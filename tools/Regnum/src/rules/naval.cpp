// Regnum — флот (ТЗ «Доработки №3», п.3, 6; «Доработки №4», п.9): вместимость, посадка войска на флот и высадка,
// обмен отрядами между объектами одной фракции, возврат войска из союзного объекта, новый флот у верфи.
//
// Войско на борту — обычная запись Army с carrier (флот), у флота — cargo (войско). Его положение всегда равно
// положению флота (syncCargo после перемещения флота), поэтому проверки «войска в провинции» его не видят: флот стоит
// на море. Размещение, встречи и попадание мышью войско на борту не учитывают (aboard).
#include "geo/geom.h"
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

std::vector<Id> factionsOf(const Army& a) {
  std::vector<Id> r;
  for (const ArmyGroup& g : a.groups)
    if (!contains(r, g.faction)) r.push_back(g.faction);
  return r;
}

i64 unitsOf(const Army& a) {
  i64 n = 0;
  for (const ArmyGroup& g : a.groups)
    for (const ArmyUnit& u : g.units) n += u.count;
  return n;
}

// Все фракции a есть в b.
bool subsetOf(const Army& a, const Army& b) {
  const std::vector<Id> fb = factionsOf(b);
  for (Id f : factionsOf(a))
    if (!contains(fb, f)) return false;
  return !a.groups.empty();
}

bool seaProvince(const World& w, Id p) {
  const Province* x = p ? w.province(p) : nullptr;
  return x && x->sea;
}

// Расстояние от точки до области провинции (до её границы; точка внутри — 0).
double distToProvince(const geo::FaceSet& fs, Id province, Vec2 p) {
  const geo::ProvinceShape* sh = fs.shape(province);
  if (!sh) return kInf;
  double best = kInf;
  for (int fi : sh->faces) {
    const geo::Face& f = fs.faces[size_t(fi)];
    if (geo::pointInPolygon(p, f.rings)) return 0;
    best = std::min(best, geo::distToRings(p, f.rings));
  }
  return best;
}

// Сухопутные провинции, граничащие с провинцией (по дугам графа).
std::vector<Id> landNeighbors(const World& w, Id province) {
  std::vector<Id> out;
  w.edges.each([&](const Edge& e) {
    const Id other = e.pl == province ? e.pr : e.pr == province ? e.pl : 0;
    if (!other || other == province || contains(out, other)) return;
    const Province* o = w.province(other);
    if (o && !o->sea) out.push_back(other);
  });
  std::sort(out.begin(), out.end());
  return out;
}

std::string provList(const World& w, const std::vector<Id>& ids) {
  std::vector<std::string> names;
  for (Id p : ids) names.push_back(provName(w, p));
  return join(names, ", ");
}

// Место посадки: войско в приморской провинции P; флот в соседней с P морской провинции, а если у P нет соседних
// морских провинций или флот стоит в неназначенном море — недалеко от P (войско вне провинций — недалеко от войска).
bool boardingPlace(const World& w, const Army& army, const Army& fleet, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  auto fs = geo::faces(w);
  if (!fs) return no("Карта ещё не создана");
  const Id P = fs->provinceAt(army.pos);
  const Id F = fs->provinceAt(fleet.pos);
  if (!P || seaProvince(w, P)) {
    if (dist(army.pos, fleet.pos) <= kNavalReach + schema::kObjectRadius) return true;
    return no("Флот далеко от войска");
  }
  if (!isCoastal(w, P)) return no("Посадка — только из приморской провинции: " + provName(w, P) + " не граничит с морем");
  const std::vector<Id> S = seaNeighbors(w, P);
  if (seaProvince(w, F) && contains(S, F)) return true;
  const bool near = distToProvince(*fs, P, fleet.pos) <= kNavalReach;
  if (S.empty()) return near ? true : no("Флот далеко от провинции " + provName(w, P));
  if (!seaProvince(w, F) && near) return true;   // неназначенное море у берега P
  return no("Флот должен стоять в морской провинции у " + provName(w, P) + ": " + provList(w, S));
}

