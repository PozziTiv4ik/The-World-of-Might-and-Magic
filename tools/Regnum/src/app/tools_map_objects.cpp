// Regnum — правка карты: инструмент «Объекты карты» (O). Щелчок — знак или фигура под указателем (инспектор),
// Shift+щелчок — добавить знак к выделению или убрать из него, рамка по пустому месту — выделить знаки.
// Перетаскивание — переместить выделенные знаки или фигуру целиком: во время перетаскивания на карте призраки,
// правка — при отпускании, одним шагом отмены. У выбранной фигуры — ручки точек (у воды — и островов): тянуть,
// двойной щелчок по контуру — новая точка, Delete или правая кнопка — удалить точку. Delete — удалить выделенное,
// стрелки — сдвиг на пиксель экрана (Shift — на 10), Esc — снять выбор.
#include "app/tools_map.h"

namespace rg::app::mapedit {

namespace {

using platform::Key;
using tools::kDragPx;
using tools::kHitPx;
using tools::Mark;
using tools::Pts;

// Точка фигуры: ring −1 — контур или линия, иначе остров воды.
struct VKey {
  int ring = -1, index = -1;
  bool valid() const { return index >= 0; }
  bool operator==(const VKey&) const = default;
};
struct EdgeAt {
  int ring = -1, segment = -1;
  Vec2 p;
};

const std::vector<Vec2>* ringOf(const MapShape& s, int ring) {
  if (ring < 0) return &s.pts;
  return ring < int(s.holes.size()) ? &s.holes[size_t(ring)] : nullptr;
}

VKey vertexAt(const MapShape& s, Vec2 p, double tol) {
  VKey best;
  double bd = tol;
  for (int r = -1; r < int(s.holes.size()); r++) {
    const std::vector<Vec2>& pts = *ringOf(s, r);
    for (int i = 0; i < int(pts.size()); i++) {
      const double d = dist(pts[size_t(i)], p);
      if (d <= bd) {
        bd = d;
        best = {r, i};
      }
    }
  }
  return best;
}

std::optional<EdgeAt> edgeAt(const MapShape& s, Vec2 p, double tol) {
  std::optional<EdgeAt> best;
  double bd = tol;
  for (int r = -1; r < int(s.holes.size()); r++) {
    const std::vector<Vec2>& pts = *ringOf(s, r);
    const int n = int(pts.size()), ns = s.closed() ? n : n - 1;
    for (int i = 0; i < ns; i++) {
      const geo::Proj pr = geo::project(p, pts[size_t(i)], pts[size_t((i + 1) % n)]);
      const double d = std::sqrt(pr.d2);
      if (d <= bd) {
        bd = d;
        best = EdgeAt{r, i, pr.p};
      }
    }
  }
  return best;
}

Vec2 clampMap(Vec2 p) { return {clamp(p.x, 0.0, schema::kMapWidth), clamp(p.y, 0.0, schema::kMapHeight)}; }

class ObjectsTool final : public MapTool {
 public:
  void deactivate(App&) override { drag_ = {}; }

