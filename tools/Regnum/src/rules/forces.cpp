// Regnum — формирование и резерв войск и флота:
// отряды из населения (нежить — из трупов, демоны — из демонической энергии; звери, чудовища, механизмы и элементали —
// без людей), ключевой и дополнительные ресурсы, эссенции элементов, особые отряды — при постройке доступа; корабли
// за ресурсы констант стоимости, 2 хода до резерва; роспуск резерва в население; флот в торговле; оккупационный
// гарнизон; верность войска и гарнизона.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

i64 getOr0(const std::map<Id, i64>& m, Id k) {
  auto it = m.find(k);
  return it == m.end() ? 0 : it->second;
}

// Что стоит один воин строки армии: людей (1 — живые и прочие) или ресурса (трупы, энергия). Звери, чудовища,
// военные механизмы и элементали не требуют ни населения, ни трупов, ни энергии.
struct Price {
  i64 people = 0;
  Id res = 0;
  double perUnit = 0;
};
Price priceOf(const World& w, Id faction, const ArmyRow& r) {
  Price p;
  if (r.merc || !schema::needsPeople(r.type)) return p;   // наёмники — только за золото
  const std::string race = unitRace(w, faction, r);
  if (race == schema::kRaceUndead) {
    p.res = resourceId(w, schema::kResCorpses);
    p.perUnit = std::max(0.0, constantOf(w, schema::cst::CorpsesPerUnit).num);
  } else if (race == schema::kRaceDemonic) {
    p.res = resourceId(w, schema::kResEnergy);
    p.perUnit = std::max(0.0, constantOf(w, schema::cst::EnergyPerUnit).num);
  } else {
    p.people = 1;
  }
  return p;
}

}  // namespace

// ================================================================ развёрнутые силы
i64 Deployed::armyField(Id row) const { return getOr0(army, row) + getOr0(garrison, row) + getOr0(occupation, row); }
i64 Deployed::fleetField(Id row) const { return getOr0(fleet, row) + getOr0(trade, row); }

i64 reserveOf(const World& w, Id faction, Id row) {
  const Faction* f = w.faction(faction);
  if (!f) return 0;
  Deployed d = deployed(w, faction);
  if (const ArmyRow* r = f->armyRow(row)) return r->total - d.armyField(row);
  if (const FleetRow* r = f->fleetRow(row)) return r->total - d.fleetField(row);
  return 0;
}

int researchTurns(const World& w, const Tech& t, Id faction) {
  const int base = std::max(1, t.turns);
  const Id who = t.faction ? t.faction : faction;
  if (!who) return base;
  const double pct = factionEffects(w, who)[Fx::ResearchTimePct];
  if (std::fabs(pct) < 1e-9) return base;
  const double k = std::max(0.05, 1.0 + pct / 100.0);
  return std::max(1, int(std::ceil(double(base) * k - 1e-9)));
}

