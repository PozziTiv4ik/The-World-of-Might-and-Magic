// Реликвии стопками (ТЗ «Доработки №4», п.3–4) на демонстрационном мире: инвентарь государства на вкладке «Археология»
// (отдать героям и положить в хранилище окном «куда и сколько», количество, «Убрать», новые из справочника; игрок —
// только раздача), хранилище реликвий постройки (положить из инвентаря государства с количеством, вынуть окном
// «Сколько», количество у администратора), карточка справочника реликвий (все места с количеством, правка количества в
// месте, у выбранного государства и героя; уникальная — «У героя»), справочники закрыты игроку. Снимки экранов.
#include "app/panels/arch_common.h"
#include "tests/test_app_editors_util.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;
namespace archui = rg::app::archui;
namespace ed = rg::apptest::editors;

namespace {

// Значимые герои государства (кроме правителя), по ID.
std::vector<Id> stateHeroes(const World& w, Id st) {
  std::vector<Id> out;
  w.characters.each([&](const Character& c) {
    if (c.faction == st && c.hero && w.faction(st)->ruler != c.id && rules::heroAvailable(w, c.id)) out.push_back(c.id);
  });
  return out;
}

Id provinceOf(const World& w, Id state) {
  Id r = 0;
  w.provinces.each([&](const Province& p) {
    if (!r && p.owner == state && !p.sea) r = p.id;
  });
  return r;
}

// Достроенное хранилище реликвий в провинции pid.
Id addStore(Tx& tx, Id pid, const char* name) {
  const Id b = rules::createBuilding(tx, 0, name);
  rules::setBuildingFlag(tx, b, rules::BuildingFlag::RelicStore, true);
  tx.province(pid).size = ProvSize::Large;
  test::setCityPop(tx, pid, CityType::City);
  rules::placeBuilding(tx, pid, b, 1);
  return b;
}

rules::RelicPlace statePlace(Id st) { return rules::RelicPlace{rules::RelicPlace::State, st, 0, st}; }
rules::RelicPlace storePlace(Id pid, Id b, Id st) { return rules::RelicPlace{rules::RelicPlace::Building, pid, b, st}; }
rules::RelicPlace heroPlace(const World& w, Id c) { return rules::RelicPlace{rules::RelicPlace::Hero, c, 0, w.character(c)->faction}; }

// Окно «куда и сколько» или «сколько»: количество (щелчок по полю, ввод, Tab).
bool setPopupCount(Harness& h, const std::string& mark, const std::string& value) {
  if (!h.clickUi(mark + ".count")) return false;
  h.retype(value);
  h.key(Key::Tab);
  h.settle();
  return true;
}

}  // namespace

