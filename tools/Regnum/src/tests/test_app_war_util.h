// Помощники сценариев войны, мятежа и судьбы героев (ТЗ «Механика войн», «Механика мятежа», «Механика героев»):
// знак «замок» или «башня» в провинции государства, новое войско рядом, перетаскивание по шагам, ввод чисел в поля.
#pragma once
#include "app/flows.h"
#include "map/art_scene.h"
#include "tests/test_app_military_util.h"

namespace rg::apptest {

// Знак карты для штурма: замок или башня в провинции state, вокруг которого нет других объектов.
struct SiegeSpot {
  Id symbol = 0, province = 0;
  Vec2 center;   // середина значка (сюда кладут призрак войска)
};

inline std::vector<MapSymbol> mapSymbols(Harness& h) {
  const World& w = h->world();
  std::vector<MapSymbol> list;
  if (w.ownMapObjects()) w.symbols.each([&](const MapSymbol& s) { list.push_back(s); });
  else if (const map::Basemap* bm = h->basemap()) list = bm->objects().symbols;
  return list;
}

inline std::optional<SiegeSpot> siegeSpot(Harness& h, Id state, Id attacker, Id skipProvince = 0) {
  const World& w = h->world();
  for (const MapSymbol& s : mapSymbols(h)) {
    if (s.kind != SymbolKind::Castle && s.kind != SymbolKind::Tower) continue;
    const Id pid = app::mil::provinceUnder(w, s.p);
    const Province* p = w.province(pid);
    if (!p || p->sea || p->owner != state || pid == skipProvince) continue;
    if (p->occupied && p->occupier == attacker) continue;
    const Vec2 c = map::art::symbolBox(s).center();
    if (app::mil::provinceUnder(w, c) != pid || !rules::validPosition(w, ArmyKind::Army, c)) continue;
    bool crowded = false;
    w.armies.each([&](const Army& a) { crowded = crowded || dist(a.pos, c) < 260; });
    if (crowded) continue;
    return SiegeSpot{s.id, pid, c};
  }
  return std::nullopt;
}

// Новое войско фракции рядом с near: отряды первой строки (units) и, если задан, герой-полководец.
inline Id spawnArmy(Harness& h, Id faction, Vec2 near, i64 units, Id hero = 0) {
  const World& w = h->world();
  auto spot = rules::findFreeSpot(w, ArmyKind::Army, near);
  if (!spot) return 0;
  Id id = 0;
  const Id row = w.faction(faction)->army[0].id;
  const bool ok = h->act("Войско для теста", [&](Tx& tx) {
    id = rules::createArmy(tx, ArmyKind::Army, faction, *spot);
    if (units > 0) {
      rules::setRowTotal(tx, faction, row, tx.w().faction(faction)->armyRow(row)->total + units);   // запас резерва
      rules::setUnits(tx, id, faction, row, units);
    }
    if (hero) {
      rules::setHero(tx, id, hero, true);
      rules::setCommander(tx, id, hero);
    }
  });
  return ok ? id : 0;
}

// Свободный герой фракции (не в войске, доступный для назначений).
inline Id freeHero(Harness& h, Id faction) {
  const World& w = h->world();
  Id found = 0;
  w.characters.each([&](const Character& c) {
    if (!found && c.faction == faction && c.hero && rules::heroAvailable(w, c.id) && !app::mil::heroLocation(w, c.id)) found = c.id;
  });
  return found;
}

// Перетаскивание по шагам (snapshot — снимок, пока кнопка ещё зажата).
inline void dragArmy(Harness& h, gfx::Pt from, gfx::Pt to, const std::string& snapshot = {}) {
  hl::mouseMove(from.x, from.y);
  h.step();
  hl::mouseDown(from.x, from.y, platform::MouseLeft);
  h.step();
  for (int i = 1; i <= 10; i++) {
    hl::mouseMove(from.x + (to.x - from.x) * float(i) / 10, from.y + (to.y - from.y) * float(i) / 10);
    h.step();
  }
  if (!snapshot.empty()) {
    h.dropToasts();
    h.settle();
    CHECK(h.shot(snapshot));
  }
  hl::mouseUp(to.x, to.y, platform::MouseLeft);
  h.step();
  hl::advance(0.6);
}

// Ввести число в поле, отмеченное markUi (щелчок, замена текста, Enter).
inline bool enterNumber(Harness& h, const std::string& mark, i64 n) {
  if (!h.clickUi(mark)) return false;
  h.retype(std::to_string(n));
  h.key(Key::Enter);
  h.settle();
  return true;
}

inline i64 unitsOf(const World& w, Id army) {
  const Army* a = w.army(army);
  return a ? app::mil::unitCount(*a) : 0;
}

}  // namespace rg::apptest
