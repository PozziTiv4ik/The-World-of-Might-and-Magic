// Regnum — правила технологий: изучение и условия (циклы, одно дерево), раскладка дерева, общее дерево (каждое
// государство изучает само), стоимость исследования и нужные постройки, время исследования с модификаторами.
#include "core/content.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

// ---------------------------------------------------------------- изучение и раскладка
TEST(rules_tech_research_and_prereqs) {
  Fix f;
  Id t1 = 0, t2 = 0, t3 = 0, tb = 0;
  f.tx([&](Tx& tx) {
    t1 = createTech(tx, f.A, "Бронза");
    t2 = createTech(tx, f.A, "Железо");
    t3 = createTech(tx, f.A, "Сталь");
    tb = createTech(tx, f.B, "Чужая");
    tx.tech(t1).turns = 2;
    setPrereq(tx, t2, t1, true);
    setPrereq(tx, t3, t1, true);
    setPrereq(tx, t3, t2, true);
    setPrereq(tx, t3, t2, true);  // повтор не дублируется
  });
  CHECK_EQ(f.w().tech(t3)->prereqs.size(), size_t(2));
  ResearchCheck c = canResearch(f.w(), t3);
  CHECK(!c.ok);
  CHECK(c.missing == std::vector<Id>({t1, t2}));
  CHECK(canResearch(f.w(), t1).ok);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startResearch(tx, t2); }); }), "Сначала изучите: «Бронза»"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setStudied(tx, t2, true); }); }), "Сначала изучите"));
  CHECK(wouldCycle(f.w(), t1, t3));
  CHECK(wouldCycle(f.w(), t1, t1));
  CHECK(!wouldCycle(f.w(), t3, t1));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setPrereq(tx, t1, t3, true); }); }), "цикл"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setPrereq(tx, t1, t1, true); }); }), "самой себя"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setPrereq(tx, t1, tb, true); }); }), "одного дерева"));
  f.tx([&](Tx& tx) { startResearch(tx, t1); });
  CHECK(f.w().tech(t1)->research);
  CHECK(has(lastLog(f.w()), "начато исследование «Бронза», осталось 2 хода"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startResearch(tx, t1); }); }), "уже исследуется"));
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(f.w().tech(t1)->progress, 1);
  f.tx([&](Tx& tx) { stopResearch(tx, t1); });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK_EQ(f.w().tech(t1)->progress, 1);  // без исследования прогресс стоит, но сохраняется
  f.tx([&](Tx& tx) { startResearch(tx, t1); });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(f.w().tech(t1)->studied);
  CHECK(!f.w().tech(t1)->research);
  CHECK(has(lastLog(f.w(), "изучена"), "«Арден»: изучена технология «Бронза»"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { startResearch(tx, t1); }); }), "уже изучена"));
  f.tx([&](Tx& tx) { setStudied(tx, t2, true); });  // отметкой
  CHECK(f.w().tech(t2)->studied);
  CHECK_EQ(f.w().tech(t2)->progress, f.w().tech(t2)->turns);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setStudied(tx, t1, false); }); }), "От неё зависят изученные технологии: «Железо»"));
  f.tx([&](Tx& tx) { setStudied(tx, t2, false); });
  CHECK(!f.w().tech(t2)->studied);
  CHECK_EQ(f.w().tech(t2)->progress, 0);
  f.tx([&](Tx& tx) { setPrereq(tx, t3, t2, false); });
  CHECK(f.w().tech(t3)->prereqs == std::vector<Id>({t1}));
  CHECK(canResearch(f.w(), t3).ok);
}

