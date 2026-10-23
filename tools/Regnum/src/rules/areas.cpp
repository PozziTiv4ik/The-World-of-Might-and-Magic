// Regnum — провинции: владелец, оккупация, столица, суша и море, удаление, объединение и разделение; правка областей
// инструментами карты (операции geo) и уборка провинций, у которых после правки не осталось области на карте (иначе
// запись «призрака» продолжала бы приносить доход).
#include <unordered_set>

#include "core/arch.h"
#include "geo/ops.h"
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

// Строительство в провинции, которая удаляется: уплаченное возвращается плательщикам. Возвращает число возвратов.
int refundProvinceConstruction(Tx& tx, const Province& p) {
  int n = 0;
  for (const ProvBuilding& pb : p.buildings)
    if (pb.constructing && refundPaid(tx, pb)) n++;
  return n;
}

// Провинции, у которых есть область: хотя бы одна дуга графа с этой провинцией по одну из сторон.
std::unordered_set<Id> withArea(const World& w) {
  std::unordered_set<Id> r;
  w.edges.each([&](const Edge& e) {
    if (e.pl) r.insert(e.pl);
    if (e.pr) r.insert(e.pr);
  });
  return r;
}

// Удалить записи провинций, у которых до правки была область, а после — нет. Возвращает их названия.
std::vector<std::string> dropEmptied(Tx& tx, const std::unordered_set<Id>& before) {
  const std::unordered_set<Id> now = withArea(tx.w());
  std::vector<Id> gone;
  for (Id id : before)
    if (!now.count(id) && tx.w().province(id)) gone.push_back(id);
  std::sort(gone.begin(), gone.end());
  std::vector<std::string> names;
  for (Id id : gone) {
    const std::string& n = tx.w().province(id)->name;
    names.push_back(n.empty() ? std::string("без названия") : n);
    dropProvinceRecord(tx, id, "на карте не осталось её области");
  }
  return names;
}

// Реликвия тайника исчезающей провинции — на склад спрятавшего государства (нет его — владельца провинции); без
// государства остаётся свободной. Вызывать после удаления записи провинции (уникальная реликвия уже не в тайнике).
void releaseHidden(Tx& tx, Id relic, Id hiddenBy, Id owner) {
  if (!relic || !tx.w().relic(relic)) return;
  for (Id s : {hiddenBy, owner}) {
    const Faction* f = tx.w().faction(s);
    if (f && f->isState()) {
      moveRelic(tx, relic, RelicPlace{}, RelicPlace{RelicPlace::State, s, 0, s}, 1);
      return;
    }
  }
}

// Новые провинции получают археологические места: все четыре слота по шансам мест (core/arch.h).
void fillArchSlots(Tx& tx) {
  if (tx.w().catalogs->archSites.empty()) return;
  for (Id pid : idsWhere(tx.w().provinces, [](const Province& p) {
         return std::all_of(p.arch.begin(), p.arch.end(), [](const ArchSlot& s) { return s.site == 0; });
       }))
    tx.province(pid).arch = arch::rollSlots(*tx.w().catalogs, arch::provinceSeed(pid));
}

}  // namespace

AreaEdit createProvince(Tx& tx, const std::vector<Vec2>& poly, Terrain terrain, double snap) {
  const auto before = withArea(tx.w());
  AreaEdit r;
  r.province = geo::createProvince(tx, poly, terrain, geo::EditOptions{snap});
  r.removed = dropEmptied(tx, before);
  fillArchSlots(tx);
  return r;
}

AreaEdit addArea(Tx& tx, Id province, const std::vector<Vec2>& poly, double snap) {
  needProvince(tx.w(), province);
  const auto before = withArea(tx.w());
  geo::addArea(tx, province, poly, geo::EditOptions{snap});
  AreaEdit r;
  r.removed = dropEmptied(tx, before);
  r.province = tx.w().province(province) ? province : 0;
  fillArchSlots(tx);
  return r;
}

AreaEdit removeArea(Tx& tx, Id province, const std::vector<Vec2>& poly, double snap) {
  needProvince(tx.w(), province);
  const auto before = withArea(tx.w());
  geo::removeArea(tx, province, poly, geo::EditOptions{snap});
  AreaEdit r;
  r.removed = dropEmptied(tx, before);
  r.province = tx.w().province(province) ? province : 0;
  fillArchSlots(tx);
  return r;
}

AreaEdit paintTerrain(Tx& tx, const std::vector<Vec2>& poly, Terrain terrain, double snap) {
  const auto before = withArea(tx.w());
  geo::paintTerrain(tx, poly, terrain, geo::EditOptions{snap});
  AreaEdit r;
  r.removed = dropEmptied(tx, before);
  fillArchSlots(tx);
  return r;
}

