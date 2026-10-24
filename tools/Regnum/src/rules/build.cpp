// Regnum — строительство: варианты с ценой и причинами, начало, отмена с возвратом, снос;
// требования к постройкам и технологиям общего и уникального дерева, культовые постройки, мгновенное завершение и
// изначальные постройки, постройки преобразования.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

// Стоимость уровня (1…) с множителем провинции; только положительные позиции.
std::map<Id, double> levelCost(const Building& b, int level, double factor) {
  std::map<Id, double> r;
  if (level < 1 || level > int(b.levels.size())) return r;
  for (auto& [res, v] : b.levels[size_t(level - 1)].cost) {
    double x = v * factor;
    if (std::isfinite(x) && x > 0) r[res] = x;
  }
  return r;
}

// Цена уровня в эссенциях с множителем провинции.
std::map<Id, double> levelEssCost(const Building& b, int level, double factor) {
  std::map<Id, double> r;
  if (level < 1 || level > int(b.levels.size())) return r;
  for (auto& [e, v] : b.levels[size_t(level - 1)].essCost) {
    double x = v * factor;
    if (std::isfinite(x) && x > 0) r[e] = x;
  }
  return r;
}

}  // namespace

namespace detail {
Id refundPaid(Tx& tx, const ProvBuilding& pb) {
  if (!pb.constructing || !pb.payer || !tx.w().faction(pb.payer)) return 0;
  bool any = false;
  for (auto& [res, v] : pb.paid) any = any || (std::isfinite(v) && v > 0);
  for (auto& [e, v] : pb.paidEss) any = any || (std::isfinite(v) && v > 0);
  if (!any) return 0;
  Faction& f = tx.faction(pb.payer);
  for (auto& [res, v] : pb.paid)
    if (std::isfinite(v) && v > 0) addStock(f, res, v);
  for (auto& [e, v] : pb.paidEss)
    if (std::isfinite(v) && v > 0 && tx.w().essence(e)) f.ess[e] += v;
  return pb.payer;
}

void releaseBuildingRelics(Tx& tx, Id state, const ProvBuilding& pb) {
  if (pb.relics.empty()) return;
  const Faction* f = tx.w().faction(state);
  if (!f || !f->isState()) return;   // без государства-владельца реликвии становятся свободными
  for (const RelicStack& st : pb.relics)
    if (tx.w().relic(st.relic) && st.count > 0) moveRelic(tx, st.relic, RelicPlace{}, RelicPlace{RelicPlace::State, state, 0, state}, st.count);
}

bool refundCycle(Tx& tx, Id state, const ProvBuilding& pb) {
  if (pb.cycle <= 0) return false;
  const Building* b = tx.w().building(pb.building);
  const Recipe* rc = b && b->convert ? activeRecipe(*b, pb) : nullptr;
  if (!rc || !tx.w().faction(state)) return true;   // вход вернуть некому (или рецепта уже нет)
  // Вход цикла списан при его начале: рецепт × множитель, не больше открытого государству (как в расчёте хода).
  int mult = std::clamp(pb.mult, 1, schema::kMaxProductionMult);
  if (mult > 1) mult = std::min(mult, maxProductionMult(tx.w(), state));
  Faction& f = tx.faction(state);
  for (const ResAmount& x : rc->in)
    if (x.amount > 0 && tx.w().resource(x.res)) addStock(f, x.res, x.amount * mult);
  for (const ResAmount& x : rc->essIn)
    if (x.amount > 0 && tx.w().essence(x.res)) {
      double& v = f.ess[x.res];
      v += x.amount * mult;
      if (std::fabs(v) < 1e-9) f.ess.erase(x.res);
    }
  return true;
}
}  // namespace detail

