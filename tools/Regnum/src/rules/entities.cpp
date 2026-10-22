// Regnum — фракции, совет, персонажи и их назначения, модификаторы: создание и удаление с полной очисткой ссылок.
// Деревья построек и технологий — rules/trees.cpp, справочники — rules/catalog_lists.cpp, провинции — rules/areas.cpp,
// строки войск и флота — rules/rows.cpp.
#include "core/content.h"
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

std::vector<std::string> factionNames(const World& w) {
  std::vector<std::string> r;
  w.factions.each([&](const Faction& f) { r.push_back(f.name); });
  return r;
}

// Удалить группу фракции из войска; войско без групп удаляется. Возвращает true, если войско удалено.
bool dropGroup(Tx& tx, Id army, Id faction) {
  Army& a = tx.army(army);
  auto it = std::find_if(a.groups.begin(), a.groups.end(), [&](const ArmyGroup& g) { return g.faction == faction; });
  if (it == a.groups.end()) return false;
  if (a.commander && contains(it->heroes, a.commander)) a.commander = 0;
  a.groups.erase(it);
  if (a.groups.empty()) {
    eraseObject(tx, army);   // флот — войско на борту высаживается; войско на борту — флот освобождается
    return true;
  }
  return false;
}

// Модификатор убирается из списков сущностей (провинции, государства, войска, герои) вместе со сроком.
void dropModifierFromHolders(Tx& tx, Id modifier) {
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
}

// Модификатор убирается у археологических групп вместе со сроком.
void dropModifierFromArchGroups(Tx& tx, Id modifier) {
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) {
         return std::any_of(f.archGroups.begin(), f.archGroups.end(), [&](const ArchGroup& g) { return contains(g.modifiers, modifier); });
       }))
    for (ArchGroup& g : tx.faction(fid).archGroups) {
      eraseValue(g.modifiers, modifier);
      g.modTurns.erase(modifier);
    }
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
  const Id id = tx.add(std::move(f)).id;
  // «Гильдия Археологов», «Штаб Разведки» и «Штаб Контрразведки» — культовые постройки уникального дерева каждого
  // государства.
  if (kind == FactionKind::State) {
    tx.add(content::archGuildFor(id, nextTreePos(tx.w().buildings, [&](const Building& x) { return x.owner == id; })));
    content::addIntelBuildings(tx, id);
  }
  return id;
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
      p.garrisonHeroes.clear();
      p.garrisonLoyalty = schema::kMaxLoyalty;
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
  // Пленники и заключённые упразднённого государства освобождаются.
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& c) { return c.captor == faction || c.jailer == faction; })) {
    for (Id m : std::vector<Id>(tx.w().character(cid)->modifiers))
      if (const Modifier* x = tx.w().modifier(m); x && ((x->key == schema::mod::Captive && tx.w().character(cid)->captor == faction) ||
                                                        (x->key == schema::mod::Arrested && tx.w().character(cid)->jailer == faction)))
        dropModifier(tx, ModTarget::Character, cid, m);
    Character& c = tx.character(cid);
    if (c.captor == faction) c.captor = 0;
    if (c.jailer == faction) c.jailer = 0;
  }
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) { return p.occupier == faction && !p.occGarrison.empty(); }))
    tx.province(pid).occGarrison.clear();
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& c) { return c.faction == faction; })) tx.character(cid).faction = 0;
  for (Id mid : idsWhere(tx.w().modifiers, [&](const Modifier& m) { return contains(m.targets, faction); }))
    eraseValue(tx.modifier(mid).targets, faction);
  // Уникальные постройки фракции удаляются вместе с ней (и «Гильдия Археологов»).
  for (Id bid : idsWhere(tx.w().buildings, [&](const Building& b) { return b.owner == faction; })) {
    if (!tx.w().building(bid)->key.empty()) tx.building(bid).key.clear();
    removeBuilding(tx, bid);
  }
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
  fitPlots(tx);   // заговоры против упразднённого государства и его заговоры удаляются
  addLog(tx, LogKind::Note, std::string(state ? "Государство " : "Гильдия ") + name + (state ? " упразднено" : " упразднена"),
         LogRefs{0, 0, {faction}});
}

