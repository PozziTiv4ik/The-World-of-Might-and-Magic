// Regnum — папка проекта, архив .regnum, снимки ходов, резервные копии, внешние изменения,
// недавние проекты и автосохранение.
#include <charconv>
#include <chrono>
#include <ctime>

#include "base/fs.h"
#include "base/jobs.h"
#include "codec/zip.h"
#include "codec/zlib.h"
#include "core/content.h"
#include "core/io_internal.h"

namespace rg::io {

using namespace detail;
using json::Value;

// ================================================================ общее
std::string Warning::text() const {
  std::string s = file;
  if (!where.empty()) {
    if (!s.empty()) s += ": ";
    s += where;
  }
  if (!s.empty()) s += ": ";
  return s + msg;
}

const std::vector<ProjectFile>& projectFiles() {
  static const std::vector<ProjectFile> list = [] {
    std::vector<ProjectFile> v;
    for (const FileDef& f : kFiles) v.push_back({f.path, f.tables});
    return v;
  }();
  return list;
}

u32 tablesOfFile(std::string_view rel) {
  int i = fileIndex(rel);
  return i < 0 ? 0 : kFiles[i].tables;
}

bool isBundlePath(const std::string& path) { return utf8::lower(fs::ext(path)) == kBundleExt; }

namespace {

constexpr const char* kWorldFile = "world.json";
constexpr const char* kHistoryDir = "history";
constexpr const char* kHistoryIndex = "history/index.json";
constexpr size_t kMaxUnpacked = size_t(1) << 30;  // предел распаковки снимка (1 ГБ)

std::string bytesStr(std::span<const u8> b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }

i64 wallMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Окно «гонки» времени файла: запись другой программой в ту же единицу времени файловой системы
// (FAT — 2 с) не меняет mtime, поэтому свежие файлы сверяются по содержимому.
constexpr i64 kRacyMs = 2000;

FileStamp stampOf(const std::string& data, std::optional<i64> mtime, u64 gen) {
  FileStamp s;
  s.exists = true;
  s.size = data.size();
  s.crc = codec::crc32(data);
  s.mtime = mtime.value_or(0);
  s.checked = wallMs();
  s.gen = gen;
  return s;
}

// ---------------------------------------------------------------- метка поколения
// Член верхнего уровня в тексте файла (json::write с отступом 2 и запись по строке на элемент): «\n  "generation": N».
constexpr std::string_view kGenLine = "\n  \"generation\": ";

// Где в тексте метка вместе с запятой-разделителем: [начало, конец) для удаления; nullopt — метки нет (или текст не
// в принятом виде).
std::optional<std::pair<size_t, size_t>> genSpan(std::string_view text) {
  const size_t p = text.find(kGenLine);
  if (p == std::string_view::npos) return std::nullopt;
  size_t e = p + kGenLine.size();
  const size_t digits = e;
  while (e < text.size() && text[e] >= '0' && text[e] <= '9') e++;
  if (e == digits || e >= text.size()) return std::nullopt;
  if (text[e] == ',') return std::make_pair(p, e + 1);   // не последний член: «\n  "generation": N,» перед следующим
  if (text[e] == '\n' && p > 0 && text[p - 1] == ',') return std::make_pair(p - 1, e);   // последний: «,\n  "generation": N»
  return std::nullopt;
}

// Текст на диске совпадает с text (без метки) без учёта своей метки поколения — без копирования.
bool sameBody(std::string_view disk, std::string_view text) {
  if (disk == text) return true;
  auto s = genSpan(disk);
  if (!s) return false;
  const size_t cut = s->second - s->first;
  return disk.size() - cut == text.size() && disk.substr(0, s->first) == text.substr(0, s->first) &&
         disk.substr(s->second) == text.substr(s->first);
}

// Вставить метку в текст без метки (на место по алфавиту ключей верхнего уровня).
void insertGeneration(std::string& out, u64 gen) {
  if (!gen) return;
  const std::string member = std::string(kGenLine) + std::to_string(gen);
  // Ключи верхнего уровня — «\n  "ключ": » в порядке байтов: метка встаёт перед первым большим ключом.
  for (size_t p = out.find("\n  \""); p != std::string::npos; p = out.find("\n  \"", p + 1)) {
    const size_t ks = p + 4, ke = out.find('"', ks);
    if (ke == std::string::npos) break;
    if (std::string_view(out).substr(ks, ke - ks) > std::string_view(kGenerationKey)) {
      out.insert(p, member + ",");
      return;
    }
  }
  const size_t close = out.rfind("\n}");
  if (close == std::string::npos || close == 0) return;   // не объект в принятом виде — без метки
  out.insert(close, "," + member);
}

// Метка разобранного файла; член удаляется (разбор таблиц его не знает). 0 — нет или не целое ≥ 1.
u64 takeGeneration(Value& v) {
  if (!v.isObj()) return 0;
  const Value* g = v.find(kGenerationKey);
  if (!g) return 0;
  const double d = g->asNum(0);
  const u64 gen = d >= 1 && d < 9e15 && d == std::floor(d) ? u64(d) : 0;
  v.erase(kGenerationKey);
  return gen;
}

// Мир только для чтения: причины по-русски (пусто — запись разрешена). version — версия формата world.json.
std::string lockReason(double version, const World& w, const std::vector<std::string>& missing) {
  std::vector<std::string> why;
  if (!missing.empty()) why.push_back((missing.size() == 1 ? "Нет файла данных " : "Нет файлов данных ") + join(missing, ", ") + ".");
  if (version > kVersion) why.push_back("Формат мира новее этой версии Regnum (" + json::write(Value(version)) + ").");
  if (w.meta->content > content::kVersion)
    why.push_back("Базовые записи мира новее этой версии Regnum (версия " + std::to_string(w.meta->content) + ", программа знает " +
                  std::to_string(content::kVersion) + ").");
  return join(why, " ");
}

// ---------------------------------------------------------------- источник файлов: папка или архив
class Source {
 public:
  virtual ~Source() = default;
  virtual bool bundle() const = 0;
  virtual const std::string& path() const = 0;
  virtual bool has(const std::string& rel) const = 0;
  virtual std::optional<std::string> read(const std::string& rel, std::string* err) const = 0;
  virtual u64 size(const std::string& rel) const = 0;
  // Файлы папки dir (без вложенных): относительные пути от корня проекта.
  virtual std::vector<std::string> list(const std::string& dir) const = 0;
  std::string origin(const std::string& rel) const { return bundle() ? fs::filename(path()) + ": " + rel : rel; }
};

class FolderSource final : public Source {
 public:
  explicit FolderSource(std::string root) : root_(std::move(root)) {}
  bool bundle() const override { return false; }
  const std::string& path() const override { return root_; }
  bool has(const std::string& rel) const override { return fs::isFile(fs::join(root_, rel)); }
  std::optional<std::string> read(const std::string& rel, std::string* err) const override { return fs::readFile(fs::join(root_, rel), err); }
  u64 size(const std::string& rel) const override { return fs::fileSize(fs::join(root_, rel)).value_or(0); }
  std::vector<std::string> list(const std::string& dir) const override {
    std::vector<std::string> out;
    for (auto& e : fs::list(fs::join(root_, dir)))
      if (!e.dir) out.push_back(dir + "/" + e.name);
    return out;
  }

