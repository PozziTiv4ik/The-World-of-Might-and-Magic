// Regnum — точка входа редактора.
//
//   regnum                      — экран запуска
//   regnum <папка мира|.regnum> — открыть мир
//   regnum --selftest           — настоящее окно, ~20 кадров с демонстрационным миром и действиями оболочки,
//                                 код возврата 0 (проверка сборки). Данные — во временной папке.
//   regnum --bench [мир]        — замер в настоящем окне (по умолчанию мир проекта 12_Карты/Мир_Меча_и_Магии):
//                                 частота кадров и время фаз для наведения, панорамы, масштаба; отчёт в stdout и журнал.
#include <algorithm>
#include <cstdio>
#include <functional>

#include "app/app_internal.h"
#include "base/fs.h"
#include "gfx/text.h"
#include "map/demo_world.h"

using namespace rg;

namespace {

// Самопроверка: сценарий по кадрам поверх настоящего приложения.
struct SelfTest final : platform::App {
  app::App& a;
  int frame = 0;
  int failures = 0;
  std::string log;
  double start = 0;
  explicit SelfTest(app::App& app) : a(app) {}

  void check(bool ok, const char* what) {
    if (!ok) {
      failures++;
      logError("Самопроверка: %s", what);
      std::fprintf(stderr, "selftest: FAIL %s\n", what);
    }
  }

  void step() {
    using app::SelType;
    switch (frame) {
      case 0: {
        start = platform::time();
        const map::Basemap* bm = a.basemap();
        check(bm != nullptr, "базовая карта не найдена");
        if (bm) {
          try {
            a.loadWorld(map::makeDemoWorld(*bm), "Демонстрационный мир");
          } catch (const std::exception& e) {
            check(false, e.what());
            a.newWorld("Самопроверка");
          }
        }
        break;
      }
      case 3: {
        Id pid = 0;
        a.world().provinces.each([&](const Province& p) {
          if (!pid && !p.sea && p.owner) pid = p.id;
        });
        check(pid != 0, "в мире нет провинций");
        if (pid) a.select(SelType::Province, pid, true);
        break;
      }
      case 5: a.showPalette(); break;
      case 7: a.closeDialogs(); break;
      case 8: a.setMapMode(schema::MapMode::Guilds); break;
      case 9: a.setEditBorders(true); break;
      case 10: a.setEditBorders(false); break;
      case 11: a.setMapMode(schema::MapMode::Political); break;
      case 12: {
        int turn = a.store.world().turn();
        check(a.endTurnNow(), "ход не завершён");
        check(a.store.world().turn() == turn + 1, "номер хода не изменился");
        a.undo();
        check(a.store.world().turn() == turn, "отмена хода не сработала");
        break;
      }
      case 14: a.showSettings(); break;
      case 16: a.closeDialogs(); break;
      case 17: a.clearSelection(); break;
      case 20: {
        for (auto& t : a.toasts())
          if (t.kind == app::ToastKind::Danger) check(false, t.text.c_str());
        check(a.frameCount() >= 20, "кадры не рисуются");
        platform::quit(failures ? 1 : 0);
        break;
      }
      default: break;
    }
  }

