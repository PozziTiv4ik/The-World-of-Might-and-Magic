// Regnum — мятеж войск и восстания провинций (ТЗ «Механика мятежа»): мятежное государство «Мятеж (Название)»,
// отделение неверной части войск по их верности (нежить и механизмы остаются верными, ТЗ «Виды государств», п.2),
// переход героев с «Недовольством правителем», восставшие крестьяне и рабы, переход неверных войск перед боем.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

constexpr i64 kMaxCount = 1000000000000000LL;

// Строка флота мятежного государства с теми же названием, типом и содержанием.
Id mirrorFleetRow(Tx& tx, Id rebelState, const FleetRow& src) {
  for (const FleetRow& r : tx.w().faction(rebelState)->fleet)
    if (r.name == src.name && r.type == src.type && r.upkeep == src.upkeep) return r.id;
  return addFleetRow(tx, rebelState, src.type, src.name, 0, src.upkeep);
}

// Верная при мятеже раса: нежить, механизмы и наёмники (наёмники верны нанимателю, ТЗ «Доработки №3», п.13).
bool alwaysLoyal(const World& w, Id faction, const ArmyRow* r) {
  if (!r) return false;
  if (r->merc) return true;
  const std::string race = unitRace(w, faction, *r);
  return race == schema::kRaceUndead || race == schema::kRaceMechanical || race == schema::kRaceMercenary;
}

// Перенести count отрядов строки row фракции origin в группу мятежного войска: строка-зеркало у мятежников
// (численность растёт), у прежнего государства численность строки уменьшается.
void moveUnits(Tx& tx, Id origin, Id rebelState, bool fleet, Id row, i64 count, ArmyGroup& dst) {
  if (count <= 0) return;
  Id mrow = 0;
  if (fleet) {
    const FleetRow src = *tx.w().faction(origin)->fleetRow(row);
    mrow = mirrorFleetRow(tx, rebelState, src);
    for (FleetRow& r : tx.faction(origin).fleet)
      if (r.id == row) r.total = std::max<i64>(0, r.total - count);
    for (FleetRow& r : tx.faction(rebelState).fleet)
      if (r.id == mrow) r.total = std::min(kMaxCount, r.total + count);
  } else {
    const ArmyRow src = *tx.w().faction(origin)->armyRow(row);
    mrow = mirrorRow(tx, rebelState, origin, src);
    for (ArmyRow& r : tx.faction(origin).army)
      if (r.id == row) r.total = std::max<i64>(0, r.total - count);
    for (ArmyRow& r : tx.faction(rebelState).army)
      if (r.id == mrow) r.total = std::min(kMaxCount, r.total + count);
  }
  for (ArmyUnit& u : dst.units)
    if (u.row == mrow) {
      u.count += count;
      return;
    }
  dst.units.push_back(ArmyUnit{mrow, count});
}

// Строка армии мятежников по названию (лёгкая пехота, раса «Живой»): «Мятежные крестьяне», «Восставшие рабы».
Id rebelRow(Tx& tx, Id rebelState, const char* name) {
  for (const ArmyRow& r : tx.w().faction(rebelState)->army)
    if (r.name == name && r.type == UnitType::LightInf) return r.id;
  Id id = addArmyRow(tx, rebelState, UnitType::LightInf, name, 0, 0);
  setRowRace(tx, rebelState, id, schema::kRaceLiving);
  return id;
}

// Рабы провинции с довольством ниже нуля (по расам рабов владельца): снимаются с работ и из рабов государства.
i64 takeUnhappySlaves(Tx& tx, Id province) {
  const Province* p = tx.w().province(province);
  if (!p || !p->owner || p->slaves.empty()) return 0;
  const Faction* o = tx.w().faction(p->owner);
  if (!o) return 0;
  i64 total = 0;
  std::vector<SlaveWork> keep;
  std::map<Id, i64> gone;
  for (const SlaveWork& s : p->slaves) {
    double cont = 0;
    for (const SlaveGroup& g : o->slaves)
      if (g.race == s.race) cont = g.contentment;
    if (cont < 0) {
      total += s.count;
      gone[s.race] += s.count;
    } else {
      keep.push_back(s);
    }
  }
  if (!total) return 0;
  const Id owner = p->owner;
  tx.province(province).slaves = std::move(keep);
  for (SlaveGroup& g : tx.faction(owner).slaves)
    if (auto it = gone.find(g.race); it != gone.end()) g.count = std::max<i64>(0, g.count - it->second);
  return total;
}

