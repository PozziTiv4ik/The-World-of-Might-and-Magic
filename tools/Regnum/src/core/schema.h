// Regnum — справочник перечислений: русские подписи, значки, пределы и константы правил.
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
extern const EnumInfo kFactionKinds[3];
extern const EnumInfo kDealKinds[3];
extern const EnumInfo kDealModes[2];
extern const EnumInfo kLogKinds[int(LogKind::Count)];
extern const EnumInfo kFlagPatterns[int(FlagPattern::Count)];
extern const EnumInfo kOccupiedIncome[3];
extern const EnumInfo kStateKinds[int(StateKind::Count)];
extern const EnumInfo kModKinds[int(ModKind::Count)];
extern const EnumInfo kDealItemKinds[int(DealItemKind::Count)];
extern const EnumInfo kConstTypes[int(ConstType::Count)];
// Поля сундука сокровищ (ChestItemKind).
extern const EnumInfo kChestItemKinds[int(ChestItemKind::Count)];
// Редкость реликвий: color — цвет подсветки (белая, синяя, фиолетовая, оранжевая, красная).
extern const EnumInfo kRarities[int(Rarity::Count)];
// Виды заговоров (PlotKind).
extern const EnumInfo kPlotKinds[int(PlotKind::Count)];
inline const EnumInfo& plotKind(PlotKind k) { return kPlotKinds[int(k) >= 0 && int(k) < int(PlotKind::Count) ? int(k) : 0]; }
// Виды предложений (ProposalKind).
extern const EnumInfo kProposalKinds[int(ProposalKind::Count)];
inline const EnumInfo& proposalKind(ProposalKind k) {
  return kProposalKinds[int(k) >= 0 && int(k) < int(ProposalKind::Count) ? int(k) : 0];
}
inline const EnumInfo& rarity(Rarity r) { return kRarities[int(r) >= 0 && int(r) < int(Rarity::Count) ? int(r) : 0]; }
inline const EnumInfo& stateKind(StateKind k) { return kStateKinds[int(k) >= 0 && int(k) < int(StateKind::Count) ? int(k) : 0]; }
inline const EnumInfo& modKind(ModKind k) { return kModKinds[int(k) >= 0 && int(k) < int(ModKind::Count) ? int(k) : 0]; }

inline const EnumInfo& unitType(UnitType t) { return kUnitTypes[int(t) >= 0 && int(t) < int(UnitType::Count) ? int(t) : 0]; }
// Найм юнита: звери, чудовища, военные механизмы и элементали не требуют
// населения, трупов и демонической энергии; элементали нанимаются только за эссенции элементов и содержатся ими.
bool needsPeople(UnitType t);
bool isCavalry(UnitType t);                // лёгкая, средняя, тяжёлая кавалерия
bool needsKeyResource(UnitType t);         // кавалерия, воздушная кавалерия, звери, чудовища, военные механизмы
bool isElemental(UnitType t);
inline const EnumInfo& shipType(ShipType t) { return kShipTypes[int(t)]; }
inline const EnumInfo& relStatus(RelStatus s) { return kRelStatus[int(s)]; }
inline const EnumInfo& provSize(ProvSize s) { return kProvSizes[int(s)]; }
inline const EnumInfo& cityType(CityType c) { return kCityTypes[int(c) >= 0 && int(c) < 4 ? int(c) : 0]; }
// Тип города по населению: аванпост (до 1000), деревня (1000–10 000), небольшой город (10 000–50 000), большой
// город (50 000 и больше).
constexpr i64 kVillagePop = 1000, kTownPop = 10000, kCityPop = 50000;
inline CityType cityTypeFor(i64 population) {
  return population >= kCityPop ? CityType::City : population >= kTownPop ? CityType::Town : population >= kVillagePop ? CityType::Village : CityType::Outpost;
}
inline const EnumInfo& buildingCat(BuildingCat c) { return kBuildingCats[int(c)]; }
inline const EnumInfo& logKind(LogKind k) { return kLogKinds[int(k)]; }

// Найти значение перечисления по строковому id (для чтения JSON). Возвращает -1, если нет.
int findEnum(const EnumInfo* list, int n, std::string_view id);

