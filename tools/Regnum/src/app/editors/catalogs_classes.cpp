// Regnum — справочник «Классы героев» (ТЗ «Доработки №1», п.1): слева — список классов (значок цвета класса, число
// подклассов и героев); посередине — класс (название, значок, цвет, удаление), полоса подклассов (выбор, «+» — новый;
// число подклассов не ограничено), полоса подкласса (значок, название, очки на ярус, число героев, удаление с
// подтверждением) и его дерево талантов — доска 5 столбцов × ярусы (app/common/talent_tree.h: пороги ярусов 0, 10,
// 20…, финальный ярус ультимейтов выделен; щелчок по пустой ячейке — новый талант, перетаскивание — перенос, правый
// щелчок — меню); справа — карточка таланта (название, значок, финальный талант, ранги 1…5, очков за ранг, итог
// «до N очков», условие из яруса выше, описание, модификаторы героя, кто изучил и на каком ранге, удаление) или
// подкласса и класса (описания, финальные таланты, герои). В узком окне карточка — под деревом. Правка дерева, после
// которой ранги героев стали незаконными, снимает их — уведомление с числом героев. Правка — только администратору.
#include <algorithm>

#include "app/common/talent_tree.h"
#include "app/editors/catalogs_internal.h"
#include "gfx/icons.h"

