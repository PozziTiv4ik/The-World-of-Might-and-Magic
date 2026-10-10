// Regnum — правила ТЗ «Доработки №1–4» (2026-10-10), общая часть: чума и иммунитет, генерация модификаторов, лич,
// реликвии в постройках и у государства, наёмники, верфи и морские чудовища, пираты, стоимость технологий, постройки
// (одна стройка в провинции, эссенции в цене, требования «на государство», приморские), совет, войска без государства,
// одна религия, показатели археологических групп.
#include "core/arch.h"
#include "core/content.h"
#include "rules/internal.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

Id essNamed(const World& w, const char* name) {
  for (const CatalogItem& e : w.catalogs->essences)
    if (e.name == name) return e.id;
  return 0;
}
Id resNamed(const World& w, const char* name) {
  for (const CatalogItem& r : w.catalogs->resources)
    if (r.name == name) return r.id;
  return 0;
}

// Общая постройка с одним уровнем и возможностью; ставится готовой в провинцию.
Id building(Tx& tx, const char* name, Id province = 0, BuildingFlag flag = BuildingFlag::Coastal, bool on = false) {
  const Id b = createBuilding(tx, 0, name);
  if (on) setBuildingFlag(tx, b, flag, true);
  if (province) placeBuilding(tx, province, b, 1);
  return b;
}

}  // namespace

// ---------------------------------------------------------------- чума
TEST(rules_tz2_plague_spread_and_immunity) {
  Fix f;
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 8; i++) tx.province(f.p[i]).owner = f.A;
    CHECK(infectPlague(tx, f.p[1]));
  });
  const Id plague = builtinModId(f.w(), schema::mod::Plague);
  CHECK(plague != 0);
  CHECK(provinceHas(f.w(), f.p[1], schema::mod::Plague));
  CHECK_EQ(f.w().province(f.p[1])->modTurns.at(plague), schema::kPlagueTurns);
  CHECK_NEAR(f.pc(f.p[1]).fx[Fx::PopGrowthPct], -5, 1e-9);
  // Пустошь нежити чуму не принимает; повтор — нельзя.
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Province, f.p[6], ensureBuiltinMod(tx, schema::mod::UndeadWaste)); });
  CHECK(!canPlague(f.w(), f.p[6]));
  CHECK(!canPlague(f.w(), f.p[1]));
  for (int t = 0; t < 4; t++) f.tx([&](Tx& tx) { endTurn(tx); });
  // Чума закончилась сама: иммунитет в провинции, соседи (кроме пустоши) заражены.
  CHECK(!provinceHas(f.w(), f.p[1], schema::mod::Plague));
  CHECK(provinceHas(f.w(), f.p[1], schema::mod::PlagueImmunity));
  CHECK_EQ(f.w().province(f.p[1])->modTurns.at(builtinModId(f.w(), schema::mod::PlagueImmunity)), schema::kImmunityTurns);
  CHECK(provinceHas(f.w(), f.p[0], schema::mod::Plague));
  CHECK(provinceHas(f.w(), f.p[2], schema::mod::Plague));
  CHECK(provinceHas(f.w(), f.p[5], schema::mod::Plague));
  CHECK(!provinceHas(f.w(), f.p[6], schema::mod::Plague));
  // Иммунитет — чуму нельзя установить никаким путём.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Province, f.p[1], plague); }); }), "иммунитет"));
  // Снятие вручную: иммунитет и без распространения.
  f.tx([&](Tx& tx) { dropModifier(tx, ModTarget::Province, f.p[5], plague); });
  CHECK(provinceHas(f.w(), f.p[5], schema::mod::PlagueImmunity));
}