AreaEdit fillAt(Tx& tx, Vec2 p, Id province) {
  if (province) needProvince(tx.w(), province);
  const auto before = withArea(tx.w());
  AreaEdit r;
  r.province = geo::fillAt(tx, p, province);
  r.removed = dropEmptied(tx, before);
  fillArchSlots(tx);
  return r;
}

// ================================================================ провинции
void setProvinceOwner(Tx& tx, Id province, Id faction) {
  const Province& p0 = needProvince(tx.w(), province);
  if (faction) {
    needState(tx.w(), faction);
    if (p0.sea) fail("У морской провинции не бывает владельца");
    // Пока действует «Опустошенная провинция», её нельзя назначить ни одному государству.
    if (hasModKey(tx.w(), p0.modifiers, schema::mod::Devastated)) fail("Опустошённую провинцию нельзя назначить ни одному государству");
  }
  const Id old = p0.owner;
  if (old == faction) return;
  // Уникальные постройки прежнего владельца сносятся (уплаченное за стройку — плательщику).
  std::vector<ProvBuilding> unique;
  for (const ProvBuilding& pb : p0.buildings)
    if (const Building* b = tx.w().building(pb.building); b && old && b->owner == old) unique.push_back(pb);
  Province& p = tx.province(province);
  p.owner = faction;
  p.garrison.clear();  // гарнизон распускается в резерв прежнего владельца
  p.garrisonHeroes.clear();
  p.garrisonLoyalty = schema::kMaxLoyalty;
  p.slaves.clear();    // рабы прежнего владельца снимаются с работ
  if (p.occupied && p.occupier == faction) {
    p.occupied = false;
    p.occupier = 0;
    p.occGarrison.clear();
    p.occIdle = 0;
  }
  if (!unique.empty()) {
    auto& list = p.buildings;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const ProvBuilding& x) {
                 return std::any_of(unique.begin(), unique.end(), [&](const ProvBuilding& u) { return u.building == x.building; });
               }),
               list.end());
  }
  std::vector<std::string> lost;
  for (const ProvBuilding& pb : unique) {
    lost.push_back(buildingName(tx.w(), pb.building));
    if (pb.constructing) refundPaid(tx, pb);
    releaseBuildingRelics(tx, old, pb);
  }
  if (const Faction* of = tx.w().faction(old); of && of->capital == province) tx.faction(old).capital = 0;
  std::string text = faction ? "Провинция " + provName(tx.w(), province) + " перешла к " + facName(tx.w(), faction)
                             : "Провинция " + provName(tx.w(), province) + " осталась без владельца";
  if (old && tx.w().faction(old)) text += " (прежний владелец — " + facName(tx.w(), old) + ")";
  if (!lost.empty()) text += ". Снесены уникальные постройки прежнего владельца: " + join(lost, ", ");
  LogRefs refs{province, 0, {}};
  if (old) refs.factions.push_back(old);
  if (faction) refs.factions.push_back(faction);
  addLog(tx, LogKind::Province, text, refs);
  fitPlots(tx);   // провинция другого владельца выходит из заговоров против прежнего
}

void setOccupied(Tx& tx, Id province, Id occupier) {
  const Province& p0 = needProvince(tx.w(), province);
  if (occupier == 0) {
    if (!p0.occupied && !p0.occupier) return;
    Id was = p0.occupier;
    Province& p = tx.province(province);
    p.occupied = false;
    p.occupier = 0;
    p.occGarrison.clear();   // оккупационный гарнизон — в резерв оккупанта
    p.occIdle = 0;
    LogRefs refs{province, 0, {}};
    if (was) refs.factions.push_back(was);
    if (p.owner) refs.factions.push_back(p.owner);
    addLog(tx, LogKind::War, "Оккупация провинции " + provName(tx.w(), province) + " снята", refs);
    return;
  }
  needState(tx.w(), occupier);
  if (p0.sea) fail("Морскую провинцию нельзя оккупировать");
  if (occupier == p0.owner) fail("Государство не может оккупировать собственную провинцию");
  if (p0.occupied && p0.occupier == occupier) return;
  Province& p = tx.province(province);
  p.occupied = true;
  p.occupier = occupier;
  p.occGarrison.clear();   // гарнизон прежнего оккупанта — в его резерв
  p.occIdle = 0;
  LogRefs refs{province, 0, {occupier}};
  if (p.owner) refs.factions.push_back(p.owner);
  addLog(tx, LogKind::War, "Провинция " + provName(tx.w(), province) + " оккупирована: " + facName(tx.w(), occupier), refs);
}

