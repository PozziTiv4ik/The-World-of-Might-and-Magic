// Сценарии вкладки «Экономика» государства и гильдии: независимый пересчёт по формулам docs/RULES.md совпадает с
// панелью, и завершение хода прибавляет к казне ровно «Чистый доход»; содержание специалистов; расход провизии
// государства живых (0,001 на жителя и живого воина) и предупреждение «Голод», казна до тысячных, доход торгового флота
// («Доход торгового галеона» за корабль), маршруты гильдии (5 % ценности провинций пути).
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

// Независимый пересчёт экономики фракции по формулам docs/RULES.md §2–3 и ТЗ «Общие доработки» (п.9, 11), «Механика
// войн» (п.1), «Механика мятежа» (п.5), «Виды государств» (п.5, 9), «Доработки №1» (п.19: ценность от населения):
// эффекты берутся из мира, провинции маршрутов — по их линиям.
struct Econ {
  double provTax = 0, guildTax = 0, hqNet = 0, trade = 0, tribute = 0, slaves = 0, tradeFleet = 0, routes = 0;
  double army = 0, fleet = 0, spec = 0, tradeOut = 0, tributeOut = 0, slaveUpkeep = 0;
  double own() const { return provTax + guildTax + hqNet + slaves + tradeFleet + routes; }
  double gross() const { return own() + trade + tribute; }
};