namespace {

const ProvBuilding* findPb(const Province& p, Id building) {
  for (const ProvBuilding& pb : p.buildings)
    if (pb.building == building) return &pb;
  return nullptr;
}

const Faction* ownerState(const World& w, const Province& p) {
  const Faction* f = w.faction(p.owner);
  return f && f->isState() ? f : nullptr;
}

// Доступна ли постройка провинции вообще: общее дерево или уникальная постройка владельца.
bool inTree(const Building& b, const Province& p) { return b.owner == 0 || b.owner == p.owner; }

// Причины, по которым постройку нельзя поставить в провинции (кроме цены): место, владелец, слоты, требования к
// постройкам и технологиям, культовая — одна на карту (общая) или на государство (уникальная).
void placeChecks(const World& w, const Province& p, const Building& b, const ProvBuilding* pb, int slots, std::vector<std::string>& r) {
  const Faction* owner = ownerState(w, p);
  if (p.sea) r.push_back("В морской провинции нельзя строить");
  else if (!owner) r.push_back("У провинции нет владельца");
  // В пустоши нежити строит только государство нежити, в осквернённой — демонов.
  if (owner && hasModKey(w, p.modifiers, schema::mod::UndeadWaste) && owner->stateKind != StateKind::Undead)
    r.push_back("В пустоши нежити строит только государство нежити");
  if (owner && hasModKey(w, p.modifiers, schema::mod::Desecrated) && owner->stateKind != StateKind::Demonic)
    r.push_back("В осквернённой провинции строит только государство демонов");
  // Владелец не строит в провинции, которую оккупировал противник.
  if (owner && !p.sea)
    if (const Id occ = occupierOf(w, p)) r.push_back("Провинция оккупирована: " + facName(w, occ) + " — строить нельзя");
  if (b.levels.empty()) r.push_back("У постройки нет уровней");
  // Только для видов государства (мясопилка — государству нежити).
  if (owner && b.stateKinds && !((b.stateKinds >> unsigned(owner->stateKind)) & 1u)) {
    std::vector<std::string> kinds;
    for (int k = 0; k < int(StateKind::Count); k++)
      if ((b.stateKinds >> unsigned(k)) & 1u) kinds.push_back(schema::stateKind(StateKind(k)).name);
    r.push_back("Только для вида государства: " + join(kinds, ", "));
  }
  // Постройка добычи: нужен ресурс провинции из её групп (кроме необязательной добычи — ферма).
  if (b.extract && !b.extractOptional && !extractAllowed(w, b, p.resource))
    r.push_back(p.resource && w.resource(p.resource) ? "Ресурс провинции «" + resName(w, p.resource) + "» не добывается этой постройкой"
                                                     : "Нужен ресурс провинции, который добывает эта постройка");
  if (!pb && int(p.buildings.size()) >= slots)
    r.push_back("Нет свободных слотов: занято " + std::to_string(p.buildings.size()) + " из " + std::to_string(slots));
  for (const BuildingReq& rq : b.requires_) {
    const Building* rb = w.building(rq.building);
    if (!rb || rq.building == b.id) continue;
    // Культовая постройка (одна на карту или на государство) требуется в любой провинции государства-владельца
    // («Штаб Контрразведки» — «Штаб Разведки» 4 уровня), остальные — в этой провинции.
    bool ok = false;
    if (rb->cat == BuildingCat::Cult && owner)
      w.provinces.each([&](const Province& q) {
        const ProvBuilding* x = q.owner == owner->id && !q.sea ? findPb(q, rq.building) : nullptr;
        ok = ok || (x && x->builtLevel() >= rq.level);
      });
    else if (const ProvBuilding* have = findPb(p, rq.building)) ok = have->builtLevel() >= rq.level;
    if (!ok)
      r.push_back("Нужна постройка " + buildingName(w, rq.building) + (rq.level > 1 ? " уровня " + std::to_string(rq.level) : "") +
                  (rb->cat == BuildingCat::Cult ? " в государстве" : ""));
  }
  for (Id t : b.techs)
    if (w.tech(t) && (!owner || !techStudied(w, t, owner->id))) r.push_back("Нужна технология " + techName(w, t));
  // Порт, верфь и другие приморские постройки.
  if (b.coastal && !p.sea && !isCoastal(w, p.id)) r.push_back("Только в приморской провинции (граничит с морем)");
  // Требования «на государство» (соборы): на каждую новую — ещё per таких построек.
  if (owner && !pb && !b.stateReqs.empty()) {
    int mine = 0;
    w.provinces.each([&](const Province& q) {
      if (q.owner == owner->id && !q.sea && findPb(q, b.id)) mine++;
    });
    for (const StateReq& sr : b.stateReqs) {
      if (!w.building(sr.building) || sr.per <= 0) continue;
      int have = 0;
      w.provinces.each([&](const Province& q) {
        if (q.owner != owner->id || q.sea) return;
        const ProvBuilding* x = findPb(q, sr.building);
        if (x && x->builtLevel() >= 1) have++;
      });
      const int need = sr.per * (mine + 1);
      if (have < need)
        r.push_back("Нужно " + std::to_string(need) + " построек " + buildingName(w, sr.building) + " в государстве: достроено " + std::to_string(have));
    }
  }
  if (b.cat == BuildingCat::Cult && !pb)
    if (Id other = cultBuiltIn(w, b.id, p.id))
      r.push_back(std::string(b.owner ? "Культовая постройка — одна на государство: уже есть в провинции "
                                      : "Культовая постройка — одна на всю карту: уже есть в провинции ") +
                  provName(w, other));
}

BuildOption optionFor(const World& w, const Province& p, const Building& b, double factor, int slots, bool player) {
  BuildOption o;
  o.building = b.id;
  const int maxLvl = int(b.levels.size());
  const ProvBuilding* pb = findPb(p, b.id);
  if (pb && pb->constructing) {
    o.level = pb->level;
    o.upgrade = pb->level > 1;
    o.turns = pb->left;
    o.cost = levelCost(b, o.level, factor);
    o.reasons.push_back("Уже строится: осталось " + nTurns(pb->left));
    return o;
  }
  if (pb && pb->level >= maxLvl) {
    o.level = pb->level;
    o.reasons.push_back("Достигнут наибольший уровень");
    o.placeReasons = o.reasons;
    return o;
  }
  o.level = pb ? pb->level + 1 : 1;
  o.upgrade = pb != nullptr;
  o.turns = maxLvl ? std::max(1, b.levels[size_t(o.level - 1)].turns) : 1;
  o.cost = levelCost(b, o.level, factor);
  o.essCost = levelEssCost(b, o.level, factor);
  const Faction* owner = ownerState(w, p);
  placeChecks(w, p, b, pb, slots, o.reasons);
  o.placeReasons = o.reasons;
  o.canPlace = o.placeReasons.empty();
  // В провинции одновременно строится только одна постройка; игрок начинает в провинции не больше одной за ход.
  for (const ProvBuilding& x : p.buildings)
    if (x.constructing && x.building != b.id) {
      o.reasons.push_back("В провинции уже строится " + buildingName(w, x.building) + ": одновременно — одна постройка");
      break;
    }
  if (player && p.buildTurn == w.turn()) o.reasons.push_back("В этом ходу в провинции уже начато строительство");
  if (owner) {
    for (auto& [res, need] : o.cost) {
      double have = owner->stock(res);
      if (have + 1e-9 < need)
        o.reasons.push_back("Недостаточно ресурса «" + resName(w, res) + "»: нужно " + amountOf(res, need) + ", есть " + amountOf(res, std::max(0.0, have)));
    }
    for (auto& [e, need] : o.essCost) {
      double have = owner->essence(e);
      if (have + 1e-9 < need) {
        const CatalogItem* ci = w.essence(e);
        o.reasons.push_back("Недостаточно эссенции «" + (ci ? ci->name : std::string("?")) + "»: нужно " + amount(need) + ", есть " +
                            amount(std::max(0.0, have)));
      }
    }
  }
  o.can = o.reasons.empty();
  return o;
}

struct ProvCtx {
  double factor;
  int slots;
};
ProvCtx ctxOf(const World& w, const Province& p) {
  Effects fx = provinceFx(w, SourceIndex(w), p);
  return ProvCtx{costFactorOf(fx), slotsOf(p, fx)};
}

}  // namespace

