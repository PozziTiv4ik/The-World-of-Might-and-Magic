// Regnum — войска и флот фракции: карточка выбранной строки (правка, раса, цена, формирование и роспуск,
// размещение отрядов) и список формирующихся. Роли (ТЗ «Доработки №4», п.5): формирование, роспуск, удаление строки,
// ключевой ресурс и отмена формирования — игровые действия за фракцию; название, тип, раса, содержание, цена наёмников и
// «Правка резерва» — правка мира.
#include "app/panels/military_internal.h"

namespace rg::app::mil::forces {

namespace {

// Где стоят отряды строки: войска (флоты), гарнизоны, оккупационные гарнизоны, торговля.
void deploymentChips(App& a, Id fid, Id row, bool fleet) {
  const World& w = frameWorld(a);
  struct Place {
    SelType type;
    Id id;
    std::string label;
    i64 n;
    const char* icon;
  };
  std::vector<Place> places;
  w.armies.each([&](const Army& ar) {
    if (ar.isFleet() != fleet) return;
    for (const ArmyGroup& g : ar.groups)
      if (g.faction == fid)
        if (i64 n = rowCount(g, row); n > 0) places.push_back({SelType::Army, ar.id, objectName(ar), n, fleet ? "fleet" : "army"});
  });
  if (!fleet) {
    w.provinces.each([&](const Province& p) {
      i64 n = 0, occ = 0;
      if (p.owner == fid)
        for (const GarrisonEntry& g : p.garrison)
          if (g.row == row) n += g.count;
      if (p.occupied && p.occupier == fid)
        for (const GarrisonEntry& g : p.occGarrison)
          if (g.row == row) occ += g.count;
      const std::string pn = p.name.empty() ? std::string("—") : p.name;
      if (n > 0) places.push_back({SelType::Province, p.id, "Гарнизон: " + pn, n, "castle"});
      if (occ > 0) places.push_back({SelType::Province, p.id, "Оккупация: " + pn, occ, "occupied"});
    });
  } else if (const Faction* f = w.faction(fid)) {
    i64 n = 0;
    for (const GarrisonEntry& g : f->tradeFleet)
      if (g.row == row) n += g.count;
    if (n > 0) places.push_back({SelType::None, 0, "Торговля", n, "trade"});
  }
  if (places.empty()) {
    ui::label(fleet ? "Все корабли в резерве" : "Все отряды в резерве", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = fleet ? "anchor" : "shield"});
    return;
  }
  ui::ChipFlow chipFlow;   // фишки переносятся по ширине
  int k = 0;
  for (const Place& p : places) {
    ui::IdScope s(k++);
    ui::ChipOpt co;
    co.icon = p.icon;
    co.color = w::factionColor(w, fid);
    co.clickable = p.type != SelType::None;
    if (p.type == SelType::Army) co.tooltip = "Открыть и показать на карте";
    else if (p.type == SelType::Province) co.tooltip = "Открыть провинцию";
    else co.tooltip = "Корабли в торговле";
    if (ui::flowChip(p.label + " · " + fmtCount(p.n), co) == ui::ChipAction::Click && p.type != SelType::None) a.select(p.type, p.id, true);
  }
}

}  // namespace

