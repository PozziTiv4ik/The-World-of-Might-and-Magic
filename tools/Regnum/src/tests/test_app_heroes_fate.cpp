// Сценарии героев (ТЗ «Механика героев», «Модификаторы», «Фиксы» п.9, «Виды государств» п.1) на демонстрационном мире:
// пленники государства и состояние «В плену», погибший герой с местом захоронения и воскрешение (способ и
// государство), модификаторы героя в инспекторе (встроенный шаблон, срок, взаимоисключение природы), ограничения
// назначений (только доступные герои своего государства), вид государства и природа новых героев.
#include "app/widgets.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

namespace {

Id characterByName(const World& w, std::string_view name) {
  Id r = 0;
  w.characters.each([&](const Character& c) {
    if (!r && c.name == name) r = c.id;
  });
  return r;
}

Id ownedProvince(const World& w, Id state) {
  Id r = 0;
  w.provinces.each([&](const Province& p) {
    if (!r && !p.sea && p.owner == state) r = p.id;
  });
  return r;
}

void shotClean(Harness& h, const std::string& name) {
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot(name));
}

// Щелчок по доле прямоугольника (сегменты переключателя).
bool clickAt(Harness& h, const std::string& name, float fx) {
  const RectF* r = h->uiRect(name);
  if (!r) return false;
  RectF rr = *r;
  h.click(rr.x + rr.w * fx, rr.cy());
  return true;
}

}  // namespace

// ---------------------------------------------------------------- пленники, погибший, воскрешение
TEST(app_heroes_captives_and_resurrect) {
  HideTestRegs regs;
  Harness h("heroes_fate");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id hel = findFaction(h->world(), "Северный союз Хельдвиг");
  const Id vk = findFaction(h->world(), "Империя Валь-Кетра");
  const Id bjorn = characterByName(h->world(), "Бьорн Медведь");
  const Id ulrika = characterByName(h->world(), "Ульрика Ледяная");
  const Id grave = ownedProvince(h->world(), hel);
  CHECK(hel && vk && bjorn && ulrika && grave);
  CHECK(rules::characterHas(h->world(), ulrika, schema::mod::Living));
  CHECK(h->act("Судьба героев", [&](Tx& tx) {
    rules::heroFate(tx, bjorn, rules::Fate::Captured, vk, 0);
    rules::heroFate(tx, ulrika, rules::Fate::Killed, 0, grave);
  }));
  // Пленник снят со всех назначений (был лордом провинции).
  h->world().provinces.each([&](const Province& p) { CHECK(p.lord != bjorn && p.lord != ulrika); });
  CHECK_EQ(rules::captivesOf(h->world(), vk).size(), size_t(1));

  // Пленники Валь-Кетры: имя и чей герой.
  openTab(h, vk, "faction.heroes");
  CHECK(ensureVisible(h, "captives.row." + std::to_string(bjorn)));
  shotClean(h, "heroes_captives");

  // Свои герои Хельдвига: состояния и «Воскресить» у погибшего.
  openTab(h, hel, "faction.heroes");
  CHECK(ensureVisible(h, "heroes.resurrect." + std::to_string(ulrika)));
  shotClean(h, "heroes_dead_and_captive");
  CHECK(h.clickUi("heroes.resurrect." + std::to_string(ulrika)));
  h.settle();
  CHECK(h->hasDialog("hero.resurrect"));
  shotClean(h, "heroes_resurrect_dialog");
  // Способ «Как нежить» (второй из четырёх), государство — Валь-Кетра (второе по алфавиту).
  CHECK(clickAt(h, "resurrect.way", 0.375f));
  CHECK(h.clickUi("resurrect.state"));
  h.key(Key::PageUp);
  h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK(h.clickUi("resurrect.ok"));
  h.settle();
  CHECK(!h->hasDialog("hero.resurrect"));
  {
    const World& w = h->world();
    const Character* u = w.character(ulrika);
    CHECK(u != nullptr);
    CHECK(!rules::characterHas(w, ulrika, schema::mod::Dead));
    CHECK(rules::characterHas(w, ulrika, schema::mod::Undead));
    CHECK(!rules::characterHas(w, ulrika, schema::mod::Living));
    CHECK(u && u->faction == vk);
    CHECK(u && u->burial == 0);
    CHECK(rules::heroAvailable(w, ulrika));
  }
  h->undo();
  h.step();
  CHECK(rules::characterHas(h->world(), ulrika, schema::mod::Dead));
  CHECK_EQ(h->world().character(ulrika)->faction, hel);
  CHECK_EQ(h->world().character(ulrika)->burial, grave);

  // Инспектор пленника: состояние в шапке, пленившее государство.
  h->ui.tabOf[app::SelType::Character] = "character.info";
  h->select(app::SelType::Character, bjorn);
  h.settle();
  CHECK(h->uiRect("character.captive") != nullptr);
  shotClean(h, "heroes_captive_inspector");
  // Погибший: место захоронения и «Воскресить» в инспекторе.
  h->select(app::SelType::Character, ulrika);
  h.settle();
  CHECK(h->uiRect("character.dead") != nullptr);
  CHECK(h->uiRect("character.burialChip") != nullptr);
  CHECK(clickIn(h, "character.resurrect"));
  h.settle();
  CHECK(h->hasDialog("hero.resurrect"));
  CHECK(h.clickUi("resurrect.cancel"));
  h.settle();
  CHECK(rules::characterHas(h->world(), ulrika, schema::mod::Dead));
}

