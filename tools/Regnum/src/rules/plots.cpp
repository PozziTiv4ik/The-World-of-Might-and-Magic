// Regnum — заговоры (ТЗ «Доработки №2»): состав заговора, приверженность и снабжение провинций, приглашения героев,
// заказные убийства, контрразведка цели (обнаружение, раскрытие этапов, аресты, тюрьмы), осуществление заговора.
// Действия заговора тайные: хроника пишет только то, что видно всем, — аресты, покушения и восстание; заказчик в
// хронике не называется.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

const Plot& needPlot(const World& w, Id plot) {
  const Plot* p = w.plot(plot);
  if (!p) fail(plot ? "Заговор не найден" : "Не выбран заговор");
  return *p;
}

PlotProvince* plotProvince(Plot& p, Id province) {
  for (PlotProvince& x : p.provinces)
    if (x.province == province) return &x;
  return nullptr;
}

bool inList(const std::vector<Id>& v, Id x) { return std::find(v.begin(), v.end(), x) != v.end(); }

bool alive(const World& w, Id character) {
  const Character* c = w.character(character);
  return c && !hasModKey(w, c->modifiers, schema::mod::Dead);
}

// Платит заказчик внешнего заговора (внутренний — без оплаты); нехватка — отказ.
void pay(Tx& tx, const Plot& p, double gold, const char* what) {
  if (!p.external() || !(gold > 0)) return;
  const Faction& f = needState(tx.w(), p.owner);
  if (f.treasury() + 1e-9 < gold) fail(std::string("Недостаточно золота на ") + what + ": нужно " + fmtGold(gold) + ", в казне " + fmtGold(f.treasury()));
  addStock(tx.faction(p.owner), kGold, -gold);
}

void refund(Tx& tx, const Plot& p, double gold) {
  if (p.external() && gold > 0 && tx.w().faction(p.owner)) addStock(tx.faction(p.owner), kGold, gold);
}

// Уровень достроенной уникальной постройки государства с ключом (наибольший по провинциям государства).
int stateBuildingLevel(const World& w, Id state, std::string_view key) {
  int best = 0;
  w.provinces.each([&](const Province& p) {
    if (p.owner != state || p.sea) return;
    for (const ProvBuilding& pb : p.buildings) {
      const Building* b = w.building(pb.building);
      if (b && b->key == key && (b->owner == 0 || b->owner == state)) best = std::max(best, pb.builtLevel());
    }
  });
  return best;
}

// Бросок события заговора: зерно — счётчик случайных событий мира и участники (каждое нажатие — новый бросок). Рост
// счётчика — отметка «действие со случайным исходом»: игроку его не отменить (app/app.cpp).
Rng plotRng(Tx& tx, Id a, Id b) {
  const u32 n = ++tx.meta().rng;
  return Rng(hashMix(hashMix(hashMix(0x504C4F5453434F4Eull, u64(n)), u64(a)), u64(b)));
}

// Взять n жителей провинции пропорционально расам; возвращает взятых по расам.
std::map<Id, i64> takeFromProvince(Tx& tx, Id province, i64 n) {
  std::map<Id, i64> out;
  const Province* p = tx.w().province(province);
  if (!p || n <= 0) return out;
  std::vector<i64> weights;
  for (const RacePop& r : p->races) weights.push_back(std::max<i64>(0, r.pop));
  const std::vector<i64> part = splitProportional(n, weights);
  Province& m = tx.province(province);
  for (size_t i = 0; i < m.races.size() && i < part.size(); i++) {
    const i64 t = std::min(part[i], std::max<i64>(0, m.races[i].pop));
    if (t <= 0) continue;
    m.races[i].pop -= t;
    out[m.races[i].race] += t;
  }
  return out;
}

void toPrison(Tx& tx, Id state, const std::map<Id, i64>& people) {
  if (people.empty()) return;
  std::vector<RacePop>& pr = tx.faction(state).prison;
  for (auto& [race, n] : people) {
    if (n <= 0) continue;
    auto it = std::find_if(pr.begin(), pr.end(), [&](const RacePop& x) { return x.race == race; });
    if (it != pr.end()) it->pop = std::min(kMaxCount, it->pop + n);
    else pr.push_back(RacePop{race, n});
  }
}

i64 sum(const std::map<Id, i64>& m) {
  i64 n = 0;
  for (auto& [k, v] : m) n += v;
  return n;
}

// Герой под арестом в тюрьме государства state (как «Взят в плен», без пленителя).
void arrestHero(Tx& tx, Id state, Id character) {
  addModifier(tx, ModTarget::Character, character, ensureBuiltinMod(tx, schema::mod::Arrested), 0);
  tx.character(character).jailer = state;
}

// Ресурс справочника по названию плана снабжения (запчасти механизмов — встроенный ресурс).
Id armsResource(const World& w, const char* name) {
  if (std::string_view(name) == "Запчасти механизмов")
    if (Id r = resourceId(w, schema::kResMechParts)) return r;
  const std::string k = utf8::searchKey(name);
  for (const CatalogItem& c : w.catalogs->resources)
    if (utf8::searchKey(c.name) == k) return c.id;
  return 0;
}

double armsSum(const World& w, const PlotProvince& pp, schema::PlotArms arms) {
  double s = 0;
  for (const schema::PlotArmsInfo& a : schema::plotArms()) {
    if (a.arms != arms) continue;
    for (const char* n : a.resources)
      if (Id r = armsResource(w, n)) {
        auto it = pp.supplies.find(r);
        if (it != pp.supplies.end() && it->second > 0) s += it->second;
      }
  }
  return s;
}

}  // namespace

// ================================================================ состав
Id plotOfProvince(const World& w, Id province) {
  return w.plots.firstId([&](const Plot& p) { return p.province(province) != nullptr; });
}

Id plotOfHero(const World& w, Id character) {
  return w.plots.firstId([&](const Plot& p) { return inList(p.heroes, character) || inList(p.invited, character) || inList(p.threats, character); });
}

