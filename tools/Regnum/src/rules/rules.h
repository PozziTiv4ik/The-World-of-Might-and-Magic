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
// Показатели археологической группы (ТЗ «Доработки №2», п.1–2): уровень по опыту, содержание за ход, шанс успеха и
// живучесть (база уровня + модификаторы группы и глобальные модификаторы государства, 0…25 %), прибавка к опыту,
// к археологическим сокровищам и к шансу обнаружения места (глобальные), ранение и чума группы.
struct ArchStats {
  int level = 1;
  double upkeep = 0;
  double success = 0, vitality = 0;
  double expPct = 0, treasurePct = 0, discoveryPct = 0;
  bool wounded = false, plague = false;
  Effects fx;
};
ArchStats archStats(const World& w, Id state, Id group);
// Уровень новой археологической группы государства: 1 + эффект «Начальный уровень» (технология «Обученные исследователи»).
int archStartLevel(const World& w, Id state);

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
// Позиция константы-списка ресурсов (essence — эссенция): не меньше базовой стоимости (Constant::minRes/minEss);
// базовую позицию убрать нельзя (ТЗ «Доработки №3», п.7–8).
void setConstantRes(Tx& tx, std::string_view key, Id res, double amount, bool essence = false);
void removeConstantRes(Tx& tx, std::string_view key, Id res, bool essence = false);
// Расы для отрядов (константа «Расы для отрядов») и раса строки армии (пусто — по виду государства и типу).
std::vector<std::string> unitRaces(const World& w);
std::string defaultUnitRace(StateKind kind, UnitType type);
std::string unitRace(const World& w, Id faction, const ArmyRow& row);
StateKind stateKindOf(const World& w, Id faction);
bool hasModKey(const World& w, const std::vector<Id>& mods, std::string_view key);
// Модификатор у героя: в его списке или у изученного таланта его класса.
bool characterHas(const World& w, Id character, std::string_view key);
// Модификаторы изученных талантов героя (без повторов).
std::vector<Id> talentModifiers(const World& w, Id character);
// Генерация встроенного модификатора по умолчанию (некромант, лич — эссенция смерти, ценности археологии —
// сокровища): по названию записи справочника мира.
std::map<Id, double> builtinGenOf(const World& w, std::string_view key, bool essence);
// Генерация модификатора за ход (у шаблона встроенного, которого ещё нет в мире, — по умолчанию).
std::map<Id, double> modEssGen(const World& w, const Modifier& m);
std::map<Id, double> modResGen(const World& w, const Modifier& m);
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
  double scale = 1;         // множитель эффектов («Влияние совета» — число должностей в совете)
};
std::vector<AutoMod> autoModifiers(const World& w, Id faction);
std::vector<AutoMod> autoProvinceModifiers(const World& w, Id province);
// Число назначений в совет (персонажи, доступные для назначений).
int councilAssigned(const World& w, Id faction);
// Новое место в совете (не больше schema::kMaxCouncilSeats должностей); возвращает ID места.
Id addCouncilSeat(Tx& tx, Id faction, const std::string& position, Id character = 0);
// Государство изучило технологию с ключом (Tech::key) — своего дерева или общую.
bool studiedTechKey(const World& w, Id faction, std::string_view key);
// В провинциях государства достроена постройка с ключом (Building::key); сколько таких.
int builtWithKey(const World& w, Id state, std::string_view key);

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
  std::map<Id, double> produce;       // ресурсы от построек владельцу за ход (и модификаторов провинции)
  std::map<Id, double> essence;       // эссенции от построек генерации и модификаторов провинции владельцу за ход
  Effects fx;
};

struct RowCalc {
  Id row = 0;
  i64 total = 0, field = 0, garrison = 0, reserve = 0;   // field включает гарнизоны (и корабли в торговле)
  i64 trade = 0, occupation = 0;      // корабли в торговле; отряды в оккупационных гарнизонах
  i64 forming = 0;                    // формируется (поступит в резерв)
  double upkeepEach = 0, upkeepTotal = 0;
};

// consumption — расход (провизия государства живых); conversionIn/Out — постройки преобразования этого хода;
// net = production + tradeIn − tradeOut − consumption − conversionIn + conversionOut.
struct ResourceFlow {
  double stock = 0, production = 0, tradeIn = 0, tradeOut = 0, consumption = 0, net = 0;
  double conversionIn = 0, conversionOut = 0;
};
// Эссенция элемента фракции за ход: генерация построек и модификаторов (провинций, государства, героев), содержание
// элементалей (может увести запас в долг).
struct EssenceFlow { double stock = 0, generation = 0, upkeep = 0, net = 0; };
// Шаг постройки преобразования в конце хода (план расчёта, его же исполняет endTurn): start — начинается цикл (ресурсы
// на входе списываются), finish — цикл завершается (ресурс на выходе поступает), cycle — ходов до конца после хода.
struct ConvertStep {
  Id province = 0, building = 0;
  bool start = false, finish = false;
  int cycle = 0;
  std::map<Id, double> in;            // списывается при начале цикла
  ResAmount out;                      // поступает при завершении
};

