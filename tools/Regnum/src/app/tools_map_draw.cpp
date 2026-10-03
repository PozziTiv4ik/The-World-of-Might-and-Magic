// Regnum — правка карты: новые объекты. «Знак» (S): щелчок — гора, крупная гора, замок или башня под указателем,
// протяжка — кисть (знаки через шаг в ширину значка: горный хребет одним шагом отмены). «Озеро» (W) — контур,
// «Река» (Shift+W) и «Стена» (Q) — линии заданной ширины, «Добавить сушу» (N) и «Убрать сушу» (Shift+N) —
// контур, внутри которого суша или море (берег перестраивается, провинции другого типа теряют площадь).
// Контур и линия вводятся как в инструментах провинций: щелчки — вершины, протяжка — от руки, Enter или двойной
// щелчок — готово, Backspace — убрать вершину, Esc — отмена.
#include "app/tools_map.h"

namespace rg::app::mapedit {

namespace {

using platform::Key;
using Result = tools::Sketch::Result;

// ---------------------------------------------------------------- знак
// Параметры инструмента живут всю сессию: вид и размер последнего поставленного знака.
struct SymbolChoice {
  SymbolKind kind = SymbolKind::Mountain;
  u8 variant = 0;
  float scale = 1;
};
SymbolChoice gChoice;

struct KindItem {
  SymbolKind kind;
  u8 variant;
  const char* label;
  const char* tip;
};
constexpr KindItem kKinds[] = {
    {SymbolKind::Mountain, 0, "Гора", "Гора со светлой вершиной"},
    {SymbolKind::Mountain, 1, "Гора 2", "Гора с тёмной вершиной"},
    {SymbolKind::Peak, 0, "Крупная", "Крупная гора"},
    {SymbolKind::Castle, 0, "Замок", "Замок"},
    {SymbolKind::Tower, 0, "Башня", "Башня"},
};

class SymbolTool final : public MapTool {
 public:
  void deactivate(App&) override { stroke_.clear(); }

  bool pointerDown(App& a, const PointerEvent& e) override {
    if (e.button != 0) return false;
    if (a.readOnly()) return true;
    stroke_ = {e.map};
    a.requestRedraw();
    return true;
  }

  bool pointerMove(App& a, const PointerEvent& e) override {
    if (stroke_.empty()) return false;
    if (!ui::mouse().down[0]) return true;
    // Кисть: следующий знак — через шаг в ширину значка.
    const double step = spacing();
    Vec2 last = stroke_.back();
    while (dist(last, e.map) >= step) {
      last = last + (e.map - last) * (step / dist(last, e.map));
      stroke_.push_back(last);
    }
    a.requestRedraw();
    return true;
  }

  bool pointerUp(App& a, const PointerEvent&) override {
    if (stroke_.empty()) return false;
    std::vector<Vec2> pts = std::move(stroke_);
    stroke_.clear();
    place(a, pts);
    return true;
  }

  bool key(App& a, const platform::Event& e) override {
    if (e.key == Key::Escape && e.mods == 0 && !stroke_.empty()) {
      stroke_.clear();
      a.requestRedraw();
      return true;
    }
    return false;
  }

  void drawOverlay(App& a, gfx::Canvas& c, const map::View& v) override {
    if (!stroke_.empty()) {
      for (Vec2 p : stroke_) ghostSymbol(c, v, gChoice.kind, p, gChoice.scale, gChoice.variant, 0.85f);
    } else if (a.ui.cursorMap && !a.readOnly()) {
      ghostSymbol(c, v, gChoice.kind, *a.ui.cursorMap, gChoice.scale, gChoice.variant, 0.6f);
      tools::cursorBadge(c, v.toScreen(*a.ui.cursorMap), "plus", tools::palette().accent);
    }
    options(a);
  }

  platform::Cursor cursor(App&) override { return platform::Cursor::Crosshair; }
  const char* hint(App&) override {
    return stroke_.size() > 1 ? "Отпустите — поставить знаки" : "Щелчок — поставить знак · протяжка — кисть (хребет) · вид и размер — на панели";
  }

