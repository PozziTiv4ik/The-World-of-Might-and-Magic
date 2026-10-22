// Regnum — классические геральдические эмблемы: силуэты описаны строками путей SVG на сетке 100 × 100,
// повторяющиеся части (зёрна колоса, мечи, перья, лепестки) строятся преобразованиями. Реестр, подписи и группы —
// emblems_registry.cpp; фантазийные группы — emblems_fantasy_a/b.cpp, расы — emblems_races_a/b/c.cpp.
#include <algorithm>
#include <array>

#include "gfx/emblems_internal.h"

namespace rg::gfx::emb {

namespace {

// ---------------------------------------------------------------- помощники построения

// Отрезок для обводок и вырезов линией.
std::string seg(double x0, double y0, double x1, double y1) { return "M" + n(x0) + " " + n(y0) + "L" + n(x1) + " " + n(y1); }

// Раздвоенное копыто в (x, y) — середина подошвы, нога сверху под углом deg (0 — прямо вниз).
std::string hoof(double x, double y, double deg, double s = 1) {
  return xf("M-3.6 -5.5H3.6L4.6 0.5H-4.6Z", place(float(x), float(y), float(deg), float(s)));
}

// Единый обход всех подпутей (как у circ и ell): части одного слоя, наложенные друг на друга, не гасят друг
// друга по правилу ненулевой обмотки (sym и rrect дают обратный обход).
std::string uni(const std::string& d) {
  const Path p = svgPath(d);
  Path out;
  size_t vi = 0, pi = 0;
  struct Seg {
    Path::Verb v;
    Pt c[3];
    int cnt;
  };
  while (vi < p.verbs.size()) {
    if (p.verbs[vi] != Path::Move) {
      vi++;
      continue;
    }
    const Pt start = p.pts[pi++];
    vi++;
    std::vector<Seg> segs;
    bool closed = false;
    while (vi < p.verbs.size() && p.verbs[vi] != Path::Move) {
      const Path::Verb vb = p.verbs[vi++];
      if (vb == Path::Close) {
        closed = true;
        continue;
      }
      Seg s{vb, {}, vb == Path::Line ? 1 : vb == Path::Quad ? 2 : 3};
      for (int k = 0; k < s.cnt; k++) s.c[k] = p.pts[pi++];
      segs.push_back(s);
    }
    std::vector<Pt> poly{start};
    for (const Seg& s : segs)
      for (int k = 0; k < s.cnt; k++) poly.push_back(s.c[k]);
    double area = 0;
    for (size_t i = 0; i < poly.size(); i++) {
      const Pt a = poly[i], b = poly[(i + 1) % poly.size()];
      area += double(a.x) * b.y - double(b.x) * a.y;
    }
    if (area <= 0 || segs.empty()) {
      out.moveTo(start.x, start.y);
      for (const Seg& s : segs) {
        if (s.v == Path::Line) out.lineTo(s.c[0].x, s.c[0].y);
        else if (s.v == Path::Quad) out.quadTo(s.c[0].x, s.c[0].y, s.c[1].x, s.c[1].y);
        else out.cubicTo(s.c[0].x, s.c[0].y, s.c[1].x, s.c[1].y, s.c[2].x, s.c[2].y);
      }
    } else {
      const Pt last = segs.back().c[segs.back().cnt - 1];
      out.moveTo(last.x, last.y);
      for (size_t i = segs.size(); i-- > 0;) {
        const Pt to = i == 0 ? start : segs[i - 1].c[segs[i - 1].cnt - 1];
        const Seg& s = segs[i];
        if (s.v == Path::Line) out.lineTo(to.x, to.y);
        else if (s.v == Path::Quad) out.quadTo(s.c[0].x, s.c[0].y, to.x, to.y);
        else out.cubicTo(s.c[1].x, s.c[1].y, s.c[0].x, s.c[0].y, to.x, to.y);
      }
    }
    if (closed) out.close();
  }
  return toSvgPath(out, 3);
}

L FU(const std::string& d) { return G::F(uni(d)); }

L XU(const std::string& d) { return G::X(uni(d)); }

// Полоса вдоль кубической кривой с произвольным профилем ширины w(t), t ∈ [0, 1] (0 на конце — острие).
template <class W>
std::string sweep(Pt p0, Pt c1, Pt c2, Pt p1, W w, int steps = 40) {
  std::vector<Pt> lft, rgt;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, h = std::max(0.0, w(t)) * 0.5;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    lft.push_back({float(q.x + q.nx * h), float(q.y + q.ny * h)});
    rgt.push_back({float(q.x - q.nx * h), float(q.y - q.ny * h)});
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Полоса вдоль ломаной pts с профилем ширины w(t) по доле пройденной длины (вырезы-прожилки, срединные линии).
template <class W>
std::string ribbon(const std::vector<Pt>& pts, W w) {
  if (pts.size() < 2) return {};
  std::vector<double> acc{0};
  for (size_t i = 1; i < pts.size(); i++) acc.push_back(acc.back() + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y));
  std::vector<Pt> lft, rgt;
  for (size_t i = 0; i < pts.size(); i++) {
    const Pt a = pts[i == 0 ? 0 : i - 1], b = pts[i + 1 < pts.size() ? i + 1 : i];
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1e-6, std::hypot(dx, dy)), h = std::max(0.0, w(acc[i] / std::max(1e-6, acc.back()))) * 0.5;
    lft.push_back({float(pts[i].x - dy / len * h), float(pts[i].y + dx / len * h)});
    rgt.push_back({float(pts[i].x + dy / len * h), float(pts[i].y - dx / len * h)});
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Конечность одной гладкой полосой через суставы knots (сплайн Катмулла — Рома) с шириной w в каждом суставе:
// без швов и «шарниров», мышцы — плавным изменением ширины.
std::string limb(const std::vector<Pt>& knots, const std::vector<double>& w) {
  std::vector<Pt> pts;
  std::vector<double> ws;
  const size_t n = knots.size();
  for (size_t i = 0; i + 1 < n; i++) {
    const Pt p0 = knots[i == 0 ? 0 : i - 1], p1 = knots[i], p2 = knots[i + 1], p3 = knots[i + 2 < n ? i + 2 : n - 1];
    const int steps = 14;
    for (int k = 0; k < steps || (i + 2 == n && k == steps); k++) {
      const double t = double(k) / steps, t2 = t * t, t3 = t2 * t;
      const auto cr = [&](double a, double b, double c, double d) {
        return 0.5 * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (3 * b - a - 3 * c + d) * t3);
      };
      pts.push_back({float(cr(p0.x, p1.x, p2.x, p3.x)), float(cr(p0.y, p1.y, p2.y, p3.y))});
      const double s = t * t * (3 - 2 * t);
      ws.push_back(w[i] + (w[i + 1] - w[i]) * s);
    }
  }
  std::vector<Pt> lft, rgt;
  for (size_t i = 0; i < pts.size(); i++) {
    const Pt a = pts[i == 0 ? 0 : i - 1], b = pts[i + 1 < pts.size() ? i + 1 : i];
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1e-6, std::hypot(dx, dy)), h = ws[i] * 0.5;
    lft.push_back({float(pts[i].x - dy / len * h), float(pts[i].y + dx / len * h)});
    rgt.push_back({float(pts[i].x + dy / len * h), float(pts[i].y - dx / len * h)});
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z" + circ(knots[0].x, knots[0].y, w[0] * 0.5);
}

Pt at(Pt p, double deg, double r) { return polar(p.x, p.y, r, deg); }

Pt mix(Pt a, Pt b, double t) { return {float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t)}; }

double angleOf(Pt a, Pt b) { return std::atan2(b.y - a.y, b.x - a.x) * 180 / kPi; }

// Контрольные точки кривой от b к t с боковыми смещениями s1, s2 (доли длины; одного знака — дуга, разных — S-кривая).
std::array<Pt, 4> bend(Pt b, Pt t, double s1, double s2) {
  const double dx = t.x - b.x, dy = t.y - b.y;
  return {b, Pt{float(b.x + dx / 3 - dy * s1), float(b.y + dy / 3 + dx * s1)}, Pt{float(b.x + dx * 2 / 3 - dy * s2), float(b.y + dy * 2 / 3 + dx * s2)}, t};
}

// Язык (прядь гривы, язык пламени, кисть хвоста): широкое основание w у b, острый кончик t; изгиб — как у bend.
std::string tongue(Pt b, Pt t, double w, double s1 = 0, double s2 = 0, double sharp = 1) {
  const auto c = bend(b, t, s1, s2);
  return sweep(c[0], c[1], c[2], c[3], [&](double u) { return w * std::pow(std::cos(u * kPi / 2), sharp); });
}

// Перо-лопасть: узкое основание, наибольшая ширина w в первой трети, острый кончик.
std::string blade(Pt b, Pt t, double w, double s1 = 0, double s2 = 0) {
  const auto c = bend(b, t, s1, s2);
  return sweep(c[0], c[1], c[2], c[3], [&](double u) { return w * std::pow(std::sin(kPi * std::pow(u, 0.62)), 0.7) + (u < 0.2 ? w * 0.35 * (1 - u / 0.2) : 0); });
}

// Прожилка-вырез: полоса вдоль кривой, острая с обоих концов, наибольшая ширина w посередине.
std::string vein(Pt a, Pt c1, Pt c2, Pt b, double w) {
  return sweep(a, c1, c2, b, [&](double t) { return w * std::pow(std::sin(kPi * t), 0.6); }, 24);
}

// Коготь-полумесяц от b в направлении deg длиной len, загиб curl градусов, ширина основания w.
std::string claw(Pt b, double deg, double len, double curl, double w) {
  const Pt c1 = at(b, deg, len * 0.38), c2 = at(c1, deg + curl * 0.5, len * 0.34), e = at(c2, deg + curl, len * 0.34);
  return sweep(b, c1, c2, e, [&](double u) { return w * std::pow(1 - u, 0.9); }, 20);
}

// Гербовая львиная лапа: подушка у запястья w, пальцы веером в направлении deg, когти загнуты в сторону side (±1).
std::string lionPaw(Pt w, double deg, double s, double side = 1, int toes = 4) {
  const Pt c = at(w, deg, 2.2 * s);
  std::string d = xf(ell(0, 0, 4.4, 3.8), place(c.x, c.y, float(deg), float(s)));
  for (int i = 0; i < toes; i++) {
    const double a = deg + (i - (toes - 1) * 0.5) * 27;
    d += claw(at(c, a, 2.8 * s), a - 22 * side, 8 * s, 80 * side, 2.7 * s);
  }
  return d;
}

// Орлиная лапа с открытыми когтями: от лодыжки ankle три пальца веером в направлении deg и задний палец; когти
// загнуты в сторону side (±1), размер s.
std::string talon(Pt ankle, double deg, double s, double side = 1) {
  std::string d = circ(ankle.x, ankle.y, 2.6 * s);
  for (double a : {-42.0, 0.0, 42.0, 180.0}) {
    const double ad = deg + a, len = (a == 180.0 ? 3.6 : 7.5) * s;
    const Pt e = at(ankle, ad, len);
    d += sweep(ankle, at(ankle, ad, len * 0.4), at(ankle, ad, len * 0.7), e, [&](double t) { return (3.4 - 1 * t) * s; }, 12);
    d += claw(at(ankle, ad, len - 0.6 * s), ad - 10 * side, 6.4 * s, 95 * side, 2.5 * s);
  }
  return d;
}

// Копыто с щёточкой у бабки: нога приходит в (x, y) — середину подошвы — под углом deg (0 — сверху вниз).
std::string hoofTuft(double x, double y, double deg, double s = 1, double tuftSide = 1) {
  const Affine m = place(float(x), float(y), float(deg), float(s));
  return xf("M-3.4 -6H3.4L4.6 0.6H-4.6Z", m) + xf(tongue({float(tuftSide * 2.5), -7}, {float(tuftSide * 7.5), -1.5}, 3.2, 0.1, -0.1), m);
}

// Заливки отдельными слоями (каждая — своим путём).
void fills(std::vector<L>& v, std::initializer_list<std::string> ds) {
  for (const std::string& d : ds) v.push_back(FU(d));
}

// Слои «спереди»: зазор вокруг всех путей, затем заливка каждого отдельным слоем.
void layerFront(std::vector<L>& v, std::initializer_list<std::string> ds, float gap = 2.0f) {
  for (const std::string& d : ds) v.push_back(G::XS(d, gap));
  for (const std::string& d : ds) v.push_back(FU(d));
}

// Пряди (грива, шерсть, оперение шеи): языки одной заливкой и прожилки-вырезы по их середине.
struct Strands {
  std::string fill, veins;
  void add(Pt b, Pt t, double w, double s1, double s2, double veinW = 1.4) {
    fill += tongue(b, t, w, s1, s2);
    const auto c = bend(b, t, s1, s2);
    std::vector<Pt> pts;
    for (int k = 0; k <= 16; k++) {
      const CurvePt q = bezier(c[0], c[1], c[2], c[3], 0.3 + 0.55 * k / 16);
      pts.push_back({float(q.x), float(q.y)});
    }
    veins += ribbon(pts, [&](double u) { return veinW * std::pow(std::sin(kPi * u), 0.6); });
  }
};

// Грива языками внахлёст вдоль эллипса: основания по дуге от a0 до a1 градусов, кончики отнесены на len наружу и
// склонены к направлению flowDeg (доля k); нижние и чётные пряди длиннее.
std::vector<std::string> locks(Pt c, double rx, double ry, double rot, double a0, double a1, int cnt, double len, double flowDeg, double k, double w,
                               double s1 = 0.16, double s2 = -0.12) {
  std::vector<std::string> out;
  for (int i = 0; i < cnt; i++) {
    const double a = a0 + (a1 - a0) * i / std::max(1, cnt - 1);
    const double t = a * kPi / 180, r = rot * kPi / 180;
    const Pt base{float(c.x + rx * std::cos(t) * std::cos(r) - ry * std::sin(t) * std::sin(r)),
                  float(c.y + rx * std::cos(t) * std::sin(r) + ry * std::sin(t) * std::cos(r))};
    const double rad = std::atan2(base.y - c.y, base.x - c.x), fl = flowDeg * kPi / 180;
    const double dir = std::atan2(std::sin(rad) * (1 - k) + std::sin(fl) * k, std::cos(rad) * (1 - k) + std::cos(fl) * k) * 180 / kPi;
    const double grow = 1 + 0.4 * std::max(0.0, std::sin(a * kPi / 180));   // нижние пряди длиннее
    out.push_back(tongue(at(base, dir + 180, w * 0.7), at(base, dir, len * grow * (i % 2 ? 0.78 : 1.0)), w * (0.85 + 0.15 * grow), s1, s2));
  }
  return out;
}

// Зубцы (мерлоны) от x0 до x1 по верху y0…y1: cnt штук с бойницами шириной gap.
std::string merlons(double x0, double x1, double y0, double y1, int cnt, double gap) {
  const double w = (x1 - x0 - gap * (cnt - 1)) / cnt;
  std::string d;
  for (int i = 0; i < cnt; i++) d += "M" + n(x0 + i * (w + gap)) + " " + n(y1) + "V" + n(y0) + "H" + n(x0 + i * (w + gap) + w) + "V" + n(y1) + "Z";
  return d;
}

// Стрельчатый проём: низ y1, верх острия y0, ширина от x0 до x1.
std::string lancet(double x0, double x1, double y0, double y1) {
  const double cx = (x0 + x1) / 2, hw = (x1 - x0) / 2, sh = y0 + hw * 1.7;
  return "M" + n(x0) + " " + n(y1) + "V" + n(sh) + "C" + n(x0) + " " + n(sh - hw * 0.9) + " " + n(cx - hw * 0.35) + " " + n(y0 + hw * 0.25) + " " + n(cx) + " " +
         n(y0) + "C" + n(cx + hw * 0.35) + " " + n(y0 + hw * 0.25) + " " + n(x1) + " " + n(sh - hw * 0.9) + " " + n(x1) + " " + n(sh) + "V" + n(y1) + "Z";
}

// Решётка в стрельчатом проёме: прутья и поперечины (заливкой поверх выреза).
std::string grate(double x0, double x1, double y0, double y1, int bars, double barW) {
  std::string d;
  const double cx = (x0 + x1) / 2, hw = (x1 - x0) / 2;
  for (int i = 0; i < bars; i++) {
    const double x = x0 + (x1 - x0) * (i + 1) / (bars + 1), k = std::fabs(x - cx) / hw;
    const double top = y0 + hw * 1.7 * (1 - std::sqrt(std::max(0.0, 1 - k * k))) * 0.9 + 0.6;
    d += rrect(x - barW / 2, top, barW, y1 - top, 0.3);
  }
  for (double t : {0.45, 0.72}) d += rrect(x0, y0 + (y1 - y0) * t - barW / 2, x1 - x0, barW, 0.3);
  return d;
}

// Волна-завиток: сужающаяся S-полоса от x0 до x1 на высоте y (геральдическое «волнистое» море).
std::string waveStrip(double x0, double x1, double y, double amp, int humps, double w) {
  std::vector<Pt> pts;
  for (int i = 0; i <= humps * 12; i++) {
    const double t = double(i) / (humps * 12);
    pts.push_back({float(x0 + (x1 - x0) * t), float(y + amp * std::sin(t * humps * 2 * kPi))});
  }
  return ribbon(pts, [&](double t) { return w * std::pow(std::sin(kPi * t), 0.35); });
}

// Резец руны: брусок от a до b шириной w с гранёными (заострёнными) концами.
std::string stave(Pt a, Pt b, double w) {
  const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1e-6, std::hypot(dx, dy)), ux = dx / len, uy = dy / len, nx = -uy * w / 2, ny = ux * w / 2;
  const double k = std::min(w * 0.6, len * 0.3);
  const auto P = [&](double along, double side) { return ptS({float(a.x + ux * along + nx * side), float(a.y + uy * along + ny * side)}); };
  return "M" + P(0, 0) + "L" + P(k, 1) + "L" + P(len - k, 1) + "L" + P(len, 0) + "L" + P(len - k, -1) + "L" + P(k, -1) + "Z";
}