std::map<Id, Econ> recompute(const World& w, const rules::Calc& c) {
  std::map<Id, Econ> e;
  // Маршруты: +10 % базы за маршрут и +2,5 % за каждое государство на его пути; гильдия-владелец — 5 % ценности
  // сухопутных провинций пути. Через оккупированные провинции и провинции государств в войне с гильдией маршрут не
  // работает (аудит 2026-10-23, п. 11).
  auto fs = geo::faces(w);
  std::map<Id, double> routeBonus;
  std::vector<std::pair<Id, std::vector<Id>>> guildRoutes;
  w.routes.each([&](const Route& r) {
    if (r.pts.empty()) return;
    std::vector<Id> open, states;
    for (Id pid : fs->provincesOnPolyline(r.pts)) {
      const Province* pr = w.province(pid);
      if (!pr || pr->sea) continue;
      if (pr->occupied && pr->occupier && pr->occupier != pr->owner && w.faction(pr->occupier)) continue;
      if (r.guild && pr->owner && w.relation(pr->owner, r.guild).s == RelStatus::War) continue;
      open.push_back(pid);
      if (const Faction* o = w.faction(pr->owner); o && o->isState() && !test::has(states, o->id)) states.push_back(o->id);
    }
    for (Id pid : open) routeBonus[pid] += 0.10 + 0.025 * double(states.size());
    if (r.guild) guildRoutes.push_back({r.guild, open});
  });
  std::map<Id, double> value;
  w.provinces.each([&](const Province& p) {
    if (p.sea) return;
    const Faction* o = w.faction(p.owner);
    if (o && !o->isState()) o = nullptr;
    rules::Effects fx = rules::provinceEffects(w, p.id);
    // Население: +1 к ценности за каждые 100 жителей.
    i64 people = 0;
    for (const RacePop& r : p.races) people += std::max<i64>(0, r.pop);
    double V = std::max(0.0, std::max(0.0, p.baseTrade) * (1 + fx[Fx::TradePct] / 100 + routeBonus[p.id]) + fx[Fx::TradeFlat] +
                                 double(people) * schema::kTradePerPerson);
    value[p.id] = V;
    double t = std::clamp((o ? std::max(0.0, o->tax) : 0.0) + p.localTax, 1.0, 100.0);
    Id rec = o ? o->id : 0;
    if (p.occupied && p.occupier && p.occupier != p.owner && w.faction(p.occupier)) {
      if (w.settings->occupiedIncome == OccupiedIncome::Occupier) rec = p.occupier;
      else if (w.settings->occupiedIncome == OccupiedIncome::None) rec = 0;
    }
    // Пустошь нежити (осквернённая): доход с ценности — только государству нежити (демонов).
    const bool waste = rules::provinceHas(w, p.id, schema::mod::UndeadWaste), desecr = rules::provinceHas(w, p.id, schema::mod::Desecrated);
    bool blocked = false;
    if (waste || desecr) {
      const Faction* rf = w.faction(rec);
      blocked = !rf || !rf->isState() || rf->stateKind != (waste ? StateKind::Undead : StateKind::Demonic);
    }
    double grossHq = 0, gtax = 0;
    for (const Influence& in : p.influence) {
      const Faction* g = w.faction(in.guild);
      if (!g || !g->isGuild() || !test::has(p.hqs, in.guild) || !(in.pct > 0)) continue;
      double gross = V * in.pct / 100;
      grossHq += gross;
      gtax += gross * t / 100;
      if (!waste && !desecr) e[in.guild].hqNet += gross - gross * t / 100;   // пустошь: гильдии дохода с ценности нет
    }
    if (rec) {
      if (!blocked) {
        e[rec].provTax += std::max(0.0, V - grossHq) * t / 100;
        e[rec].guildTax += gtax;
      }
      if (p.resource == kGold)
        e[rec].provTax += std::max(0.0, std::max(0.0, p.resourceAmount) * (1 + fx[Fx::ResourcePct] / 100) + fx[Fx::ResourceFlat]);
    }
    // Рабы на работах и производство построек — тому же получателю, что и налог (оккупация).
    if (o && rec) {
      for (const SlaveWork& s : p.slaves) e[rec].slaves += double(std::max<i64>(0, s.count)) * 0.002;
      for (const ProvBuilding& pb : p.buildings) {
        const Building* b = w.building(pb.building);
        int lvl = pb.builtLevel();
        if (!b || lvl < 1 || lvl > int(b->levels.size())) continue;
        auto it = b->levels[size_t(lvl - 1)].produce.find(kGold);
        if (it != b->levels[size_t(lvl - 1)].produce.end() && it->second > 0) e[rec].provTax += it->second;
      }
    }
  });
  for (auto& [guild, provs] : guildRoutes) {
    const Faction* g = w.faction(guild);
    if (!g || !g->isGuild()) continue;
    for (Id pid : provs)
      if (!rules::provinceHas(w, pid, schema::mod::UndeadWaste) && !rules::provinceHas(w, pid, schema::mod::Desecrated))
        e[guild].routes += value[pid] * 0.05;
  }
  // Снижение содержания армии: Σ upkeepCut действующих уровней достроенных построек государства, не больше 50 %.
  std::map<Id, double> cut;
  w.provinces.each([&](const Province& p) {
    if (!p.owner) return;
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      const int lvl = pb.builtLevel();
      if (b && b->upkeepCut && lvl >= 1 && lvl <= int(b->levels.size())) cut[p.owner] += b->levels[size_t(lvl - 1)].upkeepCut;
    }
  });
  w.factions.each([&](const Faction& f) {
    rules::Effects fx = rules::factionEffects(w, f.id);
    const double fleetK = std::max(0.0, 1 + fx[Fx::FleetUpkeepPct] / 100);
    const double cutK = 1 - std::min(cut[f.id], schema::kMaxUpkeepCut) / 100;
    for (const ArmyRow& r : f.army)
      e[f.id].army += double(r.total) * r.upkeep * std::max(0.0, 1 + fx[Fx::ArmyUpkeepPct] / 100) * cutK;
    for (const FleetRow& r : f.fleet) e[f.id].fleet += double(r.total) * r.upkeep * fleetK;
    // Торговые галеоны: «Доход торгового галеона» за корабль (от содержания не зависит).
    for (const GarrisonEntry& g : f.tradeFleet)
      if (const FleetRow* r = f.fleetRow(g.row); r && r->type == ShipType::Galleon)
        e[f.id].tradeFleet += double(g.count) * rules::constantOf(w, schema::cst::GalleonIncome).num;
    for (const SlaveGroup& s : f.slaves) e[f.id].slaveUpkeep += double(std::max<i64>(0, s.count)) * 0.001;
  });
  // Специалисты: различные персонажи с ролью — правитель, места совета (любой фракции), герои фракции.
  w.factions.each([&](const Faction& f) {
    std::vector<Id> paid;
    auto add = [&](Id ch) {
      if (ch && w.character(ch) && !test::has(paid, ch)) paid.push_back(ch);
    };
    add(f.ruler);
    for (const CouncilSeat& s : f.council) add(s.character);
    w.characters.each([&](const Character& ch) {
      if (ch.hero && ch.faction == f.id) add(ch.id);
    });
    for (Id ch : paid) e[f.id].spec += std::max(0.0, w.character(ch)->upkeep);
  });
  w.deals.each([&](const Deal& d) {
    if (d.status != DealStatus::Active) return;
    for (const DealItem& it : d.items) {
      if (it.mode != DealMode::PerTurn || it.left <= 0 || it.res != kGold) continue;
      Id payer = it.from == DealSide::A ? d.a : d.b, payee = it.from == DealSide::A ? d.b : d.a;
      if (d.kind == DealKind::Trade) {
        e[payee].trade += it.amount;
        e[payer].tradeOut += it.amount;
      } else {
        e[payee].tribute += it.amount;
        e[payer].tributeOut += it.amount;
      }
    }
  });
  return e;
}

}  // namespace

