// Сценарии найма (ТЗ «Ввод новых механик», п.1–5): ключевой ресурс юнита (кавалерия — из «Ездовых наземных»;
// без него найм заблокирован с причиной), полная цена в окне «Сформировать», дополнительный ресурс через группу и
// эссенции на юнит, элементали (только за эссенции, раса «Элементали», содержание эссенциями, а не золотом), особый
// отряд из постройки доступа (недоступен без неё, найм и причина отказа); подсказки с полным текстом у обрезанных
// названий в ячейке, поле и выпадающем списке.
#include "tests/test_app_war_util.h"
#include "ui/ui_internal.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Вкладка «Войска» в панели справа от карты (узкая таблица, правка — в карточке строки).
void openArmyTab(Harness& h, Id faction) {
  h->ui.tabOf[app::SelType::Faction] = "faction.army";
  h->select(app::SelType::Faction, faction, true);
  h.settle();
  h.dropToasts();
  h.settle();
}

Id resourceNamed(const World& w, std::string_view name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}

Id essenceNamed(const World& w, std::string_view name) {
  for (const CatalogItem& c : w.catalogs->essences)
    if (c.name == name) return c.id;
  return 0;
}

int rowIndex(const World& w, Id faction, Id row) {
  const Faction* f = w.faction(faction);
  for (size_t i = 0; f && i < f->army.size(); i++)
    if (f->army[i].id == row) return int(i);
  return -1;
}

bool hasProblem(const rules::RecruitCost& c, std::string_view part) {
  for (const std::string& p : c.problems)
    if (p.find(part) != std::string::npos) return true;
  return false;
}

double at(const std::map<Id, double>& m, Id k) {
  auto it = m.find(k);
  return it == m.end() ? 0 : it->second;
}

// Подсказка, показанная сейчас: увести указатель (после щелчка подсказка элемента ждёт нового наведения), навести
// и подождать дольше задержки подсказки.
std::string tipAt(Harness& h, float x, float y) {
  h.move(2, 2);
  h.move(x, y);
  hl::advance(0.5);
  h.frames(4);
  const ui::in::Ctx& c = ui::in::C();
  return c.tipRequested ? c.tipText : std::string();
}

// Ввести дробное число в поле (запятая — разделитель).
bool enterValue(Harness& h, const std::string& mark, const std::string& text) {
  if (!h.clickUi(mark)) return false;
  h.retype(text);
  h.key(Key::Enter);
  h.settle();
  return true;
}

// Новая строка из меню «Добавить отряд» (в заголовке раздела «Отряды»): пункт mark.
bool addFromMenu(Harness& h, const std::string& mark) {
  if (!reveal(h, "mil.army.add")) return false;
  const RectF* add = h->uiRect("mil.army.add");
  if (!add) return false;
  h.click(add->right() - 13, add->cy());
  h.settle();
  return h.clickUi(mark);
}

}  // namespace

