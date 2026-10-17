// Regnum — строки войск и флота государства: создание (обычные, наёмники, особые отряды), численность, раса, тип,
// ключевой ресурс и цены найма, удаление строки вместе с отрядами.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

ArmyRow& rowOf(Tx& tx, Id faction, Id row) {
  const Faction& f = needFaction(tx.w(), faction);
  if (!f.armyRow(row)) fail("Строки нет в таблице войск " + facName(tx.w(), faction));
  if (ArmyRow* r = findById(tx.faction(faction).army, row)) return *r;
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

// ================================================================ строки войск и флота
Id addArmyRow(Tx& tx, Id faction, UnitType type, const std::string& name, i64 total, double upkeep) {
  needFaction(tx.w(), faction);
  if (int(type) < 0 || type >= UnitType::Count) fail("Неизвестный тип войск");
  if (total < 0 || total > kMaxCount) fail("Численность должна быть от 0 до " + fmtInt(kMaxCount));
  needFinite(upkeep, "Содержание");
  if (upkeep < 0) fail("Содержание не может быть отрицательным");
  ArmyRow r;
  r.id = tx.nextId(Seq::Row);
  std::string n = trim(name);
  r.name = n.empty() ? schema::unitType(type).name : n;
  r.type = type;
  r.total = total;
  r.upkeep = upkeep;
  r.race = defaultUnitRace(stateKindOf(tx.w(), faction), type);   // раса по виду государства
  tx.faction(faction).army.push_back(r);
  return r.id;
}

void setRowRace(Tx& tx, Id faction, Id row, const std::string& race) {
  const Faction& f = needFaction(tx.w(), faction);
  const ArmyRow* cur = f.armyRow(row);
  if (!cur) fail("Строки нет в таблице войск " + facName(tx.w(), faction));
  std::string r = trim(race);
  if (r.empty()) fail("Не выбрана раса отряда");
  if (schema::isElemental(cur->type) && r != schema::kRaceElemental) fail("Раса элементалей — только «Элементали»");
  if (cur->merc && r != schema::kRaceMercenary) fail("Раса наёмников — только «Наемники»");
  findById(tx.faction(faction).army, row)->race = r;
}

Id addFleetRow(Tx& tx, Id faction, ShipType type, const std::string& name, i64 total, double upkeep) {
  needFaction(tx.w(), faction);
  if (int(type) < 0 || type >= ShipType::Count) fail("Неизвестный тип судна");
  if (total < 0 || total > kMaxCount) fail("Численность должна быть от 0 до " + fmtInt(kMaxCount));
  needFinite(upkeep, "Содержание");
  if (upkeep < 0) fail("Содержание не может быть отрицательным");
  FleetRow r;
  r.id = tx.nextId(Seq::Row);
  std::string n = trim(name);
  r.name = n.empty() ? schema::shipType(type).name : n;
  r.type = type;
  r.total = total;
  r.upkeep = upkeep;
  tx.faction(faction).fleet.push_back(r);
  return r.id;
}

void setRowTotal(Tx& tx, Id faction, Id row, i64 total) {
  const Faction& f = needFaction(tx.w(), faction);
  bool fleet = f.fleetRow(row) != nullptr;
  if (!fleet && !f.armyRow(row)) fail("Строки нет в таблицах войск и флота " + facName(tx.w(), faction));
  if (total < 0 || total > kMaxCount) fail("Численность должна быть от 0 до " + fmtInt(kMaxCount));
  Deployed d = deployed(tx.w(), faction);
  i64 field = fleet ? d.fleetField(row) : d.armyField(row);
  if (total < field)
    fail(std::string("Общая численность не может быть меньше назначенной: ") + (fleet ? "в море " : "в поле ") + fmtInt(field));
  Faction& m = tx.faction(faction);
  if (fleet) findById(m.fleet, row)->total = total;
  else findById(m.army, row)->total = total;
}

void removeRow(Tx& tx, Id faction, Id row) {
  const Faction& f = needFaction(tx.w(), faction);
  bool fleet = f.fleetRow(row) != nullptr;
  if (!fleet && !f.armyRow(row)) fail("Строки нет в таблицах войск и флота " + facName(tx.w(), faction));
  // Формирование строки отменяется с возвратом; воины строки возвращаются в население.
  for (int i = int(f.forming.size()) - 1; i >= 0; i--)
    if (tx.w().faction(faction)->forming[size_t(i)].row == row) cancelFormation(tx, faction, i);
  if (!fleet) {
    const ArmyRow r = *tx.w().faction(faction)->armyRow(row);
    returnWarriors(tx, faction, r, r.total);
  }
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
         return std::any_of(p.occGarrison.begin(), p.occGarrison.end(), [&](const GarrisonEntry& g) { return g.row == row; });
       })) {
    auto& gs = tx.province(pid).occGarrison;
    gs.erase(std::remove_if(gs.begin(), gs.end(), [&](const GarrisonEntry& g) { return g.row == row; }), gs.end());
  }
  {
    auto& tf = tx.faction(faction).tradeFleet;
    tf.erase(std::remove_if(tf.begin(), tf.end(), [&](const GarrisonEntry& g) { return g.row == row; }), tf.end());
  }
  for (Id aid : idsWhere(tx.w().armies, [&](const Army& a) {
         if (a.isFleet() != fleet) return false;
         for (const ArmyGroup& g : a.groups)
           if (g.faction == faction && std::any_of(g.units.begin(), g.units.end(), [&](const ArmyUnit& u) { return u.row == row; })) return true;
         return false;
       }))
    for (ArmyGroup& g : tx.army(aid).groups)
      if (g.faction == faction) g.units.erase(std::remove_if(g.units.begin(), g.units.end(), [&](const ArmyUnit& u) { return u.row == row; }), g.units.end());
  if (!fleet)
    for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
           return std::any_of(p.garrison.begin(), p.garrison.end(), [&](const GarrisonEntry& g) { return g.row == row; });
         })) {
      auto& gs = tx.province(pid).garrison;
      gs.erase(std::remove_if(gs.begin(), gs.end(), [&](const GarrisonEntry& g) { return g.row == row; }), gs.end());
    }
  Faction& m = tx.faction(faction);
  if (fleet) m.fleet.erase(std::remove_if(m.fleet.begin(), m.fleet.end(), [&](const FleetRow& r) { return r.id == row; }), m.fleet.end());
  else m.army.erase(std::remove_if(m.army.begin(), m.army.end(), [&](const ArmyRow& r) { return r.id == row; }), m.army.end());
}

