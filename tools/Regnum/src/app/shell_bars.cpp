// Regnum — верхняя и нижняя строки основного экрана.
// Верхняя: знак «REGNUM», мир (меню файла и состояние сохранения), режим карты (на странице — её заголовок), посередине
// параметры инструмента карты или отметка прошлого хода, справа поиск, отмена и повтор, сохранение, настройки.
// Нижняя: выделенный объект (или объект под указателем), координаты, ход и «Завершить ход».
#include "app/shell_internal.h"
#include "base/fs.h"
#include "gfx/text.h"

namespace rg::app::detail::shell {

using platform::Key;

namespace {

// Вертикальная черта между группами строки.
void barSep(float x, RectF r) { ui::draw::line(x, r.y + 11, x, r.bottom() - 11, ui::theme().border, 1); }

std::string saveStatus(App& a, ui::Tone& tone, std::string& tip) {
  App::Impl& d = D(a);
  std::string status;
  if (a.readOnly()) {
    status = "только просмотр";
    tone = ui::Tone::Info;
  } else if (a.dirty()) {
    status = a.projectPath().empty() ? "не сохранён" : "изменён";
    tone = ui::Tone::Warning;
  } else {
    std::string at = !d.autosavedAt.empty() && d.autosavedAt >= d.savedAt && !a.projectPath().empty() ? d.autosavedAt : d.savedAt;
    status = at.empty() ? std::string("сохранён") : "сохранён " + localTime(at);
    tone = ui::Tone::Success;
  }
  tip = a.projectPath().empty() ? std::string("Мир ещё не записан на диск — Ctrl+S")
                                : (a.dirty() ? "Есть несохранённые изменения — Ctrl+S" : "Все изменения записаны") + std::string("\n") +
                                      a.projectPath();
  return status;
}

void worldMenu(App& a) {
  if (!ui::beginMenu("worldmenu")) return;
  ui::menuHeader(a.projectPath().empty() ? std::string("Мир не сохранён") : fs::filename(a.projectPath()));
  auto item = [&](const char* cmd) {
    const CommandDef* c = findCommand(cmd);
    if (!c) return;
    bool en = !c->enabled || c->enabled(a);
    if (ui::menuItem(c->title, {.icon = c->icon, .shortcut = parseShortcut(c->shortcut ? c->shortcut : ""), .disabled = !en}))
      later(a, [id = std::string(cmd)](App& x) { runCommand(x, id); });
  };
  item("file.new");
  item("file.open");
  item("file.openBundle");
  ui::menuSeparator();
  item("file.save");
  item("file.saveAs");
  item("file.export");
  item("file.reveal");
  ui::menuSeparator();
  item("turn.history");
  item("app.settings");
  ui::menuSeparator();
  item("file.close");
  ui::endMenu();
}

void modeMenu(App& a) {
  if (!ui::beginMenu("modes")) return;
  ui::menuHeader("Режим карты");
  for (int i = 0; i < int(schema::MapMode::Count); i++) {
    const schema::EnumInfo& mi = schema::kMapModes[i];
    ui::IdScope s(i);
    if (ui::menuItem(mi.name, {.icon = mi.icon, .shortcut = {Key(int(Key::D1) + i), 0}, .checked = int(a.ui.mapMode) == i}))
      a.setMapMode(schema::MapMode(i));
  }
  ui::endMenu();
}

// Отметка прошлого хода посередине верхней строки.
void readOnlyPill(App& a, RectF slot, RectF bar) {
  const ui::Theme& th = ui::theme();
  std::string text = "Ход " + std::to_string(*a.ui.viewTurn) + " · только просмотр";
  const char* back = "К текущему ходу";
  float tw = std::ceil(ui::measure(text, ui::Font::Strong));
  float bw = std::ceil(ui::measure(back, ui::Font::Strong)) + 46;
  float w = std::min(slot.w, 16 + 18 + 8 + tw + 14 + bw + 4);
  if (w < 160) return;
  RectF r{std::round(clamp(bar.cx() - w * 0.5f, slot.x, slot.right() - w)), bar.y + 5, w, 30};
  a.markUi("banner", r);
  ui::draw::rect(r, th.info.alpha(0.12f), th.radiusField);
  ui::draw::rectStroke(r, th.info.alpha(0.4f), th.radiusField, 1);
  ui::draw::icon("history", RectF{r.x + 10, r.cy() - 8, 16, 16}, th.info);
  ui::draw::text(text, RectF{r.x + 32, r.y, r.w - 32 - bw - 8, r.h}, ui::Font::Strong, th.text);
  ui::at(RectF{r.right() - bw - 3, r.y + 3, bw, 24});
  if (ui::button(back, {.variant = ui::Variant::Primary, .icon = "arrow-right", .size = ui::Size::Small})) a.backToCurrent();
  a.markUi("banner.back");
}

}  // namespace

// ================================================================ верхняя строка
void topBar(App& a, RectF r) {
  App::Impl& d = D(a);
  const ui::Theme& th = ui::theme();
  ui::Panel bar("topbar", r, docked());
  a.markUi("topbar", r);
  edgeLine(r, false, false, false, true);
  const bool compact = r.w < 1200;
  const bool onMap = a.view() == View::Map;

  // Справа налево: справка, настройки, сохранение | повтор, отмена | поиск.
  const float by = r.y + std::round((r.h - 30) * 0.5f);
  float rx = r.right() - 10;
  auto rightButton = [&](const char* cmd, const char* icon, std::string_view tip, const char* mark, bool badge = false) {
    rx -= 30;
    ui::at(RectF{rx, by, 30, 30});
    const CommandDef* c = findCommand(cmd);
    if (!c) return;
    bool en = !c->enabled || c->enabled(a);
    std::string t = tip.empty() ? std::string(c->title) : std::string(tip);
    bool clicked = ui::iconButton(icon, t, {.badge = badge, .badgeColor = th.warning, .disabled = !en});
    if (c->shortcut && *c->shortcut) ui::tooltip(t, parseShortcut(c->shortcut));
    if (clicked) runCommand(a, cmd);
    a.markUi(mark);
    rx -= 2;
  };
  if (!compact) rightButton("app.help", "keyboard", {}, "topbar.help");
  rightButton("app.settings", "settings", {}, "topbar.settings");
  rightButton("file.save", "save", a.dirty() ? "Сохранить — есть несохранённые изменения" : "Сохранить", "topbar.save", a.dirty() && !a.readOnly());
  rx -= 6;
  barSep(rx, r);
  rx -= 8;
  rightButton("edit.redo", "redo", a.store.canRedo() ? "Повторить: " + a.store.redoLabel() : std::string("Повторить"), "topbar.redo");
  rightButton("edit.undo", "undo", a.store.canUndo() ? "Отменить: " + a.store.undoLabel() : std::string("Отменить"), "topbar.undo");
  rx -= 6;
  barSep(rx, r);
  rx -= 8;
  rightButton("app.palette", "search", {}, "topbar.palette");
  const float rightEdge = rx - 8;

  // Знак «REGNUM»: шрифт с засечками, разрядка.
  const gfx::TextStyle wm{gfx::FontFamily::Display, gfx::FontWeight::Regular, 17.f, 4.4f};
  const float wmW = std::ceil(gfx::measureText("REGNUM", wm));
  ui::draw::textStyled("REGNUM", RectF{r.x + 18, r.y, wmW + 4, r.h}, wm, th.text);
  float x = r.x + 18 + wmW + 14;
  barSep(x, r);
  x += 8;

  // Мир: состояние сохранения точкой и меню файла.
  float leftEnd = x;
  {
    ui::Area ar(RectF{x, by, std::max(0.f, rightEdge - x), 30}, 0);
    ui::HStack hs(30, ui::Align::Left, 2);
    ui::Tone tone;
    std::string tip;
    std::string status = saveStatus(a, tone, tip);
    ui::label("●", {.font = ui::Font::Small, .color = ui::toneColor(tone), .tooltip = "Мир " + status + "\n" + tip});
    a.markUi("topbar.status");
    std::string name = gfx::ellipsize(a.worldTitle(), ui::textStyle(ui::Font::Body), compact ? 150.f : 240.f);
    if (ui::button(name + "##world", {.variant = ui::Variant::Ghost, .iconRight = "chevron-down", .tooltip = "Мир: файл, сохранение, история ходов"}))
      ui::openPopup("worldmenu");
    a.markUi("topbar.world");
    leftEnd = ui::lastItem().rect.right();
    worldMenu(a);
    RectF sep = ui::next(17, 30);
    barSep(sep.cx(), r);
    if (onMap) {
      const schema::EnumInfo& mi = schema::kMapModes[int(a.ui.mapMode)];
      if (ui::button(std::string(mi.name) + "##mode", {.variant = ui::Variant::Ghost, .icon = mi.icon, .iconRight = "chevron-down", .tooltip = "Режим карты · 1–9"}))
        ui::openPopup("modes");
      a.markUi("topbar.mode");
      leftEnd = ui::lastItem().rect.right();
      modeMenu(a);
    } else {
      ui::label(pageTitle(a), {.font = ui::Font::Strong, .ink = ui::Ink::Dim});
      leftEnd = ui::lastItem().rect.right();
    }
  }

  // Середина: параметры инструмента (OptionsBar) или отметка прошлого хода.
  RectF slot{leftEnd + 16, r.y, std::max(0.f, rightEdge - leftEnd - 24), r.h};
  d.toolSlot = onMap ? slot : RectF{};
  if (a.readOnly()) readOnlyPill(a, slot, r);
}

// ================================================================ нижняя строка
namespace {

// Объект под указателем: значок или цвет владельца, название, владелец.
void hoverInfo(App& a, RectF r) {
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  const Selection h = a.ui.hover;
  if (!h) return;
  std::string name = entityName(w, h), sub;
  const Province* pr = h.type == SelType::Province ? w.province(h.id) : nullptr;
  const Faction* own = pr ? w.faction(pr->owner) : nullptr;
  if (own) sub = own->name;
  else if (pr && !pr->sea) sub = "без владельца";
  float x = r.x;
  if (pr) {
    if (own) ui::draw::circle(x + 5, r.cy(), 4.f, own->color);
    else ui::draw::ring(x + 5, r.cy(), 3.5f, 1.2f, th.textMuted);
  } else {
    ui::draw::icon(selIcon(h.type), RectF{x - 1, r.cy() - 8, 16, 16}, th.textDim);
  }
  x += 18;
  float nw = std::min(ui::measure(name, ui::Font::Body) + 2, r.right() - x);
  ui::draw::text(name, RectF{x, r.y, nw, r.h}, ui::Font::Body, th.text);
  x += nw + 8;
  if (!sub.empty() && r.right() - x > 30) ui::draw::text(sub, RectF{x, r.y, r.right() - x, r.h}, ui::Font::Small, th.textMuted);
}

// Выделенный объект: значок и вид; без свойств нижней строки (QuickReg) — и название (щелчок — показать на карте).
float selectionInfo(App& a, RectF r, bool withName) {
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  const Selection s = a.ui.sel;
  std::string cap = selCaption(s.type);
  if (s.type == SelType::Faction)
    if (const Faction* f = w.faction(s.id)) cap = f->isGuild() ? "Гильдия" : "Государство";
  float x = r.x;
  ui::draw::icon(selIcon(s.type), RectF{x, r.cy() - 9, 18, 18}, th.accent);
  x += 26;
  float cw = std::ceil(ui::measure(cap, ui::Font::Small));
  ui::draw::text(cap, RectF{x, r.y, cw + 2, r.h}, ui::Font::Small, th.textMuted);
  x += cw + 12;
  if (!withName) return x;
  std::string name = entityName(w, s);
  float nw = std::min(std::ceil(ui::measure(name, ui::Font::Strong)) + 2, std::max(0.f, r.right() - x));
  RectF nr{x - 6, r.y + 4, nw + 12, r.h - 8};
  ui::at(nr);
  bool clicked = ui::button("##selname", {.variant = ui::Variant::Ghost, .tooltip = "Показать на карте · F"});
  bool hov = ui::lastItem().hovered;
  ui::draw::text(name, RectF{x, r.y, nw, r.h}, ui::Font::Strong, hov ? th.accentHover : th.text);
  if (clicked) a.focusSelection();
  a.markUi("bottombar.selection", nr);
  return x + nw;
}

}  // namespace

void bottomBar(App& a, RectF r) {
  const ui::Theme& th = ui::theme();
  ui::Panel bar("bottombar", r, docked());
  a.markUi("bottombar", r);
  edgeLine(r, false, true, false, false);
  const float by = r.y + std::round((r.h - 30) * 0.5f);
  const bool ro = a.readOnly();

  // Справа: ход (история ходов) и «Завершить ход».
  float rx = r.right() - 8 - 30;
  {
    ui::at(RectF{rx, by, 30, 30});
    ui::Disabled dis(ro);
    if (ui::iconButton("next-turn", "Завершить ход", {.variant = ui::Variant::Primary})) a.endTurn();
    ui::tooltip("Завершить ход: доходы, расходы, стройки, исследования", parseShortcut("Ctrl+Enter"));
    a.markUi("topbar.endturn");
  }
  {
    int turn = ro ? *a.ui.viewTurn : a.store.world().turn();
    std::string label = "Ход " + std::to_string(turn);
    float bw = std::ceil(ui::measure(label, ui::Font::Strong)) + 40;
    rx -= bw + 6;
    ui::at(RectF{rx, by, bw, 30});
    if (ui::button(label + "##turn", {.variant = ui::Variant::Ghost, .icon = ro ? "history" : "hourglass", .tooltip = "История ходов"})) a.openDialog("turn.history");
    a.markUi("topbar.turn");
    if (ro) ui::draw::rect(RectF{rx + 8, r.bottom() - 4, bw - 16, 2}, th.info, 1);
  }
  rx -= 10;
  barSep(rx, r);
  // Координаты под указателем (над картой).
  if (a.mapShown()) {
    const float cw = 112;
    rx -= cw + 10;
    if (a.ui.cursorMap) {
      std::string coords = fmtInt(i64(std::lround(a.ui.cursorMap->x))) + " · " + fmtInt(i64(std::lround(a.ui.cursorMap->y)));
      ui::draw::icon("crosshair", RectF{rx, r.cy() - 7, 14, 14}, th.textMuted);
      ui::draw::text(coords, RectF{rx + 20, r.y, cw - 20, r.h}, ui::Font::Mono, th.textDim);
    }
    a.markUi("status", RectF{rx, r.y, cw, r.h});
  }
  const float leftMax = rx - 16;

  // Слева: выделение (вид и самое нужное — QuickReg) или объект под указателем; на странице — ничего (всё на ней).
  RectF lr{r.x + 16, r.y, std::max(0.f, leftMax - r.x - 16), r.h};
  if (a.view() != View::Map) return;
  if (!a.ui.sel) {
    hoverInfo(a, lr);
    return;
  }
  // Свернуть или показать панель выделения: свойства остаются в этой строке, карта — шире.
  {
    ui::at(RectF{lr.right() - 30, by, 30, 30});
    bool hidden = a.ui.inspectorHidden;
    if (ui::iconButton(hidden ? "chevron-left" : "chevron-right", hidden ? "Показать панель" : "Скрыть панель")) a.ui.inspectorHidden = !hidden;
    a.markUi("bottombar.panel");
  }
  bool quick = false;
  for (auto& q : quicks()) quick = quick || q.type == a.ui.sel.type;
  float x = selectionInfo(a, RectF{lr.x, lr.y, lr.w - 40, lr.h}, !quick);
  if (!quick) return;
  ui::Area ar(RectF{x, by, std::max(0.f, lr.right() - 40 - x), 30}, 0);
  ui::HStack hs(30, ui::Align::Left, 8);
  ui::IdScope sc{int(a.ui.sel.type)};
  for (auto& q : quicks()) {
    if (q.type != a.ui.sel.type || !q.draw) continue;
    ui::IdScope qs(q.id);
    try {
      q.draw(a, a.ui.sel.id);
    } catch (const std::exception& e) {
      a.error(e);
    }
  }
}

}  // namespace rg::app::detail::shell
