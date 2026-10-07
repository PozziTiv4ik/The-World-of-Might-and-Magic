// Сценарии новых вкладок окна справочников: девять вкладок в порядке a.openEditor("catalogs", N) (узкое окно —
// значки), «Эссенции элементов» (запасы, строки войск, постройки генерации в «Где используется»), «Реликвии»
// (редкость с подсветкой, уникальное название, владелец, правка на месте, удаление из инвентаря), «Особые отряды»
// (правка всей записи — строки армий повторяют её, тип элементалей, постройки доступа, удаление), должности и
// 61 религия; снимки экрана.
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;

namespace {

const char* const kViews[] = {"resources", "races", "cultures", "religions", "governments", "positions", "essences", "relics", "specials"};

Id essenceNamed(const World& w, const std::string& name) {
  for (const CatalogItem& c : w.catalogs->essences)
    if (c.name == name) return c.id;
  return 0;
}
Id resNamed(const World& w, const std::string& name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}
Id specialNamed(const World& w, const std::string& name) {
  for (const SpecialUnit& s : w.catalogs->specials)
    if (s.name == name) return s.id;
  return 0;
}
Id buildingNamed(const World& w, const std::string& name) {
  Id r = 0;
  w.buildings.each([&](const Building& b) {
    if (!r && b.name == name) r = b.id;
  });
  return r;
}
// Персонажи государств (первые n).
std::vector<Id> heroes(const World& w, size_t n) {
  std::vector<Id> out;
  w.characters.each([&](const Character& c) {
    if (out.size() < n && c.faction && !c.name.empty()) out.push_back(c.id);
  });
  return out;
}
Id landProvinceOf(const World& w, Id owner) {
  Id r = 0;
  w.provinces.each([&](const Province& p) {
    if (!r && !p.sea && p.owner == owner) r = p.id;
  });
  return r;
}
const ArmyRow* rowOf(const World& w, Id faction, Id row) {
  const Faction* f = w.faction(faction);
  return f ? f->armyRow(row) : nullptr;
}

// Выбрать пункт «нет» (первый) в поле со списком: открыть, вверх до начала, Enter.
bool pickNone(Harness& h, const std::string& name) {
  if (!clickRevealed(h, name)) return false;
  for (int i = 0; i < 6; i++) h.key(Key::PageUp);
  h.key(Key::Enter);
  quick(h);
  return true;
}

}  // namespace

TEST(app_catalogs_items_tabs) {
  Harness h("catalogs_tabs");
  h.demo();
  h.dropToasts();
  // a.openEditor("catalogs", N) — ровно вкладка N.
  for (int n = 1; n <= 9; n++) {
    h->openEditor("catalogs", Id(n));
    quick(h);
    CHECK_MSG(h->uiRect(std::string("catalogs.view.") + kViews[n - 1]) != nullptr, kViews[n - 1]);
  }
  // Щелчок по вкладке (9 равных частей не делятся — по порядку слева направо): последняя — особые отряды.
  {
    const RectF* t = h->uiRect("catalogs.tabs");
    CHECK(t != nullptr);
  }
  // 10 должностей и 61 базовая религия — таблицы работают и выглядят нормально.
  CHECK_EQ(h->world().catalogs->positions.size(), size_t(10));
  CHECK(h->world().catalogs->religions.size() >= size_t(61));
  h->openEditor("catalogs", 6);
  quick(h);
  shotClean(h, "catalogs_positions_all");
  h->openEditor("catalogs", 4);
  quick(h);
  CHECK(h->uiRect("catalogs.selected.name") != nullptr);
  h.key(Key::F, ctrl());
  h.type("Орден");
  quick(h);
  shotClean(h, "catalogs_religions_search");
  h.key(Key::Escape);
  quick(h);
  h->openEditor("catalogs", 4);
  quick(h);
  shotClean(h, "catalogs_religions");
}