// ---------------------------------------------------------------- модификаторы героя в инспекторе
TEST(app_heroes_modifiers_inspector) {
  HideTestRegs regs;
  Harness h("heroes_mods", 1440, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id hero = characterByName(h->world(), "Сэр Гарет Красный");
  CHECK(hero != 0);
  CHECK(rules::characterHas(h->world(), hero, schema::mod::Living));
  CHECK(!rules::builtinModId(h->world(), schema::mod::Loyalist));   // записи ещё нет — предлагается шаблон
  h->ui.tabOf[app::SelType::Character] = "character.info";
  h->select(app::SelType::Character, hero);
  h.settle();
  // Встроенный «Непреклонный лоялист»: запись мира создаётся вместе с назначением (одно действие).
  CHECK(pickInCombo(h, "character.mods", "Непреклонный"));
  CHECK(rules::characterHas(h->world(), hero, schema::mod::Loyalist));
  CHECK(rules::builtinModId(h->world(), schema::mod::Loyalist) != 0);
  // Природа одна: «Механизм» заменяет «Живой».
  CHECK(pickInCombo(h, "character.mods", "Механизм"));
  CHECK(rules::characterHas(h->world(), hero, schema::mod::Mechanism));
  CHECK(!rules::characterHas(h->world(), hero, schema::mod::Living));
  // «Недовольство правителем» вместе с лоялистом — отказ правил, список прежний.
  const size_t n0 = h->world().character(hero)->modifiers.size();
  h.dropToasts();
  CHECK(pickInCombo(h, "character.mods", "Недовольство"));
  CHECK_EQ(h->world().character(hero)->modifiers.size(), n0);
  CHECK(!rules::characterHas(h->world(), hero, schema::mod::Discontent));
  // Срок модификатора: щелчок по фишке — поле срока.
  const Id loyal = rules::builtinModId(h->world(), schema::mod::Loyalist);
  size_t idx = 0;
  for (size_t i = 0; i < h->world().character(hero)->modifiers.size(); i++)
    if (h->world().character(hero)->modifiers[i] == loyal) idx = i;
  h.dropToasts();
  CHECK(clickIn(h, "hero.mods.chip." + std::to_string(idx)));
  h.settle();
  CHECK(h.clickUi("hero.mods.term"));
  h.retype("3");
  h.key(Key::Enter);
  h.settle();
  {
    const Character* c = h->world().character(hero);
    auto it = c->modTurns.find(loyal);
    CHECK(it != c->modTurns.end() && it->second == 3);
  }
  h.key(Key::Escape);
  h.settle();
  shotClean(h, "heroes_modifiers_inspector");
  // Снять крестиком.
  {
    const RectF* r = h->uiRect("hero.mods.chip." + std::to_string(idx));
    CHECK(r != nullptr);
    if (r) {
      RectF rr = *r;
      h.click(rr.right() - 14, rr.cy());
    }
  }
  CHECK(!rules::characterHas(h->world(), hero, schema::mod::Loyalist));
  CHECK(h->world().character(hero)->modTurns.count(loyal) == 0);
  // «Мертв», поставленный вручную, снимает со всех назначений (герой был лордом провинции).
  bool lord = false;
  h->world().provinces.each([&](const Province& p) { lord = lord || p.lord == hero; });
  CHECK(lord);
  CHECK(pickInCombo(h, "character.mods", "Мертв"));
  CHECK(rules::characterHas(h->world(), hero, schema::mod::Dead));
  h->world().provinces.each([&](const Province& p) { CHECK(p.lord != hero); });
  CHECK(h->uiRect("character.dead") != nullptr);
}

// ---------------------------------------------------------------- назначения: только доступные герои своего государства
TEST(app_heroes_assignment_limits) {
  HideTestRegs regs;
  Harness h("heroes_assign");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id hel = findFaction(h->world(), "Северный союз Хельдвиг");
  const Id ulrika = characterByName(h->world(), "Ульрика Ледяная");
  const Id bjorn = characterByName(h->world(), "Бьорн Медведь");
  const Id foreign = characterByName(h->world(), "Легат Максен");
  const Id halvar = characterByName(h->world(), "Хальвар Мудрый");
  CHECK(hel && ulrika && bjorn && foreign && halvar);
  CHECK(h->act("Погиб", [&](Tx& tx) { rules::heroFate(tx, ulrika, rules::Fate::Killed, 0, 0); }));
  const Id seat = h->world().faction(hel)->council[0].id;
  CHECK_EQ(h->world().faction(hel)->council[0].character, halvar);
  // Правила: мёртвого и чужого назначить нельзя.
  CHECK(!h->act("Совет", [&](Tx& tx) { rules::setCouncilMember(tx, hel, seat, ulrika); }));
  CHECK(!h->act("Совет", [&](Tx& tx) { rules::setCouncilMember(tx, hel, seat, foreign); }));
  CHECK(!h->act("Правитель", [&](Tx& tx) { rules::setRuler(tx, hel, foreign); }));
  h.dropToasts();
  // Выбор советника: «Вакантно», затем доступные герои Хельдвига по алфавиту (Бьорн, Ингвар, Сигрун, Торстейн,
  // Хальвар — без погибшей Ульрики и чужих), затем «Новый персонаж».
  openTab(h, hel, "faction.council");
  CHECK(clickIn(h, "council.who.0"));
  h.settle();
  shotClean(h, "heroes_council_picker");
  h.key(Key::PageUp);
  for (int i = 0; i < 5; i++) h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(hel)->council[0].character, halvar);   // пятый — сам Хальвар: Ульрики в списке нет
  CHECK(clickIn(h, "council.who.0"));
  h.key(Key::PageUp);
  h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(hel)->council[0].character, bjorn);
  // Шестой пункт — «Новый персонаж» этого государства: создаётся и назначается.
  const u32 n0 = h->world().characters.size();
  CHECK(clickIn(h, "council.who.0"));
  h.key(Key::PageUp);
  for (int i = 0; i < 6; i++) h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().characters.size(), n0 + 1);
  const Character* nc = h->world().character(h->world().faction(hel)->council[0].character);
  CHECK(nc && nc->faction == hel);
  // Роли погибшей в инспекторе недоступны.
  h->ui.tabOf[app::SelType::Character] = "character.roles";
  h->select(app::SelType::Character, ulrika);
  h.settle();
  CHECK(h->uiRect("character.makeRuler") != nullptr);
  const Id ruler0 = h->world().faction(hel)->ruler;
  CHECK(clickIn(h, "character.makeRuler"));
  h.settle();
  if (h->hasDialog("confirm")) CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(hel)->ruler, ruler0);
  shotClean(h, "heroes_dead_roles");
}