void addSlaveRebels(Tx& tx, Id rebelState, Id province, ArmyGroup& g) {
  const i64 n = takeUnhappySlaves(tx, province);
  if (n <= 0) return;
  Id row = rebelRow(tx, rebelState, "Восставшие рабы");
  for (ArmyRow& r : tx.faction(rebelState).army)
    if (r.id == row) r.total = std::min(kMaxCount, r.total + n);
  g.units.push_back(ArmyUnit{row, n});
}

}  // namespace

// ================================================================ мятежное государство
Id rebelStateFor(Tx& tx, Id origin) {
  const Faction& o = needState(tx.w(), origin);
  if (o.rebelOf) fail(facName(tx.w(), origin) + " — само мятежное государство");
  Id found = 0;
  tx.w().factions.each([&](const Faction& f) {
    if (!found && f.isState() && f.rebelOf == origin) found = f.id;
  });
  if (!found) {
    const std::string name = "Мятеж (" + (o.name.empty() ? std::string("Без названия") : o.name) + ")";
    const StateKind kind = o.stateKind;
    const Color base = o.color;
    found = createFaction(tx, FactionKind::State, name);
    Faction& r = tx.faction(found);
    r.rebelOf = origin;
    r.stateKind = kind;
    r.color = Color::mix(base, Color::hex(0x8b1e1e), 0.55f);
    r.flag.colors = {r.color, Color::hex(0x1d1d1d), Color::hex(0xe8d9b0)};
    r.flag.pattern = FlagPattern::Bend;
    r.flag.emblem = "swords";
    addLog(tx, LogKind::War, "Мятеж против " + facName(tx.w(), origin) + ": образовано государство " + facName(tx.w(), found),
           LogRefs{0, 0, {found, origin}});
  }
  if (tx.w().relation(found, origin).s != RelStatus::War) declareWar(tx, found, origin);
  return found;
}

// ================================================================ мятеж войск
bool canMutiny(const World& w, Id army, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Army* a = w.army(army);
  if (!a) return no("Войско не найдено");
  if (a->allied()) return no("В союзном войске мятежа не бывает — сначала распустите союз");
  // Войско на борту и флот с войском на борту (ТЗ «Доработки №3», п.6) восстают только после высадки.
  if (carrierOf(w, army) || cargoOf(w, army)) return no(a->isFleet() ? "На борту флота войско — мятеж после высадки" : "Войско на борту флота — мятеж после высадки");
  const Faction* f = w.faction(a->leader());
  if (!f || !f->isState()) return no("Мятеж бывает только в войсках государства");
  if (f->rebelOf) return no("Войско само мятежное");
  if (a->loyalty >= 0) return no("Верность войска не ниже 0 %");
  return true;
}