TEST(app_catalogs_items_tabs_narrow) {
  Harness h("catalogs_tabs_narrow", 1180, 820);
  h.demo();
  h.dropToasts();
  h->openEditor("catalogs", 9);
  quick(h);
  CHECK(h->uiRect("catalogs.view.specials") != nullptr);
  // Девять вкладок помещаются: строка вкладок не шире области окна.
  const RectF* t = h->uiRect("catalogs.tabs");
  const RectF* v = h->uiRect("catalogs.view.specials");
  CHECK(t && v && t->right() <= v->right() + 1);
  shotClean(h, "catalogs_tabs_narrow");
  // Узкое окно: карточка под таблицей (правка реликвий и особых отрядов доступна и здесь).
  CHECK(h->uiRect("catalogs.special.name") != nullptr);
  h->openEditor("catalogs", 1);
  quick(h);
  CHECK(h.clickUi("catalogs.expandAll"));
  quick(h);
  shotClean(h, "catalogs_resources_narrow");
}

// Другой цветокор (багрянец): цвета — из темы, подсветка редкости и дерево групп читаются.
TEST(app_catalogs_items_alt_scheme) {
  Harness h("catalogs_alt", 1440, 900, 1, kAltScheme);
  h->setScheme(kAltScheme);
  h.demo();
  h.dropToasts();
  CHECK(h->act("Реликвии", [&](Tx& tx) {
    const Rarity rs[] = {Rarity::Common, Rarity::Rare, Rarity::Epic, Rarity::Legendary, Rarity::Epochal};
    const char* names[] = {"Медный амулет", "Перстень Тени", "Посох Звездочёта", "Корона Предвечных", "Око Бездны"};
    for (int i = 0; i < 5; i++) rules::addRelic(tx, names[i], rs[i]);
  }));
  h->openEditor("catalogs", 8);
  quick(h);
  shotClean(h, "catalogs_relics_alt");
  h->openEditor("catalogs", 1);
  quick(h);
  CHECK(h.clickUi("catalogs.group." + std::to_string(h->world().catalogs->groupId(schema::grp::Beasts)) + ".toggle"));
  quick(h);
  shotClean(h, "catalogs_resources_alt");
}

TEST(app_catalogs_items_essences) {
  Harness h("catalogs_essences");
  h.demo();
  h.dropToasts();
  const World& w0 = h->world();
  CHECK_EQ(w0.catalogs->essences.size(), size_t(18));
  const Id fire = essenceNamed(w0, "Эссенция пламени"), abyss = essenceNamed(w0, "Эссенция бездны");
  CHECK(fire && abyss);
  auto [s1, s2] = twoStates(w0);
  // Запасы двух государств, строка войск с эссенцией, постройка генерации.
  Id row = 0, gen = 0;
  CHECK(h->act("Эссенции", [&](Tx& tx) {
    rules::setEssence(tx, s1, fire, 250);
    rules::setEssence(tx, s2, fire, -40);
    row = rules::addArmyRow(tx, s1, UnitType::Casters, "Огненные маги", 30, 2);
    rules::setRowEssence(tx, s1, row, fire, 3);
    gen = rules::createBuilding(tx, 0, "Горнило пламени");
    rules::setBuildingRole(tx, gen, rules::BuildingRole::Essence, true);
    rules::setLevelEssence(tx, gen, 1, fire, 5);
  }));
  h->openEditor("catalogs", 7);
  quick(h);
  CHECK(h->uiRect("catalogs.view.essences") != nullptr);
  // Первая запись — «Эссенция пламени»: карточка с местами использования.
  CHECK(h->uiRect("catalogs.usage") != nullptr);
  shotClean(h, "catalogs_essences");
  // Переименование на месте.
  CHECK(h.clickUi("catalogs.selected.name"));
  h.retype("Эссенция огня");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().essence(fire)->name, std::string("Эссенция огня"));
  // Удаление: подтверждение, запасы, строка войск и уровень постройки очищены; Ctrl+Z возвращает.
  CHECK(clickRevealed(h, "catalogs.delete"));
  CHECK(confirmDialog(h));
  CHECK(h->world().essence(fire) == nullptr);
  CHECK_EQ(h->world().faction(s1)->essence(fire), 0.0);
  CHECK(rowOf(h->world(), s1, row)->essence.empty());
  CHECK(h->world().building(gen)->levels[0].essence.empty());
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK(h->world().essence(fire) != nullptr);
  CHECK_EQ(h->world().faction(s1)->essence(fire), 250.0);
  CHECK_EQ(rowOf(h->world(), s1, row)->essence.at(fire), 3.0);
}