struct FactionCalc {
  Id id = 0;
  std::vector<Id> provinces;          // государство: владения; гильдия: провинции штабов
  i64 population = 0;
  std::vector<std::pair<Id, i64>> races;               // по провинциям (ТЗ 1.b.iv — автоматически)
  double incProvinces = 0, incGuildTax = 0, incGuilds = 0, incTrade = 0, incTribute = 0;
  double incSlaves = 0, incTradeFleet = 0, incRoutes = 0;   // рабы на работах, флот в торговле, маршруты гильдии
  double incGross = 0, incomePct = 0, incTotal = 0;
  double expArmy = 0, expFleet = 0, expSpecialists = 0, expTrade = 0, expTribute = 0, expSlaves = 0, expTotal = 0;
  double expArch = 0;                 // содержание археологических групп
  double net = 0, treasury = 0;
  std::vector<RowCalc> army, fleet;
  i64 armyTotal = 0, armyField = 0, fleetTotal = 0, fleetField = 0;
  i64 slaves = 0;                     // всего рабов
  double pirateRisk = 0;              // вероятность нападения пиратов в конце хода, %
  double researchFactor = 1;          // множитель времени исследования
  bool famine = false;                // недостача провизии («Голод»)
  // Провизия (группа ресурсов «Провизия» с подгруппами): расход населения 0,001 на жителя отнимается поровну со всех
  // её ресурсов, которые есть у государства; недостача копится и держит «Голод», пока не будет покрыта.
  double provisionNeed = 0;           // расход за ход
  double provisionStock = 0;          // запас всех ресурсов провизии
  double provisionDebt = 0, provisionDebtNext = 0;   // недостача сейчас и после хода
  std::map<Id, ResourceFlow> resources;
  std::map<Id, EssenceFlow> essences; // все эссенции справочника и запасы фракции
  std::vector<ConvertStep> conversions;
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
// Срок исследования технологии с модификатором «Время исследования технологий» государства (faction — для общей
// технологии: изучающая фракция; 0 — дерево технологии).
int researchTurns(const World& w, const Tech& t, Id faction = 0);
// Население государства (сумма по его сухопутным провинциям).
i64 statePopulation(const World& w, Id state);

// ================================================================ сущности
Id createFaction(Tx& tx, FactionKind kind, const std::string& name = {});
void removeFaction(Tx& tx, Id faction);      // чистит владения, отношения, войска, сделки, штабы, ссылки
Id createCharacter(Tx& tx, Id faction = 0, const std::string& name = {});
void removeCharacter(Tx& tx, Id character);  // чистит лордов, правителей, совет, героев войск
Id createModifier(Tx& tx, const std::string& name = {});
void removeModifier(Tx& tx, Id modifier);    // убирает из всех списков
void fitModifierUses(Tx& tx, Id modifier);   // после смены вида: модификатор групп — только у археологических групп, прочие — не у групп
Id createBuilding(Tx& tx, Id owner /*0 — общее дерево*/, const std::string& name = {});
void removeBuilding(Tx& tx, Id building);    // убирает из провинций и требований
// Удалить последний уровень постройки (не построен и не строится нигде, остаётся хотя бы один). Требования других
// построек к удалённому уровню опускаются до нового наибольшего; возвращает ID таких построек.
std::vector<Id> removeLastBuildingLevel(Tx& tx, Id building);
Id createTech(Tx& tx, Id faction, const std::string& name = {});
void removeTech(Tx& tx, Id tech);            // убирает из зависимостей
void copyTechTree(Tx& tx, Id from, Id to);   // копия дерева технологий (без изученности)
enum class CatalogList : u8 { Resources, Races, Cultures, Religions, Governments, Positions, Essences };
Id addCatalogItem(Tx& tx, CatalogList list, const std::string& name);
void removeCatalogItem(Tx& tx, CatalogList list, Id item);   // чистит все ссылки; «Золото» удалить нельзя
std::vector<CatalogItem>& catalogList(Catalogs& c, CatalogList list);
const std::vector<CatalogItem>& catalogList(const Catalogs& c, CatalogList list);

// ---- группы ресурсов (ТЗ «Добавления в справочники», п.3–4)
Id addResGroup(Tx& tx, const std::string& name, Id parent = 0);
void renameResGroup(Tx& tx, Id group, const std::string& name);
void setGroupParent(Tx& tx, Id group, Id parent);              // без циклов
// Удалить группу: её подгруппы и ресурсы переходят в родительскую группу. Группу правил (провизия, звери, ездовые,
// чудовища) удалить нельзя.
void removeResGroup(Tx& tx, Id group);
void setResourceGroup(Tx& tx, Id resource, Id group);          // 0 — без группы
// Ресурсы группы с подгруппами в порядке справочника (выбор подгруппы — только она, группы — все её подгруппы).
std::vector<Id> resourcesIn(const World& w, Id group);
// Подгруппы группы (прямые дети; 0 — группы верхнего уровня) в порядке справочника.
std::vector<Id> childGroups(const World& w, Id parent);
// Путь группы: «Звери / Ездовые наземные».
std::string groupPath(const World& w, Id group);
// Ресурсы провизии (группа «Провизия» с подгруппами).
std::vector<Id> provisionResources(const World& w);

// ---- реликвии (ТЗ «Доработки», п.8 и 10; «Доработки №1», п.8–9, 14; «№2», п.11, 16)
Id addRelic(Tx& tx, const std::string& name = {}, Rarity rarity = Rarity::Common);
void setRelic(Tx& tx, Id relic, const std::string& name, Rarity rarity, const std::string& desc);
void removeRelic(Tx& tx, Id relic);                            // убирается отовсюду
Id relicHolder(const World& w, Id relic);                      // персонаж с реликвией; 0 — не у героя
// Где лежит реликвия: свободна, у героя, в хранилище постройки провинции, у государства, спрятана в провинции.
struct RelicPlace {
  enum Kind : u8 { Free, Hero, Building, State, Hidden } kind = Free;
  Id id = 0;          // персонаж, провинция (постройка и тайник) или государство
  Id building = 0;    // постройка хранилища
  Id owner = 0;       // государство места (герой — его фракция; постройка и тайник — владелец провинции)
  bool operator==(const RelicPlace&) const = default;
};
RelicPlace relicPlace(const World& w, Id relic);
// Убрать реликвию оттуда, где она лежит, и положить в новое место (без проверок правил; хроника — у вызывающего).
void moveRelic(Tx& tx, Id relic, const RelicPlace& to);
// Главенство редкости: true — a выше b (эпохальная > легендарная > эпическая > редкая > обычная).
inline bool rarityAbove(Rarity a, Rarity b) { return int(a) > int(b); }
// Реликвия группы «Археологические находки» (с подгруппами): героям напрямую (из свободных) не назначается.
bool isArchFind(const World& w, Id relic);
// Положить реликвию в инвентарь персонажа (от прежнего владельца — переходит) или убрать (к государству героя).
// Находку археологов можно отдать герою только из владений его государства.
void giveRelic(Tx& tx, Id character, Id relic);
void takeRelic(Tx& tx, Id character, Id relic);
// Хранилище реликвий постройки (Building::relicStore): положить — из свободных реликвий, реликвий государства-владельца
// или инвентаря его героев; убрать — к государству-владельцу.
void storeRelic(Tx& tx, Id province, Id building, Id relic);
void unstoreRelic(Tx& tx, Id province, Id building, Id relic);
// Реликвии государства (находки, вынутые из хранилищ): убрать — реликвия свободна.
void releaseStateRelic(Tx& tx, Id state, Id relic);

// ---- особые отряды (ТЗ «Ввод новых механик», п.4–5)
Id addSpecial(Tx& tx, const std::string& name = {});
// Записать особый отряд (имя, тип, раса, ключевой ресурс, цены) — с проверками; строки армий с ним повторяют запись.
void setSpecial(Tx& tx, const SpecialUnit& s);
void removeSpecial(Tx& tx, Id special);                        // строки армий становятся обычными, постройки теряют доступ
struct SpecialSource { Id special = 0, building = 0, province = 0; };
// Особые отряды, доступные государству: достроенная постройка доступа в его сухопутной провинции.
std::vector<SpecialSource> specialAccess(const World& w, Id state);
bool hasSpecialAccess(const World& w, Id state, Id special);
std::vector<Id> specialBuildings(const World& w, Id special);  // постройки, дающие доступ
// Строка армии особого отряда (нужна постройка доступа).
Id addSpecialRow(Tx& tx, Id faction, Id special);

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

// Строки таблиц войск и флота фракции (ТЗ 1.c.i). ID строк — общая последовательность Seq::Row. Новая строка
// содержится за 0,001 золота за юнит в ход (ТЗ «Доработки №3», п.9).
Id addArmyRow(Tx& tx, Id faction, UnitType type, const std::string& name = {}, i64 total = 0, double upkeep = schema::kNewRowUpkeep);
Id addFleetRow(Tx& tx, Id faction, ShipType type, const std::string& name = {}, i64 total = 0, double upkeep = schema::kNewRowUpkeep);
// Ключевой ресурс строки флота (морское чудовище — ресурс подгруппы «Морские чудовища»); 0 — снять.
void setFleetRowKey(Tx& tx, Id faction, Id row, Id res);
std::vector<Id> shipKeyResources(const World& w, ShipType type);
// Наёмники (ТЗ «Доработки №3», п.12–13): только пехота и кавалерия, найм и содержание золотом, раса «Наемники».
bool mercType(UnitType t);
Id addMercRow(Tx& tx, Id state, UnitType type, const std::string& name = {}, double hire = -1 /*константа «Найм наемника»*/);
void setMercHire(Tx& tx, Id state, Id row, double hire);
i64 mercLimit(const World& w, Id state);         // гильдии наёмников × константа «Наемников за гильдию»
i64 mercCount(const World& w, Id state);         // наёмники в строках и в формировании
// Верфи государства (ТЗ «Доработки №3», п.2): типы кораблей, открытые достроенными верфями (бит = ShipType).
u32 shipyardAccess(const World& w, Id state);
void setRowTotal(Tx& tx, Id faction, Id row, i64 total);   // правка резерва без ограничений: не меньше числа в поле
// Строка удаляется вместе с отрядами в войсках и гарнизонах; воины возвращаются в население (нежить — в трупы,
// демоны — в демоническую энергию), формирование строки отменяется с возвратом.
void removeRow(Tx& tx, Id faction, Id row);
void setRowRace(Tx& tx, Id faction, Id row, const std::string& race);   // у элементалей — только «Элементали»
// Тип строки армии: имя по типу следует за типом, раса по умолчанию — тоже, неподходящий ключевой ресурс снимается.
void setRowType(Tx& tx, Id faction, Id row, UnitType type);
// Ключевой ресурс юнита (ТЗ «Ввод новых механик», п.2): из группы типа (кавалерия — «Ездовые наземные», воздушная
// кавалерия — «Ездовые летающие», звери — «Звери» без «Чудовищ», чудовища — «Чудовища»), механизмы — «Запчасти
// механизмов» (не меньше 1 на юнит). res 0 — снять.
void setRowKey(Tx& tx, Id faction, Id row, Id res, double perUnit = 1);
std::vector<Id> keyResources(const World& w, UnitType type);   // подходящие ключевые ресурсы
bool keyAllowed(const World& w, UnitType type, Id res);
// Дополнительный ресурс и эссенция на юнит (≥ 0; 0 — убрать), содержание элементалей эссенцией за юнит в ход.
void setRowExtra(Tx& tx, Id faction, Id row, Id res, double perUnit);
void setRowEssence(Tx& tx, Id faction, Id row, Id essence, double perUnit);
void setRowEssUpkeep(Tx& tx, Id faction, Id row, Id essence, double perUnit);
// Строки армии особого отряда повторяют его запись (после правки справочника).
void syncSpecialRows(Tx& tx, Id special);

// ================================================================ формирование и резерв
// ТЗ «Общие доработки», п.10: отряды — из населения государства (нежить — из трупов, демоны — из демонической
// энергии), корабли — за ресурсы констант стоимости; через 2 хода — в резерв.
// people — из населения (живые; нежить и демоны — трупы и энергия в res); res — ресурсы (ключевой, дополнительные,
// трупы, энергия, стоимость кораблей); ess — эссенции элементов.
struct RecruitCost { i64 people = 0; std::map<Id, double> res, ess; std::vector<std::string> problems; };
RecruitCost recruitCost(const World& w, Id faction, Id row, i64 count);
void recruit(Tx& tx, Id faction, Id row, i64 count);
void cancelFormation(Tx& tx, Id faction, int index);          // возврат людей и ресурсов
void disbandReserve(Tx& tx, Id faction, Id row, i64 count);   // роспуск резерва: воины → население
// Корабли из резерва в торговле (ТЗ «Общие доработки», п.11) и оккупационный гарнизон (п.14).
void setTradeFleet(Tx& tx, Id faction, Id row, i64 count);
void setOccupationGarrison(Tx& tx, Id province, Id row, i64 count);
// Верность войска −100…100 % (с «Непреклонным лоялистом» не уменьшается, у «Армии нежити» — 100 %).
void setArmyLoyalty(Tx& tx, Id army, double loyalty);
// Эссенции элементов фракции (начальные запасы и правка).
void setEssence(Tx& tx, Id faction, Id essence, double amount);

// ================================================================ гарнизон (ТЗ «Доработки», п.1)
// Герой владельца в гарнизоне: доступный, не в войске и не в другом гарнизоне.
void setGarrisonHero(Tx& tx, Id province, Id character, bool on);
Id heroGarrison(const World& w, Id character);                 // провинция, в гарнизоне которой герой; 0 — нет
// Верность гарнизона −100…100 % (как у войска: модификаторы государства и «Непреклонный лоялист», у государства
// нежити — всегда 100 %).
void setGarrisonLoyalty(Tx& tx, Id province, double loyalty);
double garrisonLoyaltyDelta(const World& w, Id province);

// ================================================================ войска без государства (ТЗ «Доработки №2», п.19)
// Фракция «Без государства» (одна на мир, создаётся при первом обращении): её войска враждебны всем государствам.
Id wildFaction(const World& w);                      // 0 — ещё нет
Id ensureWildFaction(Tx& tx);
// Войско без государства у точки near (на суше рядом): строки отрядов создаются у фракции «Без государства».
struct WildUnitSpec { std::string name; UnitType type = UnitType::LightInf; std::string race; i64 count = 0; };
Id spawnWildArmy(Tx& tx, Vec2 near, const std::string& name, const std::vector<WildUnitSpec>& units);

// ================================================================ войска и флот
Id createArmy(Tx& tx, ArmyKind kind, Id faction, Vec2 pos);
void renameArmy(Tx& tx, Id army, const std::string& name);
void setUnits(Tx& tx, Id army, Id faction, Id row, i64 count);       // не больше резерва
void setHero(Tx& tx, Id army, Id character, bool on);
void setCommander(Tx& tx, Id army, Id character);
void setGarrison(Tx& tx, Id province, Id row, i64 count);            // строка армии владельца
void disband(Tx& tx, Id army);                                        // отряды → резерв
void moveArmy(Tx& tx, Id army, Vec2 pos);                             // проверка рельефа и наложения

// Embark — войско на флот своего государства: посадка или (на борту уже есть войско) обмен отрядами с ним.
enum class EncounterType : u8 { None, Merge, Battle, Alliance, DeclareWar, Blocked, Embark };
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
  Id province = 0;                    // провинция мятежа
  bool garrisonLoyal = false;         // в гарнизоне провинции остались верные отряды
};
// Мятеж всех войск государства в провинции войска army и её гарнизона (каждое — по своей верности).
MutinyResult mutiny(Tx& tx, Id army);
// Мятеж гарнизона с отрицательной верностью (и войск владельца в провинции): неверная часть — войско мятежников рядом,
// верная остаётся в гарнизоне с верностью 0 %; герои с «Недовольством правителем» уходят к мятежникам.
bool canGarrisonMutiny(const World& w, Id province, std::string* why = nullptr);
MutinyResult garrisonMutiny(Tx& tx, Id province);
// Мятежники штурмуют гарнизон прежнего государства с отрицательной верностью: неверная часть переходит к ним.
bool willGarrisonDefect(const World& w, Id rebelArmy, Id province);
void garrisonDefect(Tx& tx, Id rebelArmy, Id province);
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
// value — хранимое значение; bonus — смещение за одну религию (+10): действующее отношение — relationValue.
struct RelationRow { Id other = 0; double value = 0; RelStatus status = RelStatus::Unknown; double bonus = 0; };
std::vector<RelationRow> relationsOf(const World& w, Id faction);   // все прочие фракции (кроме «Без государства»)
void shiftRelation(Tx& tx, Id a, Id b, double delta);              // отношения ± delta (−100…100)
// Одна религия у двух государств: отношения всегда смещены на +10 (ТЗ «Доработки №4», п.3). relationValue — значение
// с этим смещением (в пределах −100…100); хранится значение без него.
double relationBonus(const World& w, Id a, Id b);
double relationValue(const World& w, Id a, Id b);

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
  std::map<Id, double> essCost;      // эссенции (с тем же множителем)
  bool can = false;
  std::vector<std::string> reasons;  // почему нельзя
  // Поставить готовой (изначальные постройки, без цены и срока): можно, если нет других причин, кроме нехватки ресурсов.
  bool canPlace = false;
  std::vector<std::string> placeReasons;
};
std::vector<BuildOption> buildOptions(const World& w, Id province);
void startBuilding(Tx& tx, Id province, Id building);  // новый или следующий уровень
void cancelBuilding(Tx& tx, Id province, Id building); // полный возврат стоимости
void demolish(Tx& tx, Id province, Id building);
// Мгновенно завершить строящийся уровень (уплаченное остаётся уплаченным) — ТЗ «Доработки», п.5.
void completeBuilding(Tx& tx, Id province, Id building);
// Поставить готовую постройку уровня level без цены и срока (изначальные постройки провинции): те же условия, что у
// строительства, кроме цены. Уже есть — уровень меняется (строящуюся сначала отмените).
void placeBuilding(Tx& tx, Id province, Id building, int level);
// Постройка преобразования в провинции: «Простаивает» (idle) или «Работает».
void setConvertIdle(Tx& tx, Id province, Id building, bool idle);
// Требование «на государство» (соборы): чтобы построить (n + 1)-ю постройку, нужно per × (n + 1) достроенных req.
void setStateReq(Tx& tx, Id building, Id req, int per);  // per 0 — снять
// Возможности постройки (ТЗ «Доработки №1–3»): хранилище реликвий, целительство, чума, верфь, гильдия наёмников,
// только в приморской провинции. Выключение снимает их данные (реликвии хранилищ — государству-владельцу, типы
// кораблей уровней).
enum class BuildingFlag : u8 { RelicStore, Healing, Plague, Shipyard, Mercenary, Coastal };
void setBuildingFlag(Tx& tx, Id building, BuildingFlag flag, bool on);
void setLevelShips(Tx& tx, Id building, int level, u32 ships);
void setLevelEssCost(Tx& tx, Id building, int level, Id essence, double amount);   // 0 — убрать
// Провинция приморская: граничит с морем (берег или морская провинция).
bool isCoastal(const World& w, Id province);
// Морские провинции, соседние с провинцией.
std::vector<Id> seaNeighbors(const World& w, Id province);
// Требования построек (ТЗ «Доработки», п.4 и 7): общая постройка зависит только от общих построек и технологий,
// уникальная — ещё и от построек и технологий своего государства; без циклов.
bool canRequireBuilding(const World& w, Id building, Id req, std::string* why = nullptr);
bool canRequireTech(const World& w, Id building, Id tech, std::string* why = nullptr);
void setBuildingReq(Tx& tx, Id building, Id req, int level);  // level 0 — снять
void setBuildingTech(Tx& tx, Id building, Id tech, bool on);
std::vector<Id> techUnlocks(const World& w, Id tech);          // постройки, которые требуют технологию
// Культовая постройка (ТЗ «Доработки», п.2): общая — одна на всю карту, уникальная — одна у своего государства.
// Провинция, где она уже есть или строится (кроме exceptProvince); 0 — нигде.
Id cultBuiltIn(const World& w, Id building, Id exceptProvince = 0);
// Рецепт постройки преобразования (до трёх разных ресурсов на входе, срок цикла ≥ 1 хода).
void setRecipe(Tx& tx, Id building, const Recipe& r);
// Дополнительные возможности постройки: преобразование, генерация эссенции, доступ к особым отрядам. Выключение
// снимает их данные (рецепт и циклы в провинциях, эссенции уровней, список особых отрядов).
enum class BuildingRole : u8 { Convert, Essence, Special };
void setBuildingRole(Tx& tx, Id building, BuildingRole role, bool on);
void setLevelEssence(Tx& tx, Id building, int level, Id essence, double perTurn);   // 0 — убрать
void setBuildingSpecial(Tx& tx, Id building, Id special, bool on);
// Слоты: провинции, где после изменения мира построек больше слотов, — и какие постройки будут снесены (с конца
// списка). ТЗ «Фиксы», п.10: предупреждение до действия.
struct SlotLoss { Id province = 0; int slots = 0, used = 0; std::vector<Id> buildings; };
std::vector<SlotLoss> slotLosses(const World& before, const World& after);
std::vector<SlotLoss> excessBuildings(const World& w);   // все провинции, где построек больше слотов
// Снести лишние постройки (возврат за начатое): only — только эти провинции и постройки (nullptr — все лишние).
void trimExcessBuildings(Tx& tx, const std::vector<SlotLoss>* only = nullptr);

