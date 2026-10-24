// Перемещение войск и флота игроком (ТЗ «Доработки №4», п.5): только свои объекты; внутри своей провинции — свободно,
// в соседнюю — за очко действий (фигурка приглушена, отметка в панели), второе перемещение и переход через провинцию —
// отказ с причиной, следующий ход — снова можно; посадка на флот и высадка тратят очко войска; администратор двигает
// без очков действий.
#include "tests/test_app_role_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;

namespace {

// Провинция P государства state с точкой для войска army, её сухопутная соседка Q и соседка Q — R, не граничащая с P
// (все — со свободными точками).
struct Route3 {
  Id p = 0, q = 0, r = 0;
  Vec2 pp, qp, q2, rp;
};
std::optional<Route3> findRoute(const World& w, Id state, Id army) {
  std::vector<Id> own;
  w.provinces.each([&](const Province& p) {
    if (p.owner == state && !p.sea) own.push_back(p.id);
  });
  for (Id P : own) {
    auto pp = freePointIn(w, P, army);
    if (!pp) continue;
    for (Id Q : neighborsOf(w, P)) {
      auto qp = freePointIn(w, Q, army);
      if (!qp) continue;
      auto q2 = freePointIn(w, Q, army, *qp);
      if (!q2) continue;
      for (Id R : neighborsOf(w, Q)) {
        if (R == P || rules::provincesAdjacent(w, P, R)) continue;
        auto rp = freePointIn(w, R, army);
        if (!rp) continue;
        return Route3{P, Q, R, *pp, *qp, *q2, *rp};
      }
    }
  }
  return std::nullopt;
}

bool toastWith(Harness& h, std::string_view part) { return hasToast(h, part); }

}  // namespace

// Соседняя провинция — да (очко истрачено), второе перемещение — нет, через провинцию — нет, внутри провинции —
// свободно, чужое войско не тащится, следующий ход — снова да; администратор — без очков.
TEST(app_role_player_moves) {
  Harness h("role_moves");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id x = armyOf(h->world(), hel, ArmyKind::Army);
  CHECK(hel && x);
  auto rt = findRoute(h->world(), hel, x);
  CHECK(rt.has_value());
  if (!rt) return;
  // Войско — в начало пути (администратор ставит без правил).
  CHECK(h->act("Войско в начало пути", [&](Tx& tx) { rules::moveArmy(tx, x, rt->pp); }));
  role::becomePlayer(h.a(), hel);
  h.settle();
  h.dropToasts();
  const int turn = h->world().turn();
  CHECK(rules::hasAction(h->world(), x));

  // Через провинцию — нельзя: возврат и причина.
  CHECK(dragArmyTo(h, x, rt->rp, "role_moves_denied"));
  CHECK(dist(h->world().army(x)->pos, rt->pp) < 1);
  CHECK(toastWith(h, "соседнюю провинцию"));
  CHECK(rules::hasAction(h->world(), x));

  // В соседнюю — можно, очко действий истрачено.
  CHECK(dragArmyTo(h, x, rt->qp, "role_moves_reach"));
  CHECK_MSG(dist(h->world().army(x)->pos, rt->qp) < 4, "войско не перешло в соседнюю провинцию");
  CHECK_EQ(h->world().army(x)->acted, turn);
  CHECK(!rules::hasAction(h->world(), x));
  CHECK(role::spentArmies(h.a()) == std::vector<Id>{x});
  CHECK(h->uiRect("army.action") != nullptr);   // «Ход сделан» в панели войска
  h.dropToasts();
  shotClean(h, "role_moves_spent");
  {
    const gfx::Pt fp = screenOf(h, h->world().army(x)->pos);
    CHECK(cropShot("role_moves_spent_figure", RectF{fp.x - 60, fp.y - 50, 120, 90}, 4));
  }

  // Второе перемещение в соседнюю — нельзя.
  h.dropToasts();
  CHECK(dragArmyTo(h, x, rt->pp));
  CHECK(dist(h->world().army(x)->pos, rt->qp) < 4);
  CHECK(toastWith(h, "уже ходило"));

  // Внутри своей провинции — свободно (и без очка).
  CHECK(dragArmyTo(h, x, rt->q2));
  CHECK_MSG(dist(h->world().army(x)->pos, rt->q2) < 4, "перестановка внутри провинции не выполнена");

  // Чужое войско — только просмотр.
  Id foreign = 0;
  h->world().armies.each([&](const Army& a) {
    if (!foreign && !a.isFleet() && !a.carrier && a.leader() != hel) foreign = a.id;
  });
  CHECK(foreign != 0);
  if (foreign) {
    const Vec2 fpos = h->world().army(foreign)->pos;
    auto near = rules::findFreeSpot(h->world(), ArmyKind::Army, fpos + Vec2{140, 0}, foreign);
    if (near && rules::encounter(h->world(), foreign, *near).type == rules::EncounterType::None) {
      dragArmyTo(h, foreign, *near);
      CHECK(h->world().army(foreign)->pos == fpos);
    }
  }

  // Следующий ход — снова можно.
  CHECK(adminEndTurn(h));   // ход завершает администратор
  h->closeDialogs();
  h.settle();
  CHECK(rules::hasAction(h->world(), x));
  CHECK(role::spentArmies(h.a()).empty());
  CHECK(dragArmyTo(h, x, rt->rp));
  CHECK_MSG(dist(h->world().army(x)->pos, rt->rp) < 4, "в следующем ходу войско не перешло в соседнюю провинцию");
  CHECK(!rules::hasAction(h->world(), x));

  // Администратор: без очков действий и без ограничения дальности.
  role::becomeAdmin(h.a());
  h.settle();
  CHECK(role::spentArmies(h.a()).empty());
  CHECK(dragArmyTo(h, x, rt->pp));
  CHECK_MSG(dist(h->world().army(x)->pos, rt->pp) < 4, "администратор не переместил войско");
}

