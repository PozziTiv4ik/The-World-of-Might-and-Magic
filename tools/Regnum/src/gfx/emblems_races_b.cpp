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

// Ось лопасти от b к t: кубическая кривая, контрольные точки сдвинуты вбок на k1 и k2 длины (разные знаки — S-изгиб).
struct Ax {
  Pt p0, c1, c2, p1;
};
Ax ax(Pt b, Pt t, double k1 = 0, double k2 = 0) {
  const double dx = t.x - b.x, dy = t.y - b.y;
  return {b, {float(b.x + dx / 3 - dy * k1), float(b.y + dy / 3 + dx * k1)}, {float(b.x + dx * 2 / 3 - dy * k2), float(b.y + dy * 2 / 3 + dx * k2)}, t};
}
template <class Fn>
string band(const Ax& a, Fn w, int steps = 32) {
  return taperW(a.p0, a.c1, a.c2, a.p1, w, steps);
}
// Лопасть (лист, перо): заострена с обоих концов, шире к основанию; w — наибольшая ширина.
string leaf(const Ax& a, double w) {
  return band(a, [w](double u) { return w * std::pow(std::sin(kPi * std::pow(u, 0.8)), 0.75) + 0.2; });
}
// Язык (пламя, прядь, коготь): ширина w у основания, острый кончик.
string tongue(const Ax& a, double w, double pw = 0.9) {
  return band(a, [w, pw](double u) { return w * std::pow(1 - u, pw) + 0.2; });
}
// Линия по оси от t0 до t1 (вырезы: стержень пера, прожилка, хребет).
string axLine(const Ax& a, double t0, double t1, int steps = 14) {
  string s;
  for (int i = 0; i <= steps; i++) {
    const CurvePt q = bezier(a.p0, a.c1, a.c2, a.p1, t0 + (t1 - t0) * i / steps);
    s += (i ? "L" : "M") + n(q.x) + " " + n(q.y);
  }
  return s;
}
// Отрезок поперёк оси при параметре t: на a в одну сторону и b в другую.
string axAcross(const Ax& a, double t, double l, double r) { return across(a.p0, a.c1, a.c2, a.p1, t, l, r); }
// Передний слой: зазор вокруг и заливка.
void front(std::vector<L>& v, const string& d, float gap = 2.2f) {
  v.push_back(G::XS(d, gap));
  v.push_back(G::F(d));
}
// Перо-лопасть со стержнем, поверх уже нарисованного (с зазором).
void plume(std::vector<L>& v, const Ax& a, double w, float gap = 2.f, float rib = 1.4f) {
  front(v, leaf(a, w), gap);
  v.push_back(G::XS(axLine(a, 0.14, 0.78), rib));
}
// Чечевица (глаз-прорезь, лист, звено): длина 2·len, высота 2·h, поворот deg.
string lens(double cx, double cy, double len, double h, double deg = 0) {
  return xf("M" + n(-len) + " 0Q0 " + n(-2 * h) + " " + n(len) + " 0Q0 " + n(2 * h) + " " + n(-len) + " 0Z", place(float(cx), float(cy), float(deg)));
}
// Яйцо (верх уже низа) с центром (cx, cy) и полуосями rx, ry.
string eggShape(double cx, double cy, double rx, double ry) {
  return spline({{cx, cy - ry}, {cx + 0.72 * rx, cy - 0.62 * ry}, {cx + rx, cy + 0.1 * ry}, {cx + 0.72 * rx, cy + 0.76 * ry}, {cx, cy + ry},
                 {cx - 0.72 * rx, cy + 0.76 * ry}, {cx - rx, cy + 0.1 * ry}, {cx - 0.72 * rx, cy - 0.62 * ry}});
}

// Огонёк: язык пламени от круглого донца (b, r) к острию t.
string wisp(Pt b, Pt t, double r, double k1, double k2) { return tongue(ax(b, t, k1, k2), 2 * r, 1.1) + circ(b.x, b.y, r); }

// ---------------------------------------------------------------- кецали
void quetzals(Catalog& c) {
  c.group("Кецали", EmblemSection::Races);
  // Пернатый змей: тело S-изгибом с брюшными щитками, голова в профиль с раскрытой пастью и клыком, за головой —
  // гребень из длинных перьев, хвост — веер перьев.
  {
    std::vector<L> v;
    const std::pair<Pt, Pt> crest[] = {{{50, 29}, {6, 38}}, {{49, 24}, {4, 22}}, {{50, 19}, {10, 7}}, {{53, 15}, {25, 2}}};
    for (auto [b, t] : crest) plume(v, ax(b, t, 0.1, 0.03), 9.5, 2.2f);
    const Tube body = tube({{60, 30}, {48, 40}, {34, 48}, {25, 60}, {30, 73}, {45, 79}, {60, 79}, {72, 82}, {80, 90}},
                           [](double u) { return 2.5 + 12 * std::pow(1 - u, 0.75); }, 3);
    string scutes;
    for (const Tube::S& s : body.at)
      if (s.u > 0.1 && s.u < 0.86)
        scutes += "M" + n(s.x - s.nx * (s.w / 2 + 1)) + " " + n(s.y - s.ny * (s.w / 2 + 1)) + "L" + n(s.x - s.nx * s.w * 0.08) + " " +
                  n(s.y - s.ny * s.w * 0.08);
    const std::pair<Pt, Pt> tail[] = {{{79, 89}, {97, 76}}, {{79, 90}, {98, 92}}, {{78, 91}, {88, 99}}};
    for (auto [b, t] : tail) plume(v, ax(b, t, -0.08), 7, 2.f, 1.2f);
    const string head = spline({{50, 17}, {58, 10}, {68, 8}, {78, 10.5}, {87, 13.5}, {94, 17, 0}, {90, 21}, {81, 22.5}, {72, 24.5}, {64, 28, 0},
                                {74, 29.5}, {85, 31.5}, {91, 33.5, 0}, {84, 37}, {72, 39}, {62, 39}, {54, 35}, {49, 26}});
    front(v, body.fill + head, 2.4f);
    v.push_back(G::XS(scutes, 1.5));
    v.push_back(G::F(tongue(ax({82, 22}, {80.5, 29}, 0.1), 2.8, 0.8)));
    v.push_back(G::X(lens(70, 15.5, 4.2, 1.4, 10) + lens(88.5, 16.5, 1.7, 0.7, 25)));
    v.push_back(G::XS("M59 12.5C65 10.5 72 10.5 78 12.5M66 33C73 34.5 80 35 86 35", 1.4));
    c.add("race-quetzals-serpent", "Пернатый змей", v);
  }
  // Птица кецаль на ветке: хохолок вперёд, сложенное крыло, длинные кроющие перья хвоста S-изгибом до низа.
  {
    std::vector<L> v;
    const string branch = band(ax({3, 68}, {74, 60}, 0.02, 0.03), [](double u) { return 6 - 3 * u; }) + leaf(ax({13, 67}, {6, 86}, -0.15, -0.05), 7) +
                          leaf(ax({22, 66}, {30, 80}, 0.12), 5.5);
    v.push_back(G::F(branch));
    v.push_back(G::XS(axLine(ax({13, 67}, {6, 86}, -0.15, -0.05), 0.15, 0.75) + axLine(ax({22, 66}, {30, 80}, 0.12), 0.15, 0.75), 1.2));
    const Ax s1 = ax({55, 55}, {96, 95}, 0.2, -0.16), s2 = ax({51, 58}, {80, 99}, 0.22, -0.14), s3 = ax({57, 53}, {98, 78}, 0.12, -0.12);
    for (const Ax* a : {&s3, &s1, &s2}) front(v, band(*a, [](double u) { return 9 * std::pow(1 - u, 0.8) + 0.4; }));
    const string body = spline({{10, 26, 0}, {16, 21.5}, {16.5, 14}, {21, 8.5}, {29, 5.5}, {37, 8}, {42, 14},
                                {45, 22}, {51, 33}, {55, 45}, {58, 55, 0}, {51, 59}, {43, 59}, {34, 55}, {26, 47}, {22, 38}, {20, 31}, {16, 28.5}});
    front(v, body);
    const string wing = spline({{31, 25}, {42, 27}, {52, 37}, {58, 49}, {65, 65, 0}, {55, 59}, {44, 51}, {35, 41}, {29, 32}});
    front(v, wing, 2.f);
    v.push_back(G::XS("M36 43C43 50 51 55 59 59M39 36C46 42 52 47 58 53M41 30C47 34 51 38 54 43", 1.5));
    v.push_back(G::XS(axLine(s1, 0.08, 0.6) + axLine(s2, 0.08, 0.6), 1.3));
    v.push_back(G::F(tongue(ax({38, 58}, {35, 64.5}, -0.2), 3.2) + tongue(ax({44, 58}, {42, 64.5}, -0.2), 3.2)));
    v.push_back(G::X(lens(24.5, 21.5, 2.6, 1.1, -8)));
    v.push_back(G::XS("M12 26.5 19 25M20 14C25 11 32 10.5 39 13.5", 1.3));
    c.add("race-quetzals-bird", "Птица кецаль", v);
  }
  // Солнечный камень: кайма с насечками, восемь длинных и восемь коротких лучей, диск с крестом движения.
  {
    string notches, rays, ribs, petals;
    for (int k = 0; k < 40; k++) notches += seg(polar(50, 50, 45.6, k * 9 + 4.5), polar(50, 50, 49, k * 9 + 4.5));
    for (int k = 0; k < 8; k++) {
      const double a = -90 + 45 * k, b = a + 22.5;
      rays += "M" + pt(50, 50, 22, a - 10) + "L" + pt(50, 50, 40.5, a) + "L" + pt(50, 50, 22, a + 10) + "Z";
      rays += "M" + pt(50, 50, 22, b - 7) + "L" + pt(50, 50, 33, b) + "L" + pt(50, 50, 22, b + 7) + "Z";
      ribs += seg(polar(50, 50, 25.5, a), polar(50, 50, 35, a));
    }
    for (int k = 0; k < 4; k++) petals += lens(polar(50, 50, 10.5, 45 + 90 * k).x, polar(50, 50, 10.5, 45 + 90 * k).y, 6.5, 2.6, 45 + 90 * k);
    c.add("race-quetzals-sun", "Солнечный камень",
          {G::F(circ(50, 50, 48.5)), G::X(circ(50, 50, 42.5)), G::XS(circ(50, 50, 44.6), 1.4), G::XS(notches, 1.6), G::F(rays + circ(50, 50, 23)),
           G::XS(ribs, 1.4), G::XS(circ(50, 50, 19.5), 1.6), G::X(petals + star(50, 50, 6, 2.4, 4, 0)), G::XS(sym(seg({50, 23}, {50, 29})), 1.4)});
  }
  // Маска из перьев: полукруглый веер перьев, налобная лента с нефритами, строгий лик с ушными вставками.
  {
    std::vector<L> v;
    const int order[] = {0, 10, 1, 9, 2, 8, 3, 7, 4, 6, 5};
    for (int k : order) {
      const double a = 195 + 15 * k, len = 47 - 1.2 * std::abs(k - 5);
      plume(v, ax(polar(50, 54, 10, a), polar(50, 54, len, a)), 11.5, 2.2f, 1.5f);
    }
    const string diadem = "M17 53C27 38 73 38 83 53L78 58C68 46 32 46 22 58Z";
    front(v, diadem, 2.4f);
    v.push_back(G::X(circ(32, 46.5, 2.2) + circ(50, 42.5, 2.6) + circ(68, 46.5, 2.2)));
    const string face = symSpline({{50, 47}, {39, 47.5}, {31, 52}, {30, 64}, {33, 76}, {41, 88}, {50, 95}});
    const string spool = circ(23, 64, 6.5) + circ(77, 64, 6.5) + sym(lens(23, 76, 1.8, 3.5));
    front(v, face + spool, 2.6f);
    v.push_back(G::X(circ(23, 64, 2.8) + circ(77, 64, 2.8)));
    v.push_back(G::XS(sym("M33 56.5 46 60"), 2));
    v.push_back(G::X(sym(lens(39.5, 63.5, 5.2, 1.4, 6)) + rrect(42, 81.5, 16, 2.6, 1.3)));
    v.push_back(G::XS("M48 61.5 46.5 74 50 76 53.5 74 52 61.5", 1.4));
    v.push_back(G::XS(sym("M33 70 39 74.5M35 76 40.5 80"), 1.5));
    c.add("race-quetzals-mask", "Маска из перьев", v);
  }
}

// ---------------------------------------------------------------- кицунэ
void kitsune(Catalog& c) {
  c.group("Кицунэ", EmblemSection::Races);
  // Девятихвостая лиса: сидит в профиль, морда поднята; за спиной веером девять хвостов-языков пламени.
  {
    std::vector<L> v;
    const Pt tips[] = {{46, 3}, {62, 3}, {77, 8}, {88, 18}, {95, 31}, {97, 46}, {96, 60}, {93, 73}, {88, 84}};
    for (int k = 8; k >= 0; k--) {
      const Ax a = ax(lerp({47, 58}, {55, 88}, k / 8.0), tips[k], 0.16, -0.22);
      front(v, band(a, [](double u) { return 15 * std::pow(std::sin(kPi * std::pow(u, 0.55)), 0.75) + 1.4 * (1 - u); }), 2.2f);
      v.push_back(G::XS(axAcross(a, 0.8, 7, 7), 1.5));
    }
    const string fox = spline({{6, 31, 0}, {14, 27}, {21, 22}, {25, 19}, {27, 16.5, 0}, {29, 3, 0}, {35, 15, 0}, {38, 6, 0}, {41, 18.5, 0},
                               {43, 25}, {45, 35}, {48, 46}, {55, 58}, {60, 70}, {61.5, 82}, {58, 92}, {53, 97, 0}, {31, 97, 0}, {19, 97, 0},
                               {20, 93}, {23, 89}, {23, 76}, {22, 62}, {18, 53}, {17, 44}, {19, 37}, {14, 34}, {9, 33}});
    front(v, fox, 3.f);
    v.push_back(G::X(lens(23, 24.5, 3.8, 1.3, -22) + "M30 7 31.5 16.5 34 16Z"));
    v.push_back(G::XS("M7.5 32.3C11 32.5 15 33.2 18 34.6M30 29C33 34 33 38 29 42", 1.5));
    v.push_back(G::XS("M39 97C36 86 40 74 50 70C56 68 60 71 61.5 77M30 64C31.5 75 31.5 87 29.5 97", 1.7));
    c.add("race-kitsune-tails", "Девятихвостая лиса", v);
  }
  // Маска кицунэ: высокие уши, раскосые прорези глаз с острыми хвостами, знаки пламени на лбу и щеках.
  {
    const string face = symSpline({{50, 24}, {42, 24}, {33, 18}, {24, 3, 0}, {24, 27}, {19, 45, 0.6}, {24, 60}, {34, 76}, {44, 89}, {50, 96, 0}});
    c.add("race-kitsune-mask", "Маска кицунэ",
          {G::F(face), G::XS(sym("M40.5 25 28.5 10.5 28 30"), 1.6), G::X(sym(tongue(ax({45, 51.5}, {25, 42}, -0.08, -0.12), 3.8, 0.7))),
           G::X(sym(tongue(ax({43, 41}, {26, 35.5}, -0.1, 0.05), 2.4))), G::X(wisp({50, 37}, {50, 21}, 3.4, 0.2, -0.2)),
           G::X(sym(tongue(ax({31, 61}, {44, 70}, 0.08), 2.4) + tongue(ax({33, 67}, {44, 76}, 0.08), 2.2))), G::X("M46 86.5H54L50 91Z"),
           G::XS(sym("M50 56V80"), 1.4)});
  }
  // Лисий огонь: жемчужина-хоју с заострённой макушкой и вихрем внутри в ореоле из завитых языков пламени.
  {
    std::vector<L> v;
    const string pearl = symSpline({{50, 27, 0}, {46, 35}, {38, 44}, {31, 55}, {30, 66}, {35, 77}, {42, 82.5}, {50, 84}});
    string halo = xf(pearl, scaleAt(1.2f, 50, 64)), veins;
    const std::pair<Pt, Pt> fl[] = {{{34, 74}, {6, 60}}, {{31, 58}, {4, 37}}, {{34, 44}, {12, 17}}, {{41, 34}, {28, 6}}};
    for (auto [b, t] : fl) {
      const Ax a = ax(b, t, -0.16, 0.2);
      halo += sym(tongue(a, 15, 1.1));
      veins += sym(axLine(a, 0.4, 0.85, 8));
    }
    halo += tongue(ax({50, 30}, {50, 1}), 12, 1.1);
    v.push_back(G::F(halo));
    v.push_back(G::XS(veins, 1.5));
    front(v, pearl, 3.f);
    v.push_back(G::XS("M50 77C40 77 36 67 41 61C45 56 53 57 54 63C55 67 50 69 47 66", 2.2));
    v.push_back(G::XS("M50 36C53 42 57 46 61 51", 1.5));
    c.add("race-kitsune-foxfire", "Лисий огонь", moved(v, Affine::translate(0, 5)));
  }
  // Врата-тории: верхняя балка с поднятыми концами, табличка, сквозная поперечина, наклонные столбы на подставках.
  c.add("race-kitsune-torii", "Врата-тории",
        {G::F("M1 4C18 13 82 13 99 4L97 13C80 21 20 21 3 13Z" + rrect(11, 21, 78, 5.5, 0.8) + rrect(5, 37, 90, 6.5, 1.2) + sym("M26 26H35L33.5 89H23Z") +
              sym(rrect(19.5, 88, 18, 8, 1.5)) + rrect(41, 25, 18, 14, 1.2)),
         G::XS("M4 12.5C21 20 79 20 96 12.5", 1.5), G::XS(rrect(44, 28, 12, 8, 0.6), 1.4), G::XS(sym("M19.5 91H37.5"), 1.4)});
}

