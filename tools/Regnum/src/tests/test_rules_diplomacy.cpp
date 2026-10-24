// Regnum — правила дипломатии: отношения и хроника, бонус одной религии, перемирие и запрет войны, вассалитет.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

// ---------------------------------------------------------------- отношения
TEST(rules_diplomacy_relations) {
  Fix f;
  f.tx([&](Tx& tx) { setRelation(tx, f.A, f.B, 40, RelStatus::Alliance); });
  CHECK(f.w().relation(f.A, f.B) == (Relation{40, RelStatus::Alliance}));
  CHECK(f.w().relation(f.B, f.A) == (Relation{40, RelStatus::Alliance}));  // симметрично
  CHECK(has(lastLog(f.w()), "Отношения «Арден» и «Бельмар»: в союзе"));
  int logs = int(f.w().log.size());
  f.tx([&](Tx& tx) { setRelation(tx, f.B, f.A, 150, RelStatus::Alliance); });
  CHECK_NEAR(f.w().relation(f.A, f.B).v, 100, 1e-9);
  CHECK_EQ(int(f.w().log.size()), logs);  // значение без смены состояния — не событие хроники
  f.tx([&](Tx& tx) { setRelation(tx, f.A, f.B, -60, RelStatus::War); });
  CHECK_EQ(logCount(f.w(), LogKind::War), 1);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelation(tx, f.A, f.A, 0, RelStatus::Neutral); }); }), "самой собой"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelation(tx, f.A, 999, 0, RelStatus::Neutral); }); }), "не найдена"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelation(tx, f.A, f.B, std::nan(""), RelStatus::Neutral); }); }), "недопустимое число"));
  auto rows = relationsOf(f.w(), f.A);
  CHECK_EQ(rows.size(), size_t(4));  // все прочие фракции
  CHECK_EQ(rows[0].other, f.B);      // сначала государства по алфавиту, затем гильдии
  CHECK_EQ(rows[1].other, f.C);
  CHECK_EQ(rows[2].other, f.G);
  CHECK_EQ(rows[3].other, f.H);
  CHECK(rows[0].status == RelStatus::War);
  CHECK_NEAR(rows[0].value, -60, 1e-9);
  CHECK(rows[1].status == RelStatus::Unknown);  // нет записи — 0 и «незнакомы»
  CHECK_NEAR(rows[1].value, 0, 1e-9);
  CHECK(relationsOf(f.w(), 999).empty());
}

TEST(rules_diplomacy_same_religion) {
  Fix f;
  f.tx([&](Tx& tx) {
    const Id rel = tx.w().catalogs->religions.front().id;
    tx.faction(f.A).religion = rel;
    tx.faction(f.B).religion = rel;
    setRelation(tx, f.A, f.B, -55, RelStatus::Neutral);
    setSuzerain(tx, f.B, f.A);
  });
  CHECK_NEAR(relationBonus(f.w(), f.A, f.B), 10, 1e-12);
  CHECK_NEAR(relationValue(f.w(), f.A, f.B), -45, 1e-12);
  CHECK(!canVassalRebel(f.w(), f.B));   // −55 + 10 = −45: выше −50
}

// ---------------------------------------------------------------- перемирие и вассалитет
TEST(rules_diplomacy_truce_and_war_lock) {
  Fix f;
  Id race = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.A, race, 1000);
    tx.faction(f.B).res[kGold] = 300;
    declareWar(tx, f.A, f.B);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelation(tx, f.A, f.B, 0, RelStatus::Neutral); }); }), "перемирием"));
  Truce t;
  t.a = f.A;
  t.b = f.B;
  t.fromA.provinces = {f.p[0]};
  t.fromA.slaves = truceSlavesMax(f.w(), f.A);
  t.fromA.vassal = true;
  t.fromB.resources[kGold] = 200;
  t.fromB.reparations = 10;
  t.fromB.reparationsTurns = 3;
  CHECK_EQ(t.fromA.slaves, 100);   // 5 % от 2000
  Truce bad = t;
  bad.fromA.slaves = 101;
  CHECK(!truceProblems(f.w(), bad).empty());
  f.tx([&](Tx& tx) { concludeTruce(tx, t); });
  CHECK(f.w().relation(f.A, f.B).s == RelStatus::Neutral);
  CHECK_EQ(f.w().province(f.p[0])->owner, f.B);
  CHECK_EQ(f.w().faction(f.A)->suzerain, f.B);
  CHECK_EQ(f.w().faction(f.B)->slaves[0].count, 100);
  CHECK_EQ(popOf(f.w(), f.p[1]), 900);   // провинция уже у B — рабы только из оставшейся
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 200, 1e-9);
  CHECK_NEAR(f.fc(f.A).incTribute, 10, 1e-9);
}

