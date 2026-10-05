// Regnum — создание и удаление сущностей с полной очисткой ссылок, справочники, владение провинциями,
// удаление, объединение и разделение провинций, строки войск и флота.
#include "geo/ops.h"
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

constexpr i64 kMaxCount = 1000000000000000LL;  // предел численностей (точно в double)

std::vector<std::string> factionNames(const World& w) {
  std::vector<std::string> r;
  w.factions.each([&](const Faction& f) { r.push_back(f.name); });
  return r;
}

// Следующая позиция на схеме дерева: правее всех узлов дерева.
template <class T, class Pred>
Vec2 nextTreePos(const Table<T>& t, Pred&& same) {
  bool any = false;
  double x = 0;
  t.each([&](const T& e) {
    if (!same(e)) return;
    x = any ? std::max(x, e.pos.x) : e.pos.x;
    any = true;
  });
  return any ? Vec2{x + kTreeColStep, 0} : Vec2{0, 0};
}

// Удалить группу фракции из войска; войско без групп удаляется. Возвращает true, если войско удалено.
bool dropGroup(Tx& tx, Id army, Id faction) {
  Army& a = tx.army(army);
  auto it = std::find_if(a.groups.begin(), a.groups.end(), [&](const ArmyGroup& g) { return g.faction == faction; });
  if (it == a.groups.end()) return false;
  if (a.commander && contains(it->heroes, a.commander)) a.commander = 0;
  a.groups.erase(it);
  if (a.groups.empty()) {
    tx.eraseArmy(army);
    return true;
  }
  return false;
}

// Постройку удалить из провинций; незавершённое строительство возвращает уплаченное плательщику.
void dropBuildingEverywhere(Tx& tx, Id building) {
  auto ids = idsWhere(tx.w().provinces, [&](const Province& p) {
    return std::any_of(p.buildings.begin(), p.buildings.end(), [&](const ProvBuilding& b) { return b.building == building; });
  });
  for (Id pid : ids) {
    const Province& p0 = *tx.w().province(pid);
    std::optional<ProvBuilding> started;
    for (const ProvBuilding& pb : p0.buildings)
      if (pb.building == building && pb.constructing) started = pb;
    auto& list = tx.province(pid).buildings;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const ProvBuilding& b) { return b.building == building; }), list.end());
    if (!started) continue;
    if (Id got = refundPaid(tx, *started))
      addLog(tx, LogKind::Build,
             "Строительство " + buildingName(tx.w(), building) + " в провинции " + provName(tx.w(), pid) +
                 " прекращено: постройка удалена из дерева, стоимость возвращена " + facName(tx.w(), got),
             LogRefs{pid, 0, {got}});
  }
}

// Строительство в провинции, которая удаляется: уплаченное возвращается плательщикам. Возвращает число возвратов.
int refundProvinceConstruction(Tx& tx, const Province& p) {
  int n = 0;
  for (const ProvBuilding& pb : p.buildings)
    if (pb.constructing && refundPaid(tx, pb)) n++;
  return n;
}

const char* catalogNoun(CatalogList l) {
  switch (l) {
    case CatalogList::Resources: return "Ресурс";
    case CatalogList::Races: return "Раса";
    case CatalogList::Cultures: return "Культура";
    case CatalogList::Religions: return "Религия";
    case CatalogList::Governments: return "Форма правления";
    case CatalogList::Positions: return "Должность";
  }
  return "Запись";
}
const char* catalogDefault(CatalogList l) {
  switch (l) {
    case CatalogList::Resources: return "Новый ресурс";
    case CatalogList::Races: return "Новая раса";
    case CatalogList::Cultures: return "Новая культура";
    case CatalogList::Religions: return "Новая религия";
    case CatalogList::Governments: return "Новая форма правления";
    case CatalogList::Positions: return "Новая должность";
  }
  return "Новая запись";
}
Seq catalogSeq(CatalogList l) {
  switch (l) {
    case CatalogList::Resources: return Seq::Resource;
    case CatalogList::Races: return Seq::Race;
    case CatalogList::Cultures: return Seq::Culture;
    case CatalogList::Religions: return Seq::Religion;
    case CatalogList::Governments: return Seq::Government;
    case CatalogList::Positions: return Seq::Position;
  }
  return Seq::Resource;
}

}  // namespace

// ================================================================ фракции
Id createFaction(Tx& tx, FactionKind kind, const std::string& name) {
  if (kind != FactionKind::State && kind != FactionKind::Guild) fail("Неизвестный вид фракции");
  Faction f;
  f.kind = kind;
  std::string n = trim(name);
  f.name = n.empty() ? uniqueName(factionNames(tx.w()), kind == FactionKind::State ? "Новое государство" : "Новая гильдия") : n;
  f.id = tx.nextId(Seq::Faction);
  f.color = Color::palette(int(f.id));
  f.flag.colors[0] = f.color;
  f.res[kGold] = 0;
  return tx.add(std::move(f)).id;
}

