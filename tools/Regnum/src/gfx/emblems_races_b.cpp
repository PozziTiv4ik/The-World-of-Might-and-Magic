// Regnum — эмблемы рас справочника: «Кецали» … «Наги» (раздел «Расы», группа на расу, по алфавиту; имена race-<раса>-…).
// Сетка 100 × 100, одноцветный силуэт, детали — вырезы; симметричные силуэты описаны левой половиной.
#include <algorithm>
#include <tuple>

#include "gfx/emblems_internal.h"

namespace rg::gfx::emb {

namespace {

using std::string;

// Опорная точка сглаженного контура: t — натяжение (1 — плавно, 0 — угол).
struct K {
  double x, y, t = 1;
};

// Сглаженный контур через точки (сплайн Катмулла — Рома, кубические кривые).
string spline(const std::vector<K>& p, bool closed = true) {
  const long m = long(p.size());
  auto at = [&](long i) -> const K& { return closed ? p[size_t((i % m + m) % m)] : p[size_t(std::clamp(i, 0L, m - 1))]; };
  string s = "M" + n(p[0].x) + " " + n(p[0].y);
  for (long i = 0; i < (closed ? m : m - 1); i++) {
    const K &a = at(i), &b = at(i + 1), &pa = at(i - 1), &nb = at(i + 2);
    s += "C" + n(a.x + (b.x - pa.x) * a.t / 6) + " " + n(a.y + (b.y - pa.y) * a.t / 6) + " " + n(b.x - (nb.x - a.x) * b.t / 6) + " " +
         n(b.y - (nb.y - a.y) * b.t / 6) + " " + n(b.x) + " " + n(b.y);
  }
  return closed ? s + "Z" : s;
}

// Симметричный контур: левая половина от верхней точки оси до нижней (крайние точки — на x = 50).
string symSpline(const std::vector<K>& half) {
  std::vector<K> p = half;
  for (size_t i = half.size() - 1; i-- > 1;) p.push_back({100 - half[i].x, half[i].y, half[i].t});
  return spline(p);
}

Pt lerp(Pt a, Pt b, double t) { return {float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t)}; }
string seg(Pt a, Pt b) { return "M" + ptS(a) + "L" + ptS(b); }

// Кристалл-призма от основания b к острию t шириной w; facets — грань по оси (вырез линией).
string crystal(Pt b, Pt t, double w, string* facets = nullptr, double shoulder = 0.72) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::max(1e-6, std::hypot(dx, dy)), nx = -dy / len * w / 2, ny = dx / len * w / 2;
  const Pt s{float(b.x + dx * shoulder), float(b.y + dy * shoulder)};
  if (facets) {
    const Pt b2{float(b.x + nx * 0.25), float(b.y + ny * 0.25)}, s2{float(s.x + nx * 0.25), float(s.y + ny * 0.25)};
    *facets += "M" + ptS(b2) + "L" + ptS(s2) + "L" + ptS(t) + "M" + ptS(s2) + "L" + ptS({float(s.x - nx), float(s.y - ny)});
  }
  return "M" + ptS({float(b.x + nx), float(b.y + ny)}) + "L" + ptS({float(s.x + nx), float(s.y + ny)}) + "L" + ptS(t) + "L" +
         ptS({float(s.x - nx), float(s.y - ny)}) + "L" + ptS({float(b.x - nx), float(b.y - ny)}) + "Z";
}

// Полоса переменной ширины вдоль кубической кривой: ширина w(t) (хвосты, языки пламени, щупальца).
template <class Fn>
string taperW(Pt p0, Pt c1, Pt c2, Pt p1, Fn w, int steps = 32) {
  std::vector<Pt> lft, rgt;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, h = w(t) * 0.5;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    lft.push_back({float(q.x + q.nx * h), float(q.y + q.ny * h)});
    rgt.push_back({float(q.x - q.nx * h), float(q.y - q.ny * h)});
  }
  string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Отрезок поперёк кривой при параметре t: от -a до +b по нормали.
string across(Pt p0, Pt c1, Pt c2, Pt p1, double t, double a, double b) {
  const CurvePt q = bezier(p0, c1, c2, p1, t);
  return "M" + n(q.x - q.nx * a) + " " + n(q.y - q.ny * a) + "L" + n(q.x + q.nx * b) + " " + n(q.y + q.ny * b);
}

// Трубка по сплайну через точки (тело змея, щупальце): ширина w(u), u — доля пути по точкам (0…1);
// at — выборка точек вдоль оси (perSeg на отрезок) с нормалью и шириной для деталей.
struct Tube {
  struct S {
    double x, y, nx, ny, w, u;
  };
  string fill;
  std::vector<S> at;
};
template <class Fn>
Tube tube(const std::vector<Pt>& sp, Fn width, int perSeg = 4) {
  Tube r;
  const size_t ns = sp.size() - 1;
  auto P = [&](long i) { return sp[size_t(std::clamp(i, 0L, long(ns)))]; };
  for (size_t i = 0; i < ns; i++) {
    const Pt p0 = sp[i], p1 = sp[i + 1], pm = P(long(i) - 1), pn = P(long(i) + 2);
    const Pt c1{p0.x + (p1.x - pm.x) / 6, p0.y + (p1.y - pm.y) / 6}, c2{p1.x - (pn.x - p0.x) / 6, p1.y - (pn.y - p0.y) / 6};
    const double u0 = double(i) / double(ns);
    r.fill += taperW(p0, c1, c2, p1, [&](double t) { return width(u0 + t / double(ns)) + 0.3; });
    for (int j = 0; j < perSeg; j++) {
      const double t = double(j) / perSeg;
      const CurvePt q = bezier(p0, c1, c2, p1, t);
      r.at.push_back({q.x, q.y, q.nx, q.ny, width(u0 + t / double(ns)), u0 + t / double(ns)});
    }
  }
  return r;
}

// Язык пламени (перо феникса): от основания b к острию t, ширина w у основания, изгиб bend.
string flame(Pt b, Pt t, double w, double bend) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::max(1e-6, std::hypot(dx, dy)), nx = -dy / len, ny = dx / len;
  const Pt c1{float(b.x + dx * 0.35 + nx * bend), float(b.y + dy * 0.35 + ny * bend)};
  const Pt c2{float(b.x + dx * 0.7 - nx * bend * 0.5), float(b.y + dy * 0.7 - ny * bend * 0.5)};
  return taperW(b, c1, c2, t, [w](double u) { return w * std::pow(1 - u, 0.85) * (0.75 + 0.25 * std::sin(kPi * u)) + 0.3; });
}

// Серп: круг (cx, cy, r) без круга (ox, oy, orad) — путь из двух дуг, целиком внутри серпа.
string crescent(double cx, double cy, double r, double ox, double oy, double orad) {
  const double dx = ox - cx, dy = oy - cy, d = std::max(1e-6, std::hypot(dx, dy)), ux = dx / d, uy = dy / d;
  const double a = (r * r - orad * orad + d * d) / (2 * d), h = std::sqrt(std::max(0.0, r * r - a * a));
  const Pt p{float(cx + ux * a - uy * h), float(cy + uy * a + ux * h)}, q{float(cx + ux * a + uy * h), float(cy + uy * a - ux * h)};
  auto deg = [](double x, double y) { return std::atan2(y, x) * 180 / kPi; };
  // Дуга окружности (x0, y0, rr) от точки s к точке e через направление mid.
  auto through = [&](double x0, double y0, double rr, Pt s, Pt e, double mid) {
    const double a0 = deg(s.x - x0, s.y - y0);
    double a1 = deg(e.x - x0, e.y - y0);
    while (a1 <= a0) a1 += 360;
    double m = mid;
    while (m < a0) m += 360;
    while (m >= a0 + 360) m -= 360;
    return m <= a1 ? arc(x0, y0, rr, a0, a1) : arc(x0, y0, rr, a0, a1 - 360);
  };
  const string outer = through(cx, cy, r, p, q, deg(-ux, -uy)), inner = through(ox, oy, orad, q, p, deg(-ux, -uy));
  return outer + inner.substr(inner.find('A')) + "Z";
}

// Лента по ломаной: ширина в каждой точке (тело змея вокруг клинка).
string ribbon(const std::vector<Pt>& p, const std::vector<double>& w) {
  std::vector<Pt> lft, rgt;
  for (size_t i = 0; i < p.size(); i++) {
    const Pt a = p[i > 0 ? i - 1 : i], b = p[i + 1 < p.size() ? i + 1 : i];
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1e-6, std::hypot(dx, dy)), h = w[i] / 2;
    lft.push_back({float(p[i].x - dy / len * h), float(p[i].y + dx / len * h)});
    rgt.push_back({float(p[i].x + dy / len * h), float(p[i].y - dx / len * h)});
  }
  string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Волна: гребни-завитки строкой (для обводки).
string waves(double x0, double x1, double y, double h, int count) {
  const double step = (x1 - x0) / count;
  string s = "M" + n(x0) + " " + n(y);
  for (int i = 0; i < count; i++) {
    const double x = x0 + step * i;
    s += "C" + n(x + step * 0.3) + " " + n(y - h) + " " + n(x + step * 0.7) + " " + n(y - h) + " " + n(x + step) + " " + n(y);
  }
  return s;
}

// Монета в ракурсе: верхняя грань и ребро.
void coin(std::vector<L>& v, double x, double y, double rx = 9.5, double ry = 4) {
  const string s = ell(x, y, rx, ry) + rrect(x - rx, y, rx * 2, 3, 0) + ell(x, y + 3, rx, ry);
  v.push_back(G::XS(s, 2));
  v.push_back(G::F(s));
  v.push_back(G::XS(ell(x, y, rx - 0.6, ry - 0.4), 1.4));
}

// Перо с вырезанным стержнем.
void quill(std::vector<L>& v, Pt b, Pt t, double w, float gap = 2.2f, float shaft = 1.6f) {
  const string f = feather(b, t, w);
  v.push_back(G::XS(f, gap));
  v.push_back(G::F(f));
  v.push_back(G::XS(seg(lerp(b, t, 0.18), lerp(b, t, 0.8)), shaft));
}

// ---------------------------------------------------------------- кецали
void quetzals(Catalog& c) {
  c.group("Кецали", EmblemSection::Races);
  // Пернатый змей: голова с раскрытой пастью, веер перьев за головой, тело S-изгибом, хвост с перьями.
  {
    std::vector<L> v;
    for (int k = 0; k < 5; k++) {
      const double a = 196 + 19 * k;
      quill(v, polar(52, 30, 6, a), polar(52, 30, 30 - std::abs(k - 2) * 1.5, a), 11);
    }
    // Хребет змея — сплайн через точки, ширина убывает к хвосту; перья — по внешней стороне изгибов.
    const std::vector<Pt> sp = {{56, 31}, {43, 43}, {28, 51}, {24, 64}, {32, 77}, {50, 81}, {64, 75}, {74, 67}, {86, 64}};
    const size_t ns = sp.size() - 1;
    auto P = [&](long i) { return sp[size_t(std::clamp(i, 0L, long(ns)))]; };
    auto width = [&](double u) { return 3 + 12 * std::pow(1 - u, 0.8); };
    string body, ruff, belly;
    for (size_t i = 0; i < ns; i++) {
      const Pt p0 = sp[i], p1 = sp[i + 1], pm = P(long(i) - 1), pn = P(long(i) + 2);
      const Pt c1{p0.x + (p1.x - pm.x) / 6, p0.y + (p1.y - pm.y) / 6}, c2{p1.x - (pn.x - p0.x) / 6, p1.y - (pn.y - p0.y) / 6};
      const double u0 = double(i) / ns;
      body += taperW(p0, c1, c2, p1, [&](double t) { return width(u0 + t / ns) + 0.3; });
      for (double t : {0.0, 0.5}) {
        const double u = u0 + t / ns, w = width(u);
        const CurvePt q = bezier(p0, c1, c2, p1, t);
        const double tx = q.ny, ty = -q.nx;
        if (u > 0.03 && u < 0.6)
          ruff += feather({float(q.x + q.nx * (w / 2 - 2)), float(q.y + q.ny * (w / 2 - 2))},
                          {float(q.x + q.nx * (w / 2 + 9) + tx * 7), float(q.y + q.ny * (w / 2 + 9) + ty * 7)}, 7.5);
        if (u > 0.05 && u < 0.8) belly += across(p0, c1, c2, p1, t, w / 2 + 1, -w * 0.18);
      }
    }
    const string bands = belly;
    v.push_back(G::XS(ruff, 2));
    v.push_back(G::F(ruff));
    const string head = "M48 24C50 15 58 10 67 11C76 12 84 16 91 21L89 26C81 25 73 26 65 30C59 33 54 35 49 35Z";
    const string jaw = "M56 37C64 35 73 35 83 39L81 42.5C73 43 65 45 58 47Z";
    v.push_back(G::XS(body, 3));
    v.push_back(G::F(body));
    v.push_back(G::XS(bands, 1.8));
    v.push_back(G::XS(head + jaw, 3));
    v.push_back(G::F(head + jaw + "M77 25.5 80.5 34 83.5 25ZM71 38 73 33 75 38Z"));
    v.push_back(G::X(circ(66, 18, 2.6) + circ(86.5, 20.5, 1.1)));
    v.push_back(G::XS("M60 14C64 12 69 12 72 14", 1.6));
    for (int k = 0; k < 3; k++) {
      const double a = 10 + 32 * k;
      quill(v, polar(85, 64, 2, a), polar(85, 64, 16 - 2 * std::abs(k - 1), a), 8, 2, 1.4f);
    }
    c.add("race-quetzals-serpent", "Пернатый змей", moved(v, Affine::translate(-2, 2)));
  }
  // Птица кецаль на ветке: хохолок, длинные хвостовые перья до самого низа.
  {
    std::vector<L> v;
    const string branch = "M8 59C18 56 36 55 62 56L62 61.5C38 61.5 20 62.5 9 64.5Z" + feather({14, 59}, {4, 46}, 8) + feather({20, 61}, {12, 73}, 7);
    v.push_back(G::F(branch));
    const string plume1 = taper({54, 52}, {62, 68}, {72, 84}, {95, 92}, 9, 2), plume2 = taper({51, 54}, {55, 72}, {63, 88}, {83, 97}, 8, 2);
    v.push_back(G::XS(plume1 + plume2, 2.2));
    v.push_back(G::F(plume2));
    v.push_back(G::XS(plume1, 2.2));
    v.push_back(G::F(plume1));
    const string body = xf(ell(0, 0, 14, 19.5), place(42, 38, -32)) + circ(32, 22, 10) + "M21 20C20 10 29 3 39 6C44 8 45 14 42 19Z" +
                        "M23.5 18.5 13 23.5 23.5 27Z";
    v.push_back(G::XS(body, 2.4));
    v.push_back(G::F(body));
    const string wing = "M37 31C47 27 57 34 60 45L66 59C57 56 47 51 41 45C37 41 35 36 37 31Z";
    v.push_back(G::XS(wing, 2.2));
    v.push_back(G::F(wing));
    v.push_back(G::XS("M44 38C50 41 54 46 57 52M41 33C48 34 53 39 56 44", 1.6));
    v.push_back(G::XS("M29 33C32 37 33 40 33 44M27 39C29 42 30 45 30 48", 1.6));
    v.push_back(G::X(circ(30, 20, 2.1)));
    v.push_back(G::S("M40 54 37 60M46 55 45 61", 2.6));
    c.add("race-quetzals-bird", "Птица кецаль", v);
  }
  // Солнечный камень: зубчатая кайма, восемь лучей, внутренний диск с ликом и четырьмя знаками движения.
  {
    string notches, rays, ollin;
    for (int k = 0; k < 32; k++) notches += seg(polar(50, 50, 42.5, k * 11.25), polar(50, 50, 48, k * 11.25));
    for (int k = 0; k < 4; k++) ollin += xf(rrect(-4.5, -4.5, 9, 9, 1.5), place(polar(50, 50, 16.5, 45 + 90 * k).x, polar(50, 50, 16.5, 45 + 90 * k).y, 45));
    c.add("race-quetzals-sun", "Солнечный камень",
          {G::F(circ(50, 50, 47)), G::XS(notches, 1.8), G::XS(circ(50, 50, 41), 2.4), G::X(circ(50, 50, 39.8)),
           G::F(star(50, 50, 39.8, 25, 8, -90) + star(50, 50, 33, 25, 8, -67.5)), G::XS(circ(50, 50, 24.5), 2.2),
           G::XS(star(50, 50, 33, 25, 8, -67.5), 1.6), G::XS(circ(50, 50, 10.5), 2), G::XS(ollin, 1.8),
           G::X(circ(46, 48, 1.8) + circ(54, 48, 1.8) + "M46.5 53H53.5L50 56.5Z")});
  }
  // Маска из перьев: веер перьев с каймой, налобная лента с нефритами, лик с ушными вставками.
  {
    std::vector<L> v;
    const int order[] = {0, 8, 1, 7, 2, 6, 3, 5, 4};
    for (int k : order) {
      const double a = 180 + 22.5 * k;
      const string f = feather(polar(50, 54, 8, a), polar(50, 54, 46, a), 14);
      v.push_back(G::XS(f, 2.4));
      v.push_back(G::F(f));
      v.push_back(G::XS(seg(polar(50, 54, 14, a), polar(50, 54, 30, a)), 1.6));
    }
    v.push_back(G::XS(arc(50, 54, 35, 172, 368), 2));
    const string band = "M17 51C27 37 73 37 83 51L79 57C69 45 31 45 21 57Z";
    v.push_back(G::XS(band, 2.4));
    v.push_back(G::F(band));
    v.push_back(G::X(circ(30, 46, 2) + circ(50, 42, 2.2) + circ(70, 46, 2)));
    const string face = symSpline({{50, 44}, {37, 45}, {29, 52}, {28, 64}, {33, 79}, {42, 90}, {50, 94}});
    const string spool = circ(24, 64, 6.5) + circ(76, 64, 6.5);
    v.push_back(G::XS(face + spool, 3));
    v.push_back(G::F(face + spool));
    v.push_back(G::X(circ(24, 64, 2.6) + circ(76, 64, 2.6)));
    v.push_back(G::X(sym("M33 58 45 60.5 44 65.5 34 63Z") + rrect(40, 80, 20, 6.5, 2)));
    v.push_back(G::F(rrect(43.5, 80, 4.5, 3.6, 0.8) + rrect(52, 80, 4.5, 3.6, 0.8)));
    v.push_back(G::XS("M47.5 61 46 74.5 50 76.5 54 74.5 52.5 61", 1.8));
    v.push_back(G::XS(sym("M33 69 38 72M34 74 39 77"), 1.6));
    c.add("race-quetzals-mask", "Маска из перьев", v);
  }
}