// ================================================================ помощники
namespace detail {

void returnWarriors(Tx& tx, Id faction, const ArmyRow& row, i64 count) {
  if (count <= 0 || row.merc || !schema::needsPeople(row.type)) return;   // звери, чудовища, механизмы, элементали, наёмники — без людей
  const Faction* f = tx.w().faction(faction);
  if (!f) return;
  const std::string race = unitRace(tx.w(), faction, row);
  if (race == schema::kRaceUndead) {
    const double k = std::max(0.0, constantOf(tx.w(), schema::cst::CorpsesPerUnit).num);
    if (k > 0) addStock(tx.faction(faction), ensureResource(tx, schema::kResCorpses), double(count) * k);
  } else if (race == schema::kRaceDemonic) {
    const double k = std::max(0.0, constantOf(tx.w(), schema::cst::EnergyPerUnit).num);
    if (k > 0) addStock(tx.faction(faction), ensureResource(tx, schema::kResEnergy), double(count) * k);
  } else if (f->isState()) {
    givePopulation(tx, faction, count);
  }
}

Id mirrorRow(Tx& tx, Id rebelState, Id origin, const ArmyRow& src) {
  const std::string race = unitRace(tx.w(), origin, src);
  for (const ArmyRow& r : tx.w().faction(rebelState)->army)
    if (r.name == src.name && r.type == src.type && unitRace(tx.w(), rebelState, r) == race && r.upkeep == src.upkeep && r.special == src.special)
      return r.id;
  Id id = addArmyRow(tx, rebelState, src.type, src.name, 0, src.upkeep);
  setRowRace(tx, rebelState, id, race);
  ArmyRow& r = *findById(tx.faction(rebelState).army, id);   // цена найма и особый отряд — как у прежней строки
  r.keyRes = src.keyRes;
  r.keyPer = src.keyPer;
  r.extra = src.extra;
  r.essence = src.essence;
  r.essUpkeep = src.essUpkeep;
  r.special = src.special;
  return id;
}

Id provinceAtTx(const Tx& tx, Vec2 p) {
  auto fs = facesFor(tx);
  return fs ? fs->provinceAt(p) : 0;
}

std::optional<Vec2> provinceLabel(const Tx& tx, Id province) {
  auto fs = facesFor(tx);
  if (!fs) return std::nullopt;
  const geo::ProvinceShape* sh = fs->shape(province);
  if (!sh) return std::nullopt;
  return sh->label;
}

std::vector<Id> neighborsOf(const Tx& tx, Id province) {
  std::vector<Id> out;
  auto fs = facesFor(tx);
  if (!fs) return out;
  for (auto [a, b] : fs->neighbors()) {
    if (a == province && b && !contains(out, b)) out.push_back(b);
    if (b == province && a && !contains(out, a)) out.push_back(a);
  }
  std::sort(out.begin(), out.end());
  return out;
}

namespace {

constexpr double kElementalDecay = 0.10;   // долг эссенции: элементали теряют ⌈10 %⌉ численности строки за ход

// Гибель n отрядов строки армии: сначала из резерва, затем из гарнизонов и оккупационных гарнизонов провинций и из
// войск (по ID). Группа войска без отрядов уходит с поля (её герои остаются у фракции), войско без групп исчезает.
// Возвращает погибших.
i64 loseRowUnits(Tx& tx, Id faction, Id row, i64 n) {
  const Faction* f = tx.w().faction(faction);
  const ArmyRow* r = f ? f->armyRow(row) : nullptr;
  if (!r) return 0;
  n = std::min(n, std::max<i64>(0, r->total));
  if (n <= 0) return 0;
  i64 left = n - std::min(n, std::max<i64>(0, reserveOf(tx.w(), faction, row)));
  auto takeFrom = [&](std::vector<GarrisonEntry>& gs) {
    for (GarrisonEntry& g : gs)
      if (g.row == row && left > 0) {
        const i64 t = std::min(left, std::max<i64>(0, g.count));
        g.count -= t;
        left -= t;
      }
    gs.erase(std::remove_if(gs.begin(), gs.end(), [](const GarrisonEntry& g) { return g.count <= 0; }), gs.end());
  };
  auto hasRow = [&](const std::vector<GarrisonEntry>& gs) { return std::any_of(gs.begin(), gs.end(), [&](const GarrisonEntry& g) { return g.row == row; }); };
  if (left > 0)
    for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
           return (p.owner == faction && hasRow(p.garrison)) || (p.occupied && p.occupier == faction && hasRow(p.occGarrison));
         })) {
      if (left <= 0) break;
      const Province& p = *tx.w().province(pid);
      if (p.owner == faction && hasRow(p.garrison)) takeFrom(tx.province(pid).garrison);
      if (left > 0 && p.occupied && p.occupier == faction && hasRow(p.occGarrison)) takeFrom(tx.province(pid).occGarrison);
    }
  if (left > 0)
    for (Id aid : idsWhere(tx.w().armies, [&](const Army& a) {
           if (a.isFleet()) return false;
           for (const ArmyGroup& g : a.groups)
             if (g.faction == faction && std::any_of(g.units.begin(), g.units.end(), [&](const ArmyUnit& u) { return u.row == row; })) return true;
           return false;
         })) {
      if (left <= 0) break;
      Army& a = tx.army(aid);
      for (ArmyGroup& g : a.groups) {
        if (g.faction != faction) continue;
        for (ArmyUnit& u : g.units)
          if (u.row == row && left > 0) {
            const i64 t = std::min(left, std::max<i64>(0, u.count));
            u.count -= t;
            left -= t;
          }
        g.units.erase(std::remove_if(g.units.begin(), g.units.end(), [](const ArmyUnit& u) { return u.count <= 0; }), g.units.end());
      }
      for (const ArmyGroup& g : a.groups)
        if (g.units.empty() && a.commander && contains(g.heroes, a.commander)) a.commander = 0;
      a.groups.erase(std::remove_if(a.groups.begin(), a.groups.end(), [](const ArmyGroup& g) { return g.units.empty(); }), a.groups.end());
      if (a.groups.empty()) eraseObject(tx, aid);
    }
  const i64 lost = n - left;
  if (ArmyRow* m = findById(tx.faction(faction).army, row)) m->total = std::max<i64>(0, m->total - lost);
  return lost;
}

}  // namespace

