// Regnum — ядро приложения: реестры, действия с миром, выделение, инструменты, диалоги, уведомления,
// цикл кадра (карта с кешем + интерфейс), настройки приложения (app.json).
#include "app/app_internal.h"
#include "base/fs.h"
#include "base/json.h"
#include "base/jobs.h"

namespace rg::app {

// ================================================================ реестры
namespace {
template <class T>
std::vector<T>& reg() {
  static std::vector<T> v;
  return v;
}
template <class T>
void insertSorted(std::vector<T>& v, const T& d) {
  auto it = std::upper_bound(v.begin(), v.end(), d, [](const T& a, const T& b) { return a.order < b.order; });
  v.insert(it, d);
}
}  // namespace

TabReg::TabReg(const TabDef& d) { insertSorted(reg<TabDef>(), d); }
const std::vector<TabDef>& tabs() { return reg<TabDef>(); }

HeaderReg::HeaderReg(const HeaderDef& d) { insertSorted(reg<HeaderDef>(), d); }
const std::vector<HeaderDef>& headers() { return reg<HeaderDef>(); }

QuickReg::QuickReg(const QuickDef& d) { insertSorted(reg<QuickDef>(), d); }
const std::vector<QuickDef>& quicks() { return reg<QuickDef>(); }

DrawerReg::DrawerReg(const DrawerDef& d) { insertSorted(reg<DrawerDef>(), d); }
const std::vector<DrawerDef>& drawers() { return reg<DrawerDef>(); }

SectionReg::SectionReg(const SectionDef& d) {
  auto& v = reg<SectionDef>();
  for (auto it = v.begin(); it != v.end(); ++it)
    if (std::string_view(it->id) == d.id) {
      v.erase(it);
      break;
    }
  insertSorted(v, d);
}
const std::vector<SectionDef>& sections() { return reg<SectionDef>(); }
const SectionDef* findSection(std::string_view id) {
  for (auto& s : reg<SectionDef>())
    if (id == s.id) return &s;
  return nullptr;
}

EditorReg::EditorReg(const EditorDef& d) {
  auto& v = reg<EditorDef>();
  for (auto& e : v)
    if (std::string_view(e.id) == d.id) {
      e = d;
      return;
    }
  v.push_back(d);
}
const EditorDef* findEditor(std::string_view id) {
  for (auto& e : reg<EditorDef>())
    if (id == e.id) return &e;
  return nullptr;
}
const std::vector<EditorDef>& editors() { return reg<EditorDef>(); }

ToolReg::ToolReg(const ToolDef& d) {
  auto& v = reg<ToolDef>();
  // Повторная регистрация того же инструмента заменяет прежнюю (встроенный выбор можно переопределить).
  for (auto it = v.begin(); it != v.end(); ++it)
    if (it->id == d.id) {
      v.erase(it);
      break;
    }
  insertSorted(v, d);
}
const ToolDef* findTool(ToolId id) {
  for (auto& t : reg<ToolDef>())
    if (t.id == id) return &t;
  return nullptr;
}
const std::vector<ToolDef>& toolDefs() { return reg<ToolDef>(); }

CommandReg::CommandReg(const CommandDef& d) {
  auto& v = reg<CommandDef>();
  for (auto& c : v)
    if (std::string_view(c.id) == d.id) {
      c = d;
      return;
    }
  v.push_back(d);
}
const std::vector<CommandDef>& commands() { return reg<CommandDef>(); }
const CommandDef* findCommand(std::string_view id) {
  for (auto& c : reg<CommandDef>())
    if (id == c.id) return &c;
  return nullptr;
}
bool runCommand(App& a, std::string_view id) {
  const CommandDef* c = findCommand(id);
  if (!c || !c->run) return false;
  if (c->enabled && !c->enabled(a)) return false;
  try {
    c->run(a);
  } catch (const std::exception& e) {
    a.error(e);
  }
  return true;
}

DialogReg::DialogReg(const DialogDef& d) {
  auto& v = reg<DialogDef>();
  for (auto& x : v)
    if (std::string_view(x.id) == d.id) {
      x = d;
      return;
    }
  v.push_back(d);
}
const DialogDef* findDialog(std::string_view id) {
  for (auto& d : reg<DialogDef>())
    if (id == d.id) return &d;
  return nullptr;
}

// ================================================================ экземпляр
static App* gApp = nullptr;
App& app() {
  if (!gApp) throw std::logic_error("Regnum: приложение не создано");
  return *gApp;
}
bool hasApp() { return gApp != nullptr; }

// ---------------------------------------------------------------- цветокор
// Палитра карты: исходные цвета или палитра цветокора (тот же id, что у схемы интерфейса).
map::Palette detail::schemePalette(int scheme) {
  const std::string_view id = ui::schemeInfo(scheme).id;
  for (int p = 1; p < int(map::Palette::Count); p++)
    if (id == map::paletteId(map::Palette(p))) return map::Palette(p);
  return map::Palette::Sapphire;
}

map::Palette detail::mapPalette(const App& a) { return a.ui.schemeMap ? schemePalette(a.ui.scheme) : map::Palette::Source; }

// ---------------------------------------------------------------- настройки приложения
static std::string prefsPath(const std::string& dataDir) { return fs::join(dataDir, "app.json"); }

static void loadPrefs(App& a) {
  App::Impl& d = a.impl();
  if (!d.cfg.savePrefs) return;
  auto text = fs::readFile(prefsPath(d.dataDir));
  if (!text) return;
  auto v = json::tryParse(*text);
  if (!v || !v->isObj()) return;
  const std::string scheme = v->str("scheme", "");
  for (int i = 0; i < ui::schemeCount(); i++)
    if (scheme == ui::schemeInfo(i).id) a.ui.scheme = i;
  a.ui.uiScale = float(clamp(v->num("uiScale", 1.0), 0.9, 1.5));
  a.ui.schemeMap = v->boolean("schemeMap", false);
  a.ui.drawerWidth = float(clamp(v->num("panelWidth", 300), 240.0, 560.0));
  a.ui.inspectorWidth = float(clamp(v->num("inspectorWidth", 380), 320.0, 640.0));
  d.browserDir = v->str("browserDir");
  d.builtinBrowser = v->boolean("builtinBrowser", false);
}

void detail::savePrefs(App& a) {
  App::Impl& d = a.impl();
  d.prefsDirty = false;
  if (!d.cfg.savePrefs) return;
  json::Value v = json::Value::object();
  v.set("scheme", ui::schemeInfo(a.ui.scheme).id);
  v.set("uiScale", double(a.ui.uiScale));
  v.set("schemeMap", a.ui.schemeMap);
  v.set("panelWidth", double(std::round(a.ui.drawerWidth)));
  v.set("inspectorWidth", double(std::round(a.ui.inspectorWidth)));
  v.set("browserDir", d.browserDir);
  v.set("builtinBrowser", d.builtinBrowser);
  std::string text = json::write(v, json::WriteOptions{2, true, false});
  text.push_back('\n');
  std::string err;
  if (!fs::writeFileAtomic(prefsPath(d.dataDir), text, &err)) logWarn("app.json: %s", err.c_str());
}

// ---------------------------------------------------------------- создание
App::App(const AppConfig& cfg) : d_(std::make_unique<Impl>()) {
  gApp = this;
  Impl& d = *d_;
  d.cfg = cfg;
  d.dataDir = cfg.dataDir.empty() ? fs::userDataDir() : fs::absolute(cfg.dataDir);
  fs::makeDirs(d.dataDir);
  ui::init();
  ui::setClipboard([] { return platform::clipboardText(); }, [](const std::string& s) { platform::setClipboardText(s); });
  loadPrefs(*this);
  ui::setScheme(ui.scheme);
  ui::setUiScale(ui.uiScale);

  std::string dir = cfg.basemapDir.empty() ? detail::findBasemapDir() : cfg.basemapDir;
  if (!dir.empty()) {
    auto bm = std::make_unique<map::Basemap>();
    std::string err;
    if (bm->load(dir, &err)) d.basemap = std::move(bm);
    else logWarn("Базовая карта не загружена (%s): %s", dir.c_str(), err.c_str());
  } else {
    logWarn("Базовая карта не найдена: assets/basemap");
  }
  d.map = std::make_unique<map::MapView>(d.basemap.get());
  d.map->setPalette(detail::mapPalette(*this));
  d.map->setWakeCallback([] { platform::wake(); });   // тайлы готовы в фоне — разбудить цикл событий
  d.map->setWorld(store.world());
  storeSub_ = store.subscribe([this](const Change& c) { onStoreChange(c); });
  setTool(ToolId::Select);
}

App::~App() {
  waitBackground();
  store.unsubscribe(storeSub_);
  if (tool_) {
    auto t = std::move(tool_);
    try {
      t->deactivate(*this);
    } catch (...) {
    }
  }
  if (d_->prefsDirty) detail::savePrefs(*this);
  dialogs_.clear();
  if (gApp == this) gApp = nullptr;
}

void App::onStoreChange(const Change& c) {
  Impl& d = *d_;
  if (!readOnly()) {
    if (c.kind == Change::Load) d.map->setWorld(*c.after);
    else d.map->worldChanged(*c.before, *c.after, c.tables);
    d.mapGen++;
  }
  platform::invalidate();
}

// ================================================================ мир
const World& App::world() const { return ui.viewTurn ? viewWorld_ : store.world(); }

bool App::act(std::string_view label, const std::function<void(Tx&)>& fn, const TxOptions& opt) {
  if (readOnly()) {
    toast("Открыт прошлый ход — изменения недоступны", ToastKind::Warning, "lock", "К текущему ходу", [](App& a) { a.backToCurrent(); });
    return false;
  }
  try {
    // ТЗ «Фиксы», п.10: если после действия в провинции построек больше слотов — сначала предупреждение со списком
    // построек, которые будут снесены; отказ ничего не меняет.
    const World before = store.world();
    Tx tx(before);
    fn(tx);
    const u32 touched = tx.touched();
    if (!touched) return true;
    World after = std::move(tx).finish();
    constexpr u32 kSlotTables = TB_PROVINCES | TB_FACTIONS | TB_MODIFIERS | TB_TECHS | TB_BUILDINGS | TB_CATALOGS | TB_CHARACTERS;
    std::vector<rules::SlotLoss> loss;
    if (touched & kSlotTables) loss = rules::slotLosses(before, after);
    if (loss.empty()) {
      store.transact(label, [&](Tx& t) { t.replaceWorld(after); }, opt);
      return true;
    }
    std::vector<std::string> lines;
    for (const rules::SlotLoss& l : loss) {
      std::vector<std::string> names;
      for (Id b : l.buildings) {
        const Building* bd = after.building(b);
        names.push_back("«" + (bd && !bd->name.empty() ? bd->name : std::string("Без названия")) + "»");
      }
      lines.push_back(after.provinceName(l.province) + ": " + join(names, ", "));
    }
    std::string text = "Данное действие уберёт слоты провинции и лишит её следующих построек: " + join(lines, "; ") + ".";
    std::string lbl(label);
    TxOptions o = opt;
    confirm("Провинция лишится слотов", text, "Продолжить", true, [before, after, loss, lbl, o](App& a) {
      if (World::diff(a.store.world(), before) != 0) {
        a.toast("Мир изменился — повторите действие", ToastKind::Warning, "warning");
        return;
      }
      try {
        a.store.transact(lbl, [&](Tx& t) {
          t.replaceWorld(after);
          rules::trimExcessBuildings(t, &loss);
        }, o);
      } catch (const std::exception& e) {
        a.error(e);
      }
    });
    return false;
  } catch (const std::exception& e) {
    error(e);
    return false;
  }
}

void App::undo() {
  if (readOnly()) return;
  if (!store.canUndo()) return;
  std::string l = store.undoLabel();
  store.undo();
  if (!l.empty()) toast("Отменено: " + l, ToastKind::Info, "undo");
}

void App::redo() {
  if (readOnly()) return;
  if (!store.canRedo()) return;
  std::string l = store.redoLabel();
  store.redo();
  if (!l.empty()) toast("Повторено: " + l, ToastKind::Info, "redo");
}

bool App::dirty() const { return ui.screen == Screen::Editor && (store.dirty() || d_->forceTables != 0); }

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

void App::openDialog(std::unique_ptr<Dialog> dlg) {
  if (!dlg) return;
  Impl& f = *d_;
  if (f.drawingDialogs) {
    f.pendingDialogs.push_back(std::move(dlg));
  } else {
    std::string_view id = dlg->id();
    dialogs_.erase(std::remove_if(dialogs_.begin(), dialogs_.end(), [&](auto& x) { return id == x->id(); }), dialogs_.end());
    dialogs_.push_back(std::move(dlg));
  }
  requestRedraw();
}

bool App::openDialog(std::string_view id, Id arg) {
  if (const DialogDef* def = findDialog(id)) {
    try {
      openDialog(def->make(*this, arg));
    } catch (const std::exception& e) {
      error(e);
    }
    return true;
  }
  if (id == "turn.report") {
    if (!lastReport_) return false;
    openDialog(detail::turnReportDialog(*this, *lastReport_));
    return true;
  }
  if (id == "turn.history") {
    openDialog(detail::historyDialog(*this));
    return true;
  }
  if (id == "settings") {
    openDialog(detail::settingsDialog());
    return true;
  }
  if (id == "palette") {
    openDialog(detail::paletteDialog());
    return true;
  }
  if (id == "help") {
    openDialog(detail::helpDialog());
    return true;
  }
  if (id == "world.new") {
    openDialog(detail::newWorldDialog(*this));
    return true;
  }
  return false;
}

void App::closeDialogs() {
  Impl& f = *d_;
  if (f.drawingDialogs) f.closeAllDialogs = true;
  else dialogs_.clear();
  f.pendingDialogs.clear();
  requestRedraw();
}

bool App::hasDialog(std::string_view id) const {
  for (auto& x : dialogs_)
    if (id == x->id()) return true;
  for (auto& x : d_->pendingDialogs)
    if (id == x->id()) return true;
  return false;
}

void App::toast(std::string text, ToastKind kind, std::string icon, std::string actionLabel, std::function<void(App&)> action) {
  // Над модальным окном интерфейсные панели не видны: уведомление без действия — в слой ui.
  if (ui::anyModalOpen() || hasDialog()) {
    if (actionLabel.empty()) {
      static const ui::Tone tones[] = {ui::Tone::Info, ui::Tone::Success, ui::Tone::Warning, ui::Tone::Danger};
      ui::toast(text, tones[int(kind)], icon.empty() ? nullptr : icon.c_str(), kind == ToastKind::Danger ? 8 : 4);
      requestRedraw();
      return;
    }
  }
  Toast t;
  t.id = ++toastSeq_;
  t.kind = kind;
  t.text = std::move(text);
  t.icon = std::move(icon);
  t.start = time_;
  t.duration = kind == ToastKind::Danger ? 8 : !actionLabel.empty() ? 7 : kind == ToastKind::Warning ? 5 : 3.5;
  t.actionLabel = std::move(actionLabel);
  t.action = std::move(action);
  // Не больше пяти; одинаковые подряд не повторяются.
  if (!toasts_.empty() && toasts_.back().text == t.text && toasts_.back().closeAt < 0) {
    toasts_.back().start = time_;
    toasts_.back().paused = 0;
    return;
  }
  toasts_.push_back(std::move(t));
  int live = 0;
  for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it)
    if (it->closeAt < 0 && ++live > 5) it->closeAt = time_;
  requestRedraw();
}

