// Regnum — правила государств: население целыми числами, модификаторы со сроком, совет и должности (влияние совета),
// провизия и голод, смена владельца и колонизация, рабы, виды государств, пустошь нежити и осквернение.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

// ---------------------------------------------------------------- население целыми числами
TEST(rules_states_split_integer) {
  CHECK((splitEven(10, {-1, -1, -1}) == std::vector<i64>{4, 3, 3}));
  CHECK((splitEven(10, {2, -1, -1}) == std::vector<i64>{2, 4, 4}));
  CHECK((splitEven(5, {1, 1, 1}) == std::vector<i64>{1, 1, 1}));   // мест меньше, чем людей
  auto p = splitProportional(7, {5, 0, 5});
  CHECK_EQ(p[0] + p[1] + p[2], 7);
  CHECK_EQ(p[1], 0);
  auto q = splitProportional(100, {1, 2, 7});
  CHECK((q == std::vector<i64>{10, 20, 70}));
}

TEST(rules_states_population_take_and_give) {
  Fix f;
  Id race = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.A, race, 3);
  });
  std::map<Id, i64> taken;
  f.tx([&](Tx& tx) { taken = takePopulation(tx, f.A, 9); });
  CHECK_EQ(taken[race], 9);
  CHECK_EQ(popOf(f.w(), f.p[1]), 0);   // в малой провинции — все трое, остаток — из большой
  CHECK_EQ(popOf(f.w(), f.p[0]), 994);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { takePopulation(tx, f.A, 5000); }); }), "Недостаточно населения"));
  f.tx([&](Tx& tx) { givePopulation(tx, f.A, 9); });
  CHECK_EQ(popOf(f.w(), f.p[0]) + popOf(f.w(), f.p[1]), 1003);
}

// Население в пустые провинции государства без жителей — «Люди», а не первая по алфавиту раса (аудит 7.5).
TEST(rules_states_population_give_fallback_race) {
  Fix f;
  Id angels = 0, humans = 0;
  f.tx([&](Tx& tx) {
    angels = addCatalogItem(tx, CatalogList::Races, "Ангелы");
    humans = addCatalogItem(tx, CatalogList::Races, "Люди");
    for (int i = 0; i < 2; i++) {
      tx.province(f.p[i]).owner = f.A;
      tx.province(f.p[i]).races.clear();
    }
  });
  CHECK_EQ(f.w().catalogs->races.front().id, angels);
  f.tx([&](Tx& tx) { givePopulation(tx, f.A, 50); });
  i64 human = 0, other = 0;
  for (int i = 0; i < 2; i++)
    for (const RacePop& r : f.w().province(f.p[i])->races) (r.race == humans ? human : other) += r.pop;
  CHECK_EQ(human, 50);
  CHECK_EQ(other, 0);
}

// ---------------------------------------------------------------- модификаторы со сроком
TEST(rules_states_modifier_durations_expire) {
  Fix f;
  Id m = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    m = ensureBuiltinMod(tx, schema::mod::Plundered);
    addModifier(tx, ModTarget::Province, f.p[0], m);
  });
  CHECK_EQ(f.w().province(f.p[0])->modTurns.at(m), 5);
  for (int i = 0; i < 4; i++) f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(has(f.w().province(f.p[0])->modifiers, m));
  CHECK_EQ(f.w().province(f.p[0])->modTurns.at(m), 1);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(!has(f.w().province(f.p[0])->modifiers, m));
  CHECK(has(lastLog(f.w(), "Истёк"), "Разграбленная"));
  // Вид модификатора: провинциальный нельзя назначить герою; автоматический — никому.
  Id c = 0;
  f.tx([&](Tx& tx) { c = createCharacter(tx, f.A, "Герой"); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, m); }); }), "нельзя назначить"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Faction, f.A, ensureBuiltinMod(tx, schema::mod::Famine)); }); }),
            "автоматически"));
}

// ---------------------------------------------------------------- совет, должности, голод
TEST(rules_states_council_and_position_modifiers) {
  Fix f;
  Id posMod = 0, vacMod = 0;
  f.tx([&](Tx& tx) {
    setFx(tx, schema::mod::Decentralization, {{Fx::TradePct, -10}});
    setFx(tx, schema::mod::WeakControl, {{Fx::TradePct, -5}});
    tx.province(f.p[0]).owner = f.A;
    tx.province(f.p[0]).baseTrade = 100;
    posMod = makeMod(tx, {{Fx::IncomePct, 10}});
    vacMod = makeMod(tx, {{Fx::IncomePct, -20}});
    for (CatalogItem& c : tx.catalogs().positions)
      if (c.name == "Лорд-Мастер над экономикой") {
        c.modifiers = {posMod};
        c.vacantModifiers = {vacMod};
      }
  });
  CHECK_NEAR(f.pc(f.p[0]).tradeValue, 90, 1e-9);   // «Децентрализация»
  CHECK_NEAR(f.fc(f.A).incomePct, -20, 1e-9);        // казначея нет
  f.tx([&](Tx& tx) {
    Id ch = createCharacter(tx, f.A, "Казначей");
    CouncilSeat s;
    s.id = tx.nextId(Seq::Council);
    s.position = "Лорд-Мастер над экономикой";
    s.character = ch;
    tx.faction(f.A).council.push_back(s);
  });
  CHECK_NEAR(f.pc(f.p[0]).tradeValue, 95, 1e-9);   // «Слабый контроль»
  CHECK_NEAR(f.fc(f.A).incomePct, 10, 1e-9);
  auto autos = autoModifiers(f.w(), f.A);
  CHECK(std::any_of(autos.begin(), autos.end(), [](const AutoMod& a) { return a.key == schema::mod::WeakControl; }));
}