  // ---------------------------------------------------------------- указатель
  bool pointerDown(App& a, const PointerEvent& e) override {
    if (e.button == 1) return contextMenu(a, e);
    if (e.button != 0) return false;
    if (a.readOnly()) return true;
    auto sc = a.map().artScene();
    if (!sc) return true;
    const World& w = a.world();
    const double tol = a.map().view().toMapLen(kHitPx);
    // Точки выбранной фигуры.
    if (const MapShape* s = selectedShape(a)) {
      const VKey k = vertexAt(*s, e.map, tol);
      if (k.valid()) {
        selV_ = k;
        begin(e, Kind::Vertex);
        drag_.shape = a.ui.sel.id;
        drag_.v = k;
        return true;
      }
      if (e.clicks >= 2)
        if (auto ea = edgeAt(*s, e.map, tol)) {
          const Id sid = a.ui.sel.id;
          if (act(a, "Добавить точку фигуры", [&](Tx& tx) { rules::insertShapePoint(tx, sid, ea->ring, ea->segment, ea->p); })) {
            selV_ = {ea->ring, ea->segment + 1};
            begin(e, Kind::Vertex);
            drag_.shape = sid;
            drag_.v = selV_;
          }
          return true;
        }
    }
    selV_ = {};
    // Знаки (поверх фигур).
    if (Id sym = sc->symbolAt(e.map, a.map().view().toMapLen(3))) {
      std::vector<Id> cur = selectedSymbols(a);
      const bool in = std::find(cur.begin(), cur.end(), sym) != cur.end();
      if (e.mods & platform::ModShift) {
        if (in) cur.erase(std::find(cur.begin(), cur.end(), sym));
        else cur.push_back(sym);
        selectSymbols(a, cur);
        return true;
      }
      if (!in) selectSymbols(a, {sym});
      begin(e, Kind::Symbols);
      drag_.symbols = selectedSymbols(a);
      return true;
    }
    if (Id shp = sc->shapeAt(e.map, tol)) {
      a.ui.symbolGroup.clear();
      a.select(SelType::Shape, shp);
      begin(e, Kind::Shape);
      drag_.shape = shp;
      return true;
    }
    (void)w;
    begin(e, Kind::Rubber);
    drag_.add = (e.mods & platform::ModShift) != 0;
    return true;
  }

  bool pointerMove(App& a, const PointerEvent& e) override {
    if (drag_.kind == Kind::None) return false;
    if (!drag_.moved && std::hypot(e.sx - drag_.sx, e.sy - drag_.sy) < kDragPx) return true;
    drag_.moved = true;
    drag_.at = e.map;
    a.requestRedraw();
    return true;
  }

  bool pointerUp(App& a, const PointerEvent& e) override {
    if (drag_.kind == Kind::None) return false;
    Drag d = drag_;
    drag_ = {};
    if (d.moved) d.at = e.map;
    switch (d.kind) {
      case Kind::Symbols:
        if (d.moved) moveSymbols(a, d.symbols, d.at - d.start, d.symbols.size() == 1 ? "Переместить знак" : "Переместить знаки");
        break;
      case Kind::Shape:
        if (d.moved) {
          const Vec2 delta = d.at - d.start;
          const Id sid = d.shape;
          act(a, "Переместить фигуру", [&](Tx& tx) { rules::moveShape(tx, sid, delta); });
        }
        break;
      case Kind::Vertex:
        if (d.moved) {
          const Id sid = d.shape;
          const VKey k = d.v;
          const Vec2 to = clampMap(d.at);
          act(a, "Переместить точку фигуры", [&](Tx& tx) { rules::setShapePoint(tx, sid, k.ring, k.index, to); });
        }
        break;
      case Kind::Rubber: {
        if (!d.moved) {
          if (!d.add) {
            a.ui.symbolGroup.clear();
            a.clearSelection();
          }
          break;
        }
        auto sc = a.map().artScene();
        if (!sc) break;
        Box2 b;
        b.add(d.start);
        b.add(d.at);
        std::vector<Id> ids = sc->symbolsIn(b);
        if (d.add) {
          std::vector<Id> cur = selectedSymbols(a);
          for (Id id : ids)
            if (std::find(cur.begin(), cur.end(), id) == cur.end()) cur.push_back(id);
          ids = std::move(cur);
        }
        selectSymbols(a, ids);
        if (!ids.empty()) a.toast(ids.size() == 1 ? std::string("Выбран знак") : "Выбрано знаков: " + std::to_string(ids.size()), ToastKind::Info, "mountain");
        break;
      }
      default: break;
    }
    a.requestRedraw();
    return true;
  }

