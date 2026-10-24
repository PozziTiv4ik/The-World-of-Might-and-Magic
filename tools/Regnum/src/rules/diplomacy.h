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

// Перемирие: что отдаёт каждая сторона (TruceTerms, Truce — core/world.h). После заключения — состояние status
// (по умолчанию «Статус-кво»).
using rg::Truce;
using rg::TruceTerms;
i64 truceSlavesMax(const World& w, Id giver);
std::vector<std::string> truceProblems(const World& w, const Truce& t);
// Мир — с хода заключения: войну стороны не объявят друг другу до хода «ход + Мир после перемирия» (Relation::calm);
// взаимные оккупации сторон снимаются (уступленные провинции переходят оккупанту).
void concludeTruce(Tx& tx, const Truce& t);
// Захват всех земель (RULES.md §25): победителю — запасы побеждённого (кроме «Валют», пока у него есть войска и флот на
// карте), реликвии его склада и заключённые его тюрем. Текст для хроники; пусто — передавать нечего.
std::string conquestLoot(Tx& tx, Id loser, Id winner);

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
// Расторгнуть сделку «каждый ход», дань или репарации. by — расторгающее государство (0 — администратор, без
// последствий): дань и репарации отменяет только получатель; сторона, расторгающая сделку, — отношения «Расторжение
// сделки» (константа, −10).
void cancelDeal(Tx& tx, Id deal, Id by = 0);
// Война расторгает действующие сделки «каждый ход», дань и репарации пары (статус «расторгнута», запись в хронике).
void cancelWarDeals(Tx& tx, Id a, Id b);
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
