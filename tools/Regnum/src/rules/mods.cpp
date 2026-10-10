// Regnum — источники модификаторов и общие помощники правил (тексты, проверки, запасы).
#include "core/arch.h"
#include "rules/internal.h"

namespace rg::rules {

namespace detail {

// ---------------------------------------------------------------- тексты
std::string amount(double v) { return fmtNum(v, 3); }
std::string amountOf(Id res, double v) { return res == kGold ? fmtGold(v) : amount(v); }

std::string resName(const World& w, Id res) {
  const CatalogItem* c = w.resource(res);
  if (!c) return "ресурс #" + std::to_string(res);
  return c->name.empty() ? "Без названия" : c->name;
}
std::string facName(const World& w, Id f) { return q(w.factionName(f)); }
std::string provName(const World& w, Id p) { return q(w.provinceName(p)); }
std::string armyName(const World& w, Id a) {
  const Army* x = w.army(a);
  if (!x) return q("—");
  if (!x->name.empty()) return q(x->name);
  return q(x->isFleet() ? "Флот" : "Войско");
}
std::string buildingName(const World& w, Id b) {
  const Building* x = w.building(b);
  return q(x ? (x->name.empty() ? "Без названия" : x->name) : "—");
}
std::string techName(const World& w, Id t) {
  const Tech* x = w.tech(t);
  return q(x ? (x->name.empty() ? "Без названия" : x->name) : "—");
}

std::string uniqueName(const std::vector<std::string>& taken, const std::string& base) {
  auto used = [&](const std::string& n) {
    std::string k = utf8::searchKey(n);
    for (auto& t : taken)
      if (utf8::searchKey(t) == k) return true;
    return false;
  };
  if (!used(base)) return base;
  for (int i = 2;; i++) {
    std::string n = base + " " + std::to_string(i);
    if (!used(n)) return n;
  }
}

// ---------------------------------------------------------------- проверки
const Faction& needFaction(const World& w, Id id) {
  const Faction* f = w.faction(id);
  if (!f) fail(id ? "Фракция не найдена" : "Не выбрана фракция");
  return *f;
}
const Faction& needState(const World& w, Id id) {
  const Faction* f = w.faction(id);
  if (!f) fail(id ? "Государство не найдено" : "Не выбрано государство");
  if (!f->isState()) fail(facName(w, id) + " — торговая гильдия, а нужно государство");
  return *f;
}
const Faction& needGuild(const World& w, Id id) {
  const Faction* f = w.faction(id);
  if (!f) fail(id ? "Гильдия не найдена" : "Не выбрана гильдия");
  if (!f->isGuild()) fail(facName(w, id) + " — государство, а нужна торговая гильдия");
  return *f;
}
const Province& needProvince(const World& w, Id id) {
  const Province* p = w.province(id);
  if (!p) fail(id ? "Провинция не найдена" : "Не выбрана провинция");
  return *p;
}
const Character& needCharacter(const World& w, Id id) {
  const Character* c = w.character(id);
  if (!c) fail(id ? "Персонаж не найден" : "Не выбран персонаж");
  return *c;
}
const Army& needArmy(const World& w, Id id) {
  const Army* a = w.army(id);
  if (!a) fail(id ? "Войско не найдено" : "Не выбрано войско");
  return *a;
}
const Building& needBuilding(const World& w, Id id) {
  const Building* b = w.building(id);
  if (!b) fail(id ? "Постройка не найдена" : "Не выбрана постройка");
  return *b;
}
const Tech& needTech(const World& w, Id id) {
  const Tech* t = w.tech(id);
  if (!t) fail(id ? "Технология не найдена" : "Не выбрана технология");
  return *t;
}
void needFinite(double v, const char* what) {
  if (!std::isfinite(v)) fail(std::string(what) + ": недопустимое число");
}

// ---------------------------------------------------------------- запасы
void addStock(Faction& f, Id res, double delta) {
  double& v = f.res[res];
  v += delta;
  if (std::fabs(v) < 1e-9) v = 0;  // без «−0» и хвостов округления
}

std::shared_ptr<const geo::FaceSet> facesFor(const Tx& tx) {
  return (tx.touched() & TB_GEO) ? geo::buildFaces(tx.w()) : geo::faces(tx.w());
}

// ---------------------------------------------------------------- эффекты
SourceIndex::SourceIndex(const World& w) {
  w.techs.each([&](const Tech& t) {
    if (t.studied && t.faction && !t.modifiers.empty()) studied[t.faction].push_back(&t);
  });
  // Общие технологии, изученные фракцией (каждая фракция изучает общее дерево отдельно).
  w.factions.each([&](const Faction& f) {
    for (auto& [tid, s] : f.techs) {
      if (!s.studied) continue;
      const Tech* t = w.tech(tid);
      if (t && t->faction == 0 && !t->modifiers.empty()) studied[f.id].push_back(t);
    }
  });
  w.factions.each([&](const Faction& f) {
    if (!f.isState()) return;
    auto a = autoModifiers(w, f.id);
    if (!a.empty()) autos[f.id] = std::move(a);
  });
}
const std::vector<const Tech*>* SourceIndex::of(Id faction) const {
  auto it = studied.find(faction);
  return it == studied.end() ? nullptr : &it->second;
}
const std::vector<AutoMod>* SourceIndex::autosOf(Id faction) const {
  auto it = autos.find(faction);
  return it == autos.end() ? nullptr : &it->second;
}

namespace {

// Сумматор эффектов. Level::Province — только локальные эффекты; Faction — глобальные (и эффекты войск: у
// государства они действуют на все его войска; и эффекты археологических групп); Army — только эффекты войск;
// Arch — только эффекты археологических групп.
struct FxSum {
  enum class Level : u8 { Province, Faction, Army, Arch };
  const World& w;
  Level level;
  Id self;
  Effects e;

