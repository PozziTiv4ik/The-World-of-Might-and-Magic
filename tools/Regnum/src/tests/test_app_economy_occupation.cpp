// Сценарии оккупационного гарнизона (ТЗ «Общие доработки», п.14): раздел «Оккупация» обзора провинции — отряды
// из резерва оккупанта (не больше резерва), счётчик «без гарнизона и войск N из 5 ходов»; без гарнизона и войск
// оккупанта пять ходов подряд оккупация снимается, с гарнизоном счётчик не растёт.
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

i64 garrisonOf(const Province& p, Id row) {
  i64 n = 0;
  for (const GarrisonEntry& g : p.occGarrison)
    if (g.row == row) n += g.count;
  return n;
}

// Строка войск фракции со свободным резервом (при нужде резерв пополняется правкой без ограничений).
Id rowWithReserve(Harness& h, Id faction, i64 atLeast) {
  for (const ArmyRow& r : h->world().faction(faction)->army)
    if (rules::reserveOf(h->world(), faction, r.id) >= atLeast) return r.id;
  Id row = 0;
  CHECK(h->act("Резерв", [&](Tx& tx) {
    const Faction* f = tx.w().faction(faction);
    row = f->army.empty() ? rules::addArmyRow(tx, faction, UnitType::LightInf, "Ополчение", 0, 1) : f->army.front().id;
    rules::setRowTotal(tx, faction, row, tx.w().faction(faction)->armyRow(row)->total + atLeast);
  }));
  return row;
}

// Провинция государства (не столица, без оккупации), где нет ни одного войска на карте.
Id quietProvince(const World& w) {
  auto fs = geo::faces(w);
  std::vector<Id> busy;
  w.armies.each([&](const Army& a) {
    if (Id p = fs->provinceAt(a.pos)) busy.push_back(p);
  });
  Id best = 0;
  w.provinces.each([&](const Province& p) {
    if (best || p.sea || !p.owner || p.occupied || std::find(busy.begin(), busy.end(), p.id) != busy.end()) return;
    const Faction* f = w.faction(p.owner);
    if (f && f->isState() && f->capital != p.id) best = p.id;
  });
  return best;
}

}  // namespace

TEST(app_economy_occupation_garrison) {
  HideTestRegs hide;
  Harness h("economy_occupation", 1440, 1100);
  h.demo();
  Id pid = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid && p.occupied && p.occupier && !p.sea) pid = p.id;
  });
  CHECK(pid != 0);
  const Id occ = prov(h, pid)->occupier;
  const Id row = rowWithReserve(h, occ, 50);
  const i64 reserve = rules::reserveOf(h->world(), occ, row);
  CHECK(reserve >= 50);
  openProvince(h, pid, "province.overview");
  const std::string field = "province.occGarrison." + std::to_string(row);
  CHECK(ensureVisible(h, field));
  CHECK(h->uiRect("province.occIdle") != nullptr);
  // Назначить отряды из резерва оккупанта.
  const i64 want = std::min<i64>(reserve, 120);
  CHECK(typeNumber(h, field, std::to_string(want)));
  CHECK_EQ(garrisonOf(*prov(h, pid), row), want);
  CHECK_EQ(rules::reserveOf(h->world(), occ, row), reserve - want);
  const rules::Deployed dep = rules::deployed(h->world(), occ);
  CHECK_EQ(dep.occupation.count(row) ? dep.occupation.at(row) : i64(0), want);
  CHECK_EQ(prov(h, pid)->occIdle, 0);
  // Больше резерва нельзя: поле ограничено «в гарнизоне + резерв».
  CHECK(typeNumber(h, field, std::to_string(reserve + 100000)));
  CHECK_EQ(garrisonOf(*prov(h, pid), row), reserve);
  CHECK_EQ(rules::reserveOf(h->world(), occ, row), i64(0));
  // Правило тоже не даёт больше резерва.
  CHECK(!h->act("Гарнизон", [&](Tx& tx) { rules::setOccupationGarrison(tx, pid, row, reserve + 1); }));
  CHECK(ensureVisible(h, field));
  shotClean(h, "economy_occupation_garrison");
  // Снять оккупацию — гарнизон возвращается в резерв оккупанта.
  CHECK(h->act("Снять оккупацию", [&](Tx& tx) { rules::setOccupied(tx, pid, 0); }));
  CHECK(prov(h, pid)->occGarrison.empty());
  CHECK_EQ(rules::reserveOf(h->world(), occ, row), reserve);
  h.settle();
  CHECK(h->uiRect(field) == nullptr);
}

TEST(app_economy_occupation_idle) {
  HideTestRegs hide;
  Harness h("economy_occupation_idle", 1440, 1100);
  h.demo();
  const Id pid = quietProvince(h->world());
  CHECK(pid != 0);
  const Id owner = prov(h, pid)->owner;
  Id occ = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!occ && f.isState() && f.id != owner) occ = f.id;
  });
  CHECK(occ != 0);
  CHECK(h->act("Оккупация", [&](Tx& tx) { rules::setOccupied(tx, pid, occ); }));
  openProvince(h, pid, "province.overview");
  CHECK(ensureVisible(h, "province.occIdle"));
  // Без гарнизона и войск оккупанта счётчик растёт; на пятом ходу оккупация снимается.
  for (int t = 1; t < schema::kOccupationIdleTurns; t++) {
    CHECK(h->endTurnNow());
    h->toasts().clear();
    CHECK(prov(h, pid)->occupied);
    CHECK_EQ(prov(h, pid)->occIdle, t);
  }
  h.settle();
  CHECK(ensureVisible(h, "province.occIdle"));
  shotClean(h, "economy_occupation_idle");
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK(!prov(h, pid)->occupied);
  CHECK_EQ(prov(h, pid)->occIdle, 0);
  // С оккупационным гарнизоном счётчик не растёт.
  CHECK(h->act("Оккупация", [&](Tx& tx) { rules::setOccupied(tx, pid, occ); }));
  const Id row = rowWithReserve(h, occ, 10);
  CHECK(h->act("Гарнизон", [&](Tx& tx) { rules::setOccupationGarrison(tx, pid, row, 10); }));
  for (int t = 0; t < 2; t++) {
    CHECK(h->endTurnNow());
    h->toasts().clear();
  }
  CHECK(prov(h, pid)->occupied);
  CHECK_EQ(prov(h, pid)->occIdle, 0);
}
