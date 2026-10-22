// Сценарии истории ходов: подтверждение и завершение хода со снимками, лента истории и возврат к снимку, снимки
// уникальны и не заменяют друг друга, «Начать ход заново» — ровно начало хода текущей ветви (после возврата и нового
// прохождения, после Ctrl+Z / Ctrl+Y, после повторного открытия мира), снимки несохранённых ходов живут в памяти и
// отбрасываются вместе с миром, история не переходит к другому миру.
#include <set>

#include "app/turn/history.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;

namespace {

void rename(Harness& h, Id pid, const std::string& name) {
  CHECK(h->act("Имя", [&](Tx& tx) { tx.province(pid).name = name; }));
}

std::string nameOf(Harness& h, Id pid) { return h->store.world().province(pid)->name; }

void endTurn(Harness& h) {
  CHECK(h->endTurnNow());
  h->toasts().clear();
}

// «Начать ход заново» через историю ходов (как пользователь).
void restartViaHistory(Harness& h) {
  int t = h->store.world().turn();
  h->openDialog("turn.history");
  h.dropToasts();
  h.settle();
  CHECK(h.clickUi("history.rollback." + std::to_string(t)));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK_EQ(h->store.world().turn(), t);
}

// Номер живого уведомления с кнопкой label (−1 — нет).
int toastWith(app::App& a, std::string_view label) {
  auto& t = a.toasts();
  for (size_t i = 0; i < t.size(); i++)
    if (t[i].actionLabel == label && t[i].closeAt < 0) return int(i);
  return -1;
}

}  // namespace

