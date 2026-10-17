// Regnum — помощник сценарных тестов приложения: настоящий app::App на headless-платформе.
//
//   apptest::Harness h("my_test");              // временная папка данных пользователя, окно 1440×900
//   h.demo();                                   // демонстрационный мир (map::makeDemoWorld)
//   h.clickUi("topbar.endturn");                // щелчок по элементу оболочки (App::uiRect)
//   h.key(Key::K, apptest::ctrl());             // сочетание
//   h.type("Арден");                            // ввод текста
//   h.shot("editor");                           // снимок .wmma/regnum-tests/app_editor.png
//   shotClean(h, "editor");                     // то же после дорисовки карты, без уведомлений, в покое
//   hasToast(h, "Сохранено");                   // есть уведомление с этим текстом
//
// Кадры идут с виртуальным временем (1/60 с на кадр). Щелчок разносится по кадрам интерфейса (step).
// Общие помощники по темам: test_app_editors_util.h (редакторы на месте карты), test_app_faction_util.h (страницы
// государств и гильдий), test_app_economy_util.h (провинции и экономика), test_app_trees_util.h (деревья технологий и
// построек), test_app_military_util.h и test_app_war_util.h (войска, война), test_app_tools_util.h (инструменты карты);
// поиск по имени и выборки мира — test_world_util.h, запись PNG — test_png.h.
#pragma once
#include "app/app_internal.h"
#include "base/fs.h"
#include "geo/topo.h"
#include "map/demo_world.h"
#include "tests/test.h"
#include "tests/test_png.h"
#include "tests/test_world_util.h"

namespace rg::apptest {

using platform::Key;
namespace hl = platform::headless;

inline u32 ctrl() { return platform::primaryMod(); }

constexpr int kAltScheme = 2;   // другой цветокор для снимков (багрянец)

// Чистая временная папка теста (внутри каталога артефактов).
inline std::string tempDir(const std::string& name) {
  std::string d = fs::join(test::outDir(), "app-tmp/" + name);
  fs::removeAll(d);
  fs::makeDirs(d);
  return fs::absolute(d);
}

struct Harness {
  std::string root, dataDir;
  std::unique_ptr<app::App> app;

  explicit Harness(const std::string& name, int w = 1440, int h = 900, float scale = 1, int scheme = 0) {
    root = tempDir(name);
    dataDir = tempDir(name + ".data");   // рядом, а не внутри: в папке теста — только миры
    hl::reset();
    hl::configure(w, h, scale);
    ui::shutdown();
    ui::init();
    app::AppConfig cfg;
    cfg.dataDir = dataDir;
    app = std::make_unique<app::App>(cfg);
    app->ui.scheme = scheme;
    app->impl().browserDir = root;
    hl::attach(app.get());
    frames(2);
  }
  ~Harness() {
    hl::attach(nullptr);
    app.reset();
    ui::shutdown();
    hl::reset();
  }
  Harness(const Harness&) = delete;
  Harness& operator=(const Harness&) = delete;

  app::App& a() { return *app; }
  app::App* operator->() { return app.get(); }

  // ---- кадры
  void frame() {
    hl::renderFrame();
    hl::advance(1.0 / 60);
  }
  void frames(int n) {
    for (int i = 0; i < n; i++) frame();
  }
  // Доставить события и отложенные действия: кадры, пока интерфейс разбирает очередь, и ещё три.
  void step(int max = 200) {
    frame();
    for (int i = 0; i < max && ui::pendingEvents() > 0; i++) frame();
    frames(3);
  }
  // Кадры, пока приложению нужна перерисовка (анимации камеры, появление окон), не больше max.
  void settle(int max = 240) {
    step();
    for (int i = 0; i < max && (ui::needsRedraw() || app->map().animating()); i++) frame();
  }
  // Дождаться фоновой отрисовки тайлов карты и показать их.
  void waitMap() {
    for (int i = 0; i < 6; i++) {
      app->map().waitIdle(30);
      frames(2);
      if (!app->map().loading()) break;
    }
  }
  // Уведомления оболочки закрыть (снимки без случайных карточек).
  void dropToasts() {
    app->toasts().clear();
    for (int i = 0; i < 40 && ui::toastCount() > 0; i++) {
      hl::advance(1);
      frame();
    }
  }

