// Сценарии вкладки «Экономика» государства и гильдии: расход провизии государства живых (0,001 на жителя) и
// предупреждение «Голод» (ТЗ «Общие доработки», п.7), казна до тысячных («Фиксы», п.13), доход торгового флота
// (п.11: содержание галеонов × 2), маршруты гильдии (п.9: 5 % ценности провинций пути).
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

TEST(app_economy_faction_lines) {
  HideTestRegs hide;
  Harness h("economy_faction", 1440, 1200);
  h.demo();
  const Id st = biggestState(h->world());
  CHECK(st != 0);
  CHECK(h->world().faction(st)->stateKind == StateKind::Living);
  // Голод: недостача провизии (ресурсов группы «Провизия» не хватило на расход населения).
  const std::vector<Id> provs = rules::provisionResources(h->world());
  CHECK(!provs.empty());
  CHECK(h->act("Провизия", [&](Tx& tx) {
    for (Id r : provs) tx.faction(st).res.erase(r);
    tx.faction(st).provisionDebt = 5;
  }));
  auto c = rules::calc(h->world());
  const rules::FactionCalc* fc = c->faction(st);
  CHECK(fc->famine);
  CHECK_NEAR(fc->provisionNeed, double(fc->population) * schema::kProvisionsPerPerson, 1e-9);
  // Запасов нет: на расход и прошлую недостачу идёт только добыча провизии этого хода (провинции с «Зерном»).
  double produced = 0;
  for (Id r : provs)
    if (auto it = fc->resources.find(r); it != fc->resources.end()) produced += std::max(0.0, it->second.production);
  CHECK_NEAR(fc->provisionDebtNext, std::max(0.0, fc->provisionNeed + 5 - produced), 1e-9);
  openTab(h, st, "faction.economy");
  CHECK(h->uiRect("economy.famine") != nullptr);
  shotClean(h, "economy_faction_famine");
  // Казна — до тысячных.
  CHECK(typeNumber(h, "economy.stock.1", "1234,567"));
  CHECK_NEAR(h->world().faction(st)->treasury(), 1234.567, 1e-9);
  // Недостача покрыта — голода нет.
  CHECK(h->act("Провизия", [&](Tx& tx) { tx.faction(st).provisionDebt = 0; }));
  CHECK(!rules::calc(h->world())->faction(st)->famine);
  h.settle();
  CHECK(h->uiRect("economy.famine") == nullptr);

  // Торговый флот: 20 торговых галеонов из резерва — содержание × 2 в доход.
  Id row = 0;
  CHECK(h->act("Галеоны", [&](Tx& tx) {
    row = rules::addFleetRow(tx, st, ShipType::Galleon, "Торговые галеоны", 20, 3.5);
    rules::setTradeFleet(tx, st, row, 20);
  }));
  c = rules::calc(h->world());
  fc = c->faction(st);
  const double fleetK = std::max(0.0, 1 + fc->fx[Fx::FleetUpkeepPct] / 100);
  CHECK_NEAR(fc->incTradeFleet, 20 * 3.5 * fleetK * 2, 1e-9);
  CHECK_NEAR(fc->pirateRisk, 2, 1e-9);   // 20 галеонов без охраны — 2 %
  openTab(h, st, "faction.economy");
  shotClean(h, "economy_faction_incomes");
}

TEST(app_economy_guild_routes) {
  HideTestRegs hide;
  Harness h("economy_guild_routes", 1440, 1100);
  h.demo();
  // Гильдия — владелец маршрутов: 5 % текущей ценности сухопутных провинций пути.
  Id guild = 0;
  h->world().routes.each([&](const Route& r) {
    if (!guild && r.guild) guild = r.guild;
  });
  CHECK(guild != 0);
  auto c = rules::calc(h->world());
  const rules::FactionCalc* fc = c->faction(guild);
  CHECK(fc->incRoutes > 0);
  auto fs = geo::faces(h->world());
  double expect = 0;
  h->world().routes.each([&](const Route& r) {
    if (r.guild != guild) return;
    for (Id pid : fs->provincesOnPolyline(r.pts))
      if (const rules::ProvinceCalc* pc = c->province(pid); pc && !pc->sea) expect += pc->tradeValue * 0.05;
  });
  CHECK_NEAR(fc->incRoutes, expect, 1e-9);
  openTab(h, guild, "faction.economy");
  shotClean(h, "economy_guild_routes");
}