// ---------------------------------------------------------------- кицунэ
void kitsune(Catalog& c) {
  c.group("Кицунэ", EmblemSection::Races);
  // Девятихвостая лиса: сидящая лиса в профиль, веер из девяти хвостов со светлыми кончиками.
  {
    std::vector<L> v;
    // Хвост — несимметричный язык пламени: брюшко выгнуто в одну сторону, кончик отогнут в другую.
    const Pt tips[] = {{9, 46}, {21, 33}, {26, 11}, {45, 15}, {59, 3}, {71, 20}, {86, 20}, {85, 40}, {94, 56}};
    for (int k = 8; k >= 0; k--) {
      const Pt b{48, 86}, t = tips[k];
      const double dx = t.x - b.x, dy = t.y - b.y, len = std::hypot(dx, dy);
      const Affine m = place(b.x, b.y, float(std::atan2(dy, dx) * 180 / kPi));
      auto P = [len](double f, double s, double tn = 1) { return K{len * f, len * (s + 0.09 * f * f), tn}; };
      const string tail = xf(spline({P(0, 0.04, 0), P(0.3, 0.12), P(0.62, 0.17), P(0.86, 0.1), P(1, -0.05, 0), P(0.84, -0.1), P(0.6, -0.1),
                                     P(0.3, -0.06), P(0, -0.04, 0)}),
                             m);
      v.push_back(G::XS(tail, 2.6));
      v.push_back(G::F(tail));
      v.push_back(G::XS(xf("M" + n(len * 0.76) + " " + n(len * 0.17) + "Q" + n(len * 0.86) + " " + n(len * 0.02) + " " + n(len * 0.78) + " " +
                               n(len * -0.12),
                           m),
                        2.2));
    }
    // Лиса в профиль, сидит мордой влево.
    const string head = spline({{8, 35, 0}, {18, 28}, {28, 22}, {38, 20}, {46, 23}, {50, 31}, {47, 39}, {38, 44}, {30, 49, 0}, {28, 43}, {20, 41}, {13, 39}});
    const string ear = "M35 23 40 3 50 22Z";
    const string body = spline({{43, 31}, {54, 39}, {63, 54}, {69, 70}, {70, 84}, {66, 94}, {54, 96, 0}, {28, 96, 0}, {29, 82}, {29, 64}, {33, 48}});
    const std::vector<L> fox = {G::XS(head + ear + body, 4), G::F(body), G::XS(head + ear, 2.4), G::F(head + ear),
                                G::X("M40 10.5 39 21 45.5 21.5Z" + string("M23 29C26 27.5 30 28 32.5 30C29.5 31.5 26 31.5 23 29Z")),
                                G::XS("M13 38.5C18 38 22 38.5 26 40", 1.8),
                                G::XS("M39 62C41 72 41 84 40 96M66 96C56 92 52 82 56 72C58 68 62 66 66 66", 2.2),
                                G::XS("M33 54 37 58 33 61 37 65", 1.8)};
    for (const L& l : moved(fox, scaleAt(0.68f, 6, 97))) v.push_back(l);
    c.add("race-kitsune-tails", "Девятихвостая лиса", v);
  }
  // Маска кицунэ: высокие уши, узкие раскосые глаза, знаки на лбу и щеках.
  {
    const string face = symSpline({{50, 20}, {42, 21}, {25, 3, 0}, {27, 36}, {19, 52, 0.6}, {29, 71}, {42, 86}, {50, 95, 0.3}});
    c.add("race-kitsune-mask", "Маска кицунэ",
          {G::F(face), G::X(sym("M41 23 28 10 30 33Z")), G::F(sym("M36 22 31 16 32 28Z")),
           G::X(sym("M27 45C33 46 39 49 44 54C38 55 32 52 27 45Z")),
           G::X("M50 27C54 32 55.5 35 55.5 38A5.5 5.5 0 0 1 44.5 38C44.5 35 46 32 50 27Z"),
           G::XS(sym("M24 58C30 60 34 64 36 69M27 63C31 66 33 70 34 74"), 1.8), G::X(ell(50, 89, 4, 3)),
           G::XS("M50 78V84M44 82C46 85 48 86 50 84C52 86 54 85 56 82", 1.6), G::X(sym(circ(41, 75, 1.2) + circ(38, 79, 1.2)))});
  }
  // Лисий огонь: звёздная жемчужина с языком пламени, вихрь внутри, два огонька по сторонам.
  {
    const string pearl = "M24 64C24 50 36 42 44 33C49 27 50 20 46 11C57 16 62 26 60 36C71 42 76 52 76 64A26 26 0 0 1 24 64Z";
    auto wisp = [](double x, double y, double s) {
      return xf("M0 8A8 8 0 0 1 -8 0C-8 -8 -2 -12 3 -20C2 -13 8 -9 8 0A8 8 0 0 1 0 8Z", place(float(x), float(y), 0, float(s)));
    };
    c.add("race-kitsune-foxfire", "Лисий огонь",
          {G::F(pearl + wisp(13, 44, 1) + xf(wisp(0, 0, 1), Affine{-1, 0, 0, 1, 87, 44})),
           G::XS("M50 82C38 82 32 72 36 63C40 55 52 55 54 63C56 69 50 72 46 68", 2.6), G::X(circ(62, 56, 3.4)),
           G::XS("M47 36C51 32 53 27 52 21", 1.8), G::X(circ(13, 46, 2.6) + circ(87, 46, 2.6))});
  }
  // Ворота-тории: изогнутая верхняя балка, табличка, сквозная поперечина, наклонные столбы.
  c.add("race-kitsune-torii", "Врата-тории",
        {G::F("M2 8C22 15 78 15 98 8L95 17C78 23 22 23 5 17Z" + rrect(13, 21, 74, 6, 1) + rrect(9, 37, 82, 7, 1.5) +
              sym("M25 26H34.5L32 90H23Z") + sym(rrect(18.5, 88, 18, 7, 2)) + rrect(45.5, 26, 9, 12, 0) + rrect(41, 28, 18, 10, 1.5)),
         G::XS("M5 16.5C22 22 78 22 95 16.5", 1.8), G::X(rrect(44, 30.5, 12, 5, 0.8))});
}

// ---------------------------------------------------------------- кобольды
void kobolds(Catalog& c) {
  c.group("Кобольды", EmblemSection::Races);
  // Кобольд-рудокоп: кирка на плече, фонарь в вытянутой лапе, хвост.
  {
    const string pick = xf("M-21 7C-13 -1 -7 -4 0 -4C7 -4 13 -1 21 7C13 3 7 1.5 0 1.5C-7 1.5 -13 3 -21 7Z" + rrect(-4.5, -7, 9, 11, 2) +
                               rrect(-2.6, 2, 5.2, 50, 2.2),
                           place(23, 22, -52));
    const string head = "M42 21C43 14 51 10 58 12C64 14 70 16 78 18.5C82 19.5 84 23 82 27C76 29.5 68 30.5 62 32C56 34 47 34 43 29Z" +
                        taper({48, 14}, {45, 7}, {39, 2.5}, {31, 3}, 6.5, 1.2) + taper({44, 18}, {39, 13}, {33, 11}, {27, 12.5}, 5, 1.2) +
                        "M46 23 35 27.5 45 30Z";
    const string body = "M45 29C52 31 58 36 60 44C62 52 60 60 54 66C48 70 40 68 36 62C32 54 34 40 40 32Z";
    const string tail = taper({38, 62}, {28, 70}, {18, 76}, {9, 91}, 10, 1.5);
    const string lantern = "M68 56H80L78 59H70Z" + rrect(68.5, 58.5, 11, 15, 2) + "M70 73H78L80 76H68Z";
    c.add("race-kobolds-miner", "Кобольд-рудокоп",
          moved({G::F(pick), G::XS(head + body + tail, 2.6), G::F(head + body + tail), G::S("M41 63 33 78 37 91M49 64 57 78 54 91", 7),
                 G::F("M30 89H42L41 94H31ZM50 89H63L62 94H51Z"), G::X(circ(59, 19, 2.4)), G::XS("M63 27.5C69 26.5 75 25.5 80 25", 1.6),
                 G::XS("M44 42C47 48 48 54 47 60", 1.8), G::S("M55 40 66 50", 6.5), G::S("M70 53C70 49 78 49 78 53", 2),
                 G::XS(lantern, 2.4), G::F(lantern), G::X(rrect(71, 61, 6, 9.5, 1.5)), G::F("M74 62C76 64.5 76.5 66 76 67.5A2 2 0 0 1 72 67.5C71.5 66 72 64.5 74 62Z"),
                 G::XS("M58 30C60 26 62 23 62 20", 1.4)},
                Affine::translate(4, 0)));
  }
  // Свеча на шлеме: каска с полями, оплывшая свеча, из-под полей светятся глаза.
  c.add("race-kobolds-candle", "Свеча на шлеме",
        {G::F(symSpline({{50, 68}, {31, 68}, {28, 76}, {36, 86}, {44, 93}, {50, 95}}) +
              sym(taper({31, 74}, {23, 76}, {17, 81}, {13, 89}, 6, 1.5))),
         G::X(sym("M33 74.5 45 79 44 82 35 79.5Z") + circ(47, 90.5, 1.2) + circ(53, 90.5, 1.2)),
         G::XS("M6 64C6 58 24 56 50 56C76 56 94 58 94 64C94 68 76 70 50 70C24 70 6 68 6 64Z", 3),
         G::F("M6 64C6 58 24 56 50 56C76 56 94 58 94 64C94 68 76 70 50 70C24 70 6 68 6 64Z" +
              string("M20 60C20 41 32 30 50 30C68 30 80 41 80 60Z") + ell(50, 31, 11, 3) +
              "M44 22H56V31H44ZM44 22C44 26 46 27 46 30H44ZM54 24C55 27 56 28 56 32H54Z" + rrect(49.2, 17, 1.6, 6, 0.8) +
              "M50 2C54.5 6.5 56.5 9 56.5 12A6.5 6.5 0 0 1 43.5 12C43.5 9 45.5 6.5 50 2Z"),
         G::X("M50 8.5C52 11 53 12.5 53 14A3 3 0 0 1 47 14C47 12.5 48 11 50 8.5Z"), G::XS("M7.5 62.5C24 59.5 76 59.5 92.5 62.5", 1.6),
         G::XS("M22 51C34 47.5 66 47.5 78 51", 2.2), G::X(circ(32, 46.5, 1.5) + circ(50, 44.5, 1.5) + circ(68, 46.5, 1.5)),
         G::XS("M44 26.5H56", 1.4)});
  // Капкан: дуги с зубьями, пружины по сторонам, тарелка в центре, цепь.
  {
    const double cx = 50, cy = 45, rx = 34, ry = 18;
    string far, near;
    for (int th = 20; th < 360; th += 20) {
      if (th % 180 == 0) continue;
      const double r = th * kPi / 180, x = cx + rx * std::cos(r), y = cy + ry * std::sin(r) - 1;
      (th > 180 ? far : near) += "M" + n(x - 3.8) + " " + n(y) + "L" + n(x) + " " + n(y - 12) + "L" + n(x + 3.8) + " " + n(y) + "Z";
    }
    const string spring = rrect(1, 39, 24, 12, 6) + rrect(75, 39, 24, 12, 6);
    c.add("race-kobolds-trap", "Капкан",
          moved({G::F(far), G::XS(ell(cx, cy, rx, ry), 9), G::F(rrect(4, 43, 92, 4.5, 2) + spring), G::X(rrect(5, 42.5, 15, 5, 2.5) + rrect(80, 42.5, 15, 5, 2.5)),
                 G::S(ell(cx, cy, rx, ry), 6), G::XS(near, 2.2), G::F(near), G::XS(ell(50, 47, 10, 5), 3), G::F(ell(50, 47, 10, 5)),
                 G::XS(ell(50, 46.5, 6, 2.6), 1.6), G::S(ell(50, 71, 3.6, 5.5), 2.8), G::F(rrect(48.6, 75, 2.8, 8, 1.4)),
                 G::S(ell(50, 87, 3.6, 5.5), 2.8)},
                Affine::translate(0, -3)));
  }
  // Яйцо дракона на груде монет: чешуя внахлёст, трещина.
  {
    const string egg = symSpline({{50, 5}, {38, 11}, {28, 29}, {24, 50}, {30, 68}, {40, 77}, {50, 79}});
    string scales;
    for (int r = 0; r < 7; r++) {
      const double y = 17 + 8.5 * r;
      for (int k = -4; k <= 4; k++) {
        const double x = 50 + k * 9 + (r % 2 ? 4.5 : 0);
        const double ex = (x - 50) / 25.0, ey = (y + 3 - 43) / 36.0;
        if (ex * ex + ey * ey > 0.72) continue;
        scales += "M" + n(x - 4.5) + " " + n(y) + "Q" + n(x) + " " + n(y + 8) + " " + n(x + 4.5) + " " + n(y);
      }
    }
    std::vector<L> v;
    const Pt coins[] = {{17, 76}, {83, 76}, {26, 83}, {74, 83}, {12, 88}, {88, 88}, {37, 88}, {63, 88}, {50, 90}};
    for (Pt p : coins) coin(v, p.x, p.y);
    v.push_back(G::XS(egg, 3));
    v.push_back(G::F(egg));
    v.push_back(G::XS(scales, 1.8));
    v.push_back(G::XS("M37 14 43 21 39 26 46 31 43 37", 2.2));
    c.add("race-kobolds-egg", "Яйцо дракона", v);
  }
}

