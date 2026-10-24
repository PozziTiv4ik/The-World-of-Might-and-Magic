// Regnum — войска и флот фракции: подписи строк, правка строк таблицы (название, тип, раса, ключевой ресурс,
// содержание, численность), добавление строк и особых отрядов. Роли (ТЗ «Доработки №4», п.5): новая строка, особый
// отряд и ключевой ресурс юнита (без него найм недоступен) — игровые действия за фракцию (App::play); название, тип,
// раса, содержание и «Правка резерва» — правка мира (администратор).
#include "app/panels/military_internal.h"

namespace rg::app::mil::forces {

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

namespace {

const char* raceIcon(std::string_view race) {
  if (race == schema::kRaceUndead) return "skull";
  if (race == schema::kRaceDemonic) return "flame";
  if (race == schema::kRaceMechanical) return "hammer";
  if (race == schema::kRaceElemental) return "bolt";
  if (race == schema::kRaceLiving) return "heart";
  return "race";
}

}  // namespace

// Цвет значка ресурса или эссенции, различимый на тёмном фоне (чёрные драконы, обсидиан).
Color legible(Color c) { return w::legible(c, 0.12f); }

namespace {

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

}  // namespace

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

namespace {

Id addRow(App& a, Id fid, bool fleet, int type) {
  Id id = 0;
  if (!w::checkPlay(a, fid)) return 0;
  a.play(fleet ? "Добавить судно" : "Добавить отряд", [&](Tx& tx) {
    id = fleet ? rules::addFleetRow(tx, fid, ShipType(type)) : rules::addArmyRow(tx, fid, UnitType(type));
  });
  return id;
}

}  // namespace

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
        if (w::checkPlay(a, fid) && a.play("Добавить особый отряд", [&](Tx& tx) { nid = rules::addSpecialRow(tx, fid, sid); }) && nid) st.selRow = nid;
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
  const bool locked = schema::isElemental(r->type) || r->special || r->merc;   // наёмники — всегда «Наемники»
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
  // Особый отряд: ключевой ресурс — его собственный из справочника (любой ресурс; «Шантирийские запчасти» големов).
  const bool missing = r.special ? r.keyRes && !w.resource(r.keyRes) : !rules::keyAllowed(w, r.type, r.keyRes);
  const std::string rule = r.special ? std::string("Ключевой ресурс особого отряда — из справочника") : keyRuleText(w, r.type);
  const std::string tip = missing ? "Не указан ключевой ресурс — найм недоступен\n" + rule : rule;
  const std::vector<Id> keys = r.special ? (r.keyRes ? std::vector<Id>{r.keyRes} : std::vector<Id>{}) : rules::keyResources(w, r.type);
  const bool changed = w::resourceFrom(id, v, keys, "Не выбран", disabled || r.special, tip);
  if (missing) ui::draw::rectStroke(ui::lastItem().rect.expand(1), ui::theme().danger, 8, 1.5f);
  if (changed && v != r.keyRes) {
    const Id row = r.id;
    const double per = r.keyPer;
    a.play("Ключевой ресурс", [&](Tx& tx) { rules::setRowKey(tx, fid, row, v, v ? per : 1); });
  }
}

// Ключевой ресурс судна (ТЗ «Доработки №3», п.8): морское чудовище — ресурс подгруппы «Морские чудовища», 1 на судно;
// не задан — красная обводка (найм заблокирован). У прочих судов ключевого ресурса нет.
void fleetKeyCombo(App& a, std::string_view id, Id fid, const FleetRow& r, bool disabled) {
  const World& w = frameWorld(a);
  const std::vector<Id> allowed = rules::shipKeyResources(w, r.type);
  Id v = r.keyRes;
  const bool missing = std::find(allowed.begin(), allowed.end(), r.keyRes) == allowed.end();
  const std::string rule = "Из подгруппы «Морские чудовища», 1 на судно";
  const std::string tip = missing ? "Не указан ключевой ресурс — найм недоступен\n" + rule : rule;
  const bool changed = w::resourceFrom(id, v, allowed, "Не выбран", disabled, tip);
  if (missing) ui::draw::rectStroke(ui::lastItem().rect.expand(1), ui::theme().danger, 8, 1.5f);
  if (changed && v != r.keyRes) {
    const Id row = r.id;
    a.play("Ключевой ресурс судна", [&](Tx& tx) { rules::setFleetRowKey(tx, fid, row, v); });
  }
}

const FleetRow* fleetRowOf(const World& w, Id fid, Id row) {
  const Faction* f = w.faction(fid);
  return f ? f->fleetRow(row) : nullptr;
}

}  // namespace rg::app::mil::forces