  void onEvent(const platform::Event& e) override { a.onEvent(e); }
  void onFrame(platform::Frame& f) override {
    try {
      step();
    } catch (const std::exception& e) {
      check(false, e.what());
    }
    a.onFrame(f);
    frame++;
    if (platform::time() - start > 60 && frame > 1) {
      check(false, "самопроверка не уложилась в минуту");
      platform::quit(1);
    }
  }
  bool animating() override { return true; }
  bool onCloseRequest() override { return true; }
};

// Замер в настоящем окне (regnum --bench [мир]): сценарий действий по кадрам, затем отчёт по фазам — частота кадров,
// время кадра приложения по фазам, вывод в окно и ожидание экрана. Данные — во временной папке.
struct Bench final : platform::App {
  app::App& a;
  std::string world;
  struct Phase {
    const char* name;
    int frames;
    std::function<void(Bench&, int)> act;
  };
  struct Acc {
    std::vector<double> interval, frame, build, map, ui, copy, present, wait;
  };
  std::vector<Phase> phases;
  std::vector<Acc> acc;
  size_t phase = 0;
  int frame = 0;
  int prevPhase = -1;
  double prevStart = 0;
  Bench(app::App& app, std::string w) : a(app), world(std::move(w)) {
    auto mapPoint = [](Bench& b, float fx, float fy) {
      const RectF r = b.a.mapArea();
      return Vec2(r.x + r.w * fx, r.y + r.h * fy);
    };
    auto move = [](Bench& b, Vec2 p) {
      platform::Event e;
      e.type = platform::EventType::MouseMove;
      e.x = float(p.x);
      e.y = float(p.y);
      b.a.onEvent(e);
    };
    phases = {
        {"подготовка", 120,
         [](Bench& b, int i) {
           if (i == 0 && !b.world.empty() && !b.a.loadProject(b.world)) platform::quit(1);
           if (i == 90) b.a.map().waitIdle(10);
         }},
        {"покой", 90, [](Bench&, int) {}},
        {"наведение на карту", 150,
         [=](Bench& b, int i) { move(b, mapPoint(b, 0.15f + 0.7f * float(i % 75) / 75.f, 0.35f + 0.003f * float(i % 40))); }},
        {"панорама", 150, [](Bench& b, int i) { b.a.map().panBy(i % 60 < 30 ? 9.f : -9.f, i % 20 < 10 ? 3.f : -3.f); }},
        {"масштаб колесом", 180,
         [=](Bench& b, int i) {
           if (i % 45 == 0) {
             const Vec2 p = mapPoint(b, 0.5f, 0.5f);
             b.a.map().zoomAt(float(p.x), float(p.y), (i / 45) % 2 ? 1 / 2.2 : 2.2, true);
           }
         }},
        {"крупно: панорама", 150,
         [](Bench& b, int i) {
           if (i == 0) b.a.map().centerOn(Vec2(4000, 1600), b.a.map().maxZoom() * 0.5, false);
           else b.a.map().panBy(i % 50 < 25 ? 10.f : -10.f, 2.f);
         }},
        {"весь мир", 90, [](Bench& b, int i) {
           if (i == 0) b.a.map().fitAll(false);
         }},
    };
    acc.resize(phases.size());
  }

  void onEvent(const platform::Event& e) override { a.onEvent(e); }
  void onFrame(platform::Frame& f) override {
    const double now = nowSeconds();
    if (prevPhase >= 0) {  // показ предыдущего кадра закончился только сейчас
      const platform::PresentStats ps = platform::presentStats();
      Acc& p = acc[size_t(prevPhase)];
      p.interval.push_back((now - prevStart) * 1000);
      p.present.push_back(ps.presentMs);
      p.wait.push_back(ps.waitMs);
    }
    if (phase >= phases.size()) {
      report();
      platform::quit(0);
      a.onFrame(f);
      return;
    }
    try {
      phases[phase].act(*this, frame);
    } catch (const std::exception& e) {
      logError("Замер: %s", e.what());
    }
    a.onFrame(f);
    const app::App::FrameProfile& fp = a.frameProfile();
    Acc& p = acc[phase];
    p.frame.push_back(fp.total);
    p.build.push_back(fp.build);
    p.map.push_back(fp.map);
    p.ui.push_back(fp.ui);
    p.copy.push_back(fp.copy);
    prevPhase = int(phase);
    prevStart = now;
    if (++frame >= phases[phase].frames) {
      frame = 0;
      phase++;
    }
  }
  bool animating() override { return true; }
  bool onCloseRequest() override { return true; }

