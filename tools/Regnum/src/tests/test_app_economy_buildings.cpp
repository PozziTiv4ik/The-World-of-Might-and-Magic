// Сценарии ресурсов построек (ТЗ «Виды государств», п.4, 8): у уровня в дереве построек — «Даёт за ход» (любые
// ресурсы, в том числе трупы и демоническая энергия — они есть в справочнике с создания мира), количество до
// тысячных; достроенный уровень даёт ресурсы владельцу каждый ход (золото — в доход), видно во вкладке «Постройки»
// провинции, в выборе строительства и во вкладке «Экономика» провинции.
#include "app/editors/buildings.h"
#include "tests/test_app_economy_util.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::econtest;
using namespace rg::apptest::trees;

TEST(app_economy_building_produce) {
  Harness h("economy_building_produce", 1600, 1100);
  h.demo();
  h.dropToasts();
  // Постройка общего дерева, достроенная в провинции государства (не улучшается сейчас).
  Id bid = 0, pid = 0, owner = 0;
  int lvl = 0;
  h->world().provinces.each([&](const Province& p) {
    const Faction* f = h->world().faction(p.owner);
    if (bid || p.sea || !f || !f->isState()) return;
    for (const ProvBuilding& pb : p.buildings)
      if (!bid && pb.builtLevel() > 0 && !pb.constructing)
        if (const Building* b = h->world().building(pb.building); b && b->owner == 0) {
          bid = b->id;
          pid = p.id;
          owner = p.owner;
          lvl = pb.builtLevel();
        }
  });
  CHECK(bid && pid && owner && lvl > 0);
  // Трупы — встроенный ресурс справочника с создания мира (сразу видно наличие у государства).
  const Id corpses = rules::resourceId(h->world(), schema::kResCorpses);
  CHECK(corpses != 0);
  CHECK(!h->world().building(bid)->levels[size_t(lvl - 1)].produce.count(corpses));
  const std::string L = "bt.level." + std::to_string(lvl - 1);
  app::openBuildingTree(h.a(), 0, bid);
  h.settle();
  CHECK(pickSide(h, L + ".addproduce", "Трупы"));
  CHECK_EQ(rules::resourceId(h->world(), schema::kResCorpses), corpses);
  CHECK_NEAR(h->world().building(bid)->levels[size_t(lvl - 1)].produce.at(corpses), 1, 1e-12);
  CHECK(typeSide(h, L + ".produce." + std::to_string(corpses), "2,5"));
  CHECK_NEAR(h->world().building(bid)->levels[size_t(lvl - 1)].produce.at(corpses), 2.5, 1e-12);
  // Золото за ход — до тысячных.
  CHECK(pickSide(h, L + ".addproduce", "Золото"));
  CHECK(typeSide(h, L + ".produce." + std::to_string(kGold), "0,125"));
  CHECK_NEAR(h->world().building(bid)->levels[size_t(lvl - 1)].produce.at(kGold), 0.125, 1e-12);
  CHECK(revealSide(h, L + ".produce." + std::to_string(corpses)));
  shotClean(h, "economy_building_produce_tree");
  // Расчёт: каждая провинция с достроенным уровнем даёт владельцу ресурсы.
  int count = 0;
  h->world().provinces.each([&](const Province& p) {
    const Faction* f = h->world().faction(p.owner);
    if (p.sea || p.owner != owner || !f) return;
    for (const ProvBuilding& pb : p.buildings)
      if (pb.building == bid && pb.builtLevel() == lvl) count++;
  });
  CHECK(count >= 1);
  auto c = rules::calc(h->world());
  CHECK_NEAR(c->province(pid)->produce.at(corpses), 2.5, 1e-12);
  CHECK_NEAR(c->faction(owner)->resources.at(corpses).production, 2.5 * count, 1e-9);
  // Ход: трупы — в запас, золото — в доход.
  const double corpses0 = h->world().faction(owner)->stock(corpses);
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK_NEAR(h->world().faction(owner)->stock(corpses), corpses0 + 2.5 * count, 1e-9);
  h.key(Key::Escape);
  h.settle();
  // Провинция: вкладка «Постройки» и «Экономика».
  openProvince(h, pid, "province.buildings");
  shotClean(h, "economy_building_produce_province");
  openProvince(h, pid, "province.economy");
  CHECK(h->uiRect("province.produce") != nullptr);
  shotClean(h, "economy_building_produce_economy");
  // Убрать ресурс из уровня — отменяется.
  app::openBuildingTree(h.a(), 0, bid);
  h.settle();
  CHECK(revealSide(h, L + ".produce." + std::to_string(corpses)));
  const RectF* f = h->uiRect(L + ".produce." + std::to_string(corpses));
  CHECK(f != nullptr);
  h.click(f->right() + 6 + 15, f->cy());   // крестик справа от поля
  h.settle();
  CHECK(!h->world().building(bid)->levels[size_t(lvl - 1)].produce.count(corpses));
  h.key(Key::Z, ctrl());
  CHECK(h->world().building(bid)->levels[size_t(lvl - 1)].produce.count(corpses));
}
