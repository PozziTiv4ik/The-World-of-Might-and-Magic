// Regnum — тесты правил по ТЗ 2026-10: встроенные записи, модификаторы со сроками, совет и голод, население
// целыми числами, формирование и резерв, флот в торговле, оккупация, захват, штурм, трупы, герои, мятеж,
// восстания, перемирие, вассалитет, виды государств, пустошь и осквернение, слоты, время исследования.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

// Раса «Люди» и население провинций; владелец — owner.
Id addRace(Tx& tx) { return addCatalogItem(tx, CatalogList::Races, "Люди"); }
void own(Tx& tx, Id p, Id owner, Id race, i64 pop) {
  Province& m = tx.province(p);
  m.owner = owner;
  m.races = {RacePop{race, pop}};
}
i64 popOf(const World& w, Id p) {
  i64 n = 0;
  for (const RacePop& r : w.province(p)->races) n += r.pop;
  return n;
}
i64 unitsIn(const World& w, Id army) {
  i64 n = 0;
  for (const ArmyGroup& g : w.army(army)->groups)
    for (const ArmyUnit& u : g.units) n += u.count;
  return n;
}
// Эффекты встроенного модификатора в мире теста (в базовом мире совет и голод — без эффектов).
void setFx(Tx& tx, const char* key, std::initializer_list<std::pair<Fx, double>> fx) {
  Id id = ensureBuiltinMod(tx, key);
  Modifier& m = tx.modifier(id);
  m.fx = {};
  m.fxMask = 0;
  for (auto& [f, v] : fx) {
    m.fx[size_t(f)] = v;
    m.fxMask |= 1u << unsigned(f);
  }
}
Id modId(const World& w, const char* key) { return builtinModId(w, key); }
template <class V, class X> bool contains(const V& v, const X& x) { return std::find(v.begin(), v.end(), x) != v.end(); }
Id provinceOf(const World& w, Id army) { return geo::faces(w)->provinceAt(w.army(army)->pos); }
bool hasMod(const World& w, const std::vector<Id>& mods, const char* key) { return hasModKey(w, mods, key); }

}  // namespace

// ---------------------------------------------------------------- целые числа
TEST(rules_tz_split_integer) {
  CHECK((splitEven(10, {-1, -1, -1}) == std::vector<i64>{4, 3, 3}));
  CHECK((splitEven(10, {2, -1, -1}) == std::vector<i64>{2, 4, 4}));
  CHECK((splitEven(5, {1, 1, 1}) == std::vector<i64>{1, 1, 1}));   // мест меньше, чем людей
  auto p = splitProportional(7, {5, 0, 5});
  CHECK_EQ(p[0] + p[1] + p[2], 7);
  CHECK_EQ(p[1], 0);
  auto q = splitProportional(100, {1, 2, 7});
  CHECK((q == std::vector<i64>{10, 20, 70}));
}

TEST(rules_tz_population_take_and_give) {
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

// ---------------------------------------------------------------- встроенные записи
TEST(rules_tz_builtins_lazy) {
  Fix f;
  CHECK_EQ(modId(f.w(), schema::mod::Plundered), Id(0));
  CHECK(builtinMod(f.w(), schema::mod::Plundered) != nullptr);   // шаблон
  Id m1 = 0, m2 = 0, r1 = 0, r2 = 0;
  f.tx([&](Tx& tx) {
    m1 = ensureBuiltinMod(tx, schema::mod::Plundered);
    m2 = ensureBuiltinMod(tx, schema::mod::Plundered);
    r1 = ensureResource(tx, schema::kResCorpses);
    r2 = ensureResource(tx, schema::kResCorpses);
  });
  CHECK(m1 != 0 && m1 == m2);
  CHECK_EQ(f.w().modifier(m1)->duration, 5);
  CHECK(r1 != 0 && r1 == r2);
  CHECK_EQ(resourceId(f.w(), schema::kResCorpses), r1);
  CHECK_NEAR(constantOf(f.w(), schema::cst::ColonizationCost).num, 0, 1e-9);
  CHECK_NEAR(constantOf(f.w(), schema::cst::EnergyPerUnit).num, 100, 1e-9);
  f.tx([&](Tx& tx) { ensureConstant(tx, schema::cst::ColonizationCost).num = 500; });
  CHECK_NEAR(constantOf(f.w(), schema::cst::ColonizationCost).num, 500, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { removeConstant(tx, schema::cst::ColonizationCost); }); }), "встроенная"));
  std::string key;
  f.tx([&](Tx& tx) { key = addConstant(tx, ConstType::Values, "Свои"); });
  CHECK_EQ(key, std::string("user1"));
  f.tx([&](Tx& tx) { removeConstant(tx, key); });
  CHECK(f.w().constants->find(key) == nullptr);
}