// ---------------------------------------------------------------- вид государства и природа новых героев (ТЗ «Виды государств», п.1)
TEST(app_heroes_state_kind) {
  HideTestRegs regs;
  Harness h("heroes_state_kind");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id st = findFaction(h->world(), "Орда Таргаш");
  CHECK(st != 0);
  CHECK(h->world().faction(st)->stateKind == StateKind::Living);
  openTab(h, st, "faction.overview");
  // Вид: «Государство нежити» — второй из трёх.
  CHECK(clickIn(h, "overview.kind"));
  h.key(Key::PageUp);
  h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK(h->world().faction(st)->stateKind == StateKind::Undead);
  // Основное игровое государство — галочка; в шапке — звезда.
  CHECK(!h->world().faction(st)->mainState);
  CHECK(clickIn(h, "overview.main"));
  CHECK(h->world().faction(st)->mainState);
  h.settle();
  CHECK(h->uiRect("faction.main") != nullptr);
  shotClean(h, "heroes_state_kind_overview");
  // Новый герой государства нежити — с природой «Нежить».
  openTab(h, st, "faction.heroes");
  const u32 n0 = h->world().characters.size();
  CHECK(clickIn(h, "heroes.new"));
  h.step();
  CHECK_EQ(h->world().characters.size(), n0 + 1);
  Id fresh = 0;
  h->world().characters.each([&](const Character& c) {
    if (c.faction == st && c.name == "Новый герой") fresh = c.id;
  });
  CHECK(fresh != 0);
  CHECK(rules::characterHas(h->world(), fresh, schema::mod::Undead));
  CHECK(!rules::characterHas(h->world(), fresh, schema::mod::Living));
  // Гильдия: устройства, религии и вида государства нет (ТЗ «Фиксы», п.14).
  const Id g = findFaction(h->world(), "Янтарная лига");
  CHECK(g != 0);
  openTab(h, g, "faction.overview");
  CHECK(h->uiRect("overview.culture") != nullptr);
  CHECK(h->uiRect("overview.gov") == nullptr);
  CHECK(h->uiRect("overview.religion") == nullptr);
  CHECK(h->uiRect("overview.kind") == nullptr);
  CHECK(h->uiRect("overview.main") == nullptr);
  shotClean(h, "heroes_guild_overview");
}
