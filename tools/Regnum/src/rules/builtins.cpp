// Regnum — встроенные записи (модификаторы, ресурсы, глобальные константы), расы отрядов и модификаторы сущностей
// (провинции, государства, войска, герои) со сроками и правилами ТЗ «Модификаторы».
//
// Встроенные модификаторы, ресурсы и константы создаются в мире при первом обращении: мир прежней версии не
// переписывается при открытии, а пока записи нет, действует шаблон schema (builtinModifiers, builtinConstants).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

// ================================================================ встроенные записи
const Modifier* builtinMod(const World& w, std::string_view key) {
  const Modifier* found = nullptr;
  w.modifiers.each([&](const Modifier& m) {
    if (!found && m.key == key) found = &m;
  });
  return found ? found : schema::builtinModifier(key);
}

Id builtinModId(const World& w, std::string_view key) {
  Id id = 0;
  w.modifiers.each([&](const Modifier& m) {
    if (!id && m.key == key) id = m.id;
  });
  return id;
}

Id ensureBuiltinMod(Tx& tx, std::string_view key) {
  if (Id id = builtinModId(tx.w(), key)) return id;
  const Modifier* t = schema::builtinModifier(key);
  if (!t) fail("Неизвестный встроенный модификатор «" + std::string(key) + "»");
  Modifier m = *t;
  m.id = 0;
  m.essGen = builtinGenOf(tx.w(), key, true);
  m.resGen = builtinGenOf(tx.w(), key, false);
  return tx.add(std::move(m)).id;
}

std::map<Id, double> builtinGenOf(const World& w, std::string_view key, bool essence) {
  std::map<Id, double> out;
  for (const schema::BuiltinGen& g : schema::builtinGens()) {
    if (key != g.key || g.essence != essence) continue;
    const std::string k = utf8::searchKey(g.name);
    for (const CatalogItem& c : essence ? w.catalogs->essences : w.catalogs->resources)
      if (utf8::searchKey(c.name) == k) out[c.id] = g.perTurn;
  }
  return out;
}

std::map<Id, double> modEssGen(const World& w, const Modifier& m) {
  if (m.id == 0 && !m.key.empty() && m.essGen.empty()) return builtinGenOf(w, m.key, true);   // шаблон встроенного
  return m.essGen;
}
std::map<Id, double> modResGen(const World& w, const Modifier& m) {
  if (m.id == 0 && !m.key.empty() && m.resGen.empty()) return builtinGenOf(w, m.key, false);
  return m.resGen;
}

Id resourceId(const World& w, std::string_view key) {
  if (key == schema::kResGold) return kGold;
  return w.catalogs->resourceId(key);
}

Id ensureResource(Tx& tx, std::string_view key) {
  if (Id id = resourceId(tx.w(), key)) return id;
  for (const auto& b : schema::kBuiltinResources) {
    if (key != b.key) continue;
    CatalogItem it;
    it.id = tx.nextId(Seq::Resource);
    it.name = b.name;
    it.color = Color::hex(b.color);
    it.icon = b.icon;
    it.key = b.key;
    it.builtin = true;
    if (b.group) it.group = tx.w().catalogs->groupId(b.group);
    tx.catalogs().resources.push_back(it);
    return it.id;
  }
  fail("Неизвестный встроенный ресурс «" + std::string(key) + "»");
}

const Constant& constantOf(const World& w, std::string_view key) {
  if (const Constant* c = w.constants->find(key)) return *c;
  for (const Constant& c : schema::builtinConstants())
    if (c.key == key) return c;
  static const Constant kNone;
  return kNone;
}

Constant& ensureConstant(Tx& tx, std::string_view key) {
  for (Constant& c : tx.constants().list)
    if (c.key == key) return c;
  for (const Constant& c : schema::builtinConstants()) {
    if (c.key != key) continue;
    // Встроенные — в порядке шаблона: перед первой следующей встроенной, иначе после последней встроенной.
    auto& list = tx.constants().list;
    const auto& all = schema::builtinConstants();
    size_t at = 0;
    for (size_t i = 0; i < list.size(); i++) {
      auto pos = std::find_if(all.begin(), all.end(), [&](const Constant& x) { return x.key == list[i].key; });
      if (pos == all.end()) continue;
      if (pos - all.begin() < std::find_if(all.begin(), all.end(), [&](const Constant& x) { return x.key == key; }) - all.begin()) at = i + 1;
    }
    list.insert(list.begin() + long(at), c);
    return list[at];
  }
  fail("Константа не найдена");
}

