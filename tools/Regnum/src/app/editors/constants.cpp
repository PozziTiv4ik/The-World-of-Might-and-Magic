// Regnum — глобальные константы (ТЗ «Общие доработки», п.1–2): вкладка левой ленты и окно (EditorReg «constants»).
// Встроенные (schema::builtinConstants) видны всегда: пока их не правили, показывается значение по умолчанию
// (rules::constantOf), первая правка создаёт запись мира (rules::ensureConstant). Свои константы трёх типов — число,
// список ресурсов с количествами, список значений (rules::addConstant, rules::removeConstant). Базовые расы отрядов
// не удаляются и не переименовываются; переименование и удаление своей расы правит строки войск с этой расой.
#include <algorithm>

#include "app/app_internal.h"
#include "app/dialogs/turn_ui.h"
#include "app/widgets.h"

namespace rg::app {

namespace edkit {   // modifiers.cpp
void iconTile(const std::string& icon, Color tint, float size, const char* fallback);
bool entityRow(std::string_view title, std::string_view subtitle, const char* icon, Color tint, std::string_view hint, bool selected,
               bool warn, std::string_view tip);
}  // namespace edkit

namespace {

using platform::Key;

// ---------------------------------------------------------------- состояние (сеанс мира)
struct ConstState {
  std::string sel;                           // выбранная константа окна
  std::string focusName;                     // константа, чьё название получит фокус (после создания)
  std::map<std::string, std::string> add;    // ввод нового значения списка по ключу константы
};
ConstState& state() {
  static ConstState s;
  static u64 tag = 0;
  if (hasApp()) {
    u64 t = turnui::sessionTag(app());   // другой мир — чистое состояние
    if (t != tag) {
      tag = t;
      s = ConstState{};
    }
  }
  return s;
}

// ---------------------------------------------------------------- данные
bool builtinKey(std::string_view key) {
  for (const Constant& c : schema::builtinConstants())
    if (c.key == key) return true;
  return false;
}

bool baseRace(std::string_view v) {
  for (const char* b : {schema::kRaceLiving, schema::kRaceDemonic, schema::kRaceUndead, schema::kRaceMechanical, schema::kRaceElemental})
    if (v == b) return true;
  return false;
}

// Все константы на кадр (копии: правка посреди кадра заменяет мир): встроенные в порядке шаблона, затем свои.
std::vector<Constant> allConstants(const World& w) {
  std::vector<Constant> out;
  for (const Constant& b : schema::builtinConstants()) {
    Constant c = rules::constantOf(w, b.key);
    c.builtin = true;
    if (c.key == schema::cst::UnitRaces) c.values = rules::unitRaces(w);   // базовые расы — всегда
    out.push_back(std::move(c));
  }
  for (const Constant& c : w.constants->list)
    if (!builtinKey(c.key)) out.push_back(c);
  return out;
}

const char* typeIcon(ConstType t) { return schema::kConstTypes[int(t) >= 0 && t < ConstType::Count ? int(t) : 0].icon; }
const char* typeName(ConstType t) { return schema::kConstTypes[int(t) >= 0 && t < ConstType::Count ? int(t) : 0].name; }

// Значок числа встроенной константы: золото, трупы, демоническая энергия.
const char* numberIcon(std::string_view key) {
  if (key == schema::cst::ColonizationCost) return "coins";
  if (key == schema::cst::CorpsesPerUnit) return "skull";
  if (key == schema::cst::EnergyPerUnit) return "flame";
  return "hash";
}

std::string resName(const World& w, Id res) {
  const CatalogItem* c = w.resource(res);
  return c && !c->name.empty() ? c->name : std::string(res == kGold ? "Золото" : "Ресурс");
}

std::string orName(const std::string& s) { return s.empty() ? std::string("Без названия") : s; }

std::string summary(const World& w, const Constant& c) {
  switch (c.type) {
    case ConstType::Resources: {
      if (c.res.empty()) return "Нет ресурсов";
      std::vector<std::string> parts;
      for (auto& [rid, v] : c.res) {
        if (parts.size() == 3) {
          parts.push_back("…");
          break;
        }
        parts.push_back(resName(w, rid) + " " + fmtNum(v, 3));
      }
      return join(parts, " · ");
    }
    case ConstType::Values: {
      if (c.values.empty()) return "Пусто";
      std::vector<std::string> parts(c.values.begin(), c.values.begin() + long(std::min<size_t>(c.values.size(), 4)));
      return join(parts, ", ") + (c.values.size() > 4 ? "…" : "");
    }
    default: return fmtNum(c.num, 3);
  }
}

// ---------------------------------------------------------------- действия
// Правка константы: встроенная создаётся в мире по шаблону при первой правке.
bool editConst(App& a, const std::string& key, std::string_view label, const std::function<void(Tx&, Constant&)>& fn,
               const TxOptions& opt = {}) {
  return a.act(label, [&](Tx& tx) { fn(tx, rules::ensureConstant(tx, key)); }, opt);
}

// Раса отряда переименована (to) или удалена (пусто — раса по виду государства): строки войск всех фракций.
void retargetRace(Tx& tx, const std::string& from, const std::string& to) {
  for (Id fid : tx.w().factions.ids()) {
    const Faction* f = tx.w().faction(fid);
    if (!f || std::none_of(f->army.begin(), f->army.end(), [&](const ArmyRow& r) { return r.race == from; })) continue;
    for (ArmyRow& r : tx.faction(fid).army)
      if (r.race == from) r.race = to;
  }
}

void createConstant(App& a, ConstType type) {
  std::string key;
  if (!a.act("Новая константа", [&](Tx& tx) { key = rules::addConstant(tx, type, ""); })) return;
  ConstState& st = state();
  st.sel = key;
  st.focusName = key;
}

void askRemove(App& a, const Constant& c) {
  std::string key = c.key;
  a.confirm("Удалить константу?", "«" + orName(c.name) + "» будет удалена. Действие можно отменить Ctrl+Z.", "Удалить", true, [key](App& x) {
    if (x.act("Удалить константу", [&](Tx& tx) { rules::removeConstant(tx, key); }) && state().sel == key) state().sel.clear();
  });
}

// «+»: выбор типа новой константы.
void newButton(App& a, const char* mark) {
  bool ro = a.readOnly();
  if (ui::iconButton("plus", "Новая константа", {.variant = ui::Variant::Secondary, .disabled = ro})) ui::openPopup("new");
  a.markUi(mark);
  if (ui::beginMenu("new")) {
    for (int t = 0; t < int(ConstType::Count); t++) {
      ui::IdScope s{t};
      if (ui::menuItem(schema::kConstTypes[t].name, {.icon = schema::kConstTypes[t].icon})) createConstant(a, ConstType(t));
      a.markUi(std::string(mark) + "." + schema::kConstTypes[t].id);
    }
    ui::endMenu();
  }
}

// ---------------------------------------------------------------- значение
void numberValue(App& a, const Constant& c, const std::string& mark) {
  double v = c.num;
  ui::NumberOpt o;
  o.min = c.builtin ? 0 : -1e12;
  o.max = 1e12;
  o.step = 1;
  o.digits = 3;
  o.icon = numberIcon(c.key);
  o.disabled = a.readOnly();
  o.tooltip = c.key == schema::cst::ColonizationCost ? std::string_view("Золото") : std::string_view();
  if (ui::numberField("num", v, o)) {
    std::string key = c.key;
    editConst(a, key, "Значение константы", [&](Tx&, Constant& k) { k.num = v; }, {.coalesce = "const:" + key});
  }
  a.markUi(mark + ".num");
}

void resourcesValue(App& a, const Constant& c, const std::string& mark) {
  const World& w = a.world();
  bool ro = a.readOnly();
  const std::string key = c.key;
  if (c.res.empty()) ui::label("Нет ресурсов", {.ink = ui::Ink::Muted});
  for (auto& [rid, amount] : c.res) {
    ui::IdScope s{i64(rid)};
    const Id res = rid;
    const std::string rm = mark + ".res." + std::to_string(rid);
    ui::Row r({ui::fr(1, 90), ui::px(112), ui::px(28)}, 30, 6);
    {
      ui::HStack hs(30, ui::Align::Left, 6);
      ui::iconColored(w::resourceIcon(w, res), w::resourceColor(w, res), 16);
      ui::label(resName(w, res));
    }
    double v = amount;
    ui::NumberOpt o;
    o.min = 0;
    o.max = 1e12;
    o.step = 1;
    o.digits = res == kGold ? 3 : 1;
    o.disabled = ro;
    o.tooltip = "Количество";
    if (ui::numberField("amount", v, o))
      editConst(a, key, "Ресурс константы", [&](Tx&, Constant& k) { k.res[res] = v; }, {.coalesce = "const:" + key + ":" + std::to_string(res)});
    a.markUi(rm);
    if (ui::iconButton("close", "Убрать ресурс", {.size = ui::Size::Small, .disabled = ro}))
      editConst(a, key, "Убрать ресурс константы", [&](Tx&, Constant& k) { k.res.erase(res); });
    a.markUi(rm + ".remove");
  }
  if (ro) return;
  std::vector<const CatalogItem*> left;
  for (const CatalogItem& r : w.catalogs->resources)
    if (!c.res.count(r.id)) left.push_back(&r);
  int idx = -1;
  if (ui::combo("addres", idx, int(left.size()),
                [&](int i) { return ui::Option{left[size_t(i)]->name, nullptr, left[size_t(i)]->color}; },
                {.placeholder = "Добавить ресурс", .search = 1, .icon = "plus", .disabled = left.empty()}) &&
      idx >= 0 && idx < int(left.size())) {
    Id res = left[size_t(idx)]->id;
    editConst(a, key, "Ресурс константы", [&](Tx&, Constant& k) { k.res[res] = 1; });
  }
  a.markUi(mark + ".addres");
}

// Новое значение списка: непустое и без повторов (без учёта регистра).
std::string checkValue(const std::vector<std::string>& vals, const std::string& raw, size_t except) {
  std::string v = trim(raw);
  if (v.empty()) fail("Значение не может быть пустым");
  const std::string k = utf8::searchKey(v);
  for (size_t i = 0; i < vals.size(); i++)
    if (i != except && utf8::searchKey(vals[i]) == k) fail("«" + vals[i] + "» уже есть в списке");
  return v;
}

void valuesValue(App& a, const Constant& c, const std::string& mark) {
  bool ro = a.readOnly();
  const std::string key = c.key;
  const bool races = key == schema::cst::UnitRaces;
  const std::vector<std::string> vals = c.values;   // у рас отрядов — с базовыми (allConstants)
  if (vals.empty()) ui::label("Пусто", {.ink = ui::Ink::Muted});
  for (size_t i = 0; i < vals.size(); i++) {
    ui::IdScope s{i64(i)};
    const std::string vm = mark + ".val." + std::to_string(i);
    const bool locked = races && baseRace(vals[i]);
    ui::Row r({ui::fr(1), ui::px(28)}, 30, 6);
    if (locked) {
      ui::label(vals[i], {.tooltip = "Базовая раса правил"});
    } else {
      std::string v = vals[i];
      if (ui::textField("v", v, {.maxLength = 60, .readOnly = ro}) && trim(v) != vals[i]) {
        const std::string from = vals[i];
        editConst(a, key, "Значение константы", [&](Tx& tx, Constant& k) {
          std::vector<std::string> nv = vals;
          nv[i] = checkValue(vals, v, i);
          k.values = nv;
          if (races) retargetRace(tx, from, nv[i]);
        });
      }
    }
    a.markUi(vm);
    if (locked) {
      ui::icon("lock", ui::Ink::Muted, 16, "Базовую расу нельзя удалить");
      a.markUi(vm + ".lock");
      continue;
    }
    if (ui::iconButton("close", "Убрать значение", {.size = ui::Size::Small, .disabled = ro})) {
      const std::string from = vals[i];
      editConst(a, key, "Убрать значение константы", [&](Tx& tx, Constant& k) {
        std::vector<std::string> nv = vals;
        nv.erase(nv.begin() + long(i));
        k.values = nv;
        if (races) retargetRace(tx, from, "");
      });
    }
    a.markUi(vm + ".remove");
  }
  if (ro) return;
  std::string& buf = state().add[key];
  std::string v = buf;
  if (ui::textField("add", v, {.placeholder = "Новое значение", .icon = "plus", .maxLength = 60}) && !trim(v).empty()) {
    if (editConst(a, key, "Значение константы", [&](Tx&, Constant& k) {
          std::vector<std::string> nv = vals;
          nv.push_back(checkValue(vals, v, size_t(-1)));
          k.values = nv;
        }))
      v.clear();
  }
  state().add[key] = v;
  a.markUi(mark + ".addval");
}

void valueEditor(App& a, const Constant& c, const std::string& mark) {
  switch (c.type) {
    case ConstType::Resources: resourcesValue(a, c, mark); break;
    case ConstType::Values: valuesValue(a, c, mark); break;
    default: numberValue(a, c, mark); break;
  }
}

// Название: встроенная — подпись (описание в подсказке), своя — поле.
void nameField(App& a, const Constant& c, const std::string& mark, ui::Font font) {
  if (c.builtin) {
    ui::label(orName(c.name), {.font = font, .tooltip = c.desc});
    a.markUi(mark + ".name");
    return;
  }
  ConstState& st = state();
  if (st.focusName == c.key) {
    ui::setKeyboardFocus(ui::id("name"));
    ui::scrollToItem();
    st.focusName.clear();
  }
  std::string name = c.name;
  if (ui::textField("name", name, {.placeholder = "Название константы", .maxLength = 80, .readOnly = a.readOnly(), .selectAllOnFocus = true}) &&
      trim(name) != c.name) {
    const std::string key = c.key, nn = trim(name);
    editConst(a, key, "Переименовать константу", [&](Tx& tx, Constant& k) {
      if (nn.empty()) fail("Название не может быть пустым");
      const std::string sk = utf8::searchKey(nn);
      for (const Constant& x : allConstants(tx.w()))
        if (x.key != key && utf8::searchKey(x.name) == sk) fail("Константа «" + x.name + "» уже есть");
      k.name = nn;
    });
  }
  a.markUi(mark + ".name");
}

// Встроенная — замок, своя — удаление.
void lockOrDelete(App& a, const Constant& c, const std::string& mark) {
  if (c.builtin) {
    ui::icon("lock", ui::Ink::Muted, 16, "Встроенная константа");
    return;
  }
  if (ui::iconButton("trash", "Удалить константу", {.size = ui::Size::Small, .disabled = a.readOnly(), .tone = ui::Tone::Danger})) askRemove(a, c);
  a.markUi(mark + ".delete");
}

// ---------------------------------------------------------------- выдвижная панель
void drawDrawer(App& a) {
  const World& w = a.world();
  {
    ui::HStack hs(30, ui::Align::Left, 6);
    newButton(a, "constants.new");
    ui::flex();
    if (ui::iconButton("maximize", "Открыть окно констант")) a.openEditor("constants", 0);
    a.markUi("drawer.constants.open");
  }
  ui::gap(2);
  for (const Constant& c : allConstants(w)) {
    ui::IdScope s(c.key);
    const std::string mark = "constants." + c.key;
    ui::Card card({.pad = 10});
    {
      ui::Row r({ui::px(18), ui::fr(1), ui::px(28)}, 28, 8);
      ui::icon(typeIcon(c.type), ui::Ink::Accent, 16, typeName(c.type));
      nameField(a, c, mark, ui::Font::Strong);
      lockOrDelete(a, c, mark);
    }
    valueEditor(a, c, mark);
  }
}

// ---------------------------------------------------------------- окно
void drawDetail(App& a, const Constant& c) {
  const std::string mark = "constants." + c.key;
  ui::IdScope s(c.key);
  ui::Scroll sc("detail");
  {
    ui::Row head({ui::px(52), ui::fr(1), ui::px(30)}, 52, 12);
    edkit::iconTile(typeIcon(c.type), ui::theme().accent, 52, "hash");
    {
      ui::Group g(0, 2);
      ui::caption(std::string("Константа · ") + typeName(c.type));
      nameField(a, c, mark, ui::Font::Title);
    }
    {
      ui::Group g(30, 0);
      ui::spacer(18);
      lockOrDelete(a, c, mark);
    }
  }
  if (c.builtin) {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::tag("Встроенная", ui::Tone::Accent, "lock");
    if (!c.desc.empty()) ui::label(c.desc, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  } else {
    std::string desc = c.desc;
    if (ui::textField("desc", desc, {.placeholder = "Описание", .maxLength = 200, .readOnly = a.readOnly()}) && desc != c.desc) {
      const std::string key = c.key;
      editConst(a, key, "Описание константы", [&](Tx&, Constant& k) { k.desc = trim(desc); });
    }
    a.markUi(mark + ".desc");
  }
  ui::spacer(4);
  {
    ui::Card card({.pad = 12, .icon = typeIcon(c.type), .title = c.type == ConstType::Number ? std::string_view("Значение") : std::string_view(typeName(c.type))});
    valueEditor(a, c, mark);
  }
}

void drawEditor(App& a, Id) {
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  ConstState& st = state();
  std::vector<Constant> list = allConstants(w);
  const Constant* cur = nullptr;
  for (const Constant& c : list)
    if (c.key == st.sel) cur = &c;
  if (!cur && !list.empty()) {
    cur = &list.front();
    st.sel = cur->key;
  }
  RectF R = ui::avail();
  float listW = std::round(clamp(R.w * 0.27f, 280.f, 380.f));
  RectF L{R.x, R.y, listW, R.h};
  RectF D{R.x + listW + 24, R.y, R.w - listW - 24, R.h};
  ui::draw::line(L.right() + 12, R.y, L.right() + 12, R.bottom(), th.border, 1);
  {
    ui::Area la(L, 0);
    {
      ui::HStack hs(30, ui::Align::Left, 6);
      ui::caption(fmtInt(i64(list.size())) + " " + plural(i64(list.size()), "константа", "константы", "констант"));
      ui::flex();
      newButton(a, "constants.new");
    }
    ui::Scroll sc("list");
    for (const Constant& c : list) {
      ui::IdScope s(c.key);
      if (edkit::entityRow(orName(c.name), summary(w, c), typeIcon(c.type), th.accent, c.builtin ? std::string_view("встроенная") : std::string_view(),
                           c.key == st.sel, false, c.desc))
        st.sel = c.key;
      a.markUi("constants.list." + c.key);
    }
  }
  {
    ui::Area da(D, 0);
    if (cur) drawDetail(a, *cur);
    else ui::emptyState("sliders", "Констант нет.");
  }
}

DrawerReg drawerReg({"constants", "sliders", "Глобальные константы", 95, drawDrawer});
EditorReg editorReg({"constants", "Глобальные константы", drawEditor, "sliders"});
CommandReg commandReg({"editor.constants", "Глобальные константы", "sliders", nullptr, [](App& a) { a.openEditor("constants", 0); },
                       [](App& a) { return a.ui.screen == Screen::Editor; }, false, "Справочники"});

}  // namespace
}  // namespace rg::app
