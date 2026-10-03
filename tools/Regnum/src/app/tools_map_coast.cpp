// Regnum — правка карты: инструмент «Берег» (Y). Ручки береговой линии у указателя (при крупном масштабе — все
// видимые): точки — круги, узлы — квадраты, стыки с границами провинций — ромбы (двигаются вместе с концом
// границы). Перетаскивание — предпросмотр, правка при отпускании одним шагом отмены (берег не может пересечь другие
// линии). Двойной щелчок по берегу — новая точка, Delete или правая кнопка — удалить точку, стрелки — сдвиг на
// пиксель экрана (Shift — на 10), Esc — снять выбор. Суша и море целыми областями — инструменты «Добавить сушу»
// и «Убрать сушу».
#include "app/tools_map.h"

namespace rg::app::mapedit {

namespace {

using platform::Key;
using tools::kDragPx;
using tools::kHitPx;
using tools::Mark;
using tools::Pts;

constexpr float kNearPx = 170;        // ручки берега показываются в этом радиусе от указателя
constexpr size_t kAllVisible = 1500;  // столько и меньше видимых точек берега — показываются все

bool handleExists(const World& w, const geo::Handle& h) {
  if (h.kind == geo::Handle::Node) return w.nodes.get(h.node) != nullptr;
  if (h.kind == geo::Handle::Point) {
    const Edge* e = w.edges.get(h.edge);
    return e && h.index >= 0 && h.index < int(e->pts.size());
  }
  return false;
}

bool inMap(Vec2 p) { return p.x > 0.5 && p.y > 0.5 && p.x < schema::kMapWidth - 0.5 && p.y < schema::kMapHeight - 0.5; }

// Узел — стык берега с границей провинции.
bool junction(const World& w, Id node) { return geo::isCoastJunction(w, node); }

// Соседние вершины ручки (для предпросмотра).
std::vector<Vec2> neighbours(const World& w, const geo::Handle& h) {
  std::vector<Vec2> out;
  if (h.kind == geo::Handle::Point) {
    if (const Edge* e = w.edges.get(h.edge)) {
      const std::vector<Vec2> co = geo::edgeCoords(w, *e);
      const size_t i = size_t(h.index) + 1;
      if (i >= 1 && i + 1 < co.size()) {
        out.push_back(co[i - 1]);
        out.push_back(co[i + 1]);
      }
    }
  } else if (h.kind == geo::Handle::Node) {
    w.edges.each([&](const Edge& e) {
      if (e.a != h.node && e.b != h.node) return;
      const std::vector<Vec2> co = geo::edgeCoords(w, e);
      if (co.size() < 2) return;
      if (e.a == h.node) out.push_back(co[1]);
      if (e.b == h.node) out.push_back(co[co.size() - 2]);
    });
  }
  return out;
}

class CoastTool final : public MapTool {
 public:
  void deactivate(App&) override { drag_ = {}; }

  bool pointerDown(App& a, const PointerEvent& e) override {
    if (a.readOnly()) return true;
    const World& w = a.world();
    const double tol = a.map().view().toMapLen(kHitPx);
    geo::Handle h = hit(w, e.map, tol);
    if (e.button == 1) return contextMenu(a, e, h, tol);
    if (e.button != 0) return false;
    if (h) {
      selH_ = h;
      beginDrag(w, h, e);
      return true;
    }
    if (e.clicks >= 2)
      if (auto eh = geo::hitEdge(w, e.map, tol, 0, true)) {
        geo::Handle nh;
        if (a.act("Добавить точку берега", [&](Tx& tx) { nh = geo::insertPoint(tx, eh->edge, eh->segment, eh->p, true); })) {
          selH_ = nh;
          beginDrag(a.world(), nh, e);
        }
        return true;
      }
    selH_ = {};
    return true;
  }

  bool pointerMove(App& a, const PointerEvent& e) override {
    if (!drag_.active) return false;
    if (!drag_.moved && std::hypot(e.sx - drag_.sx, e.sy - drag_.sy) < kDragPx) return true;
    drag_.moved = true;
    drag_.at = e.map;
    const World& w = a.world();
    drag_.valid = handleExists(w, drag_.h) && inMap(e.map) && geo::canMove(w, drag_.h, e.map, true);
    a.requestRedraw();
    return true;
  }

