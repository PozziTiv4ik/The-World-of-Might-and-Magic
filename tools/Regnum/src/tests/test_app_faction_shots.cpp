// Снимки панелей государств и гильдий на демонстрационном мире: шапка и каждая вкладка инспектора, списки
// «Государства» и «Гильдии», редактор флага; те же экраны в другом цветокоре; вкладки и редакторы государства (совет,
// провинции, герои, экономика, торговля, дерево технологий).
#include "app/editors/techtree.h"
#include "app/editors/trade.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

TEST(app_faction_shots_state) {
  Harness h("faction_shots_state");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id st = findFaction(h->world(), "Королевство Альмарин");
  CHECK(st != 0);
  const char* tabs[] = {"faction.overview", "faction.council", "faction.provinces", "faction.economy", "faction.diplomacy",
                        "faction.heroes", "faction.modifiers", "faction.guilds"};
  for (const char* t : tabs) {
    openTab(h, st, t);
    CHECK_EQ(h->ui.tabOf[app::SelType::Faction], std::string(t));
    CHECK(h->uiRect("faction.flag") != nullptr);
    h.waitMap();
    std::string name = std::string("faction_state_") + (t + 8);
    CHECK(h.shot(name));
    if (std::string_view(t) == "faction.economy" || std::string_view(t) == "faction.provinces" || std::string_view(t) == "faction.overview" ||
        std::string_view(t) == "faction.diplomacy") {
      scrollInspector(h, -12);
      CHECK(h.shot(name + "_bottom"));
    }
  }
}

TEST(app_faction_shots_guild) {
  Harness h("faction_shots_guild");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id g = findFaction(h->world(), "Альмарская торговая компания");
  CHECK(g != 0);
  for (const char* t : {"faction.overview", "faction.council", "faction.hqs", "faction.economy", "faction.diplomacy", "faction.heroes",
                        "faction.modifiers"}) {
    openTab(h, g, t);
    CHECK_EQ(h->ui.tabOf[app::SelType::Faction], std::string(t));
    h.waitMap();
    CHECK(h.shot(std::string("faction_guild_") + (t + 8)));
  }
  // Вкладки только для государств у гильдии не показываются, и наоборот.
  for (const app::TabDef& t : app::tabs()) {
    if (t.type != app::SelType::Faction || !t.visible) continue;
    if (std::string_view(t.id) == "faction.provinces" || std::string_view(t.id) == "faction.guilds") CHECK(!t.visible(h.a(), g));
    if (std::string_view(t.id) == "faction.hqs") CHECK(t.visible(h.a(), g));
  }
  Id st = findFaction(h->world(), "Королевство Альмарин");
  for (const app::TabDef& t : app::tabs())
    if (t.type == app::SelType::Faction && t.visible && std::string_view(t.id) == "faction.hqs") CHECK(!t.visible(h.a(), st));
}

TEST(app_faction_shots_drawers) {
  Harness h("faction_shots_drawers");
  h.demo();
  h.waitMap();
  h.dropToasts();
  h->openDrawer("states");   // каталог раздела
  h.settle();
  h.waitMap();
  CHECK(h->uiRect("states.add") != nullptr);
  CHECK(h.shot("faction_drawer_states"));
  h->openDrawer("guilds");
  h.settle();
  h.waitMap();
  CHECK(h->uiRect("guilds.add") != nullptr);
  CHECK(h.shot("faction_drawer_guilds"));
}

TEST(app_faction_shots_flag_editor) {
  Harness h("faction_shots_flag");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id st = findFaction(h->world(), "Империя Валь-Кетра");
  openTab(h, st, "faction.overview");
  CHECK(h.clickUi("faction.flag"));
  h.settle();
  CHECK(h->hasDialog("flag"));
  h.settle();
  CHECK(h.shot("faction_flag_editor"));
}

TEST(app_faction_shots_light) {
  Harness h("faction_shots_light", 1440, 900, 1, kAltScheme);
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id st = findFaction(h->world(), "Республика Корвен");
  openTab(h, st, "faction.economy");
  h->openDrawer("states");
  h.settle();
  h.waitMap();
  CHECK(h.shot("faction_light_economy"));
  openTab(h, st, "faction.diplomacy");
  h.waitMap();
  CHECK(h.shot("faction_light_diplomacy"));
  openTab(h, st, "faction.overview");
  CHECK(h.clickUi("faction.flag"));
  h.settle();
  CHECK(h.shot("faction_light_flag_editor"));
}