Id addMercRow(Tx& tx, Id state, UnitType type, const std::string& name, double hire) {
  needState(tx.w(), state);
  if (!schema::isMercType(type)) fail("Наёмники — только пехота (лёгкая, средняя, тяжёлая) и кавалерия (лёгкая, средняя, тяжёлая)");
  if (mercLimit(tx.w(), state) <= 0) fail("Нужна достроенная «Гильдия Наемников»");
  const double price = hire >= 0 ? hire : std::max(0.0, constantOf(tx.w(), schema::cst::MercHire).num);
  needFinite(price, "Найм");
  const std::string n = trim(name);
  const Id id = addArmyRow(tx, state, type, n.empty() ? std::string("Наёмники: ") + schema::unitType(type).name : n);
  ArmyRow& r = *findById(tx.faction(state).army, id);
  r.merc = true;
  r.hire = price;
  r.race = schema::kRaceMercenary;
  return id;
}

void setMercHire(Tx& tx, Id state, Id row, double hire) {
  const Faction& f = needFaction(tx.w(), state);
  const ArmyRow* r = f.armyRow(row);
  if (!r || !r->merc) fail("Строка — не наёмники");
  needFinite(hire, "Найм");
  if (hire < 0) fail("Цена найма не может быть меньше нуля");
  findById(tx.faction(state).army, row)->hire = hire;
}

