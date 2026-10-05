// Сценарии колонизации (ТЗ «Общие доработки», п.3; «Модификаторы», 1.3): кнопка «Колонизировать» у провинции без
// владельца, выбор государства, стоимость — константа «Стоимость колонизации» (до тысячных), отмена; при нехватке
// золота пункт недоступен; опустошённую провинцию назначить нельзя — причина в уведомлении. Лорд — только герой
// государства-владельца (rules::setLord).
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

double treasury(Harness& h, Id f) { return h->world().faction(f)->treasury(); }

// Два разных государства (по ID).
std::pair<Id, Id> twoStates(const World& w) {
  std::vector<Id> st;
  w.factions.each([&](const Faction& f) {
    if (f.isState()) st.push_back(f.id);
  });
  return st.size() >= 2 ? std::make_pair(st[0], st[1]) : std::make_pair(Id(0), Id(0));
}

}  // namespace

TEST(app_economy_colonize) {
  HideTestRegs hide;
  Harness h("economy_colonize", 1440, 1000);
  h.demo();
  const Id pid = unownedLand(h->world());
  CHECK(pid != 0);
  auto [rich, poor] = twoStates(h->world());
  CHECK(rich && poor);
  const double cost = 125.375;
  CHECK(h->act("Подготовка", [&](Tx& tx) {
    rules::ensureConstant(tx, schema::cst::ColonizationCost).num = cost;
    tx.faction(rich).res[kGold] = 1000;
    tx.faction(poor).res[kGold] = 10.5;
  }));
  openProvince(h, pid, "province.overview");
  CHECK(h->uiRect("province.colonize") != nullptr);
  CHECK(h->uiRect("province.colonizeCost") != nullptr);
  shotClean(h, "economy_colonize_button");

  // Выбор государства: всплывающий список с казной; нехватка золота — пункт недоступен.
  CHECK(h.clickUi("province.colonize"));
  h.settle();
  CHECK(h->uiRect("province.colonize." + std::to_string(rich)) != nullptr);
  CHECK(h->uiRect("province.colonize." + std::to_string(poor)) != nullptr);
  shotClean(h, "economy_colonize_popup");
  u64 v0 = h->store.version();
  CHECK(h.clickUi("province.colonize." + std::to_string(poor)));
  h.settle();
  CHECK_EQ(h->store.version(), v0);
  CHECK_EQ(prov(h, pid)->owner, Id(0));
  if (!h->uiRect("province.colonize." + std::to_string(rich))) {
    CHECK(h.clickUi("province.colonize"));
    h.settle();
  }
  // Колонизация: владелец — выбранное государство, из казны — стоимость (до тысячных).
  CHECK(h.clickUi("province.colonize." + std::to_string(rich)));
  h.settle();
  CHECK_EQ(prov(h, pid)->owner, rich);
  CHECK_NEAR(treasury(h, rich), 1000 - cost, 1e-9);
  CHECK_EQ(h->store.undoLabel(), std::string("Колонизация"));
  CHECK(h->uiRect("province.colonize") == nullptr);
  bool logged = false;
  h->world().log.each([&](const LogEntry& e) { logged = logged || (e.province == pid && e.text.find("колонизирует") != std::string::npos); });
  CHECK(logged);
  // Отмена возвращает и владельца, и золото.
  h.key(Key::Z, ctrl());
  h.settle();
  CHECK_EQ(prov(h, pid)->owner, Id(0));
  CHECK_NEAR(treasury(h, rich), 1000, 1e-9);

  // Опустошённая провинция: правило отказывает, причина — в уведомлении; мир не меняется.
  CHECK(h->act("Опустошение", [&](Tx& tx) {
    rules::addModifier(tx, rules::ModTarget::Province, pid, rules::ensureBuiltinMod(tx, schema::mod::Devastated));
  }));
  h.settle();
  CHECK_EQ(prov(h, pid)->modTurns.size(), size_t(1));   // срок по умолчанию — 5 ходов
  CHECK(h.clickUi("province.colonize"));
  h.settle();
  h->toasts().clear();
  CHECK(h.clickUi("province.colonize." + std::to_string(rich)));
  h.settle();
  CHECK_EQ(prov(h, pid)->owner, Id(0));
  CHECK_NEAR(treasury(h, rich), 1000, 1e-9);
  CHECK(hasToast(h, "Опустошённую"));
  h->toasts().clear();
  // Назначение владельца списком — тоже отказ.
  CHECK(!h->act("Владелец", [&](Tx& tx) { rules::setProvinceOwner(tx, pid, rich); }));
  CHECK_EQ(prov(h, pid)->owner, Id(0));
  shotClean(h, "economy_colonize_devastated");
}

TEST(app_economy_lord_own_heroes) {
  HideTestRegs hide;
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