void elementalDecay(Tx& tx, Id faction) {
  const Faction* f = tx.w().faction(faction);
  if (!f) return;
  // Строки элементалей, которые содержатся хотя бы одной эссенцией в долгу: ⌈10 %⌉ численности за ход.
  std::vector<std::pair<Id, i64>> decay;
  for (const ArmyRow& r : f->army) {
    if (!schema::isElemental(r.type) || r.total <= 0) continue;
    bool debt = false;
    for (auto& [e, v] : r.essUpkeep) debt = debt || (v > 0 && tx.w().essence(e) && f->essence(e) < -1e-9);
    if (debt) decay.push_back({r.id, i64(std::ceil(double(r.total) * kElementalDecay - 1e-9))});
  }
  std::vector<std::string> parts;
  for (auto& [row, n] : decay) {
    const std::string name = tx.w().faction(faction)->armyRow(row)->name;
    if (const i64 lost = loseRowUnits(tx, faction, row, n); lost > 0) parts.push_back(q(nameOr(name)) + " −" + fmtInt(lost));
  }
  if (!parts.empty())
    addLog(tx, LogKind::Economy, facName(tx.w(), faction) + ": долг эссенции — распад элементалей: " + join(parts, ", "), LogRefs{0, 0, {faction}});
}

}  // namespace detail