  bool pointerUp(App& a, const PointerEvent&) override {
    if (!drag_.active) return false;
    Drag d = drag_;
    drag_ = {};
    if (!d.moved) return true;
    if (!d.valid) {
      a.toast("Точку берега нельзя поставить сюда: берег пересечёт другую линию", ToastKind::Warning, "warning");
      return true;
    }
    const geo::Handle h = d.h;
    const Vec2 to = d.at;
    a.act("Переместить точку берега", [&](Tx& tx) { geo::moveHandle(tx, h, to, true); });
    return true;
  }

  bool key(App& a, const platform::Event& e) override {
    if (e.key == Key::Escape && e.mods == 0) {
      if (drag_.active) {
        drag_ = {};
        return true;
      }
      if (selH_) {
        selH_ = {};
        return true;
      }
      return false;
    }
    if (a.readOnly() || drag_.active) return false;
    if ((e.key == Key::Delete || e.key == Key::Backspace) && e.mods == 0) {
      const geo::Handle h = selH_ ? selH_ : hover_;
      if (!h) return false;
      removeHandle(a, h);
      return true;
    }
    int dx = 0, dy = 0;
    if (e.key == Key::Left) dx = -1;
    else if (e.key == Key::Right) dx = 1;
    else if (e.key == Key::Up) dy = -1;
    else if (e.key == Key::Down) dy = 1;
    if ((dx || dy) && selH_ && (e.mods & ~u32(platform::ModShift)) == 0) {
      const World& w = a.world();
      if (!handleExists(w, selH_)) return true;
      const double step = a.map().view().toMapLen(1) * ((e.mods & platform::ModShift) ? 10 : 1);
      const Vec2 to = geo::handlePos(w, selH_) + Vec2{dx * step, dy * step};
      if (!inMap(to) || !geo::canMove(w, selH_, to, true)) {
        a.toast("Точку берега нельзя поставить сюда: берег пересечёт другую линию", ToastKind::Warning, "warning");
        return true;
      }
      TxOptions opt;
      opt.coalesce = "coast.nudge";
      const geo::Handle h = selH_;
      a.act("Сдвинуть точку берега", [&](Tx& tx) { geo::moveHandle(tx, h, to, true); }, opt);
      return true;
    }
    return false;
  }

  void drawOverlay(App& a, gfx::Canvas& c, const map::View& v) override {
    const World& w = a.world();
    if (selH_ && !handleExists(w, selH_)) selH_ = {};
    hover_ = {};
    hoverEdge_.reset();
    if (a.ui.cursorMap && !drag_.active) {
      const double tol = v.toMapLen(kHitPx);
      hover_ = hit(w, *a.ui.cursorMap, tol);
      if (!hover_) hoverEdge_ = geo::hitEdge(w, *a.ui.cursorMap, tol, 0, true);
    }
    drawHandles(a, c, v);
    menu(a);
    options(a);
  }

  platform::Cursor cursor(App&) override {
    if (drag_.active) return drag_.moved && !drag_.valid ? platform::Cursor::NotAllowed : platform::Cursor::Grabbing;
    if (hover_) return platform::Cursor::Move;
    if (hoverEdge_) return platform::Cursor::Crosshair;
    return platform::Cursor::Arrow;
  }

 private:
  struct Drag {
    bool active = false, moved = false, valid = true;
    geo::Handle h;
    Vec2 start, at;
    float sx = 0, sy = 0;
  } drag_;
  geo::Handle selH_, hover_;
  std::optional<geo::EdgeHit> hoverEdge_;
  geo::Handle menuH_;
  std::optional<geo::EdgeHit> menuEdge_;
  bool menuOnHandle_ = false;

  static geo::Handle hit(const World& w, Vec2 p, double tol) {
    geo::Handle h = geo::hitHandle(w, p, tol, 0, true);
    if (!h || geo::handleLocked(w, h, true)) return {};
    return h;
  }

  void beginDrag(const World& w, const geo::Handle& h, const PointerEvent& e) {
    drag_ = {};
    drag_.active = true;
    drag_.h = h;
    drag_.start = drag_.at = geo::handlePos(w, h);
    drag_.sx = e.sx;
    drag_.sy = e.sy;
  }

  void removeHandle(App& a, const geo::Handle& h) {
    if (a.act("Удалить точку берега", [&](Tx& tx) { geo::deletePoint(tx, h, true); })) {
      if (selH_ == h) selH_ = {};
      hover_ = {};
    }
  }