TEST(rules_tz2_heal_and_plague_buildings) {
  Fix f;
  Id heal = 0, pest = 0;
  const Id water = essNamed(f.w(), "Эссенция воды"), plagueEss = essNamed(f.w(), "Эссенция чумы");
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    tx.province(f.p[1]).owner = f.A;
    heal = building(tx, "Лечебница", f.p[0], BuildingFlag::Healing, true);
    pest = building(tx, "Чумной двор", f.p[1], BuildingFlag::Plague, true);
    tx.faction(f.A).ess[water] = 600;
    tx.faction(f.A).ess[plagueEss] = 3000;
    infectPlague(tx, f.p[0]);
  });
  CHECK(hasBuildingRole(f.w(), f.p[0], BuildingFlag::Healing));
  f.tx([&](Tx& tx) { healProvince(tx, f.p[0], heal, water); });
  CHECK(!provinceHas(f.w(), f.p[0], schema::mod::Plague));
  CHECK(provinceHas(f.w(), f.p[0], schema::mod::PlagueImmunity));
  CHECK_NEAR(f.w().faction(f.A)->essence(water), 100, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { healProvince(tx, f.p[0], heal, water); }); }), "нет чумы"));
  // «Заразить чумой» — 2500 эссенции чумы; с «Зданием чумы» чума даёт прирост +2,5 %.
  f.tx([&](Tx& tx) { plagueProvince(tx, f.p[1], pest); });
  CHECK(provinceHas(f.w(), f.p[1], schema::mod::Plague));
  CHECK_NEAR(f.w().faction(f.A)->essence(plagueEss), 500, 1e-9);
  CHECK_NEAR(f.pc(f.p[1]).fx[Fx::PopGrowthPct], schema::kPlagueBuildingGrowth, 1e-9);
}

// ---------------------------------------------------------------- генерация и лич
TEST(rules_tz2_modifier_generation) {
  Fix f;
  const Id death = essNamed(f.w(), "Эссенция смерти"), fire = essNamed(f.w(), "Эссенция пламени");
  Id hero = 0, local = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    hero = createCharacter(tx, f.A, "Мортис");
    tx.character(hero).hero = true;
    addModifier(tx, ModTarget::Character, hero, ensureBuiltinMod(tx, schema::mod::Necromancer));
    local = createModifier(tx, "Огненный источник");
    tx.modifier(local).kind = ModKind::Province;
    tx.modifier(local).essGen[fire] = 7;
    tx.modifier(local).resGen[kGold] = 2;
    addModifier(tx, ModTarget::Province, f.p[0], local);
  });
  const FactionCalc& fc = f.fc(f.A);
  CHECK_NEAR(fc.essences.at(death).generation, 10, 1e-9);   // некромант: +10 эссенции смерти
  CHECK_NEAR(fc.essences.at(fire).generation, 7, 1e-9);
  CHECK_NEAR(fc.resources.at(kGold).production, 2, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->essence(death), 10, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->essence(fire), 7, 1e-9);
  // Пленный герой не генерирует.
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, hero, ensureBuiltinMod(tx, schema::mod::Dead)); });
  CHECK(!f.fc(f.A).essences.count(death) || f.fc(f.A).essences.at(death).generation == 0);
}

TEST(rules_tz2_lich_corpses) {
  Fix f;
  Id lich = 0;
  f.tx([&](Tx& tx) {
    lich = createCharacter(tx, f.A, "Лич");
    addModifier(tx, ModTarget::Character, lich, ensureBuiltinMod(tx, schema::mod::Lich));
  });
  double got = 0;
  f.tx([&](Tx& tx) { got = detail::battleCorpses(tx, f.A, {lich}, 1000); });
  CHECK_NEAR(got, 500, 1e-9);   // 50 % за лича
  CHECK_NEAR(f.fc(f.A).essences.at(essNamed(f.w(), "Эссенция смерти")).generation, 150, 1e-9);
}

