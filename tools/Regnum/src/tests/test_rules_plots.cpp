// Regnum — заговоры: состав и правила игрока, спонсирование и снабжение, приглашения героев, заказные убийства,
// контрразведка (обнаружение, раскрытие, аресты, тюрьмы), осуществление заговора, очистка ссылок.
#include "core/content.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;
using test::buildingByKey;
using test::hasPrereq;
using test::idByName;

namespace {

// Заговор A против B: провинции p0 и p1 у B (население 10 000), герои B — обычный и значимый.
struct PlotFix {
  Fix f;
  Id race = 0, plot = 0, hero = 0, vip = 0, other = 0;
  PlotFix(PlotKind kind = PlotKind::External) {
    f.tx([&](Tx& tx) {
      race = addRace(tx);
      own(tx, f.p[0], f.B, race, 10000);
      own(tx, f.p[1], f.B, race, 10000);
      own(tx, f.p[4], f.A, race, 1000);
      tx.province(f.p[0]).baseTrade = 1000;
      tx.province(f.p[0]).contentment = -20;
      tx.province(f.p[1]).contentment = 10;
      hero = createCharacter(tx, f.B, "Бран");
      vip = createCharacter(tx, f.B, "Вейла");
      tx.character(vip).hero = true;
      other = createCharacter(tx, f.B, "Гор");
      tx.faction(f.A).res[kGold] = 100000;
      plot = createPlot(tx, kind, kind == PlotKind::External ? f.A : 0, f.B);
    });
  }
  const Plot& p() const { return *f.w().plot(plot); }
};

}  // namespace

TEST(rules_plots_members_and_player_rules) {
  PlotFix x;
  Fix& f = x.f;
  CHECK_EQ(plotTitle(f.w(), x.p()), std::string("Заговор против Бельмар"));
  // Игрок: только провинции с довольством ниже 0 и герои с «Недовольством правителем».
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addPlotProvince(tx, x.plot, f.p[1], true); }); }), "Довольство"));
  f.tx([&](Tx& tx) { addPlotProvince(tx, x.plot, f.p[0], true); });
  CHECK(x.p().province(f.p[0]) != nullptr);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addPlotProvince(tx, x.plot, f.p[4], false); }); }), "не провинция"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addPlotHero(tx, x.plot, x.hero, true); }); }), "Недовольства"));
  f.tx([&](Tx& tx) {
    addModifier(tx, ModTarget::Character, x.hero, ensureBuiltinMod(tx, schema::mod::Discontent), 0);
    addPlotHero(tx, x.plot, x.hero, true);   // игрок — приглашение
    addPlotHero(tx, x.plot, x.vip, false);   // администратор — сразу участник
  });
  CHECK(has(x.p().invited, x.hero));
  CHECK(has(x.p().heroes, x.vip));
  CHECK_EQ(plotOfHero(f.w(), x.hero), x.plot);
  // Лидер — только участник; приглашённый становится участником после согласия.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setPlotLeader(tx, x.plot, x.hero); }); }), "участников"));
  f.tx([&](Tx& tx) {
    answerPlotInvite(tx, x.plot, x.hero, true);
    setPlotLeader(tx, x.plot, x.hero);
  });
  CHECK(has(x.p().heroes, x.hero) && x.p().invited.empty());
  CHECK_EQ(x.p().leader, x.hero);
  // Провинция и герой — не больше чем в одном заговоре.
  Id second = 0;
  f.tx([&](Tx& tx) { second = createPlot(tx, PlotKind::Internal, 0, f.B); });
  CHECK(has(plotProvinceProblem(f.w(), second, f.p[0], false), "другом заговоре"));
  CHECK(has(plotHeroProblem(f.w(), second, x.vip, false), "другом заговоре"));
  CHECK_EQ(f.w().plot(second)->owner, Id(0));
  // Отказ — «Угрозы заговору».
  f.tx([&](Tx& tx) {
    addModifier(tx, ModTarget::Character, x.other, ensureBuiltinMod(tx, schema::mod::Discontent), 0);
    addPlotHero(tx, second, x.other, true);
    answerPlotInvite(tx, second, x.other, false);
  });
  CHECK(has(f.w().plot(second)->threats, x.other));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { createPlot(tx, PlotKind::External, f.B, f.B); }); }), "различаться"));
}

