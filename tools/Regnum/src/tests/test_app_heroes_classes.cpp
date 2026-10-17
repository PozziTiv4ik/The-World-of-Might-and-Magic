// Справочник «Классы героев»: подклассы класса, финальный ярус, ранги и очки за ранг, финальный талант, новый подкласс
// и его дерево (новый талант щелчком по пустой ячейке, условие-стрелка, перенос и отказ переноса, ломающего условие,
// очки на ярус, описание, кто изучил), удаление таланта и подкласса с подтверждением; окно поуже и узкое окно.
#include "tests/test_app_heroes_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::herotest;

namespace ed = rg::apptest::editors;
namespace tt = rg::app::talents;

// ---------------------------------------------------------------- справочник «Классы героев»
TEST(app_heroes_classes_editor) {
  Harness h("heroes3_catalog", 1440, 900);
  h.demo();
  h.dropToasts();
  h->openEditor("catalogs", 10);
  ed::quick(h);
  CHECK(h->uiRect("catalogs.view.classes") != nullptr);
  CHECK_EQ(h->world().catalogs->classes.size(), size_t(20));
  const Id war = classId(h->world(), "Воин");
  CHECK(war != 0);
  CHECK(h.clickUi("catalogs.class." + sid(war)));
  ed::quick(h);
  // Подклассы класса — фишками; выбран первый, его дерево на доске.
  const HeroClass* wc = h->world().heroClass(war);
  CHECK_EQ(wc->subclasses.size(), size_t(4));
  for (const SubClass& s : wc->subclasses) CHECK_MSG(h->uiRect("catalogs.subclass." + sid(s.id)) != nullptr, s.name);
  const Id merc = subId(h->world(), war, "Наемник");
  CHECK(h.clickUi("catalogs.subclass." + sid(merc)));
  ed::quick(h);
  // Финальный ярус: два ультимейта по 5 очков; пороги ярусов 0…50.
  const Id flurry = talentId(h->world(), merc, "Шквал Клинков");
  CHECK(h->uiRect("catalogs.tree.talent." + sid(flurry)) != nullptr);
  CHECK(h->uiRect("catalogs.tree.tier.5") != nullptr);
  CHECK(tt::isUltimate(*talentOf(h->world(), flurry)));
  CHECK(tt::ultimateTier(*subOf(h->world(), merc), 5));
  CHECK(!tt::ultimateTier(*subOf(h->world(), merc), 4));
  CHECK_EQ(rules::tierThreshold(*subOf(h->world(), merc), 5), 50);
  CHECK_EQ(tt::ranksText(*talentOf(h->world(), flurry)), std::string("5 очков"));
  // Карточка таланта: ранги и очки за ранг, итог «до N очков».
  const Id fingers = talentId(h->world(), merc, "Ловкие пальцы");
  CHECK_EQ(talentOf(h->world(), fingers)->ranks, 2);
  CHECK(h.clickUi("catalogs.tree.talent." + sid(fingers)));
  ed::quick(h);
  CHECK(h->uiRect("catalogs.talent.ranks") != nullptr);
  CHECK(ed::enterValue(h, "catalogs.talent.ranks", "3", "catalogs.classCard"));
  CHECK_EQ(talentOf(h->world(), fingers)->ranks, 3);
  CHECK(ed::enterValue(h, "catalogs.talent.cost", "2", "catalogs.classCard"));
  CHECK_EQ(talentOf(h->world(), fingers)->cost, 2);
  CHECK_EQ(talentOf(h->world(), fingers)->maxPoints(), 6);
  CHECK_EQ(tt::ranksText(*talentOf(h->world(), fingers)), std::string("1–3 × 2"));
  shotClean(h, "heroes3_catalog_talent");
  // Финальный талант переключателем: один ранг за 5 очков; снять — обычный в 1 очко.
  CHECK(ed::clickRevealed(h, "catalogs.talent.ultimate", "catalogs.classCard"));
  CHECK(tt::isUltimate(*talentOf(h->world(), fingers)));
  CHECK(ed::clickRevealed(h, "catalogs.talent.ultimate", "catalogs.classCard"));
  CHECK(!tt::isUltimate(*talentOf(h->world(), fingers)));
  CHECK(talentOf(h->world(), fingers)->ranks == 1 && talentOf(h->world(), fingers)->cost == 1);

  // Новый подкласс: название сразу в правке, дерево пустое.
  CHECK(h.clickUi("catalogs.subclass.add"));
  ed::quick(h);
  h.retype("Дуэлянт");
  h.key(Key::Enter);
  ed::quick(h);
  const Id duel = subId(h->world(), war, "Дуэлянт");
  CHECK(duel != 0);
  CHECK_EQ(h->world().heroClass(war)->subclasses.size(), size_t(5));
  CHECK(subOf(h->world(), duel)->talents.empty());
  CHECK(h->uiRect("catalogs.tree.cell.2.4") != nullptr);
  // Пустая ячейка — новый талант с названием в правке.
  CHECK(h.clickUi("catalogs.tree.cell.0.2"));
  ed::quick(h);
  CHECK_EQ(subOf(h->world(), duel)->talents.size(), size_t(1));
  const Id lunge = subOf(h->world(), duel)->talents[0].id;
  h.retype("Выпад");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK_EQ(talentOf(h->world(), lunge)->name, std::string("Выпад"));
  CHECK(ed::enterValue(h, "catalogs.talent.ranks", "5", "catalogs.classCard"));
  // Ярус 2: «Ответный удар» с условием «Выпад» (стрелка).
  CHECK(h.clickUi("catalogs.tree.cell.1.2"));
  ed::quick(h);
  const Id riposte = talentId(h->world(), duel, "Новый талант");
  CHECK(riposte != 0);
  h.retype("Ответный удар");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK(ed::pickInCombo(h, "catalogs.talent.prereq", "Выпад", "catalogs.classCard"));
  CHECK_EQ(talentOf(h->world(), riposte)->prereq, lunge);
  // Ярус 3: финальный талант «Смертельный танец».
  CHECK(h.clickUi("catalogs.tree.cell.2.2"));
  ed::quick(h);
  const Id dance = talentId(h->world(), duel, "Новый талант");
  h.retype("Смертельный танец");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK(ed::clickRevealed(h, "catalogs.talent.ultimate", "catalogs.classCard"));
  CHECK(tt::isUltimate(*talentOf(h->world(), dance)));
  CHECK(tt::ultimateTier(*subOf(h->world(), duel), 2));
  // Перенос «Ответного удара» в соседнюю ячейку яруса.
  CHECK(dragUi(h, "catalogs.tree.talent." + sid(riposte), "catalogs.tree.cell.1.0"));
  CHECK(talentOf(h->world(), riposte)->row == 1 && talentOf(h->world(), riposte)->col == 0);
  // Условие ниже зависимого — отказ с причиной.
  h.dropToasts();
  CHECK(dragUi(h, "catalogs.tree.talent." + sid(lunge), "catalogs.tree.cell.3.1"));
  CHECK_EQ(talentOf(h->world(), lunge)->row, 0);
  CHECK(hasToast(h, "должно стоять выше"));
  // Очков на ярус.
  CHECK(ed::enterValue(h, "catalogs.subCard.tier", "4"));
  CHECK_EQ(subOf(h->world(), duel)->tierPoints, 4);
  // Герой подкласса изучил «Выпад» (2 ранга).
  const Id st = test::stateWithLand(h->world());
  const Id hero = availableHero(h->world(), st);
  CHECK(h->act("Герой", [&](Tx& tx) {
    rules::setCharacterClass(tx, hero, war);
    rules::setCharacterSubclass(tx, hero, duel);
    rules::setHeroLevel(tx, hero, 8);
    rules::learnTalent(tx, hero, lunge);
    rules::learnTalent(tx, hero, lunge);
  }));
  ed::quick(h);
  // Карточка таланта: кто изучил и на каком ранге.
  CHECK(h.clickUi("catalogs.tree.talent." + sid(lunge)));
  ed::quick(h);
  CHECK(h->uiRect("catalogs.talent.learned") != nullptr);
  shotClean(h, "heroes3_catalog_new");
  // Карточка подкласса (талант не выбран): описание, герои подкласса.
  CHECK(h.clickUi("catalogs.talent.close"));
  ed::quick(h);
  CHECK(h->uiRect("catalogs.subCard.desc") != nullptr);
  CHECK(ed::clickRevealed(h, "catalogs.subCard.desc", "catalogs.classCard"));
  h.type("Мастер парных клинков");
  h.key(Key::Tab);
  ed::quick(h);
  CHECK_EQ(subOf(h->world(), duel)->desc, std::string("Мастер парных клинков"));
  CHECK(h->uiRect("catalogs.subCard.heroes") != nullptr);
  shotClean(h, "heroes3_catalog_subclass");
  // Удаление таланта: подтверждение; у героя он снят, у зависимого снято условие.
  CHECK(h.clickUi("catalogs.tree.talent." + sid(lunge)));
  ed::quick(h);
  CHECK(ed::clickRevealed(h, "catalogs.talent.delete", "catalogs.classCard"));
  CHECK(ed::confirmDialog(h));
  CHECK(talentOf(h->world(), lunge) == nullptr);
  CHECK_EQ(talentOf(h->world(), riposte)->prereq, Id(0));
  CHECK(h->world().character(hero)->talents.empty());
  h->undo();
  ed::quick(h);
  CHECK_EQ(rankOf(h, hero, lunge), 2);
  // Удаление подкласса: подтверждение; герой теряет подкласс и таланты.
  CHECK(h.clickUi("catalogs.subCard.delete"));
  CHECK(ed::confirmDialog(h));
  CHECK_EQ(subId(h->world(), war, "Дуэлянт"), Id(0));
  CHECK(h->world().character(hero)->subclass == 0 && h->world().character(hero)->talents.empty());
  CHECK_EQ(h->world().character(hero)->heroClass, war);
  // Новый класс: название сразу в правке.
  CHECK(h.clickUi("catalogs.add"));
  ed::quick(h);
  h.retype("Странник");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK(classId(h->world(), "Странник") != 0);
}

