// Regnum — разбор исходной карты в объекты (см. art_extract.h): береговая линия, внутренние воды, знаки, стены и
// порядок наложения знаков. Поиск штампов — art_extract_symbols.cpp, отладка и разработка — art_extract_dev.cpp.
#include "mapbake/art_extract.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "mapbake/art_extract_internal.h"

namespace rg::map::art {

using namespace detail;

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

// ---------------------------------------------------------------- этапы разбора
// 2. Береговая линия и мелкие островки; профиль берега.
void traceLand(const codec::RgbaImage& flat, const bake::Layers& L, const bake::Options& bo, MapArt& art, ExtractReport& rep) {
  const int w = flat.w, h = flat.h;
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
}

// 3. Реки и озёра.
void traceWaters(const bake::Layers& L, MapArt& art, ExtractReport& rep) {
  const int w = L.w, h = L.h;
  for (const Ring& r : traceIso(L.inland.data(), w, h, 0.6)) {
    art.water.push_back(simplifyRing(r, 0.35));
    rep.waterPoints += i64(art.water.back().size());
  }
  rep.waterRings = int(art.water.size());
}

// 6. Остаток краски: крупные горы и стены.
void placeRest(SymbolSearch& ctx, const bake::Layers& L, MapArt& art, ExtractReport& rep) {
  const codec::RgbaImage& flat = ctx.flat;
  const int w = ctx.w, h = ctx.h;
  const StampFinder& F = ctx.F;
  std::vector<u8>& explained = ctx.explained;
  auto isInk = [&](size_t i) { return ctx.isInk(i); };
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
    // Бледные края найденного замка или башни — часть знака, а не стена.
    if (!ctx.stampRemnant(comp, bx0, by0, bx1, by1))
      for (Line& l : wallLines(flat, comp, bx0, by0, bw, bh)) art.lines.push_back(std::move(l));
    for (u32 i : comp) explained[i] = 1;
    rep.walls = int(art.lines.size());
  }
}

// Концы стен у башен и замков уходят под знак (на исходнике стена продолжается под штампом башни).
void tuckWallEnds(MapArt& art) {
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
}

// Пиксели краски и не объяснённые найденными объектами.
void countInk(const SymbolSearch& ctx, ExtractReport& rep) {
  const codec::RgbaImage& flat = ctx.flat;
  const std::vector<u8>& explained = ctx.explained;
  i64 inkPx = 0, unexplained = 0;
  for (size_t i = 0; i < explained.size(); i++) {
    if (255 - int(flat.rgba[i * 4 + 2]) < 32 || int(flat.rgba[i * 4 + 2]) - int(flat.rgba[i * 4]) > 40) continue;   // не краска или вода
    inkPx++;
    unexplained += !explained[i];
  }
  rep.inkPixels = inkPx;
  rep.unexplainedInk = unexplained;
}

// 7. Порядок наложения знаков.
void orderSymbols(const SymbolSearch& ctx, MapArt& art, ExtractReport& rep) {
  using Placed = SymbolSearch::Placed;
  const StampFinder& F = ctx.F;
  const std::vector<Placed>& placed = ctx.placed;
  const size_t n = art.symbols.size();
  std::unordered_map<u64, std::vector<u32>> cells;
  auto key = [](int cx, int cy) { return (u64(u32(cy)) << 32) | u32(cx); };
  // Крупные горы из остатка краски (этап 6) штампа не имеют: их порядок задаёт только основание.
  for (u32 i = 0; i < u32(std::min(n, placed.size())); i++) {
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
  MapArt art;
  art.width = flat.w;
  art.height = flat.h;

  // 1. Слои исходника (те же, что у прежней базовой карты): море, внутренние воды, знаки, поле берега.
  const bake::Options bo;
  bake::SegmentStats ss;
  const bake::Layers L = bake::segment(flat, bo, &ss);

  // 2. Береговая линия — тем же способом и с теми же параметрами, что и береговая линия миров (geo::Coast).
  traceLand(flat, L, bo, art, rep);

  // 3. Внутренние воды — реки и озёра: изолинии слоя inland (под знаками он продолжен от соседей).
  traceWaters(L, art, rep);

  // 4. Знаки: штампы башен, замков и гор (поиск по ключевым пикселям, подтверждение по всему штампу).
  SymbolSearch ctx(flat);
  rep.mountainForms = int(ctx.mountains.size());
  rep.mountainVariants = ctx.mountainVariants;
  if (!debugDir.empty()) writeMountainSheet(debugDir, ctx.mountains, ctx.mountainVariants);
  placeStamps(ctx, art, rep);

  // 5. Второй проход: штампы со сдвигом на долю пикселя, другого размера и частично перекрытые — в необъяснённых
  //    областях краски. Из подходящих вариантов берётся объясняющий больше краски (крупная гора, а не три мелких).
  //    Где ни один не подошёл — бледный или сглаженный замок или башня (корреляция формы, масштаб по габариту).
  placeShiftedStamps(ctx, art, rep);

  // 6. Остаток: крупные горы — знак «вершина» (масштаб по высоте тела), вытянутые тёмные области — стены (кроме
  //    бледных краёв найденных замков и башен).
  placeRest(ctx, L, art, rep);
  tuckWallEnds(art);
  countInk(ctx, rep);
  if (!debugDir.empty()) writeExplainedImages(debugDir, flat, ctx.explained, art);

  // 7. Порядок наложения знаков — как на исходнике: в общей части двух перекрывающихся штампов видно тот, что сверху.
  orderSymbols(ctx, art, rep);

  rep.seconds = nowSeconds() - t0;
  if (report) *report = rep;
  return art;
}

}  // namespace rg::map::art
