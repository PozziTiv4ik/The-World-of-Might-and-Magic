// Regnum — правила: петли войны и дипломатии (аудит 2026-10-23, 5.1–5.14, 3.5, срок перемирия): освобождение
// оккупированной провинции, перемирие и оккупация, срок перемирия, бой только между воюющими, вассал и сюзерен, война
// и сделки, распад союза на флоте, упразднение государства без земель и войск, добыча за последнюю провинцию, тюрьмы и
// реликвии упразднённого государства, тайник без хозяина, неназначенное море, перегруженный флот, проверки штурма и
// захвата.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

// П0 и П1 — провинции A; П0 оккупирована B (A и B в войне). У A — тяжёлая пехота, у B — лёгкая.
struct Occupied : Fix {
  Id race = 0, rA = 0, rB = 0;
  Occupied() {
    tx([&](Tx& t) {
      race = addRace(t);
      own(t, p[0], A, race, 1000);
      own(t, p[1], A, race, 1000);
      declareWar(t, B, A);
      rA = addArmyRow(t, A, UnitType::HeavyInf, "", 300, 1);
      rB = addArmyRow(t, B, UnitType::LightInf, "", 200, 1);
      setOccupied(t, p[0], B);
    });
  }
  // Оккупационный гарнизон B: войско B входит в провинцию, гарнизон пополняется, войско расформировывается.
  void garrison(i64 n) {
    tx([&](Tx& t) {
      const Id x = createArmy(t, ArmyKind::Army, B, center(0) + Vec2{60, 60});
      setUnits(t, x, B, rB, 1);
      setOccupationGarrison(t, p[0], rB, n);
      disband(t, x);
    });
  }
  Id army(Id faction, Id row, Vec2 at, i64 n) {
    Id id = 0;
    tx([&](Tx& t) {
      id = createArmy(t, ArmyKind::Army, faction, at);
      if (n > 0) setUnits(t, id, faction, row, n);
    });
    return id;
  }
};

// Морская провинция «Северное море» над верхним рядом (П0…П3 — у её берега).
Id northSea(Fix& f) {
  Id north = 0;
  f.tx([&](Tx& t) { north = rules::createProvince(t, rect(60, 20, 940, 99), Terrain::Sea).province; });
  return north;
}

}  // namespace

// ---------------------------------------------------------------- 5.1 освобождение
TEST(rules_war_loops_liberation_battle) {
  Occupied f;
  f.garrison(100);
  CHECK_EQ(f.w().province(f.p[0])->occGarrison.at(0).count, 100);
  const Id a = f.army(f.A, f.rA, center(1), 300);
  std::string why;
  CHECK(!canLiberate(f.w(), a, f.p[0], &why));
  CHECK(has(why, "не у провинции «П0»"));
  CHECK(canLiberate(f.w(), a, f.p[0], &why, center(0)));   // встанет у стен — можно
  CHECK(!canSiege(f.w(), a, f.p[0], &why, center(0)));     // свою провинцию не штурмуют
  CHECK(has(why, "самого государства"));
  f.tx([&](Tx& tx) { moveArmy(tx, a, center(0)); });
  CHECK(canLiberate(f.w(), a, f.p[0]));
  // Поражение: освободитель отходит на исходную позицию, оккупация остаётся.
  const World before = f.w();
  SiegeResult r;
  r.attacker = a;
  r.province = f.p[0];
  r.attackerWins = false;
  r.attackerOrigin = center(1);
  r.attackerLosses[{f.A, f.rA}] = 100;
  r.garrisonLosses[f.rB] = 30;
  BattleOutcome out;
  f.tx([&](Tx& tx) { out = resolveLiberation(tx, r); });
  CHECK_EQ(out.winner, f.B);
  CHECK(f.w().province(f.p[0])->occupied);
  CHECK_EQ(f.w().province(f.p[0])->occGarrison.at(0).count, 70);
  CHECK_EQ(f.w().faction(f.B)->armyRow(f.rB)->total, 170);
  CHECK_EQ(f.w().faction(f.A)->armyRow(f.rA)->total, 200);
  CHECK_EQ(provinceAtPos(f.w(), f.w().army(a)->pos), f.p[1]);
  CHECK(has(lastLog(f.w(), "Освобождение провинции"), "Оккупанты устояли"));
  // Победа: оккупационный гарнизон разбит, оккупация снята, уцелевшие — в резерв оккупанта.
  f.s.replace(before, "test");
  r.attackerWins = true;
  r.garrisonLosses[f.rB] = 60;
  f.tx([&](Tx& tx) { out = resolveLiberation(tx, r); });
  CHECK_EQ(out.winner, f.A);
  CHECK_EQ(out.winnerArmy, a);
  const Province& p = *f.w().province(f.p[0]);
  CHECK(!p.occupied);
  CHECK_EQ(p.occupier, Id(0));
  CHECK(p.occGarrison.empty());
  CHECK_EQ(f.w().faction(f.B)->armyRow(f.rB)->total, 140);
  CHECK_EQ(reserveOf(f.w(), f.B, f.rB), 140);
  CHECK_EQ(unitsIn(f.w(), a), 200);
  CHECK(has(lastLog(f.w(), "Освобождение провинции"), "оккупация снята"));
  // Потери больше гарнизона — отказ.
  f.s.replace(before, "test");
  r.garrisonLosses[f.rB] = 101;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { resolveLiberation(tx, r); }); }), "больше его численности"));
}

