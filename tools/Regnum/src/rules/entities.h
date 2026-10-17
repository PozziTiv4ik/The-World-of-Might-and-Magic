// Regnum — правила: сущности и справочники: фракции, персонажи, модификаторы, постройки и технологии, простые списки, группы ресурсов и реликвий, реликвии, особые отряды, провинции и их области, строки войск и флота (rules/entities.cpp, trees.cpp, catalog_lists.cpp, catalog.cpp, areas.cpp, population.cpp, rows.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>

#include "core/schema.h"
#include "core/world.h"
#include "geo/geom.h"

namespace rg::rules {

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
using schema::CatalogList, schema::catalogList;          // простые списки справочников (core/schema.h)
Id addCatalogItem(Tx& tx, CatalogList list, const std::string& name);
void removeCatalogItem(Tx& tx, CatalogList list, Id item);   // чистит все ссылки; «Золото» удалить нельзя

// ---- группы ресурсов
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

// ---- реликвии
Id addRelic(Tx& tx, const std::string& name = {}, Rarity rarity = Rarity::Common);
void setRelic(Tx& tx, Id relic, const std::string& name, Rarity rarity, const std::string& desc);
void removeRelic(Tx& tx, Id relic);                            // убирается отовсюду
Id relicHolder(const World& w, Id relic);                      // первый персонаж с реликвией; 0 — ни у кого из героев
// Место реликвии: свободна (нигде), у героя, в хранилище постройки провинции, в инвентаре государства, спрятана в
// провинции.
struct RelicPlace {
  enum Kind : u8 { Free, Hero, Building, State, Hidden } kind = Free;
  Id id = 0;          // персонаж, провинция (постройка и тайник) или государство
  Id building = 0;    // постройка хранилища
  Id owner = 0;       // государство места (герой — его фракция; постройка и тайник — владелец провинции)
  bool operator==(const RelicPlace& o) const { return kind == o.kind && id == o.id && building == o.building; }
};
// Место и сколько там штук (relic — реликвия, если список общий для нескольких реликвий).
struct RelicAt {
  RelicPlace place;
  i64 count = 0;
  Id relic = 0;
};
// Все места реликвии с количеством (уникальная — не больше одного места и одной штуки).
std::vector<RelicAt> relicPlaces(const World& w, Id relic);
RelicPlace relicPlace(const World& w, Id relic);               // первое место (у уникальной — единственное); Free — нигде
i64 relicCountAt(const World& w, Id relic, const RelicPlace& at);
// Реликвия доступна для выдачи из справочника (сундуки, правка мира): копируемая — всегда, уникальная — пока нигде не лежит.
bool relicFree(const World& w, Id relic);
// Переложить count штук из from в to (без проверок правил; хроника — у вызывающего). Источник Free — новые штуки из
// справочника (уникальная — только если свободна), цель Free — штуки исчезают из мира. Уникальная — одна штука.
void moveRelic(Tx& tx, Id relic, const RelicPlace& from, const RelicPlace& to, i64 count = 1);
// Одна штука в to: уникальная — из её места, копируемая — новая из справочника.
void moveRelic(Tx& tx, Id relic, const RelicPlace& to);
// Количество штук в инвентаре или хранилище (правка мира): больше — новые из справочника, меньше — лишние исчезают.
void setRelicCount(Tx& tx, Id relic, const RelicPlace& at, i64 count);
// Главенство редкости: true — a выше b (эпохальная > легендарная > эпическая > редкая > обычная).
inline bool rarityAbove(Rarity a, Rarity b) { return int(a) > int(b); }
// Реликвия группы «Археологические находки» (с подгруппами): героям напрямую (из справочника) не назначается.
bool isArchFind(const World& w, Id relic);
// Отдать герою одну штуку (from не задан: уникальная — из её места, копируемая — из инвентаря его государства, если там
// есть, иначе новая из справочника); находку археологов — только из владений его государства. takeRelic — одну
// штуку из инвентаря героя в инвентарь его государства.
void giveRelic(Tx& tx, Id character, Id relic, std::optional<RelicPlace> from = std::nullopt);
void takeRelic(Tx& tx, Id character, Id relic);
// Хранилище реликвий постройки (Building::relicStore): положить count штук — из инвентаря государства-владельца, его
// героев, его хранилищ или (правка мира) новые из справочника; from не задан — как у giveRelic. Вынуть — в инвентарь
// государства-владельца.
void storeRelic(Tx& tx, Id province, Id building, Id relic, std::optional<RelicPlace> from = std::nullopt, i64 count = 1);
void unstoreRelic(Tx& tx, Id province, Id building, Id relic, i64 count = 1);
// Убрать из инвентаря государства count штук (исчезают из мира).
void releaseStateRelic(Tx& tx, Id state, Id relic, i64 count = 1);
// Рынок (Building::market): «Продать реликвии» — штуки из хранилищ реликвий государства за золото по редкости
// (археологические находки — schema::kRelicPriceArch, остальные — kRelicPrice).
double relicPrice(const World& w, Id relic);                   // золото за штуку
std::vector<RelicAt> sellableRelics(const World& w, Id state);  // стопки в достроенных хранилищах государства
bool hasMarket(const World& w, Id state);                      // у государства достроен рынок
double sellRelics(Tx& tx, Id state, const RelicPlace& at, Id relic, i64 count);   // возвращает выручку

// ---- особые отряды
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
// Морская/сухопутная провинция. Ставшая морской: гарнизон → резерв, столица и оккупация снимаются,
// начатое строительство приостанавливается; прочие данные сохраняются и снова действуют на суше.
void setProvinceSea(Tx& tx, Id province, bool sea);
// Колонизация провинции без владельца: владелец — state, из казны — константа «Стоимость колонизации»; в провинцию
// переселяются settlers жителей (не меньше 500) поровну из провинций государства (внутри провинции — по расам).
void colonize(Tx& tx, Id province, Id state, i64 settlers);
// Жителей, которых государство может отправить переселенцами (в своих провинциях, кроме exceptProvince).
i64 settlersAvailable(const World& w, Id state, Id exceptProvince = 0);
// Назначения — только доступные герои своего государства.
void setRuler(Tx& tx, Id faction, Id character);
void setCouncilMember(Tx& tx, Id faction, Id seat, Id character);
void setLord(Tx& tx, Id province, Id character);
// Снять персонажа со всех назначений: правитель, совет, лорд, герой и полководец войск.
void clearAssignments(Tx& tx, Id character);
// Вид государства и признак основного игрового государства.
void setStateKind(Tx& tx, Id state, StateKind kind);
void setMainState(Tx& tx, Id state, bool on);
// Пустошь нежити и осквернение.
void makeWasteland(Tx& tx, Id province);
void cleanseWasteland(Tx& tx, Id province, i64 settlers);   // переселенцев — не меньше 500
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
AreaEdit createProvince(Tx& tx, const std::vector<Vec2>& poly, Terrain terrain = Terrain::None, double snap = geo::kDefaultSnap);
AreaEdit addArea(Tx& tx, Id province, const std::vector<Vec2>& poly, double snap = geo::kDefaultSnap);
AreaEdit removeArea(Tx& tx, Id province, const std::vector<Vec2>& poly, double snap = geo::kDefaultSnap);
AreaEdit fillAt(Tx& tx, Vec2 p, Id province /*0 — новая провинция*/);
// Суша или море внутри контура (правка берега, geo::paintTerrain); province — 0.
AreaEdit paintTerrain(Tx& tx, const std::vector<Vec2>& poly, Terrain terrain, double snap = geo::kDefaultSnap);

// Строки таблиц войск и флота фракции. ID строк — общая последовательность Seq::Row. Новая строка
// содержится за 0,001 золота за юнит в ход.
Id addArmyRow(Tx& tx, Id faction, UnitType type, const std::string& name = {}, i64 total = 0, double upkeep = schema::kNewRowUpkeep);
Id addFleetRow(Tx& tx, Id faction, ShipType type, const std::string& name = {}, i64 total = 0, double upkeep = schema::kNewRowUpkeep);
// Ключевой ресурс строки флота (морское чудовище — ресурс подгруппы «Морские чудовища»); 0 — снять.
void setFleetRowKey(Tx& tx, Id faction, Id row, Id res);
std::vector<Id> shipKeyResources(const World& w, ShipType type);
// Наёмники: только пехота и кавалерия (schema::isMercType), найм и содержание золотом, раса «Наемники».
Id addMercRow(Tx& tx, Id state, UnitType type, const std::string& name = {}, double hire = -1 /*константа «Найм наемника»*/);
void setMercHire(Tx& tx, Id state, Id row, double hire);
i64 mercLimit(const World& w, Id state);         // гильдии наёмников × константа «Наемников за гильдию»
i64 mercCount(const World& w, Id state);         // наёмники в строках и в формировании
// Верфи государства: типы кораблей, открытые достроенными верфями (бит = ShipType).
u32 shipyardAccess(const World& w, Id state);
void setRowTotal(Tx& tx, Id faction, Id row, i64 total);   // правка резерва без ограничений: не меньше числа в поле
// Строка удаляется вместе с отрядами в войсках и гарнизонах; воины возвращаются в население (нежить — в трупы,
// демоны — в демоническую энергию), формирование строки отменяется с возвратом.
void removeRow(Tx& tx, Id faction, Id row);
void setRowRace(Tx& tx, Id faction, Id row, const std::string& race);   // у элементалей — только «Элементали»
// Тип строки армии: имя по типу следует за типом, раса по умолчанию — тоже, неподходящий ключевой ресурс снимается.
void setRowType(Tx& tx, Id faction, Id row, UnitType type);
// Ключевой ресурс юнита: из группы типа (кавалерия — «Ездовые наземные», воздушная
// кавалерия — «Ездовые летающие», звери — «Звери» без «Чудовищ», чудовища — «Чудовища»), механизмы — «Запчасти
// механизмов» (не меньше 1 на юнит). res 0 — снять.
void setRowKey(Tx& tx, Id faction, Id row, Id res, double perUnit = 1);
std::vector<Id> keyResources(const World& w, UnitType type);   // подходящие ключевые ресурсы
bool keyAllowed(const World& w, UnitType type, Id res);
// Дополнительный ресурс и эссенция на юнит (≥ 0; 0 — убрать), содержание элементалей эссенцией за юнит в ход.
void setRowExtra(Tx& tx, Id faction, Id row, Id res, double perUnit);
void setRowEssence(Tx& tx, Id faction, Id row, Id essence, double perUnit);
void setRowEssUpkeep(Tx& tx, Id faction, Id row, Id essence, double perUnit);

}  // namespace rg::rules