namespace rg::app::cat {

using platform::Key;

namespace {

// ---------------------------------------------------------------- значки классов, подклассов и талантов
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
  return "Свой значок";
}

bool iconGrid(std::string_view id, std::string& icon, const char* fallback, bool disabled, std::string_view tip) {
  ui::IdScope scope(id);
  const bool none = icon.empty() || !gfx::hasIcon(icon);
  const std::string t = std::string(tip) + ": " + (none ? std::string("без значка") : std::string(iconTitle(icon)));
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
const char* subIcon(const HeroClass& c, const SubClass& s) { return !s.icon.empty() && gfx::hasIcon(s.icon) ? s.icon.c_str() : classIcon(c); }
const char* talentIcon(const Talent& t) { return !t.icon.empty() && gfx::hasIcon(t.icon) ? t.icon.c_str() : "talent"; }

std::string pts(int n) { return std::to_string(n) + " " + plural(n, "очко", "очка", "очков"); }

std::vector<Id> heroesWhere(const World& w, const std::function<bool(const Character&)>& f) {
  std::vector<Id> out;
  w.characters.each([&](const Character& c) {
    if (f(c)) out.push_back(c.id);
  });
  std::sort(out.begin(), out.end(), [&](Id x, Id y) { return compareRu(w.characterName(x), w.characterName(y)) < 0; });
  return out;
}
std::vector<Id> heroesOf(const World& w, Id cls) {
  return heroesWhere(w, [&](const Character& c) { return c.heroClass == cls; });
}
std::vector<Id> heroesOfSub(const World& w, Id sub) {
  return heroesWhere(w, [&](const Character& c) { return c.subclass == sub; });
}

// Таланты всех подклассов класса.
size_t talentsOf(const HeroClass& c) {
  size_t n = 0;
  for (const SubClass& s : c.subclasses) n += s.talents.size();
  return n;
}
// Наибольшее число очков, которое можно вложить в дерево.
int treePoints(const SubClass& s) {
  int n = 0;
  for (const Talent& t : s.talents) n += t.maxPoints();
  return n;
}

// Герои, изучившие талант, и их ранг.
std::vector<std::pair<Id, int>> learnedBy(const World& w, Id talent) {
  std::vector<std::pair<Id, int>> out;
  w.characters.each([&](const Character& c) {
    if (const int r = talentRank(c.talents, talent)) out.push_back({c.id, r});
  });
  std::sort(out.begin(), out.end(), [&](const auto& x, const auto& y) {
    return x.second != y.second ? x.second > y.second : compareRu(w.characterName(x.first), w.characterName(y.first)) < 0;
  });
  return out;
}

void lostToast(App& a, int lost) {
  if (lost > 0)
    a.toast("Таланты, которые больше нельзя изучить, сняты у " + nb(lost, "героя", "героев", "героев"), ToastKind::Info, "talent");
}

// Правка класса целиком (остальное — как в мире).
void editClass(App& a, Id cls, std::string_view label, const std::function<void(HeroClass&)>& fn, const std::string& coalesce = {}) {
  a.act(label, [&](Tx& tx) {
    const HeroClass* cur = tx.w().heroClass(cls);
    if (!cur) fail("Класс героя не найден");
    HeroClass c = *cur;
    fn(c);
    rules::setHeroClass(tx, c);
  }, {.coalesce = coalesce});
}

void editSub(App& a, Id sub, std::string_view label, const std::function<void(SubClass&)>& fn, const std::string& coalesce = {}) {
  int lost = 0;
  a.act(label, [&](Tx& tx) {
    const SubClass* cur = tx.w().catalogs->subclass(sub);
    if (!cur) fail("Подкласс героя не найден");
    SubClass s = *cur;
    fn(s);
    lost = rules::setSubClass(tx, s);
  }, {.coalesce = coalesce});
  lostToast(a, lost);
}

void editTalent(App& a, Id talent, std::string_view label, const std::function<void(Talent&)>& fn, const std::string& coalesce = {}) {
  int lost = 0;
  a.act(label, [&](Tx& tx) {
    const SubClass* c = rules::talentSubClass(tx.w(), talent);
    const Talent* cur = c ? c->talent(talent) : nullptr;
    if (!cur) fail("Талант не найден");
    Talent t = *cur;
    fn(t);
    lost = rules::setTalent(tx, t);
  }, {.coalesce = coalesce});
  lostToast(a, lost);
}

// Финальный талант: один ранг за 5 очков; снять — обычный талант в 1 очко за ранг.
void setUltimate(App& a, Id talent, bool on) {
  editTalent(a, talent, on ? "Финальный талант" : "Обычный талант", [&](Talent& x) {
    x.ranks = 1;
    x.cost = on ? schema::kMaxTalentCost : schema::kMinTalentCost;
  });
}

Id addClassAct(App& a, State& st) {
  Id nid = 0;
  if (!a.act("Новый класс героя", [&](Tx& tx) { nid = rules::addHeroClass(tx, ""); }) || !nid) return 0;
  st.query.clear();
  st.sel[kClasses] = nid;
  st.subclass = 0;
  st.talent = 0;
  st.focusName = nid;
  return nid;
}

Id addSubAct(App& a, State& st, Id cls) {
  Id nid = 0;
  if (!a.act("Новый подкласс героя", [&](Tx& tx) { nid = rules::addSubClass(tx, cls, ""); }) || !nid) return 0;
  st.subclass = nid;
  st.talent = 0;
  st.focusSub = nid;
  return nid;
}

void askRemoveSub(App& a, const World& w, Id sub) {
  const SubClass* s = w.catalogs->subclass(sub);
  if (!s) return;
  const size_t heroes = heroesOfSub(w, sub).size();
  std::string text = "Подкласс «" + orName(s->name) + "» и его дерево (" + nb(i64(s->talents.size()), "талант", "таланта", "талантов") + ") будут удалены.";
  if (heroes) text += " " + nb(i64(heroes), "герой потеряет", "героя потеряют", "героев потеряют") + " подкласс и таланты.";
  a.confirmAct("Удалить подкласс?", text, "Удалить", "Удалить подкласс героя", [sub](Tx& tx) { rules::removeSubClass(tx, sub); }, [](App& x) {
    state(x).subclass = 0;
    state(x).talent = 0;
  });
}

void askRemoveClass(App& a, const World& w, Id cls) {
  const HeroClass* c = w.heroClass(cls);
  if (!c) return;
  const size_t heroes = heroesOf(w, cls).size();
  std::string text = "Класс «" + orName(c->name) + "», его " + nb(i64(c->subclasses.size()), "подкласс", "подкласса", "подклассов") + " и " +
                     nb(i64(talentsOf(*c)), "талант", "таланта", "талантов") + " будут удалены.";
  if (heroes) text += " " + nb(i64(heroes), "герой потеряет", "героя потеряют", "героев потеряют") + " класс и таланты.";
  a.confirmAct("Удалить класс героя?", text, "Удалить", "Удалить класс героя", [cls](Tx& tx) { rules::removeHeroClass(tx, cls); }, [](App& x) {
    state(x).subclass = 0;
    state(x).talent = 0;
  });
}

void askRemoveTalent(App& a, const World& w, Id talent) {
  const SubClass* c = rules::talentSubClass(w, talent);
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
      st.subclass = 0;
      st.talent = 0;
    }
    if (sel && st.focusName == c.id) ui::scrollToItem();
    tile(RectF{r.x + 8, r.cy() - 15, 30, 30}, classIcon(c), c.color, 8);
    const float x = r.x + 48, right = r.right() - 8;
    const float lh = ui::lineHeight(ui::Font::Body), sh = ui::lineHeight(ui::Font::Small);
    const float y0 = std::round(r.cy() - (lh + sh) * 0.5f);
    ui::draw::text(orName(c.name), RectF{x, y0, right - x, lh}, sel ? ui::Font::Strong : ui::Font::Body, th.text);
    std::string sub = nb(i64(c.subclasses.size()), "подкласс", "подкласса", "подклассов");
    if (heroes) sub += " · " + nb(i64(heroes), "герой", "героя", "героев");
    ui::draw::text(sub, RectF{x, y0 + lh, right - x, sh}, ui::Font::Small, th.textMuted);
    a.markUi("catalogs.class." + std::to_string(c.id), r);
  }
  if (!shown) {
    ui::spacer(16);
    ui::label(st.query.empty() ? "Классов пока нет" : "Ничего не найдено", {.ink = ui::Ink::Muted, .align = ui::Align::Center});
  }
}