TEST(rules_war_loops_liberation_without_garrison_and_blockers) {
  Occupied f;
  const Id bx = f.army(f.B, f.rB, center(0) + Vec2{60, 0}, 20);
  const Id a = f.army(f.A, f.rA, center(0) - Vec2{60, 0}, 100);
  std::string why;
  CHECK(!canLiberate(f.w(), a, f.p[0], &why));
  CHECK(has(why, "Сначала разбейте войско «Войско №1»"));
  CHECK_EQ(assaultBlocker(f.w(), a, f.p[0]), bx);
  // Войско оккупанта ушло, гарнизона нет — оккупация снимается сразу, без боя.
  f.tx([&](Tx& tx) { moveArmy(tx, bx, center(5)); });
  CHECK_EQ(assaultBlocker(f.w(), a, f.p[0]), Id(0));
  SiegeResult r;
  r.attacker = a;
  r.province = f.p[0];
  BattleOutcome out;
  f.tx([&](Tx& tx) { out = resolveLiberation(tx, r); });
  CHECK_EQ(out.winner, f.A);
  CHECK(!f.w().province(f.p[0])->occupied);
  CHECK(has(lastLog(f.w()), "освобождает провинцию «П0»"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { resolveLiberation(tx, r); }); }), "не оккупирована"));
}

TEST(rules_war_loops_liberation_by_ally) {
  Occupied f;
  Id rC = 0;
  f.tx([&](Tx& tx) { rC = addArmyRow(tx, f.C, UnitType::LightInf, "", 50, 1); });
  const Id c = f.army(f.C, rC, center(0), 50);
  std::string why;
  CHECK(!canLiberate(f.w(), c, f.p[0], &why));
  CHECK(has(why, "владелец «Арден» или его союзник"));
  f.tx([&](Tx& tx) { setRelation(tx, f.C, f.A, 60, RelStatus::Alliance); });
  CHECK(!canLiberate(f.w(), c, f.p[0], &why));
  CHECK(has(why, "«Церис» и «Бельмар» не в войне"));
  f.tx([&](Tx& tx) { declareWar(tx, f.C, f.B); });
  CHECK(canLiberate(f.w(), c, f.p[0]));
  // Оккупант сам у себя провинцию не освобождает.
  const Id bx = f.army(f.B, f.rB, center(4), 5);
  CHECK(!canLiberate(f.w(), bx, f.p[0], &why, center(0)));
  CHECK(has(why, "оккупирована самим"));
}

// ---------------------------------------------------------------- 5.2 перемирие и оккупация, срок перемирия
TEST(rules_war_loops_truce_lifts_occupations_and_keeps_peace) {
  Fix f;
  f.tx([&](Tx& tx) {
    const Id race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.A, race, 1000);
    own(tx, f.p[2], f.B, race, 1000);
    own(tx, f.p[3], f.B, race, 1000);
    declareWar(tx, f.A, f.B);
    setOccupied(tx, f.p[0], f.B);
    setOccupied(tx, f.p[2], f.A);
    setOccupied(tx, f.p[3], f.A);
  });
  Truce t;
  t.a = f.A;
  t.b = f.B;
  t.fromB.provinces = {f.p[3]};   // уступленная оккупанту провинция — его без оккупации
  f.tx([&](Tx& tx) { concludeTruce(tx, t); });
  for (int i : {0, 2, 3}) CHECK(!f.w().province(f.p[i])->occupied);
  CHECK_EQ(f.w().province(f.p[3])->owner, f.A);
  const int calm = f.w().turn() + 5;
  CHECK_EQ(f.w().relation(f.A, f.B).calm, calm);
  const std::string l = lastLog(f.w(), "Перемирие");
  CHECK(has(l, "мир до хода " + std::to_string(calm)));
  CHECK(has(l, "Оккупация снята: «П0», «П2»"));
  // Срок перемирия: войну не объявить ни прямо, ни встречей войск, ни вступлением сюзерена за вассала.
  std::string why;
  CHECK(!canDeclareWar(f.w(), f.B, f.A, &why));
  CHECK(has(why, "мир до хода " + std::to_string(calm)));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { declareWar(tx, f.A, f.B); }); }), "мир до хода"));
  Id a = 0;
  f.tx([&](Tx& tx) {
    a = createArmy(tx, ArmyKind::Army, f.A, center(5));
    createArmy(tx, ArmyKind::Army, f.B, center(6));
  });
  Encounter e = encounter(f.w(), a, center(6));
  CHECK(e.type == EncounterType::Blocked);
  CHECK(has(e.reason, "мир до хода"));
  f.tx([&](Tx& tx) {
    setSuzerain(tx, f.C, f.A);
    declareWar(tx, f.B, f.C);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { suzerainDefends(tx, f.A, f.C, f.B, true); }); }), "мир до хода"));
  CHECK(f.w().relation(f.A, f.B).s != RelStatus::War);
  // Правка отношений срок не сбрасывает; с хода calm война объявляется.
  f.tx([&](Tx& tx) { setRelation(tx, f.A, f.B, 30, RelStatus::Neutral); });
  CHECK_EQ(f.w().relation(f.A, f.B).calm, calm);
  f.tx([&](Tx& tx) { tx.meta().turn = calm; });
  CHECK(canDeclareWar(f.w(), f.B, f.A));
  f.tx([&](Tx& tx) { declareWar(tx, f.B, f.A); });
  CHECK(f.w().relation(f.A, f.B).s == RelStatus::War);
}

