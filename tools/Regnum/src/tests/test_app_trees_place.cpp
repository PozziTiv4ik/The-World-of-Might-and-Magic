// Сценарии построек провинции (ТЗ «Доработки», п.2–3, 5; «Ввод новых механик»): «Завершить сейчас» у стройки,
// «Поставить готовой» в выборе строительства (изначальные постройки, без цены и срока, с выбором уровня), культовая
// постройка — одна на карту, постройка преобразования — «Работает / Простаивает», рецепт, ход цикла, «Ждёт
// ресурсы»; генерация эссенции — что даёт за ход.
#include "app/editors/buildings.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;

namespace {

Id resourceByName(const World& w, const std::string& name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}

Id essenceByName(const World& w, const std::string& name) {
  for (const CatalogItem& c : w.catalogs->essences)
    if (c.name == name) return c.id;
  return 0;
}

void setGold(app::App& a, Id faction, double v) {
  CHECK(a.act("Казна", [&](Tx& tx) { tx.faction(faction).res[kGold] = v; }));
}

void openTab(Harness& h, Id pid) {
  h->ui.tabOf[app::SelType::Province] = "province.buildings";
  h->select(app::SelType::Province, pid);
  h.settle(40);
}

// Общая постройка из трёх уровней (золото, сроки 2–4 хода).
Id threeLevels(app::App& a, const char* name, BuildingCat cat = BuildingCat::Economic) {
  Id id = 0;
  CHECK(a.act("Постройка", [&](Tx& tx) {
    id = rules::createBuilding(tx, 0, name);
    Building& b = tx.building(id);
    b.cat = cat;
    b.levels.clear();
    for (int l = 0; l < 3; l++) {
      BuildingLevel lv;
      lv.turns = 2 + l;
      lv.cost[kGold] = 100.0 * (l + 1);
      b.levels.push_back(lv);
    }
  }));
  return id;
}

}  // namespace