TEST(app_turn_history_flow_and_rollback) {
  Harness h("turn2_flow");
  h.demo();
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  int t0 = h->store.world().turn();

  // Подтверждение: пробный ход не меняет мир.
  World before = h->store.world();
  CHECK(h.clickUi("topbar.endturn"));
  h.settle();
  CHECK(h->hasDialog("turn.confirm"));
  CHECK(World::diff(before, h->store.world()) == 0);
  shotClean(h, "turn2_confirm");
  // Вкладки: ресурсы, события, внимание.
  CHECK(clickPart(h, "turn.confirm.tabs", 1, 5));
  shotClean(h, "turn2_confirm_resources");
  CHECK(clickPart(h, "turn.confirm.tabs", 2, 5));
  shotClean(h, "turn2_confirm_events");
  CHECK(clickPart(h, "turn.confirm.tabs", 3, 5));
  shotClean(h, "turn2_confirm_warnings");
  CHECK(h->hasDialog("turn.confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK_EQ(h->store.world().turn(), t0 + 1);
  CHECK(!h->snapshots().empty() && h->snapshots().front().turn == t0 && h->snapshots().front().kind == io::kSnapEnd);

  // Отчёт: записи хроники хода, карточки фракций.
  int ti = toastWith(h.a(), "Отчёт");
  CHECK(ti >= 0);
  CHECK(h.clickUi("toast." + std::to_string(ti) + ".action"));
  h.settle();
  CHECK(h->hasDialog("turn.report"));
  CHECK(h->uiRect("turn.report.body") != nullptr);
  CHECK(!h->lastTurnReport()->logIds.empty());
  shotClean(h, "turn2_report");
  // «Хроника хода» — панель с фильтром по ходу.
  CHECK(h.clickUi("turn.report.chronicle"));
  h.settle();
  CHECK(!h->hasDialog());
  CHECK_EQ(h->ui.drawer, std::string("chronicle"));
  for (Id lid : h->lastTurnReport()->logIds) CHECK(h->uiRect("chronicle.entry." + std::to_string(lid)) != nullptr);
  shotClean(h, "turn2_chronicle_turn");

  // Второй ход.
  CHECK(h->endTurnNow());
  h.settle();
  auto snaps = h->snapshots();
  CHECK_EQ(snaps.size(), size_t(3));   // ходы t0, t0 + 1 и начало текущего
  CHECK_EQ(snaps[1].turn, t0 + 1);
  CHECK(snaps[1].memory);   // до сохранения мира
  CHECK_EQ(h->store.world().turn(), t0 + 2);

  // История: лента и сравнение (снимки читаются в фоне).
  h->openDialog("turn.history");
  h.dropToasts();
  h.settle();
  h.settle();
  CHECK(h->hasDialog("turn.history"));
  CHECK(h->uiRect("history.open." + std::to_string(t0)) != nullptr);
  CHECK(h->uiRect("history.rollback." + std::to_string(t0 + 1)) != nullptr);
  shotClean(h, "turn2_history");
  CHECK(clickPart(h, "history.tabs", 1, 2));
  h.settle();
  shotClean(h, "turn2_history_compare");
  CHECK(clickPart(h, "history.tabs", 0, 2));
  h.settle();

  // Просмотр прошлого хода: только чтение, хроника показывает мир того хода.
  CHECK(h.clickUi("history.open." + std::to_string(t0 + 1)));
  h.settle();
  CHECK(h->readOnly());
  CHECK_EQ(h->world().turn(), t0 + 1);
  CHECK(h->uiRect("banner") != nullptr);
  CHECK(!h->act("Правка", [&](Tx& tx) { tx.meta().name = "x"; }));
  shotClean(h, "turn2_view_past");

  // Возврат к ходу t0: подтверждение, текущее состояние остаётся в истории.
  World cur = h->store.world();
  h->openDialog("turn.history");
  h.settle();
  CHECK(h.clickUi("history.rollback." + std::to_string(t0)));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  shotClean(h, "turn2_rollback_confirm");
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(!h->readOnly());
  CHECK(!h->hasDialog());
  CHECK_EQ(h->store.world().turn(), t0);
  CHECK(!h->dirty());   // конец хода t0 — ровно сохранённый мир (снимок в памяти, а не перечитанный с диска)
  snaps = h->snapshots();
  CHECK_EQ(snaps.size(), size_t(3));
  CHECK_EQ(snaps.back().turn, t0 + 2);
  CHECK_EQ(snaps.back().kind, std::string(io::kSnapBranch));
  CHECK_EQ(snaps.front().kind, std::string(io::kSnapStart));   // текущий ход t0 — его начало
  // Отмена возврата из уведомления.
  ti = toastWith(h.a(), "Отменить");
  CHECK(ti >= 0);
  CHECK(h.clickUi("toast." + std::to_string(ti) + ".action"));
  h.settle();
  CHECK_EQ(h->store.world().turn(), t0 + 2);
  CHECK(World::diff(cur, h->store.world()) == 0);
  // Вперёд по истории — к сохранённому состоянию хода t0 + 2 тем же способом.
  CHECK(app::turnui::rollbackToTurn(h.a(), t0));
  h.settle();
  CHECK_EQ(h->store.world().turn(), t0);
  CHECK(app::turnui::rollbackToTurn(h.a(), t0 + 2));
  h.settle();
  CHECK_EQ(h->store.world().turn(), t0 + 2);
  CHECK_EQ(h->store.world().log.size(), cur.log.size());
}

TEST(app_turn_history_memory_rollback_and_stale_report) {
  Harness h("turn2_memory");
  h.demo();
  int t0 = h->store.world().turn();
  CHECK(h->endTurnNow());
  CHECK(h->endTurnNow());
  CHECK_EQ(h->snapshots().size(), size_t(3));   // ходы t0, t0 + 1 и начало текущего
  // Отчёт отменённого хода не открывается.
  h.key(Key::Z, ctrl());
  CHECK_EQ(h->store.world().turn(), t0 + 1);
  h->openDialog("turn.report");
  h.settle();
  CHECK(!h->hasDialog("turn.report"));
  h.key(Key::Y, ctrl());
  CHECK_EQ(h->store.world().turn(), t0 + 2);
  h->openDialog("turn.report");
  h.settle();
  CHECK(h->hasDialog("turn.report"));
  h.key(Key::Escape);
  h.settle();
  // Возврат в несохранённом мире: снимки в памяти.
  CHECK(app::turnui::rollbackToTurn(h.a(), t0 + 1));
  h.settle();
  CHECK_EQ(h->store.world().turn(), t0 + 1);
  auto snaps = h->snapshots();
  CHECK_EQ(snaps.size(), size_t(3));
  CHECK(snaps.back().memory);
  CHECK(h->viewTurn(t0 + 2));
  CHECK(h->readOnly());
  CHECK_EQ(h->world().turn(), t0 + 2);
  h->backToCurrent();
  CHECK_EQ(h->world().turn(), t0 + 1);
  // Снимка нет — мир не меняется.
  World w = h->store.world();
  CHECK(!app::turnui::rollbackToTurn(h.a(), t0 + 40));
  CHECK(World::diff(w, h->store.world()) == 0);
}

// «Начать ход заново» после возврата и повторного прохождения хода: снимок текущего хода в истории — из
// брошенной ветки («до возврата»), а диалог обещает «мир станет таким, каким был в начале хода».
TEST(app_turn_history_restart_after_rollback) {
  Harness h("audit_cr_restart");
  h.demo();
  int t0 = h->store.world().turn();
  Id pid = test::firstOwnedLand(h->store.world());
  CHECK(pid != 0);
  std::string oldName = h->store.world().province(pid)->name;
  CHECK(h->endTurnNow());   // t0 -> t0+1
  CHECK(h->endTurnNow());   // t0+1 -> t0+2
  CHECK_EQ(h->store.world().turn(), t0 + 2);
  // Возврат к ходу t0+1 и новая ветка: переименование и завершение хода.
  CHECK(app::turnui::rollbackToTurn(h.a(), t0 + 1));
  h.settle();
  CHECK_EQ(h->store.world().turn(), t0 + 1);
  CHECK(h->act("Переименовать", [&](Tx& tx) { tx.province(pid).name = "Новая ветвь"; }));
  CHECK(h->endTurnNow());   // новая ветка: t0+1 -> t0+2
  h.settle();
  CHECK_EQ(h->store.world().turn(), t0 + 2);
  CHECK_EQ(h->store.world().province(pid)->name, std::string("Новая ветвь"));
  // В истории есть снимок текущего хода t0+2 — с кнопкой «Начать этот ход заново».
  bool hasCurrent = false;
  for (auto& s : h->snapshots())
    if (s.turn == t0 + 2) hasCurrent = true;
  CHECK(hasCurrent);
  h->openDialog("turn.history");
  h.settle();
  CHECK(h->uiRect("history.rollback." + std::to_string(t0 + 2)) != nullptr);
  CHECK(h.clickUi("history.rollback." + std::to_string(t0 + 2)));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_cr_restart_confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK_EQ(h->store.world().turn(), t0 + 2);
  const Province* p = h->store.world().province(pid);
  CHECK(p != nullptr);
  std::printf("  restart: name after «начать ход заново» = «%s» (было «%s»)\n", p->name.c_str(), oldName.c_str());
  // Исправлено (снимок начала хода в своей ветви): регрессионная проверка.
  CHECK_MSG(p->name == "Новая ветвь", "«Начать ход заново» загрузил мир брошенной ветки, а не начало текущего хода");
}

// Механика хода: «Начать ход заново» обещает «мир станет таким, каким был в начале хода», а загружает снимок
// с тем же номером хода из ЛЮБОЙ ветки: после возврата к прошлому ходу и повторного прохождения ходов снимок
// «до возврата» из брошенной ветки остаётся под номером текущего хода (app/turn/turn.cpp keepSnapshot, снимки
// хранятся по номеру хода). «Начать ход заново» подменяет текущую ветку брошенной.
TEST(app_turn_history_restart_after_rollback_saved) {
  Harness h("audit_tbmt_restart");
  h.demo();
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  BuildPick pk = pickBuild(h->world());
  CHECK(pk.pid != 0);
  const int t0 = h->store.world().turn();
  CHECK(h->endTurnNow());
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK_EQ(h->store.world().turn(), t0 + 2);
  CHECK(h->act("Имя", [&](Tx& tx) { tx.province(pk.pid).name = "Старая ветка"; }));
  // Возврат к первому ходу и повторное прохождение.
  CHECK(app::turnui::rollbackToTurn(h.a(), t0));
  h.settle();
  CHECK_EQ(h->store.world().turn(), t0);
  CHECK(h->endTurnNow());
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK_EQ(h->store.world().turn(), t0 + 2);
  CHECK(h->act("Имя", [&](Tx& tx) { tx.province(pk.pid).name = "Новая ветка"; }));
  // История: снимок текущего хода есть — значит, предлагается «Начать этот ход заново».
  bool hasCur = false;
  for (const app::TurnSnapshot& s : h->snapshots()) hasCur = hasCur || s.turn == t0 + 2;
  CHECK(hasCur);
  h->openDialog("turn.history");
  h.dropToasts();
  h.settle();
  h.settle();
  CHECK(h.clickUi("history.rollback." + std::to_string(t0 + 2)));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tbmt_restart_confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  const std::string name = h->store.world().province(pk.pid)->name;
  // Исправлено (снимки уникальны: ход + вид + номер, ветвь — цепочка parent): регрессионная проверка.
  CHECK_MSG(name != "Старая ветка", "«Начать ход заново» загрузил брошенную ветку (провинция снова «Старая ветка»), а не начало текущего хода");
  CHECK_MSG(name != "Новая ветка", "«Начать ход заново» оставил правку, сделанную в ходе");
  CHECK_EQ(h->store.world().turn(), t0 + 2);
}

// «Начать ход заново» (снимок текущего хода есть после Ctrl+Z завершения хода): диалог обещает «мир станет таким,
// каким был в начале хода», а загружался снимок КОНЦА хода. Исправлено: отдельный снимок начала хода (при открытии
// мира и после расчёта) — регрессионная проверка.
TEST(app_turn_history_restart_is_turn_start) {
  Harness h("audit_tbmt_more_restart_start");
  h.demo();
  BuildPick pk = pickBuild(h->world());
  CHECK(pk.pid != 0);
  const std::string orig = h->world().province(pk.pid)->name;
  const int t0 = h->store.world().turn();
  CHECK(h->act("Имя", [&](Tx& tx) { tx.province(pk.pid).name = "Правка в ходе"; }));
  CHECK(h->endTurnNow());
  h->toasts().clear();
  undo(h);   // завершение хода отменено — снова ход t0, снимок хода t0 остаётся
  CHECK_EQ(h->store.world().turn(), t0);
  h->openDialog("turn.history");
  h.dropToasts();
  h.settle();
  CHECK(h.clickUi("history.rollback." + std::to_string(t0)));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  const std::string name = h->store.world().province(pk.pid)->name;
  CHECK_MSG(name == orig, "после «Начать ход " + std::to_string(t0) + " заново» провинция называется «" + name + "», в начале хода было «" + orig + "»");
  CHECK_EQ(h->store.world().turn(), t0);
}

// Сохранённый мир: завершение хода сразу писало снимок в history/ на диске, хотя сам мир не сохранён; после
// «Не сохранять» история возвращала отброшенные правки. Исправлено: снимки ходов после последнего сохранения
// живут в памяти и пишутся только при сохранении — регрессионная проверка.
TEST(app_turn_history_discarded_turn_not_kept) {
  Harness h("audit_tbmt_more_discard");
  h.demo();
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  BuildPick pk = pickBuild(h->world());
  CHECK(pk.pid != 0);
  CHECK(h->act("Имя", [&](Tx& tx) { tx.province(pk.pid).name = "Отброшенная правка"; }));
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK(h->act("Без автосохранения", [](Tx& tx) { tx.settings().autosaveFolder = false; }));
  h->closeWorld();
  h.settle();
  CHECK(h->hasDialog("choice"));
  CHECK(h.clickUi("dialog.button.0"));   // «Не сохранять»
  h.settle();
  CHECK(h->loadProject(dir));
  h.settle();
  h->closeDialogs();
  CHECK_EQ(h->store.world().turn(), 1);
  CHECK(h->world().province(pk.pid)->name != "Отброшенная правка");
  // В истории — только начало текущего хода 1 (записано при первом сохранении), снимков отброшенного хода нет.
  std::string turns;
  for (auto& s : h->snapshots()) turns += " " + std::to_string(s.turn) + "/" + s.kind;
  CHECK_MSG(h->snapshots().size() == 1 && h->snapshots().front().turn == 1 && h->snapshots().front().kind == io::kSnapStart,
            "после «Не сохранять» в истории мира остались снимки отброшенных ходов:" + turns);
  for (auto& s : io::listSnapshots(dir)) CHECK_MSG(s.kind == io::kSnapStart && s.turn == 1, "на диске снимок отброшенного хода: " + s.file);
  CHECK(app::turnui::rollbackToTurn(h.a(), 1));
  h.settle();
  CHECK_MSG(h->world().province(pk.pid)->name != "Отброшенная правка",
            "«Начать ход 1 заново» вернул правку, от которой пользователь отказался («Не сохранять»)");
}

// Снимок начала хода не заменяется: повторное прохождение хода создаёт новые файлы, прежние байты на диске целы.
TEST(app_turn_history_start_never_overwritten) {
  Harness h("fix_turn_start_kept");
  h.demo();
  Id pid = test::firstOwnedLand(h->world());
  const std::string orig = nameOf(h, pid);
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  auto first = io::listSnapshots(dir);
  CHECK_EQ(first.size(), size_t(1));
  CHECK_EQ(first[0].kind, std::string(io::kSnapStart));
  const std::string startBytes = fs::readFile(fs::join(dir, first[0].file)).value_or("");
  CHECK(!startBytes.empty());
  rename(h, pid, "Ветвь 1");
  endTurn(h);
  endTurn(h);
  CHECK(app::turnui::rollbackToTurn(h.a(), 1));
  h.settle();
  rename(h, pid, "Ветвь 2");
  endTurn(h);
  endTurn(h);
  CHECK(h->save());
  auto all = io::listSnapshots(dir);
  std::set<std::string> files;
  std::set<u64> seqs;
  int starts2 = 0;
  for (auto& s : all) {
    files.insert(s.file);
    seqs.insert(s.seq);
    if (s.turn == 2 && s.kind == io::kSnapStart) starts2++;
  }
  CHECK_EQ(files.size(), all.size());
  CHECK_EQ(seqs.size(), all.size());
  CHECK_EQ(starts2, 2);   // начало хода 2 в каждой ветви
  CHECK_EQ(fs::readFile(fs::join(dir, first[0].file)).value_or(""), startBytes);
  // Начало хода 3 — ветви 2; начало хода 1 — исходный мир.
  rename(h, pid, "Правка в ходе 3");
  restartViaHistory(h);
  CHECK_EQ(nameOf(h, pid), std::string("Ветвь 2"));
  CHECK(app::turnui::rollbackToTurn(h.a(), 1));
  h.settle();
  restartViaHistory(h);
  CHECK_EQ(nameOf(h, pid), orig);
}

// Ctrl+Z и Ctrl+Y завершения хода и возврата возвращают и ветвь: «Начать ход заново» — начало хода той ветви,
// в которой мир сейчас (а не самый новый снимок начала этого хода).
TEST(app_turn_history_undo_redo_keeps_branch) {
  Harness h("fix_turn_undo_branch");
  h.demo();
  Id pid = test::firstOwnedLand(h->world());
  rename(h, pid, "А");
  endTurn(h);   // ход 2 начинается с «А»
  rename(h, pid, "Б");
  endTurn(h);   // ход 3 (ветвь 1) начинается с «Б»
  CHECK_EQ(h->store.world().turn(), 3);
  rename(h, pid, "Б2");
  CHECK(app::turnui::rollbackToTurn(h.a(), 2));   // конец хода 2: «Б»
  h.settle();
  CHECK_EQ(nameOf(h, pid), std::string("Б"));
  rename(h, pid, "В");
  endTurn(h);   // ход 3 (ветвь 2) начинается с «В» — самое новое начало хода 3
  // Отмена завершения хода: снова ход 2 ветви 2; его начало — «А».
  h.key(Key::Z, ctrl());
  CHECK_EQ(h->store.world().turn(), 2);
  CHECK_EQ(nameOf(h, pid), std::string("В"));
  h.key(Key::Z, ctrl());   // правка «В»
  h.key(Key::Z, ctrl());   // возврат к ходу 2 — снова ход 3 ветви 1
  CHECK_EQ(h->store.world().turn(), 3);
  CHECK_EQ(nameOf(h, pid), std::string("Б2"));
  restartViaHistory(h);
  CHECK_EQ(nameOf(h, pid), std::string("Б"));
  // Отмена и повтор самого «Начать заново».
  h.key(Key::Z, ctrl());
  CHECK_EQ(nameOf(h, pid), std::string("Б2"));
  h.key(Key::Y, ctrl());
  CHECK_EQ(nameOf(h, pid), std::string("Б"));
  // Ход 2 ветви 1 начинается с «А».
  CHECK(app::turnui::rollbackToTurn(h.a(), 2));
  h.settle();
  rename(h, pid, "Г");
  restartViaHistory(h);
  CHECK_EQ(nameOf(h, pid), std::string("А"));
}

// Ветвь сохранённого мира записывается в историю (head): после повторного открытия «Начать ход заново» — начало
// хода сохранённой ветви, даже если более новый снимок начала этого хода принадлежит другой ветви.
TEST(app_turn_history_branch_survives_reopen) {
  Harness h("fix_turn_branch_reopen");
  h.demo();
  Id pid = test::firstOwnedLand(h->world());
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  rename(h, pid, "Ветвь 1");
  endTurn(h);
  endTurn(h);   // ход 3 ветви 1
  CHECK(app::turnui::rollbackToTurn(h.a(), 2));
  h.settle();
  rename(h, pid, "Ветвь 2");
  endTurn(h);   // ход 3 ветви 2
  h.key(Key::Z, ctrl());
  h.key(Key::Z, ctrl());
  h.key(Key::Z, ctrl());   // снова ход 3 ветви 1
  CHECK_EQ(nameOf(h, pid), std::string("Ветвь 1"));
  CHECK_EQ(h->store.world().turn(), 3);
  rename(h, pid, "Правка хода 3");
  CHECK(h->save());
  CHECK(h->loadProject(dir));
  h.settle();
  h->closeDialogs();
  CHECK_EQ(nameOf(h, pid), std::string("Правка хода 3"));
  restartViaHistory(h);
  CHECK_EQ(nameOf(h, pid), std::string("Ветвь 1"));
  // Архив: та же ветвь после «Сохранить как» и открытия.
  std::string bundle = fs::join(h.root, "Мир.regnum");
  rename(h, pid, "Правка в архиве");
  CHECK(h->saveTo(bundle));
  CHECK(h->loadProject(bundle));
  h.settle();
  h->closeDialogs();
  restartViaHistory(h);
  CHECK_EQ(nameOf(h, pid), std::string("Ветвь 1"));
}

// Смена мира: снимки прежнего мира (и несохранённые, и с диска) не переходят к открытому, новому или к экрану
// запуска; «Не сохранять» отбрасывает снимки несохранённых ходов.
TEST(app_turn_history_reset_on_world_change) {
  Harness h("fix_turn_reset");
  h.demo();
  Id pid = test::firstOwnedLand(h->world());
  std::string a = fs::join(h.root, "Мир А"), b = fs::join(h.root, "Мир Б");
  CHECK(h->saveTo(a));
  endTurn(h);
  endTurn(h);
  CHECK(h->save());
  CHECK_EQ(h->snapshots().size(), size_t(3));
  // Мир Б: другой мир без ходов.
  {
    World w = map::makeDemoWorld(*h->basemap());
    Tx tx(w);
    tx.meta().name = "Мир Б";
    h->loadWorld(std::move(tx).finish(), "Мир Б");
    h.settle();
  }
  CHECK_EQ(h->snapshots().size(), size_t(1));
  CHECK(h->saveTo(b));
  CHECK_EQ(io::listSnapshots(b).size(), size_t(1));
  // Открыть А — история А; несохранённый ход и «Не сохранять» при открытии Б.
  CHECK(h->loadProject(a));
  h.settle();
  h->closeDialogs();
  CHECK_EQ(h->snapshots().size(), size_t(3));
  rename(h, pid, "Отброшенная правка");
  endTurn(h);
  CHECK(h->act("Без автосохранения", [](Tx& tx) { tx.settings().autosaveFolder = false; }));
  CHECK_EQ(h->snapshots().size(), size_t(4));
  h->openPath(b);
  h.settle();
  CHECK(h->hasDialog("choice"));
  CHECK(h.clickUi("dialog.button.0"));   // «Не сохранять»
  h.settle();
  CHECK_EQ(h->projectPath(), fs::absolute(b));
  auto snaps = h->snapshots();
  CHECK_EQ(snaps.size(), size_t(1));
  for (auto& s : snaps) CHECK_EQ(io::loadSnapshotFile(b, s.key).world.meta->name, std::string("Мир Б"));
  CHECK(h->loadProject(a));
  h.settle();
  h->closeDialogs();
  CHECK_EQ(h->store.world().turn(), 3);
  CHECK_EQ(h->snapshots().size(), size_t(3));   // хода 3 → 4 нет ни на диске, ни в памяти
  for (auto& s : io::listSnapshots(a)) CHECK(s.turn <= 3);
  // Экран запуска — истории нет.
  h->closeWorld();
  h.settle();
  CHECK(h->ui.screen == app::Screen::Start);
  CHECK(h->snapshots().empty());
}
