// Сценарии хроники: поиск, фильтр вида записи, запись ведущего, переход к объекту записи.
#include "app/turn/history.h"
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;

TEST(app_chronicle_filter_note) {
  Harness h("turn2_chronicle");
  h.demo();
  // Демо-экономика сбалансирована: государство с отрицательным чистым доходом и почти пустой казной уйдёт в долг.
  Id debtor = 0;
  auto c0 = rules::calc(h->store.world());
  h->store.world().factions.each([&](const Faction& f) {
    if (!debtor && f.isState() && c0->faction(f.id)->net < 0) debtor = f.id;
  });
  CHECK(debtor != 0);
  CHECK(h->act("Казна", [&](Tx& tx) { tx.faction(debtor).res[kGold] = 1; }));
  int t0 = h->store.world().turn();
  CHECK(h->endTurnNow());
  h.settle();
  const World& w0 = h->store.world();
  CHECK(h.clickUi("drawer.chronicle"));
  h.settle();
  CHECK_EQ(h->ui.drawer, std::string("chronicle"));
  shotClean(h, "turn2_chronicle");
  // Все записи хода видны (новые сверху, группы по ходам).
  Id turnEntry = 0, buildEntry = 0, debtEntry = 0;
  w0.log.each([&](const LogEntry& e) {
    if (e.turn != t0) return;
    if (e.kind == LogKind::Turn) turnEntry = e.id;
    if (e.kind == LogKind::Build && !buildEntry) buildEntry = e.id;
    if (e.kind == LogKind::Economy && !debtEntry) debtEntry = e.id;
  });
  CHECK(turnEntry && debtEntry);
  CHECK(h->uiRect("chronicle.entry." + std::to_string(turnEntry)) != nullptr);
  // Поиск.
  CHECK(h.clickUi("chronicle.search"));
  h.type("долг");
  h.settle();
  CHECK(h->uiRect("chronicle.entry." + std::to_string(debtEntry)) != nullptr);
  CHECK(h->uiRect("chronicle.entry." + std::to_string(turnEntry)) == nullptr);
  h.key(Key::A, ctrl());
  h.key(Key::Backspace);
  h.key(Key::Escape);
  h.settle();
  CHECK(h->uiRect("chronicle.entry." + std::to_string(turnEntry)) != nullptr);
  // Фильтр вида: только «Ход».
  CHECK(h.clickUi("chronicle.filter"));
  h.settle();
  CHECK(h.clickUi("chronicle.kind." + std::to_string(int(LogKind::Turn))));
  h.settle();
  shotClean(h, "turn2_chronicle_filter");
  h.key(Key::Escape);
  h.settle();
  CHECK(h->uiRect("chronicle.entry." + std::to_string(turnEntry)) != nullptr);
  CHECK(h->uiRect("chronicle.entry." + std::to_string(debtEntry)) == nullptr);
  CHECK(h.clickUi("chronicle.filter"));
  h.settle();
  CHECK(h.clickUi("chronicle.kind." + std::to_string(int(LogKind::Turn))));
  h.key(Key::Escape);
  h.settle();
  CHECK(h->uiRect("chronicle.entry." + std::to_string(debtEntry)) != nullptr);

  // Запись ведущего с провинцией.
  Id pid = 0;
  std::string pname;
  w0.provinces.each([&](const Province& p) {
    if (!pid && !p.sea && p.owner && !p.name.empty()) {
      pid = p.id;
      pname = p.name;
    }
  });
  u32 logs = h->store.world().log.size();
  CHECK(h.clickUi("chronicle.note"));
  h.settle();
  CHECK(h->hasDialog("chronicle.note"));
  h.type("Пир в столице по случаю урожая");
  CHECK(h.clickUi("note.province"));
  h.type(pname);
  h.key(Key::Enter);
  h.settle();
  shotClean(h, "turn2_chronicle_note");
  CHECK(h.clickUi("note.ok"));
  h.settle();
  CHECK(!h->hasDialog());
  const World& w1 = h->store.world();
  CHECK_EQ(w1.log.size(), logs + 1);
  Id note = 0;
  w1.log.each([&](const LogEntry& e) {
    if (e.kind == LogKind::Note) note = e.id;
  });
  CHECK(note != 0);
  const LogEntry* ne = w1.log.get(note);
  CHECK(ne && ne->text == "Пир в столице по случаю урожая");
  CHECK(ne && ne->turn == w1.turn() && ne->province == pid);
  // Щелчок по записи — провинция выделена и показана.
  CHECK(h.clickUi("chronicle.entry." + std::to_string(note)));
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Province, pid}));
  shotClean(h, "turn2_chronicle_focus");
  // Запись отменяется Ctrl+Z.
  h.key(Key::Escape);
  h->undo();
  CHECK_EQ(h->store.world().log.size(), logs);
}
