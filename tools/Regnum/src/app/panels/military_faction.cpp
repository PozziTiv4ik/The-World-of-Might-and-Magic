// Regnum — вкладки фракции «Войска» и «Флот» (ТЗ 1.c.i–ii, 1.d.iii; «Общие доработки», п.10–11; «Виды государств»,
// п.2) и полноэкранные таблицы войск и флота.
//
// Таблица строк: наименование, тип (12 типов войск или 3 типа судов), раса отряда, численность общая, в поле
// (в море), формируется, в резерве, содержание одного, содержание общее (с модификатором содержания). Отряды
// формируются из населения (нежить — из трупов, демоны — из демонической энергии, корабли — за ресурсы констант
// стоимости) и через 2 хода поступают в резерв; резерв распускается обратно в население. Общая численность правится
// напрямую только в режиме «Правка резерва». Узкий инспектор показывает значения в две строки и правит выбранную
// строку в карточке под таблицей; широкая таблица правится в ячейках. Во вкладке флота — участие флота в торговле.
#include "app/panels/military.h"

namespace rg::app {
namespace edkit {   // поток фишек с переносом строк (editors/modifiers.cpp)
void chipsBegin();
ui::ChipAction chip(std::string_view label, const ui::ChipOpt& o);
void chipsEnd();
}  // namespace edkit
}  // namespace rg::app

