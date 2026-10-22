// Regnum — чтение мира из JSON: разбор файлов проекта с проверкой типов, ссылок и неизвестных полей (ошибки —
// предупреждения и значения по умолчанию), проверка формата и версии.
#include "codec/base64.h"
#include "core/io_internal.h"

namespace rg::io::detail {

using json::Value;
using schema::CatalogList;
using schema::EnumInfo;

// ================================================================ чтение
namespace {

std::string show(const Value& v) {
  std::string s = json::write(v, json::WriteOptions{0, false, true});
  if (utf8::count(s) > 40) {
    size_t i = 0;
    for (int k = 0; k < 40 && i < s.size(); k++) i = utf8::next(s, i);
    s = s.substr(0, i) + "…";
  }
  return s;
}

struct Ctx {
  const char* file;
  Warnings& out;
  void warn(std::string where, std::string msg) { out.push_back(Warning{file, std::move(where), std::move(msg)}); }
};

// Плоский массив координат [x1, y1, ...]: ошибки — предупреждение (путь where) и пропуск точки.
std::vector<Vec2> flatPoints(Ctx& c, const json::Array& items, const std::string& where) {
  std::vector<Vec2> out;
  out.reserve(items.size() / 2);
  for (size_t i = 0; i + 1 < items.size(); i += 2) {
    if (items[i].isNum() && items[i + 1].isNum()) out.push_back({items[i].asNum(), items[i + 1].asNum()});
    else c.warn(where + "[" + std::to_string(i) + "]", "ожидались числа x, y — точка пропущена");
  }
  if (items.size() % 2) c.warn(where, "нечётное число координат — последняя отброшена");
  return out;
}

// Объект записи JSON: чтение полей с проверкой типов и учётом неизвестных полей.
class Rec {
 public:
  Rec(const Value& v, Ctx& c, int index = -1) : v_(v), c_(c), index_(index) {}
  Rec(const Value& v, Ctx& c, const Rec* parent, std::string_view key, int index)
      : v_(v), c_(c), parent_(parent), key_(key), index_(index) {}
  Rec(const Rec&) = delete;
  Rec& operator=(const Rec&) = delete;

  Ctx& ctx() { return c_; }
  void setBase(std::string b) { base_ = std::move(b); }

  std::string where() const {
    if (!base_.empty()) return base_;
    std::string w;
    if (parent_) w = parent_->where(key_);
    if (index_ >= 0) w += "[" + std::to_string(index_) + "]";
    return w;
  }
  std::string where(std::string_view key) const {
    std::string w = where();
    if (!w.empty()) w += '.';
    w += key;
    return w;
  }
  void warn(std::string_view key, std::string msg) { c_.warn(where(key), std::move(msg)); }
  void warnHere(std::string msg) { c_.warn(where(), std::move(msg)); }

  // Поле записи (null — нет поля).
  const Value* get(std::string_view key) {
    const Value* x = v_.find(key);
    if (std::find(known_.begin(), known_.end(), key) == known_.end()) {
      known_.push_back(key);
      if (x) found_++;
    }
    return x;
  }
  bool has(std::string_view key) const { return v_.has(key); }

  std::string str(std::string_view key, std::string def = {}) {
    const Value* x = get(key);
    if (!x || x->isNull()) return def;
    if (x->isStr()) return x->asStr();
    if (x->isNum() || x->isBool()) {
      warn(key, "ожидалась строка, получено " + show(*x) + " — записано как строка");
      return show(*x);
    }
    warn(key, "ожидалась строка, получено " + show(*x) + " — поле пропущено");
    return def;
  }

  double num(std::string_view key, double def) {
    const Value* x = get(key);
    if (!x || x->isNull()) return def;
    if (x->isNum()) return x->asNum();
    if (x->isStr()) {
      if (auto d = parseNum(x->asStr()); d && std::isfinite(*d)) {
        warn(key, "число записано строкой " + show(*x));
        return *d;
      }
    }
    warn(key, "ожидалось число, получено " + show(*x) + " — поле пропущено");
    return def;
  }

  i64 integer(std::string_view key, i64 def) {
    const Value* x = get(key);
    if (!x || x->isNull()) return def;
    double d = 0;
    std::optional<double> parsed = x->isStr() ? parseNum(x->asStr()) : std::nullopt;
    if (x->isNum()) d = x->asNum();
    else if (parsed && std::isfinite(*parsed)) {
      d = *parsed;
      warn(key, "число записано строкой " + show(*x));
    } else {
      warn(key, "ожидалось целое число, получено " + show(*x) + " — поле пропущено");
      return def;
    }
    if (std::fabs(d) > 9.0e15) {
      warn(key, "слишком большое число " + show(*x) + " — поле пропущено");
      return def;
    }
    double r = std::nearbyint(d);
    if (r != d) warn(key, "ожидалось целое число, получено " + show(*x) + " — округлено");
    return i64(r);
  }

  int intv(std::string_view key, int def) {
    i64 v = integer(key, def);
    if (v < -2000000000 || v > 2000000000) {
      warn(key, "число вне допустимого диапазона — поле пропущено");
      return def;
    }
    return int(v);
  }

  bool flag(std::string_view key, bool def) {
    const Value* x = get(key);
    if (!x || x->isNull()) return def;
    if (x->isBool()) return x->asBool();
    if (x->isNum() && (x->asNum() == 0 || x->asNum() == 1)) {
      warn(key, "ожидалось true или false, получено " + show(*x));
      return x->asNum() == 1;
    }
    if (x->isStr() && (x->asStr() == "true" || x->asStr() == "false")) {
      warn(key, "ожидалось true или false, получено " + show(*x));
      return x->asStr() == "true";
    }
    warn(key, "ожидалось true или false, получено " + show(*x) + " — поле пропущено");
    return def;
  }

  Id refOf(const Value& x, std::string_view key, Seq s) {
    auto r = parseRef(x, s);
    if (!r) {
      warn(key, "неверная ссылка " + show(x) + " (ожидалось вида «" + std::string(schema::idPrefix(s)) + "12») — ссылка удалена");
      return 0;
    }
    return *r;
  }
  Id ref(std::string_view key, Seq s) {
    const Value* x = get(key);
    return x ? refOf(*x, key, s) : 0;
  }

  // ID записи: нет или неверный — 0 (новый ID назначается при сборке мира).
  Id id(Seq s) {
    const Value* x = get("id");
    if (!x || x->isNull()) {
      warnHere("нет поля id — будет назначен новый ID");
      return 0;
    }
    auto r = parseRef(*x, s);
    if (!r || *r == 0) {
      warn("id", "неверный ID " + show(*x) + " (ожидалось вида «" + std::string(schema::idPrefix(s)) + "12») — будет назначен новый");
      return 0;
    }
    return *r;
  }

  const Value* arr(std::string_view key) {
    const Value* x = get(key);
    if (!x || x->isNull()) return nullptr;
    if (!x->isArr()) {
      warn(key, "ожидался массив [...] — поле пропущено");
      return nullptr;
    }
    return x;
  }
  const Value* obj(std::string_view key) {
    const Value* x = get(key);
    if (!x || x->isNull()) return nullptr;
    if (!x->isObj()) {
      warn(key, "ожидался объект {...} — поле пропущено");
      return nullptr;
    }
    return x;
  }

