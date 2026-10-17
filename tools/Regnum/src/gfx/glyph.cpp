// Regnum — векторные глифы: отрисовка (маска из слоёв, подгонка к пикселям, кеш масок) и построитель реестров
// (значки, эмблемы).
#include "gfx/glyph.h"

#include <mutex>
#include <unordered_map>

#include "gfx/glyph_builder.h"
#include "gfx/raster.h"
#include "gfx/stroke.h"
#include "gfx/svgpath.h"

namespace rg::gfx {

// ================================================================ отрисовка глифов
namespace {

constexpr float kHintStrength = 0.6f;   // доля приближения толщины штриха к целому числу пикселей
constexpr float kAxisEps = 0.02f;       // допуск «вертикальности» отрезка (единицы сетки)
constexpr int kCacheMaxDim = 192;       // маски крупнее не кешируются
constexpr size_t kCacheGeneration = 1500;

// Сведение покрытия слоя в маску: объединение (максимум) или вычитание.
struct CombineSink final : SpanSink {
  Mask& m;
  int ox, oy;
  bool erase;
  u32 k;  // множитель покрытия, 256 = 1
  CombineSink(Mask& m_, int x, int y, bool e, u32 k_) : m(m_), ox(x), oy(y), erase(e), k(k_) {}
  inline void put(u8& d, u32 c) const {
    const u32 v = (c * k) >> 8;
    if (erase) d = u8((u32(d) * (255 - v) + 127) / 255);
    else if (v > d) d = u8(v);
  }
  void row(int y, const Span* s, int n) override {
    u8* d = m.row(y - oy);
    for (int i = 0; i < n; i++) {
      u8* p = d + (s[i].x - ox);
      const int len = s[i].len;
      if (s[i].cov) {
        for (int j = 0; j < len; j++) put(p[j], s[i].cov[j]);
      } else {
        for (int j = 0; j < len; j++) put(p[j], s[i].value);
      }
    }
  }
};

// Опорная координата подгонки: исходное значение v (пиксели устройства) и цель t.
struct Anchor {
  float v, t;
  bool strong;
};

// Монотонное кусочно-линейное отображение одной оси по опорным точкам.
class AxisFit {
 public:
  void clear() { a_.clear(); }
  void add(float v, float t, bool strong) { a_.push_back({v, t, strong}); }
  void build() {
    std::sort(a_.begin(), a_.end(), [](const Anchor& x, const Anchor& y) {
      return x.v != y.v ? x.v < y.v : (x.strong && !y.strong);
    });
    std::vector<Anchor> out;
    out.reserve(a_.size());
    for (const Anchor& x : a_) {
      if (!out.empty() && x.v - out.back().v < 0.02f) {
        if (x.strong && !out.back().strong) out.back() = x;
        continue;
      }
      bool keep = true;
      while (!out.empty() && x.t < out.back().t) {
        if (x.strong && !out.back().strong) out.pop_back();
        else { keep = false; break; }
      }
      if (keep) out.push_back(x);
    }
    a_.swap(out);
  }
  float map(float x) const {
    if (a_.empty()) return x;
    if (x <= a_.front().v) return x + (a_.front().t - a_.front().v);
    if (x >= a_.back().v) return x + (a_.back().t - a_.back().v);
    auto it = std::upper_bound(a_.begin(), a_.end(), x, [](float q, const Anchor& a) { return q < a.v; });
    const Anchor& b = *it;
    const Anchor& a = *(it - 1);
    const float u = (x - a.v) / (b.v - a.v);
    return a.t + (b.t - a.t) * u;
  }
  bool empty() const { return a_.empty(); }

