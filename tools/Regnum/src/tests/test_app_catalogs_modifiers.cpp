// Сценарии модификаторов записей справочников (аудит 2026-10-23, п.7.1, 7.4, 7.5): карточки расы, культуры и формы
// правления (модификаторы записи с выбором по виду: раса — «Для провинций», форма правления — «Глобальный»), реликвии
// (модификаторы героя-носителя, встроенные — с созданием записи), класса героя; окно модификаторов — группы
// эффектов по виду модификатора и «Где используется» с записями справочников.
#include "app/common/relics.h"
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;

namespace {

const CatalogItem* itemNamed(const std::vector<CatalogItem>& list, std::string_view name) {
  return Catalogs::find(list, test::idByName(list, name));
}

// Вкладка справочника tab, запись по поиску query (первая найденная выбрана).
void openItem(Harness& h, Id tab, const std::string& query) {
  h->openEditor("catalogs", tab);
  quick(h);
  h.key(Key::F, ctrl());
  h.type(query);
  quick(h);
}

// Убрать фишку списка крестиком (правый край фишки).
bool removeChip(Harness& h, const std::string& mark) {
  if (!reveal(h, mark)) return false;
  const RectF* r = h->uiRect(mark);
  if (!r) return false;
  const RectF x = *r;
  h.click(x.right() - 9, x.cy());
  quick(h);
  return true;
}

}  // namespace

TEST(app_catalogs_modifiers_trait_cards) {
  Harness h("catalogs_trait_mods", 1600, 1000);
  h.demo();
  h.dropToasts();
  // Свой модификатор провинции и глобальный: раса предлагает первый, форма правления — второй.
  Id prov = 0, glob = 0;
  CHECK(h->act("Модификаторы", [&](Tx& tx) {
    prov = rules::createModifier(tx, "Плодородные долины");
    tx.modifier(prov).kind = ModKind::Province;
    tx.modifier(prov).fxMask = 1u << int(Fx::PopGrowthPct);
    tx.modifier(prov).fx[size_t(Fx::PopGrowthPct)] = 1;
    glob = rules::createModifier(tx, "Плодородная казна");
    tx.modifier(glob).kind = ModKind::Faction;
    tx.modifier(glob).fxMask = 1u << int(Fx::IncomePct);
    tx.modifier(glob).fx[size_t(Fx::IncomePct)] = 3;
  }));
  // Раса «Люди»: базовый модификатор расы — фишкой; крестик убирает, выбор добавляет модификатор провинции.
  openItem(h, 2, "Люди");
  CHECK(reveal(h, "catalogs.item.mods"));
  const Id base = itemNamed(h->world().catalogs->races, "Люди")->modifiers.at(0);
  CHECK(h->world().modifier(base)->name == "Раса: Люди");
  shotClean(h, "catalogs_race_mods");
  CHECK(removeChip(h, "catalogs.item.mods.chip.0"));
  CHECK(itemNamed(h->world().catalogs->races, "Люди")->modifiers.empty());
  CHECK(pickInCombo(h, "catalogs.item.mods", "Плодородн"));
  CHECK((itemNamed(h->world().catalogs->races, "Люди")->modifiers == std::vector<Id>{prov}));   // глобальный не предлагается
  h.key(Key::Z, ctrl());
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK((itemNamed(h->world().catalogs->races, "Люди")->modifiers == std::vector<Id>{base}));
  // Форма правления «Монархия»: глобальный модификатор формы правления; модификатор провинции не предлагается.
  openItem(h, 5, "Монархия");
  CHECK(reveal(h, "catalogs.item.mods"));
  CHECK(pickInCombo(h, "catalogs.item.mods", "Плодородн"));
  const CatalogItem* mon = itemNamed(h->world().catalogs->governments, "Монархия");
  CHECK(mon && mon->modifiers.size() == 2 && mon->modifiers[1] == glob);
  // Культура: модификатор записи из базовых — «Культура: …».
  openItem(h, 3, "Нордлинги");
  CHECK(reveal(h, "catalogs.item.mods"));
  CHECK(h->uiRect("catalogs.item.mods.chip.0") != nullptr);
  shotClean(h, "catalogs_culture_mods");
}

TEST(app_catalogs_modifiers_relic_and_class) {
  Harness h("catalogs_relic_class_mods", 1600, 1000);
  h.demo();
  h.dropToasts();
  const Id sword = test::idByName(h->world().catalogs->relics, "Стальной меч");
  CHECK(sword != 0);
  app::relics::openCatalog(h.a(), sword);
  quick(h);
  CHECK(reveal(h, "catalogs.relicCard.mods"));
  CHECK(h->uiRect("catalogs.relicCard.mods.chip.0") != nullptr);   // «Реликвия: оружие · обычная»
  // Встроенный модификатор героя без записи мира — запись создаётся и добавляется.
  CHECK(pickInCombo(h, "catalogs.relicCard.mods", "Бдительный"));
  const Relic* r = h->world().relic(sword);
  CHECK(r && r->modifiers.size() == 2);
  if (r && r->modifiers.size() == 2) CHECK_EQ(h->world().modifier(r->modifiers[1])->key, std::string(schema::mod::Vigilant));
  shotClean(h, "catalogs_relic_mods");
  // Класс «Ассасин»: модификатор класса.
  h->openEditor("catalogs", 10);
  quick(h);
  h.key(Key::F, ctrl());
  h.type("Ассасин");
  quick(h);
  CHECK(reveal(h, "catalogs.classCard.mods"));
  CHECK(h->uiRect("catalogs.classCard.mods.chip.0") != nullptr);
  const HeroClass* k = h->world().heroClass(test::idByName(h->world().catalogs->classes, "Ассасин"));
  CHECK(k && k->modifiers.size() == 1);
  CHECK(removeChip(h, "catalogs.classCard.mods.chip.0"));
  CHECK(h->world().heroClass(k->id)->modifiers.empty());
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK_EQ(h->world().heroClass(k->id)->modifiers.size(), size_t(1));
}

TEST(app_catalogs_modifiers_editor_groups) {
  Harness h("catalogs_mod_groups", 1600, 1000);
  h.demo();
  h.dropToasts();
  auto named = [&](const char* name) { return h->world().modifiers.firstId([&](const Modifier& m) { return m.name == name; }); };
  // Модификатор расы (для провинций): локальные и глобальные эффекты (глобальные — владельцу), без эффектов героя;
  // «Где используется» — раса.
  h->openEditor("modifiers", named("Раса: Люди"));
  quick(h);
  CHECK(h->uiRect("modifiers.local") != nullptr && h->uiRect("modifiers.global") != nullptr);
  CHECK(h->uiRect("modifiers.hero") == nullptr);
  CHECK(reveal(h, "modifiers.usage.races"));
  // Талант (для героев): все группы; «Где используется» — таланты по подклассам.
  h->openEditor("modifiers", named("Талант: Доблесть"));
  quick(h);
  CHECK(h->uiRect("modifiers.hero") != nullptr && h->uiRect("modifiers.army") != nullptr && h->uiRect("modifiers.local") != nullptr);
  CHECK(reveal(h, "modifiers.usage.talents"));
  shotClean(h, "catalogs_mod_talent_usage");
  // Реликвия и вера: свои группы «Где используется».
  h->openEditor("modifiers", named("Реликвия: оружие · обычная"));
  quick(h);
  CHECK(reveal(h, "modifiers.usage.relics"));
  h->openEditor("modifiers", named("Вера: Свет"));
  quick(h);
  CHECK(reveal(h, "modifiers.usage.religions"));
}
