// Regnum — расчёт провинций и фракций (RULES.md §2–3), кеш расчёта, развёрнутые силы.
#include <mutex>

#include "rules/internal.h"

namespace rg::rules {

namespace detail {

Id occupierOf(const World& w, const Province& p) {
  if (!p.occupied || !p.occupier || p.occupier == p.owner || !w.faction(p.occupier)) return 0;
  return p.occupier;
}

namespace {

// Численности рас: слить повторы, по убыванию численности, затем по ID.
std::vector<std::pair<Id, i64>> sortedRaces(const std::map<Id, i64>& m) {
  std::vector<std::pair<Id, i64>> r(m.begin(), m.end());
  std::stable_sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.second > b.second; });
  return r;
}

// Генерация модификатора за ход (эссенции и ресурсы).
void addGen(const World& w, const Modifier& m, std::map<Id, double>& ess, std::map<Id, double>& res, double k = 1) {
  for (auto& [e, v] : modEssGen(w, m))
    if (std::isfinite(v) && v != 0 && w.essence(e)) ess[e] += v * k;
  for (auto& [r, v] : modResGen(w, m))
    if (std::isfinite(v) && v != 0 && w.resource(r)) res[r] += v * k;
}

// Глобальные константы мира, которые читает расчёт (один раз на расчёт).
struct Rates {
  double taxNorm = 10, taxUnrest = 0.2;   // «Налог без недовольства», «Недовольство налогом» (за процент сверх нормы)
  double growthBase = 0.5;                // «Естественный прирост», % за ход
  double galleon = 0.5;                   // «Доход торгового галеона», золото за ход
  double famineDeaths = 10;               // «Гибель от голода», % голодающих за ход
  explicit Rates(const World& w) {
    auto num = [&](const char* key, double lo, double hi) {
      const double v = constantOf(w, key).num;
      return std::isfinite(v) ? std::clamp(v, lo, hi) : 0.0;
    };
    taxNorm = num(schema::cst::TaxNorm, 0, 100);
    taxUnrest = num(schema::cst::TaxUnrest, 0, 100);
    growthBase = num(schema::cst::PopGrowthBase, -100, 100);
    galleon = num(schema::cst::GalleonIncome, 0, 1e9);
    famineDeaths = num(schema::cst::FamineDeaths, 0, 100);
  }
};

// Предел населения провинции по величине (маленькая, средняя, большая), у столицы владельца — × 1,5.
constexpr i64 kPopCap[3] = {40000, 120000, 300000};
constexpr double kCapitalPopCap = 1.5;
i64 popCapOf(const Province& p, const Faction* owner) {
  double cap = double(kPopCap[std::clamp(int(p.size), 0, 2)]);
  if (owner && owner->capital == p.id) cap *= kCapitalPopCap;
  return i64(std::llround(cap));
}

// Живые воины строки армии едят провизию: кроме нежити, демонов, военных механизмов и элементалей.
bool eatsProvisions(const World& w, Id faction, const ArmyRow& r) {
  if (r.type == UnitType::Machines || r.type == UnitType::Elementals) return false;
  const std::string race = unitRace(w, faction, r);
  return race != schema::kRaceUndead && race != schema::kRaceDemonic && race != schema::kRaceMechanical && race != schema::kRaceElemental;
}

// Золото — до тысячных.
double roundGold(double v) { return std::round(v * 1000.0) / 1000.0; }

void calcProvince(const World& w, const SourceIndex& si, const Rates& rt, const Province& p, int routes, double routeBonus, ProvinceCalc& pc) {
  pc.id = p.id;
  pc.sea = p.sea;
  pc.owner = p.owner;
  if (p.sea) return;  // морская провинция не содержит информации и не участвует в расчётах

  const Faction* owner = w.faction(p.owner);
  if (owner && !owner->isState()) owner = nullptr;
  pc.fx = provinceFx(w, si, p);
  const Effects& fx = pc.fx;

  // Слоты: величина провинции, тип города по населению, модификаторы.
  pc.city = cityType(p);
  pc.slotsSize = int(p.size) >= 0 && int(p.size) < 3 ? schema::kProvSizes[int(p.size)].value : 0;
  pc.slotsCity = schema::cityType(pc.city).value;
  pc.slotsMods = int(std::lround(fx[Fx::Slots]));
  pc.slots = slotsOf(p, fx);
  pc.slotsUsed = int(p.buildings.size());

  // Население.
  std::map<Id, i64> races;
  for (const RacePop& r : p.races) {
    i64 n = std::max<i64>(0, r.pop);
    races[r.race] += n;
    pc.population += n;
  }
  pc.races = sortedRaces(races);

  // Торговая ценность: каждый маршрут +10 % и ещё +2,5 % за каждое государство на его пути; население — +1 за каждые
  // 100 жителей.
  pc.routes = routes;
  pc.tradeBase = std::max(0.0, p.baseTrade);
  pc.tradePop = double(pc.population) * schema::kTradePerPerson;
  pc.tradeValue = std::max(0.0, pc.tradeBase * (1.0 + fx[Fx::TradePct] / 100.0 + routeBonus) + fx[Fx::TradeFlat] + pc.tradePop);

  // Добыча ресурса: модификаторы и достроенные постройки добычи (шахта), которым подходит ресурс провинции.
  if (owner)
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      const int lvl = pb.builtLevel();
      if (!b || !b->extract || lvl < 1 || lvl > int(b->levels.size()) || !extractAllowed(w, *b, p.resource)) continue;
      const double x = b->levels[size_t(lvl - 1)].extractPct;
      if (std::isfinite(x) && x > 0) pc.extractPct += x;
    }
  if (p.resource && w.resource(p.resource))
    pc.production = std::max(0.0, std::max(0.0, p.resourceAmount) * (1.0 + (fx[Fx::ResourcePct] + pc.extractPct) / 100.0) + fx[Fx::ResourceFlat]);

