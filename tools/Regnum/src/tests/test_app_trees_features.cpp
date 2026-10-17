// Сценарии новых возможностей построек (ТЗ «Доработки №2–4»): в дереве — категория «Инфраструктурные», значки
// возможностей, правка иссушения, добычи, снижения содержания армии, рынка, видов государства, производства при
// ресурсе провинции и нескольких рецептов преобразования (сталелитейная — 9 рецептов); в провинции — выбор рецепта
// (пока не идёт цикл), иссушение (режим, раса, сколько отнимается и сколько эссенции), прибавка добычи шахты, лечение
// лечебными травами; правила игрока — одна новая постройка в провинции за ход, чужая провинция — только просмотр,
// «Завершить сейчас» — только администратору.
#include "app/editors/buildings.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;

namespace {

const Building* bld(Harness& h, Id b) { return h->world().building(b); }

// Выбор в выпадающем списке инспектора: прокрутка к нему, щелчок, поиск, Enter.
bool pickIn(Harness& h, const std::string& combo, const std::string& text) {
  if (!revealIn(h, combo)) return false;
  clickQuick(h, combo);
  h.type(text);
  h.key(Key::Enter);
  h.settle(40);
  return true;
}

Id resByName(Harness& h, const char* name) { return test::idByName(h->world().catalogs->resources, name); }

// Указатель — на пустую часть верхней строки (снимок без всплывающих подсказок).
void away(Harness& h) {
  h.move(900, 20);
  h.settle(40);
}

}  // namespace

