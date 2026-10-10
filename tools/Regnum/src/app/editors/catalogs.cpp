// Regnum — окно справочников: девять вкладок (a.openEditor("catalogs", N) открывает вкладку N): 1 ресурсы (деревом
// групп, catalogs_resources.cpp), 2 расы, 3 культуры, 4 религии, 5 формы правления, 6 должности, 7 эссенции
// элементов, 8 реликвии (catalogs_relics.cpp), 9 особые отряды (catalogs_specials.cpp), 10 классы героев
// (catalogs_classes.cpp), 11 археологические места и 12 сундуки сокровищ (catalogs_arch.cpp). Простые справочники —
// таблица с правкой названия и цвета, добавлением, удалением через rules::removeCatalogItem (с перечнем мест
// использования; встроенные записи закреплены), числами использования и карточкой выбранной записи со ссылками;
// у должности — модификаторы занятой и пустующей должности (ТЗ «Общие доработки», п.4–5). Использование записей
// считается одним проходом по миру и кешируется по тождеству мира. Узкое окно — вкладки только значками.
#include <algorithm>
#include <numeric>

#include "app/editors/buildings.h"
#include "app/editors/catalogs_internal.h"
#include "gfx/icons.h"

namespace rg::app::cat {

using platform::Key;
using rules::CatalogList;

namespace {

template <class V, class T>
bool has(const V& v, const T& x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}

const ListDef kLists[] = {
    {CatalogList::Resources, kResources, "resource", "Ресурсы", "Ресурс", "Новый ресурс", true},
    {CatalogList::Races, kRaces, "race", "Расы", "Раса", "Новая раса", true},
    {CatalogList::Cultures, kCultures, "culture", "Культуры", "Культура", "Новая культура", true},
    {CatalogList::Religions, kReligions, "religion", "Религии", "Религия", "Новая религия", true},
    {CatalogList::Governments, kGovernments, "crown", "Формы правления", "Форма правления", "Новая форма правления", false},
    {CatalogList::Positions, kPositions, "council", "Должности", "Должность", "Новая должность", false},
    {CatalogList::Essences, kEssences, "essence", "Эссенции", "Эссенция", "Новая эссенция", true},
};

// Вкладки окна: значок, подпись, подсказка, метка области вкладки (catalogs.view.<mark>).
struct TabInfo {
  const char* icon;
  const char* title;
  const char* tip;
  const char* mark;
};
const TabInfo kTabs[kTabCount] = {
    {"resource", "Ресурсы", "Ресурсы и их группы", "resources"},
    {"race", "Расы", "Расы", "races"},
    {"culture", "Культуры", "Культуры", "cultures"},
    {"religion", "Религии", "Религии", "religions"},
    {"crown", "Формы правления", "Формы правления", "governments"},
    {"council", "Должности", "Должности совета", "positions"},
    {"essence", "Эссенции", "Эссенции элементов", "essences"},
    {"relic", "Реликвии", "Реликвии", "relics"},
    {"special-unit", "Особые отряды", "Особые отряды", "specials"},
    {"hero-class", "Классы героев", "Классы героев и деревья талантов", "classes"},
    {"pickaxe", "Археологические места", "Археологические места и финальные награды", "archsites"},
    {"chest", "Сундуки сокровищ", "Сундуки сокровищ", "chests"},
};

const ListDef* defOfTab(int tab) {
  for (const ListDef& d : kLists)
    if (d.tab == tab) return &d;
  return nullptr;
}

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
    for (const ResAmount& x : b.recipe.in)
      if (x.res && !has(rec, x.res)) rec.push_back(x.res);
    if (b.recipe.out.res && !has(rec, b.recipe.out.res)) rec.push_back(b.recipe.out.res);
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

// ================================================================ состояние, записи, оформление
State& state(App& a) {
  static State s;
  static u64 tag = 0;
  const Meta& m = *a.store.world().meta;
  const u64 t = hashMix(hash64(a.dataDir()), hash64(m.createdAt + "|" + m.basemap));   // другой мир — чистое состояние
  if (t != tag) {
    tag = t;
    s = State{};
  }
  return s;
}

const ListDef& listDef(CatalogList l) {
  for (const ListDef& d : kLists)
    if (d.list == l) return d;
  return kLists[0];
}

bool locked(CatalogList l, const CatalogItem& c) { return c.builtin || (l == CatalogList::Resources && c.id == kGold); }

const char* itemIcon(CatalogList l, const CatalogItem& c) {
  if (l == CatalogList::Resources) {
    if (c.id == kGold) return "coins";
    return !c.icon.empty() && gfx::hasIcon(c.icon) ? c.icon.c_str() : "resource";
  }
  if (l == CatalogList::Essences) return "essence";
  return listDef(l).icon;
}

std::string orName(const std::string& s, const char* fallback) { return s.empty() ? std::string(fallback) : s; }
std::string nb(i64 n, const char* one, const char* few, const char* many) { return fmtInt(n) + "\xC2\xA0" + plural(n, one, few, many); }

Color legible(Color c) { return ui::theme().dark && c.luminance() < 0.1f ? c.lighten(0.45f) : c; }

void tile(RectF r, const char* icon, Color tint, float radius, bool glow) {
  const ui::Theme& th = ui::theme();
  tint = legible(tint);
  if (glow) {
    ui::draw::shadow(r, radius, 14, tint.alpha(0.55f), 0, 1);
    ui::draw::gradient(r, tint.alpha(0.34f), tint.alpha(0.12f), radius);
  } else {
    ui::draw::rect(r, tint.alpha(th.dark ? 0.2f : 0.16f), radius);
  }
  ui::draw::rectStroke(r, tint.alpha(glow ? 0.75f : 0.45f), radius, 1);
  const float pad = std::round(r.w * 0.25f);
  ui::draw::icon(icon, r.inset(pad), tint);
}

void swatch(CatalogList l, const CatalogItem& c, float size) {
  const ui::Theme& th = ui::theme();
  const ListDef& d = listDef(l);
  RectF r = ui::next(size, size);
  RectF t{r.x, r.cy() - size * 0.5f, size, size};
  if (l == CatalogList::Resources || l == CatalogList::Essences) {
    const Color col = legible(c.color);
    ui::draw::rect(t, col.alpha(th.dark ? 0.22f : 0.18f), 7);
    ui::draw::icon(itemIcon(l, c), t.inset(size * 0.2f), col);
  } else if (d.colored) {
    ui::draw::rect(t, c.color.alpha(th.dark ? 0.22f : 0.18f), 7);
    ui::draw::circle(t.cx(), t.cy(), size * 0.22f, c.color);
  } else {
    ui::draw::rect(t, th.surface3, 7);
    ui::draw::icon(d.icon, t.inset(size * 0.22f), th.textDim);
  }
}

void cardHead(const char* icon, Color tint, std::string_view caption, std::string_view title) {
  ui::Row head({ui::px(52), ui::fr(1)}, 52, 12);
  tile(ui::next(52, 52), icon, tint, 12);
  ui::Group g(0, 2);
  ui::caption(caption);
  ui::label(title, {.font = ui::Font::Title});
}

void searchBox(App& a, State& st, float w, std::string_view placeholder) {
  RectF r = ui::next(w, 30);
  ui::at(r);
  if (st.focusSearch) {
    ui::setKeyboardFocus(ui::id("q"));
    st.focusSearch = false;
  }
  ui::searchField("q", st.query, placeholder);
  a.markUi("catalogs.search");
}

void numCell(ui::Table& t, double v, int digits) {
  t.text(v == 0 ? std::string("—") : fmtNum(v, digits), v == 0 ? ui::Ink::Muted : ui::Ink::Normal);
}

void amountGlyph(const char* icon, Color color, double amount, std::string_view tip, bool gold) {
  ui::iconColored(icon, legible(color), 15, tip);
  ui::label(gold ? fmtGold(amount) : fmtNum(amount, 3), {.font = ui::Font::Small, .tooltip = tip});
  ui::next(4, 1);   // промежуток до следующей пары
}

Split split(RectF R) {
  const ui::Theme& th = ui::theme();
  Split s;
  if (R.w >= 980) {
    const float cw = std::round(clamp(R.w * 0.32f, 360.f, 440.f));
    s.table = RectF{R.x, R.y, R.w - cw - 24, R.h};
    s.card = RectF{s.table.right() + 24, R.y, cw, R.h};
    ui::draw::line(s.table.right() + 12, R.y, s.table.right() + 12, R.bottom(), th.border, 1);
  } else {
    // Узкое окно: карточка под таблицей (вся правка — в карточке у реликвий и особых отрядов).
    const float h = std::round(std::max(150.f, R.h * 0.45f));
    s.table = RectF{R.x, R.y, R.w, h};
    s.card = RectF{R.x, s.table.bottom() + 17, R.w, std::max(0.f, R.h - h - 17)};
    ui::draw::line(R.x, s.table.bottom() + 8, R.right(), s.table.bottom() + 8, th.border, 1);
  }
  return s;
}

int displayIndex(int row, int rows, int sortCol, bool desc, const std::function<int(int, int, int)>& cmp) {
  if (row < 0 || row >= rows) return -1;
  if (sortCol < 0 || !cmp) return row;
  std::vector<int> order(static_cast<size_t>(rows));
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return desc ? cmp(b, a, sortCol) < 0 : cmp(a, b, sortCol) < 0; });
  for (size_t k = 0; k < order.size(); k++)
    if (order[k] == row) return int(k);
  return -1;
}

