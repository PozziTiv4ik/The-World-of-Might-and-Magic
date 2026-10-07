// Regnum — тесты правил по ТЗ 2026-10-07: группы ресурсов и провизия, ключевой ресурс и цены найма, звери и
// элементали без населения, эссенции элементов, особые отряды и постройки доступа, постройки преобразования и
// генерации эссенции, культовые постройки, требования построек к технологиям, общее дерево технологий, мгновенное
// завершение и изначальные постройки, гарнизон с героями и верностью, мятеж гарнизона, реликвии, базовые записи.
#include "core/content.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

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
Id res(const World& w, const char* name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}
Id ess(const World& w, const char* name) {
  for (const CatalogItem& c : w.catalogs->essences)
    if (c.name == name) return c.id;
  return 0;
}
Id grp(const World& w, const char* key) { return w.catalogs->groupId(key); }
template <class V, class X> bool contains(const V& v, const X& x) { return std::find(v.begin(), v.end(), x) != v.end(); }
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

}  // namespace

// ---------------------------------------------------------------- базовые записи
TEST(rules_mech_new_world_content) {
  World w = newWorld("Мир");
  CHECK_EQ(w.meta->content, content::kVersion);
  const auto& pos = w.catalogs->positions;
  CHECK_EQ(pos.size(), size_t(10));
  CHECK_EQ(pos[0].name, std::string("Десница"));
  CHECK_EQ(pos[5].name, std::string("Хранитель знаний"));
  CHECK_EQ(pos[9].name, std::string("Верховный друид"));
  CHECK_EQ(w.catalogs->essences.size(), size_t(18));
  CHECK_EQ(w.catalogs->religions.size(), size_t(61));
  CHECK_EQ(w.catalogs->resGroups.size(), content::baseGroups().size());
  CHECK_EQ(resourcesIn(w, grp(w, schema::grp::OreCommon)).size(), size_t(8));
  CHECK_EQ(resourcesIn(w, grp(w, schema::grp::OreSpecial)).size(), size_t(7));
  CHECK_EQ(resourcesIn(w, grp(w, schema::grp::Ore)).size(), size_t(15));   // группа — с подгруппами
  CHECK_EQ(resourcesIn(w, grp(w, schema::grp::MountsGround)).size(), size_t(23));
  CHECK_EQ(resourcesIn(w, grp(w, schema::grp::MountsFlying)).size(), size_t(12));
  CHECK_EQ(resourcesIn(w, grp(w, schema::grp::WarBeasts)).size(), size_t(15));
  CHECK_EQ(resourcesIn(w, grp(w, schema::grp::Monsters)).size(), size_t(31));
  CHECK_EQ(resourcesIn(w, grp(w, schema::grp::Beasts)).size(), size_t(81));
  CHECK_EQ(provisionResources(w).size(), size_t(17));
  CHECK_EQ(resourcesIn(w, grp(w, schema::grp::Materials)).size(), size_t(29));
  // Трупы и демоническая энергия — сразу в справочнике (начальные запасы задаются без первого начисления).
  CHECK(w.catalogs->resourceId(schema::kResCorpses) && w.catalogs->resourceId(schema::kResEnergy));
  const Id parts = w.catalogs->resourceId(schema::kResMechParts);
  CHECK(parts && w.resource(parts)->name == "Запчасти механизмов" && w.catalogs->resourceIn(parts, grp(w, schema::grp::MatIndustrial)));
  // «Драконы Бездны» и «Цитадель Бездны» (общее дерево).
  CHECK_EQ(w.catalogs->specials.size(), size_t(1));
  const SpecialUnit& d = w.catalogs->specials[0];
  CHECK_EQ(d.name, std::string("Драконы Бездны"));
  CHECK(d.type == UnitType::Monsters);
  CHECK_EQ(d.keyRes, res(w, "Черные Драконы"));
  CHECK_NEAR(d.extra.at(res(w, "Обсидиановая сталь")), 100, 1e-12);
  CHECK_NEAR(d.essence.at(ess(w, "Эссенция бездны")), 1000, 1e-12);
  bool citadel = false;
  w.buildings.each([&](const Building& b) {
    if (b.name == "Цитадель Бездны" && b.owner == 0 && b.specialAccess && b.specials == std::vector<Id>{d.id}) citadel = true;
  });
  CHECK(citadel);
}