// Вассалитет по перемирию без цикла: сюзерен, который сам вассал будущего вассала, не принимается; смена прежнего
// сюзерена записана в перемирии.
TEST(rules_diplomacy_truce_vassal_chain) {
  Fix f;
  f.tx([&](Tx& tx) {
    declareWar(tx, f.A, f.B);
    setSuzerain(tx, f.B, f.A);   // B — вассал A (объявление войны вассалу сняло бы вассалитет — ставится после)
  });
  Truce t;
  t.a = f.A;
  t.b = f.B;
  t.fromA.vassal = true;       // A стал бы вассалом своего вассала B
  CHECK(has(join(truceProblems(f.w(), t), "; "), "вассал"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { concludeTruce(tx, t); }); }), "вассал"));
  CHECK_EQ(f.w().faction(f.A)->suzerain, Id(0));
  // B становится вассалом C по перемирию с C: прежний сюзерен A теряет вассала.
  f.tx([&](Tx& tx) { declareWar(tx, f.C, f.B); });
  Truce c;
  c.a = f.B;
  c.b = f.C;
  c.fromA.vassal = true;
  CHECK(truceProblems(f.w(), c).empty());
  f.tx([&](Tx& tx) { concludeTruce(tx, c); });
  CHECK_EQ(f.w().faction(f.B)->suzerain, f.C);
  CHECK(!lastLog(f.w(), "выходит из вассалитета").empty());
}

// Все земли побеждённого — победителю вместе с запасами; валюты — только если у побеждённого нет войск и флота.
TEST(rules_diplomacy_truce_conquest_loot) {
  Fix f;
  const Id iron = test::idByName(f.w().catalogs->resources, "Железо");
  const Id corpses = resourceId(f.w(), schema::kResCorpses);
  const Id ess = f.w().catalogs->essences.front().id;
  Id row = 0, army = 0;
  f.tx([&](Tx& tx) {
    const Id race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.B, race, 1000);
    own(tx, f.p[2], f.B, race, 1000);
    declareWar(tx, f.A, f.B);
    Faction& b = tx.faction(f.B);
    b.res[kGold] = 500;
    b.res[iron] = 40;
    b.res[corpses] = 7;
    b.ess[ess] = 12;
    row = addArmyRow(tx, f.B, UnitType::LightInf, "", 10, 1);
    army = createArmy(tx, ArmyKind::Army, f.B, center(1));
    setUnits(tx, army, f.B, row, 10);
  });
  const World start = f.w();
  Truce t;
  t.a = f.A;
  t.b = f.B;
  t.fromB.provinces = {f.p[1], f.p[2]};
  f.tx([&](Tx& tx) { concludeTruce(tx, t); });
  // Войско у побеждённого есть — валюты (золото, трупы) остаются у него.
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 40, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->essence(ess), 12, 1e-9);
  CHECK_NEAR(f.w().faction(f.B)->stock(iron), 0, 1e-9);
  CHECK_NEAR(f.w().faction(f.B)->treasury(), 500, 1e-9);
  CHECK_NEAR(f.w().faction(f.B)->stock(corpses), 7, 1e-9);
  CHECK(!lastLog(f.w(), "забирает запасы").empty());
  // Без войск — и валюты.
  f.s.replace(start, "test");
  f.tx([&](Tx& tx) { tx.eraseArmy(army); });
  f.tx([&](Tx& tx) { concludeTruce(tx, t); });
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 500, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(corpses), 7, 1e-9);
  CHECK_NEAR(f.w().faction(f.B)->treasury(), 0, 1e-9);
  // Не все земли — запасы остаются.
  f.s.replace(start, "test");
  t.fromB.provinces = {f.p[1]};
  f.tx([&](Tx& tx) { concludeTruce(tx, t); });
  CHECK_NEAR(f.w().faction(f.B)->stock(iron), 40, 1e-9);
}

TEST(rules_diplomacy_vassalage) {
  Fix f;
  f.tx([&](Tx& tx) { setSuzerain(tx, f.A, f.B); });
  CHECK_EQ(vassalsOf(f.w(), f.B).size(), size_t(1));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setSuzerain(tx, f.B, f.A); }); }), "вассал"));
  // Войну вассалу объявил C: сюзерен вступает.
  f.tx([&](Tx& tx) { declareWar(tx, f.C, f.A); });
  f.tx([&](Tx& tx) { suzerainDefends(tx, f.B, f.A, f.C, true); });
  CHECK(f.w().relation(f.B, f.C).s == RelStatus::War);
  CHECK_NEAR(f.w().relation(f.B, f.A).v, 15, 1e-9);
  // Не вступает — отношения −20.
  f.tx([&](Tx& tx) { suzerainDefends(tx, f.B, f.A, f.C, false); });
  CHECK_NEAR(f.w().relation(f.B, f.A).v, -5, 1e-9);
  // Призыв вассала: отказ −10.
  f.tx([&](Tx& tx) { vassalAnswers(tx, f.B, f.A, f.C, false); });
  CHECK_NEAR(f.w().relation(f.B, f.A).v, -15, 1e-9);
  CHECK(!canVassalRebel(f.w(), f.A));
  f.tx([&](Tx& tx) { shiftRelation(tx, f.A, f.B, -40); });
  CHECK(canVassalRebel(f.w(), f.A));
  f.tx([&](Tx& tx) { vassalRebels(tx, f.A); });
  CHECK_EQ(f.w().faction(f.A)->suzerain, Id(0));
  CHECK(f.w().relation(f.A, f.B).s == RelStatus::War);
}