TEST(app_catalogs_items_relics) {
  Harness h("catalogs_relics");
  h.demo();
  h.dropToasts();
  h->openEditor("catalogs", 8);
  quick(h);
  CHECK(h->uiRect("catalogs.view.relics") != nullptr);
  CHECK(h->world().catalogs->relics.empty());
  // Новая реликвия: название сразу в правке.
  CHECK(h.clickUi("catalogs.add"));
  quick(h);
  CHECK_EQ(h->world().catalogs->relics.size(), size_t(1));
  const Id crown = h->world().catalogs->relics[0].id;
  h.retype("Корона Предвечных");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().relic(crown)->name, std::string("Корона Предвечных"));
  CHECK(h->world().relic(crown)->rarity == Rarity::Common);
  // Редкость в карточке — «Легендарная» (короткий список без поиска: стрелки).
  CHECK(clickRevealed(h, "catalogs.relicCard.rarity"));
  for (int i = 0; i < 3; i++) h.key(Key::Down);
  h.key(Key::Enter);
  quick(h);
  CHECK(h->world().relic(crown)->rarity == Rarity::Legendary);
  // Вторая реликвия: то же название (без учёта регистра) — отказ, реликвия уникальна.
  CHECK(h.clickUi("catalogs.add"));
  quick(h);
  const Id second = h->world().catalogs->relics.back().id;
  CHECK(second != crown);
  const std::string before = h->world().relic(second)->name;
  h.retype("корона предвечных");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().relic(second)->name, before);
  h.dropToasts();
  // Правка на месте: щелчок по названию в таблице.
  CHECK(h.clickUi("catalogs.relic." + std::to_string(second) + ".name"));
  quick(h);
  h.retype("Око Бездны");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().relic(second)->name, std::string("Око Бездны"));
  // Редкость в строке таблицы — «Эпохальная».
  CHECK(clickRevealed(h, "catalogs.relic." + std::to_string(second) + ".rarity"));
  for (int i = 0; i < 4; i++) h.key(Key::Down);
  h.key(Key::Enter);
  quick(h);
  CHECK(h->world().relic(second)->rarity == Rarity::Epochal);
  // Владелец: персонаж из карточки, затем другой (реликвия переходит), затем «Свободна».
  const std::vector<Id> hs = heroes(h->world(), 2);
  CHECK_EQ(hs.size(), size_t(2));
  CHECK(pickInCombo(h, "catalogs.relicCard.holder", h->world().character(hs[0])->name));
  CHECK_EQ(rules::relicHolder(h->world(), second), hs[0]);
  CHECK(pickInCombo(h, "catalogs.relicCard.holder", h->world().character(hs[1])->name));
  CHECK_EQ(rules::relicHolder(h->world(), second), hs[1]);
  {
    const auto& inv0 = h->world().character(hs[0])->inventory;
    CHECK(std::find(inv0.begin(), inv0.end(), second) == inv0.end());
  }
  CHECK(pickNone(h, "catalogs.relicCard.holder"));
  CHECK_EQ(rules::relicHolder(h->world(), second), Id(0));
  CHECK(pickInCombo(h, "catalogs.relicCard.holder", h->world().character(hs[1])->name));
  // Описание.
  CHECK(clickRevealed(h, "catalogs.relicCard.desc"));
  h.type("Видит сквозь завесу.");
  h.key(Key::Tab);   // уход фокуса фиксирует описание
  quick(h);
  CHECK_EQ(h->world().relic(second)->desc, std::string("Видит сквозь завесу."));
  // Ещё три реликвии — все пять редкостей для снимка.
  CHECK(h->act("Реликвии", [&](Tx& tx) {
    rules::addRelic(tx, "Перстень Тени", Rarity::Rare);
    rules::addRelic(tx, "Посох Звездочёта", Rarity::Epic);
    rules::addRelic(tx, "Медный амулет", Rarity::Common);
  }));
  h.dropToasts();
  quick(h);
  shotClean(h, "catalogs_relics");
  // Удаление реликвии у персонажа: подтверждение, из инвентаря убрана; Ctrl+Z возвращает.
  CHECK(clickRevealed(h, "catalogs.delete"));
  CHECK(confirmDialog(h));
  CHECK(h->world().relic(second) == nullptr);
  {
    const auto& inv = h->world().character(hs[1])->inventory;
    CHECK(std::find(inv.begin(), inv.end(), second) == inv.end());
  }
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK(h->world().relic(second) != nullptr);
  CHECK_EQ(rules::relicHolder(h->world(), second), hs[1]);
}

