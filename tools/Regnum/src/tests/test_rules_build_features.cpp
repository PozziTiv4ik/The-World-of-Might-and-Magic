// Regnum — возможности построек ТЗ «Доработки №4»: рынок, добыча (шахта), производство при ресурсе провинции,
// только для вида государства, несколько рецептов и эссенции в рецепте, иссушение населения, снижение содержания
// армии, лечение травами, чума со «Зданием чумы», тип города по населению; правила игрока — одна стройка в провинции
// за ход, очко действий войска; базовые постройки и записи версии 3.
#include "core/content.h"
#include "core/io.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

Id resNamed(const World& w, const char* n) { return test::idByName(w.catalogs->resources, n); }
Id essNamed(const World& w, const char* n) { return test::idByName(w.catalogs->essences, n); }

// Провинция государства A с населением race.
Id ownedRace(Fix& f, i64 pop) {
  Id race = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, pop);
  });
  return race;
}

}  // namespace

// ---------------------------------------------------------------- тип города
TEST(rules_build_features_city_type_by_population) {
  CHECK(schema::cityTypeFor(0) == CityType::Outpost);
  CHECK(schema::cityTypeFor(999) == CityType::Outpost);
  CHECK(schema::cityTypeFor(1000) == CityType::Village);
  CHECK(schema::cityTypeFor(9999) == CityType::Village);
  CHECK(schema::cityTypeFor(10000) == CityType::Town);
  CHECK(schema::cityTypeFor(49999) == CityType::Town);
  CHECK(schema::cityTypeFor(50000) == CityType::City);
  Fix f;
  ownedRace(f, 12000);
  CHECK(f.pc(f.p[0]).city == CityType::Town);
  CHECK_EQ(f.pc(f.p[0]).slotsCity, 2);
  f.tx([&](Tx& tx) { tx.province(f.p[0]).races[0].pop = 70000; });
  CHECK(f.pc(f.p[0]).city == CityType::City);
  CHECK_EQ(f.pc(f.p[0]).slotsCity, 3);
}

// ---------------------------------------------------------------- рынок, производство, добыча
TEST(rules_build_features_production_and_extraction) {
  Fix f;
  ownedRace(f, 5000);
  const Id wood = resNamed(f.w(), "Древесина"), iron = 5;
  Id saw = 0, mine = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).resource = wood;
    tx.province(f.p[0]).resourceAmount = 10;
    saw = createBuilding(tx, 0, "Лесопилка");
    tx.building(saw).levels[0].produce[wood] = 20;
    setLevelProduceOnRes(tx, saw, 1, wood, 100);
    placeBuilding(tx, f.p[0], saw, 1);
  });
  // Ресурс провинции — «Древесина»: 100 вместо 20 (и добыча самой провинции).
  CHECK_NEAR(f.pc(f.p[0]).produce.at(wood), 100, 1e-9);
  f.tx([&](Tx& tx) { tx.province(f.p[0]).resource = iron; });
  CHECK_NEAR(f.pc(f.p[0]).produce.at(wood), 20, 1e-9);
  // Шахта: только с подходящим ресурсом; уровень увеличивает добычу.
  f.tx([&](Tx& tx) {
    mine = createBuilding(tx, 0, "Шахта");
    setBuildingRole(tx, mine, BuildingRole::Extract, true);
    setExtractGroups(tx, mine, {tx.w().catalogs->groupId(schema::grp::OreCommon)}, {});
    tx.building(mine).levels.push_back(BuildingLevel{});
    setLevelExtract(tx, mine, 1, 25);
    setLevelExtract(tx, mine, 2, 50);
    tx.province(f.p[0]).resource = wood;
  });
  auto opts = buildOptions(f.w(), f.p[0]);
  auto it = std::find_if(opts.begin(), opts.end(), [&](const BuildOption& o) { return o.building == mine; });
  CHECK(it != opts.end() && has(join(it->reasons, ";"), "не добывается"));
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).resource = test::idByName(tx.w().catalogs->resources, "Медь");
    placeBuilding(tx, f.p[0], mine, 1);
  });
  CHECK_NEAR(f.pc(f.p[0]).production, 12.5, 1e-9);
  f.tx([&](Tx& tx) { placeBuilding(tx, f.p[0], mine, 2); });
  CHECK_NEAR(f.pc(f.p[0]).production, 15, 1e-9);
  CHECK_NEAR(f.pc(f.p[0]).extractPct, 50, 1e-9);
  CHECK(extractAllowed(f.w(), *f.w().building(mine), test::idByName(f.w().catalogs->resources, "Медь")));
  CHECK(!extractAllowed(f.w(), *f.w().building(mine), wood));
}

