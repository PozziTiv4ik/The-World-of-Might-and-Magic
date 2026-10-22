// Время кадра полноэкранных схем деревьев (технологии, постройки) и вкладки построек провинции.
// Печатает средние времена; пороги — с большим запасом (защита от регрессий в разы).
#include <cstdio>

#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;

TEST(app_trees_perf_frames) {
  Harness h("trees_perf", 1600, 1000);
  h.demo();
  Id fid = 0;
  h->world().techs.each([&](const Tech& t) {
    if (!fid) fid = t.faction;
  });
  // Сорок технологий со связями.
  CHECK(h->act("Дерево", [&](Tx& tx) {
    std::vector<Id> ids;
    for (int k = 0; k < 40; k++) {
      Id id = rules::createTech(tx, fid, "Технология " + std::to_string(k));
      tx.tech(id).desc = "+5 % к доходу в казну";
      if (k >= 3) rules::setPrereq(tx, id, ids[size_t(k / 2)], true);
      ids.push_back(id);
    }
    rules::autoLayout(tx, fid);
  }));
  app::openTechTree(h.a(), fid);
  h.settle();
  // В покое редактор не просит новых кадров.
  for (int i = 0; i < 30 && ui::needsRedraw(); i++) h.frame();
  CHECK(!ui::needsRedraw());
  // Указатель над карточкой: после появления карточки сведений — тоже покой.
  {
    std::vector<Id> ts;
    h->world().techs.each([&](const Tech& t) {
      if (t.faction == fid) ts.push_back(t.id);
    });
    const RectF* nr = h->uiRect("tt.node." + std::to_string(ts.front()));
    CHECK(nr != nullptr);
    h.move(nr->cx(), nr->cy());
    int n = 0;
    for (; n < 200 && ui::needsRedraw(); n++) h.frame();
    std::printf("  кадров до покоя над карточкой: %d\n", n);
    CHECK(n < rg::test::perf(60));
    h.dropToasts();
    h.settle();
    CHECK(h.shot("trees_perf_hover"));
  }
  {
    // Выбранная технология (панель свойств): тоже покой.
    std::vector<Id> ts;
    h->world().techs.each([&](const Tech& t) {
      if (t.faction == fid) ts.push_back(t.id);
    });
    app::openTechTree(h.a(), fid, ts.back());
    h.step();
    int n = 0;
    for (; n < 400 && ui::needsRedraw(); n++) h.frame();
    std::printf("  кадров до покоя с выбранной технологией: %d\n", n);
    CHECK(n < rg::test::perf(60));
  }
  const RectF* cv = h->uiRect("tt.canvas");
  CHECK(cv != nullptr);
  RectF c = *cv;
  double hover = frameMs(30, [&](int i) { hl::mouseMove(c.x + 80 + float(i) * 23, c.cy() + float(i % 5) * 9); });
  double zoom = frameMs(20, [&](int i) { hl::wheel(c.cx(), c.cy(), i % 2 ? 1.f : -1.f); });
  std::printf("  дерево технологий: наведение %.2f мс, масштаб %.2f мс\n", hover, zoom);
  CHECK(hover < rg::test::perf(120));
  CHECK(zoom < rg::test::perf(160));
  app::openBuildingTree(h.a(), 0);
  h.settle();
  for (int i = 0; i < 30 && ui::needsRedraw(); i++) h.frame();
  CHECK(!ui::needsRedraw());
  cv = h->uiRect("bt.canvas");
  CHECK(cv != nullptr);
  c = *cv;
  double bh = frameMs(30, [&](int i) { hl::mouseMove(c.x + 200 + float(i) * 23, c.cy() + float(i % 5) * 9); });
  double bz = frameMs(20, [&](int i) { hl::wheel(c.cx(), c.cy(), i % 2 ? 1.f : -1.f); });
  std::printf("  дерево построек: наведение %.2f мс, масштаб %.2f мс\n", bh, bz);
  CHECK(bh < rg::test::perf(120));
  CHECK(bz < rg::test::perf(160));
}

// Общее дерево базовых записей (с веткой «Стимуляция промышленности» — 99 технологий змейкой): всё дерево на холсте,
// наведение, масштаб и панорама вблизи (карточки и связи за краем холста не строятся).
TEST(app_trees_perf_common_tree) {
  Harness h("trees_perf_common", 1600, 1000);
  h.demo();
  int n = 0;
  h->world().techs.each([&](const Tech& t) { n += t.faction == 0; });
  CHECK(n >= 99);
  app::openTechTree(h.a(), 0);
  h.settle();
  h.key(Key::F);
  h.settle();
  for (int i = 0; i < 30 && ui::needsRedraw(); i++) h.frame();
  CHECK(!ui::needsRedraw());
  const RectF* cv = h->uiRect("tt.canvas");
  CHECK(cv != nullptr);
  const RectF c = *cv;
  const double all = frameMs(20, [&](int i) { hl::mouseMove(c.x + 80 + float(i) * 31, c.cy() + float(i % 5) * 9); });
  const double zoom = frameMs(20, [&](int i) { hl::wheel(c.cx(), c.cy(), i % 2 ? 1.f : -1.f); });
  // Вблизи (100 %): панорама перетаскиванием фона.
  for (int i = 0; i < 12; i++) {
    hl::wheel(c.cx(), c.cy(), 1);
    h.frame();
  }
  h.settle();
  hl::mouseDown(c.cx(), c.cy(), platform::MouseLeft, 0);
  h.frame();
  const double pan = frameMs(20, [&](int i) { hl::mouseMove(c.cx() - float(i) * 17, c.cy() - float(i) * 9); });
  hl::mouseUp(c.cx() - 340, c.cy() - 180, platform::MouseLeft, 0);
  h.settle();
  std::printf("  общее дерево (%d технологий): наведение %.2f мс, масштаб %.2f мс, панорама вблизи %.2f мс\n", n, all, zoom, pan);
  CHECK(all < rg::test::perf(120));
  CHECK(zoom < rg::test::perf(160));
  CHECK(pan < rg::test::perf(120));
}
