// Regnum — модель мира.
//
// World — НЕИЗМЕНЯЕМОЕ значение. Таблицы хранятся как разделяемые блоки (shared_ptr<const ...>):
// копия World стоит десятки байт, изменённые сущности копируются точечно. Поэтому:
//  - отмена/повтор — просто прежнее значение World;
//  - снимок хода — ещё одна ссылка на World (без сериализации);
//  - фоновые потоки (отрисовка тайлов) читают свой World без блокировок.
//
// Изменения выполняются только в транзакции хранилища:
//   store.transact("Переименовать провинцию", [&](Tx& tx) { tx.province(id).name = "Арден"; });
// Исключение внутри лямбды отменяет транзакцию целиком (ничего не применяется).
// UserError — понятная пользователю причина отказа (показывается как уведомление).
//
// Идентификаторы: целые > 0, отдельная последовательность для каждого вида (Meta::seq).
// Удалённые ID не переиспользуются. В файлах JSON записываются с префиксом (p12, f3, ...).
// Координаты карты — пиксели исходного изображения 8000 × 4500, ось Y вниз.
#pragma once
#include <map>
#include <unordered_map>

#include "base/base.h"
#include "base/flag.h"
#include "core/catalogs.h"   // справочники и константы
#include "core/table.h"      // Table, TableEdit
#include "core/types.h"      // Id, перечисления, Seq, findById