// Геральдическая лилия на сетке 100 (перевязь на y ≈ 61): средний лепесток-ланцет, боковые серпами с загнутыми
// остриями, перевязь, нижние лепестки; m — преобразование в место назначения.
struct Fleur {
  std::string center, sides, bind, foot;
};

Fleur fleur(const Affine& m = {}) {
  Fleur f;
  f.center = xf("M50 2C57.5 10 62 22.5 61 35 60 45 56 51 55 57H45C44 51 40 45 39 35 38 22.5 42.5 10 50 2Z", m);
  const std::string side = sweep({45.5, 58}, {37, 28}, {9, 14}, {11, 47}, [](double t) { return (4.5 + 11.5 * std::sin(kPi * t * 0.82)) * (1 - t * t * t); });
  const std::string low = sweep({45, 66}, {41, 77}, {33, 82}, {24, 79}, [](double t) { return 7.5 * (1 - t * t); });
  f.sides = xf(sym(side), m);
  f.foot = xf(sym(low) + "M45 65H55C54.5 74 53 82 50 92 47 82 45.5 74 45 65Z", m);
  f.bind = xf(rrect(29, 56.5, 42, 9.5, 4.75), m);
  return f;
}

// Меч остриём вверх: клинок с долом, гарда с загнутыми и заострёнными концами, обмотанная рукоять, гранёное
// навершие. fill — силуэт, holes — вырезы заливкой, cuts — вырезы линией.
struct SwordParts {
  std::string fill, holes, cuts;
};

SwordParts swordParts(double hw = 10, double curl = 1, float span = 1) {
  SwordParts s;
  const float k = float(curl);
  const std::string quillon =
      sweep({45, 66}, {37, 66.2}, {29, 65.5f - k * 0.5f}, {21.5f + (1 - k) * 2, 60 + (1 - k) * 4.5f}, [](double t) { return 7 - 3.2 * t; }) +
      tongue({22 + (1 - k) * 2, 60.5f + (1 - k) * 4.5f}, {17.5f + (1 - k) * 0.5f, 55 + (1 - k) * 9.5f}, 4, -0.2 * k, 0.15 * k);
  s.fill = "M50 1.5C" + n(50 + hw * 0.4) + " 8 " + n(50 + hw * 0.8) + " 14 " + n(50 + hw) + " 21V63H" + n(50 - hw) + "V21C" + n(50 - hw * 0.8) + " 14 " +
           n(50 - hw * 0.4) + " 8 50 1.5Z" + sym(xf(quillon, Affine::translate(50, 0) * Affine::scale(span, 1) * Affine::translate(-50, 0))) +
           "M41 62H59L61.4 66.2 59 70.4H41L38.6 66.2Z" + "M43 69H57C56.4 75 56.4 80 57.2 85.5H42.8C43.6 80 43.6 75 43 69Z" + "M50 82.6 59 90.6 50 99.2 41 90.6Z";
  s.holes = vein({50, 14}, {50, 28}, {50, 44}, {50, 58}, 2.4) + "M50 63.6 52.6 66.2 50 68.8 47.4 66.2Z" + "M50 87.4 53.2 90.6 50 94.4 46.8 90.6Z";
  s.cuts = "M44 75 56 71.6M44 81 56 77.6";
  return s;
}

// ---------------------------------------------------------------- эмблемы (в порядке каталога)

// Корона: лилии-зубцы (средняя анфас, крайние — повёрнутые), обод арками с жемчужинами, обруч в перспективе
// с каймами и камнями-ромбами.
std::vector<L> crownGlyph() {
  std::vector<L> v;
  // лилии-зубцы: средняя анфас, крайние сжаты по ширине
  const auto fl = [](float x, float base, float sx, float sy) {
    return fleur(Affine::translate(x, base) * Affine::scale(sx, sy) * Affine::translate(-50, -66));
  };
  const Fleur c = fl(50, 47, 0.46f, 0.62f), l = fl(15, 51, 0.24f, 0.5f), r = fl(85, 51, 0.24f, 0.5f);
  for (const Fleur* f : {&l, &r, &c}) {
    v.push_back(G::XS(f->sides + f->center, 1.6f));
    v.push_back(FU(f->sides));
    v.push_back(FU(f->center));
    v.push_back(FU(f->bind));
  }
  // обод с зубцами-арками и обруч в перспективе
  const std::string rim = sym("M50 46 C45 56 38 57 32 49 C27 57 20 57 15 50 L12 62 Q31 66.5 50 67Z");
  const std::string band = "M12 62Q50 70 88 62L86.5 81Q50 89.5 13.5 81Z";
  v.push_back(FU(rim));
  v.push_back(FU(sym(circ(32, 46.4, 2.6))));
  v.push_back(FU(band));
  // гравировка: каймы обруча, камни-ромбы и жемчуг, внутренний контур арок, прожилка средней лилии
  v.push_back(G::XS("M13.4 66.6Q50 74.4 86.6 66.6M14.2 76.6Q50 85 85.8 76.6", 1.4f));
  const std::string gem = "M-4.6 0 0 -3.4 4.6 0 0 3.4Z";
  v.push_back(XU("M50 69.8 55.6 75.4 50 81 44.4 75.4Z" + xf(gem, place(31.5, 74.4, 5)) + xf(gem, place(68.5, 74.4, -5)) + circ(20, 72.2, 1.6) +
                 circ(80, 72.2, 1.6) + circ(41, 76, 1.3) + circ(59, 76, 1.3)));
  v.push_back(FU("M50 72.4 53 75.4 50 78.4 47 75.4Z" + xf(gem, place(31.5, 74.4, 5, 0.42f)) + xf(gem, place(68.5, 74.4, -5, 0.42f))));
  v.push_back(G::XS(sym("M50 51.5C46.5 59 40 61 33.5 55.5M30.5 55.5C25.5 61 20 60.5 16.5 56.5"), 1.3f));
  v.push_back(XU(vein({50, 11}, {50, 18}, {50, 27}, {50, 37}, 1.8)));
  return v;
}

// Башня: зубцы над навесной галереей на консолях, стрельчатые окна и бойницы-кресты, ворота с решёткой, цоколь.
std::vector<L> towerGlyph() {
  std::vector<L> v;
  std::string corbels;
  for (int i = 0; i < 8; i++) {
    const double x = 26.5 + i * 6.7;
    corbels += "M" + n(x - 2.4) + " 35.5H" + n(x + 2.4) + "L" + n(x + 1.2) + " 40H" + n(x - 1.2) + "Z";
  }
  const std::string body = "M31 38H69L72.5 88H27.5Z" + std::string("M23.5 25H76.5V35.5H23.5Z") + merlons(23.5, 76.5, 11, 25.5, 5, 4.6) + corbels +
                           "M25 87.5H75L78 96H22Z";
  v.push_back(FU(body));
  v.push_back(XU(lancet(42.5, 57.5, 63, 88) + lancet(46.5, 53.5, 43, 56) + sym(rrect(36.6, 46, 1.6, 12, 0.6) + rrect(34.2, 51.2, 6.4, 1.6, 0.6)) +
                   sym(rrect(26.2, 15.5, 1.6, 6.5, 0.6)) + rrect(49.2, 15.5, 1.6, 6.5, 0.6)));
  v.push_back(FU(grate(42.5, 57.5, 63, 88, 3, 1.6)));
  v.push_back(G::XS("M23.5 29.4H76.5M31.6 44H40M60 44H68.4M29.6 61H40M60 61H70.4M28.4 77H40M60 77H71.6M24 91.5H76", 1.3f));
  return v;
}

// Замок: донжон с вымпелом, две фланговые башни, куртина с зубцами, ворота с решёткой, цоколь.
std::vector<L> castleGlyph() {
  std::vector<L> v;
  const std::string keep = "M38 24H62V62H38Z" + merlons(36, 64, 13, 24.5, 3, 4.4) + "M36 22.5H64V26H36Z";
  const std::string towers = sym("M12 36H30V90H12Z" + merlons(10, 32, 25, 36.5, 3, 3.6) + std::string("M10 34.5H32V38H10Z"));
  const std::string wall = "M30 53H70V90H30Z" + merlons(30, 70, 46, 53.5, 5, 3.6);
  const std::string flag = rrect(49.2, 2, 1.6, 12, 0.5) + "M50.8 2.5H65L60.5 6 65 9.5H50.8Z";
  v.push_back(FU(keep + towers + wall + flag + "M8 88.5H92L94 96H6Z"));
  v.push_back(
      XU(lancet(42, 58, 66, 89) + lancet(46.5, 53.5, 31, 44) + sym(lancet(18.5, 23.5, 45, 56) + rrect(20.2, 66, 1.6, 10, 0.6) + rrect(18, 70.2, 6, 1.6, 0.6))));
  v.push_back(FU(grate(42, 58, 66, 89, 3, 1.6)));
  v.push_back(G::XS(sym("M12 41.5H30M12 62H30M12 82H30M38 30H50M38 50H50M30 59H38M30 75H42") + "M8 92H92", 1.3f));
  return v;
}

// Звезда пятиконечная, гранёная: рёбра от центра к лучам и впадинам — сужающимися вырезами.
std::vector<L> starGlyph() {
  const double cx = 50, cy = 53.5, R = 48, r = 18.5;
  std::string ridges;
  for (int i = 0; i < 5; i++) {
    const double a = -90 + 72 * i;
    const Pt c{float(cx), float(cy)}, tip = polar(cx, cy, R - 7, a), val = polar(cx, cy, r - 2, a + 36);
    ridges += vein(c, mix(c, tip, 0.33), mix(c, tip, 0.66), tip, 2.2) + vein(c, mix(c, val, 0.33), mix(c, val, 0.66), val, 1.6);
  }
  return {FU(star(cx, cy, R, r, 5, -90)), XU(ridges), XU(star(cx, cy, 6.5, 2.8, 5, -90 + 36)), FU(circ(cx, cy, 2.6))};
}

// Солнце в сиянии: строгий лик, прямые лучи-клинки чередуются с лучами-языками.
std::vector<L> sunGlyph() {
  std::vector<L> v;
  std::string straight, wavy;
  for (int i = 0; i < 12; i++) {
    const double a = -90 + 30 * i;
    straight += "M" + pt(50, 50, 24, a - 6.5) + "L" + pt(50, 50, 49, a) + "L" + pt(50, 50, 24, a + 6.5) + "Z";
    const Pt b = polar(50, 50, 25, a + 15);
    wavy += tongue(b, polar(50, 50, 41, a + 15), 6, 0.12, -0.14, 1.1);
  }
  v.push_back(FU(straight));
  v.push_back(FU(wavy));
  v.push_back(XU(circ(50, 50, 26.2)));
  v.push_back(FU(circ(50, 50, 23.2)));
  v.push_back(G::XS(circ(50, 50, 20.2), 1.2f));
  // лик: брови, миндалевидные глаза, прямой нос, сомкнутые губы
  v.push_back(XU(sym(vein({38.5, 41.5}, {41, 39.6}, {44.5, 39.4}, {47.5, 41.4}, 1.8) + "M39 46.4Q43 43.4 47 46Q43 48.2 39 46.4Z") +
                   vein({50.6, 42}, {50.4, 47}, {49.4, 51.5}, {48.6, 55}, 1.7) + vein({43.5, 59.8}, {47, 60.6}, {53, 60.6}, {56.5, 59.8}, 1.9)));
  v.push_back(FU(sym(circ(43.4, 46.1, 1.15))));
  return v;
}

// Луна: тонкий полумесяц рогами вправо с прожилкой по середине и три звезды в его чаше.
std::vector<L> moonGlyph() {
  std::vector<L> v;
  v.push_back(FU(circ(52, 50, 46)));
  v.push_back(XU(circ(67, 43, 38)));
  // срединная прожилка: середина между внешней и внутренней дугами
  std::vector<Pt> mid;
  for (int i = 0; i <= 40; i++) {
    const double a = (-96 - 216.0 * i / 40) * kPi / 180, dx = std::cos(a), dy = std::sin(a);
    const double ox = 52 + dx * 46, oy = 50 + dy * 46;
    // пересечение луча из центра внешнего круга с внутренним кругом (ближняя к краю точка)
    const double fx = 52 - 67, fy = 50 - 43, bq = fx * dx + fy * dy, cq = fx * fx + fy * fy - 38 * 38, disc = bq * bq - cq;
    const double tq = disc > 0 ? -bq + std::sqrt(disc) : 46;
    mid.push_back({float((ox + 52 + dx * tq) / 2), float((oy + 50 + dy * tq) / 2)});
  }
  v.push_back(XU(ribbon(mid, [](double t) { return 1.9 * std::pow(std::sin(kPi * t), 0.6); })));
  v.push_back(FU(star(70, 36, 9.5, 3.6, 5, -90) + star(81, 59, 6.5, 2.5, 5, -90) + star(64, 70, 5, 1.9, 5, -90)));
  return v;
}

// Дерево: ствол с ветвями, кроны из заострённых листьев веерами, вырванные корни.
std::vector<L> treeGlyph() {
  std::vector<L> v;
  // крона: купол, по краю — венец заострённых листьев внахлёст, ветви внутри — вырезом
  const Pt c{50, 37};
  const double rx = 33, ry = 25;
  std::vector<std::string> leaves;
  const int cnt = 17;
  for (int i = 0; i < cnt; i++) {
    const double a = (150 + 240.0 * i / (cnt - 1)) * kPi / 180;
    const Pt b{float(c.x + rx * 0.82 * std::cos(a)), float(c.y + ry * 0.82 * std::sin(a))};
    const double dir = std::atan2(std::sin(a) * rx, std::cos(a) * ry) * 180 / kPi;
    leaves.push_back(blade(at(b, dir, -2), at(b, dir, i % 2 ? 14 : 16.5), 9.5, 0.05, 0.08));
  }
  v.push_back(FU(ell(c.x, c.y, rx, ry)));
  layeredFeathers(v, leaves, 1.6f, false);
  std::vector<std::string> inner;
  for (int i = 0; i < 9; i++) {
    const double a = (165 + 210.0 * i / 8) * kPi / 180;
    const Pt b{float(c.x + rx * 0.36 * std::cos(a)), float(c.y + 4 + ry * 0.36 * std::sin(a))};
    const double dir = std::atan2(std::sin(a) * rx, std::cos(a) * ry) * 180 / kPi;
    inner.push_back(blade(at(b, dir, -2), at(b, dir, 15), 9, 0.05, 0.08));
  }
  layeredFeathers(v, inner, 1.5f, false);
  std::string ribs;
  for (int i = 0; i < cnt; i++) {
    const double a = (150 + 240.0 * i / (cnt - 1)) * kPi / 180;
    const Pt b{float(c.x + rx * 0.82 * std::cos(a)), float(c.y + ry * 0.82 * std::sin(a))};
    const double dir = std::atan2(std::sin(a) * rx, std::cos(a) * ry) * 180 / kPi, len = i % 2 ? 14 : 16.5;
    ribs += vein(at(b, dir, 1), at(b, dir, len * 0.35), at(b, dir, len * 0.6), at(b, dir, len * 0.85), 1.4);
  }
  v.push_back(XU(ribs));
  v.push_back(XU(sym(vein({48.5, 58}, {42, 50}, {32, 44}, {22, 40}, 2.6) + vein({49, 52}, {45, 42}, {39, 30}, {33, 22}, 2.4) +
                       vein({49.5, 48}, {48.5, 38}, {47.6, 26}, {47.4, 16}, 2.2) + vein({40, 47}, {36, 52}, {30, 55}, {24, 56}, 1.8))));
  // ствол и корни
  const std::string trunk = sweep({50, 86}, {50.5, 76}, {49.5, 66}, {50, 54}, [](double t) { return 13 - 5.5 * t; });
  const std::string roots = sym(sweep({47, 81}, {42, 87}, {31, 90}, {14, 94}, [](double t) { return 5.5 * (1 - t) + 0.6; }) +
                                sweep({48, 83}, {45, 90}, {39, 95}, {30, 98.5}, [](double t) { return 5 * (1 - t) + 0.6; }));
  v.push_back(G::XS(trunk, 2.0f));
  v.push_back(FU(trunk + roots + "M47.5 84H52.5L50 99Z"));
  v.push_back(XU(vein({47.6, 84}, {48, 76}, {47.2, 68}, {47.8, 59}, 1.4) + vein({52.4, 82}, {52.8, 75}, {52, 68}, {52.4, 62}, 1.3)));
  return v;
}

