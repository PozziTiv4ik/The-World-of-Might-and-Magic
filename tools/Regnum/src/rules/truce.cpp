// Regnum — перемирие (ТЗ «Механика войн», п.2) и вассалитет (ТЗ «Механика вассалитета»).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

constexpr i64 kMaxCount = 1000000000000000LL;

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

}  // namespace

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
      tx.faction(giver).suzerain = taker;
      parts.push_back(gn + " становится вассалом " + tn);
    }
  };
  apply(t.a, t.b, t.fromA);
  apply(t.b, t.a, t.fromB);
  Relation r = tx.w().relation(t.a, t.b);
  tx.setRelation(t.a, t.b, Relation{r.v, t.status});
  addLog(tx, LogKind::Diplomacy,
         "Перемирие " + facName(tx.w(), t.a) + " и " + facName(tx.w(), t.b) + ": " + utf8::lower(schema::relStatus(t.status).name) +
             (parts.empty() ? std::string() : ". " + join(parts, ". ")),
         LogRefs{0, 0, {t.a, t.b}});
  splitBrokenAlliances(tx, t.a, t.b);
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