TEST(app_economy_faction_lines) {
  Harness h("economy_faction", 1440, 1200);
  h.demo();
  const Id st = biggestState(h->world());
  CHECK(st != 0);
  CHECK(h->world().faction(st)->stateKind == StateKind::Living);
  // Голод: недостача провизии (ресурсов группы «Провизия» не хватило на расход населения).
  const std::vector<Id> provs = rules::provisionResources(h->world());
  CHECK(!provs.empty());
  CHECK(h->act("Провизия", [&](Tx& tx) {
    for (Id r : provs) tx.faction(st).res.erase(r);
    tx.faction(st).provisionDebt = 5;
  }));
  auto c = rules::calc(h->world());
  const rules::FactionCalc* fc = c->faction(st);
  CHECK(fc->famine);
  // Едят жители и живые воины армии.
  CHECK_NEAR(fc->provisionNeed, double(fc->population + fc->provisionSoldiers) * schema::kProvisionsPerPerson, 1e-9);
  // Запасов нет: на расход идёт только добыча провизии этого хода (провинции с «Зерном»); прошлая недостача не копится.
  double produced = 0;
  for (Id r : provs)
    if (auto it = fc->resources.find(r); it != fc->resources.end()) produced += std::max(0.0, it->second.production);
  CHECK_NEAR(fc->provisionDebtNext, std::max(0.0, fc->provisionNeed - produced), 1e-9);
  openTab(h, st, "faction.economy");
  CHECK(h->uiRect("economy.famine") != nullptr);
  shotClean(h, "economy_faction_famine");
  // Казна — до тысячных.
  CHECK(typeNumber(h, "economy.stock.1", "1234,567"));
  CHECK_NEAR(h->world().faction(st)->treasury(), 1234.567, 1e-9);
  // Недостача покрыта — голода нет.
  CHECK(h->act("Провизия", [&](Tx& tx) { tx.faction(st).provisionDebt = 0; }));
  CHECK(!rules::calc(h->world())->faction(st)->famine);
  h.settle();
  CHECK(h->uiRect("economy.famine") == nullptr);

  // Торговый флот: 20 торговых галеонов из резерва — по «Доходу торгового галеона» (0,5) за корабль.
  Id row = 0;
  CHECK(h->act("Галеоны", [&](Tx& tx) {
    row = rules::addFleetRow(tx, st, ShipType::Galleon, "Торговые галеоны", 20, 3.5);
    rules::setTradeFleet(tx, st, row, 20);
  }));
  c = rules::calc(h->world());
  fc = c->faction(st);
  CHECK_NEAR(fc->incTradeFleet, 20 * 0.5, 1e-9);
  CHECK_NEAR(fc->pirateRisk, 2, 1e-9);   // 20 галеонов без охраны — 2 %
  openTab(h, st, "faction.economy");
  shotClean(h, "economy_faction_incomes");
}