// Лилия: изящная геральдическая лилия с прожилками лепестков и перевязью с ромбом.
std::vector<L> lilyGlyph() {
  std::vector<L> v;
  const Fleur f = fleur();
  v.push_back(FU(f.sides));
  v.push_back(FU(f.foot));
  v.push_back(G::XS(f.center, 2.4f));
  v.push_back(FU(f.center));
  v.push_back(G::XS(f.bind, 2.4f));
  v.push_back(FU(f.bind));
  // гравировка: прожилки лепестков, внутренний контур перевязи с ромбом
  v.push_back(XU(vein({50, 10}, {50, 22}, {50, 36}, {50, 52}, 2.2) + sym(vein({44, 55}, {38, 33}, {22, 22}, {12.5, 30}, 1.9))));
  v.push_back(G::XS(rrect(32.5, 59.5, 35, 3.5, 1.75), 1.2f));
  v.push_back(FU("M50 58.2 53.4 61.25 50 64.3 46.6 61.25Z"));
  return v;
}

// Меч: широкий клинок с долом, гарда с загнутыми остриями, обмотанная рукоять, гранёное навершие с камнем.
std::vector<L> swordGlyph() {
  const SwordParts s = swordParts();
  return {FU(s.fill), XU(s.holes), G::XS(s.cuts, 1.3f)};
}

// Скрещённые мечи: два меча с прямой гардой крест-накрест, передний — с зазором.
std::vector<L> swordsGlyph() {
  const SwordParts s = swordParts(7, 0, 0.78f);
  return moved(crossed(s.fill, s.holes, s.cuts, 1.3f, 38, 50, 31, 1.0f, 4.2f), Affine::translate(0, 2.5f));
}

// Щит треугольный (геральдический «хизер»): кайма, поле начетверо — 1-я и 4-я четверти сплошные, 2-я и 3-я
// заштрихованы гравировкой (вертикальная штриховка — червлень).
std::vector<L> shieldGlyph() {
  std::vector<L> v;
  const std::string outer = "M11 6H89V41C89 67 72 85 50 96 28 85 11 67 11 41Z";
  const std::string inner = "M16.5 11.5H83.5V41C83.5 63.5 68.5 79 50 89.4 31.5 79 16.5 63.5 16.5 41Z";
  v.push_back(FU(outer));
  v.push_back(XU(inner));
  v.push_back(FU("M19.6 14.6H48.4V43.4H19.6Z" + std::string("M51.6 46.6H80.4C80.2 63 67 76.5 51.6 85.4Z")));
  // нижняя граница поля по дуге (левая половина): y при данном x
  const auto bottom = [](double x) {
    double lo = 0, hi = 1;
    for (int i = 0; i < 30; i++) {
      const double t = (lo + hi) / 2;
      (bezier({19.6, 43}, {19.6, 63}, {33, 76.5}, {50, 85.6}, t).x < x ? lo : hi) = t;
    }
    return bezier({19.6, 43}, {19.6, 63}, {33, 76.5}, {50, 85.6}, lo).y;
  };
  std::string hatch;
  for (double x = 53.6; x < 80; x += 3.3) hatch += rrect(x - 0.65, 14.6, 1.3, 28.8, 0.3);
  for (double x = 21.4; x < 48.4; x += 3.3) hatch += rrect(x - 0.65, 46.6, 1.3, bottom(x + 0.65) - 46.6 - 0.6, 0.3);
  v.push_back(FU(hatch));
  return v;
}

// Череп анфас, суровый: угловатые глазницы с нависшими надбровьями, носовая впадина, скулы, зубы двумя рядами,
// трещина свода.
std::vector<L> skullGlyph() {
  std::vector<L> v;
  const std::string cranium = sym("M50 4C35 4 21 14 19.5 33 19 41 20.5 47 22.5 51L20 58 26 63 31 67.5 33.5 76H50Z");
  const std::string jaw = sym("M50 79.5H33L30.5 78 27.5 82C29.5 89 34 93.5 40 95.5 43.5 96.5 47 96.8 50 96.8Z");
  v.push_back(FU(cranium));
  v.push_back(FU(jaw));
  v.push_back(XU(sym("M23.5 41 35 36.5 46 40.5 45.5 50 38.5 56 28.5 54.5 24.5 48.5Z") + "M50 55.5 45.6 66.4 50 69 54.4 66.4Z"));
  std::string teeth;
  for (double x : {38.0, 42.0, 46.0, 50.0, 54.0, 58.0, 62.0}) teeth += seg(x, 70, x, 76.5);
  for (double x : {40.0, 44.0, 48.0, 52.0, 56.0, 60.0}) teeth += seg(x, 80, x, 87.5);
  v.push_back(G::XS(teeth + "M33 77.7H67", 1.4f));
  v.push_back(G::XS(sym("M22 59Q29 66 36 63.5M20.5 30Q23.5 38 22.4 46") + "M58 5 55.4 12.5 60.2 17 57 25.5M48.4 62.6 50 65.4 51.6 62.6", 1.4f));
  return v;
}

// Якорь: кольцо, сужающийся шток-перекладина, ствол, рога полумесяцем с лапами-остриями, канат вокруг.
std::vector<L> anchorGlyph() {
  std::vector<L> v;
  const std::string arm = sweep({50, 88.5}, {37, 89}, {21, 82}, {14.5, 63.5}, [](double t) { return 9.5 - 4.5 * t; });
  const std::string fluke = "M14 50.5C15.2 56 18.5 60.5 24.5 63.5L16 64.6 6.2 66.5C9.5 61.5 12.4 56.5 14 50.5Z";
  const std::string iron = "M46 15H54L53.4 86H46.6Z" + std::string("M21 22.6 50 19.4 79 22.6 50 25.8Z") + sym(arm + fluke) + "M44.6 85H55.4L50 97Z";
  const std::string rope = "M55.5 13.5C66 15 71 20 68 28 66.5 32 64 35 62 37.5L38 43.5C31 46 29.5 53 35 56.5 39 59 47 59.5 51 61 57 63 62 66 62.5 69.5"
                           "L37.5 75.5C33 77 31.5 80 32 83";
  v.push_back(G::S(circ(50, 9.5, 5.6), 4));
  v.push_back(G::S(rope, 3.2f));
  v.push_back(G::XS(iron, 4.4f));
  v.push_back(FU(iron));
  v.push_back(G::XS("M58 38.5 42 42.5M58 71 42 74.5", 7.4f));
  v.push_back(G::S("M62 37.5 38 43.5M62.5 70 37.5 75.5", 3.2f));
  v.push_back(XU(vein({50, 28}, {50, 40}, {50, 60}, {50, 80}, 1.8) + sym(vein({47, 86}, {36, 86}, {22, 80}, {16, 66}, 1.6))));
  return v;
}

// Корабль-ког: корпус полумесяцем с надстройками, мачта, надутый парус с крестом, рей, вымпел, ванты, волны.
std::vector<L> shipGlyph() {
  std::vector<L> v;
  v.push_back(G::S("M50 9 9 50M50 9 91 47", 1.3f));
  const std::string hull =
      "M4 55H96L90.5 69C86.5 78.5 78 83 67 83H33C22 83 13.5 78.5 9.5 69Z" + merlons(4, 22, 46, 55.5, 3, 2.6) + merlons(78, 96, 46, 55.5, 3, 2.6);
  const std::string sail = "M23 15.5Q50 10.5 77 15.5C80.5 27 80.5 39 76.5 50.5Q50 45.5 23.5 50.5C19.5 39 19.5 27 23 15.5Z";
  v.push_back(FU(hull + rrect(48.6, 4, 2.8, 52, 1) + tongue({51, 5.5}, {73, 7}, 5.2, 0.1, -0.16, 1.1)));
  v.push_back(G::XS(sail, 3));
  v.push_back(FU(sail));
  v.push_back(FU(sweep({20, 15.5}, {35, 12.4}, {65, 12.4}, {80, 15.5}, [](double t) { return 2 + 2 * std::sin(kPi * t); })));
  v.push_back(XU("M47.6 23.5H52.4V29.6H58.6V34.4H52.4V43.5H47.6V34.4H41.4V29.6H47.6Z" + vein({28, 20}, {25.5, 28}, {25.5, 38}, {28, 46}, 1.6) +
                   vein({72, 20}, {74.5, 28}, {74.5, 38}, {72, 46}, 1.6)));
  v.push_back(G::XS("M8.5 61.6Q50 64 91.5 61.6M12 70Q50 72.5 88 70M4 57.6H96", 1.35f));
  v.push_back(XU(rrect(13.5, 58.8, 3.4, 3.2, 1) + rrect(83.1, 58.8, 3.4, 3.2, 1)));
  v.push_back(FU(waveStrip(3, 52, 91, 2.6, 2, 3.6) + waveStrip(48, 97, 91, 2.6, 2, 3.6)));
  return v;
}

// Колосья: три колоса с острыми зёрнами и длинными остями, листья, перевязь.
std::vector<L> wheatGlyph() {
  std::vector<L> v;
  const Affine ms[] = {rotAt(-26, 50, 76) * Affine::translate(50, 55), rotAt(26, 50, 76) * Affine::translate(50, 55), Affine::translate(50, 51)};
  std::string awns, stems;
  std::vector<std::vector<std::string>> grains(3);
  for (int k = 0; k < 3; k++) {
    const Affine& m = ms[k];
    for (int i = 0; i < 5; i++) {
      const float y = -3.5f - 6.6f * float(i);
      for (int side : {-1, 1}) {
        const Affine g = m * Affine::translate(1.6f * float(side), y) * Affine::rotate(0.42f * float(side));
        grains[k].push_back(xf("M0 0C3 -2.8 3.4 -7.4 0 -12C-3.4 -7.4 -3 -2.8 0 0Z", g));
        awns += xf("M0 -11.2C0.2 -15.5 0.6 -19 1.4 -22.5", g);
      }
    }
    const Affine top = m * Affine::translate(0, -36.5f);
    grains[k].push_back(xf("M0 0C3 -2.8 3.4 -7.4 0 -12C-3.4 -7.4 -3 -2.8 0 0Z", top));
    awns += xf("M0 -11.5V-17", top);
    stems += xf("M0 0V24", m);
  }
  v.push_back(G::S(awns, 1.1f));
  v.push_back(FU(sym(blade({47, 74}, {16, 64}, 6, -0.06, -0.1)) + sweep({50, 51}, {50, 65}, {50, 80}, {50, 96}, [](double t) { return 3 - 0.8 * t; })));
  v.push_back(G::S(stems + "M50 76 38 95M50 76 62 95", 2.4f));
  for (const auto& g : grains) layeredFeathers(v, g, 1.4f, false);
  v.push_back(XU(vein({50, 50.5}, {50, 44}, {50, 30}, {50, 17}, 1.2)));
  const std::string band = "M40 70.5H60L61.5 75 60 79.5H40L38.5 75Z";
  v.push_back(G::XS(band, 2.2f));
  v.push_back(FU(band));
  v.push_back(G::XS("M41.5 75H58.5", 1.1f));
  return moved(v, Affine::translate(50, 51.5f) * Affine::scale(0.94f) * Affine::translate(-50, -50));
}

// Самоцвет: бриллиант — площадка, грани короны и павильона сужающимися вырезами, искры.
std::vector<L> gemGlyph() {
  const std::string body = "M27 15H73L93.5 37 50 92 6.5 37Z";
  const auto e = [](Pt a, Pt b, double w) { return vein(a, mix(a, b, 0.33), mix(a, b, 0.66), b, w); };
  const std::string cuts = e({39, 16}, {29, 36.5}, 1.8) + e({61, 16}, {71, 36.5}, 1.8) + e({39, 16}, {50, 36}, 1.6) + e({61, 16}, {50, 36}, 1.6) +
                           e({27.5, 16.5}, {17.5, 36.5}, 1.6) + e({72.5, 16.5}, {82.5, 36.5}, 1.6) + e({29, 38}, {50, 88}, 2) + e({71, 38}, {50, 88}, 2) +
                           e({50, 38}, {50, 86}, 1.8) + e({17.5, 38}, {48, 84}, 1.4) + e({82.5, 38}, {52, 84}, 1.4);
  return {FU(body + star(85, 9, 8, 1.6, 4, -90) + star(94, 22, 4.4, 1, 4, -90) + star(10, 12, 4.4, 1, 4, -90)), G::XS("M8 37H92", 1.8f), XU(cuts)};
}

// Боевой молот: квадратный боёк с гранями, изогнутый клюв, верхнее остриё, лангеты, обмотка рукояти, пятка.
std::vector<L> hammerGlyph() {
  std::vector<L> v;
  // боёк: гранёный брусок с ударной площадкой слева, клюв справа, остриё сверху
  const std::string face = "M14 10.5H20V33.5H14L12 31V13Z";
  const std::string block = "M20 8H43L46 11V32L43 35H20Z";
  const std::string head = block + face + std::string("M44 13H56V30H44Z") +
                           sweep({54, 16.5}, {63, 17}, {72, 20}, {83, 32}, [](double t) { return 12 * std::pow(1 - t, 0.8); }) +
                           tongue({50, 14}, {50, 0.5}, 7.5, 0, 0, 1.2);
  const std::string haft = "M45.4 30H54.6L53.8 86H46.2Z" + std::string("M44 31H56L55 41H45Z") + "M44.6 85H55.4L50 99Z";
  v.push_back(FU(head));
  v.push_back(FU(haft));
  v.push_back(G::XS("M20 8.5V34.5M24 14H42M24 29H42M44.5 31.5V40.5", 1.4f));
  v.push_back(XU(vein({24, 21.5}, {30, 21.5}, {36, 21.5}, {42, 21.5}, 1.6) + vein({57, 19.5}, {64, 20}, {71, 22.5}, {78, 29}, 1.5) + circ(50, 21.5, 1.6)));
  v.push_back(G::XS("M46.4 66 53.6 63.2M46.4 72 53.6 69.2M46.4 78 53.6 75.2M45.6 86.5H54.4", 1.3f));
  return v;
}

