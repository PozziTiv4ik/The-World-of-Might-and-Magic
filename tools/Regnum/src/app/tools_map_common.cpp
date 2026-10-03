// Regnum — правка карты: общие помощники инструментов и инспектора объектов карты (см. tools_map.h).
#include "app/tools_map.h"

#include "map/basemap.h"

namespace rg::app::mapedit {

const map::art::Objects* base(const App& a) { return a.basemap() ? &a.basemap()->objects() : nullptr; }

bool act(App& a, std::string_view label, const std::function<void(Tx&)>& fn, const TxOptions& opt) {
  const map::art::Objects* b = base(a);
  return a.act(label, [&](Tx& tx) {
    if (b) rules::ensureMapObjects(tx, b->symbols, b->shapes);
    else rules::ensureMapObjects(tx, {}, {});
    fn(tx);
  }, opt);
}

double placeZ(const App& a, const World& w, Id self, const MapSymbol& s) {
  auto sc = const_cast<App&>(a).map().artScene();
  if (!sc) return s.z;
  const Box2 box = map::art::symbolBox(s);
  std::vector<u32> idx;
  sc->index().symbols(box, idx);
  double below = -kInf, above = kInf;
  for (u32 i : idx) {
    const Id id = sc->symbolIds()[i];
    if (id == self) continue;
    const MapSymbol* o = detail::mapSymbol(w, id);
    if (!o || !map::art::symbolBox(*o).intersects(box)) continue;
    if (o->p.y <= s.p.y) below = std::max(below, o->z);
    else above = std::min(above, o->z);
  }
  if (below == -kInf && above == kInf) return s.z;
  if (below == -kInf) return above - 1;
  if (above == kInf) return below + 1;
  if (below < above) return below + (above - below) * 0.5;
  return below + 1e-3;   // противоречие соседей: выше тех, кто стоит выше по карте
}

double topZ(const App& a, const World& w) {
  double z = 0;
  if (w.ownMapObjects()) {
    w.symbols.each([&](const MapSymbol& s) { z = std::max(z, s.z); });
  } else if (const map::art::Objects* b = base(a)) {
    for (const MapSymbol& s : b->symbols) z = std::max(z, s.z);
  }
  return z;
}

void restack(App& a, const std::vector<Id>& ids, bool front) {
  auto sc = a.map().artScene();
  if (!sc || ids.empty()) return;
  act(a, front ? "Знак на передний план" : "Знак на задний план", [&](Tx& tx) {
    for (Id id : ids) {
      const MapSymbol* s = tx.w().symbol(id);
      if (!s) continue;
      const Box2 box = map::art::symbolBox(*s);
      std::vector<u32> idx;
      sc->index().symbols(box, idx);
      double z = s->z;
      for (u32 i : idx) {
        const MapSymbol* o = tx.w().symbol(sc->symbolIds()[i]);
        if (!o || o->id == id || !map::art::symbolBox(*o).intersects(box)) continue;
        z = front ? std::max(z, o->z + 1) : std::min(z, o->z - 1);
      }
      rules::placeSymbol(tx, id, s->p, z);
    }
  });
}

const char* symbolName(SymbolKind k) {
  switch (k) {
    case SymbolKind::Mountain: return "Гора";
    case SymbolKind::Peak: return "Крупная гора";
    case SymbolKind::Castle: return "Замок";
    case SymbolKind::Tower: return "Башня";
    default: return "Знак";
  }
}

const char* shapeName(ShapeKind k) {
  switch (k) {
    case ShapeKind::Water: return "Озеро";
    case ShapeKind::Islet: return "Островок";
    case ShapeKind::Wall: return "Стена";
    case ShapeKind::River: return "Река";
    default: return "Фигура";
  }
}

const char* symbolIcon(SymbolKind k) {
  switch (k) {
    case SymbolKind::Castle: return "castle";
    case SymbolKind::Tower: return "tower";
    default: return "mountain";
  }
}

const char* shapeIcon(ShapeKind k) {
  switch (k) {
    case ShapeKind::Water: return "tool-lake";
    case ShapeKind::Islet: return "island";
    case ShapeKind::Wall: return "tool-wall";
    case ShapeKind::River: return "tool-river";
    default: return "map";
  }
}

std::vector<Id> selectedSymbols(const App& a) {
  if (a.ui.sel.type != SelType::Symbol || !a.ui.sel.id) return {};
  const auto& g = a.ui.symbolGroup;
  if (g.empty() || std::find(g.begin(), g.end(), a.ui.sel.id) == g.end()) return {a.ui.sel.id};
  std::vector<Id> out;
  for (Id id : g)
    if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
  return out;
}

void selectSymbols(App& a, const std::vector<Id>& ids) {
  a.ui.symbolGroup.clear();
  if (ids.empty()) {
    a.clearSelection();
    return;
  }
  if (ids.size() > 1) a.ui.symbolGroup = ids;
  a.select(SelType::Symbol, ids.back());
}

void outlineSymbol(gfx::Canvas& c, const map::View& v, const MapSymbol& s, Color col, float width) {
  const Box2 b = map::art::symbolBox(s);
  const gfx::Pt p0 = v.toScreen(Vec2(b.x0, b.y0)), p1 = v.toScreen(Vec2(b.x1, b.y1));
  const RectF r(p0.x - 2, p0.y - 2, p1.x - p0.x + 4, p1.y - p0.y + 4);
  c.strokeRoundRect(r.expand(1), 3, width + 2, tools::palette().ink.alpha(0.55f));
  c.strokeRoundRect(r, 3, width, col);
}

void outlineShape(gfx::Canvas& c, const map::View& v, const MapShape& s, Color col, float width, Vec2 shift) {
  auto pts = [&](const std::vector<Vec2>& in) {
    tools::Pts out;
    out.reserve(in.size());
    for (Vec2 p : in) out.push_back(v.toScreen(p + shift));
    return out;
  };
  if (s.closed()) {
    tools::glowLine(c, pts(s.pts), true, col, width);
    for (const auto& h : s.holes) tools::glowLine(c, pts(h), true, col, width, true);
  } else {
    const float w = std::max(width, float(s.w * v.zoom));
    tools::strokeLine(c, pts(s.pts), false, col.alpha(0.28f), w + 6);
    tools::glowLine(c, pts(s.pts), false, col, width);
  }
}

void ghostSymbol(gfx::Canvas& c, const map::View& v, SymbolKind kind, Vec2 at, float scale, u8 variant, float alpha) {
  c.save();
  c.setOpacity(alpha);
  map::art::drawSymbol(c, map::art::symOf(kind), v.toScreen(at), float(v.zoom * scale), variant);
  c.restore();
}

}  // namespace rg::app::mapedit