// ---------------------------------------------------------------- кобольды
void kobolds(Catalog& c) {
  c.group("Кобольды", EmblemSection::Races);
  // Кобольд-рудокоп: идёт вправо, кирка на плече, на древке — фонарь; рогатая драконья голова, длинный хвост.
  {
    std::vector<L> v;
    // Задние части: хвост и дальняя нога.
    const string tail = tongue(ax({50, 60}, {4, 94}, 0.14, -0.12), 11);
    const string legB = band(ax({50, 63}, {45, 78}), [](double u) { return 11 - 3.5 * u; }) + band(ax({45, 77}, {37, 87}), [](double u) { return 7 - 2 * u; }) +
                        tongue(ax({37, 87}, {47, 96}, 0.1), 5.5);
    v.push_back(G::F(tail + legB));
    v.push_back(G::XS(axAcross(ax({50, 60}, {4, 94}, 0.14, -0.12), 0.35, 6, 6) + axAcross(ax({50, 60}, {4, 94}, 0.14, -0.12), 0.55, 5, 5), 1.4));
    // Тело, голова с рогами, ближняя нога.
    const string head = spline({{58, 24}, {66, 17}, {76, 18}, {86, 23.5}, {92, 28, 0}, {87, 31.5}, {78, 32.5}, {70, 35}, {63, 35}, {57, 31}});
    const string horns = tongue(ax({65, 19}, {41, 8}, 0.08, 0.02), 6.5) + tongue(ax({61, 23}, {43, 21}, 0.1), 5);
    const string torso = spline({{56, 30}, {64, 34}, {68, 44}, {67, 56}, {62, 64}, {54, 67}, {47, 62}, {46, 50}, {50, 38}});
    const string legF = band(ax({58, 63}, {66, 76}), [](double u) { return 12 - 4 * u; }) + band(ax({66, 75}, {60, 87}), [](double u) { return 7.5 - 2 * u; }) +
                        tongue(ax({60, 87}, {74, 96}, -0.05), 5.5);
    front(v, head + horns + torso + legF, 2.4f);
    v.push_back(G::X(lens(73, 23.5, 3.2, 1, 12)));
    v.push_back(G::XS("M76 30.5C80 30 85 29.5 89 29M63 31C61 40 60 50 61 58M64 75 68 77M61 87 64 89", 1.4));
    // Рука к древку.
    front(v, band(ax({62, 42}, {67, 56}), [](double u) { return 8 - 2 * u; }) + band(ax({66, 55}, {74, 54}), [](double u) { return 6 - 1.5 * u; }), 2.f);
    // Кирка: древко с плеча назад-вверх, изогнутое двуострое железо; фонарь на древке.
    const string haft = band(ax({78, 59}, {21, 21}), [](double u) { return 4.2 - 0.6 * u; });
    const string iron = band(ax({12, 41}, {36, 6}, -0.14, -0.14), [](double u) { return 8 * std::pow(std::sin(kPi * u), 0.6) + 0.3; });
    front(v, haft + iron + lens(22, 22, 5.5, 3.2, 34), 2.2f);
    const string lantern = "M37 38H45L44 41H38Z" + rrect(37.5, 40.5, 7, 10, 1.5) + "M38 50.5H44L45.5 53H36.5Z";
    v.push_back(G::S("M41 33V38", 1.6));
    front(v, lantern, 2.f);
    v.push_back(G::X(rrect(39.2, 42.5, 3.6, 6.2, 1)));
    v.push_back(G::F(wisp({41, 47.5}, {41, 43}, 1.2, 0, 0)));
    c.add("race-kobolds-miner", "Кобольд-рудокоп", v);
  }
  // Свеча на шлеме: голова кобольда в профиль в каске с полями, на макушке — оплывшая свеча с языком пламени;
  // из-под полей горит узкий глаз.
  {
    std::vector<L> v;
    const string head = spline({{21, 53}, {40, 47}, {62, 49}, {78, 53}, {90, 57}, {96, 62, 0}, {94, 67}, {84, 70}, {74, 73.5}, {64, 77}, {57, 84},
                                {56, 97, 0}, {35, 97, 0}, {34, 85}, {28, 72}, {20, 61}});
    const string horns = tongue(ax({27, 55}, {3, 59}, -0.12, 0.04), 8) + tongue(ax({28, 63}, {6, 77}, -0.1, 0.04), 6.5);
    v.push_back(G::F(horns));
    front(v, head + tongue(ax({84, 69.5}, {83.5, 74.5}), 2.6, 0.8) + tongue(ax({76, 72}, {75.5, 76}), 2.2, 0.8), 2.4f);
    v.push_back(G::X(tongue(ax({61, 57}, {76, 54.5}, 0.06), 3.6, 0.6) + lens(90.5, 60, 1.8, 0.8, 18)));
    v.push_back(G::XS("M95 65.5C87 66.5 80 67.5 73 69.5M60 61C66 63.5 72 64.5 79 64M45 77C47 84 48 90 47 97", 1.4));
    const string dome = spline({{20, 49, 0}, {22, 39}, {31, 30.5}, {45, 27}, {59, 29.5}, {67, 37}, {70, 47, 0}});
    const string brim = band(ax({8, 52}, {86, 45}, -0.03, -0.03), [](double u) { return 2.4 + 3.6 * std::pow(std::sin(kPi * u), 0.5); });
    const string candle = rrect(40.5, 14, 9, 15, 1.2) + tongue(ax({42, 16}, {41.5, 24}), 2.4) + tongue(ax({48.5, 15}, {49, 21}), 2.2);
    front(v, dome + brim + candle, 2.6f);
    v.push_back(G::XS("M21.5 43.5C35 40 55 40 69.5 42.5", 1.5));
    v.push_back(G::X(lens(30, 45, 1.3, 1) + lens(45, 42.4, 1.3, 1) + lens(60, 43.2, 1.3, 1)));
    v.push_back(G::XS("M40.5 17.5H49.5", 1.3));
    front(v, wisp({45, 12}, {45.5, 2}, 3.2, 0.3, -0.25), 1.8f);
    v.push_back(G::X(wisp({45, 12.5}, {45.2, 7.5}, 1.1, 0.2, -0.2)));
    c.add("race-kobolds-candle", "Свеча на шлеме", v);
  }
  // Капкан: раскрытые челюсти с зубьями навстречу друг другу, пружины по сторонам, цепь.
  {
    const double cx = 50, cy = 38, rx = 31, ry = 24, w = 6.5;
    auto jaw = [&](double a0, double a1) {
      std::vector<Pt> in, ou;
      for (int i = 0; i <= 24; i++) {
        const double a = (a0 + (a1 - a0) * i / 24) * kPi / 180;
        ou.push_back({float(cx + (rx + w / 2) * std::cos(a)), float(cy + (ry + w / 2) * std::sin(a))});
        in.push_back({float(cx + (rx - w / 2) * std::cos(a)), float(cy + (ry - w / 2) * std::sin(a))});
      }
      string s = "M" + ptS(ou[0]);
      for (size_t i = 1; i < ou.size(); i++) s += "L" + ptS(ou[i]);
      for (size_t i = in.size(); i-- > 0;) s += "L" + ptS(in[i]);
      return s + "Z";
    };
    string teeth;
    for (int k = 0; k < 13; k++) {
      const double x = cx + (k - 6) * 4.4, up = k % 2 ? 1 : -1;   // чётные — сверху вниз, нечётные — снизу вверх
      const double e = (x - cx) / (rx - w / 2), y = cy + up * (ry - w / 2) * std::sqrt(std::max(0.0, 1 - e * e)) - up * 1;
      teeth += "M" + n(x - 3) + " " + n(y) + "L" + n(x) + " " + n(cy - up * 2.5) + "L" + n(x + 3) + " " + n(y) + "Z";
    }
    c.add("race-kobolds-trap", "Капкан",
          {G::F(jaw(180, 360) + jaw(0, 180) + teeth + sym(rrect(15, 33, 8, 10, 2))), G::S(sym("M16 34 3.5 38 16 42"), 2.6), G::F(sym(rrect(9, 33.5, 3.4, 9, 1))),
           G::XS(sym(seg({19, 33.5}, {19, 42.5})), 1.4),
           G::S(lens(50, 72, 5, 2.6, 90), 2.2), G::F(rrect(48.9, 77, 2.2, 10, 1.1)), G::S(lens(50, 91.5, 4.5, 2.4, 90), 2.2)});
  }
  // Яйцо дракона на груде монет: заострённая чешуя рядами, трещина у макушки.
  {
    std::vector<L> v;
    const string hoard = "M3 97C6 88 14 82 24 80C32 76 42 77 50 79C58 77 68 76 76 80C86 82 94 88 97 97Z";
    v.push_back(G::F(hoard));
    string coins;
    const Pt cs[] = {{13, 91}, {27, 86}, {42, 91}, {58, 91}, {73, 86}, {87, 91}, {20, 96.5}, {50, 97}, {80, 96.5}};
    for (Pt p : cs) coins += ell(p.x, p.y, 6, 2.4);
    v.push_back(G::XS(coins, 1.4));
    const string egg = symSpline({{50, 3}, {39, 9}, {30, 26}, {27, 46}, {31, 64}, {40, 75}, {50, 78}});
    front(v, egg, 2.6f);
    string scales;
    for (int r = 0; r < 8; r++) {
      const double y = 16 + 7.4 * r;
      for (int k = -4; k <= 4; k++) {
        const double x = 50 + k * 7.6 + (r % 2 ? 3.8 : 0);
        const double ex = (x - 50) / 21.0, ey = (y + 3 - 41) / 35.0;
        if (ex * ex + ey * ey > 0.78) continue;
        scales += "M" + n(x - 3.8) + " " + n(y) + "C" + n(x - 3.2) + " " + n(y + 4) + " " + n(x - 1) + " " + n(y + 6) + " " + n(x) + " " + n(y + 7.5) +
                  "C" + n(x + 1) + " " + n(y + 6) + " " + n(x + 3.2) + " " + n(y + 4) + " " + n(x + 3.8) + " " + n(y);
      }
    }
    v.push_back(G::XS(scales, 1.3));
    v.push_back(G::XS("M40 9.5 44 17 40.5 22 46 27 43 33", 1.8));
    v.push_back(G::XS(xf(egg, scaleAt(0.9f, 50, 42)), 1.4));
    c.add("race-kobolds-egg", "Яйцо дракона", v);
  }
}

// ---------------------------------------------------------------- кристаллиты
void crystallites(Catalog& c) {
  c.group("Кристаллиты", EmblemSection::Races);
  // Кристальный воин: гранёный шлем с венцом из трёх кристаллов, наплечья из друз, гранёная кираса с ядром-самоцветом.
  {
    std::vector<L> v;
    string facets;
    const string crown = crystal({44, 16}, {37, 2}, 6.5, &facets, 0.7) + crystal({56, 16}, {63, 2}, 6.5, &facets, 0.7);
    const string top = crystal({50, 14}, {50, 1}, 8, &facets, 0.66);
    string sh, shF;
    sh += crystal({31, 47}, {4, 22}, 12, &shF, 0.74) + crystal({30, 54}, {3, 49}, 8.5, &shF, 0.7) + crystal({37, 43}, {24, 15}, 8, &shF, 0.72);
    v.push_back(G::F(sym(sh) + crown));
    v.push_back(G::XS(sym(shF), 1.4));
    front(v, top, 2.f);
    const string helm = poly({50, 11, 61, 18, 61, 33, 50, 41, 39, 33, 39, 18});
    const string torso = poly({50, 42, 35, 44, 25, 53, 31, 69, 41, 86, 50, 97, 59, 86, 69, 69, 75, 53, 65, 44});
    front(v, helm + torso, 2.4f);
    v.push_back(G::XS(facets, 1.4));
    v.push_back(G::X("M41.5 23H58.5V26.5H52V35H48V26.5H41.5Z"));
    v.push_back(G::XS("M39 18 50 23 61 18M50 11V23", 1.4));
    v.push_back(G::XS(sym("M35 44 42 57 31 69M25 53 42 57M42 57 41 86M50 70 42 57"), 1.4));
    v.push_back(G::X(poly({50, 47, 58.5, 58, 50, 69, 41.5, 58})));
    v.push_back(G::F(poly({50, 51.5, 55, 58, 50, 64.5, 45, 58})));
    c.add("race-crystallites-warrior", "Кристальный воин", v);
  }
  // Друза: веер шестигранных кристаллов разной высоты из каменного основания.
  {
    std::vector<L> v;
    struct Cr {
      Pt b, t;
      double w;
    };
    const Cr cs[] = {{{28, 90}, {7, 47}, 11},  {{72, 90}, {93, 47}, 11},  {{38, 88}, {21, 15}, 13}, {{62, 88}, {79, 15}, 13},
                     {{50, 90}, {50, 2}, 17},  {{41, 95}, {31, 70}, 7.5}, {{59, 95}, {69, 70}, 7.5}};
    for (const Cr& k : cs) {
      string f;
      front(v, crystal(k.b, k.t, k.w, &f, 0.8), 2.4f);
      v.push_back(G::XS(f, 1.4));
    }
    const string rock = "M5 97C7 90 15 86 25 87C33 83 43 84 50 86C57 84 67 83 75 87C85 86 93 90 95 97Z";
    front(v, rock, 2.4f);
    v.push_back(G::XS("M22 97 26 92 24 89M61 97 59 92 63 88M80 97 82 93", 1.4));
    c.add("race-crystallites-cluster", "Друза", v);
  }
  // Самоцвет-сердце: сердце огранкой «бриллиант» — площадка и грани, искры вокруг.
  {
    const string outer = sym("M50 29 43 18 31 13 18 17 9 30 11 46 22 61 50 93 50 29Z");
    const string table = sym("M50 39 44 31 35 28 27 32 25 42 32 53 50 73");
    c.add("race-crystallites-heart", "Самоцвет-сердце",
          {G::F(outer + star(86, 13, 11, 1.8, 4, -90) + star(14, 84, 8, 1.5, 4, -90) + star(87, 74, 6, 1.2, 4, -90)), G::XS(table, 1.6),
           G::XS(sym("M43 18 44 31M31 13 35 28M18 17 27 32M9 30 25 42M11 46 32 53M22 61 50 73M50 29V39"), 1.4)});
  }
  // Призма: узкий луч входит слева и расходится веером из пяти лучей.
  {
    string rays;
    for (int k = 0; k < 5; k++) {
      const Pt a = lerp({63, 37}, {71, 52}, k / 4.0), b = lerp({63, 37}, {71, 52}, (k + 0.75) / 4.0);
      const double y0 = 17 + 15.5 * k;
      rays += "M" + ptS(a) + "L97 " + n(y0) + "L97 " + n(y0 + 9.5) + "L" + ptS(b) + "Z";
    }
    c.add("race-crystallites-prism", "Призма",
          {G::F(rays + "M3 62.5 3 58 31 49.5 32 53Z"), G::XS("M50 10 83 72H17Z", 3), G::F("M50 10 83 72H17Z"), G::XS("M50 25.5 70 63.5H30Z", 1.6),
           G::XS("M50 10V25.5M83 72 70 63.5M17 72 30 63.5", 1.4)});
  }
}