// ---------------------------------------------------------------- реликвии
TEST(rules_tz2_relic_places) {
  Fix f;
  Id store = 0, hero = 0, other = 0, find = 0, plain = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    store = building(tx, "Сокровищница", f.p[0], BuildingFlag::RelicStore, true);
    hero = createCharacter(tx, f.A, "Хранитель");
    other = createCharacter(tx, f.B, "Чужой");
    plain = addRelic(tx, "Кубок", Rarity::Rare);
    find = addRelic(tx, "Черепок", Rarity::Epic);
    for (Relic& r : tx.catalogs().relics)
      if (r.id == find) r.group = tx.w().catalogs->relicGroupId(schema::kRelicArchFinds);
  });
  CHECK(isArchFind(f.w(), find) && !isArchFind(f.w(), plain));
  CHECK(rarityAbove(Rarity::Epochal, Rarity::Legendary) && rarityAbove(Rarity::Rare, Rarity::Common));
  // Находку археологов нельзя дать герою из свободных.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { giveRelic(tx, hero, find); }); }), "археологическая находка"));
  // В хранилище — из свободных; оттуда — государству; от государства — герою (и находку тоже).
  f.tx([&](Tx& tx) { storeRelic(tx, f.p[0], store, find); });
  CHECK(relicPlace(f.w(), find).kind == RelicPlace::Building);
  f.tx([&](Tx& tx) { unstoreRelic(tx, f.p[0], store, find); });
  CHECK(relicPlace(f.w(), find).kind == RelicPlace::State && relicPlace(f.w(), find).id == f.A);
  f.tx([&](Tx& tx) { giveRelic(tx, hero, find); });
  CHECK_EQ(relicHolder(f.w(), find), hero);
  // Реликвию героя другого государства в хранилище положить нельзя.
  f.tx([&](Tx& tx) { giveRelic(tx, other, plain); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { storeRelic(tx, f.p[0], store, plain); }); }), "В хранилище"));
  // Снос хранилища: реликвии — государству-владельцу.
  f.tx([&](Tx& tx) { storeRelic(tx, f.p[0], store, find); });
  f.tx([&](Tx& tx) { demolish(tx, f.p[0], store); });
  CHECK(relicPlace(f.w(), find).kind == RelicPlace::State);
  // Удаление реликвии убирает её отовсюду.
  f.tx([&](Tx& tx) { removeRelic(tx, find); });
  CHECK(f.w().faction(f.A)->relics.empty());
}

// ---------------------------------------------------------------- наёмники
TEST(rules_tz2_mercenaries) {
  Fix f;
  Id guild = 0, row = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    tx.faction(f.A).res[kGold] = 100;
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addMercRow(tx, f.A, UnitType::HeavyInf); }); }), "Гильдия Наемников"));
  f.tx([&](Tx& tx) { guild = building(tx, "Гильдия Наемников", f.p[0], BuildingFlag::Mercenary, true); });
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

