// Раздел «Мой ход» (аудит 2026-10-23, раздел 11): что ждёт государство в этом ходу — войска с очком действий, стройки,
// исследования, археологи, таланты, войны, предложения; каждая строка ведёт к объекту. Игрок видит своё государство,
// администратор выбирает государство вверху страницы.
#include "app/shell/role.h"
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;

TEST(app_my_turn_player_lines_lead_to_objects) {
  Harness h("my_turn", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг"), kor = factionByName(h->world(), "Корвен");
  CHECK(hel && kor);
  const Id pid = calmProvince(h, hel, 100);
  CHECK(pid != 0);
  if (!pid) return;
  const Id army = spawnArmy(h, hel, geo::faces(h->world())->shape(pid)->label, 100);
  CHECK(army != 0);
  CHECK(h->act("Война", [&](Tx& tx) { rules::setRelation(tx, hel, kor, -50, RelStatus::War); }));
  role::becomePlayer(h.a(), hel);
  h.settle();
  CHECK(h->uiRect("section.myturn") != nullptr);
  h->openSection("myturn");
  h.settle();
  CHECK(h->uiRect("myturn.state") == nullptr);   // игрок — только своё государство
  CHECK(h->uiRect("myturn.armies") != nullptr);
  CHECK(h->uiRect("myturn.armies." + std::to_string(army)) != nullptr);
  CHECK(h->uiRect("myturn.wars." + std::to_string(kor)) != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("my_turn_player"));
  // Войско — выделено и показано на карте.
  CHECK(h.clickUi("myturn.armies." + std::to_string(army)));
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Army, army}));
  CHECK(h->mapShown());
  // Войско сходило — строки больше нет.
  CHECK(h->play("Ход сделан", [&](Tx& tx) { rules::spendAction(tx, army); }));
  h->openSection("myturn");
  h.settle();
  CHECK(h->uiRect("myturn.armies." + std::to_string(army)) == nullptr);
  // Война — страница противника, вкладка «Дипломатия».
  CHECK(h.clickUi("myturn.wars." + std::to_string(kor)));
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Faction, kor}));
  CHECK_EQ(h->ui.tabOf[app::SelType::Faction], std::string("faction.diplomacy"));
}

// Администратор: государство выбирается вверху страницы (по умолчанию — последнее открытое); ход любого государства
// (без числа на значке).
TEST(app_my_turn_admin_picks_state) {
  Harness h("my_turn_admin", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  CHECK(hel != 0);
  h->select(app::SelType::Faction, hel);   // страница государства — оно последнее открытое
  h.settle();
  h->openSection("myturn");
  h.settle();
  CHECK(h->uiRect("myturn.state") != nullptr);
  CHECK(h->uiRect("section.myturn.badge") == nullptr);
  // Строки — о нём: его войска с очком действий.
  Id army = 0;
  h->world().armies.each([&](const Army& a) {
    if (!army && a.leader() == hel && rules::hasAction(h->world(), a.id)) army = a.id;
  });
  CHECK(army != 0);
  CHECK(h->uiRect("myturn.armies." + std::to_string(army)) != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("my_turn_admin"));
}
