// Сценарии ТЗ «Доработки №1–4» в экономике и справочниках (агент ECON): модификаторы археологических групп и генерация
// за ход, возможности построек (верфь и типы кораблей, целительство, чума, цена в эссенциях, требование «на
// государство», встроенная постройка), одна стройка в провинции, «Вылечить провинцию» и «Заразить чумой», стоимость и
// требуемые постройки технологий (списание и возврат), наёмники (кнопка с причиной, окно, отметка строки, цена найма,
// лимит), совет не больше 10 должностей и «Влияние совета», выбор ресурса сделки через группы, «+10 одна вера» в
// дипломатии, базовая стоимость кораблей и эссенции в константах, содержание археологических групп в экономике.
#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/editors/trade.h"
#include "tests/test_app_editors_util.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

namespace ed = rg::apptest::editors;
namespace tr = rg::apptest::trees;

Id essenceNamed(const World& w, const std::string& name) {
  for (const CatalogItem& e : w.catalogs->essences)
    if (e.name == name) return e.id;
  return 0;
}
Id resourceNamed(const World& w, const std::string& name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}
Id buildingWithKey(const World& w, std::string_view key, Id owner = 0) {
  Id r = 0;
  w.buildings.each([&](const Building& b) {
    if (!r && b.key == key && b.owner == owner) r = b.id;
  });
  return r;
}
Id buildingNamed(const World& w, const std::string& name) {
  Id r = 0;
  w.buildings.each([&](const Building& b) {
    if (!r && b.name == name) r = b.id;
  });
  return r;
}
Id techWithKey(const World& w, std::string_view key) {
  Id r = 0;
  w.techs.each([&](const Tech& t) {
    if (!r && t.key == key) r = t.id;
  });
  return r;
}
const Modifier* modOf(const World& w, Id id) { return w.modifier(id); }

// Короткий список без поиска: открыть, сдвинуться стрелками от текущего пункта на steps, Enter.
bool comboStep(Harness& h, const std::string& name, int steps) {
  if (!ed::clickRevealed(h, name)) return false;
  for (int i = 0; i < std::abs(steps); i++) h.key(steps > 0 ? Key::Down : Key::Up);
  h.key(Key::Enter);
  ed::quick(h);
  return true;
}

// Вкладка фракции (страница) — с начала прокрутки.
void factionTab(Harness& h, Id faction, const char* tab) {
  h->ui.tabOf[app::SelType::Faction] = tab;
  h->select(app::SelType::Faction, faction);
  h.waitMap();
  h.settle(60);
  h.dropToasts();
}

void provinceTab(Harness& h, Id province, const char* tab) {
  h->ui.tabOf[app::SelType::Province] = tab;
  h->select(app::SelType::Province, province, true);
  h.waitMap();
  h.settle(60);
  h.dropToasts();
}

// Строка таблицы во вкладке: строки вне видимой части не строятся — прокручивать вниз, пока не появится и не станет
// видна целиком.
bool revealRow(Harness& h, const std::string& name) {
  for (int k = 0; k < 60; k++) {
    if (h->uiRect(name)) return tr::revealIn(h, name);
    const RectF* in = h->uiRect("inspector");
    if (!in) return false;
    h.wheel(in->cx(), in->cy() + 40, -2.f);
    h.frames(12);
  }
  return false;
}

void shot(Harness& h, const std::string& name) {
  h.waitMap();
  h.dropToasts();
  h.settle(60);
  CHECK(h.shot(name));
}

}  // namespace