// ---------------------------------------------------------------- класс: шапка, полоса подклассов
void classHead(App& a, State& st, const World& w, const HeroClass& c, bool ro) {
  const Id cls = c.id;
  {
    ui::Row head({ui::px(52), ui::fr(1)}, 52, 12);
    tile(ui::next(52, 52), classIcon(c), c.color, 12, true);
    ui::Group g(0, 2);
    ui::caption("Класс героя · " + nb(i64(talentsOf(c)), "талант", "таланта", "талантов"));
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
    ui::HStack row(30, ui::Align::Left, 8);
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
    ui::flex();
    if (ui::iconButton("trash", "Удалить класс", {.disabled = ro, .tone = ui::Tone::Danger})) askRemoveClass(a, w, cls);
    a.markUi("catalogs.classCard.delete");
  }
  // Полоса подклассов: выбор, «+» — новый подкласс.
  ui::spacer(4);
  ui::caption("Подклассы · " + std::to_string(c.subclasses.size()));
  {
    ui::ChipFlow cf;
    for (const SubClass& s : c.subclasses) {
      ui::IdScope sc{i64(s.id)};
      const size_t heroes = heroesOfSub(w, s.id).size();
      std::string tip = nb(i64(s.talents.size()), "талант", "таланта", "талантов") + " · " +
                        nb(rules::talentTiers(s), "ярус", "яруса", "ярусов");
      if (heroes) tip += " · " + nb(i64(heroes), "герой", "героя", "героев");
      if (!s.desc.empty()) tip += "\n" + s.desc;
      ui::ChipOpt co;
      co.icon = subIcon(c, s);
      co.clickable = true;
      co.selected = st.subclass == s.id;
      co.tooltip = tip;
      if (ui::flowChip(orName(s.name) + (heroes ? " · " + std::to_string(heroes) : std::string()), co) == ui::ChipAction::Click) {
        st.subclass = s.id;
        st.talent = 0;
      }
      a.markUi("catalogs.subclass." + std::to_string(s.id));
    }
    if (!ro) {
      ui::ChipOpt co;
      co.icon = "plus";
      co.clickable = true;
      co.tone = ui::Tone::Accent;
      co.tooltip = "Новый подкласс";
      if (ui::flowChip("Подкласс", co) == ui::ChipAction::Click) addSubAct(a, st, cls);
      a.markUi("catalogs.subclass.add");
    }
  }
}

