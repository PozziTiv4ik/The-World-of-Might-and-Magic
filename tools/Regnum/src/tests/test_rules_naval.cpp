// Regnum — правила флота (ТЗ «Доработки №3», п.3, 6; «Доработки №4», п.9): вместимость, посадка и высадка, войско на
// борту на карте и в бою, обмен отрядами, союзные объекты и «Вернуть войско», флот у верфи.
#include "rules/internal.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;
using rg::rules::detail::contains;

namespace {

// Мир тестов с морской провинцией «Северное море» над верхним рядом провинций (p0…p3 — у её берега), фракция A:
// строки фрегатов, линкоров и пехоты, флот в северном море и войско в p1.
struct Naval : Fix {
  Id north = 0, frig = 0, line = 0, inf = 0, inf2 = 0, fleet = 0, army = 0, hero = 0;
  const Vec2 sea{400, 60};
  Naval() {
    tx([&](Tx& t) { north = rules::createProvince(t, rect(60, 20, 940, 99), Terrain::Sea).province; });
    tx([&](Tx& t) {
      frig = addFleetRow(t, A, ShipType::Frigate, "", 10, 0);
      line = addFleetRow(t, A, ShipType::ShipOfLine, "", 2, 0);
      inf = addArmyRow(t, A, UnitType::LightInf, "", 20000, 0);
      inf2 = addArmyRow(t, A, UnitType::HeavyInf, "", 5000, 0);
      fleet = createArmy(t, ArmyKind::Fleet, A, sea);
      setUnits(t, fleet, A, frig, 2);   // вместимость 2 000
      army = createArmy(t, ArmyKind::Army, A, center(1));
      setUnits(t, army, A, inf, 1500);
      hero = createCharacter(t, A, "Адмирал");
      setHero(t, army, hero, true);
      setCommander(t, army, hero);
    });
  }
};

i64 unitsIn(const World& w, Id army, Id faction, Id row) {
  i64 n = 0;
  if (const Army* a = w.army(army))
    for (const ArmyGroup& g : a->groups)
      if (g.faction == faction)
        for (const ArmyUnit& u : g.units)
          if (u.row == row) n += u.count;
  return n;
}

}  // namespace

// ---------------------------------------------------------------- вместимость
TEST(rules_naval_capacity) {
  Naval f;
  CHECK_EQ(shipCapacity(f.w(), ShipType::Frigate), i64(1000));
  CHECK_EQ(shipCapacity(f.w(), ShipType::ShipOfLine), i64(5000));
  CHECK_EQ(shipCapacity(f.w(), ShipType::Galleon), i64(0));
  CHECK_EQ(shipCapacity(f.w(), ShipType::SeaMonster), i64(0));
  CHECK_EQ(fleetCapacity(f.w(), f.fleet), i64(2000));
  f.tx([&](Tx& tx) { setUnits(tx, f.fleet, f.A, f.line, 1); });
  CHECK_EQ(fleetCapacity(f.w(), f.fleet), i64(7000));
  // Константа вместимости правится.
  f.tx([&](Tx& tx) { ensureConstant(tx, schema::cst::FrigateCapacity).num = 1500; });
  CHECK_EQ(fleetCapacity(f.w(), f.fleet), i64(8000));
  CHECK_EQ(armySize(f.w(), f.army), i64(1500));
  CHECK_EQ(fleetCapacity(f.w(), f.army), i64(0));   // у войска вместимости нет
}

