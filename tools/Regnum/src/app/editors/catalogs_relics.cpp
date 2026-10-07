// Regnum — справочник «Реликвии» (ТЗ «Доработки», п.8 и 10): у реликвии — название и редкость, она уникальна и не
// имеет количества (rules::addRelic/setRelic/removeRelic). Строка подсвечена цветом редкости (свечение плитки слева,
// мягкая заливка и цветное название): обычная — белая, редкая — синяя, эпическая — фиолетовая, легендарная —
// оранжевая, эпохальная — красная. Название правится на месте (щелчок по нему), редкость — выбор с цветной точкой,
// «У кого» — фишка персонажа или «свободна». Карточка: крупная плитка, название, редкость, описание и владелец
// (rules::giveRelic/takeRelic).
#include <algorithm>

#include "app/editors/catalogs_internal.h"

namespace rg::app::cat {

using platform::Key;

namespace {

constexpr float kRowH = 44, kHeadH = 32;
enum Col : int { cTile, cName, cRarity, cHolder, cActions };

const char* rarityName(Rarity r) { return schema::rarity(r).name; }

// Выбор редкости: пункты с цветной точкой. true — выбрана другая.
bool rarityPicker(std::string_view id, Rarity& r, bool disabled) {
  std::vector<ui::Option> opts;
  for (int k = 0; k < int(Rarity::Count); k++) {
    ui::Option o;
    o.label = schema::kRarities[k].name;
    o.color = w::rarityColor(Rarity(k));
    opts.push_back(o);
  }
  int idx = int(r);
  if (!ui::combo(id, idx, std::span<const ui::Option>(opts), {.search = 0, .disabled = disabled, .tooltip = "Редкость"})) return false;
  if (idx < 0 || idx >= int(Rarity::Count) || Rarity(idx) == r) return false;
  r = Rarity(idx);
  return true;
}

// Правка одной части записи (остальное — как в мире).
void editRelic(App& a, Id id, std::string_view label, const std::function<void(Relic&)>& fn) {
  a.act(label, [&](Tx& tx) {
    const Relic* cur = tx.w().relic(id);
    if (!cur) fail("Реликвия не найдена");
    Relic r = *cur;
    fn(r);
    rules::setRelic(tx, id, r.name, r.rarity, r.desc);
  });
}

void setHolder(App& a, Id relic, Id was, Id who) {
  a.act(who ? "Владелец реликвии" : "Реликвия свободна", [&](Tx& tx) {
    if (who) rules::giveRelic(tx, who, relic);
    else if (was) rules::takeRelic(tx, was, relic);
  });
}

Id addRelicAct(App& a, State& st) {
  Id nid = 0;
  if (!a.act("Новая реликвия", [&](Tx& tx) { nid = rules::addRelic(tx, "", Rarity::Common); }) || !nid) return 0;
  st.query.clear();
  st.sel[kRelics] = nid;
  st.editRelic = nid;
  st.focusRelic = true;
  return nid;
}

void askRemoveRelic(App& a, const World& w, Id id) {
  const Relic* r = w.relic(id);
  if (!r) return;
  std::string text = "Реликвия «" + orName(r->name) + "» будет удалена";
  if (const Id who = rules::relicHolder(w, id)) text += " и убрана из инвентаря: " + w.characterName(who);
  text += ". Действие можно отменить Ctrl+Z.";
  a.confirm("Удалить реликвию?", text, "Удалить", true,
            [id](App& x) { x.act("Удалить реликвию", [&](Tx& tx) { rules::removeRelic(tx, id); }); });
}

// Фишка владельца (персонаж цвета фракции) в прямоугольнике строки; щелчок — открыть персонажа.
void holderChip(App& a, const World& w, Id who, RectF cell) {
  const Character* c = w.character(who);
  if (!c) return;
  const std::string name = orName(c->name, "Без имени");
  const float cw = std::min(cell.w, std::ceil(ui::measure(name, ui::Font::Small)) + 36);
  ui::at(RectF{cell.x, cell.cy() - 13, cw, 26});
  ui::ChipOpt co;
  co.color = c->faction ? w::factionColor(w, c->faction) : Color(0, 0, 0, 0);
  co.icon = c->hero ? "hero" : "character";
  co.clickable = true;
  co.tooltip = "Открыть персонажа";
  if (ui::chip(name, co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Character, who});
}

struct RRow {
  const Relic* r;
  Id holder;
};

void relicTable(App& a, State& st, const World& w, const std::vector<RRow>& rows, RectF T) {
  const bool ro = a.readOnly();
  const ui::Theme& th = ui::theme();
  Id& sel = st.sel[kRelics];
  int selIdx = -1;
  for (size_t i = 0; i < rows.size(); i++)
    if (rows[i].r->id == sel) selIdx = int(i);
  const int selBefore = selIdx;
  ui::Area ta(T, 0);
  ui::Scroll sc("tblscroll", T.h);
  const ui::Column cols[] = {
      {"", nullptr, ui::px(48)},
      {"Название", nullptr, ui::fr(2, 200), ui::Align::Left, true},
      {"Редкость", "relic", ui::px(196), ui::Align::Left, true},
      {"У кого", "hero", ui::fr(1.4f, 170), ui::Align::Left, true, "Персонаж, в инвентаре которого реликвия"},
      {"", nullptr, ui::px(40)},
  };
  ui::Table t("tbl", std::span<const ui::Column>(cols), int(rows.size()),
              {.rowHeight = kRowH, .selected = &selIdx, .emptyIcon = "relic", .emptyText = st.query.empty() ? "Реликвий пока нет" : "Ничего не найдено"});
  auto cmp = [&](int x, int y, int col) {
    const RRow& A = rows[size_t(x)];
    const RRow& B = rows[size_t(y)];
    if (col == cName) return compareRu(A.r->name, B.r->name);
    if (col == cRarity) return int(A.r->rarity) - int(B.r->rarity);
    if (col == cHolder) {
      if (!A.holder || !B.holder) return A.holder ? -1 : B.holder ? 1 : 0;   // свободные — в конце
      return compareRu(w.characterName(A.holder), w.characterName(B.holder));
    }
    return 0;
  };
  t.sort(cmp);
  if (st.editRelic && st.focusRelic)
    for (size_t i = 0; i < rows.size(); i++)
      if (rows[i].r->id == st.editRelic) revealRow(sc, T.h, kHeadH, kRowH, displayIndex(int(i), int(rows.size()), t.sortColumn(), t.sortDescending(), cmp));
  for (int i : t) {
    const Relic& r = *rows[size_t(i)].r;
    const Id id = r.id, who = rows[size_t(i)].holder;
    const Color rc = w::rarityColor(r.rarity);
    const RectF rr = t.rowRect();
    ui::IdScope s{i64(id)};
    // Подсветка редкостью: мягкая заливка слева.
    ui::draw::gradient(RectF{rr.x, rr.y, std::round(rr.w * 0.55f), rr.h}, rc.alpha(th.dark ? 0.11f : 0.09f), rc.alpha(0), 6, true);
    {
      const RectF cr = t.cell();
      tile(RectF{cr.x + 1, rr.cy() - 14, 28, 28}, "relic", rc, 8, true);
    }
    // Название: цветом редкости; щелчок — правка на месте (после отпускания кнопки).
    {
      const RectF cr = t.cell();
      const RectF nameR{cr.x, rr.cy() - 15, cr.w, 30};
      if (st.pendingRelic == id && !ui::mouse().down[0]) {
        st.pendingRelic = 0;
        st.editRelic = id;
        st.focusRelic = true;
      }
      if (st.editRelic == id && !ro) {
        const bool start = st.focusRelic;
        if (start) {
          ui::setKeyboardFocus(ui::id("name"));
          st.focusRelic = false;
        }
        std::string name = r.name;
        ui::at(nameR);
        if (ui::textField("name", name, {.placeholder = "Название реликвии", .maxLength = 80, .selectAllOnFocus = true}) && trim(name) != r.name) {
          const std::string n = trim(name);
          editRelic(a, id, "Переименовать реликвию", [&](Relic& x) { x.name = n; });
        }
        if (!start && !ui::lastItem().focused) st.editRelic = 0;
        a.markUi("catalogs.relic." + std::to_string(id) + ".name", nameR);
      } else {
        const std::string name = orName(r.name);
        const float tw = std::min(nameR.w, std::ceil(ui::measure(name, ui::Font::Strong)) + 2);
        const RectF hit{nameR.x, nameR.y, std::min(nameR.w, tw + 8), nameR.h};
        ui::Interaction it = ui::interact(ui::id("##name"), hit);
        if (it.hovered && !ro) ui::setCursor(platform::Cursor::IBeam);
        if (it.pressed) selIdx = i;
        if (it.clicked && !ro) st.pendingRelic = id;
        ui::draw::text(name, nameR, ui::Font::Strong, rc);
        if (it.hovered) ui::draw::line(nameR.x, nameR.cy() + 9, nameR.x + tw, nameR.cy() + 9, rc.alpha(0.45f), 1);
        a.markUi("catalogs.relic." + std::to_string(id) + ".name", hit);
      }
      if (id == sel) a.markUi("catalogs.selected.name", nameR);
    }
    t.cell();
    {
      Rarity rar = r.rarity;
      if (rarityPicker("rarity", rar, ro)) editRelic(a, id, "Редкость реликвии", [&](Relic& x) { x.rarity = rar; });
      a.markUi("catalogs.relic." + std::to_string(id) + ".rarity");
    }
    {
      const RectF cr = t.cell();
      if (who) holderChip(a, w, who, cr);
      else ui::draw::text("свободна", cr, ui::Font::Body, th.textMuted);
    }
    t.cell();
    if (ui::iconButton("trash", "Удалить реликвию", {.size = ui::Size::Small, .disabled = ro, .tone = ui::Tone::Danger})) askRemoveRelic(a, w, id);
    a.markUi("catalogs.relic." + std::to_string(id), rr);
  }
  if (selIdx != selBefore && selIdx >= 0 && selIdx < int(rows.size())) sel = rows[size_t(selIdx)].r->id;
}

void relicCard(App& a, State& st, const World& w, const Relic& r, Id holder) {
  const bool ro = a.readOnly();
  const Id id = r.id;
  const Color rc = w::rarityColor(r.rarity);
  ui::Scroll sc("card");
  // Крупная плитка с подсветкой редкости.
  {
    ui::Row head({ui::px(64), ui::fr(1)}, 64, 14);
    const RectF tr = ui::next(64, 64);
    tile(tr.inset(2), "relic", rc, 14, true);
    ui::Group g(0, 2);
    ui::caption(std::string("Реликвия · ") + rarityName(r.rarity));
    ui::label(orName(r.name), {.font = ui::Font::Title, .color = rc});
  }
  {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::tag(holder ? "У персонажа" : "Свободна", holder ? ui::Tone::Info : ui::Tone::Neutral, holder ? "inventory" : "relic");
  }
  {
    ui::prop("Название", "edit", 0.3f);
    std::string name = r.name;
    if (ui::textField("cardname", name, {.placeholder = "Название реликвии", .maxLength = 80, .readOnly = ro, .selectAllOnFocus = true}) &&
        trim(name) != r.name) {
      const std::string n = trim(name);
      editRelic(a, id, "Переименовать реликвию", [&](Relic& x) { x.name = n; });
    }
    a.markUi("catalogs.relicCard.name");
  }
  {
    ui::prop("Редкость", "relic", 0.3f);
    Rarity rar = r.rarity;
    if (rarityPicker("cardrarity", rar, ro)) editRelic(a, id, "Редкость реликвии", [&](Relic& x) { x.rarity = rar; });
    a.markUi("catalogs.relicCard.rarity");
  }
  {
    ui::prop("Владелец", "inventory", 0.3f);
    Id who = holder;
    if (w::characterPicker("holder", who, 0, "Свободна", false, ro)) setHolder(a, id, holder, who);
    a.markUi("catalogs.relicCard.holder");
    if (const Character* c = w.character(holder)) {
      ChipFlow cf;
      ui::IdScope s{i64(holder) + 0x7400000LL};
      ui::ChipOpt co;
      co.icon = c->hero ? "hero" : "character";
      co.clickable = true;
      co.tooltip = "Открыть персонажа";
      if (edkit::chip(orName(c->name, "Без имени"), co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Character, holder});
      if (c->faction) {
        ui::IdScope s2{i64(c->faction) + 0x7500000LL};
        ui::ChipOpt fo;
        fo.color = w::factionColor(w, c->faction);
        fo.clickable = true;
        fo.tooltip = "Открыть фракцию";
        if (edkit::chip(w.factionName(c->faction), fo) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, c->faction});
      }
    }
  }
  {
    ui::caption("Описание");
    std::string desc = r.desc;
    if (ui::textArea("desc", desc, 96, {.placeholder = "Описание", .readOnly = ro}) && desc != r.desc)
      editRelic(a, id, "Описание реликвии", [&](Relic& x) { x.desc = desc; });
    a.markUi("catalogs.relicCard.desc");
  }
  ui::spacer(4);
  {
    ui::Disabled dis(ro);
    if (ui::button("Удалить: " + orName(r.name, "реликвия"), {.variant = ui::Variant::Danger, .icon = "trash", .fill = true})) askRemoveRelic(a, w, id);
    a.markUi("catalogs.delete");
  }
  (void)st;
}

}  // namespace