TEST(app_trees_place_complete_and_ready) {
  HideTestRegs hide;
  Harness h("trees_place", 1440, 1200);
  h.demo();
  h.dropToasts();
  StateProv sp = stateWithFreeSlots(h->world(), 3);
  CHECK(sp.province != 0);
  const Id fresh = threeLevels(h.a(), "Обсерватория");
  const Id ready = threeLevels(h.a(), "Библиотека");
  setGold(h.a(), sp.state, 10000);
  // Стройка: «Завершить сейчас» — уровень готов сразу, уплаченное остаётся уплаченным.
  double price = 0;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province))
    if (o.building == fresh) price = o.cost.count(kGold) ? o.cost.at(kGold) : 0;
  CHECK(price > 0);
  CHECK(h->act("Начать", [&](Tx& tx) { rules::startBuilding(tx, sp.province, fresh); }));
  const double gold1 = h->world().faction(sp.state)->treasury();
  CHECK_NEAR(gold1, 10000 - price, 1e-6);
  openTab(h, sp.province);
  CHECK(h->uiRect("prov.complete." + std::to_string(fresh)) != nullptr);
  shotTrees(h, "trees_place_constructing");
  {
    RectF cr = rectOf(h.a(), "prov.complete." + std::to_string(fresh));
    h.click(cr.cx(), cr.cy());
    CHECK(hasToast(h.a(), "Достроено", app::ToastKind::Success));
    h.settle(40);
  }
  const ProvBuilding* pb = provBuilding(h->world(), sp.province, fresh);
  CHECK(pb != nullptr);
  CHECK(!pb->constructing);
  CHECK_EQ(pb->level, 1);
  CHECK_EQ(pb->builtLevel(), 1);
  CHECK_NEAR(h->world().faction(sp.state)->treasury(), gold1, 1e-6);
  h->toasts().clear();
  undo(h);
  CHECK(provBuilding(h->world(), sp.province, fresh)->constructing);
  redo(h);
  CHECK(!provBuilding(h->world(), sp.province, fresh)->constructing);

  // «Поставить готовой»: многоуровневая — сразу уровень II, без цены; окно остаётся открытым.
  clickQuick(h, "prov.build");
  CHECK(h->hasDialog("build.picker"));
  clickQuick(h, "build.search");
  h.type("Библиотека");
  h.settle(40);
  CHECK(h->uiRect("build.place." + std::to_string(ready)) != nullptr);
  RectF lv = rectOf(h.a(), "build.placeLevel." + std::to_string(ready));
  h.click(lv.x + lv.w * (1.5f / 3), lv.cy());   // уровни I, II, III — второй
  h.settle(40);
  shotTrees(h, "trees_place_picker");
  const double gold2 = h->world().faction(sp.state)->treasury();
  {
    RectF pr = rectOf(h.a(), "build.place." + std::to_string(ready));
    h.click(pr.cx(), pr.cy());
    CHECK(ui::toastCount() > 0);   // над окном — уведомление слоя интерфейса
    h.settle(40);
  }
  pb = provBuilding(h->world(), sp.province, ready);
  CHECK(pb != nullptr);
  CHECK(!pb->constructing);
  CHECK_EQ(pb->level, 2);
  CHECK_NEAR(h->world().faction(sp.state)->treasury(), gold2, 1e-6);
  CHECK(h->hasDialog("build.picker"));
  // Уже стоящая на II — следующий уровень (III) тоже ставится готовым.
  h.settle(40);
  CHECK(h->uiRect("build.placeLevel." + std::to_string(ready)) == nullptr);   // один вариант — без выбора
  clickQuick(h, "build.place." + std::to_string(ready));
  CHECK_EQ(provBuilding(h->world(), sp.province, ready)->level, 3);
  // Наибольший уровень — ставить нечего.
  h.settle(40);
  u64 v = h->store.version();
  clickQuick(h, "build.place." + std::to_string(ready));
  CHECK_EQ(h->store.version(), v);
  CHECK(h.clickUi("dialog.cancel"));
  h.settle(40);

  // Культовая постройка общего дерева — одна на всю карту: во второй провинции «Поставить готовой» недоступно.
  Id cult = 0;
  CHECK(h->act("Культовая", [&](Tx& tx) {
    cult = rules::createBuilding(tx, 0, "Храм Вечного Пламени");
    tx.building(cult).cat = BuildingCat::Cult;
  }));
  clickQuick(h, "prov.build");
  clickQuick(h, "build.search");
  h.type("Храм Вечного");
  h.settle(40);
  clickQuick(h, "build.place." + std::to_string(cult));
  CHECK(provBuilding(h->world(), sp.province, cult) != nullptr);
  CHECK(h.clickUi("dialog.cancel"));
  h.settle(40);
  StateProv other = stateWithFreeSlots(h->world(), 1, sp.state);
  CHECK(other.province != 0);
  openTab(h, other.province);
  clickQuick(h, "prov.build");
  clickQuick(h, "build.search");
  h.type("Храм Вечного");
  h.settle(40);
  shotTrees(h, "trees_place_cult");
  v = h->store.version();
  clickQuick(h, "build.place." + std::to_string(cult));
  CHECK_EQ(h->store.version(), v);
  CHECK(provBuilding(h->world(), other.province, cult) == nullptr);
  bool reason = false;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), other.province))
    if (o.building == cult)
      for (const std::string& r : o.reasons) reason = reason || r.find("одна на всю карту") != std::string::npos;
  CHECK(reason);
}

