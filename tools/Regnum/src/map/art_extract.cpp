// Regnum — разбор исходной карты в объекты (см. art_extract.h).
#include "map/art_extract.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "base/fs.h"
#include "base/jobs.h"
#include "map/basemap_build.h"

namespace rg::map::art {

namespace {

inline int inkOf(const u8* px, size_t i) { return 255 - int(px[i * 4 + 2]); }
inline int minRgb(const u8* px, size_t i) { return std::min({int(px[i * 4]), int(px[i * 4 + 1]), int(px[i * 4 + 2])}); }

// Нейтральный серый тела горы (без воды) или −1 (как в разложении базовой карты).
int bodyGray(const u8* px, int w, int x, int y) {
  const u8* q = px + (size_t(y) * size_t(w) + size_t(x)) * 4;
  if (int(q[2]) - int(q[0]) >= 32) return -1;
  const int R = q[0], tw = q[2] - q[0];
  const int g = std::min(255, (R * 255 + (255 - tw) / 2) / (255 - tw));
  return g >= 72 && g <= 120 ? g : -1;
}

}  // namespace

std::vector<InkComponent> inkComponents(const codec::RgbaImage& flat) {
  const int w = flat.w, h = flat.h;
  const size_t N = size_t(w) * size_t(h);
  const u8* px = flat.rgba.data();
  std::vector<u8> seen(N, 0);
  std::vector<u32> stack, comp;
  std::vector<InkComponent> out;
  std::vector<u64> keys;
  for (size_t start = 0; start < N; start++) {
    if (seen[start] || inkOf(px, start) < 16) continue;
    comp.clear();
    stack.push_back(u32(start));
    seen[start] = 1;
    while (!stack.empty()) {
      const u32 i = stack.back();
      stack.pop_back();
      comp.push_back(i);
      const int x = int(i % u32(w)), y = int(i / u32(w));
      for (int dy = -1; dy <= 1; dy++) {
        const int ny = y + dy;
        if (ny < 0 || ny >= h) continue;
        for (int dx = -1; dx <= 1; dx++) {
          const int nx = x + dx;
          if (nx < 0 || nx >= w) continue;
          const size_t j = size_t(ny) * size_t(w) + size_t(nx);
          if (!seen[j] && inkOf(px, j) >= 16) {
            seen[j] = 1;
            stack.push_back(u32(j));
          }
        }
      }
    }
    InkComponent c;
    c.x0 = w;
    c.y0 = h;
    c.x1 = -1;
    c.y1 = -1;
    int body = 0;
    for (u32 i : comp) {
      const int x = int(i % u32(w)), y = int(i / u32(w));
      c.x0 = std::min(c.x0, x);
      c.x1 = std::max(c.x1, x);
      c.y0 = std::min(c.y0, y);
      c.y1 = std::max(c.y1, y);
      if (minRgb(px, i) < 60) c.dark++;
      if (x > 0 && y > 0 && x < w - 1 && y < h - 1 && bodyGray(px, w, x, y) >= 0 && bodyGray(px, w, x - 1, y) >= 0 && bodyGray(px, w, x + 1, y) >= 0 &&
          bodyGray(px, w, x, y - 1) >= 0 && bodyGray(px, w, x, y + 1) >= 0)
        body++;
    }
    c.pixels = int(comp.size());
    c.mountain = body >= 8 && c.x1 - c.x0 >= 5 && c.y1 - c.y0 >= 4;
    keys.clear();
    for (u32 i : comp) {
      const u64 x = u64(i % u32(w) - u32(c.x0)), y = u64(i / u32(w) - u32(c.y0));
      keys.push_back((y << 40) | (x << 16) | u64(inkOf(px, i) >> 3));
    }
    std::sort(keys.begin(), keys.end());
    u64 hsh = 1469598103934665603ull ^ (u64(c.x1 - c.x0) << 32) ^ u64(c.y1 - c.y0);
    for (u64 k : keys) hsh = (hsh ^ k) * 1099511628211ull;
    c.shape = hsh;
    out.push_back(c);
  }
  return out;
}

// ================================================================ разбор
namespace {

Ring toRing(const bake::IRing& r) {
  Ring out;
  out.reserve(r.size());
  for (const bake::IPt& p : r) out.emplace_back(double(p.x) / bake::kCoordScale, double(p.y) / bake::kCoordScale);
  return out;
}

double ringArea(const Ring& r) {
  double a = 0;
  for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) a += r[j].x * r[i].y - r[i].x * r[j].y;
  return a * 0.5;
}

// Изолинии поля v (w × h, значения в центрах пикселей (i + 0,5, j + 0,5)) на уровне 127,5: все кольца — внешние
// и внутренние («дыры»), за краем поле равно 0. Вход внутрь — v ≥ 128; сёдла — по среднему клетки. Кольца с
// площадью меньше minArea отбрасываются. Координаты — единицы карты.
std::vector<Ring> traceIso(const u8* v, int w, int h, double minArea) {
  const int W2 = w + 2;
  auto val = [&](int X, int Y) -> int {   // X, Y — индексы сетки со сдвигом +1 (0 и w + 1 — за краем)
    if (X < 1 || Y < 1 || X > w || Y > h) return 0;
    return v[size_t(Y - 1) * size_t(w) + size_t(X - 1)];
  };
  struct Seg {
    u64 from, to;
    Vec2 p, q;
  };
  std::vector<Seg> segs;
  // Точка пересечения ребра между узлами (X0, Y0) и (X1, Y1).
  auto cross = [&](int X0, int Y0, int X1, int Y1) {
    const int a = val(X0, Y0), b = val(X1, Y1);
    const double t = clamp((127.5 - a) / double(b - a), 0.02, 0.98);
    return Vec2(X0 - 0.5 + t * (X1 - X0), Y0 - 0.5 + t * (Y1 - Y0));
  };
  // Ключи рёбер: горизонтальное (X, Y)–(X + 1, Y) и вертикальное (X, Y)–(X, Y + 1).
  auto hKey = [&](int X, int Y) { return (u64(u32(Y)) * u64(W2) + u64(u32(X))) * 2; };
  auto vKey = [&](int X, int Y) { return (u64(u32(Y)) * u64(W2) + u64(u32(X))) * 2 + 1; };
  for (int Y = 0; Y <= h; Y++) {
    for (int X = 0; X <= w; X++) {
      const int a = val(X, Y), b = val(X + 1, Y), c = val(X + 1, Y + 1), d = val(X, Y + 1);
      const bool A = a >= 128, B = b >= 128, C = c >= 128, D = d >= 128;
      const int cs = (A ? 1 : 0) | (B ? 2 : 0) | (C ? 4 : 0) | (D ? 8 : 0);
      if (cs == 0 || cs == 15) continue;
      // Рёбра: 0 — верх (a–b), 1 — право (b–c), 2 — низ (d–c), 3 — лево (a–d).
      auto edgePt = [&](int e) {
        switch (e) {
          case 0: return cross(X, Y, X + 1, Y);
          case 1: return cross(X + 1, Y, X + 1, Y + 1);
          case 2: return cross(X, Y + 1, X + 1, Y + 1);
          default: return cross(X, Y, X, Y + 1);
        }
      };
      auto edgeKey = [&](int e) {
        switch (e) {
          case 0: return hKey(X, Y);
          case 1: return vKey(X + 1, Y);
          case 2: return hKey(X, Y + 1);
          default: return vKey(X, Y);
        }
      };
      const Vec2 corner[4] = {Vec2(X - 0.5, Y - 0.5), Vec2(X + 0.5, Y - 0.5), Vec2(X + 0.5, Y + 0.5), Vec2(X - 0.5, Y + 0.5)};
      const bool in[4] = {A, B, C, D};
      const Vec2 center(X, Y);
      // Отрезок между рёбрами e1, e2; t — точка заведомо внутри (справа по ходу).
      auto add = [&](int e1, int e2, Vec2 t) {
        Vec2 p = edgePt(e1), q = edgePt(e2);
        u64 k1 = edgeKey(e1), k2 = edgeKey(e2);
        const double cr = (q.x - p.x) * (t.y - p.y) - (q.y - p.y) * (t.x - p.x);
        if (cr < 0) {
          std::swap(p, q);
          std::swap(k1, k2);
        }
        segs.push_back({k1, k2, p, q});
      };
      // Угол k отсекается рёбрами (k − 1) и k: угол 0 (a) — рёбра 3 и 0, 1 (b) — 0 и 1, 2 (c) — 1 и 2, 3 (d) — 2 и 3.
      auto cutCorner = [&](int k, bool centerInside) {
        const int e1 = (k + 3) % 4, e2 = k;
        add(e1, e2, in[k] ? corner[k] : (centerInside ? center : corner[(k + 2) % 4]));
      };
      if (cs == 5 || cs == 10) {
        const bool mid = (a + b + c + d) >= 510;
        if (cs == 5) {  // a и c внутри
          if (mid) { cutCorner(1, true); cutCorner(3, true); }
          else { cutCorner(0, false); cutCorner(2, false); }
        } else {        // b и d внутри
          if (mid) { cutCorner(0, true); cutCorner(2, true); }
          else { cutCorner(1, false); cutCorner(3, false); }
        }
        continue;
      }
      int cnt = 0;
      for (int k = 0; k < 4; k++) cnt += in[k];
      if (cnt == 1 || cnt == 3) {
        const bool odd = cnt == 1;
        int k = 0;
        while (in[k] != odd) k++;
        // Один угол отличается от прочих: он и отсекается. Внутри — этот угол (если он внутри) или противоположный.
        add((k + 3) % 4, k, in[k] ? corner[k] : corner[(k + 2) % 4]);
      } else {
        // Два соседних угла внутри: отрезок между противоположными рёбрами.
        int e1 = -1, e2 = -1;
        for (int e = 0; e < 4; e++) {
          const bool s0 = in[e], s1 = in[(e + 1) % 4];
          if (s0 != s1) (e1 < 0 ? e1 : e2) = e;
        }
        // Ребро e соединяет углы e и e + 1: 0 — верх (a–b), 1 — право (b–c), 2 — низ (c–d), 3 — лево (d–a).
        Vec2 t;
        for (int k = 0; k < 4; k++)
          if (in[k]) t = corner[k];
        add(e1, e2, t);
      }
    }
  }
  // Сшивка отрезков в кольца.
  std::unordered_map<u64, u32> byFrom;
  byFrom.reserve(segs.size() * 2);
  for (u32 i = 0; i < segs.size(); i++) byFrom.emplace(segs[i].from, i);
  std::vector<u8> used(segs.size(), 0);
  std::vector<Ring> out;
  for (u32 s = 0; s < segs.size(); s++) {
    if (used[s]) continue;
    Ring r;
    u32 i = s;
    while (!used[i]) {
      used[i] = 1;
      r.push_back(segs[i].p);
      auto it = byFrom.find(segs[i].to);
      if (it == byFrom.end()) break;
      i = it->second;
    }
    for (Vec2& p : r) p = Vec2(clamp(p.x, 0.0, double(w)), clamp(p.y, 0.0, double(h)));
    if (r.size() >= 3 && std::fabs(ringArea(r)) >= minArea) out.push_back(std::move(r));
  }
  return out;
}

// Дуглас — Пекер для замкнутого кольца (допуск tol, единицы карты).
Ring simplifyRing(const Ring& r, double tol) {
  const size_t n = r.size();
  if (n < 8) return r;
  // Опорные точки: самая левая и самая далёкая от неё.
  size_t i0 = 0;
  for (size_t i = 1; i < n; i++)
    if (r[i].x < r[i0].x) i0 = i;
  size_t i1 = i0;
  double best = -1;
  for (size_t i = 0; i < n; i++) {
    const double d = (r[i].x - r[i0].x) * (r[i].x - r[i0].x) + (r[i].y - r[i0].y) * (r[i].y - r[i0].y);
    if (d > best) { best = d; i1 = i; }
  }
  std::vector<u8> keep(n, 0);
  keep[i0] = keep[i1] = 1;
  const double tol2 = tol * tol;
  std::vector<std::pair<size_t, size_t>> stack{{i0, i1}, {i1, i0}};
  while (!stack.empty()) {
    auto [a, b] = stack.back();
    stack.pop_back();
    const Vec2 A = r[a], B = r[b];
    const double dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy;
    double far = -1;
    size_t fi = a;
    for (size_t k = (a + 1) % n; k != b; k = (k + 1) % n) {
      const double px = r[k].x - A.x, py = r[k].y - A.y;
      double d2;
      if (L2 <= 1e-12) {
        d2 = px * px + py * py;
      } else {
        const double t = clamp((px * dx + py * dy) / L2, 0.0, 1.0);
        const double ex = px - t * dx, ey = py - t * dy;
        d2 = ex * ex + ey * ey;
      }
      if (d2 > far) { far = d2; fi = k; }
    }
    if (far > tol2) {
      keep[fi] = 1;
      stack.push_back({a, fi});
      stack.push_back({fi, b});
    }
  }
  Ring out;
  for (size_t i = 0; i < n; i++)
    if (keep[i]) out.push_back(r[i]);
  return out.size() >= 3 ? out : r;
}

// Профиль берега: средняя доля воды (B − R) / 255 на расстоянии d = 0..n−1 от береговой линии внутрь суши (in) и в
// море (out). Пути через знаки, реки и озёра и через другой берег не учитываются.
void measureCoast(const codec::RgbaImage& flat, const bake::Layers& L, const std::vector<Ring>& land, int n, ExtractReport& rep) {
  const int w = flat.w, h = flat.h;
  const u8* px = flat.rgba.data();
  auto at = [&](double x, double y, int& t, bool& clean) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    if (ix < 0 || iy < 0 || ix >= w || iy >= h) {
      clean = false;
      return;
    }
    const size_t i = size_t(iy) * size_t(w) + size_t(ix);
    const int R = px[i * 4], B = px[i * 4 + 2];
    t = B - R;
    if (L.symA[i] > 8 || L.inland[i] > 32) clean = false;
  };
  std::vector<double> in(size_t(n), 0), out(size_t(n), 0);
  int count = 0;
  for (const Ring& r : land) {
    for (size_t k = 0; k + 1 < r.size(); k += 3) {
      const Vec2 a = r[k], b = r[k + 1];
      const double len = std::hypot(b.x - a.x, b.y - a.y);
      if (len < 1.5) continue;
      const Vec2 m((a.x + b.x) * 0.5, (a.y + b.y) * 0.5);
      Vec2 nrm(-(b.y - a.y) / len, (b.x - a.x) / len);
      int t1 = 0, t2 = 0;
      bool c1 = true, c2 = true;
      at(m.x + nrm.x * 4, m.y + nrm.y * 4, t1, c1);
      at(m.x - nrm.x * 4, m.y - nrm.y * 4, t2, c2);
      if (!c1 || !c2) continue;
      if (t1 > t2) nrm = Vec2(-nrm.x, -nrm.y);   // nrm — внутрь суши
      std::vector<int> ti(static_cast<size_t>(n)), to(static_cast<size_t>(n));
      bool clean = true;
      for (int d = 0; d < n && clean; d++) {
        at(m.x + nrm.x * (d + 0.5), m.y + nrm.y * (d + 0.5), ti[size_t(d)], clean);
        at(m.x - nrm.x * (d + 0.5), m.y - nrm.y * (d + 0.5), to[size_t(d)], clean);
        if (d >= 3 && (ti[size_t(d)] > 160 || to[size_t(d)] < 96)) clean = false;   // другой берег рядом
      }
      if (!clean) continue;
      for (int d = 0; d < n; d++) {
        in[size_t(d)] += ti[size_t(d)] / 255.0;
        out[size_t(d)] += to[size_t(d)] / 255.0;
      }
      count++;
    }
  }
  rep.glowSamples = count;
  rep.glowIn.assign(size_t(n), 0);
  rep.glowOut.assign(size_t(n), 0);
  for (int d = 0; d < n && count; d++) {
    rep.glowIn[size_t(d)] = in[size_t(d)] / count;
    rep.glowOut[size_t(d)] = out[size_t(d)] / count;
  }
}