  bool counts(int i) const {
    const auto& info = schema::kEffects[i];
    switch (level) {
      case Level::Province: return info.local;
      case Level::Faction: return !info.local;
      case Level::Army: return info.army;
      case Level::Arch: return info.arch;
    }
    return false;
  }

  void addMod(EffectSource::Kind kind, Id id, Id modId, const Modifier* m, std::string key = {}, double scale = 1) {
    if (!m) return;
    bool any = false;
    for (int i = 0; i < kFxCount; i++) {
      if (!m->has(Fx(i)) || !counts(i)) continue;
      double v = m->fx[size_t(i)] * scale;
      if (!std::isfinite(v)) continue;
      e.v[size_t(i)] += v;
      any = true;
      if (Fx(i) == Fx::DiplomacyPerTurn) {
        std::vector<Id> seen;
        for (Id t : m->targets) {
          if (t == 0 || t == self || contains(seen, t) || !w.faction(t)) continue;
          seen.push_back(t);
          e.diplomacy[t] += v;
        }
      }
    }
    if (any) e.sources.push_back(EffectSource{kind, id, modId, std::move(key)});
  }
  void add(EffectSource::Kind kind, Id id, Id mod) { addMod(kind, id, mod, w.modifier(mod)); }

  // Модификаторы фракции, её изученных технологий и модификаторы, которые ставятся сами (совет, голод, должности).
  void faction(const SourceIndex& si, const Faction& f, EffectSource::Kind modKind) {
    for (Id m : f.modifiers) add(modKind, f.id, m);
    if (auto* ts = si.of(f.id))
      for (const Tech* t : *ts)
        for (Id m : t->modifiers) add(EffectSource::Tech, t->id, m);
    if (auto* as = si.autosOf(f.id))
      for (const AutoMod& a : *as) addMod(EffectSource::Auto, f.id, a.modifier, a.m, a.key, a.scale);
  }

