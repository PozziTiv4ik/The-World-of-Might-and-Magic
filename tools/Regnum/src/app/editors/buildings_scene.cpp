// Regnum — дерево построек: отрисовка схемы (дорожки, карточки, связи, плашки внешних требований) в ui::custom.
#include "app/editors/buildings_internal.h"

namespace rg::app::bldtree {

namespace {

void drawCard(gfx::Canvas& c, const Scene& s, const Card& cd) {
  const Ink& k = s.k;
  const BN& b = cd.b;
  const float z = float(s.cam.z), px = 1 / z;
  RectF r{float(cd.at.x), float(cd.at.y), kBW, kBH};
  Color cc = bld::catColor(b.cat);
  c.save();
  if (cd.dim) c.setOpacity(0.28f);
  c.boxShadow(r, 12, cd.sel || cd.hover || cd.ghost ? 28 : 14, 0, k.shadow.alpha(cd.sel || cd.hover || cd.ghost ? 0.95f : 0.7f), gfx::Pt{0, cd.ghost ? 10.f : 5.f});
  gfx::Gradient g;
  g.kind = gfx::Gradient::Linear;
  g.p0 = {r.x, r.y};
  g.p1 = {r.x, r.bottom()};
  g.stops = {{0.f, Color::mix(k.surface, cc, 0.09f)}, {1.f, k.surface}};
  gfx::Paint gp;
  gp.gradient = &g;
  c.fillRoundRect(r, 12, gp);
  Color bc = cd.sel ? k.accent : cd.hover ? Color::mix(k.borderStrong, cc, 0.4f) : Color::mix(k.border, cc, 0.22f);
  c.strokeRoundRect(r.inset(0.5f * px), 12, (cd.sel ? 1.6f : 1.f) * px, bc);
  if (cd.sel) c.strokeRoundRect(r.expand(4 * px), 12 + 4 * px, 2 * px, k.accent.alpha(0.45f));
  if (cd.drop) c.strokeRoundRect(r.expand(4 * px), 12 + 4 * px, 2.2f * px, (cd.dropBad ? k.danger : k.success).alpha(0.9f));
  // Плитка значка
  RectF tile{r.x + 14, r.y + 14, 42, 42};
  c.fillRoundRect(tile, 11, cc.alpha(0.17f));
  c.strokeRoundRect(tile.inset(0.5f * px), 11, 1 * px, cc.alpha(0.35f));
  std::string ic = !b.icon.empty() && gfx::hasIcon(b.icon) ? b.icon : std::string(bld::catIcon(b.cat));
  icon(c, ic, tile.inset(10), cc);
  // Название и подпись (культовая — одна на карту или на государство)
  text(c, bname(b), textStyle(14, gfx::FontWeight::Semibold), RectF{r.x + 66, r.y + 13, kBW - 66 - 14, 20}, k.text);
  std::string sub = b.cat == BuildingCat::Cult ? std::string(b.owner ? "Одна на государство" : "Одна на всю карту")
                                               : std::string(bld::catName(b.cat)) + " · " + std::to_string(b.levels.size()) + " " +
                                                     plural(i64(b.levels.size()), "уровень", "уровня", "уровней");
  text(c, sub, textStyle(11.5f), RectF{r.x + 66, r.y + 35, kBW - 66 - 14, 16}, b.cat == BuildingCat::Cult ? Color::mix(k.textMuted, cc, 0.55f) : k.textMuted);
  // Особые возможности, требуемые технологии, где уже стоит культовая постройка.
  {
    float fx = r.x + 66;
    const float fy = r.y + 53, fr = r.right() - 14;
    // Значки возможностей: не помещаются — последним «+N».
    int extra = 0;
    auto feat = [&](const char* name, Color col) {
      if (fx + 13 > fr - (b.techs.empty() ? 0.f : 30.f)) {
        extra++;
        return;
      }
      icon(c, name, RectF{fx, fy, 13, 13}, col);
      fx += 18;
    };
    if (!b.key.empty()) feat("lock", k.textMuted);
    for (int sk = 0; sk < int(StateKind::Count); sk++)
      if ((b.stateKinds >> unsigned(sk)) & 1u) feat(schema::stateKind(StateKind(sk)).icon, k.warning);
    if (b.convert) {
      feat("convert", k.info);
      // Несколько рецептов — их число рядом со значком.
      if (b.recipes.size() > 1 && !extra) {
        std::string n = std::to_string(b.recipes.size());
        gfx::TextStyle st = textStyle(10.5f, gfx::FontWeight::Semibold);
        float tw = gfx::measureText(n, st);
        text(c, n, st, RectF{fx - 4, fy - 1, tw + 2, 15}, k.info);
        fx += tw + 2;
      }
    }
    if (b.essenceGen) feat("essence", Color::hex(kEssenceInk));
    if (b.drain) feat("population", Color::hex(kDrainInk));
    if (b.extract) feat("pickaxe", Color::hex(kExtractInk));
    if (b.upkeepCut) feat("army", k.success);
    if (b.market) feat("scales", k.accent);
    if (b.specialAccess) feat("special-unit", k.accent);
    if (b.relicStore) feat("relic", Color::hex(kRelicInk));
    if (b.healing) feat("heal", k.success);
    if (b.plague) feat("plague", Color::hex(kPlagueInk));
    if (b.shipyard) feat("shipyard", k.info);
    if (b.mercenary) feat("mercenary", k.accent);
    if (b.coastal) feat("anchor", k.textMuted);
    if (extra > 0) {
      std::string n = "+" + std::to_string(extra);
      gfx::TextStyle st = textStyle(10.5f, gfx::FontWeight::Semibold);
      float tw = gfx::measureText(n, st);
      text(c, n, st, RectF{fx - 2, fy - 1, tw + 2, 15}, k.textMuted);
      fx += tw + 6;
    }
    if (!b.techs.empty()) {
      icon(c, "tech-tree", RectF{fx, fy, 13, 13}, k.textMuted);
      std::string n = std::to_string(b.techs.size());
      gfx::TextStyle st = textStyle(10.5f, gfx::FontWeight::Semibold);
      float tw = gfx::measureText(n, st);
      text(c, n, st, RectF{fx + 15, fy - 1, tw + 2, 15}, k.textMuted);
      fx += 15 + tw + 8;
    }
    if (!cd.cultAt.empty() && fx < fr - 30) {
      icon(c, "map-pin", RectF{fx, fy, 13, 13}, k.accent);
      text(c, cd.cultAt, textStyle(10.5f), RectF{fx + 15, fy - 1, fr - fx - 15, 15}, k.textDim);
    }
  }
  // Уровни: римские цифры, построено
  float x = r.x + 14, y = r.y + 70;
  gfx::TextStyle lv = textStyle(10.5f, gfx::FontWeight::Semibold);
  int shown = std::min<int>(int(b.levels.size()), 5);
  for (int i = 0; i < shown; i++) {
    std::string rn = roman(i + 1);
    float w = std::max(22.f, gfx::measureText(rn, lv) + 12);
    RectF pr{x, y, w, 18};
    c.fillRoundRect(pr, 9, k.surfaceHi);
    c.strokeRoundRect(pr.inset(0.5f * px), 9, 1 * px, cc.alpha(0.3f));
    text(c, rn, lv, pr, k.textDim, gfx::Align::Center);
    x += w + 4;
  }
  if (int(b.levels.size()) > shown) text(c, "+" + std::to_string(b.levels.size() - size_t(shown)), lv, RectF{x, y, 30, 18}, k.textMuted);
  if (b.built + b.building > 0) {
    std::string t = std::to_string(b.built + b.building);
    gfx::TextStyle st = textStyle(11.5f, gfx::FontWeight::Semibold);
    float tw = gfx::measureText(t, st);
    RectF br{r.right() - 14 - tw - 22, y, tw + 22, 18};
    icon(c, "province", RectF{br.x, br.y + 2, 14, 14}, k.textMuted);
    text(c, t, st, RectF{br.x + 18, br.y, tw + 4, 18}, k.textDim);
  }
  // Требования к постройкам другого дерева (общие у уникальной)
  Vec2 in{r.x, r.y + kBH * 0.5}, out{r.right(), r.y + kBH * 0.5};
  if (cd.hasIn || cd.inHot || cd.hover || cd.sel) drawPort(c, in, cd.inHot ? k.accent : Color::mix(k.borderStrong, cc, 0.5f), k.bg, cd.hasIn, cd.inHot, z);
  if (cd.hasOut || cd.outHot || cd.hover || cd.sel) drawPort(c, out, cd.outHot ? k.accent : Color::mix(k.borderStrong, cc, 0.5f), k.bg, cd.hasOut, cd.outHot, z);
  c.restore();
}

}  // namespace

void renderScene(const Scene& s, gfx::Canvas& c, RectF dev, float scale) {
  c.save();
  c.clipRoundRect(dev, 10 * scale);
  drawGrid(c, dev, scale, s.cam, s.k);
  applyCamera(c, dev, scale, s.cam);
  const float z = float(s.cam.z);
  // Дорожки категорий
  double x0 = s.cam.x - 10, x1 = s.cam.x + dev.w / (z * scale) + 10;
  double y0 = s.cam.y - 10, y1 = s.cam.y + dev.h / (z * scale) + 10;
  for (int i = 0; i < kCats; i++) {
    Color cc = bld::catColor(BuildingCat(i));
    // Первая и последняя дорожки продолжаются до краёв холста (щелчок там попадает в них же).
    double top = i == 0 ? std::min(y0, s.lanes.top[i]) : s.lanes.top[i];
    double bottom = i == kCats - 1 ? std::max(y1, s.lanes.top[i] + s.lanes.h[i]) : s.lanes.top[i] + s.lanes.h[i];
    RectF lr{float(x0), float(top), float(x1 - x0), float(bottom - top)};
    c.fillRect(lr, cc.alpha(s.dropLane == i ? 0.10f : (i % 2 ? 0.035f : 0.055f)));
    if (i > 0) c.fillRect(RectF{lr.x, float(s.lanes.top[i]), lr.w, 1.5f / z}, cc.alpha(0.35f));
  }
  for (const EdgeD& e : s.edges) {
    if (e.sel) drawCurve(c, e.k, s.k.accent.alpha(0.22f), e.width + 7, z, false);
    drawCurve(c, e.k, e.col, e.width, z, true);
    if (e.level > 1) {
      Vec2 m = curveAt(e.k, 0.5);
      std::string t = "ур. " + roman(e.level);
      gfx::TextStyle st = textStyle(10.5f, gfx::FontWeight::Semibold);
      float tw = gfx::measureText(t, st) + 12;
      RectF pr{float(m.x) - tw * 0.5f, float(m.y) - 9, tw, 18};
      c.fillRoundRect(pr, 9, s.k.surface);
      c.strokeRoundRect(pr, 9, 1 / z, e.col);
      text(c, t, st, pr, s.k.textDim, gfx::Align::Center);
    }
  }
  for (const Ext& e : s.exts) {
    Color col = e.hot ? s.k.accent : Color::mix(s.k.textMuted, e.col, 0.45f);
    drawCurve(c, curve({e.pill.right(), e.pill.y + e.pill.h * 0.5}, e.port), col.alpha(0.85f), e.hot ? 2.6f : 1.8f, z, true, true);
    c.fillRoundRect(e.pill, e.pill.h * 0.5f, s.k.surface);
    c.strokeRoundRect(e.pill.inset(0.5f / z), e.pill.h * 0.5f, (e.hot ? 1.6f : 1.f) / z, e.hot ? s.k.accent : e.col.alpha(0.55f));
    icon(c, e.icon, RectF{e.pill.x + 8, e.pill.y + (e.pill.h - 13) * 0.5f, 13, 13}, e.col);
    text(c, e.label, textStyle(11.5f), RectF{e.pill.x + 26, e.pill.y, e.pill.w - 34, e.pill.h}, e.hot ? s.k.text : s.k.textDim);
  }
  for (const Card& cd : s.cards) drawCard(c, s, cd);
  if (s.pending) drawCurve(c, s.pend, s.pendCol, 2.2f, z, true, true);
  c.restore();
}

std::vector<Ext> extsOf(const World& w, const std::vector<BN>& bs, const BN& b, Vec2 at) {
  std::vector<Ext> out;
  for (const BuildingReq& r : b.reqs) {
    if (find(bs, r.building)) continue;
    const Building* rb = w.building(r.building);
    if (!rb) continue;
    Ext e;
    e.req = r.building;
    e.b = b.id;
    e.owner = rb->owner;
    e.label = (rb->name.empty() ? std::string("Без названия") : rb->name) + (r.level > 1 ? " · " + roman(r.level) : std::string());
    e.icon = bld::iconOf(*rb);
    e.col = bld::catColor(rb->cat);
    out.push_back(std::move(e));
  }
  const double h = 24, gap = 6;
  double y0 = at.y + kBH * 0.5 - (double(out.size()) * h + double(out.size() > 0 ? out.size() - 1 : 0) * gap) * 0.5;
  gfx::TextStyle st = textStyle(11.5f);
  for (size_t i = 0; i < out.size(); i++) {
    Ext& e = out[i];
    double tw = std::min(150.0, double(gfx::measureText(e.label, st)));
    double w = tw + 26 + 12;
    e.port = {at.x, at.y + kBH * 0.5};
    e.pill = RectF{float(at.x - 40 - w), float(y0 + double(i) * (h + gap)), float(w), float(h)};
  }
  return out;
}

}  // namespace rg::app::bldtree