// ---------------------------------------------------------------- группы ресурсов
TEST(rules_mech_resource_groups) {
  Fix f;
  Id metals = 0, light = 0, heavy = 0, gold = 0, iron = 0, lead = 0;
  f.tx([&](Tx& tx) {
    metals = addResGroup(tx, "Металлы");
    light = addResGroup(tx, "Лёгкие", metals);
    heavy = addResGroup(tx, "Тяжёлые", metals);
    gold = addCatalogItem(tx, CatalogList::Resources, "Алюминий");
    iron = addCatalogItem(tx, CatalogList::Resources, "Свинец");
    lead = addCatalogItem(tx, CatalogList::Resources, "Сплав");
    setResourceGroup(tx, gold, light);
    setResourceGroup(tx, iron, heavy);
    setResourceGroup(tx, lead, metals);
  });
  CHECK((resourcesIn(f.w(), light) == std::vector<Id>{gold}));   // подгруппа — только её ресурсы
  CHECK((resourcesIn(f.w(), metals) == std::vector<Id>{gold, iron, lead}));
  CHECK_EQ(groupPath(f.w(), heavy), std::string("Металлы / Тяжёлые"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setGroupParent(tx, metals, light); }); }), "в свою подгруппу"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addResGroup(tx, "Лёгкие", metals); }); }), "уже есть"));
  // Удаление группы: подгруппы и ресурсы — в родителя.
  f.tx([&](Tx& tx) { removeResGroup(tx, light); });
  CHECK_EQ(f.w().resource(gold)->group, metals);
  f.tx([&](Tx& tx) { removeResGroup(tx, metals); });
  CHECK_EQ(f.w().catalogs->group(heavy)->parent, Id(0));
  CHECK_EQ(f.w().resource(lead)->group, Id(0));
  // Группы правил удалить нельзя.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { removeResGroup(tx, grp(tx.w(), schema::grp::Provisions)); }); }), "нельзя удалить"));
}

// ---------------------------------------------------------------- ключевой ресурс и найм
TEST(rules_mech_key_resource_rules) {
  Fix f;
  const World& w = f.w();
  const Id horse = res(w, "Лошади"), griffin = res(w, "Грифоны"), elephant = res(w, "Слоны"), hydra = res(w, "Гидры");
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
  CHECK(!keyAllowed(w, UnitType::Machines, res(w, "Сталь")));
  CHECK_EQ(keyResources(w, UnitType::LightCav).size(), size_t(23));
  CHECK_EQ(keyResources(w, UnitType::Beasts).size(), size_t(50));
  CHECK(keyResources(w, UnitType::LightInf).empty());
  CHECK(!schema::needsKeyResource(UnitType::Flying));
}

TEST(rules_mech_recruit_cavalry_beasts_machines) {
  Fix f;
  Id race = 0, cav = 0, beasts = 0, mech = 0;
  const Id horse = res(f.w(), "Лошади"), elephant = res(f.w(), "Слоны"), parts = f.w().catalogs->resourceId(schema::kResMechParts);
  const Id leather = res(f.w(), "Кожа"), fire = ess(f.w(), "Эссенция пламени");
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

TEST(rules_mech_elementals) {
  Fix f;
  Id row = 0, race = 0;
  const Id water = ess(f.w(), "Эссенция воды"), ice = ess(f.w(), "Эссенция льда");
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 500);
    row = addArmyRow(tx, f.A, UnitType::Elementals, "", 0, 7);
  });
  CHECK_EQ(f.w().faction(f.A)->armyRow(row)->race, std::string(schema::kRaceElemental));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRowRace(tx, f.A, row, schema::kRaceLiving); }); }), "только «Элементали»"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRowExtra(tx, f.A, row, res(tx.w(), "Сталь"), 1); }); }), "только за эссенции"));
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