 private:
  std::string root_;
};

class ZipSource final : public Source {
 public:
  explicit ZipSource(std::string path) : path_(std::move(path)) {
    std::string err;
    if (!zr_.openFile(path_, &err)) fail("Не удалось открыть архив «" + fs::filename(path_) + "»: " + err);
    // Мир в корне архива или в единственной вложенной папке (архив, созданный упаковкой папки).
    if (zr_.find(kWorldFile) < 0) {
      std::string best;
      int found = 0;
      for (auto& e : zr_.entries()) {
        const std::string& n = e.name;
        if (n.size() > 11 && n.compare(n.size() - 11, 11, "/world.json") == 0 &&
            std::count(n.begin(), n.end(), '/') == 1) {
          best = n.substr(0, n.size() - 10);
          found++;
        }
      }
      if (found != 1) fail("Архив «" + fs::filename(path_) + "» не содержит мир Regnum (нет world.json)");
      prefix_ = best;
    }
  }
  bool bundle() const override { return true; }
  const std::string& path() const override { return path_; }
  bool has(const std::string& rel) const override { return zr_.find(prefix_ + rel) >= 0; }
  std::optional<std::string> read(const std::string& rel, std::string* err) const override { return zr_.read(prefix_ + rel, err); }
  u64 size(const std::string& rel) const override {
    int i = zr_.find(prefix_ + rel);
    return i < 0 ? 0 : zr_.entries()[size_t(i)].size;
  }
  std::vector<std::string> list(const std::string& dir) const override {
    std::vector<std::string> out;
    std::string p = prefix_ + dir + "/";
    for (auto& e : zr_.entries()) {
      if (e.dir || e.name.size() <= p.size() || e.name.compare(0, p.size(), p) != 0) continue;
      if (e.name.find('/', p.size()) != std::string::npos) continue;
      out.push_back(e.name.substr(prefix_.size()));
    }
    std::sort(out.begin(), out.end());
    return out;
  }