// ---------------------------------------------------------------- кристаллиты
void crystallites(Catalog& c) {
  c.group("Кристаллиты", EmblemSection::Races);
  // Кристальный воин: гранёная голова, плечи из кристаллов, светящееся ядро в груди.
  {
    string facets, spikes;
    spikes += crystal({29, 48}, {8, 15}, 15, &facets) + crystal({37, 42}, {31, 10}, 9, &facets) + crystal({24, 57}, {4, 44}, 10, &facets);
    const string spikesAll = sym(spikes);
    const string facetsAll = sym(facets);
    const string torso = poly({50, 39, 31, 42, 21, 53, 30, 74, 40, 96, 60, 96, 70, 74, 79, 53, 69, 42});
    const string head = poly({50, 3, 61, 10, 61, 28, 50, 36, 39, 28, 39, 10});
    c.add("race-crystallites-warrior", "Кристальный воин",
          {G::F(spikesAll), G::XS(facetsAll, 1.6), G::XS(torso + head, 3), G::F(torso + head + rrect(44, 33, 12, 8, 1)),
           G::XS("M39 10 50 17 61 10M50 17V36", 1.6), G::X(sym("M41 20 48 23 47 25.5 41 23Z")),
           G::XS(sym("M31 42 42 56 30 74M21 53 42 56M42 56 50 44M42 56 50 68 40 96M50 68 60 96"), 1.6),
           G::X(poly({50, 45, 58, 56, 50, 67, 42, 56})), G::F(poly({50, 49.5, 54.5, 56, 50, 62.5, 45.5, 56}))});
  }
  // Друза: пучок шестигранных кристаллов на камне.
  {
    std::vector<L> v;
    struct Cr {
      Pt b, t;
      double w;
    };
    const Cr cs[] = {{{30, 92}, {9, 50}, 12}, {{70, 92}, {91, 50}, 12}, {{40, 90}, {25, 18}, 15},
                     {{60, 90}, {75, 18}, 15}, {{44, 94}, {36, 66}, 8}, {{50, 88}, {50, 3}, 19}};
    for (const Cr& k : cs) {
      string f;
      const string s = crystal(k.b, k.t, k.w, &f, 0.78);
      v.push_back(G::XS(s, 2.4));
      v.push_back(G::F(s));
      v.push_back(G::XS(f, 1.6));
    }
    const string rock = "M4 96C5 87 14 81 27 83C36 78 46 80 52 82C60 78 72 79 76 83C86 81 95 87 96 96Z";
    v.push_back(G::XS(rock, 2.6));
    v.push_back(G::F(rock));
    v.push_back(G::XS("M24 96 28 89 26 85M62 96 60 89 64 84", 1.6));
    c.add("race-crystallites-cluster", "Друза", v);
  }
  // Самоцвет-сердце: гранёное сердце с площадкой и искрами.
  {
    const string outer = sym("M50 31 42 20 30 15 18 19 10 31 12 46 22 60 50 93 50 31Z");
    const string inner = sym("M50 40 44 32 35 29 27 33 26 43 33 54 50 74");
    c.add("race-crystallites-heart", "Самоцвет-сердце",
          {G::F(outer + star(86, 14, 11, 2.6, 4, -90) + star(15, 84, 8, 2, 4, -90) + star(87, 72, 5.5, 1.5, 4, -90)),
           G::XS(inner, 1.8), G::XS(sym("M42 20 44 32M30 15 35 29M18 19 27 33M10 31 26 43M12 46 33 54M22 60 50 74M50 31V40"), 1.6),
           G::X(circ(32, 26, 2.2))});
  }
  // Призма: луч входит слева и расходится веером из пяти лучей.
  {
    string rays;
    for (int k = 0; k < 5; k++) {
      const Pt a = lerp({62.5, 36}, {70.5, 51}, k / 4.0), b = lerp({62.5, 36}, {70.5, 51}, (k + 0.8) / 4.0);
      const double y0 = 19 + 14.5 * k;
      rays += "M" + ptS(a) + "L97 " + n(y0) + "L97 " + n(y0 + 10) + "L" + ptS(b) + "Z";
    }
    c.add("race-crystallites-prism", "Призма",
          {G::F(rays), G::S("M4 60 29 51", 5), G::XS("M50 12 82 72H18Z", 3), G::F("M50 12 82 72H18Z"),
           G::XS("M50 27 69.5 64H30.5Z", 1.8), G::XS("M50 12V27M82 72 69.5 64M18 72 30.5 64", 1.6)});
  }
}

// ---------------------------------------------------------------- кровавые эльфы
void bloodElves(Catalog& c) {
  c.group("Кровавые Эльфы", EmblemSection::Races);
  // Феникс в пламени: крылья подняты языками пламени, голова в профиль с огненным хохолком, хвост-пламя.
  {
    std::vector<L> v;
    const Pt bases[] = {{42, 56}, {40, 52}, {39, 48}, {40, 44}, {42, 40}};
    const Pt tips[] = {{11, 66}, {3, 50}, {4, 33}, {11, 17}, {24, 5}};
    for (int k = 0; k < 5; k++) {
      const string f = sym(flame(bases[k], tips[k], 15, 7));
      v.push_back(G::XS(f, 2.2));
      v.push_back(G::F(f));
    }
    const string covert = sym("M47 40C40 34 32 30 22 28C25 37 30 45 37 51C41 54 44 56 47 56Z");
    v.push_back(G::XS(covert, 2.2));
    v.push_back(G::F(covert));
    v.push_back(G::XS(sym("M43 44C37 40 31 37 25 34M43 50C38 47 34 44 30 41"), 1.6));
    const string tail = flame({50, 68}, {50, 98}, 14, 0) + sym(flame({47, 68}, {33, 96}, 12, -6) + flame({45, 65}, {22, 88}, 10, -6));
    v.push_back(G::XS(tail, 2.2));
    v.push_back(G::F(tail));
    const string body = ell(50, 55, 10, 15) + taper({50, 44}, {48, 34}, {42, 28}, {42, 21}, 11, 8) + circ(42, 19, 6.5);
    v.push_back(G::XS(body, 2.6));
    v.push_back(G::F(body + "M36.5 16.5C32 16.5 28.5 18.5 27 22C30 22 33 23 36 24.5Z" + flame({46, 16}, {60, 3}, 6, 2) + flame({47, 19}, {64, 11}, 5, 2) +
                     flame({47, 22}, {62, 20}, 4, 1)));
    v.push_back(G::X(circ(41, 18, 1.5) + "M50 47C54 52 56 56 56 59.5A6 6 0 0 1 44 59.5C44 56 46 52 50 47Z"));
    v.push_back(G::F("M50 53C52 55.5 53 57.5 53 59.5A3 3 0 0 1 47 59.5C47 57.5 48 55.5 50 53Z"));
    c.add("race-blood-elves-phoenix", "Феникс в пламени", v);
  }
  // Кровь в кристалле: гранёный кристалл с каплей крови внутри, два осколка по сторонам.
  {
    const string gem = poly({50, 3, 71, 23, 66, 77, 50, 97, 34, 77, 29, 23});
    const string shards = sym(poly({16, 30, 22.5, 47, 16, 66, 9.5, 47}));
    c.add("race-blood-elves-drop", "Кровь в кристалле",
          {G::F(gem + shards), G::XS(sym("M29 23H42L50 3M42 23 34 77M34 77 50 86") + "M50 86V97", 1.6), G::XS(sym("M9.5 47H16V66"), 1.4),
           G::X("M50 30C58 41 64 49 64 59A14 14 0 0 1 36 59C36 49 42 41 50 30Z"),
           G::F("M50 36C55.5 44.5 60 51 60 58.5A10 10 0 0 1 40 58.5C40 51 44.5 44.5 50 36Z"), G::X(xf(ell(0, 0, 1.8, 4), place(45, 59, 15)))});
  }
  // Солнечный клинок: эльфийский меч с крылатой гардой перед солнцем.
  {
    string rays;
    for (int i = 0; i < 16; i++) {
      const double a = -90 + 22.5 * i, R = i % 2 ? 36 : 44;
      rays += "M" + pt(50, 46, 20, a - 6.5) + "L" + pt(50, 46, R, a) + "L" + pt(50, 46, 20, a + 6.5) + "Z";
    }
    const string blade = "M50 2C55 10 57.5 18 57 28C56.5 40 55 52 54.5 62H45.5C45 52 43.5 40 43 28C42.5 18 45 10 50 2Z";
    const string guard = "M26 56C34 64 44 66 50 66C56 66 66 64 74 56C78 52 80 46 79 40C76 48 70 55 62 58C58 59.5 54 60 50 60"
                         "C46 60 42 59.5 38 58C30 55 24 48 21 40C20 46 22 52 26 56Z";
    const string sword = blade + guard + rrect(46.5, 64, 7, 22, 2) + "M50 84 56.5 90.5 50 97.5 43.5 90.5Z";
    c.add("race-blood-elves-blade", "Солнечный клинок",
          {G::F(rays + circ(50, 46, 21)), G::XS(circ(50, 46, 15.5), 2), G::XS(sword, 4.5), G::F(sword), G::XS("M50 10V57", 1.8),
           G::XS("M46.5 70H53.5M46.5 75H53.5M46.5 80H53.5", 1.5), G::X("M50 87 53 90.5 50 94 47 90.5Z"), G::X(sym(circ(30, 52, 1.6)))});
  }
  // Эльфийский глаз: глаз с солнечной радужкой, завитки у уголков, венец из пламени, кровавая слеза.
  {
    const string lids = "M13 50C26 33 74 33 87 50C74 65 26 65 13 50Z";
    string burst;
    for (int k = 0; k < 12; k++) burst += seg(polar(50, 49.5, 4.6, k * 30), polar(50, 49.5, 8, k * 30));
    const string flourish = sym(taperW({16, 47}, {8, 40}, {3, 30}, {9, 21}, [](double u) { return 6 * (1 - u) + 0.6; }) +
                                taperW({17, 54}, {10, 60}, {7, 68}, {13, 73}, [](double u) { return 4.5 * (1 - u) + 0.6; }));
    const string brow = flame({50, 32}, {50, 5}, 9, 0) + sym(flame({41, 33}, {31, 10}, 7, -3) + flame({33, 37}, {17, 20}, 5.5, -3));
    c.add("race-blood-elves-eye", "Эльфийский глаз",
          {G::F(brow + "M50 70C54.5 76.5 56.5 80.5 56.5 84.5A6.5 6.5 0 0 1 43.5 84.5C43.5 80.5 45.5 76.5 50 70Z"), G::XS(lids, 3),
           G::F(lids + flourish), G::X("M20.5 50C31 40.5 69 40.5 79.5 50C69 58.5 31 58.5 20.5 50Z"), G::F(circ(50, 49.5, 9.5)),
           G::X(circ(50, 49.5, 3.4)), G::XS(burst, 1.4), G::X(circ(55.5, 45, 1.5))});
  }
}