  // Стопки реликвий: объект «ID → количество» или (прежний формат) массив ID — по одной штуке.
  std::vector<RelicStack> relicStacks(std::string_view key) {
    std::vector<RelicStack> out;
    const Value* x = get(key);
    if (!x || x->isNull()) return out;
    auto add = [&](Id r, i64 n) {
      for (RelicStack& st : out)
        if (st.relic == r) {
          st.count += n;
          return;
        }
      out.push_back(RelicStack{r, n});
    };
    if (x->isArr()) {
      for (Id r : refs(key, Seq::Relic)) add(r, 1);
      return out;
    }
    if (!x->isObj()) {
      warn(key, "ожидался объект {«re1»: количество} — поле пропущено");
      return out;
    }
    for (auto& [k, v] : x->members()) {
      auto r = parseRef(Value(k), Seq::Relic);
      if (!r || !*r) {
        warn(std::string(key) + "." + k, "неверный ключ (ожидалось вида «re1») — пропущен");
        continue;
      }
      if (!v.isNum() || !(v.asNum() >= 1)) {
        warn(std::string(key) + "." + k, "количество должно быть целым числом не меньше 1 — пропущено");
        continue;
      }
      add(*r, i64(std::min(v.asNum(), double(schema::kMaxCount))));
    }
    return out;
  }

  std::vector<Id> refs(std::string_view key, Seq s) {
    std::vector<Id> out;
    const Value* a = arr(key);
    if (!a) return out;
    out.reserve(a->size());
    const auto& items = a->items();
    for (size_t i = 0; i < items.size(); i++) {
      auto r = parseRef(items[i], s);
      if (!r) warn(std::string(key) + "[" + std::to_string(i) + "]",
                   "неверная ссылка " + show(items[i]) + " (ожидалось вида «" + std::string(schema::idPrefix(s)) + "12») — удалена");
      else if (*r) out.push_back(*r);
    }
    return out;
  }

  int enumv(std::string_view key, Enum e, int def) {
    const Value* x = get(key);
    if (!x || x->isNull()) return def;
    if (x->isStr()) {
      int i = schema::findEnum(e.list, e.n, x->asStr());
      if (i >= 0) return i;
    }
    warn(key, "неизвестное значение " + show(*x) + " (допустимо: " + enumChoices(e) + ") — взято «" + e.list[def].id + "»");
    return def;
  }

  std::optional<Color> colorOf(const Value& x, std::string_view key) {
    if (x.isStr())
      if (auto c = Color::parse(x.asStr())) return c;
    warn(key, "неверный цвет " + show(x) + " (ожидалось «#rrggbb») — взят цвет по умолчанию");
    return std::nullopt;
  }
  Color color(std::string_view key, Color def) {
    const Value* x = get(key);
    if (!x || x->isNull()) return def;
    return colorOf(*x, key).value_or(def);
  }
  std::optional<Color> optColor(std::string_view key) {
    const Value* x = get(key);
    if (!x || x->isNull()) return std::nullopt;
    return colorOf(*x, key);
  }

  Vec2 pos(std::string_view key) {
    const Value* x = get(key);
    if (!x || x->isNull()) return {};
    if (x->isArr() && x->size() == 2 && (*x)[0].isNum() && (*x)[1].isNum()) return {(*x)[0].asNum(), (*x)[1].asNum()};
    if (x->isObj() && x->get("x").isNum() && x->get("y").isNum()) return {x->get("x").asNum(), x->get("y").asNum()};
    warn(key, "ожидались координаты [x, y], получено " + show(*x) + " — взято [0, 0]");
    return {};
  }

  // Точки: плоский массив [x1, y1, x2, y2, ...] или пары [[x1, y1], ...].
  std::vector<Vec2> pts(std::string_view key) {
    std::vector<Vec2> out;
    const Value* a = arr(key);
    if (!a) return out;
    const auto& items = a->items();
    if (!items.empty() && items[0].isArr()) {
      out.reserve(items.size());
      for (size_t i = 0; i < items.size(); i++) {
        const Value& p = items[i];
        if (p.isArr() && p.size() == 2 && p[0].isNum() && p[1].isNum()) out.push_back({p[0].asNum(), p[1].asNum()});
        else warn(std::string(key) + "[" + std::to_string(i) + "]", "ожидалась точка [x, y] — пропущена");
      }
      return out;
    }
    return flatPoints(c_, items, where(key));
  }

  std::map<Id, double> resMap(std::string_view key, Seq s = Seq::Resource) {
    std::map<Id, double> out;
    const Value* o = obj(key);
    if (!o) return out;
    for (auto& [k, v] : o->members()) {
      auto r = parseRef(Value(k), s);
      if (!r || !*r) {
        warn(std::string(key) + "." + k, "неверный ключ (ожидалось вида «" + std::string(schema::idPrefix(s)) + "1») — пропущен");
        continue;
      }
      if (!v.isNum()) {
        warn(std::string(key) + "." + k, "ожидалось число, получено " + show(v) + " — пропущено");
        continue;
      }
      out[*r] = v.asNum();
    }
    return out;
  }

  // Набор значений перечисления: массив идентификаторов → биты; неизвестные пропускаются.
  u32 mask(std::string_view key, Enum e) {
    u32 m = 0;
    const Value* a = arr(key);
    if (!a) return m;
    const auto& items = a->items();
    for (size_t i = 0; i < items.size(); i++) {
      int k = items[i].isStr() ? schema::findEnum(e.list, e.n, items[i].asStr()) : -1;
      if (k < 0 || k >= 32) {
        warn(std::string(key) + "[" + std::to_string(i) + "]", "неизвестное значение " + show(items[i]) + " (допустимо: " + enumChoices(e) + ") — пропущено");
        continue;
      }
      m |= 1u << k;
    }
    return m;
  }
  std::vector<std::string> strs(std::string_view key) {
    std::vector<std::string> out;
    const Value* a = arr(key);
    if (!a) return out;
    const auto& items = a->items();
    for (size_t i = 0; i < items.size(); i++) {
      if (items[i].isStr()) out.push_back(items[i].asStr());
      else warn(std::string(key) + "[" + std::to_string(i) + "]", "ожидалась строка — значение пропущено");
    }
    return out;
  }

  std::string bytes(std::string_view key) {
    const Value* x = get(key);
    if (!x || x->isNull()) return {};
    if (x->isStr()) {
      std::string_view t = x->asStr();
      // Допускается и data URI: «data:image/png;base64,...».
      if (t.substr(0, 5) == "data:")
        if (size_t comma = t.find(','); comma != std::string_view::npos && t.substr(0, comma).find(";base64") != std::string_view::npos)
          t.remove_prefix(comma + 1);
      if (auto d = codec::base64::decodeString(t)) return std::move(*d);
    }
    warn(key, "ожидались данные в base64 — поле пропущено");
    return {};
  }

  // Массив объектов: fn(Rec&) для каждого элемента.
  template <class F> void list(std::string_view key, F&& fn) {
    const Value* a = arr(key);
    if (!a) return;
    const auto& items = a->items();
    for (size_t i = 0; i < items.size(); i++) {
      if (!items[i].isObj()) {
        warn(std::string(key) + "[" + std::to_string(i) + "]", "ожидался объект {...} — элемент пропущен");
        continue;
      }
      Rec r(items[i], c_, this, key, int(i));
      fn(r);
      r.done();
    }
  }
  // Вложенный объект: fn(Rec&).
  template <class F> void sub(std::string_view key, F&& fn) {
    const Value* o = obj(key);
    if (!o) return;
    Rec r(*o, c_, this, key, -1);
    fn(r);
    r.done();
  }

  // Предупредить о неизвестных полях.
  void done() {
    if (found_ >= v_.size()) return;
    for (auto& m : v_.members()) {
      if (std::find(known_.begin(), known_.end(), std::string_view(m.first)) == known_.end())
        warn(m.first, "неизвестное поле отброшено");
    }
  }

