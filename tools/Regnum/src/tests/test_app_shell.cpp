// Regnum — сценарии оболочки: закреплённые строки и ленты без зазоров и наложений, панели справа от карты,
// страницы на месте карты (сущность, каталог, редакторы разделов), «Назад» и «К карте», параметры инструмента в
// верхней строке, клавиши в текстовом поле не запускают команды карты, палитра команд (прокрутка колесом, покой без
// событий). Снимки — .voidlight/regnum-tests/app_shell_*.png.
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using app::SelType;
using app::View;

namespace {

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

// Отпечаток области последнего кадра (логические пиксели).
u64 regionHash(Harness& h, RectF r) {
  const platform::Frame& f = hl::lastFrame();
  u64 x = 1469598103934665603ull;
  for (int y = int(r.y * f.scale); y < int(r.bottom() * f.scale) && y < f.h; y++)
    for (int xx = int(r.x * f.scale); xx < int(r.right() * f.scale) && xx < f.w; xx++) {
      x ^= f.row(y)[xx];
      x *= 1099511628211ull;
    }
  (void)h;
  return x;
}

}  // namespace

TEST(app_shell_docked_layout) {
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
  Id pid = test::firstOwnedLand(h->world());
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
  Harness h("shell_pages");
  h.demo();
  h.dropToasts();
  const World& w = h->world();
  Id pid = test::firstOwnedLand(w);
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
  Harness h("shell_light", 1440, 900, 1, kAltScheme);
  h.demo();
  h.dropToasts();
  Id pid = test::firstOwnedLand(h->world());
  h->select(SelType::Province, pid);
  h.settle();
  CHECK(h.shot("shell_light"));
  h->select(SelType::Faction, h->world().province(pid)->owner);
  h.settle();
  CHECK(h.shot("shell_light_state"));
}

TEST(app_shell_schemes) {
  // У каждого цветокора — своя палитра карты (тот же id); по умолчанию карта в исходных цветах.
  for (int i = 0; i < ui::schemeCount(); i++)
    CHECK_EQ(std::string(map::paletteId(app::detail::schemePalette(i))), std::string(ui::schemeInfo(i).id));
  Harness h("shell_schemes");
  h.demo();
  h.dropToasts();
  CHECK(h->map().palette() == map::Palette::Source);
  Id pid = test::firstOwnedLand(h->world());
  for (int i = 0; i < ui::schemeCount(); i++) {
    const std::string id = ui::schemeInfo(i).id;
    h->setScheme(i);
    h->select(SelType::Province, pid);
    h.settle();
    h.waitMap();
    h.dropToasts();
    h.settle();
    CHECK(ui::theme().bg == ui::schemeTheme(i).bg);
    CHECK(h.shot("scheme_" + id + "_map"));
    h->select(SelType::Faction, h->world().province(pid)->owner);
    h.settle();
    CHECK(h.shot("scheme_" + id + "_page"));
    h->toMap();
    h.settle();
  }
  // Карта в цветах цветокора.
  h->setScheme(0);
  h->setSchemeMap(true);
  CHECK(h->map().palette() == map::Palette::Sapphire);
  h.settle();
  h.waitMap();
  h.settle();
  CHECK(h.shot("scheme_sapphire_map_colored"));
  h->setSchemeMap(false);
  CHECK(h->map().palette() == map::Palette::Source);
}

