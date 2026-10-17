// Производительность кадра оболочки: интерфейс поверх закешированной карты, кадр с перерисовкой карты,
// экран запуска, окно выбора постройки, завершение хода. Печатает средние времена; пороги — с большим запасом (защита
// от регрессий в разы).
#include <cstdio>

#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Среднее и худшее время кадра (мс) за n кадров; before(i) — действие перед кадром i.
std::pair<double, double> frameStats(int n, const std::function<void(int)>& before) {
  double total = 0, worst = 0;
  for (int i = 0; i < n; i++) {
    const double ms = frameMs(1, [&](int) { before(i); });
    total += ms;
    worst = std::max(worst, ms);
  }
  return {total / n, worst};
}

}  // namespace

TEST(app_perf_frames) {
  Harness h("perf", 1600, 1000);
  h.demo();
  h.waitMap();
  auto vp = h.visibleProvince();
  CHECK(vp.has_value());
  h->select(app::SelType::Province, vp->first);
  h.waitMap();
  h.dropToasts();
  h.settle();
  // Интерфейс: указатель ходит по инспектору (карта не меняется — берётся из кеша).
  const RectF* ins = h->uiRect("inspector");
  CHECK(ins != nullptr);
  RectF r = *ins;
  double ui = frameMs(40, [&](int i) { hl::mouseMove(r.x + 40 + float(i % 7) * 30, r.y + 300 + float(i % 5) * 20); });
  // Карта: камера сдвигается каждый кадр (перерисовка карты).
  double mapMs = frameMs(30, [&](int i) {
    h->map().panBy(i % 2 ? 7.f : -7.f, 3.f);
    h->map().waitIdle(5);
  });
  // Наведение на карту: подсветка провинции меняется — перерисовка карты.
  RectF area = h->mapArea();
  double hover = frameMs(30, [&](int i) { hl::mouseMove(area.x + 60 + float(i) * 13, area.cy()); });
  // Модальное окно поверх карты (фон под ним кешируется).
  h->showSettings();
  h.settle();
  double modal = frameMs(30, [&](int i) { hl::mouseMove(700 + float(i % 9) * 11, 420); });
  std::printf("  модальное окно: %.2f мс\n", modal);
  CHECK(modal < rg::test::perf(60));
  h->closeDialogs();
  h.settle();
  h->closeWorld();
  h.settle();
  if (h->hasDialog("choice")) {
    h.clickUi("dialog.button.0");
    h.settle();
  }
  double start = frameMs(30, [&](int i) { hl::mouseMove(200 + float(i) * 9, 300); });
  std::printf("  кадр 1600×1000: интерфейс %.2f мс, панорама карты %.2f мс, наведение на карту %.2f мс, экран запуска %.2f мс\n", ui, mapMs, hover,
              start);
  CHECK(ui < rg::test::perf(40));
  CHECK(mapMs < rg::test::perf(200));
  CHECK(start < rg::test::perf(60));
}

// Окно выбора постройки поверх карты: время кадра при движении указателя (и тот же замер без окна). Порог — по
// REGNUM_AUDIT=1.
TEST(app_perf_build_picker) {
  Harness h("perf_build_picker", 1920, 1080);
  h->setUiScale(1.5f);
  h.demo();
  h.waitMap();
  CHECK(h->openDialog("build.picker", test::firstOwnedLand(h->world())));
  h.settle();
  auto move = [&](int i) { hl::mouseMove(600 + float(i % 13) * 37, 300 + float(i % 7) * 50); };
  auto [avg, worst] = frameStats(40, move);
  std::printf("  [perf] build.picker 1920x1080 ui1.5: средний кадр %.1f мс, худший %.1f мс\n", avg, worst);
  h->closeDialogs();
  h.settle();
  auto [avg2, worst2] = frameStats(40, move);
  std::printf("  [perf] карта 1920x1080 ui1.5: средний кадр %.1f мс, худший %.1f мс\n", avg2, worst2);
  if (test::auditStrict()) CHECK(avg < 100);
}

// Завершение хода: длительность расчёта в кадре.
TEST(app_perf_end_turn) {
  Harness h("perf_end_turn");
  h.demo();
  h.waitMap();
  double worst = 0;
  for (int k = 0; k < 3; k++) {
    double t0 = nowSeconds();
    CHECK(h->endTurnNow());
    double ms = (nowSeconds() - t0) * 1000;
    worst = std::max(worst, ms);
    h.settle();
  }
  std::printf("  [perf] endTurnNow: худшее %.1f мс\n", worst);
}
