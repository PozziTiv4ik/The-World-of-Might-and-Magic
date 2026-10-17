// Regnum — события после завершения хода (ТЗ «Механика мятежа», п.1.2 и 4; «Доработки», п.1): мятеж войска или
// гарнизона с верностью −100 % и восстание провинции. Окна идут по очереди: судьба верных героев, битва мятежников с
// войском прежнего государства в провинции, штурм гарнизона или захват (flow::rebelAftermath); в конце — сюзерены
// атакованных мятежниками вассалов (flow::afterWarDeclared).
#include "app/app_internal.h"
#include "app/flows.h"
#include "app/panels/military.h"

namespace rg::app::flow {

namespace {

struct Queue {
  std::vector<rules::TurnEvent> events;
  size_t next = 0;
  std::vector<std::pair<Id, Id>> wars;   // мятежное государство → прежнее (по одному разу)
  size_t nextWar = 0;
};

// Войско прежнего государства в провинции (ближайшее к мятежникам).
Id originArmyIn(const World& w, Id origin, Id province, Vec2 near) {
  Id best = 0;
  double bd = 1e300;
  w.armies.each([&](const Army& a) {
    if (a.isFleet() || a.allied() || a.leader() != origin) return;
    if (mil::provinceUnder(w, a.pos) != province) return;
    const double d = dist2(a.pos, near);
    if (d < bd) {
      bd = d;
      best = a.id;
    }
  });
  return best;
}

void wars(App& a, std::shared_ptr<Queue> q) {
  while (q->nextWar < q->wars.size()) {
    const auto [rebels, origin] = q->wars[q->nextWar++];
    if (!a.world().faction(rebels) || !a.world().faction(origin)) continue;
    afterWarDeclared(a, rebels, origin, [q](App& x) { wars(x, q); });
    return;
  }
}

void step(App& a, std::shared_ptr<Queue> q) {
  while (q->next < q->events.size()) {
    const rules::TurnEvent e = q->events[q->next++];
    const World& w = a.world();
    auto cont = [q](App& x) { step(x, q); };
    if (e.rebelState && e.origin &&
        std::find(q->wars.begin(), q->wars.end(), std::pair<Id, Id>{e.rebelState, e.origin}) == q->wars.end())
      q->wars.push_back({e.rebelState, e.origin});
    const Army* ar = w.army(e.army);
    if (e.kind == rules::TurnEvent::Mutiny) {
      // Войско или гарнизон (верность −100 %) восстали целиком: верные герои — «Судьба героя» (пленившее — мятежники);
      // затем мятежники нападают на войско прежнего государства в провинции (ТЗ «Мятеж», п.1.1), а без него — штурм
      // оставшегося гарнизона или захват.
      const Id army = e.army, origin = e.origin, province = e.province;
      auto after = [army, origin, province, cont](App& x) {
        const Army* r = x.world().army(army);
        if (!r) {
          cont(x);
          return;
        }
        const Id pid = province ? province : mil::provinceUnder(x.world(), r->pos);
        if (Id target = origin ? originArmyIn(x.world(), origin, pid, r->pos) : 0) {
          mil::openBattle(x, army, target, r->pos, {}, cont);
          return;
        }
        rebelAftermath(x, army, cont);
      };
      if (!e.heroes.empty()) {
        openHeroFate(a, e.heroes, e.rebelState, e.province, after);
        return;
      }
      if (ar) {
        after(a);
        return;
      }
      continue;
    }
    // Восстание провинции: войско государства в провинции — битва; иначе гарнизон — штурм; иначе захват.
    if (!ar) continue;
    if (Id target = originArmyIn(w, e.origin, e.province, ar->pos)) {
      mil::openBattle(a, e.army, target, ar->pos, {}, cont);
      return;
    }
    rebelAftermath(a, e.army, cont);
    return;
  }
  wars(a, q);
}

}  // namespace

void processTurnEvents(App& a, const rules::TurnReport& rep) {
  if (rep.events.empty()) return;
  auto q = std::make_shared<Queue>();
  q->events = rep.events;
  detail::later(a, [q](App& x) { step(x, q); });
}

}  // namespace rg::app::flow