void removeFaction(Tx& tx, Id faction) {
  const Faction& f0 = needFaction(tx.w(), faction);
  const bool state = f0.isState();
  const std::string name = facName(tx.w(), faction);

  // Провинции: владение, гарнизон, оккупация, влияние, штабы.
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
         return p.owner == faction || p.occupier == faction || contains(p.hqs, faction) ||
                std::any_of(p.influence.begin(), p.influence.end(), [&](const Influence& i) { return i.guild == faction; }) ||
                std::any_of(p.buildings.begin(), p.buildings.end(), [&](const ProvBuilding& b) { return b.payer == faction; });
       })) {
    Province& p = tx.province(pid);
    for (ProvBuilding& b : p.buildings)
      if (b.payer == faction) b.payer = 0;  // плательщик упразднён: при отмене возвращать некому
    if (p.owner == faction) {
      p.owner = 0;
      p.garrison.clear();
      p.slaves.clear();
    }
    if (p.occupier == faction) {
      p.occupied = false;
      p.occupier = 0;
      p.occGarrison.clear();
      p.occIdle = 0;
    }
    eraseValue(p.hqs, faction);
    p.influence.erase(std::remove_if(p.influence.begin(), p.influence.end(), [&](const Influence& i) { return i.guild == faction; }),
                      p.influence.end());
  }
  // Гильдии этого государства теряют государство расположения (государственная становится обычной).
  for (Id gid : idsWhere(tx.w().factions, [&](const Faction& g) { return g.homeState == faction; })) {
    Faction& g = tx.faction(gid);
    g.homeState = 0;
    g.stateGuild = false;
  }
  // Вассалы и мятежники упразднённого государства.
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& g) { return g.suzerain == faction || g.rebelOf == faction; })) {
    Faction& g = tx.faction(fid);
    if (g.suzerain == faction) g.suzerain = 0;
    if (g.rebelOf == faction) g.rebelOf = 0;
  }
  // Пленники упразднённого государства освобождаются.
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& c) { return c.captor == faction; })) {
    for (Id m : std::vector<Id>(tx.w().character(cid)->modifiers))
      if (const Modifier* x = tx.w().modifier(m); x && x->key == schema::mod::Captive) dropModifier(tx, ModTarget::Character, cid, m);
    tx.character(cid).captor = 0;
  }
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) { return p.occupier == faction && !p.occGarrison.empty(); }))
    tx.province(pid).occGarrison.clear();
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& c) { return c.faction == faction; })) tx.character(cid).faction = 0;
  for (Id mid : idsWhere(tx.w().modifiers, [&](const Modifier& m) { return contains(m.targets, faction); }))
    eraseValue(tx.modifier(mid).targets, faction);
  // Уникальные постройки фракции удаляются вместе с ней.
  for (Id bid : idsWhere(tx.w().buildings, [&](const Building& b) { return b.owner == faction; })) removeBuilding(tx, bid);
  for (Id tid : idsWhere(tx.w().techs, [&](const Tech& t) { return t.faction == faction; })) tx.eraseTech(tid);
  for (Id aid : idsWhere(tx.w().armies, [&](const Army& a) {
         return std::any_of(a.groups.begin(), a.groups.end(), [&](const ArmyGroup& g) { return g.faction == faction; });
       }))
    dropGroup(tx, aid, faction);
  for (Id rid : idsWhere(tx.w().routes, [&](const Route& r) { return r.guild == faction; })) tx.route(rid).guild = 0;
  for (Id did : idsWhere(tx.w().deals, [&](const Deal& d) { return d.a == faction || d.b == faction; })) tx.eraseDeal(did);
  const RelMap& rel = *tx.w().relations;
  if (std::any_of(rel.begin(), rel.end(), [&](const auto& kv) { return Id(kv.first >> 32) == faction || Id(kv.first & 0xFFFFFFFFu) == faction; })) {
    RelMap& r = tx.relations();
    for (auto it = r.begin(); it != r.end();) {
      if (Id(it->first >> 32) == faction || Id(it->first & 0xFFFFFFFFu) == faction) it = r.erase(it);
      else ++it;
    }
  }
  tx.eraseFaction(faction);
  addLog(tx, LogKind::Note, std::string(state ? "Государство " : "Гильдия ") + name + (state ? " упразднено" : " упразднена"),
         LogRefs{0, 0, {faction}});
}

// ================================================================ персонажи, модификаторы
Id createCharacter(Tx& tx, Id faction, const std::string& name) {
  if (faction) needFaction(tx.w(), faction);
  Character c;
  std::string n = trim(name);
  c.name = n.empty() ? "Новый персонаж" : n;
  c.faction = faction;
  Id id = tx.add(std::move(c)).id;
  // ТЗ «Виды государств», п.1: герою государства — природа по виду государства («Живой», «Нежить», «Демон»).
  if (const Faction* f = tx.w().faction(faction); f && f->isState()) {
    const char* key = f->stateKind == StateKind::Undead ? schema::mod::Undead : f->stateKind == StateKind::Demonic ? schema::mod::Demon : schema::mod::Living;
    addModifier(tx, ModTarget::Character, id, ensureBuiltinMod(tx, key), 0);
  }
  return id;
}