// ---------------------------------------------------------------- посадка
TEST(rules_naval_embark_rules) {
  Naval f;
  // Встреча войска с флотом своего государства — посадка.
  Encounter e = encounter(f.w(), f.army, f.sea);
  CHECK(e.type == EncounterType::Embark);
  CHECK_EQ(e.target, f.fleet);
  // Флот, наведённый на войско своего государства, забирает его на борт.
  e = encounter(f.w(), f.fleet, f.w().army(f.army)->pos);
  CHECK(e.type == EncounterType::Embark);
  CHECK_EQ(e.target, f.army);
  // Больше вместимости — нельзя.
  f.tx([&](Tx& tx) { setUnits(tx, f.army, f.A, f.inf, 2500); });
  std::string why;
  CHECK(!canEmbark(f.w(), f.army, f.fleet, &why));
  CHECK(has(why, "вместимости"));
  e = encounter(f.w(), f.army, f.sea);
  CHECK(e.type == EncounterType::Blocked);
  CHECK(has(e.reason, "вместимости"));
  f.tx([&](Tx& tx) { setUnits(tx, f.army, f.A, f.inf, 1500); });
  // Флот в морской провинции, не соседней с провинцией войска, — нельзя (у p1 есть соседняя морская провинция).
  Id far = 0;
  f.tx([&](Tx& tx) {
    far = createArmy(tx, ArmyKind::Fleet, f.A, kSeaWest);
    setUnits(tx, far, f.A, f.frig, 2);
  });
  CHECK(!canEmbark(f.w(), f.army, far, &why));
  CHECK(has(why, "Северн") || has(why, "морской провинции"));
  // У p5 соседних морских провинций нет: флот недалеко от неё (неназначенное море у южного берега) — можно, далеко — нет.
  Id south = 0, a5 = 0;
  f.tx([&](Tx& tx) {
    south = createArmy(tx, ArmyKind::Fleet, f.A, {400, 550});
    setUnits(tx, south, f.A, f.frig, 1);
    a5 = createArmy(tx, ArmyKind::Army, f.A, center(5));
    setUnits(tx, a5, f.A, f.inf, 500);
  });
  CHECK(seaNeighbors(f.w(), f.p[5]).empty());
  CHECK(canEmbark(f.w(), a5, south, &why));
  CHECK(!canEmbark(f.w(), a5, f.fleet, &why));
  CHECK(has(why, "далеко"));
  // Чужой флот — не посадка.
  Id bf = 0;
  f.tx([&](Tx& tx) {
    Id brow = addFleetRow(tx, f.B, ShipType::Frigate, "", 5, 0);
    bf = createArmy(tx, ArmyKind::Fleet, f.B, {700, 60});
    setUnits(tx, bf, f.B, brow, 3);
  });
  CHECK(!canEmbark(f.w(), f.army, bf, &why));
  CHECK(encounter(f.w(), f.army, f.w().army(bf)->pos).type == EncounterType::Blocked);
  CHECK(encounter(f.w(), bf, f.w().army(f.army)->pos).type == EncounterType::Blocked);
}

