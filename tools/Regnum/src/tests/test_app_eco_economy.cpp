// Вкладка «Экономика» государства после ТЗ 2026-10-07: ресурсы по группам справочника (сворачиваемые группы с
// суммами, поиск, «скрыть пустые»; «Золото», «Трупы» и «Демоническая энергия» — всегда первыми, их запас правится
// сразу — ТЗ «Исправления», п.5), провизия по группе «Провизия» (расход поровну, недостача — «Голод», что будет после
// хода — ТЗ «Добавления в справочники», п.3), эссенции элементов (запас, генерация построек, содержание элементалей,
// долг), постройки преобразования в столбцах прихода и расхода.
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

Id resByName(const World& w, std::string_view name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}
Id essByName(const World& w, std::string_view name) {
  for (const CatalogItem& c : w.catalogs->essences)
    if (c.name == name) return c.id;
  return 0;
}

// Поставить готовую постройку в любую провинцию государства, где хватает слотов; 0 — некуда.
Id placeAnywhere(Harness& h, Id st, Id b) {
  for (Id pid : h->world().provinces.ids()) {
    const Province* p = h->world().province(pid);
    if (!p || p->sea || p->owner != st) continue;
    if (h->act("Постройка", [&](Tx& tx) { rules::placeBuilding(tx, pid, b, 1); })) return pid;
  }
  return 0;
}

const rules::FactionCalc& calcOf(Harness& h, Id st) {
  static std::shared_ptr<const rules::Calc> keep;
  keep = rules::calc(h->world());
  return *keep->faction(st);
}

}  // namespace

