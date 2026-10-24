// Regnum — замкнутые петли экономики (аудит 2026-10-23, п. 4.1–4.13, 11): долг казны («Невыплата жалованья») и
// эссенций (распад элементалей), недовольство налогом, голод без накопления недостачи и гибель жителей, провизия для
// воинов, оккупация и постройки, множитель производства у нового владельца и возврат входа прерванного цикла, доход
// торговых галеонов, естественный прирост и предел населения, пустошь без дохода гильдий, рабы сверх лимита, снос
// построек не больше потерянных слотов, золото до тысячных, маршруты через оккупацию и войну, лимит исследований.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

Id resNamed(const World& w, const char* n) { return test::idByName(w.catalogs->resources, n); }
Id essNamed(const World& w, const char* n) { return test::idByName(w.catalogs->essences, n); }

void turn(Fix& f) {
  f.tx([](Tx& tx) { endTurn(tx); });
}

// Константа мира (в базовом мире тестов прирост и гибель от голода могут быть выключены — тесты задают их сами).
void setConst(Tx& tx, const char* key, double v) { ensureConstant(tx, key).num = v; }

bool hasAuto(const World& w, Id state, const char* key) {
  for (const AutoMod& a : autoModifiers(w, state))
    if (a.key == key) return true;
  return false;
}

}  // namespace

// ---------------------------------------------------------------- 4.1 долг казны и эссенций
TEST(rules_econ_loops_unpaid_salary) {
  Fix f;
  Id row = 0, army = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    setFx(tx, schema::mod::Unpaid, {{Fx::LoyaltyPerTurn, -5}, {Fx::ContentmentPerTurn, -2}});
    row = addArmyRow(tx, f.A, UnitType::LightInf, "", 100, 0.05);   // содержание 5 за ход
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, army, f.A, row, 10);
  });
  CHECK(!f.fc(f.A).unpaid);
  CHECK(!hasAuto(f.w(), f.A, schema::mod::Unpaid));
  turn(f);
  CHECK_NEAR(f.w().faction(f.A)->treasury(), -5, 1e-9);
  // Казна в долгу: «Невыплата жалованья» ставится сама и действует со следующего хода.
  CHECK(f.fc(f.A).unpaid);
  CHECK(hasAuto(f.w(), f.A, schema::mod::Unpaid));
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::LoyaltyPerTurn], -5, 1e-9);
  CHECK_NEAR(f.pc(f.p[0]).contentDelta, -2, 1e-9);
  CHECK_NEAR(f.w().army(army)->loyalty, 100, 1e-9);
  turn(f);
  CHECK_NEAR(f.w().army(army)->loyalty, 95, 1e-9);
  CHECK_NEAR(f.w().province(f.p[0])->contentment, -2, 1e-9);
  // Долг погашен — модификатор снимается сам; вручную его не назначить.
  f.tx([&](Tx& tx) { tx.faction(f.A).res[kGold] = 100; });
  CHECK(!f.fc(f.A).unpaid);
  CHECK(!hasAuto(f.w(), f.A, schema::mod::Unpaid));
  CHECK(!errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Faction, f.A, ensureBuiltinMod(tx, schema::mod::Unpaid)); }); }).empty());
}