TEST(app_trees_place_converter) {
  HideTestRegs hide;
  Harness h("trees_place_convert", 1440, 1200);
  h.demo();
  h.dropToasts();
  StateProv sp = stateWithFreeSlots(h->world(), 2);
  CHECK(sp.province != 0);
  // Ресурсы, которых провинции не добывают: шёлк и ртуть → магический порох за 2 хода.
  const Id silk = resourceByName(h->world(), "Шелк"), mercury = resourceByName(h->world(), "Ртуть");
  const Id powder = resourceByName(h->world(), "Магический порох");
  const Id flame = essenceByName(h->world(), "Эссенция пламени");
  CHECK(silk && mercury && powder && flame);
  Id conv = 0, gen = 0;
  CHECK(h->act("Постройки", [&](Tx& tx) {
    conv = rules::createBuilding(tx, 0, "Алхимическая мастерская");
    tx.building(conv).cat = BuildingCat::Industrial;
    rules::setBuildingRole(tx, conv, rules::BuildingRole::Convert, true);
    Recipe r;
    r.in = {ResAmount{silk, 10}, ResAmount{mercury, 5}};
    r.out = ResAmount{powder, 3};
    r.turns = 2;
    rules::setRecipe(tx, conv, r);
    gen = rules::createBuilding(tx, 0, "Источник пламени");
    tx.building(gen).cat = BuildingCat::Religious;
    rules::setBuildingRole(tx, gen, rules::BuildingRole::Essence, true);
    rules::setLevelEssence(tx, gen, 1, flame, 4);
    rules::placeBuilding(tx, sp.province, conv, 1);
    rules::placeBuilding(tx, sp.province, gen, 1);
    Faction& f = tx.faction(sp.state);
    f.res[silk] = 100;
    f.res[mercury] = 100;
  }));
  openTab(h, sp.province);
  // Работает, ресурсов хватает: новый цикл начнётся в конце хода.
  CHECK(revealIn(h, "prov.convert." + std::to_string(conv)));
  CHECK(h->uiRect("prov.recipe." + std::to_string(conv)) != nullptr);
  CHECK(h->uiRect("prov.cycleStart." + std::to_string(conv)) != nullptr);
  CHECK(h->uiRect("prov.waiting." + std::to_string(conv)) == nullptr);
  // Генерация эссенции: что постройка даёт за ход.
  CHECK(h->uiRect("prov.essence." + std::to_string(gen)) != nullptr);
  CHECK_NEAR(rules::calc(h->world())->province(sp.province)->essence.at(flame), 4, 1e-12);
  shotTrees(h, "trees_place_convert");
  // «Простаивает» — ничего не делает: шага в плане хода нет.
  RectF sw = rectOf(h.a(), "prov.convert." + std::to_string(conv));
  h.click(sw.x + sw.w * 0.75f, sw.cy());
  h.settle(40);
  CHECK(provBuilding(h->world(), sp.province, conv)->idle);
  auto planned = [&] {
    const rules::FactionCalc* fc = rules::calc(h->world())->faction(sp.state);
    if (!fc) return false;
    for (const rules::ConvertStep& s : fc->conversions)
      if (s.building == conv && s.province == sp.province) return true;
    return false;
  };
  CHECK(!planned());
  CHECK(h->uiRect("prov.cycleStart." + std::to_string(conv)) == nullptr);
  CHECK(h->uiRect("prov.waiting." + std::to_string(conv)) == nullptr);
  undo(h);
  CHECK(!provBuilding(h->world(), sp.province, conv)->idle);
  CHECK(planned());
  // Ход: цикл начат (входы списаны), ещё ход до конца.
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK_EQ(provBuilding(h->world(), sp.province, conv)->cycle, 1);
  CHECK_NEAR(h->world().faction(sp.state)->stock(silk), 90, 1e-9);
  CHECK_NEAR(h->world().faction(sp.state)->stock(mercury), 95, 1e-9);
  h.settle(40);
  CHECK(revealIn(h, "prov.cycle." + std::to_string(conv)));
  shotTrees(h, "trees_place_convert_cycle");
  // Следующий ход: цикл завершён — магический порох поступил.
  const double powder0 = h->world().faction(sp.state)->stock(powder);
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK_NEAR(h->world().faction(sp.state)->stock(powder), powder0 + 3, 1e-9);
  CHECK_EQ(provBuilding(h->world(), sp.province, conv)->cycle, 0);
  // Нет ресурсов на входе — «Ждёт ресурсы».
  CHECK(h->act("Пусто", [&](Tx& tx) {
    tx.faction(sp.state).res[silk] = 0;
    tx.faction(sp.state).res[mercury] = 0;
  }));
  h.settle(40);
  CHECK(!planned());
  CHECK(revealIn(h, "prov.waiting." + std::to_string(conv)));
  shotTrees(h, "trees_place_convert_waiting");
}
