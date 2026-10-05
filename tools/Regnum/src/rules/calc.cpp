// Regnum — расчёт провинций и фракций (RULES.md §2–3), кеш расчёта, развёрнутые силы.
#include <mutex>

#include "rules/internal.h"

namespace rg::rules {

namespace detail {

namespace {

// Численности рас: слить повторы, по убыванию численности, затем по ID.
std::vector<std::pair<Id, i64>> sortedRaces(const std::map<Id, i64>& m) {
  std::vector<std::pair<Id, i64>> r(m.begin(), m.end());
  std::stable_sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.second > b.second; });
  return r;
}

void calcProvince(const World& w, const SourceIndex& si, const Province& p, int routes, double routeBonus, ProvinceCalc& pc) {
  pc.id = p.id;
  pc.sea = p.sea;
  pc.owner = p.owner;
  if (p.sea) return;  // морская провинция не содержит информации и не участвует в расчётах

  const Faction* owner = w.faction(p.owner);
  if (owner && !owner->isState()) owner = nullptr;
  pc.fx = provinceFx(w, si, p);
  const Effects& fx = pc.fx;

  // Слоты (ТЗ 1.f.i).
  pc.slotsSize = int(p.size) >= 0 && int(p.size) < 3 ? schema::kProvSizes[int(p.size)].value : 0;
  pc.slotsCity = int(p.city) >= 0 && int(p.city) < 4 ? schema::kCityTypes[int(p.city)].value : 0;
  pc.slotsMods = int(std::lround(fx[Fx::Slots]));
  pc.slots = slotsOf(p, fx);
  pc.slotsUsed = int(p.buildings.size());

  // Торговая ценность (ТЗ 1.d.iv–v, 1.g): каждый маршрут +10 % и ещё +2,5 % за каждое государство на его пути.
  pc.routes = routes;
  pc.tradeBase = std::max(0.0, p.baseTrade);
  pc.tradeValue = std::max(0.0, pc.tradeBase * (1.0 + fx[Fx::TradePct] / 100.0 + routeBonus) + fx[Fx::TradeFlat]);

  // Добыча ресурса.
  if (p.resource && w.resource(p.resource))
    pc.production = std::max(0.0, std::max(0.0, p.resourceAmount) * (1.0 + fx[Fx::ResourcePct] / 100.0) + fx[Fx::ResourceFlat]);

  // Восстание: 2 довольства : 1 % (ТЗ 1.a.vi).
  pc.rebellion = clamp(-p.contentment * schema::kRebellionPerContentment + fx[Fx::RebellionPct], 0.0, 100.0);
  pc.buildCostFactor = costFactorOf(fx);

  // Налог (ТЗ 1.d.iv): государственный ≥ 0, общий — от 1 % до 100 % (налог не больше торговой ценности).
  pc.taxState = owner ? std::max(0.0, owner->tax) : 0.0;
  pc.taxLocal = p.localTax;
  pc.taxTotal = clamp(pc.taxState + pc.taxLocal, schema::kMinTotalTax, schema::kMaxTotalTax);

  // Население.
  std::map<Id, i64> races;
  for (const RacePop& r : p.races) {
    i64 n = std::max<i64>(0, r.pop);
    races[r.race] += n;
    pc.population += n;
  }
  pc.races = sortedRaces(races);

  // Гильдии: доход только при наличии штаба.
  const double V = pc.tradeValue, t = pc.taxTotal;
  double infSum = 0;
  for (const Influence& in : p.influence) {
    const Faction* g = w.faction(in.guild);
    if (g && g->isGuild() && std::isfinite(in.pct) && in.pct > 0) infSum += in.pct;
  }
  const double k = infSum > 100.0 ? 100.0 / infSum : 1.0;  // повреждённые данные: доли сжимаются до 100 %
  for (const Influence& in : p.influence) {
    const Faction* g = w.faction(in.guild);
    if (!g || !g->isGuild() || !std::isfinite(in.pct) || in.pct <= 0) continue;
    auto it = std::find_if(pc.guilds.begin(), pc.guilds.end(), [&](const GuildShare& s) { return s.guild == in.guild; });
    if (it == pc.guilds.end()) {
      GuildShare s;
      s.guild = in.guild;
      s.hq = contains(p.hqs, in.guild);
      pc.guilds.push_back(s);
      it = pc.guilds.end() - 1;
    }
    it->pct += in.pct * k;
  }
  for (Id g : p.hqs) {
    const Faction* gf = w.faction(g);
    if (!gf || !gf->isGuild()) continue;
    if (std::none_of(pc.guilds.begin(), pc.guilds.end(), [&](const GuildShare& s) { return s.guild == g; })) {
      GuildShare s;
      s.guild = g;
      s.hq = true;
      pc.guilds.push_back(s);
    }
  }
  double grossHq = 0;
  for (GuildShare& s : pc.guilds) {
    if (!s.hq) continue;
    s.gross = V * s.pct / 100.0;
    s.tax = s.gross * t / 100.0;
    s.net = s.gross - s.tax;
    grossHq += s.gross;
    pc.guildTax += s.tax;
  }
  std::stable_sort(pc.guilds.begin(), pc.guilds.end(), [](const GuildShare& a, const GuildShare& b) {
    if (a.pct != b.pct) return a.pct > b.pct;
    return a.guild < b.guild;
  });
  pc.provinceTax = std::max(0.0, V - grossHq) * t / 100.0;

  // Получатель дохода (оккупация — по настройкам правил).
  pc.recipient = owner ? owner->id : 0;
  const Faction* occ = p.occupied ? w.faction(p.occupier) : nullptr;
  if (occ && occ->id != p.owner) {
    switch (w.settings->occupiedIncome) {
      case OccupiedIncome::Owner: break;
      case OccupiedIncome::Occupier: pc.recipient = occ->id; break;
      case OccupiedIncome::None: pc.recipient = 0; break;
    }
  }
  // Пустошь нежити и осквернённая провинция (ТЗ «Виды государств», п.5, 9): доход с текущей ценности — только
  // государству нежити (демонов).
  const bool waste = hasModKey(w, p.modifiers, schema::mod::UndeadWaste), desecr = hasModKey(w, p.modifiers, schema::mod::Desecrated);
  if (waste || desecr) {
    const StateKind need = waste ? StateKind::Undead : StateKind::Demonic;
    const Faction* rcp = w.faction(pc.recipient);
    if (!rcp || !rcp->isState() || rcp->stateKind != need) {
      pc.tradeBlocked = true;
      pc.provinceTax = 0;
      pc.guildTax = 0;
    }
    if (desecr && owner) pc.energy = schema::kDesecratedEnergy * std::floor(double(pc.population) * schema::kDesecrateShare);
  }
  // Рабы на работах: доход владельцу (ТЗ «Механика мятежа», п.5).
  if (owner)
    for (const SlaveWork& s : p.slaves) pc.slavesAtWork += std::max<i64>(0, s.count);
  pc.slaveIncome = double(pc.slavesAtWork) * schema::kSlaveWorkIncome;
  // Ресурсы от достроенных построек — владельцу.
  if (owner)
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      int lvl = pb.builtLevel();
      if (!b || lvl < 1 || lvl > int(b->levels.size())) continue;
      for (auto& [res, v] : b->levels[size_t(lvl - 1)].produce)
        if (std::isfinite(v) && v > 0 && w.resource(res)) pc.produce[res] += v;
    }
}

