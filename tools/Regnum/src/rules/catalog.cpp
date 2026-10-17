// Regnum — справочники с поведением: группы ресурсов и реликвий (деревья), реликвии и их места, особые отряды и доступ
// к ним, запасы эссенций элементов.
#include "codec/jpeg.h"
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

// ================================================================ группы справочника (ресурсов и реликвий)
namespace {

// Дерево групп справочника: список групп, записи в группах (поле group), последовательность ID, отказы «не найдена»
// и «не выбрана».
template <class G, class Item>
struct TreeKind {
  std::vector<G> Catalogs::*groups;
  std::vector<Item> Catalogs::*items;
  Seq seq;
  const char* missing;
  const char* unchosen;
};
const TreeKind<ResGroup, CatalogItem> kResTree{&Catalogs::resGroups, &Catalogs::resources, Seq::ResGroup, "Группа ресурсов не найдена",
                                               "Не выбрана группа ресурсов"};
const TreeKind<RelicGroup, Relic> kRelicTree{&Catalogs::relicGroups, &Catalogs::relics, Seq::RelicGroup, "Группа реликвий не найдена",
                                             "Не выбрана группа реликвий"};

template <class G, class I> const std::vector<G>& groupsOf(const World& w, const TreeKind<G, I>& k) { return (*w.catalogs).*k.groups; }

template <class G, class I> const G& needTreeGroup(const World& w, const TreeKind<G, I>& k, Id group) {
  const G* g = findById(groupsOf(w, k), group);
  if (!g) fail(group ? k.missing : k.unchosen);
  return *g;
}

template <class G, class I> Id addTreeGroup(Tx& tx, const TreeKind<G, I>& k, const std::string& name, Id parent) {
  if (parent) needTreeGroup(tx.w(), k, parent);
  std::vector<std::string> taken;
  for (const G& g : groupsOf(tx.w(), k))
    if (g.parent == parent) taken.push_back(g.name);
  std::string n = trim(name);
  if (n.empty()) {
    n = uniqueName(taken, parent ? "Новая подгруппа" : "Новая группа");
  } else {
    for (const std::string& t : taken)
      if (utf8::searchKey(t) == utf8::searchKey(n)) fail("Группа «" + t + "» уже есть" + (parent ? " в этой группе" : ""));
  }
  G g;
  g.id = tx.nextId(k.seq);
  g.name = n;
  g.parent = parent;
  (tx.catalogs().*k.groups).push_back(g);
  return g.id;
}

template <class G, class I> void renameTreeGroup(Tx& tx, const TreeKind<G, I>& k, Id group, const std::string& name) {
  const G& g = needTreeGroup(tx.w(), k, group);
  const std::string n = trim(name);
  if (n.empty()) fail("Название группы не может быть пустым");
  for (const G& o : groupsOf(tx.w(), k))
    if (o.id != group && o.parent == g.parent && utf8::searchKey(o.name) == utf8::searchKey(n)) fail("Группа «" + o.name + "» уже есть рядом");
  findById(tx.catalogs().*k.groups, group)->name = n;
}

template <class G, class I> void setTreeParent(Tx& tx, const TreeKind<G, I>& k, Id group, Id parent) {
  needTreeGroup(tx.w(), k, group);
  if (parent) needTreeGroup(tx.w(), k, parent);
  if (parent == group || (parent && inTree(groupsOf(tx.w(), k), parent, group))) fail("Группа не может войти в саму себя или в свою подгруппу");
  findById(tx.catalogs().*k.groups, group)->parent = parent;
}

// Удалить группу: подгруппы и записи переходят к родителю. keep(группа) — отказ для групп правил.
template <class G, class I, class Keep> void removeTreeGroup(Tx& tx, const TreeKind<G, I>& k, Id group, Keep&& keep) {
  const G g = needTreeGroup(tx.w(), k, group);
  keep(g);
  Catalogs& c = tx.catalogs();
  for (G& x : c.*k.groups)
    if (x.parent == group) x.parent = g.parent;
  for (I& r : c.*k.items)
    if (r.group == group) r.group = g.parent;
  auto& list = c.*k.groups;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const G& x) { return x.id == group; }), list.end());
}

