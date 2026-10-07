// Regnum — вкладки фракции «Войска» и «Флот» (ТЗ 1.c.i–ii, 1.d.iii; «Общие доработки», п.10–11; «Виды государств»,
// п.2; «Ввод новых механик», п.1–5) и полноэкранные таблицы войск и флота.
//
// Таблица строк: тип (14 типов войск или 3 типа судов), наименование, раса отряда, ключевой ресурс юнита, цена найма
// за юнит, численность общая, в поле (в море), в резерве (и формируется), содержание одного и общее (с модификатором
// содержания). Отряды формируются из населения (нежить — из трупов, демоны — из демонической энергии; звери, чудовища,
// военные механизмы и элементали — без людей) за ключевой и дополнительные ресурсы и эссенции элементов; корабли — за
// ресурсы констант стоимости; через 2 хода — в резерв; резерв распускается обратно в население. Элементали
// содержатся эссенциями, а не золотом. Особые отряды справочника нанимаются, пока у государства достроена постройка
// доступа; их тип, раса и цены — из справочника. Общая численность правится напрямую только в режиме «Правка
// резерва» (без расхода людей, ресурсов и эссенций). Широкая таблица правится в ячейках и показывает столбцы по
// ширине (наименованию — не меньше kNameMin); узкая — в две строки, правка — в карточке выбранной строки (там же
// списки ресурсов и эссенций на юнит). Во вкладке флота — участие флота в торговле.
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

constexpr float kWide = 640;   // ширина, начиная с которой таблица правится в ячейках
// Столбцы широкой таблицы (точки): обязательные и те, что показываются, пока наименованию хватает kNameMin.
constexpr float kNameMin = 240, kTypeIcon = 48, kTypeLabel = 196, kRaceW = 150, kKeyIcon = 64, kKeyFull = 180, kCostW = 156,
                kTotalW = 92, kFieldW = 80, kReserveW = 84, kUpkeepW = 104, kUpkeepTotalW = 96, kActionsW = 104;

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

// Добавление ресурса или эссенции на юнит (всплывающее окно у карточки строки).
struct AddPart {
  Id pick = 0;
  double amount = 1;
};

const char* noun(bool fleet) { return fleet ? "Судно" : "Отряд"; }
const char* fieldWord(bool fleet) { return fleet ? "в море" : "в поле"; }

const ArmyRow* armyRowOf(const World& w, Id fid, Id row) {
  const Faction* f = w.faction(fid);
  return f ? f->armyRow(row) : nullptr;
}

