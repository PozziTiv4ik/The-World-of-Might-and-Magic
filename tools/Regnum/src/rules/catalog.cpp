// Regnum — группы ресурсов, реликвии, особые отряды, эссенции элементов, ключевой ресурс и цены найма строк армии
// (ТЗ «Добавления в справочники», п.3–4; «Доработки», п.8 и 10; «Ввод новых механик», п.1–5).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

constexpr double kMaxAmount = 1e12;

const ResGroup& needGroup(const World& w, Id group) {
  const ResGroup* g = w.catalogs->group(group);
  if (!g) fail(group ? "Группа ресурсов не найдена" : "Не выбрана группа ресурсов");
  return *g;
}

std::string groupName(const ResGroup& g) { return q(g.name.empty() ? std::string("Без названия") : g.name); }

void needAmount(double v, const char* what) {
  needFinite(v, what);
  if (v < 0) fail(std::string(what) + " не может быть меньше нуля");
  if (v > kMaxAmount) fail(std::string(what) + ": слишком большое число");
}

ArmyRow& rowOf(Tx& tx, Id faction, Id row) {
  const Faction& f = needFaction(tx.w(), faction);
  if (!f.armyRow(row)) fail("Строки нет в таблице войск " + facName(tx.w(), faction));
  for (ArmyRow& r : tx.faction(faction).army)
    if (r.id == row) return r;
  fail("Строка не найдена");
}

const ArmyRow& needRow(const World& w, Id faction, Id row) {
  const Faction& f = needFaction(w, faction);
  const ArmyRow* r = f.armyRow(row);
  if (!r) fail("Строки нет в таблице войск " + facName(w, faction));
  return *r;
}

void notSpecial(const ArmyRow& r) {
  if (r.special) fail("Особый отряд повторяет запись справочника «Особые отряды» — правьте её там");
}

std::string typeName(UnitType t) { return schema::unitType(t).name; }

}  // namespace

// ================================================================ группы ресурсов
Id addResGroup(Tx& tx, const std::string& name, Id parent) {
  if (parent) needGroup(tx.w(), parent);
  std::vector<std::string> taken;
  for (const ResGroup& g : tx.w().catalogs->resGroups)
    if (g.parent == parent) taken.push_back(g.name);
  std::string n = trim(name);
  if (n.empty()) {
    n = uniqueName(taken, parent ? "Новая подгруппа" : "Новая группа");
  } else {
    for (const std::string& t : taken)
      if (utf8::searchKey(t) == utf8::searchKey(n)) fail("Группа «" + t + "» уже есть" + (parent ? " в этой группе" : ""));
  }
  ResGroup g;
  g.id = tx.nextId(Seq::ResGroup);
  g.name = n;
  g.parent = parent;
  tx.catalogs().resGroups.push_back(g);
  return g.id;
}

void renameResGroup(Tx& tx, Id group, const std::string& name) {
  const ResGroup& g = needGroup(tx.w(), group);
  std::string n = trim(name);
  if (n.empty()) fail("Название группы не может быть пустым");
  for (const ResGroup& o : tx.w().catalogs->resGroups)
    if (o.id != group && o.parent == g.parent && utf8::searchKey(o.name) == utf8::searchKey(n)) fail("Группа «" + o.name + "» уже есть рядом");
  for (ResGroup& x : tx.catalogs().resGroups)
    if (x.id == group) x.name = n;
}

void setGroupParent(Tx& tx, Id group, Id parent) {
  needGroup(tx.w(), group);
  if (parent) needGroup(tx.w(), parent);
  if (parent == group || (parent && tx.w().catalogs->inGroup(parent, group))) fail("Группа не может войти в саму себя или в свою подгруппу");
  for (ResGroup& x : tx.catalogs().resGroups)
    if (x.id == group) x.parent = parent;
}

void removeResGroup(Tx& tx, Id group) {
  const ResGroup g = needGroup(tx.w(), group);
  if (schema::isRuleGroup(g.key)) fail(groupName(g) + " нужна правилам (провизия, ключевые ресурсы войск) — её нельзя удалить");
  Catalogs& c = tx.catalogs();
  for (ResGroup& x : c.resGroups)
    if (x.parent == group) x.parent = g.parent;
  for (CatalogItem& r : c.resources)
    if (r.group == group) r.group = g.parent;
  c.resGroups.erase(std::remove_if(c.resGroups.begin(), c.resGroups.end(), [&](const ResGroup& x) { return x.id == group; }), c.resGroups.end());
}