namespace rg {

// ================================================================ сущности
struct Node { Id id = 0; Vec2 p; };

// Дуга плоского графа провинций: полилиния a -> pts... -> b.
// pl/pr — провинция слева/справа по ходу a->b (0 — не назначено); tl/tr — суша/море по сторонам.
// Береговые дуги (Coast) берутся из базовой карты (geo::initFromCoast) и правятся в режиме «Правка карты».
struct Edge {
  Id id = 0;
  Id a = 0, b = 0;
  std::vector<Vec2> pts;  // промежуточные точки (без a и b)
  EdgeKind kind = EdgeKind::Border;
  Id pl = 0, pr = 0;
  Terrain tl = Terrain::Land, tr = Terrain::Land;
};

struct GarrisonEntry { Id row = 0; i64 count = 0; };       // строка армии владельца
struct RacePop { Id race = 0; i64 pop = 0; };
// Модификаторы с ограниченным сроком: модификатор → сколько ходов ещё действует (нет записи — бессрочно).
using ModTurns = std::map<Id, int>;
// Рабы государства одной расы: численность и довольство (−100…100).
struct SlaveGroup { Id race = 0; i64 count = 0; double contentment = 0; };
// Рабы на работах в провинции (из рабов владельца).
struct SlaveWork { Id race = 0; i64 count = 0; };
// Формирование отрядов или кораблей строки: сколько, через сколько ходов поступят в резерв и что за них отдано
// (люди — из населения, остальное — ресурсами: трупы, демоническая энергия, стоимость кораблей).
struct Formation {
  Id row = 0;
  i64 count = 0;
  int left = 0;
  i64 people = 0;
  std::map<Id, double> paid;
  std::map<Id, double> paidEss;   // эссенции элементов, отданные за формирование
};
struct Influence { Id guild = 0; double pct = 0; };          // торговое влияние гильдии, %
// Стопка реликвий в месте хранения (инвентарь героя, хранилище постройки, инвентарь государства): уникальная реликвия
// (легендарная, эпохальная) — всегда одна штука в одном месте; эпическая и ниже — сколько угодно штук в любых местах.
struct RelicStack {
  Id relic = 0;
  i64 count = 1;
  bool operator==(const RelicStack&) const = default;
};
// Штук реликвии в списке стопок (0 — нет).
inline i64 relicCount(const std::vector<RelicStack>& v, Id relic) {
  i64 n = 0;
  for (const RelicStack& s : v)
    if (s.relic == relic) n += s.count;
  return n;
}
struct ProvBuilding {
  Id building = 0;
  int level = 1;            // уровень, который построен или строится
  bool constructing = false;
  int left = 0;             // ходов до завершения
  // Строящийся уровень: что фактически уплачено при начале строительства и кем (возврат при отмене — ровно это
  // и тому же плательщику). У достроенной постройки пусто; payer 0 — плательщик неизвестен или упразднён.
  std::map<Id, double> paid;
  Id payer = 0;
  // Постройка преобразования или иссушения: «Простаивает» (idle) или «Работает»; cycle — ходов до конца идущего цикла
  // преобразования (0 — цикл не идёт: при «Работает» новый начнётся в конце хода, если хватит ресурсов).
  bool idle = false;
  int cycle = 0;
  int recipe = 0;                     // постройка преобразования: выбранный рецепт (индекс Building::recipes)
  // Постройка преобразования: множитель производства ×1…×100 — цикл берёт и даёт по рецепту mult раз (больше ×1 —
  // технологии «Стимуляция промышленности»).
  int mult = 1;
  Id race = 0;                        // постройка иссушения: раса провинции, у которой отнимается население
  std::map<Id, double> paidEss;       // эссенции, уплаченные за строящийся уровень (возврат при отмене — тому же плательщику)
  std::vector<RelicStack> relics;     // хранилище реликвий: реликвии в инвентаре постройки
  int builtLevel() const { return constructing ? level - 1 : level; }  // действующий уровень (0 — ещё нет)
};

// Археологический слот провинции: четыре слота (kArchSlots) — «Обычное», «Редкое», «Эпическое» и «Легендарное
// место». Место (site) и сокровище (chest — финальная награда исследования) назначаются при создании провинции;
// open — слот исследован (место обнаружено), stage — пройдено этапов исследования места (все этапы — место
// исследовано полностью, финальная награда получена).
struct ArchSlot {
  Id site = 0;
  bool open = false;
  int stage = 0;
  Id chest = 0;
  bool operator==(const ArchSlot&) const = default;
};

struct Province {
  Id id = 0;
  std::string name;
  Id owner = 0;             // государство
  Id lord = 0;              // персонаж
  bool sea = false;         // морская провинция — без информации и без заливки
  std::string capital;      // название столицы
  ProvSize size = ProvSize::Medium;   // тип города — по населению (rules::cityType)
  Id resource = 0;          // ресурс каталога
  double resourceAmount = 0;  // ≥ 0
  std::vector<GarrisonEntry> garrison;
  std::vector<Id> garrisonHeroes;           // герои владельца в гарнизоне
  double garrisonLoyalty = 100;             // верность гарнизона, −100…100 %
  double contentment = 0;   // -100..100
  Id culture = 0, religion = 0;
  std::vector<RacePop> races;
  std::vector<Influence> influence;   // сумма ≤ 100
  std::vector<Id> hqs;      // гильдии со штабом (≤ 5)
  double baseTrade = 0;     // базовая торговая ценность
  double localTax = 0;      // местный налог, % (может быть < 0)
  std::vector<ProvBuilding> buildings;
  std::vector<Id> modifiers;
  ModTurns modTurns;        // срок действия модификаторов (нет записи — бессрочно)
  bool occupied = false;
  Id occupier = 0;
  std::vector<GarrisonEntry> occGarrison;   // оккупационный гарнизон: строки армии оккупанта
  int occIdle = 0;          // ходов подряд оккупант без гарнизона и войск в провинции (5 — оккупация снимается)
  std::vector<SlaveWork> slaves;            // рабы владельца на работах (не больше 10 % населения)
  std::array<ArchSlot, kArchSlots> arch{};  // археологические места
  Id hiddenRelic = 0;       // спрятанная реликвия (видна только в режиме правки археологии)
  Id hiddenBy = 0;          // государство, которое её спрятало (только оно может откопать)
  int buildTurn = 0;        // ход, в который в провинции последний раз начато строительство (правило игрока)
  std::string notes;
  std::string entity;       // ID карточки кампании (необязательно)
};

// race — раса отряда из глобальной константы «Расы для отрядов» (пусто — по виду государства и типу отряда).
// Цена найма юнита: люди (кроме зверей, чудовищ, механизмов и элементалей), ключевой ресурс (кавалерия — из
// «Ездовых наземных», воздушная кавалерия — из «Ездовых летающих», звери — из «Зверей» кроме «Чудовищ», чудовища —
// из «Чудовищ», механизмы — «Запчасти механизмов»), дополнительные ресурсы и эссенции элементов. Элементали
// нанимаются только за эссенции и содержатся эссенциями (essUpkeep) вместо золота. special — особый отряд
// справочника: тип, раса и цены строки повторяют его запись (rules::setSpecial).
struct ArmyRow {
  Id id = 0;
  std::string name;
  UnitType type = UnitType::LightInf;
  i64 total = 0;
  double upkeep = 0;
  std::string race;
  Id keyRes = 0;                   // ключевой ресурс юнита (0 — не задан)
  double keyPer = 1;               // ключевого ресурса на юнит: 1 (механизмы — не меньше 1)
  std::map<Id, double> extra;      // дополнительные ресурсы на юнит (≥ 0)
  std::map<Id, double> essence;    // эссенции элементов на юнит при найме (≥ 0)
  std::map<Id, double> essUpkeep;  // элементали: эссенции на юнит в ход (содержание)
  Id special = 0;                  // особый отряд справочника (0 — обычный отряд)
  // Наёмники: найм и содержание — только золотом (hire — золота за юнит при найме),
  // без населения и ресурсов при любом виде государства; раса «Наемники»; при мятеже всегда верны.
  bool merc = false;
  double hire = 0;
};
// keyRes — морское чудовище: ресурс подгруппы «Морские чудовища» (1 на судно).
struct FleetRow { Id id = 0; std::string name; ShipType type = ShipType::Frigate; i64 total = 0; double upkeep = 0; Id keyRes = 0; };
struct CouncilSeat { Id id = 0; std::string position; Id character = 0; };
// Изучение общей технологии фракцией (каждое государство изучает общее дерево отдельно).
struct TechProgress {
  bool studied = false, research = false;
  int progress = 0;                // пройдено ходов исследования
  std::map<Id, double> paid;       // уплачено за идущее исследование (стоимость технологии; возврат при остановке)
  bool operator==(const TechProgress&) const = default;
};
// Археологическая группа государства: опыт 0…1000 задаёт уровень 1…5, а уровень — базовые
// содержание, шанс успеха и живучесть. busy — ход, в который группа уже участвовала в исследовании (одно за ход).
struct ArchGroup {
  Id id = 0;
  std::string name;
  int exp = 0;
  std::vector<Id> modifiers;       // модификаторы археологических групп (ModKind::ArchGroup)
  ModTurns modTurns;
  int busy = 0;
  bool danger = false;             // смертельная опасность: ждёт решения о «божественном вмешательстве» (resolveArchDanger)
};

struct Faction {
  Id id = 0;
  FactionKind kind = FactionKind::State;
  std::string name;
  Color color = Color::hex(0x7a4fd6);
  Flag flag;
  Id culture = 0, government = 0, religion = 0;
  Id ruler = 0;                  // персонаж
  std::string rulerTitle;
  std::vector<CouncilSeat> council;
  Id capital = 0;                // провинция (государство)
  std::vector<ArmyRow> army;
  std::vector<FleetRow> fleet;
  std::map<Id, double> res;      // запасы ресурсов; res[kGold] — казна
  std::vector<Id> modifiers;
  ModTurns modTurns;
  double tax = 10;               // налог государства, % (≥ 0)
  Id homeState = 0;              // гильдия: государство расположения
  bool stateGuild = false;       // гильдия: государственная
  StateKind stateKind = StateKind::Living;   // вид государства
  bool mainState = false;        // основное игровое государство (итоги хода — на первой странице)
  Id suzerain = 0;               // государство-сюзерен (вассалитет)
  Id rebelOf = 0;                // мятежное государство: от кого откололось
  std::vector<SlaveGroup> slaves;           // рабы по расам
  std::vector<Formation> forming;           // формирование отрядов и кораблей (2 хода до резерва)
  std::vector<GarrisonEntry> tradeFleet;    // корабли из резерва, участвующие в торговле: строка флота → число
  double pirateRisk = 0;         // вероятность нападения пиратов (%), сгенерированная в конце прошлого хода
  std::map<Id, double> ess;      // запасы эссенций элементов (содержание элементалей может увести в долг)
  double provisionDebt = 0;      // недостача провизии: «Голод», пока больше нуля
  std::map<Id, TechProgress> techs;   // общие технологии (дерево без фракции): изучение этой фракцией
  std::vector<ArchGroup> archGroups;  // археологические группы
  std::vector<RelicStack> relics;     // инвентарь государства: полученные реликвии (находки, раскопки), вне героев и построек
  int readyTurn = 0;             // игрок отметил готовность завершить этот ход (готов, пока readyTurn == номер хода)
  int detectTurn = 0;            // контрразведка: ход последнего «Обнаружить заговоры» (игрок — раз в ход)
  std::vector<RacePop> prison;   // «Тюрьмы государства»: арестованное население по расам
  bool conspiracy = false;       // государство заговора («Заговор …»): образовано осуществлённым заговором
  std::string notes;
  std::string entity;

