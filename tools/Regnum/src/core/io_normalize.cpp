// Regnum — нормализация мира: пределы значений, висячие ссылки, повторы, инварианты схемы.
// Работает на любом World (прочитанном из файлов, исправленном вручную или агентом): после неё
// редактор и правила могут полагаться на целостность ссылок.
#include <set>
#include <tuple>
#include <unordered_set>

#include "core/content.h"
#include "core/io_internal.h"

namespace rg::io {

using namespace detail;

namespace {

constexpr int kMaxTurn = 1000000;
constexpr int kMaxTurns = 1000;          // длительность строительства/исследования, ходов
constexpr i64 kMaxCount = 1000000000000000LL;  // численности и население (точно в double)
constexpr int kMinAutosave = 10, kMaxAutosave = 3600;
using schema::kMaxShapeWidth, schema::kMaxSymbolScale, schema::kMinShapeWidth, schema::kMinSymbolScale;

// Родительный падеж для сообщений «нет провинции p12».
const char* genitive(Seq s) {
  switch (s) {
    case Seq::Province: return "провинции";
    case Seq::Faction: return "фракции";
    case Seq::Character: return "персонажа";
    case Seq::Modifier: return "модификатора";
    case Seq::Building: return "постройки";
    case Seq::Tech: return "технологии";
    case Seq::Army: return "войска";
    case Seq::Route: return "маршрута";
    case Seq::Deal: return "сделки";
    case Seq::Log: return "записи хроники";
    case Seq::Row: return "строки войск";
    case Seq::Council: return "места совета";
    case Seq::Node: return "узла";
    case Seq::Edge: return "дуги";
    case Seq::Resource: return "ресурса";
    case Seq::Race: return "расы";
    case Seq::Culture: return "культуры";
    case Seq::Religion: return "религии";
    case Seq::Government: return "формы правления";
    case Seq::Position: return "должности";
    case Seq::Symbol: return "знака карты";
    case Seq::Shape: return "фигуры карты";
    case Seq::Essence: return "эссенции";
    case Seq::Relic: return "реликвии";
    case Seq::Special: return "особого отряда";
    case Seq::ResGroup: return "группы ресурсов";
    default: return "объекта";
  }
}

std::string ns(double v) {
  std::string s;
  json::appendNumber(s, v);
  return s;
}
std::string ref(Seq s, Id id) { return id ? refStr(s, id) : std::string("0"); }

bool isPng(const std::string& b) { return b.size() >= 8 && std::memcmp(b.data(), "\x89PNG\r\n\x1a\n", 8) == 0; }
bool isJpeg(const std::string& b) { return b.size() >= 3 && u8(b[0]) == 0xFF && u8(b[1]) == 0xD8 && u8(b[2]) == 0xFF; }

struct Norm {
  Tx& tx;
  Warnings& out;
  std::array<u32, kSeqCount> seq{};
  std::unordered_set<Id> cat[7];  // ресурсы, расы, культуры, религии, формы правления, должности, эссенции
  std::unordered_set<Id> relicIds, specialIds, groupIds;
  std::unordered_set<Id> heroUsed;  // герои в гарнизонах и войсках (каждый — не больше чем в одном месте)
  std::map<std::tuple<Id, bool, Id>, Id> rowRemap;  // (фракция, флот, прежний ID) -> новый ID строки

  explicit Norm(Tx& t, Warnings& o) : tx(t), out(o) { seq = t.w().meta->seq; }

  const World& w() const { return tx.w(); }
  void warn(FileId f, std::string where, std::string msg) { out.push_back(Warning{kFiles[f].path, std::move(where), std::move(msg)}); }

  // Новый ID последовательности (не меньше уже занятых).
  Id fresh(Seq s, u32 maxInData) {
    u32& c = seq[size_t(s)];
    c = std::max(c, maxInData);
    return ++c;
  }

  bool hasCat(Seq s, Id id) const {
    switch (s) {
      case Seq::Resource: return cat[0].count(id) > 0;
      case Seq::Race: return cat[1].count(id) > 0;
      case Seq::Culture: return cat[2].count(id) > 0;
      case Seq::Religion: return cat[3].count(id) > 0;
      case Seq::Government: return cat[4].count(id) > 0;
      case Seq::Position: return cat[5].count(id) > 0;
      case Seq::Essence: return cat[6].count(id) > 0;
      case Seq::Relic: return relicIds.count(id) > 0;
      case Seq::Special: return specialIds.count(id) > 0;
      case Seq::ResGroup: return groupIds.count(id) > 0;
      default: return false;
    }
  }
  bool exists(Seq s, Id id) const {
    switch (s) {
      case Seq::Province: return w().provinces.has(id);
      case Seq::Faction: return w().factions.has(id);
      case Seq::Character: return w().characters.has(id);
      case Seq::Modifier: return w().modifiers.has(id);
      case Seq::Building: return w().buildings.has(id);
      case Seq::Tech: return w().techs.has(id);
      case Seq::Army: return w().armies.has(id);
      case Seq::Node: return w().nodes.has(id);
      default: return hasCat(s, id);
    }
  }
  bool isState(Id id) const { const Faction* f = w().faction(id); return f && f->isState(); }
  bool isGuild(Id id) const { const Faction* f = w().faction(id); return f && f->isGuild(); }

  // Одиночная ссылка: несуществующая обнуляется.
  bool fixRef(Id& id, Seq s, FileId f, const std::string& base, const char* field) {
    if (!id || exists(s, id)) return false;
    warn(f, base + "." + field, std::string("нет ") + genitive(s) + " " + refStr(s, id) + " — ссылка удалена");
    id = 0;
    return true;
  }
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
  bool fixRefList(std::vector<Id>& v, Seq s, FileId f, const std::string& base, const char* field) {
    return fixRefList(v, s, f, base, field, [&](Id id) { return exists(s, id); });
  }

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
  bool fixFinite(double& v, FileId f, const std::string& base, const char* field) {
    if (std::isfinite(v)) return false;
    warn(f, base + "." + field, "значение не является числом — взят 0");
    v = 0;
    return true;
  }
  // Срок модификаторов: только модификаторы из списка сущности, 1…kMaxTurns ходов.
  bool fixModTurns(ModTurns& t, const std::vector<Id>& mods, FileId f, const std::string& base) {
    bool ch = false;
    for (auto it = t.begin(); it != t.end();) {
      if (std::find(mods.begin(), mods.end(), it->first) == mods.end()) {
        warn(f, base + ".modTurns", refStr(Seq::Modifier, it->first) + " нет в списке модификаторов — срок удалён");
        it = t.erase(it);
        ch = true;
        continue;
      }
      if (it->second < 1 || it->second > kMaxTurns) {
        int c = clamp(it->second, 1, kMaxTurns);
        warn(f, base + ".modTurns." + refStr(Seq::Modifier, it->first), "срок " + std::to_string(it->second) + " вне пределов 1…" +
                                                                          std::to_string(kMaxTurns) + " — взято " + std::to_string(c));
        it->second = c;
        ch = true;
      }
      ++it;
    }
    return ch;
  }
  // Записи «строка → число» (гарнизоны, корабли в торговле): строки фракции, без повторов, 0…kMaxCount.
  // valid(row) — строка существует; remap — перевод занятых ID строк.
  template <class Valid, class Remap>
  bool fixRowCounts(std::vector<GarrisonEntry>& list, FileId f, const std::string& base, const char* field, Valid&& valid, Remap&& remap,
                    const std::string& whyMissing) {
    if (list.empty()) return false;
    std::vector<GarrisonEntry> res;
    bool ch = false;
    for (size_t i = 0; i < list.size(); i++) {
      GarrisonEntry g = list[i];
      std::string at = base + "." + field + "[" + std::to_string(i) + "]";
      Id row = remap(g.row);
      if (row != g.row) { g.row = row; ch = true; }
      if (!valid(g.row)) {
        warn(f, at + ".row", whyMissing + " " + ref(Seq::Row, g.row) + " — запись удалена");
        ch = true;
        continue;
      }
      ch |= fixNum(g.count, i64(0), kMaxCount, i64(0), f, at, "count");
      auto same = std::find_if(res.begin(), res.end(), [&](const GarrisonEntry& x) { return x.row == g.row; });
      if (same != res.end()) {
        warn(f, at, "повтор строки " + refStr(Seq::Row, g.row) + " — численности сложены");
        same->count = std::min(kMaxCount, same->count + g.count);
        ch = true;
        continue;
      }
      res.push_back(g);
    }
    if (ch) list = std::move(res);
    return ch;
  }
  template <class E>
  bool fixEnum(E& v, int n, E def, FileId f, const std::string& base, const char* field) {
    if (int(v) >= 0 && int(v) < n) return false;
    warn(f, base + "." + field, "недопустимое значение перечисления — взято по умолчанию");
    v = def;
    return true;
  }
  bool fixPoint(Vec2& p, bool map, FileId f, const std::string& base, const char* field) {
    bool ch = false;
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
      warn(f, base + "." + field, "координаты не являются числами — взято [0, 0]");
      p = {};
      ch = true;
    }
    if (map) {
      Vec2 c{clamp(p.x, 0.0, schema::kMapWidth), clamp(p.y, 0.0, schema::kMapHeight)};
      if (c != p) {
        warn(f, base + "." + field, "точка [" + ns(p.x) + ", " + ns(p.y) + "] вне карты — перенесена на край");
        p = c;
        ch = true;
      }
    }
    return ch;
  }
  bool fixPoints(std::vector<Vec2>& pts, FileId f, const std::string& base, const char* field) {
    bool ch = false;
    size_t bad = 0;
    for (const Vec2& p : pts) if (!std::isfinite(p.x) || !std::isfinite(p.y)) bad++;
    if (bad) {
      pts.erase(std::remove_if(pts.begin(), pts.end(), [](const Vec2& p) { return !std::isfinite(p.x) || !std::isfinite(p.y); }), pts.end());
      warn(f, base + "." + field, "точек с нечисловыми координатами: " + std::to_string(bad) + " — удалены");
      ch = true;
    }
    size_t outside = 0;
    for (Vec2& p : pts) {
      Vec2 c{clamp(p.x, 0.0, schema::kMapWidth), clamp(p.y, 0.0, schema::kMapHeight)};
      if (c != p) { p = c; outside++; }
    }
    if (outside) {
      warn(f, base + "." + field, "точек вне карты: " + std::to_string(outside) + " — перенесены на край");
      ch = true;
    }
    return ch;
  }

  // ---------------------------------------------------------------- шаги
  void meta() {
    Meta m = *w().meta;
    bool ch = fixNum(m.turn, 1, kMaxTurn, 1, F_WORLD, "meta", "turn");
    if (m.basemap.empty()) {
      m.basemap = Meta{}.basemap;
      warn(F_WORLD, "meta.basemap", "не задана базовая карта — взята «" + m.basemap + "»");
      ch = true;
    }
    if (ch) tx.meta() = std::move(m);

    Settings s = *w().settings;
    bool sc = false;
    if (!std::isfinite(s.fillOpacity) || s.fillOpacity < 0 || s.fillOpacity > 1) {
      float c = std::isfinite(s.fillOpacity) ? clamp(s.fillOpacity, 0.f, 1.f) : 0.5f;
      warn(F_WORLD, "settings.fillOpacity", "прозрачность заливки " + ns(s.fillOpacity) + " вне пределов 0…1 — взято " + ns(c));
      s.fillOpacity = c;
      sc = true;
    }
    sc |= fixNum(s.autosaveSec, kMinAutosave, kMaxAutosave, 60, F_WORLD, "settings", "autosaveSec");
    sc |= fixEnum(s.occupiedIncome, 3, OccupiedIncome::Owner, F_WORLD, "settings", "occupiedIncome");
    sc |= fixNum(s.figureSize, schema::kFigureSizeMin, schema::kFigureSizeMax, schema::kFigureSizeDefault, F_WORLD, "settings", "figureSize");
    if (sc) tx.settings() = s;
  }

