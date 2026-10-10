// Regnum — классы героев и деревья талантов (ТЗ «Доработки №4», п.6, 8), уровень героя, изучение и отмена талантов,
// «Возвысить до Лича» (ТЗ «Доработки №1», п.11), группы реликвий, изображение и карточка реликвии (п.14).
#include <set>

#include "codec/png.h"
#include "core/io.h"
#include "rules/internal.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

Id classNamed(const World& w, const char* name) {
  for (const HeroClass& c : w.catalogs->classes)
    if (c.name == name) return c.id;
  return 0;
}

const Talent* talentOf(const World& w, Id id) {
  const HeroClass* c = talentClass(w, id);
  return c ? c->talent(id) : nullptr;
}

// Класс с деревом: ярус 1 — A (2 очка), B (1); ярус 2 — C (условие A), D; ярус 3 — E. Очков на ярус — 3.
struct Tree {
  Id cls = 0, a = 0, b = 0, c = 0, d = 0, e = 0;
};
Tree makeTree(Fix& f) {
  Tree t;
  f.tx([&](Tx& tx) {
    t.cls = addHeroClass(tx, "Паладин Зари");
    HeroClass hc = *tx.w().heroClass(t.cls);
    hc.tierPoints = 3;
    setHeroClass(tx, hc);
    t.a = addTalent(tx, t.cls, 0, 0, "Свет");
    t.b = addTalent(tx, t.cls, 0, 1, "Щит");
    t.c = addTalent(tx, t.cls, 1, 0, "Кара");
    t.d = addTalent(tx, t.cls, 1, 2, "Аура");
    t.e = addTalent(tx, t.cls, 2, 1, "Длань");
    Talent ta = *talentOf(tx.w(), t.a);
    ta.cost = 2;
    setTalent(tx, ta);
    setTalentPrereq(tx, t.c, t.a);
  });
  return t;
}

std::string png(int w, int h) {
  codec::RgbaImage img;
  img.w = w;
  img.h = h;
  img.rgba.assign(size_t(w) * size_t(h) * 4, 200);
  const std::vector<u8> b = codec::encodePng(img, 6);
  return std::string(b.begin(), b.end());
}

}  // namespace

// ---------------------------------------------------------------- классы
TEST(rules_talents_classes) {
  Fix f;
  // 20 базовых классов (ТЗ «Доработки №4», п.6).
  CHECK_EQ(f.w().catalogs->classes.size(), size_t(20));
  CHECK(classNamed(f.w(), "Палладин") && classNamed(f.w(), "Некромант") && classNamed(f.w(), "Ассасин"));
  Id cls = 0;
  f.tx([&](Tx& tx) { cls = addHeroClass(tx, ""); });
  CHECK(f.w().heroClass(cls) && f.w().heroClass(cls)->name == "Новый класс");
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addHeroClass(tx, "воин"); }); }), "уже есть"));
  HeroClass hc = *f.w().heroClass(cls);
  hc.name = "Варвар";
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHeroClass(tx, hc); }); }), "уже есть"));
  hc.name = "Странник";
  hc.tierPoints = 0;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHeroClass(tx, hc); }); }), "Очков на ярус"));
  hc.tierPoints = 4;
  hc.desc = "Бродит по миру";
  f.tx([&](Tx& tx) { setHeroClass(tx, hc); });
  CHECK(f.w().heroClass(cls)->name == "Странник" && f.w().heroClass(cls)->tierPoints == 4 && f.w().heroClass(cls)->desc == "Бродит по миру");
  // Удаление класса: герои теряют класс и таланты.
  Id hero = 0, t = 0;
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Эйрик");
    t = addTalent(tx, cls, 0, 0, "Тропа");
    setCharacterClass(tx, hero, cls);
    learnTalent(tx, hero, t);
  });
  CHECK(f.w().character(hero)->talents == std::vector<Id>{t});
  f.tx([&](Tx& tx) { removeHeroClass(tx, cls); });
  CHECK(!f.w().heroClass(cls));
  CHECK(f.w().character(hero)->heroClass == 0 && f.w().character(hero)->talents.empty());
}

