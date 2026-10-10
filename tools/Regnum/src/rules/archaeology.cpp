// Regnum — археология (ТЗ «Доработки №2»; «№4», п.1): археологические группы государства (цена, опыт и уровни,
// модификаторы групп, одно задание за ход), «Найти археологическое место», исследование мест по этапам с наградами и
// финальным сокровищем провинции, трагедии при исследовании (ранение, смертельная опасность, пробуждение бедствия с
// чумой группы и войсками без государства), «Проводить раскопки», сундуки сокровищ (вложенные — с ограничением
// глубины), тайник реликвий провинции, режим правки слотов и справочники мест и сундуков.
//
// Таблицы и числа ТЗ — core/arch.h. Случайность детерминирована: зерно — счётчик мира Meta::rng (каждое событие
// увеличивает его, отмена возвращает) и ID участников.
#include <set>

#include "core/arch.h"
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

constexpr int kMaxChestDepth = 4;   // вложенные сундуки: не глубже (защита от циклов списков)

std::string groupTitle(const std::string& name) { return q(name.empty() ? std::string("Без названия") : name); }

const ArchGroup& needGroup(const World& w, Id state, Id group) {
  const Faction& f = needState(w, state);
  const ArchGroup* g = f.archGroup(group);
  if (!g) fail("Археологическая группа не найдена");
  return *g;
}

ArchGroup& groupMut(Tx& tx, Id state, Id group) {
  needGroup(tx.w(), state, group);
  for (ArchGroup& g : tx.faction(state).archGroups)
    if (g.id == group) return g;
  fail("Археологическая группа не найдена");
}

// Бросок события: зерно — счётчик мира и участники.
Rng eventRng(Tx& tx, Id a, Id b) {
  const u32 n = ++tx.meta().rng;
  return Rng(hashMix(hashMix(hashMix(0x4152434841454F4Cull, u64(n)), u64(a)), u64(b)));
}
bool chance(Rng& rng, double pct) { return rng.uniform() * 100.0 < pct; }
template <class V> auto pickOne(Rng& rng, const V& v) { return v[size_t(rng.range(0, int(v.size()) - 1))]; }
double round3(double v) { return std::round(v * 1000.0) / 1000.0; }
double treasureOf(double base, double pct) { return round3(base * std::max(0.0, 1.0 + pct / 100.0)); }

// Провинция — суша государства state: пусто или причина отказа (проверки интерфейса — каждый кадр, без исключений).
std::string ownLandProblem(const World& w, Id state, Id province) {
  const Faction* f = w.faction(state);
  if (!f || !f->isState()) return "Государство не найдено";
  const Province* p = w.province(province);
  if (!p) return "Провинция не найдена";
  if (p->sea) return "В морской провинции нет археологических мест";
  if (p->owner != state) return "Провинция " + provName(w, province) + " не принадлежит " + facName(w, state);
  return {};
}

const Province& needOwnLand(const World& w, Id state, Id province) {
  if (std::string why = ownLandProblem(w, state, province); !why.empty()) fail(why);
  return *w.province(province);
}

bool refuse(std::string* why, std::string s) {
  if (why) *why = std::move(s);
  return false;
}

void needSlot(int slot) {
  if (slot < 0 || slot >= kArchSlots) fail("Нет такого археологического слота");
}

std::string siteName(const World& w, Id site) {
  const ArchSite* s = w.catalogs->archSite(site);
  return s ? q(s->name.empty() ? std::string("Без названия") : s->name) : std::string("«?»");
}

std::string chestName(const World& w, Id chest) {
  const Chest* c = w.catalogs->chest(chest);
  return c ? (c->name.empty() ? std::string("Сундук") : c->name) : std::string("Сундук");
}

// Реликвии, которые где-то лежат (у героя, в постройке, у государства, в тайнике).
std::set<Id> placedRelics(const World& w) {
  std::set<Id> s;
  w.characters.each([&](const Character& c) { s.insert(c.inventory.begin(), c.inventory.end()); });
  w.provinces.each([&](const Province& p) {
    if (p.hiddenRelic) s.insert(p.hiddenRelic);
    for (const ProvBuilding& pb : p.buildings) s.insert(pb.relics.begin(), pb.relics.end());
  });
  w.factions.each([&](const Faction& f) { s.insert(f.relics.begin(), f.relics.end()); });
  return s;
}

void give(Tx& tx, Id state, Id res, double amount, int depth, std::vector<ArchGain>& out) {
  if (!res || amount <= 0) return;
  addStock(tx.faction(state), res, amount);
  out.push_back(ArchGain{ArchGain::Resource, res, amount, depth});
}