  bool contextMenu(App& a, const PointerEvent& e, const geo::Handle& h, double tol) {
    menuH_ = {};
    menuEdge_.reset();
    if (h) {
      selH_ = menuH_ = h;
      menuOnHandle_ = true;
    } else if (auto eh = geo::hitEdge(a.world(), e.map, tol, 0, true)) {
      menuEdge_ = eh;
      menuOnHandle_ = false;
    } else {
      return false;
    }
    ui::openContextMenu("coast.menu");
    return true;
  }

  void menu(App& a) {
    if (!ui::beginMenu("coast.menu")) return;
    const World& w = a.world();
    if (menuOnHandle_) {
      const bool junc = menuH_.kind == geo::Handle::Node && junction(w, menuH_.node);
      ui::menuHeader(menuH_.kind == geo::Handle::Point ? "Точка берега" : junc ? "Стык берега с границей" : "Узел берега");
      if (ui::menuItem("Удалить точку", {.icon = "trash", .shortcut = {Key::Delete, 0}, .danger = true, .disabled = !handleExists(w, menuH_) || junc}))
        removeHandle(a, menuH_);
      a.markUi("coast.menu.delete");
    } else {
      ui::menuHeader("Берег");
      if (ui::menuItem("Добавить точку", {.icon = "plus"}) && menuEdge_ && w.edges.get(menuEdge_->edge)) {
        const geo::EdgeHit eh = *menuEdge_;
        geo::Handle nh;
        if (a.act("Добавить точку берега", [&](Tx& tx) { nh = geo::insertPoint(tx, eh.edge, eh.segment, eh.p, true); })) selH_ = nh;
      }
      a.markUi("coast.menu.insert");
    }
    ui::endMenu();
  }

  void options(App& a) {
    const World& w = a.world();
    const bool junc = selH_ && selH_.kind == geo::Handle::Node && junction(w, selH_.node);
    const std::string kind = !selH_ ? "" : selH_.kind == geo::Handle::Point ? "Точка берега" : junc ? "Стык с границей" : "Узел берега";
    float width = (kind.empty() ? 0 : tools::textW(kind, ui::Font::Strong) + 34 + 6) + 30;
    tools::OptionsBar bar(a, "tool-coast", "Берег", width);
    if (!bar) return;
    if (!kind.empty()) ui::tag(kind, junc ? ui::Tone::Info : ui::Tone::Accent, junc ? "anchor" : "tool-coast");
    a.markUi("tool.options.handle");
    ui::flex();
    {
      ui::Disabled dis(a.readOnly());
      const bool can = selH_ && !junc && handleExists(w, selH_);
      if (ui::iconButton("trash", "Удалить точку", {.disabled = !can})) removeHandle(a, selH_);
      ui::tooltip("Удалить точку", {Key::Delete, 0});
      a.markUi("tool.options.delete");
    }
  }

