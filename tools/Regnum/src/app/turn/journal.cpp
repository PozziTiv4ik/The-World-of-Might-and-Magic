// Regnum — журнал действий игроков (см. journal.h).
#include "app/turn/journal.h"

#include <fstream>

#include "app/app_internal.h"
#include "base/fs.h"
#include "base/json.h"

namespace rg::app {

namespace journal {

namespace {

// Журнал сеанса: записи мира без папки и последняя запись (подряд идущие одинаковые действия — правка ползунком, ввод
// числа — пишутся один раз за 2 секунды).
struct Session {
  std::vector<Entry> list;
  std::string lastKey;
  double lastAt = -1e9;
};
Session& session() { return sessionState<Session>(); }

std::string lineOf(const Entry& e) {
  json::Value v = json::Value::object();
  v.set("turn", double(e.turn));
  v.set("state", double(e.state));
  v.set("stateName", e.stateName);
  v.set("action", e.action);
  v.set("at", e.at);
  return json::write(v, json::WriteOptions{0, false, false}) + "\n";
}

std::optional<Entry> parseLine(std::string_view line) {
  std::optional<json::Value> v = json::tryParse(line);
  if (!v || !v->isObj()) return std::nullopt;
  Entry e;
  e.turn = int(v->integer("turn"));
  e.state = Id(std::max<i64>(0, v->integer("state")));
  e.stateName = v->str("stateName");
  e.action = v->str("action");
  e.at = v->str("at");
  if (e.action.empty()) return std::nullopt;
  return e;
}

}  // namespace

std::string fileOf(const App& a) {
  if (a.projectPath().empty() || a.projectIsBundle()) return {};
  return fs::join(a.projectPath(), "history/journal.jsonl");
}

std::vector<Entry> entries(App& a) {
  struct Cache {
    std::string file;
    u64 size = 0;
    i64 mtime = 0;
    std::vector<Entry> list;   // по порядку файла
  };
  static Cache c;
  std::vector<Entry> out;
  const std::string file = fileOf(a);
  if (!file.empty() && fs::isFile(file)) {
    const u64 size = fs::fileSize(file).value_or(0);
    const i64 mt = fs::mtime(file).value_or(0);
    if (c.file != file || c.size != size || c.mtime != mt) {
      c = Cache{file, size, mt, {}};
      if (auto text = fs::readFile(file)) {
        size_t p = 0;
        while (p < text->size()) {
          size_t q = text->find('\n', p);
          if (q == std::string::npos) q = text->size();
          if (q > p)
            if (auto e = parseLine(std::string_view(*text).substr(p, q - p))) c.list.push_back(std::move(*e));
          p = q + 1;
        }
      }
    }
    out = c.list;
  }
  for (const Entry& e : session().list) out.push_back(e);
  std::reverse(out.begin(), out.end());
  return out;
}

}  // namespace journal

void detail::journalPlay(App& a, std::string_view label) {
  if (!a.player()) return;
  const World& w = a.store.world();
  journal::Entry e;
  e.turn = w.turn();
  e.state = a.ui.playerState;
  e.stateName = w.factionName(e.state);
  e.action = std::string(label);
  e.at = nowIso();
  journal::Session& s = journal::session();
  const std::string key = std::to_string(e.turn) + "|" + std::to_string(e.state) + "|" + e.action;
  const double now = nowSeconds();
  if (key == s.lastKey && now - s.lastAt < 2) {
    s.lastAt = now;
    return;
  }
  s.lastKey = key;
  s.lastAt = now;
  const std::string file = journal::fileOf(a);
  if (file.empty()) {
    s.list.push_back(std::move(e));
    return;
  }
  std::string err;
  if (!fs::makeDirs(fs::parent(file), &err)) {
    logWarn("Журнал действий: %s", err.c_str());
    s.list.push_back(std::move(e));
    return;
  }
  std::ofstream out(fs::path(file), std::ios::binary | std::ios::app);
  if (!out) {
    logWarn("Журнал действий: не удалось открыть %s", file.c_str());
    s.list.push_back(std::move(e));
    return;
  }
  out << journal::lineOf(e);
}

}  // namespace rg::app