TEST(rules_econ_loops_essence_debt_decay) {
  Fix f;
  const Id fire = essNamed(f.w(), "Эссенция пламени"), water = essNamed(f.w(), "Эссенция воды");
  Id row = 0, other = 0, army = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    row = addArmyRow(tx, f.A, UnitType::Elementals, "Саламандры", 25, 0);
    setRowEssUpkeep(tx, f.A, row, fire, 1);
    other = addArmyRow(tx, f.A, UnitType::Elementals, "Ундины", 10, 0);   // эссенции воды хватает
    setRowEssUpkeep(tx, f.A, other, water, 1);
    tx.faction(f.A).ess[water] = 100;
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, army, f.A, row, 24);
  });
  // Эссенции пламени нет: содержание 25 уводит запас в −25, саламандры теряют ⌈2,5⌉ = 3 — 1 из резерва, 2 из войска.
  turn(f);
  CHECK_NEAR(f.w().faction(f.A)->essence(fire), -25, 1e-9);
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->total, i64(22));
  CHECK_EQ(unitsIn(f.w(), army, f.A, row), i64(22));
  CHECK_EQ(reserveOf(f.w(), f.A, row), i64(0));
  CHECK_EQ(f.w().faction(f.A)->armyRow(other)->total, i64(10));
  CHECK(has(lastLog(f.w(), "распад элементалей"), "«Саламандры» −3"));
  // Долг держится — распад каждый ход (⌈2,2⌉ = 3, уже из войска).
  turn(f);
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->total, i64(19));
  CHECK_EQ(unitsIn(f.w(), army, f.A, row), i64(19));
  // Долг покрыт — распада нет.
  f.tx([&](Tx& tx) { tx.faction(f.A).ess[fire] = 1000; });
  turn(f);
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->total, i64(19));
}

// ---------------------------------------------------------------- 4.2 налог
TEST(rules_econ_loops_tax_unrest) {
  Fix f;
  f.tx([&](Tx& tx) {
    setConst(tx, schema::cst::TaxNorm, 10);
    setConst(tx, schema::cst::TaxUnrest, 0.2);
    tx.province(f.p[0]).owner = f.A;
    tx.province(f.p[1]).owner = f.A;
    tx.province(f.p[1]).localTax = -15;   // местное послабление: общий налог 15 %
    tx.province(f.p[2]).localTax = 50;    // без владельца налог не собирают
    tx.faction(f.A).tax = 30;
  });
  CHECK_NEAR(f.pc(f.p[0]).taxUnrest, -4, 1e-9);   // −0,2 × (30 − 10)
  CHECK_NEAR(f.pc(f.p[0]).contentDelta, -4, 1e-9);
  CHECK_NEAR(f.pc(f.p[1]).taxUnrest, -1, 1e-9);
  CHECK_NEAR(f.pc(f.p[2]).taxUnrest, 0, 1e-9);
  turn(f);
  CHECK_NEAR(f.w().province(f.p[0])->contentment, -4, 1e-9);
  CHECK_NEAR(f.w().province(f.p[1])->contentment, -1, 1e-9);
  CHECK_NEAR(f.w().province(f.p[2])->contentment, 0, 1e-9);
  // Налог по норме — недовольства нет.
  f.tx([&](Tx& tx) { tx.faction(f.A).tax = 10; });
  CHECK_NEAR(f.pc(f.p[0]).taxUnrest, 0, 1e-9);
}