// Посадка на флот тратит очко войска (флот своё сохраняет); высадка — только при очке войска на борту и тратит его;
// войско без очка не садится на флот.
TEST(app_role_player_embark_landing) {
  Harness h("role_embark");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id x = armyOf(h->world(), hel, ArmyKind::Army);
  const Id fl = armyOf(h->world(), hel, ArmyKind::Fleet);
  CHECK(hel && x && fl);
  if (!x || !fl) return;
  // Войско — на берег у флота (администратор).
  const Vec2 fpos = h->world().army(fl)->pos;
  std::optional<Vec2> shore;
  for (double r = 40; r <= rules::kLandingReach && !shore; r += 30)
    for (int k = 0; k < 24 && !shore; k++) {
      const Vec2 p = fpos + Vec2{r * std::cos(k * kPi / 12), r * std::sin(k * kPi / 12)};
      if (!rules::validPosition(h->world(), ArmyKind::Army, p, x)) continue;
      World probe = h->world();
      Tx tx(probe);
      rules::moveArmy(tx, x, p);
      const World moved = std::move(tx).finish();
      if (rules::canEmbark(moved, x, fl)) shore = p;
    }
  CHECK(shore.has_value());
  if (!shore) return;
  CHECK(h->act("Войско на берег", [&](Tx& tx) { rules::moveArmy(tx, x, *shore); }));
  role::becomePlayer(h.a(), hel);
  h.settle();
  h.dropToasts();

  // Посадка — войско на флот, окно встречи «На борт»: тратит очко войска, флот может плыть.
  CHECK(dragArmyTo(h, x, fpos));
  CHECK(h->hasDialog("army.encounter"));
  CHECK(h.clickUi("encounter.ok"));
  h.settle();
  CHECK_EQ(h->world().army(x)->carrier, fl);
  CHECK(!rules::hasAction(h->world(), x));
  CHECK(rules::hasAction(h->world(), fl));
  // После решения встречи следующие игровые действия очко не тратят.
  CHECK(h->play("Налог", [&](Tx& tx) { tx.faction(hel).tax = 9; }));
  CHECK(rules::hasAction(h->world(), fl));

  // Высадка в том же ходу — нельзя.
  app::mil::startLanding(h.a(), fl);
  h.settle();
  CHECK(h->ui.tool != app::ToolId::Landing);
  CHECK(hasToast(h, "уже ходило"));

  // Следующий ход: высадка — щелчком по суше у флота, тратит очко войска.
  CHECK(adminEndTurn(h));   // ход завершает администратор
  h->closeDialogs();
  h.settle();
  h.dropToasts();
  CHECK(rules::hasAction(h->world(), x));
  h->select(app::SelType::Army, fl);
  h.settle();
  showAt(h, fpos, 0.6);
  app::mil::startLanding(h.a(), fl);
  h.settle();
  CHECK(h->ui.tool == app::ToolId::Landing);
  std::optional<Vec2> land;
  for (double r = 40; r <= rules::kLandingReach && !land; r += 25)
    for (int k = 0; k < 24 && !land; k++) {
      const Vec2 p = fpos + Vec2{r * std::cos(k * kPi / 12), r * std::sin(k * kPi / 12)};
      const gfx::Pt s = screenOf(h, p);
      if (freeOnScreen(h, s.x, s.y) && rules::canLand(h->world(), fl, p) && !h->map().armyAt(s.x, s.y)) land = p;
    }
  CHECK(land.has_value());
  if (!land) return;
  const gfx::Pt ls = screenOf(h, *land);
  h.move(ls.x, ls.y);
  h.click(ls.x, ls.y);
  h.settle();
  CHECK_EQ(h->world().army(x)->carrier, Id(0));
  CHECK(dist(h->world().army(x)->pos, *land) < 4);
  CHECK(!rules::hasAction(h->world(), x));
  CHECK(rules::hasAction(h->world(), fl));
  // Войско без очка на флот не садится: возврат и причина.
  h.dropToasts();
  h->select(app::SelType::Army, x);
  h.settle();
  const Vec2 back = h->world().army(x)->pos;
  dragArmyTo(h, x, fpos);
  CHECK(h->world().army(x)->pos == back);
  CHECK_EQ(h->world().army(x)->carrier, Id(0));
}

