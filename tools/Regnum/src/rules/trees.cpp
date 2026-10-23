// Regnum — деревья построек и технологий: создание и удаление построек, их уровней и технологий с очисткой ссылок,
// копирование дерева технологий.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

// Постройку удалить из провинций; незавершённое строительство возвращает уплаченное плательщику.
void dropBuildingEverywhere(Tx& tx, Id building) {
  auto ids = idsWhere(tx.w().provinces, [&](const Province& p) {
    return std::any_of(p.buildings.begin(), p.buildings.end(), [&](const ProvBuilding& b) { return b.building == building; });
  });
  for (Id pid : ids) {
    const Province& p0 = *tx.w().province(pid);
    std::optional<ProvBuilding> started;
    for (const ProvBuilding& pb : p0.buildings) {
      if (pb.building == building && pb.constructing) started = pb;
      if (pb.building == building) releaseBuildingRelics(tx, p0.owner, pb);
    }
    auto& list = tx.province(pid).buildings;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const ProvBuilding& b) { return b.building == building; }), list.end());
    if (!started) continue;
    if (Id got = refundPaid(tx, *started))
      addLog(tx, LogKind::Build,
             "Строительство " + buildingName(tx.w(), building) + " в провинции " + provName(tx.w(), pid) +
                 " прекращено: постройка удалена из дерева, стоимость возвращена " + facName(tx.w(), got),
             LogRefs{pid, 0, {got}});
  }
}

}  // namespace

// ================================================================ постройки и технологии
Id createBuilding(Tx& tx, Id owner, const std::string& name) {
  if (owner) {
    const Faction& f = needFaction(tx.w(), owner);
    if (!f.isState()) fail("Уникальные постройки бывают только у государств: гильдия не владеет провинциями");
  }
  Building b;
  b.owner = owner;
  std::string n = trim(name);
  b.name = n.empty() ? "Новая постройка" : n;
  b.pos = nextTreePos(tx.w().buildings, [&](const Building& x) { return x.owner == owner; });
  return tx.add(std::move(b)).id;
}

void removeBuilding(Tx& tx, Id building) {
  const Building& b0 = needBuilding(tx.w(), building);
  if (b0.key == schema::bld::ArchGuild && b0.owner && tx.w().faction(b0.owner))
    fail("«" + b0.name + "» — постоянная культовая постройка государства: её можно изменить, но не удалить");
  dropBuildingEverywhere(tx, building);
  for (Id bid : idsWhere(tx.w().buildings, [&](const Building& b) {
         return std::any_of(b.requires_.begin(), b.requires_.end(), [&](const BuildingReq& r) { return r.building == building; });
       })) {
    auto& rq = tx.building(bid).requires_;
    rq.erase(std::remove_if(rq.begin(), rq.end(), [&](const BuildingReq& r) { return r.building == building; }), rq.end());
  }
  tx.eraseBuilding(building);
}

std::vector<Id> removeLastBuildingLevel(Tx& tx, Id building) {
  const Building& b = needBuilding(tx.w(), building);
  const int n = int(b.levels.size());
  if (n <= 1) fail("У постройки должен остаться хотя бы один уровень");
  std::vector<std::string> where;
  tx.w().provinces.each([&](const Province& p) {
    for (const ProvBuilding& pb : p.buildings)
      if (pb.building == building && pb.level >= n) where.push_back(p.name.empty() ? "без названия" : p.name);
  });
  if (!where.empty()) fail("Уровень " + std::to_string(n) + " уже построен или строится: " + join(where, ", "));
  tx.building(building).levels.pop_back();
  // Требования других построек к удалённому уровню опускаются до нового наибольшего.
  std::vector<Id> lowered = idsWhere(tx.w().buildings, [&](const Building& x) {
    return std::any_of(x.requires_.begin(), x.requires_.end(), [&](const BuildingReq& r) { return r.building == building && r.level >= n; });
  });
  for (Id bid : lowered)
    for (BuildingReq& r : tx.building(bid).requires_)
      if (r.building == building && r.level >= n) r.level = n - 1;
  return lowered;
}

Id createTech(Tx& tx, Id faction, const std::string& name) {
  if (faction) needFaction(tx.w(), faction);   // 0 — общее дерево
  Tech t;
  t.faction = faction;
  std::string n = trim(name);
  t.name = n.empty() ? "Новая технология" : n;
  t.pos = nextTreePos(tx.w().techs, [&](const Tech& x) { return x.faction == faction; });
  return tx.add(std::move(t)).id;
}

void removeTech(Tx& tx, Id tech) {
  const Tech& t = needTech(tx.w(), tech);
  // Идущее исследование удалённой технологии — уплаченное возвращается (как у построек).
  if (t.faction && t.research && !t.studied) refundCost(tx, t.faction, tx.tech(tech).paid);
  for (Id tid : idsWhere(tx.w().techs, [&](const Tech& x) { return contains(x.prereqs, tech); })) eraseValue(tx.tech(tid).prereqs, tech);
  for (Id bid : idsWhere(tx.w().buildings, [&](const Building& b) { return contains(b.techs, tech); })) eraseValue(tx.building(bid).techs, tech);
  for (Id fid : idsWhere(tx.w().factions, [&](const Faction& f) { return f.techs.count(tech) > 0; })) {
    if (TechProgress& s = tx.faction(fid).techs[tech]; s.research && !s.studied) refundCost(tx, fid, s.paid);
    tx.faction(fid).techs.erase(tech);
  }
  tx.eraseTech(tech);
}

void copyTechTree(Tx& tx, Id from, Id to) {
  needFaction(tx.w(), from);
  needFaction(tx.w(), to);
  if (from == to) fail("Нельзя скопировать дерево технологий в ту же фракцию");
  std::vector<Tech> src;
  tx.w().techs.each([&](const Tech& t) { if (t.faction == from) src.push_back(t); });
  if (src.empty()) fail("У " + facName(tx.w(), from) + " нет технологий для копирования");
  // Копии ставятся ниже существующего дерева получателя.
  bool any = false;
  double maxY = 0, minY = 0;
  tx.w().techs.each([&](const Tech& t) {
    if (t.faction != to) return;
    maxY = any ? std::max(maxY, t.pos.y) : t.pos.y;
    any = true;
  });
  for (size_t i = 0; i < src.size(); i++) minY = i ? std::min(minY, src[i].pos.y) : src[i].pos.y;
  double dy = any ? maxY + kTreeRowStep * 2 - minY : 0;
  std::unordered_map<Id, Id> remap;
  for (const Tech& t : src) remap[t.id] = tx.nextId(Seq::Tech);
  for (const Tech& t : src) {
    Tech c = t;
    c.id = remap[t.id];
    c.faction = to;
    c.studied = false;
    c.research = false;
    c.progress = 0;
    c.pos.y += dy;
    // Условия из своего дерева — копии; общие технологии остаются общими.
    for (Id& p : c.prereqs) {
      const Tech* pt = tx.w().tech(p);
      p = remap.count(p) ? remap[p] : (pt && pt->faction == 0 ? p : 0);
    }
    c.prereqs.erase(std::remove(c.prereqs.begin(), c.prereqs.end(), Id(0)), c.prereqs.end());
    tx.add(std::move(c));
  }
}

}  // namespace rg::rules