RowCalc rowCalc(Id id, i64 total, double upkeep, i64 inArmies, i64 inGarrison, i64 other, double factor) {
  RowCalc r;
  r.row = id;
  r.total = total;
  r.garrison = inGarrison;
  r.field = inArmies + inGarrison + other;
  r.reserve = total - r.field;
  r.upkeepEach = upkeep;
  r.upkeepTotal = double(total) * upkeep * factor;
  return r;
}

i64 at(const std::map<Id, i64>& m, Id k) {
  auto it = m.find(k);
  return it == m.end() ? 0 : it->second;
}

}  // namespace

std::shared_ptr<const Calc> compute(const World& w, const geo::FaceSet* fs) {
  auto out = std::make_shared<Calc>();
  Calc& c = *out;
  SourceIndex si(w);

  // Маршруты: провинции на линии маршрута (без повторов внутри маршрута). Бонус маршрута провинции: +10 % и ещё
  // +2,5 % за каждое государство, по землям которого он проходит (ТЗ «Общие доработки», п.9).
  std::unordered_map<Id, double> routeBonus;
  std::vector<std::pair<Id, std::vector<Id>>> routeProvs;   // гильдия-владелец → провинции пути
  if (fs && !fs->faces.empty())
    w.routes.each([&](const Route& r) {
      if (r.pts.empty()) return;
      std::vector<Id> provs = fs->provincesOnPolyline(r.pts);
      std::vector<Id> states;
      for (Id p : provs) {
        c.routeCounts[p]++;
        if (const Province* pr = w.province(p); pr && !pr->sea)
          if (const Faction* o = w.faction(pr->owner); o && o->isState() && !contains(states, o->id)) states.push_back(o->id);
      }
      const double bonus = schema::kRouteBonus + schema::kRouteStateBonus * double(states.size());
      for (Id p : provs) routeBonus[p] += bonus;
      if (r.guild) routeProvs.push_back({r.guild, std::move(provs)});
    });

  // Владения государств и штабы гильдий.
  std::unordered_map<Id, std::vector<const Province*>> owned, hqOf;
  w.provinces.each([&](const Province& p) {
    if (p.sea) return;
    if (const Faction* o = w.faction(p.owner); o && o->isState()) owned[p.owner].push_back(&p);
    for (Id g : p.hqs)
      if (const Faction* gf = w.faction(g); gf && gf->isGuild() && !contains(hqOf[g], &p)) hqOf[g].push_back(&p);
  });

  // Развёрнутые силы всех фракций за один проход.
  std::unordered_map<Id, Deployed> dep;
  w.armies.each([&](const Army& a) {
    for (const ArmyGroup& g : a.groups)
      for (const ArmyUnit& u : g.units) (a.isFleet() ? dep[g.faction].fleet : dep[g.faction].army)[u.row] += u.count;
  });
  w.provinces.each([&](const Province& p) {
    if (p.owner)
      for (const GarrisonEntry& g : p.garrison) dep[p.owner].garrison[g.row] += g.count;
    if (p.occupied && p.occupier)
      for (const GarrisonEntry& g : p.occGarrison) dep[p.occupier].occupation[g.row] += g.count;
  });
  w.factions.each([&](const Faction& f) {
    for (const GarrisonEntry& g : f.tradeFleet) dep[f.id].trade[g.row] += g.count;
  });

  w.factions.each([&](const Faction& f) {
    FactionCalc& fc = c.factions[f.id];
    fc.id = f.id;
    fc.treasury = f.treasury();
  });
  auto fcOf = [&](Id id) -> FactionCalc* {
    auto it = c.factions.find(id);
    return it == c.factions.end() ? nullptr : &it->second;
  };

  // Провинции и доходы получателей.
  c.provinces.reserve(w.provinces.size());
  w.provinces.each([&](const Province& p) {
    auto rit = c.routeCounts.find(p.id);
    auto bit = routeBonus.find(p.id);
    ProvinceCalc& pc = c.provinces[p.id];
    calcProvince(w, si, p, rit == c.routeCounts.end() ? 0 : rit->second, bit == routeBonus.end() ? 0.0 : bit->second, pc);
    if (pc.sea) return;
    if (FactionCalc* r = fcOf(pc.recipient)) {
      r->incProvinces += pc.provinceTax;
      r->incGuildTax += pc.guildTax;
      if (pc.production > 0) {
        if (p.resource == kGold) r->incProvinces += pc.production;  // добыча золота идёт в казну как доход провинции
        r->resources[p.resource].production += pc.production;
      }
    }
    if (FactionCalc* o = fcOf(pc.owner)) {
      o->incSlaves += pc.slaveIncome;
      for (auto& [res, v] : pc.produce) {
        if (res == kGold) o->incProvinces += v;
        o->resources[res].production += v;
      }
      if (pc.energy > 0)
        if (Id e = resourceId(w, schema::kResEnergy)) o->resources[e].production += pc.energy;
    }
    for (const GuildShare& s : pc.guilds)
      if (s.hq)
        if (FactionCalc* g = fcOf(s.guild)) g->incGuilds += s.net;
  });

  // Гильдия — владелец маршрута: 5 % текущей торговой ценности каждой сухопутной провинции пути.
  for (auto& [guild, provs] : routeProvs) {
    FactionCalc* g = fcOf(guild);
    const Faction* gf = w.faction(guild);
    if (!g || !gf || !gf->isGuild()) continue;
    for (Id p : provs)
      if (const ProvinceCalc* pc = c.province(p); pc && !pc->sea) g->incRoutes += pc->tradeValue * schema::kGuildRouteShare;
  }

  // Сделки «каждый ход»: золото — доход/расход, прочие ресурсы — потоки.
  w.deals.each([&](const Deal& d) {
    if (d.status != DealStatus::Active) return;
    for (const DealItem& it : d.items) {
      if (it.kind != DealItemKind::Resource || it.mode != DealMode::PerTurn || it.left <= 0 || !(it.amount > 0)) continue;
      Id payer = it.from == DealSide::A ? d.a : d.b, payee = it.from == DealSide::A ? d.b : d.a;
      FactionCalc* fp = fcOf(payer);
      FactionCalc* fr = fcOf(payee);
      if (!fp || !fr) continue;
      if (it.res == kGold) {
        if (d.kind == DealKind::Trade) {
          fr->incTrade += it.amount;
          fp->expTrade += it.amount;
        } else {
          fr->incTribute += it.amount;
          fp->expTribute += it.amount;
        }
      } else {
        fr->resources[it.res].tradeIn += it.amount;
        fp->resources[it.res].tradeOut += it.amount;
      }
    }
  });

  // Специалисты: содержание различных персонажей с ролью у фракции — правитель, места совета (персонаж любой
  // фракции), герои самой фракции. Персонаж без роли не оплачивается; занимающий несколько ролей — один раз;
  // мёртвый или пленный герой недоступен государству (ТЗ «Модификаторы», 1.6 и 1.12) и не оплачивается.
  std::unordered_map<Id, std::vector<Id>> heroesOf;
  w.characters.each([&](const Character& ch) {
    if (ch.hero && ch.faction) heroesOf[ch.faction].push_back(ch.id);
  });
  w.factions.each([&](const Faction& f) {
    std::vector<Id> paid;
    auto add = [&](Id ch) {
      if (ch && w.character(ch) && heroAvailable(w, ch) && !contains(paid, ch)) paid.push_back(ch);
    };
    add(f.ruler);
    for (const CouncilSeat& s : f.council) add(s.character);
    if (auto hit = heroesOf.find(f.id); hit != heroesOf.end())
      for (Id ch : hit->second) add(ch);
    FactionCalc& fc = c.factions[f.id];
    for (Id ch : paid) fc.expSpecialists += std::max(0.0, w.character(ch)->upkeep);
  });

  const Id provisions = resourceId(w, schema::kResProvisions);
  static const std::vector<const Province*> kNone;
  w.factions.each([&](const Faction& f) {
    FactionCalc& fc = c.factions[f.id];
    auto oit = owned.find(f.id);
    const std::vector<const Province*>& mine = f.isState() ? (oit == owned.end() ? kNone : oit->second) : kNone;
    fc.fx = factionFx(w, si, f, mine);
    fc.researchFactor = std::max(0.05, 1.0 + fc.fx[Fx::ResearchTimePct] / 100.0);

    // Провинции: государство — владения, гильдия — штабы.
    if (f.isState()) {
      std::map<Id, i64> races;
      for (const Province* p : mine) {
        fc.provinces.push_back(p->id);
        const ProvinceCalc& pc = c.provinces[p->id];
        fc.population += pc.population;
        for (auto& [r, n] : pc.races) races[r] += n;
      }
      fc.races = sortedRaces(races);
    } else if (auto hit = hqOf.find(f.id); hit != hqOf.end()) {
      for (const Province* p : hit->second) fc.provinces.push_back(p->id);
    }

    // Войска и флот.
    const Deployed& d = dep[f.id];
    std::map<Id, i64> forming;
    for (const Formation& q : f.forming) forming[q.row] += q.count;
    double armyK = std::max(0.0, 1.0 + fc.fx[Fx::ArmyUpkeepPct] / 100.0);
    double fleetK = std::max(0.0, 1.0 + fc.fx[Fx::FleetUpkeepPct] / 100.0);
    for (const ArmyRow& r : f.army) {
      RowCalc rc = rowCalc(r.id, r.total, std::max(0.0, r.upkeep), at(d.army, r.id), at(d.garrison, r.id), at(d.occupation, r.id), armyK);
      rc.occupation = at(d.occupation, r.id);
      rc.forming = at(forming, r.id);
      fc.expArmy += rc.upkeepTotal;
      fc.armyTotal += rc.total;
      fc.armyField += rc.field;
      fc.army.push_back(rc);
    }
    i64 galleons = 0, frigates = 0, lines = 0;
    for (const FleetRow& r : f.fleet) {
      RowCalc rc = rowCalc(r.id, r.total, std::max(0.0, r.upkeep), at(d.fleet, r.id), 0, at(d.trade, r.id), fleetK);
      rc.trade = at(d.trade, r.id);
      rc.forming = at(forming, r.id);
      fc.expFleet += rc.upkeepTotal;
      fc.fleetTotal += rc.total;
      fc.fleetField += rc.field;
      fc.fleet.push_back(rc);
      // Флот в торговле (ТЗ «Общие доработки», п.11): торговые галеоны приносят своё содержание × 2.
      if (rc.trade > 0) {
        if (r.type == ShipType::Galleon) {
          galleons += rc.trade;
          fc.incTradeFleet += double(rc.trade) * std::max(0.0, r.upkeep) * fleetK * schema::kFleetTradeIncome;
        } else if (r.type == ShipType::Frigate) {
          frigates += rc.trade;
        } else if (r.type == ShipType::ShipOfLine) {
          lines += rc.trade;
        }
      }
    }
    // Каждые 10 галеонов — 1 % вероятности нападения пиратов, если на них нет охраны: 5 фрегатов или 1 линкор.
    {
      const i64 blocks = galleons / schema::kPirateGalleons;
      const i64 guarded = std::min(blocks, frigates / schema::kPirateFrigates + lines / schema::kPirateLines);
      fc.pirateRisk = std::min(100.0, double(blocks - guarded));
    }

    // Рабы: содержание 0,001 золота за раба.
    for (const SlaveGroup& s : f.slaves) fc.slaves += std::max<i64>(0, s.count);
    fc.expSlaves = double(fc.slaves) * schema::kSlaveUpkeep;

    // Итоги (ТЗ 1.e.i, 1.g.ii.2.a). Модификатор дохода действует на собственный доход фракции (налоги, штабы,
    // добыча золота, рабы на работах, торговый флот, маршруты гильдии); выплаты по сделкам, дань и репарации
    // передаются без изменений (ТЗ 1.e.ii: что отнято у одного, то и отдано другому).
    const double own = fc.incProvinces + fc.incGuildTax + fc.incGuilds + fc.incSlaves + fc.incTradeFleet + fc.incRoutes;
    fc.incGross = own + fc.incTrade + fc.incTribute;
    fc.incomePct = fc.fx[Fx::IncomePct];
    fc.incTotal = own * std::max(0.0, 1.0 + fc.incomePct / 100.0) + fc.incTrade + fc.incTribute;
    fc.expTotal = fc.expArmy + fc.expFleet + fc.expSpecialists + fc.expTrade + fc.expTribute + fc.expSlaves;
    fc.net = fc.incTotal - fc.expTotal;

    // Ресурсы: все позиции справочника и запасы фракции.
    for (const CatalogItem& ci : w.catalogs->resources) fc.resources[ci.id];
    for (auto& [r, v] : f.res) fc.resources[r];
    // Провизия государства живых: 0,001 на жителя за ход (ТЗ «Общие доработки», п.7).
    if (provisions && f.isState() && f.stateKind == StateKind::Living) fc.resources[provisions].consumption = double(fc.population) * schema::kProvisionsPerPerson;
    for (auto& [r, flow] : fc.resources) {
      if (r == kGold) {
        flow.stock = f.treasury();
        flow.tradeIn = fc.incTrade + fc.incTribute;
        flow.tradeOut = fc.expTrade + fc.expTribute;
        flow.net = fc.net;
      } else {
        flow.stock = f.stock(r);
        flow.net = flow.production + flow.tradeIn - flow.tradeOut - flow.consumption;
      }
    }
    fc.famine = provisions && f.stock(provisions) < -1e-9;
  });
  return out;
}

}  // namespace detail

