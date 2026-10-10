// Сценарии археологии на демонстрационном мире (ТЗ «Доработки №2»): вкладка провинции «Археология» (слоты 2 × 2 в
// панели и в ряд на странице, режим правки, тайник реликвий без хроники, переход к археологии государства), вкладка
// государства «Археология» (формирование групп, «Найти археологическое место», «Назначить археологическую группу»,
// «Проводить раскопки», окна итога, окно спасения группы, войско без государства «Показать на карте»), справочники
// «Археологические места» и «Сундуки сокровищ», «Без государства» вне списков государств и итогов хода. Снимки экранов.
#include "app/dialogs/turn_ui.h"
#include "app/panels/arch_common.h"
#include "core/arch.h"
#include "core/content.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;
namespace archui = rg::app::archui;

namespace {

Id siteKey(const World& w, const char* key) {
  const ArchSite* s = w.catalogs->archSiteByKey(key);
  return s ? s->id : 0;
}

// Первая (по ID) сухопутная провинция государства.
Id provinceOf(const World& w, Id state, Id skip = 0) {
  Id r = 0;
  w.provinces.each([&](const Province& p) {
    if (!r && p.owner == state && !p.sea && p.id != skip) r = p.id;
  });
  return r;
}

// Слоты провинции: руины (найдены, этап 1 из 3), склеп (исследован полностью), долина и гробница — не найдены.
void setupSlots(Tx& tx, Id pid) {
  const char* sites[kArchSlots] = {arch::site::Ruins, arch::site::Crypt, arch::site::Valley, arch::site::Tomb};
  for (int i = 0; i < kArchSlots; i++) {
    ArchSlot& s = tx.province(pid).arch[size_t(i)];
    s = ArchSlot{};
    s.site = siteKey(tx.w(), sites[i]);
    s.chest = tx.w().catalogs->archSite(s.site)->rewards[size_t(i)].front();
  }
  tx.province(pid).arch[0].open = true;
  tx.province(pid).arch[0].stage = 1;
  tx.province(pid).arch[1].open = true;
  tx.province(pid).arch[1].stage = arch::stagesOf(1);
}

// «Гильдия Археологов» государства достроена (в провинции pid, если её ещё нет); казна — на группы.
void setupGuild(Tx& tx, Id state, Id pid) {
  tx.faction(state).res[kGold] = std::max(tx.w().faction(state)->treasury(), 1000.0);
  if (rules::builtWithKey(tx.w(), state, schema::bld::ArchGuild) > 0) return;
  Id guild = 0;
  tx.w().buildings.each([&](const Building& b) {
    if (!guild && b.owner == state && b.key == schema::bld::ArchGuild) guild = b.id;
  });
  if (!guild) guild = tx.add(content::archGuildFor(state, {})).id;
  tx.province(pid).size = ProvSize::Large;
  tx.province(pid).city = CityType::City;
  rules::placeBuilding(tx, pid, guild, 1);
}

// Открыть меню (кнопку) и выбрать в нём группу.
bool pickGroup(Harness& h, const std::string& opener, Id group) {
  if (!h.clickUi(opener)) return false;
  h.settle();
  if (!h.clickUi("arch.menu.group." + std::to_string(group))) return false;
  h.settle();
  return true;
}

bool editMode(Harness& h) { return h->uiRect("arch.slotSite.0") != nullptr; }

// Кнопка пустого состояния (ui::emptyState): внизу его области.
bool clickEmptyAction(Harness& h, const std::string& name) {
  const RectF* r = h->uiRect(name);
  if (!r) return false;
  h.click(r->cx(), r->bottom() - 31);
  h.settle();
  return true;
}

// Выбор в выпадающем списке с поиском вне инспектора: открыть, набрать, Enter.
bool comboPick(Harness& h, const std::string& name, const std::string& text) {
  if (!h.clickUi(name)) return false;
  h.type(text);
  h.key(Key::Enter);
  h.settle();
  return true;
}

}  // namespace

