// Regnum — правила найма: формирование и резерв, нежить за трупы и корабли у верфи, ключевой ресурс и цены найма
// (кавалерия, звери, механизмы), элементали за эссенции, особые отряды и постройки доступа, наёмники.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

// ---------------------------------------------------------------- формирование и резерв
TEST(rules_recruit_disband) {
  Fix f;
  Id race = 0, row = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.A, race, 1000);
    row = addArmyRow(tx, f.A, UnitType::LightInf, "Копейщики", 0, 1);
  });
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->race, std::string(schema::kRaceLiving));
  f.tx([&](Tx& tx) { recruit(tx, f.A, row, 100); });
  CHECK_EQ(popOf(f.w(), f.p[0]) + popOf(f.w(), f.p[1]), 1900);
  CHECK_EQ(popOf(f.w(), f.p[0]), 950);
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->total, 0);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(f.w().faction(f.A)->forming[0].left, 1);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(f.w().faction(f.A)->forming.empty());
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->total, 100);
  f.tx([&](Tx& tx) { disbandReserve(tx, f.A, row, 40); });
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->total, 60);
  CHECK_EQ(popOf(f.w(), f.p[0]) + popOf(f.w(), f.p[1]), 1940);
  // Отмена формирования возвращает людей.
  f.tx([&](Tx& tx) { recruit(tx, f.A, row, 10); });
  f.tx([&](Tx& tx) { cancelFormation(tx, f.A, 0); });
  CHECK_EQ(popOf(f.w(), f.p[0]) + popOf(f.w(), f.p[1]), 1940);
  // Удаление строки — воины в население.
  f.tx([&](Tx& tx) { removeRow(tx, f.A, row); });
  CHECK_EQ(popOf(f.w(), f.p[0]) + popOf(f.w(), f.p[1]), 2000);
}

TEST(rules_recruit_undead_and_ships) {
  Fix f;
  Id row = 0, frig = 0;
  f.tx([&](Tx& tx) {
    setStateKind(tx, f.A, StateKind::Undead);
    row = addArmyRow(tx, f.A, UnitType::HeavyInf, "Скелеты", 0, 0);
    frig = addFleetRow(tx, f.A, ShipType::Frigate, "", 0, 1);
    Constant& cost = ensureConstant(tx, schema::cst::FrigateCost);
    cost.res = {{kGold, 100}};
    cost.minRes.clear();
    tx.faction(f.A).res[kGold] = 250;
  });
  // Без верфи корабли не строятся (ТЗ «Доработки №3», п.2).
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { recruit(tx, f.A, frig, 1); }); }), "верфь"));
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    const Id yard = createBuilding(tx, 0, "Верфь");
    setBuildingFlag(tx, yard, BuildingFlag::Shipyard, true);
    setLevelShips(tx, yard, 1, 1u << int(ShipType::Frigate));
    placeBuilding(tx, f.p[0], yard, 1);
  });
  CHECK_EQ(unitRace(f.w(), f.A, *f.w().faction(f.A)->armyRow(row)), std::string(schema::kRaceUndead));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { recruit(tx, f.A, row, 10); }); }), "Трупы"));
  f.tx([&](Tx& tx) { tx.faction(f.A).res[ensureResource(tx, schema::kResCorpses)] += 15; });
  f.tx([&](Tx& tx) { recruit(tx, f.A, row, 10); });
  CHECK_NEAR(f.w().faction(f.A)->stock(resourceId(f.w(), schema::kResCorpses)), 5, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { recruit(tx, f.A, frig, 3); }); }), "Недостаточно"));
  f.tx([&](Tx& tx) { recruit(tx, f.A, frig, 2); });
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 50, 1e-9);
}