// ---------------------------------------------------------------- модификаторы
TEST(app_econ2_modifiers_arch_and_generation) {
  tr::HideTestRegs hide;
  Harness h("econ2_mods", 1600, 1000);
  h.demo();
  h.dropToasts();
  h->openEditor("modifiers", 0);
  ed::quick(h);
  // Встроенные модификаторы групп («Ранение в ходе исследования», «Заражение чумой») — в своей группе списка.
  CHECK(h->uiRect("modifiers.group.arch") != nullptr);
  // Новый модификатор → вид «Для археологических групп»: только эффекты групп, без генерации.
  CHECK(h.clickUi("modifiers.new"));
  ed::quick(h);
  const Id mid = h->ui.editorArg;
  CHECK(modOf(h->world(), mid) != nullptr);
  h.retype("Опытные проводники");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK(comboStep(h, "modifiers.kind", 5));   // «Везде» → «Для археологических групп»
  CHECK(modOf(h->world(), mid) && modOf(h->world(), mid)->kind == ModKind::ArchGroup);
  CHECK(h->uiRect("modifiers.arch") != nullptr);
  CHECK(h->uiRect("modifiers.local") == nullptr);
  CHECK(h->uiRect("modifiers.global") == nullptr);
  CHECK(h->uiRect("modifiers.gen") == nullptr);
  CHECK(ed::clickRevealed(h, "modifiers.fx.archSuccessPct.on"));
  ed::quick(h);
  CHECK(modOf(h->world(), mid)->has(Fx::ArchSuccessPct));
  CHECK(ed::enterValue(h, "modifiers.fx.archSuccessPct.value", "9"));   // предел ТЗ: +1…+5 %
  CHECK_NEAR(modOf(h->world(), mid)->fx[size_t(Fx::ArchSuccessPct)], 5.0, 1e-9);
  CHECK(ed::clickRevealed(h, "modifiers.fx.archUpkeepPct.on"));
  ed::quick(h);
  CHECK(ed::enterValue(h, "modifiers.fx.archUpkeepPct.value", "-30"));
  CHECK_NEAR(modOf(h->world(), mid)->fx[size_t(Fx::ArchUpkeepPct)], -30.0, 1e-9);
  ed::shotClean(h, "econ2_mods_arch");
  // Вид «Для провинций»: эффекты групп остаются (не действуют), генерация за ход — эссенции и ресурсы.
  CHECK(comboStep(h, "modifiers.kind", -4));   // → «Для провинций»
  CHECK(modOf(h->world(), mid)->kind == ModKind::Province);
  CHECK(h->uiRect("modifiers.local") != nullptr);
  CHECK(ed::reveal(h, "modifiers.gen"));
  const Id death = essenceNamed(h->world(), "Эссенция смерти");
  CHECK(death != 0);
  CHECK(ed::pickInCombo(h, "modifiers.gen.ess.add", "смерти"));
  CHECK(modOf(h->world(), mid)->essGen.count(death) == 1);
  CHECK(ed::enterValue(h, "modifiers.gen.ess." + std::to_string(death), "12,5"));
  CHECK_NEAR(modOf(h->world(), mid)->essGen.at(death), 12.5, 1e-9);
  const Id coal = resourceNamed(h->world(), "Уголь");
  CHECK(coal != 0);
  CHECK(ed::pickInCombo(h, "modifiers.gen.res.add", "Уголь"));
  CHECK(modOf(h->world(), mid)->resGen.count(coal) == 1);
  ed::shotClean(h, "econ2_mods_generation");
  // Генерация действует: модификатор в провинции государства — эссенция владельцу за ход.
  Id pid = 0, owner = 0;
  h->world().provinces.each([&](const Province& p) {
    const Faction* f = h->world().faction(p.owner);
    if (!pid && !p.sea && f && f->isState()) {
      pid = p.id;
      owner = p.owner;
    }
  });
  CHECK(pid && owner);
  const double before = rules::calc(h->world())->faction(owner)->essences.at(death).generation;
  CHECK(h->act("Модификатор провинции", [&](Tx& tx) { rules::addModifier(tx, rules::ModTarget::Province, pid, mid); }));
  CHECK_NEAR(rules::calc(h->world())->faction(owner)->essences.at(death).generation, before + 12.5, 1e-9);
  // Снова вид групп — прочие эффекты и генерация снимаются.
  CHECK(comboStep(h, "modifiers.kind", 4));
  CHECK(modOf(h->world(), mid)->kind == ModKind::ArchGroup);
  CHECK(modOf(h->world(), mid)->essGen.empty());
  CHECK(modOf(h->world(), mid)->resGen.empty());
  CHECK(modOf(h->world(), mid)->has(Fx::ArchSuccessPct));
  // «Некромант» (шаблон): генерация по умолчанию — эссенция смерти +10.
  CHECK(ed::clickRevealed(h, "modifiers.builtin.necromancer", "modifiers.list"));
  ed::quick(h);
  CHECK(ed::reveal(h, "modifiers.gen.ess." + std::to_string(death)));
  ed::shotClean(h, "econ2_mods_necromancer");
}