// ---------------------------------------------------------------- особые отряды
TEST(rules_mech_special_units) {
  Fix f;
  const World& w0 = f.w();
  const Id dragons = res(w0, "Черные Драконы"), steel = res(w0, "Обсидиановая сталь"), abyss = ess(w0, "Эссенция бездны");
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

// ---------------------------------------------------------------- постройки: преобразование, эссенция, культ
TEST(rules_mech_conversion_building) {
  Fix f;
  const Id iron = 5, wood = res(f.w(), "Древесина"), steel = res(f.w(), "Сталь");
  Id mill = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    mill = createBuilding(tx, 0, "Сталеварня");
    setBuildingRole(tx, mill, BuildingRole::Convert, true);
    setRecipe(tx, mill, Recipe{{ResAmount{iron, 2}, ResAmount{wood, 1}}, ResAmount{steel, 1}, 2});
    placeBuilding(tx, f.p[0], mill, 1);
    tx.faction(f.A).res[iron] = 3;
    tx.faction(f.A).res[wood] = 5;
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRecipe(tx, mill, Recipe{{ResAmount{iron, 1}, ResAmount{iron, 1}}, ResAmount{steel, 1}, 1}); }); }),
            "разными"));
  // Ход 1: цикл начинается (ресурсы списаны), ход 2: сталь.
  CHECK_NEAR(f.fc(f.A).resources.at(iron).conversionIn, 2, 1e-12);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 1, 1e-12);
  CHECK_NEAR(f.w().faction(f.A)->stock(wood), 4, 1e-12);
  CHECK_EQ(f.w().province(f.p[0])->buildings[0].cycle, 1);
  CHECK_NEAR(f.fc(f.A).resources.at(steel).conversionOut, 1, 1e-12);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(steel), 1, 1e-12);
  // Не хватает железа — ждёт; простаивает — цикл на паузе.
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 1, 1e-12);
  CHECK_EQ(f.w().province(f.p[0])->buildings[0].cycle, 0);
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[iron] = 10;
    setConvertIdle(tx, f.p[0], mill, true);
  });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 10, 1e-12);
  f.tx([&](Tx& tx) { setConvertIdle(tx, f.p[0], mill, false); });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 8, 1e-12);
  // Выключение возможности снимает рецепт и цикл в провинциях.
  f.tx([&](Tx& tx) { setBuildingRole(tx, mill, BuildingRole::Convert, false); });
  CHECK(f.w().building(mill)->recipe.in.empty());
  CHECK_EQ(f.w().province(f.p[0])->buildings[0].cycle, 0);
}

TEST(rules_mech_essence_building_and_cult) {
  Fix f;
  const Id light = ess(f.w(), "Эссенция света");
  Id shrine = 0, wonder = 0, own = 0;
  f.tx([&](Tx& tx) {
    for (int i : {0, 1, 2}) tx.province(f.p[i]).owner = i < 2 ? f.A : f.B;
    shrine = createBuilding(tx, 0, "Святилище света");
    tx.building(shrine).cat = BuildingCat::Religious;
    setBuildingRole(tx, shrine, BuildingRole::Essence, true);
    setLevelEssence(tx, shrine, 1, light, 4);
    wonder = createBuilding(tx, 0, "Храм Вечности");
    tx.building(wonder).cat = BuildingCat::Cult;
    own = createBuilding(tx, f.A, "Идол Ардена");
    tx.building(own).cat = BuildingCat::Cult;
    placeBuilding(tx, f.p[0], shrine, 1);
  });
  CHECK_NEAR(f.pc(f.p[0]).essence.at(light), 4, 1e-12);
  CHECK_NEAR(f.fc(f.A).essences.at(light).generation, 4, 1e-12);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->essence(light), 4, 1e-12);
  // Общая культовая — одна на всю карту, уникальная — одна у своего государства.
  f.tx([&](Tx& tx) {
    placeBuilding(tx, f.p[0], wonder, 1);
    placeBuilding(tx, f.p[0], own, 1);
  });
  auto opts = buildOptions(f.w(), f.p[2]);
  auto it = std::find_if(opts.begin(), opts.end(), [&](const BuildOption& o) { return o.building == wonder; });
  CHECK(it != opts.end() && !it->can && has(it->reasons.front(), "одна на всю карту"));
  opts = buildOptions(f.w(), f.p[1]);
  it = std::find_if(opts.begin(), opts.end(), [&](const BuildOption& o) { return o.building == own; });
  CHECK(it != opts.end() && !it->canPlace && has(it->placeReasons.front(), "одна на государство"));
  CHECK_EQ(cultBuiltIn(f.w(), wonder), f.p[0]);
}