namespace detail {
MutinyResult mutinyArmies(Tx& tx, const std::vector<Id>& armies, Id clicked, Id garrison) {
  MutinyResult res;
  Id origin = 0, province = 0;
  ArmyKind kind = ArmyKind::Army;
  Vec2 at;
  if (clicked) {
    const Army& c0 = needArmy(tx.w(), clicked);
    origin = c0.leader();
    kind = c0.kind;
    at = c0.pos;
    province = provinceAtTx(tx, at);
  } else {
    const Province& gp = needProvince(tx.w(), garrison);
    origin = gp.owner;
    province = garrison;
    auto lab = provinceLabel(tx, garrison);
    if (!lab) fail("У провинции нет области на карте");
    at = *lab;
  }
  const bool fleet = kind == ArmyKind::Fleet;
  res.province = province;
  res.rebelState = rebelStateFor(tx, origin);
  const Id R = res.rebelState;

  ArmyGroup rebels{R, {}, {}};
  Id rebelCommander = 0;
  std::vector<Id> emptied, loyalKeep;
  std::vector<Id> orphanHeroes;   // верные герои войск, восставших целиком
  i64 moved = 0;
  for (Id aid : armies) {
    const Army* a = tx.w().army(aid);
    if (!a || a->allied() || a->leader() != origin || a->kind != kind) continue;
    const Army src = *a;
    const ArmyGroup& g = src.groups[0];
    const double p = src.loyalty < 0 ? std::min(100.0, -src.loyalty) : 0.0;
    std::map<Id, i64> take;
    for (const ArmyUnit& u : g.units) {
      if (p <= 0 || u.count <= 0) continue;
      const ArmyRow* ar = fleet ? nullptr : tx.w().faction(origin)->armyRow(u.row);
      if (!fleet && alwaysLoyal(tx.w(), origin, ar)) continue;
      i64 n = p >= 100 ? u.count : clamp<i64>(std::llround(double(u.count) * p / 100.0), 0, u.count);
      if (n > 0) take[u.row] += n;
    }
    std::vector<Id> rebelHeroes, loyalHeroes;
    for (Id h : g.heroes) (p > 0 && characterHas(tx.w(), h, schema::mod::Discontent) ? rebelHeroes : loyalHeroes).push_back(h);
    // Убрать отделившихся из войска.
    Army& m = tx.army(aid);
    for (auto& [row, n] : take) {
      for (ArmyUnit& u : m.groups[0].units)
        if (u.row == row) u.count -= n;
      moveUnits(tx, origin, R, fleet, row, n, rebels);
      moved += n;
    }
    Army& m2 = tx.army(aid);
    m2.groups[0].units.erase(std::remove_if(m2.groups[0].units.begin(), m2.groups[0].units.end(), [](const ArmyUnit& u) { return u.count <= 0; }),
                             m2.groups[0].units.end());
    for (Id h : rebelHeroes) {
      eraseValue(m2.groups[0].heroes, h);
      if (m2.commander == h) {
        m2.commander = 0;
        if (!rebelCommander) rebelCommander = h;
      }
      tx.character(h).faction = R;
      rebels.heroes.push_back(h);
    }
    if (tx.w().army(aid)->groups[0].units.empty()) {
      emptied.push_back(aid);
      for (Id h : loyalHeroes) orphanHeroes.push_back(h);
    } else {
      loyalKeep.push_back(aid);
    }
  }
  // Гарнизон провинции (ТЗ «Доработки», п.1): отделяется |верность| % каждого отряда (нежить и механизмы верны),
  // герои с «Недовольством правителем» уходят к мятежникам; оставшиеся — с верностью 0 %.
  if (!fleet && garrison) {
    const Province P = *tx.w().province(garrison);
    if (P.owner == origin && P.garrisonLoyalty < 0) {
      const double p = std::min(100.0, -P.garrisonLoyalty);
      std::map<Id, i64> take;
      for (const GarrisonEntry& g : P.garrison) {
        const ArmyRow* ar = tx.w().faction(origin)->armyRow(g.row);
        if (g.count <= 0 || !ar || alwaysLoyal(tx.w(), origin, ar)) continue;
        i64 n = p >= 100 ? g.count : clamp<i64>(std::llround(double(g.count) * p / 100.0), 0, g.count);
        if (n > 0) take[g.row] += n;
      }
      for (auto& [row, n] : take) {
        for (GarrisonEntry& g : tx.province(garrison).garrison)
          if (g.row == row) g.count -= n;
        moveUnits(tx, origin, R, false, row, n, rebels);
        moved += n;
      }
      Province& m = tx.province(garrison);
      m.garrison.erase(std::remove_if(m.garrison.begin(), m.garrison.end(), [](const GarrisonEntry& g) { return g.count <= 0; }), m.garrison.end());
      std::vector<Id> gone;
      for (Id h : P.garrisonHeroes)
        if (characterHas(tx.w(), h, schema::mod::Discontent)) gone.push_back(h);
      for (Id h : gone) {
        eraseValue(tx.province(garrison).garrisonHeroes, h);
        tx.character(h).faction = R;
        rebels.heroes.push_back(h);
        if (!rebelCommander) rebelCommander = h;
      }
      res.garrisonLoyal = !tx.w().province(garrison)->garrison.empty();
      tx.province(garrison).garrisonLoyalty = res.garrisonLoyal ? 0.0 : schema::kMaxLoyalty;
    }
  }
  // Восставшие рабы провинции (с довольством ниже нуля) присоединяются к мятежникам (ТЗ «Мятеж», п.6).
  if (!fleet && province) addSlaveRebels(tx, R, province, rebels);

  // Верная часть: всё — в одно войско (по возможности то, у которого нажали «Мятеж»), верность — 0 %.
  Id target = contains(loyalKeep, clicked) ? clicked : (loyalKeep.empty() ? 0 : loyalKeep.front());
  if (target) {
    for (Id aid : loyalKeep) {
      if (aid == target) continue;
      const Army src = *tx.w().army(aid);
      tx.eraseArmy(aid);
      Army& t = tx.army(target);
      for (const ArmyUnit& u : src.groups[0].units) {
        auto it = std::find_if(t.groups[0].units.begin(), t.groups[0].units.end(), [&](const ArmyUnit& x) { return x.row == u.row; });
        if (it != t.groups[0].units.end()) it->count += u.count;
        else t.groups[0].units.push_back(u);
      }
      for (Id h : src.groups[0].heroes)
        if (!contains(t.groups[0].heroes, h)) t.groups[0].heroes.push_back(h);
      if (!t.commander) t.commander = src.commander;
    }
    Army& t = tx.army(target);
    for (Id h : orphanHeroes)
      if (!contains(t.groups[0].heroes, h)) t.groups[0].heroes.push_back(h);
    orphanHeroes.clear();
    t.loyalty = 0;
  }
  for (Id aid : emptied)
    if (aid != target) tx.eraseArmy(aid);
  res.loyalArmy = target;
  res.full = target == 0 && !res.garrisonLoyal;
  res.loyalHeroes = orphanHeroes;

  // Войско мятежников рядом с местом мятежа.
  if (!rebels.units.empty()) {
    auto spot = Placement(tx.w(), facesFor(tx)).freeSpot(kind, at, 0);
    if (!spot) fail("Рядом нет свободного места для войска мятежников");
    Army a;
    a.kind = kind;
    a.name = std::string(fleet ? "Мятежный флот" : "Мятежное войско");
    a.pos = *spot;
    a.groups.push_back(std::move(rebels));
    a.commander = rebelCommander;
    a.loyalty = schema::kMaxLoyalty;
    res.rebelArmy = tx.add(std::move(a)).id;
  } else {
    // Без отрядов герои-мятежники остаются героями мятежного государства без войска.
  }
  addLog(tx, LogKind::War,
         "Мятеж в " + std::string(fleet ? "флоте " : "войсках ") + facName(tx.w(), origin) + ": к " + facName(tx.w(), R) + " перешло " + fmtInt(moved) +
             (res.full ? ". Войско восстало целиком" : ""),
         LogRefs{province, res.rebelArmy, {origin, R}});
  return res;
}

}  // namespace detail