  // ---------------------------------------------------------------- клавиши
  bool key(App& a, const platform::Event& e) override {
    if (e.key == Key::Escape && e.mods == 0) {
      if (drag_.kind != Kind::None) {
        drag_ = {};
        return true;
      }
      if (selV_.valid()) {
        selV_ = {};
        return true;
      }
      if (a.ui.sel.type == SelType::Symbol || a.ui.sel.type == SelType::Shape) {
        selectSymbols(a, {});
        return true;
      }
      return false;
    }
    if (a.readOnly() || drag_.kind != Kind::None) return false;
    if ((e.key == Key::Delete || e.key == Key::Backspace) && e.mods == 0) {
      const MapShape* s = selectedShape(a);
      const VKey k = selV_.valid() ? selV_ : hoverV_;
      if (s && k.valid()) {
        removeVertex(a, a.ui.sel.id, k);
        return true;
      }
      return removeSelected(a);
    }
    int dx = 0, dy = 0;
    if (e.key == Key::Left) dx = -1;
    else if (e.key == Key::Right) dx = 1;
    else if (e.key == Key::Up) dy = -1;
    else if (e.key == Key::Down) dy = 1;
    if ((dx || dy) && (e.mods & ~u32(platform::ModShift)) == 0) {
      const double step = a.map().view().toMapLen(1) * ((e.mods & platform::ModShift) ? 10 : 1);
      const Vec2 delta{dx * step, dy * step};
      TxOptions opt;
      opt.coalesce = "mapobjects.nudge";
      if (std::vector<Id> ids = selectedSymbols(a); !ids.empty()) {
        moveSymbols(a, ids, delta, ids.size() == 1 ? "Сдвинуть знак" : "Сдвинуть знаки", opt);
        return true;
      }
      if (const MapShape* s = selectedShape(a)) {
        const Id sid = a.ui.sel.id;
        if (selV_.valid()) {
          const std::vector<Vec2>* r = ringOf(*s, selV_.ring);
          if (!r || selV_.index >= int(r->size())) return true;
          const Vec2 to = clampMap((*r)[size_t(selV_.index)] + delta);
          const VKey k = selV_;
          act(a, "Сдвинуть точку фигуры", [&](Tx& tx) { rules::setShapePoint(tx, sid, k.ring, k.index, to); }, opt);
        } else {
          act(a, "Сдвинуть фигуру", [&](Tx& tx) { rules::moveShape(tx, sid, delta); }, opt);
        }
        return true;
      }
    }
    return false;
  }

  // ---------------------------------------------------------------- рисование
  void drawOverlay(App& a, gfx::Canvas& c, const map::View& v) override {
    const World& w = a.world();
    const tools::Palette P = tools::palette();
    const MapShape* sel = selectedShape(a);
    if (selFor_ != a.ui.sel) {
      selFor_ = a.ui.sel;
      selV_ = {};
    }
    // Наведение на точки и контур выбранной фигуры.
    hoverV_ = {};
    hoverEdge_.reset();
    if (sel && a.ui.cursorMap && drag_.kind == Kind::None) {
      const double tol = v.toMapLen(kHitPx);
      hoverV_ = vertexAt(*sel, *a.ui.cursorMap, tol);
      if (!hoverV_.valid()) hoverEdge_ = edgeAt(*sel, *a.ui.cursorMap, tol);
    }
    // Наведённый объект (не выбранный).
    if (drag_.kind == Kind::None) {
      const Selection h = a.ui.hover;
      const std::vector<Id> selSyms = selectedSymbols(a);
      if (h.type == SelType::Symbol && std::find(selSyms.begin(), selSyms.end(), h.id) == selSyms.end())
        if (const MapSymbol* s = detail::mapSymbol(w, h.id)) outlineSymbol(c, v, *s, P.light.alpha(0.85f), 1.5f);
      if (h.type == SelType::Shape && !(a.ui.sel == h))
        if (const MapShape* s = detail::mapShape(w, h.id)) outlineShape(c, v, *s, P.light.alpha(0.8f), 1.6f);
    }
    // Выбранные знаки и их перетаскивание.
    const std::vector<Id> syms = selectedSymbols(a);
    const bool movingSyms = drag_.kind == Kind::Symbols && drag_.moved;
    const Vec2 delta = drag_.at - drag_.start;
    for (Id id : syms) {
      const MapSymbol* s = detail::mapSymbol(w, id);
      if (!s) continue;
      outlineSymbol(c, v, *s, P.accent, 2);
      if (movingSyms) {
        MapSymbol g = *s;
        g.p = clampMap(s->p + delta);
        ghostSymbol(c, v, g.kind, g.p, g.s, g.v, 0.8f);
        outlineSymbol(c, v, g, P.light.alpha(0.9f), 1.4f);
      }
    }
    if (movingSyms && !syms.empty())
      tools::strokeLine(c, Pts{v.toScreen(drag_.start), v.toScreen(drag_.at)}, false, P.light.alpha(0.8f), 1.4f, {5, 4});
    // Выбранная фигура: контур, точки, предпросмотр перетаскивания.
    if (sel) drawShape(a, c, v, *sel);
    // Рамка выделения.
    if (drag_.kind == Kind::Rubber && drag_.moved) {
      const gfx::Pt p0 = v.toScreen(drag_.start), p1 = v.toScreen(drag_.at);
      const RectF r(std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::fabs(p1.x - p0.x), std::fabs(p1.y - p0.y));
      c.fillRect(r, P.accent.alpha(0.1f));
      c.strokeRoundRect(r, 2, 1.4f, P.accent);
    }
    menu(a);
    options(a);
  }