// Общие проверки посадки и обмена с войском на борту.
bool boardingPair(const World& w, Id army, Id fleet, const Army*& a, const Army*& f, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  a = w.army(army);
  f = w.army(fleet);
  if (!a || !f) return no("Войско не найдено");
  if (a->isFleet() || !f->isFleet()) return no("На флот садится войско");
  if (aboard(w, *a)) return no(armyName(w, army) + " уже на борту флота");
  if (!subsetOf(*a, *f)) return no("Войско садится только на флот своего государства");
  return true;
}

// Объекты фракции faction: группа и её герои.
const ArmyGroup* groupIn(const Army& a, Id faction) {
  for (const ArmyGroup& g : a.groups)
    if (g.faction == faction) return &g;
  return nullptr;
}

}  // namespace

// ================================================================ вместимость
i64 shipCapacity(const World& w, ShipType type) {
  const char* key = schema::shipCapacityKey(type);
  if (!key) return 0;
  const double v = constantOf(w, key).num;
  return v > 0 && std::isfinite(v) ? i64(std::llround(v)) : 0;
}

i64 fleetCapacity(const World& w, Id fleet) {
  const Army* a = w.army(fleet);
  if (!a || !a->isFleet()) return 0;
  i64 cap = 0;
  for (const ArmyGroup& g : a->groups) {
    const Faction* f = w.faction(g.faction);
    if (!f) continue;
    for (const ArmyUnit& u : g.units)
      if (const FleetRow* r = f->fleetRow(u.row)) cap += u.count * shipCapacity(w, r->type);
  }
  return cap;
}

i64 armySize(const World& w, Id army) {
  const Army* a = w.army(army);
  return a ? unitsOf(*a) : 0;
}

Id cargoOf(const World& w, Id fleet) {
  const Army* f = w.army(fleet);
  if (!f || !f->isFleet() || !f->cargo) return 0;
  const Army* c = w.army(f->cargo);
  return c && c->carrier == fleet ? c->id : 0;
}

Id carrierOf(const World& w, Id army) {
  const Army* a = w.army(army);
  return a && aboard(w, *a) ? a->carrier : 0;
}