// ================================================================ технологии
// Общее дерево технологий (Tech::faction == 0, ТЗ «Доработки», п.6–7) изучает каждая фракция отдельно
// (Faction::techs); faction — изучающая фракция (для технологии своего дерева — 0 или её фракция).
// missing — не изученные условия; problems — не достроены нужные постройки, не хватает ресурсов на стоимость.
struct ResearchCheck { bool ok = false; std::vector<Id> missing; std::vector<std::string> problems; };
TechProgress techState(const World& w, Id tech, Id faction = 0);
bool techStudied(const World& w, Id tech, Id faction = 0);
ResearchCheck canResearch(const World& w, Id tech, Id faction = 0);
void setStudied(Tx& tx, Id tech, bool studied, Id faction = 0);
void startResearch(Tx& tx, Id tech, Id faction = 0);
void stopResearch(Tx& tx, Id tech, Id faction = 0);
bool wouldCycle(const World& w, Id tech, Id prereq);
// Стоимость исследования (ресурсы списываются при начале, возвращаются при остановке) и постройки, которые должны
// быть достроены в государстве (по ключу Building::key).
void setTechCost(Tx& tx, Id tech, Id res, double amount);   // 0 — убрать
void setTechNeedKey(Tx& tx, Id tech, const std::string& key, bool on);
// Название постройки с ключом (уникальная — этого государства, иначе любая с ключом).
std::string buildingKeyName(const World& w, Id state, std::string_view key);
// Связь: технологии одного дерева; уникальная может зависеть от общей, общая от уникальной — нет.
void setPrereq(Tx& tx, Id tech, Id prereq, bool on);  // с проверкой цикла
void autoLayout(Tx& tx, Id faction);                  // расстановка дерева по слоям (0 — общее дерево)
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

