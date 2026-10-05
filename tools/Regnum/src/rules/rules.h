// Regnum — правила мира: расчёты (от const World&) и действия (через Tx&).
// Формулы и толкование ТЗ — docs/RULES.md. Нарушение правила — rg::fail("причина по-русски").
// Действия, являющиеся событиями мира, записывают хронику (addLog).
#pragma once
#include <map>
#include <unordered_map>

#include "core/schema.h"
#include "core/world.h"

namespace rg::rules {

// ================================================================ модификаторы
struct EffectSource {
  // Auto — модификатор, который редактор ставит сам (столица, совет, голод, должности); key — его ключ (modifier
  // может быть 0, если записи в мире ещё нет — тогда действует шаблон schema::builtinModifiers()).
  enum Kind : u8 { Province, Faction, Tech, Building, Guild, Army, Auto } kind = Province;
  Id id = 0;         // провинция / фракция / технология / постройка / гильдия / войско
  Id modifier = 0;
  std::string key;
};
struct Effects {
  std::array<double, kFxCount> v{};
  std::map<Id, double> diplomacy;     // цель -> изменение отношений за ход
  std::vector<EffectSource> sources;  // откуда взялись (для подсказок)
  double operator[](Fx f) const { return v[int(f)]; }
};
Effects provinceEffects(const World& w, Id province);   // локальные эффекты
Effects factionEffects(const World& w, Id faction);     // глобальные эффекты
// Эффекты войска: эффекты войск его модификаторов и государства-лидера (верность за ход).
Effects armyEffects(const World& w, Id army);
// Изменение верности войска за ход (с «Непреклонным лоялистом» и «Армией нежити»).
double loyaltyDelta(const World& w, Id army);

// ================================================================ встроенные записи
// Модификатор мира с ключом (ТЗ «Модификаторы»): запись мира или, пока её нет, шаблон schema::builtinModifiers().
const Modifier* builtinMod(const World& w, std::string_view key);
Id builtinModId(const World& w, std::string_view key);             // 0 — записи в мире ещё нет
Id ensureBuiltinMod(Tx& tx, std::string_view key);                 // запись мира (создаётся по шаблону при первом обращении)
// Встроенный ресурс (провизия, трупы, демоническая энергия): создаётся при первом обращении; «gold» — rs1.
Id resourceId(const World& w, std::string_view key);               // 0 — ресурса ещё нет
Id ensureResource(Tx& tx, std::string_view key);
// Глобальная константа мира или, если её не правили, встроенная по умолчанию (неизвестный ключ — пустая).
const Constant& constantOf(const World& w, std::string_view key);
Constant& ensureConstant(Tx& tx, std::string_view key);            // запись мира (встроенная — по умолчанию)
std::string addConstant(Tx& tx, ConstType type, const std::string& name);   // своя константа; возвращает ключ
void removeConstant(Tx& tx, std::string_view key);                 // встроенную удалить нельзя
// Расы для отрядов (константа «Расы для отрядов») и раса строки армии (пусто — по виду государства и типу).
std::vector<std::string> unitRaces(const World& w);
std::string defaultUnitRace(StateKind kind, UnitType type);
std::string unitRace(const World& w, Id faction, const ArmyRow& row);
StateKind stateKindOf(const World& w, Id faction);
bool hasModKey(const World& w, const std::vector<Id>& mods, std::string_view key);
bool characterHas(const World& w, Id character, std::string_view key);
bool armyHas(const World& w, Id army, std::string_view key);
bool provinceHas(const World& w, Id province, std::string_view key);
// Герой доступен для назначений: не «Мертв» и не «Взят в плен» (ТЗ «Модификаторы», 1.6 и 1.12).
bool heroAvailable(const World& w, Id character);

// ================================================================ модификаторы сущностей
enum class ModTarget : u8 { Province, Faction, Army, Character };
// Новый список модификаторов сущности. Добавленные — по правилам (срок по умолчанию, взаимоисключения, «Мертв» и
// «Взят в плен» снимают со всех назначений, «Изуверское наслаждение» — только армиям демонов и безжалостным…),
// снятые теряют срок.
void setModifiers(Tx& tx, ModTarget t, Id target, const std::vector<Id>& mods);
// Добавить один модификатор; turns < 0 — срок модификатора по умолчанию, 0 — бессрочно.
void addModifier(Tx& tx, ModTarget t, Id target, Id modifier, int turns = -1);
void dropModifier(Tx& tx, ModTarget t, Id target, Id modifier);
void setModTurns(Tx& tx, ModTarget t, Id target, Id modifier, int turns);   // 0 — бессрочно
// Модификаторы, которые редактор ставит сам: столица (провинция); совет, голод и должности (государство).
struct AutoMod {
  std::string key;          // ключ встроенного модификатора (пусто — модификатор должности)
  Id modifier = 0;          // запись мира (0 — действует шаблон)
  const Modifier* m = nullptr;
  std::string why;          // почему действует
};
std::vector<AutoMod> autoModifiers(const World& w, Id faction);
std::vector<AutoMod> autoProvinceModifiers(const World& w, Id province);
// Число назначений в совет (персонажи, доступные для назначений).
int councilAssigned(const World& w, Id faction);

// ================================================================ расчёты
struct GuildShare { Id guild = 0; double pct = 0; bool hq = false; double gross = 0, tax = 0, net = 0; };

struct ProvinceCalc {
  Id id = 0;
  bool sea = false;
  Id owner = 0;
  Id recipient = 0;                   // кто получает доход (оккупация по настройкам)
  int slotsSize = 0, slotsCity = 0, slotsMods = 0, slots = 0, slotsUsed = 0;
  double tradeBase = 0, tradeValue = 0;
  int routes = 0;
  double production = 0;              // добыча ресурса за ход
  double rebellion = 0;               // 0..100
  double buildCostFactor = 1;
  double taxState = 0, taxLocal = 0, taxTotal = 0;   // %
  i64 population = 0;
  std::vector<std::pair<Id, i64>> races;             // раса -> численность (по убыванию)
  std::vector<GuildShare> guilds;
  double provinceTax = 0, guildTax = 0;              // в казну получателя
  bool tradeBlocked = false;          // пустошь нежити / осквернённая: получатель не того вида — дохода с ценности нет
  i64 slavesAtWork = 0;               // рабы на работах
  double slaveIncome = 0;             // доход владельца с рабов на работах
  double energy = 0;                  // осквернённая провинция: демоническая энергия владельцу за ход
  std::map<Id, double> produce;       // ресурсы от построек владельцу за ход
  Effects fx;
};

struct RowCalc {
  Id row = 0;
  i64 total = 0, field = 0, garrison = 0, reserve = 0;   // field включает гарнизоны (и корабли в торговле)
  i64 trade = 0, occupation = 0;      // корабли в торговле; отряды в оккупационных гарнизонах
  i64 forming = 0;                    // формируется (поступит в резерв)
  double upkeepEach = 0, upkeepTotal = 0;
};

// consumption — расход (провизия государства живых); net = production + tradeIn − tradeOut − consumption.
struct ResourceFlow { double stock = 0, production = 0, tradeIn = 0, tradeOut = 0, consumption = 0, net = 0; };

struct FactionCalc {
  Id id = 0;
  std::vector<Id> provinces;          // государство: владения; гильдия: провинции штабов
  i64 population = 0;
  std::vector<std::pair<Id, i64>> races;               // по провинциям (ТЗ 1.b.iv — автоматически)
  double incProvinces = 0, incGuildTax = 0, incGuilds = 0, incTrade = 0, incTribute = 0;
  double incSlaves = 0, incTradeFleet = 0, incRoutes = 0;   // рабы на работах, флот в торговле, маршруты гильдии
  double incGross = 0, incomePct = 0, incTotal = 0;
  double expArmy = 0, expFleet = 0, expSpecialists = 0, expTrade = 0, expTribute = 0, expSlaves = 0, expTotal = 0;
  double net = 0, treasury = 0;
  std::vector<RowCalc> army, fleet;
  i64 armyTotal = 0, armyField = 0, fleetTotal = 0, fleetField = 0;
  i64 slaves = 0;                     // всего рабов
  double pirateRisk = 0;              // вероятность нападения пиратов в конце хода, %
  double researchFactor = 1;          // множитель времени исследования
  bool famine = false;                // запас провизии меньше нуля («Голод»)
  std::map<Id, ResourceFlow> resources;
  Effects fx;
};

struct Calc {
  std::unordered_map<Id, ProvinceCalc> provinces;
  std::unordered_map<Id, FactionCalc> factions;
  std::unordered_map<Id, int> routeCounts;          // провинция -> число маршрутов
  const ProvinceCalc* province(Id id) const { auto it = provinces.find(id); return it == provinces.end() ? nullptr : &it->second; }
  const FactionCalc* faction(Id id) const { auto it = factions.find(id); return it == factions.end() ? nullptr : &it->second; }
};
// Полный расчёт мира. Кеш по тождеству таблиц мира; потокобезопасно.
// Не вызывайте для tx.w() изменённой транзакции: таблицы черновика меняются на месте — используйте calc(tx).
std::shared_ptr<const Calc> calc(const World& w);
// Расчёт внутри транзакции: кеш используется, только пока транзакция ничего не меняла.
std::shared_ptr<const Calc> calc(const Tx& tx);

// Развёрнутые силы фракции: строка -> число в войсках, флотах, гарнизонах, оккупационных гарнизонах и торговле.
struct Deployed {
  std::map<Id, i64> army, fleet, garrison, occupation, trade;
  i64 armyField(Id row) const;        // в войсках, гарнизонах и оккупационных гарнизонах
  i64 fleetField(Id row) const;       // во флотах и в торговле
};
Deployed deployed(const World& w, Id faction);
// Резерв строки: общая численность − в поле (войска, гарнизоны, оккупационные гарнизоны) или в море (флоты, торговля).
i64 reserveOf(const World& w, Id faction, Id row);
// Срок исследования технологии с модификатором «Время исследования технологий» государства.
int researchTurns(const World& w, const Tech& t);
// Население государства (сумма по его сухопутным провинциям).
i64 statePopulation(const World& w, Id state);

// ================================================================ сущности
Id createFaction(Tx& tx, FactionKind kind, const std::string& name = {});
void removeFaction(Tx& tx, Id faction);      // чистит владения, отношения, войска, сделки, штабы, ссылки
Id createCharacter(Tx& tx, Id faction = 0, const std::string& name = {});
void removeCharacter(Tx& tx, Id character);  // чистит лордов, правителей, совет, героев войск
Id createModifier(Tx& tx, const std::string& name = {});
void removeModifier(Tx& tx, Id modifier);    // убирает из всех списков
Id createBuilding(Tx& tx, Id owner /*0 — общее дерево*/, const std::string& name = {});
void removeBuilding(Tx& tx, Id building);    // убирает из провинций и требований
// Удалить последний уровень постройки (не построен и не строится нигде, остаётся хотя бы один). Требования других
// построек к удалённому уровню опускаются до нового наибольшего; возвращает ID таких построек.
std::vector<Id> removeLastBuildingLevel(Tx& tx, Id building);
Id createTech(Tx& tx, Id faction, const std::string& name = {});
void removeTech(Tx& tx, Id tech);            // убирает из зависимостей
void copyTechTree(Tx& tx, Id from, Id to);   // копия дерева технологий (без изученности)
enum class CatalogList : u8 { Resources, Races, Cultures, Religions, Governments, Positions };
Id addCatalogItem(Tx& tx, CatalogList list, const std::string& name);
void removeCatalogItem(Tx& tx, CatalogList list, Id item);   // чистит все ссылки; «Золото» удалить нельзя
std::vector<CatalogItem>& catalogList(Catalogs& c, CatalogList list);
const std::vector<CatalogItem>& catalogList(const Catalogs& c, CatalogList list);

void setProvinceOwner(Tx& tx, Id province, Id faction);     // гарнизон → резерв прежнего владельца, столица, оккупация
void setOccupied(Tx& tx, Id province, Id occupier /*0 — снять*/);
void deleteProvince(Tx& tx, Id province);                   // геометрия → «не назначено», запись и ссылки удаляются
void mergeProvinces(Tx& tx, Id target, Id source);          // геометрия и население/постройки source → target
Id splitProvince(Tx& tx, Id province, const std::vector<Vec2>& line);   // нож; новая провинция наследует владельца, культуру, религию
void setCapital(Tx& tx, Id state, Id province /*0 — снять*/);          // столичная провинция — только своя
// Морская/сухопутная провинция (ТЗ 1.a.iii). Ставшая морской: гарнизон → резерв, столица и оккупация снимаются,
// начатое строительство приостанавливается; прочие данные сохраняются и снова действуют на суше.
void setProvinceSea(Tx& tx, Id province, bool sea);
// Колонизация провинции без владельца: владелец — state, из казны — константа «Стоимость колонизации».
void colonize(Tx& tx, Id province, Id state);
// Назначения — только доступные герои своего государства (ТЗ «Фиксы», п.9; «Модификаторы», 1.6 и 1.12).
void setRuler(Tx& tx, Id faction, Id character);
void setCouncilMember(Tx& tx, Id faction, Id seat, Id character);
void setLord(Tx& tx, Id province, Id character);
// Снять персонажа со всех назначений: правитель, совет, лорд, герой и полководец войск.
void clearAssignments(Tx& tx, Id character);
// Вид государства и признак основного игрового государства.
void setStateKind(Tx& tx, Id state, StateKind kind);
void setMainState(Tx& tx, Id state, bool on);
// Пустошь нежити и осквернение (ТЗ «Виды государств», п.6, 7, 10, 11).
void makeWasteland(Tx& tx, Id province);
void cleanseWasteland(Tx& tx, Id province, i64 settlers);
void desecrate(Tx& tx, Id province);
void cleanseDesecration(Tx& tx, Id province);
// Рабы государства и рабы на работах в провинции (раса из рабов владельца, не больше 10 % населения).
void setSlaves(Tx& tx, Id state, Id race, i64 count, double contentment);
void setSlaveWork(Tx& tx, Id province, Id race, i64 count);
i64 slaveWorkLimit(const World& w, Id province);
// Численность расы в провинции (в пустоши нежити — только 0).
void setRacePop(Tx& tx, Id province, Id race, i64 pop);
// Взять n жителей поровну со всех провинций государства (внутри провинции — пропорционально расам); возвращает
// взятых по расам. exceptProvince — кроме этой провинции. Население не дробится (целые числа).
std::map<Id, i64> takePopulation(Tx& tx, Id state, i64 n, Id exceptProvince = 0);
// Вернуть n жителей поровну по провинциям государства (onlyProvince ≠ 0 — в одну провинцию). Внутри провинции —
// пропорционально races (пусто — расам провинции; в пустой провинции — самой многочисленной расе государства).
void givePopulation(Tx& tx, Id state, i64 n, Id onlyProvince = 0, const std::map<Id, i64>& races = {});
// Поровну между получателями с пределами (−1 — без предела); остаток — по одному по порядку.
std::vector<i64> splitEven(i64 n, const std::vector<i64>& cap);
// Пропорционально весам методом наибольших остатков (сумма — ровно n, если сумма весов не меньше n или веса без пределов).
std::vector<i64> splitProportional(i64 n, const std::vector<i64>& weights);

// Правка областей инструментами карты (geo::createProvince/addArea/removeArea/fillAt): провинции, у которых после
// правки не осталось области, удаляются вместе с записью (хроника, возврат за начатое строительство).
// province — новая/изменённая провинция (0, если она сама осталась без области); removed — названия удалённых.
struct AreaEdit { Id province = 0; std::vector<std::string> removed; };
AreaEdit createProvince(Tx& tx, const std::vector<Vec2>& poly, Terrain terrain = Terrain::None, double snap = 1.0);
AreaEdit addArea(Tx& tx, Id province, const std::vector<Vec2>& poly, double snap = 1.0);
AreaEdit removeArea(Tx& tx, Id province, const std::vector<Vec2>& poly, double snap = 1.0);
AreaEdit fillAt(Tx& tx, Vec2 p, Id province /*0 — новая провинция*/);
// Суша или море внутри контура (правка берега, geo::paintTerrain); province — 0.
AreaEdit paintTerrain(Tx& tx, const std::vector<Vec2>& poly, Terrain terrain, double snap = 1.0);

// ================================================================ объекты карты (знаки и фигуры)
// Мир без своих объектов показывает объекты базовой карты; первая правка карты переносит их в мир с теми же ID
// (ensureMapObjects — в начале каждой правки знаков и фигур, до неё добавлять нельзя; base — art::baseObjects).
// Точки ограничиваются картой; контур фигуры не может пересекать сам себя; масштаб и ширина — в пределах schema.
void ensureMapObjects(Tx& tx, const std::vector<MapSymbol>& baseSymbols, const std::vector<MapShape>& baseShapes);
Id addSymbol(Tx& tx, MapSymbol s);                                 // ID выдаётся (s.id не учитывается)
void placeSymbol(Tx& tx, Id symbol, Vec2 p, double z);             // точка привязки и порядок отрисовки
void setSymbol(Tx& tx, Id symbol, SymbolKind kind, float scale, u8 variant);
void removeSymbol(Tx& tx, Id symbol);
Id addShape(Tx& tx, MapShape s);                                   // ID выдаётся; контур — от 3 точек, линия — от 2
// Точки фигуры: ring −1 — контур или линия, ring ≥ 0 — остров воды.
void setShapePoint(Tx& tx, Id shape, int ring, int index, Vec2 p);
void insertShapePoint(Tx& tx, Id shape, int ring, int segment, Vec2 p);   // после точки segment
void removeShapePoint(Tx& tx, Id shape, int ring, int index);
void moveShape(Tx& tx, Id shape, Vec2 delta);                      // сдвиг целиком (в пределах карты)
void setShapeLine(Tx& tx, Id shape, float width, float dash);      // стена и река: ширина, пунктир (только стена)
void removeShape(Tx& tx, Id shape);

// Строки таблиц войск и флота фракции (ТЗ 1.c.i). ID строк — общая последовательность Seq::Row.
Id addArmyRow(Tx& tx, Id faction, UnitType type, const std::string& name = {}, i64 total = 0, double upkeep = 0);
Id addFleetRow(Tx& tx, Id faction, ShipType type, const std::string& name = {}, i64 total = 0, double upkeep = 0);
void setRowTotal(Tx& tx, Id faction, Id row, i64 total);   // правка резерва без ограничений: не меньше числа в поле
// Строка удаляется вместе с отрядами в войсках и гарнизонах; воины возвращаются в население (нежить — в трупы,
// демоны — в демоническую энергию), формирование строки отменяется с возвратом.
void removeRow(Tx& tx, Id faction, Id row);
void setRowRace(Tx& tx, Id faction, Id row, const std::string& race);

// ================================================================ формирование и резерв
// ТЗ «Общие доработки», п.10: отряды — из населения государства (нежить — из трупов, демоны — из демонической
// энергии), корабли — за ресурсы констант стоимости; через 2 хода — в резерв.
struct RecruitCost { i64 people = 0; std::map<Id, double> res; std::vector<std::string> problems; };
RecruitCost recruitCost(const World& w, Id faction, Id row, i64 count);
void recruit(Tx& tx, Id faction, Id row, i64 count);
void cancelFormation(Tx& tx, Id faction, int index);          // возврат людей и ресурсов
void disbandReserve(Tx& tx, Id faction, Id row, i64 count);   // роспуск резерва: воины → население
// Корабли из резерва в торговле (ТЗ «Общие доработки», п.11) и оккупационный гарнизон (п.14).
void setTradeFleet(Tx& tx, Id faction, Id row, i64 count);
void setOccupationGarrison(Tx& tx, Id province, Id row, i64 count);
// Верность войска −100…100 % (с «Непреклонным лоялистом» не уменьшается, у «Армии нежити» — 100 %).
void setArmyLoyalty(Tx& tx, Id army, double loyalty);

// ================================================================ войска и флот
Id createArmy(Tx& tx, ArmyKind kind, Id faction, Vec2 pos);
void renameArmy(Tx& tx, Id army, const std::string& name);
void setUnits(Tx& tx, Id army, Id faction, Id row, i64 count);       // не больше резерва
void setHero(Tx& tx, Id army, Id character, bool on);
void setCommander(Tx& tx, Id army, Id character);
void setGarrison(Tx& tx, Id province, Id row, i64 count);            // строка армии владельца
void disband(Tx& tx, Id army);                                        // отряды → резерв
void moveArmy(Tx& tx, Id army, Vec2 pos);                             // проверка рельефа и наложения

enum class EncounterType : u8 { None, Merge, Battle, Alliance, DeclareWar, Blocked };
struct Encounter {
  EncounterType type = EncounterType::None;
  Id target = 0;
  std::string reason;
  Id us = 0, them = 0;   // пара фракций: Battle — воюющие, DeclareWar — кому предложить объявить войну
};
// Что произойдёт, если объект moving поставить в pos (с учётом объекта под позицией).
// Встреча — наложение фигурок (ближе 2 · kObjectRadius); None — свободное перемещение.
Encounter encounter(const World& w, Id moving, Vec2 pos);
void mergeArmies(Tx& tx, Id target, Id source);       // одна фракция: численности складываются
void formAllied(Tx& tx, Id target, Id source);        // союзники: группы хранятся раздельно
std::vector<Id> dissolveAllied(Tx& tx, Id army);      // группы → отдельные объекты рядом; ID всех объектов, исходный первым
struct SplitSpec { std::map<std::pair<Id, Id>, i64> units; std::vector<Id> heroes; };   // (фракция, строка) -> число
Id splitArmy(Tx& tx, Id army, const SplitSpec& spec);  // новый объект рядом
void declareWar(Tx& tx, Id a, Id b);                  // «в войне», отношения −25 (ТЗ 1.c.iv)

struct BattleResult {
  Id attacker = 0, defender = 0;
  bool attackerWins = true;
  Vec2 attackerOrigin;                                  // откуда пришёл нападавший
  std::map<Id, std::map<std::pair<Id, Id>, i64>> losses;  // армия -> (фракция, строка) -> потери
};
struct BattleOutcome {
  std::vector<Id> destroyed;          // уничтоженные объекты
  std::vector<Id> fallenHeroes;       // герои уничтоженных объектов (окно «Судьба героев»)
  Id winner = 0, loser = 0;           // фракции-лидеры сторон (0 — победы нет)
  Id winnerArmy = 0;                  // уцелевший объект победителя
  Id province = 0;                    // место боя
  double corpses = 0;                 // трупы победителю (государство нежити, некроманты)
};
BattleOutcome resolveBattle(Tx& tx, const BattleResult& r);   // потери, смещение проигравшего, хроника

// ================================================================ штурм и захват провинции (ТЗ «Войны», п.4–5)
// Войско у замка или башни провинции государства, с которым его фракция в войне (мятежники — у провинции своего
// прежнего государства). why — причина отказа.
bool canSiege(const World& w, Id army, Id province, std::string* why = nullptr);
struct SiegeResult {
  Id attacker = 0, province = 0;
  bool attackerWins = true;
  Vec2 attackerOrigin;
  std::map<std::pair<Id, Id>, i64> attackerLosses;   // (фракция, строка) → потери нападающего
  std::map<Id, i64> garrisonLosses;                  // строка гарнизона → потери
};
BattleOutcome resolveSiege(Tx& tx, const SiegeResult& r);
enum class Capture : u8 { Occupy, Plunder, Raze, Devastate, Count };
struct CaptureOptions {
  bool can[int(Capture::Count)] = {};
  std::string why[int(Capture::Count)];
  double plunderGold = 0, razeGold = 0;
  i64 population = 0;
};
CaptureOptions captureOptions(const World& w, Id army, Id province);
// Захват после победы над гарнизоном (или без гарнизона). slavesPct — опустошение: доля населения в рабы, 0…75.
void capture(Tx& tx, Id army, Id province, Capture how, int slavesPct = 0);

// ================================================================ герои (ТЗ «Механика героев»)
enum class Fate : u8 { Fled, Killed, Captured };
// Судьба героя после уничтожения его войска: сбежал, убит (место захоронения), взят в плен (пленившее государство).
void heroFate(Tx& tx, Id character, Fate fate, Id captor, Id burial);
// Воскрешение погибшего героя: природа (schema::mod::Living/Undead/Demon/Mechanism) и государство.
void resurrect(Tx& tx, Id character, std::string_view natureKey, Id faction);
std::vector<Id> captivesOf(const World& w, Id state);   // пленники государства

// ================================================================ мятеж (ТЗ «Механика мятежа»)
bool canMutiny(const World& w, Id army, std::string* why = nullptr);
struct MutinyResult {
  Id rebelState = 0, rebelArmy = 0, loyalArmy = 0;
  bool full = false;                  // войско восстало целиком (верность −100 %)
  std::vector<Id> loyalHeroes;        // верные герои восставшего целиком войска — окно «Судьба героя»
};
// Мятеж всех войск государства в провинции войска army (каждое — по своей верности).
MutinyResult mutiny(Tx& tx, Id army);
// Мятежное государство «Мятеж (Название)»: существующее или новое, в войне с origin.
Id rebelStateFor(Tx& tx, Id origin);
// Мятежники нападают на войско прежнего государства с отрицательной верностью: неверная часть переходит к ним.
bool willDefect(const World& w, Id rebelArmy, Id target);
void defect(Tx& tx, Id rebelArmy, Id target);
// Восстание провинции: армия «Мятежные крестьяне» (10 % населения) и «Восставшие рабы»; 0 — некому восставать.
Id provinceUprising(Tx& tx, Id province);
// Проверки размещения берут грани из кеша geo::faces: передавайте значение мира, а не черновик с изменённой геометрией.
std::optional<Vec2> findFreeSpot(const World& w, ArmyKind kind, Vec2 near, Id exclude = 0);   // спираль от near
bool validPosition(const World& w, ArmyKind kind, Vec2 pos, Id exclude = 0, std::string* why = nullptr);
Id armyAt(const World& w, Vec2 pos, Id exclude = 0);   // объект, фигурка которого покрывает точку

// ================================================================ дипломатия
// Состояние «В войне» меняется на другое только перемирием (ТЗ «Войны», п.2): иначе отказ.
void setRelation(Tx& tx, Id a, Id b, double value, RelStatus status);
struct RelationRow { Id other = 0; double value = 0; RelStatus status = RelStatus::Unknown; };
std::vector<RelationRow> relationsOf(const World& w, Id faction);   // все прочие фракции
void shiftRelation(Tx& tx, Id a, Id b, double delta);              // отношения ± delta (−100…100)

// Перемирие: что отдаёт каждая сторона. После заключения — состояние status (по умолчанию «Статус-кво»).
struct TruceTerms {
  std::vector<Id> provinces;          // провинции стороны → другой стороне
  double reparations = 0;             // репарации: золото за ход
  int reparationsTurns = 0;
  std::map<Id, double> resources;     // разовые выплаты (золото — из казны, ресурсы)
  i64 slaves = 0;                     // рабов из населения стороны (не больше 5 % её населения)
  std::vector<Id> heroes;             // пленные герои, которых сторона держит
  bool vassal = false;                // сторона становится вассалом другой
};
struct Truce {
  Id a = 0, b = 0;
  TruceTerms fromA, fromB;
  RelStatus status = RelStatus::Neutral;
};
i64 truceSlavesMax(const World& w, Id giver);
std::vector<std::string> truceProblems(const World& w, const Truce& t);
void concludeTruce(Tx& tx, const Truce& t);

// Вассалитет (ТЗ «Механика вассалитета»).
std::vector<Id> vassalsOf(const World& w, Id suzerain);
void setSuzerain(Tx& tx, Id vassal, Id suzerain);   // 0 — снять
// Сюзерен вступает (join) или не вступает в войну за вассала, которому объявил войну attacker.
void suzerainDefends(Tx& tx, Id suzerain, Id vassal, Id attacker, bool join);
// Вассал согласился (agree) или отказался вступить в войну сюзерена против enemy.
void vassalAnswers(Tx& tx, Id suzerain, Id vassal, Id enemy, bool agree);
bool canVassalRebel(const World& w, Id vassal, std::string* why = nullptr);
void vassalRebels(Tx& tx, Id vassal);

// ================================================================ торговля, дань, репарации
struct DealCheck { bool ok = true; std::vector<std::string> problems; };
DealCheck validateDeal(const World& w, const Deal& d);
Id concludeDeal(Tx& tx, Deal d);                       // разовые позиции исполняются сразу
void cancelDeal(Tx& tx, Id deal);
Id imposeTribute(Tx& tx, DealKind kind, Id receiver, Id payer, double amountPerTurn, int turns);

// ================================================================ строительство
struct BuildOption {
  Id building = 0;
  int level = 1;                     // уровень, который будет строиться
  bool upgrade = false;
  int turns = 1;
  std::map<Id, double> cost;         // с учётом множителя провинции
  bool can = false;
  std::vector<std::string> reasons;  // почему нельзя
};
std::vector<BuildOption> buildOptions(const World& w, Id province);
void startBuilding(Tx& tx, Id province, Id building);  // новый или следующий уровень
void cancelBuilding(Tx& tx, Id province, Id building); // полный возврат стоимости
void demolish(Tx& tx, Id province, Id building);
// Слоты: провинции, где после изменения мира построек больше слотов, — и какие постройки будут снесены (с конца
// списка). ТЗ «Фиксы», п.10: предупреждение до действия.
struct SlotLoss { Id province = 0; int slots = 0, used = 0; std::vector<Id> buildings; };
std::vector<SlotLoss> slotLosses(const World& before, const World& after);
std::vector<SlotLoss> excessBuildings(const World& w);   // все провинции, где построек больше слотов
// Снести лишние постройки (возврат за начатое): only — только эти провинции и постройки (nullptr — все лишние).
void trimExcessBuildings(Tx& tx, const std::vector<SlotLoss>* only = nullptr);

// ================================================================ технологии
struct ResearchCheck { bool ok = false; std::vector<Id> missing; };
ResearchCheck canResearch(const World& w, Id tech);
void setStudied(Tx& tx, Id tech, bool studied);
void startResearch(Tx& tx, Id tech);
void stopResearch(Tx& tx, Id tech);
bool wouldCycle(const World& w, Id tech, Id prereq);
void setPrereq(Tx& tx, Id tech, Id prereq, bool on);  // с проверкой цикла
void autoLayout(Tx& tx, Id faction);                  // расстановка дерева по слоям
constexpr double kTreeColStep = 280, kTreeRowStep = 120;  // шаг столбцов (слоёв) и строк autoLayout

// ================================================================ гильдии и маршруты
void buildHq(Tx& tx, Id guild, Id province);
void removeHq(Tx& tx, Id guild, Id province);
void setInfluence(Tx& tx, Id province, Id guild, double pct);   // сумма по провинции ≤ 100
void setHomeState(Tx& tx, Id guild, Id state);                  // запрещено для государственной гильдии
Id createStateGuild(Tx& tx, Id state, const std::string& name = {});
Id createRoute(Tx& tx, const std::vector<Vec2>& pts, Id guild = 0);
void setRoutePoints(Tx& tx, Id route, const std::vector<Vec2>& pts);
void removeRoute(Tx& tx, Id route);

// ================================================================ хроника и ход
struct LogRefs { Id province = 0, army = 0; std::vector<Id> factions; };
Id addLog(Tx& tx, LogKind kind, const std::string& text, const LogRefs& refs = {});

struct TurnFactionLine { Id faction = 0; double treasuryBefore = 0, treasuryAfter = 0, income = 0, expenses = 0; std::map<Id, double> resources; };
// События хода, которые требуют решения после него: восстание провинции (армия мятежников — битва с войском,
// гарнизоном или захват) и мятеж войска с верностью −100 % (верные герои — «Судьба героя»).
struct TurnEvent {
  enum Kind : u8 { Uprising, Mutiny } kind = Uprising;
  Id army = 0;                        // армия мятежников
  Id province = 0;
  Id origin = 0, rebelState = 0;
  std::vector<Id> heroes;
};
struct TurnReport {
  int turnFrom = 0, turnTo = 0;
  std::vector<TurnFactionLine> factions;
  std::vector<Id> logIds;            // записи хроники, созданные при завершении хода
  std::vector<Id> rebellions;        // провинции, где вспыхнуло восстание
  std::vector<TurnEvent> events;
};
// Шаг 1 (снимок мира в истории ходов) выполняет вызывающий: снимок — store.world() до транзакции (core/io).
TurnReport endTurn(Tx& tx);
TurnReport previewTurn(const World& w);   // без изменения мира; logIds указывают на записи пробного хода

}  // namespace rg::rules