std::string plotProvinceProblem(const World& w, Id plot, Id province, bool player) {
  const Plot* pl = w.plot(plot);
  const Province* p = w.province(province);
  if (!pl || !p) return "Заговор или провинция не найдены";
  if (p->sea) return "Морская провинция";
  if (p->owner != pl->target) return provName(w, province) + " — не провинция " + facName(w, pl->target);
  if (Id other = plotOfProvince(w, province)) return other == plot ? "Провинция уже в этом заговоре" : "Провинция уже в другом заговоре";
  if (hasModKey(w, p->modifiers, schema::mod::PlotExposed)) return "«Раскрытый заговор»: провинцию пока нельзя взять в заговор";
  if (player && !(p->contentment < 0)) return "Довольство населения не ниже 0";
  return {};
}

std::string plotHeroProblem(const World& w, Id plot, Id character, bool player) {
  const Plot* pl = w.plot(plot);
  const Character* c = w.character(character);
  if (!pl || !c) return "Заговор или герой не найдены";
  if (c->faction != pl->target) return q(w.characterName(character)) + " — не герой " + facName(w, pl->target);
  if (characterHas(w, character, schema::mod::Dead)) return q(w.characterName(character)) + " мёртв";
  if (characterHas(w, character, schema::mod::Captive)) return q(w.characterName(character)) + " в плену";
  if (characterHas(w, character, schema::mod::Arrested)) return q(w.characterName(character)) + " под арестом";
  if (Id other = plotOfHero(w, character)) return other == plot ? "Герой уже в этом заговоре" : "Герой уже в другом заговоре";
  if (player && !characterHas(w, character, schema::mod::Discontent)) return "Нет «Недовольства правителем»";
  return {};
}

Id createPlot(Tx& tx, PlotKind kind, Id owner, Id target, const std::string& name) {
  needState(tx.w(), target);
  if (kind == PlotKind::External) {
    needState(tx.w(), owner);
    if (owner == target) fail("Заговор против другого государства — заказчик и цель должны различаться");
  } else if (kind == PlotKind::Internal) {
    owner = 0;
  } else {
    fail("Неизвестный вид заговора");
  }
  Plot p;
  p.kind = kind;
  p.owner = owner;
  p.target = target;
  p.name = trim(name);
  p.created = tx.w().turn();
  return tx.add(std::move(p)).id;
}

void removePlot(Tx& tx, Id plot) {
  const Plot& p = needPlot(tx.w(), plot);
  // Неисполненные заказы возвращаются заказчику.
  double back = 0;
  for (const PlotHit& h : p.hits) back += h.gold;
  refund(tx, p, back);
  tx.erasePlot(plot);
}

void renamePlot(Tx& tx, Id plot, const std::string& name) {
  needPlot(tx.w(), plot);
  const std::string n = trim(name);
  if (tx.w().plot(plot)->name != n) tx.plot(plot).name = n;
}

std::string plotTitle(const World& w, const Plot& p) {
  if (!trim(p.name).empty()) return p.name;
  return std::string(p.external() ? "Заговор против " : "Внутренний заговор: ") + nameOr(w.factionName(p.target));
}

void addPlotProvince(Tx& tx, Id plot, Id province, bool player) {
  needPlot(tx.w(), plot);
  needProvince(tx.w(), province);
  const std::string why = plotProvinceProblem(tx.w(), plot, province, player);
  if (!why.empty()) fail(why);
  PlotProvince pp;
  pp.province = province;
  tx.plot(plot).provinces.push_back(std::move(pp));
}

void removePlotProvince(Tx& tx, Id plot, Id province) {
  const Plot& p = needPlot(tx.w(), plot);
  if (!p.province(province)) fail("Провинции нет в заговоре");
  auto& v = tx.plot(plot).provinces;
  v.erase(std::remove_if(v.begin(), v.end(), [&](const PlotProvince& x) { return x.province == province; }), v.end());
}

void setPlotCommitment(Tx& tx, Id plot, Id province, double pct) {
  const Plot& p = needPlot(tx.w(), plot);
  if (!p.province(province)) fail("Провинции нет в заговоре");
  needFinite(pct, "Приверженность заговору");
  const double v = clamp(pct, 0.0, 100.0);
  if (PlotProvince* pp = plotProvince(tx.plot(plot), province); pp && pp->commitment != v) pp->commitment = v;
}

double sponsorCost(const World& w, Id province) {
  const rules::ProvinceCalc* pc = calc(w)->province(province);
  return pc ? std::max(0.0, pc->tradeValue) * schema::kPlotSponsorShare : 0.0;
}

std::string sponsorProblem(const World& w, Id plot, Id province) {
  const Plot* p = w.plot(plot);
  if (!p) return "Заговор не найден";
  const PlotProvince* pp = p->province(province);
  if (!pp) return "Провинции нет в заговоре";
  if (pp->commitment >= 100 - 1e-9) return "Приверженность заговору — уже 100 %";
  if (pp->sponsored == w.turn()) return "В этом ходу провинцию уже спонсировали";
  if (provinceHas(w, province, schema::mod::PlotArrests)) return "«Аресты заговорщиков»: приверженность пока не растёт";
  if (p->external()) {
    const Faction* f = w.faction(p->owner);
    const double cost = sponsorCost(w, province);
    if (f && f->treasury() + 1e-9 < cost) return "Недостаточно золота: нужно " + fmtGold(cost) + ", в казне " + fmtGold(f->treasury());
  }
  return {};
}

void sponsorPlot(Tx& tx, Id plot, Id province) {
  const Plot p = needPlot(tx.w(), plot);
  const std::string why = sponsorProblem(tx.w(), plot, province);
  if (!why.empty()) fail(why);
  pay(tx, p, sponsorCost(tx.w(), province), "спонсирование заговора");
  PlotProvince* pp = plotProvince(tx.plot(plot), province);
  pp->commitment = std::min(100.0, pp->commitment + schema::kPlotSponsorStep);
  pp->sponsored = tx.w().turn();
}

std::vector<Id> plotSupplyResources(const World& w) {
  std::vector<Id> out;
  for (const schema::PlotArmsInfo& a : schema::plotArms())
    for (const char* n : a.resources)
      if (Id r = armsResource(w, n); r && !contains(out, r)) out.push_back(r);
  return out;
}