// Эффекты модификаторов.
struct EffectInfo {
  Fx fx;
  const char* id;
  const char* name;
  const char* unit;     // "%" или ""
  double min, max;
  bool local;           // true — провинция, false — государство/гильдия
  bool perTurn;         // применяется каждый ход
  bool targets;         // требует список целевых фракций (дипломатия)
  bool extra;           // производный эффект (слоты построек)
  const char* icon;
  bool army = false;    // действует на войска (модификатор войска; у государства — на все его войска)
  bool arch = false;    // действует на археологические группы (модификатор группы; у государства — на все его группы)
  bool hero = false;    // действует только на самого героя (модификатор героя, таланты): не глобальный
};
extern const EffectInfo kEffects[kFxCount];
inline const EffectInfo& effect(Fx f) { return kEffects[int(f)]; }

// Режимы карты. Diplomacy — война, союз, вассалы и сюзерен относительно выбранного или своего государства; Plague —
// чума, иммунитет, карантин, пустошь нежити и осквернение.
enum class MapMode : u8 { Political, Guilds, Contentment, Rebellion, Trade, Resources, Religion, Culture, Terrain, Diplomacy, Plague, Count };
extern const EnumInfo kMapModes[int(MapMode::Count)];

// Последовательности ID: имя счётчика (world.json, meta.seq), префикс ID в JSON (p12, f3…; узлы, дуги, знаки и фигуры
// карты — без префикса), родительный падеж для сообщений («нет провинции p12»).
struct SeqInfo {
  const char* name;
  const char* prefix;
  const char* genitive;
};
extern const SeqInfo kSeqs[kSeqCount];
const SeqInfo& seqInfo(Seq s);   // неизвестная последовательность — префикс «x», «объекта»
inline const char* idPrefix(Seq s) { return seqInfo(s).prefix; }

// Простые списки справочников (CatalogItem): ключ массива в data/catalogs.json, последовательность ID, поле Catalogs,
// подписи записи («Ресурс») и новой записи («Новый ресурс»).
enum class CatalogList : u8 { Resources, Races, Cultures, Religions, Governments, Positions, Essences };
constexpr int kCatalogListCount = 7;
struct CatalogListInfo {
  CatalogList list;
  const char* key;
  Seq seq;
  std::vector<CatalogItem> Catalogs::*items;
  const char* noun;
  const char* fresh;
};
extern const CatalogListInfo kCatalogLists[kCatalogListCount];
const CatalogListInfo& catalogListInfo(CatalogList l);   // неизвестный список — UserError
inline std::vector<CatalogItem>& catalogList(Catalogs& c, CatalogList l) { return c.*catalogListInfo(l).items; }
inline const std::vector<CatalogItem>& catalogList(const Catalogs& c, CatalogList l) { return c.*catalogListInfo(l).items; }
// У записей списка есть модификаторы: должности (занятая и пустующая), расы, культуры, религии, формы правления.
inline bool catalogHasModifiers(CatalogList l) {
  return l == CatalogList::Positions || l == CatalogList::Races || l == CatalogList::Cultures || l == CatalogList::Religions ||
         l == CatalogList::Governments;
}

