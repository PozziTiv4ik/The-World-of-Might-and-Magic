// Regnum — штурм гарнизона провинции и захват: захватить (оккупация), захватить и разграбить, разорить, опустошить;
// освобождение оккупированной провинции её владельцем или союзником (бой с оккупационным гарнизоном).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

// «У знака»: войско не дальше стольких единиц карты от точки привязки замка или башни провинции.
constexpr double kSiegeReach = 4 * schema::kObjectRadius;

// Сухопутные провинции государства.
int landOf(const World& w, Id state) {
  int n = 0;
  w.provinces.each([&](const Province& p) {
    if (p.owner == state && !p.sea) n++;
  });
  return n;
}

i64 entriesTotal(const std::vector<GarrisonEntry>& g) {
  i64 n = 0;
  for (const GarrisonEntry& e : g) n += std::max<i64>(0, e.count);
  return n;
}

// Войско у провинции: в самой провинции или у её знака «замок» или «башня» (знаки мира).
bool atProvince(const World& w, const geo::FaceSet& fs, Id province, Vec2 at) {
  if (fs.provinceAt(at) == province) return true;
  bool near = false;
  w.symbols.each([&](const MapSymbol& s) {
    if (near || (s.kind != SymbolKind::Castle && s.kind != SymbolKind::Tower)) return;
    if (dist2(s.p, at) <= kSiegeReach * kSiegeReach && fs.provinceAt(s.p) == province) near = true;
  });
  return near;
}

// Войско стороны side в провинции — её отряды или отряды её союзников, воюющих с нападающим L (первое по ID; 0 — нет).
// Войско на борту флота и объекты без отрядов провинцию не защищают.
Id sideArmyIn(const World& w, const geo::FaceSet& fs, Id province, Id side, Id L, Id skip) {
  return w.armies.firstId([&](const Army& a) {
    if (a.id == skip || a.isFleet() || aboard(w, a) || fs.provinceAt(a.pos) != province) return false;
    for (const ArmyGroup& g : a.groups) {
      if (std::none_of(g.units.begin(), g.units.end(), [](const ArmyUnit& u) { return u.count > 0; })) continue;
      if (g.faction == side) return true;
      if (g.faction != L && w.relation(g.faction, side).s == RelStatus::Alliance && w.relation(g.faction, L).s == RelStatus::War) return true;
    }
    return false;
  });
}

// Условия штурма и захвата (пусто — можно). siege — штурм: провинция ещё не оккупирована нападающим; захват после
// победы над гарнизоном позволяет разорить или опустошить и свою оккупированную провинцию. Чужую оккупацию не
// перехватывают.
std::string assaultProblem(const World& w, Id army, Id province, std::optional<Vec2> at, bool siege) {
  const Army* a = w.army(army);
  if (!a) return "Войско не найдено";
  if (a->isFleet()) return "Флот не штурмует провинции";
  if (carrierOf(w, army)) return "Войско на борту флота — сначала высадите его";
  if (a->allied()) return "Союзное войско не штурмует провинции — сначала распустите союз";
  const Faction* lf = w.faction(a->leader());
  // Войска без государства не вступают в бой с местными войсками и гарнизонами.
  if (lf && lf->isWild()) return "Войско без государства не штурмует провинции";
  // Захват — оккупация провинции государством: войско гильдии штурм начать не может (захватить провинцию ей нельзя).
  if (!lf || !lf->isState()) return "Провинции штурмуют только войска государств";
  const Province* p = w.province(province);
  if (!p) return "Провинция не найдена";
  if (p->sea) return "Морскую провинцию нельзя захватить";
  if (!p->owner) return "У провинции нет владельца";
  const Id L = a->leader();
  if (p->owner == L) return "Это провинция самого государства";
  if (siege && p->occupied && p->occupier == L) return "Провинция уже оккупирована " + facName(w, L);
  if (p->occupied && p->occupier && p->occupier != L) return "Провинция оккупирована " + facName(w, p->occupier);
  if (w.relation(L, p->owner).s != RelStatus::War) return facName(w, L) + " и " + facName(w, p->owner) + " не в войне";
  auto fs = geo::faces(w);
  if (!fs || fs->faces.empty()) return "Карта ещё не создана";
  if (!atProvince(w, *fs, province, at ? *at : a->pos)) return armyName(w, army) + " не у провинции " + provName(w, province);
  // Полевые войска владельца (и его союзников, воюющих с нападающим) защищают провинцию: сначала битва с ними.
  if (Id d = sideArmyIn(w, *fs, province, p->owner, L, army)) return "Сначала разбейте войско " + armyName(w, d);
  return {};
}