// ---------------------------------------------------------------- ключевой ресурс и найм
TEST(rules_recruit_key_resources) {
  Fix f;
  const World& w = f.w();
  const auto& res = w.catalogs->resources;
  const Id horse = test::idByName(res, "Лошади"), griffin = test::idByName(res, "Грифоны"), elephant = test::idByName(res, "Слоны");
  const Id hydra = test::idByName(res, "Гидры");
  const Id parts = w.catalogs->resourceId(schema::kResMechParts);
  CHECK(keyAllowed(w, UnitType::LightCav, horse));
  CHECK(!keyAllowed(w, UnitType::HeavyCav, griffin));
  CHECK(keyAllowed(w, UnitType::AirCav, griffin));
  CHECK(!keyAllowed(w, UnitType::AirCav, horse));
  CHECK(keyAllowed(w, UnitType::Beasts, elephant));
  CHECK(keyAllowed(w, UnitType::Beasts, horse));      // звери — вся группа «Звери», кроме чудовищ
  CHECK(!keyAllowed(w, UnitType::Beasts, hydra));
  CHECK(keyAllowed(w, UnitType::Monsters, hydra));
  CHECK(!keyAllowed(w, UnitType::Monsters, elephant));
  CHECK(keyAllowed(w, UnitType::Machines, parts));
  CHECK(!keyAllowed(w, UnitType::Machines, test::idByName(w.catalogs->resources, "Сталь")));
  CHECK_EQ(keyResources(w, UnitType::LightCav).size(), size_t(23));
  CHECK_EQ(keyResources(w, UnitType::Beasts).size(), size_t(51));   // без чудовищ и морских чудовищ
  CHECK(keyResources(w, UnitType::LightInf).empty());
  CHECK(!schema::needsKeyResource(UnitType::Flying));
}

TEST(rules_recruit_cavalry_beasts_machines) {
  Fix f;
  Id race = 0, cav = 0, beasts = 0, mech = 0;
  const Id horse = test::idByName(f.w().catalogs->resources, "Лошади"), elephant = test::idByName(f.w().catalogs->resources, "Слоны");
  const Id parts = f.w().catalogs->resourceId(schema::kResMechParts);
  const Id leather = test::idByName(f.w().catalogs->resources, "Кожа"), fire = test::idByName(f.w().catalogs->essences, "Эссенция пламени");
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    cav = addArmyRow(tx, f.A, UnitType::HeavyCav, "Рыцари", 0, 1);
    beasts = addArmyRow(tx, f.A, UnitType::Beasts, "Боевые слоны", 0, 1);
    mech = addArmyRow(tx, f.A, UnitType::Machines, "Катапульты", 0, 1);
  });
  // Кавалерия: ключевой ресурс обязателен.
  CHECK(has(recruitCost(f.w(), f.A, cav, 10).problems.front(), "Не указан ключевой ресурс"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRowKey(tx, f.A, cav, elephant); }); }), "не подходит"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRowKey(tx, f.A, cav, horse, 2); }); }), "ровно 1"));
  f.tx([&](Tx& tx) {
    setRowKey(tx, f.A, cav, horse);
    setRowExtra(tx, f.A, cav, leather, 2);
    setRowEssence(tx, f.A, cav, fire, 0.5);
    setRowKey(tx, f.A, beasts, elephant);
    setRowKey(tx, f.A, mech, parts, 3);   // механизмы — не меньше 1, можно больше
    tx.faction(f.A).res[horse] = 10;
    tx.faction(f.A).res[leather] = 15;
    tx.faction(f.A).res[elephant] = 4;
    tx.faction(f.A).res[parts] = 7;
    tx.faction(f.A).ess[fire] = 5;
  });
  RecruitCost c = recruitCost(f.w(), f.A, cav, 10);
  CHECK_EQ(c.people, i64(10));
  CHECK_NEAR(c.res.at(horse), 10, 1e-12);
  CHECK_NEAR(c.res.at(leather), 20, 1e-12);
  CHECK_NEAR(c.ess.at(fire), 5, 1e-12);
  CHECK(has(c.problems.front(), "Недостаточно ресурса «Кожа»"));
  f.tx([&](Tx& tx) { tx.faction(f.A).res[leather] = 20; });
  f.tx([&](Tx& tx) { recruit(tx, f.A, cav, 10); });
  CHECK_EQ(popOf(f.w(), f.p[0]), i64(990));
  CHECK_NEAR(f.w().faction(f.A)->stock(horse), 0, 1e-12);
  CHECK_NEAR(f.w().faction(f.A)->essence(fire), 0, 1e-12);
  // Отмена формирования — полный возврат, в том числе эссенций.
  f.tx([&](Tx& tx) { cancelFormation(tx, f.A, 0); });
  CHECK_EQ(popOf(f.w(), f.p[0]), i64(1000));
  CHECK_NEAR(f.w().faction(f.A)->stock(horse), 10, 1e-12);
  CHECK_NEAR(f.w().faction(f.A)->essence(fire), 5, 1e-12);
  // Звери и механизмы — без населения.
  c = recruitCost(f.w(), f.A, beasts, 4);
  CHECK_EQ(c.people, i64(0));
  CHECK(c.problems.empty());
  c = recruitCost(f.w(), f.A, mech, 2);
  CHECK_EQ(c.people, i64(0));
  CHECK_NEAR(c.res.at(parts), 6, 1e-12);
  f.tx([&](Tx& tx) {
    recruit(tx, f.A, beasts, 4);
    recruit(tx, f.A, mech, 2);
  });
  CHECK_EQ(popOf(f.w(), f.p[0]), i64(1000));
  // Роспуск резерва зверей не возвращает людей.
  f.tx([&](Tx& tx) { endTurn(tx); });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(f.w().faction(f.A)->armyRow(beasts)->total, i64(4));
  f.tx([&](Tx& tx) { disbandReserve(tx, f.A, beasts, 4); });
  CHECK_EQ(popOf(f.w(), f.p[0]), i64(1000));
  // Смена типа снимает неподходящий ключевой ресурс.
  f.tx([&](Tx& tx) { setRowType(tx, f.A, cav, UnitType::AirCav); });
  CHECK_EQ(f.w().faction(f.A)->armyRow(cav)->keyRes, Id(0));
  CHECK_EQ(f.w().faction(f.A)->armyRow(cav)->name, std::string("Рыцари"));
}