 private:
  const Value& v_;
  Ctx& c_;
  const Rec* parent_ = nullptr;
  std::string_view key_;
  int index_ = -1;
  std::string base_;
  std::vector<std::string_view> known_;
  size_t found_ = 0;
};

i64 countOf(Rec& r, std::string_view key) { return r.integer(key, 0); }

// ID записи справочника (нет или неверный — 0, новый назначается при сборке мира); путь предупреждений — «key.<ID>».
Id itemId(Rec& e, Seq s, std::string_view key) {
  const Id id = e.id(s);
  if (id) e.setBase(std::string(key) + "." + refStr(s, id));
  return id;
}

// Группы дерева справочника (группы ресурсов, группы реликвий).
template <class Tag> void decGroups(Rec& r, const char* key, Seq s, std::vector<TreeGroup<Tag>>& out) {
  out.clear();
  r.list(key, [&](Rec& e) {
    TreeGroup<Tag> g;
    g.id = itemId(e, s, key);
    g.name = e.str("name");
    g.parent = e.ref("parent", s);
    g.key = e.str("key");
    out.push_back(std::move(g));
  });
}

void decMeta(Rec& r, Meta& m) {
  m.name = r.str("name", m.name);
  m.createdAt = r.str("createdAt");
  m.updatedAt = r.str("updatedAt");
  m.turn = r.intv("turn", 1);
  r.sub("seq", [&](Rec& s) {
    for (int i = 0; i < kSeqCount; i++) {
      const char* name = schema::kSeqs[i].name;
      i64 v = s.integer(name, 0);
      if (v < 0 || v > i64(0xFFFFFFFFu)) {
        s.warn(name, "счётчик вне диапазона — взят 0");
        v = 0;
      }
      m.seq[size_t(i)] = u32(v);
    }
  });
  m.basemap = r.str("basemap", m.basemap);
  m.mapObjects = r.flag("mapObjects", false);
  m.content = r.intv("content", 0);
  m.rng = u32(std::clamp<i64>(r.integer("rng", 0), 0, i64(0xFFFFFFFFu)));
  m.notes = r.str("notes");
}

void decSettings(Rec& r, Settings& s) {
  s.fillOpacity = float(r.num("fillOpacity", s.fillOpacity));
  s.labelStates = r.flag("labelStates", s.labelStates);
  s.labelProvinces = r.flag("labelProvinces", s.labelProvinces);
  s.labelArmies = r.flag("labelArmies", s.labelArmies);
  s.showArmies = r.flag("showArmies", s.showArmies);
  s.figureSize = r.intv("figureSize", s.figureSize);
  s.occupiedIncome = OccupiedIncome(r.enumv("occupiedIncome", en(schema::kOccupiedIncome), int(s.occupiedIncome)));
  s.rebellionRoll = r.flag("rebellionRoll", s.rebellionRoll);
  s.autosaveFolder = r.flag("autosaveFolder", s.autosaveFolder);
  s.autosaveSec = r.intv("autosaveSec", s.autosaveSec);
  s.freeFleets = r.flag("freeFleets", s.freeFleets);
}

void decCatalogs(Rec& r, Catalogs& c) {
  for (const schema::CatalogListInfo& L : schema::kCatalogLists) {
    auto& list = c.*L.items;
    list.clear();
    r.list(L.key, [&](Rec& e) {
      CatalogItem it;
      it.id = itemId(e, L.seq, L.key);
      it.name = e.str("name");
      it.color = e.color("color", it.color);
      it.icon = e.str("icon");
      it.builtin = e.flag("builtin", false);
      if (L.list == CatalogList::Resources) {
        it.key = e.str("key");
        it.group = e.ref("group", Seq::ResGroup);
      }
      if (L.list == CatalogList::Positions) {
        it.modifiers = e.refs("modifiers", Seq::Modifier);
        it.vacantModifiers = e.refs("vacantModifiers", Seq::Modifier);
      }
      list.push_back(std::move(it));
    });
  }
  decGroups(r, "resGroups", Seq::ResGroup, c.resGroups);
  c.relics.clear();
  r.list("relics", [&](Rec& e) {
    Relic x;
    x.id = itemId(e, Seq::Relic, "relics");
    x.name = e.str("name");
    x.rarity = Rarity(e.enumv("rarity", en(kRarityE), 0));
    x.desc = e.str("desc");
    x.group = e.ref("group", Seq::RelicGroup);
    x.entity = e.str("entity");
    x.image = e.bytes("image");
    c.relics.push_back(std::move(x));
  });
  decGroups(r, "relicGroups", Seq::RelicGroup, c.relicGroups);
  c.classes.clear();
  r.list("classes", [&](Rec& e) {
    HeroClass k;
    k.id = itemId(e, Seq::HeroClass, "classes");
    k.name = e.str("name");
    k.desc = e.str("desc");
    k.icon = e.str("icon", k.icon);
    k.color = e.color("color", k.color);
    auto talentsOf = [&](Rec& q, std::vector<Talent>& out) {
      q.list("talents", [&](Rec& z) {
        Talent t;
        t.id = z.ref("id", Seq::Talent);
        t.name = z.str("name");
        t.desc = z.str("desc");
        t.icon = z.str("icon", t.icon);
        t.row = z.intv("row", 0);
        t.col = z.intv("col", 0);
        t.cost = z.intv("cost", 1);
        t.ranks = z.intv("ranks", 1);
        t.prereq = z.ref("prereq", Seq::Talent);
        t.modifiers = z.refs("modifiers", Seq::Modifier);
        out.push_back(std::move(t));
      });
    };
    e.list("subclasses", [&](Rec& q) {
      SubClass sc;
      sc.id = q.ref("id", Seq::SubClass);
      sc.name = q.str("name");
      sc.desc = q.str("desc");
      sc.icon = q.str("icon", sc.icon);
      sc.tierPoints = q.intv("tierPoints", schema::kDefaultTierPoints);
      talentsOf(q, sc.talents);
      k.subclasses.push_back(std::move(sc));
    });
    // Прежний формат: дерево талантов у самого класса — становится подклассом с названием класса (ID выдаст
    // нормализация, она же назначит подкласс героям класса).
    if (e.has("talents") || e.has("tierPoints")) {
      SubClass sc;
      sc.name = k.name;
      sc.tierPoints = e.intv("tierPoints", 5);
      talentsOf(e, sc.talents);
      if (!sc.talents.empty()) k.subclasses.push_back(std::move(sc));
    }
    c.classes.push_back(std::move(k));
  });
  c.archSites.clear();
  r.list("archSites", [&](Rec& e) {
    ArchSite s;
    s.id = itemId(e, Seq::ArchSite, "archSites");
    s.key = e.str("key");
    s.name = e.str("name");
    s.icon = e.str("icon", s.icon);
    if (const Value* rw = e.arr("rewards")) {
      const auto& items = rw->items();
      for (size_t i = 0; i < items.size() && i < size_t(kArchSlots); i++) {
        if (!items[i].isArr()) {
          e.warn("rewards[" + std::to_string(i) + "]", "ожидался массив сундуков — пропущен");
          continue;
        }
        for (const Value& v : items[i].items()) {
          auto id = parseRef(v, Seq::Chest);
          if (id && *id) s.rewards[i].push_back(*id);
          else e.warn("rewards[" + std::to_string(i) + "]", "неверная ссылка " + show(v) + " — удалена");
        }
      }
      if (items.size() > size_t(kArchSlots)) e.warn("rewards", "больше четырёх списков — лишние отброшены");
    }
    c.archSites.push_back(std::move(s));
  });
  c.chests.clear();
  r.list("chests", [&](Rec& e) {
    Chest ch;
    ch.id = itemId(e, Seq::Chest, "chests");
    ch.key = e.str("key");
    ch.name = e.str("name");
    ch.icon = e.str("icon", ch.icon);
    ch.desc = e.str("desc");
    e.list("items", [&](Rec& q) {
      ChestItem it;
      it.kind = ChestItemKind(q.enumv("kind", en(schema::kChestItemKinds), 0));
      it.amount = q.num("amount", 0);
      it.res = q.ref("res", Seq::Resource);
      it.group = q.ref("group", Seq::ResGroup);
      it.exclude = q.refs("exclude", Seq::Resource);
      it.essence = q.ref("essence", Seq::Essence);
      it.relicGroup = q.ref("relicGroup", Seq::RelicGroup);
      it.rarities = q.mask("rarities", en(kRarityE));
      it.chests = q.refs("chests", Seq::Chest);
      ch.items.push_back(std::move(it));
    });
    c.chests.push_back(std::move(ch));
  });
  c.specials.clear();
  r.list("specials", [&](Rec& e) {
    SpecialUnit s;
    s.id = itemId(e, Seq::Special, "specials");
    s.name = e.str("name");
    s.type = UnitType(e.enumv("type", en(schema::kUnitTypes), int(UnitType::Monsters)));
    s.race = e.str("race");
    s.keyRes = e.ref("keyRes", Seq::Resource);
    s.keyPer = e.num("keyPer", 1);
    s.extra = e.resMap("extra");
    s.essence = e.resMap("essence", Seq::Essence);
    s.upkeep = e.num("upkeep", 0);
    s.essUpkeep = e.resMap("essUpkeep", Seq::Essence);
    s.desc = e.str("desc");
    c.specials.push_back(std::move(s));
  });
}

ModTurns modTurnsOf(Rec& r, std::string_view key) {
  ModTurns out;
  const Value* o = r.obj(key);
  if (!o) return out;
  for (auto& [k, v] : o->members()) {
    auto id = parseRef(Value(k), Seq::Modifier);
    if (!id || !*id) {
      r.warn(std::string(key) + "." + k, "неверный ключ модификатора (ожидалось вида «m1») — пропущен");
      continue;
    }
    if (!v.isNum() || !std::isfinite(v.asNum())) {
      r.warn(std::string(key) + "." + k, "ожидалось число ходов — пропущено");
      continue;
    }
    out[*id] = int(std::clamp(std::nearbyint(v.asNum()), -1e9, 1e9));
  }
  return out;
}

std::vector<GarrisonEntry> garrisonOf(Rec& r, std::string_view key) {
  std::vector<GarrisonEntry> out;
  r.list(key, [&](Rec& e) {
    GarrisonEntry g;
    g.row = e.ref("row", Seq::Row);
    g.count = e.integer("count", 0);
    out.push_back(g);
  });
  return out;
}

void decConstants(Rec& r, Constants& c) {
  r.list("constants", [&](Rec& e) {
    Constant k;
    k.key = e.str("key");
    k.name = e.str("name");
    k.type = ConstType(e.enumv("type", en(schema::kConstTypes), 0));
    k.builtin = e.flag("builtin", false);
    k.desc = e.str("desc");
    k.num = e.num("num", 0);
    k.res = e.resMap("res");
    k.ess = e.resMap("ess", Seq::Essence);
    k.minRes = e.resMap("minRes");
    k.minEss = e.resMap("minEss", Seq::Essence);
    if (const Value* v = e.arr("values")) {
      const auto& items = v->items();
      for (size_t i = 0; i < items.size(); i++) {
        if (items[i].isStr()) k.values.push_back(items[i].asStr());
        else e.warn("values[" + std::to_string(i) + "]", "ожидалась строка — значение пропущено");
      }
    }
    c.list.push_back(std::move(k));
  });
}

void decGeo(Rec& r, Parts& p) {
  if (const Value* a = r.arr("nodes")) {
    const auto& items = a->items();
    p.nodes.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
      const Value& x = items[i];
      auto where = [i] { return "nodes[" + std::to_string(i) + "]"; };
      if (!(x.isArr() && x.size() == 3 && x[0].isNum() && x[1].isNum() && x[2].isNum())) {
        r.ctx().warn(where(), "ожидалось [id, x, y], получено " + show(x) + " — узел пропущен");
        continue;
      }
      auto id = parseRef(x[0], Seq::Node);
      if (!id || !*id) {
        r.ctx().warn(where(), "неверный ID узла " + show(x[0]) + " — узел пропущен");
        continue;
      }
      Node n;
      n.id = *id;
      n.p = {x[1].asNum(), x[2].asNum()};
      p.nodes.push_back(n);
    }
  }
  if (const Value* a = r.arr("edges")) {
    const auto& items = a->items();
    p.edges.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
      if (!items[i].isObj()) {
        r.ctx().warn("edges[" + std::to_string(i) + "]", "ожидался объект {...} — дуга пропущена");
        continue;
      }
      Rec e(items[i], r.ctx(), &r, "edges", int(i));
      Edge d;
      d.id = itemId(e, Seq::Edge, "edges");
      d.a = e.ref("a", Seq::Node);
      d.b = e.ref("b", Seq::Node);
      d.kind = EdgeKind(e.enumv("kind", en(kEdgeKindE), 0));
      d.pl = e.ref("pl", Seq::Province);
      d.pr = e.ref("pr", Seq::Province);
      d.tl = Terrain(e.enumv("tl", en(kTerrainE), int(Terrain::Land)));
      d.tr = Terrain(e.enumv("tr", en(kTerrainE), int(Terrain::Land)));
      d.pts = e.pts("pts");
      e.done();
      p.edges.push_back(std::move(d));
    }
  }
}