TEST(rules_mech_building_requirements) {
  Fix f;
  Id commonB = 0, uniqA = 0, uniqB = 0, commonT = 0, techA = 0, techB = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    commonB = createBuilding(tx, 0, "Кузница");
    uniqA = createBuilding(tx, f.A, "Храм Ардена");
    uniqB = createBuilding(tx, f.B, "Храм Бельмара");
    commonT = createTech(tx, 0, "Письменность");
    techA = createTech(tx, f.A, "Тайны Ардена");
    techB = createTech(tx, f.B, "Тайны Бельмара");
  });
  // Общая постройка — только от общих построек и технологий; уникальная — ещё и от своих.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setBuildingReq(tx, commonB, uniqA, 1); }); }), "Общая постройка не может зависеть"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setBuildingReq(tx, uniqA, uniqB, 1); }); }), "своего государства"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setBuildingTech(tx, commonB, techA, true); }); }), "только общие"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setBuildingTech(tx, uniqA, techB, true); }); }), "своего государства"));
  f.tx([&](Tx& tx) {
    setBuildingReq(tx, uniqA, commonB, 1);
    setBuildingTech(tx, uniqA, commonT, true);
    setBuildingTech(tx, uniqA, techA, true);
    setBuildingTech(tx, commonB, commonT, true);
  });
  CHECK((techUnlocks(f.w(), commonT) == std::vector<Id>{commonB, uniqA}));
  auto opts = buildOptions(f.w(), f.p[0]);
  auto opt = [&](Id b) { return *std::find_if(opts.begin(), opts.end(), [&](const BuildOption& o) { return o.building == b; }); };
  CHECK(has(opt(commonB).reasons.front(), "Нужна технология «Письменность»"));
  // Общую технологию изучает каждое государство само.
  f.tx([&](Tx& tx) { setStudied(tx, commonT, true, f.A); });
  CHECK(techStudied(f.w(), commonT, f.A));
  CHECK(!techStudied(f.w(), commonT, f.B));
  opts = buildOptions(f.w(), f.p[0]);
  CHECK(opt(commonB).can);
  CHECK(has(opt(uniqA).reasons.front(), "Нужна постройка «Кузница»"));
  // Удаление технологии снимает требования построек.
  f.tx([&](Tx& tx) { removeTech(tx, commonT); });
  CHECK(f.w().building(commonB)->techs.empty());
  CHECK(!f.w().faction(f.A)->techs.count(commonT));
}

// ---------------------------------------------------------------- общее дерево технологий
TEST(rules_mech_common_tech_tree) {
  Fix f;
  Id write = 0, math = 0, own = 0, m = 0;
  f.tx([&](Tx& tx) {
    write = createTech(tx, 0, "Письменность");
    math = createTech(tx, 0, "Математика");
    tx.tech(math).turns = 2;
    own = createTech(tx, f.A, "Астрология Ардена");
    m = makeMod(tx, {{Fx::IncomePct, 10}});
    tx.tech(write).modifiers = {m};
    setPrereq(tx, math, write, true);
    setPrereq(tx, own, math, true);   // уникальная — от общей
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setPrereq(tx, write, own, true); }); }), "Общая технология не может зависеть"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setStudied(tx, write, true); }); }), "выберите государство"));
  CHECK(!canResearch(f.w(), math, f.A).ok);
  f.tx([&](Tx& tx) { setStudied(tx, write, true, f.A); });
  CHECK_NEAR(f.fc(f.A).incomePct, 10, 1e-12);   // эффект изученной общей технологии — только изучившему
  CHECK_NEAR(f.fc(f.B).incomePct, 0, 1e-12);
  CHECK(canResearch(f.w(), math, f.A).ok);
  CHECK(!canResearch(f.w(), math, f.B).ok);
  f.tx([&](Tx& tx) { startResearch(tx, math, f.A); });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(techState(f.w(), math, f.A).progress, 1);
  CHECK(!techStudied(f.w(), math, f.A));
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(techStudied(f.w(), math, f.A));
  CHECK(canResearch(f.w(), own).ok);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setStudied(tx, write, false, f.A); }); }), "зависят изученные"));
  // Копия дерева: условия-общие технологии остаются общими.
  f.tx([&](Tx& tx) { copyTechTree(tx, f.A, f.C); });
  bool copied = false;
  f.w().techs.each([&](const Tech& t) {
    if (t.faction == f.C && t.name == "Астрология Ардена") copied = t.prereqs == std::vector<Id>{math};
  });
  CHECK(copied);
}

// ---------------------------------------------------------------- мгновенно и изначально
TEST(rules_mech_instant_and_initial_buildings) {
  Fix f;
  Id farm = 0, mill = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    farm = createBuilding(tx, 0, "Ферма");
    tx.building(farm).levels = {BuildingLevel{3, {{kGold, 10}}, {}, ""}, BuildingLevel{4, {{kGold, 20}}, {}, ""}};
    mill = createBuilding(tx, 0, "Мельница");
    tx.building(mill).requires_ = {BuildingReq{farm, 2}};
    tx.faction(f.A).res[kGold] = 20;
  });
  // Изначальная постройка — без цены и срока, но с требованиями.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { placeBuilding(tx, f.p[0], mill, 1); }); }), "Нужна постройка «Ферма» уровня 2"));
  f.tx([&](Tx& tx) { placeBuilding(tx, f.p[0], farm, 2); });
  CHECK_EQ(f.w().province(f.p[0])->buildings[0].builtLevel(), 2);
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 20, 1e-12);
  f.tx([&](Tx& tx) { placeBuilding(tx, f.p[0], mill, 1); });
  // Мгновенное завершение строящегося уровня.
  f.tx([&](Tx& tx) {
    placeBuilding(tx, f.p[0], farm, 1);
    startBuilding(tx, f.p[0], farm);
  });
  CHECK(f.w().province(f.p[0])->buildings[0].constructing);
  f.tx([&](Tx& tx) { completeBuilding(tx, f.p[0], farm); });
  CHECK(!f.w().province(f.p[0])->buildings[0].constructing);
  CHECK_EQ(f.w().province(f.p[0])->buildings[0].level, 2);
  CHECK(f.w().province(f.p[0])->buildings[0].paid.empty());
}

