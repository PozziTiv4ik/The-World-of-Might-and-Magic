// Regnum — правила торговли: сделки (проверка, разовые и «каждый ход», расторжение, провинция в сделке), дань и
// репарации, торговые гильдии (штабы, влияние, государство расположения), торговые маршруты и доход гильдии с них,
// торговый флот и пираты.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

// ---------------------------------------------------------------- сделки
TEST(rules_trade_validate) {
  Fix f;
  f.tx([&](Tx& tx) { tx.faction(f.A).res[5] = 12; tx.faction(f.A).res[3] = 15; });
  auto problems = [&](const Deal& d) { return join(validateDeal(f.w(), d).problems, " | "); };
  Deal d;
  CHECK(!validateDeal(f.w(), d).ok);
  CHECK(has(problems(d), "Не выбрана сторона сделки"));
  CHECK(has(problems(d), "нет ни одной позиции"));
  d.a = f.A;
  d.b = f.A;
  CHECK(has(problems(d), "Стороны сделки совпадают"));
  d.b = f.B;
  d.items = {DealItem{DealSide::A, 5, 40, DealMode::Once, 1, 0}};
  CHECK(has(problems(d), "Недостаточно ресурса «Железо» у «Арден»: нужно 40, есть 12"));
  d.items = {DealItem{DealSide::A, 3, 10, DealMode::Once, 1, 0}, DealItem{DealSide::A, 3, 10, DealMode::Once, 1, 0}};
  CHECK(has(problems(d), "нужно 20, есть 15"));  // разовые позиции суммируются
  d.items = {DealItem{DealSide::A, 999, 1, DealMode::Once, 1, 0}};
  CHECK(has(problems(d), "Ресурс позиции не найден"));
  d.items = {DealItem{DealSide::B, kGold, 0, DealMode::Once, 1, 0}};
  CHECK(has(problems(d), "больше нуля"));
  d.items = {DealItem{DealSide::B, kGold, 5, DealMode::PerTurn, 0, 0}};
  CHECK(has(problems(d), "не меньше одного хода"));
  d.items = {DealItem{DealSide::A, 5, 100, DealMode::PerTurn, 3, 0}};  // «каждый ход» не требует запаса сейчас
  CHECK(validateDeal(f.w(), d).ok);
  d.kind = DealKind::Tribute;
  CHECK(has(problems(d), "золотом каждый ход"));
  d.items = {DealItem{DealSide::B, kGold, 10, DealMode::PerTurn, 3, 0}};
  CHECK(validateDeal(f.w(), d).ok);
}

TEST(rules_trade_conclude_once_and_gift) {
  Fix f;
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[5] = 100;
    tx.faction(f.B).res[kGold] = 50;
  });
  Deal d;
  d.id = 12345;  // ID назначается заново
  d.a = f.A;
  d.b = f.B;
  d.items = {DealItem{DealSide::A, 5, 40, DealMode::Once, 1, 0}, DealItem{DealSide::B, kGold, 30, DealMode::Once, 1, 0}};
  Id id = 0;
  f.tx([&](Tx& tx) { id = concludeDeal(tx, d); });
  const World& w = f.w();
  CHECK(id != 12345);
  CHECK_NEAR(w.faction(f.A)->stock(5), 60, 1e-9);
  CHECK_NEAR(w.faction(f.A)->treasury(), 30, 1e-9);
  CHECK_NEAR(w.faction(f.B)->stock(5), 40, 1e-9);
  CHECK_NEAR(w.faction(f.B)->treasury(), 20, 1e-9);
  CHECK(w.deal(id)->status == DealStatus::Done);
  CHECK_EQ(w.deal(id)->turn, 1);
  CHECK(has(lastLog(w), "Сделка «Арден» и «Бельмар»"));
  Deal g;
  g.a = f.A;
  g.b = f.C;
  g.items = {DealItem{DealSide::A, 5, 10, DealMode::Once, 1, 0}};
  f.tx([&](Tx& tx) { concludeDeal(tx, g); });
  CHECK(has(lastLog(f.w()), "«Арден» дарит «Церис»: Железо 10"));
  CHECK_NEAR(f.w().faction(f.C)->stock(5), 10, 1e-9);
  // Нехватка — сделка не оформляется, мир не меняется.
  World before = f.w();
  g.items[0].amount = 51;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { concludeDeal(tx, g); }); }), "Недостаточно ресурса"));
  CHECK_EQ(World::diff(before, f.w()), 0u);
}

