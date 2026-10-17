// Regnum — виджеты текста (подпись, абзац, заголовок группы, свойство, ссылка, клавиши, разделители, значок,
// изображение), общие части рамок полей и подсказки с полным текстом у обрезанных подписей. Кнопки и переключатели —
// widgets_controls.cpp, показатели и фишки — widgets_display.cpp.
#include "ui/ui_internal.h"

namespace rg::ui {

using namespace in;

namespace in {

void drawChevron(RectF r, Color c, float angleDeg) {
  float cx = r.cx(), cy = r.cy();
  float s = std::min(r.w, r.h) / 14.f;
  float a = angleDeg * float(kPi) / 180.f, cs = std::cos(a), sn = std::sin(a);
  auto P = [&](float x, float y) { return gfx::Pt{cx + (x * cs - y * sn) * s, cy + (x * sn + y * cs) * s}; };
  gfx::Path p;
  gfx::Pt a0 = P(-3.75f, -1.9f), a1 = P(0, 1.9f), a2 = P(3.75f, -1.9f);
  p.moveTo(a0.x, a0.y);
  p.lineTo(a1.x, a1.y);
  p.lineTo(a2.x, a2.y);
  draw::pathStroke(p, c, 1.75f * s);
}

void drawCheckGlyph(RectF b, Color c, float t) {
  if (t <= 0.01f) return;
  float s = b.w / 18.f;
  gfx::Path p;
  p.moveTo(b.x + 4.6f * s, b.y + 9.4f * s);
  p.lineTo(b.x + 7.6f * s, b.y + 12.3f * s);
  p.lineTo(b.x + 13.4f * s, b.y + 6.2f * s);
  draw::pathStroke(p, c.alpha(clamp(t * 1.5f, 0.f, 1.f)), 2.f * s);
}

void drawFieldFrame(RectF r, const Interaction& it, bool focused, bool disabled, bool invalid, float invalidT) {
  Ctx& c = C();
  const Theme& t = c.th;
  float rad = t.radiusField;
  cmdRect(r, t.surface3, rad);
  Color bc = t.border.lighten(0.04f);
  if (it.hovered && !disabled) bc = t.borderStrong;
  if (focused) bc = t.accent;
  if (invalid) bc = mixc(bc, t.danger, invalidT);
  if (focused || (invalid && invalidT > 0.01f)) {
    Color glow = invalid ? t.danger : t.accent;
    cmdStroke(r.expand(3), glow.alpha(invalid ? 0.25f * invalidT : 0.22f), rad + 3, 3);
  }
  cmdStroke(r, bc, rad, 1);
}

}  // namespace in

// ================================================================ текст
void label(std::string_view txt, const LabelOpt& o) {
  Ctx& c = C();
  const auto& st = styleOf(o.font);
  std::string_view s = displayText(txt);
  Color col = o.color.a > 0 ? o.color : inkColor(o.ink);
  Color iconCol = o.ink == Ink::Normal && o.color.a == 0 ? c.th.textDim : col;
  float lh = lineHeight(o.font);
  float is = o.font >= Font::Title ? 22.f : 16.f;
  float iconW = o.icon ? is + 6 : 0;
  RectF r;
  bool cut = false;   // обрезано многоточием — полный текст в подсказке
  if (o.wrap) {
    RectF a = c.oneShot ? *c.oneShot : avail();
    float w = std::max(1.f, a.w - iconW);
    TextEntry* te = cachedText(s, st, w, o.maxLines, o.maxLines > 0, o.align, true);
    r = place(0, std::max(lh, te->layout.height));
    if (o.icon) cmdIcon(o.icon, RectF{r.x, r.y + (lh - is) * 0.5f, is, is}, iconCol);
    cmdText(te, r.x + iconW, r.y, col);
    cut = te->layout.truncated;
  } else {
    float tw = textWidth(s, st);
    r = place(o.align == Align::Left ? std::ceil(tw + iconW) : 0, lh);
    float total = std::min(r.w, tw + iconW);
    float x = r.x;
    if (o.align == Align::Center) x = r.x + (r.w - total) * 0.5f;
    else if (o.align == Align::Right) x = r.right() - total;
    if (o.icon) {
      cmdIcon(o.icon, RectF{x, r.cy() - is * 0.5f, is, is}, iconCol);
      x += iconW;
    }
    textIn(s, RectF{x, r.y, std::max(0.f, r.right() - x), r.h}, st, col);
    cut = textCut(s, std::max(0.f, r.right() - x), st);
  }
  if (!o.tooltip.empty()) {
    // ID — текст вместе с подсказкой: одинаковые числа рядом («10 %» налога государства и общего) с разными
    // подсказками не делят один ID.
    WidgetId wid = hashMix(id(txt), hash64(o.tooltip, 0x1abe1ull));
    Interaction it = interact(wid, r);
    setLast(wid, r, it);
    if (cut) tooltip(fullTextWith(s, o.tooltip));
    else tooltip(o.tooltip);
  } else {
    c.last = Item{};
    c.last.rect = r;
    if (cut) fullTextTip(id(txt) ^ 0x1abe1c07ull, r, s);
  }
}

void text(std::string_view s, Font f, Ink ink) {
  LabelOpt o;
  o.font = f;
  o.ink = ink;
  o.wrap = true;
  label(s, o);
}

void caption(std::string_view s) {
  Ctx& c = C();
  gfx::TextStyle st = styleWith(Font::Caption, gfx::FontWeight::Semibold);
  st.letterSpacing = 0.7f;
  RectF r = place(0, 18);
  textIn(utf8::upper(displayText(s)), r, st, c.th.textMuted);
  c.last = Item{};
  c.last.rect = r;
}

void prop(std::string_view lbl, const char* icon, float frac) {
  Ctx& c = C();
  const Theme& t = c.th;
  RectF r = place(0, t.controlH);
  float lw = std::round(r.w * clamp(frac, 0.1f, 0.9f));
  float x = r.x;
  if (icon) {
    cmdIcon(icon, RectF{x, r.cy() - 8, 16, 16}, t.textMuted);
    x += 22;
  }
  textIn(displayText(lbl), RectF{x, r.y, r.x + lw - 8 - x, r.h}, styleOf(Font::Body), t.textDim);
  c.oneShot = RectF{r.x + lw, r.y, r.w - lw, r.h};
  c.oneShotAlign = Align::Left;
}

bool link(std::string_view txt, const char* icon) {
  Ctx& c = C();
  const auto& st = styleOf(Font::Body);
  std::string_view s = displayText(txt);
  float tw = textWidth(s, st) + (icon ? 20 : 0);
  RectF r = place(tw, lineHeight(Font::Body));
  WidgetId wid = id(txt);
  Interaction it = interact(wid, r, IfFocusable);
  Color col = it.hovered ? c.th.accentHover : c.th.accent;
  float x = r.x;
  if (icon) {
    cmdIcon(icon, RectF{x, r.cy() - 8, 16, 16}, col);
    x += 20;
  }
  TextEntry* te = cachedText(s, st);
  float ty = r.y + (r.h - te->layout.height) * 0.5f;
  cmdText(te, x, ty, col);
  if (it.hovered) {
    c.cursor = platform::Cursor::Hand;
    float base = ty + (te->layout.lines.empty() ? te->layout.height : te->layout.lines[0].baseline) + 2;
    draw::line(x, base, x + te->layout.width, base, col.alpha(0.7f), 1);
  }
  setLast(wid, r, it);
  focusRing(r.expand(2), 4);
  return it.clicked;
}

void kbd(std::string_view keys) {
  Ctx& c = C();
  const Theme& t = c.th;
  std::vector<std::string> parts;
  std::string_view s = keys;
  // «Ctrl+Shift+S» → три клавиши; «+» как клавиша — «Ctrl++».
  size_t i = 0;
  std::string cur;
  while (i < s.size()) {
    if (s[i] == '+' && !cur.empty()) {
      parts.push_back(cur);
      cur.clear();
      i++;
      continue;
    }
    cur.push_back(s[i]);
    i++;
  }
  if (!cur.empty()) parts.push_back(cur);
  if (parts.empty()) return;
  const gfx::TextStyle st = styleWith(Font::Small, gfx::FontWeight::Semibold);
  float total = 0;
  std::vector<float> ws;
  for (auto& p : parts) {
    float w = std::max(20.f, textWidth(p, st) + 12);
    ws.push_back(w);
    total += w;
  }
  total += 3 * float(parts.size() - 1);
  RectF r = place(total, 22);
  float x = r.x;
  for (size_t k = 0; k < parts.size(); k++) {
    RectF cap{x, r.y, ws[k], 21};
    cmdRect(RectF{cap.x, cap.y + 1, cap.w, cap.h}, Color(0, 0, 0, 90), 5);
    cmdRect(RectF{cap.x, cap.y, cap.w, cap.h - 1}, t.surface3, 5);
    cmdStroke(RectF{cap.x, cap.y, cap.w, cap.h - 1}, t.borderStrong, 5, 1);
    textIn(parts[k], RectF{cap.x, cap.y, cap.w, cap.h - 1}, st, t.textDim, Align::Center);
    x += ws[k] + 3;
  }
  c.last = Item{};
  c.last.rect = r;
}

void kbd(Shortcut s) { kbd(shortcutText(s)); }

void separator() {
  Ctx& c = C();
  RectF r = place(0, 9);
  draw::line(r.x, r.cy(), r.right(), r.cy(), c.th.border, 1);
}

void separatorV() {
  Ctx& c = C();
  Frame& f = frame();
  float h = f.flow == Flow::HStack ? f.stackH : c.th.controlH;
  RectF r = place(9, h);
  draw::line(r.cx(), r.y + h * 0.2f, r.cx(), r.bottom() - h * 0.2f, c.th.border, 1);
}

void icon(const char* name, Ink ink, float size, std::string_view tip) { iconColored(name, inkColor(ink), size, tip); }

void iconColored(const char* name, Color col, float size, std::string_view tip) {
  Ctx& c = C();
  float s = size > 0 ? size : c.th.iconSize;
  RectF r = place(s, s);
  cmdIcon(name ? name : "", r, col);
  if (!tip.empty()) {
    WidgetId wid = id(std::string_view(name ? name : "")) ^ hash64(tip);
    Interaction it = interact(wid, r);
    setLast(wid, r, it);
    tooltip(tip);
  } else {
    c.last = Item{};
    c.last.rect = r;
  }
}

void image(const gfx::Image& img, float w, float h, float radius) {
  RectF r = place(w, h);
  draw::image(img, r, radius);
  C().last = Item{};
  C().last.rect = r;
}

// ================================================================ обрезанный текст
// Подсказки с полным текстом у подписей, обрезанных многоточием: подпись, кнопка, поле ввода, выпадающий список и его
// пункты, ячейка таблицы (объявления — ui_internal.h).
namespace in {

// Строка s в ширине w обрезается многоточием (так её рисует textIn).
bool textCut(std::string_view s, float w, const gfx::TextStyle& st) {
  if (s.empty() || w <= 0) return false;
  return cachedText(s, st, w + 0.5f, 1, true)->layout.truncated;
}

// Текст подсказки обрезанной подписи: полный текст, ниже — своя подсказка элемента (если она его не повторяет).
std::string fullTextWith(std::string_view full, std::string_view own) {
  if (own.empty()) return std::string(full);
  if (own.find(full) != std::string_view::npos) return std::string(own);
  return std::string(full) + "\n" + std::string(own);
}

// Подсказка над r без захвата наведения и нажатий: указатель над r в окне под указателем и в отсечении (у
// недоступного поля и поверх строки таблицы — тоже). tid — устойчивый ID подсказки; последний элемент не меняется.
void fullTextTip(WidgetId tid, RectF r, std::string_view text) {
  Ctx& c = C();
  if (text.empty() || !c.win || c.win->hidden || c.win != c.hoveredWin) return;
  if (!r.contains(c.m.x, c.m.y) || !currentClip().contains(c.m.x, c.m.y)) return;
  const Item keep = c.last;
  c.last = Item{};
  c.last.id = tid;
  c.last.rect = r;
  c.last.hovered = true;
  tooltip(text);
  c.last = keep;
}

}  // namespace in

// Подсказка над прямоугольником r без захвата наведения и нажатий: подпись своей отрисовки поверх строки таблицы
// (щелчок по строке по-прежнему выделяет её). key — ключ в текущей области ID.
void hoverTip(std::string_view key, RectF r, std::string_view text) { in::fullTextTip(id(key) ^ 0x407e7ull, r, text); }

bool beginTooltipOver(std::string_view key, RectF r, float width) {
  Ctx& c = C();
  if (!c.win || c.win->hidden || c.win != c.hoveredWin) return false;
  if (!r.contains(c.m.x, c.m.y) || !in::currentClip().contains(c.m.x, c.m.y)) return false;
  const Item keep = c.last;
  c.last = Item{};
  c.last.id = id(key) ^ 0x7177e5ull;
  c.last.rect = r;
  c.last.hovered = true;
  const bool open = beginTooltip(width);
  c.last = keep;
  return open;
}

}  // namespace rg::ui
