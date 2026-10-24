// Regnum — трёхстороннее слияние миров для совместной работы нескольких сессий над одной папкой мира: база — мир на
// диске при открытии или последнем сохранении, наше — текущий мир сессии, их — мир на диске сейчас. Сравнение — по
// записям файлов мира в том виде, в каком они записываются (encode: округления и порядок уже учтены), сборка — разбором
// слитых файлов и нормализацией, как при чтении.
#include <charconv>
#include <unordered_set>

#include "base/jobs.h"
#include "core/io_internal.h"

namespace rg::io {

using namespace detail;
using json::Value;

namespace {

// ---------------------------------------------------------------- ссылки
// Ссылка вида «a7» (виды с префиксом): последовательность и номер; nullopt — не ссылка.
std::optional<std::pair<Seq, Id>> refOf(std::string_view s) {
  for (int i = 0; i < kSeqCount; i++) {
    const std::string_view p = schema::kSeqs[i].prefix;
    if (p.empty() || s.size() <= p.size() || s.substr(0, p.size()) != p) continue;
    const std::string_view num = s.substr(p.size());
    if (num[0] < '0' || num[0] > '9') continue;
    u64 n = 0;
    auto r = std::from_chars(num.data(), num.data() + num.size(), n);
    if (r.ec != std::errc() || r.ptr != num.data() + num.size() || n == 0 || n > 0xFFFFFFFFull) continue;
    return std::make_pair(Seq(i), Id(n));
  }
  return std::nullopt;
}

using IdMap = std::unordered_map<std::string, std::string>;   // прежняя ссылка -> новая

std::string remapKey(const std::string& k, const IdMap& m) {
  if (auto it = m.find(k); it != m.end()) return it->second;
  const size_t bar = k.find('|');   // пара отношений «f1|f2»
  if (bar == std::string::npos) return k;
  std::string a = k.substr(0, bar), b = k.substr(bar + 1);
  if (auto it = m.find(a); it != m.end()) a = it->second;
  if (auto it = m.find(b); it != m.end()) b = it->second;
  return a + "|" + b;
}

// Копия значения с переведёнными ссылками: строки-значения, ключи объектов, пары отношений.
Value remapped(const Value& v, const IdMap& m) {
  switch (v.type()) {
    case json::Type::String: {
      auto it = m.find(v.asStr());
      return it != m.end() ? Value(it->second) : v;
    }
    case json::Type::Array: {
      json::Array a;
      a.reserve(v.size());
      for (const Value& x : v.items()) a.push_back(remapped(x, m));
      return Value(std::move(a));
    }
    case json::Type::Object: {
      Value o = Value::object();
      for (const auto& [k, x] : v.members()) o.set(remapKey(k, m), remapped(x, m));
      return o;
    }
    default: return v;
  }
}

// ---------------------------------------------------------------- подписи для отчёта
std::string clip(std::string s, size_t chars) {
  if (utf8::count(s) <= chars) return s;
  size_t i = 0;
  for (size_t n = 0; n < chars && i < s.size(); n++) i = utf8::next(s, i);
  s.resize(i);
  return s + "…";
}

// «p12 «Арден»»: ключ записи и название (у записи хроники — начало текста).
std::string label(const std::string& key, const Value* rec) {
  if (!rec || !rec->isObj()) return key;
  std::string name = rec->str("name");
  if (name.empty()) name = rec->str("text");
  name = clip(trim(name), 40);
  return name.empty() ? key : key + " «" + name + "»";
}

bool same(const Value* a, const Value* b) {
  if (!a || !b) return a == b;
  return *a == *b;
}

// ---------------------------------------------------------------- слияние
struct Merger {
  MergeResult& r;