// ---------------------------------------------------------------- 1.e.i: формулы экономики, казна после хода
// Пересчёт по RULES.md совпадает с расчётом панели «Экономика»; завершение хода прибавляет к казне ровно
// «Чистый доход». Отдельно фиксируется баланс демонстрационного мира (все государства — в глубоком минусе).
TEST(app_economy_faction_formulas) {
  Harness h("audit_tzstates_econ");
  h.demo();
  h.waitMap();
  World w = h->store.world();
  auto c = rules::calc(w);
  auto e = recompute(w, *c);
  int states = 0, negative = 0;
  w.factions.each([&](const Faction& f) {
    const rules::FactionCalc* fc = c->faction(f.id);
    CHECK(fc != nullptr);
    if (!fc) return;
    const Econ& x = e[f.id];
    CHECK_NEAR(fc->incProvinces, x.provTax, 1e-6);
    CHECK_NEAR(fc->incGuildTax, x.guildTax, 1e-6);
    CHECK_NEAR(fc->incGuilds, x.hqNet, 1e-6);
    CHECK_NEAR(fc->incTrade, x.trade, 1e-6);
    CHECK_NEAR(fc->incTribute, x.tribute, 1e-6);
    CHECK_NEAR(fc->incSlaves, x.slaves, 1e-6);
    CHECK_NEAR(fc->incTradeFleet, x.tradeFleet, 1e-6);
    CHECK_NEAR(fc->incRoutes, x.routes, 1e-6);
    CHECK_NEAR(fc->expArmy, x.army, 1e-6);
    CHECK_NEAR(fc->expFleet, x.fleet, 1e-6);
    CHECK_NEAR(fc->expSpecialists, x.spec, 1e-6);
    CHECK_NEAR(fc->expSlaves, x.slaveUpkeep, 1e-6);
    rules::Effects ffx = rules::factionEffects(w, f.id);
    double inc = x.own() * std::max(0.0, 1 + ffx[Fx::IncomePct] / 100) + x.trade + x.tribute;  // переводы — без модификатора
    double exp = x.army + x.fleet + x.spec + x.tradeOut + x.tributeOut + x.slaveUpkeep;
    CHECK_NEAR(fc->incTotal, inc, 1e-6);
    CHECK_NEAR(fc->expTotal, exp, 1e-6);
    if (f.isState()) {
      states++;
      if (fc->net < 0) negative++;
      std::fprintf(stderr, "AUDIT tz-states demo: %-32s казна %9.1f доход %8.1f расход %9.1f чистый %+10.1f (войска %.0f, флот %.0f, спец. %.0f)\n",
                   f.name.c_str(), f.treasury(), fc->incTotal, fc->expTotal, fc->net, fc->expArmy, fc->expFleet, fc->expSpecialists);
    }
  });
  CHECK_MSG(negative < states, "демо-мир: у всех " + std::to_string(states) + " государств чистый доход отрицательный — доход ~десятки, расходы ~десятки тысяч");

  // Завершение хода: казна каждой фракции меняется ровно на «Чистый доход», запасы — на «Итого за ход».
  std::map<Id, double> tre0;
  std::map<Id, std::map<Id, double>> res0;
  w.factions.each([&](const Faction& f) {
    tre0[f.id] = f.treasury();
    res0[f.id] = f.res;
  });
  CHECK(h->endTurnNow());
  h.settle();
  const World& w1 = h->store.world();
  w.factions.each([&](const Faction& f) {
    const rules::FactionCalc* fc = c->faction(f.id);
    const Faction* f1 = w1.faction(f.id);
    if (!fc || !f1) return;
    CHECK_NEAR(f1->treasury() - tre0[f.id], fc->net, 1e-6);
    for (auto& [r, flow] : fc->resources) {
      if (r == kGold) continue;
      double was = res0[f.id].count(r) ? res0[f.id][r] : 0.0;
      double expect = std::max(0.0, was + flow.net);
      AUDIT_EXPECT(std::fabs(f1->stock(r) - expect) < 1e-6,
            f.name + ": запас ресурса " + std::to_string(r) + " после хода " + std::to_string(f1->stock(r)) + ", ожидалось " + std::to_string(expect));
    }
  });
}