template <class G, class I> void setItemGroup(Tx& tx, const TreeKind<G, I>& k, Id item, Id group, const char* notFound) {
  if (!findById((*tx.w().catalogs).*k.items, item)) fail(notFound);
  if (group) needTreeGroup(tx.w(), k, group);
  findById(tx.catalogs().*k.items, item)->group = group;
}

// Записи группы с подгруппами в порядке справочника (0 — все).
template <class G, class I> std::vector<Id> itemsIn(const World& w, const TreeKind<G, I>& k, Id group) {
  std::vector<Id> out;
  for (const I& r : (*w.catalogs).*k.items)
    if (!group || inTree(groupsOf(w, k), r.group, group)) out.push_back(r.id);
  return out;
}

template <class G, class I> std::vector<Id> childTreeGroups(const World& w, const TreeKind<G, I>& k, Id parent) {
  std::vector<Id> out;
  for (const G& g : groupsOf(w, k))
    if (g.parent == parent) out.push_back(g.id);
  return out;
}

// Путь группы: «Звери / Ездовые наземные».
template <class G, class I> std::string treePath(const World& w, const TreeKind<G, I>& k, Id group) {
  std::vector<std::string> parts;
  for (int guard = 0; group && guard < 64; guard++) {
    const G* g = findById(groupsOf(w, k), group);
    if (!g) break;
    parts.insert(parts.begin(), nameOr(g->name));
    group = g->parent;
  }
  return join(parts, " / ");
}

}  // namespace

// ---- группы ресурсов
Id addResGroup(Tx& tx, const std::string& name, Id parent) { return addTreeGroup(tx, kResTree, name, parent); }
void renameResGroup(Tx& tx, Id group, const std::string& name) { renameTreeGroup(tx, kResTree, group, name); }
void setGroupParent(Tx& tx, Id group, Id parent) { setTreeParent(tx, kResTree, group, parent); }
void removeResGroup(Tx& tx, Id group) {
  removeTreeGroup(tx, kResTree, group, [](const ResGroup& g) {
    if (schema::isRuleGroup(g.key)) fail(q(nameOr(g.name)) + " нужна правилам (провизия, ключевые ресурсы войск) — её нельзя удалить");
  });
}
void setResourceGroup(Tx& tx, Id resource, Id group) { setItemGroup(tx, kResTree, resource, group, "Ресурс не найден"); }
std::vector<Id> resourcesIn(const World& w, Id group) { return itemsIn(w, kResTree, group); }
std::vector<Id> childGroups(const World& w, Id parent) { return childTreeGroups(w, kResTree, parent); }
std::string groupPath(const World& w, Id group) { return treePath(w, kResTree, group); }

std::vector<Id> provisionResources(const World& w) {
  const Id g = w.catalogs->groupId(schema::grp::Provisions);
  return g ? resourcesIn(w, g) : std::vector<Id>{};
}

// ---- группы реликвий
Id addRelicGroup(Tx& tx, const std::string& name, Id parent) { return addTreeGroup(tx, kRelicTree, name, parent); }
void renameRelicGroup(Tx& tx, Id group, const std::string& name) { renameTreeGroup(tx, kRelicTree, group, name); }
void setRelicGroupParent(Tx& tx, Id group, Id parent) { setTreeParent(tx, kRelicTree, group, parent); }
void removeRelicGroup(Tx& tx, Id group) {
  removeTreeGroup(tx, kRelicTree, group, [](const RelicGroup& g) {
    if (g.key == schema::kRelicArchFinds)
      fail(q(nameOr(g.name)) + " нужна археологии (находки героям напрямую не назначаются) — её нельзя удалить");
  });
}
void setRelicGroup(Tx& tx, Id relic, Id group) { setItemGroup(tx, kRelicTree, relic, group, "Реликвия не найдена"); }
std::vector<Id> relicsIn(const World& w, Id group) { return itemsIn(w, kRelicTree, group); }
std::vector<Id> childRelicGroups(const World& w, Id parent) { return childTreeGroups(w, kRelicTree, parent); }
std::string relicGroupPath(const World& w, Id group) { return treePath(w, kRelicTree, group); }

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
      if (utf8::searchKey(t) == utf8::searchKey(n)) fail("Реликвия «" + t + "» уже есть в справочнике");
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
    if (r.id != relic && utf8::searchKey(r.name) == utf8::searchKey(n)) fail("Реликвия «" + r.name + "» уже есть в справочнике");
  Relic& r = *findById(tx.catalogs().relics, relic);
  r.name = n;
  r.rarity = rarity;
  r.desc = desc;
}