// ================================================================ формирование
RecruitCost recruitCost(const World& w, Id faction, Id row, i64 count) {
  RecruitCost c;
  const Faction* f = w.faction(faction);
  if (!f) {
    c.problems.push_back("Фракция не найдена");
    return c;
  }
  if (count <= 0) {
    c.problems.push_back("Укажите, сколько сформировать");
    return c;
  }
  if (const ArmyRow* r = f->armyRow(row); r && r->merc) {
    // Наёмники: только золото за найм, без населения и ресурсов; лимит — гильдии наёмников.
    const i64 limit = mercLimit(w, faction), have = mercCount(w, faction);
    if (limit <= 0) c.problems.push_back("Нужна достроенная «Гильдия Наемников»");
    else if (have + count > limit)
      c.problems.push_back("Лимит наёмников: " + fmtInt(limit) + ", уже " + fmtInt(have) + " — можно ещё " + fmtInt(std::max<i64>(0, limit - have)));
    const double need = double(count) * std::max(0.0, r->hire);
    if (need > 0) {
      c.res[kGold] = need;
      if (f->treasury() + 1e-9 < need)
        c.problems.push_back("Недостаточно золота: нужно " + amount(need) + " тыс., в казне " + amount(std::max(0.0, f->treasury())) + " тыс.");
    }
    return c;
  }
  if (const ArmyRow* r = f->armyRow(row)) {
    // Особый отряд нанимается, пока у государства есть достроенная постройка доступа.
    if (r->special) {
      const SpecialUnit* s = w.special(r->special);
      if (s && !hasSpecialAccess(w, faction, r->special)) {
        std::vector<std::string> names;
        for (Id b : specialBuildings(w, r->special)) names.push_back(buildingName(w, b));
        c.problems.push_back("Нужна постройка доступа к особому отряду" + (names.empty() ? std::string() : ": " + join(names, " или ")));
      }
    }
    Price p = priceOf(w, faction, *r);
    if (p.people) {
      // Отряды государства — из его населения; у гильдии населения нет (наёмники).
      if (f->isState()) {
        c.people = count;
        const i64 have = statePopulation(w, faction);
        if (have < count) c.problems.push_back("Недостаточно населения: нужно " + fmtInt(count) + ", есть " + fmtInt(have));
      }
    } else if (p.res || p.perUnit > 0) {
      const char* key = unitRace(w, faction, *r) == schema::kRaceUndead ? schema::kResCorpses : schema::kResEnergy;
      const double need = double(count) * p.perUnit;
      if (need > 0) {
        Id res = p.res;
        c.res[res ? res : 0] += need;
        if (!res) {
          std::string name = key == schema::kResCorpses ? "Трупы" : "Демоническая энергия";
          c.problems.push_back("Недостаточно ресурса «" + name + "»: нужно " + amount(need) + ", есть 0");
        }
      }
    }
    // Ключевой ресурс (кавалерия, воздушная кавалерия, звери, чудовища, военные механизмы) — обязателен. Особый отряд
    // задаёт свой ключевой ресурс (любой ресурс справочника).
    if (r->special) {
      if (r->keyRes && w.resource(r->keyRes)) c.res[r->keyRes] += double(count) * std::max(1.0, r->keyPer);
    } else if (schema::needsKeyResource(r->type)) {
      if (!r->keyRes) c.problems.push_back("Не указан ключевой ресурс юнита");
      else if (!keyAllowed(w, r->type, r->keyRes)) c.problems.push_back("Ключевой ресурс «" + resName(w, r->keyRes) + "» не подходит этому типу войск");
      else c.res[r->keyRes] += double(count) * std::max(1.0, r->keyPer);
    }
    // Дополнительные ресурсы (у элементалей — нет: только эссенции) и эссенции элементов.
    if (!schema::isElemental(r->type))
      for (auto& [res, v] : r->extra)
        if (v > 0 && w.resource(res)) c.res[res] += double(count) * v;
    for (auto& [e, v] : r->essence)
      if (v > 0 && w.essence(e)) c.ess[e] += double(count) * v;
    for (auto& [res, need] : c.res) {
      if (!res) continue;
      const double have = f->stock(res);
      if (have + 1e-9 < need)
        c.problems.push_back("Недостаточно ресурса «" + resName(w, res) + "»: нужно " + amountOf(res, need) + ", есть " + amountOf(res, std::max(0.0, have)));
    }
    for (auto& [e, need] : c.ess) {
      const double have = f->essence(e);
      if (have + 1e-9 < need) {
        const CatalogItem* ci = w.essence(e);
        c.problems.push_back("Недостаточно эссенции «" + (ci ? ci->name : std::string("?")) + "»: нужно " + amount(need) + ", есть " +
                             amount(std::max(0.0, have)));
      }
    }
    c.res.erase(0);
    return c;
  }
  if (const FleetRow* r = f->fleetRow(row)) {
    // Корабли строят верфи: уровень верфи открывает типы кораблей. У гильдии верфей нет —
    // её флот формируется без верфи.
    if (f->isState() && !(shipyardAccess(w, faction) & (1u << int(r->type)))) {
      if (!shipyardAccess(w, faction)) c.problems.push_back("Нужна достроенная верфь: без неё корабли не строятся");
      else c.problems.push_back("«" + std::string(schema::shipType(r->type).name) + "» строит верфь более высокого уровня");
    }
    const Constant& cost = constantOf(w, schema::shipCostKey(r->type));
    for (auto& [res, v] : cost.res) {
      const double need = v * double(count);
      if (!(need > 0)) continue;
      c.res[res] += need;
    }
    for (auto& [e, v] : cost.ess) {
      const double need = v * double(count);
      if (need > 0 && w.essence(e)) c.ess[e] += need;
    }
    // Морское чудовище: ресурс подгруппы «Морские чудовища», 1 на судно.
    if (schema::shipKeyRule(r->type).group) {
      if (!r->keyRes) c.problems.push_back("Не указан ресурс подгруппы «Морские чудовища»");
      else c.res[r->keyRes] += double(count);
    }
    for (auto& [res, need] : c.res) {
      const double have = f->stock(res);
      if (have + 1e-9 < need)
        c.problems.push_back("Недостаточно ресурса «" + resName(w, res) + "»: нужно " + amountOf(res, need) + ", есть " + amountOf(res, std::max(0.0, have)));
    }
    for (auto& [e, need] : c.ess) {
      const double have = f->essence(e);
      if (have + 1e-9 < need) {
        const CatalogItem* ci = w.essence(e);
        c.problems.push_back("Недостаточно эссенции «" + (ci ? ci->name : std::string("?")) + "»: нужно " + amount(need) + ", есть " +
                             amount(std::max(0.0, have)));
      }
    }
    return c;
  }
  c.problems.push_back("Строки нет в таблицах войск и флота");
  return c;
}

