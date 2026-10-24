// Regnum — перемирие и вассалитет.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

// Пленный герой, которого держит государство holder.
bool heldBy(const World& w, Id character, Id holder) {
  const Character* c = w.character(character);
  return c && c->captor == holder && hasModKey(w, c->modifiers, schema::mod::Captive);
}

void termProblems(const World& w, Id giver, Id taker, const TruceTerms& t, std::vector<std::string>& out) {
  const std::string gn = facName(w, giver);
  for (Id pid : t.provinces) {
    const Province* p = w.province(pid);
    if (!p) out.push_back("Провинция не найдена");
    else if (p->owner != giver) out.push_back(provName(w, pid) + " не принадлежит " + gn);
    else if (p->sea) out.push_back(provName(w, pid) + " — морская провинция");
  }
  if (t.reparations < 0 || !std::isfinite(t.reparations)) out.push_back("Репарации не могут быть отрицательными");
  if (t.reparations > 0 && t.reparationsTurns < 1) out.push_back("Срок репараций — не меньше одного хода");
  const Faction* g = w.faction(giver);
  for (auto& [res, v] : t.resources) {
    if (!w.resource(res)) {
      out.push_back("Ресурс не найден");
      continue;
    }
    if (!std::isfinite(v) || v < 0) out.push_back("Выплата ресурса «" + resName(w, res) + "» не может быть отрицательной");
    else if (g && v > 0 && g->stock(res) + 1e-9 < v)
      out.push_back("Недостаточно ресурса «" + resName(w, res) + "» у " + gn + ": нужно " + amount(v) + ", есть " + amount(std::max(0.0, g->stock(res))));
  }
  if (t.slaves < 0) out.push_back("Рабов не может быть меньше нуля");
  else if (t.slaves > truceSlavesMax(w, giver))
    out.push_back("Рабов от " + gn + " — не больше 5 % населения: " + fmtInt(truceSlavesMax(w, giver)));
  for (Id h : t.heroes)
    if (!heldBy(w, h, giver)) out.push_back(q(w.characterName(h)) + " не в плену у " + gn);
  (void)taker;
}

// Сухопутные провинции государства.
int landCount(const World& w, Id state) {
  int n = 0;
  w.provinces.each([&](const Province& p) {
    if (p.owner == state && !p.sea) n++;
  });
  return n;
}

}  // namespace

// Государство потеряло последние провинции (перемирие — ТЗ «Доработки №1», п.18; разорение или опустошение последней
// провинции): победителю переходят все его ресурсы и эссенции, кроме группы «Валюты»; если у побеждённого не осталось
// ни одного войска и флота на карте — и валюты (золото тоже). Реликвии склада и заключённые тюрем — победителю всегда.
std::string conquestLoot(Tx& tx, Id loser, Id winner) {
  const World& w = tx.w();
  if (loser == winner || !w.faction(loser) || !w.faction(winner)) return {};
  bool forces = false;
  w.armies.each([&](const Army& a) {
    for (const ArmyGroup& g : a.groups) forces = forces || g.faction == loser;
  });
  const Id currencies = w.catalogs->groupId(schema::grp::Currencies);
  auto currency = [&](Id r) { return r == kGold || (currencies && w.catalogs->resourceIn(r, currencies)); };
  const Faction L = *w.faction(loser);
  std::vector<std::string> got;
  bool gold = false;
  for (auto& [r, v] : L.res) {
    if (!(v > 1e-9) || !w.resource(r) || (currency(r) && forces)) continue;
    addStock(tx.faction(loser), r, -v);
    addStock(tx.faction(winner), r, v);
    if (r == kGold) gold = true;
    else got.push_back(resName(w, r) + " " + amount(v));
  }
  for (auto& [e, v] : L.ess) {
    if (!(v > 1e-9) || !w.essence(e)) continue;
    tx.faction(loser).ess[e] -= v;
    if (std::fabs(tx.w().faction(loser)->essence(e)) < 1e-9) tx.faction(loser).ess.erase(e);
    tx.faction(winner).ess[e] += v;
    got.push_back(w.essence(e)->name + " " + amount(v));
  }
  // Реликвии склада побеждённого — в инвентарь победителя (уникальная переносится, копируемые — стопкой).
  std::vector<std::string> relics;
  for (const RelicStack& s : L.relics) {
    const Relic* r = w.relic(s.relic);
    if (!r || s.count <= 0) continue;
    const std::string name = q(r->name) + (s.count > 1 ? " × " + fmtInt(s.count) : std::string());
    moveRelic(tx, s.relic, RelicPlace{RelicPlace::State, loser, 0, loser}, RelicPlace{RelicPlace::State, winner, 0, winner}, s.count);
    relics.push_back(name);
  }
  // Заключённые тюрем побеждённого — в тюрьмы победителя (по расам).
  i64 prisoners = 0;
  for (const RacePop& r : L.prison) {
    if (r.pop <= 0) continue;
    prisoners += r.pop;
    std::vector<RacePop>& to = tx.faction(winner).prison;
    auto it = std::find_if(to.begin(), to.end(), [&](const RacePop& x) { return x.race == r.race; });
    if (it != to.end()) it->pop = std::min(kMaxCount, it->pop + r.pop);
    else to.push_back(RacePop{r.race, r.pop});
  }
  if (!L.prison.empty()) tx.faction(loser).prison.clear();
  if (got.empty() && !gold && relics.empty() && prisoners <= 0) return {};
  std::string text = facName(w, winner) + " забирает запасы " + facName(w, loser);
  if (gold) text += " (с казной: " + fmtGold(L.treasury()) + ")";
  else if (forces) text += " (валюты остаются: у побеждённого есть войска)";
  if (!got.empty()) text += ": " + join(got, ", ");
  if (!relics.empty()) text += "; реликвии: " + join(relics, ", ");
  if (prisoners > 0) text += "; заключённых: " + fmtInt(prisoners);
  return text;
}

