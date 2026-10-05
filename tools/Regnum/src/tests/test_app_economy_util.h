// Regnum — помощники сценариев провинций и экономики (test_app_economy_*.cpp): колонизация, оккупационный гарнизон,
// рабы, пустошь нежити и осквернение, ресурсы построек, экономика государства.
#pragma once
#include "tests/test_app_faction_util.h"

namespace rg::econtest {

using namespace rg::apptest;
using namespace rg::factest;

inline const Province* prov(Harness& h, Id pid) { return h->world().province(pid); }

// Выбрать провинцию, открыть вкладку и вернуть прокрутку инспектора к началу.
inline void openProvince(Harness& h, Id pid, const char* tab) {
  h->ui.tabOf[app::SelType::Province] = tab;
  h->select(app::SelType::Province, pid, true);
  h.settle();
  h.dropToasts();
  if (const RectF* r = h->uiRect("inspector")) {
    h.wheel(r->cx(), r->bottom() - 120, 40);
    h.frames(24);
  }
}

inline bool hasToast(Harness& h, std::string_view part) {
  for (auto& t : h->toasts())
    if (t.text.find(part) != std::string::npos) return true;
  return false;
}

inline i64 popOf(const Province& p) {
  i64 n = 0;
  for (const RacePop& r : p.races) n += std::max<i64>(0, r.pop);
  return n;
}

// Сухопутная провинция без владельца (с населением, если есть такая).
inline Id unownedLand(const World& w) {
  Id best = 0;
  w.provinces.each([&](const Province& p) {
    if (p.sea || p.owner) return;
    if (!best || (popOf(p) > 0 && popOf(*w.province(best)) == 0)) best = p.id;
  });
  return best;
}

// Провинция государства с наибольшим населением (не столица, если можно).
inline Id populousProvince(const World& w, Id state) {
  Id best = 0;
  i64 bestPop = -1;
  const Faction* f = w.faction(state);
  w.provinces.each([&](const Province& p) {
    if (p.sea || p.owner != state) return;
    i64 n = popOf(p) - (f && f->capital == p.id ? (i64(1) << 40) : 0);
    if (n > bestPop) {
      bestPop = n;
      best = p.id;
    }
  });
  return best;
}

// Государство с наибольшим числом провинций.
inline Id biggestState(const World& w) {
  std::map<Id, int> n;
  w.provinces.each([&](const Province& p) {
    if (!p.sea && p.owner) n[p.owner]++;
  });
  Id best = 0;
  int bn = 0;
  for (auto& [f, k] : n)
    if (const Faction* x = w.faction(f); x && x->isState() && k > bn) {
      bn = k;
      best = f;
    }
  return best;
}

inline void shotClean(Harness& h, const std::string& name) {
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot(name));
}

// Ввод числа в поле с прокруткой к нему; Enter фиксирует.
inline bool enterNumber(Harness& h, const std::string& field, const std::string& value) { return typeNumber(h, field, value); }

}  // namespace rg::econtest