// ---------------------------------------------------------------- модификаторы со сроком
TEST(rules_tz_modifier_durations_expire) {
  Fix f;
  Id m = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    m = ensureBuiltinMod(tx, schema::mod::Plundered);
    addModifier(tx, ModTarget::Province, f.p[0], m);
  });
  CHECK_EQ(f.w().province(f.p[0])->modTurns.at(m), 5);
  for (int i = 0; i < 4; i++) f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(contains(f.w().province(f.p[0])->modifiers, m));
  CHECK_EQ(f.w().province(f.p[0])->modTurns.at(m), 1);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(!contains(f.w().province(f.p[0])->modifiers, m));
  CHECK(has(lastLog(f.w(), "Истёк"), "Разграбленная"));
  // Вид модификатора: провинциальный нельзя назначить герою; автоматический — никому.
  Id c = 0;
  f.tx([&](Tx& tx) { c = createCharacter(tx, f.A, "Герой"); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, m); }); }), "нельзя назначить"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Faction, f.A, ensureBuiltinMod(tx, schema::mod::Famine)); }); }),
            "автоматически"));
}

TEST(rules_tz_hero_modifier_rules) {
  Fix f;
  Id c = 0, army = 0, row = 0;
  f.tx([&](Tx& tx) {
    c = createCharacter(tx, f.A, "Арн");
    row = addArmyRow(tx, f.A, UnitType::LightInf, "", 100, 1);
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, army, f.A, row, 10);
    setHero(tx, army, c, true);
    setRuler(tx, f.A, c);
    tx.province(f.p[0]).owner = f.A;
    setLord(tx, f.p[0], c);
  });
  CHECK(hasMod(f.w(), f.w().character(c)->modifiers, schema::mod::Living));   // природа по виду государства
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, ensureBuiltinMod(tx, schema::mod::Undead)); });
  CHECK(!hasMod(f.w(), f.w().character(c)->modifiers, schema::mod::Living));   // природа — одна
  CHECK(hasMod(f.w(), f.w().character(c)->modifiers, schema::mod::Undead));
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, ensureBuiltinMod(tx, schema::mod::Loyalist)); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, ensureBuiltinMod(tx, schema::mod::Discontent)); }); }),
            "нельзя назначить вместе"));
  // «Мертв» снимает со всех назначений.
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, ensureBuiltinMod(tx, schema::mod::Dead)); });
  CHECK_EQ(f.w().faction(f.A)->ruler, Id(0));
  CHECK_EQ(f.w().province(f.p[0])->lord, Id(0));
  CHECK(f.w().army(army)->groups[0].heroes.empty());
  CHECK(!heroAvailable(f.w(), c));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRuler(tx, f.A, c); }); }), "мёртв"));
}

TEST(rules_tz_assignment_only_own_heroes) {
  Fix f;
  Id mine = 0, other = 0, seat = 0;
  f.tx([&](Tx& tx) {
    mine = createCharacter(tx, f.A, "Свой");
    other = createCharacter(tx, f.B, "Чужой");
    CouncilSeat s;
    s.id = tx.nextId(Seq::Council);
    s.position = "Канцлер";
    tx.faction(f.A).council.push_back(s);
    seat = s.id;
    tx.province(f.p[0]).owner = f.A;
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setCouncilMember(tx, f.A, seat, other); }); }), "только героев"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setLord(tx, f.p[0], other); }); }), "только героев"));
  f.tx([&](Tx& tx) { setCouncilMember(tx, f.A, seat, mine); });
  CHECK_EQ(f.w().faction(f.A)->council[0].character, mine);
  CHECK_EQ(councilAssigned(f.w(), f.A), 1);
}

