// Regnum — разделы «Государства» (ТЗ 1.b.ii) и «Гильдии» (ТЗ 1.d.i) правой ленты: каталоги-таблицы с флагом (звезда —
// основное игровое государство), названием, правителем или государством расположения, провинциями или штабами,
// населением, казной, доходом и войсками; сортировка по шапке, поиск, создание (приятный различимый цвет и флаг),
// контекстное меню (открыть, показать на карте, флаг, удалить с подтверждением). Щелчок открывает страницу фракции.
#include "app/panels/faction_common.h"

namespace rg::app {
namespace {

using namespace fac;

struct ListState {
  std::string q;
  Id menu = 0;       // фракция контекстного меню
};

// Флаг и название в ячейке таблицы; звезда — основное игровое государство, замок — государственная гильдия.
void nameCell(const Faction& f, RectF cell) {
  const ui::Theme& t = ui::theme();
  RectF fr{cell.x, cell.cy() - 12, 36, 24};
  ui::at(fr);
  ui::flag(f.flag, 36, 24, 3);
  const char* mark = f.isState() && f.mainState ? "star-filled" : f.stateGuild ? "lock" : nullptr;
  if (mark) {
    RectF m{fr.right() - 8, fr.bottom() - 9, 13, 13};
    ui::draw::circle(m.cx(), m.cy(), 7.5f, t.surface2);
    ui::draw::icon(mark, m.inset(1), t.accent);
  }
  ui::draw::text(displayName(f), RectF{fr.right() + 12, cell.y, cell.right() - fr.right() - 12, cell.h}, ui::Font::Strong, t.text);
}

// Каталог раздела «Государства» или «Гильдии»: таблица с сортировкой по шапке (провинции или штабы, население,
// казна, доход, войска), поиск, создание; щелчок — страница фракции, правый щелчок — меню.
void drawList(App& a, FactionKind kind) {
  const World& w = frameWorld(a);
  const bool ro = a.readOnly();
  const bool states = kind == FactionKind::State;
  const std::string pre = states ? "states" : "guilds";
  auto calc = rules::calc(w);
  ListState& st = ui::state<ListState>(ui::id("##list"));

  // Поиск и создание.
  {
    ui::Row r({ui::fr(1), ui::px(30)}, 30, 6);
    ui::searchField("q", st.q, states ? "Найти государство" : "Найти гильдию");
    a.markUi(pre + ".search");
    if (ui::iconButton("plus", states ? "Новое государство" : "Новая гильдия", {.variant = ui::Variant::Secondary, .disabled = ro}))
      createFactionUi(a, kind);
    a.markUi(pre + ".add");
  }

  std::vector<const Faction*> all, list;
  w.factions.each([&](const Faction& f) {
    if (f.kind == kind) all.push_back(&f);
  });
  for (const Faction* f : all) {
    std::string sub = factionSubtitle(w, *f);
    if (!st.q.empty() && !utf8::matches(f->name + " " + sub, st.q)) continue;
    list.push_back(f);
  }
  std::stable_sort(list.begin(), list.end(), [&](const Faction* x, const Faction* y) { return compareRu(displayName(*x), displayName(*y)) < 0; });

  ui::spacer(2);
  if (all.empty()) {
    if (ui::emptyState(states ? "crown" : "guild", states ? "Государств пока нет." : "Гильдий пока нет.", ro ? "" : (states ? "Новое государство" : "Новая гильдия"),
                       "plus"))
      createFactionUi(a, kind);
    return;
  }
  {
    std::string cap = st.q.empty() ? std::to_string(all.size()) + " " + (states ? plural(i64(all.size()), "государство", "государства", "государств")
                                                                            : plural(i64(all.size()), "гильдия", "гильдии", "гильдий"))
                                   : "Найдено: " + std::to_string(list.size());
    ui::caption(cap);
  }
  if (list.empty()) {
    ui::emptyState("search", "Ничего не найдено.");
    return;
  }

  enum Col { CName, CSub, CLands, CPop, CTreasury, CNet, CForces };
  const bool narrow = ui::avail().w < 640;
  std::vector<ui::Column> cols = {{"Название", nullptr, ui::fr(1.5f, 180), ui::Align::Left, true},
                                  {states ? "Правитель" : "Расположение", nullptr, ui::fr(1, 120), ui::Align::Left, true}};
  std::vector<Col> keys = {CName, CSub};
  auto col = [&](Col k, ui::Column c) {
    cols.push_back(c);
    keys.push_back(k);
  };
  col(CLands, {"", states ? "province" : "hq", ui::px(64), ui::Align::Right, true, states ? "Провинции" : "Штабы"});
  if (states && !narrow) col(CPop, {"", "population", ui::px(84), ui::Align::Right, true, "Население"});
  col(CTreasury, {"", "coins", ui::px(84), ui::Align::Right, true, "Казна"});
  if (!narrow) col(CNet, {"", "trend-up", ui::px(84), ui::Align::Right, true, "Чистый доход за ход"});
  if (!narrow) col(CForces, {"", "army", ui::px(76), ui::Align::Right, true, "Войска и флот: общая численность"});

  auto num = [&](const Faction* f, Col k) -> double {
    const rules::FactionCalc* fc = calc->faction(f->id);
    switch (k) {
      case CLands: return fc ? double(fc->provinces.size()) : 0;
      case CPop: return fc ? double(fc->population) : 0;
      case CTreasury: return f->treasury();
      case CNet: return fc ? fc->net : 0;
      case CForces: return fc ? double(fc->armyTotal + fc->fleetTotal) : 0;
      default: return 0;
    }
  };
  int sel = -1;
  for (size_t i = 0; i < list.size(); i++)
    if (a.ui.sel == Selection{SelType::Faction, list[i]->id}) sel = int(i);
  ui::Table t(pre, cols, int(list.size()), {.rowHeight = 46, .selected = &sel, .emptyIcon = states ? "crown" : "guild"});
  t.sort([&](int x, int y, int c) {
    const Col k = c >= 0 && c < int(keys.size()) ? keys[size_t(c)] : CName;
    const Faction* fx = list[size_t(x)];
    const Faction* fy = list[size_t(y)];
    if (k == CName) return compareRu(displayName(*fx), displayName(*fy));
    if (k == CSub) return compareRu(factionSubtitle(w, *fx), factionSubtitle(w, *fy));
    double u = num(fx, k), v = num(fy, k);
    return u < v ? -1 : u > v ? 1 : 0;
  });
  for (int i : t) {
    const Faction& f = *list[size_t(i)];
    ui::IdScope sc{i64(f.id)};
    nameCell(f, t.cell());
    t.text(factionSubtitle(w, f), ui::Ink::Dim, ui::Font::Small);
    const rules::FactionCalc* fc = calc->faction(f.id);
    t.text(fmtInt(i64(num(&f, CLands))));
    if (states && !narrow) t.text(fmtShort(double(fc ? fc->population : 0)));
    t.text(money(f.treasury()), f.treasury() < 0 ? ui::Ink::Danger : ui::Ink::Normal);
    if (!narrow) {
      double net = fc ? fc->net : 0;
      t.text(std::fabs(net) < 0.0005 ? std::string("—") : moneySigned(net), net > 0.0005 ? ui::Ink::Success : net < -0.0005 ? ui::Ink::Danger : ui::Ink::Muted);
      t.text(fmtShort(num(&f, CForces)), ui::Ink::Dim);
    }
    a.markUi(pre + ".row." + std::to_string(f.id), t.rowRect());
  }
  if (int c = t.clicked(); c >= 0 && c < int(list.size())) a.select(SelType::Faction, list[size_t(c)]->id);
  if (int c = t.rightClicked(); c >= 0 && c < int(list.size())) {
    st.menu = list[size_t(c)]->id;
    ui::openContextMenu("ctx");
  }
  if (ui::beginMenu("ctx")) {
    const Faction* f = w.faction(st.menu);
    if (f) {
      Id fid = f->id;
      ui::menuHeader(displayName(*f));
      if (ui::menuItem("Открыть", {.icon = "info"})) a.select(SelType::Faction, fid);
      if (ui::menuItem("Показать на карте", {.icon = "target"})) a.select(SelType::Faction, fid, true);
      if (ui::menuItem("Изменить флаг…", {.icon = "flag", .disabled = ro})) openFlagEditor(a, fid);
      if (f->isState() && ui::menuItem("Государственная гильдия", {.icon = "guild", .disabled = ro})) createStateGuildUi(a, fid);
      ui::menuSeparator();
      if (ui::menuItem(states ? "Удалить государство" : "Удалить гильдию", {.icon = "trash", .danger = true, .disabled = ro})) confirmDelete(a, fid);
      a.markUi(pre + ".ctx.delete");
    }
    ui::endMenu();
  }
}

void drawStates(App& a) { drawList(a, FactionKind::State); }
void drawGuildList(App& a) { drawList(a, FactionKind::Guild); }

SectionReg statesSection({"states", "crown", "Государства", 20, "Ctrl+2", drawStates, SelType::Faction, isState, "Все государства"});
SectionReg guildsSection({"guilds", "guild", "Торговые гильдии", 30, "Ctrl+3", drawGuildList, SelType::Faction, isGuild, "Все гильдии"});

}  // namespace
}  // namespace rg::app