void openRec(Tx& tx, Id state, Id chest, Rng& rng, double pct, int depth, std::vector<ArchGain>& out) {
  const Chest* cp = tx.w().catalogs->chest(chest);
  if (!cp) return;
  const Chest c = *cp;   // справочник может быть скопирован транзакцией (новый встроенный ресурс)
  out.push_back(ArchGain{ArchGain::Chest, chest, 1, depth});
  const int d = depth + 1;
  for (const ChestItem& it : c.items) {
    switch (it.kind) {
      case ChestItemKind::Treasure: give(tx, state, ensureResource(tx, schema::kResArchTreasure), treasureOf(it.amount, pct), d, out); break;
      case ChestItemKind::Gold: give(tx, state, kGold, round3(it.amount), d, out); break;
      case ChestItemKind::Resource: {
        Id r = it.res && tx.w().resource(it.res) ? it.res : 0;
        if (!r && it.group) {
          std::vector<Id> pool;
          for (Id x : resourcesIn(tx.w(), it.group))
            if (!contains(it.exclude, x)) pool.push_back(x);
          if (!pool.empty()) r = pickOne(rng, pool);
        }
        give(tx, state, r, round3(it.amount), d, out);
        break;
      }
      case ChestItemKind::Essence: {
        Id e = it.essence && tx.w().essence(it.essence) ? it.essence : 0;
        if (!e) {
          const auto& list = tx.w().catalogs->essences;
          if (!list.empty()) e = list[size_t(rng.range(0, int(list.size()) - 1))].id;
        }
        if (e && it.amount > 0) {
          tx.faction(state).ess[e] += round3(it.amount);
          out.push_back(ArchGain{ArchGain::Essence, e, round3(it.amount), d});
        }
        break;
      }
      case ChestItemKind::Relic: {
        const std::set<Id> placed = placedRelics(tx.w());
        std::vector<Id> pool;
        for (const Relic& r : tx.w().catalogs->relics) {
          if (placed.count(r.id)) continue;
          if (it.relicGroup && !tx.w().catalogs->inRelicGroup(r.group, it.relicGroup)) continue;
          if (it.rarities && !((it.rarities >> unsigned(r.rarity)) & 1u)) continue;
          pool.push_back(r.id);
        }
        if (pool.empty()) {
          out.push_back(ArchGain{ArchGain::NoRelic, 0, 0, d});
          break;
        }
        const Id pick = pickOne(rng, pool);
        moveRelic(tx, pick, RelicPlace{RelicPlace::State, state, 0, state});
        out.push_back(ArchGain{ArchGain::Relic, pick, 1, d});
        break;
      }
      case ChestItemKind::Chest: {
        if (d > kMaxChestDepth) break;
        std::vector<Id> pool;
        for (Id x : it.chests)
          if (tx.w().catalogs->chest(x)) pool.push_back(x);
        if (!pool.empty()) openRec(tx, state, pickOne(rng, pool), rng, pct, d, out);
        break;
      }
      default: break;
    }
  }
}

// ---------------------------------------------------------------- события групп
void gainExp(Tx& tx, Id state, Id group, int base, double expPct, ArchReport& r) {
  ArchGroup& g = groupMut(tx, state, group);
  const int add = std::max(0, int(std::lround(double(base) * std::max(0.0, 1.0 + expPct / 100.0))));
  const int before = g.exp;
  g.exp = std::min(arch::kMaxExp, g.exp + add);
  r.exp += g.exp - before;
}

void wound(Tx& tx, Id state, Id group, ArchReport& r) {
  addArchGroupModifier(tx, state, group, ensureBuiltinMod(tx, schema::mod::ArchWounded), arch::kWoundTurns);
  r.wounded = true;
}

// Смертельная опасность (п.18.2): ранение с шансом 50 % + живучесть, иначе — решение о спасении.
void danger(Tx& tx, Id state, Id group, double vitality, Rng& rng, ArchReport& r) {
  r.lines.push_back("Группа в смертельной опасности");
  if (chance(rng, std::clamp(arch::kDangerWound + vitality, 0.0, 100.0))) {
    wound(tx, state, group, r);
    r.lines.push_back("Группа спаслась, но ранена: " + nTurns(arch::kWoundTurns) + " без исследований");
  } else {
    groupMut(tx, state, group).danger = true;
    r.awaiting = true;
    r.lines.push_back("Группу может спасти только божественное вмешательство");
  }
}

Id spawnUnits(Tx& tx, Id province, const std::vector<arch::WildUnit>& units, ArchReport& r) {
  if (units.empty()) return 0;
  std::vector<WildUnitSpec> specs;
  for (const arch::WildUnit& u : units) specs.push_back(WildUnitSpec{u.name, u.type, u.race, u.count});
  const auto at = provinceLabel(tx, province);
  Id army = 0;
  if (at) {
    try {
      army = spawnWildArmy(tx, *at, units.front().name, specs);
    } catch (const UserError&) {
      army = 0;
    }
  }
  if (army) {
    r.army = army;
  } else {
    r.lines.push_back("Войску без государства негде появиться: рядом нет свободного места");
  }
  return army;
}

// Пробуждение бедствия (п.19).
void calamity(Tx& tx, Id state, Id group, Id province, const ArchSite& site, double vitality, Rng& rng, ArchReport& r) {
  const arch::Calamity& c = arch::calamityOf(site.key);
  if (c.specialPct > 0 && chance(rng, c.specialPct)) {
    r.calamity = c.title ? c.title : "";
    r.lines.push_back(r.calamity);
    spawnUnits(tx, province, c.army, r);
  } else if (rng.range(0, 1) == 0) {
    r.calamity = arch::kPlagueTitle;
    r.lines.push_back(r.calamity);
    addArchGroupModifier(tx, state, group, ensureBuiltinMod(tx, schema::mod::ArchPlague), arch::kPlagueTurns);
    r.plague = true;
    r.lines.push_back("Группа заражена чумой на " + nTurns(arch::kPlagueTurns));
  } else {
    r.calamity = arch::kGuardsTitle;
    r.lines.push_back(r.calamity);
    danger(tx, state, group, vitality, rng, r);
    spawnUnits(tx, province, arch::guardsArmy(), r);
  }
}