void removeRelic(Tx& tx, Id relic) {
  if (!tx.w().relic(relic)) fail("Реликвия не найдена");
  for (const RelicAt& a : relicPlaces(tx.w(), relic)) moveRelic(tx, relic, a.place, RelicPlace{}, a.count);
  auto& list = tx.catalogs().relics;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const Relic& r) { return r.id == relic; }), list.end());
}

namespace {

// Список стопок места (nullptr — у места нет списка: тайник, свободно).
std::vector<RelicStack>* stacksAt(Tx& tx, const RelicPlace& at) {
  switch (at.kind) {
    case RelicPlace::Hero: return &tx.character(at.id).inventory;
    case RelicPlace::State: return &tx.faction(at.id).relics;
    case RelicPlace::Building:
      for (ProvBuilding& pb : tx.province(at.id).buildings)
        if (pb.building == at.building) return &pb.relics;
      fail("Постройки нет в провинции");
    default: return nullptr;
  }
}

void addStack(std::vector<RelicStack>& v, Id relic, i64 n) {
  for (RelicStack& s : v)
    if (s.relic == relic) {
      s.count = std::min(schema::kMaxCount, s.count + n);
      return;
    }
  v.push_back(RelicStack{relic, n});
}

// Взять n штук из списка (не хватает — отказ).
void takeStack(std::vector<RelicStack>& v, Id relic, i64 n, const std::string& name) {
  for (size_t i = 0; i < v.size(); i++)
    if (v[i].relic == relic) {
      if (v[i].count < n) fail("Не хватает реликвии " + name + ": нужно " + fmtInt(n) + ", есть " + fmtInt(v[i].count));
      v[i].count -= n;
      if (v[i].count <= 0) v.erase(v.begin() + long(i));
      return;
    }
  fail("Реликвии " + name + " здесь нет");
}

}  // namespace

std::vector<RelicAt> relicPlaces(const World& w, Id relic) {
  std::vector<RelicAt> out;
  if (!relic) return out;
  w.characters.each([&](const Character& c) {
    if (i64 n = relicCount(c.inventory, relic)) out.push_back(RelicAt{RelicPlace{RelicPlace::Hero, c.id, 0, c.faction}, n});
  });
  w.provinces.each([&](const Province& p) {
    if (p.hiddenRelic == relic) out.push_back(RelicAt{RelicPlace{RelicPlace::Hidden, p.id, 0, p.owner}, 1});
    for (const ProvBuilding& pb : p.buildings)
      if (i64 n = relicCount(pb.relics, relic)) out.push_back(RelicAt{RelicPlace{RelicPlace::Building, p.id, pb.building, p.owner}, n});
  });
  w.factions.each([&](const Faction& f) {
    if (i64 n = relicCount(f.relics, relic)) out.push_back(RelicAt{RelicPlace{RelicPlace::State, f.id, 0, f.id}, n});
  });
  return out;
}

RelicPlace relicPlace(const World& w, Id relic) {
  std::vector<RelicAt> at = relicPlaces(w, relic);
  return at.empty() ? RelicPlace{} : at.front().place;
}

i64 relicCountAt(const World& w, Id relic, const RelicPlace& at) {
  switch (at.kind) {
    case RelicPlace::Hero: {
      const Character* c = w.character(at.id);
      return c ? relicCount(c->inventory, relic) : 0;
    }
    case RelicPlace::State: {
      const Faction* f = w.faction(at.id);
      return f ? relicCount(f->relics, relic) : 0;
    }
    case RelicPlace::Building: {
      const Province* p = w.province(at.id);
      if (p)
        for (const ProvBuilding& pb : p->buildings)
          if (pb.building == at.building) return relicCount(pb.relics, relic);
      return 0;
    }
    case RelicPlace::Hidden: {
      const Province* p = w.province(at.id);
      return p && p->hiddenRelic == relic ? 1 : 0;
    }
    case RelicPlace::Free: return 0;
  }
  return 0;
}

bool relicFree(const World& w, Id relic) {
  const Relic* r = w.relic(relic);
  return r && (!r->unique() || relicPlaces(w, relic).empty());
}

Id relicHolder(const World& w, Id relic) {
  for (const RelicAt& a : relicPlaces(w, relic))
    if (a.place.kind == RelicPlace::Hero) return a.place.id;
  return 0;
}