void clearAssignments(Tx& tx, Id character) {
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) { return p.lord == character; })) tx.province(pid).lord = 0;
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) {
         return f.ruler == character || std::any_of(f.council.begin(), f.council.end(), [&](const CouncilSeat& s) { return s.character == character; });
       })) {
    Faction& f = tx.faction(fid);
    if (f.ruler == character) f.ruler = 0;
    for (CouncilSeat& s : f.council)
      if (s.character == character) s.character = 0;
  }
  for (Id aid : idsWhere(tx.w().armies, [&](const Army& a) {
         if (a.commander == character) return true;
         return std::any_of(a.groups.begin(), a.groups.end(), [&](const ArmyGroup& g) { return contains(g.heroes, character); });
       })) {
    Army& a = tx.army(aid);
    if (a.commander == character) a.commander = 0;
    for (ArmyGroup& g : a.groups) eraseValue(g.heroes, character);
  }
}

namespace {
// Назначаемый персонаж: существует, доступен (не мёртв и не в плену) и принадлежит фракции (ТЗ «Фиксы», п.9).
void needAssignable(const World& w, Id character, Id faction, const char* role) {
  const Character& c = needCharacter(w, character);
  if (characterHas(w, character, schema::mod::Dead)) fail(q(w.characterName(character)) + " мёртв — его нельзя назначить " + role);
  if (characterHas(w, character, schema::mod::Captive)) fail(q(w.characterName(character)) + " в плену — его нельзя назначить " + role);
  if (c.faction != faction)
    fail(q(w.characterName(character)) + (c.faction ? " — герой " + facName(w, c.faction) : std::string(" не состоит ни в одной фракции")) +
         ": назначать " + role + " можно только героев " + facName(w, faction));
}
}  // namespace

void setRuler(Tx& tx, Id faction, Id character) {
  const Faction& f = needFaction(tx.w(), faction);
  if (f.ruler == character) return;
  if (character) needAssignable(tx.w(), character, faction, f.isState() ? "правителем" : "главой гильдии");
  tx.faction(faction).ruler = character;
}

void setCouncilMember(Tx& tx, Id faction, Id seat, Id character) {
  const Faction& f = needFaction(tx.w(), faction);
  const CouncilSeat* s = nullptr;
  for (const CouncilSeat& x : f.council)
    if (x.id == seat) s = &x;
  if (!s) fail("Место в совете не найдено");
  if (s->character == character) return;
  if (character) needAssignable(tx.w(), character, faction, "в совет");
  for (CouncilSeat& x : tx.faction(faction).council)
    if (x.id == seat) x.character = character;
}

void setLord(Tx& tx, Id province, Id character) {
  const Province& p = needProvince(tx.w(), province);
  if (p.lord == character) return;
  if (character) {
    if (!p.owner) fail("У провинции нет владельца — лорда назначает государство-владелец");
    needAssignable(tx.w(), character, p.owner, "лордом провинции");
  }
  tx.province(province).lord = character;
}

void setStateKind(Tx& tx, Id state, StateKind kind) {
  const Faction& f = needState(tx.w(), state);
  if (int(kind) < 0 || kind >= StateKind::Count) fail("Неизвестный вид государства");
  if (f.stateKind == kind) return;
  tx.faction(state).stateKind = kind;
  addLog(tx, LogKind::Note, facName(tx.w(), state) + ": " + utf8::lower(schema::stateKind(kind).name), LogRefs{0, 0, {state}});
}

void setMainState(Tx& tx, Id state, bool on) {
  const Faction& f = needState(tx.w(), state);
  if (f.mainState == on) return;
  tx.faction(state).mainState = on;
}

void removeCharacter(Tx& tx, Id character) {
  needCharacter(tx.w(), character);
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) { return p.lord == character; })) tx.province(pid).lord = 0;
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) {
         return f.ruler == character || std::any_of(f.council.begin(), f.council.end(), [&](const CouncilSeat& s) { return s.character == character; });
       })) {
    Faction& f = tx.faction(fid);
    if (f.ruler == character) f.ruler = 0;
    for (CouncilSeat& s : f.council)
      if (s.character == character) s.character = 0;  // место в совете остаётся, лорд снят
  }
  for (Id aid : idsWhere(tx.w().armies, [&](const Army& a) {
         if (a.commander == character) return true;
         return std::any_of(a.groups.begin(), a.groups.end(), [&](const ArmyGroup& g) { return contains(g.heroes, character); });
       })) {
    Army& a = tx.army(aid);
    if (a.commander == character) a.commander = 0;
    for (ArmyGroup& g : a.groups) eraseValue(g.heroes, character);
  }
  tx.eraseCharacter(character);
}

Id createModifier(Tx& tx, const std::string& name) {
  Modifier m;
  std::string n = trim(name);
  m.name = n.empty() ? "Новый модификатор" : n;
  return tx.add(std::move(m)).id;
}