TEST(rules_war_loops_truce_calm_blocks_vassal_call) {
  Fix f;
  f.tx([&](Tx& tx) {
    declareWar(tx, f.A, f.B);
    Truce t;
    t.a = f.A;
    t.b = f.B;
    concludeTruce(tx, t);
    setSuzerain(tx, f.A, f.C);   // A — вассал C
    declareWar(tx, f.C, f.B);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { vassalAnswers(tx, f.C, f.A, f.B, true); }); }), "мир до хода"));
  f.tx([&](Tx& tx) { vassalAnswers(tx, f.C, f.A, f.B, false); });   // отказ — можно
  CHECK(f.w().relation(f.A, f.B).s != RelStatus::War);
}

TEST(rules_war_loops_turn_lifts_occupation_without_war) {
  Fix f;
  f.tx([&](Tx& tx) {
    const Id race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.A, race, 1000);
    setOccupied(tx, f.p[0], f.B);   // без войны (правка администратора)
    declareWar(tx, f.A, f.C);
    setOccupied(tx, f.p[1], f.C);   // в войне — остаётся
    Province& m = tx.province(f.p[2]);   // провинция без владельца
    m.occupied = true;
    m.occupier = f.C;
  });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(!f.w().province(f.p[0])->occupied);
  CHECK(!lastLog(f.w(), "снята: «Бельмар» и «Арден» не в войне").empty());
  CHECK(f.w().province(f.p[1])->occupied);
  CHECK(!f.w().province(f.p[2])->occupied);
  CHECK(!lastLog(f.w(), "у провинции нет владельца").empty());
}

TEST(rules_war_loops_occupation_garrison_needs_army) {
  Occupied f;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setOccupationGarrison(tx, f.p[0], f.rB, 10); }); }), "стоит войско «Бельмар»"));
  const Id x = f.army(f.B, f.rB, center(4), 5);   // в соседней провинции — не помогает
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setOccupationGarrison(tx, f.p[0], f.rB, 10); }); }), "стоит войско"));
  f.tx([&](Tx& tx) {
    moveArmy(tx, x, center(0));
    setOccupationGarrison(tx, f.p[0], f.rB, 10);
    disband(tx, x);
  });
  CHECK_EQ(f.w().province(f.p[0])->occGarrison.at(0).count, 10);
  f.tx([&](Tx& tx) { setOccupationGarrison(tx, f.p[0], f.rB, 4); });   // вывести — всегда
  CHECK_EQ(f.w().province(f.p[0])->occGarrison.at(0).count, 4);
}

// ---------------------------------------------------------------- 5.4 бой — только между воюющими
TEST(rules_war_loops_battle_needs_every_pair_at_war) {
  Fix f;
  Id rA = 0, rC = 0, rB = 0, al = 0, bB = 0;
  f.tx([&](Tx& tx) {
    rA = addArmyRow(tx, f.A, UnitType::LightInf, "", 50, 1);
    rC = addArmyRow(tx, f.C, UnitType::LightInf, "", 50, 1);
    rB = addArmyRow(tx, f.B, UnitType::LightInf, "", 50, 1);
    setRelation(tx, f.A, f.C, 70, RelStatus::Alliance);
    al = createArmy(tx, ArmyKind::Army, f.A, center(5));
    const Id ac = createArmy(tx, ArmyKind::Army, f.C, center(6));
    setUnits(tx, al, f.A, rA, 50);
    setUnits(tx, ac, f.C, rC, 20);
    formAllied(tx, al, ac);
    bB = createArmy(tx, ArmyKind::Army, f.B, center(1));
    setUnits(tx, bB, f.B, rB, 30);
  });
  // Никто не воюет: войну союзному объекту не объявляют.
  Encounter e = encounter(f.w(), bB, center(5));
  CHECK(e.type == EncounterType::Blocked);
  CHECK(has(e.reason, "«Бельмар» и «Арден» не в войне — сначала распустите союз"));
  // Война с одним из союзников — битвы ещё нет: «Церис» не воюет.
  f.tx([&](Tx& tx) { declareWar(tx, f.B, f.A); });
  e = encounter(f.w(), bB, center(5));
  CHECK(e.type == EncounterType::Blocked);
  CHECK(has(e.reason, "«Бельмар» и «Церис» не в войне — сначала распустите союз"));
  BattleResult r;
  r.attacker = bB;
  r.defender = al;
  r.attackerWins = true;
  r.attackerOrigin = center(1);
  r.losses[al][{f.C, rC}] = 5;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { resolveBattle(tx, r); }); }), "не в войне — сначала распустите союз"));
  CHECK_EQ(f.w().faction(f.C)->armyRow(rC)->total, 50);
  // Все пары в войне — битва.
  f.tx([&](Tx& tx) { declareWar(tx, f.B, f.C); });
  e = encounter(f.w(), bB, center(5));
  CHECK(e.type == EncounterType::Battle);
  f.tx([&](Tx& tx) { resolveBattle(tx, r); });
  CHECK_EQ(f.w().faction(f.C)->armyRow(rC)->total, 45);
}