// ---------------------------------------------------------------- кровавые эльфы
void bloodElves(Catalog& c) {
  c.group("Кровавые Эльфы", EmblemSection::Races);
  // Феникс в пламени: распростёртые крылья из перьев-языков, голова в профиль с огненным хохлом, хвост-пламя.
  {
    std::vector<L> v;
    const Pt tips[] = {{4, 64}, {2, 46}, {6, 28}, {15, 13}, {28, 3}};
    for (int k = 0; k < 5; k++) {
      const Ax a = ax({float(45 - k * 0.6), float(50 - k * 2)}, tips[k], 0.1, -0.12);
      front(v, sym(tongue(a, 13 - k * 0.5, 0.8)), 2.2f);
      v.push_back(G::XS(sym(axLine(a, 0.32, 0.78, 10)), 1.3));
    }
    const string covert = sym("M48 40C40 34 31 33 22 35C26 43 32 50 40 55C44 57 46 58 48 58Z");
    front(v, covert, 2.2f);
    v.push_back(G::XS(sym("M44 44C38 41 32 40 27 40M44 50C39 48 35 46 31 44"), 1.4));
    const Ax t0 = ax({50, 66}, {50, 98}, 0.08, -0.08), t1 = ax({48, 66}, {33, 96}, -0.1, 0.12), t2 = ax({52, 66}, {67, 96}, 0.1, -0.12);
    front(v, tongue(t0, 12) + tongue(t1, 10) + tongue(t2, 10), 2.2f);
    v.push_back(G::XS(axLine(t0, 0.25, 0.75, 8) + axLine(t1, 0.25, 0.7, 8) + axLine(t2, 0.25, 0.7, 8), 1.3));
    const string body = symSpline({{50, 36}, {45, 40}, {42, 51}, {44, 62}, {50, 71}});
    const string neck = band(ax({50, 44}, {41, 20}, 0.12, 0.05), [](double u) { return 11 - 3 * u; });
    const string head = spline({{27, 22, 0}, {33, 16}, {40, 12.5}, {46, 14}, {47.5, 20}, {44, 24.5}, {38, 24}, {33, 24.5}, {30, 26, 0}, {31, 23}});
    const string crest = tongue(ax({45, 15}, {60, 2}, -0.08, 0.1), 5.5) + tongue(ax({46, 17}, {66, 9}, -0.06, 0.1), 5) +
                         tongue(ax({46, 20}, {65, 18}, -0.05, 0.1), 4);
    front(v, body + neck + head + crest, 2.4f);
    v.push_back(G::X(lens(39.5, 17.5, 2.4, 1, -12)));
    v.push_back(G::XS("M46 47 50 51 54 47M45 53 50 57.5 55 53M46 59 50 63 54 59", 1.4));
    c.add("race-blood-elves-phoenix", "Феникс в пламени", v);
  }
  // Кровь в кристалле: вытянутый гранёный кристалл с каплей крови внутри, два осколка по сторонам.
  {
    const string gem = poly({50, 2, 68, 24, 64, 77, 50, 98, 36, 77, 32, 24});
    const string shards = sym(poly({16, 27, 22.5, 47, 16, 71, 9.5, 47}));
    c.add("race-blood-elves-drop", "Кровь в кристалле",
          {G::F(gem + shards), G::XS(sym("M32 24H43L50 2M43 24 36 77 50 86") + "M50 86V98", 1.5), G::XS(sym("M9.5 47H16V71"), 1.3),
           G::X("M50 29C58 40 64 49 64 59A14 14 0 0 1 36 59C36 49 42 40 50 29Z"),
           G::F("M50 35.5C55.5 44 60 51 60 58.5A10 10 0 0 1 40 58.5C40 51 44.5 44 50 35.5Z"), G::X(lens(45, 57, 3.6, 1.2, 75))});
  }
  // Солнечный клинок: эльфийский меч с крылатой гардой перед солнцем с прямыми и пламенными лучами.
  {
    string rays;
    for (int i = 0; i < 16; i++) {
      const double a = -90 + 22.5 * i;
      if (i % 2 == 0) rays += "M" + pt(50, 45, 19, a - 7.5) + "L" + pt(50, 45, 43, a) + "L" + pt(50, 45, 19, a + 7.5) + "Z";
      else rays += tongue(ax(polar(50, 45, 18, a), polar(50, 45, 37, a), 0.14, -0.14), 9);
    }
    const string blade = "M50 2C54.5 9 57.5 17 57.5 26C57.5 38 56 50 55.5 60H44.5C44 50 42.5 38 42.5 26C42.5 17 45.5 9 50 2Z";
    const Ax wing = ax({50, 64}, {13, 44}, -0.16, -0.1);
    const string wings = sym(band(wing, [](double u) { return 8.5 * std::pow(1 - u, 0.75) + 0.3; }));
    const string sword = blade + wings + rrect(46.5, 64, 7, 21, 2) + "M50 83 56 90 50 97.5 44 90Z";
    c.add("race-blood-elves-blade", "Солнечный клинок",
          {G::F(rays + circ(50, 45, 20)), G::XS(circ(50, 45, 15), 1.6), G::XS(sword, 4.5), G::F(sword), G::XS("M50 10V56", 1.6),
           G::XS(sym(axAcross(wing, 0.42, 1, 6) + axAcross(wing, 0.6, 1, 5) + axAcross(wing, 0.76, 1, 4)), 1.4),
           G::XS("M46.5 70H53.5M46.5 75H53.5M46.5 80H53.5", 1.4), G::X("M50 86.5 53 90 50 94 47 90Z")});
  }
  // Эльфийский глаз: миндалевидный глаз с каллиграфическими веками, солнечная радужка со зрачком-щелью, венец
  // языков пламени и кровавая слеза.
  {
    const string upper = band(ax({4, 55}, {96, 49}, -0.32, -0.3), [](double u) { return 9.5 * std::pow(std::sin(kPi * u), 0.7) + 0.3; });
    const string lower = band(ax({11, 51}, {89, 51}, 0.26, 0.26), [](double u) { return 6.5 * std::pow(std::sin(kPi * u), 0.7) + 0.3; });
    string burst;
    for (int k = 0; k < 16; k++) burst += seg(polar(50, 50, 6.5, k * 22.5), polar(50, 50, 11, k * 22.5));
    const string brow = tongue(ax({50, 28}, {50, 3}), 9) + sym(tongue(ax({43, 29}, {32, 7}, 0.06, -0.1), 7.5) + tongue(ax({36, 32}, {18, 17}, 0.06, -0.1), 6));
    c.add("race-blood-elves-eye", "Эльфийский глаз",
          {G::F(brow + upper + lower + "M50 72C55 79 57 83 57 87A7 7 0 0 1 43 87C43 83 45 79 50 72Z"), G::F(circ(50, 50, 13.5)),
           G::XS(burst, 1.4), G::X("M50 41C52.5 45 52.5 55 50 59C47.5 55 47.5 45 50 41Z")});
  }
}

// ---------------------------------------------------------------- крысолаки
void ratkin(Catalog& c) {
  c.group("Крысолаки", EmblemSection::Races);
  // Голова крысы: в профиль влево, «вырванная» по шее; длинная морда с вибриссами, резцы, круглое ухо, узкий глаз.
  {
    std::vector<L> v;
    const string head = spline({{9, 55, 0}, {14, 50}, {24, 44}, {35, 38}, {45, 34}, {50, 32, 0}, {47, 23}, {51, 13}, {60, 8}, {70, 11}, {74, 20},
                                {71, 30}, {66, 35, 0}, {74, 42}, {81, 52}, {86, 64}, {89, 78}, {92, 92, 0}, {84, 86, 0}, {78, 97, 0}, {70, 87, 0},
                                {62, 96, 0}, {55, 85, 0}, {48, 92, 0}, {47, 80}, {42, 71}, {33, 66}, {23, 63}, {15, 60}});
    const string teeth = poly({12.5, 59.5, 17, 60.5, 16.5, 66, 13, 65.5});
    front(v, head + teeth, 2.f);
    v.push_back(G::XS("M14.8 60.5 14.6 65.5", 1.2));
    v.push_back(G::XS("M52 30C51 22 55 15 62 13C67 12 70 15 70.5 20", 1.6));
    v.push_back(G::X(lens(33.5, 44, 2.8, 1.3, -18) + lens(10.5, 53.5, 1.6, 0.8, -30)));
    v.push_back(G::XS("M17 58C25 60 33 62 40 66M58 44C64 50 68 58 70 68", 1.4));
    v.push_back(G::F(tongue(ax({17, 53}, {3, 46}, 0.1), 1.6, 1) + tongue(ax({17, 55}, {2, 56}, 0.05), 1.6, 1) + tongue(ax({18, 57}, {4, 66}, -0.08), 1.5, 1)));
    c.add("race-ratkin-head", "Голова крысы", moved(v, Affine::translate(1, -2)));
  }
  // Крысиный король: три крысы бегут по кругу, хвосты сплетены узлом в середине.
  {
    std::vector<L> v;
    const string rat = spline({{-31, -28.5, 0}, {-24, -31.5}, {-17, -35}, {-14.5, -40}, {-10.5, -43.5}, {-6, -42}, {-5.5, -38.5}, {3, -40.5},
                               {11, -38.5}, {16.5, -33}, {17, -28}, {12, -24.5}, {2, -23}, {-9, -24}, {-17, -25.5}, {-24, -27}});
    const string legs = tongue(ax({-11, -25}, {-18, -18.5}, 0.12), 4) + tongue(ax({9, -26}, {17, -18}, -0.12), 4.8);
    const string tail = "M16 -28C22 -18 12 -6 0 2C-6 6 -10 10 -12 16";
    string rats, tails, cuts;
    for (int k = 0; k < 3; k++) {
      const Affine m = Affine::translate(50, 50) * Affine::rotate(float(k * 2 * kPi / 3 + 0.3)) * Affine::scale(1.07f);
      rats += xf(rat + legs, m);
      tails += xf(tail, m);
      cuts += xf(lens(-19, -31, 2, 0.8, -15) + lens(-9.5, -39.5, 1.8, 0.9, 60), m);
    }
    // Хвосты: каждый следующий поверх предыдущего с зазором — переплетение.
    for (int k = 0; k < 3; k++) {
      const Affine m = Affine::translate(50, 50) * Affine::rotate(float(k * 2 * kPi / 3 + 0.3)) * Affine::scale(1.07f);
      v.push_back(G::XS(xf(tail, m), 6));
      v.push_back(G::F(xf(band(ax({16, -28}, {0, 2}, -0.35, -0.2), [](double u) { return 4 - 1.2 * u; }) +
                              band(ax({0, 2}, {-12, 16}, -0.05), [](double u) { return 2.8 - 2 * u; }),
                          m)));
    }
    front(v, rats, 2.4f);
    v.push_back(G::X(cuts));
    c.add("race-ratkin-king", "Крысиный король", moved(v, Affine::translate(1, 4)));
  }
  // Чумной колокол: треснувший колокол со следами когтей на виселице с подкосами.
  {
    const string bell = symSpline({{50, 22}, {42, 23}, {36, 29}, {34, 42}, {32, 56}, {28, 68}, {19, 77, 0}, {22, 82, 0}, {50, 82}});
    c.add("race-ratkin-bell", "Чумной колокол",
          {G::F(rrect(4, 6, 92, 7, 1.5) + rrect(10, 12, 7, 82, 1.5) + rrect(83, 12, 7, 82, 1.5) + rrect(5, 91, 17, 6, 1.5) + rrect(78, 91, 17, 6, 1.5) +
                sym("M17 13H29L17 25Z")),
           G::XS("M4 9.5H96", 1.3), G::XS(sym("M13.5 16V88"), 1.3), G::S("M50 13V22", 4), G::F(rrect(44.5, 18, 11, 6, 2)),
           G::F(tongue(ax({50, 82}, {50, 93}), 6, 0.6)), G::XS(bell, 2.6), G::F(bell), G::XS("M35 35C44 33 56 33 65 35M25 72C40 69 60 69 75 72", 1.8),
           G::XS("M62 78 58 70 62.5 64 57.5 56", 1.6),
           G::X(tongue(ax({38, 41}, {44, 64}), 3.2) + tongue(ax({45, 39.5}, {50, 63}), 3.2) + tongue(ax({52, 40}, {56, 61}), 3.2))});
  }
  // Ядовитый коготь: жилистая крысиная лапа тянется вниз из меха, длинные крючковатые когти, с них капает яд.
  {
    const Pt ks[] = {{40, 37}, {46, 41}, {54, 41}, {60, 37}};
    const Pt js[] = {{29, 50}, {41, 58}, {59, 58}, {71, 50}};
    const Pt ts[] = {{20, 63}, {37, 73}, {63, 73}, {80, 63}};
    string fingers, claws, joints;
    for (int i = 0; i < 4; i++) {
      fingers += band(ax(ks[i], js[i]), [](double u) { return 8 - 1.2 * u; }) + band(ax(js[i], ts[i]), [](double u) { return 6.8 - 1.4 * u; });
      const double s = ts[i].x < 50 ? 1 : -1, dx = ts[i].x - js[i].x, dy = ts[i].y - js[i].y, len = std::hypot(dx, dy);
      const Pt tip{float(ts[i].x + dx / len * 13 + s * 7), float(ts[i].y + dy / len * 13)};
      claws += tongue(ax(ts[i], tip, s * -0.22), 5.6, 0.8);
      joints += across(js[i], js[i], ts[i], ts[i], 0, 4, 4) + across(ks[i], ks[i], js[i], js[i], 0.5, 1.8, 1.8);
    }
    const string arm = "M41 1 45.5 4.5 50 1 54.5 4.5 59 1 57 24H43Z" + symSpline({{50, 22}, {43, 23.5}, {39, 30}, {38, 37}, {42, 42}, {50, 43}});
    auto drop = [](double x, double y, double s) {
      return xf("M0 -12C2.5 -5 5 -1.5 5 2.5A5 5 0 0 1 -5 2.5C-5 -1.5 -2.5 -5 0 -12Z", place(float(x), float(y), 0, float(s)));
    };
    c.add("race-ratkin-claw", "Ядовитый коготь",
          {G::F(arm + fingers + claws), G::XS(joints, 1.4), G::XS("M46 9 47.5 24M54 9 52.5 24", 1.4),
           G::F(drop(31, 88, 1) + drop(50, 93, 0.85) + drop(69, 88, 1))});
  }
}

