// Журнал действий игроков (аудит 2026-10-23, раздел 11): игровые действия игрока (App::play) дописываются в
// history/journal.jsonl папки мира — ход, государство, действие, время; подряд идущие одинаковые — одной строкой, без
// изменения мира — не пишутся, действия администратора — не пишутся. Администратор видит журнал в «Решениях».
#include "app/shell/role.h"
#include "app/turn/journal.h"
#include "base/json.h"
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;

namespace {

std::vector<std::string> fileLines(const std::string& path) {
  std::vector<std::string> out;
  if (auto t = fs::readFile(path))
    for (const std::string& l : split(*t, '\n'))
      if (!trim(l).empty()) out.push_back(l);
  return out;
}

}  // namespace

TEST(app_turn_journal_player_actions) {
  Harness h("turn_journal");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  CHECK(hel != 0);
  const std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  const std::string file = fs::join(dir, "history/journal.jsonl");
  CHECK_EQ(app::journal::fileOf(h.a()), file);
  role::becomePlayer(h.a(), hel);
  h.settle();
  CHECK(h->play("Налог государства", [&](Tx& tx) { tx.faction(hel).tax = 12; }));
  CHECK(h->play("Налог государства", [&](Tx& tx) { tx.faction(hel).tax = 13; }));   // подряд — одна строка
  CHECK(h->play("Ничего", [&](Tx&) {}));                                              // мир не изменён — не пишется
  CHECK(h->play("Готов завершить ход", [&](Tx& tx) { rules::setTurnReady(tx, hel, true); }));
  std::vector<std::string> lines = fileLines(file);
  CHECK_EQ(lines.size(), size_t(2));
  if (lines.size() == 2) {
    CHECK(lines[0].find("Налог государства") != std::string::npos);
    CHECK(lines[1].find("Готов завершить ход") != std::string::npos);
    CHECK(json::tryParse(lines[0]).has_value());   // строка — объект JSON
  }
  // Администратор: его действия не пишутся; журнал — в «Решениях», новые сверху.
  role::becomeAdmin(h.a());
  h.settle();
  CHECK(h->act("Правка", [&](Tx& tx) { tx.faction(hel).tax = 10; }));
  CHECK_EQ(fileLines(file).size(), size_t(2));
  const std::vector<app::journal::Entry> e = app::journal::entries(h.a());
  CHECK_EQ(e.size(), size_t(2));
  if (e.size() == 2) {
    CHECK_EQ(e[0].action, std::string("Готов завершить ход"));
    CHECK_EQ(e[0].state, hel);
    CHECK_EQ(e[0].turn, h->world().turn());
  }
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect("decisions.journal") != nullptr);
  CHECK(h->uiRect("decisions.journal.0") == nullptr);   // блок свёрнут
  CHECK(h.clickUi("decisions.journal"));
  h.settle();
  CHECK(h->uiRect("decisions.journal.0") != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("turn_journal_decisions"));
}

// Мир без папки: журнал сеанса в памяти.
TEST(app_turn_journal_session_without_folder) {
  Harness h("turn_journal_memory");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  role::becomePlayer(h.a(), hel);
  h.settle();
  CHECK(app::journal::fileOf(h.a()).empty());
  CHECK(h->play("Налог государства", [&](Tx& tx) { tx.faction(hel).tax = 15; }));
  const std::vector<app::journal::Entry> e = app::journal::entries(h.a());
  CHECK_EQ(e.size(), size_t(1));
}