void drawRelics(App& a, State& st) {
  const bool ro = a.readOnly();
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    searchBox(a, st, 300, "Поиск реликвий");
    // Число реликвий по редкости (цветные точки).
    int counts[int(Rarity::Count)] = {};
    for (const Relic& r : a.world().catalogs->relics) counts[int(r.rarity) >= 0 && r.rarity < Rarity::Count ? int(r.rarity) : 0]++;
    for (int k = 0; k < int(Rarity::Count); k++) {
      if (!counts[k]) continue;
      const RectF d = ui::next(10, 30);
      ui::draw::circle(d.cx(), d.cy(), 4.5f, w::rarityColor(Rarity(k)));
      ui::label(std::to_string(counts[k]), {.font = ui::Font::Small, .ink = ui::Ink::Dim, .tooltip = schema::kRarities[k].name});
    }
    ui::flex();
    if (ui::button("Новая реликвия", {.variant = ui::Variant::Primary, .icon = "plus", .disabled = ro, .shortcut = {Key::Insert, 0}})) addRelicAct(a, st);
    a.markUi("catalogs.add");
  }
  ui::spacer(2);
  const World w = a.world();   // снимок кадра (после кнопок: новая запись уже в нём); правка посреди кадра заменяет мир
  std::vector<RRow> rows;
  for (const Relic& r : w.catalogs->relics) {
    if (!st.query.empty() && !utf8::matches(r.name, st.query) && !utf8::matches(r.desc, st.query)) continue;
    rows.push_back(RRow{&r, rules::relicHolder(w, r.id)});
  }
  Id& sel = st.sel[kRelics];
  if (std::none_of(rows.begin(), rows.end(), [&](const RRow& x) { return x.r->id == sel; })) sel = rows.empty() ? 0 : rows.front().r->id;
  const Split sp = split(ui::avail());
  relicTable(a, st, w, rows, sp.table);
  a.markUi("catalogs.table", sp.table);
  if (!sp.card.empty()) {
    ui::Area ca(sp.card, 0);
    const RRow* cur = nullptr;
    for (const RRow& x : rows)
      if (x.r->id == sel) cur = &x;
    if (cur) {
      relicCard(a, st, w, *cur->r, cur->holder);
    } else {
      ui::spacer(std::max(0.f, sp.card.h * 0.3f));
      if (st.query.empty()) {
        if (ui::emptyState("relic", "Реликвий пока нет", ro ? std::string_view() : std::string_view("Новая реликвия"), "plus")) addRelicAct(a, st);
      } else {
        ui::emptyState("relic", "Ничего не найдено");
      }
    }
  }
  if (!ro && sel && ui::shortcut({Key::Delete, 0})) askRemoveRelic(a, w, sel);
  if (!ro && sel && ui::shortcut({Key::F2, 0})) {
    st.editRelic = sel;
    st.focusRelic = true;
  }
}

}  // namespace rg::app::cat
