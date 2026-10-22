// Regnum — производство: рецепты с несколькими ресурсами на выходе, множитель производства и ветка «Стимуляция
// промышленности», необязательная добыча (ферма), торговая ценность от населения, базовые постройки и технологии
// версии 4 (ферма, бойня, мастерские, храмы, обогатители, жилой квартал, гильдия строителей, штабы разведки).
#include "core/content.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;
using test::buildingByKey;
using test::hasPrereq;
using test::idByName;

namespace {

Id res(const World& w, const char* name) { return idByName(w.catalogs->resources, name); }

// Технология ветки «Стимуляция промышленности», открывающая множитель ×mult.
Id industryTech(const World& w, int mult) {
  return w.techs.firstId([&](const Tech& t) { return t.faction == 0 && t.key == schema::industryKey(mult); });
}

}  // namespace

TEST(rules_production_multi_output_recipe) {
  Fix f;
  const Id cow = res(f.w(), "Коровы"), hide = res(f.w(), "Кожа"), bone = res(f.w(), "Кость"), fur = res(f.w(), "Мех");
  CHECK(cow && hide && bone && fur);
  Id b = 0;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.A, addRace(tx), 1000);
    b = createBuilding(tx, 0, "Бойня");
    setBuildingRole(tx, b, BuildingRole::Convert, true);
    setRecipe(tx, b, 0, Recipe{"Мамонты", {{cow, 1}}, {}, {{hide, 40}, {bone, 20}, {fur, 20}}, 1});
    placeBuilding(tx, f.p[0], b, 1);
    tx.faction(f.A).res[cow] = 3;
    tx.faction(f.A).stateKind = StateKind::Undead;   // коровы — провизия: живые съели бы одну
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRecipe(tx, b, 1, Recipe{"", {{cow, 1}}, {}, {{hide, 1}, {hide, 2}}, 1}); }); }), "разными"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRecipe(tx, b, 1, Recipe{"", {{cow, 1}}, {}, {{hide, 1}, {bone, 1}, {fur, 1}, {cow, 1}}, 1}); }); }),
            "трёх"));
  CHECK_EQ(recipeTitle(f.w(), Recipe{"", {}, {}, {{bone, 1}, {hide, 1}}, 1}), std::string("Кость"));
  const FactionCalc& fc = f.fc(f.A);
  CHECK_NEAR(fc.resources.at(hide).conversionOut, 40, 1e-9);
  CHECK_NEAR(fc.resources.at(fur).conversionOut, 20, 1e-9);
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(cow), 2, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(hide), 40, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(bone), 20, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(fur), 20, 1e-9);
}

TEST(rules_production_multiplier) {
  Fix f;
  const Id iron = res(f.w(), "Железо"), steel = res(f.w(), "Сталь");
  // Ветка: 99 технологий, ×2…×100, по 5 ходов, цена × множитель, цепочка зависимостей.
  for (int m = 2; m <= schema::kMaxProductionMult; m++) {
    const Tech* t = f.w().tech(industryTech(f.w(), m));
    CHECK_MSG(t, "нет технологии ×" + std::to_string(m));
    if (!t) continue;
    CHECK_EQ(t->name, "Стимуляция промышленности " + std::to_string(m - 1));
    CHECK_EQ(t->turns, 5);
    CHECK_NEAR(t->cost.at(kGold), 10.0 * m, 1e-9);
    CHECK_NEAR(t->cost.at(res(f.w(), "Механические детали")), 50.0 * m, 1e-9);
    if (m > 2) CHECK(hasPrereq(f.w(), t->id, industryTech(f.w(), m - 1)));
  }
  CHECK_EQ(schema::industryMultOf("industry37"), 37);
  CHECK_EQ(schema::industryMultOf("industry1"), 0);
  CHECK_EQ(schema::industryMultOf("industryX"), 0);
  Id b = 0;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.A, addRace(tx), 1000);
    b = createBuilding(tx, 0, "Плавильня");
    setBuildingRole(tx, b, BuildingRole::Convert, true);
    setRecipe(tx, b, 0, Recipe{"", {{iron, 1}}, {}, {{steel, 2}}, 1});
    placeBuilding(tx, f.p[0], b, 1);
    tx.faction(f.A).res[iron] = 10;
  });
  CHECK_EQ(maxProductionMult(f.w(), f.A), 1);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setPbMult(tx, f.p[0], b, 2); }); }), "Стимуляция"));
  // Изучены ×2 и ×3 (×4 — нет): доступно до ×3.
  f.tx([&](Tx& tx) {
    for (int m : {2, 3}) tx.faction(f.A).techs[industryTech(tx.w(), m)].studied = true;
    tx.faction(f.A).techs[industryTech(tx.w(), 5)].studied = true;   // разрыв цепочки не засчитывается
  });
  CHECK_EQ(maxProductionMult(f.w(), f.A), 3);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setPbMult(tx, f.p[0], b, 4); }); }), "до ×3"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setPbMult(tx, f.p[0], b, 101); }); }), "×100"));
  f.tx([&](Tx& tx) { setPbMult(tx, f.p[0], b, 3); });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 7, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(steel), 6, 1e-9);
  // Не хватает ресурсов на цикл ×3 — постройка ждёт.
  f.tx([&](Tx& tx) { tx.faction(f.A).res[iron] = 2; });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 2, 1e-9);
  // Выключение преобразования сбрасывает множитель.
  f.tx([&](Tx& tx) { setBuildingRole(tx, b, BuildingRole::Convert, false); });
  CHECK_EQ(f.w().province(f.p[0])->buildings.at(0).mult, 1);
}

