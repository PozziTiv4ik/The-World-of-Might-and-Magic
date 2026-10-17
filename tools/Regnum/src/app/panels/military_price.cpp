// Regnum — войска и флот фракции: цена найма и содержание юнита, формирование и роспуск, ресурсы и эссенции на юнит.
// Роли (ТЗ «Доработки №4», п.5): формирование, роспуск и отмена формирования — игровые действия за фракцию
// (App::play); ресурсы, эссенции и запчасти на юнит — правка мира (администратор).
#include "app/panels/military_internal.h"

namespace rg::app::mil::forces {

// ---------------------------------------------------------------- цена и содержание
// Часть цены или содержания: значок и цвет (ресурс, эссенция, население), название, количество.
// Цена найма: население, ключевой ресурс строки (первым), прочие ресурсы (трупы, энергия, дополнительные), эссенции.
std::vector<Part> costParts(const World& w, const rules::RecruitCost& c, Id key) {
  std::vector<Part> out;
  if (c.people > 0) out.push_back(Part{"population", Color(0, 0, 0, 0), "Население", double(c.people), Part::People, 0});
  auto res = [&](Id r, double v) { out.push_back(Part{w::resourceIcon(w, r), legible(w::resourceColor(w, r)), w::resourceName(w, r), v, Part::Res, r}); };
  if (auto it = c.res.find(key); key && it != c.res.end()) res(key, it->second);
  for (auto& [r, v] : c.res)
    if (r != key) res(r, v);
  for (auto& [e, v] : c.ess) out.push_back(Part{"essence", legible(w::essenceColor(w, e)), w::essenceName(w, e), v, Part::Ess, e});
  return out;
}

std::vector<Part> essenceParts(const World& w, const std::map<Id, double>& m) {
  std::vector<Part> out;
  for (auto& [e, v] : m)
    if (v > 0) out.push_back(Part{"essence", legible(w::essenceColor(w, e)), w::essenceName(w, e), v, Part::Ess, e});
  return out;
}

// Части в ячейке таблицы: значок и число подряд; не поместилось — «+N»; полный список — в подсказке (без перехвата
// щелчка по строке).
void partsCell(std::string_view key, RectF r, const std::vector<Part>& parts, std::string_view title, ui::Align align) {
  const ui::Theme& t = ui::theme();
  if (parts.empty()) {
    ui::draw::text("—", r, ui::Font::Body, t.textMuted, align);
    return;
  }
  constexpr float is = 14, gap = 8;
  std::vector<float> widths;
  std::vector<std::string> nums;
  for (const Part& p : parts) {
    nums.push_back(p.kind == Part::Res && p.id == kGold ? fmtGold(p.v) : fmtMoney(p.v));
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
  for (const Part& p : parts) tip += "\n" + p.name + ": " + (p.kind == Part::Res && p.id == kGold ? fmtGold(p.v) : fmtMoney(p.v));
  ui::hoverTip(key, r, tip);
}

namespace {

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
    const bool goldPart = p.kind == Part::Res && p.id == kGold;
    ui::label(p.kind == Part::People ? fmtCount(i64(p.v)) : goldPart ? fmtGold(p.v) : fmtMoney(p.v),
              {.font = ui::Font::Strong, .ink = lack ? ui::Ink::Danger : ui::Ink::Normal, .align = ui::Align::Right,
               .tooltip = lack ? "Есть " + (p.kind == Part::People ? fmtCount(i64(have)) : goldPart ? fmtGold(std::max(0.0, have)) : fmtMoney(std::max(0.0, have)))
                               : std::string()});
  }
}

}  // namespace

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
  if (ui::button("Сформировать", {.variant = ui::Variant::Primary, .icon = "plus", .fill = true, .disabled = !cost.problems.empty() || !a.canPlay(fid)})) {
    const i64 n = st.count;
    const Id r = row.id;
    if (a.play(fleet ? "Сформировать корабли" : "Сформировать отряды", [&](Tx& tx) { rules::recruit(tx, fid, r, n); })) ui::closePopup();
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
  if (ui::button("Распустить", {.variant = ui::Variant::Danger, .icon = "disband", .fill = true, .disabled = reserve <= 0 || st.count <= 0 || !a.canPlay(fid)})) {
    const i64 n = st.count;
    const Id r = row.id;
    if (a.play(fleet ? "Распустить корабли" : "Распустить отряды", [&](Tx& tx) { rules::disbandReserve(tx, fid, r, n); })) ui::closePopup();
  }
  a.markUi("mil.disband.ok");
  ui::endPopup();
}

// Отменить формирование (возврат людей и ресурсов). Индекс сверяется со строкой и числом внутри транзакции.
void cancelForming(App& a, Id fid, int index, Id row, i64 count) {
  if (!w::checkPlay(a, fid)) return;
  a.play("Отменить формирование", [&](Tx& tx) {
    const Faction* f = tx.w().faction(fid);
    if (!f || index < 0 || index >= int(f->forming.size()) || f->forming[size_t(index)].row != row || f->forming[size_t(index)].count != count)
      fail("Формирование уже изменилось");
    rules::cancelFormation(tx, fid, index);
  });
}

namespace {

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
    const std::string name = res ? w::resourceName(w, id) : w::essenceName(w, id);
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

}  // namespace

// Цена и содержание строки армии в карточке: ключевой ресурс (если не в таблице; у механизмов — и сколько на юнит),
// дополнительные ресурсы и эссенции на юнит (у элементалей — только эссенции) и содержание элементалей эссенциями.
void armyPrice(App& a, Id fid, const ArmyRow& r, bool keyInTable) {
  const bool ro = !a.canPlay(fid);                // ключевой ресурс — игровое действие
  const bool locked = !a.canEdit() || r.special;  // ресурсы и эссенции на юнит — правка мира
  const bool elem = schema::isElemental(r.type);
  if (schema::needsKeyResource(r.type)) {
    // Военные механизмы: запчастей на юнит — не меньше 1 (у прочих типов — ровно 1).
    const bool machines = !schema::keyRule(r.type).fixedOne;
    auto perField = [&] {
      ui::Disabled dis(locked || !r.keyRes);   // запчастей на юнит — показатель строки
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

}  // namespace rg::app::mil::forces