void revealRow(ui::Scroll& sc, float viewH, float headerH, float rowH, int k) {
  if (k < 0) return;
  const float top = float(k) * rowH;                       // строка видна под закреплённой шапкой: offset ≤ top
  const float low = headerH + top + rowH - viewH;          // и не ниже края: offset ≥ low
  const float off = sc.offset();
  if (off > top) sc.scrollTo(top);
  else if (off < low) sc.scrollTo(low);
}

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
    x.ui.tabOf[SelType::Faction] = "faction.army";
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
  if (edkit::chip(label, co) == ui::ChipAction::Click) openArmy(a, r.faction);
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
    ChipFlow cf;
    for (Id pid : u.provinces) {
      const Province* p = w.province(pid);
      if (!p) continue;
      ui::IdScope s{i64(pid)};
      ui::ChipOpt co;
      co.icon = "province";
      co.color = w::factionColor(w, p->owner);
      co.clickable = true;
      co.tooltip = "Открыть провинцию";
      if (edkit::chip(orName(p->name), co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Province, pid});
    }
  }
  if (!u.factions.empty()) {
    group(res || ess ? "Запасы" : l == CatalogList::Races ? "Рабы" : "Государства", u.factions.size(), "factions");
    ChipFlow cf;
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
      if (edkit::chip(label, co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, fid});
    }
  }
  if (!u.seats.empty()) {
    group("Совет", u.seats.size(), "seats");
    ChipFlow cf;
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
      if (edkit::chip(orName(f->name) + " · " + who, co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, fid});
    }
  }
  auto buildingChips = [&](const std::vector<Id>& ids, i64 salt, const char* forceIcon, const char* kind) {
    ChipFlow cf;
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
      if (edkit::chip(label, co) == ui::ChipAction::Click) openBuilding(a, w, bid);
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
    ChipFlow cf;
    for (const RowRef& r : u.rows)
      if (rowChip(a, w, r, true)) a.markUi("catalogs.usage.row." + std::to_string(r.row));
  }
  if (!u.specials.empty()) {
    group("Особые отряды", u.specials.size(), "specials");
    ChipFlow cf;
    for (Id sid : u.specials) {
      const SpecialUnit* s = w.special(sid);
      if (!s) continue;
      ui::IdScope sc{i64(sid) + 0x6000000LL};
      ui::ChipOpt co;
      co.icon = "special-unit";
      co.clickable = true;
      co.tooltip = "Открыть особый отряд";
      if (edkit::chip(orName(s->name), co) == ui::ChipAction::Click) selectInTab(st, kSpecials, sid);
      a.markUi("catalogs.usage.special." + std::to_string(sid));
    }
  }
  if (!u.deals.empty()) {
    group("Действующие сделки", u.deals.size(), "deals");
    ChipFlow cf;
    for (Id did : u.deals) {
      const Deal* dl = w.deal(did);
      if (!dl) continue;
      ui::IdScope s{i64(did) + 0x4000000LL};
      edkit::chip(w.factionName(dl->a) + " ⇄ " + w.factionName(dl->b), {.icon = "handshake"});
    }
  }
  if (!u.constants.empty()) {
    group("Глобальные константы", u.constants.size(), "constants");
    ChipFlow cf;
    for (const std::string& key : u.constants) {
      const Constant* k = w.constants->find(key);
      if (!k) continue;
      ui::IdScope s(key);
      ui::ChipOpt co;
      co.icon = "hash";
      co.clickable = true;
      co.tooltip = "Открыть глобальные константы";
      if (edkit::chip(orName(k->name), co) == ui::ChipAction::Click) detail::later(a, [](App& x) { x.openEditor("constants", 0); });
    }
  }
}

