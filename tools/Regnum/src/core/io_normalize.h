// Regnum — нормализация мира (внутреннее, только для core/io_normalize*.cpp): разборщик Norm с общими помощниками.
// Шаги по областям: справочники и константы — io_normalize_catalogs.cpp, геометрия и объекты карты — io_normalize_map.cpp,
// фракции и персонажи — io_normalize_factions.cpp, модификаторы, постройки и технологии — io_normalize_defs.cpp,
// провинции — io_normalize_provinces.cpp, войска, маршруты, сделки, хроника и отношения — io_normalize_armies.cpp;
// помощники, базовые записи, счётчики и порядок шагов (run) — io_normalize.cpp.
#pragma once
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "core/io_internal.h"

namespace rg::io::norm {

using namespace rg::io::detail;
using schema::CatalogList;

constexpr int kMaxTurn = 1000000;
constexpr int kMaxTurns = 1000;          // длительность строительства/исследования, ходов
using schema::kMaxCount;
constexpr int kMinAutosave = 10, kMaxAutosave = 3600;
using schema::kMaxShapeWidth, schema::kMaxSymbolScale, schema::kMinShapeWidth, schema::kMinSymbolScale;

// Родительный падеж для сообщений «нет провинции p12».
inline const char* genitive(Seq s) { return schema::seqInfo(s).genitive; }

inline std::string ns(double v) {
  std::string s;
  json::appendNumber(s, v);
  return s;
}
inline std::string ref(Seq s, Id id) { return id ? refStr(s, id) : std::string("0"); }

inline bool isPng(const std::string& b) { return b.size() >= 8 && std::memcmp(b.data(), "\x89PNG\r\n\x1a\n", 8) == 0; }
inline bool isJpeg(const std::string& b) { return b.size() >= 3 && u8(b[0]) == 0xFF && u8(b[1]) == 0xD8 && u8(b[2]) == 0xFF; }

struct Norm {
  Tx& tx;
  Warnings& out;
  std::array<u32, kSeqCount> seq{};
  std::unordered_set<Id> cat[schema::kCatalogListCount];   // ID простых справочников (индекс — CatalogList)
  std::array<int, kSeqCount> catOfSeq{};                    // последовательность → индекс cat (−1 — не справочник)
  std::unordered_set<Id> relicIds, specialIds, groupIds, relicGroupIds, classIds, siteIds, chestIds;
  std::unordered_set<Id> relicTaken;  // реликвии, у которых уже есть место (герой, постройка, государство, тайник)
  std::unordered_set<Id> heroUsed;  // герои в гарнизонах и войсках (каждый — не больше чем в одном месте)
  std::map<std::tuple<Id, bool, Id>, Id> rowRemap;  // (фракция, флот, прежний ID) -> новый ID строки

  explicit Norm(Tx& t, Warnings& o) : tx(t), out(o) {
    seq = t.w().meta->seq;
    catOfSeq.fill(-1);
    for (const schema::CatalogListInfo& L : schema::kCatalogLists) catOfSeq[size_t(L.seq)] = int(L.list);
  }
  std::unordered_set<Id>& resourceIds() { return cat[int(CatalogList::Resources)]; }
  std::unordered_set<Id>& essenceIds() { return cat[int(CatalogList::Essences)]; }

  const World& w() const { return tx.w(); }
  void warn(FileId f, std::string where, std::string msg) { out.push_back(Warning{kFiles[f].path, std::move(where), std::move(msg)}); }

  // Новый ID последовательности (не меньше уже занятых).
  Id fresh(Seq s, u32 maxInData);

  bool hasCat(Seq s, Id id) const;
  bool exists(Seq s, Id id) const;
  bool isState(Id id) const { const Faction* f = w().faction(id); return f && f->isState(); }
  bool isGuild(Id id) const { const Faction* f = w().faction(id); return f && f->isGuild(); }

