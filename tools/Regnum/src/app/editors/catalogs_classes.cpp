// Regnum — справочник «Классы героев» (ТЗ «Доработки №4», п.6, 8): слева — список классов (значок цвета класса,
// число талантов и героев), посередине — класс (название, значок, цвет, очки на ярус, удаление) и его дерево
// талантов сеткой 4 × ярусы (app/talent_tree.h: щелчок по пустой ячейке — новый талант, перетаскивание — перенос,
// правый щелчок — меню), справа — карточка выбранного таланта (название, значок, стоимость 1…5, условие из яруса
// выше, описание, модификаторы героя, кто изучил, удаление) или класса (описание, герои класса). Правка дерева,
// после которой таланты героев стали незаконными, снимает их — уведомление с числом героев.
#include <algorithm>

#include "app/editors/catalogs_internal.h"
#include "app/talent_tree.h"
#include "gfx/icons.h"

namespace rg::app::cat {

using platform::Key;

namespace {

// ---------------------------------------------------------------- значки классов и талантов
struct IconChoice {
  const char* name;
  const char* title;
};
const IconChoice kIcons[] = {
    {"hero-class", "Класс"},     {"talent", "Талант"},        {"hero", "Герой"},           {"sword", "Меч"},
    {"swords", "Клинки"},        {"shield", "Щит"},           {"bow", "Лук"},              {"staff", "Посох"},
    {"wand", "Жезл"},            {"sparkles", "Волшебство"},  {"star", "Звезда"},          {"bolt", "Молния"},
    {"flame", "Пламя"},          {"skull", "Смерть"},         {"lich", "Лич"},             {"heal", "Исцеление"},
    {"plague", "Чума"},          {"essence", "Эссенция"},     {"heart", "Жизнь"},          {"eye", "Око"},
    {"moon", "Луна"},            {"sun", "Солнце"},           {"religion", "Вера"},        {"book", "Знание"},
    {"scroll", "Свиток"},        {"crown", "Корона"},         {"horse", "Конь"},           {"army", "Войско"},
    {"battle", "Битва"},         {"war", "Война"},            {"banner", "Знамя"},         {"castle", "Твердыня"},
    {"hammer", "Молот"},         {"shackles", "Оковы"},       {"relic", "Реликвия"},       {"dice", "Удача"},
    {"hourglass", "Время"},      {"mountain", "Горы"},        {"sea", "Море"},             {"u-casters", "Чародеи"},
    {"u-ranged", "Стрелки"},     {"u-beasts", "Звери"},       {"u-monsters", "Чудовища"},  {"u-elementals", "Стихии"},
    {"u-flying", "Крылья"},      {"u-heavy-inf", "Латы"},     {"u-machines", "Механизмы"}, {"mercenary", "Наёмник"},
};

const char* iconTitle(std::string_view icon) {
  for (const IconChoice& c : kIcons)
    if (icon == c.name) return c.title;
  return "Без значка";
}

bool iconGrid(std::string_view id, std::string& icon, const char* fallback, bool disabled, std::string_view tip) {
  ui::IdScope scope(id);
  const bool none = icon.empty() || !gfx::hasIcon(icon);
  const std::string t = std::string(tip) + ": " + iconTitle(none ? std::string_view() : std::string_view(icon));
  if (ui::iconButton(none ? fallback : icon.c_str(), t, {.variant = ui::Variant::Secondary, .disabled = disabled})) ui::openPopup("grid");
  bool changed = false;
  if (ui::beginPopup("grid", {.side = ui::Side::Below, .width = 8 * 34 + 7 * 4 + 16})) {
    ui::caption(tip);
    ui::Row g({ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34)}, 34, 4);
    for (const IconChoice& c : kIcons) {
      if (!gfx::hasIcon(c.name)) continue;
      if (ui::iconButton(c.name, c.title, {.toggled = icon == c.name})) {
        changed = icon != c.name;
        icon = c.name;
        ui::closePopup();
      }
    }
    ui::endPopup();
  }
  return changed;
}

const char* classIcon(const HeroClass& c) { return !c.icon.empty() && gfx::hasIcon(c.icon) ? c.icon.c_str() : "hero-class"; }
const char* talentIcon(const Talent& t) { return !t.icon.empty() && gfx::hasIcon(t.icon) ? t.icon.c_str() : "talent"; }

std::vector<Id> heroesOf(const World& w, Id cls) {
  std::vector<Id> out;
  w.characters.each([&](const Character& c) {
    if (c.heroClass == cls) out.push_back(c.id);
  });
  std::sort(out.begin(), out.end(), [&](Id x, Id y) { return compareRu(w.characterName(x), w.characterName(y)) < 0; });
  return out;
}

std::vector<Id> learnedBy(const World& w, Id talent) {
  std::vector<Id> out;
  w.characters.each([&](const Character& c) {
    if (std::find(c.talents.begin(), c.talents.end(), talent) != c.talents.end()) out.push_back(c.id);
  });
  return out;
}

void lostToast(App& a, int lost) {
  if (lost > 0)
    a.toast("Таланты, которые больше нельзя изучить, сняты у " + nb(lost, "героя", "героев", "героев"), ToastKind::Info, "talent");
}

// Правка класса целиком (остальное — как в мире).
void editClass(App& a, Id cls, std::string_view label, const std::function<void(HeroClass&)>& fn, const std::string& coalesce = {}) {
  int lost = 0;
  a.act(label, [&](Tx& tx) {
    const HeroClass* cur = tx.w().heroClass(cls);
    if (!cur) fail("Класс героя не найден");
    HeroClass c = *cur;
    fn(c);
    lost = rules::setHeroClass(tx, c);
  }, {.coalesce = coalesce});
  lostToast(a, lost);
}

void editTalent(App& a, Id talent, std::string_view label, const std::function<void(Talent&)>& fn, const std::string& coalesce = {}) {
  int lost = 0;
  a.act(label, [&](Tx& tx) {
    const HeroClass* c = rules::talentClass(tx.w(), talent);
    const Talent* cur = c ? c->talent(talent) : nullptr;
    if (!cur) fail("Талант не найден");
    Talent t = *cur;
    fn(t);
    lost = rules::setTalent(tx, t);
  }, {.coalesce = coalesce});
  lostToast(a, lost);
}

Id addClassAct(App& a, State& st) {
  Id nid = 0;
  if (!a.act("Новый класс героя", [&](Tx& tx) { nid = rules::addHeroClass(tx, ""); }) || !nid) return 0;
  st.query.clear();
  st.sel[kClasses] = nid;
  st.talent = 0;
  st.focusName = nid;
  return nid;
}

void askRemoveClass(App& a, const World& w, Id cls) {
  const HeroClass* c = w.heroClass(cls);
  if (!c) return;
  const size_t heroes = heroesOf(w, cls).size();
  std::string text = "Класс «" + orName(c->name) + "» и его дерево талантов (" + nb(i64(c->talents.size()), "талант", "таланта", "талантов") + ") будут удалены.";
  if (heroes) text += " " + nb(i64(heroes), "герой потеряет", "героя потеряют", "героев потеряют") + " класс и таланты.";
  text += " Действие можно отменить Ctrl+Z.";
  a.confirm("Удалить класс героя?", text, "Удалить", true, [cls](App& x) {
    if (x.act("Удалить класс героя", [&](Tx& tx) { rules::removeHeroClass(tx, cls); })) state(x).talent = 0;
  });
}

void askRemoveTalent(App& a, const World& w, Id talent) {
  const HeroClass* c = rules::talentClass(w, talent);
  const Talent* t = c ? c->talent(talent) : nullptr;
  if (!t) return;
  const size_t learned = learnedBy(w, talent).size();
  const size_t deps = rules::talentDependents(w, talent).size();
  std::string text = "Талант «" + orName(t->name) + "» будет удалён из дерева.";
  if (learned) text += " Его изучили " + nb(i64(learned), "герой", "героя", "героев") + " — у них он будет снят.";
  if (deps) text += " У " + nb(i64(deps), "зависимого таланта", "зависимых талантов", "зависимых талантов") + " снимется условие.";
  text += " Действие можно отменить Ctrl+Z.";
  a.confirm("Удалить талант?", text, "Удалить", true, [talent](App& x) {
    int lost = 0;
    if (x.act("Удалить талант", [&](Tx& tx) { lost = rules::removeTalent(tx, talent); })) {
      state(x).talent = 0;
      lostToast(x, lost);
    }
  });
}

// ---------------------------------------------------------------- список классов
void classList(App& a, State& st, const World& w, RectF L) {
  ui::Area area(L, 0);
  ui::Scroll sc("classes", L.h);
  const ui::Theme& th = ui::theme();
  int shown = 0;
  for (const HeroClass& c : w.catalogs->classes) {
    if (!st.query.empty() && !utf8::matches(c.name, st.query)) continue;
    shown++;
    ui::IdScope s{i64(c.id)};
    const bool sel = st.sel[kClasses] == c.id;
    const RectF r = ui::next(0, 44);
    ui::at(r);
    const size_t heroes = heroesOf(w, c.id).size();
    if (ui::listItem("##cls", {.subtitle = " ", .selected = sel, .tooltip = c.desc})) {
      st.sel[kClasses] = c.id;
      st.talent = 0;
    }
    if (sel && st.focusName == c.id) ui::scrollToItem();
    tile(RectF{r.x + 8, r.cy() - 15, 30, 30}, classIcon(c), c.color, 8);
    const float x = r.x + 48, right = r.right() - 8;
    const float lh = ui::lineHeight(ui::Font::Body), sh = ui::lineHeight(ui::Font::Small);
    const float y0 = std::round(r.cy() - (lh + sh) * 0.5f);
    ui::draw::text(orName(c.name), RectF{x, y0, right - x, lh}, sel ? ui::Font::Strong : ui::Font::Body, th.text);
    std::string sub = nb(i64(c.talents.size()), "талант", "таланта", "талантов");
    if (heroes) sub += " · " + nb(i64(heroes), "герой", "героя", "героев");
    ui::draw::text(sub, RectF{x, y0 + lh, right - x, sh}, ui::Font::Small, th.textMuted);
    a.markUi("catalogs.class." + std::to_string(c.id), r);
  }
  if (!shown) {
    ui::spacer(16);
    ui::label(st.query.empty() ? "Классов пока нет" : "Ничего не найдено", {.ink = ui::Ink::Muted, .align = ui::Align::Center});
  }
}

// ---------------------------------------------------------------- класс: шапка и дерево
void classHead(App& a, State& st, const World& w, const HeroClass& c) {
  const bool ro = a.readOnly();
  const Id cls = c.id;
  {
    ui::Row head({ui::px(52), ui::fr(1)}, 52, 12);
    tile(ui::next(52, 52), classIcon(c), c.color, 12);
    ui::Group g(0, 2);
    ui::caption("Класс героя");
    std::string name = c.name;
    if (st.focusName == cls) {
      ui::setKeyboardFocus(ui::id("clsname"));
      st.focusName = 0;
    }
    if (ui::textField("clsname", name, {.placeholder = "Название класса", .maxLength = 60, .readOnly = ro, .selectAllOnFocus = true}) && trim(name) != c.name) {
      const std::string n = trim(name);
      editClass(a, cls, "Переименовать класс", [&](HeroClass& x) { x.name = n; });
    }
    a.markUi("catalogs.classCard.name");
  }
  {
    ui::Row row({ui::px(34), ui::px(52), ui::fr(1, 150), ui::px(30)}, 30, 8);
    std::string icon = c.icon;
    if (iconGrid("clsicon", icon, "hero-class", ro, "Значок класса")) editClass(a, cls, "Значок класса", [&](HeroClass& x) { x.icon = icon; });
    a.markUi("catalogs.classCard.icon");
    Color col = c.color;
    {
      ui::Disabled d(ro);
      if (ui::colorButton("clscolor", col, {.tooltip = "Цвет класса", .hex = false}) && !ro)
        editClass(a, cls, "Цвет класса", [&](HeroClass& x) { x.color = col; }, "clscolor:" + std::to_string(cls));
    }
    a.markUi("catalogs.classCard.color");
    int tp = c.tierPoints;
    if (ui::numberField("tier", tp, {.min = 1, .max = rules::kMaxTierPoints, .label = "Очков на ярус", .steppers = true, .disabled = ro,
                                     .tooltip = "Ярус N открывается, когда в ярусах выше вложено (N − 1) × столько очков"}) &&
        tp != c.tierPoints)
      editClass(a, cls, "Очков на ярус", [&](HeroClass& x) { x.tierPoints = tp; }, "clstier:" + std::to_string(cls));
    a.markUi("catalogs.classCard.tier");
    if (ui::iconButton("trash", "Удалить класс", {.disabled = ro, .tone = ui::Tone::Danger})) askRemoveClass(a, w, cls);
    a.markUi("catalogs.classCard.delete");
  }
}

void treeBlock(App& a, State& st, const World& w, const HeroClass& c) {
  const bool ro = a.readOnly();
  const Id cls = c.id;
  talents::Options o;
  o.cls = &c;
  o.selected = st.talent;
  o.editable = !ro;
  o.mark = "catalogs.tree";
  const talents::Events ev = talents::tree(a, o);
  if (ev.newRow >= 0) {
    Id nid = 0;
    const int r = ev.newRow, col = ev.newCol;
    if (a.act("Новый талант", [&](Tx& tx) { nid = rules::addTalent(tx, cls, r, col, ""); }) && nid) {
      st.talent = nid;
      st.focusTalent = nid;
    }
  }
  if (ev.clicked) st.talent = ev.clicked;
  if (ev.moved) {
    int lost = 0;
    const Id t = ev.moved;
    const int r = ev.toRow, col = ev.toCol;
    if (a.act("Перенести талант", [&](Tx& tx) { lost = rules::moveTalent(tx, t, r, col); })) st.talent = t;
    lostToast(a, lost);
  }
  if (ev.rightClicked) {
    st.talent = ev.rightClicked;
    ui::openContextMenu("talentmenu");
  }
  if (ui::beginMenu("talentmenu")) {
    const Talent* t = c.talent(st.talent);
    if (t) {
      ui::menuHeader(orName(t->name));
      if (t->prereq && ui::menuItem("Снять условие", {.icon = "unlink", .disabled = ro})) {
        int lost = 0;
        const Id id = t->id;
        a.act("Условие таланта", [&](Tx& tx) { lost = rules::setTalentPrereq(tx, id, 0); });
        lostToast(a, lost);
      }
      if (ui::menuItem("Удалить талант", {.icon = "trash", .danger = true, .disabled = ro})) askRemoveTalent(a, w, t->id);
    }
    ui::endMenu();
  }
}

// ---------------------------------------------------------------- карточки
void heroChips(App& a, const World& w, const std::vector<Id>& ids, i64 salt) {
  ChipFlow cf;
  for (Id h : ids) {
    const Character* ch = w.character(h);
    if (!ch) continue;
    ui::IdScope s{i64(h) + salt};
    ui::ChipOpt co;
    co.icon = ch->hero ? "hero" : "character";
    co.color = w::factionColor(w, ch->faction);
    co.clickable = true;
    co.tooltip = "Открыть персонажа";
    if (edkit::chip(orName(ch->name, "Без имени") + " · " + std::to_string(ch->level), co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Character, h});
  }
}

void talentCard(App& a, State& st, const World& w, const HeroClass& c, const Talent& t) {
  const bool ro = a.readOnly();
  const Id id = t.id;
  {
    ui::Row head({ui::px(52), ui::fr(1)}, 52, 12);
    tile(ui::next(52, 52), talentIcon(t), c.color, 12, true);
    ui::Group g(0, 2);
    ui::caption("Талант · ярус " + std::to_string(t.row + 1) + " · столбец " + std::to_string(t.col + 1));
    ui::label(orName(t.name), {.font = ui::Font::Title});
  }
  {
    ui::prop("Название", "edit", 0.3f);
    std::string name = t.name;
    if (st.focusTalent == id) {
      ui::setKeyboardFocus(ui::id("tname"));
      st.focusTalent = 0;
    }
    if (ui::textField("tname", name, {.placeholder = "Название таланта", .maxLength = 60, .readOnly = ro, .selectAllOnFocus = true}) && trim(name) != t.name) {
      const std::string n = trim(name);
      editTalent(a, id, "Переименовать талант", [&](Talent& x) { x.name = n; });
    }
    a.markUi("catalogs.talent.name");
  }
  {
    ui::prop("Значок", "image", 0.3f);
    ui::HStack hs(30, ui::Align::Left, 8);
    std::string icon = t.icon;
    if (iconGrid("ticon", icon, "talent", ro, "Значок таланта")) editTalent(a, id, "Значок таланта", [&](Talent& x) { x.icon = icon; });
    a.markUi("catalogs.talent.icon");
    ui::label(iconTitle(t.icon), {.ink = ui::Ink::Dim});
  }
  {
    ui::prop("Стоимость", "star", 0.3f);
    int cost = t.cost;
    if (ui::numberField("tcost", cost, {.min = double(schema::kMinTalentCost), .max = double(schema::kMaxTalentCost), .unit = "очко|очка|очков",
                                        .steppers = true, .disabled = ro, .tooltip = "Очков талантов: от 1 до 5"}) &&
        cost != t.cost)
      editTalent(a, id, "Стоимость таланта", [&](Talent& x) { x.cost = cost; }, "tcost:" + std::to_string(id));
    a.markUi("catalogs.talent.cost");
  }
  {
    ui::prop("Условие", "link", 0.3f);
    std::vector<const Talent*> list;
    for (const Talent& x : c.talents)
      if (x.row < t.row) list.push_back(&x);
    std::sort(list.begin(), list.end(), [](const Talent* x, const Talent* y) { return x->row != y->row ? x->row < y->row : x->col < y->col; });
    std::vector<std::string> hints;
    for (const Talent* x : list) hints.push_back("ярус " + std::to_string(x->row + 1));
    std::vector<ui::Option> opts;
    for (size_t i = 0; i < list.size(); i++) opts.push_back(ui::Option{list[i]->name, talentIcon(*list[i]), c.color, hints[i]});
    int idx = -1;
    for (size_t i = 0; i < list.size(); i++)
      if (list[i]->id == t.prereq) idx = int(i);
    if (ui::combo("tprereq", idx, std::span<const ui::Option>(opts),
                  {.placeholder = list.empty() ? "Нет талантов выше" : "Без условия", .noneLabel = "Без условия", .search = 1, .icon = "link",
                   .disabled = ro || list.empty(), .tooltip = "Талант из яруса выше, который нужно изучить раньше"})) {
      const Id p = idx >= 0 && idx < int(list.size()) ? list[size_t(idx)]->id : 0;
      int lost = 0;
      a.act(p ? "Условие таланта" : "Снять условие таланта", [&](Tx& tx) { lost = rules::setTalentPrereq(tx, id, p); });
      lostToast(a, lost);
    }
    a.markUi("catalogs.talent.prereq");
  }
  ui::caption("Описание");
  {
    std::string desc = t.desc;
    if (ui::textArea("tdesc", desc, 72, {.placeholder = "Описание", .readOnly = ro}) && desc != t.desc)
      editTalent(a, id, "Описание таланта", [&](Talent& x) { x.desc = desc; });
    a.markUi("catalogs.talent.desc");
  }
  {
    ui::Section sec("Модификаторы героя", "sparkles", {.badge = t.modifiers.empty() ? std::string() : std::to_string(t.modifiers.size())});
    if (sec) {
      std::vector<Id> ids = t.modifiers;
      w::ModEdit ed;
      if (w::modifierList("talent.mods", ids, ro, w::ModScope::Hero, nullptr, &ed)) {
        int lost = 0;
        a.act("Модификаторы таланта", [&](Tx& tx) {
          if (!ed.addKey.empty()) ids.push_back(rules::ensureBuiltinMod(tx, ed.addKey));
          const HeroClass* cc = rules::talentClass(tx.w(), id);
          Talent x = *cc->talent(id);
          x.modifiers = ids;
          lost = rules::setTalent(tx, x);
        });
        lostToast(a, lost);
      }
      a.markUi("catalogs.talent.mods");
    }
  }
  const std::vector<Id> learned = learnedBy(w, id);
  if (!learned.empty()) {
    ui::Section sec("Изучили", "users", {.badge = std::to_string(learned.size())});
    if (sec) heroChips(a, w, learned, 0x7600000LL);
  }
  ui::spacer(4);
  {
    ui::Disabled dis(ro);
    if (ui::button("Удалить талант", {.variant = ui::Variant::Danger, .icon = "trash", .fill = true})) askRemoveTalent(a, w, id);
    a.markUi("catalogs.talent.delete");
  }
}

void classCard(App& a, const World& w, const HeroClass& c) {
  const bool ro = a.readOnly();
  const Id cls = c.id;
  const std::vector<Id> heroes = heroesOf(w, cls);
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(fmtInt(i64(c.talents.size())), "Таланты", {.icon = "talent", .tone = ui::Tone::Accent,
                                                         .tooltip = "Ярусов: " + std::to_string(rules::talentTiers(c))});
    ui::stat(fmtInt(i64(heroes.size())), "Герои", {.icon = "users", .tone = ui::Tone::Success});
  }
  ui::caption("Описание");
  {
    std::string desc = c.desc;
    if (ui::textArea("clsdesc", desc, 96, {.placeholder = "Описание класса", .readOnly = ro}) && desc != c.desc)
      editClass(a, cls, "Описание класса", [&](HeroClass& x) { x.desc = desc; });
    a.markUi("catalogs.classCard.desc");
  }
  ui::Section sec("Герои класса", "users", {.badge = std::to_string(heroes.size())});
  a.markUi("catalogs.classCard.heroes");
  if (!sec) return;
  if (heroes.empty()) ui::label("Нет героев этого класса", {.ink = ui::Ink::Muted});
  else heroChips(a, w, heroes, 0x7700000LL);
}