// Бой войска с гарнизоном провинции: гарнизоном владельца (штурм) или оккупационным гарнизоном оккупанта
// (освобождение). Потери вводятся, победитель выбирается; войско без отрядов исчезает (герои — «Судьба героев»).
BattleOutcome garrisonBattle(Tx& tx, const SiegeResult& r, bool occupation) {
  BattleOutcome out;
  const Army A = *tx.w().army(r.attacker);
  const Province P = *tx.w().province(r.province);
  const Id L = A.leader();
  const Id D = occupation ? P.occupier : P.owner;   // сторона гарнизона
  const std::vector<GarrisonEntry>& G = occupation ? P.occGarrison : P.garrison;
  const Faction DF = needState(tx.w(), D);
  // Проверка потерь.
  for (auto& [key, n] : r.attackerLosses) {
    if (n < 0) fail("Потери не могут быть отрицательными");
    if (n == 0) continue;
    i64 have = 0;
    for (const ArmyGroup& g : A.groups)
      if (g.faction == key.first)
        for (const ArmyUnit& u : g.units)
          if (u.row == key.second) have += u.count;
    if (n > have) fail("Потери больше численности отряда: " + fmtInt(n) + " из " + fmtInt(have));
  }
  i64 garrisonLeft = entriesTotal(G);
  for (auto& [row, n] : r.garrisonLosses) {
    if (n < 0) fail("Потери не могут быть отрицательными");
    i64 have = 0;
    for (const GarrisonEntry& g : G)
      if (g.row == row) have += g.count;
    if (n > have) fail("Потери гарнизона больше его численности: " + fmtInt(n) + " из " + fmtInt(have));
    garrisonLeft -= n;
  }
  i64 attackerLeft = unitsOf(A);
  for (auto& [key, n] : r.attackerLosses) attackerLeft -= std::max<i64>(0, n);
  if (r.attackerWins && attackerLeft <= 0 && garrisonLeft > 0) fail("Победителем не может быть войско, у которого не осталось отрядов");
  if (!r.attackerWins && garrisonLeft <= 0 && attackerLeft > 0) fail("Победителем не может быть гарнизон, от которого никого не осталось");

  // Потери нападающего: из отрядов войска и общей численности; живые погибшие — для трупов.
  i64 lossA = 0, lossG = 0, livingA = 0, livingG = 0;
  for (auto& [key, n] : r.attackerLosses) {
    if (n <= 0) continue;
    auto [faction, row] = key;
    lossA += n;
    if (const ArmyRow* ar = tx.w().faction(faction)->armyRow(row); ar && unitRace(tx.w(), faction, *ar) == schema::kRaceLiving) livingA += n;
    Army& m = tx.army(r.attacker);
    for (ArmyGroup& g : m.groups) {
      if (g.faction != faction) continue;
      i64 left = n;
      for (ArmyUnit& u : g.units)
        if (u.row == row && left > 0) {
          i64 take = std::min(left, u.count);
          u.count -= take;
          left -= take;
        }
    }
    if (ArmyRow* ar = findById(tx.faction(faction).army, row)) ar->total = std::max<i64>(0, ar->total - n);
  }
  // Потери гарнизона.
  for (auto& [row, n] : r.garrisonLosses) {
    if (n <= 0) continue;
    lossG += n;
    if (const ArmyRow* ar = DF.armyRow(row); ar && unitRace(tx.w(), D, *ar) == schema::kRaceLiving) livingG += n;
    Province& m = tx.province(r.province);
    for (GarrisonEntry& g : occupation ? m.occGarrison : m.garrison)
      if (g.row == row) g.count -= n;
    if (ArmyRow* ar = findById(tx.faction(D).army, row)) ar->total = std::max<i64>(0, ar->total - n);
  }
  {
    Province& m = tx.province(r.province);
    auto& gs = occupation ? m.occGarrison : m.garrison;
    gs.erase(std::remove_if(gs.begin(), gs.end(), [](const GarrisonEntry& g) { return g.count <= 0; }), gs.end());
  }

  // Войско без отрядов исчезает; его герои — в окно «Судьба героев».
  {
    Army& m = tx.army(r.attacker);
    for (ArmyGroup& g : m.groups) g.units.erase(std::remove_if(g.units.begin(), g.units.end(), [](const ArmyUnit& u) { return u.count <= 0; }), g.units.end());
    if (std::all_of(m.groups.begin(), m.groups.end(), [](const ArmyGroup& g) { return g.units.empty(); })) {
      for (const ArmyGroup& g : m.groups)
        for (Id h : g.heroes) out.fallenHeroes.push_back(h);
      out.destroyed.push_back(r.attacker);
      tx.eraseArmy(r.attacker);
    }
  }
  out.province = r.province;
  const std::string an = q(A.name.empty() ? std::string("Войско") : A.name);
  std::string text = occupation ? "Освобождение провинции " + provName(tx.w(), r.province) + ": " + an + " против оккупационного гарнизона " + facName(tx.w(), D)
                                : "Штурм провинции " + provName(tx.w(), r.province) + ": " + an + " против гарнизона " + facName(tx.w(), D);
  const bool anyLeft = attackerLeft > 0 || garrisonLeft > 0;
  if (anyLeft) out.winner = r.attackerWins ? L : D;
  if (anyLeft && r.attackerWins) {
    Province& gp = tx.province(r.province);
    if (occupation) {
      // Оккупационный гарнизон разбит: уцелевшие — в резерв оккупанта, оккупация снимается.
      gp.occGarrison.clear();
      gp.occupied = false;
      gp.occupier = 0;
      gp.occIdle = 0;
      text += ". Оккупационный гарнизон разбит, оккупация снята";
    } else {
      // Гарнизон разбит: уцелевшие возвращаются в резерв владельца, герои гарнизона — в окно «Судьба героев».
      for (Id h : P.garrisonHeroes) out.fallenHeroes.push_back(h);
      gp.garrison.clear();
      gp.garrisonHeroes.clear();
      gp.garrisonLoyalty = schema::kMaxLoyalty;
      text += ". Гарнизон разбит";
    }
    out.winnerArmy = tx.w().army(r.attacker) ? r.attacker : 0;
    std::vector<Id> heroes;
    for (const ArmyGroup& g : A.groups)
      for (Id h : g.heroes) heroes.push_back(h);
    out.corpses = battleCorpses(tx, L, heroes, livingG);
  } else if (anyLeft) {
    // Гарнизон устоял: нападающий отходит на исходную позицию (или рядом).
    if (tx.w().army(r.attacker)) {
      Vec2 to = std::isfinite(r.attackerOrigin.x) && std::isfinite(r.attackerOrigin.y) ? r.attackerOrigin : A.pos;
      auto spot = Placement(tx.w(), facesFor(tx)).freeSpot(ArmyKind::Army, to, r.attacker);
      if (spot) tx.army(r.attacker).pos = *spot;
    }
    out.corpses = battleCorpses(tx, D, {}, livingA);
    text += occupation ? ". Оккупанты устояли" : ". Гарнизон устоял";
  }
  text += ". Потери: " + fmtInt(lossA) + " и " + fmtInt(lossG);
  if (!out.destroyed.empty()) text += ". Войско уничтожено";
  if (out.corpses > 0) text += ". Трупов: " + fmtInt(i64(out.corpses));
  std::vector<Id> refs{L, D};
  if (occupation && P.owner && P.owner != L) refs.push_back(P.owner);
  addLog(tx, LogKind::Battle, text, LogRefs{r.province, r.attacker, refs});
  return out;
}

}  // namespace

