// Regnum — справочник «Особые отряды» (ТЗ «Ввод новых механик», п.4–5): отряды, уникальные для мира. Таблица:
// название, тип (значок и подпись), ключевой ресурс, цена кратко (ресурсы и эссенции на 1 юнит), содержание.
// Карточка правит всю запись через rules::setSpecial (правило само повторяет её в строках армий): название, тип
// (все типы войск), раса (расы для отрядов; у элементалей — только «Элементали»), ключевой ресурс (из подходящих
// типу; число на юнит — только у военных механизмов, не меньше 1), дополнительные ресурсы и эссенции на 1 юнит,
// содержание золотом (у элементалей — эссенциями), описание, «Где нанимается» (постройки доступа) и «В войсках».
#include <algorithm>

#include "app/editors/buildings.h"
#include "app/editors/catalogs_internal.h"
#include "gfx/icons.h"

namespace rg::app::cat {

using platform::Key;

namespace {

constexpr float kRowH = 44, kHeadH = 32;
enum Col : int { cTile, cName, cType, cKey, cPrice, cUpkeep, cActions };

const char* typeIcon(UnitType t) { return schema::unitType(t).icon; }
const char* typeName(UnitType t) { return schema::unitType(t).name; }

std::string resName(const World& w, Id r) {
  const CatalogItem* c = w.resource(r);
  return c ? orName(c->name) : std::string("Ресурс");
}
std::string essName(const World& w, Id e) {
  const CatalogItem* c = w.essence(e);
  return c ? orName(c->name) : std::string("Эссенция");
}

// Правка записи целиком: копия из мира, изменение, rules::setSpecial (проверки и строки армий — в правиле).
bool editSpecial(App& a, Id id, std::string_view label, const std::function<void(SpecialUnit&)>& fn, std::string coalesce = {}) {
  TxOptions opt;
  opt.coalesce = std::move(coalesce);
  return a.act(label, [&](Tx& tx) {
    const SpecialUnit* cur = tx.w().special(id);
    if (!cur) fail("Особый отряд не найден");
    SpecialUnit s = *cur;
    fn(s);
    rules::setSpecial(tx, s);
  }, opt);
}

Id addSpecialAct(App& a, State& st) {
  Id nid = 0;
  if (!a.act("Новый особый отряд", [&](Tx& tx) { nid = rules::addSpecial(tx, ""); }) || !nid) return 0;
  st.query.clear();
  st.sel[kSpecials] = nid;
  st.focusName = nid;
  return nid;
}

// Строки армий с особым отрядом: (фракция, строка).
std::vector<RowRef> rowsOf(const World& w, Id special) {
  std::vector<RowRef> out;
  w.factions.each([&](const Faction& f) {
    for (const ArmyRow& r : f.army)
      if (r.special == special) out.push_back(RowRef{f.id, r.id});
  });
  std::stable_sort(out.begin(), out.end(), [&](const RowRef& x, const RowRef& y) { return compareRu(w.factionName(x.faction), w.factionName(y.faction)) < 0; });
  return out;
}

void askRemoveSpecial(App& a, const World& w, Id id) {
  const SpecialUnit* s = w.special(id);
  if (!s) return;
  const size_t rows = rowsOf(w, id).size(), blds = rules::specialBuildings(w, id).size();
  std::string text = "Особый отряд «" + orName(s->name) + "» будет удалён из справочника.";
  if (rows) text += " " + nb(i64(rows), "строка войск станет обычной", "строки войск станут обычными", "строк войск станут обычными") + ".";
  if (blds) text += " " + nb(i64(blds), "постройка потеряет доступ", "постройки потеряют доступ", "построек потеряют доступ") + " к нему.";
  text += " Действие можно отменить Ctrl+Z.";
  a.confirm("Удалить особый отряд?", text, "Удалить", true, [id](App& x) {
    x.act("Удалить особый отряд", [&](Tx& tx) { rules::removeSpecial(tx, id); });
  });
}

// Цена на 1 юнит одной строкой значков: ключевой ресурс (у механизмов — числом), дополнительные ресурсы, эссенции.
void priceGlyphs(const World& w, const SpecialUnit& s) {
  ui::HStack hs(26, ui::Align::Left, 3);
  bool any = false;
  if (s.keyRes && !schema::isElemental(s.type)) {
    amountGlyph(w::resourceIcon(w, s.keyRes), w::resourceColor(w, s.keyRes), s.keyPer, resName(w, s.keyRes) + " (ключевой) на 1 юнит");
    any = true;
  }
  for (auto& [r, v] : s.extra) {
    amountGlyph(w::resourceIcon(w, r), w::resourceColor(w, r), v, resName(w, r) + " на 1 юнит");
    any = true;
  }
  for (auto& [e, v] : s.essence) {
    amountGlyph("essence", w::essenceColor(w, e), v, essName(w, e) + " на 1 юнит");
    any = true;
  }
  if (!any) ui::label("—", {.ink = ui::Ink::Muted});
}

void upkeepGlyphs(const World& w, const SpecialUnit& s) {
  ui::HStack hs(26, ui::Align::Left, 3);
  if (schema::isElemental(s.type)) {
    for (auto& [e, v] : s.essUpkeep) amountGlyph("essence", w::essenceColor(w, e), v, essName(w, e) + " на 1 юнит в ход");
    if (s.essUpkeep.empty()) ui::label("—", {.ink = ui::Ink::Muted});
    return;
  }
  if (s.upkeep > 0) amountGlyph("coins", w::resourceColor(w, kGold), s.upkeep, "Золото на 1 юнит в ход", true);
  else ui::label("—", {.ink = ui::Ink::Muted});
}

struct SRow {
  const SpecialUnit* s;
  i64 units = 0;   // во всех строках армий
};

void specialTable(App& a, State& st, const World& w, const std::vector<SRow>& rows, RectF T) {
  const bool ro = a.readOnly();
  const ui::Theme& th = ui::theme();
  Id& sel = st.sel[kSpecials];
  int selIdx = -1;
  for (size_t i = 0; i < rows.size(); i++)
    if (rows[i].s->id == sel) selIdx = int(i);
  const int selBefore = selIdx;
  ui::Area ta(T, 0);
  ui::Scroll sc("tblscroll", T.h);
  const ui::Column cols[] = {
      {"", nullptr, ui::px(48)},
      {"Название", nullptr, ui::fr(1.3f, 140), ui::Align::Left, true},
      {"Тип", nullptr, ui::fr(1.1f, 120), ui::Align::Left, true},
      {"Ключевой ресурс", nullptr, ui::fr(1.2f, 125), ui::Align::Left, true},
      {"Цена на 1 юнит", nullptr, ui::fr(1.3f, 120), ui::Align::Left, false, "Ключевой и дополнительные ресурсы, эссенции на 1 юнит"},
      {"Содержание", nullptr, ui::fr(1.f, 100), ui::Align::Left, true, "Содержание 1 юнита в ход"},
      {"", nullptr, ui::px(40)},
  };
  ui::Table t("tbl", std::span<const ui::Column>(cols), int(rows.size()),
              {.rowHeight = kRowH, .selected = &selIdx, .emptyIcon = "special-unit",
               .emptyText = st.query.empty() ? "Особых отрядов пока нет" : "Ничего не найдено"});
  auto cmp = [&](int x, int y, int col) {
    const SpecialUnit& A = *rows[size_t(x)].s;
    const SpecialUnit& B = *rows[size_t(y)].s;
    switch (col) {
      case cName: return compareRu(A.name, B.name);
      case cType: return int(A.type) - int(B.type);
      case cKey: return compareRu(A.keyRes ? resName(w, A.keyRes) : std::string(), B.keyRes ? resName(w, B.keyRes) : std::string());
      case cUpkeep: return A.upkeep < B.upkeep ? -1 : A.upkeep > B.upkeep ? 1 : 0;
      default: return 0;
    }
  };
  t.sort(cmp);
  if (st.focusName)
    for (size_t i = 0; i < rows.size(); i++)
      if (rows[i].s->id == st.focusName) revealRow(sc, T.h, kHeadH, kRowH, displayIndex(int(i), int(rows.size()), t.sortColumn(), t.sortDescending(), cmp));
  for (int i : t) {
    const SpecialUnit& s = *rows[size_t(i)].s;
    const Id id = s.id;
    const RectF rr = t.rowRect();
    ui::IdScope scope{i64(id)};
    {
      const RectF cr = t.cell();
      tile(RectF{cr.x + 1, rr.cy() - 14, 28, 28}, typeIcon(s.type), th.accent, 8);
    }
    {
      const RectF cr = t.cell();
      ui::draw::text(orName(s.name), RectF{cr.x, rr.y + 5, cr.w, 18}, ui::Font::Strong, th.text);
      const std::string sub = rows[size_t(i)].units ? "в войсках: " + fmtInt(rows[size_t(i)].units) : std::string("нет в войсках");
      ui::draw::text(sub, RectF{cr.x, rr.y + 23, cr.w, 16}, ui::Font::Small, th.textMuted);
      if (id == sel) a.markUi("catalogs.selected.name", RectF{cr.x, rr.y, cr.w, rr.h});
    }
    {
      const RectF cr = t.cell();
      ui::draw::icon(typeIcon(s.type), RectF{cr.x, rr.cy() - 8, 16, 16}, th.textDim);
      ui::draw::text(typeName(s.type), RectF{cr.x + 22, rr.y, cr.w - 22, rr.h}, ui::Font::Body, th.text);
    }
    {
      const RectF cr = t.cell();
      if (s.keyRes && w.resource(s.keyRes)) {
        ui::draw::icon(w::resourceIcon(w, s.keyRes), RectF{cr.x, rr.cy() - 8, 16, 16}, legible(w::resourceColor(w, s.keyRes)));
        std::string label = resName(w, s.keyRes);
        if (s.keyPer != 1) label += " × " + fmtNum(s.keyPer, 3);
        ui::draw::text(label, RectF{cr.x + 22, rr.y, cr.w - 22, rr.h}, ui::Font::Body, th.text);
      } else {
        ui::draw::text(schema::needsKeyResource(s.type) ? "не задан" : "—", cr, ui::Font::Body, schema::needsKeyResource(s.type) ? th.warning : th.textMuted);
      }
    }
    t.cell();
    priceGlyphs(w, s);
    t.cell();
    upkeepGlyphs(w, s);
    t.cell();
    if (ui::iconButton("trash", "Удалить особый отряд", {.size = ui::Size::Small, .disabled = ro, .tone = ui::Tone::Danger})) askRemoveSpecial(a, w, id);
    a.markUi("catalogs.special." + std::to_string(id), rr);
  }
  if (selIdx != selBefore && selIdx >= 0 && selIdx < int(rows.size())) sel = rows[size_t(selIdx)].s->id;
}

// ---------------------------------------------------------------- карточка
// Список «ресурс/эссенция → на 1 юнит»: строки с числом и крестиком, ниже — выбор для добавления.
struct AmountList {
  const char* mark;          // метка для тестов: «<mark>.<id>», «<mark>.add»
  bool essences;             // эссенции (иначе ресурсы)
  const char* unitLabel;     // единица в поле числа
};

void amountList(App& a, const World& w, const SpecialUnit& s, const std::map<Id, double>& cur, const AmountList& o,
                const std::function<void(SpecialUnit&, Id, double)>& set, bool ro) {
  ui::IdScope listScope(o.mark);   // списки цены и содержания — разные области ID (одна эссенция бывает в обоих)
  const Id sid = s.id;
  for (auto& [id, v] : cur) {
    ui::IdScope scope{i64(id)};
    ui::Row r({ui::fr(1, 120), ui::px(132), ui::px(30)}, 30, 6);
    {
      ui::HStack hs(30, ui::Align::Left, 6);
      if (o.essences) {
        const RectF d = ui::next(16, 30);
        ui::draw::icon("essence", RectF{d.x, d.cy() - 8, 16, 16}, legible(w::essenceColor(w, id)));
        ui::label(essName(w, id));
      } else {
        ui::iconColored(w::resourceIcon(w, id), legible(w::resourceColor(w, id)), 16);
        ui::label(resName(w, id), {.tooltip = w.resource(id) && w.resource(id)->group ? rules::groupPath(w, w.resource(id)->group) : std::string()});
      }
    }
    double nv = v;
    if (ui::numberField("v", nv, {.min = 0, .max = 1e12, .step = 1, .digits = 3, .unit = o.unitLabel, .disabled = ro,
                                  .tooltip = "На 1 юнит; 0 — убрать"})) {
      const Id key = id;
      const double val = std::max(0.0, nv);
      editSpecial(a, sid, "Цена особого отряда", [&](SpecialUnit& x) { set(x, key, val); },
                  std::string("special:") + o.mark + ":" + std::to_string(sid) + ":" + std::to_string(key));
    }
    a.markUi(std::string(o.mark) + "." + std::to_string(id));
    if (ui::iconButton("close", "Убрать", {.size = ui::Size::Small, .disabled = ro})) {
      const Id key = id;
      editSpecial(a, sid, "Цена особого отряда", [&](SpecialUnit& x) { set(x, key, 0); });
    }
  }
  if (ro) return;
  Id pick = 0;
  bool picked = false;
  {
    ui::IdScope scope("add");
    if (o.essences) picked = w::essencePicker("pick", pick, "Добавить эссенцию");
    else picked = w::resourceByGroup("pick", pick);
  }
  a.markUi(std::string(o.mark) + ".add");
  if (picked && pick && !cur.count(pick)) {
    const Id key = pick;
    editSpecial(a, sid, o.essences ? "Эссенция особого отряда" : "Ресурс особого отряда", [&](SpecialUnit& x) { set(x, key, 1); });
  }
}

void specialCard(App& a, State& st, const World& w, const SpecialUnit& s) {
  const bool ro = a.readOnly();
  const Id id = s.id;
  const ui::Theme& th = ui::theme();
  const bool elem = schema::isElemental(s.type);
  ui::Scroll sc("card");
  cardHead(typeIcon(s.type), th.accent, std::string("Особый отряд · ") + utf8::lower(typeName(s.type)), orName(s.name));
  const std::vector<Id> blds = rules::specialBuildings(w, id);
  const std::vector<RowRef> rows = rowsOf(w, id);
  i64 units = 0;
  for (const RowRef& r : rows)
    if (const Faction* f = w.faction(r.faction))
      if (const ArmyRow* row = f->armyRow(r.row)) units += row->total;
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(fmtInt(i64(blds.size())), "Постройки", {.icon = "building", .tone = blds.empty() ? ui::Tone::Warning : ui::Tone::Accent,
                                                    .tooltip = "Постройки доступа: где нанимается"});
    ui::stat(fmtInt(units), "В войсках", {.icon = "army", .tone = ui::Tone::Info});
  }
  // Основное: название, тип, раса.
  {
    ui::prop("Название", "edit", 0.37f);
    std::string name = s.name;
    if (st.focusName == id) {
      ui::setKeyboardFocus(ui::id("name"));
      st.focusName = 0;
    }
    if (ui::textField("name", name, {.placeholder = "Название отряда", .maxLength = 80, .readOnly = ro, .selectAllOnFocus = true}) && trim(name) != s.name) {
      const std::string n = trim(name);
      editSpecial(a, id, "Переименовать особый отряд", [&](SpecialUnit& x) { x.name = n; });
    }
    a.markUi("catalogs.special.name");
  }
  {
    ui::prop("Тип", typeIcon(s.type), 0.37f);
    std::vector<ui::Option> opts;
    for (int k = 0; k < int(UnitType::Count); k++) opts.push_back(ui::Option{schema::kUnitTypes[k].name, schema::kUnitTypes[k].icon});
    int idx = int(s.type);
    if (ui::combo("type", idx, std::span<const ui::Option>(opts), {.disabled = ro, .maxVisible = 14, .tooltip = "Тип войск"}) && idx >= 0 &&
        idx < int(UnitType::Count) && UnitType(idx) != s.type) {
      const UnitType nt = UnitType(idx);
      editSpecial(a, id, "Тип особого отряда", [&](SpecialUnit& x) {
        if (schema::isElemental(x.type) && !schema::isElemental(nt)) x.race.clear();   // раса «Элементали» — только у элементалей
        x.type = nt;
      });
    }
    a.markUi("catalogs.special.type");
  }
  {
    ui::prop("Раса", "race", 0.37f);
    if (elem) {
      int only = 0;
      ui::combo("race", only, {ui::Option{schema::kRaceElemental}}, {.disabled = true, .tooltip = "У элементалей — только «Элементали»"});
    } else {
      const std::vector<std::string> races = rules::unitRaces(w);
      std::vector<std::string> list = races;
      if (!s.race.empty() && std::find(list.begin(), list.end(), s.race) == list.end()) list.push_back(s.race);
      std::vector<ui::Option> opts;
      for (const std::string& r : list) opts.push_back(ui::Option{r});
      int idx = -1;
      for (size_t k = 0; k < list.size(); k++)
        if (list[k] == s.race) idx = int(k);
      if (ui::combo("race", idx, std::span<const ui::Option>(opts),
                    {.placeholder = "По виду государства", .noneLabel = "По виду государства", .disabled = ro, .tooltip = "Раса отряда"})) {
        const std::string nr = idx >= 0 && idx < int(list.size()) ? list[size_t(idx)] : std::string();
        if (nr != s.race) editSpecial(a, id, "Раса особого отряда", [&](SpecialUnit& x) { x.race = nr; });
      }
    }
    a.markUi("catalogs.special.race");
  }
  // Ключевой ресурс: из подходящих типу; у механизмов — число на юнит (не меньше 1).
  {
    const schema::KeyRule kr = schema::keyRule(s.type);
    ui::prop("Ключевой ресурс", "resource", 0.4f);
    if (!schema::needsKeyResource(s.type)) {
      ui::label("Не нужен для этого типа", {.ink = ui::Ink::Muted});
    } else {
      ui::Row r({ui::fr(1), ui::px(30)}, 30, 6);
      Id key = s.keyRes;
      if (w::resourceFrom("key", key, rules::keyResources(w, s.type), "Не задан", ro, "Ключевой ресурс отряда") && key != s.keyRes)
        editSpecial(a, id, "Ключевой ресурс особого отряда", [&](SpecialUnit& x) { x.keyRes = key; });
      a.markUi("catalogs.special.key");
      if (ui::iconButton("close", "Снять ключевой ресурс", {.size = ui::Size::Small, .disabled = ro || !s.keyRes}))
        editSpecial(a, id, "Ключевой ресурс особого отряда", [&](SpecialUnit& x) {
          x.keyRes = 0;
          x.keyPer = 1;
        });
    }
    if (schema::needsKeyResource(s.type) && !kr.fixedOne) {
      ui::prop("На 1 юнит", "hash", 0.37f);
      double per = s.keyPer;
      if (ui::numberField("keyper", per, {.min = 1, .max = 1e12, .step = 1, .digits = 3, .disabled = ro || !s.keyRes,
                                          .tooltip = "Ключевого ресурса на 1 юнит — не меньше 1"}))
        editSpecial(a, id, "Ключевой ресурс на юнит", [&](SpecialUnit& x) { x.keyPer = std::max(1.0, per); }, "special:keyper:" + std::to_string(id));
      a.markUi("catalogs.special.keyPer");
    }
  }
  // Цена на 1 юнит: дополнительные ресурсы (у элементалей — нет) и эссенции.
  if (!elem) {
    ui::Section sec("Дополнительные ресурсы на 1 юнит", "resource", {.badge = std::to_string(s.extra.size())});
    a.markUi("catalogs.special.extra");
    if (sec)
      amountList(a, w, s, s.extra, {"catalogs.special.extra", false, nullptr}, [](SpecialUnit& x, Id r, double v) {
        if (v > 0) x.extra[r] = v;
        else x.extra.erase(r);
      }, ro);
  }
  {
    ui::Section sec("Эссенции на 1 юнит", "essence", {.badge = std::to_string(s.essence.size())});
    a.markUi("catalogs.special.ess");
    if (sec)
      amountList(a, w, s, s.essence, {"catalogs.special.ess", true, nullptr}, [](SpecialUnit& x, Id e, double v) {
        if (v > 0) x.essence[e] = v;
        else x.essence.erase(e);
      }, ro);
  }
  // Содержание: золото за юнит; у элементалей — эссенции за юнит в ход.
  if (elem) {
    ui::Section sec("Содержание эссенциями", "essence", {.badge = std::to_string(s.essUpkeep.size())});
    a.markUi("catalogs.special.essUpkeep");
    if (sec)
      amountList(a, w, s, s.essUpkeep, {"catalogs.special.essUpkeep", true, "в ход"}, [](SpecialUnit& x, Id e, double v) {
        if (v > 0) x.essUpkeep[e] = v;
        else x.essUpkeep.erase(e);
      }, ro);
  } else {
    ui::prop("Содержание", "coins", 0.37f);
    double up = s.upkeep;
    if (ui::numberField("upkeep", up, {.min = 0, .max = 1e12, .step = 1, .digits = 3, .unit = "тыс. в ход", .icon = "coins", .disabled = ro,
                                       .tooltip = "Золото на 1 юнит в ход"}))
      editSpecial(a, id, "Содержание особого отряда", [&](SpecialUnit& x) { x.upkeep = std::max(0.0, up); }, "special:upkeep:" + std::to_string(id));
    a.markUi("catalogs.special.upkeep");
  }
  {
    ui::caption("Описание");
    std::string desc = s.desc;
    if (ui::textArea("desc", desc, 72, {.placeholder = "Описание", .readOnly = ro}) && desc != s.desc)
      editSpecial(a, id, "Описание особого отряда", [&](SpecialUnit& x) { x.desc = desc; });
    a.markUi("catalogs.special.desc");
  }
  // Где нанимается: постройки доступа (щелчок — дерево построек).
  {
    ui::Section sec("Где нанимается", "building", {.badge = std::to_string(blds.size())});
    a.markUi("catalogs.special.buildings");
    if (sec) {
      if (blds.empty()) {
        ui::label("Нет построек доступа", {.ink = ui::Ink::Warning, .icon = "warning"});
        if (ui::link("Дерево построек", "building")) detail::later(a, [](App& x) { openBuildingTree(x, 0); });
      } else {
        ChipFlow cf;
        for (Id bid : blds) {
          const Building* b = w.building(bid);
          if (!b) continue;
          ui::IdScope scope{i64(bid) + 0x7600000LL};
          ui::ChipOpt co;
          co.icon = !b->icon.empty() && gfx::hasIcon(b->icon) ? b->icon.c_str() : "building";
          co.clickable = true;
          co.tooltip = b->owner ? "Уникальная постройка — открыть дерево построек" : "Общее дерево построек — открыть";
          std::string label = orName(b->name);
          if (b->owner) label += " · " + w.factionName(b->owner);
          if (edkit::chip(label, co) == ui::ChipAction::Click) openBuilding(a, w, bid);
        }
      }
    }
  }
  // В войсках: строки армий государств с этим отрядом.
  {
    ui::Section sec("В войсках", "army", {.badge = std::to_string(rows.size())});
    a.markUi("catalogs.special.rows");
    if (sec) {
      if (rows.empty()) {
        ui::label("Ни одно государство не нанимает", {.ink = ui::Ink::Muted});
      } else {
        ChipFlow cf;
        for (const RowRef& r : rows) {
          const Faction* f = w.faction(r.faction);
          const ArmyRow* row = f ? f->armyRow(r.row) : nullptr;
          if (!f || !row) continue;
          ui::IdScope scope{i64(r.row) + 0x7700000LL};
          ui::ChipOpt co;
          co.color = f->color;
          co.clickable = true;
          co.tooltip = "Открыть войска";
          if (edkit::chip(orName(f->name) + " · " + fmtInt(row->total), co) == ui::ChipAction::Click) openArmy(a, r.faction);
        }
      }
    }
  }
  ui::spacer(4);
  {
    ui::Disabled dis(ro);
    if (ui::button("Удалить: " + orName(s.name, "особый отряд"), {.variant = ui::Variant::Danger, .icon = "trash", .fill = true}))
      askRemoveSpecial(a, w, id);
    a.markUi("catalogs.delete");
  }
}

}  // namespace