// ---------------------------------------------------------------- 4.3 голод
TEST(rules_econ_loops_famine_deaths) {
  Fix f;
  Id race = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    setConst(tx, schema::cst::FamineDeaths, 10);
    setConst(tx, schema::cst::PopGrowthBase, 0.5);
    own(tx, f.p[0], f.A, race, 6000);
    own(tx, f.p[1], f.A, race, 4000);
    tx.province(f.p[0]).modifiers = {makeMod(tx, {{Fx::PopGrowthPct, 5}})};   // при голоде не растёт и с приростом
  });
  // Провизии нет: расход 10, недостача 10 — голодают 10 000, гибнет 10 % — 1000, поровну по провинциям.
  CHECK_NEAR(f.fc(f.A).provisionNeed, 10, 1e-9);
  CHECK_NEAR(f.fc(f.A).provisionDebtNext, 10, 1e-9);
  CHECK_EQ(f.fc(f.A).famineDeaths, i64(1000));
  CHECK_NEAR(f.pc(f.p[0]).growthPct, 0, 1e-12);
  turn(f);
  CHECK_NEAR(f.w().faction(f.A)->provisionDebt, 10, 1e-9);
  CHECK(f.fc(f.A).famine);
  CHECK_EQ(popOf(f.w(), f.p[0]), i64(5500));
  CHECK_EQ(popOf(f.w(), f.p[1]), i64(3500));
  CHECK(has(lastLog(f.w(), "провизия закончилась"), "погибло жителей"));
  // Недостача не копится: следующий ход — только своя (9), гибнет 900.
  CHECK_NEAR(f.fc(f.A).provisionNeed, 9, 1e-9);
  CHECK_EQ(f.fc(f.A).famineDeaths, i64(900));
  turn(f);
  CHECK_NEAR(f.w().faction(f.A)->provisionDebt, 9, 1e-9);
  CHECK_EQ(popOf(f.w(), f.p[0]) + popOf(f.w(), f.p[1]), i64(8100));
  // Провизии хватает — голод кончается, прирост возвращается.
  const Id grain = provisionResources(f.w()).front();
  f.tx([&](Tx& tx) { tx.faction(f.A).res[grain] = 100; });
  CHECK_NEAR(f.fc(f.A).provisionDebtNext, 0, 1e-12);
  CHECK_EQ(f.fc(f.A).famineDeaths, i64(0));
  CHECK(f.pc(f.p[0]).growthPct > 5);
  const i64 before = popOf(f.w(), f.p[0]);
  turn(f);
  CHECK_NEAR(f.w().faction(f.A)->provisionDebt, 0, 1e-12);
  CHECK(!f.fc(f.A).famine);
  CHECK(popOf(f.w(), f.p[0]) > before);
  CHECK(has(lastLog(f.w(), "голод закончился"), "покрыта"));
}

// ---------------------------------------------------------------- 4.4 воины едят
TEST(rules_econ_loops_soldiers_eat) {
  Fix f;
  Id inf = 0;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.A, addRace(tx), 1000);
    inf = addArmyRow(tx, f.A, UnitType::LightInf, "Пехота", 4000, 0);
    const Id skel = addArmyRow(tx, f.A, UnitType::HeavyInf, "Скелеты", 500, 0);
    findById(tx.faction(f.A).army, skel)->race = schema::kRaceUndead;
    addArmyRow(tx, f.A, UnitType::Machines, "Баллисты", 300, 0);
    addArmyRow(tx, f.A, UnitType::Elementals, "Саламандры", 200, 0);
    addArmyRow(tx, f.A, UnitType::Beasts, "Волки", 100, 0);
    Formation q;
    q.row = inf;
    q.count = 50;
    q.left = 2;
    q.people = 50;
    tx.faction(f.A).forming.push_back(q);
  });
  // Едят жители, пехота (с формированием) и звери; нежить, механизмы и элементали — нет.
  CHECK_EQ(f.fc(f.A).provisionSoldiers, i64(4000 + 50 + 100));
  CHECK_NEAR(f.fc(f.A).provisionNeed, (1000 + 4150) * 0.001, 1e-9);
  // Государство нежити провизию не тратит.
  f.tx([&](Tx& tx) { setStateKind(tx, f.A, StateKind::Undead); });
  CHECK_EQ(f.fc(f.A).provisionSoldiers, i64(0));
  CHECK_NEAR(f.fc(f.A).provisionNeed, 0, 1e-12);
}