  // Решение по одной записи или полю: o — наша версия, t — с диска, b — базы; nullptr — нет (удалена, не было).
  // where(запись) — подпись для отчёта: принятое с диска подписывается версией с диска, конфликт — нашей.
  template <class W>
  const Value* decide(FileId f, W&& where, const Value* b, const Value* o, const Value* t) {
    if (same(o, t)) return o;
    if (same(o, b)) {   // изменено только на диске
      r.taken.push_back({kFiles[f].path, where(t ? t : b), !b ? "добавлено на диске" : !t ? "удалено на диске" : "изменено на диске"});
      r.theirTables |= kFiles[f].tables;
      return t;
    }
    if (same(t, b)) return o;   // изменено только здесь
    std::string msg = !o   ? "удалено здесь, изменено на диске — удалено"
                      : !t ? "изменено здесь, удалено на диске — оставлено"
                      : !b ? "добавлено здесь и на диске — оставлена версия этой сессии"
                           : "изменено здесь и на диске — оставлена версия этой сессии";
    r.conflicts.push_back({kFiles[f].path, where(o ? o : t), std::move(msg)});
    return o;
  }

  // Список записей с ключом key ("id" или "key"): наш порядок, затем добавленные на диске в их порядке.
  Value list(FileId f, const std::string& name, const Value& B, const Value& O, const Value& T, const char* key) {
    auto index = [&](const Value& L) {
      std::unordered_map<std::string, const Value*> m;
      for (const Value& e : L.items())
        if (e.isObj()) m.emplace(e.str(key), &e);
      return m;
    };
    auto bi = index(B), oi = index(O), ti = index(T);
    auto find = [](const std::unordered_map<std::string, const Value*>& m, const std::string& k) -> const Value* {
      auto it = m.find(k);
      return it == m.end() ? nullptr : it->second;
    };
    json::Array out;
    auto one = [&](const std::string& k, const Value* o, const Value* t) {
      auto where = [&](const Value* rec) { return (name.empty() ? std::string() : name + ".") + label(k, rec); };
      if (const Value* x = decide(f, where, find(bi, k), o, t)) out.push_back(*x);
    };
    for (const Value& o : O.items())
      if (o.isObj()) one(o.str(key), &o, find(ti, o.str(key)));
    for (const Value& t : T.items())
      if (t.isObj() && !oi.count(t.str(key))) one(t.str(key), nullptr, &t);
    return Value(std::move(out));
  }

  // Объект, члены которого — самостоятельные записи или поля (пары отношений, meta, settings): по членам.
  // special(k, b, o, t, out) — свой способ для члена (true — член обработан).
  template <class S>
  Value members(FileId f, const std::string& prefix, const Value& B, const Value& O, const Value& T, S&& special) {
    Value out = Value::object();
    auto one = [&](const std::string& k) {
      const Value* b = B.find(k);
      const Value* o = O.find(k);
      const Value* t = T.find(k);
      if (special(k, b, o, t, out)) return;
      if (const Value* x = decide(f, [&](const Value* rec) { return prefix + label(k, rec); }, b, o, t)) out.set(k, *x);
    };
    for (const auto& m : O.members()) one(m.first);
    for (const auto& m : T.members())
      if (!O.find(m.first)) one(m.first);
    return out;
  }
  Value members(FileId f, const std::string& prefix, const Value& B, const Value& O, const Value& T) {
    return members(f, prefix, B, O, T, [](const std::string&, const Value*, const Value*, const Value*, Value&) { return false; });
  }

  // meta: счётчики ID — наибольшие (и не меньше новых ID перенумерации), время сохранения — наше, счётчик случайных
  // событий — сумма приращений обеих сторон (броски не повторяются).
  Value meta(const Value& B, const Value& O, const Value& T, const std::array<u32, kSeqCount>& minSeq) {
    return members(F_WORLD, "meta.", B, O, T, [&](const std::string& k, const Value* b, const Value* o, const Value* t, Value& out) {
      if (k == "updatedAt") {
        if (o) out.set(k, *o);
        else if (t) out.set(k, *t);
        return true;
      }
      if (k == "seq") {
        Value s = Value::object();
        for (int i = 0; i < kSeqCount; i++) {
          const char* name = schema::kSeqs[i].name;
          i64 v = std::max<i64>(o ? o->integer(name, 0) : 0, t ? t->integer(name, 0) : 0);
          s.set(name, std::max<i64>(v, minSeq[size_t(i)]));
        }
        out.set(k, std::move(s));
        return true;
      }
      if (k == "rng") {
        const i64 bv = b ? b->asInt(0) : 0, ov = o ? o->asInt(0) : 0, tv = t ? t->asInt(0) : 0;
        out.set(k, std::clamp<i64>(ov + tv - bv, std::max(ov, tv), i64(0xFFFFFFFFu)));
        return true;
      }
      return false;
    });
  }