// ---------------------------------------------------------------- дерево построек
TEST(app_econ2_buildings_roles) {
  tr::HideTestRegs hide;
  Harness h("econ2_buildings", 1600, 1000);
  h.demo();
  h.dropToasts();
  CHECK_EQ(std::string(schema::buildingCat(BuildingCat::Residential).name), std::string("Жилые и сельско-хозяйственные"));
  // Общее дерево с базовыми постройками ТЗ: сетка по дорожкам без наложений.
  app::openBuildingTree(h.a(), 0);
  h.settle();
  {
    std::vector<const Building*> common;
    h->world().buildings.each([&](const Building& b) {
      if (!b.owner) common.push_back(&b);
    });
    for (size_t i = 0; i < common.size(); i++)
      for (size_t j = i + 1; j < common.size(); j++)
        if (common[i]->cat == common[j]->cat) {
          const bool overlap = std::fabs(common[i]->pos.x - common[j]->pos.x) < 232 && std::fabs(common[i]->pos.y - common[j]->pos.y) < 100;
          CHECK_MSG(!overlap, common[i]->name + " / " + common[j]->name);
        }
  }
  tr::shotTrees(h, "econ2_bt_tree");
  // Верфь: встроенная (замок), только приморская, уровни открывают типы кораблей.
  const Id yard = buildingWithKey(h->world(), schema::bld::Shipyard);
  CHECK(yard != 0);
  h.key(Key::Escape);
  app::openBuildingTree(h.a(), 0, yard);
  h.settle();
  CHECK(h->uiRect("bt.side.builtin") != nullptr);
  CHECK(tr::revealSide(h, "bt.flag.shipyard"));
  CHECK(h->uiRect("bt.flag.coastal") != nullptr);
  const std::string frig = "bt.level.0.ship." + std::to_string(int(ShipType::Frigate));
  CHECK(tr::revealSide(h, frig));
  CHECK(!(h->world().building(yard)->levels[0].ships & (1u << int(ShipType::Frigate))));
  tr::shotTrees(h, "econ2_bt_shipyard");
  CHECK(tr::clickSide(h, frig));
  CHECK(h->world().building(yard)->levels[0].ships & (1u << int(ShipType::Frigate)));
  tr::undo(h);
  CHECK(!(h->world().building(yard)->levels[0].ships & (1u << int(ShipType::Frigate))));
  // Собор: цена в эссенции и требование «на государство» (4 храма той же эссенции на каждый собор).
  const Id death = essenceNamed(h->world(), "Эссенция смерти");
  const Id cathedral = buildingNamed(h->world(), "Собор смерти"), temple = buildingNamed(h->world(), "Храм смерти");
  CHECK(cathedral && temple && death);
  h.key(Key::Escape);
  app::openBuildingTree(h.a(), 0, cathedral);
  h.settle();
  CHECK(tr::revealSide(h, "bt.sreq." + std::to_string(temple) + ".per"));
  CHECK(tr::typeSide(h, "bt.sreq." + std::to_string(temple) + ".per", "5"));
  CHECK_EQ(h->world().building(cathedral)->stateReqs.front().per, 5);
  CHECK(tr::revealSide(h, "bt.level.0.esscost." + std::to_string(death)));
  CHECK(tr::typeSide(h, "bt.level.0.esscost." + std::to_string(death), "175"));
  CHECK_NEAR(h->world().building(cathedral)->levels[0].essCost.at(death), 175.0, 1e-9);
  tr::shotTrees(h, "econ2_bt_cathedral");
  // Своя постройка: возможности «Здание целительства», «Хранилище реликвий», цена в эссенции.
  Id own = 0;
  CHECK(h->act("Новая постройка", [&](Tx& tx) {
    own = rules::createBuilding(tx, 0, "Лечебница");
    tx.building(own).cat = BuildingCat::Residential;
  }));
  h.key(Key::Escape);
  app::openBuildingTree(h.a(), 0, own);
  h.settle();
  CHECK(h->uiRect("bt.side.builtin") == nullptr);
  CHECK(tr::clickSide(h, "bt.flag.healing", "bt.canvas", 0.9f));
  CHECK(h->world().building(own)->healing);
  CHECK(tr::clickSide(h, "bt.flag.relics", "bt.canvas", 0.9f));
  CHECK(h->world().building(own)->relicStore);
  CHECK(tr::pickSide(h, "bt.level.0.addesscost", "Эссенция смерти"));
  CHECK(h->world().building(own)->levels[0].essCost.count(death) == 1);
  tr::shotTrees(h, "econ2_bt_flags");
  CHECK(tr::clickSide(h, "bt.flag.healing", "bt.canvas", 0.9f));
  CHECK(!h->world().building(own)->healing);
}