  platform::Cursor cursor(App& a) override {
    if (drag_.kind == Kind::Rubber) return platform::Cursor::Crosshair;
    if (drag_.kind != Kind::None) return drag_.moved ? platform::Cursor::Grabbing : platform::Cursor::Arrow;
    if (hoverV_.valid()) return platform::Cursor::Move;
    if (hoverEdge_) return platform::Cursor::Crosshair;
    if (a.ui.hover.type == SelType::Symbol || a.ui.hover.type == SelType::Shape) return platform::Cursor::Grab;
    return platform::Cursor::Arrow;
  }

  const char* hint(App& a) override {
    if (a.readOnly()) return "Прошлый ход: только просмотр";
    switch (drag_.kind) {
      case Kind::Rubber: return "Отпустите — выбрать знаки в рамке";
      case Kind::Symbols:
      case Kind::Shape: return "Отпустите — поставить · Esc — отмена";
      case Kind::Vertex: return "Отпустите — поставить точку · Esc — отмена";
      default: break;
    }
    if (hoverV_.valid()) return "Тяните точку · Delete или правая кнопка — удалить";
    if (hoverEdge_) return "Двойной щелчок — новая точка";
    if (a.ui.hover.type == SelType::Symbol) return "Щелчок — выбрать · Shift — добавить к выбору · тяните — переместить";
    if (a.ui.hover.type == SelType::Shape) return "Щелчок — выбрать фигуру и её точки · тяните — переместить";
    return "Щелчок — объект карты · рамка — выбрать знаки · Delete — удалить выбранное";
  }

 private:
  enum class Kind : u8 { None, Symbols, Shape, Vertex, Rubber };
  struct Drag {
    Kind kind = Kind::None;
    bool moved = false, add = false;
    float sx = 0, sy = 0;
    Vec2 start, at;
    std::vector<Id> symbols;
    Id shape = 0;
    VKey v;
  } drag_;
  VKey selV_, hoverV_;
  std::optional<EdgeAt> hoverEdge_;
  Selection selFor_;
  // меню правой кнопки
  enum class MenuKind : u8 { Vertex, Edge, Symbols, Shape } menuKind_ = MenuKind::Symbols;
  VKey menuV_;
  std::optional<EdgeAt> menuEdge_;

  void begin(const PointerEvent& e, Kind k) {
    drag_ = {};
    drag_.kind = k;
    drag_.sx = e.sx;
    drag_.sy = e.sy;
    drag_.start = drag_.at = e.map;
  }