// ---------------------------------------------------------------- знаки: поиск штампов
// Штампы исходника одинаковы и стоят в целых пикселях; поздние перекрывают ранние. Образец сравнивается с краской
// (255 − B) в «значимых» пикселях; перекрытый соседями штамп узнаётся по большинству своих пикселей.
struct Stamp {
  Sym kind = Sym::Mountain;
  int w = 0, h = 0;
  int ax = 0, ay = 0;            // точка привязки (середина основания) от левого верхнего угла
  std::vector<i16> ink;          // ожидаемая краска; −1 — не важно
  int care = 0;                  // значимых пикселей
  int kx = 0, ky = 0, k0 = 0, k1 = 0;   // ключ поиска: верхний значимый пиксель (k0) и пиксель под ним (k1)
  int variant = 0;               // форма с точностью до бледных пикселей края
  int design = 0;                // рисунок горы: 0 — светлая вершина, 1 — тёмный кончик вершины
  std::vector<u8> solid;         // непрозрачные пиксели штампа (у гор — весь силуэт с белой шапкой)
};

// Силуэт: в каждой строке — от первого до последнего значимого пикселя.
void solidFromRows(Stamp& s) {
  s.solid.assign(size_t(s.w) * size_t(s.h), 0);
  for (int j = 0; j < s.h; j++) {
    int a = s.w, b = -1;
    for (int i = 0; i < s.w; i++)
      if (s.ink[size_t(j) * size_t(s.w) + size_t(i)] >= 0) {
        a = std::min(a, i);
        b = std::max(b, i);
      }
    for (int i = a; i <= b; i++) s.solid[size_t(j) * size_t(s.w) + size_t(i)] = 1;
  }
}