void sideCard(App& a, State& st, const World& w, const HeroClass& c) {
  if (const Talent* t = c.talent(st.talent)) talentCard(a, st, w, c, *t);
  else classCard(a, w, c);
}

}  // namespace

// ================================================================ вкладка
void drawClasses(App& a, State& st) {
  const bool ro = a.readOnly();
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    searchBox(a, st, 280, "Поиск классов");
    ui::flex();
    if (ui::button("Новый класс", {.variant = ui::Variant::Primary, .icon = "plus", .disabled = ro, .shortcut = {Key::Insert, 0}})) addClassAct(a, st);
    a.markUi("catalogs.add");
  }
  ui::spacer(2);
  const World w = a.world();   // снимок кадра (после кнопок: новая запись уже в нём)
  Id& sel = st.sel[kClasses];
  if (!w.heroClass(sel) || (!st.query.empty() && !utf8::matches(w.heroClass(sel)->name, st.query))) {
    sel = 0;
    for (const HeroClass& c : w.catalogs->classes)
      if (!sel && (st.query.empty() || utf8::matches(c.name, st.query))) sel = c.id;
    st.talent = 0;
  }
  const HeroClass* cur = w.heroClass(sel);
  if (cur && st.talent && !cur->talent(st.talent)) st.talent = 0;
  const ui::Theme& th = ui::theme();
  const RectF R = ui::avail();
  const float lw = R.w >= 1100 ? 248 : 208;
  const RectF L{R.x, R.y, lw, R.h};
  ui::draw::line(L.right() + 12, R.y, L.right() + 12, R.bottom(), th.border, 1);
  classList(a, st, w, L);
  a.markUi("catalogs.classes", L);
  const RectF M{L.right() + 25, R.y, std::max(0.f, R.right() - L.right() - 25), R.h};
  if (!cur) {
    ui::Area ma(M, 0);
    ui::spacer(std::max(0.f, M.h * 0.3f));
    if (ui::emptyState("hero-class", "Классов пока нет", ro ? std::string_view() : std::string_view("Новый класс"), "plus")) addClassAct(a, st);
    return;
  }
  const bool wide = M.w >= 760;
  const float cw = wide ? std::round(clamp(M.w * 0.36f, 330.f, 400.f)) : 0;
  const RectF T{M.x, M.y, wide ? M.w - cw - 25 : M.w, M.h};
  {
    ui::Area ta(T, 0);
    ui::Scroll sc("tree", T.h);
    classHead(a, st, w, *cur);
    ui::spacer(4);
    treeBlock(a, st, w, *cur);
    if (!wide) {   // узкое окно: карточка под деревом
      ui::separator();
      sideCard(a, st, w, *cur);
    }
  }
  if (wide) {
    const RectF C{T.right() + 25, M.y, cw, M.h};
    ui::draw::line(T.right() + 12, M.y, T.right() + 12, M.bottom(), th.border, 1);
    ui::Area ca(C, 0);
    ui::Scroll sc("card", C.h);
    sideCard(a, st, w, *cur);
    a.markUi("catalogs.classCard", C);
  }
  // Delete — удалить выбранный талант (вне текстовых полей).
  if (!ro && st.talent && ui::shortcut({Key::Delete, 0})) askRemoveTalent(a, w, st.talent);
}

}  // namespace rg::app::cat