  void catalogs() {
    static const char* const keys[7] = {"resources", "races", "cultures", "religions", "governments", "positions", "essences"};
    static const Seq seqs[7] = {Seq::Resource, Seq::Race, Seq::Culture, Seq::Religion, Seq::Government, Seq::Position, Seq::Essence};
    Catalogs c = *w().catalogs;
    std::vector<CatalogItem>* lists[7] = {&c.resources, &c.races, &c.cultures, &c.religions, &c.governments, &c.positions, &c.essences};
    bool ch = false;
    for (int i = 0; i < 7; i++) {
      auto& list = *lists[i];
      u32 maxId = 0;
      for (auto& it : list) maxId = std::max(maxId, it.id);
      std::unordered_set<Id> seen;
      for (auto& it : list) {
        if (it.id == 0 || seen.count(it.id)) {
          Id old = it.id;
          it.id = fresh(seqs[i], maxId);
          std::string where = std::string(keys[i]) + "." + refStr(seqs[i], it.id);
          warn(F_CATALOGS, where, old ? "повторный ID " + refStr(seqs[i], old) + " — назначен новый" : std::string("запись без ID — назначен новый"));
          ch = true;
        }
        seen.insert(it.id);
      }
    }
    // Встроенный ресурс «Золото» (казна) — всегда rs1.
    auto gold = std::find_if(c.resources.begin(), c.resources.end(), [](const CatalogItem& it) { return it.id == kGold; });
    if (gold == c.resources.end()) {
      CatalogItem g;
      g.id = kGold;
      g.name = "Золото";
      g.color = Color::hex(0xe2b33c);
      g.icon = "coins";
      g.builtin = true;
      c.resources.insert(c.resources.begin(), g);
      warn(F_CATALOGS, "resources", "нет встроенного ресурса «Золото» (rs1, казна) — добавлен");
      ch = true;
    } else if (!gold->builtin) {
      gold->builtin = true;
      warn(F_CATALOGS, "resources.rs1", "rs1 — встроенный ресурс «Золото» (казна): отмечен как встроенный");
      ch = true;
    }
    // Ключи встроенных ресурсов (трупы, демоническая энергия, запчасти механизмов): известные, без повторов; «gold» —
    // только у rs1. Прежний ключ «provisions» остаётся до переноса в группу «Провизия» (core/content.cpp).
    {
      std::unordered_set<std::string> seenKey;
      for (auto& it : c.resources) {
        if (it.key.empty()) continue;
        const std::string where = "resources." + refStr(Seq::Resource, it.id) + ".key";
        bool known = it.key == schema::kResLegacyProvisions && w().meta->content < content::kVersion;
        for (const auto& b : schema::kBuiltinResources) known = known || it.key == b.key;
        if (!known || (it.key == schema::kResGold && it.id != kGold)) {
          warn(F_CATALOGS, where, "неизвестный ключ встроенного ресурса «" + it.key + "» — снят");
          it.key.clear();
          ch = true;
        } else if (!seenKey.insert(it.key).second) {
          warn(F_CATALOGS, where, "ключ «" + it.key + "» уже у другого ресурса — снят");
          it.key.clear();
          ch = true;
        }
      }
      // Ресурс с названием встроенного без ключа становится встроенным (запасы и производство сохраняются).
      for (const auto& b : schema::kBuiltinResources) {
        if (b.key == std::string_view(schema::kResGold) || seenKey.count(b.key)) continue;
        auto hit = std::find_if(c.resources.begin(), c.resources.end(), [&](const CatalogItem& it) {
          return it.key.empty() && it.id != kGold && utf8::searchKey(it.name) == utf8::searchKey(b.name);
        });
        if (hit == c.resources.end()) continue;
        warn(F_CATALOGS, "resources." + refStr(Seq::Resource, hit->id), "ресурс «" + hit->name + "» отмечен как встроенный");
        hit->key = b.key;
        hit->builtin = true;
        seenKey.insert(b.key);
        ch = true;
      }
      for (auto& it : c.resources)
        if (!it.modifiers.empty() || !it.vacantModifiers.empty()) {
          it.modifiers.clear();
          it.vacantModifiers.clear();
          ch = true;
        }
    }
    // Должности: модификаторы занятой и пустующей должности — существующие, без повторов.
    for (auto& it : c.positions) {
      const std::string where = "positions." + refStr(Seq::Position, it.id);
      ch |= fixRefList(it.modifiers, Seq::Modifier, F_CATALOGS, where, "modifiers");
      ch |= fixRefList(it.vacantModifiers, Seq::Modifier, F_CATALOGS, where, "vacantModifiers");
    }
    // Группа бывает только у ресурса.
    for (int i = 1; i < 7; i++)
      for (auto& it : *lists[i])
        if (it.group) {
          it.group = 0;
          ch = true;
        }
    ch |= resGroups(c);
    ch |= relics(c);
    ch |= specials(c);
    if (ch) tx.catalogs() = std::move(c);
    catalogSets();
  }

  // Наборы ID справочников для проверки ссылок (после правки справочников и после базовых записей).
  void catalogSets() {
    const Catalogs& c = *w().catalogs;
    const std::vector<CatalogItem>* lists[7] = {&c.resources, &c.races, &c.cultures, &c.religions, &c.governments, &c.positions, &c.essences};
    for (int i = 0; i < 7; i++) {
      cat[i].clear();
      for (auto& it : *lists[i]) cat[i].insert(it.id);
    }
    relicIds.clear();
    specialIds.clear();
    groupIds.clear();
    for (auto& r : c.relics) relicIds.insert(r.id);
    for (auto& s : c.specials) specialIds.insert(s.id);
    for (auto& g : c.resGroups) groupIds.insert(g.id);
  }

  // Группы ресурсов: ID без повторов, родитель — существующая группа без циклов, ключи базовых групп — известные и
  // без повторов; группа ресурса — существующая.
  bool resGroups(Catalogs& c) {
    bool ch = false;
    u32 maxId = 0;
    for (auto& g : c.resGroups) maxId = std::max(maxId, g.id);
    std::unordered_set<Id> seen;
    for (auto& g : c.resGroups) {
      if (g.id == 0 || seen.count(g.id)) {
        Id old = g.id;
        g.id = fresh(Seq::ResGroup, maxId);
        warn(F_CATALOGS, "resGroups." + refStr(Seq::ResGroup, g.id), old ? "повторный ID " + refStr(Seq::ResGroup, old) + " — назначен новый"
                                                                          : std::string("группа без ID — назначен новый"));
        ch = true;
      }
      seen.insert(g.id);
    }
    std::unordered_set<std::string> keys;
    for (auto& g : c.resGroups) {
      const std::string where = "resGroups." + refStr(Seq::ResGroup, g.id);
      if (g.parent && (!seen.count(g.parent) || g.parent == g.id)) {
        warn(F_CATALOGS, where + ".parent", "нет группы " + refStr(Seq::ResGroup, g.parent) + " — группа стала группой верхнего уровня");
        g.parent = 0;
        ch = true;
      }
      if (!g.key.empty()) {
        bool known = false;
        for (const content::BaseGroup& b : content::baseGroups()) known = known || g.key == b.key;
        if (!known || !keys.insert(g.key).second) {
          warn(F_CATALOGS, where + ".key", (known ? "ключ «" + g.key + "» уже у другой группы" : "неизвестный ключ группы «" + g.key + "»") + std::string(" — снят"));
          g.key.clear();
          ch = true;
        }
      }
    }
    // Циклы родителей: разрываются у первой группы цикла.
    for (auto& g : c.resGroups) {
      Id p = g.parent;
      for (int guard = 0; p && guard < 256; guard++) {
        if (p == g.id) {
          warn(F_CATALOGS, "resGroups." + refStr(Seq::ResGroup, g.id) + ".parent", "группы замыкаются в цикл — группа стала группой верхнего уровня");
          g.parent = 0;
          ch = true;
          break;
        }
        const ResGroup* x = c.group(p);
        p = x ? x->parent : 0;
      }
    }
    for (auto& r : c.resources)
      if (r.group && !seen.count(r.group)) {
        warn(F_CATALOGS, "resources." + refStr(Seq::Resource, r.id) + ".group", "нет группы " + refStr(Seq::ResGroup, r.group) + " — ресурс без группы");
        r.group = 0;
        ch = true;
      }
    return ch;
  }

  bool relics(Catalogs& c) {
    bool ch = false;
    u32 maxId = 0;
    for (auto& r : c.relics) maxId = std::max(maxId, r.id);
    std::unordered_set<Id> seen;
    for (auto& r : c.relics) {
      if (r.id == 0 || seen.count(r.id)) {
        Id old = r.id;
        r.id = fresh(Seq::Relic, maxId);
        warn(F_CATALOGS, "relics." + refStr(Seq::Relic, r.id), old ? "повторный ID " + refStr(Seq::Relic, old) + " — назначен новый"
                                                                    : std::string("реликвия без ID — назначен новый"));
        ch = true;
      }
      seen.insert(r.id);
      ch |= fixEnum(r.rarity, int(Rarity::Count), Rarity::Common, F_CATALOGS, "relics." + refStr(Seq::Relic, r.id), "rarity");
    }
    return ch;
  }

  // Карта «ссылка → количество ≥ 0»: только существующие записи справочника s.
  bool fixAmounts(std::map<Id, double>& m, Seq s, const std::unordered_set<Id>& ids, FileId f, const std::string& base, const char* field) {
    bool ch = false;
    for (auto it = m.begin(); it != m.end();) {
      if (!ids.count(it->first)) {
        warn(f, base + "." + field, std::string("нет ") + genitive(s) + " " + refStr(s, it->first) + " — позиция удалена");
        it = m.erase(it);
        ch = true;
        continue;
      }
      ch |= fixNum(it->second, 0.0, 1e12, 0.0, f, base + "." + field, refStr(s, it->first).c_str());
      ++it;
    }
    return ch;
  }

  // Ключевой ресурс отряда (ТЗ «Ввод новых механик», п.2): ресурс из группы типа (или «Запчасти механизмов»).
  static bool keyAllowed(const Catalogs& c, UnitType t, Id res) {
    const schema::KeyRule k = schema::keyRule(t);
    if (!res || !Catalogs::find(c.resources, res)) return false;
    if (k.resKey) return c.resourceId(k.resKey) == res;
    if (!k.group) return false;
    const Id g = c.groupId(k.group);
    if (!g || !c.resourceIn(res, g)) return false;
    if (k.exclude)
      if (Id x = c.groupId(k.exclude); x && c.resourceIn(res, x)) return false;
    return true;
  }