// ---------------------------------------------------------------- дерево талантов
TEST(rules_talents_tree_edit) {
  Fix f;
  const Tree t = makeTree(f);
  const HeroClass& hc = *f.w().heroClass(t.cls);
  CHECK_EQ(hc.talents.size(), size_t(5));
  CHECK_EQ(talentTiers(hc), 3);
  CHECK_EQ(tierThreshold(hc, 2), 6);
  // Ячейка — одна: занятая ячейка и выход за сетку — отказ.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addTalent(tx, t.cls, 0, 0); }); }), "занята"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addTalent(tx, t.cls, 0, 4); }); }), "Столбец"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addTalent(tx, t.cls, -1, 0); }); }), "Ярус"));
  // Название уникально в дереве, стоимость 1…5.
  Talent b = *talentOf(f.w(), t.b);
  b.name = "свет";
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTalent(tx, b); }); }), "уже есть"));
  b.name = "Щит веры";
  b.cost = 6;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setTalent(tx, b); }); }), "Стоимость"));
  b.cost = 5;
  f.tx([&](Tx& tx) { setTalent(tx, b); });
  CHECK(talentOf(f.w(), t.b)->cost == 5 && talentOf(f.w(), t.b)->name == "Щит веры");
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
  f.tx([&](Tx& tx) {
    b.modifiers.push_back(ensureBuiltinMod(tx, schema::mod::Dead));
  });
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
  f.tx([&](Tx& tx) { moveTalent(tx, t.d, 1, 3); });
  CHECK(talentOf(f.w(), t.d)->row == 1 && talentOf(f.w(), t.d)->col == 3);
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

// ---------------------------------------------------------------- герой: изучение
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
  // Очков — по уровню: 1 очко не хватает на «Свет» (2).
  std::string why;
  CHECK(!canLearnTalent(f.w(), hero, t.a, &why) && has(why, "Не хватает очков"));
  CHECK(canLearnTalent(f.w(), hero, t.b));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHeroLevel(tx, hero, 61); }); }), "от 1 до 60"));
  f.tx([&](Tx& tx) { setHeroLevel(tx, hero, 10); });
  CHECK_EQ(talentPoints(f.w(), hero), 10);
  // Условие: «Кара» требует «Свет»; ярус 2 открыт с 3 очками выше.
  CHECK(!canLearnTalent(f.w(), hero, t.c, &why) && has(why, "Сначала изучите"));
  f.tx([&](Tx& tx) { learnTalent(tx, hero, t.a); });
  CHECK(!canLearnTalent(f.w(), hero, t.c, &why) && has(why, "Ярус 2"));
  CHECK(!canLearnTalent(f.w(), hero, t.d, &why) && has(why, "Ярус 2"));
  f.tx([&](Tx& tx) { learnTalent(tx, hero, t.b); });
  CHECK_EQ(talentSpentAbove(f.w(), hero, 1), 3);
  f.tx([&](Tx& tx) {
    learnTalent(tx, hero, t.c);
    learnTalent(tx, hero, t.d);
  });
  CHECK(!canLearnTalent(f.w(), hero, t.a, &why) && has(why, "уже изучен"));
  // Ярус 3: нужно 6 очков выше (вложено 5).
  CHECK(!canLearnTalent(f.w(), hero, t.e, &why) && has(why, "Ярус 3"));
  CHECK_EQ(talentPointsSpent(f.w(), hero), 5);
  // Изучение у каждого героя своё.
  CHECK(f.w().character(other)->talents.empty());
  // Отмена: от «Света» зависит «Кара»; без «Щита» закроется ярус 2.
  CHECK(!canUnlearnTalent(f.w(), hero, t.a, &why) && has(why, "зависит"));
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
  f.tx([&](Tx& tx) { setCharacterClass(tx, hero, classNamed(tx.w(), "Маг")); });
  CHECK(f.w().character(hero)->talents.empty() && f.w().character(hero)->heroClass == classNamed(f.w(), "Маг"));
}