std::string addConstant(Tx& tx, ConstType type, const std::string& name) {
  if (int(type) < 0 || type >= ConstType::Count) fail("Неизвестный тип константы");
  std::string n = trim(name);
  int maxN = 0;
  std::vector<std::string> names;
  for (const Constant& c : tx.w().constants->list) {
    names.push_back(c.name);
    if (startsWith(c.key, "user")) maxN = std::max(maxN, int(parseNum(c.key.substr(4)).value_or(0)));
  }
  for (const Constant& c : schema::builtinConstants()) names.push_back(c.name);
  Constant c;
  c.key = "user" + std::to_string(maxN + 1);
  c.name = n.empty() ? uniqueName(names, "Новая константа") : n;
  c.type = type;
  tx.constants().list.push_back(c);
  return c.key;
}

void removeConstant(Tx& tx, std::string_view key) {
  const Constant& c = constantOf(tx.w(), key);
  if (c.key.empty()) fail("Константа не найдена");
  if (c.builtin) fail("«" + c.name + "» — встроенная константа: её значение можно изменить, но не удалить");
  auto& list = tx.constants().list;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const Constant& x) { return x.key == key; }), list.end());
}

// ================================================================ расы отрядов
std::vector<std::string> unitRaces(const World& w) {
  const Constant& c = constantOf(w, schema::cst::UnitRaces);
  std::vector<std::string> out;
  for (const std::string& v : c.values) {
    std::string t = trim(v);
    if (!t.empty() && !contains(out, t)) out.push_back(t);
  }
  // Базовые расы правил есть всегда (нежить и механизмы при мятеже остаются верными).
  for (const char* b : {schema::kRaceLiving, schema::kRaceDemonic, schema::kRaceUndead, schema::kRaceMechanical, schema::kRaceElemental,
                        schema::kRaceMercenary})
    if (!contains(out, std::string(b))) out.push_back(b);
  return out;
}

std::string defaultUnitRace(StateKind kind, UnitType type) {
  if (type == UnitType::Machines) return schema::kRaceMechanical;
  if (type == UnitType::Elementals) return schema::kRaceElemental;   // элементали — только «Элементали»
  switch (kind) {
    case StateKind::Undead: return schema::kRaceUndead;
    case StateKind::Demonic: return schema::kRaceDemonic;
    default: return schema::kRaceLiving;
  }
}

std::string unitRace(const World& w, Id faction, const ArmyRow& row) {
  if (!row.race.empty()) return row.race;
  return defaultUnitRace(stateKindOf(w, faction), row.type);
}

StateKind stateKindOf(const World& w, Id faction) {
  const Faction* f = w.faction(faction);
  return f && f->isState() ? f->stateKind : StateKind::Living;
}

bool hasModKey(const World& w, const std::vector<Id>& mods, std::string_view key) {
  for (Id id : mods)
    if (const Modifier* m = w.modifier(id); m && m->key == key) return true;
  return false;
}
bool characterHas(const World& w, Id character, std::string_view key) {
  const Character* c = w.character(character);
  if (!c) return false;
  if (hasModKey(w, c->modifiers, key)) return true;
  for (Id m : talentModifiers(w, character))
    if (const Modifier* x = w.modifier(m); x && x->key == key) return true;
  return false;
}