// Константы правил (формулы — docs/RULES.md).
constexpr int kMaxHqPerProvince = 5;          // штабов гильдий в провинции — не больше
constexpr double kWarRelation = -25;          // объявление войны: «равны 0 и ещё минус 25»
constexpr double kRouteBonus = 0.10;          // +10 % базовой ценности за маршрут
constexpr double kRouteStateBonus = 0.025;    // маршрут: ещё +2,5 % за каждое государство на его пути
constexpr double kGuildRouteShare = 0.05;     // гильдия-владелец маршрута: 5 % торговой ценности сухопутных провинций пути
constexpr double kMinTotalTax = 1.0;          // общий налог не меньше 1 %
constexpr double kMaxTotalTax = 100.0;        // общий налог не больше 100 % (налог не превышает торговую ценность)
constexpr double kRebellionPerContentment = 0.5;  // 2 довольства : 1 % риска восстания
constexpr double kObjectRadius = 34;          // радиус фигурки войска/флота на карте (единицы карты)
// Расстояние встречи и наименьший промежуток между войсками (единицы карты): прежние 2 · kObjectRadius минус 30 %.
constexpr double kInteractDist = 2 * kObjectRadius * 0.7;
constexpr double kMapWidth = 8000, kMapHeight = 4500;
constexpr i64 kMaxCount = 1000000000000000LL;    // предел численностей и населения (точно в double)
// Схемы деревьев построек и технологий: шаг столбцов (слоёв) и строк расстановки (rules::autoLayout).
constexpr double kTreeColStep = 280, kTreeRowStep = 120;
// Следующая позиция на схеме дерева: правее всех узлов дерева (same — узел того же дерева); пустое дерево — (0, 0).
template <class T, class Pred>
Vec2 nextTreePos(const Table<T>& t, Pred&& same) {
  bool any = false;
  double x = 0;
  t.each([&](const T& e) {
    if (!same(e)) return;
    x = any ? std::max(x, e.pos.x) : e.pos.x;
    any = true;
  });
  return any ? Vec2{x + kTreeColStep, 0} : Vec2{0, 0};
}
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
constexpr double kFleetTradeIncome = 2;       // прибыль торговых галеонов = их базовое содержание × 2
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
// Ресурсы (CatalogItem::key): золото — всегда rs1. Трупы и демоническая энергия есть в справочнике с создания мира
// (начальные запасы задаются сразу); запчасти механизмов — ключевой ресурс военных механизмов.
constexpr const char* kResGold = "gold";
constexpr const char* kResCorpses = "corpses";
constexpr const char* kResEnergy = "demonEnergy";
constexpr const char* kResMechParts = "mechParts";
constexpr const char* kResArchTreasure = "archTreasure";      // «Археологические сокровища» (валюта археологии)
constexpr const char* kResShantiriScrolls = "shantiriScrolls"; // «Древние свитки Шантири»
// Прежний встроенный ресурс «Провизия» (до групп ресурсов): при чтении мира переносится в группу «Провизия».
constexpr const char* kResLegacyProvisions = "provisions";
struct BuiltinResource {
  const char* key;
  const char* name;
  u32 color;
  const char* icon;
  const char* group;    // ключ группы (schema::grp), в которой ресурс создаётся; nullptr — без группы
};
extern const BuiltinResource kBuiltinResources[6];
const BuiltinResource* builtinResource(std::string_view key);
// Запись справочника встроенного ресурса: название, цвет, значок, ключ, builtin (без ID и группы).
CatalogItem builtinResourceItem(const BuiltinResource& b);