void supplyPlot(Tx& tx, Id plot, Id province, const std::map<Id, double>& add) {
  const Plot p = needPlot(tx.w(), plot);
  if (!p.province(province)) fail("Провинции нет в заговоре");
  const std::vector<Id> allowed = plotSupplyResources(tx.w());
  bool any = false;
  for (auto& [r, v] : add) {
    if (!(v > 0)) continue;
    needFinite(v, "Снабжение");
    if (!contains(allowed, r)) fail("Ресурс «" + resName(tx.w(), r) + "» не подходит для вооружения заговорщиков");
    any = true;
    if (p.external()) {
      const Faction& f = needState(tx.w(), p.owner);
      if (f.stock(r) + 1e-9 < v)
        fail("Недостаточно ресурса «" + resName(tx.w(), r) + "»: нужно " + amount(v) + ", есть " + amount(std::max(0.0, f.stock(r))));
    }
  }
  if (!any) fail("Укажите, сколько ресурсов отдать");
  for (auto& [r, v] : add) {
    if (!(v > 0)) continue;
    if (p.external()) addStock(tx.faction(p.owner), r, -v);
    plotProvince(tx.plot(plot), province)->supplies[r] += v;
  }
}

void addPlotHero(Tx& tx, Id plot, Id character, bool player) {
  needPlot(tx.w(), plot);
  needCharacter(tx.w(), character);
  const std::string why = plotHeroProblem(tx.w(), plot, character, player);
  if (!why.empty()) fail(why);
  if (player) tx.plot(plot).invited.push_back(character);
  else tx.plot(plot).heroes.push_back(character);
}

void removePlotHero(Tx& tx, Id plot, Id character) {
  const Plot& p = needPlot(tx.w(), plot);
  if (!inList(p.heroes, character) && !inList(p.invited, character) && !inList(p.threats, character)) fail("Героя нет в заговоре");
  double back = 0;
  if (const PlotHit* h = p.hit(character)) back = h->gold;
  const Plot copy = p;
  refund(tx, copy, back);
  Plot& m = tx.plot(plot);
  eraseValue(m.heroes, character);
  eraseValue(m.invited, character);
  eraseValue(m.threats, character);
  m.hits.erase(std::remove_if(m.hits.begin(), m.hits.end(), [&](const PlotHit& h) { return h.character == character; }), m.hits.end());
  if (m.leader == character) m.leader = 0;
}

void answerPlotInvite(Tx& tx, Id plot, Id character, bool agree) {
  const Plot& p = needPlot(tx.w(), plot);
  if (!inList(p.invited, character)) fail("Герой не приглашён в этот заговор");
  Plot& m = tx.plot(plot);
  eraseValue(m.invited, character);
  if (agree) m.heroes.push_back(character);
  else m.threats.push_back(character);
}

void setPlotLeader(Tx& tx, Id plot, Id character) {
  const Plot& p = needPlot(tx.w(), plot);
  if (p.leader == character) return;
  if (character && !inList(p.heroes, character)) fail("Лидер заговора — один из его участников");
  tx.plot(plot).leader = character;
}

// ================================================================ заказные убийства
bool hitVip(const World& w, Id character) {
  const Character* c = w.character(character);
  if (!c) return false;
  if (c->hero) return true;
  const Faction* f = w.faction(c->faction);
  if (!f) return false;
  if (f->ruler == character) return true;
  return std::any_of(f->council.begin(), f->council.end(), [&](const CouncilSeat& s) { return s.character == character; });
}

HitRange hitRange(const World& w, Id character) {
  if (hitVip(w, character)) return HitRange{schema::kHitMinVip, schema::kHitMaxVip, schema::kHitPerPctVip};
  return HitRange{schema::kHitMin, schema::kHitMax, schema::kHitPerPct};
}

double heroAssassinPct(const World& w, Id character) {
  const Character* c = w.character(character);
  if (!c) return 0;
  double v = 0;
  for (const HeroMod& hm : heroModifiers(w, character))
    if (const Modifier* x = w.modifier(hm.modifier); x && x->has(Fx::AssassinPct) && std::isfinite(x->get(Fx::AssassinPct)))
      v += x->get(Fx::AssassinPct) * hm.scale;
  return v;
}

HitChance hitChance(const World& w, Id plot, Id character, double gold) {
  HitChance h;
  const Plot* p = w.plot(plot);
  const Character* c = w.character(character);
  if (!p || !c) return h;
  const HitRange r = hitRange(w, character);
  h.base = std::max(0.0, gold) / r.perPct;
  if (p->external()) h.intel = factionEffects(w, p->owner)[Fx::PlotAssassinPct];
  if (c->faction) h.guard = factionEffects(w, c->faction)[Fx::AssassinGuardPct];
  h.hero = heroAssassinPct(w, character);
  if (const PlotHit* x = p->hit(character)) h.guarded = x->guarded;
  h.total = h.guarded ? 0.0 : clamp(h.base + h.intel + h.guard + h.hero, 0.0, 100.0);
  return h;
}

void orderHit(Tx& tx, Id plot, Id character, double gold) {
  const Plot p = needPlot(tx.w(), plot);
  needCharacter(tx.w(), character);
  if (!inList(p.threats, character)) fail("Заказать убийство можно только угрозы заговору");
  if (!alive(tx.w(), character)) fail(q(tx.w().characterName(character)) + " уже мёртв");
  needFinite(gold, "Золото на операцию");
  const HitRange r = hitRange(tx.w(), character);
  if (gold < r.min - 1e-9 || gold > r.max + 1e-9) fail("На операцию — от " + fmtGold(r.min) + " до " + fmtGold(r.max));
  const PlotHit* was = p.hit(character);
  const double before = was ? was->gold : 0.0;
  if (gold > before) pay(tx, p, gold - before, "заказное убийство");
  else refund(tx, p, before - gold);
  Plot& m = tx.plot(plot);
  for (PlotHit& h : m.hits)
    if (h.character == character) {
      h.gold = gold;
      h.turn = tx.w().turn();
      return;
    }
  m.hits.push_back(PlotHit{character, gold, tx.w().turn(), false});
}

void cancelHit(Tx& tx, Id plot, Id character) {
  const Plot p = needPlot(tx.w(), plot);
  const PlotHit* h = p.hit(character);
  if (!h) fail("Заказа на этого героя нет");
  refund(tx, p, h->gold);
  auto& v = tx.plot(plot).hits;
  v.erase(std::remove_if(v.begin(), v.end(), [&](const PlotHit& x) { return x.character == character; }), v.end());
}