// Секира: два лезвия полумесяцем с заострёнными рогами и фаской вдоль кромки, копейное навершие, муфты, обмотка.
std::vector<L> axeGlyph() {
  std::vector<L> v;
  const std::string blade = "M45 26C37 24 28 19 20.5 8.5 11.5 18.5 5.5 31 5.5 43.5S11.5 68.5 20.5 78.5C28 68 37 63 45 61Z";
  v.push_back(FU(sym(blade) + rrect(45.2, 13, 9.6, 79, 3.5) + tongue({50, 15}, {50, 0.5}, 9, 0, 0, 1.3) + rrect(42.5, 22, 15, 5, 2) + rrect(42.5, 60, 15, 5, 2) +
                   "M44 88H56L50 99Z"));
  // фаска вдоль кромки, прорезь-ланцет у обуха
  v.push_back(XU(sym(vein({19, 13}, {11.5, 24}, {9.6, 36}, {9.6, 43.5}, 1.8) + vein({9.6, 43.5}, {9.6, 51}, {11.5, 63}, {19, 74}, 1.8) +
                       vein({37.5, 31}, {32.5, 38}, {32.5, 49}, {37.5, 56}, 3.2) + vein({28, 30}, {24.5, 37}, {24.5, 50}, {28, 57}, 1.5))));
  v.push_back(G::XS("M50 7V14M45.4 70.5 54.6 67.6M45.4 75 54.6 72.1M45.4 79.5 54.6 76.6M45.4 84 54.6 81.1", 1.3f));
  return v;
}

// Лук: составной лук с загнутыми концами, рукоять, натянутая тетива, стрела с гранёным наконечником и оперением.
std::vector<L> bowGlyph() {
  std::vector<L> v;
  const std::string limb = sweep({66, 43}, {69, 26}, {58, 10}, {38, 6}, [](double t) { return 12.5 - 7 * t; }) + tongue({38.5, 6}, {32.5, 11}, 5.4, -0.3, 0.1);
  const std::string arrow = "M14 48.3H82V51.7H14Z" + std::string("M97 50 80 42 84 50 80 58Z") + "M14 48.3 21 39H33L28.5 48.3ZM14 51.7 21 61H33L28.5 51.7Z";
  v.push_back(G::S("M35 8.5 15 50 35 91.5", 2.0f));
  v.push_back(FU(limb + xf(limb, Affine{1, 0, 0, -1, 0, 100}) + rrect(60.5, 38, 11, 24, 4)));
  v.push_back(G::XS("M60.5 43H71.5M60.5 57H71.5", 1.3f));
  v.push_back(XU(vein({66.5, 37}, {67.5, 26}, {60, 14}, {44, 9}, 1.4) + xf(vein({66.5, 37}, {67.5, 26}, {60, 14}, {44, 9}, 1.4), Affine{1, 0, 0, -1, 0, 100})));
  v.push_back(G::XS(arrow, 2.6f));
  v.push_back(FU(arrow));
  v.push_back(G::XS("M20 46.5 24 42.2M24.5 46.5 28.5 42.2M20 53.5 24 57.8M24.5 53.5 28.5 57.8M84.5 50H95", 1.2f));
  return moved(v, Affine::translate(-4, 0));
}

// Ключ: ушко-четырёхлистник с заострёнными лопастями и вырезами, кольца-муфты, стержень, бородка с прорезями.
std::vector<L> keyGlyph() {
  std::vector<L> v;
  const Pt c{50, 21};
  std::string bow, holes;
  for (int i = 0; i < 4; i++) {
    const double a = -90 + 90 * i;
    bow += blade(at(c, a, -2), at(c, a, 19.5), 17.5, 0, 0);
    holes += vein(at(c, a, 4.5), at(c, a, 8), at(c, a, 11), at(c, a, 15), 4.2);
  }
  v.push_back(FU(bow + circ(c.x, c.y, 10) + rrect(42, 38.5, 16, 5, 2) + rrect(43.5, 46, 13, 4, 1.5) + rrect(45.6, 40, 8.8, 54, 2.8) + "M53 64H73V90H53Z" +
                   "M44 92.5H56L50 99Z"));
  v.push_back(XU(holes + "M50 15.5 55.5 21 50 26.5 44.5 21Z" + "M60 64V74H63.6V64Z" + "M54.4 78H66V81.8H54.4Z" + "M68 90V84H73V90Z"));
  v.push_back(FU("M50 18.3 52.7 21 50 23.7 47.3 21Z"));
  v.push_back(G::XS("M42 41H58", 1.2f));
  return v;
}

// Око: миндалевидный глаз с острыми уголками, радужка с лучами, зрачок-щель, блик; сияние клинками.
std::vector<L> eyeGlyph() {
  std::vector<L> v;
  std::string rays;
  for (int i = 0; i < 16; i++) {
    const double a = -90 + 22.5 * i;
    if (std::fabs(std::sin(a * kPi / 180)) < 0.3) continue;   // по бокам — уголки глаза
    const double r1 = i % 2 ? 41 : 48;
    rays += "M" + pt(50, 50, 29, a - 4.2) + "L" + pt(50, 50, r1, a) + "L" + pt(50, 50, 29, a + 4.2) + "Z";
  }
  const std::string eye = "M2 50C16 32 32 23.5 50 23.5S84 32 98 50C84 68 68 76.5 50 76.5S16 68 2 50Z";
  v.push_back(FU(rays));
  v.push_back(G::XS(eye, 3.4f));
  v.push_back(FU(eye));
  v.push_back(XU("M11 50C22 37 35 30.5 50 30.5S78 37 89 50C78 63 65 69.5 50 69.5S22 63 11 50Z"));
  v.push_back(FU(circ(50, 50, 17.5)));
  std::string iris;
  for (int i = 0; i < 18; i++) iris += vein(polar(50, 50, 8.5, 20 * i), polar(50, 50, 10.5, 20 * i), polar(50, 50, 13, 20 * i), polar(50, 50, 15.6, 20 * i), 1.5);
  v.push_back(XU(iris + "M50 39.5C53.2 43 54.4 46.5 54.4 50S53.2 57 50 60.5C46.8 57 45.6 53.5 45.6 50S46.8 43 50 39.5Z"));
  v.push_back(FU(circ(46.6, 45, 1.6)));
  return v;
}

// Пламя: языки S-изгибами из общего основания с прожилками, внутреннее пламя контуром и сердцевиной-прожилкой.
std::vector<L> flameGlyph() {
  std::vector<L> v;
  // языки S-изгибами из общего основания; середины языков и внутреннее пламя — вырезами
  Strands fl;
  fl.add({50, 74}, {50, 1.5}, 22, 0.07, -0.13, 1.6);
  for (int side : {-1, 1}) {
    const auto P = [&](double x, double y) { return Pt{float(50 + (x - 50) * side), float(y)}; };
    fl.add(P(40, 72), P(27, 11), 15, -0.12 * side, 0.2 * side, 1.5);
    fl.add(P(31, 80), P(8.5, 36), 11.5, -0.08 * side, 0.24 * side, 1.4);
    fl.add(P(45, 64), P(37.5, 31), 8, 0.14 * side, -0.24 * side, 1.2);
  }
  v.push_back(FU("M50 97.5C35 97.5 24.5 88 24.5 75.5 24.5 69 27 64 30.5 60L69.5 60C73 64 75.5 69 75.5 75.5 75.5 88 65 97.5 50 97.5Z"));
  v.push_back(FU(fl.fill));
  v.push_back(XU(fl.veins));
  // внутреннее пламя — контуром, сердцевина — прожилкой
  v.push_back(G::XS("M51 44C55 55 63.5 64 63 75.5 62.5 86 57 92.5 50 92.5 43 92.5 37.5 86.5 37.5 77.5 37.5 67.5 47 59 51 44Z", 1.6f));
  v.push_back(XU(vein({50, 88}, {49, 80}, {51, 72}, {50.5, 62}, 2.4)));
  return v;
}

// Орёл распростёртый: крылья подняты, маховые перья-клинки свисают с плечевых дуг, кроющие рядом, грудь с двойным
// контуром и перьями, голова влево с крючковатым клювом и языком, хвост веером, лапы в оперённых «штанах».
std::vector<L> eagleGlyph() {
  std::vector<L> v;
  // крыло (левое; правое — отражением): плечевая дуга вверх-наружу, маховые свисают с неё веером (внешние снизу),
  // кроющие — рядом коротких перьев вдоль дуги
  {
    const Pt s0{42, 37}, s1{31, 33}, s2{17, 21}, s3{6, 6};
    const Pt t0{1.5, 16}, t1{-1, 40}, t2{10, 58}, t3{31, 64};
    std::vector<std::string> prim, cov;
    const int cnt = 8;
    for (int k = 0; k < cnt; k++) {
      const double u = double(k) / (cnt - 1);           // 0 — у кончика, 1 — у плеча
      const CurvePt q = bezier(s0, s1, s2, s3, 0.95 - u * 0.82), e = bezier(t0, t1, t2, t3, u);
      const Pt b{float(q.x), float(q.y)}, tip{float(e.x), float(e.y)};
      const double th = angleOf(b, tip);
      prim.push_back(blade(at(b, th, -3), tip, 8.4, 0.02, 0.06));
      if (k % 2 == 1 || k == cnt - 1) cov.push_back(blade(at(b, th, -2), at(b, th, 11), 8, 0.03, 0.05));
    }
    layeredFeathers(v, prim, 1.7f, true);
    layeredFeathers(v, cov, 1.6f, true);
    const std::string arm = sweep(s0, s1, s2, s3, [](double t) { return 10 * std::pow(1 - t, 0.55); });
    v.push_back(G::XS(sym(arm), 1.8f));
    v.push_back(FU(sym(arm)));
  }
  // хвост веером
  {
    std::vector<std::string> tail;
    for (double a : {-30.0, 30.0, -15.0, 15.0, 0.0}) tail.push_back(blade({50, 64}, at({50, 64}, 90 + a, 31 - std::fabs(a) * 0.12), 8.4, 0, 0));
    layeredFeathers(v, tail, 1.8f, false);
  }
  // ноги в оперённых «штанах», лапы с когтями
  {
    const std::string thigh = sweep({45, 58}, {41, 64}, {38, 69}, {35, 74}, [](double t) { return 10 - 4 * t; }) + tongue({38, 70}, {33.5, 81}, 4.6, 0.1, -0.1) +
                              tongue({41, 68}, {39.5, 79}, 4.2, 0.1, -0.1);
    v.push_back(G::XS(sym(thigh), 1.8f));
    v.push_back(FU(sym(thigh)));
    v.push_back(G::XS(sym(talon({33, 76}, 128, 1, 1)), 1.4f));
    v.push_back(FU(sym(talon({33, 76}, 128, 1, 1))));
  }
  // туловище с двойным контуром и перьями на груди; голова влево: крючковатый клюв раскрыт, язык, перья затылка
  {
    const std::string body = "M50 21C57 23 60.5 32 60.5 44 60.5 55 56.5 63 50 70 43.5 63 39.5 55 39.5 44 39.5 32 43 23 50 21Z";
    const std::string hackles = tongue({53, 12}, {60, 23}, 5, -0.1, 0.12) + tongue({54.5, 17}, {60, 28}, 4.6, -0.1, 0.12);
    const std::string head = "M56 17C56 9.5 52 4.5 46 4.5 42 4.5 38.5 6 36.5 8.5 33 8.5 30.5 11 30.5 16 31.5 14.2 33 13.4 35 13.5L39 14.6 33.6 18.2"
                             "C35 19.4 37.5 19.8 40.5 19.2 42.5 21 44 24 44.5 30L55.5 30C57 24 57 20 56 17Z";
    v.push_back(G::XS(body, 2.0f));
    v.push_back(G::XS(hackles + head, 2.0f));
    fills(v, {body, hackles, head});
    v.push_back(G::XS(xf(body, Affine::translate(50, 46) * Affine::scale(0.78f, 0.8f) * Affine::translate(-50, -46)), 1.3f));
    std::string sc;
    for (const Pt p : {Pt(47, 40), Pt(53, 40), Pt(50, 46), Pt(47, 52), Pt(53, 52), Pt(50, 58)})
      sc += "M" + n(p.x - 2.4) + " " + n(p.y) + "C" + n(p.x - 2) + " " + n(p.y + 2) + " " + n(p.x - 1) + " " + n(p.y + 3) + " " + n(p.x) + " " + n(p.y + 4) + "C" +
            n(p.x + 1) + " " + n(p.y + 3) + " " + n(p.x + 2) + " " + n(p.y + 2) + " " + n(p.x + 2.4) + " " + n(p.y);
    v.push_back(G::XS(sc, 1.3f));
    const std::string tng = tongue({38.5, 16.2}, {27.5, 21}, 2.8, 0.08, -0.24);
    v.push_back(G::XS(tng, 1.3f));
    v.push_back(FU(tng));
    v.push_back(XU("M40.8 10.4 46.2 8.6 45 11.4Z"));
    v.push_back(G::XS("M36.6 9Q35.4 11 35.8 13.3M47.5 12.5Q50.5 17 49.5 22", 1.3f));
  }
  return v;
}

// Лев восстающий (гербовый, обращён вправо от зрителя — влево): грива языками, раскрытая пасть с языком,
// когти полумесяцами, хвост S-изгибом с кистью-пламенем.
std::vector<L> lionGlyph() {
  std::vector<L> v;
  // хвост S-изгибом и кисть языками
  fills(v, {sweep({71, 66}, {84, 75}, {97, 60}, {88, 46}, [](double t) { return 4.6 - 1.2 * t; }),
            sweep({88, 46}, {81, 37}, {80, 27}, {86, 19}, [](double t) { return 3.4 - 0.9 * t; }) + circ(88, 46, 1.7),
            tongue({86, 21.5}, {97.5, 8.5}, 6.6, 0.22, -0.12), tongue({85.5, 21.5}, {82.5, 3}, 6, -0.24, 0.14), tongue({87, 21.5}, {99, 26}, 5.4, -0.26, 0.12)});
  // дальние лапы: опорная задняя и нижняя передняя
  fills(v, {limb({{67, 70}, {76, 85}, {67, 92}}, {13, 7, 5.6}), lionPaw({64.5, 92.5}, 182, 1, -1), limb({{44, 47}, {35, 57}, {22, 54}}, {10, 6.6, 5.4}),
            lionPaw({20, 54}, 185, 0.95, -1)});
  // туловище: широкая грудь, тонкая талия
  layerFront(v, {sweep({44, 36}, {46, 51}, {52, 61}, {63, 69}, [](double t) { const double u = 1 - t; return 26 * u * u + 2 * 2 * u * t + 22 * t * t; }) +
                 circ(45, 40, 11)});
  // грива: языки внахлёст, закинутые назад и вниз
  {
    const std::string mass = xf(ell(0, 0, 12, 17), place(46, 23, 12));
    v.push_back(G::XS(mass, 1.8f));
    v.push_back(FU(mass));
    layeredFeathers(v, locks({46, 23}, 11, 16, 12, 122, -100, 12, 13, 55, 0.5, 9.4, 0.14, -0.16), 1.6f, false);
  }
  // голова: тяжёлый лоб, раскрытая пасть с клыками, язык пламенем
  {
    const std::string head = "M44 5L33 4.6C30 4.8 28.5 6 27.5 7.5L20.5 10.5C19 11.2 18.3 12.5 18.5 14L19 16 32 19.5 21.5 26.5"
                             "C20.8 27 21 28.4 22.2 28.8 26 30 31 30 36 28.5 42 26.5 46 22 46 14 46 9.5 45.5 7 44 5Z";
    layerFront(v, {head});
    const std::string tng = tongue({30.5, 20.8}, {12.5, 25.5}, 3.8, -0.16, 0.2);
    layerFront(v, {tng}, 1.5f);
    v.push_back(FU("M19.8 15.7 21.2 19.6 22.8 16.1ZM22.8 25.9 24 22.5 25.4 25.1Z"));
    v.push_back(XU("M28.2 10.6 34.8 7.6 33.6 10.8Z"));
    v.push_back(G::XS("M22.6 10.6Q23.6 11.8 23.2 13.2M25 9.6Q26 10.8 25.7 12.2M37 11Q40 17 36 23.5", 1.4f));
  }
  // ближние лапы: поднятая передняя (с кисточкой у локтя) и выставленная задняя (с кисточкой у скакательного сустава)
  layerFront(v, {limb({{46, 40}, {33, 46}, {22.5, 33.5}}, {12.5, 7.4, 5.6}), tongue({34, 47}, {39, 54.5}, 4.4, 0.2, -0.16), lionPaw({21, 31.5}, -128, 1, -1)});
  layerFront(v, {xf(ell(0, 0, 12.5, 10.5), place(63, 70, -40)), limb({{59, 74}, {48.5, 85}, {37.5, 82.5}}, {11.5, 6.6, 5.4}),
                 tongue({49.5, 86.5}, {56.5, 91.5}, 4.4, -0.2, 0.16), lionPaw({35.5, 81.8}, 200, 1, -1)});
  v.push_back(G::XS("M49 53Q52.5 58 53 64M46.5 57Q49 61 49.5 66M56 63Q62 60 68 63", 1.4f));
  return v;
}

