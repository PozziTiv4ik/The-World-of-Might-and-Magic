// Regnum — боковые части основного экрана: лента инструментов карты (слева), лента разделов (справа), закреплённая
// панель выделения и панели ленты справа от карты (провинции, войска, слои, хроника), панель «Слои карты»:
// мини-карта, режимы, легенда и что показывать на карте.
#include "app/shell_internal.h"
#include "map/art_scene.h"

namespace rg::app::detail::shell {

using platform::Key;

namespace {

constexpr float kBtnL = 36;   // кнопка лент (Size::Large)

// Короткая черта между группами ленты (в вертикальном потоке).
void stripSep(float w) {
  RectF r = ui::next(w, 9);
  ui::draw::line(r.x + 7, r.cy(), r.right() - 7, r.cy(), ui::theme().border, 1);
}

// ---------------------------------------------------------------- инструменты карты
struct ToolLayout {
  std::vector<const ToolDef*> base, edit, map, extra;
};
ToolLayout toolLayout(App& a) {
  ToolLayout L;
  for (auto& t : toolDefs()) {
    if (t.id == ToolId::Select || t.id == ToolId::Pan) L.base.push_back(&t);
    else if (t.mapMode) {
      if (a.ui.editMap) L.map.push_back(&t);
    } else if (t.editMode) {
      if (a.ui.editBorders) L.edit.push_back(&t);
    } else {
      L.extra.push_back(&t);
    }
  }
  return L;
}

void toolButton(App& a, const ToolDef& t) {
  ui::IdScope s{int(t.id)};
  std::string tip = t.title;
  if (ui::iconButton(t.icon, tip, {.size = ui::Size::Large, .toggled = a.ui.tool == t.id})) a.setTool(t.id);
  if (t.shortcut && *t.shortcut) ui::tooltip(tip, parseShortcut(t.shortcut));
  static const char* names[] = {"select", "pan",        "borders-tool", "new-province", "add-area", "remove-area", "fill",
                                "knife",  "merge",      "delete",       "army",         "fleet",    "route",       "map-objects",
                                "symbol", "lake",       "river",        "wall",         "land-add", "land-remove", "coast",
                                "new-sea-province"};
  a.markUi(std::string("tool.") + (int(t.id) < int(std::size(names)) ? names[int(t.id)] : "other"));
}

// Флажок в углу кнопки режима правки.
void modeCheck(bool on, bool ro) {
  const ui::Theme& th = ui::theme();
  RectF br = ui::lastItem().rect;
  RectF cb{br.right() - 12, br.bottom() - 12, 10, 10};
  ui::draw::rect(cb.expand(1.5f), th.surface1, 4);
  if (on) {
    ui::draw::rect(cb, th.accent, 3);
    ui::draw::icon("check", cb.inset(0.5f), th.onAccent);
  } else {
    ui::draw::rectStroke(cb, ro ? th.border : th.borderStrong, 3, 1.2f);
  }
}

// Высота столбца кнопок: n кнопок, s черт, промежуток 4.
float columnH(int n, int s) { return n * kBtnL + s * 9 + std::max(0, n + s - 1) * 4; }

}  // namespace

void toolStrip(App& a, RectF r) {
  ui::Panel p("toolbar", r, docked());
  a.markUi("toolbar", r);
  edgeLine(r, false, false, true, false);
  ToolLayout L = toolLayout(a);
  const bool ro = a.readOnly();
  const int n = int(L.base.size()) + 2 + int(L.edit.size()) + int(L.map.size()) + int(L.extra.size()) + 1;
  const int seps = 2 + (L.extra.empty() ? 0 : 1);
  const float pad = 6, top = 8;
  const float used = columnH(n, seps);
  const float zoomH = columnH(3, 0);
  {
    ui::Area ar(RectF{r.x + pad, r.y + top, r.w - 2 * pad, r.h - top}, 0);
    std::optional<ui::Scroll> sc;   // низкое окно и много инструментов правки — столбец прокручивается
    if (used + 2 * top > r.h) sc.emplace("tools");
    ui::gap(4);
    for (auto* t : L.base) toolButton(a, *t);
    stripSep(kBtnL);
    // Правка границ (закрытый замок — границы закреплены) и правка карты; на прошлом ходу недоступны.
    {
      bool on = a.ui.editBorders;
      std::string tip = ro ? "Прошлый ход: только просмотр" : "Правка границ";
      if (ui::iconButton(bordersToggleIcon(on), tip, {.size = ui::Size::Large, .toggled = on, .disabled = ro})) a.setEditBorders(!on);
      ui::tooltip(tip, ro ? ui::Shortcut{} : parseShortcut("E"));
      a.markUi("tool.borders");
      modeCheck(on, ro);
    }
    for (auto* t : L.edit) toolButton(a, *t);
    {
      bool on = a.ui.editMap;
      std::string tip = ro ? "Прошлый ход: только просмотр" : "Правка карты";
      if (ui::iconButton("map-edit", tip, {.size = ui::Size::Large, .toggled = on, .disabled = ro})) a.setEditMap(!on);
      ui::tooltip(tip, ro ? ui::Shortcut{} : parseShortcut("T"));
      a.markUi("tool.mapedit");
      modeCheck(on, ro);
    }
    for (auto* t : L.map) toolButton(a, *t);
    if (!L.extra.empty()) {
      stripSep(kBtnL);
      for (auto* t : L.extra) toolButton(a, *t);
    }
    stripSep(kBtnL);
    {
      bool hidden = !a.world().settings->showArmies;
      if (ui::iconButton(hidden ? "eye-off" : "eye", hidden ? "Показать войска" : "Скрыть войска", {.size = ui::Size::Large, .toggled = hidden, .disabled = ro}))
        a.act(hidden ? "Показать войска" : "Скрыть войска", [hidden](Tx& tx) { tx.settings().showArmies = hidden; });
      a.markUi("tool.hideArmies");
    }
  }
  // Внизу — масштаб (если хватает высоты).
  if (used + zoomH + 2 * top + 16 <= r.h) {
    RectF zr{r.x + pad, r.bottom() - top - zoomH, r.w - 2 * pad, zoomH};
    ui::Area ar(zr, 0);
    ui::gap(4);
    commandButton(a, "map.zoomIn", nullptr, false, {}, ui::Size::Large);
    a.markUi("zoom.in");
    commandButton(a, "map.zoomOut", nullptr, false, {}, ui::Size::Large);
    a.markUi("zoom.out");
    commandButton(a, "map.fit", nullptr, false, {}, ui::Size::Large);
    a.markUi("zoom.fit");
    a.markUi("zoom", zr);
  }
}

// ================================================================ лента разделов
void sectionStrip(App& a, RectF r) {
  ui::Panel p("rail", r, docked());
  a.markUi("rail", r);
  edgeLine(r, true, false, false, false);
  const float pad = 6, top = 8;
  const SectionDef* act = activeSection(a);
  {
    ui::Area ar(RectF{r.x + pad, r.y + top, r.w - 2 * pad, r.h - top}, 0);
    ui::gap(4);
    for (auto& sec : sections()) {
      ui::IdScope s(sec.id);
      std::string tip = sec.title;
      if (ui::iconButton(sec.icon, tip, {.size = ui::Size::Large, .toggled = act == &sec})) a.openSection(sec.id);
      if (sec.shortcut) ui::tooltip(tip, parseShortcut(sec.shortcut));
      a.markUi(std::string("section.") + sec.id);
      a.markUi(std::string("drawer.") + sec.id);
    }
  }
  // Панели карты — внизу ленты.
  const auto& drs = drawers();
  if (drs.empty()) return;
  const float h = columnH(int(drs.size()), 1);
  RectF dr{r.x + pad, r.bottom() - top - h, r.w - 2 * pad, h};
  ui::Area ar(dr, 0);
  ui::gap(4);
  stripSep(kBtnL);
  const bool onMap = a.view() == View::Map;
  for (auto& d : drs) {
    ui::IdScope s(d.id);
    std::string tip = d.title;
    bool open = onMap && a.ui.drawer == d.id;
    if (ui::iconButton(d.icon, tip, {.size = ui::Size::Large, .toggled = open})) {
      if (!onMap) {
        a.toMap();
        if (a.ui.drawer != d.id) a.openDrawer(d.id);
      } else {
        a.openDrawer(d.id);
      }
    }
    if (d.shortcut) ui::tooltip(tip, parseShortcut(d.shortcut));
    a.markUi(std::string("drawer.") + d.id);
  }
}

// ================================================================ панель выделения
void inspectorPanel(App& a, RectF r, float maxW) {
  const World& w = a.world();
  Selection sel = a.ui.sel;
  ui::Panel p("inspector", r, docked(14));
  a.markUi("inspector", r);
  edgeLine(r, true, false, false, false);
  if (resizeHandle("##insp-resize", RectF{r.x, r.y, 6, r.h}, a.ui.inspectorWidth, 320, maxW, true)) D(a).prefsDirty = true;
  ui::IdScope scope{int(sel.type)};
  // Шапка: зарегистрированные шапки типа (название, владелец…) и кнопки «показать», «развернуть», «закрыть».
  {
    ui::Row head({ui::fr(1), ui::px(30), ui::px(30), ui::px(30)}, ui::kAuto, 2);
    {
      ui::Group g(0, 4);
      bool any = false;
      for (auto& h : headers()) {
        if (h.type != sel.type || !h.draw) continue;
        any = true;
        ui::IdScope hs(h.id);
        try {
          h.draw(a, sel.id);
        } catch (const std::exception& e) {
          a.error(e);
        }
      }
      if (!any) {
        std::string cap = selCaption(sel.type);
        if (sel.type == SelType::Faction)
          if (const Faction* f = w.faction(sel.id)) cap = f->isGuild() ? "Торговая гильдия" : "Государство";
        ui::caption(cap);
        ui::label(entityName(w, sel), {.font = ui::Font::Heading});
      }
    }
    if (ui::iconButton("target", "Показать на карте")) a.focusSelection();
    ui::tooltip("Показать на карте", parseShortcut("F"));
    a.markUi("inspector.focus");
    if (ui::iconButton("maximize", "Открыть страницей")) a.setPage(true);
    a.markUi("inspector.expand");
    if (ui::iconButton("close", "Закрыть")) a.clearSelection();
    ui::tooltip("Закрыть", {Key::Escape, 0});
    a.markUi("inspector.close");
  }
  // Вкладки
  std::vector<const TabDef*> list;
  for (auto& t : tabs()) {
    if (t.type != sel.type || !t.draw) continue;
    bool vis = true;
    if (t.visible) {
      try {
        vis = t.visible(a, sel.id);
      } catch (const std::exception& e) {
        a.error(e);
        vis = false;
      }
    }
    if (vis) list.push_back(&t);
  }
  if (list.empty()) return;
  std::string& cur = a.ui.tabOf[sel.type];
  int idx = 0;
  for (size_t i = 0; i < list.size(); i++)
    if (cur == list[i]->id) idx = int(i);
  if (list.size() > 1) {
    std::vector<ui::Tab> items;
    items.reserve(list.size());
    for (auto* t : list) {
      int badge = 0;
      if (t->badge) {
        try {
          badge = t->badge(a, sel.id);
        } catch (...) {
          badge = 0;
        }
      }
      items.push_back(ui::Tab{t->icon, {}, t->title, badge});
    }
    ui::tabs("tabs", idx, std::span<const ui::Tab>(items), {.fill = true});
    a.markUi("inspector.tabs");
    ui::spacer(2);
  }
  idx = clamp(idx, 0, int(list.size()) - 1);
  cur = list[size_t(idx)]->id;
  // Своя прокрутка у каждой вкладки: при переключении не оказываемся в середине чужой страницы.
  const std::string scrollId = std::string("body.") + list[size_t(idx)]->id;
  ui::Scroll sc(scrollId);
  ui::IdScope ts(list[size_t(idx)]->id);
  try {
    list[size_t(idx)]->draw(a, sel.id);
  } catch (const std::exception& e) {
    a.error(e);
  }
}

// ================================================================ панель ленты
void drawerPanel(App& a, const DrawerDef& dr, RectF r, float maxW) {
  ui::Panel p("drawer", r, docked(14));
  a.markUi("drawer", r);
  edgeLine(r, true, false, false, false);
  if (resizeHandle("##drawer-resize", RectF{r.x, r.y, 6, r.h}, a.ui.drawerWidth, 240, maxW, true)) D(a).prefsDirty = true;
  {
    ui::Row head({ui::px(20), ui::fr(1), ui::px(30)}, 30, 8);
    ui::icon(dr.icon, ui::Ink::Accent, 18);
    ui::label(dr.title, {.font = ui::Font::Subtitle});
    if (ui::iconButton("close", "Закрыть панель")) a.openDrawer(dr.id);
    a.markUi("drawer.close");
  }
  ui::spacer(2);
  ui::Scroll sc("body");
  ui::IdScope s(dr.id);
  try {
    dr.draw(a);
  } catch (const std::exception& e) {
    a.error(e);
  }
}

// ================================================================ мини-карта
void minimapView(App& a, RectF r) {
  App::Impl& d = D(a);
  ui::WidgetId wid = ui::id("##minimap");
  ui::Interaction it = ui::interact(wid, r);
  map::MapView* mv = d.map.get();
  float dpi = d.dpi;
  ui::custom(r, [mv, dpi](gfx::Canvas& c, RectF dev, float) {
    c.save();
    c.clipRoundRect(dev, 6 * dpi);
    mv->renderMinimap(c, RectF{dev.x / dpi, dev.y / dpi, dev.w / dpi, dev.h / dpi}, dpi);
    c.restore();
  });
  ui::draw::rectStroke(r, ui::theme().border, 6, 1);
  if (it.hovered) ui::setCursor(platform::Cursor::Hand);
  if (it.held || it.clicked) {
    float s = ui::uiScale();
    RectF lr{r.x * s, r.y * s, r.w * s, r.h * s};
    Vec2 m = mv->minimapToMap(lr, it.mx * s, it.my * s);
    mv->centerOn(m, 0, !it.dragging);
    a.requestRedraw();
  }
}

// ================================================================ панель «Слои карты»
namespace {

void drawLayers(App& a) {
  App::Impl& d = D(a);
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  // Мини-карта: щелчок или протяжка — перейти.
  {
    float mw = ui::avail().w;
    RectF mr = ui::next(std::round(mw * float(schema::kMapHeight / schema::kMapWidth)));
    minimapView(a, mr);
    a.markUi("minimap", mr);
  }
  ui::spacer(4);
  navCaption("Режим карты");
  ui::gap(2);
  for (int i = 0; i < int(schema::MapMode::Count); i++) {
    const schema::EnumInfo& mi = schema::kMapModes[i];
    ui::IdScope s(i);
    if (ui::listItem(mi.name, {.icon = mi.icon, .hint = std::to_string(i + 1), .selected = int(a.ui.mapMode) == i})) a.setMapMode(schema::MapMode(i));
    a.markUi("mode." + std::to_string(i + 1));
  }
  ui::gap(8);
  // Легенда режима.
  std::vector<map::LegendItem> items = d.map->legend(w, a.ui.mapMode);
  if (!items.empty()) {
    ui::spacer(4);
    navCaption("Легенда");
    RectF top = ui::avail();
    ui::gap(0);
    for (size_t i = 0; i < items.size(); i++) {
      const map::LegendItem& it = items[i];
      ui::IdScope s{int(i)};
      ui::Row row({ui::px(16), ui::fr(1)}, 24, 10);
      RectF sw = ui::next(16, 24);
      if (!it.icon.empty()) ui::draw::icon(it.icon, RectF{sw.x, sw.cy() - 8, 16, 16}, it.color);
      else {
        ui::draw::rect(RectF{sw.x, sw.cy() - 5, 16, 10}, it.color, 3);
        ui::draw::rectStroke(RectF{sw.x, sw.cy() - 5, 16, 10}, Color(0, 0, 0, 40), 3, 1);
      }
      ui::label(it.label, {.font = ui::Font::Small});
    }
    a.markUi("legend", RectF{top.x, top.y, top.w, ui::avail().y - top.y});
    ui::gap(8);
  }
  // Что показывать (настройки мира — отменяются Ctrl+Z).
  ui::spacer(6);
  navCaption("На карте");
  const Settings& st = *a.store.world().settings;
  ui::Disabled dis(a.readOnly());
  bool sl = st.labelStates, pl = st.labelProvinces, al = st.labelArmies, ar = st.showArmies;
  if (ui::toggle("Подписи государств", sl)) a.act("Подписи государств", [sl](Tx& tx) { tx.settings().labelStates = sl; });
  a.markUi("layers.labelStates");
  if (ui::toggle("Подписи провинций", pl)) a.act("Подписи провинций", [pl](Tx& tx) { tx.settings().labelProvinces = pl; });
  if (ui::toggle("Подписи войск", al)) a.act("Подписи войск", [al](Tx& tx) { tx.settings().labelArmies = al; });
  if (ui::toggle("Войска и флот", ar)) a.act(ar ? "Показать войска" : "Скрыть войска", [ar](Tx& tx) { tx.settings().showArmies = ar; });
  a.markUi("layers.armies");
  ui::spacer(2);
  ui::label("Заливка провинций", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  double op = std::round(double(st.fillOpacity) * 100);
  if (ui::slider("fill", op, 0, 100, {.step = 5, .unit = "%"})) {
    float v = float(op / 100.0);
    a.act("Прозрачность заливки", [v](Tx& tx) { tx.settings().fillOpacity = v; }, {.coalesce = "settings.fill"});
  }
  a.markUi("layers.fill");
  (void)th;
}

DrawerReg layersDrawer({"layers", "layers", "Слои карты", 65, drawLayers, "L"});

}  // namespace
}  // namespace rg::app::detail::shell