  void drawHandles(App& a, gfx::Canvas& c, const map::View& v) {
    const World& w = a.world();
    const tools::Palette P = tools::palette();
    const Box2 vis = v.visibleBox().inflated(v.toMapLen(12));
    // Точки берега: все видимые, если их немного, иначе — у указателя.
    struct PM {
      geo::Handle h;
      gfx::Pt s;
      bool dense;
    };
    std::vector<PM> pts;
    std::vector<Id> nodes;
    std::optional<Vec2> cur = a.ui.cursorMap;
    const double nearR = v.toMapLen(kNearPx);
    size_t visible = 0;
    w.edges.each([&](const Edge& e) {
      if (e.kind != EdgeKind::Coast) return;
      for (Vec2 p : e.pts)
        if (vis.contains(p)) visible++;
    });
    const bool all = visible <= kAllVisible;
    w.edges.each([&](const Edge& e) {
      if (e.kind != EdgeKind::Coast) return;
      std::vector<Vec2> co = geo::edgeCoords(w, e);
      double len = 0;
      for (size_t i = 1; i < co.size(); i++) len += dist(co[i - 1], co[i]);
      const bool dense = len * v.zoom / double(e.pts.size() + 1) < 7;
      auto shown = [&](Vec2 p) { return vis.contains(p) && (all || (cur && dist(p, *cur) <= nearR)); };
      for (Id n : {e.a, e.b})
        if (const Node* nd = w.nodes.get(n); nd && shown(nd->p)) nodes.push_back(n);
      for (int i = 0; i < int(e.pts.size()); i++)
        if (shown(e.pts[size_t(i)])) pts.push_back({geo::Handle{geo::Handle::Point, 0, e.id, i}, v.toScreen(e.pts[size_t(i)]), dense});
    });
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    // Наведённый отрезок берега и место новой точки.
    if (hoverEdge_ && !hover_ && !drag_.active)
      if (const Edge* e = w.edges.get(hoverEdge_->edge)) {
        const std::vector<Vec2> co = geo::edgeCoords(w, *e);
        const int s = hoverEdge_->segment;
        if (s >= 0 && s + 1 < int(co.size())) {
          const Pts seg{v.toScreen(co[size_t(s)]), v.toScreen(co[size_t(s + 1)])};
          tools::strokeLine(c, seg, false, P.info.alpha(0.3f), 10);
          tools::strokeLine(c, seg, false, P.ink.alpha(0.5f), 4.6f);
          tools::strokeLine(c, seg, false, P.light, 2.6f);
          tools::insertGhost(c, v.toScreen(hoverEdge_->p));
        }
      }
    // Перетаскивание: соседние звенья в новом положении.
    if (drag_.active && drag_.moved && handleExists(w, drag_.h)) {
      const gfx::Pt at = v.toScreen(drag_.at), s0 = v.toScreen(drag_.start);
      tools::strokeLine(c, Pts{s0, at}, false, P.ink.alpha(0.6f), 1.2f, {3, 3.5f});
      for (Vec2 nb : neighbours(w, drag_.h)) tools::glowLine(c, Pts{v.toScreen(nb), at}, false, drag_.valid ? P.light : P.danger, 2);
      tools::handleMark(c, at, drag_.h.kind == geo::Handle::Point ? Mark::Dot : Mark::Square, 5.4f, drag_.valid ? P.accent : P.danger, P.ink, 3);
    }
    auto hot = [&](const geo::Handle& h) { return h == hover_ || h == selH_ || (drag_.active && h == drag_.h); };
    for (const PM& m : pts) {
      if (hot(m.h)) continue;
      if (m.dense) tools::handleMark(c, m.s, Mark::Dot, 1.7f, P.light, P.ink.alpha(0.6f));
      else tools::handleMark(c, m.s, Mark::Dot, 3.2f, P.light, P.ink.alpha(0.85f));
    }
    for (Id n : nodes) {
      const geo::Handle h{geo::Handle::Node, n, 0, -1};
      if (hot(h) || geo::handleLocked(w, h, true)) continue;
      const gfx::Pt s = v.toScreen(w.nodes.get(n)->p);
      if (junction(w, n)) tools::handleMark(c, s, Mark::Diamond, 4.6f, P.info.lighten(0.25f), P.ink.alpha(0.9f));
      else tools::handleMark(c, s, Mark::Square, 4.4f, P.light, P.ink.alpha(0.9f));
    }
    auto mark = [&](const geo::Handle& h, bool hovered, bool selected) {
      if (!handleExists(w, h)) return;
      const bool junc = h.kind == geo::Handle::Node && junction(w, h.node);
      const Mark m = h.kind == geo::Handle::Point ? Mark::Dot : junc ? Mark::Diamond : Mark::Square;
      const float r = h.kind == geo::Handle::Point ? 4.8f : 5.6f;
      const gfx::Pt sp = v.toScreen(geo::handlePos(w, h));
      if (selected) {
        c.fillCircle(sp.x, sp.y, r + 9, P.accent.alpha(0.22f));
        tools::handleMark(c, sp, m, r + 2.2f, P.light, P.ink, 0);
        tools::handleMark(c, sp, m, r, P.accent, P.ink.alpha(0.0f), 0);
      } else {
        tools::handleMark(c, sp, m, hovered ? r + 0.8f : r, hovered ? P.accent : P.light, P.ink, hovered ? 3.f : 0.f);
      }
    };
    if (!(drag_.active && drag_.moved)) {
      if (selH_ && selH_ != hover_) mark(selH_, false, true);
      if (hover_) mark(hover_, true, hover_ == selH_);
    }
  }
};

ToolDef coastDef() {
  ToolDef d{ToolId::Coast, "tool-coast", "Берег: точки береговой линии", "Y", false, [] { return std::make_unique<CoastTool>(); }, 270};
  d.mapMode = true;
  return d;
}
ToolReg regCoast(coastDef());

}  // namespace

void registerCoastTool() { ToolReg r(coastDef()); }

}  // namespace rg::app::mapedit
