// Таланты героя и «Возвысить до Лича»: вкладка «Таланты» (карточки подклассов до выбора, ранги, закрытый ярус, снятие
// ранга, сброс, смена подкласса с подтверждением; страница, панель и узкое окно), поля «Класс», «Подкласс» и
// «Уровень», роль игрока (свой герой — изучение, чужой — только просмотр, уровень — только администратору), каталог
// персонажей, окно «Возвысить до Лича»; снимки экранов.
#include "tests/test_app_faction_util.h"
#include "tests/test_app_heroes_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::herotest;

namespace ed = rg::apptest::editors;
namespace fc = rg::factest;

namespace {

void openHero(Harness& h, Id hero, const char* tab) {
  h->ui.tabOf[app::SelType::Character] = tab;
  h->select(app::SelType::Character, hero);
  h.settle();
  h.dropToasts();
}

// Щелчок по ячейке дерева героя (с прокруткой страницы к ней); right — правой кнопкой.
bool clickTalent(Harness& h, Id talent, bool right = false) {
  const std::string m = "character.tree.talent." + sid(talent);
  if (!fc::ensureVisible(h, m)) return false;
  const bool ok = h.clickUi(m, right ? platform::MouseRight : platform::MouseLeft);
  h.settle();
  return ok;
}

}  // namespace

// ---------------------------------------------------------------- вкладка «Таланты» героя
TEST(app_heroes_talents_hero_tree) {
  Harness h("heroes3_hero", 1440, 1000);
  h.demo();
  h.waitMap();
  const Id st = test::stateWithLand(h->world());
  const Id hero = availableHero(h->world(), st);
  CHECK(st && hero);
  const Id war = classId(h->world(), "Воин");
  const Id knight = subId(h->world(), war, "Рыцарь"), merc = subId(h->world(), war, "Наемник");
  const Id t1 = talentId(h->world(), knight, "Тяжелая атлетика"), t2 = talentId(h->world(), knight, "Закалка щитом");
  const Id t3 = talentId(h->world(), knight, "Железный авангард"), ult = talentId(h->world(), knight, "Бастион Чести");
  CHECK(knight && merc && t1 && t2 && t3 && ult);
  CHECK(h->act("Класс", [&](Tx& tx) {
    rules::setCharacterClass(tx, hero, war);
    rules::setHeroLevel(tx, hero, 14);
  }));
  CHECK_EQ(h->world().character(hero)->subclass, Id(0));   // у «Воина» четыре подкласса — сам не выбирается
  // До выбора подкласса — карточки подклассов класса, дерева нет.
  openHero(h, hero, "character.talents");
  CHECK(h->uiRect("character.tree") == nullptr);
  CHECK(h->uiRect("character.pickSub." + sid(knight)) != nullptr);
  CHECK(h->uiRect("character.pickSub." + sid(merc)) != nullptr);
  CHECK(h.shot("heroes3_pick_subclass"));
  CHECK(fc::clickIn(h, "character.pickSub." + sid(knight)));
  h.settle();
  CHECK_EQ(h->world().character(hero)->subclass, knight);
  CHECK(h->uiRect("character.tree") != nullptr);
  // Ранги: три щелчка — «3/5».
  for (int i = 0; i < 3; i++) CHECK(clickTalent(h, t1));
  CHECK_EQ(rankOf(h, hero, t1), 3);
  // Ярус 2 закрыт: вложено 3 из 10.
  h.dropToasts();
  CHECK(clickTalent(h, t3));
  CHECK(hasToast(h, "Ярус 2"));
  CHECK_EQ(rankOf(h, hero, t3), 0);
  // Ярус 1 полностью (10 очков) — ярус 2 открыт.
  for (int i = 0; i < 2; i++) CHECK(clickTalent(h, t1));
  for (int i = 0; i < 5; i++) CHECK(clickTalent(h, t2));
  CHECK(rankOf(h, hero, t1) == 5 && rankOf(h, hero, t2) == 5);
  h.dropToasts();
  CHECK(clickTalent(h, t1));
  CHECK(hasToast(h, "изучен полностью"));
  CHECK(clickTalent(h, t3));
  CHECK_EQ(rankOf(h, hero, t3), 1);
  // Снять ранг яруса 1 нельзя: закроется ярус 2 с изученным талантом.
  h.dropToasts();
  CHECK(clickTalent(h, t1, true));
  CHECK(hasToast(h, "ярус 2"));
  CHECK_EQ(rankOf(h, hero, t1), 5);
  // Снять ранг яруса 2, затем ранг яруса 1.
  CHECK(clickTalent(h, t3, true));
  CHECK_EQ(rankOf(h, hero, t3), 0);
  CHECK(clickTalent(h, t1, true));
  CHECK_EQ(rankOf(h, hero, t1), 4);
  // Финальный ярус закрыт.
  h.dropToasts();
  CHECK(clickTalent(h, ult));
  CHECK(hasToast(h, "Ярус 6"));
  // Снимок: изучен полностью, частично, доступен, закрыт.
  CHECK(h->act("Ранги", [&](Tx& tx) {
    rules::learnTalent(tx, hero, t1);
    rules::learnTalent(tx, hero, t3);
    rules::learnTalent(tx, hero, t3);
  }));
  fc::scrollInspector(h, 20);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("heroes3_talents"));
  // Подсказка ячейки: ранги и причина.
  {
    const RectF* r = h->uiRect("character.tree.talent." + sid(t3));
    CHECK(r != nullptr);
    if (r) {
      h.move(r->cx(), r->cy());
      hl::advance(0.5);
      h.settle();
      CHECK(h.shot("heroes3_talents_tip"));
    }
  }
  // Очки: вложено 12 из 14.
  CHECK_EQ(rules::talentPointsSpent(h->world(), hero), 12);
  // Сброс — с подтверждением.
  CHECK(fc::clickIn(h, "character.talents.reset"));
  CHECK(ed::confirmDialog(h));
  CHECK(h->world().character(hero)->talents.empty());
  // Смена подкласса с изученными талантами — подтверждение сброса.
  CHECK(h->act("Ранг", [&](Tx& tx) { rules::learnTalent(tx, hero, t1); }));
  h.settle();
  CHECK(fc::clickIn(h, "character.talents.swap"));
  CHECK(h.clickUi("character.talents.swap." + sid(merc)));
  CHECK(ed::confirmDialog(h));
  CHECK(h->world().character(hero)->subclass == merc && h->world().character(hero)->talents.empty());
}