// ---------------------------------------------------------------- вкладка провинции
TEST(app_arch_province_tab) {
  HideTestRegs regs;
  Harness h("arch_province");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  CHECK(st != 0);
  const Id pid = provinceOf(h->world(), st);
  CHECK(pid != 0);
  Id hero = 0, relic = 0;
  CHECK(h->act("test", [&](Tx& tx) {
    setupSlots(tx, pid);
    hero = rules::createCharacter(tx, st, "Хранитель древностей");
    tx.character(hero).hero = true;
    relic = rules::addRelic(tx, "Корона Шантири", Rarity::Legendary);
    rules::giveRelic(tx, hero, relic);
  }));
  h->ui.tabOf[app::SelType::Province] = archui::kTabProvince;
  h->select(app::SelType::Province, pid, true);
  h.settle();
  h.waitMap();
  h.dropToasts();
  if (editMode(h)) {
    CHECK(h.clickUi("arch.edit"));
    h.settle();
  }
  // Узкая панель: слоты 2 × 2, сокровища скрыты.
  const RectF* s0 = h->uiRect("arch.slot.0");
  const RectF* s2 = h->uiRect("arch.slot.2");
  CHECK(s0 && s2 && h->uiRect("arch.treasure.3"));
  CHECK(s2->y > s0->bottom());
  CHECK(h->uiRect("arch.state") != nullptr);
  CHECK(h->uiRect("arch.unearth") == nullptr);
  CHECK(h.shot("arch_province_narrow"));

  // Режим правки: управление слотами и поле «Спрятанная реликвия».
  CHECK(h.clickUi("arch.edit"));
  h.settle();
  CHECK(editMode(h) && h->uiRect("arch.hidden"));
  CHECK(h.shot("arch_province_edit"));
  CHECK(h.clickUi("arch.slotOpen.1"));   // закрыть исследованный слот — этапы сброшены
  h.settle();
  CHECK(!h->world().province(pid)->arch[1].open && h->world().province(pid)->arch[1].stage == 0);
  h->undo();
  h.settle();
  CHECK(h->world().province(pid)->arch[1].open && h->world().province(pid)->arch[1].stage == arch::stagesOf(1));
  CHECK(pickInCombo(h, "arch.slotSite.3", "Подземный лабиринт"));
  h.settle();
  CHECK_EQ(h->world().province(pid)->arch[3].site, siteKey(h->world(), arch::site::Labyrinth));
  {
    const ArchSlot s3 = h->world().province(pid)->arch[3];
    const auto& rw = h->world().catalogs->archSite(s3.site)->rewards[3];
    CHECK(std::find(rw.begin(), rw.end(), s3.chest) != rw.end());
  }

  // Спрятать реликвию героя: без записей в хронике; откопать может только спрятавшее государство.
  const u32 logs = h->world().log.size();
  CHECK(h.clickUi("arch.hide"));
  h.settle();
  CHECK(h.clickUi("arch.hide.relic." + std::to_string(relic)));
  h.settle();
  CHECK_EQ(h->world().province(pid)->hiddenRelic, relic);
  CHECK_EQ(h->world().log.size(), logs);
  CHECK(h->uiRect("arch.unearth") != nullptr);
  CHECK(h.shot("arch_province_hidden"));
  CHECK(h.clickUi("arch.unearth"));
  h.settle();
  CHECK(h.clickUi("arch.unearth.hero." + std::to_string(hero)));
  h.settle();
  CHECK_EQ(rules::relicHolder(h->world(), relic), hero);
  CHECK_EQ(h->world().log.size(), logs);

  // Страница: слоты в ряд.
  CHECK(h.clickUi("arch.edit"));
  h.settle();
  h->setPage(true);
  h.settle();
  const RectF* p0 = h->uiRect("arch.slot.0");
  const RectF* p1 = h->uiRect("arch.slot.3");
  CHECK(p0 && p1 && std::fabs(p0->y - p1->y) < 1);
  CHECK(h.shot("arch_province_wide"));
  // Переход к археологии государства.
  CHECK(h.clickUi("arch.state"));
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Faction, st}));
  CHECK_EQ(h->ui.tabOf[app::SelType::Faction], std::string(archui::kTabFaction));
}