  static const MapShape* selectedShape(const App& a) {
    return a.ui.sel.type == SelType::Shape ? detail::mapShape(a.world(), a.ui.sel.id) : nullptr;
  }

  void moveSymbols(App& a, const std::vector<Id>& ids, Vec2 delta, const char* label, const TxOptions& opt = {}) {
    act(a, label, [&](Tx& tx) {
      for (Id id : ids) {
        const MapSymbol* s = tx.w().symbol(id);
        if (!s) continue;
        MapSymbol m = *s;
        m.p = clampMap(s->p + delta);
        // Один знак встаёт в порядок отрисовки по соседям, группа сохраняет свой.
        const double z = ids.size() == 1 ? placeZ(a, tx.w(), id, m) : s->z;
        rules::placeSymbol(tx, id, m.p, z);
      }
    }, opt);
  }

  bool removeSelected(App& a) {
    if (std::vector<Id> ids = selectedSymbols(a); !ids.empty()) {
      if (act(a, ids.size() == 1 ? "Удалить знак" : "Удалить знаки", [&](Tx& tx) {
            for (Id id : ids)
              if (tx.w().symbol(id)) rules::removeSymbol(tx, id);
          }))
        selectSymbols(a, {});
      return true;
    }
    if (a.ui.sel.type == SelType::Shape) {
      const Id sid = a.ui.sel.id;
      const MapShape* s = detail::mapShape(a.world(), sid);
      if (!s) return false;
      if (act(a, "Удалить: " + detail::mapShapeName(*s), [&](Tx& tx) { rules::removeShape(tx, sid); })) a.clearSelection();
      return true;
    }
    return false;
  }

  void removeVertex(App& a, Id sid, VKey k) {
    if (act(a, "Удалить точку фигуры", [&](Tx& tx) { rules::removeShapePoint(tx, sid, k.ring, k.index); })) {
      selV_ = {};
      hoverV_ = {};
    }
  }

  void drawShape(App& a, gfx::Canvas& c, const map::View& v, const MapShape& s) {
    const tools::Palette P = tools::palette();
    const bool moving = drag_.kind == Kind::Shape && drag_.moved;
    outlineShape(c, v, s, P.accent, 2);
    if (moving) outlineShape(c, v, s, P.light, 1.6f, drag_.at - drag_.start);
    // Перетаскивание точки: предпросмотр двух соседних звеньев, недопустимое (самопересечение) — красным.
    if (drag_.kind == Kind::Vertex && drag_.moved) {
      if (const std::vector<Vec2>* r0 = ringOf(s, drag_.v.ring); r0 && drag_.v.index < int(r0->size())) {
        std::vector<Vec2> r = *r0;
        r[size_t(drag_.v.index)] = clampMap(drag_.at);
        const bool ok = !s.closed() || geo::isSimple(r, true) || !geo::isSimple(*r0, true);
        const int n = int(r.size()), i = drag_.v.index;
        Pts seg;
        if (s.closed() || i > 0) seg.push_back(v.toScreen(r[size_t((i - 1 + n) % n)]));
        seg.push_back(v.toScreen(r[size_t(i)]));
        if (s.closed() || i + 1 < n) seg.push_back(v.toScreen(r[size_t((i + 1) % n)]));
        tools::glowLine(c, seg, false, ok ? P.light : P.danger, 2);
        tools::handleMark(c, v.toScreen(r[size_t(i)]), Mark::Dot, 5, ok ? P.accent : P.danger, P.ink, 3);
      }
    }
    // Точки (частые при мелком масштабе — мелкими метками).
    const Box2 vis = v.visibleBox().inflated(v.toMapLen(12));
    for (int ring = -1; ring < int(s.holes.size()); ring++) {
      const std::vector<Vec2>& pts = *ringOf(s, ring);
      double len = 0;
      for (size_t i = 1; i < pts.size(); i++) len += dist(pts[i - 1], pts[i]);
      const bool dense = len * v.zoom / double(std::max<size_t>(1, pts.size())) < 7;
      for (int i = 0; i < int(pts.size()); i++) {
        if (!vis.contains(pts[size_t(i)])) continue;
        const VKey k{ring, i};
        if (drag_.kind == Kind::Vertex && drag_.moved && k == drag_.v) continue;
        const gfx::Pt sp = v.toScreen(pts[size_t(i)]);
        if (k == selV_) {
          c.fillCircle(sp.x, sp.y, 12, P.accent.alpha(0.22f));
          tools::handleMark(c, sp, Mark::Dot, 6, P.light, P.ink, 0);
          tools::handleMark(c, sp, Mark::Dot, 4.2f, P.accent, P.ink.alpha(0), 0);
        } else if (k == hoverV_) {
          tools::handleMark(c, sp, Mark::Dot, 5.2f, P.accent, P.ink, 3);
        } else if (dense) {
          tools::handleMark(c, sp, Mark::Dot, 1.7f, P.light, P.ink.alpha(0.6f));
        } else {
          tools::handleMark(c, sp, Mark::Dot, 3.2f, P.light, P.ink.alpha(0.85f));
        }
      }
    }
    if (hoverEdge_ && drag_.kind == Kind::None) tools::insertGhost(c, v.toScreen(hoverEdge_->p));
    (void)a;
  }