// Модификаторы изученных талантов действуют на героя; правка дерева снимает незаконные таланты.
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
    HeroClass hc = *tx.w().heroClass(t.cls);
    hc.tierPoints = 4;
    lost = setHeroClass(tx, hc);
  });
  CHECK_EQ(lost, 1);
  CHECK((f.w().character(hero)->talents == std::vector<Id>{t.a, t.b}));
  // Дороже «Щит»: уровня 6 хватает (2 + 5), «Свет» и «Щит» остаются; ещё дороже «Свет» — «Щит» не помещается.
  f.tx([&](Tx& tx) {
    Talent b = *talentOf(tx.w(), t.b);
    b.cost = 4;
    lost = setTalent(tx, b);
  });
  CHECK_EQ(lost, 0);
  f.tx([&](Tx& tx) {
    Talent a = *talentOf(tx.w(), t.a);
    a.cost = 3;
    lost = setTalent(tx, a);
  });
  CHECK_EQ(lost, 1);
  CHECK(f.w().character(hero)->talents == std::vector<Id>{t.a});
  CHECK(!characterHas(f.w(), hero, schema::mod::Loyalist));
  // Удаление изученного таланта — у героя он снимается.
  f.tx([&](Tx& tx) { lost = removeTalent(tx, t.a); });
  CHECK_EQ(lost, 1);
  CHECK(f.w().character(hero)->talents.empty());
}

// Чтение файла: ячейка — одна (повтор переносится), столбец 1…4, условие — только из яруса выше.
TEST(rules_talents_normalize_tree) {
  Fix f;
  Id cls = 0, a = 0, b = 0, c = 0, d = 0;
  f.tx([&](Tx& tx) {
    cls = addHeroClass(tx, "Скиталец");
    a = addTalent(tx, cls, 0, 0, "Один");
    b = addTalent(tx, cls, 1, 0, "Два");
    c = addTalent(tx, cls, 2, 0, "Три");
    d = addTalent(tx, cls, 3, 0, "Четыре");
    for (HeroClass& k : tx.catalogs().classes)
      if (k.id == cls)
        for (Talent& t : k.talents) {
          if (t.id == b) t.row = 0;          // та же ячейка, что «Один»
          if (t.id == c) t.col = 9;          // за сеткой
          if (t.id == d) {
            t.row = 2;                       // условие в том же ярусе
            t.prereq = c;
          }
        }
  });
  World w = f.w();
  io::Warnings warns;
  io::normalize(w, warns);
  const HeroClass& hc = *w.heroClass(cls);
  CHECK(hc.talent(a)->row == 0 && hc.talent(a)->col == 0);
  CHECK(hc.talent(b)->row == 0 && hc.talent(b)->col == 1);
  CHECK_EQ(hc.talent(c)->col, kTalentCols - 1);
  CHECK_EQ(hc.talent(d)->prereq, Id(0));
  std::set<std::pair<int, int>> cells;
  for (const Talent& t : hc.talents) CHECK(cells.insert({t.row, t.col}).second);
  CHECK(!warns.empty());
  // Изученные таланты героя: по порядку изучения — только законные (ярус закрыт, условие не изучено — сняты).
  const Tree t = makeTree(f);
  Id hero = 0;
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Ирма");
    setCharacterClass(tx, hero, t.cls);
    setHeroLevel(tx, hero, 20);
    tx.character(hero).talents = {t.c, t.a, t.b, t.e};   // «Кара» раньше условия; «Длань» — ярус 3 закрыт (вложено 4 из 6)
  });
  World w2 = f.w();
  io::Warnings warns2;
  io::normalize(w2, warns2);
  CHECK((w2.character(hero)->talents == std::vector<Id>{t.a, t.b}));
  bool warned = false;
  for (const io::Warning& x : warns2) warned = warned || x.msg.find("нельзя изучить") != std::string::npos;
  CHECK(warned);
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
  f.tx([&](Tx& tx) { ascendLich(tx, hero, epic); });
  CHECK(characterHas(f.w(), hero, schema::mod::Lich));
  CHECK(!characterHas(f.w(), hero, schema::mod::Necromancer));
  CHECK(!lichOffered(f.w(), hero));
  CHECK_NEAR(f.w().faction(f.A)->stock(corpses), 5000, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->essence(death), 200, 1e-9);
  CHECK_EQ(f.w().relic(epic)->name, std::string("Чёрный череп (Филактерия «Мортис»)"));
  CHECK(isPhylactery(*f.w().relic(epic)));
  CHECK_EQ(relicHolder(f.w(), epic), hero);
  // Лич генерирует 150 эссенции смерти за ход; хроника записана.
  CHECK_NEAR(f.fc(f.A).essences.at(death).generation, 150, 1e-9);
  bool logged = false;
  f.w().log.each([&](const LogEntry& e) { logged = logged || has(e.text, "возвысился до Лича"); });
  CHECK(logged);
  // Повторно — уже лич.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { ascendLich(tx, hero, legend); }); }), "уже лич"));
}