// Полоса подкласса: значок, название, очки на ярус, герои, удаление.
void subBar(App& a, State& st, const World& w, const HeroClass& c, const SubClass& s, bool ro) {
  const Id sub = s.id;
  const size_t heroes = heroesOfSub(w, sub).size();
  ui::Card card({.pad = 8});
  ui::Row row({ui::px(30), ui::fr(1, 140), ui::px(150), ui::px(50), ui::px(30)}, 30, 8);
  std::string icon = s.icon;
  if (iconGrid("subicon", icon, classIcon(c), ro, "Значок подкласса")) editSub(a, sub, "Значок подкласса", [&](SubClass& x) { x.icon = icon; });
  a.markUi("catalogs.subCard.icon");
  std::string name = s.name;
  if (st.focusSub == sub) {
    ui::setKeyboardFocus(ui::id("subname"));
    st.focusSub = 0;
  }
  if (ui::textField("subname", name, {.placeholder = "Название подкласса", .maxLength = 60, .readOnly = ro, .selectAllOnFocus = true}) &&
      trim(name) != s.name) {
    const std::string n = trim(name);
    editSub(a, sub, "Переименовать подкласс", [&](SubClass& x) { x.name = n; });
  }
  a.markUi("catalogs.subCard.name");
  int tp = s.tierPoints;
  if (ui::numberField("tier", tp, {.min = 1, .max = rules::kMaxTierPoints, .label = "На ярус", .steppers = true, .disabled = ro,
                                   .tooltip = "Очков на ярус: ярус N открывается, когда в ярусах выше вложено (N − 1) × столько очков"}) &&
      tp != s.tierPoints)
    editSub(a, sub, "Очков на ярус", [&](SubClass& x) { x.tierPoints = tp; }, "subtier:" + std::to_string(sub));
  a.markUi("catalogs.subCard.tier");
  {
    const RectF r = ui::next(50, 30);
    const ui::Theme& th = ui::theme();
    const std::string n = std::to_string(heroes);
    ui::draw::icon("users", RectF{r.x + 4, r.cy() - 8, 16, 16}, heroes ? th.success : th.textMuted);
    ui::draw::text(n, RectF{r.x + 24, r.y, r.w - 24, r.h}, ui::Font::Strong, heroes ? th.text : th.textMuted);
    ui::hoverTip("##subheroes", r, "Героев подкласса: " + n);
    a.markUi("catalogs.subCard.heroes", r);
  }
  if (ui::iconButton("trash", "Удалить подкласс", {.disabled = ro, .tone = ui::Tone::Danger})) askRemoveSub(a, w, sub);
  a.markUi("catalogs.subCard.delete");
}

