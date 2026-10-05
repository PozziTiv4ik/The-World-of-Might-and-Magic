// Regnum — справочник перечислений: русские подписи, значки, пределы правил (ТЗ).
#pragma once
#include "core/world.h"

namespace rg::schema {

struct EnumInfo {
  const char* id;     // устойчивый идентификатор для JSON
  const char* name;   // подпись в интерфейсе
  const char* icon;   // имя значка (gfx/icons)
  int value = 0;      // слоты (величина, тип города) или иное число
  u32 color = 0;      // 0xRRGGBB, если нужен цвет
};

extern const EnumInfo kUnitTypes[int(UnitType::Count)];
extern const EnumInfo kShipTypes[int(ShipType::Count)];
extern const EnumInfo kRelStatus[4];
extern const EnumInfo kProvSizes[3];
extern const EnumInfo kCityTypes[4];
extern const EnumInfo kBuildingCats[int(BuildingCat::Count)];
extern const EnumInfo kFactionKinds[2];
extern const EnumInfo kDealKinds[3];
extern const EnumInfo kDealModes[2];
extern const EnumInfo kLogKinds[int(LogKind::Count)];
extern const EnumInfo kFlagPatterns[int(FlagPattern::Count)];
extern const EnumInfo kOccupiedIncome[3];
extern const EnumInfo kStateKinds[int(StateKind::Count)];
extern const EnumInfo kModKinds[int(ModKind::Count)];
extern const EnumInfo kDealItemKinds[int(DealItemKind::Count)];
extern const EnumInfo kConstTypes[int(ConstType::Count)];
inline const EnumInfo& stateKind(StateKind k) { return kStateKinds[int(k) >= 0 && int(k) < int(StateKind::Count) ? int(k) : 0]; }
inline const EnumInfo& modKind(ModKind k) { return kModKinds[int(k) >= 0 && int(k) < int(ModKind::Count) ? int(k) : 0]; }

inline const EnumInfo& unitType(UnitType t) { return kUnitTypes[int(t)]; }
inline const EnumInfo& shipType(ShipType t) { return kShipTypes[int(t)]; }
inline const EnumInfo& relStatus(RelStatus s) { return kRelStatus[int(s)]; }
inline const EnumInfo& provSize(ProvSize s) { return kProvSizes[int(s)]; }
inline const EnumInfo& cityType(CityType c) { return kCityTypes[int(c)]; }
inline const EnumInfo& buildingCat(BuildingCat c) { return kBuildingCats[int(c)]; }
inline const EnumInfo& logKind(LogKind k) { return kLogKinds[int(k)]; }

// Найти значение перечисления по строковому id (для чтения JSON). Возвращает -1, если нет.
int findEnum(const EnumInfo* list, int n, std::string_view id);

// Эффекты модификаторов (ТЗ 1.g.ii).
struct EffectInfo {
  Fx fx;
  const char* id;
  const char* name;
  const char* unit;     // "%" или ""
  double min, max;
  bool local;           // true — провинция, false — государство/гильдия
  bool perTurn;         // применяется каждый ход
  bool targets;         // требует список целевых фракций (дипломатия)
  bool extra;           // не перечислен в ТЗ явно (слоты — из пункта 1.f.i)
  const char* icon;
  bool army = false;    // действует на войска (модификатор войска; у государства — на все его войска)
};
extern const EffectInfo kEffects[kFxCount];
inline const EffectInfo& effect(Fx f) { return kEffects[int(f)]; }

// Режимы карты.
enum class MapMode : u8 { Political, Guilds, Contentment, Rebellion, Trade, Resources, Religion, Culture, Terrain, Count };
extern const EnumInfo kMapModes[int(MapMode::Count)];

// Префиксы ID в JSON: p12, f3, ... (узлы и рёбра геометрии — без префикса).
const char* idPrefix(Seq s);

// Константы правил (ТЗ).
constexpr int kMaxHqPerProvince = 5;          // 1.d.ii
constexpr double kWarRelation = -25;          // 1.c.iv: «равны 0 и ещё минус 25»
constexpr double kRouteBonus = 0.10;          // 1.d.v: +10 % базовой ценности за маршрут
constexpr double kRouteStateBonus = 0.025;    // маршрут: ещё +2,5 % за каждое государство на его пути
constexpr double kGuildRouteShare = 0.05;     // гильдия-владелец маршрута: 5 % торговой ценности сухопутных провинций пути
constexpr double kMinTotalTax = 1.0;          // 1.d.iv: общий налог не меньше 1 %
constexpr double kMaxTotalTax = 100.0;        // общий налог не больше 100 % (налог не превышает торговую ценность)
constexpr double kRebellionPerContentment = 0.5;  // 1.a.vi: 2 довольства : 1 %
constexpr double kObjectRadius = 34;          // радиус фигурки войска/флота на карте (единицы карты)
// Расстояние встречи и наименьший промежуток между войсками (единицы карты): прежние 2 · kObjectRadius минус 30 %.
constexpr double kInteractDist = 2 * kObjectRadius * 0.7;
constexpr double kMapWidth = 8000, kMapHeight = 4500;
// Значок войска и флота на экране: размер по умолчанию и пределы ползунка (точки интерфейса).
constexpr int kFigureSizeDefault = 34, kFigureSizeMin = 18, kFigureSizeMax = 72;

// Рабы, население, провизия.
constexpr double kSlaveUpkeep = 0.001;        // золота за раба в ход
constexpr double kSlaveWorkIncome = 0.002;    // золота за раба на работах в ход
constexpr double kSlaveWorkShare = 0.10;      // рабов на работах — не больше 10 % населения провинции
constexpr double kTruceSlavesShare = 0.05;    // перемирие: рабов — не больше 5 % населения государства
constexpr double kProvisionsPerPerson = 0.001;  // провизии на жителя государства живых в ход
constexpr int kFormationTurns = 2;            // формирование отрядов и кораблей, ходов

// Захват провинции.
constexpr int kCaptureModTurns = 5;           // разграбление, разорение, опустошение и неправедные деяния — 5 ходов
constexpr double kPlunderDivisor = 10000;     // золото: ТекущаяЦенность × (Население / 10000), разорение — вдвое
constexpr double kPlunderRelation = -5, kRazeRelation = -10, kDevastateRelation = -20;
constexpr int kDevastateMaxSlavesPct = 75;    // опустошение: в рабы — не больше 75 % населения
constexpr int kConscienceBelowPct = 25;       // в рабы меньше 25 % — «Мучения совести»

// Флот в торговле.
constexpr double kFleetTradeIncome = 2;       // прибыль торговых галеонов = их содержание × 2
constexpr int kPirateGalleons = 10;           // каждые 10 галеонов — 1 % вероятности нападения пиратов
constexpr int kPirateFrigates = 5, kPirateLines = 1;   // охрана 10 галеонов: 5 фрегатов или 1 линкор
constexpr double kPirateLossPct = 5;          // пираты уничтожают 5 % галеонов в торговле (вверх до целого)

// Оккупация, восстания, верность.
constexpr int kOccupationIdleTurns = 5;       // без гарнизона и войск оккупанта — оккупация снимается через 5 ходов
constexpr double kUprisingShare = 0.10;       // мятежные крестьяне — 10 % населения провинции
constexpr double kLoyalistBonus = 5;          // «Непреклонный лоялист»: +5 % верности войска за ход
constexpr double kMinLoyalty = -100, kMaxLoyalty = 100;

// Вассалитет.
constexpr double kVassalDefendBonus = 15, kVassalAbandonPenalty = -20;   // сюзерен вступил (не вступил) в войну за вассала
constexpr double kVassalCallAgree = 10, kVassalCallRefuse = -10;         // вассал согласился (отказался) на призыв
constexpr double kVassalRebelAt = -50;        // вассал может восстать при отношениях с сюзереном −50 и ниже

// Нежить и демоны.
constexpr double kNecromancerShare = 0.10;    // некромант: 10 % численности побеждённых живых отрядов в трупы
constexpr double kDesecrateEnergy = 1000;     // осквернение: 1000 × (10 % населения) энергии
constexpr double kDesecratedEnergy = 100;     // осквернённая провинция: 100 × (10 % населения) энергии в ход
constexpr double kDesecrateShare = 0.10;

// ---------------------------------------------------------------- встроенные записи
// Ресурсы (CatalogItem::key): золото — всегда rs1.
constexpr const char* kResGold = "gold";
constexpr const char* kResProvisions = "provisions";
constexpr const char* kResCorpses = "corpses";
constexpr const char* kResEnergy = "demonEnergy";
struct BuiltinResource {
  const char* key;
  const char* name;
  u32 color;
  const char* icon;
  const char* legacy;   // прежнее название (миграция: «Зерно» → «Провизия»); nullptr — нет
};
extern const BuiltinResource kBuiltinResources[4];

// Модификаторы (Modifier::key).
namespace mod {
constexpr const char* Plundered = "plundered";            // Разграбленная провинция
constexpr const char* Ravaged = "ravaged";                // Разоренная провинция
constexpr const char* Devastated = "devastated";          // Опустошенная провинция
constexpr const char* Discontent = "discontent";          // Недовольство правителем
constexpr const char* Loyalist = "loyalist";              // Непреклонный лоялист
constexpr const char* Dead = "dead";                      // Мертв
constexpr const char* Living = "living";                  // Живой
constexpr const char* Undead = "undead";                  // Нежить
constexpr const char* Demon = "demon";                    // Демон
constexpr const char* Mechanism = "mechanism";            // Механизм
constexpr const char* Capital = "capital";                // Столица государства
constexpr const char* Captive = "captive";                // Взят в плен
constexpr const char* UndeadArmy = "undeadArmy";          // Армия нежити
constexpr const char* DemonArmy = "demonArmy";            // Армия демонов
constexpr const char* Ruthless = "ruthless";              // Безжалостная армия
constexpr const char* Unrighteous = "unrighteous";        // Неправедное деяние
constexpr const char* Conscience = "conscience";          // Мучения совести
constexpr const char* Patriotism = "patriotism";          // Патриотизм
constexpr const char* Sadism = "sadism";                  // Изуверское наслаждение
constexpr const char* Decentralization = "decentralization";
constexpr const char* WeakControl = "weakControl";
constexpr const char* Centralized = "centralized";
constexpr const char* Famine = "famine";
constexpr const char* UndeadWaste = "undeadWaste";        // Пустошь нежити
constexpr const char* Desecrated = "desecrated";          // Оскверненная провинция
constexpr const char* Necromancer = "necromancer";        // Некромант
}  // namespace mod
// Шаблоны встроенных модификаторов (id = 0) в порядке ТЗ.
const std::vector<Modifier>& builtinModifiers();
const Modifier* builtinModifier(std::string_view key);
// Природа героя: «Живой», «Нежить», «Демон», «Механизм» — взаимоисключающие.
bool isNatureKey(std::string_view key);
// Модификаторы, которые редактор ставит и снимает сам (столица, совет, голод): вручную не добавляются.
bool isAutoKey(std::string_view key);

// Глобальные константы (Constant::key).
namespace cst {
constexpr const char* ColonizationCost = "colonizationCost";   // число: золото за колонизацию
constexpr const char* ShipLineCost = "shipLineCost";           // ресурсы: один линкор
constexpr const char* FrigateCost = "frigateCost";             // ресурсы: один фрегат
constexpr const char* GalleonCost = "galleonCost";             // ресурсы: один торговый галеон
constexpr const char* UnitRaces = "unitRaces";                 // значения: расы для отрядов
constexpr const char* CorpsesPerUnit = "corpsesPerUnit";       // число: трупов на 1 воина-нежить
constexpr const char* EnergyPerUnit = "energyPerUnit";         // число: демонической энергии на 1 воина-демона
}  // namespace cst
const std::vector<Constant>& builtinConstants();
const char* shipCostKey(ShipType t);

// Базовые расы отрядов (значения константы «Расы для отрядов»).
constexpr const char* kRaceLiving = "Живой";
constexpr const char* kRaceDemonic = "Демонический";
constexpr const char* kRaceUndead = "Нежить";
constexpr const char* kRaceMechanical = "Механический";
constexpr const char* kRaceElemental = "Элементали";
// Объекты карты: пределы масштаба знака и ширины линии (единицы карты), ширина новой стены и реки.
constexpr float kMinSymbolScale = 0.2f, kMaxSymbolScale = 8.f;
constexpr float kMinShapeWidth = 0.25f, kMaxShapeWidth = 100.f;
constexpr float kWallWidth = 2.f, kRiverWidth = 4.f;

}  // namespace rg::schema
