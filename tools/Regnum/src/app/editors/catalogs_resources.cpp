// Regnum — справочник «Ресурсы» деревом групп (ТЗ «Добавления в справочники», п.3–4): строки групп (шеврон
// свернуть/развернуть, папка, название с правкой на месте, число ресурсов с подгруппами, новая подгруппа и новый
// ресурс в группе, удаление с переносом содержимого в родителя; у групп правил — замок), под ними — подгруппы с
// отступом и ресурсы группы; ресурсы без группы — «Без группы» в конце (не настоящая группа). Раскрытие групп —
// состояние окна (по умолчанию верхний уровень свёрнут), поиск раскрывает подходящие ветви, сортировка по столбцу
// упорядочивает соседей внутри своих групп. Карточки ресурса (группа, цвет, значок, добыча, запасы, где используется)
// и группы (название, родительская группа без циклов, подгруппы и ресурсы фишками, создание, удаление).
#include <algorithm>
#include <functional>
#include <unordered_set>

#include "app/editors/catalogs_internal.h"
#include "gfx/icons.h"

namespace rg::app::cat {

using platform::Key;
using rules::CatalogList;

namespace {

constexpr float kRowH = 42, kHeadH = 32, kIndent = 20, kChevW = 18;
// Столбцы таблицы: название (дерево), цвет, значок, провинции, добыча, запасы, действия.
enum Col : int { cName, cColor, cIcon, cProv, cProd, cStock, cActions };

// Строка дерева.
struct TRow {
  enum Kind : u8 { Group, NoGroup, Res } kind = Res;
  Id id = 0;                         // группа или ресурс
  int depth = 0;
  int parent = -1;                   // строка родительской группы (−1 — верхний уровень)
  bool open = false;                 // группа раскрыта в этом виде
  int count = 0;                     // группа: ресурсов с подгруппами
  const ResGroup* g = nullptr;
  const CatalogItem* r = nullptr;
  const Use* use = nullptr;
};

struct Tree {
  std::vector<TRow> rows;
  std::unordered_map<Id, int> count;   // группа → ресурсов с подгруппами
  std::vector<const CatalogItem*> loose;   // ресурсы без группы (или группа не найдена)
};

const ResGroup* groupOf(const World& w, Id g) { return g ? w.catalogs->group(g) : nullptr; }
Id parentOf(const World& w, Id g) {
  const ResGroup* x = groupOf(w, g);
  return x && groupOf(w, x->parent) ? x->parent : 0;
}

// Раскрыта ли группа: нет записи — верхний уровень свёрнут, подгруппы раскрыты.
bool isOpen(const State& st, const World& w, Id g) {
  auto it = st.open.find(g);
  return it != st.open.end() ? it->second : parentOf(w, g) != 0;
}

// Раскрыть группу и всех её предков (показать строку внутри).
void revealGroup(State& st, const World& w, Id g) {
  for (int guard = 0; g && guard < 64; guard++) {
    st.open[g] = true;
    g = parentOf(w, g);
  }
}

// Назначение группы в правилах (для подсказки замка).
const char* ruleReason(std::string_view key) {
  if (key == schema::grp::Provisions) return "расход провизии населением и «Голод»";
  if (key == schema::grp::Beasts) return "ключевой ресурс зверей";
  if (key == schema::grp::MountsGround) return "ключевой ресурс кавалерии";
  if (key == schema::grp::MountsFlying) return "ключевой ресурс воздушной кавалерии";
  if (key == schema::grp::Monsters) return "ключевой ресурс чудовищ";
  return "";
}
std::string ruleTip(const ResGroup& g) { return std::string("Группа нужна правилам (") + ruleReason(g.key) + ") — удалить нельзя"; }

Tree buildTree(const World& w, const State& st, const UseMap& use) {
  const Catalogs& c = *w.catalogs;
  const bool searching = !st.query.empty();
  Tree t;
  std::unordered_map<Id, std::vector<Id>> kids;                       // группа → подгруппы (порядок справочника)
  std::unordered_map<Id, std::vector<const CatalogItem*>> items;      // группа → её ресурсы
  for (const ResGroup& g : c.resGroups) kids[parentOf(w, g.id)].push_back(g.id);
  for (const CatalogItem& r : c.resources) items[groupOf(w, r.group) ? r.group : 0].push_back(&r);
  for (const CatalogItem& r : c.resources) {
    Id g = groupOf(w, r.group) ? r.group : 0;
    for (int guard = 0; g && guard < 64; guard++, g = parentOf(w, g)) t.count[g]++;
  }
  t.loose = items[0];
  // Поиск: ресурс подходит по своему названию или по названию группы-предка; группа видна, если подходит сама
  // (с содержимым) или в ней есть подходящее.
  std::unordered_set<Id> visG, visR, hitG;
  if (searching) {
    auto under = [&](Id g) {
      for (int guard = 0; g && guard < 64; guard++, g = parentOf(w, g))
        if (utf8::matches(c.group(g)->name, st.query)) return true;
      return false;
    };
    auto showUp = [&](Id g) {
      for (int guard = 0; g && guard < 64; guard++, g = parentOf(w, g)) visG.insert(g);
    };
    for (const ResGroup& g : c.resGroups)
      if (under(g.id)) {
        hitG.insert(g.id);
        showUp(g.id);
      }
    for (const CatalogItem& r : c.resources) {
      const Id g = groupOf(w, r.group) ? r.group : 0;
      if (utf8::matches(r.name, st.query) || (g && hitG.count(g))) {
        visR.insert(r.id);
        showUp(g);
      }
    }
  }
  std::unordered_set<Id> seen;
  std::function<void(Id, int, int)> addGroup = [&](Id gid, int depth, int parent) {
    if (depth > 16 || !seen.insert(gid).second) return;
    if (searching && !visG.count(gid)) return;
    TRow row;
    row.kind = TRow::Group;
    row.id = gid;
    row.depth = depth;
    row.parent = parent;
    row.g = c.group(gid);
    row.count = t.count.count(gid) ? t.count[gid] : 0;
    row.open = searching || isOpen(st, w, gid);
    const int me = int(t.rows.size());
    t.rows.push_back(row);
    if (!row.open) return;
    for (Id k : kids[gid]) addGroup(k, depth + 1, me);
    for (const CatalogItem* r : items[gid]) {
      if (searching && !visR.count(r->id)) continue;
      TRow rr;
      rr.kind = TRow::Res;
      rr.id = r->id;
      rr.depth = depth + 1;
      rr.parent = me;
      rr.r = r;
      rr.use = &useOf(use, r->id);
      t.rows.push_back(rr);
    }
  };
  for (Id g : kids[0]) addGroup(g, 0, -1);
  // «Без группы» — в конце.
  std::vector<const CatalogItem*> loose;
  for (const CatalogItem* r : t.loose)
    if (!searching || visR.count(r->id)) loose.push_back(r);
  if (!loose.empty()) {
    TRow row;
    row.kind = TRow::NoGroup;
    row.count = int(t.loose.size());
    row.open = searching || st.noGroupOpen;
    const int me = int(t.rows.size());
    t.rows.push_back(row);
    if (row.open)
      for (const CatalogItem* r : loose) {
        TRow rr;
        rr.kind = TRow::Res;
        rr.id = r->id;
        rr.depth = 1;
        rr.parent = me;
        rr.r = r;
        rr.use = &useOf(use, r->id);
        t.rows.push_back(rr);
      }
  }
  return t;
}

// Порядок строк при сортировке по столбцу: соседи упорядочиваются внутри своей группы (группы — перед ресурсами,
// «Без группы» — в конце), дерево не разрывается. Возвращает место каждой строки на экране.
std::vector<int> treeOrder(const Tree& t, int col, bool desc) {
  const size_t n = t.rows.size();
  std::vector<std::vector<int>> kids(n + 1);
  for (size_t i = 0; i < n; i++) kids[t.rows[i].parent < 0 ? n : size_t(t.rows[i].parent)].push_back(int(i));
  auto metric = [&](const TRow& r) -> double {
    const Use& u = *r.use;
    return col == cProv ? double(u.provinces.size()) : col == cProd ? u.production : u.stock;
  };
  auto less = [&](int x, int y) {
    const TRow& A = t.rows[size_t(x)];
    const TRow& B = t.rows[size_t(y)];
    auto rank = [](const TRow& r) { return r.kind == TRow::Group ? 0 : r.kind == TRow::Res ? 1 : 2; };
    if (rank(A) != rank(B)) return rank(A) < rank(B);
    int c = 0;
    if (col == cName) {
      if (A.kind == TRow::Group) c = compareRu(A.g->name, B.g->name);
      else if (A.kind == TRow::Res) c = compareRu(A.r->name, B.r->name);
    } else if (A.kind == TRow::Res) {
      const double p = metric(A), q = metric(B);
      c = p < q ? -1 : p > q ? 1 : 0;
    }
    if (desc) c = -c;
    return c != 0 ? c < 0 : x < y;
  };
  std::vector<int> pos(n, 0);
  int next = 0;
  std::function<void(size_t)> walk = [&](size_t node) {
    std::vector<int>& ks = kids[node];
    std::stable_sort(ks.begin(), ks.end(), less);
    for (int k : ks) {
      pos[size_t(k)] = next++;
      walk(size_t(k));
    }
  };
  walk(n);
  return pos;
}

ResSel selOfRow(const TRow& r) {
  return r.kind == TRow::Group ? ResSel{ResSel::Group, r.id} : r.kind == TRow::NoGroup ? ResSel{ResSel::NoGroup, 0} : ResSel{ResSel::Resource, r.id};
}

// ---------------------------------------------------------------- действия
Id addGroupAct(App& a, State& st, const World& w, Id parent) {
  Id nid = 0;
  if (!a.act(parent ? "Новая подгруппа ресурсов" : "Новая группа ресурсов", [&](Tx& tx) { nid = rules::addResGroup(tx, "", parent); }) || !nid)
    return 0;
  if (parent) revealGroup(st, w, parent);
  st.query.clear();
  st.res = ResSel{ResSel::Group, nid};
  st.scrollTo = st.res;
  st.editGroup = st.focusGroup = nid;   // название — сразу в правку
  return nid;
}

Id addResourceAct(App& a, State& st, const World& w, Id group) {
  const Id nid = addItem(a, CatalogList::Resources, group);
  if (!nid) return 0;
  if (group) revealGroup(st, w, group);
  else st.noGroupOpen = true;
  st.query.clear();
  st.res = ResSel{ResSel::Resource, nid};
  st.scrollTo = st.res;
  st.focusName = nid;
  return nid;
}

void renameGroupAct(App& a, Id gid, const std::string& raw) {
  const std::string name = trim(raw);
  a.act("Переименовать группу ресурсов", [&](Tx& tx) { rules::renameResGroup(tx, gid, name); });
}

void askRemoveGroup(App& a, const World& w, Id gid) {
  const ResGroup* g = groupOf(w, gid);
  if (!g) return;
  if (schema::isRuleGroup(g->key)) {
    a.toast("«" + orName(g->name) + "» нужна правилам (" + ruleReason(g->key) + ") — её нельзя удалить", ToastKind::Warning, "lock");
    return;
  }
  const std::vector<Id> subs = rules::childGroups(w, gid);
  int direct = 0;
  for (const CatalogItem& r : w.catalogs->resources) direct += r.group == gid ? 1 : 0;
  const Id parent = parentOf(w, gid);
  std::string text = "Группа «" + orName(g->name) + "» будет удалена.";
  if (!subs.empty() || direct) {
    std::vector<std::string> what;
    if (!subs.empty()) what.push_back(nb(i64(subs.size()), "подгруппа", "подгруппы", "подгрупп"));
    if (direct) what.push_back(nb(direct, "ресурс", "ресурса", "ресурсов"));
    text += " " + join(what, " и ") + (parent ? " перейдут в группу «" + orName(groupOf(w, parent)->name) + "»." : " перейдут на верхний уровень (ресурсы — без группы).");
  }
  text += " Действие можно отменить Ctrl+Z.";
  a.confirm("Удалить группу ресурсов?", text, "Удалить", true, [gid, parent](App& x) {
    if (!x.act("Удалить группу ресурсов", [&](Tx& tx) { rules::removeResGroup(tx, gid); })) return;
    State& s = state(x);
    if (s.res == ResSel{ResSel::Group, gid}) s.res = parent ? ResSel{ResSel::Group, parent} : ResSel{};
  });
}

// Выбор родительской группы: сама группа и её подгруппы недоступны (без циклов).
bool parentPicker(const World& w, Id gid, Id& parent, bool disabled) {
  std::vector<std::string> labels;
  std::vector<std::string> hints;
  std::vector<Id> ids;
  std::vector<bool> off;
  for (auto [g, depth] : w::groupOrder(w)) {
    const ResGroup* x = groupOf(w, g);
    std::string pad;
    for (int i = 0; i < depth; i++) pad += "    ";
    labels.push_back(pad + orName(x ? x->name : std::string()));
    ids.push_back(g);
    off.push_back(g == gid || w.catalogs->inGroup(g, gid));
    hints.push_back(std::to_string(rules::resourcesIn(w, g).size()));
  }
  std::vector<ui::Option> opts;
  for (size_t i = 0; i < ids.size(); i++) {
    ui::Option o;
    o.label = labels[i];
    o.icon = "folder";
    o.hint = hints[i];
    o.disabled = off[i];
    opts.push_back(o);
  }
  int idx = -1;
  for (size_t i = 0; i < ids.size(); i++)
    if (ids[i] == parent) idx = int(i);
  if (!ui::combo("parent", idx, std::span<const ui::Option>(opts),
                 {.placeholder = "Верхний уровень", .noneLabel = "Верхний уровень", .icon = "folder", .disabled = disabled,
                  .tooltip = "Родительская группа"}))
    return false;
  const Id nv = idx >= 0 && idx < int(ids.size()) ? ids[size_t(idx)] : 0;
  if (nv == parent) return false;
  parent = nv;
  return true;
}

// Значок «папка с плюсом»: кнопка новой подгруппы.
bool newSubButton(bool disabled) {
  const bool r = ui::iconButton("folder", "Новая подгруппа", {.size = ui::Size::Small, .disabled = disabled});
  const RectF b = ui::lastItem().rect;
  const ui::Theme& th = ui::theme();
  const float cx = b.right() - 6, cy = b.bottom() - 6;
  ui::draw::circle(cx, cy, 5.5f, th.surface2);
  ui::draw::icon("plus", RectF{cx - 4.5f, cy - 4.5f, 9, 9}, disabled ? th.textMuted : th.accent);
  return r;
}

// Плашка с числом (как в строках списков).
void pill(RectF r, std::string_view text, bool selected) {
  const ui::Theme& th = ui::theme();
  ui::draw::rect(r, selected ? th.accent.alpha(0.22f) : th.surface3, r.h * 0.5f);
  ui::draw::text(text, r, ui::Font::Caption, selected ? th.accent : th.textDim, ui::Align::Center);
}
float pillW(std::string_view text) { return std::max(22.f, std::ceil(ui::measure(text, ui::Font::Caption)) + 12); }

// ---------------------------------------------------------------- строки таблицы
struct Ctx {
  App& a;
  State& st;
  const World& w;
  const Tree& tree;
  bool ro;
  int& selIdx;
  int toggled = -1;                     // строка группы, которую свернуть или развернуть после таблицы
  std::array<RectF, cActions + 1> cells{};   // ячейки строки группы (метки столбцов шапки для тестов)
};

// Направляющие дерева: тонкие вертикальные линии уровней слева от строки.
void guides(RectF cell, int depth) {
  const ui::Theme& th = ui::theme();
  for (int k = 0; k < depth; k++) {
    const float x = std::round(cell.x + float(k) * kIndent + kChevW * 0.5f) + 0.5f;
    ui::draw::line(x, cell.y - 3, x, cell.bottom() + 3, th.border, 1);
  }
}

void groupRow(Ctx& c, ui::Table& t, int i, bool selected, bool hovered) {
  const TRow& row = c.tree.rows[size_t(i)];
  const ui::Theme& th = ui::theme();
  const bool none = row.kind == TRow::NoGroup;
  const RectF rr = t.rowRect();
  ui::draw::rect(rr, th.text.alpha(row.depth == 0 ? 0.035f : 0.018f), 6);   // строка группы — чуть светлее
  const std::string mark = none ? std::string("catalogs.nogroup") : "catalogs.group." + std::to_string(row.id);
  const RectF cell = t.cell();
  c.cells[cName] = cell;
  guides(cell, row.depth);
  const float x0 = cell.x + float(row.depth) * kIndent;
  // Шеврон: свернуть/развернуть.
  {
    const RectF hit{x0 - 4, rr.y + 4, kChevW + 6, rr.h - 8};
    ui::Interaction it = ui::interact(ui::id("##chev"), hit);
    if (it.hovered) ui::setCursor(platform::Cursor::Hand);
    if (it.clicked) c.toggled = i;
    if (it.hovered) ui::draw::rect(RectF{x0 - 2, rr.cy() - 10, kChevW + 2, 20}, th.hover, 5);
    ui::draw::icon(row.open ? "chevron-down" : "chevron-right", RectF{x0 + 1, rr.cy() - 7, 14, 14}, it.hovered ? th.text : th.textMuted);
    c.a.markUi(mark + ".toggle", hit);
  }
  // Папка.
  const RectF tl{x0 + kChevW, rr.cy() - 14, 28, 28};
  {
    const Color tint = none ? th.textMuted : th.accent;
    ui::draw::rect(tl, tint.alpha(th.dark ? 0.16f : 0.13f), 7);
    ui::draw::icon(row.open ? "folder-open" : "folder", tl.inset(6), tint);
  }
  // Название: двойной щелчок по названию (или F2) — правка на месте, по остальной строке — свернуть/развернуть.
  const std::string cnt = std::to_string(row.count);
  const float pw = pillW(cnt);
  const float nx = tl.right() + 10;
  const RectF nameR{nx, rr.cy() - 15, std::max(40.f, cell.right() - nx - pw - 8), 30};
  // Правка начинается, когда кнопка отпущена: иначе отпускание отдаст фокус таблице.
  if (!none && c.st.pendingEdit == row.id && !ui::mouse().down[0]) {
    c.st.pendingEdit = 0;
    c.st.editGroup = c.st.focusGroup = row.id;
  }
  if (!none && c.st.editGroup == row.id && !c.ro) {
    std::string name = row.g->name;
    const bool start = c.st.focusGroup == row.id;
    if (start) {
      ui::setKeyboardFocus(ui::id("gname"));
      c.st.focusGroup = 0;
    }
    ui::at(nameR);
    if (ui::textField("gname", name, {.placeholder = "Название группы", .maxLength = 60, .selectAllOnFocus = true}) && trim(name) != row.g->name)
      renameGroupAct(c.a, row.id, name);
    c.a.markUi(mark + ".name");
    if (!start && !ui::lastItem().focused) c.st.editGroup = 0;   // Enter, Esc или щелчок мимо — правка окончена
  } else {
    const std::string title = none ? std::string("Без группы") : orName(row.g->name);
    const float tw = std::min(nameR.w, std::ceil(ui::measure(title, ui::Font::Strong)) + 2);
    ui::draw::text(title, nameR, ui::Font::Strong, none ? th.textDim : th.text);
    c.a.markUi(mark + ".name", RectF{nameR.x, nameR.y, tw, nameR.h});
    if (t.doubleClicked() == i) {
      const ui::Mouse& m = ui::mouse();
      if (!none && !c.ro && m.x >= nameR.x && m.x <= nameR.x + tw + 6) c.st.pendingEdit = row.id;
      else c.toggled = i;
    }
  }
  pill(RectF{cell.right() - pw, rr.cy() - 9, pw, 18}, cnt, selected);
  c.a.markUi(mark, rr);
  // Пустые ячейки (цвет, значок, числа) и действия.
  for (int k = cColor; k < cActions; k++) c.cells[size_t(k)] = t.cell();
  const RectF ac = t.cell();
  float x = ac.right() - 24;
  if (none) {
    if (selected || hovered) {
      ui::at(RectF{x, rr.cy() - 12, 24, 24});
      if (ui::iconButton("plus", "Новый ресурс без группы", {.size = ui::Size::Small, .disabled = c.ro})) addResourceAct(c.a, c.st, c.w, 0);
      c.a.markUi(mark + ".addRes");
    }
    return;
  }
  ui::at(RectF{x, rr.cy() - 12, 24, 24});
  if (schema::isRuleGroup(row.g->key)) {
    ui::icon("lock", ui::Ink::Muted, 16, ruleTip(*row.g));
    c.a.markUi(mark + ".lock");
  } else {
    if (ui::iconButton("trash", "Удалить группу", {.size = ui::Size::Small, .disabled = c.ro, .tone = ui::Tone::Danger})) askRemoveGroup(c.a, c.w, row.id);
    c.a.markUi(mark + ".delete");
  }
  if (selected || hovered) {
    x -= 28;
    ui::at(RectF{x, rr.cy() - 12, 24, 24});
    if (ui::iconButton("plus", "Новый ресурс в группе", {.size = ui::Size::Small, .disabled = c.ro})) addResourceAct(c.a, c.st, c.w, row.id);
    c.a.markUi(mark + ".addRes");
    x -= 28;
    ui::at(RectF{x, rr.cy() - 12, 24, 24});
    if (newSubButton(c.ro)) addGroupAct(c.a, c.st, c.w, row.id);
    c.a.markUi(mark + ".addSub");
  }
}

void resRow(Ctx& c, ui::Table& t, int i) {
  const TRow& row = c.tree.rows[size_t(i)];
  const CatalogItem& r = *row.r;
  const Use& u = *row.use;
  const Id id = r.id;
  const RectF rr = t.rowRect();
  const RectF cell = t.cell();
  guides(cell, row.depth);
  const float x0 = cell.x + float(row.depth) * kIndent + kChevW;
  ui::at(RectF{x0, rr.cy() - 14, 28, 28});
  swatch(CatalogList::Resources, r, 28);
  {
    std::string name = r.name;
    if (c.st.focusName == id) {
      ui::setKeyboardFocus(ui::id("name"));
      c.st.focusName = 0;
    }
    ui::at(RectF{x0 + 36, rr.cy() - 15, std::max(40.f, cell.right() - x0 - 36), 30});
    if (ui::textField("name", name, {.placeholder = "Название", .maxLength = 60, .readOnly = c.ro, .selectAllOnFocus = true}) && name != r.name)
      renameItem(c.a, CatalogList::Resources, id, name);
    if (ui::lastItem().focused) c.selIdx = i;   // правка строки — её карточка
    if (c.st.res == ResSel{ResSel::Resource, id}) c.a.markUi("catalogs.selected.name");
    c.a.markUi("catalogs.res." + std::to_string(id) + ".name");
  }
  c.a.markUi("catalogs.res." + std::to_string(id), rr);
  t.cell();
  {
    Color col = r.color;
    ui::Disabled dcol(c.ro);
    if (ui::colorButton("color", col, {.tooltip = "Цвет на карте и в списках", .size = ui::Size::Small, .hex = false}) && !c.ro)
      setItemColor(c.a, CatalogList::Resources, id, col);
  }
  t.cell();
  if (id == kGold) {
    ui::icon("coins", ui::Ink::Accent, 18, "Значок казны закреплён");
  } else {
    std::string icon = r.icon;
    if (edkit::iconPicker("icon", icon, true, c.ro, "Значок ресурса") && icon != r.icon)
      c.a.act("Значок ресурса", [&](Tx& tx) {
        for (CatalogItem& x : tx.catalogs().resources)
          if (x.id == id) x.icon = icon;
      });
  }
  numCell(t, double(u.provinces.size()));
  numCell(t, std::round(u.production * 1000) / 1000, 3);
  if (id == kGold) t.text(fmtNum(u.stock, 3), ui::Ink::Accent);
  else numCell(t, u.stock, 3);
  t.cell();
  if (locked(CatalogList::Resources, r)) ui::icon("lock", ui::Ink::Muted, 16, "Встроенная запись — удалить нельзя");
  else if (ui::iconButton("trash", "Удалить ресурс", {.size = ui::Size::Small, .disabled = c.ro, .tone = ui::Tone::Danger}))
    askRemove(c.a, CatalogList::Resources, id);
}

// ---------------------------------------------------------------- карточки
void resourceChips(State& st, const std::vector<const CatalogItem*>& list) {
  ChipFlow cf;
  for (const CatalogItem* r : list) {
    ui::IdScope s{i64(r->id) + 0x7100000LL};
    ui::ChipOpt co;
    co.icon = itemIcon(CatalogList::Resources, *r);
    co.clickable = true;
    co.tooltip = "Выбрать ресурс";
    if (edkit::chip(orName(r->name), co) == ui::ChipAction::Click) {
      st.res = ResSel{ResSel::Resource, r->id};
      st.scrollTo = st.res;
    }
  }
}

// Путь группы фишками: «Звери · Ездовые наземные»; щелчок — выбрать группу. Одна группа (верхний уровень) — уже
// видна в поле выбора, путь не нужен.
void pathChips(State& st, const World& w, Id g) {
  std::vector<Id> path;
  for (int guard = 0; g && guard < 64; guard++, g = parentOf(w, g)) path.insert(path.begin(), g);
  if (path.size() < 2) return;
  ChipFlow cf;
  for (Id x : path) {
    const ResGroup* rg = groupOf(w, x);
    ui::IdScope s{i64(x) + 0x7200000LL};
    ui::ChipOpt co;
    co.icon = "folder";
    co.clickable = true;
    co.tooltip = "Выбрать группу";
    if (edkit::chip(orName(rg ? rg->name : std::string()), co) == ui::ChipAction::Click) {
      st.res = ResSel{ResSel::Group, x};
      st.scrollTo = st.res;
    }
  }
}

void resourceCard(App& a, State& st, const World& w, const CatalogItem& r, const Use& u) {
  const bool ro = a.readOnly();
  const Id id = r.id;
  ui::Scroll sc("card");
  cardHead(itemIcon(CatalogList::Resources, r), r.color, "Ресурс", orName(r.name));
  if (locked(CatalogList::Resources, r)) {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::tag(id == kGold ? "Казна государства" : "Встроенная запись", ui::Tone::Accent, "lock");
  }
  // Группа: выбор (подгруппы — с отступом) и путь.
  {
    ui::prop("Группа", "folder", 0.3f);
    Id g = groupOf(w, r.group) ? r.group : 0;
    if (w::resGroupPicker("resgroup", g, "Без группы", ro)) {
      if (a.act("Группа ресурса", [&](Tx& tx) { rules::setResourceGroup(tx, id, g); })) {
        if (g) revealGroup(st, w, g);
        else st.noGroupOpen = true;
        st.scrollTo = st.res;
      }
    }
    a.markUi("catalogs.resCard.group");
    if (groupOf(w, r.group)) pathChips(st, w, r.group);
  }
  {
    ui::prop("Цвет", "palette", 0.3f);
    Color col = r.color;
    ui::Disabled dcol(ro);
    if (ui::colorButton("cardcolor", col, {.tooltip = "Цвет на карте и в списках"}) && !ro) setItemColor(a, CatalogList::Resources, id, col);
  }
  if (id != kGold) {
    ui::prop("Значок", "image", 0.3f);
    ui::HStack hs(30, ui::Align::Left, 8);
    std::string icon = r.icon;
    if (edkit::iconPicker("cardicon", icon, true, ro, "Значок ресурса") && icon != r.icon)
      a.act("Значок ресурса", [&](Tx& tx) {
        for (CatalogItem& x : tx.catalogs().resources)
          if (x.id == id) x.icon = icon;
      });
    ui::label(edkit::iconTitle(r.icon), {.ink = ui::Ink::Dim});
  }
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    if (id == kGold) {   // золото — казна: добычи провинций нет
      ui::stat(fmtShort(u.stock), "Казна всех фракций", {.icon = "treasury", .tone = ui::Tone::Accent});
      ui::stat(fmtInt(i64(u.factions.size())), "Фракции с казной", {.icon = "crown", .tone = ui::Tone::Info});
    } else {
      ui::stat(fmtNum(u.production, 3), "Добыча за ход", {.icon = "pickaxe", .tone = ui::Tone::Success});
      ui::stat(fmtNum(u.stock, 3), "Запасы фракций", {.icon = "treasury", .tone = ui::Tone::Accent});
    }
  }
  usageSection(a, st, w, CatalogList::Resources, r, u);
  ui::spacer(4);
  if (!locked(CatalogList::Resources, r)) {
    ui::Disabled dis(ro);
    if (ui::button("Удалить: " + orName(r.name, "ресурс"), {.variant = ui::Variant::Danger, .icon = "trash", .fill = true}))
      askRemove(a, CatalogList::Resources, id);
    a.markUi("catalogs.delete");
  }
}

void groupCard(App& a, State& st, const World& w, const Tree& tree, const ResGroup& g) {
  const bool ro = a.readOnly();
  const Id gid = g.id;
  const ui::Theme& th = ui::theme();
  const Id parent = parentOf(w, gid);
  const bool rule = schema::isRuleGroup(g.key);
  ui::Scroll sc("card");
  cardHead("folder-open", th.accent, parent ? "Подгруппа ресурсов" : "Группа ресурсов", orName(g.name));
  if (rule) {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::tag("Группа правил", ui::Tone::Accent, "lock");
    ui::tooltip(ruleTip(g));
    a.markUi("catalogs.groupCard.rule");
  }
  {
    ui::prop("Название", "edit", 0.3f);
    std::string name = g.name;
    if (ui::textField("gcardname", name, {.placeholder = "Название группы", .maxLength = 60, .readOnly = ro, .selectAllOnFocus = true}) &&
        trim(name) != g.name)
      renameGroupAct(a, gid, name);
    a.markUi("catalogs.groupCard.name");
  }
  {
    ui::prop("Входит в", "folder", 0.3f);
    Id p = parent;
    if (parentPicker(w, gid, p, ro))
      if (a.act("Родительская группа", [&](Tx& tx) { rules::setGroupParent(tx, gid, p); })) {
        revealGroup(st, w, p);
        st.scrollTo = st.res;
      }
    a.markUi("catalogs.groupCard.parent");
    if (parent) pathChips(st, w, parent);
  }
  const std::vector<Id> subs = rules::childGroups(w, gid);
  std::vector<const CatalogItem*> direct;
  for (const CatalogItem& r : w.catalogs->resources)
    if (r.group == gid) direct.push_back(&r);
  const int total = tree.count.count(gid) ? tree.count.at(gid) : 0;
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(fmtInt(i64(subs.size())), "Подгруппы", {.icon = "folder", .tone = ui::Tone::Accent});
    ui::stat(fmtInt(total), "Всего ресурсов", {.icon = "resource", .tone = ui::Tone::Info, .tooltip = "Ресурсы группы и её подгрупп"});
  }
  {
    ui::Row btns({ui::fr(1), ui::fr(1)}, 30, 8);
    if (ui::button("Новая подгруппа##card", {.icon = "folder", .fill = true, .disabled = ro})) addGroupAct(a, st, w, gid);
    a.markUi("catalogs.groupCard.addSub");
    if (ui::button("Новый ресурс##card", {.icon = "plus", .fill = true, .disabled = ro})) addResourceAct(a, st, w, gid);
    a.markUi("catalogs.groupCard.addRes");
  }
  if (!subs.empty()) {
    ui::Section sec("Подгруппы", "folder", {.badge = std::to_string(subs.size())});
    if (sec) {
      ChipFlow cf;
      for (Id s : subs) {
        const ResGroup* x = groupOf(w, s);
        if (!x) continue;
        ui::IdScope scope{i64(s) + 0x7300000LL};
        ui::ChipOpt co;
        co.icon = "folder";
        co.clickable = true;
        co.tooltip = "Выбрать подгруппу";
        const int n = tree.count.count(s) ? tree.count.at(s) : 0;
        if (edkit::chip(orName(x->name) + " · " + std::to_string(n), co) == ui::ChipAction::Click) {
          st.res = ResSel{ResSel::Group, s};
          revealGroup(st, w, gid);
          st.scrollTo = st.res;
        }
      }
    }
  }
  {
    ui::Section sec("Ресурсы группы", "resource", {.badge = std::to_string(direct.size())});
    a.markUi("catalogs.groupCard.resources");
    if (sec) {
      if (direct.empty()) ui::label(subs.empty() ? "Пусто" : "Все ресурсы — в подгруппах", {.ink = ui::Ink::Muted});
      else resourceChips(st, direct);
    }
  }
  ui::spacer(4);
  if (!rule) {
    ui::Disabled dis(ro);
    if (ui::button("Удалить группу: " + orName(g.name), {.variant = ui::Variant::Danger, .icon = "trash", .fill = true})) askRemoveGroup(a, w, gid);
    a.markUi("catalogs.groupCard.delete");
  }
}

