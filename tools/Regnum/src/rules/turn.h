// Regnum — правила: хроника и расчёт хода (rules/log.cpp, rules/turn.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>

#include "core/world.h"
#include "rules/plots.h"

namespace rg::rules {

// ================================================================ хроника и ход
struct LogRefs { Id province = 0, army = 0; std::vector<Id> factions; };
Id addLog(Tx& tx, LogKind kind, const std::string& text, const LogRefs& refs = {});

struct TurnFactionLine {
  Id faction = 0;
  double treasuryBefore = 0, treasuryAfter = 0, income = 0, expenses = 0;
  std::map<Id, double> resources;     // изменение запасов ресурсов (кроме золота)
  std::map<Id, double> essences;      // изменение запасов эссенций элементов
  double provisionDebt = 0;           // недостача провизии после хода (голод, если больше нуля)
};
// События хода, которые требуют решения после него: восстание провинции (армия мятежников — битва с войском,
// гарнизоном или захват) и мятеж войска с верностью −100 % (верные герои — «Судьба героя»).
struct TurnEvent {
  enum Kind : u8 { Uprising, Mutiny } kind = Uprising;
  Id army = 0;                        // армия мятежников
  Id province = 0;
  Id origin = 0, rebelState = 0;
  std::vector<Id> heroes;
};
struct TurnReport {
  int turnFrom = 0, turnTo = 0;
  std::vector<TurnFactionLine> factions;
  std::vector<Id> logIds;            // записи хроники, созданные при завершении хода
  std::vector<Id> rebellions;        // провинции, где вспыхнуло восстание
  std::vector<TurnEvent> events;
  std::vector<HitOutcome> hits;      // исходы заказных убийств заговоров
};
// Игрок отметил готовность завершить ход за государство (ход завершает администратор; отметка действует до конца
// текущего хода — Faction::readyTurn).
bool turnReady(const World& w, Id state);
void setTurnReady(Tx& tx, Id state, bool ready);
// Шаг 1 (снимок мира в истории ходов) выполняет вызывающий: снимок — store.world() до транзакции (core/io).
TurnReport endTurn(Tx& tx);
TurnReport previewTurn(const World& w);   // без изменения мира; logIds указывают на записи пробного хода

}  // namespace rg::rules
