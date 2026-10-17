// Regnum — правила: дипломатия, перемирие и вассалитет, сделки, дань и репарации, гильдии и торговые маршруты (rules/diplomacy.cpp, truce.cpp, trade.cpp, guilds.cpp, routes.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>

#include "core/world.h"

namespace rg::rules {

// ================================================================ дипломатия
// Состояние «В войне» меняется на другое только перемирием: иначе отказ.
void setRelation(Tx& tx, Id a, Id b, double value, RelStatus status);
// value — хранимое значение; bonus — смещение за одну религию (+10): действующее отношение — relationValue.
struct RelationRow { Id other = 0; double value = 0; RelStatus status = RelStatus::Unknown; double bonus = 0; };
std::vector<RelationRow> relationsOf(const World& w, Id faction);   // все прочие фракции (кроме «Без государства»)
void shiftRelation(Tx& tx, Id a, Id b, double delta);              // отношения ± delta (−100…100)
// Одна религия у двух государств: отношения всегда смещены на +10. relationValue — значение
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

// Вассалитет.
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

// ================================================================ гильдии и маршруты
void buildHq(Tx& tx, Id guild, Id province);
void removeHq(Tx& tx, Id guild, Id province);
void setInfluence(Tx& tx, Id province, Id guild, double pct);   // сумма по провинции ≤ 100
void setHomeState(Tx& tx, Id guild, Id state);                  // запрещено для государственной гильдии
Id createStateGuild(Tx& tx, Id state, const std::string& name = {});
Id createRoute(Tx& tx, const std::vector<Vec2>& pts, Id guild = 0);
void setRoutePoints(Tx& tx, Id route, const std::vector<Vec2>& pts);
void removeRoute(Tx& tx, Id route);

}  // namespace rg::rules
