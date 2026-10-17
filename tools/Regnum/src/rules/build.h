// Regnum — правила: строительство в провинциях, требования и возможности построек, слоты, чума (rules/build.cpp, rules/plague.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>

#include "core/world.h"

namespace rg::rules {

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
// player — правила игрока: в провинции за ход начинается не больше одной постройки (Province::buildTurn).
std::vector<BuildOption> buildOptions(const World& w, Id province, bool player = false);
void startBuilding(Tx& tx, Id province, Id building, bool player = false);  // новый или следующий уровень
void cancelBuilding(Tx& tx, Id province, Id building); // полный возврат стоимости
void demolish(Tx& tx, Id province, Id building);
// Мгновенно завершить строящийся уровень (уплаченное остаётся уплаченным).
void completeBuilding(Tx& tx, Id province, Id building);
// Поставить готовую постройку уровня level без цены и срока (изначальные постройки провинции): те же условия, что у
// строительства, кроме цены. Уже есть — уровень меняется (строящуюся сначала отмените).
void placeBuilding(Tx& tx, Id province, Id building, int level);
// Постройка преобразования или иссушения в провинции: «Простаивает» (idle) или «Работает».
void setConvertIdle(Tx& tx, Id province, Id building, bool idle);
// Выбранный рецепт постройки преобразования в провинции (пока не идёт цикл).
void setPbRecipe(Tx& tx, Id province, Id building, int recipe);
// Раса провинции, у которой постройка иссушения отнимает население (0 — не выбрана: постройка простаивает).
void setDrainRace(Tx& tx, Id province, Id building, Id race);
// Ресурс подходит постройке добычи (группы extractGroups с подгруппами, кроме extractExclude); не добыча — всегда.
bool extractAllowed(const World& w, const Building& b, Id res);
// Требование «на государство» (соборы): чтобы построить (n + 1)-ю постройку, нужно per × (n + 1) достроенных req.
void setStateReq(Tx& tx, Id building, Id req, int per);  // per 0 — снять
// Возможности постройки: хранилище реликвий, целительство, чума, верфь, гильдия наёмников,
// только в приморской провинции. Выключение снимает их данные (реликвии хранилищ — государству-владельцу, типы
// кораблей уровней).
enum class BuildingFlag : u8 { RelicStore, Healing, Plague, Shipyard, Mercenary, Coastal, Market };
void setBuildingFlag(Tx& tx, Id building, BuildingFlag flag, bool on);
void setLevelShips(Tx& tx, Id building, int level, u32 ships);
void setLevelEssCost(Tx& tx, Id building, int level, Id essence, double amount);   // 0 — убрать
// Производство уровня при ресурсе провинции res (лесопилка в провинции с древесиной): amount 0 — убрать.
void setLevelProduceOnRes(Tx& tx, Id building, int level, Id res, double amount);
void setLevelExtract(Tx& tx, Id building, int level, double pct);              // постройка добычи: +% добычи
// Постройка иссушения: жителей за ход и эссенция за них (essence 0 — только жители; amount 0 — убрать эссенцию).
void setLevelDrain(Tx& tx, Id building, int level, i64 pop, Id essence = 0, double amount = 0);
void setLevelUpkeepCut(Tx& tx, Id building, int level, double pct);            // снижение содержания армии, %
// Постройка добычи: группы ресурсов провинции, которые она добывает, и исключённые ресурсы.
void setExtractGroups(Tx& tx, Id building, const std::vector<Id>& groups, const std::vector<Id>& exclude);
// Виды государства, которым доступна постройка (бит = StateKind; 0 — всем).
void setBuildingStateKinds(Tx& tx, Id building, u32 kinds);
// Провинция приморская: граничит с морем (берег или морская провинция).
bool isCoastal(const World& w, Id province);
// Морские провинции, соседние с провинцией.
std::vector<Id> seaNeighbors(const World& w, Id province);
// Требования построек: общая постройка зависит только от общих построек и технологий,
// уникальная — ещё и от построек и технологий своего государства; без циклов.
bool canRequireBuilding(const World& w, Id building, Id req, std::string* why = nullptr);
bool canRequireTech(const World& w, Id building, Id tech, std::string* why = nullptr);
void setBuildingReq(Tx& tx, Id building, Id req, int level);  // level 0 — снять
void setBuildingTech(Tx& tx, Id building, Id tech, bool on);
std::vector<Id> techUnlocks(const World& w, Id tech);          // постройки, которые требуют технологию
// Культовая постройка: общая — одна на всю карту, уникальная — одна у своего государства.
// Провинция, где она уже есть или строится (кроме exceptProvince); 0 — нигде.
Id cultBuiltIn(const World& w, Id building, Id exceptProvince = 0);
// Рецепты постройки преобразования: до трёх разных ресурсов и эссенций на входе, срок цикла ≥ 1 хода; index ==
// число рецептов — новый рецепт в конец. Удаление рецепта переводит провинции с ним на первый.
void setRecipe(Tx& tx, Id building, int index, const Recipe& r);
void removeRecipe(Tx& tx, Id building, int index);
std::string recipeTitle(const World& w, const Recipe& r);      // название или ресурс на выходе
const Recipe* activeRecipe(const Building& b, const ProvBuilding& pb);   // выбранный в провинции; nullptr — рецептов нет
// Дополнительные возможности постройки: преобразование, генерация эссенции, доступ к особым отрядам, иссушение
// населения, снижение содержания армии, добыча ресурса провинции. Выключение снимает их данные (рецепты и циклы в
// провинциях, эссенции уровней, список особых отрядов, расы иссушения, проценты уровней, группы добычи).
enum class BuildingRole : u8 { Convert, Essence, Special, Drain, UpkeepCut, Extract };
void setBuildingRole(Tx& tx, Id building, BuildingRole role, bool on);
void setLevelEssence(Tx& tx, Id building, int level, Id essence, double perTurn);   // 0 — убрать
void setBuildingSpecial(Tx& tx, Id building, Id special, bool on);
// Слоты: провинции, где после изменения мира построек больше слотов, — и какие постройки будут снесены (с конца
// списка): предупреждение до действия.
struct SlotLoss { Id province = 0; int slots = 0, used = 0; std::vector<Id> buildings; };
std::vector<SlotLoss> slotLosses(const World& before, const World& after);
// Снести лишние постройки (возврат за начатое): only — только эти провинции и постройки (nullptr — все лишние).
void trimExcessBuildings(Tx& tx, const std::vector<SlotLoss>* only = nullptr);

// ================================================================ чума
// Чуму можно установить: сухопутная провинция без чумы, без «Временного иммунитета» и не пустошь нежити.
bool canPlague(const World& w, Id province, std::string* why = nullptr);
// Установить чуму, если можно (true — установлена).
bool infectPlague(Tx& tx, Id province);
// «Вылечить провинцию» постройкой целительства (500 любой эссенции владельца или 500 лечебных трав — essence 0) и
// «Заразить чумой» постройкой чумы (2500 эссенции чумы).
void healProvince(Tx& tx, Id province, Id building, Id essence);
Id healingHerbs(const World& w);                               // ресурс «Лечебные травы» (0 — нет в справочнике)
void plagueProvince(Tx& tx, Id province, Id building);
// В провинции достроена постройка с возможностью (целительство, чума).
bool hasBuildingRole(const World& w, Id province, BuildingFlag flag);

}  // namespace rg::rules