// Карточка выбранной строки: правка (узкая таблица), раса, цена найма, формирование и роспуск, размещение отрядов.
bool rowCard(App& a, Id fid, bool fleet, ForcesState& st, bool editors, Shown inTable) {
  const World& w = frameWorld(a);
  auto r = unitRow(w, fid, st.selRow, fleet);
  if (!r) return false;
  const ArmyRow* ar = fleet ? nullptr : armyRowOf(w, fid, r->id);
  auto c = rules::calc(w);
  const rules::RowCalc* rc = rowCalc(*c, fid, r->id, fleet);
  i64 field = rc ? rc->field : 0, reserve = rc ? rc->reserve : r->total, forming = rc ? rc->forming : 0;
  const bool ro = !a.canEdit(), noPlay = !a.canPlay(fid);
  const bool special = r->special != 0;
  const ui::Theme& th = ui::theme();
  ui::IdScope scope(i64(r->id));
  ui::Card card({.pad = 12, .tone = r->keyMissing ? ui::Tone::Danger : ui::Tone::Accent});
  {
    ui::Row head({ui::px(36), ui::fr(1), ui::px(30)}, 36, 10);
    const RectF tr = ui::next(36, 36);
    typeTile(tr, r->icon, r->keyMissing ? th.danger : th.accent);
    if (special) {
      const RectF b{tr.right() - 11, tr.y - 5, 16, 16};
      ui::draw::circle(b.cx(), b.cy(), 9, th.surface2);
      ui::draw::circle(b.cx(), b.cy(), 8, th.accent);
      ui::draw::icon("special-unit", b.inset(3), th.onAccent);
    }
    {
      // Подпись — тип (если название с ним не совпадает) и отметка особого отряда; без подписи название — по центру.
      std::string sub = r->name != r->typeName ? std::string(r->typeName) : std::string();
      if (special) sub += sub.empty() ? "Особый отряд" : " · особый отряд";
      ui::Group g(0, 0);
      if (sub.empty()) ui::spacer(std::round((36 - ui::lineHeight(ui::Font::Strong)) * 0.5f));
      const float room = ui::avail().w;
      ui::label(r->name, {.font = ui::Font::Strong, .tooltip = ui::measure(r->name, ui::Font::Strong) > room ? std::string_view(r->name) : std::string_view()});
      if (!sub.empty()) ui::label(sub, {.font = ui::Font::Caption, .ink = ui::Ink::Muted});
    }
    if (noPlay && !a.admin()) {   // игрок у чужой фракции — без кнопок
      ui::next(30, 30);
    } else {
      ui::Disabled dis(noPlay);
      if (ui::iconButton("trash", fleet ? "Удалить судно из таблицы" : "Удалить отряд из таблицы", {.tone = ui::Tone::Danger}))
        askRemoveRow(a, fid, r->id, fleet);
      a.markUi("mil.detail.delete");
    }
  }
  {
    if (editors && a.admin()) {
      ui::Disabled lock(ro);
      // Наименование — во всю ширину карточки.
      std::string name = r->name;
      if (ui::textField("name", name, {.placeholder = noun(fleet), .icon = special ? "special-unit" : "edit", .maxLength = 60,
                                       .tooltip = "Наименование"}))
        renameRow(a, fid, r->id, fleet, name);
      a.markUi("mil.detail.name");
      ui::prop(fleet ? "Тип судна" : "Тип войск", r->icon);
      int type = r->type;
      typeCombo("type", type, fleet, ro || special);
      if (type != r->type) setRowType(a, fid, r->id, fleet, type);
      a.markUi("mil.detail.type");
    }
    if (!fleet && !inTable.race) {
      ui::prop("Раса", "race");
      raceCombo(a, "race", fid, r->id, ro);
      a.markUi("mil.detail.race");
    }
    if (ar && ar->merc) mercHireField(a, fid, ar->id);   // наёмники — только золото (military_merc.cpp)
    else if (ar && !ar->merc) armyPrice(a, fid, *ar, inTable.key);   // у наёмников — только цена найма золотом
    // Морское чудовище: ключевой ресурс (в узкой таблице его столбца нет).
    if (const FleetRow* fr = fleet && !inTable.key ? fleetRowOf(w, fid, r->id) : nullptr; fr && fr->type == ShipType::SeaMonster) {
      ui::caption("Ключевой ресурс");   // во всю ширину карточки, как у отрядов
      fleetKeyCombo(a, "key", fid, *fr, noPlay);
      a.markUi("mil.detail.key");
    }
    if (editors && a.admin()) {
      ui::Disabled lock(ro);
      if (st.editReserve) {   // п.10.3: напрямую — только в режиме «Правка резерва»
        ui::prop("Численность", fleet ? "fleet" : "users");
        i64 total = r->total;
        if (ui::numberField("total", total, {.min = double(field), .max = 1e12, .steppers = true,
                                             .tooltip = "Не меньше численности " + std::string(fieldWord(fleet)) + ": " + fmtCount(field)}))
          setTotal(a, fid, r->id, total);
        a.markUi("mil.detail.total");
      }
      if (!r->elemental) {   // элементали содержатся эссенциями (список выше)
        ui::prop(fleet ? "За корабль" : "За единицу", "coins");
        ui::Disabled lock(special);
        double up = r->upkeep;
        if (ui::numberField("upkeep", up, {.min = 0, .max = 1e9, .step = 0.5, .digits = 3, .unit = "тыс.",
                                           .tooltip = special ? "Содержание особого отряда — из справочника" : "Содержание одного за ход, золотом"}))
          setRowUpkeep(a, fid, r->id, fleet, up);
        a.markUi("mil.detail.upkeep");
      }
    }
    // Особый отряд: тип, раса и цены — из справочника «Особые отряды»; найм — пока достроена постройка доступа.
    if (special) {
      ui::HStack hs(26, ui::Align::Left, 6);
      ui::tag("Особый отряд", ui::Tone::Accent, "special-unit");
      ui::flex();
      if (a.admin()) {   // справочники — только администратору
        if (ui::button("Справочник", {.variant = ui::Variant::Ghost, .icon = "book", .size = ui::Size::Small,
                                      .tooltip = "Тип, раса и цены особого отряда правятся в справочнике «Особые отряды»"}))
          a.openEditor("catalogs", 9);
        a.markUi("mil.detail.special.open");
      }
    }
    if (special && !rules::hasSpecialAccess(w, fid, r->special)) {
      ui::label(specialNeed(w, r->special), {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
      a.markUi("mil.detail.special.need");
    }
    if (r->keyMissing) ui::label("Не указан ключевой ресурс — найм недоступен", {.font = ui::Font::Small, .ink = ui::Ink::Danger, .icon = "warning", .wrap = true});
    if (!noPlay || a.admin()) {
      ui::Disabled dis(noPlay);
      ui::Row rr({ui::fr(1), ui::fr(1)}, 30, 8);
      if (ui::button("Сформировать", {.icon = "plus", .fill = true,
                                      .tooltip = fleet ? "Новые корабли за ресурсы — в резерв через " + nTurns(schema::kFormationTurns)
                                                       : "Новые отряды — в резерв через " + nTurns(schema::kFormationTurns)}))
        ui::openPopup("recruit");
      a.markUi("mil.detail.recruit");
      recruitPopup(a, "recruit", fid, *r, fleet);
      if (ui::button("Распустить", {.icon = "disband", .fill = true, .disabled = reserve <= 0,
                                    .tooltip = fleet ? "Корабли из резерва" : "Отряды из резерва — обратно в население"}))
        ui::openPopup("disband");
      a.markUi("mil.detail.disband");
      disbandPopup(a, "disband", fid, *r, fleet, reserve);
    }
  }
  {
    ui::HStack hs(0, ui::Align::Left, 6);
    ui::tag(std::string(fleet ? "В море " : "В поле ") + fmtCount(field), ui::Tone::Info, fleet ? "sea" : "map-pin");
    ui::tag("Резерв " + fmtCount(reserve), reserve > 0 ? ui::Tone::Success : ui::Tone::Neutral, fleet ? "anchor" : "shield");
    if (forming > 0) ui::tag("+" + fmtCount(forming), ui::Tone::Accent, "hourglass");
    if (r->elemental && ar) {
      const std::map<Id, double> ess = essenceUpkeep(w, fid, *ar);
      double sum = 0;
      for (auto& [e, v] : ess) sum += v;
      ui::tag(fmtMoney(sum) + " за ход", ui::Tone::Warning, "essence");
    } else {
      ui::tag(fmtGold(rc ? rc->upkeepTotal : 0) + " за ход", ui::Tone::Warning, "coins");
    }
  }
  ui::caption(fleet ? "Где корабли" : "Где отряды");
  deploymentChips(a, fid, r->id, fleet);
  return true;
}

// Формирующиеся отряды (корабли) вкладки: сколько, через сколько ходов, отмена с возвратом.
void formingList(App& a, Id fid, bool fleet) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(fid);
  if (!f) return;
  struct L {
    int index;
    UnitRow row;
    i64 count;
    int left;
  };
  std::vector<L> lines;
  for (size_t i = 0; i < f->forming.size(); i++) {
    const Formation& q = f->forming[i];
    if (auto r = unitRow(w, fid, q.row, fleet)) lines.push_back(L{int(i), *r, q.count, q.left});
  }
  if (lines.empty()) return;
  i64 total = 0;
  for (const L& l : lines) total += l.count;
  ui::Section s("Формируется", "hourglass", {.badge = fmtCount(total), .card = false});
  if (!s) return;
  const bool cancel = a.canPlay(fid) || a.admin();   // игрок у чужой фракции — без отмены
  ui::Column cols[] = {{noun(fleet), nullptr, ui::fr(1, 110)},
                       {"Число", nullptr, ui::px(84), ui::Align::Right},
                       {"", "hourglass", ui::px(80), ui::Align::Right, false, "Поступит в резерв через"},
                       {"", nullptr, ui::px(36)}};
  ui::Table t("forming", cols, int(lines.size()), {.rowHeight = 40, .selectable = false});
  for (int i : t) {
    const L& l = lines[size_t(i)];
    RectF cr = t.cell();
    unitCell(cr, l.row);
    t.text(fmtCount(l.count));
    t.text(nTurns(l.left), ui::Ink::Accent);
    t.cell();
    if (!cancel) continue;
    ui::Disabled dis(!a.canPlay(fid));
    if (ui::iconButton("close", "Отменить формирование — с возвратом")) cancelForming(a, fid, l.index, l.row.id, l.count);
    a.markUi(std::string(fleet ? "mil.fleet" : "mil.army") + ".forming." + std::to_string(i) + ".cancel");
  }
}

}  // namespace rg::app::mil::forces