// Узкий инспектор и масштаб 125 %: ничего не налезает, строки переносятся.
TEST(app_faction_shots_scaled) {
  Harness h("faction_shots_scaled", 1600, 1000, 1.25f);
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id g = findFaction(h->world(), "Гильдия магов Аркана");
  openTab(h, g, "faction.hqs");
  h.waitMap();
  CHECK(h.shot("faction_scaled_hqs"));
  Id st = findFaction(h->world(), "Республика Корвен");
  openTab(h, st, "faction.council");
  h.waitMap();
  CHECK(h.shot("faction_scaled_council"));
}

// ---------------------------------------------------------------- снимки: экономика с обменом в обе стороны
// Ольсты получают золото по сделке и репарации; после новой дани в пользу Мирели у них в столбце «Торговля» золота
// и приход, и расход: проверка, что значения помещаются в узкий столбец таблицы ресурсов.
TEST(app_faction_shots_economy) {
  Harness h("audit_tzstates_shots");
  h.demo();
  h.waitMap();
  Id ols = findFaction(h->world(), "Вольные города Ольсты");
  Id mir = findFaction(h->world(), "Княжество Мирель");
  CHECK(ols && mir);
  CHECK(h->act("Дань", [&](Tx& tx) { rules::imposeTribute(tx, DealKind::Tribute, mir, ols, 1250, 4); }));
  openTab(h, ols, "faction.economy");
  shotClean(h, "audit_tzstates_economy_top");
  CHECK(ensureVisible(h, "economy.stock.1"));
  shotClean(h, "audit_tzstates_economy_resources");
  openTab(h, ols, "faction.trade");
  shotClean(h, "audit_tzstates_trade_tab");
  openTab(h, ols, "faction.diplomacy");
  shotClean(h, "audit_tzstates_diplomacy");
  openTab(h, ols, "faction.overview");
  shotClean(h, "audit_tzstates_overview");
  openTab(h, ols, "faction.tech");
  shotClean(h, "audit_tzstates_tech_tab");
}

// ---------------------------------------------------------------- снимки вкладок Альмарина (ТЗ 1.b.iv)
TEST(app_faction_shots_tabs) {
  Harness h("audit_tzstates_tabs");
  h.demo();
  h.waitMap();
  Id alm = findFaction(h->world(), "Королевство Альмарин");
  Id hel = findFaction(h->world(), "Северный союз Хельдвиг");
  CHECK(alm && hel);
  openTab(h, alm, "faction.council");
  shotClean(h, "audit_tzstates_tab_council");
  openTab(h, alm, "faction.provinces");
  shotClean(h, "audit_tzstates_tab_provinces");
  scrollInspector(h, -6);
  h.frames(24);
  shotClean(h, "audit_tzstates_tab_provinces2");
  openTab(h, alm, "faction.heroes");
  shotClean(h, "audit_tzstates_tab_heroes");
  openTab(h, alm, "faction.economy");
  shotClean(h, "audit_tzstates_tab_economy");
  openTab(h, alm, "faction.trade");
  shotClean(h, "audit_tzstates_tab_trade");

  // Редактор торговли: подарок золотом и поставка ресурса «каждый ход».
  app::trade::startDraft(alm, hel);
  {
    app::trade::DraftItem& g = app::trade::addItem(DealSide::A);
    g.res = kGold;
    g.amount = 250;
  }
  {
    Id res = 0;
    for (auto& [r, v] : h->world().faction(hel)->res)
      if (r != kGold && v > 0 && !res) res = r;
    app::trade::DraftItem& p = app::trade::addItem(DealSide::B);
    p.res = res ? res : kGold;
    p.amount = 15;
    p.mode = DealMode::PerTurn;
    p.turns = 6;
  }
  h->openEditor("trade", alm);
  h.settle();
  shotClean(h, "audit_tzstates_trade_editor");

  // Дерево технологий Альмарина.
  Id iron = test::techByName(h->world(), alm, "Обработка железа");
  app::openTechTree(h.a(), alm, iron);
  h.settle();
  shotClean(h, "audit_tzstates_techtree");
}
