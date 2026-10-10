// Regnum — справочник «Реликвии» деревом групп (ТЗ «Доработки», п.8 и 10; «Доработки №1», п.8, 14; «№2», п.11): строки
// групп (шеврон, папка, название с правкой на месте, число реликвий с подгруппами, новая подгруппа и новая реликвия в
// группе, удаление с переносом содержимого в родителя; «Археологические находки» — с замком), под ними — подгруппы и
// реликвии группы по главенству редкости (эпохальная выше); реликвии без группы — «Без группы» в конце. Строка
// реликвии: значок (изображение реликвии или знак) с обводкой цвета редкости, название цветом редкости (щелчок — правка
// на месте), редкость, где лежит (герой, постройка, государство; спрятанная — только «спрятана»). Карточка реликвии:
// название, редкость, группа, изображение из файла, владелец-герой, описание, раздел «Канон» (карточка актива проекта:
// описание и изображение из неё). Карточка группы: название, родитель без циклов, подгруппы и реликвии.
#include <algorithm>
#include <functional>
#include <unordered_set>

#include "app/canon.h"
#include "app/editors/catalogs_internal.h"
#include "base/fs.h"
#include "gfx/icons.h"

namespace rg::app::cat {

using platform::Key;

namespace {

constexpr float kRowH = 44, kHeadH = 32, kIndent = 20, kChevW = 18;
enum Col : int { cName, cRarity, cPlace, cActions };

const char* rarityName(Rarity r) { return schema::rarity(r).name; }

const RelicGroup* groupOf(const World& w, Id g) { return g ? w.catalogs->relicGroup(g) : nullptr; }
Id parentOf(const World& w, Id g) {
  const RelicGroup* x = groupOf(w, g);
  return x && groupOf(w, x->parent) ? x->parent : 0;
}
bool isFinds(const RelicGroup& g) { return g.key == schema::kRelicArchFinds; }

// Раскрыта ли группа (нет записи — раскрыта: групп реликвий немного).
bool isOpen(const State& st, Id g) {
  auto it = st.relOpen.find(g);
  return it == st.relOpen.end() || it->second;
}
void revealGroup(State& st, const World& w, Id g) {
  for (int guard = 0; g && guard < 64; guard++) {
    st.relOpen[g] = true;
    g = parentOf(w, g);
  }
}

// ---------------------------------------------------------------- дерево
struct TRow {
  enum Kind : u8 { Group, NoGroup, Item } kind = Item;
  Id id = 0;
  int depth = 0;
  bool open = false;
  int count = 0;                       // группа: реликвий с подгруппами
  const RelicGroup* g = nullptr;
  const Relic* r = nullptr;
  rules::RelicPlace place;
};
struct Tree {
  std::vector<TRow> rows;
  std::unordered_map<Id, int> count;
  std::vector<Id> loose;               // реликвии без группы (по главенству редкости)
};

Tree buildTree(const World& w, const State& st) {
  const Catalogs& c = *w.catalogs;
  const bool searching = !st.query.empty();
  Tree t;
  std::unordered_map<Id, std::vector<Id>> kids, items;
  for (const RelicGroup& g : c.relicGroups) kids[parentOf(w, g.id)].push_back(g.id);
  for (const Relic& r : c.relics) {
    const Id g = groupOf(w, r.group) ? r.group : 0;
    items[g].push_back(r.id);
    for (Id x = g; x; x = parentOf(w, x)) t.count[x]++;
  }
  for (auto& [g, list] : items) rules::sortByRarity(w, list);
  t.loose = items[0];
  // Поиск: реликвия подходит по названию, описанию или названию группы-предка; группа видна, если подходит сама или
  // в ней есть подходящее.
  std::unordered_set<Id> visG, visR, hitG;
  if (searching) {
    auto under = [&](Id g) {
      for (int guard = 0; g && guard < 64; guard++, g = parentOf(w, g))
        if (utf8::matches(c.relicGroup(g)->name, st.query)) return true;
      return false;
    };
    auto showUp = [&](Id g) {
      for (int guard = 0; g && guard < 64; guard++, g = parentOf(w, g)) visG.insert(g);
    };
    for (const RelicGroup& g : c.relicGroups)
      if (under(g.id)) {
        hitG.insert(g.id);
        showUp(g.id);
      }
    for (const Relic& r : c.relics) {
      const Id g = groupOf(w, r.group) ? r.group : 0;
      if (utf8::matches(r.name, st.query) || utf8::matches(r.desc, st.query) || (g && hitG.count(g))) {
        visR.insert(r.id);
        showUp(g);
      }
    }
  }
  auto addItem = [&](Id rid, int depth) {
    TRow row;
    row.kind = TRow::Item;
    row.id = rid;
    row.depth = depth;
    row.r = w.relic(rid);
    row.place = rules::relicPlace(w, rid);
    t.rows.push_back(row);
  };
  std::unordered_set<Id> seen;
  std::function<void(Id, int)> addGroup = [&](Id gid, int depth) {
    if (depth > 16 || !seen.insert(gid).second) return;
    if (searching && !visG.count(gid)) return;
    TRow row;
    row.kind = TRow::Group;
    row.id = gid;
    row.depth = depth;
    row.g = c.relicGroup(gid);
    row.count = t.count.count(gid) ? t.count[gid] : 0;
    row.open = searching || isOpen(st, gid);
    t.rows.push_back(row);
    if (!row.open) return;
    for (Id k : kids[gid]) addGroup(k, depth + 1);
    for (Id rid : items[gid])
      if (!searching || visR.count(rid)) addItem(rid, depth + 1);
  };
  for (Id g : kids[0]) addGroup(g, 0);
  std::vector<Id> loose;
  for (Id rid : t.loose)
    if (!searching || visR.count(rid)) loose.push_back(rid);
  if (!loose.empty()) {
    TRow row;
    row.kind = TRow::NoGroup;
    row.count = int(t.loose.size());
    row.open = searching || st.relNoGroupOpen;
    t.rows.push_back(row);
    if (row.open)
      for (Id rid : loose) addItem(rid, 1);
  }
  return t;
}

bool selected(const State& st, const TRow& r) {
  switch (r.kind) {
    case TRow::Group: return st.relGroup == r.id;
    case TRow::NoGroup: return !st.relGroup && st.relNoGroup;
    case TRow::Item: return !st.relGroup && !st.relNoGroup && st.sel[kRelics] == r.id;
  }
  return false;
}
void selectRow(State& st, const TRow& r) {
  st.relGroup = r.kind == TRow::Group ? r.id : 0;
  st.relNoGroup = r.kind == TRow::NoGroup;
  if (r.kind == TRow::Item) st.sel[kRelics] = r.id;
}
void selectRelic(State& st, Id rid) {
  st.relGroup = 0;
  st.relNoGroup = false;
  st.sel[kRelics] = rid;
}

// ---------------------------------------------------------------- действия
// Правка одной части записи (остальное — как в мире).
void editRelic(App& a, Id id, std::string_view label, const std::function<void(Relic&)>& fn) {
  a.act(label, [&](Tx& tx) {
    const Relic* cur = tx.w().relic(id);
    if (!cur) fail("Реликвия не найдена");
    Relic r = *cur;
    fn(r);
    rules::setRelic(tx, id, r.name, r.rarity, r.desc);
  });
}

void setHolder(App& a, Id relic, Id was, Id who) {
  a.act(who ? "Владелец реликвии" : "Реликвия убрана у героя", [&](Tx& tx) {
    if (who) rules::giveRelic(tx, who, relic);
    else if (was) rules::takeRelic(tx, was, relic);
  });
}

Id addRelicAct(App& a, State& st, const World& w, Id group) {
  Id nid = 0;
  if (!a.act("Новая реликвия", [&](Tx& tx) {
        nid = rules::addRelic(tx, "", Rarity::Common);
        if (group) rules::setRelicGroup(tx, nid, group);
      }) ||
      !nid)
    return 0;
  if (group) revealGroup(st, w, group);
  else st.relNoGroupOpen = true;
  st.query.clear();
  selectRelic(st, nid);
  st.relScroll = nid;
  st.editRelic = nid;
  st.focusRelic = true;
  return nid;
}

Id addGroupAct(App& a, State& st, const World& w, Id parent) {
  Id nid = 0;
  if (!a.act(parent ? "Новая подгруппа реликвий" : "Новая группа реликвий", [&](Tx& tx) { nid = rules::addRelicGroup(tx, "", parent); }) || !nid)
    return 0;
  if (parent) revealGroup(st, w, parent);
  st.query.clear();
  st.relGroup = nid;
  st.relNoGroup = false;
  st.relScrollGroup = nid;
  st.editRelGroup = st.focusRelGroup = nid;
  return nid;
}

void renameGroupAct(App& a, Id gid, const std::string& raw) {
  const std::string name = trim(raw);
  a.act("Переименовать группу реликвий", [&](Tx& tx) { rules::renameRelicGroup(tx, gid, name); });
}

void askRemoveRelic(App& a, const World& w, Id id) {
  const Relic* r = w.relic(id);
  if (!r) return;
  std::string text = "Реликвия «" + orName(r->name) + "» будет удалена";
  if (const Id who = rules::relicHolder(w, id)) text += " и убрана из инвентаря: " + w.characterName(who);
  text += ". Действие можно отменить Ctrl+Z.";
  a.confirm("Удалить реликвию?", text, "Удалить", true,
            [id](App& x) { x.act("Удалить реликвию", [&](Tx& tx) { rules::removeRelic(tx, id); }); });
}

void askRemoveGroup(App& a, const World& w, Id gid) {
  const RelicGroup* g = groupOf(w, gid);
  if (!g) return;
  if (isFinds(*g)) {
    a.toast("«" + orName(g->name) + "» нужна археологии — её нельзя удалить", ToastKind::Warning, "lock");
    return;
  }
  const std::vector<Id> subs = rules::childRelicGroups(w, gid);
  int direct = 0;
  for (const Relic& r : w.catalogs->relics) direct += r.group == gid ? 1 : 0;
  const Id parent = parentOf(w, gid);
  std::string text = "Группа «" + orName(g->name) + "» будет удалена.";
  if (!subs.empty() || direct) {
    std::vector<std::string> what;
    if (!subs.empty()) what.push_back(nb(i64(subs.size()), "подгруппа", "подгруппы", "подгрупп"));
    if (direct) what.push_back(nb(direct, "реликвия", "реликвии", "реликвий"));
    text += " " + join(what, " и ") + (parent ? " перейдут в группу «" + orName(groupOf(w, parent)->name) + "»." : " перейдут на верхний уровень.");
  }
  text += " Действие можно отменить Ctrl+Z.";
  a.confirm("Удалить группу реликвий?", text, "Удалить", true, [gid, parent](App& x) {
    if (!x.act("Удалить группу реликвий", [&](Tx& tx) { rules::removeRelicGroup(tx, gid); })) return;
    State& s = state(x);
    if (s.relGroup == gid) s.relGroup = parent;
  });
}

// Изображение реликвии из файла пользователя (PNG/JPEG; большие уменьшаются до 256 точек).
void imageFromFile(App& a, Id relic) {
  if (a.readOnly()) {
    a.act("Изображение реликвии", [](Tx&) {});   // покажет отказ с подсказкой
    return;
  }
  auto apply = [relic](App& x, const std::string& path) {
    auto bytes = fs::readFile(path);
    if (!bytes) {
      x.toast("Не удалось прочитать файл: " + fs::filename(path), ToastKind::Warning, "warning");
      return false;
    }
    const std::string img = canon::fitImage(*bytes, 256);
    if (img.empty()) {
      x.toast("Файл не похож на изображение PNG или JPEG", ToastKind::Warning, "image");
      return false;
    }
    return x.act("Изображение реликвии", [&](Tx& tx) { rules::setRelicImage(tx, relic, img); });
  };
  detail::later(a, [apply](App& x) {
    static std::string lastDir;
    const std::string start = !lastDir.empty() ? lastDir : (x.projectPath().empty() ? fs::documentsDir() : fs::parent(x.projectPath()));
    if (!platform::dialogsSupported()) {
      x.prompt("Изображение реликвии", "Путь к файлу PNG или JPEG", "", [apply](App& y, const std::string& p) {
        if (apply(y, trim(p))) lastDir = fs::parent(trim(p));
      });
      return;
    }
    auto path = platform::openFileDialog("Изображение реликвии", {{"Изображения PNG и JPEG", {"png", "jpg", "jpeg"}}}, start);
    if (path && apply(x, *path)) lastDir = fs::parent(*path);
  });
}

// ---------------------------------------------------------------- выбор
// Выбор редкости: пункты с цветной точкой. true — выбрана другая.
bool rarityPicker(std::string_view id, Rarity& r, bool disabled) {
  std::vector<ui::Option> opts;
  for (int k = 0; k < int(Rarity::Count); k++) {
    ui::Option o;
    o.label = schema::kRarities[k].name;
    o.color = w::rarityColor(Rarity(k));
    opts.push_back(o);
  }
  int idx = int(r);
  if (!ui::combo(id, idx, std::span<const ui::Option>(opts), {.search = 0, .disabled = disabled, .tooltip = "Редкость"})) return false;
  if (idx < 0 || idx >= int(Rarity::Count) || Rarity(idx) == r) return false;
  r = Rarity(idx);
  return true;
}

// Группы реликвий в порядке дерева: (группа, глубина).
void groupTree(const World& w, Id parent, int depth, std::vector<std::pair<Id, int>>& out) {
  if (depth > 16) return;
  for (Id g : rules::childRelicGroups(w, parent)) {
    out.push_back({g, depth});
    groupTree(w, g, depth + 1, out);
  }
}

// Выбор группы реликвий (подгруппы — с отступом, справа — число реликвий с подгруппами); off — недоступна.
bool groupPicker(const World& w, std::string_view id, Id& group, std::string_view noneLabel, bool disabled, const std::function<bool(Id)>& off = {}) {
  std::vector<std::pair<Id, int>> order;
  groupTree(w, 0, 0, order);
  std::vector<std::string> labels, hints;
  for (auto [g, depth] : order) {
    std::string pad;
    for (int i = 0; i < depth; i++) pad += "    ";
    labels.push_back(pad + orName(groupOf(w, g)->name));
    hints.push_back(std::to_string(rules::relicsIn(w, g).size()));
  }
  std::vector<ui::Option> opts;
  for (size_t i = 0; i < order.size(); i++) {
    ui::Option o;
    o.label = labels[i];
    o.icon = order[i].second ? nullptr : "folder";
    o.hint = hints[i];
    o.disabled = off && off(order[i].first);
    opts.push_back(o);
  }
  int idx = -1;
  for (size_t i = 0; i < order.size(); i++)
    if (order[i].first == group) idx = int(i);
  if (!ui::combo(id, idx, std::span<const ui::Option>(opts),
                 {.placeholder = noneLabel, .noneLabel = noneLabel, .search = 1, .icon = "folder", .disabled = disabled, .tooltip = "Группа реликвий"}))
    return false;
  const Id nv = idx >= 0 && idx < int(order.size()) ? order[size_t(idx)].first : 0;
  if (nv == group) return false;
  group = nv;
  return true;
}

// ---------------------------------------------------------------- где лежит
struct PlaceView {
  std::string text;
  const char* icon = "relic";
  Color color{0, 0, 0, 0};
  Selection go;                        // куда перейти по щелчку (пусто — некуда)
  const char* tip = "";
};
PlaceView placeOf(const World& w, const rules::RelicPlace& p) {
  PlaceView v;
  switch (p.kind) {
    case rules::RelicPlace::Hero: {
      const Character* c = w.character(p.id);
      v.text = orName(c ? c->name : std::string(), "Без имени");
      v.icon = c && c->hero ? "hero" : "character";
      v.color = w::factionColor(w, p.owner);
      v.go = {SelType::Character, p.id};
      v.tip = "В инвентаре героя — открыть";
      break;
    }
    case rules::RelicPlace::Building: {
      const Building* b = w.building(p.building);
      v.text = orName(b ? b->name : std::string(), "Постройка") + " · " + w.provinceName(p.id);
      v.icon = "building";
      v.color = w::factionColor(w, p.owner);
      v.go = {SelType::Province, p.id};
      v.tip = "В хранилище постройки — открыть провинцию";
      break;
    }
    case rules::RelicPlace::State:
      v.text = w.factionName(p.id);
      v.icon = "crown";
      v.color = w::factionColor(w, p.id);
      v.go = {SelType::Faction, p.id};
      v.tip = "У государства — открыть";
      break;
    case rules::RelicPlace::Hidden: v.text = "спрятана"; break;   // место тайника не показывается
    case rules::RelicPlace::Free: v.text = "свободна"; break;
  }
  return v;
}

// Фишка места в прямоугольнике ячейки; щелчок — перейти.
void placeChip(App& a, const World& w, const rules::RelicPlace& p, RectF cell) {
  const PlaceView v = placeOf(w, p);
  if (!v.go) {
    ui::draw::text(v.text, cell, ui::Font::Body, ui::theme().textMuted);
    return;
  }
  const float cw = std::min(cell.w, std::ceil(ui::measure(v.text, ui::Font::Small)) + 40);
  ui::at(RectF{cell.x, cell.cy() - 13, cw, 26});
  ui::ChipOpt co;
  co.icon = v.icon;
  co.color = v.color;
  co.clickable = true;
  co.tooltip = v.tip;
  if (ui::chip(v.text, co) == ui::ChipAction::Click) edkit::goTo(a, v.go);
}

// ---------------------------------------------------------------- строки
struct Ctx {
  App& a;
  State& st;
  const World& w;
  bool ro;
  int toggled = -1;
};

void guides(RectF cell, int depth) {
  const ui::Theme& th = ui::theme();
  for (int k = 0; k < depth; k++) {
    const float x = std::round(cell.x + float(k) * kIndent + kChevW * 0.5f) + 0.5f;
    ui::draw::line(x, cell.y - 3, x, cell.bottom() + 3, th.border, 1);
  }
}

void pill(RectF r, std::string_view text, bool sel) {
  const ui::Theme& th = ui::theme();
  ui::draw::rect(r, sel ? th.accent.alpha(0.22f) : th.surface3, r.h * 0.5f);
  ui::draw::text(text, r, ui::Font::Caption, sel ? th.accent : th.textDim, ui::Align::Center);
}
float pillW(std::string_view text) { return std::max(22.f, std::ceil(ui::measure(text, ui::Font::Caption)) + 12); }

bool newSubButton(bool disabled) {
  const bool r = ui::iconButton("folder", "Новая подгруппа", {.size = ui::Size::Small, .disabled = disabled});
  const RectF b = ui::lastItem().rect;
  const ui::Theme& th = ui::theme();
  const float cx = b.right() - 6, cy = b.bottom() - 6;
  ui::draw::circle(cx, cy, 5.5f, th.surface2);
  ui::draw::icon("plus", RectF{cx - 4.5f, cy - 4.5f, 9, 9}, disabled ? th.textMuted : th.accent);
  return r;
}

void groupRow(Ctx& c, ui::Table& t, const TRow& row, int i, bool sel, bool hovered) {
  const ui::Theme& th = ui::theme();
  const bool none = row.kind == TRow::NoGroup;
  const RectF rr = t.rowRect();
  ui::draw::rect(rr, th.text.alpha(row.depth == 0 ? 0.035f : 0.018f), 6);
  const std::string mark = none ? std::string("catalogs.relicNogroup") : "catalogs.relicGroup." + std::to_string(row.id);
  const RectF cell = t.cell();
  guides(cell, row.depth);
  const float x0 = cell.x + float(row.depth) * kIndent;
  {
    const RectF hit{x0 - 4, rr.y + 4, kChevW + 6, rr.h - 8};
    ui::Interaction it = ui::interact(ui::id("##chev"), hit);
    if (it.hovered) ui::setCursor(platform::Cursor::Hand);
    if (it.clicked) c.toggled = i;
    if (it.hovered) ui::draw::rect(RectF{x0 - 2, rr.cy() - 10, kChevW + 2, 20}, th.hover, 5);
    ui::draw::icon(row.open ? "chevron-down" : "chevron-right", RectF{x0 + 1, rr.cy() - 7, 14, 14}, it.hovered ? th.text : th.textMuted);
    c.a.markUi(mark + ".toggle", hit);
  }
  const RectF tl{x0 + kChevW, rr.cy() - 14, 28, 28};
  {
    const Color tint = none ? th.textMuted : th.accent;
    ui::draw::rect(tl, tint.alpha(0.16f), 7);
    ui::draw::icon(row.open ? "folder-open" : "folder", tl.inset(6), tint);
  }
  const std::string cnt = std::to_string(row.count);
  const float pw = pillW(cnt);
  const float nx = tl.right() + 10;
  const RectF nameR{nx, rr.cy() - 15, std::max(40.f, cell.right() - nx - pw - 8), 30};
  if (!none && c.st.pendingRelGroup == row.id && !ui::mouse().down[0]) {
    c.st.pendingRelGroup = 0;
    c.st.editRelGroup = c.st.focusRelGroup = row.id;
  }
  if (!none && c.st.editRelGroup == row.id && !c.ro) {
    std::string name = row.g->name;
    const bool start = c.st.focusRelGroup == row.id;
    if (start) {
      ui::setKeyboardFocus(ui::id("gname"));
      c.st.focusRelGroup = 0;
    }
    ui::at(nameR);
    if (ui::textField("gname", name, {.placeholder = "Название группы", .maxLength = 60, .selectAllOnFocus = true}) && trim(name) != row.g->name)
      renameGroupAct(c.a, row.id, name);
    c.a.markUi(mark + ".name");
    if (!start && !ui::lastItem().focused) c.st.editRelGroup = 0;
  } else {
    const std::string title = none ? std::string("Без группы") : orName(row.g->name);
    const float tw = std::min(nameR.w, std::ceil(ui::measure(title, ui::Font::Strong)) + 2);
    ui::draw::text(title, nameR, ui::Font::Strong, none ? th.textDim : th.text);
    c.a.markUi(mark + ".name", RectF{nameR.x, nameR.y, tw, nameR.h});
    if (t.doubleClicked() == i) {
      const ui::Mouse& m = ui::mouse();
      if (!none && !c.ro && m.x >= nameR.x && m.x <= nameR.x + tw + 6) c.st.pendingRelGroup = row.id;
      else c.toggled = i;
    }
  }
  pill(RectF{cell.right() - pw, rr.cy() - 9, pw, 18}, cnt, sel);
  c.a.markUi(mark, rr);
  t.cell();
  t.cell();
  const RectF ac = t.cell();
  float x = ac.right() - 24;
  if (none) {
    if (sel || hovered) {
      ui::at(RectF{x, rr.cy() - 12, 24, 24});
      if (ui::iconButton("plus", "Новая реликвия без группы", {.size = ui::Size::Small, .disabled = c.ro})) addRelicAct(c.a, c.st, c.w, 0);
      c.a.markUi(mark + ".add");
    }
    return;
  }
  ui::at(RectF{x, rr.cy() - 12, 24, 24});
  if (isFinds(*row.g)) {
    ui::icon("lock", ui::Ink::Muted, 16, "Группа нужна археологии: находки героям напрямую не назначаются — удалить нельзя");
  } else {
    if (ui::iconButton("trash", "Удалить группу", {.size = ui::Size::Small, .disabled = c.ro, .tone = ui::Tone::Danger})) askRemoveGroup(c.a, c.w, row.id);
    c.a.markUi(mark + ".delete");
  }
  if (sel || hovered) {
    x -= 28;
    ui::at(RectF{x, rr.cy() - 12, 24, 24});
    if (ui::iconButton("plus", "Новая реликвия в группе", {.size = ui::Size::Small, .disabled = c.ro})) addRelicAct(c.a, c.st, c.w, row.id);
    c.a.markUi(mark + ".add");
    x -= 28;
    ui::at(RectF{x, rr.cy() - 12, 24, 24});
    if (newSubButton(c.ro)) addGroupAct(c.a, c.st, c.w, row.id);
    c.a.markUi(mark + ".addSub");
  }
}

void itemRow(Ctx& c, ui::Table& t, const TRow& row, int& selIdx, int i) {
  const ui::Theme& th = ui::theme();
  const Relic& r = *row.r;
  const Id id = r.id;
  const Color rc = w::rarityColor(r.rarity);
  const RectF rr = t.rowRect();
  // Подсветка редкостью: мягкая заливка слева.
  ui::draw::gradient(RectF{rr.x, rr.y, std::round(rr.w * 0.5f), rr.h}, rc.alpha(th.dark ? 0.10f : 0.08f), rc.alpha(0), 6, true);
  const RectF cell = t.cell();
  guides(cell, row.depth);
  const float x0 = cell.x + float(row.depth) * kIndent + kChevW;
  w::drawRelicIcon(r, RectF{x0, rr.cy() - 15, 30, 30}, 8);
  // Название цветом редкости; щелчок — правка на месте (после отпускания кнопки).
  const RectF nameR{x0 + 40, rr.cy() - 15, std::max(40.f, cell.right() - x0 - 40), 30};
  if (c.st.pendingRelic == id && !ui::mouse().down[0]) {
    c.st.pendingRelic = 0;
    c.st.editRelic = id;
    c.st.focusRelic = true;
  }
  if (c.st.editRelic == id && !c.ro) {
    const bool start = c.st.focusRelic;
    if (start) {
      ui::setKeyboardFocus(ui::id("name"));
      c.st.focusRelic = false;
    }
    std::string name = r.name;
    ui::at(nameR);
    if (ui::textField("name", name, {.placeholder = "Название реликвии", .maxLength = 80, .selectAllOnFocus = true}) && trim(name) != r.name) {
      const std::string n = trim(name);
      editRelic(c.a, id, "Переименовать реликвию", [&](Relic& x) { x.name = n; });
    }
    if (!start && !ui::lastItem().focused) c.st.editRelic = 0;
    c.a.markUi("catalogs.relic." + std::to_string(id) + ".name", nameR);
  } else {
    const std::string name = orName(r.name);
    const float tw = std::min(nameR.w, std::ceil(ui::measure(name, ui::Font::Strong)) + 2);
    const RectF hit{nameR.x, nameR.y, std::min(nameR.w, tw + 8), nameR.h};
    ui::Interaction it = ui::interact(ui::id("##name"), hit);
    if (it.hovered && !c.ro) ui::setCursor(platform::Cursor::IBeam);
    if (it.pressed) selIdx = i;
    if (it.clicked && !c.ro) c.st.pendingRelic = id;
    ui::draw::text(name, nameR, ui::Font::Strong, rc);
    if (it.hovered) ui::draw::line(nameR.x, nameR.cy() + 9, nameR.x + tw, nameR.cy() + 9, rc.alpha(0.45f), 1);
    c.a.markUi("catalogs.relic." + std::to_string(id) + ".name", hit);
  }
  if (selected(c.st, row)) c.a.markUi("catalogs.selected.name", nameR);
  t.cell();
  {
    Rarity rar = r.rarity;
    if (rarityPicker("rarity", rar, c.ro)) editRelic(c.a, id, "Редкость реликвии", [&](Relic& x) { x.rarity = rar; });
    c.a.markUi("catalogs.relic." + std::to_string(id) + ".rarity");
  }
  placeChip(c.a, c.w, row.place, t.cell());
  c.a.markUi("catalogs.relic." + std::to_string(id) + ".place");
  t.cell();
  if (ui::iconButton("trash", "Удалить реликвию", {.size = ui::Size::Small, .disabled = c.ro, .tone = ui::Tone::Danger})) askRemoveRelic(c.a, c.w, id);
  c.a.markUi("catalogs.relic." + std::to_string(id), rr);
}

// ---------------------------------------------------------------- карточки
void relicChips(State& st, const World& w, const std::vector<Id>& ids) {
  ChipFlow cf;
  for (Id rid : ids) {
    const Relic* r = w.relic(rid);
    if (!r) continue;
    if (w::relicChip(*r, false, "Выбрать реликвию") == ui::ChipAction::Click) {
      selectRelic(st, rid);
      st.relScroll = rid;
    }
  }
}

void relicCard(App& a, State& st, const World& w, const Relic& r) {
  const bool ro = a.readOnly();
  const Id id = r.id;
  const Color rc = w::rarityColor(r.rarity);
  const rules::RelicPlace place = rules::relicPlace(w, id);
  ui::Scroll sc("card");
  {
    ui::Row head({ui::px(72), ui::fr(1)}, 72, 14);
    const RectF tr = ui::next(72, 72);
    w::drawRelicIcon(r, tr.inset(4), 14);
    a.markUi("catalogs.relicCard.icon", tr);
    ui::Group g(0, 2);
    ui::caption(std::string("Реликвия · ") + rarityName(r.rarity));
    ui::label(orName(r.name), {.font = ui::Font::Title, .color = rc});
    if (const RelicGroup* gr = groupOf(w, r.group)) ui::label(rules::relicGroupPath(w, gr->id), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "folder"});
  }
  {
    // Где лежит: состояние, место (фишка-переход), находка археологов — фишками с переносом.
    const PlaceView v = placeOf(w, place);
    const char* label = place.kind == rules::RelicPlace::Hero       ? "У героя"
                        : place.kind == rules::RelicPlace::Building ? "В хранилище"
                        : place.kind == rules::RelicPlace::State    ? "У государства"
                        : place.kind == rules::RelicPlace::Hidden   ? "Спрятана"
                                                                    : "Свободна";
    ChipFlow cf;
    edkit::chip(label, {.icon = place.kind == rules::RelicPlace::Hidden ? "eye-off" : "inventory",
                        .tone = place.kind == rules::RelicPlace::Free ? ui::Tone::Neutral : ui::Tone::Info});
    a.markUi("catalogs.relicCard.place");
    if (v.go) {
      ui::ChipOpt co;
      co.icon = v.icon;
      co.color = v.color;
      co.clickable = true;
      co.tooltip = v.tip;
      if (edkit::chip(v.text, co) == ui::ChipAction::Click) edkit::goTo(a, v.go);
    }
    if (rules::isArchFind(w, id))
      edkit::chip("Находка археологов", {.icon = "shovel", .tone = ui::Tone::Warning, .tooltip = "Героям — только из владений их государства"});
  }
  {
    ui::prop("Название", "edit", 0.34f);
    std::string name = r.name;
    if (ui::textField("cardname", name, {.placeholder = "Название реликвии", .maxLength = 80, .readOnly = ro, .selectAllOnFocus = true}) &&
        trim(name) != r.name) {
      const std::string n = trim(name);
      editRelic(a, id, "Переименовать реликвию", [&](Relic& x) { x.name = n; });
    }
    a.markUi("catalogs.relicCard.name");
  }
  {
    ui::prop("Редкость", "relic", 0.34f);
    Rarity rar = r.rarity;
    if (rarityPicker("cardrarity", rar, ro)) editRelic(a, id, "Редкость реликвии", [&](Relic& x) { x.rarity = rar; });
    a.markUi("catalogs.relicCard.rarity");
  }
  {
    ui::prop("Группа", "folder", 0.34f);
    Id g = groupOf(w, r.group) ? r.group : 0;
    if (groupPicker(w, "cardgroup", g, "Без группы", ro))
      if (a.act("Группа реликвии", [&](Tx& tx) { rules::setRelicGroup(tx, id, g); })) {
        if (g) revealGroup(st, w, g);
        else st.relNoGroupOpen = true;
        st.relScroll = id;
      }
    a.markUi("catalogs.relicCard.group");
  }
  {
    ui::prop("Изображение", "image", 0.34f);
    ui::Row row({ui::fr(1), ui::px(30)}, 30, 6);
    if (ui::button("Изображение из файла…", {.icon = "upload", .fill = true, .disabled = ro, .tooltip = "PNG или JPEG; большие уменьшаются до 256 точек"}))
      imageFromFile(a, id);
    a.markUi("catalogs.relicCard.image");
    if (ui::iconButton("trash", "Убрать изображение", {.disabled = ro || r.image.empty()}))
      a.act("Убрать изображение реликвии", [&](Tx& tx) { rules::setRelicImage(tx, id, ""); });
    a.markUi("catalogs.relicCard.clearImage");
  }
  {
    ui::prop("Владелец", "inventory", 0.34f);
    const Id holder = place.kind == rules::RelicPlace::Hero ? place.id : 0;
    Id who = holder;
    if (w::characterPicker("holder", who, 0, "Не у героя", false, ro)) setHolder(a, id, holder, who);
    a.markUi("catalogs.relicCard.holder");
  }
  ui::caption("Описание");
  canon::relicDescField(a, id, 96);
  if (ui::Section s("Канон", "book", {.defaultOpen = true}); s) canon::relicSection(a, id);
  ui::spacer(4);
  {
    ui::Disabled dis(ro);
    if (ui::button("Удалить: " + orName(r.name, "реликвия"), {.variant = ui::Variant::Danger, .icon = "trash", .fill = true})) askRemoveRelic(a, w, id);
    a.markUi("catalogs.delete");
  }
}

void groupCard(App& a, State& st, const World& w, const Tree& tree, const RelicGroup& g) {
  const bool ro = a.readOnly();
  const Id gid = g.id;
  const ui::Theme& th = ui::theme();
  const Id parent = parentOf(w, gid);
  const bool finds = isFinds(g);
  ui::Scroll sc("card");
  cardHead("folder-open", th.accent, parent ? "Подгруппа реликвий" : "Группа реликвий", orName(g.name));
  if (finds) {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::tag("Находки археологов", ui::Tone::Warning, "lock");
    ui::tooltip("Реликвии группы героям напрямую не назначаются — только из владений их государства");
  }
  {
    ui::prop("Название", "edit", 0.34f);
    std::string name = g.name;
    if (ui::textField("gcardname", name, {.placeholder = "Название группы", .maxLength = 60, .readOnly = ro, .selectAllOnFocus = true}) &&
        trim(name) != g.name)
      renameGroupAct(a, gid, name);
    a.markUi("catalogs.relicGroupCard.name");
  }
  {
    ui::prop("Входит в", "folder", 0.34f);
    Id p = parent;
    if (groupPicker(w, "gparent", p, "Верхний уровень", ro, [&](Id x) { return x == gid || w.catalogs->inRelicGroup(x, gid); }))
      if (a.act("Родительская группа", [&](Tx& tx) { rules::setRelicGroupParent(tx, gid, p); })) revealGroup(st, w, p);
    a.markUi("catalogs.relicGroupCard.parent");
  }
  const std::vector<Id> subs = rules::childRelicGroups(w, gid);
  std::vector<Id> direct;
  for (const Relic& r : w.catalogs->relics)
    if (r.group == gid) direct.push_back(r.id);
  rules::sortByRarity(w, direct);
  const int total = tree.count.count(gid) ? tree.count.at(gid) : 0;
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(fmtInt(i64(subs.size())), "Подгруппы", {.icon = "folder", .tone = ui::Tone::Accent});
    ui::stat(fmtInt(total), "Всего реликвий", {.icon = "relic", .tone = ui::Tone::Info, .tooltip = "Реликвии группы и её подгрупп"});
  }
  {
    ui::Row btns({ui::fr(1), ui::fr(1)}, 30, 8);
    if (ui::button("Новая подгруппа##card", {.icon = "folder", .fill = true, .disabled = ro})) addGroupAct(a, st, w, gid);
    a.markUi("catalogs.relicGroupCard.addSub");
    if (ui::button("Новая реликвия##card", {.icon = "plus", .fill = true, .disabled = ro})) addRelicAct(a, st, w, gid);
    a.markUi("catalogs.relicGroupCard.add");
  }
  if (!subs.empty()) {
    ui::Section sec("Подгруппы", "folder", {.badge = std::to_string(subs.size())});
    if (sec) {
      ChipFlow cf;
      for (Id s : subs) {
        const RelicGroup* x = groupOf(w, s);
        if (!x) continue;
        ui::IdScope scope{i64(s) + 0x7330000LL};
        ui::ChipOpt co;
        co.icon = "folder";
        co.clickable = true;
        co.tooltip = "Выбрать подгруппу";
        const int n = tree.count.count(s) ? tree.count.at(s) : 0;
        if (edkit::chip(orName(x->name) + " · " + std::to_string(n), co) == ui::ChipAction::Click) {
          st.relGroup = s;
          revealGroup(st, w, gid);
          st.relScrollGroup = s;
        }
      }
    }
  }
  {
    ui::Section sec("Реликвии группы", "relic", {.badge = std::to_string(direct.size())});
    if (sec) {
      if (direct.empty()) ui::label(subs.empty() ? "Пусто" : "Все реликвии — в подгруппах", {.ink = ui::Ink::Muted});
      else relicChips(st, w, direct);
    }
  }
  ui::spacer(4);
  if (!finds) {
    ui::Disabled dis(ro);
    if (ui::button("Удалить группу: " + orName(g.name), {.variant = ui::Variant::Danger, .icon = "trash", .fill = true})) askRemoveGroup(a, w, gid);
    a.markUi("catalogs.relicGroupCard.delete");
  }
}