// ---------------------------------------------------------------- 4.5 оккупация
TEST(rules_econ_loops_occupation_buildings) {
  Fix f;
  const Id iron = 5, coal = resNamed(f.w(), "Уголь"), steel = resNamed(f.w(), "Сталь"), fire = essNamed(f.w(), "Эссенция пламени");
  Id race = 0, mine = 0, works = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);   // деревня: 3 слота
    mine = createBuilding(tx, 0, "Рудник");
    tx.building(mine).levels[0].produce[iron] = 5;
    tx.building(mine).levels.push_back(BuildingLevel{});
    placeBuilding(tx, f.p[0], mine, 1);
    const Id shrine = createBuilding(tx, 0, "Святилище пламени");
    setBuildingRole(tx, shrine, BuildingRole::Essence, true);
    setLevelEssence(tx, shrine, 1, fire, 3);
    placeBuilding(tx, f.p[0], shrine, 1);
    works = createBuilding(tx, 0, "Плавильня");
    setBuildingRole(tx, works, BuildingRole::Convert, true);
    setRecipe(tx, works, 0, Recipe{"", {{coal, 1}}, {}, {{steel, 1}}, 2});
    placeBuilding(tx, f.p[0], works, 1);
    tx.faction(f.A).res[coal] = 10;
    setSlaves(tx, f.A, race, 50, 0);
    setSlaveWork(tx, f.p[0], race, 50);
  });
  turn(f);   // цикл начат: уголь −1, до конца — 1 ход
  CHECK_NEAR(f.w().faction(f.A)->stock(coal), 9, 1e-9);
  CHECK_EQ(f.w().province(f.p[0])->buildings.at(2).cycle, 1);
  f.tx([&](Tx& tx) {
    setRelation(tx, f.A, f.B, -50, RelStatus::War);   // оккупация держится, пока стороны в войне
    setOccupied(tx, f.p[0], f.B);
    tx.settings().occupiedIncome = OccupiedIncome::Occupier;
  });
  // Производство, эссенции и рабы на работах — тому же получателю, что и налог.
  CHECK(f.pc(f.p[0]).occupied);
  CHECK_NEAR(f.fc(f.B).resources.at(iron).production, 5, 1e-9);
  CHECK_NEAR(f.fc(f.B).essences.at(fire).generation, 3, 1e-9);
  CHECK_NEAR(f.fc(f.B).incSlaves, 0.1, 1e-9);
  CHECK_NEAR(f.fc(f.A).resources.at(iron).production, 0, 1e-9);
  CHECK_NEAR(f.fc(f.A).incSlaves, 0, 1e-9);
  // Циклы стоят, владелец не строит.
  CHECK(f.fc(f.A).conversions.empty());
  auto opts = buildOptions(f.w(), f.p[0]);
  auto it = std::find_if(opts.begin(), opts.end(), [&](const BuildOption& o) { return o.building == mine; });
  CHECK(it != opts.end() && !it->can && has(it->reasons.front(), "оккупирована"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], mine); }); }), "оккупирована"));
  turn(f);
  CHECK_NEAR(f.w().faction(f.B)->stock(iron), 5, 1e-9);
  CHECK_NEAR(f.w().faction(f.B)->essence(fire), 3, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 5, 1e-9);   // первый ход — ещё до оккупации
  CHECK_EQ(f.w().province(f.p[0])->buildings.at(2).cycle, 1);
  CHECK_NEAR(f.w().faction(f.A)->stock(steel), 0, 1e-9);
  // Получатель «никто» — производство не получает никто.
  f.tx([&](Tx& tx) { tx.settings().occupiedIncome = OccupiedIncome::None; });
  CHECK_NEAR(f.fc(f.A).resources.at(iron).production + f.fc(f.B).resources.at(iron).production, 0, 1e-9);
  // Оккупация снята — цикл продолжается и завершается.
  f.tx([&](Tx& tx) { setOccupied(tx, f.p[0], 0); });
  turn(f);
  CHECK_NEAR(f.w().faction(f.A)->stock(steel), 1, 1e-9);
}