 private:
  std::vector<Vec2> stroke_;

  static double spacing() {
    const RectF b = map::art::symbolBounds(map::art::symOf(gChoice.kind));
    return std::max(2.0, double(b.w) * gChoice.scale * 0.75);
  }

  static void place(App& a, const std::vector<Vec2>& pts) {
    if (pts.empty()) return;
    std::vector<Id> added;
    const std::string label = pts.size() == 1 ? std::string("Поставить: ") + symbolName(gChoice.kind) : std::string("Знаки карты: ") + std::to_string(pts.size());
    act(a, label, [&](Tx& tx) {
      double top = topZ(a, tx.w());
      for (Vec2 p : pts) {
        MapSymbol s;
        s.kind = gChoice.kind;
        s.v = gChoice.variant;
        s.s = gChoice.scale;
        s.p = p;
        s.z = ++top;
        s.z = placeZ(a, tx.w(), 0, s);
        added.push_back(rules::addSymbol(tx, s));
      }
    });
    if (!added.empty()) selectSymbols(a, added);
  }

  void options(App& a) {
    float segW = 0;
    for (const KindItem& k : kKinds) segW += tools::textW(k.label, ui::Font::Small) + 22;
    const float width = segW + 8 + 118 + 6 + 18;
    tools::OptionsBar bar(a, "tool-symbol", "Знак", width);
    if (!bar) return;
    int idx = 0;
    for (int i = 0; i < int(std::size(kKinds)); i++)
      if (kKinds[i].kind == gChoice.kind && kKinds[i].variant == gChoice.variant) idx = i;
    std::vector<ui::Segment> segs;
    for (const KindItem& k : kKinds) segs.push_back(ui::Segment{nullptr, k.label, k.tip});
    if (ui::segmented("kind", idx, std::span<const ui::Segment>(segs), {.size = ui::Size::Small, .fill = false})) {
      gChoice.kind = kKinds[idx].kind;
      gChoice.variant = kKinds[idx].variant;
    }
    a.markUi("tool.options.kind");
    ui::numberField("scale", gChoice.scale,
                    {.min = schema::kMinSymbolScale, .max = schema::kMaxSymbolScale, .step = 0.1, .digits = 2, .label = "×", .steppers = true,
                     .tooltip = "Размер знака (1 — как на карте)"});
    a.markUi("tool.options.scale");
    ui::icon("help", ui::Ink::Muted, 18, "Щелчок — один знак\nПротяжка — кисть: знаки вдоль пути (горный хребет)\nНовый знак встаёт за знаками ниже по карте");
  }
};

// ---------------------------------------------------------------- контуры и линии
class MapSketchTool : public MapTool {
 public:
  MapSketchTool(bool closed, bool snapping) {
    sk_.closed = closed;
    sk_.snapping = snapping;
  }
  void deactivate(App&) override { sk_.clear(); }

  bool pointerDown(App& a, const PointerEvent& e) override {
    if (a.readOnly()) return true;
    Result r = sk_.down(a, e);
    if (r == Result::Finish) finish(a);
    return r != Result::Ignored;
  }
  bool pointerMove(App& a, const PointerEvent& e) override { return sk_.move(a, e, ui::mouse().down[0]) != Result::Ignored; }
  bool pointerUp(App& a, const PointerEvent& e) override {
    Result r = sk_.up(a, e);
    if (r == Result::Finish) finish(a);
    return r != Result::Ignored;
  }
  bool key(App& a, const platform::Event& e) override {
    Result r = sk_.key(a, e);
    if (r == Result::Finish) finish(a);
    return r != Result::Ignored;
  }
  platform::Cursor cursor(App&) override { return platform::Cursor::Crosshair; }
  const char* hint(App&) override {
    if (sk_.drawingFreehand()) return "Отпустите кнопку — готово";
    if (!sk_.active()) return idleHint();
    return sk_.closed ? "Щелчок — вершина · Enter — готово · Esc — отмена" : "Щелчок — точка линии · Enter — готово · Esc — отмена";
  }
  void drawOverlay(App& a, gfx::Canvas& c, const map::View& v) override {
    const Color line = lineColor();
    sk_.draw(a, c, v, line, sk_.closed ? line.alpha(0.17f) : Color(0, 0, 0, 0));
    if (a.ui.cursorMap) tools::cursorBadge(c, v.toScreen(*a.ui.cursorMap), badge(), line);
    options(a);
  }

