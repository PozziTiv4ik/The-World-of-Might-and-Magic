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

// Войска оккупанта уходят из провинции (расформировываются); new — новое пустое войско оккупанта у подписи провинции:
// пополнять оккупационный гарнизон можно, только пока в провинции стоит войско оккупанта (аудит 2026-10-23, 5.2).
bool occupierArmy(Harness& h, Id pid, Id occ, bool fresh) {
  auto fs = geo::faces(h->world());
  const geo::ProvinceShape* sh = fs ? fs->shape(pid) : nullptr;
  if (!sh) return false;
  return h->act("Войска оккупанта", [&](Tx& tx) {
    for (Id aid : tx.w().armies.ids())
      if (const Army* a = tx.w().army(aid); a && !a->isFleet() && a->leader() == occ && fs->provinceAt(a->pos) == pid) rules::disband(tx, aid);
    if (!fresh) return;
    auto spot = rules::findFreeSpot(tx.w(), ArmyKind::Army, sh->label);
    if (!spot || fs->provinceAt(*spot) != pid) fail("нет места для войска оккупанта");
    rules::createArmy(tx, ArmyKind::Army, occ, *spot);
  });
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
  Harness h("economy_occupation", 1440, 1100);
  h.demo();
  Id pid = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid && p.occupied && p.occupier && !p.sea) pid = p.id;
  });
  CHECK(pid != 0);
  const Id occ = prov(h, pid)->occupier;
  CHECK(occupierArmy(h, pid, occ, false));   // войска оккупанта уходят из провинции
  const Id row = rowWithReserve(h, occ, 50);
  const i64 reserve = rules::reserveOf(h->world(), occ, row);
  CHECK(reserve >= 50);
  openProvince(h, pid, "province.overview");
  const std::string field = "province.occGarrison." + std::to_string(row);
  CHECK(ensureVisible(h, field));
  CHECK(h->uiRect("province.occIdle") != nullptr);
  // Без войска оккупанта в провинции гарнизон не пополнить: поле выключено, причина — в подсказке.
  CHECK(typeNumber(h, field, "10"));
  CHECK_EQ(garrisonOf(*prov(h, pid), row), 0);
  if (const RectF* r = h->uiRect(field)) {
    const RectF fr = *r;
    h.move(fr.x - 40, fr.cy() + 40);
    h.frames(4);
    h.move(fr.cx(), fr.cy());
    h.frames(40);
    CHECK_MSG(ui::tooltipText().find("стоит войско") != std::string::npos, ui::tooltipText());
  }
  CHECK(occupierArmy(h, pid, occ, true));
  CHECK(ensureVisible(h, field));
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
  CHECK(h->act("Оккупация", [&](Tx& tx) {
    // Вне войны оккупация снимается в конце хода сразу (аудит 2026-10-23, 5.2).
    if (tx.w().relation(occ, owner).s != RelStatus::War) rules::setRelation(tx, occ, owner, -60, RelStatus::War);
    rules::setOccupied(tx, pid, occ);
  }));
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
  CHECK(occupierArmy(h, pid, occ, true));   // гарнизон пополняется, пока в провинции стоит войско оккупанта
  CHECK(h->act("Гарнизон", [&](Tx& tx) { rules::setOccupationGarrison(tx, pid, row, 10); }));
  CHECK(occupierArmy(h, pid, occ, false));
  for (int t = 0; t < 2; t++) {
    CHECK(h->endTurnNow());
    h->toasts().clear();
  }
  CHECK(prov(h, pid)->occupied);
  CHECK_EQ(prov(h, pid)->occIdle, 0);
}
