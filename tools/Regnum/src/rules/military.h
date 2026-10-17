// Regnum — правила: войска и флот на карте: встречи, битвы, штурм и захват провинции, судьба героев, мятеж и восстание (rules/military.cpp, capture.cpp, heroes.cpp, rebellion.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>

#include "core/world.h"

namespace rg::rules {

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
void declareWar(Tx& tx, Id a, Id b);                  // «в войне», отношения −25

struct BattleResult {
  Id attacker = 0, defender = 0;
  bool attackerWins = true;
  Vec2 attackerOrigin;                                  // откуда пришёл нападавший
  std::map<Id, std::map<std::pair<Id, Id>, i64>> losses;  // армия -> (фракция, строка) -> потери
};
struct BattleOutcome {
  std::vector<Id> destroyed;          // уничтоженные объекты
  std::vector<Id> fallenHeroes;       // герои уничтоженных объектов (окно «Судьба героев»)
  Id winner = 0;                      // фракция-лидер победившей стороны (0 — победы нет)
  Id winnerArmy = 0;                  // уцелевший объект победителя
  Id province = 0;                    // место боя
  double corpses = 0;                 // трупы победителю (государство нежити, некроманты)
};
BattleOutcome resolveBattle(Tx& tx, const BattleResult& r);   // потери, смещение проигравшего, хроника

// ================================================================ штурм и захват провинции
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

// ================================================================ герои
enum class Fate : u8 { Fled, Killed, Captured };
// Судьба героя после уничтожения его войска: сбежал, убит (место захоронения), взят в плен (пленившее государство).
void heroFate(Tx& tx, Id character, Fate fate, Id captor, Id burial);
// Воскрешение погибшего героя: природа (schema::mod::Living/Undead/Demon/Mechanism) и государство.
void resurrect(Tx& tx, Id character, std::string_view natureKey, Id faction);
std::vector<Id> captivesOf(const World& w, Id state);   // пленники государства

// ================================================================ мятеж
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
// ================================================================ правила игрока: очко действий войска и флота
// Игрок перемещает войско (флот) за ход не дальше соседней провинции: перемещение в другую провинцию, посадка на флот
// и высадка с него тратят очко действий до следующего хода (Army::acted = номер хода). Перестановка внутри своей
// провинции очка не тратит.
bool hasAction(const World& w, Id army);                      // очко действий не истрачено в этом ходу
Id provinceAtPos(const World& w, Vec2 pos);                   // провинция под точкой (0 — нет)
bool provincesAdjacent(const World& w, Id a, Id b);           // общая граница
// Можно ли игроку поставить войско в pos (провинция та же — всегда; соседняя — при очке действий).
bool canPlayerMove(const World& w, Id army, Vec2 pos, std::string* why = nullptr);
// Перемещение тратит очко действий: позиция в другой провинции, чем from.
bool moveSpendsAction(const World& w, Id army, Vec2 from, Vec2 to);
void spendAction(Tx& tx, Id army);                            // истратить очко действий (войско и, если есть, его груз)

// Проверки размещения берут грани из кеша geo::faces: передавайте значение мира, а не черновик с изменённой геометрией.
std::optional<Vec2> findFreeSpot(const World& w, ArmyKind kind, Vec2 near, Id exclude = 0);   // спираль от near
bool validPosition(const World& w, ArmyKind kind, Vec2 pos, Id exclude = 0, std::string* why = nullptr);
Id armyAt(const World& w, Vec2 pos, Id exclude = 0);   // объект, фигурка которого покрывает точку

}  // namespace rg::rules