void setHitReviewed(Tx& tx, Id plot, Id character, bool on) {
  const Plot& p = needPlot(tx.w(), plot);
  const PlotHit* h = p.hit(character);
  if (!h) fail("Заказа на этого героя нет");
  if (h->reviewed == on) return;
  for (PlotHit& x : tx.plot(plot).hits)
    if (x.character == character) x.reviewed = on;
}

void guardHit(Tx& tx, Id plot, Id character, bool on) {
  const Plot& p = needPlot(tx.w(), plot);
  const PlotHit* h = p.hit(character);
  if (!h) fail("Заказа на этого героя нет");
  if (h->guarded == on) return;
  for (PlotHit& x : tx.plot(plot).hits)
    if (x.character == character) x.guarded = on;
}

std::vector<HitOutcome> resolveHits(Tx& tx) {
  std::vector<HitOutcome> out;
  const int turn = tx.w().turn();
  for (Id pid : idsWhere(tx.w().plots, [](const Plot& p) { return !p.hits.empty(); })) {
    const Plot p = *tx.w().plot(pid);
    std::vector<HitOutcome> done;
    for (const PlotHit& h : p.hits) {
      if (!alive(tx.w(), h.character)) continue;
      HitOutcome o;
      o.plot = pid;
      o.character = h.character;
      o.chance = hitChance(tx.w(), pid, h.character, h.gold).total;
      // Зерно — ход, заговор и герой: повтор хода (отмена и снова «Завершить ход») даёт тот же исход.
      Rng rng(hashMix(hashMix(hashMix(0x4153534153534E31ull, u64(u32(turn))), u64(pid)), u64(h.character)));
      o.roll = rng.uniform() * 100.0;
      o.killed = o.roll < o.chance;
      done.push_back(o);
    }
    // Заказы исполнены (золото потрачено при любом исходе): снимаются до смерти целей, иначе fitPlots вернул бы
    // золото успешного заказа как «неисполненного».
    tx.plot(pid).hits.clear();
    for (const HitOutcome& o : done) {
      const Character* c = tx.w().character(o.character);
      const Id own = c ? c->faction : 0;
      const std::string name = q(tx.w().characterName(o.character));
      if (o.killed) {
        addModifier(tx, ModTarget::Character, o.character, ensureBuiltinMod(tx, schema::mod::Dead), 0);
        addLog(tx, LogKind::War, name + " убит наёмными убийцами", LogRefs{0, 0, own ? std::vector<Id>{own} : std::vector<Id>{}});
      } else {
        addLog(tx, LogKind::War, "Покушение на " + name + " провалилось", LogRefs{0, 0, own ? std::vector<Id>{own} : std::vector<Id>{}});
      }
      out.push_back(o);
    }
  }
  fitPlots(tx);
  return out;
}

// ================================================================ контрразведка
int intelLevel(const World& w, Id state) { return stateBuildingLevel(w, state, schema::bld::IntelHq); }
int counterIntelLevel(const World& w, Id state) { return stateBuildingLevel(w, state, schema::bld::CounterIntelHq); }
double detectChance(const World& w, Id state) { return std::min(100.0, counterIntelLevel(w, state) * schema::kDetectPerLevel); }

std::vector<Id> foundPlots(const World& w, Id state) {
  return idsWhere(w.plots, [&](const Plot& p) { return p.target == state && p.found; });
}

std::string detectProblem(const World& w, Id state, bool player) {
  const Faction* f = w.faction(state);
  if (!f || !f->isState()) return "Государство не найдено";
  if (counterIntelLevel(w, state) < 1) return "Нужен «Штаб Контрразведки» 1 уровня";
  if (player && f->detectTurn == w.turn()) return "Заговоры уже искали в этом ходу";
  return {};
}

Id detectPlots(Tx& tx, Id state, bool player) {
  const std::string why = detectProblem(tx.w(), state, player);
  if (!why.empty()) fail(why);
  tx.faction(state).detectTurn = tx.w().turn();
  Rng rng = plotRng(tx, state, 0);
  if (!(rng.uniform() * 100.0 < detectChance(tx.w(), state))) return 0;
  const std::vector<Id> hidden = idsWhere(tx.w().plots, [&](const Plot& p) { return p.target == state && !p.found; });
  if (hidden.empty()) return 0;
  const Id pid = hidden[size_t(rng.range(0, int(hidden.size()) - 1))];
  Plot& m = tx.plot(pid);
  m.found = true;
  m.stage = 0;
  return pid;
}

std::string revealProblem(const World& w, Id state, Id plot, bool player) {
  const Plot* p = w.plot(plot);
  if (!p || p->target != state || !p->found) return "Заговор не обнаружен";
  if (counterIntelLevel(w, state) < 1) return "Нужен «Штаб Контрразведки» 1 уровня";
  if (p->stage >= schema::kPlotStages) return "Заговор раскрыт полностью";
  if (player && p->revealTurn == w.turn()) return "Этот заговор уже пытались раскрыть в этом ходу";
  return {};
}

bool revealPlot(Tx& tx, Id state, Id plot, bool player) {
  const std::string why = revealProblem(tx.w(), state, plot, player);
  if (!why.empty()) fail(why);
  Rng rng = plotRng(tx, state, plot);
  const bool ok = rng.uniform() * 100.0 < detectChance(tx.w(), state);
  Plot& m = tx.plot(plot);
  m.revealTurn = tx.w().turn();
  if (ok) m.stage++;
  else if (m.stage >= 2) m.alarm = true;   // неудача раскрытия героев и лидера — заговорщики видят расследование
  return ok;
}