void App::error(const std::exception& e) {
  bool user = dynamic_cast<const UserError*>(&e) != nullptr;
  std::string msg = e.what();
  Impl& f = *d_;
  if (msg == f.lastError && time_ - f.lastErrorAt < 2) return;   // повторяющаяся ошибка каждого кадра
  f.lastError = msg;
  f.lastErrorAt = time_;
  if (user) {
    toast(msg, ToastKind::Warning, "warning");
  } else {
    logError("Ошибка: %s", msg.c_str());
    toast("Что-то пошло не так: " + msg + ". Мир не изменён.", ToastKind::Danger, "error");
  }
}

void App::confirm(std::string title, std::string text, std::string okLabel, bool danger, std::function<void(App&)> onYes) {
  openDialog(detail::confirmDialog(std::move(title), std::move(text), std::move(okLabel), danger, std::move(onYes)));
}

void App::choose(std::string title, std::string text, std::vector<std::string> buttons, std::function<void(App&, int)> onPick,
                 const char* icon, bool firstSubtle) {
  openDialog(detail::choiceDialog(std::move(title), std::move(text), std::move(buttons), std::move(onPick), icon, firstSubtle));
}

void App::prompt(std::string title, std::string label, std::string initial, std::function<void(App&, const std::string&)> onOk) {
  openDialog(detail::promptDialog(std::move(title), std::move(label), std::move(initial), std::move(onOk)));
}

