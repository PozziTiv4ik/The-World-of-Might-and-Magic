// Regnum — чума (ТЗ «Доработки №3», п.4–5; «Доработки №2», п.19.1.1): заражение, иммунитет, лечение постройкой
// целительства, заражение постройкой чумы. Распространение при естественном окончании — в завершении хода (turn.cpp).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

Id modWithKey(const World& w, const std::vector<Id>& mods, std::string_view key) {
  for (Id m : mods)
    if (const Modifier* x = w.modifier(m); x && x->key == key) return m;
  return 0;
}

// Достроенная в провинции постройка с возможностью (целительство, чума).
const ProvBuilding* roleBuilding(const World& w, const Province& p, Id building, BuildingFlag flag) {
  for (const ProvBuilding& pb : p.buildings) {
    if (pb.building != building) continue;
    const Building* b = w.building(pb.building);
    if (!b || pb.builtLevel() < 1) return nullptr;
    if (flag == BuildingFlag::Healing && b->healing) return &pb;
    if (flag == BuildingFlag::Plague && b->plague) return &pb;
    return nullptr;
  }
  return nullptr;
}

Id essenceNamed(const World& w, const char* name) {
  const std::string k = utf8::searchKey(name);
  for (const CatalogItem& e : w.catalogs->essences)
    if (utf8::searchKey(e.name) == k) return e.id;
  return 0;
}

}  // namespace

bool canPlague(const World& w, Id province, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Province* p = w.province(province);
  if (!p) return no("Провинция не найдена");
  if (p->sea) return no("Чума не приходит в морскую провинцию");
  if (hasModKey(w, p->modifiers, schema::mod::Plague)) return no("В провинции " + provName(w, province) + " уже чума");
  if (hasModKey(w, p->modifiers, schema::mod::PlagueImmunity))
    return no("У провинции " + provName(w, province) + " временный иммунитет: чуму установить нельзя");
  if (hasModKey(w, p->modifiers, schema::mod::UndeadWaste)) return no("Чума не приходит в пустошь нежити");
  return true;
}

bool infectPlague(Tx& tx, Id province) {
  if (!canPlague(tx.w(), province)) return false;
  addModifier(tx, ModTarget::Province, province, ensureBuiltinMod(tx, schema::mod::Plague));
  return true;
}

void curePlague(Tx& tx, Id province) {
  needProvince(tx.w(), province);
  if (Id m = modWithKey(tx.w(), tx.w().province(province)->modifiers, schema::mod::Plague))
    dropModifier(tx, ModTarget::Province, province, m);   // снятие даёт «Временный иммунитет»
}

void healProvince(Tx& tx, Id province, Id building, Id essence) {
  const Province& p = needProvince(tx.w(), province);
  needBuilding(tx.w(), building);
  if (!roleBuilding(tx.w(), p, building, BuildingFlag::Healing))
    fail("В провинции нет достроенного здания целительства " + buildingName(tx.w(), building));
  if (!hasModKey(tx.w(), p.modifiers, schema::mod::Plague)) fail("В провинции " + provName(tx.w(), province) + " нет чумы");
  const Faction* o = tx.w().faction(p.owner);
  if (!o || !o->isState()) fail("У провинции нет владельца — платить за лечение некому");
  const CatalogItem* e = tx.w().essence(essence);
  if (!e) fail("Не выбрана эссенция");
  if (o->essence(essence) + 1e-9 < schema::kHealCost)
    fail("Недостаточно эссенции «" + e->name + "»: нужно " + amount(schema::kHealCost) + ", есть " + amount(std::max(0.0, o->essence(essence))));
  const Id owner = o->id;
  tx.faction(owner).ess[essence] -= schema::kHealCost;
  curePlague(tx, province);
  addLog(tx, LogKind::Province,
         "Провинция " + provName(tx.w(), province) + " вылечена от чумы (" + buildingName(tx.w(), building) + ", " + e->name + " " +
             amount(schema::kHealCost) + "): временный иммунитет " + nTurns(schema::kImmunityTurns),
         LogRefs{province, 0, {owner}});
}

void plagueProvince(Tx& tx, Id province, Id building) {
  const Province& p = needProvince(tx.w(), province);
  needBuilding(tx.w(), building);
  if (!roleBuilding(tx.w(), p, building, BuildingFlag::Plague)) fail("В провинции нет достроенного здания чумы " + buildingName(tx.w(), building));
  std::string why;
  if (!canPlague(tx.w(), province, &why)) fail(why);
  const Faction* o = tx.w().faction(p.owner);
  if (!o || !o->isState()) fail("У провинции нет владельца — платить за заражение некому");
  const Id ess = essenceNamed(tx.w(), "Эссенция чумы");
  if (!ess) fail("В справочнике нет «Эссенции чумы»");
  if (o->essence(ess) + 1e-9 < schema::kInfectCost)
    fail("Недостаточно эссенции «Эссенция чумы»: нужно " + amount(schema::kInfectCost) + ", есть " + amount(std::max(0.0, o->essence(ess))));
  const Id owner = o->id;
  tx.faction(owner).ess[ess] -= schema::kInfectCost;
  infectPlague(tx, province);
  addLog(tx, LogKind::Province, "Провинция " + provName(tx.w(), province) + " заражена чумой (" + buildingName(tx.w(), building) + ")",
         LogRefs{province, 0, {owner}});
}

}  // namespace rg::rules