// Группы ресурсов, на которые опираются правила (ResGroup::key): провизия (расход населением и голод), звери и
// ездовые (ключевой ресурс кавалерии, воздушной кавалерии, зверей и чудовищ). Их нельзя удалить.
namespace grp {
constexpr const char* Ore = "ore";
constexpr const char* OreCommon = "oreCommon";
constexpr const char* OreSpecial = "oreSpecial";
constexpr const char* Beasts = "beasts";
constexpr const char* MountsGround = "mountsGround";
constexpr const char* MountsFlying = "mountsFlying";
constexpr const char* WarBeasts = "warBeasts";
constexpr const char* Monsters = "monsters";
constexpr const char* Provisions = "provisions";
constexpr const char* ProvAgri = "provAgri";
constexpr const char* ProvAnimal = "provAnimal";
constexpr const char* ProvGather = "provGather";
constexpr const char* Materials = "materials";
constexpr const char* MatRaw = "matRaw";
constexpr const char* MatPrecious = "matPrecious";
constexpr const char* MatAlchemy = "matAlchemy";
constexpr const char* MatIndustrial = "matIndustrial";
constexpr const char* Currencies = "currencies";     // «Валюты»: золото, трупы, демоническая энергия, сокровища, свитки
constexpr const char* SeaMonsters = "seaMonsters";   // «Звери / Морские чудовища»: ключевой ресурс морского чудовища
}  // namespace grp
// Группа нужна правилам (провизия, звери, ездовые, чудовища): удалить нельзя.
bool isRuleGroup(std::string_view key);
// Ключевой ресурс юнита: группа (ключ grp), из которой он выбирается; exclude — подгруппа, которая не подходит
// (звери — без чудовищ); resKey — единственный подходящий ресурс (военные механизмы — «Запчасти механизмов»).
struct KeyRule {
  const char* group = nullptr;
  const char* exclude = nullptr;
  const char* exclude2 = nullptr;   // ещё одна неподходящая подгруппа (звери — без морских чудовищ)
  const char* resKey = nullptr;
  bool fixedOne = true;   // ровно 1 на юнит (механизмы — не меньше 1)
};
KeyRule keyRule(UnitType t);
// Ключевой ресурс корабля: морское чудовище — ресурс подгруппы «Морские чудовища» (1 на судно); остальные — нет.
KeyRule shipKeyRule(ShipType t);
// Ресурс справочника подходит ключевым ресурсом юнита типа t (по keyRule) или корабля типа t (по shipKeyRule).
bool keyAllowed(const Catalogs& c, UnitType t, Id res);
bool shipKeyAllowed(const Catalogs& c, ShipType t, Id res);
// Наёмники — только пехота и кавалерия (лёгкая, средняя, тяжёлая).
bool isMercType(UnitType t);

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
constexpr const char* Unpaid = "unpaid";                  // Невыплата жалованья: казна в долгу (ставится сама)
constexpr const char* Quarantine = "quarantine";          // Карантин: чума не входит в провинцию и не выходит из неё
constexpr const char* ForeignFaith = "foreignFaith";      // Чужая вера: религия провинции — не религия государства (ставится сама)
constexpr const char* UndeadWaste = "undeadWaste";        // Пустошь нежити
constexpr const char* Desecrated = "desecrated";          // Оскверненная провинция
constexpr const char* Necromancer = "necromancer";        // Некромант
constexpr const char* Lich = "lich";                      // Лич
constexpr const char* CouncilInfluence = "councilInfluence";   // Влияние совета (вместе с «Централизованной властью»)
constexpr const char* Plague = "plague";                  // Чума
constexpr const char* PlagueImmunity = "plagueImmunity";  // Временный иммунитет (к чуме)
constexpr const char* ArchWounded = "archWounded";        // Ранение в ходе исследования (археологическая группа)
constexpr const char* ArchPlague = "archPlague";          // Заражение чумой (археологическая группа)
constexpr const char* ArchValues = "archValues";          // Ценности археологии (гильдия археологов)
// Заговоры: модификаторы героев для заказных убийств, арест, модификаторы провинций после ареста заговорщиков.
constexpr const char* Vigilant = "vigilant";              // Бдительный: −25 % к успеху заказного убийства
constexpr const char* Paranoid = "paranoid";              // Параноик: −50 %
constexpr const char* Immortal = "immortal";              // Бессмертный: −100 %
constexpr const char* Careless = "careless";              // Неосмотрительный: +5 %
constexpr const char* Overconfident = "overconfident";    // Чрезмерно самоуверенный: +15 %
constexpr const char* Arrested = "arrested";              // Под арестом (как «Взят в плен», без пленителя)
constexpr const char* PlotArrests = "plotArrests";        // Аресты заговорщиков: приверженность не растёт (5 ходов)
constexpr const char* PlotExposed = "plotExposed";        // Раскрытый заговор: провинцию нельзя взять в заговор (5 ходов)
// Модификаторы уровней построек (ключ — префикс и номер уровня, levelModKey): «Жилой квартал I…V» (прирост населения
// +1…+5 %), «Гильдия строителей I…V» (слоты +1…+5), «Штаб Разведки I…V» (+10…+50 % к успеху заказных убийств),
// «Штаб Контрразведки I…III» (−10…−30 % к успеху заказных убийств против героев государства).
constexpr const char* HousingLevel = "housing";
constexpr const char* BuildersLevel = "builders";
constexpr const char* IntelLevel = "intelHq";
constexpr const char* CounterIntelLevel = "counterIntelHq";
}  // namespace mod
std::string levelModKey(const char* prefix, int level);
// Шаблоны встроенных модификаторов (id = 0) в постоянном порядке.
const std::vector<Modifier>& builtinModifiers();
const Modifier* builtinModifier(std::string_view key);
// Природа героя: «Живой», «Нежить», «Демон», «Механизм» — взаимоисключающие.
bool isNatureKey(std::string_view key);
// Модификаторы, которые редактор ставит и снимает сам (столица, совет, влияние совета, голод, ценности археологии):
// вручную не добавляются.
bool isAutoKey(std::string_view key);
// Эссенция или ресурс, которые встроенный модификатор генерирует по умолчанию (некромант, лич — эссенция смерти;
// ценности археологии — археологические сокровища): название записи справочника и количество за ход.
struct BuiltinGen {
  const char* key;
  bool essence;
  const char* name;
  double perTurn;
};
const std::vector<BuiltinGen>& builtinGens();