// Дерево героя в панели выделения (только значки) и на странице узкого окна; полностью изученное дерево.
TEST(app_heroes_talents_hero_narrow) {
  Harness h("heroes3_hero_narrow", 1024, 760);
  h.demo();
  h.waitMap();
  const Id st = test::stateWithLand(h->world());
  const Id hero = availableHero(h->world(), st);
  const Id war = classId(h->world(), "Воин");
  const Id knight = subId(h->world(), war, "Рыцарь");
  CHECK(h->act("Класс", [&](Tx& tx) {
    rules::setCharacterClass(tx, hero, war);
    rules::setCharacterSubclass(tx, hero, knight);
    rules::setHeroLevel(tx, hero, 55);
    // Ярусы 1–5 полностью и первый финальный талант.
    const SubClass& s = *tx.w().catalogs->subclass(knight);
    for (int row = 0; row < 5; row++)
      for (const Talent& t : s.talents)
        if (t.row == row)
          for (int k = 0; k < t.ranks; k++) rules::learnTalent(tx, hero, t.id);
    for (const Talent& t : s.talents)
      if (t.row == 5 && t.col == 1) rules::learnTalent(tx, hero, t.id);
  }));
  CHECK_EQ(rules::talentPointsSpent(h->world(), hero), 55);
  openHero(h, hero, "character.talents");
  const RectF* tree = h->uiRect("character.tree");
  CHECK(tree != nullptr);
  CHECK(inside(h->uiRect("character.tree.cell.0.4"), tree));
  const RectF* insp = h->uiRect("inspector");
  CHECK(tree && insp && tree->right() <= insp->right());
  fc::scrollInspector(h, -6);
  CHECK(h.shot("heroes3_talents_page_narrow"));
  // Панель справа от карты: доска только со значками, пять столбцов помещаются.
  h->setPage(false);
  h.settle();
  const RectF* tree2 = h->uiRect("character.tree");
  const RectF* panel = h->uiRect("inspector");
  CHECK(tree2 && panel && tree2->right() <= panel->right() && tree2->x >= panel->x);
  CHECK(inside(h->uiRect("character.tree.cell.5.4"), tree2));
  fc::scrollInspector(h, -10);
  h.waitMap();
  CHECK(h.shot("heroes3_talents_panel"));
  // Самая узкая панель: доска всё ещё помещается.
  h->ui.inspectorWidth = 320;
  h.settle();
  const RectF* tree3 = h->uiRect("character.tree");
  const RectF* panel3 = h->uiRect("inspector");
  CHECK(tree3 && panel3 && tree3->right() <= panel3->right() && tree3->x >= panel3->x);
  CHECK(inside(h->uiRect("character.tree.cell.0.4"), tree3));
  CHECK(inside(h->uiRect("character.tree.cell.0.0"), tree3));
}

