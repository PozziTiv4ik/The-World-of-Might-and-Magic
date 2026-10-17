// Regnum — точка входа тестов.
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <thread>

#include "base/fs.h"
#include "base/process.h"
#include "tests/test.h"

namespace rg::test {

std::vector<Case>& registry() { static std::vector<Case> r; return r; }

void fail(const char* file, int line, const std::string& msg) {
  std::string f = file;
  size_t p = f.find_last_of("/\\");
  if (p != std::string::npos) f = f.substr(p + 1);
  throw Failure{f + ":" + std::to_string(line) + ": " + msg};
}

static std::string gOut;
std::string outDir() {
  if (gOut.empty()) {
    const char* env = std::getenv("REGNUM_TEST_OUT");
    gOut = env && *env ? env : ".wmma/regnum-tests";
  }
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(reinterpret_cast<const char8_t*>(gOut.c_str())), ec);
  return gOut;
}

}  // namespace rg::test

namespace {

using namespace rg;

struct Totals { int pass = 0, fail = 0; };

// Замеры времени (perf, speed, bench) при параллельном прогоне идут отдельно, после остальных: под нагрузкой
// соседних процессов их пороги не показательны.
bool timing(const test::Case& c) {
  std::string_view n = c.name;
  return n.find("perf") != n.npos || n.find("speed") != n.npos || n.find("bench") != n.npos;
}

// Последовательный прогон выбранных тестов (shardCount > 1 — только доля shard).
Totals runCases(const std::vector<test::Case*>& cases, int shard, int shardCount) {
  Totals t;
  for (size_t i = 0; i < cases.size(); i++) {
    if (shardCount > 1 && int(i % size_t(shardCount)) != shard) continue;
    auto& c = *cases[i];
    double s = nowSeconds();
    try {
      c.fn();
      t.pass++;
      double ms = (nowSeconds() - s) * 1000;
      if (ms > 300) std::printf("  ok   %s (%.0f ms)\n", c.name, ms);
    } catch (const test::Failure& f) {
      t.fail++;
      std::printf("  FAIL %s\n       %s\n", c.name, f.msg.c_str());
    } catch (const std::exception& e) {
      t.fail++;
      std::printf("  FAIL %s\n       exception: %s\n", c.name, e.what());
    } catch (...) {
      t.fail++;
      std::printf("  FAIL %s\n       unknown exception\n", c.name);
    }
    std::fflush(stdout);
  }
  return t;
}

// Параллельный прогон: тесты (кроме замеров времени) делятся на части вперемешку, jobs потоков по очереди
// запускают для каждой части дочерний процесс этой же программы со своим журналом — освободившийся поток берёт
// следующую часть, поэтому долгие тесты не задерживают остальные. Печатаются только провалы; итог — в той же
// строке, что и у последовательного прогона.
Totals runParallel(int jobs, size_t count, const std::vector<std::string>& filters) {
  const int parts = int(std::min<size_t>(count, size_t(jobs) * 4));
  std::string self = fs::exePath();
  std::string logs = fs::join(test::outDir(), "shards");
  fs::removeAll(logs);
  fs::makeDirs(logs);
  auto logOf = [&](int k) { return fs::join(logs, "part-" + std::to_string(k) + ".log"); };
  std::vector<int> codes(size_t(parts), -1);
  std::atomic<int> next{0};
  std::vector<std::thread> threads;
  for (int j = 0; j < jobs; j++) {
    threads.emplace_back([&] {
      for (int k; (k = next++) < parts;) {
        std::vector<std::string> args{"--shard=" + std::to_string(k) + "/" + std::to_string(parts), "--out=" + test::outDir()};
        args.insert(args.end(), filters.begin(), filters.end());
        codes[size_t(k)] = process::run(self, args, logOf(k));
      }
    });
  }
  for (auto& th : threads) th.join();
  Totals t;
  for (int k = 0; k < parts; k++) {
    std::string text = fs::readFile(logOf(k)).value_or("");
    bool summary = false, inFail = false;
    for (auto& line : split(text, '\n')) {
      int total = 0, pass = 0, failn = 0;
      if (std::sscanf(line.c_str(), "%*s tests: %d, passed: %d, failed: %d", &total, &pass, &failn) == 3) {
        t.pass += pass;
        t.fail += failn;
        summary = true;
        continue;
      }
      if (startsWith(line, "  FAIL ")) inFail = true;
      else if (!startsWith(line, "       ")) inFail = false;
      if (inFail) std::printf("%s\n", line.c_str());
    }
    if (!summary || (codes[size_t(k)] != 0 && codes[size_t(k)] != 1)) {
      t.fail++;
      std::printf("  FAIL часть %d/%d: процесс завершился аварийно (код %d), журнал: %s\n", k, parts, codes[size_t(k)], logOf(k).c_str());
    }
  }
  std::fflush(stdout);
  return t;
}

}  // namespace

// regnum-tests [-j N] [--list] [--out=папка] [фильтр ...]
//   фильтр  — выполняются тесты, в имени которых есть одна из подстрок;
//   -j N    — N параллельных процессов (-j 0 — по числу ядер); --list — только имена тестов.
int main(int argc, char** argv) {
  using namespace rg;
  std::vector<std::string> filters;
  int jobs = 1, shard = 0, shardCount = 1;
  bool list = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a.rfind("--out=", 0) == 0) { test::gOut = a.substr(6); continue; }
    if (a.rfind("--shard=", 0) == 0) { std::sscanf(a.c_str() + 8, "%d/%d", &shard, &shardCount); continue; }
    if (a == "--list") { list = true; continue; }
    if (a == "-j" && i + 1 < argc) { jobs = std::atoi(argv[++i]); continue; }
    if (a.rfind("-j", 0) == 0 && a.size() > 2) { jobs = std::atoi(a.c_str() + 2); continue; }
    filters.push_back(a);
  }
  std::vector<test::Case*> cases;
  for (auto& c : test::registry()) {
    std::string n = c.name;
    bool any = filters.empty();
    for (auto& f : filters) if (n.find(f) != std::string::npos) any = true;
    if (any) cases.push_back(&c);
  }
  if (list) {
    for (auto* c : cases) std::printf("%s\n", c->name);
    return 0;
  }
  if (jobs <= 0) jobs = int(std::max(1u, std::thread::hardware_concurrency() / 2));
  std::vector<test::Case*> general, timed;
  for (auto* c : cases) (timing(*c) ? timed : general).push_back(c);
  jobs = std::min(jobs, int(general.size()));
  double t0 = nowSeconds();
  Totals t;
  if (shardCount > 1) {
    t = runCases(general, shard, shardCount);
  } else if (jobs > 1 && !general.empty()) {
    t = runParallel(jobs, general.size(), filters);
    Totals tt = runCases(timed, 0, 1);
    t.pass += tt.pass;
    t.fail += tt.fail;
  } else {
    t = runCases(cases, 0, 1);
  }
  std::printf("%s tests: %d, passed: %d, failed: %d (%.2f s)\n", t.fail ? "FAILED" : "OK", t.pass + t.fail, t.pass, t.fail, nowSeconds() - t0);
  return t.fail ? 1 : 0;
}