// ---------------------------------------------------------------- ресурсы по группам
TEST(app_eco_economy_resource_groups) {
  HideTestRegs hide;
  Harness h("eco_economy_groups", 1440, 1300);
  h.demo();
  const World& w0 = h->world();
  const Id st = biggestState(w0);
  const Id corpses = w0.catalogs->resourceId(schema::kResCorpses), energy = w0.catalogs->resourceId(schema::kResEnergy);
  CHECK(st && corpses && energy);
  CHECK(h->world().faction(st)->stock(corpses) == 0 && h->world().faction(st)->stock(energy) == 0);
  const Id gOre = w0.catalogs->groupId(schema::grp::Ore), gBeasts = w0.catalogs->groupId(schema::grp::Beasts);
  const Id gProv = w0.catalogs->groupId(schema::grp::Provisions), gMat = w0.catalogs->groupId(schema::grp::Materials);
  const Id iron = resByName(w0, "Железо"), grain = resByName(w0, "Зерно"), titan = resByName(w0, "Титан");
  CHECK(gOre && gBeasts && gProv && gMat && iron && grain && titan);
  openTab(h, st, "faction.economy");
  // Первая группа видна внизу — строки над ней тоже видны.
  CHECK(ensureVisible(h, "economy.group." + std::to_string(gOre)));
  // Золото, трупы и демоническая энергия — первыми (выше любой группы), запас — поле в строке.
  const RectF* rg = h->uiRect("economy.res.1");
  const RectF* rc = h->uiRect("economy.res." + std::to_string(corpses));
  const RectF* re = h->uiRect("economy.res." + std::to_string(energy));
  const RectF* grp = h->uiRect("economy.group." + std::to_string(gOre));
  CHECK(rg && rc && re && grp);
  if (rg && rc && re && grp) {
    CHECK(rg->y < rc->y && rc->y < re->y && re->y < grp->y);
  }
  CHECK(h->uiRect("economy.stock." + std::to_string(corpses)) != nullptr);
  CHECK(h->uiRect("economy.stock." + std::to_string(energy)) != nullptr);
  // Начальные значения — сразу, без первого начисления.
  CHECK(typeNumber(h, "economy.stock." + std::to_string(corpses), "250"));
  CHECK_NEAR(h->world().faction(st)->stock(corpses), 250, 1e-9);
  CHECK(typeNumber(h, "economy.stock." + std::to_string(energy), "1234,5"));
  CHECK_NEAR(h->world().faction(st)->stock(energy), 1234.5, 1e-9);
  // Пустые ресурсы по умолчанию скрыты (видно, что у государства есть): группы с запасами раскрыты — «Руда»
  // (железо), «Провизия» (зерно), «Материалы» (древесина); пустой «Титан» не виден.
  for (Id g : {gOre, gProv, gMat}) CHECK(ensureVisible(h, "economy.group." + std::to_string(g)));
  CHECK(ensureVisible(h, "economy.res." + std::to_string(grain)));
  CHECK(ensureVisible(h, "economy.res." + std::to_string(iron)));
  CHECK(h->uiRect("economy.res." + std::to_string(titan)) == nullptr);
  shotClean(h, "eco_economy_groups");
  // Свернуть «Руду» щелчком по строке группы: её ресурсов не видно; ещё щелчок — снова видны.
  CHECK(clickIn(h, "economy.group." + std::to_string(gOre)));
  h.settle();
  CHECK(h->uiRect("economy.res." + std::to_string(iron)) == nullptr);
  CHECK(clickIn(h, "economy.group." + std::to_string(gOre)));
  h.settle();
  CHECK(ensureVisible(h, "economy.res." + std::to_string(iron)));
  // Суммы группы: запас «Руды» — сумма запасов её ресурсов (с подгруппами).
  {
    double sum = 0;
    for (Id r : rules::resourcesIn(h->world(), gOre)) sum += h->world().faction(st)->stock(r);
    CHECK(sum > 0);
  }
  // Показать все (ссылка «Пустые ресурсы: N» под таблицей): «Титан» и группа «Звери» видны.
  CHECK(clickIn(h, "economy.showEmpty"));
  h.settle();
  CHECK(h->uiRect("economy.showEmpty") == nullptr);
  CHECK(ensureVisible(h, "economy.res." + std::to_string(titan)));
  CHECK(ensureVisible(h, "economy.group." + std::to_string(gBeasts)));
  shotClean(h, "eco_economy_all");
  // Снова скрыть пустые: «Титан» спрятан, трупы и энергия (даже пустые) — нет.
  CHECK(clickIn(h, "economy.hideEmpty"));
  h.settle();
  CHECK(h->uiRect("economy.res." + std::to_string(titan)) == nullptr);
  CHECK(ensureVisible(h, "economy.res." + std::to_string(corpses)));
  // Поиск «желез» (находит и пустые): Железо, Черное железо, Алое железо; трупов не видно.
  CHECK(clickIn(h, "economy.search"));
  h.type("желез");
  h.settle();
  CHECK(ensureVisible(h, "economy.res." + std::to_string(iron)));
  CHECK(h->uiRect("economy.res." + std::to_string(resByName(h->world(), "Черное железо"))) != nullptr);
  CHECK(h->uiRect("economy.res." + std::to_string(corpses)) == nullptr);
  CHECK(h->uiRect("economy.group." + std::to_string(gBeasts)) == nullptr);
  shotClean(h, "eco_economy_search");
  h.key(Key::A, ctrl());
  h.key(Key::Backspace);
  h.settle();
  // Узкая панель справа от карты: столбцы «Запас» и «Итого» (приход, торговля и расход — в подсказке итога).
  h->setPage(false);
  h.settle();
  CHECK(ensureVisible(h, "economy.res." + std::to_string(grain)));
  CHECK(h->uiRect("economy.net." + std::to_string(grain)) != nullptr);
  CHECK(h->uiRect("economy.use." + std::to_string(grain)) == nullptr);
  CHECK(h->uiRect("economy.in." + std::to_string(grain)) == nullptr);
  shotClean(h, "eco_economy_narrow");
  h->setPage(true);
  h.settle();
  // У нового государства без запасов трупы и энергия видны сразу.
  Id fresh = 0;
  CHECK(h->act("Новое государство", [&](Tx& tx) { fresh = rules::createFaction(tx, FactionKind::State, "Пустое княжество"); }));
  openTab(h, fresh, "faction.economy");
  CHECK(ensureVisible(h, "economy.stock." + std::to_string(corpses)));
  CHECK(ensureVisible(h, "economy.stock." + std::to_string(energy)));
  shotClean(h, "eco_economy_fresh_state");
}