// Плоский массив координат [x1, y1, ...] (для островов воды); ошибки — предупреждение и пропуск точки.
std::vector<Vec2> flatPts(Rec& r, const Value& a, const std::string& where) {
  if (!a.isArr()) {
    r.ctx().warn(where, "ожидался массив координат [x1, y1, ...] — пропущено");
    return {};
  }
  return flatPoints(r.ctx(), a.items(), where);
}

void decMap(Rec& r, Parts& p) {
  if (const Value* a = r.arr("symbols")) {
    const auto& items = a->items();
    p.symbols.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
      const Value& x = items[i];
      auto where = [i] { return "symbols[" + std::to_string(i) + "]"; };
      const bool shapeOk = x.isArr() && (x.size() == 6 || x.size() == 7);
      bool nums = shapeOk;
      for (size_t k = 2; shapeOk && k < x.size(); k++) nums = nums && x[k].isNum();
      if (!nums || !x[1].isStr()) {
        r.ctx().warn(where(), "ожидалось [id, вид, x, y, масштаб, рисунок, z], получено " + show(x) + " — знак пропущен");
        continue;
      }
      auto id = parseRef(x[0], Seq::Symbol);
      if (!id || !*id) {
        r.ctx().warn(where(), "неверный ID знака " + show(x[0]) + " — знак пропущен");
        continue;
      }
      const int kind = schema::findEnum(kSymbolKindE, int(std::size(kSymbolKindE)), x[1].asStr());
      if (kind < 0) {
        r.ctx().warn(where(), "неизвестный вид знака " + show(x[1]) + " (допустимо: " + enumChoices(en(kSymbolKindE)) + ") — знак пропущен");
        continue;
      }
      MapSymbol s;
      s.id = *id;
      s.kind = SymbolKind(kind);
      s.p = {x[2].asNum(), x[3].asNum()};
      s.s = float(x[4].asNum());
      const double v = x[5].asNum();
      s.v = u8(v >= 0 && v <= 255 ? v : 0);
      s.z = x.size() == 7 ? x[6].asNum() : 0.0;
      p.symbols.push_back(s);
    }
  }
  if (const Value* a = r.arr("shapes")) {
    const auto& items = a->items();
    p.shapes.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
      if (!items[i].isObj()) {
        r.ctx().warn("shapes[" + std::to_string(i) + "]", "ожидался объект {...} — фигура пропущена");
        continue;
      }
      Rec e(items[i], r.ctx(), &r, "shapes", int(i));
      MapShape d;
      d.id = itemId(e, Seq::Shape, "shapes");
      d.kind = ShapeKind(e.enumv("kind", en(kShapeKindE), 0));
      d.pts = e.pts("pts");
      if (const Value* h = e.arr("holes")) {
        const auto& hs = h->items();
        for (size_t k = 0; k < hs.size(); k++) d.holes.push_back(flatPts(e, hs[k], e.where("holes") + "[" + std::to_string(k) + "]"));
      }
      d.w = float(e.num("w", 0));
      d.dash = float(e.num("dash", 0));
      e.done();
      p.shapes.push_back(std::move(d));
    }
  }
}