// Дракон шагающий: хищная рогатая голова с раскрытой пастью и раздвоенным языком, шея S-изгибом с гребнем
// и брюшными пластинами, перепончатое крыло с фестонами, когтистые лапы, хвост змеёй с остриём-стрелой.
std::vector<L> dragonGlyph() {
  std::vector<L> v;
  // крыло: перепонка с глубокими фестонами между пальцами, пальцы — сужающимися костями, коготь на сгибе
  {
    const Pt sh{52, 50}, wr{57, 10};
    const Pt tips[] = {{78, 2}, {94, 10}, {99, 28}, {90, 44}};
    std::string mem = "M" + ptS(sh) + "C45 36 46 20 " + ptS(wr) + "L" + ptS(tips[0]);
    for (int i = 0; i < 3; i++) {
      const Pt m = mix(mix(tips[i], tips[i + 1], 0.5), wr, 0.36);
      mem += "Q" + ptS(m) + " " + ptS(tips[i + 1]);
    }
    mem += "Q74 44 68 55Z";
    v.push_back(FU(mem));
    std::string bones, veins;
    for (const Pt t : tips) bones += sweep(wr, at(wr, angleOf(wr, t) - 6, 12), at(wr, angleOf(wr, t) - 2, 26), t, [](double u) { return 4.4 * (1 - u) + 0.4; }, 20);
    for (int i = 0; i < 3; i++) {
      const Pt m = mix(mix(tips[i], tips[i + 1], 0.5), wr, 0.5);
      veins += "M" + ptS(mix(wr, m, 0.3)) + "Q" + ptS(at(mix(wr, m, 0.65), angleOf(wr, m) - 90, 1.2)) + " " + ptS(m);
    }
    v.push_back(G::XS(veins, 1.3f));
    v.push_back(G::XS(bones, 2.0f));
    v.push_back(FU(bones));
    const std::string arm = sweep(sh, {45, 36}, {46, 20}, wr, [](double t) { return 9 - 4.5 * t; }) + claw(wr, -115, 7.5, -75, 3.2);
    v.push_back(G::XS(arm, 2.0f));
    v.push_back(FU(arm));
  }
  // хвост змеёй с остриём-стрелой
  fills(v, {sweep({66, 58}, {84, 55}, {98, 64}, {95, 78}, [](double t) { return 6.2 - 2.8 * t; }) + circ(95, 78, 1.7),
            sweep({95, 78}, {92, 89}, {80, 93}, {76, 86}, [](double t) { return 3.4 - 1.6 * t; }),
            xf("M0 0 5 -5.5 0 -13 -1.4 -5ZM0 0-5 -5.5 0 -13 1.4 -5Z", place(76.5, 87.5, -24))});
  // дальние лапы
  fills(v, {limb({{40, 57}, {37.5, 74}, {34, 90}}, {8, 5.2, 4.2}), lionPaw({32.5, 91.6}, 175, 0.9, -1, 3),
            limb({{70, 59}, {77, 73}, {72, 86}, {67.5, 92}}, {10.5, 5.8, 4.4, 4}), lionPaw({65.5, 92.6}, 178, 0.9, -1, 3)});
  // шея S-изгибом и туловище, гребень зубцами назад, брюхо пластинами
  {
    const Pt n0{31, 27}, n1{47, 31}, n2{23, 44}, n3{40, 54};
    const auto nw = [](double t) { return 8 + 6 * t; };
    std::string ridge;
    for (int i = 0; i < 5; i++) {
      const double t = 0.16 + i * 0.17;
      const CurvePt q = bezier(n0, n1, n2, n3, t);
      const double w = nw(t) * 0.5, dir = std::atan2(-q.ny, -q.nx) * 180 / kPi;
      const Pt b0 = {float(q.x - q.nx * w * 0.75), float(q.y - q.ny * w * 0.75)};
      ridge += tongue(b0, at(b0, dir + 55, 5.5), 4, 0.1, 0);
    }
    for (int i = 0; i < 5; i++) {
      const CurvePt q = bezier({43, 51}, {51, 47}, {60, 48.5}, {69, 52.5}, 0.1 + i * 0.2);
      const Pt b0{float(q.x), float(q.y)};
      ridge += tongue(b0, at(b0, -35, 5), 3.8, 0.1, 0);
    }
    const std::string neck = sweep(n0, n1, n2, n3, nw);
    const auto bw = [](double t) { return 15 * (1 - t) * (1 - t) + 16 * (1 - t) * t + 13 * t * t; };
    const std::string body = sweep({38, 56}, {48, 60}, {60, 60}, {70, 57}, bw) +
                             circ(39, 56, 7.5);
    layerFront(v, {ridge, neck, body});
    std::string belly;
    {
      std::vector<Pt> edge;
      for (int i = 0; i <= 12; i++) {
        const double t = 0.18 + i * 0.065;
        const CurvePt q = bezier(n0, n1, n2, n3, t);
        const double w = nw(t) * 0.5;
        edge.push_back({float(q.x + q.nx * w * 0.42), float(q.y + q.ny * w * 0.42)});
        if (i % 2 == 1) belly += "M" + ptS(edge.back()) + "L" + ptS({float(q.x + q.nx * w * 1.1), float(q.y + q.ny * w * 1.1)});
      }
      belly += "M" + ptS(edge[0]);
      for (size_t i = 1; i < edge.size(); i++) belly += "L" + ptS(edge[i]);
    }
    for (double x : {46.0, 52.0, 58.0, 64.0}) belly += "M" + n(x) + " 61.5Q" + n(x - 1.4) + " 63.5 " + n(x - 1) + " 65.5";
    v.push_back(G::XS(belly, 1.3f));
  }
  // голова: рога назад, раскрытая пасть с зубами, раздвоенный язык
  {
    std::vector<L> h;
    const std::string horns = tongue({29, 10.5}, {48, 2}, 4.6, 0.14, -0.1, 1.3) + tongue({32, 14}, {46, 14}, 3.8, 0.12, -0.12, 1.3) +
                              tongue({34, 23}, {43, 26}, 3.6, -0.1, 0.1);
    const std::string head = "M34 11C30 9 27 8.6 24 9.2L22 7.2 20.6 10 9 13.4C7.4 13.6 6 13 4.8 12.4 4.6 14.2 5 15.8 6 17L24 21 9.5 25.6"
                             "C8.6 26 8.6 27.4 9.6 27.8 13 28.8 18 29 23 28.6 28 28 32 26.8 34.5 24.5 37 21.5 37 15 34 11Z";
    h.push_back(G::XS(horns + head, 2.0f));
    h.push_back(FU(horns));
    h.push_back(FU(head));
    const std::string tng = sweep({21, 22.5}, {15, 23.2}, {9, 23.6}, {3, 23}, [](double t) { return 2.2 - 0.6 * t; }, 16) + tongue({3.5, 23}, {0, 19.8}, 2, 0, 0) +
                            tongue({3.5, 23}, {0.5, 26.5}, 2, 0, 0);
    h.push_back(G::XS(tng, 1.3f));
    h.push_back(FU(tng));
    h.push_back(
        FU("M8 17.3 9.4 20.6 10.8 17.9ZM12.5 18.3 13.8 21.4 15.2 18.9ZM17 19.3 18.2 22 19.5 19.8ZM11.2 25.4 12.6 22.5 14 24.8ZM15.6 24 16.8 21.4 18.2 23.6Z"));
    h.push_back(XU("M17.5 13.6 24 11.4 23 14.2Z"));
    h.push_back(G::XS("M7.5 14.4Q9 13.6 10.5 14.2M28 15.5Q31.5 20 29.5 25.5M22.5 10.2Q27 9.6 31 11", 1.3f));
    for (const L& l : moved(h, Affine::translate(1, 5))) v.push_back(l);
  }
  // ближние лапы: поднятая передняя и шагающая задняя
  layerFront(v, {limb({{41, 57}, {30.5, 65.5}, {20.5, 59}}, {8.8, 5.2, 4.4}), lionPaw({19, 57.6}, -150, 0.95, -1, 3)});
  layerFront(v, {xf(ell(0, 0, 10.5, 8.5), place(64, 61, -30)), limb({{61, 65}, {59, 80}, {54.5, 91}}, {9.5, 5, 4.2}), lionPaw({53, 92.6}, 178, 0.9, -1, 3)});
  v.push_back(G::XS("M56 57Q61 54.5 67 56", 1.3f));
  return v;
}

// Волк воющий: сидит, морда к небу, уши прижаты, грудь в острых прядях, пышный хвост языками.
std::vector<L> wolfGlyph() {
  std::vector<L> v;
  // хвост вдоль земли, пушистый, с языками-прядями сверху; дальняя передняя лапа
  {
    const std::string tail = sweep({79, 80}, {90, 84}, {97, 90}, {98, 78}, [](double t) { return 9 * std::sin(kPi * (0.2 + 0.8 * t)) + 1; });
    std::string tufts;
    for (int i = 0; i < 4; i++) {
      const CurvePt q = bezier({79, 80}, {90, 84}, {97, 90}, {98, 78}, 0.3 + i * 0.17);
      const Pt b{float(q.x), float(q.y)};
      tufts += tongue(b, at(b, std::atan2(q.ny, q.nx) * 180 / kPi + (q.ny < 0 ? 0 : 180) - 25, 7.5), 4.4, 0.1, -0.1);
    }
    fills(v, {tail, tufts, limb({{48, 62}, {47.5, 80}, {48, 90.5}}, {8, 5.4, 4.8}), lionPaw({48, 91.5}, 178, 0.75, -1, 3)});
  }
  // туловище, бедро, шея
  layerFront(v, {xf(ell(0, 0, 14, 18), place(56, 61, 30)), xf(ell(0, 0, 15, 14), place(69, 77, 0)),
                 sweep({33, 22}, {41, 32}, {44, 42}, {46, 54}, [](double t) { return 14 + 8 * t; })});
  // голова: морда вверх-влево, приоткрытая пасть, уши назад
  {
    const std::string head = "M47 21C45 15 40 12 35 12.5L17.5 4.5C15.5 3.8 14 4.5 14.5 6.6L16 9.2 30 17.5 17 13.6C16 15 16.4 16.6 17.6 17.4L29 25.5"
                             "C33 28.5 39 30.5 44 29 47.5 27.5 48.5 24.5 47 21Z";
    const std::string ears = tongue({41, 14}, {53, 6}, 6.5, 0.12, -0.1, 1.1) + tongue({44, 16}, {55, 12.5}, 5.5, 0.1, -0.1, 1.1);
    layerFront(v, {ears, head});
    v.push_back(XU("M33.5 13.4 39.6 12.2 38 15.2Z"));
    v.push_back(G::XS("M17.2 6.8Q18.8 7.8 20.4 7.2M22 8.2Q24.5 10.2 27 10.4M38 18.5Q42 22 46 21.5", 1.3f));
  }
  // грудь и загривок острыми прядями
  {
    Strands st;
    for (int i = 0; i < 3; i++) st.add({float(51 + i * 4), float(24 + i * 8)}, {float(61 + i * 4), float(33 + i * 8)}, 6.6, 0.18, -0.22);
    for (int i = 0; i < 4; i++) st.add({float(39 + i * 1.6), float(28 + i * 6)}, {float(30 + i * 3.1), float(41 + i * 6)}, 7.4, -0.2, 0.26);
    v.push_back(FU(st.fill));
    v.push_back(XU(st.veins));
  }
  // ближняя передняя лапа и задняя лапа
  layerFront(v, {limb({{41, 56}, {39, 76}, {39.5, 90.5}}, {10.5, 6, 5.4}), lionPaw({39.5, 91.5}, 178, 0.8, -1, 3),
                 limb({{66, 84}, {58, 90}, {52.5, 92}}, {11, 6.4, 5.2}), lionPaw({51.5, 92.6}, 182, 0.8, -1, 3)});
  v.push_back(G::XS("M64 70Q69 64 76 64.5M76 72Q81 77 80 85", 1.3f));
  return moved(v, Affine::translate(-3.5f, 0));
}

// Бык: голова анфас — рога полумесяцами с кольцами, уши, чуб языками, гневные глаза, морда с ноздрями и кольцом.
std::vector<L> bullGlyph() {
  std::vector<L> v;
  const std::string horn = sweep({38, 29}, {22, 31}, {8, 26}, {13, 3}, [](double t) { return 12 * std::pow(1 - t, 0.8) + 0.2; });
  v.push_back(FU(sym(horn)));
  v.push_back(G::XS(sym("M28.5 24.5Q30.5 29.5 29 34.5M23 23Q25 28 23.5 32.5"), 1.4f));
  layerFront(v, {sym(blade({36, 38}, {9, 42}, 9, 0.06, 0.1))});
  v.push_back(XU(sym(vein({33, 39}, {26, 39.6}, {19, 40.4}, {13, 41.2}, 1.6))));
  const std::string face = "M50 24C59 24 66 27 68 32L66.5 46C65.5 54 62.5 60 61.5 66 61.5 70 64 74 64.5 78 64.5 88 58.5 94 50 94 41.5 94 35.5 88 35.5 78"
                           " 36 74 38.5 70 38.5 66 37.5 60 34.5 54 33.5 46L32 32C34 27 41 24 50 24Z";
  layerFront(v, {face});
  std::vector<std::string> tuft;
  for (const auto& [b, t] :
       std::vector<std::pair<Pt, Pt>>{{{50, 22}, {50, 37}}, {{45, 23}, {41.5, 35}}, {{55, 23}, {58.5, 35}}, {{41, 25}, {35.5, 33}}, {{59, 25}, {64.5, 33}}})
    tuft.push_back(tongue(b, t, 6.4, b.x < 50 ? 0.12 : b.x > 50 ? -0.12 : 0.08, b.x < 50 ? -0.12 : b.x > 50 ? 0.12 : -0.1));
  layeredFeathers(v, tuft, 1.5f, false);
  v.push_back(XU(sym("M36.5 44.5 45.5 46.5 43.4 49.6 38 48Z") + sym(xf("M0 -3.6C2 -2 2.2 1.6 0 3.6-1.4 1.2-1.4-1.4 0-3.6Z", place(43, 82.5, 20)))));
  v.push_back(G::XS(sym("M35 41Q40.5 40 47 44M40.5 53Q43.5 61 44 70M43 76Q46.5 74.6 50 75") + "M50 42V66", 1.4f));
  v.push_back(G::XS(circ(50, 92, 5.4), 6));
  v.push_back(G::S(circ(50, 92, 5.4), 2.6f));
  return v;
}

// Конь: голова на изогнутой шее, вырванный срез, грива языками пламени, чёлка, два уха, ноздря и глаз.
std::vector<L> horseGlyph() {
  std::vector<L> v;
  // голова и шея с рваным срезом
  const std::string neck = "M42 13C56 15 70 26 78 42 84 54 86 68 86.5 82L88 95 82 90.5 77 96 71.5 91 66 96.5 60.5 91.5 55 96 49.5 91 45 95C47.5 84 47 72 46 62"
                           " 45 56 44 52 41 49 38 46 33 47 30 51L20 63C17 66.5 12.5 67 10 64.5 8 62.5 8 59 9.5 56 13 47 17 36 22 28 26 22 30 18 34 15.5Z";
  // грива: пряди вдоль гребня шеи, назад и вниз
  Strands mane;
  for (int i = 0; i < 9; i++) {
    const double t = 0.04 + i * 0.105;
    const CurvePt q = bezier({46, 12}, {63, 15}, {78, 33}, {85, 70}, t);
    const Pt b{float(q.x + q.nx * 3), float(q.y + q.ny * 3)};
    const double dir = std::atan2(-q.ny, -q.nx) * 180 / kPi + 48;
    mane.add(b, at(b, dir, 15 + (i % 2) * 3), 9, 0.18, -0.24, 1.5);
  }
  v.push_back(FU(mane.fill));
  layerFront(v, {neck});
  v.push_back(XU(mane.veins));
  // уши и чёлка
  layerFront(v, {tongue({42, 15}, {46.5, 2}, 6, -0.12, 0.08, 1.1), tongue({38, 16.5}, {37.5, 3.5}, 5.5, -0.1, 0.06, 1.1)}, 1.6f);
  {
    Strands fl;
    fl.add({40, 15}, {27, 25}, 7, -0.18, 0.2);
    fl.add({43, 16}, {33, 29}, 6.2, -0.16, 0.2);
    layerFront(v, {fl.fill}, 1.5f);
    v.push_back(XU(fl.veins));
  }
  // глаз, ноздря, рот, скула, мускулы шеи
  v.push_back(XU("M24.6 31.6 31 28.4 30 32.2Z" + xf("M0 -3.4C2 -2 2.2 1.6 0 3.4-1.4 1.2-1.4-1.4 0-3.4Z", place(13.2, 57.6, 30))));
  v.push_back(G::XS("M10.8 63Q14 63.4 17.4 61.6M31 36Q40 38.5 40.5 47M18.5 39Q21.5 45 19 52M47 66Q56 74 61 90M56 40Q62 52 63 64", 1.4f));
  return v;
}

