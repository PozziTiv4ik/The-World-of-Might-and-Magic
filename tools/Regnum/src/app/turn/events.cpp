// Regnum — события после завершения хода (ТЗ «Механика мятежа», п.1.2 и 4; «Доработки», п.1): мятеж войска или
// гарнизона с верностью −100 % и восстание провинции. Окна идут по очереди: судьба верных героев, битва мятежников с
// войском прежнего государства в провинции, штурм гарнизона или захват (flow::rebelAftermath); затем — войны
// мятежников: сюзерены атакованных мятежниками вассалов (flow::afterWarDeclared).
// Очередь живёт в мире (Meta::turnEvents, её записывает rules::endTurn): разобранное событие удаляется из очереди
// транзакцией без записи в отмену, война мятежников при этом встаёт в конец очереди (событие RebelWar). Если окна
// закрыты все сразу (closeDialogs: возврат к ходу, другой мир) или программа закрыта, неразобранные события остаются в
// мире — раздел «Решения» показывает их с кнопкой «Продолжить».
#include "app/app_internal.h"
#include "app/flows.h"
#include "app/panels/military.h"

namespace rg::app::flow {

namespace {

// Разбор на сеанс мира: идёт ли он и поколение закрытий окон (detail::dialogEpoch) на момент открытия окон события.
struct Run {
  bool active = false;
  u64 epoch = 0;
};
Run& run() { return sessionState<Run>(); }

// Окна закрыты все сразу после открытия окон события: цепочка прервана, событие остаётся в очереди мира.
bool stale(u64 epoch) { return !run().active || epoch != detail::dialogEpoch(); }

bool sameWar(const TurnEvent& x, const TurnEvent& y) {
  return x.kind == TurnEvent::RebelWar && y.kind == TurnEvent::RebelWar && x.rebelState == y.rebelState && x.origin == y.origin;
}

// Изменить очередь событий мира без записи в отмену (служебная правка: разбор событий не отменяется Ctrl+Z).
template <class F>
void editQueue(App& a, F&& fn) {
  try {
    a.store.transact("События хода", [&](Tx& tx) { fn(tx.meta().turnEvents); }, TxOptions{.history = false});
  } catch (const std::exception& e) {
    a.error(e);
  }
}

// Событие разобрано: удаляется из очереди (первое равное); мятежное государство события воюет с прежним — война
// мятежников встаёт в конец очереди (одна на пару).
void resolve(App& a, const TurnEvent& e) {
  editQueue(a, [&](std::vector<TurnEvent>& q) {
    if (auto it = std::find(q.begin(), q.end(), e); it != q.end()) q.erase(it);
    if (e.kind == TurnEvent::RebelWar || !e.rebelState || !e.origin) return;
    TurnEvent war;
    war.kind = TurnEvent::RebelWar;
    war.rebelState = e.rebelState;
    war.origin = e.origin;
    if (std::none_of(q.begin(), q.end(), [&](const TurnEvent& x) { return sameWar(x, war); })) q.push_back(war);
  });
}

// Событие частично разобрано (судьба героев решена): запись в очереди заменяется.
void update(App& a, const TurnEvent& from, const TurnEvent& to) {
  editQueue(a, [&](std::vector<TurnEvent>& q) {
    if (auto it = std::find(q.begin(), q.end(), from); it != q.end()) *it = to;
  });
}

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

void step(App& a);

// Продолжение после окон события e: событие разобрано, дальше — следующее (если цепочку не прервали).
std::function<void(App&)> finishOf(const TurnEvent& e, u64 epoch) {
  return [e, epoch](App& x) {
    if (stale(epoch)) return;
    resolve(x, e);
    step(x);
  };
}

// Мятежники события e нападают на войско прежнего государства в провинции (ТЗ «Мятеж», п.1.1), без него — штурм
// оставшегося гарнизона или захват. false — мятежников уже нет.
bool rebelsAttack(App& a, const TurnEvent& e, const std::function<void(App&)>& done) {
  const World& w = a.store.world();
  const Army* r = w.army(e.army);
  if (!r) return false;
  const Id pid = e.province ? e.province : mil::provinceUnder(w, r->pos);
  if (Id target = e.origin ? originArmyIn(w, e.origin, pid, r->pos) : 0) {
    mil::openBattle(a, e.army, target, r->pos, {}, done);
    return true;
  }
  rebelAftermath(a, e.army, done);
  return true;
}

// Окна события e; false — окон нет (участников события больше нет), событие разобрано сразу.
bool open(App& a, const TurnEvent& e, u64 epoch) {
  const World& w = a.store.world();
  switch (e.kind) {
    case TurnEvent::Mutiny:
      // Войско или гарнизон (верность −100 %) восстали целиком: верные герои — «Судьба героя» (пленившее — мятежники);
      // решённая судьба убирает героев из события (после перерыва окно не повторяется), затем — мятежники в провинции.
      if (!e.heroes.empty()) {
        openHeroFate(a, e.heroes, e.rebelState, e.province, [e, epoch](App& x) {
          if (stale(epoch)) return;
          TurnEvent next = e;
          next.heroes.clear();
          update(x, e, next);
          if (!rebelsAttack(x, next, finishOf(next, epoch))) {
            resolve(x, next);
            step(x);
          }
        });
        return true;
      }
      return rebelsAttack(a, e, finishOf(e, epoch));
    case TurnEvent::Uprising:
      // Восстание провинции: войско государства в провинции — битва; иначе гарнизон — штурм; иначе захват.
      return rebelsAttack(a, e, finishOf(e, epoch));
    case TurnEvent::RebelWar:
      // Война мятежников с прежним государством: сюзерены и вассалы отвечают на неё (вассалитет).
      if (!w.faction(e.rebelState) || !w.faction(e.origin)) return false;
      afterWarDeclared(a, e.rebelState, e.origin, finishOf(e, epoch));
      return true;
  }
  return false;
}

void step(App& a) {
  Run& r = run();
  for (int guard = 0; guard < 10000; guard++) {
    const std::vector<TurnEvent>& q = a.store.world().meta->turnEvents;
    if (q.empty() || a.readOnly()) break;
    const TurnEvent e = q.front();
    r.active = true;
    r.epoch = detail::dialogEpoch();
    if (open(a, e, r.epoch)) return;   // окна события открыты: продолжение — finishOf
    resolve(a, e);
    const std::vector<TurnEvent>& after = a.store.world().meta->turnEvents;
    if (!after.empty() && after.front() == e) break;   // событие не удалилось — не крутиться
  }
  r.active = false;
}

}  // namespace

void processTurnEvents(App& a) {
  if (a.store.world().meta->turnEvents.empty() || a.readOnly() || turnEventsRunning(a)) return;
  Run& r = run();
  r.active = true;
  r.epoch = detail::dialogEpoch();
  // Первое окно — после кадра (уведомление о ходе и окно итога успевают появиться).
  const u64 epoch = r.epoch;
  detail::later(a, [epoch](App& x) {
    if (!stale(epoch)) step(x);
  });
}

bool turnEventsRunning(App&) {
  const Run& r = run();
  return r.active && r.epoch == detail::dialogEpoch();
}

}  // namespace rg::app::flow