// ---------------------------------------------------------------- вкладка государства
TEST(app_arch_faction_tab_and_reports) {
  HideTestRegs regs;
  Harness h("arch_faction", 1600, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id pid = provinceOf(h->world(), st);
  CHECK(h->act("test", [&](Tx& tx) {
    setupGuild(tx, st, pid);
    setupSlots(tx, pid);
  }));
  openTab(h, st, archui::kTabFaction);
  h.settle();
  CHECK(h->uiRect("arch.groups") && h->uiRect("arch.provinces") && h->uiRect("arch.relics"));
  // Первая группа — кнопкой пустого списка.
  CHECK(clickEmptyAction(h, "arch.groups.create"));
  CHECK_EQ(h->world().faction(st)->archGroups.size(), size_t(1));
  const Id g1 = h->world().faction(st)->archGroups[0].id;
  Id g2 = 0;
  CHECK(h->act("test", [&](Tx& tx) {
    g2 = rules::createArchGroup(tx, st, "Экспедиция Севера");
    rules::setArchGroupExp(tx, st, g2, 320);
    rules::addArchGroupModifier(tx, st, g2, rules::ensureBuiltinMod(tx, schema::mod::ArchPlague));
  }));
  h.settle();
  h.dropToasts();
  CHECK(h->uiRect("arch.group.name." + std::to_string(g2)) != nullptr);
  CHECK(h.shot("arch_faction_wide"));

  // «Найти археологическое место»: меню групп, затем окно итога.
  CHECK(pickGroup(h, "arch.prov." + std::to_string(pid) + ".discover", g1));
  CHECK(h->hasDialog("arch.result"));
  CHECK_EQ(h->world().faction(st)->archGroup(g1)->busy, h->world().turn());
  CHECK(h.shot("arch_result_discover"));
  CHECK(h.clickUi("arch.result.ok"));
  h.settle();
  CHECK(!h->hasDialog("arch.result"));
  // Та же группа в этом ходу больше не назначается.
  std::string why;
  CHECK(!rules::archGroupReady(h->world(), st, g1, &why) && !why.empty());

  // «Назначить археологическую группу»: щелчок по найденному месту (руины, этап 2 из 3).
  CHECK(pickGroup(h, "arch.prov." + std::to_string(pid) + ".slot.0", g2));
  CHECK(h->hasDialog("arch.result"));
  CHECK_EQ(h->world().faction(st)->archGroup(g2)->busy, h->world().turn());
  CHECK(h.shot("arch_result_explore"));
  CHECK(h.clickUi("arch.result.ok"));
  h.settle();

  // Раскопки: все места исследованы — кнопка после слотов.
  CHECK(h->uiRect("arch.prov." + std::to_string(pid) + ".dig") == nullptr);
  CHECK(h->act("test", [&](Tx& tx) {
    for (int i = 0; i < kArchSlots; i++) {
      tx.province(pid).arch[size_t(i)].open = true;
      tx.province(pid).arch[size_t(i)].stage = arch::stagesOf(i);
    }
    for (ArchGroup& g : tx.faction(st).archGroups) g.busy = 0;
  }));
  h.settle();
  CHECK(pickGroup(h, "arch.prov." + std::to_string(pid) + ".dig", g1));
  CHECK(h->hasDialog("arch.result"));
  CHECK(h.shot("arch_result_dig"));
  CHECK(h.clickUi("arch.result.ok"));
  h.settle();

  // Трагедия: смертельная опасность и пробуждение стражей — окно итога, затем окно спасения (решение обязательно).
  const Id death = [&] {
    for (const CatalogItem& e : h->world().catalogs->essences)
      if (e.name == "Эссенция смерти") return e.id;
    return Id(0);
  }();
  Id wildArmy = 0;
  CHECK(h->act("test", [&](Tx& tx) {
    tx.faction(st).ess[death] = 1500;
    for (ArchGroup& g : tx.faction(st).archGroups)
      if (g.id == g1) g.danger = true;
    const geo::ProvinceShape* sh = geo::faces(tx.w())->shape(pid);
    wildArmy = rules::spawnWildArmy(tx, sh ? sh->label : Vec2{4000, 2000}, "Древние стражи",
                                    {{"Древние стражи", UnitType::HeavyInf, schema::kRaceMechanical, 5000}});
  }));
  rules::ArchReport r;
  r.kind = rules::ArchReport::Explore;
  r.state = st;
  r.group = g1;
  r.groupName = h->world().faction(st)->archGroup(g1)->name;
  r.province = pid;
  r.title = "Результат исследования в провинции «" + h->world().provinceName(pid) + "»";
  r.slot = 3;
  r.site = h->world().province(pid)->arch[3].site;
  r.stages = arch::stagesOf(3);
  r.stage = 5;
  r.chance = 31;
  r.exp = 0;
  r.levelBefore = r.levelAfter = 1;
  r.tragedy = rules::ArchReport::Calamity;
  r.calamity = arch::kGuardsTitle;
  r.tragedyLine = 0;
  r.lines = {"Трагедия при исследовании", arch::kGuardsTitle, "Группа в смертельной опасности", "Группу может спасти только божественное вмешательство"};
  r.awaiting = true;
  r.army = wildArmy;
  archui::showReport(h.a(), r);
  h.settle();
  CHECK(h->hasDialog("arch.result") && h->uiRect("arch.result.tragedy") && h->uiRect("arch.result.army"));
  CHECK(h.shot("arch_result_tragedy"));
  CHECK(h.clickUi("arch.result.ok"));
  h.settle();
  CHECK(h->hasDialog("arch.divine"));
  CHECK(h.shot("arch_divine"));
  h.key(Key::Escape);   // без решения окно не закрывается
  h.settle();
  CHECK(h->hasDialog("arch.divine"));
  CHECK(h.clickUi("arch.divine.save"));
  h.settle();
  CHECK(!h->hasDialog("arch.divine"));
  CHECK(rules::archStats(h->world(), st, g1).wounded);
  CHECK(!h->world().faction(st)->archGroup(g1)->danger);
  CHECK_NEAR(h->world().faction(st)->essence(death), 500, 1e-9);

  // Опасность хранится в мире: кнопка на карточке группы открывает окно спасения, отмена события его закрывает.
  CHECK(h->act("test", [&](Tx& tx) {
    for (ArchGroup& g : tx.faction(st).archGroups)
      if (g.id == g1) g.danger = true;
  }));
  h.settle();
  CHECK(!rules::archGroupReady(h->world(), st, g1, nullptr));
  CHECK(h.clickUi("arch.group.divine." + std::to_string(g1)));
  h.settle();
  CHECK(h->hasDialog("arch.divine"));
  h->undo();
  h.settle();
  CHECK(!h->hasDialog("arch.divine"));
  CHECK(!h->uiRect("arch.group.divine." + std::to_string(g1)));

  // Войско без государства — «Показать на карте».
  r.awaiting = false;
  archui::showReport(h.a(), r);
  h.settle();
  CHECK(h.clickUi("arch.result.army"));
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Army, wildArmy}));
  CHECK(!h->hasDialog("arch.result"));
}

