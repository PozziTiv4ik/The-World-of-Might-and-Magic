// Regnum — история ходов (turn.cpp): снимки ходов в памяти и на диске, ветви после возврата, возврат к ходу.
#pragma once
#include <optional>

#include "app/app.h"
#include "core/io.h"

namespace rg::app::turnui {

// Снимки уникальны (ход + вид + номер) и не заменяют друг друга: начало хода (io::kSnapStart), конец хода
// перед расчётом (kSnapEnd), состояние перед возвратом (kSnapBranch). Ветвь текущего мира — цепочка parent
// от App::Impl::snapHead. Новые снимки живут в памяти до сохранения мира (pendingSnapshots).
// Запомнить мир как снимок вида kind (в памяти до сохранения); возвращает номер снимка.
u64 keepSnapshot(App& a, const World& w, const char* kind, const std::string& label, u64 parent);
// Мир снимка, который открывает строка хода turn в истории; nullopt — снимка нет (уведомление показано).
std::optional<World> loadTurnWorld(App& a, int turn);
// Вернуть мир к снимку хода: подтверждение, текущее состояние — в историю, затем замена мира.
// Ход turn — текущий: мир становится ровно таким, каким был в начале этого хода в текущей ветви.
void askRollback(App& a, int turn);
bool rollbackToTurn(App& a, int turn);
// После открытия или создания мира: история с диска, ветвь сохранённого мира, снимок начала текущего хода.
void syncHistory(App& a);
// Несохранённые снимки (сжатые) для записи при сохранении и номер ветви; historySaved — после записи.
std::vector<io::NewSnapshot> pendingSnapshots(App& a);
void historySaved(App& a);

}  // namespace rg::app::turnui