// ---------------------------------------------------------------- 4.6 множитель и прерванный цикл
TEST(rules_econ_loops_multiplier_and_cycle_refund) {
  Fix f;
  const Id iron = 5, steel = resNamed(f.w(), "Сталь"), blood = essNamed(f.w(), "Эссенция крови");
  Id works = 0;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.A, addRace(tx), 1000);
    works = createBuilding(tx, 0, "Плавильня");
    setBuildingRole(tx, works, BuildingRole::Convert, true);
    setRecipe(tx, works, 0, Recipe{"", {{iron, 2}}, {{blood, 1}}, {{steel, 1}}, 3});
    placeBuilding(tx, f.p[0], works, 1);
    // A изучило всю ветку «Стимуляция промышленности», B — ничего.
    for (Id t : tx.w().techs.ids())
      if (tx.w().tech(t)->faction == 0 && schema::industryMultOf(tx.w().tech(t)->key) >= 2) tx.faction(f.A).techs[t].studied = true;
    for (Id s : {f.A, f.B}) {
      tx.faction(s).res[iron] = 100;
      tx.faction(s).ess[blood] = 10;
    }
  });
  f.tx([&](Tx& tx) { setPbMult(tx, f.p[0], works, 3); });
  CHECK_EQ(f.fc(f.A).conversions.at(0).mult, 3);
  turn(f);   // цикл ×3 начат: железо −6, кровь −3
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 94, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->essence(blood), 7, 1e-9);
  CHECK_EQ(f.w().province(f.p[0])->buildings.at(0).cycle, 2);
  // Смена владельца посреди цикла: цикл прерывается, вход — прежнему владельцу.
  f.tx([&](Tx& tx) { setProvinceOwner(tx, f.p[0], f.B); });
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 100, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->essence(blood), 10, 1e-9);
  CHECK_EQ(f.w().province(f.p[0])->buildings.at(0).cycle, 0);
  CHECK(has(lastLog(f.w(), "перешла к"), "Циклы преобразования прерваны"));
  // У нового владельца нет «Стимуляции промышленности»: фабрика ×3 работает как ×1.
  CHECK_EQ(f.w().province(f.p[0])->buildings.at(0).mult, 3);
  CHECK_EQ(f.fc(f.B).conversions.at(0).mult, 1);
  CHECK_NEAR(f.fc(f.B).resources.at(iron).conversionIn, 2, 1e-9);
  turn(f);
  CHECK_NEAR(f.w().faction(f.B)->stock(iron), 98, 1e-9);
  CHECK_NEAR(f.w().faction(f.B)->essence(blood), 9, 1e-9);
  // Снос посреди цикла: вход возвращается владельцу.
  f.tx([&](Tx& tx) { demolish(tx, f.p[0], works); });
  CHECK_NEAR(f.w().faction(f.B)->stock(iron), 100, 1e-9);
  CHECK_NEAR(f.w().faction(f.B)->essence(blood), 10, 1e-9);
  CHECK(has(lastLog(f.w(), "Снесена постройка"), "ресурсы на входе возвращены"));
}

// ---------------------------------------------------------------- 4.8 торговые галеоны
TEST(rules_econ_loops_galleon_income) {
  Fix f;
  Id gal = 0;
  f.tx([&](Tx& tx) {
    gal = addFleetRow(tx, f.A, ShipType::Galleon, "", 20, 10);   // содержание на доход не влияет
    setTradeFleet(tx, f.A, gal, 20);
  });
  CHECK_NEAR(f.fc(f.A).incTradeFleet, 10, 1e-9);   // 20 × 0,5
  // Штраф к содержанию флота доход не меняет; модификатор дохода — меняет (собственный доход).
  f.tx([&](Tx& tx) { tx.faction(f.A).modifiers = {makeMod(tx, {{Fx::FleetUpkeepPct, 75}, {Fx::IncomePct, 10}})}; });
  CHECK_NEAR(f.fc(f.A).incTradeFleet, 10, 1e-9);
  CHECK_NEAR(f.fc(f.A).incTotal, 11, 1e-9);
  f.tx([&](Tx& tx) { setConst(tx, schema::cst::GalleonIncome, 2); });
  CHECK_NEAR(f.fc(f.A).incTradeFleet, 40, 1e-9);
}