// Глобальные константы (Constant::key).
namespace cst {
constexpr const char* ColonizationCost = "colonizationCost";   // число: золото за колонизацию
constexpr const char* ShipLineCost = "shipLineCost";           // ресурсы: один линкор
constexpr const char* FrigateCost = "frigateCost";             // ресурсы: один фрегат
constexpr const char* GalleonCost = "galleonCost";             // ресурсы: один торговый галеон
constexpr const char* UnitRaces = "unitRaces";                 // значения: расы для отрядов
constexpr const char* CorpsesPerUnit = "corpsesPerUnit";       // число: трупов на 1 воина-нежить
constexpr const char* EnergyPerUnit = "energyPerUnit";         // число: демонической энергии на 1 воина-демона
constexpr const char* SeaMonsterCost = "seaMonsterCost";       // ресурсы и эссенции: одно морское чудовище
constexpr const char* FrigateCapacity = "frigateCapacity";     // число: вместимость фрегата (воинов)
constexpr const char* LineCapacity = "shipLineCapacity";       // число: вместимость линкора
constexpr const char* MercPerGuild = "mercPerGuild";           // число: лимит наёмников за каждую гильдию наёмников
constexpr const char* MercHire = "mercHire";                   // число: золото за найм одного наёмника (новые строки)
constexpr const char* ArchGroupPeople = "archGroupPeople";     // число: населения (трупов) на археологическую группу
constexpr const char* ArchGroupGold = "archGroupGold";         // число: золота на археологическую группу
constexpr const char* TaxNorm = "taxNorm";                     // число: налог, %, выше которого падает довольство
constexpr const char* TaxUnrest = "taxUnrest";                 // число: довольство за ход за процент налога сверх нормы
constexpr const char* FamineDeaths = "famineDeaths";           // число: % голодающих, гибнущих за ход
constexpr const char* PopGrowthBase = "popGrowthBase";         // число: естественный прирост населения, % за ход
constexpr const char* PlagueSpread = "plagueSpread";           // число: вероятность заражения соседа, %
constexpr const char* GalleonIncome = "galleonIncome";         // число: золото за ход от торгового галеона
constexpr const char* ResearchSlots = "researchSlots";         // число: одновременных исследований фракции
constexpr const char* DealBreak = "dealBreak";                 // число: отношения при расторжении сделки
constexpr const char* LandlessTurns = "landlessTurns";         // число: ходов без земель и войск до упразднения
constexpr const char* RespecTurns = "respecTurns";             // число: ходов между сменами класса героя игроком
constexpr const char* TruceCalm = "truceCalm";                 // число: ходов после перемирия без права объявить войну
}  // namespace cst
const std::vector<Constant>& builtinConstants();
const char* shipCostKey(ShipType t);
// Вместимость корабля (воинов): фрегат и линкор — константы, остальные не перевозят войска.
const char* shipCapacityKey(ShipType t);

// Базовые расы отрядов (значения константы «Расы для отрядов»).
constexpr const char* kRaceLiving = "Живой";
constexpr const char* kRaceDemonic = "Демонический";
constexpr const char* kRaceUndead = "Нежить";
constexpr const char* kRaceMechanical = "Механический";
constexpr const char* kRaceElemental = "Элементали";
constexpr const char* kRaceMercenary = "Наемники";         // наёмники: всегда верны при мятеже