MutinyResult mutiny(Tx& tx, Id army) {
  std::string why;
  if (!canMutiny(tx.w(), army, &why)) fail(why);
  const Army& a = *tx.w().army(army);
  const Id origin = a.leader();
  const Id province = provinceAtTx(tx, a.pos);
  // Все войска государства в провинции восстают разом (ТЗ «Мятеж», п.1.3), и её гарнизон — по своей верности.
  std::vector<Id> list{army};
  if (province)
    tx.w().armies.each([&](const Army& x) {
      if (x.id == army || x.allied() || x.leader() != origin || x.kind != a.kind || x.cargo || x.carrier) return;
      if (provinceAtTx(tx, x.pos) == province) list.push_back(x.id);
    });
  const Province* p = province ? tx.w().province(province) : nullptr;
  const Id garrison = p && !a.isFleet() && p->owner == origin && p->garrisonLoyalty < 0 && !p->garrison.empty() ? province : 0;
  return mutinyArmies(tx, list, army, garrison);
}

// ================================================================ мятеж гарнизона (ТЗ «Доработки», п.1)
bool canGarrisonMutiny(const World& w, Id province, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Province* p = w.province(province);
  if (!p) return no("Провинция не найдена");
  if (p->sea || !p->owner) return no("У провинции нет гарнизона владельца");
  const Faction* f = w.faction(p->owner);
  if (!f || !f->isState()) return no("Мятеж бывает только в гарнизоне государства");
  if (f->rebelOf) return no("Гарнизон сам мятежный");
  if (p->garrison.empty()) return no("В гарнизоне нет отрядов");
  if (p->garrisonLoyalty >= 0) return no("Верность гарнизона не ниже 0 %");
  return true;
}

