// Regnum — оболочка основного экрана: закреплённые строки и ленты, карта или страница на её месте, панели справа от
// карты, уведомления, диалоги, глобальные сочетания, ввод мышью над картой и отрисовка карты. Части оболочки —
// shell_bars.cpp (верхняя и нижняя строки), shell_side.cpp (ленты и панели), shell_pages.cpp (страницы).
#include "app/shell_internal.h"
#include "map/art_scene.h"
#include "base/fs.h"
#include "gfx/text.h"

namespace rg::app::detail {

using platform::Key;

// ================================================================ помощники оболочки
namespace shell {

ui::PanelOpt docked(float pad) { return ui::PanelOpt{.pad = pad, .shadow = false, .border = false, .radius = 0}; }

void edgeLine(RectF r, bool left, bool top, bool right, bool bottom) {
  const Color c = ui::theme().border;
  if (left) ui::draw::line(r.x + 0.5f, r.y, r.x + 0.5f, r.bottom(), c, 1);
  if (right) ui::draw::line(r.right() - 0.5f, r.y, r.right() - 0.5f, r.bottom(), c, 1);
  if (top) ui::draw::line(r.x, r.y + 0.5f, r.right(), r.y + 0.5f, c, 1);
  if (bottom) ui::draw::line(r.x, r.bottom() - 0.5f, r.right(), r.bottom() - 0.5f, c, 1);
}

bool commandButton(App& a, const char* cmd, const char* icon, bool toggled, std::string_view tip, ui::Size size) {
  const CommandDef* c = findCommand(cmd);
  if (!c) return false;
  bool en = !c->enabled || c->enabled(a);
  std::string t = tip.empty() ? std::string(c->title) : std::string(tip);
  bool clicked = ui::iconButton(icon ? icon : c->icon, t, {.size = size, .toggled = toggled, .disabled = !en});
  if (c->shortcut && *c->shortcut) {
    std::string_view sc = c->shortcut;
    if (sc == "+" || sc == "-") ui::tooltip(t + " · " + std::string(sc));   // «=» на клавише «+» понятнее как «+»
    else ui::tooltip(t, parseShortcut(sc));
  }
  if (clicked) runCommand(a, cmd);
  return clicked;
}

bool resizeHandle(const char* id, RectF r, float& width, float minW, float maxW, bool leftEdge) {
  struct S {
    float start = 0;
  };
  ui::WidgetId wid = ui::id(id);
  auto& st = ui::state<S>(wid);
  ui::Interaction it = ui::interact(wid, r);
  if (it.pressed) st.start = width;
  if (it.hovered || it.held) {
    ui::setCursor(platform::Cursor::ResizeH);
    const ui::Theme& th = ui::theme();
    float k = ui::animate(wid ^ 7, 1.f, 0.12f);
    ui::draw::rect(RectF{leftEdge ? r.x : r.right() - 2, r.y + r.h * 0.5f - 20, 2, 40}, th.accent.alpha(0.75f * k), 1);
  }
  if (it.held && it.dragging) {
    width = clamp(st.start + (leftEdge ? -it.dx : it.dx), minW, maxW);
    return true;
  }
  return it.held;
}

bool navItem(std::string_view label, const char* icon, bool active, std::string_view hint, int badge, std::string_view tooltip) {
  std::string b = badge > 0 ? std::to_string(badge) : std::string();
  return ui::listItem(label, {.icon = icon, .hint = hint, .badge = b, .selected = active, .tooltip = tooltip});
}

void navCaption(std::string_view text) {
  ui::spacer(2);
  ui::Indent in(12);   // по значкам пунктов
  ui::caption(text);
}

}  // namespace shell

using namespace shell;

namespace {

const char* toastIconOf(ToastKind k) {
  switch (k) {
    case ToastKind::Success: return "check-circle";
    case ToastKind::Warning: return "warning";
    case ToastKind::Danger: return "error";
    default: return "info";
  }
}
ui::Tone toneOf(ToastKind k) {
  switch (k) {
    case ToastKind::Success: return ui::Tone::Success;
    case ToastKind::Warning: return ui::Tone::Warning;
    case ToastKind::Danger: return ui::Tone::Danger;
    default: return ui::Tone::Info;
  }
}

}  // namespace

// ================================================================ уведомления
void drawToasts(App& a, RectF area) {
  auto& list = a.toasts();
  if (list.empty()) return;
  const ui::Theme& th = ui::theme();
  const float W = 340;
  double now = a.time();
  float dt = ui::dt();
  float y = area.bottom();
  gfx::TextStyle st = ui::textStyle(ui::Font::Body);
  for (size_t i = list.size(); i-- > 0;) {
    Toast& t = list[i];
    bool hasAction = !t.actionLabel.empty();
    float textW = W - 50 - 36 - (hasAction ? ui::measure(t.actionLabel, ui::Font::Strong) + 30 : 0);
    gfx::TextLayout L = gfx::layoutText(t.text, st, textW, 4, true);
    float h = std::max(48.f, std::ceil(L.height) + 28);
    float tin = clamp(float((now - t.start) / 0.22), 0.f, 1.f);
    float tout = t.closeAt >= 0 ? clamp(float((now - t.closeAt) / 0.18), 0.f, 1.f) : 0.f;
    float ein = 1 - (1 - tin) * (1 - tin);
    float ty = ui::animate(ui::id(i64(t.id) * 7 + 1), y - h, 0.18f);
    RectF r{std::round(area.right() - W + 40 * (1 - ein) + 60 * tout), std::round(ty), W, h};
    {
      ui::Panel p("toast#" + std::to_string(t.id), r, {.pad = 0, .radius = 10});
      a.markUi("toast." + std::to_string(i), r);
      const ui::Mouse& m = ui::mouse();
      t.hovered = r.contains(m.x, m.y);
      if (t.hovered && t.closeAt < 0) t.paused += dt;
      Color tc = ui::toneColor(toneOf(t.kind));
      RectF ic{r.x + 14, r.y + std::round((h - 22) * 0.5f), 22, 22};
      ui::draw::rect(ic.expand(3), tc.alpha(0.16f), 8);
      ui::draw::icon(t.icon.empty() ? toastIconOf(t.kind) : t.icon, ic.inset(1), tc);
      {
        ui::Area ta(RectF{r.x + 50, r.y + std::round((h - L.height) * 0.5f), textW + 1, std::ceil(L.height) + 2}, 0);
        ui::text(t.text, ui::Font::Body, ui::Ink::Normal);
      }
      if (hasAction) {
        float bw = ui::measure(t.actionLabel, ui::Font::Strong) + 22;
        ui::at(RectF{r.right() - 34 - bw, r.y + std::round((h - 24) * 0.5f), bw, 24});
        if (ui::button(t.actionLabel, {.variant = ui::Variant::Secondary, .size = ui::Size::Small})) {
          auto fn = t.action;
          later(a, [fn](App& x) {
            if (fn) fn(x);
          });
          t.closeAt = now;
        }
        a.markUi("toast." + std::to_string(i) + ".action");
      }
      RectF xr{r.right() - 30, r.y + std::round((h - 22) * 0.5f), 22, 22};
      ui::WidgetId xid = ui::id("##x");
      ui::Interaction xi = ui::interact(xid, xr);
      float xh = ui::animate(xid ^ 3, xi.hovered ? 1.f : 0.f);
      if (xh > 0.01f) ui::draw::rect(xr, th.hover.alpha(xh * 1.5f), 6);
      ui::draw::icon("close", xr.inset(4), Color::mix(th.textMuted, th.text, xh));
      if (xi.clicked && t.closeAt < 0) t.closeAt = now;
      double left = t.duration - (now - t.start - t.paused);
      if (t.closeAt < 0) {
        float k = clamp(float(left / std::max(0.1, t.duration)), 0.f, 1.f);
        ui::draw::rect(RectF{r.x + 10, r.bottom() - 3, (r.w - 20) * k, 2}, tc.alpha(0.55f), 1);
        if (left <= 0) t.closeAt = now;
      }
    }
    y -= (h + 8) * (1 - tout);
  }
  while (!list.empty() && list.front().closeAt >= 0 && now - list.front().closeAt > 0.2) list.pop_front();
  for (auto it = list.begin(); it != list.end();) {
    if (it->closeAt >= 0 && now - it->closeAt > 0.2) it = list.erase(it);
    else ++it;
  }
  ui::requestRedraw();
}

// ================================================================ основной экран
void drawEditorScreen(App& a) {
  App::Impl& d = D(a);
  RectF V = ui::viewport();
  float s = ui::uiScale();
  d.toolSlot = RectF{};
  const float top = kTopH, bottom = V.h - kBotH;
  const float H = std::max(0.f, bottom - top);
  topBar(a, RectF{0, 0, V.w, kTopH});
  bottomBar(a, RectF{0, bottom, V.w, kBotH});
  float right = V.w;
  if (!sections().empty() || !drawers().empty()) {
    sectionStrip(a, RectF{V.w - kStripW, top, kStripW, H});
    right -= kStripW;
  }

  // Страница на месте карты: навигация и содержимое.
  if (a.view() != View::Map) {
    a.ui.hover = {};
    a.ui.cursorMap.reset();
    RectF pr{0, top, std::max(0.f, right), H};
    pageHost(a, pr);
    d.rectsNext["map.area"] = RectF{pr.x * s, pr.y * s, pr.w * s, pr.h * s};
    drawToasts(a, RectF{pr.x + kNavW, top + 8, pr.w - kNavW - 12, H - 16});
    return;
  }

  // Карта: лента инструментов слева, справа — панель ленты и панель выделения.
  toolStrip(a, RectF{0, top, kStripW, H});
  const float left = kStripW;
  const float maxSide = std::max(280.f, (right - left) * 0.45f);
  if (!a.ui.drawer.empty()) {
    const DrawerDef* dr = nullptr;
    for (auto& x : drawers())
      if (a.ui.drawer == x.id) dr = &x;
    if (dr) {
      float maxW = std::min(560.f, maxSide);
      float dw = std::round(clamp(a.ui.drawerWidth, 240.f, maxW));
      drawerPanel(a, *dr, RectF{right - dw, top, dw, H}, maxW);
      right -= dw;
    } else {
      a.ui.drawer.clear();
    }
  }
  if (a.ui.sel && !a.ui.inspectorHidden) {
    float maxW = std::min(640.f, std::max(320.f, (right - left) * 0.55f));
    float iw = std::round(clamp(a.ui.inspectorWidth, 320.f, maxW));
    inspectorPanel(a, RectF{right - iw, top, iw, H}, maxW);
    right -= iw;
  }
  RectF area{left, top, std::max(0.f, right - left), H};
  d.rectsNext["map.area"] = RectF{area.x * s, area.y * s, area.w * s, area.h * s};
  drawToasts(a, RectF{area.x + 12, area.y + 8, area.w - 24, area.h - 16});
}

// ================================================================ диалоги
void drawDialogs(App& a) {
  auto& st = a.dialogStack();
  std::vector<Dialog*> closed;
  size_t n = st.size();
  for (size_t i = 0; i < n && i < st.size(); i++) {
    Dialog* dlg = st[i].get();
    Dialog::Style sty;
    try {
      sty = dlg->style(a);
    } catch (const std::exception& e) {
      a.error(e);
      closed.push_back(dlg);
      continue;
    }
    bool open = true;
    ui::ModalOpt o;
    o.title = sty.title;
    o.icon = sty.icon;
    o.tone = sty.tone;
    o.width = sty.width;
    o.closeButton = sty.closeButton;
    o.dismissOnBackdrop = sty.dismissOnBackdrop;
    bool keep = true;
    if (ui::beginModal(dlg->id(), o, &open)) {
      try {
        keep = dlg->draw(a);
      } catch (const std::exception& e) {
        a.error(e);
        keep = false;
      }
      ui::endModal();
    }
    if (!open) {
      try {
        dlg->dismissed(a);
      } catch (const std::exception& e) {
        a.error(e);
      }
      closed.push_back(dlg);
    } else if (!keep) {
      closed.push_back(dlg);
    }
  }
  if (!closed.empty())
    st.erase(std::remove_if(st.begin(), st.end(), [&](auto& p) { return std::find(closed.begin(), closed.end(), p.get()) != closed.end(); }), st.end());
}

// ================================================================ клавиши
void globalKeys(App& a) {
  App::Impl& d = D(a);
  bool editorScreen = a.ui.screen == Screen::Editor;
  bool onMap = a.mapShown() && !a.hasDialog() && !ui::anyModalOpen();
  // 1. Инструмент карты — первым (Esc отменяет построение, Enter завершает, Delete удаляет точку).
  if (onMap && !ui::wantsKeyboard()) {
    if (MapTool* t = a.activeTool()) {
      static const u32 modsets[] = {0, platform::ModShift, platform::ModCtrl, platform::ModCtrl | platform::ModShift, platform::ModAlt,
                                    platform::ModSuper, platform::ModSuper | platform::ModShift};
      for (int k = 1; k < int(Key::Count); k++) {
        Key key = Key(k);
        for (u32 m : modsets) {
          if (!ui::keyPressed(key, m)) continue;
          platform::Event e;
          e.type = platform::EventType::KeyDown;
          e.key = key;
          e.mods = m;
          bool used = false;
          try {
            used = t->key(a, e);
          } catch (const std::exception& ex) {
            a.error(ex);
            used = true;
          }
          if (used) ui::consumeKey(key);
          if (a.activeTool() != t) break;   // инструмент сменился — остальное разберут сочетания
        }
        if (a.activeTool() != t) break;
      }
    }
  }
  // 2. Команды реестра.
  for (auto& c : commands()) {
    if (!c.shortcut || !*c.shortcut || !c.run) continue;
    ui::Shortcut sc = parseShortcut(c.shortcut);
    if (!sc) continue;
    bool hit = false;
    if (c.global) {
      if (ui::keyPressed(sc.key, sc.mods)) {
        ui::consumeKey(sc.key);
        hit = true;
      }
    } else {
      hit = ui::shortcut(sc);
    }
    if (hit) runCommand(a, c.id);
  }
  if (editorScreen && ui::shortcut({Key::Z, ui::ModPrimary | platform::ModShift})) a.redo();
  // 3. Разделы и панели правой ленты, инструменты.
  if (editorScreen) {
    for (auto& sec : sections()) {
      if (!sec.shortcut) continue;
      ui::Shortcut sc = parseShortcut(sec.shortcut);
      if (sc && ui::shortcut(sc)) a.openSection(sec.id);
    }
    for (auto& dr : drawers()) {
      if (!dr.shortcut) continue;
      ui::Shortcut sc = parseShortcut(dr.shortcut);
      if (!sc || !ui::shortcut(sc)) continue;
      if (!a.mapShown()) {
        a.toMap();
        if (a.ui.drawer != dr.id) a.openDrawer(dr.id);
      } else {
        a.openDrawer(dr.id);
      }
    }
  }
  if (onMap) {
    for (auto& t : toolDefs()) {
      if (!t.shortcut || !*t.shortcut) continue;
      ui::Shortcut sc = parseShortcut(t.shortcut);
      if (sc && ui::shortcut(sc)) a.setTool(t.id);
    }
  }
  // 4. Esc: редактор → страница или каталог → выделение → инструмент → панель.
  if (editorScreen && ui::shortcut({Key::Escape, 0})) {
    if (a.view() != View::Map) a.back();
    else if (a.ui.sel) a.clearSelection();
    else if (a.ui.tool != ToolId::Select) a.setTool(ToolId::Select);
    else if (!a.ui.drawer.empty()) a.openDrawer(a.ui.drawer);
  }
  (void)d;
}

// ================================================================ ввод над картой
void mapInput(App& a) {
  App::Impl& d = D(a);
  if (a.hasDialog()) {
    d.panning = false;
    return;
  }
  RectF V = ui::viewport();
  ui::Interaction it = ui::interact(ui::id("##map"), V, ui::IfRightButton | ui::IfMiddleButton);
  // Пробел над картой — панорама: кнопка с фокусом не должна «нажаться» им.
  if ((it.hovered || it.held) && !ui::wantsKeyboard() && ui::keyPressed(Key::Space)) ui::consumeKey(Key::Space);
  float s = ui::uiScale();
  map::MapView& mv = *d.map;
  const map::View& v = mv.view();
  float lx = it.mx * s, ly = it.my * s;
  bool moved = std::fabs(it.mx - d.lastMx) > 0.01f || std::fabs(it.my - d.lastMy) > 0.01f;
  float dxl = (it.mx - d.lastMx) * s, dyl = (it.my - d.lastMy) * s;
  bool hadLast = d.lastMx > -1e8f;
  d.lastMx = it.mx;
  d.lastMy = it.my;
  d.mapHovered = it.hovered || it.held;
  Vec2 mp = v.toMap(lx, ly);
  PointerEvent pe;
  pe.map = mp;
  pe.sx = lx;
  pe.sy = ly;
  pe.button = it.button;
  pe.mods = ui::mouse().mods;
  MapTool* tool = a.activeTool();
  auto call = [&](auto fn) {
    try {
      return fn();
    } catch (const std::exception& e) {
      a.error(e);
      return true;
    }
  };
  if (it.pressed) {
    pe.clicks = it.doubleClicked ? 2 : 1;
    bool pan = it.button == 2 || (it.button == 0 && (d.spaceDown || a.ui.tool == ToolId::Pan));
    if (pan) {
      d.panning = true;
    } else {
      bool handled = tool ? call([&] { return tool->pointerDown(a, pe); }) : false;
      d.toolDown = tool && a.activeTool() == tool;   // инструмент мог смениться (выбор -> правка границ)
      if (!handled && it.doubleClicked && it.button == 0) {
        Id army = mv.armyAt(lx, ly);
        Id prov = army ? 0 : mv.provinceAt(lx, ly);
        if (army) a.select(SelType::Army, army, true);
        else if (prov) a.select(SelType::Province, prov, true);
      }
    }
  } else if (it.held && moved && hadLast) {
    if (d.panning) {
      mv.panBy(dxl, dyl);
      a.requestRedraw();
    } else if (d.toolDown && tool) {
      call([&] { return tool->pointerMove(a, pe); });
    }
  }
  if (it.released) {
    if (d.panning) d.panning = false;
    else if (d.toolDown) {
      d.toolDown = false;
      if (tool && a.activeTool() == tool) call([&] { return tool->pointerUp(a, pe); });
    }
  }
  if (!it.held && !it.pressed && !it.released && it.hovered && moved && tool) call([&] { return tool->pointerMove(a, pe); });
  if (!it.held) d.panning = false;
  // Наведение и координаты
  if (it.hovered || it.held) {
    a.ui.cursorMap = mp;
    if (!d.panning && a.ui.editMap) {
      // Правка карты: под указателем — знак или фигура карты.
      Selection h;
      if (auto sc = mv.artScene()) {
        const double tol = mv.view().toMapLen(4);
        if (Id s = sc->symbolAt(mp, tol)) h = {SelType::Symbol, s};
        else if (Id f = sc->shapeAt(mp, tol)) h = {SelType::Shape, f};
      }
      a.ui.hover = h;
    } else if (!d.panning) {
      Id army = mv.armyAt(lx, ly);
      // Видимая линия маршрута (режимы гильдий и торговли или выбранный маршрут) — над провинцией: так же решает
      // и щелчок инструмента «Выбор».
      Id route = 0;
      if (!army && a.ui.tool == ToolId::Select && !a.ui.editBorders) {
        Id r = mv.routeAt(lx, ly);
        bool shown = a.ui.mapMode == schema::MapMode::Guilds || a.ui.mapMode == schema::MapMode::Trade;
        if (r && (shown || a.ui.sel == Selection{SelType::Route, r})) route = r;
      }
      if (army) a.ui.hover = {SelType::Army, army};
      else if (route) a.ui.hover = {SelType::Route, route};
      else if (Id prov = mv.provinceAt(lx, ly)) a.ui.hover = {SelType::Province, prov};
      else a.ui.hover = {};
    }
  } else {
    a.ui.hover = {};
    a.ui.cursorMap.reset();
  }
}

void mapWheel(App& a) {
  App::Impl& d = D(a);
  if (!d.mapHovered || d.panning) return;
  const ui::Mouse& m = ui::mouse();
  if (m.wheelY == 0) return;
  float s = ui::uiScale();
  double notches = double(m.wheelY) / 64.0;
  double factor = std::pow(1.2, clamp(notches, -6.0, 6.0));
  d.map->zoomAt(m.x * s, m.y * s, factor, std::fabs(notches) >= 0.99);
  a.requestRedraw();
}

// ================================================================ отрисовка карты
map::RenderOptions renderOptions(App& a) {
  map::RenderOptions o;
  o.mode = a.ui.mapMode;
  switch (a.ui.sel.type) {
    case SelType::Province: o.selProvince = a.ui.sel.id; break;
    case SelType::Faction: o.selFaction = a.ui.sel.id; break;
    case SelType::Army: {
      const Id carrier = rules::carrierOf(a.world(), a.ui.sel.id);   // войско на борту — выделен его флот
      o.selArmy = carrier ? carrier : a.ui.sel.id;
      break;
    }
    case SelType::Route: o.selRoute = a.ui.sel.id; break;
    default: break;
  }
  if (a.ui.hover.type == SelType::Province) o.hoverProvince = a.ui.hover.id;
  if (a.ui.hover.type == SelType::Army) o.hoverArmy = a.ui.hover.id;
  o.editBorders = a.ui.editBorders;
  o.background = ui::theme().bg;
  const Settings& st = *a.world().settings;
  o.labels = st.labelStates || st.labelProvinces || st.labelArmies;
  if (MapTool* t = a.activeTool()) {
    try {
      t->renderOptions(a, o);
    } catch (const std::exception& e) {
      a.error(e);
    }
  }
  return o;
}

static u64 hashOf(const map::RenderOptions& o, const map::View& v, u64 gen) {
  u64 h = hash64("map");
  auto mixd = [&](double x) {
    u64 b;
    std::memcpy(&b, &x, sizeof b);
    h = hashMix(h, b);
  };
  mixd(v.cx);
  mixd(v.cy);
  mixd(v.zoom);
  mixd(v.viewport.x);
  mixd(v.viewport.y);
  mixd(v.viewport.w);
  mixd(v.viewport.h);
  mixd(v.dpi);
  h = hashMix(h, u64(o.mode));
  h = hashMix(h, (u64(o.selProvince) << 32) | o.selFaction);
  h = hashMix(h, (u64(o.selArmy) << 32) | o.selRoute);
  h = hashMix(h, (u64(o.hoverProvince) << 32) | o.hoverArmy);
  h = hashMix(h, u64(o.editBorders) | (u64(o.labels) << 1) | (u64(o.darkUi) << 2));
  for (Id x : o.hideArmies) h = hashMix(h, x);
  return hashMix(h, gen);
}

void renderMap(App& a, gfx::Canvas& c) {
  App::Impl& d = D(a);
  map::RenderOptions o = renderOptions(a);
  const map::View& v = d.map->view();
  u64 key = hashOf(o, v, d.mapGen);
  gfx::Image& out = c.target();
  // Меняющаяся карта (панорама, масштаб, догрузка тайлов) рисуется прямо в кадр. Копия для неподвижной карты
  // снимается с первого кадра без изменений; дальше, пока карта стоит, кадр начинается с этой копии.
  const bool changed = key != d.mapKey || d.map->needsRedraw() || d.map->animating();
  if (changed || !d.mapImgValid || d.mapImg.w != out.w || d.mapImg.h != out.h) {
    d.map->render(c, o);
    d.mapKey = key;
    d.mapImgValid = false;
    if (!changed) {
      if (d.mapImg.w != out.w || d.mapImg.h != out.h) d.mapImg.resize(out.w, out.h);
      std::memcpy(d.mapImg.px.data(), out.px.data(), out.px.size() * sizeof(u32));
      d.mapImgValid = true;
    }
  } else {
    std::memcpy(out.px.data(), d.mapImg.px.data(), out.px.size() * sizeof(u32));
  }
  if (MapTool* t = a.activeTool()) {
    c.save();
    c.scale(d.dpi, d.dpi);
    try {
      t->drawOverlay(a, c, v);
    } catch (const std::exception& e) {
      a.error(e);
    }
    c.restore();
  }
}

const char* bordersToggleIcon(bool on) { return on ? "unlock" : "lock"; }

}  // namespace rg::app::detail