// ================================================================ действия с записями
Id addItem(App& a, CatalogList l, Id group) {
  Id nid = 0;
  a.act(std::string(listDef(l).newLabel), [&](Tx& tx) {
    nid = rules::addCatalogItem(tx, l, "");
    if (l == CatalogList::Resources) {
      for (CatalogItem& c : tx.catalogs().resources)
        if (c.id == nid) c.icon = "resource";
      if (group) rules::setResourceGroup(tx, nid, group);
    }
  });
  return nid;
}

void renameItem(App& a, CatalogList l, Id id, const std::string& raw) {
  const std::string name = trim(raw);
  const ListDef& d = listDef(l);
  a.act(std::string("Переименовать: ") + d.noun, [&](Tx& tx) {
    if (name.empty()) fail("Название не может быть пустым");
    auto& items = rules::catalogList(tx.catalogs(), l);
    const std::string key = utf8::searchKey(name);
    std::string old;
    for (const CatalogItem& c : items)
      if (c.id != id && utf8::searchKey(c.name) == key) fail(std::string(d.noun) + " «" + c.name + "» уже есть в справочнике");
    for (CatalogItem& c : items)
      if (c.id == id) {
        old = c.name;
        c.name = name;
      }
    // Должность хранится в местах совета текстом — переименовать и там.
    if (l == CatalogList::Positions && !old.empty()) {
      const std::string ok = utf8::searchKey(old);
      std::vector<Id> fids;
      tx.w().factions.each([&](const Faction& f) {
        for (const CouncilSeat& s : f.council)
          if (utf8::searchKey(s.position) == ok) {
            fids.push_back(f.id);
            break;
          }
      });
      for (Id fid : fids)
        for (CouncilSeat& s : tx.faction(fid).council)
          if (utf8::searchKey(s.position) == ok) s.position = name;
    }
  });
}

