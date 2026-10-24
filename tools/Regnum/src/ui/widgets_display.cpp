// Regnum — показатели и отображение: бейджи, теги, фишки, полосы, индикатор, круговая диаграмма, график, плитки
// показателей, аватары, флаги, пустые состояния, пункты списка, ожидание.
#include "gfx/flag.h"
#include "ui/ui_internal.h"

namespace rg::ui {

using namespace in;

namespace {

std::string initialsOf(std::string_view name) {
  std::string out;
  std::string_view s = name;
  bool take = true;
  size_t i = 0;
  int n = 0;
  while (i < s.size() && n < 2) {
    u32 cp = utf8::decode(s, i);
    bool word = utf8::isWordChar(cp);
    if (word && take) {
      utf8::append(out, utf8::upperCp(cp));
      n++;
      take = false;
    } else if (!word) {
      take = true;
    }
  }
  return out;
}

}  // namespace

// ================================================================ отображение
void badge(std::string_view txt, Tone tone, bool solid) {
  Ctx& c = C();
  const Theme& t = c.th;
  const auto st = styleWith(Font::Caption, gfx::FontWeight::Semibold);
  std::string_view s = displayText(txt);
  float w = std::max(18.f, textWidth(s, st) + 12);
  RectF r = place(w, 18);
  Color tc = tone == Tone::Neutral ? t.textDim : toneColor(tone);
  if (solid) {
    cmdRect(r, tc, 9);
    textIn(s, r, st, tone == Tone::Accent ? t.onAccent : Color(255, 255, 255), Align::Center);
  } else {
    cmdRect(r, tone == Tone::Neutral ? t.surface3 : tc.alpha(0.18f), 9);
    textIn(s, r, st, tc, Align::Center);
  }
  c.last = Item{};
  c.last.rect = r;
}

void tag(std::string_view txt, Tone tone, const char* icon) {
  Ctx& c = C();
  const Theme& t = c.th;
  const auto st = styleWith(Font::Small, gfx::FontWeight::Semibold);
  std::string_view s = displayText(txt);
  float w = textWidth(s, st) + 16 + (icon ? 18 : 0);
  RectF r = place(w, 22);
  Color tc = tone == Tone::Neutral ? t.textDim : toneColor(tone);
  if (tone == Tone::Neutral) {
    cmdRect(r, t.surface3, 6);
    cmdStroke(r, t.border, 6, 1);
  } else {
    cmdRect(r, tc.alpha(0.14f), 6);
    cmdStroke(r, tc.alpha(0.32f), 6, 1);
  }
  float x = r.x + 8;
  if (icon) {
    cmdIcon(icon, RectF{x, r.cy() - 7, 14, 14}, tc);
    x += 18;
  }
  textIn(s, RectF{x, r.y, r.right() - x - 6, r.h}, st, tone == Tone::Neutral ? t.text : tc);
  // ID наведения без захвата (как у fullTextTip): ui::tooltip и beginTooltip сразу после метки показывают подсказку.
  c.last = Item{};
  c.last.id = hashMix(id(txt), 0x7a91ull);
  c.last.rect = r;
  c.last.hovered = c.win && !c.win->hidden && c.win == c.hoveredWin && r.contains(c.m.x, c.m.y) && currentClip().contains(c.m.x, c.m.y);
}

ChipAction chip(std::string_view lbl, const ChipOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  const auto& st = styleOf(Font::Small);
  std::string_view s = displayText(lbl);
  bool lead = o.icon || o.color.a > 0;
  float w = textWidth(s, st) + 20 + (lead ? 16 : 0) + (o.removable ? 18 : 0);
  RectF r = place(w, 26);
  if (r.h > 26) r = RectF{r.x, r.y + std::round((r.h - 26) * 0.5f), r.w, 26};
  WidgetId wid = id(lbl);
  bool interactive = o.clickable || o.removable || !o.tooltip.empty();
  // Крестик получает нажатие раньше фишки.
  RectF xr{r.right() - 22, r.cy() - 8, 16, 16};
  WidgetId xid = wid ^ 0xc42ull;
  Interaction xi{};
  if (o.removable) xi = interact(xid, xr);
  Interaction it{};
  if (interactive) it = interact(wid, r, o.clickable ? IfFocusable : IfNone);
  float hv = interactive ? animate(wid ^ 0xc41ull, it.hovered ? 1.f : 0.f) : 0.f;
  Color tc = o.tone == Tone::Neutral ? t.text : toneColor(o.tone);
  Color bg = o.tone == Tone::Neutral ? t.surface3 : toneColor(o.tone).alpha(0.14f);
  Color bc = o.tone == Tone::Neutral ? t.border : toneColor(o.tone).alpha(0.32f);
  if (o.selected) {
    bg = t.accent.alpha(0.14f);
    bc = t.accent.alpha(0.6f);
  }
  if (o.glow.a > 0) {   // подсветка (редкость реликвии): тон фона, цветная рамка и мягкий ореол
    bg = mixc(t.surface3, o.glow.withA(255), 0.16f);
    bc = o.glow.alpha(0.8f);
    cmdStroke(r.expand(1.5f), o.glow.alpha(0.22f), 14.5f, 2);
  }
  if (o.clickable) bc = mixc(bc, t.borderStrong, hv * (o.glow.a > 0 ? 0.35f : 1.f));
  cmdRect(r, bg, 13);
  cmdStroke(r, bc, 13, 1);
  float x = r.x + 10;
  if (o.color.a > 0) {
    draw::circle(x + 4, r.cy(), 4.5f, o.color);
    draw::ring(x + 4, r.cy(), 4.5f, 1, Color(0, 0, 0, 70));
    x += 14;
  } else if (o.icon) {
    cmdIcon(o.icon, RectF{x - 1, r.cy() - 7, 14, 14}, o.glow.a > 0 ? o.glow.withA(255) : o.tone == Tone::Neutral ? t.textDim : tc);
    x += 16;
  }
  float right = r.right() - 10 - (o.removable ? 16 : 0);
  textIn(s, RectF{x, r.y, right - x, r.h}, st, tc);
  ChipAction act = ChipAction::None;
  if (o.removable) {
    float xh = animate(xid ^ 1, xi.hovered ? 1.f : 0.f);
    if (xh > 0.01f) draw::circle(xr.cx(), xr.cy(), 8, t.danger.alpha(0.18f * xh));
    cmdIcon("close", xr.inset(2.5f), mixc(t.textMuted, t.danger, xh));
    if (xi.clicked) act = ChipAction::Remove;
  }
  if (act == ChipAction::None && o.clickable && it.clicked) act = ChipAction::Click;
  if (o.clickable && it.hovered) c.cursor = platform::Cursor::Hand;
  setLast(wid, r, it);
  if (o.clickable) focusRing(r, 13);
  if (!o.tooltip.empty()) tooltip(o.tooltip);
  return act;
}

void progress(double v, const ProgressOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  double k = std::isfinite(v) ? clamp(v, 0.0, 1.0) : 0.0;
  std::string lbl = !o.text.empty() ? std::string(o.text) : (o.label ? fmtPct(k * 100) : std::string());
  const auto& st = styleOf(Font::Small);
  float lw = lbl.empty() ? 0 : std::max(36.f, textWidth(lbl, st) + 4);
  float h = std::max(o.height, lbl.empty() ? o.height : lineHeight(Font::Small));
  RectF r = place(o.width, h);
  RectF tr{r.x, r.cy() - o.height * 0.5f, r.w - (lw > 0 ? lw + 8 : 0), o.height};
  Color col = o.color.a ? o.color : toneColor(o.tone);
  cmdRect(tr, t.track, o.height * 0.5f);
  if (k > 0) {
    RectF fr{tr.x, tr.y, std::max(o.height, tr.w * float(k)), tr.h};
    draw::gradient(fr, col.lighten(0.08f), col, o.height * 0.5f, true);
  }
  if (lw > 0) textIn(lbl, RectF{tr.right() + 8, r.y, lw, r.h}, st, t.textDim, Align::Right);
  c.last = Item{};
  c.last.rect = r;
}

void meter(double value, const MeterOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  double mn = o.min, mx = o.max > o.min ? o.max : o.min + 1;
  double v = std::isfinite(value) ? clamp(value, mn, mx) : 0;
  std::string lbl = o.label ? fmtSigned(v) : std::string();
  const auto st = styleWith(Font::Small, gfx::FontWeight::Semibold);
  float lw = lbl.empty() ? 0 : std::max(34.f, textWidth(fmtSigned(mx), st) + 6);
  RectF r = place(o.width, std::max(o.height + 6, 18.f));
  RectF tr{r.x, r.cy() - o.height * 0.5f, r.w - (lw > 0 ? lw + 8 : 0), o.height};
  cmdRect(tr, t.track, o.height * 0.5f);
  float zero = clamp(float((0 - mn) / (mx - mn)), 0.f, 1.f);
  float zx = tr.x + tr.w * zero;
  float vx = tr.x + tr.w * float((v - mn) / (mx - mn));
  Color col = v >= 0 ? t.success : t.danger;
  if (std::fabs(vx - zx) > 0.5f) {
    RectF fr{std::min(zx, vx), tr.y, std::fabs(vx - zx), tr.h};
    bool pos = vx > zx;
    float rr = o.height * 0.5f;
    cmdRect4(fr, col, pos ? 0 : rr, pos ? rr : 0, pos ? rr : 0, pos ? 0 : rr);
  }
  cmdRect(RectF{zx - 1, r.cy() - o.height * 0.5f - 3, 2, o.height + 6}, t.textMuted, 1);
  if (lw > 0) textIn(lbl, RectF{tr.right() + 8, r.y, lw, r.h}, st, std::fabs(v) < 1e-9 ? t.textDim : col, Align::Right);
  c.last = Item{};
  c.last.rect = r;
}

static int pieImpl(std::span<const Slice> slices, const PieOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  float sz = std::max(16.f, o.size);
  float legendW = 0;
  const auto& lst = styleOf(Font::Small);
  double sum = 0;
  for (const Slice& s : slices) sum += std::max(0.0, s.value);
  if (o.legend) {
    for (const Slice& s : slices) legendW = std::max(legendW, textWidth(displayText(s.label), lst) + 60);
    legendW = std::min(legendW + 16, 240.f);
  }
  float lh = 22;
  float legendH = o.legend ? lh * float(slices.size()) : 0;
  RectF r = place(o.legend ? 0 : sz, std::max(sz, legendH));
  WidgetId wid = autoId("pie");
  float cx = r.x + sz * 0.5f, cy = r.y + r.h * 0.5f;
  float R = sz * 0.5f - 3;
  float inner = o.thickness > 0 ? std::max(0.f, R - o.thickness) : 0;
  // Наведённый сектор
  int hovered = -1;
  float dxm = c.m.x - cx, dym = c.m.y - cy;
  float dm = std::sqrt(dxm * dxm + dym * dym);
  bool over = c.win == c.hoveredWin && dm <= R + 3 && dm >= inner - 2 && currentClip().contains(c.m.x, c.m.y) && c.active == 0;
  double ang = std::atan2(dym, dxm) + kPi * 0.5;
  if (ang < 0) ang += 2 * kPi;
  if (sum <= 0) {
    draw::ring(cx, cy, (R + inner) * 0.5f, std::max(2.f, R - inner), t.track);
  } else {
    double a0 = 0;
    for (size_t i = 0; i < slices.size(); i++) {
      double frac = std::max(0.0, slices[i].value) / sum;
      double a1 = a0 + frac * 2 * kPi;
      if (over && ang >= a0 && ang < a1) hovered = int(i);
      a0 = a1;
    }
    a0 = 0;
    float gapPx = slices.size() > 1 ? 1.2f : 0;
    for (size_t i = 0; i < slices.size(); i++) {
      double frac = std::max(0.0, slices[i].value) / sum;
      double a1 = a0 + frac * 2 * kPi;
      if (frac <= 0) {
        a0 = a1;
        continue;
      }
      float grow = animate(hashMix(wid, i + 1), int(i) == hovered ? 1.f : 0.f, 0.12f);
      float Ro = R + 2.5f * grow, Ri = inner > 0 ? inner : 0;
      double ga = Ro > 0 ? gapPx / Ro : 0, gi = Ri > 0 ? gapPx / Ri : 0;
      double s0 = a0 + ga * 0.5, s1 = a1 - ga * 0.5;
      if (s1 <= s0) {
        s0 = a0;
        s1 = a1;
      }
      gfx::Path p;
      int segs = std::max(2, int((s1 - s0) / (kPi / 60)) + 1);
      for (int k = 0; k <= segs; k++) {
        double a = s0 + (s1 - s0) * k / segs - kPi * 0.5;
        float x = cx + Ro * float(std::cos(a)), y = cy + Ro * float(std::sin(a));
        if (k == 0) p.moveTo(x, y);
        else p.lineTo(x, y);
      }
      if (Ri > 0) {
        double i0 = a0 + gi * 0.5, i1 = a1 - gi * 0.5;
        if (i1 <= i0) {
          i0 = a0;
          i1 = a1;
        }
        for (int k = segs; k >= 0; k--) {
          double a = i0 + (i1 - i0) * k / segs - kPi * 0.5;
          p.lineTo(cx + Ri * float(std::cos(a)), cy + Ri * float(std::sin(a)));
        }
      } else {
        p.lineTo(cx, cy);
      }
      p.close();
      Color col = slices[i].color;
      if (hovered >= 0 && int(i) != hovered) col = col.alpha(0.55f);
      draw::path(p, col);
      a0 = a1;
    }
  }
  if (inner > 12 && (!o.centerValue.empty() || !o.centerLabel.empty())) {
    Font vf = inner > 34 ? Font::Title : Font::Strong;
    float vh = lineHeight(vf), ch = o.centerLabel.empty() ? 0 : lineHeight(Font::Caption);
    float y0 = cy - (vh + ch) * 0.5f;
    std::string hv;
    std::string_view cv = o.centerValue, cl = o.centerLabel;
    if (hovered >= 0 && sum > 0) {
      hv = fmtPct(std::max(0.0, slices[size_t(hovered)].value) / sum * 100);
      cv = hv;
      cl = displayText(slices[size_t(hovered)].label);
    }
    textIn(cv, RectF{cx - inner, y0, inner * 2, vh}, styleOf(vf), t.text, Align::Center);
    if (ch > 0 || !cl.empty()) textIn(cl, RectF{cx - inner + 6, y0 + vh, inner * 2 - 12, lineHeight(Font::Caption)}, styleOf(Font::Caption), t.textMuted, Align::Center);
  }
  if (o.legend && !slices.empty()) {
    float lx = r.x + sz + 16;
    float ly = r.y + (r.h - legendH) * 0.5f;
    float lwid = r.right() - lx;
    for (size_t i = 0; i < slices.size(); i++) {
      RectF row{lx, ly + lh * float(i), lwid, lh};
      bool hi = int(i) == hovered;
      if (hi) cmdRect(row.inset(-4, 0), t.hover, 5);
      draw::circle(row.x + 5, row.cy(), 4.5f, slices[i].color);
      std::string pct = sum > 0 ? fmtPct(std::max(0.0, slices[i].value) / sum * 100) : std::string("—");
      const auto pst = styleWith(Font::Small, gfx::FontWeight::Semibold);
      float pw = textWidth(pct, pst);
      textIn(displayText(slices[i].label), RectF{row.x + 16, row.y, row.w - 24 - pw, row.h}, lst, hi ? t.text : t.textDim);
      textIn(pct, RectF{row.right() - pw, row.y, pw, row.h}, pst, t.text, Align::Right);
    }
  }
  c.last = Item{};
  c.last.rect = r;
  if (hovered >= 0) {
    c.last.hovered = true;
    c.last.id = hashMix(wid, u64(hovered) + 100);
    if (o.tooltips && !o.legend) {
      std::string tip = std::string(displayText(slices[size_t(hovered)].label)) + " — " + fmtPct(std::max(0.0, slices[size_t(hovered)].value) / sum * 100);
      tooltip(tip);
    }
  }
  return hovered;
}

int pie(std::span<const Slice> slices, const PieOpt& o) { return pieImpl(slices, o); }
int pie(std::initializer_list<Slice> slices, const PieOpt& o) { return pieImpl(std::span<const Slice>(slices.begin(), slices.size()), o); }

void sparkline(std::span<const float> values, const SparkOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  RectF r = place(o.width, o.height);
  c.last = Item{};
  c.last.rect = r;
  if (values.size() < 2) {
    draw::line(r.x, r.cy(), r.right(), r.cy(), t.border, 1);
    return;
  }
  float mn = values[0], mx = values[0];
  for (float v : values) {
    if (!std::isfinite(v)) continue;
    mn = std::min(mn, v);
    mx = std::max(mx, v);
  }
  if (mx - mn < 1e-6f) {
    mn -= 1;
    mx += 1;
  }
  Color col = o.color.a ? o.color : t.accent;
  RectF a = r.inset(3, 3);
  auto P = [&](size_t i) {
    float v = std::isfinite(values[i]) ? values[i] : mn;
    return gfx::Pt{a.x + a.w * float(i) / float(values.size() - 1), a.bottom() - a.h * (v - mn) / (mx - mn)};
  };
  gfx::Path line;
  for (size_t i = 0; i < values.size(); i++) {
    gfx::Pt p = P(i);
    if (i == 0) line.moveTo(p.x, p.y);
    else line.lineTo(p.x, p.y);
  }
  if (o.fill) {
    gfx::Path area = line;
    area.lineTo(a.right(), r.bottom());
    area.lineTo(a.x, r.bottom());
    area.close();
    draw::path(area, col.alpha(0.14f));
  }
  draw::pathStroke(line, col, 1.6f);
  if (o.dot) {
    gfx::Pt p = P(values.size() - 1);
    draw::circle(p.x, p.y, 4.5f, col.alpha(0.25f));
    draw::circle(p.x, p.y, 2.5f, col);
  }
}

void stat(std::string_view value, std::string_view lbl, const StatOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  RectF r = place(0, 64);
  WidgetId wid = id(lbl) ^ 0x57a7ull;
  Interaction it{};
  if (!o.tooltip.empty()) it = interact(wid, r);
  float hv = o.tooltip.empty() ? 0.f : animate(wid ^ 1, it.hovered ? 1.f : 0.f);
  Color bg = Color(255, 255, 255, u8(7 + 5 * hv));
  Color bc = Color(255, 255, 255, 11);
  cmdRect(r, bg, t.radiusCard);
  cmdStroke(r, bc, t.radiusCard, 1);
  float x = r.x + 12;
  Color tc = toneColor(o.tone);
  if (o.icon) {
    float d = 36;
    RectF ic{x, r.cy() - d * 0.5f, d, d};
    cmdRect(ic, tc.alpha(0.15f), 10);
    cmdIcon(o.icon, ic.inset(9), tc);
    x += d + 12;
  }
  const auto& vst = styleOf(Font::Number);
  // Мини-график — только если число и подпись помещаются целиком.
  float need = std::max(textWidth(value, vst), textWidth(displayText(lbl), styleOf(Font::Small)));
  float sparkW = o.spark.size() >= 2 ? std::min(72.f, r.right() - 12 - x - need - 12) : 0;
  if (sparkW < 40) sparkW = 0;
  float right = r.right() - 12 - (sparkW > 0 ? sparkW + 8 : 0);
  float vh = lineHeight(Font::Number), lh = lineHeight(Font::Small);
  float y0 = r.cy() - (vh + lh - 2) * 0.5f;
  float vw = std::min(textWidth(value, vst), right - x);
  textIn(value, RectF{x, y0, vw + 1, vh}, vst, t.text);
  bool hasDelta = o.delta != 0 || !o.deltaText.empty();
  if (hasDelta) {
    bool up = o.delta > 0 || (o.delta == 0 && !o.deltaText.empty() && o.deltaText[0] != '-' && !startsWith(o.deltaText, "−"));
    bool good = o.invertDelta ? !up : up;
    Color dc = o.delta == 0 && o.deltaText.empty() ? t.textMuted : (good ? t.success : t.danger);
    std::string dt = !o.deltaText.empty() ? std::string(o.deltaText) : fmtSigned(o.delta);
    const auto dst = styleWith(Font::Caption, gfx::FontWeight::Semibold);
    float dw = textWidth(dt, dst) + 22;
    float dx = x + vw + 8;
    if (dx + dw <= right + 4) {
      RectF dr{dx, y0 + (vh - 18) * 0.5f + 1, dw, 18};
      cmdRect(dr, dc.alpha(0.14f), 9);
      cmdIcon(up ? "trend-up" : "trend-down", RectF{dr.x + 5, dr.cy() - 6, 12, 12}, dc);
      textIn(dt, RectF{dr.x + 18, dr.y, dw - 20, dr.h}, dst, dc);
    }
  }
  textIn(displayText(lbl), RectF{x, y0 + vh - 2, right - x, lh}, styleOf(Font::Small), t.textDim);
  if (sparkW > 0) {
    RectF sr{r.right() - 12 - sparkW, r.cy() - 14, sparkW, 28};
    at(sr);
    SparkOpt so;
    so.height = 28;
    so.color = tc;
    sparkline(o.spark, so);
  }
  setLast(wid, r, it);
  if (!o.tooltip.empty()) tooltip(o.tooltip);
}

void avatar(std::string_view name, const AvatarOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  float s = std::max(12.f, o.size);
  float extra = o.ring ? 6 : 0;
  RectF r = place(s + extra, s + extra);
  RectF a{r.x + extra * 0.5f, r.y + std::round((r.h - s) * 0.5f), s, s};
  if (o.ring) draw::ring(a.cx(), a.cy(), s * 0.5f + 2.5f, 1.75f, t.accent);
  if (o.image && !o.image->empty()) {
    draw::image(*o.image, a, s * 0.5f);
  } else {
    Color bg = o.color.a ? o.color : Color::hsl(double(hash64(name) % 360), 0.32, 0.36);
    draw::circle(a.cx(), a.cy(), s * 0.5f, bg);
    std::string ini = initialsOf(name);
    gfx::TextStyle st = styleWith(Font::Body, gfx::FontWeight::Semibold);
    st.size = std::round(s * 0.4f);
    textIn(ini, a, st, bg.textOn(), Align::Center);
  }
  WidgetId wid = id(name) ^ 0xa7a7ull;
  Interaction it{};
  if (!o.tooltip.empty()) it = interact(wid, r);
  setLast(wid, r, it);
  if (!o.tooltip.empty()) tooltip(o.tooltip);
}

void flag(const Flag& f, float w, float h, float radius, std::string_view tip) {
  float fh = h > 0 ? h : std::round(w * 2.f / 3.f);
  RectF r = place(w, fh);
  if (r.w > w) r = RectF{r.x, r.y, w, r.h};
  if (r.h > fh) r = RectF{r.x, r.y + std::round((r.h - fh) * 0.5f), r.w, fh};
  const Flag* fp = &f;
  float rad = radius;
  float k = alphaMul();
  custom(r, [fp, rad, k](gfx::Canvas& cv, RectF d, float s) {
    cv.save();
    if (k < 1) cv.setOpacity(k);
    gfx::drawFlag(cv, *fp, d, rad * s, true);
    cv.restore();
  });
  WidgetId wid = autoId("flag");
  Interaction it{};
  if (!tip.empty()) it = interact(wid, r);
  setLast(wid, r, it);
  if (!tip.empty()) tooltip(tip);
}

bool emptyState(const char* icon, std::string_view txt, std::string_view action, const char* actionIcon) {
  Ctx& c = C();
  const Theme& t = c.th;
  RectF a = c.oneShot ? *c.oneShot : avail();
  float tw = std::min(a.w - 24, 300.f);
  TextEntry* te = cachedText(displayText(txt), styleOf(Font::Body), tw, 3, true, Align::Center, true);
  float h = 52 + 12 + te->layout.height + (action.empty() ? 0 : 12 + 30) + 16;
  RectF r = place(0, h);
  float cx = r.cx();
  float y = r.y + 8;
  draw::circle(cx, y + 26, 26, t.surface3);
  draw::ring(cx, y + 26, 25.5f, 1, t.border);
  cmdIcon(icon ? icon : "info", RectF{cx - 12, y + 14, 24, 24}, t.textMuted);
  y += 52 + 12;
  cmdText(te, r.x + (r.w - tw) * 0.5f, y, t.textDim);
  y += te->layout.height + 12;
  bool clicked = false;
  if (!action.empty()) {
    float bw = textWidth(displayText(action), styleOf(Font::Strong)) + 28 + (actionIcon ? 23 : 0);
    at(RectF{cx - bw * 0.5f, y, bw, 30});
    ButtonOpt bo;
    bo.icon = actionIcon;
    // Своя область ID: кнопка пустого состояния часто повторяет подпись кнопки над списком («Новая сделка»).
    IdScope es("##emptyState");
    clicked = button(action, bo);
  }
  c.last.rect = r;
  return clicked;
}

bool listItem(std::string_view lbl, const ListItemOpt& o) {
  Ctx& c = C();
  const Theme& t = c.th;
  DisableGuard dg(o.disabled);
  bool disabled = isDisabled();
  float h = o.subtitle.empty() ? 32.f : 44.f;
  RectF r = place(0, h);
  WidgetId wid = id(lbl);
  Interaction it = interact(wid, r, disabled ? IfNone : IfFocusable);
  float hv = animate(wid ^ 0x115e1ull, it.hovered ? 1.f : 0.f, 0.1f);
  float rad = 7;
  if (o.selected) {
    cmdRect(r, t.accent.alpha(0.14f), rad);
    cmdRect4(RectF{r.x, r.y + 7, 3, r.h - 14}, t.accent, 0, 2, 2, 0);
  } else if (hv > 0.01f) {
    cmdRect(r, (it.held ? t.pressed : t.hover).alpha(std::min(1.f, hv * 1.4f)), rad);
  }
  float x = r.x + 12;
  if (o.dot.a > 0) {
    draw::circle(x + 5, r.cy(), 5, o.dot);
    draw::ring(x + 5, r.cy(), 5, 1, Color(0, 0, 0, 80));
    x += 20;
  } else if (o.icon) {
    cmdIcon(o.icon, RectF{x, r.cy() - 8, 16, 16}, o.selected ? t.accent : mixc(t.textDim, t.text, hv));
    x += 26;
  }
  float right = r.right() - 10;
  if (!o.badge.empty()) {
    const auto bs = styleWith(Font::Caption, gfx::FontWeight::Semibold);
    float bw = std::max(18.f, textWidth(o.badge, bs) + 12);
    RectF br{right - bw, r.cy() - 9, bw, 18};
    cmdRect(br, o.selected ? t.accent.alpha(0.22f) : t.surface3, 9);
    textIn(o.badge, br, bs, o.selected ? t.accent : t.textDim, Align::Center);
    right = br.x - 8;
  }
  if (!o.hint.empty()) {
    const auto& hs = styleOf(Font::Small);
    float hw = std::min(textWidth(o.hint, hs), (right - x) * 0.45f);
    textIn(o.hint, RectF{right - hw, r.y, hw, r.h}, hs, t.textMuted, Align::Right);
    right -= hw + 10;
  }
  const gfx::TextStyle st = o.selected ? styleWith(Font::Body, gfx::FontWeight::Semibold) : styleOf(Font::Body);
  if (o.subtitle.empty()) {
    textIn(displayText(lbl), RectF{x, r.y, right - x, r.h}, st, disabled ? t.textMuted : t.text);
  } else {
    float lh = lineHeight(Font::Body), sh = lineHeight(Font::Small);
    float y0 = r.cy() - (lh + sh) * 0.5f;
    textIn(displayText(lbl), RectF{x, y0, right - x, lh}, st, disabled ? t.textMuted : t.text);
    textIn(o.subtitle, RectF{x, y0 + lh, right - x, sh}, styleOf(Font::Small), t.textMuted);
  }
  setLast(wid, r, it);
  focusRing(r, rad);
  if (!o.tooltip.empty()) tooltip(o.tooltip);
  return it.clicked && !disabled;
}

void spinner(float size, Tone tone) {
  Ctx& c = C();
  float s = std::max(8.f, size);
  RectF r = place(s, s);
  c.last = Item{};
  c.last.rect = r;
  float cx = r.cx(), cy = r.cy(), rad = s * 0.5f - 1.5f;
  float w = std::max(1.5f, s / 9.f);
  draw::ring(cx, cy, rad, w, c.th.track);
  double a0 = std::fmod(c.time * 2 * kPi * 0.9, 2 * kPi);
  double sweep = kPi * (0.55 + 0.35 * std::sin(c.time * 2.4));
  gfx::Path p;
  int n = 24;
  for (int i = 0; i <= n; i++) {
    double a = a0 + sweep * i / n;
    float x = cx + rad * float(std::cos(a)), y = cy + rad * float(std::sin(a));
    if (i == 0) p.moveTo(x, y);
    else p.lineTo(x, y);
  }
  draw::pathStroke(p, toneColor(tone), w);
  c.animating = true;
}

}  // namespace rg::ui