// ================================================================ ключевой ресурс
bool keyAllowed(const World& w, UnitType type, Id res) { return schema::keyAllowed(*w.catalogs, type, res); }

std::vector<Id> shipKeyResources(const World& w, ShipType type) {
  std::vector<Id> out;
  const schema::KeyRule k = schema::shipKeyRule(type);
  const Id g = k.group ? w.catalogs->groupId(k.group) : 0;
  if (!g) return out;
  for (const CatalogItem& r : w.catalogs->resources)
    if (w.catalogs->resourceIn(r.id, g)) out.push_back(r.id);
  return out;
}

void setFleetRowKey(Tx& tx, Id faction, Id row, Id res) {
  const Faction& f = needFaction(tx.w(), faction);
  const FleetRow* r = f.fleetRow(row);
  if (!r) fail("Строки нет в таблице флота " + facName(tx.w(), faction));
  if (res && !contains(shipKeyResources(tx.w(), r->type), res))
    fail(r->type == ShipType::SeaMonster ? "Ключевой ресурс морского чудовища — из подгруппы «Морские чудовища»"
                                         : std::string("У этого типа судна нет ключевого ресурса"));
  findById(tx.faction(faction).fleet, row)->keyRes = res;
}

std::vector<Id> keyResources(const World& w, UnitType type) {
  std::vector<Id> out;
  for (const CatalogItem& r : w.catalogs->resources)
    if (keyAllowed(w, type, r.id)) out.push_back(r.id);
  return out;
}

namespace {

// Ключевой ресурс, раса и цены под новый тип: неподходящее снимается.
template <class R>
void fitUnit(const World& w, R& r, bool special) {
  if (!special && r.keyRes && !keyAllowed(w, r.type, r.keyRes)) r.keyRes = 0;
  if ((!special && schema::keyRule(r.type).fixedOne) || r.keyPer < 1) r.keyPer = 1;
  if (schema::isElemental(r.type)) {
    r.extra.clear();   // элементали нанимаются только за эссенции
    r.keyRes = 0;
    r.keyPer = 1;
    r.race = schema::kRaceElemental;
  } else {
    r.essUpkeep.clear();
  }
}

}  // namespace

namespace detail {

void needAmount(double v, const char* what) {
  needFinite(v, what);
  if (v < 0) fail(std::string(what) + " не может быть меньше нуля");
  if (v > kMaxAmount) fail(std::string(what) + ": слишком большое число");
}

void fitToType(const World& w, ArmyRow& r) { fitUnit(w, r, false); }
void fitToType(const World& w, SpecialUnit& s) { fitUnit(w, s, true); }

// Проверка цены юнита (строки или особого отряда). Особый отряд задаёт свои цены: ключевой ресурс — любой ресурс
// справочника, не меньше 1 на юнит.
void checkKey(const World& w, UnitType type, Id res, double per, bool special) {
  const schema::KeyRule k = schema::keyRule(type);
  if (!res) return;
  if (special) {
    if (!w.resource(res)) fail("Ресурс не найден");
    needFinite(per, "Ключевой ресурс на юнит");
    if (per < 1) fail("Ключевого ресурса на юнит — не меньше 1");
    if (per > kMaxAmount) fail("Ключевой ресурс на юнит: слишком большое число");
    return;
  }
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

}  // namespace detail

// ================================================================ строки армии
void setRowType(Tx& tx, Id faction, Id row, UnitType type) {
  if (int(type) < 0 || type >= UnitType::Count) fail("Неизвестный тип войск");
  const ArmyRow cur = needRow(tx.w(), faction, row);
  notSpecial(cur);
  if (cur.type == type) return;
  if (cur.merc && !schema::isMercType(type)) fail("Наёмники — только пехота и кавалерия");
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

}  // namespace rg::rules