void setItemColor(App& a, CatalogList l, Id id, Color col) {
  a.act("Цвет записи справочника", [&](Tx& tx) {
    for (CatalogItem& x : rules::catalogList(tx.catalogs(), l))
      if (x.id == id) x.color = col;
  }, {.coalesce = "catcolor:" + std::to_string(int(l)) + ":" + std::to_string(id)});
}

void askRemove(App& a, CatalogList l, Id id) {
  const World w = a.world();
  const ListDef& d = listDef(l);
  const CatalogItem* it = Catalogs::find(rules::catalogList(*w.catalogs, l), id);
  if (!it) return;
  if (locked(l, *it)) {
    a.toast("«" + it->name + "» — встроенная запись, её нельзя удалить", ToastKind::Warning, "lock");
    return;
  }
  auto map = usageMap(w, l);
  const Use& u = useOf(*map, id);
  const std::string name = "«" + orName(it->name) + "»";
  std::string text;
  if (u.total()) {
    text = name + " используется: " + usageText(l, u) + ".";
    text += l == CatalogList::Positions ? " Места в совете останутся с прежним названием должности." : " Все ссылки будут очищены.";
  } else {
    text = name + " нигде не используется.";
  }
  text += " Действие можно отменить Ctrl+Z.";
  a.confirm(std::string("Удалить: ") + d.noun + "?", text, "Удалить", true, [l, id](App& x) {
    x.act("Удалить из справочника", [&](Tx& tx) { rules::removeCatalogItem(tx, l, id); });
  });
}

