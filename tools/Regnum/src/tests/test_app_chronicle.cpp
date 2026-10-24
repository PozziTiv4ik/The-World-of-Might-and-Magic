// Сценарии хроники: поиск, фильтр вида записи, запись ведущего, переход к объекту записи.
#include "app/shell/role.h"
#include "app/turn/history.h"
#include "app/turn/turn_ui.h"
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

// Туман войны в хронике (аудит 2026-10-23, 1.5 и 8.3): игрок видит общие записи и записи с участием своего государства,
// союзников, сюзерена и вассалов; записи о чужих государствах скрыты. «Моё государство» — быстрый отбор.
TEST(app_chronicle_player_fog_and_mine) {
  Harness h("chronicle_fog");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг"), kor = factionByName(h->world(), "Корвен"), sol = factionByName(h->world(), "Солмар");
  CHECK(hel && kor && sol);
  Id own = 0, ally = 0, foreign = 0, general = 0;
  CHECK(h->act("Записи", [&](Tx& tx) {
    rules::setRelation(tx, hel, sol, 60, RelStatus::Alliance);
    rules::setRelation(tx, hel, kor, 0, RelStatus::Neutral);
    if (tx.w().faction(kor)->suzerain == hel) rules::setSuzerain(tx, kor, 0);   // Корвен — не вассал и не сюзерен
    if (tx.w().faction(hel)->suzerain == kor) rules::setSuzerain(tx, hel, 0);
    own = rules::addLog(tx, LogKind::Note, "Запись о Хельдвиге", rules::LogRefs{0, 0, {hel}});
    ally = rules::addLog(tx, LogKind::Note, "Запись о союзнике", rules::LogRefs{0, 0, {sol}});
    foreign = rules::addLog(tx, LogKind::Note, "Запись о Корвене", rules::LogRefs{0, 0, {kor}});
    general = rules::addLog(tx, LogKind::Note, "Общая запись", {});
  }));
  app::detail::role::becomePlayer(h.a(), hel);
  h.settle();
  app::turnui::showChronicle(h.a(), 0);
  h.settle();
  CHECK_EQ(h->ui.drawer, std::string("chronicle"));
  CHECK(h->uiRect("chronicle.entry." + std::to_string(own)) != nullptr);
  CHECK(h->uiRect("chronicle.entry." + std::to_string(ally)) != nullptr);
  CHECK(h->uiRect("chronicle.entry." + std::to_string(general)) != nullptr);
  CHECK(h->uiRect("chronicle.entry." + std::to_string(foreign)) == nullptr);
  CHECK(h->uiRect("chronicle.note") == nullptr);   // запись в хронику — правка мира
  // «Моё государство»: только записи о нём; повторно — снова все видимые.
  CHECK(h.clickUi("chronicle.mine"));
  h.settle();
  CHECK(h->uiRect("chronicle.entry." + std::to_string(own)) != nullptr);
  CHECK(h->uiRect("chronicle.entry." + std::to_string(ally)) == nullptr);
  CHECK(h->uiRect("chronicle.entry." + std::to_string(general)) == nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("chronicle_player_mine"));
  CHECK(h.clickUi("chronicle.mine"));
  h.settle();
  CHECK(h->uiRect("chronicle.entry." + std::to_string(ally)) != nullptr);
  // Администратор видит всё.
  app::detail::role::becomeAdmin(h.a());
  h.settle();
  app::turnui::showChronicle(h.a(), 0);
  h.settle();
  CHECK(h->uiRect("chronicle.entry." + std::to_string(foreign)) != nullptr);
  CHECK(h->uiRect("chronicle.mine") == nullptr);
}