// ---------------------------------------------------------------- крысолаки
void ratkin(Catalog& c) {
  c.group("Крысолаки", EmblemSection::Races);
  // Голова крысы: большие уши, косые злые глаза, резцы, усы, шрам.
  {
    const string ears = xf(ell(0, 0, 14, 16), place(25, 25, -20)) + xf(ell(0, 0, 14, 16), place(75, 25, 20));
    const string earIn = xf(ell(0, 0, 8, 10), place(23, 23, -20)) + xf(ell(0, 0, 8, 10), place(77, 23, 20));
    const string head = symSpline({{50, 22}, {38, 24}, {29, 33}, {25, 42}, {17, 50, 0}, {27, 52}, {21, 59, 0}, {32, 63}, {42, 77}, {47, 86}, {50, 88}});
    c.add("race-ratkin-head", "Голова крысы",
          {G::F(ears), G::X(earIn), G::XS(head, 2.6), G::F(head + rrect(46, 87, 3.7, 9, 1) + rrect(50.3, 87, 3.7, 9, 1)),
           G::S(sym("M40 78 13 71M41 82 9 83M43 86 15 94"), 1.7), G::X(sym("M33 44 45 50 43.5 54 35 50Z")), G::F(sym(circ(41.5, 50, 1.5))),
           G::XS("M44.5 82Q50 85.5 55.5 82", 1.8), G::XS("M57 36 67.5 56", 2.2), G::XS(sym("M44 27 46.5 33M39 30 42 35"), 1.5)});
  }
  // Крысиный король: шесть крыс головами наружу, хвосты сплетены узлом.
  {
    std::vector<L> v;
    const string rat = ell(0, -16, 7, 11) + "M-5.5 -24C-5 -32 -2 -37 0 -39C2 -37 5 -32 5.5 -24Z" + circ(-5, -27, 3.4) + circ(5, -27, 3.4);
    string rats, eyes, legs;
    std::vector<string> tails;
    for (int k = 0; k < 6; k++) {
      const double a = -90 + 60 * k;
      const Pt o = polar(50, 50, 11, a);
      const Affine m = place(o.x, o.y, float(a + 90 + 18), 0.95f);
      rats += xf(rat, m);
      eyes += xf(circ(-2.6, -31, 1.2) + circ(2.6, -31, 1.2), m);
      legs += xf("M-6 -22-12 -25M6 -22 12 -25M-6 -10-12 -6.5M6 -10 12 -6.5", m);
      tails.push_back("M" + ptS(m.apply({0, -6})) + "C" + pt(50, 50, 4, a + 80) + " " + pt(50, 50, 9, a + 160) + " " + pt(50, 50, 15, a + 200));
    }
    v.push_back(G::S(legs, 2.8));
    // Хвосты по одному: зазор вокруг каждого следующего — переплетение.
    for (const string& t : tails) {
      v.push_back(G::XS(t, 6.5));
      v.push_back(G::S(t, 3));
    }
    v.push_back(G::XS(rats, 2.4));
    v.push_back(G::F(rats));
    v.push_back(G::X(eyes));
    c.add("race-ratkin-king", "Крысиный король", v);
  }
  // Чумной колокол: треснувший колокол с когтистыми метками на балке-виселице.
  {
    const string bell = symSpline({{50, 20}, {42, 21}, {35, 27}, {33, 40}, {31, 56}, {27, 68}, {20, 76, 0}, {22, 81, 0}, {50, 81}});
    c.add("race-ratkin-bell", "Чумной колокол",
          {G::F(rrect(5, 7, 90, 8, 2) + rrect(10, 13, 8, 83, 2) + rrect(82, 13, 8, 83, 2) + rrect(6, 91, 16, 6, 1.5) + rrect(78, 91, 16, 6, 1.5) +
                sym("M18 15H28L18 25Z")),
           G::X(sym(circ(10, 11, 1.3) + circ(22, 11, 1.3))),
           G::S("M50 14V22", 4.5), G::F(circ(50, 88, 5.5) + rrect(48.5, 80, 3, 6, 1)), G::XS(bell, 2.6), G::F(bell),
           G::XS("M34 35C44 33 56 33 66 35M25.5 72C40 69 60 69 74.5 72", 2), G::XS("M63 77 59 69 63 63 58 55", 1.8),
           G::X("M38 42 41.5 41 47 63ZM46 40 49.5 39.5 53 62ZM54 42 57.5 41.5 59 61Z"), G::XS("M14 20V90M86 20V90", 1.6)});
  }
  // Ядовитый коготь: худая крысиная лапа тянется вниз, с крючковатых когтей капает яд.
  {
    const Pt ks[] = {{38, 36}, {46, 40}, {54, 40}, {62, 36}};
    const Pt ts[] = {{21, 61}, {40, 70}, {60, 70}, {79, 61}};
    string fingers, claws, joints;
    for (int i = 0; i < 4; i++) {
      const Pt k = ks[i], t = ts[i];
      const float dx = t.x - k.x, dy = t.y - k.y, len = std::hypot(dx, dy), ux = dx / len, uy = dy / len;
      fingers += taper(k, lerp(k, t, 0.33), lerp(k, t, 0.66), t, 8.5, 5.5);
      const float hx = t.x < 50 ? uy : -uy, hy = t.x < 50 ? -ux : ux;   // к середине
      const Pt tip{t.x + ux * 13 + hx * 7, t.y + uy * 13 + hy * 7};
      claws += "M" + ptS({t.x - hx * 3.6f, t.y - hy * 3.6f}) + "Q" + ptS({t.x + ux * 12 - hx * 2, t.y + uy * 12 - hy * 2}) + " " + ptS(tip) + "Q" +
               ptS({t.x + ux * 5 + hx * 4, t.y + uy * 5 + hy * 4}) + " " + ptS({t.x + hx * 3.6f, t.y + hy * 3.6f}) + "Z";
      for (float f : {0.45f, 0.95f}) {
        const Pt j = lerp(k, t, f);
        joints += "M" + ptS({j.x - uy * 5, j.y + ux * 5}) + "L" + ptS({j.x + uy * 5, j.y - ux * 5});
      }
    }
    auto drop = [](double x, double y, double s) {
      return xf("M0 -9C3.5 -4 5.5 -1 5.5 2A5.5 5.5 0 0 1 -5.5 2C-5.5 -1 -3.5 -4 0 -9Z", place(float(x), float(y), 0, float(s)));
    };
    c.add("race-ratkin-claw", "Ядовитый коготь",
          {G::F("M41 2H59L61 24H39Z" + ell(50, 31, 15, 11) + fingers + claws), G::XS(joints, 1.8), G::XS("M41 12H59M41 17H60", 1.6),
           G::F(drop(36, 86, 1) + drop(55, 91, 0.8) + drop(71, 82, 0.7))});
  }
}

// ---------------------------------------------------------------- ктониды
void chthonids(Catalog& c) {
  c.group("Ктониды", EmblemSection::Races);
  // Щупальца из бездны: три щупальца с присосками поднимаются из провала.
  {
    std::vector<L> v;
    v.push_back(G::F(ell(50, 84, 46, 12)));
    v.push_back(G::X(ell(50, 83, 37, 7.5)));
    struct Tn {
      std::vector<Pt> p;
      double w, side;
    };
    const Tn ts[] = {{{{50, 86}, {46, 66}, {56, 48}, {53, 30}, {44, 17}, {33, 15}, {29, 23}, {35, 28}}, 15, 1},
                     {{{28, 86}, {24, 72}, {14, 62}, {9, 48}, {13, 36}, {22, 35}, {23, 42}}, 11, -1},
                     {{{72, 86}, {77, 72}, {87, 63}, {91, 50}, {87, 39}, {78, 38}, {77, 45}}, 11, 1}};
    for (const Tn& t : ts) {
      const double w0 = t.w;
      const Tube tb = tube(t.p, [w0](double u) { return w0 * std::pow(1 - u, 0.85) + 0.6; }, 3);
      string suckers;
      for (const Tube::S& s : tb.at)
        if (s.u > 0.12 && s.u < 0.8) suckers += circ(s.x + s.nx * s.w * 0.2 * t.side, s.y + s.ny * s.w * 0.2 * t.side, std::max(1.1, s.w * 0.16));
      v.push_back(G::XS(tb.fill, 2.6));
      v.push_back(G::F(tb.fill));
      v.push_back(G::X(suckers));
    }
    const string lip = "M4 84C4 92 26 97 50 97C74 97 96 92 96 84C96 88.5 74 91.5 50 91.5C26 91.5 4 88.5 4 84Z";
    v.push_back(G::XS(lip, 2.4));
    v.push_back(G::F(lip));
    c.add("race-chthonids-tentacles", "Щупальца из бездны", v);
  }
  // Многоглазое существо: бугристая туша с большим глазом и россыпью малых, щупальца снизу.
  {
    const string blob = circ(50, 44, 31) + circ(29, 33, 14) + circ(71, 33, 14) + circ(33, 60, 14) + circ(67, 60, 14) + circ(50, 18, 12);
    string legs;
    const Pt lb[] = {{36, 68}, {45, 72}, {55, 72}, {64, 68}};
    const Pt lt[] = {{18, 94}, {38, 97}, {62, 97}, {82, 94}};
    for (int i = 0; i < 4; i++) {
      const double s = i < 2 ? -1 : 1;
      legs += taper(lb[i], {float(lb[i].x + 4 * s), lb[i].y + 10}, {float(lt[i].x - 8 * s), lt[i].y - 12}, lt[i], 9, 1);
    }
    struct E {
      double x, y, r;
    };
    const E es[] = {{29, 26, 5}, {71, 26, 5.5}, {50, 13, 4.5}, {21, 44, 4}, {79, 45, 4.5}, {34, 64, 4.5}, {66, 63, 4}, {50, 67, 3.2}};
    string holes, pupils;
    for (const E& e : es) {
      holes += circ(e.x, e.y, e.r);
      pupils += circ(e.x + e.r * 0.15, e.y + e.r * 0.1, e.r * 0.5);
    }
    c.add("race-chthonids-eyes", "Многоглазое существо",
          {G::F(blob + legs), G::X(holes + "M31 44C39 33 61 33 69 44C61 55 39 55 31 44Z"), G::F(pupils + circ(50, 44, 8)),
           G::X("M50 37C52.2 41 52.2 47 50 51C47.8 47 47.8 41 50 37Z")});
  }
  // Разлом: скала расколота зубчатой трещиной, из глубины смотрит око; обломки летят вверх.
  {
    const string rock = poly({3, 95, 5, 75, 11, 57, 21, 41, 33, 29, 45, 23, 55, 23, 67, 29, 79, 41, 89, 57, 95, 75, 97, 95});
    const string crack = "M48 18L45 27 49 33 44 40C40 45 36 50 32 54C36 58 40 62 44 67L46 75 42 83 47 90 45 97H55L52 90 56 83 53 75 56 67"
                         "C60 62 64 58 68 54C64 50 60 45 56 40L51 33 55 27 52 18Z";
    c.add("race-chthonids-rift", "Разлом",
          {G::F(rock + poly({38, 8, 44, 6, 45, 12, 39, 14}) + poly({57, 4, 63, 7, 59, 12}) + poly({27, 18, 31, 16, 32, 21}) +
                poly({70, 16, 75, 18, 72, 22})),
           G::X(crack), G::F(circ(50, 54, 9)), G::X("M50 46C52.2 50 52.2 58 50 62C47.8 58 47.8 50 50 46Z"), G::X(circ(54, 50.5, 1.4)),
           G::XS("M7 80 18 77 30 80 38 78M10 64 20 62 30 65M62 78 70 80 82 77 93 80M70 65 80 62 90 64M24 46 32 48 38 45M62 45 68 48 76 46", 1.8)});
  }
  // Хтонический идол: спрутоголовое божество со сложенными крыльями на ступенчатом постаменте.
  {
    const string head = symSpline({{50, 6}, {38, 8}, {30, 17}, {28, 30}, {31, 40}, {38, 45}, {50, 46}});
    const string wings = sym("M30 46C24 36 16 26 6 19C10 29 10 37 6 46C12 46 16 50 18 56C22 54 26 54 30 58Z");
    const string body = symSpline({{50, 44}, {36, 46}, {25, 54}, {21, 70}, {23, 82}, {50, 82}});
    string beard;
    const double xs[] = {38, 44, 50, 56, 62};
    const Pt tipsB[] = {{33, 66}, {41, 72}, {50, 75}, {59, 72}, {67, 66}};
    for (int i = 0; i < 5; i++) {
      const Pt b{float(xs[i]), 40}, t = tipsB[i];
      const float bend = float((t.x - b.x) * 0.6);
      beard += taperW(b, {b.x, 52}, {t.x - bend, t.y - 6}, t, [](double u) { return 6.5 * (1 - u) + 1; });
    }
    c.add("race-chthonids-idol", "Хтонический идол",
          {G::F(wings), G::XS(head + body, 2.6), G::F(body), G::XS(sym("M27 56C29 66 34 72 42 75M23 74C30 77 38 79 46 79"), 1.8),
           G::F(head), G::X(sym(xf(ell(0, 0, 4.6, 2.6), place(39.5, 28, 22)))), G::XS(beard, 2), G::F(beard),
           G::XS(sym("M40 12C36 16 34 22 34 27"), 1.6), G::F(rrect(15, 81, 70, 8, 1.5) + rrect(8, 89, 84, 8, 1.5)),
           G::XS("M15 89.5H85", 1.8), G::X(rrect(28, 84, 6, 2.2, 1) + rrect(44, 84, 12, 2.2, 1) + rrect(66, 84, 6, 2.2, 1)),
           G::XS(sym("M18 93H24M30 93H34"), 1.6)});
  }
}

// ---------------------------------------------------------------- лаваниды
void lavanids(Catalog& c) {
  c.group("Лаваниды", EmblemSection::Races);
  // Королева роя: венец на голове, усики, четыре крыла с жилками, длинное брюшко с кольцами.
  {
    std::vector<L> v;
    const Affine wu = place(25, 33, -28), wl = place(28, 52, 24);
    // Крылья перепончатые: кайма, прозрачная середина, жилки.
    v.push_back(G::F(sym(xf(ell(0, 0, 22, 8.5), wu))));
    v.push_back(G::X(sym(xf(ell(-0.5, 0, 19, 5.8), wu))));
    v.push_back(G::S(sym(xf("M-19 0H18M-6 0-14 -4.5M3 0-5 5M9 0 3 -5M14 0 10 4.5", wu)), 1.6));
    v.push_back(G::XS(sym(xf(ell(0, 0, 16, 6.5), wl)), 2.2));
    v.push_back(G::F(sym(xf(ell(0, 0, 16, 6.5), wl))));
    v.push_back(G::X(sym(xf(ell(-0.5, 0, 13.2, 4), wl))));
    v.push_back(G::S(sym(xf("M-13 0H13M-3 0-9 3.5M5 0 0 -3.5", wl)), 1.5));
    v.push_back(G::XS(sym("M42 47C36 33 30 25 24 22"), 2));
    v.push_back(G::S(sym("M44 45 35 52 31 62M45 49 37 60 37 72M46 53 41 66 43 79"), 3));
    const string abdomen = ell(50, 72, 12.5, 23), thorax = ell(50, 43, 9.5, 9.5);
    const string head = symSpline({{50, 15}, {42, 16}, {37, 23}, {40, 30}, {50, 34}});
    v.push_back(G::XS(abdomen + thorax + head, 2.6));
    v.push_back(G::F(abdomen));
    v.push_back(G::XS("M38.5 64Q50 68 61.5 64M38 73Q50 77 62 73M40 82Q50 86 60 82", 2));
    v.push_back(G::XS(thorax, 2.2));
    v.push_back(G::F(thorax));
    v.push_back(G::XS(head, 2.2));
    v.push_back(G::F(head + sym("M41.5 17.5 37 6 46 15Z") + "M47.5 15 50 3 52.5 15Z"));
    v.push_back(G::S(sym("M45 18C40 12 32 8 18 9"), 2.4));
    v.push_back(G::F(sym(circ(17, 9, 2.8))));
    v.push_back(G::X(sym(xf(ell(0, 0, 3.6, 5), place(42.5, 23, -18)))));
    v.push_back(G::S(sym("M46.5 33C44.5 36 46 39 49 39"), 2.2));
    c.add("race-lavanids-queen", "Королева роя", v);
  }
  // Жук-воин: рог, щит переднеспинки, надкрылья с бороздами, суставчатые лапы.
  {
    const string horn = "M46.5 27C46 19 45.5 11 43.5 5L48.5 9 50 2 51.5 9 56.5 5C54.5 11 54 19 53.5 27Z";
    const string pron = symSpline({{50, 29}, {40, 30}, {30, 34}, {27, 42}, {33, 48}, {50, 49}});
    const string ely = symSpline({{50, 50}, {37, 51}, {28, 59}, {27, 76}, {35, 90}, {50, 96}});
    c.add("race-lavanids-beetle", "Жук-воин",
          {G::S(sym("M32 39 18 33 12 20M30 57 14 57 6 69M33 75 19 85 17 96"), 4), G::S(sym("M12 20 9 15M12 20 15 15M6 69 3 74M6 69 8 75M17 96 13 94"), 2.4),
           G::F(sym("M18 33 14 37 16 30ZM14 57 12 62 10 55ZM19 85 22 89 16 88Z")), G::XS(ely + horn + ell(50, 28, 8, 6), 2.4),
           G::F(ely + horn + ell(50, 28, 8, 6)), G::XS(pron, 2.4), G::F(pron), G::XS("M50 50V96", 2.2),
           G::XS(sym("M44 56C40 66 40 79 44 90M37 58C33 67 33 78 37 85"), 1.6), G::X(sym(circ(44.5, 26, 1.7))), G::XS("M50 32V46", 1.4),
           G::X(sym(circ(38, 38, 1.4) + circ(35, 43, 1.1)))});
  }
  // Соты улья: семь шестиугольных ячеек — запечатанные, с яйцами и личинкой в середине.
  {
    const double R = 16, hx = R * std::sqrt(3.0) / 2;
    auto hex = [](double cx, double cy, double r) {
      string s;
      for (int k = 0; k < 6; k++) s += (k ? "L" : "M") + pt(cx, cy, r, -90 + 60 * k);
      return s + "Z";
    };
    struct Cell {
      double x, y;
      int kind;   // 0 — запечатана, 1 — открыта, 2 — с яйцом, 3 — с личинкой
    };
    const Cell cells[] = {{50, 50, 3},          {50 + 2 * hx, 50, 0},   {50 - 2 * hx, 50, 2},  {50 + hx, 50 - 1.5 * R, 2},
                          {50 - hx, 50 - 1.5 * R, 0}, {50 + hx, 50 + 1.5 * R, 0}, {50 - hx, 50 + 1.5 * R, 1}};
    string fill, open, caps, inner;
    for (const Cell& k : cells) {
      fill += hex(k.x, k.y, R - 1.3);
      if (k.kind == 0) caps += hex(k.x, k.y, R - 6.5);
      else open += hex(k.x, k.y, R - 5);
      if (k.kind == 2) inner += xf(ell(0, 0, 2.8, 4.6), place(float(k.x), float(k.y), 25));
    }
    c.add("race-lavanids-hive", "Соты улья",
          {G::F(fill), G::X(open), G::XS(caps, 1.6), G::F(inner), G::S(arc(50, 50.5, 5.2, -40, 215), 4.2), G::F(circ(54, 46.5, 2.6)),
           G::XS(sym("M50 50V57"), 1.2)});
  }
  // Жвала: голова жука-оленя сверху — огромные изогнутые жвала-клещи с зубцами, глаза, коленчатые усики.
  {
    const Pt m0{40, 62}, m1{14, 52}, m2{12, 18}, m3{45, 6};
    auto mw = [](double u) { return 14 * std::pow(1 - u, 0.75) + 0.7; };
    string teeth;
    for (auto [t, len] : {std::pair{0.42, 7.0}, std::pair{0.6, 4.5}, std::pair{0.74, 3.5}}) {
      const CurvePt q = bezier(m0, m1, m2, m3, t);
      const double h = mw(t) / 2 - 0.5, bx = q.x + q.nx * h, by = q.y + q.ny * h, tx = q.ny, ty = -q.nx;
      teeth += "M" + n(bx - tx * 3) + " " + n(by - ty * 3) + "L" + n(bx + q.nx * len + tx * 1.5) + " " + n(by + q.ny * len + ty * 1.5) + "L" +
               n(bx + tx * 3) + " " + n(by + ty * 3) + "Z";
    }
    const string mand = sym(taperW(m0, m1, m2, m3, mw) + teeth);
    const string head = symSpline({{50, 57}, {38, 57}, {29, 62}, {27, 72}, {32, 80}, {50, 82}});
    const string thorax = symSpline({{50, 82}, {32, 83}, {21, 87}, {20, 96, 0}, {50, 96}});
    c.add("race-lavanids-mandibles", "Жвала",
          {G::F(mand), G::S(sym("M30 66 19 63 13 53"), 2.8), G::F(sym(circ(13, 52, 2.4))), G::XS(head + thorax, 2.6), G::F(thorax), G::F(head),
           G::XS("M38 84Q50 88 62 84", 1.6), G::X(sym(xf(ell(0, 0, 3.6, 5), place(33, 69, 15)))), G::XS("M50 60V78M42 62Q50 66 58 62", 1.6),
           G::XS(sym(across(m0, m1, m2, m3, 0.14, 6, 6)), 1.6)});
  }
}

