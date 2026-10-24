// Regnum — правила: флот: вместимость, посадка и высадка, обмен отрядами, союзные войска, флот у верфи (rules/naval.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>

#include "core/schema.h"
#include "core/world.h"

namespace rg::rules {

// ================================================================ флот: вместимость, посадка и высадка, обмен отрядами, союзные
// войска, флот у верфи
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
// Посадка на флот: войско и флот одного государства (все фракции войска есть во флоте),
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
// Ближайшее к флоту свободное место на суше по правилам высадки (canLand); nullopt — высадиться некуда.
std::optional<Vec2> landingSpot(const World& w, Id fleet);
// Перегруз: вместимость флота меньше войска на борту (потери кораблей в бою) — флот не плывёт, только высаживает войско.
bool fleetOverloaded(const World& w, Id fleet, std::string* why = nullptr);
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
// «Вернуть войско»: группа фракции союзного объекта (кроме лидера) отделяется в отдельный
// объект рядом — её отряды и герои; войско на борту союзного флота уходит вместе с группой своей фракции.
bool canReturnGroup(const World& w, Id army, Id faction, std::string* why = nullptr);
Id returnGroup(Tx& tx, Id army, Id faction);
// Флот у верфи: провинции фракции (государство — свои неоккупированные, гильдия — со штабом),
// приморские, с достроенной верфью; новый флот — на свободном месте моря у берега провинции.
std::vector<Id> shipyardProvinces(const World& w, Id faction);
std::optional<Vec2> fleetSpot(const World& w, Id province);
Id placeFleet(Tx& tx, Id faction, Id province);

}  // namespace rg::rules