// ---------------------------------------------------------------- ктониды
void chthonids(Catalog& c) {
  c.group("Ктониды", EmblemSection::Races);
  // Щупальца из бездны: три щупальца с рядами присосок поднимаются из провала, кончики завиты.
  {
    std::vector<L> v;
    v.push_back(G::F(ell(50, 84, 46, 12)));
    v.push_back(G::X(ell(50, 83, 38.5, 7.5)));
    struct Tn {
      std::vector<Pt> p;
      double w, side;
    };
    const Tn ts[] = {{{{50, 88}, {45, 70}, {55, 52}, {54, 34}, {45, 20}, {35, 17}, {30, 23}, {34, 28.5}, {39, 26}}, 14, 1},
                     {{{27, 88}, {24, 74}, {14, 64}, {9, 50}, {12, 38}, {20, 35}, {23, 40}, {20, 43}}, 10.5, -1},
                     {{{73, 88}, {76, 74}, {86, 64}, {91, 50}, {88, 38}, {80, 35}, {77, 40}, {80, 43}}, 10.5, 1}};
    for (const Tn& t : ts) {
      const double w0 = t.w;
      const Tube tb = tube(t.p, [w0](double u) { return w0 * std::pow(1 - u, 0.9) + 0.5; }, 3);
      string suckers;
      for (const Tube::S& s : tb.at)
        if (s.u > 0.14 && s.u < 0.7)
          suckers += lens(s.x + s.nx * s.w * 0.22 * t.side, s.y + s.ny * s.w * 0.22 * t.side, std::max(1.2, s.w * 0.16), std::max(0.9, s.w * 0.11),
                          std::atan2(s.ny, s.nx) * 180 / kPi + 90);
      front(v, tb.fill, 2.6f);
      v.push_back(G::X(suckers));
    }
    const string lip = "M4 84C4 92 26 97 50 97C74 97 96 92 96 84C96 89 74 92 50 92C26 92 4 89 4 84Z";
    front(v, lip, 2.4f);
    c.add("race-chthonids-tentacles", "Щупальца из бездны", moved(v, Affine::translate(0, -3)));
  }
  // Многоглазое существо: в середине великое око, вокруг — венец стебельков с глазами, внизу свисают щупальца.
  {
    std::vector<L> v;
    string stalks, eyes, pupils;
    const double ang[] = {-160, -128, -104, -76, -52, -20};
    for (double a : ang) {
      const Pt b = polar(50, 50, 18, a), t = polar(50, 47, 41, a);
      const Ax s = ax(b, t, a < -90 ? 0.12 : -0.12);
      stalks += band(s, [](double u) { return 5.5 - 3 * u; });
      const double deg = std::atan2(t.y - b.y, t.x - b.x) * 180 / kPi;
      eyes += lens(t.x, t.y, 5.5, 3.6, deg + 90);
      pupils += lens(t.x, t.y, 2.6, 0.8, deg);
    }
    string legs;
    const Pt lb[] = {{35, 66}, {44, 71}, {56, 71}, {65, 66}};
    const Pt lt[] = {{14, 93}, {35, 98}, {65, 98}, {86, 93}};
    for (int i = 0; i < 4; i++) legs += tongue(ax(lb[i], lt[i], i < 2 ? 0.18 : -0.18, i < 2 ? -0.16 : 0.16), 8.5);
    v.push_back(G::F(stalks + legs));
    front(v, eyes, 2.f);
    v.push_back(G::X(pupils));
    const string body = symSpline({{50, 24}, {38, 27}, {29, 36}, {26, 49}, {30, 62}, {39, 70}, {50, 73}});
    front(v, body, 2.6f);
    v.push_back(G::X("M30 48C37 37 63 37 70 48C63 59 37 59 30 48Z"));
    v.push_back(G::F(circ(50, 48, 8.5)));
    v.push_back(G::X("M50 40.5C52 44 52 52 50 55.5C48 52 48 44 50 40.5Z"));
    v.push_back(G::XS("M37 30C44 27 56 27 63 30M38 65C44 68 56 68 62 65", 1.4));
    c.add("race-chthonids-eyes", "Многоглазое существо", v);
  }
  // Разлом: две скалы расколоты зубчатой трещиной, из глубины смотрит око; обломки летят вверх.
  {
    const string left = "M3 96 5 76 11 59 19 45 29 33 38 24 46 19 43 28 47 34 42 41C38 46 34 50 31 54C35 58 39 62 43 67L45 75 41 83 46 90 44 96Z";
    const string right = xf(left, Affine{-1, 0, 0, 1, 100, 0});
    c.add("race-chthonids-rift", "Разлом",
          {G::F(left + right + poly({38, 9, 44, 6, 44, 13}) + poly({57, 3, 63, 7, 58, 12}) + poly({27, 16, 31, 13, 32, 19}) + poly({71, 14, 76, 17, 72, 21})),
           G::F("M50 42C56 47 59 51 59 55C59 59 56 63 50 68C44 63 41 59 41 55C41 51 44 47 50 42Z"), G::X("M50 46.5C51.6 50 51.6 60 50 63.5C48.4 60 48.4 50 50 46.5Z"),
           G::XS(sym("M7 82C15 79 25 79 35 82M10 67C17 64 24 64 31 66M17 52C22 49 27 49 32 50M25 39C29 37 33 37 37 38"), 1.5)});
  }
  // Хтонический идол: спрутоголовое божество со сложенными перепончатыми крыльями на ступенчатом постаменте.
  {
    std::vector<L> v;
    const string wings = sym("M31 47C25 36 16 25 5 17C8 27 9 36 5 45C11 44 15 47 17 53C21 51 26 52 30 57Z");
    v.push_back(G::F(wings));
    v.push_back(G::XS(sym("M29 46C24 40 18 33 9 24M27 52C22 50 16 47 10 44"), 1.4));
    const string body = symSpline({{50, 44}, {36, 46}, {25, 54}, {21, 70}, {23, 82}, {50, 82}});
    front(v, body, 2.6f);
    v.push_back(G::XS(sym("M27 56C29 66 34 72 42 76M23 74C30 77 38 79 46 79"), 1.5));
    const string head = symSpline({{50, 4}, {38, 7}, {30, 16}, {28, 29}, {31, 39}, {38, 44}, {50, 45}});
    front(v, head, 2.4f);
    v.push_back(G::X(sym(tongue(ax({45.5, 33}, {32, 27}, 0.05), 3.6, 0.6))));
    v.push_back(G::XS(sym("M42 8C37 13 35 19 35 25") + "M50 5V25", 1.4));
    const double xs[] = {38, 62, 44, 56, 50};
    const Pt tipsB[] = {{30, 67}, {70, 67}, {39, 75}, {61, 75}, {50, 78}};
    for (int i = 0; i < 5; i++) {
      const double s = (tipsB[i].x - xs[i]) / 10;
      front(v, tongue(ax({float(xs[i]), 37}, tipsB[i], 0.16 * s + (i == 4 ? 0.08 : 0), -0.22 * s - (i == 4 ? 0.08 : 0)), 6.5, 0.85), 1.8f);
    }
    front(v, rrect(15, 81, 70, 8, 1.5) + rrect(8, 89, 84, 8, 1.5), 2.4f);
    v.push_back(G::XS("M17 85H83M10 93H90", 1.3));
    c.add("race-chthonids-idol", "Хтонический идол", v);
  }
}

// ---------------------------------------------------------------- лаваниды
void lavanids(Catalog& c) {
  c.group("Лаваниды", EmblemSection::Races);
  // Королева роя: венец, изогнутые усики, четыре крыла с жилками, суставчатые лапы, длинное кольчатое брюшко с жалом.
  {
    std::vector<L> v;
    const Ax wu = ax({44, 39}, {4, 19}, -0.07), wl = ax({44, 46}, {9, 61}, 0.07);
    v.push_back(G::F(sym(leaf(wu, 15))));
    v.push_back(G::XS(sym(axLine(wu, 0.12, 0.85) + axAcross(wu, 0.35, 0, 5.5) + axAcross(wu, 0.55, 0, 5) + axAcross(wu, 0.45, 5, 0)), 1.3));
    front(v, sym(leaf(wl, 11)), 2.f);
    v.push_back(G::XS(sym(axLine(wl, 0.12, 0.85) + axAcross(wl, 0.45, 0, 4)), 1.3));
    string legs;
    const std::pair<Pt, Pt> lg[] = {{{44, 44}, {34, 52}}, {{45, 48}, {37, 62}}, {{46, 52}, {41, 70}}};
    const Pt ft[] = {{30, 64}, {33, 76}, {40, 84}};
    for (int k = 0; k < 3; k++) legs += band(ax(lg[k].first, lg[k].second), [](double) { return 3.2; }) + tongue(ax(lg[k].second, ft[k], 0.1), 3.2);
    v.push_back(G::F(sym(legs)));
    v.push_back(G::F(sym(tongue(ax({46, 19}, {22, 6}, 0.12, -0.1), 3))));
    const string abdomen = symSpline({{50, 51}, {42, 54}, {37.5, 64}, {38.5, 77}, {44, 89}, {50, 98, 0}});
    const string thorax = ell(50, 43, 9, 9.5);
    const string head = symSpline({{50, 17}, {42, 18}, {37.5, 24}, {40.5, 31}, {50, 34.5}}) + sym("M42 19 39 7 46.5 15.5Z") + "M47.5 16.5 50 4 52.5 16.5Z";
    front(v, abdomen, 2.4f);
    v.push_back(G::XS("M39 64Q50 68.5 61 64M38.5 73Q50 77.5 61.5 73M41 82Q50 86 59 82", 1.8));
    front(v, thorax, 2.2f);
    v.push_back(G::XS("M44 38Q50 41 56 38", 1.4));
    front(v, head, 2.2f);
    v.push_back(G::X(sym(lens(42.5, 24, 4.6, 2, -70))));
    c.add("race-lavanids-queen", "Королева роя", v);
  }
  // Жук-воин: изогнутый рог, щит переднеспинки, надкрылья с бороздами, суставчатые лапы с острыми лапками.
  {
    std::vector<L> v;
    string legs;
    const Pt hip[] = {{33, 40}, {31, 58}, {34, 75}}, knee[] = {{18, 34}, {15, 58}, {19, 83}}, foot[] = {{11, 18}, {4, 70}, {14, 98}};
    for (int k = 0; k < 3; k++) legs += band(ax(hip[k], knee[k]), [](double u) { return 4.6 - u; }) + tongue(ax(knee[k], foot[k], k == 1 ? 0.1 : -0.1), 3.6, 0.7);
    v.push_back(G::F(sym(legs)));
    v.push_back(G::XS(sym(axAcross(ax(knee[0], foot[0]), 0.5, 2.5, 2.5) + axAcross(ax(knee[1], foot[1]), 0.5, 2.5, 2.5) + axAcross(ax(knee[2], foot[2]), 0.5, 2.5, 2.5)), 1.3));
    const string horn = tongue(ax({50, 28}, {50, 2}), 9, 1.2) + sym(tongue(ax({47.5, 14}, {41, 7}, 0.1), 3));
    const string headS = ell(50, 29, 8.5, 6.5);
    const string pron = symSpline({{50, 31}, {40, 32}, {30, 36}, {27, 44}, {33, 50}, {50, 51}});
    const string ely = symSpline({{50, 51.5}, {37, 52.5}, {28, 60}, {27, 77}, {35, 91}, {50, 97}});
    front(v, ely + headS + horn, 2.4f);
    front(v, pron, 2.2f);
    v.push_back(G::XS("M50 52V97", 2));
    v.push_back(G::XS(sym("M44 57C40 67 40 80 44 90M37 59C33 68 33 78 37 86"), 1.4));
    v.push_back(G::XS(sym("M33 39C38 37 44 36.5 50 36.5"), 1.3));
    v.push_back(G::X(sym(lens(44, 28, 2.4, 1.3, -30))));
    c.add("race-lavanids-beetle", "Жук-воин", v);
  }
  // Соты улья: семь шестиугольных ячеек — запечатанные, с яйцами и личинкой в середине.
  {
    const double R = 16.5, hx = R * std::sqrt(3.0) / 2;
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
    string fill, open, caps, eggs;
    for (const Cell& k : cells) {
      fill += hex(k.x, k.y, R - 1.2);
      if (k.kind == 0) caps += hex(k.x, k.y, R - 5.5) + hex(k.x, k.y, R - 9.5);
      else open += hex(k.x, k.y, R - 4.6);
      if (k.kind == 2) eggs += lens(k.x, k.y, 4.6, 1.9, 60);
    }
    c.add("race-lavanids-hive", "Соты улья",
          {G::F(fill), G::X(open), G::XS(caps, 1.4), G::F(eggs + taperArc(50, 50, 5, -60, 230, 4.6, 1.6)), G::F(lens(54.5, 46, 2.2, 1.6, -30))});
  }
  // Жвала: голова жука-оленя сверху — огромные изогнутые жвала-клещи с зубцами, глаза, коленчатые усики, переднеспинка.
  {
    const Pt m0{41, 60}, m1{13, 52}, m2{11, 16}, m3{45, 5};
    auto mw = [](double u) { return 13 * std::pow(1 - u, 0.8) + 0.4; };
    string teeth;
    for (auto [t, len] : {std::pair{0.4, 7.5}, std::pair{0.58, 5.0}, std::pair{0.72, 3.5}}) {
      const CurvePt q = bezier(m0, m1, m2, m3, t);
      const double h = mw(t) / 2 - 0.5, bx = q.x + q.nx * h, by = q.y + q.ny * h, tx = q.ny, ty = -q.nx;
      teeth += "M" + n(bx - tx * 2.6) + " " + n(by - ty * 2.6) + "L" + n(bx + q.nx * len + tx * 2) + " " + n(by + q.ny * len + ty * 2) + "L" +
               n(bx + tx * 2.6) + " " + n(by + ty * 2.6) + "Z";
    }
    const string mand = sym(taperW(m0, m1, m2, m3, mw) + teeth);
    const string head = symSpline({{50, 56}, {37, 56.5}, {28, 61}, {26, 70}, {31, 78}, {50, 80}});
    const string thorax = symSpline({{50, 81}, {33, 82}, {22, 86}, {20, 97, 0}, {50, 97}});
    c.add("race-lavanids-mandibles", "Жвала",
          {G::F(mand), G::XS(sym(axLine({m0, m1, m2, m3}, 0.12, 0.6)), 1.3), G::F(sym(band(ax({30, 64}, {18, 62}), [](double) { return 2.6; }) +
                                                                                     tongue(ax({18, 62}, {11, 51}, 0.1), 2.6))),
           G::XS(head + thorax, 2.6), G::F(thorax), G::F(head), G::XS("M36 85Q50 89 64 85", 1.4), G::X(sym(lens(31.5, 67, 3.4, 1.6, 70))),
           G::XS("M38 80.5Q50 83 62 80.5", 1.4)});
  }
}