// ---------------------------------------------------------------- постройки провинции: чума, целительство, одна стройка
TEST(app_econ2_construction_plague) {
  tr::HideTestRegs hide;
  Harness h("econ2_plague", 1600, 1000);
  h.demo();
  h.dropToasts();
  const tr::StateProv sp = tr::stateWithFreeSlots(h->world(), 3);
  CHECK(sp.state && sp.province);
  const Id plagueEss = essenceNamed(h->world(), "Эссенция чумы"), water = essenceNamed(h->world(), "Эссенция воды");
  CHECK(plagueEss && water);
  Id heal = 0, plague = 0;
  CHECK(h->act("Постройки чумы и целительства", [&](Tx& tx) {
    heal = rules::createBuilding(tx, 0, "Обитель целителей");
    plague = rules::createBuilding(tx, 0, "Чумной двор");
    rules::setBuildingFlag(tx, heal, rules::BuildingFlag::Healing, true);
    rules::setBuildingFlag(tx, plague, rules::BuildingFlag::Plague, true);
    rules::placeBuilding(tx, sp.province, heal, 1);
    rules::placeBuilding(tx, sp.province, plague, 1);
    rules::setEssence(tx, sp.state, plagueEss, 3000);
    rules::setEssence(tx, sp.state, water, 600);
  }));
  provinceTab(h, sp.province, "province.buildings");
  const std::string healMark = "prov.heal." + std::to_string(heal), plagueMark = "prov.plague." + std::to_string(plague);
  CHECK(tr::revealIn(h, plagueMark));
  // Чумы нет — «Вылечить провинцию» недоступна (щелчок ничего не делает).
  u64 v = h->store.version();
  CHECK(tr::revealIn(h, healMark));
  tr::clickQuick(h, healMark);
  CHECK_EQ(h->store.version(), v);
  // «Заразить чумой» — 2500 эссенции чумы.
  CHECK(tr::revealIn(h, plagueMark));
  tr::clickQuick(h, plagueMark);
  CHECK(rules::provinceHas(h->world(), sp.province, schema::mod::Plague));
  CHECK_NEAR(h->world().faction(sp.state)->essence(plagueEss), 500.0, 1e-9);
  shot(h, "econ2_prov_plague");
  // «Вылечить провинцию» — выбор эссенции (500): чума снята, «Временный иммунитет».
  CHECK(tr::revealIn(h, healMark));
  tr::clickQuick(h, healMark);
  shot(h, "econ2_prov_heal_menu");
  CHECK(h.clickUi(healMark + ".ess." + std::to_string(water)));
  h.settle(40);
  CHECK(!rules::provinceHas(h->world(), sp.province, schema::mod::Plague));
  CHECK(rules::provinceHas(h->world(), sp.province, schema::mod::PlagueImmunity));
  CHECK_NEAR(h->world().faction(sp.state)->essence(water), 100.0, 1e-9);
  // Иммунитет — заразить снова нельзя.
  v = h->store.version();
  CHECK(tr::revealIn(h, plagueMark));
  tr::clickQuick(h, plagueMark);
  CHECK_EQ(h->store.version(), v);
  // Модификаторы провинции: «Временный иммунитет» со сроком.
  provinceTab(h, sp.province, "province.modifiers");
  shot(h, "econ2_prov_immunity");
  // Одна стройка в провинции: пока строится одна постройка, «Построить» не открывает выбор.
  Id quick = 0;
  CHECK(h->act("Стройка", [&](Tx& tx) {
    quick = rules::createBuilding(tx, 0, "Колодец");
    tx.building(quick).levels[0].cost.clear();
    tx.building(quick).levels[0].turns = 3;
    rules::startBuilding(tx, sp.province, quick);
  }));
  provinceTab(h, sp.province, "province.buildings");
  CHECK(tr::revealIn(h, "prov.build"));
  tr::clickQuick(h, "prov.build");
  CHECK(!h->hasDialog("build.picker"));
  bool reasonOne = false;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province))
    for (const std::string& r : o.reasons) reasonOne = reasonOne || r.find("одновременно") != std::string::npos;
  CHECK(reasonOne);
  // Выбор стройки: собор — цена в эссенции (нехватка — красным).
  CHECK(h->act("Отменить стройку", [&](Tx& tx) { rules::cancelBuilding(tx, sp.province, quick); }));
  CHECK(h->openDialog("build.picker", sp.province));
  h.settle(40);
  const Id cathedral = buildingNamed(h->world(), "Собор смерти");
  CHECK(cathedral != 0);
  bool essOpt = false;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province))
    if (o.building == cathedral) essOpt = !o.essCost.empty() && !o.can;
  CHECK(essOpt);
  CHECK(h.clickUi("build.search"));
  h.type("Собор смерти");
  h.settle(40);
  shot(h, "econ2_build_picker");
}