// Трагедия при исследовании (п.18): 70 % ранение, 25 % смертельная опасность, 5 % пробуждение бедствия.
void tragedy(Tx& tx, Id state, Id group, Id province, const ArchSite& site, double vitality, Rng& rng, ArchReport& r) {
  r.tragedyLine = int(r.lines.size());
  r.lines.push_back("Трагедия при исследовании");
  const double roll = rng.uniform() * 100.0;
  if (roll < arch::kTragedyWound) {
    r.tragedy = ArchReport::Wound;
    wound(tx, state, group, r);
    r.lines.push_back("Группа ранена: " + nTurns(arch::kWoundTurns) + " без исследований");
  } else if (roll < arch::kTragedyWound + arch::kTragedyDanger) {
    r.tragedy = ArchReport::Danger;
    danger(tx, state, group, vitality, rng, r);
  } else {
    r.tragedy = ArchReport::Calamity;
    calamity(tx, state, group, province, site, vitality, rng, r);
  }
}

// Начало задания: проверки группы, отметка «занята в этом ходу», чума группы заражает провинцию.
ArchReport start(Tx& tx, ArchReport::Kind kind, Id state, Id group, Id province, const ArchStats& st) {
  needOwnLand(tx.w(), state, province);
  std::string why;
  if (!archGroupReady(tx.w(), state, group, &why)) fail(why);
  ArchReport r;
  r.kind = kind;
  r.state = state;
  r.group = group;
  r.province = province;
  r.title = std::string(kind == ArchReport::Dig ? "Результат раскопок в провинции " : "Результат исследования в провинции ") + provName(tx.w(), province);
  const ArchGroup& g = needGroup(tx.w(), state, group);
  r.groupName = g.name;
  r.levelBefore = arch::levelOf(g.exp);
  groupMut(tx, state, group).busy = tx.w().turn();
  if (st.plague && infectPlague(tx, province)) {
    r.infected = province;
    r.lines.push_back("Группа занесла чуму в провинцию " + provName(tx.w(), province));
  }
  return r;
}

std::string gainsText(const World& w, const std::vector<ArchGain>& gains) {
  std::string s;
  for (const ArchGain& g : gains) {
    if (g.depth > 0 && g.kind == ArchGain::Chest) continue;   // вложенные сундуки — их содержимым
    if (!s.empty()) s += ", ";
    s += archGainText(w, g);
  }
  return s;
}

void finish(Tx& tx, ArchReport& r) {
  if (const Faction* f = tx.w().faction(r.state))
    if (const ArchGroup* g = f->archGroup(r.group)) {
      r.expAfter = g->exp;
      r.levelAfter = arch::levelOf(g->exp);
    }
  std::string text = "Археологическая группа " + groupTitle(r.groupName) + " (" + tx.w().factionName(r.state) + "): ";
  switch (r.kind) {
    case ArchReport::Discover:
      text += r.success ? "найдено археологическое место " + siteName(tx.w(), r.site) : std::string("археологическое место не найдено");
      break;
    case ArchReport::Explore:
      text += "исследование места " + siteName(tx.w(), r.site) + (r.success ? " — этап " + std::to_string(r.stage) + " из " + std::to_string(r.stages) + " пройден"
                                                                           : std::string(" не удалось"));
      if (r.done) text += ", место исследовано полностью";
      break;
    case ArchReport::Dig: text += r.success ? "раскопки принесли находки" : std::string("раскопки ничего не дали"); break;
  }
  text += " в провинции " + provName(tx.w(), r.province);
  if (!r.gains.empty()) text += ". Получено: " + gainsText(tx.w(), r.gains);
  if (!r.calamity.empty()) text += ". " + r.calamity;
  else if (r.tragedy == ArchReport::Wound) text += ". Трагедия: группа ранена";
  else if (r.tragedy == ArchReport::Danger) text += ". Трагедия: группа в смертельной опасности";
  if (r.army) text += ". Появилось войско без государства " + armyName(tx.w(), r.army);
  if (r.infected) text += ". Группа занесла чуму";
  if (r.exp > 0) text += ". Опыт +" + fmtInt(r.exp);
  if (r.levelAfter > r.levelBefore) text += ", новый уровень " + std::to_string(r.levelAfter);
  addLog(tx, LogKind::Archaeology, text, LogRefs{r.province, r.army, {r.state}});
}

double discoveryChanceOf(const ArchStats& st) { return std::clamp(arch::kDiscoverySuccess + st.success + st.discoveryPct, 0.0, 100.0); }

}  // namespace

// ================================================================ группы
ArchGroupCost archGroupCost(const World& w, Id state) {
  ArchGroupCost c;
  const Faction* f = w.faction(state);
  if (!f || !f->isState()) {
    c.problems.push_back("Археологические группы бывают только у государства");
    return c;
  }
  c.people = std::max<i64>(0, i64(std::llround(constantOf(w, schema::cst::ArchGroupPeople).num)));
  c.gold = std::max(0.0, constantOf(w, schema::cst::ArchGroupGold).num);
  c.corpses = stateKindOf(w, state) == StateKind::Undead;
  if (builtWithKey(w, state, schema::bld::ArchGuild) < 1)
    c.problems.push_back("Нужна достроенная постройка " + q(buildingKeyName(w, state, schema::bld::ArchGuild)));
  if (c.corpses) {
    const double have = f->stock(resourceId(w, schema::kResCorpses));
    if (have + 1e-9 < double(c.people)) c.problems.push_back("Недостаточно трупов: нужно " + fmtInt(c.people) + ", есть " + amount(std::max(0.0, have)));
  } else {
    const i64 have = statePopulation(w, state);
    if (have < c.people) c.problems.push_back("Недостаточно населения: нужно " + fmtInt(c.people) + ", есть " + fmtInt(have));
  }
  if (f->treasury() + 1e-9 < c.gold)
    c.problems.push_back("Недостаточно золота: нужно " + amount(c.gold) + " тыс., в казне " + amount(std::max(0.0, f->treasury())) + " тыс.");
  return c;
}