  // Постройки провинции: набор модификаторов текущего достроенного уровня.
  void buildings(const Province& p) {
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      int lvl = pb.builtLevel();
      if (!b || lvl < 1 || lvl > int(b->levels.size())) continue;
      for (Id m : b->levels[size_t(lvl - 1)].modifiers) add(EffectSource::Building, b->id, m);
    }
  }
};

}  // namespace

Effects provinceFx(const World& w, const SourceIndex& si, const Province& p) {
  FxSum s{w, FxSum::Level::Province, 0, {}};
  if (p.sea) return s.e;  // морские провинции не участвуют в расчётах
  // «Здание чумы» в провинции меняет действие «Чумы»: прирост населения +2,5 % (ТЗ «Доработки №3», п.5).
  const bool plagueBld = hasBuildingRole(w, p.id, BuildingFlag::Plague);
  for (Id m : p.modifiers) {
    const Modifier* x = w.modifier(m);
    if (x && plagueBld && x->key == schema::mod::Plague) {
      Modifier alt = *x;
      alt.fx[size_t(Fx::PopGrowthPct)] = schema::kPlagueBuildingGrowth;
      alt.fxMask |= 1u << int(Fx::PopGrowthPct);
      s.addMod(EffectSource::Province, p.id, m, &alt);
      continue;
    }
    s.add(EffectSource::Province, p.id, m);
  }
  for (const AutoMod& a : autoProvinceModifiers(w, p.id)) s.addMod(EffectSource::Auto, p.id, a.modifier, a.m, a.key);
  if (const Faction* o = w.faction(p.owner); o && o->isState()) s.faction(si, *o, EffectSource::Faction);
  s.buildings(p);
  for (Id g : p.hqs)
    if (const Faction* gf = w.faction(g); gf && gf->isGuild()) s.faction(si, *gf, EffectSource::Guild);
  return std::move(s.e);
}

Effects factionFx(const World& w, const SourceIndex& si, const Faction& f, const std::vector<const Province*>& owned) {
  FxSum s{w, FxSum::Level::Faction, f.id, {}};
  s.faction(si, f, EffectSource::Faction);
  if (f.isState())
    for (const Province* p : owned)
      if (!p->sea && p->owner == f.id) s.buildings(*p);
  return std::move(s.e);
}

Effects armyFx(const World& w, const Army& a, const Effects* leaderFx) {
  FxSum s{w, FxSum::Level::Army, a.leader(), {}};
  for (Id m : a.modifiers) s.add(EffectSource::Army, a.id, m);
  // Эффекты войск у модификаторов героев войска (и изученных ими талантов).
  for (const ArmyGroup& g : a.groups)
    for (Id h : g.heroes)
      if (const Character* c = w.character(h)) {
        for (Id m : c->modifiers) s.add(EffectSource::Army, a.id, m);
        for (Id m : talentModifiers(w, h)) s.add(EffectSource::Army, a.id, m);
      }
  // Эффекты войск государства-лидера (его модификаторы, технологии, совет, голод).
  Effects lf = leaderFx ? *leaderFx : factionEffects(w, a.leader());
  for (int i = 0; i < kFxCount; i++)
    if (schema::kEffects[i].army && lf.v[size_t(i)] != 0) s.e.v[size_t(i)] += lf.v[size_t(i)];
  for (const EffectSource& src : lf.sources) {
    const Modifier* m = src.modifier ? w.modifier(src.modifier) : (src.key.empty() ? nullptr : builtinMod(w, src.key));
    bool armyFx = false;
    for (int i = 0; m && i < kFxCount; i++) armyFx = armyFx || (schema::kEffects[i].army && m->has(Fx(i)));
    if (armyFx) s.e.sources.push_back(src);
  }
  return std::move(s.e);
}

int slotsOf(const Province& p, const Effects& fx) {
  if (p.sea) return 0;
  int size = int(p.size) >= 0 && int(p.size) < 3 ? schema::kProvSizes[int(p.size)].value : 0;
  int city = int(p.city) >= 0 && int(p.city) < 4 ? schema::kCityTypes[int(p.city)].value : 0;
  long mods = std::lround(fx[Fx::Slots]);
  return int(std::max<long>(0, long(size) + long(city) + mods));
}

double costFactorOf(const Effects& fx) { return std::max(0.0, 1.0 + fx[Fx::BuildCostPct] / 100.0); }

ArchStats archStatsWith(const World& w, const ArchGroup& g, const Effects& factionFx) {
  ArchStats st;
  FxSum s{w, FxSum::Level::Arch, 0, {}};
  for (Id m : g.modifiers) s.add(EffectSource::Faction, 0, m);
  st.fx = s.e;
  for (int i = 0; i < kFxCount; i++)
    if (!schema::kEffects[i].local) st.fx.v[size_t(i)] += factionFx.v[size_t(i)];
  st.level = arch::levelOf(g.exp);
  const arch::LevelInfo& L = arch::kLevels[st.level - 1];
  st.success = clamp(L.success + st.fx[Fx::ArchSuccessPct], 0.0, arch::kMaxBonus);
  st.vitality = clamp(L.vitality + st.fx[Fx::ArchVitalityPct], 0.0, arch::kMaxBonus);
  st.upkeep = L.upkeep * std::max(0.0, 1.0 + st.fx[Fx::ArchUpkeepPct] / 100.0);
  st.expPct = st.fx[Fx::ArchExpPct];
  st.treasurePct = st.fx[Fx::ArchTreasurePct];
  st.discoveryPct = st.fx[Fx::ArchDiscoveryPct];
  st.wounded = hasModKey(w, g.modifiers, schema::mod::ArchWounded);
  st.plague = hasModKey(w, g.modifiers, schema::mod::ArchPlague);
  return st;
}

}  // namespace detail