  static double avg(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? 0 : s / double(v.size());
  }
  static double pct(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
  }
  void report() {
    const platform::PresentStats ps = platform::presentStats();
    std::string out = "Замер Regnum: окно " + std::to_string(int(a.mapArea().w)) + "×" + std::to_string(int(a.mapArea().h)) +
                      " (карта, логические пиксели), период экрана " + std::to_string(ps.periodMs).substr(0, 5) + " мс\n";
    char line[512];
    for (size_t i = 1; i < phases.size(); i++) {
      const Acc& p = acc[i];
      const double fps = p.interval.empty() ? 0 : 1000.0 / avg(p.interval);
      std::snprintf(line, sizeof line,
                    "  %-20s %5.1f к/с (интервал 95%% %5.1f) | кадр %5.1f (95%% %5.1f) = сборка %4.1f + карта %4.1f + интерфейс %4.1f + "
                    "копия %3.1f | вывод %4.1f | ожидание %4.1f\n",
                    phases[i].name, fps, pct(p.interval, 0.95), avg(p.frame), pct(p.frame, 0.95), avg(p.build), avg(p.map), avg(p.ui),
                    avg(p.copy), avg(p.present), avg(p.wait));
      out += line;
    }
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
    logInfo("%s", out.c_str());
  }
};

// Мир проекта рядом с программой (12_Карты/Мир_Меча_и_Магии выше по дереву папок).
std::string findProjectWorld() {
  std::string base = fs::exeDir();
  for (int i = 0; i < 8; i++) {
    const std::string p = fs::join(base, "12_Карты/Мир_Меча_и_Магии");
    if (fs::isFile(fs::join(p, "world.json"))) return fs::absolute(p);
    base = fs::join(base, "..");
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) args.push_back(argv[i]);
  bool selftest = false, bench = false;
  std::string path;
  for (auto& s : args) {
    if (s == "--selftest") selftest = true;
    else if (s == "--bench") bench = true;
    else if (!s.empty() && s[0] != '-' && path.empty()) path = s;
  }
  std::string dataDir = selftest ? fs::join(fs::tempDir(), "regnum-selftest") : bench ? fs::join(fs::tempDir(), "regnum-bench") : fs::userDataDir();
  fs::makeDirs(dataDir);
  setLogFile(fs::join(dataDir, "regnum.log"));
  logInfo("Regnum: запуск%s", selftest ? " (самопроверка)" : "");

  std::string err;
  if (!gfx::initFonts(&err)) {
    logError("%s", err.c_str());
    platform::showFatal("Regnum", err.empty() ? std::string("Не найден системный шрифт с кириллицей.") : err);
    return 1;
  }
  for (const std::string& line : gfx::fontReport()) logInfo("%s", line.c_str());

  int code = 0;
  try {
    app::AppConfig cfg;
    cfg.dataDir = dataDir;
    cfg.savePrefs = !selftest && !bench;
    app::App a(cfg);
    if (!path.empty() && !bench) a.loadProject(fs::absolute(path));
    platform::WindowConfig wc;
    wc.title = "Regnum";
    wc.width = 1440;
    wc.height = 900;
    wc.minWidth = 1100;
    wc.minHeight = 700;
    wc.maximized = !selftest;
    wc.darkFrame = true;   // все цветокоры тёмные
    wc.appName = "Regnum";
    wc.placementFile = selftest || bench ? std::string() : fs::join(dataDir, "window.ini");
    try {
      if (selftest) {
        SelfTest st(a);
        code = platform::run(st, wc);
        std::printf("selftest: %s (%d кадров)\n", code == 0 ? "OK" : "FAILED", st.frame);
      } else if (bench) {
        Bench b(a, path.empty() ? findProjectWorld() : fs::absolute(path));
        code = platform::run(b, wc);
      } else {
        code = platform::run(a, wc);
      }
    } catch (const std::exception& e) {
      // Аварийное автосохранение: при следующем запуске экран запуска предложит восстановить работу.
      bool saved = false;
      if (a.dirty()) {
        try {
          io::writeAutosave(a.store.world(), a.projectPath(), dataDir);
          saved = true;
        } catch (...) {
        }
      }
      logError("Аварийное завершение: %s", e.what());
      platform::showFatal("Regnum", std::string("Редактор остановлен из-за внутренней ошибки:\n") + e.what() +
                                        (saved ? "\n\nНесохранённая работа записана — при следующем запуске её можно восстановить." : ""));
      code = 2;
    }
  } catch (const std::exception& e) {
    logError("Не удалось запустить редактор: %s", e.what());
    platform::showFatal("Regnum", std::string("Не удалось запустить редактор:\n") + e.what());
    code = 2;
  }
  logInfo("Regnum: завершение, код %d", code);
  return code;
}