  // ---- ввод (логические пиксели окна)
  void move(float x, float y) {
    hl::mouseMove(x, y);
    step();
  }
  void click(float x, float y, int button = platform::MouseLeft, u32 mods = 0) {
    hl::click(x, y, button, mods);
    step();
    hl::advance(0.6);   // следующий щелчок не станет двойным
  }
  void doubleClick(float x, float y) {
    hl::doubleClick(x, y);
    step();
    hl::advance(0.6);
  }
  bool clickUi(std::string_view name, int button = platform::MouseLeft) {
    const RectF* r = app->uiRect(name);
    if (!r) return false;
    RectF c = *r;
    click(c.cx(), c.cy(), button);
    return true;
  }
  void drag(float x0, float y0, float x1, float y1, int button = platform::MouseLeft, u32 mods = 0) {
    hl::mouseMove(x0, y0, mods);
    step();
    hl::mouseDown(x0, y0, button, mods);
    step();
    for (int i = 1; i <= 8; i++) {
      hl::mouseMove(x0 + (x1 - x0) * float(i) / 8, y0 + (y1 - y0) * float(i) / 8, mods);
      step();
    }
    hl::mouseUp(x1, y1, button, mods);
    step();
    hl::advance(0.6);
  }
  void wheel(float x, float y, float notches) {
    hl::mouseMove(x, y);
    step();
    hl::wheel(x, y, notches);
    step();
  }
  void key(Key k, u32 mods = 0) {
    hl::press(k, mods);
    step();
  }
  void type(const std::string& s) {
    hl::type(s);
    step();
  }
  // Выделить всё в поле с фокусом и заменить текстом.
  void retype(const std::string& s) {
    key(Key::A, ctrl());
    type(s);
  }

  // ---- мир
  void demo() {
    const map::Basemap* bm = app->basemap();
    if (!bm) test::fail(__FILE__, __LINE__, "базовая карта не найдена");
    app->loadWorld(map::makeDemoWorld(*bm), "Демонстрационный мир");
    settle();
  }
  // Сухопутная провинция с владельцем, точка подписи которой видна в свободной части карты (не под панелями).
  std::optional<std::pair<Id, gfx::Pt>> visibleProvince(Id skip = 0) {
    const World& w = app->world();
    auto fs = geo::faces(w);
    // Точка должна быть свободна и в текущей раскладке (сюда придётся щелчок), и в раскладке с открытым
    // инспектором (мини-карта, масштаб и легенда сдвигаются).
    std::vector<RectF> covers;
    auto collect = [&] {
      for (const char* n : {"legend", "status", "zoom", "minimap", "toolbar", "inspector", "banner", "rail", "drawer", "topbar"})
        if (const RectF* r = app->uiRect(n)) covers.push_back(r->expand(6));
    };
    collect();
    app::Selection keep = app->ui.sel;
    if (!keep) {
      Id any = 0;
      w.provinces.each([&](const Province& p) {
        if (!any && !p.sea) any = p.id;
      });
      app->select(app::SelType::Province, any);
      frames(2);
    }
    struct Restore {
      Harness* h;
      app::Selection s;
      ~Restore() {
        if (!s) {
          h->app->clearSelection();
          h->frames(2);
        }
      }
    } restore{this, keep};
    RectF area = app->mapArea();
    collect();
    std::optional<std::pair<Id, gfx::Pt>> best;
    double bestArea = 0;
    w.provinces.each([&](const Province& p) {
      if (p.sea || !p.owner || p.id == skip) return;
      const geo::ProvinceShape* sh = fs->shape(p.id);
      if (!sh) return;
      gfx::Pt s = app->map().view().toScreen(sh->label);
      if (!area.inset(20).contains(s.x, s.y)) return;
      for (auto& c : covers)
        if (c.contains(s.x, s.y)) return;
      if (app->map().provinceAt(s.x, s.y) != p.id || app->map().armyAt(s.x, s.y) != 0) return;
      if (sh->area > bestArea) {
        bestArea = sh->area;
        best = std::make_pair(p.id, s);
      }
    });
    return best;
  }