// ---------------------------------------------------------------- дерево построек
TEST(app_trees_features_editor) {
  Harness h("trees_features_editor", 1600, 1100);
  h.demo();
  h.dropToasts();
  app::openBuildingTree(h.a(), 0);
  h.settle(40);
  // Встроенные постройки новых возможностей — в общем дереве; лечебница — в дорожке «Инфраструктурные».
  const Id infirmary = test::buildingByKey(h->world(), schema::bld::Infirmary), steel = test::buildingByKey(h->world(), schema::bld::Steelworks);
  CHECK(infirmary && steel);
  CHECK(bld(h, infirmary)->cat == BuildingCat::Infrastructure);
  CHECK(h->uiRect("bt.node." + std::to_string(infirmary)) != nullptr);
  shotClean(h, "trees_features_tree", 60);

  // Новая постройка: возможности значками.
  clickQuick(h, "bt.add");
  Id b = 0;
  h->world().buildings.each([&](const Building& x) {
    if (x.owner == 0) b = std::max(b, x.id);
  });
  CHECK(b != 0);
  h.retype("Кровавая мельница");
  h.key(Key::Enter);
  h.settle(40);
  // Иссушение: по уровню — жителей за ход и эссенция.
  CHECK(clickSide(h, "bt.role.drain"));
  CHECK(bld(h, b)->drain);
  CHECK(typeSide(h, "bt.level.0.drainpop", "150"));
  CHECK_EQ(bld(h, b)->levels[0].drainPop, i64(150));
  const Id blood = test::idByName(h->world().catalogs->essences, "Эссенция крови");
  CHECK(blood != 0);
  CHECK(pickSide(h, "bt.level.0.adddrainess", "крови"));
  CHECK_NEAR(bld(h, b)->levels[0].drainEss.at(blood), 1, 1e-12);
  CHECK(typeSide(h, "bt.level.0.drainess." + std::to_string(blood), "40"));
  CHECK_NEAR(bld(h, b)->levels[0].drainEss.at(blood), 40, 1e-12);
  // Добыча: группа ресурсов, исключение, +% уровня.
  CHECK(clickSide(h, "bt.role.extract"));
  CHECK(bld(h, b)->extract);
  CHECK(pickSide(h, "bt.extract.addgroup", "Руда"));
  CHECK_EQ(bld(h, b)->extractGroups.size(), size_t(1));
  const Id copper = resByName(h, "Медь");   // однозначное название («Железо» нашло бы и «Алое железо»)
  CHECK(pickSide(h, "bt.extract.addexclude", "Медь"));
  CHECK(bld(h, b)->extractExclude == std::vector<Id>{copper});
  CHECK(typeSide(h, "bt.level.0.extract", "35"));
  CHECK_NEAR(bld(h, b)->levels[0].extractPct, 35, 1e-12);
  // Снижение содержания армии: не больше 50 %.
  CHECK(clickSide(h, "bt.role.upkeep"));
  CHECK(bld(h, b)->upkeepCut);
  CHECK(typeSide(h, "bt.level.0.upkeep", "70"));
  CHECK(bld(h, b)->levels[0].upkeepCut <= schema::kMaxUpkeepCut + 1e-9);
  CHECK(typeSide(h, "bt.level.0.upkeep", "5"));
  CHECK_NEAR(bld(h, b)->levels[0].upkeepCut, 5, 1e-12);
  // Рынок и «Только для вида государства»: нежить.
  CHECK(clickSide(h, "bt.flag.market"));
  CHECK(bld(h, b)->market);
  CHECK(clickSide(h, "bt.kind." + std::to_string(int(StateKind::Undead))));
  CHECK_EQ(bld(h, b)->stateKinds, 1u << unsigned(StateKind::Undead));
  // Производство при ресурсе провинции: древесина 20, с древесиной — 100.
  const Id wood = resByName(h, "Древесина");
  CHECK(wood != 0);
  CHECK(h->act("Производство", [&](Tx& tx) { tx.building(b).levels[0].produce[wood] = 20; }));
  h.settle(40);
  CHECK(pickSide(h, "bt.level.0.addonres", "Древесина"));
  CHECK_NEAR(bld(h, b)->levels[0].produceOnRes.at(wood), 100, 1e-12);   // ×5 обычного по умолчанию
  CHECK(revealSide(h, "bt.side.roles"));
  shotClean(h, "trees_features_side", 60);
  // Карточка на схеме: значки возможностей.
  {
    RectF nr = rectOf(h.a(), "bt.node." + std::to_string(b));
    h.move(nr.x + nr.w * 0.5f, nr.y + nr.h * 0.3f);
    h.frames(40);
    h.settle(40);
    CHECK(h.shot("trees_features_card"));
  }
  // Выключение возможности снимает её данные; Ctrl+Z возвращает.
  CHECK(clickSide(h, "bt.role.drain"));
  CHECK(!bld(h, b)->drain);
  CHECK(bld(h, b)->levels[0].drainEss.empty());
  undo(h);
  CHECK_NEAR(bld(h, b)->levels[0].drainEss.at(blood), 40, 1e-12);

  // Сталелитейная: 9 рецептов строками; раскрыть третий, назвать, добавить эссенцию на входе; новый рецепт и удаление.
  app::openBuildingTree(h.a(), 0, steel);
  h.settle(40);
  CHECK_EQ(bld(h, steel)->recipes.size(), size_t(9));
  for (int i = 0; i < 9; i++) CHECK(h->uiRect("bt.recipe." + std::to_string(i) + ".row") != nullptr);
  CHECK(clickSide(h, "bt.recipe.2.open"));
  CHECK(h->uiRect("bt.recipe.2.name") != nullptr);
  CHECK(typeSide(h, "bt.recipe.2.name", "Оскверненная плавка"));
  CHECK_EQ(bld(h, steel)->recipes[2].name, std::string("Оскверненная плавка"));
  CHECK(clickSide(h, "bt.recipe.2.addess"));
  CHECK_EQ(bld(h, steel)->recipes[2].essIn.size(), size_t(1));
  CHECK(typeSide(h, "bt.recipe.2.essamount.0", "3"));
  CHECK_NEAR(bld(h, steel)->recipes[2].essIn[0].amount, 3, 1e-12);
  CHECK(revealSide(h, "bt.recipe.2.name"));
  shotClean(h, "trees_features_recipes", 60);
  CHECK(clickSide(h, "bt.recipe.add"));
  CHECK_EQ(bld(h, steel)->recipes.size(), size_t(10));
  CHECK(h->uiRect("bt.recipe.9.name") != nullptr);   // новый — сразу в правке
  CHECK(clickSide(h, "bt.recipe.9.remove"));
  CHECK_EQ(bld(h, steel)->recipes.size(), size_t(9));
  // Подсказка карточки на схеме — рецепты списком.
  {
    RectF nr = rectOf(h.a(), "bt.node." + std::to_string(steel));
    h.move(nr.x + nr.w * 0.5f, nr.y + nr.h * 0.3f);
    h.frames(40);
    h.settle(40);
    CHECK(h.shot("trees_features_recipes_tip"));
  }
}