// Штамп в масштабе sc со сдвигом (fx, fy) ∈ [0, 1): площадное усреднение (как отрисовка уменьшенного или
// увеличенного штампа со сдвигом на долю пикселя). Точка привязки — та же доля образца (ax · sc, ay · sc).
Stamp resampleStamp(const Stamp& s, double sc, double fx, double fy) {
  Stamp o = s;
  o.w = int(std::ceil(s.w * sc + fx)) + 1;
  o.h = int(std::ceil(s.h * sc + fy)) + 1;
  o.ink.assign(size_t(o.w) * size_t(o.h), -1);
  o.care = 0;
  std::vector<double> acc(o.ink.size(), 0), cov(o.ink.size(), 0), sol(o.ink.size(), 0);
  for (int j = 0; j < s.h; j++)
    for (int i = 0; i < s.w; i++) {
      const int t = s.ink[size_t(j) * size_t(s.w) + size_t(i)];
      const bool solidHere = !s.solid.empty() && s.solid[size_t(j) * size_t(s.w) + size_t(i)];
      const double x0 = fx + i * sc, x1 = x0 + sc, y0 = fy + j * sc, y1 = y0 + sc;
      for (int v = int(std::floor(y0)); v < int(std::ceil(y1)); v++)
        for (int u = int(std::floor(x0)); u < int(std::ceil(x1)); u++) {
          const double ov = std::max(0.0, std::min(x1, u + 1.0) - std::max(x0, double(u))) * std::max(0.0, std::min(y1, v + 1.0) - std::max(y0, double(v)));
          if (ov <= 0 || u < 0 || v < 0 || u >= o.w || v >= o.h) continue;
          const size_t k = size_t(v) * size_t(o.w) + size_t(u);
          acc[k] += std::max(0, t) * ov;
          if (t >= 0) cov[k] += ov;
          if (solidHere) sol[k] += ov;
        }
    }
  o.solid.assign(o.ink.size(), 0);
  for (size_t k = 0; k < o.ink.size(); k++) {
    if (cov[k] > 0.25) {
      o.ink[k] = i16(std::min(255L, std::lround(acc[k])));
      o.care++;
    }
    o.solid[k] = sol[k] >= 0.5;
  }
  return o;
}

// Стены: тёмная область -> осевые линии (утончение Чжана — Суэня) с шириной по краске поперёк линии.
std::vector<Line> wallLines(const codec::RgbaImage& flat, const std::vector<u32>& comp, int bx0, int by0, int bw, int bh) {
  const int fw = flat.w, W = bw + 2, H = bh + 2;
  std::vector<u8> m(size_t(W) * size_t(H), 0);
  for (u32 i : comp) {
    const int x = int(i % u32(fw)) - bx0 + 1, y = int(i / u32(fw)) - by0 + 1;
    m[size_t(y) * size_t(W) + size_t(x)] = 255 - int(flat.rgba[size_t(i) * 4 + 2]) >= 100;
  }
  // Утончение.
  std::vector<u8> sk = m;
  auto P = [&](int x, int y) { return int(sk[size_t(y) * size_t(W) + size_t(x)]); };
  for (bool changed = true; changed;) {
    changed = false;
    for (int pass = 0; pass < 2; pass++) {
      std::vector<size_t> del;
      for (int y = 1; y < H - 1; y++)
        for (int x = 1; x < W - 1; x++) {
          if (!P(x, y)) continue;
          const int p2 = P(x, y - 1), p3 = P(x + 1, y - 1), p4 = P(x + 1, y), p5 = P(x + 1, y + 1), p6 = P(x, y + 1), p7 = P(x - 1, y + 1),
                    p8 = P(x - 1, y), p9 = P(x - 1, y - 1);
          const int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
          if (B < 2 || B > 6) continue;
          const int A = (!p2 && p3) + (!p3 && p4) + (!p4 && p5) + (!p5 && p6) + (!p6 && p7) + (!p7 && p8) + (!p8 && p9) + (!p9 && p2);
          if (A != 1) continue;
          if (pass == 0 ? (p2 * p4 * p6 || p4 * p6 * p8) : (p2 * p4 * p8 || p2 * p6 * p8)) continue;
          del.push_back(size_t(y) * size_t(W) + size_t(x));
        }
      for (size_t k : del) sk[k] = 0;
      changed = changed || !del.empty();
    }
  }
  // Осевые линии: самый длинный путь по скелету (диаметр), отростки рядом с ним отбрасываются; так — пока остаются
  // длинные куски (развилки стен).
  const u8* px = flat.rgba.data();
  auto inkAt = [&](double x, double y) {   // краска исходника, билинейно
    const int ix = int(std::floor(x - 0.5)), iy = int(std::floor(y - 0.5));
    const double fx = x - 0.5 - ix, fy = y - 0.5 - iy;
    auto at = [&](int qx, int qy) {
      if (qx < 0 || qy < 0 || qx >= fw || qy >= flat.h) return 0.0;
      return (255 - int(px[(size_t(qy) * size_t(fw) + size_t(qx)) * 4 + 2])) / 255.0;
    };
    return (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy) + (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy;
  };
  std::vector<u8> left = sk;
  std::vector<Line> out;
  std::vector<int> dist(sk.size()), parent(sk.size());
  auto bfs = [&](int start) {
    std::fill(dist.begin(), dist.end(), -1);
    std::vector<int> q{start};
    dist[size_t(start)] = 0;
    parent[size_t(start)] = -1;
    int far = start;
    for (size_t qi = 0; qi < q.size(); qi++) {
      const int c = q[qi], x = c % W, y = c / W;
      if (dist[size_t(c)] > dist[size_t(far)]) far = c;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          const int n = (y + dy) * W + (x + dx);
          if ((dx || dy) && left[size_t(n)] && dist[size_t(n)] < 0) {
            dist[size_t(n)] = dist[size_t(c)] + 1;
            parent[size_t(n)] = c;
            q.push_back(n);
          }
        }
    }
    return far;
  };
  for (int guard = 0; guard < 64; guard++) {
    int any = -1;
    for (size_t k = 0; k < left.size() && any < 0; k++)
      if (left[k]) any = int(k);
    if (any < 0) break;
    const int a = bfs(any);
    const int b = bfs(a);
    std::vector<int> path;
    for (int c = b; c >= 0; c = parent[size_t(c)]) path.push_back(c);
    // Убрать путь и всё рядом с ним (отростки), а также весь его кусок скелета, если путь короткий.
    for (int c : path)
      for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 3; dx++) {
          const int x = c % W + dx, y = c / W + dy;
          if (x >= 0 && y >= 0 && x < W && y < H) left[size_t(y) * size_t(W) + size_t(x)] = 0;
        }
    if (path.size() < 6) {
      for (size_t k = 0; k < left.size(); k++)
        if (dist[k] >= 0) left[k] = 0;
      continue;
    }
    Line l;
    for (int c : path) l.pts.emplace_back(c % W + bx0 - 1 + 0.5, c / W + by0 - 1 + 0.5);
    // Ширина: краска поперёк линии в точках пути (медиана), со сглаженным краем.
    std::vector<double> widths;
    for (size_t k = 2; k + 2 < l.pts.size(); k++) {
      const double dx = l.pts[k + 2].x - l.pts[k - 2].x, dy = l.pts[k + 2].y - l.pts[k - 2].y, L = std::hypot(dx, dy);
      if (L < 1e-6) continue;
      const double nx = -dy / L, ny = dx / L;
      double sum = 0;
      for (double t = -6; t <= 6; t += 0.25) sum += inkAt(l.pts[k].x + nx * t, l.pts[k].y + ny * t) * 0.25;
      widths.push_back(sum);
    }
    if (!widths.empty()) {
      std::nth_element(widths.begin(), widths.begin() + long(widths.size() / 2), widths.end());
      l.w = float(clamp(widths[widths.size() / 2], 1.0, 12.0));
    }
    // Упрощение (Дуглас — Пекер, допуск 0,6).
    const Ring simp = l.pts;
    std::vector<u8> keep(simp.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<size_t, size_t>> st{{0, simp.size() - 1}};
    while (!st.empty()) {
      auto [i0, i1] = st.back();
      st.pop_back();
      double far = -1;
      size_t fi = i0;
      for (size_t k = i0 + 1; k < i1; k++) {
        const double ex = simp[i1].x - simp[i0].x, ey = simp[i1].y - simp[i0].y, L = std::hypot(ex, ey);
        const double dd = L < 1e-9 ? std::hypot(simp[k].x - simp[i0].x, simp[k].y - simp[i0].y)
                                   : std::fabs((simp[k].x - simp[i0].x) * ey - (simp[k].y - simp[i0].y) * ex) / L;
        if (dd > far) {
          far = dd;
          fi = k;
        }
      }
      if (far > 0.6) {
        keep[fi] = 1;
        st.push_back({i0, fi});
        st.push_back({fi, i1});
      }
    }
    l.pts.clear();
    for (size_t k = 0; k < simp.size(); k++)
      if (keep[k]) l.pts.push_back(simp[k]);
    out.push_back(std::move(l));
  }
  return out;
}

