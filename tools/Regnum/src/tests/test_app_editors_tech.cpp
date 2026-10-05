// Сценарии сроков исследования с модификатором государства («Время исследования технологий»,
// rules::researchTurns): вкладка «Технологии» и дерево показывают действительный срок, исследование завершается по
// нему (пройденные ходы могут превысить базовый срок).
#include "app/editors/techtree.h"
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;

TEST(app_editors_tech_research_time) {
  Harness h("editors_tech_time");
  h.demo();
  h.dropToasts();
  auto [A, B] = twoStates(h->world());
  (void)B;
  // Технология без условий на 2 хода и модификатор государства +100 % ко времени исследования.
  Id t = 0, mod = 0;
  CHECK(h->act("Технология", [&](Tx& tx) {
    t = rules::createTech(tx, A, "Картография");
    tx.tech(t).turns = 2;
    tx.tech(t).pos = {0, -300};
    mod = rules::createModifier(tx, "Медленные архивы");
    Modifier& m = tx.modifier(mod);
    m.kind = ModKind::Faction;
    m.fxMask = 1u << int(Fx::ResearchTimePct);
    m.fx[size_t(Fx::ResearchTimePct)] = 100;
    tx.faction(A).modifiers.push_back(mod);
  }));
  const int need = rules::researchTurns(h->world(), *h->world().tech(t));
  CHECK_MSG(need >= 4, std::to_string(need));   // 2 × (1 + 1,0 + слабый контроль демо-мира)
  // Вкладка государства: начать исследование, остаток — по действительному сроку.
  h->ui.tabOf[app::SelType::Faction] = "faction.tech";
  h->select(app::SelType::Faction, A);
  quick(h);
  CHECK(h->uiRect("faction.tech.left." + std::to_string(t)) != nullptr);
  CHECK(clickRevealed(h, "faction.tech.start." + std::to_string(t), "inspector", 100));
  CHECK(h->world().tech(t)->research);
  quick(h);
  CHECK(h->uiRect("faction.tech.progress." + std::to_string(t)) != nullptr);
  // need − 1 ходов: пройдено больше базового срока, ещё не изучена.
  for (int i = 0; i < need - 1; i++) {
    CHECK(h->endTurnNow());
    h->toasts().clear();
  }
  CHECK_EQ(h->world().tech(t)->progress, need - 1);
  CHECK(h->world().tech(t)->progress > h->world().tech(t)->turns);
  CHECK(!h->world().tech(t)->studied);
  quick(h);
  shotClean(h, "editors_tech_tab");
  // Дерево: срок с модификаторами рядом с базовым.
  app::openTechTree(h.a(), A, t);
  quick(h);
  CHECK(h->uiRect("tt.side.need") != nullptr);
  shotClean(h, "editors_tech_tree");
  h->closeEditor();
  quick(h);
  // Последний ход — изучена.
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK(h->world().tech(t)->studied);
}