bool isArchFind(const World& w, Id relic) {
  const Relic* r = w.relic(relic);
  const Id root = w.catalogs->relicGroupId(schema::kRelicArchFinds);
  return r && root && w.catalogs->inRelicGroup(r->group, root);
}

void moveRelic(Tx& tx, Id relic, const RelicPlace& from, const RelicPlace& to, i64 count) {
  const Relic* r = tx.w().relic(relic);
  if (!r) fail("Реликвия не найдена");
  if (count < 1) fail("Количество — не меньше 1");
  const std::string name = q(r->name);
  if (r->unique()) {
    if (count != 1) fail(name + " — уникальная реликвия: одна штука");
    if (to.kind != RelicPlace::Free) {
      const std::vector<RelicAt> at = relicPlaces(tx.w(), relic);
      const bool here = !at.empty() && at.front().place == from;
      if (!at.empty() && !here) fail(name + " — уникальная реликвия и уже лежит в другом месте");
    }
  }
  if (from == to) return;
  // Снять из места.
  switch (from.kind) {
    case RelicPlace::Free:
      if (r->unique() && !relicPlaces(tx.w(), relic).empty()) fail(name + " — уникальная реликвия и уже лежит в другом месте");
      break;
    case RelicPlace::Hidden: {
      const Province& p0 = needProvince(tx.w(), from.id);
      if (p0.hiddenRelic != relic) fail("Реликвии " + name + " нет в тайнике");
      if (count != 1) fail("В тайнике — одна штука");
      Province& p = tx.province(from.id);
      p.hiddenRelic = 0;
      p.hiddenBy = 0;
      break;
    }
    default: takeStack(*stacksAt(tx, from), relic, count, name);
  }
  // Положить в место.
  switch (to.kind) {
    case RelicPlace::Free: break;
    case RelicPlace::Hidden: {
      Province& p = tx.province(to.id);
      if (p.hiddenRelic) fail("В провинции уже спрятана реликвия");
      if (count != 1) fail("Спрятать можно одну штуку");
      p.hiddenRelic = relic;
      p.hiddenBy = to.owner;
      break;
    }
    default: addStack(*stacksAt(tx, to), relic, count);
  }
}

void moveRelic(Tx& tx, Id relic, const RelicPlace& to) {
  const Relic* r = tx.w().relic(relic);
  if (!r) fail("Реликвия не найдена");
  // Уникальная — из её места; копируемая — новая штука из справочника.
  moveRelic(tx, relic, r->unique() ? relicPlace(tx.w(), relic) : RelicPlace{}, to, 1);
}

void setRelicCount(Tx& tx, Id relic, const RelicPlace& at, i64 count) {
  const Relic* r = tx.w().relic(relic);
  if (!r) fail("Реликвия не найдена");
  if (count < 0) fail("Количество не может быть меньше нуля");
  if (count > schema::kMaxCount) fail("Слишком большое количество");
  if (at.kind == RelicPlace::Free || at.kind == RelicPlace::Hidden) fail("Количество задаётся в инвентаре или хранилище");
  const i64 have = relicCountAt(tx.w(), relic, at);
  if (count == have) return;
  if (count > have) moveRelic(tx, relic, RelicPlace{}, at, count - have);
  else moveRelic(tx, relic, at, RelicPlace{}, have - count);
}

namespace {

// Источник для героя или хранилища: уникальная — её место; копируемая — инвентарь государства (если там есть),
// иначе новая штука из справочника.
RelicPlace defaultSource(const World& w, Id relic, Id state) {
  const Relic* r = w.relic(relic);
  if (!r) return {};
  if (r->unique()) return relicPlace(w, relic);
  if (state && relicCountAt(w, relic, RelicPlace{RelicPlace::State, state, 0, state}) > 0) return RelicPlace{RelicPlace::State, state, 0, state};
  return {};
}

}  // namespace