// ================================================================ кеш
namespace {

struct CacheEntry {
  World key;  // копия удерживает блоки таблиц: адреса не переиспользуются
  std::shared_ptr<const Calc> calc;
};
std::mutex gCalcMu;
std::vector<CacheEntry> gCalcCache;  // свежие — в начале
constexpr size_t kCalcCacheSize = 6;

// Расчёт не зависит от хроники.
bool sameInputs(const World& a, const World& b) { return (World::diff(a, b) & ~u32(TB_LOG)) == 0; }

}  // namespace

std::shared_ptr<const Calc> calc(const World& w) {
  {
    std::lock_guard<std::mutex> lk(gCalcMu);
    for (size_t i = 0; i < gCalcCache.size(); i++) {
      if (sameInputs(gCalcCache[i].key, w)) {
        auto c = gCalcCache[i].calc;
        if (i > 0) std::rotate(gCalcCache.begin(), gCalcCache.begin() + long(i), gCalcCache.begin() + long(i) + 1);
        return c;
      }
    }
  }
  std::shared_ptr<const geo::FaceSet> fs;
  if (!w.routes.empty()) fs = geo::faces(w);
  std::shared_ptr<const Calc> c = detail::compute(w, fs.get());
  std::lock_guard<std::mutex> lk(gCalcMu);
  for (auto& e : gCalcCache)
    if (sameInputs(e.key, w)) return e.calc;
  gCalcCache.insert(gCalcCache.begin(), CacheEntry{w, c});
  if (gCalcCache.size() > kCalcCacheSize) gCalcCache.resize(kCalcCacheSize);
  return c;
}

std::shared_ptr<const Calc> calc(const Tx& tx) {
  if (tx.touched() == 0) return calc(tx.w());
  std::shared_ptr<const geo::FaceSet> fs;
  if (!tx.w().routes.empty()) fs = detail::facesFor(tx);
  return detail::compute(tx.w(), fs.get());
}

Deployed deployed(const World& w, Id faction) {
  Deployed d;
  if (!faction) return d;
  w.armies.each([&](const Army& a) {
    for (const ArmyGroup& g : a.groups) {
      if (g.faction != faction) continue;
      for (const ArmyUnit& u : g.units) (a.isFleet() ? d.fleet : d.army)[u.row] += u.count;
    }
  });
  w.provinces.each([&](const Province& p) {
    if (p.owner == faction)
      for (const GarrisonEntry& g : p.garrison) d.garrison[g.row] += g.count;
    if (p.occupied && p.occupier == faction)
      for (const GarrisonEntry& g : p.occGarrison) d.occupation[g.row] += g.count;
  });
  if (const Faction* f = w.faction(faction))
    for (const GarrisonEntry& g : f->tradeFleet) d.trade[g.row] += g.count;
  return d;
}

}  // namespace rg::rules