// ================================================================ чума (ТЗ «Доработки №3», п.4–5)
// Чуму можно установить: сухопутная провинция без чумы, без «Временного иммунитета» и не пустошь нежити.
bool canPlague(const World& w, Id province, std::string* why = nullptr);
// Установить чуму, если можно (true — установлена).
bool infectPlague(Tx& tx, Id province);
// Снять чуму без распространения (любое снятие даёт «Временный иммунитет»).
void curePlague(Tx& tx, Id province);
// «Вылечить провинцию» постройкой целительства (500 любой эссенции владельца) и «Заразить чумой» постройкой чумы
// (2500 эссенции чумы).
void healProvince(Tx& tx, Id province, Id building, Id essence);
void plagueProvince(Tx& tx, Id province, Id building);
// В провинции достроена постройка с возможностью (целительство, чума).
bool hasBuildingRole(const World& w, Id province, BuildingFlag flag);

// ================================================================ хроника и ход
struct LogRefs { Id province = 0, army = 0; std::vector<Id> factions; };
Id addLog(Tx& tx, LogKind kind, const std::string& text, const LogRefs& refs = {});

struct TurnFactionLine {
  Id faction = 0;
  double treasuryBefore = 0, treasuryAfter = 0, income = 0, expenses = 0;
  std::map<Id, double> resources;     // изменение запасов ресурсов (кроме золота)
  std::map<Id, double> essences;      // изменение запасов эссенций элементов
  double provisionDebt = 0;           // недостача провизии после хода (голод, если больше нуля)
};
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

