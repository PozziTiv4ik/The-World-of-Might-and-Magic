// Regnum — галерея экранов: только снимки для просмотра глазами (оформление, раскладка, цветокоры).
// Каждый экран снимается в четырёх видах, по тесту app_gallery_<экран>_<вид> на вид: цветокор по умолчанию 1440×900 (d),
// другой 1440×900 (l), малое окно 1100×700 (s), масштаб интерфейса 125 % на 1440×900 (z). Снимки:
// .wmma/regnum-tests/app_audit_dr_<вид>_<экран>.png; экраны войск и гильдий, стройки, базовой карты и штриховки
// оккупации (один вид) — app_audit_tzmil_*.png, app_audit_tbmt_more_*.png, app_audit_tzprov_*.png.
// Тесты ничего не проверяют, кроме того, что экраны открываются (сбоев нет). REGNUM_AUDIT_CFG=dl — только выбранные
// виды (тесты остальных ничего не делают). Запуск: build.sh test-app --test app_gallery
#include "app/editors/buildings.h"
#include "app/turn/history.h"
#include "tests/test_app_faction_util.h"
#include "tests/test_app_military_util.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;
namespace tr = rg::apptest::trees;

namespace {

struct Cfg {
  const char* tag;
  int w, h;
  float scale;
  int scheme;   // цветокор (ui::schemeInfo)
};
const Cfg kCfgs[] = {{"d", 1440, 900, 1.f, 0}, {"l", 1440, 900, 1.f, kAltScheme}, {"s", 1100, 700, 1.f, 0}, {"z", 1440, 900, 1.25f, 0}};

// Только выбранные виды (REGNUM_AUDIT_CFG=dl — основной и другой цветокор), по умолчанию все.
bool wanted(const Cfg& c) {
  const char* v = std::getenv("REGNUM_AUDIT_CFG");
  if (!v || !*v) return true;
  return std::string_view(v).find(c.tag) != std::string_view::npos;
}

struct Rig {
  Harness h;
  const Cfg& c;
  Rig(const Cfg& cfg, const std::string& name) : h("gallery_" + std::string(cfg.tag) + "_" + name, cfg.w, cfg.h, 1, cfg.scheme), c(cfg) {
    if (cfg.scale != 1.f) h->setUiScale(cfg.scale);
    h.settle();
  }
  void demo() {
    h.demo();
    h.waitMap();
    h.dropToasts();
    h.settle();
  }
  // Указатель — на край окна над морем: без подсказок и подсветки наведения на снимке.
  void park() {
    hl::mouseMove(3, float(c.h) * 0.5f);
    h.step();
  }
  void shot(const std::string& name) {
    std::fprintf(stderr, "[gallery] %s_%s\n", c.tag, name.c_str());
    park();
    h.settle();
    h.waitMap();
    h.frames(2);
    h.shot("audit_dr_" + std::string(c.tag) + "_" + name);
  }
  bool primary() const { return c.tag[0] == 'd'; }   // основной вид — снимков больше
};

// Экран name в виде tag (ничего, если вид не выбран REGNUM_AUDIT_CFG).
void shoot(char tag, const char* name, void (*body)(Rig&)) {
  for (const Cfg& c : kCfgs) {
    if (c.tag[0] != tag || !wanted(c)) continue;
    Rig r(c, name);
    body(r);
  }
}

Id firstProvinceOf(const World& w, Id owner) {
  Id r = 0;
  double best = -1;
  auto fs = geo::faces(w);
  w.provinces.each([&](const Province& p) {
    if (p.sea || p.owner != owner) return;
    const geo::ProvinceShape* sh = fs->shape(p.id);
    if (sh && sh->area > best) {
      best = sh->area;
      r = p.id;
    }
  });
  return r;
}

void openSel(Rig& r, app::SelType t, Id id, const char* tab) { apptest::openTab(r.h, t, id, tab); }

void tabsOf(Rig& r, app::SelType t, Id id, const std::string& prefix, bool bottoms) {
  std::vector<std::string> ids;
  for (const app::TabDef& d : app::tabs()) {
    if (d.type != t || !d.draw) continue;
    if (d.visible && !d.visible(r.h.a(), id)) continue;
    ids.push_back(d.id);
  }
  for (auto& tab : ids) {
    openSel(r, t, id, tab.c_str());
    std::string n = tab;
    for (char& ch : n)
      if (ch == '.') ch = '_';
    r.shot(prefix + "_" + n);
    if (bottoms) {
      scrollInspector(r.h, -40);
      r.shot(prefix + "_" + n + "_end");
    }
  }
}

// Экраны галереи: тело теста для одного вида.
namespace screen {

// ---------------------------------------------------------------- экран запуска
void start(Rig& r) { r.shot("start"); }

// ---------------------------------------------------------------- редактор: режимы карты
void modes(Rig& r) {
  r.demo();
  for (int i = 0; i < int(schema::MapMode::Count); i++) {
    r.h->setMapMode(schema::MapMode(i));
    r.shot("mode" + std::to_string(i + 1));
    if (!r.primary()) break;   // в остальных видах — только политическая
  }
  r.h->setMapMode(schema::MapMode::Political);
  // Крупный и мелкий масштаб: подписи и фигурки.
  Id alm = findFaction(r.h->world(), "Королевство Альмарин");
  Id pid = firstProvinceOf(r.h->world(), alm);
  if (auto fs = geo::faces(r.h->world()); fs && fs->shape(pid)) {
    showAt(r.h, fs->shape(pid)->label, 1.6);
    r.shot("zoom_in");
    showAt(r.h, fs->shape(pid)->label, 0.25);
    r.shot("zoom_out");
  }
}

// ---------------------------------------------------------------- инспектор провинции
void province(Rig& r) {
  r.demo();
  Id alm = findFaction(r.h->world(), "Королевство Альмарин");
  Id pid = firstProvinceOf(r.h->world(), alm);
  tabsOf(r, app::SelType::Province, pid, "prov", r.primary() || r.c.tag[0] == 's');
  Id sea = test::idWhere(r.h->world().provinces, [](const Province& p) { return p.sea; });
  if (sea) {
    r.h->select(app::SelType::Province, sea);
    r.shot("prov_sea");
  }
}

// ---------------------------------------------------------------- инспектор государства и гильдии
void faction(Rig& r) {
  r.demo();
  Id st = findFaction(r.h->world(), "Королевство Альмарин");
  tabsOf(r, app::SelType::Faction, st, "state", r.primary());
  Id g = findFaction(r.h->world(), "Альмарская торговая компания");
  tabsOf(r, app::SelType::Faction, g, "guild", false);
}

// ---------------------------------------------------------------- войско, флот, маршрут, персонаж
void objects(Rig& r) {
  r.demo();
  const World& w = r.h->world();
  Id hel = factionByName(w, "Хельдвиг");
  Id army = armyOf(w, hel, ArmyKind::Army);
  Id fleet = 0;
  w.factions.each([&](const Faction& f) {
    if (!fleet) fleet = armyOf(w, f.id, ArmyKind::Fleet);
  });
  if (army) {
    tabsOf(r, app::SelType::Army, army, "army", r.primary());
    r.h->focusSelection();
    r.shot("army_focus");
  }
  if (fleet) tabsOf(r, app::SelType::Army, fleet, "fleet", false);
  Id rt = test::idWhere(r.h->world().routes, [](const Route&) { return true; });
  if (rt) tabsOf(r, app::SelType::Route, rt, "route", false);
  Id ch = test::idWhere(r.h->world().characters, [](const Character&) { return true; });
  if (ch) tabsOf(r, app::SelType::Character, ch, "char", false);
  // Союзное войско: Альмарин + Хельдвиг.
  Id alm = factionByName(r.h->world(), "Альмарин");
  if (army && alm) {
    Vec2 pos = r.h->world().army(army)->pos;
    auto spot = rules::findFreeSpot(r.h->world(), ArmyKind::Army, pos + Vec2(-160, 20));
    if (spot) {
      Id nid = 0;
      Id row = r.h->world().faction(alm)->army[0].id;
      r.h->act("Союзное войско (галерея)", [&](Tx& tx) {
        nid = rules::createArmy(tx, ArmyKind::Army, alm, *spot);
        rules::setUnits(tx, nid, alm, row, 400);
        rules::formAllied(tx, army, nid);
      });
      tabsOf(r, app::SelType::Army, army, "allied", false);
    }
  }
}

// ---------------------------------------------------------------- панели и разделы правой ленты
void drawers(Rig& r) {
  r.demo();
  for (const app::DrawerDef& d : app::drawers()) {
    r.h->openDrawer(d.id);
    r.h.settle();
    r.shot(std::string("drawer_") + d.id);
    r.h->openDrawer(d.id);   // закрыть
    r.h.settle();
  }
  for (const app::SectionDef& s : app::sections()) {
    r.h->openSection(s.id);
    r.h.settle();
    r.shot(std::string("section_") + s.id);
    r.h->toMap();
    r.h.settle();
  }
  // Выдвижная панель и инспектор одновременно (самая тесная раскладка).
  Id st = findFaction(r.h->world(), "Королевство Альмарин");
  r.h->openDrawer("provinces");
  openSel(r, app::SelType::Faction, st, "faction.overview");
  r.shot("drawer_and_inspector");
}

// ---------------------------------------------------------------- полноэкранные редакторы
void editors(Rig& r) {
  r.demo();
  Id st = findFaction(r.h->world(), "Королевство Альмарин");
  for (const char* e : {"techtree", "buildings", "military", "trade", "modifiers", "catalogs"}) {
    r.h->openEditor(e, st);
    r.h.settle();
    r.shot(std::string("editor_") + e);
    r.h->closeEditor();
    r.h.settle();
  }
}

// ---------------------------------------------------------------- диалоги
void dialogs(Rig& r) {
  r.demo();
  auto& h = r.h;
  const World& w0 = h->world();
  Id alm = factionByName(w0, "Альмарин");
  Id hel = factionByName(w0, "Хельдвиг");
  Id vk = factionByName(w0, "Валь-Кетра");
  Id pid = firstProvinceOf(w0, alm);
  auto close = [&] {
    h->closeDialogs();
    h.settle();
    h.dropToasts();
  };
  // Палитра, справка, настройки (все вкладки).
  h->showPalette();
  r.shot("dlg_palette");
  h.type("пров");
  r.shot("dlg_palette_query");
  close();
  h->showHelp();
  r.shot("dlg_help");
  close();
  h->showSettings();
  r.shot("dlg_settings");
  if (const RectF* t = h->uiRect("settings.tabs")) {
    for (int i = 1; i < 4; i++) {
      h.click(t->x + t->w * (float(i) + 0.5f) / 4, t->cy());
      r.shot("dlg_settings_tab" + std::to_string(i));
    }
  }
  close();
  // Файлы.
  h->openWorldDialog();
  r.shot("dlg_browser");
  close();
  h->newWorldDialog();
  r.shot("dlg_newworld");
  close();
  h->saveAs();
  r.shot("dlg_saveas");
  close();
  // Подтверждение, запрос, сообщение.
  h->confirm("Удалить провинцию?", "Земли отойдут соседям. Действие можно отменить Ctrl+Z.", "Удалить", true, [](app::App&) {});
  r.shot("dlg_confirm");
  close();
  h->prompt("Новое государство", "Название", "Королевство", [](app::App&, const std::string&) {});
  r.shot("dlg_prompt");
  close();
  h->message("Мир прочитан с предупреждениями", "Некоторые записи пропущены:", {"Провинция 12: неверный владелец", "Маршрут 4: нет точек"},
             app::ToastKind::Warning);
  r.shot("dlg_message");
  close();
  // Флаг, дань, выбор постройки, запись хроники.
  h->openDialog("flag", alm);
  r.shot("dlg_flag");
  close();
  h->openDialog("tribute", alm);
  r.shot("dlg_tribute");
  close();
  h->openDialog("build.picker", pid);
  r.shot("dlg_build_picker");
  close();
  h->openDialog("chronicle.note");
  r.shot("dlg_note");
  close();
  // Войска: битва, встречи, разделение.
  const World& w = h->world();
  Id ha = armyOf(w, hel, ArmyKind::Army);
  Id va = armyOf(w, vk, ArmyKind::Army);
  if (ha && va) {
    app::mil::openBattle(h.a(), ha, va, w.army(ha)->pos);
    r.shot("dlg_battle");
    close();
  }
  Id aa = armyOf(h->world(), alm, ArmyKind::Army);
  if (aa && ha) {
    rules::Encounter e = rules::encounter(h->world(), aa, h->world().army(ha)->pos);
    if (e.type != rules::EncounterType::None) {
      app::mil::openEncounter(h.a(), aa, ha, e, h->world().army(aa)->pos, [](app::App&, bool) {});
      r.shot("dlg_encounter_ally");
      close();
    }
  }
  if (aa && va) {
    rules::Encounter e = rules::encounter(h->world(), aa, h->world().army(va)->pos);
    if (e.type != rules::EncounterType::None) {
      app::mil::openEncounter(h.a(), aa, va, e, h->world().army(aa)->pos, [](app::App&, bool) {});
      r.shot("dlg_encounter_war");
      close();
    }
  }
  if (aa) {
    // Второе войско той же фракции рядом — объединение.
    auto spot = rules::findFreeSpot(h->world(), ArmyKind::Army, h->world().army(aa)->pos + Vec2(150, 0));
    if (spot) {
      Id nid = 0;
      Id row = h->world().faction(alm)->army[0].id;
      h->act("Второе войско (галерея)", [&](Tx& tx) {
        nid = rules::createArmy(tx, ArmyKind::Army, alm, *spot);
        rules::setUnits(tx, nid, alm, row, 100);
      });
      rules::Encounter e = rules::encounter(h->world(), nid, h->world().army(aa)->pos);
      if (e.type != rules::EncounterType::None) {
        app::mil::openEncounter(h.a(), nid, aa, e, *spot, [](app::App&, bool) {});
        r.shot("dlg_encounter_merge");
        close();
      }
    }
    app::mil::openSplit(h.a(), aa);
    r.shot("dlg_split");
    close();
  }
  // Ход: подтверждение, отчёт, история, просмотр прошлого хода.
  h->endTurn();
  r.shot("dlg_turn_confirm");
  if (const RectF* t = h->uiRect("turn.confirm.tabs")) {
    for (int i = 1; i < 3; i++) {
      h.click(t->x + t->w * (float(i) + 0.5f) / 3, t->cy());
      r.shot("dlg_turn_confirm_tab" + std::to_string(i));
    }
  }
  close();
  h->endTurnNow();
  h.settle();
  r.shot("after_turn_toast");
  h->openDialog("turn.report");
  r.shot("dlg_turn_report");
  close();
  h->openDialog("turn.history");
  r.shot("dlg_history");
  close();
  if (h->viewTurn(1)) {
    h.settle();
    h.dropToasts();
    r.shot("readonly_view");
    h->select(app::SelType::Province, pid);
    r.shot("readonly_province");
    h->backToCurrent();
    h.settle();
  }
  // Несохранённые изменения при закрытии мира.
  h->closeWorld();
  r.shot("dlg_unsaved");
  close();
}

// ---------------------------------------------------------------- инструменты карты
void tools(Rig& r) {
  r.demo();
  auto& h = r.h;
  Id alm = findFaction(h->world(), "Королевство Альмарин");
  Id pid = firstProvinceOf(h->world(), alm);
  auto fs = geo::faces(h->world());
  Vec2 lab = fs->shape(pid)->label;
  h->select(app::SelType::Province, pid);
  showAt(h, lab, 0.9);
  h->setEditBorders(true);
  h.settle();
  r.shot("tool_borders_on");
  h->setTool(app::ToolId::EditBorders);
  r.shot("tool_edit_borders");
  gfx::Pt s = screenOf(h, lab);
  h->setTool(app::ToolId::NewProvince);
  h.click(s.x - 60, s.y - 40);
  h.click(s.x + 40, s.y - 50);
  h.click(s.x + 30, s.y + 40);
  r.shot("tool_new_province");
  h.key(Key::Escape);
  h->setTool(app::ToolId::Knife);
  h.click(s.x - 80, s.y);
  h.move(s.x + 80, s.y + 10);
  r.shot("tool_knife");
  h.key(Key::Escape);
  h->setTool(app::ToolId::Merge);
  h.move(s.x + 140, s.y);
  r.shot("tool_merge");
  h->setTool(app::ToolId::Fill);
  r.shot("tool_fill");
  h->setTool(app::ToolId::AddArea);
  r.shot("tool_add_area");
  h->setEditBorders(false);
  h.settle();
  h->setTool(app::ToolId::Route);
  h.click(s.x, s.y);
  h.move(s.x + 120, s.y + 60);
  r.shot("tool_route");
  h.key(Key::Escape);
  h->setTool(app::ToolId::NewArmy);
  h.move(s.x + 30, s.y + 30);
  r.shot("tool_new_army");
  h->setTool(app::ToolId::NewFleet);
  r.shot("tool_new_fleet");
  h->setTool(app::ToolId::Select);
  // Контекстное меню на карте и уведомление.
  h.click(s.x, s.y, platform::MouseRight);
  r.shot("map_context_menu");
  h.key(Key::Escape);
  h->toast("Мир сохранён", app::ToastKind::Success);
  h->toast("Не удалось открыть архив: файл повреждён", app::ToastKind::Danger, "", "Подробнее", [](app::App&) {});
  h.frames(20);
  h.shot(std::string("audit_dr_") + r.c.tag + "_toasts");
}

}  // namespace screen

}  // namespace

