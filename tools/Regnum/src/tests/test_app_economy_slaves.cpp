// Сценарии рабов (ТЗ «Механика войн», п.1; «Механика мятежа», п.5): раздел «Рабы» вкладки «Экономика» государства —
// раса, численность, довольство −100…100, содержание 0,001 за раба; рабы на работах во вкладке «Население»
// провинции — не больше 10 % населения и свободных рабов расы, доход 0,002 за раба владельцу.
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

const SlaveGroup* groupOf(const Faction& f, Id race) {
  for (const SlaveGroup& g : f.slaves)
    if (g.race == race) return &g;
  return nullptr;
}

i64 atWork(const Province& p, Id race) {
  i64 n = 0;
  for (const SlaveWork& s : p.slaves)
    if (s.race == race) n += s.count;
  return n;
}

}  // namespace

TEST(app_economy_slaves_faction) {
  HideTestRegs hide;
  Harness h("economy_slaves_faction", 1440, 1100);
  h.demo();
  const Id st = biggestState(h->world());
  CHECK(st != 0);
  CHECK(h->world().faction(st)->slaves.empty());
  openTab(h, st, "faction.economy");
  CHECK(ensureVisible(h, "economy.slaves"));
  // Добавить расу рабов из справочника рас.
  CHECK(clickIn(h, "economy.slaveAdd"));
  h.key(Key::Down);
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(h->world().faction(st)->slaves.size(), size_t(1));
  const Id race = h->world().faction(st)->slaves.front().race;
  CHECK(Catalogs::find(h->world().catalogs->races, race) != nullptr);
  const SlaveGroup* g = groupOf(*h->world().faction(st), race);
  CHECK(g != nullptr);
  CHECK_EQ(g->count, i64(0));
  // Численность и довольство.
  const std::string count = "economy.slave." + std::to_string(race) + ".count";
  const std::string content = "economy.slave." + std::to_string(race) + ".content";
  CHECK(typeNumber(h, count, "1500"));
  CHECK_EQ(groupOf(*h->world().faction(st), race)->count, i64(1500));
  CHECK(typeNumber(h, content, "-30"));
  CHECK_NEAR(groupOf(*h->world().faction(st), race)->contentment, -30, 1e-9);
  CHECK(typeNumber(h, content, "250"));   // довольство — не больше 100
  CHECK_NEAR(groupOf(*h->world().faction(st), race)->contentment, 100, 1e-9);
  // Содержание: 0,001 золота за раба — в расходах государства.
  const rules::FactionCalc* fc = rules::calc(h->world())->faction(st);
  CHECK_EQ(fc->slaves, i64(1500));
  CHECK_NEAR(fc->expSlaves, 1.5, 1e-9);
  CHECK(ensureVisible(h, count));
  shotClean(h, "economy_slaves_faction");
  // Убрать расу рабов (никто не на работах) и отмена.
  CHECK(clickIn(h, "economy.slaveDel." + std::to_string(race)));
  h.settle();
  CHECK(groupOf(*h->world().faction(st), race) == nullptr);
  h.key(Key::Z, ctrl());
  CHECK(groupOf(*h->world().faction(st), race) != nullptr);
}

TEST(app_economy_slaves_at_work) {
  HideTestRegs hide;
  Harness h("economy_slaves_work", 1440, 1200);
  h.demo();
  const Id st = biggestState(h->world());
  const Id pid = populousProvince(h->world(), st);
  CHECK(st && pid);
  const Id race = h->world().catalogs->races.front().id;
  const i64 pool = 2000;
  CHECK(h->act("Рабы", [&](Tx& tx) { rules::setSlaves(tx, st, race, pool, -10); }));
  const i64 limit = rules::slaveWorkLimit(h->world(), pid);
  CHECK_EQ(limit, i64(std::floor(double(popOf(*prov(h, pid))) * 0.10)));
  CHECK(limit > 100);
  openProvince(h, pid, "province.population");
  const std::string field = "province.slaveWork." + std::to_string(race);
  CHECK(ensureVisible(h, field));
  CHECK(h->uiRect("province.slaves") != nullptr);
  // На работы — 100 рабов: доход 0,002 за раба владельцу.
  CHECK(typeNumber(h, field, "100"));
  CHECK_EQ(atWork(*prov(h, pid), race), i64(100));
  auto c = rules::calc(h->world());
  CHECK_EQ(c->province(pid)->slavesAtWork, i64(100));
  CHECK_NEAR(c->province(pid)->slaveIncome, 0.2, 1e-12);
  CHECK_NEAR(c->faction(st)->incSlaves, 0.2, 1e-12);
  CHECK(h->uiRect("province.slaveIncome") != nullptr);
  // Больше предела — поле ограничено: не больше 10 % населения и не больше свободных рабов.
  CHECK(typeNumber(h, field, "99999999"));
  CHECK_EQ(atWork(*prov(h, pid), race), std::min(limit, pool));
  // Правило: больше 10 % населения нельзя.
  CHECK(!h->act("Рабы", [&](Tx& tx) { rules::setSlaveWork(tx, pid, race, limit + 1); }));
  // Рабов государства меньше, чем на работах, — отказ.
  CHECK(!h->act("Рабы", [&](Tx& tx) { rules::setSlaves(tx, st, race, 1, 0); }));
  CHECK(ensureVisible(h, field));
  shotClean(h, "economy_slaves_work");
  // Экономика государства: строка «Рабы на работах» и содержание рабов.
  openTab(h, st, "faction.economy");
  shotClean(h, "economy_slaves_income");
  CHECK_NEAR(rules::calc(h->world())->faction(st)->expSlaves, double(pool) * 0.001, 1e-9);
}