// ================================================================ археология (ТЗ «Доработки №2», rules/archaeology.cpp)
// Случайность — rng с зерном из счётчика мира Meta::rng (каждое событие увеличивает его) и ID участников; хроника —
// LogKind::Archaeology (тайник реликвий и режим правки — без хроники).
//
// ---- археологические группы (п.1–2)
// Цена группы: население государства (нежить — трупы) и золото по глобальным константам. problems — почему нельзя.
struct ArchGroupCost {
  i64 people = 0;
  bool corpses = false;               // государство нежити платит трупами
  double gold = 0;
  std::vector<std::string> problems;  // нет гильдии, не хватает населения, трупов или золота
};
ArchGroupCost archGroupCost(const World& w, Id state);
// Сформировать группу (нужна достроенная «Гильдия Археологов»): опыт — наименьший опыт начального уровня
// (archStartLevel). Возвращает ID группы.
Id createArchGroup(Tx& tx, Id state, const std::string& name = {});
void renameArchGroup(Tx& tx, Id state, Id group, const std::string& name);
void disbandArchGroup(Tx& tx, Id state, Id group);             // без возврата цены
void setArchGroupExp(Tx& tx, Id state, Id group, int exp);      // 0…1000
// Модификаторы группы — только модификаторы археологических групп (ModKind::ArchGroup): добавленные получают срок по
// умолчанию, снятые теряют срок; turns 0 — бессрочно.
void setArchGroupModifiers(Tx& tx, Id state, Id group, const std::vector<Id>& mods);
void addArchGroupModifier(Tx& tx, Id state, Id group, Id modifier, int turns = -1);   // turns < 0 — по умолчанию
void setArchGroupModTurns(Tx& tx, Id state, Id group, Id modifier, int turns);
// Группа может получить задание в этот ход: не занята в этом ходу и не ранена (с чумой — может).
bool archGroupReady(const World& w, Id state, Id group, std::string* why = nullptr);

