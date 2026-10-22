// Готовность игроков завершить ход (ТЗ «Доработки №1», п.1; ТЗ «Доработки №2», п.6): у игрока «Завершить ход» (кнопка
// нижней строки, Ctrl+Enter, палитра) отмечает и снимает готовность его государства, ход не наступает; отметка — в
// файле мира; администратор видит готовность в подсказке кнопки и во вкладке «Готовность» окна завершения хода (основные
// государства без отметки — приглушённо), завершает ход для всех — отметки сбрасываются.
#include "app/shell/role.h"
#include "app/turn/turn_ui.h"
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;
namespace tu = rg::app::turnui;

namespace {

// Правая часть нижней строки (ход и «Завершить ход») для снимка.
RectF endTurnArea(Harness& h) {
  const RectF* bar = h->uiRect("bottombar");
  if (!bar) return {};
  return RectF{bar->right() - 280, bar->y, 280, bar->h};
}

}  // namespace

TEST(app_turn_ready_player_marks_admin_ends) {
  Harness h("turn_ready");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг"), sol = factionByName(h->world(), "Солмар");
  CHECK(hel && sol);
  CHECK(h->act("Основные государства", [&](Tx& tx) {
    tx.faction(hel).mainState = true;
    tx.faction(sol).mainState = true;
  }));
  const std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  const int t0 = h->world().turn();

  // Игрок: «Завершить ход» — отметка готовности, окна подтверждения нет, ход прежний.
  role::becomePlayer(h.a(), hel);
  h.settle();
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(cropShot("turn_ready_player_idle", endTurnArea(h), 2));
  CHECK(h.clickUi("topbar.endturn"));
  CHECK(hasToast(h, "Готов завершить ход"));
  h.settle();
  CHECK(!h->hasDialog("turn.confirm"));
  CHECK(!h->hasDialog());
  CHECK_EQ(h->world().turn(), t0);
  CHECK(rules::turnReady(h->world(), hel));
  CHECK_EQ(tu::readyCount(h->world()), 1);
  h.dropToasts();
  h.settle();
  CHECK(cropShot("turn_ready_player_ready", endTurnArea(h), 2));
  // Отметка «Готов» шире значка: кнопка на месте, подсказка — «отменить готовность».
  const RectF* btn = h->uiRect("topbar.endturn");
  CHECK(btn != nullptr);
  if (btn) {
    CHECK(btn->w > 40);
    h.move(btn->cx(), btn->cy());
    h.frames(40);
    CHECK_MSG(ui::tooltipText().find("отменить готовность") != std::string::npos, ui::tooltipText());
    h.move(btn->cx(), btn->y - 200);
  }

  // Повторно (Ctrl+Enter) — отметка снята; команда палитры — снова готов. Ход не наступает.
  h.dropToasts();
  h.key(Key::Enter, ctrl());
  CHECK(hasToast(h, "Готовность снята"));
  h.settle();
  CHECK(!rules::turnReady(h->world(), hel));
  CHECK(app::runCommand(h.a(), "turn.end"));
  h.settle();
  CHECK(rules::turnReady(h->world(), hel));
  CHECK(!h->hasDialog());
  CHECK_EQ(h->world().turn(), t0);
  // Отметка — изменение мира: отменяется Ctrl+Z и сохраняется в файле мира.
  h->undo();
  h.settle();
  CHECK(!rules::turnReady(h->world(), hel));
  h->redo();
  h.settle();
  CHECK(rules::turnReady(h->world(), hel));
  CHECK(h->save());

  // Администратор открывает мир: отметка игрока на месте, у кнопки — зелёная точка и число готовых в подсказке.
  CHECK(h->loadProject(dir));
  h.settle();
  if (h->player()) role::becomeAdmin(h.a());
  h.settle();
  CHECK(h->admin());
  CHECK(rules::turnReady(h->world(), hel));
  CHECK(!rules::turnReady(h->world(), sol));
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(cropShot("turn_ready_admin_bar", endTurnArea(h), 2));
  if (const RectF* r = h->uiRect("topbar.endturn")) {
    CHECK(r->w <= 31);
    h.move(r->cx(), r->cy());
    h.frames(40);
    CHECK_MSG(ui::tooltipText().find("Готовы завершить ход: 1") != std::string::npos, ui::tooltipText());
    h.move(r->cx(), r->y - 200);
  }

  // Окно завершения хода: вкладка «Готовность» — готовое государство и основное без отметки.
  CHECK(h.clickUi("topbar.endturn"));
  h.settle();
  CHECK(h->hasDialog("turn.confirm"));
  CHECK(clickPart(h, "turn.confirm.tabs", 4, 5));
  h.settle();
  const RectF* rh = h->uiRect("turn.confirm.ready." + std::to_string(hel));
  const RectF* rs = h->uiRect("turn.confirm.ready." + std::to_string(sol));
  CHECK(rh != nullptr);
  CHECK(rs != nullptr);
  if (rh && rs) CHECK(rh->y < rs->y);   // готовые — первыми
  h.dropToasts();
  h.settle();
  CHECK(h.shot("turn_ready_confirm"));
  // Завершить ход — для всех; отметки сброшены.
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK_EQ(h->world().turn(), t0 + 1);
  CHECK(!rules::turnReady(h->world(), hel));
  CHECK_EQ(tu::readyCount(h->world()), 0);
  CHECK(h->world().faction(hel)->readyTurn == t0);   // отметка прошлого хода — уже не действует
}

// Игрок не открывает окно завершения хода ни кнопкой, ни App::endTurn; на прошлом ходу отметка недоступна.
TEST(app_turn_ready_player_never_confirms) {
  Harness h("turn_ready_player");
  h.demo();
  const Id kor = factionByName(h->world(), "Корвен");
  CHECK(kor != 0);
  role::becomePlayer(h.a(), kor);
  h.settle();
  const int t0 = h->world().turn();
  h->endTurn();
  h.settle();
  CHECK(!h->hasDialog("turn.confirm"));
  CHECK(rules::turnReady(h->world(), kor));
  // Чужая отметка игроку недоступна: только своё государство.
  const Id other = factionByName(h->world(), "Хельдвиг");
  CHECK(other != 0);
  CHECK(!rules::turnReady(h->world(), other));
  h->endTurn();
  h.settle();
  CHECK(!rules::turnReady(h->world(), kor));
  CHECK_EQ(h->world().turn(), t0);
}
