// Regnum — дерево технологий: отрисовка схемы (карточки, связи, плашки условий из общего дерева) в ui::custom.
#include "app/editors/techtree_internal.h"

namespace rg::app::techtree {

// Плашки внешних условий технологии t — по одной на условие, по центру входа.
std::vector<Ext> extsOf(const World& w, const std::vector<TN>& ts, const TN& t, Id faction) {
  std::vector<Ext> out;
  for (Id p : t.prereqs) {
    if (find(ts, p)) continue;
    const Tech* pt = w.tech(p);
    if (!pt) continue;
    Ext e;
    e.pre = p;
    e.tech = t.id;
    e.label = w::orName(pt->name);
    e.st = stateOf(w, p, faction);
    out.push_back(std::move(e));
  }
  const double h = 24, gap = 6;
  double y0 = t.pos.y + kH * 0.5 - (double(out.size()) * h + double(out.size() > 0 ? out.size() - 1 : 0) * gap) * 0.5;
  gfx::TextStyle st = textStyle(11.5f);
  for (size_t i = 0; i < out.size(); i++) {
    Ext& e = out[i];
    double tw = std::min(150.0, double(gfx::measureText(e.label, st)));
    double wd = tw + 26 + 12;
    e.port = {t.pos.x, t.pos.y + kH * 0.5};
    e.pill = RectF{float(t.pos.x - 40 - wd), float(y0 + double(i) * (h + gap)), float(wd), float(h)};
  }
  return out;
}

namespace {

void drawArc(gfx::Canvas& c, float cx, float cy, float r, float frac, float width, Color col) {
  if (frac <= 0) return;
  gfx::Path p;
  int n = std::max(8, int(48 * frac));
  for (int i = 0; i <= n; i++) {
    float a = float(-kPi / 2 + 2 * kPi * double(frac) * double(i) / double(n));
    float x = cx + r * std::cos(a), y = cy + r * std::sin(a);
    if (i == 0) p.moveTo(x, y);
    else p.lineTo(x, y);
  }
  gfx::Stroke st;
  st.width = width;
  st.cap = gfx::Cap::Round;
  st.join = gfx::Join::Round;
  c.strokePath(p, st, gfx::Paint(col));
}

void drawCard(gfx::Canvas& c, const Scene& s, const Card& cd) {
  const Ink& k = s.k;
  const TN& t = cd.t;
  const float z = float(s.cam.z);
  const float px = 1 / z;   // одна точка интерфейса в единицах схемы
  RectF r{float(t.pos.x), float(t.pos.y), kW, kH};
  Color tone = stColor(k, t.st);
  c.save();
  if (cd.dim) c.setOpacity(0.28f);
  // Тень и подложка
  c.boxShadow(r, 12, cd.sel || cd.hover ? 26 : 14, 0, k.shadow.alpha(cd.sel || cd.hover ? 0.95f : 0.7f), gfx::Pt{0, 5});
  float tint = t.st == St::Locked ? 0.0f : 0.11f;
  gfx::Gradient g;
  g.kind = gfx::Gradient::Linear;
  g.p0 = {r.x, r.y};
  g.p1 = {r.x, r.bottom()};
  g.stops = {{0.f, Color::mix(k.surface, tone, tint)}, {1.f, k.surface}};
  gfx::Paint gp;
  gp.gradient = &g;
  c.fillRoundRect(r, 12, gp);
  // Полоска состояния сверху
  c.save();
  c.clipRoundRect(r, 12);
  c.fillRect(RectF{r.x, r.y, r.w, 3}, tone.alpha(t.st == St::Locked ? 0.35f : 0.9f));
  c.restore();
  Color bc = cd.sel ? k.accent : cd.hover ? Color::mix(k.borderStrong, tone, 0.45f) : Color::mix(k.border, tone, t.st == St::Locked ? 0.f : 0.3f);
  c.strokeRoundRect(r.inset(0.5f * px), 12, (cd.sel ? 1.6f : 1.f) * px, bc);
  if (cd.sel) c.strokeRoundRect(r.expand(4 * px), 12 + 4 * px, 2 * px, k.accent.alpha(0.45f));
  if (cd.drop) c.strokeRoundRect(r.expand(4 * px), 12 + 4 * px, 2.2f * px, (cd.dropBad ? k.danger : k.success).alpha(0.9f));

  // Медальон состояния (у исследуемой — кольцо хода)
  float mx = r.x + 28, my = r.y + 30;
  c.fillCircle(mx, my, 17, tone.alpha(t.st == St::Locked ? 0.12f : 0.17f));
  if (t.st == St::Research) {
    c.strokeCircle(mx, my, 17, 2.4f, k.surfaceHi);
    drawArc(c, mx, my, 17, clamp(float(t.progress) / float(std::max(1, t.need)), 0.f, 1.f), 2.4f, tone);
  }
  icon(c, stIcon(t.st), RectF{mx - 9, my - 9, 18, 18}, tone);

  // Название, срок, модификаторы
  Color nameCol = t.st == St::Locked ? k.textDim : k.text;
  text(c, t.name.empty() ? "Без названия" : t.name, textStyle(14, gfx::FontWeight::Semibold), RectF{r.x + 54, r.y + 11, kW - 54 - 40, 20}, nameCol);
  {
    float x = r.x + 54, y = r.y + 33;
    icon(c, "hourglass", RectF{x, y + 1, 13, 13}, k.textMuted);
    std::string tt = t.st == St::Research ? "ещё " + nTurns(turnsLeft(t.need, t.progress)) : nTurns(t.need);
    gfx::TextStyle st = textStyle(11.5f);
    float tw = gfx::measureText(tt, st);
    text(c, tt, st, RectF{x + 17, y, tw + 2, 15}, k.textMuted);
    x += 17 + tw + 10;
    if (!t.mods.empty()) {
      icon(c, "sparkles", RectF{x, y + 1, 13, 13}, k.accent.alpha(0.9f));
      std::string mt = std::to_string(t.mods.size());
      text(c, mt, st, RectF{x + 16, y, 24, 15}, k.textMuted);
      x += 16 + gfx::measureText(mt, st) + 10;
    }
    // Стоимость исследования: значок ресурса и количество (сколько поместится до края карточки).
    const float right = r.right() - 12;
    for (const PriceTag& p : cd.price) {
      const float pw = gfx::measureText(p.text, st);
      if (x + 16 + pw > right) break;
      icon(c, p.icon, RectF{x, y + 1, 13, 13}, p.col);
      text(c, p.text, st, RectF{x + 16, y, pw + 2, 15}, k.textMuted);
      x += 16 + pw + 10;
    }
  }
  // «Галочка» изученности (ТЗ 1.b.v)
  {
    RectF cb{r.right() - 31, r.y + 13, 18, 18};
    if (t.studied) {
      c.fillRoundRect(cb, 5, k.success);
      icon(c, "check", cb.inset(2), k.success.textOn());
    } else {
      if (cd.checkHot && !s.readOnly) c.fillRoundRect(cb, 5, k.accent.alpha(0.18f));
      c.strokeRoundRect(cb.inset(0.6f * px), 5, 1.4f * px, cd.checkHot && !s.readOnly ? k.accent : k.borderStrong);
    }
  }
  // Бонус и ход исследования
  gfx::TextStyle small = textStyle(11.5f);
  if (t.st == St::Research || (t.progress > 0 && !t.studied)) {
    std::string b = cd.bonus.empty() ? std::string("Без бонуса") : cd.bonus;
    text(c, b, small, RectF{r.x + 14, r.y + 56, kW - 28, 16}, cd.bonus.empty() ? k.textMuted : k.textDim);
    RectF bar{r.x + 14, r.y + 79, kW - 28 - 34, 5};
    c.fillRoundRect(bar, 2.5f, k.surfaceHi);
    float fr = clamp(float(t.progress) / float(std::max(1, t.need)), 0.f, 1.f);
    if (fr > 0) c.fillRoundRect(RectF{bar.x, bar.y, std::max(5.f, bar.w * fr), bar.h}, 2.5f, t.research ? k.info : k.textMuted);
    text(c, std::to_string(t.progress) + "/" + std::to_string(t.need), small, RectF{bar.right() + 4, bar.y - 6, 30, 16}, k.textMuted, gfx::Align::Right);
  } else if (!cd.bonus.empty()) {
    text(c, cd.bonus, small, RectF{r.x + 14, r.y + 54, kW - 28, 34}, k.textDim, gfx::Align::Left, 2, gfx::VAlign::Top);
  } else if (!cd.need.empty()) {
    text(c, cd.need, small, RectF{r.x + 14, r.y + 54, kW - 28, 34}, k.textMuted, gfx::Align::Left, 2, gfx::VAlign::Top);
  } else {
    text(c, "Без бонуса", small, RectF{r.x + 14, r.y + 56, kW - 28, 16}, k.textMuted);
  }
  // Гнёзда связей
  Vec2 in{r.x, r.y + kH * 0.5}, out{r.right(), r.y + kH * 0.5};
  if (cd.hasIn || cd.inHot || cd.hover || cd.sel) drawPort(c, in, cd.inHot ? k.accent : Color::mix(k.borderStrong, tone, 0.5f), k.bg, cd.hasIn, cd.inHot, z);
  if (cd.hasOut || cd.outHot || cd.hover || cd.sel) drawPort(c, out, cd.outHot ? k.accent : Color::mix(k.borderStrong, tone, 0.5f), k.bg, cd.hasOut, cd.outHot, z);
  c.restore();
}

}  // namespace

void renderScene(const Scene& s, gfx::Canvas& c, RectF dev, float scale) {
  c.save();
  c.clipRoundRect(dev, 10 * scale);
  drawGrid(c, dev, scale, s.cam, s.k);
  applyCamera(c, dev, scale, s.cam);
  const float z = float(s.cam.z);
  for (const EdgeD& e : s.edges) {
    if (e.sel) drawCurve(c, e.k, s.k.accent.alpha(0.22f), e.width + 7, z, false);
    drawCurve(c, e.k, e.col, e.width, z, true);
  }
  // Условия из общего дерева: плашка цвета состояния, пунктир во вход карточки.
  for (const Ext& e : s.exts) {
    Color sc = stColor(s.k, e.st);
    Color col = e.hot ? s.k.accent : Color::mix(s.k.textMuted, sc, 0.5f);
    drawCurve(c, curve({e.pill.right(), e.pill.y + e.pill.h * 0.5}, e.port), col.alpha(0.85f), e.hot ? 2.6f : 1.8f, z, true, true);
    c.fillRoundRect(e.pill, e.pill.h * 0.5f, s.k.surface);
    c.strokeRoundRect(e.pill.inset(0.5f / z), e.pill.h * 0.5f, (e.hot ? 1.6f : 1.f) / z, e.hot ? s.k.accent : sc.alpha(0.6f));
    icon(c, stIcon(e.st), RectF{e.pill.x + 8, e.pill.y + (e.pill.h - 13) * 0.5f, 13, 13}, sc);
    text(c, e.label, textStyle(11.5f), RectF{e.pill.x + 26, e.pill.y, e.pill.w - 34, e.pill.h}, e.hot ? s.k.text : s.k.textDim);
  }
  for (const Card& cd : s.cards) drawCard(c, s, cd);
  if (s.pending) drawCurve(c, s.pend, s.pendCol, 2.2f, z, true, true);
  c.restore();
}

}  // namespace rg::app::techtree