TEST(rules_naval_embark_and_carry) {
  Naval f;
  const Vec2 was = f.w().army(f.army)->pos;
  f.tx([&](Tx& tx) { embark(tx, f.army, f.fleet); });
  const World& w = f.w();
  CHECK_EQ(cargoOf(w, f.fleet), f.army);
  CHECK_EQ(carrierOf(w, f.army), f.fleet);
  CHECK(w.army(f.army)->pos == f.sea);
  CHECK(has(lastLog(w), "на борту"));
  // Войско на борту на карте не стоит: место у провинции свободно, флот — единственный объект в точке.
  CHECK(validPosition(w, ArmyKind::Army, was));
  CHECK_EQ(armyAt(w, f.sea), f.fleet);
  CHECK_EQ(f.fc(f.A).army[0].field, i64(1500));   // отряды на борту — в поле
  // Войско на борту не перемещается и не делится; флот везёт его.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { moveArmy(tx, f.army, center(2)); }); }), "высадите"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { splitArmy(tx, f.army, SplitSpec{{{{f.A, f.inf}, 100}}, {}}); }); }), "высадите"));
  f.tx([&](Tx& tx) { moveArmy(tx, f.fleet, {600, 60}); });
  CHECK(f.w().army(f.army)->pos == Vec2(600, 60));
  // Войско на борту — не больше вместимости; флот не теряет нужную вместимость.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setUnits(tx, f.army, f.A, f.inf, 2100); }); }), "поместится"));
  f.tx([&](Tx& tx) { setUnits(tx, f.army, f.A, f.inf, 2000); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setUnits(tx, f.fleet, f.A, f.frig, 1); }); }), "вместимость"));
  // Флот с войском на борту не расформировывается; второе войско — обмен, а не посадка.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { disband(tx, f.fleet); }); }), "высадите"));
  Id other = 0;
  f.tx([&](Tx& tx) {
    other = createArmy(tx, ArmyKind::Army, f.A, center(2));
    setUnits(tx, other, f.A, f.inf2, 100);
  });
  std::string why;
  CHECK(!canEmbark(f.w(), other, f.fleet, &why));
  CHECK(has(why, "уже есть войско"));
  CHECK(canBoardExchange(f.w(), other, f.fleet));
  const Encounter e = encounter(f.w(), other, f.w().army(f.fleet)->pos);
  CHECK(e.type == EncounterType::Embark);
  CHECK(has(e.reason, "Обмен"));
  // Мятеж и штурм — только после высадки.
  f.tx([&](Tx& tx) {
    tx.army(f.army).loyalty = -50;
    tx.army(f.fleet).loyalty = -50;
  });
  CHECK(!canMutiny(f.w(), f.army, &why));
  CHECK(!canMutiny(f.w(), f.fleet, &why));
  CHECK(!canSiege(f.w(), f.army, f.p[2], &why));
  // Расформировать войско на борту — флот свободен.
  f.tx([&](Tx& tx) { disband(tx, f.army); });
  CHECK_EQ(f.w().army(f.fleet)->cargo, Id(0));
}

// ---------------------------------------------------------------- высадка
TEST(rules_naval_landing) {
  Naval f;
  f.tx([&](Tx& tx) { embark(tx, f.army, f.fleet); });
  // Флот в северном море: высадка в провинции у его берега.
  CHECK((landingProvinces(f.w(), f.fleet) == std::vector<Id>{f.p[0], f.p[1], f.p[2], f.p[3]}));
  std::string why;
  CHECK(canLand(f.w(), f.fleet, center(3), &why));
  CHECK(!canLand(f.w(), f.fleet, center(6), &why));
  CHECK(has(why, "не граничит"));
  CHECK(!canLand(f.w(), f.fleet, {400, 60}, &why));   // море
  Id landed = 0;
  f.tx([&](Tx& tx) { landed = land(tx, f.fleet, center(2)); });
  CHECK_EQ(landed, f.army);
  CHECK_EQ(cargoOf(f.w(), f.fleet), Id(0));
  CHECK_EQ(carrierOf(f.w(), f.army), Id(0));
  CHECK(f.w().army(f.army)->pos == center(2));
  CHECK(has(lastLog(f.w()), "высажено"));
  CHECK(!canLand(f.w(), f.fleet, center(1), &why));   // на борту никого
  // Флот вне морской провинции: суша недалеко от флота.
  Id south = 0, a5 = 0;
  f.tx([&](Tx& tx) {
    south = createArmy(tx, ArmyKind::Fleet, f.A, {400, 550});
    setUnits(tx, south, f.A, f.frig, 1);
    a5 = createArmy(tx, ArmyKind::Army, f.A, center(5));
    setUnits(tx, a5, f.A, f.inf, 500);
    embark(tx, a5, south);
  });
  CHECK(landingProvinces(f.w(), south).empty());
  CHECK(canLand(f.w(), south, {420, 460}, &why));
  CHECK(!canLand(f.w(), south, center(0), &why));
  CHECK(has(why, "далеко"));
}