// Окно поуже: класс — выпадающим списком, карточка справа, доска — ячейки со значком и названием мелким шрифтом.
TEST(app_heroes_classes_narrow) {
  Harness h("heroes3_catalog_narrow", 1180, 820);
  h.demo();
  h.dropToasts();
  const Id pal = classId(h->world(), "Палладин");
  const Id keeper = subId(h->world(), pal, "Хранитель");
  h->openEditor("catalogs", 10);
  ed::quick(h);
  CHECK(h->uiRect("catalogs.classes") == nullptr);
  CHECK(ed::pickInCombo(h, "catalogs.classPick", "Палладин"));
  CHECK(h.clickUi("catalogs.subclass." + sid(keeper)));
  ed::quick(h);
  const Id aura = talentId(h->world(), keeper, "Аура благочестия");
  CHECK(h.clickUi("catalogs.tree.talent." + sid(aura)));
  ed::quick(h);
  // Доска из пяти столбцов целиком левее карточки.
  const RectF* tree = h->uiRect("catalogs.tree");
  const RectF* card = h->uiRect("catalogs.classCard");
  CHECK(tree && card && tree->right() <= card->x);
  CHECK(inside(h->uiRect("catalogs.tree.cell.0.4"), tree));
  shotClean(h, "heroes3_catalog_narrow");
}