bool canSiege(const World& w, Id army, Id province, std::string* why, std::optional<Vec2> at) {
  std::string s = assaultProblem(w, army, province, at, true);
  if (s.empty()) return true;
  if (why) *why = std::move(s);
  return false;
}

BattleOutcome resolveSiege(Tx& tx, const SiegeResult& r) {
  std::string why;
  if (!canSiege(tx.w(), r.attacker, r.province, &why)) fail(why);
  return garrisonBattle(tx, r, false);
}

// ================================================================ освобождение
bool canLiberate(const World& w, Id army, Id province, std::string* why, std::optional<Vec2> at) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Army* a = w.army(army);
  if (!a) return no("Войско не найдено");
  if (a->isFleet()) return no("Флот не освобождает провинции");
  if (carrierOf(w, army)) return no("Войско на борту флота — сначала высадите его");
  if (a->allied()) return no("Союзное войско не освобождает провинции — сначала распустите союз");
  const Faction* lf = w.faction(a->leader());
  if (!lf || !lf->isState()) return no("Провинции освобождают только войска государств");
  const Province* p = w.province(province);
  if (!p) return no("Провинция не найдена");
  if (p->sea || !p->occupied || !p->occupier) return no("Провинция " + provName(w, province) + " не оккупирована");
  if (!p->owner) return no("У провинции нет владельца");
  const Id L = a->leader(), O = p->occupier;
  if (O == L) return no("Провинция оккупирована самим " + facName(w, L));
  if (L != p->owner && w.relation(L, p->owner).s != RelStatus::Alliance)
    return no("Провинцию освобождает её владелец " + facName(w, p->owner) + " или его союзник");
  if (w.relation(L, O).s != RelStatus::War) return no(facName(w, L) + " и " + facName(w, O) + " не в войне");
  auto fs = geo::faces(w);
  if (!fs || fs->faces.empty()) return no("Карта ещё не создана");
  if (!atProvince(w, *fs, province, at ? *at : a->pos)) return no(armyName(w, army) + " не у провинции " + provName(w, province));
  // Войска оккупанта (и его союзников, воюющих с освободителем) в провинции — сначала битва с ними.
  if (Id d = sideArmyIn(w, *fs, province, O, L, army)) return no("Сначала разбейте войско " + armyName(w, d));
  return true;
}

