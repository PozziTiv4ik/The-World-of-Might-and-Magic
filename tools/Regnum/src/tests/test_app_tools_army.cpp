// Инструмент «Новое войско» администратора (аудит 2026-10-23, 8.7): «Без государства» в выборе фракции — войско без
// государства (rules::spawnWildArmy) с отрядом выбранного типа и численности; игроку пункта нет.
#include "app/shell/role.h"
#include "tests/test_app_military_util.h"

using namespace rg;
using namespace rg::apptest;

TEST(app_tools_army_wild) {
  Harness h("tools_army_wild");
  h.demo();
  h->setTool(app::ToolId::NewArmy);
  h.settle();
  CHECK(h->uiRect("place.faction") != nullptr);
  CHECK(h->uiRect("place.wild.type") == nullptr);
  // «Без государства» — последний пункт выбора фракции (поиск по названию).
  CHECK(h.clickUi("place.faction"));
  h.type("Без государства");
  h.key(Key::Enter);
  h.settle();
  CHECK(h->uiRect("place.wild.type") != nullptr);
  CHECK(h->uiRect("place.wild.count") != nullptr);
  CHECK(h.clickUi("place.wild.count"));
  h.retype("250");
  h.key(Key::Enter);
  h.settle();
  h.dropToasts();
  h.settle();
  if (const RectF* bar = h->uiRect("tool.options")) CHECK(cropShot("tools_army_wild_bar", *bar, 2));
  auto spot = freeSpot(h, ArmyKind::Army);
  CHECK(spot.has_value());
  if (!spot) return;
  h.click(spot->second.x, spot->second.y);
  h.settle();
  const Id wild = rules::wildFaction(h->world());
  CHECK(wild != 0);
  const Id army = h->ui.sel.type == app::SelType::Army ? h->ui.sel.id : 0;
  CHECK(army != 0);
  if (!army) return;
  const Army* ar = h->world().army(army);
  CHECK(ar && ar->leader() == wild);
  CHECK_EQ(ar ? app::mil::unitCount(*ar) : 0, i64(250));
  // Отмена — одним шагом.
  h->undo();
  h.settle();
  CHECK(!h->world().army(army));
}

TEST(app_tools_army_wild_admin_only) {
  Harness h("tools_army_wild_player");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  app::detail::role::becomePlayer(h.a(), hel);
  h.settle();
  app::mil::startPlacing(h.a(), ArmyKind::Army, hel);
  h.settle();
  CHECK(h->ui.tool == app::ToolId::NewArmy);
  CHECK(h->uiRect("place.wild.type") == nullptr);
}