 private:
  std::vector<Anchor> a_;
};

// Центр штриха нечётной толщины — в центр пикселя, чётной — на границу. Равноудалённость — к центру глифа.
float snapStroke(float X, int n, float center) {
  if (n % 2 == 1) {
    const float f = std::floor(X), frac = X - f;
    if (frac < 0.02f || frac > 0.98f) {
      const float e = std::floor(X + 0.5f);
      return X > center ? e - 0.5f : e + 0.5f;
    }
    return f + 0.5f;
  }
  const float frac = X - std::floor(X);
  if (std::fabs(frac - 0.5f) < 0.02f) return X > center ? std::floor(X) : std::ceil(X);
  return std::floor(X + 0.5f);
}

// Край заливки — на границу пикселя; равноудалённость — от центра глифа (фигура не худеет).
float snapEdge(float X, float center) {
  const float frac = X - std::floor(X);
  if (std::fabs(frac - 0.5f) < 0.02f) return X > center ? std::ceil(X) : std::floor(X);
  return std::floor(X + 0.5f);
}

struct HintFrame {
  float k, ox, oy, cx, cy;
};

// Опорные точки контура: осевые отрезки (сильные) и осевые касательные на концах кривых (слабые).
void collectAnchors(const Path& p, bool stroke, int n, const HintFrame& h, AxisFit& fx, AxisFit& fy) {
  auto addX = [&](float x, bool strong) {
    const float X = h.ox + h.k * x;
    fx.add(X, stroke ? snapStroke(X, n, h.cx) : snapEdge(X, h.cx), strong);
  };
  auto addY = [&](float y, bool strong) {
    const float Y = h.oy + h.k * y;
    fy.add(Y, stroke ? snapStroke(Y, n, h.cy) : snapEdge(Y, h.cy), strong);
  };
  auto seg = [&](Pt a, Pt b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    if (std::fabs(dx) <= kAxisEps && std::fabs(dy) >= 1.0f) addX((a.x + b.x) * 0.5f, true);
    else if (std::fabs(dy) <= kAxisEps && std::fabs(dx) >= 1.0f) addY((a.y + b.y) * 0.5f, true);
  };
  auto tangent = [&](Pt at, Pt d) {
    const float l = std::sqrt(d.x * d.x + d.y * d.y);
    if (!(l > 1e-4f)) return;
    if (std::fabs(d.x) <= kAxisEps * l) addX(at.x, false);
    else if (std::fabs(d.y) <= kAxisEps * l) addY(at.y, false);
  };
  size_t pi = 0;
  const size_t np = p.pts.size();
  Pt cur{0, 0}, start{0, 0};
  for (Path::Verb v : p.verbs) {
    switch (v) {
      case Path::Move:
        if (pi + 1 > np) return;
        cur = start = p.pts[pi++];
        break;
      case Path::Line:
        if (pi + 1 > np) return;
        seg(cur, p.pts[pi]);
        cur = p.pts[pi++];
        break;
      case Path::Quad: {
        if (pi + 2 > np) return;
        const Pt c = p.pts[pi], e = p.pts[pi + 1];
        tangent(cur, c - cur);
        tangent(e, e - c);
        cur = e;
        pi += 2;
        break;
      }
      case Path::Cubic: {
        if (pi + 3 > np) return;
        const Pt c1 = p.pts[pi], c2 = p.pts[pi + 1], e = p.pts[pi + 2];
        Pt d0 = c1 - cur;
        if (d0.x * d0.x + d0.y * d0.y < 1e-8f) d0 = c2 - cur;
        Pt d1 = e - c2;
        if (d1.x * d1.x + d1.y * d1.y < 1e-8f) d1 = e - c1;
        tangent(cur, d0);
        tangent(e, d1);
        cur = e;
        pi += 3;
        break;
      }
      case Path::Close:
        seg(cur, start);
        cur = start;
        break;
    }
  }
}

inline bool isStrokeKind(VecLayer::Kind k) { return k == VecLayer::Stroke || k == VecLayer::EraseStroke; }
inline bool isEraseKind(VecLayer::Kind k) { return k == VecLayer::Erase || k == VecLayer::EraseStroke; }

int hintedPixels(float w) { return std::max(1, int(std::lround(w))); }

float maxStrokeWidth(const VecGlyph& g, const GlyphStyle& st) {
  float m = 0;
  for (const VecLayer& L : g.layers)
    if (isStrokeKind(L.kind)) m = std::max(m, (L.width > 0 ? L.width : st.strokeWidth) * st.strokeScale);
  return m;
}

// ---------------------------------------------------------------- кеш масок
struct MaskKey {
  u64 id;
  u32 size, scale, width;
  u8 flags;
  bool operator==(const MaskKey&) const = default;
};
struct MaskKeyHash {
  size_t operator()(const MaskKey& k) const {
    u64 h = hashMix(k.id, (u64(k.size) << 32) | k.scale);
    h = hashMix(h, (u64(k.width) << 8) | k.flags);
    return size_t(h);
  }
};

struct CachedMask {
  Mask mask;
  int margin = 0;
};

// Кеш из двух поколений: промах в текущем ищется в прошлом; переполнение текущего делает его прошлым.
class MaskCache {
 public:
  std::shared_ptr<const CachedMask> get(const MaskKey& k) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = cur_.find(k);
    if (it != cur_.end()) return it->second;
    auto jt = old_.find(k);
    if (jt == old_.end()) return nullptr;
    auto v = jt->second;
    insertLocked(k, v);
    return v;
  }
  void put(const MaskKey& k, std::shared_ptr<const CachedMask> v) {
    std::lock_guard<std::mutex> lock(mu_);
    insertLocked(k, std::move(v));
  }
  void clear() {
    std::lock_guard<std::mutex> lock(mu_);
    cur_.clear();
    old_.clear();
  }
  size_t size() {
    std::lock_guard<std::mutex> lock(mu_);
    return cur_.size() + old_.size();
  }