MutinyResult garrisonMutiny(Tx& tx, Id province) {
  std::string why;
  if (!canGarrisonMutiny(tx.w(), province, &why)) fail(why);
  const Id origin = tx.w().province(province)->owner;
  // Войска владельца в провинции восстают вместе с гарнизоном (каждое — по своей верности).
  std::vector<Id> list;
  tx.w().armies.each([&](const Army& x) {
    if (x.isFleet() || x.allied() || x.leader() != origin) return;
    if (provinceAtTx(tx, x.pos) == province) list.push_back(x.id);
  });
  return mutinyArmies(tx, list, 0, province);
}

bool willGarrisonDefect(const World& w, Id rebelArmy, Id province) {
  const Army* r = w.army(rebelArmy);
  const Province* p = w.province(province);
  if (!r || !p || r->isFleet() || p->garrison.empty()) return false;
  const Faction* rf = w.faction(r->leader());
  return rf && rf->rebelOf && rf->rebelOf == p->owner && p->garrisonLoyalty < 0;
}

void garrisonDefect(Tx& tx, Id rebelArmy, Id province) {
  if (!willGarrisonDefect(tx.w(), rebelArmy, province)) fail("Гарнизон не переходит к мятежникам");
  const Province P = *tx.w().province(province);
  const Id origin = P.owner, R = tx.w().army(rebelArmy)->leader();
  const double p = std::min(100.0, -P.garrisonLoyalty);
  ArmyGroup add{R, {}, {}};
  i64 moved = 0;
  std::map<Id, i64> take;
  for (const GarrisonEntry& g : P.garrison) {
    const ArmyRow* ar = tx.w().faction(origin)->armyRow(g.row);
    if (g.count <= 0 || !ar || alwaysLoyal(tx.w(), origin, ar)) continue;
    i64 n = p >= 100 ? g.count : clamp<i64>(std::llround(double(g.count) * p / 100.0), 0, g.count);
    if (n > 0) take[g.row] = n;
  }
  for (auto& [row, n] : take) {
    for (GarrisonEntry& g : tx.province(province).garrison)
      if (g.row == row) g.count -= n;
    moveUnits(tx, origin, R, false, row, n, add);
    moved += n;
  }
  Province& m = tx.province(province);
  m.garrison.erase(std::remove_if(m.garrison.begin(), m.garrison.end(), [](const GarrisonEntry& g) { return g.count <= 0; }), m.garrison.end());
  m.garrisonLoyalty = m.garrison.empty() ? schema::kMaxLoyalty : 0.0;
  Army& r = tx.army(rebelArmy);
  for (const ArmyUnit& u : add.units) {
    auto it = std::find_if(r.groups[0].units.begin(), r.groups[0].units.end(), [&](const ArmyUnit& x) { return x.row == u.row; });
    if (it != r.groups[0].units.end()) it->count += u.count;
    else r.groups[0].units.push_back(u);
  }
  addLog(tx, LogKind::War, "К " + facName(tx.w(), R) + " перед штурмом перешло " + fmtInt(moved) + " из гарнизона провинции " + provName(tx.w(), province),
         LogRefs{province, rebelArmy, {origin, R}});
}

// ================================================================ переход перед боем
bool willDefect(const World& w, Id rebelArmy, Id target) {
  const Army* r = w.army(rebelArmy);
  const Army* t = w.army(target);
  if (!r || !t || t->allied() || r->kind != t->kind || cargoOf(w, target)) return false;   // с войском на борту — не переходит
  const Faction* rf = w.faction(r->leader());
  return rf && rf->rebelOf && rf->rebelOf == t->leader() && t->loyalty < 0;
}