void setCapital(Tx& tx, Id state, Id province) {
  const Faction& f = needState(tx.w(), state);
  if (province) {
    const Province& p = needProvince(tx.w(), province);
    if (p.sea) fail("Морская провинция не может быть столичной");
    if (p.owner != state) fail("Столицей может быть только провинция самого государства");
  }
  if (f.capital == province) return;
  tx.faction(state).capital = province;
  if (province)
    addLog(tx, LogKind::Province, "Столица " + facName(tx.w(), state) + " — провинция " + provName(tx.w(), province),
           LogRefs{province, 0, {state}});
}

void deleteProvince(Tx& tx, Id province) {
  needProvince(tx.w(), province);
  geo::unassign(tx, province);
  dropProvinceRecord(tx, province, {});
}

namespace detail {
void dropProvinceRecord(Tx& tx, Id province, const std::string& why) {
  const Province p = needProvince(tx.w(), province);
  const std::string name = provName(tx.w(), province);
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) { return f.capital == province; })) tx.faction(fid).capital = 0;
  const int refunds = refundProvinceConstruction(tx, p);
  tx.eraseProvince(province);
  // Реликвии хранилищ построек — владельцу провинции, тайник — спрятавшему; заговоры забывают провинцию.
  for (const ProvBuilding& pb : p.buildings) releaseBuildingRelics(tx, p.owner, pb);
  releaseHidden(tx, p.hiddenRelic, p.hiddenBy, p.owner);
  fitPlots(tx);
  LogRefs refs{province, 0, {}};
  if (p.owner) refs.factions.push_back(p.owner);
  addLog(tx, LogKind::Province,
         "Провинция " + name + " удалена" + (why.empty() ? std::string(" с карты") : ": " + why) +
             (refunds ? ". Стоимость начатого строительства возвращена" : ""),
         refs);
}
}  // namespace detail

void setProvinceSea(Tx& tx, Id province, bool sea) {
  const Province& p0 = needProvince(tx.w(), province);
  if (p0.sea == sea) return;
  Province& p = tx.province(province);
  p.sea = sea;
  if (sea) {
    // Море не содержит сведений и не участвует в расчётах: гарнизон — в резерв владельца, оккупация снимается,
    // столица снимается; начатое строительство приостановлено до возвращения суши. Остальные данные (владелец,
    // население, постройки, влияние) сохраняются и снова действуют, когда провинция станет сухопутной.
    p.garrison.clear();
    p.garrisonHeroes.clear();
    p.occupied = false;
    p.occupier = 0;
    for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) { return f.capital == province; })) tx.faction(fid).capital = 0;
  }
  const Province& now = *tx.w().province(province);
  const bool paused = std::any_of(now.buildings.begin(), now.buildings.end(), [](const ProvBuilding& b) { return b.constructing; });
  LogRefs refs{province, 0, {}};
  if (now.owner) refs.factions.push_back(now.owner);
  addLog(tx, LogKind::Province,
         "Провинция " + provName(tx.w(), province) + (sea ? " стала морской" : " стала сухопутной") +
             (paused ? (sea ? ". Строительство приостановлено" : ". Строительство продолжается") : ""),
         refs);
}

