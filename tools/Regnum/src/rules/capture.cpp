// Regnum — штурм гарнизона провинции и захват (ТЗ «Механика войн», п.4–5; «Модификаторы», 1.1–1.3, 1.16–1.17):
// захватить (оккупация), захватить и разграбить, разорить, опустошить.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

constexpr i64 kMaxCount = 1000000000000000LL;

i64 provincePop(const Province& p) {
  i64 n = 0;
  for (const RacePop& r : p.races) n += std::max<i64>(0, r.pop);
  return n;
}

i64 unitsOf(const Army& a) {
  i64 n = 0;
  for (const ArmyGroup& g : a.groups)
    for (const ArmyUnit& u : g.units) n += u.count;
  return n;
}

}  // namespace

bool canSiege(const World& w, Id army, Id province, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Army* a = w.army(army);
  if (!a) return no("Войско не найдено");
  if (a->isFleet()) return no("Флот не штурмует провинции");
  if (a->allied()) return no("Союзное войско не штурмует провинции — сначала распустите союз");
  const Province* p = w.province(province);
  if (!p) return no("Провинция не найдена");
  if (p->sea) return no("Морскую провинцию нельзя захватить");
  if (!p->owner) return no("У провинции нет владельца");
  const Id L = a->leader();
  if (p->owner == L) return no("Это провинция самого государства");
  if (p->occupied && p->occupier == L) return no("Провинция уже оккупирована " + facName(w, L));
  if (w.relation(L, p->owner).s != RelStatus::War) return no(facName(w, L) + " и " + facName(w, p->owner) + " не в войне");
  return true;
}

BattleOutcome resolveSiege(Tx& tx, const SiegeResult& r) {
  BattleOutcome out;
  std::string why;
  if (!canSiege(tx.w(), r.attacker, r.province, &why)) fail(why);
  const Army A = *tx.w().army(r.attacker);
  const Province P = *tx.w().province(r.province);
  const Id owner = P.owner, L = A.leader();
  const Faction& O = needState(tx.w(), owner);
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
  i64 garrisonLeft = 0;
  for (const GarrisonEntry& g : P.garrison) garrisonLeft += g.count;
  for (auto& [row, n] : r.garrisonLosses) {
    if (n < 0) fail("Потери не могут быть отрицательными");
    i64 have = 0;
    for (const GarrisonEntry& g : P.garrison)
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
    for (ArmyRow& ar : tx.faction(faction).army)
      if (ar.id == row) ar.total = std::max<i64>(0, ar.total - n);
  }
  // Потери гарнизона.
  for (auto& [row, n] : r.garrisonLosses) {
    if (n <= 0) continue;
    lossG += n;
    if (const ArmyRow* ar = O.armyRow(row); ar && unitRace(tx.w(), owner, *ar) == schema::kRaceLiving) livingG += n;
    for (GarrisonEntry& g : tx.province(r.province).garrison)
      if (g.row == row) g.count -= n;
    for (ArmyRow& ar : tx.faction(owner).army)
      if (ar.id == row) ar.total = std::max<i64>(0, ar.total - n);
  }
  auto& gs = tx.province(r.province).garrison;
  gs.erase(std::remove_if(gs.begin(), gs.end(), [](const GarrisonEntry& g) { return g.count <= 0; }), gs.end());

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
  std::string text = "Штурм провинции " + provName(tx.w(), r.province) + ": " + an + " против гарнизона " + facName(tx.w(), owner);
  const bool anyLeft = attackerLeft > 0 || garrisonLeft > 0;
  if (anyLeft) {
    out.winner = r.attackerWins ? L : owner;
    out.loser = r.attackerWins ? owner : L;
  }
  if (anyLeft && r.attackerWins) {
    // Гарнизон разбит: уцелевшие возвращаются в резерв владельца.
    tx.province(r.province).garrison.clear();
    out.winnerArmy = tx.w().army(r.attacker) ? r.attacker : 0;
    std::vector<Id> heroes;
    for (const ArmyGroup& g : A.groups)
      for (Id h : g.heroes) heroes.push_back(h);
    out.corpses = battleCorpses(tx, L, heroes, livingG);
    text += ". Гарнизон разбит";
  } else if (anyLeft) {
    // Гарнизон устоял: нападающий отходит на исходную позицию (или рядом).
    if (tx.w().army(r.attacker)) {
      Vec2 to = std::isfinite(r.attackerOrigin.x) && std::isfinite(r.attackerOrigin.y) ? r.attackerOrigin : A.pos;
      auto spot = Placement(tx.w(), facesFor(tx)).freeSpot(ArmyKind::Army, to, r.attacker);
      if (spot) tx.army(r.attacker).pos = *spot;
    }
    out.corpses = battleCorpses(tx, owner, {}, livingA);
    text += ". Гарнизон устоял";
  }
  text += ". Потери: " + fmtInt(lossA) + " и " + fmtInt(lossG);
  if (!out.destroyed.empty()) text += ". Войско уничтожено";
  if (out.corpses > 0) text += ". Трупов: " + fmtInt(i64(out.corpses));
  addLog(tx, LogKind::Battle, text, LogRefs{r.province, r.attacker, {L, owner}});
  return out;
}

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
  if (!p->owner) {
    for (int i = 0; i < int(Capture::Count); i++) {
      o.can[i] = false;
      o.why[i] = "У провинции нет владельца";
    }
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
      addLog(tx, LogKind::War, facName(tx.w(), L) + " разграбляет провинцию " + pn + ": " + amount(o.plunderGold) + " золота", LogRefs{province, army, {L, owner}});
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
      addLog(tx, LogKind::War, facName(tx.w(), L) + " разоряет провинцию " + pn + ": " + amount(o.razeGold) + " золота" + moved, LogRefs{province, army, {L, owner}});
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
      addLog(tx, LogKind::War,
             facName(tx.w(), L) + " опустошает провинцию " + pn + ": в рабство — " + fmtInt(slaves) + ", погибли — " + fmtInt(pop - slaves) + guilt,
             LogRefs{province, army, owner ? std::vector<Id>{L, owner} : std::vector<Id>{L}});
      return;
    }
    default: return;
  }
}

}  // namespace rg::rules
