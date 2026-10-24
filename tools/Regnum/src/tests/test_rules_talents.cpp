// Regnum — классы и подклассы героев, деревья талантов подклассов с рангами, уровень героя, изучение и отмена рангов,
// «Возвысить до Лича», трупы после битвы у лича.
#include <set>

#include "base/json.h"
#include "core/io.h"
#include "rules/internal.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

// Запись списка по названию (nullptr — нет).
template <class T> const T* findByName(const std::vector<T>& list, std::string_view name) {
  for (const T& x : list)
    if (x.name == name) return &x;
  return nullptr;
}

const Talent* talentOf(const World& w, Id id) {
  const SubClass* s = talentSubClass(w, id);
  return s ? s->talent(id) : nullptr;
}

// Подкласс с деревом: ярус 1 — A (1 ранг × 2 очка), B (3 ранга × 1); ярус 2 — C (условие A), D; ярус 3 — E.
// Очков на ярус — 3.
struct Tree {
  Id cls = 0, sub = 0, a = 0, b = 0, c = 0, d = 0, e = 0;
};
Tree makeTree(Fix& f) {
  Tree t;
  f.tx([&](Tx& tx) {
    t.cls = addHeroClass(tx, "Паладин Зари");
    t.sub = addSubClass(tx, t.cls, "Страж рассвета");
    SubClass sc = *tx.w().catalogs->subclass(t.sub);
    sc.tierPoints = 3;
    setSubClass(tx, sc);
    t.a = addTalent(tx, t.sub, 0, 0, "Свет");
    t.b = addTalent(tx, t.sub, 0, 1, "Щит");
    t.c = addTalent(tx, t.sub, 1, 0, "Кара");
    t.d = addTalent(tx, t.sub, 1, 2, "Аура");
    t.e = addTalent(tx, t.sub, 2, 1, "Длань");
    Talent ta = *talentOf(tx.w(), t.a);
    ta.cost = 2;
    setTalent(tx, ta);
    Talent tb = *talentOf(tx.w(), t.b);
    tb.ranks = 3;
    setTalent(tx, tb);
    setTalentPrereq(tx, t.c, t.a);
  });
  return t;
}

}  // namespace