Stamp binaryStamp(Sym kind, const std::vector<const char*>& rows, int ax, int ay) {
  Stamp s;
  s.kind = kind;
  s.h = int(rows.size());
  s.w = int(std::strlen(rows[0]));
  s.ax = ax;
  s.ay = ay;
  for (const char* r : rows)
    for (int x = 0; x < s.w; x++) {
      s.ink.push_back(r[x] == '#' ? 255 : 0);
      s.solid.push_back(r[x] == '#');
      s.care++;
    }
  return s;
}

struct Found {
  Sym kind;
  int x, y;       // левый верхний угол штампа
  double score;
  int form = 0;   // номер образца горы
};

class StampFinder {
 public:
  StampFinder(const codec::RgbaImage& flat) : px_(flat.rgba.data()), w_(flat.w), h_(flat.h) {}
  int ink(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
    return 255 - int(px_[(size_t(y) * size_t(w_) + size_t(x)) * 4 + 2]);
  }
  // Доля значимых пикселей штампа, совпавших с изображением (|разница| ≤ tol), и доля «белых» противоречий.
  // Доли совпавших (|разница| ≤ tol) тёмных пикселей штампа (краска ≥ 100) и светлых (значимых, краска < 100).
  struct Score {
    double dark = 0, light = 0;
  };
  Score score(const Stamp& s, int x, int y, int tol) const {
    int dg = 0, dn = 0, lg = 0, ln = 0;
    for (int j = 0; j < s.h; j++)
      for (int i = 0; i < s.w; i++) {
        const int t = s.ink[size_t(j) * size_t(s.w) + size_t(i)];
        if (t < 0) continue;
        const bool ok = std::abs(ink(x + i, y + j) - t) <= tol;
        if (t >= 100) {
          dn++;
          dg += ok;
        } else {
          ln++;
          lg += ok;
        }
      }
    return {dn ? double(dg) / dn : 1.0, ln ? double(lg) / ln : 1.0};
  }

 private:
  const u8* px_;
  int w_, h_;
};

// Образцы гор — частые формы одиночных гор. Формы с одинаковыми телом и вершиной (краска ≥ 100) — один вариант
// рисунка; у каждой формы свой ключ поиска (вершина бывает светлее или темнее).
std::vector<Stamp> mountainStamps(const codec::RgbaImage& flat, int* variants) {
  struct G { int n = 0; InkComponent c; };
  std::unordered_map<u64, G> count;
  for (const InkComponent& c : inkComponents(flat)) {
    const int bw = c.x1 - c.x0 + 1, bh = c.y1 - c.y0 + 1;
    if (!c.mountain || bw < 15 || bw > 21 || bh < 10 || bh > 16) continue;
    G& g = count[c.shape];
    if (g.n++ == 0) g.c = c;
  }
  std::vector<G> groups;
  for (auto& [k, g] : count)
    if (g.n >= 3) groups.push_back(g);
  std::sort(groups.begin(), groups.end(), [](const G& a, const G& b) { return a.n != b.n ? a.n > b.n : (a.c.y0 != b.c.y0 ? a.c.y0 < b.c.y0 : a.c.x0 < b.c.x0); });
  if (groups.size() > 16) groups.resize(16);
  if (groups.empty()) fail("Образцы гор не найдены: на исходнике нет одиночных гор.");
  const StampFinder f(flat);
  std::vector<Stamp> out;
  std::vector<std::vector<int>> cores;   // ядро (краска ≥ 100) в габарите ядра: ширина, высота, значения
  for (const G& g : groups) {
    Stamp s;
    s.kind = Sym::Mountain;
    s.w = g.c.x1 - g.c.x0 + 1;
    s.h = g.c.y1 - g.c.y0 + 1;
    int cx0 = s.w, cy0 = s.h, cx1 = -1, cy1 = -1;
    for (int j = 0; j < s.h; j++)
      for (int i = 0; i < s.w; i++) {
        const int v = f.ink(g.c.x0 + i, g.c.y0 + j);
        s.ink.push_back(v >= 8 ? i16(v) : i16(-1));
        s.care += v >= 8;
        if (v >= 100) {
          cx0 = std::min(cx0, i);
          cx1 = std::max(cx1, i);
          cy0 = std::min(cy0, j);
          cy1 = std::max(cy1, j);
        }
      }
    if (cx1 < 0) continue;
    solidFromRows(s);
    // Привязка — середина основания ядра.
    s.ax = (cx0 + cx1 + 1) / 2;
    s.ay = cy1 + 1;
    // Ключ поиска.
    bool keyed = false;
    for (int j = 0; j + 1 < s.h && !keyed; j++)
      for (int i = 0; i < s.w; i++)
        if (s.ink[size_t(j) * size_t(s.w) + size_t(i)] >= 0) {
          s.kx = i;
          s.ky = j;
          s.k0 = s.ink[size_t(j) * size_t(s.w) + size_t(i)];
          s.k1 = std::max<int>(0, s.ink[size_t(j + 1) * size_t(s.w) + size_t(i)]);
          keyed = true;
          break;
        }
    std::vector<int> core{cx1 - cx0 + 1, cy1 - cy0 + 1};
    for (int j = cy0; j <= cy1; j++)
      for (int i = cx0; i <= cx1; i++) {
        const int v = s.ink[size_t(j) * size_t(s.w) + size_t(i)];
        core.push_back(v >= 100 ? v : 0);
      }
    s.variant = -1;
    for (size_t k = 0; k < cores.size() && s.variant < 0; k++) {
      if (cores[k].size() != core.size() || cores[k][0] != core[0] || cores[k][1] != core[1]) continue;
      bool same = true;
      for (size_t q = 2; q < core.size() && same; q++) same = (cores[k][q] == 0) == (core[q] == 0) && std::abs(cores[k][q] - core[q]) <= 8;
      if (same) s.variant = int(k);
    }
    if (s.variant < 0) {
      s.variant = int(cores.size());
      cores.push_back(core);
    }
    // Рисунок: тёмный кончик вершины в двух верхних строках — рисунок 1, иначе 0.
    for (int j = 0; j < 2 && j < s.h; j++)
      for (int i = 0; i < s.w; i++)
        if (s.ink[size_t(j) * size_t(s.w) + size_t(i)] >= 150) s.design = 1;
    out.push_back(std::move(s));
  }
  if (variants) *variants = int(cores.size());
  return out;
}

}  // namespace

std::string ExtractReport::text() const {
  std::string s = strf("Суша: %d колец (%lld точек), островков %d\n", landRings, (long long)landPoints, isletRings);
  s += strf("Внутренние воды: %d колец (%lld точек)\n", waterRings, (long long)waterPoints);
  s += "Знаки:";
  for (int k = 0; k < int(Sym::Count); k++) s += strf(" %s %d", symName(Sym(k)), symbols[size_t(k)]);
  s += strf(" (образцов гор %d, вариантов рисунка %d; со сдвигом на долю пикселя %d); линий стен %d", mountainForms, mountainVariants, subpixel, walls);
  s += strf("\nПорядок наложения: правил %d, разорванных циклов %d", orderEdges, orderCycles);
  s += strf("\nКраска: %lld пикселей, не объяснено объектами %lld (%.2f %%)\n", (long long)inkPixels, (long long)unexplainedInk,
            inkPixels ? 100.0 * double(unexplainedInk) / double(inkPixels) : 0.0);
  s += strf("Профиль берега (%d замеров), доля воды внутрь суши:", glowSamples);
  for (double v : glowIn) s += strf(" %.3f", v);
  s += "\n                                 в море:";
  for (double v : glowOut) s += strf(" %.3f", v);
  s += strf("\nГотово за %.2f с\n", seconds);
  return s;
}