// ---------------------------------------------------------------- технологии: стоимость и требуемые постройки
TEST(app_econ2_tech_cost) {
  tr::HideTestRegs hide;
  Harness h("econ2_tech", 1600, 1000);
  h.demo();
  h.dropToasts();
  const tr::StateProv sp = tr::stateWithFreeSlots(h->world(), 2);
  CHECK(sp.state && sp.province);
  const Id basics = techWithKey(h->world(), "archBasics");
  const Id treasure = h->world().catalogs->resourceId(schema::kResArchTreasure);
  CHECK(basics && treasure);
  CHECK_NEAR(h->world().tech(basics)->cost.at(treasure), 100.0, 1e-9);
  // Нет «Гильдии Археологов» и сокровищ — исследовать нельзя: причина видна, кнопка недоступна.
  {
    const rules::ResearchCheck rc = rules::canResearch(h->world(), basics, sp.state);
    CHECK(!rc.ok);
    CHECK(rc.problems.size() >= 2);
  }
  factionTab(h, sp.state, "faction.tech");
  const std::string sb = std::to_string(basics);
  CHECK(tr::revealIn(h, "faction.ctech.cost." + sb));
  u64 v = h->store.version();
  CHECK(tr::revealIn(h, "faction.ctech.start." + sb));
  tr::clickQuick(h, "faction.ctech.start." + sb);
  CHECK_EQ(h->store.version(), v);
  shot(h, "econ2_tech_tab_locked");
  // Гильдия археологов достроена, сокровищ 150: начать — списано 100, остановить — возвращено.
  const Id guild = buildingWithKey(h->world(), schema::bld::ArchGuild, sp.state);
  CHECK(guild != 0);
  CHECK(h->act("Гильдия и сокровища", [&](Tx& tx) {
    rules::placeBuilding(tx, sp.province, guild, 1);
    tx.faction(sp.state).res[treasure] = 150;
  }));
  CHECK(rules::canResearch(h->world(), basics, sp.state).ok);
  h.settle(40);
  CHECK(tr::revealIn(h, "faction.ctech.start." + sb));
  tr::clickQuick(h, "faction.ctech.start." + sb);
  CHECK(rules::techState(h->world(), basics, sp.state).research);
  CHECK_NEAR(h->world().faction(sp.state)->stock(treasure), 50.0, 1e-9);
  shot(h, "econ2_tech_tab_research");
  CHECK(tr::revealIn(h, "faction.ctech.stop." + sb));
  tr::clickQuick(h, "faction.ctech.stop." + sb);
  CHECK(!rules::techState(h->world(), basics, sp.state).research);
  CHECK_NEAR(h->world().faction(sp.state)->stock(treasure), 150.0, 1e-9);
  // Редактор общего дерева: стоимость (ресурс через группы) и нужные постройки (по ключу встроенной постройки).
  app::openTechTree(h.a(), 0, basics, sp.state);
  h.settle(60);
  CHECK(tr::revealSide(h, "tt.side.cost." + std::to_string(treasure), "tt.canvas"));
  CHECK(h->uiRect("tt.side.need." + std::string(schema::bld::ArchGuild)) != nullptr);
  CHECK(tr::typeSide(h, "tt.side.cost." + std::to_string(treasure), "120", "tt.canvas"));
  CHECK_NEAR(h->world().tech(basics)->cost.at(treasure), 120.0, 1e-9);
  const Id coal = resourceNamed(h->world(), "Уголь");
  CHECK(tr::pickSide(h, "tt.side.addcost", "Уголь", "tt.canvas"));
  CHECK(h->world().tech(basics)->cost.count(coal) == 1);
  CHECK(tr::pickSide(h, "tt.side.addneed", "Порт", "tt.canvas"));
  const std::vector<std::string>& keys = h->world().tech(basics)->needKeys;
  CHECK(std::find(keys.begin(), keys.end(), std::string(schema::bld::Port)) != keys.end());
  CHECK(!rules::canResearch(h->world(), basics, sp.state).ok);   // порта нет, угля нет
  tr::shotTrees(h, "econ2_tech_editor");
}

