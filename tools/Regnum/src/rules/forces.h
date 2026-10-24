// Regnum — правила: формирование и роспуск отрядов, флот в торговле, оккупационный гарнизон, верность войск и гарнизонов, войска без государства (rules/forces.cpp, rules/military.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>

#include "core/world.h"

namespace rg::rules {

// ================================================================ формирование и резерв
// Отряды — из населения государства (нежить — из трупов, демоны — из демонической
// энергии), корабли — за ресурсы констант стоимости; через 2 хода — в резерв.
// people — из населения (живые; нежить и демоны — трупы и энергия в res); res — ресурсы (ключевой, дополнительные,
// трупы, энергия, стоимость кораблей); ess — эссенции элементов.
struct RecruitCost { i64 people = 0; std::map<Id, double> res, ess; std::vector<std::string> problems; };
RecruitCost recruitCost(const World& w, Id faction, Id row, i64 count);
void recruit(Tx& tx, Id faction, Id row, i64 count);
void cancelFormation(Tx& tx, Id faction, int index);          // возврат людей и ресурсов
void disbandReserve(Tx& tx, Id faction, Id row, i64 count);   // роспуск резерва: воины → население
// Корабли из резерва в торговле и оккупационный гарнизон (отряды из резерва оккупанта; пополнить — только когда в
// провинции стоит его войско, уменьшить — всегда).
void setTradeFleet(Tx& tx, Id faction, Id row, i64 count);
void setOccupationGarrison(Tx& tx, Id province, Id row, i64 count);
// Верность войска −100…100 % (с «Непреклонным лоялистом» не уменьшается, у «Армии нежити» — 100 %).
void setArmyLoyalty(Tx& tx, Id army, double loyalty);
// Эссенции элементов фракции (начальные запасы и правка).
void setEssence(Tx& tx, Id faction, Id essence, double amount);

// ================================================================ гарнизон
// Герой владельца в гарнизоне: доступный, не в войске и не в другом гарнизоне.
void setGarrisonHero(Tx& tx, Id province, Id character, bool on);
Id heroGarrison(const World& w, Id character);                 // провинция, в гарнизоне которой герой; 0 — нет
// Верность гарнизона −100…100 % (как у войска: модификаторы государства и «Непреклонный лоялист», у государства
// нежити — всегда 100 %).
void setGarrisonLoyalty(Tx& tx, Id province, double loyalty);
double garrisonLoyaltyDelta(const World& w, Id province);

// ================================================================ войска без государства
// Фракция «Без государства» (одна на мир, создаётся при первом обращении): её войска враждебны всем государствам.
Id wildFaction(const World& w);                      // 0 — ещё нет
// Войско без государства у точки near (на суше рядом): строки отрядов создаются у фракции «Без государства».
struct WildUnitSpec { std::string name; UnitType type = UnitType::LightInf; std::string race; i64 count = 0; };
Id spawnWildArmy(Tx& tx, Vec2 near, const std::string& name, const std::vector<WildUnitSpec>& units);

}  // namespace rg::rules