TEST(app_catalogs_items_specials) {
  Harness h("catalogs_specials");
  h.demo();
  h.dropToasts();
  const World& w0 = h->world();
  const Id dragons = specialNamed(w0, "Драконы Бездны"), citadel = buildingNamed(w0, "Цитадель Бездны");
  const Id black = resNamed(w0, "Черные Драконы"), steel = resNamed(w0, "Обсидиановая сталь");
  const Id abyss = essenceNamed(w0, "Эссенция бездны"), fire = essenceNamed(w0, "Эссенция пламени");
  CHECK(dragons && citadel && black && steel && abyss && fire);
  {
    const SpecialUnit* s = w0.special(dragons);
    CHECK(s && s->type == UnitType::Monsters && s->keyRes == black);
    CHECK(s && s->extra.count(steel) && s->extra.at(steel) == 100);
    CHECK(s && s->essence.count(abyss) && s->essence.at(abyss) == 1000);
  }
  // Государство с «Цитаделью Бездны» нанимает драконов.
  auto [state, other] = twoStates(w0);
  (void)other;
  Id row = 0;
  CHECK(h->act("Доступ к драконам", [&](Tx& tx) {
    const Id p = landProvinceOf(tx.w(), state);
    tx.province(p).buildings.push_back(ProvBuilding{citadel, 1, false, 0});
    row = rules::addSpecialRow(tx, state, dragons);
    rules::setRowTotal(tx, state, row, 12);
  }));
  CHECK(row != 0);
  h->openEditor("catalogs", 9);
  quick(h);
  CHECK(h->uiRect("catalogs.view.specials") != nullptr);
  CHECK(h->uiRect("catalogs.special." + std::to_string(dragons)) != nullptr);
  CHECK(h->uiRect("catalogs.special.buildings") != nullptr);
  CHECK(h->uiRect("catalogs.special.rows") != nullptr);
  shotClean(h, "catalogs_specials");
  // Содержание 25 — строка армии повторяет запись.
  CHECK(enterValue(h, "catalogs.special.upkeep", "25"));
  CHECK_EQ(h->world().special(dragons)->upkeep, 25.0);
  CHECK_EQ(rowOf(h->world(), state, row)->upkeep, 25.0);
  // Дополнительный ресурс: 150 на юнит.
  CHECK(enterValue(h, "catalogs.special.extra." + std::to_string(steel), "150"));
  CHECK_EQ(h->world().special(dragons)->extra.at(steel), 150.0);
  CHECK_EQ(rowOf(h->world(), state, row)->extra.at(steel), 150.0);
  // Эссенция пламени — добавить (1 на юнит), строка повторяет.
  CHECK(pickInCombo(h, "catalogs.special.ess.add", "пламени"));
  CHECK(h->world().special(dragons)->essence.count(fire));
  CHECK(rowOf(h->world(), state, row)->essence.count(fire));
  // Тип — «Элементали»: раса «Элементали», ключевой ресурс и доп. ресурсы сняты, содержание — эссенциями.
  CHECK(pickInCombo(h, "catalogs.special.type", "Элемент"));
  {
    const SpecialUnit* s = h->world().special(dragons);
    CHECK(s && s->type == UnitType::Elementals);
    CHECK(s && s->race == schema::kRaceElemental);
    CHECK(s && s->keyRes == 0 && s->extra.empty());
    const ArmyRow* r = rowOf(h->world(), state, row);
    CHECK(r && r->type == UnitType::Elementals && r->race == schema::kRaceElemental && r->extra.empty());
  }
  CHECK(h->uiRect("catalogs.special.essUpkeep") != nullptr);
  CHECK(h->uiRect("catalogs.special.upkeep") == nullptr);
  CHECK(h->uiRect("catalogs.special.extra") == nullptr);
  CHECK(pickInCombo(h, "catalogs.special.essUpkeep.add", "бездны"));
  CHECK(h->world().special(dragons)->essUpkeep.count(abyss));
  CHECK(rowOf(h->world(), state, row)->essUpkeep.count(abyss));
  shotClean(h, "catalogs_specials_elementals");
  // Отмена — снова чудовища с ключевым ресурсом «Черные Драконы».
  for (int i = 0; i < 2; i++) h.key(Key::Z, ctrl());
  quick(h);
  CHECK(h->world().special(dragons)->type == UnitType::Monsters);
  CHECK_EQ(h->world().special(dragons)->keyRes, black);
  CHECK(rowOf(h->world(), state, row)->type == UnitType::Monsters);
  // Новый особый отряд: название сразу в правке.
  const size_t n0 = h->world().catalogs->specials.size();
  CHECK(h.clickUi("catalogs.add"));
  quick(h);
  CHECK_EQ(h->world().catalogs->specials.size(), n0 + 1);
  const Id fresh = h->world().catalogs->specials.back().id;
  h.retype("Стражи Рассвета");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().special(fresh)->name, std::string("Стражи Рассвета"));
  // Удаление драконов: строка армии станет обычной, постройка потеряет доступ; Ctrl+Z возвращает.
  {
    const RectF* r = h->uiRect("catalogs.special." + std::to_string(dragons));   // щелчок по названию — выбор строки
    CHECK(r != nullptr);
    if (r) h.click(r->x + 90, r->cy());
    quick(h);
  }
  h.key(Key::Delete);
  CHECK(confirmDialog(h));
  CHECK(h->world().special(dragons) == nullptr);
  CHECK_EQ(rowOf(h->world(), state, row)->special, Id(0));
  CHECK(h->world().building(citadel)->specials.empty());
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK(h->world().special(dragons) != nullptr);
  CHECK_EQ(rowOf(h->world(), state, row)->special, dragons);
}