// ---------------------------------------------------------------- классы и подклассы
TEST(rules_talents_classes) {
  Fix f;
  // 20 базовых классов, «Жнец» стал «Мехомантом», «Палладин» — «Паладином»; у каждого — подклассы с деревьями по 60
  // очков, ярус открывается после 7 очков в ярусе выше (по 10 очков в ярусе — есть выбор).
  const auto& classes = f.w().catalogs->classes;
  CHECK_EQ(classes.size(), size_t(20));
  CHECK(test::idByName(classes, "Паладин") && test::idByName(classes, "Некромант") && test::idByName(classes, "Мехомант"));
  CHECK(!test::idByName(classes, "Жнец") && !test::idByName(classes, "Палладин"));
  int subs = 0;
  for (const HeroClass& c : classes) {
    CHECK(c.subclasses.size() >= 3);
    for (const SubClass& s : c.subclasses) {
      subs++;
      int pts = 0;
      for (const Talent& t : s.talents) pts += t.maxPoints();
      CHECK_EQ(pts, 60);
      CHECK_EQ(talentTiers(s), 6);
      CHECK_EQ(s.tierPoints, 7);
    }
  }
  CHECK_EQ(subs, 70);
  const HeroClass* war = f.w().heroClass(test::idByName(classes, "Воин"));
  const SubClass* knight = war ? findByName(war->subclasses, "Рыцарь") : nullptr;
  CHECK(knight != nullptr);
  if (knight) {
    const Talent* ult = findByName(knight->talents, "Бастион Чести");
    CHECK(ult && ult->row == 5 && ult->ranks == 1 && ult->cost == 5);
    const Talent* first = findByName(knight->talents, "Тяжелая атлетика");
    CHECK(first && first->row == 0 && first->ranks == 5 && first->cost == 1);
  }
  Id cls = 0;
  f.tx([&](Tx& tx) { cls = addHeroClass(tx, ""); });
  CHECK(f.w().heroClass(cls) && f.w().heroClass(cls)->name == "Новый класс");
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addHeroClass(tx, "воин"); }); }), "уже есть"));
  HeroClass hc = *f.w().heroClass(cls);
  hc.name = "Варвар";
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHeroClass(tx, hc); }); }), "уже есть"));
  hc.name = "Странник";
  hc.desc = "Бродит по миру";
  f.tx([&](Tx& tx) { setHeroClass(tx, hc); });
  CHECK(f.w().heroClass(cls)->name == "Странник" && f.w().heroClass(cls)->desc == "Бродит по миру");
  // Подклассы: название уникально в классе, очки на ярус 1…60.
  Id sub = 0, sub2 = 0;
  f.tx([&](Tx& tx) {
    sub = addSubClass(tx, cls, "Следопыт");
    sub2 = addSubClass(tx, cls, "");
  });
  CHECK(f.w().catalogs->subclass(sub2)->name == "Новый подкласс");
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addSubClass(tx, cls, "следопыт"); }); }), "уже есть"));
  SubClass sc = *f.w().catalogs->subclass(sub);
  sc.tierPoints = 0;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setSubClass(tx, sc); }); }), "Очков на ярус"));
  sc.tierPoints = 4;
  f.tx([&](Tx& tx) { setSubClass(tx, sc); });
  CHECK_EQ(f.w().catalogs->subclass(sub)->tierPoints, 4);
  CHECK_EQ(subclassClass(f.w(), sub)->id, cls);
  // Класс с несколькими подклассами — подкласс выбирают; таланты — только из дерева выбранного подкласса.
  Id hero = 0, t = 0;
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Эйрик");
    t = addTalent(tx, sub, 0, 0, "Тропа");
    setCharacterClass(tx, hero, cls);
  });
  CHECK_EQ(f.w().character(hero)->subclass, Id(0));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { learnTalent(tx, hero, t); }); }), "не выбран подкласс"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setCharacterSubclass(tx, hero, knight ? knight->id : 0); }); }), "не из класса"));
  f.tx([&](Tx& tx) {
    setCharacterSubclass(tx, hero, sub);
    learnTalent(tx, hero, t);
  });
  CHECK(f.w().character(hero)->talents == std::vector<Id>{t});
  // Смена подкласса сбрасывает таланты; удаление подкласса — подкласс и таланты героев.
  f.tx([&](Tx& tx) { setCharacterSubclass(tx, hero, sub2); });
  CHECK(f.w().character(hero)->talents.empty());
  f.tx([&](Tx& tx) {
    setCharacterSubclass(tx, hero, sub);
    learnTalent(tx, hero, t);
  });
  f.tx([&](Tx& tx) { removeSubClass(tx, sub); });
  CHECK(!f.w().catalogs->subclass(sub));
  CHECK(f.w().character(hero)->subclass == 0 && f.w().character(hero)->talents.empty());
  // Класс с единственным подклассом выбирается сразу; удаление класса — герои теряют класс, подкласс и таланты.
  Id only = 0;
  f.tx([&](Tx& tx) {
    only = addHeroClass(tx, "Одиночка");
    addSubClass(tx, only, "Единственный");
    setCharacterClass(tx, hero, only);
  });
  CHECK(f.w().character(hero)->subclass != 0);
  f.tx([&](Tx& tx) { removeHeroClass(tx, only); });
  CHECK(!f.w().heroClass(only));
  CHECK(f.w().character(hero)->heroClass == 0 && f.w().character(hero)->subclass == 0);
}