namespace detail {

bool aboard(const World& w, const Army& a) {
  if (a.isFleet() || !a.carrier) return false;
  const Army* f = w.army(a.carrier);
  return f && f->isFleet() && f->cargo == a.id;
}

void syncCargo(Tx& tx, Id fleet) {
  const Id c = cargoOf(tx.w(), fleet);
  if (!c) return;
  const Vec2 p = tx.w().army(fleet)->pos;
  if (tx.w().army(c)->pos != p) tx.army(c).pos = p;
}

void eraseObject(Tx& tx, Id army) {
  const Army* cur = tx.w().army(army);
  if (!cur) return;
  const Army a = *cur;
  if (a.isFleet()) {
    if (const Id c = cargoOf(tx.w(), army)) {
      // Флот исчезает не в бою (упразднение фракции): войско на борту высаживается на ближайшую свободную сушу.
      tx.army(c).carrier = 0;
      tx.army(army).cargo = 0;
      auto spot = Placement(tx.w(), facesFor(tx)).freeSpot(ArmyKind::Army, a.pos, c);
      if (spot) {
        tx.army(c).pos = *spot;
        addLog(tx, LogKind::Army, armyName(tx.w(), c) + " высажено на берег: флот " + armyName(tx.w(), army) + " исчез", LogRefs{provinceAtTx(tx, *spot), c, factionsOf(a)});
      } else {
        tx.eraseArmy(c);
      }
    }
  } else if (aboard(tx.w(), a)) {
    tx.army(a.carrier).cargo = 0;
  }
  tx.eraseArmy(army);
}

void combineCargo(Tx& tx, Id target, Id source) {
  const Id cs = cargoOf(tx.w(), source);
  if (!cs) return;
  const Id ct = cargoOf(tx.w(), target);
  tx.army(source).cargo = 0;
  if (!ct) {
    tx.army(cs).carrier = target;
    tx.army(target).cargo = cs;
    syncCargo(tx, target);
    return;
  }
  // На обоих флотах войска: войско с source складывается с войском на борту target по группам фракций (союзники —
  // союзное войско под управлением войска target). Фракции флотов одни и те же или в союзе — как и их войск.
  const Army B = *tx.w().army(cs);
  const bool sameState = subsetOf(B, *tx.w().army(ct));
  const std::string an = armyName(tx.w(), ct), bn = armyName(tx.w(), cs);
  tx.eraseArmy(cs);
  Army& m = tx.army(ct);
  const bool wasAllied = m.allied();
  for (const ArmyGroup& g : B.groups) {
    auto it = std::find_if(m.groups.begin(), m.groups.end(), [&](const ArmyGroup& x) { return x.faction == g.faction; });
    if (it == m.groups.end()) {
      m.groups.push_back(g);
      continue;
    }
    for (const ArmyUnit& u : g.units) {
      auto ut = std::find_if(it->units.begin(), it->units.end(), [&](const ArmyUnit& x) { return x.row == u.row; });
      if (ut != it->units.end()) ut->count += u.count;
      else it->units.push_back(u);
    }
    for (Id h : g.heroes)
      if (!contains(it->heroes, h)) it->heroes.push_back(h);
  }
  // Полководец — прежний у войска target; нет — полководец войска source (одна фракция) или первый герой лидера.
  if (!m.commander && B.commander && sameState) m.commander = B.commander;
  if (!m.commander && !m.groups.empty() && !m.groups[0].heroes.empty()) m.commander = m.groups[0].heroes.front();
  if (!wasAllied && m.allied()) m.name = "Союзное войско";
  addLog(tx, LogKind::Army, "Войска на борту объединены: " + an + " и " + bn, LogRefs{0, ct, factionsOf(m)});
}

void sinkCargo(Tx& tx, Id fleet, std::vector<Id>& heroes) {
  const Id c = cargoOf(tx.w(), fleet);
  if (!c) return;
  const Army C = *tx.w().army(c);
  i64 lost = 0;
  for (const ArmyGroup& g : C.groups) {
    if (tx.w().faction(g.faction))
      for (const ArmyUnit& u : g.units) {
        if (u.count <= 0) continue;
        lost += u.count;
        for (ArmyRow& r : tx.faction(g.faction).army)
          if (r.id == u.row) r.total = std::max<i64>(0, r.total - u.count);
      }
    for (Id h : g.heroes)
      if (!contains(heroes, h)) heroes.push_back(h);
  }
  const std::string cn = armyName(tx.w(), c), fn = armyName(tx.w(), fleet);
  tx.eraseArmy(c);
  tx.army(fleet).cargo = 0;
  addLog(tx, LogKind::Battle, "Вместе с флотом " + fn + " погибло войско на борту " + cn + ": " + fmtInt(lost), LogRefs{0, fleet, factionsOf(C)});
}

void needCapacity(const World& w, Id fleet, i64 before) {
  const Id c = cargoOf(w, fleet);
  if (!c) return;
  const i64 cap = fleetCapacity(w, fleet), size = armySize(w, c);
  if (cap < size && cap < before)
    fail("На борту " + armyName(w, fleet) + " войско " + armyName(w, c) + ": " + fmtInt(size) + " — вместимость флота стала бы " + fmtInt(cap));
}

}  // namespace detail

// ================================================================ посадка и высадка
bool canEmbark(const World& w, Id army, Id fleet, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Army* a = nullptr;
  const Army* f = nullptr;
  if (!boardingPair(w, army, fleet, a, f, why)) return false;
  if (Id c = cargoOf(w, fleet)) return no("На борту " + armyName(w, fleet) + " уже есть войско " + armyName(w, c));
  const i64 size = unitsOf(*a), cap = fleetCapacity(w, fleet);
  if (size > cap)
    return no("Войско " + fmtInt(size) + " больше вместимости флота " + fmtInt(cap));
  return boardingPlace(w, *a, *f, why);
}