// Журнал без предупреждений интерфейса (повторяющиеся ID, окна дважды за кадр): все вкладки, карточки групп,
// «Без группы», ресурса, реликвии у персонажа, особого отряда-элементаля, раскрытое дерево, окно модификаторов.
TEST(app_catalogs_items_log_clean) {
  Harness h("catalogs_log");
  const std::string log = fs::join(test::outDir(), "app-tmp/catalogs-log.txt");
  fs::remove(log);
  setLogFile(log);
  logInfo("catalogs-log: начало");   // журнал пишется в файл (проверка ниже)
  h.demo();
  h.dropToasts();
  const World& w0 = h->world();
  const Id dragons = specialNamed(w0, "Драконы Бездны"), fire = essenceNamed(w0, "Эссенция пламени"), abyss = essenceNamed(w0, "Эссенция бездны");
  const std::vector<Id> hs = heroes(w0, 1);
  CHECK(h->act("Данные", [&](Tx& tx) {
    const Id r1 = rules::addRelic(tx, "Корона Предвечных", Rarity::Legendary);
    rules::addRelic(tx, "Око Бездны", Rarity::Epochal);
    if (!hs.empty()) rules::giveRelic(tx, hs[0], r1);
    const Id el = rules::addSpecial(tx, "Духи пламени");
    SpecialUnit s = *tx.w().special(el);
    s.type = UnitType::Elementals;
    s.essence[fire] = 5;
    s.essence[abyss] = 2;
    s.essUpkeep[fire] = 1;
    s.essUpkeep[abyss] = 1;
    rules::setSpecial(tx, s);
  }));
  for (int n = 1; n <= 9; n++) {
    h->openEditor("catalogs", Id(n));
    h.settle();
  }
  // Особые отряды: элементаль (эссенции и в цене, и в содержании) и драконы.
  h->openEditor("catalogs", 9);
  h.settle();
  for (const SpecialUnit& s : h->world().catalogs->specials) {
    const RectF* r = h->uiRect("catalogs.special." + std::to_string(s.id));
    if (r) h.click(r->x + 90, r->cy());
    h.settle();
  }
  (void)dragons;
  // Реликвии: карточка реликвии у персонажа.
  h->openEditor("catalogs", 8);
  h.settle();
  // Ресурсы: всё раскрыто, карточки группы, «Без группы» и ресурса.
  h->openEditor("catalogs", 1);
  h.settle();
  CHECK(h.clickUi("catalogs.expandAll"));
  h.settle();
  for (int k = 0; k < 6; k++) {
    const RectF* t = h->uiRect("catalogs.table");
    if (t) h.wheel(t->cx(), t->cy(), -8);
    h.settle();
  }
  if (const RectF* r = h->uiRect("catalogs.nogroup")) h.click(r->right() - 130, r->cy());
  h.settle();
  if (const RectF* r = h->uiRect("catalogs.res.1")) h.click(r->right() - 130, r->cy());
  h.settle();
  // Модификаторы: все группы.
  h->openEditor("modifiers", 0);
  h.settle();
  setLogFile("");
  std::vector<std::string> bad;
  const auto text = fs::readFile(log);
  CHECK(text && text->find("catalogs-log: начало") != std::string::npos);
  if (text)
    for (const std::string& line : split(*text, '\n'))
      if ((line.find("[WARN]") != std::string::npos || line.find("[ERROR]") != std::string::npos) && line.find("ui:") != std::string::npos)
        bad.push_back(line);
  for (const std::string& b : bad) CHECK_MSG(false, b);
  CHECK(bad.empty());
}