// ---------------------------------------------------------------- дерево талантов
TEST(rules_talents_tree_edit) {
  Fix f;
  const Tree t = makeTree(f);
  const SubClass& sc = *f.w().catalogs->subclass(t.sub);
  CHECK_EQ(sc.talents.size(), size_t(5));
  CHECK_EQ(talentTiers(sc), 3);
  CHECK_EQ(tierThreshold(sc, 2), 6);
  // Ячейка — одна: занятая ячейка и выход за сетку — отказ.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addTalent(tx, t.sub, 0, 0); }); }), "занята"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addTalent(tx, t.sub, 0, kTalentCols); }); }), "Столбец"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addTalent(tx, t.sub, -1, 0); }); }), "Ярус"));
  // Название уникально в дереве, стоимость ранга 1…5, рангов 1…5.
  Talent b = *talentOf(f.w(), t.b);
  b.name = "свет";
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTalent(tx, b); }); }), "уже есть"));
  b.name = "Щит веры";
  b.cost = 6;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTalent(tx, b); }); }), "Стоимость"));
  b.cost = 1;
  b.ranks = 6;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTalent(tx, b); }); }), "Рангов"));
  b.ranks = 5;
  f.tx([&](Tx& tx) { setTalent(tx, b); });
  CHECK(talentOf(f.w(), t.b)->ranks == 5 && talentOf(f.w(), t.b)->name == "Щит веры");
  // Модификаторы — только для героев (или везде), без состояний героя.
  Id heroMod = 0, provMod = 0;
  f.tx([&](Tx& tx) {
    heroMod = createModifier(tx, "Стойкость");
    tx.modifier(heroMod).kind = ModKind::Hero;
    provMod = createModifier(tx, "Плодородие");
    tx.modifier(provMod).kind = ModKind::Province;
  });
  b.modifiers = {provMod};
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTalent(tx, b); }); }), "не для героев"));
  b.modifiers = {heroMod};
  f.tx([&](Tx& tx) { b.modifiers.push_back(ensureBuiltinMod(tx, schema::mod::Dead)); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTalent(tx, b); }); }), "состояние"));
  b.modifiers = {heroMod};
  f.tx([&](Tx& tx) { setTalent(tx, b); });
  CHECK(talentOf(f.w(), t.b)->modifiers == std::vector<Id>{heroMod});
  // Условие — только из яруса выше; на себя — нельзя.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTalentPrereq(tx, t.b, t.a); }); }), "ярусе выше"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTalentPrereq(tx, t.e, t.e); }); }), "сам себя"));
  f.tx([&](Tx& tx) { setTalentPrereq(tx, t.e, t.c); });
  CHECK(talentDependents(f.w(), t.c) == std::vector<Id>{t.e});
  // Перенос: условие должно остаться выше зависимого.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { moveTalent(tx, t.c, 0, 3); }); }), "должно стоять выше"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { moveTalent(tx, t.a, 1, 1); }); }), "должно стоять выше"));
  // В свободную ячейку того же яруса — можно; на занятую — таланты меняются местами.
  f.tx([&](Tx& tx) { moveTalent(tx, t.d, 1, 4); });
  CHECK(talentOf(f.w(), t.d)->row == 1 && talentOf(f.w(), t.d)->col == 4);
  f.tx([&](Tx& tx) { moveTalent(tx, t.a, 0, 1); });
  CHECK(talentOf(f.w(), t.a)->col == 1 && talentOf(f.w(), t.b)->col == 0 && talentOf(f.w(), t.b)->row == 0);
  // Удаление таланта: условие зависимого снимается.
  f.tx([&](Tx& tx) { removeTalent(tx, t.c); });
  CHECK(!talentOf(f.w(), t.c));
  CHECK(talentOf(f.w(), t.e)->prereq == 0);
  // Удалённый модификатор уходит и из талантов.
  f.tx([&](Tx& tx) { removeModifier(tx, heroMod); });
  CHECK(talentOf(f.w(), t.b)->modifiers.empty());
}

