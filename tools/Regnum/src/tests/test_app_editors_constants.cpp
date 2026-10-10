// Сценарии «Глобальных констант» (ТЗ «Общие доработки», п.1–2): окно раздела «Справочники» — встроенные видны
// всегда, первая правка создаёт запись мира (Ctrl+Z возвращает шаблон), золото до тысячных, список ресурсов,
// список значений (базовые расы закреплены; переименование и удаление своей расы правит строки войск),
// своя константа (создание по типу, переименование, удаление с подтверждением), просмотр прошлого хода.
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;

namespace {

const char* kArea = "editor";
constexpr float kTop = 8;

// Окно констант открыто, константа key выбрана в списке (её карточка — справа).
bool sel(Harness& h, const std::string& key) {
  if (h->ui.editor != "constants") {
    h->openEditor("constants");
    quick(h);
  }
  if (!clickRevealed(h, "constants.list." + key, kArea, kTop)) return false;
  quick(h);
  return true;
}

double num(const World& w, const char* key) { return rules::constantOf(w, key).num; }

// Первая строка войск первого государства.
std::pair<Id, Id> armyRow(const World& w) {
  std::pair<Id, Id> r{0, 0};
  w.factions.each([&](const Faction& f) {
    if (!r.first && f.isState() && !f.army.empty()) r = {f.id, f.army.front().id};
  });
  return r;
}

std::string rowRace(const World& w, std::pair<Id, Id> fr) {
  const Faction* f = w.faction(fr.first);
  const ArmyRow* r = f ? f->armyRow(fr.second) : nullptr;
  return r ? r->race : std::string("?");
}

Id resByName(const World& w, const std::string& name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}

bool hasValue(const World& w, const char* key, const std::string& v) {
  const Constant& c = rules::constantOf(w, key);
  return std::find(c.values.begin(), c.values.end(), v) != c.values.end();
}

}  // namespace