// «Поставить войско» у игрока: только своё государство (чужое — отказ), инструмент не выбирается клавишей A, войско —
// только в своей провинции; объект получает фракцию игрока.
TEST(app_role_player_place_army) {
  Harness h("role_place");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id alm = factionByName(h->world(), "Альмарин");
  CHECK(hel && alm);
  role::becomePlayer(h.a(), hel);
  h.settle();
  h->clearSelection();
  h.settle();
  // Клавиша A и чужое государство — отказ.
  h.key(Key::A);
  CHECK(h->ui.tool == app::ToolId::Select);
  app::mil::startPlacing(h.a(), ArmyKind::Army, alm);
  CHECK(h->ui.tool == app::ToolId::Select);
  CHECK(hasToast(h, role::kAdminOnly));
  // Своё государство — инструмент с фракцией игрока.
  app::mil::startPlacing(h.a(), ArmyKind::Army, hel);
  h.settle();
  CHECK(h->ui.tool == app::ToolId::NewArmy);
  // Чужая провинция — отказ; своя — войско игрока.
  Id ownProv = 0, foreignProv = 0;
  std::optional<Vec2> ownPt, foreignPt;
  h->world().provinces.each([&](const Province& p) {
    if (p.sea) return;
    auto fs = geo::faces(h->world());
    const geo::ProvinceShape* sh = fs->shape(p.id);
    if (!sh) return;
    auto s = rules::findFreeSpot(h->world(), ArmyKind::Army, sh->label);
    if (!s || rules::provinceAtPos(h->world(), *s) != p.id) return;
    if (!ownProv && p.owner == hel) {
      ownProv = p.id;
      ownPt = s;
    }
    if (!foreignProv && p.owner == alm) {
      foreignProv = p.id;
      foreignPt = s;
    }
  });
  CHECK(ownPt && foreignPt);
  if (!ownPt || !foreignPt) return;
  const u32 before = h->world().armies.size();
  showAt(h, *foreignPt, 0.5);
  h.dropToasts();
  gfx::Pt fs = screenOf(h, *foreignPt);
  CHECK(freeOnScreen(h, fs.x, fs.y));
  h.click(fs.x, fs.y);
  h.settle();
  CHECK_EQ(h->world().armies.size(), before);
  CHECK(hasToast(h, "провинции своего государства"));
  showAt(h, *ownPt, 0.5);
  h.dropToasts();
  gfx::Pt os = screenOf(h, *ownPt);
  CHECK(freeOnScreen(h, os.x, os.y));
  h.click(os.x, os.y);
  h.settle();
  CHECK_EQ(h->world().armies.size(), before + 1u);
  CHECK(h->ui.sel.type == app::SelType::Army);
  if (const Army* a = h->world().army(h->ui.sel.id)) CHECK_EQ(a->leader(), hel);
}