TEST(rules_production_farm_optional_extract) {
  Fix f;
  const Id grain = res(f.w(), "Зерно"), wood = res(f.w(), "Древесина");
  Id farm = 0;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.A, addRace(tx), 1000);
    tx.province(f.p[1]).owner = f.A;
    tx.province(f.p[0]).resource = grain;
    tx.province(f.p[0]).resourceAmount = 100;
    tx.province(f.p[1]).resource = wood;
    tx.province(f.p[1]).resourceAmount = 100;
    farm = createBuilding(tx, 0, "Ферма");
    setBuildingRole(tx, farm, BuildingRole::Extract, true);
    setExtractGroups(tx, farm, {tx.w().catalogs->groupId(schema::grp::ProvAgri)}, {});
    setLevelExtract(tx, farm, 1, 25);
  });
  // Обязательная добыча — нельзя при древесине; необязательная — можно, но добыча древесины не растёт.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { placeBuilding(tx, f.p[1], farm, 1); }); }), "не добывается"));
  f.tx([&](Tx& tx) {
    setExtractOptional(tx, farm, true);
    placeBuilding(tx, f.p[0], farm, 1);
    placeBuilding(tx, f.p[1], farm, 1);
  });
  CHECK_NEAR(f.pc(f.p[0]).production, 125, 1e-9);
  CHECK_NEAR(f.pc(f.p[1]).production, 100, 1e-9);
  f.tx([&](Tx& tx) { setBuildingRole(tx, farm, BuildingRole::Extract, false); });
  CHECK(!f.w().building(farm)->extractOptional);
}

TEST(rules_production_trade_value_population) {
  Fix f;
  f.tx([&](Tx& tx) {
    own(tx, f.p[0], f.A, addRace(tx), 100000);
    tx.province(f.p[0]).baseTrade = 1000;
  });
  CHECK_NEAR(f.pc(f.p[0]).tradePop, 1000, 1e-9);
  CHECK_NEAR(f.pc(f.p[0]).tradeValue, 2000, 1e-9);
}