// ---------------------------------------------------------------- провизия и голод
TEST(app_eco_economy_provision) {
  HideTestRegs hide;
  Harness h("eco_economy_provision", 1440, 1300);
  h.demo();
  const Id st = biggestState(h->world());
  const Id grain = resByName(h->world(), "Зерно"), fish = resByName(h->world(), "Рыба");
  CHECK(st && grain && fish);
  // Ни одна провинция государства не добывает провизию — расход только из запасов.
  CHECK(h->act("Без добычи провизии", [&](Tx& tx) {
    const std::vector<Id> provs = rules::provisionResources(tx.w());
    for (Id pid : tx.w().provinces.ids()) {
      const Province* p = tx.w().province(pid);
      if (p->owner == st && std::find(provs.begin(), provs.end(), p->resource) != provs.end()) tx.province(pid).resource = 0;
    }
    for (Id r : provs) tx.faction(st).res.erase(r);
    tx.faction(st).res[grain] = 300;
    tx.faction(st).res[fish] = 100000;
  }));
  const rules::FactionCalc& fc = calcOf(h, st);
  const double need = fc.provisionNeed;
  CHECK(need > 200);
  // Два ресурса провизии: поровну (у зерна меньше доли — отдаёт всё, остальное — рыба).
  const double g = fc.resources.at(grain).consumption, f = fc.resources.at(fish).consumption;
  CHECK_NEAR(g + f, need, 1e-6);
  CHECK_NEAR(g, std::min(300.0, need / 2), 1e-6);
  CHECK(fc.provisionDebtNext == 0);
  openTab(h, st, "faction.economy");
  for (const char* m : {"economy.provision", "economy.provision.stock", "economy.provision.need", "economy.provision.debt", "economy.provision.after"})
    CHECK_MSG(ensureVisible(h, m), m);
  CHECK(ensureVisible(h, "economy.provision.use." + std::to_string(grain)));
  CHECK(ensureVisible(h, "economy.provision.use." + std::to_string(fish)));
  // Столбец расхода в таблице ресурсов: у обоих ресурсов провизии.
  CHECK(ensureVisible(h, "economy.use." + std::to_string(grain)));
  CHECK(ensureVisible(h, "economy.use." + std::to_string(fish)));
  CHECK(h->uiRect("economy.famine") == nullptr && h->uiRect("economy.famineNext") == nullptr);
  shotClean(h, "eco_economy_provision_two");
  // Один ресурс провизии и его не хватает: весь расход — с него, после хода — недостача, «Голод после хода».
  CHECK(h->act("Только зерно", [&](Tx& tx) {
    tx.faction(st).res.erase(fish);
    tx.faction(st).res[grain] = 50;
  }));
  const rules::FactionCalc& fc2 = calcOf(h, st);
  CHECK_NEAR(fc2.resources.at(grain).consumption, 50, 1e-9);
  CHECK_NEAR(fc2.provisionDebtNext, fc2.provisionNeed - 50, 1e-6);
  openTab(h, st, "faction.economy");
  CHECK(h->uiRect("economy.famineNext") != nullptr);
  CHECK(ensureVisible(h, "economy.provision.debtNext"));
  shotClean(h, "eco_economy_provision_short");
  // Недостача прошлых ходов — «Голод» сейчас.
  CHECK(h->act("Недостача", [&](Tx& tx) { tx.faction(st).provisionDebt = 12.5; }));
  openTab(h, st, "faction.economy");
  CHECK(h->uiRect("economy.famine") != nullptr);
  CHECK(calcOf(h, st).famine);
  // Государство нежити провизию не расходует: раздела нет.
  CHECK(h->act("Нежить", [&](Tx& tx) { rules::setStateKind(tx, st, StateKind::Undead); }));
  openTab(h, st, "faction.economy");
  CHECK(h->uiRect("economy.provision") == nullptr);
  CHECK(h->uiRect("economy.famine") == nullptr);
}