// ---------------------------------------------------------------- лесные эльфы
void woodElves(Catalog& c) {
  c.group("Лесные Эльфы", EmblemSection::Races);
  // Лесной олень: голова анфас с ветвистыми рогами, на отростках — листья; уши-листья, строгие глаза.
  {
    std::vector<L> v;
    const Ax beam{{44, 39}, {31, 39}, {12, 31}, {13, 5}};
    string ant = band(beam, [](double u) { return 6.4 - 3.4 * u; }), leaves, ribs;
    const std::tuple<double, double, double> tines[] = {{0.1, 1, -12}, {0.42, 4, -13}, {0.7, 5, -12}};
    for (auto [t, dx, dy] : tines) {
      const CurvePt q = bezier(beam.p0, beam.c1, beam.c2, beam.p1, t);
      const Pt b{float(q.x), float(q.y)}, e{float(q.x + dx), float(q.y + dy)};
      ant += tongue(ax(b, e, -0.12), 3.8 - t);
      if (t > 0.3) {
        const Ax l = ax(e, {float(e.x + 9), float(e.y - 3)}, -0.12);
        leaves += leaf(l, 5.4);
        ribs += axLine(l, 0.2, 0.7, 6);
      }
    }
    const Ax l0 = ax({13, 5}, {2, 10}, 0.12), l1 = ax({14, 8}, {24, 3}, -0.12);
    leaves += leaf(l0, 5.4) + leaf(l1, 5);
    ribs += axLine(l0, 0.2, 0.7, 6) + axLine(l1, 0.2, 0.7, 6);
    v.push_back(G::F(sym(ant)));
    front(v, sym(leaves), 1.6f);
    v.push_back(G::XS(sym(ribs), 1.2f));
    const Ax ear = ax({39, 47}, {13, 55}, 0.1);
    front(v, sym(leaf(ear, 11)), 2.f);
    v.push_back(G::X(sym(leaf(ax({36, 48.5}, {19, 53.5}, 0.1), 4.5))));
    const string head = symSpline({{50, 36}, {43, 37}, {37.5, 42}, {35.5, 52}, {38, 64}, {42, 76}, {43.5, 86}, {46.5, 92}, {50, 93.5}});
    front(v, head, 2.4f);
    v.push_back(G::X(sym(tongue(ax({45, 57}, {37.5, 53}, 0.05), 3, 0.6) + lens(46.5, 88, 1.8, 0.9, 70))));
    v.push_back(G::XS(sym("M44.5 40.5C46 43 47.5 44.5 50 45.5"), 1.3));
    c.add("race-wood-elves-stag", "Лесной олень", v);
  }
  // Стрела в листе: стрела с листовидным наконечником пронзает большой лист с прожилками.
  {
    const Ax lf = ax({15, 86}, {86, 13}, 0.05, -0.03);
    const string leafS = leaf(lf, 46) + tongue(ax({18, 83}, {7, 95}, -0.1), 3.4, 0.4);
    string veins = axLine(lf, 0.04, 0.9, 20);
    for (double t = 0.16; t < 0.86; t += 0.13) {
      const CurvePt q = bezier(lf.p0, lf.c1, lf.c2, lf.p1, t);
      const double tx = q.ny, ty = -q.nx, L = 17 * std::sin(kPi * std::pow(t, 0.8)) + 3;
      for (int s : {-1, 1})
        veins += "M" + n(q.x) + " " + n(q.y) + "L" + n(q.x + L * (0.62 * tx + s * 0.78 * q.nx)) + " " + n(q.y + L * (0.62 * ty + s * 0.78 * q.ny));
    }
    const string shaft = "M84 82.5 82.5 84 22 23.5 23.5 22Z";
    const string head = xf("M0 -15C4.5 -10 6 -5 5.5 0C5 2 3 3.5 0 4.5C-3 3.5 -5 2 -5.5 0C-6 -5 -4.5 -10 0 -15Z", place(17, 17, -45));
    string fletch;
    for (int k = 0; k < 2; k++) {
      const float o = float(k * 6.5);
      fletch += xf("M0 0C-3 4-5 8-5.5 13L0 9.5C0 6 0 3 0 0ZM0 0C3 4 5 8 5.5 13L0 9.5C0 6 0 3 0 0Z", place(80 - o * 0.707f, 80 - o * 0.707f, -45, 1.05f));
    }
    const string arrow = shaft + head + fletch;
    c.add("race-wood-elves-arrow", "Стрела в листе", {G::F(leafS), G::XS(veins, 1.5), G::XS(arrow, 4), G::F(arrow), G::XS(xf("M0 -11V1", place(17, 17, -45)), 1.3)});
  }
  // Сова на ветке: ушные пучки, лицевой диск, строгие узкие глаза, сложенные крылья с перьями, когти на ветви.
  {
    std::vector<L> v;
    v.push_back(G::F(band(ax({4, 90}, {96, 88}, 0.02), [](double u) { return 5.5 - 1.5 * u; }) + leaf(ax({10, 90}, {3, 78}, -0.1), 6) +
                     leaf(ax({88, 88}, {97, 98}, 0.1), 6)));
    const string owl = symSpline({{50, 17}, {42, 16}, {31, 3, 0}, {31, 18}, {26, 30}, {25, 46}, {28, 62}, {35, 75}, {43, 84}, {50, 87}});
    front(v, owl, 2.4f);
    const string wing = spline({{32, 38}, {38, 47}, {41, 60}, {41, 73}, {38, 84, 0}, {31, 74}, {27, 60}, {27, 47}});
    front(v, sym(wing), 1.8f);
    v.push_back(G::XS(sym("M29 52C32 54 36 54 39 52M29 60C32 62 36 62 40 60M30 68C33 70 36 70 40 68"), 1.3));
    v.push_back(G::XS(sym("M50 22C44 19 35 20 32 27C29 35 33 43 41 45C45 46 48 44 50 41"), 1.5));
    v.push_back(G::X(sym(lens(40.5, 33, 5, 2, 12))));
    v.push_back(G::F(sym(circ(41, 33.3, 1.5))));
    v.push_back(G::XS(sym("M33 26.5 47 31"), 1.6));
    v.push_back(G::F("M47 37H53L50 46Z"));
    v.push_back(G::XS("M45 54 47.5 57 50 54 52.5 57 55 54M44 62 46.5 65 49 62M51 62 53.5 65 56 62M45 70 47.5 73 50 70 52.5 73 55 70", 1.4));
    v.push_back(G::F(sym(tongue(ax({45, 85}, {42, 92}, 0.2), 2.6) + tongue(ax({47.5, 86}, {47, 93}, 0.1), 2.6))));
    c.add("race-wood-elves-owl", "Сова", v);
  }
  // Венок с луком: дубовые ветви с лопастными листьями и желудями, лук с наложенной стрелой нацелен вверх.
  {
    std::vector<L> v{G::F(sym(taperArc(50, 54, 37, 96, 240, 3.4, 1.4)))};
    // Листья парами вдоль ветви, остриём к макушке венка: наружный и внутренний.
    for (int i = 0; i < 8; i++) {
      const double a = 102 + 18 * i;
      const Pt p = polar(50, 54, 37, a);
      for (double side : {1.0, -1.0}) {
        const double d = (a + 90 - 30 * side) * kPi / 180;
        const Pt t{float(p.x + 13.5 * std::cos(d)), float(p.y + 13.5 * std::sin(d))};
        const Ax l = ax(p, t, -0.06 * side);
        front(v, sym(leaf(l, 7.5)), 1.5f);
        v.push_back(G::XS(sym(axLine(l, 0.2, 0.7, 6)), 1.2f));
      }
    }
    const Ax limb = ax({50, 41}, {13, 63}, 0.06, -0.34);
    const string bow = sym(band(limb, [](double u) { return 6 - 3.8 * u; })) + rrect(46.5, 36, 7, 10, 2);
    const string arrow = rrect(48.6, 18, 2.8, 50, 1.2) + "M50 3C53.5 7 55 11 54.5 15C54 17 52.5 18.5 50 19.5C47.5 18.5 46 17 45.5 15C45 11 46.5 7 50 3Z" +
                         sym("M48.6 53C46 56 44 60 43.5 66L48.6 63Z");
    v.push_back(G::S("M14 63.5H86", 1.4));
    front(v, bow + arrow, 2.4f);
    v.push_back(G::XS("M46.5 41H53.5M50 6.5V16", 1.3));
    c.add("race-wood-elves-wreath", "Венок с луком", v);
  }
}

// ---------------------------------------------------------------- луниды
void lunids(Catalog& c) {
  c.group("Луниды", EmblemSection::Races);
  // Лунный мотылёк: серповидные передние крылья, задние — с длинными хвостами, глазки-полумесяцы, перистые усики.
  {
    std::vector<L> v;
    const string fore = spline({{47, 31}, {36, 21}, {22, 14}, {4, 10, 0}, {9, 25}, {17, 38}, {29, 46}, {41, 47}, {47, 43}});
    const string hind = spline({{47, 47}, {36, 50}, {26, 58}, {24, 68}, {29, 75}, {27, 85}, {19, 98, 0}, {34, 86}, {40, 76}, {46, 63}});
    v.push_back(G::F(sym(fore + hind)));
    v.push_back(G::XS(sym("M45 35C35 28 23 21 9 13M45 40C36 38 26 37 14 31M46 52C39 58 33 65 29 74"), 1.4));
    v.push_back(G::X(sym(crescent(26, 31, 6.5, 28.5, 29, 5.2) + crescent(35, 63, 4.6, 37, 61.5, 3.6))));
    v.push_back(G::XS(sym(xf(fore, scaleAt(0.86f, 30, 30))), 1.3));
    const string body = ell(50, 52, 4.8, 21) + ell(50, 28.5, 4.4, 4);
    front(v, body, 2.4f);
    v.push_back(G::XS("M45.5 46H54.5M45.5 52H54.5M46 58H54M47 64H53", 1.3));
    string ant;
    const Ax a = ax({48, 25}, {31, 4}, 0.12);
    for (double t : {0.2, 0.36, 0.52, 0.68, 0.84}) {
      const CurvePt q = bezier(a.p0, a.c1, a.c2, a.p1, t);
      ant += tongue(ax({float(q.x), float(q.y)}, {float(q.x - 4.5), float(q.y + 1.5)}), 1.5, 1) +
             tongue(ax({float(q.x), float(q.y)}, {float(q.x + 1.5), float(q.y - 4)}), 1.5, 1);
    }
    v.push_back(G::F(sym(band(a, [](double u) { return 2.2 - 1.2 * u; }) + ant)));
    c.add("race-lunids-moth", "Лунный мотылёк", v);
  }
  // Око полумесяца: миндалевидный глаз над чашей полумесяца, лучи-ресницы, звёзды.
  {
    string lashes;
    for (int k = 0; k < 7; k++) {
      const double a = -160 + 23.3 * k;
      lashes += tongue(ax(polar(50, 46, 18, a), polar(50, 46, 28 - std::abs(k - 3) * 1.5, a)), 3.6, 1.1);
    }
    const string lids = "M24 46C33 33 67 33 76 46C67 59 33 59 24 46Z";
    c.add("race-lunids-eye", "Око полумесяца",
          {G::F(crescent(50, 56, 42, 50, 40, 37.5) + lashes), G::XS(lids, 3.4), G::F(lids), G::X("M30.5 46C38 38 62 38 69.5 46C62 54 38 54 30.5 46Z"),
           G::F(circ(50, 46, 7.5)), G::X("M50 40C51.6 43 51.6 49 50 52C48.4 49 48.4 43 50 40Z"),
           G::F(star(50, 8, 7.5, 1.6, 4, -90) + star(27, 14, 4.5, 1.1, 4, -90) + star(73, 14, 4.5, 1.1, 4, -90)),
           G::XS(crescent(50, 56, 38.5, 50, 44.5, 34), 1.3)});
  }
  // Лунный цветок: пятилепестковый цветок с острыми лепестками над чашей полумесяца.
  {
    std::vector<L> v{G::F(crescent(50, 52, 46, 50, 38, 43.5))};
    v.push_back(G::XS(crescent(50, 52, 42, 50, 41, 38.5), 1.3));
    string petals, ribs;
    for (int k = 0; k < 5; k++) {
      const double a = -90 + 72 * k;
      const Ax p = ax(polar(50, 42, 3, a), polar(50, 42, 37, a));
      petals += leaf(p, 22);
      ribs += axLine(p, 0.25, 0.8, 8);
    }
    front(v, petals, 2.6f);
    v.push_back(G::XS(ribs, 1.4));
    string inner;
    for (int k = 0; k < 5; k++) inner += leaf(ax(polar(50, 42, 3, -54 + 72 * k), polar(50, 42, 17, -54 + 72 * k)), 9);
    front(v, inner, 1.8f);
    v.push_back(G::X(circ(50, 42, 4.2)));
    v.push_back(G::F(star(50, 42, 3.4, 1.2, 5, -90)));
    c.add("race-lunids-flower", "Лунный цветок", v);
  }
  // Фазы луны: полная луна с кратерами между растущим и убывающим серпами, звёзды.
  c.add("race-lunids-phases", "Фазы луны",
        {G::F(crescent(25, 50, 22, 33, 50, 19.5) + crescent(75, 50, 22, 67, 50, 19.5)), G::XS(circ(50, 50, 21), 4), G::F(circ(50, 50, 21)),
         G::XS(circ(50, 50, 17.5), 1.4), G::X(ell(44, 44, 4, 3) + ell(57, 55, 4.6, 3.4) + ell(55, 41, 2.4, 1.8) + ell(42, 57, 2.4, 1.8)),
         G::F(star(50, 12, 8, 1.6, 4, -90) + star(50, 88, 8, 1.6, 4, -90) + star(19, 18, 4.5, 1, 4, -90) + star(81, 82, 4.5, 1, 4, -90))});
}

// ---------------------------------------------------------------- лунные эльфы
void moonElves(Catalog& c) {
  c.group("Лунные Эльфы", EmblemSection::Races);
  // Дева полумесяца: профиль эльфийки с острым ухом и диадемой в чаше полумесяца, пряди волос стекают в серп.
  {
    std::vector<L> v;
    v.push_back(G::F(crescent(46, 50, 46, 62, 46, 39)));
    // Профиль вправо: лоб, переносица, нос, губы, подбородок, длинная шея; затылок и волосы уходят в серп.
    const string face = spline({{60, 93, 0}, {57, 80}, {55, 66}, {56, 60}, {61, 57}, {63.5, 54}, {62.5, 51.5}, {65, 49.5}, {63.5, 47.5}, {65.5, 46},
                                {68.5, 42, 0}, {64.5, 35.5}, {63.5, 32.5}, {63, 27}, {59, 20}, {51, 16}, {42, 18}, {36, 25}, {35, 36},
                                {38, 48}, {43, 60}, {45, 70}, {44, 93, 0}});
    const Ax h0 = ax({38, 22}, {12, 74}, -0.08, 0.1), h1 = ax({37, 34}, {21, 86}, -0.06, 0.1), h2 = ax({40, 46}, {32, 95}, -0.04, 0.08);
    v.push_back(G::F(tongue(h0, 11, 1) + tongue(h1, 9, 1) + tongue(h2, 7, 1)));
    v.push_back(G::XS(axLine(h0, 0.25, 0.85, 10) + axLine(h1, 0.2, 0.85, 10), 1.3));
    front(v, face + tongue(ax({45, 41}, {35, 30}, 0.08), 3.8, 1), 2.4f);
    v.push_back(G::XS("M57 33.5C59.5 35 61.5 35 63 33.5", 1.4));
    v.push_back(G::XS("M43 19.5C50 21 57 24 62 28.5", 1.6));
    v.push_back(G::F(star(53, 20.5, 4.2, 1.2, 4, -90)));
    v.push_back(G::XS("M45 63C49 66 53 67 56 66", 1.4));
    v.push_back(G::F(star(84, 16, 9, 1.6, 4, -90) + star(88, 74, 5, 1, 4, -90) + star(76, 36, 3.6, 0.8, 4, -90)));
    c.add("race-moon-elves-maiden", "Дева полумесяца", moved(v, Affine::translate(2, 0)));
  }
  // Лунная сова: сова с распахнутыми крыльями из перьев-лопастей перед полной луной.
  {
    std::vector<L> v;
    v.push_back(G::F(circ(50, 38, 32)));
    v.push_back(G::XS(circ(50, 38, 28.5), 1.4));
    // Крыло поднято: маховые перья свисают от плеча до запястья (от внутренних к внешним), сверху — кроющие.
    const Pt fb[] = {{42, 50}, {37, 45}, {31, 40}, {25, 34}, {19, 28}, {13, 22}};
    const Pt ft[] = {{36, 84}, {26, 81}, {17, 75}, {9, 65}, {4, 53}, {3, 38}};
    for (int k = 0; k < 6; k++) {
      const Ax a = ax(fb[k], ft[k], 0.04);
      v.push_back(G::XS(sym(leaf(a, 10)), 2.f));
      v.push_back(G::F(sym(leaf(a, 10))));
      v.push_back(G::XS(sym(axLine(a, 0.3, 0.8, 8)), 1.2));
    }
    const string covert = sym(band(ax({46, 50}, {9, 16}, -0.1, -0.05), [](double u) { return 10 - 5 * u; }) + tongue(ax({11, 18}, {4, 8}, 0.1), 5.5));
    front(v, covert, 2.2f);
    v.push_back(G::XS(sym("M42 45C34 40 26 33 17 24"), 1.3));
    const string owl = symSpline({{50, 30}, {44, 30}, {38, 22, 0}, {38, 33}, {37.5, 44}, {39, 58}, {42, 70}, {46, 78}, {50, 80}}) +
                       "M44 76 50 92 56 76Z";
    front(v, owl, 2.4f);
    v.push_back(G::XS(sym("M50 34C46 32 41 33 40 37C39 41 42 45 46 46C48 46.5 49 45.5 50 44"), 1.3));
    v.push_back(G::X(sym(lens(44.5, 39, 3.6, 1.4, 12))));
    v.push_back(G::XS(sym("M40.5 34.5 48.5 37.5"), 1.4));
    v.push_back(G::F("M48 41H52L50 47Z"));
    v.push_back(G::XS("M45 54 47.5 57 50 54 52.5 57 55 54M44.5 61 47 64 49.5 61M50.5 61 53 64 55.5 61M45.5 68 48 71 50 68 52 71 54.5 68", 1.3));
    v.push_back(G::XS("M50 80V91", 1.2));
    c.add("race-moon-elves-owl", "Лунная сова", moved(v, Affine::translate(0, 2)));
  }
  // Звёздный лук: лук-полумесяц с острыми концами, тетива, стрела со звездой на острие.
  c.add("race-moon-elves-bow", "Звёздный лук",
        {G::F(crescent(50, 62, 42.5, 50, 78, 37)), G::XS(crescent(50, 62, 39, 50, 72, 35), 1.3), G::S("M10.5 76 50 87 89.5 76", 1.5),
         G::XS("M50 92V22", 7), G::F(rrect(48.2, 20, 3.6, 70, 1.4) + "M48.2 80C45 83 42.5 88 42 95L48.2 91ZM51.8 80C55 83 57.5 88 58 95L51.8 91Z"),
         G::F(star(50, 13, 12, 3.2, 4, -90)), G::X(star(50, 13, 3.6, 1.2, 4, -45)), G::F(star(20, 13, 6, 1.3, 4, -90) + star(80, 13, 6, 1.3, 4, -90))});
  // Лунный олень: олень в прыжке над чашей полумесяца, рога закинуты назад, звёзды.
  {
    std::vector<L> v;
    v.push_back(G::F(crescent(50, 54, 44, 50, 40, 41.5)));
    v.push_back(G::XS(crescent(50, 54, 40.5, 50, 43, 38), 1.3));
    const string body = spline({{30, 44}, {38, 38}, {52, 36}, {66, 37}, {76, 40}, {80, 46}, {74, 51}, {62, 53}, {46, 53}, {34, 51}});
    const string neck = spline({{36, 46}, {29, 34}, {27, 26}, {32, 21}, {38, 28}, {42, 38}});
    const string head = spline({{34, 21}, {28, 18}, {20, 20}, {13, 25, 0}, {17, 28}, {25, 27.5}, {32, 26}});
    const string legs = band(ax({36, 49}, {24, 56}, 0.1), [](double u) { return 5 - 1.5 * u; }) + tongue(ax({24, 56}, {11, 54}, 0.1), 3.4, 0.6) +
                        band(ax({41, 51}, {32, 61}, 0.1), [](double u) { return 4.5 - 1.2 * u; }) + tongue(ax({32, 61}, {20, 64}, 0.1), 3.2, 0.6) +
                        band(ax({70, 49}, {84, 54}, -0.1), [](double u) { return 6 - 2.5 * u; }) + tongue(ax({84, 54}, {96, 50}, -0.1), 3.4, 0.6) +
                        band(ax({74, 46}, {88, 46}, -0.1), [](double u) { return 5.5 - 2 * u; }) + tongue(ax({88, 46}, {97, 40}, -0.1), 3.2, 0.6);
    const Ax beam = ax({30, 21}, {52, 9}, -0.26, -0.1);
    string ant = band(beam, [](double u) { return 3.4 - 2 * u; });
    for (double t : {0.25, 0.55, 0.85}) {
      const CurvePt q = bezier(beam.p0, beam.c1, beam.c2, beam.p1, t);
      ant += tongue(ax({float(q.x), float(q.y)}, {float(q.x - 1), float(q.y - 7)}, 0.1), 2.6);
    }
    front(v, body + neck + head + legs + ant + tongue(ax({33, 22}, {42, 17}, -0.1), 4) + tongue(ax({79, 43}, {86, 38}, -0.1), 3.6), 2.4f);
    v.push_back(G::X(lens(25, 22.5, 2, 0.9, -10)));
    v.push_back(G::XS("M40 40C37 44 37 48 39 52M70 40C67 44 67 48 69 51", 1.3));
    v.push_back(G::F(star(84, 14, 7, 1.4, 4, -90) + star(66, 8, 4, 0.9, 4, -90) + star(10, 40, 3.6, 0.8, 4, -90)));
    c.add("race-moon-elves-stag", "Лунный олень", v);
  }
}

