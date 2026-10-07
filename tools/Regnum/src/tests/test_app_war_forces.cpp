// Сценарии вкладок «Войска» и «Флот» (ТЗ «Общие доработки», п.10–11; «Виды государств», п.2): формирование отрядов
// из населения (2 хода до резерва, отмена с возвратом), роспуск резерва в население, «Правка резерва», удаление
// строки (воины — в население), раса отряда (нежить — из трупов), флот в торговле (прибыль, риск пиратов).
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Вкладка в панели справа от карты (узкая таблица со строками и карточкой строки), а не страницей.
void openTab(Harness& h, Id faction, const char* tab) {
  h->ui.tabOf[app::SelType::Faction] = tab;
  h->select(app::SelType::Faction, faction, true);
  h.settle();
  h.dropToasts();
  h.settle();
}

}  // namespace

// Формирование: число и цена (люди из населения), список «Формируется», отмена с возвратом, через 2 хода — в резерв;
// «Распустить» — воины в население; общая численность правится только в режиме «Правка резерва».
TEST(app_war_forces_recruit_disband) {
  Harness h("war_forces_recruit", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  Id hel = factionByName(h->world(), "Хельдвиг");
  CHECK(hel != 0);
  const Id row = h->world().faction(hel)->army[0].id;
  openTab(h, hel, "faction.army");
  CHECK(h.clickUi("mil.row.0"));
  h.settle();
  // Без «Правки резерва» общая численность не правится.
  CHECK(h->uiRect("mil.detail.total") == nullptr);
  CHECK(reveal(h, "mil.army.editReserve"));   // карточка строки прокрутила панель вниз
  CHECK(h.clickUi("mil.army.editReserve"));
  h.settle();
  CHECK(h->uiRect("mil.detail.total") != nullptr);
  const i64 total0 = h->world().faction(hel)->armyRow(row)->total;
  CHECK(reveal(h, "mil.detail.total"));
  CHECK(enterNumber(h, "mil.detail.total", total0 + 50));
  CHECK_EQ(h->world().faction(hel)->armyRow(row)->total, total0 + 50);
  h.key(Key::Z, ctrl());
  CHECK_EQ(h->world().faction(hel)->armyRow(row)->total, total0);
  CHECK(reveal(h, "mil.army.editReserve"));
  CHECK(h.clickUi("mil.army.editReserve"));
  h.settle();
  CHECK(h->uiRect("mil.detail.total") == nullptr);

  // «Сформировать» 500: люди — из населения, в списке «Формируется».
  const i64 pop0 = rules::statePopulation(h->world(), hel);
  CHECK(reveal(h, "mil.detail.recruit"));
  CHECK(h.clickUi("mil.detail.recruit"));
  h.settle();
  CHECK(h->uiRect("mil.recruit.ok") != nullptr);
  CHECK(enterNumber(h, "mil.recruit.count", 500));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_forces_recruit_popup"));
  CHECK(h.clickUi("mil.recruit.ok"));
  h.settle();
  const Faction* f = h->world().faction(hel);
  CHECK_EQ(f->forming.size(), size_t(1));
  if (!f->forming.empty()) {
    CHECK_EQ(f->forming[0].row, row);
    CHECK_EQ(f->forming[0].count, 500);
    CHECK_EQ(f->forming[0].left, schema::kFormationTurns);
  }
  CHECK_EQ(rules::statePopulation(h->world(), hel), pop0 - 500);
  CHECK_EQ(f->armyRow(row)->total, total0);   // в резерв — только через 2 хода
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_forces_forming"));
  // Отмена формирования — люди вернулись.
  CHECK(reveal(h, "mil.army.forming.0.cancel"));
  CHECK(h.clickUi("mil.army.forming.0.cancel"));
  h.settle();
  CHECK(h->world().faction(hel)->forming.empty());
  CHECK_EQ(rules::statePopulation(h->world(), hel), pop0);
  // Снова 500 и два хода — в резерве.
  CHECK(reveal(h, "mil.detail.recruit"));
  CHECK(h.clickUi("mil.detail.recruit"));
  CHECK(enterNumber(h, "mil.recruit.count", 500));
  CHECK(h.clickUi("mil.recruit.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->forming.size(), size_t(1));
  CHECK(h->endTurnNow());
  h.dropToasts();
  CHECK_EQ(h->world().faction(hel)->forming.size(), size_t(1));
  CHECK_EQ(h->world().faction(hel)->forming[0].left, 1);
  CHECK(h->endTurnNow());
  h.dropToasts();
  h.settle();
  CHECK(h->world().faction(hel)->forming.empty());
  CHECK_EQ(h->world().faction(hel)->armyRow(row)->total, total0 + 500);

  // «Распустить» 300 из резерва — воины в население (поровну по провинциям).
  const i64 pop1 = rules::statePopulation(h->world(), hel);
  const i64 res1 = app::mil::reserveOf(h->world(), hel, row, false);
  CHECK(res1 >= 300);
  CHECK(reveal(h, "mil.row.0"));
  CHECK(h.clickUi("mil.row.0"));
  CHECK(reveal(h, "mil.detail.disband"));
  CHECK(h.clickUi("mil.detail.disband"));
  h.settle();
  CHECK(enterNumber(h, "mil.disband.count", 300));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_forces_disband_popup"));
  CHECK(h.clickUi("mil.disband.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->armyRow(row)->total, total0 + 500 - 300);
  CHECK_EQ(rules::statePopulation(h->world(), hel), pop1 + 300);
  CHECK_EQ(app::mil::reserveOf(h->world(), hel, row, false), res1 - 300);
  // Больше резерва не распустить: поле ограничено резервом.
  CHECK(reveal(h, "mil.detail.disband"));
  CHECK(h.clickUi("mil.detail.disband"));
  CHECK(enterNumber(h, "mil.disband.count", res1 * 10));
  CHECK(h.clickUi("mil.disband.ok"));
  h.settle();
  CHECK_EQ(app::mil::reserveOf(h->world(), hel, row, false), 0);
  h.key(Key::Z, ctrl());
  CHECK_EQ(app::mil::reserveOf(h->world(), hel, row, false), res1 - 300);
}

// Удаление строки: подтверждение, воины строки — в население; раса отряда (нежить — из трупов, нехватка — отказ);
// чудовища не требуют ни населения, ни трупов — только ключевой ресурс (ТЗ «Ввод новых механик», п.2–3).
TEST(app_war_forces_remove_row_and_race) {
  Harness h("war_forces_race", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  Id hel = factionByName(h->world(), "Хельдвиг");
  openTab(h, hel, "faction.army");
  // Новая строка «Чудовища», 400 в резерве (правка резерва).
  const size_t rows0 = h->world().faction(hel)->army.size();
  const RectF* add = h->uiRect("mil.army.add");
  CHECK(add != nullptr);
  h.click(add->right() - 13, add->cy());
  h.settle();
  CHECK(h.clickUi("mil.addrow." + std::to_string(int(UnitType::Monsters))));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->army.size(), rows0 + 1);
  const Id nrow = h->world().faction(hel)->army.back().id;
  CHECK(reveal(h, "mil.army.editReserve"));   // карточка новой строки прокрутила панель вниз
  CHECK(h.clickUi("mil.army.editReserve"));
  h.settle();
  CHECK(reveal(h, "mil.detail.total"));
  CHECK(enterNumber(h, "mil.detail.total", 400));
  CHECK_EQ(h->world().faction(hel)->armyRow(nrow)->total, 400);
  CHECK(reveal(h, "mil.army.editReserve"));
  CHECK(h.clickUi("mil.army.editReserve"));
  h.settle();
  // Раса по умолчанию — «Живой» (государство живых); тип «Военные механизмы» — «Механический» (раса следует за
  // типом), обратно — снова «Живой»; затем выбор «Нежить».
  CHECK_EQ(rules::unitRace(h->world(), hel, *h->world().faction(hel)->armyRow(nrow)), std::string(schema::kRaceLiving));
  CHECK(reveal(h, "mil.detail.type"));
  CHECK(h.clickUi("mil.detail.type"));
  h.key(Key::Down);
  h.key(Key::Enter);
  h.settle();
  CHECK(h->world().faction(hel)->armyRow(nrow)->type == UnitType::Machines);
  CHECK_EQ(rules::unitRace(h->world(), hel, *h->world().faction(hel)->armyRow(nrow)), std::string(schema::kRaceMechanical));
  CHECK(reveal(h, "mil.detail.type"));
  CHECK(h.clickUi("mil.detail.type"));
  h.key(Key::Up);
  h.key(Key::Enter);
  h.settle();
  CHECK(h->world().faction(hel)->armyRow(nrow)->type == UnitType::Monsters);
  CHECK_EQ(rules::unitRace(h->world(), hel, *h->world().faction(hel)->armyRow(nrow)), std::string(schema::kRaceLiving));
  CHECK(reveal(h, "mil.detail.race"));
  CHECK(h.clickUi("mil.detail.race"));
  h.settle();
  std::vector<std::string> races = rules::unitRaces(h->world());
  int undead = int(std::find(races.begin(), races.end(), std::string(schema::kRaceUndead)) - races.begin());
  int living = int(std::find(races.begin(), races.end(), std::string(schema::kRaceLiving)) - races.begin());
  for (int k = living; k < undead; k++) h.key(Key::Down);
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(h->world().faction(hel)->armyRow(nrow)->race, std::string(schema::kRaceUndead));
  // Чудовища — без населения, трупов и энергии: в цене только ключевой ресурс (он не указан — отказ с причиной).
  {
    const rules::RecruitCost mc = rules::recruitCost(h->world(), hel, nrow, 100);
    CHECK_EQ(mc.people, 0);
    CHECK(mc.res.empty());
    CHECK(std::find(mc.problems.begin(), mc.problems.end(), std::string("Не указан ключевой ресурс юнита")) != mc.problems.end());
  }
  // Пехота-нежить формируется из трупов: трупов нет — «Сформировать» недоступно (причина в окне). Тип меняется по
  // правилам: раса «Нежить» (не по умолчанию) остаётся.
  CHECK(h->act("Тип войск", [&](Tx& tx) { rules::setRowType(tx, hel, nrow, UnitType::HeavyInf); }));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->armyRow(nrow)->race, std::string(schema::kRaceUndead));
  const rules::RecruitCost cost = rules::recruitCost(h->world(), hel, nrow, 100);
  CHECK(cost.people == 0);
  CHECK(!cost.res.empty());   // трупы
  CHECK(!cost.problems.empty());
  const size_t forming0 = h->world().faction(hel)->forming.size();
  CHECK(reveal(h, "mil.detail.recruit"));
  CHECK(h.clickUi("mil.detail.recruit"));
  CHECK(enterNumber(h, "mil.recruit.count", 100));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_forces_recruit_undead"));
  CHECK(h.clickUi("mil.recruit.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->forming.size(), forming0);
  h.key(Key::Escape);
  h.settle();
  // Раса обратно — «Живой»; удаление строки: подтверждение, 400 воинов — в население.
  CHECK(h->act("Раса", [&](Tx& tx) { rules::setRowRace(tx, hel, nrow, schema::kRaceLiving); }));
  h.settle();
  const i64 pop0 = rules::statePopulation(h->world(), hel);
  CHECK(reveal(h, "mil.detail.delete"));
  CHECK(h.clickUi("mil.detail.delete"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_forces_remove_confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->world().faction(hel)->armyRow(nrow) == nullptr);
  CHECK_EQ(rules::statePopulation(h->world(), hel), pop0 + 400);
}

// Флот в торговле: корабли из резерва, прибыль галеонов (содержание × 2), риск пиратов (10 галеонов — 1 %,
// линкор охраняет 10 галеонов).
TEST(app_war_forces_trade_fleet) {
  Harness h("war_forces_trade", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  Id hel = factionByName(h->world(), "Хельдвиг");
  openTab(h, hel, "faction.fleet");
  // Торговые галеоны: новая строка, 30 в резерве, содержание 1,5.
  const RectF* add = h->uiRect("mil.fleet.add");
  CHECK(add != nullptr);
  h.click(add->right() - 13, add->cy());
  h.settle();
  CHECK(h.clickUi("mil.addrow." + std::to_string(int(ShipType::Galleon))));
  h.settle();
  const Id gal = h->world().faction(hel)->fleet.back().id;
  CHECK(h->world().faction(hel)->fleet.back().type == ShipType::Galleon);
  CHECK(h.clickUi("mil.fleet.editReserve"));
  h.settle();
  CHECK(reveal(h, "mil.detail.total"));
  CHECK(enterNumber(h, "mil.detail.total", 30));
  CHECK(reveal(h, "mil.detail.upkeep"));
  CHECK(h.clickUi("mil.detail.upkeep"));
  h.retype("1,5");
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(h->world().faction(hel)->fleetRow(gal)->total, 30);
  CHECK_NEAR(h->world().faction(hel)->fleetRow(gal)->upkeep, 1.5, 1e-9);
  CHECK(h.clickUi("mil.fleet.editReserve"));
  h.settle();
  // 20 галеонов в торговле: прибыль 20 × 1,5 × 2 = 60, риск 2 %.
  CHECK(reveal(h, "mil.trade." + std::to_string(gal)));
  CHECK(enterNumber(h, "mil.trade." + std::to_string(gal), 20));
  const Faction* f = h->world().faction(hel);
  CHECK_EQ(f->tradeFleet.size(), size_t(1));
  if (!f->tradeFleet.empty()) CHECK_EQ(f->tradeFleet[0].count, 20);
  auto c = rules::calc(h->world());
  CHECK_NEAR(c->faction(hel)->incTradeFleet, 60, 1e-6);
  CHECK_NEAR(c->faction(hel)->pirateRisk, 2, 1e-9);
  CHECK_EQ(app::mil::reserveOf(h->world(), hel, gal, true), 10);
  // Больше резерва не назначить.
  CHECK(enterNumber(h, "mil.trade." + std::to_string(gal), 999));
  CHECK_EQ(h->world().faction(hel)->tradeFleet[0].count, 30);
  CHECK(enterNumber(h, "mil.trade." + std::to_string(gal), 20));
  // Линкор из резерва охраняет 10 галеонов: риск 1 %.
  Id line = 0;
  for (const FleetRow& r : h->world().faction(hel)->fleet)
    if (r.type == ShipType::ShipOfLine) line = r.id;
  CHECK(line != 0);
  CHECK(app::mil::reserveOf(h->world(), hel, line, true) >= 1);
  CHECK(reveal(h, "mil.trade." + std::to_string(line)));
  CHECK(enterNumber(h, "mil.trade." + std::to_string(line), 1));
  c = rules::calc(h->world());
  CHECK_NEAR(c->faction(hel)->pirateRisk, 1, 1e-9);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_forces_trade"));
  // Конец хода: риск переходит в бросок следующего хода (Faction::pirateRisk).
  CHECK(h->endTurnNow());
  h.dropToasts();
  CHECK_NEAR(h->world().faction(hel)->pirateRisk, 1, 1e-9);
}

// Полноэкранные таблицы: столбцы расы, ключевого ресурса и цены, формирование под резервом, действия у строки.
TEST(app_war_forces_editor_columns) {
  Harness h("war_forces_editor", 1600, 1000);
  RealArmyTools tools;
  h.demo();
  Id hel = factionByName(h->world(), "Хельдвиг");
  const Id row = h->world().faction(hel)->army[1].id;   // кавалерия: нужен ключевой ресурс (ездовые наземные)
  CHECK(schema::isCavalry(h->world().faction(hel)->armyRow(row)->type));
  CHECK(!h->act("Без ключевого ресурса", [&](Tx& tx) { rules::recruit(tx, hel, row, 120); }));
  h.dropToasts();
  Id horses = 0;
  for (const CatalogItem& c : h->world().catalogs->resources)
    if (c.name == "Лошади") horses = c.id;
  CHECK(horses != 0);
  CHECK(h->act("Ключевой ресурс и запас", [&](Tx& tx) {
    rules::setRowKey(tx, hel, row, horses);
    tx.faction(hel).res[horses] = 1000;
  }));
  CHECK(h->act("Формирование", [&](Tx& tx) { rules::recruit(tx, hel, row, 120); }));
  CHECK_NEAR(h->world().faction(hel)->stock(horses), 880, 1e-9);   // 1 лошадь на всадника
  h->openEditor("military", hel);
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK(h->uiRect("mil.row.0.race") != nullptr);
  CHECK(h->uiRect("mil.row.0.key") != nullptr);
  CHECK(h->uiRect("mil.row.0.cost") != nullptr);
  CHECK(h->uiRect("mil.row.1.recruit") != nullptr);
  CHECK(h.shot("war_forces_editor"));
  // «Сформировать» у строки в таблице.
  CHECK(h.clickUi("mil.row.1.recruit"));
  h.settle();
  CHECK(enterNumber(h, "mil.recruit.count", 80));
  CHECK(h.clickUi("mil.recruit.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->forming.size(), size_t(2));
}
