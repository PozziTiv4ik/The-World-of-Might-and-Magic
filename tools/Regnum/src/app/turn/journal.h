// Regnum — журнал действий игроков (для администратора): каждое игровое действие игрока (App::play) — строка журнала:
// ход, государство, действие (подпись транзакции) и время. Журнал лежит рядом с миром — history/journal.jsonl папки
// мира (строка — объект JSON; файл только дописывается, в World не входит); у архива .regnum и мира без файла — журнал
// сеанса в памяти. Запись — detail::journalPlay (app_internal.h), просмотр — блок раздела «Решения».
#pragma once
#include "app/app.h"

namespace rg::app::journal {

struct Entry {
  int turn = 0;
  Id state = 0;
  std::string stateName;   // название государства на момент действия
  std::string action;      // подпись действия («Начать строительство», «Готов завершить ход»)
  std::string at;          // время (ISO)
};

// Файл журнала мира (пусто — у мира нет папки: архив или мир без файла).
std::string fileOf(const App& a);
// Записи журнала мира, новые первыми: файл (кеш по размеру и времени файла) и записи сеанса.
std::vector<Entry> entries(App& a);

}  // namespace rg::app::journal