// ---------------------------------------------------------------- люди
// Лапа с когтями: подушка в (x, y), три крючковатых когтя в направлении deg, размер s.
string paw(double x, double y, double deg, double s = 1) {
  string d = ell(0, 0, 5.2, 4);
  for (double o : {-3.0, 0.0, 3.0}) d += tongue(ax({3.5, float(o)}, {8.5, float(o * 1.3 + 2.2)}, -0.32), 2.4, 0.8);
  return xf(d, place(float(x), float(y), float(deg), float(s)));
}

void humans(Catalog& c) {
  c.group("Люди", EmblemSection::Races);
  // Королевский лев: восстающий лев в короне, грива прядями-завитками назад, раскрытая пасть с языком, хвост S-изгибом
  // с кистью-пламенем.
  {
    std::vector<L> v;
    auto limb = [](const std::vector<Pt>& p, double w0, double w1) {
      return tube(p, [w0, w1](double u) { return w1 + (w0 - w1) * std::pow(1 - u, 0.8); }, 2).fill + circ(p[0].x, p[0].y, w0 / 2);
    };
    const string tail = limb({{66, 64}, {81, 67}, {87, 57}, {80, 45}, {81, 34}, {86, 28}}, 5, 2.8);
    const string tuft = flame({85, 31}, {81, 9}, 10, 3.5) + flame({87, 31}, {92, 11}, 8.5, -3) + flame({88, 32}, {97, 21}, 6.5, -2.5) + circ(86.5, 31, 4);
    const string foreFar = limb({{45, 52}, {36, 58}, {28, 60}, {23, 60}}, 9.5, 6) + paw(19.5, 60, 180);
    const string hindFar = limb({{59, 70}, {49, 73}, {45, 80}, {40, 84}}, 13, 6) + paw(35.5, 84.5, 180);
    v.push_back(G::F(tail + tuft + foreFar + hindFar));
    v.push_back(G::XS("M84.5 27C84 22 83 17 81.5 12M87.5 27C88.5 22 89.5 18 91 13.5", 1.2));
    const string torso = spline({{34, 29}, {46, 31}, {56, 39}, {62, 50}, {66, 60}, {70, 68}, {66, 76}, {58, 76}, {52, 68}, {45, 62}, {37, 57},
                                 {30, 48}, {28, 38}});
    front(v, torso, 2.f);
    v.push_back(G::XS("M47 45C51 49 54 54 55 60M52 40C57 44 60 50 61 56", 1.4));
    const std::pair<Pt, Pt> lk[] = {{{36, 9}, {53, 3}}, {{40, 14}, {58, 12}}, {{41, 20}, {60, 23}}, {{40, 26}, {57, 34}},
                                    {{37, 31}, {50, 44}}, {{31, 34}, {38, 49}}, {{25, 34}, {26, 47}}};
    for (auto [b, t] : lk) {
      const Ax l = ax(b, t, 0.2, -0.22);
      front(v, tongue(l, 10, 0.85), 1.6f);
      v.push_back(G::XS(axLine(l, 0.4, 0.75, 6), 1.2));
    }
    const string head = spline({{37, 8}, {29, 6.5}, {21, 9}, {16, 13}, {11.5, 16, 0}, {11.5, 20}, {14, 23}, {22, 24.5, 0}, {14, 26.5, 0}, {14.5, 30},
                                {20, 33}, {28, 33}, {35, 28}, {38, 18}});
    const string crown = "M23.5 8.5 22 1.5 27 5 30.5 0.8 34 5 39 1.5 37.5 8.5Z";
    const string lick = tongue(ax({20, 25.5}, {4, 30}, -0.3, 0.25), 3.6, 0.7) + tongue(ax({15, 23}, {15.5, 26.5}), 1.8, 0.8);
    front(v, head + crown + lick, 2.2f);
    v.push_back(G::X(lens(24, 13.5, 2.6, 1, -15) + "M24 7.2H37L36.8 8.6H24.2Z"));
    v.push_back(G::XS("M19 10.5C22 10 25 10.5 28 12M13 19.5C15 19.5 17 20 18.5 21", 1.3));
    const string hindNear = limb({{63, 66}, {58, 78}, {62, 86}, {65, 89}, {61, 93}}, 15, 5.5) + paw(57, 93.5, 180);
    const string foreNear = limb({{40, 41}, {31, 46}, {24, 43}, {20, 40}}, 10, 6) + paw(16.5, 38.5, -150);
    front(v, hindNear, 2.2f);
    front(v, foreNear, 2.2f);
    c.add("race-humans-lion", "Королевский лев", v);
  }
  // Крепость с флагами: две башни под коническими кровлями с раздвоенными вымпелами, стена с зубцами, ворота.
  {
    const string tower = rrect(9, 34, 21, 59, 0.8) + "M5 35C11 28 16 18 19.5 7C23 18 28 28 34 35Z" + rrect(18.8, 1.5, 1.6, 8, 0.6) +
                         "M20.4 2 33 4.5 28.5 6.5 33 8.5 20.4 9.5Z";
    const string wall = rrect(28, 50, 44, 43, 0) + "M28 43H34.5V50H28ZM39 43H45.5V50H39ZM54.5 43H61V50H54.5ZM65.5 43H72V50H65.5Z";
    c.add("race-humans-fortress", "Крепость с флагами",
          {G::F(wall), G::XS(sym(tower), 2.6), G::F(sym(tower) + rrect(4, 91, 92, 6, 1.5)),
           G::X("M40 92V73C40 67 44.5 63 50 63C55.5 63 60 67 60 73V92Z" + sym(rrect(18, 43, 3, 10, 1.5) + rrect(18, 62, 3, 10, 1.5)) +
                rrect(48, 53, 4, 6, 2)),
           G::S("M44 65V92M50 63V92M56 65V92M41 75H59M40.5 83H59.5", 1.5), G::XS(sym("M7 34.5H32M9 80H30"), 1.4), G::XS("M28 50.5H72", 1.4),
           G::XS(sym("M12 33C15 26 17.5 19 19.5 12"), 1.3), G::XS("M30 62H38M62 62H70M30 74H36.5M63.5 74H70", 1.3)});
  }
  // Мечи под короной: два меча крест-накрест остриём вверх, над ними корона с зубцами-остриями.
  {
    const string blade = "M50 3 55.5 12.5V64H44.5V12.5Z";
    const string guard = lens(50, 66, 19, 3.6);
    const string sword = blade + guard + rrect(46.5, 67, 7, 16, 2) + "M50 81.5 55.5 88 50 95 44.5 88Z";
    std::vector<L> v = crossed(sword, "M50 86 52.2 88 50 90.5 47.8 88Z", "M50 15V60M46.5 72H53.5M46.5 77H53.5", 1.4, 33, 50, 58, 1.04f, 4.5);
    const string crown = "M33 27 30.5 11 38.5 18.5 43 8 47 16.5 50 3 53 16.5 57 8 61.5 18.5 69.5 11 67 27Z" + rrect(32, 25.5, 36, 7.5, 2);
    front(v, crown, 4.f);
    v.push_back(G::X(poly({40, 29.2, 42.2, 27, 44.4, 29.2, 42.2, 31.4}) + poly({50, 26.7, 52.5, 29.2, 50, 31.7, 47.5, 29.2}) +
                     poly({55.6, 29.2, 57.8, 27, 60, 29.2, 57.8, 31.4})));
    v.push_back(G::XS("M33 25H67", 1.3));
    c.add("race-humans-swords", "Мечи под короной", v);
  }
  // Двуглавый орёл: головы в коронах смотрят в стороны, крылья подняты, маховые перья, хвост веером, щиток на груди.
  {
    std::vector<L> v;
    const Pt fb[] = {{40, 54}, {35, 49}, {29, 44}, {23, 38}, {17, 32}, {11, 26}};
    const Pt ft[] = {{33, 80}, {23, 77}, {14, 70}, {7, 60}, {3, 48}, {2, 34}};
    for (int k = 0; k < 6; k++) {
      const Ax a = ax(fb[k], ft[k], 0.04);
      v.push_back(G::XS(sym(leaf(a, 9.5)), 2.f));
      v.push_back(G::F(sym(leaf(a, 9.5))));
      v.push_back(G::XS(sym(axLine(a, 0.3, 0.8, 8)), 1.2));
    }
    front(v, sym(band(ax({46, 52}, {8, 18}, -0.1, -0.05), [](double u) { return 10 - 4.5 * u; }) + tongue(ax({10, 20}, {4, 9}, 0.1), 5.5)), 2.2f);
    v.push_back(G::XS(sym("M42 47C34 42 25 34 15 25"), 1.3));
    const string tail = sym(tongue(ax({47, 72}, {37, 94}, 0.06), 6.5, 0.8) + tongue(ax({48.5, 72}, {45, 96}, 0.03), 6.5, 0.8)) +
                        tongue(ax({50, 72}, {50, 97}), 6.5, 0.8);
    front(v, tail, 2.f);
    const string legs = sym(band(ax({44, 66}, {36, 76}), [](double u) { return 5 - u; }) + tongue(ax({36, 76}, {28, 74}, 0.15), 3) +
                            tongue(ax({36, 76}, {30, 82}, 0.15), 3) + tongue(ax({36, 76}, {37, 84}, -0.15), 3));
    v.push_back(G::F(legs));
    const string body = symSpline({{50, 36}, {43, 39}, {40, 50}, {42, 62}, {50, 72}});
    const string heads = sym(band(ax({46, 42}, {36, 22}, 0.12), [](double u) { return 8 - 2 * u; }) +
                             spline({{41, 15}, {34, 14}, {28, 16.5}, {25, 21, 0}, {28, 22}, {26, 25, 0}, {31, 25}, {37, 25.5}, {41, 22}}) +
                             "M30.5 14.5 29.5 6 33 10 36 4 39 10 42.5 6 41.5 14.5Z");
    front(v, body + heads, 2.4f);
    v.push_back(G::X(sym(lens(34.5, 18.5, 2, 0.9, -10) + "M31 12.8H41L40.8 14.2H31.2Z")));
    const string shield = "M43 44H57V54C57 59.5 54 62.5 50 64.5C46 62.5 43 59.5 43 54Z";
    v.push_back(G::XS(shield, 1.6));
    v.push_back(G::XS("M50 47V60M45.5 51.5H54.5", 1.4));
    c.add("race-humans-eagle", "Двуглавый орёл", v);
  }
}