// ---------------------------------------------------------------- совет, должности, голод
TEST(rules_tz_council_and_position_modifiers) {
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

TEST(rules_tz_provisions_and_famine) {
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
  CHECK_NEAR(f.w().faction(f.A)->provisionDebt, 20, 1e-9);
  // Ресурсы группы «Провизия» (с подгруппами): расход и недостача — поровну, исчерпанный отдаёт всё.
  const std::vector<Id> provs = provisionResources(f.w());
  CHECK(provs.size() >= 17);
  const Id grain = provs[1], fish = provs.back();
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[grain] = 100;
    tx.faction(f.A).res[fish] = 4;
  });
  CHECK_NEAR(f.fc(f.A).resources.at(fish).consumption, 4, 1e-9);
  CHECK_NEAR(f.fc(f.A).resources.at(grain).consumption, 26, 1e-9);   // 10 + 20 недостачи − 4
  CHECK_NEAR(f.fc(f.A).provisionDebtNext, 0, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(grain), 74, 1e-9);
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

// ---------------------------------------------------------------- маршруты гильдии
TEST(rules_tz_guild_route_income) {
  Fix f;
  f.tx([&](Tx& tx) {
    for (int i : {0, 1}) {
      tx.province(f.p[i]).owner = f.A;
      tx.province(f.p[i]).baseTrade = 100;
    }
    createRoute(tx, {center(0), center(1)}, f.G);
  });
  const double v = f.pc(f.p[0]).tradeValue + f.pc(f.p[1]).tradeValue;
  CHECK_NEAR(v, 2 * 112.5, 1e-9);
  CHECK_NEAR(f.fc(f.G).incRoutes, v * 0.05, 1e-9);
}

// ---------------------------------------------------------------- формирование и резерв
TEST(rules_tz_recruit_disband) {
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

TEST(rules_tz_recruit_undead_and_ships) {
  Fix f;
  Id row = 0, frig = 0;
  f.tx([&](Tx& tx) {
    setStateKind(tx, f.A, StateKind::Undead);
    row = addArmyRow(tx, f.A, UnitType::HeavyInf, "Скелеты", 0, 0);
    frig = addFleetRow(tx, f.A, ShipType::Frigate, "", 0, 1);
    ensureConstant(tx, schema::cst::FrigateCost).res[kGold] = 100;
    tx.faction(f.A).res[kGold] = 250;
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

// ---------------------------------------------------------------- флот в торговле
TEST(rules_tz_trade_fleet_and_pirates) {
  Fix f;
  Id gal = 0, frig = 0;
  f.tx([&](Tx& tx) {
    gal = addFleetRow(tx, f.A, ShipType::Galleon, "", 20, 10);
    frig = addFleetRow(tx, f.A, ShipType::Frigate, "", 10, 1);
    setTradeFleet(tx, f.A, gal, 20);
  });
  CHECK_NEAR(f.fc(f.A).incTradeFleet, 400, 1e-9);
  CHECK_NEAR(f.fc(f.A).pirateRisk, 2, 1e-9);
  CHECK_EQ(reserveOf(f.w(), f.A, gal), 0);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTradeFleet(tx, f.A, gal, 21); }); }), "В резерве недостаточно"));
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->pirateRisk, 2, 1e-9);   // бросок — в конце следующего хода
  f.tx([&](Tx& tx) { setTradeFleet(tx, f.A, frig, 10); });   // охрана: 5 фрегатов на 10 галеонов
  CHECK_NEAR(f.fc(f.A).pirateRisk, 0, 1e-9);
}

// ---------------------------------------------------------------- оккупация
TEST(rules_tz_occupation_lifts_without_garrison) {
  Fix f;
  Id row = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    tx.province(f.p[1]).owner = f.A;
    setOccupied(tx, f.p[0], f.B);
    setOccupied(tx, f.p[1], f.B);
    row = addArmyRow(tx, f.B, UnitType::LightInf, "", 50, 1);
    setOccupationGarrison(tx, f.p[1], row, 20);
  });
  CHECK_EQ(reserveOf(f.w(), f.B, row), 30);
  for (int i = 0; i < 4; i++) f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(f.w().province(f.p[0])->occupied);
  CHECK_EQ(f.w().province(f.p[0])->occIdle, 4);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(!f.w().province(f.p[0])->occupied);
  CHECK(f.w().province(f.p[1])->occupied);   // с гарнизоном — остаётся
}

// ---------------------------------------------------------------- провинции
TEST(rules_tz_owner_change_and_colonize) {
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
  f.tx([&](Tx& tx) { colonize(tx, f.p[1], f.B); });
  CHECK_EQ(f.w().province(f.p[1])->owner, f.B);
  CHECK_NEAR(f.w().faction(f.B)->treasury(), 200, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { colonize(tx, f.p[2], f.B); }); }), "Недостаточно золота"));
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Province, f.p[3], ensureBuiltinMod(tx, schema::mod::Devastated)); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setProvinceOwner(tx, f.p[3], f.A); }); }), "Опустошённую"));
}