void noGroupCard(App& a, State& st, const World& w, const Tree& tree) {
  const bool ro = a.readOnly();
  const ui::Theme& th = ui::theme();
  ui::Scroll sc("card");
  cardHead("folder", th.textMuted, "Реликвии", "Без группы");
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(fmtInt(i64(tree.loose.size())), "Реликвии без группы", {.icon = "relic", .tone = ui::Tone::Info});
    ui::stat(fmtInt(i64(w.catalogs->relicGroups.size())), "Группы", {.icon = "folder", .tone = ui::Tone::Accent});
  }
  if (ui::button("Новая реликвия без группы", {.icon = "plus", .fill = true, .disabled = ro})) addRelicAct(a, st, w, 0);
  ui::Section sec("Реликвии", "relic", {.badge = std::to_string(tree.loose.size())});
  if (sec) relicChips(st, w, tree.loose);
}

}  // namespace

// ================================================================ вкладка
void drawRelics(App& a, State& st) {
  const bool ro = a.readOnly();
  {
    const World w0 = a.world();
    // Группа новой реликвии: выбранная группа или группа выбранной реликвии.
    Id target = 0;
    if (st.relGroup && groupOf(w0, st.relGroup)) target = st.relGroup;
    else if (!st.relNoGroup)
      if (const Relic* r = w0.relic(st.sel[kRelics]); r && groupOf(w0, r->group)) target = r->group;
    ui::HStack hs(30, ui::Align::Left, 8);
    searchBox(a, st, 280, "Поиск реликвий и групп");
    // Число реликвий по редкости (цветные точки).
    int counts[int(Rarity::Count)] = {};
    for (const Relic& r : w0.catalogs->relics) counts[int(r.rarity) >= 0 && r.rarity < Rarity::Count ? int(r.rarity) : 0]++;
    for (int k = int(Rarity::Count) - 1; k >= 0; k--) {
      if (!counts[k]) continue;
      const RectF d = ui::next(10, 30);
      ui::draw::circle(d.cx(), d.cy(), 4.5f, w::rarityColor(Rarity(k)));
      ui::label(std::to_string(counts[k]), {.font = ui::Font::Small, .ink = ui::Ink::Dim, .tooltip = schema::kRarities[k].name});
    }
    if (ui::iconButton("collapse", "Свернуть все группы")) {
      for (const RelicGroup& g : w0.catalogs->relicGroups) st.relOpen[g.id] = false;
      st.relNoGroupOpen = false;
    }
    a.markUi("catalogs.relicCollapseAll");
    if (ui::iconButton("expand", "Развернуть все группы")) {
      st.relOpen.clear();
      st.relNoGroupOpen = true;
    }
    a.markUi("catalogs.relicExpandAll");
    ui::flex();
    if (ui::button("Новая группа", {.icon = "folder", .disabled = ro})) addGroupAct(a, st, w0, 0);
    a.markUi("catalogs.relicNewGroup");
    const std::string tip = target ? "Новая реликвия в группе «" + orName(groupOf(w0, target)->name) + "»" : std::string("Новая реликвия без группы");
    if (ui::button("Новая реликвия", {.variant = ui::Variant::Primary, .icon = "plus", .disabled = ro, .tooltip = tip, .shortcut = {Key::Insert, 0}}))
      addRelicAct(a, st, w0, target);
    a.markUi("catalogs.add");
  }
  ui::spacer(2);
  const World w = a.world();   // снимок кадра (после кнопок: новая запись уже в нём)
  // Показать строку: раскрыть предков.
  if (st.relScroll) {
    if (const Relic* r = w.relic(st.relScroll)) {
      if (groupOf(w, r->group)) revealGroup(st, w, r->group);
      else st.relNoGroupOpen = true;
    }
  }
  if (st.relScrollGroup) revealGroup(st, w, parentOf(w, st.relScrollGroup));
  if (st.relGroup && !groupOf(w, st.relGroup)) st.relGroup = 0;
  const Tree tree = buildTree(w, st);
  const int n = int(tree.rows.size());
  int selIdx = -1;
  for (int i = 0; i < n; i++)
    if (selected(st, tree.rows[size_t(i)])) selIdx = i;
  // Выбор по умолчанию (ничего не выбрано, выбранное исчезло или скрыто поиском) — первая реликвия дерева.
  const bool valid = st.relGroup ? groupOf(w, st.relGroup) != nullptr : st.relNoGroup || w.relic(st.sel[kRelics]) != nullptr;
  if (!valid || (selIdx < 0 && !st.query.empty())) {
    st.relGroup = 0;
    st.relNoGroup = false;
    st.sel[kRelics] = 0;
    for (int i = 0; i < n && selIdx < 0; i++)
      if (tree.rows[size_t(i)].kind == TRow::Item) {
        selectRow(st, tree.rows[size_t(i)]);
        selIdx = i;
      }
  }
  const int selBefore = selIdx;
  const Split sp = split(ui::avail());
  int toggled = -1;
  {
    const RectF T = sp.table;
    ui::Area ta(T, 0);
    ui::Scroll sc("tblscroll", T.h);
    const ui::Column cols[] = {
        {"Название", nullptr, ui::fr(2.4f, 220)},
        {"Редкость", "relic", ui::px(172)},
        {"Где", "map-pin", ui::fr(1.3f, 150), ui::Align::Left, false, "Где лежит реликвия: герой, хранилище постройки, государство"},
        {"", nullptr, ui::px(96)},
    };
    ui::Table t("tbl", std::span<const ui::Column>(cols), n,
                {.rowHeight = kRowH, .selected = &selIdx, .emptyIcon = "relic", .emptyText = st.query.empty() ? "Реликвий пока нет" : "Ничего не найдено"});
    if (st.relScroll || st.relScrollGroup) {
      for (int i = 0; i < n; i++) {
        const TRow& row = tree.rows[size_t(i)];
        if ((row.kind == TRow::Item && row.id == st.relScroll) || (row.kind == TRow::Group && row.id == st.relScrollGroup))
          revealRow(sc, T.h, kHeadH, kRowH, i);
      }
      st.relScroll = st.relScrollGroup = 0;
    }
    Ctx c{a, st, w, ro};
    const ui::Mouse& m = ui::mouse();
    for (int i : t) {
      const TRow& row = tree.rows[size_t(i)];
      ui::IdScope s{i64(row.kind) * 0x100000000LL + i64(row.id)};
      const RectF rr = t.rowRect();
      const bool hovered = rr.contains(m.x, m.y) && !ui::anyModalOpen();
      if (row.kind == TRow::Item) itemRow(c, t, row, selIdx, i);
      else groupRow(c, t, row, i, selIdx == i, hovered);
    }
    toggled = c.toggled;
  }
  a.markUi("catalogs.table", sp.table);
  if (toggled >= 0) {
    const TRow& row = tree.rows[size_t(toggled)];
    if (row.kind == TRow::NoGroup) st.relNoGroupOpen = !row.open;
    else st.relOpen[row.id] = !row.open;
  }
  if (selIdx != selBefore && selIdx >= 0 && selIdx < n) selectRow(st, tree.rows[size_t(selIdx)]);
  if (!sp.card.empty()) {
    ui::Area ca(sp.card, 0);
    const Relic* cur = !st.relGroup && !st.relNoGroup ? w.relic(st.sel[kRelics]) : nullptr;
    if (st.relGroup && groupOf(w, st.relGroup)) {
      groupCard(a, st, w, tree, *groupOf(w, st.relGroup));
    } else if (st.relNoGroup) {
      noGroupCard(a, st, w, tree);
    } else if (cur) {
      relicCard(a, st, w, *cur);
    } else {
      ui::spacer(std::max(0.f, sp.card.h * 0.3f));
      if (st.query.empty() && w.catalogs->relics.empty()) {
        if (ui::emptyState("relic", "Реликвий пока нет", ro ? std::string_view() : std::string_view("Новая реликвия"), "plus")) addRelicAct(a, st, w, 0);
      } else {
        ui::emptyState("relic", st.query.empty() ? "Выберите реликвию" : "Ничего не найдено");
      }
    }
  }
  // Delete — удалить выбранное; F2 — переименовать.
  if (!ro && ui::shortcut({Key::Delete, 0})) {
    if (st.relGroup) askRemoveGroup(a, w, st.relGroup);
    else if (!st.relNoGroup && st.sel[kRelics]) askRemoveRelic(a, w, st.sel[kRelics]);
  }
  if (!ro && ui::shortcut({Key::F2, 0})) {
    if (st.relGroup) {
      st.editRelGroup = st.focusRelGroup = st.relGroup;
      st.relScrollGroup = st.relGroup;
    } else if (!st.relNoGroup && st.sel[kRelics]) {
      st.editRelic = st.sel[kRelics];
      st.focusRelic = true;
      st.relScroll = st.sel[kRelics];
    }
  }
}

}  // namespace rg::app::cat