  // Одиночная ссылка: несуществующая обнуляется.
  bool fixRef(Id& id, Seq s, FileId f, const std::string& base, const char* field);
  // Список ссылок: несуществующие и повторы удаляются.
  template <class Pred>
  bool fixRefList(std::vector<Id>& v, Seq s, FileId f, const std::string& base, const char* field, Pred&& valid,
                  const char* invalidWhy = nullptr) {
    bool bad = false;
    for (size_t i = 0; i < v.size() && !bad; i++) {
      if (!v[i] || !valid(v[i])) bad = true;
      for (size_t k = 0; k < i && !bad; k++) bad = v[k] == v[i];
    }
    if (!bad) return false;
    std::vector<Id> res;
    res.reserve(v.size());
    for (Id id : v) {
      if (!id) continue;
      if (!valid(id)) {
        if (!exists(s, id)) warn(f, base + "." + field, std::string("нет ") + genitive(s) + " " + refStr(s, id) + " — ссылка удалена");
        else warn(f, base + "." + field, refStr(s, id) + ": " + (invalidWhy ? invalidWhy : "недопустимая ссылка") + " — удалена");
        continue;
      }
      if (std::find(res.begin(), res.end(), id) != res.end()) {
        warn(f, base + "." + field, "повтор " + refStr(s, id) + " — удалён");
        continue;
      }
      res.push_back(id);
    }
    v = std::move(res);
    return true;
  }
  bool fixRefList(std::vector<Id>& v, Seq s, FileId f, const std::string& base, const char* field);
  // Стопки реликвий: реликвии справочника, повторы сложены, количество ≥ 1; уникальная (легендарная, эпохальная) —
  // одна штука и только в одном месте мира (relicTaken).
  bool fixStacks(std::vector<RelicStack>& v, FileId f, const std::string& base, const char* field);

  // Число: конечное и в пределах.
  template <class T>
  bool fixNum(T& v, T lo, T hi, T def, FileId f, const std::string& base, const char* field) {
    if constexpr (std::is_floating_point_v<T>) {
      if (!std::isfinite(v)) {
        warn(f, base + "." + field, "значение не является числом — взято " + ns(double(def)));
        v = def;
        return true;
      }
    }
    if (v < lo || v > hi) {
      T c = clamp(v, lo, hi);
      warn(f, base + "." + field, "значение " + ns(double(v)) + " вне пределов " + ns(double(lo)) + "…" + ns(double(hi)) + " — взято " + ns(double(c)));
      v = c;
      return true;
    }
    return false;
  }
  bool fixFinite(double& v, FileId f, const std::string& base, const char* field);
  // Срок модификаторов: только модификаторы из списка сущности, 1…kMaxTurns ходов.
  bool fixModTurns(ModTurns& t, const std::vector<Id>& mods, FileId f, const std::string& base);
  // Список записей с ключом (раса, строка, гильдия): keep(x, at, ch) проверяет и исправляет запись (false — запись
  // удаляется, предупреждение — в keep; исправление отмечается в ch); повтор ключа — предупреждение dup(x) и сложение
  // merge(первая, повтор). Список заменяется, только если что-то исправлено.
  template <class T, class Key, class Keep, class Dup, class Merge>
  bool fixKeyed(std::vector<T>& list, FileId f, const std::string& base, const char* field, Key&& key, Keep&& keep, Dup&& dup,
                Merge&& merge) {
    if (list.empty()) return false;
    std::vector<T> res;
    bool ch = false;
    for (size_t i = 0; i < list.size(); i++) {
      T x = list[i];
      const std::string at = base + "." + field + "[" + std::to_string(i) + "]";
      if (!keep(x, at, ch)) {
        ch = true;
        continue;
      }
      auto same = std::find_if(res.begin(), res.end(), [&](const T& y) { return key(y) == key(x); });
      if (same != res.end()) {
        warn(f, at, dup(x));
        merge(*same, x);
        ch = true;
        continue;
      }
      res.push_back(x);
    }
    if (ch) list = std::move(res);
    return ch;
  }
  static Id rowOf(const GarrisonEntry& g) { return g.row; }
  static void addCount(GarrisonEntry& a, const GarrisonEntry& b) { a.count = std::min(kMaxCount, a.count + b.count); }
  static std::string rowsAdded(const GarrisonEntry& g) { return "повтор строки " + refStr(Seq::Row, g.row) + " — численности сложены"; }
  // Записи «строка → число» (гарнизоны, корабли в торговле): строки фракции, без повторов, 0…kMaxCount.
  // valid(row) — строка существует; remap — перевод занятых ID строк.
  template <class Valid, class Remap>
  bool fixRowCounts(std::vector<GarrisonEntry>& list, FileId f, const std::string& base, const char* field, Valid&& valid, Remap&& remap,
                    const std::string& whyMissing) {
    return fixKeyed(list, f, base, field, rowOf,
                    [&](GarrisonEntry& g, const std::string& at, bool& ch) {
                      if (Id row = remap(g.row); row != g.row) {
                        g.row = row;
                        ch = true;
                      }
                      if (!valid(g.row)) {
                        warn(f, at + ".row", whyMissing + " " + ref(Seq::Row, g.row) + " — запись удалена");
                        return false;
                      }
                      ch |= fixNum(g.count, i64(0), kMaxCount, i64(0), f, at, "count");
                      return true;
                    },
                    rowsAdded, addCount);
  }
  template <class E>
  bool fixEnum(E& v, int n, E def, FileId f, const std::string& base, const char* field) {
    if (int(v) >= 0 && int(v) < n) return false;
    warn(f, base + "." + field, "недопустимое значение перечисления — взято по умолчанию");
    v = def;
    return true;
  }
  bool fixPoint(Vec2& p, bool map, FileId f, const std::string& base, const char* field);
  bool fixPoints(std::vector<Vec2>& pts, FileId f, const std::string& base, const char* field);