std::vector<BuildOption> buildOptions(const World& w, Id province, bool player) {
  std::vector<BuildOption> out;
  const Province* p = w.province(province);
  if (!p) return out;
  ProvCtx c = ctxOf(w, *p);
  std::vector<const Building*> list;
  w.buildings.each([&](const Building& b) { if (inTree(b, *p)) list.push_back(&b); });
  std::stable_sort(list.begin(), list.end(), [](const Building* a, const Building* b) {
    if (a->cat != b->cat) return a->cat < b->cat;
    int r = compareRu(a->name, b->name);
    if (r != 0) return r < 0;
    return a->id < b->id;
  });
  out.reserve(list.size());
  for (const Building* b : list) out.push_back(optionFor(w, *p, *b, c.factor, c.slots, player));
  return out;
}

void startBuilding(Tx& tx, Id province, Id building, bool player) {
  const Province& p = needProvince(tx.w(), province);
  const Building& b = needBuilding(tx.w(), building);
  if (!inTree(b, p)) fail(buildingName(tx.w(), building) + " — уникальная постройка другого государства");
  ProvCtx c = ctxOf(tx.w(), p);
  BuildOption o = optionFor(tx.w(), p, b, c.factor, c.slots, player);
  if (!o.can) fail(o.reasons.front());
  const Id owner = p.owner;
  Faction& f = tx.faction(owner);
  for (auto& [res, v] : o.cost) addStock(f, res, -v);
  for (auto& [e, v] : o.essCost) f.ess[e] -= v;
  Province& m = tx.province(province);
  m.buildTurn = tx.w().turn();
  if (o.upgrade) {
    for (ProvBuilding& pb : m.buildings)
      if (pb.building == building) {
        pb.level = o.level;
        pb.constructing = true;
        pb.left = o.turns;
        pb.paid = o.cost;
        pb.paidEss = o.essCost;
        pb.payer = owner;
      }
  } else {
    ProvBuilding nb{building, 1, true, o.turns, o.cost, owner};
    nb.paidEss = o.essCost;
    m.buildings.push_back(std::move(nb));
  }
  addLog(tx, LogKind::Build,
         "Начато строительство " + buildingName(tx.w(), building) + (o.upgrade ? " (уровень " + std::to_string(o.level) + ")" : std::string()) +
             " в провинции " + provName(tx.w(), province) + ": " + nTurns(o.turns),
         LogRefs{province, 0, {owner}});
}

void cancelBuilding(Tx& tx, Id province, Id building) {
  const Province& p = needProvince(tx.w(), province);
  needBuilding(tx.w(), building);
  const ProvBuilding* found = findPb(p, building);
  if (!found) fail("Постройки " + buildingName(tx.w(), building) + " нет в провинции");
  if (!found->constructing) fail(buildingName(tx.w(), building) + " не строится");
  const ProvBuilding pb = *found;
  const int level = pb.level;
  const Id owner = p.owner;
  Province& m = tx.province(province);
  if (level > 1) {
    for (ProvBuilding& x : m.buildings)
      if (x.building == building) {
        x.level = level - 1;
        x.constructing = false;
        x.left = 0;
        x.paid.clear();
        x.paidEss.clear();
        x.payer = 0;
      }
  } else {
    m.buildings.erase(std::remove_if(m.buildings.begin(), m.buildings.end(), [&](const ProvBuilding& x) { return x.building == building; }),
                      m.buildings.end());
    releaseBuildingRelics(tx, owner, pb);
  }
  // Возврат — ровно уплаченное и тому, кто платил (провинция могла сменить владельца, цена — измениться).
  const Id got = refundPaid(tx, pb);
  std::string text = "Отменено строительство " + buildingName(tx.w(), building) + " в провинции " + provName(tx.w(), province);
  if (got) text += got == owner ? ", стоимость возвращена" : ", стоимость возвращена " + facName(tx.w(), got);
  LogRefs refs{province, 0, {}};
  if (owner) refs.factions.push_back(owner);
  if (got && got != owner) refs.factions.push_back(got);
  addLog(tx, LogKind::Build, text, refs);
}

// ================================================================ слоты
namespace {

// Провинции с постройками: слоты и лишние постройки (с конца списка). only — только эти провинции (пусто — все).
// cityFrom — мир, по населению которого берётся тип города, если он больше (уменьшение населения постройки не сносит).
std::vector<SlotLoss> excessIn(const World& w, const std::vector<Id>* only, const World* cityFrom = nullptr) {
  std::vector<SlotLoss> out;
  SourceIndex si(w);
  auto check = [&](const Province& p) {
    if (p.sea || p.buildings.empty()) return;
    int slots = slotsOf(p, provinceFx(w, si, p));
    if (cityFrom)
      if (const Province* q = cityFrom->province(p.id)) {
        const int was = schema::cityType(cityType(*q)).value, now = schema::cityType(cityType(p)).value;
        if (was > now) slots += was - now;
      }
    const int used = int(p.buildings.size());
    if (used <= slots) return;
    SlotLoss l;
    l.province = p.id;
    l.slots = slots;
    l.used = used;
    for (int i = used - 1; i >= slots; i--) l.buildings.push_back(p.buildings[size_t(i)].building);
    out.push_back(std::move(l));
  };
  if (only) {
    for (Id pid : *only)
      if (const Province* p = w.province(pid)) check(*p);
  } else {
    w.provinces.each(check);
  }
  return out;
}

// Все провинции, где построек больше слотов.
std::vector<SlotLoss> excessBuildings(const World& w) { return excessIn(w, nullptr); }

}  // namespace

std::vector<SlotLoss> slotLosses(const World& before, const World& after) {
  // Провинции, где лишних построек (сверх слотов) стало больше, чем до изменения. Сносится только прибавка лишних —
  // не больше, чем потеряно слотов: постройки, законно оставшиеся сверх слотов раньше (тип города упал вместе с
  // населением), остаются. Тип города, упавший вместе с населением, слотов не отнимает (новые — только в свободные).
  std::vector<SlotLoss> now = excessIn(after, nullptr, &before), out;
  if (now.empty()) return out;
  std::vector<Id> ids;
  for (const SlotLoss& l : now) ids.push_back(l.province);
  std::vector<SlotLoss> was = excessIn(before, &ids);
  for (SlotLoss& l : now) {
    auto it = std::find_if(was.begin(), was.end(), [&](const SlotLoss& x) { return x.province == l.province; });
    const int wasExcess = it == was.end() ? 0 : int(it->buildings.size()), nowExcess = int(l.buildings.size());
    if (nowExcess <= wasExcess) continue;
    const int gone = nowExcess - wasExcess;
    l.slots = l.used - gone;                 // остаются все постройки, кроме последних gone
    l.buildings.resize(size_t(gone));        // список — с конца
    out.push_back(std::move(l));
  }
  return out;
}