TEST(rules_states_council_influence) {
  Fix f;
  std::vector<Id> heroes;
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 4; i++) {
      const Id h = createCharacter(tx, f.A, "Советник " + std::to_string(i));
      addCouncilSeat(tx, f.A, "Должность " + std::to_string(i), h);
    }
    addCouncilSeat(tx, f.A, "Пустая");
  });
  auto autos = autoModifiers(f.w(), f.A);
  bool influence = false;
  for (const AutoMod& a : autos)
    if (a.key == schema::mod::CouncilInfluence) influence = a.scale == 4;   // пустая должность влияния не даёт
  CHECK(influence);
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::ResearchTimePct], -8, 1e-9);   // −2 % × 4 занятые должности
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 5; i++) addCouncilSeat(tx, f.A, "Ещё " + std::to_string(i));
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addCouncilSeat(tx, f.A, "Лишняя"); }); }), "не больше 10"));
}

TEST(rules_states_provisions_and_famine) {
  Fix f;
  Id race = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 10000);
    setFx(tx, schema::mod::Famine, {{Fx::ContentmentPerTurn, -5}});
  });
  CHECK(!f.fc(f.A).famine);
  // Провизии нет: расход 10 за ход становится недостачей — «Голод».
  CHECK_NEAR(f.fc(f.A).provisionNeed, 10, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->provisionDebt, 10, 1e-9);
  CHECK(f.fc(f.A).famine);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().province(f.p[0])->contentment, -5, 1e-9);   // «Голод» со следующего хода
  CHECK_NEAR(f.w().faction(f.A)->provisionDebt, 10, 1e-9);      // недостача не копится — только этого хода
  // Ресурсы группы «Провизия» (с подгруппами): расход и недостача — поровну, исчерпанный отдаёт всё.
  const std::vector<Id> provs = provisionResources(f.w());
  CHECK(provs.size() >= 17);
  const Id grain = provs[1], fish = provs.back();
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[grain] = 100;
    tx.faction(f.A).res[fish] = 4;
  });
  CHECK_NEAR(f.fc(f.A).resources.at(fish).consumption, 4, 1e-9);
  CHECK_NEAR(f.fc(f.A).resources.at(grain).consumption, 6, 1e-9);   // расход 10 − 4 (прошлая недостача не возвращается)
  CHECK_NEAR(f.fc(f.A).provisionDebtNext, 0, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(grain), 94, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(fish), 0, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->provisionDebt, 0, 1e-9);
  CHECK(!f.fc(f.A).famine);
  f.tx([&](Tx& tx) { tx.faction(f.A).res[fish] = 74; });
  CHECK_NEAR(f.fc(f.A).resources.at(fish).consumption, 5, 1e-9);   // поровну: 10 на два ресурса
  CHECK_NEAR(f.fc(f.A).resources.at(grain).consumption, 5, 1e-9);
  // Государство нежити провизию не тратит.
  f.tx([&](Tx& tx) { setStateKind(tx, f.A, StateKind::Undead); });
  const double before = f.w().faction(f.A)->stock(grain);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(grain), before, 1e-9);
}

// ---------------------------------------------------------------- провинции и рабы
TEST(rules_states_owner_change_and_colonize) {
  Fix f;
  Id b = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    b = createBuilding(tx, f.A, "Уникальная");
    tx.province(f.p[0]).buildings.push_back(ProvBuilding{b, 1, false, 0, {}, 0});
    ensureConstant(tx, schema::cst::ColonizationCost).num = 500;
    tx.faction(f.B).res[kGold] = 700;
  });
  f.tx([&](Tx& tx) { setProvinceOwner(tx, f.p[0], f.B); });
  CHECK(f.w().province(f.p[0])->buildings.empty());
  CHECK(has(lastLog(f.w()), "уникальные постройки"));
  // Колонизация: переселенцев не меньше 500 и не больше, чем жителей в провинциях государства.
  f.tx([&](Tx& tx) { tx.province(f.p[0]).races = {RacePop{addRace(tx), 2000}}; });
  CHECK_EQ(settlersAvailable(f.w(), f.B), i64(2000));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { colonize(tx, f.p[1], f.B, 499); }); }), "не меньше 500"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { colonize(tx, f.p[1], f.B, 2001); }); }), "не может быть больше"));
  f.tx([&](Tx& tx) { colonize(tx, f.p[1], f.B, 600); });
  CHECK_EQ(f.w().province(f.p[1])->owner, f.B);
  CHECK_EQ(popOf(f.w(), f.p[1]), 600);
  CHECK_EQ(popOf(f.w(), f.p[0]), 1400);
  CHECK_NEAR(f.w().faction(f.B)->treasury(), 200, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { colonize(tx, f.p[2], f.B, 500); }); }), "Недостаточно золота"));
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Province, f.p[3], ensureBuiltinMod(tx, schema::mod::Devastated)); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setProvinceOwner(tx, f.p[3], f.A); }); }), "Опустошённую"));
}