void drawSpecials(App& a, State& st) {
  const bool ro = a.readOnly();
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    searchBox(a, st, 300, "Поиск особых отрядов");
    ui::flex();
    if (ui::button("Новый особый отряд", {.variant = ui::Variant::Primary, .icon = "plus", .disabled = ro, .shortcut = {Key::Insert, 0}}))
      addSpecialAct(a, st);
    a.markUi("catalogs.add");
  }
  ui::spacer(2);
  const World w = a.world();   // снимок кадра (после кнопок: новая запись уже в нём); правка посреди кадра заменяет мир
  std::unordered_map<Id, i64> units;
  w.factions.each([&](const Faction& f) {
    for (const ArmyRow& r : f.army)
      if (r.special) units[r.special] += r.total;
  });
  std::vector<SRow> rows;
  for (const SpecialUnit& s : w.catalogs->specials) {
    if (!st.query.empty() && !utf8::matches(s.name, st.query) && !utf8::matches(typeName(s.type), st.query)) continue;
    rows.push_back(SRow{&s, units.count(s.id) ? units[s.id] : 0});
  }
  Id& sel = st.sel[kSpecials];
  if (std::none_of(rows.begin(), rows.end(), [&](const SRow& x) { return x.s->id == sel; })) sel = rows.empty() ? 0 : rows.front().s->id;
  const Split sp = split(ui::avail());
  specialTable(a, st, w, rows, sp.table);
  a.markUi("catalogs.table", sp.table);
  if (!sp.card.empty()) {
    ui::Area ca(sp.card, 0);
    const SpecialUnit* cur = w.special(sel);
    if (cur && sel) {
      specialCard(a, st, w, *cur);
    } else {
      ui::spacer(std::max(0.f, sp.card.h * 0.3f));
      if (st.query.empty()) {
        if (ui::emptyState("special-unit", "Особых отрядов пока нет", ro ? std::string_view() : std::string_view("Новый особый отряд"), "plus"))
          addSpecialAct(a, st);
      } else {
        ui::emptyState("special-unit", "Ничего не найдено");
      }
    }
  }
  if (!ro && sel && ui::shortcut({Key::Delete, 0})) askRemoveSpecial(a, w, sel);
}

}  // namespace rg::app::cat