i64 truceSlavesMax(const World& w, Id giver) { return i64(std::floor(double(statePopulation(w, giver)) * schema::kTruceSlavesShare)); }

std::vector<std::string> truceProblems(const World& w, const Truce& t) {
  std::vector<std::string> out;
  const Faction* A = w.faction(t.a);
  const Faction* B = w.faction(t.b);
  if (!A || !B) {
    out.push_back("Сторона перемирия не найдена");
    return out;
  }
  if (t.a == t.b) out.push_back("Стороны перемирия совпадают");
  if (!A->isState() || !B->isState()) out.push_back("Перемирие заключают государства");
  if (w.relation(t.a, t.b).s != RelStatus::War) out.push_back(facName(w, t.a) + " и " + facName(w, t.b) + " не в войне");
  if (t.status == RelStatus::War) out.push_back("После перемирия стороны не могут остаться в войне");
  if (t.fromA.vassal && t.fromB.vassal) out.push_back("Стороны не могут стать вассалами друг друга");
  // Вассалитет по перемирию — без цикла: будущий сюзерен не может быть вассалом будущего вассала (прямо или через цепочку).
  auto chainCheck = [&](Id vassal, Id suzerain) {
    for (Id s = suzerain, guard = 0; s && guard < 64; guard++) {
      const Faction* x = w.faction(s);
      s = x ? x->suzerain : 0;
      if (s == vassal) {
        out.push_back(facName(w, suzerain) + " — вассал " + facName(w, vassal) + " (прямо или через цепочку)");
        return;
      }
    }
  };
  if (t.fromA.vassal) chainCheck(t.a, t.b);
  if (t.fromB.vassal) chainCheck(t.b, t.a);
  termProblems(w, t.a, t.b, t.fromA, out);
  termProblems(w, t.b, t.a, t.fromB, out);
  std::vector<std::string> uniq;
  for (auto& s : out)
    if (!contains(uniq, s)) uniq.push_back(s);
  return uniq;
}