void decProvince(Rec& r, Province& p) {
  p.name = r.str("name");
  p.owner = r.ref("owner", Seq::Faction);
  p.lord = r.ref("lord", Seq::Character);
  p.sea = r.flag("sea", false);
  p.capital = r.str("capital");
  p.size = ProvSize(r.enumv("size", en(schema::kProvSizes), int(ProvSize::Medium)));
  r.get("city");   // прежний формат: тип города задавался вручную, теперь — по населению
  p.resource = r.ref("resource", Seq::Resource);
  p.resourceAmount = r.num("resourceAmount", 0);
  p.garrison = garrisonOf(r, "garrison");
  p.garrisonHeroes = r.refs("garrisonHeroes", Seq::Character);
  p.garrisonLoyalty = r.num("garrisonLoyalty", 100);
  p.contentment = r.num("contentment", 0);
  p.culture = r.ref("culture", Seq::Culture);
  p.religion = r.ref("religion", Seq::Religion);
  r.list("races", [&](Rec& e) {
    RacePop x;
    x.race = e.ref("race", Seq::Race);
    x.pop = countOf(e, "pop");
    p.races.push_back(x);
  });
  r.list("influence", [&](Rec& e) {
    Influence x;
    x.guild = e.ref("guild", Seq::Faction);
    x.pct = e.num("pct", 0);
    p.influence.push_back(x);
  });
  p.hqs = r.refs("hqs", Seq::Faction);
  p.baseTrade = r.num("baseTrade", 0);
  p.localTax = r.num("localTax", 0);
  r.list("buildings", [&](Rec& e) {
    ProvBuilding b;
    b.building = e.ref("building", Seq::Building);
    b.level = e.intv("level", 1);
    b.constructing = e.flag("constructing", false);
    b.left = e.intv("left", 0);
    if (e.has("paid") || e.has("payer")) {
      b.paid = e.resMap("paid");
      b.payer = e.ref("payer", Seq::Faction);
    } else if (b.constructing) {
      // Файл прежней версии: уплаченное не записано — плательщиком считается владелец, возврата при отмене нет.
      b.payer = p.owner;
      e.warnHere("уплаченная стоимость строительства не записана — при отмене стоимость не вернётся");
    }
    b.paidEss = e.resMap("paidEss", Seq::Essence);
    b.idle = e.flag("idle", false);
    b.cycle = e.intv("cycle", 0);
    b.recipe = e.intv("recipe", 0);
    b.mult = e.intv("mult", 1);
    b.race = e.ref("race", Seq::Race);
    b.relics = e.relicStacks("relics");
    p.buildings.push_back(b);
  });
  p.modifiers = r.refs("modifiers", Seq::Modifier);
  p.modTurns = modTurnsOf(r, "modTurns");
  p.buildTurn = r.intv("buildTurn", 0);
  p.occupied = r.flag("occupied", false);
  p.occupier = r.ref("occupier", Seq::Faction);
  p.occGarrison = garrisonOf(r, "occGarrison");
  p.occIdle = r.intv("occIdle", 0);
  r.list("slaves", [&](Rec& e) {
    SlaveWork s;
    s.race = e.ref("race", Seq::Race);
    s.count = e.integer("count", 0);
    p.slaves.push_back(s);
  });
  if (const Value* a = r.arr("arch")) {
    const auto& items = a->items();
    if (items.size() > size_t(kArchSlots)) r.warn("arch", "больше четырёх археологических слотов — лишние отброшены");
    size_t i = 0;
    r.list("arch", [&](Rec& e) {
      ArchSlot s;
      s.site = e.ref("site", Seq::ArchSite);
      s.open = e.flag("open", false);
      s.stage = e.intv("stage", 0);
      s.chest = e.ref("chest", Seq::Chest);
      if (i < size_t(kArchSlots)) p.arch[i] = s;
      i++;
    });
  }
  p.hiddenRelic = r.ref("hiddenRelic", Seq::Relic);
  p.hiddenBy = r.ref("hiddenBy", Seq::Faction);
  p.notes = r.str("notes");
  p.entity = r.str("entity");
}