// ---------------------------------------------------------------- 5.5 вассал и сюзерен
TEST(rules_war_loops_vassal_and_suzerain_war) {
  Fix f;
  Id a = 0, b = 0;
  f.tx([&](Tx& tx) {
    setSuzerain(tx, f.B, f.A);   // B — вассал A
    a = createArmy(tx, ArmyKind::Army, f.A, center(5));
    b = createArmy(tx, ArmyKind::Army, f.B, center(6));
  });
  std::string why;
  CHECK(!canDeclareWar(f.w(), f.B, f.A, &why));
  CHECK(has(why, "вассал «Арден»"));
  CHECK(has(why, "«Восстать»"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { declareWar(tx, f.B, f.A); }); }), "«Восстать»"));
  Encounter e = encounter(f.w(), b, center(5));
  CHECK(e.type == EncounterType::Blocked);
  CHECK(has(e.reason, "«Восстать»"));
  // Сюзерен объявляет войну вассалу — вассалитет снимается.
  CHECK(encounter(f.w(), a, center(6)).type == EncounterType::DeclareWar);
  f.tx([&](Tx& tx) { declareWar(tx, f.A, f.B); });
  CHECK_EQ(f.w().faction(f.B)->suzerain, Id(0));
  CHECK(f.w().relation(f.A, f.B).s == RelStatus::War);
  CHECK(!lastLog(f.w(), "«Бельмар» больше не вассал «Арден»").empty());
}

// ---------------------------------------------------------------- 5.7 война, сделки и предложения
TEST(rules_war_loops_war_cancels_deals_and_proposals) {
  Fix f;
  const Id iron = test::idByName(f.w().catalogs->resources, "Железо");
  Id trade = 0, tribute = 0, rep = 0, other = 0, prop = 0;
  Deal d;
  d.kind = DealKind::Trade;
  d.a = f.A;
  d.b = f.B;
  d.items.push_back(DealItem{DealSide::A, iron, 5, DealMode::PerTurn, 5, 5});
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[iron] = 100;
    trade = concludeDeal(tx, d);
    tribute = imposeTribute(tx, DealKind::Tribute, f.A, f.B, 3, 4);
    rep = imposeTribute(tx, DealKind::Reparations, f.B, f.A, 2, 4);
    other = imposeTribute(tx, DealKind::Tribute, f.A, f.C, 1, 4);
    Proposal p;
    p.kind = ProposalKind::Deal;
    p.from = f.A;
    p.to = f.B;
    p.deal = d;
    prop = propose(tx, p);
  });
  CHECK(f.w().proposal(prop) != nullptr);
  f.tx([&](Tx& tx) { declareWar(tx, f.B, f.A); });
  for (Id id : {trade, tribute, rep}) CHECK(f.w().deal(id)->status == DealStatus::Cancelled);
  CHECK(f.w().deal(other)->status == DealStatus::Active);
  CHECK(f.w().proposal(prop) == nullptr);
  const std::string l = lastLog(f.w(), "расторгла");
  CHECK(has(l, "сделка «каждый ход»"));
  CHECK(has(l, "дань («Бельмар» платит «Арден»)"));
  CHECK(has(l, "репарации («Арден» платит «Бельмар»)"));
  // Сделки между воюющими невозможны.
  Deal once = d;
  once.items[0].mode = DealMode::Once;
  DealCheck c = validateDeal(f.w(), once);
  CHECK(!c.ok);
  CHECK(has(join(c.problems, "; "), "в войне: сделки невозможны"));
  // Перемирие с репарациями — уже не между воюющими.
  Truce t;
  t.a = f.A;
  t.b = f.B;
  t.fromA.reparations = 4;
  t.fromA.reparationsTurns = 2;
  f.tx([&](Tx& tx) { concludeTruce(tx, t); });
  CHECK(f.w().deal(test::newestDeal(f.w()))->kind == DealKind::Reparations);
  CHECK(f.w().deal(test::newestDeal(f.w()))->status == DealStatus::Active);
}