// ---- задания групп в провинциях своего государства (п.3, 14, 15, 17–19)
// Полученное: ресурс (сокровища, золото, свитки, ресурс сундука), эссенция, реликвия (к реликвиям государства), сундук
// (его содержимое — следующими строками на глубине depth + 1), «реликвии не нашлось».
struct ArchGain {
  enum Kind : u8 { Resource, Essence, Relic, Chest, NoRelic } kind = Resource;
  Id id = 0;
  double amount = 0;
  int depth = 0;
};
// Итог события «Результат исследования в провинции «…»» / «Результат раскопок в провинции «…»» (для окна).
struct ArchReport {
  enum Kind : u8 { Discover, Explore, Dig } kind = Discover;
  enum Tragedy : u8 { NoTragedy, Wound, Danger, Calamity } tragedy = NoTragedy;
  std::string title;
  Id state = 0, group = 0, province = 0;
  std::string groupName;              // имя группы (группа может погибнуть позже)
  int slot = -1;                      // найденный или исследуемый слот
  Id site = 0;
  bool success = false;
  double chance = 0;                  // шанс успеха, %
  int stage = 0, stages = 0;          // этап исследования (1…) и этапов места
  bool done = false;                  // место исследовано полностью (финальная награда получена)
  std::vector<std::string> lines;     // заметки: чума группы, реликвия из тайника, трагедия и бедствие (исход — в полях)
  std::vector<ArchGain> gains;
  int exp = 0, expAfter = 0, levelBefore = 1, levelAfter = 1;
  std::string calamity;               // заголовок пробуждения бедствия
  bool wounded = false;               // группа ранена
  bool plague = false;                // группа получила «Заражение чумой»
  bool awaiting = false;              // смертельная опасность: ждёт решения (resolveArchDanger)
  Id army = 0;                        // появившееся войско без государства
  Id infected = 0;                    // провинция, заражённая чумой группы
  int tragedyLine = -1;               // первая строка lines о трагедии или бедствии (−1 — их нет)
};
// Шанс «Найти археологическое место»: 50 % + шанс успеха группы + «Старательные искатели», %.
double discoveryChance(const World& w, Id state, Id group);
// Шанс «Успеха» следующего этапа места слота с шансом успеха группы, %.
double exploreChance(const World& w, Id state, Id group, Id province, int slot);
// Провинция государства: есть закрытый слот (найти), открытое неисследованное место (исследовать), все места
// исследованы полностью (раскопки).
bool canDiscover(const World& w, Id state, Id province, std::string* why = nullptr);
bool canExplore(const World& w, Id state, Id province, int slot, std::string* why = nullptr);
bool canExcavate(const World& w, Id state, Id province, std::string* why = nullptr);
ArchReport discoverSite(Tx& tx, Id state, Id group, Id province);
ArchReport exploreSite(Tx& tx, Id state, Id group, Id province, int slot);
ArchReport excavate(Tx& tx, Id state, Id group, Id province);
// «Группу может спасти только божественное вмешательство»: essence ≠ 0 — 1000 этой эссенции государства, группа
// спасена (ранена); 0 — отказ, группа погибает.
void resolveArchDanger(Tx& tx, Id state, Id group, Id essence);
std::vector<Id> divineEssences(const World& w, Id state);      // эссенции, которых хватает на спасение
// Открыть сундук для государства (всё полученное — ему): treasurePct — прибавка к археологическим сокровищам.
std::vector<ArchGain> openChest(Tx& tx, Id state, Id chest, Rng& rng, double treasurePct);
// Строка полученного: «Археологические сокровища 50», «Реликвия «…»», «Ящик».
std::string archGainText(const World& w, const ArchGain& g);

// ---- тайник реликвий (п.16): без хроники
// Реликвии, которые можно спрятать в провинции: инвентари героев, хранилища построек и реликвии её владельца.
std::vector<Id> hideableRelics(const World& w, Id province);
void hideRelic(Tx& tx, Id province, Id relic);
// Откопать может только спрятавшее государство, пока владеет провинцией: в инвентарь его героя или в хранилище его
// постройки (to — RelicPlace::Hero или RelicPlace::Building).
bool canUnearth(const World& w, Id province, std::string* why = nullptr);
void unearthRelic(Tx& tx, Id province, const RelicPlace& to);