  // ---------------------------------------------------------------- шаги
  void meta();

  void catalogs();

  // Наборы ID справочников для проверки ссылок (после правки справочников и после базовых записей).
  void catalogSets();

  // Группы дерева справочника: ID без повторов, родитель — существующая группа без циклов (цикл разрывается у первой
  // группы цикла); keyProblem(ключ, уже занятые) — почему ключ снимается (пусто — ключ допустим).
  template <class Tag, class KeyProblem>
  bool fixTree(std::vector<TreeGroup<Tag>>& groups, Seq s, const char* key, KeyProblem&& keyProblem) {
    bool ch = fixIds(groups, s, key, "группа");
    std::unordered_set<Id> seen;
    for (auto& g : groups) seen.insert(g.id);
    std::unordered_set<std::string> keys;
    for (auto& g : groups) {
      const std::string where = std::string(key) + "." + refStr(s, g.id);
      if (g.parent && (!seen.count(g.parent) || g.parent == g.id)) {
        warn(F_CATALOGS, where + ".parent", "нет группы " + refStr(s, g.parent) + " — группа стала группой верхнего уровня");
        g.parent = 0;
        ch = true;
      }
      if (!g.key.empty())
        if (const std::string why = keyProblem(g.key, keys); !why.empty()) {
          warn(F_CATALOGS, where + ".key", why);
          g.key.clear();
          ch = true;
        }
    }
    for (auto& g : groups) {
      const auto parentOf = [&](Id p) {
        const TreeGroup<Tag>* x = findById(groups, p);
        return x ? x->parent : Id(0);
      };
      if (chainReaches(g.parent, g.id, 256, parentOf)) {
        warn(F_CATALOGS, std::string(key) + "." + refStr(s, g.id) + ".parent", "группы замыкаются в цикл — группа стала группой верхнего уровня");
        g.parent = 0;
        ch = true;
      }
    }
    return ch;
  }