// ---------------------------------------------------------------- 5.9 распад союза на флоте
namespace {

struct AlliedFleet : Fix {
  Id north = 0, rowFa = 0, rowFc = 0, rowA = 0, rowC = 0, fa = 0, aa = 0;
  // Союзный флот {A, C} (у C — shipsC фрегатов) с союзным войском {A — 800, C — 600} на борту.
  explicit AlliedFleet(i64 shipsC) {
    north = northSea(*this);
    tx([&](Tx& t) {
      setRelation(t, A, C, 80, RelStatus::Alliance);
      rowFa = addFleetRow(t, A, ShipType::Frigate, "", 5, 0);
      rowFc = addFleetRow(t, C, ShipType::Frigate, "", 5, 0);
      rowA = addArmyRow(t, A, UnitType::LightInf, "", 1000, 0);
      rowC = addArmyRow(t, C, UnitType::LightInf, "", 1000, 0);
      fa = createArmy(t, ArmyKind::Fleet, A, {400, 60});
      setUnits(t, fa, A, rowFa, 2);
      const Id fc = createArmy(t, ArmyKind::Fleet, C, {600, 60});
      setUnits(t, fc, C, rowFc, 1);
      formAllied(t, fa, fc);
      if (shipsC == 0) setUnits(t, fa, C, rowFc, 0);
      aa = createArmy(t, ArmyKind::Army, A, center(1));
      setUnits(t, aa, A, rowA, 800);
      const Id ac = createArmy(t, ArmyKind::Army, C, center(2));
      setUnits(t, ac, C, rowC, 600);
      formAllied(t, aa, ac);
      embark(t, aa, fa);
    });
  }
  // Объекты фракции C: флот и войско.
  std::pair<Id, Id> ofC() const {
    Id ship = 0, army = 0;
    w().armies.each([&](const Army& x) {
      if (x.leader() == C) (x.isFleet() ? ship : army) = x.id;
    });
    return {ship, army};
  }
};

}  // namespace

TEST(rules_war_loops_alliance_split_aboard_own_ships) {
  AlliedFleet f(1);
  CHECK(f.w().army(f.fa)->allied());
  CHECK(f.w().army(f.aa)->allied());
  f.tx([&](Tx& tx) { declareWar(tx, f.A, f.C); });
  CHECK(f.w().relation(f.A, f.C).s == RelStatus::War);
  // Флот распущен первым: войско A — на кораблях A, группа C — на своих кораблях.
  CHECK(!f.w().army(f.fa)->allied());
  CHECK_EQ(cargoOf(f.w(), f.fa), f.aa);
  CHECK(!f.w().army(f.aa)->allied());
  CHECK_EQ(unitsIn(f.w(), f.aa, f.A, f.rowA), 800);
  auto [ship, army] = f.ofC();
  CHECK(ship != 0 && army != 0);
  CHECK_EQ(cargoOf(f.w(), ship), army);
  CHECK_EQ(unitsIn(f.w(), army, f.C, f.rowC), 600);
  CHECK(f.w().army(army)->pos == f.w().army(ship)->pos);
}

TEST(rules_war_loops_alliance_split_aboard_lands_or_perishes) {
  {
    // У C нет кораблей во флоте: группа высаживается на сушу по правилам высадки (у берега Северного моря).
    AlliedFleet f(0);
    f.tx([&](Tx& tx) { declareWar(tx, f.A, f.C); });
    CHECK_EQ(cargoOf(f.w(), f.fa), f.aa);
    auto [ship, army] = f.ofC();
    CHECK(army != 0);
    CHECK_EQ(carrierOf(f.w(), army), Id(0));
    CHECK(validPosition(f.w(), ArmyKind::Army, f.w().army(army)->pos, army));
    CHECK(has(landingProvinces(f.w(), f.fa), provinceAtPos(f.w(), f.w().army(army)->pos)));
    CHECK(!lastLog(f.w(), "на берег: союз распался").empty());
    (void)ship;
  }
  {
    // Флот в Западном море — у него нет сухопутных соседей: группе C негде высадиться, она гибнет; война объявлена.
    AlliedFleet f(0);
    f.tx([&](Tx& tx) { moveArmy(tx, f.fa, kSeaWest); });
    f.tx([&](Tx& tx) { declareWar(tx, f.A, f.C); });
    CHECK(f.w().relation(f.A, f.C).s == RelStatus::War);
    CHECK_EQ(cargoOf(f.w(), f.fa), f.aa);
    CHECK_EQ(f.ofC().second, Id(0));
    CHECK_EQ(f.w().faction(f.C)->armyRow(f.rowC)->total, 400);
    CHECK(has(lastLog(f.w(), "погибли"), "негде высадиться"));
  }
  {
    // «Распустить союз» вручную для войска на борту — отказ: сначала высадка.
    AlliedFleet f(1);
    CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { dissolveAllied(tx, f.aa); }); }), "сначала высадите"));
  }
}

// ---------------------------------------------------------------- 5.10 государство без земель и войск
TEST(rules_war_loops_landless_state_abolished) {
  Fix f;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.A, addRace(tx), 1000);
    declareWar(tx, f.A, f.B);   // у B нет ни провинций, ни войск
  });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(f.w().faction(f.B)->landless, 1);
  CHECK_EQ(f.w().faction(f.C)->landless, 0);   // не в войне — заготовка администратора, не считается
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(f.w().faction(f.B)->landless, 2);
  const World two = f.w();
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(f.w().faction(f.B) == nullptr);
  CHECK(has(lastLog(f.w(), "упразднено"), "нет земель и войск 3 хода"));
  CHECK(f.w().faction(f.C) != nullptr);
  // Войско на карте прерывает отсчёт.
  f.s.replace(two, "test");
  f.tx([&](Tx& tx) { createArmy(tx, ArmyKind::Army, f.B, center(6)); });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(f.w().faction(f.B) != nullptr);
  CHECK_EQ(f.w().faction(f.B)->landless, 0);
  // Мир тоже прерывает отсчёт.
  f.s.replace(two, "test");
  f.tx([&](Tx& tx) {
    Truce t;
    t.a = f.A;
    t.b = f.B;
    concludeTruce(tx, t);
    endTurn(tx);
  });
  CHECK(f.w().faction(f.B) != nullptr);
  CHECK_EQ(f.w().faction(f.B)->landless, 0);
}