void decFaction(Rec& r, Faction& f) {
  f.kind = FactionKind(r.enumv("kind", en(schema::kFactionKinds), 0));
  f.name = r.str("name");
  f.color = r.color("color", f.color);
  r.sub("flag", [&](Rec& q) {
    Flag& fl = f.flag;
    fl.image = q.flag("image", false);
    fl.pattern = FlagPattern(q.enumv("pattern", en(schema::kFlagPatterns), 0));
    if (const Value* cols = q.arr("colors")) {
      const auto& items = cols->items();
      if (items.size() > 3) q.warn("colors", "больше трёх цветов — лишние отброшены");
      for (size_t i = 0; i < items.size() && i < 3; i++)
        if (auto c = q.colorOf(items[i], "colors[" + std::to_string(i) + "]")) fl.colors[i] = *c;
    }
    fl.emblem = q.str("emblem");
    fl.emblemColor = q.color("emblemColor", fl.emblemColor);
    fl.png = q.bytes("png");
  });
  f.culture = r.ref("culture", Seq::Culture);
  f.government = r.ref("government", Seq::Government);
  f.religion = r.ref("religion", Seq::Religion);
  f.ruler = r.ref("ruler", Seq::Character);
  f.rulerTitle = r.str("rulerTitle");
  r.list("council", [&](Rec& e) {
    CouncilSeat s;
    s.id = e.ref("id", Seq::Council);
    s.position = e.str("position");
    s.character = e.ref("character", Seq::Character);
    f.council.push_back(std::move(s));
  });
  f.capital = r.ref("capital", Seq::Province);
  r.list("army", [&](Rec& e) {
    ArmyRow a;
    a.id = e.ref("id", Seq::Row);
    a.name = e.str("name");
    a.type = UnitType(e.enumv("type", en(schema::kUnitTypes), 0));
    a.total = countOf(e, "total");
    a.upkeep = e.num("upkeep", 0);
    a.race = e.str("race");
    a.keyRes = e.ref("keyRes", Seq::Resource);
    a.keyPer = e.num("keyPer", 1);
    a.extra = e.resMap("extra");
    a.essence = e.resMap("essence", Seq::Essence);
    a.essUpkeep = e.resMap("essUpkeep", Seq::Essence);
    a.special = e.ref("special", Seq::Special);
    a.merc = e.flag("merc", false);
    a.hire = e.num("hire", 0);
    f.army.push_back(std::move(a));
  });
  r.list("fleet", [&](Rec& e) {
    FleetRow a;
    a.id = e.ref("id", Seq::Row);
    a.name = e.str("name");
    a.type = ShipType(e.enumv("type", en(schema::kShipTypes), int(ShipType::Frigate)));
    a.total = countOf(e, "total");
    a.upkeep = e.num("upkeep", 0);
    a.keyRes = e.ref("keyRes", Seq::Resource);
    f.fleet.push_back(std::move(a));
  });
  f.res = r.resMap("res");
  f.modifiers = r.refs("modifiers", Seq::Modifier);
  f.modTurns = modTurnsOf(r, "modTurns");
  f.tax = r.num("tax", 10);
  f.homeState = r.ref("homeState", Seq::Faction);
  f.stateGuild = r.flag("stateGuild", false);
  f.stateKind = StateKind(r.enumv("stateKind", en(schema::kStateKinds), 0));
  f.mainState = r.flag("mainState", false);
  f.suzerain = r.ref("suzerain", Seq::Faction);
  f.rebelOf = r.ref("rebelOf", Seq::Faction);
  r.list("slaves", [&](Rec& e) {
    SlaveGroup s;
    s.race = e.ref("race", Seq::Race);
    s.count = e.integer("count", 0);
    s.contentment = e.num("contentment", 0);
    f.slaves.push_back(s);
  });
  r.list("forming", [&](Rec& e) {
    Formation q;
    q.row = e.ref("row", Seq::Row);
    q.count = e.integer("count", 0);
    q.left = e.intv("left", 1);
    q.people = e.integer("people", 0);
    q.paid = e.resMap("paid");
    q.paidEss = e.resMap("paidEss", Seq::Essence);
    f.forming.push_back(std::move(q));
  });
  f.tradeFleet = garrisonOf(r, "tradeFleet");
  f.pirateRisk = r.num("pirateRisk", 0);
  f.ess = r.resMap("ess", Seq::Essence);
  f.provisionDebt = r.num("provisionDebt", 0);
  if (const Value* o = r.obj("techs")) {
    for (auto& [k, v] : o->members()) {
      auto id = parseRef(Value(k), Seq::Tech);
      if (!id || !*id || !v.isObj()) {
        r.warn("techs." + k, "неверная запись изучения общей технологии — пропущена");
        continue;
      }
      TechProgress s;
      s.studied = v.get("studied").isBool() && v.get("studied").asBool();
      s.research = v.get("research").isBool() && v.get("research").asBool();
      s.progress = v.get("progress").isNum() ? int(std::clamp(std::nearbyint(v.get("progress").asNum()), 0.0, 1e6)) : 0;
      if (v.get("paid").isObj())
        for (auto& [rk, rv] : v.get("paid").members()) {
          auto rid = parseRef(Value(rk), Seq::Resource);
          if (rid && *rid && rv.isNum()) s.paid[*rid] = rv.asNum();
        }
      for (auto& [mk, mv] : v.members())
        if (mk != "studied" && mk != "research" && mk != "progress" && mk != "paid") r.warn("techs." + k + "." + mk, "неизвестное поле отброшено");
      f.techs[*id] = s;
    }
  }
  r.list("archGroups", [&](Rec& e) {
    ArchGroup g;
    g.id = e.ref("id", Seq::ArchGroup);
    g.name = e.str("name");
    g.exp = e.intv("exp", 0);
    g.modifiers = e.refs("modifiers", Seq::Modifier);
    g.modTurns = modTurnsOf(e, "modTurns");
    g.busy = e.intv("busy", 0);
    g.danger = e.flag("danger", false);
    f.archGroups.push_back(std::move(g));
  });
  f.relics = r.relicStacks("relics");
  f.readyTurn = r.intv("readyTurn", 0);
  f.detectTurn = r.intv("detectTurn", 0);
  r.list("prison", [&](Rec& e) {
    RacePop x;
    x.race = e.ref("race", Seq::Race);
    x.pop = countOf(e, "pop");
    f.prison.push_back(x);
  });
  f.conspiracy = r.flag("conspiracy", false);
  f.notes = r.str("notes");
  f.entity = r.str("entity");
}

void decCharacter(Rec& r, Character& c) {
  c.name = r.str("name");
  c.title = r.str("title");
  c.faction = r.ref("faction", Seq::Faction);
  c.hero = r.flag("hero", false);
  c.upkeep = r.num("upkeep", 0);
  c.portrait = r.bytes("portrait");
  c.modifiers = r.refs("modifiers", Seq::Modifier);
  c.modTurns = modTurnsOf(r, "modTurns");
  c.captor = r.ref("captor", Seq::Faction);
  c.burial = r.ref("burial", Seq::Province);
  c.inventory = r.relicStacks("inventory");
  c.level = r.intv("level", 1);
  c.heroClass = r.ref("heroClass", Seq::HeroClass);
  c.subclass = r.ref("subclass", Seq::SubClass);
  c.talents = r.refs("talents", Seq::Talent);
  c.jailer = r.ref("jailer", Seq::Faction);
  c.notes = r.str("notes");
  c.entity = r.str("entity");
}

void decModifier(Rec& r, Modifier& m) {
  m.name = r.str("name");
  m.icon = r.str("icon", m.icon);
  m.color = r.color("color", m.color);
  m.desc = r.str("desc");
  r.sub("fx", [&](Rec& q) {
    for (int i = 0; i < kFxCount; i++) {
      const char* key = schema::kEffects[i].id;
      if (!q.has(key)) continue;
      double v = q.num(key, std::nan(""));
      if (std::isnan(v)) continue;
      m.fx[size_t(i)] = v;
      m.fxMask |= 1u << i;
    }
  });
  m.targets = r.refs("targets", Seq::Faction);
  m.key = r.str("key");
  m.kind = ModKind(r.enumv("kind", en(schema::kModKinds), 0));
  m.duration = r.intv("duration", 0);
  m.essGen = r.resMap("essGen", Seq::Essence);
  m.resGen = r.resMap("resGen");
}

