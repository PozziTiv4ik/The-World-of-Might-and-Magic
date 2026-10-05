// Сценарии «Глобальных констант» (ТЗ «Общие доработки», п.1–2): вкладка левой ленты и окно — встроенные видны
// всегда, первая правка создаёт запись мира (Ctrl+Z возвращает шаблон), золото до тысячных, список ресурсов,
// список значений (базовые расы закреплены; переименование и удаление своей расы правит строки войск),
// своя константа (создание по типу, переименование, удаление с подтверждением), просмотр прошлого хода.
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;

namespace {

const char* kDrawer = "drawer";
constexpr float kTop = 40;

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

bool hasValue(const World& w, const char* key, const std::string& v) {
  const Constant& c = rules::constantOf(w, key);
  return std::find(c.values.begin(), c.values.end(), v) != c.values.end();
}

}  // namespace

TEST(app_editors_constants_builtin) {
  Harness h("editors_constants_builtin");
  h.demo();
  h.dropToasts();
  // Лента слева → «Глобальные константы».
  CHECK(h.clickUi("drawer.constants"));
  quick(h);
  CHECK_EQ(h->ui.drawer, std::string("constants"));
  for (const Constant& c : schema::builtinConstants()) CHECK_MSG(h->uiRect("constants." + c.key + ".name") != nullptr, c.key);
  CHECK(h->uiRect("constants.colonizationCost.delete") == nullptr);   // встроенные не удаляются
  shotClean(h, "editors_constants_drawer");
  // Стоимость колонизации — золото до тысячных: первая правка создаёт запись мира.
  CHECK(h->world().constants->find(schema::cst::ColonizationCost) == nullptr);
  CHECK(enterValue(h, "constants.colonizationCost.num", "12,345", kDrawer, kTop));
  CHECK(h->world().constants->find(schema::cst::ColonizationCost) != nullptr);
  CHECK_NEAR(num(h->world(), schema::cst::ColonizationCost), 12.345, 1e-9);
  // Ctrl+Z — снова значение по умолчанию (записи нет).
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK(h->world().constants->find(schema::cst::ColonizationCost) == nullptr);
  CHECK_NEAR(num(h->world(), schema::cst::ColonizationCost), 0.0, 1e-12);
  // Трупов на воина-нежить: по умолчанию 1, правка — 2,5.
  CHECK_NEAR(num(h->world(), schema::cst::CorpsesPerUnit), 1.0, 1e-12);
  CHECK(enterValue(h, "constants.corpsesPerUnit.num", "2.5", kDrawer, kTop));
  CHECK_NEAR(num(h->world(), schema::cst::CorpsesPerUnit), 2.5, 1e-12);
  // Стоимость линкора — список ресурсов: древесина 40, золото 250,125; золото убирается крестиком.
  CHECK(pickInCombo(h, "constants.shipLineCost.addres", "Древесина", kDrawer, kTop));
  Id wood = 0;
  for (auto& [rid, v] : rules::constantOf(h->world(), schema::cst::ShipLineCost).res) wood = rid;
  CHECK(wood != 0);
  CHECK(enterValue(h, "constants.shipLineCost.res." + std::to_string(wood), "40", kDrawer, kTop));
  CHECK(pickInCombo(h, "constants.shipLineCost.addres", "Золото", kDrawer, kTop));
  CHECK(enterValue(h, "constants.shipLineCost.res." + std::to_string(kGold), "250.125", kDrawer, kTop));
  {
    const Constant& c = rules::constantOf(h->world(), schema::cst::ShipLineCost);
    CHECK_EQ(c.res.size(), size_t(2));
    CHECK_NEAR(c.res.count(wood) ? c.res.at(wood) : -1, 40.0, 1e-9);
    CHECK_NEAR(c.res.count(kGold) ? c.res.at(kGold) : -1, 250.125, 1e-9);
  }
  CHECK(clickRevealed(h, "constants.shipLineCost.res." + std::to_string(kGold) + ".remove", kDrawer, kTop));
  quick(h);
  CHECK_EQ(rules::constantOf(h->world(), schema::cst::ShipLineCost).res.count(kGold), size_t(0));
  // Расы для отрядов: базовые закреплены (замок, без удаления).
  CHECK(reveal(h, "constants.unitRaces.addval", kDrawer, kTop));
  CHECK(h->uiRect("constants.unitRaces.val.0.lock") != nullptr);
  CHECK(h->uiRect("constants.unitRaces.val.0.remove") == nullptr);
  // Своя раса: добавить, назначить строке войск, переименовать (строка — следом), удалить (строка — по виду государства).
  CHECK(clickRevealed(h, "constants.unitRaces.addval", kDrawer, kTop));
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
  CHECK(enterValue(h, mark, "Драконы-маги", kDrawer, kTop));
  CHECK(hasValue(h->world(), schema::cst::UnitRaces, "Драконы-маги"));
  CHECK(!hasValue(h->world(), schema::cst::UnitRaces, "Драконы"));
  CHECK_EQ(rowRace(h->world(), row), std::string("Драконы-маги"));
  // Повтор базовой расы — отказ.
  CHECK(enterValue(h, mark, "живой", kDrawer, kTop));
  CHECK(hasValue(h->world(), schema::cst::UnitRaces, "Драконы-маги"));
  shotClean(h, "editors_constants_races");
  CHECK(clickRevealed(h, mark + ".remove", kDrawer, kTop));
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
  CHECK(h.clickUi("drawer.constants"));
  quick(h);
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
  CHECK(enterValue(h, "constants.user1.num", "-7,5", kDrawer, kTop));
  CHECK_NEAR(num(h->world(), "user1"), -7.5, 1e-12);
  // Название встроенной занято — отказ.
  CHECK(enterValue(h, "constants.user1.name", "Стоимость колонизации", kDrawer, kTop));
  CHECK_EQ(rules::constantOf(h->world(), "user1").name, std::string("Налог на соль"));
  // «+» → «Список значений».
  CHECK(clickRevealed(h, "constants.new", kDrawer, kTop));
  quick(h);
  CHECK(h.clickUi("constants.new.values"));
  quick(h);
  const Constant* v = h->world().constants->find("user2");
  CHECK(v && v->type == ConstType::Values);
  h.key(Key::Escape);   // фокус названия — снять, имя по умолчанию
  quick(h);
  CHECK(clickRevealed(h, "constants.user2.addval", kDrawer, kTop));
  h.type("Север");
  h.key(Key::Enter);
  quick(h);
  CHECK(hasValue(h->world(), "user2", "Север"));
  // Удаление своей константы — с подтверждением; Ctrl+Z возвращает.
  CHECK(clickRevealed(h, "constants.user1.delete", kDrawer, kTop));
  CHECK(confirmDialog(h));
  CHECK(h->world().constants->find("user1") == nullptr);
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK(h->world().constants->find("user1") != nullptr);
  // Окно констант: список и карточка.
  CHECK(clickRevealed(h, "drawer.constants.open", kDrawer, kTop));
  quick(h);
  CHECK_EQ(h->ui.editor, std::string("constants"));
  CHECK(h.clickUi("constants.list.frigateCost"));
  quick(h);
  CHECK(pickInCombo(h, "constants.frigateCost.addres", "Железо"));
  CHECK_EQ(rules::constantOf(h->world(), schema::cst::FrigateCost).res.size(), size_t(1));
  shotClean(h, "editors_constants_window");
  CHECK(h.clickUi("constants.list.user2"));
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
  if (h->ui.drawer != "constants") CHECK(h.clickUi("drawer.constants"));
  quick(h);
  CHECK(h->readOnly());
  double was = num(h->world(), schema::cst::ColonizationCost);
  CHECK(enterValue(h, "constants.colonizationCost.num", "99", kDrawer, kTop));
  CHECK_NEAR(num(h->world(), schema::cst::ColonizationCost), was, 1e-12);
  CHECK_NEAR(num(h->store.world(), schema::cst::ColonizationCost), was, 1e-12);
  h->backToCurrent();
  quick(h);
}