 private:
  void insertLocked(const MaskKey& k, std::shared_ptr<const CachedMask> v) {
    if (cur_.size() >= kCacheGeneration) {
      old_.swap(cur_);
      cur_.clear();
    }
    cur_[k] = std::move(v);
  }
  std::mutex mu_;
  std::unordered_map<MaskKey, std::shared_ptr<const CachedMask>, MaskKeyHash> cur_, old_;
};

MaskCache& maskCache() {
  static MaskCache c;
  return c;
}

u32 fbits(float f) {
  u32 u;
  std::memcpy(&u, &f, 4);
  return u;
}

// Залить маску в пикселях устройства (преобразование холста временно сбрасывается).
void blitMask(Canvas& c, const Mask& m, int x, int y, Color color) {
  c.save();
  c.setTransform(Affine{});
  c.fillMask(m, float(x), float(y), Paint(color));
  c.restore();
}

}  // namespace

void renderGlyphMask(const VecGlyph& g, const Affine& T, Mask& out, int ox, int oy, const GlyphStyle& st) {
  std::fill(out.a.begin(), out.a.end(), u8(0));
  if (out.empty() || g.layers.empty()) return;
  const float sc = T.scaleFactor();
  if (!(sc > 0) || !std::isfinite(sc)) return;
  const float tolA = 1e-4f * std::max(std::fabs(T.a), std::fabs(T.b));
  const bool conformal = std::fabs(T.a - T.d) <= tolA && std::fabs(T.b + T.c) <= tolA;
  const bool axis = conformal && T.b == 0 && T.c == 0 && T.a > 0;
  const float k = T.a;
  const bool hint = st.hint && axis && k * g.grid <= st.hintMaxPx + 0.01f;

  thread_local AxisFit fx, fy;
  thread_local Rasterizer ras;
  thread_local Path dev, outline;
  fx.clear();
  fy.clear();
  if (hint) {
    const HintFrame hf{k, T.e, T.f, T.e + k * g.grid * 0.5f, T.f + k * g.grid * 0.5f};
    for (const VecLayer& L : g.layers) {
      if (isEraseKind(L.kind)) continue;
      const bool stroke = L.kind == VecLayer::Stroke;
      const float w = (L.width > 0 ? L.width : st.strokeWidth) * st.strokeScale * k;
      collectAnchors(L.path, stroke, hintedPixels(w), hf, fx, fy);
    }
    fx.build();
    fy.build();
  }

  const RectI clip{ox, oy, out.w, out.h};
  for (const VecLayer& L : g.layers) {
    if (L.path.empty()) continue;
    const bool stroke = isStrokeKind(L.kind);
    const float wGrid = (L.width > 0 ? L.width : st.strokeWidth) * st.strokeScale;
    u32 kcov = 256;
    ras.reset(clip);
    if (conformal) {
      // Контур в пикселях устройства (с подгонкой), обводка — там же: толщина и скругления точные.
      dev = L.path;
      for (Pt& p : dev.pts) {
        p = T.apply(p);
        if (hint) p = {fx.map(p.x), fy.map(p.y)};
      }
      if (stroke) {
        float w = wGrid * sc;
        if (hint) w += (float(hintedPixels(w)) - w) * kHintStrength;
        if (w < 1) { kcov = u32(std::max(0.f, w) * 256 + 0.5f); w = 1; }
        Stroke s;
        s.width = w;
        s.join = st.join;
        s.cap = st.cap;
        strokeToPath(dev, s, 0.05f, outline);
        ras.addPath(outline);
      } else {
        ras.addPath(dev);
      }
    } else {
      // Наклон или неравномерный масштаб: обводка в единицах сетки, затем преобразование.
      if (stroke) {
        float w = wGrid;
        if (w * sc < 1) { kcov = u32(std::max(0.f, w * sc) * 256 + 0.5f); w = 1 / sc; }
        Stroke s;
        s.width = w;
        s.join = st.join;
        s.cap = st.cap;
        strokeToPath(L.path, s, 0.05f / sc, outline);
        ras.addPath(outline, T);
      } else {
        ras.addPath(L.path, T);
      }
    }
    if (ras.invalid() || ras.empty() || kcov == 0) continue;
    CombineSink sink(out, ox, oy, isEraseKind(L.kind), kcov);
    ras.sweep(L.kind == VecLayer::FillEvenOdd ? FillRule::EvenOdd : FillRule::NonZero, sink);
  }
}