  // ---- снимки (.wmma/regnum-tests/app_<name>.png)
  bool shot(const std::string& name) const {
    const platform::Frame& f = hl::lastFrame();
    return test::savePng("app_" + name + ".png", test::argbToRgba(f, 0, 0, f.w, f.h));
  }
  u32 pixel(float x, float y) const {
    const platform::Frame& f = hl::lastFrame();
    int ix = int(x * f.scale), iy = int(y * f.scale);
    if (ix < 0 || iy < 0 || ix >= f.w || iy >= f.h) return 0;
    return f.row(iy)[ix];
  }
};

// ---- карта на экране
// Точка карты на экране (логические пиксели окна).
inline gfx::Pt screenOf(Harness& h, Vec2 p) { return h->map().view().toScreen(p); }

// Показать точку карты в центре видимой части с масштабом zoom и дождаться камеры и тайлов.
inline void showAt(Harness& h, Vec2 p, double zoom) {
  h->focusMap(p, zoom);
  h.settle();
  h.waitMap();
}

// Точка экрана в свободной части карты: не ближе margin к краю области карты и не под панелями оболочки и панелью
// инструмента.
inline bool freeOnScreen(Harness& h, float x, float y, float margin = 24) {
  RectF area = h->mapArea().inset(margin);
  if (!area.contains(x, y)) return false;
  for (const char* n : {"legend", "status", "zoom", "minimap", "toolbar", "inspector", "banner", "rail", "drawer", "topbar", "tool.options"})
    if (const RectF* r = h->uiRect(n))
      if (r->expand(8).contains(x, y)) return false;
  return true;
}

// ---- снимки (.wmma/regnum-tests/app_<name>.png)
// Снимок без случайных карточек (с проверкой записи): карта дорисована, уведомления закрыты, интерфейс в покое — не
// дольше settleMax кадров (мигание каретки после ввода держит перерисовку ещё 10 с).
inline void shotClean(Harness& h, const std::string& name, int settleMax = 240) {
  h.waitMap();
  h.dropToasts();
  h.settle(settleMax);
  CHECK(h.shot(name));
}

// Снимок части окна (логические пиксели), увеличенный в k раз — для проверки мелких деталей.
inline bool cropShot(const std::string& name, RectF r, int k = 2) {
  const platform::Frame& f = hl::lastFrame();
  int x0 = std::max(0, int(r.x * f.scale)), y0 = std::max(0, int(r.y * f.scale));
  int x1 = std::min(f.w, int(r.right() * f.scale)), y1 = std::min(f.h, int(r.bottom() * f.scale));
  if (x1 <= x0 || y1 <= y0) return false;
  return test::savePng("app_" + name + ".png", test::argbToRgba(f, x0, y0, x1, y1, k));
}

// ---- мир
inline const Province* prov(Harness& h, Id pid) { return h->world().province(pid); }

// Фракция с точно таким названием (последняя по ID) и первая (по ID), в названии которой есть part.
inline Id findFaction(const World& w, std::string_view name) {
  Id r = 0;
  w.factions.each([&](const Faction& f) {
    if (f.name == name) r = f.id;
  });
  return r;
}
inline Id factionByName(const World& w, std::string_view part) {
  return test::idWhere(w.factions, [&](const Faction& f) { return f.name.find(part) != std::string::npos; });
}

// Свободный для назначений герой государства (не правитель): skip — сколько таких пропустить.
inline Id availableHero(const World& w, Id state, int skip = 0) {
  Id r = 0;
  w.characters.each([&](const Character& c) {
    if (r || c.faction != state || w.faction(state)->ruler == c.id || !rules::heroAvailable(w, c.id)) return;
    if (skip-- > 0) return;
    r = c.id;
  });
  return r;
}

// ---- выделение и ввод
// Выделить объект (focus — с показом на карте), открыть вкладку инспектора и вернуть прокрутку инспектора к началу (она
// общая для вкладок).
inline void openTab(Harness& h, app::SelType type, Id id, const char* tab, bool focus = false) {
  h->ui.tabOf[type] = tab;
  h->select(type, id, focus);
  h.settle();
  h.dropToasts();
  if (const RectF* r = h->uiRect("inspector")) {
    h.wheel(r->cx(), r->bottom() - 120, 40);
    h.frames(24);   // плавная прокрутка — 120–180 мс
  }
}

// Выделить фракцию (провинцию с показом на карте) и открыть вкладку инспектора; карта дорисована, уведомления закрыты.
inline void factionTab(Harness& h, Id faction, const char* tab) {
  h->ui.tabOf[app::SelType::Faction] = tab;
  h->select(app::SelType::Faction, faction);
  h.waitMap();
  h.settle(60);
  h.dropToasts();
}
inline void provinceTab(Harness& h, Id province, const char* tab) {
  h->ui.tabOf[app::SelType::Province] = tab;
  h->select(app::SelType::Province, province, true);
  h.waitMap();
  h.settle(60);
  h.dropToasts();
}

// Выбрать видимую на карте провинцию щелчком и открыть вкладку инспектора (skip — кроме этой провинции).
inline Id clickProvince(Harness& h, const char* tab, Id skip = 0) {
  auto vp = h.visibleProvince(skip);
  CHECK(vp.has_value());
  h->ui.tabOf[app::SelType::Province] = tab;
  h.click(vp->second.x, vp->second.y);
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Province, vp->first}));
  return vp->first;
}