TEST(rules_plots_sponsor_and_supplies) {
  PlotFix x;
  Fix& f = x.f;
  f.tx([&](Tx& tx) { addPlotProvince(tx, x.plot, f.p[0], false); });
  // Цена — 20 % текущей ценности: 1000 + 10 000 жителей / 100 = 1100 → 220.
  CHECK_NEAR(sponsorCost(f.w(), f.p[0]), 220, 1e-9);
  f.tx([&](Tx& tx) { sponsorPlot(tx, x.plot, f.p[0]); });
  CHECK_NEAR(x.p().province(f.p[0])->commitment, 10, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 100000 - 220, 1e-9);
  CHECK(has(sponsorProblem(f.w(), x.plot, f.p[0]), "уже спонсировали"));
  // Администратор задаёт приверженность прямо (0…100).
  f.tx([&](Tx& tx) { setPlotCommitment(tx, x.plot, f.p[0], 150); });
  CHECK_NEAR(x.p().province(f.p[0])->commitment, 100, 1e-9);
  // «Аресты заговорщиков» — приверженность не растёт.
  f.tx([&](Tx& tx) {
    setPlotCommitment(tx, x.plot, f.p[0], 30);
    tx.plot(x.plot).provinces[0].sponsored = 0;
    addModifier(tx, ModTarget::Province, f.p[0], ensureBuiltinMod(tx, schema::mod::PlotArrests), 5);
  });
  CHECK(has(sponsorProblem(f.w(), x.plot, f.p[0]), "Аресты"));
  // Снабжение: только ресурсы вооружения, со склада заказчика.
  const Id powder = idByName(f.w().catalogs->resources, "Порох");
  const Id wood = idByName(f.w().catalogs->resources, "Древесина");
  CHECK(powder && wood);
  CHECK(has(plotSupplyResources(f.w()), powder));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { supplyPlot(tx, x.plot, f.p[0], {{powder, 10}}); }); }), "Недостаточно"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { supplyPlot(tx, x.plot, f.p[0], {{wood, 10}}); }); }), "не подходит"));
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[powder] = 50;
    supplyPlot(tx, x.plot, f.p[0], {{powder, 40}});
  });
  CHECK_NEAR(f.w().faction(f.A)->stock(powder), 10, 1e-9);
  CHECK_NEAR(x.p().province(f.p[0])->supplies.at(powder), 40, 1e-9);
}