 protected:
  tools::Sketch sk_;
  virtual Color lineColor() const = 0;
  virtual const char* badge() const = 0;
  virtual const char* icon() const = 0;
  virtual const char* title() const = 0;
  virtual const char* idleHint() const = 0;
  virtual bool commit(App& a, const std::vector<Vec2>& pts) = 0;
  virtual float extraW(App&) { return 0; }
  virtual void extra(App&) {}

  void finish(App& a) {
    if (!sk_.ready()) return;
    if (commit(a, sk_.result(a.map().view()))) sk_.clear();
  }

  void options(App& a) {
    const std::string count = sk_.active() ? std::to_string(sk_.count()) + " " + plural(sk_.count(), "точка", "точки", "точек") : std::string();
    const float width = extraW(a) + (count.empty() ? 0 : tools::textW(count, ui::Font::Small) + 6) + tools::sketchButtonsW();
    tools::OptionsBar bar(a, icon(), title(), width);
    if (!bar) return;
    extra(a);
    if (!count.empty()) ui::label(count, {.font = ui::Font::Small, .ink = ui::Ink::Dim});
    ui::flex();
    const int r = tools::sketchButtons(a, sk_, a.readOnly());
    if (r == 1) finish(a);
    else if (r == 2) sk_.pop();
    else if (r == -1) sk_.clear();
  }
};

// Озеро: контур воды.
class LakeTool final : public MapSketchTool {
 public:
  LakeTool() : MapSketchTool(true, false) {}

 protected:
  Color lineColor() const override { return tools::palette().info; }
  const char* badge() const override { return "plus"; }
  const char* icon() const override { return "tool-lake"; }
  const char* title() const override { return "Озеро"; }
  const char* idleHint() const override { return "Щелчки — вершины озера · протяжка — от руки"; }
  bool commit(App& a, const std::vector<Vec2>& pts) override {
    Id id = 0;
    MapShape s;
    s.kind = ShapeKind::Water;
    s.pts = pts;
    const bool ok = act(a, "Новое озеро", [&](Tx& tx) { id = rules::addShape(tx, s); });
    if (ok) {
      a.ui.symbolGroup.clear();
      a.select(SelType::Shape, id);
    }
    return ok;
  }
};

// Река и стена: линия заданной ширины. Ширина — на всю сессию.
float gRiverWidth = schema::kRiverWidth, gWallWidth = schema::kWallWidth;

class LineTool final : public MapSketchTool {
 public:
  explicit LineTool(ShapeKind kind) : MapSketchTool(false, false), kind_(kind) {}

 protected:
  ShapeKind kind_;
  bool river() const { return kind_ == ShapeKind::River; }
  float& width() const { return river() ? gRiverWidth : gWallWidth; }
  Color lineColor() const override { return river() ? tools::palette().info : tools::palette().accent; }
  const char* badge() const override { return "plus"; }
  const char* icon() const override { return river() ? "tool-river" : "tool-wall"; }
  const char* title() const override { return river() ? "Река" : "Стена"; }
  const char* idleHint() const override { return river() ? "Щелчки — точки реки от истока к устью · протяжка — от руки" : "Щелчки — точки стены · протяжка — от руки"; }
  float extraW(App&) override { return 118 + 6; }
  void extra(App& a) override {
    ui::numberField("width", width(),
                    {.min = schema::kMinShapeWidth, .max = schema::kMaxShapeWidth, .step = 0.5, .digits = 1, .label = "Ширина", .steppers = true,
                     .tooltip = "Ширина линии в единицах карты"});
    a.markUi("tool.options.width");
  }
  bool commit(App& a, const std::vector<Vec2>& pts) override {
    Id id = 0;
    MapShape s;
    s.kind = kind_;
    s.pts = pts;
    s.w = width();
    const bool ok = act(a, river() ? "Новая река" : "Новая стена", [&](Tx& tx) { id = rules::addShape(tx, s); });
    if (ok) {
      a.ui.symbolGroup.clear();
      a.select(SelType::Shape, id);
    }
    return ok;
  }
};

// Суша и море контуром (берег — граф провинций).
class LandTool final : public MapSketchTool {
 public:
  explicit LandTool(bool add) : MapSketchTool(true, true), add_(add) {}