MapArt extract(const codec::RgbaImage& flat, ExtractReport* report, const std::string& debugDir) {
  const double t0 = nowSeconds();
  ExtractReport rep;
  const int w = flat.w, h = flat.h;
  MapArt art;
  art.width = w;
  art.height = h;

  // 1. Слои исходника (те же, что у прежней базовой карты): море, внутренние воды, знаки, поле берега.
  const bake::Options bo;
  bake::SegmentStats ss;
  const bake::Layers L = bake::segment(flat, bo, &ss);

  // 2. Береговая линия — тем же способом и с теми же параметрами, что и береговая линия миров (geo::Coast).
  bake::CoastStats cs;
  const std::vector<bake::IRing> rings = bake::simplifyCoast(bake::traceCoast(L.field.data(), w, h, bo.minIsletArea, &cs), bo.simplifyTol, w, h, &cs);
  for (const bake::IRing& r : rings) art.land.push_back(toRing(r));
  // Островки меньше minIsletArea в береговую линию не входят — только в рисунок.
  {
    std::vector<bake::IRing> small;
    for (bake::IRing& r : bake::traceCoast(L.field.data(), w, h, 1.0, nullptr))
      if (std::fabs(bake::ringArea(r)) < bo.minIsletArea) small.push_back(std::move(r));
    for (const bake::IRing& r : bake::simplifyCoast(small, 0.5, w, h, nullptr))
      if (r.size() >= 3) art.islets.push_back(toRing(r));
  }
  rep.landRings = int(art.land.size());
  for (const Ring& r : art.land) rep.landPoints += i64(r.size());
  rep.isletRings = int(art.islets.size());
  measureCoast(flat, L, art.land, 24, rep);

  // 3. Внутренние воды — реки и озёра: изолинии слоя inland (под знаками он продолжен от соседей).
  for (const Ring& r : traceIso(L.inland.data(), w, h, 0.6)) {
    art.water.push_back(simplifyRing(r, 0.35));
    rep.waterPoints += i64(art.water.back().size());
  }
  rep.waterRings = int(art.water.size());

  // 4. Знаки: штампы башен, замков и гор (поиск по ключевым пикселям, подтверждение по всему штампу).
  static const std::vector<const char*> kTower = {"##..##..##", "##..##..##", "##########", "##########", "##########", "##########", "##########",
                                                  "##########", "##########", "##########", "##########", "##########", "##########", "##########",
                                                  "##########"};
  static const std::vector<const char*> kCastle = {
      "..............####....####..............", "..............####.##.####..............", "..............####.##.####..............",
      "..............############..............", "..............#..##..##..#..............", "..............#...#..#...#..............",
      "..............#...#..#...#..............", "..............#...#..#...#..............", "..............#...#..#...#..............",
      "..............#...#..#...#..............", "..............#...#..#...#..............", "..............#...#..#...#..............",
      "..............#...#..#...#..............", "##.#.##.......#...#..#...#.......##.#.##", "#######.......#...#..#...#.......#######",
      "#.....#.......#...#..#...#.......#.....#", "#.....#.......#...#..#...#.......#.....#", "#.....#.#.#.#.#.#.#..#.#.#.#.#.#.#.....#",
      "#.....############################.....#", "#.....#..........................#.....#", "#.....#..........................#.....#",
      "#.....#..........................#.....#", "#.....#..........................#.....#", "#.....#..........................#.....#",
      "#.....#..........................#.....#", "#.....#..........................#.....#", "#.....#...........####...........#.....#",
      "#.....#..........#.##.#..........#.....#", "#.....#..........#.##.#..........#.....#", "#.....#..........#.##.#..........#.....#",
      "#.....#..........#.##.#..........#.....#",
  };
  const StampFinder F(flat);
  int mountainVariants = 0;
  const std::vector<Stamp> mountains = mountainStamps(flat, &mountainVariants);
  rep.mountainForms = int(mountains.size());
  rep.mountainVariants = mountainVariants;
  if (!debugDir.empty()) {
    // Образцы гор: по одному на вариант рисунка, ×10 (форма — краска образца, белое — незначимые пиксели).
    fs::makeDirs(debugDir);
    const int K = 10, cell = 22 * K;
    codec::RgbaImage sheet;
    sheet.w = cell * std::max(1, mountainVariants);
    sheet.h = cell;
    sheet.rgba.assign(size_t(sheet.w) * size_t(sheet.h) * 4, 255);
    std::vector<u8> shown(size_t(mountainVariants), 0);
    for (const Stamp& st : mountains) {
      if (shown[size_t(st.variant)]) continue;
      shown[size_t(st.variant)] = 1;
      for (int j = 0; j < st.h * K; j++)
        for (int i = 0; i < st.w * K; i++) {
          const int v = st.ink[size_t(j / K) * size_t(st.w) + size_t(i / K)];
          u8* d = &sheet.rgba[(size_t(j + K) * size_t(sheet.w) + size_t(st.variant * cell + i + K)) * 4];
          const u8 g = u8(v < 0 ? 255 : 255 - v);
          d[0] = d[1] = d[2] = g;
          if (i % K == 0 || j % K == 0) d[0] = d[1] = d[2] = u8(g * 0.85);
        }
      // Точка привязки — красным.
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
          const int px = st.variant * cell + K + st.ax * K + dx, py = K + st.ay * K + dy;
          if (px < 0 || py < 0 || px >= sheet.w || py >= sheet.h) continue;
          u8* d = &sheet.rgba[(size_t(py) * size_t(sheet.w) + size_t(px)) * 4];
          d[0] = 255;
          d[1] = d[2] = 0;
        }
    }
    codec::writePngFile(fs::join(debugDir, "mountains.png"), sheet, 6);
  }
  const Stamp tower = binaryStamp(Sym::Tower, kTower, 5, 15);
  const Stamp castle = binaryStamp(Sym::Castle, kCastle, 20, 31);
  std::vector<std::vector<Found>> parts(static_cast<size_t>(h));
  jobs::parallelFor(size_t(h), [&](size_t yy) {
    const int y = int(yy);
    std::vector<Found>& out = parts[yy];
    for (int x = 0; x < w; x++) {
      const int i0 = F.ink(x, y);
      if (i0 >= 200 && F.ink(x + 1, y) >= 200 && F.ink(x + 2, y) < 60 && F.ink(x + 3, y) < 60 && F.ink(x + 4, y) >= 200 && F.ink(x + 8, y) >= 200 &&
          F.ink(x, y + 2) >= 200 && F.ink(x + 2, y + 2) >= 200) {
        const auto s = F.score(tower, x, y, 60);
        if (s.dark >= 0.9 && s.light >= 0.75) out.push_back({Sym::Tower, x, y, s.dark + s.light});
      }
      if (F.ink(x + 14, y) >= 200 && F.ink(x + 17, y) >= 200 && F.ink(x + 18, y) < 60 && F.ink(x + 21, y) < 60 && F.ink(x + 22, y) >= 200 &&
          F.ink(x + 25, y) >= 200) {
        const auto s = F.score(castle, x, y, 60);
        if (s.dark >= 0.9 && s.light >= 0.8) out.push_back({Sym::Castle, x, y, s.dark + s.light});
      }
      for (int m = 0; m < int(mountains.size()); m++) {
        const Stamp& st = mountains[size_t(m)];
        if (std::abs(F.ink(x + st.kx, y + st.ky) - st.k0) > 14 || std::abs(F.ink(x + st.kx, y + st.ky + 1) - st.k1) > 14) continue;
        const auto s = F.score(st, x, y, 14);
        if (s.dark >= 0.6 && s.light >= 0.4) out.push_back({Sym::Mountain, x, y, s.dark + s.light, m});
      }
    }
  });
  std::vector<Found> cand;
  for (auto& p : parts) cand.insert(cand.end(), p.begin(), p.end());
  // Повторы одного штампа (сдвиг на строку и т. п.): лучший по оценке, остальные в пределах половины штампа — отброс.
  std::sort(cand.begin(), cand.end(), [](const Found& a, const Found& b) { return a.score != b.score ? a.score > b.score : (a.y != b.y ? a.y < b.y : a.x < b.x); });
  std::vector<Found> found;
  {
    std::unordered_map<u64, std::vector<size_t>> cells;   // ячейки 64 × 64 для поиска соседей
    auto cellKey = [](int x, int y) { return (u64(u32(y >> 6)) << 32) | u32(x >> 6); };
    for (const Found& f : cand) {
      const Stamp& s = f.kind == Sym::Tower ? tower : f.kind == Sym::Castle ? castle : mountains[size_t(f.form)];
      bool dup = false;
      for (int cy = (f.y >> 6) - 1; cy <= (f.y >> 6) + 1 && !dup; cy++)
        for (int cx = (f.x >> 6) - 1; cx <= (f.x >> 6) + 1 && !dup; cx++) {
          auto it = cells.find((u64(u32(cy)) << 32) | u32(cx));
          if (it == cells.end()) continue;
          for (size_t k : it->second) {
            const Found& g = found[k];
            if (g.kind == f.kind && std::abs(g.x - f.x) * 2 < s.w && std::abs(g.y - f.y) * 2 < s.h) {
              dup = true;
              break;
            }
          }
        }
      if (dup) continue;
      cells[cellKey(f.x, f.y)].push_back(found.size());
      found.push_back(f);
    }
  }
  // Знак и его штамп на исходнике (для порядка наложения).
  struct Placed {
    const Stamp* st;
    int x, y;
  };
  std::vector<Placed> placed;
  for (const Found& f : found) {
    const Stamp& s = f.kind == Sym::Tower ? tower : f.kind == Sym::Castle ? castle : mountains[size_t(f.form)];
    art.symbols.push_back(Symbol{f.kind, float(f.x + s.ax), float(f.y + s.ay), 1, u8(f.kind == Sym::Mountain ? s.design : 0)});
    placed.push_back({&s, f.x, f.y});
    rep.symbols[size_t(f.kind)]++;
  }
  // Объяснённые штампами пиксели (для поиска стен, крупных гор и проверки).
  std::vector<u8> explained(size_t(w) * size_t(h), 0);
  // Объяснены пиксели, где штамп ждёт краску и она совпала (части, закрытые соседями, остаются необъяснёнными).
  auto markStamp = [&](const Stamp& s, int x0, int y0) {
    for (int j = 0; j < s.h; j++)
      for (int i = 0; i < s.w; i++) {
        const int x = x0 + i, y = y0 + j;
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        const int t = s.ink[size_t(j) * size_t(s.w) + size_t(i)];
        if (t >= 24 && std::abs(F.ink(x, y) - t) <= 48) explained[size_t(y) * size_t(w) + size_t(x)] = 1;
      }
  };
  for (const Found& f : found) markStamp(f.kind == Sym::Tower ? tower : f.kind == Sym::Castle ? castle : mountains[size_t(f.form)], f.x, f.y);
  auto isInk = [&](size_t i) { return 255 - int(flat.rgba[i * 4 + 2]) >= 32 && int(flat.rgba[i * 4 + 2]) - int(flat.rgba[i * 4]) <= 40; };

  // 5. Второй проход: штампы со сдвигом на долю пикселя, другого размера и частично перекрытые — в необъяснённых
  //    областях краски. Из подходящих вариантов берётся объясняющий больше краски (крупная гора, а не три мелких).
  struct Base {
    const Stamp* st;
    double scale;
    std::vector<Stamp> shifts;   // 16 сдвигов по 1/4 пикселя
    int dark = 0;                // тёмных пикселей без сдвига
  };
  std::vector<Base> bases;
  {
    auto addBase = [&](const Stamp& st, double sc) {
      Base b{&st, sc, {}, 0};
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) b.shifts.push_back(resampleStamp(st, sc, sx * 0.25, sy * 0.25));
      for (i16 t : b.shifts[0].ink) b.dark += t >= 100;
      bases.push_back(std::move(b));
    };
    for (double sc : {0.92, 0.96, 1.0, 1.04, 1.08}) {
      addBase(castle, sc);
      addBase(tower, sc);
    }
    for (int d = 0; d < 2; d++)
      for (const Stamp& st : mountains)
        if (st.design == d) {
          for (double sc : {0.9, 1.0, 1.1}) addBase(st, sc);
          // Крупные горы на исходнике — увеличенный рисунок 0 (светлая вершина).
          if (d == 0)
            for (double sc : {1.5, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0}) addBase(st, sc);
          break;
        }
    std::vector<u8> seen(size_t(w) * size_t(h), 0);
    std::vector<u32> comp, stack;
    for (size_t start = 0; start < explained.size(); start++) {
      if (seen[start] || explained[start] || !isInk(start)) continue;
      comp.clear();
      stack.push_back(u32(start));
      seen[start] = 1;
      while (!stack.empty()) {
        const u32 i = stack.back();
        stack.pop_back();
        comp.push_back(i);
        const int x = int(i % u32(w)), y = int(i / u32(w));
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const size_t j = size_t(ny) * size_t(w) + size_t(nx);
            if (!seen[j] && !explained[j] && isInk(j)) {
              seen[j] = 1;
              stack.push_back(u32(j));
            }
          }
      }
      if (comp.size() < 10) continue;
      int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
      for (u32 i : comp) {
        bx0 = std::min(bx0, int(i % u32(w)));
        bx1 = std::max(bx1, int(i % u32(w)));
        by0 = std::min(by0, int(i / u32(w)));
        by1 = std::max(by1, int(i / u32(w)));
      }
      for (int round = 0; round < 16; round++) {
        int bestHit = 0, bestB = -1, bestS = 0, bestX = 0, bestY = 0;
        for (size_t b = 0; b < bases.size(); b++) {
          const Stamp& T = bases[b].shifts[0];
          if (T.w > (bx1 - bx0 + 1) + 8 || T.h > (by1 - by0 + 1) + 8) continue;   // крупнее области — не она
          // Грубо: целые положения, где тёмные пиксели образца ложатся на необъяснённую краску.
          std::vector<std::pair<int, std::pair<int, int>>> top;
          for (int y = by0 - T.h + 1; y <= by1; y++)
            for (int x = bx0 - T.w + 1; x <= bx1; x++) {
              int hit = 0;
              for (int j = 0; j < T.h; j++)
                for (int i = 0; i < T.w; i++) {
                  if (T.ink[size_t(j) * size_t(T.w) + size_t(i)] < 100) continue;
                  const int px = x + i, py = y + j;
                  if (px < 0 || py < 0 || px >= w || py >= h) continue;
                  const size_t k = size_t(py) * size_t(w) + size_t(px);
                  hit += !explained[k] && F.ink(px, py) >= 48;
                }
              if (hit * 20 >= bases[b].dark * 9) top.push_back({hit, {x, y}});
            }
          std::sort(top.begin(), top.end(), [](const auto& a, const auto& c) { return a.first > c.first; });
          if (top.size() > 3) top.resize(3);
          // Точно: сдвиги на долю пикселя около лучших целых положений.
          for (const auto& [hit, xy] : top)
            for (int oy = -1; oy <= 0; oy++)
              for (int ox = -1; ox <= 0; ox++)
                for (int s = 0; s < 16; s++) {
                  const Stamp& S = bases[b].shifts[size_t(s)];
                  const auto sc = F.score(S, xy.first + ox, xy.second + oy, 48);
                  if (sc.dark < 0.6 || sc.light < 0.3) continue;
                  // Не второй штамп на месте уже найденного (частично необъяснённые пиксели знака — не новый знак).
                  {
                    const Stamp& T0 = *bases[b].st;
                    const double cxA = xy.first + ox + (s % 4) * 0.25 + T0.ax * bases[b].scale;
                    const double cyA = xy.second + oy + (s / 4) * 0.25 + T0.ay * bases[b].scale;
                    const double rx = 0.4 * T0.w * bases[b].scale, ry = 0.4 * T0.h * bases[b].scale;
                    bool near = false;
                    for (const Symbol& q : art.symbols) {
                      const bool sameKind = (q.kind == Sym::Mountain || q.kind == Sym::Peak) == (T0.kind == Sym::Mountain) && (T0.kind == Sym::Mountain || q.kind == T0.kind);
                      if (sameKind && std::fabs(q.x - cxA) < rx && std::fabs(q.y - cyA) < ry) {
                        near = true;
                        break;
                      }
                    }
                    if (near) continue;
                  }
                  // Ценность — тёмные пиксели штампа, совпавшие с ещё не объяснённой краской, за вычетом несовпавших
                  // (крупный образец не получает зачёт за тело соседних знаков).
                  int unexpl = 0, miss = 0;
                  for (int j = 0; j < S.h; j++)
                    for (int i = 0; i < S.w; i++) {
                      const int t = S.ink[size_t(j) * size_t(S.w) + size_t(i)];
                      if (t < 100) continue;
                      const int px = xy.first + ox + i, py = xy.second + oy + j;
                      if (px < 0 || py < 0 || px >= w || py >= h) continue;
                      if (std::abs(F.ink(px, py) - t) > 48) miss++;
                      else if (!explained[size_t(py) * size_t(w) + size_t(px)]) unexpl++;
                    }
                  if (unexpl * 10 < bases[b].dark * 3) continue;
                  const int value = unexpl * 2 - miss;
                  if (value > bestHit) {
                    bestHit = value;
                    bestB = int(b);
                    bestS = s;
                    bestX = xy.first + ox;
                    bestY = xy.second + oy;
                  }
                }
        }
        if (bestB < 0) break;
        const Base& B = bases[size_t(bestB)];
        const Stamp& T = *B.st;
        const double fx = (bestS % 4) * 0.25, fy = (bestS / 4) * 0.25;
        Sym kind = T.kind;
        if (kind == Sym::Mountain && B.scale >= 1.4) kind = Sym::Peak;
        art.symbols.push_back(Symbol{kind, float(bestX + fx + T.ax * B.scale), float(bestY + fy + T.ay * B.scale), float(B.scale),
                                     u8(T.kind == Sym::Mountain ? T.design : 0)});
        rep.symbols[size_t(kind)]++;
        rep.subpixel++;
        placed.push_back({&B.shifts[size_t(bestS)], bestX, bestY});
        markStamp(B.shifts[size_t(bestS)], bestX, bestY);
        if (kind == Sym::Peak) {  // размытый ореол крупной горы — тоже она (а не новые знаки)
          const Stamp& S = B.shifts[size_t(bestS)];
          for (int y = std::max(0, bestY - 3); y < std::min(h, bestY + S.h + 3); y++)
            for (int x = std::max(0, bestX - 3); x < std::min(w, bestX + S.w + 3); x++) explained[size_t(y) * size_t(w) + size_t(x)] = 1;
        }
      }
    }
  }

  // 6. Остаток: крупные горы — знак «вершина» (масштаб по высоте тела), вытянутые тёмные области — стены.
  {
    std::vector<u8> seen(size_t(w) * size_t(h), 0);
    std::vector<u32> comp, stack;
    for (size_t start = 0; start < explained.size(); start++) {
      if (seen[start] || explained[start] || !isInk(start)) continue;
      comp.clear();
      stack.push_back(u32(start));
      seen[start] = 1;
      while (!stack.empty()) {
        const u32 i = stack.back();
        stack.pop_back();
        comp.push_back(i);
        const int x = int(i % u32(w)), y = int(i / u32(w));
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const size_t j = size_t(ny) * size_t(w) + size_t(nx);
            if (!seen[j] && !explained[j] && isInk(j)) {
              seen[j] = 1;
              stack.push_back(u32(j));
            }
          }
      }
      if (comp.size() < 12) continue;
      int bx0 = w, by0 = h, bx1 = -1, by1 = -1, body = 0, dark = 0, coreY0 = h, coreY1 = -1, coreX0 = w, coreX1 = -1;
      for (u32 i : comp) {
        const int x = int(i % u32(w)), y = int(i / u32(w));
        bx0 = std::min(bx0, x);
        bx1 = std::max(bx1, x);
        by0 = std::min(by0, y);
        by1 = std::max(by1, y);
        const int v = F.ink(x, y);
        if (L.kind[i] & bake::KindMountain) body++;
        if (v >= 140 && minRgb(flat.rgba.data(), i) < 90) dark++;
        if (v >= 100) {
          coreY0 = std::min(coreY0, y);
          coreY1 = std::max(coreY1, y);
          coreX0 = std::min(coreX0, x);
          coreX1 = std::max(coreX1, x);
        }
      }
      const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
      if (body * 2 >= int(comp.size()) && bh >= 24 && coreY1 >= coreY0) {
        // Крупная гора: высота тела обычной горы — 13 единиц.
        const float s = float(coreY1 - coreY0 + 1) / 13.0f;
        art.symbols.push_back(Symbol{Sym::Peak, float(coreX0 + coreX1 + 1) * 0.5f, float(coreY1 + 1), s, 0});
        rep.symbols[size_t(Sym::Peak)]++;
        for (u32 i : comp) explained[i] = 1;
        continue;
      }
      if (dark * 3 < int(comp.size()) || std::max(bw, bh) < 12) continue;
      for (Line& l : wallLines(flat, comp, bx0, by0, bw, bh)) art.lines.push_back(std::move(l));
      for (u32 i : comp) explained[i] = 1;
      rep.walls = int(art.lines.size());
    }
  }
  // Концы стен у башен и замков уходят под знак (на исходнике стена продолжается под штампом башни).
  for (Line& l : art.lines) {
    for (int end = 0; end < 2; end++) {
      Vec2& p = end ? l.pts.back() : l.pts.front();
      for (const Symbol& q : art.symbols) {
        if (q.kind != Sym::Tower && q.kind != Sym::Castle) continue;
        const RectF b = q.kind == Sym::Tower ? RectF(-5, -15, 10, 15) : RectF(-20, -31, 40, 31);
        const double x0 = q.x + b.x * q.s - 3, x1 = q.x + b.right() * q.s + 3, y0 = q.y + b.y * q.s - 3, y1 = q.y + b.bottom() * q.s + 3;
        if (p.x < x0 || p.x > x1 || p.y < y0 || p.y > y1) continue;
        const Vec2 c(q.x, q.y + b.cy() * q.s);
        if (end) l.pts.push_back(c);
        else l.pts.insert(l.pts.begin(), c);
        break;
      }
    }
  }
  i64 inkPx = 0, unexplained = 0;
  for (size_t i = 0; i < explained.size(); i++) {
    if (255 - int(flat.rgba[i * 4 + 2]) < 32 || int(flat.rgba[i * 4 + 2]) - int(flat.rgba[i * 4]) > 40) continue;   // не краска или вода
    inkPx++;
    unexplained += !explained[i];
  }
  rep.inkPixels = inkPx;
  rep.unexplainedInk = unexplained;
  if (!debugDir.empty()) {
    fs::makeDirs(debugDir);
    codec::RgbaImage dbg = flat;
    for (size_t i = 0; i < explained.size(); i++) {
      const bool inkHere = 255 - int(flat.rgba[i * 4 + 2]) >= 32 && int(flat.rgba[i * 4 + 2]) - int(flat.rgba[i * 4]) <= 40;
      if (inkHere && !explained[i]) {
        dbg.rgba[i * 4] = 255;
        dbg.rgba[i * 4 + 1] = 0;
        dbg.rgba[i * 4 + 2] = 0;
      }
    }
    codec::writePngFile(fs::join(debugDir, "unexplained.png"), dbg, 6);
    // Точки привязки знаков: зелёные (первый проход) и голубые (второй) крестики.
    codec::RgbaImage anc = flat;
    for (size_t k = 0; k < art.symbols.size(); k++) {
      const Symbol& q = art.symbols[k];
      const int cx = int(std::lround(q.x)), cy = int(std::lround(q.y));
      const bool second = std::fabs(q.x - std::round(q.x)) > 0.01 || std::fabs(q.y - std::round(q.y)) > 0.01 || q.s != 1;
      for (int d = -2; d <= 2; d++)
        for (int e = 0; e < 2; e++) {
          const int px = e ? cx + d : cx, py = e ? cy : cy + d;
          if (px < 0 || py < 0 || px >= w || py >= h) continue;
          u8* o = &anc.rgba[(size_t(py) * size_t(w) + size_t(px)) * 4];
          o[0] = 0;
          o[1] = second ? 160 : 200;
          o[2] = second ? 255 : 0;
        }
    }
    codec::writePngFile(fs::join(debugDir, "anchors.png"), anc, 6);
  }
  // 7. Порядок наложения знаков — как на исходнике: в общей части двух перекрывающихся штампов видно тот, что сверху.
  {
    const size_t n = art.symbols.size();
    std::unordered_map<u64, std::vector<u32>> cells;
    auto key = [](int cx, int cy) { return (u64(u32(cy)) << 32) | u32(cx); };
    for (u32 i = 0; i < n; i++) {
      const Placed& p = placed[i];
      for (int cy = p.y >> 6; cy <= (p.y + p.st->h) >> 6; cy++)
        for (int cx = p.x >> 6; cx <= (p.x + p.st->w) >> 6; cx++) cells[key(cx, cy)].push_back(i);
    }
    std::vector<std::vector<u32>> above(n);   // above[i] — знаки, лежащие поверх i
    std::vector<int> indeg(n, 0);
    std::unordered_set<u64> done;
    int edges = 0;
    for (auto& [k, list] : cells)
      for (size_t a = 0; a < list.size(); a++)
        for (size_t b = a + 1; b < list.size(); b++) {
          const u32 i = std::min(list[a], list[b]), j = std::max(list[a], list[b]);
          if (!done.insert((u64(i) << 32) | j).second) continue;
          const Placed &A = placed[i], &B = placed[j];
          const int x0 = std::max(A.x, B.x), x1 = std::min(A.x + A.st->w, B.x + B.st->w);
          const int y0 = std::max(A.y, B.y), y1 = std::min(A.y + A.st->h, B.y + B.st->h);
          if (x0 >= x1 || y0 >= y1) continue;
          int va = 0, vb = 0;
          for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
              // Свидетельствуют только пиксели, где непрозрачны оба штампа (иначе вид не зависит от порядка).
              const size_t ka = size_t(y - A.y) * size_t(A.st->w) + size_t(x - A.x), kb = size_t(y - B.y) * size_t(B.st->w) + size_t(x - B.x);
              if (!A.st->solid[ka] || !B.st->solid[kb]) continue;
              const int ta = std::max<int>(0, A.st->ink[ka]), tb = std::max<int>(0, B.st->ink[kb]);
              if (std::abs(ta - tb) < 40) continue;
              const int v = F.ink(x, y), da = std::abs(v - ta), db = std::abs(v - tb);
              if (da + 10 < db) va++;
              else if (db + 10 < da) vb++;
            }
          if (va > vb + 2) {
            above[j].push_back(i);
            indeg[i]++;
            edges++;
          } else if (vb > va + 2) {
            above[i].push_back(j);
            indeg[j]++;
            edges++;
          }
        }
    // Топологический порядок; при свободе выбора — сверху вниз по основанию.
    auto later = [&](u32 a, u32 b) { return art.symbols[a].y != art.symbols[b].y ? art.symbols[a].y > art.symbols[b].y : a > b; };
    std::priority_queue<u32, std::vector<u32>, decltype(later)> ready(later);
    for (u32 i = 0; i < n; i++)
      if (!indeg[i]) ready.push(i);
    std::vector<u32> order;
    std::vector<u8> out(n, 0);
    while (order.size() < n) {
      if (ready.empty()) {  // цикл (почти не бывает): берём верхний из оставшихся
        u32 pick = u32(n);
        for (u32 i = 0; i < n; i++)
          if (!out[i] && (pick == n || later(pick, i))) pick = i;
        indeg[pick] = 0;
        ready.push(pick);
        rep.orderCycles++;
      }
      const u32 i = ready.top();
      ready.pop();
      if (out[i]) continue;
      out[i] = 1;
      order.push_back(i);
      for (u32 j : above[i])
        if (--indeg[j] == 0) ready.push(j);
    }
    std::vector<Symbol> sorted;
    sorted.reserve(n);
    for (u32 i : order) sorted.push_back(art.symbols[i]);
    art.symbols = std::move(sorted);
    rep.orderEdges = edges;
  }

  rep.seconds = nowSeconds() - t0;
  if (report) *report = rep;
  return art;
}