// ---------------------------------------------------------------- эссенции элементов
TEST(app_eco_economy_essences) {
  HideTestRegs hide;
  Harness h("eco_economy_essences", 1440, 1300);
  h.demo();
  const Id st = biggestState(h->world());
  const Id fire = essByName(h->world(), "Эссенция пламени"), water = essByName(h->world(), "Эссенция воды");
  const Id earth = essByName(h->world(), "Эссенция земли");
  CHECK(st && fire && water && earth);
  // Генерация: постройка генерации эссенции в провинции государства; содержание: элементали.
  Id b = 0;
  CHECK(h->act("Эссенции", [&](Tx& tx) {
    b = rules::createBuilding(tx, 0, "Источник пламени");
    rules::setBuildingRole(tx, b, rules::BuildingRole::Essence, true);
    rules::setLevelEssence(tx, b, 1, fire, 7);
    const Id row = rules::addArmyRow(tx, st, UnitType::Elementals, "Водные элементали", 10, 0);
    rules::setRowEssUpkeep(tx, st, row, water, 2);
  }));
  CHECK(placeAnywhere(h, st, b) != 0);
  const rules::FactionCalc& fc = calcOf(h, st);
  CHECK_NEAR(fc.essences.at(fire).generation, 7, 1e-9);
  CHECK(fc.essences.at(water).upkeep >= 20 - 1e-9);
  openTab(h, st, "faction.economy");
  CHECK(ensureVisible(h, "economy.ess"));
  // Раздел может быть свёрнут (пусто при первом показе) — раскрыть. Строка ниже видимой части не строится: сначала
  // прокрутка к ней, и только если её нет — раздел свёрнут.
  if (!ensureVisible(h, "economy.ess." + std::to_string(fire))) {
    CHECK(clickIn(h, "economy.ess"));
    h.settle();
  }
  // Запас — правка числа (rules::setEssence).
  CHECK(typeNumber(h, "economy.ess." + std::to_string(fire), "120"));
  CHECK_NEAR(h->world().faction(st)->essence(fire), 120, 1e-9);
  // Долг (отрицательный запас) можно задать — строка помечена.
  CHECK(typeNumber(h, "economy.ess." + std::to_string(water), "-15"));
  CHECK_NEAR(h->world().faction(st)->essence(water), -15, 1e-9);
  CHECK(ensureVisible(h, "economy.essNet." + std::to_string(water)));
  CHECK(calcOf(h, st).essences.at(water).net < 0);
  shotClean(h, "eco_economy_essences");
  // «Скрыть пустые»: «Эссенция земли» прячется, пламя и вода — нет.
  CHECK(ensureVisible(h, "economy.ess." + std::to_string(earth)));
  CHECK(clickIn(h, "economy.essHide"));
  h.settle();
  CHECK(h->uiRect("economy.ess." + std::to_string(earth)) == nullptr);
  CHECK(ensureVisible(h, "economy.ess." + std::to_string(fire)));
  CHECK(ensureVisible(h, "economy.ess." + std::to_string(water)));
  shotClean(h, "eco_economy_essences_hidden");
  // Значок вкладки: эссенция в долгу.
  bool badge = false;
  for (const app::TabDef& t : app::tabs())
    if (std::string_view(t.id) == "faction.economy" && t.badge) badge = t.badge(h.a(), st) != 0;
  CHECK(badge);
}

// ---------------------------------------------------------------- преобразование в столбцах ресурса
TEST(app_eco_economy_conversion_columns) {
  HideTestRegs hide;
  Harness h("eco_economy_convert", 1440, 1300);
  h.demo();
  const Id st = biggestState(h->world());
  const Id wood = resByName(h->world(), "Древесина"), fuel = resByName(h->world(), "Топливо");
  CHECK(st && wood && fuel);
  Id b = 0;
  CHECK(h->act("Углежогня", [&](Tx& tx) {
    b = rules::createBuilding(tx, 0, "Углежогня");
    rules::setBuildingRole(tx, b, rules::BuildingRole::Convert, true);
    Recipe r;
    r.in = {ResAmount{wood, 40}};
    r.out = ResAmount{fuel, 10};
    r.turns = 1;
    rules::setRecipe(tx, b, r);
    tx.faction(st).res[wood] = 500;
  }));
  CHECK(placeAnywhere(h, st, b) != 0);
  const rules::FactionCalc& fc = calcOf(h, st);
  CHECK_NEAR(fc.resources.at(wood).conversionIn, 40, 1e-9);
  CHECK_NEAR(fc.resources.at(fuel).conversionOut, 10, 1e-9);
  openTab(h, st, "faction.economy");
  CHECK(ensureVisible(h, "economy.use." + std::to_string(wood)));
  CHECK(ensureVisible(h, "economy.in." + std::to_string(fuel)));
  CHECK(ensureVisible(h, "economy.net." + std::to_string(fuel)));
  shotClean(h, "eco_economy_conversion");
}