void concludeTruce(Tx& tx, const Truce& t) {
  std::vector<std::string> problems = truceProblems(tx.w(), t);
  if (!problems.empty()) fail(join(problems, "; "));
  // Мир — сразу: условия (репарации — сделка) исполняются уже не между воюющими. Срок перемирия: войну стороны не
  // объявят друг другу до хода calm (константа «Мир после перемирия»).
  int calm = 0;
  {
    const double n = constantOf(tx.w(), schema::cst::TruceCalm).num;
    if (std::isfinite(n) && n >= 1) calm = tx.w().turn() + int(std::lround(std::min(n, 1e6)));
    const Relation r = tx.w().relation(t.a, t.b);
    tx.setRelation(t.a, t.b, Relation{r.v, t.status, calm});
  }
  std::vector<std::string> parts;
  auto apply = [&](Id giver, Id taker, const TruceTerms& terms) {
    const std::string gn = facName(tx.w(), giver), tn = facName(tx.w(), taker);
    // Провинции.
    std::vector<std::string> provs;
    for (Id pid : terms.provinces) {
      provs.push_back(provName(tx.w(), pid));
      setProvinceOwner(tx, pid, taker);
    }
    if (!provs.empty()) parts.push_back(gn + " уступает " + tn + ": " + join(provs, ", "));
    // Разовые выплаты.
    std::vector<std::string> pay;
    for (auto& [res, v] : terms.resources) {
      if (!(v > 0)) continue;
      addStock(tx.faction(giver), res, -v);
      addStock(tx.faction(taker), res, v);
      pay.push_back(resName(tx.w(), res) + " " + amount(v));
    }
    if (!pay.empty()) parts.push_back(gn + " выплачивает " + tn + ": " + join(pay, ", "));
    // Репарации — как в окне «Дань или репарации».
    if (terms.reparations > 0) imposeTribute(tx, DealKind::Reparations, taker, giver, terms.reparations, terms.reparationsTurns);
    // Рабы из населения: поровну со всех провинций, по расам.
    if (terms.slaves > 0) {
      std::map<Id, i64> taken = takePopulation(tx, giver, terms.slaves);
      std::vector<SlaveGroup>& pool = tx.faction(taker).slaves;
      for (auto& [race, n] : taken) {
        if (n <= 0) continue;
        auto it = std::find_if(pool.begin(), pool.end(), [&](const SlaveGroup& g) { return g.race == race; });
        if (it != pool.end()) it->count = std::min(kMaxCount, it->count + n);
        else pool.push_back(SlaveGroup{race, n, 0});
      }
      parts.push_back(gn + " отдаёт " + tn + " рабов: " + fmtInt(terms.slaves));
    }
    // Пленные герои: свои — освобождаются, чужие — переходят в плен к другой стороне.
    for (Id h : terms.heroes) {
      const Character& c = *tx.w().character(h);
      if (c.faction == taker) {
        for (Id m : std::vector<Id>(c.modifiers))
          if (const Modifier* x = tx.w().modifier(m); x && x->key == schema::mod::Captive) dropModifier(tx, ModTarget::Character, h, m);
        parts.push_back(q(tx.w().characterName(h)) + " освобождён из плена");
      } else {
        tx.character(h).captor = taker;
        parts.push_back(q(tx.w().characterName(h)) + " передан в плен " + tn);
      }
    }
    if (terms.vassal) {
      const Id was = tx.w().faction(giver)->suzerain;
      if (was && was != taker) parts.push_back(gn + " выходит из вассалитета " + facName(tx.w(), was));
      tx.faction(giver).suzerain = taker;
      parts.push_back(gn + " становится вассалом " + tn);
    }
  };
  const int landA = landCount(tx.w(), t.a), landB = landCount(tx.w(), t.b);
  apply(t.a, t.b, t.fromA);
  apply(t.b, t.a, t.fromB);
  // Взаимные оккупации сторон снимаются (уступленная оккупанту провинция уже его — оккупации у неё нет); гарнизоны
  // оккупантов — в их резерв.
  {
    std::vector<std::string> freed;
    for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
           return p.occupied && ((p.owner == t.a && p.occupier == t.b) || (p.owner == t.b && p.occupier == t.a));
         })) {
      Province& p = tx.province(pid);
      p.occupied = false;
      p.occupier = 0;
      p.occGarrison.clear();
      p.occIdle = 0;
      freed.push_back(provName(tx.w(), pid));
    }
    if (!freed.empty()) parts.push_back("Оккупация снята: " + join(freed, ", "));
  }
  // Все земли побеждённого — победителю: и его запасы (без валют, пока у побеждённого есть войска).
  if (landA > 0 && !t.fromA.provinces.empty() && landCount(tx.w(), t.a) == 0)
    if (std::string s = conquestLoot(tx, t.a, t.b); !s.empty()) parts.push_back(s);
  if (landB > 0 && !t.fromB.provinces.empty() && landCount(tx.w(), t.b) == 0)
    if (std::string s = conquestLoot(tx, t.b, t.a); !s.empty()) parts.push_back(s);
  addLog(tx, LogKind::Diplomacy,
         "Перемирие " + facName(tx.w(), t.a) + " и " + facName(tx.w(), t.b) + ": " + utf8::lower(schema::relStatus(t.status).name) +
             (calm ? ", мир до хода " + std::to_string(calm) : std::string()) + (parts.empty() ? std::string() : ". " + join(parts, ". ")),
         LogRefs{0, 0, {t.a, t.b}});
  splitBrokenAlliances(tx, t.a, t.b);
  fitProposals(tx);   // предложения перемирия этой пары больше не нужны
}

// ================================================================ вассалитет
std::vector<Id> vassalsOf(const World& w, Id suzerain) {
  std::vector<Id> out;
  w.factions.each([&](const Faction& f) {
    if (f.suzerain == suzerain && f.isState()) out.push_back(f.id);
  });
  return out;
}

