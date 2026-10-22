// Regnum — окно модификаторов (ТЗ 1.g, ТЗ «Модификаторы»): список своих и встроенных модификаторов с поиском и
// фильтром, сгруппированный по виду (для провинций, глобальные, для армий, для героев, везде; группы сворачиваются,
// поиск и фильтр их раскрывают), создание, копия, удаление своих с предупреждением о местах использования; карточка модификатора
// (название, значок, цвет, описание, где действует, срок по умолчанию), все эффекты schema::kEffects в пределах ТЗ
// (локальные, глобальные, эффекты войск), цели дипломатии, живой предпросмотр и «Где используется» со ссылками.
// Встроенные (schema::builtinModifiers, 1.1–1.26) видны всегда: пока записи мира нет, показывается шаблон, первая
// правка создаёт запись (rules::ensureBuiltinMod); встроенные не удаляются, автоматические помечены.
// Быстрый доступ — раздел «Справочники» правой ленты и команда палитры (Ctrl+8). Части окна — modifiers_internal.h.
#include "app/editors/modifiers_internal.h"

namespace rg::app {
namespace modedit {

namespace {

const char* modIcon(const Modifier& m) { return m.icon.empty() || !gfx::hasIcon(m.icon) ? "sparkles" : m.icon.c_str(); }

// ---------------------------------------------------------------- встроенные модификаторы
// Выбор шаблона встроенного модификатора, у которого ещё нет записи мира: kTplBase + номер в builtinModifiers().
constexpr Id kTplBase = 0xFFFF0000u;
int tplIndex(Id id) {
  return id >= kTplBase && size_t(id - kTplBase) < schema::builtinModifiers().size() ? int(id - kTplBase) : -1;
}
}  // namespace

bool isAuto(const Modifier& m) { return !m.key.empty() && schema::isAutoKey(m.key); }

namespace {

bool isBuiltin(const Modifier& m) { return !m.key.empty() && schema::builtinModifier(m.key) != nullptr; }

// ---------------------------------------------------------------- группы списка по виду модификатора
// Список сгруппирован по тому, где модификатор действует (ModKind); группы сворачиваются, поиск и фильтр их раскрывают.
struct KindGroup {
  ModKind kind;
  const char* title;
  const char* icon;
  const char* mark;   // метка строки группы: modifiers.group.<mark>
};
const KindGroup kKindGroups[] = {
    {ModKind::Province, "Для провинций", "province", "province"},
    {ModKind::Faction, "Глобальные", "crown", "faction"},
    {ModKind::Army, "Для армий", "army", "army"},
    {ModKind::Hero, "Для героев", "hero", "hero"},
    {ModKind::ArchGroup, "Для археологических групп", "pickaxe", "arch"},
    {ModKind::Any, "Везде", "sparkles", "any"},
};
constexpr int kKindGroupCount = int(std::size(kKindGroups));
int groupIndex(ModKind k) {
  for (int i = 0; i < kKindGroupCount; i++)
    if (kKindGroups[i].kind == k) return i;
  return kKindGroupCount - 1;
}

// Генерация за ход (ТЗ «Доработки №1», п.5): виды «Везде», «Для провинций», «Глобальный», «Для героев».
bool genKind(ModKind k) { return k == ModKind::Any || k == ModKind::Province || k == ModKind::Faction || k == ModKind::Hero; }

// Свёрнутые группы списка: состояние окна, пока открыт тот же мир (другой мир — все раскрыты).
struct ClosedGroups {
  std::array<bool, kKindGroupCount> v{};
};
std::array<bool, kKindGroupCount>& closedGroups(App&) { return sessionState<ClosedGroups>().v; }

// ---------------------------------------------------------------- строки списка
// Модификатор строки (копия на кадр: правка посреди кадра заменяет мир).
struct Entry {
  Id id = 0;            // запись мира или kTplBase + номер шаблона
  Modifier m;           // у шаблона m.id = 0
  bool builtin = false;
  int uses = 0;         // мест использования; у автоматических — где действует сейчас
};

// Свои модификаторы по названию, затем встроенные в порядке ТЗ (запись мира или шаблон).
void collect(const World& w, const std::unordered_map<Id, int>& uses, const std::map<std::string, std::vector<Id>>* autos,
             std::vector<Entry>& own, std::vector<Entry>& built) {
  auto count = [&](Id id) {
    auto it = uses.find(id);
    return it == uses.end() ? 0 : it->second;
  };
  const auto& tpl = schema::builtinModifiers();
  std::vector<Id> canonical;
  for (size_t i = 0; i < tpl.size(); i++) {
    Entry e;
    e.builtin = true;
    if (Id rid = rules::builtinModId(w, tpl[i].key)) {
      e.id = rid;
      e.m = *w.modifier(rid);
      canonical.push_back(rid);
    } else {
      e.id = kTplBase + Id(i);
      e.m = tpl[i];
    }
    e.uses = count(e.m.id);
    if (autos && isAuto(e.m))
      if (auto it = autos->find(e.m.key); it != autos->end()) e.uses = int(it->second.size());
    built.push_back(std::move(e));
  }
  w.modifiers.each([&](const Modifier& m) {
    if (has(canonical, m.id)) return;
    Entry e;
    e.id = m.id;
    e.m = m;
    e.uses = count(m.id);
    own.push_back(std::move(e));
  });
  std::sort(own.begin(), own.end(), [](const Entry& a, const Entry& b) {
    int c = compareRu(a.m.name, b.m.name);
    return c != 0 ? c < 0 : a.id < b.id;
  });
}

// ---------------------------------------------------------------- состояние окна
struct EdState {
  std::string query;
  int filter = 0;          // 0 — все, 1 — с локальными, 2 — с глобальными, 3 — нигде не используются
  bool focusName = false;
  bool focusSearch = false;
  std::string seenQuery;   // запрос и фильтр прошлого кадра (смена — выделение к первому найденному)
  int seenFilter = 0;
  Id seenSel = 0;          // выделение прошлого кадра (новое — прокрутить список к строке)
  int seenGroup = -1;      // группа выделенного прошлого кадра (новая — раскрыть её)
  std::string selKey;      // выделен встроенный: его ключ (запись отменили Ctrl+Z — выделение остаётся на шаблоне)
};

bool passes(const Entry& e, const EdState& st) {
  const Modifier& m = e.m;
  if (!st.query.empty() && !utf8::matches(m.name, st.query) && !utf8::matches(m.desc, st.query)) return false;
  switch (st.filter) {
    case 1: return hasLocal(m);
    case 2: return hasGlobal(m);
    case 3: return e.uses == 0;
    default: return true;
  }
}

// ---------------------------------------------------------------- действия
Id createModifierAct(App& a) {
  Id nid = 0;
  a.act("Новый модификатор", [&](Tx& tx) { nid = rules::createModifier(tx); });
  return nid;
}

}  // namespace

bool editMod(App& a, const Modifier& m, std::string_view label, const std::function<void(Modifier&)>& fn, std::string coalesce) {
  const Id id = m.id;
  const std::string key = m.key;
  Id made = 0;
  TxOptions opt;
  opt.coalesce = std::move(coalesce);
  bool ok = a.act(label, [&](Tx& tx) {
    made = id ? id : rules::ensureBuiltinMod(tx, key);
    fn(tx.modifier(made));
  }, opt);
  if (ok && !id && made && a.ui.editor == "modifiers") a.ui.editorArg = made;
  return ok;
}

Id recordOf(Tx& tx, const Modifier& m) { return m.id ? m.id : rules::ensureBuiltinMod(tx, m.key); }

std::string mergeKey(const Modifier& m, std::string_view part) {
  return "mod:" + (m.key.empty() ? std::to_string(m.id) : m.key) + ":" + std::string(part);
}

namespace {

Id duplicateAct(App& a, const Modifier& src) {
  Modifier copy = src;
  Id nid = 0;
  a.act("Копия модификатора", [&](Tx& tx) {
    nid = rules::createModifier(tx, orName(copy.name, "Модификатор") + " — копия");
    Modifier& d = tx.modifier(nid);
    std::string name = d.name;
    d = copy;
    d.id = nid;
    d.name = name;
    d.key.clear();   // копия встроенного — свой модификатор
  });
  return nid;
}

// Удаление своего модификатора с подтверждением: в тексте — где он используется. next — что выделить после.
void askDelete(App& a, Id mod, Id next) {
  const World& w = a.world();
  const Modifier* m = w.modifier(mod);
  if (!m || isBuiltin(*m)) return;
  ModUse u = usageOf(w, mod);
  std::string name = "«" + orName(m->name, "Модификатор") + "»";
  std::string text = u.total() ? name + " используется: " + usageText(u) + ". Модификатор будет убран из всех этих списков."
                               : name + " нигде не используется.";
  a.confirmAct("Удалить модификатор?", text, "Удалить", "Удалить модификатор", [mod](Tx& tx) { rules::removeModifier(tx, mod); },
               [next](App& x) {
                 if (x.ui.editor == "modifiers") x.ui.editorArg = next;
               });
}

// ---------------------------------------------------------------- список
void listRow(App& a, const Entry& e, Id& sel, EdState& st) {
  const World& w = a.world();
  const Modifier& m = e.m;
  ui::IdScope s{i64(e.id)};
  std::string tip = missingTargets(m) ? std::string("Дипломатия без государств-целей")
                    : isAuto(m)       ? (e.uses ? "Действует сейчас: " + fmtInt(e.uses) : std::string("Ставится сам"))
                    : e.uses          ? "Используется: " + usageText(usageOf(w, m.id))
                                      : std::string("Нигде не используется");
  if (edkit::entityRow(orName(m.name, "Без названия"), effectSummary(w, m), modIcon(m), m.color, e.uses ? std::to_string(e.uses) : std::string(),
                       e.id == sel, missingTargets(m), e.builtin ? "Встроенный · " + tip : tip))
    sel = e.id;
  if (e.builtin) {   // встроенный — замок на плитке значка
    const ui::Theme& th = ui::theme();
    const RectF rr = ui::lastItem().rect;
    const float cx = rr.x + 10 + 30 - 1, cy = rr.cy() + 15 - 1;
    ui::draw::circle(cx, cy, 7, th.surface1);
    ui::draw::icon("lock", RectF{cx - 5, cy - 5, 10, 10}, th.textMuted);
  }
  if (e.id == sel) {
    a.markUi("modifiers.selected");
    if (st.seenSel != sel) ui::scrollToItem();
  }
  if (e.builtin && !m.key.empty()) a.markUi("modifiers.builtin." + m.key);
}

// Строка группы: шеврон, значок вида, название, число. true — щелчок (свернуть/развернуть).
bool groupHeader(App& a, const KindGroup& g, size_t n, bool open, bool forced) {
  const ui::Theme& th = ui::theme();
  ui::IdScope s(g.mark);
  const RectF r = ui::next(0, 30);
  ui::Interaction it = ui::interact(ui::id("##head"), r);
  if (it.hovered && !forced) ui::setCursor(platform::Cursor::Hand);
  const float hv = ui::animate(ui::id("##hv"), it.hovered && !forced ? 1.f : 0.f);
  if (hv > 0.01f) ui::draw::rect(r, th.hover.alpha(hv), 6);
  ui::draw::icon(open ? "chevron-down" : "chevron-right", RectF{r.x + 6, r.cy() - 7, 14, 14}, forced ? th.textMuted.alpha(0.5f) : th.textMuted);
  ui::draw::icon(g.icon, RectF{r.x + 26, r.cy() - 8, 16, 16}, th.textDim);
  const std::string cnt = fmtInt(i64(n));
  const float pw = std::max(22.f, std::ceil(ui::measure(cnt, ui::Font::Caption)) + 12);
  const RectF pr{r.right() - 10 - pw, r.cy() - 9, pw, 18};
  ui::draw::text(g.title, RectF{r.x + 50, r.y, pr.x - r.x - 58, r.h}, ui::Font::Strong, th.text);
  ui::draw::rect(pr, th.surface3, 9);
  ui::draw::text(cnt, pr, ui::Font::Caption, th.textDim, ui::Align::Center);
  a.markUi(std::string("modifiers.group.") + g.mark, r);
  return it.clicked && !forced;
}

using Groups = std::array<std::vector<const Entry*>, kKindGroupCount>;

void drawList(App& a, EdState& st, Id& sel, const Groups& groups, size_t shown, size_t totalOwn, size_t totalAll) {
  bool ro = !a.canEdit();
  {
    ui::Row r({ui::fr(1), ui::px(30)}, 30, 6);
    if (st.focusSearch) {
      ui::setKeyboardFocus(ui::id("q"));
      st.focusSearch = false;
    }
    ui::searchField("q", st.query, "Поиск модификаторов");
    a.markUi("modifiers.search");
    if (ui::iconButton("plus", "Новый модификатор", {.variant = ui::Variant::Secondary, .disabled = ro, .shortcut = {Key::Insert, 0}})) {
      if (Id nid = createModifierAct(a)) {
        sel = nid;
        st.query.clear();
        st.filter = 0;
        st.focusName = true;
      }
    }
    a.markUi("modifiers.new");
  }
  ui::segmented("filter", st.filter,
                {{"list", {}, "Все модификаторы"}, {"province", {}, "С локальными эффектами"}, {"crown", {}, "С глобальными эффектами"},
                 {"unlink", {}, "Нигде не используются"}},
                {.size = ui::Size::Small});
  a.markUi("modifiers.filter");
  RectF rest = ui::avail();
  float footH = ui::lineHeight(ui::Font::Small);
  const bool filtering = !st.query.empty() || st.filter != 0;   // поиск и фильтр раскрывают группы
  auto& closed = closedGroups(a);
  {
    const float listH = std::max(60.f, rest.h - footH - 8);
    a.markUi("modifiers.list", RectF{rest.x, rest.y, rest.w, listH});
    ui::Scroll sc("list", listH);
    ui::gap(2);
    if (shown == 0) {
      ui::spacer(16);
      ui::label("Ничего не найдено", {.ink = ui::Ink::Muted, .align = ui::Align::Center});
    }
    for (int gi = 0; gi < kKindGroupCount; gi++) {
      const auto& items = groups[size_t(gi)];
      if (items.empty()) continue;
      const bool open = filtering || !closed[size_t(gi)];
      if (groupHeader(a, kKindGroups[gi], items.size(), open, filtering)) closed[size_t(gi)] = !closed[size_t(gi)];
      if (!open) continue;
      for (const Entry* e : items) listRow(a, *e, sel, st);
      ui::spacer(2);
    }
    if (totalOwn == 0 && !filtering) ui::label("Своих модификаторов пока нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  st.seenSel = sel;
  std::string cnt = fmtInt(i64(totalAll)) + " " + plural(i64(totalAll), "модификатор", "модификатора", "модификаторов");
  if (shown != totalAll) cnt = "Показано " + fmtInt(i64(shown)) + " из " + fmtInt(i64(totalAll));
  ui::label(cnt, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
}

// ---------------------------------------------------------------- карточка модификатора
void drawDetail(App& a, EdState& st, const Modifier& m, Id next, const std::map<std::string, std::vector<Id>>& autos) {
  const World& w = a.world();
  bool ro = !a.canEdit();
  const bool builtin = isBuiltin(m), automatic = isAuto(m);
  ui::Scroll sc("detail");
  // Шапка: плитка значка, название, действия.
  {
    ui::Row head({ui::px(60), ui::fr(1), ui::px(30), ui::px(30)}, 60, 12);
    edkit::iconTile(m.icon, m.color, 60, "sparkles");
    {
      ui::Group g(0, 4);
      ui::caption(std::string(builtin ? "Встроенный модификатор · " : "Модификатор · ") + utf8::lower(schema::modKind(m.kind).name));
      std::string name = m.name;
      if (st.focusName) {
        ui::setKeyboardFocus(ui::id("name"));
        st.focusName = false;
      }
      if (ui::textField("name", name, {.placeholder = "Название модификатора", .icon = "edit", .maxLength = 80, .readOnly = ro,
                                       .selectAllOnFocus = true}) &&
          trim(name) != m.name) {
        std::string n = trim(name);
        editMod(a, m, "Переименовать модификатор", [&](Modifier& x) { x.name = n; });
      }
      a.markUi("modifiers.name");
    }
    {
      ui::Group g(30, 0);
      ui::spacer(22);
      if (ui::iconButton("duplicate", "Копия модификатора", {.disabled = ro, .shortcut = {Key::D, ui::ModPrimary}})) {
        if (Id nid = duplicateAct(a, m)) a.ui.editorArg = nid;
      }
      a.markUi("modifiers.duplicate");
    }
    {
      ui::Group g(30, 0);
      ui::spacer(22);
      if (builtin) {
        ui::icon("lock", ui::Ink::Muted, 18, "Встроенный модификатор — удалить нельзя");
      } else {
        if (ui::iconButton("trash", "Удалить модификатор", {.disabled = ro, .shortcut = {Key::Delete, 0}, .tone = ui::Tone::Danger}))
          askDelete(a, m.id, next);
        a.markUi("modifiers.delete");
      }
    }
  }
  if (builtin) {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::tag("встроенный", ui::Tone::Accent, "lock");
    a.markUi("modifiers.builtinTag");
    if (automatic) {
      ui::tag("ставится сам", ui::Tone::Info, "bolt");
      a.markUi("modifiers.autoTag");
    }
    if (!m.id) ui::label("шаблон", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .tooltip = "Запись мира появится при первой правке"});
  }
  ui::spacer(4);
  // Оформление, применение и предпросмотр.
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, ui::kAuto, 12);
    {
      ui::Group g;
      ui::Card c({.icon = "palette", .title = "Оформление"});
      {
        ui::Row pr({ui::px(76), ui::fr(1)}, 30, 8);
        ui::label("Значок", {.ink = ui::Ink::Dim});
        {
          ui::HStack hs(30, ui::Align::Left, 8);
          std::string icon = m.icon;
          if (edkit::iconPicker("icon", icon, false, ro, "Значок модификатора") && icon != m.icon)
            editMod(a, m, "Значок модификатора", [&](Modifier& x) { x.icon = icon; });
          a.markUi("modifiers.icon");
          ui::label(edkit::iconTitle(m.icon), {.ink = ui::Ink::Dim});
        }
        ui::label("Цвет", {.ink = ui::Ink::Dim});
        Color col = m.color;
        {
          ui::Disabled dcol(ro);
          if (ui::colorButton("color", col, {.tooltip = "Цвет модификатора"}) && !ro)
            editMod(a, m, "Цвет модификатора", [&](Modifier& x) { x.color = col; }, mergeKey(m, "color"));
          a.markUi("modifiers.color");
        }
        // Где действует (ТЗ «Модификаторы», п.1): у встроенного — закреплено.
        ui::label("Действует", {.ink = ui::Ink::Dim});
        {
          std::vector<ui::Option> kinds;
          for (int k = 0; k < int(ModKind::Count); k++) kinds.push_back(ui::Option{schema::kModKinds[k].name, schema::kModKinds[k].icon});
          int kind = int(m.kind);
          if (ui::combo("kind", kind, std::span<const ui::Option>(kinds),
                        {.disabled = ro || builtin, .tooltip = builtin ? std::string_view("У встроенного модификатора закреплено") : std::string_view("Где действует")}) &&
              kind != int(m.kind) && kind >= 0 && kind < int(ModKind::Count))
            a.act("Где действует модификатор", [&](Tx& tx) {
              Modifier& x = tx.modifier(m.id);
              x.kind = ModKind(kind);
              // Модификатор археологических групп — только эффекты групп (ТЗ «Доработки №2», п.1): прочие снимаются.
              if (x.kind == ModKind::ArchGroup) {
                x.fxMask &= archMask();
                for (int f = 0; f < kFxCount; f++)
                  if (!x.has(Fx(f))) x.fx[size_t(f)] = 0;
                x.targets.clear();
              }
              // Генерация за ход — у видов «Везде», «Для провинций», «Глобальный», «Для героев».
              if (!genKind(x.kind)) {
                x.essGen.clear();
                x.resGen.clear();
              }
              // Носители, на которых модификатор по новому виду не действует, его теряют.
              rules::fitModifierUses(tx, m.id);
            });
          a.markUi("modifiers.kind");
        }
        // Срок по умолчанию: ходов действия при установке, 0 — бессрочно.
        ui::label("Срок", {.ink = ui::Ink::Dim});
        {
          int dur = std::max(0, m.duration);
          if (ui::numberField("duration", dur, {.min = 0, .max = 1000, .unit = "ход|хода|ходов", .icon = "hourglass", .steppers = true, .disabled = ro,
                                                .tooltip = "Срок при установке; 0 — бессрочно"}))
            editMod(a, m, "Срок модификатора", [&](Modifier& x) { x.duration = std::max(0, dur); }, mergeKey(m, "duration"));
          a.markUi("modifiers.duration");
        }
      }
      std::string desc = m.desc;
      if (ui::textArea("desc", desc, 64, {.placeholder = "Описание", .readOnly = ro}) && desc != m.desc)
        editMod(a, m, "Описание модификатора", [&](Modifier& x) { x.desc = desc; });
      a.markUi("modifiers.desc");
    }
    {
      ui::Group g;
      ui::Card c({.icon = "eye", .title = "Предпросмотр"});
      {
        ui::HStack hs(26, ui::Align::Left, 6);
        ui::ChipOpt co;
        co.icon = modIcon(m);
        co.color = m.color;
        co.tooltip = m.desc;
        ui::chip(orName(m.name, "Модификатор"), co);
        if (m.duration > 0) ui::tag(nTurns(m.duration), ui::Tone::Neutral, "hourglass");
        if (missingTargets(m)) ui::tag("Нет целей дипломатии", ui::Tone::Warning, "warning");
      }
      edkit::effectChips(m);
      a.markUi("modifiers.preview");
      ModUse u = usageOf(w, m.id);
      ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
      ui::stat(fmtInt(i64(u.provinces.size())), "Провинции", {.icon = "province", .tone = ui::Tone::Info});
      ui::stat(fmtInt(i64(u.states.size() + u.guilds.size())), "Государства и гильдии", {.icon = "crown", .tone = ui::Tone::Accent});
      ui::stat(fmtInt(i64(u.armies.size() + u.heroes.size())), "Войска и персонажи", {.icon = "army", .tone = ui::Tone::Danger});
      ui::stat(fmtInt(i64(u.techs.size() + u.levels.size())), "Технологии и постройки", {.icon = "tech", .tone = ui::Tone::Success});
    }
  }
  // Эффекты (ТЗ 1.g.ii): на широком экране локальные и глобальные — рядом, эффекты войск — ниже. Модификатор
  // археологических групп — только эффекты групп; у глобального и «везде» они тоже есть (действуют на все группы
  // государства). Генерация за ход — у видов «Везде», «Для провинций», «Глобальный», «Для героев».
  if (m.kind == ModKind::ArchGroup) {
    effectGroup(a, m, FxGroup::Arch, ro);
  } else {
    if (ui::avail().w >= 1180) {
      ui::Row r({ui::fr(1), ui::fr(1)}, ui::kAuto, 12);
      {
        ui::Group g;
        effectGroup(a, m, FxGroup::Local, ro);
      }
      {
        ui::Group g;
        effectGroup(a, m, FxGroup::Global, ro);
      }
    } else {
      effectGroup(a, m, FxGroup::Local, ro);
      effectGroup(a, m, FxGroup::Global, ro);
    }
    effectGroup(a, m, FxGroup::Army, ro);
    if (m.kind == ModKind::Any || m.kind == ModKind::Hero || (m.fxMask & heroMask())) effectGroup(a, m, FxGroup::Hero, ro);
    if (m.kind == ModKind::Any || m.kind == ModKind::Faction || (m.fxMask & archMask())) effectGroup(a, m, FxGroup::Arch, ro);
  }
  if (genKind(m.kind)) genSection(a, m, ro);
  usageSection(a, m, autos, ro);
  ui::spacer(8);
}

// ---------------------------------------------------------------- окно
void drawEditor(App& a, Id arg) {
  const World& w = a.world();
  EdState& st = ui::state<EdState>(ui::id("##modstate"));
  auto uses = usageCounts(w);
  auto autos = autoUse(w);
  std::vector<Entry> own, built;
  collect(w, uses, &autos, own, built);
  // Группы по виду (в каждой — свои по названию, затем встроенные в порядке ТЗ) и строки в порядке показа.
  const bool filtering = !st.query.empty() || st.filter != 0;
  auto& closed = closedGroups(a);
  Groups groups;
  size_t matched = 0;
  for (const std::vector<Entry>* list : {&own, &built})
    for (const Entry& e : *list)
      if (passes(e, st)) {
        groups[size_t(groupIndex(e.m.kind))].push_back(&e);
        matched++;
      }
  auto findEntry = [&](Id id) -> const Entry* {
    for (const Entry& e : own)
      if (e.id == id) return &e;
    for (const Entry& e : built)
      if (e.id == id) return &e;
    return nullptr;
  };
  // Выделение — аргумент окна; шаблон, у которого уже есть запись мира, — эта запись; нет такого — первый из видимых.
  Id sel = arg;
  if (int ti = tplIndex(sel); ti >= 0)
    if (Id rid = rules::builtinModId(w, schema::builtinModifiers()[size_t(ti)].key)) sel = rid;
  bool refiltered = st.query != st.seenQuery || st.filter != st.seenFilter;
  st.seenQuery = st.query;
  st.seenFilter = st.filter;
  // Запись встроенного отменили (Ctrl+Z) — выделение остаётся на нём (снова шаблон).
  if (!findEntry(sel) && !st.selKey.empty())
    for (const Entry& e : built)
      if (e.m.key == st.selKey) sel = e.id;
  // Выделение сменилось или модификатор перешёл в другую группу — его группа раскрывается.
  if (const Entry* e = findEntry(sel)) {
    const int gi = groupIndex(e->m.kind);
    if (sel != st.seenSel || gi != st.seenGroup) closed[size_t(gi)] = false;
    st.seenGroup = gi;
  }
  std::vector<const Entry*> shown;   // видимые строки (раскрытые группы) в порядке показа
  for (int gi = 0; gi < kKindGroupCount; gi++)
    if (filtering || !closed[size_t(gi)]) shown.insert(shown.end(), groups[size_t(gi)].begin(), groups[size_t(gi)].end());
  bool visible = std::any_of(shown.begin(), shown.end(), [&](const Entry* e) { return e->id == sel; });
  if (!findEntry(sel) || (refiltered && !visible && !shown.empty())) {
    const Entry* first = !shown.empty() ? shown.front() : !own.empty() ? &own.front() : !built.empty() ? &built.front() : nullptr;
    sel = first ? first->id : 0;
  }
  auto neighbor = [&](Id id) -> Id {
    for (size_t i = 0; i < shown.size(); i++)
      if (shown[i]->id == id) {
        if (i + 1 < shown.size()) return shown[i + 1]->id;
        if (i > 0) return shown[i - 1]->id;
      }
    return 0;
  };
  if (ui::shortcut({Key::F, ui::ModPrimary})) st.focusSearch = true;

  const auto [L, D] = edkit::listDetail(ui::avail());
  {
    ui::Area la(L, 0);
    drawList(a, st, sel, groups, matched, own.size(), own.size() + built.size());
  }
  {
    ui::Area da(D, 0);
    const Entry* e = findEntry(sel);
    if (!e) {
      ui::spacer(std::max(0.f, D.h * 0.3f));
      ui::emptyState("sparkles", "Модификатор не выбран.");
    } else {
      Modifier m = e->m;   // копия: правка посреди кадра заменяет мир
      drawDetail(a, st, m, neighbor(sel), autos);
    }
  }
  // Стрелки — по списку (если их не забрало поле или ползунок).
  if (!shown.empty()) {
    int idx = -1;
    for (size_t i = 0; i < shown.size(); i++)
      if (shown[i]->id == sel) idx = int(i);
    if (ui::shortcut({Key::Down, 0})) sel = shown[size_t(std::min(int(shown.size()) - 1, idx + 1))]->id;
    if (ui::shortcut({Key::Up, 0})) sel = shown[size_t(std::max(0, idx - 1))]->id;
  }
  if (const Entry* e = findEntry(sel)) st.selKey = e->builtin ? e->m.key : std::string();
  // Выбор в списке — в аргумент окна (если действие кадра не выбрало другое: копия, удаление, запись встроенного).
  if (a.ui.editor == "modifiers" && a.ui.editorArg == arg) a.ui.editorArg = sel;
}

}  // namespace
}  // namespace modedit

namespace {
EditorReg editorReg({"modifiers", "Модификаторы", modedit::drawEditor, "sparkles", "reference", 10});
CommandReg commandReg({"editor.modifiers", "Модификаторы", "sparkles", "Ctrl+8", [](App& a) { a.openEditor("modifiers", 0); },
                       [](App& a) { return a.ui.screen == Screen::Editor && a.admin(); }, false, "Справочники", 60});

}  // namespace
}  // namespace rg::app