// ---- режим правки (п.13): без хроники
void setArchSlotOpen(Tx& tx, Id province, int slot, bool open);   // закрытие сбрасывает этапы
// Другое место из справочника (без повторов в провинции): этапы сброшены, сокровище выбрано заново из наград места.
void setArchSlotSite(Tx& tx, Id province, int slot, Id site);

// ---- справочники «Археологические места» и «Сундуки сокровищ» (п.4, 10, 12)
// Места не добавляются и не удаляются: правятся название, значок и списки наград по слотам.
void setArchSite(Tx& tx, Id site, const std::string& name, const std::string& icon);
void setArchSiteRewards(Tx& tx, Id site, int slot, const std::vector<Id>& chests);
Id addChest(Tx& tx, const std::string& name = {});
// Записать сундук с проверкой полей: ресурс — конкретный («Руда», «Материалы» или «Трупы») или случайный из группы
// «Руда»/«Материалы» (с подгруппами); артефакт — группа реликвий и редкости; сундук — список без самого себя.
void setChest(Tx& tx, const Chest& c);
// Удалить сундук: он убирается из наград мест и вложенных списков; сокровища провинций с ним выбираются заново.
void removeChest(Tx& tx, Id chest);
bool chestResourceAllowed(const World& w, Id res);
bool chestGroupAllowed(const World& w, Id group);
// Поле сундука одной строкой: «Археологические сокровища 50», «Руда / Обычная 50», «Артефакт: обычный»…
std::string chestItemText(const World& w, const ChestItem& it);

// ================================================================ флот: вместимость, посадка и высадка, обмен отрядами, союзные
// войска, флот у верфи (ТЗ «Доработки №3», п.3, 6; «№4», п.9; rules/naval.cpp)
// Вместимость (воинов): фрегат — константа «Вместимость фрегата», линкор — «Вместимость линкора», прочие суда (галеоны,
// морские чудовища) войска не перевозят. Вместимость флота — сумма по его кораблям всех групп.
i64 shipCapacity(const World& w, ShipType type);
i64 fleetCapacity(const World& w, Id fleet);
i64 armySize(const World& w, Id army);                // численность объекта — сумма отрядов
// Войско на борту (Army::carrier ↔ Army::cargo): не стоит на карте, его положение — положение флота.
Id cargoOf(const World& w, Id fleet);                 // войско на борту флота (0 — нет)
Id carrierOf(const World& w, Id army);                // флот, на борту которого войско (0 — войско на карте)
// «Недалеко» (единицы карты): флот от провинции войска при посадке, место высадки от флота вне морской провинции.
constexpr double kNavalReach = 4 * schema::kObjectRadius;
constexpr double kLandingReach = 6 * schema::kObjectRadius;
// Посадка на флот (ТЗ «Доработки №3», п.6): войско и флот одного государства (все фракции войска есть во флоте),
// войско в приморской провинции P, флот — в морской провинции, соседней с P (у P нет соседних морских провинций или
// флот вне морских провинций — недалеко от P), на борту нет другого войска, численность не больше вместимости.
bool canEmbark(const World& w, Id army, Id fleet, std::string* why = nullptr);
void embark(Tx& tx, Id army, Id fleet);
// На флоте уже есть войско: подошедшее войско может обменяться отрядами с войском на борту (то же место посадки).
bool canBoardExchange(const World& w, Id army, Id fleet, std::string* why = nullptr);
// Высадка: место на суше в провинции, соседней с морской провинцией флота (landingProvinces); флот вне морской
// провинции — суша недалеко от флота. Возвращает высаженное войско.
std::vector<Id> landingProvinces(const World& w, Id fleet);
bool canLand(const World& w, Id fleet, Vec2 pos, std::string* why = nullptr);
Id land(Tx& tx, Id fleet, Vec2 pos);
// Обмен отрядами двух объектов одного вида и одной фракции (и подошедшего войска с войском на борту): first — итоговая
// численность первого объекта по (фракция, строка) (не указанные строки не меняются), heroesFirst — все герои первого
// после обмена (остальные герои обоих — у второго). Сумма по строкам сохраняется; отряды и герои фракции переходят
// только в объект, где у неё есть группа; войско на борту — не больше вместимости флота, вместимость флота с войском
// на борту не становится меньше его численности. Главный полководец следует за героем (без полководца — первый герой).
// Объект без отрядов и героев исчезает.
struct ExchangeSpec {
  std::map<std::pair<Id, Id>, i64> first;
  std::vector<Id> heroesFirst;
};
bool canExchange(const World& w, Id a, Id b, std::string* why = nullptr);
std::vector<std::string> exchangeProblems(const World& w, Id a, Id b, const ExchangeSpec& spec);
void exchangeUnits(Tx& tx, Id a, Id b, const ExchangeSpec& spec);
// «Вернуть войско» (ТЗ «Доработки №4», п.9): группа фракции союзного объекта (кроме лидера) отделяется в отдельный
// объект рядом — её отряды и герои; войско на борту союзного флота уходит вместе с группой своей фракции.
bool canReturnGroup(const World& w, Id army, Id faction, std::string* why = nullptr);
Id returnGroup(Tx& tx, Id army, Id faction);
// Флот у верфи (ТЗ «Доработки №3», п.3): провинции фракции (государство — свои неоккупированные, гильдия — со штабом),
// приморские, с достроенной верфью; новый флот — на свободном месте моря у берега провинции.
std::vector<Id> shipyardProvinces(const World& w, Id faction);
std::optional<Vec2> fleetSpot(const World& w, Id province);
Id placeFleet(Tx& tx, Id faction, Id province);