TEST(rules_war_loops_rebel_state_abolished_at_once) {
  Fix f;
  Id rebel = 0;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.A, addRace(tx), 1000);
    rebel = rebelStateFor(tx, f.A);
  });
  CHECK(f.w().faction(rebel) != nullptr);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(f.w().faction(rebel) == nullptr);
}

// ---------------------------------------------------------------- §25 добыча за последнюю провинцию
TEST(rules_war_loops_last_province_loot) {
  Fix f;
  const Id iron = test::idByName(f.w().catalogs->resources, "Железо");
  Id race = 0, crown = 0, cup = 0, army = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[1], f.B, race, 1000);
    declareWar(tx, f.A, f.B);
    tx.faction(f.B).res[iron] = 40;
    tx.faction(f.B).prison = {RacePop{race, 25}};
    crown = addRelic(tx, "Корона", Rarity::Legendary);
    cup = addRelic(tx, "Кубок", Rarity::Common);
    moveRelic(tx, crown, RelicPlace{RelicPlace::State, f.B, 0, f.B});
    moveRelic(tx, cup, RelicPlace{}, RelicPlace{RelicPlace::State, f.B, 0, f.B}, 3);
    const Id row = addArmyRow(tx, f.A, UnitType::LightInf, "", 10, 1);
    army = createArmy(tx, ArmyKind::Army, f.A, center(1));
    setUnits(tx, army, f.A, row, 10);
  });
  const World start = f.w();
  // Опустошение последней провинции: запасы, реликвии склада и заключённые побеждённого — победителю.
  f.tx([&](Tx& tx) { capture(tx, army, f.p[1], Capture::Devastate, 10); });
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 40, 1e-9);
  CHECK_EQ(relicCount(f.w().faction(f.A)->relics, crown), 1);
  CHECK_EQ(relicCount(f.w().faction(f.A)->relics, cup), 3);
  CHECK(f.w().faction(f.B)->relics.empty());
  CHECK_EQ(f.w().faction(f.A)->prison.at(0).pop, 25);
  CHECK(f.w().faction(f.B)->prison.empty());
  const std::string l = lastLog(f.w(), "опустошает");
  CHECK(has(l, "забирает запасы"));
  CHECK(has(l, "реликвии: «Корона», «Кубок» × 3"));
  CHECK(has(l, "заключённых: 25"));
  // Разорение последней провинции — так же.
  f.s.replace(start, "test");
  f.tx([&](Tx& tx) { capture(tx, army, f.p[1], Capture::Raze); });
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 40, 1e-9);
  CHECK_EQ(f.w().province(f.p[1])->owner, f.B);
  // Не последняя провинция — запасы остаются.
  f.s.replace(start, "test");
  f.tx([&](Tx& tx) { own(tx, f.p[2], f.B, race, 1000); });
  f.tx([&](Tx& tx) { capture(tx, army, f.p[1], Capture::Raze); });
  CHECK_NEAR(f.w().faction(f.B)->stock(iron), 40, 1e-9);
  CHECK_EQ(relicCount(f.w().faction(f.B)->relics, crown), 1);
}

TEST(rules_war_loops_truce_loot_relics_and_prison) {
  Fix f;
  Id race = 0, crown = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.B, race, 1000);
    declareWar(tx, f.A, f.B);
    tx.faction(f.B).prison = {RacePop{race, 7}};
    tx.faction(f.A).prison = {RacePop{race, 3}};
    crown = addRelic(tx, "Корона", Rarity::Epochal);
    moveRelic(tx, crown, RelicPlace{RelicPlace::State, f.B, 0, f.B});
  });
  Truce t;
  t.a = f.A;
  t.b = f.B;
  t.fromB.provinces = {f.p[1]};
  f.tx([&](Tx& tx) { concludeTruce(tx, t); });
  CHECK_EQ(relicCount(f.w().faction(f.A)->relics, crown), 1);
  CHECK_EQ(f.w().faction(f.A)->prison.at(0).pop, 10);
  CHECK(has(lastLog(f.w(), "Перемирие"), "реликвии: «Корона»"));
}