// ---------------------------------------------------------------- инвентарь государства
TEST(app_relics_state_inventory) {
  Harness h("relics_state", 1600, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id pid = provinceOf(h->world(), st);
  const std::vector<Id> heroes = stateHeroes(h->world(), st);
  CHECK(st && pid && heroes.size() >= 1);
  Id store = 0, ring = 0, crown = 0, shard = 0;
  CHECK(h->act("test", [&](Tx& tx) {
    store = addStore(tx, pid, "Сокровищница");
    ring = rules::addRelic(tx, "Кольцо Странника", Rarity::Common);
    crown = rules::addRelic(tx, "Корона Зари", Rarity::Legendary);
    shard = rules::addRelic(tx, "Черепок Шантири", Rarity::Rare);
    rules::setRelicGroup(tx, shard, tx.w().catalogs->relicGroupId(schema::kRelicArchFinds));
    rules::setRelicCount(tx, ring, statePlace(st), 6);
    rules::setRelicCount(tx, shard, statePlace(st), 2);
    rules::moveRelic(tx, crown, statePlace(st));
  }));
  openTab(h, st, archui::kTabFaction);
  h.settle();
  const std::string rs = std::to_string(ring);
  CHECK(ensureVisible(h, "arch.relic." + rs));
  CHECK(h->uiRect("arch.relic.count." + rs) != nullptr);              // администратор: количество
  CHECK(h->uiRect("arch.relic.count." + std::to_string(crown)) == nullptr);   // у уникальной — нет
  h.move(8, 500);
  h.settle();
  CHECK(h.shot("relics_state_inventory"));
  // Отдать герою две штуки.
  CHECK(h.clickUi("arch.relic.give." + rs));
  h.settle();
  CHECK(h.shot("relics_state_give"));
  CHECK(setPopupCount(h, "arch.giveTo." + rs, "2"));
  CHECK(h.clickUi("arch.giveTo." + rs + ".0"));
  h.settle();
  Id got = 0;
  for (Id c : heroes)
    if (relicCount(h->world().character(c)->inventory, ring) == 2) got = c;
  CHECK(got != 0);
  CHECK_EQ(rules::relicCountAt(h->world(), ring, statePlace(st)), i64(4));
  // Положить три штуки в хранилище.
  CHECK(ensureVisible(h, "arch.relic.store." + rs));
  CHECK(h.clickUi("arch.relic.store." + rs));
  h.settle();
  CHECK(setPopupCount(h, "arch.storeTo." + rs, "3"));
  CHECK(h.clickUi("arch.storeTo." + rs + ".0"));
  h.settle();
  CHECK_EQ(rules::relicCountAt(h->world(), ring, storePlace(pid, store, st)), i64(3));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, statePlace(st)), i64(1));
  // Находка археологов — герою из инвентаря своего государства.
  const std::string ss = std::to_string(shard);
  CHECK(ensureVisible(h, "arch.relic.give." + ss));
  CHECK(h.clickUi("arch.relic.give." + ss));
  h.settle();
  CHECK(h.clickUi("arch.giveTo." + ss + ".0"));
  h.settle();
  CHECK_EQ(rules::relicCountAt(h->world(), shard, statePlace(st)), i64(1));
  // Количество (правка мира): 1 → 5.
  CHECK(typeNumber(h, "arch.relic.count." + rs, "5"));
  h.settle();
  CHECK_EQ(rules::relicCountAt(h->world(), ring, statePlace(st)), i64(5));
  // Новая из справочника и «Убрать».
  CHECK(pickInCombo(h, "arch.relic.add", "Посох странника"));
  h.settle();
  const Id staff = test::idByName(h->world().catalogs->relics, "Посох странника");
  CHECK(staff != 0);
  CHECK_EQ(rules::relicCountAt(h->world(), staff, statePlace(st)), i64(1));
  CHECK(clickIn(h, "arch.relic.release." + std::to_string(staff)));
  h.settle();
  CHECK_EQ(rules::relicCountAt(h->world(), staff, statePlace(st)), i64(0));
  // Игрок: раздача есть, количества, «Убрать» и справочника нет.
  h->ui.role = app::Role::Player;
  h->ui.playerState = st;
  h.settle();
  CHECK(ensureVisible(h, "arch.relic." + rs));
  CHECK(h->uiRect("arch.relic.give." + rs) != nullptr);
  CHECK(h->uiRect("arch.relic.count." + rs) == nullptr);
  CHECK(h->uiRect("arch.relic.release." + rs) == nullptr);
  CHECK(h->uiRect("arch.relic.add") == nullptr);
  CHECK(h.clickUi("arch.relic.give." + rs));
  h.settle();
  CHECK(h.clickUi("arch.giveTo." + rs + ".0"));
  h.settle();
  CHECK_EQ(rules::relicCountAt(h->world(), ring, statePlace(st)), i64(4));
  h.move(8, 500);
  h.settle();
  CHECK(h.shot("relics_state_player"));
  // Чужое государство — только просмотр.
  Id other = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!other && f.isState() && f.id != st) other = f.id;
  });
  h->ui.role = app::Role::Admin;
  CHECK(h->act("test", [&](Tx& tx) { rules::setRelicCount(tx, ring, statePlace(other), 2); }));
  h->ui.role = app::Role::Player;
  h.dropToasts();
  openTab(h, other, archui::kTabFaction);
  h.settle();
  CHECK(ensureVisible(h, "arch.relic." + rs));
  CHECK(h->uiRect("arch.relic.give." + rs) == nullptr);
  CHECK(h->uiRect("arch.relic.store." + rs) == nullptr);
}