Id createArchGroup(Tx& tx, Id state, const std::string& name) {
  const ArchGroupCost c = archGroupCost(tx.w(), state);
  if (!c.problems.empty()) fail(c.problems.front());
  if (c.corpses) addStock(tx.faction(state), ensureResource(tx, schema::kResCorpses), -double(c.people));
  else takePopulation(tx, state, c.people);
  if (c.gold > 0) addStock(tx.faction(state), kGold, -c.gold);
  std::vector<std::string> taken;
  for (const ArchGroup& g : tx.w().faction(state)->archGroups) taken.push_back(g.name);
  const std::string nm = trim(name);
  ArchGroup g;
  g.id = tx.nextId(Seq::ArchGroup);
  g.name = nm.empty() ? uniqueName(taken, "Археологическая группа") : nm;
  g.exp = arch::levelMinExp(archStartLevel(tx.w(), state));
  const std::string title = groupTitle(g.name);
  tx.faction(state).archGroups.push_back(std::move(g));
  const Id id = tx.w().faction(state)->archGroups.back().id;
  addLog(tx, LogKind::Archaeology, facName(tx.w(), state) + " формирует археологическую группу " + title, LogRefs{0, 0, {state}});
  return id;
}

void renameArchGroup(Tx& tx, Id state, Id group, const std::string& name) {
  const std::string nm = trim(name);
  if (nm.empty()) fail("Название группы не может быть пустым");
  groupMut(tx, state, group).name = nm;
}

void disbandArchGroup(Tx& tx, Id state, Id group) {
  const std::string title = groupTitle(needGroup(tx.w(), state, group).name);
  auto& list = tx.faction(state).archGroups;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const ArchGroup& g) { return g.id == group; }), list.end());
  addLog(tx, LogKind::Archaeology, facName(tx.w(), state) + " распускает археологическую группу " + title, LogRefs{0, 0, {state}});
}

void setArchGroupExp(Tx& tx, Id state, Id group, int exp) {
  if (exp < 0 || exp > arch::kMaxExp) fail("Опыт группы — от 0 до " + std::to_string(arch::kMaxExp));
  groupMut(tx, state, group).exp = exp;
}

void setArchGroupModifiers(Tx& tx, Id state, Id group, const std::vector<Id>& mods) {
  needGroup(tx.w(), state, group);
  std::vector<Id> clean;
  std::map<Id, int> dur;
  for (Id m : mods) {
    if (!m || contains(clean, m)) continue;
    const Modifier* x = tx.w().modifier(m);
    if (!x) fail("Модификатор не найден");
    if (x->kind != ModKind::ArchGroup) fail(q(x->name) + " — не модификатор археологических групп");
    clean.push_back(m);
    dur[m] = x->duration;
  }
  ArchGroup& g = groupMut(tx, state, group);
  for (Id m : clean)
    if (!contains(g.modifiers, m) && dur[m] > 0) g.modTurns[m] = dur[m];
  for (Id m : g.modifiers)
    if (!contains(clean, m)) g.modTurns.erase(m);
  g.modifiers = std::move(clean);
}

void addArchGroupModifier(Tx& tx, Id state, Id group, Id modifier, int turns) {
  const Modifier* x = tx.w().modifier(modifier);
  if (!x) fail("Модификатор не найден");
  if (x->kind != ModKind::ArchGroup) fail(q(x->name) + " — не модификатор археологических групп");
  const int n = turns < 0 ? x->duration : turns;
  ArchGroup& g = groupMut(tx, state, group);
  if (!contains(g.modifiers, modifier)) g.modifiers.push_back(modifier);
  if (n > 0) g.modTurns[modifier] = n;
  else g.modTurns.erase(modifier);
}

void setArchGroupModTurns(Tx& tx, Id state, Id group, Id modifier, int turns) {
  ArchGroup& g = groupMut(tx, state, group);
  if (!contains(g.modifiers, modifier)) fail("У группы нет этого модификатора");
  if (turns > 0) g.modTurns[modifier] = turns;
  else g.modTurns.erase(modifier);
}

bool archGroupReady(const World& w, Id state, Id group, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Faction* f = w.faction(state);
  const ArchGroup* g = f ? f->archGroup(group) : nullptr;
  if (!g) return no("Археологическая группа не найдена");
  if (g->danger) return no("Группа " + groupTitle(g->name) + " в смертельной опасности — нужно решение о спасении");
  if (g->busy == w.turn()) return no("Группа " + groupTitle(g->name) + " уже участвовала в исследовании в этом ходу");
  if (hasModKey(w, g->modifiers, schema::mod::ArchWounded)) return no("Группа " + groupTitle(g->name) + " ранена — исследования недоступны");
  return true;
}

// ================================================================ задания
double discoveryChance(const World& w, Id state, Id group) { return discoveryChanceOf(archStats(w, state, group)); }

double exploreChance(const World& w, Id state, Id group, Id province, int slot) {
  const Province* p = w.province(province);
  if (!p || slot < 0 || slot >= kArchSlots) return 0;
  return arch::stageSuccess(slot, p->arch[size_t(slot)].stage + 1, archStats(w, state, group).success);
}

bool canDiscover(const World& w, Id state, Id province, std::string* why) {
  if (std::string s = ownLandProblem(w, state, province); !s.empty()) return refuse(why, std::move(s));
  for (const ArchSlot& s : w.province(province)->arch)
    if (s.site && !s.open) return true;
  return refuse(why, "Все археологические места провинции уже найдены");
}