// Змей: тело S-изгибами вверх, брюшные щитки шевронами, голова с клыками и раздвоенным языком, хвост остриём.
std::vector<L> serpentGlyph() {
  std::vector<L> v;
  const std::vector<std::array<Pt, 4>> segs = {{Pt(30, 22), Pt(42, 26), Pt(62, 20), Pt(72, 30)},  {Pt(72, 30), Pt(84, 42), Pt(70, 54), Pt(52, 52)},
                                               {Pt(52, 52), Pt(34, 50), Pt(18, 60), Pt(26, 74)}, {Pt(26, 74), Pt(34, 88), Pt(60, 92), Pt(74, 84)},
                                               {Pt(74, 84), Pt(86, 77), Pt(90, 64), Pt(82, 60)}};
  const double w[] = {11, 12, 12.5, 11, 8, 0.5};
  std::string body, plates;
  for (size_t i = 0; i < segs.size(); i++) {
    const auto& s = segs[i];
    const double w0 = w[i], w1 = w[i + 1];
    body += sweep(s[0], s[1], s[2], s[3], [&](double t) { return w0 + (w1 - w0) * t; }) + (i + 1 < segs.size() ? circ(s[3].x, s[3].y, w1 * 0.5) : "");
    for (int k = 0; k < 4; k++) {
      const double t = (k + 0.5) / 4, ww = (w0 + (w1 - w0) * t) * 0.5;
      if (ww < 2.2) continue;
      const CurvePt q = bezier(s[0], s[1], s[2], s[3], t);
      const double dx = q.ny, dy = -q.nx;
      plates += "M" + ptS({float(q.x + q.nx * ww * 0.7 - dx * 1.6), float(q.y + q.ny * ww * 0.7 - dy * 1.6)}) + "L" +
                ptS({float(q.x + dx * 1.4), float(q.y + dy * 1.4)}) + "L" +
                ptS({float(q.x - q.nx * ww * 0.7 - dx * 1.6), float(q.y - q.ny * ww * 0.7 - dy * 1.6)});
    }
  }
  v.push_back(FU(body));
  v.push_back(G::XS(plates, 1.3f));
  const std::string head =
      "M37 12C31 8.5 22 8.5 15 11.5 11 13 8.5 15.5 9 18L20 20.5 9.5 23.5C9.5 26.5 13 28.5 18 28.5L29 28C35 27.5 39.5 23.5 39.5 18.5 39.5 15.5 38.5 13.5 37 12Z";
  layerFront(v, {head});
  const std::string tng = sweep({18, 21.8}, {13, 22}, {8, 22}, {4, 22}, [](double t) { return 1.8 - 0.4 * t; }, 12) +
                          tongue({4.4, 22}, {0.5, 18.6}, 1.7, 0, 0) + tongue({4.4, 22}, {0.5, 25.4}, 1.7, 0, 0);
  v.push_back(FU("M11.4 18.4 13.4 18.8 12.6 22.2ZM15.4 19.2 17.2 19.6 16.4 22.4Z" + tng));
  v.push_back(XU("M22.5 14.6 29 12.6 27.6 15.8Z"));
  v.push_back(G::XS("M20 12Q26 10 32 12.4M30.5 18Q34 21 33 25", 1.3f));
  return moved(v, Affine::translate(3, -1));
}

// Кракен: мантия-клин с гребнем, грозные глаза-щели, восемь щупалец завитками с присосками и острыми концами.
std::vector<L> krakenGlyph() {
  std::vector<L> v;
  const auto arm = [&](const std::vector<std::array<Pt, 4>>& segs, const std::vector<double>& w, double side) {
    std::string body, suckers;
    for (size_t i = 0; i < segs.size(); i++) {
      const auto& s = segs[i];
      const double w0 = w[i], w1 = w[i + 1];
      body += sweep(s[0], s[1], s[2], s[3], [&](double t) { return w0 + (w1 - w0) * t; }) + (i + 1 < segs.size() ? circ(s[3].x, s[3].y, w1 * 0.5) : "");
      for (double t : {0.25, 0.6, 0.9}) {
        const double ww = w0 + (w1 - w0) * t;
        if (ww < 3.6) continue;
        const CurvePt q = bezier(s[0], s[1], s[2], s[3], t);
        suckers += circ(q.x + q.nx * side * ww * 0.18, q.y + q.ny * side * ww * 0.18, ww * 0.16);
      }
    }
    v.push_back(G::XS(sym(body), 1.8f));
    v.push_back(FU(sym(body)));
    v.push_back(XU(sym(suckers)));
  };
  arm({{Pt(42, 47), Pt(30, 49), Pt(15, 46), Pt(9, 35)}, {Pt(9, 35), Pt(5, 26), Pt(12, 19), Pt(17, 23.5)}, {Pt(17, 23.5), Pt(20.5, 27), Pt(17, 31), Pt(14, 28)}},
      {9, 6, 3, 0.3}, -1);
  arm({{Pt(42, 53), Pt(28, 59), Pt(14, 67), Pt(13, 80)},
       {Pt(13, 80), Pt(12, 89), Pt(22, 92), Pt(25, 86)},
       {Pt(25, 86), Pt(27, 82), Pt(22.5, 79), Pt(20.5, 82.5)}},
      {9.5, 6.5, 3, 0.3}, -1);
  arm({{Pt(44, 51), Pt(39, 64), Pt(30, 77), Pt(32, 88)},
       {Pt(32, 88), Pt(33, 96), Pt(42, 96.5), Pt(41.5, 90.5)},
       {Pt(41.5, 90.5), Pt(41, 87.5), Pt(37.5, 87.5), Pt(37.5, 90.5)}},
      {9, 6, 3, 0.3}, -1);
  arm({{Pt(47.5, 50), Pt(47.5, 66), Pt(45, 77), Pt(45.5, 86)}, {Pt(45.5, 86), Pt(46, 92), Pt(40, 94), Pt(39.5, 89)}}, {8, 4.6, 0.3}, 1);
  const std::string mantle = "M50 2C58 9 69 18 71.5 30 73 39 67.5 47.5 60 51.5 56.5 53.3 53 54 50 54S43.5 53.3 40 51.5C32.5 47.5 27 39 28.5 30 31 18 42 9 50 2Z";
  layerFront(v, {mantle}, 2.2f);
  v.push_back(XU(sym(vein({48.6, 9}, {44, 16}, {40.5, 24}, {39, 33}, 1.6) + vein({49.2, 6}, {48, 14}, {47, 22}, {46.8, 30}, 1.4)) +
                 vein({50, 5}, {50, 13}, {50, 21}, {50, 29}, 1.5)));
  // глаза по бокам мантии: миндалины с поперечным зрачком-щелью
  v.push_back(XU(sym("M30.5 38.2C34.5 36.6 40 37.4 44 41.8 39 44.2 33.6 43 30.5 38.2Z")));
  v.push_back(FU(sym(xf(rrect(-3.4, -0.9, 6.8, 1.8, 0.9), place(37.4, 40.4, 14)))));
  return v;
}

// Руна: вязаная руна резцами с гранёными концами в двойном кольце с насечками.
std::vector<L> runeGlyph() {
  std::vector<L> v;
  std::string notches;
  for (int i = 0; i < 24; i++) {
    const double a = -90 + 15 * i + 7.5;
    notches += "M" + pt(50, 50, 42.6, a) + "L" + pt(50, 50, 46.4, a);
  }
  v.push_back(G::S(circ(50, 50, 44.5), 7.4f));
  v.push_back(G::XS(notches, 1.3f));
  v.push_back(G::XS(circ(50, 50, 39.2), 1.3f));
  v.push_back(G::S(circ(50, 50, 37.2), 2.2f));
  const std::string staves = stave({50, 17}, {50, 83}, 7.6) + stave({50, 43}, {32.5, 25.5}, 7) + stave({50, 43}, {67.5, 25.5}, 7) +
                             stave({50, 57}, {33.5, 73.5}, 7) + stave({50, 57}, {66.5, 73.5}, 7);
  v.push_back(G::XS(staves, 3.4f));
  v.push_back(FU(staves));
  v.push_back(XU(vein({50, 21}, {50, 38}, {50, 62}, {50, 79}, 1.4)));
  return v;
}

// Роза тюдоровская: наружные лепестки с выемкой, острые чашелистики между ними, внутренние лепестки, семенная
// коробочка с семенами.
std::vector<L> roseGlyph() {
  std::vector<L> v;
  std::string barbs, outer, inner, notch, veins, seeds;
  for (int i = 0; i < 5; i++) {
    const double a = -90 + 72 * i, ai = a + 36;
    barbs += blade(polar(50, 50, 24, ai), polar(50, 50, 49, ai), 9.5, 0, 0);
    const Pt pc = polar(50, 50, 25, a);
    outer += circ(pc.x, pc.y, 18.6);
    notch += "M" + pt(50, 50, 45.5, a - 8) + "Q" + pt(50, 50, 41, a) + " " + pt(50, 50, 45.5, a + 8) + "L" + pt(50, 50, 48, a) + "Z";
    veins += vein(polar(50, 50, 21, a + 22), polar(50, 50, 28, a + 20), polar(50, 50, 34, a + 16), polar(50, 50, 38.5, a + 10), 1.5) +
             vein(polar(50, 50, 21, a - 22), polar(50, 50, 28, a - 20), polar(50, 50, 34, a - 16), polar(50, 50, 38.5, a - 10), 1.5);
    const Pt ic = polar(50, 50, 11.5, ai);
    inner += circ(ic.x, ic.y, 10);
    seeds += circ(50 + 4.6 * std::cos(ai * kPi / 180), 50 + 4.6 * std::sin(ai * kPi / 180), 1.05);
  }
  v.push_back(FU(barbs));
  v.push_back(G::XS(outer + circ(50, 50, 26), 2.2f));
  v.push_back(FU(outer + circ(50, 50, 26)));
  v.push_back(XU(notch + veins));
  v.push_back(G::XS(inner, 2.4f));
  v.push_back(FU(inner));
  v.push_back(XU(circ(50, 50, 8.4)));
  v.push_back(FU(circ(50, 50, 6.4)));
  v.push_back(XU(seeds + circ(50, 50, 1.2)));
  return v;
}

// Грифон восстающий: орлиная голова с ухом и раскрытым клювом, перья шеи языками, поднятое крыло, передние лапы
// орлиные, задние и хвост — львиные.
std::vector<L> griffinGlyph() {
  std::vector<L> v;
  // крыло за спиной: плечевая дуга вверх-вправо, маховые свисают наружу, кроющие рядом
  {
    const Pt s0{55, 40}, s1{62, 27}, s2{73, 13}, s3{90, 3};
    const Pt t0{97, 8}, t1{101, 30}, t2{94, 47}, t3{73, 56};
    std::vector<std::string> prim, cov;
    const int cnt = 8;
    for (int k = 0; k < cnt; k++) {
      const double u = double(k) / (cnt - 1);
      const CurvePt q = bezier(s0, s1, s2, s3, 0.95 - u * 0.82), e = bezier(t0, t1, t2, t3, u);
      const Pt b{float(q.x), float(q.y)}, tip{float(e.x), float(e.y)};
      const double th = angleOf(b, tip);
      prim.push_back(blade(at(b, th, -3), tip, 8, -0.02, -0.06));
      if (k % 2 == 1 || k == cnt - 1) cov.push_back(blade(at(b, th, -2), at(b, th, 11), 7.6, -0.03, -0.05));
    }
    layeredFeathers(v, prim, 1.7f, false);
    layeredFeathers(v, cov, 1.6f, false);
    layerFront(v, {sweep(s0, s1, s2, s3, [](double t) { return 10 * std::pow(1 - t, 0.55); })}, 1.8f);
  }
  // хвост низко, кисть языками
  fills(v, {sweep({70, 70}, {82, 84}, {96, 84}, {95, 72}, [](double t) { return 4.4 - 1.2 * t; }), tongue({95, 73.5}, {98, 59}, 5.5, 0.2, -0.12),
            tongue({94.5, 73.5}, {87.5, 61}, 5, -0.22, 0.12), tongue({95.5, 74}, {100, 66}, 4.6, -0.22, 0.1)});
  // дальние лапы: опорная задняя львиная и нижняя передняя орлиная
  fills(v, {limb({{67, 70}, {76, 85}, {67, 92}}, {13, 7, 5.6}), lionPaw({64.5, 92.5}, 182, 1, -1), limb({{44, 47}, {35, 56}, {24, 57}}, {10, 5.6, 4.4}),
            talon({22, 57}, 175, 0.95, -1)});
  // туловище
  layerFront(v, {sweep({44, 36}, {46, 51}, {52, 61}, {63, 69}, [](double t) { const double u = 1 - t; return 26 * u * u + 2 * 2 * u * t + 22 * t * t; }) +
                 circ(45, 40, 11)});
  // перья шеи: острые языки назад и вниз
  {
    const std::string mass = xf(ell(0, 0, 11, 15), place(45, 25, 12));
    layerFront(v, {mass}, 1.8f);
    std::vector<std::string> ls;
    for (int i = 0; i < 11; i++) {
      const double a = 118 - i * 20;
      const Pt b = polar(45, 25, 12.5, a);
      const double rad = a * kPi / 180, fl = 60 * kPi / 180;
      const double dir = std::atan2(std::sin(rad) * 0.5 + std::sin(fl) * 0.5, std::cos(rad) * 0.5 + std::cos(fl) * 0.5) * 180 / kPi;
      ls.push_back(tongue(at(b, dir, -5), at(b, dir, (i % 2 ? 10 : 12.5) * (1 + 0.3 * std::max(0.0, std::sin(rad)))), 7.4, 0.1, -0.14, 1.25));
    }
    layeredFeathers(v, ls, 1.6f, false);
  }
  // голова орла: ухо, нависшая бровь, крючковатый клюв раскрыт, язык
  {
    const std::string ear = tongue({42, 8.5}, {48, 0.8}, 5.4, -0.12, 0.1, 1.1);
    const std::string head = "M31 8.5C34 5 37.5 3 41.5 3.2 45.5 3.5 48 7 48 11.5 48 16.5 46 21 42.5 23.5 39.5 25.5 36 25.8 33.5 24L31 21 23 19.6 31 16.5"
                             "C28 15 24 14.6 21 15.4 19.6 15.8 18.4 16.6 17.3 17.8 17.2 13.6 18.8 10.2 22 8.6 24.6 7.4 28 7.4 31 8.5Z";
    layerFront(v, {ear, head});
    layerFront(v, {tongue({29.5, 18}, {15.5, 22.5}, 2.8, 0.08, -0.24)}, 1.3f);
    v.push_back(XU("M33.4 11.8 39.2 9.6 38 12.8Z"));
    v.push_back(G::XS("M31 9.8Q35 7.2 40.6 8.2M26 9.2Q24.6 11 24.8 13.4", 1.3f));
  }
  // ближние лапы: поднятая передняя орлиная и выставленная задняя львиная
  layerFront(v, {limb({{46, 40}, {34, 46}, {25, 37}}, {12.5, 6.4, 4.6}), tongue({35, 47}, {40, 54.5}, 4.4, 0.2, -0.16), talon({24, 35.5}, -130, 1, -1)});
  layerFront(v, {xf(ell(0, 0, 12.5, 10.5), place(63, 70, -40)), limb({{59, 74}, {48.5, 85}, {37.5, 82.5}}, {11.5, 6.6, 5.4}),
                 tongue({49.5, 86.5}, {56.5, 91.5}, 4.4, -0.2, 0.16), lionPaw({35.5, 81.8}, 200, 1, -1)});
  v.push_back(G::XS("M49 53Q52.5 58 53 64M46.5 57Q49 61 49.5 66M56 63Q62 60 68 63", 1.4f));
  return moved(v, Affine::translate(-1.5f, 0));
}