ArrestResult arrestPlotters(Tx& tx, Id state, Id plot) {
  const Plot P = needPlot(tx.w(), plot);
  if (P.target != state || !P.found || P.stage < 1) fail("Сначала раскройте провинции заговора");
  ArrestResult res;
  std::map<Id, i64> jailed;
  std::vector<std::string> where;
  const int stage = P.stage;
  if (stage == 1) {
    // Этап 1: −20 % приверженности, 20 % от 10 % населения провинций — в тюрьму, «Аресты заговорщиков».
    const Id mod = ensureBuiltinMod(tx, schema::mod::PlotArrests);
    for (const PlotProvince& pp : P.provinces) {
      const Province* pr = tx.w().province(pp.province);
      if (!pr || pr->owner != state) continue;
      const i64 n = i64(std::floor(double(provincePop(*pr)) * schema::kArrestShare));
      for (auto& [r, k] : takeFromProvince(tx, pp.province, n)) jailed[r] += k;
      if (PlotProvince* x = plotProvince(tx.plot(plot), pp.province)) x->commitment = std::max(0.0, x->commitment - schema::kArrestCommitCut);
      addModifier(tx, ModTarget::Province, pp.province, mod, schema::kPlotModTurns);
      where.push_back(provName(tx.w(), pp.province));
    }
  } else {
    // Этап 2 и дальше: население по приверженности (1 % = 0,2 % населения) — в тюрьму, провинции выходят из заговора
    // (снабжение теряется), «Раскрытый заговор».
    const Id mod = ensureBuiltinMod(tx, schema::mod::PlotExposed);
    for (const PlotProvince& pp : P.provinces) {
      const Province* pr = tx.w().province(pp.province);
      if (!pr) continue;
      if (pr->owner == state) {
        const i64 n = std::min(provincePop(*pr), i64(std::floor(double(provincePop(*pr)) * pp.commitment * schema::kPlotRiseShare)));
        for (auto& [r, k] : takeFromProvince(tx, pp.province, n)) jailed[r] += k;
        addModifier(tx, ModTarget::Province, pp.province, mod, schema::kPlotModTurns);
        where.push_back(provName(tx.w(), pp.province));
      }
    }
    tx.plot(plot).provinces.clear();
    // Герои: этап 3 — без «Значимого героя фракции», 4 — все, 5 — и лидер.
    std::vector<Id> heroes;
    for (Id h : P.heroes) {
      const Character* c = tx.w().character(h);
      if (!c || !alive(tx.w(), h)) continue;
      if (h == P.leader && stage < 5) continue;
      if (stage == 3 && c->hero) continue;
      if (stage >= 3) heroes.push_back(h);
    }
    for (Id h : heroes) {
      arrestHero(tx, state, h);
      eraseValue(tx.plot(plot).heroes, h);
      res.heroes.push_back(h);
    }
    if (stage >= 5) {
      tx.plot(plot).leader = 0;
      res.removed = true;
    }
  }
  toPrison(tx, state, jailed);
  res.people = sum(jailed);
  // Заговор перестаёт быть виден контрразведке (полностью раскрытый — удаляется).
  if (res.removed) {
    removePlot(tx, plot);
  } else {
    Plot& m = tx.plot(plot);
    m.found = false;
    m.stage = 0;
    m.alarm = false;   // расследование закончилось арестами: отметка «Заговор частично раскрыт!» снимается
  }
  std::string text = "Аресты заговорщиков: " + facName(tx.w(), state);
  if (!where.empty()) text += " — " + join(where, ", ");
  if (res.people > 0) text += "; арестовано жителей: " + fmtInt(res.people);
  if (!res.heroes.empty()) {
    std::vector<std::string> names;
    for (Id h : res.heroes) names.push_back(q(tx.w().characterName(h)));
    text += "; под арестом: " + join(names, ", ");
  }
  addLog(tx, LogKind::Province, text, LogRefs{0, 0, {state}});
  fitPlots(tx);
  return res;
}

void releasePrisoners(Tx& tx, Id state, const std::map<Id, i64>& byRace) {
  const Faction& f = needState(tx.w(), state);
  if (!tx.w().provinces.findIf([&](const Province& p) { return p.owner == state && !p.sea; }))
    fail("У государства нет провинций — заключённых некуда отпустить");
  i64 total = 0;
  for (auto& [race, n] : byRace) {
    if (n <= 0) continue;
    auto it = std::find_if(f.prison.begin(), f.prison.end(), [&](const RacePop& x) { return x.race == race; });
    if (it == f.prison.end() || it->pop < n) fail("В тюрьме нет столько заключённых этой расы");
    total += n;
  }
  if (total <= 0) fail("Укажите, сколько заключённых освободить");
  for (auto& [race, n] : byRace) {
    if (n <= 0) continue;
    std::vector<RacePop>& pr = tx.faction(state).prison;
    for (RacePop& x : pr)
      if (x.race == race) x.pop -= n;
    pr.erase(std::remove_if(pr.begin(), pr.end(), [](const RacePop& x) { return x.pop <= 0; }), pr.end());
    givePopulation(tx, state, n, 0, std::map<Id, i64>{{race, n}});
  }
  addLog(tx, LogKind::Population, facName(tx.w(), state) + ": из тюрьмы освобождено " + fmtInt(total), LogRefs{0, 0, {state}});
}

std::vector<Id> prisonHeroes(const World& w, Id state) {
  return idsWhere(w.characters, [&](const Character& c) { return c.jailer == state && hasModKey(w, c.modifiers, schema::mod::Arrested); });
}

void releaseHero(Tx& tx, Id state, Id character) {
  const Character& c = needCharacter(tx.w(), character);
  if (c.jailer != state || !characterHas(tx.w(), character, schema::mod::Arrested)) fail(q(tx.w().characterName(character)) + " не в тюрьме этого государства");
  for (Id m : std::vector<Id>(c.modifiers))
    if (const Modifier* x = tx.w().modifier(m); x && x->key == schema::mod::Arrested) dropModifier(tx, ModTarget::Character, character, m);
  tx.character(character).jailer = 0;
  addLog(tx, LogKind::Note, q(tx.w().characterName(character)) + " освобождён из тюрьмы", LogRefs{0, 0, {state}});
}

// ================================================================ осуществление
std::string executeProblem(const World& w, Id plot) {
  const Plot* p = w.plot(plot);
  if (!p) return "Заговор не найден";
  if (!w.faction(p->target)) return "Государство-цель не найдено";
  if (!p->leader || !inList(p->heroes, p->leader) || !alive(w, p->leader)) return "Назначьте лидера заговора";
  if (characterHas(w, p->leader, schema::mod::Captive)) return "Лидер заговора в плену — назначьте другого";
  if (characterHas(w, p->leader, schema::mod::Arrested)) return "Лидер заговора под арестом — назначьте другого";
  if (p->provinces.empty() && p->heroes.empty()) return "В заговоре нет провинций и героев";
  return {};
}