namespace {

// ================================================================ простые справочники
// Числовые столбцы справочника.
struct NumCol {
  const char* title;
  const char* icon;
  const char* tip;
  float min;
};
std::vector<NumCol> numCols(CatalogList l) {
  switch (l) {
    case CatalogList::Races:
      return {{"Провинции", "province", "Провинции, где живёт раса", 96}, {"Население", "population", "Жители расы во всех провинциях", 110}};
    case CatalogList::Cultures:
    case CatalogList::Religions:
      return {{"Провинции", "province", "Провинции с этой записью", 96}, {"Государства", "crown", "Фракции, где это основная запись", 110}};
    case CatalogList::Governments: return {{"Государства", "crown", "Государства с этой формой правления", 110}};
    case CatalogList::Positions: return {{"Места в совете", "council", "Места в советах государств", 130}};
    case CatalogList::Essences:
      return {{"Государства", "crown", "Государства с запасом эссенции", 104},
              {"Запасы", "treasury", "Сумма запасов всех фракций", 104},
              {"Войска", "army", "Строки войск, которым нужна эссенция", 90}};
    case CatalogList::Resources: break;
  }
  return {};
}
double numOf(CatalogList l, const Use& u, int k) {
  switch (l) {
    case CatalogList::Races: return k == 0 ? double(u.provinces.size()) : double(u.population);
    case CatalogList::Cultures:
    case CatalogList::Religions: return k == 0 ? double(u.provinces.size()) : double(u.factions.size());
    case CatalogList::Governments: return double(u.factions.size());
    case CatalogList::Positions: return double(u.seats.size());
    case CatalogList::Essences: return k == 0 ? double(u.states) : k == 1 ? u.stock : double(u.rows.size());
    case CatalogList::Resources: break;
  }
  return 0;
}

struct Row {
  const CatalogItem* item;
  const Use* use;
};

void listTable(App& a, State& st, const World& w, const ListDef& d, const std::vector<Row>& rows, RectF T) {
  const bool ro = a.readOnly();
  const auto nums = numCols(d.list);
  std::vector<ui::Column> cols;
  cols.push_back({"", nullptr, ui::px(40)});
  cols.push_back({"Название", nullptr, ui::fr(nums.size() > 2 ? 1.6f : 2.f, 160), ui::Align::Left, true});
  if (d.colored) cols.push_back({"", "palette", ui::px(52), ui::Align::Left, false, "Цвет на карте и в списках"});
  for (const NumCol& n : nums) cols.push_back({n.title, n.icon, ui::fr(1, n.min), ui::Align::Right, true, n.tip});
  cols.push_back({"", nullptr, ui::px(40)});
  const int firstNum = 2 + (d.colored ? 1 : 0);
  Id& sel = st.sel[size_t(d.tab)];
  int selIdx = -1;
  for (size_t i = 0; i < rows.size(); i++)
    if (rows[i].item->id == sel) selIdx = int(i);
  const int selBefore = selIdx;
  const std::string emptyText = st.query.empty() ? std::string("Справочник пуст") : std::string("Ничего не найдено");
  ui::Area ta(T, 0);
  ui::Scroll sc("tblscroll", T.h);
  ui::Table t("tbl", cols, int(rows.size()), {.rowHeight = 42, .selected = &selIdx, .emptyIcon = d.icon, .emptyText = emptyText});
  auto cmp = [&](int x, int y, int col) {
    const Row& A = rows[size_t(x)];
    const Row& B = rows[size_t(y)];
    if (col == 1) return compareRu(A.item->name, B.item->name);
    const int k = col - firstNum;
    const double p = numOf(d.list, *A.use, k), q = numOf(d.list, *B.use, k);
    return p < q ? -1 : p > q ? 1 : 0;
  };
  t.sort(cmp);
  // Новая запись: показать строку (таблица строит только видимые строки).
  if (st.focusName)
    for (size_t i = 0; i < rows.size(); i++)
      if (rows[i].item->id == st.focusName) revealRow(sc, T.h, 32, 42, displayIndex(int(i), int(rows.size()), t.sortColumn(), t.sortDescending(), cmp));
  for (int i : t) {
    const CatalogItem& c = *rows[size_t(i)].item;
    const Use& u = *rows[size_t(i)].use;
    const Id id = c.id;
    t.cell();
    swatch(d.list, c, 28);
    t.cell();
    {
      std::string name = c.name;
      if (st.focusName == id) {
        ui::setKeyboardFocus(ui::id("name"));
        st.focusName = 0;
      }
      if (ui::textField("name", name, {.placeholder = "Название", .maxLength = 60, .readOnly = ro, .selectAllOnFocus = true}) && name != c.name)
        renameItem(a, d.list, id, name);
      if (ui::lastItem().focused && id != sel) sel = id;   // правка строки — её карточка
      if (id == sel) a.markUi("catalogs.selected.name");
    }
    if (d.colored) {
      t.cell();
      Color col = c.color;
      ui::Disabled dcol(ro);
      if (ui::colorButton("color", col, {.tooltip = "Цвет на карте и в списках", .size = ui::Size::Small, .hex = false}) && !ro)
        setItemColor(a, d.list, id, col);
    }
    for (size_t k = 0; k < nums.size(); k++) {
      const double v = numOf(d.list, u, int(k));
      if (d.list == CatalogList::Races && k == 1) t.text(v ? fmtShort(v) : std::string("—"), v ? ui::Ink::Normal : ui::Ink::Muted);
      else numCell(t, v, d.list == CatalogList::Essences && k == 1 ? 3 : 0);
    }
    t.cell();
    if (locked(d.list, c)) ui::icon("lock", ui::Ink::Muted, 16, "Встроенная запись — удалить нельзя");
    else if (ui::iconButton("trash", "Удалить запись", {.size = ui::Size::Small, .disabled = ro, .tone = ui::Tone::Danger})) askRemove(a, d.list, id);
  }
  if (selIdx != selBefore && selIdx >= 0 && selIdx < int(rows.size())) sel = rows[size_t(selIdx)].item->id;
}

// Модификаторы должности (ТЗ «Общие доработки», п.4–5): действуют для государства, пока в его совете должность
// занята (или пустует). Предлагаются модификаторы видов «Везде» и «Глобальный».
void positionMods(App& a, const CatalogItem& c) {
  const Id id = c.id;
  const bool ro = a.readOnly();
  auto field = [&](const char* title, const char* icon, const std::string& mark, bool vacant) {
    const std::vector<Id>& cur = vacant ? c.vacantModifiers : c.modifiers;
    ui::Section sec(title, icon, {.badge = std::to_string(cur.size())});
    if (!sec) return;
    std::vector<Id> ids = cur;
    const bool changed = w::modifierList(mark, ids, ro, w::ModScope::Faction);
    a.markUi(mark);   // поле добавления — последний элемент списка
    if (changed)
      a.act(vacant ? "Модификатор отсутствия должности" : "Модификатор должности", [&](Tx& tx) {
        for (CatalogItem& x : tx.catalogs().positions)
          if (x.id == id) (vacant ? x.vacantModifiers : x.modifiers) = ids;
      });
  };
  field("Модификатор должности", "council", "catalogs.position.mods", false);
  field("Модификатор отсутствия должности", "user", "catalogs.position.vacant", true);
}

void listCard(App& a, State& st, const World& w, const ListDef& d, const Row& row) {
  const CatalogItem& c = *row.item;
  const Use& u = *row.use;
  const bool ro = a.readOnly();
  ui::Scroll sc("card");
  const ui::Theme& th = ui::theme();
  cardHead(itemIcon(d.list, c), d.colored ? c.color : th.textDim, d.noun, orName(c.name));
  if (locked(d.list, c)) {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::tag("Встроенная запись", ui::Tone::Accent, "lock");
  }
  if (d.colored) {
    ui::prop("Цвет", "palette", 0.3f);
    Color col = c.color;
    ui::Disabled dcol(ro);
    if (ui::colorButton("cardcolor", col, {.tooltip = "Цвет на карте и в списках"}) && !ro) setItemColor(a, d.list, c.id, col);
    a.markUi("catalogs.card.color");
  }
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    switch (d.list) {
      case CatalogList::Races:
        ui::stat(fmtShort(double(u.population)), "Население", {.icon = "population", .tone = ui::Tone::Info});
        ui::stat(fmtInt(i64(u.provinces.size())), "Провинции", {.icon = "province", .tone = ui::Tone::Accent});
        break;
      case CatalogList::Cultures:
      case CatalogList::Religions:
        ui::stat(fmtInt(i64(u.provinces.size())), "Провинции", {.icon = "province", .tone = ui::Tone::Info});
        ui::stat(fmtInt(i64(u.factions.size())), "Государства", {.icon = "crown", .tone = ui::Tone::Accent});
        break;
      case CatalogList::Governments:
        ui::stat(fmtInt(i64(u.factions.size())), "Государства", {.icon = "crown", .tone = ui::Tone::Accent});
        ui::stat(fmtInt(i64(u.total())), "Всего ссылок", {.icon = "link", .tone = ui::Tone::Info});
        break;
      case CatalogList::Positions:
        ui::stat(fmtInt(i64(u.seats.size())), "Места в совете", {.icon = "council", .tone = ui::Tone::Accent});
        ui::stat(fmtInt(i64(std::count_if(u.seats.begin(), u.seats.end(), [&](auto& s) {
                   const Faction* f = w.faction(s.first);
                   if (!f) return false;
                   for (const CouncilSeat& x : f->council)
                     if (x.id == s.second) return x.character == 0;
                   return false;
                 }))),
                 "Свободные", {.icon = "user", .tone = ui::Tone::Warning});
        break;
      case CatalogList::Essences:
        ui::stat(fmtInt(u.states), "Государства с запасом", {.icon = "crown", .tone = ui::Tone::Accent});
        ui::stat(fmtNum(u.stock, 3), "Запасы фракций", {.icon = "treasury", .tone = u.stock < 0 ? ui::Tone::Danger : ui::Tone::Info});
        break;
      case CatalogList::Resources: break;
    }
  }
  if (d.list == CatalogList::Positions) positionMods(a, c);
  usageSection(a, st, w, d.list, c, u);
  ui::spacer(4);
  if (!locked(d.list, c)) {
    ui::Disabled dis(ro);
    if (ui::button(std::string("Удалить: ") + orName(c.name, "запись"), {.variant = ui::Variant::Danger, .icon = "trash", .fill = true}))
      askRemove(a, d.list, c.id);
    a.markUi("catalogs.delete");
  }
}