TEST(rules_tech_autolayout) {
  Fix f;
  Id r1 = 0, r2 = 0, x = 0, y = 0, e = 0, g = 0;
  f.tx([&](Tx& tx) {
    r1 = createTech(tx, f.A, "Корень 1");
    r2 = createTech(tx, f.A, "Корень 2");
    x = createTech(tx, f.A, "X");  // зависит от r2
    y = createTech(tx, f.A, "Y");  // зависит от r1: в порядке ID связи пересекаются
    e = createTech(tx, f.A, "E");
    g = createTech(tx, f.A, "G");
    setPrereq(tx, x, r2, true);
    setPrereq(tx, y, r1, true);
    setPrereq(tx, e, x, true);
    setPrereq(tx, e, y, true);
    setPrereq(tx, g, r1, true);  // длинная связь через два слоя
    setPrereq(tx, g, e, true);
    createTech(tx, f.B, "Чужая");
  });
  f.tx([&](Tx& tx) { autoLayout(tx, f.A); });
  const World& w = f.w();
  auto P = [&](Id id) { return w.tech(id)->pos; };
  CHECK_NEAR(P(r1).x, 0, 1e-9);
  CHECK_NEAR(P(r2).x, 0, 1e-9);
  CHECK_NEAR(P(x).x, kTreeColStep, 1e-9);
  CHECK_NEAR(P(y).x, kTreeColStep, 1e-9);
  CHECK_NEAR(P(e).x, 2 * kTreeColStep, 1e-9);
  CHECK_NEAR(P(g).x, 3 * kTreeColStep, 1e-9);  // слой — длиннейший путь от корня
  // Пересечение снято: порядок детей совпадает с порядком родителей.
  CHECK((P(r1).y < P(r2).y) == (P(y).y < P(x).y));
  CHECK_NEAR(std::fabs(P(r1).y - P(r2).y), kTreeRowStep, 1e-9);
  CHECK_NEAR(P(e).y, kTreeRowStep * 0.5, 1e-9);  // один узел в слое — по центру
  // Повторная раскладка ничего не меняет.
  World before = f.w();
  f.tx([&](Tx& tx) { autoLayout(tx, f.A); });
  CHECK_EQ(World::diff(before, f.w()), 0u);
}

// ---------------------------------------------------------------- общее дерево технологий
TEST(rules_tech_common_tree) {
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

// ---------------------------------------------------------------- стоимость и время исследования
TEST(rules_tech_cost_and_buildings) {
  Fix f;
  const Id treasure = test::idByName(f.w().catalogs->resources, "Археологические сокровища");
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
  // Глобальный модификатор технологии: сокровища групп +10 %.
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::ArchTreasurePct], 10, 1e-9);
}

// Остановка исследования в конце хода (нет нужной постройки или снята предшествующая) и удаление исследуемой
// технологии возвращают уплаченное.
TEST(rules_tech_stop_refunds) {
  Fix f;
  const Id treasure = test::idByName(f.w().catalogs->resources, "Археологические сокровища");
  Id basics = 0, guildB = 0, own = 0;
  f.w().techs.each([&](const Tech& t) {
    if (t.faction == 0 && t.prereqs.empty() && !t.needKeys.empty()) basics = t.id;
  });
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[treasure] = 150;
    tx.province(f.p[0]).owner = f.A;
    guildB = tx.add(content::archGuildFor(f.A, {})).id;
    placeBuilding(tx, f.p[0], guildB, 1);
    startResearch(tx, basics, f.A);
  });
  CHECK_NEAR(f.w().faction(f.A)->stock(treasure), 50, 1e-9);
  // Провинция с «Гильдией Археологов» ушла — в конце хода исследование остановлено, сокровища возвращены.
  f.tx([&](Tx& tx) { tx.province(f.p[0]).owner = f.B; });
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(!techState(f.w(), basics, f.A).research);
  CHECK(!techStudied(f.w(), basics, f.A));
  CHECK_NEAR(f.w().faction(f.A)->stock(treasure), 150, 1e-9);
  CHECK(has(lastLog(f.w(), "остановлено"), "нужна достроенная постройка"));
  // Удаление исследуемой технологии своего дерева — возврат.
  f.tx([&](Tx& tx) {
    own = createTech(tx, f.A, "Свод");
    tx.tech(own).cost[treasure] = 40;
    tx.tech(own).turns = 3;
    startResearch(tx, own);
  });
  CHECK_NEAR(f.w().faction(f.A)->stock(treasure), 110, 1e-9);
  f.tx([&](Tx& tx) { removeTech(tx, own); });
  CHECK_NEAR(f.w().faction(f.A)->stock(treasure), 150, 1e-9);
}

TEST(rules_tech_research_time_factor) {
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