bool canExplore(const World& w, Id state, Id province, int slot, std::string* why) {
  if (slot < 0 || slot >= kArchSlots) return refuse(why, "Нет такого археологического слота");
  if (std::string s = ownLandProblem(w, state, province); !s.empty()) return refuse(why, std::move(s));
  const ArchSlot& s = w.province(province)->arch[size_t(slot)];
  if (!s.site || !s.open) return refuse(why, "Место ещё не найдено");
  if (arch::slotDone(s, slot)) return refuse(why, "Место " + siteName(w, s.site) + " уже исследовано полностью");
  return true;
}

bool canExcavate(const World& w, Id state, Id province, std::string* why) {
  if (std::string s = ownLandProblem(w, state, province); !s.empty()) return refuse(why, std::move(s));
  if (!arch::allDone(*w.province(province))) return refuse(why, "Раскопки — когда все археологические места провинции исследованы полностью");
  return true;
}

ArchReport discoverSite(Tx& tx, Id state, Id group, Id province) {
  std::string why;
  if (!canDiscover(tx.w(), state, province, &why)) fail(why);
  const ArchStats st = archStats(tx.w(), state, group);
  ArchReport r = start(tx, ArchReport::Discover, state, group, province, st);
  r.chance = discoveryChanceOf(st);
  Rng rng = eventRng(tx, group, province);
  r.success = chance(rng, r.chance);
  if (r.success) {
    const Province& p = *tx.w().province(province);
    for (int i = 0; i < kArchSlots; i++)
      if (p.arch[size_t(i)].site && !p.arch[size_t(i)].open) {
        r.slot = i;
        break;
      }
    ArchSlot& s = tx.province(province).arch[size_t(r.slot)];
    s.open = true;
    s.stage = 0;
    r.site = s.site;
    r.stages = arch::stagesOf(r.slot);
  }
  finish(tx, r);
  return r;
}

ArchReport exploreSite(Tx& tx, Id state, Id group, Id province, int slot) {
  std::string why;
  if (!canExplore(tx.w(), state, province, slot, &why)) fail(why);
  const ArchStats st = archStats(tx.w(), state, group);
  ArchReport r = start(tx, ArchReport::Explore, state, group, province, st);
  const ArchSlot s0 = tx.w().province(province)->arch[size_t(slot)];
  const ArchSite* sp = tx.w().catalogs->archSite(s0.site);
  if (!sp) fail("Археологическое место не найдено в справочнике");
  const ArchSite site = *sp;
  r.slot = slot;
  r.site = s0.site;
  r.stages = arch::stagesOf(slot);
  r.stage = s0.stage + 1;
  r.chance = arch::stageSuccess(slot, r.stage, st.success);
  const arch::StageInfo& si = arch::stageInfo(slot, r.stage);
  Rng rng = eventRng(tx, group, province * 4 + Id(slot));
  r.success = chance(rng, r.chance);
  if (r.success) {
    tx.province(province).arch[size_t(slot)].stage = r.stage;
    if (r.stage >= r.stages) {
      r.done = true;
      if (s0.chest && tx.w().catalogs->chest(s0.chest)) openRec(tx, state, s0.chest, rng, st.treasurePct, 0, r.gains);
    } else {
      std::vector<Id> chests;
      for (const char* k : si.chests)
        if (const Chest* c = tx.w().catalogs->chestByKey(k)) chests.push_back(c->id);
      const bool treasure = si.hi > 0;
      if (!chests.empty() && (!treasure || rng.range(0, 1) == 1)) {
        openRec(tx, state, pickOne(rng, chests), rng, st.treasurePct, 0, r.gains);
      } else if (treasure) {
        give(tx, state, ensureResource(tx, schema::kResArchTreasure), treasureOf(rng.range(si.lo, si.hi), st.treasurePct), 0, r.gains);
      }
    }
    gainExp(tx, state, group, si.exp, st.expPct, r);
  } else {
    if (si.tragedy > 0 && chance(rng, si.tragedy)) tragedy(tx, state, group, province, site, st.vitality, rng, r);
    else gainExp(tx, state, group, arch::kFailExp, st.expPct, r);
  }
  finish(tx, r);
  return r;
}

ArchReport excavate(Tx& tx, Id state, Id group, Id province) {
  std::string why;
  if (!canExcavate(tx.w(), state, province, &why)) fail(why);
  const ArchStats st = archStats(tx.w(), state, group);
  ArchReport r = start(tx, ArchReport::Dig, state, group, province, st);
  Rng rng = eventRng(tx, group, province);
  const arch::DigOutcome o = arch::digOutcome(rng.range(1, 100));
  r.success = o.kind != arch::DigOutcome::Nothing;
  switch (o.kind) {
    case arch::DigOutcome::Treasure:
      give(tx, state, ensureResource(tx, schema::kResArchTreasure), treasureOf(rng.range(o.lo, o.hi), st.treasurePct), 0, r.gains);
      break;
    case arch::DigOutcome::Scroll: give(tx, state, ensureResource(tx, schema::kResShantiriScrolls), double(o.lo), 0, r.gains); break;
    case arch::DigOutcome::Relic: {
      const Province& p = *tx.w().province(province);
      if (p.hiddenRelic && p.hiddenBy != state) {
        const Id relic = p.hiddenRelic;
        moveRelic(tx, relic, RelicPlace{RelicPlace::State, state, 0, state});
        r.gains.push_back(ArchGain{ArchGain::Relic, relic, 1, 0});
        r.lines.push_back("Откопана реликвия, спрятанная другим государством");
        break;
      }
      // Случайная свободная находка случайной редкости (из редкостей, у которых есть свободные находки).
      const std::set<Id> placed = placedRelics(tx.w());
      std::map<int, std::vector<Id>> byRarity;
      for (const Relic& x : tx.w().catalogs->relics)
        if (!placed.count(x.id) && isArchFind(tx.w(), x.id)) byRarity[int(x.rarity)].push_back(x.id);
      if (byRarity.empty()) {
        r.gains.push_back(ArchGain{ArchGain::NoRelic, 0, 0, 0});
        break;
      }
      std::vector<int> rarities;
      for (auto& [k, v] : byRarity) rarities.push_back(k);
      const Id relic = pickOne(rng, byRarity[pickOne(rng, rarities)]);
      moveRelic(tx, relic, RelicPlace{RelicPlace::State, state, 0, state});
      r.gains.push_back(ArchGain{ArchGain::Relic, relic, 1, 0});
      break;
    }
    case arch::DigOutcome::Nothing: break;
  }
  finish(tx, r);
  return r;
}