void drawList(App& a, State& st, const ListDef& d) {
  const bool ro = a.readOnly();
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    searchBox(a, st, 300);
    ui::flex();
    if (ui::button(d.newLabel, {.variant = ui::Variant::Primary, .icon = "plus", .disabled = ro, .shortcut = {Key::Insert, 0}})) {
      if (Id nid = addItem(a, d.list)) {
        st.query.clear();
        st.sel[size_t(d.tab)] = nid;
        st.focusName = nid;
      }
    }
    a.markUi("catalogs.add");
  }
  ui::spacer(2);
  const World w = a.world();   // снимок кадра (после кнопок: новая запись уже в нём); правка посреди кадра заменяет мир
  auto map = usageMap(w, d.list);
  std::vector<Row> rows;
  for (const CatalogItem& c : rules::catalogList(*w.catalogs, d.list)) {
    if (!st.query.empty() && !utf8::matches(c.name, st.query)) continue;
    rows.push_back(Row{&c, &useOf(*map, c.id)});
  }
  Id& sel = st.sel[size_t(d.tab)];
  if (std::none_of(rows.begin(), rows.end(), [&](const Row& r) { return r.item->id == sel; })) sel = rows.empty() ? 0 : rows.front().item->id;
  const Split sp = split(ui::avail());
  listTable(a, st, w, d, rows, sp.table);
  a.markUi("catalogs.table", sp.table);
  if (!sp.card.empty()) {
    ui::Area ca(sp.card, 0);
    const Row* cur = nullptr;
    for (const Row& r : rows)
      if (r.item->id == sel) cur = &r;
    if (cur) {
      listCard(a, st, w, d, *cur);
    } else {
      ui::spacer(std::max(0.f, sp.card.h * 0.3f));
      ui::emptyState(d.icon, st.query.empty() ? "Справочник пуст" : "Ничего не найдено");
    }
  }
  // Delete — удалить выбранную запись (вне текстовых полей).
  if (!ro && sel && ui::shortcut({Key::Delete, 0})) askRemove(a, d.list, sel);
}

