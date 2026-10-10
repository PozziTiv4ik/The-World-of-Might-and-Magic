// Regnum — справочники археологии (ТЗ «Доработки №2», п.4, 8, 10, 12): «Археологические места» — постоянный список из
// десяти мест (новые не добавляются и не удаляются): название, значок, шансы по слотам, «Финальная награда
// исследования» — сундуки по слотам; «Сундуки сокровищ» — добавление, удаление (сундук убирается из наград мест,
// вложенных списков и сокровищ провинций), название, значок, описание и поля: археологические сокровища, золото, ресурс
// (конкретный — из «Руды», «Материалов» или «Трупы»; случайный — из группы «Руда»/«Материалы» с исключениями), артефакт
// (группа реликвий и редкости), эссенция (конкретная или случайная), сундук (случайный из списка).
#include <algorithm>

#include "app/editors/catalogs_internal.h"
#include "core/arch.h"
#include "gfx/icons.h"

namespace rg::app::cat {

using platform::Key;

namespace {

constexpr float kRowH = 44, kHeadH = 32;

const char* chestIcon(const Chest& c) { return !c.icon.empty() && gfx::hasIcon(c.icon) ? c.icon.c_str() : "chest"; }
const char* siteIconOf(const ArchSite& s) { return !s.icon.empty() && gfx::hasIcon(s.icon) ? s.icon.c_str() : "pickaxe"; }
Color slotColor(int slot) { return Color::hex(arch::kSlots[slot].color); }
const char* kindIcon(ChestItemKind k) { return schema::kChestItemKinds[int(k)].icon; }
const char* kindName(ChestItemKind k) { return schema::kChestItemKinds[int(k)].name; }

// Шанс места в слоте по таблице ТЗ (0 — место в слот не выпадает).
int baseChance(const ArchSite& s, int slot) {
  for (const arch::SiteChance& c : arch::slotTable(slot))
    if (s.key == c.key) return c.pct;
  return 0;
}

// Провинции с местом в слотах (всего и найдено).
struct SiteUse {
  int provinces = 0, found = 0;
};
SiteUse siteUse(const World& w, Id site) {
  SiteUse u;
  w.provinces.each([&](const Province& p) {
    if (p.sea) return;
    for (const ArchSlot& s : p.arch)
      if (s.site == site) {
        u.provinces++;
        u.found += s.open;
      }
  });
  return u;
}

// Где используется сундук: места (с номером слота), другие сундуки, провинции (сокровища слотов).
struct ChestUse {
  std::vector<std::pair<Id, int>> sites;
  std::vector<Id> chests;
  int provinces = 0;
  size_t total() const { return sites.size() + chests.size() + size_t(provinces); }
};
ChestUse chestUse(const World& w, Id chest) {
  ChestUse u;
  for (const ArchSite& s : w.catalogs->archSites)
    for (int i = 0; i < kArchSlots; i++)
      if (std::find(s.rewards[size_t(i)].begin(), s.rewards[size_t(i)].end(), chest) != s.rewards[size_t(i)].end()) u.sites.push_back({s.id, i});
  for (const Chest& c : w.catalogs->chests)
    for (const ChestItem& it : c.items)
      if (it.kind == ChestItemKind::Chest && std::find(it.chests.begin(), it.chests.end(), chest) != it.chests.end()) {
        u.chests.push_back(c.id);
        break;
      }
  w.provinces.each([&](const Province& p) {
    for (const ArchSlot& s : p.arch)
      if (s.chest == chest) {
        u.provinces++;
        break;
      }
  });
  return u;
}

// ================================================================ археологические места
bool editSite(App& a, Id id, std::string_view label, const std::string& name, const std::string& icon) {
  return a.act(label, [&](Tx& tx) { rules::setArchSite(tx, id, name, icon); });
}

void sitesTable(App& a, State& st, const World& w, const std::vector<const ArchSite*>& rows, RectF T) {
  const ui::Theme& th = ui::theme();
  Id& sel = st.sel[kArchSites];
  int selIdx = -1;
  for (size_t i = 0; i < rows.size(); i++)
    if (rows[i]->id == sel) selIdx = int(i);
  const int selBefore = selIdx;
  ui::Area ta(T, 0);
  ui::Scroll sc("tblscroll", T.h);
  const ui::Column cols[] = {
      {"", nullptr, ui::px(48)},
      {"Место", nullptr, ui::fr(1.4f, 150), ui::Align::Left, true},
      {"Слоты", nullptr, ui::fr(1.2f, 140), ui::Align::Left, false, "Шанс места в слоте при создании провинции"},
      {"Награды", nullptr, ui::fr(0.6f, 70), ui::Align::Right, true, "Сундуков в списках финальной награды"},
      {"Провинции", nullptr, ui::fr(0.7f, 80), ui::Align::Right, true, "Провинций с этим местом (найдено)"},
  };
  ui::Table t("tbl", std::span<const ui::Column>(cols), int(rows.size()),
              {.rowHeight = kRowH, .selected = &selIdx, .emptyIcon = "pickaxe", .emptyText = "Ничего не найдено"});
  std::vector<SiteUse> uses;
  for (const ArchSite* s : rows) uses.push_back(siteUse(w, s->id));
  auto rewards = [](const ArchSite& s) {
    size_t n = 0;
    for (const auto& l : s.rewards) n += l.size();
    return int(n);
  };
  t.sort([&](int x, int y, int col) {
    switch (col) {
      case 1: return compareRu(rows[size_t(x)]->name, rows[size_t(y)]->name);
      case 3: return rewards(*rows[size_t(x)]) - rewards(*rows[size_t(y)]);
      case 4: return uses[size_t(x)].provinces - uses[size_t(y)].provinces;
      default: return 0;
    }
  });
  for (int i : t) {
    const ArchSite& s = *rows[size_t(i)];
    const RectF rr = t.rowRect();
    ui::IdScope scope{i64(s.id)};
    {
      const RectF cr = t.cell();
      tile(RectF{cr.x + 1, rr.cy() - 14, 28, 28}, siteIconOf(s), th.accent, 8);
    }
    {
      const RectF cr = t.cell();
      ui::draw::text(orName(s.name), RectF{cr.x, rr.y, cr.w, rr.h}, ui::Font::Strong, th.text);
      if (s.id == sel) a.markUi("catalogs.selected.name", RectF{cr.x, rr.y, cr.w, rr.h});
    }
    {
      const RectF cr = t.cell();
      float x = cr.x;
      for (int k = 0; k < kArchSlots; k++) {
        const int pct = baseChance(s, k);
        if (!pct) continue;
        const std::string txt = fmtInt(pct) + " %";
        const float wv = ui::measure(txt, ui::Font::Small) + 18;
        if (x + wv > cr.right()) break;
        const RectF chip{x, rr.cy() - 10, wv, 20};
        ui::draw::rect(chip, slotColor(k).alpha(0.14f), 6);
        ui::draw::rectStroke(chip, slotColor(k).alpha(0.55f), 6, 1);
        ui::draw::text(txt, chip.inset(9, 0), ui::Font::Small, th.text, ui::Align::Center);
        ui::hoverTip("##slot" + std::to_string(k), chip, std::string(arch::kSlots[k].name) + ": " + txt);
        x += wv + 4;
      }
    }
    numCell(t, rewards(s));
    t.text(uses[size_t(i)].provinces ? fmtInt(uses[size_t(i)].provinces) + " (" + fmtInt(uses[size_t(i)].found) + ")" : std::string("—"),
           uses[size_t(i)].provinces ? ui::Ink::Normal : ui::Ink::Muted);
    a.markUi("catalogs.site." + std::to_string(s.id), rr);
  }
  if (selIdx != selBefore && selIdx >= 0 && selIdx < int(rows.size())) sel = rows[size_t(selIdx)]->id;
}

// Список сундуков (фишки с крестиком) и выбор для добавления.
bool chestListEdit(const World& w, std::string_view id, std::vector<Id>& list, Id exclude, bool ro) {
  ui::IdScope scope(id);
  bool changed = false;
  if (!list.empty()) {
    ChipFlow cf;
    for (size_t i = 0; i < list.size(); i++) {
      const Chest* c = w.catalogs->chest(list[i]);
      if (!c) continue;
      ui::IdScope s2{i64(c->id)};
      ui::ChipOpt co;
      co.icon = chestIcon(*c);
      co.removable = !ro;
      std::string tip = orName(c->name);
      for (const ChestItem& it : c->items) tip += "\n· " + rules::chestItemText(w, it);
      co.tooltip = tip;
      if (edkit::chip(orName(c->name), co) == ui::ChipAction::Remove) {
        list.erase(list.begin() + long(i));
        changed = true;
        break;
      }
    }
  }
  if (!ro) {
    std::vector<ui::Option> opts;
    std::vector<Id> ids;
    for (const Chest& c : w.catalogs->chests) {
      if (c.id == exclude || std::find(list.begin(), list.end(), c.id) != list.end()) continue;
      ids.push_back(c.id);
      opts.push_back(ui::Option{c.name, chestIcon(c)});
    }
    int idx = -1;
    if (!ids.empty() && ui::combo("add", idx, opts, {.placeholder = "Добавить сундук", .search = 1, .icon = "plus"}) && idx >= 0 &&
        idx < int(ids.size())) {
      list.push_back(ids[size_t(idx)]);
      changed = true;
    }
  }
  return changed;
}

void siteCard(App& a, State& st, const World& w, const ArchSite& s) {
  const bool ro = a.readOnly();
  const Id id = s.id;
  const ui::Theme& th = ui::theme();
  ui::Scroll sc("card");
  cardHead(siteIconOf(s), th.accent, "Археологическое место", orName(s.name));
  const SiteUse u = siteUse(w, id);
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(fmtInt(u.provinces), "Провинции", {.icon = "province", .tone = ui::Tone::Accent, .tooltip = "Провинций с этим местом"});
    ui::stat(fmtInt(u.found), "Найдено", {.icon = "search", .tone = ui::Tone::Info, .tooltip = "Найдено археологическими группами"});
  }
  {
    ui::prop("Название", "edit", 0.37f);
    std::string name = s.name;
    if (ui::textField("name", name, {.placeholder = "Название места", .maxLength = 80, .readOnly = ro}) && trim(name) != s.name)
      editSite(a, id, "Переименовать археологическое место", name, s.icon);
    a.markUi("catalogs.site.name");
  }
  {
    ui::prop("Значок", siteIconOf(s), 0.37f);
    std::string icon = s.icon;
    if (edkit::iconPicker("icon", icon, false, ro, "Значок места") && icon != s.icon) editSite(a, id, "Значок археологического места", s.name, icon);
    a.markUi("catalogs.site.icon");
  }
  // Финальная награда по слотам: шанс места в слоте и сундуки, из которых выбирается сокровище провинции.
  for (int k = 0; k < kArchSlots; k++) {
    ui::IdScope ks(k);
    const int pct = baseChance(s, k);
    const std::string badge = s.rewards[size_t(k)].empty() ? std::string() : std::to_string(s.rewards[size_t(k)].size());
    ui::Section sec(arch::kSlots[k].name, "chest", {.badge = badge});
    a.markUi("catalogs.site.slot." + std::to_string(k));
    if (!sec) continue;
    {
      ui::HStack hs(20, ui::Align::Left, 6);
      const RectF d = ui::next(10, 20);
      ui::draw::circle(d.cx(), d.cy(), 4, slotColor(k));
      ui::label(pct ? "Шанс при создании провинции " + fmtInt(pct) + " %" : std::string("Только вручную"), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    }
    std::vector<Id> list = s.rewards[size_t(k)];
    if (chestListEdit(w, "rewards", list, 0, ro))
      a.act("Финальная награда места", [&](Tx& tx) { rules::setArchSiteRewards(tx, id, k, list); });
    a.markUi("catalogs.site.rewards." + std::to_string(k));
    if (list.empty() && ro) ui::label("Нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  (void)st;
}

// ================================================================ сундуки
bool editChest(App& a, Id id, std::string_view label, const std::function<void(Chest&)>& fn, std::string coalesce = {}) {
  TxOptions opt;
  opt.coalesce = std::move(coalesce);
  return a.act(label, [&](Tx& tx) {
    const Chest* cur = tx.w().catalogs->chest(id);
    if (!cur) fail("Сундук не найден");
    Chest c = *cur;
    fn(c);
    rules::setChest(tx, c);
  }, opt);
}

Id addChestAct(App& a, State& st) {
  Id nid = 0;
  if (!a.act("Новый сундук сокровищ", [&](Tx& tx) { nid = rules::addChest(tx); }) || !nid) return 0;
  st.query.clear();
  st.sel[kChests] = nid;
  st.focusName = nid;
  return nid;
}

void askRemoveChest(App& a, const World& w, Id id) {
  const Chest* c = w.catalogs->chest(id);
  if (!c) return;
  const ChestUse u = chestUse(w, id);
  std::string text = "Сундук «" + orName(c->name) + "» будет удалён из справочника.";
  if (!u.sites.empty()) text += " Он уйдёт из " + nb(i64(u.sites.size()), "списка наград", "списков наград", "списков наград") + " мест.";
  if (!u.chests.empty()) text += " " + nb(i64(u.chests.size()), "сундук", "сундука", "сундуков") + " потеряют его в своих полях.";
  if (u.provinces) text += " Сокровища " + nb(u.provinces, "провинции", "провинций", "провинций") + " будут выбраны заново.";
  text += " Действие можно отменить Ctrl+Z.";
  a.confirm("Удалить сундук?", text, "Удалить", true, [id](App& x) { x.act("Удалить сундук сокровищ", [&](Tx& tx) { rules::removeChest(tx, id); }); });
}

// Краткое содержимое строкой значков.
void contentGlyphs(const World& w, const Chest& c) {
  ui::HStack hs(26, ui::Align::Left, 3);
  if (c.items.empty()) {
    ui::label("—", {.ink = ui::Ink::Muted});
    return;
  }
  for (size_t i = 0; i < c.items.size(); i++) {
    const ChestItem& it = c.items[i];
    ui::IdScope s{i64(i)};
    const std::string tip = rules::chestItemText(w, it);
    Color col = ui::theme().textDim;
    const char* icon = kindIcon(it.kind);
    if (it.kind == ChestItemKind::Treasure) col = w::resourceColor(w, w.catalogs->resourceId(schema::kResArchTreasure));
    if (it.kind == ChestItemKind::Gold) col = w::resourceColor(w, kGold);
    if (it.kind == ChestItemKind::Resource && it.res) {
      icon = w::resourceIcon(w, it.res);
      col = w::resourceColor(w, it.res);
    }
    if (it.kind == ChestItemKind::Essence && it.essence) col = w::essenceColor(w, it.essence);
    if (it.kind == ChestItemKind::Relic || it.kind == ChestItemKind::Chest) {
      ui::iconColored(icon, legible(col), 15, tip);
      ui::next(4, 1);
    } else {
      amountGlyph(icon, col, it.amount, tip, it.kind == ChestItemKind::Gold);
    }
  }
}

void chestsTable(App& a, State& st, const World& w, const std::vector<const Chest*>& rows, RectF T) {
  const bool ro = a.readOnly();
  const ui::Theme& th = ui::theme();
  Id& sel = st.sel[kChests];
  int selIdx = -1;
  for (size_t i = 0; i < rows.size(); i++)
    if (rows[i]->id == sel) selIdx = int(i);
  const int selBefore = selIdx;
  ui::Area ta(T, 0);
  ui::Scroll sc("tblscroll", T.h);
  const ui::Column cols[] = {
      {"", nullptr, ui::px(48)},
      {"Сундук", nullptr, ui::fr(1.3f, 150), ui::Align::Left, true},
      {"Содержимое", nullptr, ui::fr(1.6f, 160)},
      {"Награда мест", nullptr, ui::fr(0.7f, 90), ui::Align::Right, true, "В списках финальной награды мест"},
      {"", nullptr, ui::px(40)},
  };
  ui::Table t("tbl", std::span<const ui::Column>(cols), int(rows.size()),
              {.rowHeight = kRowH, .selected = &selIdx, .emptyIcon = "chest",
               .emptyText = st.query.empty() ? "Сундуков пока нет" : "Ничего не найдено"});
  std::vector<ChestUse> uses;
  for (const Chest* c : rows) uses.push_back(chestUse(w, c->id));
  auto cmp = [&](int x, int y, int col) {
    switch (col) {
      case 1: return compareRu(rows[size_t(x)]->name, rows[size_t(y)]->name);
      case 3: return int(uses[size_t(x)].sites.size()) - int(uses[size_t(y)].sites.size());
      default: return 0;
    }
  };
  t.sort(cmp);
  if (st.focusName)
    for (size_t i = 0; i < rows.size(); i++)
      if (rows[i]->id == st.focusName) revealRow(sc, T.h, kHeadH, kRowH, displayIndex(int(i), int(rows.size()), t.sortColumn(), t.sortDescending(), cmp));
  for (int i : t) {
    const Chest& c = *rows[size_t(i)];
    const Id id = c.id;
    const RectF rr = t.rowRect();
    ui::IdScope scope{i64(id)};
    {
      const RectF cr = t.cell();
      tile(RectF{cr.x + 1, rr.cy() - 14, 28, 28}, chestIcon(c), th.accent, 8);
    }
    {
      const RectF cr = t.cell();
      ui::draw::text(orName(c.name), RectF{cr.x, rr.y, cr.w, rr.h}, ui::Font::Strong, th.text);
      if (id == sel) a.markUi("catalogs.selected.name", RectF{cr.x, rr.y, cr.w, rr.h});
    }
    t.cell();
    contentGlyphs(w, c);
    numCell(t, double(uses[size_t(i)].sites.size()));
    t.cell();
    if (ui::iconButton("trash", "Удалить сундук", {.size = ui::Size::Small, .disabled = ro, .tone = ui::Tone::Danger})) askRemoveChest(a, w, id);
    a.markUi("catalogs.chest." + std::to_string(id), rr);
  }
  if (selIdx != selBefore && selIdx >= 0 && selIdx < int(rows.size())) sel = rows[size_t(selIdx)]->id;
}

// Группы ресурсов, допустимые для случайного ресурса сундука («Руда», «Материалы» с подгруппами), в порядке дерева.
std::vector<std::pair<Id, int>> chestGroups(const World& w) {
  std::vector<std::pair<Id, int>> out;
  for (auto [g, depth] : w::groupOrder(w))
    if (rules::chestGroupAllowed(w, g)) out.push_back({g, depth});
  return out;
}

std::vector<Id> chestResources(const World& w) {
  std::vector<Id> out;
  for (const CatalogItem& r : w.catalogs->resources)
    if (rules::chestResourceAllowed(w, r.id)) out.push_back(r.id);
  return out;
}

std::string relicGroupPath(const World& w, Id g) {
  std::string s;
  for (int guard = 0; g && guard < 16; guard++) {
    const RelicGroup* x = w.catalogs->relicGroup(g);
    if (!x) break;
    s = s.empty() ? orName(x->name) : orName(x->name) + " / " + s;
    g = x->parent;
  }
  return s;
}

// Поле сундука: вид, количество, удаление; ниже — выбор по виду.
void itemEditor(App& a, const World& w, const Chest& c, size_t i, bool ro) {
  const ChestItem& it = c.items[i];
  const Id cid = c.id;
  ui::IdScope scope{i64(i)};
  ui::Card card({.pad = 8});
  const bool hasAmount = it.kind != ChestItemKind::Relic && it.kind != ChestItemKind::Chest;
  {
    ui::Row r({ui::fr(1, 120), ui::px(hasAmount ? 120.f : 0.f), ui::px(26)}, 30, 6);
    {
      ui::HStack hs(30, ui::Align::Left, 6);
      ui::icon(kindIcon(it.kind), ui::Ink::Accent, 16);
      ui::label(it.kind == ChestItemKind::Treasure ? std::string("Археологические сокровища") : std::string(kindName(it.kind)), {.font = ui::Font::Strong});
    }
    if (hasAmount) {
      double v = it.amount;
      if (ui::numberField("amount", v, {.min = 0, .max = 1e12, .step = 1, .digits = 3, .unit = it.kind == ChestItemKind::Gold ? "тыс." : nullptr, .disabled = ro,
                                        .tooltip = "Количество"}))
        editChest(a, cid, "Количество в сундуке", [&](Chest& x) { x.items[i].amount = std::max(0.0, v); },
                  "chest:amount:" + std::to_string(cid) + ":" + std::to_string(i));
      a.markUi("catalogs.chest.item." + std::to_string(i) + ".amount");
    } else {
      ui::next(0, 30);
    }
    if (ui::iconButton("close", "Убрать поле", {.size = ui::Size::Small, .disabled = ro}))
      editChest(a, cid, "Поле сундука", [&](Chest& x) { x.items.erase(x.items.begin() + long(i)); });
    a.markUi("catalogs.chest.item." + std::to_string(i) + ".remove");
  }
  switch (it.kind) {
    case ChestItemKind::Resource: {
      int mode = it.res ? 0 : 1;
      if (ui::segmented("mode", mode, {{"resource", "Ресурс", "Конкретный ресурс"}, {"dice", "Случайный", "Случайный ресурс из группы"}},
                        {.size = ui::Size::Small, .disabled = ro}) &&
          (mode == 0) != (it.res != 0)) {
        if (mode == 0) {
          const std::vector<Id> list = chestResources(w);
          if (!list.empty()) editChest(a, cid, "Ресурс сундука", [&](Chest& x) { x.items[i].res = list.front(); });
        } else {
          const auto groups = chestGroups(w);
          if (!groups.empty())
            editChest(a, cid, "Ресурс сундука", [&](Chest& x) {
              x.items[i].res = 0;
              x.items[i].group = groups.front().first;
            });
        }
      }
      a.markUi("catalogs.chest.item." + std::to_string(i) + ".mode");
      if (it.res) {
        Id r = it.res;
        if (w::resourceFrom("res", r, chestResources(w), "Ресурс", ro, "Руда, материалы или трупы") && r != it.res)
          editChest(a, cid, "Ресурс сундука", [&](Chest& x) { x.items[i].res = r; });
        a.markUi("catalogs.chest.item." + std::to_string(i) + ".res");
      } else {
        const auto groups = chestGroups(w);
        std::vector<ui::Option> opts;
        std::vector<std::string> labels;
        labels.reserve(groups.size());
        int idx = -1;
        for (size_t k = 0; k < groups.size(); k++) {
          labels.push_back(std::string(size_t(groups[k].second) * 4, ' ') + orName(w.catalogs->group(groups[k].first)->name));
          opts.push_back(ui::Option{labels.back(), groups[k].second ? nullptr : "resource"});
          if (groups[k].first == it.group) idx = int(k);
        }
        if (ui::combo("group", idx, opts, {.placeholder = "Группа", .disabled = ro, .tooltip = "Группа ресурсов"}) && idx >= 0 && idx < int(groups.size()) &&
            groups[size_t(idx)].first != it.group) {
          const Id g = groups[size_t(idx)].first;
          editChest(a, cid, "Группа ресурса сундука", [&](Chest& x) {
            x.items[i].group = g;
            x.items[i].exclude.clear();
          });
        }
        a.markUi("catalogs.chest.item." + std::to_string(i) + ".group");
        if (it.group) {
          const std::vector<Id> pool = rules::resourcesIn(w, it.group);
          std::vector<ui::Option> ro2;
          std::vector<int> selected;
          for (size_t k = 0; k < pool.size(); k++) {
            const CatalogItem* rc = w.resource(pool[k]);
            ro2.push_back(ui::Option{rc ? std::string_view(rc->name) : std::string_view("Ресурс"), w::resourceIcon(w, pool[k])});
            if (std::find(it.exclude.begin(), it.exclude.end(), pool[k]) != it.exclude.end()) selected.push_back(int(k));
          }
          ui::caption("Кроме");
          if (ui::multiSelect("exclude", selected, ro2, {.placeholder = "—", .addTooltip = "Исключить ресурс", .disabled = ro})) {
            std::vector<Id> ex;
            for (int k : selected)
              if (k >= 0 && k < int(pool.size())) ex.push_back(pool[size_t(k)]);
            editChest(a, cid, "Исключения ресурса сундука", [&](Chest& x) { x.items[i].exclude = ex; });
          }
          a.markUi("catalogs.chest.item." + std::to_string(i) + ".exclude");
        }
      }
      break;
    }
    case ChestItemKind::Essence: {
      Id e = it.essence;
      if (w::essencePicker("ess", e, "Случайная эссенция", ro) && e != it.essence)
        editChest(a, cid, "Эссенция сундука", [&](Chest& x) { x.items[i].essence = e; });
      a.markUi("catalogs.chest.item." + std::to_string(i) + ".essence");
      break;
    }
    case ChestItemKind::Relic: {
      std::vector<ui::Option> opts;
      std::vector<std::string> labels;
      labels.reserve(w.catalogs->relicGroups.size());
      int idx = -1;
      for (size_t k = 0; k < w.catalogs->relicGroups.size(); k++) {
        labels.push_back(relicGroupPath(w, w.catalogs->relicGroups[k].id));
        opts.push_back(ui::Option{labels.back(), "relic"});
        if (w.catalogs->relicGroups[k].id == it.relicGroup) idx = int(k);
      }
      if (ui::combo("rgroup", idx, opts, {.placeholder = "Любая группа", .noneLabel = "Любая группа", .disabled = ro, .tooltip = "Группа реликвий"})) {
        const Id g = idx >= 0 && idx < int(w.catalogs->relicGroups.size()) ? w.catalogs->relicGroups[size_t(idx)].id : 0;
        if (g != it.relicGroup) editChest(a, cid, "Группа артефакта сундука", [&](Chest& x) { x.items[i].relicGroup = g; });
      }
      a.markUi("catalogs.chest.item." + std::to_string(i) + ".relicGroup");
      ChipFlow cf;
      for (int k = 0; k < int(Rarity::Count); k++) {
        ui::IdScope rs(k);
        const bool on = (it.rarities >> unsigned(k)) & 1u;
        ui::ChipOpt co;
        co.icon = on ? "check" : "relic";
        co.glow = on ? w::rarityColor(Rarity(k)) : Color(0, 0, 0, 0);
        co.clickable = !ro;
        co.selected = on;
        co.tooltip = on ? "Допустимая редкость — убрать" : "Добавить редкость";
        if (edkit::chip(schema::kRarities[k].name, co) == ui::ChipAction::Click && !ro) {
          const u32 bits = it.rarities ^ (1u << unsigned(k));
          editChest(a, cid, "Редкость артефакта сундука", [&](Chest& x) { x.items[i].rarities = bits; });
        }
        a.markUi("catalogs.chest.item." + std::to_string(i) + ".rarity." + std::to_string(k));
      }
      break;
    }
    case ChestItemKind::Chest: {
      std::vector<Id> list = it.chests;
      if (chestListEdit(w, "chests", list, cid, ro)) editChest(a, cid, "Вложенные сундуки", [&](Chest& x) { x.items[i].chests = list; });
      a.markUi("catalogs.chest.item." + std::to_string(i) + ".chests");
      break;
    }
    default: break;
  }
}

void chestCard(App& a, State& st, const World& w, const Chest& c) {
  const bool ro = a.readOnly();
  const Id id = c.id;
  const ui::Theme& th = ui::theme();
  ui::Scroll sc("card");
  cardHead(chestIcon(c), th.accent, "Сундук сокровищ", orName(c.name));
  const ChestUse u = chestUse(w, id);
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(fmtInt(i64(u.sites.size())), "Награда мест", {.icon = "pickaxe", .tone = ui::Tone::Accent, .tooltip = "В списках финальной награды мест"});
    ui::stat(fmtInt(u.provinces), "Провинции", {.icon = "province", .tone = ui::Tone::Info, .tooltip = "Сокровище слотов провинций"});
  }
  {
    ui::prop("Название", "edit", 0.37f);
    std::string name = c.name;
    if (st.focusName == id) {
      ui::setKeyboardFocus(ui::id("name"));
      st.focusName = 0;
    }
    if (ui::textField("name", name, {.placeholder = "Название сундука", .maxLength = 80, .readOnly = ro, .selectAllOnFocus = true}) && trim(name) != c.name) {
      const std::string n = trim(name);
      editChest(a, id, "Переименовать сундук", [&](Chest& x) { x.name = n; });
    }
    a.markUi("catalogs.chest.name");
  }
  {
    ui::prop("Значок", chestIcon(c), 0.37f);
    std::string icon = c.icon;
    if (edkit::iconPicker("icon", icon, false, ro, "Значок сундука") && icon != c.icon) editChest(a, id, "Значок сундука", [&](Chest& x) { x.icon = icon; });
    a.markUi("catalogs.chest.icon");
  }
  {
    ui::Section sec("Поля", "list", {.badge = std::to_string(c.items.size())});
    a.markUi("catalogs.chest.items");
    if (sec) {
      for (size_t i = 0; i < c.items.size(); i++) itemEditor(a, w, c, i, ro);
      if (!ro) {
        std::vector<ui::Option> opts;
        for (int k = 0; k < int(ChestItemKind::Count); k++)
          opts.push_back(ui::Option{k == 0 ? std::string_view("Археологические сокровища") : std::string_view(schema::kChestItemKinds[k].name), schema::kChestItemKinds[k].icon});
        int idx = -1;
        if (ui::combo("addItem", idx, opts, {.placeholder = "Добавить поле", .search = 1, .icon = "plus"}) && idx >= 0 && idx < int(ChestItemKind::Count)) {
          const ChestItemKind kind = ChestItemKind(idx);
          const std::vector<Id> res = chestResources(w);
          const auto groups = chestGroups(w);
          editChest(a, id, "Поле сундука", [&](Chest& x) {
            ChestItem it;
            it.kind = kind;
            it.amount = kind == ChestItemKind::Relic || kind == ChestItemKind::Chest ? 1 : 50;
            if (kind == ChestItemKind::Gold) it.amount = 1;
            if (kind == ChestItemKind::Resource) {
              if (!groups.empty()) it.group = groups.front().first;
              else if (!res.empty()) it.res = res.front();
            }
            if (kind == ChestItemKind::Relic) {
              it.relicGroup = w.catalogs->relicGroupId(schema::kRelicArchFinds);
              it.rarities = 1u << unsigned(Rarity::Common);
            }
            if (kind == ChestItemKind::Chest)
              for (const Chest& o : w.catalogs->chests)
                if (o.id != id) {
                  it.chests.push_back(o.id);
                  break;
                }
            x.items.push_back(std::move(it));
          });
        }
        a.markUi("catalogs.chest.addItem");
      }
    }
  }
  {
    ui::caption("Описание");
    std::string desc = c.desc;
    if (ui::textArea("desc", desc, 60, {.placeholder = "Описание", .readOnly = ro}) && desc != c.desc)
      editChest(a, id, "Описание сундука", [&](Chest& x) { x.desc = desc; });
    a.markUi("catalogs.chest.desc");
  }
  if (!u.sites.empty() || !u.chests.empty()) {
    ui::Section sec("Где используется", "link", {.badge = std::to_string(u.sites.size() + u.chests.size())});
    a.markUi("catalogs.chest.usage");
    if (sec) {
      ChipFlow cf;
      for (auto [sid, k] : u.sites) {
        const ArchSite* s = w.catalogs->archSite(sid);
        if (!s) continue;
        ui::IdScope scope{i64(sid) * 8 + k};
        ui::ChipOpt co;
        co.icon = siteIconOf(*s);
        co.clickable = true;
        co.tooltip = "Финальная награда — открыть место";
        if (edkit::chip(orName(s->name) + " · " + arch::kSlots[k].shortName, co) == ui::ChipAction::Click) selectInTab(st, kArchSites, sid);
      }
      for (Id oc : u.chests) {
        const Chest* o = w.catalogs->chest(oc);
        if (!o) continue;
        ui::IdScope scope{i64(oc) + 0x7900000LL};
        ui::ChipOpt co;
        co.icon = chestIcon(*o);
        co.clickable = true;
        co.tooltip = "Вложенный сундук — открыть";
        if (edkit::chip(orName(o->name), co) == ui::ChipAction::Click) selectInTab(st, kChests, oc);
      }
    }
  }
  ui::spacer(4);
  {
    ui::Disabled dis(ro);
    if (ui::button("Удалить: " + orName(c.name, "сундук"), {.variant = ui::Variant::Danger, .icon = "trash", .fill = true})) askRemoveChest(a, w, id);
    a.markUi("catalogs.delete");
  }
}

}  // namespace

void drawArchSites(App& a, State& st) {
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    searchBox(a, st, 300, "Поиск мест");
  }
  ui::spacer(2);
  const World w = a.world();
  std::vector<const ArchSite*> rows;
  for (const ArchSite& s : w.catalogs->archSites)
    if (st.query.empty() || utf8::matches(s.name, st.query)) rows.push_back(&s);
  Id& sel = st.sel[kArchSites];
  if (std::none_of(rows.begin(), rows.end(), [&](const ArchSite* x) { return x->id == sel; })) sel = rows.empty() ? 0 : rows.front()->id;
  const Split sp = split(ui::avail());
  sitesTable(a, st, w, rows, sp.table);
  a.markUi("catalogs.table", sp.table);
  if (!sp.card.empty()) {
    ui::Area ca(sp.card, 0);
    if (const ArchSite* cur = w.catalogs->archSite(sel)) siteCard(a, st, w, *cur);
    else {
      ui::spacer(std::max(0.f, sp.card.h * 0.3f));
      ui::emptyState("pickaxe", "Ничего не найдено");
    }
  }
}

void drawChests(App& a, State& st) {
  const bool ro = a.readOnly();
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    searchBox(a, st, 300, "Поиск сундуков");
    ui::flex();
    if (ui::button("Новый сундук", {.variant = ui::Variant::Primary, .icon = "plus", .disabled = ro, .shortcut = {Key::Insert, 0}})) addChestAct(a, st);
    a.markUi("catalogs.add");
  }
  ui::spacer(2);
  const World w = a.world();
  std::vector<const Chest*> rows;
  for (const Chest& c : w.catalogs->chests)
    if (st.query.empty() || utf8::matches(c.name, st.query)) rows.push_back(&c);
  Id& sel = st.sel[kChests];
  if (std::none_of(rows.begin(), rows.end(), [&](const Chest* x) { return x->id == sel; })) sel = rows.empty() ? 0 : rows.front()->id;
  const Split sp = split(ui::avail());
  chestsTable(a, st, w, rows, sp.table);
  a.markUi("catalogs.table", sp.table);
  if (!sp.card.empty()) {
    ui::Area ca(sp.card, 0);
    if (const Chest* cur = w.catalogs->chest(sel)) {
      chestCard(a, st, w, *cur);
    } else {
      ui::spacer(std::max(0.f, sp.card.h * 0.3f));
      if (st.query.empty()) {
        if (ui::emptyState("chest", "Сундуков пока нет", ro ? std::string_view() : std::string_view("Новый сундук"), "plus")) addChestAct(a, st);
      } else {
        ui::emptyState("chest", "Ничего не найдено");
      }
    }
  }
  if (!ro && sel && ui::shortcut({Key::Delete, 0})) askRemoveChest(a, w, sel);
}

}  // namespace rg::app::cat
