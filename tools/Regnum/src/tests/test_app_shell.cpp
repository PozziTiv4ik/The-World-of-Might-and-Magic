// Regnum — сценарии оболочки: закреплённые строки и ленты без зазоров и наложений, панели справа от карты,
// страницы на месте карты (сущность, каталог, редакторы разделов), «Назад» и «К карте», параметры инструмента в
// верхней строке. Снимки — .wmma/regnum-tests/app_shell_*.png.
#include "tests/test_app_faction_util.h"

namespace rg {
namespace {

using namespace apptest;
using app::SelType;
using app::View;

bool near(float a, float b) { return std::fabs(a - b) < 0.75f; }

RectF rect(Harness& h, const char* name) {
  const RectF* r = h->uiRect(name);
  if (!r) test::fail(__FILE__, __LINE__, std::string("нет элемента ") + name);
  return r ? *r : RectF{};
}

Id firstFaction(const World& w, bool guild) {
  Id id = 0;
  w.factions.each([&](const Faction& f) {
    if (!id && f.isGuild() == guild) id = f.id;
  });
  return id;
}

Id firstLand(const World& w) {
  Id id = 0;
  w.provinces.each([&](const Province& p) {
    if (!id && !p.sea && p.owner) id = p.id;
  });
  return id;
}

}  // namespace

TEST(app_shell_docked_layout) {
  factest::HideTestRegs hide;
  Harness h("shell_layout");
  h.demo();
  h.dropToasts();
  // Строки и ленты прилегают к краям окна и друг к другу.
  RectF top = rect(h, "topbar"), bottom = rect(h, "bottombar"), tools = rect(h, "toolbar"), rail = rect(h, "rail"), map = h->mapArea();
  CHECK(near(top.x, 0) && near(top.y, 0) && near(top.w, 1440));
  CHECK(near(bottom.bottom(), 900) && near(bottom.w, 1440));
  CHECK(near(tools.x, 0) && near(tools.y, top.bottom()) && near(tools.bottom(), bottom.y));
  CHECK(near(rail.right(), 1440) && near(rail.y, top.bottom()) && near(rail.bottom(), bottom.y));
  CHECK(near(map.x, tools.right()) && near(map.right(), rail.x) && near(map.y, top.bottom()) && near(map.bottom(), bottom.y));
  CHECK(h.shot("shell_map"));

  // Провинция — панель справа от карты, вплотную к ленте разделов.
  Id pid = firstLand(h->world());
  CHECK(pid != 0);
  h->select(SelType::Province, pid);
  h.settle();
  RectF insp = rect(h, "inspector");
  map = h->mapArea();
  CHECK(near(insp.right(), rail.x) && near(map.right(), insp.x));
  CHECK(h->uiRect("inspector.expand") != nullptr);
  // Нижняя строка: название провинции правится на месте.
  CHECK(h->uiRect("quick.culture") != nullptr);
  CHECK(h.clickUi("quick.name"));
  h.retype("Алый Брод");
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(h->world().province(pid)->name, std::string("Алый Брод"));
  CHECK(h.shot("shell_province"));

  // Панель «Слои карты» — у ленты, панель выделения — левее неё.
  h.key(Key::L);
  h.settle();
  RectF dr = rect(h, "drawer");
  insp = rect(h, "inspector");
  CHECK(near(dr.right(), rail.x) && near(insp.right(), dr.x) && near(h->mapArea().right(), insp.x));
  CHECK(h->uiRect("minimap") != nullptr && h->uiRect("legend") != nullptr && h->uiRect("mode.3") != nullptr);
  CHECK(h.clickUi("mode.3"));
  CHECK(h->ui.mapMode == schema::MapMode::Contentment);
  CHECK(h.shot("shell_layers"));
  h.key(Key::D1);

  // Свернуть панель выделения — свойства в нижней строке, карта шире.
  CHECK(h.clickUi("bottombar.panel"));
  h.settle();
  CHECK(h->uiRect("inspector") == nullptr);
  CHECK(near(h->mapArea().right(), dr.x));
  CHECK(h.clickUi("bottombar.panel"));
  h.settle();
  CHECK(h->uiRect("inspector") != nullptr);
  h.key(Key::L);
  h.settle();
}

TEST(app_shell_pages) {
  factest::HideTestRegs hide;
  Harness h("shell_pages");
  h.demo();
  h.dropToasts();
  const World& w = h->world();
  Id pid = firstLand(w);
  Id owner = w.province(pid)->owner;
  h->select(SelType::Province, pid);
  h.settle();

  // Государство открывается страницей на месте карты: навигация слева, содержимое — до ленты разделов.
  CHECK(h.clickUi("province.ownerPill"));
  h.settle();
  CHECK(h->view() == View::Entity);
  CHECK(h->ui.sel == (app::Selection{SelType::Faction, owner}));
  RectF nav = rect(h, "pagenav"), page = rect(h, "page"), rail = rect(h, "rail");
  CHECK(near(nav.x, 0) && near(page.x, nav.right()) && near(page.right(), rail.x));
  CHECK(h->uiRect("toolbar") == nullptr);
  CHECK(h->uiRect("page.tab.faction.economy") != nullptr);
  CHECK(h->uiRect("section.states") != nullptr);
  CHECK(h.shot("shell_state"));
  CHECK(h.clickUi("page.tab.faction.economy"));
  h.settle();
  CHECK_EQ(h->ui.tabOf[SelType::Faction], std::string("faction.economy"));
  CHECK(h.shot("shell_state_economy"));
  CHECK(h.clickUi("page.tab.faction.army"));
  h.settle();
  CHECK(h.shot("shell_state_army"));

  // Каталог раздела, «Назад» к странице, «К карте» — к выделенной провинции.
  CHECK(h.clickUi("page.directory"));
  h.settle();
  CHECK(h->view() == View::Directory);
  CHECK(h.shot("shell_states_directory"));
  h.key(Key::Escape);
  h.settle();
  CHECK(h->view() == View::Entity);
  h.key(Key::Escape);
  h.settle();
  CHECK(h->view() == View::Map);
  CHECK(h->ui.sel == (app::Selection{SelType::Province, pid}));

  // Развернуть провинцию страницей и свернуть обратно.
  CHECK(h.clickUi("inspector.expand"));
  h.settle();
  CHECK(h->view() == View::Entity);
  CHECK(h.shot("shell_province_page"));
  CHECK(h.clickUi("inspector.collapse"));
  h.settle();
  CHECK(h->view() == View::Map);
  CHECK(h->ui.sel == (app::Selection{SelType::Province, pid}));

  // Разделы редакторов: экономика и справочники; навигация переключает редакторы группы.
  h->openSection("economy");
  h.settle();
  CHECK(h->view() == View::Editor);
  CHECK_EQ(h->ui.editor, std::string("trade"));
  CHECK(h.shot("shell_economy"));
  CHECK(h.clickUi("page.nav.buildings"));
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("buildings"));
  h->openSection("reference");
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("modifiers"));
  CHECK(h.shot("shell_reference"));
  h->openSection("economy");
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("buildings"));   // последний открытый редактор раздела
  h->openSection("economy");                          // повторно — к карте
  h.settle();
  CHECK(h->view() == View::Map);

  // Персонажи: каталог, страница персонажа.
  h->openSection("characters");
  h.settle();
  CHECK(h->view() == View::Directory);
  CHECK(h.shot("shell_characters"));
  Id cid = 0;
  w.characters.each([&](const Character& c) {
    if (!cid && !c.name.empty()) cid = c.id;
  });
  CHECK(cid != 0);
  h->select(SelType::Character, cid);
  h.settle();
  CHECK(h->view() == View::Entity);
  CHECK(h.shot("shell_character"));
  // Гильдия
  Id gid = firstFaction(w, true);
  if (gid) {
    h->select(SelType::Faction, gid);
    h.settle();
    CHECK(h.shot("shell_guild"));
  }
  h->toMap();
  h.settle();
  CHECK(h->view() == View::Map);
}