void App::message(std::string title, std::string text, std::vector<std::string> lines, ToastKind kind) {
  openDialog(detail::messageDialog(std::move(title), std::move(text), std::move(lines), kind));
}

void App::focusMap(Box2 box) {
  if (box.empty()) return;
  map::MapView& m = *d_->map;
  RectF area = mapArea();
  if (area.w < 40 || area.h < 40) area = m.view().viewport;
  double pad = 0.12;
  double zw = area.w / std::max(1.0, box.w() * (1 + 2 * pad));
  double zh = area.h / std::max(1.0, box.h() * (1 + 2 * pad));
  double z = clamp(std::min(zw, zh), m.minZoom(), std::min(m.maxZoom(), 1.6));
  focusMap(box.center(), z);
}

void App::focusMap(Vec2 p, double zoom) {
  map::MapView& m = *d_->map;
  const map::View& v = m.view();
  double z = zoom > 0 ? clamp(zoom, m.minZoom(), m.maxZoom()) : v.zoom;
  // Точка должна оказаться в центре видимой части карты, а не всего окна.
  RectF area = mapArea();
  double ox = (double(area.cx()) - double(v.viewport.cx())) / z;
  double oy = (double(area.cy()) - double(v.viewport.cy())) / z;
  m.centerOn(Vec2{p.x - ox, p.y - oy}, z, true);
  requestRedraw();
}