void trimExcessBuildings(Tx& tx, const std::vector<SlotLoss>* only) {
  std::vector<SlotLoss> all = excessBuildings(tx.w());
  for (SlotLoss& l : all) {
    if (only) {
      auto it = std::find_if(only->begin(), only->end(), [&](const SlotLoss& x) { return x.province == l.province; });
      if (it == only->end()) continue;
      l.slots = std::max(l.slots, it->slots);   // сносится не больше, чем было показано в предупреждении
      if (l.slots >= l.used) continue;
    }
    const Province& p = *tx.w().province(l.province);
    std::vector<ProvBuilding> gone(p.buildings.begin() + l.slots, p.buildings.end());
    const Id owner = p.owner;
    tx.province(l.province).buildings.resize(size_t(l.slots));
    std::vector<std::string> names;
    for (const ProvBuilding& pb : gone) {
      names.push_back(buildingName(tx.w(), pb.building));
      if (pb.constructing) refundPaid(tx, pb);
      refundCycle(tx, owner, pb);   // снос посреди цикла преобразования: вход — владельцу
      releaseBuildingRelics(tx, owner, pb);
    }
    addLog(tx, LogKind::Build,
           "Провинция " + provName(tx.w(), l.province) + " лишилась слотов (" + std::to_string(l.slots) + " из " + std::to_string(l.used) +
               "): снесены " + join(names, ", "),
           LogRefs{l.province, 0, owner ? std::vector<Id>{owner} : std::vector<Id>{}});
  }
}

// ================================================================ мгновенно и изначально
void completeBuilding(Tx& tx, Id province, Id building) {
  const Province& p = needProvince(tx.w(), province);
  needBuilding(tx.w(), building);
  const ProvBuilding* pb = findPb(p, building);
  if (!pb) fail("Постройки " + buildingName(tx.w(), building) + " нет в провинции");
  if (!pb->constructing) fail(buildingName(tx.w(), building) + " не строится");
  const int lvl = pb->level;
  const Id owner = p.owner;
  for (ProvBuilding& x : tx.province(province).buildings)
    if (x.building == building) {
      x.constructing = false;
      x.left = 0;
      x.paid.clear();
      x.paidEss.clear();
      x.payer = 0;
    }
  addLog(tx, LogKind::Build,
         "Достроено сразу: " + buildingName(tx.w(), building) + (lvl > 1 ? " (уровень " + std::to_string(lvl) + ")" : std::string()) +
             " в провинции " + provName(tx.w(), province),
         LogRefs{province, 0, owner ? std::vector<Id>{owner} : std::vector<Id>{}});
}

void placeBuilding(Tx& tx, Id province, Id building, int level) {
  const Province& p = needProvince(tx.w(), province);
  const Building& b = needBuilding(tx.w(), building);
  if (!inTree(b, p)) fail(buildingName(tx.w(), building) + " — уникальная постройка другого государства");
  if (level < 1 || level > int(b.levels.size())) fail("У постройки нет уровня " + std::to_string(level));
  const ProvBuilding* pb = findPb(p, building);
  if (pb && pb->constructing) fail(buildingName(tx.w(), building) + " строится — завершите или отмените строительство");
  if (pb && pb->level == level) return;
  ProvCtx c = ctxOf(tx.w(), p);
  std::vector<std::string> why;
  placeChecks(tx.w(), p, b, pb, c.slots, why);
  if (!why.empty()) fail(why.front());
  Province& m = tx.province(province);
  if (pb) {
    for (ProvBuilding& x : m.buildings)
      if (x.building == building) x.level = level;
  } else {
    ProvBuilding nb;
    nb.building = building;
    nb.level = level;
    m.buildings.push_back(nb);
  }
  addLog(tx, LogKind::Build,
         "Поставлена постройка " + buildingName(tx.w(), building) + (level > 1 ? " (уровень " + std::to_string(level) + ")" : std::string()) +
             " в провинции " + provName(tx.w(), province),
         LogRefs{province, 0, p.owner ? std::vector<Id>{p.owner} : std::vector<Id>{}});
}

void setConvertIdle(Tx& tx, Id province, Id building, bool idle) {
  const Province& p = needProvince(tx.w(), province);
  const Building& b = needBuilding(tx.w(), building);
  if (!b.convert && !b.drain) fail(buildingName(tx.w(), building) + " — не постройка преобразования или иссушения");
  const ProvBuilding* pb = findPb(p, building);
  if (!pb) fail("Постройки " + buildingName(tx.w(), building) + " нет в провинции");
  if (pb->idle == idle) return;
  for (ProvBuilding& x : tx.province(province).buildings)
    if (x.building == building) x.idle = idle;
}

void setPbRecipe(Tx& tx, Id province, Id building, int recipe) {
  const Province& p = needProvince(tx.w(), province);
  const Building& b = needBuilding(tx.w(), building);
  if (!b.convert) fail(buildingName(tx.w(), building) + " — не постройка преобразования");
  if (recipe < 0 || recipe >= int(b.recipes.size())) fail("У постройки нет такого рецепта");
  const ProvBuilding* pb = findPb(p, building);
  if (!pb) fail("Постройки " + buildingName(tx.w(), building) + " нет в провинции");
  if (pb->recipe == recipe) return;
  if (pb->cycle > 0) fail("Идёт цикл преобразования — рецепт можно сменить после его конца: осталось " + nTurns(pb->cycle));
  for (ProvBuilding& x : tx.province(province).buildings)
    if (x.building == building) x.recipe = recipe;
}

int maxProductionMult(const World& w, Id state) {
  // Технологии ветки по множителю (ступени могут идти не подряд: ×2, ×5, ×10…); множитель ×N доступен, когда изучены
  // все ступени ветки до ×N включительно.
  std::map<int, Id> branch;
  w.techs.each([&](const Tech& t) {
    if (t.faction != 0) return;
    if (int m = schema::industryMultOf(t.key); m >= 2 && m <= schema::kMaxProductionMult) branch.emplace(m, t.id);
  });
  int best = 1;
  for (auto& [m, id] : branch) {
    if (!techStudied(w, id, state)) break;
    best = m;
  }
  return best;
}