  // Восстание: 2 довольства : 1 %.
  pc.rebellion = clamp(-p.contentment * schema::kRebellionPerContentment + fx[Fx::RebellionPct], 0.0, 100.0);
  pc.buildCostFactor = costFactorOf(fx);

  // Налог: государственный ≥ 0, общий — от 1 % до 100 % (налог не больше торговой ценности).
  pc.taxState = owner ? std::max(0.0, owner->tax) : 0.0;
  pc.taxLocal = p.localTax;
  pc.taxTotal = clamp(pc.taxState + pc.taxLocal, schema::kMinTotalTax, schema::kMaxTotalTax);
  // Общий налог сверх нормы снижает довольство провинции государства за ход.
  if (owner && pc.taxTotal > rt.taxNorm) pc.taxUnrest = -rt.taxUnrest * (pc.taxTotal - rt.taxNorm);
  pc.contentDelta = fx[Fx::ContentmentPerTurn] + pc.taxUnrest;

  // Прирост населения: естественный у государства живых (не в пустоши нежити) и модификаторы; положительный убывает
  // к пределу населения провинции (голод владельца обнуляет его — в расчёте государства).
  const bool waste = hasModKey(w, p.modifiers, schema::mod::UndeadWaste), desecr = hasModKey(w, p.modifiers, schema::mod::Desecrated);
  pc.popCap = popCapOf(p, owner);
  if (owner && owner->stateKind == StateKind::Living && !waste) pc.growthBase = rt.growthBase;
  pc.growthPct = pc.growthBase + fx[Fx::PopGrowthPct];
  if (pc.growthPct > 0) pc.growthPct *= std::max(0.0, 1.0 - double(pc.population) / double(std::max<i64>(1, pc.popCap)));

