// Права на героев (аудит 2026-10-23, 1.8, 1.9, 6.10 и раздел 11): у пленного и арестованного героя реликвии не выдаются
// и не возвращаются (кнопки выключены, причина — в подсказке); игрок меняет подкласс и сбрасывает таланты не чаще
// «Ходов между сменами класса» и не у мёртвых, пленных и арестованных; пленников государство отпускает или казнит (с
// подтверждением); администратор отправляет героя в плен или под арест с выбором государства, а «Взят в плен» и «Под
// арестом» в общем списке модификаторов героя не предлагаются.
#include "tests/test_app_faction_util.h"
#include "tests/test_app_heroes_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::herotest;
namespace fc = rg::factest;

namespace {

void openHero(Harness& h, Id hero, const char* tab) {
  h->ui.tabOf[app::SelType::Character] = tab;
  h->select(app::SelType::Character, hero);
  h.settle();
  h.dropToasts();
}

void asPlayer(Harness& h, Id state) {
  h->ui.role = app::Role::Player;
  h->ui.playerState = state;
  h.settle();
}

// Подсказка над элементом после наведения (указатель сначала уходит: после щелчка подсказка ждёт движения).
std::string tipOf(Harness& h, const std::string& mark) {
  const RectF* r = h->uiRect(mark);
  if (!r) return {};
  const RectF rr = *r;
  h.move(rr.x - 40, rr.cy() + 40);
  h.frames(4);
  h.move(rr.cx(), rr.cy());
  h.frames(40);
  return ui::tooltipText();
}

}  // namespace

// Пленный герой: «Вернуть» и «Выдать реликвию» выключены с причиной; отпущенный — снова доступен.
TEST(app_heroes_rights_captive_relics) {
  Harness h("heroes_rights_relics", 1440, 1000);
  h.demo();
  h.waitMap();
  const auto [own, enemy] = test::twoStates(h->world());
  const Id hero = availableHero(h->world(), own);
  CHECK(own && enemy && hero);
  Id ring = 0;
  CHECK(h->act("Реликвия", [&](Tx& tx) {
    ring = rules::addRelic(tx, "Кольцо Узника", Rarity::Common);
    rules::giveRelic(tx, hero, ring);
    rules::setRelicCount(tx, ring, rules::RelicPlace{rules::RelicPlace::State, own, 0, own}, 2);
    rules::heroFate(tx, hero, rules::Fate::Captured, enemy, 0);
  }));
  CHECK_EQ(relicCount(h->world().character(hero)->inventory, ring), i64(1));
  asPlayer(h, own);
  openHero(h, hero, "character.info");
  const std::string ret = "character.relicReturn." + std::to_string(ring);
  CHECK(fc::ensureVisible(h, ret));
  CHECK(fc::clickIn(h, ret));
  h.settle();
  CHECK_EQ(relicCount(h->world().character(hero)->inventory, ring), i64(1));
  CHECK_MSG(tipOf(h, ret).find("в плену") != std::string::npos, ui::tooltipText());
  CHECK(fc::ensureVisible(h, "character.addRelic"));
  CHECK_MSG(tipOf(h, "character.addRelic").find("в плену") != std::string::npos, ui::tooltipText());
  shotClean(h, "heroes_rights_captive_relics");
  // Правила тоже отказывают.
  CHECK(!h->play("Выдать", [&](Tx& tx) { rules::giveRelic(tx, hero, ring, rules::RelicPlace{rules::RelicPlace::State, own, 0, own}); }));
  // Отпущенный (администратор) — снова «Вернуть».
  h->ui.role = app::Role::Admin;
  CHECK(h->act("Отпустить", [&](Tx& tx) { rules::releaseCaptive(tx, hero); }));
  asPlayer(h, own);
  openHero(h, hero, "character.info");
  CHECK(fc::clickIn(h, ret));
  h.settle();
  CHECK_EQ(relicCount(h->world().character(hero)->inventory, ring), i64(0));
}

