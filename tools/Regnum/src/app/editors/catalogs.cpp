// Regnum — окно справочников: двенадцать вкладок (a.openEditor("catalogs", N) открывает вкладку N): 1 ресурсы (деревом
// групп, catalogs_resources.cpp), 2 расы, 3 культуры, 4 религии, 5 формы правления, 6 должности, 7 эссенции
// элементов, 8 реликвии (catalogs_relics.cpp), 9 особые отряды (catalogs_specials.cpp), 10 классы героев
// (catalogs_classes.cpp), 11 археологические места и 12 сундуки сокровищ (catalogs_arch.cpp). Простые справочники —
// таблица с правкой названия и цвета, добавлением, удалением через rules::removeCatalogItem (с перечнем мест
// использования; встроенные записи закреплены), числами использования и карточкой выбранной записи со ссылками;
// у должности — модификаторы занятой и пустующей должности (ТЗ «Общие доработки», п.4–5), у расы, культуры, религии и
// формы правления — модификаторы записи (аудит 2026-10-23, п.7.5). Использование записей
// считается одним проходом по миру и кешируется по тождеству мира (catalogs_usage.cpp). Расы и культуры — по алфавиту
// (ТЗ «Доработки №2», п.2–3). Узкое окно — вкладки только значками. Справочники — правка мира: только администратор
// редактора (игроку окно закрыто, App::canEdit).
#include <algorithm>

#include "app/editors/catalogs_internal.h"