void setPbMult(Tx& tx, Id province, Id building, int mult) {
  const Province& p = needProvince(tx.w(), province);
  const Building& b = needBuilding(tx.w(), building);
  if (!b.convert) fail(buildingName(tx.w(), building) + " — не постройка преобразования");
  const ProvBuilding* pb = findPb(p, building);
  if (!pb) fail("Постройки " + buildingName(tx.w(), building) + " нет в провинции");
  if (mult < 1 || mult > schema::kMaxProductionMult) fail("Множитель производства — от ×1 до ×" + std::to_string(schema::kMaxProductionMult));
  if (pb->mult == mult) return;
  if (pb->cycle > 0) fail("Идёт цикл преобразования — множитель можно сменить после его конца: осталось " + nTurns(pb->cycle));
  if (mult > 1) {
    const int max = maxProductionMult(tx.w(), p.owner);
    if (mult > max)
      fail(max <= 1 ? std::string("Множитель больше ×1 открывают технологии «Стимуляция промышленности»")
                    : "Государству открыт множитель до ×" + std::to_string(max) + " — следующие открывают технологии «Стимуляция промышленности»");
  }
  for (ProvBuilding& x : tx.province(province).buildings)
    if (x.building == building) x.mult = mult;
}

void setDrainRace(Tx& tx, Id province, Id building, Id race) {
  const Province& p = needProvince(tx.w(), province);
  const Building& b = needBuilding(tx.w(), building);
  if (!b.drain) fail(buildingName(tx.w(), building) + " — не постройка иссушения");
  if (race && !Catalogs::find(tx.w().catalogs->races, race)) fail("Раса не найдена");
  const ProvBuilding* pb = findPb(p, building);
  if (!pb) fail("Постройки " + buildingName(tx.w(), building) + " нет в провинции");
  if (pb->race == race) return;
  for (ProvBuilding& x : tx.province(province).buildings)
    if (x.building == building) x.race = race;
}

bool extractAllowed(const World& w, const Building& b, Id res) {
  if (!b.extract) return true;
  const CatalogItem* r = w.resource(res);
  if (!r || contains(b.extractExclude, res)) return false;
  for (Id g : b.extractGroups)
    if (w.catalogs->inGroup(r->group, g)) return true;
  return false;
}

// ================================================================ требования построек
Id cultBuiltIn(const World& w, Id building, Id exceptProvince) {
  const Building* b = w.building(building);
  if (!b || b->cat != BuildingCat::Cult) return 0;
  return w.provinces.firstId([&](const Province& p) {
    if (p.id == exceptProvince) return false;
    if (b->owner && p.owner != b->owner) return false;   // уникальная — одна у своего государства
    return findPb(p, building) != nullptr;
  });
}

bool canRequireBuilding(const World& w, Id building, Id req, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Building* b = w.building(building);
  const Building* r = w.building(req);
  if (!b || !r) return no("Постройка не найдена");
  if (building == req) return no("Постройка не может требовать саму себя");
  if (r->owner && r->owner != b->owner)
    return no(b->owner ? "Уникальная постройка может требовать общие постройки и постройки своего государства"
                       : "Общая постройка не может зависеть от уникальной постройки государства");
  // Цикл: req (через свои требования) уже зависит от building.
  std::vector<Id> st{req};
  std::vector<Id> seen{req};
  while (!st.empty()) {
    const Building* x = w.building(st.back());
    st.pop_back();
    if (!x) continue;
    for (const BuildingReq& q : x->requires_) {
      if (q.building == building) return no("Связь создаст цикл требований");
      if (!contains(seen, q.building)) {
        seen.push_back(q.building);
        st.push_back(q.building);
      }
    }
  }
  return true;
}

bool canRequireTech(const World& w, Id building, Id tech, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Building* b = w.building(building);
  const Tech* t = w.tech(tech);
  if (!b || !t) return no("Постройка или технология не найдена");
  if (t->faction && t->faction != b->owner)
    return no(b->owner ? "Уникальная постройка может требовать общие технологии и технологии своего государства"
                       : "Общая постройка может требовать только общие технологии");
  return true;
}

void setBuildingReq(Tx& tx, Id building, Id req, int level) {
  needBuilding(tx.w(), building);
  const Building& r = needBuilding(tx.w(), req);
  auto& list = tx.w().building(building)->requires_;
  const bool has = std::any_of(list.begin(), list.end(), [&](const BuildingReq& q) { return q.building == req; });
  if (level <= 0) {
    if (!has) return;
    auto& m = tx.building(building).requires_;
    m.erase(std::remove_if(m.begin(), m.end(), [&](const BuildingReq& q) { return q.building == req; }), m.end());
    return;
  }
  if (level > int(r.levels.size())) fail("У постройки " + buildingName(tx.w(), req) + " нет уровня " + std::to_string(level));
  if (!has) {
    std::string why;
    if (!canRequireBuilding(tx.w(), building, req, &why)) fail(why);
    tx.building(building).requires_.push_back(BuildingReq{req, level});
    return;
  }
  for (BuildingReq& q : tx.building(building).requires_)
    if (q.building == req) q.level = level;
}

void setBuildingTech(Tx& tx, Id building, Id tech, bool on) {
  const Building& b = needBuilding(tx.w(), building);
  needTech(tx.w(), tech);
  const bool has = contains(b.techs, tech);
  if (on == has) return;
  if (on) {
    std::string why;
    if (!canRequireTech(tx.w(), building, tech, &why)) fail(why);
    tx.building(building).techs.push_back(tech);
  } else {
    eraseValue(tx.building(building).techs, tech);
  }
}

std::vector<Id> techUnlocks(const World& w, Id tech) {
  std::vector<Id> out;
  w.buildings.each([&](const Building& b) {
    if (contains(b.techs, tech)) out.push_back(b.id);
  });
  return out;
}

