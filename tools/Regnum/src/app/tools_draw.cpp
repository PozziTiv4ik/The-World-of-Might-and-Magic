// Regnum — инструменты контура: новая провинция (ТЗ 1.a.i «добавление»), расширение и вырезание выбранной
// провинции, нож. Щелчки — вершины с прилипанием к узлам и границам, протяжка — рисование от руки,
// Enter или двойной щелчок — готово, Backspace — убрать вершину, Esc — отмена.
#include "app/tools_edit.h"

namespace rg::app::tools {

namespace {

using platform::Key;
using Result = Sketch::Result;

std::string pointsText(int n) { return std::to_string(n) + " " + plural(n, "точка", "точки", "точек"); }

// Провинции, удалённые правилом из-за того, что у них не осталось области на карте, — уведомление.
void toastRemoved(App& a, const std::vector<std::string>& names) {
  if (names.empty()) return;
  a.toast(names.size() == 1 ? "Удалена провинция без области: «" + names[0] + "»" : "Удалены провинции без области: «" + join(names, "», «") + "»",
          ToastKind::Warning, "trash");
}

// Общая основа: ввод контура, предпросмотр, панель параметров, завершение.
class SketchTool : public MapTool {
 public:
  void deactivate(App&) override { sk_.clear(); }

  bool pointerDown(App& a, const PointerEvent& e) override {
    if (a.readOnly()) return true;
    // Инструменту нужна выбранная провинция: первый щелчок выбирает её.
    if (needsTarget() && !targetOk(a) && !sk_.active()) {
      if (e.button != 0) return false;
      if (Id p = provinceUnder(a, e)) a.select(SelType::Province, p);
      return true;
    }
    Result r = sk_.down(a, e);
    if (r == Result::Finish) finish(a);
    return r != Result::Ignored;
  }
  bool pointerMove(App& a, const PointerEvent& e) override {
    bool held = ui::mouse().down[0];
    Result r = sk_.move(a, e, held);
    return r != Result::Ignored;
  }
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
  platform::Cursor cursor(App& a) override {
    if (needsTarget() && !targetOk(a) && !sk_.active()) return a.ui.hover.type == SelType::Province ? platform::Cursor::Hand : platform::Cursor::Arrow;
    return platform::Cursor::Crosshair;
  }

  void drawOverlay(App& a, gfx::Canvas& c, const map::View& v) override {
    Color line = lineColor(), fill = line.alpha(0.17f);
    sk_.draw(a, c, v, line, sk_.closed ? fill : Color(0, 0, 0, 0));
    if (a.ui.cursorMap && !(needsTarget() && !targetOk(a) && !sk_.active())) cursorBadge(c, v.toScreen(*a.ui.cursorMap), badge(), line);
    options(a);
  }

 protected:
  Sketch sk_;
  virtual bool needsTarget() const { return false; }
  virtual Color lineColor() const = 0;
  virtual const char* badge() const = 0;
  virtual const char* icon() const = 0;
  virtual const char* title() const = 0;
  virtual bool commit(App& a, const std::vector<Vec2>& pts) = 0;
  // Дополнительные элементы панели (до счётчика и кнопок) и их ширина.
  virtual float extraW(App&) { return 0; }
  virtual void extra(App&) {}

  bool targetOk(App& a) const {
    Id p = selectedProvince(a);
    return p && a.world().province(p);
  }
  double snapTol(App& a) const { return a.map().view().toMapLen(kSnapPx); }

  void finish(App& a) {
    if (!sk_.ready()) return;
    std::vector<Vec2> pts = sk_.result(a.map().view());
    if (commit(a, pts)) sk_.clear();
  }

  void options(App& a) {
    const World& w = a.world();
    std::string count = sk_.active() ? pointsText(sk_.count()) : std::string();
    float width = extraW(a) + (count.empty() ? 0 : textW(count, ui::Font::Small) + 6) + sketchButtonsW();
    if (needsTarget() && targetOk(a)) width += provinceTagW(w, selectedProvince(a)) + 6;
    OptionsBar bar(a, icon(), title(), width);
    if (!bar) return;
    if (needsTarget() && targetOk(a)) provinceTag(w, selectedProvince(a));
    extra(a);
    if (!count.empty()) ui::label(count, {.font = ui::Font::Small, .ink = ui::Ink::Dim});
    ui::flex();
    int r = sketchButtons(a, sk_, a.readOnly());
    if (r == 1) finish(a);
    else if (r == 2) sk_.pop();
    else if (r == -1) sk_.clear();
  }
};

// ---------------------------------------------------------------- новая провинция
// ТЗ «Фиксы», п.17: два инструмента — сухопутная и морская провинция. Контур забирает весь рельеф своего вида
// внутри (сушу — в том числе участки, разделённые морем); берег «прилипает».
class NewProvinceTool final : public SketchTool {
 public:
  explicit NewProvinceTool(Terrain t) : ter_(t) {}

 protected:
  Terrain ter_;
  Color lineColor() const override { return ter_ == Terrain::Sea ? palette().info : palette().accent; }
  const char* badge() const override { return "plus"; }
  const char* icon() const override { return ter_ == Terrain::Sea ? "sea-province" : "tool-polygon"; }
  const char* title() const override { return ter_ == Terrain::Sea ? "Новая провинция (море)" : "Новая провинция (суша)"; }