namespace rg::app::mil {

namespace {

constexpr float kWide = 640;       // ширина, начиная с которой таблица правится в ячейках
constexpr float kFieldCols = 820;  // столбцы «в поле» и «формируется» в широкой таблице
constexpr float kRaceCol = 1000;   // раса отряда — столбцом таблицы (уже — полем карточки строки)

struct ForcesState {
  Id selRow = 0;
  Id shownRow = 0;            // строка, карточка которой уже показана (новую — прокрутить в видимую часть)
  bool editReserve = false;   // «Правка резерва» (п.10.3): общая численность правится напрямую
};

// Число во всплывающих окнах «Сформировать» и «Распустить» (у каждой строки своё).
struct CountState {
  i64 count = 0;
  bool init = false;
};

const char* noun(bool fleet) { return fleet ? "Судно" : "Отряд"; }
const char* fieldWord(bool fleet) { return fleet ? "в море" : "в поле"; }

// Раса строки армии (пусто — не строка армии).
std::string rowRace(const World& w, Id fid, Id row) {
  const Faction* f = w.faction(fid);
  const ArmyRow* r = f ? f->armyRow(row) : nullptr;
  return r ? rules::unitRace(w, fid, *r) : std::string();
}

const char* raceIcon(std::string_view race) {
  if (race == schema::kRaceUndead) return "skull";
  if (race == schema::kRaceDemonic) return "flame";
  if (race == schema::kRaceMechanical) return "hammer";
  if (race == schema::kRaceElemental) return "bolt";
  if (race == schema::kRaceLiving) return "heart";
  return "race";
}

// ---------------------------------------------------------------- правка строк
template <class F>
void editRow(Tx& tx, Id fid, Id row, bool fleet, F&& fn) {
  Faction& f = tx.faction(fid);
  if (fleet) {
    for (FleetRow& r : f.fleet)
      if (r.id == row) fn(r);
  } else {
    for (ArmyRow& r : f.army)
      if (r.id == row) fn(r);
  }
}

void renameRow(App& a, Id fid, Id row, bool fleet, const std::string& name) {
  std::string n = trim(name);
  if (n.empty()) {
    a.toast("Название не может быть пустым", ToastKind::Warning, "warning");
    return;
  }
  a.act(fleet ? "Переименовать судно" : "Переименовать отряд", [&](Tx& tx) { editRow(tx, fid, row, fleet, [&](auto& r) { r.name = n; }); });
}

void setRowType(App& a, Id fid, Id row, bool fleet, int type) {
  a.act(fleet ? "Тип судна" : "Тип войск", [&](Tx& tx) {
    editRow(tx, fid, row, fleet, [&](auto& r) {
      using T = std::decay_t<decltype(r)>;
      if constexpr (std::is_same_v<T, FleetRow>) {
        if (r.name == schema::shipType(r.type).name) r.name = schema::shipType(ShipType(type)).name;   // имя по типу следует за типом
        r.type = ShipType(type);
      } else {
        if (r.name == schema::unitType(r.type).name) r.name = schema::unitType(UnitType(type)).name;
        // Раса по умолчанию следует за типом (военные механизмы — «Механический», ТЗ «Виды государств», п.2.4).
        const StateKind kind = rules::stateKindOf(tx.w(), fid);
        if (r.race.empty() || r.race == rules::defaultUnitRace(kind, r.type)) r.race = rules::defaultUnitRace(kind, UnitType(type));
        r.type = UnitType(type);
      }
    });
  });
}

void setRowUpkeep(App& a, Id fid, Id row, bool fleet, double v) {
  if (!std::isfinite(v) || v < 0) return;
  a.act(fleet ? "Содержание судна" : "Содержание отряда", [&](Tx& tx) { editRow(tx, fid, row, fleet, [&](auto& r) { r.upkeep = v; }); },
        {.coalesce = "upkeep:" + std::to_string(row)});
}

void setTotal(App& a, Id fid, Id row, i64 v) {
  a.act("Правка резерва", [&](Tx& tx) { rules::setRowTotal(tx, fid, row, v); }, {.coalesce = "rowtotal:" + std::to_string(row)});
}

Id addRow(App& a, Id fid, bool fleet, int type) {
  Id id = 0;
  a.act(fleet ? "Добавить судно" : "Добавить отряд", [&](Tx& tx) {
    id = fleet ? rules::addFleetRow(tx, fid, ShipType(type)) : rules::addArmyRow(tx, fid, UnitType(type));
  });
  return id;
}

// Меню выбора типа для новой строки (открывается ui::openPopup(id)).
void addRowMenu(App& a, const char* id, Id fid, bool fleet, ForcesState& st) {
  if (!ui::beginMenu(id)) return;
  ui::menuHeader(fleet ? "Тип судна" : "Тип войск");
  int n = fleet ? int(ShipType::Count) : int(UnitType::Count);
  for (int i = 0; i < n; i++) {
    const schema::EnumInfo& ti = fleet ? schema::kShipTypes[i] : schema::kUnitTypes[i];
    ui::IdScope s(i);
    if (ui::menuItem(ti.name, {.icon = ti.icon}))
      if (Id nid = addRow(a, fid, fleet, i)) st.selRow = nid;
    a.markUi(std::string("mil.addrow.") + std::to_string(i));
  }
  ui::endMenu();
}

// Раса отряда (ТЗ «Виды государств», п.2): значения константы «Расы для отрядов»; по умолчанию — по виду
// государства и типу отряда.
void raceCombo(App& a, std::string_view id, Id fid, Id row, bool disabled) {
  const World& w = frameWorld(a);
  const std::string cur = rowRace(w, fid, row);
  if (cur.empty()) return;
  std::vector<std::string> races = rules::unitRaces(w);
  if (std::find(races.begin(), races.end(), cur) == races.end()) races.push_back(cur);   // раса не из константы
  std::vector<ui::Option> opts;
  int idx = -1;
  for (size_t i = 0; i < races.size(); i++) {
    opts.push_back(ui::Option{races[i], raceIcon(races[i])});
    if (races[i] == cur) idx = int(i);
  }
  const int before = idx;
  ui::combo(id, idx, opts, {.disabled = disabled, .tooltip = "Раса отряда"});
  if (idx != before && idx >= 0 && idx < int(races.size())) {
    const std::string r = races[size_t(idx)];
    a.act("Раса отряда", [&](Tx& tx) { rules::setRowRace(tx, fid, row, r); });
  }
}

// ---------------------------------------------------------------- формирование и роспуск
// Цена формирования: люди из населения и ресурсы (до тысячных). Ресурса ещё нет в мире (трупы, демоническая
// энергия) — значок по расе отряда.
void costLine(const World& w, const rules::RecruitCost& c, const std::string& race) {
  ui::HStack hs(24, ui::Align::Left, 6);
  bool any = false;
  if (c.people > 0) {
    ui::icon("population", ui::Ink::Dim, 16, "Из населения государства — поровну со всех провинций");
    ui::label(fmtCount(c.people), {.font = ui::Font::Strong});
    any = true;
  }
  for (const auto& [res, v] : c.res) {
    ui::IdScope s{i64(res)};
    if (any) ui::spacer(4);
    if (res) {
      const CatalogItem* ci = w.resource(res);
      ui::iconColored(w::resourceIcon(w, res), w::resourceColor(w, res), 16, ci ? std::string_view(ci->name) : std::string_view("Ресурс"));
    } else {
      const bool undead = race == schema::kRaceUndead;
      ui::icon(undead ? "skull" : "flame", ui::Ink::Dim, 16, undead ? "Трупы" : "Демоническая энергия");
    }
    ui::label(fmtMoney(v), {.font = ui::Font::Strong});
    any = true;
  }
  if (!any) ui::label("Без затрат", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
}

// «Сформировать» (п.10.1–10.2, 10.5): число, цена, причины отказа; через 2 хода — в резерв.
void recruitPopup(App& a, const char* id, Id fid, const UnitRow& row, bool fleet) {
  if (!ui::beginPopup(id, {.width = 300})) return;
  const World& w = frameWorld(a);
  auto& st = ui::state<CountState>(ui::id("recruit-count"));
  if (!st.init) {
    st.count = fleet ? 1 : 100;
    st.init = true;
  }
  ui::caption(row.name);
  ui::numberField("count", st.count, {.min = 1, .max = 1e12, .icon = fleet ? "fleet" : "users", .steppers = true,
                                      .tooltip = fleet ? "Сколько кораблей сформировать" : "Сколько воинов сформировать"});
  a.markUi("mil.recruit.count");
  const rules::RecruitCost cost = rules::recruitCost(w, fid, row.id, st.count);
  costLine(w, cost, fleet ? std::string() : rowRace(w, fid, row.id));
  for (const std::string& p : cost.problems) ui::label(p, {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
  ui::label("В резерв через " + nTurns(schema::kFormationTurns), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "hourglass"});
  if (ui::button("Сформировать", {.variant = ui::Variant::Primary, .icon = "plus", .fill = true, .disabled = !cost.problems.empty() || a.readOnly()})) {
    const i64 n = st.count;
    const Id r = row.id;
    if (a.act(fleet ? "Сформировать корабли" : "Сформировать отряды", [&](Tx& tx) { rules::recruit(tx, fid, r, n); })) ui::closePopup();
  }
  a.markUi("mil.recruit.ok");
  ui::endPopup();
}

// «Распустить» (п.10.4): из резерва; воины возвращаются в население (нежить — в трупы, демоны — в энергию).
void disbandPopup(App& a, const char* id, Id fid, const UnitRow& row, bool fleet, i64 reserve) {
  if (!ui::beginPopup(id, {.width = 280})) return;
  const World& w = frameWorld(a);
  reserve = std::max<i64>(0, reserve);
  auto& st = ui::state<CountState>(ui::id("disband-count"));
  if (!st.init) {
    st.count = reserve;
    st.init = true;
  }
  st.count = clamp<i64>(st.count, reserve > 0 ? 1 : 0, reserve);
  ui::caption(row.name);
  ui::numberField("count", st.count, {.min = reserve > 0 ? 1.0 : 0.0, .max = double(reserve), .icon = fleet ? "fleet" : "users", .steppers = true,
                                      .tooltip = "Не больше резерва: " + fmtCount(reserve)});
  a.markUi("mil.disband.count");
  if (!fleet) {
    const std::string race = rowRace(w, fid, row.id);
    const Faction* f = w.faction(fid);
    if (race == schema::kRaceUndead) ui::label("В трупы", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "skull"});
    else if (race == schema::kRaceDemonic) ui::label("В демоническую энергию", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "flame"});
    else if (f && f->isState()) ui::label("В население государства", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "population"});
  }
  if (reserve <= 0) ui::label("Резерв пуст", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
  if (ui::button("Распустить", {.variant = ui::Variant::Danger, .icon = "disband", .fill = true, .disabled = reserve <= 0 || st.count <= 0 || a.readOnly()})) {
    const i64 n = st.count;
    const Id r = row.id;
    if (a.act(fleet ? "Распустить корабли" : "Распустить отряды", [&](Tx& tx) { rules::disbandReserve(tx, fid, r, n); })) ui::closePopup();
  }
  a.markUi("mil.disband.ok");
  ui::endPopup();
}

// Отменить формирование (возврат людей и ресурсов). Индекс сверяется со строкой и числом внутри транзакции.
void cancelForming(App& a, Id fid, int index, Id row, i64 count) {
  a.act("Отменить формирование", [&](Tx& tx) {
    const Faction* f = tx.w().faction(fid);
    if (!f || index < 0 || index >= int(f->forming.size()) || f->forming[size_t(index)].row != row || f->forming[size_t(index)].count != count)
      fail("Формирование уже изменилось");
    rules::cancelFormation(tx, fid, index);
  });
}

// ---------------------------------------------------------------- показатели
// Четыре показателя: всего, в поле (в море), в резерве (и формируется), содержание. wide — в одну строку.
void statTiles(App& a, Id fid, bool fleet, bool wide) {
  auto c = rules::calc(frameWorld(a));
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
    ui::stat(fmtMoney(upkeep), "Содержание",
             {.icon = fleet ? "fleet-upkeep" : "army-upkeep", .tone = ui::Tone::Warning, .delta = mod, .deltaText = modText,
              .invertDelta = true,
              .tooltip = std::fabs(mod) > 1e-9 ? std::string("С модификатором содержания ") + (fleet ? "флота " : "войск ") + modText
                                               : std::string("Сумма «содержание общее» по строкам")});
  };
  if (wide) {
    ui::Row r({ui::fr(1), ui::fr(1), ui::fr(1), ui::fr(1)}, 64, 8);
    tiles();
  } else {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
    tiles();
  }
}

// ---------------------------------------------------------------- таблица
struct Line {
  UnitRow row;
  const rules::RowCalc* rc = nullptr;
  std::string race;
  i64 field() const { return rc ? rc->field : 0; }
  i64 reserve() const { return rc ? rc->reserve : row.total; }
  i64 forming() const { return rc ? rc->forming : 0; }
  double upkeepTotal() const { return rc ? rc->upkeepTotal : 0; }
};

// Столбцы широкой таблицы (сортировка по смыслу, а не по номеру показанного столбца).
enum Col : int { ColType, ColName, ColRace, ColTotal, ColField, ColForming, ColReserve, ColUpkeep, ColUpkeepTotal, ColActions };

int compareLines(const Line& x, const Line& y, int col) {
  auto cmpNum = [](double p, double q) { return p < q ? -1 : p > q ? 1 : 0; };
  switch (col) {
    case ColType: return cmpNum(x.row.type, y.row.type);
    case ColName: return compareRu(x.row.name, y.row.name);
    case ColRace: return compareRu(x.race, y.race);
    case ColTotal: return cmpNum(double(x.row.total), double(y.row.total));
    case ColField: return cmpNum(double(x.field()), double(y.field()));
    case ColForming: return cmpNum(double(x.forming()), double(y.forming()));
    case ColReserve: return cmpNum(double(x.reserve()), double(y.reserve()));
    case ColUpkeep: return cmpNum(x.row.upkeep, y.row.upkeep);
    case ColUpkeepTotal: return cmpNum(x.upkeepTotal(), y.upkeepTotal());
    default: return 0;
  }
}

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

// true — раса отряда показана столбцом таблицы.
bool forcesTable(App& a, Id fid, bool fleet, ForcesState& st, bool wide) {
  const World& w = frameWorld(a);
  auto c = rules::calc(w);
  std::vector<Line> lines;
  for (UnitRow& r : unitRows(w, fid, fleet)) lines.push_back(Line{r, rowCalc(*c, fid, r.id, fleet), fleet ? std::string() : rowRace(w, fid, r.id)});
  int sel = -1;
  for (size_t i = 0; i < lines.size(); i++)
    if (lines[i].row.id == st.selRow) sel = int(i);
  bool ro = a.readOnly();
  bool raceShown = false;
  const int sel0 = sel;
  i64 sumTotal = 0, sumField = 0, sumReserve = 0, sumForming = 0;
  double sumUpkeep = 0;
  for (const Line& l : lines) {
    sumTotal += l.row.total;
    sumField += l.field();
    sumReserve += l.reserve();
    sumForming += l.forming();
    sumUpkeep += l.upkeepTotal();
  }
  // Подписи живут до конца функции (столбцы таблицы ссылаются на строки).
  const std::string fieldTitle = fleet ? "В море" : "В поле";
  const std::string tipReserve = "Численность общая − численность " + std::string(fieldWord(fleet));
  const std::string tipTotal = "Численность общая и " + std::string(fieldWord(fleet));
  const std::string tipMin = "Не меньше численности " + std::string(fieldWord(fleet));
  const std::string tipForming = "Формируется — поступит в резерв через " + nTurns(schema::kFormationTurns);
  const std::string tipReserveForming = tipReserve + "; ниже — формируется";
  if (wide) {
    // Во весь экран тип — выпадающий список с подписью; в широком инспекторе — значок с меню типов.
    const float aw = ui::avail().w;
    const bool typeLabel = aw >= 900;
    const bool raceCol = !fleet && aw >= kRaceCol;
    raceShown = raceCol;
    const bool fieldCols = aw >= kFieldCols;
    std::vector<ui::Column> cols;
    std::vector<int> keys;
    auto col = [&](int key, ui::Column c2) {
      cols.push_back(c2);
      keys.push_back(key);
    };
    col(ColType, typeLabel ? ui::Column{fleet ? "Тип судна" : "Тип войск", nullptr, ui::px(214), ui::Align::Left, true}
                           : ui::Column{"", nullptr, ui::px(52), ui::Align::Left, true, fleet ? "Тип судна" : "Тип войск"});
    col(ColName, {"Наименование", nullptr, ui::fr(1, 140), ui::Align::Left, true});
    if (raceCol) col(ColRace, {"Раса", nullptr, ui::px(156), ui::Align::Left, true, "Раса отряда"});
    col(ColTotal, {"Всего", nullptr, ui::px(92), st.editReserve ? ui::Align::Left : ui::Align::Right, true,
                   st.editReserve ? "Численность общая — правка резерва" : "Численность общая"});
    if (fieldCols) {
      col(ColField, {fieldTitle, nullptr, ui::px(76), ui::Align::Right, true,
                     fleet ? "Численность в море (флоты и торговля)" : "Численность в поле (войска и гарнизоны)"});
      col(ColForming, {"", "hourglass", ui::px(64), ui::Align::Right, true, tipForming});
    }
    col(ColReserve, {"Резерв", nullptr, ui::px(76), ui::Align::Right, true, tipReserve});
    col(ColUpkeep, {"Сод. 1", nullptr, ui::px(96), ui::Align::Left, true, fleet ? "Содержание одного корабля" : "Содержание одного юнита"});
    col(ColUpkeepTotal, {"Итого", "coins", ui::px(98), ui::Align::Right, true, "Содержание общее за ход (с модификатором)"});
    col(ColActions, {"", nullptr, ui::px(110)});
    ui::Table t("rows", cols, int(lines.size()), {.rowHeight = 40, .selected = &sel, .emptyIcon = fleet ? "fleet" : "army",
                                                  .emptyText = fleet ? "Кораблей нет" : "Отрядов нет"});
    t.sort([&](int x, int y, int k) { return compareLines(lines[size_t(x)], lines[size_t(y)], k >= 0 && k < int(keys.size()) ? keys[size_t(k)] : -1); });
    for (int i : t) {
      const Line& l = lines[size_t(i)];
      const std::string mark = "mil.row." + std::to_string(i);
      ui::Disabled dis(ro);
      // Тип: выпадающий список или значок с меню типов.
      t.cell();
      if (typeLabel) {
        int type = l.row.type;
        typeCombo("type", type, fleet, ro);
        if (type != l.row.type) setRowType(a, fid, l.row.id, fleet, type);
      } else if (ui::iconButton(l.row.icon, l.row.typeName)) {
        ui::openPopup("type");
      }
      a.markUi(mark + ".type");
      if (!typeLabel && ui::beginMenu("type")) {
        int n = fleet ? int(ShipType::Count) : int(UnitType::Count);
        for (int k = 0; k < n; k++) {
          const schema::EnumInfo& ti = fleet ? schema::kShipTypes[k] : schema::kUnitTypes[k];
          ui::IdScope s(k);
          if (ui::menuItem(ti.name, {.icon = ti.icon, .checked = k == l.row.type}) && k != l.row.type) setRowType(a, fid, l.row.id, fleet, k);
        }
        ui::endMenu();
      }
      t.cell();
      std::string name = l.row.name;
      if (ui::textField("name", name, {.placeholder = noun(fleet), .maxLength = 60})) renameRow(a, fid, l.row.id, fleet, name);
      if (raceCol) {
        t.cell();
        raceCombo(a, "race", fid, l.row.id, ro);
        a.markUi(mark + ".race");
      }
      // Общая численность правится только в режиме «Правка резерва» (п.10.3).
      if (st.editReserve) {
        t.cell();
        i64 total = l.row.total;
        if (ui::numberField("total", total, {.min = double(l.field()), .max = 1e12, .tooltip = tipMin})) setTotal(a, fid, l.row.id, total);
        a.markUi(mark + ".total");
      } else {
        t.text(fmtCount(l.row.total));
      }
      if (fieldCols) {
        t.text(fmtCount(l.field()), l.field() > 0 ? ui::Ink::Normal : ui::Ink::Muted);
        t.text(l.forming() > 0 ? "+" + fmtCount(l.forming()) : std::string("—"), l.forming() > 0 ? ui::Ink::Accent : ui::Ink::Muted);
      }
      t.text(fmtCount(l.reserve()), l.reserve() > 0 ? ui::Ink::Success : ui::Ink::Muted);
      t.cell();
      double up = l.row.upkeep;
      if (ui::numberField("upkeep", up, {.min = 0, .max = 1e9, .step = 0.5, .digits = 3})) setRowUpkeep(a, fid, l.row.id, fleet, up);
      t.text(fmtMoney(l.upkeepTotal()));
      t.cell();
      {
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
      if (raceCol) t.cell();
      t.text(fmtCount(sumTotal));
      if (fieldCols) {
        t.text(fmtCount(sumField));
        t.text(sumForming > 0 ? "+" + fmtCount(sumForming) : std::string("—"));
      }
      t.text(fmtCount(sumReserve));
      t.cell();
      t.text(fmtMoney(sumUpkeep));
    }
  } else {
    ui::Column cols[] = {{noun(fleet), nullptr, ui::fr(1, 110)},
                         {"Всего", nullptr, ui::px(84), ui::Align::Right, false, tipTotal},
                         {"Резерв", nullptr, ui::px(70), ui::Align::Right, false, tipReserveForming},
                         {"Сод.", "coins", ui::px(74), ui::Align::Right, false, "Содержание общее за ход и содержание одного"}};
    ui::Table t("rows", cols, int(lines.size()), {.rowHeight = 44, .selected = &sel, .emptyIcon = fleet ? "fleet" : "army",
                                                  .emptyText = fleet ? "Кораблей нет" : "Отрядов нет"});
    for (int i : t) {
      const Line& l = lines[size_t(i)];
      a.markUi("mil.row." + std::to_string(i));
      RectF cr = t.cell();
      unitCell(cr, l.row, sel == i, l.row.name != l.row.typeName ? std::string_view(l.row.typeName) : std::string_view());
      cr = t.cell();
      cellLines(cr, fmtCount(l.row.total), std::string(fieldWord(fleet)) + " " + fmtCount(l.field()), ui::Align::Right);
      cr = t.cell();
      cellLines(cr, fmtCount(l.reserve()), l.forming() > 0 ? "+" + fmtCount(l.forming()) : std::string(), ui::Align::Right,
                l.reserve() > 0 ? ui::Ink::Success : ui::Ink::Muted);
      cr = t.cell();
      cellLines(cr, fmtMoney(l.upkeepTotal()), fmtMoney(l.row.upkeep) + " за ед.", ui::Align::Right);
    }
    if (!lines.empty() && t.footer()) {
      t.text("Итого");
      RectF cr = t.cell();
      cellLines(cr, fmtCount(sumTotal), std::string(fieldWord(fleet)) + " " + fmtCount(sumField), ui::Align::Right);
      cr = t.cell();
      cellLines(cr, fmtCount(sumReserve), sumForming > 0 ? "+" + fmtCount(sumForming) : std::string(), ui::Align::Right);
      t.text(fmtMoney(sumUpkeep));
    }
  }
  // Выбор меняется только щелчком или стрелками: новая строка из меню ещё может отсутствовать в мире кадра.
  if (sel != sel0) st.selRow = sel >= 0 && sel < int(lines.size()) ? lines[size_t(sel)].row.id : 0;
  if (st.selRow && !unitRow(a.world(), fid, st.selRow, fleet)) st.selRow = 0;   // строку удалили
  return raceShown;
}

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
  edkit::chipsBegin();   // фишки переносятся по ширине
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
    if (edkit::chip(p.label + " · " + fmtCount(p.n), co) == ui::ChipAction::Click && p.type != SelType::None) a.select(p.type, p.id, true);
  }
  edkit::chipsEnd();
}