TEST(rules_plots_hits) {
  PlotFix x;
  Fix& f = x.f;
  f.tx([&](Tx& tx) {
    for (Id h : {x.hero, x.vip}) {
      addPlotHero(tx, x.plot, h, false);
      removePlotHero(tx, x.plot, h);
      tx.plot(x.plot).threats.push_back(h);
    }
  });
  // Обычный герой — 100…5000 (1 % за 100), значимый — 500…25000 (1 % за 500); советник — тоже значимый.
  CHECK_NEAR(hitRange(f.w(), x.hero).max, 5000, 1e-9);
  CHECK_NEAR(hitRange(f.w(), x.vip).perPct, 500, 1e-9);
  f.tx([&](Tx& tx) { addCouncilSeat(tx, f.B, "Казначей", x.other); });
  CHECK(hitVip(f.w(), x.other));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { orderHit(tx, x.plot, x.hero, 50); }); }), "от "));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { orderHit(tx, x.plot, x.other, 1000); }); }), "угрозы"));
  f.tx([&](Tx& tx) { orderHit(tx, x.plot, x.hero, 3000); });
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 97000, 1e-9);
  HitChance h = hitChance(f.w(), x.plot, x.hero, 3000);
  CHECK_NEAR(h.base, 30, 1e-9);
  CHECK_NEAR(h.total, 30, 1e-9);
  // Модификаторы героя: «Бдительный» −25 %, «Чрезмерно самоуверенный» +15 %.
  f.tx([&](Tx& tx) {
    addModifier(tx, ModTarget::Character, x.hero, ensureBuiltinMod(tx, schema::mod::Vigilant), 0);
    addModifier(tx, ModTarget::Character, x.hero, ensureBuiltinMod(tx, schema::mod::Overconfident), 0);
  });
  CHECK_NEAR(heroAssassinPct(f.w(), x.hero), -10, 1e-9);
  CHECK_NEAR(hitChance(f.w(), x.plot, x.hero, 3000).total, 20, 1e-9);
  // Модификатор героя — не глобальный эффект государства.
  CHECK_NEAR(factionEffects(f.w(), f.B)[Fx::AssassinPct], 0, 1e-9);
  // Перезаказ: доплата разницы; отмена — возврат.
  f.tx([&](Tx& tx) { orderHit(tx, x.plot, x.hero, 5000); });
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 95000, 1e-9);
  f.tx([&](Tx& tx) { cancelHit(tx, x.plot, x.hero); });
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 100000, 1e-9);
  // «Штаб Разведки» 5 уровня (+50 %) и «Бессмертный» значимый герой: 0 %.
  f.tx([&](Tx& tx) {
    content::addIntelBuildings(tx, f.A);
    placeBuilding(tx, f.p[4], buildingByKey(tx.w(), schema::bld::IntelHq, f.A), 5);
    addModifier(tx, ModTarget::Character, x.vip, ensureBuiltinMod(tx, schema::mod::Immortal), 0);
    orderHit(tx, x.plot, x.hero, 5000);
    orderHit(tx, x.plot, x.vip, 25000);
  });
  CHECK_EQ(intelLevel(f.w(), f.A), 5);
  CHECK_NEAR(hitChance(f.w(), x.plot, x.hero, 5000).intel, 50, 1e-9);
  CHECK_NEAR(hitChance(f.w(), x.plot, x.hero, 5000).total, 90, 1e-9);
  CHECK_NEAR(hitChance(f.w(), x.plot, x.vip, 25000).total, 0, 1e-9);   // 50 + 50 − 100
  // «Защитить от убийства» — 0 %; конец хода: защищённый жив, заказ снят.
  f.tx([&](Tx& tx) { guardHit(tx, x.plot, x.hero, true); });
  CHECK_NEAR(hitChance(f.w(), x.plot, x.hero, 5000).total, 0, 1e-9);
  TurnReport rep;
  f.tx([&](Tx& tx) { rep = endTurn(tx); });
  CHECK_EQ(rep.hits.size(), size_t(2));
  CHECK(!characterHas(f.w(), x.hero, schema::mod::Dead));
  CHECK(!characterHas(f.w(), x.vip, schema::mod::Dead));
  CHECK(x.p().hits.empty());
  // Без защиты и со 100 % — герой убит, из заговора выходит.
  f.tx([&](Tx& tx) {
    dropModifier(tx, ModTarget::Character, x.hero, ensureBuiltinMod(tx, schema::mod::Vigilant));
    orderHit(tx, x.plot, x.hero, 5000);
  });
  CHECK_NEAR(hitChance(f.w(), x.plot, x.hero, 5000).total, 100, 1e-9);
  f.tx([&](Tx& tx) { rep = endTurn(tx); });
  CHECK_EQ(rep.hits.size(), size_t(1));
  CHECK(rep.hits[0].killed);
  CHECK(characterHas(f.w(), x.hero, schema::mod::Dead));
  CHECK(!has(x.p().threats, x.hero));
}