// ---------------------------------------------------------------- «Сведения»: класс, подкласс, уровень
TEST(app_heroes_talents_info_fields) {
  Harness h("heroes3_info", 1440, 1000);
  h.demo();
  h.waitMap();
  const Id st = test::stateWithLand(h->world());
  const Id hero = availableHero(h->world(), st);
  const Id war = classId(h->world(), "Воин"), mage = classId(h->world(), "Маг");
  const Id knight = subId(h->world(), war, "Рыцарь"), raver = subId(h->world(), war, "Опустошитель");
  const Id t1 = talentId(h->world(), knight, "Тяжелая атлетика");
  openHero(h, hero, "character.info");
  CHECK(fc::pickInCombo(h, "character.class", "Воин"));
  CHECK_EQ(h->world().character(hero)->heroClass, war);
  CHECK(fc::pickInCombo(h, "character.subclass", "Рыцарь"));
  CHECK_EQ(h->world().character(hero)->subclass, knight);
  // Уровень и изученные таланты; уровень ниже вложенных очков — подтверждение сброса.
  CHECK(fc::typeNumber(h, "character.level", "12"));
  CHECK_EQ(h->world().character(hero)->level, 12);
  CHECK(h->act("Таланты", [&](Tx& tx) {
    for (int k = 0; k < 4; k++) rules::learnTalent(tx, hero, t1);
  }));
  CHECK(fc::typeNumber(h, "character.level", "2"));
  CHECK(h->hasDialog("confirm"));
  CHECK(ed::confirmDialog(h));
  CHECK(h->world().character(hero)->level == 2 && h->world().character(hero)->talents.empty());
  // Смена подкласса с талантами — подтверждение.
  CHECK(h->act("Таланты", [&](Tx& tx) { rules::learnTalent(tx, hero, t1); }));
  CHECK(fc::pickInCombo(h, "character.subclass", "Опустошитель"));
  CHECK(ed::confirmDialog(h));
  CHECK(h->world().character(hero)->subclass == raver && h->world().character(hero)->talents.empty());
  // Смена класса — подкласс снят (у «Мага» несколько подклассов).
  CHECK(fc::pickInCombo(h, "character.class", "Маг"));
  CHECK(h->world().character(hero)->heroClass == mage);
  CHECK_EQ(h->world().character(hero)->subclass, Id(0));
  CHECK(fc::ensureVisible(h, "character.level"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("heroes3_info"));
}

// ---------------------------------------------------------------- роль игрока
TEST(app_heroes_talents_player_role) {
  Harness h("heroes3_player", 1440, 1000);
  h.demo();
  h.waitMap();
  const auto [own, foreign] = test::twoStates(h->world());
  const Id mine = availableHero(h->world(), own), theirs = availableHero(h->world(), foreign);
  CHECK(mine && theirs);
  const Id war = classId(h->world(), "Воин"), pal = classId(h->world(), "Палладин");
  const Id knight = subId(h->world(), war, "Рыцарь");
  const Id t1 = talentId(h->world(), knight, "Тяжелая атлетика");
  CHECK(h->act("Классы", [&](Tx& tx) {
    for (Id x : {mine, theirs}) {
      rules::setCharacterClass(tx, x, war);
      rules::setCharacterSubclass(tx, x, knight);
      rules::setHeroLevel(tx, x, 10);
    }
  }));
  h->ui.role = app::Role::Player;
  h->ui.playerState = own;
  // Свой герой: изучение и снятие рангов.
  openHero(h, mine, "character.talents");
  CHECK(clickTalent(h, t1));
  CHECK(clickTalent(h, t1));
  CHECK_EQ(rankOf(h, mine, t1), 2);
  CHECK(clickTalent(h, t1, true));
  CHECK_EQ(rankOf(h, mine, t1), 1);
  CHECK(h->uiRect("character.talents.reset") != nullptr);
  CHECK(h->uiRect("character.talents.catalog") == nullptr);   // справочник — не игроку
  // Чужой герой: только просмотр.
  openHero(h, theirs, "character.talents");
  CHECK(h->uiRect("character.tree") != nullptr);
  CHECK(clickTalent(h, t1));
  CHECK_EQ(rankOf(h, theirs, t1), 0);
  CHECK(h->uiRect("character.talents.reset") == nullptr);
  CHECK(h->uiRect("character.talents.swap") == nullptr);
  h.dropToasts();
  CHECK(h.shot("heroes3_player_foreign"));
  // Чужой герой без подкласса: карточки подклассов не выбирают. Правка мира игроку недоступна — класс ставит администратор.
  CHECK(!h->act("Класс", [&](Tx& tx) { rules::setCharacterClass(tx, theirs, pal); }));
  h->ui.role = app::Role::Admin;
  CHECK(h->act("Класс", [&](Tx& tx) { rules::setCharacterClass(tx, theirs, pal); }));
  h->ui.role = app::Role::Player;
  h.settle();
  const Id keeper = subId(h->world(), pal, "Хранитель");
  CHECK(fc::clickIn(h, "character.pickSub." + sid(keeper)));
  h.settle();
  CHECK_EQ(h->world().character(theirs)->subclass, Id(0));
  // Свой герой: выбор подкласса из карточек и класса — игровые действия.
  h->ui.role = app::Role::Admin;
  CHECK(h->act("Класс", [&](Tx& tx) { rules::setCharacterClass(tx, mine, pal); }));
  h->ui.role = app::Role::Player;
  openHero(h, mine, "character.talents");
  CHECK(fc::clickIn(h, "character.pickSub." + sid(keeper)));
  h.settle();
  CHECK_EQ(h->world().character(mine)->subclass, keeper);
  // Уровень — только администратору: «+» у поля не меняет уровень.
  openHero(h, mine, "character.info");
  CHECK(fc::ensureVisible(h, "character.level"));
  CHECK(clickAt(h, "character.level", 0.96f));
  CHECK_EQ(h->world().character(mine)->level, 10);
  h->ui.role = app::Role::Admin;
  h.settle();
  CHECK(fc::ensureVisible(h, "character.level"));
  CHECK(clickAt(h, "character.level", 0.96f));
  CHECK_EQ(h->world().character(mine)->level, 11);
}

// ---------------------------------------------------------------- каталог персонажей: класс, подкласс, уровень
TEST(app_heroes_talents_directory) {
  Harness h("heroes3_directory", 1440, 1000);
  h.demo();
  h.waitMap();
  CHECK(h->act("Классы", [&](Tx& tx) {
    const char* names[] = {"Воин", "Маг", "Жрец", "Ассасин"};
    int k = 0;
    const Id olsta = findFaction(tx.w(), "Вольные города Ольсты");
    for (Id cid : tx.w().characters.ids()) {
      if (tx.w().character(cid)->faction != olsta || k >= 4) continue;
      const Id cls = test::idByName(tx.w().catalogs->classes, names[k]);
      rules::setCharacterClass(tx, cid, cls);
      if (k < 3) rules::setCharacterSubclass(tx, cid, tx.w().heroClass(cls)->subclasses[size_t(k)].id);
      rules::setHeroLevel(tx, cid, 5 + 11 * k);
      k++;
    }
  }));
  h->openDirectory("characters");
  h.settle();
  h.dropToasts();
  Id withSub = 0;
  h->world().characters.each([&](const Character& c) {
    if (!withSub && c.subclass) withSub = c.id;
  });
  CHECK(withSub != 0);
  if (!fc::ensureVisible(h, "characters.class." + sid(withSub))) {
    // Каталог — своя прокрутка: строка может быть ниже видимой части.
    for (int i = 0; i < 20 && !h->uiRect("characters.class." + sid(withSub)); i++) {
      const RectF* d = h->uiRect("drawer");
      if (!d) break;
      h.wheel(d->cx(), d->cy(), -3);
      h.frames(8);
    }
  }
  CHECK(h->uiRect("characters.class." + sid(withSub)) != nullptr);
  CHECK(h.shot("heroes3_directory"));
}

// ---------------------------------------------------------------- «Возвысить до Лича»
TEST(app_heroes_lich) {
  Harness h("heroes2_lich", 1440, 1000);
  h.demo();
  h.waitMap();
  const Id st = test::stateWithLand(h->world());
  const Id hero = availableHero(h->world(), st), other = availableHero(h->world(), st, 1);
  CHECK(st && hero && other);
  Id skull = 0, ring = 0, heart = 0;
  CHECK(h->act("Некромант", [&](Tx& tx) {
    skull = rules::addRelic(tx, "Чёрный череп", Rarity::Epic);
    ring = rules::addRelic(tx, "Медный перстень", Rarity::Rare);
    heart = rules::addRelic(tx, "Сердце зимы", Rarity::Legendary);
    rules::setRelicImage(tx, heart, test::gemPng(90, 170, 255));
    rules::setRelicImage(tx, skull, test::gemPng(170, 90, 230));
    rules::giveRelic(tx, hero, ring);
    rules::giveRelic(tx, hero, skull);
    rules::giveRelic(tx, hero, heart);
    rules::addModifier(tx, rules::ModTarget::Character, hero, rules::ensureBuiltinMod(tx, schema::mod::Necromancer));
    Faction& f = tx.faction(st);
    f.res[rules::ensureResource(tx, schema::kResCorpses)] = 30000;
    f.ess[rules::deathEssence(tx.w())] = 6000;
  }));
  openHero(h, other, "character.info");
  CHECK(h->uiRect("character.lich") == nullptr);   // без «Некроманта» кнопки нет
  openHero(h, hero, "character.info");
  // Игрок другого государства: окно не открывается (игровое действие — только за своё государство).
  {
    const auto [s1, s2] = test::twoStates(h->world());
    h->ui.role = app::Role::Player;
    h->ui.playerState = s1 == st ? s2 : s1;
    h.settle();
    CHECK(fc::clickIn(h, "character.lich"));
    h.settle();
    CHECK(!h->hasDialog("hero.lich"));
    h->ui.role = app::Role::Admin;
    h->ui.playerState = 0;
    h.settle();
  }
  // Инвентарь: строки реликвий по главенству редкости, значки с изображением.
  CHECK(fc::ensureVisible(h, "character.relic." + std::to_string(ring)));
  h.settle();
  CHECK(h.shot("heroes2_inventory"));
  CHECK(fc::clickIn(h, "character.lich"));
  h.settle();
  CHECK(h->hasDialog("hero.lich"));
  CHECK(h.clickUi("lich.relic." + std::to_string(heart)));
  h.settle();
  CHECK(h.shot("heroes2_lich_dialog"));
  CHECK(h.clickUi("lich.ok"));
  h.settle();
  CHECK(!h->hasDialog("hero.lich"));
  CHECK(rules::characterHas(h->world(), hero, schema::mod::Lich));
  CHECK(!rules::characterHas(h->world(), hero, schema::mod::Necromancer));
  CHECK_EQ(h->world().relic(heart)->name, "Сердце зимы (Филактерия «" + h->world().character(hero)->name + "»)");
  CHECK_NEAR(h->world().faction(st)->stock(rules::resourceId(h->world(), schema::kResCorpses)), 10000, 1e-9);
  h.settle();
  CHECK(h->uiRect("character.lich") == nullptr);
}