TEST(rules_trade_per_turn_and_cancel) {
  Fix f;
  Deal d;
  d.a = f.A;
  d.b = f.B;
  d.items = {DealItem{DealSide::A, 5, 5, DealMode::PerTurn, 3, 0}, DealItem{DealSide::B, 3, 2, DealMode::Once, 1, 0}};
  f.tx([&](Tx& tx) { tx.faction(f.B).res[3] = 2; });
  Id id = 0;
  f.tx([&](Tx& tx) { id = concludeDeal(tx, d); });
  const Deal& x = *f.w().deal(id);
  CHECK(x.status == DealStatus::Active);
  CHECK_EQ(x.items[0].left, 3);
  CHECK_EQ(x.items[1].left, 0);
  CHECK_NEAR(f.w().faction(f.A)->stock(5), 0, 1e-9);  // «каждый ход» исполняется при завершении хода
  CHECK_NEAR(f.w().faction(f.A)->stock(3), 2, 1e-9);  // разовая — сразу
  CHECK(has(lastLog(f.w()), "Железо 5 за ход, 3 хода"));
  f.tx([&](Tx& tx) { cancelDeal(tx, id); });
  CHECK(f.w().deal(id)->status == DealStatus::Cancelled);
  CHECK(has(lastLog(f.w()), "Расторгнута сделка"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { cancelDeal(tx, id); }); }), "уже расторгнута"));
  Deal once;
  once.a = f.A;
  once.b = f.B;
  once.items = {DealItem{DealSide::A, kGold, 0.5, DealMode::Once, 1, 0}};
  f.tx([&](Tx& tx) { tx.faction(f.A).res[kGold] = 1; });
  f.tx([&](Tx& tx) { id = concludeDeal(tx, once); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { cancelDeal(tx, id); }); }), "уже выполнена"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { cancelDeal(tx, 9999); }); }), "не найдена"));
}

TEST(rules_trade_deal_province) {
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

// ---------------------------------------------------------------- дань и репарации
TEST(rules_trade_tribute_and_reparations) {
  Fix f;
  Id t = 0, r = 0;
  f.tx([&](Tx& tx) {
    t = imposeTribute(tx, DealKind::Tribute, f.A, f.B, 50, 10);
    r = imposeTribute(tx, DealKind::Reparations, f.C, f.B, 7.5, 2);
  });
  const Deal& d = *f.w().deal(t);
  CHECK(d.kind == DealKind::Tribute);
  CHECK_EQ(d.a, f.A);
  CHECK_EQ(d.b, f.B);
  CHECK_EQ(d.items.size(), size_t(1));
  CHECK(d.items[0].from == DealSide::B);
  CHECK_EQ(d.items[0].res, kGold);
  CHECK_EQ(d.items[0].left, 10);
  CHECK(d.status == DealStatus::Active);
  CHECK(f.w().deal(r)->kind == DealKind::Reparations);
  CHECK(has(lastLog(f.w()), "«Бельмар» выплачивает «Церис» репарации: 7,5 тыс. золота за ход, 2 хода"));
  CHECK_NEAR(f.fc(f.A).incTribute, 50, 1e-9);
  CHECK_NEAR(f.fc(f.C).incTribute, 7.5, 1e-9);
  CHECK_NEAR(f.fc(f.B).expTribute, 57.5, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { imposeTribute(tx, DealKind::Trade, f.A, f.B, 1, 1); }); }), "дань или репарации"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { imposeTribute(tx, DealKind::Tribute, f.A, f.B, 0, 1); }); }), "больше нуля"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { imposeTribute(tx, DealKind::Tribute, f.A, f.B, 1, 0); }); }), "одного хода"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { imposeTribute(tx, DealKind::Tribute, f.A, f.A, 1, 1); }); }), "самой себе"));
  f.tx([&](Tx& tx) { cancelDeal(tx, t); });
  CHECK(has(lastLog(f.w()), "Отменена дань"));
  CHECK_NEAR(f.fc(f.B).expTribute, 7.5, 1e-9);
}