TEST(rules_recruit_elementals) {
  Fix f;
  Id row = 0, race = 0;
  const Id water = test::idByName(f.w().catalogs->essences, "Эссенция воды"), ice = test::idByName(f.w().catalogs->essences, "Эссенция льда");
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 500);
    row = addArmyRow(tx, f.A, UnitType::Elementals, "", 0, 7);
  });
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->race, std::string(schema::kRaceElemental));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRowRace(tx, f.A, row, schema::kRaceLiving); }); }), "только «Элементали»"));
  const Id steel = test::idByName(f.w().catalogs->resources, "Сталь");
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRowExtra(tx, f.A, row, steel, 1); }); }), "только за эссенции"));
  f.tx([&](Tx& tx) {
    setRowEssence(tx, f.A, row, water, 10);
    setRowEssUpkeep(tx, f.A, row, ice, 0.5);
    tx.faction(f.A).ess[water] = 100;
  });
  RecruitCost c = recruitCost(f.w(), f.A, row, 10);
  CHECK_EQ(c.people, i64(0));
  CHECK(c.res.empty());
  CHECK_NEAR(c.ess.at(water), 100, 1e-12);
  f.tx([&](Tx& tx) { setRowTotal(tx, f.A, row, 20); });   // правка резерва — без затрат
  CHECK_NEAR(f.w().faction(f.A)->essence(water), 100, 1e-12);
  // Содержание — эссенцией, а не золотом.
  CHECK_NEAR(f.fc(f.A).expArmy, 0, 1e-12);
  CHECK_NEAR(f.fc(f.A).essences.at(ice).upkeep, 10, 1e-12);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->essence(ice), -10, 1e-12);   // долг содержания
  CHECK(has(lastLog(f.w(), "эссенций"), "Эссенция льда"));
  // Другой тип: содержание эссенциями снимается, раса — по умолчанию.
  f.tx([&](Tx& tx) { setRowType(tx, f.A, row, UnitType::LightInf); });
  CHECK(f.w().faction(f.A)->armyRow(row)->essUpkeep.empty());
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->race, std::string(schema::kRaceLiving));
}