// ================================================================ окно
// Вкладки: подписи и числа, если помещаются; иначе подписи без чисел; иначе значки (с числами или без).
void drawTabs(App& a, State& st, const World& w) {
  const Catalogs& c = *w.catalogs;
  const int counts[kTabCount] = {int(c.resources.size()), int(c.races.size()),     int(c.cultures.size()),
                                 int(c.religions.size()), int(c.governments.size()), int(c.positions.size()),
                                 int(c.essences.size()),  int(c.relics.size()),     int(c.specials.size()),
                                 int(c.classes.size()),   int(c.archSites.size()),   int(c.chests.size())};
  const float avail = ui::avail().w;
  auto natural = [&](bool labels, bool badges) {
    float total = 0;
    for (int i = 0; i < kTabCount; i++) {
      float x = 18 + (labels ? ui::measure(kTabs[i].title, ui::Font::Strong) + 7 : 0);
      if (badges && counts[i] > 0) x += ui::measure(std::to_string(counts[i]), ui::Font::Caption) + 18;
      total += x + (labels ? 24 : 20) + 2;
    }
    return total;
  };
  const int mode = natural(true, true) <= avail ? 0 : natural(true, false) <= avail ? 1 : natural(false, true) <= avail ? 2 : 3;
  std::vector<ui::Tab> items;
  for (int i = 0; i < kTabCount; i++)
    items.push_back(ui::Tab{kTabs[i].icon, mode <= 1 ? std::string_view(kTabs[i].title) : std::string_view(), kTabs[i].tip,
                            mode == 0 || mode == 2 ? counts[i] : 0, ui::Tone::Neutral});
  int tab = st.tab;
  ui::tabs("tabs", tab, std::span<const ui::Tab>(items));
  a.markUi("catalogs.tabs");
  if (tab != st.tab) {
    st.tab = tab;
    st.query.clear();
    st.editRelic = 0;
    st.focusName = 0;
  }
}