// Карточка выбранной строки: правка (узкая таблица), раса, формирование и роспуск, размещение отрядов.
bool rowCard(App& a, Id fid, bool fleet, ForcesState& st, bool editors, bool raceInTable) {
  const World& w = frameWorld(a);
  auto r = unitRow(w, fid, st.selRow, fleet);
  if (!r) return false;
  auto c = rules::calc(w);
  const rules::RowCalc* rc = rowCalc(*c, fid, r->id, fleet);
  i64 field = rc ? rc->field : 0, reserve = rc ? rc->reserve : r->total, forming = rc ? rc->forming : 0;
  bool ro = a.readOnly();
  const ui::Theme& th = ui::theme();
  ui::IdScope scope(i64(r->id));
  ui::Card card({.pad = 12, .tone = ui::Tone::Accent});
  {
    ui::Row head({ui::px(36), ui::fr(1), ui::px(30)}, 36, 10);
    typeTile(ui::next(36, 36), r->icon, th.accent);
    {
      ui::Group g(0, 0);
      ui::label(r->name, {.font = ui::Font::Strong});
      ui::label(r->typeName, {.font = ui::Font::Caption, .ink = ui::Ink::Muted});
    }
    {
      ui::Disabled dis(ro);
      if (ui::iconButton("trash", fleet ? "Удалить судно из таблицы" : "Удалить отряд из таблицы", {.tone = ui::Tone::Danger}))
        askRemoveRow(a, fid, r->id, fleet);
      a.markUi("mil.detail.delete");
    }
  }
  {
    ui::Disabled dis(ro);
    if (editors) {
      ui::prop("Наименование", "edit");
      std::string name = r->name;
      if (ui::textField("name", name, {.placeholder = noun(fleet), .maxLength = 60})) renameRow(a, fid, r->id, fleet, name);
      a.markUi("mil.detail.name");
      ui::prop(fleet ? "Тип судна" : "Тип войск", r->icon);
      int type = r->type;
      typeCombo("type", type, fleet, ro);
      if (type != r->type) setRowType(a, fid, r->id, fleet, type);
      a.markUi("mil.detail.type");
    }
    if (!fleet && !raceInTable) {
      ui::prop("Раса", "race");
      raceCombo(a, "race", fid, r->id, ro);
      a.markUi("mil.detail.race");
    }
    if (editors) {
      if (st.editReserve) {   // п.10.3: напрямую — только в режиме «Правка резерва»
        ui::prop("Численность", fleet ? "fleet" : "users");
        i64 total = r->total;
        if (ui::numberField("total", total, {.min = double(field), .max = 1e12, .steppers = true,
                                             .tooltip = "Не меньше численности " + std::string(fieldWord(fleet)) + ": " + fmtCount(field)}))
          setTotal(a, fid, r->id, total);
        a.markUi("mil.detail.total");
      }
      ui::prop(fleet ? "За корабль" : "За единицу", "coins");
      double up = r->upkeep;
      if (ui::numberField("upkeep", up, {.min = 0, .max = 1e9, .step = 0.5, .digits = 3})) setRowUpkeep(a, fid, r->id, fleet, up);
      a.markUi("mil.detail.upkeep");
    }
    {
      ui::Row rr({ui::fr(1), ui::fr(1)}, 30, 8);
      if (ui::button("Сформировать", {.icon = "plus", .fill = true,
                                      .tooltip = fleet ? "Новые корабли за ресурсы — в резерв через " + nTurns(schema::kFormationTurns)
                                                       : "Новые отряды из населения — в резерв через " + nTurns(schema::kFormationTurns)}))
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
    ui::tag(fmtMoney(rc ? rc->upkeepTotal : 0) + " за ход", ui::Tone::Warning, "coins");
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
    ui::Disabled dis(a.readOnly());
    if (ui::iconButton("close", "Отменить формирование — с возвратом")) cancelForming(a, fid, l.index, l.row.id, l.count);
    a.markUi(std::string(fleet ? "mil.fleet" : "mil.army") + ".forming." + std::to_string(i) + ".cancel");
  }
}

// ---------------------------------------------------------------- флот в торговле
// ТЗ «Общие доработки», п.11: корабли из резерва участвуют в торговле, не выходя на карту. Торговые галеоны приносят
// своё содержание × 2; каждые 10 галеонов без охраны (5 фрегатов или 1 линкор) — 1 % вероятности нападения пиратов.
void tradeFleetSection(App& a, Id fid) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(fid);
  if (!f) return;
  auto c = rules::calc(w);
  const rules::FactionCalc* fc = c->faction(fid);
  i64 inTrade = 0;
  for (const GarrisonEntry& g : f->tradeFleet) inTrade += g.count;
  ui::Section s("Участие флота в торговле", "trade", {.badge = fmtCount(inTrade), .card = false});
  a.markUi("mil.trade");
  if (!s) return;
  if (f->fleet.empty()) {
    ui::label("Кораблей нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "anchor"});
    return;
  }
  const double income = fc ? fc->incTradeFleet : 0, next = fc ? fc->pirateRisk : 0;
  {
    // Риск пиратов: бросок в конце этого хода (сгенерирован в конце прошлого) и новый — по кораблям в торговле.
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
    ui::stat((income > 0 ? "+" : "") + fmtMoney(income), "Прибыль за ход", {.icon = "coins", .tone = ui::Tone::Success,
                                                                            .tooltip = "Торговые галеоны в торговле: содержание × 2"});
    const std::string nextText = "→ " + fmtPct(next, 1);
    ui::stat(fmtPct(f->pirateRisk, 1), "Пираты", {.icon = "dice", .tone = f->pirateRisk > 0 ? ui::Tone::Danger : ui::Tone::Neutral,
                                                  .delta = next - f->pirateRisk, .deltaText = nextText, .invertDelta = true,
                                                  .tooltip = "Вероятность нападения пиратов в конце этого хода (погибнет 5 % галеонов в торговле); "
                                                             "→ сгенерируется в конце хода: 1 % за каждые 10 галеонов без охраны (5 фрегатов или 1 линкор)"});
    a.markUi("mil.trade.pirates");
  }
  const double fleetK = fc ? std::max(0.0, 1.0 + fc->fx[Fx::FleetUpkeepPct] / 100.0) : 1.0;
  struct L {
    UnitRow row;
    i64 trade, reserve;
    double income;
  };
  std::vector<L> lines;
  for (const UnitRow& r : unitRows(w, fid, true)) {
    const rules::RowCalc* rc = fc ? rowCalc(*c, fid, r.id, true) : nullptr;
    const i64 tr = rc ? rc->trade : 0;
    const double inc = r.type == int(ShipType::Galleon) ? double(tr) * std::max(0.0, r.upkeep) * fleetK * schema::kFleetTradeIncome : 0;
    lines.push_back(L{r, tr, rc ? std::max<i64>(0, rc->reserve) : 0, inc});
  }
  const bool ro = a.readOnly();
  ui::Column cols[] = {{"Судно", nullptr, ui::fr(1, 110)},
                       {"В торговле", nullptr, ui::px(104), ui::Align::Left, false, "Корабли из резерва в торговле"},
                       {"Резерв", nullptr, ui::px(64), ui::Align::Right, false, "Свободно в резерве"},
                       {"", "coins", ui::px(70), ui::Align::Right, false, "Прибыль за ход"}};
  ui::Table t("trade", cols, int(lines.size()), {.rowHeight = 40, .selectable = false});
  for (int i : t) {
    const L& l = lines[size_t(i)];
    RectF cr = t.cell();
    unitCell(cr, l.row, l.trade > 0);
    t.cell();
    {
      ui::Disabled dis(ro);
      i64 n = l.trade;
      if (ui::numberField("n", n, {.min = 0, .max = double(l.trade + l.reserve), .tooltip = "Не больше резерва: " + fmtCount(l.reserve) + " свободно"})) {
        const Id row = l.row.id;
        a.act("Флот в торговле", [&](Tx& tx) { rules::setTradeFleet(tx, fid, row, n); }, {.coalesce = "tradefleet:" + std::to_string(row)});
      }
      a.markUi("mil.trade." + std::to_string(l.row.id));
    }
    t.text(fmtCount(l.reserve), l.reserve > 0 ? ui::Ink::Success : ui::Ink::Muted);
    t.text(l.income > 0 ? "+" + fmtMoney(l.income) : std::string("—"), l.income > 0 ? ui::Ink::Success : ui::Ink::Muted);
  }
}

// ---------------------------------------------------------------- объекты и содержание
// Объекты фракции на карте (щелчок — выделить и показать).
void objectsOnMap(App& a, Id fid, bool fleet) {
  const World& w = frameWorld(a);
  std::vector<const Army*> list;
  w.armies.each([&](const Army& ar) {
    if (ar.isFleet() != fleet) return;
    for (const ArmyGroup& g : ar.groups)
      if (g.faction == fid) {
        list.push_back(&ar);
        return;
      }
  });
  std::sort(list.begin(), list.end(), [](const Army* x, const Army* y) { return compareRu(x->name, y->name) < 0; });
  std::string badge = std::to_string(list.size());
  ui::Section s(fleet ? "Флоты на карте" : "Войска на карте", fleet ? "fleet" : "army", {.badge = badge, .card = false});
  if (!s) return;
  if (list.empty()) {
    bool ro = a.readOnly();
    ui::Disabled dis(ro);
    if (ui::emptyState(fleet ? "fleet" : "army", fleet ? "Флотов на карте нет." : "Войск на карте нет.", fleet ? "Поставить флот" : "Поставить войско",
                       fleet ? "tool-fleet" : "tool-army"))
      startPlacing(a, fleet ? ArmyKind::Fleet : ArmyKind::Army, fid);
    a.markUi(fleet ? "mil.fleet.placeEmpty" : "mil.army.placeEmpty");
    return;
  }
  for (const Army* ar : list) {
    std::string sub = w.provinceName(provinceUnder(w, ar->pos));
    if (ar->allied()) {
      std::vector<std::string> names;
      for (Id f : factionsIn(*ar))
        if (f != fid) names.push_back(w.factionName(f));
      sub = "союз с " + join(names, ", ") + " · " + sub;
    }
    if (objectItem(w, *ar, a.ui.sel == Selection{SelType::Army, ar->id}, sub)) a.select(SelType::Army, ar->id, true);
  }
  ui::Disabled dis(a.readOnly());
  ui::HStack hs(0, ui::Align::Left, 6);
  if (ui::button(fleet ? "Поставить флот" : "Поставить войско", {.variant = ui::Variant::Ghost, .icon = fleet ? "tool-fleet" : "tool-army", .size = ui::Size::Small}))
    startPlacing(a, fleet ? ArmyKind::Fleet : ArmyKind::Army, fid);
}

// Списание из казны за ход: войска + флот (ТЗ 1.c.ii).
void upkeepCard(App& a, Id fid, bool fleet) {
  auto c = rules::calc(frameWorld(a));
  const rules::FactionCalc* fc = c->faction(fid);
  double army = fc ? fc->expArmy : 0, fl = fc ? fc->expFleet : 0;
  ui::Card card({.pad = 12});
  ui::Row r({ui::px(36), ui::fr(1), ui::px(120)}, 40, 10);
  {
    RectF ic = ui::next(36, 36);
    ui::draw::rect(ic, ui::toneColor(ui::Tone::Danger).alpha(0.14f), 10);
    ui::draw::icon("expense", ic.inset(9), ui::toneColor(ui::Tone::Danger));
  }
  {
    ui::Group g(0, 0);
    ui::label("Из казны за ход", {.font = ui::Font::Strong});
    std::string parts = std::string(fleet ? "флот " : "войска ") + fmtMoney(fleet ? fl : army) + " + " + (fleet ? "войска " : "флот ") + fmtMoney(fleet ? army : fl);
    ui::label(parts, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  ui::label(fmtSigned(-(army + fl), 3), {.font = ui::Font::Number, .ink = army + fl > 0 ? ui::Ink::Danger : ui::Ink::Muted, .align = ui::Align::Right,
                                         .tooltip = "Сумма содержания общего из таблиц войск и флота"});
}

void drawForces(App& a, Id fid, bool fleet, bool fullscreen) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(fid);
  if (!f) return;
  ui::IdScope scope(fleet ? "fleet" : "army");
  auto& st = ui::state<ForcesState>(ui::id("forces"));
  const float aw = ui::avail().w;
  bool wide = aw >= kWide;
  size_t n = fleet ? f->fleet.size() : f->army.size();
  if (n > 0) {   // пустая таблица — без нулевых показателей
    statTiles(a, fid, fleet, wide);
    ui::spacer(4);
  }
  {
    ui::Section s(fleet ? "Корабли" : "Отряды", fleet ? "fleet" : "army",
                  {.badge = std::to_string(n), .card = false, .actionIcon = a.readOnly() ? nullptr : "plus",
                   .actionTooltip = fleet ? "Добавить судно" : "Добавить отряд"});
    if (s.action()) ui::openPopup("addrow");
    a.markUi(fleet ? "mil.fleet.add" : "mil.army.add");
    addRowMenu(a, "addrow", fid, fleet, st);
    if (s) {
      if (n == 0) {
        ui::Disabled dis(a.readOnly());
        if (ui::emptyState(fleet ? "fleet" : "army", fleet ? "В таблице флота пока нет судов." : "В таблице войск пока нет отрядов.",
                           fleet ? "Добавить судно" : "Добавить отряд", "plus"))
          ui::openPopup("addrow2");
        a.markUi(fleet ? "mil.fleet.empty" : "mil.army.empty");
        addRowMenu(a, "addrow2", fid, fleet, st);
      } else {
        {
          ui::HStack hs(30, ui::Align::Left, 8);
          ui::Disabled dis(a.readOnly());
          ui::toggle("Правка резерва", st.editReserve);
          ui::tooltip("Общая численность строк правится напрямую, без формирования и роспуска");
          a.markUi(fleet ? "mil.fleet.editReserve" : "mil.army.editReserve");
        }
        const bool raceShown = forcesTable(a, fid, fleet, st, wide);
        if (st.selRow) {
          if (rowCard(a, fid, fleet, st, !wide, raceShown)) {
            if (st.shownRow != st.selRow) ui::scrollToItem();   // новая карточка — в видимую часть
            st.shownRow = st.selRow;
          }
        } else {
          ui::label("Строка не выбрана", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
        }
      }
    }
  }
  formingList(a, fid, fleet);
  if (fleet && n > 0) {
    ui::spacer(4);
    tradeFleetSection(a, fid);
  }
  if (!fullscreen) {   // во весь экран списание за ход — одна карточка над обеими таблицами
    ui::spacer(4);
    upkeepCard(a, fid, fleet);
    ui::spacer(4);
    objectsOnMap(a, fid, fleet);
    ui::spacer(2);
    if (ui::button("Таблицы войск и флота", {.variant = ui::Variant::Ghost, .icon = "maximize", .size = ui::Size::Small,
                                             .tooltip = "Открыть таблицы во весь экран"}))
      a.openEditor("military", fid);
    a.markUi("mil.fullscreen");
  }
}

void drawArmyTab(App& a, Id fid) { drawForces(a, fid, false, false); }
void drawFleetTab(App& a, Id fid) { drawForces(a, fid, true, false); }

// ---------------------------------------------------------------- полноэкранные таблицы
void drawEditor(App& a, Id arg) {
  const World& w = frameWorld(a);
  struct EditorState {
    Id cur = 0;   // показанная фракция (меняется выбором вверху)
    Id arg = 0;   // фракция, с которой редактор открыт
  };
  auto& es = ui::state<EditorState>(ui::id("faction"));
  if (arg != es.arg) {
    es.arg = arg;
    if (arg) es.cur = arg;
  }
  Id& cur = es.cur;
  if (!w.faction(cur)) {
    cur = 0;
    std::vector<const Faction*> all = w.factions.all();
    std::sort(all.begin(), all.end(), [](const Faction* x, const Faction* y) {
      if (x->kind != y->kind) return x->kind < y->kind;
      return compareRu(x->name, y->name) < 0;
    });
    if (!all.empty()) cur = all.front()->id;
  }
  if (!cur) {
    ui::emptyState("crown", "Фракций пока нет.");
    return;
  }
  const Faction& f = *w.faction(cur);
  {
    ui::Row head({ui::px(54), ui::fr(1), ui::px(300)}, 40, 12);
    factionFlag(w, cur, 54, 36);
    {
      ui::Group g(0, 0);
      ui::caption(f.isGuild() ? "Торговая гильдия" : "Государство");
      ui::label(f.name.empty() ? std::string("Без названия") : f.name, {.font = ui::Font::Heading});
    }
    Id pick = cur;
    if (w::factionPicker("pick", pick, w::FactionFilter::Any, "")) cur = pick;
    a.markUi("mil.editor.faction");
  }
  ui::spacer(4);
  ui::Scroll sc("body");
  ui::IdScope s{i64(cur)};
  upkeepCard(a, cur, false);   // ТЗ 1.c.ii: содержание из обеих таблиц — из казны каждый ход
  ui::spacer(8);
  ui::label("Войска", {.font = ui::Font::Title, .icon = "army"});
  drawForces(a, cur, false, true);
  ui::spacer(12);
  ui::label("Флот", {.font = ui::Font::Title, .icon = "fleet"});
  drawForces(a, cur, true, true);
  ui::spacer(8);
}

TabReg armyTab({"faction.army", "army", "Войска", 50, SelType::Faction, nullptr, drawArmyTab});
TabReg fleetTab({"faction.fleet", "fleet", "Флот", 52, SelType::Faction, nullptr, drawFleetTab});
EditorReg editorReg({"military", "Войска и флот", drawEditor, "army"});

}  // namespace
}  // namespace rg::app::mil