// ---------------------------------------------------------------- наёмники
TEST(app_econ2_mercenaries) {
  tr::HideTestRegs hide;
  Harness h("econ2_merc", 1600, 1000);
  h.demo();
  h.dropToasts();
  const tr::StateProv sp = tr::stateWithFreeSlots(h->world(), 2);
  CHECK(sp.state && sp.province);
  factionTab(h, sp.state, "faction.army");
  // Без «Гильдии Наемников» кнопка недоступна.
  CHECK(tr::revealIn(h, "mil.merc.create"));
  tr::clickQuick(h, "mil.merc.create");
  CHECK(!h->hasDialog("mil.merc"));
  CHECK(h->uiRect("mil.merc.limit") == nullptr);
  const Id guild = buildingWithKey(h->world(), schema::bld::MercGuild);
  CHECK(guild != 0);
  CHECK(h->act("Гильдия наёмников", [&](Tx& tx) { rules::placeBuilding(tx, sp.province, guild, 1); }));
  CHECK_EQ(rules::mercLimit(h->world(), sp.state), i64(2500));
  h.settle(40);
  CHECK(tr::revealIn(h, "mil.merc.create"));
  tr::clickQuick(h, "mil.merc.create");
  CHECK(h->hasDialog("mil.merc"));
  // Окно: тяжёлая пехота, название, найм 0,05 тыс. за юнит; содержание по умолчанию 0,001.
  CHECK(ed::clickPart(h, "mil.merc.type", 2, 6));
  h.settle(20);
  CHECK(h.clickUi("mil.merc.name"));
  h.type("Чёрные клинки");
  h.key(Key::Tab);
  h.settle(20);
  CHECK(h.clickUi("mil.merc.hire"));
  h.key(Key::A, ctrl());
  h.type("0,05");
  h.key(Key::Tab);
  h.settle(20);
  shot(h, "econ2_merc_dialog");
  const size_t rows0 = h->world().faction(sp.state)->army.size();
  CHECK(h.clickUi("mil.merc.ok"));
  h.settle(40);
  CHECK(!h->hasDialog("mil.merc"));
  const Faction* f = h->world().faction(sp.state);
  CHECK_EQ(f->army.size(), rows0 + 1);
  const ArmyRow& r = f->army.back();
  CHECK(r.merc);
  CHECK(r.type == UnitType::HeavyInf);
  CHECK_EQ(r.name, std::string("Чёрные клинки"));
  CHECK_NEAR(r.hire, 0.05, 1e-12);
  CHECK_NEAR(r.upkeep, schema::kNewRowUpkeep, 1e-12);
  CHECK_EQ(r.race, std::string(schema::kRaceMercenary));
  const Id row = r.id;
  // Формирование — только золото: 100 × 0,05 = 5 тыс.; население не тратится.
  const double gold0 = f->treasury();
  const i64 pop0 = rules::statePopulation(h->world(), sp.state);
  CHECK(h->act("Найм наёмников", [&](Tx& tx) { rules::recruit(tx, sp.state, row, 100); }));
  CHECK_NEAR(h->world().faction(sp.state)->treasury(), gold0 - 5.0, 1e-9);
  CHECK_EQ(rules::statePopulation(h->world(), sp.state), pop0);
  CHECK_EQ(rules::mercCount(h->world(), sp.state), i64(100));
  h.settle(40);
  CHECK(tr::revealIn(h, "mil.merc.limit"));
  // Строка в таблице: выбрать — в карточке цена найма правится.
  int idx = -1;
  for (size_t i = 0; i < h->world().faction(sp.state)->army.size(); i++)
    if (h->world().faction(sp.state)->army[i].id == row) idx = int(i);
  CHECK(idx >= 0);
  // Широкая таблица страницы: щелчок по ячейке «Резерв» (слева от поля содержания) выбирает строку.
  const std::string upMark = "mil.row." + std::to_string(idx) + ".upkeep";
  CHECK(tr::revealIn(h, upMark));
  {
    const RectF ur = tr::rectOf(h.a(), upMark);
    h.click(ur.x - 24, ur.cy());
    h.settle(40);
  }
  CHECK(tr::revealIn(h, "mil.detail.hire"));
  CHECK(h.clickUi("mil.detail.hire"));
  h.key(Key::A, ctrl());
  h.type("0,08");
  h.key(Key::Enter);
  h.settle(20);
  CHECK_NEAR(h->world().faction(sp.state)->armyRow(row)->hire, 0.08, 1e-12);
  shot(h, "econ2_merc_tab");
}

