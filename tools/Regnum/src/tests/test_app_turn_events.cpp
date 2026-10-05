// Сценарии событий хода (ТЗ «Механика мятежа», п.1.2 и 4): после «Завершить ход» окна идут по очереди — мятеж
// войска с верностью −100 % («Судьба героя» верного героя, затем захват провинции мятежниками) и восстание
// провинции («Мятежные крестьяне»; без войск и гарнизона — сразу выбор захвата).
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Провинция государства без войск, гарнизона и оккупации, где у точки подписи можно поставить войско.
Id calmProvince(Harness& h, Id state, Id skip = 0) {
  const World& w = h->world();
  auto fs = geo::faces(w);
  Id found = 0;
  w.provinces.each([&](const Province& p) {
    if (found || p.id == skip || p.sea || p.owner != state || p.occupied || !p.garrison.empty()) return;
    i64 pop = 0;
    for (const RacePop& r : p.races) pop += r.pop;
    if (pop < 100) return;
    const geo::ProvinceShape* sh = fs->shape(p.id);
    if (!sh || !rules::validPosition(w, ArmyKind::Army, sh->label)) return;
    bool busy = false;
    w.armies.each([&](const Army& a) { busy = busy || app::mil::provinceUnder(w, a.pos) == p.id; });
    if (!busy) found = p.id;
  });
  return found;
}

}  // namespace

TEST(app_turn_events_full_mutiny) {
  Harness h("turn_events_mutiny", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel);
  CHECK(pid != 0);
  if (!pid) return;
  const Id hero = freeHero(h, hel);
  CHECK(hero != 0);
  const Id army = spawnArmy(h, hel, geo::faces(h->world())->shape(pid)->label, 300, hero);
  CHECK(army != 0);
  CHECK(h->act("Верность −100 %", [&](Tx& tx) { rules::setArmyLoyalty(tx, army, -100); }));
  CHECK(h->endTurnNow());
  h.settle();
  CHECK(!h->world().army(army));
  Id rebel = 0;
  h->world().factions.each([&](const Faction& f) {
    if (f.rebelOf == hel) rebel = f.id;
  });
  CHECK(rebel != 0);
  // Верный герой — «Судьба героя»; «сбежал» — герой снова у Хельдвига.
  CHECK(h->hasDialog("hero.fate"));
  CHECK(h.clickUi("fate." + std::to_string(hero) + ".0"));
  CHECK(h.clickUi("fate.ok"));
  h.settle();
  CHECK(!h->hasDialog("hero.fate"));
  CHECK_EQ(h->world().character(hero)->faction, hel);
  CHECK(rules::heroAvailable(h->world(), hero));
  // Затем мятежники в провинции без войск и гарнизона — выбор захвата.
  CHECK(h->hasDialog("capture"));
  CHECK(h.clickUi("capture.cancel"));
  h.settle();
  CHECK(!h->hasDialog());
  CHECK(!h->world().province(pid)->occupied);
}

TEST(app_turn_events_province_uprising) {
  Harness h("turn_events_uprising", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel);
  CHECK(pid != 0);
  if (!pid) return;
  i64 pop = 0;
  for (const RacePop& r : h->world().province(pid)->races) pop += r.pop;
  // Риск восстания 100 % и бросок восстания в конце хода.
  CHECK(h->act("Риск восстания", [&](Tx& tx) {
    tx.settings().rebellionRoll = true;
    Id m = rules::createModifier(tx, "Смута");
    Modifier& md = tx.modifier(m);
    md.fx[size_t(Fx::RebellionPct)] = 100;
    md.fxMask |= 1u << unsigned(Fx::RebellionPct);
    rules::addModifier(tx, rules::ModTarget::Province, pid, m);
  }));
  CHECK(h->endTurnNow());
  h.settle();
  Id rebelArmy = 0;
  h->world().armies.each([&](const Army& a) {
    if (a.name == "Мятежные крестьяне" && app::mil::provinceUnder(h->world(), a.pos) == pid) rebelArmy = a.id;
  });
  CHECK(rebelArmy != 0);
  i64 after = 0;
  for (const RacePop& r : h->world().province(pid)->races) after += r.pop;
  CHECK(after < pop);
  // Ни войска, ни гарнизона — сразу выбор захвата; «захватить» — оккупация мятежниками.
  CHECK(h->hasDialog("capture"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("turn_events_uprising_capture"));
  CHECK(h.clickUi("capture.option.0"));
  CHECK(h.clickUi("capture.ok"));
  h.settle();
  const Province* p = h->world().province(pid);
  CHECK(p->occupied);
  CHECK(h->world().faction(p->occupier) && h->world().faction(p->occupier)->rebelOf == hel);
}