  // ---- меню правой кнопки
  bool contextMenu(App& a, const PointerEvent& e) {
    if (a.readOnly()) return false;
    auto sc = a.map().artScene();
    if (!sc) return false;
    const double tol = a.map().view().toMapLen(kHitPx);
    menuV_ = {};
    menuEdge_.reset();
    if (const MapShape* s = selectedShape(a)) {
      if (VKey k = vertexAt(*s, e.map, tol); k.valid()) {
        selV_ = menuV_ = k;
        menuKind_ = MenuKind::Vertex;
        ui::openContextMenu("mapobjects.menu");
        return true;
      }
      if (auto ea = edgeAt(*s, e.map, tol)) {
        menuEdge_ = ea;
        menuKind_ = MenuKind::Edge;
        ui::openContextMenu("mapobjects.menu");
        return true;
      }
    }
    if (Id sym = sc->symbolAt(e.map, a.map().view().toMapLen(3))) {
      std::vector<Id> cur = selectedSymbols(a);
      if (std::find(cur.begin(), cur.end(), sym) == cur.end()) selectSymbols(a, {sym});
      menuKind_ = MenuKind::Symbols;
      ui::openContextMenu("mapobjects.menu");
      return true;
    }
    if (Id shp = sc->shapeAt(e.map, tol)) {
      a.ui.symbolGroup.clear();
      a.select(SelType::Shape, shp);
      menuKind_ = MenuKind::Shape;
      ui::openContextMenu("mapobjects.menu");
      return true;
    }
    return false;
  }