namespace {

void checkRecipe(const World& w, const Recipe& r) {
  if (r.in.size() > 3) fail("На входе — не больше трёх разных ресурсов");
  if (r.essIn.size() > 3) fail("На входе — не больше трёх разных эссенций");
  for (size_t i = 0; i < r.in.size(); i++) {
    if (!w.resource(r.in[i].res)) fail("Ресурс на входе не найден");
    needFinite(r.in[i].amount, "Количество на входе");
    if (r.in[i].amount < 0) fail("Количество на входе не может быть меньше нуля");
    for (size_t k = 0; k < i; k++)
      if (r.in[k].res == r.in[i].res) fail("Ресурсы на входе должны быть разными");
  }
  for (size_t i = 0; i < r.essIn.size(); i++) {
    if (!w.essence(r.essIn[i].res)) fail("Эссенция на входе не найдена");
    needFinite(r.essIn[i].amount, "Эссенция на входе");
    if (r.essIn[i].amount < 0) fail("Эссенция на входе не может быть меньше нуля");
    for (size_t k = 0; k < i; k++)
      if (r.essIn[k].res == r.essIn[i].res) fail("Эссенции на входе должны быть разными");
  }
  if (r.out.size() > 3) fail("На выходе — не больше трёх разных ресурсов");
  for (size_t i = 0; i < r.out.size(); i++) {
    if (r.out[i].res && !w.resource(r.out[i].res)) fail("Ресурс на выходе не найден");
    needFinite(r.out[i].amount, "Количество на выходе");
    if (r.out[i].amount < 0) fail("Количество на выходе не может быть меньше нуля");
    for (size_t k = 0; k < i; k++)
      if (r.out[k].res && r.out[k].res == r.out[i].res) fail("Ресурсы на выходе должны быть разными");
  }
  if (r.turns < 1 || r.turns > 1000) fail("Срок преобразования — от 1 до 1000 ходов");
}

// Выбранные рецепты в провинциях — в пределах списка, идущие циклы не длиннее срока своего рецепта.
void fitRecipes(Tx& tx, Id building) {
  const std::vector<Recipe> rs = tx.w().building(building)->recipes;
  auto target = [&](const ProvBuilding& x, int& idx, int& cycle) {
    idx = rs.empty() ? 0 : std::clamp(x.recipe, 0, int(rs.size()) - 1);
    cycle = std::min(x.cycle, rs.empty() ? 0 : rs[size_t(idx)].turns);
  };
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
         return std::any_of(p.buildings.begin(), p.buildings.end(), [&](const ProvBuilding& x) {
           if (x.building != building) return false;
           int idx = 0, cycle = 0;
           target(x, idx, cycle);
           return idx != x.recipe || cycle != x.cycle;
         });
       }))
    for (ProvBuilding& x : tx.province(pid).buildings)
      if (x.building == building) {
        int idx = 0, cycle = 0;
        target(x, idx, cycle);
        x.recipe = idx;
        x.cycle = cycle;
      }
}

}  // namespace

void setRecipe(Tx& tx, Id building, int index, const Recipe& r) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.convert) fail(buildingName(tx.w(), building) + " — не постройка преобразования");
  if (index < 0 || index > int(b.recipes.size())) fail("У постройки нет такого рецепта");
  checkRecipe(tx.w(), r);
  Recipe x = r;
  x.name = trim(x.name);
  Building& m = tx.building(building);
  if (index == int(m.recipes.size())) m.recipes.push_back(std::move(x));
  else m.recipes[size_t(index)] = std::move(x);
  fitRecipes(tx, building);
}

void removeRecipe(Tx& tx, Id building, int index) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.convert) fail(buildingName(tx.w(), building) + " — не постройка преобразования");
  if (index < 0 || index >= int(b.recipes.size())) fail("У постройки нет такого рецепта");
  auto& rs = tx.building(building).recipes;
  rs.erase(rs.begin() + index);
  // Провинции с удалённым рецептом — на первый (цикл прерывается без возврата), с последующими — на один выше.
  for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
         return std::any_of(p.buildings.begin(), p.buildings.end(), [&](const ProvBuilding& x) { return x.building == building && x.recipe >= index; });
       }))
    for (ProvBuilding& x : tx.province(pid).buildings)
      if (x.building == building && x.recipe >= index) {
        if (x.recipe == index) {
          x.recipe = 0;
          x.cycle = 0;
        } else {
          x.recipe--;
        }
      }
  fitRecipes(tx, building);
}

std::string recipeTitle(const World& w, const Recipe& r) {
  if (!trim(r.name).empty()) return r.name;
  for (const ResAmount& x : r.out)
    if (const CatalogItem* o = w.resource(x.res)) return o->name;
  return "Рецепт";
}

const Recipe* activeRecipe(const Building& b, const ProvBuilding& pb) {
  if (b.recipes.empty()) return nullptr;
  return &b.recipes[size_t(std::clamp(pb.recipe, 0, int(b.recipes.size()) - 1))];
}

void setBuildingRole(Tx& tx, Id building, BuildingRole role, bool on) {
  const Building& b = needBuilding(tx.w(), building);
  switch (role) {
    case BuildingRole::Convert: {
      if (b.convert == on) return;
      Building& m = tx.building(building);
      m.convert = on;
      if (!on) {
        m.recipes.clear();
        for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
               return std::any_of(p.buildings.begin(), p.buildings.end(),
                                  [&](const ProvBuilding& x) { return x.building == building && (x.idle || x.cycle || x.recipe || x.mult != 1); });
             }))
          for (ProvBuilding& x : tx.province(pid).buildings)
            if (x.building == building) {
              x.idle = false;
              x.cycle = 0;
              x.recipe = 0;
              x.mult = 1;
            }
      }
      return;
    }
    case BuildingRole::Drain: {
      if (b.drain == on) return;
      Building& m = tx.building(building);
      m.drain = on;
      if (!on) {
        for (BuildingLevel& l : m.levels) {
          l.drainPop = 0;
          l.drainEss.clear();
        }
        for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
               return std::any_of(p.buildings.begin(), p.buildings.end(), [&](const ProvBuilding& x) { return x.building == building && (x.idle || x.race); });
             }))
          for (ProvBuilding& x : tx.province(pid).buildings)
            if (x.building == building) {
              x.idle = false;
              x.race = 0;
            }
      }
      return;
    }
    case BuildingRole::UpkeepCut: {
      if (b.upkeepCut == on) return;
      Building& m = tx.building(building);
      m.upkeepCut = on;
      if (!on)
        for (BuildingLevel& l : m.levels) l.upkeepCut = 0;
      return;
    }
    case BuildingRole::Extract: {
      if (b.extract == on) return;
      Building& m = tx.building(building);
      m.extract = on;
      if (!on) {
        m.extractGroups.clear();
        m.extractExclude.clear();
        m.extractOptional = false;
        for (BuildingLevel& l : m.levels) l.extractPct = 0;
      }
      return;
    }
    case BuildingRole::Essence: {
      if (b.essenceGen == on) return;
      Building& m = tx.building(building);
      m.essenceGen = on;
      if (!on)
        for (BuildingLevel& l : m.levels) l.essence.clear();
      return;
    }
    case BuildingRole::Special: {
      if (b.specialAccess == on) return;
      Building& m = tx.building(building);
      m.specialAccess = on;
      if (!on) m.specials.clear();
      return;
    }
  }
}