i64 plotRebels(const World& w, const PlotProvince& pp) {
  const Province* p = w.province(pp.province);
  if (!p) return 0;
  const i64 pop = provincePop(*p);
  return std::min(pop, i64(std::floor(double(pop) * clamp(pp.commitment, 0.0, 100.0) * schema::kPlotRiseShare)));
}

std::vector<PlotSquad> plotSquads(const World& w, const PlotProvince& pp, i64 rebels) {
  using schema::PlotArms;
  std::vector<PlotSquad> out;
  i64 left = std::max<i64>(0, rebels);
  auto take = [&](PlotArms arms, const char* name, UnitType type) {
    if (left <= 0) return;
    const i64 armed = i64(std::floor(armsSum(w, pp, arms) / schema::kPlotPerUnit + 1e-9));
    const i64 n = std::min(left, armed);
    if (n <= 0) return;
    out.push_back(PlotSquad{name, type, schema::kRaceLiving, n});
    left -= n;
  };
  take(PlotArms::Firearms, "Заговорщики-Мушкетеры", UnitType::Ranged);
  take(PlotArms::Steel, "Заговорщики-Латники", UnitType::HeavyInf);
  take(PlotArms::MoonSilver, "Заговорщики-Стрелки Лунного Серебра", UnitType::Ranged);
  take(PlotArms::Iron, "Заговорщики-Пехотинцы", UnitType::MediumInf);
  if (left > 0) out.push_back(PlotSquad{"Заговорщики-Ополченцы", UnitType::LightInf, schema::kRaceLiving, left});
  // Вне населения: баллисты за механические детали, големы за запчасти механизмов.
  if (const i64 n = i64(std::floor(armsSum(w, pp, PlotArms::MechParts) / schema::kPlotPerBallista + 1e-9)); n > 0)
    out.push_back(PlotSquad{"Баллисты", UnitType::Machines, schema::kRaceMechanical, n});
  if (const i64 n = i64(std::floor(armsSum(w, pp, PlotArms::SpareParts) / schema::kPlotPerGolem + 1e-9)); n > 0)
    out.push_back(PlotSquad{"Големы", UnitType::HeavyInf, schema::kRaceMechanical, n});
  return out;
}

namespace {

// Строка армии государства заговора по названию, типу и расе (существующая или новая, без содержания).
Id squadRow(Tx& tx, Id state, const PlotSquad& s) {
  for (const ArmyRow& r : tx.w().faction(state)->army)
    if (r.name == s.name && r.type == s.type && r.race == s.race) return r.id;
  const Id id = addArmyRow(tx, state, s.type, s.name, 0, 0);
  setRowRace(tx, state, id, s.race);
  return id;
}

// Строка флота государства заговора с теми же названием, типом и содержанием.
Id fleetMirror(Tx& tx, Id state, const FleetRow& src) {
  for (const FleetRow& r : tx.w().faction(state)->fleet)
    if (r.name == src.name && r.type == src.type && r.upkeep == src.upkeep) return r.id;
  const Id id = addFleetRow(tx, state, src.type, src.name, 0, src.upkeep);
  if (src.keyRes)
    if (FleetRow* r = findById(tx.faction(state).fleet, id)) r->keyRes = src.keyRes;
  return id;
}

// Перенести count отрядов строки row государства origin в группу dst (строка-зеркало у state).
void moveRowUnits(Tx& tx, Id origin, Id state, bool fleet, Id row, i64 count, ArmyGroup& dst) {
  if (count <= 0) return;
  Id mrow = 0;
  if (fleet) {
    const FleetRow* src = tx.w().faction(origin)->fleetRow(row);
    if (!src) return;
    const FleetRow copy = *src;
    mrow = fleetMirror(tx, state, copy);
    if (FleetRow* r = findById(tx.faction(origin).fleet, row)) r->total = std::max<i64>(0, r->total - count);
    if (FleetRow* r = findById(tx.faction(state).fleet, mrow)) r->total = std::min(kMaxCount, r->total + count);
  } else {
    const ArmyRow* src = tx.w().faction(origin)->armyRow(row);
    if (!src) return;
    const ArmyRow copy = *src;
    mrow = mirrorRow(tx, state, origin, copy);
    if (ArmyRow* r = findById(tx.faction(origin).army, row)) r->total = std::max<i64>(0, r->total - count);
    if (ArmyRow* r = findById(tx.faction(state).army, mrow)) r->total = std::min(kMaxCount, r->total + count);
  }
  for (ArmyUnit& u : dst.units)
    if (u.row == mrow) {
      u.count += count;
      return;
    }
  dst.units.push_back(ArmyUnit{mrow, count});
}

i64 share(i64 count, size_t k, size_t n) {
  if (n == 0 || k == 0) return 0;
  if (k >= n) return count;
  return clamp<i64>(std::llround(double(count) * double(k) / double(n)), 0, count);
}

}  // namespace

