// Итоги хода из мира (аудит 2026-10-23, 2.3): отчёт строится из Meta::lastTurn и переживает перезапуск; игрок, впервые
// открыв мир после смены хода, видит «Итоги хода N» своего государства (отметка виденного хода — turns.json данных
// пользователя, по миру и государству); команда «Отчёт о последнем ходе» доступна игроку.
#include "app/shell/role.h"
#include "app/turn/turn_ui.h"
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;
namespace tu = rg::app::turnui;

TEST(app_turn_report_from_world_after_reload) {
  Harness h("turn_report_world");
  h.demo();
  const std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  const int t0 = h->world().turn();
  CHECK(!app::findCommand("turn.report")->enabled(h.a()));   // ход ещё не завершался
  CHECK(h->endTurnNow());
  h.settle();
  h->closeDialogs();
  CHECK(h->save());
  CHECK(h->loadProject(dir));
  h.settle();
  if (h->player()) role::becomeAdmin(h.a());
  h.settle();
  CHECK(!h->lastTurnReport().has_value());   // отчёт сеанса сброшен — итоги берутся из мира
  CHECK_EQ(h->world().meta->lastTurn.turnFrom, t0);
  CHECK_EQ(h->world().meta->lastTurn.turnTo, t0 + 1);
  CHECK(app::findCommand("turn.report")->enabled(h.a()));
  CHECK(app::runCommand(h.a(), "turn.report"));
  h.settle();
  CHECK(h->hasDialog("turn.report"));
  CHECK(h->uiRect("turn.report.pages") != nullptr);
  CHECK(h->uiRect("turn.report.body") != nullptr);
}

TEST(app_turn_report_player_arrival) {
  Harness h("turn_report_player");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг"), sol = factionByName(h->world(), "Солмар");
  CHECK(hel && sol);
  const std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  const int t0 = h->world().turn();
  CHECK(h->endTurnNow());
  h.settle();
  h->closeDialogs();
  CHECK(h->save());
  CHECK_EQ(tu::seenTurn(h.a(), h->projectPath(), hel), 0);
  // Игрок впервые видит мир после хода: «Итоги хода t0» — только его государство, без страниц.
  role::becomePlayer(h.a(), hel);
  h.settle();
  CHECK(h->hasDialog("turn.report"));
  CHECK(h->uiRect("turn.report.state") != nullptr);
  CHECK(h->uiRect("turn.report.pages") == nullptr);
  CHECK(h->uiRect("turn.report.card." + std::to_string(hel)) != nullptr);
  CHECK(h->uiRect("turn.report.card." + std::to_string(sol)) == nullptr);
  CHECK_EQ(tu::seenTurn(h.a(), h->projectPath(), hel), t0 + 1);
  // Записи итогов игрока — только о его государстве.
  const World& w = h->world();
  for (const LogEntry* e : tu::digest(w, w.meta->lastTurn, tu::kPageAll, hel).all) CHECK(tu::entryConcerns(w, *e, hel));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("turn_report_player"));
  h.key(Key::Escape);
  h.settle();
  CHECK(!h->hasDialog());
  // Тот же мир перечитан — итоги уже видены.
  CHECK(h->loadProject(dir));
  h.settle();
  CHECK(h->player());
  CHECK(!h->hasDialog("turn.report"));
  // Команда «Отчёт о последнем ходе» доступна игроку — итоги его государства.
  CHECK(app::findCommand("turn.report")->enabled(h.a()));
  CHECK(app::runCommand(h.a(), "turn.report"));
  h.settle();
  CHECK(h->hasDialog("turn.report"));
  CHECK(h->uiRect("turn.report.card." + std::to_string(hel)) != nullptr);
  CHECK(h->uiRect("turn.report.card." + std::to_string(sol)) == nullptr);
  h.key(Key::Escape);
  h.settle();
  // Другое государство за этим компьютером — свои итоги (отметка — по государству).
  role::becomePlayer(h.a(), sol);
  h.settle();
  CHECK(h->hasDialog("turn.report"));
  CHECK(h->uiRect("turn.report.card." + std::to_string(sol)) != nullptr);
  CHECK(h->uiRect("turn.report.card." + std::to_string(hel)) == nullptr);
}

// Мир без файла: отметки виденного хода нет — итоги сами не открываются (их открывает команда).
TEST(app_turn_report_player_unsaved_world_no_popup) {
  Harness h("turn_report_unsaved");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  CHECK(h->endTurnNow());
  h.settle();
  h->closeDialogs();
  role::becomePlayer(h.a(), hel);
  h.settle();
  CHECK(!h->hasDialog("turn.report"));
  CHECK(app::runCommand(h.a(), "turn.report"));
  h.settle();
  CHECK(h->hasDialog("turn.report"));
}