TEST(rules_tz_slaves_work) {
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

// ---------------------------------------------------------------- захват
TEST(rules_tz_capture_plunder_raze_devastate) {
  Fix f;
  Id race = 0, row = 0, a0 = 0, a1 = 0, a2 = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    for (int i : {0, 1, 2}) {
      own(tx, f.p[i], f.B, race, 10000);
      tx.province(f.p[i]).baseTrade = 100;
    }
    own(tx, f.p[4], f.A, race, 10);
    declareWar(tx, f.A, f.B);
    row = addArmyRow(tx, f.A, UnitType::LightInf, "", 300, 1);
    a0 = createArmy(tx, ArmyKind::Army, f.A, center(0));
    a1 = createArmy(tx, ArmyKind::Army, f.A, center(1));
    a2 = createArmy(tx, ArmyKind::Army, f.A, center(2));
    for (Id a : {a0, a1, a2}) setUnits(tx, a, f.A, row, 10);
  });
  CaptureOptions o = captureOptions(f.w(), a0, f.p[0]);
  CHECK(o.can[int(Capture::Plunder)]);
  CHECK_NEAR(o.plunderGold, 100, 1e-9);   // V × N / 10 000
  CHECK_NEAR(o.razeGold, 200, 1e-9);
  f.tx([&](Tx& tx) { capture(tx, a0, f.p[0], Capture::Plunder); });
  CHECK(f.w().province(f.p[0])->occupied && f.w().province(f.p[0])->occupier == f.A);
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 100, 1e-9);
  CHECK(provinceHas(f.w(), f.p[0], schema::mod::Plundered));
  CHECK_NEAR(f.w().relation(f.A, f.B).v, -30, 1e-9);
  CHECK(!captureOptions(f.w(), a0, f.p[0]).can[int(Capture::Plunder)]);
  // Разорить: золото вдвое, без оккупации, войско отходит в соседнюю свою или оккупированную провинцию.
  f.tx([&](Tx& tx) { capture(tx, a1, f.p[1], Capture::Raze); });
  CHECK(!f.w().province(f.p[1])->occupied);
  CHECK(provinceHas(f.w(), f.p[1], schema::mod::Ravaged));
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 300, 1e-9);
  CHECK_EQ(provinceOf(f.w(), a1), f.p[0]);   // отступило в оккупированную П0
  CHECK_NEAR(f.w().relation(f.A, f.B).v, -40, 1e-9);
  // Опустошить 10 %: рабы, гибель, без владельца, «Неправедное деяние» и «Мучения совести».
  f.tx([&](Tx& tx) { capture(tx, a2, f.p[2], Capture::Devastate, 10); });
  CHECK_EQ(popOf(f.w(), f.p[2]), 0);
  CHECK_EQ(f.w().province(f.p[2])->owner, Id(0));
  CHECK(provinceHas(f.w(), f.p[2], schema::mod::Devastated));
  CHECK_EQ(f.w().faction(f.A)->slaves[0].count, 1000);
  CHECK(armyHas(f.w(), a2, schema::mod::Unrighteous));
  CHECK(armyHas(f.w(), a2, schema::mod::Conscience));
  CHECK_NEAR(f.w().relation(f.A, f.B).v, -60, 1e-9);
}

// ---------------------------------------------------------------- штурм, трупы, герои
TEST(rules_tz_siege_and_corpses) {
  Fix f;
  Id race = 0, ra = 0, rb = 0, army = 0, necro = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.B, race, 1000);
    declareWar(tx, f.A, f.B);
    rb = addArmyRow(tx, f.B, UnitType::LightInf, "", 100, 1);
    setGarrison(tx, f.p[0], rb, 100);
    ra = addArmyRow(tx, f.A, UnitType::HeavyInf, "", 200, 1);
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, army, f.A, ra, 200);
    necro = createCharacter(tx, f.A, "Некромант");
    addModifier(tx, ModTarget::Character, necro, ensureBuiltinMod(tx, schema::mod::Necromancer));
    setHero(tx, army, necro, true);
  });
  CHECK(canSiege(f.w(), army, f.p[0]));
  SiegeResult s;
  s.attacker = army;
  s.province = f.p[0];
  s.attackerWins = true;
  s.attackerOrigin = center(1);
  s.attackerLosses[{f.A, ra}] = 20;
  s.garrisonLosses[rb] = 60;
  BattleOutcome out;
  f.tx([&](Tx& tx) { out = resolveSiege(tx, s); });
  CHECK(f.w().province(f.p[0])->garrison.empty());
  CHECK_EQ(f.w().faction(f.B)->armyRow(rb)->total, 40);
  CHECK_EQ(out.winner, f.A);
  CHECK_NEAR(out.corpses, 6, 1e-9);   // некромант: 10 % от 60 погибших живых
  CHECK_NEAR(f.w().faction(f.A)->stock(resourceId(f.w(), schema::kResCorpses)), 6, 1e-9);
}