void embark(Tx& tx, Id army, Id fleet) {
  std::string why;
  if (!canEmbark(tx.w(), army, fleet, &why)) fail(why);
  const Vec2 at = tx.w().army(fleet)->pos;
  const Id from = provinceAtTx(tx, tx.w().army(army)->pos);
  Army& a = tx.army(army);
  a.carrier = fleet;
  a.pos = at;
  tx.army(fleet).cargo = army;
  addLog(tx, LogKind::Fleet, armyName(tx.w(), army) + " на борту " + armyName(tx.w(), fleet) + (from ? " (посадка: " + provName(tx.w(), from) + ")" : std::string()),
         LogRefs{from, fleet, factionsOf(*tx.w().army(fleet))});
}

bool canBoardExchange(const World& w, Id army, Id fleet, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Army* a = nullptr;
  const Army* f = nullptr;
  if (!boardingPair(w, army, fleet, a, f, why)) return false;
  const Id c = cargoOf(w, fleet);
  if (!c) return no("На борту " + armyName(w, fleet) + " нет войска");
  if (!canExchange(w, army, c, why)) return false;
  return boardingPlace(w, *a, *f, why);
}

std::vector<Id> landingProvinces(const World& w, Id fleet) {
  const Army* f = w.army(fleet);
  if (!f || !f->isFleet()) return {};
  auto fs = geo::faces(w);
  const Id F = fs ? fs->provinceAt(f->pos) : 0;
  if (!seaProvince(w, F)) return {};
  return landNeighbors(w, F);
}

bool canLand(const World& w, Id fleet, Vec2 pos, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Army* f = w.army(fleet);
  if (!f || !f->isFleet()) return no("Флот не найден");
  const Id c = cargoOf(w, fleet);
  if (!c) return no("На борту нет войска");
  if (!validPosition(w, ArmyKind::Army, pos, c, why)) return false;
  auto fs = geo::faces(w);
  const Id F = fs->provinceAt(f->pos);
  const bool nearFleet = dist(pos, f->pos) <= kLandingReach;
  if (seaProvince(w, F)) {
    const Id Q = fs->provinceAt(pos);
    if (!Q) return nearFleet ? true : no("Слишком далеко от флота");
    if (contains(landNeighbors(w, F), Q)) return true;
    return no(provName(w, Q) + " не граничит с " + provName(w, F));
  }
  return nearFleet ? true : no("Слишком далеко от флота");
}

Id land(Tx& tx, Id fleet, Vec2 pos) {
  std::string why;
  if (!canLand(tx.w(), fleet, pos, &why)) fail(why);
  const Id c = cargoOf(tx.w(), fleet);
  Army& a = tx.army(c);
  a.carrier = 0;
  a.pos = pos;
  tx.army(fleet).cargo = 0;
  const Id to = provinceAtTx(tx, pos);
  addLog(tx, LogKind::Army, armyName(tx.w(), c) + " высажено с " + armyName(tx.w(), fleet) + (to ? " в провинции " + provName(tx.w(), to) : std::string()),
         LogRefs{to, c, factionsOf(*tx.w().army(c))});
  return c;
}

// ================================================================ обмен отрядами
bool canExchange(const World& w, Id a, Id b, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Army* A = w.army(a);
  const Army* B = w.army(b);
  if (!A || !B) return no("Войско не найдено");
  if (a == b) return no("Нельзя обменяться отрядами с самим собой");
  if (A->kind != B->kind) return no("Войско не обменивается отрядами с флотом");
  if (!subsetOf(*A, *B) && !subsetOf(*B, *A)) return no("Отрядами обмениваются только объекты одного государства");
  if (aboard(w, *A) && aboard(w, *B)) return no("Оба войска на борту флотов");
  return true;
}

