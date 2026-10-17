// Regnum — реализация модели мира, транзакций и хранилища.
#include "core/world.h"

#include "core/content.h"

namespace rg {

// ---------------------------------------------------------------- World
Relation World::relation(Id a, Id b) const {
  if (a == b) return Relation{100, RelStatus::Alliance};
  // Войска без государства враждебны всем.
  const Faction* fa = factions.get(a);
  const Faction* fb = factions.get(b);
  if ((fa && fa->isWild()) || (fb && fb->isWild())) return Relation{-100, RelStatus::War};
  auto it = relations->find(relKey(a, b));
  return it == relations->end() ? Relation{} : it->second;
}

int talentRank(const std::vector<Id>& learned, Id talent) { return int(std::count(learned.begin(), learned.end(), talent)); }

int talentSpent(const SubClass& sc, const std::vector<Id>& learned) {
  int s = 0;
  for (Id id : learned)
    if (const Talent* t = sc.talent(id)) s += t->cost;
  return s;
}

int talentSpentAbove(const SubClass& sc, const std::vector<Id>& learned, int row) {
  int s = 0;
  for (Id id : learned)
    if (const Talent* t = sc.talent(id); t && t->row < row) s += t->cost;
  return s;
}

std::vector<Id> lawfulTalents(const SubClass& sc, const std::vector<Id>& learned, int points) {
  std::vector<Id> keep;
  std::map<Id, int> rank;
  std::map<int, int> rowSpent;   // ярус → вложено очков
  int spent = 0;
  const int tier = std::max(1, sc.tierPoints);
  for (Id id : learned) {
    const Talent* t = sc.talent(id);
    if (!t || rank[id] >= std::max(1, t->ranks)) continue;
    if (t->prereq) {
      const Talent* p = sc.talent(t->prereq);
      if (!p || rank[t->prereq] < std::max(1, p->ranks)) continue;
    }
    int above = 0;
    for (auto& [row, v] : rowSpent)
      if (row < t->row) above += v;
    if (above < t->row * tier) continue;
    if (spent + t->cost > points) continue;
    keep.push_back(id);
    rank[id]++;
    rowSpent[t->row] += t->cost;
    spent += t->cost;
  }
  return keep;
}

std::string World::factionName(Id id) const { const Faction* f = faction(id); return f ? nameOr(f->name) : "\xE2\x80\x94"; }
std::string World::provinceName(Id id) const { const Province* p = province(id); return p ? nameOr(p->name) : "\xE2\x80\x94"; }
std::string World::characterName(Id id) const { const Character* c = character(id); return c ? nameOr(c->name, kNameless) : "\xE2\x80\x94"; }

u32 World::diff(const World& a, const World& b) {
  u32 m = 0;
  if (a.meta != b.meta) m |= TB_META;
  if (a.settings != b.settings) m |= TB_SETTINGS;
  if (a.catalogs != b.catalogs) m |= TB_CATALOGS;
  if (a.constants != b.constants) m |= TB_CONSTANTS;
  if (a.relations != b.relations) m |= TB_RELATIONS;
#define RG_DIFF(T, field, name, Name, bit, ...) \
  if (!a.field.same(b.field)) m |= bit;
  RG_WORLD_TABLES(RG_DIFF)
#undef RG_DIFF
  return m;
}

World newWorld(const std::string& name) {
  World w;
  auto meta = std::make_shared<Meta>();
  meta->name = name.empty() ? "Новый мир" : name;
  meta->createdAt = meta->updatedAt = nowIso();
  auto cat = std::make_shared<Catalogs>();
  auto item = [](Id id, const char* n, u32 color, const char* icon, bool builtin = false, const char* key = "") {
    CatalogItem c; c.id = id; c.name = n; c.color = Color::hex(color); c.icon = icon; c.builtin = builtin; c.key = key; return c;
  };
  cat->resources = {item(kGold, "Золото", 0xe2b33c, "coins", true)};
  meta->seq[int(Seq::Resource)] = 1;
  const char* govs[] = {"Монархия", "Империя", "Республика", "Теократия", "Олигархия", "Племенной союз"};
  for (int i = 0; i < 6; i++) cat->governments.push_back(item(Id(i + 1), govs[i], 0x888888, ""));
  meta->seq[int(Seq::Government)] = 6;
  w.meta = meta;
  w.catalogs = cat;
  // Базовые записи: должности, эссенции, группы ресурсов, религии, особый отряд и его постройка.
  Tx tx(w);
  content::seed(tx);
  return std::move(tx).finish();
}

// ---------------------------------------------------------------- Tx
#define RG_TX_INIT(T, field, ...) , field##_(&w_.field)
Tx::Tx(const World& base) : w_(base) RG_WORLD_TABLES(RG_TX_INIT) {}
#undef RG_TX_INIT

Meta& Tx::meta() {
  if (!metaOwned_) { meta_ = std::make_shared<Meta>(*w_.meta); w_.meta = meta_; metaOwned_ = true; touched_ |= TB_META; }
  return *meta_;
}
Settings& Tx::settings() {
  if (!settingsOwned_) { settings_ = std::make_shared<Settings>(*w_.settings); w_.settings = settings_; settingsOwned_ = true; touched_ |= TB_SETTINGS; }
  return *settings_;
}
Catalogs& Tx::catalogs() {
  if (!catalogsOwned_) { catalogs_ = std::make_shared<Catalogs>(*w_.catalogs); w_.catalogs = catalogs_; catalogsOwned_ = true; touched_ |= TB_CATALOGS; }
  return *catalogs_;
}
Constants& Tx::constants() {
  if (!constantsOwned_) { constants_ = std::make_shared<Constants>(*w_.constants); w_.constants = constants_; constantsOwned_ = true; touched_ |= TB_CONSTANTS; }
  return *constants_;
}
RelMap& Tx::relations() {
  if (!relOwned_) { rel_ = std::make_shared<RelMap>(*w_.relations); w_.relations = rel_; relOwned_ = true; touched_ |= TB_RELATIONS; }
  return *rel_;
}

Id Tx::nextId(Seq s) { return ++meta().seq[int(s)]; }

void Tx::setRelation(Id a, Id b, Relation r) {
  if (a == 0 || b == 0 || a == b) return;
  r.v = clamp(r.v, -100.0, 100.0);
  relations()[relKey(a, b)] = r;
}

#define RG_TX_TABLE(T, field, name, Name, BIT, SEQ, LABEL)                      \
  T& Tx::name(Id id) {                                                          \
    T* p = field##_.mut(id);                                                    \
    if (!p) fail(std::string(LABEL) + " не найден(а): #" + std::to_string(id)); \
    touched_ |= BIT;                                                            \
    return *p;                                                                  \
  }                                                                             \
  T& Tx::add(T v) {                                                             \
    if (v.id == 0) v.id = nextId(SEQ);                                          \
    else if (v.id > meta().seq[int(SEQ)]) meta().seq[int(SEQ)] = v.id;          \
    touched_ |= BIT;                                                            \
    return field##_.put(std::move(v));                                          \
  }                                                                             \
  void Tx::erase##Name(Id id) {                                                 \
    if (field##_.erase(id)) touched_ |= BIT;                                    \
  }
RG_WORLD_TABLES(RG_TX_TABLE)
#undef RG_TX_TABLE

void Tx::replaceWorld(const World& w) {
  w_ = w;
  metaOwned_ = settingsOwned_ = catalogsOwned_ = relOwned_ = constantsOwned_ = false;
  meta_.reset();
  settings_.reset();
  catalogs_.reset();
  constants_.reset();
  rel_.reset();
#define RG_TX_RESET(T, field, ...) field##_ = TableEdit<T>(&w_.field);
  RG_WORLD_TABLES(RG_TX_RESET)
#undef RG_TX_RESET
  touched_ = TB_ALL;
}

World Tx::finish() && { return std::move(w_); }

// ---------------------------------------------------------------- Store
Store::Store() : cur_(newWorld("")), saved_(cur_) {}

void Store::commit(std::string_view label, Tx&& tx, const TxOptions& opt) {
  u32 touched = tx.touched();
  if (!touched) return;
  World next = std::move(tx).finish();
  u32 changed = World::diff(cur_, next);
  if (!changed) return;
  World before = cur_;
  cur_ = std::move(next);
  version_++;
  if (opt.history) {
    double now = nowSeconds();
    if (!opt.coalesce.empty() && !undo_.empty() && undo_.back().coalesce == opt.coalesce && now - undo_.back().time < opt.coalesceSec) {
      undo_.back().after = cur_;
      undo_.back().time = now;
    } else {
      undo_.push_back(Entry{std::string(label), before, cur_, opt.coalesce, now});
      if (undo_.size() > limit) undo_.erase(undo_.begin(), undo_.begin() + long(undo_.size() - limit));
    }
    redo_.clear();
  }
  Change c;
  c.kind = Change::Commit;
  c.label = std::string(label);
  c.tables = changed;
  c.before = &before;
  c.after = &cur_;
  notify(c);
}

bool Store::undo() {
  if (undo_.empty()) return false;
  Entry e = std::move(undo_.back());
  undo_.pop_back();
  World before = cur_;
  cur_ = e.before;
  version_++;
  Change c;
  c.kind = Change::Undo;
  c.label = e.label;
  c.tables = World::diff(before, cur_);
  c.before = &before;
  c.after = &cur_;
  redo_.push_back(std::move(e));
  notify(c);
  return true;
}

bool Store::redo() {
  if (redo_.empty()) return false;
  Entry e = std::move(redo_.back());
  redo_.pop_back();
  World before = cur_;
  cur_ = e.after;
  version_++;
  Change c;
  c.kind = Change::Redo;
  c.label = e.label;
  c.tables = World::diff(before, cur_);
  c.before = &before;
  c.after = &cur_;
  e.coalesce.clear();
  undo_.push_back(std::move(e));
  notify(c);
  return true;
}

void Store::endCoalesce() { if (!undo_.empty()) undo_.back().coalesce.clear(); }
void Store::clearHistory() { undo_.clear(); redo_.clear(); }

void Store::replace(World w, std::string_view label, bool mark) {
  World before = cur_;
  cur_ = std::move(w);
  version_++;
  undo_.clear();
  redo_.clear();
  if (mark) saved_ = cur_;
  Change c;
  c.kind = Change::Load;
  c.label = std::string(label);
  c.tables = TB_ALL;
  c.before = &before;
  c.after = &cur_;
  notify(c);
}

void Store::markSaved() { saved_ = cur_; }

int Store::subscribe(std::function<void(const Change&)> fn) {
  int t = nextSub_++;
  subs_.emplace_back(t, std::move(fn));
  return t;
}

void Store::unsubscribe(int token) {
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(), [&](auto& p) { return p.first == token; }), subs_.end());
}

void Store::notify(const Change& c) {
  auto copy = subs_;
  for (auto& [t, fn] : copy) {
    try { fn(c); } catch (const std::exception& e) { logError("store subscriber: %s", e.what()); }
  }
}

}  // namespace rg