// Кавалерия: ключевой ресурс обязателен (красная обводка, найм заблокирован с причиной), выбор из «Ездовых
// наземных»; дополнительный ресурс через группу, эссенция на юнит; полная цена в окне «Сформировать» и формирование
// с оплатой; отмена — возврат ресурсов и эссенций. В широкой таблице — столбцы ключевого ресурса и цены.
TEST(app_military_hire_key_resource_and_cost) {
  Harness h("military_hire_key", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id cav = h->world().faction(hel)->army[1].id;
  CHECK(schema::isCavalry(h->world().faction(hel)->armyRow(cav)->type));
  const Id horses = resourceNamed(h->world(), "Лошади"), iron = resourceNamed(h->world(), "Железо");
  const Id fire = essenceNamed(h->world(), "Эссенция пламени");
  CHECK(horses && iron && fire);
  CHECK(rules::keyAllowed(h->world(), UnitType::HeavyCav, horses));
  CHECK(!rules::keyAllowed(h->world(), UnitType::HeavyCav, iron));
  openArmyTab(h, hel);
  CHECK(reveal(h, "mil.row.1"));
  CHECK(h.clickUi("mil.row.1"));
  h.settle();

  // Ключевой ресурс не задан: найм заблокирован, причина — от правил.
  CHECK(hasProblem(rules::recruitCost(h->world(), hel, cav, 10), "Не указан ключевой ресурс юнита"));
  CHECK(reveal(h, "mil.detail.key"));
  h.dropToasts();
  h.settle();
  if (const RectF* insp = h->uiRect("inspector")) CHECK(cropShot("military_hire_key_missing", *insp));
  CHECK(reveal(h, "mil.detail.recruit"));
  CHECK(h.clickUi("mil.detail.recruit"));
  CHECK(enterNumber(h, "mil.recruit.count", 10));
  const size_t forming0 = h->world().faction(hel)->forming.size();
  CHECK(h.clickUi("mil.recruit.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->forming.size(), forming0);
  h.key(Key::Escape);
  h.settle();

  // Выбор из подходящих (поиск по списку «Ездовые наземные»).
  CHECK(reveal(h, "mil.detail.key"));
  CHECK(h.clickUi("mil.detail.key"));
  h.type("Лошади");
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(h->world().faction(hel)->armyRow(cav)->keyRes, horses);
  CHECK(!hasProblem(rules::recruitCost(h->world(), hel, cav, 10), "ключевой ресурс"));

  // Дополнительный ресурс: сначала группа «Обычная» (руда), затем «Железо»; 2 на юнит.
  CHECK(reveal(h, "mil.detail.extra.add"));
  CHECK(h.clickUi("mil.detail.extra.add"));
  h.settle();
  const RectF* pick = h->uiRect("mil.extra.pick");
  CHECK(pick != nullptr);
  if (pick) {
    const RectF pr = *pick;
    h.click(pr.x + pr.w * 0.25f, pr.cy());   // группа
    h.type("Обычная");
    h.key(Key::Enter);
    h.settle();
    const Id common = h->world().catalogs->groupId(schema::grp::OreCommon);
    const std::vector<Id> ores = rules::resourcesIn(h->world(), common);
    const int idx = int(std::find(ores.begin(), ores.end(), iron) - ores.begin());
    CHECK(idx < int(ores.size()));
    h.click(pr.x + pr.w * 0.75f, pr.cy());   // ресурс группы (≤ 8 пунктов — без поиска, стрелками)
    for (int k = 0; k < idx; k++) h.key(Key::Down);
    h.key(Key::Enter);
    h.settle();
  }
  CHECK(enterNumber(h, "mil.extra.amount", 2));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("military_hire_extra_popup"));
  CHECK(h.clickUi("mil.extra.ok"));
  h.settle();
  CHECK_NEAR(at(h->world().faction(hel)->armyRow(cav)->extra, iron), 2, 1e-9);

  // Эссенция элемента на юнит: «Эссенция пламени», 3.
  CHECK(reveal(h, "mil.detail.ess.add"));
  CHECK(h.clickUi("mil.detail.ess.add"));
  h.settle();
  CHECK(h.clickUi("mil.ess.pick"));
  h.type("пламени");
  h.key(Key::Enter);
  h.settle();
  CHECK(enterNumber(h, "mil.ess.amount", 3));
  CHECK(h.clickUi("mil.ess.ok"));
  h.settle();
  CHECK_NEAR(at(h->world().faction(hel)->armyRow(cav)->essence, fire), 3, 1e-9);

  // Количество на юнит правится в строке списка (до тысячных); отрицательное — нельзя (поле ограничено нулём).
  CHECK(reveal(h, "mil.detail.extra." + std::to_string(iron)));
  CHECK(enterValue(h, "mil.detail.extra." + std::to_string(iron), "2,5"));
  CHECK_NEAR(at(h->world().faction(hel)->armyRow(cav)->extra, iron), 2.5, 1e-9);
  CHECK(enterValue(h, "mil.detail.extra." + std::to_string(iron), "-4"));
  CHECK(h->world().faction(hel)->armyRow(cav)->extra.count(iron) == 0);   // 0 — убрать
  h.key(Key::Z, ctrl());
  CHECK_NEAR(at(h->world().faction(hel)->armyRow(cav)->extra, iron), 2.5, 1e-9);
  h.dropToasts();
  h.settle();
  if (const RectF* insp = h->uiRect("inspector")) CHECK(cropShot("military_hire_card", *insp));

  // Полная цена: население, ключевой ресурс (1 на юнит), железо 2,5, эссенция 3 — и формирование с оплатой.
  CHECK(h->act("Запасы", [&](Tx& tx) {
    tx.faction(hel).res[horses] = 100;
    tx.faction(hel).res[iron] = 1000;
    rules::setEssence(tx, hel, fire, 500);
  }));
  const rules::RecruitCost c = rules::recruitCost(h->world(), hel, cav, 20);
  CHECK_EQ(c.people, 20);
  CHECK_NEAR(at(c.res, horses), 20, 1e-9);
  CHECK_NEAR(at(c.res, iron), 50, 1e-9);
  CHECK_NEAR(at(c.ess, fire), 60, 1e-9);
  CHECK(c.problems.empty());
  const i64 pop0 = rules::statePopulation(h->world(), hel);
  CHECK(reveal(h, "mil.detail.recruit"));
  CHECK(h.clickUi("mil.detail.recruit"));
  CHECK(enterNumber(h, "mil.recruit.count", 20));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("military_hire_cost_popup"));
  CHECK(h.clickUi("mil.recruit.ok"));
  h.settle();
  const Faction* f = h->world().faction(hel);
  CHECK_EQ(f->forming.size(), forming0 + 1);
  if (f->forming.size() == forming0 + 1) {
    const Formation& q = f->forming.back();
    CHECK_EQ(q.people, 20);
    CHECK_NEAR(at(q.paid, horses), 20, 1e-9);
    CHECK_NEAR(at(q.paid, iron), 50, 1e-9);
    CHECK_NEAR(at(q.paidEss, fire), 60, 1e-9);
  }
  CHECK_NEAR(f->stock(horses), 80, 1e-9);
  CHECK_NEAR(f->stock(iron), 950, 1e-9);
  CHECK_NEAR(f->essence(fire), 440, 1e-9);
  CHECK_EQ(rules::statePopulation(h->world(), hel), pop0 - 20);
  // Отмена формирования — возврат людей, ресурсов и эссенций.
  const std::string cancel = "mil.army.forming." + std::to_string(forming0) + ".cancel";
  CHECK(reveal(h, cancel));
  CHECK(h.clickUi(cancel));
  h.settle();
  f = h->world().faction(hel);
  CHECK_NEAR(f->stock(horses), 100, 1e-9);
  CHECK_NEAR(f->stock(iron), 1000, 1e-9);
  CHECK_NEAR(f->essence(fire), 500, 1e-9);
  CHECK_EQ(rules::statePopulation(h->world(), hel), pop0);

  // Во весь экран: ключевой ресурс — столбцом, цена — значками с подсказкой; щелчок по ячейке цены выделяет строку.
  h->openEditor("military", hel);
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK(h->uiRect("mil.row.1.key") != nullptr);
  const RectF* cost0 = h->uiRect("mil.row.0.cost");
  const RectF* cost1 = h->uiRect("mil.row.1.cost");
  CHECK(cost0 != nullptr && cost1 != nullptr);
  if (cost0 && cost1) {
    const RectF r0 = *cost0, r1 = *cost1;
    const std::string tip = tipAt(h, r1.x + 8, r1.cy());
    CHECK(tip.find("Железо: 2,5") != std::string::npos);
    CHECK(tip.find("Эссенция пламени: 3") != std::string::npos);
    CHECK(h.shot("military_hire_editor_cost"));
    h.click(r0.cx(), r0.cy());
    h.settle();
    CHECK(h->uiRect("mil.detail.extra." + std::to_string(iron)) == nullptr);   // карточка строки 0
    h.click(r1.cx(), r1.cy());
    h.settle();
    CHECK(h->uiRect("mil.detail.extra." + std::to_string(iron)) != nullptr);   // карточка кавалерии
  }
}

// Элементали: в меню — последний тип; раса только «Элементали»; без дополнительных ресурсов и золотого содержания —
// найм и содержание эссенциями (в показателях вкладки — по эссенциям).
TEST(app_military_hire_elementals) {
  Harness h("military_hire_elementals", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id air = essenceNamed(h->world(), "Эссенция воздуха");
  CHECK(air != 0);
  openArmyTab(h, hel);
  const size_t rows0 = h->world().faction(hel)->army.size();
  const double gold0 = rules::calc(h->world())->faction(hel)->expArmy;
  CHECK(addFromMenu(h, "mil.addrow." + std::to_string(int(UnitType::Elementals))));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->army.size(), rows0 + 1);
  const ArmyRow row = h->world().faction(hel)->army.back();
  CHECK(row.type == UnitType::Elementals);
  CHECK_EQ(rules::unitRace(h->world(), hel, row), std::string(schema::kRaceElemental));
  // Раса заблокирована (правило отказывает; выбор расы в карточке недоступен), золотого содержания и
  // дополнительных ресурсов нет.
  CHECK(!h->act("Раса", [&](Tx& tx) { rules::setRowRace(tx, hel, row.id, schema::kRaceLiving); }));
  h.dropToasts();
  CHECK(reveal(h, "mil.detail.race"));
  CHECK_EQ(h->world().faction(hel)->armyRow(row.id)->race, std::string(schema::kRaceElemental));
  CHECK(h->uiRect("mil.detail.upkeep") == nullptr);
  CHECK(h->uiRect("mil.detail.extra.add") == nullptr);
  CHECK(h->uiRect("mil.detail.ess.add") != nullptr);
  CHECK(h->uiRect("mil.detail.essup.add") != nullptr);

  // Содержание эссенциями: «Эссенция воздуха», 2 на юнит за ход; найм — 5 той же эссенции на юнит.
  CHECK(reveal(h, "mil.detail.essup.add"));
  CHECK(h.clickUi("mil.detail.essup.add"));
  h.settle();
  CHECK(h.clickUi("mil.essup.pick"));
  h.type("воздуха");
  h.key(Key::Enter);
  h.settle();
  CHECK(enterNumber(h, "mil.essup.amount", 2));
  CHECK(h.clickUi("mil.essup.ok"));
  h.settle();
  CHECK(reveal(h, "mil.detail.ess.add"));
  CHECK(h.clickUi("mil.detail.ess.add"));
  h.settle();
  CHECK(h.clickUi("mil.ess.pick"));
  h.type("воздуха");
  h.key(Key::Enter);
  h.settle();
  CHECK(enterNumber(h, "mil.ess.amount", 5));
  CHECK(h.clickUi("mil.ess.ok"));
  h.settle();
  const ArmyRow* r = h->world().faction(hel)->armyRow(row.id);
  CHECK_NEAR(at(r->essUpkeep, air), 2, 1e-9);
  CHECK_NEAR(at(r->essence, air), 5, 1e-9);

  // Найм: ни людей, ни ресурсов — только эссенции (не хватает — причина от правил).
  rules::RecruitCost c = rules::recruitCost(h->world(), hel, row.id, 10);
  CHECK_EQ(c.people, 0);
  CHECK(c.res.empty());
  CHECK_NEAR(at(c.ess, air), 50, 1e-9);
  CHECK(hasProblem(c, "Эссенция воздуха"));

  // 100 элементалей (правка резерва — без расхода эссенций): содержание 200 эссенции за ход, золото не растёт.
  CHECK(reveal(h, "mil.army.editReserve"));
  CHECK(h.clickUi("mil.army.editReserve"));
  h.settle();
  CHECK(reveal(h, "mil.detail.total"));
  CHECK(enterNumber(h, "mil.detail.total", 100));
  CHECK_EQ(h->world().faction(hel)->armyRow(row.id)->total, 100);
  CHECK_NEAR(h->world().faction(hel)->essence(air), 0, 1e-9);
  auto calc = rules::calc(h->world());
  CHECK_NEAR(calc->faction(hel)->essences.at(air).upkeep, 200, 1e-9);
  CHECK_NEAR(calc->faction(hel)->expArmy, gold0, 1e-9);
  CHECK(reveal(h, "mil.army.editReserve"));
  CHECK(h.clickUi("mil.army.editReserve"));
  h.settle();
  CHECK(h->uiRect("mil.army.essupkeep") != nullptr);   // показатели: содержание эссенциями
  CHECK(reveal(h, "mil.row." + std::to_string(rowIndex(h->world(), hel, row.id))));
  h.dropToasts();
  h.settle();
  if (const RectF* insp = h->uiRect("inspector")) CHECK(cropShot("military_hire_elementals", *insp));

  // Найм за эссенции: запас 600 — 10 элементалей за 50, население не тратится.
  CHECK(h->act("Эссенция воздуха", [&](Tx& tx) { rules::setEssence(tx, hel, air, 600); }));
  c = rules::recruitCost(h->world(), hel, row.id, 10);
  CHECK(c.problems.empty());
  const i64 pop0 = rules::statePopulation(h->world(), hel);
  CHECK(h->act("Формирование", [&](Tx& tx) { rules::recruit(tx, hel, row.id, 10); }));
  CHECK_NEAR(h->world().faction(hel)->essence(air), 550, 1e-9);
  CHECK_EQ(rules::statePopulation(h->world(), hel), pop0);

  // Широкая таблица: содержание одного и итог — эссенциями.
  h->openEditor("military", hel);
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("military_hire_elementals_editor"));
}