// ---------------------------------------------------------------- бой
TEST(rules_naval_fleet_sunk_with_army) {
  Naval f;
  Id bf = 0, brow = 0;
  f.tx([&](Tx& tx) {
    embark(tx, f.army, f.fleet);
    brow = addFleetRow(tx, f.B, ShipType::Frigate, "", 5, 0);
    bf = createArmy(tx, ArmyKind::Fleet, f.B, {700, 60});
    setUnits(tx, bf, f.B, brow, 5);
    declareWar(tx, f.A, f.B);
  });
  // Потеря части кораблей: перегруз (вместимость меньше войска) допустим.
  BattleResult r;
  r.attacker = bf;
  r.defender = f.fleet;
  r.attackerWins = true;
  r.attackerOrigin = {700, 60};
  r.losses[f.fleet][{f.A, f.frig}] = 1;
  f.tx([&](Tx& tx) { resolveBattle(tx, r); });
  CHECK_EQ(cargoOf(f.w(), f.fleet), f.army);
  CHECK(fleetCapacity(f.w(), f.fleet) < armySize(f.w(), f.army));
  CHECK(f.w().army(f.army)->pos == f.w().army(f.fleet)->pos);   // проигравший флот отошёл вместе с войском
  // Флот уничтожен: войско на борту гибнет, его герои — в «Судьбу героев».
  r.attackerOrigin = f.w().army(bf)->pos;
  r.losses.clear();
  r.losses[f.fleet][{f.A, f.frig}] = 1;
  BattleOutcome out;
  f.tx([&](Tx& tx) { out = resolveBattle(tx, r); });
  CHECK(!f.w().army(f.fleet));
  CHECK(!f.w().army(f.army));
  CHECK(contains(out.fallenHeroes, f.hero));
  CHECK_EQ(f.w().faction(f.A)->armyRow(f.inf)->total, i64(20000 - 1500));
  CHECK(has(lastLog(f.w(), "погибло"), "войско на борту"));
}

// ---------------------------------------------------------------- обмен отрядами
TEST(rules_naval_exchange) {
  Naval f;
  Id a2 = 0, h2 = 0;
  f.tx([&](Tx& tx) {
    a2 = createArmy(tx, ArmyKind::Army, f.A, center(2));
    setUnits(tx, a2, f.A, f.inf, 50);
    setUnits(tx, a2, f.A, f.inf2, 30);
    h2 = createCharacter(tx, f.A, "Капитан");
    setHero(tx, a2, h2, true);
  });
  CHECK(canExchange(f.w(), f.army, a2));
  ExchangeSpec s;
  s.first[{f.A, f.inf}] = 1520;
  s.first[{f.A, f.inf2}] = 10;
  s.heroesFirst = {h2};
  CHECK(exchangeProblems(f.w(), f.army, a2, s).empty());
  f.tx([&](Tx& tx) { exchangeUnits(tx, f.army, a2, s); });
  const World& w = f.w();
  CHECK_EQ(unitsIn(w, f.army, f.A, f.inf), i64(1520));
  CHECK_EQ(unitsIn(w, f.army, f.A, f.inf2), i64(10));
  CHECK_EQ(unitsIn(w, a2, f.A, f.inf), i64(30));
  CHECK_EQ(unitsIn(w, a2, f.A, f.inf2), i64(20));
  // Полководец следует за героем; без полководца — первый герой.
  CHECK_EQ(w.army(a2)->commander, f.hero);
  CHECK_EQ(w.army(f.army)->commander, h2);
  CHECK(has(lastLog(w), "Обмен отрядами"));
  // Больше, чем есть, — нельзя.
  ExchangeSpec bad;
  bad.first[{f.A, f.inf}] = 9999;
  bad.heroesFirst = {h2};
  CHECK(!exchangeProblems(f.w(), f.army, a2, bad).empty());
  // Всё в одно войско — второе исчезает.
  ExchangeSpec all;
  all.first[{f.A, f.inf}] = 1550;
  all.first[{f.A, f.inf2}] = 30;
  all.heroesFirst = {h2, f.hero};
  f.tx([&](Tx& tx) { exchangeUnits(tx, f.army, a2, all); });
  CHECK(!f.w().army(a2));
  CHECK_EQ(armySize(f.w(), f.army), i64(1580));
  // Чужие отряды не переходят; флот с войском — разные виды.
  Id bA = 0;
  f.tx([&](Tx& tx) {
    Id row = addArmyRow(tx, f.B, UnitType::LightInf, "", 100, 0);
    bA = createArmy(tx, ArmyKind::Army, f.B, center(6));
    setUnits(tx, bA, f.B, row, 10);
  });
  CHECK(!canExchange(f.w(), f.army, bA));
  CHECK(!canExchange(f.w(), f.army, f.fleet));
}

