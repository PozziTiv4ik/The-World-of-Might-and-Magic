// Regnum — минимальный тестовый фреймворк (без внешних библиотек).
//
//   TEST(geo_split_square) { CHECK(x > 0); CHECK_EQ(a, b); CHECK_NEAR(v, 1.0, 1e-9); CHECK_THROWS(f()); }
//
// Запуск: regnum-tests [-j N] [--list] [фильтр ...] — выполняются тесты, в имени которых есть подстрока фильтра;
// -j N — в N параллельных процессах (-j 0 — по числу ядер), --list — только имена.
// Файл — test_<модуль>_<функция>.cpp, имя теста — <модуль>_<функция>_<что проверяется>
// (app_turn_history_restart_after_rollback, core_io_roundtrip_folder): фильтр по началу имени выбирает модуль или
// функцию. Тесты скорости — со словом perf, speed или bench в имени. Устройство набора и общие помощники —
// docs/ARCHITECTURE.md, §7.
// Каталог для артефактов (PNG и т. п.): rg::test::outDir() (по умолчанию .voidlight/regnum-tests); запись PNG — tests/test_png.h.
//
// Переменные окружения: REGNUM_PERF_STRICT=1 — строгие пороги скорости (perf()); REGNUM_AUDIT=1 — проверки аудита
// (AUDIT_EXPECT, auditStrict()) роняют тест; REGNUM_BENCH=1 — долгие замеры (иначе они пропускаются).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "base/base.h"

namespace rg::test {

struct Case { const char* name; const char* file; int line; std::function<void()> fn; };
std::vector<Case>& registry();
struct Reg { Reg(const char* n, const char* f, int l, std::function<void()> fn) { registry().push_back({n, f, l, std::move(fn)}); } };

struct Failure { std::string msg; };
void fail(const char* file, int line, const std::string& msg);  // бросает Failure
std::string outDir();                                            // каталог артефактов (создаётся)
// Переменная окружения задана и не «0».
inline bool envFlag(const char* name) {
  const char* v = std::getenv(name);
  return v && *v && *v != '0';
}
// Число из переменной окружения (def — не задана или пуста).
inline int envInt(const char* name, int def) {
  const char* v = std::getenv(name);
  return v && *v ? std::atoi(v) : def;
}
// Порог производительности: строгий при REGNUM_PERF_STRICT=1, иначе с запасом ×4 (машина может быть загружена
// параллельными сборками — тест ловит только катастрофическое замедление).
inline double perf(double limit) { return envFlag("REGNUM_PERF_STRICT") ? limit : limit * 4; }

// Аудит — проверки ожидаемого поведения, которое пока не выполняется, и находки обходов интерфейса (fuzz): без
// REGNUM_AUDIT расхождение только печатается (общий прогон не падает), при REGNUM_AUDIT=1 — роняет тест. Исправив
// дефект, AUDIT_EXPECT заменяют на CHECK.
inline bool auditStrict() { return envFlag("REGNUM_AUDIT"); }
inline void auditFail(const char* file, int line, const char* cond, const std::string& msg) {
  std::string f = file;
  if (size_t p = f.find_last_of("/\\"); p != std::string::npos) f = f.substr(p + 1);
  std::printf("  AUDIT %s:%d: %s — %s\n", f.c_str(), line, cond, msg.c_str());
  std::fflush(stdout);
  if (auditStrict()) fail(file, line, "AUDIT: " + msg);
}

// Текст UserError, брошенной f (пусто — исключения не было).
template <class F> std::string errorOf(F&& f) {
  try {
    f();
  } catch (const UserError& e) {
    return e.what();
  }
  return {};
}

template <class T> std::string show(const T& v) {
  if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> || std::is_same_v<T, const char*>) return "\"" + std::string(v) + "\"";
  else if constexpr (std::is_enum_v<T>) return std::to_string(int(v));
  else if constexpr (requires(std::ostream& o, const T& x) { o << x; }) { std::ostringstream s; s << v; return s.str(); }
  else return "<?>";
}

}  // namespace rg::test

#define RG_CAT2(a, b) a##b
#define RG_CAT(a, b) RG_CAT2(a, b)
#define TEST(name)                                                                              \
  static void RG_CAT(test_fn_, name)();                                                          \
  static ::rg::test::Reg RG_CAT(test_reg_, name)(#name, __FILE__, __LINE__, RG_CAT(test_fn_, name)); \
  static void RG_CAT(test_fn_, name)()

#define CHECK(cond) do { if (!(cond)) ::rg::test::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)
#define CHECK_MSG(cond, msg) do { if (!(cond)) ::rg::test::fail(__FILE__, __LINE__, std::string("CHECK(" #cond "): ") + (msg)); } while (0)
// Значения копируются: ссылка на часть временного объекта (load(dir).world.province(1)->name) не пережила бы строку.
#define CHECK_EQ(a, b) do { const auto _a = (a); const auto _b = (b); if (!(_a == _b)) ::rg::test::fail(__FILE__, __LINE__, std::string("CHECK_EQ(" #a ", " #b "): ") + ::rg::test::show(_a) + " != " + ::rg::test::show(_b)); } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = double(a), _b = double(b); if (!(std::fabs(_a - _b) <= (eps))) ::rg::test::fail(__FILE__, __LINE__, std::string("CHECK_NEAR(" #a ", " #b "): ") + std::to_string(_a) + " vs " + std::to_string(_b)); } while (0)
#define AUDIT_EXPECT(cond, msg) do { if (!(cond)) ::rg::test::auditFail(__FILE__, __LINE__, #cond, (msg)); } while (0)
#define CHECK_THROWS(expr) do { bool _t = false; try { (void)(expr); } catch (...) { _t = true; } if (!_t) ::rg::test::fail(__FILE__, __LINE__, "CHECK_THROWS(" #expr ")"); } while (0)