 protected:
  bool add_;
  Color lineColor() const override { return add_ ? tools::palette().success : tools::palette().danger; }
  const char* badge() const override { return add_ ? "plus" : "minus"; }
  const char* icon() const override { return add_ ? "tool-land-plus" : "tool-land-minus"; }
  const char* title() const override { return add_ ? "Добавить сушу" : "Убрать сушу"; }
  const char* idleHint() const override {
    return add_ ? "Обведите море — оно станет сушей (новый остров или мыс)" : "Обведите сушу — она станет морем (залив, пролив)";
  }
  bool commit(App& a, const std::vector<Vec2>& pts) override {
    const double snap = a.map().view().toMapLen(tools::kSnapPx);
    std::vector<std::string> removed;
    const bool ok = a.act(add_ ? "Добавить сушу" : "Убрать сушу", [&](Tx& tx) {
      removed = rules::paintTerrain(tx, pts, add_ ? Terrain::Land : Terrain::Sea, snap).removed;
    });
    if (ok && !removed.empty())
      a.toast(removed.size() == 1 ? "Удалена провинция без области: «" + removed[0] + "»" : "Удалены провинции без области: «" + join(removed, "», «") + "»",
              ToastKind::Warning, "trash");
    return ok;
  }
};

ToolDef mapDef(ToolId id, const char* icon, const char* title, const char* key, int order, std::function<std::unique_ptr<MapTool>()> make) {
  ToolDef d{id, icon, title, key, false, std::move(make), order};
  d.mapMode = true;
  return d;
}
ToolDef symbolDef() {
  return mapDef(ToolId::MapSymbol, "tool-symbol", "Знак: гора, замок, башня", "S", 210, [] { return std::make_unique<SymbolTool>(); });
}
ToolDef lakeDef() { return mapDef(ToolId::MapLake, "tool-lake", "Озеро", "W", 220, [] { return std::make_unique<LakeTool>(); }); }
ToolDef riverDef() {
  return mapDef(ToolId::MapRiver, "tool-river", "Река", "Shift+W", 230, [] { return std::make_unique<LineTool>(ShapeKind::River); });
}
ToolDef wallDef() { return mapDef(ToolId::MapWall, "tool-wall", "Стена", "Q", 240, [] { return std::make_unique<LineTool>(ShapeKind::Wall); }); }
ToolDef landAddDef() {
  return mapDef(ToolId::LandAdd, "tool-land-plus", "Добавить сушу", "N", 250, [] { return std::make_unique<LandTool>(true); });
}
ToolDef landRemoveDef() {
  return mapDef(ToolId::LandRemove, "tool-land-minus", "Убрать сушу (море)", "Shift+N", 260, [] { return std::make_unique<LandTool>(false); });
}

ToolReg regSymbol(symbolDef());
ToolReg regLake(lakeDef());
ToolReg regRiver(riverDef());
ToolReg regWall(wallDef());
ToolReg regLandAdd(landAddDef());
ToolReg regLandRemove(landRemoveDef());

}  // namespace

void registerDrawTools() {
  ToolReg a(symbolDef());
  ToolReg b(lakeDef());
  ToolReg c(riverDef());
  ToolReg d(wallDef());
  ToolReg e(landAddDef());
  ToolReg f(landRemoveDef());
}

}  // namespace rg::app::mapedit