// ---------------------------------------------------------------- людоящеры
void lizardfolk(Catalog& c) {
  c.group("Людоящеры", EmblemSection::Races);
  // Воин-ящер: стоит в профиль, держит копьё; гребень из шипов на голове и шее, длинный хвост.
  {
    std::vector<L> v;
    auto limb = [](const std::vector<Pt>& p, double w0, double w1) {
      return tube(p, [w0, w1](double u) { return w1 + (w0 - w1) * std::pow(1 - u, 0.8); }, 2).fill + circ(p[0].x, p[0].y, w0 / 2);
    };
    const Ax tailA = ax({56, 58}, {97, 93}, 0.22, -0.14);
    const string back = tongue(tailA, 13, 0.95) + limb({{48, 60}, {43, 72}, {49, 84}, {44, 93}}, 10, 4.5) + tongue(ax({44, 93}, {34, 96}, 0.1), 4);
    v.push_back(G::F(back));
    v.push_back(G::XS(axAcross(tailA, 0.3, 6, 6) + axAcross(tailA, 0.45, 5, 5) + axAcross(tailA, 0.6, 4, 4) + axAcross(tailA, 0.75, 3, 3), 1.4));
    const string spear = rrect(20.4, 14, 3.4, 83, 1.4) + leaf(ax({22.1, 17}, {22.1, 1}), 7.5) + lens(22.1, 17.5, 5, 1.4);
    front(v, spear, 2.2f);
    v.push_back(G::XS("M22.1 5V14", 1.2));
    string crest;
    const std::pair<Pt, Pt> sp[] = {{{43, 10}, {46, 1}}, {{49, 10}, {55, 2.5}}, {{54, 12}, {63, 6}}, {{57, 17}, {67, 13}}, {{58, 23}, {67, 22}}, {{59, 29}, {67, 31}}};
    for (auto [b, t] : sp) crest += tongue(ax(b, t, -0.1), 5.5, 1);
    const string head = spline({{58, 14}, {50, 9}, {40, 9}, {32, 11.5}, {25, 15, 0}, {27, 18.5}, {34, 20}, {42, 22}, {50, 24}, {57, 22}});
    const string torso = spline({{52, 20}, {58, 26}, {61, 38}, {59, 52}, {55, 62}, {46, 63}, {43, 50}, {44, 36}, {47, 26}});
    const string leg = limb({{53, 60}, {61, 72}, {57, 84}, {51, 93}}, 11, 4.5) + tongue(ax({51, 93}, {40, 96}, 0.1), 4.5) + tongue(ax({52, 94}, {47, 98}), 3);
    const string arm = limb({{51, 32}, {42, 43}, {28, 38}}, 8, 5.5);
    front(v, head + torso + crest + leg + arm, 2.4f);
    v.push_back(G::X(lens(41, 14, 2.6, 1, -10)));
    v.push_back(G::XS("M27 17C32 17.5 38 18.5 44 20M49 46 54 47M48.5 52 54.5 53M49 58 54 58.5M55 70C57 74 58 78 57 82M46 33C43 37 40 39 36 38.5", 1.4));
    v.push_back(G::F(lens(22.1, 38.5, 3.6, 3.4, 90)));
    c.add("race-lizardfolk-warrior", "Воин-ящер", moved(v, Affine::translate(-6, 0)));
  }
  // Череп крокодила (сверху): глазницы, височные окна, острые зубы по краям длинной морды, гравированный гребень.
  {
    const string skull = symSpline({{50, 4}, {38, 5}, {27, 10}, {22, 20}, {25, 31}, {33, 40}, {37, 52}, {39, 66}, {41, 80}, {42, 88}, {45, 95}, {50, 96}});
    string teeth;
    for (int i = 0; i < 8; i++) {
      const double y = 46 + 6 * i, x = 37.2 + (y - 52) * 4.6 / 36;
      teeth += "M" + n(x + 1) + " " + n(y - 2.2) + "L" + n(x - 4.6) + " " + n(y + 1.6) + "L" + n(x + 1) + " " + n(y + 2.2) + "Z";
    }
    c.add("race-lizardfolk-skull", "Череп крокодила",
          {G::F(sym(teeth) + skull), G::X(sym(ell(36, 25, 5.4, 6.6))), G::X(sym(lens(42, 11.5, 3.4, 1.8, 20))), G::X(sym(lens(47.5, 90, 2.4, 1.2, 70))),
           G::XS("M50 4V18M50 34V84", 1.4), G::XS(sym("M43 50C44 62 45 74 46 84M29 36C33 39 36 42 38 46"), 1.4)});
  }
  // Болотный храм: хижина на сваях под крутой соломенной кровлей со скрещёнными коньками, лестница к воде, камыши.
  {
    std::vector<L> v;
    string waves;
    for (int k = 0; k < 4; k++) waves += tongue(ax({float(6 + k * 23), 95}, {float(26 + k * 23), 93}, 0.18, -0.1), 3.4, 0.9);
    v.push_back(G::F(waves));
    v.push_back(G::F(rrect(20, 66, 4, 25, 1) + rrect(35, 66, 4, 25, 1) + rrect(61, 66, 4, 25, 1) + rrect(76, 66, 4, 25, 1)));
    v.push_back(G::S("M46 68V89M54 68V89", 1.8));
    v.push_back(G::S("M46 73H54M46 78H54M46 83H54", 1.5));
    front(v, rrect(12, 62, 76, 5, 1.5) + rrect(24, 45, 52, 18, 1), 2.f);
    v.push_back(G::X("M44 63V55C44 51.5 46.5 49.5 50 49.5C53.5 49.5 56 51.5 56 55V63Z"));
    v.push_back(G::XS("M27 50H41M27 55H41M59 50H73M59 55H73", 1.3));
    const string roof = "M50 10C58 22 70 36 89 47H11C30 36 42 22 50 10Z";
    front(v, roof + tongue(ax({50, 11}, {38, 1}, 0.1), 3.4) + tongue(ax({50, 11}, {62, 1}, -0.1), 3.4), 2.6f);
    v.push_back(G::XS("M50 14V47M43 22C39 30 33 38 24 46M57 22C61 30 67 38 76 46M37 29H63M27 38H73", 1.4));
    v.push_back(G::F(sym(leaf(ax({9, 94}, {7, 55}, 0.04), 3.6) + leaf(ax({12, 94}, {20, 64}, -0.05), 3.2) + lens(7.6, 60, 4.6, 2, 92))));
    c.add("race-lizardfolk-temple", "Болотный храм", v);
  }
  // Яйца в гнезде: три яйца в плетёном гнезде, крапины, на среднем — трещина.
  {
    std::vector<L> v;
    const string eggL = xf(eggShape(0, 0, 11, 15), place(31, 51, -20)), eggR = xf(eggShape(0, 0, 11, 15), place(69, 51, 20));
    const string eggC = eggShape(50, 39, 14, 20.5);
    v.push_back(G::F(eggL + eggR));
    v.push_back(G::X(xf(lens(-3, -6, 1.8, 0.9, 30) + lens(3, 1, 1.5, 0.8, -20) + lens(-2, 6, 1.4, 0.7, 10), place(31, 51, -20)) +
                     xf(lens(3, -6, 1.8, 0.9, -30) + lens(-3, 1, 1.5, 0.8, 20) + lens(2, 6, 1.4, 0.7, -10), place(69, 51, 20))));
    front(v, eggC, 2.6f);
    v.push_back(G::X(lens(45, 34, 2, 1, 30) + lens(55.5, 45, 1.8, 0.9, -20) + lens(47, 51, 1.5, 0.8, 10) + lens(56, 30, 1.4, 0.7, -30)));
    v.push_back(G::XS("M41 25 45 29 43 32 48 35 46 39", 1.6));
    const string nest = "M4 57C8 81 28 95 50 95C72 95 92 81 96 57C84 65 68 68 50 68C32 68 16 65 4 57Z";
    front(v, nest + tongue(ax({6, 59}, {1, 50}, 0.1), 4) + tongue(ax({94, 59}, {99, 50}, -0.1), 4) + tongue(ax({12, 62}, {9, 52}, 0.1), 3) +
                 tongue(ax({88, 62}, {91, 52}, -0.1), 3), 3.f);
    v.push_back(G::XS("M10 73C26 82 74 82 90 73M17 82C32 89 68 89 83 82M22 71 25 80M40 75 41 85M60 75 59 85M78 71 75 80M33 83 34 90M67 83 66 90", 1.4));
    v.push_back(G::XS("M8 64C22 72 36 75 50 75C64 75 78 72 92 64", 1.6));
    c.add("race-lizardfolk-egg", "Яйца в гнезде", moved(v, Affine::translate(0, -4)));
  }
}

// ---------------------------------------------------------------- минотавры
void minotaurs(Catalog& c) {
  c.group("Минотавры", EmblemSection::Races);
  // Голова с кольцом: рога-полумесяцы с кольцами-насечками, чёлка прядями, тяжёлый лоб, гневные глаза, кольцо в носу.
  {
    std::vector<L> v;
    const Ax horn{{37, 30}, {20, 33}, {6, 22}, {11, 3}};
    v.push_back(G::F(sym(taperW(horn.p0, horn.c1, horn.c2, horn.p1, [](double u) { return 12 * std::pow(1 - u, 0.85) + 0.5; }))));
    v.push_back(G::XS(sym(axAcross(horn, 0.32, 5, 5) + axAcross(horn, 0.45, 4.5, 4.5) + axAcross(horn, 0.58, 4, 4)), 1.4));
    const string ears = sym(leaf(ax({33, 41}, {12, 46}, 0.1), 9));
    front(v, ears, 2.f);
    v.push_back(G::X(sym(leaf(ax({30, 42.5}, {17, 45.5}, 0.1), 3.6))));
    const string head = symSpline({{50, 24}, {38, 25}, {31, 31}, {31, 43}, {35, 55}, {36, 65}, {34, 75}, {37, 85}, {44, 90}, {50, 91}});
    const string forelock = tongue(ax({50, 24}, {50, 44}, 0.1, -0.1), 9) + sym(tongue(ax({44, 25}, {39, 40}, 0.1, -0.12), 7.5));
    front(v, head + forelock, 2.4f);
    v.push_back(G::XS(axLine(ax({50, 24}, {50, 44}, 0.1, -0.1), 0.3, 0.8, 6) + sym(axLine(ax({44, 25}, {39, 40}, 0.1, -0.12), 0.3, 0.8, 6)), 1.2));
    v.push_back(G::X(sym(tongue(ax({45, 51}, {33, 46}, 0.05), 3.4, 0.6))));
    v.push_back(G::XS(sym("M32 43 46 48.5M38 63C42 66 45 72 46 78"), 1.4));
    v.push_back(G::X(sym(lens(43.5, 81, 3.2, 1.6, 70))));
    v.push_back(G::XS(circ(50, 88, 7.5), 6.5));
    v.push_back(G::S(circ(50, 88, 7.5), 3));
    c.add("race-minotaurs-head", "Голова с кольцом", v);
  }
  // Лабрис: двусторонняя секира с лезвиями-полумесяцами, гравированная кромка, остриё на древке, обмотка.
  {
    const string blade = crescent(30, 44, 27, 47, 44, 24);
    const string blades = blade + xf(blade, Affine{-1, 0, 0, 1, 100, 0}) + rrect(20, 40, 60, 8, 2);
    const string shaft = rrect(46.5, 9, 7, 83, 3) + leaf(ax({50, 12}, {50, 1}), 7) + rrect(42, 33, 16, 22, 3) + lens(50, 93, 4, 3.2, 90);
    c.add("race-minotaurs-labrys", "Лабрис",
          {G::F(blades), G::XS(sym(arc(30, 44, 21.5, 132, 228)), 1.4), G::XS(sym("M23 40H36M23 48H36"), 1.3), G::XS(shaft, 3), G::F(shaft),
           G::XS("M46.5 65 53.5 68M46.5 70 53.5 73M46.5 75 53.5 78M46.5 80 53.5 83", 1.4), G::XS(rrect(45.5, 37, 9, 14, 1.5), 1.4),
           G::F(poly({50, 39.5, 52.8, 44, 50, 48.5, 47.2, 44})), G::XS("M50 3V10", 1.2)});
  }
  // Лабиринт: концентрические стены с проходами и перемычками, в середине — бычьи рога.
  {
    string walls;
    const double gaps[] = {90, 270, 90, 270, 90, 270};
    for (int k = 0; k < 6; k++) {
      const double r = 10 + 6.6 * k, g = gaps[k], half = std::max(8.0, 290.0 / r);
      walls += arc(50, 50, r, g + half, g + 360 - half);
      if (k < 5) {
        const double a = g + (k % 2 ? -1 : 1) * (half + 24);
        walls += seg(polar(50, 50, r, a), polar(50, 50, r + 6.6, a));
      }
    }
    const string horns = sym(tongue(ax({47, 52}, {41.5, 43}, -0.25, -0.1), 4, 0.9));
    c.add("race-minotaurs-labyrinth", "Лабиринт", {G::S(walls, 3.4), G::F(horns + lens(50, 52.5, 3.6, 2.4))});
  }
  // Рога над щитом: геральдический щит с каймой и кольцом, над ним — бычьи рога с насечками.
  {
    const Ax horn{{28, 38}, {11, 34}, {4, 17}, {16, 3}};
    const string shield = "M22 35H78V55C78 73 66 86 50 95C34 86 22 73 22 55Z";
    c.add("race-minotaurs-horns", "Рога над щитом",
          {G::F(sym(taperW(horn.p0, horn.c1, horn.c2, horn.p1, [](double u) { return 13 * std::pow(1 - u, 0.85) + 0.5; }))),
           G::XS(sym(axAcross(horn, 0.38, 5.5, 5.5) + axAcross(horn, 0.5, 5, 5) + axAcross(horn, 0.62, 4.5, 4.5)), 1.4), G::XS(shield, 3), G::F(shield),
           G::XS("M27.5 40.5H72.5V55C72.5 70 62.5 81 50 88.5C37.5 81 27.5 70 27.5 55Z", 1.6), G::XS(circ(50, 60, 10), 4.4)});
  }
}

// ---------------------------------------------------------------- морозные эльфы
// Снежинка: шесть заострённых лучей с веточками (заливка).
string snowflake(double cx, double cy, double r) {
  string s;
  for (int k = 0; k < 6; k++) {
    const double a = -90 + 60 * k;
    s += leaf(ax({float(cx), float(cy)}, polar(cx, cy, r, a)), r * 0.24);
    const Pt q = polar(cx, cy, r * 0.55, a);
    s += tongue(ax(q, polar(q.x, q.y, r * 0.34, a - 50)), r * 0.13, 1) + tongue(ax(q, polar(q.x, q.y, r * 0.34, a + 50)), r * 0.13, 1);
  }
  return s;
}

void frostElves(Catalog& c) {
  c.group("Морозные Эльфы", EmblemSection::Races);
  // Снежная корона: обруч с самоцветами, зубцы-кристаллы с гранями, над средним — снежинка.
  {
    std::vector<L> v;
    struct Cr {
      Pt b, t;
      double w;
    };
    const Cr cs[] = {{{18, 73}, {7, 55}, 7},     {{82, 73}, {93, 55}, 7},     {{27, 73}, {19, 42}, 9},
                     {{73, 73}, {81, 42}, 9},    {{38, 74}, {34, 31}, 10.5},  {{62, 74}, {66, 31}, 10.5}, {{50, 75}, {50, 29}, 13}};
    for (const Cr& k : cs) {
      string f;
      front(v, crystal(k.b, k.t, k.w, &f, 0.8), 2.2f);
      v.push_back(G::XS(f, 1.3));
    }
    const string hoop = "M12 69C30 78 70 78 88 69L86 79C68 88 32 88 14 79Z";
    front(v, hoop, 2.4f);
    v.push_back(G::XS("M13 74C30 82.5 70 82.5 87 74", 1.3));
    v.push_back(G::X(poly({50, 77.5, 53, 81, 50, 84.5, 47, 81}) + sym(lens(34, 80, 2.2, 1.4, 12) + lens(22, 76.5, 1.8, 1.2, 22))));
    v.push_back(G::F(snowflake(50, 14, 13)));
    c.add("race-frost-elves-crown", "Снежная корона", moved(v, Affine::translate(0, 4)));
  }
  // Ледяной волк: голова анфас, острые уши, гребни инея на скулах, узкие глаза, снежная звезда на лбу.
  {
    const string head = symSpline({{50, 22}, {42, 23}, {33, 28}, {28, 37}, {16, 44, 0}, {26, 49}, {13, 57, 0}, {26, 61}, {18, 70, 0}, {32, 71},
                                   {39, 82}, {45, 91}, {50, 95}});
    const string ears = sym(tongue(ax({34, 32}, {24, 3}, 0.1, -0.05), 13, 1));
    c.add("race-frost-elves-wolf", "Ледяной волк",
          {G::F(head + ears), G::XS(sym("M33 29 26.5 9"), 1.4), G::X(sym(tongue(ax({45.5, 50}, {32, 44}, 0.05), 3.6, 0.6))),
           G::XS(sym("M34 43 45 46.5"), 1.4), G::X(star(50, 34, 8, 1.8, 4, -90)), G::XS(sym("M46 56 48 82"), 1.3), G::X(lens(50, 88, 3.6, 1.8)),
           G::XS(sym("M24 52 32 55M21 63 30 65M28 71 34 72"), 1.4)});
  }
  // Клинок-сосулька: ледяной клинок остриём вниз с зубчатыми наплывами, гарда из кристаллов, рукоять с навершием.
  {
    string facets;
    const string guard = crystal({45, 31}, {10, 22}, 11, &facets, 0.75) + crystal({45, 34}, {22, 44}, 8, &facets, 0.75);
    const string blade = "M36 36H64L61 48 65 52 59 64 62.5 68 56 80 57.5 84 50 98 42.5 84 44 80 37.5 68 41 64 35 52 39 48Z";
    const string hilt = rrect(45, 11, 10, 20, 2.5) + poly({50, 1, 57, 7, 50, 13, 43, 7});
    c.add("race-frost-elves-blade", "Клинок-сосулька",
          {G::F(sym(guard)), G::XS(sym(facets), 1.3),
           G::XS(blade + hilt + poly({50, 25, 59, 33, 50, 41, 41, 33}), 2.6), G::F(blade + hilt + poly({50, 25, 59, 33, 50, 41, 41, 33})),
           G::XS("M50 43V90", 1.6), G::XS("M45 16 55 14M45 21 55 19M45 26 55 24", 1.3), G::X(poly({50, 29, 54, 33, 50, 37, 46, 33})),
           G::XS("M43.5 7H56.5", 1.2), G::XS(sym("M40 48 46 50M42 64 47 66M45 80 48.5 81"), 1.4)});
  }
  // Ледяная лиса: свернулась клубком, пушистый хвост укрывает лапы, над ней снежинка.
  {
    std::vector<L> v;
    const string body = spline({{30, 44}, {44, 36}, {62, 35}, {78, 40}, {88, 52}, {87, 66}, {76, 76}, {58, 80}, {40, 78}, {28, 72}, {24, 60}});
    v.push_back(G::F(body));
    v.push_back(G::XS("M60 39C72 41 80 47 84 56", 1.4));
    const Ax tl{{86, 64}, {88, 90}, {40, 96}, {12, 78}};
    const string tail = taperW(tl.p0, tl.c1, tl.c2, tl.p1, [](double u) { return 4 + 17 * std::pow(std::sin(kPi * std::pow(u, 0.75)), 0.7); });
    front(v, tail, 2.6f);
    v.push_back(G::XS(axAcross(tl, 0.84, 12, 12), 1.8));
    v.push_back(G::XS(axLine(tl, 0.15, 0.75, 14), 1.3));
    const string head = spline({{22, 44}, {28, 38}, {37, 40}, {43, 48}, {42, 58}, {34, 63}, {22, 64}, {13, 64.5, 0}, {16, 59}, {19, 53}}) +
                        tongue(ax({26, 41}, {21, 24}, 0.08), 9, 1) + tongue(ax({35, 41}, {38, 25}, -0.06), 8.5, 1);
    front(v, head, 2.6f);
    v.push_back(G::X(lens(25, 50, 3.4, 1.1, 18) + "M23.5 30 24 39 27.5 38Z"));
    v.push_back(G::XS("M14 62C18 60 22 60 26 61M34 46C37 50 38 54 37 58", 1.3));
    v.push_back(G::F(snowflake(81, 17, 13)));
    c.add("race-frost-elves-fox", "Ледяная лиса", v);
  }
}

