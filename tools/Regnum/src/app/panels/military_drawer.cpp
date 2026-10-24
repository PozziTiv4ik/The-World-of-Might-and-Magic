// Regnum — выдвижная панель «Войска»: все войска и флоты на карте (значок — портрет главного полководца в круге или
// фигурка цвета фракции, численность), поиск, отбор по виду и фракции; щелчок выделяет объект и показывает его на карте.
// Новые объекты любой фракции (инструменты карты) — правка мира: у игрока кнопок нет (его войска — во вкладках
// государства). У игрока (аудит 2026-10-23, раздел 11 и 1.5): у своих объектов — отметка очка действий (молния —
// может ходить, песочные часы — ход сделан) и отбор «Может ходить»; численность чужих объектов скрыта (App::knows).
#include "app/panels/military.h"

namespace rg::app::mil {

namespace {

struct DrawerState {
  std::string query;
  int kind = 0;     // 0 — все, 1 — войска, 2 — флот
  Id faction = 0;   // 0 — все фракции
  bool canMove = false;   // игрок: только свои объекты с очком действий
};

// Отметка очка действий своего объекта игрока поверх значка строки списка.
void actionMark(App& a, const World& w, const Army& ar, RectF row) {
  const bool can = rules::hasAction(w, ar.id);
  const ui::Theme& th = ui::theme();
  const RectF m{row.x + 20, row.cy() + 1, 13, 13};
  ui::draw::circle(m.cx(), m.cy(), 7.5f, th.surface1);
  ui::draw::icon(can ? "bolt" : "hourglass", m.inset(1), can ? th.success : th.textMuted);
  const char* what = ar.isFleet() ? "Флот" : "Войско";
  ui::hoverTip("##act", m.expand(2), can ? std::string(what) + " может ходить" : std::string("Ход сделан"));
  a.markUi("mil.drawer.action." + std::to_string(ar.id), m);
}

void drawDrawer(App& a) {
  const World& w = frameWorld(a);
  auto& st = ui::state<DrawerState>(ui::id("military-drawer"));
  if (st.faction && !w.faction(st.faction)) st.faction = 0;
  const Id own = a.playerState();
  if (!own) st.canMove = false;
  if (a.admin()) {
    ui::Disabled dis(a.readOnly());
    ui::Row r({ui::fr(1), ui::fr(1)}, 30, 8);
    if (ui::button("Войско", {.icon = "tool-army", .fill = true, .tooltip = "Поставить новое войско на карту (A)"}))
      startPlacing(a, ArmyKind::Army, st.faction ? st.faction : lastPlaceFaction());
    a.markUi("mil.drawer.newArmy");
    if (ui::button("Флот", {.icon = "tool-fleet", .fill = true, .tooltip = "Поставить новый флот на карту (Shift+A)"}))
      startPlacing(a, ArmyKind::Fleet, st.faction ? st.faction : lastPlaceFaction());
    a.markUi("mil.drawer.newFleet");
  }
  ui::searchField("q", st.query, "Название, фракция, провинция");
  a.markUi("mil.drawer.search");
  {
    const ui::Len cols[] = {ui::fr(1), ui::px(30)};
    ui::Row r(std::span<const ui::Len>(cols, own ? 2 : 1), 30, 6);
    ui::segmented("kind", st.kind, {{nullptr, "Все", "Войска и флоты"}, {"army", "Войска", "Только войска"}, {"fleet", "Флот", "Только флоты"}},
                  {.size = ui::Size::Small});
    a.markUi("mil.drawer.kind");
    if (own) {
      ui::iconToggle("bolt", "Может ходить", st.canMove, {.size = ui::Size::Small});
      a.markUi("mil.drawer.canMove");
    }
  }
  w::factionPicker("faction", st.faction, w::FactionFilter::Any, "Все фракции");
  a.markUi("mil.drawer.faction");

  struct Item {
    const Army* army;
    std::string sub;
    std::string key;
    bool hidden;
  };
  std::vector<Item> items;
  i64 armies = 0, fleets = 0, units = 0;
  w.armies.each([&](const Army& ar) {
    if (st.kind == 1 && ar.isFleet()) return;
    if (st.kind == 2 && !ar.isFleet()) return;
    if (st.canMove && (ar.leader() != own || !rules::hasAction(w, ar.id))) return;
    std::vector<Id> fs = factionsIn(ar);
    if (st.faction && std::find(fs.begin(), fs.end(), st.faction) == fs.end()) return;
    std::vector<std::string> names;
    for (Id f : fs) names.push_back(w.factionName(f));
    std::string prov = w.provinceName(provinceUnder(w, ar.pos));
    if (const Id fl = rules::carrierOf(w, ar.id)) prov = "на борту «" + objectName(*w.army(fl)) + "»";   // войско на борту флота
    std::string sub = join(names, " + ") + " · " + prov;
    if (!st.query.empty() && !utf8::matches(ar.name + " " + sub, st.query)) return;
    // Туман войны: численность объекта, где нет ни одной известной игроку фракции, скрыта.
    bool known = false;
    for (Id f : fs) known = known || a.knows(f);
    (ar.isFleet() ? fleets : armies)++;
    if (known) units += unitCount(ar);
    items.push_back(Item{&ar, sub, w.factionName(ar.leader()), !known});
  });
  std::sort(items.begin(), items.end(), [](const Item& x, const Item& y) {
    if (int c = compareRu(x.key, y.key); c != 0) return c < 0;
    if (x.army->kind != y.army->kind) return x.army->kind < y.army->kind;
    return compareRu(x.army->name, y.army->name) < 0;
  });
  {
    ui::HStack hs(20, ui::Align::Left, 6);
    ui::caption(std::to_string(armies) + " " + plural(armies, "войско", "войска", "войск") + " · " + std::to_string(fleets) + " " +
                plural(fleets, "флот", "флота", "флотов"));
    ui::flex();
    ui::label(fmtShort(double(units)), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "users", .tooltip = "Численность в найденных объектах"});
  }
  if (items.empty()) {
    if (w.armies.empty()) {
      ui::Disabled dis(a.readOnly());
      if (ui::emptyState("army", "Войск и флотов на карте пока нет.", a.admin() ? "Новое войско" : "", "tool-army"))
        startPlacing(a, ArmyKind::Army, st.faction ? st.faction : lastPlaceFaction());
    } else {
      if (ui::emptyState("search", "Ничего не найдено.", "Сбросить отбор", "close")) st = DrawerState{};
    }
    return;
  }
  for (const Item& it : items) {
    if (objectItem(w, *it.army, a.ui.sel == Selection{SelType::Army, it.army->id}, it.sub, it.hidden)) a.select(SelType::Army, it.army->id, true);
    const RectF row = ui::lastItem().rect;
    a.markUi("mil.drawer.item." + std::to_string(it.army->id), row);
    if (it.hidden) a.markUi("mil.drawer.hidden." + std::to_string(it.army->id), row);
    if (own && it.army->leader() == own) {
      ui::IdScope s{i64(it.army->id)};
      actionMark(a, w, *it.army, row);
    }
  }
}

DrawerReg drawer({"military", "army", "Войска и флот", 50, drawDrawer, "Ctrl+5"});

}  // namespace
}  // namespace rg::app::mil