namespace rg::app::cat {

using platform::Key;
using rules::CatalogList;

namespace {

// Вкладки окна: значок, подпись, подсказка, метка области вкладки (catalogs.view.<mark>).
struct TabInfo {
  const char* icon;
  const char* title;
  const char* tip;
  const char* mark;
};
const TabInfo kTabs[kTabCount] = {
    {"resource", "Ресурсы", "Ресурсы и их группы", "resources"},
    {"race", "Расы", "Расы", "races"},
    {"culture", "Культуры", "Культуры", "cultures"},
    {"religion", "Религии", "Религии", "religions"},
    {"crown", "Формы правления", "Формы правления", "governments"},
    {"council", "Должности", "Должности совета", "positions"},
    {"essence", "Эссенции", "Эссенции элементов", "essences"},
    {"relic", "Реликвии", "Реликвии", "relics"},
    {"special-unit", "Особые отряды", "Особые отряды", "specials"},
    {"hero-class", "Классы героев", "Классы героев и деревья талантов", "classes"},
    {"pickaxe", "Археологические места", "Археологические места и финальные награды", "archsites"},
    {"chest", "Сундуки сокровищ", "Сундуки сокровищ", "chests"},
};


// ================================================================ простые справочники
// Числовые столбцы справочника.
struct NumCol {
  const char* title;
  const char* icon;
  const char* tip;
  float min;
};
std::vector<NumCol> numCols(CatalogList l) {
  switch (l) {
    case CatalogList::Races:
      return {{"Провинции", "province", "Провинции, где живёт раса", 96}, {"Население", "population", "Жители расы во всех провинциях", 110}};
    case CatalogList::Cultures:
    case CatalogList::Religions:
      return {{"Провинции", "province", "Провинции с этой записью", 96}, {"Государства", "crown", "Фракции, где это основная запись", 110}};
    case CatalogList::Governments: return {{"Государства", "crown", "Государства с этой формой правления", 110}};
    case CatalogList::Positions: return {{"Места в совете", "council", "Места в советах государств", 130}};
    case CatalogList::Essences:
      return {{"Государства", "crown", "Государства с запасом эссенции", 104},
              {"Запасы", "treasury", "Сумма запасов всех фракций", 104},
              {"Войска", "army", "Строки войск, которым нужна эссенция", 90}};
    case CatalogList::Resources: break;
  }
  return {};
}
double numOf(CatalogList l, const Use& u, int k) {
  switch (l) {
    case CatalogList::Races: return k == 0 ? double(u.provinces.size()) : double(u.population);
    case CatalogList::Cultures:
    case CatalogList::Religions: return k == 0 ? double(u.provinces.size()) : double(u.factions.size());
    case CatalogList::Governments: return double(u.factions.size());
    case CatalogList::Positions: return double(u.seats.size());
    case CatalogList::Essences: return k == 0 ? double(u.states) : k == 1 ? u.stock : double(u.rows.size());
    case CatalogList::Resources: break;
  }
  return 0;
}

struct Row {
  const CatalogItem* item;
  const Use* use;
};

void listTable(App& a, State& st, const World& w, const ListDef& d, const std::vector<Row>& rows, RectF T) {
  const bool ro = !a.canEdit();
  const auto nums = numCols(d.list);
  std::vector<ui::Column> cols;
  cols.push_back({"", nullptr, ui::px(40)});
  cols.push_back({"Название", nullptr, ui::fr(nums.size() > 2 ? 1.6f : 2.f, 160), ui::Align::Left, true});
  if (d.colored) cols.push_back({"", "palette", ui::px(52), ui::Align::Left, false, "Цвет на карте и в списках"});
  for (const NumCol& n : nums) cols.push_back({n.title, n.icon, ui::fr(1, n.min), ui::Align::Right, true, n.tip});
  cols.push_back({"", nullptr, ui::px(40)});
  const int firstNum = 2 + (d.colored ? 1 : 0);
  Id& sel = st.sel[size_t(d.tab)];
  int selIdx = -1;
  for (size_t i = 0; i < rows.size(); i++)
    if (rows[i].item->id == sel) selIdx = int(i);
  const int selBefore = selIdx;
  const std::string emptyText = st.query.empty() ? std::string("Справочник пуст") : std::string("Ничего не найдено");
  ui::Area ta(T, 0);
  ui::Scroll sc("tblscroll", T.h);
  ui::Table t("tbl", cols, int(rows.size()), {.rowHeight = 42, .selected = &selIdx, .emptyIcon = d.icon, .emptyText = emptyText});
  auto cmp = [&](int x, int y, int col) {
    const Row& A = rows[size_t(x)];
    const Row& B = rows[size_t(y)];
    if (col == 1) return compareRu(A.item->name, B.item->name);
    const int k = col - firstNum;
    const double p = numOf(d.list, *A.use, k), q = numOf(d.list, *B.use, k);
    return p < q ? -1 : p > q ? 1 : 0;
  };
  t.sort(cmp);
  // Новая запись или переименованная в списке по алфавиту: показать строку (таблица строит только видимые строки).
  if (st.focusName || st.revealSel)
    for (size_t i = 0; i < rows.size(); i++)
      if (rows[i].item->id == (st.focusName ? st.focusName : sel)) revealRow(sc, T.h, 32, 42, displayIndex(int(i), int(rows.size()), t.sortColumn(), t.sortDescending(), cmp));
  st.revealSel = false;
  for (int i : t) {
    const CatalogItem& c = *rows[size_t(i)].item;
    const Use& u = *rows[size_t(i)].use;
    const Id id = c.id;
    t.cell();
    swatch(d.list, c, 28);
    t.cell();
    {
      std::string name = c.name;
      if (st.focusName == id) {
        ui::setKeyboardFocus(ui::id("name"));
        st.focusName = 0;
      }
      if (ui::textField("name", name, {.placeholder = "Название", .maxLength = 60, .readOnly = ro, .selectAllOnFocus = true}) && name != c.name) {
        renameItem(a, d.list, id, name);
        st.revealSel = true;   // строка могла переехать (список по алфавиту)
      }
      if (ui::lastItem().focused && id != sel) sel = id;   // правка строки — её карточка
      if (id == sel) a.markUi("catalogs.selected.name");
    }
    if (d.colored) {
      t.cell();
      Color col = c.color;
      ui::Disabled dcol(ro);
      if (ui::colorButton("color", col, {.tooltip = "Цвет на карте и в списках", .size = ui::Size::Small, .hex = false}) && !ro)
        setItemColor(a, d.list, id, col);
    }
    for (size_t k = 0; k < nums.size(); k++) {
      const double v = numOf(d.list, u, int(k));
      if (d.list == CatalogList::Races && k == 1) t.text(v ? fmtShort(v) : std::string("—"), v ? ui::Ink::Normal : ui::Ink::Muted);
      else numCell(t, v, d.list == CatalogList::Essences && k == 1 ? 3 : 0);
    }
    t.cell();
    if (locked(d.list, c)) ui::icon("lock", ui::Ink::Muted, 16, "Встроенная запись — удалить нельзя");
    else if (ui::iconButton("trash", "Удалить запись", {.size = ui::Size::Small, .disabled = ro, .tone = ui::Tone::Danger})) askRemove(a, d.list, id);
  }
  if (selIdx != selBefore && selIdx >= 0 && selIdx < int(rows.size())) sel = rows[size_t(selIdx)].item->id;
}

// Модификаторы должности (ТЗ «Общие доработки», п.4–5): действуют для государства, пока в его совете должность
// занята (или пустует). Предлагаются модификаторы видов «Везде» и «Глобальный».
void positionMods(App& a, const CatalogItem& c) {
  const Id id = c.id;
  const bool ro = !a.canEdit();
  auto field = [&](const char* title, const char* icon, const std::string& mark, bool vacant) {
    const std::vector<Id>& cur = vacant ? c.vacantModifiers : c.modifiers;
    ui::Section sec(title, icon, {.badge = std::to_string(cur.size())});
    if (!sec) return;
    std::vector<Id> ids = cur;
    const bool changed = w::modifierList(mark, ids, ro, w::ModScope::Faction);
    a.markUi(mark);   // поле добавления — последний элемент списка
    if (changed)
      a.act(vacant ? "Модификатор отсутствия должности" : "Модификатор должности", [&](Tx& tx) {
        for (CatalogItem& x : tx.catalogs().positions)
          if (x.id == id) (vacant ? x.vacantModifiers : x.modifiers) = ids;
      });
  };
  field("Модификатор должности", "council", "catalogs.position.mods", false);
  field("Модификатор отсутствия должности", "user", "catalogs.position.vacant", true);
}

// Модификаторы расы, культуры, религии и формы правления (ТЗ аудита 7.5, RULES.md §1): раса — провинциям, где она
// преобладает (локальные эффекты; предлагаются модификаторы «Для провинций» и «Везде»); культура и религия — провинциям
// с ней и государствам с ней («Для провинций», «Глобальный», «Везде»); форма правления — государствам («Глобальный»,
// «Везде»).
void traitMods(App& a, CatalogList l, const CatalogItem& c) {
  const Id id = c.id;
  const bool ro = !a.canEdit();
  const w::ModScope scope = l == CatalogList::Races ? w::ModScope::Local : l == CatalogList::Governments ? w::ModScope::Faction : w::ModScope::Any;
  ui::Section sec("Модификаторы", "sparkles", {.badge = std::to_string(c.modifiers.size())});
  if (!sec) return;
  std::vector<Id> ids = c.modifiers;
  const bool changed = w::modifierList("catalogs.item.mods", ids, ro, scope);
  a.markUi("catalogs.item.mods");   // поле добавления — последний элемент списка
  if (changed)
    a.act("Модификаторы записи справочника", [&](Tx& tx) {
      for (CatalogItem& x : rules::catalogList(tx.catalogs(), l))
        if (x.id == id) x.modifiers = ids;
    });
}

void listCard(App& a, State& st, const World& w, const ListDef& d, const Row& row) {
  const CatalogItem& c = *row.item;
  const Use& u = *row.use;
  const bool ro = !a.canEdit();
  ui::Scroll sc("card");
  const ui::Theme& th = ui::theme();
  cardHead(itemIcon(d.list, c), d.colored ? c.color : th.textDim, d.noun, orName(c.name));
  if (locked(d.list, c)) {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::tag("Встроенная запись", ui::Tone::Accent, "lock");
  }
  if (d.colored) {
    ui::prop("Цвет", "palette", 0.3f);
    Color col = c.color;
    ui::Disabled dcol(ro);
    if (ui::colorButton("cardcolor", col, {.tooltip = "Цвет на карте и в списках"}) && !ro) setItemColor(a, d.list, c.id, col);
    a.markUi("catalogs.card.color");
  }
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    switch (d.list) {
      case CatalogList::Races:
        ui::stat(fmtShort(double(u.population)), "Население", {.icon = "population", .tone = ui::Tone::Info});
        ui::stat(fmtInt(i64(u.provinces.size())), "Провинции", {.icon = "province", .tone = ui::Tone::Accent});
        break;
      case CatalogList::Cultures:
      case CatalogList::Religions:
        ui::stat(fmtInt(i64(u.provinces.size())), "Провинции", {.icon = "province", .tone = ui::Tone::Info});
        ui::stat(fmtInt(i64(u.factions.size())), "Государства", {.icon = "crown", .tone = ui::Tone::Accent});
        break;
      case CatalogList::Governments:
        ui::stat(fmtInt(i64(u.factions.size())), "Государства", {.icon = "crown", .tone = ui::Tone::Accent});
        ui::stat(fmtInt(i64(u.total())), "Всего ссылок", {.icon = "link", .tone = ui::Tone::Info});
        break;
      case CatalogList::Positions:
        ui::stat(fmtInt(i64(u.seats.size())), "Места в совете", {.icon = "council", .tone = ui::Tone::Accent});
        ui::stat(fmtInt(i64(std::count_if(u.seats.begin(), u.seats.end(), [&](auto& s) {
                   const Faction* f = w.faction(s.first);
                   if (!f) return false;
                   for (const CouncilSeat& x : f->council)
                     if (x.id == s.second) return x.character == 0;
                   return false;
                 }))),
                 "Свободные", {.icon = "user", .tone = ui::Tone::Warning});
        break;
      case CatalogList::Essences:
        ui::stat(fmtInt(u.states), "Государства с запасом", {.icon = "crown", .tone = ui::Tone::Accent});
        ui::stat(fmtNum(u.stock, 3), "Запасы фракций", {.icon = "treasury", .tone = u.stock < 0 ? ui::Tone::Danger : ui::Tone::Info});
        break;
      case CatalogList::Resources: break;
    }
  }
  if (d.list == CatalogList::Positions) positionMods(a, c);
  else if (schema::catalogHasModifiers(d.list)) traitMods(a, d.list, c);
  usageSection(a, st, w, d.list, c, u);
  ui::spacer(4);
  if (!locked(d.list, c) && deleteButton(a, orName(c.name, "запись"))) askRemove(a, d.list, c.id);
}

// Справочники, которые показываются по алфавиту (новые записи и прежние миры — тоже).
bool alphabetical(CatalogList l) { return l == CatalogList::Races || l == CatalogList::Cultures; }

void drawList(App& a, State& st, const ListDef& d) {
  const bool ro = !a.canEdit();
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    searchBox(a, st, 300);
    ui::flex();
    if (newButton(a, d.newLabel)) {
      if (Id nid = addItem(a, d.list)) {
        st.query.clear();
        st.sel[size_t(d.tab)] = nid;
        st.focusName = nid;
      }
    }
  }
  ui::spacer(2);
  const World w = a.world();   // снимок кадра (после кнопок: новая запись уже в нём); правка посреди кадра заменяет мир
  auto map = usageMap(w, d.list);
  std::vector<Row> rows;
  for (const CatalogItem& c : rules::catalogList(*w.catalogs, d.list)) {
    if (!st.query.empty() && !utf8::matches(c.name, st.query)) continue;
    rows.push_back(Row{&c, &useOf(*map, c.id)});
  }
  if (alphabetical(d.list)) std::stable_sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) { return compareRu(x.item->name, y.item->name) < 0; });
  Id& sel = st.sel[size_t(d.tab)];
  keepSelection(sel, rows, [](const Row& r) { return r.item->id; });
  const Split sp = split(ui::avail());
  listTable(a, st, w, d, rows, sp.table);
  a.markUi("catalogs.table", sp.table);
  if (!sp.card.empty()) {
    ui::Area ca(sp.card, 0);
    const Row* cur = nullptr;
    for (const Row& r : rows)
      if (r.item->id == sel) cur = &r;
    if (cur) {
      listCard(a, st, w, d, *cur);
    } else {
      ui::spacer(std::max(0.f, sp.card.h * 0.3f));
      ui::emptyState(d.icon, st.query.empty() ? "Справочник пуст" : "Ничего не найдено");
    }
  }
  // Delete — удалить выбранную запись (вне текстовых полей).
  if (!ro && sel && ui::shortcut({Key::Delete, 0})) askRemove(a, d.list, sel);
}

