// Regnum — выделение и навигация: что на месте карты (карта, редактор, страница, каталог), разделы и панели правой
// ленты, назад и к карте, инструменты и режимы правки.
#include "app/app_internal.h"
#include "app/shell/role.h"

namespace rg::app {

// ================================================================ интерфейс
// Государство, гильдия и персонаж открываются страницей на месте карты; провинция, войско, маршрут и объекты
// карты — панелью справа от карты (страницей — по кнопке «Развернуть»).
static bool pageType(SelType t) { return t == SelType::Faction || t == SelType::Character; }

void App::select(Selection s, bool focus) {
  if (s && !detail::selectionExists(world(), s)) s = Selection{};
  const bool wasMap = view() == View::Map;
  if (s && pageType(s.type) && !focus) {
    // Страница сущности: выделение на карте запоминается — «К карте» вернёт его.
    if (ui.sel && !pageType(ui.sel.type)) ui.backSel = ui.sel;
    ui.page = true;
    ui.directory.clear();
  } else {
    ui.page = false;
    if (s) {
      ui.directory.clear();
      ui.backSel = {};
    }
  }
  ui.sel = s;
  if (const SectionDef* sec = sectionOf(s)) ui.lastOf[sec->id] = s;
  if (s && focus) {
    if (wasMap || view() != View::Map) focusSelection();
    else d_->focusAfter = 2;   // со страницы на карту — когда известна её раскладка
  }
  requestRedraw();
}

View App::view() const {
  if (!ui.editor.empty()) return View::Editor;
  if (!ui.directory.empty()) return View::Directory;
  if (ui.page && ui.sel) return View::Entity;
  return View::Map;
}

bool App::mapShown() const { return ui.screen == Screen::Editor && view() == View::Map; }

const SectionDef* App::sectionOf(Selection s) {
  if (!s) return nullptr;
  for (auto& sec : sections()) {
    if (sec.type != s.type || !sec.directory) continue;
    if (sec.owns && !sec.owns(*this, s.id)) continue;
    return &sec;
  }
  return nullptr;
}

void App::setPage(bool on) {
  if (!ui.sel) on = false;
  if (ui.page == on && ui.directory.empty()) return;
  if (!on) ui.backSel = {};
  ui.page = on;
  ui.directory.clear();
  d_->mapKey = 0;
  requestRedraw();
}

void App::openDirectory(std::string_view id) {
  const SectionDef* sec = findSection(id);
  if (!sec || !sec->directory) return;
  ui.editor.clear();
  ui.editorArg = 0;
  ui.directory = std::string(id);
  requestRedraw();
}

void App::toMap() {
  ui.editor.clear();
  ui.editorArg = 0;
  ui.directory.clear();
  if (ui.page) {
    ui.page = false;
    // Со страницы государства, гильдии или персонажа — к выделению на карте, с которого на неё перешли.
    if (pageType(ui.sel.type)) ui.sel = ui.backSel && detail::selectionExists(world(), ui.backSel) ? ui.backSel : Selection{};
  }
  ui.backSel = {};
  d_->mapKey = 0;
  requestRedraw();
}

void App::back() {
  if (!ui.editor.empty()) closeEditor();
  else if (!ui.directory.empty()) {
    ui.directory.clear();
    requestRedraw();
  } else if (ui.page) {
    toMap();
  }
}

void App::openSection(std::string_view id) {
  const SectionDef* sec = findSection(id);
  if (!sec) return;
  if (!detail::role::sectionAllowed(*this, id)) {
    toast(detail::role::kAdminOnly, ToastKind::Info, "lock");
    return;
  }
  // Раздел уже на экране (у редактора без раздела — страница под ним) — к карте.
  View v = view();
  bool active = false;
  if (v == View::Editor) {
    const EditorDef* e = findEditor(ui.editor);
    if (e && e->group) active = std::string_view(e->group) == sec->id;
    else v = !ui.directory.empty() ? View::Directory : ui.page && ui.sel ? View::Entity : View::Map;
  }
  if (v == View::Directory) active = ui.directory == sec->id;
  else if (v == View::Entity) active = sectionOf(ui.sel) == sec;
  // Разделы ленты — верхний уровень: другой раздел открывается вместо текущей страницы, а не поверх неё.
  toMap();
  if (active) return;
  if (sec->directory) {
    if (ui.sel && sectionOf(ui.sel) == sec) {   // сущность раздела выделена на карте — её страница
      ui.editor.clear();
      setPage(true);
      return;
    }
    auto it = ui.lastOf.find(sec->id);
    if (it != ui.lastOf.end() && detail::selectionExists(world(), it->second) && sectionOf(it->second) == sec) {
      ui.editor.clear();
      select(it->second);
      return;
    }
    openDirectory(sec->id);
    return;
  }
  // Раздел редакторов: последний открытый или первый по порядку.
  const EditorDef* pick = nullptr;
  auto le = ui.lastEditor.find(sec->id);
  if (le != ui.lastEditor.end()) pick = findEditor(le->second);
  if (!pick || !pick->group || std::string_view(pick->group) != sec->id) {
    pick = nullptr;
    for (auto& e : editors())
      if (e.group && std::string_view(e.group) == sec->id && (!pick || e.order < pick->order)) pick = &e;
  }
  if (pick) openEditor(pick->id, 0);
}

void App::setTool(ToolId t) {
  const ToolDef* def = findTool(t);
  if (!def) {
    if (t != ToolId::Select) toast("Этот инструмент пока недоступен", ToastKind::Info, "info");
    return;
  }
  // Игрок: выбор, панорама, высадка и постановка своих объектов из панели государства.
  if (std::string why = detail::role::toolBlocked(*this, t); !why.empty()) {
    toast(why, ToastKind::Info, "lock");
    return;
  }
  if ((def->editMode || def->mapMode) && readOnly()) {
    toast("Прошлый ход: только просмотр — инструменты правки недоступны", ToastKind::Info, "lock");
    return;
  }
  if (def->editMode && !ui.editBorders) {
    toast("Включите правку границ (E), чтобы менять провинции", ToastKind::Info, "lock");
    return;
  }
  if (def->mapMode && !ui.editMap) {
    toast("Включите правку карты (T), чтобы менять сушу, воды, горы, замки и башни", ToastKind::Info, "lock");
    return;
  }
  if (def->blocked)
    if (std::string why = def->blocked(*this); !why.empty()) {
      toast(why, ToastKind::Info, def->icon);
      return;
    }
  // Инструмент карты со страницы (например, «Поставить войско» на странице государства) — на карту.
  if (t != ToolId::Select && t != ToolId::Pan && ui.screen == Screen::Editor && view() != View::Map) {
    if (view() == View::Entity) setPage(false);
    else toMap();
  }
  if (tool_ && ui.tool == t) return;
  if (tool_) {
    auto old = std::move(tool_);
    try {
      old->deactivate(*this);
    } catch (const std::exception& e) {
      error(e);
    }
  }
  d_->toolDown = false;
  ui.tool = t;
  try {
    tool_ = def->make ? def->make() : nullptr;
    if (tool_) tool_->activate(*this);
  } catch (const std::exception& e) {
    tool_.reset();
    error(e);
    if (t != ToolId::Select) setTool(ToolId::Select);
  }
  requestRedraw();
}

void App::setEditBorders(bool on) {
  if (ui.editBorders == on) return;
  // Прошлый ход — только просмотр: режим правки границ не включается (ТЗ 1.a.ii, 1.f).
  if (on && readOnly()) return;
  if (on && !admin()) {
    toast(detail::role::kAdminOnly, ToastKind::Info, "lock");
    return;
  }
  if (on && ui.editMap) setEditMap(false);
  ui.editBorders = on;
  if (!on) {
    const ToolDef* cur = findTool(ui.tool);
    if (cur && cur->editMode) setTool(ToolId::Select);
  } else if (ui.tool == ToolId::Select && findTool(ToolId::EditBorders)) {
    // ТЗ 1.a.iv: в режиме правки щелчок по провинции открывает её границы — сразу инструмент границ.
    setTool(ToolId::EditBorders);
  }
  d_->mapGen++;
  requestRedraw();
}

void App::setEditMap(bool on) {
  if (ui.editMap == on) return;
  if (on && readOnly()) return;
  if (on && !admin()) {
    toast(detail::role::kAdminOnly, ToastKind::Info, "lock");
    return;
  }
  if (on && ui.editBorders) setEditBorders(false);
  ui.editMap = on;
  if (!on) {
    const ToolDef* cur = findTool(ui.tool);
    if (cur && cur->mapMode) setTool(ToolId::Select);
    ui.symbolGroup.clear();
    if (ui.sel.type == SelType::Symbol || ui.sel.type == SelType::Shape) ui.sel = {};
    if (ui.hover.type == SelType::Symbol || ui.hover.type == SelType::Shape) ui.hover = {};
  } else if (findTool(ToolId::MapObjects)) {
    if (ui.sel.type != SelType::Symbol && ui.sel.type != SelType::Shape) ui.sel = {};
    setTool(ToolId::MapObjects);
  }
  d_->mapGen++;
  requestRedraw();
}

void App::setMapMode(schema::MapMode m) {
  if (int(m) < 0 || m >= schema::MapMode::Count) return;
  ui.mapMode = m;
  requestRedraw();
}

void App::openDrawer(std::string_view id) {
  if (const SectionDef* sec = findSection(id)) {   // раздел на всё окно — его каталог (или страницы редакторов)
    if (!sec->directory) openSection(id);
    else if (ui.directory == id && ui.editor.empty()) back();
    else openDirectory(id);
    return;
  }
  if (ui.drawer == id) ui.drawer.clear();
  else {
    bool found = false;
    for (auto& dr : drawers())
      if (id == dr.id) found = true;
    ui.drawer = found ? std::string(id) : std::string();
  }
  requestRedraw();
}

void App::openEditor(std::string_view id, Id arg) {
  if (!findEditor(id)) {
    toast("Редактор недоступен", ToastKind::Warning, "warning");
    return;
  }
  if (!detail::role::editorAllowed(*this, id)) {   // справочники, модификаторы, константы — только администратору
    toast(detail::role::kAdminOnly, ToastKind::Info, "lock");
    return;
  }
  ui.editor = std::string(id);
  ui.editorArg = arg;
  if (const EditorDef* e = findEditor(id); e && e->group) ui.lastEditor[e->group] = std::string(id);
  requestRedraw();
}

void App::closeEditor() {
  ui.editor.clear();
  ui.editorArg = 0;
  d_->mapKey = 0;
  requestRedraw();
}

}  // namespace rg::app