TEST(app_gallery_start_d) { shoot('d', "start", screen::start); }
TEST(app_gallery_start_l) { shoot('l', "start", screen::start); }
TEST(app_gallery_start_s) { shoot('s', "start", screen::start); }
TEST(app_gallery_start_z) { shoot('z', "start", screen::start); }

TEST(app_gallery_modes_d) { shoot('d', "modes", screen::modes); }
TEST(app_gallery_modes_l) { shoot('l', "modes", screen::modes); }
TEST(app_gallery_modes_s) { shoot('s', "modes", screen::modes); }
TEST(app_gallery_modes_z) { shoot('z', "modes", screen::modes); }

TEST(app_gallery_province_d) { shoot('d', "province", screen::province); }
TEST(app_gallery_province_l) { shoot('l', "province", screen::province); }
TEST(app_gallery_province_s) { shoot('s', "province", screen::province); }
TEST(app_gallery_province_z) { shoot('z', "province", screen::province); }

TEST(app_gallery_faction_d) { shoot('d', "faction", screen::faction); }
TEST(app_gallery_faction_l) { shoot('l', "faction", screen::faction); }
TEST(app_gallery_faction_s) { shoot('s', "faction", screen::faction); }
TEST(app_gallery_faction_z) { shoot('z', "faction", screen::faction); }

