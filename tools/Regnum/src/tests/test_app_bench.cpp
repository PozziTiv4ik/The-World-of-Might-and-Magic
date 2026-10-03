// Замер кадра на настоящем мире проекта (12_Карты/Мир_Меча_и_Магии) в размере экрана пользователя: 2560×1440 при
// масштабе 125 %, окно развёрнуто. В обычный прогон не входит: запускается при REGNUM_BENCH=1
// (REGNUM_BENCH=1 tools/Regnum/build.sh --test bench_). Печатает средние и худшие времена фаз кадра для типичных
// действий и время, за которое карта становится чёткой после масштабирования.
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

std::string findWorld() {
  if (const char* e = std::getenv("REGNUM_BENCH_WORLD")) return e;
  for (std::string base : {fs::exeDir(), fs::absolute(".")}) {
    for (int i = 0; i < 8; i++) {
      std::string p = fs::join(base, "12_Карты/Мир_Меча_и_Магии");
      if (fs::isFile(fs::join(p, "world.json"))) return fs::absolute(p);
      base = fs::join(base, "..");
    }
  }
  return {};
}

struct Sample {
  app::App::FrameProfile f;
  map::RenderStats m;
};

void report(const char* name, const std::vector<Sample>& s) {
  if (s.empty()) return;
  auto avg = [&](auto get) {
    double t = 0;
    for (const Sample& x : s) t += get(x);
    return t / double(s.size());
  };
  auto worst = [&](auto get) {
    double t = 0;
    for (const Sample& x : s) t = std::max(t, get(x));
    return t;
  };
  int fallback = 0;
  for (const Sample& x : s) fallback += x.m.fallback ? 1 : 0;
  std::printf("  %-24s кадр %6.1f (худший %6.1f) | сборка %5.1f | карта %6.1f (фон %5.1f, подписи %4.1f) | интерфейс %5.1f | копия %4.1f | "
              "запасные %d/%zu\n",
              name, avg([](const Sample& x) { return x.f.total; }), worst([](const Sample& x) { return x.f.total; }),
              avg([](const Sample& x) { return x.f.build; }), avg([](const Sample& x) { return x.f.map; }),
              avg([](const Sample& x) { return x.m.composeMs; }), avg([](const Sample& x) { return x.m.labelsMs; }),
              avg([](const Sample& x) { return x.f.ui; }), avg([](const Sample& x) { return x.f.copy; }), fallback, s.size());
}

template <class F>
std::vector<Sample> run(Harness& h, int n, F&& before) {
  std::vector<Sample> out;
  for (int i = 0; i < n; i++) {
    before(i);
    hl::renderFrame();
    hl::advance(1.0 / 60);
    out.push_back({h->frameProfile(), h->map().stats()});
  }
  return out;
}

// Время до чёткой карты: фоновая отрисовка всех тайлов, запрошенных кадром.
double sharpMs(Harness& h) {
  const double t0 = nowSeconds();
  for (int i = 0; i < 10; i++) {
    h->map().waitIdle(30);
    hl::renderFrame();
    hl::advance(1.0 / 60);
    if (!h->map().loading()) break;
  }
  return (nowSeconds() - t0) * 1000;
}

}  // namespace

TEST(bench_app_real_world) {
  if (!std::getenv("REGNUM_BENCH")) {
    std::printf("  пропущено: запуск с REGNUM_BENCH=1\n");
    return;
  }
  const std::string world = findWorld();
  CHECK_MSG(!world.empty(), "мир 12_Карты/Мир_Меча_и_Магии не найден (REGNUM_BENCH_WORLD)");
  Harness h("bench", 2048, 1104, 1.25f);
  double t0 = nowSeconds();
  CHECK(h->loadProject(world));
  h.settle();
  const double openMs = (nowSeconds() - t0) * 1000;
  t0 = nowSeconds();
  h.waitMap();
  const double firstSharp = (nowSeconds() - t0) * 1000;
  h.dropToasts();
  h.settle();
  std::printf("  мир: %s\n  открытие %.0f мс, первая чёткая карта ещё %.0f мс\n", world.c_str(), openMs, firstSharp);

  const RectF area = h->mapArea();
  report("наведение на карту", run(h, 60, [&](int i) { hl::mouseMove(area.x + 80 + float(i) * 9, area.cy() + float(i % 5) * 7); }));
  report("панорама", run(h, 60, [&](int i) { h->map().panBy(14.f, i % 2 ? 5.f : -3.f); }));
  std::printf("  чёткость после панорамы: %.0f мс\n", sharpMs(h));

  // Масштаб колесом: три щелчка с анимацией, затем дорисовка тайлов.
  hl::mouseMove(area.cx(), area.cy());
  hl::wheel(area.cx(), area.cy(), 3);
  report("приближение (анимация)", run(h, 30, [](int) {}));
  std::printf("  чёткость после приближения: %.0f мс\n", sharpMs(h));
  report("наведение (приближено)", run(h, 40, [&](int i) { hl::mouseMove(area.x + 120 + float(i) * 11, area.cy() - 40); }));
  hl::wheel(area.cx(), area.cy(), -3);
  report("отдаление (анимация)", run(h, 30, [](int) {}));
  std::printf("  чёткость после отдаления: %.0f мс\n", sharpMs(h));

  // Выбранная провинция: инспектор открыт, указатель ходит по нему (в мире без провинций замер пропускается).
  if (auto vp = h.visibleProvince()) {
    h->select(app::SelType::Province, vp->first);
    h.settle();
    sharpMs(h);
    if (const RectF* ins = h->uiRect("inspector")) {
      const RectF r = *ins;
      report("указатель над инспектором", run(h, 40, [&](int i) { hl::mouseMove(r.x + 40 + float(i % 7) * 30, r.y + 300 + float(i % 5) * 20); }));
    }
    report("наведение (с выделением)", run(h, 40, [&](int i) { hl::mouseMove(area.x + 80 + float(i) * 13, area.cy() + 60); }));
  } else {
    std::printf("  в мире нет провинций: замер инспектора пропущен\n");
  }

  // Режим правки границ.
  h->setEditBorders(true);
  h.settle();
  std::printf("  чёткость после включения правки: %.0f мс\n", sharpMs(h));
  report("правка границ: наведение", run(h, 40, [&](int i) { hl::mouseMove(area.x + 100 + float(i) * 12, area.cy() + 20); }));
  h->setEditBorders(false);
  h.settle();

  // Режим гильдий (диаграммы, маршруты).
  h->setMapMode(schema::MapMode::Guilds);
  h.settle();
  std::printf("  чёткость после смены режима: %.0f мс\n", sharpMs(h));
  report("гильдии: панорама", run(h, 40, [&](int i) { h->map().panBy(i % 2 ? -9.f : 11.f, 4.f); }));
  h->setMapMode(schema::MapMode::Political);
  h.settle();

  // Сильное приближение: панорама по крупному масштабу.
  h->map().centerOn(Vec2(4000, 1600), h->map().maxZoom() * 0.5, false);
  h.settle();
  std::printf("  чёткость при крупном масштабе: %.0f мс\n", sharpMs(h));
  report("крупно: панорама", run(h, 40, [&](int i) { h->map().panBy(16.f, 2.f); }));
  h.shot("bench_end");
}