// ---------------------------------------------------------------- 3.5 упразднение: тюрьмы, реликвии, тайник
TEST(rules_war_loops_removed_state_prison_and_relics) {
  Fix f;
  Id crown = 0;
  f.tx([&](Tx& tx) {
    const Id race = addRace(tx);
    own(tx, f.p[1], f.B, race, 1000);
    tx.faction(f.B).prison = {RacePop{race, 30}};
    tx.faction(f.C).prison = {RacePop{race, 10}};
    crown = addRelic(tx, "Корона", Rarity::Legendary);
    moveRelic(tx, crown, RelicPlace{RelicPlace::State, f.B, 0, f.B});
  });
  CHECK(!relicFree(f.w(), crown));
  f.tx([&](Tx& tx) { removeFaction(tx, f.B); });
  CHECK_EQ(popOf(f.w(), f.p[1]), 1030);   // заключённые — в население своей провинции
  CHECK(!lastLog(f.w(), "освобождены в провинции государства: 30").empty());
  CHECK(relicFree(f.w(), crown));
  CHECK(has(lastLog(f.w(), "снова свободны"), "«Корона»"));
  f.tx([&](Tx& tx) { removeFaction(tx, f.C); });   // без провинций — рассеиваются
  CHECK(!lastLog(f.w(), "рассеялись: 10").empty());
}

TEST(rules_war_loops_orphan_cache_unearthed_by_owner) {
  Fix f;
  Id relic = 0;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.B, addRace(tx), 1000);
    relic = addRelic(tx, "Кубок", Rarity::Epic);
    moveRelic(tx, relic, RelicPlace{RelicPlace::State, f.B, 0, f.B});
    hideRelic(tx, f.p[0], relic);
    setProvinceOwner(tx, f.p[0], f.A);
  });
  CHECK(!canUnearth(f.w(), f.p[0]));   // спрятавшее государство живо и провинцией не владеет
  f.tx([&](Tx& tx) { removeFaction(tx, f.B); });
  CHECK_EQ(f.w().province(f.p[0])->hiddenBy, Id(0));
  CHECK(canUnearth(f.w(), f.p[0]));
  f.tx([&](Tx& tx) { unearthRelic(tx, f.p[0]); });
  CHECK_EQ(relicCount(f.w().faction(f.A)->relics, relic), 1);
  CHECK_EQ(f.w().province(f.p[0])->hiddenRelic, Id(0));
}

// ---------------------------------------------------------------- 5.11 неназначенное море, 5.14 перегруз
TEST(rules_war_loops_open_sea_moves) {
  Fix f;
  Id fl = 0, fn = 0, fe = 0;
  f.tx([&](Tx& tx) {
    fl = createArmy(tx, ArmyKind::Fleet, f.A, kSeaNorth);      // неназначенное море
    fn = createArmy(tx, ArmyKind::Fleet, f.A, {850, 50});      // неназначенное море у Восточного
    fe = createArmy(tx, ArmyKind::Fleet, f.A, {965, 300});     // Восточное море
  });
  std::string why;
  CHECK(moveSpendsAction(f.w(), fl, kSeaNorth, kSeaNorth + Vec2{150, 0}));
  CHECK(canPlayerMove(f.w(), fl, kSeaNorth + Vec2{150, 0}, &why));
  CHECK(!canPlayerMove(f.w(), fl, kSeaNorth + Vec2{300, 0}, &why));
  CHECK(has(why, "вне провинций — недалеко"));
  // Из неназначенного моря — в морскую провинцию рядом; из морской провинции — в неназначенное море рядом.
  CHECK(canPlayerMove(f.w(), fn, {965, 60}, &why));
  CHECK(moveSpendsAction(f.w(), fn, {850, 50}, {965, 60}));
  CHECK(canPlayerMove(f.w(), fe, {920, 300}, &why));
  CHECK(!canPlayerMove(f.w(), fe, {920, 50}, &why));
  // Внутри своей морской провинции — свободно.
  CHECK(!moveSpendsAction(f.w(), fe, {965, 300}, {965, 500}));
  CHECK(canPlayerMove(f.w(), fe, {965, 500}, &why));
  // Очко истрачено — в неназначенном море не сдвинуться.
  f.tx([&](Tx& tx) { spendAction(tx, fl); });
  CHECK(!canPlayerMove(f.w(), fl, kSeaNorth + Vec2{50, 0}, &why));
  CHECK(has(why, "уже ходило"));
}

TEST(rules_war_loops_overloaded_fleet_only_lands) {
  Fix f;
  northSea(f);
  Id fleet = 0, army = 0;
  f.tx([&](Tx& tx) {
    const Id frig = addFleetRow(tx, f.A, ShipType::Frigate, "", 5, 0);
    const Id inf = addArmyRow(tx, f.A, UnitType::LightInf, "", 5000, 0);
    fleet = createArmy(tx, ArmyKind::Fleet, f.A, {400, 60});
    setUnits(tx, fleet, f.A, frig, 2);
    army = createArmy(tx, ArmyKind::Army, f.A, center(1));
    setUnits(tx, army, f.A, inf, 1500);
    embark(tx, army, fleet);
    tx.army(fleet).groups[0].units[0].count = 1;   // потери в бою: остался один фрегат
  });
  std::string why;
  CHECK(fleetOverloaded(f.w(), fleet, &why));
  CHECK(has(why, "перегружен: на борту 1500, вместимость 1000"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { moveArmy(tx, fleet, {600, 60}); }); }), "перегружен"));
  Encounter e = encounter(f.w(), fleet, {600, 60});
  CHECK(e.type == EncounterType::Blocked);
  CHECK(has(e.reason, "перегружен"));
  f.tx([&](Tx& tx) { land(tx, fleet, center(2)); });   // высадка — можно
  CHECK(!fleetOverloaded(f.w(), fleet));
  f.tx([&](Tx& tx) { moveArmy(tx, fleet, {600, 60}); });
  CHECK(f.w().army(fleet)->pos == Vec2(600, 60));
}