TEST(app_gallery_objects_d) { shoot('d', "objects", screen::objects); }
TEST(app_gallery_objects_l) { shoot('l', "objects", screen::objects); }
TEST(app_gallery_objects_s) { shoot('s', "objects", screen::objects); }
TEST(app_gallery_objects_z) { shoot('z', "objects", screen::objects); }

TEST(app_gallery_drawers_d) { shoot('d', "drawers", screen::drawers); }
TEST(app_gallery_drawers_l) { shoot('l', "drawers", screen::drawers); }
TEST(app_gallery_drawers_s) { shoot('s', "drawers", screen::drawers); }
TEST(app_gallery_drawers_z) { shoot('z', "drawers", screen::drawers); }

TEST(app_gallery_editors_d) { shoot('d', "editors", screen::editors); }
TEST(app_gallery_editors_l) { shoot('l', "editors", screen::editors); }
TEST(app_gallery_editors_s) { shoot('s', "editors", screen::editors); }
TEST(app_gallery_editors_z) { shoot('z', "editors", screen::editors); }

TEST(app_gallery_dialogs_d) { shoot('d', "dialogs", screen::dialogs); }
TEST(app_gallery_dialogs_l) { shoot('l', "dialogs", screen::dialogs); }
TEST(app_gallery_dialogs_s) { shoot('s', "dialogs", screen::dialogs); }
TEST(app_gallery_dialogs_z) { shoot('z', "dialogs", screen::dialogs); }