void resolveArchDanger(Tx& tx, Id state, Id group, Id essence) {
  const std::string title = groupTitle(needGroup(tx.w(), state, group).name);
  if (!needGroup(tx.w(), state, group).danger) fail("Группа " + title + " не в смертельной опасности");
  if (essence) {
    const CatalogItem* e = tx.w().essence(essence);
    if (!e) fail("Эссенция не найдена");
    const double have = tx.w().faction(state)->essence(essence);
    if (have + 1e-9 < arch::kDivineCost)
      fail("Недостаточно эссенции " + q(e->name) + ": нужно " + amount(arch::kDivineCost) + ", есть " + amount(std::max(0.0, have)));
    const std::string en = e->name;
    tx.faction(state).ess[essence] -= arch::kDivineCost;
    groupMut(tx, state, group).danger = false;
    addArchGroupModifier(tx, state, group, ensureBuiltinMod(tx, schema::mod::ArchWounded), arch::kWoundTurns);
    addLog(tx, LogKind::Archaeology,
           "Божественное вмешательство спасло археологическую группу " + title + " (" + tx.w().factionName(state) + ", " + en + " " +
               amount(arch::kDivineCost) + "): группа ранена",
           LogRefs{0, 0, {state}});
    return;
  }
  auto& list = tx.faction(state).archGroups;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const ArchGroup& g) { return g.id == group; }), list.end());
  addLog(tx, LogKind::Archaeology, "Археологическая группа " + title + " (" + tx.w().factionName(state) + ") погибла", LogRefs{0, 0, {state}});
}

std::vector<Id> divineEssences(const World& w, Id state) {
  std::vector<Id> out;
  const Faction* f = w.faction(state);
  if (!f) return out;
  for (const CatalogItem& e : w.catalogs->essences)
    if (f->essence(e.id) + 1e-9 >= arch::kDivineCost) out.push_back(e.id);
  return out;
}

std::vector<ArchGain> openChest(Tx& tx, Id state, Id chest, Rng& rng, double treasurePct) {
  needState(tx.w(), state);
  if (!tx.w().catalogs->chest(chest)) fail("Сундук не найден");
  std::vector<ArchGain> out;
  openRec(tx, state, chest, rng, treasurePct, 0, out);
  return out;
}

std::string archGainText(const World& w, const ArchGain& g) {
  switch (g.kind) {
    case ArchGain::Resource: return resName(w, g.id) + " " + amount(g.amount) + (g.id == kGold ? " тыс." : "");
    case ArchGain::Essence: {
      const CatalogItem* e = w.essence(g.id);
      return (e ? e->name : std::string("Эссенция")) + " " + amount(g.amount);
    }
    case ArchGain::Relic: {
      const Relic* r = w.relic(g.id);
      return "реликвия " + q(r ? r->name : std::string("?"));
    }
    case ArchGain::Chest: return chestName(w, g.id);
    case ArchGain::NoRelic: return "реликвии не нашлось";
  }
  return {};
}

// ================================================================ тайник
std::vector<Id> hideableRelics(const World& w, Id province) {
  std::vector<Id> out;
  const Province* p = w.province(province);
  if (!p || p->sea || !p->owner) return out;
  const Faction* o = w.faction(p->owner);
  if (!o || !o->isState()) return out;
  auto add = [&](Id r) {
    if (r && w.relic(r) && !contains(out, r)) out.push_back(r);
  };
  w.characters.each([&](const Character& c) {
    if (c.faction == o->id)
      for (Id r : c.inventory) add(r);
  });
  w.provinces.each([&](const Province& q2) {
    if (q2.owner != o->id || q2.sea) return;
    for (const ProvBuilding& pb : q2.buildings)
      for (Id r : pb.relics) add(r);
  });
  for (Id r : o->relics) add(r);
  return out;
}

void hideRelic(Tx& tx, Id province, Id relic) {
  const Province& p = needProvince(tx.w(), province);
  if (p.sea) fail("В морской провинции нельзя спрятать реликвию");
  const Faction* o = tx.w().faction(p.owner);
  if (!o || !o->isState()) fail("У провинции нет государства-владельца");
  if (p.hiddenRelic) fail("В провинции уже спрятана реликвия");
  if (!tx.w().relic(relic)) fail("Реликвия не найдена");
  if (!contains(hideableRelics(tx.w(), province), relic))
    fail("Спрятать можно реликвию героя, постройки или государства " + facName(tx.w(), o->id));
  moveRelic(tx, relic, RelicPlace{RelicPlace::Hidden, province, 0, o->id});
}

bool canUnearth(const World& w, Id province, std::string* why) {
  const Province* p = w.province(province);
  std::string s;
  if (!p || !p->hiddenRelic) s = "В провинции нет спрятанной реликвии";
  else if (!p->owner || p->owner != p->hiddenBy) s = "Откопать реликвию может только спрятавшее её государство, пока владеет провинцией";
  if (s.empty()) return true;
  if (why) *why = s;
  return false;
}