void drawGlyph(Canvas& c, const VecGlyph& g, RectF rect, Color color, const GlyphStyle& st) {
  if (color.a == 0 || g.layers.empty() || !(g.grid > 0)) return;
  const float s = std::min(rect.w, rect.h);
  if (!(s > 0) || !std::isfinite(s) || !std::isfinite(rect.x) || !std::isfinite(rect.y)) return;
  const float ux = rect.cx() - s * 0.5f, uy = rect.cy() - s * 0.5f;
  const float overflow = maxStrokeWidth(g, st) / g.grid * s;  // обводка может выступать за сетку
  if (c.quickReject({ux - overflow, uy - overflow, s + 2 * overflow, s + 2 * overflow})) return;
  const RectI clipB = c.clipBounds();
  if (clipB.empty()) return;

  const Affine& M = c.transform();
  const bool axis = M.b == 0 && M.c == 0 && M.a > 0 && std::fabs(M.a - M.d) <= 1e-4f * M.a;
  if (axis) {
    float S = s * M.a;
    if (!(S >= 0.5f) || S > 1.0e5f) return;
    const Pt cd = M.apply({rect.cx(), rect.cy()});
    const bool hint = st.hint && S <= st.hintMaxPx;
    S = hint ? std::max(1.f, std::round(S)) : std::round(S * 64) / 64;
    const float X0 = std::round(cd.x - S * 0.5f), Y0 = std::round(cd.y - S * 0.5f);
    if (!(std::fabs(X0) < 1e8f && std::fabs(Y0) < 1e8f)) return;
    const float k = S / g.grid;
    const int margin = int(std::ceil(maxStrokeWidth(g, st) * k * 0.5f + 1.5f));
    const int dim = int(std::ceil(S)) + 2 * margin;
    const int mx = int(X0) - margin, my = int(Y0) - margin;
    if (g.id != 0 && dim <= kCacheMaxDim) {
      const MaskKey key{g.id, fbits(S), fbits(st.strokeScale), fbits(st.strokeWidth),
                        u8((st.hint ? 1 : 0) | (u8(st.join) << 1) | (u8(st.cap) << 3))};
      auto cm = maskCache().get(key);
      if (!cm) {
        auto fresh = std::make_shared<CachedMask>();
        fresh->margin = margin;
        fresh->mask = Mask(dim, dim);
        renderGlyphMask(g, Affine{k, 0, 0, k, float(margin), float(margin)}, fresh->mask, 0, 0, st);
        cm = fresh;
        maskCache().put(key, cm);
      }
      blitMask(c, cm->mask, mx, my, color);
      return;
    }
    // Крупный или временный глиф: только видимая часть.
    const RectI area = RectI{mx, my, dim, dim}.intersect(clipB);
    if (area.empty()) return;
    Mask m(area.w, area.h);
    renderGlyphMask(g, Affine{k, 0, 0, k, X0, Y0}, m, area.x, area.y, st);
    blitMask(c, m, area.x, area.y, color);
    return;
  }
  // Поворот, наклон, отражение: маска по габаритам повёрнутого квадрата.
  const float kg = s / g.grid;
  const Affine T = M * Affine{kg, 0, 0, kg, ux, uy};
  const float ov = maxStrokeWidth(g, st) * 0.5f + 0.5f;
  const Pt corners[4] = {T.apply({-ov, -ov}), T.apply({g.grid + ov, -ov}), T.apply({g.grid + ov, g.grid + ov}),
                         T.apply({-ov, g.grid + ov})};
  float x0 = corners[0].x, x1 = x0, y0 = corners[0].y, y1 = y0;
  for (const Pt& p : corners) {
    x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
    y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
  }
  if (!(std::isfinite(x0) && std::isfinite(x1) && std::isfinite(y0) && std::isfinite(y1))) return;
  x0 = std::clamp(x0, -1e8f, 1e8f); x1 = std::clamp(x1, -1e8f, 1e8f);
  y0 = std::clamp(y0, -1e8f, 1e8f); y1 = std::clamp(y1, -1e8f, 1e8f);
  const int ix0 = int(std::floor(x0)) - 1, iy0 = int(std::floor(y0)) - 1;
  const RectI area = RectI{ix0, iy0, int(std::ceil(x1)) + 1 - ix0, int(std::ceil(y1)) + 1 - iy0}.intersect(clipB);
  if (area.empty()) return;
  Mask m(area.w, area.h);
  renderGlyphMask(g, T, m, area.x, area.y, st);
  blitMask(c, m, area.x, area.y, color);
}

