// Regnum — справочники мира (Catalogs) и глобальные константы (Constants).
#pragma once
#include <array>
#include <map>

#include "core/types.h"

namespace rg {

struct CatalogItem {
  Id id = 0;
  std::string name;
  Color color = Color::hex(0x888888);
  std::string icon;
  bool builtin = false;
  std::string key;                    // встроенный ресурс (schema::kRes*): золото, трупы, энергия, запчасти механизмов
  // Должность: глобальные модификаторы, пока она занята. Раса, культура, религия, форма правления: модификаторы
  // провинций с этой культурой, религией или преобладающей расой (локальные эффекты) и государств с этой культурой,
  // религией или формой правления (глобальные эффекты).
  std::vector<Id> modifiers;
  std::vector<Id> vacantModifiers;    // должность: глобальные модификаторы, пока она пустует
  Id group = 0;                       // ресурс: группа справочника (0 — без группы)
};

// Группа дерева справочника: parent 0 — группа верхнего уровня; key — группа, на которую опираются правила (такую
// нельзя удалить). Tag различает виды групп (ресурсов и реликвий) при общем устройстве.
template <class Tag>
struct TreeGroup {
  Id id = 0;
  std::string name;
  Id parent = 0;
  std::string key;
};
// Группа ресурсов («Руда», её подгруппы «Обычная», «Особая»…); ключи групп правил — schema::grp (провизия, звери,
// ездовые, чудовища).
using ResGroup = TreeGroup<struct ResGroupTag>;
// Группа реликвий; key — группа правил («Археологические находки»).
using RelicGroup = TreeGroup<struct RelicGroupTag>;

// Группа g — сама root или её потомок (через parent; глубина — не больше 64).
template <class Tag> bool inTree(const std::vector<TreeGroup<Tag>>& groups, Id g, Id root) {
  return chainReaches(g, root, 64, [&](Id x) {
    const TreeGroup<Tag>* p = findById(groups, x);
    return p ? p->parent : Id(0);
  });
}

// Реликвия справочника с редкостью. Главенство редкости: эпохальная > легендарная > эпическая > редкая > обычная
// (больше Rarity — выше). Легендарная и эпохальная — уникальны: одна штука в одном месте (инвентарь героя, хранилище
// постройки, инвентарь государства или тайник провинции). Эпическая и ниже — «копируемые»: лежат стопками с
// количеством (RelicStack) в скольких угодно местах, у скольких угодно героев. image — своё изображение (PNG/JPEG),
// entity — карточка проекта (портрет канона). modifiers действуют как модификаторы героя, в чьём инвентаре реликвия.
// Филактерия лича (phylactery) уникальна при любой редкости, не выпадает из сундуков и раскопок и не продаётся.
struct Relic {
  Id id = 0;
  std::string name;
  Rarity rarity = Rarity::Common;
  std::string desc;
  Id group = 0;                       // группа реликвий (RelicGroup)
  std::string entity;
  std::string image;
  std::vector<Id> modifiers;          // модификаторы героя-носителя
  bool phylactery = false;            // филактерия лича
  bool unique() const { return phylactery || int(rarity) >= int(Rarity::Legendary); }
};
// Талант дерева подкласса героя (как дерево талантов World of Warcraft): ярус row (сверху вниз) и столбец col; ranks —
// рангов (1…5), cost — очков за ранг (1…5; финальные таланты — один ранг за 5 очков); prereq — талант, который нужно
// изучить полностью раньше; modifiers — модификаторы героя, пока изучен хотя бы один ранг.
struct Talent {
  Id id = 0;
  std::string name, desc;
  std::string icon = "star";
  int row = 0, col = 0;
  int cost = 1;
  int ranks = 1;
  Id prereq = 0;
  std::vector<Id> modifiers;
  int maxPoints() const { return std::max(1, ranks) * std::max(1, cost); }
};
// Подкласс героя: своя ветка талантов. Изучение — у каждого героя своё (Character::talents). Талант яруса r доступен,
// когда в ярусах выше уже вложено не меньше r × tierPoints очков.
struct SubClass {
  Id id = 0;
  std::string name, desc;
  std::string icon = "hero-class";
  int tierPoints = 10;
  std::vector<Talent> talents;
  const Talent* talent(Id t) const { return findById(talents, t); }
};
// Законные изученные ранги (порядок изучения learned, талант повторяется по числу рангов): каждый ранг законен после
// уже оставленных — талант дерева sc, рангов не больше ranks, условие изучено полностью, ярус открыт (в ярусах выше
// вложено не меньше ярус × tierPoints очков), всего вложено не больше points очков. Незаконные ранги пропускаются.
std::vector<Id> lawfulTalents(const SubClass& sc, const std::vector<Id>& learned, int points);
// Изучено рангов таланта и вложено очков (всего и в ярусах выше row).
int talentRank(const std::vector<Id>& learned, Id talent);
int talentSpent(const SubClass& sc, const std::vector<Id>& learned);
int talentSpentAbove(const SubClass& sc, const std::vector<Id>& learned, int row);

// Класс героя: подклассы (число не ограничено), у каждого — своё дерево талантов; герой выбирает класс и подкласс.
// modifiers — модификаторы героя этого класса (как собственные).
struct HeroClass {
  Id id = 0;
  std::string name, desc;
  std::string icon = "hero";
  Color color = Color::hex(0x8c7ae6);
  std::vector<SubClass> subclasses;
  std::vector<Id> modifiers;
  const SubClass* subclass(Id s) const { return findById(subclasses, s); }
};
// Археологическое место: постоянный список из десяти мест (новые не добавляются). rewards — «Финальная награда
// исследования» по слотам (0 — «Обычное место» … 3 — «Легендарное место»): сундуки, из которых случайно выбирается
// сокровище провинции.
struct ArchSite {
  Id id = 0;
  std::string key;
  std::string name;
  std::string icon = "pickaxe";
  std::array<std::vector<Id>, kArchSlots> rewards;
};
// Поле сундука сокровищ: археологическая награда, золото, ресурс (конкретный или случайный из группы «Руда» или
// «Материалы», с исключениями), артефакт (случайная свободная реликвия группы с одной из редкостей), эссенция
// (конкретная или случайная), сундук (случайный из списка).
struct ChestItem {
  ChestItemKind kind = ChestItemKind::Treasure;
  double amount = 0;
  Id res = 0;                         // ресурс: конкретный (0 — случайный из group)
  Id group = 0;                       // ресурс: группа для случайного выбора
  std::vector<Id> exclude;            // ресурс: не выбирать
  Id essence = 0;                     // эссенция: конкретная (0 — случайная из справочника)
  Id relicGroup = 0;                  // артефакт: группа реликвий
  u32 rarities = 0;                   // артефакт: допустимые редкости (бит = Rarity)
  std::vector<Id> chests;             // сундук: случайный из списка
  bool operator==(const ChestItem&) const = default;
};
// Сундук сокровищ.
struct Chest {
  Id id = 0;
  std::string key;                    // базовый сундук (сопоставление при дополнении мира)
  std::string name;
  std::string icon = "chest";
  std::string desc;
  std::vector<ChestItem> items;
};
// Особый отряд мира (справочник «Особые отряды»): нанимается государством, у которого достроена постройка
// доступа (Building::specials). Поля — как у строки армии (ArmyRow).
struct SpecialUnit {
  Id id = 0;
  std::string name;
  UnitType type = UnitType::Monsters;
  std::string race;                   // пусто — по виду государства и типу
  Id keyRes = 0;
  double keyPer = 1;
  std::map<Id, double> extra, essence;
  double upkeep = 0;
  std::map<Id, double> essUpkeep;
  std::string desc;
};
struct Catalogs {
  std::vector<CatalogItem> resources, races, cultures, religions, governments, positions, essences;
  std::vector<ResGroup> resGroups;    // группы ресурсов (порядок — как в справочнике)
  std::vector<Relic> relics;
  std::vector<SpecialUnit> specials;
  std::vector<RelicGroup> relicGroups;
  std::vector<HeroClass> classes;     // классы героев с деревьями талантов
  std::vector<ArchSite> archSites;    // археологические места (постоянный список)
  std::vector<Chest> chests;          // сундуки сокровищ
  static const CatalogItem* find(const std::vector<CatalogItem>& list, Id id) { return findById(list, id); }
  // Встроенный ресурс по ключу (nullptr — нет).
  const CatalogItem* resourceByKey(std::string_view key) const { return findByKey(resources, key); }
  Id resourceId(std::string_view key) const { const CatalogItem* c = resourceByKey(key); return c ? c->id : 0; }
  const ResGroup* group(Id id) const { return findById(resGroups, id); }
  const ResGroup* groupByKey(std::string_view key) const { return findByKey(resGroups, key); }
  Id groupId(std::string_view key) const { const ResGroup* g = groupByKey(key); return g ? g->id : 0; }
  // Группа g — сама root или её потомок (через parent).
  bool inGroup(Id g, Id root) const { return inTree(resGroups, g, root); }
  // Ресурс входит в группу root (с подгруппами).
  bool resourceIn(Id res, Id root) const {
    const CatalogItem* r = find(resources, res);
    return r && root && inGroup(r->group, root);
  }
  const Relic* relic(Id id) const { return findById(relics, id); }
  const SpecialUnit* special(Id id) const { return findById(specials, id); }
  const RelicGroup* relicGroup(Id id) const { return findById(relicGroups, id); }
  Id relicGroupId(std::string_view key) const { const RelicGroup* g = findByKey(relicGroups, key); return g ? g->id : 0; }
  // Группа реликвий g — сама root или её потомок.
  bool inRelicGroup(Id g, Id root) const { return inTree(relicGroups, g, root); }
  const HeroClass* heroClass(Id id) const { return findById(classes, id); }
  // Подкласс по ID (любого класса) и его класс (nullptr — нет).
  const SubClass* subclass(Id s) const {
    for (auto& c : classes)
      if (const SubClass* x = c.subclass(s)) return x;
    return nullptr;
  }
  const HeroClass* classOfSubclass(Id s) const {
    for (auto& c : classes)
      if (c.subclass(s)) return &c;
    return nullptr;
  }
  // Подкласс, в дереве которого талант (nullptr — нет).
  const SubClass* subclassOfTalent(Id t) const {
    for (auto& c : classes)
      for (auto& s : c.subclasses)
        if (s.talent(t)) return &s;
    return nullptr;
  }
  const ArchSite* archSite(Id id) const { return findById(archSites, id); }
  const ArchSite* archSiteByKey(std::string_view key) const { return findByKey(archSites, key); }
  const Chest* chest(Id id) const { return findById(chests, id); }
  const Chest* chestByKey(std::string_view key) const { return findByKey(chests, key); }
};

// Глобальная константа: число, список ресурсов или список значений.
struct Constant {
  std::string key;                    // устойчивый ключ: встроенные — schema::cst, свои — «userN»
  std::string name;
  ConstType type = ConstType::Number;
  double num = 0;
  std::map<Id, double> res;
  std::vector<std::string> values;
  bool builtin = false;               // встроенная: не удаляется, тип не меняется
  std::string desc;
  std::map<Id, double> ess;           // список ресурсов: ещё и эссенции (стоимость морского чудовища)
  // Наименьшие значения базовой стоимости (стоимость кораблей): цену можно увеличивать и дополнять, но нельзя
  // уменьшить ниже базовой и убрать базовый ресурс или эссенцию.
  std::map<Id, double> minRes, minEss;
};
struct Constants {
  std::vector<Constant> list;         // порядок — как в интерфейсе
  const Constant* find(std::string_view key) const { return findByKey(list, key); }
  double num(std::string_view key, double def = 0) const { const Constant* c = find(key); return c && c->type == ConstType::Number ? c->num : def; }
};

}  // namespace rg