  void menu(App& a) {
    if (!ui::beginMenu("mapobjects.menu")) return;
    const World& w = a.world();
    switch (menuKind_) {
      case MenuKind::Vertex:
        ui::menuHeader("Точка фигуры");
        if (ui::menuItem("Удалить точку", {.icon = "trash", .shortcut = {Key::Delete, 0}, .danger = true}) && a.ui.sel.type == SelType::Shape)
          removeVertex(a, a.ui.sel.id, menuV_);
        a.markUi("mapobjects.menu.delete-point");
        break;
      case MenuKind::Edge:
        ui::menuHeader("Контур фигуры");
        if (ui::menuItem("Добавить точку", {.icon = "plus"}) && menuEdge_ && a.ui.sel.type == SelType::Shape) {
          const Id sid = a.ui.sel.id;
          const EdgeAt ea = *menuEdge_;
          if (act(a, "Добавить точку фигуры", [&](Tx& tx) { rules::insertShapePoint(tx, sid, ea.ring, ea.segment, ea.p); }))
            selV_ = {ea.ring, ea.segment + 1};
        }
        a.markUi("mapobjects.menu.insert");
        break;
      case MenuKind::Symbols: {
        const std::vector<Id> ids = selectedSymbols(a);
        const MapSymbol* s = ids.size() == 1 ? detail::mapSymbol(w, ids[0]) : nullptr;
        ui::menuHeader(s ? symbolName(s->kind) : "Знаков: " + std::to_string(ids.size()));
        if (ui::menuItem("На передний план", {.icon = "arrow-up"})) restack(a, ids, true);
        if (ui::menuItem("На задний план", {.icon = "arrow-down"})) restack(a, ids, false);
        ui::menuSeparator();
        if (ui::menuItem(ids.size() == 1 ? "Удалить знак" : "Удалить знаки", {.icon = "trash", .shortcut = {Key::Delete, 0}, .danger = true}))
          removeSelected(a);
        a.markUi("mapobjects.menu.delete");
        break;
      }
      case MenuKind::Shape: {
        const MapShape* s = selectedShape(a);
        ui::menuHeader(s ? detail::mapShapeName(*s) : std::string("Фигура"));
        if (ui::menuItem("Удалить", {.icon = "trash", .shortcut = {Key::Delete, 0}, .danger = true})) removeSelected(a);
        a.markUi("mapobjects.menu.delete");
        break;
      }
    }
    ui::endMenu();
  }

  // ---- панель параметров
  void options(App& a) {
    const World& w = a.world();
    std::string what;
    const char* icon = "tool-map-objects";
    const std::vector<Id> syms = selectedSymbols(a);
    if (syms.size() > 1) {
      what = "Знаков: " + std::to_string(syms.size());
    } else if (syms.size() == 1) {
      if (const MapSymbol* s = detail::mapSymbol(w, syms[0])) {
        what = symbolName(s->kind);
        icon = symbolIcon(s->kind);
      }
    } else if (const MapShape* s = selectedShape(a)) {
      const size_t n = s->pts.size();
      what = detail::mapShapeName(*s) + " · " + std::to_string(n) + " " + plural(int(n), "точка", "точки", "точек");
      icon = shapeIcon(s->kind);
    }
    const char* empty = "Щелчок — объект карты, рамка — знаки";
    float width = (what.empty() ? tools::textW(empty, ui::Font::Small) : tools::textW(what, ui::Font::Strong) + 34) + 6 + 30 + 6 + 18;
    tools::OptionsBar bar(a, "tool-map-objects", "Объекты карты", width);
    if (!bar) return;
    if (what.empty()) ui::label(empty, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    else ui::tag(what, ui::Tone::Accent, icon);
    a.markUi("tool.options.selection");
    ui::flex();
    {
      ui::Disabled dis(a.readOnly());
      const bool can = !syms.empty() || selectedShape(a);
      if (ui::iconButton("trash", "Удалить выбранное", {.disabled = !can})) removeSelected(a);
      ui::tooltip("Удалить выбранное", {Key::Delete, 0});
      a.markUi("tool.options.delete");
    }
    ui::icon("help", ui::Ink::Muted, 18,
             "Щелчок — выбрать · Shift+щелчок — добавить знак · рамка — выбрать знаки\n"
             "Тяните — переместить · стрелки — сдвиг (Shift — ×10) · Delete — удалить\n"
             "У выбранной фигуры: тяните точки, двойной щелчок по контуру — новая точка");
  }
};

ToolDef objectsDef() {
  ToolDef d{ToolId::MapObjects, "tool-map-objects", "Объекты карты: выбор и перемещение", "O", false, [] { return std::make_unique<ObjectsTool>(); }, 200};
  d.mapMode = true;
  return d;
}
ToolReg regObjects(objectsDef());

}  // namespace

void registerObjectsTool() { ToolReg r(objectsDef()); }

}  // namespace rg::app::mapedit