void unearthRelic(Tx& tx, Id province, const RelicPlace& to) {
  std::string why;
  if (!canUnearth(tx.w(), province, &why)) fail(why);
  const Province& p = *tx.w().province(province);
  const Id owner = p.owner, relic = p.hiddenRelic;
  if (to.kind == RelicPlace::Hero) {
    const Character& c = needCharacter(tx.w(), to.id);
    if (c.faction != owner) fail("Герой не служит государству " + facName(tx.w(), owner));
    if (!heroAvailable(tx.w(), c.id)) fail("Герой " + q(c.name) + " недоступен");
    moveRelic(tx, relic, RelicPlace{RelicPlace::Hero, c.id, 0, owner});
    return;
  }
  if (to.kind == RelicPlace::Building) {
    const Province& q2 = needProvince(tx.w(), to.id);
    if (q2.owner != owner) fail("Постройка не принадлежит государству " + facName(tx.w(), owner));
    const Building& b = needBuilding(tx.w(), to.building);
    if (!b.relicStore) fail(buildingName(tx.w(), b.id) + " — не хранилище реликвий");
    bool built = false;
    for (const ProvBuilding& pb : q2.buildings) built = built || (pb.building == b.id && pb.builtLevel() >= 1);
    if (!built) fail(buildingName(tx.w(), b.id) + " ещё не достроена");
    moveRelic(tx, relic, RelicPlace{RelicPlace::Building, q2.id, b.id, owner});
    return;
  }
  fail("Откопанную реликвию кладут в инвентарь героя или в хранилище постройки");
}

// ================================================================ режим правки
void setArchSlotOpen(Tx& tx, Id province, int slot, bool open) {
  needSlot(slot);
  const Province& p = needProvince(tx.w(), province);
  if (p.sea) fail("В морской провинции нет археологических мест");
  if (open && !p.arch[size_t(slot)].site) fail("В слоте нет археологического места");
  ArchSlot& s = tx.province(province).arch[size_t(slot)];
  s.open = open;
  if (!open) s.stage = 0;
}

void setArchSlotSite(Tx& tx, Id province, int slot, Id site) {
  needSlot(slot);
  const Province& p = needProvince(tx.w(), province);
  if (p.sea) fail("В морской провинции нет археологических мест");
  if (!tx.w().catalogs->archSite(site)) fail("Археологическое место не найдено");
  if (p.arch[size_t(slot)].site == site) return;
  for (int i = 0; i < kArchSlots; i++)
    if (i != slot && p.arch[size_t(i)].site == site) fail("Место " + siteName(tx.w(), site) + " уже есть в этой провинции");
  Rng rng = eventRng(tx, province, Id(slot) + 1);
  const Id chest = arch::pickChest(*tx.w().catalogs, site, slot, rng);
  ArchSlot& s = tx.province(province).arch[size_t(slot)];
  s.site = site;
  s.stage = 0;
  s.chest = chest;
}

// ================================================================ справочники
void setArchSite(Tx& tx, Id site, const std::string& name, const std::string& icon) {
  if (!tx.w().catalogs->archSite(site)) fail("Археологическое место не найдено");
  const std::string nm = trim(name);
  if (nm.empty()) fail("Название места не может быть пустым");
  for (const ArchSite& s : tx.w().catalogs->archSites)
    if (s.id != site && utf8::searchKey(s.name) == utf8::searchKey(nm)) fail("Место " + q(nm) + " уже есть в справочнике");
  for (ArchSite& s : tx.catalogs().archSites)
    if (s.id == site) {
      s.name = nm;
      if (!trim(icon).empty()) s.icon = trim(icon);
    }
}

void setArchSiteRewards(Tx& tx, Id site, int slot, const std::vector<Id>& chests) {
  needSlot(slot);
  if (!tx.w().catalogs->archSite(site)) fail("Археологическое место не найдено");
  std::vector<Id> clean;
  for (Id c : chests) {
    if (!tx.w().catalogs->chest(c)) fail("Сундук не найден");
    if (!contains(clean, c)) clean.push_back(c);
  }
  for (ArchSite& s : tx.catalogs().archSites)
    if (s.id == site) s.rewards[size_t(slot)] = std::move(clean);
}

Id addChest(Tx& tx, const std::string& name) {
  std::vector<std::string> taken;
  for (const Chest& c : tx.w().catalogs->chests) taken.push_back(c.name);
  const std::string nm = trim(name);
  Chest c;
  c.id = tx.nextId(Seq::Chest);
  c.name = uniqueName(taken, nm.empty() ? std::string("Новый сундук") : nm);
  tx.catalogs().chests.push_back(c);
  return c.id;
}

bool chestGroupAllowed(const World& w, Id group) {
  const Catalogs& c = *w.catalogs;
  if (!c.group(group)) return false;
  const Id ore = c.groupId(schema::grp::Ore), mat = c.groupId(schema::grp::Materials);
  return (ore && c.inGroup(group, ore)) || (mat && c.inGroup(group, mat));
}

bool chestResourceAllowed(const World& w, Id res) {
  if (!w.resource(res)) return false;
  if (res == resourceId(w, schema::kResCorpses)) return true;
  const CatalogItem* r = w.resource(res);
  return r->group && chestGroupAllowed(w, r->group);
}