// ---------------------------------------------------------------- герой: изучение рангов
TEST(rules_talents_learning) {
  Fix f;
  const Tree t = makeTree(f);
  Id hero = 0, other = 0;
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Лиара");
    other = createCharacter(tx, f.A, "Брен");
  });
  CHECK_EQ(f.w().character(hero)->level, 1);
  // Без класса изучать нечего.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { learnTalent(tx, hero, t.b); }); }), "нет класса"));
  f.tx([&](Tx& tx) {
    setCharacterClass(tx, hero, t.cls);
    setCharacterClass(tx, other, t.cls);
  });
  CHECK_EQ(f.w().character(hero)->subclass, t.sub);   // единственный подкласс выбран сразу
  // Очков — по уровню: 1 очко не хватает на ранг «Света» (2).
  std::string why;
  CHECK(!canLearnTalent(f.w(), hero, t.a, &why) && has(why, "Не хватает очков"));
  CHECK(canLearnTalent(f.w(), hero, t.b));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHeroLevel(tx, hero, 61); }); }), "от 1 до 60"));
  f.tx([&](Tx& tx) { setHeroLevel(tx, hero, 10); });
  CHECK_EQ(talentPoints(f.w(), hero), 10);
  // Условие: «Кара» требует «Свет» полностью; ярус 2 открыт с 3 очками выше.
  CHECK(!canLearnTalent(f.w(), hero, t.c, &why) && has(why, "Сначала изучите"));
  f.tx([&](Tx& tx) { learnTalent(tx, hero, t.a); });
  CHECK(!canLearnTalent(f.w(), hero, t.c, &why) && has(why, "Ярус 2"));
  // Ранги «Щита»: 1 из 3, затем 2 из 3, третий — не нужен для яруса 2.
  f.tx([&](Tx& tx) { learnTalent(tx, hero, t.b); });
  CHECK_EQ(heroTalentRank(f.w(), hero, t.b), 1);
  CHECK_EQ(heroSpentAbove(f.w(), hero, 1), 3);
  f.tx([&](Tx& tx) {
    learnTalent(tx, hero, t.c);
    learnTalent(tx, hero, t.d);
    learnTalent(tx, hero, t.b);
    learnTalent(tx, hero, t.b);
  });
  CHECK_EQ(heroTalentRank(f.w(), hero, t.b), 3);
  CHECK(!canLearnTalent(f.w(), hero, t.b, &why) && has(why, "изучен полностью"));
  CHECK(!canLearnTalent(f.w(), hero, t.a, &why) && has(why, "изучен полностью"));
  // Ярус 3: вложено 7 из 6 — открыт.
  CHECK(canLearnTalent(f.w(), hero, t.e));
  CHECK_EQ(talentPointsSpent(f.w(), hero), 7);
  // Изучение у каждого героя своё.
  CHECK(f.w().character(other)->talents.empty());
  // Отмена ранга: от «Света» зависит «Кара»; без последних рангов «Щита» ярус 2 держится, без первого — нет.
  CHECK(!canUnlearnTalent(f.w(), hero, t.a, &why) && has(why, "зависит"));
  CHECK(canUnlearnTalent(f.w(), hero, t.b));
  f.tx([&](Tx& tx) {
    unlearnTalent(tx, hero, t.b);
    unlearnTalent(tx, hero, t.b);
  });
  CHECK_EQ(heroTalentRank(f.w(), hero, t.b), 1);
  CHECK(!canUnlearnTalent(f.w(), hero, t.b, &why) && has(why, "закроется ярус 2"));
  CHECK(canUnlearnTalent(f.w(), hero, t.d));
  f.tx([&](Tx& tx) { unlearnTalent(tx, hero, t.d); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { unlearnTalent(tx, hero, t.d); }); }), "не изучен"));
  // Уровень ниже вложенного — отказ; с повторным выбором — сброс талантов.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHeroLevel(tx, hero, 3); }); }), "Сбросьте таланты"));
  f.tx([&](Tx& tx) { setHeroLevel(tx, hero, 4); });
  f.tx([&](Tx& tx) { setHeroLevel(tx, hero, 2, true); });
  CHECK(f.w().character(hero)->talents.empty() && f.w().character(hero)->level == 2);
  // Сброс и смена класса.
  f.tx([&](Tx& tx) {
    setHeroLevel(tx, hero, 10);
    learnTalent(tx, hero, t.b);
  });
  f.tx([&](Tx& tx) { resetTalents(tx, hero); });
  CHECK(f.w().character(hero)->talents.empty());
  f.tx([&](Tx& tx) { learnTalent(tx, hero, t.b); });
  f.tx([&](Tx& tx) { setCharacterClass(tx, hero, test::idByName(tx.w().catalogs->classes, "Маг")); });
  CHECK(f.w().character(hero)->talents.empty() && f.w().character(hero)->heroClass == test::idByName(f.w().catalogs->classes, "Маг"));
  CHECK_EQ(f.w().character(hero)->subclass, Id(0));
}