void setResourceGroup(Tx& tx, Id resource, Id group) {
  if (!tx.w().resource(resource)) fail("Ресурс не найден");
  if (group) needGroup(tx.w(), group);
  for (CatalogItem& r : tx.catalogs().resources)
    if (r.id == resource) r.group = group;
}

std::vector<Id> resourcesIn(const World& w, Id group) {
  std::vector<Id> out;
  const Catalogs& c = *w.catalogs;
  for (const CatalogItem& r : c.resources)
    if (!group || c.inGroup(r.group, group)) out.push_back(r.id);
  return out;
}

std::vector<Id> childGroups(const World& w, Id parent) {
  std::vector<Id> out;
  for (const ResGroup& g : w.catalogs->resGroups)
    if (g.parent == parent) out.push_back(g.id);
  return out;
}

std::string groupPath(const World& w, Id group) {
  std::vector<std::string> parts;
  for (int guard = 0; group && guard < 64; guard++) {
    const ResGroup* g = w.catalogs->group(group);
    if (!g) break;
    parts.insert(parts.begin(), g->name.empty() ? std::string("Без названия") : g->name);
    group = g->parent;
  }
  return join(parts, " / ");
}

std::vector<Id> provisionResources(const World& w) {
  const Id g = w.catalogs->groupId(schema::grp::Provisions);
  return g ? resourcesIn(w, g) : std::vector<Id>{};
}

// ================================================================ реликвии
Id addRelic(Tx& tx, const std::string& name, Rarity rarity) {
  if (int(rarity) < 0 || rarity >= Rarity::Count) fail("Неизвестная редкость");
  std::vector<std::string> taken;
  for (const Relic& r : tx.w().catalogs->relics) taken.push_back(r.name);
  std::string n = trim(name);
  if (n.empty()) {
    n = uniqueName(taken, "Новая реликвия");
  } else {
    for (const std::string& t : taken)
      if (utf8::searchKey(t) == utf8::searchKey(n)) fail("Реликвия «" + t + "» уже есть — каждая реликвия уникальна");
  }
  Relic r;
  r.id = tx.nextId(Seq::Relic);
  r.name = n;
  r.rarity = rarity;
  tx.catalogs().relics.push_back(r);
  return r.id;
}

void setRelic(Tx& tx, Id relic, const std::string& name, Rarity rarity, const std::string& desc) {
  if (!tx.w().relic(relic)) fail("Реликвия не найдена");
  if (int(rarity) < 0 || rarity >= Rarity::Count) fail("Неизвестная редкость");
  std::string n = trim(name);
  if (n.empty()) fail("Название реликвии не может быть пустым");
  for (const Relic& r : tx.w().catalogs->relics)
    if (r.id != relic && utf8::searchKey(r.name) == utf8::searchKey(n)) fail("Реликвия «" + r.name + "» уже есть — каждая реликвия уникальна");
  for (Relic& r : tx.catalogs().relics)
    if (r.id == relic) {
      r.name = n;
      r.rarity = rarity;
      r.desc = desc;
    }
}

void removeRelic(Tx& tx, Id relic) {
  if (!tx.w().relic(relic)) fail("Реликвия не найдена");
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& c) { return contains(c.inventory, relic); }))
    eraseValue(tx.character(cid).inventory, relic);
  auto& list = tx.catalogs().relics;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const Relic& r) { return r.id == relic; }), list.end());
}

Id relicHolder(const World& w, Id relic) {
  Id who = 0;
  w.characters.each([&](const Character& c) {
    if (!who && contains(c.inventory, relic)) who = c.id;
  });
  return who;
}