void treeBlock(App& a, State& st, const World& w, const HeroClass& c, const SubClass& sc, bool ro) {
  const Id sub = sc.id;
  talents::Options o;
  o.cls = &c;
  o.sub = &sc;
  o.selected = st.talent;
  o.editable = !ro;
  o.mark = "catalogs.tree";
  const talents::Events ev = talents::tree(a, o);
  if (ev.newRow >= 0) {
    Id nid = 0;
    const int r = ev.newRow, col = ev.newCol;
    if (a.act("Новый талант", [&](Tx& tx) { nid = rules::addTalent(tx, sub, r, col, ""); }) && nid) {
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
    if (const Talent* t = sc.talent(st.talent)) {
      const Id id = t->id;
      ui::menuHeader(orName(t->name));
      if (ui::menuItem("Финальный талант", {.icon = "crown", .checked = talents::isUltimate(*t), .disabled = ro}))
        setUltimate(a, id, !talents::isUltimate(*t));
      if (t->prereq && ui::menuItem("Снять условие", {.icon = "unlink", .disabled = ro})) {
        int lost = 0;
        a.act("Снять условие таланта", [&](Tx& tx) { lost = rules::setTalentPrereq(tx, id, 0); });
        lostToast(a, lost);
      }
      ui::menuSeparator();
      if (ui::menuItem("Удалить талант", {.icon = "trash", .shortcut = {Key::Delete, 0}, .danger = true, .disabled = ro})) askRemoveTalent(a, w, id);
    }
    ui::endMenu();
  }
}

// ---------------------------------------------------------------- карточки
void heroChips(App& a, const World& w, const std::vector<Id>& ids, i64 salt, const std::function<std::string(Id)>& suffix = {}) {
  ui::ChipFlow cf;
  for (Id h : ids) {
    const Character* ch = w.character(h);
    if (!ch) continue;
    ui::IdScope s{i64(h) + salt};
    ui::ChipOpt co;
    co.icon = ch->hero ? "hero" : "character";
    co.color = w::factionColor(w, ch->faction);
    co.clickable = true;
    const std::string tip = (ch->faction ? w.factionName(ch->faction) + " · " : std::string()) + "уровень " + std::to_string(ch->level);
    co.tooltip = tip;
    const std::string label = orName(ch->name, "Без имени") + (suffix ? " · " + suffix(h) : std::string());
    if (ui::flowChip(label, co) == ui::ChipAction::Click) edkit::goTo(a, {SelType::Character, h});
  }
}

void talentCard(App& a, State& st, const World& w, const HeroClass& c, const SubClass& sc, const Talent& t, bool ro) {
  const Id id = t.id;
  const bool ult = talents::isUltimate(t);
  {
    ui::Row head({ui::px(52), ui::fr(1), ui::px(30)}, 52, 12);
    tile(ui::next(52, 52), talentIcon(t), ult ? ui::theme().accent : c.color, 12, true);
    {
      ui::Group g(0, 2);
      ui::caption((ult ? "Финальный талант" : "Талант") + std::string(" · ярус ") + std::to_string(t.row + 1) + " · столбец " + std::to_string(t.col + 1));
      ui::label(orName(t.name), {.font = ui::Font::Title, .tooltip = orName(sc.name) + " · " + orName(c.name)});
    }
    if (ui::iconButton("close", "Закрыть карточку таланта")) st.talent = 0;
    a.markUi("catalogs.talent.close");
  }
  {
    ui::prop("Название", "edit", 0.32f);
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
    ui::prop("Значок", "image", 0.32f);
    ui::HStack hs(30, ui::Align::Left, 8);
    std::string icon = t.icon;
    if (iconGrid("ticon", icon, "talent", ro, "Значок таланта")) editTalent(a, id, "Значок таланта", [&](Talent& x) { x.icon = icon; });
    a.markUi("catalogs.talent.icon");
    ui::label(iconTitle(t.icon), {.ink = ui::Ink::Dim});
  }
  {
    bool on = ult;
    if (ui::toggle("Финальный талант", on, ro) && on != ult) setUltimate(a, id, on);
    ui::tooltip("Один ранг за " + pts(schema::kMaxTalentCost) + ": ультимейт финального яруса");
    a.markUi("catalogs.talent.ultimate");
  }
  {
    ui::Disabled dis(ult);
    ui::prop("Ранги", "talent", 0.32f);
    int ranks = t.ranks;
    if (ui::numberField("tranks", ranks, {.min = 1, .max = double(schema::kMaxTalentRanks), .steppers = true, .disabled = ro,
                                          .tooltip = "Рангов таланта: от 1 до 5, каждый изучается отдельно"}) &&
        ranks != t.ranks)
      editTalent(a, id, "Ранги таланта", [&](Talent& x) { x.ranks = ranks; }, "tranks:" + std::to_string(id));
    a.markUi("catalogs.talent.ranks");
    ui::prop("За ранг", "star", 0.32f);
    int cost = t.cost;
    if (ui::numberField("tcost", cost, {.min = double(schema::kMinTalentCost), .max = double(schema::kMaxTalentCost), .unit = "очко|очка|очков",
                                        .steppers = true, .disabled = ro, .tooltip = "Очков талантов за ранг: от 1 до 5"}) &&
        cost != t.cost)
      editTalent(a, id, "Стоимость таланта", [&](Talent& x) { x.cost = cost; }, "tcost:" + std::to_string(id));
    a.markUi("catalogs.talent.cost");
  }
  {
    ui::prop("Всего", "coins", 0.32f);
    const int total = t.maxPoints();
    ui::label((t.ranks > 1 ? "до " : "") + pts(total) + (t.ranks > 1 ? " · " + talents::ranksText(t) : std::string()),
              {.font = ui::Font::Strong, .ink = ult ? ui::Ink::Accent : ui::Ink::Normal});
    a.markUi("catalogs.talent.total");
  }
  {
    ui::prop("Условие", "link", 0.32f);
    std::vector<const Talent*> list;
    for (const Talent& x : sc.talents)
      if (x.row < t.row) list.push_back(&x);
    std::sort(list.begin(), list.end(), [](const Talent* x, const Talent* y) { return x->row != y->row ? x->row < y->row : x->col < y->col; });
    std::vector<std::string> hints;
    for (const Talent* x : list) hints.push_back("ярус " + std::to_string(x->row + 1));
    std::vector<ui::Option> opts;
    for (size_t i = 0; i < list.size(); i++) opts.push_back(ui::Option{list[i]->name, talentIcon(*list[i]), Color(0, 0, 0, 0), hints[i]});
    int idx = -1;
    for (size_t i = 0; i < list.size(); i++)
      if (list[i]->id == t.prereq) idx = int(i);
    if (ui::combo("tprereq", idx, std::span<const ui::Option>(opts),
                  {.placeholder = list.empty() ? "Нет талантов выше" : "Без условия", .noneLabel = "Без условия", .search = 1, .icon = "link",
                   .disabled = ro || list.empty(), .tooltip = "Талант из яруса выше, который нужно изучить полностью раньше"})) {
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
    if (ui::textArea("tdesc", desc, 72, {.placeholder = "Описание таланта", .readOnly = ro}) && desc != t.desc)
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
          const SubClass* cc = rules::talentSubClass(tx.w(), id);
          if (!cc || !cc->talent(id)) fail("Талант не найден");
          Talent x = *cc->talent(id);
          x.modifiers = ids;
          lost = rules::setTalent(tx, x);
        });
        lostToast(a, lost);
      }
      a.markUi("catalogs.talent.mods");
    }
  }
  const std::vector<std::pair<Id, int>> learned = learnedBy(w, id);
  {
    ui::Section sec("Изучили", "users", {.badge = std::to_string(learned.size())});
    a.markUi("catalogs.talent.learned");
    if (sec) {
      if (learned.empty()) {
        ui::label("Пока никто", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      } else {
        std::vector<Id> ids;
        std::unordered_map<Id, int> rk;
        for (auto [h, r] : learned) {
          ids.push_back(h);
          rk[h] = r;
        }
        heroChips(a, w, ids, 0x7600000LL, [&](Id h) { return ult ? std::string("изучен") : std::to_string(rk[h]) + "/" + std::to_string(t.ranks); });
      }
    }
  }
  ui::spacer(4);
  {
    ui::Disabled dis(ro);
    if (ui::button("Удалить талант", {.variant = ui::Variant::Danger, .icon = "trash", .fill = true})) askRemoveTalent(a, w, id);
    ui::tooltip("Удалить талант из дерева", {Key::Delete, 0});
    a.markUi("catalogs.talent.delete");
  }
}