// Базовое дерево: 60 очков на 60 уровне, финальный талант — 5 очков за раз после 50 вложенных.
TEST(rules_talents_base_tree_full) {
  Fix f;
  const HeroClass* war = f.w().heroClass(test::idByName(f.w().catalogs->classes, "Воин"));
  const SubClass* knight = war ? findByName(war->subclasses, "Рыцарь") : nullptr;
  CHECK(knight != nullptr);
  if (!knight) return;
  Id hero = 0;
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Гарет");
    setCharacterClass(tx, hero, war->id);
    setCharacterSubclass(tx, hero, knight->id);
    setHeroLevel(tx, hero, 60);
  });
  const Talent* ult = findByName(knight->talents, "Бастион Чести");
  std::string why;
  CHECK(!canLearnTalent(f.w(), hero, ult->id, &why) && has(why, "Ярус 6"));
  // Все ярусы 1–5 полностью (50 очков), затем оба финальных таланта.
  f.tx([&](Tx& tx) {
    for (int row = 0; row < 5; row++)
      for (const Talent& t : knight->talents)
        if (t.row == row)
          for (int r = 0; r < t.ranks; r++) learnTalent(tx, hero, t.id);
  });
  CHECK_EQ(talentPointsSpent(f.w(), hero), 50);
  f.tx([&](Tx& tx) {
    for (const Talent& t : knight->talents)
      if (t.row == 5) learnTalent(tx, hero, t.id);
  });
  CHECK_EQ(talentPointsSpent(f.w(), hero), 60);
  CHECK_EQ(heroTalentRank(f.w(), hero, ult->id), 1);
}

// Модификаторы изученных талантов действуют на героя; правка дерева снимает незаконные ранги.
TEST(rules_talents_modifiers_and_fit) {
  Fix f;
  const Tree t = makeTree(f);
  Id hero = 0;
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Мирра");
    setCharacterClass(tx, hero, t.cls);
    setHeroLevel(tx, hero, 6);
    Talent b = *talentOf(tx.w(), t.b);
    b.modifiers = {ensureBuiltinMod(tx, schema::mod::Loyalist)};
    setTalent(tx, b);
  });
  CHECK(!characterHas(f.w(), hero, schema::mod::Loyalist));
  f.tx([&](Tx& tx) {
    learnTalent(tx, hero, t.a);
    learnTalent(tx, hero, t.b);
    learnTalent(tx, hero, t.c);
  });
  CHECK(characterHas(f.w(), hero, schema::mod::Loyalist));
  CHECK_EQ(talentModifiers(f.w(), hero).size(), size_t(1));
  // Больше очков на ярус: «Кара» (ярус 2, нужно 4 выше, вложено 3) снимается, остальное остаётся.
  int lost = 0;
  f.tx([&](Tx& tx) {
    SubClass sc = *tx.w().catalogs->subclass(t.sub);
    sc.tierPoints = 4;
    lost = setSubClass(tx, sc);
  });
  CHECK_EQ(lost, 1);
  CHECK((f.w().character(hero)->talents == std::vector<Id>{t.a, t.b}));
  // Дороже «Свет»: уровня 6 хватает на 5 + 1; меньше рангов «Щита» — лишние ранги снимаются.
  f.tx([&](Tx& tx) {
    learnTalent(tx, hero, t.b);
    Talent a = *talentOf(tx.w(), t.a);
    a.cost = 5;
    lost = setTalent(tx, a);
  });
  CHECK_EQ(lost, 1);
  CHECK((f.w().character(hero)->talents == std::vector<Id>{t.a, t.b}));
  // Удаление изученного таланта — у героя он снимается.
  f.tx([&](Tx& tx) { lost = removeTalent(tx, t.a); });
  CHECK_EQ(lost, 1);
  CHECK((f.w().character(hero)->talents == std::vector<Id>{t.b}));
  f.tx([&](Tx& tx) { lost = removeTalent(tx, t.b); });
  CHECK(f.w().character(hero)->talents.empty());
  CHECK(!characterHas(f.w(), hero, schema::mod::Loyalist));
}