TEST(rules_production_content_v4) {
  const World w = newWorld("Мир");
  const Catalogs& c = *w.catalogs;
  // Ресурсы и значок археологических сокровищ.
  for (const char* n : {"Мех", "Прочная чешуя", "Хитин"}) CHECK_EQ(w.resource(idByName(c.resources, n))->group, c.groupId(schema::grp::MatRaw));
  CHECK_EQ(w.resource(idByName(c.resources, "Титанобоа"))->group, c.groupId(schema::grp::WarBeasts));
  CHECK_EQ(c.resourceByKey(schema::kResArchTreasure)->icon, std::string("amphora"));
  auto bld = [&](const char* key) { return w.building(buildingByKey(w, key)); };
  // Ферма: 5 уровней, добыча +25…+200 %, овощи, зерно, фрукты, курицы.
  const Building* farm = bld(schema::bld::Farm);
  CHECK(farm && farm->cat == BuildingCat::Residential && farm->extract && farm->extractOptional);
  if (farm) {
    CHECK_EQ(farm->levels.size(), size_t(5));
    CHECK_NEAR(farm->levels[4].extractPct, 200, 1e-9);
    CHECK_NEAR(farm->levels[2].cost.at(kGold), 7.5, 1e-9);
    CHECK_EQ(farm->levels[0].produce.size(), size_t(4));
    CHECK_NEAR(farm->levels[3].produce.at(idByName(c.resources, "Курицы")), 25, 1e-9);
  }
  const Building* slaughter = bld(schema::bld::Slaughterhouse);
  CHECK(slaughter && slaughter->convert && slaughter->recipes.size() == 12 && slaughter->cat == BuildingCat::Industrial);
  if (slaughter) CHECK_EQ(slaughter->recipes[6].out.size(), size_t(3));   // мамонты: кожа, кость, мех
  const Building* altar = bld(schema::bld::CultAltar);
  CHECK(altar && altar->levels[0].produce.at(c.resourceId(schema::kResEnergy)) == 5000);
  for (const char* k : {schema::bld::HolyTemple, schema::bld::UnholyTemple}) {
    const Building* t = bld(k);
    CHECK(t && t->cat == BuildingCat::Religious && t->convert && t->recipes.size() == 1 && t->recipes[0].essIn.size() == 1);
    CHECK(t && t->levels[0].essCost.size() == 1);
  }
  for (const char* k : {schema::bld::AbyssEnricher, schema::bld::NecroEnricher}) {
    const Building* t = bld(k);
    CHECK(t && t->recipes.size() == 2 && t->cat == BuildingCat::Industrial);
  }
  const Building* housing = bld(schema::bld::Housing);
  CHECK(housing && housing->levels.size() == 5);
  if (housing) {
    const Modifier* m = w.modifier(housing->levels[4].modifiers.at(0));
    CHECK(m && m->get(Fx::PopGrowthPct) == 5);
  }
  const Building* guild = bld(schema::bld::BuildersGuild);
  CHECK(guild && guild->levels.size() == 5);
  if (guild) {
    const Modifier* m = w.modifier(guild->levels[2].modifiers.at(0));
    CHECK(m && m->get(Fx::Slots) == 3);
    CHECK_NEAR(guild->levels[4].cost.at(idByName(c.resources, "Камень")), 4000, 1e-9);
  }
  const Building* war = bld(schema::bld::WarWorkshop);
  const Id powderTech = w.techs.firstId([&](const Tech& t) { return t.faction == 0 && t.key == schema::tech::Powder; });
  CHECK(powderTech != 0);
  CHECK(war && war->cat == BuildingCat::Military && has(war->techs, powderTech));
  if (war) CHECK_NEAR(war->recipes.at(0).out.at(0).amount, 5, 1e-9);
  const Building* eng = bld(schema::bld::EngWorkshop);
  CHECK(eng && eng->recipes.size() == 2);
  const Building* pit = bld(schema::bld::CharcoalPit);
  CHECK(pit && pit->recipes.size() == 1 && pit->levels[0].cost.size() == 1);
  // «Производство пороха» — как «Производство магического пороха».
  const Tech* magic = w.tech(w.techs.firstId([&](const Tech& t) { return t.faction == 0 && t.key == schema::tech::MagicPowder; }));
  const Tech* powder = w.tech(powderTech);
  CHECK(magic && powder && magic->turns == powder->turns && magic->cost == powder->cost);
}

TEST(rules_production_intel_buildings) {
  Fix f;
  Id s = 0;
  f.tx([&](Tx& tx) { s = createFaction(tx, FactionKind::State, "Новая держава"); });
  const Id intel = buildingByKey(f.w(), schema::bld::IntelHq, s), counter = buildingByKey(f.w(), schema::bld::CounterIntelHq, s);
  CHECK(intel && counter);
  if (!intel || !counter) return;
  const Building& a = *f.w().building(intel);
  const Building& b = *f.w().building(counter);
  CHECK(a.cat == BuildingCat::Cult && b.cat == BuildingCat::Cult);
  CHECK_EQ(a.levels.size(), size_t(5));
  CHECK_EQ(b.levels.size(), size_t(3));
  CHECK(b.requires_.size() == 1 && b.requires_[0].building == intel && b.requires_[0].level == 4);
  CHECK_NEAR(f.w().modifier(a.levels[4].modifiers.at(0))->get(Fx::PlotAssassinPct), 50, 1e-9);
  CHECK_NEAR(f.w().modifier(b.levels[2].modifiers.at(0))->get(Fx::AssassinGuardPct), -30, 1e-9);
  CHECK_NEAR(a.levels[0].cost.at(kGold), 50, 1e-9);
  // Гильдия не получает штабов.
  Id g = 0;
  f.tx([&](Tx& tx) { g = createFaction(tx, FactionKind::Guild, "Гильдия"); });
  CHECK_EQ(buildingByKey(f.w(), schema::bld::IntelHq, g), Id(0));
}