  // Файл целиком (геометрия, объекты карты).
  Value whole(FileId f, const char* what, const Value& B, const Value& O, const Value& T) {
    if (O == T) return O;
    if (O == B) {
      r.taken.push_back({kFiles[f].path, "", std::string(what) + " изменены на диске"});
      r.theirTables |= kFiles[f].tables;
      return T;
    }
    if (T == B) return O;
    r.conflicts.push_back({kFiles[f].path, "", std::string(what) + " изменены здесь и на диске — оставлена версия этой сессии"});
    return O;
  }

  Value file(FileId f, const Value& B, const Value& O, const Value& T, const std::array<u32, kSeqCount>& minSeq) {
    if (O == T || T == B) return O;
    if (O == B) {   // изменено только на диске: файл с диска как есть, отчёт — по записям
      (void)byRecords(f, B, O, T, minSeq);
      return T;
    }
    return byRecords(f, B, O, T, minSeq);
  }

  Value byRecords(FileId f, const Value& B, const Value& O, const Value& T, const std::array<u32, kSeqCount>& minSeq) {
    switch (f) {
      case F_GEO: return whole(f, "Границы и берег", B, O, T);
      case F_MAP: return whole(f, "Объекты карты", B, O, T);
      case F_WORLD:
        return members(f, "", B, O, T, [&](const std::string& k, const Value* b, const Value* o, const Value* t, Value& out) {
          static const Value empty = Value::object();
          if (k == "meta") {
            out.set(k, meta(b ? *b : empty, o ? *o : empty, t ? *t : empty, minSeq));
            return true;
          }
          if (k == "settings") {
            out.set(k, members(f, "settings.", b ? *b : empty, o ? *o : empty, t ? *t : empty));
            return true;
          }
          if (o) out.set(k, *o);   // format, version
          return true;
        });
      case F_RELATIONS: {
        Value out = Value::object();
        out.set("relations", members(f, "", B.obj("relations"), O.obj("relations"), T.obj("relations")));
        return out;
      }
      case F_CONSTANTS: {
        Value out = Value::object();
        out.set("constants", list(f, "", B.get("constants"), O.get("constants"), T.get("constants"), "key"));
        return out;
      }
      case F_CATALOGS:
        // Списки справочников — по записям (ID); остальное — по полям.
        return members(f, "", B, O, T, [&](const std::string& k, const Value* b, const Value* o, const Value* t, Value& out) {
          if (!(o ? *o : *t).isArr()) return false;
          static const Value none;
          out.set(k, list(f, k, b ? *b : none, o ? *o : none, t ? *t : none, "id"));
          return true;
        });
      default: {
        const char* key = kFiles[f].key;
        if (!key || !*key) return O;
        Value out = Value::object();
        out.set(key, list(f, "", B.get(key), O.get(key), T.get(key), "id"));
        return out;
      }
    }
  }
};

// Записи с ID у файла: таблица (массив под ключом файла) или списки справочников.
template <class F>
void eachIdList(FileId f, const Value& v, F&& fn) {
  if (f == F_CATALOGS) {
    for (const auto& [k, x] : v.members())
      if (x.isArr()) fn(k, x);
    return;
  }
  if (f == F_WORLD || f == F_GEO || f == F_MAP || f == F_RELATIONS || f == F_CONSTANTS) return;
  const char* key = kFiles[f].key;
  if (key && *key) fn(std::string(key), v.get(key));
}

}  // namespace

MergeResult merge3(const World& base, const World& ours, const World& theirs) {
  MergeResult r;
  // Файлы трёх сторон; файлы, которые наша сессия не трогала, совпадают с базой (те же блоки таблиц).
  const u32 touched = World::diff(ours, base);
  std::vector<Value> B(F_COUNT), O(F_COUNT), T(F_COUNT);
  std::vector<char> ownO(F_COUNT, 0);
  for (int f = 0; f < F_COUNT; f++) ownO[size_t(f)] = (kFiles[f].tables & touched) != 0;
  jobs::parallelFor(size_t(F_COUNT) * 3, [&](size_t i) {
    const FileId f = FileId(i / 3);
    switch (i % 3) {
      case 0: B[size_t(f)] = encode(base, f); break;
      case 1: if (ownO[size_t(f)]) O[size_t(f)] = encode(ours, f); break;
      default: T[size_t(f)] = encode(theirs, f); break;
    }
  }, 1);
  for (int f = 0; f < F_COUNT; f++)
    if (!ownO[size_t(f)]) O[size_t(f)] = B[size_t(f)];

  // Наши новые записи, чей ID на диске занят другой новой записью, получают новые ID (записи с диска уже видят
  // другие сессии — их ID не меняются); ссылки на них в нашем мире переводятся.
  std::array<u32, kSeqCount> next{};
  for (int i = 0; i < kSeqCount; i++) next[size_t(i)] = std::max(ours.meta->seq[size_t(i)], theirs.meta->seq[size_t(i)]);
  auto bump = [&](const Value& L) {
    for (const Value& e : L.items())
      if (auto ref = refOf(e.str("id"))) next[size_t(ref->first)] = std::max(next[size_t(ref->first)], ref->second);
  };
  for (int f = 0; f < F_COUNT; f++) {
    eachIdList(FileId(f), O[size_t(f)], [&](const std::string&, const Value& L) { bump(L); });
    eachIdList(FileId(f), T[size_t(f)], [&](const std::string&, const Value& L) { bump(L); });
  }
  IdMap ids;
  for (int fi = 0; fi < F_COUNT; fi++) {
    const FileId f = FileId(fi);
    if (!ownO[size_t(f)]) continue;
    eachIdList(f, O[size_t(f)], [&](const std::string& name, const Value& L) {
      const Value& BL = B[size_t(f)].get(name);
      const Value& TL = T[size_t(f)].get(name);
      std::unordered_set<std::string> inBase;
      for (const Value& e : BL.items()) inBase.insert(e.str("id"));
      std::unordered_map<std::string, const Value*> inTheirs;
      for (const Value& e : TL.items()) inTheirs.emplace(e.str("id"), &e);
      for (const Value& e : L.items()) {
        const std::string id = e.str("id");
        if (inBase.count(id)) continue;
        auto t = inTheirs.find(id);
        if (t == inTheirs.end() || *t->second == e) continue;
        auto ref = refOf(id);
        if (!ref) continue;
        const Id nid = ++next[size_t(ref->first)];
        const std::string to = refStr(ref->first, nid);
        ids[id] = to;
        r.ids.push_back({id, to});
        r.renamed.push_back({kFiles[f].path, (f == F_CATALOGS ? name + "." : std::string()) + label(id, &e),
                             "ID занят записью, добавленной на диске, — новый ID " + to});
      }
    });
  }
  if (!ids.empty())
    for (int f = 0; f < F_COUNT; f++)
      if (ownO[size_t(f)]) O[size_t(f)] = remapped(O[size_t(f)], ids);

  // Слияние по файлам (отчёт — в порядке файлов мира).
  Merger m{r};
  std::vector<Value> M(F_COUNT);
  for (int f = 0; f < F_COUNT; f++) M[size_t(f)] = m.file(FileId(f), B[size_t(f)], O[size_t(f)], T[size_t(f)], next);
  // Наша сторона изменила meta (например, только время сохранения), а диск — нет, но счётчики должны вместить новые ID.
  if (!ids.empty()) {
    Value& meta = M[F_WORLD]["meta"];
    Value seq = Value::object();
    for (int i = 0; i < kSeqCount; i++) {
      const char* name = schema::kSeqs[i].name;
      seq.set(name, std::max<i64>(meta.obj("seq").integer(name, 0), next[size_t(i)]));
    }
    meta.set("seq", std::move(seq));
  }

  // Сборка мира: разбор слитых файлов и нормализация (исправления — в отчёт).
  Parts parts;
  Warnings warnFiles[F_COUNT];
  jobs::parallelFor(size_t(F_COUNT), [&](size_t i) { decode(M[i], FileId(i), parts, warnFiles[i]); }, 1);
  for (int f = 0; f < F_COUNT; f++) r.fixes.insert(r.fixes.end(), warnFiles[f].begin(), warnFiles[f].end());
  u32 fixed = 0;
  r.world = assemble(std::move(parts), r.fixes, &fixed);
  return r;
}

}  // namespace rg::io