void noGroupCard(App& a, State& st, const World& w, const Tree& tree) {
  const bool ro = a.readOnly();
  const ui::Theme& th = ui::theme();
  ui::Scroll sc("card");
  cardHead("folder", th.textMuted, "Ресурсы", "Без группы");
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(fmtInt(i64(tree.loose.size())), "Ресурсы без группы", {.icon = "resource", .tone = ui::Tone::Info});
    ui::stat(fmtInt(i64(w.catalogs->resGroups.size())), "Группы", {.icon = "folder", .tone = ui::Tone::Accent});
  }
  if (ui::button("Новый ресурс без группы", {.icon = "plus", .fill = true, .disabled = ro})) addResourceAct(a, st, w, 0);
  a.markUi("catalogs.noGroupCard.addRes");
  ui::Section sec("Ресурсы", "resource", {.badge = std::to_string(tree.loose.size())});
  if (sec) resourceChips(st, tree.loose);
}

}  // namespace

// ================================================================ вкладка
void drawResources(App& a, State& st) {
  const bool ro = a.readOnly();
  // Строка поиска и кнопок (мир до правок этого кадра).
  {
    const World w0 = a.world();
    Id target = 0;   // группа нового ресурса: выбранная группа или группа выбранного ресурса
    if (st.res.kind == ResSel::Group && groupOf(w0, st.res.id)) target = st.res.id;
    if (st.res.kind == ResSel::Resource)
      if (const CatalogItem* r = w0.resource(st.res.id); r && groupOf(w0, r->group)) target = r->group;
    ui::HStack hs(30, ui::Align::Left, 8);
    searchBox(a, st, 300, "Поиск ресурсов и групп");
    if (ui::iconButton("collapse", "Свернуть все группы")) {
      for (const ResGroup& g : w0.catalogs->resGroups) st.open[g.id] = false;
      st.noGroupOpen = false;
    }
    a.markUi("catalogs.collapseAll");
    if (ui::iconButton("expand", "Развернуть все группы")) {
      for (const ResGroup& g : w0.catalogs->resGroups) st.open[g.id] = true;
      st.noGroupOpen = true;
    }
    a.markUi("catalogs.expandAll");
    ui::flex();
    if (ui::button("Новая группа", {.icon = "folder", .disabled = ro})) addGroupAct(a, st, w0, 0);
    a.markUi("catalogs.newGroup");
    const std::string tip = target ? "Новый ресурс в группе «" + orName(groupOf(w0, target)->name) + "»" : std::string("Новый ресурс без группы");
    if (ui::button("Новый ресурс", {.variant = ui::Variant::Primary, .icon = "plus", .disabled = ro, .tooltip = tip, .shortcut = {Key::Insert, 0}}))
      addResourceAct(a, st, w0, target);
    a.markUi("catalogs.add");
  }
  ui::spacer(2);
  const World w = a.world();   // снимок кадра (после кнопок: новая запись уже в нём); правка посреди кадра заменяет мир
  // Показать строку (выбор из фишки, новая запись, смена группы): раскрыть предков.
  if (st.scrollTo.kind == ResSel::Resource) {
    if (const CatalogItem* r = w.resource(st.scrollTo.id)) {
      if (groupOf(w, r->group)) revealGroup(st, w, r->group);
      else st.noGroupOpen = true;
    }
  } else if (st.scrollTo.kind == ResSel::Group) {
    revealGroup(st, w, parentOf(w, st.scrollTo.id));
  }
  // Выбранная группа (ресурс) могла исчезнуть (Ctrl+Z, удаление).
  if (st.res.kind == ResSel::Group && !groupOf(w, st.res.id)) st.res = ResSel{};
  if (st.res.kind == ResSel::Resource && !w.resource(st.res.id)) st.res = ResSel{};
  auto use = usageMap(w, CatalogList::Resources);
  const Tree tree = buildTree(w, st, *use);
  const int n = int(tree.rows.size());
  // Выбор по умолчанию — первая строка.
  int selIdx = -1;
  for (int i = 0; i < n; i++)
    if (selOfRow(tree.rows[size_t(i)]) == st.res) selIdx = i;
  if (selIdx < 0 && st.res.kind == ResSel::None && n > 0) {
    st.res = selOfRow(tree.rows[0]);
    selIdx = 0;
  }
  // Поиск скрыл выбранную строку — выбор переходит на первую найденную.
  if (selIdx < 0 && !st.query.empty() && n > 0) {
    st.res = selOfRow(tree.rows[0]);
    selIdx = 0;
  }
  const int selBefore = selIdx;
  const Split sp = split(ui::avail());
  int toggled = -1;
  {
    const RectF T = sp.table;
    ui::Area ta(T, 0);
    ui::Scroll sc("tblscroll", T.h);
    // Числовые столбцы — значками с подсказками: названиям в дереве нужна ширина.
    const ui::Column cols[] = {
        {"Название", nullptr, ui::fr(2.8f, 240), ui::Align::Left, true},
        {"", "palette", ui::px(44), ui::Align::Left, false, "Цвет на карте и в списках"},
        {"", "image", ui::px(46), ui::Align::Left, false, "Значок ресурса"},
        {"", "province", ui::fr(0.8f, 64), ui::Align::Right, true, "Провинции, добывающие ресурс"},
        {"", "pickaxe", ui::fr(0.8f, 72), ui::Align::Right, true, "Добыча всех провинций за ход"},
        {"", "treasury", ui::fr(0.9f, 80), ui::Align::Right, true, "Сумма запасов всех фракций"},
        {"", nullptr, ui::px(96)},
    };
    const ui::WidgetId tblId = ui::id("tbl");
    ui::Table t("tbl", std::span<const ui::Column>(cols), n,
                {.rowHeight = kRowH, .selected = &selIdx, .emptyIcon = "resource", .emptyText = st.query.empty() ? "Справочник пуст" : "Ничего не найдено"});
    std::vector<int> pos;
    if (t.sortColumn() >= 0) pos = treeOrder(tree, t.sortColumn(), t.sortDescending());
    const bool desc = t.sortDescending();
    t.sort([&](int x, int y, int) {
      const int d = pos[size_t(x)] - pos[size_t(y)];
      return desc ? -d : d;
    });
    // Показать строку: прокрутка (таблица строит только видимые строки).
    if (st.scrollTo.kind != ResSel::None) {
      for (int i = 0; i < n; i++)
        if (selOfRow(tree.rows[size_t(i)]) == st.scrollTo) revealRow(sc, T.h, kHeadH, kRowH, pos.empty() ? i : pos[size_t(i)]);
      st.scrollTo = ResSel{};
    }
    Ctx c{a, st, w, tree, ro, selIdx};
    const ui::Mouse& m = ui::mouse();
    for (int i : t) {
      const TRow& row = tree.rows[size_t(i)];
      ui::IdScope s{i64(row.kind) * 0x100000000LL + i64(row.id)};
      const RectF rr = t.rowRect();
      const bool hovered = rr.contains(m.x, m.y) && !ui::anyModalOpen();
      if (row.kind == TRow::Res) resRow(c, t, i);
      else groupRow(c, t, i, selIdx == i, hovered);
    }
    toggled = c.toggled;
    for (int k = cName; k < cActions; k++)
      if (c.cells[size_t(k)].w > 0) a.markUi("catalogs.head." + std::to_string(k), RectF{c.cells[size_t(k)].x - 10, T.y, c.cells[size_t(k)].w + 20, kHeadH});
    // ← → при фокусе таблицы: свернуть и развернуть выбранную группу.
    if (ui::keyboardFocus() == tblId && selIdx >= 0 && selIdx < n && tree.rows[size_t(selIdx)].kind != TRow::Res) {
      const TRow& row = tree.rows[size_t(selIdx)];
      if (ui::keyPressed(Key::Right) && !row.open) {
        ui::consumeKey(Key::Right);
        toggled = selIdx;
      } else if (ui::keyPressed(Key::Left) && row.open) {
        ui::consumeKey(Key::Left);
        toggled = selIdx;
      }
    }
  }
  a.markUi("catalogs.table", sp.table);
  if (toggled >= 0) {
    const TRow& row = tree.rows[size_t(toggled)];
    if (row.kind == TRow::NoGroup) st.noGroupOpen = !row.open;
    else st.open[row.id] = !row.open;
  }
  if (selIdx != selBefore && selIdx >= 0 && selIdx < n) st.res = selOfRow(tree.rows[size_t(selIdx)]);
  // Карточка выбранной строки.
  if (!sp.card.empty()) {
    ui::Area ca(sp.card, 0);
    if (st.res.kind == ResSel::Resource && w.resource(st.res.id)) {
      resourceCard(a, st, w, *w.resource(st.res.id), useOf(*use, st.res.id));
    } else if (st.res.kind == ResSel::Group && groupOf(w, st.res.id)) {
      groupCard(a, st, w, tree, *groupOf(w, st.res.id));
    } else if (st.res.kind == ResSel::NoGroup) {
      noGroupCard(a, st, w, tree);
    } else {
      ui::spacer(std::max(0.f, sp.card.h * 0.3f));
      ui::emptyState("resource", st.query.empty() ? "Справочник пуст" : "Ничего не найдено");
    }
  }
  // Delete — удалить выбранный ресурс или группу (вне текстовых полей); F2 — переименовать группу.
  if (!ro && ui::shortcut({Key::Delete, 0})) {
    if (st.res.kind == ResSel::Resource) askRemove(a, CatalogList::Resources, st.res.id);
    else if (st.res.kind == ResSel::Group) askRemoveGroup(a, w, st.res.id);
  }
  if (!ro && st.res.kind == ResSel::Group && ui::shortcut({Key::F2, 0})) {
    st.editGroup = st.focusGroup = st.res.id;
    st.scrollTo = st.res;
  }
}

}  // namespace rg::app::cat