void setStateReq(Tx& tx, Id building, Id req, int per) {
  const Building& b = needBuilding(tx.w(), building);
  const Building& r = needBuilding(tx.w(), req);
  if (building == req) fail("Постройка не может требовать саму себя");
  if (r.owner && r.owner != b.owner)
    fail(b.owner ? "Уникальная постройка может требовать общие постройки и постройки своего государства"
                 : "Общая постройка не может зависеть от уникальной постройки государства");
  auto& list = tx.building(building).stateReqs;
  auto it = std::find_if(list.begin(), list.end(), [&](const StateReq& q) { return q.building == req; });
  if (per <= 0) {
    if (it != list.end()) list.erase(it);
    return;
  }
  if (per > 100) fail("На каждую постройку — от 1 до 100 требуемых");
  if (it != list.end()) it->per = per;
  else list.push_back(StateReq{req, per});
}

void setBuildingFlag(Tx& tx, Id building, BuildingFlag flag, bool on) {
  const Building& b0 = needBuilding(tx.w(), building);
  bool* field = nullptr;
  Building& b = tx.building(building);
  switch (flag) {
    case BuildingFlag::RelicStore: field = &b.relicStore; break;
    case BuildingFlag::Healing: field = &b.healing; break;
    case BuildingFlag::Plague: field = &b.plague; break;
    case BuildingFlag::Shipyard: field = &b.shipyard; break;
    case BuildingFlag::Mercenary: field = &b.mercenary; break;
    case BuildingFlag::Coastal: field = &b.coastal; break;
    case BuildingFlag::Market: field = &b.market; break;
  }
  if (!field) return;
  const bool was = flag == BuildingFlag::RelicStore ? b0.relicStore : *field;
  *field = on;
  if (on || !was) return;
  if (flag == BuildingFlag::Shipyard)
    for (BuildingLevel& l : b.levels) l.ships = 0;
  if (flag == BuildingFlag::RelicStore)
    for (Id pid : idsWhere(tx.w().provinces, [&](const Province& p) {
           return std::any_of(p.buildings.begin(), p.buildings.end(), [&](const ProvBuilding& x) { return x.building == building && !x.relics.empty(); });
         })) {
      const Id owner = tx.w().province(pid)->owner;
      for (ProvBuilding& x : tx.province(pid).buildings)
        if (x.building == building) {
          releaseBuildingRelics(tx, owner, x);
          x.relics.clear();
        }
    }
}

void setLevelShips(Tx& tx, Id building, int level, u32 ships) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.shipyard) fail(buildingName(tx.w(), building) + " — не верфь");
  if (level < 1 || level > int(b.levels.size())) fail("У постройки нет уровня " + std::to_string(level));
  tx.building(building).levels[size_t(level - 1)].ships = ships & ((1u << int(ShipType::Count)) - 1);
}

namespace {
BuildingLevel& levelRef(Tx& tx, Id building, int level) {
  const Building& b = needBuilding(tx.w(), building);
  if (level < 1 || level > int(b.levels.size())) fail("У постройки нет уровня " + std::to_string(level));
  return tx.building(building).levels[size_t(level - 1)];
}
}  // namespace

void setLevelProduceOnRes(Tx& tx, Id building, int level, Id res, double amount) {
  needBuilding(tx.w(), building);
  if (!tx.w().resource(res)) fail("Ресурс не найден");
  needFinite(amount, "Производство");
  if (amount < 0) fail("Производство не может быть меньше нуля");
  auto& m = levelRef(tx, building, level).produceOnRes;
  if (amount > 0) m[res] = amount;
  else m.erase(res);
}

void setLevelExtract(Tx& tx, Id building, int level, double pct) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.extract) fail(buildingName(tx.w(), building) + " — не постройка добычи");
  needFinite(pct, "Добыча");
  if (pct < 0 || pct > 10000) fail("Увеличение добычи — от 0 до 10 000 %");
  levelRef(tx, building, level).extractPct = pct;
}

void setLevelDrain(Tx& tx, Id building, int level, i64 pop, Id essence, double amount) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.drain) fail(buildingName(tx.w(), building) + " — не постройка иссушения");
  if (pop < 0 || pop > schema::kMaxCount) fail("Жителей за ход — не меньше нуля");
  BuildingLevel& l = levelRef(tx, building, level);
  l.drainPop = pop;
  if (essence) {
    if (!tx.w().essence(essence)) fail("Эссенция не найдена");
    needFinite(amount, "Эссенция");
    if (amount < 0) fail("Эссенция не может быть меньше нуля");
    if (amount > 0) l.drainEss[essence] = amount;
    else l.drainEss.erase(essence);
  }
}

void setLevelUpkeepCut(Tx& tx, Id building, int level, double pct) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.upkeepCut) fail(buildingName(tx.w(), building) + " — не снижает содержание армии");
  needFinite(pct, "Снижение содержания");
  if (pct < 0 || pct > schema::kMaxUpkeepCut) fail("Снижение содержания — от 0 до 50 %");
  levelRef(tx, building, level).upkeepCut = pct;
}

