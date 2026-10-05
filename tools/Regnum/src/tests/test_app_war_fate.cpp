// Сценарии окна «Судьба героев» (ТЗ «Механика героев», п.1): войско уничтожено в битве — его герои сбегают, гибнут
// (модификатор «Мертв», место захоронения — провинция битвы) или попадают в плен к победителю (модификатор
// «Взят в плен», список пленников государства); закрытие окна без решения — все сбежали.
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

// Битва: войско Хельдвига с двумя героями уничтожено — окно судьбы; один убит, другой взят в плен Валь-Кетрой.
TEST(app_war_fate_after_battle) {
  Harness h("war_fate_battle", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  Id hel = factionByName(h->world(), "Хельдвиг");
  Id vk = factionByName(h->world(), "Валь-Кетра");
  Id y = armyOf(h->world(), vk, ArmyKind::Army);
  CHECK(y != 0);
  const Id h1 = freeHero(h, hel);
  CHECK(h1 != 0);
  Id x = spawnArmy(h, hel, h->world().army(y)->pos + Vec2(-200, 30), 300, h1);
  CHECK(x != 0);
  if (!x) return;
  const Id h2 = freeHero(h, hel);
  CHECK(h2 != 0 && h2 != h1);
  CHECK(h->act("Второй герой", [&](Tx& tx) { rules::setHero(tx, x, h2, true); }));
  const Id prov = app::mil::provinceUnder(h->world(), h->world().army(y)->pos);
  app::mil::openBattle(h.a(), x, y, h->world().army(x)->pos);
  h.settle();
  CHECK(h->hasDialog("battle"));
  const Id row = h->world().army(x)->groups[0].units[0].row;
  CHECK(enterNumber(h, "battle.loss." + std::to_string(x) + "." + std::to_string(row), 300));
  CHECK(h.clickUi("battle.apply"));
  h.settle();
  CHECK(h->world().army(x) == nullptr);
  CHECK(h->hasDialog("hero.fate"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_fate_dialog"));
  CHECK(h.clickUi("fate." + std::to_string(h1) + ".1"));
  CHECK(h.clickUi("fate." + std::to_string(h2) + ".2"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_fate_choice"));
  CHECK(h.clickUi("fate.ok"));
  h.settle();
  CHECK(!h->hasDialog("hero.fate"));
  CHECK(rules::characterHas(h->world(), h1, schema::mod::Dead));
  CHECK_EQ(h->world().character(h1)->burial, prov);
  CHECK(rules::characterHas(h->world(), h2, schema::mod::Captive));
  CHECK_EQ(h->world().character(h2)->captor, vk);
  const std::vector<Id> captives = rules::captivesOf(h->world(), vk);
  CHECK(std::find(captives.begin(), captives.end(), h2) != captives.end());
  CHECK_EQ(h->world().character(h2)->faction, hel);   // пленник остаётся героем своего государства
  // Мёртвого и пленного героя в войско не предлагают.
  const Id other = armyOf(h->world(), hel, ArmyKind::Army);
  CHECK(other != 0);
  h->ui.tabOf[app::SelType::Army] = "army.units";
  for (Id gone : {h1, h2}) {
    h->select(app::SelType::Army, other);
    h.settle();
    CHECK(reveal(h, "army.addhero." + std::to_string(hel)));
    CHECK(h.clickUi("army.addhero." + std::to_string(hel)));
    h.type(h->world().characterName(gone));
    h.key(Key::Enter);
    h.settle();
    h.key(Key::Escape);
    h.settle();
    const std::vector<Id>& hs = h->world().army(other)->groups[0].heroes;
    CHECK(std::find(hs.begin(), hs.end(), gone) == hs.end());
  }
  // Отмена — одним шагом.
  h.key(Key::Z, ctrl());
  CHECK(rules::heroAvailable(h->world(), h1));
  CHECK(rules::heroAvailable(h->world(), h2));
}

// Окно напрямую: «Все в плен» недоступно без пленившего; закрытие без решения — мир не меняется.
TEST(app_war_fate_dismiss_and_all) {
  Harness h("war_fate_dismiss", 1440, 1000);
  h.demo();
  Id hel = factionByName(h->world(), "Хельдвиг");
  Id vk = factionByName(h->world(), "Валь-Кетра");
  const Id h1 = freeHero(h, hel), h2 = freeHero(h, vk);
  CHECK(h1 && h2);
  bool done = false;
  // Без пленившего: плен не предлагается.
  app::flow::openHeroFate(h.a(), {h1, h2}, 0, 0, [&](app::App&) { done = true; });
  h.settle();
  CHECK(h->hasDialog("hero.fate"));
  CHECK(h->uiRect("fate." + std::to_string(h1) + ".2") == nullptr);
  World before = h->world();
  h.key(Key::Escape);
  h.settle();
  CHECK(done);
  CHECK(!h->hasDialog("hero.fate"));
  CHECK_EQ(World::diff(before, h->world()), 0u);
  // Пленившее — Валь-Кетра: своего героя она пленить не может; «Все убиты».
  done = false;
  app::flow::openHeroFate(h.a(), {h1, h2}, vk, 0, [&](app::App&) { done = true; });
  h.settle();
  CHECK(h->uiRect("fate." + std::to_string(h1) + ".2") != nullptr);
  CHECK(h->uiRect("fate." + std::to_string(h2) + ".2") == nullptr);
  CHECK(h.clickUi("fate.all.1"));
  CHECK(h.clickUi("fate.ok"));
  h.settle();
  CHECK(done);
  CHECK(rules::characterHas(h->world(), h1, schema::mod::Dead));
  CHECK(rules::characterHas(h->world(), h2, schema::mod::Dead));
}
