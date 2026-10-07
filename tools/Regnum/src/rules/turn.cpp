// Regnum — завершение хода (RULES.md §4). Детерминированно: обход по возрастанию ID, случайность — Rng с зерном.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

constexpr i64 kMaxCount = 1000000000000000LL;

// Зерно броска восстания: «ход + провинция»; нападения пиратов: «ход + фракция».
u64 rebellionSeed(int turn, Id province) { return hashMix(hashMix(0x5245424C4C494F4Eull, u64(u32(turn))), u64(province)); }
u64 pirateSeed(int turn, Id faction) { return hashMix(hashMix(0x5049524154455321ull, u64(u32(turn))), u64(faction)); }

std::string dealDoneText(const World& w, const Deal& d) {
  switch (d.kind) {
    case DealKind::Tribute: return "Завершена выплата дани: " + facName(w, d.b) + " → " + facName(w, d.a);
    case DealKind::Reparations: return "Завершена выплата репараций: " + facName(w, d.b) + " → " + facName(w, d.a);
    case DealKind::Trade: break;
  }
  return "Выполнена сделка " + facName(w, d.a) + " и " + facName(w, d.b);
}

std::string modName(const World& w, Id m) {
  const Modifier* x = w.modifier(m);
  return q(x && !x->name.empty() ? x->name : std::string("Без названия"));
}

}  // namespace