// Узкое окно: карточка под доской, доска помещается по ширине.
TEST(app_heroes_classes_compact) {
  Harness h("heroes3_catalog_compact", 980, 760);
  h.demo();
  h.dropToasts();
  h->openEditor("catalogs", 10);
  ed::quick(h);
  CHECK(ed::pickInCombo(h, "catalogs.classPick", "Маг"));
  const RectF* tree = h->uiRect("catalogs.tree");
  const RectF* editor = h->uiRect("editor");
  CHECK(tree && editor && tree->right() <= editor->right());
  CHECK(inside(h->uiRect("catalogs.tree.cell.0.4"), tree));
  CHECK(inside(h->uiRect("catalogs.tree.cell.0.0"), tree));
  shotClean(h, "heroes3_catalog_compact");
  // Выбранный талант: карточка под доской прокручивается в видимую часть.
  const Id mage = classId(h->world(), "Маг");
  const SubClass& first = h->world().heroClass(mage)->subclasses.front();
  Id pick = 0;
  for (const Talent& t : first.talents)
    if (!pick && t.row == 0) pick = t.id;
  CHECK(h.clickUi("catalogs.tree.talent." + sid(pick)));
  h.settle();
  const RectF* name = h->uiRect("catalogs.talent.name");
  const RectF* ed2 = h->uiRect("editor");
  CHECK(name && ed2 && name->y >= ed2->y && name->bottom() <= ed2->bottom());
  shotClean(h, "heroes3_catalog_compact_card");
}