// ---------------------------------------------------------------- совет
TEST(app_econ2_council_limit_and_influence) {
  tr::HideTestRegs hide;
  Harness h("econ2_council", 1600, 1000);
  h.demo();
  h.dropToasts();
  Id st = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!st && f.isState()) st = f.id;
  });
  CHECK(st != 0);
  // Десять должностей, пять назначений — «Централизованная власть» и «Влияние совета» −2 % × 10.
  CHECK(h->act("Совет", [&](Tx& tx) {
    tx.faction(st).council.clear();
    for (int i = 0; i < schema::kMaxCouncilSeats; i++) {
      Id who = 0;
      if (i < 5) who = rules::createCharacter(tx, st, "Советник " + std::to_string(i + 1));
      rules::addCouncilSeat(tx, st, "Должность " + std::to_string(i + 1), who);
    }
  }));
  CHECK(!h->act("Одиннадцатая", [&](Tx& tx) { rules::addCouncilSeat(tx, st, "Лишняя"); }));
  h->toasts().clear();
  const rules::Effects fx = rules::factionEffects(h->world(), st);
  double influence = 0;
  for (const rules::AutoMod& am : rules::autoModifiers(h->world(), st))
    if (am.key == schema::mod::CouncilInfluence) influence = am.scale;
  CHECK_NEAR(influence, 10.0, 1e-9);
  CHECK(fx[Fx::ResearchTimePct] <= -20 + 1e-9);
  factionTab(h, st, "faction.council");
  CHECK(h->uiRect("council.influence") != nullptr);
  CHECK(tr::revealIn(h, "council.add"));
  tr::clickQuick(h, "council.add");
  CHECK_EQ(h->world().faction(st)->council.size(), size_t(schema::kMaxCouncilSeats));
  shot(h, "econ2_council");
  // Сводка модификаторов государства: автоматические — «Влияние совета» с множителем.
  factionTab(h, st, "faction.modifiers");
  CHECK(h->uiRect("mods.auto.section") != nullptr);
  shot(h, "econ2_faction_mods");
}

// ---------------------------------------------------------------- торговля: ресурс через группы
TEST(app_econ2_trade_groups) {
  tr::HideTestRegs hide;
  Harness h("econ2_trade", 1600, 1000);
  h.demo();
  h.dropToasts();
  auto [a, b] = ed::twoStates(h->world());
  CHECK(a && b);
  app::trade::startDraft(a, b);
  h->openEditor("trade", a);
  h.settle();
  CHECK(h.clickUi("trade.add.a"));
  h.settle();
  CHECK(!app::trade::draft().items.empty());
  const u64 key = app::trade::draft().items.back().key;
  const std::string base = "trade.item." + std::to_string(key);
  // Группа «Руда» → в списке ресурсов только её ресурсы; выбор «Железо».
  CHECK(ed::pickInCombo(h, base + ".group", "Руда"));
  const Id ore = h->world().catalogs->groupId(schema::grp::Ore);
  CHECK(ore != 0);
  CHECK_EQ(app::trade::draft().items.back().group, ore);
  CHECK(ed::pickInCombo(h, base + ".res", "Железо"));
  const Id iron = resourceNamed(h->world(), "Железо");
  CHECK(iron != 0);
  CHECK_EQ(app::trade::draft().items.back().res, iron);
  CHECK(h->world().catalogs->resourceIn(iron, ore));
  ed::shotClean(h, "econ2_trade_groups");
}

// ---------------------------------------------------------------- дипломатия: одна вера
TEST(app_econ2_diplomacy_faith) {
  tr::HideTestRegs hide;
  Harness h("econ2_faith", 1600, 1000);
  h.demo();
  h.dropToasts();
  auto [a, b] = ed::twoStates(h->world());
  CHECK(a && b);
  const Id rel = h->world().catalogs->religions.front().id;
  CHECK(h->act("Одна вера", [&](Tx& tx) {
    tx.faction(a).religion = rel;
    tx.faction(b).religion = rel;
    rules::setRelation(tx, a, b, 20, RelStatus::Neutral);
  }));
  CHECK_NEAR(rules::relationValue(h->world(), a, b), 30.0, 1e-9);
  factionTab(h, a, "faction.diplomacy");
  const std::string sb = std::to_string(b);
  CHECK(tr::revealIn(h, "dip.faith." + sb));
  // Поле — действующее значение; правка меняет хранимое на ту же разницу.
  CHECK(tr::revealIn(h, "dip.value." + sb));
  CHECK(h.clickUi("dip.value." + sb));
  h.key(Key::A, ctrl());
  h.type("50");
  h.key(Key::Enter);
  h.settle(20);
  CHECK_NEAR(h->world().relation(a, b).v, 40.0, 1e-9);
  CHECK_NEAR(rules::relationValue(h->world(), a, b), 50.0, 1e-9);
  shot(h, "econ2_diplomacy_faith");
}