// Подкласс: шапка, число талантов, ярусов и героев, описание, финальные таланты, герои подкласса.
void subCard(App& a, State& st, const World& w, const HeroClass& c, const SubClass& s, bool ro) {
  const Id sub = s.id;
  const std::vector<Id> heroes = heroesOfSub(w, sub);
  cardHead(subIcon(c, s), c.color, "Подкласс · " + orName(c.name), orName(s.name));
  {
    // Сводка строкой: таланты (до N очков), ярусы (очков на ярус), герои.
    ui::HStack hs(22, ui::Align::Left, 14);
    ui::label(nb(i64(s.talents.size()), "талант", "таланта", "талантов"),
              {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "talent", .tooltip = "До " + pts(treePoints(s)) + " во всём дереве"});
    ui::label(nb(rules::talentTiers(s), "ярус", "яруса", "ярусов"),
              {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "layers", .tooltip = "По " + pts(s.tierPoints) + " на ярус"});
    ui::label(nb(i64(heroes.size()), "герой", "героя", "героев"), {.font = ui::Font::Small, .ink = heroes.empty() ? ui::Ink::Muted : ui::Ink::Success, .icon = "users"});
    a.markUi("catalogs.subCard.stats");
  }
  ui::caption("Описание подкласса");
  {
    std::string desc = s.desc;
    if (ui::textArea("subdesc", desc, 72, {.placeholder = "Описание подкласса", .readOnly = ro}) && desc != s.desc)
      editSub(a, sub, "Описание подкласса", [&](SubClass& x) { x.desc = desc; });
    a.markUi("catalogs.subCard.desc");
  }
  std::vector<const Talent*> ults;
  for (const Talent& t : s.talents)
    if (talents::isUltimate(t)) ults.push_back(&t);
  std::sort(ults.begin(), ults.end(), [](const Talent* x, const Talent* y) { return x->row != y->row ? x->row < y->row : x->col < y->col; });
  if (!ults.empty()) {
    ui::Section sec("Финальные таланты", "crown", {.badge = std::to_string(ults.size())});
    if (sec) {
      ui::ChipFlow cf;
      for (const Talent* t : ults) {
        ui::IdScope is{i64(t->id)};
        ui::ChipOpt co;
        co.icon = talentIcon(*t);
        co.clickable = true;
        co.glow = ui::theme().accent;
        const std::string tip = "Ярус " + std::to_string(t->row + 1) + " · " + pts(t->cost);
        co.tooltip = tip;
        if (ui::flowChip(orName(t->name), co) == ui::ChipAction::Click) st.talent = t->id;
      }
    }
  }
  ui::Section sec("Герои подкласса", "users", {.badge = std::to_string(heroes.size())});
  a.markUi("catalogs.subCard.heroList");
  if (!sec) return;
  if (heroes.empty()) ui::label("Нет героев этого подкласса", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  else heroChips(a, w, heroes, 0x7500000LL, [&](Id h) { return std::to_string(rules::talentPointsSpent(w, h)) + "/" + std::to_string(rules::talentPoints(w, h)); });
}

// Класс: описание и герои класса (без подкласса — с пометкой).
void classCard(App& a, const World& w, const HeroClass& c, bool ro, bool head) {
  const Id cls = c.id;
  const std::vector<Id> heroes = heroesOf(w, cls);
  if (head) {
    cardHead(classIcon(c), c.color, "Класс героя", orName(c.name));
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(fmtInt(i64(c.subclasses.size())), "Подклассы", {.icon = "hero-class", .tone = ui::Tone::Accent,
                                                              .tooltip = "Талантов: " + std::to_string(talentsOf(c))});
    ui::stat(fmtInt(i64(heroes.size())), "Герои", {.icon = "users", .tone = ui::Tone::Success});
  }
  ui::caption("Описание класса");
  {
    std::string desc = c.desc;
    if (ui::textArea("clsdesc", desc, 72, {.placeholder = "Описание класса", .readOnly = ro}) && desc != c.desc)
      editClass(a, cls, "Описание класса", [&](HeroClass& x) { x.desc = desc; });
    a.markUi("catalogs.classCard.desc");
  }
  ui::Section sec("Герои класса", "users", {.defaultOpen = head, .badge = std::to_string(heroes.size())});
  a.markUi("catalogs.classCard.heroes");
  if (!sec) return;
  if (heroes.empty()) {
    ui::label("Нет героев этого класса", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    return;
  }
  heroChips(a, w, heroes, 0x7700000LL, [&](Id h) {
    const Character* ch = w.character(h);
    const SubClass* s = ch ? c.subclass(ch->subclass) : nullptr;
    return s ? orName(s->name) : std::string("без подкласса");
  });
}

// Узкое окно: класс — выпадающим списком вместо списка слева.
void classCombo(App& a, State& st, const World& w) {
  const RectF r = ui::next(300, 30);
  ui::at(r);
  std::vector<std::string> hints;
  for (const HeroClass& c : w.catalogs->classes) hints.push_back(std::to_string(c.subclasses.size()));
  std::vector<ui::Option> opts;
  int idx = -1;
  for (size_t i = 0; i < w.catalogs->classes.size(); i++) {
    const HeroClass& c = w.catalogs->classes[i];
    opts.push_back(ui::Option{c.name, classIcon(c), Color(0, 0, 0, 0), hints[i]});
    if (c.id == st.sel[kClasses]) idx = int(i);
  }
  if (ui::combo("clspick", idx, std::span<const ui::Option>(opts), {.placeholder = "Класс героя", .search = 1, .icon = "hero-class", .popupWidth = 300,
                                                                    .tooltip = "Класс героя"}) &&
      idx >= 0 && idx < int(opts.size())) {
    st.sel[kClasses] = w.catalogs->classes[size_t(idx)].id;
    st.subclass = 0;
    st.talent = 0;
  }
  a.markUi("catalogs.classPick", r);
}

void sideCard(App& a, State& st, const World& w, const HeroClass& c, bool ro) {
  const SubClass* sc = c.subclass(st.subclass);
  if (const Talent* t = sc ? sc->talent(st.talent) : nullptr) {
    talentCard(a, st, w, c, *sc, *t, ro);
    return;
  }
  if (sc) {
    subCard(a, st, w, c, *sc, ro);
    ui::spacer(8);
    ui::separator();
    classCard(a, w, c, ro, false);
  } else {
    classCard(a, w, c, ro, true);
  }
}

}  // namespace

// ================================================================ вкладка
void drawClasses(App& a, State& st) {
  const bool ro = !a.canEdit();
  // Список классов слева, пока окно широкое; в узком — выпадающий список в строке кнопок (дереву нужна ширина).
  const bool side = ui::avail().w >= 1000;
  if (!side) st.query.clear();
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    if (side) searchBox(a, st, 280, "Поиск классов");
    else classCombo(a, st, a.world());
    ui::flex();
    ui::Disabled d(ro);
    if (newButton(a, "Новый класс")) addClassAct(a, st);
  }
  ui::spacer(2);
  const World w = a.world();   // снимок кадра (после кнопок: новая запись уже в нём)
  Id& sel = st.sel[kClasses];
  if (!w.heroClass(sel) || (!st.query.empty() && !utf8::matches(w.heroClass(sel)->name, st.query))) {
    sel = 0;
    for (const HeroClass& c : w.catalogs->classes)
      if (!sel && (st.query.empty() || utf8::matches(c.name, st.query))) sel = c.id;
    st.subclass = 0;
    st.talent = 0;
  }
  const HeroClass* cur = w.heroClass(sel);
  if (cur && !cur->subclass(st.subclass)) {
    st.subclass = cur->subclasses.empty() ? 0 : cur->subclasses.front().id;
    st.talent = 0;
  }
  const SubClass* curSub = cur ? cur->subclass(st.subclass) : nullptr;
  if (st.talent && (!curSub || !curSub->talent(st.talent))) st.talent = 0;
  const ui::Theme& th = ui::theme();
  const RectF R = ui::avail();
  RectF M = R;
  if (side) {
    const float lw = R.w >= 1200 ? 240 : 210;
    const RectF L{R.x, R.y, lw, R.h};
    ui::draw::line(L.right() + 12, R.y, L.right() + 12, R.bottom(), th.border, 1);
    classList(a, st, w, L);
    a.markUi("catalogs.classes", L);
    M = RectF{L.right() + 25, R.y, std::max(0.f, R.right() - L.right() - 25), R.h};
  }
  if (!cur) {
    ui::Area ma(M, 0);
    ui::spacer(std::max(0.f, M.h * 0.3f));
    if (ui::emptyState("hero-class", "Классов пока нет", ro ? std::string_view() : std::string_view("Новый класс"), "plus")) addClassAct(a, st);
    return;
  }
  // Карточка справа, пока дереву хватает места на доску из пяти столбцов; иначе — под деревом.
  const float cw = std::round(clamp(M.w * 0.34f, 300.f, 400.f));
  const bool wide = M.w - cw - 25 >= std::max(talents::minTreeWidth(), 440.f);
  const RectF T{M.x, M.y, wide ? M.w - cw - 25 : M.w, M.h};
  {
    ui::Area ta(T, 0);
    ui::Scroll sc("tree", T.h);
    classHead(a, st, w, *cur, ro);
    ui::spacer(6);
    if (curSub) {
      subBar(a, st, w, *cur, *curSub, ro);
      ui::spacer(6);
      treeBlock(a, st, w, *cur, *curSub, ro);
    } else {
      ui::spacer(16);
      if (ui::emptyState("talent", "Подклассов пока нет", ro ? std::string_view() : std::string_view("Новый подкласс"), "plus")) addSubAct(a, st, cur->id);
    }
    if (!wide) {   // узкое окно: карточка под деревом; выбран другой талант — прокрутка к карточке
      struct Seen {
        Id talent = 0;
      };
      Seen& seen = ui::state<Seen>(ui::id("##seen"));
      ui::spacer(8);
      ui::separator();
      const float cardY = ui::avail().y - T.y + sc.offset();
      sideCard(a, st, w, *cur, ro);
      a.markUi("catalogs.classCard", RectF{T.x, T.y, T.w, T.h});
      if (st.talent && st.talent != seen.talent) sc.scrollTo(std::max(0.f, cardY - 8));
      seen.talent = st.talent;
    }
  }
  if (wide) {
    const RectF C{T.right() + 25, M.y, cw, M.h};
    ui::draw::line(T.right() + 12, M.y, T.right() + 12, M.bottom(), th.border, 1);
    ui::Area ca(C, 0);
    ui::Scroll sc("card", C.h);
    sideCard(a, st, w, *cur, ro);
    a.markUi("catalogs.classCard", C);
  }
  // Delete — удалить выбранный талант (вне текстовых полей).
  if (!ro && st.talent && ui::shortcut({Key::Delete, 0})) askRemoveTalent(a, w, st.talent);
}

}  // namespace rg::app::cat
