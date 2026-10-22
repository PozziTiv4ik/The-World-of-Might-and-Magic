// Regnum — нормализация: войска и флот (группы, отряды, герои, войско на борту), торговые маршруты, сделки,
// хроника, заговоры, отношения.
#include "core/io_normalize.h"

namespace rg::io::norm {

void Norm::armies() {
  const World base = w();
  std::unordered_set<Id>& usedHeroes = heroUsed;   // герои гарнизонов уже учтены (provinces)
  base.armies.each([&](const Army& a0) {
    Army a = a0;
    const std::string where = refStr(Seq::Army, a.id);
    bool ch = fixEnum(a.kind, 2, ArmyKind::Army, F_ARMIES, where, "kind");
    ch |= fixPoint(a.pos, true, F_ARMIES, where, "pos");
    bool fleet = a.isFleet();
    std::vector<ArmyGroup> groups;
    for (size_t gi = 0; gi < a.groups.size(); gi++) {
      ArmyGroup g = a.groups[gi];
      std::string at = where + ".groups[" + std::to_string(gi) + "]";
      const Faction* f = base.faction(g.faction);
      if (!f) {
        warn(F_ARMIES, at + ".faction", "нет фракции " + ref(Seq::Faction, g.faction) + " — отряды группы удалены");
        ch = true;
        continue;
      }
      ch |= fixKeyed(
          g.units, F_ARMIES, at, "units", [](const ArmyUnit& u) { return u.row; },
          [&](ArmyUnit& u, const std::string& uat, bool& uch) {
            if (Id row = remapRow(f->id, fleet, u.row); row != u.row) {
              u.row = row;
              uch = true;
            }
            if (fleet ? !f->fleetRow(u.row) : !f->armyRow(u.row)) {
              warn(F_ARMIES, uat + ".row", std::string("у ") + refStr(Seq::Faction, f->id) + (fleet ? " нет строки флота " : " нет строки армии ") +
                                               ref(Seq::Row, u.row) + " — отряд удалён");
              return false;
            }
            if (fleet && f->fleetRow(u.row)->type == ShipType::Galleon) {
              warn(F_ARMIES, uat, "торговые галеоны не входят во флоты на карте — вернулись в резерв");
              return false;
            }
            uch |= fixNum(u.count, i64(0), kMaxCount, i64(0), F_ARMIES, uat, "count");
            return true;
          },
          [](const ArmyUnit& u) { return "повтор строки " + refStr(Seq::Row, u.row) + " — численности сложены"; },
          [](ArmyUnit& a, const ArmyUnit& b) { a.count = std::min(kMaxCount, a.count + b.count); });
      // Герой — персонаж, состоящий не больше чем в одном войске.
      std::vector<Id> heroes;
      for (Id h : g.heroes) {
        if (!exists(Seq::Character, h)) {
          warn(F_ARMIES, at + ".heroes", "нет персонажа " + ref(Seq::Character, h) + " — удалён из войска");
          ch = true;
          continue;
        }
        if (usedHeroes.count(h)) {
          warn(F_ARMIES, at + ".heroes", "персонаж " + refStr(Seq::Character, h) + " уже в другом войске, группе или гарнизоне — удалён");
          ch = true;
          continue;
        }
        usedHeroes.insert(h);
        heroes.push_back(h);
      }
      g.heroes = std::move(heroes);
      auto same = std::find_if(groups.begin(), groups.end(), [&](const ArmyGroup& x) { return x.faction == g.faction; });
      if (same != groups.end()) {
        warn(F_ARMIES, at, "вторая группа фракции " + refStr(Seq::Faction, g.faction) + " — объединена с первой");
        for (auto& u : g.units) {
          auto su = std::find_if(same->units.begin(), same->units.end(), [&](const ArmyUnit& x) { return x.row == u.row; });
          if (su != same->units.end()) su->count = std::min(kMaxCount, su->count + u.count);
          else same->units.push_back(u);
        }
        for (Id h : g.heroes) same->heroes.push_back(h);
        ch = true;
        continue;
      }
      groups.push_back(std::move(g));
    }
    if (groups.empty()) {
      warn(F_ARMIES, where, a.groups.empty() ? std::string("в войске нет ни одной группы отрядов — войско удалено")
                                             : std::string("не осталось ни одной допустимой группы — войско удалено"));
      tx.eraseArmy(a.id);
      return;
    }
    a.groups = std::move(groups);
    ch |= fixRef(a.commander, Seq::Character, F_ARMIES, where, "commander");
    ch |= fixNum(a.loyalty, schema::kMinLoyalty, schema::kMaxLoyalty, schema::kMaxLoyalty, F_ARMIES, where, "loyalty");
    ch |= fixRefList(a.modifiers, Seq::Modifier, F_ARMIES, where, "modifiers");
    ch |= fixModTurns(a.modTurns, a.modifiers, F_ARMIES, where);
    if (ch) tx.army(a.id) = std::move(a);
  });
  // Войско на борту флота: флот — существующий флот, у которого это войско в грузе; у флота — одно войско;
  // положение войска — у флота.
  const World cur = w();
  cur.armies.each([&](const Army& a) {
    const std::string where = refStr(Seq::Army, a.id);
    if (a.isFleet()) {
      if (a.carrier) {
        warn(F_ARMIES, where + ".carrier", "флот не перевозится другим флотом — снято");
        tx.army(a.id).carrier = 0;
      }
      const Army* c = cur.army(a.cargo);
      if (a.cargo && (!c || c->isFleet() || c->carrier != a.id)) {
        warn(F_ARMIES, where + ".cargo", "войска на борту нет или оно на другом флоте — груз снят");
        tx.army(a.id).cargo = 0;
      }
    } else {
      if (a.cargo) {
        tx.army(a.id).cargo = 0;
        warn(F_ARMIES, where + ".cargo", "груз бывает только у флота — снят");
      }
      const Army* s = cur.army(a.carrier);
      if (a.carrier && (!s || !s->isFleet() || s->cargo != a.id)) {
        warn(F_ARMIES, where + ".carrier", "флота нет или у него другое войско на борту — войско высажено");
        tx.army(a.id).carrier = 0;
      } else if (s && a.pos != s->pos) {
        tx.army(a.id).pos = s->pos;
      }
    }
  });
}

void Norm::routes() {
  const World base = w();
  base.routes.each([&](const Route& r0) {
    Route r = r0;
    const std::string where = refStr(Seq::Route, r.id);
    bool ch = false;
    if (r.guild && !isGuild(r.guild)) {
      warn(F_ROUTES, where + ".guild", exists(Seq::Faction, r.guild) ? refStr(Seq::Faction, r.guild) + " — не гильдия: владелец удалён"
                                                                    : "нет фракции " + refStr(Seq::Faction, r.guild) + " — владелец удалён");
      r.guild = 0;
      ch = true;
    }
    ch |= fixPoints(r.pts, F_ROUTES, where, "pts");
    if (r.pts.size() < 2) {
      warn(F_ROUTES, where, "у маршрута меньше двух точек — маршрут удалён");
      tx.eraseRoute(r.id);
      return;
    }
    if (ch) tx.route(r.id) = std::move(r);
  });
}

void Norm::deals() {
  const World base = w();
  base.deals.each([&](const Deal& d0) {
    Deal d = d0;
    const std::string where = refStr(Seq::Deal, d.id);
    if (!exists(Seq::Faction, d.a) || !exists(Seq::Faction, d.b) || d.a == d.b) {
      warn(F_DEALS, where, d.a == d.b && d.a ? "стороны сделки совпадают — сделка удалена"
                                             : "нет стороны сделки " + ref(Seq::Faction, exists(Seq::Faction, d.a) ? d.b : d.a) + " — сделка удалена");
      tx.eraseDeal(d.id);
      return;
    }
    bool ch = fixEnum(d.kind, 3, DealKind::Trade, F_DEALS, where, "kind");
    ch |= fixEnum(d.status, 3, DealStatus::Active, F_DEALS, where, "status");
    ch |= fixNum(d.turn, 1, kMaxTurn, 1, F_DEALS, where, "turn");
    std::vector<DealItem> items;
    for (size_t i = 0; i < d.items.size(); i++) {
      DealItem it = d.items[i];
      std::string at = where + ".items[" + std::to_string(i) + "]";
      if (fixEnum(it.kind, int(DealItemKind::Count), DealItemKind::Resource, F_DEALS, at, "kind")) ch = true;
      if (it.kind == DealItemKind::Province || it.kind == DealItemKind::Hero) {
        // Провинция и пленный герой передаются разово; ссылка — на существующую запись.
        const bool prov = it.kind == DealItemKind::Province;
        if (!exists(prov ? Seq::Province : Seq::Character, it.ref)) {
          warn(F_DEALS, at + ".ref", std::string("нет ") + (prov ? "провинции " : "персонажа ") + ref(prov ? Seq::Province : Seq::Character, it.ref) +
                                     " — позиция удалена");
          ch = true;
          continue;
        }
        if (it.mode != DealMode::Once || it.res != kGold) {
          it.mode = DealMode::Once;
          it.res = kGold;
          ch = true;
        }
        items.push_back(it);
        continue;
      }
      if (it.ref) {
        it.ref = 0;
        ch = true;
      }
      if (!hasCat(Seq::Resource, it.res)) {
        warn(F_DEALS, at + ".res", "нет ресурса " + ref(Seq::Resource, it.res) + " — позиция удалена");
        ch = true;
        continue;
      }
      ch |= fixEnum(it.from, 2, DealSide::A, F_DEALS, at, "from");
      ch |= fixEnum(it.mode, 2, DealMode::Once, F_DEALS, at, "mode");
      ch |= fixNum(it.amount, 0.0, 1e12, 0.0, F_DEALS, at, "amount");
      ch |= fixNum(it.turns, 1, kMaxTurn, 1, F_DEALS, at, "turns");
      ch |= fixNum(it.left, 0, it.turns, 0, F_DEALS, at, "left");
      items.push_back(it);
    }
    if (ch) {
      d.items = std::move(items);
      tx.deal(d.id) = std::move(d);
    }
  });
}

// Хроника хранит историю: ссылки на удалённые провинции, войска и фракции допустимы и не проверяются.
void Norm::log() {
  const World base = w();
  base.log.each([&](const LogEntry& l0) {
    const std::string where = refStr(Seq::Log, l0.id);
    LogEntry l = l0;
    bool ch = fixNum(l.turn, 1, kMaxTurn, 1, F_LOG, where, "turn");
    ch |= fixEnum(l.kind, int(LogKind::Count), LogKind::Note, F_LOG, where, "kind");
    ch |= fixRefList(l.factions, Seq::Faction, F_LOG, where, "factions", [](Id) { return true; });
    if (ch) tx.add(std::move(l));  // запись с тем же ID заменяется
  });
}

void Norm::plots() {
  const World base = w();
  std::unordered_set<Id> provTaken, heroTaken;   // провинции и герои уже в заговоре (первом по ID)
  base.plots.each([&](const Plot& p0) {
    const std::string where = refStr(Seq::Plot, p0.id);
    Plot p = p0;
    bool ch = fixEnum(p.kind, int(PlotKind::Count), PlotKind::External, F_PLOTS, where, "kind");
    if (!isState(p.target)) {
      warn(F_PLOTS, where + ".target", p.target && exists(Seq::Faction, p.target) ? refStr(Seq::Faction, p.target) + " — не государство: заговор удалён"
                                                                                  : "нет государства-цели — заговор удалён");
      tx.erasePlot(p.id);
      return;
    }
    if (p.external()) {
      if (!isState(p.owner) || p.owner == p.target) {
        warn(F_PLOTS, where + ".owner", p.owner == p.target && p.owner ? std::string("заказчик совпадает с целью — заговор удалён")
                                                                       : "нет государства-заказчика — заговор удалён");
        tx.erasePlot(p.id);
        return;
      }
    } else if (p.owner) {
      warn(F_PLOTS, where + ".owner", "у внутреннего заговора нет заказчика — ссылка удалена");
      p.owner = 0;
      ch = true;
    }
    ch |= fixNum(p.created, 1, kMaxTurn, 1, F_PLOTS, where, "created");
    ch |= fixNum(p.stage, 0, schema::kPlotStages, 0, F_PLOTS, where, "stage");
    ch |= fixNum(p.revealTurn, 0, kMaxTurn, 0, F_PLOTS, where, "revealTurn");
    if (!p.found && p.stage) {   // раскрытые этапы — только у обнаруженного заговора
      p.stage = 0;
      ch = true;
    }
    // Провинции: существующие сухопутные, без повторов и не в другом заговоре.
    std::vector<PlotProvince> provs;
    for (size_t i = 0; i < p.provinces.size(); i++) {
      PlotProvince x = p.provinces[i];
      const std::string at = where + ".provinces[" + std::to_string(i) + "]";
      const Province* pr = base.province(x.province);
      if (!pr || pr->sea || provTaken.count(x.province)) {
        warn(F_PLOTS, at, !pr ? "нет провинции " + ref(Seq::Province, x.province) + " — удалена из заговора"
                              : pr->sea ? refStr(Seq::Province, x.province) + " — морская провинция: удалена из заговора"
                                        : refStr(Seq::Province, x.province) + " уже в другом заговоре — удалена");
        ch = true;
        continue;
      }
      provTaken.insert(x.province);
      ch |= fixNum(x.commitment, 0.0, 100.0, 0.0, F_PLOTS, at, "commitment");
      ch |= fixNum(x.sponsored, 0, kMaxTurn, 0, F_PLOTS, at, "sponsored");
      ch |= fixAmounts(x.supplies, Seq::Resource, resourceIds(), F_PLOTS, at, "supplies");
      provs.push_back(std::move(x));
    }
    if (provs != p.provinces) p.provinces = std::move(provs);
    // Герои: участники, приглашённые, угрозы — существующие персонажи, каждый в одном списке и одном заговоре.
    auto fixHeroes = [&](std::vector<Id>& list, const char* field) {
      std::vector<Id> keep;
      for (Id h : list) {
        if (!h) continue;
        if (!base.character(h)) {
          warn(F_PLOTS, where + "." + field, "нет персонажа " + refStr(Seq::Character, h) + " — удалён");
          ch = true;
          continue;
        }
        if (heroTaken.count(h)) {
          warn(F_PLOTS, where + "." + field, refStr(Seq::Character, h) + " уже в заговоре — удалён");
          ch = true;
          continue;
        }
        heroTaken.insert(h);
        keep.push_back(h);
      }
      if (keep.size() != list.size()) list = std::move(keep);
    };
    fixHeroes(p.heroes, "heroes");
    fixHeroes(p.invited, "invited");
    fixHeroes(p.threats, "threats");
    if (p.leader && std::find(p.heroes.begin(), p.heroes.end(), p.leader) == p.heroes.end()) {
      warn(F_PLOTS, where + ".leader", refStr(Seq::Character, p.leader) + " — не участник заговора: лидер снят");
      p.leader = 0;
      ch = true;
    }
    // Заказные убийства — только угроз заговору, по одному на героя.
    std::vector<PlotHit> hits;
    for (size_t i = 0; i < p.hits.size(); i++) {
      PlotHit h = p.hits[i];
      const std::string at = where + ".hits[" + std::to_string(i) + "]";
      if (std::find(p.threats.begin(), p.threats.end(), h.character) == p.threats.end() ||
          std::any_of(hits.begin(), hits.end(), [&](const PlotHit& y) { return y.character == h.character; })) {
        warn(F_PLOTS, at, ref(Seq::Character, h.character) + " — не угроза заговору или повтор: заказ удалён");
        ch = true;
        continue;
      }
      ch |= fixNum(h.gold, 0.0, 1e12, 0.0, F_PLOTS, at, "gold");
      ch |= fixNum(h.turn, 0, kMaxTurn, 0, F_PLOTS, at, "turn");
      hits.push_back(h);
    }
    if (hits.size() != p.hits.size()) p.hits = std::move(hits);
    if (ch) tx.plot(p.id) = std::move(p);
  });
}

void Norm::relations() {
  const RelMap& rel = *w().relations;
  bool bad = false;
  for (auto& [k, r] : rel) {
    Id a = Id(k >> 32), b = Id(k & 0xFFFFFFFFu);
    if (!exists(Seq::Faction, a) || !exists(Seq::Faction, b) || a == b || a > b || !std::isfinite(r.v) || r.v < -100 ||
        r.v > 100 || int(r.s) < 0 || int(r.s) > 3)
      bad = true;
  }
  if (!bad) return;
  RelMap res;
  for (auto& [k, r0] : rel) {
    Id a = Id(k >> 32), b = Id(k & 0xFFFFFFFFu);
    std::string where = refStr(Seq::Faction, std::min(a, b)) + "|" + refStr(Seq::Faction, std::max(a, b));
    if (a == b) {
      warn(F_RELATIONS, where, "отношение фракции к самой себе — удалено");
      continue;
    }
    if (!exists(Seq::Faction, a) || !exists(Seq::Faction, b)) {
      warn(F_RELATIONS, where, "нет фракции " + refStr(Seq::Faction, exists(Seq::Faction, a) ? b : a) + " — отношение удалено");
      continue;
    }
    Relation r = r0;
    fixNum(r.v, -100.0, 100.0, 0.0, F_RELATIONS, where, "v");
    fixEnum(r.s, 4, RelStatus::Unknown, F_RELATIONS, where, "s");
    res[relKey(a, b)] = r;
  }
  tx.relations() = std::move(res);
}

}  // namespace rg::io::norm