void clearGlyphCache() { maskCache().clear(); }
size_t glyphCacheSize() { return maskCache().size(); }

// ================================================================ построитель реестров
namespace shapes {

std::string n(double v) { return svgNum(v, 3); }

std::string circ(double cx, double cy, double r) {
  return "M" + n(cx - r) + " " + n(cy) + "A" + n(r) + " " + n(r) + " 0 1 0 " + n(cx + r) + " " + n(cy) + "A" + n(r) + " " +
         n(r) + " 0 1 0 " + n(cx - r) + " " + n(cy) + "Z";
}

std::string ell(double cx, double cy, double rx, double ry) {
  return "M" + n(cx - rx) + " " + n(cy) + "A" + n(rx) + " " + n(ry) + " 0 1 0 " + n(cx + rx) + " " + n(cy) + "A" + n(rx) +
         " " + n(ry) + " 0 1 0 " + n(cx - rx) + " " + n(cy) + "Z";
}

std::string rrect(double x, double y, double w, double h, double r) {
  r = std::max(0.0, std::min({r, w / 2, h / 2}));
  if (r <= 0) return "M" + n(x) + " " + n(y) + "H" + n(x + w) + "V" + n(y + h) + "H" + n(x) + "Z";
  const std::string a = "A" + n(r) + " " + n(r) + " 0 0 1 ";
  return "M" + n(x + r) + " " + n(y) + "H" + n(x + w - r) + a + n(x + w) + " " + n(y + r) + "V" + n(y + h - r) + a +
         n(x + w - r) + " " + n(y + h) + "H" + n(x + r) + a + n(x) + " " + n(y + h - r) + "V" + n(y + r) + a + n(x + r) +
         " " + n(y) + "Z";
}

std::string poly(std::initializer_list<double> xy, bool closed) {
  std::string s;
  int i = 0;
  double x = 0;
  for (double v : xy) {
    if (i % 2 == 0) x = v;
    else s += (i == 1 ? "M" : "L") + n(x) + " " + n(v);
    i++;
  }
  if (closed && !s.empty()) s += "Z";
  return s;
}

std::string star(double cx, double cy, double R, double r, int points, double rotDeg) {
  std::string s;
  const int m = std::max(2, points) * 2;
  for (int i = 0; i < m; i++) {
    const double a = (rotDeg + 180.0 * i / (m / 2)) * kPi / 180.0;
    const double rad = i % 2 == 0 ? R : r;
    s += (i == 0 ? "M" : "L") + n(cx + rad * std::cos(a)) + " " + n(cy + rad * std::sin(a));
  }
  return s + "Z";
}

std::string gear(double cx, double cy, double ro, double ri, int teeth, double topHalfDeg, double baseHalfDeg) {
  std::string s;
  auto pt = [&](double r, double deg) {
    const double a = deg * kPi / 180.0;
    return n(cx + r * std::cos(a)) + " " + n(cy + r * std::sin(a));
  };
  const double step = 360.0 / teeth;
  for (int i = 0; i < teeth; i++) {
    const double a = -90 + i * step;
    s += (i == 0 ? "M" : "L") + pt(ri, a - baseHalfDeg);
    s += "L" + pt(ro, a - topHalfDeg);
    s += "A" + n(ro) + " " + n(ro) + " 0 0 1 " + pt(ro, a + topHalfDeg);
    s += "L" + pt(ri, a + baseHalfDeg);
    s += "A" + n(ri) + " " + n(ri) + " 0 0 1 " + pt(ri, a + step - baseHalfDeg);
  }
  return s + "Z";
}

std::string arc(double cx, double cy, double r, double fromDeg, double toDeg) {
  const double a0 = fromDeg * kPi / 180.0, a1 = toDeg * kPi / 180.0;
  const double sweepDeg = toDeg - fromDeg;
  const bool large = std::fabs(sweepDeg) > 180.0;
  const bool sweep = sweepDeg > 0;
  return "M" + n(cx + r * std::cos(a0)) + " " + n(cy + r * std::sin(a0)) + "A" + n(r) + " " + n(r) + " 0 " +
         (large ? "1 " : "0 ") + (sweep ? "1 " : "0 ") + n(cx + r * std::cos(a1)) + " " + n(cy + r * std::sin(a1));
}

std::string mirrorX(std::string_view d, double axis) {
  Path p = svgPath(d);
  p.transform(Affine{-1, 0, 0, 1, float(2 * axis), 0});
  return toSvgPath(p, 3);
}

std::string dashedCircle(double cx, double cy, double r, int dashes, double fill) {
  std::string s;
  const double step = 360.0 / dashes;
  for (int i = 0; i < dashes; i++) {
    const double a = -90 + i * step + step * (1 - fill) * 0.5;
    s += arc(cx, cy, r, a, a + step * fill);
  }
  return s;
}

}  // namespace shapes

