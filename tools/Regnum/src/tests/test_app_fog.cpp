// Туман войны (аудит 2026-10-23, 1.5; App::knows): игрок видит подробности только своего государства, союзников,
// сюзерена и вассалов. У чужих — «Скрыто»: казна в шапке, экономика, совет, таблицы войск (только объекты на карте, без
// численности), добыча, налоги и стройки провинций, состав гарнизона («есть гарнизон»), содержание и инвентари героев;
// в каталоге государств — замки вместо казны, дохода и войск; в списке «Войска и флот» — численность. Администратор
// видит всё.
#include "app/shell/role.h"
#include "tests/test_app_economy_util.h"
#include "tests/test_app_faction_util.h"
#include "tests/test_app_military_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;
using namespace rg::econtest;
namespace role = rg::app::detail::role;

namespace {

constexpr const char* kOwn = "Королевство Альмарин";
constexpr const char* kAlly = "Северный союз Хельдвиг";       // союзник Альмарина в демонстрационном мире
constexpr const char* kForeign = "Республика Корвен";

// Значок вкладки: TabDef::badge вкладки id для сущности.
int tabBadge(Harness& h, const char* id, Id entity) {
  for (const app::TabDef& t : app::tabs())
    if (std::string_view(t.id) == id && t.badge) return t.badge(h.a(), entity);
  return 0;
}

// Вкладка фракции без прокрутки к низу (таблица войск — вверху вкладки).
void topTab(Harness& h, Id faction, const char* tab) {
  h->ui.tabOf[app::SelType::Faction] = tab;
  h->select(app::SelType::Faction, faction);
  h.settle();
  h.dropToasts();
}

// Войско, которым управляет фракция (и союзное, где она лидер).
Id leadArmy(const World& w, Id faction) {
  return test::idWhere(w.armies, [&](const Army& a) { return !a.isFleet() && a.leader() == faction; });
}

// Провинция государства со стройкой (0 — нет).
Id buildingProvince(const World& w, Id state) {
  return test::idWhere(w.provinces, [&](const Province& p) {
    if (p.owner != state) return false;
    for (const ProvBuilding& pb : p.buildings)
      if (pb.constructing) return true;
    return false;
  });
}

}  // namespace