 private:
  std::string path_, prefix_;
  codec::ZipReader zr_;
};

std::unique_ptr<Source> openSource(std::string path) {
  if (path.empty()) fail("Не указан путь к миру");
  if (fs::isFile(path) && fs::filename(path) == kWorldFile) path = fs::parent(path);
  if (fs::isDir(path)) {
    if (!fs::isFile(fs::join(path, kWorldFile))) fail("Папка «" + path + "» не является миром Regnum: нет файла world.json");
    return std::make_unique<FolderSource>(path);
  }
  if (fs::isFile(path)) return std::make_unique<ZipSource>(path);
  fail("Мир не найден: «" + path + "»");
}

// ---------------------------------------------------------------- чтение
LoadResult loadFrom(const Source& src) {
  LoadResult r;
  r.bundle = src.bundle();
  if (!r.bundle) r.files.folder = fs::absolute(src.path());

  std::string err;
  // world.json — первым: проверка версии до разбора остального.
  std::optional<i64> worldTime = r.bundle ? std::nullopt : fs::mtime(fs::join(src.path(), kWorldFile));
  auto worldText = src.read(kWorldFile, &err);
  if (!worldText) fail("Не удалось прочитать " + src.origin(kWorldFile) + ": " + err);
  Value world = parseFile(*worldText, src.origin(kWorldFile));
  Warnings warnFiles[F_COUNT];
  checkVersion(world, src.origin(kWorldFile), warnFiles[F_WORLD]);
  const double version = world.num("version", 1);
  const u64 worldGen = takeGeneration(world);
  r.generation = worldGen;
  if (!r.bundle) r.files.files[kWorldFile] = stampOf(*worldText, worldTime, worldGen);

  Parts parts;
  std::string errors[F_COUNT];
  std::string texts[F_COUNT];
  bool present[F_COUNT] = {};
  FileStamp stamps[F_COUNT];
  u64 gens[F_COUNT] = {};
  // Архив читается последовательно, папка — параллельно; разбор — параллельно.
  auto readOne = [&](size_t i) {
    const std::string rel = kFiles[i].path;
    std::optional<i64> t = r.bundle ? std::nullopt : fs::mtime(fs::join(src.path(), rel));
    if (!src.has(rel)) return;
    std::string e;
    auto text = src.read(rel, &e);
    if (!text) {
      errors[i] = "Не удалось прочитать " + src.origin(rel) + ": " + e;
      return;
    }
    if (!r.bundle) stamps[i] = stampOf(*text, t, 0);
    texts[i] = std::move(*text);
    present[i] = true;
  };
  if (r.bundle) {
    for (size_t i = 0; i < F_WORLD; i++) readOne(i);
  }
  jobs::parallelFor(size_t(F_WORLD), [&](size_t i) {
    if (!r.bundle) readOne(i);
    if (!present[i] || !errors[i].empty()) return;
    try {
      Value v = parseFile(texts[i], src.origin(kFiles[i].path));
      std::string().swap(texts[i]);
      gens[i] = takeGeneration(v);
      stamps[i].gen = gens[i];
      decode(v, FileId(i), parts, warnFiles[i]);
    } catch (const std::exception& e) {
      errors[i] = e.what();
    }
  }, 1);
  for (size_t i = 0; i < F_WORLD; i++)
    if (!errors[i].empty()) fail(errors[i]);
  decode(world, F_WORLD, parts, warnFiles[F_WORLD]);
  for (size_t i = 0; i < F_WORLD; i++) {
    if (!present[i] && !kFiles[i].optional) {
      warnFiles[i].push_back({kFiles[i].path, "", "файл не найден — таблица пуста"});
      r.missing.push_back(kFiles[i].path);
    }
    // Смешанное сохранение: файл записан сохранением, которое не дошло до world.json (оно пишется последним).
    if (present[i] && worldGen && gens[i] > worldGen) {
      warnFiles[i].push_back({kFiles[i].path, "", "смешанное сохранение: файл новее world.json (поколение " + std::to_string(gens[i]) +
                                                      ", у world.json — " + std::to_string(worldGen) + ")"});
      r.mixed.push_back(kFiles[i].path);
    }
    if (!r.bundle) r.files.files[kFiles[i].path] = stamps[i];
  }
  r.warnings = std::move(warnFiles[F_WORLD]);
  for (size_t i = 0; i < F_WORLD; i++) r.warnings.insert(r.warnings.end(), warnFiles[i].begin(), warnFiles[i].end());
  // Лишние файлы JSON в data/ (например, таблица под неверным именем) не читаются — об этом стоит знать.
  for (const std::string& rel : src.list("data"))
    if (fileIndex(rel) < 0 && utf8::lower(fs::ext(rel)) == ".json")
      r.warnings.push_back({rel, "", "неизвестный файл данных — не читается (имена файлов мира — docs/FORMAT.md)"});
  r.world = assemble(std::move(parts), r.warnings, &r.fixedTables);
  r.readOnly = lockReason(version, r.world, r.missing);
  return r;
}

// ---------------------------------------------------------------- текст файлов
std::vector<std::string> fileTexts(const World& w, const std::vector<int>& files) {
  std::vector<std::string> out(files.size());
  jobs::parallelFor(files.size(), [&](size_t k) {
    FileId f = FileId(files[k]);
    out[k] = format(encode(w, f), f);
  }, 1);
  return out;
}

std::vector<int> allFiles() {
  std::vector<int> v;
  for (int i = 0; i < F_COUNT; i++) v.push_back(i);
  return v;
}

// ---------------------------------------------------------------- резервные копии
// .regnum-backup/<папка файла>/<имя>.<NNNNNN><расширение>; номер растёт, хранятся последние keep.
// auto — версия, записанная автосохранением: своя очередь в .regnum-backup/auto/ (ручные копии не вытесняются).
struct Copy {
  u64 num = 0;
  std::string path;  // полный путь
  std::string rel;   // относительный путь в папке мира
  i64 mtime = 0;
};
// Копии файла relIn одной очереди по возрастанию номера.
std::vector<Copy> backupCopies(const std::string& folder, const std::string& relIn, bool autoQueue) {
  std::string rel = autoQueue ? "auto/" + relIn : relIn;
  std::string sub = fs::parent(rel);
  std::string root = fs::join(folder, kBackupDir);
  std::string dir = sub.empty() ? root : fs::join(root, sub);
  std::string stem = fs::stem(rel), ext = fs::ext(rel);
  std::vector<Copy> have;
  for (auto& e : fs::list(dir)) {
    if (e.dir) continue;
    const std::string& n = e.name;
    if (n.size() <= stem.size() + 1 + ext.size() || n.compare(0, stem.size() + 1, stem + ".") != 0 ||
        n.compare(n.size() - ext.size(), ext.size(), ext) != 0)
      continue;
    std::string_view num(n.data() + stem.size() + 1, n.size() - stem.size() - 1 - ext.size());
    u64 k = 0;
    auto res = std::from_chars(num.data(), num.data() + num.size(), k);
    if (num.empty() || res.ec != std::errc() || res.ptr != num.data() + num.size()) continue;
    have.push_back({k, e.path, std::string(kBackupDir) + "/" + (sub.empty() ? "" : sub + "/") + n, e.mtime});
  }
  std::sort(have.begin(), have.end(), [](const Copy& a, const Copy& b) { return a.num < b.num; });
  return have;
}

// Копия, совпадающая с последней копией очереди, не дублируется (повторная попытка сохранения после ошибки,
// возврат к прежнему содержимому): очередь не вытесняется одинаковыми версиями.
std::string backupFile(const std::string& folder, const std::string& relIn, const std::string& content, int keep, bool autoQueue = false) {
  std::string root = fs::join(folder, kBackupDir);
  std::string rel = autoQueue ? "auto/" + relIn : relIn;
  std::string sub = fs::parent(rel);
  std::string dir = sub.empty() ? root : fs::join(root, sub);
  std::string err;
  std::string ignore = fs::join(root, ".gitignore");
  if (!fs::isFile(ignore) && !fs::writeFileAtomic(ignore, std::string_view("*\n"), &err))
    fail("Не удалось создать папку резервных копий «" + root + "»: " + err);
  std::vector<Copy> have = backupCopies(folder, relIn, autoQueue);
  if (!have.empty() && fs::readFile(have.back().path) == content) return have.back().rel;
  u64 next = have.empty() ? 1 : have.back().num + 1;
  char buf[32];
  std::snprintf(buf, sizeof buf, ".%06llu", static_cast<unsigned long long>(next));
  std::string name = fs::stem(rel) + buf + fs::ext(rel);
  std::string full = fs::join(dir, name);
  if (!fs::writeFileAtomic(full, content, &err)) fail("Не удалось создать резервную копию «" + full + "»: " + err);
  have.push_back({next, full, {}, 0});
  size_t excess = have.size() > size_t(keep) ? have.size() - size_t(keep) : 0;
  for (size_t i = 0; i < excess; i++) fs::remove(have[i].path);
  return std::string(kBackupDir) + "/" + (sub.empty() ? "" : sub + "/") + name;
}

// Файл изменён извне относительно отпечатка? (обновляет время, если содержимое прежнее)
bool changedOnDisk(const std::string& full, FileStamp& st) {
  bool ex = fs::isFile(full);
  if (!ex) return st.exists;
  if (!st.exists) return true;
  auto sz = fs::fileSize(full);
  auto mt = fs::mtime(full);
  const bool racy = st.checked - st.mtime < kRacyMs;
  if (sz && mt && *sz == st.size && *mt == st.mtime && !racy) return false;
  auto data = fs::readFile(full);
  if (!data) return true;
  if (data->size() == st.size && codec::crc32(*data) == st.crc) {
    if (mt) st.mtime = *mt;
    st.checked = wallMs();
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- история ходов
// turn-NNNN.json.gz (снимок без номера) или turn-NNNN-SSSSSS.json.gz (ход и номер снимка).
std::optional<std::pair<int, u64>> parseSnapName(std::string_view name) {
  constexpr std::string_view pre = "turn-", post = ".json.gz";
  if (name.size() <= pre.size() + post.size() || name.substr(0, pre.size()) != pre || name.substr(name.size() - post.size()) != post)
    return std::nullopt;
  std::string_view num = name.substr(pre.size(), name.size() - pre.size() - post.size());
  std::string_view seqPart;
  if (size_t dash = num.find('-'); dash != std::string_view::npos) {
    seqPart = num.substr(dash + 1);
    num = num.substr(0, dash);
    if (seqPart.empty()) return std::nullopt;
  }
  int t = 0;
  auto r = std::from_chars(num.data(), num.data() + num.size(), t);
  if (r.ec != std::errc() || r.ptr != num.data() + num.size() || t < 1) return std::nullopt;
  u64 seq = 0;
  if (!seqPart.empty()) {
    auto rs = std::from_chars(seqPart.data(), seqPart.data() + seqPart.size(), seq);
    if (rs.ec != std::errc() || rs.ptr != seqPart.data() + seqPart.size() || seq == 0) return std::nullopt;
  }
  return std::make_pair(t, seq);
}

std::string snapFileName(int turn, u64 seq) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "turn-%04d-%06llu.json.gz", turn, static_cast<unsigned long long>(seq));
  return buf;
}

void sortSnaps(std::vector<SnapshotInfo>& v) {
  std::sort(v.begin(), v.end(), [](const SnapshotInfo& a, const SnapshotInfo& b) {
    if (a.turn != b.turn) return a.turn < b.turn;
    if (a.seq != b.seq) return a.seq < b.seq;
    return a.file < b.file;
  });
}

std::vector<SnapshotInfo> listFrom(const Source& src, u64* head = nullptr) {
  std::map<std::string, SnapshotInfo> m;   // файл -> сведения
  std::vector<std::string> files = src.list(kHistoryDir);
  auto exists = [&](const std::string& rel) { return std::find(files.begin(), files.end(), rel) != files.end(); };
  if (head) *head = 0;
  if (auto text = src.read(kHistoryIndex, nullptr)) {
    if (auto v = json::tryParse(*text)) {
      if (head) *head = u64(std::max<i64>(0, v->integer("head")));
      for (const Value& e : v->arr("snapshots")) {
        auto p = parseSnapName(e.str("file"));
        if (!p) continue;
        SnapshotInfo s;
        s.turn = p->first;
        s.seq = p->second;
        s.file = std::string(kHistoryDir) + "/" + e.str("file");
        if (!exists(s.file)) continue;
        s.at = e.str("at");
        s.label = e.str("label");
        s.name = e.str("name");
        s.kind = e.str("kind");
        s.parent = u64(std::max<i64>(0, e.integer("parent")));
        m[s.file] = std::move(s);
      }
    }
  }
  for (const std::string& rel : files) {
    auto p = parseSnapName(fs::filename(rel));
    if (!p || m.count(rel)) continue;
    SnapshotInfo s;
    s.turn = p->first;
    s.seq = p->second;
    s.file = rel;
    m[rel] = std::move(s);
  }
  std::vector<SnapshotInfo> out;
  for (auto& [f, s] : m) {
    s.size = src.size(s.file);
    out.push_back(std::move(s));
  }
  sortSnaps(out);
  return out;
}

std::string indexText(const std::vector<SnapshotInfo>& list, u64 head = 0) {
  json::Array a;
  for (auto& s : list) {
    Value e = Value::object();
    e.set("turn", s.turn);
    e.set("file", fs::filename(s.file));
    e.set("at", s.at);
    e.set("label", s.label);
    e.set("name", s.name);
    e.set("size", s.size);
    if (s.seq) e.set("seq", s.seq);
    if (!s.kind.empty()) e.set("kind", s.kind);
    if (s.parent) e.set("parent", s.parent);
    a.push_back(std::move(e));
  }
  Value o = Value::object();
  o.set("snapshots", Value(std::move(a)));
  if (head) o.set("head", head);
  std::string out = json::write(o, json::WriteOptions{2, true, true});
  out.push_back('\n');
  return out;
}

// Файлы истории источника: относительный путь -> байты (index.json пересобирается).
std::vector<std::pair<std::string, std::string>> readHistory(const Source& src, std::vector<SnapshotInfo>* infos, u64* head = nullptr) {
  std::vector<std::pair<std::string, std::string>> out;
  auto list = listFrom(src, head);
  for (auto& s : list) {
    std::string err;
    auto data = src.read(s.file, &err);
    if (!data) fail("Не удалось прочитать " + src.origin(s.file) + ": " + err);
    out.push_back({s.file, std::move(*data)});
  }
  if (infos) *infos = std::move(list);
  return out;
}

using FileList = std::vector<std::pair<std::string, std::string>>;  // относительный путь -> байты

// Файлы мира (world.json — первым), записанные заново.
FileList worldFiles(const World& w) {
  auto files = allFiles();
  std::rotate(files.begin(), files.begin() + F_WORLD, files.end());
  auto texts = fileTexts(w, files);
  FileList out;
  for (size_t k = 0; k < files.size(); k++) out.push_back({kFiles[files[k]].path, std::move(texts[k])});
  return out;
}

// Файлы мира источника как есть (для пересборки архива без изменения мира).
FileList rawWorldFiles(const Source& src) {
  FileList out;
  for (int k = 0; k < F_COUNT; k++) {
    int i = (k + F_WORLD) % F_COUNT;
    std::string rel = kFiles[i].path, err;
    if (!src.has(rel)) continue;
    auto data = src.read(rel, &err);
    if (!data) fail("Не удалось прочитать " + src.origin(rel) + ": " + err);
    out.push_back({rel, std::move(*data)});
  }
  return out;
}

// Собрать архив: файлы мира + история.
std::string buildBundle(const FileList& world, const FileList& history, const std::vector<SnapshotInfo>& infos, u64 head = 0) {
  i64 now = i64(std::time(nullptr));
  codec::ZipWriter zw(6);
  std::string err;
  for (auto& [rel, data] : world)
    if (!zw.add(rel, data, now, &err)) fail("Не удалось собрать архив: " + err);
  if (!history.empty()) {
    if (!zw.add(kHistoryIndex, indexText(infos, head), now, &err)) fail("Не удалось собрать архив: " + err);
    zw.setLevel(0);   // снимки уже сжаты gzip: повторное сжатие только тратит время записи архива
    for (auto& [rel, data] : history)
      if (!zw.add(rel, data, now, &err)) fail("Не удалось собрать архив: " + err);
  }
  return zw.finish("Regnum world");
}

void writeBundleFile(const std::string& path, const std::string& bytes) {
  std::string err;
  if (!fs::writeFileAtomic(path, bytes, &err)) fail("Не удалось записать архив «" + path + "»: " + err);
}

// ---------------------------------------------------------------- данные пользователя
std::string dataDirOr(const std::string& d) {
  std::string dir = d.empty() ? fs::userDataDir() : d;
  fs::makeDirs(dir);
  return dir;
}

std::string hex16(u64 v) {
  char buf[20];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
  return buf;
}

std::string autosaveSlot(const std::string& project) {
  return project.empty() ? std::string("new") : hex16(hash64(fs::absolute(project)));
}

}  // namespace

bool isProject(const std::string& path) {
  if (fs::isDir(path)) return fs::isFile(fs::join(path, kWorldFile));
  if (!fs::isFile(path)) return false;
  if (fs::filename(path) == kWorldFile) return true;
  return isBundlePath(path);  // содержимое архива проверяется при чтении
}

// ================================================================ папка и архив
LoadResult load(const std::string& path) {
  auto src = openSource(path);
  return loadFrom(*src);
}

SaveResult save(const std::string& folder, const World& w, u32 dirtyTables, FileState* state, const SaveOptions& opt) {
  if (folder.empty()) fail("Не указана папка для сохранения мира");
  std::string err;
  if (fs::exists(folder) && !fs::isDir(folder)) fail("«" + folder + "» — файл, а не папка");
  if (!fs::makeDirs(folder, &err)) fail("Не удалось создать папку «" + folder + "»: " + err);
  if (state && (state->folder.empty() || fs::absolute(state->folder) != fs::absolute(folder))) {
    state->folder = fs::absolute(folder);
    state->files.clear();
  }

  std::vector<int> todo;
  for (int i = 0; i < F_COUNT; i++)
    if ((kFiles[i].tables & dirtyTables) || !fs::isFile(fs::join(folder, kFiles[i].path))) todo.push_back(i);

  if (state && !opt.overwriteExternal) {
    std::vector<std::string> conflicts;
    for (int i : todo) {
      auto it = state->files.find(kFiles[i].path);
      if (it == state->files.end()) continue;
      std::string full = fs::join(folder, kFiles[i].path);
      if (!fs::isFile(full)) continue;  // удалённый файл просто создаётся заново
      if (changedOnDisk(full, it->second)) conflicts.push_back(kFiles[i].path);
    }
    if (!conflicts.empty())
      fail("Файлы мира изменены другой программой: " + join(conflicts, ", ") +
           ". Перечитайте мир или сохраните с заменой чужих изменений.");
  }

  SaveResult res;
  // Тексты без метки поколения и прежнее содержимое; совпадающие с диском (без метки) файлы не переписываются.
  std::vector<std::string> texts = fileTexts(w, todo);
  std::vector<std::optional<std::string>> olds(todo.size());
  jobs::parallelFor(todo.size(), [&](size_t k) {
    const std::string full = fs::join(folder, kFiles[todo[k]].path);
    if (fs::isFile(full)) olds[k] = fs::readFile(full);
  }, 1);
  std::vector<size_t> changed;   // индексы todo в порядке записи (world.json — последним)
  for (size_t k = 0; k < todo.size(); k++) {
    const std::string rel = kFiles[todo[k]].path;
    if (olds[k] && sameBody(*olds[k], texts[k])) {
      res.unchanged.push_back(rel);
      if (state) state->files[rel] = stampOf(*olds[k], fs::mtime(fs::join(folder, rel)), generationIn(*olds[k]));
      continue;
    }
    changed.push_back(k);
  }
  if (!changed.empty()) {
    // Поколение: несколько файлов — новое, и world.json (запись о сохранении целиком) пишется последним даже без
    // изменений; один файл заменяется атомарно сам — поколение world.json на диске.
    const std::string worldFull = fs::join(folder, kWorldFile);
    auto worldOnDisk = fs::isFile(worldFull) ? fs::readFile(worldFull) : std::nullopt;
    u64 diskGen = 0;
    if (worldOnDisk)
      if (auto v = json::tryParse(*worldOnDisk)) diskGen = u64(std::max<i64>(0, v->integer(kGenerationKey, 0)));
    const bool multi = changed.size() > 1;
    if (multi && todo[changed.back()] != F_WORLD) {
      auto it = std::find(todo.begin(), todo.end(), int(F_WORLD));
      if (it != todo.end()) {   // world.json без изменений — всё равно пишется последним
        changed.push_back(size_t(it - todo.begin()));
        res.unchanged.erase(std::remove(res.unchanged.begin(), res.unchanged.end(), std::string(kWorldFile)), res.unchanged.end());
      } else {
        // world.json не входил в запись — его внешнее изменение не затирается молча.
        if (state && !opt.overwriteExternal && worldOnDisk)
          if (auto st = state->files.find(kWorldFile); st != state->files.end() && changedOnDisk(worldFull, st->second))
            fail(std::string("Файлы мира изменены другой программой: ") + kWorldFile +
                 ". Перечитайте мир или сохраните с заменой чужих изменений.");
        todo.push_back(F_WORLD);
        texts.push_back(fileText(w, kWorldFile));
        olds.push_back(worldOnDisk);
        changed.push_back(todo.size() - 1);
      }
    }
    u64 gen = diskGen;
    if (multi) {
      u64 known = diskGen;
      if (state)
        for (auto& [rel, st] : state->files) known = std::max(known, st.gen);
      gen = known + 1;
    }
    for (size_t k : changed) insertGeneration(texts[k], gen);

    // Резервные копии прежних версий — до записи (ошибка здесь ничего не меняет).
    for (size_t k : changed) {
      const std::string rel = kFiles[todo[k]].path;
      if (!olds[k] || !opt.backups || opt.keepBackups <= 0) continue;
      bool autoQueue = std::find(opt.autoBackups.begin(), opt.autoBackups.end(), rel) != opt.autoBackups.end();
      res.backups.push_back(backupFile(folder, rel, *olds[k], opt.keepBackups, autoQueue));
    }
    // Все временные файлы, затем замены: файл, занятый другой программой, не оставляет мир наполовину записанным.
    std::vector<std::string> temps;
    auto dropTemps = [&](size_t from) {
      for (size_t j = from; j < temps.size(); j++) fs::remove(temps[j]);
    };
    for (size_t k : changed) {
      const std::string full = fs::join(folder, kFiles[todo[k]].path);
      auto tmp = fs::writeTemp(full, texts[k], &err);
      if (!tmp) {
        dropTemps(0);
        fail("Мир не сохранён: " + err + ". Файлы мира на диске не изменены.");
      }
      temps.push_back(*tmp);
    }
    for (size_t j = 0; j < changed.size(); j++) {
      const size_t k = changed[j];
      const std::string full = fs::join(folder, kFiles[todo[k]].path);
      if (fs::replaceFile(temps[j], full, &err)) continue;
      dropTemps(j);
      // Уже заменённые файлы — к прежним версиям (новый файл — удаляется): мир на диске остаётся прежним.
      std::vector<std::string> stuck;
      for (size_t c = j; c-- > 0;) {
        const size_t kc = changed[c];
        const std::string rel = kFiles[todo[kc]].path, fc = fs::join(folder, rel);
        const bool back = olds[kc] ? fs::writeFileAtomic(fc, *olds[kc]) : fs::remove(fc);
        if (!back) {
          stuck.push_back(rel);
          if (state) state->files[rel] = stampOf(texts[kc], fs::mtime(fc), gen);
        }
      }
      if (stuck.empty()) fail("Мир не сохранён: " + err + ". Файлы мира на диске не изменены — повторите сохранение.");
      fail("Мир не сохранён: " + err + ". Не удалось вернуть прежние версии файлов " + join(stuck, ", ") +
           " — мир на диске смешан (прежние версии — в " + kBackupDir + ").");
    }
    for (size_t k : changed) {
      const std::string rel = kFiles[todo[k]].path;
      res.written.push_back(rel);
      if (state) state->files[rel] = stampOf(texts[k], fs::mtime(fs::join(folder, rel)), gen);
    }
  }
  if (state) {
    // Отпечатки файлов, не входивших в запись, остаются прежними; недостающие — снимаются с диска.
    for (const FileDef& f : kFiles) {
      if (state->files.count(f.path)) continue;
      std::string full = fs::join(folder, f.path);
      if (auto data = fs::readFile(full)) state->files[f.path] = stampOf(*data, fs::mtime(full), generationIn(*data));
      else state->files[f.path] = FileStamp{};
    }
  }
  return res;
}

std::vector<ExternalChange> externalChanges(FileState& state) {
  std::vector<ExternalChange> out;
  if (state.folder.empty()) return out;
  for (auto& [rel, st] : state.files) {
    std::string full = fs::join(state.folder, rel);
    bool ex = fs::isFile(full);
    ExternalChange c;
    c.file = rel;
    c.tables = tablesOfFile(rel);
    if (!ex) {
      if (!st.exists) continue;
      c.kind = ChangeKind::Removed;
    } else if (!st.exists) {
      c.kind = ChangeKind::Added;
    } else if (!changedOnDisk(full, st)) {
      continue;
    } else {
      c.kind = ChangeKind::Modified;
    }
    out.push_back(std::move(c));
  }
  return out;
}

FileStamp stampFile(const std::string& path) {
  auto mt = fs::mtime(path);
  auto data = fs::isFile(path) ? fs::readFile(path) : std::nullopt;
  if (!data) return FileStamp{};
  return stampOf(*data, mt, generationIn(*data));
}

bool fileChanged(const std::string& path, FileStamp& st) { return changedOnDisk(path, st); }

// ================================================================ метка поколения
u64 generationIn(std::string_view text) {
  const size_t p = text.find(kGenLine);
  if (p == std::string_view::npos) return 0;
  const char* b = text.data() + p + kGenLine.size();
  const char* e = text.data() + text.size();
  u64 v = 0;
  auto r = std::from_chars(b, e, v);
  return r.ec == std::errc() && r.ptr != b ? v : 0;
}

std::string withoutGeneration(std::string_view text) {
  auto s = genSpan(text);
  if (!s) return std::string(text);
  std::string out;
  out.reserve(text.size());
  out.append(text.substr(0, s->first)).append(text.substr(s->second));
  return out;
}

std::string withGeneration(std::string_view text, u64 gen) {
  std::string out = withoutGeneration(text);
  insertGeneration(out, gen);
  return out;
}

// ================================================================ резервные копии: восстановление
std::optional<BackupCopy> latestBackup(const std::string& folder, const std::string& rel, u64 maxGeneration) {
  std::vector<Copy> all;
  for (bool autoQueue : {false, true}) {
    auto list = backupCopies(folder, rel, autoQueue);
    all.insert(all.end(), list.begin(), list.end());
  }
  // Новые первыми: по времени записи, при равном времени — по номеру в очереди.
  std::stable_sort(all.begin(), all.end(), [](const Copy& a, const Copy& b) {
    if (a.mtime != b.mtime) return a.mtime > b.mtime;
    return a.num > b.num;
  });
  for (const Copy& c : all) {
    auto text = fs::readFile(c.path);
    if (!text) continue;
    auto v = json::tryParse(*text);
    if (!v || !v->isObj()) continue;
    const u64 gen = u64(std::max<i64>(0, v->integer(kGenerationKey, 0)));
    if (maxGeneration && gen > maxGeneration) continue;
    return BackupCopy{rel, c.rel, gen, c.mtime};
  }
  return std::nullopt;
}

std::vector<std::string> restoreBackups(const std::string& folder, const std::vector<std::string>& files, u64 maxGeneration,
                                        std::vector<std::string>* noCopy) {
  std::vector<std::string> done;
  for (const std::string& rel : files) {
    if (fileIndex(rel) < 0) continue;
    auto c = latestBackup(folder, rel, maxGeneration);
    auto text = c ? fs::readFile(fs::join(folder, c->copy)) : std::nullopt;
    if (!text) {
      if (noCopy) noCopy->push_back(rel);
      continue;
    }
    const std::string full = fs::join(folder, rel);
    // Нынешняя версия (смешанное сохранение) не теряется: она уходит в копии, но новее допустимого поколения —
    // повторное восстановление её не выберет.
    if (auto cur = fs::isFile(full) ? fs::readFile(full) : std::nullopt; cur && *cur != *text) backupFile(folder, rel, *cur, kKeepBackups);
    std::string err;
    if (!fs::writeFileAtomic(full, *text, &err)) fail("Не удалось восстановить «" + rel + "» из резервной копии: " + err);
    done.push_back(rel);
  }
  return done;
}

std::string savedTime(const std::string& path) {
  try {
    auto src = openSource(path);
    auto text = src->read(kWorldFile, nullptr);
    if (!text) return {};
    auto v = json::tryParse(*text);
    return v ? v->obj("meta").str("updatedAt") : std::string();
  } catch (const std::exception&) {
    return {};
  }
}

void saveBundle(const std::string& path, const World& w, const std::string& historyFrom) {
  if (path.empty()) fail("Не указан путь архива");
  if (fs::isDir(path)) fail("«" + path + "» — папка, а не файл архива");
  std::vector<std::pair<std::string, std::string>> history;
  std::vector<SnapshotInfo> infos;
  u64 head = 0;
  std::string from = historyFrom;
  if (from.empty() && fs::isFile(path)) {
    // История прежней версии архива (повреждённый архив истории не даёт — перезаписывается).
    try {
      ZipSource old(path);
      history = readHistory(old, &infos, &head);
    } catch (const UserError& e) {
      logWarn("история архива не перенесена: %s", e.what());
    }
  } else if (!from.empty()) {
    auto src = openSource(from);
    history = readHistory(*src, &infos, &head);
  }
  writeBundleFile(path, buildBundle(worldFiles(w), history, infos, head));
}

void convert(const std::string& src, const std::string& dst, Warnings* warnings, bool overwrite) {
  if (src.empty() || dst.empty()) fail("Не указан исходный или целевой путь");
  if (fs::absolute(src) == fs::absolute(dst)) fail("Исходный и целевой пути совпадают");
  auto source = openSource(src);
  LoadResult r = loadFrom(*source);
  if (warnings) warnings->insert(warnings->end(), r.warnings.begin(), r.warnings.end());
  if (!r.readOnly.empty()) fail("Мир «" + src + "» можно открыть только для чтения: " + r.readOnly);
  std::vector<SnapshotInfo> infos;
  u64 head = 0;
  auto history = readHistory(*source, &infos, &head);
  if (isBundlePath(dst)) {
    if (fs::exists(dst) && !overwrite) fail("Файл «" + dst + "» уже существует");
    writeBundleFile(dst, buildBundle(worldFiles(r.world), history, infos, head));
    return;
  }
  if (fs::exists(dst) && !fs::isDir(dst)) fail("«" + dst + "» — файл, а не папка");
  if (fs::isDir(dst) && !fs::list(dst).empty() && !overwrite) fail("Папка «" + dst + "» не пуста");
  SaveOptions so;
  so.backups = false;
  save(dst, r.world, TB_ALL, nullptr, so);
  std::string err;
  for (auto& [rel, data] : history)
    if (!fs::writeFileAtomic(fs::join(dst, rel), data, &err)) fail("Не удалось записать «" + rel + "»: " + err);
  if (!history.empty() && !fs::writeFileAtomic(fs::join(dst, kHistoryIndex), indexText(infos, head), &err))
    fail("Не удалось записать историю ходов: " + err);
}

// ================================================================ снимки ходов
std::vector<SnapshotInfo> listSnapshots(const std::string& path) {
  auto src = openSource(path);
  return listFrom(*src);
}

// ---------------------------------------------------------------- история с ветвями (снимки с номерами)
History listHistory(const std::string& path) {
  auto src = openSource(path);
  History h;
  h.list = listFrom(*src, &h.head);
  return h;
}

namespace {

// Сведения нового снимка: файл с номером, размер — по данным.
SnapshotInfo newInfo(const NewSnapshot& n) {
  if (n.info.seq == 0) fail("Снимок хода без номера");
  SnapshotInfo s = n.info;
  s.turn = std::max(1, s.turn);
  s.file = std::string(kHistoryDir) + "/" + snapFileName(s.turn, s.seq);
  s.size = n.gz.size();
  return s;
}

// Добавить новые снимки к истории в памяти (файл с тем же именем заменяется).
void mergeNew(FileList& history, std::vector<SnapshotInfo>& infos, const std::vector<NewSnapshot>& add) {
  for (const NewSnapshot& n : add) {
    SnapshotInfo s = newInfo(n);
    history.erase(std::remove_if(history.begin(), history.end(), [&](auto& h) { return h.first == s.file; }), history.end());
    infos.erase(std::remove_if(infos.begin(), infos.end(), [&](auto& x) { return x.file == s.file; }), infos.end());
    history.push_back({s.file, bytesStr(n.gz)});
    infos.push_back(std::move(s));
  }
  std::sort(history.begin(), history.end());
  sortSnaps(infos);
}

void writeFolderHistory(const std::string& folder, const FileList& files, const std::vector<SnapshotInfo>& infos, u64 head) {
  std::string err;
  for (auto& [rel, data] : files)
    if (!fs::writeFileAtomic(fs::join(folder, rel), data, &err)) fail("Не удалось записать «" + rel + "»: " + err);
  if (!fs::writeFileAtomic(fs::join(folder, kHistoryIndex), indexText(infos, head), &err)) fail("Не удалось записать историю ходов: " + err);
}

}  // namespace

void addSnapshots(const std::string& path, const std::vector<NewSnapshot>& snaps, u64 head) {
  auto src = openSource(path);
  u64 oldHead = 0;
  if (src->bundle()) {
    std::vector<SnapshotInfo> infos;
    auto history = readHistory(*src, &infos, &oldHead);
    mergeNew(history, infos, snaps);
    std::string bytes = buildBundle(rawWorldFiles(*src), history, infos, head ? head : oldHead);
    src.reset();
    writeBundleFile(path, bytes);
    return;
  }
  std::vector<SnapshotInfo> infos = listFrom(*src, &oldHead);
  FileList files;
  mergeNew(files, infos, snaps);
  writeFolderHistory(src->path(), files, infos, head ? head : oldHead);
}

LoadResult loadSnapshotFile(const std::string& path, const std::string& file) {
  auto src = openSource(path);
  if (!src->has(file)) fail("Нет снимка хода «" + file + "»");
  std::string err;
  auto data = src->read(file, &err);
  if (!data) fail("Не удалось прочитать снимок хода «" + file + "»: " + err);
  return unpack(std::span<const u8>(reinterpret_cast<const u8*>(data->data()), data->size()), src->origin(file));
}

void saveBundleWith(const std::string& path, const World& w, const std::string& historyFrom, const std::vector<NewSnapshot>& add, u64 head) {
  if (path.empty()) fail("Не указан путь архива");
  if (fs::isDir(path)) fail("«" + path + "» — папка, а не файл архива");
  FileList history;
  std::vector<SnapshotInfo> infos;
  u64 oldHead = 0;
  if (!historyFrom.empty() && isProject(historyFrom)) {
    try {
      auto src = openSource(historyFrom);
      history = readHistory(*src, &infos, &oldHead);
    } catch (const UserError& e) {
      // Повреждённая прежняя версия самого архива истории не даёт — он перезаписывается.
      if (fs::absolute(historyFrom) != fs::absolute(path)) throw;
      logWarn("история архива не перенесена: %s", e.what());
      history.clear();
      infos.clear();
    }
  }
  mergeNew(history, infos, add);
  writeBundleFile(path, buildBundle(worldFiles(w), history, infos, head ? head : oldHead));
}

void replaceHistory(const std::string& to, const std::string& from) {
  if (to.empty() || !fs::isDir(to)) fail("История ходов: «" + to + "» — не папка мира");
  if (!from.empty() && fs::absolute(from) == fs::absolute(to)) return;
  FileList history;
  std::vector<SnapshotInfo> infos;
  u64 head = 0;
  if (!from.empty() && isProject(from)) {
    auto src = openSource(from);
    history = readHistory(*src, &infos, &head);
  }
  std::string err;
  std::string dir = fs::join(to, kHistoryDir);
  if (fs::exists(dir) && !fs::removeAll(dir, &err)) fail("Не удалось очистить историю ходов «" + dir + "»: " + err);
  if (history.empty()) return;
  writeFolderHistory(to, history, infos, head);
}

// ================================================================ мир целиком
std::string toJson(const World& w, int indent) {
  std::vector<Value> vals(F_COUNT);
  jobs::parallelFor(size_t(F_COUNT), [&](size_t i) { vals[i] = encode(w, FileId(i)); }, 1);
  Value files = Value::object();
  for (int i = 0; i < F_COUNT; i++) files.set(kFiles[i].path, std::move(vals[size_t(i)]));
  Value doc = Value::object();
  doc.set("format", kSnapshotFormat);
  doc.set("version", kVersion);
  doc.set("files", std::move(files));
  std::string out = json::write(doc, json::WriteOptions{std::max(0, indent), true, true});
  if (indent > 0) out.push_back('\n');
  return out;
}

LoadResult fromJson(std::string_view text, const std::string& origin) {
  Value doc = parseFile(text, origin);
  if (!doc.isObj() || doc.str("format") != kSnapshotFormat) fail(origin + ": это не снимок мира Regnum (format должен быть «regnum-snapshot»)");
  double ver = doc.num("version", 1);
  if (!std::isfinite(ver) || ver < 1) fail(origin + ": неверная версия формата снимка");
  if (std::floor(ver) > kVersion)
    fail("Снимок сохранён более новой версией Regnum (формат " + std::to_string(i64(ver)) + "). Обновите редактор.");
  const Value& files = doc.obj("files");
  const Value* world = files.find(kWorldFile);
  if (!world) fail(origin + ": в снимке нет world.json");
  LoadResult r;
  Warnings warnFiles[F_COUNT];
  checkVersion(*world, origin + ": world.json", warnFiles[F_WORLD]);
  Parts parts;
  jobs::parallelFor(size_t(F_COUNT), [&](size_t i) {
    const Value* v = files.find(kFiles[i].path);
    if (v && v->has(kGenerationKey)) {   // метка поколения файла папки — не данные таблицы
      Value copy = *v;
      takeGeneration(copy);
      decode(copy, FileId(i), parts, warnFiles[i]);
    } else if (v) {
      decode(*v, FileId(i), parts, warnFiles[i]);
    } else if (!kFiles[i].optional) {
      warnFiles[i].push_back({kFiles[i].path, "", "файла нет в снимке — таблица пуста"});
    }
  }, 1);
  r.warnings = std::move(warnFiles[F_WORLD]);
  for (size_t i = 0; i < F_WORLD; i++) r.warnings.insert(r.warnings.end(), warnFiles[i].begin(), warnFiles[i].end());
  for (auto& [k, v] : files.members())
    if (fileIndex(k) < 0) r.warnings.push_back({k, "", "неизвестный файл в снимке — отброшен"});
  r.world = assemble(std::move(parts), r.warnings, &r.fixedTables);
  r.readOnly = lockReason(std::max(ver, world->num("version", 1)), r.world, {});
  return r;
}

std::vector<u8> pack(const World& w, int level) { return codec::deflate(toJson(w, 0), level, codec::ZFormat::Gzip); }

LoadResult unpack(std::span<const u8> gz, const std::string& origin) {
  std::string err;
  codec::InflateOptions o;
  o.maxOutput = kMaxUnpacked;
  auto raw = codec::inflate(gz, codec::ZFormat::Gzip, &err, o);
  if (!raw) fail(origin + ": повреждённые сжатые данные (" + err + ")");
  return fromJson(std::string_view(reinterpret_cast<const char*>(raw->data()), raw->size()), origin);
}

std::string fileText(const World& w, std::string_view rel) {
  int i = fileIndex(rel);
  if (i < 0) fail("Нет такого файла мира: «" + std::string(rel) + "»");
  return format(encode(w, FileId(i)), FileId(i));
}

// ================================================================ недавние проекты
namespace {

std::vector<RecentProject> readRecent(const std::string& dir) {
  std::vector<RecentProject> out;
  auto text = fs::readFile(fs::join(dir, "recent.json"));
  if (!text) return out;
  auto v = json::tryParse(*text);
  if (!v) return out;
  for (const Value& e : v->arr("recent")) {
    RecentProject r;
    r.path = e.str("path");
    if (r.path.empty()) continue;
    r.name = e.str("name");
    r.at = e.str("at");
    out.push_back(std::move(r));
  }
  return out;
}

void writeRecent(const std::string& dir, const std::vector<RecentProject>& list) {
  json::Array a;
  for (auto& r : list) {
    Value e = Value::object();
    e.set("path", r.path);
    e.set("name", r.name);
    e.set("at", r.at);
    a.push_back(std::move(e));
  }
  Value o = Value::object();
  o.set("recent", Value(std::move(a)));
  std::string text = json::write(o, json::WriteOptions{2, true, true});
  text.push_back('\n');
  std::string err;
  if (!fs::writeFileAtomic(fs::join(dir, "recent.json"), text, &err)) logWarn("недавние проекты не сохранены: %s", err.c_str());
}

}  // namespace

std::vector<RecentProject> recentProjects(const std::string& dataDir) {
  auto list = readRecent(dataDirOr(dataDir));
  for (auto& r : list) {
    r.exists = isProject(r.path);
    r.bundle = isBundlePath(r.path);
  }
  return list;
}

void addRecent(const std::string& path, const std::string& name, const std::string& dataDir) {
  std::string dir = dataDirOr(dataDir);
  std::string abs = fs::absolute(path);
  auto list = readRecent(dir);
  list.erase(std::remove_if(list.begin(), list.end(), [&](auto& r) { return r.path == abs; }), list.end());
  RecentProject r;
  r.path = abs;
  r.name = name;
  r.at = nowIso();
  list.insert(list.begin(), std::move(r));
  if (list.size() > size_t(kMaxRecent)) list.resize(size_t(kMaxRecent));
  writeRecent(dir, list);
}

void removeRecent(const std::string& path, const std::string& dataDir) {
  std::string dir = dataDirOr(dataDir);
  std::string abs = fs::absolute(path);
  auto list = readRecent(dir);
  size_t n = list.size();
  list.erase(std::remove_if(list.begin(), list.end(), [&](auto& r) { return r.path == abs || r.path == path; }), list.end());
  if (list.size() != n) writeRecent(dir, list);
}

// ================================================================ автосохранение
// <dataDir>/autosave/autosave-<слот>.json.gz — мир; autosave-<слот>.json — сведения (проект, время, ход).
AutosaveInfo writeAutosave(const World& w, const std::string& project, const std::string& dataDir) {
  std::string dir = fs::join(dataDirOr(dataDir), "autosave");
  std::string slot = autosaveSlot(project);
  AutosaveInfo info;
  info.file = fs::join(dir, "autosave-" + slot + ".json.gz");
  info.project = project.empty() ? std::string() : fs::absolute(project);
  info.name = w.meta->name;
  info.at = nowIso();
  info.turn = w.turn();
  std::vector<u8> gz = pack(w, 1);
  info.size = gz.size();
  std::string err;
  if (!fs::writeFileAtomic(info.file, gz, &err)) fail("Не удалось записать автосохранение: " + err);
  Value m = Value::object();
  m.set("project", info.project);
  m.set("name", info.name);
  m.set("at", info.at);
  m.set("turn", info.turn);
  std::string text = json::write(m, json::WriteOptions{2, true, true});
  text.push_back('\n');
  if (!fs::writeFileAtomic(fs::join(dir, "autosave-" + slot + ".json"), text, &err)) fail("Не удалось записать автосохранение: " + err);
  return info;
}

std::vector<AutosaveInfo> listAutosaves(const std::string& dataDir) {
  std::string dir = fs::join(dataDirOr(dataDir), "autosave");
  std::vector<std::pair<i64, AutosaveInfo>> found;
  for (auto& e : fs::list(dir)) {
    const std::string& n = e.name;
    if (e.dir || n.size() < 15 || n.compare(0, 9, "autosave-") != 0 || n.compare(n.size() - 5, 5, ".json") != 0) continue;
    std::string gz = fs::join(dir, n + ".gz");
    if (!fs::isFile(gz)) continue;
    auto text = fs::readFile(e.path);
    if (!text) continue;
    auto v = json::tryParse(*text);
    if (!v || !v->isObj()) continue;
    AutosaveInfo info;
    info.file = gz;
    info.project = v->str("project");
    info.name = v->str("name");
    info.at = v->str("at");
    info.turn = int(v->integer("turn", 0));
    info.size = fs::fileSize(gz).value_or(0);
    found.push_back({fs::mtime(gz).value_or(0), std::move(info)});
  }
  std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second.file < b.second.file; });
  std::vector<AutosaveInfo> out;
  for (auto& f : found) out.push_back(std::move(f.second));
  return out;
}