// Подпути заливки ориентируются одинаково: NonZero даёт объединение фигур, а не дыры на пересечениях
// кругов и многоугольников, описанных в разных направлениях.
static Path unifyWinding(const Path& p) {
  struct Seg {
    Path::Verb v;
    Pt c1, c2, end;
  };
  Path out;
  std::vector<Seg> segs;
  Pt start{0, 0};
  bool closed = false, have = false;
  auto flush = [&]() {
    if (!have) return;
    Path sub;
    sub.moveTo(start.x, start.y);
    for (const Seg& s : segs) {
      if (s.v == Path::Line) sub.lineTo(s.end.x, s.end.y);
      else if (s.v == Path::Quad) sub.quadTo(s.c1.x, s.c1.y, s.end.x, s.end.y);
      else sub.cubicTo(s.c1.x, s.c1.y, s.c2.x, s.c2.y, s.end.x, s.end.y);
    }
    double area = 0;
    sub.flatten(0.05f, [&](const std::vector<Pt>& q, bool) {
      for (size_t i = 0, n = q.size(); i < n; i++) {
        const Pt a = q[i], b = q[(i + 1) % n];
        area += double(a.x) * b.y - double(b.x) * a.y;
      }
    });
    if (area >= 0) {
      out.addPath(sub);
    } else {
      const Pt last = segs.empty() ? start : segs.back().end;
      out.moveTo(last.x, last.y);
      for (size_t i = segs.size(); i-- > 0;) {
        const Pt prev = i > 0 ? segs[i - 1].end : start;
        const Seg& s = segs[i];
        if (s.v == Path::Line) out.lineTo(prev.x, prev.y);
        else if (s.v == Path::Quad) out.quadTo(s.c1.x, s.c1.y, prev.x, prev.y);
        else out.cubicTo(s.c2.x, s.c2.y, s.c1.x, s.c1.y, prev.x, prev.y);
      }
    }
    if (closed) out.close();
    segs.clear();
    closed = false;
    have = false;
  };
  size_t pi = 0;
  const size_t np = p.pts.size();
  Pt cur{0, 0};
  for (Path::Verb v : p.verbs) {
    if (v != Path::Move && v != Path::Close && !have) {
      // Рисование после Z без M — от начала прежнего подпути.
      have = true;
      cur = start;
    }
    switch (v) {
      case Path::Move:
        if (pi + 1 > np) return out;
        flush();
        start = cur = p.pts[pi++];
        have = true;
        break;
      case Path::Line:
        if (pi + 1 > np) return out;
        segs.push_back({Path::Line, {}, {}, p.pts[pi]});
        cur = p.pts[pi++];
        break;
      case Path::Quad:
        if (pi + 2 > np) return out;
        segs.push_back({Path::Quad, p.pts[pi], {}, p.pts[pi + 1]});
        cur = p.pts[pi + 1];
        pi += 2;
        break;
      case Path::Cubic:
        if (pi + 3 > np) return out;
        segs.push_back({Path::Cubic, p.pts[pi], p.pts[pi + 1], p.pts[pi + 2]});
        cur = p.pts[pi + 2];
        pi += 3;
        break;
      case Path::Close:
        if (have) {
          closed = true;
          flush();
        }
        cur = start;
        break;
    }
  }
  flush();
  return out;
}