TEST(app_shell_tool_options) {
  factest::HideTestRegs hide;
  // Параметры инструмента — в середине верхней строки; в узком окне — строкой над картой.
  {
    Harness h("shell_tool_options");
    h.demo();
    h.dropToasts();
    h->setTool(app::ToolId::NewArmy);
    h.settle();
    RectF top = rect(h, "topbar"), opt = rect(h, "tool.options");
    CHECK(opt.y >= top.y && opt.bottom() <= top.bottom());
    CHECK(h.shot("shell_tool_options"));
    h->setTool(app::ToolId::Select);
  }
  {
    Harness h("shell_tool_options_narrow", 1024, 700);
    h.demo();
    h.dropToasts();
    h->setTool(app::ToolId::NewArmy);
    h.settle();
    RectF top = rect(h, "topbar"), opt = rect(h, "tool.options");
    CHECK(near(opt.y, top.bottom()));
    CHECK(h.shot("shell_tool_options_narrow"));
  }
}

TEST(app_shell_light_and_readonly) {
  factest::HideTestRegs hide;
  Harness h("shell_light", 1440, 900, 1, false);
  h.demo();
  h.dropToasts();
  Id pid = firstLand(h->world());
  h->select(SelType::Province, pid);
  h.settle();
  CHECK(h.shot("shell_light"));
  h->select(SelType::Faction, h->world().province(pid)->owner);
  h.settle();
  CHECK(h.shot("shell_light_state"));
}

}  // namespace rg
