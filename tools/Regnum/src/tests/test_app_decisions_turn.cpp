// Блоки «Решений», связанные с ходом (аудит 2026-10-23, 2.8): археологические группы в смертельной опасности —
// администратору всех государств, игроку — своего; строка ведёт во вкладку «Археология» государства; число на значке.
#include "app/panels/decisions.h"
#include "app/shell/role.h"
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;

TEST(app_decisions_turn_arch_danger) {
  Harness h("decisions_arch_danger");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг"), kor = factionByName(h->world(), "Корвен");
  CHECK(hel && kor);
  Id gid = 0;
  CHECK(h->act("Группа в опасности", [&](Tx& tx) {
    ArchGroup g;
    g.id = tx.nextId(Seq::ArchGroup);
    g.name = "Искатели";
    g.danger = true;
    gid = g.id;
    tx.faction(hel).archGroups.push_back(std::move(g));
  }));
  CHECK_EQ(app::decisions::archDangerBadge(h.a(), hel), -1);
  CHECK_EQ(app::decisions::archDangerBadge(h.a(), kor), 0);
  CHECK(app::decisions::turnPending(h.a()) >= 1);
  h->openSection("decisions");
  h.settle();
  const std::string mark = "decisions.arch." + std::to_string(hel) + "." + std::to_string(gid);
  CHECK(h->uiRect("decisions.arch") != nullptr);
  CHECK(h->uiRect(mark) != nullptr);
  CHECK(h->uiRect("section.decisions.badge") != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("decisions_arch_danger"));
  CHECK(h.clickUi(mark));
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Faction, hel}));
  CHECK_EQ(h->ui.tabOf[app::SelType::Faction], std::string("faction.arch"));
  // Игрок другого государства чужих групп не видит; игрок этого государства — видит.
  role::becomePlayer(h.a(), kor);
  h.settle();
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect("decisions.arch") == nullptr);
  CHECK_EQ(app::decisions::archDangerBadge(h.a(), hel), 0);
  role::becomePlayer(h.a(), hel);
  h.settle();
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect(mark) != nullptr);
  CHECK(h->uiRect("decisions.events") == nullptr);   // события хода разбирает администратор
  CHECK(h->uiRect("decisions.journal") == nullptr);
}