// Встроенные постройки (Building::key): правила узнают их по ключу.
namespace bld {
constexpr const char* ArchGuild = "archGuild";     // «Гильдия Археологов» — культовая постройка в дереве каждого государства
constexpr const char* Port = "port";
constexpr const char* Shipyard = "shipyard";
constexpr const char* MercGuild = "mercGuild";     // «Гильдия Наемников»
constexpr const char* Market = "market";           // «Рынок»: золото за ход, продажа реликвий
constexpr const char* Infirmary = "infirmary";     // «Лечебница»
constexpr const char* PlagueLab = "plagueLab";     // «Чумная лаборатория»
constexpr const char* RelicVault = "relicVault";   // «Хранилище реликвий»
constexpr const char* Barracks = "barracks";       // «Казармы»: снижение содержания армии
constexpr const char* Mine = "mine";               // «Шахта»: добыча ресурса провинции
constexpr const char* Sawmill = "sawmill";         // «Лесопилка»
constexpr const char* Quarry = "quarry";           // «Каменоломня»
constexpr const char* AlchemyLab = "alchemyLab";   // «Алхимическая лаборатория»
constexpr const char* AlchemyConverter = "alchemyConverter";   // «Алхимический преобразователь»
constexpr const char* CorpseMill = "corpseMill";   // «Мясопилка»: только государству нежити
constexpr const char* Drainer = "drainer";         // «Иссушительница»
constexpr const char* Sprinkler = "sprinkler";     // «Окропительница»
constexpr const char* Steelworks = "steelworks";   // «Сталелитейная»
// Постройки «Доработок №1» (2026-10-22) и штабы разведки и контрразведки (уникальное дерево каждого государства).
constexpr const char* Farm = "farm";               // «Ферма»: добыча провизии, овощи, зерно, фрукты, курицы
constexpr const char* Slaughterhouse = "slaughterhouse";   // «Бойня»
constexpr const char* CultAltar = "cultAltar";     // «Алтарь культа»: демоническая энергия
constexpr const char* HolyTemple = "consecrationTemple";   // «Храм освящения»
constexpr const char* UnholyTemple = "desecrationTemple";  // «Храм осквернения»
constexpr const char* AbyssEnricher = "abyssEnricher";     // «Обогатитель бездны»
constexpr const char* NecroEnricher = "necroEnricher";     // «Некротический обогатитель»
constexpr const char* Housing = "housing";         // «Жилой квартал»: прирост населения
constexpr const char* WarWorkshop = "warWorkshop"; // «Военная мастерская»: порох
constexpr const char* EngWorkshop = "engineeringWorkshop"; // «Инженерная мастерская»
constexpr const char* CharcoalPit = "charcoalPit"; // «Углежогная яма»
constexpr const char* BuildersGuild = "buildersGuild";     // «Гильдия строителей»: слоты
constexpr const char* IntelHq = "intelHq";         // «Штаб Разведки» (уникальная культовая)
constexpr const char* CounterIntelHq = "counterIntelHq";   // «Штаб Контрразведки» (уникальная культовая)
}  // namespace bld
// Технологии, на которые опираются правила (Tech::key).
namespace tech {
constexpr const char* ArchValues = "archValues";   // «Ценности археологии»: гильдия археологов даёт сокровища
constexpr const char* MagicPowder = "magicPowder"; // «Производство магического пороха»: алхимический преобразователь
constexpr const char* Powder = "powder";           // «Производство пороха»: военная мастерская
// Ветка «Стимуляция промышленности»: технология с ключом industryN (N = 2…100) открывает множитель производства ×N.
constexpr const char* IndustryPrefix = "industry";
}  // namespace tech
// Множитель производства постройки преобразования: ×1 … ×100.
constexpr int kMaxProductionMult = 100;
// Ветка «Стимуляция промышленности»: срок технологии и цена (×N для технологии множителя ×N).
constexpr int kIndustryTechTurns = 5;
constexpr double kIndustryTechGold = 10, kIndustryTechParts = 50;
// Множитель, который открывает технология с ключом key (industryN → N); 0 — не технология ветки.
int industryMultOf(std::string_view key);
std::string industryKey(int mult);
// Группы реликвий (RelicGroup::key).
constexpr const char* kRelicArchFinds = "archFinds";   // «Археологические находки»: героям напрямую не назначаются

// Колонизация и очищение пустоши нежити: переселенцев — не меньше 500.
constexpr i64 kMinSettlers = 500;
// «Продать реликвии» (рынок): золото за штуку по редкости — археологические находки и остальные реликвии.
constexpr double kRelicPriceArch[int(Rarity::Count)] = {1, 2.5, 5, 50, 250};
constexpr double kRelicPrice[int(Rarity::Count)] = {0.1, 0.25, 1, 50, 250};
constexpr double kMaxUpkeepCut = 50;             // снижение содержания армии постройками — не больше 50 %

