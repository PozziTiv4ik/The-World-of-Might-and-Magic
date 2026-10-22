// Regnum — внутреннее устройство базовых записей мира (только для core/content*.cpp): дополнение справочников и
// дерева построек по версиям (core/content_v1.cpp, core/content_v2.cpp) с общими помощниками (core/content.cpp).
#pragma once
#include <map>

#include "core/content.h"
#include "core/schema.h"

namespace rg::content::detail {

// ---------------------------------------------------------------- данные
struct Named {
  const char* name;
  u32 color;
  const char* icon;
};

// Ключ сравнения названий (регистр, «ё»).
std::string key(std::string_view s);
// Запись списка с тем же названием (по ключу сравнения; nullptr — нет).
template <class T>
T* byName(std::vector<T>& list, std::string_view name) {
  const std::string k = key(name);
  for (T& x : list)
    if (key(x.name) == k) return &x;
  return nullptr;
}

struct SpecialDef;   // особый отряд версии 2 (content_v2.cpp)

// Подкласс героя версии 3: класс, название, ярусы талантов (content_classes.cpp).
struct TalentDef {
  const char* name;
  int ranks;
  int cost;
};
struct SubclassDef {
  const char* cls;
  const char* name;
  std::vector<std::vector<TalentDef>> tiers;
};
const std::vector<SubclassDef>& subclassDefs();

// Дополнение мира базовыми записями в транзакции: счётчики добавленного — для предупреждения нормализации.
struct Seeder {
  Tx& tx;
  std::vector<std::string> notes;
  int positions = 0, essences = 0, groups = 0, resources = 0, religions = 0, specials = 0, buildings = 0;
  int chests = 0, sites = 0, techs = 0, classes = 0, relicGroups = 0, modifiers = 0;
  int races = 0, cultures = 0, relics = 0, subclasses = 0, talents = 0;

  Catalogs& cat() { return tx.catalogs(); }

  // ---- общее (content.cpp)
  Id groupOf(const char* gkey);                       // базовая группа по ключу (создаётся при необходимости)
  static CatalogItem item(const Named& n, Id group);
  Id resource(CatalogItem it);                        // ресурс по ключу или названию; нет — создаётся
  void builtinResources();
  Id resId(const char* name);                         // ресурс по названию («Золото» — kGold); 0 — нет
  Id essId(const char* name);
  // Религии справочника по списку названий (новые — с цветами палитры).
  void addReligions(const char* const* names, size_t n);

  // ---- версия 1 (content_v1.cpp)
  void v1();
  void positionsV1();
  void legacyProvisions(Id provGroup);
  void groupsV1();
  void essencesV1();
  void abyssV1();

  // ---- версия 2 (content_v2.cpp)
  void v2();
  void currenciesV2();
  Id archFindsGroup();
  void archV2();
  void classesV2();
  // Место новой постройки на схеме общего дерева: в дорожке её категории ниже прежних построек этой дорожки, по
  // kPerRow в ряд с шагом «Расставить» дерева построек — карточки не налагаются.
  static constexpr int kPerRow = 6;
  static constexpr double kColStep = schema::kTreeColStep, kRowStep = 124;
  struct LaneCursor {
    double y0 = -1;
    int n = 0;
  };
  std::map<int, LaneCursor> lanes;
  LaneCursor& lane(BuildingCat c);
  Vec2 slot(BuildingCat c);
  void newRow(BuildingCat c);
  Id commonBuilding(const char* name);
  template <class Fill>
  Id ensureBuilding(const char* bkey, const char* name, BuildingCat catg, const char* icon, Fill&& fill, bool adopt = false);
  std::map<Id, double> costOf(const std::vector<std::pair<const char*, double>>& list);
  Id ensureSpecial(const SpecialDef& d);
  Id modifierNamed(const char* name, Fx fx, double v, const char* desc);
  void techsV2(std::map<std::string, Id>& techOf);
  void buildingsV2();
  void archGuilds();
  void shipCostsV2();
  void builtinGensV2();

  // ---- версия 3 (content_v3.cpp)
  void v3();
  void classesV3();
  void racesCulturesV3();
  void resourcesV3();
  void relicsV3();
  void buildingsV3();
  // ---- версия 4 (content_v4.cpp)
  void v4();
  void resourcesV4();
  void techsV4(Id& powderTech);
  void buildingsV4(Id powderTech);
  void intelV4();
  // ensureBuilding с adopt: одноимённая своя постройка мира становится базовой записью версии 3.
  template <class Fill>
  Id adoptBuilding(const char* bkey, const char* name, BuildingCat catg, const char* icon, Fill&& fill) {
    return ensureBuilding(bkey, name, catg, icon, std::forward<Fill>(fill), true);
  }
};

// Общая постройка с ключом или названием: есть — дополняется ключом, нет — создаётся (fill заполняет новую).
// adopt — найденная по названию постройка без ключа (своя запись мира) становится базовой: fill применяется поверх её
// данных (возможности и уровни базовой записи; уровни, которых fill не трогает, остаются).
template <class Fill>
Id Seeder::ensureBuilding(const char* bkey, const char* name, BuildingCat catg, const char* icon, Fill&& fill, bool adopt) {
  Id found = bkey ? tx.w().buildings.firstId([&](const Building& b) { return b.owner == 0 && b.key == bkey; }) : 0;
  if (!found) found = commonBuilding(name);
  if (found) {
    if (bkey && tx.w().building(found)->key.empty()) {
      Building& b = tx.building(found);
      b.key = bkey;
      if (adopt) {
        if (b.levels.empty()) b.levels.push_back(BuildingLevel{});
        fill(b);
      }
    }
    return found;
  }
  Building b;
  b.owner = 0;
  b.name = name;
  b.icon = icon;
  b.cat = catg;
  b.pos = slot(catg);
  if (bkey) b.key = bkey;
  fill(b);
  buildings++;
  return tx.add(std::move(b)).id;
}

}  // namespace rg::content::detail