// Золото исполненного заказа не возвращается: смерть цели не делает заказ «неисполненным»; запись об убийстве —
// в событиях хода.
TEST(rules_plots_hit_gold_spent) {
  PlotFix x;
  Fix& f = x.f;
  f.tx([&](Tx& tx) {
    addPlotHero(tx, x.plot, x.hero, false);
    removePlotHero(tx, x.plot, x.hero);
    tx.plot(x.plot).threats.push_back(x.hero);
    content::addIntelBuildings(tx, f.A);
    placeBuilding(tx, f.p[4], buildingByKey(tx.w(), schema::bld::IntelHq, f.A), 5);
    orderHit(tx, x.plot, x.hero, 5000);
  });
  CHECK_NEAR(hitChance(f.w(), x.plot, x.hero, 5000).total, 100, 1e-9);
  TurnReport rep;
  f.tx([&](Tx& tx) { rep = endTurn(tx); });
  CHECK(characterHas(f.w(), x.hero, schema::mod::Dead));
  const TurnFactionLine* line = nullptr;
  for (const TurnFactionLine& l : rep.factions)
    if (l.faction == f.A) line = &l;
  CHECK(line != nullptr);
  if (line) CHECK_NEAR(line->treasuryAfter - line->treasuryBefore, line->income - line->expenses, 1e-6);   // без возврата 5000
  bool logged = false;
  for (Id lid : rep.logIds)
    if (const LogEntry* e = f.w().logEntry(lid); e && has(e->text, "убит наёмными убийцами")) logged = true;
  CHECK(logged);
}

// Пленные и арестованные выходят из заговора; отметка расследования снимается после арестов.
TEST(rules_plots_captives_leave_and_alarm_reset) {
  PlotFix x;
  Fix& f = x.f;
  f.tx([&](Tx& tx) {
    for (Id h : {x.hero, x.vip}) addPlotHero(tx, x.plot, h, false);
    setPlotLeader(tx, x.plot, x.vip);
    addPlotProvince(tx, x.plot, f.p[0], false);
  });
  f.tx([&](Tx& tx) {
    addModifier(tx, ModTarget::Character, x.vip, ensureBuiltinMod(tx, schema::mod::Captive), 0);
    fitPlots(tx);
  });
  CHECK(!has(x.p().heroes, x.vip));
  CHECK_EQ(x.p().leader, Id(0));
  CHECK(has(x.p().heroes, x.hero));
  f.tx([&](Tx& tx) {
    tx.plot(x.plot).found = true;
    tx.plot(x.plot).stage = 1;
    tx.plot(x.plot).alarm = true;
  });
  f.tx([&](Tx& tx) { arrestPlotters(tx, f.B, x.plot); });
  CHECK(!x.p().alarm);
}

TEST(rules_plots_counterintel_detect_reveal) {
  PlotFix x;
  Fix& f = x.f;
  f.tx([&](Tx& tx) { addPlotProvince(tx, x.plot, f.p[0], false); });
  CHECK(has(detectProblem(f.w(), f.B, false), "Штаб Контрразведки"));
  f.tx([&](Tx& tx) {
    content::addIntelBuildings(tx, f.B);
    const Id cnt = buildingByKey(tx.w(), schema::bld::CounterIntelHq, f.B);
    CHECK(cnt != 0);
    tx.building(cnt).requires_.clear();
    placeBuilding(tx, f.p[1], cnt, 3);
  });
  CHECK_EQ(counterIntelLevel(f.w(), f.B), 3);
  CHECK_NEAR(detectChance(f.w(), f.B), 30, 1e-9);
  // Игрок — раз в ход.
  f.tx([&](Tx& tx) { detectPlots(tx, f.B, true); });
  CHECK(has(detectProblem(f.w(), f.B, true), "в этом ходу"));
  // Администратор — сколько угодно: рано или поздно заговор обнаружен.
  for (int i = 0; i < 200 && !x.p().found; i++) f.tx([&](Tx& tx) { detectPlots(tx, f.B, false); });
  CHECK(x.p().found);
  CHECK_EQ(foundPlots(f.w(), f.B), std::vector<Id>{x.plot});
  // Раскрытие по этапам; неудача с этапа 3 — заговорщики видят расследование.
  bool failedLate = false;
  for (int i = 0; i < 500 && x.p().stage < schema::kPlotStages; i++) {
    const int was = x.p().stage;
    bool ok = false;
    f.tx([&](Tx& tx) { ok = revealPlot(tx, f.B, x.plot, false); });
    CHECK_EQ(x.p().stage, ok ? was + 1 : was);
    if (!ok && was >= 2) failedLate = true;
  }
  CHECK_EQ(x.p().stage, schema::kPlotStages);
  CHECK_EQ(x.p().alarm, failedLate);
  CHECK(has(revealProblem(f.w(), f.B, x.plot, false), "полностью"));
}