void App::focusSelection() {
  if (!ui.sel) return;
  const World& w = world();
  if (ui.sel.type == SelType::Army) {
    if (const Army* a = w.army(ui.sel.id)) focusMap(a->pos, std::max(d_->map->view().zoom, 0.45));
    return;
  }
  Box2 b = detail::selectionBox(w, ui.sel);
  if (!b.empty()) focusMap(b);
}

void App::requestRedraw() { platform::invalidate(); }

void App::setScheme(int i) {
  i = clamp(i, 0, ui::schemeCount() - 1);
  if (ui.scheme == i) return;
  ui.scheme = i;
  ui::setScheme(i);
  d_->map->setPalette(detail::mapPalette(*this));
  d_->prefsDirty = true;
  d_->backdropW = 0;
  d_->mapKey = 0;
  d_->mapGen++;
  requestRedraw();
}

void App::setSchemeMap(bool on) {
  if (ui.schemeMap == on) return;
  ui.schemeMap = on;
  d_->map->setPalette(detail::mapPalette(*this));
  d_->prefsDirty = true;
  d_->mapGen++;
  requestRedraw();
}

void App::setUiScale(float s) {
  s = clamp(s, 0.9f, 1.5f);
  if (std::fabs(ui.uiScale - s) < 1e-3f) return;
  ui.uiScale = s;
  d_->prefsDirty = true;
  requestRedraw();
}