// Щелчок по элементу оболочки в доле fx его ширины (посередине по высоте).
inline bool clickAt(Harness& h, const std::string& name, float fx) {
  const RectF* r = h->uiRect(name);
  if (!r) return false;
  RectF c = *r;
  h.click(c.x + c.w * fx, c.cy());
  return true;
}

// Щёлкнуть по i-й из n равных частей элемента (сегменты, вкладки с fill).
inline bool clickPart(Harness& h, std::string_view name, int i, int n) {
  const RectF* r = h->uiRect(name);
  if (!r) return false;
  RectF c = *r;
  h.click(c.x + c.w * (float(i) + 0.5f) / float(n), c.cy());
  return true;
}

// Ввод в поле модального окна: щелчок, выделить всё, текст, Tab — фиксация уходом фокуса (Enter в модальном окне
// нажал бы основную кнопку).
inline void enterInDialog(Harness& h, const std::string& name, const std::string& value) {
  CHECK_MSG(h.clickUi(name), name);
  h.key(Key::A, ctrl());
  h.type(value);
  h.key(Key::Tab);
  h.settle();
}

// ---- уведомления, журнал, время кадра
// Есть уведомление с этим текстом (и этого вида).
inline bool hasToast(Harness& h, std::string_view part) {
  for (const app::Toast& t : h->toasts())
    if (t.text.find(part) != std::string::npos) return true;
  return false;
}
inline bool hasToast(Harness& h, std::string_view part, app::ToastKind kind) {
  for (const app::Toast& t : h->toasts())
    if (t.kind == kind && t.text.find(part) != std::string::npos) return true;
  return false;
}

// Журнал программы на время жизни объекта дублируется в файл app-tmp/<name>.log каталога артефактов. lines() — новые
// полные строки с прошлого вызова; warnings(filter) — из них сообщения WARN и ERROR (текст после «] »), содержащие
// filter (повторяющиеся ID виджетов и т. п.).
struct LogTap {
  std::string path;
  size_t off = 0;
  explicit LogTap(const std::string& name) : path(fs::join(test::outDir(), "app-tmp/" + name + ".log")) {
    fs::remove(path);
    setLogFile(path);
  }
  ~LogTap() { setLogFile(""); }
  LogTap(const LogTap&) = delete;
  LogTap& operator=(const LogTap&) = delete;
  std::vector<std::string> lines() {
    std::vector<std::string> out;
    auto t = fs::readFile(path);
    if (!t || t->size() <= off) return out;
    const size_t last = t->rfind('\n');
    if (last == std::string::npos || last < off) return out;
    for (size_t p = off; p <= last;) {
      const size_t q = t->find('\n', p);
      out.push_back(t->substr(p, q - p));
      p = q + 1;
    }
    off = last + 1;
    return out;
  }
  std::vector<std::string> warnings(std::string_view filter = {}) {
    std::vector<std::string> out;
    for (const std::string& line : lines()) {
      if (line.find("[WARN]") == std::string::npos && line.find("[ERROR]") == std::string::npos) continue;
      if (!filter.empty() && line.find(filter) == std::string::npos) continue;
      const size_t m = line.find("] ");
      out.push_back(m == std::string::npos ? line : line.substr(m + 2));
    }
    return out;
  }
};

// Среднее время отрисовки кадра (мс) за n кадров; before(i) — действие перед кадром i.
inline double frameMs(int n, const std::function<void(int)>& before = {}) {
  double total = 0;
  for (int i = 0; i < n; i++) {
    if (before) before(i);
    hl::pump();
    double t0 = nowSeconds();
    hl::renderFrame();
    total += nowSeconds() - t0;
    hl::advance(1.0 / 60);
  }
  return total * 1000 / n;
}

}  // namespace rg::apptest