PlotResult executePlot(Tx& tx, Id plot) {
  const std::string why = executeProblem(tx.w(), plot);
  if (!why.empty()) fail(why);
  const Plot P = *tx.w().plot(plot);
  const Id target = P.target;
  PlotResult res;

  // Участники — живые герои цели.
  std::vector<Id> heroes;
  for (Id h : P.heroes)
    if (const Character* c = tx.w().character(h); c && c->faction == target && alive(tx.w(), h)) heroes.push_back(h);

  // Государство заговора.
  const Faction& T = *tx.w().faction(target);
  const std::string name = "Заговор " + nameOr(tx.w().characterName(P.leader), kNameless);
  const Color base = T.color;
  const StateKind kind = T.stateKind;   // заговорщики — того же вида, что и государство-цель
  const Id capital = T.capital;
  const Id S = createFaction(tx, FactionKind::State, name);
  {
    Faction& s = tx.faction(S);
    s.conspiracy = true;
    s.rebelOf = target;
    s.stateKind = kind;
    s.color = Color::mix(base, Color::hex(0x2a1a3a), 0.6f);
    s.flag.colors = {s.color, Color::hex(0x141018), Color::hex(0xd9c79a)};
    s.flag.pattern = FlagPattern::Bend;
    s.flag.emblem = "dark-elf-daggers";
    s.flag.emblemColor = Color::hex(0xd9c79a);
  }
  res.state = S;
  declareWar(tx, S, target);
  if (P.external() && tx.w().faction(P.owner)) {
    const Relation r = tx.w().relation(S, P.owner);
    tx.setRelation(S, P.owner, Relation{std::max(r.v, 75.0), RelStatus::Alliance});
  }

  Placement place(tx.w(), facesFor(tx));
  auto newArmy = [&](ArmyKind kind, const std::string& nm, Vec2 at, ArmyGroup g, Id commander) -> Id {
    auto spot = place.freeSpot(kind, at, 0);
    if (!spot) return 0;
    Army a;
    a.kind = kind;
    a.name = nm;
    a.pos = *spot;
    a.groups.push_back(std::move(g));
    a.commander = commander;
    a.loyalty = schema::kMaxLoyalty;
    const Id id = tx.add(std::move(a)).id;
    place.set(id, *spot);
    res.armies.push_back(id);
    return id;
  };

  // Восстания провинций.
  std::vector<Id> uprisings;
  for (const PlotProvince& pp : P.provinces) {
    const Province* pr = tx.w().province(pp.province);
    if (!pr || pr->sea || pr->owner != target) continue;
    const i64 rebels = plotRebels(tx.w(), pp);
    const std::vector<PlotSquad> squads = plotSquads(tx.w(), pp, rebels);
    i64 people = 0;
    for (const PlotSquad& s : squads)
      if (s.race == schema::kRaceLiving) people += s.count;
    takeFromProvince(tx, pp.province, people);
    res.rebels += people;
    if (tx.w().province(pp.province)->contentment < 0) tx.province(pp.province).contentment = 0;
    if (squads.empty()) continue;
    ArmyGroup g{S, {}, {}};
    for (const PlotSquad& s : squads) {
      const Id row = squadRow(tx, S, s);
      if (ArmyRow* r = findById(tx.faction(S).army, row)) r->total = std::min(kMaxCount, r->total + s.count);
      g.units.push_back(ArmyUnit{row, s.count});
    }
    const auto lab = provinceLabel(tx, pp.province);
    if (!lab) continue;
    if (Id a = newArmy(ArmyKind::Army, "Заговорщики: " + tx.w().provinceName(pp.province), *lab, std::move(g), 0)) uprisings.push_back(a);
  }
  // Провинции заговора без восстания тоже успокаиваются (отрицательное довольство — 0).
  for (const PlotProvince& pp : P.provinces)
    if (const Province* pr = tx.w().province(pp.province); pr && pr->owner == target && pr->contentment < 0) tx.province(pp.province).contentment = 0;

  // Где герои заговора: войска и флот (группа цели), гарнизоны; остальные свободны. Копии групп и гарнизонов —
  // до изменений (таблицы черновика меняются на месте).
  struct Split {
    Id object = 0;              // войско, флот или провинция гарнизона
    bool garrison = false;
    bool fleet = false;
    Vec2 pos;
    std::string name;
    std::vector<ArmyUnit> units;   // отряды группы цели (гарнизон — строки армии)
    std::vector<Id> heroes;     // герои заговора
    size_t all = 0;             // всех героев в группе
  };
  std::vector<Split> splits;
  std::vector<Id> placed;
  tx.w().armies.each([&](const Army& a) {
    for (const ArmyGroup& g : a.groups) {
      if (g.faction != target) continue;
      Split s;
      s.object = a.id;
      s.fleet = a.isFleet();
      s.pos = a.pos;
      s.name = a.name;
      s.units = g.units;
      s.all = g.heroes.size();
      for (Id h : g.heroes)
        if (contains(heroes, h) && !contains(placed, h)) s.heroes.push_back(h);
      if (!s.heroes.empty()) {
        for (Id h : s.heroes) placed.push_back(h);
        splits.push_back(std::move(s));
      }
    }
  });
  tx.w().provinces.each([&](const Province& p) {
    if (p.owner != target) return;
    Split s;
    s.object = p.id;
    s.garrison = true;
    for (const GarrisonEntry& e : p.garrison) s.units.push_back(ArmyUnit{e.row, e.count});
    s.all = p.garrisonHeroes.size();
    for (Id h : p.garrisonHeroes)
      if (contains(heroes, h) && !contains(placed, h)) s.heroes.push_back(h);
    if (!s.heroes.empty()) {
      for (Id h : s.heroes) placed.push_back(h);
      splits.push_back(std::move(s));
    }
  });
  // Назначения у цели снимаются (совет, правитель, лорд, войска, гарнизоны); герои переходят к заговору.
  for (Id h : heroes) clearAssignments(tx, h);
  for (Id h : heroes) tx.character(h).faction = S;
  tx.faction(S).ruler = P.leader;

  // Отколовшиеся части войск, флота и гарнизонов — по доле героев заговора в группе.
  for (const Split& s : splits) {
    ArmyGroup g{S, {}, s.heroes};
    if (s.garrison) {
      for (const ArmyUnit& e : s.units) {
        const i64 n = share(e.count, s.heroes.size(), s.all);
        if (n <= 0) continue;
        for (GarrisonEntry& x : tx.province(s.object).garrison)
          if (x.row == e.row) x.count -= n;
        moveRowUnits(tx, target, S, false, e.row, n, g);
      }
      Province& m = tx.province(s.object);
      m.garrison.erase(std::remove_if(m.garrison.begin(), m.garrison.end(), [](const GarrisonEntry& x) { return x.count <= 0; }), m.garrison.end());
      if (const auto lab = provinceLabel(tx, s.object))
        newArmy(ArmyKind::Army, "Заговорщики гарнизона: " + tx.w().provinceName(s.object), *lab, std::move(g), s.heroes.front());
      continue;
    }
    for (const ArmyUnit& u : s.units) {
      const i64 n = share(u.count, s.heroes.size(), s.all);
      if (n <= 0) continue;
      if (tx.w().army(s.object))
        for (ArmyGroup& x : tx.army(s.object).groups)
          if (x.faction == target)
            for (ArmyUnit& y : x.units)
              if (y.row == u.row) y.count -= n;
      moveRowUnits(tx, target, S, s.fleet, u.row, n, g);
    }
    // Группа цели без отрядов уходит из объекта; объект без групп удаляется (флот высаживает войско на борту).
    if (tx.w().army(s.object)) {
      Army& a = tx.army(s.object);
      auto it = std::find_if(a.groups.begin(), a.groups.end(), [&](const ArmyGroup& x) { return x.faction == target; });
      if (it != a.groups.end()) {
        it->units.erase(std::remove_if(it->units.begin(), it->units.end(), [](const ArmyUnit& y) { return y.count <= 0; }), it->units.end());
        if (it->units.empty()) {
          a.groups.erase(it);
          if (a.groups.empty()) {
            eraseObject(tx, s.object);
            place.remove(s.object);
          }
        }
      }
    }
    const std::string nm = (s.fleet ? "Флот заговорщиков" : "Войско заговорщиков") + (s.name.empty() ? std::string() : ": " + s.name);
    if (!g.units.empty() || !g.heroes.empty()) newArmy(s.fleet ? ArmyKind::Fleet : ArmyKind::Army, nm, s.pos, std::move(g), s.heroes.front());
  }

  // Свободные герои заговора — в одно из войск восстания (случайно).
  std::vector<Id> free;
  for (Id h : heroes)
    if (!contains(placed, h)) free.push_back(h);
  if (!free.empty() && !uprisings.empty()) {
    Rng rng(hashMix(hashMix(0x50524F544553544Full, u64(plot)), u64(u32(tx.w().turn()))));
    for (Id h : free) {
      const Id a = uprisings[size_t(rng.range(0, int(uprisings.size()) - 1))];
      Army& m = tx.army(a);
      m.groups[0].heroes.push_back(h);
      if (!m.commander) m.commander = h;
    }
  }
  // Войск восстания нет — свободные герои образуют своё войско у провинции, где лидер — лорд (иначе у столицы цели,
  // иначе у первой её провинции): государство заговора не исчезает в конце хода без войск.
  if (!free.empty() && uprisings.empty()) {
    Id at = tx.w().provinces.firstId([&](const Province& p) { return p.lord == P.leader && !p.sea; });
    if (!at && capital && tx.w().province(capital)) at = capital;
    if (!at) at = tx.w().provinces.firstId([&](const Province& p) { return p.owner == target && !p.sea; });
    if (const auto lab = at ? provinceLabel(tx, at) : std::nullopt) {
      const Id cmd = contains(free, P.leader) ? P.leader : free.front();
      newArmy(ArmyKind::Army, "Заговорщики: " + nameOr(tx.w().characterName(P.leader), kNameless), *lab, ArmyGroup{S, {}, free}, cmd);
    }
  }

  // Заговор осуществлён — запись удаляется; хроника — без заказчика.
  tx.erasePlot(plot);
  std::vector<std::string> provs;
  for (const PlotProvince& pp : P.provinces)
    if (tx.w().province(pp.province)) provs.push_back(provName(tx.w(), pp.province));
  addLog(tx, LogKind::War,
         "Заговор против " + facName(tx.w(), target) + ": образовано государство " + facName(tx.w(), S) +
             (provs.empty() ? std::string() : ", восстание в провинциях " + join(provs, ", ")) +
             (res.rebels > 0 ? ", восставших — " + fmtInt(res.rebels) : std::string()) +
             (heroes.empty() ? std::string() : ", героев — " + std::to_string(heroes.size())),
         LogRefs{0, 0, {target, S}});
  fitPlots(tx);
  return res;
}