TEST(rules_naval_exchange_aboard_capacity) {
  Naval f;
  Id shore = 0;
  f.tx([&](Tx& tx) {
    embark(tx, f.army, f.fleet);   // 1 500 на борту, вместимость 2 000
    shore = createArmy(tx, ArmyKind::Army, f.A, center(1));
    setUnits(tx, shore, f.A, f.inf, 1000);
  });
  ExchangeSpec s;
  s.first[{f.A, f.inf}] = 0;            // на берегу — никого
  s.heroesFirst = {};
  CHECK(!exchangeProblems(f.w(), shore, f.army, s).empty());   // на борту стало бы 2 500
  s.first[{f.A, f.inf}] = 600;          // на борту 1 900
  s.heroesFirst = {f.hero};             // герой — на берег
  CHECK(exchangeProblems(f.w(), shore, f.army, s).empty());
  f.tx([&](Tx& tx) { exchangeUnits(tx, shore, f.army, s); });
  CHECK_EQ(armySize(f.w(), f.army), i64(1900));
  CHECK_EQ(cargoOf(f.w(), f.fleet), f.army);
  CHECK_EQ(f.w().army(f.army)->commander, Id(0));   // герой ушёл на берег
  CHECK_EQ(f.w().army(shore)->commander, f.hero);
  // Флот с войском на борту: обмен кораблями не оставляет его без нужной вместимости.
  Id f2 = 0;
  f.tx([&](Tx& tx) {
    f2 = createArmy(tx, ArmyKind::Fleet, f.A, {700, 60});
    setUnits(tx, f2, f.A, f.frig, 1);
  });
  ExchangeSpec ships;
  ships.first[{f.A, f.frig}] = 1;       // у флота с войском останется 1 фрегат
  CHECK(!exchangeProblems(f.w(), f.fleet, f2, ships).empty());
  ships.first[{f.A, f.frig}] = 3;
  f.tx([&](Tx& tx) { exchangeUnits(tx, f.fleet, f2, ships); });
  CHECK_EQ(fleetCapacity(f.w(), f.fleet), i64(3000));
  CHECK(!f.w().army(f2));   // флот без кораблей и героев исчез
}

// ---------------------------------------------------------------- объединение флотов с войсками на борту
TEST(rules_naval_merge_fleets_with_cargo) {
  Naval f;
  Id f2 = 0, a2 = 0;
  f.tx([&](Tx& tx) {
    embark(tx, f.army, f.fleet);
    f2 = createArmy(tx, ArmyKind::Fleet, f.A, {600, 60});
    setUnits(tx, f2, f.A, f.frig, 2);
    a2 = createArmy(tx, ArmyKind::Army, f.A, center(2));
    setUnits(tx, a2, f.A, f.inf2, 700);
    embark(tx, a2, f2);
  });
  // Флот с войском присоединяется к флоту с войском: корабли и войска на борту складываются.
  f.tx([&](Tx& tx) { mergeArmies(tx, f.fleet, f2); });
  CHECK(!f.w().army(f2));
  CHECK(!f.w().army(a2));
  CHECK_EQ(cargoOf(f.w(), f.fleet), f.army);
  CHECK_EQ(unitsIn(f.w(), f.army, f.A, f.inf2), i64(700));
  CHECK_EQ(fleetCapacity(f.w(), f.fleet), i64(4000));
  // Флот без войска присоединяет флот с войском — войско переходит.
  Id f3 = 0;
  f.tx([&](Tx& tx) {
    f3 = createArmy(tx, ArmyKind::Fleet, f.A, {800, 60});
    setUnits(tx, f3, f.A, f.frig, 1);
  });
  f.tx([&](Tx& tx) { mergeArmies(tx, f3, f.fleet); });
  CHECK_EQ(cargoOf(f.w(), f3), f.army);
  CHECK(f.w().army(f.army)->pos == f.w().army(f3)->pos);
}