// ================================================================ публичный интерфейс
Effects provinceEffects(const World& w, Id province) {
  const Province* p = w.province(province);
  if (!p) return {};
  detail::SourceIndex si(w);
  return detail::provinceFx(w, si, *p);
}

Effects factionEffects(const World& w, Id faction) {
  const Faction* f = w.faction(faction);
  if (!f) return {};
  detail::SourceIndex si(w);
  std::vector<const Province*> owned;
  if (f->isState()) w.provinces.each([&](const Province& p) { if (p.owner == faction && !p.sea) owned.push_back(&p); });
  return detail::factionFx(w, si, *f, owned);
}

Effects armyEffects(const World& w, Id army) {
  const Army* a = w.army(army);
  if (!a) return {};
  return detail::armyFx(w, *a, nullptr);
}

ArchStats archStats(const World& w, Id state, Id group) {
  const Faction* f = w.faction(state);
  const ArchGroup* g = f ? f->archGroup(group) : nullptr;
  if (!g) return {};
  return detail::archStatsWith(w, *g, factionEffects(w, state));
}

int archStartLevel(const World& w, Id state) {
  return std::clamp(1 + int(std::lround(factionEffects(w, state)[Fx::ArchStartLevel])), 1, arch::kMaxLevel);
}

namespace detail {
double loyaltyDeltaOf(const World& w, const Army& a, const Effects& fx) {
  if (hasModKey(w, a.modifiers, schema::mod::UndeadArmy)) return 0;   // армия нежити — всегда 100 %
  double d = fx[Fx::LoyaltyPerTurn];
  int loyalists = 0;
  for (const ArmyGroup& g : a.groups)
    for (Id h : g.heroes)
      if (characterHas(w, h, schema::mod::Loyalist)) loyalists++;
  // «Непреклонный лоялист»: верность не уменьшается и растёт на 5 % за каждого такого героя.
  if (loyalists) d = std::max(0.0, d) + schema::kLoyalistBonus * loyalists;
  return d;
}
}  // namespace detail

double loyaltyDelta(const World& w, Id army) {
  const Army* a = w.army(army);
  if (!a) return 0;
  return detail::loyaltyDeltaOf(w, *a, detail::armyFx(w, *a, nullptr));
}

}  // namespace rg::rules