// ---------------------------------------------------------------- 1.b.iv / 1.e.i: совет и герои — расход «специалисты»
// Вкладки «Совет» и «Герои» пишут «Содержание за ход … входит в расход «специалисты»». Но расход считается по всем
// персонажам фракции: убранный из совета или из героев продолжает получать содержание. Советника другой фракции
// назначить нельзя (ТЗ «Фиксы», п.9).
TEST(app_economy_faction_specialists_upkeep) {
  Harness h("audit_tzstates_specialists");
  h.demo();
  h.waitMap();
  Id alm = findFaction(h->world(), "Королевство Альмарин");
  CHECK(alm);
  auto specOf = [&] { return rules::calc(h->store.world())->faction(alm)->expSpecialists; };
  auto councilSum = [&] {
    double s = 0;
    for (const CouncilSeat& seat : h->world().faction(alm)->council)
      if (const Character* ch = h->world().character(seat.character)) s += std::max(0.0, ch->upkeep);
    return s;
  };

  // 1. Убрать место совета (корзина в строке).
  openTab(h, alm, "faction.council");
  const Faction* f = h->world().faction(alm);
  CHECK(!f->council.empty());
  const Id who = f->council[0].character;
  const double up = h->world().character(who)->upkeep;
  CHECK(up > 0);
  const double spec0 = specOf(), council0 = councilSum();
  CHECK(clickIn(h, "council.remove.0"));
  h.step();
  CHECK_EQ(h->world().faction(alm)->council.size(), f->council.size() - 1);
  CHECK_NEAR(councilSum(), council0 - up, 1e-9);   // вкладка «Совет»: «Содержание за ход» уменьшилось
  CHECK_MSG(std::fabs(specOf() - (spec0 - up)) < 1e-6,
        "место совета убрано, «Содержание за ход» совета −" + std::to_string(up) + ", а расход «специалисты» не изменился: " + std::to_string(specOf()));
  shotClean(h, "audit_tzstates_council_removed");
  h->undo();
  h.step();

  // 2. Советник другого государства (ТЗ «Фиксы», п.9): в выборе его нет, правило отказывает — сумма совета и расход
  // «специалисты» Альмарина не меняются.
  Id foreign = test::idByName(h->world().characters, "Бьорн Медведь");
  CHECK(foreign);
  CHECK(h->world().character(foreign)->faction != alm);
  const double spec1 = specOf(), council1 = councilSum();
  const Id seat0 = h->world().faction(alm)->council[0].id, who0 = h->world().faction(alm)->council[0].character;
  CHECK(!h->act("Советник", [&](Tx& tx) { rules::setCouncilMember(tx, alm, seat0, foreign); }));
  h.dropToasts();
  openTab(h, alm, "faction.council");
  CHECK(clickIn(h, "council.who.0"));
  h.type("Бьорн");
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(h->world().faction(alm)->council[0].character, who0);
  CHECK_NEAR(councilSum(), council1, 1e-9);
  CHECK_NEAR(specOf(), spec1, 1e-6);
  shotClean(h, "audit_tzstates_council_foreign");

  // 3. «Убрать из героев»: вкладка «Герои» уменьшает «Содержание за ход», расход «специалисты» — нет.
  openTab(h, alm, "faction.heroes");
  std::vector<const Character*> heroes;
  h->world().characters.each([&](const Character& ch) {
    if (ch.faction == alm && ch.hero) heroes.push_back(&ch);
  });
  std::sort(heroes.begin(), heroes.end(), [](const Character* x, const Character* y) { return compareRu(x->name, y->name) < 0; });
  int k = -1;
  for (size_t i = 0; i < heroes.size(); i++)
    if (heroes[i]->id != h->world().faction(alm)->ruler && heroes[i]->upkeep > 0) {
      k = int(i);
      break;
    }
  CHECK(k >= 0);
  const Id hid = heroes[size_t(k)]->id;
  const double hup = heroes[size_t(k)]->upkeep;
  const double spec2 = specOf();
  CHECK(clickIn(h, "heroes.remove." + std::to_string(k)));
  h.step();
  CHECK(!h->world().character(hid)->hero);
  CHECK_MSG(std::fabs(specOf() - (spec2 - hup)) < 1e-6,
        "герой убран из списка, содержание героев −" + std::to_string(hup) + ", расход «специалисты» не изменился: " + std::to_string(specOf()));
}