TEST(rules_tz_hero_fate_and_resurrect) {
  Fix f;
  Id c = 0;
  f.tx([&](Tx& tx) { c = createCharacter(tx, f.A, "Лорд"); });
  f.tx([&](Tx& tx) { heroFate(tx, c, Fate::Captured, f.B, 0); });
  CHECK(characterHas(f.w(), c, schema::mod::Captive));
  CHECK_EQ(f.w().character(c)->captor, f.B);
  CHECK_EQ(captivesOf(f.w(), f.B).size(), size_t(1));
  // Обмен пленными в переговорах: B передаёт героя A — он свободен.
  Deal d;
  d.a = f.A;
  d.b = f.B;
  d.items.push_back(DealItem{DealSide::B, kGold, 0, DealMode::Once, 1, 0, DealItemKind::Hero, c});
  f.tx([&](Tx& tx) { concludeDeal(tx, d); });
  CHECK(!characterHas(f.w(), c, schema::mod::Captive));
  CHECK_EQ(f.w().character(c)->captor, Id(0));
  f.tx([&](Tx& tx) { heroFate(tx, c, Fate::Killed, 0, f.p[3]); });
  CHECK(characterHas(f.w(), c, schema::mod::Dead));
  CHECK_EQ(f.w().character(c)->burial, f.p[3]);
  f.tx([&](Tx& tx) { resurrect(tx, c, schema::mod::Undead, f.B); });
  CHECK(!characterHas(f.w(), c, schema::mod::Dead));
  CHECK(characterHas(f.w(), c, schema::mod::Undead));
  CHECK(!characterHas(f.w(), c, schema::mod::Living));
  CHECK_EQ(f.w().character(c)->faction, f.B);
  CHECK_EQ(f.w().character(c)->burial, Id(0));
}

// ---------------------------------------------------------------- мятеж
TEST(rules_tz_mutiny_partial) {
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
  CHECK(contains(f.w().army(m.rebelArmy)->groups[0].heroes, hero));
  CHECK(contains(f.w().army(army)->groups[0].heroes, loyal));
  // Повторный мятеж — то же мятежное государство.
  f.tx([&](Tx& tx) { tx.army(army).loyalty = -10; });
  MutinyResult m2;
  f.tx([&](Tx& tx) { m2 = mutiny(tx, army); });
  CHECK_EQ(m2.rebelState, m.rebelState);
}

TEST(rules_tz_mutiny_full_at_minus_100_and_cleanup) {
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
  CHECK(contains(rep.events[0].heroes, loyal));
  Id rebels = rep.events[0].army;
  CHECK_EQ(unitsIn(f.w(), rebels), 50);
  // Мятежники без войск и провинций упраздняются в конце хода.
  f.tx([&](Tx& tx) { disband(tx, rebels); });
  Id rs = rep.events[0].rebelState;
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(!f.w().faction(rs));
}