TEST(app_gallery_tools_d) { shoot('d', "tools", screen::tools); }
TEST(app_gallery_tools_l) { shoot('l', "tools", screen::tools); }
TEST(app_gallery_tools_s) { shoot('s', "tools", screen::tools); }
TEST(app_gallery_tools_z) { shoot('z', "tools", screen::tools); }

// ---------------------------------------------------------------- войска, флот и торговые гильдии (один вид)
namespace {

void showTab(Harness& h, app::SelType t, Id id, const char* tabId) {
  h->ui.tabOf[t] = tabId;
  h->select(t, id);
  h.settle();
  h.dropToasts();
  h.settle();
}

}  // namespace

// Вкладки фракции: войска, флот, полноэкранные таблицы; гильдия: обзор, штабы, войска; вкладка «Гильдии» государства.
TEST(app_gallery_military_faction) {
  Harness h("gallery_military_faction");
  h.demo();
  h.waitMap();
  const World& w = h->world();
  Id hel = factionByName(w, "Хельдвиг");
  Id alm = factionByName(w, "Альмарин");
  Id amber = factionByName(w, "Янтарная");
  Id comp = factionByName(w, "Альмарская");
  CHECK(hel && alm && amber && comp);
  showTab(h, app::SelType::Faction, hel, "faction.army");
  CHECK(h.shot("audit_tzmil_faction_army"));
  showTab(h, app::SelType::Faction, hel, "faction.fleet");
  CHECK(h.shot("audit_tzmil_faction_fleet"));
  h->openEditor("military", hel);
  h.settle();
  CHECK(h.shot("audit_tzmil_military_editor"));
  h->closeEditor();
  h.settle();
  showTab(h, app::SelType::Faction, alm, "faction.guilds");
  CHECK(h.shot("audit_tzmil_state_guilds"));
  showTab(h, app::SelType::Faction, comp, "faction.overview");
  CHECK(h.shot("audit_tzmil_stateguild_overview"));
  showTab(h, app::SelType::Faction, amber, "faction.overview");
  CHECK(h.shot("audit_tzmil_guild_overview"));
  showTab(h, app::SelType::Faction, amber, "faction.hqs");
  CHECK(h.shot("audit_tzmil_guild_hqs"));
  showTab(h, app::SelType::Faction, amber, "faction.army");
  CHECK(h.shot("audit_tzmil_guild_army"));
  showTab(h, app::SelType::Faction, amber, "faction.diplomacy");
  CHECK(h.shot("audit_tzmil_guild_diplomacy"));
  showTab(h, app::SelType::Faction, amber, "faction.economy");
  CHECK(h.shot("audit_tzmil_guild_economy"));
  showTab(h, app::SelType::Faction, amber, "faction.trade");
  CHECK(h.shot("audit_tzmil_guild_trade"));
}