void setChest(Tx& tx, const Chest& c0) {
  if (!tx.w().catalogs->chest(c0.id)) fail("Сундук не найден");
  Chest c = c0;
  c.name = trim(c.name);
  if (c.name.empty()) fail("Название сундука не может быть пустым");
  for (const Chest& x : tx.w().catalogs->chests)
    if (x.id != c.id && utf8::searchKey(x.name) == utf8::searchKey(c.name)) fail("Сундук " + q(c.name) + " уже есть в справочнике");
  if (c.icon.empty()) c.icon = "chest";
  const World& w = tx.w();
  for (ChestItem& it : c.items) {
    if (int(it.kind) < 0 || int(it.kind) >= int(ChestItemKind::Count)) fail("Неизвестное поле сундука");
    if (!std::isfinite(it.amount) || it.amount < 0) fail("Количество в сундуке — неотрицательное число");
    switch (it.kind) {
      case ChestItemKind::Resource:
        if (it.res) {
          if (!chestResourceAllowed(w, it.res)) fail("Ресурс сундука — из групп «Руда» и «Материалы» или «Трупы»");
          it.group = 0;
          it.exclude.clear();
        } else {
          if (!it.group) fail("Выберите ресурс или группу ресурсов");
          if (!chestGroupAllowed(w, it.group)) fail("Случайный ресурс сундука — из групп «Руда» и «Материалы»");
          std::vector<Id> ex;
          for (Id r : it.exclude)
            if (w.catalogs->resourceIn(r, it.group) && !contains(ex, r)) ex.push_back(r);
          it.exclude = std::move(ex);
        }
        break;
      case ChestItemKind::Essence:
        if (it.essence && !w.essence(it.essence)) fail("Эссенция не найдена");
        break;
      case ChestItemKind::Relic:
        if (it.relicGroup && !w.catalogs->relicGroup(it.relicGroup)) fail("Группа реликвий не найдена");
        it.rarities &= (1u << unsigned(Rarity::Count)) - 1u;
        it.amount = 1;
        break;
      case ChestItemKind::Chest: {
        std::vector<Id> list;
        for (Id x : it.chests) {
          if (x == c.id) fail("Сундук не может лежать сам в себе");
          if (!w.catalogs->chest(x)) fail("Сундук не найден");
          if (!contains(list, x)) list.push_back(x);
        }
        it.chests = std::move(list);
        it.amount = 1;
        break;
      }
      default: break;
    }
  }
  for (Chest& x : tx.catalogs().chests)
    if (x.id == c.id) x = c;
}

void removeChest(Tx& tx, Id chest) {
  if (!tx.w().catalogs->chest(chest)) fail("Сундук не найден");
  Catalogs& cat = tx.catalogs();
  cat.chests.erase(std::remove_if(cat.chests.begin(), cat.chests.end(), [&](const Chest& c) { return c.id == chest; }), cat.chests.end());
  for (ArchSite& s : cat.archSites)
    for (auto& list : s.rewards) eraseValue(list, chest);
  for (Chest& c : cat.chests) {
    for (ChestItem& it : c.items) eraseValue(it.chests, chest);
    c.items.erase(std::remove_if(c.items.begin(), c.items.end(), [](const ChestItem& it) { return it.kind == ChestItemKind::Chest && it.chests.empty(); }),
                  c.items.end());
  }
  // Сокровища провинций: не полученное — выбирается заново из наград места, полученное (место исследовано) — убирается.
  const std::vector<Id> provs = idsWhere(tx.w().provinces, [&](const Province& p) {
    return std::any_of(p.arch.begin(), p.arch.end(), [&](const ArchSlot& s) { return s.chest == chest; });
  });
  for (Id pid : provs) {
    for (int i = 0; i < kArchSlots; i++) {
      const ArchSlot s = tx.w().province(pid)->arch[size_t(i)];
      if (s.chest != chest) continue;
      Id next = 0;
      if (!arch::slotDone(s, i)) {
        Rng rng = eventRng(tx, pid, chest);
        next = arch::pickChest(*tx.w().catalogs, s.site, i, rng);
      }
      tx.province(pid).arch[size_t(i)].chest = next;
    }
  }
}

std::string chestItemText(const World& w, const ChestItem& it) {
  switch (it.kind) {
    case ChestItemKind::Treasure: return "Археологические сокровища " + amount(it.amount);
    case ChestItemKind::Gold: return "Золото " + amount(it.amount) + " тыс.";
    case ChestItemKind::Resource: {
      if (it.res) return resName(w, it.res) + " " + amount(it.amount);
      std::string s = "Случайный ресурс: " + (it.group ? groupPath(w, it.group) : std::string("—")) + " " + amount(it.amount);
      if (!it.exclude.empty()) {
        std::vector<std::string> names;
        for (Id r : it.exclude) names.push_back(resName(w, r));
        s += " (кроме " + join(names, ", ") + ")";
      }
      return s;
    }
    case ChestItemKind::Essence: {
      const CatalogItem* e = it.essence ? w.essence(it.essence) : nullptr;
      return (e ? e->name : std::string("Случайная эссенция")) + " " + amount(it.amount);
    }
    case ChestItemKind::Relic: {
      std::vector<std::string> rs;
      for (int i = 0; i < int(Rarity::Count); i++)
        if ((it.rarities >> unsigned(i)) & 1u) rs.push_back(utf8::lower(schema::kRarities[i].name));
      const RelicGroup* g = w.catalogs->relicGroup(it.relicGroup);
      return "Артефакт" + (g ? " " + q(g->name) : std::string()) + (rs.empty() ? std::string() : ": " + join(rs, " или "));
    }
    case ChestItemKind::Chest: {
      std::vector<std::string> names;
      for (Id c : it.chests) names.push_back(chestName(w, c));
      return "Сундук: " + (names.empty() ? std::string("—") : join(names, ", "));
    }
    default: break;
  }
  return {};
}

}  // namespace rg::rules