  bool isState() const { return kind == FactionKind::State; }
  bool isGuild() const { return kind == FactionKind::Guild; }
  bool isWild() const { return kind == FactionKind::Wild; }
  const ArchGroup* archGroup(Id g) const { return findById(archGroups, g); }
  double treasury() const { auto it = res.find(kGold); return it == res.end() ? 0 : it->second; }
  double stock(Id r) const { auto it = res.find(r); return it == res.end() ? 0 : it->second; }
  double essence(Id e) const { auto it = ess.find(e); return it == ess.end() ? 0 : it->second; }
  const ArmyRow* armyRow(Id row) const { return findById(army, row); }
  const FleetRow* fleetRow(Id row) const { return findById(fleet, row); }
};

struct Character {
  Id id = 0;
  std::string name, title;
  Id faction = 0;
  bool hero = false;             // значимый герой фракции
  double upkeep = 0;             // содержание за ход (расход «специалисты»)
  std::string portrait;          // байты PNG/JPEG или пусто
  std::vector<Id> modifiers;     // модификаторы героя («Живой», «Некромант», «Мертв», «Взят в плен»…)
  ModTurns modTurns;
  Id captor = 0;                 // взят в плен: пленившее государство
  Id burial = 0;                 // погиб: место захоронения (провинция)
  std::vector<RelicStack> inventory;   // инвентарь героя: реликвии (уникальная — не больше чем у одного персонажа)
  int level = 1;                 // уровень героя 1…60: очков талантов — по одному за уровень
  Id heroClass = 0;              // класс героя (справочник классов); 0 — без класса
  Id subclass = 0;               // подкласс выбранного класса (его дерево талантов); 0 — не выбран
  // Изученные ранги талантов дерева подкласса в порядке изучения: талант повторяется столько раз, сколько у него
  // изучено рангов (у каждого героя — своё).
  std::vector<Id> talents;
  Id jailer = 0;                 // «Под арестом»: государство, в чьей тюрьме герой
  std::string notes, entity;
};

struct Modifier {
  Id id = 0;
  std::string name;
  std::string icon = "sparkles";
  Color color = Color::hex(0x8c7ae6);
  std::string desc;
  std::array<double, kFxCount> fx{};  // значения эффектов
  u32 fxMask = 0;                     // какие эффекты заданы (бит = int(Fx))
  std::vector<Id> targets;            // цели эффекта «дипломатия»
  std::string key;                    // встроенный модификатор (schema::kMod*); пусто — свой
  ModKind kind = ModKind::Any;        // где применяется
  int duration = 0;                   // ходов действия при установке (0 — бессрочно)
  // Генерация за ход: эссенции и ресурсы, которые модификатор даёт каждый ход — владельцу
  // провинции (модификатор провинции и её построек), государству (его модификаторы, технологии, автоматические),
  // фракции героя (модификатор героя).
  std::map<Id, double> essGen;
  std::map<Id, double> resGen;
  bool has(Fx f) const { return (fxMask >> int(f)) & 1u; }
  double get(Fx f) const { return has(f) ? fx[int(f)] : 0.0; }
};

struct BuildingLevel {
  int turns = 1;
  std::map<Id, double> cost;          // ресурс -> количество
  std::vector<Id> modifiers;
  std::string desc;
  std::map<Id, double> produce;       // ресурсы, которые достроенный уровень даёт владельцу каждый ход
  std::map<Id, double> essence;       // постройка генерации эссенции: эссенции владельцу каждый ход
  std::map<Id, double> essCost;       // цена уровня в эссенциях (вместе с cost)
  u32 ships = 0;                      // верфь: типы кораблей, которые открывает уровень (бит = ShipType)
  // Производство при ресурсе провинции: если ресурс провинции — ключ, вместо produce этого ресурса — значение
  // (лесопилка: 20 древесины, в провинции с ресурсом «Древесина» — 100).
  std::map<Id, double> produceOnRes;
  double extractPct = 0;              // постройка добычи: добыча ресурса провинции +N %
  i64 drainPop = 0;                   // постройка иссушения: жителей выбранной расы в ход
  std::map<Id, double> drainEss;      // постройка иссушения: эссенции за drainPop жителей (меньше жителей — меньше)
  double upkeepCut = 0;               // снижение содержания армии: % (сумма по государству — не больше 50 %)
};
struct BuildingReq { Id building = 0; int level = 1; };
// Требование «на государство» (соборы): чтобы построить в государстве (n + 1)-ю такую постройку, в его провинциях должно
// быть достроено не меньше per × (n + 1) построек building.
struct StateReq {
  Id building = 0;
  int per = 4;
  bool operator==(const StateReq&) const = default;
};
struct ResAmount {
  Id res = 0;
  double amount = 0;
  bool operator==(const ResAmount&) const = default;
};
// Рецепт постройки преобразования: до трёх разных ресурсов и до трёх эссенций на входе → до трёх разных ресурсов на
// выходе за turns ходов (один цикл); name — название рецепта («Сталь»), пусто — по первому ресурсу на выходе.
struct Recipe {
  std::string name;
  std::vector<ResAmount> in;
  std::vector<ResAmount> essIn;       // эссенции на входе (res — ID эссенции)
  std::vector<ResAmount> out;         // ресурсы на выходе (до трёх)
  int turns = 1;
  bool operator==(const Recipe&) const = default;
};
struct Building {
  Id id = 0;
  Id owner = 0;                       // 0 — общее дерево; иначе уникальная постройка фракции
  std::string name;
  std::string icon = "building";
  BuildingCat cat = BuildingCat::Economic;
  std::string desc;
  std::vector<BuildingReq> requires_;
  std::vector<Id> techs;              // технологии, без которых постройку не начать (общие; у уникальной — и своего дерева)
  std::vector<BuildingLevel> levels{BuildingLevel{}};
  Vec2 pos;                           // положение на схеме дерева
  // Дополнительные возможности постройки.
  bool convert = false;               // постройка преобразования (рецепты; в провинции выбирается один)
  std::vector<Recipe> recipes;
  bool essenceGen = false;            // постройка генерации эссенции (BuildingLevel::essence)
  bool specialAccess = false;         // постройка доступа к особым отрядам (specials)
  std::vector<Id> specials;           // особые отряды, которые государство может нанимать, пока постройка достроена
  bool relicStore = false;            // хранилище реликвий (ProvBuilding::relics)
  bool healing = false;               // здание целительства: «Вылечить провинцию»
  bool plague = false;                // здание чумы: чума в провинции даёт прирост, «Заразить чумой»
  bool shipyard = false;              // верфь: уровни открывают типы кораблей (BuildingLevel::ships)
  bool mercenary = false;             // гильдия наёмников: лимит наёмников государства
  bool coastal = false;               // только в приморской провинции (граничит с морем)
  bool market = false;                // рынок: «Продать реликвии» хранилищ реликвий государства за золото
  bool drain = false;                 // постройка иссушения: население выбранной расы → эссенции (BuildingLevel::drain*)
  bool upkeepCut = false;             // снижение содержания армии государства (BuildingLevel::upkeepCut)
  // Постройка добычи: требует ресурс провинции из групп extractGroups (с подгруппами), кроме extractExclude;
  // уровни увеличивают его добычу (BuildingLevel::extractPct).
  bool extract = false;
  std::vector<Id> extractGroups, extractExclude;
  // Добыча не обязательна (ферма): постройку можно ставить при любом ресурсе провинции, а уровни увеличивают добычу,
  // только если ресурс провинции подходит.
  bool extractOptional = false;
  u32 stateKinds = 0;                 // только для видов государства (бит = StateKind); 0 — для всех
  std::vector<StateReq> stateReqs;    // требования «на государство»
  std::string key;                    // встроенная постройка (schema::bld): правила узнают её по ключу
};

struct Tech {
  Id id = 0;
  Id faction = 0;                     // дерево технологий фракции; 0 — общее дерево (изучение — Faction::techs)
  std::string name, desc;
  int turns = 1;
  std::vector<Id> prereqs;            // общая технология зависит только от общих; уникальная — от своих и общих
  std::vector<Id> modifiers;
  bool studied = false;               // у общей технологии не используются (изучение — у каждой фракции своё)
  bool research = false;              // исследуется сейчас
  int progress = 0;                   // пройдено ходов
  Vec2 pos;                           // положение на схеме дерева
  std::map<Id, double> cost;          // стоимость исследования: ресурсы при начале (возврат при остановке)
  std::map<Id, double> paid;          // уплачено за идущее исследование (технология фракции; у общей — Faction::techs)
  std::vector<std::string> needKeys;  // постройки (ключ schema::bld), которые должны быть достроены в государстве
  std::string key;                    // технология, на которую опираются правила (schema::tech)
  bool common() const { return faction == 0; }
};

struct ArmyUnit { Id row = 0; i64 count = 0; };
struct ArmyGroup {                    // отряды одной фракции в объекте (союзное войско — несколько групп)
  Id faction = 0;
  std::vector<ArmyUnit> units;
  std::vector<Id> heroes;
};
struct Army {
  Id id = 0;
  ArmyKind kind = ArmyKind::Army;
  std::string name;
  Vec2 pos;
  std::vector<ArmyGroup> groups;
  Id commander = 0;                   // главный полководец/флотоводец
  double loyalty = 100;               // верность войска, −100…100 %
  Id carrier = 0;                     // войско на борту флота (не стоит на карте; положение — у флота)
  Id cargo = 0;                       // флот: войско на борту (одно)
  int acted = 0;                      // ход, в который войско (флот) истратило очко действий (правила игрока)
  std::vector<Id> modifiers;          // модификаторы войска («Армия нежити», «Патриотизм»…)
  ModTurns modTurns;
  bool isFleet() const { return kind == ArmyKind::Fleet; }
  bool allied() const { return groups.size() > 1; }
  Id leader() const { return groups.empty() ? 0 : groups[0].faction; }
};

// Знак карты: гора (рисунок v: 0 или 1), крупная гора, замок, башня. Точка привязки p — середина основания значка,
// s — масштаб относительно обычного размера. Порядок отрисовки — по z (больше — выше), при равных z — по ID.
struct MapSymbol {
  Id id = 0;
  SymbolKind kind = SymbolKind::Mountain;
  Vec2 p;
  float s = 1;
  u8 v = 0;
  double z = 0;
};

// Фигура карты. Water — озеро или река контуром (pts — внешний контур, holes — острова внутри), Islet — островок
// (контур; суша рисунка, в граф провинций не входит), Wall — стена (ломаная шириной w, dash > 0 — пунктир),
// River — река линией (ломаная шириной w цветом воды).
struct MapShape {
  Id id = 0;
  ShapeKind kind = ShapeKind::Water;
  std::vector<Vec2> pts;
  std::vector<std::vector<Vec2>> holes;
  float w = 0;
  float dash = 0;
  bool closed() const { return kind == ShapeKind::Water || kind == ShapeKind::Islet; }
};

struct Route {
  Id id = 0;
  std::string name;
  Id guild = 0;                       // гильдия-владелец (необязательно)
  std::vector<Vec2> pts;
  std::optional<Color> color;
};

struct DealItem {
  DealSide from = DealSide::A;
  Id res = kGold;
  double amount = 0;
  DealMode mode = DealMode::Once;
  int turns = 1;
  int left = 0;
  DealItemKind kind = DealItemKind::Resource;   // провинция и пленный герой — разово, ref — их ID
  Id ref = 0;
};
struct Deal {
  Id id = 0;
  DealKind kind = DealKind::Trade;
  Id a = 0, b = 0;                    // стороны (для дани: a — получатель, b — плательщик)
  std::vector<DealItem> items;
  int turn = 1;                       // ход заключения
  DealStatus status = DealStatus::Active;
  std::string note;
};

struct LogEntry {
  Id id = 0;
  int turn = 1;
  LogKind kind = LogKind::Note;
  std::string text;
  Id province = 0, army = 0;
  std::vector<Id> factions;
  std::string at;                     // реальное время записи (ISO)
};

// Заговор (ТЗ «Доработки №2»): против государства target — провинции с приверженностью заговору и снабжением,
// герои-участники, лидер, приглашённые игроком герои (ждут решения администратора), угрозы (отказавшиеся) и заказные
// убийства угроз. Сведения контрразведки цели: обнаружен (found), раскрыто этапов (stage: 1 — провинции,
// 2 — приверженность, 3 — герои без «Значимого героя», 4 — значимые герои, 5 — лидер), ход последней попытки
// раскрытия, «частично раскрыт» (alarm — неудача раскрытия этапа 3 и дальше; заговорщики видят расследование).
struct PlotProvince {
  Id province = 0;
  double commitment = 0;              // «Приверженность заговору», 0…100 %
  int sponsored = 0;                  // ход последнего «Спонсировать заговор» (раз в ход)
  std::map<Id, double> supplies;      // «Снабдить вооружением»: ресурсы, отданные заговорщикам провинции
  bool operator==(const PlotProvince&) const = default;
};
struct PlotHit {                      // заказное убийство (исполняется в конце хода turn)
  Id character = 0;
  double gold = 0;                    // отдано на операцию (золото)
  int turn = 0;
  bool guarded = false;               // «Защитить от убийства» (администратор): вероятность 0 %
  bool operator==(const PlotHit&) const = default;
};
struct Plot {
  Id id = 0;
  PlotKind kind = PlotKind::External;
  Id owner = 0;                       // заказчик — государство (внутренний заговор: 0)
  Id target = 0;                      // государство, против которого заговор
  std::string name;                   // пусто — «Заговор против …»
  std::vector<PlotProvince> provinces;
  std::vector<Id> heroes;             // участники (герои государства target)
  std::vector<Id> invited;            // приглашены игроком — ждут решения администратора
  std::vector<Id> threats;            // отказались — «Угрозы заговору»
  Id leader = 0;                      // лидер заговора (один из участников)
  std::vector<PlotHit> hits;          // заказные убийства угроз
  int created = 1;                    // ход создания
  bool found = false;
  int stage = 0;
  int revealTurn = 0;
  bool alarm = false;
  bool external() const { return kind == PlotKind::External; }
  const PlotProvince* province(Id p) const {
    for (const PlotProvince& x : provinces)
      if (x.province == p) return &x;
    return nullptr;
  }
  const PlotHit* hit(Id ch) const {
    for (const PlotHit& x : hits)
      if (x.character == ch) return &x;
    return nullptr;
  }
};

struct Relation {
  double v = 0;                       // -100..100
  RelStatus s = RelStatus::Unknown;
  bool operator==(const Relation&) const = default;
};
using RelMap = std::map<u64, Relation>;  // ключ relKey(a, b)
inline u64 relKey(Id a, Id b) { if (a > b) std::swap(a, b); return (u64(a) << 32) | b; }

struct Settings {
  float fillOpacity = 0.5f;
  bool labelStates = true, labelProvinces = true, labelArmies = true;
  bool showArmies = true;             // войска и флот на карте («Скрыть войска» — false)
  int figureSize = 34;                // размер значка войска и флота на экране, точки (постоянный при любом масштабе)
  OccupiedIncome occupiedIncome = OccupiedIncome::Owner;
  bool rebellionRoll = false;
  bool autosaveFolder = true;
  int autosaveSec = 60;
  // «Свободное редактирование флотов»: новый флот ставится в любое место карты; иначе — из вкладки «Флот» государства
  // у верфи приморской провинции.
  bool freeFleets = false;
};

struct Meta {
  std::string name = "Новый мир";
  std::string createdAt, updatedAt;
  int turn = 1;
  std::array<u32, kSeqCount> seq{};
  std::string basemap = "wmm-expanded-v1";
  // Знаки и фигуры карты хранятся в мире (data/map.json) и правятся в редакторе. false — мир показывает объекты
  // базовой карты (assets/basemap/map.json); первая правка карты переносит их в мир.
  bool mapObjects = false;
  // Версия базовых записей справочников (core/content.h): мир прежней версии дополняется при чтении один раз.
  int content = 0;
  // Счётчик случайных событий мира (археология): зерно броска — счётчик и ID участников; событие увеличивает счётчик,
  // поэтому отмена возвращает и его.
  u32 rng = 0;
  std::string notes;
};

// ================================================================ мир
// Таблицы сущностей мира — одна строка на таблицу: тип записи, поле World (и доступ w.province(id)), правка в Tx
// (tx.province(id)), суффикс удаления (tx.eraseProvince(id)), бит TableBit, последовательность ID, подпись для ошибок.
// Порядок — порядок разбора при чтении. Новая таблица — строка здесь, бит TableBit, файл и формат в core/io.
#define RG_WORLD_TABLES(X)                                                                  \
  X(Node, nodes, node, Node, TB_NODES, Seq::Node, "Узел")                                   \
  X(Edge, edges, edge, Edge, TB_EDGES, Seq::Edge, "Граница")                                \
  X(Province, provinces, province, Province, TB_PROVINCES, Seq::Province, "Провинция")      \
  X(Faction, factions, faction, Faction, TB_FACTIONS, Seq::Faction, "Фракция")              \
  X(Character, characters, character, Character, TB_CHARACTERS, Seq::Character, "Персонаж") \
  X(Modifier, modifiers, modifier, Modifier, TB_MODIFIERS, Seq::Modifier, "Модификатор")    \
  X(Building, buildings, building, Building, TB_BUILDINGS, Seq::Building, "Постройка")      \
  X(Tech, techs, tech, Tech, TB_TECHS, Seq::Tech, "Технология")                             \
  X(Army, armies, army, Army, TB_ARMIES, Seq::Army, "Войско")                               \
  X(Route, routes, route, Route, TB_ROUTES, Seq::Route, "Маршрут")                          \
  X(Deal, deals, deal, Deal, TB_DEALS, Seq::Deal, "Сделка")                                 \
  X(LogEntry, log, logEntry, Log, TB_LOG, Seq::Log, "Запись хроники")                       \
  X(MapSymbol, symbols, symbol, Symbol, TB_SYMBOLS, Seq::Symbol, "Знак карты")              \
  X(MapShape, shapes, shape, Shape, TB_SHAPES, Seq::Shape, "Фигура карты")                \
  X(Plot, plots, plot, Plot, TB_PLOTS, Seq::Plot, "Заговор")

enum TableBit : u32 {
  TB_META = 1u << 0, TB_SETTINGS = 1u << 1, TB_CATALOGS = 1u << 2, TB_NODES = 1u << 3, TB_EDGES = 1u << 4,
  TB_PROVINCES = 1u << 5, TB_FACTIONS = 1u << 6, TB_CHARACTERS = 1u << 7, TB_RELATIONS = 1u << 8,
  TB_MODIFIERS = 1u << 9, TB_BUILDINGS = 1u << 10, TB_TECHS = 1u << 11, TB_ARMIES = 1u << 12,
  TB_ROUTES = 1u << 13, TB_DEALS = 1u << 14, TB_LOG = 1u << 15, TB_SYMBOLS = 1u << 16, TB_SHAPES = 1u << 17,
  TB_CONSTANTS = 1u << 18, TB_PLOTS = 1u << 19,
  TB_GEO = TB_NODES | TB_EDGES, TB_MAPART = TB_SYMBOLS | TB_SHAPES, TB_ALL = 0xFFFFFu
};

struct World {
  std::shared_ptr<const Meta> meta = std::make_shared<Meta>();
  std::shared_ptr<const Settings> settings = std::make_shared<Settings>();
  std::shared_ptr<const Catalogs> catalogs = std::make_shared<Catalogs>();
  std::shared_ptr<const Constants> constants = std::make_shared<Constants>();
  std::shared_ptr<const RelMap> relations = std::make_shared<RelMap>();
  // Таблицы сущностей (RG_WORLD_TABLES): Table<Province> provinces; const Province* province(Id) const; …
#define RG_WORLD_TABLE(T, field, name, ...) \
  Table<T> field;                           \
  const T* name(Id id) const { return field.get(id); }
  RG_WORLD_TABLES(RG_WORLD_TABLE)
#undef RG_WORLD_TABLE