void giveRelic(Tx& tx, Id character, Id relic, std::optional<RelicPlace> from0) {
  const Character& c0 = needCharacter(tx.w(), character);
  const Relic* r = tx.w().relic(relic);
  if (!r) fail("Реликвия не найдена");
  const RelicPlace from = from0 ? *from0 : defaultSource(tx.w(), relic, c0.faction);
  const RelicPlace to{RelicPlace::Hero, character, 0, c0.faction};
  if (from == to) return;
  if (r->unique() && relicCountAt(tx.w(), relic, to) > 0) return;
  if (!r->unique() && relicCountAt(tx.w(), relic, to) > 0 && from.kind == RelicPlace::Free)
    fail(q(r->name) + " уже есть у " + q(tx.w().characterName(character)));
  // Находки археологов — героям только из владений их государства, не из свободных.
  if (isArchFind(tx.w(), relic) && (from.kind == RelicPlace::Free || !c0.faction || from.owner != c0.faction))
    fail(q(r->name) + " — археологическая находка: героям её не назначают напрямую, только из владений их государства");
  const std::string rn = q(r->name);
  const Id was = from.kind == RelicPlace::Hero ? from.id : 0;
  moveRelic(tx, relic, from, to, 1);
  const Character& c = *tx.w().character(character);
  LogRefs refs{0, 0, c.faction ? std::vector<Id>{c.faction} : std::vector<Id>{}};
  addLog(tx, LogKind::Note,
         "Реликвия " + rn + (was ? " перешла от " + q(tx.w().characterName(was)) + " к " : " у ") + q(tx.w().characterName(character)), refs);
}

void takeRelic(Tx& tx, Id character, Id relic) {
  const Character& c = needCharacter(tx.w(), character);
  if (!relicCount(c.inventory, relic)) return;
  // Из инвентаря героя — в инвентарь его государства (если оно есть), иначе реликвия исчезает из мира.
  const Faction* f = tx.w().faction(c.faction);
  moveRelic(tx, relic, RelicPlace{RelicPlace::Hero, character, 0, c.faction},
            f && f->isState() ? RelicPlace{RelicPlace::State, f->id, 0, f->id} : RelicPlace{}, 1);
}

void storeRelic(Tx& tx, Id province, Id building, Id relic, std::optional<RelicPlace> from0, i64 count) {
  const Province& p = needProvince(tx.w(), province);
  const Building& b = needBuilding(tx.w(), building);
  const Relic* r = tx.w().relic(relic);
  if (!r) fail("Реликвия не найдена");
  if (!b.relicStore) fail(buildingName(tx.w(), building) + " — не хранилище реликвий");
  const ProvBuilding* pb = nullptr;
  for (const ProvBuilding& x : p.buildings)
    if (x.building == building) pb = &x;
  if (!pb) fail("Постройки " + buildingName(tx.w(), building) + " нет в провинции");
  if (pb->builtLevel() < 1) fail(buildingName(tx.w(), building) + " ещё не достроена");
  if (!p.owner) fail("У провинции нет владельца");
  const RelicPlace to{RelicPlace::Building, province, building, p.owner};
  const RelicPlace from = from0 ? *from0 : defaultSource(tx.w(), relic, p.owner);
  if (from == to) return;
  // Из инвентаря государства-владельца, его героев или (правка мира) свободных из справочника.
  const bool ok = from.kind == RelicPlace::Free || (from.kind == RelicPlace::State && from.id == p.owner) ||
                  (from.kind == RelicPlace::Hero && from.owner == p.owner) || (from.kind == RelicPlace::Building && from.owner == p.owner);
  if (!ok) fail("В хранилище кладут реликвии инвентаря государства, его героев или других его хранилищ");
  moveRelic(tx, relic, from, to, count);
}

void unstoreRelic(Tx& tx, Id province, Id building, Id relic, i64 count) {
  const Province& p = needProvince(tx.w(), province);
  const RelicPlace at{RelicPlace::Building, province, building, p.owner};
  if (relicCountAt(tx.w(), relic, at) < 1) fail("Реликвии нет в этой постройке");
  const Faction* f = tx.w().faction(p.owner);
  moveRelic(tx, relic, at, f && f->isState() ? RelicPlace{RelicPlace::State, f->id, 0, f->id} : RelicPlace{}, count);
}

void releaseStateRelic(Tx& tx, Id state, Id relic, i64 count) {
  needFaction(tx.w(), state);
  const RelicPlace at{RelicPlace::State, state, 0, state};
  if (relicCountAt(tx.w(), relic, at) < 1) fail("Реликвии нет у государства");
  moveRelic(tx, relic, at, RelicPlace{}, std::min(count, relicCountAt(tx.w(), relic, at)));
}