// ---------------------------------------------------------------- гарнизон
TEST(rules_mech_garrison_heroes_and_loyalty) {
  Fix f;
  Id race = 0, row = 0, hero = 0, other = 0, army = 0;
  f.tx([&](Tx& tx) {
    race = addRace(tx);
    own(tx, f.p[0], f.A, race, 1000);
    row = addArmyRow(tx, f.A, UnitType::LightInf, "Стража", 100, 0);
    setGarrison(tx, f.p[0], row, 60);
    hero = createCharacter(tx, f.A, "Капитан стражи");
    other = createCharacter(tx, f.B, "Чужак");
    army = createArmy(tx, ArmyKind::Army, f.A, center(1));
    setUnits(tx, army, f.A, row, 10);
  });
  f.tx([&](Tx& tx) { setGarrisonHero(tx, f.p[0], hero, true); });
  CHECK_EQ(heroGarrison(f.w(), hero), f.p[0]);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHero(tx, army, hero, true); }); }), "в гарнизоне"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setGarrisonHero(tx, f.p[0], other, true); }); }), "только герои"));
  // Верность гарнизона меняется эффектами государства («Децентрализация» −5 за ход).
  f.tx([&](Tx& tx) { setFx(tx, schema::mod::Decentralization, {{Fx::LoyaltyPerTurn, -5}}); });
  CHECK_NEAR(garrisonLoyaltyDelta(f.w(), f.p[0]), -5, 1e-12);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().province(f.p[0])->garrisonLoyalty, 95, 1e-12);
  // «Непреклонный лоялист» в гарнизоне: верность не падает и растёт на 5 %.
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, hero, ensureBuiltinMod(tx, schema::mod::Loyalist)); });
  CHECK_NEAR(garrisonLoyaltyDelta(f.w(), f.p[0]), 5, 1e-12);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setGarrisonLoyalty(tx, f.p[0], 50); }); }), "лоялист"));
  // Мёртвый герой снимается с гарнизона.
  f.tx([&](Tx& tx) { heroFate(tx, hero, Fate::Killed, 0, f.p[0]); });
  CHECK(f.w().province(f.p[0])->garrisonHeroes.empty());
  // Смена владельца снимает героев и верность.
  f.tx([&](Tx& tx) {
    Id h2 = createCharacter(tx, f.A, "Второй");
    setGarrisonHero(tx, f.p[0], h2, true);
    setGarrisonLoyalty(tx, f.p[0], -30);
    setProvinceOwner(tx, f.p[0], f.B);
  });
  CHECK(f.w().province(f.p[0])->garrisonHeroes.empty());
  CHECK_NEAR(f.w().province(f.p[0])->garrisonLoyalty, 100, 1e-12);
}

TEST(rules_mech_garrison_mutiny) {
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

// ---------------------------------------------------------------- реликвии
TEST(rules_mech_relics) {
  Fix f;
  Id a = 0, b = 0, crown = 0;
  f.tx([&](Tx& tx) {
    a = createCharacter(tx, f.A, "Король");
    b = createCharacter(tx, f.B, "Маг");
    crown = addRelic(tx, "Корона Вечности", Rarity::Legendary);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addRelic(tx, "корона вечности"); }); }), "уникальна"));
  f.tx([&](Tx& tx) { giveRelic(tx, a, crown); });
  CHECK_EQ(relicHolder(f.w(), crown), a);
  f.tx([&](Tx& tx) { giveRelic(tx, b, crown); });   // реликвия одна — переходит
  CHECK(f.w().character(a)->inventory.empty());
  CHECK((f.w().character(b)->inventory == std::vector<Id>{crown}));
  f.tx([&](Tx& tx) { removeRelic(tx, crown); });
  CHECK(f.w().character(b)->inventory.empty());
}