BattleOutcome resolveLiberation(Tx& tx, const SiegeResult& r) {
  std::string why;
  if (!canLiberate(tx.w(), r.attacker, r.province, &why)) fail(why);
  const Province& p = *tx.w().province(r.province);
  if (entriesTotal(p.occGarrison) > 0) return garrisonBattle(tx, r, true);
  // У оккупанта нет в провинции ни гарнизона, ни войск: оккупация снимается без боя.
  BattleOutcome out;
  const Id L = tx.w().army(r.attacker)->leader(), O = p.occupier, owner = p.owner;
  Province& m = tx.province(r.province);
  m.occupied = false;
  m.occupier = 0;
  m.occGarrison.clear();
  m.occIdle = 0;
  out.winner = L;
  out.winnerArmy = r.attacker;
  out.province = r.province;
  std::vector<Id> refs{L, O};
  if (owner != L) refs.push_back(owner);
  addLog(tx, LogKind::War,
         facName(tx.w(), L) + " освобождает провинцию " + provName(tx.w(), r.province) + ": у " + facName(tx.w(), O) + " нет в ней гарнизона и войск",
         LogRefs{r.province, r.attacker, refs});
  return out;
}

Id assaultBlocker(const World& w, Id army, Id province) {
  const Army* a = w.army(army);
  const Province* p = w.province(province);
  if (!a || !p || a->isFleet() || p->sea || !p->owner) return 0;
  auto fs = geo::faces(w);
  if (!fs || fs->faces.empty()) return 0;
  const Id L = a->leader();
  // Освобождение: владелец или его союзник против оккупанта, с которым он в войне.
  if (p->occupied && p->occupier && p->occupier != L && (L == p->owner || w.relation(L, p->owner).s == RelStatus::Alliance) &&
      w.relation(L, p->occupier).s == RelStatus::War)
    return sideArmyIn(w, *fs, province, p->occupier, L, army);
  // Штурм: провинция государства, с которым войско в войне.
  if (p->owner != L && w.relation(L, p->owner).s == RelStatus::War) return sideArmyIn(w, *fs, province, p->owner, L, army);
  return 0;
}