void mergeProvinces(Tx& tx, Id target, Id source) {
  if (target == source) fail("Нельзя объединить провинцию с самой собой");
  const Province& t0 = needProvince(tx.w(), target);
  const Province src = needProvince(tx.w(), source);
  if (t0.sea != src.sea) fail("Нельзя объединить морскую провинцию с сухопутной");
  const std::string srcName = provName(tx.w(), source);
  geo::merge(tx, target, source);

  Province& t = tx.province(target);
  // Население: численности рас складываются.
  for (const RacePop& r : src.races) {
    auto it = std::find_if(t.races.begin(), t.races.end(), [&](const RacePop& x) { return x.race == r.race; });
    if (it != t.races.end()) it->pop = std::min(kMaxCount, it->pop + std::max<i64>(0, r.pop));
    else t.races.push_back(r);
  }
  // Гарнизон того же владельца переходит, иначе распускается в резерв.
  if (t.owner && src.owner == t.owner) {
    for (const GarrisonEntry& g : src.garrison) {
      auto it = std::find_if(t.garrison.begin(), t.garrison.end(), [&](const GarrisonEntry& x) { return x.row == g.row; });
      if (it != t.garrison.end()) it->count += g.count;
      else t.garrison.push_back(g);
    }
    for (Id h : src.garrisonHeroes)
      if (!contains(t.garrisonHeroes, h)) t.garrisonHeroes.push_back(h);
    t.garrisonLoyalty = std::min(t.garrisonLoyalty, src.garrisonLoyalty);
  }
  // Штабы — не больше пяти разных гильдий.
  std::vector<std::string> lostHq;
  for (Id g : src.hqs) {
    if (contains(t.hqs, g)) continue;
    if (int(t.hqs.size()) < schema::kMaxHqPerProvince) t.hqs.push_back(g);
    else lostHq.push_back(facName(tx.w(), g));
  }
  // Влияние: доли цели сохраняются, новые гильдии занимают свободный остаток до 100 %.
  double tsum = 0;
  for (const Influence& i : t.influence) tsum += std::max(0.0, i.pct);
  std::vector<Influence> add;
  double asum = 0;
  for (const Influence& i : src.influence) {
    if (i.pct <= 0 || std::any_of(t.influence.begin(), t.influence.end(), [&](const Influence& x) { return x.guild == i.guild; })) continue;
    add.push_back(i);
    asum += i.pct;
  }
  double room = std::max(0.0, 100.0 - tsum);
  double k = asum > room ? (asum > 0 ? room / asum : 0) : 1.0;
  for (Influence& i : add) {
    i.pct *= k;
    if (i.pct > 1e-9) t.influence.push_back(i);
  }
  // Постройки: без повторов и в пределах слотов цели.
  int slots = slotsOf(t, provinceFx(tx.w(), SourceIndex(tx.w()), t));
  std::vector<std::string> lostDup, lostSlots;
  std::vector<ProvBuilding> lostStarted;  // снесённые недостроенные: уплаченное возвращается плательщику
  for (const ProvBuilding& b : src.buildings) {
    if (std::any_of(t.buildings.begin(), t.buildings.end(), [&](const ProvBuilding& x) { return x.building == b.building; })) {
      lostDup.push_back(buildingName(tx.w(), b.building));
      if (b.constructing) lostStarted.push_back(b);
      continue;
    }
    if (int(t.buildings.size()) >= slots) {
      lostSlots.push_back(buildingName(tx.w(), b.building));
      if (b.constructing) lostStarted.push_back(b);
      continue;
    }
    t.buildings.push_back(b);
  }
  // Ссылки на исходную провинцию.
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) { return f.capital == source; }))
    tx.faction(fid).capital = fid == t.owner ? target : 0;
  std::vector<ProvBuilding> lostRelics;   // снесённые постройки с хранилищем: реликвии — прежнему владельцу
  for (const ProvBuilding& b : src.buildings)
    if (std::none_of(t.buildings.begin(), t.buildings.end(), [&](const ProvBuilding& x) { return x.building == b.building && x.relics == b.relics; }))
      lostRelics.push_back(b);
  // Тайник переходит в цель (если там нет своего), иначе реликвия — спрятавшему государству.
  const bool hiddenMoves = src.hiddenRelic && !t.hiddenRelic;
  if (hiddenMoves) {
    t.hiddenRelic = src.hiddenRelic;
    t.hiddenBy = src.hiddenBy;
  }
  tx.eraseProvince(source);
  for (const ProvBuilding& b : lostRelics) releaseBuildingRelics(tx, src.owner, b);
  if (!hiddenMoves) releaseHidden(tx, src.hiddenRelic, src.hiddenBy, src.owner);
  int refunds = 0;
  for (const ProvBuilding& b : lostStarted)
    if (refundPaid(tx, b)) refunds++;
  fitPlots(tx);

  std::string text = "Провинция " + srcName + " присоединена к " + provName(tx.w(), target);
  if (!lostSlots.empty()) text += ". Снесены из-за нехватки слотов: " + join(lostSlots, ", ");
  if (!lostDup.empty()) text += ". Снесены как повторные: " + join(lostDup, ", ");
  if (!lostHq.empty()) text += ". Не поместились штабы: " + join(lostHq, ", ");
  if (refunds) text += ". Стоимость прерванного строительства возвращена";
  LogRefs refs{target, 0, {}};
  if (t.owner) refs.factions.push_back(t.owner);
  if (src.owner && src.owner != t.owner) refs.factions.push_back(src.owner);
  addLog(tx, LogKind::Province, text, refs);
}

Id splitProvince(Tx& tx, Id province, const std::vector<Vec2>& line) {
  const Province src = needProvince(tx.w(), province);
  Id nid = geo::split(tx, province, line, [&](Tx& t, Id) {
    Province rec;
    rec.name = src.name.empty() ? std::string() : src.name + " (часть)";
    rec.sea = src.sea;
    rec.owner = src.owner;
    rec.culture = src.culture;
    rec.religion = src.religion;
    rec.contentment = src.contentment;
    rec.localTax = src.localTax;
    rec.occupied = src.occupied;
    rec.occupier = src.occupier;
    const Id nid = t.add(std::move(rec)).id;
    t.province(nid).arch = arch::rollSlots(*t.w().catalogs, arch::provinceSeed(nid));   // археологические места новой провинции
    return nid;
  });
  LogRefs refs{province, 0, {}};
  if (src.owner) refs.factions.push_back(src.owner);
  addLog(tx, LogKind::Province, "Провинция " + provName(tx.w(), province) + " разделена: выделена " + provName(tx.w(), nid), refs);
  return nid;
}

}  // namespace rg::rules
