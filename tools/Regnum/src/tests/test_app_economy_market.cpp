// Сценарии рынка (ТЗ «Доработки №2–4»): у достроенного рынка в провинции — «Продать реликвии»; окно рынка — стопки
// реликвий из хранилищ реликвий государства (где лежит, редкость, количество, цена за штуку; археологические находки
// дороже), выбор количества, «Продать» — выручка в казну одной отменяемой транзакцией. Игрок продаёт только реликвии
// своего государства.
#include "tests/test_app_economy_util.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

struct MarketFix {
  Id state = 0, pid = 0, market = 0, vault = 0;
  Id plain = 0, find = 0;   // копируемая реликвия и археологическая находка
};

// Государство с рынком и хранилищем реликвий в провинции: 5 штук обычной реликвии и 3 находки в хранилище.
MarketFix setupMarket(Harness& h) {
  MarketFix f;
  const apptest::trees::StateProv sp = apptest::trees::stateWithFreeSlots(h->world(), 2);
  f.state = sp.state;
  f.pid = sp.province;
  CHECK(f.state && f.pid);
  f.market = test::buildingByKey(h->world(), schema::bld::Market);
  f.vault = test::buildingByKey(h->world(), schema::bld::RelicVault);
  CHECK(f.market && f.vault);
  for (const Relic& r : h->world().catalogs->relics) {
    if (r.unique()) continue;
    if (rules::isArchFind(h->world(), r.id)) {
      if (!f.find) f.find = r.id;
    } else if (!f.plain) {
      f.plain = r.id;
    }
  }
  CHECK(f.plain && f.find);
  CHECK(h->act("Рынок и хранилище", [&](Tx& tx) {
    rules::setBuildingFlag(tx, f.market, rules::BuildingFlag::Market, true);
    rules::placeBuilding(tx, f.pid, f.market, 1);
    rules::placeBuilding(tx, f.pid, f.vault, 1);
    const rules::RelicPlace at{rules::RelicPlace::Building, f.pid, f.vault, f.state};
    rules::setRelicCount(tx, f.plain, at, 5);
    rules::setRelicCount(tx, f.find, at, 3);
  }));
  return f;
}

i64 countIn(Harness& h, const MarketFix& f, Id relic) {
  return rules::relicCountAt(h->world(), relic, rules::RelicPlace{rules::RelicPlace::Building, f.pid, f.vault, f.state});
}

}  // namespace

TEST(app_economy_market_sell) {
  Harness h("economy_market", 1440, 1100);
  h.demo();
  h.dropToasts();
  const MarketFix f = setupMarket(h);
  CHECK(rules::hasMarket(h->world(), f.state));
  // Находка дороже обычной реликвии той же редкости.
  CHECK(rules::relicPrice(h->world(), f.find) > 0 && rules::relicPrice(h->world(), f.plain) > 0);
  openProvince(h, f.pid, "province.buildings");
  const std::string btn = "prov.market." + std::to_string(f.market);
  CHECK(apptest::trees::revealIn(h, btn));
  apptest::trees::clickQuick(h, btn);
  CHECK(h->hasDialog("market"));
  const std::string plainLot = "market.lot." + std::to_string(f.pid) + "." + std::to_string(f.plain);
  const std::string findLot = "market.lot." + std::to_string(f.pid) + "." + std::to_string(f.find);
  CHECK(h->uiRect(plainLot) != nullptr);
  CHECK(h->uiRect(findLot) != nullptr);
  // Ничего не выбрано — «Продать» недоступна.
  u64 v0 = h->store.version();
  CHECK(h.clickUi("market.sell"));
  h.settle(40);
  CHECK_EQ(h->store.version(), v0);
  // Все находки и две обычные реликвии.
  CHECK(h.clickUi(findLot + ".all"));
  h.settle(40);
  enterInDialog(h, plainLot + ".qty", "2");
  shotClean(h, "economy_market_dialog", 60);
  const double gold0 = h->world().faction(f.state)->treasury();
  const double expect = rules::relicPrice(h->world(), f.find) * 3 + rules::relicPrice(h->world(), f.plain) * 2;
  CHECK(h.clickUi("market.sell"));
  h.settle(40);
  CHECK_NEAR(h->world().faction(f.state)->treasury(), gold0 + expect, 1e-9);
  CHECK_EQ(countIn(h, f, f.find), i64(0));
  CHECK_EQ(countIn(h, f, f.plain), i64(3));
  CHECK_EQ(h->store.undoLabel(), std::string("Продать реликвии"));
  CHECK(ui::toastCount() > 0);   // уведомление над окном — в слое интерфейса
  // Проданные стопки исчезли из окна, оставшиеся — с новым количеством.
  CHECK(h->uiRect(findLot) == nullptr);
  CHECK(h->uiRect(plainLot) != nullptr);
  CHECK(h.clickUi("dialog.cancel"));
  h.settle(40);
  // Отмена возвращает реликвии и казну одним шагом.
  h.key(Key::Z, ctrl());
  h.settle(40);
  CHECK_NEAR(h->world().faction(f.state)->treasury(), gold0, 1e-9);
  CHECK_EQ(countIn(h, f, f.find), i64(3));
  CHECK_EQ(countIn(h, f, f.plain), i64(5));
}

// Игрок другого государства: кнопка рынка недоступна, окно рынка чужого государства — только просмотр.
TEST(app_economy_market_player_foreign) {
  Harness h("economy_market_player", 1440, 1100);
  h.demo();
  h.dropToasts();
  const MarketFix f = setupMarket(h);
  Id other = 0;
  h->world().factions.each([&](const Faction& x) {
    if (!other && x.isState() && x.id != f.state) other = x.id;
  });
  CHECK(other != 0);
  h->ui.role = app::Role::Player;
  h->ui.playerState = other;
  openProvince(h, f.pid, "province.buildings");
  const std::string btn = "prov.market." + std::to_string(f.market);
  CHECK(apptest::trees::revealIn(h, btn));
  apptest::trees::clickQuick(h, btn);
  CHECK(!h->hasDialog("market"));
  // Окно по имени (например, из палитры): продать нельзя.
  CHECK(h->openDialog("market", f.state));
  h.settle(40);
  const std::string findLot = "market.lot." + std::to_string(f.pid) + "." + std::to_string(f.find);
  CHECK(h.clickUi(findLot + ".all"));
  h.settle(40);
  const u64 v0 = h->store.version();
  CHECK(h.clickUi("market.sell"));
  h.settle(40);
  CHECK_EQ(h->store.version(), v0);
  CHECK_EQ(countIn(h, f, f.find), i64(3));
  // Своё государство — продаёт.
  h->closeDialogs();
  h->ui.playerState = f.state;
  CHECK(h->openDialog("market", f.state));
  h.settle(40);
  CHECK(h.clickUi(findLot + ".all"));
  h.settle(40);
  CHECK(h.clickUi("market.sell"));
  h.settle(40);
  CHECK_EQ(countIn(h, f, f.find), i64(0));
}