void giveRelic(Tx& tx, Id character, Id relic) {
  needCharacter(tx.w(), character);
  const Relic* r = tx.w().relic(relic);
  if (!r) fail("Реликвия не найдена");
  const Id was = relicHolder(tx.w(), relic);
  if (was == character) return;
  const std::string rn = q(r->name);
  if (was) eraseValue(tx.character(was).inventory, relic);
  tx.character(character).inventory.push_back(relic);
  const Character& c = *tx.w().character(character);
  LogRefs refs{0, 0, c.faction ? std::vector<Id>{c.faction} : std::vector<Id>{}};
  addLog(tx, LogKind::Note,
         "Реликвия " + rn + (was ? " перешла от " + q(tx.w().characterName(was)) + " к " : " у ") + q(tx.w().characterName(character)), refs);
}

void takeRelic(Tx& tx, Id character, Id relic) {
  const Character& c = needCharacter(tx.w(), character);
  if (!contains(c.inventory, relic)) return;
  eraseValue(tx.character(character).inventory, relic);
}

// ================================================================ ключевой ресурс
bool keyAllowed(const World& w, UnitType type, Id res) {
  const schema::KeyRule k = schema::keyRule(type);
  const Catalogs& c = *w.catalogs;
  if (!res || !w.resource(res)) return false;
  if (k.resKey) return c.resourceId(k.resKey) == res;
  if (!k.group) return false;
  const Id g = c.groupId(k.group);
  if (!g || !c.resourceIn(res, g)) return false;
  if (k.exclude)
    if (Id x = c.groupId(k.exclude); x && c.resourceIn(res, x)) return false;
  return true;
}

std::vector<Id> keyResources(const World& w, UnitType type) {
  std::vector<Id> out;
  for (const CatalogItem& r : w.catalogs->resources)
    if (keyAllowed(w, type, r.id)) out.push_back(r.id);
  return out;
}

namespace {

// Проверка цены юнита (строки или особого отряда).
void checkKey(const World& w, UnitType type, Id res, double per) {
  const schema::KeyRule k = schema::keyRule(type);
  if (!res) return;
  if (!schema::needsKeyResource(type)) fail("У типа «" + typeName(type) + "» нет ключевого ресурса");
  if (!keyAllowed(w, type, res)) {
    if (k.resKey) fail("Ключевой ресурс военных механизмов — «Запчасти механизмов»");
    fail("«" + resName(w, res) + "» не подходит: ключевой ресурс типа «" + typeName(type) + "» — из группы «" + groupPath(w, w.catalogs->groupId(k.group)) +
         "»" + (k.exclude ? " (кроме «" + groupPath(w, w.catalogs->groupId(k.exclude)) + "»)" : std::string()));
  }
  needFinite(per, "Ключевой ресурс на юнит");
  if (k.fixedOne && per != 1) fail("Для типа «" + typeName(type) + "» на юнит нужна ровно 1 единица ключевого ресурса");
  if (per < 1) fail("Ключевого ресурса на юнит — не меньше 1");
  if (per > kMaxAmount) fail("Ключевой ресурс на юнит: слишком большое число");
}

// Ключевой ресурс, раса и цены под новый тип: неподходящее снимается.
template <class R>
void fitToType(const World& w, R& r) {
  if (r.keyRes && !keyAllowed(w, r.type, r.keyRes)) r.keyRes = 0;
  if (schema::keyRule(r.type).fixedOne || r.keyPer < 1) r.keyPer = 1;
  if (schema::isElemental(r.type)) {
    r.extra.clear();   // элементали нанимаются только за эссенции
    r.race = schema::kRaceElemental;
  } else {
    r.essUpkeep.clear();
  }
}

}  // namespace

// ================================================================ строки армии
void setRowType(Tx& tx, Id faction, Id row, UnitType type) {
  if (int(type) < 0 || type >= UnitType::Count) fail("Неизвестный тип войск");
  const ArmyRow cur = needRow(tx.w(), faction, row);
  notSpecial(cur);
  if (cur.type == type) return;
  const StateKind kind = stateKindOf(tx.w(), faction);
  ArmyRow& r = rowOf(tx, faction, row);
  if (r.name == schema::unitType(r.type).name) r.name = schema::unitType(type).name;   // имя по типу следует за типом
  // Раса по умолчанию следует за типом (механизмы — «Механический», элементали — «Элементали»).
  if (r.race.empty() || r.race == defaultUnitRace(kind, r.type)) r.race = defaultUnitRace(kind, type);
  if (schema::isElemental(r.type) && !schema::isElemental(type)) r.race = defaultUnitRace(kind, type);
  r.type = type;
  fitToType(tx.w(), r);
}