namespace {

std::optional<Autosave> loadAutosaveInfo(AutosaveInfo info, std::string* error) {
  std::string err;
  auto data = fs::readFile(info.file, &err);
  if (!data) {
    if (error) *error = err;
    return std::nullopt;
  }
  try {
    LoadResult r = unpack(std::span<const u8>(reinterpret_cast<const u8*>(data->data()), data->size()), "автосохранение");
    Autosave a;
    a.info = std::move(info);
    a.world = std::move(r.world);
    a.warnings = std::move(r.warnings);
    a.readOnly = std::move(r.readOnly);
    return a;
  } catch (const UserError& e) {
    if (error) *error = e.what();
    return std::nullopt;
  }
}

}  // namespace

std::optional<Autosave> loadAutosaveFor(const std::string& project, const std::string& dataDir, std::string* error) {
  std::string file = fs::join(fs::join(dataDirOr(dataDir), "autosave"), "autosave-" + autosaveSlot(project) + ".json.gz");
  for (auto& info : listAutosaves(dataDir))
    if (info.file == file) return loadAutosaveInfo(info, error);
  if (error) *error = "автосохранения этого мира нет";
  return std::nullopt;
}

void clearAutosave(const std::string& project, const std::string& dataDir) {
  std::string dir = fs::join(dataDirOr(dataDir), "autosave");
  std::string base = fs::join(dir, "autosave-" + autosaveSlot(project));
  fs::remove(base + ".json");
  fs::remove(base + ".json.gz");
}

}  // namespace rg::io
