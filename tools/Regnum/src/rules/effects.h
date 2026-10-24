// Regnum — правила: модификаторы и их эффекты, встроенные записи (модификаторы, ресурсы, константы), расы отрядов, модификаторы сущностей и автоматические модификаторы (rules/mods.cpp, rules/builtins.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>

#include "core/schema.h"
#include "core/world.h"

namespace rg::rules {

// ================================================================ модификаторы
struct EffectSource {
  // Auto — модификатор, который редактор ставит сам (столица, совет, голод, должности); key — его ключ (modifier
  // может быть 0, если записи в мире ещё нет — тогда действует шаблон schema::builtinModifiers()).
  // Hero — модификатор героя (свой, класса, реликвии, таланта); Trait — раса, культура, религия или форма правления
  // справочника (key — «race», «culture», «religion», «government»).
  enum Kind : u8 { Province, Faction, Tech, Building, Guild, Army, Auto, Hero, Trait } kind = Province;
  Id id = 0;         // провинция / фракция / технология / постройка / гильдия / войско / герой / запись справочника
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
// Показатели археологической группы: уровень по опыту, содержание за ход, шанс успеха и
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
// Модификатор мира с ключом: запись мира или, пока её нет, шаблон schema::builtinModifiers().
const Modifier* builtinMod(const World& w, std::string_view key);
Id builtinModId(const World& w, std::string_view key);             // 0 — записи в мире ещё нет
Id ensureBuiltinMod(Tx& tx, std::string_view key);                 // запись мира (создаётся по шаблону при первом обращении)
// Встроенный ресурс (schema::kBuiltinResources: трупы, демоническая энергия, запчасти механизмов, археологические
// сокровища, свитки Шантири): создаётся при первом обращении; «gold» — rs1.
Id resourceId(const World& w, std::string_view key);               // 0 — ресурса ещё нет
Id ensureResource(Tx& tx, std::string_view key);
// Глобальная константа мира или, если её не правили, встроенная по умолчанию (неизвестный ключ — пустая).
const Constant& constantOf(const World& w, std::string_view key);
Constant& ensureConstant(Tx& tx, std::string_view key);            // запись мира (встроенная — по умолчанию)
std::string addConstant(Tx& tx, ConstType type, const std::string& name);   // своя константа; возвращает ключ
void removeConstant(Tx& tx, std::string_view key);                 // встроенную удалить нельзя
// Позиция константы-списка ресурсов (essence — эссенция): не меньше базовой стоимости (Constant::minRes/minEss);
// базовую позицию убрать нельзя.
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
// Модификаторы героя с весом: собственные (1), класса (1), реликвий инвентаря (1 за реликвию), изученных талантов
// (× число изученных рангов); один модификатор из нескольких источников — веса складываются. У лича «Некромант» не
// действует (лич его заменяет). Действие: эффекты войск — на его войско; локальные — на провинцию, где он лорд;
// глобальные (кроме эффектов войск) — на его государство, если он правитель или в совете; «Успех заказного убийства» —
// на него самого.
struct HeroMod {
  Id modifier = 0;
  double scale = 1;
};
std::vector<HeroMod> heroModifiers(const World& w, Id character);
// Подпись источника-справочника (EffectSource::Trait): «культура «…»», «религия «…»», «раса «…»», «форма правления «…»».
std::string traitName(const World& w, const EffectSource& s);
// Генерация модификатора за ход (у шаблона встроенного, которого ещё нет в мире, — генерация по умолчанию: некромант,
// лич — эссенция смерти, ценности археологии — сокровища; по названию записи справочника мира).
std::map<Id, double> modEssGen(const World& w, const Modifier& m);
std::map<Id, double> modResGen(const World& w, const Modifier& m);
bool armyHas(const World& w, Id army, std::string_view key);
bool provinceHas(const World& w, Id province, std::string_view key);
// Модификатор провинции с действующими в ней эффектами: «Чума» при «Здании чумы» в провинции даёт прирост населения
// (schema::kPlagueBuildingGrowth) вместо убыли. Нет модификатора — пустая запись.
Modifier provinceModView(const World& w, Id province, Id modifier);
// Герой доступен для назначений: не «Мертв» и не «Взят в плен».
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

}  // namespace rg::rules
