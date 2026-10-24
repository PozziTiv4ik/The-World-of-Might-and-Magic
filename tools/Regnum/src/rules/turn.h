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

// Строки итогов, события и исходы заказов — core/world.h (TurnFactionLine, TurnEvent, HitOutcome, TurnRecord):
// итоги хранятся в мире (Meta::lastTurn), неразобранные события — Meta::turnEvents.
using rg::HitOutcome;
using rg::TurnEvent;
using rg::TurnFactionLine;
using rg::TurnRecord;
struct TurnReport : TurnRecord {
  std::vector<TurnEvent> events;
};
// Игрок отметил готовность завершить ход за государство (ход завершает администратор; отметка действует до конца
// текущего хода — Faction::readyTurn).
bool turnReady(const World& w, Id state);
void setTurnReady(Tx& tx, Id state, bool ready);
// Шаг 1 (снимок мира в истории ходов) выполняет вызывающий: снимок — store.world() до транзакции (core/io).
TurnReport endTurn(Tx& tx);
TurnReport previewTurn(const World& w);   // без изменения мира; logIds указывают на записи пробного хода

}  // namespace rg::rules
