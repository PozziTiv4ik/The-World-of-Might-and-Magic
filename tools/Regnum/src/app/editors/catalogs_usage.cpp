// Regnum — окно справочников: где используются записи (одним проходом по миру, кеш по тождеству мира) и раздел
// карточки записи со ссылками.
#include <algorithm>

#include "app/editors/buildings.h"
#include "app/editors/catalogs_internal.h"
#include "app/panels/military.h"
#include "gfx/icons.h"

namespace rg::app::cat {

using platform::Key;
using rules::CatalogList;

namespace {

std::string facName(const World& w, Id id) {
  const Faction* f = w.faction(id);
  return f ? orName(f->name) : std::string("—");
}

// ---------------------------------------------------------------- использование: построение индекса
void sortUse(const World& w, Use& u) {
  auto byProv = [&](Id x, Id y) { return compareRu(w.provinceName(x), w.provinceName(y)) < 0; };
  auto byFac = [&](Id x, Id y) { return compareRu(w.factionName(x), w.factionName(y)) < 0; };
  auto byBld = [&](Id x, Id y) {
    const Building* a = w.building(x);
    const Building* b = w.building(y);
    return compareRu(a ? a->name : std::string(), b ? b->name : std::string()) < 0;
  };
  auto bySpec = [&](Id x, Id y) {
    const SpecialUnit* a = w.special(x);
    const SpecialUnit* b = w.special(y);
    return compareRu(a ? a->name : std::string(), b ? b->name : std::string()) < 0;
  };
  std::sort(u.provinces.begin(), u.provinces.end(), byProv);
  std::sort(u.factions.begin(), u.factions.end(), byFac);
  std::sort(u.buildings.begin(), u.buildings.end(), byBld);
  std::sort(u.recipes.begin(), u.recipes.end(), byBld);
  std::sort(u.specials.begin(), u.specials.end(), bySpec);
  std::stable_sort(u.rows.begin(), u.rows.end(), [&](const RowRef& x, const RowRef& y) {
    if (x.faction != y.faction) return compareRu(w.factionName(x.faction), w.factionName(y.faction)) < 0;
    return false;
  });
  std::stable_sort(u.seats.begin(), u.seats.end(), [&](auto& x, auto& y) { return compareRu(w.factionName(x.first), w.factionName(y.first)) < 0; });
}

void buildResources(const World& w, UseMap& m) {
  auto calc = rules::calc(w);
  w.provinces.each([&](const Province& p) {
    if (p.sea || !p.resource) return;
    Use& u = m[p.resource];
    u.provinces.push_back(p.id);
    if (const rules::ProvinceCalc* pc = calc->province(p.id)) u.production += pc->production;
  });
  w.factions.each([&](const Faction& f) {
    for (auto& [r, v] : f.res)
      if (v != 0) {
        Use& u = m[r];
        u.factions.push_back(f.id);
        u.stock += v;
      }
    for (const ArmyRow& row : f.army) {
      std::vector<Id> ids;
      if (row.keyRes) ids.push_back(row.keyRes);
      for (auto& [r, v] : row.extra)
        if (v > 0 && !has(ids, r)) ids.push_back(r);
      for (Id r : ids) m[r].rows.push_back(RowRef{f.id, row.id});
    }
  });
  w.buildings.each([&](const Building& b) {
    std::vector<Id> ids;
    for (const BuildingLevel& l : b.levels) {
      for (auto& [r, v] : l.cost)
        if (!has(ids, r)) ids.push_back(r);
      for (auto& [r, v] : l.produce)
        if (!has(ids, r)) ids.push_back(r);
    }
    for (Id r : ids) m[r].buildings.push_back(b.id);
    std::vector<Id> rec;
    for (const Recipe& rc : b.recipes) {
      for (const ResAmount& x : rc.in)
        if (x.res && !has(rec, x.res)) rec.push_back(x.res);
      for (const ResAmount& x : rc.out)
        if (x.res && !has(rec, x.res)) rec.push_back(x.res);
    }
    for (Id r : rec) m[r].recipes.push_back(b.id);
  });
  w.deals.each([&](const Deal& d) {
    if (d.status != DealStatus::Active) return;
    std::vector<Id> ids;
    for (const DealItem& x : d.items)
      if (x.kind == DealItemKind::Resource && x.res && !has(ids, x.res)) ids.push_back(x.res);
    for (Id r : ids) m[r].deals.push_back(d.id);
  });
  for (const SpecialUnit& s : w.catalogs->specials) {
    std::vector<Id> ids;
    if (s.keyRes) ids.push_back(s.keyRes);
    for (auto& [r, v] : s.extra)
      if (!has(ids, r)) ids.push_back(r);
    for (Id r : ids) m[r].specials.push_back(s.id);
  }
  for (const Constant& c : w.constants->list)
    for (auto& [r, v] : c.res) m[r].constants.push_back(c.key);
}

void buildEssences(const World& w, UseMap& m) {
  w.factions.each([&](const Faction& f) {
    for (auto& [e, v] : f.ess)
      if (v != 0) {
        Use& u = m[e];
        u.factions.push_back(f.id);
        u.stock += v;
        if (v > 0 && f.isState()) u.states++;
      }
    for (const ArmyRow& row : f.army) {
      std::vector<Id> ids;
      for (auto* mp : {&row.essence, &row.essUpkeep})
        for (auto& [e, v] : *mp)
          if (v > 0 && !has(ids, e)) ids.push_back(e);
      for (Id e : ids) m[e].rows.push_back(RowRef{f.id, row.id});
    }
  });
  w.buildings.each([&](const Building& b) {
    std::vector<Id> ids;
    for (const BuildingLevel& l : b.levels)
      for (auto& [e, v] : l.essence)
        if (!has(ids, e)) ids.push_back(e);
    for (Id e : ids) m[e].buildings.push_back(b.id);
  });
  for (const SpecialUnit& s : w.catalogs->specials) {
    std::vector<Id> ids;
    for (auto* mp : {&s.essence, &s.essUpkeep})
      for (auto& [e, v] : *mp)
        if (!has(ids, e)) ids.push_back(e);
    for (Id e : ids) m[e].specials.push_back(s.id);
  }
}

void build(const World& w, CatalogList l, UseMap& m) {
  switch (l) {
    case CatalogList::Resources: buildResources(w, m); break;
    case CatalogList::Essences: buildEssences(w, m); break;
    case CatalogList::Races:
      w.provinces.each([&](const Province& p) {
        if (p.sea) return;
        for (const RacePop& r : p.races) {
          Use& u = m[r.race];
          if (u.provinces.empty() || u.provinces.back() != p.id) u.provinces.push_back(p.id);
          u.population += r.pop;
        }
      });
      // Рабы этой расы у государств (удаление расы их убирает).
      w.factions.each([&](const Faction& f) {
        for (const SlaveGroup& s : f.slaves) {
          if (s.count <= 0) continue;
          Use& u = m[s.race];
          if (u.factions.empty() || u.factions.back() != f.id) u.factions.push_back(f.id);
          u.stock += double(s.count);
        }
      });
      break;
    case CatalogList::Cultures:
    case CatalogList::Religions: {
      const bool cul = l == CatalogList::Cultures;
      w.provinces.each([&](const Province& p) {
        if (Id id = cul ? p.culture : p.religion; !p.sea && id) m[id].provinces.push_back(p.id);
      });
      w.factions.each([&](const Faction& f) {
        if (Id id = cul ? f.culture : f.religion) m[id].factions.push_back(f.id);
      });
      break;
    }
    case CatalogList::Governments:
      w.factions.each([&](const Faction& f) {
        if (f.government) m[f.government].factions.push_back(f.id);
      });
      break;
    case CatalogList::Positions: {
      std::unordered_map<std::string, Id> byKey;
      for (const CatalogItem& c : w.catalogs->positions) byKey.emplace(utf8::searchKey(c.name), c.id);
      w.factions.each([&](const Faction& f) {
        for (const CouncilSeat& s : f.council)
          if (auto it = byKey.find(utf8::searchKey(s.position)); it != byKey.end()) m[it->second].seats.push_back({f.id, s.id});
      });
      break;
    }
  }
  for (auto& [id, u] : m) sortUse(w, u);
}

struct UseCache {
  World w;
  std::shared_ptr<const UseMap> map;
};

}  // namespace

// ================================================================ использование
std::shared_ptr<const UseMap> usageMap(const World& w, CatalogList l) {
  static std::array<UseCache, 7> caches;
  UseCache& c = caches[size_t(l) < caches.size() ? size_t(l) : 0];
  if (!c.map || World::diff(c.w, w) != 0) {
    auto m = std::make_shared<UseMap>();
    build(w, l, *m);
    c.map = std::move(m);
    c.w = w;
  }
  return c.map;
}

const Use& useOf(const UseMap& m, Id item) {
  static const Use kNone;
  auto it = m.find(item);
  return it == m.end() ? kNone : it->second;
}

std::string usageText(CatalogList l, const Use& u) {
  std::vector<std::string> parts;
  auto add = [&](size_t n, std::string prefix, const char* one, const char* few, const char* many) {
    if (n) parts.push_back(prefix + nb(i64(n), one, few, many));
  };
  const bool stocks = l == CatalogList::Resources || l == CatalogList::Essences;
  add(u.provinces.size(), "", "провинция", "провинции", "провинций");
  if (stocks) add(u.factions.size(), "запасы ", "фракции", "фракций", "фракций");
  else if (l == CatalogList::Races) add(u.factions.size(), "рабы ", "государства", "государств", "государств");
  else add(u.factions.size(), "", "государство", "государства", "государств");
  add(u.seats.size(), "", "место в совете", "места в совете", "мест в совете");
  if (l == CatalogList::Essences) add(u.buildings.size(), "генерация ", "постройки", "построек", "построек");
  else add(u.buildings.size(), "уровни ", "постройки", "построек", "построек");
  add(u.recipes.size(), "рецепты ", "постройки", "построек", "построек");
  add(u.deals.size(), "", "сделка", "сделки", "сделок");
  add(u.rows.size(), "", "строка войск", "строки войск", "строк войск");
  add(u.specials.size(), "", "особый отряд", "особых отряда", "особых отрядов");
  add(u.constants.size(), "", "константа", "константы", "констант");
  return join(parts, ", ");
}

// ================================================================ переходы
void openArmy(App& a, Id faction) {
  detail::later(a, [faction](App& x) {
    x.closeEditor();
    x.ui.tabOf[SelType::Faction] = mil::kTabArmy;
    x.select(Selection{SelType::Faction, faction}, true);
  });
}

void selectInTab(State& st, int tab, Id id) {
  if (tab < 0 || tab >= kTabCount) return;
  if (st.tab != tab) st.query.clear();
  st.tab = tab;
  st.editRelic = 0;
  st.focusName = 0;
  if (tab == kResources) {
    st.res = ResSel{ResSel::Resource, id};
    st.scrollTo = st.res;
  } else {
    st.sel[size_t(tab)] = id;
  }
}

void openBuilding(App& a, const World& w, Id building) {
  const Building* b = w.building(building);
  if (!b) return;
  const Id owner = b->owner;
  detail::later(a, [owner, building](App& x) { openBuildingTree(x, owner, building); });
}

bool rowChip(App& a, const World& w, const RowRef& r, bool showCount) {
  const Faction* f = w.faction(r.faction);
  const ArmyRow* row = f ? f->armyRow(r.row) : nullptr;
  if (!f || !row) return false;
  ui::IdScope s{i64(r.row) + 0x5000000LL};
  std::string label = orName(f->name) + " · " + orName(row->name, schema::unitType(row->type).name);
  if (showCount) label += " · " + fmtInt(row->total);
  ui::ChipOpt co;
  co.color = f->color;
  co.clickable = true;
  co.tooltip = "Открыть войска";
  if (ui::flowChip(label, co) == ui::ChipAction::Click) openArmy(a, r.faction);
  return true;
}

void usageSection(App& a, State& st, const World& w, CatalogList l, const CatalogItem& c, const Use& u) {
  const bool res = l == CatalogList::Resources, ess = l == CatalogList::Essences;
  ui::Section sec("Где используется", "link", {.badge = std::to_string(u.total())});
  a.markUi("catalogs.usage");
  if (!sec) return;
  if (u.total() == 0) ui::label("Нигде не используется", {.ink = ui::Ink::Muted});
  auto group = [&](std::string title, size_t n, const char* mark) {
    ui::caption(title + " · " + std::to_string(n));
    a.markUi(std::string("catalogs.usage.") + mark);
  };
  if (!u.provinces.empty()) {
    group(res ? "Добыча" : "Провинции", u.provinces.size(), "provinces");
    ui::ChipFlow cf;
    for (Id pid : u.provinces) {
      const Province* p = w.province(pid);
      if (!p) continue;
      ui::IdScope s{i64(pid)};
      ui::ChipOpt co;
      co.icon = "province";
      co.color = w::factionColor(w, p->owner);
      co.clickable = true;
      co.tooltip = "Открыть провинцию";
      if (ui::flowChip(orName(p->name), co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Province, pid});
    }
  }
  if (!u.factions.empty()) {
    group(res || ess ? "Запасы" : l == CatalogList::Races ? "Рабы" : "Государства", u.factions.size(), "factions");
    ui::ChipFlow cf;
    for (Id fid : u.factions) {
      const Faction* f = w.faction(fid);
      if (!f) continue;
      ui::IdScope s{i64(fid) + 0x1000000LL};
      ui::ChipOpt co;
      co.color = f->color;
      co.clickable = true;
      co.tooltip = f->isGuild() ? "Открыть гильдию" : "Открыть государство";
      std::string label = orName(f->name);
      if (res) {
        label += " · " + (c.id == kGold ? fmtGold(f->stock(c.id)) : fmtNum(f->stock(c.id), 3));
      } else if (ess) {
        const double v = f->essence(c.id);
        label += " · " + fmtNum(v, 3);
        if (v < 0) co.tone = ui::Tone::Danger;
      } else if (l == CatalogList::Races) {
        i64 n = 0;
        for (const SlaveGroup& g : f->slaves)
          if (g.race == c.id) n += g.count;
        label += " · " + fmtInt(n);
      }
      if (ui::flowChip(label, co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, fid});
    }
  }
  if (!u.seats.empty()) {
    group("Совет", u.seats.size(), "seats");
    ui::ChipFlow cf;
    for (auto [fid, sid] : u.seats) {
      const Faction* f = w.faction(fid);
      if (!f) continue;
      ui::IdScope s{i64(sid) + 0x2000000LL};
      std::string who = "место свободно";
      for (const CouncilSeat& x : f->council)
        if (x.id == sid && x.character) who = w.characterName(x.character);
      ui::ChipOpt co;
      co.color = f->color;
      co.clickable = true;
      co.tooltip = "Открыть государство";
      if (ui::flowChip(orName(f->name) + " · " + who, co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, fid});
    }
  }
  auto buildingChips = [&](const std::vector<Id>& ids, i64 salt, const char* forceIcon, const char* kind) {
    ui::ChipFlow cf;
    for (Id bid : ids) {
      const Building* b = w.building(bid);
      if (!b) continue;
      ui::IdScope s{i64(bid) + salt};
      ui::ChipOpt co;
      co.icon = forceIcon ? forceIcon : !b->icon.empty() && gfx::hasIcon(b->icon) ? b->icon.c_str() : "building";
      co.clickable = true;
      co.tooltip = b->owner ? "Уникальная постройка — открыть дерево построек" : "Общее дерево построек — открыть";
      std::string label = orName(b->name);
      if (b->owner) label += " · " + facName(w, b->owner);
      if (ui::flowChip(label, co) == ui::ChipAction::Click) openBuilding(a, w, bid);
      a.markUi(std::string("catalogs.usage.") + kind + "." + std::to_string(bid));
    }
  };
  if (!u.buildings.empty()) {
    group(ess ? "Генерация построек" : "Постройки", u.buildings.size(), "buildings");
    buildingChips(u.buildings, 0x3000000LL, ess ? "essence" : nullptr, "building");
  }
  if (!u.recipes.empty()) {
    group("Рецепты преобразования", u.recipes.size(), "recipes");
    buildingChips(u.recipes, 0x3800000LL, "convert", "recipe");
  }
  if (!u.rows.empty()) {
    group("Строки войск", u.rows.size(), "rows");
    ui::ChipFlow cf;
    for (const RowRef& r : u.rows)
      if (rowChip(a, w, r, true)) a.markUi("catalogs.usage.row." + std::to_string(r.row));
  }
  if (!u.specials.empty()) {
    group("Особые отряды", u.specials.size(), "specials");
    ui::ChipFlow cf;
    for (Id sid : u.specials) {
      const SpecialUnit* s = w.special(sid);
      if (!s) continue;
      ui::IdScope sc{i64(sid) + 0x6000000LL};
      ui::ChipOpt co;
      co.icon = "special-unit";
      co.clickable = true;
      co.tooltip = "Открыть особый отряд";
      if (ui::flowChip(orName(s->name), co) == ui::ChipAction::Click) selectInTab(st, kSpecials, sid);
      a.markUi("catalogs.usage.special." + std::to_string(sid));
    }
  }
  if (!u.deals.empty()) {
    group("Действующие сделки", u.deals.size(), "deals");
    ui::ChipFlow cf;
    for (Id did : u.deals) {
      const Deal* dl = w.deal(did);
      if (!dl) continue;
      ui::IdScope s{i64(did) + 0x4000000LL};
      ui::flowChip(w.factionName(dl->a) + " ⇄ " + w.factionName(dl->b), {.icon = "handshake"});
    }
  }
  if (!u.constants.empty()) {
    group("Глобальные константы", u.constants.size(), "constants");
    ui::ChipFlow cf;
    for (const std::string& key : u.constants) {
      const Constant* k = w.constants->find(key);
      if (!k) continue;
      ui::IdScope s(key);
      ui::ChipOpt co;
      co.icon = "hash";
      co.clickable = true;
      co.tooltip = "Открыть глобальные константы";
      if (ui::flowChip(orName(k->name), co) == ui::ChipAction::Click) detail::later(a, [](App& x) { x.openEditor("constants", 0); });
    }
  }
}

}  // namespace rg::app::cat