// ================================================================ очистка ссылок
void fitPlots(Tx& tx) {
  if (tx.w().plots.empty()) return;
  const World& w = tx.w();
  std::vector<Id> drop;
  std::vector<std::pair<Id, Plot>> fixed;
  w.plots.each([&](const Plot& p) {
    const Faction* t = w.faction(p.target);
    if (!t || !t->isState() || (p.external() && (!w.faction(p.owner) || !w.faction(p.owner)->isState()))) {
      drop.push_back(p.id);
      return;
    }
    Plot m = p;
    m.provinces.erase(std::remove_if(m.provinces.begin(), m.provinces.end(),
                                     [&](const PlotProvince& x) {
                                       const Province* pr = w.province(x.province);
                                       return !pr || pr->sea || pr->owner != p.target;
                                     }),
                      m.provinces.end());
    // Участники, приглашённые и угрозы — доступные герои цели: мёртвые, пленные и арестованные выходят из заговора
    // (заказ на снятую угрозу возвращается заказчику).
    auto ok = [&](Id h) {
      const Character* c = w.character(h);
      return c && c->faction == p.target && !hasModKey(w, c->modifiers, schema::mod::Dead) && !hasModKey(w, c->modifiers, schema::mod::Captive) &&
             !hasModKey(w, c->modifiers, schema::mod::Arrested);
    };
    for (std::vector<Id>* v : {&m.heroes, &m.invited, &m.threats}) v->erase(std::remove_if(v->begin(), v->end(), [&](Id h) { return !ok(h); }), v->end());
    if (m.leader && !inList(m.heroes, m.leader)) m.leader = 0;
    m.hits.erase(std::remove_if(m.hits.begin(), m.hits.end(), [&](const PlotHit& h) { return !inList(m.threats, h.character); }), m.hits.end());
    if (m.provinces != p.provinces || m.heroes != p.heroes || m.invited != p.invited || m.threats != p.threats || m.leader != p.leader ||
        m.hits != p.hits)
      fixed.push_back({p.id, std::move(m)});
  });
  // Неисполненные заказы снятых героев и удалённых заговоров возвращаются заказчику.
  std::vector<std::pair<Id, double>> back;
  for (Id id : drop)
    if (const Plot* p = w.plot(id); p && p->external())
      for (const PlotHit& h : p->hits) back.push_back({p->owner, h.gold});
  for (auto& [id, m] : fixed)
    if (const Plot* p = w.plot(id); p && p->external())
      for (const PlotHit& h : p->hits)
        if (!m.hit(h.character)) back.push_back({p->owner, h.gold});
  for (Id id : drop) tx.erasePlot(id);
  for (auto& [id, m] : fixed) tx.plot(id) = std::move(m);
  for (auto& [owner, gold] : back)
    if (gold > 0 && tx.w().faction(owner)) addStock(tx.faction(owner), kGold, gold);
}

}  // namespace rg::rules