// Страницы государства: чужое — «Скрыто», союзник и своё — всё видно; у администратора — всё.
TEST(app_fog_faction_pages) {
  Harness h("fog_faction", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id own = findFaction(h->world(), kOwn), ally = findFaction(h->world(), kAlly), foreign = findFaction(h->world(), kForeign);
  CHECK(own && ally && foreign);
  CHECK(h->world().relation(own, ally).s == RelStatus::Alliance);
  role::becomePlayer(h.a(), own);
  h.settle();
  CHECK(h->knows(own));
  CHECK(h->knows(ally));
  CHECK(!h->knows(foreign));
  CHECK(h->knows(0));
  // Экономика чужого: шапка без казны, вкладка — «Скрыто».
  openTab(h, foreign, "faction.economy");
  CHECK(h->uiRect("faction.treasury") == nullptr);
  CHECK(h->uiRect("faction.treasury.hidden") != nullptr);
  CHECK(h->uiRect("fog.hidden") != nullptr);
  CHECK(h->uiRect("economy.treasury") == nullptr);
  CHECK_EQ(tabBadge(h, "faction.economy", foreign), 0);
  shotClean(h, "fog_foreign_economy");
  // Союзник: казна и экономика видны.
  openTab(h, ally, "faction.economy");
  CHECK(h->uiRect("faction.treasury") != nullptr);
  CHECK(h->uiRect("economy.treasury") != nullptr);
  CHECK(h->uiRect("fog.hidden") == nullptr);
  // Совет: чужой скрыт, свой виден.
  openTab(h, foreign, "faction.council");
  CHECK(h->uiRect("fog.hidden") != nullptr);
  CHECK(h->uiRect("council.who.0") == nullptr);
  openTab(h, own, "faction.council");
  CHECK(h->uiRect("council.who.0") != nullptr);
  // Войска чужого: только объекты на карте.
  // Строка таблицы: узкая — «mil.row.N», широкая (страница) — её ячейки («mil.row.N.upkeep»).
  auto rowShown = [&] { return h->uiRect("mil.row.0") != nullptr || h->uiRect("mil.row.0.upkeep") != nullptr; };
  topTab(h, foreign, "faction.army");
  CHECK(h->uiRect("mil.army.hidden") != nullptr);
  CHECK(!rowShown());
  const Id farmy = armyOf(h->world(), foreign, ArmyKind::Army);
  if (farmy) CHECK(h->uiRect("mil.object." + std::to_string(farmy)) != nullptr);
  shotClean(h, "fog_foreign_army");
  topTab(h, ally, "faction.army");
  CHECK(h->uiRect("mil.army.hidden") == nullptr);
  CHECK(rowShown());
  // Каталог государств: у чужих — замки вместо казны, дохода и войск; у своего и союзника — числа.
  h->openDirectory("states");
  h.settle();
  CHECK(h->uiRect("states.hidden." + std::to_string(foreign)) != nullptr);
  CHECK(h->uiRect("states.hidden." + std::to_string(own)) == nullptr);
  CHECK(h->uiRect("states.hidden." + std::to_string(ally)) == nullptr);
  shotClean(h, "fog_states_catalog");
  // Администратор — всё.
  role::becomeAdmin(h.a());
  h->toMap();
  h.settle();
  CHECK(h->knows(foreign));
  openTab(h, foreign, "faction.economy");
  CHECK(h->uiRect("economy.treasury") != nullptr);
  CHECK(h->uiRect("faction.treasury") != nullptr);
  h->openDirectory("states");
  h.settle();
  CHECK(h->uiRect("states.hidden." + std::to_string(foreign)) == nullptr);
}

// Провинции и войска чужого государства: добыча, налоги, доход и стройки скрыты, гарнизон — только «есть гарнизон»;
// в списке «Войска и флот» численность чужих скрыта, у своих — отметка очка действий и отбор «Может ходить».
TEST(app_fog_provinces_and_forces) {
  Harness h("fog_provinces", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id own = findFaction(h->world(), kOwn), foreign = findFaction(h->world(), kForeign);
  CHECK(own && foreign);
  const Id fp = populousProvince(h->world(), foreign);
  CHECK(fp != 0);
  // Гарнизон у провинции чужого — есть, чтобы видеть «есть гарнизон».
  CHECK(h->act("Гарнизон", [&](Tx& tx) {
    const Faction* f = tx.w().faction(foreign);
    if (f && !f->army.empty()) tx.province(fp).garrison.push_back(GarrisonEntry{f->army[0].id, 10});
  }));
  const Id bp = buildingProvince(h->world(), foreign);
  role::becomePlayer(h.a(), own);
  h.settle();
  openProvince(h, fp, "province.economy");
  CHECK(h->uiRect("province.production") == nullptr);
  CHECK(h->uiRect("province.localTax") == nullptr);
  CHECK(h->uiRect("fog.hidden") != nullptr);
  shotClean(h, "fog_foreign_province_economy");
  openProvince(h, fp, "province.garrison");
  CHECK(h->uiRect("garrison.exists") != nullptr);
  CHECK(h->uiRect("garrison.loyalty") == nullptr);
  shotClean(h, "fog_foreign_garrison");
  if (bp) CHECK_EQ(tabBadge(h, "province.buildings", bp), 0);
  // Своя провинция — всё видно.
  const Id op = populousProvince(h->world(), own);
  openProvince(h, op, "province.economy");
  CHECK(h->uiRect("province.production") != nullptr);
  CHECK(h->uiRect("province.localTax") != nullptr);
  // «Войска и флот»: численность чужих скрыта; свои — с отметкой очка действий, отбор «Может ходить».
  h->openDrawer("military");
  h.settle();
  const Id farmy = armyOf(h->world(), foreign, ArmyKind::Army);
  const Id oarmy = leadArmy(h->world(), own);
  CHECK(farmy && oarmy);
  CHECK(h->uiRect("mil.drawer.hidden." + std::to_string(farmy)) != nullptr);
  CHECK(h->uiRect("mil.drawer.hidden." + std::to_string(oarmy)) == nullptr);
  CHECK(h->uiRect("mil.drawer.action." + std::to_string(oarmy)) != nullptr);
  CHECK(h->uiRect("mil.drawer.action." + std::to_string(farmy)) == nullptr);
  shotClean(h, "fog_drawer_player");
  CHECK(h.clickUi("mil.drawer.canMove"));
  h.settle();
  CHECK(h->uiRect("mil.drawer.item." + std::to_string(farmy)) == nullptr);
  CHECK(h->uiRect("mil.drawer.item." + std::to_string(oarmy)) != nullptr);
  // Объект, который уже ходил, из отбора уходит.
  CHECK(h->play("Ход", [&](Tx& tx) { rules::spendAction(tx, oarmy); }));
  h.settle();
  CHECK(h->uiRect("mil.drawer.item." + std::to_string(oarmy)) == nullptr);
  // Администратор (панель остаётся открытой): численность всех, без отметок и отбора.
  role::becomeAdmin(h.a());
  h.settle();
  CHECK(h->uiRect("mil.drawer.search") != nullptr);
  CHECK(h->uiRect("mil.drawer.canMove") == nullptr);
  CHECK(h->uiRect("mil.drawer.hidden." + std::to_string(farmy)) == nullptr);
  CHECK(h->uiRect("mil.drawer.action." + std::to_string(oarmy)) == nullptr);
}

// Герои чужого государства: содержание и инвентарь скрыты.
TEST(app_fog_heroes) {
  Harness h("fog_heroes", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id own = findFaction(h->world(), kOwn), foreign = findFaction(h->world(), kForeign);
  const Id hero = availableHero(h->world(), foreign);
  CHECK(own && foreign && hero);
  role::becomePlayer(h.a(), own);
  h.settle();
  h->ui.tabOf[app::SelType::Character] = "character.info";
  h->select(app::SelType::Character, hero);
  h.settle();
  CHECK(h->uiRect("character.upkeep") == nullptr);
  CHECK(h->uiRect("character.addRelic") == nullptr);
  CHECK(h->uiRect("fog.hidden") != nullptr);
  shotClean(h, "fog_foreign_hero");
  const Id mine = availableHero(h->world(), own);
  CHECK(mine != 0);
  h->select(app::SelType::Character, mine);
  h.settle();
  CHECK(h->uiRect("character.upkeep") != nullptr);
}