void setSuzerain(Tx& tx, Id vassal, Id suzerain) {
  const Faction& v = needState(tx.w(), vassal);
  if (v.suzerain == suzerain) return;
  if (suzerain) {
    needState(tx.w(), suzerain);
    if (suzerain == vassal) fail("Государство не может быть вассалом самого себя");
    for (Id s = suzerain, guard = 0; s && guard < 64; guard++) {
      if (s == vassal) fail(facName(tx.w(), suzerain) + " — вассал " + facName(tx.w(), vassal) + " (прямо или через цепочку)");
      const Faction* x = tx.w().faction(s);
      s = x ? x->suzerain : 0;
    }
  }
  const Id was = v.suzerain;
  tx.faction(vassal).suzerain = suzerain;
  if (suzerain) addLog(tx, LogKind::Diplomacy, facName(tx.w(), vassal) + " — вассал " + facName(tx.w(), suzerain), LogRefs{0, 0, {vassal, suzerain}});
  else addLog(tx, LogKind::Diplomacy, facName(tx.w(), vassal) + " больше не вассал " + facName(tx.w(), was), LogRefs{0, 0, {vassal, was}});
}

void suzerainDefends(Tx& tx, Id suzerain, Id vassal, Id attacker, bool join) {
  needState(tx.w(), suzerain);
  const Faction& v = needState(tx.w(), vassal);
  needFaction(tx.w(), attacker);
  if (v.suzerain != suzerain) fail(facName(tx.w(), vassal) + " — не вассал " + facName(tx.w(), suzerain));
  if (join) {
    if (attacker != suzerain && tx.w().relation(suzerain, attacker).s != RelStatus::War) declareWar(tx, suzerain, attacker);
    shiftRelation(tx, suzerain, vassal, schema::kVassalDefendBonus);
    addLog(tx, LogKind::Diplomacy, facName(tx.w(), suzerain) + " вступает в войну на стороне вассала " + facName(tx.w(), vassal),
           LogRefs{0, 0, {suzerain, vassal, attacker}});
  } else {
    shiftRelation(tx, suzerain, vassal, schema::kVassalAbandonPenalty);
    addLog(tx, LogKind::Diplomacy, facName(tx.w(), suzerain) + " не вступает в войну за вассала " + facName(tx.w(), vassal),
           LogRefs{0, 0, {suzerain, vassal}});
  }
}

void vassalAnswers(Tx& tx, Id suzerain, Id vassal, Id enemy, bool agree) {
  needState(tx.w(), suzerain);
  const Faction& v = needState(tx.w(), vassal);
  needFaction(tx.w(), enemy);
  if (v.suzerain != suzerain) fail(facName(tx.w(), vassal) + " — не вассал " + facName(tx.w(), suzerain));
  if (agree) {
    if (enemy != vassal && tx.w().relation(vassal, enemy).s != RelStatus::War) declareWar(tx, vassal, enemy);
    shiftRelation(tx, suzerain, vassal, schema::kVassalCallAgree);
    addLog(tx, LogKind::Diplomacy, facName(tx.w(), vassal) + " откликается на призыв " + facName(tx.w(), suzerain) + " и вступает в войну",
           LogRefs{0, 0, {suzerain, vassal, enemy}});
  } else {
    shiftRelation(tx, suzerain, vassal, schema::kVassalCallRefuse);
    addLog(tx, LogKind::Diplomacy, facName(tx.w(), vassal) + " отказывается вступить в войну " + facName(tx.w(), suzerain),
           LogRefs{0, 0, {suzerain, vassal}});
  }
}

bool canVassalRebel(const World& w, Id vassal, std::string* why) {
  const Faction* v = w.faction(vassal);
  if (!v || !v->suzerain) {
    if (why) *why = "Государство — не вассал";
    return false;
  }
  const double rel = relationValue(w, vassal, v->suzerain);   // с учётом одной религии (+10)
  if (rel > schema::kVassalRebelAt) {
    if (why) *why = "Восстать можно при отношениях с сюзереном " + fmtNum(schema::kVassalRebelAt) + " и ниже (сейчас " + fmtSigned(rel) + ")";
    return false;
  }
  return true;
}

void vassalRebels(Tx& tx, Id vassal) {
  std::string why;
  if (!canVassalRebel(tx.w(), vassal, &why)) fail(why);
  const Id s = tx.w().faction(vassal)->suzerain;
  tx.faction(vassal).suzerain = 0;
  Relation r = tx.w().relation(vassal, s);
  tx.setRelation(vassal, s, Relation{r.v, RelStatus::War});
  addLog(tx, LogKind::War, facName(tx.w(), vassal) + " восстаёт против сюзерена " + facName(tx.w(), s) + " — война", LogRefs{0, 0, {vassal, s}});
  splitBrokenAlliances(tx, vassal, s);
}

}  // namespace rg::rules