namespace {

// Итог обмена: состав двух объектов (группы с новыми численностями и героями) и проверки.
struct ExchangePlan {
  Army A, B;
  std::vector<std::string> problems;
};

ExchangePlan planExchange(const World& w, Id a, Id b, const ExchangeSpec& spec) {
  ExchangePlan p;
  std::string why;
  if (!canExchange(w, a, b, &why)) {
    p.problems.push_back(why);
    return p;
  }
  p.A = *w.army(a);
  p.B = *w.army(b);
  // Суммы по (фракция, строка) и порядок строк.
  std::map<std::pair<Id, Id>, i64> total, have;
  for (const ArmyGroup& g : p.A.groups)
    for (const ArmyUnit& u : g.units) {
      total[{g.faction, u.row}] += u.count;
      have[{g.faction, u.row}] += u.count;
    }
  for (const ArmyGroup& g : p.B.groups)
    for (const ArmyUnit& u : g.units) total[{g.faction, u.row}] += u.count;
  std::map<std::pair<Id, Id>, i64> first = have;
  for (auto& [key, n] : spec.first) {
    auto it = total.find(key);
    if (it == total.end()) {
      if (n != 0) p.problems.push_back("Такого отряда нет ни в одном из объектов");
      continue;
    }
    if (n < 0 || n > it->second) {
      p.problems.push_back("Численность строки — от 0 до " + fmtInt(it->second));
      continue;
    }
    first[key] = n;
  }
  // Герои: все герои обоих объектов; первому — heroesFirst.
  std::vector<Id> all;
  for (const Army* x : {&p.A, &p.B})
    for (const ArmyGroup& g : x->groups)
      for (Id h : g.heroes)
        if (!contains(all, h)) all.push_back(h);
  for (Id h : spec.heroesFirst)
    if (!contains(all, h)) p.problems.push_back(q(w.characterName(h)) + " не сопровождает ни один из объектов");
  if (!p.problems.empty()) return p;

  std::map<Id, Id> heroFac;   // герой → фракция его группы (до обмена)
  for (const Army* x : {&p.A, &p.B})
    for (const ArmyGroup& g : x->groups)
      for (Id h : g.heroes) heroFac.emplace(h, g.faction);
  auto heroFaction = [&](Id h) -> Id {
    auto it = heroFac.find(h);
    return it == heroFac.end() ? 0 : it->second;
  };
  // Новый состав объекта: строки в прежнем порядке, новые — в конец; герои — оставшиеся и пришедшие.
  auto rebuild = [&](Army& x, bool isFirst) {
    for (ArmyGroup& g : x.groups) {
      std::vector<ArmyUnit> units;
      auto countFor = [&](Id row) {
        const auto key = std::make_pair(g.faction, row);
        return isFirst ? first[key] : total[key] - first[key];
      };
      for (const ArmyUnit& u : g.units)
        if (std::none_of(units.begin(), units.end(), [&](const ArmyUnit& v) { return v.row == u.row; }))
          if (i64 n = countFor(u.row); n > 0) units.push_back(ArmyUnit{u.row, n});
      for (auto& [key, t] : total)
        if (key.first == g.faction && std::none_of(units.begin(), units.end(), [&](const ArmyUnit& v) { return v.row == key.second; }))
          if (i64 n = countFor(key.second); n > 0) units.push_back(ArmyUnit{key.second, n});
      std::vector<Id> heroes;
      for (Id h : g.heroes)
        if (contains(spec.heroesFirst, h) == isFirst) heroes.push_back(h);
      for (Id h : all)
        if (contains(spec.heroesFirst, h) == isFirst && heroFaction(h) == g.faction && !contains(heroes, h)) heroes.push_back(h);
      g.units = std::move(units);
      g.heroes = std::move(heroes);
    }
  };
  const Army A0 = p.A, B0 = p.B;
  rebuild(p.A, true);
  rebuild(p.B, false);
  // Отряды и герои фракции, у которой нет группы в объекте, туда не переходят.
  for (auto& [key, t] : total) {
    const i64 fa = first[key], fb = t - first[key];
    if (fa > 0 && !groupIn(A0, key.first)) p.problems.push_back("В " + armyName(w, a) + " нет отрядов " + facName(w, key.first));
    if (fb > 0 && !groupIn(B0, key.first)) p.problems.push_back("В " + armyName(w, b) + " нет отрядов " + facName(w, key.first));
  }
  for (Id h : all) {
    const bool toFirst = contains(spec.heroesFirst, h);
    if (!groupIn(toFirst ? A0 : B0, heroFaction(h)))
      p.problems.push_back(q(w.characterName(h)) + " не может перейти в " + armyName(w, toFirst ? a : b));
  }
  // Главный полководец следует за героем; без полководца — первый герой объекта.
  auto has = [](const Army& x, Id h) {
    for (const ArmyGroup& g : x.groups)
      if (contains(g.heroes, h)) return true;
    return false;
  };
  const Id cmdA = A0.commander, cmdB = B0.commander;
  p.A.commander = cmdA && has(p.A, cmdA) ? cmdA : 0;
  p.B.commander = cmdB && has(p.B, cmdB) ? cmdB : 0;
  if (!p.A.commander && cmdB && has(p.A, cmdB)) p.A.commander = cmdB;
  if (!p.B.commander && cmdA && has(p.B, cmdA)) p.B.commander = cmdA;
  for (Army* x : {&p.A, &p.B})
    if (!x->commander)
      for (const ArmyGroup& g : x->groups)
        if (!g.heroes.empty()) {
          x->commander = g.heroes.front();
          break;
        }
  // Вместимость: войско на борту — не больше вместимости своего флота; флот с войском на борту не теряет нужную
  // вместимость (флоты — по новому составу).
  if (!p.A.isFleet()) {
    for (const Army* x : {&p.A, &p.B})
      if (aboard(w, *x)) {
        const i64 size = unitsOf(*x), cap = fleetCapacity(w, x->carrier);
        const i64 was = unitsOf(x == &p.A ? A0 : B0);
        if (size > cap && size > was)
          p.problems.push_back("На борту " + armyName(w, x->carrier) + " поместится " + fmtInt(cap) + ", а в войске стало бы " + fmtInt(size));
      }
  } else {
    for (const Army* x : {&p.A, &p.B}) {
      const Id c = cargoOf(w, x->id);
      if (!c) continue;
      i64 cap = 0;
      for (const ArmyGroup& g : x->groups)
        if (const Faction* f = w.faction(g.faction))
          for (const ArmyUnit& u : g.units)
            if (const FleetRow* r = f->fleetRow(u.row)) cap += u.count * shipCapacity(w, r->type);
      const i64 size = armySize(w, c), was = fleetCapacity(w, x->id);
      if (cap < size && cap < was)
        p.problems.push_back("На борту " + armyName(w, x->id) + " войско " + fmtInt(size) + " — вместимость стала бы " + fmtInt(cap));
    }
  }
  return p;
}

bool emptyObject(const Army& x) {
  return std::all_of(x.groups.begin(), x.groups.end(), [](const ArmyGroup& g) { return g.units.empty() && g.heroes.empty(); });
}

}  // namespace