// ---------------------------------------------------------------- союзное войско: управление и «Вернуть войско»
TEST(rules_naval_allied_return_group) {
  Naval f;
  Id cRow = 0, aC = 0, hC = 0, hA2 = 0, a2 = 0;
  f.tx([&](Tx& tx) {
    setRelation(tx, f.A, f.C, 80, RelStatus::Alliance);
    cRow = addArmyRow(tx, f.C, UnitType::LightInf, "", 500, 0);
    aC = createArmy(tx, ArmyKind::Army, f.C, center(5));
    setUnits(tx, aC, f.C, cRow, 300);
    hC = createCharacter(tx, f.C, "Герой Ц");
    setHero(tx, aC, hC, true);
    a2 = createArmy(tx, ArmyKind::Army, f.A, center(6));
    setUnits(tx, a2, f.A, f.inf, 400);
    hA2 = createCharacter(tx, f.A, "Герой А");
    setHero(tx, a2, hA2, true);
    setCommander(tx, a2, hA2);
  });
  // Государство A наводит войско на войско союзника C: управление — у C (его группа первая, полководец — его герой).
  CHECK(encounter(f.w(), a2, center(5)).type == EncounterType::Alliance);
  f.tx([&](Tx& tx) { formAllied(tx, aC, a2); });
  const Army& al = *f.w().army(aC);
  CHECK(al.allied());
  CHECK_EQ(al.leader(), f.C);
  CHECK_EQ(al.commander, hC);
  CHECK_EQ(al.groups[1].faction, f.A);
  std::string why;
  CHECK(!canReturnGroup(f.w(), aC, f.C, &why));   // лидер не уходит
  CHECK(canReturnGroup(f.w(), aC, f.A, &why));
  // «Вернуть войско»: группа A — отдельным войском рядом с отрядами и героем.
  Id back = 0;
  f.tx([&](Tx& tx) { back = returnGroup(tx, aC, f.A); });
  const World& w = f.w();
  CHECK(!w.army(aC)->allied());
  CHECK(w.army(aC)->name != "Союзное войско");
  CHECK_EQ(w.army(back)->leader(), f.A);
  CHECK_EQ(unitsIn(w, back, f.A, f.inf), i64(400));
  CHECK(contains(w.army(back)->groups[0].heroes, hA2));
  CHECK_EQ(w.army(back)->commander, hA2);
  CHECK(dist(w.army(back)->pos, w.army(aC)->pos) < 400);
  CHECK(validPosition(w, ArmyKind::Army, w.army(back)->pos, back));
  CHECK(has(lastLog(w), "возвращает"));
}

TEST(rules_naval_allied_fleet_return_with_cargo) {
  Naval f;
  Id cRow = 0, fC = 0;
  f.tx([&](Tx& tx) {
    setRelation(tx, f.A, f.C, 80, RelStatus::Alliance);
    cRow = addFleetRow(tx, f.C, ShipType::ShipOfLine, "", 3, 0);
    fC = createArmy(tx, ArmyKind::Fleet, f.C, {700, 60});
    setUnits(tx, fC, f.C, cRow, 1);
    embark(tx, f.army, f.fleet);
    formAllied(tx, fC, f.fleet);   // флот A с войском — к флоту C
  });
  CHECK(!f.w().army(f.fleet));
  CHECK_EQ(cargoOf(f.w(), fC), f.army);
  CHECK_EQ(fleetCapacity(f.w(), fC), i64(7000));
  Id back = 0;
  f.tx([&](Tx& tx) { back = returnGroup(tx, fC, f.A); });
  CHECK_EQ(cargoOf(f.w(), back), f.army);   // войско A уходит с кораблями A
  CHECK_EQ(cargoOf(f.w(), fC), Id(0));
  CHECK(f.w().army(f.army)->pos == f.w().army(back)->pos);
}

