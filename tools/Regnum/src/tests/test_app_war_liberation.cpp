// Сценарии «Освободить провинцию» (аудит 2026-10-23, 5.1): войско владельца перетаскивают на замок или башню своей
// провинции, оккупированной противником (у провинции без таких знаков — на её подпись): с оккупационным гарнизоном —
// окно боя с ним (потери, победитель), победа снимает оккупацию; без гарнизона и войск оккупанта — оккупация снимается
// сразу; войско оккупанта в провинции — отказ «Сначала разбейте войско …», войско возвращается.
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

struct Setup {
  Id hel = 0, vk = 0, province = 0, army = 0;
  Vec2 spot;
};

// Цель освобождения в провинции pid: середина замка или башни (в провинции, место свободно), у провинции без таких
// знаков — её подпись.
std::optional<Vec2> liberationSpot(Harness& h, Id pid) {
  const World& w = h->world();
  bool fort = false;
  for (const MapSymbol& s : mapSymbols(h)) {
    if (s.kind != SymbolKind::Castle && s.kind != SymbolKind::Tower) continue;
    if (app::mil::provinceUnder(w, s.p) != pid) continue;
    fort = true;
    const Vec2 c = map::art::symbolBox(s).center();
    if (app::mil::provinceUnder(w, c) == pid && rules::validPosition(w, ArmyKind::Army, c)) return c;
  }
  if (fort) return std::nullopt;
  auto fs = geo::faces(w);
  const geo::ProvinceShape* sh = fs ? fs->shape(pid) : nullptr;
  if (sh && rules::validPosition(w, ArmyKind::Army, sh->label)) return sh->label;
  return std::nullopt;
}

// Провинция Хельдвига, оккупированная Валь-Кетрой (демонстрационный мир: они в войне); войска Валь-Кетры из неё
// уходят, оккупационный гарнизон — garrison; войско Хельдвига (2000) — рядом.
std::optional<Setup> prepare(Harness& h, i64 garrison) {
  Setup s;
  s.hel = factionByName(h->world(), "Хельдвиг");
  s.vk = factionByName(h->world(), "Валь-Кетра");
  CHECK(h->world().relation(s.hel, s.vk).s == RelStatus::War);
  std::optional<Vec2> spot;
  h->world().provinces.each([&](const Province& p) {
    if (s.province || p.sea || p.owner != s.hel || !p.occupied || p.occupier != s.vk) return;
    if (auto at = liberationSpot(h, p.id)) {
      s.province = p.id;
      spot = at;
    }
  });
  CHECK_MSG(s.province != 0, "нет провинции Хельдвига, оккупированной Валь-Кетрой, с местом у стен");
  if (!s.province) return std::nullopt;
  s.spot = *spot;
  const Id vrow = h->world().faction(s.vk)->army[0].id;
  CHECK(h->act("Оккупация", [&](Tx& tx) {
    for (Id aid : tx.w().armies.ids()) {
      const Army* a = tx.w().army(aid);
      if (a && !a->isFleet() && a->leader() == s.vk && app::mil::provinceUnder(tx.w(), a->pos) == s.province) rules::disband(tx, aid);
    }
    Province& p = tx.province(s.province);
    p.occGarrison.clear();
    if (garrison > 0) {
      rules::setRowTotal(tx, s.vk, vrow, tx.w().faction(s.vk)->armyRow(vrow)->total + garrison);
      tx.province(s.province).occGarrison.push_back(GarrisonEntry{vrow, garrison});
    }
  }));
  s.army = spawnArmy(h, s.hel, s.spot + Vec2(230, 40), 2000);
  CHECK(s.army != 0);
  if (!s.army) return std::nullopt;
  h->select(app::SelType::Army, s.army);
  h.settle();
  showAt(h, (s.spot + h->world().army(s.army)->pos) * 0.5, 0.6);
  h.dropToasts();
  return s;
}

}  // namespace