std::vector<std::string> exchangeProblems(const World& w, Id a, Id b, const ExchangeSpec& spec) { return planExchange(w, a, b, spec).problems; }

void exchangeUnits(Tx& tx, Id a, Id b, const ExchangeSpec& spec) {
  ExchangePlan p = planExchange(tx.w(), a, b, spec);
  if (!p.problems.empty()) fail(p.problems.front());
  const std::string an = armyName(tx.w(), a), bn = armyName(tx.w(), b);
  const bool fleet = p.A.isFleet();
  // Пустые группы (без отрядов и героев) уходят, если в объекте есть другие.
  for (Army* x : {&p.A, &p.B})
    if (x->groups.size() > 1)
      x->groups.erase(std::remove_if(x->groups.begin(), x->groups.end(), [](const ArmyGroup& g) { return g.units.empty() && g.heroes.empty(); }),
                      x->groups.end());
  for (Army* x : {&p.A, &p.B}) {
    Army& m = tx.army(x->id);
    m.groups = x->groups;
    m.commander = x->commander;
  }
  std::vector<Id> fs = factionsOf(p.A);
  for (Id f : factionsOf(p.B))
    if (!contains(fs, f)) fs.push_back(f);
  addLog(tx, fleet ? LogKind::Fleet : LogKind::Army, "Обмен отрядами: " + an + " и " + bn, LogRefs{0, a, fs});
  for (Id id : {a, b})
    if (const Army* x = tx.w().army(id); x && emptyObject(*x)) eraseObject(tx, id);
}