// ---------------------------------------------------------------- 4.9 прирост и предел населения
TEST(rules_econ_loops_growth_cap) {
  Fix f;
  Id race = 0, elf = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    elf = addCatalogItem(tx, CatalogList::Races, "Эльфы");
    setConst(tx, schema::cst::PopGrowthBase, 0.5);
    setConst(tx, schema::cst::FamineDeaths, 10);
    own(tx, f.p[0], f.A, race, 12000);   // средняя провинция: предел 120 000
    own(tx, f.p[1], f.A, race, 10);      // малая группа растёт хотя бы на 1
    own(tx, f.p[2], f.A, race, 119998);  // у предела: и +50 % не переходят его
    tx.province(f.p[2]).races.push_back(RacePop{elf, 1});
    tx.province(f.p[2]).modifiers = {makeMod(tx, {{Fx::PopGrowthPct, 50}})};
    own(tx, f.p[3], f.A, race, 130000);  // выше предела: положительного прироста нет, отрицательный — как был
    tx.province(f.p[3]).modifiers = {makeMod(tx, {{Fx::PopGrowthPct, -10}})};
    own(tx, f.p[4], f.A, race, 40000);   // маленькая столица: предел 40 000 × 1,5
    tx.province(f.p[4]).size = ProvSize::Small;
    setCapital(tx, f.A, f.p[4]);
    tx.faction(f.A).res[provisionResources(tx.w()).front()] = 1e6;   // провизии хватает
    own(tx, f.p[5], f.B, race, 1000);    // государство нежити: естественного прироста нет
    setStateKind(tx, f.B, StateKind::Undead);
  });
  CHECK_NEAR(f.pc(f.p[0]).growthBase, 0.5, 1e-12);
  CHECK_EQ(f.pc(f.p[0]).popCap, i64(120000));
  CHECK_NEAR(f.pc(f.p[0]).growthPct, 0.45, 1e-12);   // 0,5 × (1 − 12 000 / 120 000)
  CHECK_EQ(f.pc(f.p[4]).popCap, i64(60000));
  CHECK_NEAR(f.pc(f.p[3]).growthPct, -9.5, 1e-12);
  CHECK_NEAR(f.pc(f.p[5]).growthPct, 0, 1e-12);
  turn(f);
  CHECK_EQ(popOf(f.w(), f.p[0]), i64(12054));
  CHECK_EQ(popOf(f.w(), f.p[1]), i64(11));
  CHECK_EQ(popOf(f.w(), f.p[2]), i64(120000));
  CHECK_EQ(popOf(f.w(), f.p[3]), i64(117650));
  CHECK_EQ(popOf(f.w(), f.p[4]), i64(40067));
  CHECK_EQ(popOf(f.w(), f.p[5]), i64(1000));
  // На пределе положительного прироста нет.
  CHECK_NEAR(f.pc(f.p[2]).growthPct, 0, 1e-12);
  turn(f);
  CHECK_EQ(popOf(f.w(), f.p[2]), i64(120000));
}

// ---------------------------------------------------------------- 4.10 пустошь и гильдии
TEST(rules_econ_loops_wasteland_blocks_guilds) {
  Fix f;
  f.tx([&](Tx& tx) {
    for (int i : {0, 1}) {
      tx.province(f.p[i]).owner = f.A;
      tx.province(f.p[i]).baseTrade = 100;
    }
    Province& p = tx.province(f.p[0]);
    p.influence = {Influence{f.G, 50}};
    p.hqs = {f.G};
    createRoute(tx, {center(0), center(1)}, f.G);
  });
  CHECK(f.pc(f.p[0]).guilds.at(0).net > 0);
  CHECK_NEAR(f.fc(f.G).incRoutes, (f.pc(f.p[0]).tradeValue + f.pc(f.p[1]).tradeValue) * 0.05, 1e-9);
  // Пустошь нежити: доход с ценности — только государству нежити; штаб и маршрут гильдии с неё ничего не получают.
  f.tx([&](Tx& tx) {
    setStateKind(tx, f.A, StateKind::Undead);
    makeWasteland(tx, f.p[0]);
  });
  const ProvinceCalc& pc = f.pc(f.p[0]);
  CHECK(pc.guildBlocked && !pc.tradeBlocked);
  CHECK_NEAR(pc.guilds.at(0).net, 0, 1e-12);
  CHECK(pc.guildTax > 0);   // налог гильдии — государству нежити
  CHECK_NEAR(f.fc(f.G).incGuilds, 0, 1e-12);
  CHECK_NEAR(f.fc(f.G).incRoutes, f.pc(f.p[1]).tradeValue * 0.05, 1e-9);
}