// Игрок видит дерево построек, но не правит его.
TEST(app_trees_features_editor_player_readonly) {
  Harness h("trees_features_player_tree", 1600, 1000);
  h.demo();
  h.dropToasts();
  const Id st = test::stateWithLand(h->world());
  h->ui.role = app::Role::Player;
  h->ui.playerState = st;
  const Id steel = test::buildingByKey(h->world(), schema::bld::Steelworks);
  app::openBuildingTree(h.a(), 0, steel);
  h.settle(40);
  const u64 v0 = h->store.version();
  CHECK(clickSide(h, "bt.role.convert"));
  CHECK(clickSide(h, "bt.recipe.0.open"));
  CHECK(h->uiRect("bt.recipe.add") == nullptr);
  CHECK_EQ(h->store.version(), v0);
  CHECK(bld(h, steel)->convert);
}

// ---------------------------------------------------------------- провинция
TEST(app_trees_features_province_recipe) {
  Harness h("trees_features_recipe", 1600, 1100);
  h.demo();
  h.dropToasts();
  const StateProv sp = stateWithFreeSlots(h->world(), 1);
  const Id steel = test::buildingByKey(h->world(), schema::bld::Steelworks);
  CHECK(sp.province && steel);
  CHECK(h->act("Сталелитейная", [&](Tx& tx) { rules::placeBuilding(tx, sp.province, steel, 1); }));
  setStock(h.a(), sp.state, 100);
  provinceTab(h, sp.province, "province.buildings");
  const std::string pick = "prov.recipePick." + std::to_string(steel);
  CHECK(revealIn(h, pick));
  // Выбор рецепта: «Бронза».
  CHECK(pickIn(h, pick, "Бронза"));
  const ProvBuilding* pb = provBuilding(h->world(), sp.province, steel);
  CHECK(pb != nullptr);
  CHECK_EQ(bld(h, steel)->recipes[size_t(pb->recipe)].name, std::string("Бронза"));
  CHECK_EQ(h->store.undoLabel(), std::string("Рецепт преобразования"));
  CHECK(h->uiRect("prov.cycleStart." + std::to_string(steel)) != nullptr);   // новый цикл — в конце хода
  away(h);
  shotClean(h, "trees_features_province_recipe", 60);
  // Идёт цикл — рецепт не сменить (поле недоступно).
  CHECK(h->act("Цикл", [&](Tx& tx) {
    for (ProvBuilding& x : tx.province(sp.province).buildings)
      if (x.building == steel) x.cycle = 2;
  }));
  h.settle(40);
  const int before = provBuilding(h->world(), sp.province, steel)->recipe;
  const u64 v0 = h->store.version();
  CHECK(pickIn(h, pick, "Сталь"));
  CHECK_EQ(h->store.version(), v0);
  CHECK_EQ(provBuilding(h->world(), sp.province, steel)->recipe, before);
  CHECK(h->uiRect("prov.cycle." + std::to_string(steel)) != nullptr);
}