// Чтение файла: ячейка — одна (повтор переносится), столбец в пределах дерева, условие — только из яруса выше; ранги
// героя — только законные.
TEST(rules_talents_normalize_tree) {
  Fix f;
  Id cls = 0, sub = 0, a = 0, b = 0, c = 0, d = 0;
  f.tx([&](Tx& tx) {
    cls = addHeroClass(tx, "Скиталец");
    sub = addSubClass(tx, cls, "Тропы");
    a = addTalent(tx, sub, 0, 0, "Один");
    b = addTalent(tx, sub, 1, 0, "Два");
    c = addTalent(tx, sub, 2, 0, "Три");
    d = addTalent(tx, sub, 3, 0, "Четыре");
    for (HeroClass& k : tx.catalogs().classes)
      for (SubClass& s : k.subclasses)
        if (s.id == sub)
          for (Talent& t : s.talents) {
            if (t.id == b) t.row = 0;          // та же ячейка, что «Один»
            if (t.id == c) t.col = 9;          // за сеткой
            if (t.id == d) {
              t.row = 2;                       // условие в том же ярусе
              t.prereq = c;
              t.ranks = 9;                     // рангов больше пяти
            }
          }
  });
  World w = f.w();
  io::Warnings warns;
  io::normalize(w, warns);
  const SubClass& sc = *w.catalogs->subclass(sub);
  CHECK(sc.talent(a)->row == 0 && sc.talent(a)->col == 0);
  CHECK(sc.talent(b)->row == 0 && sc.talent(b)->col == 1);
  CHECK_EQ(sc.talent(c)->col, kTalentCols - 1);
  CHECK_EQ(sc.talent(d)->prereq, Id(0));
  CHECK_EQ(sc.talent(d)->ranks, schema::kMaxTalentRanks);
  std::set<std::pair<int, int>> cells;
  for (const Talent& t : sc.talents) CHECK(cells.insert({t.row, t.col}).second);
  CHECK(!warns.empty());
  // Изученные ранги героя: по порядку изучения — только законные (условие не изучено полностью, ярус закрыт, лишний
  // ранг — сняты).
  const Tree t = makeTree(f);
  Id hero = 0;
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Ирма");
    setCharacterClass(tx, hero, t.cls);
    setHeroLevel(tx, hero, 20);
    tx.character(hero).talents = {t.c, t.a, t.b, t.b, t.b, t.b, t.e};   // «Кара» раньше условия; четвёртый ранг «Щита»
  });
  World w2 = f.w();
  io::Warnings warns2;
  io::normalize(w2, warns2);
  CHECK((w2.character(hero)->talents == std::vector<Id>{t.a, t.b, t.b, t.b}));
  bool warned = false;
  for (const io::Warning& x : warns2) warned = warned || x.msg.find("нельзя изучить") != std::string::npos;
  CHECK(warned);
}

// Мир прежней версии: дерево талантов у самого класса становится его подклассом, герои класса получают подкласс.
TEST(rules_talents_legacy_class_tree) {
  Fix f;
  Id cls = 0, hero = 0, sub = 0, a = 0;
  f.tx([&](Tx& tx) {
    cls = addHeroClass(tx, "Древний");
    sub = addSubClass(tx, cls, "Древний");
    a = addTalent(tx, sub, 0, 0, "Корень");
    hero = createCharacter(tx, f.A, "Старец");
    setCharacterClass(tx, hero, cls);
    learnTalent(tx, hero, a);
  });
  // Прежний формат: у класса поля tierPoints и talents, у героя нет поля subclass.
  json::Value v = json::parse(io::toJson(f.w()));
  for (json::Value& c : v["catalogs"]["classes"].items()) {
    if (c.str("name", "") != "Древний") continue;
    json::Value subs = c["subclasses"];
    c.set("tierPoints", subs[0].get("tierPoints"));
    c.set("talents", subs[0].get("talents"));
    c.set("subclasses", json::Value::array());
  }
  for (json::Value& ch : v["characters"].items())
    if (ch.str("name", "") == "Старец") ch.set("subclass", json::Value());
  io::LoadResult r = io::fromJson(json::write(v));
  const HeroClass* hc = r.world.heroClass(cls);
  CHECK(hc && hc->subclasses.size() == 1);
  if (!hc || hc->subclasses.empty()) return;
  CHECK_EQ(hc->subclasses[0].name, std::string("Древний"));
  CHECK(hc->subclasses[0].talent(a) != nullptr);
  CHECK_EQ(r.world.character(hero)->subclass, hc->subclasses[0].id);
  CHECK(r.world.character(hero)->talents == std::vector<Id>{a});
}