// ---------------------------------------------------------------- 4.11 рабы сверх лимита
TEST(rules_econ_loops_slaves_trimmed) {
  Fix f;
  Id human = 0, elf = 0;
  f.tx([&](Tx& tx) {
    human = addRace(tx);
    elf = addCatalogItem(tx, CatalogList::Races, "Эльфы");
    setConst(tx, schema::cst::PopGrowthBase, 0);
    setConst(tx, schema::cst::FamineDeaths, 0);
    own(tx, f.p[0], f.A, human, 1000);
    setSlaves(tx, f.A, human, 60, 0);
    setSlaves(tx, f.A, elf, 40, 0);
    setSlaveWork(tx, f.p[0], human, 60);
    setSlaveWork(tx, f.p[0], elf, 40);
  });
  f.tx([&](Tx& tx) { tx.province(f.p[0]).races[0].pop = 500; });   // население упало: лимит 50
  turn(f);
  const auto& sl = f.w().province(f.p[0])->slaves;
  CHECK_EQ(sl.size(), size_t(1));
  CHECK(sl.size() == 1 && sl[0].race == human && sl[0].count == 50);
  CHECK(has(lastLog(f.w(), "рабов на работах"), "снято 50"));
  turn(f);   // в пределах лимита — без изменений
  CHECK_EQ(f.w().province(f.p[0])->slaves.at(0).count, i64(50));
}

// ---------------------------------------------------------------- 4.12 снос не больше потерянных слотов
TEST(rules_econ_loops_slot_loss_keeps_old_excess) {
  Fix f;
  std::vector<Id> bs;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.A, addRace(tx), 12000);   // небольшой город: средняя провинция 2 + 2 = 4 слота
    for (int i = 0; i < 4; i++) {
      bs.push_back(createBuilding(tx, 0, "Постройка " + std::to_string(i)));
      placeBuilding(tx, f.p[0], bs.back(), 1);
    }
  });
  // Население упало до аванпоста (2 слота): обе лишние постройки законно остаются.
  f.tx([&](Tx& tx) { tx.province(f.p[0]).races[0].pop = 500; });
  CHECK_EQ(f.pc(f.p[0]).slots, 2);
  CHECK_EQ(f.w().province(f.p[0])->buildings.size(), size_t(4));
  // Модификатор −1 слот: сносится одна постройка (последняя), а не три.
  const World before = f.w();
  Id minus = 0;
  World after = [&] {
    Tx tx(before);
    minus = makeMod(tx, {{Fx::Slots, -1}});
    tx.province(f.p[0]).modifiers = {minus};
    return std::move(tx).finish();
  }();
  auto loss = slotLosses(before, after);
  CHECK_EQ(loss.size(), size_t(1));
  if (!loss.empty()) {
    CHECK_EQ(loss[0].buildings.size(), size_t(1));
    CHECK_EQ(loss[0].buildings.at(0), bs[3]);
    CHECK_EQ(loss[0].slots, 3);
  }
  f.tx([&](Tx& tx) {
    tx.replaceWorld(after);
    trimExcessBuildings(tx, &loss);
  });
  CHECK_EQ(f.w().province(f.p[0])->buildings.size(), size_t(3));
  // Ход без новых потерь слотов ничего не сносит.
  turn(f);
  CHECK_EQ(f.w().province(f.p[0])->buildings.size(), size_t(3));
}

// ---------------------------------------------------------------- 4.13 золото до тысячных
TEST(rules_econ_loops_gold_thousandths) {
  Fix f;
  f.tx([&](Tx& tx) {
    Province& p = tx.province(f.p[0]);
    p.owner = f.A;
    p.baseTrade = 1.2345;   // налог 10 % — 0,12345
    tx.faction(f.A).res[kGold] = 4.9996;
  });
  CHECK_NEAR(f.fc(f.A).net, 0.123, 1e-12);
  turn(f);
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 5.123, 1e-12);   // 4,9996 + 0,123 → до тысячных
  turn(f);
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 5.246, 1e-12);
}