void App::toggleFullscreen() { platform::setFullscreen(!platform::fullscreen()); }

void App::showPalette() {
  if (hasDialog("palette")) closeDialogs();
  else openDialog("palette");
}
void App::showHelp() {
  if (hasDialog("help")) closeDialogs();
  else openDialog("help");
}
void App::showSettings() { openDialog("settings"); }

RectF App::mapArea() const {
  auto it = d_->rects.find("map.area");
  if (it != d_->rects.end()) return it->second;
  return RectF{0, 0, d_->lw, d_->lh};
}

const RectF* App::uiRect(std::string_view name) const {
  auto it = d_->rects.find(std::string(name));
  return it == d_->rects.end() ? nullptr : &it->second;
}

void App::markUi(std::string_view name) { markUi(name, ui::lastItem().rect); }

void App::markUi(std::string_view name, RectF r) {
  float s = ui::uiScale();
  d_->rectsNext[std::string(name)] = RectF{r.x * s, r.y * s, r.w * s, r.h * s};
}

map::MapView& App::map() { return *d_->map; }
const map::Basemap* App::basemap() const { return d_->basemap.get(); }
std::string App::dataDir() const { return d_->dataDir; }
const std::string& App::projectPath() const { return d_->path; }
bool App::projectIsBundle() const { return d_->bundle; }
std::string App::worldTitle() const {
  const std::string& n = store.world().meta->name;
  return n.empty() ? std::string("Без названия") : n;
}