TEST(rules_build_features_state_kind_only) {
  Fix f;
  ownedRace(f, 5000);
  Id mill = 0;
  f.tx([&](Tx& tx) {
    mill = createBuilding(tx, 0, "Мясопилка");
    setBuildingStateKinds(tx, mill, 1u << int(StateKind::Undead));
  });
  auto opts = buildOptions(f.w(), f.p[0]);
  auto it = std::find_if(opts.begin(), opts.end(), [&](const BuildOption& o) { return o.building == mill; });
  CHECK(it != opts.end() && has(join(it->reasons, ";"), "Только для вида государства"));
  f.tx([&](Tx& tx) { setStateKind(tx, f.A, StateKind::Undead); });
  opts = buildOptions(f.w(), f.p[0]);
  it = std::find_if(opts.begin(), opts.end(), [&](const BuildOption& o) { return o.building == mill; });
  CHECK(it != opts.end() && it->can);
}

// ---------------------------------------------------------------- рецепты
TEST(rules_build_features_recipes_and_essence_inputs) {
  Fix f;
  ownedRace(f, 5000);
  const Id iron = 5, coal = resNamed(f.w(), "Уголь"), steel = resNamed(f.w(), "Сталь"), copper = resNamed(f.w(), "Медь"),
           tin = resNamed(f.w(), "Олово"), bronze = resNamed(f.w(), "Бронза"), red = resNamed(f.w(), "Алое железо");
  const Id blood = essNamed(f.w(), "Эссенция крови");
  Id works = 0, sprinkler = 0;
  f.tx([&](Tx& tx) {
    works = createBuilding(tx, 0, "Сталелитейная");
    setBuildingRole(tx, works, BuildingRole::Convert, true);
    setRecipe(tx, works, 0, Recipe{"Сталь", {{iron, 1}, {coal, 1}}, {}, {steel, 1}, 1});
    setRecipe(tx, works, 1, Recipe{"Бронза", {{copper, 1}, {coal, 1}, {tin, 1}}, {}, {bronze, 1}, 1});
    placeBuilding(tx, f.p[0], works, 1);
    Faction& a = tx.faction(f.A);
    a.res[iron] = 5;
    a.res[coal] = 5;
    a.res[copper] = 5;
    a.res[tin] = 5;
  });
  CHECK_EQ(f.w().building(works)->recipes.size(), size_t(2));
  CHECK_EQ(recipeTitle(f.w(), f.w().building(works)->recipes[1]), std::string("Бронза"));
  // Выбран первый рецепт — сталь.
  CHECK_NEAR(f.fc(f.A).resources.at(steel).conversionOut, 1, 1e-12);
  f.tx([&](Tx& tx) { setPbRecipe(tx, f.p[0], works, 1); });
  CHECK_NEAR(f.fc(f.A).resources.at(bronze).conversionOut, 1, 1e-12);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(bronze), 1, 1e-12);
  CHECK_NEAR(f.w().faction(f.A)->stock(tin), 4, 1e-12);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setPbRecipe(tx, f.p[0], works, 5); }); }), "нет такого рецепта"));
  // Удаление выбранного рецепта — провинция переходит на первый.
  f.tx([&](Tx& tx) { removeRecipe(tx, works, 1); });
  CHECK_EQ(f.w().province(f.p[0])->buildings[0].recipe, 0);
  // Эссенция на входе: железо + эссенция крови → алое железо.
  f.tx([&](Tx& tx) {
    sprinkler = createBuilding(tx, 0, "Окропительница");
    setBuildingRole(tx, sprinkler, BuildingRole::Convert, true);
    setRecipe(tx, sprinkler, 0, Recipe{"", {{iron, 1}}, {{blood, 1}}, {red, 1}, 1});
    placeBuilding(tx, f.p[0], sprinkler, 1);
  });
  // Без эссенции крови — ждёт.
  CHECK_NEAR(f.fc(f.A).resources.at(red).conversionOut, 0, 1e-12);
  f.tx([&](Tx& tx) { tx.faction(f.A).ess[blood] = 3; });
  CHECK_NEAR(f.fc(f.A).resources.at(red).conversionOut, 1, 1e-12);
  CHECK_NEAR(f.fc(f.A).essences.at(blood).conversionIn, 1, 1e-12);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->essence(blood), 2, 1e-12);
  CHECK_NEAR(f.w().faction(f.A)->stock(red), 1, 1e-12);
  // Удалённая эссенция уходит из рецептов.
  f.tx([&](Tx& tx) { removeCatalogItem(tx, CatalogList::Essences, blood); });
  CHECK(f.w().building(sprinkler)->recipes[0].essIn.empty());
}