// ================================================================ совет
Id addCouncilSeat(Tx& tx, Id faction, const std::string& position, Id character) {
  const Faction& f = needFaction(tx.w(), faction);
  if (int(f.council.size()) >= schema::kMaxCouncilSeats)
    fail("В совете не больше " + std::to_string(schema::kMaxCouncilSeats) + " должностей");
  CouncilSeat seat;
  seat.id = tx.nextId(Seq::Council);
  const std::string p = trim(position);
  seat.position = p.empty() ? std::string("Советник") : p;
  tx.faction(faction).council.push_back(seat);
  if (character) setCouncilMember(tx, faction, seat.id, character);
  return seat.id;
}

// ================================================================ персонажи, модификаторы
Id createCharacter(Tx& tx, Id faction, const std::string& name) {
  if (faction) needFaction(tx.w(), faction);
  Character c;
  std::string n = trim(name);
  c.name = n.empty() ? "Новый персонаж" : n;
  c.faction = faction;
  Id id = tx.add(std::move(c)).id;
  // Герою государства — природа по виду государства («Живой», «Нежить», «Демон»).
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
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) { return contains(p.garrisonHeroes, character); }))
    eraseValue(tx.province(pid).garrisonHeroes, character);
}

namespace {
// Назначаемый персонаж: существует, доступен (не мёртв и не в плену) и принадлежит фракции.
void needAssignable(const World& w, Id character, Id faction, const char* role) {
  const Character& c = needCharacter(w, character);
  if (characterHas(w, character, schema::mod::Dead)) fail(q(w.characterName(character)) + " мёртв — его нельзя назначить " + role);
  if (characterHas(w, character, schema::mod::Captive)) fail(q(w.characterName(character)) + " в плену — его нельзя назначить " + role);
  if (characterHas(w, character, schema::mod::Arrested)) fail(q(w.characterName(character)) + " под арестом — его нельзя назначить " + role);
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
  const CouncilSeat* s = findById(f.council, seat);
  if (!s) fail("Место в совете не найдено");
  if (s->character == character) return;
  if (character) needAssignable(tx.w(), character, faction, "в совет");
  findById(tx.faction(faction).council, seat)->character = character;
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
  clearAssignments(tx, character);   // места в совете остаются пустыми
  tx.eraseCharacter(character);      // реликвии инвентаря освобождаются вместе с ним
  fitPlots(tx);
}

Id createModifier(Tx& tx, const std::string& name) {
  Modifier m;
  std::string n = trim(name);
  m.name = n.empty() ? "Новый модификатор" : n;
  return tx.add(std::move(m)).id;
}

void fitModifierUses(Tx& tx, Id modifier) {
  const Modifier* m = tx.w().modifier(modifier);
  if (!m) fail("Модификатор не найден");
  if (m->kind != ModKind::ArchGroup) dropModifierFromArchGroups(tx, modifier);
  else dropModifierFromHolders(tx, modifier);
}

void removeModifier(Tx& tx, Id modifier) {
  if (!tx.w().modifier(modifier)) fail(modifier ? "Модификатор не найден" : "Не выбран модификатор");
  dropModifierFromHolders(tx, modifier);
  dropModifierFromArchGroups(tx, modifier);
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
  // Модификаторы талантов подклассов героев.
  {
    bool used = false;
    for (const HeroClass& c : tx.w().catalogs->classes)
      for (const SubClass& s : c.subclasses)
        for (const Talent& t : s.talents) used = used || contains(t.modifiers, modifier);
    if (used)
      for (HeroClass& c : tx.catalogs().classes)
        for (SubClass& s : c.subclasses)
          for (Talent& t : s.talents) eraseValue(t.modifiers, modifier);
  }
  tx.eraseModifier(modifier);
}

}  // namespace rg::rules