// ---------------------------------------------------------------- 11 маршруты: оккупация и война с гильдией
TEST(rules_econ_loops_route_blocked) {
  Fix f;
  f.tx([&](Tx& tx) {
    for (int i : {0, 1}) tx.province(f.p[i]).baseTrade = 100;
    tx.province(f.p[0]).owner = f.A;
    tx.province(f.p[1]).owner = f.B;
    createRoute(tx, {center(0), center(1)}, f.G);
  });
  // Два государства на пути: +10 % и 2 × 2,5 %.
  CHECK_NEAR(f.pc(f.p[0]).tradeValue, 115, 1e-9);
  CHECK_NEAR(f.pc(f.p[1]).tradeValue, 115, 1e-9);
  // Оккупированная провинция: маршрут через неё не работает — нет бонуса, её государство не считается, 5 % гильдии нет.
  f.tx([&](Tx& tx) { setOccupied(tx, f.p[1], f.C); });
  CHECK_EQ(f.pc(f.p[1]).routes, 1);
  CHECK_NEAR(f.pc(f.p[1]).tradeValue, 100, 1e-9);
  CHECK_NEAR(f.pc(f.p[0]).tradeValue, 112.5, 1e-9);
  CHECK_NEAR(f.fc(f.G).incRoutes, 112.5 * 0.05, 1e-9);
  // Оккупация снята, но владелец в войне с гильдией маршрута — так же.
  f.tx([&](Tx& tx) {
    setOccupied(tx, f.p[1], 0);
    setRelation(tx, f.B, f.G, -50, RelStatus::War);
  });
  CHECK_NEAR(f.pc(f.p[1]).tradeValue, 100, 1e-9);
  CHECK_NEAR(f.pc(f.p[0]).tradeValue, 112.5, 1e-9);
  CHECK_NEAR(f.fc(f.G).incRoutes, 112.5 * 0.05, 1e-9);
}

// ---------------------------------------------------------------- 11 лимит исследований
TEST(rules_econ_loops_research_slots) {
  Fix f;
  std::vector<Id> ts;
  Id common = 0;
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 4; i++) {
      ts.push_back(createTech(tx, f.A, "Т" + std::to_string(i)));
      tx.tech(ts.back()).turns = 5;
    }
    common = createTech(tx, 0, "Общая");
    tx.tech(common).turns = 5;
  });
  CHECK_EQ(researchSlots(f.w()), 3);
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 3; i++) startResearch(tx, ts[size_t(i)]);
  });
  CHECK_EQ(researchCount(f.w(), f.A), 3);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startResearch(tx, ts[3]); }); }), "не больше 3"));
  const ResearchCheck rc = canResearch(f.w(), ts[3]);
  CHECK(!rc.ok && !rc.problems.empty() && has(rc.problems.back(), "не больше 3"));
  CHECK(canResearch(f.w(), ts[0]).problems.empty());   // идущее исследование лимит не трогает
  // Общие технологии — в том же лимите.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startResearch(tx, common, f.A); }); }), "не больше 3"));
  f.tx([&](Tx& tx) {
    stopResearch(tx, ts[0]);
    startResearch(tx, common, f.A);
  });
  CHECK_EQ(researchCount(f.w(), f.A), 3);
  // Константа мира; уменьшение не останавливает идущие исследования.
  f.tx([&](Tx& tx) {
    setConst(tx, schema::cst::ResearchSlots, 4);
    startResearch(tx, ts[3]);
  });
  CHECK_EQ(researchCount(f.w(), f.A), 4);
  f.tx([&](Tx& tx) { setConst(tx, schema::cst::ResearchSlots, 1); });
  turn(f);
  CHECK_EQ(researchCount(f.w(), f.A), 4);
  CHECK_EQ(researchCount(f.w(), f.B), 0);
}
