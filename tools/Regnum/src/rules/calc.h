// Regnum — правила: полный расчёт мира: провинции, фракции, ресурсы и эссенции, развёрнутые силы (rules/calc.cpp, rules/forces.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>
#include <unordered_map>

#include "rules/effects.h"

namespace rg::rules {

// ================================================================ расчёты
struct GuildShare { Id guild = 0; double pct = 0; bool hq = false; double gross = 0, tax = 0, net = 0; };

struct ProvinceCalc {
  Id id = 0;
  bool sea = false;
  Id owner = 0;
  Id recipient = 0;                   // кто получает доход (оккупация по настройкам)
  CityType city = CityType::Outpost;  // тип города по населению
  int slotsSize = 0, slotsCity = 0, slotsMods = 0, slots = 0, slotsUsed = 0;
  double tradeBase = 0, tradeValue = 0;
  int routes = 0;
  double production = 0;              // добыча ресурса за ход
  double extractPct = 0;              // постройки добычи: добыча ресурса провинции +%
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
// Эссенция элемента фракции за ход: генерация построек (и иссушения населения) и модификаторов (провинций,
// государства, героев), содержание элементалей и расход построек преобразования (может увести запас в долг).
struct EssenceFlow { double stock = 0, generation = 0, upkeep = 0, conversionIn = 0, net = 0; };
// Шаг постройки преобразования в конце хода (план расчёта, его же исполняет endTurn): start — начинается цикл (ресурсы
// на входе списываются), finish — цикл завершается (ресурс на выходе поступает), cycle — ходов до конца после хода.
struct ConvertStep {
  Id province = 0, building = 0;
  bool start = false, finish = false;
  int cycle = 0;
  std::map<Id, double> in;            // списывается при начале цикла
  std::map<Id, double> essIn;         // эссенции, списываемые при начале цикла (через поток эссенций хода)
  ResAmount out;                      // поступает при завершении
  int recipe = 0;                     // рецепт постройки
};
// Постройка иссушения в конце хода: у расы race провинции отнимается pop жителей, государство получает эссенции ess.
struct DrainStep {
  Id province = 0, building = 0, race = 0;
  i64 pop = 0;
  std::map<Id, double> ess;
};

struct FactionCalc {
  Id id = 0;
  std::vector<Id> provinces;          // государство: владения; гильдия: провинции штабов
  i64 population = 0;
  std::vector<std::pair<Id, i64>> races;               // по провинциям
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
  std::vector<DrainStep> drains;      // постройки иссушения (их же исполняет endTurn)
  double upkeepCut = 0;               // снижение содержания армии постройками, % (не больше 50)
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
// Тип города провинции — по её населению (schema::cityTypeFor).
CityType cityType(const Province& p);

}  // namespace rg::rules