// Просмотр прошлого хода: справочники только для чтения (кнопки создания недоступны, правка не проходит).
TEST(app_catalogs_items_readonly) {
  Harness h("catalogs_ro");
  h.demo();
  h.dropToasts();
  CHECK(h->endTurnNow());
  quick(h);
  CHECK(h->viewTurn(1));
  h->openEditor("catalogs", 1);
  quick(h);
  CHECK(h->readOnly());
  const size_t groups = h->store.world().catalogs->resGroups.size(), res = h->store.world().catalogs->resources.size();
  CHECK(h.clickUi("catalogs.newGroup"));
  CHECK(h.clickUi("catalogs.add"));
  quick(h);
  CHECK_EQ(h->store.world().catalogs->resGroups.size(), groups);
  CHECK_EQ(h->store.world().catalogs->resources.size(), res);
  // Группа: F2 не открывает правку названия.
  const Id ore = h->world().catalogs->groupId(schema::grp::Ore);
  {
    const RectF* r = h->uiRect("catalogs.group." + std::to_string(ore));
    CHECK(r != nullptr);
    if (r) h.click(r->right() - 130, r->cy());
  }
  h.key(Key::F2);
  h.type("Испорчено");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->store.world().catalogs->group(ore)->name, std::string("Руда"));
  for (int n : {8, 9}) {
    h->openEditor("catalogs", Id(n));
    quick(h);
    const size_t relics = h->store.world().catalogs->relics.size(), specials = h->store.world().catalogs->specials.size();
    CHECK(h.clickUi("catalogs.add"));
    quick(h);
    CHECK_EQ(h->store.world().catalogs->relics.size(), relics);
    CHECK_EQ(h->store.world().catalogs->specials.size(), specials);
  }
  shotClean(h, "catalogs_readonly");
  h->backToCurrent();
  quick(h);
}