void App::waitBackground() {
  Impl& d = *d_;
  if (d.autosaveJob.valid()) {
    try {
      std::string err = d.autosaveJob.get();
      if (!err.empty()) logWarn("Автосохранение: %s", err.c_str());
    } catch (const std::exception& e) {
      logWarn("Автосохранение: %s", e.what());
    }
  }
}

// ================================================================ события
void App::onEvent(const platform::Event& e) {
  using T = platform::EventType;
  Impl& d = *d_;
  switch (e.type) {
    case T::KeyDown:
      if (e.key == platform::Key::Space && !ui::wantsKeyboard()) d.spaceDown = true;
      break;
    case T::KeyUp:
      if (e.key == platform::Key::Space) d.spaceDown = false;
      break;
    case T::FocusOut:
      d.spaceDown = false;
      break;
    case T::FocusIn:
      d_->later.push_back([](App& a) { a.checkExternalChanges(); });
      break;
    case T::FilesDropped:
      if (!e.files.empty()) {
        std::string p = e.files.front();
        d_->later.push_back([p](App& a) { a.openPath(p); });
      }
      break;
    default:
      break;
  }
  ui::onEvent(e);
  platform::invalidate();
}

bool App::animating() {
  Impl& d = *d_;
  if (ui::needsRedraw()) return true;
  if (!toasts_.empty()) return true;
  if (!d_->later.empty()) return true;
  if (mapShown()) {
    if (d.map->animating() || d.map->needsRedraw()) return true;
    if (tool_ && tool_->animating(*this)) return true;
  }
  if (d.autosaveJob.valid()) return true;
  return false;
}

bool App::onCloseRequest() {
  if (!dirty()) {
    waitBackground();
    if (ui.screen == Screen::Editor) io::clearAutosave(d_->path, d_->dataDir);
    if (d_->prefsDirty) detail::savePrefs(*this);
    return true;
  }
  requestQuit();
  return false;
}