TEST(app_trees_features_province_drain) {
  Harness h("trees_features_drain", 1600, 1100);
  h.demo();
  h.dropToasts();
  const StateProv sp = stateWithFreeSlots(h->world(), 1);
  const Id drainer = test::buildingByKey(h->world(), schema::bld::Drainer);
  const Id blood = test::idByName(h->world().catalogs->essences, "Эссенция крови");
  CHECK(sp.province && drainer && blood);
  CHECK(h->act("Иссушительница", [&](Tx& tx) { rules::placeBuilding(tx, sp.province, drainer, 1); }));
  // Раса провинции с наибольшим населением, название которой не входит в названия других рас провинции (поиск в
  // списке — по подстроке).
  const auto& races = h->world().province(sp.province)->races;
  auto raceName = [&](Id r) {
    const CatalogItem* c = Catalogs::find(h->world().catalogs->races, r);
    return c ? c->name : std::string();
  };
  Id race = 0;
  i64 most = 0;
  for (const RacePop& r : races) {
    bool unique = !raceName(r.race).empty();
    for (const RacePop& o : races)
      if (o.race != r.race && utf8::matches(raceName(o.race), raceName(r.race))) unique = false;
    if (unique && r.pop > most) {
      most = r.pop;
      race = r.race;
    }
  }
  CHECK(race != 0 && most > 100);
  provinceTab(h, sp.province, "province.buildings");
  const std::string id = std::to_string(drainer);
  CHECK(revealIn(h, "prov.drainRace." + id));
  CHECK(h->uiRect("prov.drainWhy." + id) != nullptr);   // раса не выбрана — не работает
  const CatalogItem* rc = Catalogs::find(h->world().catalogs->races, race);
  CHECK(rc != nullptr);
  CHECK(pickIn(h, "prov.drainRace." + id, rc->name));
  CHECK_EQ(provBuilding(h->world(), sp.province, drainer)->race, race);
  // План хода: 100 жителей → 50 эссенции крови.
  CHECK(revealIn(h, "prov.drain." + id));
  const rules::FactionCalc* fc = rules::calc(h->world())->faction(sp.state);
  CHECK(fc != nullptr);
  bool planned = false;
  for (const rules::DrainStep& d : fc->drains)
    if (d.province == sp.province && d.building == drainer) planned = d.pop == 100 && std::fabs(d.ess.at(blood) - 50) < 1e-9;
  CHECK(planned);
  away(h);
  shotClean(h, "trees_features_province_drain", 60);
  // «Простаивает» — плана нет; в конце хода население не меняется.
  CHECK(revealIn(h, "prov.drainMode." + id));
  RectF mr = rectOf(h.a(), "prov.drainMode." + id);
  h.click(mr.x + mr.w * 0.75f, mr.cy());
  h.settle(40);
  CHECK(provBuilding(h->world(), sp.province, drainer)->idle);
  for (const rules::DrainStep& d : rules::calc(h->world())->faction(sp.state)->drains) CHECK(!(d.province == sp.province && d.building == drainer));
}

TEST(app_trees_features_heal_herbs) {
  Harness h("trees_features_herbs", 1600, 1000);
  h.demo();
  h.dropToasts();
  const StateProv sp = stateWithFreeSlots(h->world(), 1);
  const Id infirmary = test::buildingByKey(h->world(), schema::bld::Infirmary);
  const Id herbs = rules::healingHerbs(h->world());
  CHECK(sp.province && infirmary && herbs);
  CHECK(h->act("Лечебница и чума", [&](Tx& tx) {
    rules::placeBuilding(tx, sp.province, infirmary, 1);
    rules::infectPlague(tx, sp.province);
    Faction& f = tx.faction(sp.state);
    f.res[herbs] = 600;
    f.ess.clear();   // эссенций нет — лечить можно только травами
  }));
  CHECK(rules::provinceHas(h->world(), sp.province, schema::mod::Plague));
  provinceTab(h, sp.province, "province.buildings");
  const std::string heal = "prov.heal." + std::to_string(infirmary);
  CHECK(revealIn(h, heal));
  clickQuick(h, heal);
  CHECK(h->uiRect(heal + ".herbs") != nullptr);
  shotClean(h, "trees_features_heal_herbs", 60);
  CHECK(h.clickUi(heal + ".herbs"));
  h.settle(40);
  CHECK(!rules::provinceHas(h->world(), sp.province, schema::mod::Plague));
  CHECK_NEAR(h->world().faction(sp.state)->stock(herbs), 100, 1e-9);
  CHECK_EQ(h->store.undoLabel(), std::string("Вылечить провинцию"));
}