void decBuilding(Rec& r, Building& b) {
  b.owner = r.ref("owner", Seq::Faction);
  b.name = r.str("name");
  b.icon = r.str("icon", b.icon);
  b.cat = BuildingCat(r.enumv("cat", en(schema::kBuildingCats), int(BuildingCat::Economic)));
  b.desc = r.str("desc");
  r.list("requires", [&](Rec& e) {
    BuildingReq q;
    q.building = e.ref("building", Seq::Building);
    q.level = e.intv("level", 1);
    b.requires_.push_back(q);
  });
  b.techs = r.refs("techs", Seq::Tech);
  if (r.has("levels")) {
    b.levels.clear();
    r.list("levels", [&](Rec& e) {
      BuildingLevel l;
      l.turns = e.intv("turns", 1);
      l.cost = e.resMap("cost");
      l.modifiers = e.refs("modifiers", Seq::Modifier);
      l.desc = e.str("desc");
      l.produce = e.resMap("produce");
      l.essence = e.resMap("essence", Seq::Essence);
      l.essCost = e.resMap("essCost", Seq::Essence);
      l.ships = e.mask("ships", en(schema::kShipTypes));
      l.produceOnRes = e.resMap("produceOnRes");
      l.extractPct = e.num("extractPct", 0);
      l.drainPop = e.integer("drainPop", 0);
      l.drainEss = e.resMap("drainEss", Seq::Essence);
      l.upkeepCut = e.num("upkeepCut", 0);
      b.levels.push_back(std::move(l));
    });
  }
  b.pos = r.pos("pos");
  b.convert = r.flag("convert", false);
  auto recipeOf = [&](Rec& q) {
    Recipe rc;
    rc.name = q.str("name");
    q.list("in", [&](Rec& e) {
      ResAmount x;
      x.res = e.ref("res", Seq::Resource);
      x.amount = e.num("amount", 0);
      rc.in.push_back(x);
    });
    q.list("essIn", [&](Rec& e) {
      ResAmount x;
      x.res = e.ref("essence", Seq::Essence);
      x.amount = e.num("amount", 0);
      rc.essIn.push_back(x);
    });
    // На выходе — массив до трёх ресурсов; прежний формат — один объект.
    auto outOf = [&](Rec& e) {
      ResAmount x;
      x.res = e.ref("res", Seq::Resource);
      x.amount = e.num("amount", 0);
      rc.out.push_back(x);
    };
    if (const Value* ov = q.get("out"); ov && ov->isObj()) q.sub("out", outOf);
    else q.list("out", outOf);
    rc.turns = q.intv("turns", 1);
    b.recipes.push_back(std::move(rc));
  };
  r.list("recipes", recipeOf);
  r.sub("recipe", recipeOf);   // прежний формат: один рецепт
  b.essenceGen = r.flag("essenceGen", false);
  b.specialAccess = r.flag("specialAccess", false);
  b.specials = r.refs("specials", Seq::Special);
  b.relicStore = r.flag("relicStore", false);
  b.healing = r.flag("healing", false);
  b.plague = r.flag("plague", false);
  b.shipyard = r.flag("shipyard", false);
  b.mercenary = r.flag("mercenary", false);
  b.coastal = r.flag("coastal", false);
  b.market = r.flag("market", false);
  b.drain = r.flag("drain", false);
  b.upkeepCut = r.flag("upkeepCut", false);
  b.extract = r.flag("extract", false);
  b.extractGroups = r.refs("extractGroups", Seq::ResGroup);
  b.extractExclude = r.refs("extractExclude", Seq::Resource);
  b.extractOptional = r.flag("extractOptional", false);
  b.stateKinds = r.mask("stateKinds", en(schema::kStateKinds));
  r.list("stateReqs", [&](Rec& e) {
    StateReq q;
    q.building = e.ref("building", Seq::Building);
    q.per = e.intv("per", 4);
    b.stateReqs.push_back(q);
  });
  b.key = r.str("key");
}

void decTech(Rec& r, Tech& t) {
  t.faction = r.ref("faction", Seq::Faction);
  t.name = r.str("name");
  t.desc = r.str("desc");
  t.turns = r.intv("turns", 1);
  t.prereqs = r.refs("prereqs", Seq::Tech);
  t.modifiers = r.refs("modifiers", Seq::Modifier);
  t.studied = r.flag("studied", false);
  t.research = r.flag("research", false);
  t.progress = r.intv("progress", 0);
  t.pos = r.pos("pos");
  t.cost = r.resMap("cost");
  t.paid = r.resMap("paid");
  t.needKeys = r.strs("needKeys");
  t.key = r.str("key");
}

void decArmy(Rec& r, Army& a) {
  a.kind = ArmyKind(r.enumv("kind", en(kArmyKindE), 0));
  a.name = r.str("name");
  a.pos = r.pos("pos");
  r.list("groups", [&](Rec& e) {
    ArmyGroup g;
    g.faction = e.ref("faction", Seq::Faction);
    e.list("units", [&](Rec& u) {
      ArmyUnit x;
      x.row = u.ref("row", Seq::Row);
      x.count = countOf(u, "count");
      g.units.push_back(x);
    });
    g.heroes = e.refs("heroes", Seq::Character);
    a.groups.push_back(std::move(g));
  });
  a.commander = r.ref("commander", Seq::Character);
  a.loyalty = r.num("loyalty", 100);
  a.modifiers = r.refs("modifiers", Seq::Modifier);
  a.modTurns = modTurnsOf(r, "modTurns");
  a.carrier = r.ref("carrier", Seq::Army);
  a.cargo = r.ref("cargo", Seq::Army);
  a.acted = r.intv("acted", 0);
}

void decRoute(Rec& r, Route& x) {
  x.name = r.str("name");
  x.guild = r.ref("guild", Seq::Faction);
  x.pts = r.pts("pts");
  x.color = r.optColor("color");
}

void decDeal(Rec& r, Deal& d) {
  d.kind = DealKind(r.enumv("kind", en(schema::kDealKinds), 0));
  d.a = r.ref("a", Seq::Faction);
  d.b = r.ref("b", Seq::Faction);
  r.list("items", [&](Rec& e) {
    DealItem it;
    it.from = DealSide(e.enumv("from", en(kDealSideE), 0));
    it.res = e.has("res") ? e.ref("res", Seq::Resource) : kGold;
    it.amount = e.num("amount", 0);
    it.mode = DealMode(e.enumv("mode", en(schema::kDealModes), 0));
    it.turns = e.intv("turns", 1);
    it.left = e.intv("left", 0);
    it.kind = DealItemKind(e.enumv("kind", en(schema::kDealItemKinds), 0));
    it.ref = it.kind == DealItemKind::Province ? e.ref("ref", Seq::Province) : it.kind == DealItemKind::Hero ? e.ref("ref", Seq::Character) : (e.get("ref"), Id(0));
    d.items.push_back(it);
  });
  d.turn = r.intv("turn", 1);
  d.status = DealStatus(r.enumv("status", en(kDealStatusE), 0));
  d.note = r.str("note");
}

void decLog(Rec& r, LogEntry& l) {
  l.turn = r.intv("turn", 1);
  l.kind = LogKind(r.enumv("kind", en(schema::kLogKinds), int(LogKind::Note)));
  l.text = r.str("text");
  l.province = r.ref("province", Seq::Province);
  l.army = r.ref("army", Seq::Army);
  l.factions = r.refs("factions", Seq::Faction);
  l.at = r.str("at");
}

void decPlot(Rec& r, Plot& p) {
  p.kind = PlotKind(r.enumv("kind", en(schema::kPlotKinds), 0));
  p.owner = r.ref("owner", Seq::Faction);
  p.target = r.ref("target", Seq::Faction);
  p.name = r.str("name");
  r.list("provinces", [&](Rec& e) {
    PlotProvince x;
    x.province = e.ref("province", Seq::Province);
    x.commitment = e.num("commitment", 0);
    x.sponsored = e.intv("sponsored", 0);
    x.supplies = e.resMap("supplies");
    p.provinces.push_back(std::move(x));
  });
  p.heroes = r.refs("heroes", Seq::Character);
  p.invited = r.refs("invited", Seq::Character);
  p.threats = r.refs("threats", Seq::Character);
  p.leader = r.ref("leader", Seq::Character);
  r.list("hits", [&](Rec& e) {
    PlotHit h;
    h.character = e.ref("character", Seq::Character);
    h.gold = e.num("gold", 0);
    h.turn = e.intv("turn", 0);
    h.guarded = e.flag("guarded", false);
    p.hits.push_back(h);
  });
  p.created = r.intv("created", 1);
  p.found = r.flag("found", false);
  p.stage = r.intv("stage", 0);
  p.revealTurn = r.intv("revealTurn", 0);
  p.alarm = r.flag("alarm", false);
}