void recruit(Tx& tx, Id faction, Id row, i64 count) {
  const Faction& f = needFaction(tx.w(), faction);
  const bool fleet = f.fleetRow(row) != nullptr;
  if (!fleet && !f.armyRow(row)) fail("Строки нет в таблицах войск и флота " + facName(tx.w(), faction));
  if (count <= 0 || count > kMaxCount) fail("Сформировать можно от 1 до " + fmtInt(kMaxCount));
  RecruitCost c = recruitCost(tx.w(), faction, row, count);
  if (!c.problems.empty()) fail(c.problems.front());
  Formation q;
  q.row = row;
  q.count = count;
  q.left = schema::kFormationTurns;
  if (c.people > 0) {
    takePopulation(tx, faction, c.people);
    q.people = c.people;
  }
  for (auto& [res, v] : c.res) {
    Id r = res;
    if (!r) {   // ресурс ещё не создан (нужно 0 — проверка выше не пропустила бы ненулевую цену)
      continue;
    }
    addStock(tx.faction(faction), r, -v);
    q.paid[r] = v;
  }
  for (auto& [e, v] : c.ess) {
    Faction& m = tx.faction(faction);
    double& s = m.ess[e];
    s -= v;
    if (std::fabs(s) < 1e-9) m.ess.erase(e);
    q.paidEss[e] = v;
  }
  tx.faction(faction).forming.push_back(q);
  const std::string name = fleet ? f.fleetRow(row)->name : f.armyRow(row)->name;
  addLog(tx, fleet ? LogKind::Fleet : LogKind::Army,
         facName(tx.w(), faction) + ": начато формирование «" + name + "» — " + fmtInt(count) + ", поступит в резерв через " + nTurns(q.left),
         LogRefs{0, 0, {faction}});
}

void cancelFormation(Tx& tx, Id faction, int index) {
  const Faction& f = needFaction(tx.w(), faction);
  if (index < 0 || index >= int(f.forming.size())) fail("Формирование не найдено");
  const Formation q = f.forming[size_t(index)];
  auto& list = tx.faction(faction).forming;
  list.erase(list.begin() + index);
  if (q.people > 0 && tx.w().faction(faction)->isState()) givePopulation(tx, faction, q.people);
  for (auto& [res, v] : q.paid)
    if (tx.w().resource(res) && v > 0) addStock(tx.faction(faction), res, v);
  for (auto& [e, v] : q.paidEss)
    if (tx.w().essence(e) && v > 0) {
      Faction& m = tx.faction(faction);
      double& s = m.ess[e];
      s += v;
      if (std::fabs(s) < 1e-9) m.ess.erase(e);
    }
}

