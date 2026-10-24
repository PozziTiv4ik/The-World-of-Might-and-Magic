// Помощники сценариев роли за редактором (test_app_role*.cpp): соседние провинции, свободная точка в провинции для
// войска, перетаскивание объекта по карте с показом пути.
#pragma once
#include "app/shell/role.h"
#include "tests/test_app_military_util.h"
#include "tests/test_app_trees_util.h"

namespace rg::apptest {

// Завершить ход за администратора, не меняя роли сеанса (у игрока App::endTurnNow отказывает — аудит 2026-10-23, 1.4).
inline bool adminEndTurn(Harness& h) {
  const app::Role was = h->ui.role;
  h->ui.role = app::Role::Admin;
  const bool ok = h->endTurnNow();
  h->ui.role = was;
  return ok;
}

// Сухопутные (land) или морские соседи провинции по общим границам, по возрастанию ID.
inline std::vector<Id> neighborsOf(const World& w, Id prov, bool sea = false) {
  std::vector<Id> out;
  w.edges.each([&](const Edge& e) {
    const Id o = e.pl == prov ? e.pr : e.pr == prov ? e.pl : 0;
    if (!o || o == prov || std::find(out.begin(), out.end(), o) != out.end()) return;
    const Province* p = w.province(o);
    if (p && p->sea == sea) out.push_back(o);
  });
  std::sort(out.begin(), out.end());
  return out;
}

// Точка в провинции, куда объект army встанет без встречи (ближе к точке подписи); skip — не ближе 80 к ней.
inline std::optional<Vec2> freePointIn(const World& w, Id province, Id army, std::optional<Vec2> skip = std::nullopt) {
  auto fs = geo::faces(w);
  const geo::ProvinceShape* sh = fs ? fs->shape(province) : nullptr;
  if (!sh) return std::nullopt;
  const Army* ar = w.army(army);
  if (!ar) return std::nullopt;
  for (double r : {0.0, 60.0, 120.0, 180.0})
    for (int k = 0; k < (r == 0 ? 1 : 12); k++) {
      const Vec2 near = sh->label + Vec2{r * std::cos(k * kPi / 6), r * std::sin(k * kPi / 6)};
      auto s = rules::findFreeSpot(w, ar->kind, near, army);
      if (!s || rules::provinceAtPos(w, *s) != province) continue;
      if (skip && dist(*s, *skip) < 80) continue;
      if (rules::encounter(w, army, *s).type != rules::EncounterType::None) continue;
      return s;
    }
  return std::nullopt;
}

// Перетащить объект army в точку карты to (объект выделен, путь целиком на экране); true — обе точки видны.
inline bool dragArmyTo(Harness& h, Id army, Vec2 to, const std::string& snapshot = {}) {
  const Army* ar = h->world().army(army);
  if (!ar) return false;
  const Vec2 from = ar->pos;
  if (h->ui.sel != app::Selection{app::SelType::Army, army}) {
    h->select(app::SelType::Army, army);
    h.settle();
  }
  const RectF area = h->mapArea();
  const double d = std::max(1.0, dist(from, to));
  const double zoom = std::clamp(0.5 * std::min(area.w, area.h) / d, 0.12, 0.7);
  showAt(h, (from + to) * 0.5, zoom);
  h.dropToasts();
  const gfx::Pt a = screenOf(h, from), b = screenOf(h, to);
  if (!freeOnScreen(h, a.x, a.y) || !freeOnScreen(h, b.x, b.y)) return false;
  dragTo(h, a, b, snapshot);
  h.settle();
  return true;
}

}  // namespace rg::apptest
