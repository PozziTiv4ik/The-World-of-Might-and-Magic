// Regnum — пиксели карты: наложение, копирование и масштабирование изображений (тайлы в кадре, мини-карта).
#include "map/map_internal.h"

namespace rg::map::detail {

namespace {

constexpr u32 kRB = 0x00FF00FFu;

inline u32 mulPx(u32 p, u32 a) {
  u32 rb = (p & kRB) * a + 0x00800080u;
  u32 ag = ((p >> 8) & kRB) * a + 0x00800080u;
  rb = ((rb + ((rb >> 8) & kRB)) >> 8) & kRB;
  ag = (ag + ((ag >> 8) & kRB)) & ~kRB;
  return rb | ag;
}
inline u32 over(u32 s, u32 d) { return s + mulPx(d, 255 - (s >> 24)); }


// Отводы фильтра по одной оси: выход k покрывает исходный отрезок [u0 + k·r, u0 + (k + 1)·r).
struct Taps {
  std::vector<i32> first;
  std::vector<u8> count;
  std::vector<u16> w;   // по maxTaps на выход, сумма 256
  int maxTaps = 0;
  int lo = 0, hi = 0;   // диапазон используемых исходных индексов [lo, hi]
};

Taps makeTaps(int outN, double u0, double r, int srcN) {
  Taps t;
  t.maxTaps = r <= 1.0 ? 2 : int(std::ceil(r)) + 1;
  t.first.resize(size_t(outN));
  t.count.resize(size_t(outN));
  t.w.assign(size_t(outN) * size_t(t.maxTaps), 0);
  t.lo = srcN;
  t.hi = -1;
  std::vector<double> wt(size_t(t.maxTaps));
  std::vector<int> idx(size_t(t.maxTaps));
  for (int k = 0; k < outN; k++) {
    int n = 0;
    if (r <= 1.0) {
      const double c = u0 + (k + 0.5) * r - 0.5;
      const double fl = std::floor(c);
      const double f = c - fl;
      const int i0 = clamp(int(fl), 0, srcN - 1), i1 = clamp(int(fl) + 1, 0, srcN - 1);
      if (i0 == i1 || f <= 0) { idx[0] = i0; wt[0] = 1; n = 1; }
      else { idx[0] = i0; wt[0] = 1 - f; idx[1] = i1; wt[1] = f; n = 2; }
    } else {
      const double a = u0 + k * r, b = a + r;
      for (int i = int(std::floor(a)); i < int(std::ceil(b)) && n < t.maxTaps; i++) {
        const double ov = std::min(b, double(i + 1)) - std::max(a, double(i));
        if (ov <= 0) continue;
        const int ci = clamp(i, 0, srcN - 1);
        if (n > 0 && idx[size_t(n - 1)] == ci) { wt[size_t(n - 1)] += ov / r; continue; }
        idx[size_t(n)] = ci;
        wt[size_t(n)] = ov / r;
        n++;
      }
      if (n == 0) { idx[0] = clamp(int(std::floor(a)), 0, srcN - 1); wt[0] = 1; n = 1; }
    }
    // индексы идут подряд: первый + номер отвода
    const int first = idx[0];
    int sum = 0, big = 0;
    u16* w = &t.w[size_t(k) * size_t(t.maxTaps)];
    for (int j = 0; j < n; j++) {
      const int pos = idx[size_t(j)] - first;
      w[pos] = u16(std::lround(wt[size_t(j)] * 256));
      sum += w[pos];
    }
    const int span = idx[size_t(n - 1)] - first + 1;
    for (int j = 1; j < span; j++) if (w[j] > w[big]) big = j;
    w[big] = u16(int(w[big]) + 256 - sum);
    t.first[size_t(k)] = first;
    t.count[size_t(k)] = u8(span);
    t.lo = std::min(t.lo, first);
    t.hi = std::max(t.hi, first + span - 1);
  }
  return t;
}

inline u32 combine(const u32* const* rows, const u16* w, int n, int x) {
  u32 rb = 0, ag = 0;
  for (int j = 0; j < n; j++) {
    const u32 p = rows[j][x], k = w[j];
    rb += (p & kRB) * k;
    ag += ((p >> 8) & kRB) * k;
  }
  rb = ((rb + 0x00800080u) >> 8) & kRB;
  ag = (ag + 0x00800080u) & ~kRB;
  return rb | ag;
}

inline u32 combineRow(const u32* row, int first, const u16* w, int n) {
  u32 rb = 0, ag = 0;
  for (int j = 0; j < n; j++) {
    const u32 p = row[first + j], k = w[j];
    rb += (p & kRB) * k;
    ag += ((p >> 8) & kRB) * k;
  }
  rb = ((rb + 0x00800080u) >> 8) & kRB;
  ag = (ag + 0x00800080u) & ~kRB;
  return rb | ag;
}

// Пересэмплирование src в прямоугольник выхода [dx0, dx1) × [dy0, dy1) пикселей dst: пиксель X выхода покрывает
// исходный отрезок [ux + (X − dx0)·rx, ...). mode: 0 — копия, 1 — наложение.
void resample(gfx::Image& dst, int dx0, int dy0, int dx1, int dy1, const gfx::Image& src, double ux, double uy, double rx, double ry,
              bool blend) {
  const int ow = dx1 - dx0, oh = dy1 - dy0;
  if (ow <= 0 || oh <= 0 || src.empty()) return;
  const Taps tx = makeTaps(ow, ux, rx, src.w), ty = makeTaps(oh, uy, ry, src.h);
  // Горизонтальный проход для нужных строк источника.
  const int rows = ty.hi - ty.lo + 1;
  std::vector<u32> tmp(size_t(rows) * size_t(ow));
  for (int y = 0; y < rows; y++) {
    const u32* s = src.row(ty.lo + y);
    u32* o = &tmp[size_t(y) * size_t(ow)];
    for (int k = 0; k < ow; k++)
      o[k] = combineRow(s, tx.first[size_t(k)], &tx.w[size_t(k) * size_t(tx.maxTaps)], tx.count[size_t(k)]);
  }
  std::vector<const u32*> rp(size_t(ty.maxTaps));
  for (int j = 0; j < oh; j++) {
    const int n = ty.count[size_t(j)];
    for (int q = 0; q < n; q++) rp[size_t(q)] = &tmp[size_t(ty.first[size_t(j)] - ty.lo + q) * size_t(ow)];
    const u16* w = &ty.w[size_t(j) * size_t(ty.maxTaps)];
    u32* d = dst.row(dy0 + j) + dx0;
    if (n == 1 && w[0] == 256) {
      const u32* s = rp[0];
      if (blend) for (int x = 0; x < ow; x++) d[x] = over(s[x], d[x]);
      else std::memcpy(d, s, size_t(ow) * 4);
      continue;
    }
    if (blend) for (int x = 0; x < ow; x++) d[x] = over(combine(rp.data(), w, n, x), d[x]);
    else for (int x = 0; x < ow; x++) d[x] = combine(rp.data(), w, n, x);
  }
}

}  // namespace

// ================================================================ наложение изображений
void blendImage(gfx::Image& dst, const gfx::Image& src, int dx, int dy, const gfx::RectI& clip, float opacity) {
  const gfx::RectI r = gfx::RectI(dx, dy, src.w, src.h).intersect(clip).intersect(gfx::RectI(0, 0, dst.w, dst.h));
  if (r.empty() || opacity <= 0) return;
  const u32 k = u32(std::lround(clamp(opacity, 0.f, 1.f) * 255));
  for (int y = r.y; y < r.bottom(); y++) {
    const u32* s = src.row(y - dy) + (r.x - dx);
    u32* d = dst.row(y) + r.x;
    if (k >= 255) {
      for (int x = 0; x < r.w; x++) {
        const u32 p = s[x];
        if (p >= 0xFF000000u) d[x] = p;
        else if (p) d[x] = over(p, d[x]);
      }
    } else {
      for (int x = 0; x < r.w; x++)
        if (const u32 p = s[x]) d[x] = over(mulPx(p, k), d[x]);
    }
  }
}

void copyImage(gfx::Image& dst, const gfx::Image& src, int dx, int dy, const gfx::RectI& clip) {
  const gfx::RectI r = gfx::RectI(dx, dy, src.w, src.h).intersect(clip).intersect(gfx::RectI(0, 0, dst.w, dst.h));
  if (r.empty()) return;
  for (int y = r.y; y < r.bottom(); y++) std::memcpy(dst.row(y) + r.x, src.row(y - dy) + (r.x - dx), size_t(r.w) * 4);
}

void scaleImage(gfx::Image& dst, const gfx::Image& src, double x0, double y0, double x1, double y1, const gfx::RectI& clip, bool opaque) {
  if (src.empty() || !(x1 > x0) || !(y1 > y0)) return;
  const int X0 = int(std::lround(x0)), X1 = int(std::lround(x1)), Y0 = int(std::lround(y0)), Y1 = int(std::lround(y1));
  const gfx::RectI r = gfx::RectI(X0, Y0, X1 - X0, Y1 - Y0).intersect(clip).intersect(gfx::RectI(0, 0, dst.w, dst.h));
  if (r.empty()) return;
  const double rx = src.w / (x1 - x0), ry = src.h / (y1 - y0);
  if (std::fabs(rx - 1) < 1e-9 && std::fabs(ry - 1) < 1e-9 && X0 == x0 && Y0 == y0) {
    if (opaque) copyImage(dst, src, X0, Y0, r);
    else blendImage(dst, src, X0, Y0, r);
    return;
  }
  resample(dst, r.x, r.y, r.right(), r.bottom(), src, (r.x - x0) * rx, (r.y - y0) * ry, rx, ry, !opaque);
}

}  // namespace rg::map::detail