std::vector<Id> talentModifiers(const World& w, Id character) {
  std::vector<Id> out;
  const Character* c = w.character(character);
  if (!c || c->talents.empty()) return out;
  const HeroClass* hc = w.heroClass(c->heroClass);
  if (!hc) return out;
  for (Id t : c->talents)
    if (const Talent* x = hc->talent(t))
      for (Id m : x->modifiers)
        if (std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
  return out;
}
bool armyHas(const World& w, Id army, std::string_view key) {
  const Army* a = w.army(army);
  return a && hasModKey(w, a->modifiers, key);
}
bool provinceHas(const World& w, Id province, std::string_view key) {
  const Province* p = w.province(province);
  return p && hasModKey(w, p->modifiers, key);
}
bool heroAvailable(const World& w, Id character) {
  const Character* c = w.character(character);
  return c && !hasModKey(w, c->modifiers, schema::mod::Dead) && !hasModKey(w, c->modifiers, schema::mod::Captive);
}

// ================================================================ модификаторы сущностей
namespace {

struct ModSlot {
  std::vector<Id>* list;
  ModTurns* turns;
};

const char* targetNoun(ModTarget t) {
  switch (t) {
    case ModTarget::Province: return "провинции";
    case ModTarget::Faction: return "государству";
    case ModTarget::Army: return "войску";
    case ModTarget::Character: return "герою";
  }
  return "";
}

// Модификатор применим к сущности такого вида (вид модификатора «везде» — к любой).
bool kindFits(ModKind k, ModTarget t) {
  switch (k) {
    case ModKind::Any: return true;
    case ModKind::Province: return t == ModTarget::Province;
    case ModKind::Faction: return t == ModTarget::Faction;
    case ModKind::Army: return t == ModTarget::Army;
    case ModKind::Hero: return t == ModTarget::Character;
    default: return false;   // модификаторы археологических групп — только группам (rules/archaeology.cpp)
  }
}

const std::vector<Id>& currentMods(const World& w, ModTarget t, Id id) {
  static const std::vector<Id> kNone;
  switch (t) {
    case ModTarget::Province: if (const Province* p = w.province(id)) return p->modifiers; break;
    case ModTarget::Faction: if (const Faction* f = w.faction(id)) return f->modifiers; break;
    case ModTarget::Army: if (const Army* a = w.army(id)) return a->modifiers; break;
    case ModTarget::Character: if (const Character* c = w.character(id)) return c->modifiers; break;
  }
  return kNone;
}

ModSlot slotOf(Tx& tx, ModTarget t, Id id) {
  switch (t) {
    case ModTarget::Province: { Province& p = tx.province(id); return {&p.modifiers, &p.modTurns}; }
    case ModTarget::Faction: { Faction& f = tx.faction(id); return {&f.modifiers, &f.modTurns}; }
    case ModTarget::Army: { Army& a = tx.army(id); return {&a.modifiers, &a.modTurns}; }
    case ModTarget::Character: { Character& c = tx.character(id); return {&c.modifiers, &c.modTurns}; }
  }
  fail("Неизвестная сущность");
}

void needTarget(const World& w, ModTarget t, Id id) {
  switch (t) {
    case ModTarget::Province: needProvince(w, id); return;
    case ModTarget::Faction: needFaction(w, id); return;
    case ModTarget::Army: needArmy(w, id); return;
    case ModTarget::Character: needCharacter(w, id); return;
  }
}

Id modByKey(const World& w, const std::vector<Id>& mods, std::string_view key) {
  for (Id id : mods)
    if (const Modifier* m = w.modifier(id); m && m->key == key) return id;
  return 0;
}

}  // namespace

void dropModifier(Tx& tx, ModTarget t, Id target, Id modifier) {
  needTarget(tx.w(), t, target);
  if (!contains(currentMods(tx.w(), t, target), modifier)) return;
  const Modifier* m = tx.w().modifier(modifier);
  const std::string key = m ? m->key : std::string();
  ModSlot s = slotOf(tx, t, target);
  eraseValue(*s.list, modifier);
  s.turns->erase(modifier);
  if (t == ModTarget::Character) {
    Character& c = tx.character(target);
    if (key == schema::mod::Dead) c.burial = 0;
    if (key == schema::mod::Captive) c.captor = 0;
  }
  // Любое снятие «Чумы» даёт провинции «Временный иммунитет» (ТЗ «Доработки №3», п.4).
  if (t == ModTarget::Province && key == schema::mod::Plague) {
    const Id imm = ensureBuiltinMod(tx, schema::mod::PlagueImmunity);
    if (!contains(tx.w().province(target)->modifiers, imm)) {
      Province& p = tx.province(target);
      p.modifiers.push_back(imm);
      p.modTurns[imm] = schema::kImmunityTurns;
    }
  }
}

void addModifier(Tx& tx, ModTarget t, Id target, Id modifier, int turns) {
  needTarget(tx.w(), t, target);
  const Modifier* m = tx.w().modifier(modifier);
  if (!m) fail(modifier ? "Модификатор не найден" : "Не выбран модификатор");
  const std::string key = m->key, name = q(m->name.empty() ? std::string("Без названия") : m->name);
  if (!kindFits(m->kind, t)) fail(name + " — " + utf8::lower(schema::modKind(m->kind).name) + ": его нельзя назначить " + targetNoun(t));
  if (!key.empty() && schema::isAutoKey(key)) fail(name + " ставится и снимается автоматически");
  const std::vector<Id>& cur = currentMods(tx.w(), t, target);
  if (contains(cur, modifier)) {
    if (turns >= 0) setModTurns(tx, t, target, modifier, turns);
    return;
  }
  const World& w = tx.w();
  // Правила ТЗ «Модификаторы».
  if (t == ModTarget::Character) {
    if (key == schema::mod::Loyalist && hasModKey(w, cur, schema::mod::Discontent))
      fail("«Непреклонный лоялист» нельзя назначить вместе с «Недовольство правителем»");
    if (key == schema::mod::Discontent && hasModKey(w, cur, schema::mod::Loyalist))
      fail("«Недовольство правителем» нельзя назначить вместе с «Непреклонный лоялист»");
    if (schema::isNatureKey(key))   // природа героя — одна
      for (Id other : std::vector<Id>(cur))
        if (const Modifier* om = w.modifier(other); om && om != m && schema::isNatureKey(om->key)) dropModifier(tx, t, target, other);
  }
  if (t == ModTarget::Army && key == schema::mod::Sadism && !hasModKey(w, cur, schema::mod::DemonArmy) && !hasModKey(w, cur, schema::mod::Ruthless))
    fail("«Изуверское наслаждение» — только для войск с модификатором «Армия демонов» или «Безжалостная армия»");
  if (t == ModTarget::Province && key == schema::mod::Plague) {
    std::string why;
    if (!canPlague(w, target, &why)) fail(why);
  }
  const int n = turns < 0 ? std::max(0, m->duration) : turns;
  ModSlot s = slotOf(tx, t, target);
  s.list->push_back(modifier);
  if (n > 0) (*s.turns)[modifier] = n;
  else s.turns->erase(modifier);
  if (t == ModTarget::Character && (key == schema::mod::Dead || key == schema::mod::Captive)) {
    // Мёртвый и пленный снимаются со всех назначений (ТЗ «Модификаторы», 1.6, 1.12).
    if (key == schema::mod::Dead) {
      if (Id cap = modByKey(tx.w(), tx.w().character(target)->modifiers, schema::mod::Captive)) dropModifier(tx, t, target, cap);
    }
    clearAssignments(tx, target);
  }
  if (t == ModTarget::Army && key == schema::mod::UndeadArmy) tx.army(target).loyalty = schema::kMaxLoyalty;
}

void setModTurns(Tx& tx, ModTarget t, Id target, Id modifier, int turns) {
  needTarget(tx.w(), t, target);
  if (!contains(currentMods(tx.w(), t, target), modifier)) fail("Модификатора нет в списке");
  if (turns < 0 || turns > 1000) fail("Срок — от 0 (бессрочно) до 1000 ходов");
  ModSlot s = slotOf(tx, t, target);
  if (turns > 0) (*s.turns)[modifier] = turns;
  else s.turns->erase(modifier);
}

void setModifiers(Tx& tx, ModTarget t, Id target, const std::vector<Id>& mods) {
  needTarget(tx.w(), t, target);
  const std::vector<Id> cur = currentMods(tx.w(), t, target);
  for (Id id : cur)
    if (!contains(mods, id)) dropModifier(tx, t, target, id);
  for (Id id : mods)
    if (!contains(cur, id)) addModifier(tx, t, target, id);
  // Порядок — как в новом списке.
  ModSlot s = slotOf(tx, t, target);
  std::vector<Id> ordered;
  for (Id id : mods)
    if (contains(*s.list, id) && !contains(ordered, id)) ordered.push_back(id);
  for (Id id : *s.list)
    if (!contains(ordered, id)) ordered.push_back(id);
  *s.list = std::move(ordered);
}

// ================================================================ константы: базовая стоимость
void setConstantRes(Tx& tx, std::string_view key, Id res, double amount, bool essence) {
  if (!std::isfinite(amount) || amount < 0) fail("Количество — число не меньше 0");
  Constant& c = ensureConstant(tx, key);
  if (c.type != ConstType::Resources) fail("«" + c.name + "» — не список ресурсов");
  auto& list = essence ? c.ess : c.res;
  const auto& floor = essence ? c.minEss : c.minRes;
  if (essence ? !tx.w().essence(res) : !tx.w().resource(res)) fail(essence ? "Эссенция не найдена" : "Ресурс не найден");
  auto it = floor.find(res);
  if (it != floor.end() && amount + 1e-9 < it->second)
    fail("Базовая стоимость «" + c.name + "»: не меньше " + fmtNum(it->second, 3) + " — её можно увеличивать, но не уменьшать");
  list[res] = amount;
}

void removeConstantRes(Tx& tx, std::string_view key, Id res, bool essence) {
  Constant& c = ensureConstant(tx, key);
  const auto& floor = essence ? c.minEss : c.minRes;
  if (floor.count(res)) fail("«" + c.name + "»: базовый ресурс или эссенцию нельзя убрать — только увеличить количество");
  (essence ? c.ess : c.res).erase(res);
}

// ================================================================ модификаторы, которые ставятся сами
int councilAssigned(const World& w, Id faction) {
  const Faction* f = w.faction(faction);
  if (!f) return 0;
  int n = 0;
  for (const CouncilSeat& s : f->council)
    if (s.character && heroAvailable(w, s.character)) n++;
  return n;
}

std::vector<AutoMod> autoModifiers(const World& w, Id faction) {
  std::vector<AutoMod> out;
  const Faction* f = w.faction(faction);
  if (!f || !f->isState()) return out;
  auto addKey = [&](const char* key, std::string why) {
    AutoMod a;
    a.key = key;
    a.modifier = builtinModId(w, key);
    a.m = builtinMod(w, key);
    a.why = std::move(why);
    if (a.m) out.push_back(std::move(a));
  };
  // Совет (ТЗ «Общие доработки», п.6): нет назначений — «Децентрализация», 1–3 — «Слабый контроль», больше — «Централизованная власть».
  const int n = councilAssigned(w, faction);
  const std::string seats = std::to_string(n) + " " + plural(n, "назначение", "назначения", "назначений") + " в совете";
  if (n == 0) addKey(schema::mod::Decentralization, "В совете нет назначений");
  else if (n <= 3) addKey(schema::mod::WeakControl, seats);
  else {
    addKey(schema::mod::Centralized, seats);
    // «Влияние совета» (ТЗ «Доработки №1», п.4): вместе с «Централизованной властью» — значение модификатора за каждую
    // должность в совете.
    const int posts = int(f->council.size());
    addKey(schema::mod::CouncilInfluence, std::to_string(posts) + " " + plural(posts, "должность", "должности", "должностей") + " в совете");
    if (!out.empty() && out.back().key == schema::mod::CouncilInfluence) out.back().scale = double(posts);
  }
  // Голод (п.7): недостача провизии (ресурсов группы «Провизия» не хватило на расход населения).
  if (f->provisionDebt > 1e-9) addKey(schema::mod::Famine, "Недостача провизии " + fmtNum(f->provisionDebt, 2));
  // Должности (п.4–5): модификатор занятой или пустующей должности.
  for (const CatalogItem& pos : w.catalogs->positions) {
    if (pos.modifiers.empty() && pos.vacantModifiers.empty()) continue;
    bool taken = false;
    const std::string key = utf8::searchKey(pos.name);
    for (const CouncilSeat& s : f->council)
      if (s.character && heroAvailable(w, s.character) && utf8::searchKey(s.position) == key) taken = true;
    const std::string nm = "«" + (pos.name.empty() ? std::string("Без названия") : pos.name) + "»";
    for (Id mid : taken ? pos.modifiers : pos.vacantModifiers) {
      const Modifier* m = w.modifier(mid);
      if (!m) continue;
      AutoMod a;
      a.modifier = mid;
      a.m = m;
      a.why = taken ? "Должность " + nm + " занята" : "Должность " + nm + " пустует";
      out.push_back(std::move(a));
    }
  }
  return out;
}

std::vector<AutoMod> autoProvinceModifiers(const World& w, Id province) {
  std::vector<AutoMod> out;
  const Province* p = w.province(province);
  if (!p || p->sea) return out;
  const Faction* o = w.faction(p->owner);
  if (o && o->isState() && o->capital == province) {
    AutoMod a;
    a.key = schema::mod::Capital;
    a.modifier = builtinModId(w, a.key);
    a.m = builtinMod(w, a.key);
    a.why = "Столица " + facName(w, o->id);
    if (a.m) out.push_back(std::move(a));
  }
  // «Ценности археологии»: достроенная «Гильдия Археологов» государства, изучившего технологию «Ценности археологии».
  if (o && o->isState()) {
    bool guild = false;
    for (const ProvBuilding& pb : p->buildings) {
      const Building* b = w.building(pb.building);
      guild = guild || (b && b->key == schema::bld::ArchGuild && pb.builtLevel() >= 1);
    }
    if (guild && studiedTechKey(w, o->id, schema::tech::ArchValues)) {
      AutoMod a;
      a.key = schema::mod::ArchValues;
      a.modifier = builtinModId(w, a.key);
      a.m = builtinMod(w, a.key);
      a.why = "Изучена технология «Ценности археологии»";
      if (a.m) out.push_back(std::move(a));
    }
  }
  return out;
}

}  // namespace rg::rules