// ================================================================ союзные объекты: «Вернуть войско»
bool canReturnGroup(const World& w, Id army, Id faction, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Army* a = w.army(army);
  if (!a) return no("Войско не найдено");
  if (!a->allied()) return no(armyName(w, army) + (a->isFleet() ? " — не союзный флот" : " — не союзное войско"));
  if (a->groups[0].faction == faction) return no(facName(w, faction) + " управляет " + armyName(w, army));
  if (!groupIn(*a, faction)) return no("В " + armyName(w, army) + " нет отрядов " + facName(w, faction));
  if (aboard(w, *a)) return no("Войско на борту флота — сначала высадите его");
  return true;
}

Id returnGroup(Tx& tx, Id army, Id faction) {
  std::string why;
  if (!canReturnGroup(tx.w(), army, faction, &why)) fail(why);
  const Army a = *tx.w().army(army);
  ArmyGroup g = *groupIn(a, faction);
  auto spot = Placement(tx.w(), facesFor(tx)).freeSpot(a.kind, a.pos, 0);
  if (!spot) fail("Рядом нет свободного места для отделившегося объекта");
  Army& keep = tx.army(army);
  keep.groups.erase(std::remove_if(keep.groups.begin(), keep.groups.end(), [&](const ArmyGroup& x) { return x.faction == faction; }), keep.groups.end());
  const bool cmdMoves = a.commander && contains(g.heroes, a.commander);
  if (cmdMoves) keep.commander = 0;
  if (!keep.allied() && (keep.name == "Союзное войско" || keep.name == "Союзный флот")) keep.name = defaultArmyName(tx.w(), a.kind, keep.groups[0].faction);
  Army n;
  n.kind = a.kind;
  n.name = defaultArmyName(tx.w(), a.kind, faction);
  n.pos = *spot;
  n.groups.push_back(std::move(g));
  n.loyalty = a.loyalty;
  n.commander = cmdMoves ? a.commander : 0;
  if (!n.commander && !n.groups[0].heroes.empty()) n.commander = n.groups[0].heroes.front();
  const Id nid = tx.add(std::move(n)).id;
  // Войско этой фракции на борту союзного флота уходит вместе с её кораблями.
  if (a.isFleet())
    if (const Id c = cargoOf(tx.w(), army); c && tx.w().army(c)->leader() == faction) {
      tx.army(army).cargo = 0;
      tx.army(c).carrier = nid;
      tx.army(nid).cargo = c;
      syncCargo(tx, nid);
    }
  // Модификаторы войска государства (нежить, демоны) — у отделившегося объекта по виду его государства.
  if (const Faction* f = tx.w().faction(faction); f && f->isState() && !a.isFleet()) {
    if (f->stateKind == StateKind::Undead) addModifier(tx, ModTarget::Army, nid, ensureBuiltinMod(tx, schema::mod::UndeadArmy), 0);
    else if (f->stateKind == StateKind::Demonic) addModifier(tx, ModTarget::Army, nid, ensureBuiltinMod(tx, schema::mod::DemonArmy), 0);
  }
  addLog(tx, a.isFleet() ? LogKind::Fleet : LogKind::Army,
         facName(tx.w(), faction) + (a.isFleet() ? " возвращает свой флот из " : " возвращает своё войско из ") + q(a.name.empty() ? "—" : a.name) + ": " +
             armyName(tx.w(), nid),
         LogRefs{provinceAtTx(tx, *spot), nid, {faction, a.groups[0].faction}});
  return nid;
}