TEST(app_arch_faction_tab_narrow) {
  HideTestRegs regs;
  Harness h("arch_faction_narrow", 960, 860);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id pid = provinceOf(h->world(), st);
  Id relic = 0;
  CHECK(h->act("test", [&](Tx& tx) {
    setupGuild(tx, st, pid);
    setupSlots(tx, pid);
    const Id g = rules::createArchGroup(tx, st, "Первая экспедиция");
    rules::addArchGroupModifier(tx, st, g, rules::ensureBuiltinMod(tx, schema::mod::ArchWounded));
    relic = rules::addRelic(tx, "Черепок Шантири", Rarity::Rare);
    for (Relic& x : tx.catalogs().relics)
      if (x.id == relic) x.group = tx.w().catalogs->relicGroupId(schema::kRelicArchFinds);
    rules::moveRelic(tx, relic, rules::RelicPlace{rules::RelicPlace::State, st, 0, st});
  }));
  openTab(h, st, archui::kTabFaction);
  h.settle();
  const RectF* groups = h->uiRect("arch.groups");
  const RectF* provs = h->uiRect("arch.provinces");
  CHECK(groups && provs && provs->y > groups->bottom());   // столбцы друг под другом
  CHECK(h.shot("arch_faction_narrow"));
  // Реликвия государства: освободить.
  CHECK(ensureVisible(h, "arch.relic.release." + std::to_string(relic)) || h->uiRect("arch.relic.release." + std::to_string(relic)));
  CHECK(h.clickUi("arch.relic.release." + std::to_string(relic)));
  h.settle();
  CHECK(rules::relicPlace(h->world(), relic).kind == rules::RelicPlace::Free);
}