TEST(rules_plots_arrests_and_prison) {
  PlotFix x;
  Fix& f = x.f;
  f.tx([&](Tx& tx) {
    addPlotProvince(tx, x.plot, f.p[0], false);
    addPlotProvince(tx, x.plot, f.p[1], false);
    setPlotCommitment(tx, x.plot, f.p[0], 50);
    setPlotCommitment(tx, x.plot, f.p[1], 30);
    for (Id h : {x.hero, x.vip, x.other}) addPlotHero(tx, x.plot, h, false);
    setPlotLeader(tx, x.plot, x.other);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { arrestPlotters(tx, f.B, x.plot); }); }), "раскройте"));
  // Этап 1: −20 % приверженности, 2 % населения — в тюрьму, «Аресты заговорщиков», заговор скрыт.
  f.tx([&](Tx& tx) {
    tx.plot(x.plot).found = true;
    tx.plot(x.plot).stage = 1;
  });
  ArrestResult r;
  f.tx([&](Tx& tx) { r = arrestPlotters(tx, f.B, x.plot); });
  CHECK_EQ(r.people, i64(400));
  CHECK_EQ(popOf(f.w(), f.p[0]), 9800);
  CHECK_NEAR(x.p().province(f.p[0])->commitment, 30, 1e-9);
  CHECK_NEAR(x.p().province(f.p[1])->commitment, 10, 1e-9);
  CHECK(provinceHas(f.w(), f.p[0], schema::mod::PlotArrests));
  CHECK(!x.p().found && x.p().stage == 0);
  CHECK_EQ(f.w().faction(f.B)->prison.at(0).pop, i64(400));
  // Этап 3: население по приверженности (30 % → 6 %), провинции выходят, обычные герои (не лидер) — под арест.
  f.tx([&](Tx& tx) {
    tx.plot(x.plot).found = true;
    tx.plot(x.plot).stage = 3;
  });
  f.tx([&](Tx& tx) { r = arrestPlotters(tx, f.B, x.plot); });
  CHECK_EQ(r.people, i64(588 + 196));   // 9800 × 6 % и 9800 × 2 %
  CHECK(x.p().provinces.empty());
  CHECK(provinceHas(f.w(), f.p[0], schema::mod::PlotExposed));
  CHECK(has(plotProvinceProblem(f.w(), x.plot, f.p[0], false), "Раскрытый"));
  CHECK_EQ(r.heroes, std::vector<Id>{x.hero});
  CHECK(characterHas(f.w(), x.hero, schema::mod::Arrested));
  CHECK_EQ(f.w().character(x.hero)->jailer, f.B);
  CHECK(!heroAvailable(f.w(), x.hero));
  CHECK(has(x.p().heroes, x.vip) && has(x.p().heroes, x.other));
  CHECK_EQ(prisonHeroes(f.w(), f.B), std::vector<Id>{x.hero});
  // Арестованного нельзя назначить.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRuler(tx, f.B, x.hero); }); }), "под арестом"));
  // Этап 5: все с лидером, заговор удаляется.
  f.tx([&](Tx& tx) {
    tx.plot(x.plot).found = true;
    tx.plot(x.plot).stage = 5;
  });
  f.tx([&](Tx& tx) { r = arrestPlotters(tx, f.B, x.plot); });
  CHECK(r.removed);
  CHECK(f.w().plot(x.plot) == nullptr);
  CHECK(characterHas(f.w(), x.other, schema::mod::Arrested));
  // Тюрьма: освобождение людей (к своей расе в провинции государства) и героев.
  const i64 jailed = f.w().faction(f.B)->prison.at(0).pop;
  const i64 before = popOf(f.w(), f.p[0]) + popOf(f.w(), f.p[1]);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { releasePrisoners(tx, f.B, {{x.race, jailed + 1}}); }); }), "нет столько"));
  f.tx([&](Tx& tx) { releasePrisoners(tx, f.B, {{x.race, 100}}); });
  CHECK_EQ(popOf(f.w(), f.p[0]) + popOf(f.w(), f.p[1]), before + 100);
  CHECK_EQ(f.w().faction(f.B)->prison.at(0).pop, jailed - 100);
  f.tx([&](Tx& tx) { releaseHero(tx, f.B, x.hero); });
  CHECK(!characterHas(f.w(), x.hero, schema::mod::Arrested));
  CHECK_EQ(f.w().character(x.hero)->jailer, Id(0));
  CHECK(heroAvailable(f.w(), x.hero));
}