TEST(rules_states_slaves_work) {
  Fix f;
  Id race = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    setSlaves(tx, f.A, race, 100, 0);
  });
  CHECK_EQ(slaveWorkLimit(f.w(), f.p[0]), 100);
  f.tx([&](Tx& tx) { setSlaveWork(tx, f.p[0], race, 60); });
  CHECK_NEAR(f.fc(f.A).incSlaves, 60 * 0.002, 1e-12);
  CHECK_NEAR(f.fc(f.A).expSlaves, 100 * 0.001, 1e-12);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setSlaveWork(tx, f.p[0], race, 101); }); }), "10 %"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setSlaves(tx, f.A, race, 50, 0); }); }), "снимите"));
}

// ---------------------------------------------------------------- виды государств
TEST(rules_states_state_kinds) {
  Fix f;
  Id c = 0, army = 0, row = 0, mrow = 0;
  f.tx([&](Tx& tx) {
    setStateKind(tx, f.A, StateKind::Demonic);
    c = createCharacter(tx, f.A, "Бес");
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    row = addArmyRow(tx, f.A, UnitType::Flying, "", 0, 0);
    mrow = addArmyRow(tx, f.A, UnitType::Machines, "", 0, 0);
  });
  CHECK(characterHas(f.w(), c, schema::mod::Demon));
  CHECK(armyHas(f.w(), army, schema::mod::DemonArmy));
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->race, std::string(schema::kRaceDemonic));
  CHECK_EQ(f.w().faction(f.A)->armyRow(mrow)->race, std::string(schema::kRaceMechanical));
  Id u = 0;
  f.tx([&](Tx& tx) {
    setStateKind(tx, f.B, StateKind::Undead);
    u = createArmy(tx, ArmyKind::Army, f.B, center(2));
  });
  CHECK(armyHas(f.w(), u, schema::mod::UndeadArmy));
  CHECK_NEAR(f.w().army(u)->loyalty, 100, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setArmyLoyalty(tx, u, 10); }); }), "всегда 100"));
}

TEST(rules_states_wasteland_and_desecration) {
  Fix f;
  Id race = 0, b = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    setStateKind(tx, f.A, StateKind::Undead);
    setStateKind(tx, f.C, StateKind::Demonic);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.C, race, 1000);
    own(tx, f.p[2], f.B, race, 900);
    b = createBuilding(tx, 0, "Склеп");
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { makeWasteland(tx, f.p[2]); }); }), "нежити"));
  f.tx([&](Tx& tx) { makeWasteland(tx, f.p[0]); });
  CHECK_EQ(popOf(f.w(), f.p[0]), 0);
  CHECK_NEAR(f.w().faction(f.A)->stock(resourceId(f.w(), schema::kResCorpses)), 1000, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRacePop(tx, f.p[0], race, 5); }); }), "пустоши"));
  // Провинцию получило государство живых: строить нельзя, доход с ценности не идёт, можно очистить.
  f.tx([&](Tx& tx) {
    setProvinceOwner(tx, f.p[0], f.B);
    tx.province(f.p[0]).baseTrade = 100;
  });
  auto opts = buildOptions(f.w(), f.p[0]);
  CHECK(!opts.empty() && has(join(opts[0].reasons, ";"), "пустоши нежити"));
  CHECK(f.pc(f.p[0]).tradeBlocked);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { cleanseWasteland(tx, f.p[0], 100); }); }), "не меньше 500"));
  f.tx([&](Tx& tx) { cleanseWasteland(tx, f.p[0], 500); });
  CHECK(!provinceHas(f.w(), f.p[0], schema::mod::UndeadWaste));
  CHECK_EQ(popOf(f.w(), f.p[0]), 500);
  CHECK_EQ(popOf(f.w(), f.p[2]), 400);
  // Осквернение: 1000 × ⌊10 % населения⌋ сразу и 100 × ⌊10 %⌋ за ход.
  f.tx([&](Tx& tx) { desecrate(tx, f.p[1]); });
  const Id e = resourceId(f.w(), schema::kResEnergy);
  CHECK_NEAR(f.w().faction(f.C)->stock(e), 100000, 1e-9);
  CHECK_NEAR(f.pc(f.p[1]).energy, 10000, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.C)->stock(e), 110000, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { cleanseDesecration(tx, f.p[1]); }); }), "живых или нежити"));
}
