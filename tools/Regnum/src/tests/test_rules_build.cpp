// Regnum — правила построек: варианты строительства и причины отказа, стройка, отмена, улучшение и снос, требования
// построек к постройкам и технологиям, изначальные постройки и мгновенное завершение, одна стройка в провинции,
// эссенции в цене, требования «на государство», приморские постройки, постройки преобразования, генерации эссенции и
// культовые, потеря слотов.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

const ProvBuilding* pbOf(const World& w, Id province, Id building) {
  for (auto& b : w.province(province)->buildings)
    if (b.building == building) return &b;
  return nullptr;
}

const BuildOption* opt(const std::vector<BuildOption>& v, Id b) {
  for (auto& o : v)
    if (o.building == b) return &o;
  return nullptr;
}

bool hasReason(const BuildOption* o, const std::string& sub) {
  if (!o) return false;
  for (auto& r : o->reasons)
    if (has(r, sub)) return true;
  return false;
}

// p0 у A (строительство на 50 % дороже), запасы A; постройки: ферма (2 уровня с бонусами торговли), кузница (нужна
// ферма 2-го уровня), арсенал, уникальные храмы A и B.
struct BuildSetup {
  Fix f;
  Id farm = 0, smith = 0, exp = 0, uniqA = 0, uniqB = 0, mFarm1 = 0, mFarm2 = 0;
  BuildSetup() {
    f.tx([&](Tx& tx) {
      Province& p = tx.province(f.p[0]);
      p.owner = f.A;
      p.modifiers = {makeMod(tx, {{Fx::BuildCostPct, 50}})};
      Faction& a = tx.faction(f.A);
      a.res[kGold] = 100;
      a.res[5] = 30;
      a.res[3] = 50;
      mFarm1 = makeMod(tx, {{Fx::TradeFlat, 10}});
      mFarm2 = makeMod(tx, {{Fx::TradeFlat, 25}});
      farm = createBuilding(tx, 0, "Ферма");
      Building& fb = tx.building(farm);
      fb.cat = BuildingCat::Economic;
      fb.levels = {BuildingLevel{2, {{3, 20}}, {mFarm1}, ""}, BuildingLevel{3, {{3, 40}}, {mFarm2}, ""}};
      smith = createBuilding(tx, 0, "Кузница");
      tx.building(smith).cat = BuildingCat::Industrial;
      tx.building(smith).requires_ = {BuildingReq{farm, 2}};
      tx.building(smith).levels[0].cost = {{5, 10}};
      exp = createBuilding(tx, 0, "Арсенал");
      tx.building(exp).cat = BuildingCat::Military;
      tx.building(exp).levels[0].cost = {{5, 40}};
      uniqA = createBuilding(tx, f.A, "Храм Ардена");
      uniqB = createBuilding(tx, f.B, "Храм Бельмара");
    });
  }
};

}  // namespace

// ---------------------------------------------------------------- варианты и жизненный цикл
TEST(rules_build_options) {
  BuildSetup s;
  Fix& f = s.f;
  auto opts = buildOptions(f.w(), f.p[0]);
  CHECK_EQ(opts.size(), size_t(4));  // уникальная постройка другого государства не предлагается
  CHECK(!opt(opts, s.uniqB));
  CHECK_EQ(opts[0].building, s.exp);  // по категориям: военные первыми
  const BuildOption* farm = opt(opts, s.farm);
  CHECK(farm->can);
  CHECK_EQ(farm->level, 1);
  CHECK(!farm->upgrade);
  CHECK_EQ(farm->turns, 2);
  CHECK_NEAR(farm->cost.at(3), 30, 1e-9);  // 20 × 1,5
  CHECK(hasReason(opt(opts, s.smith), "Нужна постройка «Ферма» уровня 2"));
  CHECK(hasReason(opt(opts, s.exp), "Недостаточно ресурса «Железо»: нужно 60, есть 30"));
  CHECK(opt(opts, s.uniqA)->can);
  // Без владельца и в море.
  auto none = buildOptions(f.w(), f.p[1]);
  CHECK(hasReason(opt(none, s.farm), "нет владельца"));
  CHECK(!opt(none, s.uniqA));
  auto sea = buildOptions(f.w(), f.seaW);
  CHECK(hasReason(opt(sea, s.farm), "морской"));
  // Слоты: средняя провинция + деревня (население 1000–10 000) = 3.
  f.tx([&](Tx& tx) {
    setCity(tx, f.p[0], CityType::Village);
    for (int i = 0; i < 3; i++) {
      Id b = createBuilding(tx, 0, "Заполнитель " + std::to_string(i));
      tx.province(f.p[0]).buildings.push_back(ProvBuilding{b, 1, false, 0});
    }
  });
  opts = buildOptions(f.w(), f.p[0]);
  CHECK(hasReason(opt(opts, s.farm), "Нет свободных слотов: занято 3 из 3"));
  CHECK(buildOptions(f.w(), 9999).empty());
}