TEST(rules_naval_dissolve_aboard_allied_army) {
  Naval f;
  Id cRow = 0, aC = 0, fC = 0;
  f.tx([&](Tx& tx) {
    setRelation(tx, f.A, f.C, 80, RelStatus::Alliance);
    cRow = addArmyRow(tx, f.C, UnitType::LightInf, "", 500, 0);
    aC = createArmy(tx, ArmyKind::Army, f.C, center(2));
    setUnits(tx, aC, f.C, cRow, 200);
    formAllied(tx, f.army, aC);
    Id cf = addFleetRow(tx, f.C, ShipType::Frigate, "", 2, 0);
    fC = createArmy(tx, ArmyKind::Fleet, f.C, {700, 60});
    setUnits(tx, fC, f.C, cf, 1);
  });
  // Союзное войско садится только на флот, где есть все его фракции.
  CHECK(!canEmbark(f.w(), f.army, f.fleet));
  f.tx([&](Tx& tx) {
    formAllied(tx, f.fleet, fC);
    embark(tx, f.army, f.fleet);
  });
  CHECK(!canReturnGroup(f.w(), f.army, f.C));   // на борту — сначала высадка
  // Союз распался: союзник высаживается на ближайший берег, войско лидера остаётся на борту.
  f.tx([&](Tx& tx) { declareWar(tx, f.A, f.C); });
  CHECK_EQ(cargoOf(f.w(), f.fleet), f.army);
  CHECK(!f.w().army(f.army)->allied());
  Id landed = 0;
  f.w().armies.each([&](const Army& a) {
    if (a.leader() == f.C && !a.isFleet()) landed = a.id;
  });
  CHECK(landed != 0);
  CHECK(validPosition(f.w(), ArmyKind::Army, f.w().army(landed)->pos, landed));
}

// ---------------------------------------------------------------- флот у верфи
TEST(rules_naval_place_fleet_at_shipyard) {
  Naval f;
  Id yard = 0;
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 8; i++) tx.province(f.p[i]).owner = f.A;
    tx.province(f.p[3]).owner = f.B;
    yard = createBuilding(tx, 0, "Верфь");
    setBuildingFlag(tx, yard, BuildingFlag::Shipyard, true);
    setLevelShips(tx, yard, 1, 1u << int(ShipType::Frigate));
    placeBuilding(tx, f.p[1], yard, 1);
    placeBuilding(tx, f.p[3], yard, 1);   // верфь чужой провинции
  });
  CHECK((shipyardProvinces(f.w(), f.A) == std::vector<Id>{f.p[1]}));
  CHECK((shipyardProvinces(f.w(), f.B) == std::vector<Id>{f.p[3]}));
  Id fl = 0;
  f.tx([&](Tx& tx) { fl = placeFleet(tx, f.A, f.p[1]); });
  const Army& a = *f.w().army(fl);
  CHECK(a.isFleet());
  CHECK_EQ(a.leader(), f.A);
  CHECK(validPosition(f.w(), ArmyKind::Fleet, a.pos, fl));
  CHECK(a.pos.x > 290 && a.pos.x < 510 && a.pos.y < 100);   // у северного берега p1
  // Второй флот — рядом на свободном месте.
  Id fl2 = 0;
  f.tx([&](Tx& tx) { fl2 = placeFleet(tx, f.A, f.p[1]); });
  CHECK(dist(f.w().army(fl2)->pos, a.pos) >= schema::kInteractDist - 1e-6);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { placeFleet(tx, f.A, f.p[5]); }); }), "верфи"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { placeFleet(tx, f.A, f.p[3]); }); }), "не провинция"));
}