TEST(app_trees_features_extract) {
  Harness h("trees_features_extract", 1600, 1000);
  h.demo();
  h.dropToasts();
  const StateProv sp = stateWithFreeSlots(h->world(), 1);
  const Id mine = test::buildingByKey(h->world(), schema::bld::Mine);
  const Id iron = resByName(h, "Железо"), grain = resByName(h, "Зерно");
  CHECK(sp.province && mine && iron && grain);
  // Ресурс не из групп шахты — в выборе строительства причина, поставить нельзя.
  CHECK(h->act("Зерно", [&](Tx& tx) {
    tx.province(sp.province).resource = grain;
    tx.province(sp.province).resourceAmount = 10;
  }));
  bool why = false;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province))
    if (o.building == mine) {
      CHECK(!o.canPlace);
      for (const std::string& r : o.placeReasons) why = why || r.find("не добывается") != std::string::npos;
    }
  CHECK(why);
  CHECK(h->openDialog("build.picker", sp.province));
  h.settle(40);
  CHECK(h.clickUi("build.search"));
  h.type("Шахта");
  h.settle(40);
  shotClean(h, "trees_features_picker_extract", 60);
  h->closeDialogs();
  // Железо: шахта I — добыча +25 %.
  CHECK(h->act("Железо и шахта", [&](Tx& tx) {
    tx.province(sp.province).resource = iron;
    rules::placeBuilding(tx, sp.province, mine, 1);
  }));
  const rules::ProvinceCalc* pc = rules::calc(h->world())->province(sp.province);
  CHECK(pc != nullptr);
  CHECK_NEAR(pc->extractPct, 25, 1e-9);
  provinceTab(h, sp.province, "province.buildings");
  CHECK(revealIn(h, "prov.extract." + std::to_string(mine)));
  provinceTab(h, sp.province, "province.economy");
  CHECK(h->uiRect("province.extract") != nullptr);
}

// Правила игрока: одна новая постройка в провинции за ход, чужая провинция — только просмотр, «Завершить сейчас» и
// «Поставить готовой» — только администратору.
TEST(app_trees_features_player_build) {
  Harness h("trees_features_player", 1600, 1000);
  h.demo();
  h.dropToasts();
  const BuildPick pk = pickBuild(h->world());
  CHECK(pk.pid != 0);
  setStock(h.a(), pk.owner, 100000);
  h->ui.role = app::Role::Player;
  h->ui.playerState = pk.owner;
  CHECK(h->openDialog("build.picker", pk.pid));
  h.settle(40);
  CHECK(h->uiRect("build.place." + std::to_string(pk.building)) == nullptr);   // «Поставить готовой» — нет
  CHECK(h.clickUi("build.search"));
  h.type(bld(h, pk.building)->name);
  h.settle(40);
  CHECK(h.clickUi("build.option." + std::to_string(pk.building)));
  h.settle(40);
  const ProvBuilding* pb = provBuilding(h->world(), pk.pid, pk.building);
  CHECK(pb != nullptr && pb->constructing);
  // «Завершить сейчас» — нет, «Отменить» — есть.
  provinceTab(h, pk.pid, "province.buildings");
  CHECK(h->uiRect("prov.complete." + std::to_string(pk.building)) == nullptr);
  CHECK(h->uiRect("prov.cancel." + std::to_string(pk.building)) != nullptr);
  // Отменил — в этом ходу новую постройку уже не начать.
  CHECK(h->play("Отменить", [&](Tx& tx) { rules::cancelBuilding(tx, pk.pid, pk.building); }));
  h.settle(40);
  bool once = false;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), pk.pid, true))
    for (const std::string& r : o.reasons) once = once || r.find("В этом ходу") != std::string::npos;
  CHECK(once);
  u64 v0 = h->store.version();
  CHECK(revealIn(h, "prov.build"));
  clickQuick(h, "prov.build");
  CHECK(!h->hasDialog("build.picker"));
  CHECK_EQ(h->store.version(), v0);
  away(h);
  shotClean(h, "trees_features_player_turn", 60);
  // Чужая провинция: «Построить» недоступна.
  Id foreign = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!foreign && !p.sea && p.owner && p.owner != pk.owner) foreign = p.id;
  });
  CHECK(foreign != 0);
  provinceTab(h, foreign, "province.buildings");
  CHECK(revealIn(h, "prov.build"));
  clickQuick(h, "prov.build");
  CHECK(!h->hasDialog("build.picker"));
}