// ---------------------------------------------------------------- справочники
TEST(app_arch_catalogs) {
  HideTestRegs regs;
  Harness h("arch_catalogs", 1600, 1000);
  h.demo();
  h.dropToasts();
  h->openEditor("catalogs", 11);
  h.settle();
  CHECK(h->uiRect("catalogs.view.archsites") != nullptr);
  CHECK(h->uiRect("catalogs.add") == nullptr);   // места не добавляются
  const Id tomb = siteKey(h->world(), arch::site::Tomb);
  CHECK(h.clickUi("catalogs.site." + std::to_string(tomb)));
  h.settle();
  CHECK(h->uiRect("catalogs.site.rewards.3") != nullptr);
  CHECK(h.shot("arch_catalog_sites"));
  h->openEditor("catalogs", 12);
  h.settle();
  CHECK(h->uiRect("catalogs.view.chests") != nullptr);
  CHECK(h.shot("arch_catalog_chests"));
  const size_t n = h->world().catalogs->chests.size();
  CHECK(h.clickUi("catalogs.add"));
  h.settle();
  CHECK_EQ(h->world().catalogs->chests.size(), n + 1);
  const Id nc = h->world().catalogs->chests.back().id;
  CHECK(comboPick(h, "catalogs.chest.addItem", "Ресурс"));
  h.settle();
  const Chest* c = h->world().catalogs->chest(nc);
  CHECK(c && c->items.size() == 1 && c->items[0].kind == ChestItemKind::Resource && c->items[0].group != 0);
  CHECK(comboPick(h, "catalogs.chest.addItem", "Артефакт"));
  h.settle();
  CHECK(h->world().catalogs->chest(nc)->items.size() == 2);
  CHECK(h.clickUi("catalogs.chest.item.1.rarity.2"));   // + эпическая
  h.settle();
  CHECK_EQ(h->world().catalogs->chest(nc)->items[1].rarities, (1u << unsigned(Rarity::Common)) | (1u << unsigned(Rarity::Epic)));
  CHECK(h.shot("arch_catalog_chest_new"));
  // Базовый сундук «Сокровище Бессмертного Императора»: вложенные сундуки.
  const Chest* emp = h->world().catalogs->chestByKey("emperor");
  CHECK(emp != nullptr);
  CHECK(h.clickUi("catalogs.chest." + std::to_string(emp->id)));
  h.settle();
  CHECK(h->uiRect("catalogs.chest.item.0.chests") != nullptr);
  CHECK(h.shot("arch_catalog_chest_emperor"));
}

// ---------------------------------------------------------------- «Без государства»
TEST(app_arch_wild_faction_hidden) {
  HideTestRegs regs;
  Harness h("arch_wild");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id pid = provinceOf(h->world(), st);
  Id army = 0;
  CHECK(h->act("test", [&](Tx& tx) {
    const geo::ProvinceShape* sh = geo::faces(tx.w())->shape(pid);
    army = rules::spawnWildArmy(tx, sh->label, "Легионеры неведомого",
                                {{"Легионеры неведомого", UnitType::HeavyInf, schema::kRaceLiving, 5000},
                                 {"Стрелки неведомого", UnitType::Ranged, schema::kRaceLiving, 2500}});
  }));
  const Id wild = rules::wildFaction(h->world());
  CHECK(wild != 0);
  // Каталог государств — без «Без государства».
  h->openDrawer("states");
  h.settle();
  CHECK(h->uiRect("states.row." + std::to_string(st)) != nullptr);
  CHECK(h->uiRect("states.row." + std::to_string(wild)) == nullptr);
  // Новое государство (кнопка каталога) — с «Гильдией Археологов» в своём дереве (ТЗ «Доработки №1», п.10).
  const u32 before = h->world().factions.size();
  CHECK(h.clickUi("states.add"));
  h.settle();
  CHECK_EQ(h->world().factions.size(), before + 1);
  Id created = 0;
  h->world().factions.each([&](const Faction& f) { created = std::max(created, f.id); });
  int guilds = 0;
  h->world().buildings.each([&](const Building& b) { guilds += b.owner == created && b.key == schema::bld::ArchGuild && b.cat == BuildingCat::Cult; });
  CHECK_EQ(guilds, 1);
  h.key(Key::Escape);
  h.settle();
  // Итоги хода — без «Без государства».
  const rules::TurnReport rep = rules::previewTurn(h->world());
  for (const rules::TurnFactionLine* l : app::turnui::sortedLines(h->world(), rep)) CHECK(l->faction != wild);
  // Войско без государства на карте и в панели войска.
  h->toMap();
  h->select(app::SelType::Army, army, true);
  h.settle();
  h.waitMap();
  h.dropToasts();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Army, army}));
  CHECK(h.shot("arch_wild_army"));
}