// ---------------------------------------------------------------- верфи, морские чудовища, пираты
TEST(rules_tz2_shipyard_and_sea_monster) {
  Fix f;
  Id yard = 0, monster = 0, galleon = 0, frigate = 0;
  const Id water = essNamed(f.w(), "Эссенция воды"), kraken = resNamed(f.w(), "Кракены");
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    yard = createBuilding(tx, 0, "Верфь");
    setBuildingFlag(tx, yard, BuildingFlag::Shipyard, true);
    tx.building(yard).levels.push_back(BuildingLevel{});
    setLevelShips(tx, yard, 1, (1u << int(ShipType::Galleon)) | (1u << int(ShipType::SeaMonster)));
    setLevelShips(tx, yard, 2, (1u << int(ShipType::Galleon)) | (1u << int(ShipType::Frigate)) | (1u << int(ShipType::SeaMonster)));
    placeBuilding(tx, f.p[0], yard, 1);
    monster = addFleetRow(tx, f.A, ShipType::SeaMonster);
    galleon = addFleetRow(tx, f.A, ShipType::Galleon);
    frigate = addFleetRow(tx, f.A, ShipType::Frigate);
    for (Constant* c : {&ensureConstant(tx, schema::cst::GalleonCost), &ensureConstant(tx, schema::cst::FrigateCost)}) {
      c->res.clear();
      c->minRes.clear();
    }
    tx.faction(f.A).ess[water] = 1000;
    tx.faction(f.A).res[kraken] = 1;
  });
  CHECK_EQ(shipyardAccess(f.w(), f.A), (1u << int(ShipType::Galleon)) | (1u << int(ShipType::SeaMonster)));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { recruit(tx, f.A, frigate, 1); }); }), "верфь более высокого уровня"));
  f.tx([&](Tx& tx) { recruit(tx, f.A, galleon, 2); });
  // Морское чудовище: ресурс подгруппы «Морские чудовища» и 750 эссенции воды.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { recruit(tx, f.A, monster, 1); }); }), "Морские чудовища"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setFleetRowKey(tx, f.A, monster, kGold); }); }), "Морские чудовища"));
  f.tx([&](Tx& tx) { setFleetRowKey(tx, f.A, monster, kraken); });
  f.tx([&](Tx& tx) { recruit(tx, f.A, monster, 1); });
  CHECK_NEAR(f.w().faction(f.A)->essence(water), 250, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(kraken), 0, 1e-9);
  // Базовая стоимость кораблей: меньше базовой нельзя, базовую позицию не убрать.
  const Id wood = resNamed(f.w(), "Древесина");
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setConstantRes(tx, schema::cst::ShipLineCost, wood, 10); }); }), "не меньше"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { removeConstantRes(tx, schema::cst::ShipLineCost, wood); }); }), "нельзя убрать"));
  f.tx([&](Tx& tx) { setConstantRes(tx, schema::cst::ShipLineCost, wood, 1500); });
  CHECK_NEAR(constantOf(f.w(), schema::cst::ShipLineCost).res.at(wood), 1500, 1e-9);
  // Галеоны не присоединяются к флотам на карте.
  CHECK(has(errorOf([&] {
          f.tx([&](Tx& tx) {
            tx.faction(f.A).fleet[1].total = 5;
            Id fl = createArmy(tx, ArmyKind::Fleet, f.A, kSeaNorth);
            setUnits(tx, fl, f.A, galleon, 1);
          });
        }),
        "галеоны"));
}

TEST(rules_tz2_pirates_flat_loss) {
  Fix f;
  Id gal = 0;
  f.tx([&](Tx& tx) {
    gal = addFleetRow(tx, f.A, ShipType::Galleon, "", 50, 1);
    setTradeFleet(tx, f.A, gal, 50);
    tx.faction(f.A).pirateRisk = 100;   // нападение наверняка
  });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(f.w().faction(f.A)->fleetRow(gal)->total, i64(30));   // меньше 100 галеонов — минус 20
}

// ---------------------------------------------------------------- технологии
TEST(rules_tz2_tech_cost_and_buildings) {
  Fix f;
  const Id treasure = resNamed(f.w(), "Археологические сокровища");
  Id basics = 0;
  f.w().techs.each([&](const Tech& t) {
    if (t.faction == 0 && t.prereqs.empty() && !t.needKeys.empty()) basics = t.id;
  });
  CHECK(basics != 0);
  f.tx([&](Tx& tx) { tx.faction(f.A).res[treasure] = 150; });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startResearch(tx, basics, f.A); }); }), "Гильдия Археологов"));
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    const Id g = tx.add(content::archGuildFor(f.A, {})).id;
    placeBuilding(tx, f.p[0], g, 1);
  });
  CHECK_EQ(builtWithKey(f.w(), f.A, schema::bld::ArchGuild), 1);
  f.tx([&](Tx& tx) { startResearch(tx, basics, f.A); });
  CHECK_NEAR(f.w().faction(f.A)->stock(treasure), 50, 1e-9);   // стоимость 100 сокровищ
  f.tx([&](Tx& tx) { stopResearch(tx, basics, f.A); });
  CHECK_NEAR(f.w().faction(f.A)->stock(treasure), 150, 1e-9);  // возврат
  f.tx([&](Tx& tx) { startResearch(tx, basics, f.A); });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(techStudied(f.w(), basics, f.A));
  CHECK(f.w().faction(f.A)->techs.at(basics).paid.empty());
  // Глобальный модификатор технологии: сокровища групп +1 %.
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::ArchTreasurePct], 1, 1e-9);
}

