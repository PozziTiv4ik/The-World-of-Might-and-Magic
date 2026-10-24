// Regnum — базовые типы модели мира: идентификаторы, перечисления, последовательности ID, поиск записей по ID и ключу.
#pragma once
#include <string_view>
#include <vector>

#include "base/base.h"

namespace rg {

using Id = u32;  // 0 — «нет»

// Подпись записи без названия (персонаж — «Без имени»).
constexpr const char* kUnnamed = "Без названия";
constexpr const char* kNameless = "Без имени";
inline std::string nameOr(const std::string& name, const char* none = kUnnamed) { return name.empty() ? std::string(none) : name; }

// ================================================================ перечисления
enum class Terrain : u8 { None = 0, Land = 1, Sea = 2 };
enum class EdgeKind : u8 { Border = 0, Coast = 1, Frame = 2 };
enum class ProvSize : u8 { Small = 0, Medium = 1, Large = 2 };
enum class CityType : u8 { Outpost = 0, Village = 1, Town = 2, City = 3 };
// Wild — войска без государства (пробуждённые стражи, вторжения из археологических мест): враждебны всем государствам,
// фракция одна на мир и не показывается в списках государств и гильдий.
enum class FactionKind : u8 { State = 0, Guild = 1, Wild = 2 };
// Типы войск. В файлах хранятся строковые ID (schema::kUnitTypes), поэтому порядок можно менять.
enum class UnitType : u8 {
  LightInf, MediumInf, HeavyInf, LightCav, MediumCav, HeavyCav, AirCav, Flying, Casters, Ranged, Beasts, Monsters, Machines, Elementals, Count
};
// Морское чудовище — «судно» за эссенцию воды и ресурс подгруппы «Морские чудовища» (ключевой ресурс строки флота).
enum class ShipType : u8 { ShipOfLine, Frigate, Galleon, SeaMonster, Count };
enum class RelStatus : u8 { War = 0, Alliance = 1, Neutral = 2, Unknown = 3 };
// Категории построек; Infrastructure — инфраструктурные (лечебница, хранилище реликвий, чумная лаборатория).
enum class BuildingCat : u8 { Military = 0, Economic = 1, Industrial = 2, Residential = 3, Religious = 4, Cult = 5, Infrastructure = 6, Count };
// Редкость реликвии: обычная (белая подсветка), редкая (синяя), эпическая (фиолетовая), легендарная (оранжевая),
// эпохальная (красная).
enum class Rarity : u8 { Common = 0, Rare = 1, Epic = 2, Legendary = 3, Epochal = 4, Count };
enum class ArmyKind : u8 { Army = 0, Fleet = 1 };
enum class DealKind : u8 { Trade = 0, Tribute = 1, Reparations = 2 };
enum class DealSide : u8 { A = 0, B = 1 };
enum class DealMode : u8 { Once = 0, PerTurn = 1 };
enum class DealStatus : u8 { Active = 0, Done = 1, Cancelled = 2 };
enum class OccupiedIncome : u8 { Owner = 0, Occupier = 1, None = 2 };
enum class LogKind : u8 { Turn, Economy, Build, Tech, War, Battle, Army, Fleet, Diplomacy, Trade, Province, Guild, Population, Note, Archaeology, Count };
// Объекты карты, нарисованные кодом: знаки и фигуры (суша и берег — граф провинций, см. Edge).
enum class SymbolKind : u8 { Mountain = 0, Peak = 1, Castle = 2, Tower = 3, Count };   // гора, крупная гора, замок, башня
enum class ShapeKind : u8 { Water = 0, Islet = 1, Wall = 2, River = 3, Count };      // вода, островок, стена, река линией
// Вид государства: государство живых (по умолчанию), нежити, демонов.
enum class StateKind : u8 { Living = 0, Undead = 1, Demonic = 2, Count };
// Где применяется модификатор: везде, провинция, государство (глобальный), войско, герой, археологическая группа
// (модификаторы археологических групп назначаются только группам, и группам — только они).
enum class ModKind : u8 { Any = 0, Province = 1, Faction = 2, Army = 3, Hero = 4, ArchGroup = 5, Count };
// Позиция сделки: ресурс, провинция (переходит к другой стороне), пленный герой.
enum class DealItemKind : u8 { Resource = 0, Province = 1, Hero = 2, Count };
// Вид заговора: против другого государства (заказчик — государство, видит и игрок) или внутренний (только
// администратор редактора).
enum class PlotKind : u8 { External = 0, Internal = 1, Count };
// Тип глобальной константы: число, список ресурсов с количествами, список значений.
enum class ConstType : u8 { Number = 0, Resources = 1, Values = 2, Count };
// Поле сундука сокровищ: археологическая награда, золото, ресурс, артефакт, эссенция, вложенный сундук.
enum class ChestItemKind : u8 { Treasure = 0, Gold = 1, Resource = 2, Relic = 3, Essence = 4, Chest = 5, Count };
// Предложение другой стороне или ожидающее решение государства (core/world.h, Proposal): сделка (обмен, дань,
// репарации), перемирие, союз; вопросы вассалитета — сюзерен вступает в войну за вассала, вассал отвечает на призыв.
enum class ProposalKind : u8 { Deal = 0, Truce = 1, Alliance = 2, Defend = 3, Call = 4, Count };

// Эффекты модификаторов. Пределы и подписи — schema.h (kEffects).
enum class Fx : u8 {
  PopGrowthPct, TradePct, TradeFlat, BuildCostPct, ContentmentPerTurn, RebellionPct, ResourcePct, ResourceFlat, Slots,  // локальные
  IncomePct, DiplomacyPerTurn, ArmyUpkeepPct, FleetUpkeepPct, ResearchTimePct,                                      // глобальные
  LoyaltyPerTurn,                                                                                                     // войско
  // Археологические группы (у модификатора группы — эта группа, у глобального — все группы государства).
  ArchSuccessPct, ArchVitalityPct, ArchExpPct, ArchUpkeepPct,
  // Глобальные (ветка технологий «Археология»): сокровища от всех действий групп, шанс обнаружения места,
  // начальный уровень новых групп.
  ArchTreasurePct, ArchDiscoveryPct, ArchStartLevel,
  // Заговоры: успех заказного убийства героя (модификатор героя: «Бдительный», «Параноик»…), успех заказных убийств
  // заговоров государства против других государств (глобальный: «Штаб Разведки»), успех заказных убийств против
  // героев государства (глобальный: «Штаб Контрразведки» — отрицательный).
  AssassinPct, PlotAssassinPct, AssassinGuardPct,
  Count
};
constexpr int kFxCount = int(Fx::Count);

// Последовательности идентификаторов (имена, префиксы ID в JSON и падежи для сообщений — schema::seqInfo).
enum class Seq : u8 {
  Province, Faction, Character, Modifier, Building, Tech, Army, Route, Deal, Log, Row, Council,
  Node, Edge, Resource, Race, Culture, Religion, Government, Position, Symbol, Shape,
  Essence, Relic, Special, ResGroup, HeroClass, Talent, RelicGroup, ArchSite, Chest, ArchGroup, SubClass, Plot, Proposal, Count
};
constexpr int kSeqCount = int(Seq::Count);

constexpr Id kGold = 1;      // встроенный ресурс «Золото» = казна
constexpr int kArchSlots = 4;  // археологические слоты провинции: «Обычное», «Редкое», «Эпическое», «Легендарное место»

// ================================================================ поиск в списках записей
// Запись списка с полем id (первая; nullptr — нет).
template <class T> const T* findById(const std::vector<T>& list, Id id) {
  for (const T& x : list)
    if (x.id == id) return &x;
  return nullptr;
}
template <class T> T* findById(std::vector<T>& list, Id id) {
  for (T& x : list)
    if (x.id == id) return &x;
  return nullptr;
}
// Запись списка с полем key (первая; nullptr — нет).
template <class T> const T* findByKey(const std::vector<T>& list, std::string_view key) {
  for (const T& x : list)
    if (x.key == key) return &x;
  return nullptr;
}
template <class T> T* findByKey(std::vector<T>& list, std::string_view key) {
  for (T& x : list)
    if (x.key == key) return &x;
  return nullptr;
}

// Цепочка ссылок start → next(start) → … (0 — конец) доходит до target не больше чем за guard шагов (сам start —
// первый шаг). Защита guard — от циклов в данных.
template <class Next> bool chainReaches(Id start, Id target, int guard, Next&& next) {
  for (int i = 0; start && i < guard; i++) {
    if (start == target) return true;
    start = next(start);
  }
  return false;
}

}  // namespace rg