// ---------------------------------------------------------------- иссушение
TEST(rules_build_features_drain) {
  Fix f;
  const Id race = ownedRace(f, 250);
  const Id blood = essNamed(f.w(), "Эссенция крови");
  Id drainer = 0;
  f.tx([&](Tx& tx) {
    drainer = createBuilding(tx, 0, "Иссушительница");
    setBuildingRole(tx, drainer, BuildingRole::Drain, true);
    setLevelDrain(tx, drainer, 1, 100, blood, 50);
    placeBuilding(tx, f.p[0], drainer, 1);
  });
  // Без расы — простаивает.
  CHECK(f.fc(f.A).drains.empty());
  f.tx([&](Tx& tx) { setDrainRace(tx, f.p[0], drainer, race); });
  CHECK_EQ(f.fc(f.A).drains.size(), size_t(1));
  CHECK_NEAR(f.fc(f.A).essences.at(blood).generation, 50, 1e-12);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->essence(blood), 50, 1e-9);
  // Население уменьшилось на 100 (и затем выросло по приросту — его нет).
  CHECK_EQ(popOf(f.w(), f.p[0]), 150);
  // Простой — ничего не отнимает.
  f.tx([&](Tx& tx) { setConvertIdle(tx, f.p[0], drainer, true); });
  CHECK(f.fc(f.A).drains.empty());
  f.tx([&](Tx& tx) { setConvertIdle(tx, f.p[0], drainer, false); });
  // Меньше жителей, чем за ход, — эссенция пропорционально.
  f.tx([&](Tx& tx) { tx.province(f.p[0]).races[0].pop = 40; });
  CHECK_NEAR(f.fc(f.A).essences.at(blood).generation, 20, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(popOf(f.w(), f.p[0]), 0);
}

// ---------------------------------------------------------------- казармы
TEST(rules_build_features_upkeep_cut) {
  Fix f;
  ownedRace(f, 5000);
  Id barracks = 0;
  f.tx([&](Tx& tx) {
    addArmyRow(tx, f.A, UnitType::LightInf, "", 1000, 0.01);
    barracks = createBuilding(tx, 0, "Казармы");
    setBuildingRole(tx, barracks, BuildingRole::UpkeepCut, true);
    setLevelUpkeepCut(tx, barracks, 1, 1);
  });
  const double base = f.fc(f.A).expArmy;
  CHECK(base > 0);
  f.tx([&](Tx& tx) { placeBuilding(tx, f.p[0], barracks, 1); });
  CHECK_NEAR(f.fc(f.A).upkeepCut, 1, 1e-12);
  CHECK_NEAR(f.fc(f.A).expArmy, base * 0.99, 1e-9);
  // Не больше 50 % от всех казарм.
  f.tx([&](Tx& tx) { setLevelUpkeepCut(tx, barracks, 1, 30); });
  f.tx([&](Tx& tx) {
    tx.province(f.p[1]).owner = f.A;
    placeBuilding(tx, f.p[1], barracks, 1);
  });
  CHECK_NEAR(f.fc(f.A).upkeepCut, 50, 1e-12);
  CHECK_NEAR(f.fc(f.A).expArmy, base * 0.5, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setLevelUpkeepCut(tx, barracks, 1, 60); }); }), "от 0 до 50"));
}