  // Группы ресурсов: ключи базовых групп — известные и без повторов; группа ресурса — существующая.
  bool resGroups(Catalogs& c);

  bool relics(Catalogs& c);

  // ID записи справочника: нет или повтор — новый (fresh).
  template <class T>
  bool fixIds(std::vector<T>& list, Seq s, const char* key, const char* noun) {
    bool ch = false;
    u32 maxId = 0;
    for (auto& x : list) maxId = std::max(maxId, x.id);
    std::unordered_set<Id> seen;
    for (auto& x : list) {
      if (x.id == 0 || seen.count(x.id)) {
        Id old = x.id;
        x.id = fresh(s, maxId);
        warn(F_CATALOGS, std::string(key) + "." + refStr(s, x.id),
             old ? "повторный ID " + refStr(s, old) + " — назначен новый" : std::string(noun) + " без ID — назначен новый");
        ch = true;
      }
      seen.insert(x.id);
    }
    return ch;
  }

  // Группы реликвий: ключ — только «Археологические находки», без повторов.
  bool relicGroups(Catalogs& c);

  // Сундуки сокровищ: поля своего вида, ссылки на существующие записи, вложенные сундуки — без циклов.
  bool chests(Catalogs& c);

  // Археологические места: постоянный список (ключи базовых мест, без повторов), награды — существующие сундуки.
  bool archSites(Catalogs& c);

  // Классы героев: таланты с уникальными ID во всём мире, ярус и столбец ≥ 0, стоимость 1…5, предшествующий талант —
  // того же класса и без циклов, модификаторы — существующие.
  bool classes(Catalogs& c);

  // Карта «ссылка → количество ≥ 0»: только существующие записи справочника s (остальные — «… — <removed>»).
  bool fixAmounts(std::map<Id, double>& m, Seq s, const std::unordered_set<Id>& ids, FileId f, const std::string& base, const char* field,
                  const char* removed = "позиция удалена");

  // Цена юнита: ключевой ресурс по типу, дополнительные ресурсы и эссенции ≥ 0, содержание эссенциями — у элементалей.
  template <class Row>
  bool fixUnitCost(Row& r, const Catalogs& c, const std::unordered_set<Id>& resIds, FileId f, const std::string& at, bool special = false) {
    bool ch = false;
    const schema::KeyRule k = schema::keyRule(r.type);
    if (special) {
      // Особый отряд задаёт свои цены: ключевой ресурс — любой ресурс справочника, не меньше 1 на юнит.
      if (r.keyRes && !resIds.count(r.keyRes)) {
        warn(f, at + ".keyRes", "нет ресурса " + refStr(Seq::Resource, r.keyRes) + " — ключевой ресурс снят");
        r.keyRes = 0;
        ch = true;
      }
      if (!std::isfinite(r.keyPer) || r.keyPer < 1 || r.keyPer > 1e12) {
        r.keyPer = std::isfinite(r.keyPer) ? clamp(r.keyPer, 1.0, 1e12) : 1.0;
        ch = true;
      }
    } else if (r.keyRes && !schema::keyAllowed(c, r.type, r.keyRes)) {
      warn(f, at + ".keyRes", refStr(Seq::Resource, r.keyRes) + " не подходит ключевым ресурсом для типа «" + schema::unitType(r.type).name + "» — снят");
      r.keyRes = 0;
      ch = true;
    }
    if (!special && (!std::isfinite(r.keyPer) || r.keyPer < 1 || r.keyPer > 1e12 || (k.fixedOne && r.keyPer != 1))) {
      r.keyPer = k.fixedOne || !std::isfinite(r.keyPer) ? 1.0 : clamp(r.keyPer, 1.0, 1e12);
      ch = true;
    }
    ch |= fixAmounts(r.extra, Seq::Resource, resIds, f, at, "extra");
    ch |= fixAmounts(r.essence, Seq::Essence, essenceIds(), f, at, "essence");
    ch |= fixAmounts(r.essUpkeep, Seq::Essence, essenceIds(), f, at, "essUpkeep");
    if (schema::isElemental(r.type)) {
      if (r.keyRes) {   // и без ключевого ресурса
        warn(f, at + ".keyRes", "элементали нанимаются только за эссенции — ключевой ресурс снят");
        r.keyRes = 0;
        r.keyPer = 1;
        ch = true;
      }
      if (!r.extra.empty()) {   // элементали нанимаются только за эссенции
        warn(f, at + ".extra", "элементали нанимаются только за эссенции — ресурсы сняты");
        r.extra.clear();
        ch = true;
      }
      if (!r.race.empty() && r.race != schema::kRaceElemental) {
        warn(f, at + ".race", "раса элементалей — только «Элементали»");
        r.race = schema::kRaceElemental;
        ch = true;
      }
    } else if (!r.essUpkeep.empty()) {
      r.essUpkeep.clear();
      ch = true;
    }
    return ch;
  }