// Медведь шагающий: горб на загривке, тяжёлые лапы с когтями, морда клином, бахрома шерсти языками по брюху.
std::vector<L> bearGlyph() {
  std::vector<L> v;
  // дальние лапы
  fills(v, {limb({{46, 60}, {44, 75}, {41, 87.5}}, {13, 10, 8.6}), lionPaw({40, 89}, 178, 1.15, -1), limb({{82, 58}, {84.5, 73}, {80, 87.5}}, {14, 10, 8.6}),
            lionPaw({79, 89}, 178, 1.15, -1)});
  // туловище с горбом на загривке, шерсть бахромой по брюху и загривку
  {
    const std::string body = "M30 40C33 28 41 18.5 52 17.5 60 17 66 20.5 72 21.5 82 23 90 30 93.5 40 96.5 49 95 58 90 64 85 69 76 71 66 71L42 71"
                             "C35 70 30 64 28.5 56 27.5 50 28 45 30 40Z";
    Strands fur;
    for (int i = 0; i < 7; i++)
      fur.add({float(38 + i * 7), float(67.5 - std::sin(i * 0.5) * 1.2)}, {float(34.5 + i * 7), float(77.5 - std::sin(i * 0.5) * 1.2)}, 6.4, -0.12, 0.16, 1.3);
    for (int i = 0; i < 3; i++) fur.add({float(40 + i * 6), float(22 - i * 2)}, {float(50 + i * 6), float(18 - i * 2.5)}, 5.6, 0.14, -0.16, 1.2);
    layerFront(v, {body});
    v.push_back(FU(fur.fill));
    v.push_back(XU(fur.veins));
    v.push_back(G::XS("M58 28Q57 40 59 50M86 34Q91 44 88 56", 1.3f));
  }
  // голова: морда клином вниз-влево, маленькое ухо, глаз-щель, нос
  {
    const std::string head = "M35 34C30 30 23 29.5 18 32L8.5 38.5C5.5 40.5 4 44 5.5 47 7 49.5 10 50.5 13.5 50L22.5 49C29.5 48.5 35.5 45.5 38 40.5Z";
    const std::string ear = "M27.5 32C26.5 28 29 25.5 32.5 26 34.5 28 34 31.5 31.5 33Z";
    layerFront(v, {ear, head});
    v.push_back(XU("M19 36.8 25.4 34.4 24.4 37.8Z" + std::string("M4.8 43.2C5.6 41.2 8 40.8 9.2 42.2 9.2 44.2 6.8 45.2 4.8 43.2Z")));
    v.push_back(G::XS("M7.6 47.6Q11.5 48.4 15.5 47.4M14 35.5Q18.5 33 24 33.4M32 36.5Q34 41 32 45.5", 1.3f));
  }
  // ближние лапы: передняя шагает, задняя с бедром
  layerFront(v, {limb({{38, 54}, {33, 72}, {28, 87.5}}, {15.5, 11, 9.4}), lionPaw({26.6, 89}, 178, 1.2, -1)});
  layerFront(v, {xf(ell(0, 0, 14, 16), place(75, 52, 15)), limb({{74, 62}, {70, 76}, {67, 87.5}}, {14, 10, 9.2}), lionPaw({65.6, 89}, 178, 1.2, -1)});
  v.push_back(G::XS("M26 64Q30 66 33.5 64.6M66 50Q71 44 79 44.5", 1.3f));
  return moved(v, Affine::translate(0, -3));
}

// Единорог восстающий: конская голова с витым рогом и бородкой, грива языками, ноги с раздвоенными копытами
// и щёточками, львиный хвост с кистью.
std::vector<L> unicornGlyph() {
  std::vector<L> v;
  // хвост с кистью
  fills(v,
        {sweep({71, 66}, {84, 75}, {97, 60}, {88, 46}, [](double t) { return 4.2 - 1.1 * t; }),
         sweep({88, 46}, {81, 37}, {80, 27}, {86, 19}, [](double t) { return 3.2 - 0.9 * t; }) + circ(88, 46, 1.6),
         tongue({86, 21.5}, {97.5, 8.5}, 6, 0.22, -0.12), tongue({85.5, 21.5}, {82.5, 3}, 5.6, -0.24, 0.14), tongue({87, 21.5}, {99, 26}, 5.2, -0.26, 0.12)});
  // дальние ноги: опорная задняя и нижняя передняя
  fills(v, {limb({{67, 70}, {76, 85}, {67.5, 92.4}}, {12, 6, 4.6}), hoofTuft(64.5, 92.5, 80, 0.85, -1),
            limb({{44, 47}, {35, 56}, {29.5, 60}, {29, 67}}, {9, 5.4, 4.6, 4.4}), hoofTuft(29, 69.5, 5, 0.85, 1)});
  // туловище
  layerFront(v, {sweep({44, 36}, {46, 51}, {52, 61}, {63, 69}, [](double t) { const double u = 1 - t; return 24 * u * u + 2 * 3 * u * t + 21 * t * t; }) +
                 circ(45, 40, 10)});
  // шея и грива прядями
  {
    Strands mane;
    for (int i = 0; i < 7; i++) {
      const double t = 0.04 + i * 0.15;
      const CurvePt q = bezier({42, 8}, {52, 12}, {57, 24}, {58, 42}, t);
      const Pt b{float(q.x + q.nx * 2), float(q.y + q.ny * 2)};
      const double dir = std::atan2(-q.ny, -q.nx) * 180 / kPi + 45;
      mane.add(b, at(b, dir, 13 + (i % 2) * 2.5), 8.6, 0.18, -0.24, 1.4);
    }
    v.push_back(FU(mane.fill));
    layerFront(v, {sweep({38, 20}, {44, 26}, {46, 32}, {46, 42}, [](double t) { return 16 + 6 * t; })});
    v.push_back(XU(mane.veins));
  }
  // голова: витой рог, ухо, бородка
  {
    const std::string head = "M45 8C41 5 36 5.5 33 8.5L23 20C21 22.6 20.2 25 20.8 27.4L22 30.4C23.4 32 25.8 32.2 27.8 31.2L33.5 29C38.5 28 43.5 25 46.5 20"
                             "C48 15.5 47.5 11 45 8Z";
    const std::string horn = tongue({34.5, 9}, {16, 0.5}, 5, 0, 0, 0.9);
    const std::string ear = tongue({42, 8.5}, {46, 0.8}, 5, -0.1, 0.08, 1.1);
    const std::string beard = tongue({27, 31}, {23.5, 42}, 4.4, 0.16, -0.18);
    layerFront(v, {horn, ear, beard, head});
    std::string twist;
    for (int i = 1; i < 5; i++) {
      const Pt c = mix({34.5, 9}, {16, 0.5}, i * 0.19);
      twist += "M" + ptS(at(c, 115, 2.6 - i * 0.4)) + "L" + ptS(at(c, -45, 2.6 - i * 0.4));
    }
    v.push_back(G::XS(twist, 1.2f));
    v.push_back(XU("M31.2 13.4 37 11.6 35.6 14.8Z" + xf("M0 -2.6C1.6 -1.4 1.6 1.2 0 2.6-1.2 0.8-1.2-1 0-2.6Z", place(23.2, 25.4, 35))));
    v.push_back(G::XS("M21.6 29.4Q24 29.6 26.4 28.6M38 15Q41.5 20 39 26", 1.3f));
  }
  // ближние ноги: поднятая передняя и выставленная задняя
  layerFront(v, {limb({{46, 40}, {34, 45}, {28, 41}, {23.2, 36.6}}, {12, 6, 5, 4.6}), tongue({34.5, 46}, {38.5, 52.5}, 3.8, 0.2, -0.16),
                 hoofTuft(21.5, 35, -128, 0.85, 1)});
  layerFront(v, {xf(ell(0, 0, 12, 10), place(63, 70, -40)), limb({{59, 74}, {48.5, 85}, {41, 84}, {37.5, 83}}, {10.5, 5.8, 4.8, 4.6}),
                 hoofTuft(35, 82.5, 105, 0.85, -1)});
  v.push_back(G::XS("M49 53Q52.5 58 53 64M46.5 57Q49 61 49.5 66M56 63Q62 60 68 63", 1.4f));
  return moved(v, Affine::translate(-3, 0));
}

// Олень шагающий: ветвистые рога, поднятая передняя нога, стройные ноги с копытами, короткий хвост.
std::vector<L> stagGlyph() {
  std::vector<L> v;
  // рога: ствол вверх и назад, надглазничный и средние отростки вперёд-вверх, корона из двух острий
  const auto antler = [](const Affine& m) {
    const Pt p0{0, 0}, p1{2, -10}, p2{8, -18}, p3{18, -22};
    std::string d = sweep(p0, p1, p2, p3, [](double t) { return 4.4 - 2.6 * t; });
    const struct {
      double t;
      Pt tip;
    } tines[] = {{0.1, {-12, -7}}, {0.3, {-9, -16}}, {0.55, {-1, -23.5}}, {0.82, {9, -27}}};
    for (const auto& tn : tines) {
      const CurvePt q = bezier(p0, p1, p2, p3, tn.t);
      d += tongue({float(q.x), float(q.y)}, tn.tip, 3.6, -0.1, 0.06, 0.8);
    }
    d += tongue({17, -21.6}, {22, -19.5}, 2.6, 0, 0, 0.8);
    return xf(d, m);
  };
  const std::string nearA = antler(place(27.5, 27, 0, 1.0f)), farA = antler(place(33, 26, 0, 0.94f));
  v.push_back(FU(farA));
  // дальние ноги
  fills(v, {limb({{38, 62}, {38, 76}, {37.5, 88.5}}, {6.4, 4.4, 3.8}), hoof(37.5, 92, 0, 0.75), limb({{74, 60}, {79, 70}, {77, 80}, {76, 88.5}}, {8, 4.8, 4, 3.8}),
            hoof(76, 92, 0, 0.75)});
  // туловище, шея, хвост
  layerFront(v, {"M28 50C30 42 40 39.5 52 40 64 40.5 74 39.5 80.5 43.5 86.5 47.5 86.5 56 82.5 61 78.5 65.5 70 66 60 66L40 66C33 65 28 58 28 50Z",
                 sweep({35, 48}, {31, 42}, {28, 36}, {27, 30}, [](double t) { return 14 - 4.5 * t; }), tongue({84, 47}, {91, 39}, 5, -0.1, 0.1)});
  v.push_back(XU(vein({40, 64}, {50, 66}, {62, 66}, {72, 63}, 1.4)));
  // голова: морда влево, ухо
  layerFront(v, {"M30 25C26 23.5 21 24 17 27L10.5 32C8.5 33.5 8 36 9.5 37.5 11 39 13.5 39 15.5 38L24 35C28.5 33.5 31 30.5 30 25Z",
                 tongue({29, 27}, {40.5, 22.5}, 5.4, -0.08, 0.1, 1.1)},
             1.8f);
  v.push_back(XU("M18.6 30 24 28 23.2 31Z" + std::string("M9.4 34.6C10.4 33.6 11.8 33.8 12.2 35Z")));
  v.push_back(G::XS("M10.4 37.4Q12.8 37.8 15 36.8", 1.3f));
  layerFront(v, {nearA}, 1.6f);
  // ближние ноги: поднятая передняя и задняя с бедром
  layerFront(v, {limb({{37, 56}, {30, 66.5}, {23, 69}, {21.2, 78}}, {7.4, 4.6, 4, 3.6}), hoof(21.2, 81.5, -10, 0.7)}, 1.8f);
  layerFront(v, {xf(ell(0, 0, 9.5, 12), place(75, 53, 10)), limb({{74, 62}, {71, 72}, {67.5, 80}, {66.5, 88.5}}, {9, 5, 4, 3.8}), hoof(66.5, 92, 0, 0.75)}, 1.8f);
  v.push_back(G::XS("M70 50Q74 46 80 47M44 46Q47 50 46 56", 1.3f));
  return v;
}

// Сокол на ветви: сложенное крыло перьями-клинками, крючковатый клюв, «усы» под глазом, пёстрая грудь,
// лапы, охватившие ветвь.
std::vector<L> falconGlyph() {
  std::vector<L> v;
  // хвост и ветвь с листьями
  v.push_back(FU("M48 70 57 96H66L61 69Z" + sweep({4, 86}, {30, 83}, {62, 82}, {96, 79}, [](double t) { return 4.6 + 1.6 * t; }) +
                   blade({80, 80}, {92, 70}, 5.4, 0.06, 0.06) + blade({14, 84.5}, {4, 76}, 5, -0.06, -0.06) + blade({20, 84}, {14, 93}, 4.6, 0.06, 0.06)));
  v.push_back(G::XS("M55 92 59.5 75M61 93 61.5 74", 1.3f));
  // туловище и голова
  const std::string body =
      "M38 6.5C46 6.5 51.5 12 53.5 20 57.5 33 60 49 58.5 63 57.5 72 51.5 78 44.5 78 37 77 32.5 70 31.5 60 30.5 47 31 31 30.5 24 29.5 14 32 6.5 38 6.5Z";
  const std::string beak = "M31.5 10.5C25 10.5 21 14 20.5 19.5 22.5 18 25 17.8 27 18.4L30.5 21Z";
  layerFront(v, {body, beak});
  std::string spots;
  for (const Pt p : {Pt(36, 38), Pt(42, 36), Pt(34.5, 46), Pt(40.5, 44.5), Pt(46.5, 43), Pt(35, 54.5), Pt(41, 53), Pt(47, 51.5), Pt(37.5, 62.5), Pt(43.5, 61)})
    spots += "M" + n(p.x - 1.6) + " " + n(p.y) + "L" + n(p.x) + " " + n(p.y + 2.6) + "L" + n(p.x + 1.6) + " " + n(p.y);
  v.push_back(G::XS(spots, 1.3f));
  v.push_back(XU("M33.4 13.2 40.4 11.4 38.8 15Z" + vein({35.5, 17}, {35, 21}, {35.5, 25}, {37.5, 29}, 2.6)));
  v.push_back(G::XS("M24.4 14.2Q26.4 13.4 28.4 14.4M33 7.5Q38 9.5 41 12.5", 1.3f));
  // сложенное крыло
  {
    std::vector<std::string> fs;
    for (int i = 0; i < 5; i++) fs.push_back(blade({float(45 + i * 1.2), float(34 + i * 6)}, {float(58 + i * 0.8), float(66 + i * 5)}, 8, -0.04, -0.06));
    const std::string cov = "M43 25C51 24 56.5 30 58 39 59 46 58 52 56 56 50 54 45 49 42.5 42 40.5 36 40 29.5 43 25Z";
    layeredFeathers(v, fs, 1.7f, false);
    layerFront(v, {cov});
    std::string sc;
    for (const Pt p : {Pt(47, 31), Pt(52.5, 33), Pt(45.5, 38.5), Pt(51.5, 40.5), Pt(50, 47.5)})
      sc += "M" + n(p.x - 3) + " " + n(p.y) + "Q" + n(p.x) + " " + n(p.y + 4.2) + " " + n(p.x + 3) + " " + n(p.y);
    v.push_back(G::XS(sc, 1.3f));
  }
  // лапы, охватившие ветвь
  layerFront(v, {"M36 66C40 64 46 64 49 67L47.5 76C45 78 40 78 37.5 76Z", talon({42, 80}, 120, 0.85, -1)}, 1.8f);
  v.push_back(G::XS("M38.5 70 41 72.5M43 69.5 45.5 72M39.5 74.5 42 76.5", 1.2f));
  return v;
}