TurnReport endTurn(Tx& tx) {
  TurnReport rep;
  const int turn = tx.w().turn();
  rep.turnFrom = turn;
  rep.turnTo = turn + 1;
  auto log = [&](LogKind kind, const std::string& text, LogRefs refs) { rep.logIds.push_back(addLog(tx, kind, text, refs)); };

  // Расчёт по состоянию на начало процедуры.
  const std::shared_ptr<const Calc> c = calc(tx);
  const std::vector<Id> factionIds = tx.w().factions.ids();
  std::vector<std::map<Id, double>> before, beforeEss;
  before.reserve(factionIds.size());
  beforeEss.reserve(factionIds.size());
  for (Id id : factionIds) {
    before.push_back(tx.w().faction(id)->res);
    beforeEss.push_back(tx.w().faction(id)->ess);
  }

  // 2. Казна — чистый доход; ресурсы — добыча провинций, производство построек, энергия осквернённых провинций.
  for (Id id : factionIds) {
    const FactionCalc* fc = c->faction(id);
    if (!fc) continue;
    bool prod = std::any_of(fc->resources.begin(), fc->resources.end(), [](auto& kv) { return kv.first != kGold && kv.second.production > 0; });
    if (fc->net == 0 && !prod) continue;
    double was = tx.w().faction(id)->treasury();
    Faction& f = tx.faction(id);
    addStock(f, kGold, fc->net);
    for (auto& [r, flow] : fc->resources)
      if (r != kGold && flow.production > 0) addStock(f, r, flow.production);
    double now = f.treasury();
    if (now < 0)
      log(LogKind::Economy,
          was >= 0 ? "Казна " + facName(tx.w(), id) + " ушла в долг: " + fmtNum(now, 2) : "Долг казны " + facName(tx.w(), id) + ": " + fmtNum(now, 2),
          LogRefs{0, 0, {id}});
  }

  // 2а. Провизия государств живых (ТЗ «Добавления в справочники», п.3): 0,001 на жителя и недостача прошлых ходов —
  // поровну со всех ресурсов группы «Провизия»; чего не хватило — недостача («Голод», пока она не покрыта).
  for (Id id : factionIds) {
    const FactionCalc* fc = c->faction(id);
    const Faction* f0 = tx.w().faction(id);
    if (!fc || !f0 || !f0->isState()) continue;
    if (f0->stateKind != StateKind::Living) {
      if (f0->provisionDebt != 0) tx.faction(id).provisionDebt = 0;   // провизия нужна только живым
      continue;
    }
    for (auto& [r, flow] : fc->resources)
      if (flow.consumption > 0) addStock(tx.faction(id), r, -flow.consumption);
    const double was = f0->provisionDebt, now = fc->provisionDebtNext;
    if (now != was) tx.faction(id).provisionDebt = now;
    if (now > 1e-9 && was <= 1e-9)
      log(LogKind::Economy, facName(tx.w(), id) + ": провизия закончилась (недостача " + fmtNum(now, 2) + ") — голод", LogRefs{0, 0, {id}});
    else if (now <= 1e-9 && was > 1e-9)
      log(LogKind::Economy, facName(tx.w(), id) + ": недостача провизии покрыта — голод закончился", LogRefs{0, 0, {id}});
  }

  // 2б. Постройки преобразования (ТЗ «Доработки», п.3): начало цикла списывает ресурсы на входе, завершение даёт
  // ресурс на выходе (план расчёта на начало хода).
  for (Id id : factionIds) {
    const FactionCalc* fc = c->faction(id);
    if (!fc || fc->conversions.empty()) continue;
    std::map<Id, double> gained;
    for (const ConvertStep& s : fc->conversions) {
      Faction& f = tx.faction(id);
      for (auto& [r, v] : s.in) addStock(f, r, -v);
      if (s.finish && s.out.res && s.out.amount > 0 && tx.w().resource(s.out.res)) {
        addStock(tx.faction(id), s.out.res, s.out.amount);
        gained[s.out.res] += s.out.amount;
      }
      for (ProvBuilding& pb : tx.province(s.province).buildings)
        if (pb.building == s.building) pb.cycle = s.cycle;
    }
    if (!gained.empty()) {
      std::vector<std::string> parts;
      for (auto& [r, v] : gained) parts.push_back(resName(tx.w(), r) + " +" + amount(v));
      log(LogKind::Economy, facName(tx.w(), id) + ": преобразование ресурсов — " + join(parts, ", "), LogRefs{0, 0, {id}});
    }
  }

  // 2в. Эссенции элементов: генерация построек и содержание элементалей (запас может уйти в долг).
  for (Id id : factionIds) {
    const FactionCalc* fc = c->faction(id);
    if (!fc) continue;
    std::vector<std::string> debts;
    for (auto& [e, flow] : fc->essences) {
      if (flow.net == 0 || !tx.w().essence(e)) continue;
      Faction& f = tx.faction(id);
      const double was = f.essence(e);
      double& v = f.ess[e];
      v += flow.net;
      if (std::fabs(v) < 1e-9) f.ess.erase(e);
      const double now = tx.w().faction(id)->essence(e);
      if (now < 0 && was >= 0) debts.push_back(tx.w().essence(e)->name + " " + fmtNum(now, 2));
    }
    if (!debts.empty())
      log(LogKind::Economy, facName(tx.w(), id) + ": не хватает эссенций на содержание элементалей — " + join(debts, ", "), LogRefs{0, 0, {id}});
  }

  // 3. Сделки «каждый ход»: ресурсы кроме золота; оставшиеся ходы; завершение.
  for (Id did : idsWhere(tx.w().deals, [](const Deal& d) { return d.status == DealStatus::Active; })) {
    Deal d = *tx.w().deal(did);
    bool anyLeft = false;
    for (DealItem& it : d.items) {
      if (it.kind != DealItemKind::Resource || it.mode != DealMode::PerTurn || it.left <= 0) continue;
      if (it.res != kGold && it.amount > 0) {
        Id payer = it.from == DealSide::A ? d.a : d.b, payee = it.from == DealSide::A ? d.b : d.a;
        const Faction* fp = tx.w().faction(payer);
        const Faction* fr = tx.w().faction(payee);
        if (fp && fr) {
          double give = std::min(it.amount, std::max(0.0, fp->stock(it.res)));
          if (give > 0) {
            addStock(tx.faction(payer), it.res, -give);
            addStock(tx.faction(payee), it.res, give);
          }
          if (give + 1e-9 < it.amount)
            log(LogKind::Trade,
                "Недостача по сделке: " + facName(tx.w(), payer) + " → " + facName(tx.w(), payee) + ", " + resName(tx.w(), it.res) + " " +
                    amount(give) + " из " + amount(it.amount),
                LogRefs{0, 0, {payer, payee}});
        }
      }
      it.left--;
      if (it.left > 0) anyLeft = true;
    }
    if (!anyLeft) {
      d.status = DealStatus::Done;
      log(d.kind == DealKind::Trade ? LogKind::Trade : LogKind::Diplomacy, dealDoneText(tx.w(), d), LogRefs{0, 0, {d.a, d.b}});
    }
    tx.deal(did) = std::move(d);
  }

  // 4. Строительство. В морской провинции стройка приостановлена (море не участвует в расчётах).
  int built = 0;
  for (Id pid : idsWhere(tx.w().provinces, [](const Province& p) {
         return !p.sea && std::any_of(p.buildings.begin(), p.buildings.end(), [](const ProvBuilding& b) { return b.constructing; });
       })) {
    std::vector<std::pair<Id, int>> done;
    Province& p = tx.province(pid);
    for (ProvBuilding& pb : p.buildings) {
      if (!pb.constructing) continue;
      if (--pb.left <= 0) {
        pb.left = 0;
        pb.constructing = false;
        pb.paid.clear();  // уплаченное нужно только для возврата при отмене
        pb.payer = 0;
        done.push_back({pb.building, pb.level});
      }
    }
    Id owner = p.owner;
    for (auto& [b, lvl] : done) {
      built++;
      log(LogKind::Build,
          "Достроено: " + buildingName(tx.w(), b) + (lvl > 1 ? " (уровень " + std::to_string(lvl) + ")" : std::string()) + " в провинции " +
              provName(tx.w(), pid),
          LogRefs{pid, 0, owner ? std::vector<Id>{owner} : std::vector<Id>{}});
    }
  }

  // 5. Исследования: срок — с модификатором «Время исследования технологий» государства.
  int studied = 0;
  for (Id tid : idsWhere(tx.w().techs, [](const Tech& t) { return t.research && !t.studied; })) {
    const Tech& t0 = *tx.w().tech(tid);
    Id faction = t0.faction;
    if (!canResearch(tx.w(), tid).missing.empty()) {
      tx.tech(tid).research = false;
      log(LogKind::Tech, facName(tx.w(), faction) + ": исследование " + techName(tx.w(), tid) + " остановлено — не изучены предшествующие технологии",
          LogRefs{0, 0, {faction}});
      continue;
    }
    const FactionCalc* fc = c->faction(faction);
    const double k = fc ? fc->researchFactor : 1.0;
    const int need = std::max(1, int(std::ceil(double(std::max(1, t0.turns)) * k - 1e-9)));
    Tech& t = tx.tech(tid);
    t.progress = std::max(0, t.progress) + 1;
    if (t.progress >= need) {
      t.progress = std::max(1, t.turns);
      t.studied = true;
      t.research = false;
      studied++;
      log(LogKind::Tech, facName(tx.w(), faction) + ": изучена технология " + techName(tx.w(), tid), LogRefs{0, 0, {faction}});
    }
  }
  // 5а. Общие технологии: каждая фракция исследует общее дерево сама (ТЗ «Доработки», п.6).
  for (Id fid : idsWhere(tx.w().factions, [](const Faction& f) {
         return std::any_of(f.techs.begin(), f.techs.end(), [](const auto& kv) { return kv.second.research && !kv.second.studied; });
       })) {
    const FactionCalc* fc = c->faction(fid);
    const double k = fc ? fc->researchFactor : 1.0;
    std::vector<Id> list;
    for (auto& [tid, s] : tx.w().faction(fid)->techs)
      if (s.research && !s.studied) list.push_back(tid);
    for (Id tid : list) {
      const Tech* t0 = tx.w().tech(tid);
      if (!t0) continue;
      if (!canResearch(tx.w(), tid, fid).missing.empty()) {
        tx.faction(fid).techs[tid].research = false;
        log(LogKind::Tech, facName(tx.w(), fid) + ": исследование " + techName(tx.w(), tid) + " остановлено — не изучены предшествующие технологии",
            LogRefs{0, 0, {fid}});
        continue;
      }
      const int need = std::max(1, int(std::ceil(double(std::max(1, t0->turns)) * k - 1e-9)));
      const int turns = std::max(1, t0->turns);
      TechProgress& s = tx.faction(fid).techs[tid];
      s.progress = std::max(0, s.progress) + 1;
      if (s.progress >= need) {
        s.progress = turns;
        s.studied = true;
        s.research = false;
        studied++;
        log(LogKind::Tech, facName(tx.w(), fid) + ": изучена общая технология " + techName(tx.w(), tid), LogRefs{0, 0, {fid}});
      }
    }
  }

  // 6–7. Население и довольство (эффекты — на начало хода). В пустоши нежити население не растёт выше 0.
  for (Id pid : tx.w().provinces.ids()) {
    const ProvinceCalc* pc = c->province(pid);
    if (!pc || pc->sea) continue;
    const Province& p = *tx.w().province(pid);
    const bool waste = hasModKey(tx.w(), p.modifiers, schema::mod::UndeadWaste);
    const double growth = pc->fx[Fx::PopGrowthPct];
    const double k = 1.0 + growth / 100.0;
    std::vector<RacePop> races = p.races;
    bool ch = false;
    for (RacePop& r : races) {
      const i64 was = std::max<i64>(0, r.pop);
      double v = std::round(double(was) * k);
      i64 n = v <= 0 ? 0 : (v >= double(kMaxCount) ? kMaxCount : i64(v));
      // Небольшая группа при ненулевом приросте меняется хотя бы на 1 за ход (иначе округление её «замораживает»).
      if (n == was && was > 0) {
        if (growth > 0 && was < kMaxCount) n = was + 1;
        else if (growth < 0) n = was - 1;
      }
      if (waste) n = 0;
      if (n != r.pop) {
        r.pop = n;
        ch = true;
      }
    }
    double cont = clamp(p.contentment + pc->fx[Fx::ContentmentPerTurn], -100.0, 100.0);
    if (!ch && cont == p.contentment) continue;
    Province& m = tx.province(pid);
    m.races = std::move(races);
    m.contentment = cont;
  }

  // 8. Дипломатия: модификаторы отношений с целями.
  for (Id id : factionIds) {
    const FactionCalc* fc = c->faction(id);
    if (!fc) continue;
    for (auto& [target, delta] : fc->fx.diplomacy) {
      if (delta == 0 || target == id || !tx.w().faction(target)) continue;
      Relation r = tx.w().relation(id, target);
      r.v = clamp(r.v + delta, -100.0, 100.0);
      tx.setRelation(id, target, r);
    }
  }

  // 9. Формирование отрядов и кораблей: через 2 хода — в резерв (ТЗ «Общие доработки», п.10.2).
  for (Id id : idsWhere(tx.w().factions, [](const Faction& f) { return !f.forming.empty(); })) {
    std::vector<Formation> keep, done;
    for (Formation q : tx.w().faction(id)->forming) {
      if (--q.left <= 0) done.push_back(q);
      else keep.push_back(q);
    }
    Faction& f = tx.faction(id);
    f.forming = std::move(keep);
    for (const Formation& q : done) {
      std::string name;
      bool fleet = false;
      for (ArmyRow& r : f.army)
        if (r.id == q.row) {
          r.total = std::min(kMaxCount, r.total + q.count);
          name = r.name;
        }
      for (FleetRow& r : f.fleet)
        if (r.id == q.row) {
          r.total = std::min(kMaxCount, r.total + q.count);
          name = r.name;
          fleet = true;
        }
      if (!name.empty())
        log(fleet ? LogKind::Fleet : LogKind::Army, facName(tx.w(), id) + ": сформировано «" + name + "» — " + fmtInt(q.count) + ", в резерве",
            LogRefs{0, 0, {id}});
    }
  }

  // 10. Верность войск (эффекты — на начало хода). Верность −100 % — войско восстаёт целиком (ТЗ «Мятеж», п.1.2).
  std::vector<Id> rebelled;
  for (Id aid : tx.w().armies.ids()) {
    const Army& a = *tx.w().army(aid);
    const FactionCalc* lc = c->faction(a.leader());
    const Effects fx = armyFx(tx.w(), a, lc ? &lc->fx : nullptr);
    double v = hasModKey(tx.w(), a.modifiers, schema::mod::UndeadArmy) ? schema::kMaxLoyalty
                                                                       : clamp(a.loyalty + loyaltyDeltaOf(tx.w(), a, fx), schema::kMinLoyalty, schema::kMaxLoyalty);
    if (v != a.loyalty) tx.army(aid).loyalty = v;
    if (v <= schema::kMinLoyalty && !a.allied() && !tx.w().faction(a.leader())->rebelOf) rebelled.push_back(aid);
  }
  for (Id aid : rebelled) {
    if (!tx.w().army(aid)) continue;
    std::string why;
    if (!canMutiny(tx.w(), aid, &why)) continue;
    MutinyResult m = mutinyArmies(tx, {aid}, aid);
    TurnEvent e;
    e.kind = TurnEvent::Mutiny;
    e.army = m.rebelArmy;
    e.rebelState = m.rebelState;
    e.origin = tx.w().faction(m.rebelState) ? tx.w().faction(m.rebelState)->rebelOf : 0;
    e.province = m.rebelArmy ? provinceAtTx(tx, tx.w().army(m.rebelArmy)->pos) : 0;
    e.heroes = m.loyalHeroes;
    rep.events.push_back(std::move(e));
  }
  // 10а. Верность гарнизонов (ТЗ «Доработки», п.1): как у войск — эффекты государства и «Непреклонный лоялист»;
  // у государства нежити — всегда 100 %. Гарнизон с верностью −100 % восстаёт целиком (событие хода).
  {
    std::vector<Id> risen;
    for (Id pid : idsWhere(tx.w().provinces, [](const Province& p) { return !p.sea && p.owner && !p.garrison.empty(); })) {
      const Province& p = *tx.w().province(pid);
      const Faction* o = tx.w().faction(p.owner);
      if (!o || !o->isState()) continue;
      double v = schema::kMaxLoyalty;
      if (o->stateKind != StateKind::Undead) {
        const FactionCalc* oc = c->faction(p.owner);
        double d = oc ? oc->fx[Fx::LoyaltyPerTurn] : 0.0;
        int loyalists = 0;
        for (Id h : p.garrisonHeroes)
          if (characterHas(tx.w(), h, schema::mod::Loyalist)) loyalists++;
        if (loyalists) d = std::max(0.0, d) + schema::kLoyalistBonus * loyalists;
        v = clamp(p.garrisonLoyalty + d, schema::kMinLoyalty, schema::kMaxLoyalty);
      }
      if (v != p.garrisonLoyalty) tx.province(pid).garrisonLoyalty = v;
      if (v <= schema::kMinLoyalty && !o->rebelOf) risen.push_back(pid);
    }
    for (Id pid : risen) {
      std::string why;
      if (!canGarrisonMutiny(tx.w(), pid, &why)) continue;
      MutinyResult m = garrisonMutiny(tx, pid);
      TurnEvent e;
      e.kind = TurnEvent::Mutiny;
      e.army = m.rebelArmy;
      e.rebelState = m.rebelState;
      e.origin = tx.w().faction(m.rebelState) ? tx.w().faction(m.rebelState)->rebelOf : 0;
      e.province = pid;
      e.heroes = m.loyalHeroes;
      rep.events.push_back(std::move(e));
    }
  }

  // 11. Восстания провинций (по настройке): бросок с зерном «ход + провинция»; восставшие — армия мятежников.
  if (tx.w().settings->rebellionRoll) {
    for (Id pid : tx.w().provinces.ids()) {
      const ProvinceCalc* pc = c->province(pid);
      const Province* p = tx.w().province(pid);
      if (!p || !pc || pc->sea || !p->owner || !(pc->rebellion > 0)) continue;
      const Id owner = p->owner;
      Rng rng(rebellionSeed(turn, pid));
      double roll = rng.uniform() * 100.0;
      if (roll < pc->rebellion) {
        rep.rebellions.push_back(pid);
        log(LogKind::Province, "Восстание в провинции " + provName(tx.w(), pid) + " (вероятность " + fmtPct(pc->rebellion, 1) + ")",
            LogRefs{pid, 0, {owner}});
        if (const Faction* o = tx.w().faction(owner); o && o->rebelOf) continue;   // мятежники не восстают против себя
        if (Id army = provinceUprising(tx, pid)) {
          TurnEvent e;
          e.kind = TurnEvent::Uprising;
          e.army = army;
          e.province = pid;
          e.origin = owner;
          e.rebelState = tx.w().army(army)->leader();
          rep.events.push_back(std::move(e));
        }
      }
    }
  }

  // 12. Пираты (ТЗ «Общие доработки», п.11): бросок по вероятности, сгенерированной в конце прошлого хода;
  // затем новая вероятность по кораблям в торговле.
  for (Id id : factionIds) {
    const Faction* f0 = tx.w().faction(id);
    if (!f0) continue;
    const double risk = f0->pirateRisk;
    if (risk > 0) {
      Rng rng(pirateSeed(turn, id));
      if (rng.uniform() * 100.0 < risk) {
        i64 galleons = 0;
        for (const GarrisonEntry& g : f0->tradeFleet)
          if (const FleetRow* r = f0->fleetRow(g.row); r && r->type == ShipType::Galleon) galleons += g.count;
        i64 lost = i64(std::ceil(double(galleons) * schema::kPirateLossPct / 100.0 - 1e-9));
        if (lost > 0) {
          i64 left = lost;
          Faction& f = tx.faction(id);
          for (GarrisonEntry& g : f.tradeFleet) {
            const FleetRow* r = f.fleetRow(g.row);
            if (!r || r->type != ShipType::Galleon || left <= 0) continue;
            i64 take = std::min(left, g.count);
            g.count -= take;
            left -= take;
            for (FleetRow& fr : f.fleet)
              if (fr.id == g.row) fr.total = std::max<i64>(0, fr.total - take);
          }
          f.tradeFleet.erase(std::remove_if(f.tradeFleet.begin(), f.tradeFleet.end(), [](const GarrisonEntry& g) { return g.count <= 0; }),
                             f.tradeFleet.end());
          log(LogKind::Fleet, "Нападение пиратов на торговые корабли " + facName(tx.w(), id) + ": потеряно галеонов — " + fmtInt(lost - left),
              LogRefs{0, 0, {id}});
        }
      }
    }
    const FactionCalc* fc = c->faction(id);
    const double next = fc ? fc->pirateRisk : 0.0;
    if (tx.w().faction(id)->pirateRisk != next) tx.faction(id).pirateRisk = next;
  }

  // 13. Оккупация без гарнизона и войск оккупанта 5 ходов подряд снимается (ТЗ «Общие доработки», п.14).
  {
    std::unordered_map<Id, std::vector<Id>> armiesIn;   // провинция → фракции, чьи войска в ней
    auto fs = facesFor(tx);
    if (fs)
      tx.w().armies.each([&](const Army& a) {
        if (a.isFleet()) return;
        Id pid = fs->provinceAt(a.pos);
        if (!pid) return;
        for (const ArmyGroup& g : a.groups)
          if (!contains(armiesIn[pid], g.faction)) armiesIn[pid].push_back(g.faction);
      });
    for (Id pid : idsWhere(tx.w().provinces, [](const Province& p) { return p.occupied && p.occupier; })) {
      const Province& p = *tx.w().province(pid);
      bool present = !p.occGarrison.empty() || contains(armiesIn[pid], p.occupier);
      if (present) {
        if (p.occIdle) tx.province(pid).occIdle = 0;
        continue;
      }
      int idle = p.occIdle + 1;
      if (idle >= schema::kOccupationIdleTurns) {
        const Id occ = p.occupier;
        setOccupied(tx, pid, 0);
        log(LogKind::War, "Оккупация провинции " + provName(tx.w(), pid) + " снята: у " + facName(tx.w(), occ) + " нет гарнизона и войск в ней " +
                              nTurns(schema::kOccupationIdleTurns),
            LogRefs{pid, 0, {occ}});
      } else {
        tx.province(pid).occIdle = idle;
      }
    }
  }

  // 14. Сроки модификаторов: −1 ход; истёкшие снимаются.
  {
    auto expire = [&](ModTarget t, Id id, const ModTurns& turns, const std::string& where, LogRefs refs) {
      if (turns.empty()) return;
      ModTurns next;
      std::vector<Id> gone;
      for (auto& [m, n] : turns) {
        if (n - 1 <= 0) gone.push_back(m);
        else next[m] = n - 1;
      }
      for (Id m : gone) {
        const std::string name = modName(tx.w(), m);
        dropModifier(tx, t, id, m);
        log(LogKind::Note, "Истёк модификатор " + name + where, refs);
      }
      switch (t) {
        case ModTarget::Province: tx.province(id).modTurns = next; break;
        case ModTarget::Faction: tx.faction(id).modTurns = next; break;
        case ModTarget::Army: tx.army(id).modTurns = next; break;
        case ModTarget::Character: tx.character(id).modTurns = next; break;
      }
    };
    for (Id pid : idsWhere(tx.w().provinces, [](const Province& p) { return !p.modTurns.empty(); })) {
      const Province& p = *tx.w().province(pid);
      expire(ModTarget::Province, pid, p.modTurns, " в провинции " + provName(tx.w(), pid), LogRefs{pid, 0, p.owner ? std::vector<Id>{p.owner} : std::vector<Id>{}});
    }
    for (Id fid : idsWhere(tx.w().factions, [](const Faction& f) { return !f.modTurns.empty(); }))
      expire(ModTarget::Faction, fid, tx.w().faction(fid)->modTurns, " у " + facName(tx.w(), fid), LogRefs{0, 0, {fid}});
    for (Id aid : idsWhere(tx.w().armies, [](const Army& a) { return !a.modTurns.empty(); })) {
      const Army& a = *tx.w().army(aid);
      expire(ModTarget::Army, aid, a.modTurns, " у " + armyName(tx.w(), aid), LogRefs{0, aid, {a.leader()}});
    }
    for (Id cid : idsWhere(tx.w().characters, [](const Character& ch) { return !ch.modTurns.empty(); })) {
      const Character& ch = *tx.w().character(cid);
      expire(ModTarget::Character, cid, ch.modTurns, " у " + q(tx.w().characterName(cid)), LogRefs{0, 0, ch.faction ? std::vector<Id>{ch.faction} : std::vector<Id>{}});
    }
  }

  // 15. Постройки сверх слотов (слоты уменьшились) сносятся с конца списка.
  trimExcessBuildings(tx);

  // 16. Мятежное государство без армий и провинций упраздняется (ТЗ «Мятеж», п.7; записи хроники сохраняются).
  for (Id fid : idsWhere(tx.w().factions, [](const Faction& f) { return f.rebelOf != 0; })) {
    bool any = false;
    tx.w().armies.each([&](const Army& a) {
      for (const ArmyGroup& g : a.groups) any = any || g.faction == fid;
    });
    tx.w().provinces.each([&](const Province& p) { any = any || p.owner == fid || (p.occupied && p.occupier == fid); });
    if (!any) removeFaction(tx, fid);
  }

  // Итог по фракциям.
  for (size_t i = 0; i < factionIds.size(); i++) {
    Id id = factionIds[i];
    const Faction* f = tx.w().faction(id);
    const FactionCalc* fc = c->faction(id);
    if (!f || !fc) continue;
    TurnFactionLine line;
    line.faction = id;
    auto bt = before[i].find(kGold);
    line.treasuryBefore = bt == before[i].end() ? 0 : bt->second;
    line.treasuryAfter = f->treasury();
    line.income = fc->incTotal;
    line.expenses = fc->expTotal;
    std::map<Id, double> keys = before[i];
    for (auto& [r, v] : f->res) keys[r];
    for (auto& [r, v] : keys) {
      if (r == kGold) continue;
      auto b = before[i].find(r);
      double d = f->stock(r) - (b == before[i].end() ? 0.0 : b->second);
      if (std::fabs(d) > 1e-9) line.resources[r] = d;
    }
    std::map<Id, double> ekeys = beforeEss[i];
    for (auto& [e, v] : f->ess) ekeys[e];
    for (auto& [e, v] : ekeys) {
      auto b = beforeEss[i].find(e);
      double d = f->essence(e) - (b == beforeEss[i].end() ? 0.0 : b->second);
      if (std::fabs(d) > 1e-9) line.essences[e] = d;
    }
    line.provisionDebt = f->provisionDebt;
    rep.factions.push_back(std::move(line));
  }

  // 17. Номер хода; итог — в хронику.
  log(LogKind::Turn,
      "Завершён ход " + std::to_string(turn) + ". Построек достроено: " + std::to_string(built) + ", технологий изучено: " + std::to_string(studied) +
          (tx.w().settings->rebellionRoll ? ", восстаний: " + std::to_string(rep.rebellions.size()) : std::string()),
      LogRefs{});
  tx.meta().turn = turn + 1;
  return rep;
}

TurnReport previewTurn(const World& w) {
  Tx tx(w);  // черновик отбрасывается: исходный мир неизменяем
  return endTurn(tx);
}

}  // namespace rg::rules