// ---------------------------------------------------------------- чума и лечение
TEST(rules_build_features_plague_building_growth) {
  Fix f;
  ownedRace(f, 10000);
  Id lab = 0;
  f.tx([&](Tx& tx) {
    lab = simpleBuilding(tx, "Чумная лаборатория", f.p[0], BuildingFlag::Plague, true);
    infectPlague(tx, f.p[0]);
  });
  // Со «Зданием чумы» чума даёт прирост: население растёт.
  CHECK_NEAR(f.pc(f.p[0]).fx[Fx::PopGrowthPct], schema::kPlagueBuildingGrowth, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(popOf(f.w(), f.p[0]), 10250);
  // Модификатор в провинции показывается с действующим приростом.
  const Id plague = builtinModId(f.w(), schema::mod::Plague);
  const Modifier view = provinceModView(f.w(), f.p[0], plague);
  CHECK_NEAR(view.get(Fx::PopGrowthPct), schema::kPlagueBuildingGrowth, 1e-9);
  // Без здания — убыль.
  f.tx([&](Tx& tx) { demolish(tx, f.p[0], lab); });
  CHECK_NEAR(provinceModView(f.w(), f.p[0], plague).get(Fx::PopGrowthPct), -5, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(popOf(f.w(), f.p[0]) < 10250);
}

TEST(rules_build_features_heal_with_herbs) {
  Fix f;
  ownedRace(f, 1000);
  const Id herbs = healingHerbs(f.w());
  CHECK(herbs != 0);
  Id heal = 0;
  f.tx([&](Tx& tx) {
    heal = simpleBuilding(tx, "Лечебница", f.p[0], BuildingFlag::Healing, true);
    infectPlague(tx, f.p[0]);
    tx.faction(f.A).res[herbs] = 499;
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { healProvince(tx, f.p[0], heal, 0); }); }), "Лечебные травы"));
  f.tx([&](Tx& tx) { tx.faction(f.A).res[herbs] = 600; });
  f.tx([&](Tx& tx) { healProvince(tx, f.p[0], heal, 0); });
  CHECK(!provinceHas(f.w(), f.p[0], schema::mod::Plague));
  CHECK_NEAR(f.w().faction(f.A)->stock(herbs), 100, 1e-9);
}

// ---------------------------------------------------------------- правила игрока
TEST(rules_build_features_player_one_start_per_turn) {
  Fix f;
  ownedRace(f, 20000);
  Id a = 0, b = 0;
  f.tx([&](Tx& tx) {
    a = createBuilding(tx, 0, "Амбар");
    b = createBuilding(tx, 0, "Кузня");
  });
  f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], a, true); });
  CHECK_EQ(f.w().province(f.p[0])->buildTurn, f.w().turn());
  // Отменили — в этом ходу игрок не начнёт другую; администратор — может.
  f.tx([&](Tx& tx) { cancelBuilding(tx, f.p[0], a); });
  auto opts = buildOptions(f.w(), f.p[0], true);
  auto it = std::find_if(opts.begin(), opts.end(), [&](const BuildOption& o) { return o.building == b; });
  CHECK(it != opts.end() && has(join(it->reasons, ";"), "уже начато строительство"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], b, true); }); }), "уже начато"));
  f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], b); });
  f.tx([&](Tx& tx) { cancelBuilding(tx, f.p[0], b); });
  f.tx([&](Tx& tx) { endTurn(tx); });
  f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], b, true); });
  CHECK(f.w().province(f.p[0])->buildings.size() == 1);
}

TEST(rules_build_features_player_action_points) {
  Fix f;
  Id army = 0, row = 0;
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 8; i++) tx.province(f.p[i]).owner = f.A;
    row = addArmyRow(tx, f.A, UnitType::LightInf, "", 100, 0);
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, army, f.A, row, 10);
  });
  CHECK(hasAction(f.w(), army));
  CHECK(provincesAdjacent(f.w(), f.p[0], f.p[1]));
  CHECK(!provincesAdjacent(f.w(), f.p[0], f.p[2]));
  // Внутри своей провинции — без очка действий; в соседнюю — тратит очко; дальше — нельзя.
  Vec2 inside = center(0) + Vec2{30, 30};
  CHECK(canPlayerMove(f.w(), army, inside));
  CHECK(!moveSpendsAction(f.w(), army, center(0), inside));
  std::string why;
  CHECK(!canPlayerMove(f.w(), army, center(2), &why) && has(why, "соседнюю"));
  CHECK(canPlayerMove(f.w(), army, center(1)));
  CHECK(moveSpendsAction(f.w(), army, center(0), center(1)));
  f.tx([&](Tx& tx) {
    moveArmy(tx, army, center(1));
    spendAction(tx, army);
  });
  CHECK(!hasAction(f.w(), army));
  CHECK(!canPlayerMove(f.w(), army, center(2), &why) && has(why, "очки действий"));
  CHECK(canPlayerMove(f.w(), army, center(1) + Vec2{20, 0}));
  // Следующий ход — очко снова есть.
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(hasAction(f.w(), army));
  CHECK(canPlayerMove(f.w(), army, center(2)));
}