  static const char* terrainName(Terrain t) { return t == Terrain::Sea ? "Море" : "Суша"; }
  float extraW(App&) override { return textW(terrainName(ter_), ui::Font::Strong) + 34 + 6; }
  void extra(App& a) override {
    ui::tag(terrainName(ter_), ter_ == Terrain::Sea ? ui::Tone::Info : ui::Tone::Success, ter_ == Terrain::Sea ? "sea" : "land");
    ui::tooltip(ter_ == Terrain::Sea ? "Морская провинция: всё море внутри контура" : "Сухопутная провинция: вся суша внутри контура");
    a.markUi("tool.options.terrain");
  }

  bool commit(App& a, const std::vector<Vec2>& pts) override {
    Id nid = 0;
    double snap = snapTol(a);
    std::vector<std::string> removed;
    bool ok = a.act(ter_ == Terrain::Sea ? "Новая морская провинция" : "Новая провинция", [&](Tx& tx) {
      rules::AreaEdit r = rules::createProvince(tx, pts, ter_, snap);  // поглощённые целиком провинции удаляются
      nid = r.province;
      removed = std::move(r.removed);
      std::string name = newProvinceName(tx.w());
      tx.province(nid).name = name;
      rules::addLog(tx, LogKind::Province, "Провинция «" + name + "» добавлена на карту", rules::LogRefs{nid, 0, {}});
    });
    if (ok) a.select(SelType::Province, nid);
    if (ok) toastRemoved(a, removed);
    return ok;
  }
};

// ---------------------------------------------------------------- расширение и вырезание
class AreaTool final : public SketchTool {
 public:
  explicit AreaTool(bool add) : add_(add) {}

 protected:
  bool add_;
  bool needsTarget() const override { return true; }
  Color lineColor() const override { return add_ ? palette().success : palette().danger; }
  const char* badge() const override { return add_ ? "plus" : "minus"; }
  const char* icon() const override { return add_ ? "tool-polygon-plus" : "tool-polygon-minus"; }
  const char* title() const override { return add_ ? "Расширение" : "Вырезание"; }
  bool commit(App& a, const std::vector<Vec2>& pts) override {
    Id pid = selectedProvince(a);
    double snap = snapTol(a);
    // Провинции, оставшиеся без области (поглощённые или вырезанные целиком), удаляются правилом.
    std::vector<std::string> removed;
    bool ok = add_ ? a.act("Расширить провинцию", [&](Tx& tx) { removed = rules::addArea(tx, pid, pts, snap).removed; })
                   : a.act("Вырезать часть провинции", [&](Tx& tx) { removed = rules::removeArea(tx, pid, pts, snap).removed; });
    if (ok) toastRemoved(a, removed);
    return ok;
  }
};

// ---------------------------------------------------------------- нож
class KnifeTool final : public SketchTool {
 public:
  KnifeTool() { sk_.closed = false; }

 protected:
  bool needsTarget() const override { return true; }
  Color lineColor() const override { return palette().danger; }
  const char* badge() const override { return "tool-knife"; }
  const char* icon() const override { return "tool-knife"; }
  const char* title() const override { return "Нож"; }
  bool commit(App& a, const std::vector<Vec2>& pts) override {
    Id pid = selectedProvince(a), nid = 0;
    bool ok = a.act("Разрезать провинцию", [&](Tx& tx) { nid = rules::splitProvince(tx, pid, pts); });
    if (ok) a.select(SelType::Province, nid);
    return ok;
  }
};

ToolDef newDef() {
  return {ToolId::NewProvince, "tool-polygon", "Новая провинция (суша)", "P", true, [] { return std::make_unique<NewProvinceTool>(Terrain::Land); }, 20};
}
ToolDef newSeaDef() {
  return {ToolId::NewSeaProvince, "sea-province", "Новая провинция (море)", "Shift+P", true,
          [] { return std::make_unique<NewProvinceTool>(Terrain::Sea); }, 21};
}
ToolDef addDef() {
  return {ToolId::AddArea, "tool-polygon-plus", "Расширить провинцию", "G", true, [] { return std::make_unique<AreaTool>(true); }, 30};
}
ToolDef removeDef() {
  return {ToolId::RemoveArea, "tool-polygon-minus", "Вырезать часть провинции", "X", true, [] { return std::make_unique<AreaTool>(false); }, 40};
}
ToolDef knifeDef() {
  return {ToolId::Knife, "tool-knife", "Нож: разрезать провинцию", "K", true, [] { return std::make_unique<KnifeTool>(); }, 60};
}

ToolReg regNew(newDef());
ToolReg regNewSea(newSeaDef());
ToolReg regAdd(addDef());
ToolReg regRemove(removeDef());
ToolReg regKnife(knifeDef());

}  // namespace

void registerDrawTools() {
  ToolReg a(newDef());
  ToolReg a2(newSeaDef());
  ToolReg b(addDef());
  ToolReg c(removeDef());
  ToolReg d(knifeDef());
}

}  // namespace rg::app::tools