  // Гильдии: доход только при наличии штаба. В пустоши нежити и осквернённой провинции доход с ценности получает только
  // государство нежити (демонов): чистый доход штабов гильдий не начисляется.
  pc.guildBlocked = waste || desecr;
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
    s.net = pc.guildBlocked ? 0.0 : s.gross - s.tax;
    grossHq += s.gross;
    pc.guildTax += s.tax;
  }
  std::stable_sort(pc.guilds.begin(), pc.guilds.end(), [](const GuildShare& a, const GuildShare& b) {
    if (a.pct != b.pct) return a.pct > b.pct;
    return a.guild < b.guild;
  });
  pc.provinceTax = std::max(0.0, V - grossHq) * t / 100.0;

  // Получатель дохода (оккупация — по настройкам правил): налоги, добыча, постройки, рабы на работах.
  pc.recipient = owner ? owner->id : 0;
  if (const Id occ = occupierOf(w, p)) {
    pc.occupied = true;
    switch (w.settings->occupiedIncome) {
      case OccupiedIncome::Owner: break;
      case OccupiedIncome::Occupier: pc.recipient = occ; break;
      case OccupiedIncome::None: pc.recipient = 0; break;
    }
  }
  // Пустошь нежити и осквернённая провинция: доход с текущей ценности — только
  // государству нежити (демонов).
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
  // Рабы на работах (рабы владельца): доход — получателю дохода.
  if (owner)
    for (const SlaveWork& s : p.slaves) pc.slavesAtWork += std::max<i64>(0, s.count);
  pc.slaveIncome = double(pc.slavesAtWork) * schema::kSlaveWorkIncome;
  // Ресурсы и эссенции от достроенных построек — получателю дохода.
  if (owner)
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      int lvl = pb.builtLevel();
      if (!b || lvl < 1 || lvl > int(b->levels.size())) continue;
      const BuildingLevel& L = b->levels[size_t(lvl - 1)];
      // Производство уровня; при подходящем ресурсе провинции — своё количество (лесопилка, каменоломня).
      std::map<Id, double> prod = L.produce;
      for (auto& [res, v] : L.produceOnRes)
        if (res == p.resource) prod[res] = v;
      for (auto& [res, v] : prod)
        if (std::isfinite(v) && v > 0 && w.resource(res)) pc.produce[res] += v;
      if (b->essenceGen)
        for (auto& [e, v] : L.essence)
          if (std::isfinite(v) && v > 0 && w.essence(e)) pc.essence[e] += v;
      // Генерация модификаторов действующего уровня постройки.
      for (Id m : L.modifiers)
        if (const Modifier* x = w.modifier(m)) addGen(w, *x, pc.essence, pc.produce);
    }
  // Генерация модификаторов провинции (её список и автоматические: «Ценности археологии») — тоже получателю дохода.
  if (owner) {
    for (Id m : p.modifiers)
      if (const Modifier* x = w.modifier(m)) addGen(w, *x, pc.essence, pc.produce);
    for (const AutoMod& a : autoProvinceModifiers(w, p.id))
      if (a.m) addGen(w, *a.m, pc.essence, pc.produce);
  }
}