// Массив записей таблицы: {"<key>": [...]} (или сразу массив — с предупреждением).
template <class T, class F>
void decTable(const Value& v, FileId f, Ctx& c, std::vector<T>& out, Seq s, F&& fn) {
  const char* key = kFiles[f].key;
  const Value* arr = nullptr;
  std::optional<Rec> top;
  if (v.isArr()) {
    c.warn("", std::string("ожидался объект {\"") + key + "\": [...]}, найден массив — прочитан как список записей");
    arr = &v;
  } else if (v.isObj()) {
    top.emplace(v, c);
    arr = top->arr(key);
    if (!arr && !v.has(key)) c.warn("", std::string("нет массива «") + key + "» — таблица пуста");
  } else {
    c.warn("", std::string("ожидался объект {\"") + key + "\": [...]} — таблица пуста");
  }
  if (arr) {
    const auto& items = arr->items();
    out.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
      if (!items[i].isObj()) {
        c.warn("[" + std::to_string(i) + "]", "запись должна быть объектом {...} — пропущена");
        continue;
      }
      Rec r(items[i], c, int(i));
      T e;
      e.id = r.id(s);
      if (e.id) r.setBase(refStr(s, e.id));
      fn(r, e);
      r.done();
      out.push_back(std::move(e));
    }
  }
  if (top) top->done();
}

}  // namespace

void checkVersion(const Value& v, const std::string& origin, Warnings& warns) {
  if (!v.isObj()) fail(origin + ": ожидался объект JSON с полями format, version, meta, settings");
  const Value& fmt = v.get("format");
  if (fmt.isNull()) warns.push_back({kFiles[F_WORLD].path, "format", "нет поля format — считается «regnum-world»"});
  else if (!fmt.isStr() || fmt.asStr() != kFormat)
    fail(origin + ": это не мир Regnum (поле format = " + show(fmt) + ", ожидалось «regnum-world»)");
  const Value& ver = v.get("version");
  if (ver.isNull()) {
    warns.push_back({kFiles[F_WORLD].path, "version", "нет поля version — считается 1"});
    return;
  }
  if (!ver.isNum() || !std::isfinite(ver.asNum()) || ver.asNum() < 1)
    fail(origin + ": неверная версия формата " + show(ver) + " (ожидалось целое число ≥ 1)");
  double major = std::floor(ver.asNum());
  if (major > kVersion)
    fail("Мир сохранён более новой версией Regnum (формат " + show(Value(major)) + ", эта версия читает формат " +
         std::to_string(kVersion) + "). Обновите редактор.");
}

void decode(const Value& v, FileId f, Parts& p, Warnings& warns) {
  Ctx c{kFiles[f].path, warns};
  p.present[f] = true;
  switch (f) {
    case F_WORLD: {
      if (!v.isObj()) {
        c.warn("", "ожидался объект — взяты значения по умолчанию");
        return;
      }
      Rec r(v, c);
      r.get("format");
      r.get("version");
      if (!r.has("meta")) c.warn("meta", "нет раздела meta — взяты значения по умолчанию");
      r.sub("meta", [&](Rec& m) { decMeta(m, p.meta); });
      r.sub("settings", [&](Rec& s) { decSettings(s, p.settings); });
      r.done();
      return;
    }
    case F_CATALOGS: {
      if (!v.isObj()) {
        c.warn("", "ожидался объект со списками справочников — справочники пусты");
        return;
      }
      Rec r(v, c);
      decCatalogs(r, p.catalogs);
      r.done();
      return;
    }
    case F_GEO: {
      if (!v.isObj()) {
        c.warn("", "ожидался объект {\"nodes\": [...], \"edges\": [...]} — геометрия пуста");
        return;
      }
      Rec r(v, c);
      decGeo(r, p);
      r.done();
      return;
    }
    case F_MAP: {
      if (!v.isObj()) {
        c.warn("", "ожидался объект {\"symbols\": [...], \"shapes\": [...]} — объектов карты нет");
        return;
      }
      Rec r(v, c);
      decMap(r, p);
      r.done();
      return;
    }
    case F_RELATIONS: {
      const Value* rel = nullptr;
      std::optional<Rec> top;
      if (v.isObj()) {
        top.emplace(v, c);
        rel = top->obj("relations");
      } else {
        c.warn("", "ожидался объект {\"relations\": {...}} — отношений нет");
      }
      if (rel) {
        for (auto& [k, x] : rel->members()) {
          size_t bar = k.find('|');
          std::optional<Id> a, b;
          if (bar != std::string::npos) {
            a = parseRef(Value(std::string_view(k).substr(0, bar)), Seq::Faction);
            b = parseRef(Value(std::string_view(k).substr(bar + 1)), Seq::Faction);
          }
          if (!a || !b || !*a || !*b) {
            c.warn(k, "неверный ключ пары (ожидалось вида «f1|f2») — отношение пропущено");
            continue;
          }
          if (*a == *b) {
            c.warn(k, "отношение фракции к самой себе — пропущено");
            continue;
          }
          if (!x.isObj()) {
            c.warn(k, "ожидался объект {\"v\": число, \"s\": состояние} — отношение пропущено");
            continue;
          }
          Rec r(x, c);
          r.setBase(k);
          Relation rr;
          rr.v = r.num("v", 0);
          rr.s = RelStatus(r.enumv("s", en(schema::kRelStatus), int(RelStatus::Unknown)));
          r.done();
          u64 key = relKey(*a, *b);
          if (p.relations.count(key)) c.warn(k, "пара указана повторно — взято последнее значение");
          p.relations[key] = rr;
        }
      }
      if (top) top->done();
      return;
    }
    case F_PROVINCES: decTable(v, f, c, p.provinces, Seq::Province, decProvince); return;
    case F_FACTIONS: decTable(v, f, c, p.factions, Seq::Faction, decFaction); return;
    case F_CHARACTERS: decTable(v, f, c, p.characters, Seq::Character, decCharacter); return;
    case F_MODIFIERS: decTable(v, f, c, p.modifiers, Seq::Modifier, decModifier); return;
    case F_BUILDINGS: decTable(v, f, c, p.buildings, Seq::Building, decBuilding); return;
    case F_TECHS: decTable(v, f, c, p.techs, Seq::Tech, decTech); return;
    case F_ARMIES: decTable(v, f, c, p.armies, Seq::Army, decArmy); return;
    case F_ROUTES: decTable(v, f, c, p.routes, Seq::Route, decRoute); return;
    case F_DEALS: decTable(v, f, c, p.deals, Seq::Deal, decDeal); return;
    case F_LOG: decTable(v, f, c, p.log, Seq::Log, decLog); return;
    case F_PLOTS: decTable(v, f, c, p.plots, Seq::Plot, decPlot); return;
    case F_CONSTANTS: {
      if (!v.isObj()) {
        c.warn("", "ожидался объект {\"constants\": [...]} — констант нет");
        return;
      }
      Rec r(v, c);
      decConstants(r, p.constants);
      r.done();
      return;
    }
    default: return;
  }
}

json::Value parseFile(std::string_view text, const std::string& origin) {
  if (text.size() >= 3 && u8(text[0]) == 0xEF && u8(text[1]) == 0xBB && u8(text[2]) == 0xBF) text.remove_prefix(3);
  try {
    return json::parse(text);
  } catch (const json::ParseError& e) {
    fail(origin + ": " + e.what());
  }
}

}  // namespace rg::io::detail