  // Цена юнита: ключевой ресурс по типу, дополнительные ресурсы и эссенции ≥ 0, содержание эссенциями — у элементалей.
  template <class Row>
  bool fixUnitCost(Row& r, const Catalogs& c, const std::unordered_set<Id>& resIds, FileId f, const std::string& at) {
    bool ch = false;
    const schema::KeyRule k = schema::keyRule(r.type);
    if (r.keyRes && !keyAllowed(c, r.type, r.keyRes)) {
      warn(f, at + ".keyRes", refStr(Seq::Resource, r.keyRes) + " не подходит ключевым ресурсом для типа «" + schema::unitType(r.type).name + "» — снят");
      r.keyRes = 0;
      ch = true;
    }
    if (!std::isfinite(r.keyPer) || r.keyPer < 1 || r.keyPer > 1e12 || (k.fixedOne && r.keyPer != 1)) {
      r.keyPer = k.fixedOne || !std::isfinite(r.keyPer) ? 1.0 : clamp(r.keyPer, 1.0, 1e12);
      ch = true;
    }
    ch |= fixAmounts(r.extra, Seq::Resource, resIds, f, at, "extra");
    ch |= fixAmounts(r.essence, Seq::Essence, cat[6], f, at, "essence");
    ch |= fixAmounts(r.essUpkeep, Seq::Essence, cat[6], f, at, "essUpkeep");
    if (schema::isElemental(r.type)) {
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

  bool specials(Catalogs& c) {
    bool ch = false;
    u32 maxId = 0;
    for (auto& s : c.specials) maxId = std::max(maxId, s.id);
    std::unordered_set<Id> seen, resIds, essIds;
    for (auto& r : c.resources) resIds.insert(r.id);
    for (auto& e : c.essences) essIds.insert(e.id);
    cat[6] = essIds;
    for (auto& s : c.specials) {
      if (s.id == 0 || seen.count(s.id)) {
        Id old = s.id;
        s.id = fresh(Seq::Special, maxId);
        warn(F_CATALOGS, "specials." + refStr(Seq::Special, s.id), old ? "повторный ID " + refStr(Seq::Special, old) + " — назначен новый"
                                                                        : std::string("особый отряд без ID — назначен новый"));
        ch = true;
      }
      seen.insert(s.id);
      const std::string where = "specials." + refStr(Seq::Special, s.id);
      ch |= fixEnum(s.type, int(UnitType::Count), UnitType::Monsters, F_CATALOGS, where, "type");
      ch |= fixNum(s.upkeep, 0.0, 1e12, 0.0, F_CATALOGS, where, "upkeep");
      ch |= fixUnitCost(s, c, resIds, F_CATALOGS, where);
    }
    return ch;
  }

  // Запасы «ссылка → количество» любого знака: только существующие записи, конечные числа.
  bool fixAmountsSigned(std::map<Id, double>& m, Seq s, const std::unordered_set<Id>& ids, FileId f, const std::string& base, const char* field) {
    bool ch = false;
    for (auto it = m.begin(); it != m.end();) {
      if (!ids.count(it->first)) {
        warn(f, base + "." + field, std::string("нет ") + genitive(s) + " " + refStr(s, it->first) + " — запас удалён");
        it = m.erase(it);
        ch = true;
        continue;
      }
      ch |= fixFinite(it->second, f, base + "." + field, refStr(s, it->first).c_str());
      ++it;
    }
    return ch;
  }

  // Базовые записи справочников (core/content.h): мир прежней версии дополняется один раз.
  void content() {
    std::vector<std::string> notes = content::seed(tx);
    for (int i = 0; i < kSeqCount; i++) seq[size_t(i)] = std::max(seq[size_t(i)], w().meta->seq[size_t(i)]);
    if (notes.empty()) return;
    warn(F_CATALOGS, "", "мир дополнен: " + join(notes, "; "));
    catalogSets();
  }

  // Глобальные константы: ключи непусты и уникальны; встроенные — своего типа; числа конечны; ресурсы — из справочника.
  void constants() {
    Constants c = *w().constants;
    bool ch = false;
    std::unordered_set<std::string> keys;
    int userN = 0;
    for (auto& k : c.list)
      if (startsWith(k.key, "user")) userN = std::max(userN, int(parseNum(k.key.substr(4)).value_or(0)));
    for (size_t i = 0; i < c.list.size(); i++) {
      Constant& k = c.list[i];
      const std::string where = "constants[" + std::to_string(i) + "]";
      if (k.key.empty() || keys.count(k.key)) {
        std::string old = k.key;
        k.key = "user" + std::to_string(++userN);
        warn(F_CONSTANTS, where, old.empty() ? "константа без ключа — назначен «" + k.key + "»" : "повторный ключ «" + old + "» — назначен «" + k.key + "»");
        ch = true;
      }
      keys.insert(k.key);
      const Constant* b = nullptr;
      for (const Constant& x : schema::builtinConstants())
        if (x.key == k.key) b = &x;
      if (b) {
        if (!k.builtin) { k.builtin = true; ch = true; }
        if (k.type != b->type) {
          warn(F_CONSTANTS, where + ".type", "встроенная константа «" + b->name + "» — тип «" + schema::kConstTypes[int(b->type)].name + "»");
          Constant fixed = *b;
          fixed.name = k.name.empty() ? b->name : k.name;
          k = fixed;
          ch = true;
        }
      } else if (k.builtin) {
        warn(F_CONSTANTS, where + ".builtin", "«" + k.key + "» — не встроенная константа: признак снят");
        k.builtin = false;
        ch = true;
      }
      if (int(k.type) < 0 || int(k.type) >= int(ConstType::Count)) {
        warn(F_CONSTANTS, where + ".type", "недопустимый тип — взято «число»");
        k.type = ConstType::Number;
        ch = true;
      }
      ch |= fixNum(k.num, -1e12, 1e12, 0.0, F_CONSTANTS, where, "num");
      for (auto it = k.res.begin(); it != k.res.end();) {
        if (!hasCat(Seq::Resource, it->first)) {
          warn(F_CONSTANTS, where + ".res", "нет ресурса " + refStr(Seq::Resource, it->first) + " — позиция удалена");
          it = k.res.erase(it);
          ch = true;
        } else {
          ch |= fixNum(it->second, 0.0, 1e12, 0.0, F_CONSTANTS, where + ".res", refStr(Seq::Resource, it->first).c_str());
          ++it;
        }
      }
    }
    if (ch) tx.constants() = std::move(c);
  }

  void nodes() {
    const World base = w();
    base.nodes.each([&](const Node& n) {
      if (!std::isfinite(n.p.x) || !std::isfinite(n.p.y)) {
        warn(F_GEO, "nodes." + std::to_string(n.id), "координаты узла не являются числами — узел удалён");
        tx.eraseNode(n.id);
        return;
      }
      Vec2 p = n.p;
      if (fixPoint(p, true, F_GEO, "nodes." + std::to_string(n.id), "p")) tx.node(n.id).p = p;
    });
  }

  void edges() {
    const World base = w();
    base.edges.each([&](const Edge& e0) {
      std::string where = "edges." + std::to_string(e0.id);
      if (!exists(Seq::Node, e0.a) || !exists(Seq::Node, e0.b)) {
        warn(F_GEO, where, "дуга ссылается на несуществующий узел " + std::to_string(exists(Seq::Node, e0.a) ? e0.b : e0.a) + " — дуга удалена");
        tx.eraseEdge(e0.id);
        return;
      }
      Edge e = e0;
      bool ch = fixPoints(e.pts, F_GEO, where, "pts");
      if (e.a == e.b && e.pts.size() < 2) {
        warn(F_GEO, where, "замкнутая дуга без промежуточных точек — дуга удалена");
        tx.eraseEdge(e.id);
        return;
      }
      ch |= fixEnum(e.kind, 3, EdgeKind::Border, F_GEO, where, "kind");
      ch |= fixEnum(e.tl, 3, Terrain::Land, F_GEO, where, "tl");
      ch |= fixEnum(e.tr, 3, Terrain::Land, F_GEO, where, "tr");
      ch |= fixRef(e.pl, Seq::Province, F_GEO, where, "pl");
      ch |= fixRef(e.pr, Seq::Province, F_GEO, where, "pr");
      if (ch) tx.edge(e.id) = std::move(e);
    });
  }

  // Знаки и фигуры карты: пределы масштаба и ширины, точки на карте, достаточное число вершин.
  void mapObjects() {
    const World base = w();
    base.symbols.each([&](const MapSymbol& s0) {
      const std::string where = "symbols." + std::to_string(s0.id);
      MapSymbol s = s0;
      bool ch = fixEnum(s.kind, int(SymbolKind::Count), SymbolKind::Mountain, F_MAP, where, "kind");
      ch |= fixPoint(s.p, true, F_MAP, where, "p");
      ch |= fixNum(s.s, kMinSymbolScale, kMaxSymbolScale, 1.f, F_MAP, where, "s");
      const u8 vmax = s.kind == SymbolKind::Mountain || s.kind == SymbolKind::Peak ? 1 : 0;
      ch |= fixNum(s.v, u8(0), vmax, u8(0), F_MAP, where, "v");
      ch |= fixFinite(s.z, F_MAP, where, "z");
      if (ch) tx.symbol(s.id) = s;
    });
    base.shapes.each([&](const MapShape& s0) {
      const std::string where = "shapes." + std::to_string(s0.id);
      MapShape s = s0;
      bool ch = fixEnum(s.kind, int(ShapeKind::Count), ShapeKind::Water, F_MAP, where, "kind");
      ch |= fixPoints(s.pts, F_MAP, where, "pts");
      const size_t need = s.closed() ? 3 : 2;
      if (s.pts.size() < need) {
        warn(F_MAP, where, std::string(s.closed() ? "контур" : "линия") + " короче " + std::to_string(need) + " точек — фигура удалена");
        tx.eraseShape(s.id);
        return;
      }
      if (!s.holes.empty() && s.kind != ShapeKind::Water) {
        warn(F_MAP, where + ".holes", "острова бывают только у воды — удалены");
        s.holes.clear();
        ch = true;
      }
      for (size_t i = 0; i < s.holes.size();) {
        const std::string hf = "holes[" + std::to_string(i) + "]", hw = where + "." + hf;
        ch |= fixPoints(s.holes[i], F_MAP, where, hf.c_str());
        if (s.holes[i].size() < 3) {
          warn(F_MAP, hw, "контур острова короче 3 точек — удалён");
          s.holes.erase(s.holes.begin() + long(i));
          ch = true;
        } else {
          i++;
        }
      }
      if (s.closed()) {
        if (s.w != 0 || s.dash != 0) {
          s.w = s.dash = 0;
          ch = true;
        }
      } else {
        const float def = s.kind == ShapeKind::River ? schema::kRiverWidth : schema::kWallWidth;
        if (!(s.w > 0)) {
          if (s.w != 0 || std::isnan(s.w)) warn(F_MAP, where + ".w", "ширина " + ns(s.w) + " — взято " + ns(def));
          s.w = def;
          ch = true;
        }
        ch |= fixNum(s.w, kMinShapeWidth, kMaxShapeWidth, def, F_MAP, where, "w");
        if (s.kind == ShapeKind::River && s.dash != 0) {
          s.dash = 0;
          ch = true;
        }
        ch |= fixNum(s.dash, 0.f, kMaxShapeWidth, 0.f, F_MAP, where, "dash");
      }
      if (ch) tx.shape(s.id) = std::move(s);
    });
    if (!w().meta->mapObjects && (!w().symbols.empty() || !w().shapes.empty())) {
      warn(F_WORLD, "meta.mapObjects", "в data/map.json есть объекты карты — мир показывает их (mapObjects = true)");
      tx.meta().mapObjects = true;
    }
  }

  void factions() {
    const World base = w();
    u32 maxRow = 0, maxSeat = 0;
    base.factions.each([&](const Faction& f) {
      for (auto& r : f.army) maxRow = std::max(maxRow, r.id);
      for (auto& r : f.fleet) maxRow = std::max(maxRow, r.id);
      for (auto& s : f.council) maxSeat = std::max(maxSeat, s.id);
    });
    std::unordered_set<Id> usedRows, usedSeats;
    base.factions.each([&](const Faction& f0) {
      Faction f = f0;
      const std::string where = refStr(Seq::Faction, f.id);
      bool ch = fixEnum(f.kind, 2, FactionKind::State, F_FACTIONS, where, "kind");

      // Строки войск и флота: ID уникальны во всём мире (общая последовательность u).
      auto rows = [&](auto& list, bool fleet, const char* field, int types) {
        std::unordered_set<Id> local;
        for (size_t i = 0; i < list.size(); i++) {
          auto& r = list[i];
          std::string at = where + "." + field + "[" + std::to_string(i) + "]";
          Id old = r.id;
          if (old == 0) {
            r.id = fresh(Seq::Row, maxRow);
            warn(F_FACTIONS, at, "строка без ID — назначен " + refStr(Seq::Row, r.id));
            ch = true;
          } else if (local.count(old)) {
            r.id = fresh(Seq::Row, maxRow);
            warn(F_FACTIONS, at, "повторный ID " + refStr(Seq::Row, old) + " в списке — назначен " + refStr(Seq::Row, r.id) + " (ссылки остаются на первую строку)");
            ch = true;
          } else if (usedRows.count(old)) {
            r.id = fresh(Seq::Row, maxRow);
            rowRemap[{f.id, fleet, old}] = r.id;
            warn(F_FACTIONS, at, "ID " + refStr(Seq::Row, old) + " уже занят другой строкой — назначен " + refStr(Seq::Row, r.id) + ", ссылки обновлены");
            ch = true;
          }
          local.insert(r.id);
          usedRows.insert(r.id);
          if (int(r.type) < 0 || int(r.type) >= types) {
            warn(F_FACTIONS, at + ".type", "недопустимый тип — взят по умолчанию");
            r.type = decltype(r.type)(fleet ? int(ShipType::Frigate) : 0);
            ch = true;
          }
          ch |= fixNum(r.total, i64(0), kMaxCount, i64(0), F_FACTIONS, at, "total");
          ch |= fixNum(r.upkeep, 0.0, 1e12, 0.0, F_FACTIONS, at, "upkeep");
        }
      };
      rows(f.army, false, "army", int(UnitType::Count));
      rows(f.fleet, true, "fleet", int(ShipType::Count));
      // Цена найма строк армии; строка особого отряда повторяет запись справочника.
      for (size_t i = 0; i < f.army.size(); i++) {
        ArmyRow& r = f.army[i];
        const std::string at = where + ".army[" + std::to_string(i) + "]";
        if (r.special && !specialIds.count(r.special)) {
          warn(F_FACTIONS, at + ".special", "нет особого отряда " + refStr(Seq::Special, r.special) + " — строка стала обычной");
          r.special = 0;
          ch = true;
        }
        if (const SpecialUnit* s = r.special ? w().catalogs->special(r.special) : nullptr) {
          if (r.type != s->type || r.race != s->race || r.keyRes != s->keyRes || r.keyPer != s->keyPer || r.extra != s->extra ||
              r.essence != s->essence || r.upkeep != s->upkeep || r.essUpkeep != s->essUpkeep) {
            r.type = s->type;
            r.race = s->race;
            r.keyRes = s->keyRes;
            r.keyPer = s->keyPer;
            r.extra = s->extra;
            r.essence = s->essence;
            r.upkeep = s->upkeep;
            r.essUpkeep = s->essUpkeep;
            ch = true;
          }
          continue;
        }
        ch |= fixUnitCost(r, *w().catalogs, cat[0], F_FACTIONS, at);
      }
      // Эссенции, недостача провизии, изучение общих технологий.
      ch |= fixAmountsSigned(f.ess, Seq::Essence, cat[6], F_FACTIONS, where, "ess");
      ch |= fixNum(f.provisionDebt, 0.0, 1e15, 0.0, F_FACTIONS, where, "provisionDebt");
      for (auto it = f.techs.begin(); it != f.techs.end();) {
        const Tech* t = base.tech(it->first);
        if (!t || t->faction != 0) {
          warn(F_FACTIONS, where + ".techs", (t ? refStr(Seq::Tech, it->first) + " — не общая технология" : "нет технологии " + refStr(Seq::Tech, it->first)) +
                                                std::string(" — запись удалена"));
          it = f.techs.erase(it);
          ch = true;
          continue;
        }
        TechProgress& s = it->second;
        ch |= fixNum(s.progress, 0, kMaxTurns, 0, F_FACTIONS, where + ".techs." + refStr(Seq::Tech, it->first), "progress");
        if (s.studied && s.research) {
          s.research = false;
          ch = true;
        }
        if (s == TechProgress{}) {
          it = f.techs.erase(it);
          ch = true;
          continue;
        }
        ++it;
      }

      for (size_t i = 0; i < f.council.size(); i++) {
        auto& s = f.council[i];
        std::string at = where + ".council[" + std::to_string(i) + "]";
        if (s.id == 0 || usedSeats.count(s.id)) {
          Id old = s.id;
          s.id = fresh(Seq::Council, maxSeat);
          warn(F_FACTIONS, at, old ? "повторный ID " + refStr(Seq::Council, old) + " — назначен " + refStr(Seq::Council, s.id)
                                   : "место без ID — назначен " + refStr(Seq::Council, s.id));
          ch = true;
        }
        usedSeats.insert(s.id);
        ch |= fixRef(s.character, Seq::Character, F_FACTIONS, at, "character");
      }

      if (f.flag.pattern >= FlagPattern::Count) {
        warn(F_FACTIONS, where + ".flag.pattern", "недопустимый узор — взят однотонный");
        f.flag.pattern = FlagPattern::Solid;
        ch = true;
      }
      if (!f.flag.png.empty() && !isPng(f.flag.png)) {
        warn(F_FACTIONS, where + ".flag.png", "данные флага не являются PNG — изображение удалено");
        f.flag.png.clear();
        f.flag.image = false;
        ch = true;
      }
      if (f.flag.image && f.flag.png.empty()) {
        warn(F_FACTIONS, where + ".flag.image", "флаг-изображение без PNG — включён конструктор");
        f.flag.image = false;
        ch = true;
      }

      ch |= fixRef(f.culture, Seq::Culture, F_FACTIONS, where, "culture");
      ch |= fixRef(f.government, Seq::Government, F_FACTIONS, where, "government");
      ch |= fixRef(f.religion, Seq::Religion, F_FACTIONS, where, "religion");
      ch |= fixRef(f.ruler, Seq::Character, F_FACTIONS, where, "ruler");
      ch |= fixRef(f.capital, Seq::Province, F_FACTIONS, where, "capital");

      for (auto it = f.res.begin(); it != f.res.end();) {
        if (!hasCat(Seq::Resource, it->first)) {
          warn(F_FACTIONS, where + ".res", "нет ресурса " + refStr(Seq::Resource, it->first) + " — запас удалён");
          it = f.res.erase(it);
          ch = true;
        } else {
          ch |= fixFinite(it->second, F_FACTIONS, where + ".res", refStr(Seq::Resource, it->first).c_str());
          ++it;
        }
      }
      ch |= fixRefList(f.modifiers, Seq::Modifier, F_FACTIONS, where, "modifiers");
      ch |= fixModTurns(f.modTurns, f.modifiers, F_FACTIONS, where);
      ch |= fixNum(f.tax, 0.0, 100.0, 10.0, F_FACTIONS, where, "tax");
      ch |= fixEnum(f.stateKind, int(StateKind::Count), StateKind::Living, F_FACTIONS, where, "stateKind");
      ch |= fixNum(f.pirateRisk, 0.0, 100.0, 0.0, F_FACTIONS, where, "pirateRisk");

      // Рабы: раса справочника, численность 0…kMaxCount, довольство −100…100, повторы рас складываются.
      if (!f.slaves.empty()) {
        std::vector<SlaveGroup> res;
        bool sch = false;
        for (size_t i = 0; i < f.slaves.size(); i++) {
          SlaveGroup s = f.slaves[i];
          std::string at = where + ".slaves[" + std::to_string(i) + "]";
          if (!hasCat(Seq::Race, s.race)) {
            warn(F_FACTIONS, at + ".race", "нет расы " + ref(Seq::Race, s.race) + " — рабы удалены");
            sch = true;
            continue;
          }
          sch |= fixNum(s.count, i64(0), kMaxCount, i64(0), F_FACTIONS, at, "count");
          sch |= fixNum(s.contentment, -100.0, 100.0, 0.0, F_FACTIONS, at, "contentment");
          auto same = std::find_if(res.begin(), res.end(), [&](const SlaveGroup& x) { return x.race == s.race; });
          if (same != res.end()) {
            warn(F_FACTIONS, at, "повтор расы " + refStr(Seq::Race, s.race) + " — рабы сложены");
            same->count = std::min(kMaxCount, same->count + s.count);
            sch = true;
            continue;
          }
          res.push_back(s);
        }
        if (sch) { f.slaves = std::move(res); ch = true; }
      }
      // Формирование отрядов и кораблей: строка фракции, число > 0, 1…kMaxTurns ходов, уплаченное — ресурсы справочника.
      if (!f.forming.empty()) {
        std::vector<Formation> res;
        bool fch = false;
        for (size_t i = 0; i < f.forming.size(); i++) {
          Formation q = f.forming[i];
          std::string at = where + ".forming[" + std::to_string(i) + "]";
          Id ra = remapRow(f.id, false, q.row), rf = remapRow(f.id, true, q.row);
          Id row = f.armyRow(ra) ? ra : rf;
          if (row != q.row) { q.row = row; fch = true; }
          if (!f.armyRow(q.row) && !f.fleetRow(q.row)) {
            warn(F_FACTIONS, at + ".row", "нет строки " + ref(Seq::Row, q.row) + " — формирование удалено");
            fch = true;
            continue;
          }
          if (q.count <= 0) {
            warn(F_FACTIONS, at + ".count", "численность " + std::to_string(q.count) + " — формирование удалено");
            fch = true;
            continue;
          }
          fch |= fixNum(q.count, i64(1), kMaxCount, i64(1), F_FACTIONS, at, "count");
          fch |= fixNum(q.left, 1, kMaxTurns, 1, F_FACTIONS, at, "left");
          fch |= fixNum(q.people, i64(0), kMaxCount, i64(0), F_FACTIONS, at, "people");
          fch |= fixAmounts(q.paidEss, Seq::Essence, cat[6], F_FACTIONS, at, "paidEss");
          for (auto it = q.paid.begin(); it != q.paid.end();) {
            if (!hasCat(Seq::Resource, it->first)) {
              warn(F_FACTIONS, at + ".paid", "нет ресурса " + refStr(Seq::Resource, it->first) + " — позиция удалена");
              it = q.paid.erase(it);
              fch = true;
            } else {
              fch |= fixNum(it->second, 0.0, 1e12, 0.0, F_FACTIONS, at + ".paid", refStr(Seq::Resource, it->first).c_str());
              ++it;
            }
          }
          res.push_back(std::move(q));
        }
        if (fch) { f.forming = std::move(res); ch = true; }
      }
      // Корабли в торговле: строки флота фракции.
      ch |= fixRowCounts(f.tradeFleet, F_FACTIONS, where, "tradeFleet", [&](Id row) { return f.fleetRow(row) != nullptr; },
                         [&](Id row) { return remapRow(f.id, true, row); }, "нет строки флота");

      if (f.isState()) {
        if (f.homeState) {
          warn(F_FACTIONS, where + ".homeState", "у государства не бывает государства расположения — удалено");
          f.homeState = 0;
          ch = true;
        }
        if (f.stateGuild) {
          warn(F_FACTIONS, where + ".stateGuild", "признак государственной гильдии у государства — снят");
          f.stateGuild = false;
          ch = true;
        }
        // Сюзерен и мятежное происхождение — другое существующее государство.
        for (auto [field, ptr] : {std::pair{"suzerain", &f.suzerain}, std::pair{"rebelOf", &f.rebelOf}}) {
          Id& v = *ptr;
          if (!v) continue;
          if (v == f.id || !isState(v)) {
            warn(F_FACTIONS, where + "." + field, v == f.id ? std::string("ссылка на само государство — удалена")
                                                  : exists(Seq::Faction, v) ? refStr(Seq::Faction, v) + " — не государство: ссылка удалена"
                                                                            : "нет фракции " + refStr(Seq::Faction, v) + " — ссылка удалена");
            v = 0;
            ch = true;
          }
        }
      } else {
        if (f.homeState && !isState(f.homeState)) {
          warn(F_FACTIONS, where + ".homeState", exists(Seq::Faction, f.homeState) ? refStr(Seq::Faction, f.homeState) + " — не государство: ссылка удалена"
                                                                                 : "нет фракции " + refStr(Seq::Faction, f.homeState) + " — ссылка удалена");
          f.homeState = 0;
          ch = true;
        }
        // У гильдии нет вида государства, вассалитета и признака основного государства.
        if (f.mainState || f.suzerain || f.rebelOf || f.stateKind != StateKind::Living || !f.slaves.empty()) {
          warn(F_FACTIONS, where, "у гильдии не бывает вида государства, сюзерена, рабов и признака основного государства — сняты");
          f.mainState = false;
          f.suzerain = f.rebelOf = 0;
          f.stateKind = StateKind::Living;
          f.slaves.clear();
          ch = true;
        }
      }
      if (ch) tx.faction(f.id) = std::move(f);
    });
    // Вассалитет без циклов: сюзерен не может быть (прямо или через цепочку) вассалом своего вассала.
    const World cur = w();
    cur.factions.each([&](const Faction& f) {
      Id s = f.suzerain;
      for (int guard = 0; s && guard < 64; guard++) {
        if (s == f.id) {
          warn(F_FACTIONS, refStr(Seq::Faction, f.id) + ".suzerain", "цепочка вассалитета замыкается на само государство — сюзерен снят");
          tx.faction(f.id).suzerain = 0;
          break;
        }
        const Faction* x = tx.w().faction(s);
        s = x ? x->suzerain : 0;
      }
    });
  }

  void characters() {
    const World base = w();
    base.characters.each([&](const Character& c0) {
      Character c = c0;
      const std::string where = refStr(Seq::Character, c.id);
      bool ch = fixRef(c.faction, Seq::Faction, F_CHARACTERS, where, "faction");
      ch |= fixNum(c.upkeep, 0.0, 1e12, 0.0, F_CHARACTERS, where, "upkeep");
      if (!c.portrait.empty() && !isPng(c.portrait) && !isJpeg(c.portrait)) {
        warn(F_CHARACTERS, where + ".portrait", "портрет не является PNG или JPEG — удалён");
        c.portrait.clear();
        ch = true;
      }
      ch |= fixRefList(c.modifiers, Seq::Modifier, F_CHARACTERS, where, "modifiers");
      ch |= fixModTurns(c.modTurns, c.modifiers, F_CHARACTERS, where);
      if (c.captor && (!isState(c.captor) || c.captor == c.faction)) {
        warn(F_CHARACTERS, where + ".captor", c.captor == c.faction ? std::string("пленён собственным государством — ссылка удалена")
                                              : exists(Seq::Faction, c.captor) ? refStr(Seq::Faction, c.captor) + " — не государство: ссылка удалена"
                                                                               : "нет фракции " + refStr(Seq::Faction, c.captor) + " — ссылка удалена");
        c.captor = 0;
        ch = true;
      }
      ch |= fixRef(c.burial, Seq::Province, F_CHARACTERS, where, "burial");
      // Инвентарь: реликвии справочника; каждая реликвия — только у одного персонажа (первого по ID).
      ch |= fixRefList(c.inventory, Seq::Relic, F_CHARACTERS, where, "inventory", [&](Id id) { return relicIds.count(id) && !relicOwner.count(id); },
                       "реликвия уже у другого персонажа");
      for (Id r : c.inventory) relicOwner[r] = c.id;
      if (ch) tx.character(c.id) = std::move(c);
    });
  }
  std::unordered_map<Id, Id> relicOwner;

  void modifiers() {
    const World base = w();
    base.modifiers.each([&](const Modifier& m0) {
      Modifier m = m0;
      const std::string where = refStr(Seq::Modifier, m.id);
      bool ch = false;
      u32 valid = (1u << kFxCount) - 1;
      if (m.fxMask & ~valid) {
        m.fxMask &= valid;
        ch = true;
      }
      for (int i = 0; i < kFxCount; i++) {
        const auto& e = schema::kEffects[i];
        double& v = m.fx[size_t(i)];
        if (!m.has(Fx(i))) {
          if (v != 0) { v = 0; ch = true; }
          continue;
        }
        if (!std::isfinite(v)) {
          warn(F_MODIFIERS, where + ".fx." + e.id, "значение эффекта не является числом — эффект удалён");
          m.fxMask &= ~(1u << i);
          v = 0;
          ch = true;
          continue;
        }
        ch |= fixNum(v, e.min, e.max, 0.0, F_MODIFIERS, where + ".fx", e.id);
      }
      ch |= fixRefList(m.targets, Seq::Faction, F_MODIFIERS, where, "targets");
      ch |= fixEnum(m.kind, int(ModKind::Count), ModKind::Any, F_MODIFIERS, where, "kind");
      ch |= fixNum(m.duration, 0, kMaxTurns, 0, F_MODIFIERS, where, "duration");
      // Ключ встроенного модификатора: известный и у одной записи.
      if (!m.key.empty()) {
        if (!schema::builtinModifier(m.key)) {
          warn(F_MODIFIERS, where + ".key", "неизвестный ключ встроенного модификатора «" + m.key + "» — снят");
          m.key.clear();
          ch = true;
        } else if (!modKeys.insert(m.key).second) {
          warn(F_MODIFIERS, where + ".key", "ключ «" + m.key + "» уже у другого модификатора — снят");
          m.key.clear();
          ch = true;
        }
      }
      if (ch) tx.modifier(m.id) = std::move(m);
    });
  }
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

  void buildings() {
    {
      const World base = w();
      base.buildings.each([&](const Building& b0) {
        Building b = b0;
        const std::string where = refStr(Seq::Building, b.id);
        bool ch = false;
        if (b.owner && !exists(Seq::Faction, b.owner)) {
          warn(F_BUILDINGS, where + ".owner", "нет фракции " + refStr(Seq::Faction, b.owner) + " — постройка стала общей");
          b.owner = 0;
          ch = true;
        } else if (b.owner && !isState(b.owner)) {
          // Уникальные постройки бывают только у государств: гильдия не владеет провинциями.
          warn(F_BUILDINGS, where + ".owner", refStr(Seq::Faction, b.owner) + " — гильдия, а уникальная постройка бывает только у государства: постройка стала общей");
          b.owner = 0;
          ch = true;
        }
        ch |= fixEnum(b.cat, int(BuildingCat::Count), BuildingCat::Economic, F_BUILDINGS, where, "cat");
        if (b.levels.empty()) {
          warn(F_BUILDINGS, where + ".levels", "у постройки нет уровней — добавлен один");
          b.levels.push_back(BuildingLevel{});
          ch = true;
        }
        for (size_t i = 0; i < b.levels.size(); i++) {
          auto& l = b.levels[i];
          std::string at = where + ".levels[" + std::to_string(i) + "]";
          ch |= fixNum(l.turns, 1, kMaxTurns, 1, F_BUILDINGS, at, "turns");
          for (auto it = l.cost.begin(); it != l.cost.end();) {
            if (!hasCat(Seq::Resource, it->first)) {
              warn(F_BUILDINGS, at + ".cost", "нет ресурса " + refStr(Seq::Resource, it->first) + " — цена удалена");
              it = l.cost.erase(it);
              ch = true;
            } else {
              ch |= fixNum(it->second, 0.0, 1e12, 0.0, F_BUILDINGS, at + ".cost", refStr(Seq::Resource, it->first).c_str());
              ++it;
            }
          }
          ch |= fixRefList(l.modifiers, Seq::Modifier, F_BUILDINGS, at, "modifiers");
          for (auto it = l.produce.begin(); it != l.produce.end();) {
            if (!hasCat(Seq::Resource, it->first)) {
              warn(F_BUILDINGS, at + ".produce", "нет ресурса " + refStr(Seq::Resource, it->first) + " — производство удалено");
              it = l.produce.erase(it);
              ch = true;
            } else {
              ch |= fixNum(it->second, 0.0, 1e12, 0.0, F_BUILDINGS, at + ".produce", refStr(Seq::Resource, it->first).c_str());
              ++it;
            }
          }
          // Эссенции уровня — только у постройки генерации эссенции.
          if (!b.essenceGen && !l.essence.empty()) {
            warn(F_BUILDINGS, at + ".essence", "постройка не генерирует эссенцию — эссенции уровня сняты");
            l.essence.clear();
            ch = true;
          }
          ch |= fixAmounts(l.essence, Seq::Essence, cat[6], F_BUILDINGS, at, "essence");
        }
        ch |= fixPoint(b.pos, false, F_BUILDINGS, where, "pos");
        // Постройка преобразования: до трёх разных ресурсов на входе, ресурс на выходе, срок цикла 1…kMaxTurns.
        if (!b.convert) {
          if (!(b.recipe == Recipe{})) {
            b.recipe = Recipe{};
            ch = true;
          }
        } else {
          std::vector<ResAmount> in;
          for (const ResAmount& x : b.recipe.in) {
            if (!hasCat(Seq::Resource, x.res) || std::any_of(in.begin(), in.end(), [&](const ResAmount& y) { return y.res == x.res; })) {
              warn(F_BUILDINGS, where + ".recipe.in", (hasCat(Seq::Resource, x.res) ? "повтор ресурса " : "нет ресурса ") + ref(Seq::Resource, x.res) +
                                                         " — позиция удалена");
              ch = true;
              continue;
            }
            ResAmount y = x;
            ch |= fixNum(y.amount, 0.0, 1e12, 0.0, F_BUILDINGS, where + ".recipe.in", refStr(Seq::Resource, x.res).c_str());
            in.push_back(y);
          }
          if (in.size() > 3) {
            warn(F_BUILDINGS, where + ".recipe.in", "на входе больше трёх ресурсов — лишние удалены");
            in.resize(3);
            ch = true;
          }
          if (in != b.recipe.in) {
            b.recipe.in = std::move(in);
            ch = true;
          }
          ch |= fixRef(b.recipe.out.res, Seq::Resource, F_BUILDINGS, where + ".recipe", "out");
          ch |= fixNum(b.recipe.out.amount, 0.0, 1e12, 0.0, F_BUILDINGS, where + ".recipe.out", "amount");
          ch |= fixNum(b.recipe.turns, 1, kMaxTurns, 1, F_BUILDINGS, where + ".recipe", "turns");
        }
        // Доступ к особым отрядам.
        if (!b.specialAccess && !b.specials.empty()) {
          warn(F_BUILDINGS, where + ".specials", "постройка не даёт доступ к особым отрядам — список снят");
          b.specials.clear();
          ch = true;
        }
        ch |= fixRefList(b.specials, Seq::Special, F_BUILDINGS, where, "specials");
        // Технологии: общая постройка зависит только от общих технологий, уникальная — ещё и от технологий своего
        // государства (ТЗ «Доработки», п.4 и 7).
        ch |= fixRefList(b.techs, Seq::Tech, F_BUILDINGS, where, "techs",
                         [&](Id id) { const Tech* t = base.tech(id); return t && (t->faction == 0 || (b.owner && t->faction == b.owner)); },
                         "технология другого государства (общей постройке — только общие)");
        if (ch) tx.building(b.id) = std::move(b);
      });
    }
    // Требования: существующая постройка, не сама, без повторов, уровень в пределах.
    {
      const World base = w();
      base.buildings.each([&](const Building& b0) {
        Building b = b0;
        const std::string where = refStr(Seq::Building, b.id);
        bool ch = false;
        std::vector<BuildingReq> res;
        for (auto& r : b.requires_) {
          const Building* t = base.building(r.building);
          if (!t) {
            warn(F_BUILDINGS, where + ".requires", "нет постройки " + ref(Seq::Building, r.building) + " — требование удалено");
            ch = true;
            continue;
          }
          if (r.building == b.id) {
            warn(F_BUILDINGS, where + ".requires", "постройка требует саму себя — требование удалено");
            ch = true;
            continue;
          }
          // Общая постройка не зависит от уникальных; уникальная — от общих и своего государства (ТЗ «Доработки», п.7).
          if (t->owner && t->owner != b.owner) {
            warn(F_BUILDINGS, where + ".requires", b.owner ? "требование " + refStr(Seq::Building, r.building) + " — уникальная постройка другого государства: удалено"
                                                           : "общая постройка не может зависеть от уникальной " + refStr(Seq::Building, r.building) + " — требование удалено");
            ch = true;
            continue;
          }
          if (std::any_of(res.begin(), res.end(), [&](const BuildingReq& q) { return q.building == r.building; })) {
            warn(F_BUILDINGS, where + ".requires", "повтор требования " + refStr(Seq::Building, r.building) + " — удалён");
            ch = true;
            continue;
          }
          BuildingReq q = r;
          ch |= fixNum(q.level, 1, int(t->levels.size()), 1, F_BUILDINGS, where + ".requires." + refStr(Seq::Building, r.building), "level");
          res.push_back(q);
        }
        if (ch) {
          b.requires_ = std::move(res);
          tx.building(b.id) = std::move(b);
        }
      });
    }
    // Циклы требований.
    const World base = w();
    std::unordered_map<Id, std::vector<Id>> deps;
    base.buildings.each([&](const Building& b) {
      auto& d = deps[b.id];
      for (auto& r : b.requires_) d.push_back(r.building);
    });
    auto cut = cycleEdges(base.buildings.ids(), [&](Id id) -> const std::vector<Id>& { return deps[id]; });
    for (auto& [from, to] : cut) {
      warn(F_BUILDINGS, refStr(Seq::Building, from) + ".requires", "требование " + refStr(Seq::Building, to) + " замыкает цикл — удалено");
      auto& rq = tx.building(from).requires_;
      Id target = to;
      rq.erase(std::remove_if(rq.begin(), rq.end(), [&](const BuildingReq& q) { return q.building == target; }), rq.end());
    }
  }

  void techs() {
    {
      const World base = w();
      base.techs.each([&](const Tech& t) {
        if (t.faction && !exists(Seq::Faction, t.faction)) {   // фракция 0 — общее дерево
          warn(F_TECHS, refStr(Seq::Tech, t.id), "нет фракции " + refStr(Seq::Faction, t.faction) + " — технология удалена");
          tx.eraseTech(t.id);
        }
      });
    }
    {
      const World base = w();
      base.techs.each([&](const Tech& t0) {
        Tech t = t0;
        const std::string where = refStr(Seq::Tech, t.id);
        bool ch = fixNum(t.turns, 1, kMaxTurns, 1, F_TECHS, where, "turns");
        // Пройдено ходов исследования: с модификатором «Время исследования технологий» срок может быть больше turns.
        ch |= fixNum(t.progress, 0, kMaxTurns, 0, F_TECHS, where, "progress");
        if (t.studied && t.research) {
          warn(F_TECHS, where + ".research", "изученная технология не может исследоваться — исследование снято");
          t.research = false;
          ch = true;
        }
        // Общая технология изучается каждой фракцией отдельно (Faction::techs): своих отметок у неё нет.
        if (t.faction == 0 && (t.studied || t.research || t.progress)) {
          warn(F_TECHS, where, "общая технология изучается каждым государством отдельно — отметки изучения сняты");
          t.studied = t.research = false;
          t.progress = 0;
          ch = true;
        }
        // Уникальная технология зависит от своих и общих, общая — только от общих (ТЗ «Доработки», п.7).
        ch |= fixRefList(t.prereqs, Seq::Tech, F_TECHS, where, "prereqs",
                         [&](Id id) { const Tech* p = base.tech(id); return p && id != t.id && (p->faction == t.faction || (t.faction && p->faction == 0)); },
                         "технология другой фракции, уникальная у общей или она сама");
        ch |= fixRefList(t.modifiers, Seq::Modifier, F_TECHS, where, "modifiers");
        ch |= fixPoint(t.pos, false, F_TECHS, where, "pos");
        if (ch) tx.tech(t.id) = std::move(t);
      });
    }
    const World base = w();
    auto ids = base.techs.ids();
    auto cut = cycleEdges(ids, [&](Id id) -> const std::vector<Id>& { return base.tech(id)->prereqs; });
    for (auto& [from, to] : cut) {
      warn(F_TECHS, refStr(Seq::Tech, from) + ".prereqs", "условие " + refStr(Seq::Tech, to) + " замыкает цикл — удалено");
      auto& pr = tx.tech(from).prereqs;
      pr.erase(std::remove(pr.begin(), pr.end(), to), pr.end());
    }
    // ТЗ 1.b.v: изученная технология требует изученных предшествующих. Иначе изученность снимается (по цепочке —
    // и у зависящих от неё); пройденные ходы сохраняются.
    for (bool again = true; again;) {
      again = false;
      const World cur = w();
      auto studiedBy = [&](const Tech* pt, Id faction) {
        if (!pt) return true;
        if (pt->faction) return pt->studied;
        const Faction* f = cur.faction(faction);
        auto it = f ? f->techs.find(pt->id) : decltype(f->techs.end()){};
        return f && it != f->techs.end() && it->second.studied;
      };
      cur.techs.each([&](const Tech& t) {
        if (!t.studied || !t.faction) return;
        for (Id pid : t.prereqs) {
          if (studiedBy(cur.tech(pid), t.faction)) continue;
          warn(F_TECHS, refStr(Seq::Tech, t.id) + ".studied",
               "не изучено условие " + refStr(Seq::Tech, pid) + " — изученность снята");
          Tech& m = tx.tech(t.id);
          m.studied = false;
          m.research = false;
          m.progress = std::min(m.progress, std::max(0, m.turns - 1));
          again = true;
          return;
        }
      });
      // Общие технологии, изученные фракцией: их условия изучены этой же фракцией.
      cur.factions.each([&](const Faction& f) {
        for (auto& [tid, s] : f.techs) {
          if (!s.studied) continue;
          const Tech* t = cur.tech(tid);
          if (!t) continue;
          for (Id pid : t->prereqs) {
            if (studiedBy(cur.tech(pid), f.id)) continue;
            warn(F_FACTIONS, refStr(Seq::Faction, f.id) + ".techs." + refStr(Seq::Tech, tid),
                 "не изучено условие " + refStr(Seq::Tech, pid) + " — изученность снята");
            TechProgress& m = tx.faction(f.id).techs[tid];
            m.studied = false;
            m.research = false;
            m.progress = std::min(m.progress, std::max(0, t->turns - 1));
            again = true;
            return;
          }
        }
      });
    }
  }

  Id remapRow(Id faction, bool fleet, Id row) const {
    auto it = rowRemap.find({faction, fleet, row});
    return it == rowRemap.end() ? row : it->second;
  }

  void provinces() {
    const World base = w();
    base.provinces.each([&](const Province& p0) {
      Province p = p0;
      const std::string where = refStr(Seq::Province, p.id);
      bool ch = false;
      if (p.owner && !isState(p.owner)) {
        warn(F_PROVINCES, where + ".owner", exists(Seq::Faction, p.owner) ? refStr(Seq::Faction, p.owner) + " — гильдия, а владельцем может быть только государство: владелец удалён"
                                                                         : "нет фракции " + refStr(Seq::Faction, p.owner) + " — владелец удалён");
        p.owner = 0;
        ch = true;
      }
      ch |= fixRef(p.lord, Seq::Character, F_PROVINCES, where, "lord");
      ch |= fixEnum(p.size, 3, ProvSize::Medium, F_PROVINCES, where, "size");
      ch |= fixEnum(p.city, 4, CityType::Village, F_PROVINCES, where, "city");
      ch |= fixRef(p.resource, Seq::Resource, F_PROVINCES, where, "resource");
      ch |= fixNum(p.resourceAmount, 0.0, 1e12, 0.0, F_PROVINCES, where, "resourceAmount");

      // Гарнизон: строки армии владельца, без повторов (численности складываются).
      if (!p.garrison.empty()) {
        const Faction* owner = base.faction(p.owner);
        std::vector<GarrisonEntry> res;
        bool gch = false;
        for (size_t i = 0; i < p.garrison.size(); i++) {
          GarrisonEntry g = p.garrison[i];
          std::string at = where + ".garrison[" + std::to_string(i) + "]";
          Id row = owner ? remapRow(owner->id, false, g.row) : g.row;
          if (row != g.row) { g.row = row; gch = true; }
          if (!owner) {
            warn(F_PROVINCES, at, "у провинции нет владельца — гарнизон удалён");
            gch = true;
            continue;
          }
          if (!owner->armyRow(g.row)) {
            warn(F_PROVINCES, at + ".row", "у владельца " + refStr(Seq::Faction, owner->id) + " нет строки армии " + ref(Seq::Row, g.row) + " — запись удалена");
            gch = true;
            continue;
          }
          gch |= fixNum(g.count, i64(0), kMaxCount, i64(0), F_PROVINCES, at, "count");
          auto same = std::find_if(res.begin(), res.end(), [&](const GarrisonEntry& x) { return x.row == g.row; });
          if (same != res.end()) {
            warn(F_PROVINCES, at, "повтор строки " + refStr(Seq::Row, g.row) + " — численности сложены");
            same->count = std::min(kMaxCount, same->count + g.count);
            gch = true;
            continue;
          }
          res.push_back(g);
        }
        if (gch) { p.garrison = std::move(res); ch = true; }
      }
      // Герои в гарнизоне: доступные герои владельца, не больше чем в одном гарнизоне или войске.
      ch |= fixRefList(p.garrisonHeroes, Seq::Character, F_PROVINCES, where, "garrisonHeroes",
                       [&](Id id) {
                         const Character* c = base.character(id);
                         if (!c || !p.owner || c->faction != p.owner || heroUsed.count(id)) return false;
                         for (Id m : c->modifiers)
                           if (const Modifier* x = base.modifier(m); x && (x->key == schema::mod::Dead || x->key == schema::mod::Captive)) return false;
                         return true;
                       },
                       "не доступный герой владельца или уже в другом гарнизоне");
      for (Id h : p.garrisonHeroes) heroUsed.insert(h);
      ch |= fixNum(p.garrisonLoyalty, schema::kMinLoyalty, schema::kMaxLoyalty, schema::kMaxLoyalty, F_PROVINCES, where, "garrisonLoyalty");

      ch |= fixNum(p.contentment, -100.0, 100.0, 0.0, F_PROVINCES, where, "contentment");
      ch |= fixRef(p.culture, Seq::Culture, F_PROVINCES, where, "culture");
      ch |= fixRef(p.religion, Seq::Religion, F_PROVINCES, where, "religion");

      if (!p.races.empty()) {
        std::vector<RacePop> res;
        bool rch = false;
        for (size_t i = 0; i < p.races.size(); i++) {
          RacePop r = p.races[i];
          std::string at = where + ".races[" + std::to_string(i) + "]";
          if (!hasCat(Seq::Race, r.race)) {
            warn(F_PROVINCES, at + ".race", "нет расы " + ref(Seq::Race, r.race) + " — запись удалена");
            rch = true;
            continue;
          }
          rch |= fixNum(r.pop, i64(0), kMaxCount, i64(0), F_PROVINCES, at, "pop");
          auto same = std::find_if(res.begin(), res.end(), [&](const RacePop& x) { return x.race == r.race; });
          if (same != res.end()) {
            warn(F_PROVINCES, at, "повтор расы " + refStr(Seq::Race, r.race) + " — население сложено");
            same->pop = std::min(kMaxCount, same->pop + r.pop);
            rch = true;
            continue;
          }
          res.push_back(r);
        }
        if (rch) { p.races = std::move(res); ch = true; }
      }

      // Влияние гильдий: только гильдии, без повторов, 0…100, сумма ≤ 100.
      if (!p.influence.empty()) {
        std::vector<Influence> res;
        bool ich = false;
        for (size_t i = 0; i < p.influence.size(); i++) {
          Influence x = p.influence[i];
          std::string at = where + ".influence[" + std::to_string(i) + "]";
          if (!isGuild(x.guild)) {
            warn(F_PROVINCES, at + ".guild", exists(Seq::Faction, x.guild) ? refStr(Seq::Faction, x.guild) + " — не гильдия: запись удалена"
                                                                          : "нет гильдии " + ref(Seq::Faction, x.guild) + " — запись удалена");
            ich = true;
            continue;
          }
          ich |= fixNum(x.pct, 0.0, 100.0, 0.0, F_PROVINCES, at, "pct");
          auto same = std::find_if(res.begin(), res.end(), [&](const Influence& y) { return y.guild == x.guild; });
          if (same != res.end()) {
            warn(F_PROVINCES, at, "повтор гильдии " + refStr(Seq::Faction, x.guild) + " — влияние сложено");
            same->pct = std::min(100.0, same->pct + x.pct);
            ich = true;
            continue;
          }
          res.push_back(x);
        }
        double sum = 0;
        for (auto& x : res) sum += x.pct;
        if (sum > 100.0 + 1e-9) {
          double k = 100.0 / sum;
          for (auto& x : res) x.pct *= k;
          warn(F_PROVINCES, where + ".influence", "сумма влияния " + ns(sum) + " % больше 100 % — доли уменьшены пропорционально");
          ich = true;
        }
        if (ich) { p.influence = std::move(res); ch = true; }
      }

      // Штабы: только гильдии, без повторов, не больше kMaxHqPerProvince.
      ch |= fixRefList(p.hqs, Seq::Faction, F_PROVINCES, where, "hqs", [&](Id id) { return isGuild(id); }, "не гильдия");
      if (int(p.hqs.size()) > schema::kMaxHqPerProvince) {
        warn(F_PROVINCES, where + ".hqs", "штабов " + std::to_string(p.hqs.size()) + ", допустимо не больше " +
                                              std::to_string(schema::kMaxHqPerProvince) + " — лишние удалены");
        p.hqs.resize(size_t(schema::kMaxHqPerProvince));
        ch = true;
      }

      ch |= fixNum(p.baseTrade, 0.0, 1e12, 0.0, F_PROVINCES, where, "baseTrade");
      ch |= fixNum(p.localTax, -100.0, 100.0, 0.0, F_PROVINCES, where, "localTax");

      if (!p.buildings.empty()) {
        std::vector<ProvBuilding> res;
        bool bch = false;
        for (size_t i = 0; i < p.buildings.size(); i++) {
          ProvBuilding b = p.buildings[i];
          std::string at = where + ".buildings[" + std::to_string(i) + "]";
          const Building* def = base.building(b.building);
          if (!def) {
            warn(F_PROVINCES, at + ".building", "нет постройки " + ref(Seq::Building, b.building) + " — запись удалена");
            bch = true;
            continue;
          }
          if (std::any_of(res.begin(), res.end(), [&](const ProvBuilding& x) { return x.building == b.building; })) {
            warn(F_PROVINCES, at, "повтор постройки " + refStr(Seq::Building, b.building) + " — удалён");
            bch = true;
            continue;
          }
          bch |= fixNum(b.level, 1, int(def->levels.size()), 1, F_PROVINCES, at, "level");
          bch |= fixNum(b.left, 0, kMaxTurns, 0, F_PROVINCES, at, "left");
          // Состояние преобразования — только у постройки преобразования; цикл не длиннее срока рецепта.
          if (!def->convert) {
            if (b.idle || b.cycle) {
              b.idle = false;
              b.cycle = 0;
              bch = true;
            }
          } else {
            bch |= fixNum(b.cycle, 0, std::max(1, def->recipe.turns), 0, F_PROVINCES, at, "cycle");
          }
          if (!b.constructing && b.left != 0) {
            b.left = 0;
            bch = true;
          }
          // Уплаченное за строящийся уровень: только у строящейся постройки; ресурсы каталога, числа ≥ 0;
          // плательщик — существующее государство (иначе возвращать некому).
          if (!b.constructing) {
            if (!b.paid.empty() || b.payer) {
              warn(F_PROVINCES, at + ".paid", "уплаченное указано у достроенной постройки — удалено");
              b.paid.clear();
              b.payer = 0;
              bch = true;
            }
          } else {
            for (auto it = b.paid.begin(); it != b.paid.end();) {
              if (!hasCat(Seq::Resource, it->first)) {
                warn(F_PROVINCES, at + ".paid", "нет ресурса " + refStr(Seq::Resource, it->first) + " — позиция удалена");
                it = b.paid.erase(it);
                bch = true;
              } else {
                bch |= fixNum(it->second, 0.0, 1e12, 0.0, F_PROVINCES, at + ".paid", refStr(Seq::Resource, it->first).c_str());
                ++it;
              }
            }
            if (b.payer && !isState(b.payer)) {
              warn(F_PROVINCES, at + ".payer", exists(Seq::Faction, b.payer) ? refStr(Seq::Faction, b.payer) + " — не государство: плательщик снят"
                                                                            : "нет фракции " + refStr(Seq::Faction, b.payer) + " — плательщик снят");
              b.payer = 0;
              bch = true;
            }
          }
          res.push_back(b);
        }
        if (bch) { p.buildings = std::move(res); ch = true; }
      }
      ch |= fixRefList(p.modifiers, Seq::Modifier, F_PROVINCES, where, "modifiers");
      ch |= fixModTurns(p.modTurns, p.modifiers, F_PROVINCES, where);

      // Рабы на работах: раса справочника, без повторов; только у провинции с владельцем.
      if (!p.slaves.empty()) {
        std::vector<SlaveWork> res;
        bool sch = false;
        for (size_t i = 0; i < p.slaves.size(); i++) {
          SlaveWork s = p.slaves[i];
          std::string at = where + ".slaves[" + std::to_string(i) + "]";
          if (!p.owner) {
            warn(F_PROVINCES, at, "у провинции нет владельца — рабы сняты с работ");
            sch = true;
            continue;
          }
          if (!hasCat(Seq::Race, s.race)) {
            warn(F_PROVINCES, at + ".race", "нет расы " + ref(Seq::Race, s.race) + " — запись удалена");
            sch = true;
            continue;
          }
          sch |= fixNum(s.count, i64(0), kMaxCount, i64(0), F_PROVINCES, at, "count");
          auto same = std::find_if(res.begin(), res.end(), [&](const SlaveWork& x) { return x.race == s.race; });
          if (same != res.end()) {
            warn(F_PROVINCES, at, "повтор расы " + refStr(Seq::Race, s.race) + " — рабы сложены");
            same->count = std::min(kMaxCount, same->count + s.count);
            sch = true;
            continue;
          }
          res.push_back(s);
        }
        if (sch) { p.slaves = std::move(res); ch = true; }
      }

      if (p.occupied) {
        if (!isState(p.occupier) || p.occupier == p.owner) {
          warn(F_PROVINCES, where + ".occupier", p.occupier == 0 ? std::string("оккупация без оккупанта — снята")
                                                 : p.occupier == p.owner ? "оккупант совпадает с владельцем — оккупация снята"
                                                 : exists(Seq::Faction, p.occupier) ? refStr(Seq::Faction, p.occupier) + " — не государство: оккупация снята"
                                                                                    : "нет фракции " + refStr(Seq::Faction, p.occupier) + " — оккупация снята");
          p.occupied = false;
          p.occupier = 0;
          ch = true;
        }
      } else if (p.occupier) {
        warn(F_PROVINCES, where + ".occupier", "оккупант указан, но провинция не оккупирована — оккупант удалён");
        p.occupier = 0;
        ch = true;
      }
      // Оккупационный гарнизон: строки армии оккупанта; без оккупации — снимается.
      if (!p.occupied && (!p.occGarrison.empty() || p.occIdle)) {
        if (!p.occGarrison.empty()) warn(F_PROVINCES, where + ".occGarrison", "провинция не оккупирована — оккупационный гарнизон снят");
        p.occGarrison.clear();
        p.occIdle = 0;
        ch = true;
      }
      if (p.occupied) {
        const Faction* occ = base.faction(p.occupier);
        ch |= fixRowCounts(p.occGarrison, F_PROVINCES, where, "occGarrison", [&](Id row) { return occ && occ->armyRow(row) != nullptr; },
                           [&](Id row) { return occ ? remapRow(occ->id, false, row) : row; }, "у оккупанта нет строки армии");
        ch |= fixNum(p.occIdle, 0, kMaxTurns, 0, F_PROVINCES, where, "occIdle");
      }
      if (ch) tx.province(p.id) = std::move(p);
    });
  }

  void armies() {
    const World base = w();
    std::unordered_set<Id>& usedHeroes = heroUsed;   // герои гарнизонов уже учтены (provinces)
    base.armies.each([&](const Army& a0) {
      Army a = a0;
      const std::string where = refStr(Seq::Army, a.id);
      bool ch = fixEnum(a.kind, 2, ArmyKind::Army, F_ARMIES, where, "kind");
      ch |= fixPoint(a.pos, true, F_ARMIES, where, "pos");
      bool fleet = a.isFleet();
      std::vector<ArmyGroup> groups;
      for (size_t gi = 0; gi < a.groups.size(); gi++) {
        ArmyGroup g = a.groups[gi];
        std::string at = where + ".groups[" + std::to_string(gi) + "]";
        const Faction* f = base.faction(g.faction);
        if (!f) {
          warn(F_ARMIES, at + ".faction", "нет фракции " + ref(Seq::Faction, g.faction) + " — отряды группы удалены");
          ch = true;
          continue;
        }
        std::vector<ArmyUnit> units;
        for (size_t ui = 0; ui < g.units.size(); ui++) {
          ArmyUnit u = g.units[ui];
          std::string uat = at + ".units[" + std::to_string(ui) + "]";
          Id row = remapRow(f->id, fleet, u.row);
          if (row != u.row) { u.row = row; ch = true; }
          if (fleet ? !f->fleetRow(u.row) : !f->armyRow(u.row)) {
            warn(F_ARMIES, uat + ".row", std::string("у ") + refStr(Seq::Faction, f->id) + (fleet ? " нет строки флота " : " нет строки армии ") +
                                             ref(Seq::Row, u.row) + " — отряд удалён");
            ch = true;
            continue;
          }
          ch |= fixNum(u.count, i64(0), kMaxCount, i64(0), F_ARMIES, uat, "count");
          auto same = std::find_if(units.begin(), units.end(), [&](const ArmyUnit& x) { return x.row == u.row; });
          if (same != units.end()) {
            warn(F_ARMIES, uat, "повтор строки " + refStr(Seq::Row, u.row) + " — численности сложены");
            same->count = std::min(kMaxCount, same->count + u.count);
            ch = true;
            continue;
          }
          units.push_back(u);
        }
        g.units = std::move(units);
        // Герой — персонаж, состоящий не больше чем в одном войске.
        std::vector<Id> heroes;
        for (Id h : g.heroes) {
          if (!exists(Seq::Character, h)) {
            warn(F_ARMIES, at + ".heroes", "нет персонажа " + ref(Seq::Character, h) + " — удалён из войска");
            ch = true;
            continue;
          }
          if (usedHeroes.count(h)) {
            warn(F_ARMIES, at + ".heroes", "персонаж " + refStr(Seq::Character, h) + " уже в другом войске, группе или гарнизоне — удалён");
            ch = true;
            continue;
          }
          usedHeroes.insert(h);
          heroes.push_back(h);
        }
        g.heroes = std::move(heroes);
        auto same = std::find_if(groups.begin(), groups.end(), [&](const ArmyGroup& x) { return x.faction == g.faction; });
        if (same != groups.end()) {
          warn(F_ARMIES, at, "вторая группа фракции " + refStr(Seq::Faction, g.faction) + " — объединена с первой");
          for (auto& u : g.units) {
            auto su = std::find_if(same->units.begin(), same->units.end(), [&](const ArmyUnit& x) { return x.row == u.row; });
            if (su != same->units.end()) su->count = std::min(kMaxCount, su->count + u.count);
            else same->units.push_back(u);
          }
          for (Id h : g.heroes) same->heroes.push_back(h);
          ch = true;
          continue;
        }
        groups.push_back(std::move(g));
      }
      if (groups.empty()) {
        warn(F_ARMIES, where, a.groups.empty() ? std::string("в войске нет ни одной группы отрядов — войско удалено")
                                               : std::string("не осталось ни одной допустимой группы — войско удалено"));
        tx.eraseArmy(a.id);
        return;
      }
      a.groups = std::move(groups);
      ch |= fixRef(a.commander, Seq::Character, F_ARMIES, where, "commander");
      ch |= fixNum(a.loyalty, schema::kMinLoyalty, schema::kMaxLoyalty, schema::kMaxLoyalty, F_ARMIES, where, "loyalty");
      ch |= fixRefList(a.modifiers, Seq::Modifier, F_ARMIES, where, "modifiers");
      ch |= fixModTurns(a.modTurns, a.modifiers, F_ARMIES, where);
      if (ch) tx.army(a.id) = std::move(a);
    });
  }

  void routes() {
    const World base = w();
    base.routes.each([&](const Route& r0) {
      Route r = r0;
      const std::string where = refStr(Seq::Route, r.id);
      bool ch = false;
      if (r.guild && !isGuild(r.guild)) {
        warn(F_ROUTES, where + ".guild", exists(Seq::Faction, r.guild) ? refStr(Seq::Faction, r.guild) + " — не гильдия: владелец удалён"
                                                                      : "нет фракции " + refStr(Seq::Faction, r.guild) + " — владелец удалён");
        r.guild = 0;
        ch = true;
      }
      ch |= fixPoints(r.pts, F_ROUTES, where, "pts");
      if (r.pts.size() < 2) {
        warn(F_ROUTES, where, "у маршрута меньше двух точек — маршрут удалён");
        tx.eraseRoute(r.id);
        return;
      }
      if (ch) tx.route(r.id) = std::move(r);
    });
  }

  void deals() {
    const World base = w();
    base.deals.each([&](const Deal& d0) {
      Deal d = d0;
      const std::string where = refStr(Seq::Deal, d.id);
      if (!exists(Seq::Faction, d.a) || !exists(Seq::Faction, d.b) || d.a == d.b) {
        warn(F_DEALS, where, d.a == d.b && d.a ? "стороны сделки совпадают — сделка удалена"
                                               : "нет стороны сделки " + ref(Seq::Faction, exists(Seq::Faction, d.a) ? d.b : d.a) + " — сделка удалена");
        tx.eraseDeal(d.id);
        return;
      }
      bool ch = fixEnum(d.kind, 3, DealKind::Trade, F_DEALS, where, "kind");
      ch |= fixEnum(d.status, 3, DealStatus::Active, F_DEALS, where, "status");
      ch |= fixNum(d.turn, 1, kMaxTurn, 1, F_DEALS, where, "turn");
      std::vector<DealItem> items;
      for (size_t i = 0; i < d.items.size(); i++) {
        DealItem it = d.items[i];
        std::string at = where + ".items[" + std::to_string(i) + "]";
        if (fixEnum(it.kind, int(DealItemKind::Count), DealItemKind::Resource, F_DEALS, at, "kind")) ch = true;
        if (it.kind == DealItemKind::Province || it.kind == DealItemKind::Hero) {
          // Провинция и пленный герой передаются разово; ссылка — на существующую запись.
          const bool prov = it.kind == DealItemKind::Province;
          if (!exists(prov ? Seq::Province : Seq::Character, it.ref)) {
            warn(F_DEALS, at + ".ref", std::string("нет ") + (prov ? "провинции " : "персонажа ") + ref(prov ? Seq::Province : Seq::Character, it.ref) +
                                       " — позиция удалена");
            ch = true;
            continue;
          }
          if (it.mode != DealMode::Once || it.res != kGold) {
            it.mode = DealMode::Once;
            it.res = kGold;
            ch = true;
          }
          items.push_back(it);
          continue;
        }
        if (it.ref) {
          it.ref = 0;
          ch = true;
        }
        if (!hasCat(Seq::Resource, it.res)) {
          warn(F_DEALS, at + ".res", "нет ресурса " + ref(Seq::Resource, it.res) + " — позиция удалена");
          ch = true;
          continue;
        }
        ch |= fixEnum(it.from, 2, DealSide::A, F_DEALS, at, "from");
        ch |= fixEnum(it.mode, 2, DealMode::Once, F_DEALS, at, "mode");
        ch |= fixNum(it.amount, 0.0, 1e12, 0.0, F_DEALS, at, "amount");
        ch |= fixNum(it.turns, 1, kMaxTurn, 1, F_DEALS, at, "turns");
        ch |= fixNum(it.left, 0, it.turns, 0, F_DEALS, at, "left");
        items.push_back(it);
      }
      if (ch) {
        d.items = std::move(items);
        tx.deal(d.id) = std::move(d);
      }
    });
  }

  // Хроника хранит историю: ссылки на удалённые провинции, войска и фракции допустимы и не проверяются.
  void log() {
    const World base = w();
    base.log.each([&](const LogEntry& l0) {
      const std::string where = refStr(Seq::Log, l0.id);
      LogEntry l = l0;
      bool ch = fixNum(l.turn, 1, kMaxTurn, 1, F_LOG, where, "turn");
      ch |= fixEnum(l.kind, int(LogKind::Count), LogKind::Note, F_LOG, where, "kind");
      ch |= fixRefList(l.factions, Seq::Faction, F_LOG, where, "factions", [](Id) { return true; });
      if (ch) tx.add(std::move(l));  // запись с тем же ID заменяется
    });
  }

  void relations() {
    const RelMap& rel = *w().relations;
    bool bad = false;
    for (auto& [k, r] : rel) {
      Id a = Id(k >> 32), b = Id(k & 0xFFFFFFFFu);
      if (!exists(Seq::Faction, a) || !exists(Seq::Faction, b) || a == b || a > b || !std::isfinite(r.v) || r.v < -100 ||
          r.v > 100 || int(r.s) < 0 || int(r.s) > 3)
        bad = true;
    }
    if (!bad) return;
    RelMap res;
    for (auto& [k, r0] : rel) {
      Id a = Id(k >> 32), b = Id(k & 0xFFFFFFFFu);
      std::string where = refStr(Seq::Faction, std::min(a, b)) + "|" + refStr(Seq::Faction, std::max(a, b));
      if (a == b) {
        warn(F_RELATIONS, where, "отношение фракции к самой себе — удалено");
        continue;
      }
      if (!exists(Seq::Faction, a) || !exists(Seq::Faction, b)) {
        warn(F_RELATIONS, where, "нет фракции " + refStr(Seq::Faction, exists(Seq::Faction, a) ? b : a) + " — отношение удалено");
        continue;
      }
      Relation r = r0;
      fixNum(r.v, -100.0, 100.0, 0.0, F_RELATIONS, where, "v");
      fixEnum(r.s, 4, RelStatus::Unknown, F_RELATIONS, where, "s");
      res[relKey(a, b)] = r;
    }
    tx.relations() = std::move(res);
  }

  // Счётчики ID не меньше наибольших занятых ID.
  void counters() {
    std::array<u32, kSeqCount> maxId{};
    auto upd = [&](Seq s, Id id) { maxId[size_t(s)] = std::max(maxId[size_t(s)], id); };
    const World& x = w();
    x.nodes.each([&](const Node& e) { upd(Seq::Node, e.id); });
    x.edges.each([&](const Edge& e) { upd(Seq::Edge, e.id); });
    x.provinces.each([&](const Province& e) { upd(Seq::Province, e.id); });
    x.factions.each([&](const Faction& e) {
      upd(Seq::Faction, e.id);
      for (auto& r : e.army) upd(Seq::Row, r.id);
      for (auto& r : e.fleet) upd(Seq::Row, r.id);
      for (auto& s : e.council) upd(Seq::Council, s.id);
    });
    x.characters.each([&](const Character& e) { upd(Seq::Character, e.id); });
    x.modifiers.each([&](const Modifier& e) { upd(Seq::Modifier, e.id); });
    x.buildings.each([&](const Building& e) { upd(Seq::Building, e.id); });
    x.techs.each([&](const Tech& e) { upd(Seq::Tech, e.id); });
    x.armies.each([&](const Army& e) { upd(Seq::Army, e.id); });
    x.routes.each([&](const Route& e) { upd(Seq::Route, e.id); });
    x.deals.each([&](const Deal& e) { upd(Seq::Deal, e.id); });
    x.log.each([&](const LogEntry& e) { upd(Seq::Log, e.id); });
    x.symbols.each([&](const MapSymbol& e) { upd(Seq::Symbol, e.id); });
    x.shapes.each([&](const MapShape& e) { upd(Seq::Shape, e.id); });
    const Catalogs& c = *x.catalogs;
    for (auto& i : c.resources) upd(Seq::Resource, i.id);
    for (auto& i : c.races) upd(Seq::Race, i.id);
    for (auto& i : c.cultures) upd(Seq::Culture, i.id);
    for (auto& i : c.religions) upd(Seq::Religion, i.id);
    for (auto& i : c.governments) upd(Seq::Government, i.id);
    for (auto& i : c.positions) upd(Seq::Position, i.id);
    for (auto& i : c.essences) upd(Seq::Essence, i.id);
    for (auto& i : c.relics) upd(Seq::Relic, i.id);
    for (auto& i : c.specials) upd(Seq::Special, i.id);
    for (auto& i : c.resGroups) upd(Seq::ResGroup, i.id);
    static const char* const names[kSeqCount] = {"province", "faction", "character", "modifier", "building", "tech", "army",
                                                 "route", "deal", "log", "row", "council", "node", "edge", "resource", "race",
                                                 "culture", "religion", "government", "position", "symbol", "shape",
                                                 "essence", "relic", "special", "resGroup"};
    for (int i = 0; i < kSeqCount; i++) {
      if (seq[size_t(i)] < maxId[size_t(i)]) {
        warn(F_WORLD, std::string("meta.seq.") + names[i], "счётчик " + std::to_string(seq[size_t(i)]) + " меньше наибольшего ID " +
                                                          std::to_string(maxId[size_t(i)]) + " — поднят");
        seq[size_t(i)] = maxId[size_t(i)];
      }
    }
    if (seq != x.meta->seq) tx.meta().seq = seq;
  }

  // ТЗ «Виды государств», п.1: героям государств без модификатора природы — природа по виду государства. Один раз:
  // только пока в мире нет записи этого встроенного модификатора (мир сохранён до появления видов государств);
  // после — природу снимают и меняют по правилам модификаторов. Выполняется после counters (выдаются новые ID).
  void natures() {
    static const char* const keys[] = {schema::mod::Living, schema::mod::Undead, schema::mod::Demon};
    const World base = w();
    std::unordered_set<std::string> present;
    base.modifiers.each([&](const Modifier& m) { if (!m.key.empty()) present.insert(m.key); });
    for (int k = 0; k < 3; k++) {
      const char* key = keys[k];
      if (present.count(key)) continue;
      std::vector<Id> heroes;
      base.characters.each([&](const Character& c) {
        const Faction* f = base.faction(c.faction);
        if (!f || !f->isState() || int(f->stateKind) != k) return;
        for (Id m : c.modifiers)
          if (const Modifier* x = base.modifier(m); x && schema::isNatureKey(x->key)) return;
        heroes.push_back(c.id);
      });
      if (heroes.empty()) continue;
      Modifier m = *schema::builtinModifier(key);
      m.id = 0;
      const Id mid = tx.add(std::move(m)).id;
      for (Id h : heroes) tx.character(h).modifiers.push_back(mid);
      warn(F_CHARACTERS, "", "героям государств без природы назначен модификатор «" + schema::builtinModifier(key)->name + "»: " +
                                 std::to_string(heroes.size()));
    }
  }

  void run() {
    meta();
    catalogs();
    counters();   // базовые записи получают ID после наибольших занятых
    content();
    constants();
    nodes();
    edges();
    mapObjects();
    factions();
    characters();
    modifiers();
    buildings();
    techs();
    provinces();
    armies();
    routes();
    deals();
    log();
    relations();
    counters();
    natures();
  }
};

}  // namespace

u32 normalize(World& w, Warnings& warnings) {
  Tx tx(w);
  Norm n(tx, warnings);
  n.run();
  World out = std::move(tx).finish();
  u32 mask = World::diff(w, out);
  w = std::move(out);
  return mask;
}

}  // namespace rg::io