void setRowKey(Tx& tx, Id faction, Id row, Id res, double perUnit) {
  const ArmyRow cur = needRow(tx.w(), faction, row);
  notSpecial(cur);
  checkKey(tx.w(), cur.type, res, res ? perUnit : 1);
  ArmyRow& r = rowOf(tx, faction, row);
  r.keyRes = res;
  r.keyPer = res ? perUnit : 1;
}

void setRowExtra(Tx& tx, Id faction, Id row, Id res, double perUnit) {
  const ArmyRow cur = needRow(tx.w(), faction, row);
  notSpecial(cur);
  if (!tx.w().resource(res)) fail("Ресурс не найден");
  needAmount(perUnit, "Дополнительный ресурс на юнит");
  if (schema::isElemental(cur.type) && perUnit > 0) fail("Элементали нанимаются только за эссенции элементов");
  ArmyRow& r = rowOf(tx, faction, row);
  if (perUnit > 0) r.extra[res] = perUnit;
  else r.extra.erase(res);
}

void setRowEssence(Tx& tx, Id faction, Id row, Id essence, double perUnit) {
  const ArmyRow cur = needRow(tx.w(), faction, row);
  notSpecial(cur);
  if (!tx.w().essence(essence)) fail("Эссенция не найдена");
  needAmount(perUnit, "Эссенция на юнит");
  ArmyRow& r = rowOf(tx, faction, row);
  if (perUnit > 0) r.essence[essence] = perUnit;
  else r.essence.erase(essence);
}

void setRowEssUpkeep(Tx& tx, Id faction, Id row, Id essence, double perUnit) {
  const ArmyRow cur = needRow(tx.w(), faction, row);
  notSpecial(cur);
  if (!schema::isElemental(cur.type)) fail("Эссенциями содержатся только элементали");
  if (!tx.w().essence(essence)) fail("Эссенция не найдена");
  needAmount(perUnit, "Содержание эссенцией на юнит");
  ArmyRow& r = rowOf(tx, faction, row);
  if (perUnit > 0) r.essUpkeep[essence] = perUnit;
  else r.essUpkeep.erase(essence);
}

void syncSpecialRows(Tx& tx, Id special) {
  const SpecialUnit* s = tx.w().special(special);
  if (!s) return;
  const SpecialUnit S = *s;
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) {
         return std::any_of(f.army.begin(), f.army.end(), [&](const ArmyRow& r) { return r.special == special; });
       }))
    for (ArmyRow& r : tx.faction(fid).army) {
      if (r.special != special) continue;
      r.type = S.type;
      r.race = S.race;
      r.keyRes = S.keyRes;
      r.keyPer = S.keyPer;
      r.extra = S.extra;
      r.essence = S.essence;
      r.upkeep = S.upkeep;
      r.essUpkeep = S.essUpkeep;
    }
}

// ================================================================ особые отряды
Id addSpecial(Tx& tx, const std::string& name) {
  std::vector<std::string> taken;
  for (const SpecialUnit& s : tx.w().catalogs->specials) taken.push_back(s.name);
  std::string n = trim(name);
  if (n.empty()) {
    n = uniqueName(taken, "Новый особый отряд");
  } else {
    for (const std::string& t : taken)
      if (utf8::searchKey(t) == utf8::searchKey(n)) fail("Особый отряд «" + t + "» уже есть");
  }
  SpecialUnit s;
  s.id = tx.nextId(Seq::Special);
  s.name = n;
  tx.catalogs().specials.push_back(s);
  return s.id;
}