// ================================================================ окно
// Вкладки: подписи и числа, если помещаются; иначе подписи без чисел; иначе значки (с числами или без).
void drawTabs(App& a, State& st, const World& w) {
  const Catalogs& c = *w.catalogs;
  const int counts[kTabCount] = {int(c.resources.size()), int(c.races.size()),     int(c.cultures.size()),
                                 int(c.religions.size()), int(c.governments.size()), int(c.positions.size()),
                                 int(c.essences.size()),  int(c.relics.size()),     int(c.specials.size()),
                                 int(c.classes.size()),   int(c.archSites.size()),   int(c.chests.size())};
  const float avail = ui::avail().w;
  auto natural = [&](bool labels, bool badges) {
    float total = 0;
    for (int i = 0; i < kTabCount; i++) {
      float x = 18 + (labels ? ui::measure(kTabs[i].title, ui::Font::Strong) + 7 : 0);
      if (badges && counts[i] > 0) x += ui::measure(std::to_string(counts[i]), ui::Font::Caption) + 18;
      total += x + (labels ? 24 : 20) + 2;
    }
    return total;
  };
  const int mode = natural(true, true) <= avail ? 0 : natural(true, false) <= avail ? 1 : natural(false, true) <= avail ? 2 : 3;
  std::vector<ui::Tab> items;
  for (int i = 0; i < kTabCount; i++)
    items.push_back(ui::Tab{kTabs[i].icon, mode <= 1 ? std::string_view(kTabs[i].title) : std::string_view(), kTabs[i].tip,
                            mode == 0 || mode == 2 ? counts[i] : 0, ui::Tone::Neutral});
  int tab = st.tab;
  ui::tabs("tabs", tab, std::span<const ui::Tab>(items));
  a.markUi("catalogs.tabs");
  if (tab != st.tab) {
    st.tab = tab;
    st.query.clear();
    st.editRelic = 0;
    st.focusName = 0;
  }
}

