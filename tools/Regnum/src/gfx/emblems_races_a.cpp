// Regnum — эмблемы рас справочника: «Ангелы» … «Кентавры» (раздел «Расы», группа на расу, по алфавиту; имена race-<раса>-…).
// Сетка 100 × 100, стиль «гравированная геральдика»: одноцветный силуэт из сужающихся каллиграфических форм с острыми
// концами, детали — тонкие вырезы (внутренний контур, прожилки, насечки). Симметричные силуэты описаны левой половиной.
// Сюжеты отличаются от тематических групп с теми же названиями («Гномы», «Демоны», «Вампиры»).
#include <algorithm>
#include <tuple>

#include "gfx/emblems_internal.h"

namespace rg::gfx::emb {

namespace {

// ---------------------------------------------------------------- помощники
inline Pt P(double x, double y) { return {float(x), float(y)}; }
inline Pt lerpP(Pt a, Pt b, double t) { return P(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
inline std::string n2(double x, double y) { return n(x) + " " + n(y); }
inline Pt bzP(Pt p0, Pt c1, Pt c2, Pt p1, double t) {
  const CurvePt q = bezier(p0, c1, c2, p1, t);
  return P(q.x, q.y);
}

// Ломаная по точкам.
std::string polyPath(const std::vector<Pt>& p, bool closed = true) {
  std::string s;
  for (size_t i = 0; i < p.size(); i++) s += (i ? "L" : "M") + ptS(p[i]);
  return closed ? s + "Z" : s;
}

// Полоса переменной ширины w(t) вдоль кривой f(t), t ∈ [0, 1].
template <class F, class W>
std::string ribbon(F f, W w, int steps = 40) {
  std::vector<Pt> lft, rgt;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, e = 1e-3;
    const Pt p = f(t), q = f(std::min(1.0, t + e)), o = f(std::max(0.0, t - e));
    const double dx = q.x - o.x, dy = q.y - o.y, len = std::max(1e-9, std::hypot(dx, dy));
    const double nx = -dy / len, ny = dx / len, h = w(t) * 0.5;
    lft.push_back(P(p.x + nx * h, p.y + ny * h));
    rgt.push_back(P(p.x - nx * h, p.y - ny * h));
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Профиль «лепесток»: 0 на концах, 1 в доле peak; k0, k1 — острота начала и конца (больше — тоньше остриё).
inline double lobe(double t, double peak, double k0 = 0.7, double k1 = 1.0) {
  t = std::clamp(t, 0.0, 1.0);
  if (t <= peak) return std::pow(std::sin(kPi / 2 * t / peak), k0);
  return std::pow(std::max(0.0, std::cos(kPi / 2 * (t - peak) / (1 - peak))), k1);
}

// Каллиграфическое лезвие вдоль кубической кривой: острые концы, наибольшая ширина w в доле peak длины.
std::string blade(Pt p0, Pt c1, Pt c2, Pt p1, double w, double peak = 0.4, double k0 = 0.7, double k1 = 1.0, int steps = 40) {
  return ribbon([&](double t) { return bzP(p0, c1, c2, p1, t); }, [&](double t) { return w * lobe(t, peak, k0, k1); }, steps);
}

// Рог, коготь, хвост: от ширины w0 к острию по закону (1 − t)^k.
std::string spike(Pt p0, Pt c1, Pt c2, Pt p1, double w0, double k = 1.0, int steps = 40) {
  return ribbon([&](double t) { return bzP(p0, c1, c2, p1, t); }, [&](double t) { return w0 * std::pow(1 - t, k); }, steps);
}

// Лист или перо: от основания b к острию t, ширина w, изгиб оси bend (вбок от хода, доли длины не нужны — единицы сетки).
std::string leaf(Pt b, Pt t, double w, double bend = 0, double peak = 0.42, double k1 = 1.0) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::max(1e-6, std::hypot(dx, dy)), nx = -dy / len, ny = dx / len;
  const Pt c1 = P(b.x + dx / 3 + nx * bend, b.y + dy / 3 + ny * bend), c2 = P(b.x + dx * 2 / 3 + nx * bend, b.y + dy * 2 / 3 + ny * bend);
  return blade(b, c1, c2, t, w, peak, 0.6, k1);
}
// Средняя жилка листа leaf(b, t, …, bend) — от доли a0 до a1 оси.
std::string leafRib(Pt b, Pt t, double bend = 0, double a0 = 0.12, double a1 = 0.8) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::max(1e-6, std::hypot(dx, dy)), nx = -dy / len, ny = dx / len;
  const Pt c1 = P(b.x + dx / 3 + nx * bend, b.y + dy / 3 + ny * bend), c2 = P(b.x + dx * 2 / 3 + nx * bend, b.y + dy * 2 / 3 + ny * bend);
  std::string s;
  for (int i = 0; i <= 12; i++) s += (i ? "L" : "M") + ptS(bzP(b, c1, c2, t, a0 + (a1 - a0) * i / 12.0));
  return s;
}

// Открытая ломаная вдоль кубической кривой (для вырезов-линий).
std::string curve(Pt p0, Pt c1, Pt c2, Pt p1, double t0 = 0, double t1 = 1, int steps = 16) {
  std::string s;
  for (int i = 0; i <= steps; i++) s += (i ? "L" : "M") + ptS(bzP(p0, c1, c2, p1, t0 + (t1 - t0) * i / steps));
  return s;
}

// Замкнутые ломаные контура d (кривые спрямлены, повторы точек убраны).
std::vector<std::vector<Pt>> rings(std::string_view d, float tol = 0.12f) {
  std::vector<std::vector<Pt>> out;
  svgPath(d).flatten(tol, [&](const std::vector<Pt>& p, bool) {
    std::vector<Pt> q;
    for (Pt a : p)
      if (q.empty() || std::hypot(a.x - q.back().x, a.y - q.back().y) > 1e-3) q.push_back(a);
    while (q.size() > 2 && std::hypot(q.front().x - q.back().x, q.front().y - q.back().y) < 1e-3) q.pop_back();
    if (q.size() >= 3) out.push_back(std::move(q));
  });
  return out;
}

// Внутренний контур: каждый замкнутый подпуть d, смещённый внутрь на dist (для гравированной двойной линии края).
std::string inset(std::string_view d, double dist) {
  std::string s;
  for (const std::vector<Pt>& r : rings(d)) {
    const size_t m = r.size();
    double area = 0;
    for (size_t i = 0; i < m; i++) area += double(r[i].x) * r[(i + 1) % m].y - double(r[(i + 1) % m].x) * r[i].y;
    const double sg = area > 0 ? 1 : -1;
    std::vector<Pt> o;
    for (size_t i = 0; i < m; i++) {
      const Pt a = r[(i + m - 1) % m], b = r[i], c = r[(i + 1) % m];
      double ax = b.x - a.x, ay = b.y - a.y, bx = c.x - b.x, by = c.y - b.y;
      const double la = std::max(1e-9, std::hypot(ax, ay)), lb = std::max(1e-9, std::hypot(bx, by));
      const double n1x = -ay / la * sg, n1y = ax / la * sg, n2x = -by / lb * sg, n2y = bx / lb * sg;
      double mx = n1x + n2x, my = n1y + n2y, ml = std::hypot(mx, my);
      if (ml < 1e-6) mx = n1x, my = n1y, ml = 1;
      mx /= ml, my /= ml;
      const double k = dist / std::max(0.35, mx * n1x + my * n1y);
      o.push_back(P(b.x + mx * k, b.y + my * k));
    }
    // Петли на скруглениях радиусом меньше dist: рёбра, развернувшиеся против исходных, стягиваются в точку.
    std::vector<Pt> src = r;
    for (int pass = 0; pass < 8 && o.size() > 3; pass++) {
      bool changed = false;
      for (size_t i = 0; i < o.size() && o.size() > 3; i++) {
        const size_t j = (i + 1) % o.size();
        const double dx = o[j].x - o[i].x, dy = o[j].y - o[i].y, sx = src[j].x - src[i].x, sy = src[j].y - src[i].y;
        if (dx * sx + dy * sy >= 0) continue;
        o[i] = lerpP(o[i], o[j], 0.5);
        src[i] = lerpP(src[i], src[j], 0.5);
        o.erase(o.begin() + long(j));
        src.erase(src.begin() + long(j));
        changed = true;
      }
      if (!changed) break;
    }
    // Точки, оказавшиеся ближе 0,65·dist к исходному контуру (острия, петли), выпадают: линия рвётся, а не режет край.
    auto distTo = [&](Pt p) {
      double best = 1e9;
      for (size_t i = 0; i < m; i++) {
        const Pt a = r[i], b = r[(i + 1) % m];
        const double ex = b.x - a.x, ey = b.y - a.y, l2 = std::max(1e-12, ex * ex + ey * ey);
        const double t = std::clamp(((p.x - a.x) * ex + (p.y - a.y) * ey) / l2, 0.0, 1.0);
        best = std::min(best, std::hypot(p.x - a.x - ex * t, p.y - a.y - ey * t));
      }
      return best;
    };
    std::vector<bool> ok;
    bool all = true;
    for (const Pt& p : o) {
      ok.push_back(distTo(p) >= dist * 0.65);
      all = all && ok.back();
    }
    if (all) {
      s += polyPath(o);
      continue;
    }
    size_t first = 0;
    while (first < o.size() && ok[first]) first++;
    std::string run;
    int count = 0;
    for (size_t k = 1; k <= o.size(); k++) {
      const size_t i = (first + k) % o.size();
      if (ok[i]) {
        run += (count ? "L" : "M") + ptS(o[i]);
        count++;
      } else {
        if (count > 1) s += run;
        run.clear();
        count = 0;
      }
    }
    if (count > 1) s += run;
  }
  return s;
}

// Цельный симметричный контур из левой половины half (замкнутой по оси x = 50): шов на оси убирается, правая половина —
// отражение. Нужен, когда по контуру режется внутренняя линия (у двух половин она прошла бы и по оси).
std::string symFull(std::string_view half) {
  std::string s;
  for (const std::vector<Pt>& r : rings(half)) {
    const size_t m = r.size();
    size_t start = 0;
    for (size_t i = 0; i < m; i++)
      if (r[i].x < 49.9f && r[(i + m - 1) % m].x >= 49.9f) start = i;
    std::vector<Pt> left;
    for (size_t k = 0; k < m; k++) {
      const Pt p = r[(start + k) % m];
      if (p.x >= 49.9f) break;
      left.push_back(p);
    }
    std::vector<Pt> full = left;
    for (size_t k = left.size(); k-- > 0;) full.push_back(P(100 - left[k].x, left[k].y));
    s += polyPath(full);
  }
  return s;
}

// Штриховка внутри контура d: параллельные линии под углом deg с шагом step, отступ margin от края.
std::string hatchIn(std::string_view d, double deg, double step, double margin = 2.2, double phase = 0.5) {
  const auto rs = rings(d);
  const double ux = std::cos(deg * kPi / 180), uy = std::sin(deg * kPi / 180), vx = -uy, vy = ux;
  double lo = 1e9, hi = -1e9;
  for (const auto& r : rs)
    for (Pt p : r) lo = std::min(lo, p.x * vx + p.y * vy), hi = std::max(hi, p.x * vx + p.y * vy);
  std::string s;
  for (double c = lo + step * phase; c < hi; c += step) {
    std::vector<std::pair<double, double>> hits;  // положение вдоль линии и отступ с учётом угла пересечения
    for (const auto& r : rs)
      for (size_t i = 0; i < r.size(); i++) {
        const Pt a = r[i], b = r[(i + 1) % r.size()];
        const double da = a.x * vx + a.y * vy - c, db = b.x * vx + b.y * vy - c;
        if ((da < 0) == (db < 0)) continue;
        const double t = da / (da - db), x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t;
        const double ex = b.x - a.x, ey = b.y - a.y, el = std::max(1e-9, std::hypot(ex, ey));
        const double sn = std::fabs((ex * vx + ey * vy) / el);
        hits.push_back({x * ux + y * uy, margin / std::max(0.3, sn)});
      }
    std::sort(hits.begin(), hits.end());
    for (size_t i = 0; i + 1 < hits.size(); i += 2) {
      const double s0 = hits[i].first + hits[i].second, s1 = hits[i + 1].first - hits[i + 1].second;
      if (s1 - s0 < 1.2) continue;
      s += "M" + n2(ux * s0 + vx * c, uy * s0 + vy * c) + "L" + n2(ux * s1 + vx * c, uy * s1 + vy * c);
    }
  }
  return s;
}

// Часть поверх нарисованного: вокруг неё — зазор видимой ширины gap.
void over(std::vector<L>& v, const std::string& d, double gap = 1.5) {
  v.push_back(G::XS(d, float(gap * 2)));
  v.push_back(G::F(d));
}

// Габариты видимых слоёв (заливки и обводки) после преобразований.
RectF inkBounds(const std::vector<L>& v) {
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (const L& l : v) {
    if (l.op != L::Layer || l.kind == VecLayer::Erase || l.kind == VecLayer::EraseStroke) continue;
    Path p = svgPath(l.d);
    p.transform(l.xf);
    const float w = l.kind == VecLayer::Stroke ? (l.width > 0 ? l.width : 1.75f) * 0.5f : 0.f;
    p.flatten(0.2f, [&](const std::vector<Pt>& q, bool) {
      for (Pt a : q) x0 = std::min(x0, a.x - w), y0 = std::min(y0, a.y - w), x1 = std::max(x1, a.x + w), y1 = std::max(y1, a.y + w);
    });
  }
  return {x0, y0, x1 - x0, y1 - y0};
}

// Вписать эмблему в квадрат: большая сторона габаритов — size, центр — (50, 50) (единый оптический размер группы).
std::vector<L> fit(std::vector<L> v, double size = 94) {
  const RectF b = inkBounds(v);
  if (!(b.w > 0 && b.h > 0)) return v;
  const float s = float(size / std::max(b.w, b.h));
  return moved(std::move(v), Affine::translate(50, 50) * Affine::scale(s) * Affine::translate(-(b.x + b.w / 2), -(b.y + b.h / 2)));
}

// Геральдическое крыло (левое; пара — отражением): передняя кромка root→tip (кубическая e1, e2), концы маховых — по
// задней кромке tip→end (r1, r2). Три яруса внахлёст с зазорами: n длинных маховых-лезвий (ширина — fk шагов между
// концами), ряд коротких кроющих перьев на доле cover их длины и сужающаяся полоса вдоль передней кромки.
struct Wing {
  Pt root, e1, e2, tip, r1, r2, end;
  int n = 8;
  double cover = 0.4;  // доля длины маховых под кроющими (у корня; к концу крыла — меньше)
  double fk = 1.6;     // ширина пера в шагах между концами
  double band = 5;     // ширина полосы вдоль передней кромки у корня
  double slim = 0.3;   // наибольшая ширина махового в долях его длины
};
Wing xfWing(Wing w, const Affine& m) {
  for (Pt* p : {&w.root, &w.e1, &w.e2, &w.tip, &w.r1, &w.r2, &w.end}) *p = m.apply(*p);
  return w;
}
void addWing(std::vector<L>& v, const Wing& w, bool mirror, double gap = 1.2) {
  auto mm = [&](const std::string& d) { return mirror ? sym(d) : d; };
  std::vector<Pt> edges, tips;
  for (int i = 0; i < w.n; i++) {
    const double t = double(i) / (w.n - 1);
    tips.push_back(bzP(w.tip, w.r1, w.r2, w.end, t));
    edges.push_back(bzP(w.root, w.e1, w.e2, w.tip, 0.86 - 0.8 * t));
  }
  auto spacing = [&](int i) {
    const Pt a = tips[std::max(0, i - 1)], b = tips[std::min(w.n - 1, i + 1)];
    return std::hypot(a.x - b.x, a.y - b.y) / ((i == 0 || i == w.n - 1) ? 1 : 2);
  };
  // Маховые: от корня к концу крыла, каждое поверх предыдущего с зазором.
  for (int i = w.n - 1; i >= 0; i--) {
    const Pt b = edges[i], t = tips[i];
    const double len = std::hypot(t.x - b.x, t.y - b.y);
    const std::string f = leaf(b, t, std::min(spacing(i) * w.fk, len * w.slim), -0.035 * len, 0.62, 1.3);
    v.push_back(G::XS(mm(f), float(gap * 2)));
    v.push_back(G::F(mm(f)));
  }
  // Кроющие: короткие перья поверх оснований маховых.
  for (int i = w.n - 1; i >= 0; i--) {
    const double t = double(i) / (w.n - 1), k = w.cover * (0.5 + 0.5 * t);
    const Pt b = edges[i], tp = lerpP(edges[i], tips[i], k);
    const double len = std::hypot(tp.x - b.x, tp.y - b.y);
    const std::string f = leaf(b, tp, std::min(spacing(i) * w.fk * 1.1, len * 0.62), 0, 0.55, 1.2);
    v.push_back(G::XS(mm(f), float(gap * 2)));
    v.push_back(G::F(mm(f)));
  }
  // Полоса вдоль передней кромки (от корня к концу крыла сужается).
  const std::string band = ribbon([&](double t) { return bzP(w.root, w.e1, w.e2, w.tip, t * 0.9); },
                                  [&](double t) { return w.band * (1 - 0.8 * t) * std::min(1.0, 0.35 + 6 * t); }, 40);
  v.push_back(G::XS(mm(band), float(gap * 2)));
  v.push_back(G::F(mm(band)));
}

// Неровная окружность (годичные кольца): r(a) = r + amp·sin(k·a + ph) + amp/2·sin((2k+1)·a).
std::string wobble(double cx, double cy, double r, double amp, int k, double ph, int steps = 90) {
  std::string s;
  for (int i = 0; i < steps; i++) {
    const double a = 360.0 * i / steps, t = a * kPi / 180;
    const double rr = r + amp * std::sin(k * t + ph) + amp * 0.5 * std::sin((2 * k + 1) * t);
    s += (i ? "L" : "M") + pt(cx, cy, rr, a);
  }
  return s + "Z";
}

// ---------------------------------------------------------------- ангелы
void angels(Catalog& c) {
  c.group("Ангелы", EmblemSection::Races);
  // Ангел-воитель: высоко поднятые крылья, нимб, меч остриём вниз в сложенных на рукояти руках.
  {
    std::vector<L> v;
    addWing(v, {{44, 34}, {36, 20}, {21, 6}, {3, 2}, {5, 32}, {13, 60}, {32, 86}, 12, 0.42, 1.5, 5}, true);
    const std::string robe = sym("M50 24C46 24 43 25.5 41 28C38.5 31 37.5 35 36.5 41C35 54 31 74 25 97H50.5V24Z");
    over(v, robe, 1.6);
    v.push_back(G::XS(sym(curve({45, 54}, {44, 67}, {41.5, 82}, {38.5, 97})) + sym(curve({40.5, 56}, {38.5, 69}, {35, 84}, {31, 97})), 1.4f));
    v.push_back(G::XS(sym(curve({41.5, 28.5}, {44, 33}, {47, 36.5}, {50, 38})) + "M38.5 47Q50 50.5 61.5 47", 1.4f));
    over(v, ell(50, 14.5, 5.4, 6.6), 1.6);
    v.push_back(G::S(circ(50, 14.5, 10), 1.8f));
    const std::string sword = "M50 31L52.6 34.6 50 38.2 47.4 34.6Z" + rrect(48.5, 37.4, 3, 10, 0.8) +
                              blade({37, 48.4}, {43, 49.6}, {57, 49.6}, {63, 48.4}, 3.4, 0.5, 0.4, 0.8) + "M46 50H54L53.6 84 50 97.5 46.4 84Z";
    over(v, sword, 1.5);
    v.push_back(G::XS("M50 54V88", 1.3f));
    c.add("race-angels-warrior", "Ангел-воитель", fit(v));
  }
  // Серафим: лик в нимбе, три пары крыльев — верхняя поднята, средняя раскинута, нижняя опущена.
  {
    std::vector<L> v;
    auto wingAt = [&](double x, double y, double deg, double len, double wid, double flip) {
      const Wing loc{P(0, 0), P(len * 0.25, -wid * 0.75 * flip), P(len * 0.62, -wid * 0.7 * flip), P(len, -wid * 0.25 * flip),
                     P(len * 0.96, wid * 0.3 * flip), P(len * 0.78, wid * 0.62 * flip), P(len * 0.42, wid * 0.62 * flip), 6, 0.45, 2.3, 5.6, 0.46};
      addWing(v, xfWing(loc, place(float(x), float(y), float(deg))), true, 1.1);
    };
    wingAt(45, 41, -108, 48, 21, -1);
    wingAt(41, 50, 184, 46, 20, -1);
    wingAt(45, 59, 108, 48, 21, 1);
    over(v, circ(50, 50, 15.5), 1.4);
    v.push_back(G::XS(circ(50, 50, 12.6), 1.4f));
    const std::string face = "M50 40.5C55.2 40.5 58.2 44.5 58.2 49.5C58.2 55 54.6 59.2 50 61C45.4 59.2 41.8 55 41.8 49.5C41.8 44.5 44.8 40.5 50 40.5Z";
    v.push_back(G::X(face));
    v.push_back(G::F(inset(face, 1.3)));
    v.push_back(G::XS(sym(curve({43.8, 48.6}, {45.3, 47.2}, {47.1, 47.2}, {48.6, 48.4})) + "M50 49.6V54.4", 1.3f));
    c.add("race-angels-seraph", "Серафим", fit(v));
  }
  // Пылающий меч: клинок остриём вверх, по обеим кромкам — языки пламени, крестовина-крылья, рукоять с обмоткой.
  {
    std::vector<L> v;
    for (int k = 3; k >= 0; k--) {
      const double s = 1.2 - 0.22 * k, y = 66 - 13.5 * k, x = 47;
      const std::string f = k % 2 ? blade(P(x, y), P(x - 14 * s, y - 2 * s), P(x - 4 * s, y - 16 * s), P(x - 19 * s, y - 31 * s), 10 * s, 0.24, 0.6, 1.8)
                                  : blade(P(x, y), P(x - 19 * s, y - 3 * s), P(x - 21 * s, y - 20 * s), P(x - 7 * s, y - 31 * s), 11 * s, 0.3, 0.6, 1.8);
      over(v, sym(f), 1.1);
    }
    over(v, "M45.8 66V16L50 3 54.2 16V66Z", 1.6);
    v.push_back(G::XS("M50 14V60", 1.3f));
    std::string ribs;
    for (auto [tx, ty, w] : {std::tuple{23.0, 77.0, 6.0}, std::tuple{16.0, 70.0, 7.0}, std::tuple{12.0, 62.0, 7.5}}) {
      over(v, sym(leaf({45, 69}, P(tx, ty), w, -2, 0.6, 1.2)), 1.1);
      ribs += leafRib({45, 69}, P(tx, ty), -2, 0.35, 0.8);
    }
    v.push_back(G::XS(sym(ribs), 1.2f));
    const std::string hilt = "M40 66H60L56.5 71H43.5Z" + rrect(46.6, 70.5, 6.8, 15, 1.2) + "M50 84.5 55 90.5 50 97.5 45 90.5Z";
    over(v, hilt, 1.4);
    v.push_back(G::XS("M46.6 74.5 53.4 77M46.6 78.5 53.4 81", 1.2f));
    v.push_back(G::X("M50 87.5 52.2 90.5 50 93.5 47.8 90.5Z"));
    c.add("race-angels-flamesword", "Пылающий меч", fit(v));
  }
  // Труба вестника: длинная труба наискось, раздвоенное знамя со звездой, лучи гласа у раструба.
  {
    std::vector<L> v;
    const Affine m = place(8, 66, -24);
    auto bellW = [](double x) { return x < 62 ? 3.4 : 3.4 + 15 * std::pow((x - 62) / 30, 2.2); };
    std::string tube = ribbon([](double t) { return P(4 + 88 * t, 0); }, [&](double t) { return 2 * bellW(4 + 88 * t); }, 60);
    tube += "M0 -6C2.6 -6 4 -3.8 6 -3V3C4 3.8 2.6 6 0 6Z" + std::string("M16 0 21 -5.8 26 0 21 5.8ZM44 0 49 -6.4 54 0 49 6.4Z") + "M91 -18.6H95V18.6H91Z";
    // Знамя свисает с трубы между x = 22 и 50.
    const Pt a = m.apply({20, 2.5f}), b = m.apply({52, 2.5f});
    const double drop = 58, mx = (a.x + b.x) / 2;
    const std::string banner = "M" + ptS(a) + "C" + n2(a.x - 2.5, a.y + drop * 0.4) + " " + n2(a.x + 2.5, a.y + drop * 0.75) + " " +
                               n2(a.x, b.y + drop) + "L" + n2(mx, b.y + drop - 13) + "L" + n2(b.x, b.y + drop) + "C" + n2(b.x + 2.5, b.y + drop * 0.7) +
                               " " + n2(b.x - 2.5, b.y + drop * 0.3) + " " + ptS(b) + "Z";
    v.push_back(G::F(banner));
    v.push_back(G::XS(inset(banner, 3.2), 1.4f));
    v.push_back(G::X(star(mx, b.y + 21, 10.5, 4.2, 5, -90)));
    over(v, xf(tube, m), 1.6);
    v.push_back(G::XS(xf("M64 0H86", m), 1.3f));
    std::string rays;
    for (int i = -2; i <= 2; i++) {
      const double ang = i * 22.0, r0 = 5, len = i % 2 ? 11 : 17;
      rays += blade(polar(96, 0, r0, ang), polar(96, 0, r0 + len * 0.33, ang), polar(96, 0, r0 + len * 0.66, ang), polar(96, 0, r0 + len, ang),
                    i % 2 ? 3.4 : 4.6, 0.25, 0.5, 1.0, 12);
    }
    v.push_back(G::F(xf(rays, m)));
    c.add("race-angels-trumpet", "Труба вестника", fit(v));
  }
}

// Членистая нога паука: бедро от тазика a к колену k (с угловатым выступом сустава), голень от колена к острию f.
std::string spiderLeg(Pt a, Pt k, Pt f, double w0, double bow = 0.12) {
  auto bowed = [&](Pt p, Pt q, double b) {
    const double dx = q.x - p.x, dy = q.y - p.y;
    return std::pair{P(p.x + dx / 3 - dy * b, p.y + dy / 3 + dx * b), P(p.x + dx * 2 / 3 - dy * b, p.y + dy * 2 / 3 + dx * b)};
  };
  const double dl = std::max(1e-6, double(std::hypot(k.x - a.x, k.y - a.y)));
  const Pt ke = P(k.x + (k.x - a.x) / dl * w0 * 0.55, k.y + (k.y - a.y) / dl * w0 * 0.55);
  const auto [a1, a2] = bowed(a, ke, -bow);
  const auto [b1, b2] = bowed(k, f, bow * 0.6);
  return taper(a, a1, a2, ke, w0, w0 * 0.62) + spike(k, b1, b2, f, w0 * 0.72, 0.9);
}

// ---------------------------------------------------------------- арахниды
void arachnids(Catalog& c) {
  c.group("Арахниды", EmblemSection::Races);
  // Паук-воин: человеческий торс на паучьем теле, серпы в поднятых руках, восемь членистых ног.
  {
    std::vector<L> v;
    std::string legs;
    for (auto [a, k, f] : {std::tuple{P(44, 57), P(23, 37), P(4, 52)}, std::tuple{P(43, 61), P(16, 49), P(3, 74)},
                           std::tuple{P(43, 65), P(21, 63), P(9, 91)}, std::tuple{P(45, 69), P(32, 75), P(25, 98)}})
      legs += spiderLeg(a, k, f, 4.4);
    v.push_back(G::F(sym(legs)));
    const std::string abdomen = "M50 60C58 60 63.5 66 63.5 74C63.5 84 57 92 50 97C43 92 36.5 84 36.5 74C36.5 66 42 60 50 60Z";
    over(v, abdomen, 1.4);
    v.push_back(G::X("M50 69 53.5 75 50 78.5 46.5 75ZM50 80.5 52.6 85 50 89.5 47.4 85Z"));
    v.push_back(G::XS(inset(abdomen, 2.4), 1.2f));
    // Торс, руки с серпами, голова в шлеме с гребнем.
    const std::string torso = symFull("M50 59C47.5 59 46.5 57 46 54L45 47.5C43 44.5 40.5 40.5 39.8 36.5C39.5 33 42 31 46 30.5H50.5V59Z");
    const std::string arms = sym(taper({42, 34}, {37, 33}, {33.5, 30}, {31, 25}, 5, 4) + taper({31.5, 26}, {30, 22}, {29.5, 19}, {30, 15}, 4.2, 3.4));
    const std::string sickles = sym(spike({30, 14}, {29, 6}, {20, 1}, {11, 4}, 4.6, 0.8) + spike({30, 14}, {30, 10}, {28, 7}, {25.5, 6}, 3, 1) +
                                    taper({30, 19}, {30, 17}, {30, 15.5}, {30, 14}, 3.6, 3.6));
    over(v, torso + arms, 1.4);
    over(v, sickles, 1.2);
    v.push_back(G::XS(sym(curve({30.5, 13.5}, {27, 7}, {20, 4.4}, {13.5, 5.2}, 0.1, 0.85)), 1.2f));
    v.push_back(G::XS(sym(curve({42.5, 37.5}, {44.5, 41}, {47.5, 41.5}, {50, 40.5})) + "M50 41V51M46 55H54", 1.3f));
    const std::string head = "M50 13C53.5 13 55.5 16 55.5 20C55.5 24.5 53.5 28 50 29.5C46.5 28 44.5 24.5 44.5 20C44.5 16 46.5 13 50 13Z" +
                             spike({50, 15}, {49.5, 10}, {50, 6}, {50, 1}, 3.6, 1.2);
    over(v, head, 1.4);
    v.push_back(G::X(sym("M46.2 20.5 49 21.6 46.6 22.6Z")));
    c.add("race-arachnids-warrior", "Паук-воин", fit(v));
  }
  // Кокон: веретено, обмотанное спиралью шёлка, в середине паутины.
  {
    std::vector<L> v;
    std::string web;
    for (int k = 0; k < 8; k++) {
      const double a = -67.5 + 45 * k;
      web += taper(polar(50, 52, 10, a), polar(50, 52, 22, a), polar(50, 52, 34, a), polar(50, 52, 47, a), 3, 1.4);
      for (double r : {30.0, 42.0}) {
        const Pt p0 = polar(50, 52, r, a), p1 = polar(50, 52, r, a + 45), m = polar(50, 52, r * 0.8, a + 22.5);
        web += taper(p0, lerpP(p0, m, 0.7), lerpP(p1, m, 0.7), p1, 2.4, 2.4);
      }
    }
    v.push_back(G::F(web + taper({50, 2}, {50, 5}, {50, 8}, {50, 11}, 2.4, 2.4)));
    const std::string cocoon = "M50 9C59 16 64.5 30 65 47C65.5 64 59 80 50 95C41 80 34.5 64 35 47C35.5 30 41 16 50 9Z";
    over(v, cocoon, 2);
    std::string wraps;
    for (int i = 0; i < 8; i++) {
      const double y = 21 + 9 * i;
      wraps += curve(P(31, y), P(40, y + 6), P(56, y + 1), P(69, y - 9));
    }
    v.push_back(G::XS(wraps, 1.5f));
    c.add("race-arachnids-cocoon", "Кокон", fit(v));
  }
  // Паучьи жвала: щиток головы с восемью глазами, могучие хелицеры с серповидными клыками, педипальпы, дуги передних ног.
  {
    std::vector<L> v;
    v.push_back(G::F(sym(spiderLeg({34, 18}, {17, 3}, {3, 28}, 5) + spiderLeg({32, 28}, {11, 20}, {5, 52}, 4.6))));
    const std::string head = symFull("M50 6C44 6 38 7.5 34 11C30.5 14.5 29.5 20 30.5 26C31.5 32 35 36.5 41 38.5L50.5 40V6Z");
    over(v, head, 1.6);
    v.push_back(G::X(sym(xf(ell(0, 0, 2.6, 4), place(45.6, 28.5, 24)) + xf(ell(0, 0, 1.8, 2.7), place(46.4, 19.5, 14)) +
                         xf(ell(0, 0, 1.4, 2.2), place(39.8, 22.5, 34)) + xf(ell(0, 0, 1.3, 2), place(38.2, 29.5, 42)))));
    v.push_back(G::F(sym(circ(46.1, 27.2, 0.95))));
    const std::string palps = sym(taper({35, 40}, {26, 44}, {22, 54}, {23, 64}, 5.6, 4) + spike({23, 63}, {24, 73}, {27, 80}, {33, 86}, 4, 1));
    over(v, palps, 1.3);
    const std::string chel = "M50 37C45 37 40.5 39.5 39 44.5C37.5 50 38 57 40.5 63C42 66.5 44 69 46 70.5C48 67.5 49.5 63 50.5 58V37Z";
    const std::string fang = spike({43, 67}, {37, 74}, {38.5, 86}, {48.5, 95}, 7, 1.2);
    over(v, sym(chel + fang), 1.5);
    v.push_back(G::XS(sym(curve({44.5, 41.5}, {42.5, 48}, {42.5, 56}, {45, 63}) + curve({41.6, 69.5}, {40, 76}, {42, 84}, {47, 91}, 0, 0.8)), 1.2f));
    c.add("race-arachnids-fangs", "Паучьи жвала", fit(v));
  }
  // Паутина с глазами: восемь радиальных нитей, провисающие кольца, в центре — паук с горящими глазами.
  {
    std::vector<L> v;
    std::string radial, rings;
    for (int k = 0; k < 8; k++) {
      const double a = -90 + 45 * k;
      radial += taper(polar(50, 50, 14, a), polar(50, 50, 24, a), polar(50, 50, 36, a), polar(50, 50, 48.5, a), 3.4, 1.4);
      for (double r : {33.0, 43.0}) {
        const Pt p0 = polar(50, 50, r, a), p1 = polar(50, 50, r, a + 45), m = polar(50, 50, r * 0.8, a + 22.5);
        rings += taper(p0, lerpP(p0, m, 0.7), lerpP(p1, m, 0.7), p1, 2.4, 2.4);
      }
    }
    v.push_back(G::F(radial + rings));
    // Паук в центре: голова с глазами, брюшко с узором, восемь ног вдоль кольца.
    std::string slegs;
    for (auto [a, k, f] : {std::tuple{P(46.5, 40), P(36, 27), P(35, 12)}, std::tuple{P(45.5, 43), P(30, 36), P(19, 28)},
                           std::tuple{P(45.5, 46), P(30, 51), P(20, 61)}, std::tuple{P(46.5, 49), P(37, 61), P(35, 76)}})
      slegs += spiderLeg(a, k, f, 3.3, 0.08);
    over(v, sym(slegs), 1.2);
    const std::string sbody = ell(50, 44, 5.6, 6.2) + "M50 49.5C55 49.5 58.5 54 58.5 60C58.5 66 55 71 50 74C45 71 41.5 66 41.5 60C41.5 54 45 49.5 50 49.5Z";
    over(v, sbody, 1.4);
    v.push_back(G::X(sym(circ(47.6, 42.4, 1.15) + circ(45.8, 45, 0.9)) + "M50 55 52.6 60 50 64.5 47.4 60Z"));
    c.add("race-arachnids-web", "Паутина с глазами", fit(v));
  }
}

// ---------------------------------------------------------------- арборианы
void arborians(Catalog& c) {
  c.group("Арборианы", EmblemSection::Races);
  // Человек-дерево: ствол-тело с суровым ликом в коре, ветви-руки подняты и кончаются листьями, корни-ноги.
  {
    std::vector<L> v;
    const std::string arms = sym(taper({43.5, 50}, {35, 46}, {29, 36}, {25.5, 22}, 8, 4) + spike({25.6, 23}, {25, 16}, {23, 11}, {20, 6}, 4, 1.1) +
                                 spike({28, 33}, {22, 31}, {17, 26}, {12, 19}, 3.8, 1.1) + spike({32, 42}, {26, 43}, {20, 42}, {14, 38}, 3.4, 1.1) +
                                 spike({45.5, 26}, {44.5, 20}, {42.5, 15}, {40, 9}, 3.4, 1.1));
    v.push_back(G::F(arms));
    std::string ribs;
    for (auto [bx, by, tx, ty, bend] : {std::tuple{20.5, 7.0, 15.0, -0.5, 1.2}, std::tuple{12.5, 19.5, 3.0, 14.0, 1.2}, std::tuple{14.5, 38.0, 3.5, 36.5, 1.2},
                                        std::tuple{40.0, 9.5, 37.0, 0.5, 1.0}, std::tuple{23.5, 12.5, 29.0, 4.0, -1.0}, std::tuple{17.5, 27.0, 13.0, 21.0, 1.0}}) {
      over(v, sym(leaf(P(bx, by), P(tx, ty), 5.2, bend, 0.42, 1.3)), 0.9);
      ribs += leafRib(P(bx, by), P(tx, ty), bend, 0.25, 0.75);
    }
    v.push_back(G::XS(sym(ribs), 1.2f));
    const std::string body = symFull("M50 18C46.5 18 44 21 43.5 26L42.5 40C42 48 41.8 56 42.6 64C43.4 70 42.8 74 40.5 78L37 80H50.5V18Z") +
                             sym(spike({43, 74}, {39, 83}, {30, 90}, {14, 95}, 7, 1.1) + spike({46, 77}, {45, 86}, {42, 92}, {35, 98}, 5.6, 1.1)) +
                             spike({50, 79}, {50, 87}, {50.5, 92}, {50, 98.5}, 4.6, 1);
    over(v, body, 1.4);
    v.push_back(G::X(sym("M43 33.6 48.6 35.4 44.2 36.8Z")));
    v.push_back(G::XS(sym(curve({42.5, 30.5}, {44.5, 29.5}, {47, 30}, {49, 32})) + "M50 33V41M46 46.5H54" +
                          sym(curve({46.6, 53}, {45.6, 60}, {46.8, 66}, {45.5, 73})) + "M50 55V75",
                      1.3f));
    c.add("race-arborians-treeman", "Человек-дерево", fit(v));
  }
  // Лист-маска: лик из листа с зубчатым краем и черенком, раскосые прорези глаз, нос — средняя жилка, боковые жилки.
  {
    std::vector<L> v;
    auto halfW = [](double t) { return 36 * std::pow(std::sin(kPi * t), 0.62) * (1 - 0.32 * t); };
    std::vector<Pt> left;
    const int N = 150, teeth = 12;
    for (int i = 0; i <= N; i++) {
      const double t = double(i) / N, ph = std::fmod(t * teeth + 0.5, 1.0);
      const double tooth = (t > 0.05 && t < 0.93) ? 2.6 * ph * std::sin(kPi * t) : 0;
      left.push_back(P(50 - halfW(t) - tooth, 9 + 88 * t));
    }
    std::vector<Pt> full = left;
    for (size_t k = left.size(); k-- > 0;) full.push_back(P(100 - left[k].x, left[k].y));
    v.push_back(G::F(polyPath(full) + spike({50, 12}, {51, 7}, {53, 4}, {57, 1.5}, 3.4, 1.2)));
    std::string veins = "M50 14V86";
    for (double y : {17.0, 26.0, 51.0, 60.0, 69.0}) {
      const double t = (y - 9) / 88, x1 = 50 - halfW(std::min(0.97, t + 0.08)) + 5.5;
      veins += sym(curve(P(50, y), P(46, y + 1), P(x1 + 6, y + 3), P(x1, y + 8)));
    }
    v.push_back(G::XS(veins, 1.4f));
    v.push_back(G::X(sym("M24.5 37.5C31 36 39.5 37.5 46 42C39.5 46 30.5 45 24.5 37.5Z")));
    c.add("race-arborians-leafmask", "Лист-маска", fit(v));
  }
  // Кольца ствола: пень — на срезе годичные кольца и трещина к сердцевине, кора в бороздах, корни, росток из среза.
  {
    std::vector<L> v;
    const double cx = 50, cy = 36, rx = 37, ry = 14;
    const std::string body = symFull("M50 36H13C13.5 48 14.5 60 13 70C12 76 8 82 3 86L18 84C24 86 30 88 33 93L42 85C45 88 48 92 50 96H50.5V36Z");
    v.push_back(G::F(body));
    v.push_back(G::XS(sym(curve({20, 52}, {21, 60}, {20, 68}, {18, 77}) + curve({29, 54}, {30, 63}, {29.5, 72}, {28, 82})) + "M40 55V86M50 52V58M50 64V90",
                      1.4f));
    const Affine el = Affine::translate(float(cx), float(cy)) * Affine::scale(1, float(ry / rx));
    over(v, xf(circ(0, 0, rx), el), 1.6);
    std::string rings;
    for (double r : {30.0, 23.0, 16.0, 9.5}) rings += xf(wobble((rx - r) * 0.1, 0, r, 0.25 + r * 0.015, 5, r * 0.3, 72), el);
    v.push_back(G::XS(rings, 1.3f));
    v.push_back(G::X(xf(circ(3, 0, 3), el)));
    v.push_back(G::X(xf(ribbon([](double t) { return P(4 + 30 * t + 1.6 * std::sin(t * 16), 1.5 + 26 * t); }, [](double t) { return 0.6 + 4 * t; }, 30), el)));
    v.push_back(G::F(spike({66, 30}, {66.5, 22}, {68.5, 15}, {72, 9}, 3, 0.7)));
    over(v, leaf({68, 16}, {56, 6}, 7, 1.6, 0.45, 1.3) + leaf({70.5, 12}, {84, 3}, 7, -1.6, 0.45, 1.3), 1.0);
    v.push_back(G::XS(leafRib({68, 16}, {56, 6}, 1.6, 0.2, 0.75) + leafRib({70.5, 12}, {84, 3}, -1.6, 0.2, 0.75), 1.2f));
    c.add("race-arborians-rings", "Кольца ствола", fit(v));
  }
  // Корни-щит: щит с вырезанным деревом, корни дерева выходят из острия щита и расходятся вниз.
  {
    std::vector<L> v;
    const std::string roots = sym(spike({46, 80}, {37, 86}, {24, 88}, {5, 93}, 6, 1.1) + spike({47.5, 82}, {44, 89}, {39, 94}, {31, 99}, 5, 1.1) +
                                  spike({27, 88}, {22, 92}, {19, 95}, {16, 99}, 2.8, 1)) +
                              spike({50, 83}, {50, 89}, {50.5, 94}, {50, 99}, 4.6, 1);
    v.push_back(G::F(roots));
    const std::string shield = symFull("M50 5H19C17.8 5 17 5.8 17 7V40C17 60 30 74 50 84H50.5V5Z");
    over(v, shield, 1.5);
    v.push_back(G::XS(inset(shield, 3), 1.4f));
    // Дерево вырезом: ствол, три пары ветвей вверх, листья на концах и вдоль ветвей, корни к острию.
    std::string tree = taper({50, 78}, {50, 64}, {50, 50}, {50, 30}, 6, 3) +
                       sym(spike({49, 57}, {42, 55}, {35, 50}, {30, 41}, 3.4, 1) + spike({49, 46}, {43, 43}, {38.5, 37}, {36.5, 28}, 3, 1) +
                           spike({49.5, 36}, {46, 32}, {44.5, 27}, {44.5, 20}, 2.8, 1) + spike({48, 73}, {44, 76}, {40, 78}, {35, 78.5}, 3, 1));
    for (auto [bx, by, tx, ty, bend] : {std::tuple{30.5, 42.0, 26.0, 30.0, 1.0}, std::tuple{37.0, 29.0, 34.5, 17.5, 1.0}, std::tuple{44.5, 21.0, 43.0, 10.0, 0.8},
                                        std::tuple{36.0, 52.0, 26.0, 52.0, -1.0}, std::tuple{41.0, 41.0, 32.0, 38.0, -1.0}, std::tuple{45.5, 31.0, 38.0, 25.0, -1.0}})
      tree += sym(leaf(P(bx, by), P(tx, ty), 4.4, bend, 0.42, 1.3));
    tree += leaf({50, 31}, {50, 9}, 5.6, 0, 0.42, 1.3);
    v.push_back(G::X(tree));
    c.add("race-arborians-rootshield", "Корни-щит", fit(v));
  }
}

// ---------------------------------------------------------------- безликие
void faceless(Catalog& c) {
  c.group("Безликие", EmblemSection::Races);
  // Маска без лица: гладкая маска с лепкой бровей и носа, за ней — тёмный венец из острых лучей.
  {
    std::vector<L> v;
    std::string rays;
    for (int i = 0; i < 28; i++) {
      const double a = -90 + 360.0 / 28 * i, len = i % 2 ? 33 : (i % 4 == 0 ? 48 : 41);
      rays += blade(polar(50, 50, 14, a), polar(50, 50, 14 + (len - 14) * 0.33, a), polar(50, 50, 14 + (len - 14) * 0.66, a), polar(50, 50, len, a),
                    i % 2 ? 3.4 : 5, 0.15, 0.5, 1.0, 16);
    }
    v.push_back(G::F(rays));
    const std::string mask = "M50 16C62.5 16 71 26 71 41C71 57 62.5 72 50 83C37.5 72 29 57 29 41C29 26 37.5 16 50 16Z";
    over(v, mask, 2);
    v.push_back(G::XS(inset(mask, 2.8), 1.3f));
    v.push_back(G::XS(sym(curve({36, 41}, {39.5, 36.5}, {45, 35.5}, {48.8, 38.6})) + curve({50, 39.5}, {49.2, 46}, {48.4, 52}, {50.6, 57}),
                      1.4f));
    c.add("race-faceless-mask", "Маска без лица", fit(v));
  }
  // Капюшон с пустотой: пустой плащ парит, рукава-крылья с рваным краем, в остроконечном капюшоне — пустота со звездой.
  {
    std::vector<L> v;
    const std::string sleeves = sym("M39 33C30 32 18 28 4 19L6.5 29C8 37 7.5 46 4.5 55L10.5 50 12.5 60 17 52.5 21 61.5 24 53 29.5 59 31 50C33.5 46 36.5 43 39 42Z");
    v.push_back(G::F(sleeves));
    v.push_back(G::XS(sym(curve({33, 37}, {25, 36}, {16, 33}, {8, 28}) + curve({32, 44}, {26, 45}, {20, 47}, {14, 50})), 1.4f));
    const std::string robe = symFull("M50 35H38.5C36.5 50 33 67 25 93L31 87 33.5 98 39 89 44 99 47 91 50 98V35Z");
    over(v, robe, 1.5);
    v.push_back(G::XS(sym(curve({44.5, 50}, {43, 64}, {41, 78}, {38.5, 91}) + curve({39.5, 52}, {37.5, 64}, {35, 76}, {31.5, 88})) + "M50 52V92", 1.4f));
    const std::string hood = symFull("M50 2C45 8 37.5 14.5 35.5 25C34 33 35 41 38 48L44 50H50.5V2Z");
    over(v, hood, 1.5);
    v.push_back(G::X("M50 13C56 17.5 59.5 24.5 59.5 32C59.5 39.5 56 44.5 50 47C44 44.5 40.5 39.5 40.5 32C40.5 24.5 44 17.5 50 13Z"));
    v.push_back(G::F(star(50, 31, 7.5, 1.5, 4, -90)));
    c.add("race-faceless-hood", "Капюшон с пустотой", fit(v));
  }
  // Воронка пустоты: шесть серповидных рукавов с гравированной жилкой вокруг тёмного зрачка.
  {
    std::vector<L> v;
    std::string arms, veins;
    for (int k = 0; k < 6; k++) {
      const double a0 = 60.0 * k;
      arms += ribbon([&](double t) { return polar(50, 50, 9 + 39 * t, a0 + 150 * t); },
                     [](double t) { return 12 * std::pow(std::sin(kPi * std::pow(t, 0.6)), 0.9) * (1 - 0.3 * t); });
      std::string vn;
      for (int i = 0; i <= 14; i++) {
        const double t = 0.12 + 0.6 * i / 14;
        vn += (i ? "L" : "M") + ptS(polar(50, 50, 9 + 39 * t - 1.2, a0 + 150 * t - 2));
      }
      veins += vn;
    }
    v.push_back(G::F(arms));
    v.push_back(G::XS(veins, 1.3f));
    v.push_back(G::F(circ(50, 50, 10.5)));
    v.push_back(G::X(circ(50, 50, 7)));
    v.push_back(G::F(star(50, 50, 4.6, 1.2, 4, 45)));
    c.add("race-faceless-vortex", "Воронка пустоты", fit(v));
  }
  // Зеркало: овальная рама с навершием, листьями-завитками и подставкой; в тёмном стекле — бюст без лица.
  {
    std::vector<L> v;
    const std::string frame = ell(50, 50, 27, 36);
    const std::string crest = blade({50, 18}, {50, 12}, {50, 7}, {50, 1}, 7.5, 0.35, 0.6, 1.2) + sym(leaf({48.5, 15.5}, {38.5, 7}, 5.4, 1.4, 0.45, 1.3));
    const std::string scrolls = sym(leaf({24, 47}, {13, 38}, 5.5, 1.5, 0.45, 1.3) + leaf({24, 55}, {13, 64}, 5.5, -1.5, 0.45, 1.3));
    const std::string stand = symFull("M50 84L45.5 85.5C44 90 39 94 31 96.5V98.5H50.5V84Z");
    v.push_back(G::F(frame + crest + scrolls + stand));
    v.push_back(G::XS(ell(50, 50, 24, 33), 1.3f));
    v.push_back(G::X(ell(50, 50, 20.5, 29.5)));
    std::vector<Pt> bust;
    for (int i = 0; i <= 16; i++) {
      const double a = 30 + 120.0 * i / 16;
      bust.push_back(P(50 + 18 * std::cos(a * kPi / 180), 50 + 27 * std::sin(a * kPi / 180)));
    }
    std::string arc;
    for (const Pt& p : bust) arc += "L" + ptS(p);
    v.push_back(G::F("M" + ptS(bust.back()) + "C35 62 40 58.5 45.6 57L46.6 47H53.4L54.4 57C60 58.5 65 62 " + ptS(bust.front()) + arc + "Z"));
    over(v, "M50 28C54.5 28 57.2 32 57.2 37.5C57.2 43.5 54.2 48.5 50 50C45.8 48.5 42.8 43.5 42.8 37.5C42.8 32 45.5 28 50 28Z", 1.2);
    c.add("race-faceless-mirror", "Зеркало", fit(v));
  }
}

// Геральдическая летучая мышь с центром (cx, cy), размах 2·span: перепонки с фестонами между пальцами, острые уши.
// Пальцы крыла — вырезы fingers (для XS).
std::string batWing(double cx, double cy, double span, std::string* fingers = nullptr) {
  auto q = [&](double x, double y) { return P(cx + x * span / 45, cy + y * span / 45); };
  const Pt sh = q(-4, -3), wr = q(-20, -17), tip = q(-45, -17), f1 = q(-38, 14), f2 = q(-25, 20), f3 = q(-12, 16), bd = q(-4, 9);
  auto scal = [&](Pt a, Pt b, double depth) {
    const Pt m = lerpP(a, b, 0.5);
    const double dx = b.x - a.x, dy = b.y - a.y;
    return "Q" + ptS(P(m.x + dy * depth, m.y - dx * depth)) + " " + ptS(b);
  };
  const std::string half = "M" + ptS(sh) + "C" + ptS(q(-9, -10)) + " " + ptS(q(-15, -17)) + " " + ptS(wr) + "C" + ptS(q(-28, -15)) + " " + ptS(q(-38, -15)) +
                           " " + ptS(tip) + scal(tip, f1, 0.14) + scal(f1, f2, 0.3) + scal(f2, f3, 0.3) + scal(f3, bd, 0.28) + "Z";
  if (fingers) *fingers += "M" + ptS(lerpP(wr, f1, 0.08)) + "L" + ptS(lerpP(wr, f1, 0.86)) + "M" + ptS(lerpP(wr, f2, 0.08)) + "L" + ptS(lerpP(wr, f2, 0.86)) +
                           "M" + ptS(lerpP(wr, f3, 0.1)) + "L" + ptS(lerpP(wr, f3, 0.84));
  return half;
}
std::string bat(double cx, double cy, double span, std::string* fingers = nullptr) {
  std::string f;
  const std::string w = batWing(cx, cy, span, &f);
  if (fingers) *fingers += f + mirrorX(f, cx);
  const double k = span / 45;
  const std::string body = ell(cx, cy + 2.5 * k, 4.6 * k, 9 * k) + ell(cx, cy - 7.5 * k, 3.8 * k, 3.6 * k) +
                           "M" + n2(cx - 3.6 * k, cy - 9 * k) + "L" + n2(cx - 3.4 * k, cy - 15.5 * k) + "L" + n2(cx - 0.8 * k, cy - 10.5 * k) + "Z";
  return w + mirrorX(w, cx) + body + mirrorX("M" + n2(cx - 3.6 * k, cy - 9 * k) + "L" + n2(cx - 3.4 * k, cy - 15.5 * k) + "L" + n2(cx - 0.8 * k, cy - 10.5 * k) + "Z", cx);
}

// ---------------------------------------------------------------- вампиры
void vampires(Catalog& c) {
  c.group("Вампиры", EmblemSection::Races);
  // Вампирский лорд: плащ распахнут перепончатыми крыльями, высокий острый воротник, голова с клиновидной чёлкой.
  {
    std::vector<L> v;
    std::string ribs;
    const std::string cloak = batWing(50, 44, 48, &ribs) + "M46 38H40C38 52 36 70 33 90L41.5 86 46 95 50 89.5H50.5V38Z";
    v.push_back(G::F(sym(cloak)));
    v.push_back(G::XS(sym(ribs) + sym(curve({45, 50}, {44, 62}, {42.5, 74}, {41, 85})), 1.4f));
    const std::string collar = sym("M44 39L28.5 9.5 37 14.5 41 25 46.5 33Z");
    over(v, collar, 1.3);
    v.push_back(G::XS(sym(curve({42.5, 35}, {37.5, 26}, {34, 19}, {31.5, 13.5})), 1.2f));
    const std::string head = symFull("M50 15C45.5 15 43 18.5 43 23.5C43 29 45.5 33.5 50 36H50.5V15Z") + sym("M43.6 22 37.5 16.5 43.8 19.4Z");
    over(v, head, 1.3);
    v.push_back(G::X("M50 15.5 52.6 18.5C51.5 19.5 50.5 20.6 50 22C49.5 20.6 48.5 19.5 47.4 18.5Z" + sym("M44.6 25.2 48.4 26.4 45.2 27.4Z")));
    v.push_back(G::X("M47.5 38H52.5L50 44.5Z"));
    c.add("race-vampires-lord", "Вампирский лорд", fit(v));
  }
  // Кровавая луна: полный диск в двойном ободе, по нижнему краю стекают капли, на диске — летучая мышь.
  {
    std::vector<L> v;
    auto drip = [](double x, double y0, double len, double w) {
      return spike(P(x, y0), P(x, y0 + len * 0.35), P(x + 0.8, y0 + len * 0.7), P(x, y0 + len), w, 1.15);
    };
    v.push_back(G::F(circ(50, 43, 39) + drip(37, 70, 24, 6.4) + drip(50, 74, 25, 7.2) + drip(63, 70, 19, 5.6) + drip(27, 66, 13, 4.6)));
    v.push_back(G::XS(circ(50, 43, 35), 1.5f));
    std::string fingers;
    v.push_back(G::X(bat(50, 42, 30, &fingers)));
    v.push_back(G::S(fingers, 1.3f));
    c.add("race-vampires-moon", "Кровавая луна", fit(v));
  }
  // Перстень-печать: тонкая шинка с листьями на плечах, восьмигранный щиток с двойным ободком и летучей мышью.
  {
    std::vector<L> v;
    v.push_back(G::FE(ell(50, 66, 25, 30) + ell(50, 66, 19.5, 24.5)));
    over(v, sym(leaf({41, 49}, {26.5, 60}, 8.5, -1.5, 0.4, 1.3) + leaf({41, 51}, {31, 70}, 6.5, -1, 0.4, 1.3)), 1.2);
    v.push_back(G::XS(sym(leafRib({41, 49}, {26.5, 60}, -1.5, 0.3, 0.75) + leafRib({41, 51}, {31, 70}, -1, 0.3, 0.75)), 1.2f));
    std::string oct, oct2;
    for (int i = 0; i < 8; i++) {
      oct += (i ? "L" : "M") + pt(50, 30, 27, -67.5 + 45 * i);
      oct2 += (i ? "L" : "M") + pt(50, 30, 22.5, -67.5 + 45 * i);
    }
    oct += "Z";
    oct2 += "Z";
    over(v, oct, 1.6);
    v.push_back(G::XS(oct2, 1.4f));
    std::string fingers;
    v.push_back(G::X(bat(50, 31, 19, &fingers)));
    v.push_back(G::S(fingers, 1.2f));
    c.add("race-vampires-signet", "Перстень-печать", fit(v));
  }
  // Клыки в короне: зубцы-копья с огранкой, обод с самоцветами, из обода свисают клыки, между ними — капля крови.
  {
    std::vector<L> v;
    std::string tines;
    for (auto [x, top, w] : {std::tuple{50.0, 2.0, 11.0}, std::tuple{32.5, 11.0, 10.0}, std::tuple{15.5, 20.0, 8.5}})
      tines += blade(P(x, 44), P(x, 34), P(x, top + 12), P(x, top), w, 0.45, 0.3, 1.25);
    v.push_back(G::F(sym(tines)));
    v.push_back(G::XS("M50 12V36" + sym("M33 21V38M16 29V40"), 1.2f));
    const std::string band = symFull("M50 39H14C13 39 12.5 39.5 12.5 40.5V52.5C12.5 53.5 13 54 14 54H50.5V39Z");
    over(v, band, 1.5);
    v.push_back(G::XS(inset(band, 2.4), 1.2f));
    v.push_back(G::X("M50 42.5 54 46.5 50 50.5 46 46.5Z" + sym("M31.5 43.5 34.5 46.5 31.5 49.5 28.5 46.5Z")));
    const std::string fangs = sym(blade({35.5, 52}, {36.5, 66}, {39, 80}, {44, 95}, 11, 0.12, 0.4, 1.1));
    over(v, fangs, 1.4);
    v.push_back(G::XS(sym(curve({37.4, 58}, {38.2, 68}, {39.8, 78}, {42.4, 88})), 1.2f));
    v.push_back(G::F(blade({50, 60}, {50, 66}, {50, 72}, {50, 80}, 9, 0.72, 1.4, 0.6, 30)));
    c.add("race-vampires-crown", "Клыки в короне", fit(v));
  }
}

// ---------------------------------------------------------------- великаны
void giants(Catalog& c) {
  c.group("Великаны", EmblemSection::Races);
  // Великан с дубиной: исполин держит над головой суковатую дубину обеими руками, борода клином, повязка с рваным краем.
  {
    std::vector<L> v;
    const std::string club = ribbon([](double t) { return P(4 + 84 * t, 18.5 - 7 * t - 1.5 * std::sin(kPi * t)); }, [](double t) { return 5 + 10 * t * t; }) +
                             "M86 4.5L95.5 7 96.5 15 88 18.5Z" + spike({72, 9}, {72.5, 6}, {74, 3.5}, {76.5, 1.5}, 4.4, 1) +
                             spike({80, 16}, {82, 18.5}, {83, 21}, {83, 24}, 4.4, 1) + spike({60, 15}, {61, 17.5}, {61, 20}, {60, 22.5}, 3.6, 1) +
                             spike({48, 13}, {48.5, 10}, {50, 8}, {52, 6.5}, 3.2, 1);
    v.push_back(G::F(club));
    v.push_back(G::XS("M70 12.5 92 9M74 9.5 79 5.5M62 16.5 66 12.5", 1.3f));
    const std::string arms = sym(taper({35, 40}, {29, 36}, {25, 31}, {24, 24}, 10, 8) + taper({24.5, 25}, {25, 21}, {26.5, 18}, {28.5, 15}, 8, 7));
    const std::string fists = sym("M24 12C24 9.5 26 8 28.5 8H31.5C33.5 8 35 9.5 35 11.5V16C35 18 33.5 19.5 31.5 19.5H27C25.2 19.5 24 18.2 24 16.5Z");
    const std::string body = symFull("M50 33C44 33 38 34.5 34 37C31.5 39 31.5 43 33.5 47C35.5 51 37 56 38 62H50.5V33Z");
    const std::string legs = sym(taper({42, 74}, {40.5, 82}, {38.5, 88}, {36.5, 93}, 10, 8.5) + "M29 92.5H41.5L42.5 98H26.5Z");
    over(v, arms + fists + body + legs, 1.6);
    v.push_back(G::XS(sym("M27.5 8.5V13"), 1.2f));
    const std::string kilt = symFull("M50 60H37.5L34 76 38 73 41 78 44.5 74 47.5 79 50 75V60Z");
    over(v, kilt, 1.3);
    v.push_back(G::XS(sym(curve({36.5, 41}, {41, 45}, {46, 45.5}, {50, 43.5}) + curve({39, 51}, {42.5, 53}, {46, 53.5}, {50, 52.5})) + "M50 44V58", 1.3f));
    const std::string head = symFull("M50 18C46 18 43.5 21 43.5 25.5C43.5 29 44 31.5 45 33C46 37 47.5 41 50 46H50.5V18Z");
    over(v, head, 1.4);
    v.push_back(G::X(sym("M44.8 25.6H48.6L45.8 27.4Z")));
    v.push_back(G::XS(sym(curve({44.2, 23.4}, {45.8, 22.8}, {47.6, 22.9}, {49.2, 23.6})) + "M45.5 32.5H54.5" + sym(curve({45.5, 32.5}, {44.6, 33.5}, {44.2, 35}, {44.4, 36.5})) +
                          "M50 35.5V43",
                      1.2f));
    c.add("race-giants-club", "Великан с дубиной", fit(v));
  }
  // Горный трон: высеченный трон с острыми навершиями у подножия заснеженной горы о трёх вершинах, ступени.
  {
    std::vector<L> v;
    const std::string peaks = "M2 90L16 56 23 62 35 34 41 40 50 4 59 40 65 34 77 62 84 56 98 90Z";
    v.push_back(G::F(peaks));
    v.push_back(G::XS("M44.5 26 47 29.5 50 24.5 53 29.5 55.5 26", 1.5f));
    v.push_back(G::XS(hatchIn("M2 90L16 56 23 62 35 34 41 40 50 31 59 40 65 34 77 62 84 56 98 90Z", -50, 4.6, 2.2), 1.3f));
    const std::string throne = symFull("M50 36L46 41.5H41.5L39.5 35.5 37.5 41.5V64H31.5V76H35V79H50.5V36Z") + rrect(30, 78, 40, 5.5, 0.5) +
                               rrect(24, 85.5, 52, 5.5, 0.5) + rrect(18, 93, 64, 5.5, 0.5);
    over(v, throne, 2.2);
    v.push_back(G::XS(inset(symFull("M50 36L46 41.5H41.5L39.5 35.5 37.5 41.5V64H31.5V76H35V79H50.5V36Z"), 2.4), 1.2f));
    v.push_back(G::X(symFull("M50 44L47 47.5V62.5H50.5V44Z")));
    v.push_back(G::XS(sym("M40.5 45.5V62.5") + "M37.5 68.5H62.5", 1.3f));
    v.push_back(G::F("M50 49 52.2 53 50 57 47.8 53Z"));
    c.add("race-giants-throne", "Горный трон", fit(v));
  }
  // Огромный кулак: латная перчатка с пластинами обрушивается на каменную плиту, плита раскалывается, летят осколки.
  {
    std::vector<L> v;
    const std::string ground = "M3 82L18 78 30 81 41 77 50 80 59 77 70 81 82 78 97 82V97H3Z";
    v.push_back(G::F(ground));
    v.push_back(G::XS("M50 80 46.5 87 50.5 91 48 97M40 78 35 88 37 97M60 78 65.5 87 63 97M22 84 27 91M78 84 73 91", 1.5f));
    std::string shards;
    for (auto [x, y, a, s] : {std::tuple{11.0, 64.0, -55.0, 1.0}, std::tuple{89.0, 64.0, 55.0, 1.0}, std::tuple{15.0, 48.0, -70.0, 0.85},
                              std::tuple{85.0, 48.0, 70.0, 0.85}, std::tuple{6.0, 75.0, -35.0, 0.8}, std::tuple{94.0, 75.0, 35.0, 0.8}})
      shards += xf("M0 -8 2.8 0.5 0.6 5 -2.8 1Z", place(float(x), float(y), float(a), float(s)));
    v.push_back(G::F(shards));
    const std::string fist = rrect(23, 21, 54, 34, 8) + rrect(23, 45, 13.5, 23, 6.5) + rrect(36.5, 47, 13.5, 26, 6.5) + rrect(50, 47, 13.5, 26, 6.5) +
                             rrect(63.5, 45, 13.5, 23, 6.5);
    over(v, fist, 1.5);
    v.push_back(G::XS("M36.5 50V66M50 50V70M63.5 50V66M27 38H73M30 30C38 27.5 46 27 52 28.5M24.5 57H35M38 59H48.5M51.5 59H62M65 57H75.5", 1.4f));
    const std::string cuff = symFull("M50 2H31.5L33.5 22H50.5V2Z");
    over(v, cuff, 1.5);
    v.push_back(G::XS("M33 9H67M33.6 15H66.4", 1.4f));
    c.add("race-giants-fist", "Огромный кулак", fit(v));
  }
  // Каменная корона: веер стоячих камней-менгиров с рунами на изогнутом тёсаном ободе, самоцвет в средней глыбе.
  {
    std::vector<L> v;
    std::string stones;
    for (auto [a, len, w] : {std::tuple{0.0, 58.0, 20.0}, std::tuple{-21.0, 49.0, 16.0}, std::tuple{21.0, 49.0, 16.0}, std::tuple{-41.0, 40.0, 13.0},
                             std::tuple{41.0, 40.0, 13.0}}) {
      const double tilt = a > 0 ? 2.5 : (a < 0 ? -2.5 : 0);
      stones += xf("M" + n2(-w / 2, 0) + "L" + n2(-w / 2, -len + w * 0.5) + "L" + n2(-w * 0.1 + tilt, -len) + "L" + n2(w / 2, -len + w * 0.3) + "L" +
                       n2(w / 2, 0) + "Z",
                   rotAt(float(a), 50, 120) * Affine::translate(50, 68));
    }
    v.push_back(G::F(stones));
    v.push_back(G::XS(xf("M-1.5 -36 1.5 -32 -1.5 -28 1.5 -24", rotAt(-21, 50, 120) * Affine::translate(50, 68)) +
                          xf("M1.5 -36 -1.5 -32 1.5 -28 -1.5 -24", rotAt(21, 50, 120) * Affine::translate(50, 68)) +
                          xf("M0 -28 2 -25 0 -22", rotAt(-41, 50, 120) * Affine::translate(50, 68)) + xf("M0 -28 -2 -25 0 -22", rotAt(41, 50, 120) * Affine::translate(50, 68)),
                      1.4f));
    const std::string rim = "M4 63Q50 51 96 63L91 88Q50 76 9 88Z";
    over(v, rim, 1.6);
    v.push_back(G::XS("M7 75Q50 63 93 75" + std::string("M27 60 26 70M50 57V68M73 60 74 70M38 70 37.5 80M62 70 62.5 80M16 73 15.5 83M84 73 84.5 83"), 1.4f));
    v.push_back(G::X("M50 24 56 33 50 42 44 33Z"));
    v.push_back(G::F("M50 28 53.2 33 50 38 46.8 33Z"));
    c.add("race-giants-crown", "Каменная корона", fit(v));
  }
}

// ---------------------------------------------------------------- высшие эльфы
void highElves(Catalog& c) {
  c.group("Высшие Эльфы", EmblemSection::Races);
  // Феникс: восстающий феникс — крылья подняты, голова в профиль с огненным хохолком, хвост из языков пламени.
  {
    std::vector<L> v;
    for (auto [tx, ty, c2x, w] : {std::tuple{24.0, 88.0, 18.0, 7.0}, std::tuple{37.0, 97.0, 30.0, 8.0}}) {
      const std::string f = blade({48, 66}, {36, 72}, P(c2x + 14, ty - 14), P(tx, ty), w, 0.3, 0.6, 1.6);
      over(v, sym(f), 1.0);
    }
    over(v, blade({50, 64}, {46, 76}, {54, 86}, {50, 99}, 9, 0.3, 0.6, 1.6), 1.0);
    addWing(v, {{45, 44}, {37, 30}, {22, 14}, {4, 6}, {5, 30}, {12, 54}, {30, 72}, 10, 0.42, 1.5, 4.6}, true);
    const std::string body = blade({50, 30}, {50, 42}, {50, 58}, {50, 74}, 14, 0.45, 0.6, 1.1);
    const std::string neck = taper({50, 40}, {46, 32}, {47, 24}, {53, 17}, 7.5, 5.5);
    const std::string head = "M53 11C57 11 59.5 13.5 59.5 17C59.5 19.5 58 21.5 56 22.5L51 22C49 20.5 48.5 18 49 15.5C49.8 12.8 51.2 11 53 11Z" +
                             spike({58.5, 15}, {62, 15.5}, {65, 17}, {67, 20.5}, 3.8, 1.1);
    const std::string crest = blade({51, 13}, {46, 8}, {44, 3}, {38, 1}, 4.4, 0.3, 0.6, 1.3) + blade({50.5, 16}, {44, 13}, {40, 12}, {34, 13}, 4, 0.3, 0.6, 1.3) +
                              blade({52, 11.5}, {51, 6}, {53, 3}, {50, 0.5}, 3.6, 0.3, 0.6, 1.3);
    over(v, body + neck + head + crest, 1.3);
    v.push_back(G::X(circ(55, 15.6, 1.3)));
    v.push_back(G::XS("M50 46V66" + sym("M47 50 50 53M47 56 50 59M47.4 62 50 64.6"), 1.2f));
    c.add("race-high-elves-phoenix", "Феникс", fit(v));
  }
  // Высокий шпиль: башня с вогнутым шпилем-иглой, две башни пониже, арочные мостики, стрельчатые окна, терраса.
  {
    std::vector<L> v;
    auto tower = [](double x, double w, double top, double shaft, double base) {
      return "M" + n2(x - w * 0.62, shaft) + "C" + n2(x - w * 0.2, shaft - 5) + " " + n2(x - w * 0.06, top + (shaft - top) * 0.4) + " " + n2(x, top) + "C" +
             n2(x + w * 0.06, top + (shaft - top) * 0.4) + " " + n2(x + w * 0.2, shaft - 5) + " " + n2(x + w * 0.62, shaft) + "Z" +
             rrect(x - w * 0.62, shaft - 0.2, w * 1.24, 3.2, 0.6) + "M" + n2(x - w / 2, shaft + 2.8) + "H" + n(x + w / 2) + "L" + n2(x + w * 0.56, base) + "H" +
             n(x - w * 0.56) + "Z";
    };
    const std::string side = tower(28, 10, 22, 54, 91);
    const std::string mainT = tower(50, 13, 1, 38, 91);
    v.push_back(G::F(sym(side)));
    const std::string bridges = sym(ribbon([](double t) { return P(33 + 12 * t, 66 - 6 * std::sin(kPi * t)); }, [](double) { return 3; }, 20) +
                                    ribbon([](double t) { return P(33 + 12 * t, 78 - 5 * std::sin(kPi * t)); }, [](double) { return 2.6; }, 20));
    v.push_back(G::F(bridges));
    over(v, mainT, 1.5);
    auto lancet = [](double x, double y, double w, double h) {
      return "M" + n2(x - w / 2, y + h) + "V" + n(y + w * 0.6) + "Q" + n2(x - w / 2, y) + " " + n2(x, y - w * 0.4) + "Q" + n2(x + w / 2, y) + " " +
             n2(x + w / 2, y + w * 0.6) + "V" + n(y + h) + "Z";
    };
    v.push_back(G::X(lancet(50, 47, 4, 12) + lancet(50, 66, 4, 10) + sym(lancet(28, 62, 3, 9))));
    v.push_back(G::XS("M50 8V30" + sym("M28 30V48"), 1.2f));
    const std::string base = "M10 97L14 89H86L90 97Z";
    over(v, base, 1.4);
    v.push_back(G::X("M44.5 97V93C44.5 90.5 47 89 50 89C53 89 55.5 90.5 55.5 93V97Z"));
    c.add("race-high-elves-spire", "Высокий шпиль", fit(v));
  }
  // Эльфийская тиара: обод дугой с самоцветами, высокий лист с самоцветом-маркизом, боковые листья, завитки на концах обода.
  {
    std::vector<L> v;
    auto bandC = [](double t) { return P(5 + 90 * t, 60 + 20 * std::sin(kPi * t)); };
    const std::string band = ribbon(bandC, [](double t) { return 3.6 + 9 * std::sin(kPi * t); }, 48);
    std::string curls;
    for (double dir : {-1.0, 1.0}) {
      const double cx = 50 + dir * 41, cy = 54.5;
      curls += ribbon([&](double t) { return polar(cx, cy, 5.5 * (1 - 0.7 * t), dir < 0 ? 90 + 300 * t : 90 - 300 * t); }, [](double t) { return 2.8 * (1 - 0.6 * t); }, 30);
    }
    for (auto [bx, by, tx, ty, w, bend] : {std::tuple{29.0, 71.0, 11.0, 40.0, 11.5, -2.0}, std::tuple{39.0, 75.0, 26.0, 25.0, 13.0, -2.5}}) {
      v.push_back(G::F(sym(leaf(P(bx, by), P(tx, ty), w, bend, 0.42, 1.3))));
      v.push_back(G::XS(sym(leafRib(P(bx, by), P(tx, ty), bend, 0.15, 0.8)), 1.2f));
    }
    const std::string mid = leaf({50, 76}, {50, 2}, 21, 0, 0.45, 1.25);
    over(v, mid, 1.4);
    v.push_back(G::XS(inset(mid, 2.4), 1.2f));
    v.push_back(G::X("M50 22C53.5 27.5 55.2 32.5 55.2 37C55.2 41.5 53.5 46.5 50 52C46.5 46.5 44.8 41.5 44.8 37C44.8 32.5 46.5 27.5 50 22Z"));
    v.push_back(G::F("M50 27C52 31 52.9 34.5 52.9 37C52.9 39.5 52 43 50 47C48 43 47.1 39.5 47.1 37C47.1 34.5 48 31 50 27Z"));
    over(v, band + curls, 1.4);
    std::string inner;
    for (int i = 0; i <= 30; i++) inner += (i ? "L" : "M") + ptS(bandC(0.12 + 0.76 * i / 30.0));
    v.push_back(G::XS(inner, 1.2f));
    std::string gems;
    for (double t : {0.3, 0.5, 0.7}) {
      const Pt p = bandC(t);
      gems += "M" + n2(p.x, p.y - 2.6) + "L" + n2(p.x + 2.6, p.y) + "L" + n2(p.x, p.y + 2.6) + "L" + n2(p.x - 2.6, p.y) + "Z";
    }
    v.push_back(G::XS(gems, 3.2f));
    v.push_back(G::F(gems));
    c.add("race-high-elves-tiara", "Эльфийская тиара", fit(v));
  }
  // Магический кристалл: огранённый кристалл с двумя меньшими, кольцо-орбита вокруг, звёзды-искры.
  {
    std::vector<L> v;
    const Affine el = Affine::translate(50, 56) * Affine::rotate(float(-12 * kPi / 180)) * Affine::scale(1, 0.24f);
    v.push_back(G::S(xf(arc(0, 0, 44, 180, 360), el), 2.6f));
    const std::string side = "M0 -24L5.5 -16V10L0 16 -5.5 10V-16Z";
    const std::string sides = xf(side, place(31, 66, -22)) + xf(side, place(69, 66, 22));
    over(v, sides, 1.4);
    v.push_back(G::XS(xf("M0 -22V14", place(31, 66, -22)) + xf("M0 -22V14", place(69, 66, 22)), 1.2f));
    const std::string cr = "M50 3L61 19V74L50 90 39 74V19Z";
    over(v, cr, 1.6);
    v.push_back(G::XS("M50 3V90M39 19 50 27 61 19M39 74 50 66 61 74", 1.3f));
    v.push_back(G::XS(xf(arc(0, 0, 44, 0, 180), el), 6.5f));
    v.push_back(G::S(xf(arc(0, 0, 44, 0, 180), el), 2.6f));
    std::string sparks;
    for (auto [x, y, r] : {std::tuple{16.0, 22.0, 8.0}, std::tuple{84.0, 17.0, 6.5}, std::tuple{86.0, 86.0, 5.5}, std::tuple{14.0, 88.0, 4.5}})
      sparks += star(x, y, r, r * 0.2, 4, -90);
    v.push_back(G::F(sparks));
    c.add("race-high-elves-crystal", "Магический кристалл", fit(v));
  }
}

// ---------------------------------------------------------------- гарпии
void harpies(Catalog& c) {
  c.group("Гарпии", EmblemSection::Races);
  // Гарпия в полёте: распростёртая гарпия — крылья раскинуты, женская голова в волнах волос, лапы с когтями, хвост веером.
  {
    std::vector<L> v;
    for (auto [tx, ty, w] : {std::tuple{36.0, 95.0, 8.0}, std::tuple{43.0, 98.0, 8.5}})
      over(v, sym(leaf({48, 72}, P(tx, ty), w, 0, 0.6, 1.3)), 1.0);
    over(v, leaf({50, 72}, {50, 99}, 9, 0, 0.6, 1.3), 1.0);
    // Лапы: голени и когти.
    std::string legs = sym(taper({45, 70}, {42, 75}, {39, 79}, {35, 82}, 5, 4) + spike({35.5, 81.5}, {31, 82}, {28, 85}, {26.5, 89.5}, 3.6, 1) +
                           spike({35.5, 82}, {33, 86}, {33, 89}, {35, 92}, 3.4, 1) + spike({36, 81}, {33, 79}, {30, 78.5}, {27.5, 80}, 3, 1));
    over(v, legs, 1.1);
    addWing(v, {{44, 44}, {36, 30}, {22, 12}, {3, 8}, {2, 30}, {8, 54}, {28, 72}, 10, 0.42, 1.5, 5}, true);
    const std::string body = symFull("M50 30C46 30 43 32 42 35.5C41 40 41.5 46 42.5 52C43.5 60 44.5 66 46.5 73H50.5V30Z");
    over(v, body, 1.3);
    v.push_back(G::XS(sym(curve({43, 41}, {45, 45}, {47.5, 46}, {50, 45})) + sym("M45 56 50 60M45.5 63 50 67M46.5 69.5 50 72.5"), 1.3f));
    // Голова с волосами: пряди-языки по обе стороны лица.
    const std::string hair = sym(blade({47, 13}, {38, 14}, {40, 26}, {33, 36}, 7, 0.35, 0.6, 1.3) + blade({46, 18}, {41, 24}, {44, 32}, {39, 40}, 5, 0.35, 0.6, 1.3));
    over(v, hair, 1.0);
    const std::string face = "M50 10C54 10 56.5 13.5 56.5 18C56.5 23 54 27 50 29C46 27 43.5 23 43.5 18C43.5 13.5 46 10 50 10Z";
    over(v, face, 1.2);
    v.push_back(G::X(sym("M45.4 18.6 48.8 19.6 45.8 20.8Z")));
    v.push_back(G::XS("M50 20V24" + sym(curve({44.6, 15.6}, {46, 14.8}, {47.6, 14.9}, {49, 16})), 1.2f));
    c.add("race-harpies-flight", "Гарпия в полёте", fit(v));
  }
  // Когтистая лапа: в профиль — оперённое бедро с рваным верхом, чешуйчатая цевка, пальцы с крючьями когтей сжимаются.
  {
    std::vector<L> v;
    const std::string thigh = "M10 3L15.5 8.5 20 1.5 25 8.5 30 2 35 9 40.5 3 44 12C47.5 20 47.5 30 44 37L39 45.5 35.5 38 31 46.5 28 38C20 33 12 25 10 15Z";
    v.push_back(G::F(thigh));
    v.push_back(G::XS(curve({20, 12}, {22, 20}, {26, 27}, {30.5, 33}) + curve({28, 11}, {30, 19}, {33, 26}, {36.5, 33}) + curve({36, 12}, {38, 20}, {40, 26}, {41, 32}),
                      1.3f));
    const std::string shank = taper({37, 36}, {41, 46}, {46, 55}, {51, 63}, 13, 11);
    over(v, shank, 1.3);
    std::string scales;
    for (int i = 0; i < 5; i++) {
      const Pt p = bzP({37, 36}, {41, 46}, {46, 55}, {51, 63}, 0.22 + 0.15 * i);
      scales += xf("M-4.4 -1.5Q0 1.5 4.4 -1.5", place(p.x, p.y, -33));
    }
    v.push_back(G::XS(scales, 1.2f));
    const std::string toes = taper({50, 61}, {60, 58}, {70, 56}, {80, 58}, 11, 8) + taper({51, 64}, {60, 68}, {66, 74}, {71, 82}, 11, 8) +
                             taper({50, 66}, {52, 74}, {53, 80}, {52, 88}, 10.5, 7.5) + taper({47, 62}, {41, 66}, {36, 70}, {31, 75}, 9.5, 7);
    const std::string claws = spike({79.5, 57.5}, {87, 59}, {92, 66}, {91, 75}, 8, 1.2) + spike({70.5, 81.5}, {74, 88}, {73, 94}, {68, 98}, 8, 1.2) +
                              spike({52, 87.5}, {52, 93}, {49, 97}, {44, 98.5}, 7.5, 1.2) + spike({31.5, 74.5}, {27, 79}, {22, 81}, {17, 80}, 7, 1.2);
    over(v, toes + claws, 1.2);
    v.push_back(G::XS("M61.5 55.5 62.5 60.5M70.5 54.5 71 59.5M59 66.5 62.5 70M64.5 72.5 68 75M48.5 74 53.5 74.5M49 80 53.5 80.5M41 63.5 43 68M36 67 38 71.5",
                      1.2f));
    c.add("race-harpies-talon", "Когтистая лапа", fit(v));
  }
  // Перо-кинжал: клинок-перо с бородками и надрывом, прямая гарда с остриями, рукоять с обмоткой, гранёное навершие.
  {
    std::vector<L> v;
    const std::string vane = leaf({50, 68}, {50, 2}, 29, 0, 0.5, 1.2);
    v.push_back(G::F(vane));
    std::string barbs;
    for (int i = 0; i < 7; i++) {
      const double y = 60 - 7.2 * i;
      barbs += sym("M49 " + n(y) + "L" + n2(38.5 + i * 0.9, y - 7));
    }
    v.push_back(G::XS(barbs + "M50 6V66", 1.3f));
    v.push_back(G::X("M36.5 31 46.5 34.5 37.5 38.5Z"));
    const std::string guard = blade({27, 66}, {38, 67.5}, {62, 67.5}, {73, 66}, 5.5, 0.5, 0.45, 0.9) + "M50 63 54 67.5 50 72 46 67.5Z";
    const std::string hilt = rrect(45, 70, 10, 16, 2.5) + "M50 84.5 56 90.5 50 97.5 44 90.5Z";
    over(v, guard + hilt, 1.4);
    v.push_back(G::XS("M45 74.5 55 77.5M45 79 55 82", 1.2f));
    v.push_back(G::X("M50 87.6 52.4 90.5 50 93.6 47.6 90.5Z"));
    c.add("race-harpies-dagger", "Перо-кинжал", fit(moved(v, rotAt(36, 50, 50))));
  }
  // Гнездо на скале: острый утёс, гнездо из переплетённых прутьев, три яйца.
  {
    std::vector<L> v;
    const std::string crag = "M14 98L22 80 27 77 31 66 38 64 44 56 52 60 58 54 64 62 72 66 76 76 82 80 88 98Z";
    v.push_back(G::F(crag));
    v.push_back(G::XS("M31 66 35 76 31 86M44 56 47 66 45 74M58 54 60 63M64 62 67 74 64 84M76 76 77 88M50 76 53 88", 1.4f));
    auto egg = [](double x, double y, double h) {
      std::vector<Pt> p;
      for (int i = 0; i < 48; i++) {
        const double t = 2 * kPi * i / 48, w = h * 0.37;
        p.push_back(P(x + w * std::sin(t) * (1 - 0.16 * std::cos(t)), y - h / 2 * std::cos(t)));
      }
      return polyPath(p);
    };
    v.push_back(G::F(egg(36, 34, 21) + egg(64, 34, 21)));
    over(v, egg(50, 26, 26), 1.3);
    v.push_back(G::XS(curve({45.5, 19}, {44, 22}, {43.6, 26}, {44.2, 29.5}) + curve({31.6, 29}, {30.6, 31.5}, {30.4, 34}, {31, 37}) +
                          curve({68.4, 29}, {69.4, 31.5}, {69.6, 34}, {69, 37}),
                      1.2f));
    const std::string nest = "M10 37Q50 47 90 37Q86 55 70 61H30Q14 55 10 37Z";
    over(v, nest, 1.5);
    v.push_back(G::XS("M16 43 26 57M25 44.5 35 59M34 45.5 43 60M43 46 51 60M52 46 59 60M61 45.5 67 59M70 44.5 74 56M79 42.5 81 50" +
                          std::string("M14 47Q50 56 86 47M20 54Q50 62 80 54"),
                      1.3f));
    v.push_back(G::F(sym(spike({14, 40}, {10, 38}, {6, 36}, {2, 33}, 3, 1) + spike({20, 53}, {15, 55}, {11, 56}, {6, 59}, 2.8, 1))));
    c.add("race-harpies-nest", "Гнездо на скале", fit(v));
  }
}

// Кость вдоль оси x от −len/2 до len/2 толщины w: стержень и по два заострённых мыщелка на концах.
std::string boneD(double len, double w) {
  const double h = len / 2;
  std::string s = taper(P(-h + w * 0.8, 0), P(-h / 3, 0), P(h / 3, 0), P(h - w * 0.8, 0), w, w);
  for (double sx : {-1.0, 1.0})
    for (double sy : {-1.0, 1.0})
      s += blade(P(sx * (h - w * 1.6), 0), P(sx * (h - w * 0.9), sy * w * 0.35), P(sx * (h - w * 0.3), sy * w * 0.7), P(sx * h, sy * w * 1.05), w * 1.15, 0.45,
                 0.5, 1.1, 16);
  return s;
}

// ---------------------------------------------------------------- гноллы
void gnolls(Catalog& c) {
  c.group("Гноллы", EmblemSection::Races);
  // Голова гиены: анфас — торчащие уши, грива-гребень языками, клин морды с оскалом, раскосые глаза, бакенбарды-пряди.
  {
    std::vector<L> v;
    std::string mane = blade({50, 22}, {47, 15}, {53, 9}, {50, 1}, 7.5, 0.3, 0.6, 1.4);
    mane += sym(blade({46, 22}, {41, 17}, {46, 11}, {41, 4}, 6.5, 0.3, 0.6, 1.4) + blade({33, 46}, {25, 47}, {20, 52}, {12, 52}, 7, 0.3, 0.6, 1.3) +
                blade({34, 54}, {27, 57}, {23, 63}, {16, 65}, 6.5, 0.3, 0.6, 1.3) + blade({37, 62}, {32, 67}, {30, 73}, {24, 77}, 6, 0.3, 0.6, 1.3));
    v.push_back(G::F(mane));
    const std::string ears = sym(leaf({38, 31}, {19, 6}, 18, 1.5, 0.55, 0.8));
    over(v, ears, 1.2);
    v.push_back(G::X(sym(leaf({36, 28}, {22.5, 11}, 7.5, 1, 0.55, 0.8))));
    const std::string head = symFull("M50 18C42 18 35 21 32 27C29.5 32 30 40 33 46C35.5 51 37 57 38.5 63C40 72 43 80 46.5 86C47.5 87.5 48.8 88 50.5 88V18Z");
    over(v, head, 1.4);
    v.push_back(G::X(sym("M35.5 39 46.5 42.5 44.5 46.5 37.5 43.5Z")));
    v.push_back(G::XS(sym(curve({34, 35}, {38, 35.5}, {43, 37}, {47, 39.5}) + curve({39.5, 63}, {42, 71}, {45, 76}, {50, 78})) + "M50 47V66", 1.3f));
    v.push_back(G::X("M44.5 69C47 67.8 53 67.8 55.5 69C54.5 72 52 74 50 74.5C48 74 45.5 72 44.5 69Z" + sym("M41.5 75 44.5 81 45.5 76Z")));
    c.add("race-gnolls-head", "Голова гиены", fit(v));
  }
  // Костяной цеп: рукоять из бедренной кости с ремнями, звенья цепи, шипастое било с обручами.
  {
    std::vector<L> v;
    const Affine m = place(15, 85, -46);
    const std::string bone = xf(boneD(52, 10), m * Affine::translate(26, 0));
    over(v, bone, 1.4);
    v.push_back(G::XS(xf("M11 -5.4 14 5.4M15.5 -5.4 18.5 5.4M20 -5.4 23 5.4", m), 1.3f));
    std::string links;
    for (int i = 0; i < 3; i++) {
      const Pt p = m.apply(P(57 + 6.2 * i, 0));
      links += xf(i % 2 ? rrect(-4.6, -1.4, 9.2, 2.8, 1.4) : ell(0, 0, 4.6, 3), place(p.x, p.y, -46));
    }
    v.push_back(G::S(links, 2.2f));
    const Pt hc = m.apply(P(88, 0));
    std::string spikes;
    for (int k = 0; k < 8; k++) {
      const double a = -67.5 + 45 * k;
      spikes += "M" + pt(hc.x, hc.y, 14, a - 15) + "L" + pt(hc.x, hc.y, 27, a) + "L" + pt(hc.x, hc.y, 14, a + 15) + "Z";
    }
    v.push_back(G::F(spikes + circ(hc.x, hc.y, 17)));
    v.push_back(G::XS(circ(hc.x, hc.y, 17), 1.4f));
    v.push_back(G::XS(xf(ell(0, 0, 5, 16.5), place(hc.x, hc.y, -46)) + xf("M-16.5 0H16.5", place(hc.x, hc.y, -46)), 1.3f));
    c.add("race-gnolls-flail", "Костяной цеп", fit(v));
  }
  // Хохочущая пасть: гиена в профиль — тяжёлая голова, короткая морда с распахнутой пастью, клыки, круглое ухо, грива.
  {
    std::vector<L> v;
    std::string mane;
    for (int i = 0; i < 6; i++) {
      const double t = i / 5.0, x = 63 + 22 * t, y = 12 + 58 * t;
      mane += blade(P(x - 6, y + 2), P(x + 2, y - 1), P(x + 6, y - 2), P(x + 11 - 2 * t, y - 7), 8, 0.3, 0.6, 1.4);
    }
    v.push_back(G::F(mane));
    const std::string ear = leaf({62, 24}, {70, 3}, 15, -1.5, 0.55, 0.9);
    over(v, ear, 1.2);
    v.push_back(G::X(leaf({63, 21}, {68.5, 8}, 6, -1, 0.55, 0.9)));
    const std::string upper = "M84 32C82 22 74 15 63 15C54 15 47 18 41 23C36 27 29 30 22 32C17 33.5 15 37 15.5 40.5C16 44 18.5 46 22 46.5L46 47C55 47 62 50 67 55L80 62C86 55 86 42 84 32Z";
    const std::string lower = "M69 60C63 65 56 70 48 74C41 77.5 35 79 30 78.5C27 78 26 75.5 28 74C35 69.5 41 65 46.5 61C52 57 58 55 64 55Z";
    const std::string neck = "M67 50C75 52 83 58 87 66C91 76 93 86 94 98H63C64 88 65 79 67 72Z";
    over(v, neck, 1.3);
    over(v, upper + lower, 1.3);
    std::string teeth;
    for (int i = 0; i < 3; i++) {
      const double x = 24 + 7.5 * i, h = i == 0 ? 8.5 : 5.5;
      teeth += "M" + n2(x - 2.4, 46.6) + "L" + n2(x + 2.4, 46.8) + "L" + n2(x + 0.3, 46.8 + h) + "Z";
      const double lx = 32 + 7 * i, ly = 76.5 - 3.8 * i, lh = i == 0 ? 8 : 5.5;
      teeth += "M" + n2(lx - 2.4, ly - 0.5) + "L" + n2(lx + 2.4, ly - 2) + "L" + n2(lx - 0.6, ly - 1 - lh) + "Z";
    }
    v.push_back(G::F(teeth));
    v.push_back(G::X("M45 28.5 56 27.5 53.5 32 46.5 32Z" + std::string("M15.8 38C17.5 35.6 21 35.6 22 38.2C21 40.8 17.5 41 15.8 38Z")));
    v.push_back(G::XS(curve({39, 23.5}, {45, 21.5}, {51, 21.5}, {57, 23}) + curve({59, 35}, {65, 37}, {71, 43}, {73, 51}), 1.3f));
    v.push_back(G::X(circ(77, 73, 1.8) + circ(73, 87, 1.8) + circ(74, 40, 1.5)));
    c.add("race-gnolls-laugh", "Хохочущая пасть", fit(v));
  }
  // Тотем из черепов: шест с обмоткой, череп гиены наверху, скрещённые кости, рогатый череп зверя, перья на шнурах.
  {
    std::vector<L> v;
    v.push_back(G::F(rrect(46.5, 8, 7, 90, 1.5)));
    v.push_back(G::XS("M46.5 52 53.5 54.5M46.5 55.5 53.5 58M46.5 88 53.5 90.5M46.5 91.5 53.5 94", 1.2f));
    over(v, xf(boneD(46, 5.2), place(50, 45, 32)) + xf(boneD(46, 5.2), place(50, 45, -32)), 1.3);
    const std::string top = symFull("M50 2C45 2 41 5 40 10.5L38.5 19C38.5 24.5 40.5 28.5 44 31L46 37H50.5V2Z") + sym(leaf({41, 9}, {31, 1}, 7, 1, 0.5, 1.2));
    over(v, top, 1.3);
    v.push_back(G::X(sym("M41 14 47 15.5 46.5 20.5 41.5 19Z") + "M48 28.5H52L50 32Z"));
    v.push_back(G::XS("M45.5 34H54.5", 1.2f));
    const std::string low = symFull("M50 62C44 62 40 66 40 71.5L42 80.5 46 87H50.5V62Z") + sym(spike({42, 66.5}, {32, 60}, {20, 63}, {17, 75}, 7.5, 1.1));
    over(v, low, 1.3);
    v.push_back(G::X(sym("M42 70 47.5 71.5 47 76 42.5 74.5Z") + "M48.5 80H51.5L50 83.5Z"));
    v.push_back(G::F(sym(taper({18.5, 72}, {18.3, 74}, {18.2, 76}, {18, 78}, 1.8, 1.8) + taper({26, 68}, {26.2, 70}, {26.4, 72}, {26.5, 74}, 1.8, 1.8))));
    const std::string feathers = sym(leaf({18, 77}, {15, 97}, 7, 1, 0.5, 1.3) + leaf({26.5, 73}, {28, 92}, 6.5, -1, 0.5, 1.3));
    over(v, feathers, 1.0);
    v.push_back(G::XS(sym(leafRib({18, 77}, {15, 97}, 1, 0.2, 0.8) + leafRib({26.5, 73}, {28, 92}, -1, 0.2, 0.8)), 1.2f));
    c.add("race-gnolls-totem", "Тотем из черепов", fit(v));
  }
}

// ---------------------------------------------------------------- гномы
void dwarvesRace(Catalog& c) {
  c.group("Гномы", EmblemSection::Races);
  // Гномий король: корона с острыми зубцами, тяжёлые брови, усы вразлёт, борода, расходящаяся двумя косами с кольцами.
  {
    std::vector<L> v;
    const std::string face = symFull("M50 25H33C32 31 31.5 38 32 44C28.5 47 26 53 27 60C28 68 31 75 35 81L41 98 44.5 84C47 82 48.5 81 50 80.5H50.5V25Z");
    over(v, face, 1.4);
    v.push_back(G::XS(sym(curve({37, 61}, {38, 67}, {39.5, 73}, {42, 79}) + curve({43.5, 61}, {44.5, 67}, {46, 72}, {48.5, 77})) + sym("M36.5 85.5 43 83.5M38 90.5 42.5 89"), 1.3f));
    const std::string brows = sym("M50 36.5C46 33.5 40 33 33.5 35.5L32.5 39C38 37.5 43.5 38.5 48 40.5Z");
    over(v, brows, 1.0);
    v.push_back(G::X(sym("M37.5 41.5 45 42.5 40 44.5Z")));
    v.push_back(G::XS(sym(curve({47.5, 41}, {46.5, 45}, {45.8, 49}, {47, 52})), 1.3f));
    const std::string moust = sym(blade({50, 53}, {43, 52}, {35, 55}, {24, 51}, 6.5, 0.3, 0.6, 1.2));
    over(v, moust, 1.1);
    const std::string crown = "M31 27L28 5 38 13 44 3 50 11 56 3 62 13 72 5 69 27Z";
    over(v, crown, 1.4);
    v.push_back(G::XS("M30.6 21.5H69.4", 1.3f));
    v.push_back(G::X("M50 14.5 52.6 17.5 50 20.5 47.4 17.5Z" + sym("M37 16.6 39 18.6 37 20.6 35 18.6Z")));
    c.add("race-dwarves-king", "Гномий король", fit(v));
  }
  // Рунный молот: тяжёлый боёк со скосами и рунами, шип сверху, окованная рукоять с обмоткой, гранёное навершие.
  {
    std::vector<L> v;
    const std::string headD = "M14 9H86L92 15V40L86 46H14L8 40V15Z";
    over(v, headD + "M42 9 50 0.5 58 9Z", 1.4);
    v.push_back(G::XS(inset(headD, 2.6), 1.3f));
    v.push_back(G::XS("M25 9V46M75 9V46", 1.4f));
    v.push_back(G::XS("M36 19V37M36 19 32 24M36 19 40 24M45 20V36L55 20V36ZM64 19V37M64 19 60 24M64 19 68 24", 1.8f));
    const std::string haft = "M44 46H56L55 86H45Z" + std::string("M41.5 46H58.5L57.5 52H42.5Z") + "M43 84H57L55.5 90 50 99 44.5 90Z";
    over(v, haft, 1.4);
    v.push_back(G::XS("M45 57 55 60.5M45 62.5 55 66M45 68 55 71.5M45 73.5 55 77M43.5 87.5H56.5", 1.3f));
    c.add("race-dwarves-hammer", "Рунный молот", fit(v));
  }
  // Щит с рунами: круглый щит, руны по ободу, лучи-рёбра к умбону, умбон со звездой.
  {
    std::vector<L> v;
    v.push_back(G::F(circ(50, 50, 47)));
    v.push_back(G::XS(circ(50, 50, 37), 1.6f));
    v.push_back(G::XS(circ(50, 50, 44.2), 1.3f));
    std::string runes;
    const char* glyphs[] = {"M0 -3V3M0 -3 2.4 -0.8", "M-1.6 -3V3M1.6 -3V3M-1.6 0 1.6 0", "M0 -3V3M-2 -1 0 1 2 -1", "M-2 3 0 -3 2 3", "M0 -3V3M-2 -3 2 3"};
    for (int k = 0; k < 16; k++) {
      const Pt p = polar(50, 50, 40.6, -90 + 22.5 * k);
      runes += xf(glyphs[k % 5], place(p.x, p.y, float(22.5 * k)));
    }
    v.push_back(G::XS(runes, 1.4f));
    std::string ribs;
    for (int k = 0; k < 8; k++)
      ribs += blade(polar(50, 50, 13, -90 + 45 * k), polar(50, 50, 20, -90 + 45 * k), polar(50, 50, 27, -90 + 45 * k), polar(50, 50, 34, -90 + 45 * k), 4.6, 0.3, 0.5, 1);
    v.push_back(G::X(ribs));
    v.push_back(G::XS(circ(50, 50, 13.5), 1.6f));
    v.push_back(G::X(star(50, 50, 9, 3.6, 8, -90)));
    c.add("race-dwarves-shield", "Щит с рунами", fit(v));
  }
  // Кружка эля: кружка с обручами и узорным поясом, изогнутая ручка, шапка пены, по стенке стекают острые подтёки.
  {
    std::vector<L> v;
    const std::string handle = "M66 34C77 32 85 38 85.5 50C86 62 79 72 67 77L65 70C73 66 77.5 59 77.5 51C77.5 43 73 40 66 41Z";
    v.push_back(G::F(handle));
    const std::string mug = "M18 30H66L63 92H21Z";
    over(v, mug, 1.4);
    v.push_back(G::XS("M18.6 40H65.4M20.4 82H63.6M19.4 49H64.6M20 74H64", 1.4f));
    v.push_back(G::XS("M21 66 26.5 56 32 66 37.5 56 43 66 48.5 56 54 66 59.5 56 63 62.5", 1.5f));
    std::string foam = "M16 32C13 27 16 21 22 21C24 16 31 14 36 17C40 12 48 12 51 16C55 13 62 14 64 19C68 20 69 25 67 30C66 32 64 33 62 33H20C18 33 17 33 16 32Z";
    foam += spike({22, 31}, {22, 36}, {21.5, 41}, {22.5, 47}, 5.4, 1.2) + spike({57, 31}, {57, 37}, {57.5, 42}, {56.5, 48}, 5, 1.2) +
            spike({39, 31}, {39, 35}, {39.5, 38}, {39, 42}, 4.4, 1.2);
    over(v, foam, 1.4);
    v.push_back(G::XS(curve({21, 25.5}, {24, 23}, {28, 22.5}, {31, 23.5}) + curve({39, 19}, {42, 16.5}, {46, 16}, {49, 17.5}), 1.3f));
    c.add("race-dwarves-ale", "Кружка эля", fit(v));
  }
}

// ---------------------------------------------------------------- гоблины
void goblins(Catalog& c) {
  c.group("Гоблины", EmblemSection::Races);
  // Голова гоблина: длинные уши-лезвия (одно надорвано), раскосые глаза, крючковатый нос, кривой оскал с клыками.
  {
    std::vector<L> v;
    const std::string ears = sym(blade({36, 44}, {24, 36}, {12, 26}, {2, 14}, 15, 0.3, 0.5, 1.2));
    v.push_back(G::F(ears));
    v.push_back(G::X("M87.5 23.5 92.5 22.5 89 27.5Z"));
    v.push_back(G::XS(sym(curve({34, 41}, {25, 35}, {16, 27}, {8, 18}, 0.05, 0.9)), 1.3f));
    const std::string head = symFull("M50 18C42 18 35.5 23 34 31L32.5 44C32.5 56 37 67 44 74L50 78H50.5V18Z");
    v.push_back(G::F(head + "M50 40C54 45 58.5 51 58.5 57.5C58.5 61 55 62.5 51.5 61C49 60 48 56 48 51Z"));
    v.push_back(G::X(sym("M36.5 43.5 46.5 46.5 38.5 48.5Z")));
    v.push_back(G::XS(sym(curve({35.5, 39}, {39, 38.5}, {43, 39.5}, {47, 42.5})) + curve({50, 41}, {53.5, 46}, {57, 52}, {57.5, 57.5}) + "M57.5 57.5C56 60.5 53 61 50.5 59.5",
                      1.3f));
    v.push_back(G::X("M38.5 65.5 61.5 64 60 67.5 40 68.5Z"));
    v.push_back(G::F("M42 68.6 43.6 63.5 45.2 68.4ZM54.5 68 56 63.3 57.4 67.8Z"));
    v.push_back(G::XS(sym(curve({42, 22}, {44.5, 25}, {47, 26.5}, {50, 27})) + "M50 18V27", 1.3f));
    c.add("race-goblins-head", "Голова гоблина", fit(v));
  }
  // Мешок золота: завязанный мешок с острыми краями горловины, клеймо-монета, рассыпанные монеты.
  {
    std::vector<L> v;
    auto coin = [](double x, double y, double rx, double ry) { return ell(x, y, rx, ry); };
    const std::string coins = coin(12, 90, 9, 3.6) + coin(88, 91, 9, 3.6) + coin(85, 83, 8, 3.2);
    v.push_back(G::F(coins));
    v.push_back(G::XS(curve({6, 89}, {9, 88}, {13, 88}, {16, 88.5}) + curve({82, 90}, {85, 89}, {89, 89}, {92, 89.5}) + curve({80, 82}, {83, 81}, {86, 81}, {89, 81.5}), 1.2f));
    const std::string sack = "M50 30C38 30 22 46 20 66C18 84 30 95 50 95C70 95 82 84 80 66C78 46 62 30 50 30Z";
    over(v, sack, 1.6);
    v.push_back(G::XS(curve({30, 46}, {26, 53}, {25, 60}, {25.5, 67}) + curve({66, 38}, {71, 44}, {74, 50}, {75, 56}), 1.4f));
    const std::string neck = "M40 32C36 26 35 18 38 10L44 16 47 6 50 14 53 6 56 16 62 10C65 18 64 26 60 32Z";
    over(v, neck, 1.3);
    v.push_back(G::XS("M40.5 28H59.5M41 32H59", 1.4f));
    v.push_back(G::X(circ(50, 64, 14)));
    v.push_back(G::F(circ(50, 64, 11.5)));
    v.push_back(G::X(star(50, 64, 8, 3.2, 5, -90)));
    c.add("race-goblins-sack", "Мешок золота", fit(v));
  }
  // Бомба с фитилём: чугунный шар с обручем и черепом-клеймом, горлышко, горящий фитиль с искрой-звездой.
  {
    std::vector<L> v;
    const std::string ball = circ(44, 60, 34);
    v.push_back(G::F(ball + xf(rrect(-9, -8, 18, 16, 2), place(66, 34, 40))));
    v.push_back(G::XS(xf(arc(0, 0, 34, 200, 340), Affine::translate(44, 60) * Affine::scale(1, 0.32f)), 1.6f));
    v.push_back(G::XS(xf("M-9 -3.5H9", place(66, 34, 40)), 1.4f));
    v.push_back(G::XS(arc(44, 60, 28, 200, 245), 1.6f));
    const std::string skull = "M44 49C50 49 54 53 54 58C54 61.5 52.5 64 50 65.5V69H38V65.5C35.5 64 34 61.5 34 58C34 53 38 49 44 49Z";
    v.push_back(G::X(skull));
    const std::string socket = xf("M0 -2.4C2 -2.4 3 -1 3 0.6C3 2 1.6 2.6 0 2.6C-1.6 2.6 -3 2 -3 0.6C-3 -1 -2 -2.4 0 -2.4Z", place(40, 58.5, 0));
    v.push_back(G::F(socket + mirrorX(socket, 44) + "M44 61 45.6 64H42.4Z" + "M40.5 66V69M44 66V69M47.5 66V69"));
    v.push_back(G::F(taper({69, 28}, {72, 20}, {77, 16}, {83, 17}, 3.4, 2.4)));
    v.push_back(G::F(star(87, 13, 11, 2.6, 8, 0) + star(87, 13, 6, 2.6, 8, 22.5)));
    c.add("race-goblins-bomb", "Бомба с фитилём", fit(v));
  }
  // Кривой нож: широкий изогнутый клинок с зубьями пилы по обуху и гравированным долом, обмотанная рукоять, кольцо.
  {
    std::vector<L> v;
    std::vector<Pt> back, edge;
    for (int i = 0; i <= 48; i++) {
      const double t = i / 48.0;
      const Pt p = bzP({50, 62}, {48, 42}, {60, 20}, {42, 2}, t);
      const double w = 19 * std::pow(1 - t, 0.7) + 0.6, saw = (i % 4 == 2 && t > 0.12 && t < 0.78) ? 3 : 0;
      back.push_back(P(p.x - w * 0.42 - saw, p.y));
      edge.push_back(P(p.x + w * 0.58 + 6 * std::sin(kPi * std::pow(t, 0.8)), p.y));
    }
    std::vector<Pt> bl = back;
    for (size_t k = edge.size(); k-- > 0;) bl.push_back(edge[k]);
    over(v, polyPath(bl), 1.4);
    v.push_back(G::XS(curve({50, 56}, {49.5, 42}, {57, 26}, {48, 10}, 0, 0.9), 1.4f));
    const std::string guard = "M33 63.5C40 61 60 61 67 63.5L65 68C58 66 42 66 35 68Z";
    const std::string hilt = rrect(43, 67, 14, 19, 3) + "M50 84C56 84 60 88 60 92.5C60 97 55.5 99.5 50 99.5C44.5 99.5 40 97 40 92.5C40 88 44 84 50 84Z";
    over(v, guard + hilt, 1.4);
    v.push_back(G::XS("M44 71.5 56 75M44 76.5 56 80M44 81.5 56 85", 1.3f));
    v.push_back(G::X(ell(50, 92.3, 3.6, 3.4)));
    c.add("race-goblins-knife", "Кривой нож", fit(moved(v, rotAt(34, 50, 50))));
  }
}

// ---------------------------------------------------------------- демоны
void demonsRace(Catalog& c) {
  c.group("Демоны", EmblemSection::Races);
  // Рогатая корона: обод с самоцветами и клыками понизу, вместо зубцов — пять изогнутых рогов с кольцами-насечками.
  {
    std::vector<L> v;
    struct Horn {
      Pt p0, c1, c2, p1;
      double w;
    };
    const Horn hs[] = {{{50, 64}, {46, 44}, {56, 24}, {49, 2}, 15}, {{36, 64}, {29, 50}, {20, 40}, {23, 16}, 13.5}, {{23, 65}, {14, 58}, {5, 50}, {4, 30}, 11.5}};
    std::string horns, rings;
    for (int i = 0; i < 3; i++) {
      const Horn& h = hs[i];
      const std::string d = spike(h.p0, h.c1, h.c2, h.p1, h.w, 1.15);
      horns += i ? sym(d) : d;
      for (double t : {0.3, 0.5}) {
        const CurvePt r = bezier(h.p0, h.c1, h.c2, h.p1, t);
        const double rw = h.w * std::pow(1 - t, 1.15) * 0.5;
        const std::string cut = "M" + n2(r.x + r.nx * rw, r.y + r.ny * rw) + "Q" + n2(r.x - r.ny * 1.6, r.y + r.nx * 1.6 + 1) + " " + n2(r.x - r.nx * rw, r.y - r.ny * rw);
        rings += i ? sym(cut) : cut;
      }
    }
    v.push_back(G::F(horns));
    v.push_back(G::XS(rings, 1.3f));
    const std::string band = "M14 62H86V80H14Z" + std::string("M20 79 25 91 30 79ZM45 79 50 93 55 79ZM70 79 75 91 80 79Z");
    over(v, band, 1.6);
    v.push_back(G::XS(inset("M14 62H86V80H14Z", 2.4), 1.3f));
    v.push_back(G::X("M50 66 54 71 50 76 46 71Z" + sym("M33 68 36 71 33 74 30 71Z")));
    c.add("race-demons-crown", "Рогатая корона", fit(v));
  }
  // Пентакль ада: круглая печать в двойном ободе с перевёрнутой звездой, охваченная языками пламени.
  {
    std::vector<L> v;
    std::string flames;
    for (int k = 0; k < 14; k++) {
      const double a = -90 + 360.0 / 14 * k, up = 0.5 - 0.5 * std::sin(a * kPi / 180), len = 9 + 15 * up;
      const double side = k % 2 ? 1 : -1;
      flames += blade(polar(50, 55, 31, a), polar(50, 55, 34 + len * 0.35, a + 9 * side), polar(50, 55, 34 + len * 0.7, a - 7 * side),
                      polar(50, 55, 34 + len, a + 8 * side), 9, 0.22, 0.6, 1.8, 24);
    }
    v.push_back(G::F(flames + circ(50, 55, 34)));
    v.push_back(G::XS(circ(50, 55, 30.5), 1.6f));
    v.push_back(G::XS(circ(50, 55, 27), 1.3f));
    std::string star5;
    for (int i = 0; i < 5; i++) star5 += (i ? "L" : "M") + pt(50, 55, 25.5, 90 + 144 * i);
    v.push_back(G::XS(star5 + "Z", 2.2f));
    c.add("race-demons-pentacle", "Пентакль ада", fit(v));
  }
  // Крылатые рога: перепончатые крылья, рога-лира, между ними — огненное око с вертикальным зрачком и язык пламени.
  {
    std::vector<L> v;
    std::string fingers;
    const std::string wings = batWing(50, 52, 48, &fingers);
    v.push_back(G::F(sym(wings)));
    v.push_back(G::XS(sym(fingers), 1.4f));
    const std::string horns = sym(spike({45, 54}, {32, 45}, {27, 26}, {38, 4}, 11, 1.2));
    over(v, horns, 1.4);
    std::string rings;
    for (double t : {0.25, 0.42, 0.58}) {
      const CurvePt r = bezier({45, 54}, {32, 45}, {27, 26}, {38, 4}, t);
      const double rw = 11 * (1 - t) * 0.55;
      rings += sym("M" + n2(r.x + r.nx * rw, r.y + r.ny * rw) + "L" + n2(r.x - r.nx * rw, r.y - r.ny * rw - 1));
    }
    v.push_back(G::XS(rings, 1.3f));
    const std::string eye = "M50 50C57 52 61 57 61 63C61 69 57 74 50 76C43 74 39 69 39 63C39 57 43 52 50 50Z" +
                            blade({50, 74}, {48, 80}, {52, 86}, {50, 96}, 9, 0.3, 0.6, 1.4);
    over(v, eye, 1.6);
    v.push_back(G::X("M50 55C52.5 58 53.5 61 53.5 63C53.5 65 52.5 68 50 71C47.5 68 46.5 65 46.5 63C46.5 61 47.5 58 50 55Z"));
    v.push_back(G::F("M50 58.5C51 60.5 51.4 62 51.4 63C51.4 64 51 65.5 50 67.5C49 65.5 48.6 64 48.6 63C48.6 62 49 60.5 50 58.5Z"));
    c.add("race-demons-wings", "Крылатые рога", fit(v));
  }
  // Адский клинок: волнистый клинок-фламберг с долом, гарда — рогатый череп, шип на навершии.
  {
    std::vector<L> v;
    const std::string bladeD = ribbon([](double t) { return P(50, 60 - 58 * t); },
                                      [](double t) { const double w = 21 + 4.5 * std::sin(t * 7 * kPi); return t < 0.78 ? w : w * std::pow((1 - t) / 0.22, 0.9); }, 70);
    over(v, bladeD, 1.4);
    v.push_back(G::XS("M50 30V56", 1.3f));
    const std::string horns = sym(spike({44, 65}, {31, 67}, {18, 61}, {12, 41}, 15, 1.15));
    const std::string skull = "M50 58C56.5 58 60.5 62 60.5 67.5C60.5 71.5 58.5 74.5 55.5 76V80H44.5V76C41.5 74.5 39.5 71.5 39.5 67.5C39.5 62 43.5 58 50 58Z";
    over(v, horns + skull, 1.4);
    std::string rings;
    for (double t : {0.3, 0.5}) {
      const CurvePt r = bezier({44, 65}, {31, 67}, {18, 61}, {12, 41}, t);
      const double rw = 15 * std::pow(1 - t, 1.15) * 0.5;
      rings += sym("M" + n2(r.x + r.nx * rw, r.y + r.ny * rw) + "L" + n2(r.x - r.nx * rw, r.y - r.ny * rw));
    }
    v.push_back(G::XS(rings, 1.2f));
    v.push_back(G::X(sym("M43 65.5 48.5 67 47.5 71 43.5 69.5Z") + "M50 71.5 51.6 74.5H48.4Z"));
    v.push_back(G::XS("M47 76.5V80M50 76.5V80M53 76.5V80", 1.2f));
    const std::string hilt = rrect(44.5, 80, 11, 10, 1.5) + "M50 87.5 57 92.5 50 99.5 43 92.5Z";
    over(v, hilt, 1.3);
    v.push_back(G::XS("M44.5 83.5H55.5M44.5 86.5H55.5", 1.2f));
    c.add("race-demons-blade", "Адский клинок", fit(v));
  }
}

// ---------------------------------------------------------------- дриады
void dryads(Catalog& c) {
  c.group("Дриады", EmblemSection::Races);
  // Дева с волосами-листьями: лик с сомкнутыми веками, венец из острых листьев, пряди-листья до плеч, плечи.
  {
    std::vector<L> v;
    std::string ribs;
    for (int k = 0; k <= 8; k++) {
      const double a = -180 + 22.5 * k, len = 37 - std::abs(k - 4) * 1.2;
      const Pt b = polar(50, 40, 10, a), t = polar(50, 40, len, a);
      over(v, leaf(b, t, 10, 0, 0.45, 1.3), 1.0);
      ribs += leafRib(b, t, 0, 0.45, 0.82);
    }
    v.push_back(G::XS(ribs, 1.2f));
    std::string lr;
    for (auto [bx, by, tx, ty, w] : {std::tuple{40.0, 46.0, 22.0, 66.0, 10.0}, std::tuple{40.0, 52.0, 25.0, 80.0, 10.0}, std::tuple{42.0, 58.0, 33.0, 92.0, 9.0}}) {
      over(v, sym(leaf(P(bx, by), P(tx, ty), w, 2, 0.45, 1.3)), 1.0);
      lr += leafRib(P(bx, by), P(tx, ty), 2, 0.35, 0.82);
    }
    v.push_back(G::XS(sym(lr), 1.2f));
    const std::string body = "M28 98C30 86 38 78 50 76C62 78 70 86 72 98Z";
    const std::string face = "M50 28C57 28 61 33.5 61 40.5C61 49 56.5 56 50 59C43.5 56 39 49 39 40.5C39 33.5 43 28 50 28Z";
    over(v, body + rrect(46, 56, 8, 22, 2) + face, 1.4);
    v.push_back(G::XS(sym(curve({42, 41.5}, {43.5, 43.4}, {46, 43.4}, {47.6, 41.5})) + curve({50, 43}, {49.4, 46.5}, {49.2, 48.5}, {50.4, 50}) + "M47.6 53.2H52.4" +
                          curve({38, 87}, {44, 90}, {56, 90}, {62, 87}),
                      1.2f));
    c.add("race-dryads-maiden", "Дева с волосами-листьями", fit(v));
  }
  // Цветок: шесть острых лепестков с жилками, сердцевина с тычинками, изогнутый стебель и два листа.
  {
    std::vector<L> v;
    v.push_back(G::F(taper({50, 54}, {47, 68}, {53, 82}, {49, 98}, 4.4, 3)));
    for (auto [b, t, bend] : {std::tuple{P(49, 78), P(18, 62), 3.0}, std::tuple{P(51, 86), P(83, 72), -3.0}}) {
      over(v, leaf(b, t, 13, bend, 0.42, 1.3), 1.2);
      v.push_back(G::XS(leafRib(b, t, bend, 0.15, 0.82), 1.3f));
    }
    std::string ribs;
    for (int k = 0; k < 6; k++) {
      const double a = -90 + 60 * k + (k % 2 ? 0 : 0);
      const Pt b = polar(50, 36, 6, a), t = polar(50, 36, 34, a);
      over(v, leaf(b, t, 16, 0, 0.42, 1.25), 1.1);
      ribs += leafRib(b, t, 0, 0.42, 0.85);
    }
    v.push_back(G::XS(ribs, 1.3f));
    v.push_back(G::X(circ(50, 36, 9)));
    v.push_back(G::F(circ(50, 36, 6.6)));
    std::string stamens;
    for (int k = 0; k < 6; k++) {
      const double a = -60 + 60 * k;
      stamens += taper(polar(50, 36, 6, a), polar(50, 36, 9, a), polar(50, 36, 12, a), polar(50, 36, 15, a), 1.6, 1.4) +
                 blade(polar(50, 36, 13.5, a), polar(50, 36, 15, a), polar(50, 36, 16.5, a), polar(50, 36, 19, a), 3.6, 0.5, 0.6, 1);
    }
    v.push_back(G::F(stamens));
    v.push_back(G::X(circ(50, 36, 2)));
    c.add("race-dryads-flower", "Цветок", fit(v));
  }
  // Лоза-кольцо: два переплетённых стебля венком, листья плюща наружу и внутрь, завитки-усики, цветок наверху.
  {
    std::vector<L> v;
    const std::string ivy = "M0 0C-3 -1.5 -7.5 -3.5 -11 -8L-6.5 -9.5 -9 -15 -3.5 -13 0 -21 3.5 -13 9 -15 6.5 -9.5 11 -8C7.5 -3.5 3 -1.5 0 0Z";
    auto stemAt = [](int s, double a) { return polar(50, 52, 31 + (s ? -2.8 : 2.8) * std::sin((a + 66) * kPi / 39), a); };
    std::string stems;
    for (int s = 0; s < 2; s++)
      stems += ribbon([&](double t) { return stemAt(s, -66 + 312 * t); }, [](double t) { return 4.2 * std::min(1.0, std::min(t, 1 - t) * 10 + 0.25); }, 140);
    v.push_back(G::F(stems));
    std::string leaves, ribs, curls;
    for (int k = 0; k < 8; k++) {
      const double a = -66 + 19.5 + 39 * k;
      const bool out = k % 2 == 0;
      const Pt p = stemAt(out ? 0 : 1, a);
      const Affine lm = place(p.x, p.y, float(a + (out ? 90 + 35 : -90 - 35)), 1.02f);
      leaves += xf(ivy, lm);
      ribs += xf("M0 -3V-15M0 -6 -5.5 -10M0 -6 5.5 -10", lm);
      const Pt q = stemAt(out ? 1 : 0, a);
      const double a0 = a + (out ? 180 : 0);
      curls += ribbon([&](double t) { return polar(q.x + 4 * std::cos(a0 * kPi / 180), q.y + 4 * std::sin(a0 * kPi / 180), 3.8 * (1 - 0.7 * t), a0 + 180 + 330 * t); },
                      [](double t) { return 1.8 * (1 - 0.5 * t); }, 30);
    }
    v.push_back(G::F(curls));
    over(v, leaves, 1.1);
    v.push_back(G::XS(ribs, 1.2f));
    std::string flower;
    for (int k = 0; k < 5; k++) flower += leaf(P(50, 15), polar(50, 15, 14, -90 + 72 * k), 10, 0, 0.45, 1.2);
    over(v, flower, 1.3);
    v.push_back(G::X(circ(50, 15, 3)));
    v.push_back(G::F(circ(50, 15, 1.4)));
    c.add("race-dryads-vine", "Лоза-кольцо", fit(v));
  }
  // Олень в листве: голова оленя в профиль на длинной шее, рога-ветви с листьями, внизу — лиственная ветвь.
  {
    std::vector<L> v;
    const std::string antlers = spike({57, 31}, {59, 21}, {64, 13}, {73, 7}, 4, 0.8) + spike({60, 22}, {57, 17}, {55, 12}, {55, 6}, 3, 0.8) +
                                spike({66, 12}, {67, 8}, {67, 5}, {65, 1.5}, 2.6, 0.8) + spike({52, 32}, {48, 25}, {43, 20}, {36, 17}, 4, 0.8) +
                                spike({46, 22}, {42, 21}, {38, 22}, {34, 25}, 2.8, 0.8);
    v.push_back(G::F(antlers));
    std::string ribs;
    for (auto [bx, by, tx, ty, bend] : {std::tuple{72.5, 7.5, 84.0, 4.0, 1.0}, std::tuple{55.0, 7.0, 49.0, 1.0, -1.0}, std::tuple{36.5, 17.5, 27.0, 12.0, 1.0},
                                        std::tuple{34.5, 25.0, 25.0, 26.0, -1.0}, std::tuple{65.5, 2.5, 73.0, 1.0, -1.0}}) {
      over(v, leaf(P(bx, by), P(tx, ty), 6.5, bend, 0.45, 1.3), 0.9);
      ribs += leafRib(P(bx, by), P(tx, ty), bend, 0.25, 0.8);
    }
    v.push_back(G::XS(ribs, 1.2f));
    const std::string head = "M12 54C12 51.5 13.6 50 16 49.6L33 42C37.5 36.5 44 33 51 33C57 33 61.5 36 63.5 40.5L67 48.5"
                             "C71 58.5 77.5 74 82 97H55C55 88 53 81 49 75.5C45 70.5 40 67 34 65C29 63.5 24 61 19.5 59C15 58.5 12 57 12 54Z";
    const std::string ear = blade({63, 38}, {68, 34}, {74, 31}, {80, 30}, 8, 0.45, 0.6, 1.1);
    over(v, head + ear, 1.4);
    v.push_back(G::X(xf("M-3.4 0C-2 -1.8 2 -1.8 3.6 0.4C1.6 1.6 -1.6 1.6 -3.4 0Z", place(41.5, 45, -16)) + ell(14.6, 52.4, 1.3, 0.9)));
    v.push_back(G::XS("M13.5 56.5Q18 58.5 23 58.5M47 76.5C54 72.5 60 72.5 66 76.5M66 37.5 74.5 33M31 44C34 46 38 46.5 41 45.5" + std::string("M57 82 60 97M63 80 68 97"), 1.3f));
    v.push_back(G::F(taper({3, 97}, {9, 89}, {17, 81}, {30, 77}, 3.6, 2)));
    for (auto [b, t, bend] : {std::tuple{P(9, 89), P(2, 78), 1.0}, std::tuple{P(15, 84), P(10, 72), 1.0}, std::tuple{P(22, 80), P(21, 68), 1.0},
                              std::tuple{P(13, 87), P(25, 92), -1.0}, std::tuple{P(21, 82), P(33, 86), -1.0}})
      over(v, leaf(b, t, 7, bend, 0.45, 1.3), 0.9);
    c.add("race-dryads-deer", "Олень в листве", fit(v));
  }
}

// Египетское крыло вдоль оси x (от корня в 0 к концу L, вниз — до H): сверху полоса кроющих с чешуйками, под ней — длинные
// маховые лентами, каждая короче предыдущей, концы чуть загнуты вниз. mirror — пара.
void egyptWing(std::vector<L>& v, const Affine& m, double L, double H, bool mirror) {
  auto mm = [&](const std::string& d) { return mirror ? sym(xf(d, m)) : xf(d, m); };
  const int nf = 4;
  const double fw = (H - H * 0.3) / nf * 1.35;
  for (int r = nf - 1; r >= 0; r--) {
    const double y = H * 0.3 + (H - H * 0.3) * (r + 0.5) / nf, len = L * (1 - 0.13 * r);
    const std::string f = blade(P(0, y), P(len * 0.4, y), P(len * 0.8, y + 1), P(len, y + 2.5 + r), fw, 0.75, 0.3, 0.9);
    v.push_back(G::XS(mm(f), 2.2f));
    v.push_back(G::F(mm(f)));
  }
  const std::string band = ribbon([&](double t) { return P(L * 0.97 * t, H * 0.16 - 1.2 * std::sin(kPi * t)); }, [&](double t) { return H * 0.34 * (1 - 0.75 * t); }, 30);
  v.push_back(G::XS(mm(band), 2.2f));
  v.push_back(G::F(mm(band)));
  v.push_back(G::XS(mm("M2 " + n(H * 0.16) + "C" + n2(L * 0.3, H * 0.16 - 1) + " " + n2(L * 0.6, H * 0.16 - 1.5) + " " + n2(L * 0.8, H * 0.16 - 1.5)), 1.2f));
}

// ---------------------------------------------------------------- ибисианцы
void ibisians(Catalog& c) {
  c.group("Ибисианцы", EmblemSection::Races);
  // Голова ибиса: профиль с длинным изогнутым клювом, полосатый немес, лунный диск в полумесяце, ожерелье-ускх.
  {
    std::vector<L> v;
    const std::string usekh = "M30 84C44 90 66 92 92 86L94 96C70 100 48 99 28 93Z";
    v.push_back(G::F(usekh));
    v.push_back(G::XS("M31 88.5C46 94 68 95.5 93 90.5", 1.3f));
    const std::string nemes = "M47 27C53 21 64 20 71 26C77 32 79 44 79 56L84 86H55L57 72C57 62 55 54 51 47Z";
    over(v, nemes, 1.4);
    v.push_back(G::XS("M59 58H80.5M58.5 65H81.5M58 72H82.5M57 79H83.5" + std::string("M70 27C74 33 76 42 76 52"), 1.4f));
    const std::string head = "M52 25C59 25 64 30 64 37C64 43 60 47.5 54 48.5L47 48C43.5 46 41.5 42 42 38C42.5 31 46.5 25 52 25Z";
    const std::string beak = spike({45, 40}, {33, 44}, {19, 54}, {6, 76}, 8, 1.05);
    over(v, head + beak, 1.4);
    v.push_back(G::X("M47 34.5C49.5 32.5 53.5 32.5 56 34.5C53.5 36.5 49.5 36.5 47 34.5Z"));
    v.push_back(G::XS("M56 34.5 61 35.5M51 39.5C51 42 50 44 48 45.5", 1.3f));
    const std::string moon = circ(58, 9, 7.5) + "M44 13A14 14 0 0 0 72 13A14 10 0 0 1 44 13Z";
    over(v, moon, 1.3);
    c.add("race-ibisians-head", "Голова ибиса", fit(v));
  }
  // Крылатый анкх: анкх с двойным контуром, от перекладины — раскинутые египетские крылья в три яруса.
  {
    std::vector<L> v;
    egyptWing(v, Affine{-1, 0, 0, 1, 42, 37}, 40, 26, true);
    const std::string ankh = "M50 4C58.5 4 64 11 64 20C64 27 60.5 33 55.5 38H72V48H55.5L58.5 96H41.5L44.5 48H28V38H44.5C39.5 33 36 27 36 20C36 11 41.5 4 50 4Z";
    over(v, ankh, 1.6);
    v.push_back(G::X("M50 11.5C54 11.5 56.5 15 56.5 20C56.5 25.5 53.8 30 50 34C46.2 30 43.5 25.5 43.5 20C43.5 15 46 11.5 50 11.5Z"));
    v.push_back(G::XS(inset("M28 38H72V48H55.5L58.5 96H41.5L44.5 48H28Z", 2.4), 1.2f));
    c.add("race-ibisians-ankh", "Крылатый анкх", fit(v));
  }
  // Свиток и перо: развёрнутый папирус на двух валиках со знаками-иероглифами, наискось — тростниковое перо.
  {
    std::vector<L> v;
    const std::string sheet = "M18 28H82V72H18Z";
    const std::string rolls = rrect(6, 22, 15, 56, 7) + rrect(79, 22, 15, 56, 7);
    v.push_back(G::F(sheet));
    v.push_back(G::XS("M22 32.5H78M22 67.5H78" + circ(30, 50, 6) + "M61 50C64 46 71 46 74 50C71 54 64 54 61 50Z" + "M67.5 54V60", 1.5f));
    v.push_back(G::X(circ(30, 50, 1.8) + circ(67.5, 50, 1.6)));
    v.push_back(G::XS(xf("M50 41.5C52.5 41.5 54 43.2 54 45.5C54 47.6 52.2 49.3 50 51C47.8 49.3 46 47.6 46 45.5C46 43.2 47.5 41.5 50 41.5ZM45 51.5H55M50 51.5V60", Affine::translate(-7, 0)), 1.5f));
    over(v, rolls, 1.4);
    v.push_back(G::XS("M13.5 26V74M86.5 26V74", 1.3f));
    const std::string reed = spike({86, 4}, {66, 30}, {44, 62}, {24, 96}, 6.5, 0.5);
    over(v, reed, 1.5);
    v.push_back(G::XS(curve({82, 9}, {66, 30}, {48, 56}, {32, 82}, 0, 0.9), 1.2f));
    c.add("race-ibisians-scroll", "Свиток и перо", fit(v));
  }
  // Глаз-уджат: бровь-лезвие, миндалевидный глаз со зрачком, удлинённый уголок, капля-слеза и завиток.
  {
    std::vector<L> v;
    const std::string brow = blade({10, 24}, {34, 13}, {66, 12}, {94, 20}, 8, 0.35, 0.5, 0.8);
    const std::string eye = "M14 46C26 32 50 28 68 36C76 40 84 42 95 41L93 47.5C83 48.5 75 49 68 52C54 60 30 61 14 46Z";
    const std::string drop = "M33 57H42L43 82C43 87.5 38.5 89.5 35 86.5L33 84.5Z";
    const std::string tail = taper({50, 57}, {56, 68}, {63, 80}, {74, 84}, 7, 4.6) +
                             ribbon([](double t) { return polar(72, 76, 8.5 * (1 - 0.6 * t), 90 - 290 * t); }, [](double t) { return 4.6 - 1.8 * t; });
    over(v, brow + eye + drop + tail, 1.3);
    v.push_back(G::X("M24 45C34 37 50 35 62 41C50 50.5 35 52 24 45Z"));
    v.push_back(G::F(circ(44, 43.6, 7)));
    v.push_back(G::X(circ(42, 41.6, 1.6)));
    c.add("race-ibisians-wedjat", "Глаз-уджат", fit(v));
  }
}

// Голова шакала в профиль (влево) в локальных координатах: кончик носа в (0, 0), длинная морда, два высоких уха.
const char* kJackal = "M0 0C0 -1.6 1 -2.7 2.7 -3L20 -6.2C23 -9.5 26.5 -11.5 30.5 -12.5L31.5 -31 37.5 -15.2 39.5 -14.8 43 -32 46 -12.5"
                      "C48 -7.5 47.5 -2.5 44.5 1.5C41.5 5.5 36.5 8 30.5 8.5L4 3.2C1.6 2.8 0 1.7 0 0Z";
// Вырезы на голове шакала: глаз, рот, внутренность уха.
const char* kJackalCut = "M24.5 -6.6 32 -7.6 30 -4.4Z";
const char* kJackalLines = "M3 0.6 21 3.4M38.5 -27 41.6 -15.6";

// ---------------------------------------------------------------- канидисы
void canidis(Catalog& c) {
  c.group("Канидисы", EmblemSection::Races);
  // Голова шакала: профиль с высокими ушами, полосатый немес, ожерелье-ускх.
  {
    std::vector<L> v;
    const std::string usekh = "M34 82C48 89 70 91 92 85L94 96C70 100 48 98 30 91Z";
    v.push_back(G::F(usekh));
    v.push_back(G::XS("M35 86.5C50 93 71 94.5 93 90", 1.3f));
    const std::string nemes = "M52 34C58 29 68 29 74 35C79 41 81 51 81 61L86 88H58L60 75C60 65 58 57 54 50Z";
    over(v, nemes, 1.4);
    v.push_back(G::XS("M62 62H82.5M61.5 69H83.5M61 76H84.5M60 83H85.5M73 36C77 42 79 51 79 59", 1.4f));
    const Affine hm = place(5, 50, -6, 1.45f);
    over(v, xf(kJackal, hm), 1.5);
    v.push_back(G::X(xf(kJackalCut, hm)));
    v.push_back(G::XS(xf(kJackalLines, hm), 1.3f));
    c.add("race-canidis-head", "Голова шакала", fit(v));
  }
  // Скипетры-уас: два скрещённых жезла с головой зверя и раздвоенной пятой, над ними — солнечный диск в ободе.
  {
    const std::string was = "M44 21L45.5 7 63 9 66.5 16.5 55.5 16 54.5 21Z" + rrect(45.5, 19, 9, 69, 2) + "M45.5 86H54.5L61 97 54.5 92.5 50 97 45.5 92.5 39 97Z";
    std::vector<L> v = crossed(was, "", "M45.5 30H54.5M45.5 34H54.5M45.5 77H54.5", 1.3f, 26, 50, 55, 1.0f, 2.4f);
    over(v, circ(50, 12, 11), 1.6);
    v.push_back(G::XS(circ(50, 12, 7.5), 1.4f));
    c.add("race-canidis-was", "Скипетры-уас", fit(v));
  }
  // Страж с копьём: шакалоглавый воин в профиль шагает, копьё у плеча, щит с ромбом, схенти с поясом.
  {
    std::vector<L> v;
    v.push_back(G::F(taper({20.5, 98}, {20.5, 60}, {20.5, 30}, {20.5, 13}, 3.2, 3) + blade({20.5, 15}, {20.5, 9}, {20.5, 5}, {20.5, 0.5}, 7, 0.45, 0.5, 1.2)));
    const std::string body = "M38 30H52L55.5 34 57.5 54H36.5L37.5 34Z" + std::string("M35 52H59L62.5 69H31.5Z") +
                             taper({41, 66}, {38, 76}, {33.5, 86}, {29, 94}, 8, 6) + taper({53, 66}, {55.5, 76}, {58, 86}, {61.5, 94}, 8, 6) +
                             "M22 93.5H35L35.5 98H21.5ZM57 93.5H70L70.5 98H56.5Z" + taper({40, 33}, {34, 39}, {28, 41}, {22, 41}, 6.4, 5);
    over(v, body, 1.4);
    v.push_back(G::XS("M35.5 53.5H58.5M40 57.5 38 68M47 57.5V68.5M54 57.5 56 68M38 39H56", 1.4f));
    const Affine hm = place(24, 23, -4, 0.72f);
    over(v, xf(kJackal, hm), 1.3);
    v.push_back(G::X(xf(kJackalCut, hm)));
    v.push_back(G::XS(xf(kJackalLines, hm), 1.2f));
    const std::string shield = "M58 30H75C77 30 78 31 78 33V56C78 64 73 71 66.5 73C60 71 55 64 55 56V33C55 31 56 30 58 30Z";
    over(v, shield, 1.5);
    v.push_back(G::XS(inset(shield, 2.6), 1.3f));
    v.push_back(G::X("M66.5 40 70.5 47.5 66.5 55 62.5 47.5Z"));
    c.add("race-canidis-guard", "Страж с копьём", fit(v));
  }
  // Шакал на ларце: лежащий шакал с поднятой головой и высокими ушами на ковчеге с карнизом и знаками.
  {
    std::vector<L> v;
    const std::string chest = "M8 60H92L89 66H11Z" + rrect(13, 66, 74, 24, 0.5) + rrect(9, 90, 82, 6, 0.5);
    v.push_back(G::F(chest));
    v.push_back(G::XS("M12 69.5H88" + std::string("M19 74H81V86H19Z"), 1.4f));
    v.push_back(G::X(circ(50, 80, 3.6) + sym(blade({45, 80}, {39, 78.5}, {32, 78.5}, {23, 77.5}, 4.4, 0.25, 0.5, 1.2))));
    const std::string body = "M26 58C26 49 31 43 40 40C50 37 62 37 72 40C80 43 85 50 83 58Z" + taper({81, 55}, {87, 58}, {89, 64}, {87, 74}, 5.5, 3.5) +
                             "M34 58H12C9.5 58 9 55 11 53.5H34ZM35 53.5H16C13.5 53.5 13 50.5 15 49H37Z" + taper({38, 46}, {36, 40}, {34, 36}, {32, 31}, 11, 9.5);
    over(v, body, 1.3);
    v.push_back(G::XS("M68 43C74 46 77 51 76 56M46 47C52 45 58 45 64 46", 1.4f));
    const Affine hm = place(13, 32, -4, 0.82f);
    over(v, xf(kJackal, hm), 1.3);
    v.push_back(G::X(xf(kJackalCut, hm)));
    v.push_back(G::XS(xf(kJackalLines, hm), 1.2f));
    c.add("race-canidis-shrine", "Шакал на ларце", fit(v));
  }
}

// Акула в профиль (влево) в локальных координатах: нос в (0, 0), длина около 100, хвост-полумесяц справа.
const char* kShark = "M0 0C6 -6 16 -9 28 -10L38 -10.5 50 -24 53 -10C62 -9.5 72 -8 80 -6L92 -18C93.5 -12 93 -6 90.5 -1C93 4 94 9 93 14"
                     "L80 3C72 5 62 7 52 8L46 18 41 9C30 9.5 20 9 12 6C6 4 2 2.5 0 0Z";
const char* kSharkCut = "M23 -3.4 27 -3.4 25.5 -1.4ZM31 -4V3M34 -4.4V3.4M37 -4.6V3.6";

// ---------------------------------------------------------------- кархароны
void karcharons(Catalog& c) {
  c.group("Кархароны", EmblemSection::Races);
  // Голова акулы: анфас — заострённое рыло, распахнутая пасть с рядами зубов, глаза по бокам, жабры, грудные плавники.
  {
    std::vector<L> v;
    const std::string fins = sym(blade({28, 72}, {20, 80}, {12, 88}, {3, 97}, 12, 0.3, 0.5, 1.1));
    v.push_back(G::F(fins));
    const std::string head = symFull("M50 3C43 3 37 7 32 14C25 24 18 36 15 50C12.5 63 16 75 25 83C32 89 41 92 50 92.5H50.5V3Z");
    over(v, head, 1.5);
    const std::string mouth = "M50 42C61 42 72 50 73 61C74 72 64 82 50 82C36 82 26 72 27 61C28 50 39 42 50 42Z";
    v.push_back(G::X(mouth));
    std::string teeth;
    for (int i = 0; i < 9; i++) {
      const double a = 195 + 18.75 * i;
      const Pt b0 = P(50 + 23 * std::cos((a - 7) * kPi / 180), 62 + 20 * std::sin((a - 7) * kPi / 180));
      const Pt b1 = P(50 + 23 * std::cos((a + 7) * kPi / 180), 62 + 20 * std::sin((a + 7) * kPi / 180));
      const Pt tip = P(50 + 13.5 * std::cos(a * kPi / 180), 62 + 11 * std::sin(a * kPi / 180));
      teeth += "M" + ptS(b0) + "L" + ptS(b1) + "L" + ptS(tip) + "Z";
      const double a2 = 15 + 18.75 * i;
      const Pt c0 = P(50 + 23 * std::cos((a2 - 7) * kPi / 180), 62 + 20 * std::sin((a2 - 7) * kPi / 180));
      const Pt c1 = P(50 + 23 * std::cos((a2 + 7) * kPi / 180), 62 + 20 * std::sin((a2 + 7) * kPi / 180));
      const Pt tip2 = P(50 + 14.5 * std::cos(a2 * kPi / 180), 62 + 12 * std::sin(a2 * kPi / 180));
      teeth += "M" + ptS(c0) + "L" + ptS(c1) + "L" + ptS(tip2) + "Z";
    }
    v.push_back(G::F(teeth));
    v.push_back(G::X(sym("M25.5 38 33 34.5 31.5 39.5Z")));
    v.push_back(G::XS(sym(curve({20, 46}, {18, 52}, {18, 58}, {19.5, 63}) + curve({23.5, 45}, {21.5, 51}, {21.5, 57}, {23, 62})) + "M50 9V32", 1.4f));
    c.add("race-karcharons-head", "Голова акулы", fit(v));
  }
  // Акулий зуб: подвеска — зуб остриём вниз с мелкой зазубриной по вогнутым краям и гравированной гранью, корень с отверстием,
  // шнурок.
  {
    std::vector<L> v;
    v.push_back(G::F(taper({50, 34}, {40, 22}, {26, 10}, {8, 2}, 2.8, 2.2) + taper({50, 34}, {60, 22}, {74, 10}, {92, 2}, 2.8, 2.2)));
    const Pt apex = P(50, 98), base = P(18, 40);
    std::vector<Pt> half{apex};
    const int teeth = 18;
    for (int i = 1; i <= teeth; i++) {
      const double t0 = (i - 0.5) / teeth, t1 = double(i) / teeth;
      auto at = [&](double t, double out) {
        const Pt p = bzP(apex, P(44, 80), P(30, 58), base, t);
        const CurvePt q = bezier(apex, P(44, 80), P(30, 58), base, t);
        return P(p.x - q.nx * out, p.y - q.ny * out);
      };
      half.push_back(at(t0, (i > 1 && i < teeth) ? 1.3 : 0));
      half.push_back(at(t1, 0));
    }
    std::vector<Pt> tooth = half;
    for (size_t k = half.size(); k-- > 1;) tooth.push_back(P(100 - half[k].x, half[k].y));
    over(v, polyPath(tooth), 1.4);
    v.push_back(G::XS(curve({47.5, 88}, {42, 74}, {34, 58}, {27, 46}), 1.4f));
    const std::string root = "M16 43C30 37 70 37 84 43C86 35 84 28 78 25C70 24 62 26 57 22C54 19.5 46 19.5 43 22C38 26 30 24 22 25C16 28 14 35 16 43Z";
    over(v, root, 1.6);
    v.push_back(G::XS(inset(root, 2.2), 1.2f));
    v.push_back(G::X(circ(50, 31, 3)));
    c.add("race-karcharons-tooth", "Акулий зуб", fit(v));
  }
  // Плавник над волной: серповидный спинной плавник с надрывом рассекает волны — три волнистые полосы с гребнями.
  {
    std::vector<L> v;
    const std::string fin = "M22 70C32 48 46 25 72 3C66 24 67 46 80 70Z";
    v.push_back(G::F(fin));
    v.push_back(G::X("M76.5 48 70.5 52.5 77.5 55Z"));
    v.push_back(G::XS(curve({34, 64}, {42, 46}, {52, 30}, {64, 15}, 0, 0.9), 1.4f));
    for (int r = 0; r < 3; r++) {
      const double y0 = 68 + 10 * r, ph = r % 2 ? kPi : 0;
      const std::string band = ribbon([&](double t) { return P(2 + 96 * t, y0 + 3 * std::sin(t * 4 * 2 * kPi + ph)); }, [](double) { return 6.4; }, 120);
      over(v, band, 1.2);
    }
    std::string crests;
    for (int i = 0; i < 4; i++) {
      const double x = 9 + 24 * i;
      crests += spike(P(x, 67), P(x + 4, 61), P(x + 10, 60), P(x + 13, 63.5), 4.2, 1);
    }
    over(v, crests, 1.0);
    c.add("race-karcharons-fin", "Плавник над волной", fit(v));
  }
  // Трезубец с акулой: акула наискось позади трезубца с зазубренными зубцами.
  {
    std::vector<L> v;
    const Affine sm = place(3, 84, -32, 1.02f);
    v.push_back(G::F(xf(kShark, sm)));
    v.push_back(G::X(xf(kSharkCut, sm)));
    v.push_back(G::XS(xf("M14 4C24 7 36 7.5 46 6", sm), 1.3f));
    const std::string prong = "M0 0 6 12 2.6 10.5V30H-2.6V10.5L-6 12Z";
    const std::string trident = xf(prong, place(50, 2, 0)) + xf(prong, place(27, 9, 0, 0.9f)) + xf(prong, place(73, 9, 0, 0.9f)) +
                                "M24.6 34C24.6 44 34 50 50 50C66 50 75.4 44 75.4 34H69.4C69.4 40.5 61 44 50 44C39 44 30.6 40.5 30.6 34Z" +
                                rrect(46.6, 46, 6.8, 44, 1.5) + "M44.5 88H55.5L50 99Z";
    over(v, trident, 1.6);
    v.push_back(G::XS("M46.6 60H53.4M46.6 64H53.4M46.6 75H53.4M46.6 79H53.4", 1.3f));
    c.add("race-karcharons-trident", "Трезубец с акулой", fit(v));
  }
}

// ---------------------------------------------------------------- кентавры
void centaurs(Catalog& c) {
  c.group("Кентавры", EmblemSection::Races);
  // Кентавр на дыбах: конь встал на задние ноги, всадник-торс натягивает лук влево, хвост языками по ветру.
  {
    std::vector<L> v;
    const std::string tail = blade({80, 60}, {90, 60}, {96, 70}, {92, 86}, 8, 0.3, 0.6, 1.4) + blade({80, 62}, {86, 70}, {86, 80}, {80, 92}, 6, 0.3, 0.6, 1.4);
    v.push_back(G::F(tail));
    // Конь: туловище с подъёмом к груди, задние ноги на земле, передние подогнуты в воздухе.
    const std::string horse = "M40 47C42 41 48 38 55 40C64 43 73 48 79 53C85 58 86 66 81 70C76 74 69 73 63 70C57 67 50 63 45 59C41 56 39 52 40 47Z" +
                              taper({76, 68}, {79, 76}, {80, 84}, {80, 92}, 8.5, 4.6) + taper({67, 70}, {66, 78}, {64, 85}, {61, 92}, 7.5, 4.4) +
                              "M76 91.5H84.5L85 96.5H75.5ZM56.5 91.5H65L65.5 96.5H56Z" +
                              taper({45, 54}, {39, 57}, {34, 57}, {29, 54}, 7, 5) + taper({29.5, 54.5}, {26.5, 59}, {26, 63}, {27, 68}, 5, 3.6) + "M24.5 67.5H30L29.5 71H24.5Z" +
                              taper({44, 47}, {38, 46}, {33, 45}, {28, 42}, 6.5, 4.8) + taper({28.5, 42.5}, {25, 46}, {23.5, 50}, {23.5, 55}, 4.8, 3.4) + "M21 54.5H26.5L26 58H21Z";
    over(v, horse, 1.4);
    v.push_back(G::XS(curve({56, 66}, {60, 70}, {66, 72}, {71, 72}) + "M50 42 54 47", 1.3f));
    // Торс лучника, голова, рука с луком.
    const std::string torso = taper({49, 47}, {48, 39}, {47, 32}, {46, 25}, 9, 13) + "M42 12.5C45 12.5 46.8 15 46.8 18C46.8 21.5 44.5 24.5 41.5 25C39.5 24 38 22 38 19.5L36.6 18.6 38.2 17.6C38.5 14.5 40 12.5 42 12.5Z" +
                              taper({43, 27}, {36, 26.5}, {29, 26}, {22, 26}, 5, 3.8) + taper({48, 26}, {52, 23}, {54, 21}, {55, 19.5}, 5, 4) +
                              spike({45.5, 14.5}, {50, 13}, {53, 15}, {55.5, 19.5}, 4.4, 1);
    over(v, torso, 1.3);
    v.push_back(G::XS("M43.5 31 49 33", 1.2f));
    // Лук, тетива, стрела.
    const std::string bow = ribbon([](double t) { return P(22 - 9 * std::sin(kPi * t) + 2 * std::sin(2 * kPi * t), 3 + 45 * t); },
                                   [](double t) { return 1.2 + 2.6 * std::sin(kPi * t); }, 40);
    v.push_back(G::F(bow));
    v.push_back(G::S("M22 3.5 55 19.5 22 47.5", 1.2f));
    v.push_back(G::F(taper({55, 19.5}, {40, 21}, {22, 23.5}, {10, 25}, 1.8, 1.8) + "M2 26 11 21.5 9.5 25.5 11.5 29Z" +
                     blade({56, 19}, {58, 16}, {60, 15}, {62, 14}, 3, 0.4, 0.5, 1) + blade({56, 20}, {58, 21}, {60, 23}, {62, 24}, 3, 0.4, 0.5, 1)));
    c.add("race-centaurs-archer", "Кентавр на дыбах", fit(v));
  }
  // Подкова со звездой: подкова концами вверх — утолщение у зацепа, шипы на концах, борозда с гвоздевыми прорезями; над ней звезда.
  {
    std::vector<L> v;
    auto u = [](double t) {
      const double a = -0.42 + (kPi + 0.84) * t, s = std::sin(a);
      return P(50 - 34 * std::cos(a), 40 + 48 * (s < 0 ? -1 : 1) * std::pow(std::fabs(s), 0.85));
    };
    const std::string shoe = ribbon(u, [](double t) { return 10 + 6 * std::sin(kPi * t); }, 60);
    const Pt l0 = u(0), r0 = u(1);
    v.push_back(G::F(shoe + rrect(l0.x - 6.2, l0.y - 3, 12.4, 5, 1) + rrect(r0.x - 6.2, r0.y - 3, 12.4, 5, 1)));
    std::string groove;
    for (int i = 0; i <= 30; i++) groove += (i ? "L" : "M") + ptS(u(0.06 + 0.88 * i / 30.0));
    v.push_back(G::XS(groove, 1.4f));
    std::string holes;
    for (int k = 0; k < 8; k++) {
      const double t = 0.1 + 0.8 * k / 7;
      const Pt p = u(t), q = u(t + 0.01);
      holes += xf("M-2.6 -1.2H2.6V1.2H-2.6Z", place(p.x, p.y, float(std::atan2(q.y - p.y, q.x - p.x) * 180 / kPi)));
    }
    v.push_back(G::X(holes));
    over(v, star(50, 24, 17, 5, 8, -90), 1.4);
    v.push_back(G::X(circ(50, 24, 2.4)));
    c.add("race-centaurs-horseshoe", "Подкова со звездой", fit(v));
  }
  // Копьё и щит: круглый щит с конской головой вырезом и двойным ободом, за ним — копьё наискось с листовидным наконечником и флажком.
  {
    std::vector<L> v;
    const Affine m = place(10, 92, -45);
    const std::string spear = xf(taper({0, 0}, {40, 0}, {80, 0}, {104, 0}, 4.4, 3.6) + blade({102, 0}, {108, 0}, {114, 0}, {122, 0}, 11, 0.4, 0.6, 1.1), m);
    const std::string pennon = xf("M86 -2C90 -10 98 -16 106 -15L100 -9 108 -6C100 -3 92 -1 88 2Z", m);
    v.push_back(G::F(spear + pennon));
    v.push_back(G::XS(xf("M108 0H118", m), 1.2f));
    const std::string shield = circ(45, 56, 34);
    over(v, shield, 1.8);
    v.push_back(G::XS(circ(45, 56, 29.5), 1.6f));
    const std::string head = "M30 64C31 57 37 49 44 42L48 31 52 38 56 31 58 41C62 47 64.5 57 63.5 67L65.5 80H48C48 74 48 70 46 66C42.5 67.5 38 68.5 34.5 68.5C31.5 68.5 29.5 66.5 30 64Z";
    v.push_back(G::X(head));
    v.push_back(G::F(xf("M-3 0C-1.5 -1.6 1.5 -1.6 3 0C1.5 1.2 -1.5 1.2 -3 0Z", place(48, 48, -35)) + ell(34, 64.5, 1.2, 0.9)));
    v.push_back(G::S("M55.5 44C58.5 51 59.5 61 59 73M52.5 41C55 47 56 53 56 59", 1.4f));
    c.add("race-centaurs-spear", "Копьё и щит", fit(v));
  }
  // Степной шатёр: купол с рёбрами, решётчатая стена, полог входа, навершие с раздвоенным флажком.
  {
    std::vector<L> v;
    v.push_back(G::F(taper({50, 22}, {50, 15}, {50, 9}, {50, 2}, 2.6, 2.2) + "M51 2C58 1 64 3 72 2L66 7.5 72 13C64 12 58 13.5 51 12.5Z"));
    const std::string roof = "M6 54C14 36 30 24 50 21C70 24 86 36 94 54Z";
    over(v, roof + "M43 14H57L55 22H45Z", 1.3);
    v.push_back(G::XS("M50 23 23 52M50 23 36 52.5M50 23V53M50 23 64 52.5M50 23 77 52M9 49Q50 41 91 49", 1.4f));
    const std::string wall = "M11 54H89V88H11Z";
    over(v, wall, 1.5);
    std::string lattice;
    for (double x = 11; x < 89; x += 9) {
      if (x + 9 > 38 && x < 62) continue;
      lattice += "M" + n2(x, 58) + "L" + n2(x + 9, 85) + "M" + n2(x + 9, 58) + "L" + n2(x, 85);
    }
    v.push_back(G::XS(lattice + "M13 57.5H87", 1.4f));
    v.push_back(G::X("M39 88V62C39 59 41 58 44 58H56C59 58 61 59 61 62V88Z"));
    v.push_back(G::F("M42 88V63C42 61.5 43 61 44.5 61H49V88ZM51 88V61H55.5C57 61 58 61.5 58 63V88Z"));
    v.push_back(G::F("M3 88H97L95 95H5Z"));
    c.add("race-centaurs-tent", "Степной шатёр", fit(v));
  }
}

}  // namespace

void raceEmblemsA(Catalog& c) {
  angels(c);
  arachnids(c);
  arborians(c);
  faceless(c);
  vampires(c);
  giants(c);
  highElves(c);
  harpies(c);
  gnolls(c);
  dwarvesRace(c);
  goblins(c);
  demonsRace(c);
  dryads(c);
  ibisians(c);
  canidis(c);
  karcharons(c);
  centaurs(c);
}

}  // namespace rg::gfx::emb