void setExtractOptional(Tx& tx, Id building, bool on) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.extract) fail(buildingName(tx.w(), building) + " — не постройка добычи");
  if (b.extractOptional != on) tx.building(building).extractOptional = on;
}

void setExtractGroups(Tx& tx, Id building, const std::vector<Id>& groups, const std::vector<Id>& exclude) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.extract) fail(buildingName(tx.w(), building) + " — не постройка добычи");
  std::vector<Id> g, x;
  for (Id id : groups) {
    if (!tx.w().catalogs->group(id)) fail("Группа ресурсов не найдена");
    if (!contains(g, id)) g.push_back(id);
  }
  for (Id id : exclude) {
    if (!tx.w().resource(id)) fail("Ресурс не найден");
    if (!contains(x, id)) x.push_back(id);
  }
  Building& m = tx.building(building);
  m.extractGroups = std::move(g);
  m.extractExclude = std::move(x);
}

void setBuildingStateKinds(Tx& tx, Id building, u32 kinds) {
  needBuilding(tx.w(), building);
  kinds &= (1u << int(StateKind::Count)) - 1;
  if (tx.w().building(building)->stateKinds != kinds) tx.building(building).stateKinds = kinds;
}

void setLevelEssCost(Tx& tx, Id building, int level, Id essence, double amount) {
  const Building& b = needBuilding(tx.w(), building);
  if (level < 1 || level > int(b.levels.size())) fail("У постройки нет уровня " + std::to_string(level));
  if (!tx.w().essence(essence)) fail("Эссенция не найдена");
  needFinite(amount, "Цена в эссенции");
  if (amount < 0) fail("Цена в эссенции не может быть меньше нуля");
  auto& m = tx.building(building).levels[size_t(level - 1)].essCost;
  if (amount > 0) m[essence] = amount;
  else m.erase(essence);
}

bool isCoastal(const World& w, Id province) {
  const Province* p = w.province(province);
  if (!p || p->sea) return false;
  bool yes = false;
  w.edges.each([&](const Edge& e) {
    if (yes) return;
    if (e.pl == province && e.tr == Terrain::Sea) yes = true;
    if (e.pr == province && e.tl == Terrain::Sea) yes = true;
    // Граница с морской провинцией.
    if (e.pl == province && e.pr)
      if (const Province* o = w.province(e.pr); o && o->sea) yes = true;
    if (e.pr == province && e.pl)
      if (const Province* o = w.province(e.pl); o && o->sea) yes = true;
  });
  return yes;
}

std::vector<Id> seaNeighbors(const World& w, Id province) {
  std::vector<Id> out;
  w.edges.each([&](const Edge& e) {
    Id other = e.pl == province ? e.pr : e.pr == province ? e.pl : 0;
    if (!other || other == province) return;
    const Province* o = w.province(other);
    if (o && o->sea && !contains(out, other)) out.push_back(other);
  });
  std::sort(out.begin(), out.end());
  return out;
}

bool hasBuildingRole(const World& w, Id province, BuildingFlag flag) {
  const Province* p = w.province(province);
  if (!p) return false;
  for (const ProvBuilding& pb : p->buildings) {
    const Building* b = w.building(pb.building);
    if (!b || pb.builtLevel() < 1) continue;
    switch (flag) {
      case BuildingFlag::RelicStore: if (b->relicStore) return true; break;
      case BuildingFlag::Healing: if (b->healing) return true; break;
      case BuildingFlag::Plague: if (b->plague) return true; break;
      case BuildingFlag::Shipyard: if (b->shipyard) return true; break;
      case BuildingFlag::Mercenary: if (b->mercenary) return true; break;
      case BuildingFlag::Coastal: if (b->coastal) return true; break;
      case BuildingFlag::Market: if (b->market) return true; break;
    }
  }
  return false;
}

void setLevelEssence(Tx& tx, Id building, int level, Id essence, double perTurn) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.essenceGen) fail(buildingName(tx.w(), building) + " — не постройка генерации эссенции");
  if (level < 1 || level > int(b.levels.size())) fail("У постройки нет уровня " + std::to_string(level));
  if (!tx.w().essence(essence)) fail("Эссенция не найдена");
  needFinite(perTurn, "Эссенция за ход");
  if (perTurn < 0) fail("Эссенция за ход не может быть меньше нуля");
  auto& m = tx.building(building).levels[size_t(level - 1)].essence;
  if (perTurn > 0) m[essence] = perTurn;
  else m.erase(essence);
}

void setBuildingSpecial(Tx& tx, Id building, Id special, bool on) {
  const Building& b = needBuilding(tx.w(), building);
  if (!b.specialAccess) fail(buildingName(tx.w(), building) + " — не постройка доступа к особым отрядам");
  if (!tx.w().special(special)) fail("Особый отряд не найден");
  if (contains(b.specials, special) == on) return;
  if (on) tx.building(building).specials.push_back(special);
  else eraseValue(tx.building(building).specials, special);
}

void demolish(Tx& tx, Id province, Id building) {
  const Province& p = needProvince(tx.w(), province);
  needBuilding(tx.w(), building);
  const ProvBuilding* pb = findPb(p, building);
  if (!pb) fail("Постройки " + buildingName(tx.w(), building) + " нет в провинции");
  if (pb->constructing) fail(buildingName(tx.w(), building) + " строится — сначала отмените строительство");
  const Id owner = p.owner;
  const ProvBuilding gone = *pb;
  auto& list = tx.province(province).buildings;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const ProvBuilding& x) { return x.building == building; }), list.end());
  releaseBuildingRelics(tx, owner, gone);
  // Снос посреди цикла преобразования: вход цикла возвращается владельцу.
  const bool cycle = refundCycle(tx, owner, gone);
  addLog(tx, LogKind::Build,
         "Снесена постройка " + buildingName(tx.w(), building) + " в провинции " + provName(tx.w(), province) +
             (cycle ? ": цикл преобразования прерван, ресурсы на входе возвращены" : ""),
         LogRefs{province, 0, owner ? std::vector<Id>{owner} : std::vector<Id>{}});
}

}  // namespace rg::rules