// Особый отряд «Драконы Бездны»: без «Цитадели Бездны» — серый пункт меню с подсказкой о постройке; после постановки
// постройки — строка особого отряда (тип, раса и цены — из справочника, только для чтения), найм с оплатой; снос
// постройки — найм заблокирован с причиной; переход к справочнику.
TEST(app_military_hire_special_unit) {
  Harness h("military_hire_special", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  const World& w0 = h->world();
  const Id hel = factionByName(w0, "Хельдвиг");
  Id special = 0, citadel = 0;
  for (const SpecialUnit& s : w0.catalogs->specials)
    if (s.name == "Драконы Бездны") special = s.id;
  w0.buildings.each([&](const Building& b) {
    if (b.name == "Цитадель Бездны") citadel = b.id;
  });
  CHECK(special && citadel);
  if (!special || !citadel) return;
  const SpecialUnit S = *w0.special(special);
  CHECK(!rules::hasSpecialAccess(w0, hel, special));
  openArmyTab(h, hel);
  const size_t rows0 = h->world().faction(hel)->army.size();
  const std::string item = "mil.addspecial." + std::to_string(special);
  CHECK(addFromMenu(h, item));   // пункт недоступен: щелчок ничего не добавляет
  h.settle();
  CHECK_EQ(h->world().faction(hel)->army.size(), rows0);
  if (const RectF* it = h->uiRect(item)) {
    const RectF r = *it;
    const std::string tip = tipAt(h, r.cx(), r.cy());
    CHECK(tip.find("Цитадель Бездны") != std::string::npos);
    CHECK(h.shot("military_hire_special_locked"));
  }
  h.key(Key::Escape);
  h.settle();
  CHECK(!h->act("Особый отряд без постройки", [&](Tx& tx) { rules::addSpecialRow(tx, hel, special); }));
  h.dropToasts();

  // Цитадель в провинции Хельдвига (готовой постройкой) — отряд доступен.
  Id where = 0;
  h->world().provinces.each([&](const Province& p) {
    if (where || p.sea || p.owner != hel) return;
    for (const rules::BuildOption& o : rules::buildOptions(h->world(), p.id))
      if (o.building == citadel && o.canPlace) where = p.id;
  });
  CHECK(where != 0);
  if (!where) return;
  CHECK(h->act("Цитадель Бездны", [&](Tx& tx) { rules::placeBuilding(tx, where, citadel, 1); }));
  CHECK(rules::hasSpecialAccess(h->world(), hel, special));
  h.settle();
  CHECK(addFromMenu(h, item));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->army.size(), rows0 + 1);
  const ArmyRow row = h->world().faction(hel)->army.back();
  CHECK_EQ(row.special, special);
  CHECK(row.type == S.type);
  CHECK_EQ(row.keyRes, S.keyRes);
  CHECK(row.extra == S.extra);
  CHECK(row.essence == S.essence);
  // Тип, ключевой ресурс и цены — только для чтения (правила откажут; правка — в справочнике).
  CHECK(reveal(h, "mil.detail.special.open"));
  CHECK(h->uiRect("mil.detail.extra.add") == nullptr);
  CHECK(h->uiRect("mil.detail.ess.add") == nullptr);
  CHECK(!S.extra.empty());
  if (!S.extra.empty()) CHECK(h->uiRect("mil.detail.extra." + std::to_string(S.extra.begin()->first)) != nullptr);   // цены видны
  CHECK(!h->act("Тип особого отряда", [&](Tx& tx) { rules::setRowType(tx, hel, row.id, UnitType::LightInf); }));
  h.dropToasts();
  h.settle();
  if (const RectF* insp = h->uiRect("inspector")) CHECK(cropShot("military_hire_special_card", *insp));

  // Найм: ключевой ресурс, обсидиановая сталь и эссенция бездны — из справочника.
  const Id steel = resourceNamed(h->world(), "Обсидиановая сталь");
  const Id abyss = essenceNamed(h->world(), "Эссенция бездны");
  CHECK(steel && abyss);
  CHECK(h->act("Запасы", [&](Tx& tx) {
    tx.faction(hel).res[S.keyRes] = 10;
    tx.faction(hel).res[steel] = 1000;
    rules::setEssence(tx, hel, abyss, 10000);
  }));
  const size_t forming0 = h->world().faction(hel)->forming.size();
  CHECK(reveal(h, "mil.detail.recruit"));
  CHECK(h.clickUi("mil.detail.recruit"));
  CHECK(enterNumber(h, "mil.recruit.count", 5));
  CHECK(h.clickUi("mil.recruit.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->forming.size(), forming0 + 1);
  CHECK_NEAR(h->world().faction(hel)->stock(S.keyRes), 5, 1e-9);
  CHECK_NEAR(h->world().faction(hel)->stock(steel), 500, 1e-9);
  CHECK_NEAR(h->world().faction(hel)->essence(abyss), 5000, 1e-9);

  // Постройку снесли — найм заблокирован с причиной; в карточке — какая постройка нужна.
  CHECK(h->act("Снести цитадель", [&](Tx& tx) { rules::demolish(tx, where, citadel); }));
  h.settle();
  CHECK(hasProblem(rules::recruitCost(h->world(), hel, row.id, 1), "Нужна постройка доступа"));
  CHECK(reveal(h, "mil.detail.special.need"));
  CHECK(!h->act("Формирование без постройки", [&](Tx& tx) { rules::recruit(tx, hel, row.id, 1); }));
  h.dropToasts();
  // Справочник «Особые отряды».
  CHECK(reveal(h, "mil.detail.special.open"));
  CHECK(h.clickUi("mil.detail.special.open"));
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("catalogs"));
}

// Длинные названия: обрезанное многоточием название в ячейке узкой таблицы, в поле широкой таблицы и выбранный пункт
// выпадающего списка показывают подсказку с полным текстом; щелчок по ячейке по-прежнему выделяет строку.
TEST(app_military_hire_long_names) {
  Harness h("military_hire_names", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id cav = h->world().faction(hel)->army[1].id;
  const std::string longName = "Тяжёлая кавалерия северных баронов Хельдвига и вольных охотников";
  CHECK(h->act("Название", [&](Tx& tx) {
    for (ArmyRow& r : tx.faction(hel).army)
      if (r.id == cav) r.name = longName;
  }));
  openArmyTab(h, hel);
  // Узкая таблица: название в две строки с многоточием — подсказка; щелчок по названию выделяет строку.
  CHECK(reveal(h, "mil.row.1"));
  const RectF* row = h->uiRect("mil.row.1");
  CHECK(row != nullptr);
  if (row) {
    const RectF r = *row;
    const float nx = r.x + 60;   // название — правее плитки типа
    CHECK_EQ(tipAt(h, nx, r.cy()), longName);
    h.click(nx, r.cy());
    h.settle();
    CHECK(reveal(h, "mil.detail.name"));   // карточка выбранной строки
    CHECK(h->uiRect("mil.detail.key") != nullptr);
  }
  // Поле наименования в карточке (без фокуса) — подсказка с полным названием.
  CHECK(reveal(h, "mil.detail.name"));
  if (const RectF* nf = h->uiRect("mil.detail.name")) {
    const RectF r = *nf;
    const std::string tip = tipAt(h, r.cx(), r.cy());
    CHECK_MSG(tip.find(longName) == 0, tip);
  }
  h.dropToasts();
  h.settle();
  if (const RectF* insp = h->uiRect("inspector")) CHECK(cropShot("military_hire_long_name", *insp));

  // Ключевой ресурс воздушной кавалерии с длинным названием: в карточке поле во всю ширину — название помещается,
  // подсказка — только своя (откуда ключевой ресурс).
  const Id bats = resourceNamed(h->world(), "Гигантские летучие мыши");
  CHECK(bats != 0);
  CHECK(h->act("Воздушная кавалерия", [&](Tx& tx) {
    rules::setRowType(tx, hel, cav, UnitType::AirCav);
    rules::setRowKey(tx, hel, cav, bats);
  }));
  h.settle();
  CHECK(reveal(h, "mil.detail.key"));
  if (const RectF* kc = h->uiRect("mil.detail.key")) {
    const RectF r = *kc;
    const std::string tip = tipAt(h, r.cx(), r.cy());
    CHECK_MSG(tip.find("Ключевой ресурс юнита") == 0, tip);
  }

  // Широкая таблица: поле наименования и выпадающий список ключевого ресурса с многоточием — подсказки с полным
  // текстом; в открытом списке (шире поля) название видно целиком.
  h->openEditor("military", hel);
  h.settle();
  h.dropToasts();
  h.settle();
  const RectF* enf = h->uiRect("mil.row.1.name");
  CHECK(enf != nullptr);
  if (enf) {
    const RectF r = *enf;
    const std::string tip = tipAt(h, r.cx(), r.cy());
    CHECK(h.shot("military_hire_long_name_editor"));
    CHECK_MSG(tip.find(longName) == 0, tip);
  }
  const RectF* ekc = h->uiRect("mil.row.1.key");
  CHECK(ekc != nullptr);
  if (ekc) {
    const RectF r = *ekc;
    const std::string tip = tipAt(h, r.cx(), r.cy());
    CHECK_MSG(tip.find("Гигантские летучие мыши") == 0, tip);
    CHECK(h.shot("military_hire_key_tip"));
    h.click(r.cx(), r.cy());
    h.settle();
    CHECK(h.shot("military_hire_key_list"));
    h.key(Key::Escape);
    h.settle();
  }
}