void removeModifier(Tx& tx, Id modifier) {
  if (!tx.w().modifier(modifier)) fail(modifier ? "Модификатор не найден" : "Не выбран модификатор");
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) { return contains(p.modifiers, modifier); })) {
    eraseValue(tx.province(pid).modifiers, modifier);
    tx.province(pid).modTurns.erase(modifier);
  }
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) { return contains(f.modifiers, modifier); })) {
    eraseValue(tx.faction(fid).modifiers, modifier);
    tx.faction(fid).modTurns.erase(modifier);
  }
  for (Id aid : idsWhere(tx.w().armies, [&](const Army& a) { return contains(a.modifiers, modifier); })) {
    eraseValue(tx.army(aid).modifiers, modifier);
    tx.army(aid).modTurns.erase(modifier);
  }
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& c) { return contains(c.modifiers, modifier); })) {
    eraseValue(tx.character(cid).modifiers, modifier);
    tx.character(cid).modTurns.erase(modifier);
  }
  {
    const auto& pos = tx.w().catalogs->positions;
    if (std::any_of(pos.begin(), pos.end(), [&](const CatalogItem& c) { return contains(c.modifiers, modifier) || contains(c.vacantModifiers, modifier); }))
      for (CatalogItem& c : tx.catalogs().positions) {
        eraseValue(c.modifiers, modifier);
        eraseValue(c.vacantModifiers, modifier);
      }
  }
  for (Id bid : idsWhere(tx.w().buildings, [&](const Building& b) {
         return std::any_of(b.levels.begin(), b.levels.end(), [&](const BuildingLevel& l) { return contains(l.modifiers, modifier); });
       }))
    for (BuildingLevel& l : tx.building(bid).levels) eraseValue(l.modifiers, modifier);
  for (Id tid : idsWhere(tx.w().techs, [&](const Tech& t) { return contains(t.modifiers, modifier); }))
    eraseValue(tx.tech(tid).modifiers, modifier);
  tx.eraseModifier(modifier);
}

// ================================================================ постройки и технологии
Id createBuilding(Tx& tx, Id owner, const std::string& name) {
  if (owner) {
    const Faction& f = needFaction(tx.w(), owner);
    if (!f.isState()) fail("Уникальные постройки бывают только у государств: гильдия не владеет провинциями");
  }
  Building b;
  b.owner = owner;
  std::string n = trim(name);
  b.name = n.empty() ? "Новая постройка" : n;
  b.pos = nextTreePos(tx.w().buildings, [&](const Building& x) { return x.owner == owner; });
  return tx.add(std::move(b)).id;
}

void removeBuilding(Tx& tx, Id building) {
  needBuilding(tx.w(), building);
  dropBuildingEverywhere(tx, building);
  for (Id bid : idsWhere(tx.w().buildings, [&](const Building& b) {
         return std::any_of(b.requires_.begin(), b.requires_.end(), [&](const BuildingReq& r) { return r.building == building; });
       })) {
    auto& rq = tx.building(bid).requires_;
    rq.erase(std::remove_if(rq.begin(), rq.end(), [&](const BuildingReq& r) { return r.building == building; }), rq.end());
  }
  tx.eraseBuilding(building);
}

std::vector<Id> removeLastBuildingLevel(Tx& tx, Id building) {
  const Building& b = needBuilding(tx.w(), building);
  const int n = int(b.levels.size());
  if (n <= 1) fail("У постройки должен остаться хотя бы один уровень");
  std::vector<std::string> where;
  tx.w().provinces.each([&](const Province& p) {
    for (const ProvBuilding& pb : p.buildings)
      if (pb.building == building && pb.level >= n) where.push_back(p.name.empty() ? "без названия" : p.name);
  });
  if (!where.empty()) fail("Уровень " + std::to_string(n) + " уже построен или строится: " + join(where, ", "));
  tx.building(building).levels.pop_back();
  // Требования других построек к удалённому уровню опускаются до нового наибольшего.
  std::vector<Id> lowered = idsWhere(tx.w().buildings, [&](const Building& x) {
    return std::any_of(x.requires_.begin(), x.requires_.end(), [&](const BuildingReq& r) { return r.building == building && r.level >= n; });
  });
  for (Id bid : lowered)
    for (BuildingReq& r : tx.building(bid).requires_)
      if (r.building == building && r.level >= n) r.level = n - 1;
  return lowered;
}

Id createTech(Tx& tx, Id faction, const std::string& name) {
  needFaction(tx.w(), faction);
  Tech t;
  t.faction = faction;
  std::string n = trim(name);
  t.name = n.empty() ? "Новая технология" : n;
  t.pos = nextTreePos(tx.w().techs, [&](const Tech& x) { return x.faction == faction; });
  return tx.add(std::move(t)).id;
}

void removeTech(Tx& tx, Id tech) {
  needTech(tx.w(), tech);
  for (Id tid : idsWhere(tx.w().techs, [&](const Tech& t) { return contains(t.prereqs, tech); })) eraseValue(tx.tech(tid).prereqs, tech);
  tx.eraseTech(tech);
}