// ================================================================ классы героев, таланты, лич, группы реликвий (ТЗ «Доработки
// №1», п.11, 14; «№4», п.6, 8; rules/talents.cpp)
// Классы героев и деревья талантов (как в World of Warcraft): ячейка — ярус row (0 — верхний) и столбец col (0…3), в
// ячейке не больше одного таланта; условие — талант того же класса из более высокого яруса (меньший row), поэтому
// циклов нет. Ярус r открыт герою, когда в ярусах выше он вложил не меньше r × tierPoints очков. Очков — по одному за
// уровень. Дерево общее для класса, изучение у каждого героя своё (Character::talents — в порядке изучения).
// Правка дерева (стоимость, ячейка, условие, очки на ярус, удаление) снимает у героев класса таланты, которые перестали
// быть законными (по порядку изучения); функции правки возвращают число таких героев.
constexpr int kTalentCols = schema::kTalentCols;
constexpr int kTalentRows = schema::kTalentRows;   // ярусов не больше (как при чтении файла)
constexpr int kMaxTierPoints = 60;
Id addHeroClass(Tx& tx, const std::string& name = {});
// Название (уникальное), описание, значок, цвет, очки на ярус (1…60); таланты записи не меняются.
int setHeroClass(Tx& tx, const HeroClass& c);
void removeHeroClass(Tx& tx, Id cls);                          // герои класса теряют класс и таланты
// Новый талант в свободной ячейке; возвращает его ID.
Id addTalent(Tx& tx, Id cls, int row, int col, const std::string& name = {});
// Название (уникальное в дереве), описание, значок, стоимость 1…5, модификаторы героя (вид «Везде» или «Для героев»,
// без состояний «Мертв» и «Взят в плен»). Ячейка и условие — moveTalent и setTalentPrereq.
int setTalent(Tx& tx, const Talent& t);
// Перенести талант в ячейку; занятая ячейка — таланты меняются местами. Условия остаются выше зависимых — иначе отказ.
int moveTalent(Tx& tx, Id talent, int row, int col);
int setTalentPrereq(Tx& tx, Id talent, Id prereq);             // 0 — снять условие
// Удалить талант: у героев снимается, у зависимых талантов условие снимается.
int removeTalent(Tx& tx, Id talent);
const HeroClass* talentClass(const World& w, Id talent);       // класс, в дереве которого талант
// Таланты, для которых talent — условие.
std::vector<Id> talentDependents(const World& w, Id talent);
// Очков, которые нужно вложить в ярусы выше, чтобы открыть ярус row.
inline int tierThreshold(const HeroClass& c, int row) { return std::max(0, row) * std::max(1, c.tierPoints); }
// Ярусов в дереве (наибольший занятый + 1; пустое дерево — 0).
int talentTiers(const HeroClass& c);

// ---- герой: уровень, класс, изучение талантов
int talentPoints(const World& w, Id character);                // всего очков (уровень героя)
int talentPointsSpent(const World& w, Id character);           // вложено в изученные таланты
int talentSpentAbove(const World& w, Id character, int row);   // вложено в ярусы выше row
// Изучить можно: талант дерева класса героя, ещё не изучен, изучено условие, ярус открыт, хватает свободных очков.
bool canLearnTalent(const World& w, Id character, Id talent, std::string* why = nullptr);
void learnTalent(Tx& tx, Id character, Id talent);
// Отменить нельзя, если от таланта зависят другие изученные (условие или порог ярусов).
bool canUnlearnTalent(const World& w, Id character, Id talent, std::string* why = nullptr);
void unlearnTalent(Tx& tx, Id character, Id talent);
void resetTalents(Tx& tx, Id character);
// Уровень героя 1…60. Ниже вложенных очков — отказ с причиной; resetTalents = true — сначала таланты сбрасываются.
void setHeroLevel(Tx& tx, Id character, int level, bool resetTalents = false);
void setCharacterClass(Tx& tx, Id character, Id cls);          // 0 — без класса; смена класса сбрасывает таланты

// ---- «Возвысить до Лича» (ТЗ «Доработки №1», п.11): герой с «Некромантом» отдаёт 20 000 трупов и 5000 эссенции
// смерти своего государства и реликвию инвентаря не ниже эпической; «Некромант» сменяется «Личем», реликвия остаётся
// у героя с припиской « (Филактерия «Имя героя»)».
bool lichOffered(const World& w, Id character);                // кнопка видна: «Некромант» и ещё не лич
Id deathEssence(const World& w);                               // «Эссенция смерти» справочника (0 — нет)
std::string phylacteryName(const std::string& relic, const std::string& hero);
bool isPhylactery(const Relic& r);
// Реликвии инвентаря героя, годные в филактерию: не ниже эпической и ещё не филактерия (по главенству редкости).
std::vector<Id> phylacteryRelics(const World& w, Id character);
// Почему возвысить нельзя (пусто — можно); relic 0 — реликвия ещё не выбрана.
std::vector<std::string> lichProblems(const World& w, Id character, Id relic);
void ascendLich(Tx& tx, Id character, Id relic);

// ---- группы реликвий (как группы ресурсов) и реликвия в справочнике (rules/catalog.cpp)
Id addRelicGroup(Tx& tx, const std::string& name = {}, Id parent = 0);
void renameRelicGroup(Tx& tx, Id group, const std::string& name);
void setRelicGroupParent(Tx& tx, Id group, Id parent);         // без циклов
// Удалить группу: подгруппы и реликвии переходят к родителю. «Археологические находки» удалить нельзя.
void removeRelicGroup(Tx& tx, Id group);
void setRelicGroup(Tx& tx, Id relic, Id group);                // 0 — без группы
std::vector<Id> relicsIn(const World& w, Id group);            // с подгруппами; 0 — все реликвии
std::vector<Id> childRelicGroups(const World& w, Id parent);   // прямые подгруппы (0 — верхний уровень)
std::string relicGroupPath(const World& w, Id group);          // «Находки / Шантири»
// Порядок реликвий по главенству редкости (эпохальная выше), затем по названию.
void sortByRarity(const World& w, std::vector<Id>& relics);
// Изображение реликвии (PNG или JPEG; пусто — значок) и карточка проекта (05_Активы_персонажей, ASSET-…).
void setRelicImage(Tx& tx, Id relic, const std::string& bytes);
void setRelicEntity(Tx& tx, Id relic, const std::string& entity);

}  // namespace rg::rules