TEST(rules_build_lifecycle) {
  BuildSetup s;
  Fix& f = s.f;
  f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], s.farm); });
  CHECK_NEAR(f.w().faction(f.A)->stock(3), 20, 1e-9);
  const ProvBuilding* pb = pbOf(f.w(), f.p[0], s.farm);
  CHECK(pb && pb->constructing && pb->level == 1 && pb->left == 2);
  CHECK(has(lastLog(f.w()), "Начато строительство «Ферма» в провинции «П0»: 2 хода"));
  CHECK_EQ(f.pc(f.p[0]).slotsUsed, 1);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], s.farm); }); }), "Уже строится"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { demolish(tx, f.p[0], s.farm); }); }), "сначала отмените"));
  // Отмена — полный возврат.
  f.tx([&](Tx& tx) { cancelBuilding(tx, f.p[0], s.farm); });
  CHECK_NEAR(f.w().faction(f.A)->stock(3), 50, 1e-9);
  CHECK(!pbOf(f.w(), f.p[0], s.farm));
  CHECK(has(lastLog(f.w()), "стоимость возвращена"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { cancelBuilding(tx, f.p[0], s.farm); }); }), "нет в провинции"));
  // Строительство завершается через указанное число ходов; бонус — только после завершения.
  f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], s.farm); });
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::TradeFlat], 0, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(pbOf(f.w(), f.p[0], s.farm)->left, 1);
  f.tx([&](Tx& tx) { endTurn(tx); });
  pb = pbOf(f.w(), f.p[0], s.farm);
  CHECK(!pb->constructing);
  CHECK_EQ(pb->builtLevel(), 1);
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::TradeFlat], 10, 1e-9);
  // Улучшение: во время строительства действуют бонусы прежнего уровня.
  f.tx([&](Tx& tx) { tx.faction(f.A).res[3] = 100; });
  auto up = *opt(buildOptions(f.w(), f.p[0]), s.farm);
  CHECK(up.upgrade);
  CHECK_EQ(up.level, 2);
  CHECK_NEAR(up.cost.at(3), 60, 1e-9);
  f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], s.farm); });
  pb = pbOf(f.w(), f.p[0], s.farm);
  CHECK(pb->constructing && pb->level == 2 && pb->left == 3 && pb->builtLevel() == 1);
  CHECK_NEAR(f.w().faction(f.A)->stock(3), 40, 1e-9);
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::TradeFlat], 10, 1e-9);
  CHECK(has(lastLog(f.w()), "(уровень 2)"));
  f.tx([&](Tx& tx) { cancelBuilding(tx, f.p[0], s.farm); });  // отмена улучшения: остаётся 1-й уровень
  pb = pbOf(f.w(), f.p[0], s.farm);
  CHECK(!pb->constructing && pb->level == 1);
  CHECK_NEAR(f.w().faction(f.A)->stock(3), 100, 1e-9);
  f.tx([&](Tx& tx) {
    startBuilding(tx, f.p[0], s.farm);
    for (int i = 0; i < 3; i++) endTurn(tx);
  });
  CHECK_EQ(pbOf(f.w(), f.p[0], s.farm)->builtLevel(), 2);
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::TradeFlat], 25, 1e-9);  // уровень заменяет бонусы
  CHECK(hasReason(opt(buildOptions(f.w(), f.p[0]), s.farm), "наибольший уровень"));
  // Требование выполнено — кузница доступна.
  CHECK(opt(buildOptions(f.w(), f.p[0]), s.smith)->can);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], s.exp); }); }), "Недостаточно ресурса «Железо»"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startBuilding(tx, f.p[0], s.uniqB); }); }), "уникальная постройка другого"));
  f.tx([&](Tx& tx) { demolish(tx, f.p[0], s.farm); });
  CHECK(!pbOf(f.w(), f.p[0], s.farm));
  CHECK(has(lastLog(f.w()), "Снесена постройка «Ферма»"));
}

// ---------------------------------------------------------------- требования, изначальные постройки, мгновенное завершение
TEST(rules_build_requirements) {
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

TEST(rules_build_instant_and_initial) {
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

// ---------------------------------------------------------------- одна стройка, эссенции в цене, «на государство», приморские
TEST(rules_build_construction_rules) {
  Fix f;
  Id a = 0, b = 0, temple = 0, cathedral = 0;
  const Id light = test::idByName(f.w().catalogs->essences, "Эссенция света");
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
    setCity(tx, f.p[0], CityType::City);
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

// ---------------------------------------------------------------- преобразование, эссенция, культ
TEST(rules_build_conversion) {
  Fix f;
  const Id iron = 5, wood = test::idByName(f.w().catalogs->resources, "Древесина"), steel = test::idByName(f.w().catalogs->resources, "Сталь");
  Id mill = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    mill = createBuilding(tx, 0, "Сталеварня");
    setBuildingRole(tx, mill, BuildingRole::Convert, true);
    setRecipe(tx, mill, 0, Recipe{"", {ResAmount{iron, 2}, ResAmount{wood, 1}}, {}, {ResAmount{steel, 1}}, 2});
    placeBuilding(tx, f.p[0], mill, 1);
    tx.faction(f.A).res[iron] = 3;
    tx.faction(f.A).res[wood] = 5;
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRecipe(tx, mill, 0, Recipe{"", {ResAmount{iron, 1}, ResAmount{iron, 1}}, {}, {ResAmount{steel, 1}}, 1}); }); }),
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
  CHECK(f.w().building(mill)->recipes.empty());
  CHECK_EQ(f.w().province(f.p[0])->buildings[0].cycle, 0);
}

TEST(rules_build_essence_and_cult) {
  Fix f;
  const Id light = test::idByName(f.w().catalogs->essences, "Эссенция света");
  Id shrine = 0, wonder = 0, own = 0;
  f.tx([&](Tx& tx) {
    for (int i : {0, 1, 2}) {
      tx.province(f.p[i]).owner = i < 2 ? f.A : f.B;
      setCity(tx, f.p[i], CityType::Village);
    }
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

// ---------------------------------------------------------------- слоты
TEST(rules_build_slot_losses_and_trim) {
  Fix f;
  std::vector<Id> bs;
  f.tx([&](Tx& tx) {
    Province& p = tx.province(f.p[0]);
    p.owner = f.A;
    p.size = ProvSize::Small;
    setCity(tx, f.p[0], CityType::Village);   // 1 + 1 = 2 слота, со столицей — 5
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