// ---------------------------------------------------------------- клавиши в текстовом поле не запускают команды карты
TEST(app_shell_textfield_keys_not_commands) {
  Harness h("audit_tzprov2_keys", 1440, 1180);
  h.demo();
  auto vp = h.visibleProvince();
  CHECK(vp.has_value());
  h->ui.tabOf[app::SelType::Province] = "province.overview";
  h.click(vp->second.x, vp->second.y);
  h.settle();
  CHECK(h->ui.sel.id == vp->first);
  const RectF* r = h->uiRect("province.capital");
  CHECK(r != nullptr);
  if (!r) return;
  h.click(r->cx(), r->cy());
  h.settle();
  CHECK(ui::wantsKeyboard());
  int mode0 = int(h->ui.mapMode);
  size_t n0 = h->world().provinces.size();
  h.key(Key::Delete);
  h.key(Key::E);
  h.key(Key::P);
  h.key(Key::M);
  h.key(Key::D2);
  h.settle();
  std::printf("  dialog %d editBorders %d provinces %zu -> %zu\n", int(h->hasDialog()), int(h->ui.editBorders), n0, size_t(h->world().provinces.size()));
  // Регрессия: клавиши без Ctrl в текстовом поле не запускают команды карты, инструменты и панели.
  CHECK(!h->hasDialog());
  CHECK(!h->ui.editBorders);
  CHECK(h->ui.tool == app::ToolId::Select);
  CHECK_EQ(h->world().provinces.size(), n0);
  CHECK(h->ui.drawer.empty());
  CHECK_EQ(int(h->ui.mapMode), mode0);
}

// Палитра команд (Ctrl+K): каждый кадр прокручивает список к подсвеченной строке (VirtualList::scrollToRow →
// scrollToItem с requestRedraw) — колесо мыши не листает результаты, а окно перерисовывается без остановки.
TEST(app_shell_palette_scroll_and_idle) {
  Harness h("audit_cr_palette");
  h.demo();
  h.dropToasts();
  h.settle();
  h.waitMap();
  h.settle();
  bool idleBefore = !h->animating();
  std::printf("  palette: animating before open = %d\n", int(!idleBefore));
  h.key(Key::K, ctrl());
  h.settle();
  CHECK(h->hasDialog("palette"));
  h.type("а");   // много провинций, персонажей и государств
  h.settle();
  const RectF* sr = h->uiRect("palette.search");
  CHECK(sr != nullptr);
  RectF list{sr->x, sr->bottom() + 8, sr->w, 360};
  h.move(list.cx(), list.y + 120);
  h.frames(10);
  u64 before = regionHash(h, list);
  CHECK(h.shot("audit_cr_palette_before"));
  (void)before;
  // Подсвеченная строка (золотая полоска слева) видна до прокрутки.
  auto accentRows = [&] {
    int n = 0;
    for (float y = list.y; y < list.bottom(); y += 1) {
      u32 p = h.pixel(list.x + 1, y);
      int r = int((p >> 16) & 255), g = int((p >> 8) & 255), b = int(p & 255);
      if (r > 190 && g > 130 && g < 190 && b < 110) n++;
    }
    return n;
  };
  int accBefore = accentRows();
  // Колесо вниз на 12 щелчков (≈ 12 строк) без движения мыши: подсвеченная строка должна уйти из вида.
  hl::wheel(list.cx(), list.y + 120, -12);
  h.step();
  h.frames(40);
  int accAfter = accentRows();
  CHECK(h.shot("audit_cr_palette_after_wheel"));
  std::printf("  palette: accent bar pixels before=%d after wheel=%d\n", accBefore, accAfter);
  CHECK(accBefore > 0);
  CHECK_MSG(!(accBefore > 0 && accAfter > 0), "колесо мыши не уводит список палитры от подсвеченной строки (scrollToRow каждый кадр)");
  // Простой: окно с палитрой не должно перерисовываться без событий. Мигание каретки в поле поиска длится 10 с
  // после ввода — пропускаем его, затем дожидаемся конца анимаций.
  hl::advance(11);
  h.settle();
  int busy = 0;
  for (int i = 0; i < 30; i++) {
    h.frame();
    if (h->animating()) busy++;
  }
  std::printf("  palette: busy frames while idle = %d/30\n", busy);
  CHECK_MSG(!idleBefore || busy == 0, "открытая палитра команд перерисовывает окно каждый кадр без событий (ui::requestRedraw в scrollToItem)");
}