void disbandReserve(Tx& tx, Id faction, Id row, i64 count) {
  const Faction& f = needFaction(tx.w(), faction);
  const bool fleet = f.fleetRow(row) != nullptr;
  if (!fleet && !f.armyRow(row)) fail("Строки нет в таблицах войск и флота " + facName(tx.w(), faction));
  if (count <= 0) fail("Укажите, сколько распустить");
  const i64 reserve = reserveOf(tx.w(), faction, row);
  if (count > reserve) fail("В резерве только " + fmtInt(std::max<i64>(0, reserve)));
  std::string name;
  if (fleet) {
    FleetRow& r = *findById(tx.faction(faction).fleet, row);
    r.total -= count;
    name = r.name;
  } else {
    const ArmyRow src = *f.armyRow(row);
    name = src.name;
    findById(tx.faction(faction).army, row)->total -= count;
    returnWarriors(tx, faction, src, count);
  }
  const bool people = !fleet && schema::needsPeople(f.armyRow(row)->type);
  addLog(tx, fleet ? LogKind::Fleet : LogKind::Army,
         facName(tx.w(), faction) + ": распущено «" + name + "» — " + fmtInt(count) + (people ? ", воины вернулись в население" : ""),
         LogRefs{0, 0, {faction}});
}

// ================================================================ наёмники и верфи
i64 mercLimit(const World& w, Id state) {
  const Faction* f = w.faction(state);
  if (!f || !f->isState()) return 0;
  i64 guilds = 0;
  w.provinces.each([&](const Province& p) {
    if (p.sea || p.owner != state) return;
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      if (b && b->mercenary && pb.builtLevel() >= 1) guilds++;
    }
  });
  const double per = std::max(0.0, constantOf(w, schema::cst::MercPerGuild).num);
  return i64(std::floor(double(guilds) * per + 1e-9));
}

i64 mercCount(const World& w, Id state) {
  const Faction* f = w.faction(state);
  if (!f) return 0;
  i64 n = 0;
  for (const ArmyRow& r : f->army)
    if (r.merc) n += std::max<i64>(0, r.total);
  for (const Formation& q : f->forming)
    if (const ArmyRow* r = f->armyRow(q.row); r && r->merc) n += std::max<i64>(0, q.count);
  return n;
}

u32 shipyardAccess(const World& w, Id state) {
  u32 mask = 0;
  w.provinces.each([&](const Province& p) {
    if (p.sea || p.owner != state) return;
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      const int lvl = pb.builtLevel();
      if (b && b->shipyard && lvl >= 1 && lvl <= int(b->levels.size())) mask |= b->levels[size_t(lvl - 1)].ships;
    }
  });
  return mask;
}

// ================================================================ флот в торговле и оккупационный гарнизон
void setTradeFleet(Tx& tx, Id faction, Id row, i64 count) {
  const Faction& f = needFaction(tx.w(), faction);
  if (!f.fleetRow(row)) fail("Такого судна нет в таблице флота " + facName(tx.w(), faction));
  if (count < 0) fail("Численность не может быть отрицательной");
  i64 cur = 0;
  for (const GarrisonEntry& g : f.tradeFleet)
    if (g.row == row) cur += g.count;
  if (count > cur) {
    const i64 reserve = reserveOf(tx.w(), faction, row);
    if (count - cur > reserve) fail("В резерве недостаточно: нужно " + fmtInt(count - cur) + ", в резерве " + fmtInt(std::max<i64>(0, reserve)));
  }
  auto& list = tx.faction(faction).tradeFleet;
  auto it = std::find_if(list.begin(), list.end(), [&](const GarrisonEntry& g) { return g.row == row; });
  if (count == 0) {
    if (it != list.end()) list.erase(it);
  } else if (it != list.end()) {
    it->count = count;
  } else {
    list.push_back(GarrisonEntry{row, count});
  }
}