void copyTechTree(Tx& tx, Id from, Id to) {
  needFaction(tx.w(), from);
  needFaction(tx.w(), to);
  if (from == to) fail("Нельзя скопировать дерево технологий в ту же фракцию");
  std::vector<Tech> src;
  tx.w().techs.each([&](const Tech& t) { if (t.faction == from) src.push_back(t); });
  if (src.empty()) fail("У " + facName(tx.w(), from) + " нет технологий для копирования");
  // Копии ставятся ниже существующего дерева получателя.
  bool any = false;
  double maxY = 0, minY = 0;
  tx.w().techs.each([&](const Tech& t) {
    if (t.faction != to) return;
    maxY = any ? std::max(maxY, t.pos.y) : t.pos.y;
    any = true;
  });
  for (size_t i = 0; i < src.size(); i++) minY = i ? std::min(minY, src[i].pos.y) : src[i].pos.y;
  double dy = any ? maxY + kTreeRowStep * 2 - minY : 0;
  std::unordered_map<Id, Id> remap;
  for (const Tech& t : src) remap[t.id] = tx.nextId(Seq::Tech);
  for (const Tech& t : src) {
    Tech c = t;
    c.id = remap[t.id];
    c.faction = to;
    c.studied = false;
    c.research = false;
    c.progress = 0;
    c.pos.y += dy;
    for (Id& p : c.prereqs) p = remap.count(p) ? remap[p] : 0;
    c.prereqs.erase(std::remove(c.prereqs.begin(), c.prereqs.end(), Id(0)), c.prereqs.end());
    tx.add(std::move(c));
  }
}

// ================================================================ справочники
std::vector<CatalogItem>& catalogList(Catalogs& c, CatalogList list) {
  switch (list) {
    case CatalogList::Resources: return c.resources;
    case CatalogList::Races: return c.races;
    case CatalogList::Cultures: return c.cultures;
    case CatalogList::Religions: return c.religions;
    case CatalogList::Governments: return c.governments;
    case CatalogList::Positions: return c.positions;
  }
  fail("Неизвестный справочник");
}
const std::vector<CatalogItem>& catalogList(const Catalogs& c, CatalogList list) { return catalogList(const_cast<Catalogs&>(c), list); }

Id addCatalogItem(Tx& tx, CatalogList list, const std::string& name) {
  const auto& cur = catalogList(*tx.w().catalogs, list);
  std::vector<std::string> taken;
  for (auto& c : cur) taken.push_back(c.name);
  std::string n = trim(name);
  if (n.empty()) {
    n = uniqueName(taken, catalogDefault(list));
  } else {
    std::string k = utf8::searchKey(n);
    for (auto& c : cur)
      if (utf8::searchKey(c.name) == k) fail(std::string(catalogNoun(list)) + " " + q(c.name) + " уже есть в справочнике");
  }
  CatalogItem it;
  it.id = tx.nextId(catalogSeq(list));
  it.name = n;
  it.color = Color::palette(int(it.id) + 7 * int(list));
  catalogList(tx.catalogs(), list).push_back(std::move(it));
  return catalogList(*tx.w().catalogs, list).back().id;
}

void removeCatalogItem(Tx& tx, CatalogList list, Id item) {
  const CatalogItem* ci = Catalogs::find(catalogList(*tx.w().catalogs, list), item);
  if (!ci) fail("Запись справочника не найдена");
  if (list == CatalogList::Resources && item == kGold) fail("«Золото» — встроенный ресурс казны, его нельзя удалить");
  if (ci->builtin) fail(q(ci->name) + " — встроенная запись справочника, её нельзя удалить");
  const World& w = tx.w();
  switch (list) {
    case CatalogList::Resources: {
      for (Id pid : idsWhere(w.provinces, [&](const Province& p) { return p.resource == item; })) tx.province(pid).resource = 0;
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) { return f.res.count(item) > 0; })) tx.faction(fid).res.erase(item);
      for (Id bid : idsWhere(w.buildings, [&](const Building& b) {
             return std::any_of(b.levels.begin(), b.levels.end(), [&](const BuildingLevel& l) { return l.cost.count(item) > 0 || l.produce.count(item) > 0; });
           }))
        for (BuildingLevel& l : tx.building(bid).levels) {
          l.cost.erase(item);
          l.produce.erase(item);
        }
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) {
             return std::any_of(f.forming.begin(), f.forming.end(), [&](const Formation& q) { return q.paid.count(item) > 0; });
           }))
        for (Formation& q : tx.faction(fid).forming) q.paid.erase(item);
      {
        const auto& cs = w.constants->list;
        if (std::any_of(cs.begin(), cs.end(), [&](const Constant& c) { return c.res.count(item) > 0; }))
          for (Constant& c : tx.constants().list) c.res.erase(item);
      }
      for (Id did : idsWhere(w.deals, [&](const Deal& d) {
             return std::any_of(d.items.begin(), d.items.end(), [&](const DealItem& i) { return i.res == item; });
           })) {
        Deal& d = tx.deal(did);
        d.items.erase(std::remove_if(d.items.begin(), d.items.end(), [&](const DealItem& i) { return i.res == item; }), d.items.end());
        if (d.items.empty() && d.status == DealStatus::Active) d.status = DealStatus::Cancelled;
      }
      break;
    }
    case CatalogList::Races:
      for (Id pid : idsWhere(w.provinces, [&](const Province& p) {
             return std::any_of(p.races.begin(), p.races.end(), [&](const RacePop& r) { return r.race == item; });
           })) {
        auto& rs = tx.province(pid).races;
        rs.erase(std::remove_if(rs.begin(), rs.end(), [&](const RacePop& r) { return r.race == item; }), rs.end());
      }
      // Рабы этой расы (у государств и на работах).
      for (Id pid : idsWhere(w.provinces, [&](const Province& p) {
             return std::any_of(p.slaves.begin(), p.slaves.end(), [&](const SlaveWork& s) { return s.race == item; });
           })) {
        auto& ss = tx.province(pid).slaves;
        ss.erase(std::remove_if(ss.begin(), ss.end(), [&](const SlaveWork& s) { return s.race == item; }), ss.end());
      }
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) {
             return std::any_of(f.slaves.begin(), f.slaves.end(), [&](const SlaveGroup& s) { return s.race == item; });
           })) {
        auto& ss = tx.faction(fid).slaves;
        ss.erase(std::remove_if(ss.begin(), ss.end(), [&](const SlaveGroup& s) { return s.race == item; }), ss.end());
      }
      break;
    case CatalogList::Cultures:
      for (Id pid : idsWhere(w.provinces, [&](const Province& p) { return p.culture == item; })) tx.province(pid).culture = 0;
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) { return f.culture == item; })) tx.faction(fid).culture = 0;
      break;
    case CatalogList::Religions:
      for (Id pid : idsWhere(w.provinces, [&](const Province& p) { return p.religion == item; })) tx.province(pid).religion = 0;
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) { return f.religion == item; })) tx.faction(fid).religion = 0;
      break;
    case CatalogList::Governments:
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) { return f.government == item; })) tx.faction(fid).government = 0;
      break;
    case CatalogList::Positions:
      break;  // должность в совете хранится текстом и остаётся как есть
  }
  auto& l = catalogList(tx.catalogs(), list);
  l.erase(std::remove_if(l.begin(), l.end(), [&](const CatalogItem& c) { return c.id == item; }), l.end());
}