// ---------------------------------------------------------------- колонизация
TEST(rules_build_features_settlers_available) {
  Fix f;
  ownedRace(f, 3000);
  CHECK_EQ(settlersAvailable(f.w(), f.A), i64(3000));
  CHECK_EQ(settlersAvailable(f.w(), f.A, f.p[0]), i64(0));
}

// ---------------------------------------------------------------- базовые записи версии 3
// Мир версии 2 со своей постройкой «Рынок» без ключа: дополнение версии 3 не задваивает её, а делает базовым рынком
// (ключ, возможность «рынок», уровни по ТЗ).
TEST(rules_build_features_content_v3_adopts_own_building) {
  World w = newWorld("Свой рынок");
  {
    Tx tx(w);
    const Id m = tx.w().buildings.firstId([](const Building& b) { return b.key == schema::bld::Market; });
    Building& b = tx.building(m);
    b.key.clear();
    b.market = false;
    b.levels = {BuildingLevel{}};
    tx.meta().content = 2;
    w = std::move(tx).finish();
  }
  io::Warnings warns;
  io::normalize(w, warns);
  CHECK_EQ(w.meta->content, content::kVersion);
  int markets = 0;
  const Building* market = nullptr;
  w.buildings.each([&](const Building& b) {
    if (b.owner == 0 && b.name == "Рынок") {
      markets++;
      market = &b;
    }
  });
  CHECK_EQ(markets, 1);
  CHECK(market && market->key == schema::bld::Market && market->market && market->levels.size() == 5);
}