// ---------------------------------------------------------------- постройки
TEST(rules_tz2_construction_rules) {
  Fix f;
  Id a = 0, b = 0, temple = 0, cathedral = 0;
  const Id light = essNamed(f.w(), "Эссенция света");
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 8; i++) tx.province(f.p[i]).owner = f.A;
    tx.faction(f.A).res[kGold] = 1000;
    tx.faction(f.A).ess[light] = 100;
    a = createBuilding(tx, 0, "Казармы");
    b = createBuilding(tx, 0, "Амбар");
    temple = createBuilding(tx, 0, "Храм света");
    cathedral = createBuilding(tx, 0, "Собор света");
    setLevelEssCost(tx, cathedral, 1, light, 150);
    setStateReq(tx, cathedral, temple, 4);
    tx.province(f.p[0]).size = ProvSize::Large;
    tx.province(f.p[0]).city = CityType::City;
  });
  // Одна стройка в провинции.
  f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], a); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], b); }); }), "одновременно"));
  // Собор: 4 храма на каждый, эссенция в цене.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startBuilding(tx, f.p[1], cathedral); }); }), "Нужно 4"));
  f.tx([&](Tx& tx) {
    for (int i = 1; i <= 4; i++) placeBuilding(tx, f.p[i], temple, 1);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startBuilding(tx, f.p[1], cathedral); }); }), "эссенции"));
  f.tx([&](Tx& tx) { tx.faction(f.A).ess[light] = 200; });
  f.tx([&](Tx& tx) { startBuilding(tx, f.p[1], cathedral); });
  CHECK_NEAR(f.w().faction(f.A)->essence(light), 50, 1e-9);
  f.tx([&](Tx& tx) { cancelBuilding(tx, f.p[1], cathedral); });
  CHECK_NEAR(f.w().faction(f.A)->essence(light), 200, 1e-9);   // возврат эссенции
  f.tx([&](Tx& tx) { placeBuilding(tx, f.p[1], cathedral, 1); });
  // Второй собор требует 8 храмов.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startBuilding(tx, f.p[2], cathedral); }); }), "Нужно 8"));
  // Приморская постройка.
  CHECK(isCoastal(f.w(), f.p[1]));
  CHECK(seaNeighbors(f.w(), f.p[1]).empty());
  Id north = 0;
  f.tx([&](Tx& tx) { north = rules::createProvince(tx, rect(60, 20, 940, 99), Terrain::Sea).province; });
  CHECK((seaNeighbors(f.w(), f.p[1]) == std::vector<Id>{north}));
  CHECK(seaNeighbors(f.w(), f.p[5]).empty());
}

// ---------------------------------------------------------------- совет
TEST(rules_tz2_council_influence) {
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
    if (a.key == schema::mod::CouncilInfluence) influence = a.scale == 5;
  CHECK(influence);
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::ResearchTimePct], -10, 1e-9);   // −2 % × 5 должностей
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 5; i++) addCouncilSeat(tx, f.A, "Ещё " + std::to_string(i));
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addCouncilSeat(tx, f.A, "Лишняя"); }); }), "не больше 10"));
}

