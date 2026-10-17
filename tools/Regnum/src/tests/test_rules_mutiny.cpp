// Regnum — правила верности и мятежей: изменение верности войска, мятеж войска (частичный, полный при −100 %, все
// войска провинции), переход мятежников перед боем, мятеж гарнизона, восстание провинции.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

// ---------------------------------------------------------------- верность
TEST(rules_mutiny_loyalty_delta) {
  Fix f;
  Id army = 0, hero = 0;
  f.tx([&](Tx& tx) {
    setFx(tx, schema::mod::Decentralization, {{Fx::LoyaltyPerTurn, -5}});
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
  });
  CHECK_NEAR(loyaltyDelta(f.w(), army), -5, 1e-9);
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Army, army, ensureBuiltinMod(tx, schema::mod::Patriotism)); });
  CHECK_NEAR(loyaltyDelta(f.w(), army), 0, 1e-9);
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Лоялист");
    addModifier(tx, ModTarget::Character, hero, ensureBuiltinMod(tx, schema::mod::Loyalist));
    Id row = addArmyRow(tx, f.A, UnitType::LightInf, "", 10, 0);
    setUnits(tx, army, f.A, row, 10);
    setHero(tx, army, hero, true);
    dropModifier(tx, ModTarget::Army, army, builtinModId(tx.w(), schema::mod::Patriotism));
  });
  CHECK_NEAR(loyaltyDelta(f.w(), army), 5, 1e-9);   // не уменьшается и +5 за лоялиста
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setArmyLoyalty(tx, army, 50); }); }), "не уменьшается"));
  // «Изуверское наслаждение» — только армиям демонов и безжалостным.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Army, army, ensureBuiltinMod(tx, schema::mod::Sadism)); }); }),
            "Армия демонов"));
}

// ---------------------------------------------------------------- мятеж войска
TEST(rules_mutiny_partial) {
  Fix f;
  Id inf = 0, mech = 0, army = 0, hero = 0, loyal = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    inf = addArmyRow(tx, f.A, UnitType::LightInf, "Пехота", 100, 1);
    mech = addArmyRow(tx, f.A, UnitType::Machines, "Баллисты", 10, 1);
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, army, f.A, inf, 100);
    setUnits(tx, army, f.A, mech, 10);
    hero = createCharacter(tx, f.A, "Смутьян");
    loyal = createCharacter(tx, f.A, "Верный");
    addModifier(tx, ModTarget::Character, hero, ensureBuiltinMod(tx, schema::mod::Discontent));
    setHero(tx, army, hero, true);
    setHero(tx, army, loyal, true);
    tx.army(army).loyalty = -30;
  });
  MutinyResult m;
  f.tx([&](Tx& tx) { m = mutiny(tx, army); });
  CHECK(m.rebelState != 0 && m.rebelArmy != 0);
  CHECK_EQ(m.loyalArmy, army);
  CHECK_EQ(f.w().faction(m.rebelState)->name, std::string("Мятеж (Арден)"));
  CHECK(f.w().relation(m.rebelState, f.A).s == RelStatus::War);
  CHECK_EQ(unitsIn(f.w(), m.rebelArmy), 30);           // 30 % пехоты; механизмы верны
  CHECK_EQ(unitsIn(f.w(), army), 80);
  CHECK_NEAR(f.w().army(army)->loyalty, 0, 1e-9);
  CHECK_EQ(f.w().faction(f.A)->armyRow(inf)->total, 70);
  CHECK_EQ(f.w().character(hero)->faction, m.rebelState);
  CHECK(has(f.w().army(m.rebelArmy)->groups[0].heroes, hero));
  CHECK(has(f.w().army(army)->groups[0].heroes, loyal));
  // Повторный мятеж — то же мятежное государство.
  f.tx([&](Tx& tx) { tx.army(army).loyalty = -10; });
  MutinyResult m2;
  f.tx([&](Tx& tx) { m2 = mutiny(tx, army); });
  CHECK_EQ(m2.rebelState, m.rebelState);
}