// Инспектор войска (обычное и союзное), разделение, гильдейский режим карты.
TEST(app_gallery_military_army) {
  Harness h("gallery_military_army");
  h.demo();
  h.waitMap();
  const World& w = h->world();
  Id hel = factionByName(w, "Хельдвиг");
  Id x = armyOf(w, hel, ArmyKind::Army);
  CHECK(x != 0);
  showTab(h, app::SelType::Army, x, "army.units");
  showAt(h, w.army(x)->pos, 0.6);
  h.dropToasts();
  CHECK(h.shot("audit_tzmil_army_inspector"));
  // Союзное войско демонстрационного мира.
  Id allied = 0;
  h->world().armies.each([&](const Army& a) {
    if (!allied && a.allied() && !a.isFleet()) allied = a.id;
  });
  CHECK(allied != 0);
  showTab(h, app::SelType::Army, allied, "army.units");
  showAt(h, h->world().army(allied)->pos, 0.6);
  h.dropToasts();
  CHECK(h.shot("audit_tzmil_allied_inspector"));
  // Разделение союзного войска.
  CHECK(h.clickUi("army.split"));
  h.settle();
  CHECK(h->hasDialog("army.split"));
  CHECK(h.shot("audit_tzmil_split_dialog"));
  h.key(Key::Escape);
  h.settle();
  // Режим гильдий: весь мир и крупно.
  h->clearSelection();
  h->setMapMode(schema::MapMode::Guilds);
  h.settle();
  h.waitMap();
  h.dropToasts();
  CHECK(h.shot("audit_tzmil_guild_mode_default"));
  Box2 all = geo::faces(h->world())->bounds;
  h->focusMap(all);
  h.settle();
  h.waitMap();
  h.dropToasts();
  CHECK(h.shot("audit_tzmil_guild_mode_world"));
  Id alm = factionByName(h->world(), "Альмарин");
  Id cap = h->world().faction(alm)->capital;
  if (const auto fs = geo::faces(h->world()); fs && fs->shape(cap)) {
    showAt(h, fs->shape(cap)->label, 0.5);
    h.dropToasts();
    CHECK(h.shot("audit_tzmil_guild_mode_zoom"));
  }
}