// Цены и лимиты новых механик.
constexpr double kNewRowUpkeep = 0.001;           // содержание новой строки армии и флота, золота за ход
constexpr i64 kPirateFlatLoss = 20;               // пираты: при меньше чем 100 галеонах в торговле — 20 галеонов
constexpr i64 kPirateFlatBelow = 100;
constexpr int kMaxCouncilSeats = 10;              // совет — не больше 10 должностей
constexpr double kCouncilInfluencePerSeat = -2;   // «Влияние совета»: −2 % времени исследования за должность
constexpr int kPlagueTurns = 4, kImmunityTurns = 6;
constexpr int kQuarantineTurns = 5;                 // «Карантин»   // «Чума» (и «Заражение чумой» группы) и «Временный иммунитет»
constexpr int kWoundTurns = 2;                    // «Ранение в ходе исследования» археологической группы
constexpr double kPlagueBuildingGrowth = 2.5;     // чума в провинции со «Зданием чумы»: прирост +2,5 %
constexpr double kHealCost = 500;                 // «Вылечить провинцию»: эссенции любого вида
constexpr double kInfectCost = 2500;              // «Заразить чумой»: эссенции чумы
constexpr double kLichCorpses = 20000, kLichDeathEssence = 5000;   // «Возвысить до Лича»
constexpr double kLichShare = 0.50;               // лич: 50 % побеждённых живых отрядов в трупы
constexpr int kMaxHeroLevel = 60;
constexpr int kMinTalentCost = 1, kMaxTalentCost = 5;
constexpr int kMaxTalentRanks = 5;              // рангов таланта — от 1 до 5
constexpr int kTalentCols = 5, kTalentRows = 50;  // дерево талантов: 5 столбцов, ярусов не больше 50
constexpr int kDefaultTierPoints = 10;           // очков на ярус дерева нового подкласса
constexpr int kMaxTierPoints = 60;                // очков на ярус дерева талантов — не больше
constexpr double kSameReligionRelation = 10;      // одна религия у государств: отношения всегда +10
// Объекты карты: пределы масштаба знака и ширины линии (единицы карты), ширина новой стены и реки.
constexpr float kMinSymbolScale = 0.2f, kMaxSymbolScale = 8.f;
constexpr float kMinShapeWidth = 0.25f, kMaxShapeWidth = 100.f;
constexpr float kWallWidth = 2.f, kRiverWidth = 4.f;

// Торговая ценность провинции: +1 за каждые 100 жителей.
constexpr double kTradePerPerson = 0.01;

// Заговоры (ТЗ «Доработки №2»).
constexpr double kPlotSponsorShare = 0.20;        // «Спонсировать заговор»: 20 % текущей ценности провинции, золото
constexpr double kPlotSponsorStep = 10;           // … и +10 % приверженности (раз в ход)
constexpr double kPlotRiseShare = 0.002;          // восстание: 1 % приверженности = 0,2 % населения
constexpr double kPlotPerUnit = 5;                // снабжение: 5 ед. ресурсов — один воин заговорщиков
constexpr double kPlotPerBallista = 50;           // механические детали: 50 — одна баллиста
constexpr double kPlotPerGolem = 5;               // запчасти механизмов: 5 — один голем
constexpr double kHitMin = 100, kHitMax = 5000, kHitPerPct = 100;           // заказное убийство: золото, 1 % за 100
constexpr double kHitMinVip = 500, kHitMaxVip = 25000, kHitPerPctVip = 500; // советник или значимый герой: 1 % за 500
constexpr double kDetectPerLevel = 10;            // контрразведка: шанс обнаружить и раскрыть — 10 % за уровень штаба
constexpr double kArrestShare = 0.02;             // арест на этапе 1: 20 % от 10 % населения
constexpr double kArrestCommitCut = 20;           // … и −20 % приверженности
constexpr int kPlotModTurns = 5;                  // «Аресты заговорщиков», «Раскрытый заговор» — 5 ходов
constexpr int kPlotStages = 5;                    // этапы раскрытия: провинции, приверженность, герои, значимые, лидер
// Снабжение вооружением: ресурсы по названиям (группы правил отрядов заговорщиков, в порядке приоритета).
// Огнестрельное — «Заговорщики-Мушкетеры», сталь — «Заговорщики-Латники», лунное серебро — «Заговорщики-Стрелки
// Лунного Серебра», железо и обсидиан — «Заговорщики-Пехотинцы»; механические детали — баллисты, запчасти
// механизмов (встроенный ресурс) — големы.
enum class PlotArms : u8 { Firearms = 0, Steel = 1, MoonSilver = 2, Iron = 3, MechParts = 4, SpareParts = 5, Count };
struct PlotArmsInfo {
  PlotArms arms;
  std::vector<const char*> resources;   // названия ресурсов (SpareParts — встроенный kResMechParts)
};
const std::vector<PlotArmsInfo>& plotArms();

}  // namespace rg::schema