  bool specials(Catalogs& c);

  // Запасы «ссылка → количество» любого знака: только существующие записи, конечные числа.
  bool fixAmountsSigned(std::map<Id, double>& m, Seq s, const std::unordered_set<Id>& ids, FileId f, const std::string& base, const char* field);

  // Базовые записи справочников (core/content.h): мир прежней версии дополняется один раз.
  void content();

  // Глобальные константы: ключи непусты и уникальны; встроенные — своего типа; числа конечны; ресурсы — из справочника.
  void constants();

  void nodes();

  void edges();

  // Знаки и фигуры карты: пределы масштаба и ширины, точки на карте, достаточное число вершин.
  void mapObjects();

  void factions();

  void characters();

  void modifiers();
  std::unordered_set<std::string> modKeys;

  // Разорвать циклы графа требований: deps(id) — список зависимостей; удаляются обратные рёбра (обход по возрастанию ID).
  template <class GetDeps>
  std::set<std::pair<Id, Id>> cycleEdges(const std::vector<Id>& ids, GetDeps&& deps) {
    std::set<std::pair<Id, Id>> cut;
    std::unordered_map<Id, u8> color;  // 0 — белый, 1 — в стеке, 2 — готово
    for (Id start : ids) {
      if (color[start]) continue;
      std::vector<std::pair<Id, size_t>> stack{{start, 0}};
      color[start] = 1;
      while (!stack.empty()) {
        auto& [id, i] = stack.back();
        const std::vector<Id>& d = deps(id);
        if (i >= d.size()) {
          color[id] = 2;
          stack.pop_back();
          continue;
        }
        Id next = d[i++];
        u8& c = color[next];
        if (c == 1) cut.insert({id, next});
        else if (c == 0) {
          c = 1;
          stack.push_back({next, 0});
        }
      }
    }
    return cut;
  }

  std::set<std::pair<Id, std::string>> buildingKeys;
  void buildings();

  std::set<std::pair<Id, std::string>> techKeys;
  void techs();

  Id remapRow(Id faction, bool fleet, Id row) const;

  void provinces();
  std::map<Id, std::vector<RelicStack>> orphanRelics;

  void armies();

  void routes();

  void deals();

  // Хроника хранит историю: ссылки на удалённые провинции, войска и фракции допустимы и не проверяются.
  void log();

  void relations();

  // Счётчики ID не меньше наибольших занятых ID.
  void counters();

  // Героям государств без модификатора природы — природа по виду государства. Один раз:
  // только пока в мире нет записи этого встроенного модификатора (мир сохранён до появления видов государств);
  // после — природу снимают и меняют по правилам модификаторов. Выполняется после counters (выдаются новые ID).
  void natures();

  void run();
};

}  // namespace rg::io::norm