void setSpecial(Tx& tx, const SpecialUnit& s0) {
  if (!tx.w().special(s0.id)) fail("Особый отряд не найден");
  SpecialUnit s = s0;
  s.name = trim(s.name);
  if (s.name.empty()) fail("Название особого отряда не может быть пустым");
  for (const SpecialUnit& o : tx.w().catalogs->specials)
    if (o.id != s.id && utf8::searchKey(o.name) == utf8::searchKey(s.name)) fail("Особый отряд «" + o.name + "» уже есть");
  if (int(s.type) < 0 || s.type >= UnitType::Count) fail("Неизвестный тип войск");
  fitToType(tx.w(), s);
  checkKey(tx.w(), s.type, s.keyRes, s.keyRes ? s.keyPer : 1);
  needAmount(s.upkeep, "Содержание");
  for (auto& [res, v] : s.extra) {
    if (!tx.w().resource(res)) fail("Ресурс не найден");
    needAmount(v, "Дополнительный ресурс на юнит");
  }
  for (const std::map<Id, double>* m : {&s.essence, &s.essUpkeep})
    for (auto& [e, v] : *m) {
      if (!tx.w().essence(e)) fail("Эссенция не найдена");
      needAmount(v, "Эссенция на юнит");
    }
  for (auto* m : {&s.extra, &s.essence, &s.essUpkeep})
    for (auto it = m->begin(); it != m->end();) it = it->second > 0 ? std::next(it) : m->erase(it);
  for (SpecialUnit& x : tx.catalogs().specials)
    if (x.id == s.id) x = s;
  syncSpecialRows(tx, s.id);
}

void removeSpecial(Tx& tx, Id special) {
  if (!tx.w().special(special)) fail("Особый отряд не найден");
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) {
         return std::any_of(f.army.begin(), f.army.end(), [&](const ArmyRow& r) { return r.special == special; });
       }))
    for (ArmyRow& r : tx.faction(fid).army)
      if (r.special == special) r.special = 0;   // отряды остаются — строка становится обычной
  for (Id bid : idsWhere(tx.w().buildings, [&](const Building& b) { return contains(b.specials, special); }))
    eraseValue(tx.building(bid).specials, special);
  auto& list = tx.catalogs().specials;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const SpecialUnit& s) { return s.id == special; }), list.end());
}

std::vector<SpecialSource> specialAccess(const World& w, Id state) {
  std::vector<SpecialSource> out;
  const Faction* f = w.faction(state);
  if (!f || !f->isState()) return out;
  w.provinces.each([&](const Province& p) {
    if (p.sea || p.owner != state) return;
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      if (!b || !b->specialAccess || pb.builtLevel() < 1) continue;
      for (Id s : b->specials)
        if (w.special(s) && std::none_of(out.begin(), out.end(), [&](const SpecialSource& x) { return x.special == s; }))
          out.push_back(SpecialSource{s, b->id, p.id});
    }
  });
  return out;
}

bool hasSpecialAccess(const World& w, Id state, Id special) {
  for (const SpecialSource& s : specialAccess(w, state))
    if (s.special == special) return true;
  return false;
}

std::vector<Id> specialBuildings(const World& w, Id special) {
  std::vector<Id> out;
  w.buildings.each([&](const Building& b) {
    if (b.specialAccess && contains(b.specials, special)) out.push_back(b.id);
  });
  return out;
}

Id addSpecialRow(Tx& tx, Id faction, Id special) {
  const SpecialUnit* s = tx.w().special(special);
  if (!s) fail("Особый отряд не найден");
  needState(tx.w(), faction);
  if (!hasSpecialAccess(tx.w(), faction, special)) {
    std::vector<std::string> names;
    for (Id b : specialBuildings(tx.w(), special)) names.push_back(buildingName(tx.w(), b));
    fail("Чтобы нанимать " + q(s->name) + ", постройте " + (names.empty() ? std::string("постройку доступа к нему") : join(names, " или ")));
  }
  const SpecialUnit S = *s;
  const Id id = addArmyRow(tx, faction, S.type, S.name, 0, S.upkeep);
  ArmyRow& r = rowOf(tx, faction, id);
  r.special = special;
  r.race = S.race;
  r.keyRes = S.keyRes;
  r.keyPer = S.keyPer;
  r.extra = S.extra;
  r.essence = S.essence;
  r.essUpkeep = S.essUpkeep;
  return id;
}

// ================================================================ эссенции
void setEssence(Tx& tx, Id faction, Id essence, double amount) {
  needFaction(tx.w(), faction);
  if (!tx.w().essence(essence)) fail("Эссенция не найдена");
  needFinite(amount, "Эссенция");
  if (std::fabs(amount) > 1e15) fail("Эссенция: слишком большое число");
  Faction& f = tx.faction(faction);
  if (amount == 0) f.ess.erase(essence);
  else f.ess[essence] = amount;
}

}  // namespace rg::rules