// Снимки экранов зоны аудита — для визуальной проверки (сами по себе не проверяют ничего).
TEST(app_gallery_construction) {
  Harness h("audit_tbmt_more_shots", 1440, 900);
  h.demo();
  tr::BuildPick pk = tr::pickBuild(h->world());
  CHECK(pk.pid != 0);
  tr::setStock(h.a(), pk.owner, 5);   // почти пусто: часть построек недоступна
  h->ui.tabOf[app::SelType::Province] = "province.buildings";
  h->select(app::SelType::Province, pk.pid);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tbmt_more_buildings_tab"));
  CHECK(h.clickUi("prov.build"));
  h.settle();
  CHECK(h->hasDialog("build.picker"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tbmt_more_picker_poor"));
  h->closeDialogs();
  h.settle();
  // Модификаторы: окно с выбранным модификатором дипломатии (если есть).
  Id dip = 0, any = 0;
  h->world().modifiers.each([&](const Modifier& m) {
    if (!any) any = m.id;
    if (!dip && m.has(Fx::DiplomacyPerTurn)) dip = m.id;
  });
  h->openEditor("modifiers", dip ? dip : any);
  h.dropToasts();
  h.settle();
  h.settle();
  CHECK(h.shot("audit_tbmt_more_modifiers"));
  h->closeEditor();
  h.settle();
  // Общее дерево построек.
  app::openBuildingTree(h.a(), 0);
  h.dropToasts();
  h.settle();
  h.settle();
  CHECK(h.shot("audit_tbmt_more_common_tree"));
  h->closeEditor();
  h.settle();
  // Ход: подтверждение, отчёт, история.
  h->endTurn();
  h.dropToasts();
  h.settle();
  CHECK(h->hasDialog("turn.confirm"));
  CHECK(h.shot("audit_tbmt_more_turn_confirm"));
  h->closeDialogs();
  h.settle();
  CHECK(h->endTurnNow());
  CHECK(h->endTurnNow());
  h->toasts().clear();
  h->openDialog("turn.report");
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tbmt_more_turn_report"));
  h->closeDialogs();
  h->openDialog("turn.history");
  h.dropToasts();
  h.settle();
  h.settle();
  CHECK(h.shot("audit_tbmt_more_history"));
  h->closeDialogs();
  h.settle();
  CHECK(h->viewTurn(1));
  h.dropToasts();
  h.settle();
  h.settle();
  CHECK(h.shot("audit_tbmt_more_view_past"));
}

// ---------------------------------------------------------------- базовая карта: снимки пустого мира на разных масштабах
TEST(app_gallery_basemap_zoom) {
  Harness h("audit_tzprov_basemap", 1600, 900, 1);
  CHECK(h->newWorld("Пустой мир"));
  h.settle();
  h.dropToasts();
  // Без панелей справа от карты.
  h->ui.drawer.clear();
  struct Z {
    const char* name;
    double zoom;
  } zs[] = {{"fit", 0}, {"z050", 0.5}, {"z100", 1.0}, {"z300", 3.0}};
  for (auto& z : zs) {
    if (z.zoom == 0) h->map().fitAll(false);
    else h->map().centerOn({3000, 1000}, z.zoom, false);
    h.settle();
    h.waitMap();
    h.dropToasts();
    h.frames(3);
    const map::View& v = h->map().view();
    RectF ma = h->mapArea();
    std::printf("  shot %s: zoom %.4f center %.2f %.2f viewport %.1f %.1f %.1f %.1f mapArea %.1f %.1f %.1f %.1f\n", z.name, v.zoom, v.cx, v.cy,
                v.viewport.x, v.viewport.y, v.viewport.w, v.viewport.h, ma.x, ma.y, ma.w, ma.h);
    CHECK(h.shot(std::string("audit_tzprov_basemap_") + z.name));
  }
}

// ---------------------------------------------------------------- ТЗ 1.a.ix: штриховка оккупации поверх рек и озёр
TEST(app_gallery_occupation_hatch) {
  Harness h("audit_tzprov_hatch", 1440, 900, 1);
  h.demo();
  h->ui.drawer.clear();
  const World& w = h->world();
  Id occ = 0;
  w.provinces.each([&](const Province& p) {
    if (!occ && p.occupied && p.name == "Фьорн") occ = p.id;
  });
  if (!occ)
    w.provinces.each([&](const Province& p) {
      if (!occ && p.occupied) occ = p.id;
    });
  CHECK(occ != 0);
  auto fs = geo::faces(w);
  const geo::ProvinceShape* sh = fs->shape(occ);
  CHECK(sh != nullptr);
  h->map().centerOn(sh->label, 1.0, false);
  h.settle();
  h.waitMap();
  h.dropToasts();
  h.frames(3);
  const map::View& v = h->map().view();
  std::printf("  hatch shot: zoom %.3f center %.1f %.1f viewport %.1f %.1f %.1f %.1f\n", v.zoom, v.cx, v.cy, v.viewport.x, v.viewport.y, v.viewport.w,
              v.viewport.h);
  CHECK(h.shot("audit_tzprov_hatch_1to1"));
}