void drawEditor(App& a, Id arg) {
  State& st = state(a);
  if (!a.admin()) {   // игроку справочники закрыты (ТЗ «Доработки №4», п.5)
    ui::spacer(std::max(0.f, ui::avail().h * 0.3f));
    ui::emptyState("lock", "Справочники доступны администратору редактора");
    a.markUi("catalogs.locked");
    return;
  }
  if (arg >= 1 && arg <= Id(kTabCount)) {
    if (st.tab != int(arg) - 1) {
      st.tab = int(arg) - 1;
      st.query.clear();
      st.editRelic = 0;
      st.focusName = 0;
    }
    a.ui.editorArg = 0;   // вкладка выбрана; дальше — по щелчкам
  }
  {
    const World w = a.world();
    drawTabs(a, st, w);
  }
  ui::spacer(4);
  a.markUi(std::string("catalogs.view.") + kTabs[st.tab].mark, ui::avail());
  if (ui::shortcut({Key::F, ui::ModPrimary})) st.focusSearch = true;
  switch (st.tab) {
    case kResources: drawResources(a, st); break;
    case kRelics: drawRelics(a, st); break;
    case kSpecials: drawSpecials(a, st); break;
    case kClasses: drawClasses(a, st); break;
    case kArchSites: drawArchSites(a, st); break;
    case kChests: drawChests(a, st); break;
    default:
      if (const ListDef* d = defOfTab(st.tab)) drawList(a, st, *d);
      break;
  }
}

EditorReg editorReg({"catalogs", "Справочники", drawEditor, "book", "reference", 20});
CommandReg commandReg({"editor.catalogs", "Справочники: ресурсы, расы, культуры, религии, эссенции, реликвии, особые отряды…", "book", nullptr,
                       [](App& a) { a.openEditor("catalogs", 0); }, [](App& a) { return a.ui.screen == Screen::Editor; }, false,
                       "Справочники", 80});

}  // namespace
}  // namespace rg::app::cat