// ---------------------------------------------------------------- войска без государства
TEST(rules_tz2_wild_armies) {
  Fix f;
  Id wild = 0, mine = 0, row = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[1]).owner = f.B;
    wild = spawnWildArmy(tx, center(1), "Древние стражи", {{"Древние стражи", UnitType::HeavyInf, schema::kRaceMechanical, 5000}});
    row = addArmyRow(tx, f.A, UnitType::LightInf, "", 100);
    mine = createArmy(tx, ArmyKind::Army, f.A, center(5));
    setUnits(tx, mine, f.A, row, 100);
  });
  const Id wf = wildFaction(f.w());
  CHECK(wf != 0 && f.w().faction(wf)->isWild());
  CHECK(f.w().relation(f.A, wf).s == RelStatus::War);
  CHECK(encounter(f.w(), mine, f.w().army(wild)->pos).type == EncounterType::Battle);
  CHECK(!canSiege(f.w(), wild, f.p[1]));
  for (const RelationRow& r : relationsOf(f.w(), f.A)) CHECK(r.other != wf);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelation(tx, f.A, wf, 50, RelStatus::Alliance); }); }), "без государства"));
}

// ---------------------------------------------------------------- одна религия
TEST(rules_tz2_same_religion_relation) {
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

// ---------------------------------------------------------------- археологические группы: показатели
TEST(rules_tz2_arch_stats_and_upkeep) {
  Fix f;
  Id mod = 0;
  f.tx([&](Tx& tx) {
    mod = createModifier(tx, "Проводники");
    Modifier& m = tx.modifier(mod);
    m.kind = ModKind::ArchGroup;
    m.fx[size_t(Fx::ArchSuccessPct)] = 5;
    m.fx[size_t(Fx::ArchUpkeepPct)] = 50;
    m.fxMask = (1u << int(Fx::ArchSuccessPct)) | (1u << int(Fx::ArchUpkeepPct));
    ArchGroup g;
    g.id = tx.nextId(Seq::ArchGroup);
    g.name = "Экспедиция";
    g.exp = 460;   // 4 уровень
    g.modifiers = {mod};
    tx.faction(f.A).archGroups.push_back(g);
  });
  const ArchGroup& g = f.w().faction(f.A)->archGroups[0];
  const ArchStats st = archStats(f.w(), f.A, g.id);
  CHECK_EQ(st.level, 4);
  CHECK_NEAR(st.success, 15, 1e-9);   // 10 % уровня + 5 %
  CHECK_NEAR(st.vitality, 10, 1e-9);
  CHECK_NEAR(st.upkeep, 0.6, 1e-9);   // 0,4 × 1,5
  CHECK_NEAR(f.fc(f.A).expArch, 0.6, 1e-9);
  CHECK_EQ(archStartLevel(f.w(), f.A), 1);
  CHECK_EQ(arch::levelOf(100), 1);
  CHECK_EQ(arch::levelOf(101), 2);
  CHECK_EQ(arch::levelOf(1000), 5);
  CHECK_NEAR(arch::stageSuccess(3, 8, 0), 40 - 21, 1e-9);
}

// ---------------------------------------------------------------- новые государства и провинции
TEST(rules_tz2_new_state_and_province_content) {
  Fix f;
  Id s = 0, p = 0;
  f.tx([&](Tx& tx) {
    s = createFaction(tx, FactionKind::State, "Новое");
    p = splitProvince(tx, f.p[0], {{150, 50}, {150, 550}});
  });
  int guilds = 0;
  f.w().buildings.each([&](const Building& b) { guilds += b.owner == s && b.key == schema::bld::ArchGuild && b.cat == BuildingCat::Cult; });
  CHECK_EQ(guilds, 1);
  CHECK(has(errorOf([&] {
          f.tx([&](Tx& tx) {
            f.w().buildings.each([&](const Building& b) {
              if (b.owner == s) removeBuilding(tx, b.id);
            });
          });
        }),
        "постоянная"));
  for (const ArchSlot& sl : f.w().province(p)->arch) CHECK(sl.site != 0);
}
