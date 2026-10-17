// Regnum — ядро приложения: экземпляр, создание и настройки приложения (app.json), действия с миром, отмена,
// показ области карты, цветокор и масштаб. Реестры — registry.cpp, выделение и навигация — navigation.cpp, окна и
// уведомления — app_dialogs.cpp, события и кадр — frame.cpp.
#include "app/app_internal.h"
#include "app/shell/player.h"
#include "app/shell/role.h"
#include "base/fs.h"
#include "base/json.h"
#include "base/jobs.h"

namespace rg::app {

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
  std::optional<json::Value> v;
  if (d.cfg.savePrefs)
    if (auto text = fs::readFile(prefsPath(d.dataDir))) v = json::tryParse(*text);
  if (v && !v->isObj()) v.reset();
  // Плеер саундтреков: его настройки (без файла — по умолчанию) и разбор папки саундтреков в фоне.
  detail::shell::playerAttach(a, v ? v->get("player") : json::Value());
  if (!v) return;
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
  if (json::Value player = detail::shell::playerPrefs(); !player.isNull()) v.set("player", std::move(player));
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

  std::string dir = cfg.basemapDir.empty() ? map::findBasemapDir() : cfg.basemapDir;
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

bool App::play(std::string_view label, const std::function<void(Tx&)>& fn, const TxOptions& opt) {
  const bool was = d_->playing;
  d_->playing = true;
  const bool ok = act(label, fn, opt);
  d_->playing = was;
  return ok;
}

bool App::act(std::string_view label, const std::function<void(Tx&)>& fn, const TxOptions& opt) {
  if (readOnly()) {
    toast("Открыт прошлый ход — изменения недоступны", ToastKind::Warning, "lock", "К текущему ходу", [](App& a) { a.backToCurrent(); });
    return false;
  }
  if (player() && !d_->playing) {
    toast("Доступно только администратору редактора", ToastKind::Warning, "lock");
    return false;
  }
  try {
    // ТЗ «Фиксы», п.10: если после действия в провинции построек больше слотов — сначала предупреждение со списком
    // построек, которые будут снесены; отказ ничего не меняет.
    const World before = store.world();
    Tx tx(before);
    fn(tx);
    if (d_->playing) detail::role::decorate(*this, tx);   // игрок: очко действий объектов встречи
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

}  // namespace rg::app

namespace rg::app::detail {
// Отложить действие до конца кадра (после сведения интерфейса).
void later(App& a, std::function<void(App&)> fn) {
  a.impl().later.push_back(std::move(fn));
  platform::invalidate();
}
}  // namespace rg::app::detail
