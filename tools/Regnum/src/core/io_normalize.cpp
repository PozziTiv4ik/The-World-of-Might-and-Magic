// Regnum — нормализация мира: пределы значений, висячие ссылки, повторы, инварианты схемы.
// Работает на любом World (прочитанном из файлов, исправленном вручную или агентом): после неё
// редактор и правила могут полагаться на целостность ссылок. Здесь — общие помощники Norm, базовые записи,
// счётчики ID, природа героев и порядок шагов; шаги по областям — core/io_normalize_*.cpp.
#include "core/content.h"
#include "core/io_normalize.h"

namespace rg::io::norm {

// Новый ID последовательности (не меньше уже занятых).
Id Norm::fresh(Seq s, u32 maxInData) {
  u32& c = seq[size_t(s)];
  c = std::max(c, maxInData);
  return ++c;
}

bool Norm::hasCat(Seq s, Id id) const {
  if (const int k = catOfSeq[size_t(s)]; k >= 0) return cat[k].count(id) > 0;
  switch (s) {
    case Seq::Relic: return relicIds.count(id) > 0;
    case Seq::Special: return specialIds.count(id) > 0;
    case Seq::ResGroup: return groupIds.count(id) > 0;
    case Seq::RelicGroup: return relicGroupIds.count(id) > 0;
    case Seq::HeroClass: return classIds.count(id) > 0;
    case Seq::ArchSite: return siteIds.count(id) > 0;
    case Seq::Chest: return chestIds.count(id) > 0;
    default: return false;
  }
}

bool Norm::exists(Seq s, Id id) const {
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

// Одиночная ссылка: несуществующая обнуляется.
bool Norm::fixRef(Id& id, Seq s, FileId f, const std::string& base, const char* field) {
  if (!id || exists(s, id)) return false;
  warn(f, base + "." + field, std::string("нет ") + genitive(s) + " " + refStr(s, id) + " — ссылка удалена");
  id = 0;
  return true;
}

bool Norm::fixRefList(std::vector<Id>& v, Seq s, FileId f, const std::string& base, const char* field) {
  return fixRefList(v, s, f, base, field, [&](Id id) { return exists(s, id); });
}

bool Norm::fixStacks(std::vector<RelicStack>& v, FileId f, const std::string& base, const char* field) {
  std::vector<RelicStack> keep;
  bool ch = false;
  for (const RelicStack& st : v) {
    const Relic* r = w().relic(st.relic);
    if (!r || !relicIds.count(st.relic)) {
      warn(f, base + "." + field, "нет реликвии " + refStr(Seq::Relic, st.relic) + " — удалена");
      ch = true;
      continue;
    }
    i64 n = std::clamp<i64>(st.count, 1, kMaxCount);
    if (n != st.count) ch = true;
    if (r->unique()) {
      if (relicTaken.count(st.relic)) {
        warn(f, base + "." + field, refStr(Seq::Relic, st.relic) + ": уникальная реликвия уже в другом месте — удалена");
        ch = true;
        continue;
      }
      if (n != 1) {
        warn(f, base + "." + field, refStr(Seq::Relic, st.relic) + ": уникальная реликвия — одна штука");
        n = 1;
        ch = true;
      }
      relicTaken.insert(st.relic);
    }
    auto it = std::find_if(keep.begin(), keep.end(), [&](const RelicStack& x) { return x.relic == st.relic; });
    if (it != keep.end()) {
      it->count = std::min(kMaxCount, it->count + n);
      ch = true;
    } else {
      keep.push_back(RelicStack{st.relic, n});
    }
  }
  if (ch) v = std::move(keep);
  return ch;
}

bool Norm::fixFinite(double& v, FileId f, const std::string& base, const char* field) {
  if (std::isfinite(v)) return false;
  warn(f, base + "." + field, "значение не является числом — взят 0");
  v = 0;
  return true;
}

// Срок модификаторов: только модификаторы из списка сущности, 1…kMaxTurns ходов.
bool Norm::fixModTurns(ModTurns& t, const std::vector<Id>& mods, FileId f, const std::string& base) {
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

bool Norm::fixPoint(Vec2& p, bool map, FileId f, const std::string& base, const char* field) {
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

bool Norm::fixPoints(std::vector<Vec2>& pts, FileId f, const std::string& base, const char* field) {
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
void Norm::meta() {
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

// Карта «ссылка → количество ≥ 0»: только существующие записи справочника s (остальные — «… — <removed>»).
bool Norm::fixAmounts(std::map<Id, double>& m, Seq s, const std::unordered_set<Id>& ids, FileId f, const std::string& base, const char* field,
                const char* removed) {
  bool ch = false;
  for (auto it = m.begin(); it != m.end();) {
    if (!ids.count(it->first)) {
      warn(f, base + "." + field, std::string("нет ") + genitive(s) + " " + refStr(s, it->first) + " — " + removed);
      it = m.erase(it);
      ch = true;
      continue;
    }
    ch |= fixNum(it->second, 0.0, 1e12, 0.0, f, base + "." + field, refStr(s, it->first).c_str());
    ++it;
  }
  return ch;
}

// Запасы «ссылка → количество» любого знака: только существующие записи, конечные числа.
bool Norm::fixAmountsSigned(std::map<Id, double>& m, Seq s, const std::unordered_set<Id>& ids, FileId f, const std::string& base, const char* field) {
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
void Norm::content() {
  std::vector<std::string> notes = content::seed(tx);
  for (int i = 0; i < kSeqCount; i++) seq[size_t(i)] = std::max(seq[size_t(i)], w().meta->seq[size_t(i)]);
  if (notes.empty()) return;
  warn(F_CATALOGS, "", "мир дополнен: " + join(notes, "; "));
  catalogSets();
}

Id Norm::remapRow(Id faction, bool fleet, Id row) const {
  auto it = rowRemap.find({faction, fleet, row});
  return it == rowRemap.end() ? row : it->second;
}

// Счётчики ID не меньше наибольших занятых ID.
void Norm::counters() {
  std::array<u32, kSeqCount> maxId{};
  auto upd = [&](Seq s, Id id) { maxId[size_t(s)] = std::max(maxId[size_t(s)], id); };
  const World& x = w();
#define RG_UPD_TABLE(T, field, name, Name, bit, seq, ...) x.field.each([&](const T& e) { upd(seq, e.id); });
  RG_WORLD_TABLES(RG_UPD_TABLE)
#undef RG_UPD_TABLE
  x.factions.each([&](const Faction& e) {
    for (auto& r : e.army) upd(Seq::Row, r.id);
    for (auto& r : e.fleet) upd(Seq::Row, r.id);
    for (auto& s : e.council) upd(Seq::Council, s.id);
    for (auto& g : e.archGroups) upd(Seq::ArchGroup, g.id);
  });
  const Catalogs& c = *x.catalogs;
  for (const schema::CatalogListInfo& L : schema::kCatalogLists)
    for (auto& i : c.*L.items) upd(L.seq, i.id);
  for (auto& i : c.relics) upd(Seq::Relic, i.id);
  for (auto& i : c.specials) upd(Seq::Special, i.id);
  for (auto& i : c.resGroups) upd(Seq::ResGroup, i.id);
  for (auto& i : c.relicGroups) upd(Seq::RelicGroup, i.id);
  for (auto& i : c.archSites) upd(Seq::ArchSite, i.id);
  for (auto& i : c.chests) upd(Seq::Chest, i.id);
  for (auto& k : c.classes) {
    upd(Seq::HeroClass, k.id);
    for (auto& sc : k.subclasses) {
      upd(Seq::SubClass, sc.id);
      for (auto& t : sc.talents) upd(Seq::Talent, t.id);
    }
  }
  for (int i = 0; i < kSeqCount; i++) {
    if (seq[size_t(i)] < maxId[size_t(i)]) {
      warn(F_WORLD, std::string("meta.seq.") + schema::kSeqs[i].name, "счётчик " + std::to_string(seq[size_t(i)]) + " меньше наибольшего ID " +
                                                        std::to_string(maxId[size_t(i)]) + " — поднят");
      seq[size_t(i)] = maxId[size_t(i)];
    }
  }
  if (seq != x.meta->seq) tx.meta().seq = seq;
}

// Героям государств без модификатора природы — природа по виду государства. Один раз:
// только пока в мире нет записи этого встроенного модификатора (мир сохранён до появления видов государств);
// после — природу снимают и меняют по правилам модификаторов. Выполняется после counters (выдаются новые ID).
void Norm::natures() {
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

void Norm::run() {
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

}  // namespace rg::io::norm

namespace rg::io {

u32 normalize(World& w, Warnings& warnings) {
  Tx tx(w);
  norm::Norm n(tx, warnings);
  n.run();
  World out = std::move(tx).finish();
  u32 mask = World::diff(w, out);
  w = std::move(out);
  return mask;
}

}  // namespace rg::io
