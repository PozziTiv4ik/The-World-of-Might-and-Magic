// Сценарии итогов хода на двух страницах (ТЗ «Фиксы», п.16): основные игровые государства (Faction::mainState)
// и остальные фракции — подтверждение завершения хода и отчёт о ходе; записи хроники по страницам.
#include "app/dialogs/turn_ui.h"
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;
namespace turnui = rg::app::turnui;

TEST(app_editors_turn_pages) {
  Harness h("editors_turn_pages");
  h.demo();
  h.dropToasts();
  auto [A, B] = twoStates(h->world());
  Id guild = firstGuild(h->world());
  CHECK(A && B && guild);
  CHECK(h->act("Основное государство", [&](Tx& tx) { rules::setMainState(tx, A, true); }));
  Id firstOther = 0;   // первая строка страницы остальных (таблица показывает только видимые строки)
  // Разбиение по страницам.
  {
    const World& w = h->world();
    CHECK(turnui::isMainState(w, A));
    CHECK(!turnui::isMainState(w, B));
    CHECK(!turnui::isMainState(w, guild));
    LogEntry e;
    e.factions = {A};
    CHECK(turnui::entryOnPage(w, e, turnui::kPageMain));
    CHECK(!turnui::entryOnPage(w, e, turnui::kPageOther));
    e.factions = {B};
    CHECK(!turnui::entryOnPage(w, e, turnui::kPageMain));
    CHECK(turnui::entryOnPage(w, e, turnui::kPageOther));
    e.factions = {A, B};   // о двух сторонах — на обеих страницах
    CHECK(turnui::entryOnPage(w, e, turnui::kPageMain));
    CHECK(turnui::entryOnPage(w, e, turnui::kPageOther));
    e.factions.clear();    // без фракций — у остальных
    CHECK(!turnui::entryOnPage(w, e, turnui::kPageMain));
    CHECK(turnui::entryOnPage(w, e, turnui::kPageOther));
    rules::TurnReport rep = rules::previewTurn(w);
    auto main = turnui::sortedLines(w, rep, turnui::kPageMain);
    auto other = turnui::sortedLines(w, rep, turnui::kPageOther);
    CHECK_EQ(main.size(), size_t(1));
    CHECK(!main.empty() && main[0]->faction == A);
    CHECK_EQ(main.size() + other.size(), rep.factions.size());
    for (auto* l : other) CHECK(l->faction != A);
    if (!other.empty()) firstOther = other[0]->faction;
  }
  CHECK(firstOther != 0);
  // Подтверждение хода: первая страница — основные государства.
  CHECK(h.clickUi("topbar.endturn"));
  h.settle();
  CHECK(h->hasDialog("turn.confirm"));
  CHECK(h->uiRect("turn.confirm.pages") != nullptr);
  CHECK(h->uiRect("turn.confirm.row." + std::to_string(A)) != nullptr);
  CHECK(h->uiRect("turn.confirm.row." + std::to_string(firstOther)) == nullptr);
  shotClean(h, "editors_turn_confirm_main");
  // Вторая страница — остальные государства и гильдии.
  CHECK(clickPart(h, "turn.confirm.pages", 1, 2));
  quick(h);
  CHECK(h->uiRect("turn.confirm.row." + std::to_string(A)) == nullptr);
  CHECK(h->uiRect("turn.confirm.row." + std::to_string(firstOther)) != nullptr);
  shotClean(h, "editors_turn_confirm_other");
  // Вкладки страницы остальных: события и предупреждения.
  CHECK(clickPart(h, "turn.confirm.tabs", 2, 4));
  quick(h);
  CHECK(h->hasDialog("turn.confirm"));
  CHECK(clickPart(h, "turn.confirm.tabs", 0, 4));
  quick(h);
  // Завершить ход — отчёт с теми же страницами.
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->lastTurnReport().has_value());
  h->toasts().clear();
  h->openDialog("turn.report");
  h.settle();
  CHECK(h->hasDialog("turn.report"));
  CHECK(h->uiRect("turn.report.pages") != nullptr);
  CHECK(h->uiRect("turn.report.card." + std::to_string(A)) != nullptr);
  CHECK(h->uiRect("turn.report.card." + std::to_string(B)) == nullptr);
  shotClean(h, "editors_turn_report_main");
  CHECK(clickPart(h, "turn.report.pages", 1, 2));
  quick(h);
  CHECK(h->uiRect("turn.report.card." + std::to_string(A)) == nullptr);
  CHECK(h->uiRect("turn.report.card." + std::to_string(B)) != nullptr);
  shotClean(h, "editors_turn_report_other");
}

TEST(app_editors_turn_no_main_states) {
  Harness h("editors_turn_nomain");
  h.demo();
  h.dropToasts();
  rules::TurnReport rep = rules::previewTurn(h->world());
  auto lines = turnui::sortedLines(h->world(), rep, turnui::kPageOther);
  CHECK_EQ(lines.size(), rep.factions.size());
  CHECK(turnui::sortedLines(h->world(), rep, turnui::kPageMain).empty());
  Id first = lines.empty() ? 0 : lines[0]->faction;
  CHECK(first != 0);
  // Основных государств нет — подтверждение сразу открывается на странице остальных.
  h->endTurn();
  h.settle();
  CHECK(h->hasDialog("turn.confirm"));
  CHECK(h->uiRect("turn.confirm.row." + std::to_string(first)) != nullptr);
  CHECK(clickPart(h, "turn.confirm.pages", 0, 2));
  quick(h);
  CHECK(h->uiRect("turn.confirm.row." + std::to_string(first)) == nullptr);
  shotClean(h, "editors_turn_confirm_empty");
}