// ---------------------------------------------------------------- лесные эльфы
void woodElves(Catalog& c) {
  c.group("Лесные Эльфы", EmblemSection::Races);
  // Лесной олень: стоит в профиль, голова поднята, ветвистые рога с листьями.
  {
    const string body = spline({{28, 50}, {35, 44}, {50, 43}, {66, 42}, {76, 46}, {79, 54}, {74, 62}, {60, 64}, {44, 64}, {32, 62}, {27, 56}});
    const string neck = spline({{26, 55}, {21, 40}, {22, 29}, {30, 25}, {36, 34}, {39, 47}});
    const string head = spline({{34, 22}, {26, 19}, {18, 23}, {10, 31}, {7, 36, 0}, {12, 39}, {20, 37}, {29, 33}, {35, 28}});
    const string antlers = "M28 21C26 13 29 7 35 3M27.5 15 20 8M29.5 9.5 26 3M32 21C36 13 43 9 52 7M37 14 38 5M44 10 47 3";
    const string leaves = feather({20, 8}, {13, 4}, 5) + feather({26, 3}, {21, 1}, 4) + feather({52, 7}, {59, 4}, 5) + feather({47, 3}, {53, 1}, 4);
    c.add("race-wood-elves-stag", "Лесной олень",
          moved({G::S(antlers, 3.4), G::F(leaves), G::S("M33 61 30 94M40 62 39 94M70 61 74 76 70 94M64 62 66 77 62 94", 4.8),
                 G::F("M27 92H34V96H27ZM36 92H43V96H36ZM66 92H73V96H66ZM58 92H65V96H58Z"), G::XS(neck + head, 2.4), G::F(body + neck),
                 G::XS(head, 2), G::F(head + "M32 24 42 18 37 28Z" + "M77 47 83 43 81 50Z"), G::X(circ(21, 28, 1.7) + circ(9.5, 35, 1)),
                 G::XS("M38 46C34 50 33 56 34 62M70 46C66 52 65 58 67 63", 1.8), G::XS("M38 60C48 62 58 62 66 60", 1.6)},
                Affine::translate(6, 0)));
  }
  // Стрела в листе: стрела с листовидным наконечником пронзает большой лист.
  {
    const Affine leafM = rotAt(-32, 50, 50) * scaleAt(1.02f, 50, 50);
    const string leaf = xf("M50 3C66 17 72 35 68 55C64 71 57 81 50 91C43 81 36 71 32 55C28 35 34 17 50 3Z", leafM);
    const string veins = xf("M50 9V88M50 26 59 18M50 26 41 18M50 40 63 31M50 40 37 31M50 54 63 45M50 54 37 45M50 68 60 61M50 68 40 61", leafM);
    const string arrow = rrect(-1.8, -36, 3.6, 76, 1.8) + "M0 -50C4 -46 6 -42 6 -38C6 -35 3 -33 0 -31C-3 -33 -6 -35 -6 -38C-6 -42 -4 -46 0 -50Z" +
                         "M-1.8 26-9 31-9 43-1.8 38ZM1.8 26 9 31 9 43 1.8 38Z";
    const string ar = xf(arrow, place(52, 52, 45));
    c.add("race-wood-elves-arrow", "Стрела в листе",
          {G::F(leaf + xf("M48.6 88H51.4L52.5 97H47.5Z", leafM)), G::XS(veins, 1.8), G::XS(ar, 4), G::F(ar),
           G::XS(xf("M-9 35H9", place(52, 52, 45)), 1.4)});
  }
  // Сова на ветке: ушные пучки, большие глаза, крылья с перьями, когти.
  {
    const string owl = symSpline({{50, 17}, {42, 15}, {31, 5, 0}, {29, 18}, {24, 30}, {24, 46}, {28, 62}, {36, 76}, {44, 85}, {50, 88}});
    c.add("race-wood-elves-owl", "Сова",
          {G::F(rrect(6, 85, 88, 6, 3) + feather({12, 87}, {3, 76}, 7) + feather({88, 87}, {97, 96}, 7)), G::XS(owl, 2.6), G::F(owl),
           G::X(circ(39, 33, 10) + circ(61, 33, 10)), G::F(circ(39, 33, 7) + circ(61, 33, 7)), G::X(circ(39, 33, 3.2) + circ(61, 33, 3.2)),
           G::F(circ(40, 32, 1.1) + circ(62, 32, 1.1)), G::XS("M47 40 50 48 53 40Z", 1.8), G::F("M47 40 50 48 53 40Z"),
           G::XS(sym("M28 46C30 62 35 73 44 83"), 2.2), G::XS(sym("M29 56C31 58 33 58 35 56M31 64C33 66 35 66 37 64M34 71C36 73 38 73 40 71"), 1.6),
           G::XS("M44 56 47 59 50 56 53 59 56 56M42 64 45 67 48 64M52 64 55 67 58 64M45 72 48 75 51 72 54 75", 1.6),
           G::S(sym("M44 86V90M47 86V91"), 2.2)});
  }
  // Венок с луком: дубовые ветви с желудями, лук нацелен вверх.
  {
    // Дубовый лист (остриём к -y): выемки по краям, жилка; листья внахлёст с зазором.
    const string oak = "M0 0C-4 -3-5.5 -8-4.5 -12C-3.5 -16-1.5 -18.5 0 -20C1.5 -18.5 3.5 -16 4.5 -12C5.5 -8 4 -3 0 0Z";
    const string notch = circ(-4.7, -6.5, 1.5) + circ(4.7, -6.5, 1.5) + circ(-4.5, -12, 1.4) + circ(4.5, -12, 1.4);
    std::vector<L> v{G::S(sym(arc(50, 54, 36, 98, 236)), 2.8)};
    string acorns;
    for (int i = 0; i < 6; i++) {
      const double a = 106 + 22 * i, side = i % 2 ? 1 : -1;
      const Pt p = polar(50, 54, 36 + 2.5 * side, a);
      const Affine m = place(p.x, p.y, float(a + 180 + 34 * side));
      const string lf = sym(xf(oak, m));
      v.push_back(G::XS(lf, 1.8));
      v.push_back(G::F(lf));
      v.push_back(G::X(sym(xf(notch, m))));
      v.push_back(G::XS(sym(xf("M0 -2V-16", m)), 1.2));
      if (i % 2 == 0 && i > 0) {
        const Pt q = polar(50, 54, 29.5, a + 11);
        acorns += xf(ell(0, 1.5, 2.8, 3.6) + rrect(-3.6, -3.5, 7.2, 3.4, 1.4), place(q.x, q.y, float(a - 90)));
      }
    }
    for (const L& l : std::vector<L>{G::F(sym(acorns)), G::S("M50 94V88", 3), G::S("M20 58C27 44 40 39 50 39C60 39 73 44 80 58", 5),
                                     G::S("M20 58C18 61 15 62 12 60M80 58C82 61 85 62 88 60", 3), G::S("M20.5 58.5 50 70 79.5 58.5", 1.6),
                                     G::S("M50 70V16", 3), G::F("M50 4 56 17H44Z" + string("M50 66 45 76V64ZM50 66 55 76V64Z")),
                                     G::XS("M46.5 39V44M53.5 39V44", 1.4)})
      v.push_back(l);
    c.add("race-wood-elves-wreath", "Венок с луком", v);
  }
}

// ---------------------------------------------------------------- луниды
void lunids(Catalog& c) {
  c.group("Луниды", EmblemSection::Races);
  // Лунный мотылёк: широкие передние крылья, хвостатые задние, серповидные глазки, перистые усики.
  {
    const string fore = "M47 31C36 19 20 11 5 13C4 25 10 39 22 46C32 50 42 48 47 44Z";
    const string hind = spline({{48, 47}, {36, 50}, {25, 58}, {23, 68}, {30, 74}, {29, 84}, {21, 97, 0}, {35, 86}, {41, 76}, {47, 63}});
    const string antenna = "M48 24C44 16 40 10 33 5";
    string barbs;
    for (double t : {0.25, 0.45, 0.65, 0.85}) {
      const CurvePt q = bezier({48, 24}, {44, 16}, {40, 10}, {33, 5}, t);
      barbs += "M" + n(q.x) + " " + n(q.y) + "L" + n(q.x - 3.4) + " " + n(q.y + 1.2);
    }
    c.add("race-lunids-moth", "Лунный мотылёк",
          {G::F(sym(fore + hind)), G::XS(sym("M46 36C36 30 24 24 12 19M46 41C38 40 28 40 18 42M47 52C40 58 33 64 28 72"), 1.6),
           G::X(sym(circ(25, 30, 6.5) + circ(33, 63, 4.6))), G::F(sym(circ(27.5, 31.5, 5) + circ(35, 64.5, 3.4))), G::XS(sym(ell(50, 50, 5, 20)), 2.4),
           G::F(ell(50, 50, 5, 20) + circ(50, 27, 4.5)), G::XS("M45 44H55M45 50H55M45.5 56H54.5M46.5 62H53.5", 1.4), G::S(sym(antenna), 2.2),
           G::S(sym(barbs), 1.6)});
  }
  // Око полумесяца: глаз в чаше полумесяца, лучи-ресницы, звёзды.
  {
    string lashes;
    for (int k = 0; k < 5; k++) {
      const double a = -150 + 30 * k;
      lashes += seg(polar(50, 44, 17, a), polar(50, 44, 24, a));
    }
    c.add("race-lunids-eye", "Око полумесяца",
          {G::F(circ(50, 57, 40)), G::X(circ(50, 41, 36)), G::S(lashes, 3), G::F("M29 44C37 33 63 33 71 44C63 55 37 55 29 44Z"),
           G::X("M34.5 44C41 37 59 37 65.5 44C59 51 41 51 34.5 44Z"), G::F(circ(50, 44, 6.5)), G::X(circ(50, 44, 2.6)),
           G::F(star(50, 9, 7, 1.8, 4, -90) + star(28, 15, 4.5, 1.2, 4, -90) + star(72, 15, 4.5, 1.2, 4, -90)),
           G::X(sym(circ(30, 76, 1.6) + circ(40, 84, 1.2)))});
  }
  // Лунный цветок: пятилепестковый цветок в чаше полумесяца.
  {
    std::vector<K> pts;
    for (int k = 0; k < 5; k++) {
      const double a = -90 + 72 * k;
      for (auto [da, r, t] : {std::tuple{-22.0, 30.0, 1.0}, std::tuple{0.0, 36.0, 1.0}, std::tuple{22.0, 30.0, 1.0}, std::tuple{36.0, 19.0, 0.5}})
        pts.push_back({polar(50, 42, r, a + da).x, polar(50, 42, r, a + da).y, t});
    }
    const string flower = spline(pts);
    string pleats;
    for (int k = 0; k < 5; k++) pleats += seg(polar(50, 42, 8, -90 + 72 * k), polar(50, 42, 29, -90 + 72 * k));
    c.add("race-lunids-flower", "Лунный цветок",
          {G::F(circ(50, 50, 46)), G::X(circ(50, 38, 44)), G::XS(flower, 3.4), G::F(flower), G::XS(pleats, 1.8),
           G::XS(star(50, 42, 14, 7, 5, -54), 1.6), G::X(circ(50, 42, 4.5)), G::F(circ(50, 42, 2.2))});
  }
  // Фазы луны: полная луна с кратерами между растущим и убывающим серпами, звёзды.
  c.add("race-lunids-phases", "Фазы луны",
        {G::F(circ(25, 50, 22) + circ(75, 50, 22)), G::X(circ(33, 50, 19.5) + circ(67, 50, 19.5)), G::XS(circ(50, 50, 21), 4),
         G::F(circ(50, 50, 21)), G::X(circ(44, 44, 4.2) + circ(57.5, 55, 5) + circ(55, 40, 2.6) + circ(42, 58, 2.4) + circ(60, 46, 1.6)),
         G::F(star(50, 14, 7, 1.8, 4, -90) + star(50, 86, 7, 1.8, 4, -90) + star(18, 20, 4, 1, 4, -90) + star(82, 80, 4, 1, 4, -90))});
}