// ================================================================ кадр
void App::onFrame(platform::Frame& f) {
  Impl& d = *d_;
  const double p0 = nowSeconds();
  frames_++;
  time_ = platform::time();
  d.lw = f.logicalW();
  d.lh = f.logicalH();
  d.dpi = f.scale;
  if (ui::scheme() != ui.scheme) {   // цветокор задан полем (настройки, тесты)
    ui::setScheme(ui.scheme);
    d.backdropW = 0;
    d.mapKey = 0;
  }
  if (const map::Palette pal = detail::mapPalette(*this); d.map->palette() != pal) {
    d.map->setPalette(pal);
    d.mapGen++;
  }
  if (std::fabs(ui::uiScale() - ui.uiScale) > 1e-3f) ui::setUiScale(ui.uiScale);
  if (d.frameImg.w != f.w || d.frameImg.h != f.h) d.frameImg.resize(f.w, f.h);
  d.map->setViewport(RectF{0, 0, d.lw, d.lh}, d.dpi);
  d.map->update(time_);
  d.rectsNext.clear();

  ui::beginFrame(d.lw, d.lh, d.dpi, time_);
  try {
    buildFrame();
  } catch (const std::exception& e) {
    error(e);
  }
  // Свободная часть карты между панелями (раскладка этого кадра): в неё «Показать всю карту» вписывает мир,
  // по ней же минимальный масштаб. Вся карта при открытии мира — когда эта часть уже известна.
  {
    auto it = d.rectsNext.find("map.area");
    d.map->setSafeArea(it != d.rectsNext.end() ? it->second : RectF{});
  }
  if (d.fitPending && d.lw > 1 && d.lh > 1) {
    d.map->fitAll(false);
    d.fitPending = false;
  }
  const double p1 = nowSeconds();
  gfx::Canvas c(d.frameImg);
  try {
    if (mapShown()) detail::renderMap(*this, c);
    else if (ui.screen == Screen::Start) detail::renderStartBackdrop(*this, c);
    else c.clear(ui::theme().bg);
  } catch (const std::exception& e) {
    c.clear(ui::theme().bg);
    error(e);
  }
  const double p2 = nowSeconds();
  ui::endFrame(c);
  const double p3 = nowSeconds();
  d.rects.swap(d.rectsNext);

  // Курсор: интерфейс, иначе инструмент карты.
  platform::Cursor cur = ui::cursor();
  if (cur == platform::Cursor::Arrow && d.mapHovered && mapShown() && !hasDialog()) {
    if (d.panning) cur = platform::Cursor::Grabbing;
    else if (d.spaceDown || ui.tool == ToolId::Pan) cur = platform::Cursor::Grab;
    else if (tool_) cur = tool_->cursor(*this);
  }
  platform::setCursor(cur);
  if (auto r = ui::textInputRect()) platform::setTextInputRect(*r);

  // Заголовок окна — просто «Regnum» (название мира и состояние сохранения — в верхней панели).
  if (d.title.empty()) {
    d.title = "Regnum";
    platform::setTitle(d.title);
  }

  // Кадр для окна — прямо из буфера кадра (Windows выводит его без копии, прочие платформы копируют сами). Если ждут
  // отложенные действия, кадр копируется в буфер окна: системный диалог крутит свой цикл сообщений, и окно под ним
  // перерисовывается из этого буфера.
  const double p4 = nowSeconds();
  if (d.later.empty())
    f.source = d.frameImg.px.data();
  else
    for (int y = 0; y < f.h; y++) std::memcpy(f.row(y), d.frameImg.row(y), size_t(f.w) * sizeof(u32));
  const double p5 = nowSeconds();
  profile_ = FrameProfile{(p1 - p0) * 1000, (p2 - p1) * 1000, (p3 - p2) * 1000, (p5 - p4) * 1000, (p5 - p0) * 1000};

  // Отложенные действия (после сведения кадра: системные диалоги, открытие файлов).
  Impl& fs = *d_;
  if (!fs.later.empty()) {
    auto later = std::move(fs.later);
    fs.later.clear();
    for (auto& fn : later) {
      try {
        fn(*this);
      } catch (const std::exception& e) {
        error(e);
      }
    }
    platform::invalidate();
  }
  if (d.prefsDirty && frames_ % 120 == 0) detail::savePrefs(*this);
}

void App::buildFrame() {
  Impl& d = *d_;
  if (ui.sel && !detail::selectionExists(world(), ui.sel)) ui.sel = Selection{};
  if (!ui.sel) ui.page = false;
  if (ui.hover && !detail::selectionExists(world(), ui.hover)) ui.hover = Selection{};
  if (!ui.editor.empty() && !findEditor(ui.editor)) ui.editor.clear();
  if (!ui.directory.empty() && !findSection(ui.directory)) ui.directory.clear();
  d.mapHovered = false;

  if (ui.screen == Screen::Start) {
    detail::drawStartScreen(*this);
  } else {
    if (mapShown()) detail::mapInput(*this);
    detail::drawEditorScreen(*this);
    // Со страницы на карту: показать выделение, когда раскладка карты (панели справа) уже известна.
    if (d.focusAfter > 0 && --d.focusAfter == 0 && mapShown()) focusSelection();
  }
  // Диалоги
  Impl& fs = *d_;
  fs.drawingDialogs = true;
  try {
    detail::drawDialogs(*this);
  } catch (...) {
    fs.drawingDialogs = false;
    throw;
  }
  fs.drawingDialogs = false;
  if (fs.closeAllDialogs) {
    dialogs_.clear();
    fs.closeAllDialogs = false;
  }
  auto pending = std::move(fs.pendingDialogs);
  fs.pendingDialogs.clear();
  for (auto& p : pending) openDialog(std::move(p));

  detail::globalKeys(*this);
  if (mapShown()) detail::mapWheel(*this);
  if (ui.screen == Screen::Editor) detail::autosaveTick(*this);
}

}  // namespace rg::app

namespace rg::app::detail {
// Отложить действие до конца кадра (после сведения интерфейса).
void later(App& a, std::function<void(App&)> fn) {
  a.impl().later.push_back(std::move(fn));
  platform::invalidate();
}
}  // namespace rg::app::detail