// ================================================================ захват
CaptureOptions captureOptions(const World& w, Id army, Id province) {
  CaptureOptions o;
  const Army* a = w.army(army);
  const Province* p = w.province(province);
  if (!a || !p) {
    for (auto& s : o.why) s = "Войско или провинция не найдены";
    return o;
  }
  auto c = calc(w);
  const ProvinceCalc* pc = c->province(province);
  const double V = pc ? pc->tradeValue : 0;
  o.population = pc ? pc->population : provincePop(*p);
  o.plunderGold = V * (double(o.population) / schema::kPlunderDivisor);
  o.razeGold = 2 * o.plunderGold;
  // Захват — после победы над гарнизоном (или без гарнизона): те же условия, что у штурма (война с владельцем, войско у
  // провинции, в ней нет войск владельца); свою провинцию не захватывают.
  std::string why = assaultProblem(w, army, province, std::nullopt, false);
  if (why.empty() && entriesTotal(p->garrison) > 0) why = "Сначала разбейте гарнизон провинции " + provName(w, province);
  if (!why.empty()) {
    for (auto& s : o.why) s = why;
    return o;
  }
  const bool plundered = hasModKey(w, p->modifiers, schema::mod::Plundered), ravaged = hasModKey(w, p->modifiers, schema::mod::Ravaged);
  for (int i = 0; i < int(Capture::Count); i++) o.can[i] = true;
  if (p->occupied && p->occupier == a->leader()) {
    o.can[int(Capture::Occupy)] = o.can[int(Capture::Plunder)] = false;
    o.why[int(Capture::Occupy)] = o.why[int(Capture::Plunder)] = "Провинция уже оккупирована";
  }
  if (plundered || ravaged) {
    o.can[int(Capture::Plunder)] = false;
    o.why[int(Capture::Plunder)] = ravaged ? "Провинция разорена — разграбить её снова нельзя" : "Провинция разграблена — разграбить её снова нельзя";
  }
  if (ravaged) {
    o.can[int(Capture::Raze)] = false;
    o.why[int(Capture::Raze)] = "Провинция уже разорена";
  }
  return o;
}