// ---------------------------------------------------------------- константы: морское чудовище и новые константы
TEST(app_econ2_constants_ships) {
  tr::HideTestRegs hide;
  Harness h("econ2_constants", 1600, 1000);
  h.demo();
  h.dropToasts();
  h->openEditor("constants");
  ed::quick(h);
  for (const char* k : {schema::cst::FrigateCapacity, schema::cst::LineCapacity, schema::cst::MercPerGuild, schema::cst::MercHire,
                        schema::cst::ArchGroupPeople, schema::cst::ArchGroupGold, schema::cst::SeaMonsterCost})
    CHECK_MSG(h->uiRect(std::string("constants.list.") + k) != nullptr, k);
  // Морское чудовище: эссенция воды 750 — базовая (замок, меньше нельзя), ещё одна эссенция — добавить и убрать.
  CHECK(ed::clickRevealed(h, "constants.list." + std::string(schema::cst::SeaMonsterCost)));
  ed::quick(h);
  const Id water = essenceNamed(h->world(), "Эссенция воды"), fire = essenceNamed(h->world(), "Эссенция пламени");
  CHECK(water && fire);
  const std::string base = "constants." + std::string(schema::cst::SeaMonsterCost);
  CHECK(h->uiRect(base + ".ess." + std::to_string(water) + ".lock") != nullptr);
  CHECK(ed::enterValue(h, base + ".ess." + std::to_string(water), "500"));
  CHECK_NEAR(rules::constantOf(h->world(), schema::cst::SeaMonsterCost).ess.at(water), 750.0, 1e-9);
  CHECK(ed::enterValue(h, base + ".ess." + std::to_string(water), "900"));
  CHECK_NEAR(rules::constantOf(h->world(), schema::cst::SeaMonsterCost).ess.at(water), 900.0, 1e-9);
  ed::shotClean(h, "econ2_constants_sea_monster_base");
  CHECK(ed::pickInCombo(h, base + ".addess", "пламени"));
  CHECK(rules::constantOf(h->world(), schema::cst::SeaMonsterCost).ess.count(fire) == 1);
  ed::shotClean(h, "econ2_constants_sea_monster");
  CHECK(ed::clickRevealed(h, base + ".ess." + std::to_string(fire) + ".remove"));
  ed::quick(h);
  CHECK(rules::constantOf(h->world(), schema::cst::SeaMonsterCost).ess.count(fire) == 0);
  // Вместимость фрегата — целое число.
  CHECK(ed::clickRevealed(h, "constants.list." + std::string(schema::cst::FrigateCapacity)));
  ed::quick(h);
  CHECK(ed::enterValue(h, "constants." + std::string(schema::cst::FrigateCapacity) + ".num", "1200"));
  CHECK_NEAR(rules::constantOf(h->world(), schema::cst::FrigateCapacity).num, 1200.0, 1e-9);
  ed::shotClean(h, "econ2_constants_capacity");
}

// ---------------------------------------------------------------- экономика государства
TEST(app_econ2_economy_arch_and_currencies) {
  tr::HideTestRegs hide;
  Harness h("econ2_economy", 1600, 1000);
  h.demo();
  h.dropToasts();
  Id st = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!st && f.isState()) st = f.id;
  });
  CHECK(st != 0);
  CHECK(h->act("Археологическая группа", [&](Tx& tx) {
    ArchGroup g;
    g.id = tx.nextId(Seq::ArchGroup);
    g.name = "Первая экспедиция";
    tx.faction(st).archGroups.push_back(g);
    tx.faction(st).res[h->world().catalogs->resourceId(schema::kResArchTreasure)] = 75;
  }));
  const rules::FactionCalc* fc = rules::calc(h->world())->faction(st);
  CHECK(fc && fc->expArch > 0);
  factionTab(h, st, "faction.economy");
  shot(h, "econ2_economy");
  // Валюты: золото — казна (одна строка), группа «Валюты» — сокровища и свитки.
  CHECK(tr::revealIn(h, "economy.resources"));
  CHECK(revealRow(h, "economy.res." + std::to_string(kGold)));
  const Id cur = h->world().catalogs->groupId(schema::grp::Currencies);
  CHECK(cur != 0);
  CHECK(revealRow(h, "economy.group." + std::to_string(cur)));
  shot(h, "econ2_economy_resources");
}