// ---------------------------------------------------------------- лунные эльфы
void moonElves(Catalog& c) {
  c.group("Лунные Эльфы", EmblemSection::Races);
  // Дева полумесяца: профиль эльфийки с острым ухом и диадемой в чаше полумесяца, волосы стекают в серп.
  {
    // Профиль вправо: лоб, переносица, нос, губы, подбородок, длинная шея; затылок и волосы уходят в серп.
    const string face = spline({{60, 93, 0}, {57, 80}, {55, 66}, {56, 60}, {61, 57}, {63.5, 54}, {62.5, 51.5}, {65, 49.5}, {63.5, 47.5}, {65.5, 46},
                                {68.5, 42, 0}, {64.5, 35.5}, {63.5, 32.5}, {63, 27}, {59, 20}, {51, 16}, {42, 18}, {36, 25}, {35, 36},
                                {38, 48}, {43, 60}, {45, 70}, {44, 93, 0}});
    const string hair = taper({37, 24}, {27, 36}, {28, 56}, {18, 76}, 9, 2) + taper({40, 40}, {33, 56}, {36, 70}, {29, 88}, 7, 2);
    c.add("race-moon-elves-maiden", "Дева полумесяца",
          moved({G::F(circ(46, 50, 45)), G::X(circ(62, 46, 39)), G::F(hair), G::XS(face, 2.6), G::F(face + "M44 41 30 27 47 35Z"),
                 G::XS("M44.5 39.5 36 31.5 44 35.5", 1.4), G::XS("M57.5 33.5C59.5 34.5 61 34.5 62 33.5", 1.4),
                 G::XS("M44 19C51 21 57 24 62 28.5", 1.8), G::X("M53 21 55 24 52.5 26.5 50.5 23.5Z"), G::XS("M45 63C49 66 53 67 56 66", 1.4),
                 G::F(star(84, 16, 8, 2, 4, -90) + star(88, 74, 4.5, 1.2, 4, -90))},
                Affine::translate(2, 0)));
  }
  // Лунная сова: сова с распахнутыми крыльями перед полной луной.
  {
    std::vector<L> v;
    v.push_back(G::F(circ(50, 38, 31)));
    const Pt bases[] = {{40, 58}, {38, 55}, {37, 52}, {37, 49}, {38, 46}, {40, 43}};
    const Pt tips[] = {{31, 82}, {20, 77}, {11, 69}, {5, 58}, {3, 46}, {5, 34}};
    std::vector<string> fs;
    for (int k = 0; k < 6; k++) fs.push_back(feather(bases[k], tips[k], 11));
    fs.push_back("M44 42C34 34 20 30 6 30C12 38 20 46 30 52C36 55 41 57 45 58Z");
    const string owl = ell(50, 61, 11, 17) + circ(50, 41, 12) + "M44 76 50 88 56 76Z";
    v.push_back(G::XS(sym(fs.back()) + owl, 3.4));
    layeredFeathers(v, fs, 2.2f, true);
    v.push_back(G::XS(owl, 2.4));
    v.push_back(G::F(owl));
    v.push_back(G::XS("M50 31C44 31 40 35 40 41C40 47 45 52 50 54C55 52 60 47 60 41C60 35 56 31 50 31Z", 1.8));
    v.push_back(G::X(circ(45, 40, 2.6) + circ(55, 40, 2.6)));
    v.push_back(G::XS("M48.5 45 50 49 51.5 45", 1.4));
    v.push_back(G::XS("M45 62 47.5 64.5 50 62 52.5 64.5 55 62M45.5 69 48 71.5 50.5 69 53 71.5", 1.6));
    v.push_back(G::S(sym("M46 77 43 84M46 77 46 85"), 2.4));
    c.add("race-moon-elves-owl", "Лунная сова", moved(v, Affine::translate(0, 4)));
  }
  // Звёздный лук: лук-полумесяц, стрела со звездой на острие.
  c.add("race-moon-elves-bow", "Звёздный лук",
        {G::F(crescent(50, 62, 41, 50, 74, 39)), G::S("M11 74.5 50 90 89 74.5", 1.8), G::XS("M50 92V18", 6), G::S("M50 92V18", 3),
         G::F(star(50, 11, 10, 3.6, 4, -90) + "M50 88 44 97V84ZM50 88 56 97V84Z"), G::X(circ(50, 11, 1.8)),
         G::XS("M43 22.5V28M57 22.5V28", 1.6), G::X(sym(circ(30, 30, 1.5) + circ(18, 44, 1.3))),
         G::F(star(22, 12, 4.5, 1.2, 4, -90) + star(78, 12, 4.5, 1.2, 4, -90))});
  // Лунный олень: олень в прыжке над чашей полумесяца.
  {
    const string stag = spline({{30, 42}, {40, 36}, {56, 35}, {70, 36}, {78, 41}, {76, 48}, {66, 52}, {48, 53}, {36, 52}}) +
                        spline({{36, 44}, {28, 30}, {26, 22}, {32, 19}, {38, 30}, {42, 40}}) +
                        spline({{33, 20}, {26, 17}, {18, 20}, {12, 26, 0}, {17, 28}, {25, 27}, {32, 25}});
    c.add("race-moon-elves-stag", "Лунный олень",
          {G::F(circ(50, 52, 46)), G::X(circ(50, 38, 43)), G::XS(stag + "M38 50 22 58 14 54M42 51 30 62 22 64M68 48 84 54 94 50M72 46 88 46 96 40", 6),
           G::S("M38 50 22 58 14 54M42 51 30 62 22 64M68 48 84 54 94 50M72 46 88 46 96 40", 4),
           G::S("M29 18C27 11 30 6 36 3M28.5 12.5 22 7M33 19C37 12 43 9 50 8M38 12 40 5", 3.2), G::F(stag + "M31 21 39 16 35 25Z" + "M78 40 84 36 82 43Z"),
           G::X(circ(22, 23, 1.5)), G::F(star(84, 14, 6, 1.6, 4, -90) + star(64, 8, 3.5, 1, 4, -90))});
  }
}

// ---------------------------------------------------------------- люди
void humans(Catalog& c) {
  c.group("Люди", EmblemSection::Races);
  // Королевский лев: лев на задних лапах (стоящий на дыбах) с короной, хвост с кистью.
  {
    // Грива — языки-пряди назад и вниз от головы.
    string mane = circ(37, 27, 11.5);
    for (int i = 0; i < 8; i++) {
      const double a = -75 + 27 * i, r = 21 - std::abs(i - 3.5) * 0.8;
      mane += flame(polar(37, 27, 5, a), polar(37, 27, r, a + 12), 11, 3);
    }
    const string torso = xf(ell(0, 0, 13, 23), place(52, 54, -30)) + ell(42, 39, 11, 11);
    const string foreA = taper({42, 38}, {36, 34}, {29, 35}, {21, 39}, 9, 6.5) + circ(20, 40, 4.6);
    const string foreB = taper({46, 47}, {40, 51}, {33, 55}, {26, 57}, 9, 6.5) + circ(24.5, 57.5, 4.6);
    const string hindA = taper({60, 63}, {66, 74}, {60, 84}, {54, 92}, 12, 6.5) + ell(50, 93.5, 7, 3.4);
    const string hindB = taper({57, 68}, {50, 76}, {43, 78}, {35, 80}, 11, 6.5) + circ(33.5, 80, 4.6);
    const string tail = taper({64, 62}, {82, 64}, {74, 40}, {83, 26}, 5, 3) + flame({83, 29}, {81, 11}, 10, 2) + flame({84, 29}, {93, 17}, 8, -3) +
                        flame({82, 29}, {73, 18}, 8, 3);
    // Голова в профиль: лоб, нос, раскрытая пасть с языком, подбородок.
    const string head = spline({{38, 15}, {29, 16}, {22, 19}, {15, 23, 0}, {15.5, 27.5, 0}, {22, 28.5}, {26, 30.5, 0}, {18, 33, 0}, {19.5, 37, 0},
                                {28, 38}, {36, 35}, {41, 26}});
    const string crown = "M28 15 26.5 5 32 10 37 2.5 42 10 47.5 5 46 15Z";
    const string claws = "M17 37 13 36M16 40 12 40.5M17 43 13.5 45M21 55 17 54M20.5 58 16.5 58.5M21.5 61 18 63M30 77 26.5 76M29.5 80 25.5 80.5M30 83 27 85";
    c.add("race-humans-lion", "Королевский лев",
          moved({G::F(tail), G::XS(torso + hindA, 2.4), G::F(hindB), G::XS(torso + hindA, 2.4), G::F(torso + hindA), G::XS(foreB, 2.2),
                 G::F(foreB), G::XS(mane + foreA, 2.2), G::F(foreA), G::F(mane), G::S(claws, 1.8), G::XS(head + crown, 2.2),
                 G::F(head + crown + "M24 30 15 31.5 12 29.5 13.5 33.5 19 33Z"), G::XS("M18 31.8 24.5 30.6", 1.2),
                 G::X(xf(ell(0, 0, 2.2, 1.4), place(27, 23, -15)) + circ(32.5, 9, 1.1) + circ(37, 7.5, 1.1) + circ(41.5, 9, 1.1)),
                 G::XS("M30 33C33 31 35 27 35 23", 1.5), G::XS("M38 34C34 38 34 44 37 48M62 62C58 66 58 72 61 76", 1.6)},
                Affine::translate(-3, 1)));
  }
  // Крепость с флагами: две башни с коническими крышами и вымпелами, стена с зубцами, ворота.
  {
    const string tower = rrect(10, 32, 21, 61, 1) + "M6 33 20.5 10 35 33Z" + rrect(19.5, 2, 2, 10, 0.8) + "M21.5 2.5 32 5.5 21.5 8.5Z";
    const string wall = rrect(28, 48, 44, 45, 0) + "M28 42H34V48H28ZM39 42H45V48H39ZM55 42H61V48H55ZM66 42H72V48H66Z" + "M44 44 50 30 56 44Z";
    c.add("race-humans-fortress", "Крепость с флагами",
          {G::F(wall), G::XS(sym(tower), 2.6), G::F(sym(tower) + rrect(4, 90, 92, 6, 2)),
           G::X("M40 92V74C40 68 44.5 64 50 64C55.5 64 60 68 60 74V92Z" + sym(rrect(18.5, 44, 4, 10, 2) + rrect(18.5, 64, 4, 10, 2)) + rrect(47.5, 52, 5, 7, 2.5)),
           G::S("M44 66V92M50 64V92M56 66V92M41 76H59M40.5 84H59.5", 1.6), G::XS(sym("M8 33.5H33"), 1.6), G::XS("M28 48.5H72", 1.6)});
  }
  // Мечи под короной: два меча крест-накрест, корона с жемчужинами над ними.
  {
    const string sword = "M50 4 56 14V70H44V14Z" + rrect(30, 68, 40, 6, 3) + rrect(46.5, 74, 7, 15, 2) + circ(50, 92, 5);
    std::vector<L> v = crossed(sword, "", "M50 16V66M46.5 79H53.5M46.5 84H53.5", 1.6, 36, 50, 56, 0.86f, 4.5);
    const string crown = "M30 27 26 12 36.5 19 43 7 50 16 57 7 63.5 19 74 12 70 27Z" + rrect(29, 25, 42, 7, 2) + circ(26, 10, 3) + circ(43, 5, 3) +
                         circ(57, 5, 3) + circ(74, 10, 3);
    v.push_back(G::XS(crown, 4));
    v.push_back(G::F(crown));
    v.push_back(G::X(circ(39, 28.5, 1.8) + "M50 26 52.5 28.5 50 31 47.5 28.5Z" + circ(61, 28.5, 1.8)));
    c.add("race-humans-swords", "Мечи под короной", moved(v, Affine::translate(0, 0)));
  }
  // Двуглавый орёл: головы в коронах смотрят в стороны, крылья подняты, хвост веером.
  {
    std::vector<L> v;
    const Pt bases[] = {{41, 50}, {39, 46}, {38, 42}, {38, 38}, {40, 34}};
    const Pt tips[] = {{14, 63}, {6, 50}, {3, 37}, {4, 24}, {10, 11}};
    std::vector<string> fs;
    for (int k = 0; k < 5; k++) fs.push_back(feather(bases[k], tips[k], 11));
    fs.push_back("M46 34C38 28 28 26 16 26C20 36 26 44 34 50C38 52 42 54 46 54Z");
    layeredFeathers(v, fs, 2.2f, true);
    const string tail = sym("M46 70 36 92 44 89 47 95 50 90Z");
    const string head = sym(circ(35, 19, 6.8) + "M30 15.5C25.5 14.5 21 16.5 20 21.5C20.5 23.5 22.5 24 23.5 22.5C24 20.5 26 19.5 29.5 20Z" +
                            "M28 21.5 22.5 26 22 24.5Z" + taper({45, 36}, {42, 29}, {38, 25}, {36, 21}, 8, 7) +
                            "M29.5 13 28.5 4.5 32.5 8.5 35 2.5 37.5 8.5 41.5 4.5 40.5 13Z");
    const string body = ell(50, 53, 11, 18);
    const string shield = "M42.5 44H57.5V54C57.5 60 54 63.5 50 65.5C46 63.5 42.5 60 42.5 54Z";
    v.push_back(G::XS(tail + body + head, 2.4));
    v.push_back(G::F(tail + body + head));
    v.push_back(G::X(sym(circ(33.5, 17.5, 1.5)) + sym(rrect(28.5, 11.5, 13, 1.8, 0.6))));
    v.push_back(G::S(sym("M44 67 37 77M37 77H31M37 77 33 82M37 77 38 83"), 3));
    v.push_back(G::XS(shield, 1.8));
    v.push_back(G::XS("M50 47.5V61M45.5 52H54.5", 1.6));
    v.push_back(G::XS(sym("M46 74 41 88M48.5 74 47 90"), 1.4));
    c.add("race-humans-eagle", "Двуглавый орёл", v);
  }
}

// ---------------------------------------------------------------- людоящеры
// Яйцо (верх уже низа) с центром (cx, cy) и полуосями rx, ry.
string eggShape(double cx, double cy, double rx, double ry) {
  return spline({{cx, cy - ry}, {cx + 0.72 * rx, cy - 0.62 * ry}, {cx + rx, cy + 0.1 * ry}, {cx + 0.72 * rx, cy + 0.76 * ry}, {cx, cy + ry},
                 {cx - 0.72 * rx, cy + 0.76 * ry}, {cx - rx, cy + 0.1 * ry}, {cx - 0.72 * rx, cy - 0.62 * ry}});
}

