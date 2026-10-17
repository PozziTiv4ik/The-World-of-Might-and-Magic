// Regnum — элементы управления: кнопки, кнопки-значки, сегменты, переключатель, флажок, радио, ползунок, вкладки.
#include "ui/ui_internal.h"

namespace rg::ui {

using namespace in;

namespace {

float ctlH(Size s) { return s == Size::Small ? C().th.controlHSmall : s == Size::Large ? 36.f : C().th.controlH; }

}  // namespace

// ================================================================ кнопки
bool button(std::string_view lbl, const ButtonOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  DisableGuard dg(o.disabled);
  bool disabled = isDisabled();
  std::string_view s = displayText(lbl);
  bool small = o.size == Size::Small;
  const gfx::TextStyle st = small ? styleWith(Font::Small, gfx::FontWeight::Semibold) : styleOf(Font::Strong);
  float is = small ? 14 : 16;
  float pad = small ? 10 : 14;
  float tw = s.empty() ? 0 : textWidth(s, st);
  float w = pad * 2 + tw + (o.icon ? is + (s.empty() ? 0 : 7) : 0) + (o.iconRight ? is + 6 : 0);
  if (o.icon && !s.empty()) w -= 2;
  float h = ctlH(o.size);
  RectF r = place(o.fill ? 0 : std::ceil(w), h);
  if (r.h > h) {
    r.y += std::round((r.h - h) * 0.5f);
    r.h = h;
  }
  WidgetId wid = id(lbl.empty() ? std::string_view(o.icon ? o.icon : "button") : lbl);
  Interaction it = interact(wid, r, IfFocusable);
  bool clicked = it.clicked;
  if (o.isDefault && !c.modalStack.empty()) c.modalStack.back().defaultBtn = wid;
  if (c.pendingDefault == wid) {
    c.pendingDefault = 0;
    if (!disabled) clicked = true;
  }
  if (o.shortcut && !disabled && shortcut(o.shortcut)) clicked = true;
  float hv = animate(wid ^ 0xb7701ull, it.hovered ? 1.f : 0.f);
  bool down = it.held && it.hovered;
  float rad = t.radiusField;
  Color bg{0, 0, 0, 0}, fg = t.text, border{0, 0, 0, 0};
  switch (o.variant) {
    case Variant::Primary:
      bg = mixc(t.accent, t.accentHover, hv);
      if (down) bg = t.accent.darken(0.08f);
      fg = t.onAccent;
      break;
    case Variant::Danger:
      bg = mixc(t.danger, t.danger.lighten(0.1f), hv);
      if (down) bg = t.danger.darken(0.08f);
      fg = Color(255, 255, 255);
      break;
    case Variant::Secondary:
      bg = mixc(t.surface3, t.surface3.lighten(0.05f), hv);
      if (down) bg = t.surface3.darken(0.06f);
      border = mixc(t.borderStrong.alpha(0.7f), t.borderStrong, hv);
      break;
    case Variant::Ghost:
      bg = t.hover.alpha(hv);
      if (down) bg = t.pressed;
      break;
    case Variant::Subtle:
      bg = t.hover.alpha(hv * 0.8f);
      if (down) bg = t.pressed;
      fg = mixc(t.textDim, t.text, hv);
      break;
  }
  if (o.variant == Variant::Primary || o.variant == Variant::Danger) {
    cmdShadow(r, rad, 3, Color(0, 0, 0, 70), 1);
    draw::gradient(r, bg.lighten(0.07f), bg.darken(0.03f), rad);
    cmdStroke(r, bg.darken(0.12f).alpha(0.6f), rad, 1);
  } else {
    cmdRect(r, bg, rad);
    if (border.a) cmdStroke(r, border, rad, 1);
  }
  const float rightW = o.iconRight ? is + 6 : 0;
  float cw = tw + (o.icon ? is + (s.empty() ? 0 : 5) : 0) + rightW;
  // Содержимое по центру; не помещается — от левого края, подпись обрезается справа (значок справа остаётся в кнопке).
  float x = std::max(r.x + std::round((r.w - cw) * 0.5f), r.x + std::round(pad * 0.5f));
  if (o.icon) {
    cmdIcon(o.icon, RectF{x, r.cy() - is * 0.5f, is, is}, fg);
    x += is + (s.empty() ? 0 : 5);
  }
  bool cut = false;   // подпись обрезана многоточием — полный текст в подсказке
  if (!s.empty()) {
    const float room = std::max(0.f, std::min(tw + 1, r.right() - pad * 0.5f - rightW - x));
    textIn(s, RectF{x, r.y, room, r.h}, st, fg);
    cut = textCut(s, room, st);
    x += (cut ? room : tw) + 6;
  }
  if (o.iconRight) cmdIcon(o.iconRight, RectF{x, r.cy() - is * 0.5f, is, is}, o.variant == Variant::Secondary ? t.textDim : fg);
  setLast(wid, r, it);
  c.last.clicked = clicked;
  focusRing(r, rad);
  if (cut) tooltip(fullTextWith(s, o.tooltip), o.shortcut);
  else if (!o.tooltip.empty() || o.shortcut) tooltip(o.tooltip.empty() ? s : o.tooltip, o.shortcut);
  return clicked;
}

bool iconButton(const char* icon, std::string_view tip, const IconButtonOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  DisableGuard dg(o.disabled);
  bool disabled = isDisabled();
  if (tip.empty()) {
    static bool warned = false;
    if (!warned) logWarn("ui: iconButton без подсказки (%s)", icon ? icon : "?");
    warned = true;
  }
  float h = ctlH(o.size);
  RectF r = place(h, h);
  if (r.w > h || r.h > h) r = RectF{r.x + std::round((r.w - h) * 0.5f), r.y + std::round((r.h - h) * 0.5f), h, h};
  std::string key = std::string(icon ? icon : "") + "\x1f" + std::string(tip);
  WidgetId wid = id(key);
  Interaction it = interact(wid, r, IfFocusable);
  bool clicked = it.clicked;
  if (o.shortcut && !disabled && shortcut(o.shortcut)) clicked = true;
  float hv = animate(wid ^ 0x1c0b1ull, it.hovered ? 1.f : 0.f);
  float on = animate(wid ^ 0x1c0b2ull, o.toggled ? 1.f : 0.f);
  bool down = it.held && it.hovered;
  float rad = t.radiusField;
  Color fg = o.tone != Tone::Neutral ? toneColor(o.tone) : mixc(t.textDim, t.text, hv);
  switch (o.variant) {
    case Variant::Primary:
      draw::gradient(r, mixc(t.accent, t.accentHover, hv).lighten(0.06f), mixc(t.accent, t.accentHover, hv).darken(0.03f), rad);
      fg = t.onAccent;
      break;
    case Variant::Danger:
      cmdRect(r, mixc(t.danger, t.danger.lighten(0.1f), hv), rad);
      fg = Color(255, 255, 255);
      break;
    case Variant::Secondary:
      cmdRect(r, mixc(t.surface3, t.surface3.lighten(0.05f), hv), rad);
      cmdStroke(r, t.borderStrong.alpha(0.7f + 0.3f * hv), rad, 1);
      break;
    case Variant::Ghost:
    case Variant::Subtle:
      if (hv > 0.01f) cmdRect(r, down ? t.pressed : t.hover.alpha(hv), rad);
      break;
  }
  if (on > 0.01f) {
    cmdRect(r, t.accent.alpha(0.16f * on), rad);
    cmdStroke(r, t.accent.alpha(0.38f * on), rad, 1);
    fg = mixc(fg, t.accent, on);
  }
  float is = o.size == Size::Small ? t.iconSizeSmall : o.size == Size::Large ? 20.f : t.iconSize;
  RectF ir{r.x + std::round((r.w - is) * 0.5f), r.y + std::round((r.h - is) * 0.5f), is, is};
  if (down) ir.y += 0.5f;
  cmdIcon(icon ? icon : "", ir, fg);
  if (o.badge) {
    Color bc = o.badgeColor.a ? o.badgeColor : t.danger;
    float bx = r.right() - 7, by = r.y + 7;
    draw::circle(bx, by, 4.5f, t.surface1);
    draw::circle(bx, by, 3.2f, bc);
  }
  setLast(wid, r, it);
  c.last.clicked = clicked;
  focusRing(r, rad);
  tooltip(tip, o.shortcut);
  return clicked;
}

bool iconToggle(const char* icon, std::string_view tip, bool& on, const IconButtonOpt& o) {
  IconButtonOpt k = o;
  k.toggled = on;
  if (iconButton(icon, tip, k)) {
    on = !on;
    C().last.changed = true;
    return true;
  }
  return false;
}

// ================================================================ сегменты
static bool segmentedImpl(std::string_view name, int& index, std::span<const Segment> items, const SegmentedOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  DisableGuard dg(o.disabled);
  int n = int(items.size());
  if (n == 0) return false;
  float h = ctlH(o.size);
  const gfx::TextStyle& st = styleOf(o.size == Size::Small ? Font::Small : Font::Body);
  const gfx::TextStyle stOn = styleWith(o.size == Size::Small ? Font::Small : Font::Body, gfx::FontWeight::Semibold);
  float is = o.size == Size::Small ? 14 : 16;
  std::vector<float> nat(size_t(n), 0);
  float natTotal = 4;
  for (int i = 0; i < n; i++) {
    const Segment& sg = items[size_t(i)];
    float w = (sg.icon ? is : 0) + (sg.label.empty() ? 0 : textWidth(displayText(sg.label), stOn) + (sg.icon ? 6 : 0));
    nat[size_t(i)] = w + (sg.label.empty() ? 14 : 24);
    natTotal += nat[size_t(i)];
  }
  RectF r = place(o.fill ? 0 : natTotal, h);
  if (r.h > h) {
    r.y += std::round((r.h - h) * 0.5f);
    r.h = h;
  }
  WidgetId wid = id(name);
  registerId(wid);
  Color track = mixc(t.bg, t.surface1, 0.5f);
  cmdRect(r, track, t.radiusField);
  cmdStroke(r, t.border.alpha(0.7f), t.radiusField, 1);
  RectF inner = r.inset(2);
  std::vector<float> xs(size_t(n) + 1, inner.x);
  for (int i = 0; i < n; i++) xs[size_t(i) + 1] = xs[size_t(i)] + (o.fill ? (inner.w / float(n)) : nat[size_t(i)]);
  int cur = clamp(index, -1, n - 1);
  bool changed = false;
  // Клавиатура: фокус на всём переключателе, стрелки меняют выбор.
  if (!isDisabled()) registerFocusable(wid);
  bool focused = c.focus == wid;
  if (focused && !isDisabled()) {
    if (takeKey(Key::Left) && cur > 0) { cur--; changed = true; }
    if (takeKey(Key::Right) && cur < n - 1) { cur++; changed = true; }
  }
  int hoveredI = -1;
  for (int i = 0; i < n; i++) {
    RectF cell{xs[size_t(i)], inner.y, xs[size_t(i) + 1] - xs[size_t(i)], inner.h};
    WidgetId cid = hashMix(wid, u64(i) + 1);
    Interaction it = interact(cid, cell);
    if (it.pressed) setFocus(wid, false);
    if (it.hovered) hoveredI = i;
    if (it.clicked && cur != i) { cur = i; changed = true; }
    if (it.hovered && !items[size_t(i)].tooltip.empty()) {
      setLast(cid, cell, it);
      tooltip(items[size_t(i)].tooltip);
    }
  }
  if (cur >= 0) {
    float tx = animate(wid ^ 0x5e1ull, xs[size_t(cur)], 0.16f);
    float tw = animate(wid ^ 0x5e2ull, xs[size_t(cur) + 1] - xs[size_t(cur)], 0.16f);
    RectF th{tx, inner.y, tw, inner.h};
    float rr = t.radiusField - 2;
    cmdShadow(th, rr, 3, Color(0, 0, 0, 90), 1);
    cmdRect(th, t.surface3.lighten(0.06f), rr);
    cmdStroke(th, Color(255, 255, 255, 14), rr, 1);
  }
  for (int i = 0; i < n; i++) {
    const Segment& sg = items[size_t(i)];
    RectF cell{xs[size_t(i)], inner.y, xs[size_t(i) + 1] - xs[size_t(i)], inner.h};
    bool on = i == cur;
    Color fg = on ? t.text : (i == hoveredI ? t.text : t.textDim);
    std::string_view lb = displayText(sg.label);
    float cw = (sg.icon ? is : 0) + (lb.empty() ? 0 : textWidth(lb, on ? stOn : st) + (sg.icon ? 6 : 0));
    float x = cell.x + std::round((cell.w - cw) * 0.5f);
    if (sg.icon) {
      cmdIcon(sg.icon, RectF{x, cell.cy() - is * 0.5f, is, is}, on ? t.accent : fg);
      x += is + 6;
    }
    if (!lb.empty()) textIn(lb, RectF{x, cell.y, cell.right() - x, cell.h}, on ? stOn : st, fg);
  }
  Interaction whole{};
  whole.hovered = hoveredI >= 0;
  setLast(wid, r, whole);
  c.last.focused = focused;
  focusRing(r, t.radiusField);
  if (changed) {
    index = cur;
    c.last.changed = true;
  }
  return changed;
}

bool segmented(std::string_view name, int& index, std::initializer_list<Segment> items, const SegmentedOpt& o) {
  return segmentedImpl(name, index, std::span<const Segment>(items.begin(), items.size()), o);
}
bool segmented(std::string_view name, int& index, std::span<const Segment> items, const SegmentedOpt& o) { return segmentedImpl(name, index, items, o); }

// ================================================================ переключатели
bool toggle(std::string_view lbl, bool& on, bool disabled) {
  Ctx& c = C();
  const Theme& t = c.th;
  DisableGuard dg(disabled);
  std::string_view s = displayText(lbl);
  const auto& st = styleOf(Font::Body);
  float tw = s.empty() ? 0 : textWidth(s, st);
  Frame& f = frame();
  bool natural = f.flow == Flow::HStack;
  RectF r = place(natural ? tw + (s.empty() ? 0 : 12) + 36 : 0, t.controlH);
  WidgetId wid = id(lbl);
  Interaction it = interact(wid, r, IfFocusable);
  bool changed = false;
  if (it.clicked) {
    on = !on;
    changed = true;
  }
  float k = animate(wid ^ 0x7091ull, on ? 1.f : 0.f, 0.16f);
  float hv = animate(wid ^ 0x7092ull, it.hovered ? 1.f : 0.f);
  RectF sw{r.right() - 36, r.cy() - 10, 36, 20};
  if (!s.empty()) textIn(s, RectF{r.x, r.y, sw.x - 12 - r.x, r.h}, st, mixc(t.textDim, t.text, std::max(hv, 0.6f)));
  Color offc = t.borderStrong;
  Color trackc = mixc(offc, t.accent, k);
  if (hv > 0.01f && !on) trackc = mixc(trackc, t.textMuted, 0.25f * hv);
  cmdRect(sw, trackc, 10);
  float kx = sw.x + 2 + 16 * k;
  RectF knob{kx, sw.y + 2, 16, 16};
  cmdShadow(knob, 8, 3, Color(0, 0, 0, 70), 1);
  draw::circle(knob.cx(), knob.cy(), 8, Color::hex(0xf4f1ea));
  setLast(wid, r, it);
  c.last.changed = changed;
  RectF fr = sw;
  focusRing(fr, 10);
  return changed;
}

static bool checkImpl(std::string_view lbl, Check& state, bool disabled, bool radioStyle) {
  Ctx& c = C();
  const Theme& t = c.th;
  DisableGuard dg(disabled);
  std::string_view s = displayText(lbl);
  const auto& st = styleOf(Font::Body);
  float tw = s.empty() ? 0 : textWidth(s, st);
  RectF r = place(18 + (s.empty() ? 0 : 8 + tw), std::max(22.f, lineHeight(Font::Body)));
  WidgetId wid = id(lbl);
  Interaction it = interact(wid, r, IfFocusable);
  bool changed = false;
  if (it.clicked) {
    state = radioStyle ? Check::On : (state == Check::On ? Check::Off : Check::On);
    changed = true;
  }
  float k = animate(wid ^ 0xc4e1ull, state != Check::Off ? 1.f : 0.f, 0.12f);
  float hv = animate(wid ^ 0xc4e2ull, it.hovered ? 1.f : 0.f);
  RectF box{r.x, r.cy() - 9, 18, 18};
  if (radioStyle) {
    Color ring = mixc(t.borderStrong, t.textMuted, hv * 0.6f);
    draw::circle(box.cx(), box.cy(), 9, t.surface3);
    draw::ring(box.cx(), box.cy(), 8.5f, 1.25f, mixc(ring, t.accent, k));
    if (k > 0.01f) {
      draw::circle(box.cx(), box.cy(), 9 * k, t.accent);
      draw::circle(box.cx(), box.cy(), 3.6f * k, t.onAccent);
    }
  } else {
    float rad = 5;
    cmdRect(box, mixc(t.surface3, t.accent, k), rad);
    if (k < 0.99f) cmdStroke(box, mixc(t.borderStrong, t.textMuted, hv * 0.6f).alpha(1 - k), rad, 1.25f);
    if (state == Check::Mixed) draw::line(box.x + 5, box.cy(), box.right() - 5, box.cy(), t.onAccent.alpha(k), 2);
    else drawCheckGlyph(box, t.onAccent, k);
  }
  if (!s.empty()) textIn(s, RectF{box.right() + 8, r.y, r.right() - box.right() - 8, r.h}, st, t.text);
  setLast(wid, r, it);
  c.last.changed = changed;
  focusRing(box, radioStyle ? 9 : 5);
  return changed;
}

bool checkbox(std::string_view lbl, bool& on, bool disabled) {
  Check s = on ? Check::On : Check::Off;
  bool ch = checkImpl(lbl, s, disabled, false);
  if (ch) on = s == Check::On;
  return ch;
}

bool checkbox(std::string_view lbl, Check& st, bool disabled) {
  if (st == Check::Mixed) {
    Check s = st;
    bool ch = checkImpl(lbl, s, disabled, false);
    if (ch) st = Check::On;
    return ch;
  }
  return checkImpl(lbl, st, disabled, false);
}

bool radio(std::string_view lbl, int& value, int option, bool disabled) {
  Check s = value == option ? Check::On : Check::Off;
  bool ch = checkImpl(lbl, s, disabled, true);
  if (ch && value != option) {
    value = option;
    return true;
  }
  C().last.changed = false;
  return false;
}

// ================================================================ ползунок
bool slider(std::string_view name, double& v, double mn, double mx, const SliderOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  DisableGuard dg(o.disabled);
  if (!(mx > mn)) mx = mn + 1;
  RectF r = place(0, t.controlH);
  WidgetId wid = id(name);
  const auto& vst = styleOf(Font::Body);
  auto fmt = [&](double x) {
    std::string s = fmtNum(x, o.digits);
    if (o.unit) {
      std::string u = unitFor(o.unit, x, o.digits);
      s += u == "%" ? " " : " ";
      s += u;
    }
    return s;
  };
  float valW = o.showValue ? std::max(44.f, textWidth(fmt(mx), vst) + 6) : 0;
  RectF tr{r.x + 8, r.cy() - 2, r.w - 16 - (o.showValue ? valW + 8 : 0), 4};
  RectF hit{r.x, r.y, tr.right() + 8 - r.x, r.h};
  Interaction it = interact(wid, hit, IfFocusable);
  double nv = clamp(v, mn, mx);
  auto snapv = [&](double x) {
    if (o.step > 0) x = mn + std::round((x - mn) / o.step) * o.step;
    if (o.digits >= 0 && o.step <= 0) {
      double p = std::pow(10.0, o.digits);
      x = std::round(x * p) / p;
    }
    return clamp(x, mn, mx);
  };
  if (c.active == wid && (it.held || it.pressed)) {
    double k = clamp(double((c.m.x - tr.x) / std::max(1.f, tr.w)), 0.0, 1.0);
    nv = snapv(mn + (mx - mn) * k);
  }
  if (it.focused && !isDisabled()) {
    double st = o.step > 0 ? o.step : (mx - mn) / 100;
    if (takeKey(Key::Right) || takeKey(Key::Up)) nv = snapv(nv + st);
    if (takeKey(Key::Left) || takeKey(Key::Down)) nv = snapv(nv - st);
    if (takeKey(Key::Right, platform::ModShift) || takeKey(Key::Up, platform::ModShift)) nv = snapv(nv + st * 10);
    if (takeKey(Key::Left, platform::ModShift) || takeKey(Key::Down, platform::ModShift)) nv = snapv(nv - st * 10);
    if (takeKey(Key::Home)) nv = mn;
    if (takeKey(Key::End)) nv = mx;
  }
  bool changed = nv != v;
  if (changed) v = nv;
  float k = float((v - mn) / (mx - mn));
  float hv = animate(wid ^ 0x511ull, (it.hovered || c.active == wid) ? 1.f : 0.f);
  Color tone = toneColor(o.tone);
  cmdRect(tr, t.track, 2);
  cmdRect(RectF{tr.x, tr.y, tr.w * k, tr.h}, tone, 2);
  float kx = tr.x + tr.w * k, ky = tr.cy();
  if (hv > 0.01f) draw::circle(kx, ky, 8 + 5 * hv, tone.alpha(0.18f * hv));
  cmdShadow(RectF{kx - 8, ky - 8, 16, 16}, 8, 4, Color(0, 0, 0, 110), 1);
  draw::circle(kx, ky, 8, Color::hex(0xf4f1ea));
  draw::ring(kx, ky, 7.25f, 1.5f, tone);
  if (o.showValue) textIn(fmt(v), RectF{tr.right() + 8, r.y, valW + 8, r.h}, vst, t.textDim, Align::Right);
  if (o.bubble && c.active == wid) {
    std::string s = fmt(v);
    const auto bst = styleWith(Font::Small, gfx::FontWeight::Semibold);
    float bw = textWidth(s, bst) + 14;
    RectF b{kx - bw * 0.5f, r.y - 26, bw, 22};
    cmdShadow(b, 6, 8, t.shadow, 2);
    cmdRect(b, Color::hex(0x2a323e), 6);
    textIn(s, b, bst, Color::hex(0xf4f1ea), Align::Center);
  }
  setLast(wid, r, it);
  c.last.changed = changed;
  c.last.active = c.active == wid;
  focusRing(RectF{kx - 8, ky - 8, 16, 16}, 8);
  if (it.hovered || c.active == wid) c.cursor = platform::Cursor::Hand;
  return changed;
}

// ================================================================ вкладки
static bool tabsImpl(std::string_view name, int& active, std::span<const Tab> items, const TabsOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  int n = int(items.size());
  if (n == 0) return false;
  bool pill = o.style == TabStyle::Pill;
  bool small = o.size == Size::Small;
  float h = pill ? (small ? 28.f : 32.f) : (small ? 34.f : 40.f);
  const gfx::TextStyle st = styleWith(small ? Font::Small : Font::Body, gfx::FontWeight::Semibold);
  float is = small ? 16 : 18;
  std::vector<float> nat(static_cast<size_t>(n));
  float total = pill ? 4 : 0;
  for (int i = 0; i < n; i++) {
    const Tab& tb = items[size_t(i)];
    std::string_view lb = displayText(tb.label);
    float w = (tb.icon ? is : 0) + (lb.empty() ? 0 : textWidth(lb, st) + (tb.icon ? 7 : 0));
    if (tb.badge > 0) w += textWidth(std::to_string(tb.badge), styleWith(Font::Caption, gfx::FontWeight::Semibold)) + 16;
    nat[size_t(i)] = w + (lb.empty() ? 20 : 24);
    total += nat[size_t(i)];
  }
  RectF r = place(o.fill ? 0 : total, h);
  WidgetId wid = id(name);
  registerId(wid);
  RectF inner = pill ? r.inset(2) : r;
  if (pill) {
    cmdRect(r, mixc(t.bg, t.surface1, 0.5f), t.radiusField + 2);
    cmdStroke(r, t.border.alpha(0.7f), t.radiusField + 2, 1);
  } else {
    draw::line(r.x, r.bottom() - 0.5f, r.right(), r.bottom() - 0.5f, t.border, 1);
  }
  std::vector<float> xs(size_t(n) + 1, inner.x);
  for (int i = 0; i < n; i++) xs[size_t(i) + 1] = xs[size_t(i)] + (o.fill ? inner.w / float(n) : nat[size_t(i)]);
  int cur = clamp(active, 0, n - 1);
  bool changed = false;
  registerFocusable(wid);
  bool focused = c.focus == wid;
  if (focused) {
    if (takeKey(Key::Left) && cur > 0) { cur--; changed = true; }
    if (takeKey(Key::Right) && cur < n - 1) { cur++; changed = true; }
  }
  std::vector<float> hov(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) {
    RectF cell{xs[size_t(i)], inner.y, xs[size_t(i) + 1] - xs[size_t(i)], inner.h};
    WidgetId cid = hashMix(wid, u64(i) + 7);
    Interaction it = interact(cid, cell);
    if (it.pressed) {
      setFocus(wid, false);
      if (cur != i) { cur = i; changed = true; }
    }
    hov[size_t(i)] = animate(cid ^ 0x7ab1ull, it.hovered ? 1.f : 0.f);
    const Tab& tb = items[size_t(i)];
    if (it.hovered && (!tb.tooltip.empty() || !tb.label.empty())) {
      setLast(cid, cell, it);
      if (!tb.tooltip.empty()) tooltip(tb.tooltip);
      else if (displayText(tb.label).empty()) tooltip(tb.label);
    }
  }
  float ax = animate(wid ^ 0x7ab2ull, xs[size_t(cur)], 0.18f);
  float aw = animate(wid ^ 0x7ab3ull, xs[size_t(cur) + 1] - xs[size_t(cur)], 0.18f);
  if (pill) {
    RectF th{ax, inner.y, aw, inner.h};
    cmdShadow(th, t.radiusField, 4, Color(0, 0, 0, 90), 1);
    cmdRect(th, t.surface3.lighten(0.06f), t.radiusField);
    cmdStroke(th, Color(255, 255, 255, 14), t.radiusField, 1);
  }
  for (int i = 0; i < n; i++) {
    const Tab& tb = items[size_t(i)];
    RectF cell{xs[size_t(i)], inner.y, xs[size_t(i) + 1] - xs[size_t(i)], inner.h};
    bool on = i == cur;
    float hv = hov[size_t(i)];
    if (!pill && hv > 0.01f && !on) cmdRect(RectF{cell.x + 2, cell.y + 4, cell.w - 4, cell.h - 9}, t.hover.alpha(hv), 6);
    Color fg = on ? t.text : mixc(t.textMuted, t.text, hv * 0.8f);
    std::string_view lb = displayText(tb.label);
    std::string badgeTxt = tb.badge > 0 ? std::to_string(tb.badge) : std::string();
    const auto bst = styleWith(Font::Caption, gfx::FontWeight::Semibold);
    float bw = tb.badge > 0 ? textWidth(badgeTxt, bst) + 10 : 0;
    // Подпись укорачивается с многоточием, если вкладка узкая.
    float fixedW = (tb.icon ? is + (lb.empty() ? 0 : 7) : 0) + (bw > 0 ? bw + 6 : 0);
    float lw = lb.empty() ? 0 : std::max(0.f, std::min(textWidth(lb, st), cell.w - 16 - fixedW));
    float cw = fixedW + lw;
    float x = cell.x + std::round((cell.w - cw) * 0.5f);
    float cy = pill ? cell.cy() : cell.cy() - 1;
    if (tb.icon) {
      cmdIcon(tb.icon, RectF{x, cy - is * 0.5f, is, is}, on ? t.accent : fg);
      if (tb.badge < 0) {
        draw::circle(x + is + 1, cy - is * 0.5f + 1, 4, pill ? t.surface3 : t.surface1);
        draw::circle(x + is + 1, cy - is * 0.5f + 1, 2.8f, toneColor(tb.badgeTone));
      }
      x += is + (lb.empty() ? 0 : 7);
    }
    if (!lb.empty()) {
      textIn(lb, RectF{x, cell.y, lw + 1, cell.h}, st, fg);
      x += lw;
    }
    if (bw > 0) {
      x += 6;
      RectF b{x, cy - 8, bw, 16};
      Color tc = toneColor(tb.badgeTone);
      cmdRect(b, on ? tc : tc.alpha(0.18f), 8);
      textIn(badgeTxt, b, bst, on ? (tb.badgeTone == Tone::Accent ? t.onAccent : Color(255, 255, 255)) : tc, Align::Center);
    }
  }
  if (!pill) cmdRect(RectF{ax + 10, r.bottom() - 2, std::max(0.f, aw - 20), 2}, t.accent, 1);
  Interaction whole{};
  setLast(wid, r, whole);
  c.last.focused = focused;
  focusRing(r, 6);
  if (changed) {
    active = cur;
    c.last.changed = true;
  }
  return changed;
}

bool tabs(std::string_view name, int& active, std::initializer_list<Tab> items, const TabsOpt& o) {
  return tabsImpl(name, active, std::span<const Tab>(items.begin(), items.size()), o);
}
bool tabs(std::string_view name, int& active, std::span<const Tab> items, const TabsOpt& o) { return tabsImpl(name, active, items, o); }

}  // namespace rg::ui