TEST(rules_mutiny_full_at_minus_100_and_cleanup) {
  Fix f;
  Id inf = 0, army = 0, loyal = 0;
  f.tx([&](Tx& tx) {
    inf = addArmyRow(tx, f.A, UnitType::LightInf, "", 50, 1);
    army = createArmy(tx, ArmyKind::Army, f.A, center(5));
    setUnits(tx, army, f.A, inf, 50);
    loyal = createCharacter(tx, f.A, "Верный");
    setHero(tx, army, loyal, true);
    tx.army(army).loyalty = -96;
    tx.army(army).modifiers = {ensureBuiltinMod(tx, schema::mod::Conscience)};   // −5 за ход
  });
  TurnReport rep;
  f.tx([&](Tx& tx) { rep = endTurn(tx); });
  CHECK_EQ(rep.events.size(), size_t(1));
  CHECK(rep.events[0].kind == TurnEvent::Mutiny);
  CHECK(!f.w().army(army));                                // войско восстало целиком
  CHECK(has(rep.events[0].heroes, loyal));
  Id rebels = rep.events[0].army;
  CHECK_EQ(unitsIn(f.w(), rebels), 50);
  // Мятежники без войск и провинций упраздняются в конце хода.
  f.tx([&](Tx& tx) { disband(tx, rebels); });
  Id rs = rep.events[0].rebelState;
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(!f.w().faction(rs));
}

TEST(rules_mutiny_all_armies_in_province_merge) {
  Fix f;
  Id inf = 0, a1 = 0, a2 = 0, far = 0;
  f.tx([&](Tx& tx) {
    inf = addArmyRow(tx, f.A, UnitType::LightInf, "", 300, 1);
    a1 = createArmy(tx, ArmyKind::Army, f.A, center(0) + Vec2{-40, 0});
    a2 = createArmy(tx, ArmyKind::Army, f.A, center(0) + Vec2{40, 0});
    far = createArmy(tx, ArmyKind::Army, f.A, center(3));
    setUnits(tx, a1, f.A, inf, 100);
    setUnits(tx, a2, f.A, inf, 50);
    setUnits(tx, far, f.A, inf, 10);
    tx.army(a1).loyalty = -50;
    tx.army(a2).loyalty = -20;
    tx.army(far).loyalty = -90;
  });
  MutinyResult m;
  f.tx([&](Tx& tx) { m = mutiny(tx, a2); });
  CHECK_EQ(m.loyalArmy, a2);                 // верные — в войско, у которого нажали «Мятеж»
  CHECK(!f.w().army(a1));
  CHECK_EQ(unitsIn(f.w(), a2), 50 + 40);
  CHECK_EQ(unitsIn(f.w(), m.rebelArmy), 50 + 10);
  CHECK_NEAR(f.w().army(a2)->loyalty, 0, 1e-9);
  CHECK_EQ(unitsIn(f.w(), far), 10);         // войско в другой провинции не затронуто
  CHECK_NEAR(f.w().army(far)->loyalty, -90, 1e-9);
}

TEST(rules_mutiny_defect_before_battle) {
  Fix f;
  Id inf = 0, target = 0, rebels = 0;
  f.tx([&](Tx& tx) {
    inf = addArmyRow(tx, f.A, UnitType::LightInf, "", 300, 1);
    target = createArmy(tx, ArmyKind::Army, f.A, center(1));
    setUnits(tx, target, f.A, inf, 100);
    Id src = createArmy(tx, ArmyKind::Army, f.A, center(5));
    setUnits(tx, src, f.A, inf, 100);
    tx.army(src).loyalty = -100;
    rebels = mutiny(tx, src).rebelArmy;
    tx.army(target).loyalty = -50;
  });
  CHECK(willDefect(f.w(), rebels, target));
  f.tx([&](Tx& tx) { defect(tx, rebels, target); });
  CHECK_EQ(unitsIn(f.w(), target), 50);
  CHECK_EQ(unitsIn(f.w(), rebels), 150);
  CHECK_NEAR(f.w().army(target)->loyalty, 0, 1e-9);
}