TEST(app_economy_guild_routes) {
  Harness h("economy_guild_routes", 1440, 1100);
  h.demo();
  // Гильдия — владелец маршрутов: 5 % текущей ценности сухопутных провинций пути.
  Id guild = 0;
  h->world().routes.each([&](const Route& r) {
    if (!guild && r.guild) guild = r.guild;
  });
  CHECK(guild != 0);
  auto c = rules::calc(h->world());
  const rules::FactionCalc* fc = c->faction(guild);
  CHECK(fc->incRoutes > 0);
  auto fs = geo::faces(h->world());
  double expect = 0;
  h->world().routes.each([&](const Route& r) {
    if (r.guild != guild) return;
    for (Id pid : fs->provincesOnPolyline(r.pts))
      if (const rules::ProvinceCalc* pc = c->province(pid); pc && !pc->sea) expect += pc->tradeValue * 0.05;
  });
  CHECK_NEAR(fc->incRoutes, expect, 1e-9);
  openTab(h, guild, "faction.economy");
  shotClean(h, "economy_guild_routes");
}

// Снижение содержания армии постройками (казармы, ТЗ «Доработки №2–4»): расходы на войска меньше на % уровня — строка
// расходов «Снижение содержания» с подсказкой; сумма по государству — не больше 50 %.
TEST(app_economy_faction_upkeep_cut) {
  Harness h("economy_upkeep_cut", 1440, 1200);
  h.demo();
  h.dropToasts();
  const Id st = biggestState(h->world());
  const Id barracks = test::buildingByKey(h->world(), schema::bld::Barracks);
  CHECK(st && barracks);
  const double army0 = rules::calc(h->world())->faction(st)->expArmy;
  const double cut0 = rules::calc(h->world())->faction(st)->upkeepCut;   // казармы демо-мира в столицах
  CHECK(army0 > 0);
  // Казармы в провинции государства без казарм (без других построек — есть свободный слот).
  auto noBarracks = [&](const Province& p) {
    for (const ProvBuilding& b : p.buildings)
      if (b.building == barracks) return false;
    return true;
  };
  Id pid = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid && !p.sea && p.owner == st && noBarracks(p)) pid = p.id;
  });
  CHECK(pid != 0);
  CHECK(h->act("Казармы", [&](Tx& tx) {
    tx.province(pid).buildings.clear();
    rules::placeBuilding(tx, pid, barracks, 1);
  }));
  const rules::FactionCalc* fc = rules::calc(h->world())->faction(st);
  CHECK(fc != nullptr);
  CHECK_NEAR(fc->upkeepCut, cut0 + h->world().building(barracks)->levels[0].upkeepCut, 1e-9);
  CHECK(fc->expArmy < army0);
  openTab(h, st, "faction.economy");
  CHECK(ensureVisible(h, "economy.upkeepCut"));
  shotClean(h, "economy_upkeep_cut");
  // Больше 50 % не бывает: уровень казарм — 50 %, вторые казармы ничего не прибавляют.
  CHECK(h->act("Казармы 50 %", [&](Tx& tx) { rules::setLevelUpkeepCut(tx, barracks, 1, 50); }));
  Id pid2 = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid2 && !p.sea && p.owner == st && p.id != pid && noBarracks(p)) pid2 = p.id;
  });
  CHECK(pid2 != 0);
  CHECK(h->act("Вторые казармы", [&](Tx& tx) {
    tx.province(pid2).buildings.clear();
    rules::placeBuilding(tx, pid2, barracks, 1);
  }));
  CHECK_NEAR(rules::calc(h->world())->faction(st)->upkeepCut, schema::kMaxUpkeepCut, 1e-9);
}