// ---------------------------------------------------------------- 5.13 проверки штурма и захвата
TEST(rules_war_loops_siege_position_and_defenders) {
  Fix f;
  Id rA = 0, rB = 0, a = 0;
  f.tx([&](Tx& tx) {
    const Id race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.B, race, 1000);
    declareWar(tx, f.A, f.B);
    rA = addArmyRow(tx, f.A, UnitType::LightInf, "", 100, 1);
    rB = addArmyRow(tx, f.B, UnitType::LightInf, "", 100, 1);
    a = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, a, f.A, rA, 50);
  });
  std::string why;
  CHECK(!canSiege(f.w(), a, f.p[1], &why));
  CHECK(has(why, "не у провинции «П1»"));
  CHECK(canSiege(f.w(), a, f.p[1], &why, center(1)));
  // Замок П1 у границы: войско у знака (ещё в П0) штурмует.
  f.tx([&](Tx& tx) {
    MapSymbol s;
    s.kind = SymbolKind::Castle;
    s.p = {310, 200};
    tx.add(s);
    moveArmy(tx, a, {260, 200});
  });
  CHECK_EQ(provinceAtPos(f.w(), f.w().army(a)->pos), f.p[0]);
  CHECK(canSiege(f.w(), a, f.p[1]));
  // Войско владельца в провинции — сначала битва с ним.
  Id b = 0;
  f.tx([&](Tx& tx) {
    b = createArmy(tx, ArmyKind::Army, f.B, center(1));
    setUnits(tx, b, f.B, rB, 10);
  });
  CHECK(!canSiege(f.w(), a, f.p[1], &why));
  CHECK(has(why, "Сначала разбейте войско «Войско №1»"));
  CHECK_EQ(assaultBlocker(f.w(), a, f.p[1]), b);
  CHECK(!captureOptions(f.w(), a, f.p[1]).can[int(Capture::Occupy)]);
  // Союзник владельца в провинции мешает, только если воюет с нападающим.
  f.tx([&](Tx& tx) {
    disband(tx, b);
    setRelation(tx, f.C, f.B, 60, RelStatus::Alliance);
    const Id rC = addArmyRow(tx, f.C, UnitType::LightInf, "", 10, 1);
    const Id c = createArmy(tx, ArmyKind::Army, f.C, center(1));
    setUnits(tx, c, f.C, rC, 10);
  });
  CHECK(canSiege(f.w(), a, f.p[1]));
  f.tx([&](Tx& tx) { declareWar(tx, f.C, f.A); });
  CHECK(!canSiege(f.w(), a, f.p[1], &why));
  CHECK(has(why, "Сначала разбейте войско"));
}

TEST(rules_war_loops_capture_checks) {
  Fix f;
  Id rA = 0, rB = 0, a = 0, a0 = 0;
  f.tx([&](Tx& tx) {
    const Id race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.B, race, 1000);
    declareWar(tx, f.A, f.B);
    rA = addArmyRow(tx, f.A, UnitType::LightInf, "", 100, 1);
    rB = addArmyRow(tx, f.B, UnitType::LightInf, "", 100, 1);
    a = createArmy(tx, ArmyKind::Army, f.A, center(1));
    setUnits(tx, a, f.A, rA, 50);
    a0 = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, a0, f.A, rA, 10);
    setGarrison(tx, f.p[1], rB, 5);
  });
  // Свою провинцию не захватывают и не разоряют.
  CaptureOptions o = captureOptions(f.w(), a0, f.p[0]);
  for (int i = 0; i < int(Capture::Count); i++) {
    CHECK(!o.can[i]);
    CHECK(has(o.why[i], "самого государства"));
  }
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { capture(tx, a0, f.p[0], Capture::Raze); }); }), "самого государства"));
  // Гарнизон — только штурмом.
  o = captureOptions(f.w(), a, f.p[1]);
  CHECK(!o.can[int(Capture::Occupy)]);
  CHECK(has(o.why[int(Capture::Occupy)], "Сначала разбейте гарнизон"));
  f.tx([&](Tx& tx) { setGarrison(tx, f.p[1], rB, 0); });
  CHECK(captureOptions(f.w(), a, f.p[1]).can[int(Capture::Occupy)]);
  // Чужую оккупацию не перехватывают.
  f.tx([&](Tx& tx) {
    declareWar(tx, f.C, f.B);
    setOccupied(tx, f.p[1], f.C);
  });
  std::string why;
  CHECK(!canSiege(f.w(), a, f.p[1], &why));
  CHECK(has(why, "оккупирована «Церис»"));
  CHECK(!captureOptions(f.w(), a, f.p[1]).can[int(Capture::Raze)]);
}