// ---------------------------------------------------------------- морские эльфы
// Ряд волн: гребни-языки, загнутые вперёд, от x0 до x1 на высоте y.
string waveRow(double x0, double x1, double y, int count, double h, double w) {
  const double step = (x1 - x0) / count;
  string s;
  for (int k = 0; k < count; k++)
    s += tongue(ax({float(x0 + step * k), float(y)}, {float(x0 + step * (k + 0.85)), float(y - h)}, 0.3, -0.25), w, 1.1);
  return s;
}
// Дуга-звук с острыми концами: радиус r, от a0 до a1, наибольшая ширина w в середине.
string soundArc(double cx, double cy, double r, double a0, double a1, double w) {
  const double m = (a0 + a1) / 2;
  return taperArc(cx, cy, r, m, a0, w, 0.4) + taperArc(cx, cy, r, m, a1, w, 0.4);
}

void seaElves(Catalog& c) {
  c.group("Морские Эльфы", EmblemSection::Races);
  // Корабль-лебедь: нос — лебединая шея с головой, корма завитком, парус-крыло с перьями, волны.
  {
    std::vector<L> v;
    v.push_back(G::F(waveRow(4, 96, 95, 5, 6, 4.2)));
    v.push_back(G::F(rrect(49.5, 8, 3, 56, 1.2) + "M51 3 61 5.5 51 8Z"));
    const string sail = spline({{53, 10}, {66, 15}, {77, 26}, {83, 40}, {84, 53, 0}, {78, 50}, {74, 54}, {68, 51}, {62, 55}, {56, 52}, {53, 54, 0}});
    const string jib = spline({{48, 14}, {40, 22}, {34, 33}, {31, 46, 0}, {36, 44}, {40, 48}, {44, 45}, {48, 49, 0}});
    front(v, sail + jib, 2.f);
    v.push_back(G::XS("M56 20C64 25 70 33 74 43M56 33C61 37 65 42 68 48M45 25C41 31 38 37 36 43", 1.4));
    const string hull = "M10 58C18 75 80 78 92 56L86 56C70 61.5 32 62.5 14 57Z";
    const string neck = tube({{14, 59}, {9, 48}, {11, 36}, {18, 27}, {21, 20}}, [](double u) { return 7.5 - 2.5 * u; }, 2).fill +
                        xf(eggShape(0, 0, 4.6, 6.4), place(21.5, 19, 70)) + tongue(ax({26, 18}, {34, 22}, 0.05), 3.2, 0.8);
    const string stern = tongue(ax({86, 57}, {88, 33}, -0.35, 0.2), 6.5);
    front(v, hull + neck + stern, 2.4f);
    v.push_back(G::X(lens(22, 17.5, 1.8, 0.8, 20)));
    v.push_back(G::XS("M17 61.5C34 66.5 70 67 86 59.5", 1.3));
    v.push_back(G::X(poly({30, 64, 32.5, 66.5, 30, 69, 27.5, 66.5}) + poly({42, 65.5, 44.5, 68, 42, 70.5, 39.5, 68}) +
                     poly({54, 65.5, 56.5, 68, 54, 70.5, 51.5, 68}) + poly({66, 64.5, 68.5, 67, 66, 69.5, 63.5, 67})));
    c.add("race-sea-elves-ship", "Корабль-лебедь", v);
  }
  // Рог-раковина: витой конус от мундштука к раструбу, шипы на завитках, из раструба расходится звук.
  {
    const Pt r0{10, 90}, r1{20, 64}, r2{37, 45}, r3{61, 36};
    auto rw = [](double u) { return 7.5 + 29 * std::pow(u, 1.25); };
    const string cone = taperW(r0, r1, r2, r3, rw);
    string bands, spikes;
    for (double t : {0.32, 0.47, 0.61, 0.74, 0.86}) {
      const CurvePt q = bezier(r0, r1, r2, r3, t), q2 = bezier(r0, r1, r2, r3, t + 0.07);
      const double h = rw(t) / 2 + 0.5;
      bands += "M" + n(q.x - q.nx * h) + " " + n(q.y - q.ny * h) + "Q" + n(q.x) + " " + n(q.y) + " " + n(q2.x + q2.nx * h) + " " + n(q2.y + q2.ny * h);
      const double tx = q.ny, ty = -q.nx;
      spikes += tongue(ax({float(q.x - q.nx * (h - 2) - tx * 1), float(q.y - q.ny * (h - 2) - ty * 1)},
                          {float(q.x - q.nx * (h + 6) + tx * 2.5), float(q.y - q.ny * (h + 6) + ty * 2.5)}, 0.1),
                       4.8, 0.9);
    }
    const Affine mouth = place(62, 35, -21);
    c.add("race-sea-elves-conch", "Рог-раковина",
          {G::F(cone + spikes + xf(ell(0, 0, 7.5, 20.5), mouth) + xf(rrect(-5.5, -4.5, 7, 9, 2), place(10, 90, -69))), G::XS(bands, 1.6),
           G::X(xf(ell(1.5, 0, 4.2, 14.5), mouth)), G::F(xf(soundArc(0, 0, 24, -55, 55, 5.4) + soundArc(0, 0, 32.5, -46, 46, 4.6), mouth))});
  }
  // Дельфин: геральдический дельфин в прыжке дугой над волнами — клюв, спинной и грудной плавники, хвост-лопасти.
  {
    const Pt d0{13, 66}, d1{19, 22}, d2{66, 6}, d3{82, 52};
    auto dw = [](double u) { return 3.5 + 18 * std::pow(std::sin(kPi * std::pow(u, 0.7)), 0.85); };
    const string body = taperW(d0, d1, d2, d3, dw);
    auto fin = [&](double u, double side, double len, double base) {
      const CurvePt q = bezier(d0, d1, d2, d3, u);
      const double h = dw(u) / 2 - 1, bx = q.x + q.nx * h * side, by = q.y + q.ny * h * side, tx = q.ny, ty = -q.nx;
      return tongue(ax({float(bx - tx * base * 0.4), float(by - ty * base * 0.4)},
                       {float(bx + q.nx * side * len + tx * base * 1.6), float(by + q.ny * side * len + ty * base * 1.6)}, -0.15 * side),
                    base * 1.6, 0.9);
    };
    const string fluke = xf("M0 0C5 -5 11 -11 18 -12C15 -6 11 -2 7 0C11 2 15 6 18 12C11 11 5 5 0 0Z", place(81, 52, 64, 0.9f));
    c.add("race-sea-elves-dolphin", "Дельфин",
          {G::F(waveRow(4, 96, 95, 5, 6, 4.2)), G::F(body + fin(0.5, -1, 11, 5) + fin(0.27, 1, 9, 3.6) + fluke), G::X(lens(21.5, 45.5, 2.2, 1, -40)),
           G::XS("M14 62C17 58 19 55 22 53M24 52C27 49 29 47 31 44", 1.3), G::XS(across(d0, d1, d2, d3, 0.2, 9, 9), 1.3),
           G::XS(axLine({d0, d1, d2, d3}, 0.3, 0.85, 16), 1.3), G::F(lens(8, 80, 3.6, 2.2, 90) + lens(14, 86, 2.6, 1.6, 90))});
  }
  // Волна и жемчужина: завиток большой волны с пенными когтями, в его полости — жемчужина с бликом.
  {
    const string wave = "M4 96L4 86C12 80 26 74 38 71C52 68 64 62 68 50C71 40 66 30 57 27C47 24 37 28 33 34C31 37 27 37 26 33"
                        "C26 24 36 13 50 9C66 5 84 9 92 22C97 30 97 40 96 50L96 96Z";
    string claws;
    for (double t : {0.12, 0.32, 0.52, 0.72}) {
      const CurvePt q = bezier({26, 33}, {26, 24}, {36, 13}, {50, 9}, t);
      claws += tongue(ax({float(q.x + q.nx * 1), float(q.y + q.ny * 1)}, {float(q.x - q.nx * 9 - q.ny * 4), float(q.y - q.ny * 9 + q.nx * 4)}, 0.25), 6, 0.9);
    }
    c.add("race-sea-elves-pearl", "Волна и жемчужина",
          {G::F(wave + claws), G::XS("M44 86C60 80 74 70 80 54C84 44 84 34 80 26M62 92C76 86 86 74 89 58M20 90C34 84 46 82 56 80", 1.6),
           G::XS("M40 18C50 13 62 12 74 15M55 21C63 22 71 26 76 33", 1.4), G::F(circ(46, 49, 10)), G::X(lens(42.5, 45, 2.6, 1.2, -45)),
           G::XS(arc(46, 49, 6.5, 20, 100), 1.3)});
  }
}

// ---------------------------------------------------------------- наги
void nagasRace(Catalog& c) {
  c.group("Наги", EmblemSection::Races);
  // Королева наг: корона, раскрытый капюшон кобры с узором, руки воздеты, змеиный хвост кольцом с брюшными щитками.
  {
    std::vector<L> v;
    const string hood = symSpline({{50, 3}, {40, 5}, {30, 13}, {24, 26}, {27, 40}, {36, 49}, {50, 52}});
    v.push_back(G::F(hood));
    v.push_back(G::XS(sym("M44 10C38 16 34 24 33 32M40 7C33 12 29 20 28 29"), 1.3));
    v.push_back(G::X(sym(lens(33, 39, 3.6, 1.6, 60))));
    const Tube tail = tube({{50, 60}, {44, 71}, {32, 77}, {25, 84}, {32, 90}, {56, 91}, {75, 86}, {82, 77}, {77, 69}, {70, 72}},
                           [](double u) { return 14 * std::pow(1 - u, 0.75) + 1.2; }, 6);
    // Брюшная полоса: гравированная линия вдоль хвоста ближе к внутренней стороне кольца.
    string belly;
    for (size_t i = 0; i < tail.at.size(); i++) {
      const Tube::S& q = tail.at[i];
      if (q.u < 0.1 || q.u > 0.86) continue;
      const double ox = q.x - q.nx * q.w * 0.2, oy = q.y - q.ny * q.w * 0.2;
      belly += (belly.empty() ? "M" : "L") + n(ox) + " " + n(oy);
    }
    front(v, tail.fill, 2.6f);
    v.push_back(G::XS(belly, 1.4));
    const string torso = symSpline({{50, 31}, {45, 33}, {37, 38}, {37.5, 44}, {42, 52}, {43.5, 58}, {42, 64}, {50, 67}});
    const string head = ell(50, 23, 6.5, 8.5) + "M43 16.5 42 7.5 46 11.5 50 4 54 11.5 58 7.5 57 16.5Z";
    auto arm = [](float s) {
      return tube({{50 + s * 12, 39}, {50 + s * 22, 47}, {50 + s * 29, 40}, {50 + s * 31, 30}}, [](double u) { return 6 - 2.6 * u; }, 2).fill +
             tongue(ax({50 + s * 31, 31}, {50 + s * 34.5f, 21}, 0.12 * s), 3.4, 0.8);
    };
    front(v, torso + head + arm(-1.f) + arm(1.f), 2.4f);
    v.push_back(G::X(sym(lens(47, 23, 1.8, 0.7, 15)) + "M45.5 14.6H54.5L54.3 15.8H45.7Z"));
    v.push_back(G::XS("M42 37C46 41 54 41 58 37M43.5 58.5C47 60 53 60 56.5 58.5", 1.4));
    v.push_back(G::X(poly({50, 40.5, 52, 43, 50, 45.5, 48, 43})));
    c.add("race-nagas-queen", "Королева наг", v);
  }
  // Змей и меч: змей обвивает клинок спиралью, треугольная голова над остриём с раздвоенным языком.
  {
    const string sword = "M50 10 58 20V68H42V20Z" + lens(50, 69, 23, 4) + rrect(46, 71, 8, 16, 2) + "M50 84.5 56 91 50 97.5 44 91Z";
    std::vector<string> front_, back_;
    std::vector<Pt> pts;
    std::vector<double> ws;
    bool curFront = true;
    const int N = 160;
    for (int i = 0; i <= N; i++) {
      const double t = double(i) / N, ph = 2 * kPi * 2 * t;
      const Pt p{float(50 + 18 * std::sin(ph)), float(64 - 40 * t)};
      const bool fr = std::cos(ph) > 0;
      const double w = 5.6 + 5.6 * t;
      if (i > 0 && fr != curFront) {
        pts.push_back(p);
        ws.push_back(w);
        (curFront ? front_ : back_).push_back(ribbon(pts, ws));
        pts = {p};
        ws = {w};
      } else {
        pts.push_back(p);
        ws.push_back(w);
      }
      curFront = fr;
    }
    (curFront ? front_ : back_).push_back(ribbon(pts, ws));
    string fr, bk;
    for (const string& s : front_) fr += s;
    for (const string& s : back_) bk += s;
    const string neck = taper({50, 24.5}, {47, 19}, {47, 15}, {51, 11}, 9.5, 8);
    const string head = spline({{47, 13}, {50, 6}, {57, 2.5}, {64, 4.5}, {67, 8, 0}, {62, 11.5}, {55, 15}, {50, 16}});
    c.add("race-nagas-sword", "Змей и меч",
          {G::F(bk), G::XS(sword, 3), G::F(sword), G::XS("M50 23V64M46.5 76H53.5M46.5 81H53.5", 1.4), G::XS(fr + neck + head, 2.6),
           G::F(fr + neck + head + tongue(ax({66, 8}, {72, 6}), 1.6, 0.5) + tongue(ax({71.5, 6.2}, {75.5, 3}), 1.4, 1) +
                tongue(ax({71.5, 6.2}, {76, 8.5}), 1.4, 1)),
           G::X(lens(58.5, 6.5, 2, 0.9, 20)), G::X("M50 88.5 52.6 91 50 94 47.4 91Z")});
  }
  // Змеиный глаз: миндалевидный глаз с вертикальным зрачком в ромбе из чешуи.
  {
    string grid;
    for (int j = 1; j < 7; j++) {
      const double f = j / 7.0;
      grid += seg(lerp({50, 3}, {3, 50}, f), lerp({97, 50}, {50, 97}, f)) + seg(lerp({50, 3}, {97, 50}, f), lerp({3, 50}, {50, 97}, f));
    }
    string iris;
    for (int k = 0; k < 16; k++) iris += seg(polar(50, 50, 8, k * 22.5 + 11), polar(50, 50, 15, k * 22.5 + 11));
    c.add("race-nagas-eye", "Змеиный глаз",
          {G::F(poly({50, 3, 97, 50, 50, 97, 3, 50})), G::XS(grid, 1.4), G::X(lens(50, 50, 33, 14)), G::F(circ(50, 50, 16)),
           G::XS(lens(50, 50, 33, 14), 3), G::XS(iris, 1.2), G::X("M50 36C54 43 54 57 50 64C46 57 46 43 50 36Z"), G::X(lens(57, 43, 2.6, 1.3, -45))});
  }
  // Морской храм: колонны и ярусы под луковичным куполом с трезубцем, ступени уходят в волны.
  c.add("race-nagas-temple", "Морской храм",
        {G::F(waveRow(4, 96, 96, 5, 5.5, 4)),
         G::F(rrect(8, 79, 84, 7, 1.5) + rrect(14, 72, 72, 8, 1.5) + rrect(16, 44, 68, 29, 1) + rrect(12, 37, 76, 8, 1.5) + rrect(20, 30, 60, 8, 1.5) +
              "M31 31C31 20 42 14 50 9C58 14 69 20 69 31Z" + rrect(49, 1.5, 2, 9, 0.8) + sym(tongue(ax({49, 5}, {45, 1.5}, 0.1), 2.2))),
         G::X(rrect(25, 47, 4, 23, 1) + rrect(35, 47, 4, 23, 1) + rrect(61, 47, 4, 23, 1) + rrect(71, 47, 4, 23, 1) +
              "M43 72V58C43 54 46 51 50 51C54 51 57 54 57 58V72Z"),
         G::XS("M12 45H88M20 38H80M14 80H86M8 86H92", 1.4), G::XS(sym("M41 30C41 22 45 16 50 12"), 1.4),
         G::F(sym(tongue(ax({12, 37}, {12, 25}), 4.4)))});
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