GlyphBuilder::GlyphBuilder(float grid, u64 idBase) : grid_(grid), idBase_(idBase) {}

void GlyphBuilder::addList(const char* name, const std::vector<L>& layers) {
  VecGlyph g;
  g.grid = grid_;
  for (const L& l : layers) {
    if (l.op == L::Use) {
      const VecGlyph* src = find(l.d);
      if (!src) {
        issues_.push_back(std::string(name) + ": нет глифа «" + l.d + "» для вставки");
        continue;
      }
      for (const VecLayer& s : src->layers) {
        VecLayer c = s;
        if (!l.xf.isIdentity()) c.path.transform(l.xf);
        if (l.width > 0 && isStrokeKind(c.kind)) c.width = l.width;
        g.layers.push_back(std::move(c));
      }
      continue;
    }
    VecLayer v;
    v.kind = l.kind;
    v.width = l.width;
    SvgPathError err;
    if (!parseSvgPath(l.d, v.path, &err))
      issues_.push_back(std::string(name) + ": ошибка пути в позиции " + std::to_string(err.pos) + " (" + err.msg + ")");
    if (v.path.empty()) {
      issues_.push_back(std::string(name) + ": пустой слой");
      continue;
    }
    if (!l.xf.isIdentity()) v.path.transform(l.xf);
    if (v.kind == VecLayer::Fill || v.kind == VecLayer::Erase) v.path = unifyWinding(v.path);
    g.layers.push_back(std::move(v));
  }
  // Проверка: контуры внутри сетки (обводка может выступать не более чем на полтолщины).
  for (const VecLayer& v : g.layers) {
    const RectF b = v.path.bounds();
    const float w = isStrokeKind(v.kind) ? (v.width > 0 ? v.width : 1.75f) * 0.5f : 0.f;
    if (b.x - w < -0.05f || b.y - w < -0.05f || b.right() + w > grid_ + 0.05f || b.bottom() + w > grid_ + 0.05f)
      if (!isEraseKind(v.kind))
        issues_.push_back(std::string(name) + ": выход за сетку (" + svgNum(b.x) + ", " + svgNum(b.y) + ", " +
                          svgNum(b.right()) + ", " + svgNum(b.bottom()) + ")");
  }
  if (g.layers.empty()) issues_.push_back(std::string(name) + ": нет слоёв");
  if (index_.count(name)) issues_.push_back(std::string(name) + ": повторное имя");
  g.id = idBase_ + glyphs_.size() + 1;
  index_[name] = glyphs_.size();
  names_.push_back(name);
  glyphs_.push_back(std::move(g));
}

const VecGlyph* GlyphBuilder::find(std::string_view name) const {
  auto it = index_.find(name);
  return it == index_.end() ? nullptr : &glyphs_[it->second];
}

}  // namespace rg::gfx