// ---------------------------------------------------------------- хранилище реликвий постройки
TEST(app_relics_store_stacks) {
  Harness h("relics_store", 1440, 1100);
  h.demo();
  h.waitMap();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id pid = provinceOf(h->world(), st);
  Id store = 0, ring = 0, cup = 0;
  CHECK(h->act("test", [&](Tx& tx) {
    store = addStore(tx, pid, "Сокровищница");
    ring = rules::addRelic(tx, "Кольцо Странника", Rarity::Common);
    cup = rules::addRelic(tx, "Кубок королей", Rarity::Legendary);
    rules::setRelicCount(tx, ring, statePlace(st), 8);
    rules::moveRelic(tx, cup, statePlace(st));
  }));
  h->ui.tabOf[app::SelType::Province] = "province.buildings";
  h->select(app::SelType::Province, pid);
  h.settle();
  h.dropToasts();
  const std::string mark = "prov.store." + std::to_string(store);
  const rules::RelicPlace here = storePlace(pid, store, st);
  // Положить: сколько — поле рядом (5), из инвентаря государства.
  CHECK(typeNumber(h, mark + ".putCount", "5"));
  CHECK(pickInCombo(h, mark + ".put", "Кольцо Странника"));
  h.settle();
  CHECK_EQ(rules::relicCountAt(h->world(), ring, here), i64(5));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, statePlace(st)), i64(3));
  // Уникальная — одна штука, сколько бы ни было в поле.
  CHECK(pickInCombo(h, mark + ".put", "Кубок королей"));
  h.settle();
  CHECK_EQ(rules::relicCountAt(h->world(), cup, here), i64(1));
  CHECK(ensureVisible(h, mark + ".relic." + std::to_string(ring)));
  h.dropToasts();
  h.move(300, 600);
  h.settle();
  CHECK(h.shot("relics_store"));
  // Вынуть две из пяти — окно «Сколько».
  CHECK(clickIn(h, mark + ".take." + std::to_string(ring)));
  h.settle();
  CHECK(setPopupCount(h, mark + ".takeN", "2"));
  CHECK(h.clickUi(mark + ".takeN.ok"));
  h.settle();
  CHECK_EQ(rules::relicCountAt(h->world(), ring, here), i64(3));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, statePlace(st)), i64(5));
  // Уникальная вынимается сразу.
  CHECK(clickIn(h, mark + ".take." + std::to_string(cup)));
  h.settle();
  CHECK(rules::relicPlace(h->world(), cup).kind == rules::RelicPlace::State);
  // Количество (администратор): 3 → 10.
  CHECK(typeNumber(h, mark + ".count." + std::to_string(ring), "10"));
  h.settle();
  CHECK_EQ(rules::relicCountAt(h->world(), ring, here), i64(10));
  h->undo();
  h.step();
  CHECK_EQ(rules::relicCountAt(h->world(), ring, here), i64(3));
  // Игрок: вынуть и положить есть, количества нет.
  h->ui.role = app::Role::Player;
  h->ui.playerState = st;
  h.settle();
  CHECK(ensureVisible(h, mark + ".relic." + std::to_string(ring)));
  CHECK(h->uiRect(mark + ".count." + std::to_string(ring)) == nullptr);
  CHECK(h->uiRect(mark + ".take." + std::to_string(ring)) != nullptr);
  CHECK(h->uiRect(mark + ".put") != nullptr);
}