// ---------------------------------------------------------------- лич
TEST(rules_talents_lich) {
  Fix f;
  Id hero = 0, epic = 0, rare = 0, legend = 0;
  const Id death = deathEssence(f.w());
  CHECK(death != 0);
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Мортис");
    epic = addRelic(tx, "Чёрный череп", Rarity::Epic);
    rare = addRelic(tx, "Старый перстень", Rarity::Rare);
    legend = addRelic(tx, "Сердце зимы", Rarity::Legendary);
    giveRelic(tx, hero, rare);
    giveRelic(tx, hero, epic);
    giveRelic(tx, hero, legend);
  });
  // Без «Некроманта» кнопки нет.
  CHECK(!lichOffered(f.w(), hero));
  CHECK(has(join(lichProblems(f.w(), hero, epic), "\n"), "только некроманта"));
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, hero, ensureBuiltinMod(tx, schema::mod::Necromancer)); });
  CHECK(lichOffered(f.w(), hero));
  // Филактерия — реликвии инвентаря не ниже эпической, по главенству редкости.
  CHECK((phylacteryRelics(f.w(), hero) == std::vector<Id>{legend, epic}));
  // Не хватает трупов и эссенции смерти; редкая реликвия не годится.
  {
    const std::string p = join(lichProblems(f.w(), hero, rare), "\n");
    CHECK(has(p, "трупов") && has(p, "эссенции смерти") && has(p, "не ниже эпической"));
  }
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { ascendLich(tx, hero, epic); }); }), "трупов"));
  const Id corpses = resourceId(f.w(), schema::kResCorpses);
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[corpses] = 25000;
    tx.faction(f.A).ess[death] = 5200;
  });
  CHECK(lichProblems(f.w(), hero, epic).empty());
  // Реликвия не из инвентаря героя — нельзя.
  Id loose = 0;
  f.tx([&](Tx& tx) { loose = addRelic(tx, "Ничья корона", Rarity::Epochal); });
  CHECK(has(join(lichProblems(f.w(), hero, loose), "\n"), "из инвентаря героя"));
  // Эпическая (копируемая) реликвия: одна штука становится отдельной реликвией-филактерией.
  f.tx([&](Tx& tx) { ascendLich(tx, hero, epic); });
  CHECK(characterHas(f.w(), hero, schema::mod::Lich));
  CHECK(!characterHas(f.w(), hero, schema::mod::Necromancer));
  CHECK(!lichOffered(f.w(), hero));
  CHECK_NEAR(f.w().faction(f.A)->stock(corpses), 5000, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->essence(death), 200, 1e-9);
  CHECK_EQ(f.w().relic(epic)->name, std::string("Чёрный череп"));
  const Id ph = test::idByName(f.w().catalogs->relics, "Чёрный череп (Филактерия «Мортис»)");
  CHECK(ph != 0);
  CHECK(isPhylactery(*f.w().relic(ph)));
  CHECK_EQ(relicHolder(f.w(), ph), hero);
  CHECK_EQ(relicCount(f.w().character(hero)->inventory, epic), i64(0));
  // Лич генерирует 150 эссенции смерти за ход; хроника записана.
  CHECK_NEAR(f.fc(f.A).essences.at(death).generation, 150, 1e-9);
  bool logged = false;
  f.w().log.each([&](const LogEntry& e) { logged = logged || has(e.text, "возвысился до Лича"); });
  CHECK(logged);
  // Повторно — уже лич.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { ascendLich(tx, hero, legend); }); }), "уже лич"));
}

// Уникальная (легендарная) реликвия-филактерия переименовывается на месте.
TEST(rules_talents_lich_unique_phylactery) {
  Fix f;
  Id hero = 0, legend = 0;
  const Id death = deathEssence(f.w());
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Велиар");
    legend = addRelic(tx, "Сердце зимы", Rarity::Legendary);
    giveRelic(tx, hero, legend);
    addModifier(tx, ModTarget::Character, hero, ensureBuiltinMod(tx, schema::mod::Necromancer));
    tx.faction(f.A).res[ensureResource(tx, schema::kResCorpses)] = 20000;
    tx.faction(f.A).ess[death] = 5000;
  });
  f.tx([&](Tx& tx) { ascendLich(tx, hero, legend); });
  CHECK_EQ(f.w().relic(legend)->name, std::string("Сердце зимы (Филактерия «Велиар»)"));
  CHECK_EQ(relicHolder(f.w(), legend), hero);
}

TEST(rules_talents_lich_corpses) {
  Fix f;
  Id lich = 0;
  f.tx([&](Tx& tx) {
    lich = createCharacter(tx, f.A, "Лич");
    addModifier(tx, ModTarget::Character, lich, ensureBuiltinMod(tx, schema::mod::Lich));
  });
  double got = 0;
  f.tx([&](Tx& tx) { got = detail::battleCorpses(tx, f.A, {lich}, 1000); });
  CHECK_NEAR(got, 500, 1e-9);   // 50 % за лича
  CHECK_NEAR(f.fc(f.A).essences.at(test::idByName(f.w().catalogs->essences, "Эссенция смерти")).generation, 150, 1e-9);
}
