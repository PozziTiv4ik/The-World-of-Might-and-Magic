// Вкладка «События» провинции, государства, героя и войска (аудит 2026-10-23, 8.3): записи хроники о сущности общими
// строками хроники; щелчок по записи — её объект (провинция на карте).
#include "app/turn/turn_ui.h"
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;
namespace tu = rg::app::turnui;

TEST(app_events_tabs_mentions_whole_words) {
  CHECK(tu::mentions("«Ингвар Седобородый» вернулся", "Ингвар Седобородый"));
  CHECK(tu::mentions("Погиб ингвар седобородый.", "Ингвар Седобородый"));
  CHECK(tu::mentions("Ёрм в плену", "Ерм"));
  CHECK(!tu::mentions("Ингварссон прибыл", "Ингвар"));
  CHECK(!tu::mentions("Праингвар прибыл", "Ингвар"));
  CHECK(!tu::mentions("Кто-то прибыл", ""));
}

TEST(app_events_tabs_entities) {
  Harness h("events_tabs", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel, 100);
  const Id hero = freeHero(h, hel);
  CHECK(pid && hero);
  if (!pid || !hero) return;
  const std::string name = h->world().character(hero)->name;
  const Id army = spawnArmy(h, hel, geo::faces(h->world())->shape(pid)->label, 50);
  CHECK(army != 0);
  Id provEntry = 0, facEntry = 0, heroEntry = 0, armyEntry = 0;
  CHECK(h->act("Записи", [&](Tx& tx) {
    provEntry = rules::addLog(tx, LogKind::Note, "Ярмарка в провинции", rules::LogRefs{pid, 0, {}});
    facEntry = rules::addLog(tx, LogKind::Note, "Указ ярла", rules::LogRefs{0, 0, {hel}});
    heroEntry = rules::addLog(tx, LogKind::Note, "«" + name + "» вернулся из похода", rules::LogRefs{pid, 0, {}});
    armyEntry = rules::addLog(tx, LogKind::Army, "Войско выступило", rules::LogRefs{0, army, {hel}});
  }));
  // Провинция: записи с ней.
  provinceTab(h, pid, "province.events");
  const std::string pm = "province.events." + std::to_string(pid) + ".";
  CHECK(h->uiRect(pm + std::to_string(provEntry)) != nullptr);
  CHECK(h->uiRect(pm + std::to_string(heroEntry)) != nullptr);
  CHECK(h->uiRect(pm + std::to_string(facEntry)) == nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("events_tab_province"));
  // Государство: записи, где оно среди фракций.
  factionTab(h, hel, "faction.events");
  const std::string fm = "faction.events." + std::to_string(hel) + ".";
  CHECK(h->uiRect(fm + std::to_string(facEntry)) != nullptr);
  CHECK(h->uiRect(fm + std::to_string(armyEntry)) != nullptr);
  CHECK(h->uiRect(fm + std::to_string(provEntry)) == nullptr);
  // Герой: записи, где он упомянут; щелчок — провинция записи на карте.
  h->ui.tabOf[app::SelType::Character] = "character.events";
  h->select(app::SelType::Character, hero);
  h.settle();
  const std::string hm = "character.events." + std::to_string(hero) + "." + std::to_string(heroEntry);
  CHECK(h->uiRect(hm) != nullptr);
  CHECK(h->uiRect("character.events." + std::to_string(hero) + "." + std::to_string(provEntry)) == nullptr);
  CHECK(h.clickUi(hm));
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Province, pid}));
  // Войско: та же вкладка.
  openTab(h, app::SelType::Army, army, "army.log");
  CHECK(h->uiRect("army.events." + std::to_string(army) + "." + std::to_string(armyEntry)) != nullptr);
}