// Поровну между запасами (наполнение): каждый отдаёт need / k, исчерпанный — всё, остаток делится между остальными.
std::map<Id, double> takeEvenly(double need, std::vector<std::pair<Id, double>> avail) {
  std::map<Id, double> take;
  std::stable_sort(avail.begin(), avail.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
  double left = std::max(0.0, need);
  for (size_t i = 0; i < avail.size() && left > 1e-12; i++) {
    const double share = left / double(avail.size() - i);
    const double t = std::min(avail[i].second, share);
    if (t > 0) take[avail[i].first] = t;
    left -= t;
  }
  return take;
}

// План построек преобразования фракции на конец хода (порядок — провинции по ID, постройки по списку): идущий цикл
// продвигается; новый начинается, если хватает ресурсов (avail — запасы после добычи и расхода провизии).
// Эссенции на входе рецепта берутся из запаса с чистым потоком хода (availEss). В оккупированной провинции циклы стоят.
void planConversion(const World& w, const Calc& c, const std::vector<const Province*>& mine, std::map<Id, double>& avail,
                    std::map<Id, double>& availEss, FactionCalc& fc) {
  int maxMult = 0;   // множитель, открытый государству (считается при первой постройке с множителем больше ×1)
  for (const Province* p : mine) {
    if (p->sea) continue;
    if (const ProvinceCalc* pc = c.province(p->id); pc && pc->occupied) continue;
    for (const ProvBuilding& pb : p->buildings) {
      const Building* b = w.building(pb.building);
      if (!b || !b->convert || pb.builtLevel() < 1 || pb.idle) continue;
      const Recipe* rcp = activeRecipe(*b, pb);
      if (!rcp) continue;
      const Recipe& rc = *rcp;
      ConvertStep s;
      s.province = p->id;
      s.building = b->id;
      s.recipe = std::clamp(pb.recipe, 0, int(b->recipes.size()) - 1);
      // Множитель производства: цикл берёт и даёт по рецепту mult раз — не больше множителя, открытого владельцу
      // (провинция с фабрикой ×100 досталась государству без «Стимуляции промышленности»).
      s.mult = std::clamp(pb.mult, 1, schema::kMaxProductionMult);
      if (s.mult > 1) {
        if (!maxMult) maxMult = maxProductionMult(w, fc.id);
        s.mult = std::min(s.mult, maxMult);
      }
      const double k = double(s.mult);
      for (const ResAmount& x : rc.out)
        if (x.res && x.amount > 0 && w.resource(x.res)) s.out.push_back(ResAmount{x.res, x.amount * k});
      if (pb.cycle > 0) {
        s.cycle = pb.cycle - 1;
      } else {
        bool any = false, ok = !s.out.empty();
        for (const ResAmount& x : rc.in) {
          if (!(x.amount > 0) || !w.resource(x.res)) continue;
          any = true;
          if (avail[x.res] + 1e-9 < x.amount * k) ok = false;
        }
        for (const ResAmount& x : rc.essIn) {
          if (!(x.amount > 0) || !w.essence(x.res)) continue;
          any = true;
          if (availEss[x.res] + 1e-9 < x.amount * k) ok = false;
        }
        if (!any || !ok) continue;   // нет рецепта или не хватает ресурсов — ждёт
        s.start = true;
        for (const ResAmount& x : rc.in)
          if (x.amount > 0 && w.resource(x.res)) {
            s.in[x.res] += x.amount * k;
            avail[x.res] -= x.amount * k;
          }
        for (const ResAmount& x : rc.essIn)
          if (x.amount > 0 && w.essence(x.res)) {
            s.essIn[x.res] += x.amount * k;
            availEss[x.res] -= x.amount * k;
          }
        s.cycle = std::max(1, rc.turns) - 1;
      }
      s.finish = s.cycle == 0;
      for (auto& [r, v] : s.in) fc.resources[r].conversionIn += v;
      for (auto& [e, v] : s.essIn) fc.essences[e].conversionIn += v;
      if (s.finish)
        for (const ResAmount& x : s.out) fc.resources[x.res].conversionOut += x.amount;
      fc.conversions.push_back(std::move(s));
    }
  }
}

// Постройки иссушения: «Работает» и выбрана раса — у неё отнимается до drainPop жителей, эссенции — пропорционально
// взятым (меньше жителей — меньше эссенции). В оккупированной провинции иссушение стоит, как и циклы преобразования.
void planDrain(const World& w, const Calc& c, const std::vector<const Province*>& mine, FactionCalc& fc) {
  for (const Province* p : mine) {
    if (p->sea) continue;
    if (const ProvinceCalc* pc = c.province(p->id); pc && pc->occupied) continue;
    std::map<Id, i64> left;
    for (const RacePop& r : p->races) left[r.race] += std::max<i64>(0, r.pop);
    for (const ProvBuilding& pb : p->buildings) {
      const Building* b = w.building(pb.building);
      const int lvl = pb.builtLevel();
      if (!b || !b->drain || lvl < 1 || lvl > int(b->levels.size()) || pb.idle || !pb.race) continue;
      const BuildingLevel& L = b->levels[size_t(lvl - 1)];
      const i64 take = std::min(std::max<i64>(0, L.drainPop), left[pb.race]);
      if (take <= 0) continue;
      left[pb.race] -= take;
      DrainStep d;
      d.province = p->id;
      d.building = b->id;
      d.race = pb.race;
      d.pop = take;
      for (auto& [e, v] : L.drainEss)
        if (std::isfinite(v) && v > 0 && w.essence(e) && L.drainPop > 0) {
          const double got = v * double(take) / double(L.drainPop);
          d.ess[e] += got;
          fc.essences[e].generation += got;
        }
      fc.drains.push_back(std::move(d));
    }
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

// Полный расчёт мира (без кеша).
std::shared_ptr<const Calc> compute(const World& w, const geo::FaceSet* fs) {
  auto out = std::make_shared<Calc>();
  Calc& c = *out;
  SourceIndex si(w);
  const Rates rates(w);

  // Маршруты: провинции на линии маршрута (без повторов внутри маршрута). Бонус маршрута провинции: +10 % и ещё
  // +2,5 % за каждое государство, по землям которого он проходит. Маршрут не работает через оккупированные провинции и
  // провинции государств в войне с его гильдией: им нет бонуса, их государства не считаются, гильдия не получает с них 5 %.
  std::unordered_map<Id, double> routeBonus;
  std::vector<std::pair<Id, std::vector<Id>>> routeProvs;   // гильдия-владелец → провинции пути
  if (fs && !fs->faces.empty())
    w.routes.each([&](const Route& r) {
      if (r.pts.empty()) return;
      std::vector<Id> provs = fs->provincesOnPolyline(r.pts), open, states;
      for (Id p : provs) {
        c.routeCounts[p]++;
        const Province* pr = w.province(p);
        if (!pr || pr->sea || occupierOf(w, *pr)) continue;
        if (r.guild && pr->owner && w.relation(pr->owner, r.guild).s == RelStatus::War) continue;
        open.push_back(p);
        if (const Faction* o = w.faction(pr->owner); o && o->isState() && !contains(states, o->id)) states.push_back(o->id);
      }
      const double bonus = schema::kRouteBonus + schema::kRouteStateBonus * double(states.size());
      for (Id p : open) routeBonus[p] += bonus;
      if (r.guild) routeProvs.push_back({r.guild, std::move(open)});
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
    calcProvince(w, si, rates, p, rit == c.routeCounts.end() ? 0 : rit->second, bit == routeBonus.end() ? 0.0 : bit->second, pc);
    if (pc.sea) return;
    // Всё, что провинция даёт за ход, — получателю дохода (оккупированная — по настройкам правил): налоги, добыча,
    // постройки и модификаторы провинции (ресурсы и эссенции), рабы на работах, энергия осквернения.
    if (FactionCalc* r = fcOf(pc.recipient)) {
      r->incProvinces += pc.provinceTax;
      r->incGuildTax += pc.guildTax;
      if (pc.production > 0) {
        if (p.resource == kGold) r->incProvinces += pc.production;  // добыча золота идёт в казну как доход провинции
        r->resources[p.resource].production += pc.production;
      }
      r->incSlaves += pc.slaveIncome;
      for (auto& [res, v] : pc.produce) {
        if (res == kGold) r->incProvinces += v;
        r->resources[res].production += v;
      }
      for (auto& [e, v] : pc.essence) r->essences[e].generation += v;
      if (pc.energy > 0)
        if (Id e = resourceId(w, schema::kResEnergy)) r->resources[e].production += pc.energy;
    }
    for (const GuildShare& s : pc.guilds)
      if (s.hq)
        if (FactionCalc* g = fcOf(s.guild)) g->incGuilds += s.net;
  });

  // Гильдия — владелец маршрута: 5 % текущей торговой ценности каждой сухопутной провинции пути (кроме пустоши нежити и
  // осквернённых провинций).
  for (auto& [guild, provs] : routeProvs) {
    FactionCalc* g = fcOf(guild);
    const Faction* gf = w.faction(guild);
    if (!g || !gf || !gf->isGuild()) continue;
    for (Id p : provs)
      if (const ProvinceCalc* pc = c.province(p); pc && !pc->sea && !pc->guildBlocked) g->incRoutes += pc->tradeValue * schema::kGuildRouteShare;
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
  // мёртвый или пленный герой недоступен государству и не оплачивается.
  std::unordered_map<Id, std::vector<Id>> heroesOf, charsOf;
  w.characters.each([&](const Character& ch) {
    if (ch.hero && ch.faction) heroesOf[ch.faction].push_back(ch.id);
    if (ch.faction) charsOf[ch.faction].push_back(ch.id);
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

  const std::vector<Id> provisionRes = provisionResources(w);
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

    // Войска и флот. Постройки «Снижение содержания армии» уменьшают содержание войск государства (не больше 50 %).
    const Deployed& d = dep[f.id];
    std::map<Id, i64> forming;
    for (const Formation& q : f.forming) forming[q.row] += q.count;
    if (f.isState())
      for (const Province* p : mine)
        for (const ProvBuilding& pb : p->buildings) {
          const Building* b = w.building(pb.building);
          const int lvl = pb.builtLevel();
          if (b && b->upkeepCut && lvl >= 1 && lvl <= int(b->levels.size()) && std::isfinite(b->levels[size_t(lvl - 1)].upkeepCut))
            fc.upkeepCut += std::max(0.0, b->levels[size_t(lvl - 1)].upkeepCut);
        }
    fc.upkeepCut = std::min(fc.upkeepCut, schema::kMaxUpkeepCut);
    double armyK = std::max(0.0, 1.0 + fc.fx[Fx::ArmyUpkeepPct] / 100.0) * (1.0 - fc.upkeepCut / 100.0);
    double fleetK = std::max(0.0, 1.0 + fc.fx[Fx::FleetUpkeepPct] / 100.0);
    for (const ArmyRow& r : f.army) {
      // Элементали содержатся эссенциями, а не золотом.
      const bool elem = schema::isElemental(r.type);
      RowCalc rc = rowCalc(r.id, r.total, elem ? 0.0 : std::max(0.0, r.upkeep), at(d.army, r.id), at(d.garrison, r.id), at(d.occupation, r.id), armyK);
      rc.occupation = at(d.occupation, r.id);
      rc.forming = at(forming, r.id);
      fc.expArmy += rc.upkeepTotal;
      fc.armyTotal += rc.total;
      fc.armyField += rc.field;
      fc.army.push_back(rc);
      if (elem)
        for (auto& [e, v] : r.essUpkeep)
          if (v > 0 && w.essence(e)) fc.essences[e].upkeep += double(std::max<i64>(0, r.total)) * v * armyK;
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
      // Флот в торговле: каждый торговый галеон приносит «Доход торгового галеона» за ход — от содержания не зависит
      // (модификатор содержания флота меняет только расход).
      if (rc.trade > 0) {
        if (r.type == ShipType::Galleon) {
          galleons += rc.trade;
          fc.incTradeFleet += double(rc.trade) * rates.galleon;
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
    // Археологические группы: содержание по уровню с модификаторами.
    for (const ArchGroup& g : f.archGroups) fc.expArch += archStatsWith(w, g, fc.fx).upkeep;

    // Генерация модификаторов государства (его список, изученные технологии, автоматические) и его героев
    // (модификаторы и таланты; мёртвые и пленные — нет).
    {
      std::map<Id, double> gEss, gRes;
      auto genOf = [&](Id m, double k = 1) {
        if (const Modifier* x = w.modifier(m)) addGen(w, *x, gEss, gRes, k);
      };
      for (Id m : f.modifiers) genOf(m);
      if (auto* ts = si.of(f.id))
        for (const Tech* t : *ts)
          for (Id m : t->modifiers) genOf(m);
      if (auto* as = si.autosOf(f.id))
        for (const AutoMod& a : *as)
          if (a.m) addGen(w, *a.m, gEss, gRes);
      if (auto hit = charsOf.find(f.id); hit != charsOf.end())
        for (Id ch : hit->second) {
          if (!heroAvailable(w, ch)) continue;
          for (const HeroMod& hm : heroModifiers(w, ch)) genOf(hm.modifier);
        }
      for (auto& [e, v] : gEss) fc.essences[e].generation += v;
      for (auto& [r, v] : gRes) {
        if (r == kGold) fc.incProvinces += v;
        fc.resources[r].production += v;
      }
    }

    // Итоги. Модификатор дохода действует на собственный доход фракции (налоги, штабы,
    // добыча золота, рабы на работах, торговый флот, маршруты гильдии); выплаты по сделкам, дань и репарации
    // передаются без изменений.
    const double own = fc.incProvinces + fc.incGuildTax + fc.incGuilds + fc.incSlaves + fc.incTradeFleet + fc.incRoutes;
    fc.incGross = own + fc.incTrade + fc.incTribute;
    fc.incomePct = fc.fx[Fx::IncomePct];
    fc.incTotal = own * std::max(0.0, 1.0 + fc.incomePct / 100.0) + fc.incTrade + fc.incTribute;
    fc.expTotal = fc.expArmy + fc.expFleet + fc.expSpecialists + fc.expTrade + fc.expTribute + fc.expSlaves + fc.expArch;
    fc.net = roundGold(fc.incTotal - fc.expTotal);   // в казну — до тысячных
    fc.unpaid = f.isState() && f.treasury() < -1e-9;

    // Ресурсы: все позиции справочника и запасы фракции.
    for (const CatalogItem& ci : w.catalogs->resources) fc.resources[ci.id];
    for (auto& [r, v] : f.res) fc.resources[r];
    // Провизия государства живых: 0,001 на жителя и живого воина армии за ход — поровну со всех ресурсов группы
    // «Провизия», что есть у государства после добычи этого хода. Недостача — только этого хода (не копится):
    // «Голод», гибель «Гибель от голода» % голодающих, положительный прирост провинций — 0.
    const bool living = f.isState() && f.stateKind == StateKind::Living;
    std::map<Id, double> avail;   // запасы после шага 2 хода: добыча и производство, казна — с чистым доходом
    for (auto& [r, flow] : fc.resources)
      avail[r] = r == kGold ? f.treasury() + fc.net : f.stock(r) + std::max(0.0, flow.production);
    if (living) {
      for (const ArmyRow& r : f.army)
        if (eatsProvisions(w, f.id, r)) fc.provisionSoldiers += std::max<i64>(0, r.total);
      for (const Formation& q : f.forming)
        if (const ArmyRow* r = f.armyRow(q.row); r && eatsProvisions(w, f.id, *r)) fc.provisionSoldiers += std::max<i64>(0, q.count);
      fc.provisionNeed = double(fc.population + fc.provisionSoldiers) * schema::kProvisionsPerPerson;
      fc.provisionDebt = std::max(0.0, f.provisionDebt);
      std::vector<std::pair<Id, double>> stocks;
      for (Id r : provisionRes) {
        fc.provisionStock += std::max(0.0, f.stock(r));
        if (avail[r] > 1e-12) stocks.push_back({r, avail[r]});
      }
      const double need = fc.provisionNeed;
      double taken = 0;
      for (auto& [r, v] : takeEvenly(need, stocks)) {
        fc.resources[r].consumption += v;
        avail[r] -= v;
        taken += v;
      }
      fc.provisionDebtNext = need - taken > 1e-9 ? need - taken : 0.0;
      if (fc.provisionDebtNext > 0) {
        const double starving = fc.provisionDebtNext / schema::kProvisionsPerPerson;
        fc.famineDeaths = std::min(fc.population, i64(std::floor(starving * rates.famineDeaths / 100.0 + 1e-6)));
        for (const Province* p : mine) {
          ProvinceCalc& pc = c.provinces[p->id];
          pc.growthPct = std::min(pc.growthPct, 0.0);
        }
      }
    }
    // Эссенции: генерация построек и модификаторов провинций (получателю дохода — уже в потоке), иссушение населения,
    // содержание элементалей; затем постройки преобразования (эссенции на входе — из запаса с потоком хода).
    for (const CatalogItem& e : w.catalogs->essences) fc.essences[e.id];
    for (auto& [e, v] : f.ess) fc.essences[e];
    if (f.isState()) planDrain(w, c, mine, fc);
    std::map<Id, double> availEss;
    for (auto& [e, flow] : fc.essences) availEss[e] = std::max(0.0, f.essence(e) + flow.generation - flow.upkeep);
    if (f.isState()) planConversion(w, c, mine, avail, availEss, fc);
    for (auto& [r, flow] : fc.resources) {
      if (r == kGold) {
        flow.stock = f.treasury();
        flow.tradeIn = fc.incTrade + fc.incTribute;
        flow.tradeOut = fc.expTrade + fc.expTribute;
        flow.net = fc.net - flow.consumption - flow.conversionIn + flow.conversionOut;
      } else {
        flow.stock = f.stock(r);
        flow.net = flow.production + flow.tradeIn - flow.tradeOut - flow.consumption - flow.conversionIn + flow.conversionOut;
      }
    }
    fc.famine = living && f.provisionDebt > 1e-9;
    for (auto& [e, flow] : fc.essences) {
      flow.stock = f.essence(e);
      flow.net = flow.generation - flow.upkeep - flow.conversionIn;
    }
  });
  return out;
}

}  // namespace
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

CityType cityType(const Province& p) {
  i64 pop = 0;
  for (const RacePop& r : p.races) pop += std::max<i64>(0, r.pop);
  return schema::cityTypeFor(pop);
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
