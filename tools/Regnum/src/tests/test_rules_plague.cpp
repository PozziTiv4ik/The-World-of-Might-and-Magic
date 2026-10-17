// Regnum — правила чумы: заражение и распространение на соседей, иммунитет, лечебница и «Чумной двор».
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

TEST(rules_plague_spread_and_immunity) {
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

TEST(rules_plague_heal_and_plague_buildings) {
  Fix f;
  Id heal = 0, pest = 0;
  const Id water = test::idByName(f.w().catalogs->essences, "Эссенция воды"), plagueEss = test::idByName(f.w().catalogs->essences, "Эссенция чумы");
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    tx.province(f.p[1]).owner = f.A;
    heal = simpleBuilding(tx, "Лечебница", f.p[0], BuildingFlag::Healing, true);
    pest = simpleBuilding(tx, "Чумной двор", f.p[1], BuildingFlag::Plague, true);
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