// ---------------------------------------------------------------- гарнизон и провинция
TEST(rules_mutiny_garrison) {
  Fix f;
  Id race = 0, row = 0, undeadRow = 0, rebel = 0, loyal = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    row = addArmyRow(tx, f.A, UnitType::LightInf, "Стража", 100, 0);
    undeadRow = addArmyRow(tx, f.A, UnitType::HeavyInf, "Скелеты", 10, 0);
    setRowRace(tx, f.A, undeadRow, schema::kRaceUndead);
    setGarrison(tx, f.p[0], row, 80);
    setGarrison(tx, f.p[0], undeadRow, 10);
    rebel = createCharacter(tx, f.A, "Заговорщик");
    loyal = createCharacter(tx, f.A, "Верный");
    addModifier(tx, ModTarget::Character, rebel, ensureBuiltinMod(tx, schema::mod::Discontent));
    setGarrisonHero(tx, f.p[0], rebel, true);
    setGarrisonHero(tx, f.p[0], loyal, true);
  });
  CHECK(!canGarrisonMutiny(f.w(), f.p[0]));
  f.tx([&](Tx& tx) { setGarrisonLoyalty(tx, f.p[0], -25); });
  CHECK(canGarrisonMutiny(f.w(), f.p[0]));
  MutinyResult m;
  f.tx([&](Tx& tx) { m = garrisonMutiny(tx, f.p[0]); });
  CHECK(m.rebelState && m.rebelArmy);
  const Province& p = *f.w().province(f.p[0]);
  i64 left = 0, skeletons = 0;
  for (const GarrisonEntry& g : p.garrison) (g.row == undeadRow ? skeletons : left) += g.count;
  CHECK_EQ(left, i64(60));       // 25 % из 80 ушли к мятежникам
  CHECK_EQ(skeletons, i64(10));  // нежить верна
  CHECK_NEAR(p.garrisonLoyalty, 0, 1e-12);
  CHECK((p.garrisonHeroes == std::vector<Id>{loyal}));
  CHECK_EQ(f.w().character(rebel)->faction, m.rebelState);
  i64 rebels = 0;
  for (const ArmyUnit& u : f.w().army(m.rebelArmy)->groups[0].units) rebels += u.count;
  CHECK_EQ(rebels, i64(20));
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->total, i64(80));
  // −100 % в конце хода — гарнизон восстаёт целиком (событие хода).
  f.tx([&](Tx& tx) { setGarrisonLoyalty(tx, f.p[0], -100); });
  TurnReport rep;
  f.tx([&](Tx& tx) { rep = endTurn(tx); });
  CHECK(std::any_of(rep.events.begin(), rep.events.end(), [&](const TurnEvent& e) { return e.kind == TurnEvent::Mutiny && e.province == f.p[0]; }));
  i64 rest = 0;
  for (const GarrisonEntry& g : f.w().province(f.p[0])->garrison) rest += g.row == row ? g.count : 0;
  CHECK_EQ(rest, i64(0));
}

TEST(rules_mutiny_province_uprising) {
  Fix f;
  Id race = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[5], f.A, race, 1000);
    setSlaves(tx, f.A, race, 50, -20);
    setSlaveWork(tx, f.p[5], race, 50);
  });
  Id army = 0;
  f.tx([&](Tx& tx) { army = provinceUprising(tx, f.p[5]); });
  CHECK(army != 0);
  CHECK_EQ(popOf(f.w(), f.p[5]), 900);
  CHECK_EQ(unitsIn(f.w(), army), 150);   // 100 крестьян + 50 восставших рабов
  CHECK_EQ(f.w().army(army)->name, std::string("Мятежные крестьяне"));
  CHECK(f.w().province(f.p[5])->slaves.empty());
  CHECK_EQ(f.w().faction(f.A)->slaves[0].count, 0);
  const Faction& r = *f.w().faction(f.w().army(army)->leader());
  CHECK_EQ(r.rebelOf, f.A);
  for (const ArmyRow& row : r.army) CHECK_EQ(row.race, std::string(schema::kRaceLiving));
}