TEST(rules_tz_defect_before_battle) {
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

TEST(rules_tz_province_uprising) {
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

// ---------------------------------------------------------------- перемирие и вассалитет
TEST(rules_tz_truce_and_war_lock) {
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

TEST(rules_tz_vassalage) {
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

// ---------------------------------------------------------------- виды государств
TEST(rules_tz_state_kinds) {
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

TEST(rules_tz_wasteland_and_desecration) {
  Fix f;
  Id race = 0, b = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    setStateKind(tx, f.A, StateKind::Undead);
    setStateKind(tx, f.C, StateKind::Demonic);
    own(tx, f.p[0], f.A, race, 1000);
    own(tx, f.p[1], f.C, race, 1000);
    own(tx, f.p[2], f.B, race, 500);
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
  f.tx([&](Tx& tx) { cleanseWasteland(tx, f.p[0], 100); });
  CHECK(!provinceHas(f.w(), f.p[0], schema::mod::UndeadWaste));
  CHECK_EQ(popOf(f.w(), f.p[0]), 100);
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

// ---------------------------------------------------------------- слоты и время исследования
TEST(rules_tz_slot_losses_and_trim) {
  Fix f;
  std::vector<Id> bs;
  f.tx([&](Tx& tx) {
    Province& p = tx.province(f.p[0]);
    p.owner = f.A;
    p.size = ProvSize::Small;
    p.city = CityType::Village;   // 1 + 1 = 2 слота, со столицей — 5
    setCapital(tx, f.A, f.p[0]);
    for (int i = 0; i < 4; i++) {
      Id b = createBuilding(tx, 0, "П" + std::to_string(i));
      bs.push_back(b);
      tx.province(f.p[0]).buildings.push_back(ProvBuilding{b, 1, false, 0, {}, 0});
    }
  });
  CHECK_EQ(f.pc(f.p[0]).slots, 5);
  World before = f.w();
  World after = [&] {
    Tx tx(before);
    setCapital(tx, f.A, 0);
    return std::move(tx).finish();
  }();
  auto loss = slotLosses(before, after);
  CHECK_EQ(loss.size(), size_t(1));
  CHECK_EQ(loss[0].buildings.size(), size_t(2));
  CHECK_EQ(loss[0].buildings[0], bs[3]);
  f.tx([&](Tx& tx) {
    setCapital(tx, f.A, 0);
    trimExcessBuildings(tx, &loss);
  });
  CHECK_EQ(f.w().province(f.p[0])->buildings.size(), size_t(2));
}

TEST(rules_tz_research_time_factor) {
  Fix f;
  Id t = 0;
  f.tx([&](Tx& tx) {
    setFx(tx, schema::mod::Decentralization, {{Fx::ResearchTimePct, 200}});
    t = createTech(tx, f.A, "Магия");
    tx.tech(t).turns = 2;
    startResearch(tx, t);
  });
  CHECK_EQ(researchTurns(f.w(), *f.w().tech(t)), 6);
  for (int i = 0; i < 5; i++) f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(!f.w().tech(t)->studied);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(f.w().tech(t)->studied);
}

TEST(rules_tz_loyalty_delta) {
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
    dropModifier(tx, ModTarget::Army, army, modId(tx.w(), schema::mod::Patriotism));
  });
  CHECK_NEAR(loyaltyDelta(f.w(), army), 5, 1e-9);   // не уменьшается и +5 за лоялиста
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setArmyLoyalty(tx, army, 50); }); }), "не уменьшается"));
  // «Изуверское наслаждение» — только армиям демонов и безжалостным.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Army, army, ensureBuiltinMod(tx, schema::mod::Sadism)); }); }),
            "Армия демонов"));
}

TEST(rules_tz_deal_province) {
  Fix f;
  f.tx([&](Tx& tx) { tx.province(f.p[0]).owner = f.A; });
  Deal d;
  d.a = f.A;
  d.b = f.B;
  d.items.push_back(DealItem{DealSide::A, kGold, 0, DealMode::Once, 1, 0, DealItemKind::Province, f.p[0]});
  CHECK(validateDeal(f.w(), d).ok);
  f.tx([&](Tx& tx) { concludeDeal(tx, d); });
  CHECK_EQ(f.w().province(f.p[0])->owner, f.B);
  CHECK(!validateDeal(f.w(), d).ok);   // провинция уже не у A
}

TEST(rules_tz_mutiny_all_armies_in_province_merge) {
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

TEST(rules_tz_unavailable_heroes_not_assigned_or_paid) {
  Fix f;
  Id row = 0, army = 0, c = 0;
  f.tx([&](Tx& tx) {
    row = addArmyRow(tx, f.A, UnitType::LightInf, "", 10, 0);
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, army, f.A, row, 10);
    c = createCharacter(tx, f.A, "Сэр");
    tx.character(c).hero = true;
    tx.character(c).upkeep = 2.5;
  });
  CHECK_NEAR(f.fc(f.A).expSpecialists, 2.5, 1e-9);
  f.tx([&](Tx& tx) { heroFate(tx, c, Fate::Captured, f.B, 0); });
  CHECK_NEAR(f.fc(f.A).expSpecialists, 0, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHero(tx, army, c, true); }); }), "в плену"));
  f.tx([&](Tx& tx) { heroFate(tx, c, Fate::Killed, 0, f.p[0]); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHero(tx, army, c, true); }); }), "мёртв"));
  CHECK_NEAR(f.fc(f.A).expSpecialists, 0, 1e-9);
}
