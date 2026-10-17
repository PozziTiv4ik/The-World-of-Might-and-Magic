// Regnum — войска и флот фракции: плитки показателей и таблица строк (широкая — правка в ячейках, узкая — в две
// строки).
#include "app/panels/military_internal.h"

namespace rg::app::mil::forces {

// ---------------------------------------------------------------- показатели
// Четыре показателя: всего, в поле (в море), в резерве (и формируется), содержание; ниже — содержание элементалей
// эссенциями (если есть). wide — в одну строку.
void statTiles(App& a, Id fid, bool fleet, bool wide) {
  const World& w = frameWorld(a);
  auto c = rules::calc(w);
  const rules::FactionCalc* fc = c->faction(fid);
  i64 total = fc ? (fleet ? fc->fleetTotal : fc->armyTotal) : 0;
  i64 field = fc ? (fleet ? fc->fleetField : fc->armyField) : 0;
  i64 forming = 0;
  if (fc)
    for (const rules::RowCalc& r : fleet ? fc->fleet : fc->army) forming += r.forming;
  double upkeep = fc ? (fleet ? fc->expFleet : fc->expArmy) : 0;
  double mod = fc ? fc->fx[fleet ? Fx::FleetUpkeepPct : Fx::ArmyUpkeepPct] : 0;
  std::string modText = std::fabs(mod) > 1e-9 ? fmtPct(mod, 0, true) : std::string();
  const std::string formText = forming > 0 ? "+" + fmtCount(forming) : std::string();
  const std::string reserveTip = "Численность общая − численность " + std::string(fieldWord(fleet)) +
                                 (forming > 0 ? "; формируется " + fmtCount(forming) : std::string());
  auto tiles = [&] {
    ui::stat(fmtCount(total), fleet ? "Всего кораблей" : "Всего войск", {.icon = fleet ? "fleet" : "army", .tone = ui::Tone::Accent});
    ui::stat(fmtCount(field), fleet ? "В море" : "В поле",
             {.icon = fleet ? "sea" : "map-pin", .tone = ui::Tone::Info,
              .tooltip = fleet ? "Корабли во флотах на карте и в торговле" : "Отряды в войсках на карте, в гарнизонах и оккупационных гарнизонах"});
    ui::stat(fmtCount(total - field), "В резерве",
             {.icon = fleet ? "anchor" : "shield", .tone = ui::Tone::Success, .delta = double(forming), .deltaText = formText, .tooltip = reserveTip});
    ui::stat(fmtGold(upkeep), "Содержание",
             {.icon = fleet ? "fleet-upkeep" : "army-upkeep", .tone = ui::Tone::Warning, .delta = mod, .deltaText = modText,
              .invertDelta = true,
              .tooltip = std::fabs(mod) > 1e-9 ? std::string("Золотом; с модификатором содержания ") + (fleet ? "флота " : "войск ") + modText
                                               : std::string("Золотом: сумма «содержание общее» по строкам")});
  };
  if (wide) {
    ui::Row r({ui::fr(1), ui::fr(1), ui::fr(1), ui::fr(1)}, 64, 8);
    tiles();
  } else {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
    tiles();
  }
  // Элементали содержатся эссенциями (ТЗ «Ввод новых механик», п.3.2): за ход по эссенциям, запас — в подсказке.
  if (fleet || !fc) return;
  bool any = false;
  for (auto& [e, flow] : fc->essences) any = any || flow.upkeep > 1e-9;
  if (!any) return;
  ui::IdScope scope("essup");
  ui::caption("Содержание эссенциями за ход");
  {
    ui::ChipFlow chipFlow;
    for (auto& [e, flow] : fc->essences) {
      if (flow.upkeep <= 1e-9) continue;
      ui::IdScope s{i64(e)};
      const std::string tip = "Запас " + fmtMoney(flow.stock) +
                              (flow.generation > 1e-9 ? "; постройки дают " + fmtMoney(flow.generation) + " за ход" : std::string()) +
                              (flow.stock + 1e-9 < flow.upkeep ? "\nЗапаса не хватит на ход — уйдёт в долг" : std::string());
      ui::ChipOpt co;
      co.icon = "essence";
      co.tone = flow.stock + 1e-9 < flow.upkeep ? ui::Tone::Danger : ui::Tone::Neutral;
      co.color = legible(w::essenceColor(w, e));
      co.tooltip = tip;
      ui::flowChip(w::essenceName(w, e) + " · " + fmtMoney(flow.upkeep), co);
    }
  }
  a.markUi("mil.army.essupkeep");
}

namespace {

// ---------------------------------------------------------------- таблица
struct Line {
  UnitRow row;
  const rules::RowCalc* rc = nullptr;
  std::string race;
  std::string keyName;               // ключевой ресурс (сортировка)
  std::map<Id, double> essTotal;     // элементали: содержание эссенциями за ход
  i64 field() const { return rc ? rc->field : 0; }
  i64 reserve() const { return rc ? rc->reserve : row.total; }
  i64 forming() const { return rc ? rc->forming : 0; }
  double upkeepTotal() const { return rc ? rc->upkeepTotal : 0; }
};

// Столбцы широкой таблицы (сортировка по смыслу, а не по номеру показанного столбца).
enum Col : int { ColType, ColName, ColRace, ColKey, ColCost, ColTotal, ColField, ColReserve, ColUpkeep, ColUpkeepTotal, ColActions };

int compareLines(const Line& x, const Line& y, int col) {
  auto cmpNum = [](double p, double q) { return p < q ? -1 : p > q ? 1 : 0; };
  switch (col) {
    case ColType: return cmpNum(x.row.type, y.row.type);
    case ColName: return compareRu(x.row.name, y.row.name);
    case ColRace: return compareRu(x.race, y.race);
    case ColKey: return compareRu(x.keyName, y.keyName);
    case ColTotal: return cmpNum(double(x.row.total), double(y.row.total));
    case ColField: return cmpNum(double(x.field()), double(y.field()));
    case ColReserve: return cmpNum(double(x.reserve()), double(y.reserve()));
    case ColUpkeep: return cmpNum(x.row.upkeep, y.row.upkeep);
    case ColUpkeepTotal: return cmpNum(x.upkeepTotal(), y.upkeepTotal());
    default: return 0;
  }
}

}  // namespace

void typeCombo(std::string_view id, int& type, bool fleet, bool disabled) {
  int n = fleet ? int(ShipType::Count) : int(UnitType::Count);
  std::vector<ui::Option> opts;
  opts.reserve(size_t(n));
  for (int i = 0; i < n; i++) {
    const schema::EnumInfo& ti = fleet ? schema::kShipTypes[i] : schema::kUnitTypes[i];
    opts.push_back(ui::Option{ti.name, ti.icon});
  }
  ui::combo(id, type, opts, {.disabled = disabled});
}

namespace {

// Какие необязательные столбцы широкой таблицы показать при ширине aw: по порядку, пока наименованию остаётся не
// меньше kNameMin (итог за ход, ключевой ресурс с названием, цена, раса, «в поле», тип с подписью).
struct WideCols {
  bool upkeepTotal = false, keyFull = false, cost = false, race = false, field = false, typeLabel = false;
};
WideCols wideCols(float aw, bool fleet) {
  float budget = aw - kNameMin - (kTypeIcon + kTotalW + kReserveW + kUpkeepW + kActionsW + kKeyIcon);
  WideCols c;
  auto take = [&](bool& on, float w) {
    if (budget >= w) {
      on = true;
      budget -= w;
    }
  };
  take(c.upkeepTotal, kUpkeepTotalW);
  take(c.keyFull, kKeyFull - kKeyIcon);   // флот: ключевой ресурс морского чудовища
  if (!fleet) {
    take(c.cost, kCostW);
    take(c.race, kRaceW);
  }
  take(c.field, kFieldW);
  take(c.typeLabel, kTypeLabel - kTypeIcon);
  return c;
}

// Содержание элементалей в узкой ячейке: первая эссенция (значок и число), «+N» — ещё эссенции; ниже — за единицу.
void essenceUpkeepCell(std::string_view key, RectF r, const World& w, const std::map<Id, double>& total, const std::map<Id, double>& per) {
  const ui::Theme& t = ui::theme();
  const std::vector<Part> parts = essenceParts(w, total);
  if (parts.empty()) {
    cellLines(r, "—", "эссенциями", ui::Align::Right, ui::Ink::Muted);
    return;
  }
  const float h1 = ui::lineHeight(ui::Font::Body), h2 = ui::lineHeight(ui::Font::Caption);
  const float y0 = std::round(r.cy() - (h1 + h2) * 0.5f);
  const std::string top = fmtMoney(parts[0].v) + (parts.size() > 1 ? " +" + std::to_string(parts.size() - 1) : std::string());
  const float tw = ui::measure(top, ui::Font::Body);
  ui::draw::text(top, RectF{r.x, y0, r.w, h1}, ui::Font::Body, t.text, ui::Align::Right);
  ui::draw::icon("essence", RectF{std::max(r.x, r.right() - tw - 17), y0 + (h1 - 13) * 0.5f, 13, 13}, parts[0].color);
  double each = 0;
  if (auto it = per.find(total.begin()->first); it != per.end()) each = it->second;
  ui::draw::text(fmtMoney(each) + " за ед.", RectF{r.x, y0 + h1, r.w, h2}, ui::Font::Caption, t.textMuted, ui::Align::Right);
  std::string tip = "Содержание эссенциями за ход";
  for (const Part& p : parts) tip += "\n" + p.name + ": " + (p.kind == Part::Res && p.id == kGold ? fmtGold(p.v) : fmtMoney(p.v));
  ui::hoverTip(key, r, tip);
}

}  // namespace

// Таблица строк. Возвращает, какие поля карточки уже показаны столбцами (раса, ключевой ресурс).
Shown forcesTable(App& a, Id fid, bool fleet, ForcesState& st, bool wide) {
  const World& w = frameWorld(a);
  auto c = rules::calc(w);
  const Faction* fac = w.faction(fid);
  std::vector<Line> lines;
  for (UnitRow& r : unitRows(w, fid, fleet)) {
    Line l{r, rowCalc(*c, fid, r.id, fleet), fleet ? std::string() : rowRace(w, fid, r.id), {}, {}};
    if (const ArmyRow* ar = fleet || !fac ? nullptr : fac->armyRow(r.id)) {
      if (ar->keyRes) l.keyName = w::resourceName(w, ar->keyRes);
      l.essTotal = essenceUpkeep(w, fid, *ar);
    }
    lines.push_back(std::move(l));
  }
  int sel = -1;
  for (size_t i = 0; i < lines.size(); i++)
    if (lines[i].row.id == st.selRow) sel = int(i);
  // Роли (ТЗ «Доработки №4», п.5): тип, название, раса, содержание и «Правка резерва» — правка мира; ключевой ресурс,
  // формирование, роспуск и удаление строки — игровые действия за фракцию.
  const bool ro = !a.canEdit(), noPlay = !a.canPlay(fid);
  Shown shown;
  const int sel0 = sel;
  i64 sumTotal = 0, sumField = 0, sumReserve = 0, sumForming = 0;
  double sumUpkeep = 0;
  std::map<Id, double> sumEss;
  for (const Line& l : lines) {
    sumTotal += l.row.total;
    sumField += l.field();
    sumReserve += l.reserve();
    sumForming += l.forming();
    sumUpkeep += l.upkeepTotal();
    for (auto& [e, v] : l.essTotal) sumEss[e] += v;
  }
  // Подписи живут до конца функции (столбцы таблицы ссылаются на строки).
  const std::string fieldTitle = fleet ? "В море" : "В поле";
  const std::string tipReserve = "Численность общая − численность " + std::string(fieldWord(fleet)) + "; ниже — формируется";
  const std::string tipMin = "Не меньше численности " + std::string(fieldWord(fleet));
  if (wide) {
    const float aw = ui::avail().w;
    const WideCols wc = wideCols(aw, fleet);
    shown.race = wc.race;
    shown.key = true;
    std::vector<ui::Column> cols;
    std::vector<int> keys;
    auto col = [&](int key, ui::Column c2) {
      cols.push_back(c2);
      keys.push_back(key);
    };
    col(ColType, wc.typeLabel ? ui::Column{fleet ? "Тип судна" : "Тип войск", nullptr, ui::px(kTypeLabel), ui::Align::Left, true}
                              : ui::Column{"", nullptr, ui::px(kTypeIcon), ui::Align::Left, true, fleet ? "Тип судна" : "Тип войск"});
    col(ColName, {"Наименование", nullptr, ui::fr(1, 120), ui::Align::Left, true});
    if (wc.race) col(ColRace, {"Раса", nullptr, ui::px(kRaceW), ui::Align::Left, true, "Раса отряда"});
    col(ColKey, wc.keyFull ? ui::Column{"Ключевой ресурс", nullptr, ui::px(kKeyFull), ui::Align::Left, true, "Ключевой ресурс юнита — без него найм недоступен"}
                           : ui::Column{"", "resource", ui::px(kKeyIcon), ui::Align::Left, true, "Ключевой ресурс юнита — без него найм недоступен"});
    if (wc.cost) col(ColCost, {"Цена", nullptr, ui::px(kCostW), ui::Align::Left, false, "Цена найма одного юнита: население, ресурсы, эссенции"});
    col(ColTotal, {"Всего", nullptr, ui::px(kTotalW), st.editReserve ? ui::Align::Left : ui::Align::Right, true,
                   st.editReserve ? "Численность общая — правка резерва" : "Численность общая"});
    if (wc.field)
      col(ColField, {fieldTitle, nullptr, ui::px(kFieldW), ui::Align::Right, true,
                     fleet ? "Численность в море (флоты и торговля)" : "Численность в поле (войска и гарнизоны)"});
    col(ColReserve, {"Резерв", nullptr, ui::px(kReserveW), ui::Align::Right, true, tipReserve});
    col(ColUpkeep, {"Сод. 1", nullptr, ui::px(kUpkeepW), ui::Align::Left, true,
                    fleet ? "Содержание одного корабля" : "Содержание одного юнита (элементали — эссенциями)"});
    if (wc.upkeepTotal) col(ColUpkeepTotal, {"Итого", "coins", ui::px(kUpkeepTotalW), ui::Align::Right, true, "Содержание общее за ход (с модификатором)"});
    col(ColActions, {"", nullptr, ui::px(kActionsW)});
    ui::Table t("rows", cols, int(lines.size()), {.rowHeight = 40, .selected = &sel, .emptyIcon = fleet ? "fleet" : "army",
                                                  .emptyText = fleet ? "Кораблей нет" : "Отрядов нет"});
    t.sort([&](int x, int y, int k) { return compareLines(lines[size_t(x)], lines[size_t(y)], k >= 0 && k < int(keys.size()) ? keys[size_t(k)] : -1); });
    for (int i : t) {
      const Line& l = lines[size_t(i)];
      const ArmyRow* ar = fleet ? nullptr : armyRowOf(w, fid, l.row.id);
      const bool special = l.row.special != 0;
      const std::string mark = std::string(fleet ? "mil.fleet.row." : "mil.row.") + std::to_string(i);   // войска и флот — разные отметки
      // Тип: выпадающий список или значок с меню типов (особый отряд — из справочника).
      t.cell();
      if (wc.typeLabel) {
        int type = l.row.type;
        typeCombo("type", type, fleet, ro || special);
        if (type != l.row.type) setRowType(a, fid, l.row.id, fleet, type);
      } else if (!a.admin()) {
        ui::icon(l.row.icon, ui::Ink::Dim, 18, l.row.typeName);   // игрок: тип только для чтения — значок с подсказкой
      } else {
        ui::Disabled lock(ro || special);
        if (ui::iconButton(l.row.icon, l.row.typeName)) ui::openPopup("type");
      }
      a.markUi(mark + ".type");
      if (!wc.typeLabel && ui::beginMenu("type")) {
        int n = fleet ? int(ShipType::Count) : int(UnitType::Count);
        for (int k = 0; k < n; k++) {
          const schema::EnumInfo& ti = fleet ? schema::kShipTypes[k] : schema::kUnitTypes[k];
          ui::IdScope s(k);
          if (ui::menuItem(ti.name, {.icon = ti.icon, .checked = k == l.row.type}) && k != l.row.type) setRowType(a, fid, l.row.id, fleet, k);
        }
        ui::endMenu();
      }
      const RectF nameR = t.cell();
      if (!a.admin()) {
        nameCell(nameR, l.row.name, special ? std::string_view("особый отряд") : l.row.merc ? std::string_view("наёмники") : std::string_view());
        a.markUi(mark + ".name", nameR);
      } else {
        ui::Disabled lock(ro);
        std::string name = l.row.name;
        if (ui::textField("name", name, {.placeholder = noun(fleet), .icon = special ? "special-unit" : l.row.merc ? "mercenary" : nullptr, .maxLength = 60,
                                         .tooltip = special ? "Особый отряд справочника" : ""}))
          renameRow(a, fid, l.row.id, fleet, name);
        a.markUi(mark + ".name");
      }
      if (wc.race) {
        t.cell();
        raceCombo(a, "race", fid, l.row.id, ro);
        a.markUi(mark + ".race");
      }
      if (ar) {
        t.cell();
        if (schema::needsKeyResource(ar->type) && !ar->merc) keyCombo(a, "key", fid, *ar, noPlay);
        else ui::label("—", {.ink = ui::Ink::Muted, .tooltip = "У этого типа нет ключевого ресурса"});
        a.markUi(mark + ".key");
      } else if (const FleetRow* fr = fleet ? fleetRowOf(w, fid, l.row.id) : nullptr) {
        t.cell();
        if (fr->type == ShipType::SeaMonster) fleetKeyCombo(a, "key", fid, *fr, noPlay);
        else ui::label("—", {.ink = ui::Ink::Muted, .tooltip = "У этого судна нет ключевого ресурса"});
        a.markUi(mark + ".key");
      }
      if (wc.cost && ar) {
        const RectF cr = t.cell();
        partsCell("##cost", cr, costParts(w, rules::recruitCost(w, fid, l.row.id, 1), ar->keyRes), "Цена найма одного юнита");
        a.markUi(mark + ".cost", cr);
      }
      // Общая численность правится только в режиме «Правка резерва» (п.10.3).
      if (st.editReserve) {   // только у администратора (ForcesState сбрасывается для игрока)
        t.cell();
        ui::Disabled lock(ro);
        i64 total = l.row.total;
        if (ui::numberField("total", total, {.min = double(l.field()), .max = 1e12, .tooltip = tipMin})) setTotal(a, fid, l.row.id, total);
        a.markUi(mark + ".total");
      } else {
        t.text(fmtCount(l.row.total));
      }
      if (wc.field) t.text(fmtCount(l.field()), l.field() > 0 ? ui::Ink::Normal : ui::Ink::Muted);
      {
        const RectF cr = t.cell();
        cellLines(cr, fmtCount(l.reserve()), l.forming() > 0 ? "+" + fmtCount(l.forming()) : std::string(), ui::Align::Right,
                  l.reserve() > 0 ? ui::Ink::Success : ui::Ink::Muted, ui::Ink::Accent);
      }
      // Содержание одного: золотом или (элементали) эссенциями — правка эссенций в карточке строки.
      if (l.row.elemental && ar) {
        const RectF cr = t.cell();
        partsCell("##essup", cr, essenceParts(w, ar->essUpkeep), "Содержание эссенциями на юнит за ход");
      } else {
        t.cell();
        ui::Disabled lock(special || ro);
        double up = l.row.upkeep;
        if (ui::numberField("upkeep", up, {.min = 0, .max = 1e9, .step = 0.5, .digits = 3, .unit = "тыс.",
                                           .tooltip = special ? "Содержание особого отряда — из справочника" : ""}))
          setRowUpkeep(a, fid, l.row.id, fleet, up);
      }
      a.markUi(mark + ".upkeep");
      if (wc.upkeepTotal) {
        if (l.row.elemental) {
          const RectF cr = t.cell();
          partsCell("##esstotal", cr, essenceParts(w, l.essTotal), "Содержание эссенциями за ход", ui::Align::Right);
        } else {
          t.text(fmtGold(l.upkeepTotal()));
        }
      }
      t.cell();
      if (!noPlay || a.admin()) {   // игрок у чужой фракции — без кнопок
        ui::Disabled dis(noPlay);
        ui::HStack hs(30, ui::Align::Right, 2);
        if (ui::iconButton("plus", fleet ? "Сформировать корабли" : "Сформировать отряды")) ui::openPopup("recruit");
        a.markUi(mark + ".recruit");
        recruitPopup(a, "recruit", fid, l.row, fleet);
        if (ui::iconButton("disband", fleet ? "Распустить корабли из резерва" : "Распустить отряды из резерва", {.disabled = l.reserve() <= 0}))
          ui::openPopup("disband");
        a.markUi(mark + ".disband");
        disbandPopup(a, "disband", fid, l.row, fleet, l.reserve());
        if (ui::iconButton("trash", fleet ? "Удалить судно" : "Удалить отряд", {.tone = ui::Tone::Danger})) askRemoveRow(a, fid, l.row.id, fleet);
        a.markUi(mark + ".delete");
      }
    }
    if (!lines.empty() && t.footer()) {
      t.cell();
      t.text("Итого");
      if (wc.race) t.cell();
      t.cell();   // ключевой ресурс
      if (wc.cost) t.cell();
      t.text(fmtCount(sumTotal));
      if (wc.field) t.text(fmtCount(sumField));
      {
        const RectF cr = t.cell();
        cellLines(cr, fmtCount(sumReserve), sumForming > 0 ? "+" + fmtCount(sumForming) : std::string(), ui::Align::Right, ui::Ink::Normal,
                  ui::Ink::Accent);
      }
      {
        const RectF cr = t.cell();   // содержание элементалей эссенциями — под «Сод. 1»
        if (!sumEss.empty()) partsCell("##essfoot", cr, essenceParts(w, sumEss), "Содержание эссенциями за ход");
      }
      if (wc.upkeepTotal) t.text(fmtGold(sumUpkeep));
    }
  } else {
    ui::Column cols[] = {{noun(fleet), nullptr, ui::fr(1, 110)},
                         {"Всего", nullptr, ui::px(76), ui::Align::Right, false, "Численность общая; ниже — в резерве"},
                         {"Сод.", "coins", ui::px(76), ui::Align::Right, false, "Содержание общее за ход; ниже — одного (элементали — эссенциями)"}};
    ui::Table t("rows", cols, int(lines.size()), {.rowHeight = 44, .selected = &sel, .emptyIcon = fleet ? "fleet" : "army",
                                                  .emptyText = fleet ? "Кораблей нет" : "Отрядов нет"});
    for (int i : t) {
      const Line& l = lines[size_t(i)];
      a.markUi(std::string(fleet ? "mil.fleet.row." : "mil.row.") + std::to_string(i));
      RectF cr = t.cell();
      std::string caption = l.row.name != l.row.typeName ? std::string(l.row.typeName) : std::string();
      unitCell(cr, l.row, sel == i, caption, true);
      cr = t.cell();
      cellLines(cr, fmtCount(l.row.total), fmtCount(l.reserve()), ui::Align::Right, ui::Ink::Normal, l.reserve() > 0 ? ui::Ink::Success : ui::Ink::Muted);
      cr = t.cell();
      if (l.row.elemental) {
        const ArmyRow* ar = armyRowOf(w, fid, l.row.id);
        essenceUpkeepCell("##essup", cr, w, l.essTotal, ar ? ar->essUpkeep : std::map<Id, double>{});
      } else {
        cellLines(cr, fmtGold(l.upkeepTotal()), fmtGold(l.row.upkeep) + " за ед.", ui::Align::Right);
      }
    }
    if (!lines.empty() && t.footer()) {
      t.text("Итого");
      RectF cr = t.cell();
      cellLines(cr, fmtCount(sumTotal), fmtCount(sumReserve), ui::Align::Right, ui::Ink::Normal, ui::Ink::Success);
      t.text(fmtGold(sumUpkeep));
    }
  }
  // Выбор меняется только щелчком или стрелками: новая строка из меню ещё может отсутствовать в мире кадра.
  if (sel != sel0) st.selRow = sel >= 0 && sel < int(lines.size()) ? lines[size_t(sel)].row.id : 0;
  if (st.selRow && !unitRow(a.world(), fid, st.selRow, fleet)) st.selRow = 0;   // строку удалили
  return shown;
}

}  // namespace rg::app::mil::forces