void lizardfolk(Catalog& c) {
  c.group("Людоящеры", EmblemSection::Races);
  // Воин-ящер: стоит в профиль с копьём, гребень на голове, длинный хвост.
  {
    const string spear = rrect(22.3, 16, 3.4, 80, 1.5) + "M24 2C28 7 29.5 12 27.5 17L24 21 20.5 17C18.5 12 20 7 24 2Z" + rrect(20, 19.5, 8, 3, 1);
    const string head = spline({{56, 13}, {47, 9.5}, {38, 11}, {29, 15.5, 0}, {30.5, 20}, {39, 21.5}, {47, 25}, {56, 22}});
    const string crest = "M47 11 56 2 56 13ZM54 13 66 8 60 19ZM57 20 69 19 60 26Z";
    const string torso = spline({{46, 22}, {57, 24}, {63, 34}, {63, 46}, {59, 58}, {49, 61}, {44, 50}, {44, 36}});
    const string tail = taper({58, 55}, {74, 63}, {86, 75}, {96, 94}, 13, 1.5);
    c.add("race-lizardfolk-warrior", "Воин-ящер",
          moved({G::F(tail), G::S("M59 59 66 73 60 85 68 94", 7), G::F("M64 92H74L72 96H64Z"), G::XS(torso, 2.4), G::F(torso),
                 G::XS("M53 58 45 72 53 84 44 94", 10), G::S("M53 58 45 72 53 84 44 94", 7), G::F("M40 92H50L49 96H39Z"), G::XS(head, 2.2),
                 G::F(head + crest), G::X(circ(43.5, 14.5, 1.7) + circ(31.5, 16.5, 0.9)), G::XS("M31.5 19.5C38 20.5 44 21.5 50 22.5", 1.4),
                 G::XS(spear, 2.8), G::F(spear), G::XS("M52 32 40 42 26 36M57 39 47 52 26 56", 9), G::S("M52 32 40 42 26 36M57 39 47 52 26 56", 6),
                 G::F(circ(25.5, 36, 4) + circ(25.5, 56, 4)), G::XS("M47 30 51 32M46 38 51 40M47 46 52 48M48 53 53 55", 1.6),
                 G::XS(across({58, 55}, {74, 63}, {86, 75}, {96, 94}, 0.25, 6, 6) + across({58, 55}, {74, 63}, {86, 75}, {96, 94}, 0.45, 5, 5) +
                           across({58, 55}, {74, 63}, {86, 75}, {96, 94}, 0.65, 4, 4),
                       1.6)},
                Affine::translate(-7, 0)));
  }
  // Череп крокодила (сверху): глазницы, височные окна, зубы по краям длинной морды.
  {
    const string skull = symSpline({{50, 5}, {38, 6}, {26, 11}, {21, 21}, {25, 32}, {34, 41}, {38, 52}, {40, 66}, {42, 80}, {43, 88}, {46, 95}, {50, 96}});
    string teeth, pits;
    for (int i = 0; i < 7; i++) {
      const double y = 49 + 6 * i, x = 38.5 + (y - 52) * 5 / 36;
      teeth += "M" + n(x + 1) + " " + n(y - 2) + "L" + n(x - 4) + " " + n(y + 2.5) + "L" + n(x + 1) + " " + n(y + 2) + "Z";
      if (i < 6) pits += circ(45, y + 3, 0.9);
    }
    c.add("race-lizardfolk-skull", "Череп крокодила",
          {G::F(sym(teeth)), G::F(skull), G::X(sym(xf(ell(0, 0, 6.5, 9), place(35, 26, -20)))), G::X(sym(xf(ell(0, 0, 3, 4), place(42, 12, 30)))),
           G::X(ell(50, 90, 3, 2.2)), G::X(sym(pits)), G::XS("M50 36V84", 1.4), G::XS(sym("M30 42C36 40 42 40 46 42M28 8C34 14 40 18 46 19"), 1.6)});
  }
  // Болотный храм: хижина на сваях под крутой соломенной крышей, лестница к воде, камыши.
  c.add("race-lizardfolk-temple", "Болотный храм",
        {G::S(waves(4, 96, 95, 4.5, 6), 3), G::F(rrect(19, 66, 4, 24, 1) + rrect(35, 66, 4, 24, 1) + rrect(61, 66, 4, 24, 1) + rrect(77, 66, 4, 24, 1)),
         G::S("M46 68V88M54 68V88", 1.8), G::S("M46 73H54M46 78H54M46 83H54", 1.6), G::F(rrect(12, 63, 76, 5, 1.5) + rrect(24, 45, 52, 19, 1)),
         G::X("M44 64V55C44 51.5 46.5 49.5 50 49.5C53.5 49.5 56 51.5 56 55V64Z"), G::XS("M27 50H41M27 55H41M27 60H41M59 50H73M59 55H73M59 60H73", 1.4),
         G::XS("M50 8 88 47H12Z", 3), G::F("M50 8 88 47H12Z"), G::XS("M30 29H70M22 38H78M50 8V47", 1.6), G::S("M42 2 58 17M58 2 42 17", 2.8),
         G::S(sym("M8 94C8 82 9 72 8 58M12 94C14 84 18 76 22 70"), 1.8), G::F(sym(rrect(5.5, 50, 5, 12, 2.5)))});
  // Яйца в гнезде: три пятнистых яйца в плетёном гнезде, на среднем — трещина.
  {
    const string eggL = xf(eggShape(0, 0, 11, 15), place(31, 50, -20)), eggR = xf(eggShape(0, 0, 11, 15), place(69, 50, 20));
    const string eggC = eggShape(50, 38, 14, 20);
    const string nest = "M4 56C8 80 28 94 50 94C72 94 92 80 96 56C84 64 68 67 50 67C32 67 16 64 4 56Z";
    // Плетение: изогнутые прутья вдоль чаши и редкие перевязи.
    const string weave = "M10 72C26 81 74 81 90 72M17 81C32 88 68 88 83 81M27 88C40 92 60 92 73 88" +
                         string("M22 70 24 79M40 74 41 84M60 74 59 84M78 70 76 79M33 82 34 89M67 82 66 89");
    c.add("race-lizardfolk-egg", "Яйца в гнезде",
          moved({G::F(eggL + eggR), G::X(xf(circ(-3, -5, 1.8) + circ(3, 2, 1.4) + circ(-2, 6, 1.2), place(31, 50, -20)) +
                                         xf(circ(3, -5, 1.8) + circ(-3, 2, 1.4) + circ(2, 6, 1.2), place(69, 50, 20))),
                 G::XS(eggC, 2.6), G::F(eggC), G::X(circ(45, 33, 2) + circ(55, 44, 1.6) + circ(47, 50, 1.3) + circ(56, 30, 1.2)),
                 G::XS("M41 24 45 28 43 31 48 34 46 38", 1.8), G::XS(nest, 3), G::F(nest + "M4 56 0.5 50 8 55ZM96 56 99.5 50 92 55Z"),
                 G::XS(weave, 1.4), G::XS("M8 63C22 71 36 74 50 74C64 74 78 71 92 63", 1.8)},
                Affine::translate(0, -5)));
  }
}

// ---------------------------------------------------------------- минотавры
void minotaurs(Catalog& c) {
  c.group("Минотавры", EmblemSection::Races);
  // Голова с кольцом: рога вверх с кольцами, чёлка, тяжёлый лоб, злые глаза, большое кольцо в носу.
  {
    const Pt h0{37, 29}, h1{20, 31}, h2{7, 22}, h3{12, 3};
    const string horn = taper(h0, h1, h2, h3, 12, 2);
    const string head = symSpline({{50, 22}, {38, 23}, {31, 30}, {31, 42}, {35, 54}, {36, 64}, {34, 74}, {38, 84}, {50, 88}});
    c.add("race-minotaurs-head", "Голова с кольцом",
          {G::F(sym(horn + "M32 36 13 41 31 47Z")), G::XS(sym(across(h0, h1, h2, h3, 0.5, 4, 4) + across(h0, h1, h2, h3, 0.62, 3.5, 3.5)), 1.6),
           G::XS(head, 2.2), G::F(head + "M38 25 42 12 45 20 50 9 55 20 58 12 62 25Z"), G::X(sym("M34 45 45 49.5 43.5 53 35.5 49.5Z")),
           G::XS(sym("M33 40.5 46 45.5"), 2), G::X(sym(xf(ell(0, 0, 2.6, 3.8), place(43, 77, -15)))), G::XS("M39 70Q50 66 61 70", 1.6),
           G::XS("M50 27V40", 1.6), G::XS(circ(50, 87, 7.5), 6.5), G::S(circ(50, 87, 7.5), 3.2)});
  }
  // Лабрис: двусторонняя секира с лезвиями-полумесяцами, остриё на древке, обмотка.
  {
    const string blade = crescent(30, 44, 26, 46, 44, 24);
    const string blades = blade + xf(blade, Affine{-1, 0, 0, 1, 100, 0}) + rrect(20, 40, 60, 8, 2);
    const string shaft = rrect(46.5, 8, 7, 84, 3) + "M50 1 55.5 10H44.5Z" + rrect(42, 33, 16, 22, 3) + ell(50, 93, 5.5, 3.5);
    c.add("race-minotaurs-labrys", "Лабрис",
          {G::F(blades), G::XS(sym(arc(30, 44, 20, 140, 220)), 1.8), G::X(sym(circ(36, 24, 1.8) + circ(36, 64, 1.8) + circ(13, 44, 2.2))),
           G::XS(shaft, 3), G::F(shaft), G::XS("M46.5 66H53.5M46.5 71H53.5M46.5 76H53.5M46.5 81H53.5", 1.5), G::X(rrect(46, 38, 8, 12, 2)),
           G::F("M50 40 52.5 44 50 48 47.5 44Z")});
  }
  // Лабиринт: концентрические стены с проходами и перемычками, в середине — рога.
  {
    string walls;
    const double gaps[] = {90, 270, 90, 270, 90, 270};
    for (int k = 0; k < 6; k++) {
      const double r = 9 + 6.4 * k, g = gaps[k], half = std::max(9.0, 300.0 / r);
      walls += arc(50, 50, r, g + half, g + 360 - half);
      if (k < 5) {
        const double a = g + (k % 2 ? -1 : 1) * (half + 22);
        walls += seg(polar(50, 50, r, a), polar(50, 50, r + 6.4, a));
      }
    }
    c.add("race-minotaurs-labyrinth", "Лабиринт",
          {G::S(walls, 3.6), G::F("M43 46C43 51 46 54 50 54C54 54 57 51 57 46C55 48.5 52.5 49.5 50 49.5C47.5 49.5 45 48.5 43 46Z")});
  }
  // Рога над щитом: геральдический щит с каймой и кольцом, над ним — бычьи рога.
  {
    const Pt h0{27, 38}, h1{12, 34}, h2{5, 18}, h3{16, 4};
    const string shield = "M22 36H78V56C78 74 66 86 50 95C34 86 22 74 22 56Z";
    c.add("race-minotaurs-horns", "Рога над щитом",
          {G::F(sym(taper(h0, h1, h2, h3, 13, 1.5))), G::XS(sym(across(h0, h1, h2, h3, 0.45, 5, 5) + across(h0, h1, h2, h3, 0.6, 4, 4)), 1.6),
           G::XS(shield, 3), G::F(shield), G::XS("M28 42H72V56C72 71 62 81 50 88C38 81 28 71 28 56Z", 2), G::XS(circ(50, 61, 9.5), 4),
           G::F(circ(50, 61, 2.4))});
  }
}

// ---------------------------------------------------------------- морозные эльфы
string snowflake(double cx, double cy, double r) {
  string s;
  for (int k = 0; k < 6; k++) {
    const double a = -90 + 60 * k;
    s += "M" + n(cx) + " " + n(cy) + "L" + pt(cx, cy, r, a);
    const Pt q = polar(cx, cy, r * 0.55, a);
    s += "M" + pt(q.x, q.y, r * 0.32, a - 45) + "L" + ptS(q) + "L" + pt(q.x, q.y, r * 0.32, a + 45);
  }
  return s;
}

void frostElves(Catalog& c) {
  c.group("Морозные Эльфы", EmblemSection::Races);
  // Снежная корона: обруч с самоцветами, ледяные зубцы-кристаллы, снежинка над средним.
  {
    std::vector<L> v;
    struct Cr {
      Pt b, t;
      double w;
    };
    const Cr cs[] = {{{18, 74}, {8, 55}, 7},     {{82, 74}, {92, 55}, 7},     {{27, 74}, {20, 42}, 9},
                     {{73, 74}, {80, 42}, 9},    {{38, 75}, {34, 31}, 10.5},  {{62, 75}, {66, 31}, 10.5}, {{50, 76}, {50, 30}, 13}};
    for (const Cr& k : cs) {
      string f;
      const string s = crystal(k.b, k.t, k.w, &f, 0.8);
      v.push_back(G::XS(s, 2.2));
      v.push_back(G::F(s));
      v.push_back(G::XS(f, 1.4));
    }
    const string band = "M12 70C30 79 70 79 88 70L86 80C68 89 32 89 14 80Z";
    v.push_back(G::XS(band, 2.4));
    v.push_back(G::F(band));
    v.push_back(G::X(poly({50, 79, 53, 82.5, 50, 86, 47, 82.5}) + circ(34, 81, 1.8) + circ(66, 81, 1.8) + circ(22, 77, 1.4) + circ(78, 77, 1.4)));
    v.push_back(G::S(snowflake(50, 15, 12), 3));
    c.add("race-frost-elves-crown", "Снежная корона", moved(v, Affine::translate(0, 4)));
  }
  // Ледяной волк: голова анфас, острые уши, гребни инея на щеках, снежный знак на лбу.
  {
    const string head = symSpline({{50, 20}, {42, 21}, {32, 27}, {27, 37}, {17, 45, 0}, {26, 50}, {15, 58, 0}, {27, 61}, {20, 70, 0}, {33, 71},
                                   {39, 82}, {45, 92}, {50, 95}});
    c.add("race-frost-elves-wolf", "Ледяной волк",
          {G::F(head + sym("M36 31 25 4 47 22Z")), G::X(sym("M37 27 30 11 42 23Z")), G::X(sym("M33 45 45 50 44 53 35 50Z")),
           G::X(star(50, 34, 8, 2.4, 4, -90)), G::XS(sym("M45 56 47.5 82"), 1.6), G::XS("M43.5 85Q50 89 56.5 85", 1.8),
           G::XS(sym("M24 52 31 55M22 63 30 65M28 72 34 72"), 1.6), G::F(sym(circ(41.5, 49.5, 1.2)))});
  }
  // Клинок-сосулька: ледяной клинок остриём вниз, гарда из кристаллов, сосульки.
  {
    string facets;
    const string guard = crystal({45, 31}, {9, 20}, 12, &facets, 0.75) + crystal({45, 34}, {19, 45}, 9, &facets, 0.75);
    const string blade = "M37 36H63L60 48 63.5 52 58 64 61 68 55 80 56.5 84 50 98 43.5 84 45 80 39 68 42 64 36.5 52 40 48Z";
    const string hilt = rrect(45, 11, 10, 20, 2.5) + poly({50, 1, 57, 7, 50, 13, 43, 7});
    c.add("race-frost-elves-blade", "Клинок-сосулька",
          {G::F(sym(guard + "M23 26 27 26 25 38ZM30.5 29 34.5 29 32.5 41Z")), G::XS(sym(facets), 1.4), G::XS(blade + hilt + circ(50, 33, 8), 2.6),
           G::F(blade + hilt + circ(50, 33, 8)), G::XS("M50 43V90", 1.8), G::XS("M45 16H55M45 20.5H55M45 25H55", 1.5),
           G::X(poly({50, 28, 54.5, 33, 50, 38, 45.5, 33})), G::XS("M43.5 7H56.5", 1.3), G::XS(sym("M40 48 46 50M42 64 47 66M45 80 48.5 81"), 1.5)});
  }
  // Ледяная лиса: свернулась клубком, пушистый хвост укрывает лапы, над ней снежинка.
  {
    const Pt t0{86, 70}, t1{86, 90}, t2{38, 94}, t3{13, 77};
    const string tail = taperW(t0, t1, t2, t3, [](double u) { return 4 + 17 * std::pow(std::sin(kPi * std::pow(u, 0.75)), 0.7); });
    const string head = circ(32, 52, 13) + "M22 51C16 55 12 60 13 63C18 64 25 62 31 59Z" + "M26 43 22 25 36 37Z" + "M35 40 40 25 45 40Z";
    c.add("race-frost-elves-fox", "Ледяная лиса",
          {G::F(ell(56, 58, 34, 25)), G::XS("M58 34C70 36 80 44 84 54", 1.8), G::XS(tail, 2.8), G::F(tail),
           G::XS(across(t0, t1, t2, t3, 0.82, 12, 12), 2), G::XS(head, 2.6), G::F(head), G::X("M27 30 29.5 39 33 36Z"),
           G::XS("M20 49Q24 51.5 28 49", 1.6), G::F(circ(13.5, 62, 1.6)), G::S(snowflake(82, 16, 11), 3)});
  }
}