TEST(app_editors_constants_builtin) {
  Harness h("editors_constants_builtin");
  h.demo();
  h.dropToasts();
  // Правая лента → «Справочники» → «Глобальные константы».
  CHECK(h.clickUi("section.reference"));
  quick(h);
  CHECK(h.clickUi("page.nav.constants"));
  quick(h);
  CHECK_EQ(h->ui.editor, std::string("constants"));
  for (const Constant& c : schema::builtinConstants()) CHECK_MSG(h->uiRect("constants.list." + c.key) != nullptr, c.key);
  CHECK(sel(h, schema::cst::ColonizationCost));
  CHECK(h->uiRect("constants.colonizationCost.name") != nullptr);
  CHECK(h->uiRect("constants.colonizationCost.delete") == nullptr);   // встроенные не удаляются
  shotClean(h, "editors_constants_drawer");
  // Стоимость колонизации — золото до тысячных: первая правка создаёт запись мира.
  CHECK(h->world().constants->find(schema::cst::ColonizationCost) == nullptr);
  CHECK(enterValue(h, "constants.colonizationCost.num", "12,345", kArea, kTop));
  CHECK(h->world().constants->find(schema::cst::ColonizationCost) != nullptr);
  CHECK_NEAR(num(h->world(), schema::cst::ColonizationCost), 12.345, 1e-9);
  // Ctrl+Z — снова значение по умолчанию (записи нет).
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK(h->world().constants->find(schema::cst::ColonizationCost) == nullptr);
  CHECK_NEAR(num(h->world(), schema::cst::ColonizationCost), 0.0, 1e-12);
  // Трупов на воина-нежить: по умолчанию 1, правка — 2,5.
  CHECK_NEAR(num(h->world(), schema::cst::CorpsesPerUnit), 1.0, 1e-12);
  CHECK(sel(h, schema::cst::CorpsesPerUnit));
  CHECK(enterValue(h, "constants.corpsesPerUnit.num", "2.5", kArea, kTop));
  CHECK_NEAR(num(h->world(), schema::cst::CorpsesPerUnit), 2.5, 1e-12);
  // Стоимость линкора — список ресурсов с базовой стоимостью (ТЗ «Доработки №3», п.7): древесина 1000, золото 100,
  // ткань 1000, сталь 250 — меньше нельзя, убрать нельзя (замок); увеличить можно, свой ресурс — добавить и убрать.
  CHECK(sel(h, schema::cst::ShipLineCost));
  const Id wood = resByName(h->world(), "Древесина");
  CHECK(wood != 0);
  {
    const Constant& c = rules::constantOf(h->world(), schema::cst::ShipLineCost);
    CHECK_EQ(c.res.size(), size_t(4));
    CHECK_NEAR(c.res.count(wood) ? c.res.at(wood) : -1, 1000.0, 1e-9);
    CHECK_NEAR(c.res.count(kGold) ? c.res.at(kGold) : -1, 100.0, 1e-9);
  }
  CHECK(reveal(h, "constants.shipLineCost.res." + std::to_string(wood), kArea, kTop));
  CHECK(h->uiRect("constants.shipLineCost.res." + std::to_string(wood) + ".lock") != nullptr);
  CHECK(h->uiRect("constants.shipLineCost.res." + std::to_string(wood) + ".remove") == nullptr);
  CHECK(enterValue(h, "constants.shipLineCost.res." + std::to_string(wood), "40", kArea, kTop));   // ниже базовой — нельзя
  CHECK_NEAR(rules::constantOf(h->world(), schema::cst::ShipLineCost).res.at(wood), 1000.0, 1e-9);
  CHECK(enterValue(h, "constants.shipLineCost.res." + std::to_string(wood), "1200", kArea, kTop));
  CHECK_NEAR(rules::constantOf(h->world(), schema::cst::ShipLineCost).res.at(wood), 1200.0, 1e-9);
  CHECK(enterValue(h, "constants.shipLineCost.res." + std::to_string(kGold), "250.125", kArea, kTop));
  CHECK_NEAR(rules::constantOf(h->world(), schema::cst::ShipLineCost).res.at(kGold), 250.125, 1e-9);
  CHECK(pickInCombo(h, "constants.shipLineCost.addres", "Уголь", kArea, kTop));
  const Id coal = resByName(h->world(), "Уголь");
  CHECK(coal != 0);
  CHECK_EQ(rules::constantOf(h->world(), schema::cst::ShipLineCost).res.size(), size_t(5));
  CHECK(enterValue(h, "constants.shipLineCost.res." + std::to_string(coal), "7.5", kArea, kTop));
  CHECK_NEAR(rules::constantOf(h->world(), schema::cst::ShipLineCost).res.at(coal), 7.5, 1e-9);
  CHECK(clickRevealed(h, "constants.shipLineCost.res." + std::to_string(coal) + ".remove", kArea, kTop));
  quick(h);
  CHECK_EQ(rules::constantOf(h->world(), schema::cst::ShipLineCost).res.count(coal), size_t(0));
  CHECK_EQ(rules::constantOf(h->world(), schema::cst::ShipLineCost).res.size(), size_t(4));
  // Расы для отрядов: базовые закреплены (замок, без удаления).
  CHECK(sel(h, schema::cst::UnitRaces));
  CHECK(reveal(h, "constants.unitRaces.addval", kArea, kTop));
  CHECK(h->uiRect("constants.unitRaces.val.0.lock") != nullptr);
  CHECK(h->uiRect("constants.unitRaces.val.0.remove") == nullptr);
  // Своя раса: добавить, назначить строке войск, переименовать (строка — следом), удалить (строка — по виду государства).
  CHECK(clickRevealed(h, "constants.unitRaces.addval", kArea, kTop));
  h.type("Драконы");
  h.key(Key::Enter);
  quick(h);
  CHECK(hasValue(h->world(), schema::cst::UnitRaces, "Драконы"));
  auto row = armyRow(h->world());
  CHECK(row.first != 0);
  CHECK(h->act("Раса отряда", [&](Tx& tx) { rules::setRowRace(tx, row.first, row.second, "Драконы"); }));
  quick(h);
  const size_t idx = rules::unitRaces(h->world()).size() - 1;
  const std::string mark = "constants.unitRaces.val." + std::to_string(idx);
  CHECK(enterValue(h, mark, "Драконы-маги", kArea, kTop));
  CHECK(hasValue(h->world(), schema::cst::UnitRaces, "Драконы-маги"));
  CHECK(!hasValue(h->world(), schema::cst::UnitRaces, "Драконы"));
  CHECK_EQ(rowRace(h->world(), row), std::string("Драконы-маги"));
  // Повтор базовой расы — отказ.
  CHECK(enterValue(h, mark, "живой", kArea, kTop));
  CHECK(hasValue(h->world(), schema::cst::UnitRaces, "Драконы-маги"));
  shotClean(h, "editors_constants_races");
  CHECK(clickRevealed(h, mark + ".remove", kArea, kTop));
  quick(h);
  CHECK(!hasValue(h->world(), schema::cst::UnitRaces, "Драконы-маги"));
  CHECK_EQ(rowRace(h->world(), row), std::string());
  for (const char* b : {schema::kRaceLiving, schema::kRaceDemonic, schema::kRaceUndead, schema::kRaceMechanical, schema::kRaceElemental})
    CHECK(hasValue(h->world(), schema::cst::UnitRaces, b));
}