// ================================================================ провинции
void setProvinceOwner(Tx& tx, Id province, Id faction) {
  const Province& p0 = needProvince(tx.w(), province);
  if (faction) {
    needState(tx.w(), faction);
    if (p0.sea) fail("У морской провинции не бывает владельца");
    // ТЗ «Модификаторы», 1.3: пока действует «Опустошенная провинция», её нельзя назначить ни одному государству.
    if (hasModKey(tx.w(), p0.modifiers, schema::mod::Devastated)) fail("Опустошённую провинцию нельзя назначить ни одному государству");
  }
  const Id old = p0.owner;
  if (old == faction) return;
  // ТЗ «Общие доработки», п.13: уникальные постройки прежнего владельца сносятся (уплаченное за стройку — плательщику).
  std::vector<ProvBuilding> unique;
  for (const ProvBuilding& pb : p0.buildings)
    if (const Building* b = tx.w().building(pb.building); b && old && b->owner == old) unique.push_back(pb);
  Province& p = tx.province(province);
  p.owner = faction;
  p.garrison.clear();  // гарнизон распускается в резерв прежнего владельца
  p.slaves.clear();    // рабы прежнего владельца снимаются с работ
  if (p.occupied && p.occupier == faction) {
    p.occupied = false;
    p.occupier = 0;
    p.occGarrison.clear();
    p.occIdle = 0;
  }
  if (!unique.empty()) {
    auto& list = p.buildings;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const ProvBuilding& x) {
                 return std::any_of(unique.begin(), unique.end(), [&](const ProvBuilding& u) { return u.building == x.building; });
               }),
               list.end());
  }
  std::vector<std::string> lost;
  for (const ProvBuilding& pb : unique) {
    lost.push_back(buildingName(tx.w(), pb.building));
    if (pb.constructing) refundPaid(tx, pb);
  }
  if (const Faction* of = tx.w().faction(old); of && of->capital == province) tx.faction(old).capital = 0;
  std::string text = faction ? "Провинция " + provName(tx.w(), province) + " перешла к " + facName(tx.w(), faction)
                             : "Провинция " + provName(tx.w(), province) + " осталась без владельца";
  if (old && tx.w().faction(old)) text += " (прежний владелец — " + facName(tx.w(), old) + ")";
  if (!lost.empty()) text += ". Снесены уникальные постройки прежнего владельца: " + join(lost, ", ");
  LogRefs refs{province, 0, {}};
  if (old) refs.factions.push_back(old);
  if (faction) refs.factions.push_back(faction);
  addLog(tx, LogKind::Province, text, refs);
}