void setOccupationGarrison(Tx& tx, Id province, Id row, i64 count) {
  const Province& p = needProvince(tx.w(), province);
  if (!p.occupied || !p.occupier) fail("Провинция не оккупирована");
  const Faction& occ = needState(tx.w(), p.occupier);
  if (!occ.armyRow(row)) fail("Такого отряда нет в таблице войск " + facName(tx.w(), p.occupier));
  if (count < 0) fail("Численность не может быть отрицательной");
  i64 cur = 0;
  for (const GarrisonEntry& g : p.occGarrison)
    if (g.row == row) cur += g.count;
  if (count > cur) {
    // Пополнить оккупационный гарнизон можно, только когда в провинции стоит войско оккупанта (вывести — всегда).
    auto fs = facesFor(tx);
    const Id occ = p.occupier;
    const bool here = fs && tx.w().armies.findIf([&](const Army& a) {
      return !a.isFleet() && !aboard(tx.w(), a) && fs->provinceAt(a.pos) == province &&
             std::any_of(a.groups.begin(), a.groups.end(), [&](const ArmyGroup& g) { return g.faction == occ; });
    }) != nullptr;
    if (!here) fail("Оккупационный гарнизон пополняется, только когда в провинции стоит войско " + facName(tx.w(), occ));
    const i64 reserve = reserveOf(tx.w(), p.occupier, row);
    if (count - cur > reserve) fail("В резерве недостаточно: нужно " + fmtInt(count - cur) + ", в резерве " + fmtInt(std::max<i64>(0, reserve)));
  }
  Province& m = tx.province(province);
  auto& list = m.occGarrison;
  auto it = std::find_if(list.begin(), list.end(), [&](const GarrisonEntry& g) { return g.row == row; });
  if (count == 0) {
    if (it != list.end()) list.erase(it);
  } else if (it != list.end()) {
    it->count = count;
  } else {
    list.push_back(GarrisonEntry{row, count});
  }
  if (!m.occGarrison.empty()) m.occIdle = 0;
}

// ================================================================ верность
void setArmyLoyalty(Tx& tx, Id army, double loyalty) {
  const Army& a = needArmy(tx.w(), army);
  needFinite(loyalty, "Верность");
  loyalty = clamp(loyalty, schema::kMinLoyalty, schema::kMaxLoyalty);
  if (loyalty == a.loyalty) return;
  if (hasModKey(tx.w(), a.modifiers, schema::mod::UndeadArmy)) fail("Верность армии нежити — всегда 100 %");
  if (loyalty < a.loyalty)
    for (const ArmyGroup& g : a.groups)
      for (Id h : g.heroes)
        if (characterHas(tx.w(), h, schema::mod::Loyalist))
          fail("С ним " + q(tx.w().characterName(h)) + " — непреклонный лоялист: верность войска не уменьшается");
  tx.army(army).loyalty = loyalty;
}

// ================================================================ верность гарнизона
namespace {
bool undeadOwner(const World& w, const Province& p) {
  const Faction* o = w.faction(p.owner);
  return o && o->isState() && o->stateKind == StateKind::Undead;
}
}  // namespace

void setGarrisonLoyalty(Tx& tx, Id province, double loyalty) {
  const Province& p = needProvince(tx.w(), province);
  needFinite(loyalty, "Верность");
  if (p.sea || !p.owner) fail("У провинции нет гарнизона владельца");
  loyalty = clamp(loyalty, schema::kMinLoyalty, schema::kMaxLoyalty);
  if (loyalty == p.garrisonLoyalty) return;
  if (undeadOwner(tx.w(), p)) fail("Гарнизон государства нежити верен всегда (100 %)");
  if (loyalty < p.garrisonLoyalty)
    for (Id h : p.garrisonHeroes)
      if (characterHas(tx.w(), h, schema::mod::Loyalist))
        fail("В гарнизоне " + q(tx.w().characterName(h)) + " — непреклонный лоялист: верность гарнизона не уменьшается");
  tx.province(province).garrisonLoyalty = loyalty;
}

double garrisonLoyaltyDelta(const World& w, Id province) {
  const Province* p = w.province(province);
  if (!p || p->sea || !p->owner || undeadOwner(w, *p)) return 0;
  // Эффект «Верность войска за ход» государства действует и на его гарнизоны.
  double d = factionEffects(w, p->owner)[Fx::LoyaltyPerTurn];
  int loyalists = 0;
  for (Id h : p->garrisonHeroes)
    if (characterHas(w, h, schema::mod::Loyalist)) loyalists++;
  if (loyalists) d = std::max(0.0, d) + schema::kLoyalistBonus * loyalists;
  return d;
}

}  // namespace rg::rules