std::vector<MountainSample> mountainSamples(const codec::RgbaImage& flat) {
  std::vector<MountainSample> out;
  for (const Stamp& st : mountainStamps(flat, nullptr)) {
    bool have = false;
    for (const MountainSample& m : out) have = have || m.design == st.design;
    if (have) continue;
    out.push_back({st.design, st.w, st.h, st.ax, st.ay, st.ink});
  }
  std::sort(out.begin(), out.end(), [](const MountainSample& a, const MountainSample& b) { return a.design < b.design; });
  return out;
}

void writeInventory(const codec::RgbaImage& flat, const std::string& dir) {
  fs::makeDirs(dir);
  const std::vector<InkComponent> comps = inkComponents(flat);
  struct Group {
    u64 shape = 0;
    std::vector<size_t> members;
  };
  std::unordered_map<u64, size_t> index;
  std::vector<Group> groups;
  for (size_t i = 0; i < comps.size(); i++) {
    auto [it, fresh] = index.try_emplace(comps[i].shape, groups.size());
    if (fresh) groups.push_back({comps[i].shape, {}});
    groups[it->second].members.push_back(i);
  }
  std::vector<size_t> order(groups.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    if (groups[a].members.size() != groups[b].members.size()) return groups[a].members.size() > groups[b].members.size();
    return comps[groups[a].members[0]].pixels > comps[groups[b].members[0]].pixels;
  });
  std::string txt = strf("Связных областей краски: %zu, разных форм: %zu\n", comps.size(), groups.size());
  int mountains = 0, darkComps = 0;
  for (const InkComponent& c : comps) {
    mountains += c.mountain;
    darkComps += c.dark > 0;
  }
  txt += strf("С телом горы: %d, с тёмными пикселями: %d\n\n", mountains, darkComps);
  txt += "№    раз  ширина×высота  пикселей  тёмных  гора  пример (x, y)\n";
  for (size_t k = 0; k < order.size() && k < 400; k++) {
    const Group& g = groups[order[k]];
    const InkComponent& c = comps[g.members[0]];
    txt += strf("%-4zu %5zu  %4d×%-4d      %7d  %6d  %s   %d, %d\n", k, g.members.size(), c.x1 - c.x0 + 1, c.y1 - c.y0 + 1, c.pixels, c.dark,
                c.mountain ? "да " : "нет", c.x0, c.y0);
  }
  // Крупные области (скопления гор, стены, пунктиры, необычные знаки).
  std::vector<size_t> big;
  for (size_t i = 0; i < comps.size(); i++) big.push_back(i);
  std::sort(big.begin(), big.end(), [&](size_t a, size_t b) { return comps[a].pixels > comps[b].pixels; });
  txt += "\nКрупнейшие области:\n";
  for (size_t k = 0; k < big.size() && k < 60; k++) {
    const InkComponent& c = comps[big[k]];
    txt += strf("  %4d×%-4d  пикселей %7d  тёмных %6d  гора %s  (%d, %d)\n", c.x1 - c.x0 + 1, c.y1 - c.y0 + 1, c.pixels, c.dark, c.mountain ? "да " : "нет",
                c.x0, c.y0);
  }
  std::string err;
  fs::writeFileAtomic(fs::join(dir, "inventory.txt"), txt, &err);
  std::fputs(txt.substr(0, std::min<size_t>(txt.size(), 6000)).c_str(), stdout);

  // Лист образцов: пример каждой из первых групп, увеличенный в K раз (полками слева направо).
  auto sheet = [&](const std::vector<size_t>& compIdx, int K, const std::string& name) {
    const int W = 2400, gap = 6;
    struct Place { size_t c; int x, y, cw, ch; };
    std::vector<Place> places;
    int x = gap, y = gap, rowH = 0;
    for (size_t ci : compIdx) {
      const InkComponent& c = comps[ci];
      const int cw = std::min(W - 2 * gap, (c.x1 - c.x0 + 5) * K), ch = std::min(600, (c.y1 - c.y0 + 5) * K);
      if (x + cw + gap > W) {
        x = gap;
        y += rowH + gap;
        rowH = 0;
      }
      places.push_back({ci, x, y, cw, ch});
      x += cw + gap;
      rowH = std::max(rowH, ch);
    }
    codec::RgbaImage out;
    out.w = W;
    out.h = y + rowH + gap;
    out.rgba.assign(size_t(out.w) * size_t(out.h) * 4, 255);
    for (size_t i = 0; i < out.rgba.size(); i += 4) {
      out.rgba[i] = 200;
      out.rgba[i + 1] = 30;
      out.rgba[i + 2] = 200;
    }
    for (const Place& p : places) {
      const InkComponent& c = comps[p.c];
      for (int yy = 0; yy < p.ch; yy++)
        for (int xx = 0; xx < p.cw; xx++) {
          const int sx = c.x0 - 2 + xx / K, sy = c.y0 - 2 + yy / K;
          u8* d = &out.rgba[(size_t(p.y + yy) * size_t(out.w) + size_t(p.x + xx)) * 4];
          if (sx < 0 || sy < 0 || sx >= flat.w || sy >= flat.h) continue;
          const u8* s = &flat.rgba[(size_t(sy) * size_t(flat.w) + size_t(sx)) * 4];
          d[0] = s[0];
          d[1] = s[1];
          d[2] = s[2];
        }
    }
    codec::writePngFile(fs::join(dir, name), out, 6);
  };
  std::vector<size_t> top;
  for (size_t k = 0; k < order.size() && k < 160; k++) top.push_back(groups[order[k]].members[0]);
  sheet(top, 6, "inventory.png");
  std::vector<size_t> largest(big.begin(), big.begin() + std::min<size_t>(big.size(), 40));
  sheet(largest, 2, "inventory_large.png");
}

}  // namespace rg::map::art