TEST(app_editors_constants_user_and_window) {
  Harness h("editors_constants_user");
  h.demo();
  h.dropToasts();
  CHECK(sel(h, schema::cst::ColonizationCost));
  // «+» → «Число»: своя константа, название получает фокус.
  CHECK(h.clickUi("constants.new"));
  quick(h);
  CHECK(h.clickUi("constants.new.number"));
  quick(h);
  const Constant* c = h->world().constants->find("user1");
  CHECK(c != nullptr);
  CHECK(c && c->type == ConstType::Number && !c->builtin);
  h.retype("Налог на соль");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(rules::constantOf(h->world(), "user1").name, std::string("Налог на соль"));
  CHECK(enterValue(h, "constants.user1.num", "-7,5", kArea, kTop));
  CHECK_NEAR(num(h->world(), "user1"), -7.5, 1e-12);
  // Название встроенной занято — отказ.
  CHECK(enterValue(h, "constants.user1.name", "Стоимость колонизации", kArea, kTop));
  CHECK_EQ(rules::constantOf(h->world(), "user1").name, std::string("Налог на соль"));
  // «+» → «Список значений».
  CHECK(clickRevealed(h, "constants.new", kArea, kTop));
  quick(h);
  CHECK(h.clickUi("constants.new.values"));
  quick(h);
  const Constant* v = h->world().constants->find("user2");
  CHECK(v && v->type == ConstType::Values);
  h.key(Key::Escape);   // фокус названия — снять, имя по умолчанию
  quick(h);
  CHECK(clickRevealed(h, "constants.user2.addval", kArea, kTop));
  h.type("Север");
  h.key(Key::Enter);
  quick(h);
  CHECK(hasValue(h->world(), "user2", "Север"));
  // Удаление своей константы — с подтверждением; Ctrl+Z возвращает.
  CHECK(sel(h, "user1"));
  CHECK(clickRevealed(h, "constants.user1.delete", kArea, kTop));
  CHECK(confirmDialog(h));
  CHECK(h->world().constants->find("user1") == nullptr);
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK(h->world().constants->find("user1") != nullptr);
  // Окно констант: список и карточка.
  CHECK_EQ(h->ui.editor, std::string("constants"));
  CHECK(h.clickUi("constants.list.frigateCost"));
  quick(h);
  // Стоимость фрегата: 4 базовых ресурса (древесина, золото, ткань, железо) и добавленный уголь.
  CHECK_EQ(rules::constantOf(h->world(), schema::cst::FrigateCost).res.size(), size_t(4));
  CHECK(pickInCombo(h, "constants.frigateCost.addres", "Уголь"));
  CHECK_EQ(rules::constantOf(h->world(), schema::cst::FrigateCost).res.size(), size_t(5));
  shotClean(h, "editors_constants_window");
  CHECK(clickRevealed(h, "constants.list.user2", kArea, kTop));   // свои — ниже 14 встроенных: список прокручивается
  quick(h);
  CHECK(h->uiRect("constants.user2.delete") != nullptr);
  shotClean(h, "editors_constants_window_values");
  // Прошлый ход — только просмотр.
  h->closeEditor();
  quick(h);
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK(h->viewTurn(1));
  quick(h);
  CHECK(sel(h, schema::cst::ColonizationCost));
  CHECK(h->readOnly());
  double was = num(h->world(), schema::cst::ColonizationCost);
  CHECK(enterValue(h, "constants.colonizationCost.num", "99", kArea, kTop));
  CHECK_NEAR(num(h->world(), schema::cst::ColonizationCost), was, 1e-12);
  CHECK_NEAR(num(h->store.world(), schema::cst::ColonizationCost), was, 1e-12);
  h->backToCurrent();
  quick(h);
}