double relicPrice(const World& w, Id relic) {
  const Relic* r = w.relic(relic);
  if (!r) return 0;
  const int k = std::clamp(int(r->rarity), 0, int(Rarity::Count) - 1);
  return isArchFind(w, relic) ? schema::kRelicPriceArch[k] : schema::kRelicPrice[k];
}

std::vector<RelicAt> sellableRelics(const World& w, Id state) {
  std::vector<RelicAt> out;
  w.provinces.each([&](const Province& p) {
    if (p.owner != state || p.sea) return;
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      if (!b || !b->relicStore || pb.builtLevel() < 1) continue;
      for (const RelicStack& s : pb.relics)
        if (w.relic(s.relic) && s.count > 0) out.push_back(RelicAt{RelicPlace{RelicPlace::Building, p.id, pb.building, state}, s.count, s.relic});
    }
  });
  return out;
}

bool hasMarket(const World& w, Id state) {
  bool any = false;
  w.provinces.each([&](const Province& p) {
    if (any || p.owner != state || p.sea) return;
    for (const ProvBuilding& pb : p.buildings)
      if (const Building* b = w.building(pb.building); b && b->market && pb.builtLevel() >= 1) any = true;
  });
  return any;
}

double sellRelics(Tx& tx, Id state, const RelicPlace& at, Id relic, i64 count) {
  const Faction& f = needState(tx.w(), state);
  const Relic* r = tx.w().relic(relic);
  if (!r) fail("Реликвия не найдена");
  if (!hasMarket(tx.w(), state)) fail("У государства " + facName(tx.w(), state) + " нет достроенного рынка");
  if (at.kind != RelicPlace::Building || at.owner != state) fail("Продаются реликвии из хранилищ реликвий государства");
  const Province& p = needProvince(tx.w(), at.id);
  if (p.owner != state) fail("Хранилище не в провинции государства");
  const Building& b = needBuilding(tx.w(), at.building);
  if (!b.relicStore) fail(buildingName(tx.w(), at.building) + " — не хранилище реликвий");
  if (count < 1) fail("Количество — не меньше 1");
  const i64 have = relicCountAt(tx.w(), relic, at);
  if (have < count) fail("В хранилище " + fmtInt(have) + " шт. реликвии " + q(r->name));
  const double gold = relicPrice(tx.w(), relic) * double(count);
  const std::string name = q(r->name);
  moveRelic(tx, relic, at, RelicPlace{}, count);
  addStock(tx.faction(state), kGold, gold);
  addLog(tx, LogKind::Trade,
         nameOr(f.name) + " продаёт на рынке " + name + (count > 1 ? " × " + fmtInt(count) : std::string()) + ": " + amount(gold) + " тыс. золота",
         LogRefs{at.id, 0, {state}});
  return gold;
}

void sortByRarity(const World& w, std::vector<Id>& relics) {
  std::stable_sort(relics.begin(), relics.end(), [&](Id x, Id y) {
    const Relic* a = w.relic(x);
    const Relic* b = w.relic(y);
    if (!a || !b) return a != nullptr;
    if (a->rarity != b->rarity) return rarityAbove(a->rarity, b->rarity);
    return compareRu(a->name, b->name) < 0;
  });
}

void setRelicImage(Tx& tx, Id relic, const std::string& bytes) {
  if (!tx.w().relic(relic)) fail("Реликвия не найдена");
  constexpr size_t kMaxBytes = size_t(4) << 20;
  if (bytes.size() > kMaxBytes) fail("Изображение реликвии слишком большое (больше 4 МБ)");
  if (!bytes.empty()) {
    auto img = codec::decodeImage(bytes);
    if (!img || img->empty()) fail("Файл не похож на изображение PNG или JPEG");
  }
  findById(tx.catalogs().relics, relic)->image = bytes;
}

void setRelicEntity(Tx& tx, Id relic, const std::string& entity) {
  if (!tx.w().relic(relic)) fail("Реликвия не найдена");
  const std::string e = trim(entity);
  if (e.size() > 64) fail("ID карточки слишком длинный");
  findById(tx.catalogs().relics, relic)->entity = e;
}

namespace {

// Строки армии особого отряда повторяют его запись (после правки справочника).
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

}  // namespace

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
  checkKey(tx.w(), s.type, s.keyRes, s.keyRes ? s.keyPer : 1, true);
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