void capture(Tx& tx, Id army, Id province, Capture how, int slavesPct) {
  const Army& a = needArmy(tx.w(), army);
  const Province& p0 = needProvince(tx.w(), province);
  if (int(how) < 0 || how >= Capture::Count) fail("Неизвестный способ захвата");
  CaptureOptions o = captureOptions(tx.w(), army, province);
  if (!o.can[int(how)]) fail(o.why[int(how)]);
  const Id L = a.leader(), owner = p0.owner;
  const std::string pn = provName(tx.w(), province);
  // Последняя провинция побеждённого: опустошив или разорив её, победитель забирает его запасы (RULES.md §25).
  const bool lastLand = owner && !p0.sea && landOf(tx.w(), owner) == 1;
  // Герои гарнизона без отрядов покидают захваченную провинцию (остаются героями своего государства).
  if (!p0.garrisonHeroes.empty()) {
    std::vector<std::string> names;
    for (Id h : p0.garrisonHeroes) names.push_back(q(tx.w().characterName(h)));
    tx.province(province).garrisonHeroes.clear();
    addLog(tx, LogKind::War, "Герои гарнизона провинции " + pn + " покинули её: " + join(names, ", "), LogRefs{province, army, {L, owner}});
  }
  switch (how) {
    case Capture::Occupy: {
      setOccupied(tx, province, L);
      return;
    }
    case Capture::Plunder: {
      setOccupied(tx, province, L);
      if (o.plunderGold > 0) addStock(tx.faction(L), kGold, o.plunderGold);
      addModifier(tx, ModTarget::Province, province, ensureBuiltinMod(tx, schema::mod::Plundered), schema::kCaptureModTurns);
      shiftRelation(tx, L, owner, schema::kPlunderRelation);
      addLog(tx, LogKind::War, facName(tx.w(), L) + " разграбляет провинцию " + pn + ": " + amount(o.plunderGold) + " тыс. золота", LogRefs{province, army, {L, owner}});
      return;
    }
    case Capture::Raze: {
      if (o.razeGold > 0) addStock(tx.faction(L), kGold, o.razeGold);
      addModifier(tx, ModTarget::Province, province, ensureBuiltinMod(tx, schema::mod::Ravaged), schema::kCaptureModTurns);
      shiftRelation(tx, L, owner, schema::kRazeRelation);
      // Войско отступает в соседнюю свою или оккупированную провинцию (если такой нет — остаётся).
      std::optional<Vec2> to;
      double best = 1e300;
      for (Id nb : neighborsOf(tx, province)) {
        const Province* q2 = tx.w().province(nb);
        if (!q2 || q2->sea || !(q2->owner == L || (q2->occupied && q2->occupier == L))) continue;
        auto lab = provinceLabel(tx, nb);
        if (!lab) continue;
        double d = dist2(*lab, a.pos);
        if (d < best) {
          best = d;
          to = lab;
        }
      }
      std::string moved;
      if (to)
        if (auto spot = Placement(tx.w(), facesFor(tx)).freeSpot(ArmyKind::Army, *to, army)) {
          tx.army(army).pos = *spot;
          moved = ", войско отступило в провинцию " + provName(tx.w(), provinceAtTx(tx, *spot));
        }
      const std::string loot = lastLand ? conquestLoot(tx, owner, L) : std::string();
      addLog(tx, LogKind::War,
             facName(tx.w(), L) + " разоряет провинцию " + pn + ": " + amount(o.razeGold) + " тыс. золота" + moved + (loot.empty() ? std::string() : ". " + loot),
             LogRefs{province, army, {L, owner}});
      return;
    }
    case Capture::Devastate: {
      if (slavesPct < 0 || slavesPct > schema::kDevastateMaxSlavesPct) fail("В рабы — от 0 до " + std::to_string(schema::kDevastateMaxSlavesPct) + " % населения");
      // Рабы — по расам населения пропорционально; остальное население погибает.
      const i64 pop = provincePop(p0);
      const i64 slaves = i64(std::floor(double(pop) * slavesPct / 100.0));
      std::vector<i64> weights;
      for (const RacePop& r : p0.races) weights.push_back(std::max<i64>(0, r.pop));
      std::vector<i64> part = splitProportional(slaves, weights);
      if (slaves > 0) {
        std::vector<SlaveGroup>& pool = tx.faction(L).slaves;
        for (size_t i = 0; i < p0.races.size(); i++) {
          if (part[i] <= 0) continue;
          auto it = std::find_if(pool.begin(), pool.end(), [&](const SlaveGroup& g) { return g.race == p0.races[i].race; });
          if (it != pool.end()) it->count = std::min(kMaxCount, it->count + part[i]);
          else pool.push_back(SlaveGroup{p0.races[i].race, part[i], 0});
        }
      }
      // Постройки уничтожаются (уплаченное за начатое — плательщику), модификаторы снимаются, владельца нет.
      const std::vector<ProvBuilding> builtAll = p0.buildings;
      tx.province(province).buildings.clear();
      for (const ProvBuilding& pb : builtAll)
        if (pb.constructing) refundPaid(tx, pb);
      if (owner) setProvinceOwner(tx, province, 0);
      setOccupied(tx, province, 0);
      {
        Province& m = tx.province(province);
        for (RacePop& r : m.races) r.pop = 0;
        m.buildings.clear();
        m.garrison.clear();
        m.slaves.clear();
        m.modifiers.clear();
        m.modTurns.clear();
        m.lord = 0;
      }
      for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) { return f.capital == province; })) tx.faction(fid).capital = 0;
      addModifier(tx, ModTarget::Province, province, ensureBuiltinMod(tx, schema::mod::Devastated), schema::kCaptureModTurns);
      // Войску без «Армия нежити», «Армия демонов», «Безжалостная армия» — неправедное деяние (и мучения совести).
      std::string guilt;
      const std::vector<Id> mods = tx.w().army(army)->modifiers;
      if (!hasModKey(tx.w(), mods, schema::mod::UndeadArmy) && !hasModKey(tx.w(), mods, schema::mod::DemonArmy) &&
          !hasModKey(tx.w(), mods, schema::mod::Ruthless)) {
        addModifier(tx, ModTarget::Army, army, ensureBuiltinMod(tx, schema::mod::Unrighteous), schema::kCaptureModTurns);
        guilt = ". Войску — «Неправедное деяние»";
        if (slavesPct < schema::kConscienceBelowPct) {
          addModifier(tx, ModTarget::Army, army, ensureBuiltinMod(tx, schema::mod::Conscience), schema::kCaptureModTurns);
          guilt += " и «Мучения совести»";
        }
      }
      if (owner) shiftRelation(tx, L, owner, schema::kDevastateRelation);
      const std::string loot = lastLand ? conquestLoot(tx, owner, L) : std::string();
      addLog(tx, LogKind::War,
             facName(tx.w(), L) + " опустошает провинцию " + pn + ": в рабство — " + fmtInt(slaves) + ", погибли — " + fmtInt(pop - slaves) + guilt +
                 (loot.empty() ? std::string() : ". " + loot),
             LogRefs{province, army, owner ? std::vector<Id>{L, owner} : std::vector<Id>{L}});
      return;
    }
    default: return;
  }
}

}  // namespace rg::rules