// ---------------------------------------------------------------- морские эльфы
void seaElves(Catalog& c) {
  c.group("Морские Эльфы", EmblemSection::Races);
  // Корабль-лебедь: нос — лебединая шея, корма завитком, паруса-крылья, волны.
  {
    const string hull = "M10 58C18 74 80 78 92 56L86 56C70 61 32 62 14 57Z";
    const string neck = taper({15, 58}, {6, 48}, {8, 30}, {18, 24}, 7.5, 5.5) + xf(ell(0, 0, 6.5, 4.6), place(21, 22, 15)) + "M26 21 33.5 24 26 26Z";
    const string stern = taper({86, 57}, {95, 50}, {95, 38}, {88, 34}, 6, 2);
    const string sails = "M53 10C68 16 79 31 81 50C72 48 62 50 53 54Z" + string("M49 15C39 23 33 35 32 49C38 49 44 50 49 52Z");
    c.add("race-sea-elves-ship", "Корабль-лебедь",
          {G::S(waves(4, 96, 86, 4.5, 5), 3), G::S(waves(14, 86, 95, 4, 4), 2.6), G::S("M51 62V9", 3), G::F("M51 4 61 6.5 51 9Z"), G::F(sails),
           G::XS("M56 19C64 25 70 33 73 43M56 31C62 35 66 41 68 47M46 24C41 31 38 38 37 45", 1.6), G::XS(hull + neck + stern, 2.6),
           G::F(hull + neck + stern), G::X(circ(23, 20.5, 1.2) + circ(32, 64, 2) + circ(44, 66, 2) + circ(56, 66, 2) + circ(68, 64, 2)),
           G::XS("M16 61C32 66 70 67 86 59", 1.4)});
  }
  // Рог-раковина: витой конус от мундштука к раструбу, шипы на завитках, из раструба расходится звук.
  {
    const Pt r0{10, 90}, r1{20, 64}, r2{38, 44}, r3{64, 34};
    auto rw = [](double u) { return 4.5 + 25 * std::pow(u, 1.35); };
    const string cone = taperW(r0, r1, r2, r3, rw);
    string bands, spikes;
    for (double t : {0.3, 0.45, 0.6, 0.74, 0.87}) {
      const CurvePt q = bezier(r0, r1, r2, r3, t), q2 = bezier(r0, r1, r2, r3, t + 0.06);
      const double h = rw(t) / 2 + 0.5;
      bands += "M" + n(q.x - q.nx * h) + " " + n(q.y - q.ny * h) + "Q" + n(q.x) + " " + n(q.y) + " " + n(q2.x + q2.nx * h) + " " + n(q2.y + q2.ny * h);
      // Шипы — по выпуклой (внешней) стороне изгиба: против нормали.
      const double tx = q.ny, ty = -q.nx, ox = -q.nx, oy = -q.ny;
      spikes += "M" + n(q.x + ox * (h - 1) - tx * 2.5) + " " + n(q.y + oy * (h - 1) - ty * 2.5) + "L" + n(q.x + ox * (h + 6) + tx * 1.5) + " " +
                n(q.y + oy * (h + 6) + ty * 1.5) + "L" + n(q.x + ox * (h - 1) + tx * 2.5) + " " + n(q.y + oy * (h - 1) + ty * 2.5) + "Z";
    }
    const Affine mouth = place(65, 33, -21);
    c.add("race-sea-elves-conch", "Рог-раковина",
          {G::F(cone + spikes + xf(ell(0, 0, 6.5, 19), mouth) + xf(rrect(-5, -4, 6, 8, 2), place(10, 90, -69))), G::XS(bands, 1.8),
           G::X(xf(ell(1.5, 0, 3.6, 13.5), mouth)), G::S(xf(arc(0, 0, 22, -58, 58) + arc(0, 0, 31, -50, 50), mouth), 3)});
  }
  // Дельфин: прыжок дугой над волнами, спинной и грудной плавники, хвост-лопасти.
  {
    const Pt d0{13, 66}, d1{19, 22}, d2{66, 6}, d3{82, 52};
    auto dw = [](double u) { return 3 + 15 * std::pow(std::sin(kPi * std::pow(u, 0.7)), 0.85); };
    const string body = taperW(d0, d1, d2, d3, dw);
    auto fin = [&](double u, double side, double len, double base) {
      const CurvePt q = bezier(d0, d1, d2, d3, u);
      const double h = dw(u) / 2 - 1, tx = q.ny, ty = -q.nx, bx = q.x + q.nx * h * side, by = q.y + q.ny * h * side;
      return "M" + n(bx - tx * base) + " " + n(by - ty * base) + "Q" + n(bx + q.nx * side * len * 0.8 + tx * base * 0.2) + " " +
             n(by + q.ny * side * len * 0.8 + ty * base * 0.2) + " " + n(bx + q.nx * side * len + tx * base * 1.4) + " " +
             n(by + q.ny * side * len + ty * base * 1.4) + "L" + n(bx + tx * base) + " " + n(by + ty * base) + "Z";
    };
    const string fluke = xf("M0 0C5 -5 11 -11 17 -12C15 -6 11 -2 7 0C11 2 15 6 17 12C11 11 5 5 0 0Z", place(81, 52, 64, 0.88f));
    c.add("race-sea-elves-dolphin", "Дельфин",
          {G::S(waves(4, 96, 92, 5, 5), 3.2), G::F(body + fin(0.5, -1, 11, 5) + fin(0.28, 1, 9, 3.5) + fluke),
           G::X(circ(21.5, 46, 1.6)), G::XS("M14 62C17 58 19 55 22 53", 1.4), G::XS(across(d0, d1, d2, d3, 0.2, 9, 9), 1.4),
           G::F(xf(eggShape(0, 0, 2.6, 3.6), place(8, 80, 0)) + xf(eggShape(0, 0, 2, 2.8), place(14, 86, 0)) + circ(90, 74, 2.2))});
  }
  // Волна и жемчужина: завиток большой волны с пенными когтями, в его полости — жемчужина.
  {
    const string wave = "M4 96L4 86C12 80 26 74 38 71C52 68 64 62 68 50C71 40 66 30 57 27C47 24 37 28 33 34C31 37 27 37 26 33"
                        "C26 24 36 13 50 9C66 5 84 9 92 22C97 30 97 40 96 50L96 96Z";
    string claws;
    for (double t : {0.15, 0.35, 0.55, 0.75}) {
      const CurvePt q = bezier({26, 33}, {26, 24}, {36, 13}, {50, 9}, t);
      claws += "M" + n(q.x + q.ny * 3) + " " + n(q.y - q.nx * 3) + "Q" + n(q.x - q.nx * 6) + " " + n(q.y - q.ny * 6) + " " + n(q.x - q.nx * 8 - q.ny * 3) +
               " " + n(q.y - q.ny * 8 + q.nx * 3) + "Q" + n(q.x - q.nx * 3) + " " + n(q.y - q.ny * 3) + " " + n(q.x - q.ny * 3) + " " +
               n(q.y + q.nx * 3) + "Z";
    }
    c.add("race-sea-elves-pearl", "Волна и жемчужина",
          {G::F(wave + claws), G::XS("M44 86C60 80 74 70 80 54C84 44 84 34 80 26M62 92C76 86 86 74 89 58M20 90C34 84 46 82 56 80", 1.8),
           G::XS("M40 18C50 13 62 12 74 15", 1.6), G::F(circ(46, 49, 10)), G::X(circ(42.5, 45.5, 2.6)), G::XS(arc(46, 49, 6.5, 20, 100), 1.4)});
  }
}

// ---------------------------------------------------------------- наги
void nagasRace(Catalog& c) {
  c.group("Наги", EmblemSection::Races);
  // Королева наг: корона, капюшон кобры за головой, руки подняты, хвост кольцом.
  {
    const string hood = symSpline({{50, 3}, {40, 5}, {30, 13}, {25, 27}, {28, 41}, {38, 49}, {50, 52}});
    const Tube tail = tube({{50, 60}, {44, 73}, {30, 79}, {24, 86}, {36, 92}, {62, 92}, {80, 86}, {84, 77}, {76, 69}, {70, 73}},
                           [](double u) { return 15 * std::pow(1 - u, 0.7) + 1.5; }, 2);
    string bands;
    for (size_t i = 1; i < tail.at.size(); i += 2) {
      const Tube::S& s = tail.at[i];
      if (s.u < 0.85) bands += "M" + n(s.x - s.nx * s.w / 2) + " " + n(s.y - s.ny * s.w / 2) + "L" + n(s.x + s.nx * s.w * 0.15) + " " + n(s.y + s.ny * s.w * 0.15);
    }
    const string torso = symSpline({{50, 32}, {44, 34}, {37, 39}, {38, 47}, {43, 57}, {44, 64}, {50, 66}});
    const string head = ell(50, 24, 7, 9) + "M42.5 17 41.5 8 45.5 12 50 4.5 54.5 12 58.5 8 57.5 17Z";
    c.add("race-nagas-queen", "Королева наг",
          {G::F(hood), G::XS(sym("M44 10C38 16 34 24 33 32M40 8C33 13 29 21 28 30"), 1.6), G::X(sym(xf(ell(0, 0, 2.4, 4), place(33, 38, 20)))),
           G::XS(tail.fill, 2.6), G::F(tail.fill), G::XS(bands, 1.4), G::XS(torso + head, 2.4), G::F(torso),
           G::XS(sym("M37.5 40 27 48 20 37"), 8), G::S(sym("M37.5 40 27 48 20 37"), 5), G::F(sym(circ(19.5, 35.5, 3.2))), G::F(head),
           G::X(sym("M45.5 23 48.5 24 46 25.5Z") + circ(50, 12.5, 1.2)), G::XS("M44 37C47 40 53 40 56 37", 1.6), G::XS("M45 52H55", 1.4)});
  }
  // Змей и меч: змей обвивает клинок, голова над острием с раздвоенным языком.
  {
    const string sword = "M50 12 56 21V68H44V21Z" + rrect(28, 66, 44, 7, 3) + rrect(46.5, 73, 7, 15, 2) + circ(50, 92, 5);
    std::vector<string> front, back;
    std::vector<Pt> pts;
    std::vector<double> ws;
    bool curFront = true;
    const int N = 160;
    for (int i = 0; i <= N; i++) {
      const double t = double(i) / N, ph = 2 * kPi * 2 * t;
      const Pt p{float(50 + 18 * std::sin(ph)), float(63 - 40 * t)};
      const bool fr = std::cos(ph) > 0;
      if (i > 0 && fr != curFront) {
        pts.push_back(p);
        ws.push_back(3.5 + 5 * t);
        (curFront ? front : back).push_back(ribbon(pts, ws));
        pts = {p};
        ws = {3.5 + 5 * t};
      } else {
        pts.push_back(p);
        ws.push_back(3.5 + 5 * t);
      }
      curFront = fr;
    }
    (curFront ? front : back).push_back(ribbon(pts, ws));
    string fr, bk;
    for (const string& s : front) fr += s;
    for (const string& s : back) bk += s;
    // Шея поднимается от последнего витка над остриём, голова смотрит вправо-вверх.
    const string neck = taper({50, 23.5}, {47, 18}, {47, 14}, {51, 10}, 8.5, 7.5);
    const string head = "M47 12C47 6 52 2.5 58 2.5C63 2.5 66 5 65 8.5C64 12 57 15 51 15Z";
    c.add("race-nagas-sword", "Змей и меч",
          {G::F(bk), G::XS(sword, 3), G::F(sword), G::XS("M50 24V64M46.5 77H53.5M46.5 82H53.5", 1.5), G::XS(fr + neck + head, 2.6),
           G::F(fr + neck + head), G::X(circ(58, 6.5, 1.4)), G::S("M65 6.5 71 5M71 5 75 2.5M71 5 75 7.5", 1.4), G::X("M50 89 53 92 50 95 47 92Z")});
  }
  // Змеиный глаз: круглая радужка с вертикальным зрачком в ромбе из чешуи.
  {
    // Сетка чешуи вдоль сторон ромба: семь рядов в каждую сторону.
    string grid;
    for (int j = 1; j < 7; j++) {
      const double f = j / 7.0;
      grid += seg(lerp({50, 3}, {3, 50}, f), lerp({97, 50}, {50, 97}, f)) + seg(lerp({50, 3}, {97, 50}, f), lerp({3, 50}, {50, 97}, f));
    }
    string iris;
    for (int k = 0; k < 16; k++) iris += seg(polar(50, 50, 11, k * 22.5), polar(50, 50, 17.5, k * 22.5));
    c.add("race-nagas-eye", "Змеиный глаз",
          {G::F(poly({50, 3, 97, 50, 50, 97, 3, 50})), G::XS(grid, 1.5), G::X(circ(50, 50, 25)), G::F(circ(50, 50, 21)), G::XS(iris, 1.3),
           G::X("M50 31C55 40 55 60 50 69C45 60 45 40 50 31Z"), G::X(circ(58.5, 40.5, 2.6)), G::XS(arc(50, 50, 25, 200, 340), 4)});
  }
  // Морской храм: колонны и ярусы под луковичным куполом с трезубцем, ступени уходят в волны.
  c.add("race-nagas-temple", "Морской храм",
        {G::S(waves(4, 96, 95, 4, 6), 3), G::F(rrect(8, 80, 84, 7, 1.5) + rrect(14, 73, 72, 8, 1.5) + rrect(16, 44, 68, 30, 1) +
                                                rrect(12, 37, 76, 8, 1.5) + rrect(20, 30, 60, 8, 1.5) + "M32 31C32 19 42 13 50 8C58 13 68 19 68 31Z"),
         G::S("M50 9V2M46 5V2.5M54 5V2.5M46 5H54", 1.8),
         G::X(rrect(25, 47, 4, 24, 1) + rrect(35, 47, 4, 24, 1) + rrect(61, 47, 4, 24, 1) + rrect(71, 47, 4, 24, 1) +
              "M43 73V58C43 54 46 51 50 51C54 51 57 54 57 58V73Z"),
         G::XS("M12 45H88M20 38H80M14 81H86", 1.6), G::XS("M42 30C42 22 46 16 50 12M58 30C58 22 54 16 50 12", 1.6),
         G::F(sym("M12 37C8 33 8 27 12 25C16 27 16 33 12 37Z"))});
}

}  // namespace

void raceEmblemsB(Catalog& c) {
  quetzals(c);
  kitsune(c);
  kobolds(c);
  crystallites(c);
  bloodElves(c);
  ratkin(c);
  chthonids(c);
  lavanids(c);
  woodElves(c);
  lunids(c);
  moonElves(c);
  humans(c);
  lizardfolk(c);
  minotaurs(c);
  frostElves(c);
  seaElves(c);
  nagasRace(c);
}

}  // namespace rg::gfx::emb