// ---------------------------------------------------------------- справочник реликвий: места и количества
TEST(app_relics_catalog_places) {
  Harness h("relics_catalog", 1600, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id pid = provinceOf(h->world(), st);
  const std::vector<Id> heroes = stateHeroes(h->world(), st);
  CHECK(heroes.size() >= 2);
  Id store = 0, ring = 0, crown = 0;
  CHECK(h->act("test", [&](Tx& tx) {
    store = addStore(tx, pid, "Сокровищница");
    ring = rules::addRelic(tx, "Кольцо Странника", Rarity::Common);
    crown = rules::addRelic(tx, "Корона Зари", Rarity::Legendary);
    rules::setRelicCount(tx, ring, heroPlace(tx.w(), heroes[0]), 3);
    rules::setRelicCount(tx, ring, heroPlace(tx.w(), heroes[1]), 1);
    rules::setRelicCount(tx, ring, statePlace(st), 4);
    rules::setRelicCount(tx, ring, storePlace(pid, store, st), 2);
  }));
  // Справочник — на реликвии: четыре места, 10 штук.
  h->select(app::SelType::Character, heroes[0]);
  h->ui.tabOf[app::SelType::Character] = "character.info";
  h.settle();
  CHECK(ensureVisible(h, "character.relic." + std::to_string(ring)));
  CHECK(h.clickUi("character.relic." + std::to_string(ring)));   // щелчок по строке — справочник на реликвии
  ed::quick(h);
  CHECK_EQ(h->ui.editor, std::string("catalogs"));
  CHECK(ed::reveal(h, "catalogs.relicCard.places"));
  {
    i64 total = 0;
    for (const rules::RelicAt& at : rules::relicPlaces(h->world(), ring)) total += at.count;
    CHECK_EQ(total, i64(10));
  }
  h.move(8, 500);
  h.settle();
  CHECK(h.shot("relics_catalog_places"));
  // Строка таблицы: «4 места · 10 шт.».
  CHECK(h->uiRect("catalogs.relic." + std::to_string(ring) + ".place") != nullptr);
  // Количество у выбранного государства и героя.
  CHECK(ed::pickInCombo(h, "catalogs.relicCard.state", h->world().faction(st)->name));
  CHECK(ed::enterValue(h, "catalogs.relicCard.stateCount", "6"));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, statePlace(st)), i64(6));
  CHECK(ed::pickInCombo(h, "catalogs.relicCard.hero", h->world().character(heroes[1])->name));
  CHECK(ed::enterValue(h, "catalogs.relicCard.heroCount", "0"));
  CHECK_EQ(relicCount(h->world().character(heroes[1])->inventory, ring), i64(0));
  {
    int places = 0;
    for (const rules::RelicAt& at : rules::relicPlaces(h->world(), ring)) places += at.count > 0 ? 1 : 0;
    CHECK_EQ(places, 3);
  }
  // Уникальная: «У героя» передаёт её.
  h->undo();
  h->undo();
  h.step();
  CHECK_EQ(rules::relicCountAt(h->world(), ring, statePlace(st)), i64(4));
  h->select(app::SelType::Character, heroes[0]);
  h.settle();
  h->openEditor("catalogs", 8);
  ed::quick(h);
  CHECK(h.clickUi("catalogs.search"));
  h.type("Корона Зари");
  ed::quick(h);
  CHECK(ed::reveal(h, "catalogs.relicCard.holder"));
  CHECK(ed::pickInCombo(h, "catalogs.relicCard.holder", h->world().character(heroes[1])->name));
  CHECK_EQ(rules::relicHolder(h->world(), crown), heroes[1]);
  h.move(8, 500);
  h.settle();
  CHECK(h.shot("relics_catalog_unique"));
}

// Игроку справочники закрыты: редактор не открывается (или показывает замок).
TEST(app_relics_catalog_player_locked) {
  Harness h("relics_catalog_player");
  h.demo();
  h.dropToasts();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  h->ui.role = app::Role::Player;
  h->ui.playerState = st;
  h->openEditor("catalogs", 8);
  ed::quick(h);
  CHECK(h->ui.editor != "catalogs" || h->uiRect("catalogs.locked") != nullptr);
  CHECK(h->uiRect("catalogs.table") == nullptr);
}