  int turn() const { return meta->turn; }
  // Объекты карты хранятся в мире (иначе показываются объекты базовой карты).
  bool ownMapObjects() const { return meta->mapObjects || !symbols.empty() || !shapes.empty(); }
  Relation relation(Id a, Id b) const;                 // по умолчанию: 0, «незнакомы»; a == b — союз 100
  const CatalogItem* resource(Id id) const { return Catalogs::find(catalogs->resources, id); }
  const CatalogItem* essence(Id id) const { return Catalogs::find(catalogs->essences, id); }
  const Relic* relic(Id id) const { return catalogs->relic(id); }
  const HeroClass* heroClass(Id id) const { return catalogs->heroClass(id); }
  const SpecialUnit* special(Id id) const { return catalogs->special(id); }
  std::string factionName(Id id) const;                // «—» если нет
  std::string provinceName(Id id) const;
  std::string characterName(Id id) const;

  // Битовая маска таблиц, отличающихся между двумя версиями мира.
  static u32 diff(const World& a, const World& b);
};

// Новый мир с настройками и каталогами по умолчанию (без геометрии провинций).
World newWorld(const std::string& name);

// ================================================================ транзакция
class Tx {
 public:
  explicit Tx(const World& base);
  Tx(const Tx&) = delete;
  Tx& operator=(const Tx&) = delete;