// Оккупационный гарнизон 300: перетаскивание на свою провинцию — «Освободить: …», окно боя, победа снимает оккупацию.
TEST(app_war_liberation_drag_battle) {
  Harness h("war_liberation_battle", 1440, 1000);
  h.demo();
  auto s = prepare(h, 300);
  if (!s) return;
  const Vec2 from = h->world().army(s->army)->pos;
  dragTo(h, screenOf(h, from), screenOf(h, s->spot), "war_liberation_ghost");
  h.settle();
  CHECK(h->hasDialog("liberation"));
  CHECK_EQ(app::mil::provinceUnder(h->world(), h->world().army(s->army)->pos), s->province);
  const Id vrow = h->world().faction(s->vk)->army[0].id;
  const i64 vkTotal = h->world().faction(s->vk)->armyRow(vrow)->total;
  CHECK(enterNumber(h, "siege.loss.garrison." + std::to_string(vrow), 300));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_liberation_dialog"));
  CHECK(h.clickUi("siege.apply"));
  h.settle();
  CHECK(!h->hasDialog("liberation"));
  CHECK(!h->hasDialog("capture"));
  const Province* p = h->world().province(s->province);
  CHECK(!p->occupied);
  CHECK(p->occGarrison.empty());
  CHECK_EQ(h->world().faction(s->vk)->armyRow(vrow)->total, vkTotal - 300);
  // Отмена возвращает оккупацию.
  h.key(Key::Z, ctrl());
  CHECK(h->world().province(s->province)->occupied);
}

// Без гарнизона и войск оккупанта — оккупация снимается сразу, без окна боя.
TEST(app_war_liberation_without_garrison) {
  Harness h("war_liberation_free", 1440, 1000);
  h.demo();
  auto s = prepare(h, 0);
  if (!s) return;
  const Vec2 from = h->world().army(s->army)->pos;
  dragTo(h, screenOf(h, from), screenOf(h, s->spot));
  CHECK(hasToast(h, "Провинция освобождена"));   // сразу: уведомление живёт 4 с
  h.settle();
  CHECK(!h->hasDialog("liberation"));
  CHECK(!h->world().province(s->province)->occupied);
}

// Войско оккупанта в провинции: освобождение отказано с причиной, войско возвращается на исходную позицию.
TEST(app_war_liberation_blocked_by_occupier_army) {
  Harness h("war_liberation_blocked", 1440, 1000);
  h.demo();
  auto s = prepare(h, 0);
  if (!s) return;
  // Войско Валь-Кетры встаёт в провинции рядом со стенами.
  std::optional<Vec2> near;
  for (Vec2 d : {Vec2(-90, 30), Vec2(90, -30), Vec2(0, 90), Vec2(0, -90), Vec2(-90, -60), Vec2(90, 60)})
    if (auto spot = rules::findFreeSpot(h->world(), ArmyKind::Army, s->spot + d); !near && spot && app::mil::provinceUnder(h->world(), *spot) == s->province)
      near = s->spot + d;
  CHECK_MSG(near.has_value(), "нет места для войска оккупанта в провинции");
  if (!near) return;
  const Id guard = spawnArmy(h, s->vk, *near, 200);
  CHECK(guard != 0);
  CHECK_EQ(app::mil::provinceUnder(h->world(), h->world().army(guard)->pos), s->province);
  h.dropToasts();
  const Vec2 from = h->world().army(s->army)->pos;
  dragTo(h, screenOf(h, from), screenOf(h, s->spot), "war_liberation_blocked_ghost");
  CHECK(hasToast(h, "Сначала разбейте войско"));   // сразу: уведомление живёт 4 с
  h.settle();
  CHECK(!h->hasDialog("liberation"));
  CHECK(h->world().province(s->province)->occupied);
  CHECK(dist(h->world().army(s->army)->pos, from) < 1e-6);
}