TEST(rules_plots_execute) {
  PlotFix x;
  Fix& f = x.f;
  const Id powder = idByName(f.w().catalogs->resources, "Порох");
  const Id steel = idByName(f.w().catalogs->resources, "Сталь");
  const Id mech = idByName(f.w().catalogs->resources, "Механические детали");
  const Id parts = resourceId(f.w(), schema::kResMechParts);
  CHECK(powder && steel && mech && parts);
  Id row = 0, army = 0;
  f.tx([&](Tx& tx) {
    addPlotProvince(tx, x.plot, f.p[0], false);
    setPlotCommitment(tx, x.plot, f.p[0], 50);
    for (auto [r, v] : std::vector<std::pair<Id, double>>{{powder, 500}, {steel, 1000}, {mech, 100}, {parts, 10}}) tx.faction(f.A).res[r] = v;
    supplyPlot(tx, x.plot, f.p[0], {{powder, 500}, {steel, 1000}, {mech, 100}, {parts, 10}});
    addPlotHero(tx, x.plot, x.hero, false);
    addPlotHero(tx, x.plot, x.vip, false);
    setPlotLeader(tx, x.plot, x.hero);
    // Значимый герой — в войске B вместе с другим героем: откалывается половина войска.
    row = addArmyRow(tx, f.B, UnitType::HeavyInf, "Стража", 400, 1);
    army = createArmy(tx, ArmyKind::Army, f.B, center(1));
    setUnits(tx, army, f.B, row, 400);
    setHero(tx, army, x.vip, true);
    setHero(tx, army, x.other, true);
    addCouncilSeat(tx, f.B, "Маршал", x.hero);
  });
  // Отряды провинции: 10 000 × 50 % × 0,2 % = 1000 восставших.
  const PlotProvince pp = *x.p().province(f.p[0]);
  CHECK_EQ(plotRebels(f.w(), pp), i64(1000));
  const std::vector<PlotSquad> sq = plotSquads(f.w(), pp, 1000);
  CHECK_EQ(sq.size(), size_t(5));
  CHECK_EQ(sq[0].name, std::string("Заговорщики-Мушкетеры"));
  CHECK_EQ(sq[0].count, i64(100));
  CHECK_EQ(sq[1].name, std::string("Заговорщики-Латники"));
  CHECK_EQ(sq[1].count, i64(200));
  CHECK_EQ(sq[2].name, std::string("Заговорщики-Ополченцы"));
  CHECK_EQ(sq[2].count, i64(700));
  CHECK_EQ(sq[3].name, std::string("Баллисты"));
  CHECK_EQ(sq[3].count, i64(2));
  CHECK(sq[3].type == UnitType::Machines && sq[3].race == schema::kRaceMechanical);
  CHECK_EQ(sq[4].count, i64(2));   // големы
  PlotResult res;
  f.tx([&](Tx& tx) { res = executePlot(tx, x.plot); });
  const Faction* S = f.w().faction(res.state);
  CHECK(S && S->conspiracy && S->rebelOf == f.B);
  CHECK_EQ(S->name, std::string("Заговор Бран"));
  CHECK_EQ(S->ruler, x.hero);
  CHECK(f.w().relation(res.state, f.B).s == RelStatus::War);
  CHECK(f.w().relation(res.state, f.A).s == RelStatus::Alliance);
  CHECK(f.w().plot(x.plot) == nullptr);
  CHECK_EQ(res.rebels, i64(1000));
  CHECK_EQ(popOf(f.w(), f.p[0]), 9000);
  CHECK_NEAR(f.w().province(f.p[0])->contentment, 0, 1e-9);
  // Герои ушли к заговору, советник снят с должности.
  CHECK_EQ(f.w().character(x.hero)->faction, res.state);
  CHECK_EQ(f.w().character(x.vip)->faction, res.state);
  CHECK_EQ(f.w().faction(f.B)->council.at(0).character, Id(0));
  // Войско B: из двух героев один заговорщик — половина отрядов откололась.
  CHECK_EQ(unitsIn(f.w(), army), 200);
  CHECK_EQ(f.w().faction(f.B)->armyRow(row)->total, 200);
  i64 rebelUnits = 0;
  bool vipWithSplit = false;
  for (Id a : res.armies) {
    rebelUnits += unitsIn(f.w(), a);
    for (const ArmyGroup& g : f.w().army(a)->groups)
      if (has(g.heroes, x.vip)) vipWithSplit = unitsIn(f.w(), a) == 200;
  }
  CHECK(vipWithSplit);
  CHECK_EQ(rebelUnits, 1000 + 4 + 200);
  // Свободный лидер — в войске восстания.
  bool leaderPlaced = false;
  for (Id a : res.armies)
    for (const ArmyGroup& g : f.w().army(a)->groups) leaderPlaced = leaderPlaced || has(g.heroes, x.hero);
  CHECK(leaderPlaced);
  // Мятеж войск B образует своё «Мятеж (…)», не государство заговора.
  f.tx([&](Tx& tx) { CHECK(rebelStateFor(tx, f.B) != res.state); });
}

TEST(rules_plots_cleanup) {
  PlotFix x;
  Fix& f = x.f;
  f.tx([&](Tx& tx) {
    addPlotProvince(tx, x.plot, f.p[0], false);
    addPlotHero(tx, x.plot, x.hero, false);
    tx.plot(x.plot).threats.push_back(x.vip);
    orderHit(tx, x.plot, x.vip, 1000);
  });
  CHECK(executeProblem(f.w(), x.plot).find("лидера") != std::string::npos);
  // Провинция сменила владельца — выходит из заговора; угроза умерла — заказ снят с возвратом.
  f.tx([&](Tx& tx) {
    setProvinceOwner(tx, f.p[0], f.C);
    addModifier(tx, ModTarget::Character, x.vip, ensureBuiltinMod(tx, schema::mod::Dead), 0);
  });
  CHECK(x.p().provinces.empty());
  CHECK(x.p().threats.empty() && x.p().hits.empty());
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 100000, 1e-9);
  // Государство-цель упразднено — заговор удаляется.
  f.tx([&](Tx& tx) { removeFaction(tx, f.B); });
  CHECK(f.w().plot(x.plot) == nullptr);
}