// ---------------------------------------------------------------- группы реликвий, изображение, карточка
TEST(rules_talents_relic_groups) {
  Fix f;
  const Id finds = f.w().catalogs->relicGroupId(schema::kRelicArchFinds);
  CHECK(finds != 0);
  Id arms = 0, swords = 0, blade = 0, crown = 0, shard = 0;
  f.tx([&](Tx& tx) {
    arms = addRelicGroup(tx, "Оружие");
    swords = addRelicGroup(tx, "Мечи", arms);
    blade = addRelic(tx, "Клинок скорби", Rarity::Legendary);
    crown = addRelic(tx, "Корона", Rarity::Epochal);
    shard = addRelic(tx, "Черепок", Rarity::Common);
    setRelicGroup(tx, blade, swords);
    setRelicGroup(tx, crown, arms);
    setRelicGroup(tx, shard, finds);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addRelicGroup(tx, "мечи", arms); }); }), "уже есть"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { renameRelicGroup(tx, swords, " "); }); }), "пустым"));
  f.tx([&](Tx& tx) { renameRelicGroup(tx, swords, "Клинки"); });
  CHECK(relicGroupPath(f.w(), swords) == "Оружие / Клинки");
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelicGroupParent(tx, arms, swords); }); }), "саму себя"));
  CHECK((relicsIn(f.w(), arms) == std::vector<Id>{blade, crown}));
  CHECK((childRelicGroups(f.w(), arms) == std::vector<Id>{swords}));
  // Главенство редкости: эпохальная выше.
  std::vector<Id> all = relicsIn(f.w(), 0);
  sortByRarity(f.w(), all);
  CHECK((all == std::vector<Id>{crown, blade, shard}));
  CHECK(isArchFind(f.w(), shard));
  // Группу «Археологические находки» удалить нельзя; удаление группы отдаёт подгруппы и реликвии родителю.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { removeRelicGroup(tx, finds); }); }), "нельзя удалить"));
  f.tx([&](Tx& tx) { removeRelicGroup(tx, swords); });
  CHECK(f.w().relic(blade)->group == arms);
  f.tx([&](Tx& tx) { removeRelicGroup(tx, arms); });
  CHECK(f.w().relic(blade)->group == 0 && f.w().relic(crown)->group == 0);
  // Изображение: только PNG/JPEG; пусто — значок.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelicImage(tx, crown, "не картинка"); }); }), "PNG или JPEG"));
  const std::string img = png(8, 8);
  f.tx([&](Tx& tx) { setRelicImage(tx, crown, img); });
  CHECK(f.w().relic(crown)->image == img);
  f.tx([&](Tx& tx) { setRelicImage(tx, crown, ""); });
  CHECK(f.w().relic(crown)->image.empty());
  f.tx([&](Tx& tx) { setRelicEntity(tx, crown, " ASSET-0037 "); });
  CHECK_EQ(f.w().relic(crown)->entity, std::string("ASSET-0037"));
}