// Феникс: крылья языками пламени, хохолок, хвостовые перья-языки, костёр под ним.
std::vector<L> phoenixGlyph() {
  std::vector<L> v;
  // костёр
  {
    const std::string fire = tongue({50, 94}, {50, 69}, 16, 0.08, -0.12) + tongue({40, 94.5}, {28, 73}, 11, -0.12, 0.18) +
                             tongue({60, 94.5}, {72, 73}, 11, 0.12, -0.18) + tongue({31, 95}, {14, 79}, 8, -0.12, 0.2) +
                             tongue({69, 95}, {86, 79}, 8, 0.12, -0.2) + "M18 97.5H82L76 90.5H24Z";
    v.push_back(FU(fire));
  }
  // крылья: огненные перья с плечевой дуги
  {
    const Pt s0{42, 40}, s1{32, 36}, s2{19, 24}, s3{8, 9};
    const Pt t0{2, 14}, t1{-2, 36}, t2{6, 54}, t3{26, 64};
    std::vector<std::string> fs;
    const int cnt = 7;
    for (int k = 0; k < cnt; k++) {
      const double u = double(k) / (cnt - 1);
      const CurvePt q = bezier(s0, s1, s2, s3, 0.95 - u * 0.8), e = bezier(t0, t1, t2, t3, u);
      const Pt b{float(q.x), float(q.y)}, tip{float(e.x), float(e.y)};
      fs.push_back(tongue(at(b, angleOf(b, tip), -3), tip, 9, 0.16, -0.2));
    }
    layeredFeathers(v, fs, 1.7f, true);
    const std::string arm = sweep(s0, s1, s2, s3, [](double t) { return 10 * std::pow(1 - t, 0.55); });
    v.push_back(G::XS(sym(arm), 1.8f));
    v.push_back(FU(sym(arm)));
  }
  // хвостовые языки
  {
    std::vector<std::string> tail;
    for (double a : {-34.0, 34.0, -17.0, 17.0, 0.0})
      tail.push_back(tongue({50, 62}, at({50, 62}, 90 + a, 26 - std::fabs(a) * 0.1), 8, a < 0 ? -0.12 : 0.12, a < 0 ? 0.14 : -0.14));
    layeredFeathers(v, tail, 1.7f, false);
  }
  // тело, шея, голова с хохолком
  {
    const std::string body = "M50 24C56.5 26 60 34 60 44 60 54 56.5 62 50 68 43.5 62 40 54 40 44 40 34 43.5 26 50 24Z";
    const std::string crest =
        tongue({49, 9}, {58, 0.5}, 4.6, -0.15, 0.1) + tongue({51, 11}, {63, 6}, 4.2, -0.15, 0.1) + tongue({52, 14}, {64, 13}, 3.8, -0.15, 0.1);
    const std::string head =
        "M54 16C54 10.5 51 7 46.5 7 43.5 7 41 8.5 39.5 10.5 36.5 10.5 34 12.5 33.5 16.5 35 15 37 14.6 39 15.2L41.5 16.6C42.5 20 44 23.5 44.5 28L55.5 28C56 24 "
        "55.5 20 54 16Z";
    v.push_back(G::XS(body, 2.0f));
    v.push_back(G::XS(crest + head, 2.0f));
    fills(v, {body, crest, head});
    v.push_back(XU("M42.6 10.6 47.4 9.2 46.4 11.8Z"));
    std::string sc;
    for (const Pt p : {Pt(47, 38), Pt(53, 38), Pt(50, 44), Pt(47, 50), Pt(53, 50), Pt(50, 56)})
      sc += "M" + n(p.x - 2.4) + " " + n(p.y) + "C" + n(p.x - 2) + " " + n(p.y + 2) + " " + n(p.x - 1) + " " + n(p.y + 3) + " " + n(p.x) + " " + n(p.y + 4) + "C" +
            n(p.x + 1) + " " + n(p.y + 3) + " " + n(p.x + 2) + " " + n(p.y + 2) + " " + n(p.x + 2.4) + " " + n(p.y);
    v.push_back(G::XS(sc, 1.3f));
  }
  return v;
}

// Вепрь шагающий: щетинистый гребень зубцами, клык полумесяцем, пятачок, уши, тонкие ноги с копытами, хвост.
std::vector<L> boarGlyph() {
  std::vector<L> v;
  // дальние ноги и хвост с кисточкой
  fills(v, {limb({{44, 62}, {43, 75}, {41.5, 87}}, {8, 5, 4.4}), hoof(41.5, 90.5, 0, 0.8), limb({{82, 60}, {85, 72}, {83.5, 87}}, {9, 5, 4.4}),
            hoof(83.5, 90.5, 0, 0.8),
            sweep({87, 44}, {94, 40}, {97, 47}, {92, 50}, [](double t) { return 2.6 - 1 * t; }) + tongue({92, 50}, {88, 57}, 4, 0.12, -0.12)});
  // туловище с гребнем щетины по хребту
  {
    std::string bristles;
    for (int i = 0; i < 12; i++) {
      const double t = 0.04 + i * 0.08;
      const CurvePt q = bezier({30, 37}, {40, 22}, {66, 21}, {86, 36}, t);
      const Pt b{float(q.x), float(q.y)};
      bristles += tongue(at(b, 90, 3), at(b, std::atan2(-q.ny, -q.nx) * 180 / kPi + 40, 10.5 - std::fabs(t - 0.38) * 8), 5.2, 0.1, -0.08, 1.1);
    }
    layerFront(v, {bristles, "M26 46C28 36 36 28 48 26.5 60 25.5 72 28 80 34 87 39 90 48 89 56 88 63 84 67 78 68L40 70C32 68 27 58 26 46Z"});
    v.push_back(G::XS("M44 34Q48 36 50 40M56 31Q59 34 60 38M68 32Q70 35 70.5 39M38 64Q46 67 56 66", 1.3f));
  }
  // голова: рыло клином, пятачок, острое ухо, клык полумесяцем
  {
    const std::string head = "M36 37C29.5 34.5 21 36.5 15.5 40.5L7.5 46C5.2 47.5 4.2 50 4.5 52.5L5 58 9 58.5 18 61C25 63 32.5 61 37 55.5Z";
    const std::string ear = tongue({30.5, 38}, {36, 25}, 7, -0.12, 0.14, 1.1);
    layerFront(v, {ear, head});
    v.push_back(XU("M18.8 44.4 25.4 42.2 24.2 45.8Z" + std::string("M4.8 51.2C5.8 49.6 7.6 49.8 8 51.6Z")));
    v.push_back(G::XS("M5.6 55.2Q10 56.6 14 57M28 41Q31 47 30 53M8.5 47.5V58", 1.3f));
    layerFront(v, {sweep({21, 60}, {14, 63}, {7.5, 58}, {9.5, 46.5}, [](double t) { return 5 * (1 - t) + 0.2; })}, 1.6f);
  }
  // ближние ноги
  {
    const std::string leg = limb({{36, 58}, {33.5, 75}, {31.5, 87}}, {11, 5.6, 4.8}) + hoof(31.5, 90.5, 0, 0.85);
    v.push_back(G::XS(limb({{35, 66}, {33.5, 75}, {31.5, 87}}, {8.4, 5.6, 4.8}) + hoof(31.5, 90.5, 0, 0.85), 1.8f));
    v.push_back(FU(leg));
  }
  layerFront(v, {xf(ell(0, 0, 11, 13), place(75, 55, 10)), limb({{74, 62}, {73, 75}, {72.5, 87}}, {11, 5.8, 4.8}), hoof(72.5, 90.5, 0, 0.85)}, 1.8f);
  v.push_back(G::XS("M66 52Q70 46 77 45", 1.3f));
  return moved(v, Affine::translate(0, -4));
}

// Дельфин геральдический: тело дугой, лобастая голова с клювом, спинной плавник-парус с лучами, грудной
// плавник, раздвоенный хвост.
std::vector<L> dolphinGlyph() {
  std::vector<L> v;
  const std::vector<std::array<Pt, 4>> segs = {{Pt(30, 54), Pt(30, 36), Pt(45, 23), Pt(60, 23)}, {Pt(60, 23), Pt(74, 23), Pt(82, 37), Pt(80, 53)},
                                               {Pt(80, 53), Pt(78.5, 66), Pt(71, 74), Pt(69, 83)}};
  const double w[] = {22, 18, 12, 6};
  // спинной плавник-парус с лучами и раздвоенный хвост
  v.push_back(FU("M45 26C49 15 57 6.5 69 3 65 9.5 64 16 65.5 23.5Z" + blade({70, 84}, {96, 82}, 9, -0.12, -0.06) + blade({70, 86}, {88, 99}, 8, 0.06, 0.12)));
  v.push_back(G::XS("M51 18 58.5 8.5M56 20 63 9.5M60.5 22 65.5 12M77 90.6Q84 87.6 90.6 85.8M77 93Q82.5 95.5 87.6 97.4", 1.3f));
  {
    std::string body;
    for (size_t i = 0; i < 3; i++) {
      const double w0 = w[i], w1 = w[i + 1];
      body += sweep(segs[i][0], segs[i][1], segs[i][2], segs[i][3], [&](double t) { return w0 + (w1 - w0) * t; }) + circ(segs[i][3].x, segs[i][3].y, w1 * 0.5);
    }
    layerFront(v, {body});
    v.push_back(XU(vein({36, 52}, {37, 40}, {46, 31}, {57, 29.5}, 1.5) + vein({78, 58}, {76, 66}, {72, 73}, {70.5, 80}, 1.3)));
  }
  // голова
  layerFront(v, {"M40 44C36 37 28 35 21.5 38 16.5 40.5 13 45 11.5 49.5L3.5 53.5C2 54.5 2.2 56.6 4 57L12.5 58.5C15.5 63.5 21.5 66.5 28.5 66.5 35 66.5 39.5 62.5 "
                 "41.5 57.5Z"});
  v.push_back(XU("M18.6 46.2 25.6 44 24.4 47.6Z" + std::string("M4 55.4 15 55.6 20 58.4 13.5 57.2Z")));
  v.push_back(G::XS("M32 44Q35.5 51 33 59M17 41Q21 39.6 26 40.4", 1.4f));
  // грудной плавник
  layerFront(v, {blade({33, 62}, {44, 80}, 8.6, 0.06, 0.1)}, 1.8f);
  v.push_back(XU(vein({34.5, 65}, {37.5, 70}, {40, 74}, {42, 77.5}, 1.3)));
  return v;
}

// Опускная решётка: брус сверху, прутья с острыми зубьями, поперечины с заклёпками, цепи звеньями к кольцам.
std::vector<L> portcullisGlyph() {
  std::vector<L> v;
  std::string bars, rivets;
  for (double x : {21.0, 35.5, 50.0, 64.5, 79.0}) {
    bars += rrect(x - 2.6, 25, 5.2, 58, 0.8) + "M" + n(x - 2.6) + " 82.5H" + n(x + 2.6) + "L" + n(x) + " 96Z";
    for (double y : {38.0, 53.0, 68.0})
      rivets += "M" + n(x) + " " + n(y - 1.6) + "L" + n(x + 1.6) + " " + n(y) + "L" + n(x) + " " + n(y + 1.6) + "L" + n(x - 1.6) + " " + n(y) + "Z";
  }
  for (double y : {38.0, 53.0, 68.0}) bars += rrect(15, y - 2.5, 70, 5, 0.8);
  std::string links;
  for (int i = 0; i < 4; i++) {
    const Pt a = polar(17.5, 22, 4.2 + i * 4.6, -126);
    links += xf(ell(0, 0, 2.6, 1.5), place(a.x, a.y, i % 2 ? 54 : -36));
  }
  v.push_back(FU(rrect(12, 20, 76, 7.5, 1.2) + bars));
  v.push_back(XU(rivets + sym(circ(17.5, 23.75, 1.5))));
  v.push_back(G::XS("M14 23.75H82", 1.2f));
  v.push_back(G::S(sym(links), 1.4f));
  v.push_back(G::S(sym(circ(5.4, 5.6, 3.3)), 2.2f));
  return v;
}

// Охотничий рог: сужающийся изогнутый рог с мундштуком и раструбом, пояски, перевязь с пряжкой и кистями.
std::vector<L> hornGlyph() {
  std::vector<L> v;
  const Pt h0{9, 33}, h1{16, 79}, h2{60, 96}, h3{84, 50};
  const auto hornW = [](double t) { return 4.6 + 17 * t * t + 4 * t; };
  v.push_back(G::S("M17.6 57C12 33 30 11 50 11 68 11 76 27 72 46 70 56 68 64 66 71.5", 2.6f));
  v.push_back(FU(rrect(45, 7.5, 10, 7, 2) + tongue({48.5, 14}, {44.5, 26}, 3.4, 0.1, -0.1) + tongue({51.5, 14}, {55.5, 26}, 3.4, -0.1, 0.1)));
  v.push_back(XU(rrect(47.4, 9.4, 5.2, 3.2, 1)));
  const double endDeg = std::atan2(h3.y - h2.y, h3.x - h2.x) * 180 / kPi;
  const std::string horn = sweep(h0, h1, h2, h3, hornW) + xf(ell(0, 0, 4.6, 13.5), place(h3.x, h3.y, float(endDeg)));
  v.push_back(G::XS(horn, 2.2f));
  v.push_back(FU(horn));
  v.push_back(XU(xf(ell(0, 0, 2.3, 10.2), place(h3.x + 1.2f, h3.y - 1.2f, float(endDeg)))));
  std::string bands, vn;
  for (double t : {0.2, 0.48, 0.76}) {
    const CurvePt q = bezier(h0, h1, h2, h3, t);
    const double w = hornW(t) * 0.5 + 1.6;
    bands += "M" + ptS({float(q.x + q.nx * w), float(q.y + q.ny * w)}) + "L" + ptS({float(q.x - q.nx * w), float(q.y - q.ny * w)});
  }
  v.push_back(G::XS(bands, 6.4f));
  v.push_back(G::S(bands, 3.4f));
  {
    std::vector<Pt> pts;
    for (int i = 0; i <= 30; i++) {
      const double t = 0.06 + 0.86 * i / 30;
      const CurvePt q = bezier(h0, h1, h2, h3, t);
      const double w = hornW(t) * 0.5;
      pts.push_back({float(q.x - q.nx * w * 0.45), float(q.y - q.ny * w * 0.45)});
    }
    v.push_back(XU(ribbon(pts, [](double t) { return 1.6 * std::pow(std::sin(kPi * t), 0.5); })));
  }
  v.push_back(FU(xf("M-4 -2.6H4L5 0 4 2.6H-4L-5 0Z", place(h0.x, h0.y, 80))));
  return v;
}

// Эмблемы группы «Классические»: имя (хранится в мирах — не переименовывать), подпись, построение.
struct Entry {
  const char* name;
  const char* title;
  std::vector<L> (*glyph)();
};
const Entry kEmblems[] = {
    {"crown", "Корона", crownGlyph},
    {"tower", "Башня", towerGlyph},
    {"castle", "Замок", castleGlyph},
    {"star", "Звезда", starGlyph},
    {"sun", "Солнце", sunGlyph},
    {"moon", "Луна", moonGlyph},
    {"tree", "Дерево", treeGlyph},
    {"lily", "Лилия", lilyGlyph},
    {"sword", "Меч", swordGlyph},
    {"swords", "Скрещённые мечи", swordsGlyph},
    {"shield", "Щит", shieldGlyph},
    {"skull", "Череп", skullGlyph},
    {"anchor", "Якорь", anchorGlyph},
    {"ship", "Корабль", shipGlyph},
    {"wheat", "Колосья", wheatGlyph},
    {"gem", "Самоцвет", gemGlyph},
    {"hammer", "Молот", hammerGlyph},
    {"axe", "Топор", axeGlyph},
    {"bow", "Лук", bowGlyph},
    {"key", "Ключ", keyGlyph},
    {"eye", "Глаз", eyeGlyph},
    {"flame", "Пламя", flameGlyph},
    {"eagle", "Орёл", eagleGlyph},
    {"lion", "Лев", lionGlyph},
    {"dragon", "Дракон", dragonGlyph},
    {"wolf", "Волк", wolfGlyph},
    {"bull", "Бык", bullGlyph},
    {"horse", "Конь", horseGlyph},
    {"serpent", "Змей", serpentGlyph},
    {"kraken", "Кракен", krakenGlyph},
    {"rune", "Руна", runeGlyph},
    {"rose", "Роза", roseGlyph},
    {"griffin", "Грифон", griffinGlyph},
    {"bear", "Медведь", bearGlyph},
    {"unicorn", "Единорог", unicornGlyph},
    {"stag", "Олень", stagGlyph},
    {"falcon", "Сокол", falconGlyph},
    {"phoenix", "Феникс", phoenixGlyph},
    {"boar", "Вепрь", boarGlyph},
    {"dolphin", "Дельфин", dolphinGlyph},
    {"portcullis", "Решётка", portcullisGlyph},
    {"horn", "Охотничий рог", hornGlyph},
};

}  // namespace

void classicEmblems(Catalog& c) {
  c.group("Классические");
  for (const Entry& e : kEmblems) c.add(e.name, e.title, e.glyph());
}

}  // namespace rg::gfx::emb
