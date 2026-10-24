// Regnum — окно справочников: общее для вкладок (catalogs_internal.h) — состояние окна, описания простых справочников,
// оформление записей, таблица в своей прокрутке, действия с записями.
#include <numeric>

#include "app/editors/catalogs_internal.h"
#include "gfx/icons.h"

namespace rg::app::cat {

using platform::Key;
using rules::CatalogList;

namespace {

const ListDef kLists[] = {
    {CatalogList::Resources, kResources, "resource", "Ресурсы", "Ресурс", "Новый ресурс", true},
    {CatalogList::Races, kRaces, "race", "Расы", "Раса", "Новая раса", true},
    {CatalogList::Cultures, kCultures, "culture", "Культуры", "Культура", "Новая культура", true},
    {CatalogList::Religions, kReligions, "religion", "Религии", "Религия", "Новая религия", true},
    {CatalogList::Governments, kGovernments, "crown", "Формы правления", "Форма правления", "Новая форма правления", false},
    {CatalogList::Positions, kPositions, "council", "Должности", "Должность", "Новая должность", false},
    {CatalogList::Essences, kEssences, "essence", "Эссенции", "Эссенция", "Новая эссенция", true},
};

}  // namespace

const ListDef* defOfTab(int tab) {
  for (const ListDef& d : kLists)
    if (d.tab == tab) return &d;
  return nullptr;
}

// ================================================================ состояние, записи, оформление
State& state(App&) { return sessionState<State>(); }   // другой мир — чистое состояние

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

std::string nb(i64 n, const char* one, const char* few, const char* many) { return fmtInt(n) + "\xC2\xA0" + plural(n, one, few, many); }

bool newButton(App& a, std::string_view label, std::string_view tip) {
  const bool clicked = ui::button(label, {.variant = ui::Variant::Primary, .icon = "plus", .disabled = !a.canEdit(), .tooltip = tip,
                                          .shortcut = {Key::Insert, 0}});
  a.markUi("catalogs.add");
  return clicked;
}

bool deleteButton(App& a, std::string_view what) {
  ui::Disabled dis(!a.canEdit());
  const bool clicked = ui::button("Удалить: " + std::string(what), {.variant = ui::Variant::Danger, .icon = "trash", .fill = true});
  a.markUi("catalogs.delete");
  return clicked;
}

void treeGuides(RectF cell, int depth) {
  const ui::Theme& th = ui::theme();
  for (int k = 0; k < depth; k++) {
    const float x = std::round(cell.x + float(k) * kTreeIndent + kTreeChevW * 0.5f) + 0.5f;
    ui::draw::line(x, cell.y - 3, x, cell.bottom() + 3, th.border, 1);
  }
}

void countPill(RectF r, std::string_view text, bool selected) {
  const ui::Theme& th = ui::theme();
  ui::draw::rect(r, selected ? th.accent.alpha(0.22f) : th.surface3, r.h * 0.5f);
  ui::draw::text(text, r, ui::Font::Caption, selected ? th.accent : th.textDim, ui::Align::Center);
}
float countPillW(std::string_view text) { return std::max(22.f, std::ceil(ui::measure(text, ui::Font::Caption)) + 12); }

bool newSubButton(bool disabled) {
  const bool r = ui::iconButton("folder", "Новая подгруппа", {.size = ui::Size::Small, .disabled = disabled});
  const RectF b = ui::lastItem().rect;
  const ui::Theme& th = ui::theme();
  const float cx = b.right() - 6, cy = b.bottom() - 6;
  ui::draw::circle(cx, cy, 5.5f, th.surface2);
  ui::draw::icon("plus", RectF{cx - 4.5f, cy - 4.5f, 9, 9}, disabled ? th.textMuted : th.accent);
  return r;
}

void tile(RectF r, const char* icon, Color tint, float radius, bool glow) {
  tint = legible(tint);
  if (glow) {
    ui::draw::shadow(r, radius, 14, tint.alpha(0.55f), 0, 1);
    ui::draw::gradient(r, tint.alpha(0.34f), tint.alpha(0.12f), radius);
  } else {
    ui::draw::rect(r, tint.alpha(0.2f), radius);
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
    ui::draw::rect(t, col.alpha(0.22f), 7);
    ui::draw::icon(itemIcon(l, c), t.inset(size * 0.2f), col);
  } else if (d.colored) {
    ui::draw::rect(t, c.color.alpha(0.22f), 7);
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

void heroModList(App& a, const std::string& mark, const std::vector<Id>& cur, bool ro, std::string_view label,
                 const std::function<void(Tx&, const std::vector<Id>&)>& set) {
  std::vector<Id> ids = cur;
  w::ModEdit ed;
  const bool changed = w::modifierList(mark, ids, ro, w::ModScope::Hero, nullptr, &ed);
  a.markUi(mark);   // поле добавления — последний элемент списка
  if (!changed) return;
  a.act(label, [&](Tx& tx) {
    if (!ed.addKey.empty()) ids.push_back(rules::ensureBuiltinMod(tx, ed.addKey));
    std::vector<Id> out;
    for (Id m : ids) {
      const Modifier* x = tx.w().modifier(m);
      if (!x) fail("Модификатор не найден");
      if (std::find(out.begin(), out.end(), m) != out.end()) continue;
      if (x->kind != ModKind::Any && x->kind != ModKind::Hero)
        fail("Модификатор «" + x->name + "» не для героев: подходят модификаторы видов «Везде» и «Для героев»");
      if (x->key == schema::mod::Dead || x->key == schema::mod::Captive || x->key == schema::mod::Arrested || schema::isNatureKey(x->key) ||
          schema::isAutoKey(x->key))
        fail("Модификатор «" + x->name + "» — состояние или природа героя либо ставится сам");
      out.push_back(m);
    }
    set(tx, out);
  });
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
  a.confirmAct(std::string("Удалить: ") + d.noun + "?", text, "Удалить", "Удалить из справочника",
               [l, id](Tx& tx) { rules::removeCatalogItem(tx, l, id); });
}

}  // namespace rg::app::cat