// Игрок: смена подкласса — затем «через N ходов» (поле выключено, сброс тоже); мёртвому герою класс не меняется.
TEST(app_heroes_rights_respec_wait) {
  Harness h("heroes_rights_respec", 1440, 1000);
  h.demo();
  h.waitMap();
  const auto [own, other] = test::twoStates(h->world());
  const Id hero = availableHero(h->world(), own), dead = availableHero(h->world(), own, 1);
  CHECK(own && hero && dead);
  const Id war = classId(h->world(), "Воин");
  const Id knight = subId(h->world(), war, "Рыцарь");
  Id second = 0;
  if (const HeroClass* hc = h->world().heroClass(war))
    for (const SubClass& s : hc->subclasses)
      if (!second && s.id != knight) second = s.id;
  CHECK(war && knight && second);
  CHECK(h->act("Классы", [&](Tx& tx) {
    for (Id x : {hero, dead}) {
      rules::setCharacterClass(tx, x, war);
      rules::setCharacterSubclass(tx, x, knight);
      rules::setHeroLevel(tx, x, 10);
    }
    rules::addModifier(tx, rules::ModTarget::Character, dead, rules::ensureBuiltinMod(tx, schema::mod::Dead), 0);
  }));
  asPlayer(h, own);
  CHECK_EQ(rules::respecWait(h->world(), hero), 0);
  // Первая смена подкласса — можно.
  openHero(h, hero, "character.info");
  CHECK(fc::pickInCombo(h, "character.subclass", h->world().catalogs->subclass(second)->name));
  h.settle();
  CHECK_EQ(h->world().character(hero)->subclass, second);
  CHECK(rules::respecWait(h->world(), hero) > 0);
  // Вторая — нет: поле выключено, в подсказке — срок.
  CHECK(fc::pickInCombo(h, "character.subclass", h->world().catalogs->subclass(knight)->name));
  h.settle();
  CHECK_EQ(h->world().character(hero)->subclass, second);
  CHECK_MSG(tipOf(h, "character.subclass").find("через") != std::string::npos, ui::tooltipText());
  CHECK(!h->play("Подкласс", [&](Tx& tx) { rules::setCharacterSubclass(tx, hero, knight, true); }));
  // Сброс талантов на вкладке — тоже выключен.
  CHECK(h->play("Талант", [&](Tx& tx) {
    for (const Talent& t : h->world().catalogs->subclass(second)->talents)
      if (t.row == 0) {
        rules::learnTalent(tx, hero, t.id);
        break;
      }
  }));
  openHero(h, hero, "character.talents");
  CHECK(fc::clickIn(h, "character.talents.reset"));
  h.settle();
  CHECK(!h->hasDialog("confirm"));
  CHECK(!h->world().character(hero)->talents.empty());
  // Администратору — без срока.
  h->ui.role = app::Role::Admin;
  CHECK(h->act("Подкласс", [&](Tx& tx) { rules::setCharacterSubclass(tx, hero, knight); }));
  // Мёртвый герой: класс игрок не меняет.
  asPlayer(h, own);
  openHero(h, dead, "character.info");
  CHECK_MSG(tipOf(h, "character.class").find("мёртв") != std::string::npos, ui::tooltipText());
  CHECK(!h->play("Класс", [&](Tx& tx) { rules::setCharacterClass(tx, dead, classId(tx.w(), "Паладин"), true); }));
  (void)other;
}

// Пленники государства: «Отпустить» (отношения +5) и «Казнить» с подтверждением (захоронение — столица, −20).
TEST(app_heroes_rights_captives_actions) {
  Harness h("heroes_rights_captives", 1440, 1000);
  h.demo();
  h.waitMap();
  const auto [own, enemy] = test::twoStates(h->world());
  const Id a = availableHero(h->world(), enemy), b = availableHero(h->world(), enemy, 1);
  CHECK(own && enemy && a && b);
  CHECK(h->act("Плен", [&](Tx& tx) {
    rules::heroFate(tx, a, rules::Fate::Captured, own, 0);
    rules::heroFate(tx, b, rules::Fate::Captured, own, 0);
  }));
  asPlayer(h, own);
  fc::openTab(h, own, "faction.heroes");
  CHECK(fc::ensureVisible(h, "captives.release." + std::to_string(a)));
  const double rel0 = h->world().relation(own, enemy).v;
  shotClean(h, "heroes_rights_captives");
  CHECK(fc::clickIn(h, "captives.release." + std::to_string(a)));
  h.settle();
  CHECK(!rules::characterHas(h->world(), a, schema::mod::Captive));
  CHECK_EQ(h->world().character(a)->captor, Id(0));
  CHECK(h->world().relation(own, enemy).v > rel0);
  // Казнь — с подтверждением.
  CHECK(fc::clickIn(h, "captives.execute." + std::to_string(b)));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(rules::characterHas(h->world(), b, schema::mod::Dead));
  CHECK(!rules::characterHas(h->world(), b, schema::mod::Captive));
  CHECK_EQ(h->world().character(b)->burial, h->world().faction(own)->capital);
  CHECK(rules::captivesOf(h->world(), own).empty());
}

// Администратор: «В плен к…» и «Под арест к…» с выбором государства; в общем списке модификаторов героя этих
// состояний нет.
TEST(app_heroes_rights_admin_capture_arrest) {
  Harness h("heroes_rights_admin", 1440, 1000);
  h.demo();
  h.waitMap();
  const auto [own, enemy] = test::twoStates(h->world());
  const Id a = availableHero(h->world(), own), b = availableHero(h->world(), own, 1);
  CHECK(own && enemy && a && b);
  openHero(h, a, "character.info");
  // Список модификаторов: поиск «Взят в плен» и «Под арестом» ничего не находит.
  const size_t mods0 = h->world().character(a)->modifiers.size();
  CHECK(fc::pickInCombo(h, "character.mods", "Взят в плен"));
  h.key(Key::Escape);
  h.settle();
  CHECK(fc::pickInCombo(h, "character.mods", "Под арестом"));
  h.key(Key::Escape);
  h.settle();
  CHECK_EQ(h->world().character(a)->modifiers.size(), mods0);
  CHECK(!rules::characterHas(h->world(), a, schema::mod::Captive));
  // «В плен к…»: государство — пленитель.
  CHECK(fc::pickInCombo(h, "character.toCaptor", h->world().factionName(enemy)));
  h.settle();
  CHECK(rules::characterHas(h->world(), a, schema::mod::Captive));
  CHECK_EQ(h->world().character(a)->captor, enemy);
  CHECK(!rules::captivesOf(h->world(), enemy).empty());
  // «Под арест к…»: тюрьма государства.
  openHero(h, b, "character.info");
  CHECK(fc::pickInCombo(h, "character.toJailer", h->world().factionName(own)));
  h.settle();
  CHECK(rules::characterHas(h->world(), b, schema::mod::Arrested));
  CHECK_EQ(h->world().character(b)->jailer, own);
  shotClean(h, "heroes_rights_admin_arrest");
}