// ================================================================ флот у верфи
std::vector<Id> shipyardProvinces(const World& w, Id faction) {
  std::vector<Id> out;
  const Faction* f = w.faction(faction);
  if (!f || f->isWild()) return out;
  w.provinces.each([&](const Province& p) {
    if (p.sea) return;
    const bool mine = f->isState() ? p.owner == faction && !(p.occupied && p.occupier && p.occupier != faction) : contains(p.hqs, faction);
    if (!mine || !hasBuildingRole(w, p.id, BuildingFlag::Shipyard)) return;
    if (isCoastal(w, p.id)) out.push_back(p.id);
  });
  return out;
}

std::optional<Vec2> fleetSpot(const World& w, Id province) {
  auto fs = geo::faces(w);
  if (!fs) return std::nullopt;
  const geo::ProvinceShape* sh = fs->shape(province);
  if (!sh) return std::nullopt;
  // Точки береговых дуг провинции (с морем или морской провинцией по другую сторону) — ближние к её подписи первыми.
  struct Cand {
    double d;
    Vec2 p, n;
  };
  std::vector<Cand> cands;
  w.edges.each([&](const Edge& e) {
    const bool left = e.pl == province, right = e.pr == province;
    if (left == right) return;
    const Terrain other = left ? e.tr : e.tl;
    const Id op = left ? e.pr : e.pl;
    if (other != Terrain::Sea && !seaProvince(w, op)) return;
    const std::vector<Vec2> c = geo::edgeCoords(w, e);
    for (size_t i = 0; i + 1 < c.size(); i++) {
      const Vec2 m = (c[i] + c[i + 1]) * 0.5;
      const Vec2 n = (c[i + 1] - c[i]).norm().perp();
      if (n.len2() == 0) continue;
      cands.push_back(Cand{dist2(m, sh->label), m, n});
    }
  });
  std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) {
    if (x.d != y.d) return x.d < y.d;
    return x.p.x != y.p.x ? x.p.x < y.p.x : x.p.y < y.p.y;
  });
  const Placement pl(w, fs);
  const double off = schema::kObjectRadius * 1.3;
  for (size_t i = 0; i < cands.size() && i < 48; i++) {
    // Сторона моря у дуги: точка чуть в стороне от берега, где рельеф — море.
    for (double s : {1.0, -1.0}) {
      const Vec2 q = cands[i].p + cands[i].n * (off * s);
      if (fs->terrainAt(q) != Terrain::Sea) continue;
      if (auto spot = pl.freeSpot(ArmyKind::Fleet, q, 0)) return spot;
    }
  }
  return std::nullopt;
}

Id placeFleet(Tx& tx, Id faction, Id province) {
  const Faction& f = needFaction(tx.w(), faction);
  const Province& p = needProvince(tx.w(), province);
  if (!contains(shipyardProvinces(tx.w(), faction), province)) {
    if (p.sea) fail("Флот ставится у приморской провинции, а не в морской");
    if (f.isState() && p.owner != faction) fail(provName(tx.w(), province) + " — не провинция " + facName(tx.w(), faction));
    if (!isCoastal(tx.w(), province)) fail(provName(tx.w(), province) + " не граничит с морем");
    if (!hasBuildingRole(tx.w(), province, BuildingFlag::Shipyard)) fail("В провинции " + provName(tx.w(), province) + " нет достроенной верфи");
    fail("Флот нельзя поставить у провинции " + provName(tx.w(), province));
  }
  auto spot = fleetSpot(tx.w(), province);
  if (!spot) fail("У берега провинции " + provName(tx.w(), province) + " нет свободного места для флота");
  return createArmy(tx, ArmyKind::Fleet, faction, *spot);
}

}  // namespace rg::rules