// Раса строки армии (пусто — не строка армии).
std::string rowRace(const World& w, Id fid, Id row) {
  const ArmyRow* r = armyRowOf(w, fid, row);
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

// Цвет значка ресурса или эссенции, различимый на тёмном фоне (чёрные драконы, обсидиан).
Color legible(Color c) { return c.luminance() < 0.12f ? c.lighten(0.45f) : c; }

std::string resName(const World& w, Id res) {
  const CatalogItem* c = w.resource(res);
  return c && !c->name.empty() ? c->name : std::string("Ресурс");
}
std::string essName(const World& w, Id e) {
  const CatalogItem* c = w.essence(e);
  return c && !c->name.empty() ? c->name : std::string("Эссенция");
}

// Откуда ключевой ресурс типа: «из группы «Звери / Ездовые наземные», 1 на юнит».
std::string keyRuleText(const World& w, UnitType type) {
  const schema::KeyRule k = schema::keyRule(type);
  if (k.resKey) return "Ключевой ресурс юнита: «Запчасти механизмов», не меньше 1 на юнит";
  if (!k.group) return {};
  std::string s = "Ключевой ресурс юнита: из группы «" + rules::groupPath(w, w.catalogs->groupId(k.group)) + "»";
  if (k.exclude) s += " (кроме «" + rules::groupPath(w, w.catalogs->groupId(k.exclude)) + "»)";
  return s + ", 1 на юнит";
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

// Тип строки: войска — по правилам (имя и раса по умолчанию следуют за типом, неподходящий ключевой ресурс снимается),
// судно — имя по типу следует за типом.
void setRowType(App& a, Id fid, Id row, bool fleet, int type) {
  if (!fleet) {
    a.act("Тип войск", [&](Tx& tx) { rules::setRowType(tx, fid, row, UnitType(type)); });
    return;
  }
  a.act("Тип судна", [&](Tx& tx) {
    editRow(tx, fid, row, true, [&](auto& r) {
      using T = std::decay_t<decltype(r)>;
      if constexpr (std::is_same_v<T, FleetRow>) {
        if (r.name == schema::shipType(r.type).name) r.name = schema::shipType(ShipType(type)).name;
        r.type = ShipType(type);
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

// Почему особый отряд недоступен: какая постройка доступа нужна (из правил — постройки с этим отрядом).
std::string specialNeed(const World& w, Id special) {
  std::vector<std::string> names;
  for (Id b : rules::specialBuildings(w, special))
    if (const Building* bd = w.building(b)) names.push_back("«" + (bd->name.empty() ? std::string("Без названия") : bd->name) + "»");
  if (names.empty()) return "Ни одна постройка не открывает этот отряд";
  return "Нужна постройка: " + join(names, " или ");
}

// Меню выбора типа для новой строки (открывается ui::openPopup(id)): все типы по порядку и особые отряды справочника
// (доступные — новая строка; без постройки доступа — серые, в подсказке — какая постройка нужна).
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
  const World& w = frameWorld(a);
  const Faction* f = w.faction(fid);
  if (!fleet && f && f->isState() && !w.catalogs->specials.empty()) {
    ui::menuSeparator();
    ui::menuHeader("Особые отряды");
    for (const SpecialUnit& s : w.catalogs->specials) {
      ui::IdScope sc(i64(s.id) + 0x5e000000LL);
      const bool can = rules::hasSpecialAccess(w, fid, s.id);
      const std::string name = s.name.empty() ? std::string("Без названия") : s.name;
      if (ui::menuItem(name, {.icon = "special-unit", .disabled = !can})) {
        const Id sid = s.id;
        Id nid = 0;
        if (a.act("Добавить особый отряд", [&](Tx& tx) { nid = rules::addSpecialRow(tx, fid, sid); }) && nid) st.selRow = nid;
      }
      std::string tip = can ? std::string(schema::unitType(s.type).name) + (s.desc.empty() ? std::string() : "\n" + s.desc) : specialNeed(w, s.id);
      ui::tooltip(tip);
      a.markUi("mil.addspecial." + std::to_string(s.id));
    }
  }
  ui::endMenu();
}

// Раса отряда (ТЗ «Виды государств», п.2): значения константы «Расы для отрядов»; по умолчанию — по виду
// государства и типу отряда. У элементалей — только «Элементали», у особого отряда — из справочника.
void raceCombo(App& a, std::string_view id, Id fid, Id row, bool disabled) {
  const World& w = frameWorld(a);
  const ArmyRow* r = armyRowOf(w, fid, row);
  if (!r) return;
  const std::string cur = rules::unitRace(w, fid, *r);
  const bool locked = schema::isElemental(r->type) || r->special;
  std::vector<std::string> races = rules::unitRaces(w);
  if (std::find(races.begin(), races.end(), cur) == races.end()) races.push_back(cur);   // раса не из константы
  std::vector<ui::Option> opts;
  int idx = -1;
  for (size_t i = 0; i < races.size(); i++) {
    opts.push_back(ui::Option{races[i], raceIcon(races[i])});
    if (races[i] == cur) idx = int(i);
  }
  const int before = idx;
  ui::combo(id, idx, opts, {.disabled = disabled || locked, .tooltip = "Раса отряда"});
  if (idx != before && idx >= 0 && idx < int(races.size())) {
    const std::string nr = races[size_t(idx)];
    a.act("Раса отряда", [&](Tx& tx) { rules::setRowRace(tx, fid, row, nr); });
  }
}

// Ключевой ресурс строки (ТЗ «Ввод новых механик», п.2): выбор из подходящих типу ресурсов; не задан — красная
// обводка (найм заблокирован). Особый отряд — только для чтения (из справочника).
void keyCombo(App& a, std::string_view id, Id fid, const ArmyRow& r, bool disabled) {
  const World& w = frameWorld(a);
  Id v = r.keyRes;
  const bool missing = !rules::keyAllowed(w, r.type, r.keyRes);
  const std::string tip = missing ? "Не указан ключевой ресурс — найм недоступен\n" + keyRuleText(w, r.type) : keyRuleText(w, r.type);
  const bool changed = w::resourceFrom(id, v, rules::keyResources(w, r.type), "Не выбран", disabled || r.special, tip);
  if (missing) ui::draw::rectStroke(ui::lastItem().rect.expand(1), ui::theme().danger, 8, 1.5f);
  if (changed && v != r.keyRes) {
    const Id row = r.id;
    const double per = r.keyPer;
    a.act("Ключевой ресурс", [&](Tx& tx) { rules::setRowKey(tx, fid, row, v, v ? per : 1); });
  }
}

// ---------------------------------------------------------------- цена и содержание
// Часть цены или содержания: значок и цвет (ресурс, эссенция, население), название, количество.
struct Part {
  enum Kind : u8 { People, Res, Ess };
  const char* icon = "resource";
  Color color{0, 0, 0, 0};   // a = 0 — приглушённый цвет темы
  std::string name;
  double v = 0;
  Kind kind = Res;
  Id id = 0;
};

// Цена найма: население, ключевой ресурс строки (первым), прочие ресурсы (трупы, энергия, дополнительные), эссенции.
std::vector<Part> costParts(const World& w, const rules::RecruitCost& c, Id key = 0) {
  std::vector<Part> out;
  if (c.people > 0) out.push_back(Part{"population", Color(0, 0, 0, 0), "Население", double(c.people), Part::People, 0});
  auto res = [&](Id r, double v) { out.push_back(Part{w::resourceIcon(w, r), legible(w::resourceColor(w, r)), resName(w, r), v, Part::Res, r}); };
  if (auto it = c.res.find(key); key && it != c.res.end()) res(key, it->second);
  for (auto& [r, v] : c.res)
    if (r != key) res(r, v);
  for (auto& [e, v] : c.ess) out.push_back(Part{"essence", legible(w::essenceColor(w, e)), essName(w, e), v, Part::Ess, e});
  return out;
}

std::vector<Part> essenceParts(const World& w, const std::map<Id, double>& m) {
  std::vector<Part> out;
  for (auto& [e, v] : m)
    if (v > 0) out.push_back(Part{"essence", legible(w::essenceColor(w, e)), essName(w, e), v, Part::Ess, e});
  return out;
}

// Части в ячейке таблицы: значок и число подряд; не поместилось — «+N»; полный список — в подсказке (без перехвата
// щелчка по строке).
void partsCell(std::string_view key, RectF r, const std::vector<Part>& parts, std::string_view title, ui::Align align = ui::Align::Left) {
  const ui::Theme& t = ui::theme();
  if (parts.empty()) {
    ui::draw::text("—", r, ui::Font::Body, t.textMuted, align);
    return;
  }
  constexpr float is = 14, gap = 8;
  std::vector<float> widths;
  std::vector<std::string> nums;
  for (const Part& p : parts) {
    nums.push_back(fmtMoney(p.v));
    widths.push_back(is + 3 + ui::measure(nums.back(), ui::Font::Small));
  }
  size_t shown = 0;
  float used = 0;
  for (size_t i = 0; i < parts.size(); i++) {
    const float more = i + 1 < parts.size() ? 26.f : 0.f;   // место под «+N»
    if (shown > 0 && used + widths[i] > r.w - more) break;
    used += widths[i] + (i + 1 < parts.size() ? gap : 0.f);
    shown++;
  }
  const std::string rest = shown < parts.size() ? "+" + std::to_string(parts.size() - shown) : std::string();
  const float restW = rest.empty() ? 0 : ui::measure(rest, ui::Font::Small) + 2;
  float x = align == ui::Align::Right ? r.right() - std::min(r.w, used + restW) : r.x;
  for (size_t i = 0; i < shown; i++) {
    ui::draw::icon(parts[i].icon, RectF{x, std::round(r.cy() - is * 0.5f), is, is}, parts[i].color.a ? parts[i].color : t.textDim);
    ui::draw::text(nums[i], RectF{x + is + 3, r.y, std::max(0.f, std::min(widths[i] - is - 3 + 1, r.right() - x - is - 3)), r.h}, ui::Font::Small, t.text);
    x += widths[i] + gap;
  }
  if (!rest.empty()) ui::draw::text(rest, RectF{x, r.y, std::max(0.f, r.right() - x), r.h}, ui::Font::Small, t.textMuted);
  std::string tip(title);
  for (const Part& p : parts) tip += "\n" + p.name + ": " + fmtMoney(p.v);
  cellTip(key, r, tip);
}

// Полная цена формирования (окно «Сформировать»): население (у типов без людей — нет), ключевой ресурс, трупы и
// энергия, дополнительные ресурсы, эссенции; нехватка — красным (причины — строками ниже, от правил).
void costList(App& a, Id fid, const rules::RecruitCost& c, Id key) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(fid);
  const std::vector<Part> parts = costParts(w, c, key);
  if (parts.empty()) {
    ui::label("Без затрат", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "check"});
    return;
  }
  ui::IdScope scope("cost");
  ui::Row row({ui::px(16), ui::fr(1), ui::px(104)}, 22, 6);
  for (const Part& p : parts) {
    ui::IdScope s(i64(p.id) * 4 + i64(p.kind));
    const double have = !f ? 0 : p.kind == Part::People ? double(f->isState() ? rules::statePopulation(w, fid) : 0)
                             : p.kind == Part::Res      ? f->stock(p.id)
                                                        : f->essence(p.id);
    const bool lack = f && have + 1e-9 < p.v;
    if (p.kind == Part::People) ui::icon("population", ui::Ink::Dim, 16, "Из населения государства — поровну со всех провинций");
    else ui::iconColored(p.icon, p.color, 16);
    const std::string tag = p.kind == Part::Res && p.id == key ? p.name + " · ключевой" : p.name;
    const float room = ui::avail().w;
    ui::label(tag, {.font = ui::Font::Small, .ink = ui::Ink::Dim, .tooltip = ui::measure(tag, ui::Font::Small) > room ? std::string_view(tag) : std::string_view()});
    ui::label(p.kind == Part::People ? fmtCount(i64(p.v)) : fmtMoney(p.v),
              {.font = ui::Font::Strong, .ink = lack ? ui::Ink::Danger : ui::Ink::Normal, .align = ui::Align::Right,
               .tooltip = lack ? "Есть " + (p.kind == Part::People ? fmtCount(i64(have)) : fmtMoney(std::max(0.0, have))) : std::string()});
  }
}

// ---------------------------------------------------------------- формирование и роспуск
// «Сформировать» (п.10.1–10.2, 10.5; «Ввод новых механик», п.1–4): число, полная цена, причины отказа; через 2 хода —
// в резерв.
void recruitPopup(App& a, const char* id, Id fid, const UnitRow& row, bool fleet) {
  if (!ui::beginPopup(id, {.width = 320})) return;
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
  const ArmyRow* ar = fleet ? nullptr : armyRowOf(w, fid, row.id);
  costList(a, fid, cost, ar ? ar->keyRes : 0);
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

// «Распустить» (п.10.4): из резерва; воины возвращаются в население (нежить — в трупы, демоны — в энергию; звери,
// чудовища, механизмы и элементали — никуда).
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
  if (!fleet && schema::needsPeople(UnitType(row.type))) {
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

// ---------------------------------------------------------------- ресурсы и эссенции на юнит (карточка строки)
enum class PerKind : u8 { Extra, Essence, EssUpkeep };

const std::map<Id, double>& perMap(const ArmyRow& r, PerKind k) {
  return k == PerKind::Extra ? r.extra : k == PerKind::Essence ? r.essence : r.essUpkeep;
}
const char* perMark(PerKind k) { return k == PerKind::Extra ? "extra" : k == PerKind::Essence ? "ess" : "essup"; }
const char* perWhat(PerKind k) {
  return k == PerKind::Extra ? "Дополнительный ресурс на юнит при найме" : k == PerKind::Essence ? "Эссенция элемента на юнит при найме"
                                                                                                   : "Содержание эссенцией на юнит за ход";
}

void setPer(Tx& tx, Id fid, Id row, PerKind k, Id id, double v) {
  if (k == PerKind::Extra) rules::setRowExtra(tx, fid, row, id, v);
  else if (k == PerKind::Essence) rules::setRowEssence(tx, fid, row, id, v);
  else rules::setRowEssUpkeep(tx, fid, row, id, v);
}

// Строки списка: значок, название, количество на юнит (≥ 0; 0 — убрать), крестик. locked — только для чтения
// (особый отряд — из справочника; просмотр прошлого хода).
void perRows(App& a, Id fid, const ArmyRow& r, PerKind kind, bool locked) {
  const World& w = frameWorld(a);
  const std::map<Id, double>& m = perMap(r, kind);
  if (m.empty()) return;
  const char* mark = perMark(kind);
  ui::IdScope scope(mark);
  const Id row = r.id;
  ui::Row line({ui::px(16), ui::fr(1), ui::px(112), ui::px(30)}, 30, 6);   // по строке на запись
  for (auto& [id, v0] : m) {
    ui::IdScope s{i64(id)};
    const bool res = kind == PerKind::Extra;
    const std::string name = res ? resName(w, id) : essName(w, id);
    ui::iconColored(res ? w::resourceIcon(w, id) : "essence", legible(res ? w::resourceColor(w, id) : w::essenceColor(w, id)), 16);
    const float room = ui::avail().w;
    ui::label(name, {.tooltip = ui::measure(name) > room ? std::string_view(name) : std::string_view()});
    {
      ui::Disabled dis(locked);
      double v = v0;
      const Id key = id;
      if (ui::numberField("n", v, {.min = 0, .max = 1e12, .step = 1, .digits = 3, .tooltip = std::string(perWhat(kind)) + "; 0 — убрать"}))
        a.act(perWhat(kind), [&](Tx& tx) { setPer(tx, fid, row, kind, key, v); },
              {.coalesce = std::string("per:") + mark + ":" + std::to_string(row) + ":" + std::to_string(key)});
      a.markUi(std::string("mil.detail.") + mark + "." + std::to_string(id));
    }
    if (locked) {
      ui::next(30, 30);
    } else {
      const Id key = id;
      if (ui::iconButton("close", "Убрать")) a.act(perWhat(kind), [&](Tx& tx) { setPer(tx, fid, row, kind, key, 0); });
      a.markUi(std::string("mil.detail.") + mark + "." + std::to_string(id) + ".remove");
    }
  }
}

// Кнопка «+ Ресурс» или «+ Эссенция» и её окно: ресурс — сначала группа, затем ресурс; эссенция — из справочника;
// количество на юнит (> 0).
void perAddButton(App& a, Id fid, const ArmyRow& r, PerKind kind) {
  const World& w = frameWorld(a);
  const std::map<Id, double>& m = perMap(r, kind);
  const char* mark = perMark(kind);
  ui::IdScope scope(mark);   // две кнопки «Эссенция» у элементалей (найм и содержание)
  const std::string popup = std::string("add-") + mark;
  if (ui::button(kind == PerKind::Extra ? "Ресурс" : "Эссенция", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small,
                                                                 .tooltip = perWhat(kind)}))
    ui::openPopup(popup);
  a.markUi(std::string("mil.detail.") + mark + ".add");
  if (!ui::beginPopup(popup, {.width = 330})) return;
  auto& st = ui::state<AddPart>(ui::id(popup + ".state"));
  ui::caption(perWhat(kind));
  if (kind == PerKind::Extra) {
    std::vector<Id> only;   // ещё не в списке
    for (Id x : rules::resourcesIn(w, 0))
      if (!m.count(x)) only.push_back(x);
    if (st.pick && (m.count(st.pick) || !w.resource(st.pick))) st.pick = 0;
    const RectF at = ui::avail();
    w::resourceByGroup("pick", st.pick, false, &only);
    a.markUi(std::string("mil.") + mark + ".pick", RectF{at.x, at.y, at.w, 30});
  } else {
    if (st.pick && (m.count(st.pick) || !w.essence(st.pick))) st.pick = 0;
    w::essencePicker("pick", st.pick);
    a.markUi(std::string("mil.") + mark + ".pick");
  }
  ui::prop(kind == PerKind::EssUpkeep ? "За ход на юнит" : "На юнит", kind == PerKind::Extra ? "resource" : "essence");
  ui::numberField("amount", st.amount, {.min = 0, .max = 1e12, .step = 1, .digits = 3, .tooltip = perWhat(kind)});
  a.markUi(std::string("mil.") + mark + ".amount");
  if (ui::button("Добавить", {.variant = ui::Variant::Primary, .icon = "plus", .fill = true, .disabled = !st.pick || !(st.amount > 0)})) {
    const Id key = st.pick, row = r.id;
    const double v = st.amount;
    if (a.act(perWhat(kind), [&](Tx& tx) { setPer(tx, fid, row, kind, key, v); })) {
      st = AddPart{};
      ui::closePopup();
    }
  }
  a.markUi(std::string("mil.") + mark + ".ok");
  ui::endPopup();
}

// Цена и содержание строки армии в карточке: ключевой ресурс (если не в таблице; у механизмов — и сколько на юнит),
// дополнительные ресурсы и эссенции на юнит (у элементалей — только эссенции) и содержание элементалей эссенциями.
void armyPrice(App& a, Id fid, const ArmyRow& r, bool keyInTable, bool ro) {
  const bool locked = ro || r.special;
  const bool elem = schema::isElemental(r.type);
  if (schema::needsKeyResource(r.type)) {
    // Военные механизмы: запчастей на юнит — не меньше 1 (у прочих типов — ровно 1).
    const bool machines = !schema::keyRule(r.type).fixedOne;
    auto perField = [&] {
      ui::Disabled dis(locked || !r.keyRes);
      double per = r.keyPer;
      if (ui::numberField("keyper", per, {.min = 1, .max = 1e12, .step = 1, .digits = 3, .icon = "hammer",
                                          .tooltip = r.keyRes ? "Запчастей механизмов на юнит — не меньше 1" : "Сначала выберите ключевой ресурс"})) {
        const Id fac = fid, row = r.id, res = r.keyRes;
        a.act("Ключевой ресурс на юнит", [&](Tx& tx) { rules::setRowKey(tx, fac, row, res, per); }, {.coalesce = "keyper:" + std::to_string(row)});
      }
      a.markUi("mil.detail.keyper");
    };
    if (!keyInTable) {   // во всю ширину карточки: названия ездовых и чудовищ длинные
      ui::caption("Ключевой ресурс");
      const ui::Len withPer[] = {ui::fr(1), ui::px(112)}, alone[] = {ui::fr(1)};
      ui::Row kr(machines ? std::span<const ui::Len>(withPer) : std::span<const ui::Len>(alone), 30, 6);
      {
        ui::Disabled dis(ro);
        keyCombo(a, "key", fid, r, ro);
        a.markUi("mil.detail.key");
      }
      if (machines) perField();
    } else if (machines) {
      ui::prop("Запчастей на юнит", "hammer");
      perField();
    }
  }
  const bool hire = (!elem && !r.extra.empty()) || !r.essence.empty();
  if (hire || !locked) {
    ui::caption(elem ? "Эссенции за юнит" : "Дополнительно за юнит");
    if (!elem) perRows(a, fid, r, PerKind::Extra, locked);
    perRows(a, fid, r, PerKind::Essence, locked);
    if (!locked) {
      ui::HStack hs(24, ui::Align::Left, 6);
      if (!elem) perAddButton(a, fid, r, PerKind::Extra);
      perAddButton(a, fid, r, PerKind::Essence);
    }
  }
  // Элементали содержатся эссенциями, а не золотом (ТЗ «Ввод новых механик», п.3.2).
  if (elem && (!r.essUpkeep.empty() || !locked)) {
    ui::caption("Содержание эссенциями за ход");
    perRows(a, fid, r, PerKind::EssUpkeep, locked);
    if (!locked) {
      ui::HStack hs(24, ui::Align::Left, 6);
      perAddButton(a, fid, r, PerKind::EssUpkeep);
    }
  }
}

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
    ui::stat(fmtMoney(upkeep), "Содержание",
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
  edkit::chipsBegin();
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
    edkit::chip(essName(w, e) + " · " + fmtMoney(flow.upkeep), co);
  }
  edkit::chipsEnd();
  a.markUi("mil.army.essupkeep");
}

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

// Какие необязательные столбцы широкой таблицы показать при ширине aw: по порядку, пока наименованию остаётся не
// меньше kNameMin (итог за ход, ключевой ресурс с названием, цена, раса, «в поле», тип с подписью).
struct WideCols {
  bool upkeepTotal = false, keyFull = false, cost = false, race = false, field = false, typeLabel = false;
};
WideCols wideCols(float aw, bool fleet) {
  float budget = aw - kNameMin - (kTypeIcon + kTotalW + kReserveW + kUpkeepW + kActionsW + (fleet ? 0 : kKeyIcon));
  WideCols c;
  auto take = [&](bool& on, float w) {
    if (budget >= w) {
      on = true;
      budget -= w;
    }
  };
  take(c.upkeepTotal, kUpkeepTotalW);
  if (!fleet) {
    take(c.keyFull, kKeyFull - kKeyIcon);
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
  for (const Part& p : parts) tip += "\n" + p.name + ": " + fmtMoney(p.v);
  cellTip(key, r, tip);
}

// Таблица строк. Возвращает, какие поля карточки уже показаны столбцами (раса, ключевой ресурс).
struct Shown {
  bool race = false, key = false;
};
Shown forcesTable(App& a, Id fid, bool fleet, ForcesState& st, bool wide) {
  const World& w = frameWorld(a);
  auto c = rules::calc(w);
  const Faction* fac = w.faction(fid);
  std::vector<Line> lines;
  for (UnitRow& r : unitRows(w, fid, fleet)) {
    Line l{r, rowCalc(*c, fid, r.id, fleet), fleet ? std::string() : rowRace(w, fid, r.id), {}, {}};
    if (const ArmyRow* ar = fleet || !fac ? nullptr : fac->armyRow(r.id)) {
      if (ar->keyRes) l.keyName = resName(w, ar->keyRes);
      l.essTotal = essenceUpkeep(w, fid, *ar);
    }
    lines.push_back(std::move(l));
  }
  int sel = -1;
  for (size_t i = 0; i < lines.size(); i++)
    if (lines[i].row.id == st.selRow) sel = int(i);
  bool ro = a.readOnly();
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
    shown.key = !fleet;
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
    if (!fleet)
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
      ui::Disabled dis(ro);
      // Тип: выпадающий список или значок с меню типов (особый отряд — из справочника).
      t.cell();
      if (wc.typeLabel) {
        int type = l.row.type;
        typeCombo("type", type, fleet, ro || special);
        if (type != l.row.type) setRowType(a, fid, l.row.id, fleet, type);
      } else {
        ui::Disabled lock(special);
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
      t.cell();
      std::string name = l.row.name;
      if (ui::textField("name", name, {.placeholder = noun(fleet), .icon = special ? "special-unit" : nullptr, .maxLength = 60,
                                       .tooltip = special ? "Особый отряд справочника" : ""}))
        renameRow(a, fid, l.row.id, fleet, name);
      a.markUi(mark + ".name");
      if (wc.race) {
        t.cell();
        raceCombo(a, "race", fid, l.row.id, ro);
        a.markUi(mark + ".race");
      }
      if (ar) {
        t.cell();
        if (schema::needsKeyResource(ar->type)) keyCombo(a, "key", fid, *ar, ro);
        else ui::label("—", {.ink = ui::Ink::Muted, .tooltip = "У этого типа нет ключевого ресурса"});
        a.markUi(mark + ".key");
      }
      if (wc.cost && ar) {
        const RectF cr = t.cell();
        partsCell("##cost", cr, costParts(w, rules::recruitCost(w, fid, l.row.id, 1), ar->keyRes), "Цена найма одного юнита");
        a.markUi(mark + ".cost", cr);
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
        ui::Disabled lock(special);
        double up = l.row.upkeep;
        if (ui::numberField("upkeep", up, {.min = 0, .max = 1e9, .step = 0.5, .digits = 3,
                                           .tooltip = special ? "Содержание особого отряда — из справочника" : ""}))
          setRowUpkeep(a, fid, l.row.id, fleet, up);
      }
      a.markUi(mark + ".upkeep");
      if (wc.upkeepTotal) {
        if (l.row.elemental) {
          const RectF cr = t.cell();
          partsCell("##esstotal", cr, essenceParts(w, l.essTotal), "Содержание эссенциями за ход", ui::Align::Right);
        } else {
          t.text(fmtMoney(l.upkeepTotal()));
        }
      }
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
      if (wc.race) t.cell();
      if (!fleet) t.cell();
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
      if (wc.upkeepTotal) t.text(fmtMoney(sumUpkeep));
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
        cellLines(cr, fmtMoney(l.upkeepTotal()), fmtMoney(l.row.upkeep) + " за ед.", ui::Align::Right);
      }
    }
    if (!lines.empty() && t.footer()) {
      t.text("Итого");
      RectF cr = t.cell();
      cellLines(cr, fmtCount(sumTotal), fmtCount(sumReserve), ui::Align::Right, ui::Ink::Normal, ui::Ink::Success);
      t.text(fmtMoney(sumUpkeep));
    }
  }
  // Выбор меняется только щелчком или стрелками: новая строка из меню ещё может отсутствовать в мире кадра.
  if (sel != sel0) st.selRow = sel >= 0 && sel < int(lines.size()) ? lines[size_t(sel)].row.id : 0;
  if (st.selRow && !unitRow(a.world(), fid, st.selRow, fleet)) st.selRow = 0;   // строку удалили
  return shown;
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

// Карточка выбранной строки: правка (узкая таблица), раса, цена найма, формирование и роспуск, размещение отрядов.
bool rowCard(App& a, Id fid, bool fleet, ForcesState& st, bool editors, Shown inTable) {
  const World& w = frameWorld(a);
  auto r = unitRow(w, fid, st.selRow, fleet);
  if (!r) return false;
  const ArmyRow* ar = fleet ? nullptr : armyRowOf(w, fid, r->id);
  auto c = rules::calc(w);
  const rules::RowCalc* rc = rowCalc(*c, fid, r->id, fleet);
  i64 field = rc ? rc->field : 0, reserve = rc ? rc->reserve : r->total, forming = rc ? rc->forming : 0;
  bool ro = a.readOnly();
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
    if (ar) armyPrice(a, fid, *ar, inTable.key, ro);
    if (editors) {
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
        if (ui::numberField("upkeep", up, {.min = 0, .max = 1e9, .step = 0.5, .digits = 3,
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
      if (ui::button("Справочник", {.variant = ui::Variant::Ghost, .icon = "book", .size = ui::Size::Small,
                                    .tooltip = "Тип, раса и цены особого отряда правятся в справочнике «Особые отряды»"}))
        a.openEditor("catalogs", 9);
      a.markUi("mil.detail.special.open");
    }
    if (special && !rules::hasSpecialAccess(w, fid, r->special)) {
      ui::label(specialNeed(w, r->special), {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
      a.markUi("mil.detail.special.need");
    }
    if (r->keyMissing) ui::label("Не указан ключевой ресурс — найм недоступен", {.font = ui::Font::Small, .ink = ui::Ink::Danger, .icon = "warning", .wrap = true});
    {
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
      ui::tag(fmtMoney(rc ? rc->upkeepTotal : 0) + " за ход", ui::Tone::Warning, "coins");
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
    a.markUi("mil.object." + std::to_string(ar->id));
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
          ui::tooltip(fleet ? "Общая численность строк правится напрямую, без формирования и роспуска: ресурсы не расходуются"
                            : "Общая численность строк правится напрямую, без формирования и роспуска: население, ресурсы и эссенции "
                              "не расходуются");
          a.markUi(fleet ? "mil.fleet.editReserve" : "mil.army.editReserve");
        }
        const Shown shown = forcesTable(a, fid, fleet, st, wide);
        if (st.selRow) {
          if (rowCard(a, fid, fleet, st, !wide, shown)) {
            if (st.shownRow != st.selRow) ui::scrollToItem();   // новая карточка — в видимую часть
            st.shownRow = st.selRow;
          }
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

TabReg armyTab({"faction.army", "army", "Войска", 50, SelType::Faction, nullptr, drawArmyTab, nullptr, true});
TabReg fleetTab({"faction.fleet", "fleet", "Флот", 52, SelType::Faction, nullptr, drawFleetTab, nullptr, true});
EditorReg editorReg({"military", "Войска и флот", drawEditor, "army"});

}  // namespace
}  // namespace rg::app::mil