  const World& w() const { return w_; }   // текущее состояние внутри транзакции
  u32 touched() const { return touched_; }

  Meta& meta();
  Settings& settings();
  Catalogs& catalogs();
  Constants& constants();
  RelMap& relations();
  Id nextId(Seq s);                         // выдать следующий ID последовательности

  // Таблицы сущностей (RG_WORLD_TABLES), например провинции:
  //   Province& province(Id id) — изменение (копия при первом обращении); нет такой сущности — UserError;
  //   Province& add(Province v) — добавление: ID выдаётся автоматически (поле id заполняется), если v.id == 0;
  //   void eraseProvince(Id id) — удаление записи без очистки ссылок (ссылки чистит rules::entities).
#define RG_TX_TABLE_API(T, field, name, Name, ...) \
  T& name(Id id);                                  \
  T& add(T v);                                     \
  void erase##Name(Id id);
  RG_WORLD_TABLES(RG_TX_TABLE_API)
#undef RG_TX_TABLE_API

  // Отношения пары (симметричны).
  void setRelation(Id a, Id b, Relation r);

  // Заменить мир целиком (возврат к снимку хода) — отменяемо, как любая транзакция.
  // Правки, сделанные в этой транзакции до вызова, отбрасываются; после вызова можно продолжать менять мир.
  void replaceWorld(const World& w);

  // Итог транзакции (вызывает Store).
  World finish() &&;

 private:
  World w_;
  u32 touched_ = 0;
  bool metaOwned_ = false, settingsOwned_ = false, catalogsOwned_ = false, relOwned_ = false, constantsOwned_ = false;
  std::shared_ptr<Meta> meta_;
  std::shared_ptr<Settings> settings_;
  std::shared_ptr<Catalogs> catalogs_;
  std::shared_ptr<Constants> constants_;
  std::shared_ptr<RelMap> rel_;
#define RG_TX_TABLE_EDIT(T, field, ...) TableEdit<T> field##_;
  RG_WORLD_TABLES(RG_TX_TABLE_EDIT)
#undef RG_TX_TABLE_EDIT
};

}  // namespace rg

#include "core/store.h"