void setOccupied(Tx& tx, Id province, Id occupier) {
  const Province& p0 = needProvince(tx.w(), province);
  if (occupier == 0) {
    if (!p0.occupied && !p0.occupier) return;
    Id was = p0.occupier;
    Province& p = tx.province(province);
    p.occupied = false;
    p.occupier = 0;
    p.occGarrison.clear();   // оккупационный гарнизон — в резерв оккупанта
    p.occIdle = 0;
    LogRefs refs{province, 0, {}};
    if (was) refs.factions.push_back(was);
    if (p.owner) refs.factions.push_back(p.owner);
    addLog(tx, LogKind::War, "Оккупация провинции " + provName(tx.w(), province) + " снята", refs);
    return;
  }
  needState(tx.w(), occupier);
  if (p0.sea) fail("Морскую провинцию нельзя оккупировать");
  if (occupier == p0.owner) fail("Государство не может оккупировать собственную провинцию");
  if (p0.occupied && p0.occupier == occupier) return;
  Province& p = tx.province(province);
  p.occupied = true;
  p.occupier = occupier;
  p.occGarrison.clear();   // гарнизон прежнего оккупанта — в его резерв
  p.occIdle = 0;
  LogRefs refs{province, 0, {occupier}};
  if (p.owner) refs.factions.push_back(p.owner);
  addLog(tx, LogKind::War, "Провинция " + provName(tx.w(), province) + " оккупирована: " + facName(tx.w(), occupier), refs);
}

void setCapital(Tx& tx, Id state, Id province) {
  const Faction& f = needState(tx.w(), state);
  if (province) {
    const Province& p = needProvince(tx.w(), province);
    if (p.sea) fail("Морская провинция не может быть столичной");
    if (p.owner != state) fail("Столицей может быть только провинция самого государства");
  }
  if (f.capital == province) return;
  tx.faction(state).capital = province;
  if (province)
    addLog(tx, LogKind::Province, "Столица " + facName(tx.w(), state) + " — провинция " + provName(tx.w(), province),
           LogRefs{province, 0, {state}});
}

void deleteProvince(Tx& tx, Id province) {
  needProvince(tx.w(), province);
  geo::unassign(tx, province);
  dropProvinceRecord(tx, province, {});
}

namespace detail {
void dropProvinceRecord(Tx& tx, Id province, const std::string& why) {
  const Province p = needProvince(tx.w(), province);
  const std::string name = provName(tx.w(), province);
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) { return f.capital == province; })) tx.faction(fid).capital = 0;
  const int refunds = refundProvinceConstruction(tx, p);
  tx.eraseProvince(province);
  LogRefs refs{province, 0, {}};
  if (p.owner) refs.factions.push_back(p.owner);
  addLog(tx, LogKind::Province,
         "Провинция " + name + " удалена" + (why.empty() ? std::string(" с карты") : ": " + why) +
             (refunds ? ". Стоимость начатого строительства возвращена" : ""),
         refs);
}
}  // namespace detail

void setProvinceSea(Tx& tx, Id province, bool sea) {
  const Province& p0 = needProvince(tx.w(), province);
  if (p0.sea == sea) return;
  Province& p = tx.province(province);
  p.sea = sea;
  if (sea) {
    // Море не содержит сведений и не участвует в расчётах: гарнизон — в резерв владельца, оккупация снимается,
    // столица снимается; начатое строительство приостановлено до возвращения суши. Остальные данные (владелец,
    // население, постройки, влияние) сохраняются и снова действуют, когда провинция станет сухопутной.
    p.garrison.clear();
    p.occupied = false;
    p.occupier = 0;
    for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) { return f.capital == province; })) tx.faction(fid).capital = 0;
  }
  const Province& now = *tx.w().province(province);
  const bool paused = std::any_of(now.buildings.begin(), now.buildings.end(), [](const ProvBuilding& b) { return b.constructing; });
  LogRefs refs{province, 0, {}};
  if (now.owner) refs.factions.push_back(now.owner);
  addLog(tx, LogKind::Province,
         "Провинция " + provName(tx.w(), province) + (sea ? " стала морской" : " стала сухопутной") +
             (paused ? (sea ? ". Строительство приостановлено" : ". Строительство продолжается") : ""),
         refs);
}

