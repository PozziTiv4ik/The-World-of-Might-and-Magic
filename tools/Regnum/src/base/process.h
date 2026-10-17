// Regnum — запуск дочерних процессов (параллельный прогон тестов).
#pragma once
#include "base/base.h"

namespace rg::process {

// Запустить программу exe с аргументами args и дождаться завершения. Вывод (stdout и stderr) пишется
// в файл outFile (создаётся заново). Пути и аргументы — UTF-8. Возвращает код выхода; −1 — не удалось запустить.
int run(const std::string& exe, const std::vector<std::string>& args, const std::string& outFile);

}  // namespace rg::process