void drawEditor(App& a, Id arg) {
  State& st = state(a);
  if (arg >= 1 && arg <= Id(kTabCount)) {
    if (st.tab != int(arg) - 1) {
      st.tab = int(arg) - 1;
      st.query.clear();
      st.editRelic = 0;
      st.focusName = 0;
    }
    a.ui.editorArg = 0;   // вкладка выбрана; дальше — по щелчкам
  }
  {
    const World w = a.world();
    drawTabs(a, st, w);
  }
  ui::spacer(4);
  a.markUi(std::string("catalogs.view.") + kTabs[st.tab].mark, ui::avail());
  if (ui::shortcut({Key::F, ui::ModPrimary})) st.focusSearch = true;
  switch (st.tab) {
    case kResources: drawResources(a, st); break;
    case kRelics: drawRelics(a, st); break;
    case kSpecials: drawSpecials(a, st); break;
    case kClasses: drawClasses(a, st); break;
    case kArchSites: drawArchSites(a, st); break;
    case kChests: drawChests(a, st); break;
    default:
      if (const ListDef* d = defOfTab(st.tab)) drawList(a, st, *d);
      break;
  }
}

EditorReg editorReg({"catalogs", "Справочники", drawEditor, "book", "reference", 20});
CommandReg commandReg({"editor.catalogs", "Справочники: ресурсы, расы, культуры, религии, эссенции, реликвии, особые отряды…", "book", nullptr,
                       [](App& a) { a.openEditor("catalogs", 0); }, [](App& a) { return a.ui.screen == Screen::Editor; }, false,
                       "Справочники"});

}  // namespace
}  // namespace rg::app::cat
