// Сценарии колонизации (ТЗ «Общие доработки», п.3; «Модификаторы», 1.3; «Доработки №2–4»): кнопка «Колонизировать» у
// провинции без владельца — окно: государство (казна; при нехватке золота или жителей пункт недоступен), переселенцы —
// не меньше 500 и не больше жителей государства, стоимость — константа «Стоимость колонизации» (до тысячных), отмена;
// опустошённую провинцию назначить нельзя — причина в окне и в отказе правила. Игрок колонизирует только за своё
// государство. Лорд — только герой государства-владельца (rules::setLord).
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

double treasury(Harness& h, Id f) { return h->world().faction(f)->treasury(); }

// Выбор в выпадающем списке окна: щелчок, поиск по названию, Enter.
void pickIn(Harness& h, const std::string& combo, const std::string& text) {
  CHECK(h.clickUi(combo));
  h.settle(40);
  h.type(text);
  h.key(Key::Enter);
  h.settle(40);
}

}  // namespace

TEST(app_economy_colonize) {
  Harness h("economy_colonize", 1440, 1000);
  h.demo();
  const Id pid = unownedLand(h->world());
  CHECK(pid != 0);
  auto [rich, poor] = test::twoStates(h->world());
  CHECK(rich && poor);
  const double cost = 125.375;
  CHECK(h->act("Подготовка", [&](Tx& tx) {
    rules::ensureConstant(tx, schema::cst::ColonizationCost).num = cost;
    tx.faction(rich).res[kGold] = 1000;
    tx.faction(poor).res[kGold] = 10.5;
  }));
  const i64 pop0 = popOf(*prov(h, pid)), richPop0 = rules::statePopulation(h->world(), rich);
  CHECK(rules::settlersAvailable(h->world(), rich, pid) >= 800);
  openProvince(h, pid, "province.overview");
  CHECK(h->uiRect("province.colonize") != nullptr);
  CHECK(h->uiRect("province.colonizeCost") != nullptr);
  shotClean(h, "economy_colonize_button");

  // Окно: государство и переселенцы; бедное государство недоступно (казна) — выбор не меняется.
  CHECK(h.clickUi("province.colonize"));
  h.settle();
  CHECK(h->hasDialog("province.colonize"));
  CHECK(h->uiRect("colonize.state") != nullptr);
  CHECK(h->uiRect("colonize.settlers") != nullptr);
  CHECK(h->uiRect("colonize.cost") != nullptr);
  u64 v0 = h->store.version();
  pickIn(h, "colonize.state", h->world().faction(poor)->name);
  CHECK(h.clickUi("colonize.ok"));
  h.settle();
  CHECK_EQ(h->store.version(), v0);
  CHECK_EQ(prov(h, pid)->owner, Id(0));
  CHECK(h->hasDialog("province.colonize"));
  // Богатое: 800 переселенцев.
  pickIn(h, "colonize.state", h->world().faction(rich)->name);
  enterInDialog(h, "colonize.settlers", "800");
  shotClean(h, "economy_colonize_dialog");
  // Меньше 500 поле не принимает; затем снова 800.
  enterInDialog(h, "colonize.settlers", "120");
  enterInDialog(h, "colonize.settlers", "800");
  CHECK(h.clickUi("colonize.ok"));
  h.settle();
  CHECK(!h->hasDialog("province.colonize"));
  CHECK_EQ(prov(h, pid)->owner, rich);
  CHECK_NEAR(treasury(h, rich), 1000 - cost, 1e-9);
  CHECK_EQ(popOf(*prov(h, pid)), pop0 + 800);
  CHECK_EQ(rules::statePopulation(h->world(), rich), richPop0 + pop0);   // переселенцы — из провинций государства
  CHECK_EQ(h->store.undoLabel(), std::string("Колонизация"));
  CHECK(h->uiRect("province.colonize") == nullptr);
  bool logged = false;
  h->world().log.each([&](const LogEntry& e) { logged = logged || (e.province == pid && e.text.find("колонизирует") != std::string::npos); });
  CHECK(logged);
  // Отмена возвращает и владельца, и золото, и жителей.
  h.key(Key::Z, ctrl());
  h.settle();
  CHECK_EQ(prov(h, pid)->owner, Id(0));
  CHECK_NEAR(treasury(h, rich), 1000, 1e-9);
  CHECK_EQ(popOf(*prov(h, pid)), pop0);

  // Опустошённая провинция: в окне — причина, «Колонизировать» недоступна; правило тоже отказывает.
  CHECK(h->act("Опустошение", [&](Tx& tx) {
    rules::addModifier(tx, rules::ModTarget::Province, pid, rules::ensureBuiltinMod(tx, schema::mod::Devastated));
  }));
  h.settle();
  CHECK_EQ(prov(h, pid)->modTurns.size(), size_t(1));   // срок по умолчанию — 5 ходов
  CHECK(h.clickUi("province.colonize"));
  h.settle();
  pickIn(h, "colonize.state", h->world().faction(rich)->name);
  CHECK(h->uiRect("colonize.why") != nullptr);
  v0 = h->store.version();
  CHECK(h.clickUi("colonize.ok"));
  h.settle();
  CHECK_EQ(h->store.version(), v0);
  CHECK_EQ(prov(h, pid)->owner, Id(0));
  shotClean(h, "economy_colonize_devastated");
  CHECK(h.clickUi("dialog.cancel"));
  h.settle();
  h->toasts().clear();
  CHECK(!h->act("Колонизация", [&](Tx& tx) { rules::colonize(tx, pid, rich, 500); }));
  CHECK(hasToast(h, "Опустошённую"));
  h->toasts().clear();
  // Назначение владельца списком — тоже отказ.
  CHECK(!h->act("Владелец", [&](Tx& tx) { rules::setProvinceOwner(tx, pid, rich); }));
  CHECK_EQ(prov(h, pid)->owner, Id(0));
}