void mergeProvinces(Tx& tx, Id target, Id source) {
  if (target == source) fail("Нельзя объединить провинцию с самой собой");
  const Province& t0 = needProvince(tx.w(), target);
  const Province src = needProvince(tx.w(), source);
  if (t0.sea != src.sea) fail("Нельзя объединить морскую провинцию с сухопутной");
  const std::string srcName = provName(tx.w(), source);
  geo::merge(tx, target, source);

  Province& t = tx.province(target);
  // Население: численности рас складываются.
  for (const RacePop& r : src.races) {
    auto it = std::find_if(t.races.begin(), t.races.end(), [&](const RacePop& x) { return x.race == r.race; });
    if (it != t.races.end()) it->pop = std::min(kMaxCount, it->pop + std::max<i64>(0, r.pop));
    else t.races.push_back(r);
  }
  // Гарнизон того же владельца переходит, иначе распускается в резерв.
  if (t.owner && src.owner == t.owner)
    for (const GarrisonEntry& g : src.garrison) {
      auto it = std::find_if(t.garrison.begin(), t.garrison.end(), [&](const GarrisonEntry& x) { return x.row == g.row; });
      if (it != t.garrison.end()) it->count += g.count;
      else t.garrison.push_back(g);
    }
  // Штабы — не больше пяти разных гильдий.
  std::vector<std::string> lostHq;
  for (Id g : src.hqs) {
    if (contains(t.hqs, g)) continue;
    if (int(t.hqs.size()) < schema::kMaxHqPerProvince) t.hqs.push_back(g);
    else lostHq.push_back(facName(tx.w(), g));
  }
  // Влияние: доли цели сохраняются, новые гильдии занимают свободный остаток до 100 %.
  double tsum = 0;
  for (const Influence& i : t.influence) tsum += std::max(0.0, i.pct);
  std::vector<Influence> add;
  double asum = 0;
  for (const Influence& i : src.influence) {
    if (i.pct <= 0 || std::any_of(t.influence.begin(), t.influence.end(), [&](const Influence& x) { return x.guild == i.guild; })) continue;
    add.push_back(i);
    asum += i.pct;
  }
  double room = std::max(0.0, 100.0 - tsum);
  double k = asum > room ? (asum > 0 ? room / asum : 0) : 1.0;
  for (Influence& i : add) {
    i.pct *= k;
    if (i.pct > 1e-9) t.influence.push_back(i);
  }
  // Постройки: без повторов и в пределах слотов цели.
  int slots = slotsOf(t, provinceFx(tx.w(), SourceIndex(tx.w()), t));
  std::vector<std::string> lostDup, lostSlots;
  std::vector<ProvBuilding> lostStarted;  // снесённые недостроенные: уплаченное возвращается плательщику
  for (const ProvBuilding& b : src.buildings) {
    if (std::any_of(t.buildings.begin(), t.buildings.end(), [&](const ProvBuilding& x) { return x.building == b.building; })) {
      lostDup.push_back(buildingName(tx.w(), b.building));
      if (b.constructing) lostStarted.push_back(b);
      continue;
    }
    if (int(t.buildings.size()) >= slots) {
      lostSlots.push_back(buildingName(tx.w(), b.building));
      if (b.constructing) lostStarted.push_back(b);
      continue;
    }
    t.buildings.push_back(b);
  }
  // Ссылки на исходную провинцию.
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) { return f.capital == source; }))
    tx.faction(fid).capital = fid == t.owner ? target : 0;
  tx.eraseProvince(source);
  int refunds = 0;
  for (const ProvBuilding& b : lostStarted)
    if (refundPaid(tx, b)) refunds++;

  std::string text = "Провинция " + srcName + " присоединена к " + provName(tx.w(), target);
  if (!lostSlots.empty()) text += ". Снесены из-за нехватки слотов: " + join(lostSlots, ", ");
  if (!lostDup.empty()) text += ". Снесены как повторные: " + join(lostDup, ", ");
  if (!lostHq.empty()) text += ". Не поместились штабы: " + join(lostHq, ", ");
  if (refunds) text += ". Стоимость прерванного строительства возвращена";
  LogRefs refs{target, 0, {}};
  if (t.owner) refs.factions.push_back(t.owner);
  if (src.owner && src.owner != t.owner) refs.factions.push_back(src.owner);
  addLog(tx, LogKind::Province, text, refs);
}

Id splitProvince(Tx& tx, Id province, const std::vector<Vec2>& line) {
  const Province src = needProvince(tx.w(), province);
  Id nid = geo::split(tx, province, line, [&](Tx& t, Id) {
    Province rec;
    rec.name = src.name.empty() ? std::string() : src.name + " (часть)";
    rec.sea = src.sea;
    rec.owner = src.owner;
    rec.culture = src.culture;
    rec.religion = src.religion;
    rec.contentment = src.contentment;
    rec.localTax = src.localTax;
    rec.occupied = src.occupied;
    rec.occupier = src.occupier;
    return t.add(std::move(rec)).id;
  });
  LogRefs refs{province, 0, {}};
  if (src.owner) refs.factions.push_back(src.owner);
  addLog(tx, LogKind::Province, "Провинция " + provName(tx.w(), province) + " разделена: выделена " + provName(tx.w(), nid), refs);
  return nid;
}

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
  r.race = defaultUnitRace(stateKindOf(tx.w(), faction), type);   // ТЗ «Виды государств», п.2
  tx.faction(faction).army.push_back(r);
  return r.id;
}

void setRowRace(Tx& tx, Id faction, Id row, const std::string& race) {
  const Faction& f = needFaction(tx.w(), faction);
  if (!f.armyRow(row)) fail("Строки нет в таблице войск " + facName(tx.w(), faction));
  std::string r = trim(race);
  if (r.empty()) fail("Не выбрана раса отряда");
  for (ArmyRow& x : tx.faction(faction).army)
    if (x.id == row) x.race = r;
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
  if (fleet) {
    for (FleetRow& r : m.fleet)
      if (r.id == row) r.total = total;
  } else {
    for (ArmyRow& r : m.army)
      if (r.id == row) r.total = total;
  }
}

void removeRow(Tx& tx, Id faction, Id row) {
  const Faction& f = needFaction(tx.w(), faction);
  bool fleet = f.fleetRow(row) != nullptr;
  if (!fleet && !f.armyRow(row)) fail("Строки нет в таблицах войск и флота " + facName(tx.w(), faction));
  // Формирование строки отменяется с возвратом; воины строки возвращаются в население (ТЗ «Общие доработки», п.10.4).
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

}  // namespace rg::rules