// ---------------------------------------------------------------- особые отряды и наёмники
TEST(rules_recruit_special_units) {
  Fix f;
  const World& w0 = f.w();
  const Id dragons = test::idByName(w0.catalogs->resources, "Черные Драконы"), steel = test::idByName(w0.catalogs->resources, "Обсидиановая сталь");
  const Id abyss = test::idByName(w0.catalogs->essences, "Эссенция бездны");
  const Id special = w0.catalogs->specials[0].id;
  Id citadel = 0, row = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    citadel = createBuilding(tx, 0, "Цитадель Бездны");
    setBuildingRole(tx, citadel, BuildingRole::Special, true);
    setBuildingSpecial(tx, citadel, special, true);
  });
  CHECK(specialAccess(f.w(), f.A).empty());
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addSpecialRow(tx, f.A, special); }); }), "«Цитадель Бездны»"));
  f.tx([&](Tx& tx) { placeBuilding(tx, f.p[0], citadel, 1); });
  CHECK(hasSpecialAccess(f.w(), f.A, special));
  f.tx([&](Tx& tx) { row = addSpecialRow(tx, f.A, special); });
  const ArmyRow& r = *f.w().faction(f.A)->armyRow(row);
  CHECK(r.type == UnitType::Monsters);
  CHECK_EQ(r.special, special);
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[dragons] = 2;
    tx.faction(f.A).res[steel] = 200;
    tx.faction(f.A).ess[abyss] = 2000;
  });
  RecruitCost c = recruitCost(f.w(), f.A, row, 2);
  CHECK(c.problems.empty());
  CHECK_EQ(c.people, i64(0));
  CHECK_NEAR(c.res.at(dragons), 2, 1e-12);
  CHECK_NEAR(c.res.at(steel), 200, 1e-12);
  CHECK_NEAR(c.ess.at(abyss), 2000, 1e-12);
  // Строки особого отряда правятся только в справочнике.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRowType(tx, f.A, row, UnitType::Beasts); }); }), "Особые отряды"));
  f.tx([&](Tx& tx) {
    SpecialUnit s = *tx.w().special(special);
    s.upkeep = 3;
    s.extra[steel] = 50;
    setSpecial(tx, s);
  });
  CHECK_NEAR(f.w().faction(f.A)->armyRow(row)->upkeep, 3, 1e-12);
  CHECK_NEAR(f.w().faction(f.A)->armyRow(row)->extra.at(steel), 50, 1e-12);
  // Без постройки найм недоступен, отряды остаются.
  f.tx([&](Tx& tx) { demolish(tx, f.p[0], citadel); });
  CHECK(has(recruitCost(f.w(), f.A, row, 1).problems.front(), "постройка доступа"));
  f.tx([&](Tx& tx) { removeSpecial(tx, special); });
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->special, Id(0));
  CHECK(f.w().building(citadel)->specials.empty());
}

TEST(rules_recruit_mercenaries) {
  Fix f;
  Id guild = 0, row = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    tx.faction(f.A).res[kGold] = 100;
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addMercRow(tx, f.A, UnitType::HeavyInf); }); }), "Гильдия Наемников"));
  f.tx([&](Tx& tx) { guild = simpleBuilding(tx, "Гильдия Наемников", f.p[0], BuildingFlag::Mercenary, true); });
  CHECK_EQ(mercLimit(f.w(), f.A), i64(2500));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addMercRow(tx, f.A, UnitType::Beasts); }); }), "пехота"));
  f.tx([&](Tx& tx) { row = addMercRow(tx, f.A, UnitType::HeavyInf, "Вольные клинки", 0.01); });
  const ArmyRow* r = f.w().faction(f.A)->armyRow(row);
  CHECK(r && r->merc && r->race == schema::kRaceMercenary);
  CHECK_NEAR(r->upkeep, schema::kNewRowUpkeep, 1e-12);   // новая строка — 0,001 золота за юнит
  const i64 popBefore = statePopulation(f.w(), f.A);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { recruit(tx, f.A, row, 3000); }); }), "Лимит наёмников"));
  f.tx([&](Tx& tx) { recruit(tx, f.A, row, 2000); });
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 80, 1e-9);   // только золото
  CHECK_EQ(statePopulation(f.w(), f.A), popBefore);      // без населения
  CHECK_EQ(mercCount(f.w(), f.A), i64(2000));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRowRace(tx, f.A, row, schema::kRaceLiving); }); }), "Наемники"));
}