// Игрок: в окне — только своё государство (выбрано сразу), колонизация — игровое действие; владелец провинции списком
// — правка мира (недоступна).
TEST(app_economy_colonize_player) {
  Harness h("economy_colonize_player", 1440, 1000);
  h.demo();
  const Id pid = unownedLand(h->world());
  const Id st = biggestState(h->world());
  CHECK(pid && st);
  CHECK(h->act("Казна", [&](Tx& tx) {
    rules::ensureConstant(tx, schema::cst::ColonizationCost).num = 50;
    tx.faction(st).res[kGold] = 500;
  }));
  h->ui.role = app::Role::Player;
  h->ui.playerState = st;
  openProvince(h, pid, "province.overview");
  // Владелец — правка мира: у игрока список недоступен.
  u64 v0 = h->store.version();
  CHECK(h.clickUi("province.owner"));
  h.settle(40);
  CHECK_EQ(h->store.version(), v0);
  CHECK(h.clickUi("province.colonize"));
  h.settle();
  CHECK(h->hasDialog("province.colonize"));
  CHECK(h.clickUi("colonize.ok"));
  h.settle();
  CHECK_EQ(prov(h, pid)->owner, st);
  CHECK_NEAR(treasury(h, st), 450, 1e-9);
  CHECK_EQ(popOf(*prov(h, pid)) >= schema::kMinSettlers, true);
}

TEST(app_economy_lord_own_heroes) {
  Harness h("economy_lord", 1440, 1000);
  h.demo();
  const Id st = biggestState(h->world());
  CHECK(st != 0);
  const Id pid = populousProvince(h->world(), st);
  CHECK(pid != 0);
  // Герой своего государства и герой чужого.
  Id own = 0, foreign = 0;
  h->world().characters.each([&](const Character& c) {
    if (!own && c.faction == st && rules::heroAvailable(h->world(), c.id)) own = c.id;
    if (!foreign && c.faction && c.faction != st) foreign = c.id;
  });
  CHECK(own && foreign);
  openProvince(h, pid, "province.overview");
  CHECK(h->uiRect("province.lord") != nullptr);
  // Правило назначения: чужого героя — отказ, своего — можно; снять — всегда.
  CHECK(!h->act("Лорд провинции", [&](Tx& tx) { rules::setLord(tx, pid, foreign); }));
  CHECK(prov(h, pid)->lord != foreign);
  CHECK(h->act("Лорд провинции", [&](Tx& tx) { rules::setLord(tx, pid, own); }));
  CHECK_EQ(prov(h, pid)->lord, own);
  // Мёртвого героя назначить нельзя.
  CHECK(h->act("Мертв", [&](Tx& tx) { rules::addModifier(tx, rules::ModTarget::Character, own, rules::ensureBuiltinMod(tx, schema::mod::Dead)); }));
  CHECK_EQ(prov(h, pid)->lord, Id(0));   // «Мертв» снимает со всех назначений
  CHECK(!h->act("Лорд провинции", [&](Tx& tx) { rules::setLord(tx, pid, own); }));
  h.settle();
  shotClean(h, "economy_lord");
}