void defect(Tx& tx, Id rebelArmy, Id target) {
  if (!willDefect(tx.w(), rebelArmy, target)) fail("Войско не переходит к мятежникам");
  const Army T = *tx.w().army(target);
  const Id origin = T.leader(), R = tx.w().army(rebelArmy)->leader();
  const bool fleet = T.isFleet();
  const double p = std::min(100.0, -T.loyalty);
  ArmyGroup add{R, {}, {}};
  i64 moved = 0;
  Army& t = tx.army(target);
  std::map<Id, i64> take;
  for (ArmyUnit& u : t.groups[0].units) {
    const ArmyRow* ar = fleet ? nullptr : tx.w().faction(origin)->armyRow(u.row);
    if (!fleet && alwaysLoyal(tx.w(), origin, ar)) continue;
    i64 n = p >= 100 ? u.count : clamp<i64>(std::llround(double(u.count) * p / 100.0), 0, u.count);
    if (n > 0) take[u.row] = n;
  }
  for (auto& [row, n] : take) {
    for (ArmyUnit& u : tx.army(target).groups[0].units)
      if (u.row == row) u.count -= n;
    moveUnits(tx, origin, R, fleet, row, n, add);
    moved += n;
  }
  Army& t2 = tx.army(target);
  t2.groups[0].units.erase(std::remove_if(t2.groups[0].units.begin(), t2.groups[0].units.end(), [](const ArmyUnit& u) { return u.count <= 0; }),
                           t2.groups[0].units.end());
  t2.loyalty = 0;
  if (t2.groups[0].units.empty()) tx.eraseArmy(target);
  Army& r = tx.army(rebelArmy);
  for (const ArmyUnit& u : add.units) {
    auto it = std::find_if(r.groups[0].units.begin(), r.groups[0].units.end(), [&](const ArmyUnit& x) { return x.row == u.row; });
    if (it != r.groups[0].units.end()) it->count += u.count;
    else r.groups[0].units.push_back(u);
  }
  addLog(tx, LogKind::War, "К " + facName(tx.w(), R) + " перед боем перешло " + fmtInt(moved) + " из " + armyName(tx.w(), target),
         LogRefs{0, rebelArmy, {origin, R}});
}

// ================================================================ восстание провинции
Id provinceUprising(Tx& tx, Id province) {
  const Province& p = needProvince(tx.w(), province);
  if (p.sea || !p.owner) return 0;
  const Id origin = p.owner;
  i64 pop = 0;
  for (const RacePop& r : p.races) pop += std::max<i64>(0, r.pop);
  const i64 peasants = i64(std::floor(double(pop) * schema::kUprisingShare));
  i64 unhappy = 0;
  if (const Faction* o = tx.w().faction(origin))
    for (const SlaveWork& s : p.slaves)
      for (const SlaveGroup& g : o->slaves)
        if (g.race == s.race && g.contentment < 0) unhappy += s.count;
  if (peasants <= 0 && unhappy <= 0) return 0;
  auto lab = provinceLabel(tx, province);
  if (!lab) return 0;
  auto spot = Placement(tx.w(), facesFor(tx)).freeSpot(ArmyKind::Army, *lab, 0);
  if (!spot) return 0;
  const Id R = rebelStateFor(tx, origin);
  ArmyGroup g{R, {}, {}};
  if (peasants > 0) {
    // Мятежные крестьяне — из населения провинции (по расам пропорционально).
    std::vector<i64> weights;
    for (const RacePop& r : tx.w().province(province)->races) weights.push_back(std::max<i64>(0, r.pop));
    std::vector<i64> part = splitProportional(peasants, weights);
    Province& m = tx.province(province);
    for (size_t i = 0; i < m.races.size(); i++) m.races[i].pop = std::max<i64>(0, m.races[i].pop - part[i]);
    Id row = rebelRow(tx, R, "Мятежные крестьяне");
    for (ArmyRow& r : tx.faction(R).army)
      if (r.id == row) r.total = std::min(kMaxCount, r.total + peasants);
    g.units.push_back(ArmyUnit{row, peasants});
  }
  addSlaveRebels(tx, R, province, g);
  if (g.units.empty()) return 0;
  Army a;
  a.kind = ArmyKind::Army;
  a.name = peasants > 0 ? "Мятежные крестьяне" : "Восставшие рабы";
  a.pos = *spot;
  a.groups.push_back(std::move(g));
  a.loyalty = schema::kMaxLoyalty;
  Id id = tx.add(std::move(a)).id;
  i64 n = 0;
  for (const ArmyUnit& u : tx.w().army(id)->groups[0].units) n += u.count;
  addLog(tx, LogKind::War, "В провинции " + provName(tx.w(), province) + " поднялись мятежники " + facName(tx.w(), R) + ": " + fmtInt(n),
         LogRefs{province, id, {origin, R}});
  return id;
}

}  // namespace rg::rules