// ---------------------------------------------------------------- гильдии и маршруты
TEST(rules_trade_guild_hq_influence_home) {
  Fix f;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    buildHq(tx, f.G, f.p[0]);
  });
  CHECK(f.w().province(f.p[0])->hqs == std::vector<Id>({f.G}));
  CHECK(has(lastLog(f.w()), "открыт штаб в провинции «П0»"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { buildHq(tx, f.G, f.p[0]); }); }), "уже есть штаб"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { buildHq(tx, f.A, f.p[0]); }); }), "нужна торговая гильдия"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { buildHq(tx, f.G, f.seaW); }); }), "морской"));
  f.tx([&](Tx& tx) {
    buildHq(tx, f.H, f.p[0]);
    for (int i = 0; i < 3; i++) buildHq(tx, createFaction(tx, FactionKind::Guild), f.p[0]);
  });
  CHECK_EQ(f.w().province(f.p[0])->hqs.size(), size_t(5));
  CHECK(has(errorOf([&] {
          f.tx([&](Tx& tx) { buildHq(tx, createFaction(tx, FactionKind::Guild), f.p[0]); });
        }),
        "уже 5 штабов"));
  f.tx([&](Tx& tx) { removeHq(tx, f.H, f.p[0]); });
  CHECK_EQ(f.w().province(f.p[0])->hqs.size(), size_t(4));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { removeHq(tx, f.H, f.p[0]); }); }), "нет штаба"));
  // Влияние: сумма не больше 100 %.
  f.tx([&](Tx& tx) { setInfluence(tx, f.p[0], f.G, 60); });
  std::string e = errorOf([&] { f.tx([&](Tx& tx) { setInfluence(tx, f.p[0], f.H, 50); }); });
  CHECK(has(e, "превысит 100 %"));
  CHECK(has(e, "свободно 40 %"));
  f.tx([&](Tx& tx) { setInfluence(tx, f.p[0], f.H, 40); });
  f.tx([&](Tx& tx) { setInfluence(tx, f.p[0], f.G, 55); });  // своё значение не мешает изменению
  CHECK_EQ(f.w().province(f.p[0])->influence.size(), size_t(2));
  CHECK_NEAR(f.w().province(f.p[0])->influence[0].pct, 55, 1e-9);
  f.tx([&](Tx& tx) { setInfluence(tx, f.p[0], f.G, 0); });
  CHECK_EQ(f.w().province(f.p[0])->influence.size(), size_t(1));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setInfluence(tx, f.p[0], f.G, 101); }); }), "от 0 до 100"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setInfluence(tx, f.p[0], f.G, -1); }); }), "от 0 до 100"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setInfluence(tx, f.p[0], f.A, 5); }); }), "нужна торговая гильдия"));
  // Государство расположения.
  f.tx([&](Tx& tx) { setHomeState(tx, f.G, f.A); });
  CHECK_EQ(f.w().faction(f.G)->homeState, f.A);
  CHECK(has(lastLog(f.w()), "перенесла основное государство расположения в «Арден»"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHomeState(tx, f.G, f.H); }); }), "нужно государство"));
  Id sg = 0;
  f.tx([&](Tx& tx) { sg = createStateGuild(tx, f.B); });
  const Faction& g = *f.w().faction(sg);
  CHECK(g.isGuild());
  CHECK(g.stateGuild);
  CHECK_EQ(g.homeState, f.B);
  CHECK_EQ(g.name, std::string("Гильдия «Бельмар»"));
  CHECK(has(lastLog(f.w()), "учреждена государственная гильдия"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHomeState(tx, sg, f.A); }); }), "Государственная гильдия не может"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { createStateGuild(tx, f.G); }); }), "нужно государство"));
}

TEST(rules_trade_routes) {
  Fix f;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { createRoute(tx, {center(0)}); }); }), "не менее двух"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { createRoute(tx, {center(0), center(0)}); }); }), "не менее двух"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { createRoute(tx, {center(0), {1200, 100}}); }); }), "вне карты"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { createRoute(tx, {center(0), center(1)}, f.A); }); }), "нужна торговая гильдия"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { createRoute(tx, {center(0), {std::nan(""), 1}}); }); }), "Недопустимые координаты"));
  Id r = 0;
  f.tx([&](Tx& tx) { r = createRoute(tx, {center(0), center(0), center(1), center(2)}, f.G); });
  CHECK_EQ(f.w().route(r)->pts.size(), size_t(3));  // повторная точка убрана
  CHECK_EQ(f.w().route(r)->name, std::string("Торговый путь 1"));
  CHECK_EQ(f.w().route(r)->guild, f.G);
  CHECK(has(lastLog(f.w()), "Проложен торговый маршрут «Торговый путь 1» гильдии «Гильдия весов»"));
  auto c = calc(f.w());
  CHECK_EQ(c->routeCounts.at(f.p[0]), 1);
  CHECK_EQ(c->routeCounts.at(f.p[2]), 1);
  CHECK(c->routeCounts.count(f.p[3]) == 0);
  f.tx([&](Tx& tx) { setRoutePoints(tx, r, {center(3), center(7)}); });
  c = calc(f.w());
  CHECK(c->routeCounts.count(f.p[0]) == 0);
  CHECK_EQ(c->routeCounts.at(f.p[3]), 1);
  CHECK_EQ(c->routeCounts.at(f.p[7]), 1);
  f.tx([&](Tx& tx) { removeRoute(tx, r); });
  CHECK(!f.w().route(r));
  CHECK(calc(f.w())->routeCounts.empty());
  CHECK(has(lastLog(f.w()), "Упразднён торговый маршрут"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { removeRoute(tx, r); }); }), "не найден"));
}

TEST(rules_trade_guild_route_income) {
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

// ---------------------------------------------------------------- торговый флот и пираты
TEST(rules_trade_fleet_and_pirates) {
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

TEST(rules_trade_pirates_flat_loss) {
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
