// Regnum — правила: технологии: изучение общего дерева и деревьев фракций, стоимость, связи, расстановка (rules/tech.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include "core/schema.h"
#include "core/world.h"

namespace rg::rules {

// ================================================================ технологии
// Общее дерево технологий (Tech::faction == 0) изучает каждая фракция отдельно
// (Faction::techs); faction — изучающая фракция (для технологии своего дерева — 0 или её фракция).
// missing — не изученные условия; problems — не достроены нужные постройки, не хватает ресурсов на стоимость.
struct ResearchCheck { bool ok = false; std::vector<Id> missing; std::vector<std::string> problems; };
TechProgress techState(const World& w, Id tech, Id faction = 0);
bool techStudied(const World& w, Id tech, Id faction = 0);
// Одновременных исследований фракции — не больше константы «Одновременных исследований» (не меньше 1): свои и общие
// технологии вместе. researchCount — сколько исследований фракции идёт сейчас. Сверх лимита canResearch даёт причину в
// problems (у идущего исследования — нет), startResearch отказывает.
int researchSlots(const World& w);
int researchCount(const World& w, Id faction);
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
using schema::kTreeColStep, schema::kTreeRowStep;      // шаг столбцов (слоёв) и строк autoLayout

}  // namespace rg::rules