TEST(rules_build_features_content_v3) {
  World w = newWorld("Содержимое 3");
  CHECK_EQ(w.meta->content, content::kVersion);
  CHECK(w.catalogs->races.size() >= 51);
  CHECK(w.catalogs->cultures.size() >= 55);
  // Расы и культуры — по алфавиту.
  for (size_t i = 1; i < w.catalogs->races.size(); i++) CHECK(compareRu(w.catalogs->races[i - 1].name, w.catalogs->races[i].name) <= 0);
  for (size_t i = 1; i < w.catalogs->cultures.size(); i++)
    CHECK(compareRu(w.catalogs->cultures[i - 1].name, w.catalogs->cultures[i].name) <= 0);
  CHECK(test::idByName(w.catalogs->races, "Сянмаорены") && test::idByName(w.catalogs->cultures, "Сянмао-философия"));
  // Ресурсы внутри групп — по алфавиту, золото — первым.
  CHECK_EQ(w.catalogs->resources.front().id, kGold);
  CHECK(test::idByName(w.catalogs->resources, "Сталь обсидиана бездны") && test::idByName(w.catalogs->resources, "Зараженный бездной обсидиан"));
  // Реликвии: 50 археологических находок и 50 общих, по 10 каждой редкости.
  const Id finds = w.catalogs->relicGroupId(schema::kRelicArchFinds);
  int arch = 0, general = 0;
  int byRarity[int(Rarity::Count)] = {};
  for (const Relic& r : w.catalogs->relics) {
    (w.catalogs->inRelicGroup(r.group, finds) ? arch : general)++;
    byRarity[int(r.rarity)]++;
  }
  CHECK_EQ(arch, 50);
  CHECK_EQ(general, 50);
  for (int k = 0; k < int(Rarity::Count); k++) CHECK_EQ(byRarity[k], 20);
  // Постройки общего дерева.
  auto bld = [&](const char* key) -> const Building* {
    const Building* out = nullptr;
    w.buildings.each([&](const Building& b) {
      if (b.owner == 0 && b.key == key) out = &b;
    });
    return out;
  };
  const Building* market = bld(schema::bld::Market);
  CHECK(market && market->market && market->levels.size() == 5 && market->levels[4].produce.at(kGold) == 30);
  CHECK(market && market->levels[2].cost.at(test::idByName(w.catalogs->resources, "Бронза")) == 50);
  const Building* mine = bld(schema::bld::Mine);
  CHECK(mine && mine->extract && mine->levels.size() == 5 && mine->levels[4].extractPct == 200 && mine->extractExclude.size() == 1);
  CHECK(bld(schema::bld::Infirmary) && bld(schema::bld::Infirmary)->healing && bld(schema::bld::Infirmary)->cat == BuildingCat::Infrastructure);
  const Building* lab = bld(schema::bld::PlagueLab);
  CHECK(lab && lab->plague && lab->essenceGen && lab->levels[0].essence.size() == 1 && lab->levels[0].essCost.size() == 1);
  CHECK(bld(schema::bld::RelicVault) && bld(schema::bld::RelicVault)->relicStore);
  CHECK(bld(schema::bld::Barracks) && bld(schema::bld::Barracks)->upkeepCut && bld(schema::bld::Barracks)->levels[0].upkeepCut == 1);
  CHECK(bld(schema::bld::Sawmill) && bld(schema::bld::Sawmill)->levels[0].produceOnRes.size() == 1);
  CHECK(bld(schema::bld::CorpseMill) && bld(schema::bld::CorpseMill)->stateKinds == (1u << int(StateKind::Undead)));
  CHECK(bld(schema::bld::Drainer) && bld(schema::bld::Drainer)->drain && bld(schema::bld::Drainer)->levels[0].drainPop == 100);
  CHECK(bld(schema::bld::Sprinkler) && bld(schema::bld::Sprinkler)->recipes.size() == 1 && bld(schema::bld::Sprinkler)->recipes[0].essIn.size() == 1);
  CHECK(bld(schema::bld::Steelworks) && bld(schema::bld::Steelworks)->recipes.size() == 9);
  const Building* conv = bld(schema::bld::AlchemyConverter);
  CHECK(conv && conv->techs.size() == 1 && w.tech(conv->techs[0]) && w.tech(conv->techs[0])->key == schema::tech::MagicPowder);
  // «Цитадель Бездны»: цена — сталь обсидиана бездны и эссенция бездны, без генерации эссенции.
  bool citadel = false;
  w.buildings.each([&](const Building& b) {
    if (b.name != "Цитадель Бездны") return;
    citadel = true;
    CHECK(!b.essenceGen);
    CHECK_EQ(b.levels[0].cost.at(test::idByName(w.catalogs->resources, "Сталь обсидиана бездны")), 5000.0);
    CHECK_EQ(b.levels[0].essCost.at(test::idByName(w.catalogs->essences, "Эссенция бездны")), 5000.0);
  });
  CHECK(citadel);
}

// Тип города упал вместе с населением: постройки остаются (и в конце хода), новые — только в свободные слоты.
TEST(rules_build_features_population_drop_keeps_buildings) {
  Fix f;
  ownedRace(f, 12000);   // небольшой город: средняя провинция 2 + 2 = 4 слота
  Id extra = 0;
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 4; i++) placeBuilding(tx, f.p[0], createBuilding(tx, 0, "Постройка " + std::to_string(i)), 1);
    extra = createBuilding(tx, 0, "Лишняя");
  });
  const World before = f.w();
  f.tx([&](Tx& tx) { tx.province(f.p[0]).races[0].pop = 500; });   // аванпост: 2 слота
  CHECK(slotLosses(before, f.w()).empty());
  CHECK_EQ(f.pc(f.p[0]).slots, 2);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(f.w().province(f.p[0])->buildings.size(), size_t(4));
  auto opts = buildOptions(f.w(), f.p[0]);
  auto it = std::find_if(opts.begin(), opts.end(), [&](const BuildOption& o) { return o.building == extra; });
  CHECK(it != opts.end() && has(join(it->reasons, ";"), "Нет свободных слотов"));
}
