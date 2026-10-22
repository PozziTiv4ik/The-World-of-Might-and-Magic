// Regnum — фантазийные эмблемы каталога: рыцари, святые, демоны, культисты, магия, орки, ящеры, зверолюды. Сетка
// 100 × 100, стиль «гравированная геральдика»: одноцветный силуэт с изящными пропорциями, сужающимися штрихами и
// острыми окончаниями; детали — тонкие вырезы (внутренняя кайма вдоль края, прожилки, насечки). Симметричные силуэты
// описаны левой половиной.
#include <algorithm>
#include <functional>

#include "gfx/emblems_internal.h"

namespace rg::gfx::emb {

namespace {

// ---------------------------------------------------------------- общие помощники файла
using Pts = std::vector<Pt>;

// Замкнутая ломаная по точкам.
std::string polyPts(const std::vector<Pt>& p) {
  std::string s;
  for (size_t i = 0; i < p.size(); i++) s += (i ? "L" : "M") + ptS(p[i]);
  return s + "Z";
}
// Подконтуры пути ломаными (кривые спрямлены), без повторов соседних точек и замыкающей точки.
std::vector<Pts> flatAll(std::string_view d, float tol = 0.04f) {
  std::vector<Pts> out;
  svgPath(d).flatten(tol, [&](const std::vector<Pt>& p, bool) {
    Pts q;
    for (Pt a : p)
      if (q.empty() || std::hypot(a.x - q.back().x, a.y - q.back().y) > 1e-3f) q.push_back(a);
    while (q.size() > 2 && std::hypot(q.front().x - q.back().x, q.front().y - q.back().y) < 1e-3f) q.pop_back();
    if (q.size() >= 2) out.push_back(std::move(q));
  });
  return out;
}
Pts flat(std::string_view d) {
  std::vector<Pts> all = flatAll(d);
  return all.empty() ? Pts{} : all.front();
}

// Симметричный замкнутый контур: half — путь левой половины от оси x = 50 до оси (без замыкания).
Pts symPts(std::string_view half) {
  Pts p = flat(half);
  if (p.empty()) return p;
  p.front().x = p.back().x = 50;
  Pts out = p;
  for (size_t i = p.size() - 1; i-- > 1;) out.push_back({100 - p[i].x, p[i].y});
  return out;
}
std::string symPath(std::string_view half) { return polyPts(symPts(half)); }

// Смещение замкнутой ломаной внутрь на dist (отрицательное — наружу); острые входящие углы срезаются.
Pts offsetPts(const Pts& p, double dist, double limit = 3) {
  const size_t n = p.size();
  if (n < 3) return p;
  double area = 0;
  for (size_t i = 0; i < n; i++) area += double(p[i].x) * p[(i + 1) % n].y - double(p[(i + 1) % n].x) * p[i].y;
  const double sg = area > 0 ? 1 : -1;
  auto nrm = [&](Pt a, Pt b) {
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1e-9, std::hypot(dx, dy));
    return std::pair<double, double>{-dy / len * sg, dx / len * sg};
  };
  Pts out;
  for (size_t i = 0; i < n; i++) {
    const Pt a = p[(i + n - 1) % n], b = p[i], c = p[(i + 1) % n];
    const auto [n1x, n1y] = nrm(a, b);
    const auto [n2x, n2y] = nrm(b, c);
    double mx = n1x + n2x, my = n1y + n2y;
    const double ml = std::hypot(mx, my);
    const double turn = ((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x)) * sg;
    if (ml < 1e-6) {
      out.push_back({float(b.x + n1x * dist), float(b.y + n1y * dist)});
      continue;
    }
    mx /= ml;
    my /= ml;
    const double cosH = mx * n1x + my * n1y, k = dist / std::max(1e-3, cosH);
    if ((turn < 0) == (dist > 0) && std::fabs(k) > limit * std::fabs(dist)) {
      out.push_back({float(b.x + n1x * dist), float(b.y + n1y * dist)});
      out.push_back({float(b.x + n2x * dist), float(b.y + n2y * dist)});
    } else {
      out.push_back({float(b.x + mx * k), float(b.y + my * k)});
    }
  }
  return out;
}
// Внутренний контур всех подконтуров пути.
std::string inset(std::string_view d, double dist) {
  std::string s;
  for (const Pts& p : flatAll(d))
    if (p.size() >= 3) s += polyPts(offsetPts(p, dist));
  return s;
}
// Гравированная кайма: вырез линией толщины w на расстоянии gap от края внутрь.
L rim(std::string_view d, double gap = 2.2, double w = 1.5) { return G::XS(inset(d, gap + w * 0.5), float(w)); }

// Тонкий вырез заливкой: каждый отрезок пути — четырёхугольник ширины w. В отличие от выреза обводкой не
// утолщается подгонкой к пикселям на малых размерах (гравировка не съедает мелкий силуэт).
std::string slit(std::string_view d, double w) {
  std::string s;
  svgPath(d).flatten(0.05f, [&](const std::vector<Pt>& p, bool closed) {
    const size_t n = p.size();
    for (size_t i = 0; i + 1 < n + (closed ? 1 : 0); i++) {
      const Pt a = p[i], b = p[(i + 1) % n];
      const double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
      if (len < 1e-4) continue;
      const double ux = dx / len, uy = dy / len, nx = -uy * w * 0.5, ny = ux * w * 0.5, e = w * 0.3;
      const Pt a0{float(a.x - ux * e), float(a.y - uy * e)}, b0{float(b.x + ux * e), float(b.y + uy * e)};
      s += "M" + ptS({float(a0.x + nx), float(a0.y + ny)}) + "L" + ptS({float(b0.x + nx), float(b0.y + ny)}) + "L" +
           ptS({float(b0.x - nx), float(b0.y - ny)}) + "L" + ptS({float(a0.x - nx), float(a0.y - ny)}) + "Z";
    }
  });
  return s;
}
// Гравировка линией без утолщения на малых размерах.
L engr(std::string_view d, double w = 1.4) { return G::X(slit(d, w)); }
// Кайма-гравировка без утолщения на малых размерах.
L rimX(std::string_view d, double gap = 2.2, double w = 1.5) { return engr(inset(d, gap + w * 0.5), w); }

// Полоса вдоль кубической кривой с профилем ширины w(t) (t от 0 до 1): рога, когти, пальцы, языки пламени.
std::string band(Pt p0, Pt c1, Pt c2, Pt p1, const std::function<double(double)>& w, int steps = 40) {
  Pts lft, rgt;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, hw = std::max(0.0, w(t)) * 0.5;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    lft.push_back({float(q.x + q.nx * hw), float(q.y + q.ny * hw)});
    rgt.push_back({float(q.x - q.nx * hw), float(q.y - q.ny * hw)});
  }
  Pts all = lft;
  for (size_t i = rgt.size(); i-- > 0;) all.push_back(rgt[i]);
  return polyPts(all);
}
// Рог, коготь: от ширины w0 к острому кончику (выпуклый профиль).
std::string horn(Pt p0, Pt c1, Pt c2, Pt p1, double w0, double k = 1.3) {
  return band(p0, c1, c2, p1, [=](double t) { return w0 * (1 - std::pow(t, k)); });
}
// Палец, перо с готическим (стрельчатым) кончиком: ширина w0 → w1, последние cap доли — остриё.
std::string digit(Pt p0, Pt c1, Pt c2, Pt p1, double w0, double w1, double cap = 0.2) {
  return band(p0, c1, c2, p1, [=](double t) {
    if (t < 1 - cap) return w0 + (w1 - w0) * t / (1 - cap);
    const double u = (t - (1 - cap)) / cap;
    return w1 * (1 - u * u);
  });
}
// Язык пламени, лопасть: заострённый лист с утолщением ближе к основанию.
std::string flameTongue(Pt p0, Pt c1, Pt c2, Pt p1, double w, double base = 0.3) {
  return band(p0, c1, c2, p1, [=](double t) {
    const double a = t < base ? std::sin(t / base * kPi / 2) : 1 - std::pow((t - base) / (1 - base), 1.4);
    return std::max(0.0, w * a);
  });
}
// Заострённое перо от основания b к кончику t (bend — изгиб в долях длины, знак — сторона).
std::string quill(Pt b, Pt t, double w, double bend = 0) {
  const double dx = t.x - b.x, dy = t.y - b.y, nx = -dy, ny = dx;
  const Pt c1{float(b.x + dx * 0.35 + nx * bend), float(b.y + dy * 0.35 + ny * bend)};
  const Pt c2{float(b.x + dx * 0.7 + nx * bend), float(b.y + dy * 0.7 + ny * bend)};
  return band(b, c1, c2, t, [=](double u) { return u < 0.3 ? w * (0.55 + 0.45 * u / 0.3) : w * (1 - std::pow((u - 0.3) / 0.7, 1.7)); });
}
// Перья вдоль дуги плеча крыла (p0 → p1): основания равномерно от t0 до t1, кончики — по списку.
std::vector<std::string> wingQuills(Pt p0, Pt c1, Pt c2, Pt p1, const std::vector<Pt>& tips, double w, double t0 = 0.1, double t1 = 0.92,
                                    double bend = 0) {
  std::vector<std::string> out;
  for (size_t i = 0; i < tips.size(); i++) {
    const double t = tips.size() > 1 ? t0 + (t1 - t0) * double(i) / double(tips.size() - 1) : t0;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    out.push_back(quill({float(q.x), float(q.y)}, tips[i], w, bend));
  }
  return out;
}

// Геральдическое крыло: плечо p0 (корень) → p1 (конец крыла) и кончики маховых перьев по контуру — от конца
// крыла к корню. fill — силуэт с острыми кончиками перьев, cuts — линии раздела перьев и ряд кроющих.
struct Wing {
  std::string fill, cuts;
};
Wing heraldicWing(Pt p0, Pt c1, Pt c2, Pt p1, const std::vector<Pt>& tips, double depth = 0.3, double cutLen = 0.62, double covert = 9,
                  double bulge = 0.1) {
  const size_t n = tips.size();
  std::vector<Pt> base(n), notch(n > 1 ? n - 1 : 0);
  for (size_t i = 0; i < n; i++) {
    const double t = n > 1 ? 0.97 - 0.94 * double(i) / double(n - 1) : 0.5;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    base[i] = {float(q.x), float(q.y)};
  }
  auto mid = [](Pt a, Pt b) { return Pt{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}; };
  auto lerp = [](Pt a, Pt b, double k) { return Pt{float(a.x + (b.x - a.x) * k), float(a.y + (b.y - a.y) * k)}; };
  for (size_t i = 0; i + 1 < n; i++) notch[i] = lerp(mid(tips[i], tips[i + 1]), mid(base[i], base[i + 1]), depth);
  Wing w;
  w.fill = "M" + ptS(p0) + "C" + ptS(c1) + " " + ptS(c2) + " " + ptS(p1);
  auto edge = [&](Pt from, Pt to, size_t i) {
    // Выпуклый край пера: контрольная точка отодвинута от оси пера.
    const Pt m = mid(from, to), b = base[i], t = tips[i];
    const double ax = t.x - b.x, ay = t.y - b.y, al = std::max(1e-6, std::hypot(ax, ay));
    const double proj = ((m.x - b.x) * ax + (m.y - b.y) * ay) / (al * al);
    double ox = m.x - (b.x + ax * proj), oy = m.y - (b.y + ay * proj);
    const double ol = std::max(1e-6, std::hypot(ox, oy)), len = std::hypot(to.x - from.x, to.y - from.y);
    ox = ox / ol * bulge * len;
    oy = oy / ol * bulge * len;
    return "Q" + ptS({float(m.x + ox), float(m.y + oy)}) + " " + ptS(to);
  };
  for (size_t i = 0; i < n; i++) {
    const Pt from = i == 0 ? p1 : notch[i - 1];
    const Pt to = i + 1 == n ? p0 : notch[i];
    w.fill += edge(from, tips[i], i) + edge(tips[i], to, i);
  }
  w.fill += "Z";
  for (size_t i = 0; i + 1 < n; i++) w.cuts += "M" + ptS(notch[i]) + "L" + ptS(lerp(notch[i], mid(base[i], base[i + 1]), cutLen));
  if (covert > 0) {
    // Ряд кроющих: фестоны вдоль плеча на расстоянии covert в сторону перьев.
    const Pt tm = tips[n / 2];
    const int m = 7;
    std::string sc;
    for (int k = 0; k <= m; k++) {
      const double t = 0.07 + 0.83 * k / m;
      const CurvePt q = bezier(p0, c1, c2, p1, t);
      const double sd = (tm.x - q.x) * q.nx + (tm.y - q.y) * q.ny > 0 ? 1 : -1;
      const Pt a{float(q.x + q.nx * covert * sd), float(q.y + q.ny * covert * sd)};
      if (k == 0) {
        sc += "M" + ptS(a);
      } else {
        const CurvePt h = bezier(p0, c1, c2, p1, 0.07 + 0.83 * (k - 0.5) / m);
        const double r = covert + 3.2;
        sc += "Q" + ptS({float(h.x + h.nx * r * sd), float(h.y + h.ny * r * sd)}) + " " + ptS(a);
      }
    }
    w.cuts += sc;
  }
  return w;
}

// Сияние: count тонких лучей-клиньев от r0 (длинные rl и короткие rs поочерёдно) в секторе a0…a1 градусов,
// кончики не выходят за сетку.
std::string glory(double cx, double cy, double r0, double rl, double rs, double a0, double a1, int count, double w) {
  std::string s;
  for (int i = 0; i < count; i++) {
    const double a = count > 1 ? a0 + (a1 - a0) * i / (count - 1) : a0, t = a * kPi / 180;
    double r = i % 2 ? rs : rl;
    const double ex = std::cos(t), ey = std::sin(t);
    if (ex > 1e-3) r = std::min(r, (99.5 - cx) / ex);
    if (ex < -1e-3) r = std::min(r, (0.5 - cx) / ex);
    if (ey > 1e-3) r = std::min(r, (99.5 - cy) / ey);
    if (ey < -1e-3) r = std::min(r, (0.5 - cy) / ey);
    s += horn(polar(cx, cy, r0, a), polar(cx, cy, r0 + (r - r0) / 3, a), polar(cx, cy, r0 + (r - r0) * 2 / 3, a), polar(cx, cy, r, a), w, 1.2);
  }
  return s;
}

// Насечки поперёк полосы с профилем w(t): отрезки по нормали длиной w(t) + over.
std::string notches(Pt p0, Pt c1, Pt c2, Pt p1, const std::vector<double>& ts, const std::function<double(double)>& w, double over = 1) {
  std::string s;
  for (double t : ts) {
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    const double hw = w(t) * 0.5 + over;
    s += "M" + ptS({float(q.x - q.nx * hw), float(q.y - q.ny * hw)}) + "L" + ptS({float(q.x + q.nx * hw), float(q.y + q.ny * hw)});
  }
  return s;
}

// Лапчатый крест (pattée): плечи от ширины w0 у центра до w1 на концах, вогнутые стороны; notch — глубина
// выемки на концах (мальтийский крест).
std::string crossPattee(double cx, double cy, double r, double w0, double w1, double notch = 0, double rotDeg = 0) {
  std::string s;
  auto P = [&](double x, double y, int k) {
    const double a = (rotDeg + 90.0 * k) * kPi / 180, cs = std::cos(a), sn = std::sin(a);
    return ptS({float(cx + x * cs - y * sn), float(cy + x * sn + y * cs)});
  };
  for (int k = 0; k < 4; k++) {
    s += (k ? "L" : "M") + P(-w0 / 2, -w0 / 2, k);
    s += "C" + P(-w0 / 2, -r * 0.55, k) + " " + P(-w1 / 2 + w1 * 0.08, -r * 0.8, k) + " " + P(-w1 / 2, -r, k);
    if (notch > 0) s += "L" + P(0, -r + notch, k);
    s += "L" + P(w1 / 2, -r, k);
    s += "C" + P(w1 / 2 - w1 * 0.08, -r * 0.8, k) + " " + P(w0 / 2, -r * 0.55, k) + " " + P(w0 / 2, -w0 / 2, k);
  }
  return s + "Z";
}

// Ромб (гранёный камень, заклёпка) с центром (cx, cy).
std::string lozenge(double cx, double cy, double rx, double ry) {
  return "M" + n(cx) + " " + n(cy - ry) + "L" + n(cx + rx) + " " + n(cy) + "L" + n(cx) + " " + n(cy + ry) + "L" + n(cx - rx) + " " + n(cy) + "Z";
}

// Лист (плюмаж, перо, язык пламени) вдоль кубической кривой: ширина w·sin(πt)^k, у основания не меньше w0.
std::string leaf(Pt p0, Pt c1, Pt c2, Pt p1, double w, double w0 = 1, double k = 0.7, int steps = 30) {
  std::vector<Pt> lft, rgt;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps;
    const double hw = std::max(w0 * (1 - t), w * std::pow(std::sin(kPi * t), k)) * 0.5;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    lft.push_back({float(q.x + q.nx * hw), float(q.y + q.ny * hw)});
    rgt.push_back({float(q.x - q.nx * hw), float(q.y - q.ny * hw)});
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Короткие штрихи от кривой в сторону (side = ±1) под углом к касательной: бородки пера, чешуйки, пряди.
std::string barbs(Pt p0, Pt c1, Pt c2, Pt p1, double t0, double t1, int count, double len, double deg, int side) {
  std::string s;
  const double a = deg * kPi / 180;
  for (int i = 0; i < count; i++) {
    const double t = count > 1 ? t0 + (t1 - t0) * i / (count - 1) : t0;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    // Касательная (по ходу кривой) и нормаль.
    const double tx = q.ny, ty = -q.nx;
    const double dx = tx * std::cos(a) + q.nx * side * std::sin(a), dy = ty * std::cos(a) + q.ny * side * std::sin(a);
    s += "M" + ptS({float(q.x), float(q.y)}) + "L" + ptS({float(q.x + dx * len), float(q.y + dy * len)});
  }
  return s;
}

// Кривая как открытый путь (для обводок и вырезов линией).
std::string curve(Pt p0, Pt c1, Pt c2, Pt p1) { return "M" + ptS(p0) + "C" + ptS(c1) + " " + ptS(c2) + " " + ptS(p1); }

// ---------------------------------------------------------------- рыцари
void knights(Catalog& c) {
  c.group("Рыцари");
  // Топфхельм «сахарная голова»: стрельчатый купол, кайма, рёбро-накладка, налобная полоса, смотровые щели,
  // дыхательные прорези жалюзи, ромбы-заклёпки.
  {
    const std::string helm = symPath("M50 3.5C45.5 8 35 15 30 23C26.5 28.5 24.5 34 24.2 41L24 70C24 79 27.5 85.5 34.5 89.5L50 96.5");
    std::string louvres;
    for (int i = 0; i < 4; i++) {
      const float y = float(57 + 5.5 * i);
      louvres += leaf({32.5, y + 0.6f}, {36.5, y}, {40.5, y - 0.2f}, {44.5, y}, 2.1, 0.5, 0.5);
    }
    c.add("knight-helm", "Шлем рыцаря",
          {G::F(helm), rim(helm, 2.2, 1.5), G::XS("M47.6 8V91M52.4 8V91" + sym("M26.5 34C34 32.5 41 32.3 47.6 33.1"), 1.4),
           G::X(sym(leaf({27.5, 42.2}, {33, 41.2}, {40, 40.8}, {45.8, 40.8}, 3.8, 0.4, 0.4) + louvres)),
           G::X(lozenge(50, 21, 1.6, 2.4) + lozenge(50, 48, 1.6, 2.4) + lozenge(50, 79, 1.6, 2.4))});
  }
  // Шахматный конь: благородный профиль с выгнутой шеей, острое ухо, грива языками, глаз под надбровьем, ноздря,
  // скула; основание с выкружкой и пояском.
  {
    const std::string head = "M33 82C31 72 33.5 63 39.5 57C35 58.5 30 60 25 59.5C21 59 18 57.5 15.5 55.5C12.5 53.5 11 50.5 11.5 47.5"
                             "C12 44.5 14.5 42 18 40L29 27C32 22.5 35.5 18.5 39 16L42.5 2.5 47 12.5C49 12.2 51.5 12.3 54 12.8"
                             "C67 15 77 27 78.5 44C80 58 76 71 69 82Z";
    const Pt m0{52, 18.5}, m1{63, 21.5}, m2{71.5, 31}, m3{72.5, 45};
    const Pt n0{72.5, 45}, n1{73.5, 57}, n2{70.5, 68}, n3{64.5, 79};
    c.add("knight-chess", "Шахматный конь",
          {G::F(head + "M16 96.5C16 92.5 18.5 90.5 23 89.5C28 88.5 31 86.5 32 83H68C69 86.5 72 88.5 77 89.5C81.5 90.5 84 92.5 84 96.5Z"),
           G::XS("M30 83H70M21 91H79", 1.6),
           G::XS(curve(m0, m1, m2, m3) + curve(n0, n1, n2, n3) + barbs(m0, m1, m2, m3, 0.2, 0.95, 5, 6.5, 50, -1) +
                     barbs(n0, n1, n2, n3, 0.1, 0.85, 4, 6.5, 50, -1),
                 1.5),
           G::X(leaf({30, 31.5}, {32.5, 29}, {35.5, 27.5}, {39, 27}, 3.4, 0.8, 0.6)),
           G::XS("M28 27.5C31 24.5 35 22.5 39.5 22.5M15 43.5C17.5 44 18.8 46 17.8 48.5M12.5 53C16 54.5 19.5 55 23 54.5"
                 "M44.5 38C48.5 44 47 52 40.5 56.5M42.3 6.5 44.5 14.5",
                 1.5)});
  }
  // Готическая латная перчатка: стрельчатые пальцы из лам, большой палец, желобки на тыльной пластине, раструб
  // с остриём и каймой.
  {
    struct Fg {
      Pt b, c1, c2, t;
      double w0, w1;
    };
    const Fg fs[] = {{{37.5, 47}, {37, 35}, {35.5, 22}, {33, 10}, 9.2, 6.8},
                     {{46.5, 45}, {46.5, 31}, {46, 17}, {45, 4}, 9.4, 7},
                     {{55.5, 45}, {56, 32}, {56.5, 19}, {57, 7}, 9.2, 6.8},
                     {{64, 48}, {65, 37}, {66, 27}, {67.5, 16}, 8.6, 6.4}};
    std::string fingers, joints;
    for (const Fg& f : fs) {
      fingers += digit(f.b, f.c1, f.c2, f.t, f.w0, f.w1, 0.17);
      joints += notches(f.b, f.c1, f.c2, f.t, {0.36, 0.64}, [&](double t) { return f.w0 + (f.w1 - f.w0) * t; }, -0.9);
    }
    const Pt t0{38, 63}, t1{30, 58}, t2{23.5, 50}, t3{17, 39};
    const std::string thumb = digit(t0, t1, t2, t3, 10.5, 7.5, 0.22);
    const std::string hand = "M31 48C38 43.5 58 42.5 70 46.5L68 67C62 70 38 70 33 67Z";
    const std::string cuff = "M33 70.5C40 72.5 60 72.5 67 70.5L76.5 90C68 92 57 93.5 50 98.5C43 93.5 32 92 23.5 90Z";
    c.add("knight-gauntlet", "Латная перчатка",
          moved({G::F(fingers + hand), G::XS(joints, 1.4), G::XS(thumb, 2.4), G::F(thumb),
                 G::XS(notches(t0, t1, t2, t3, {0.45, 0.72}, [](double t) { return 10.5 - 3 * t; }, -0.9), 1.4),
                 G::XS("M32 48.5C40 45 58 44.5 69 48M41.5 66 40 52.5M50 67V51.5M58.5 66 60 52.5", 1.4), G::XS(cuff, 2.6), G::F(cuff),
                 rim(cuff, 2, 1.4), G::XS("M50 74.5V91", 1.4)},
                Affine::translate(3, 0)));
  }
  // Щит и меч: гранёное навершие, обмотка, гарда с острыми загнутыми концами; щит-«треугольник» с каймой
  // и лапчатым крестом, остриё клинка под щитом.
  {
    const std::string shield = "M23 31H77V50C77 66 67 77 50 85C33 77 23 66 23 50Z";
    const std::string guard = sym(horn({50.5, 24}, {40, 24}, {27, 23.5}, {15, 30.5}, 6.4, 1.5));
    c.add("knight-shield", "Щит и меч",
          {G::F(lozenge(50, 7, 5, 6.5) + rrect(46.8, 11.5, 6.4, 12.5, 1.2) + guard + "M45 24H55V87.5L50 99 45 87.5Z"),
           G::X(lozenge(50, 7, 2, 3)), G::XS("M46.5 15 53.5 18M46.5 19 53.5 22", 1.3), G::XS("M50 86V95", 1.3), G::XS(shield, 4.2),
           G::F(shield), rim(shield, 2.4, 1.6), G::X(crossPattee(50, 53, 17, 4.4, 12.5))});
  }
  // Конный рыцарь: конь в галопе с выгнутой шеей и попоной с зубчатым подолом, хвост языками, всадник в топфхельме
  // с намётом, щит с крестом, копьё наперевес.
  {
    const std::string horse = "M26 47C34 43.5 44 44 52 45.5C58 46.5 62 45 65 41C69 35 73 29 77 24.5L78.5 16.5 81.5 23.2"
                              "C85 25 88 29 91 33C93.5 36 96 38.5 95.5 41.5C95 44 93 45 90.5 44.5C87 43 83.5 40 80.5 37"
                              "C77.5 42 75.5 48 74.5 54C74 60 72 65 67 67.5C58 70 42 70 33 67.5C27 66 23.5 61 23.5 55"
                              "C23.5 51 24.5 48.5 26 47Z";
    const std::string drape = "M25.5 47C34 43 45 43.5 53 45C59 46 63 45 66.5 41.5L71.5 50C72.5 58 72.5 65 71 72L67.5 77.5 63.5 72.5"
                              "L59.5 78 55.5 73 51.5 78.5 47.5 73 43.5 78.5 39.5 73 35.5 78.5 31.5 72.5 27.5 77.5 24 71.5"
                              "C22 64 22 54 25.5 47Z";
    auto leg = [](Pt a, Pt k1, Pt k2, Pt b) { return band(a, k1, k2, b, [](double t) { return 7.4 - 4 * t; }); };
    const std::string legs = leg({66, 70}, {72, 70}, {79, 71}, {76, 83}) + leg({60, 72}, {61, 80}, {63, 86}, {66, 93}) +
                             leg({33, 71}, {29, 77}, {22, 79}, {14, 85}) + leg({40, 73}, {38, 80}, {37, 86}, {39, 93});
    const std::string hooves = xf("M-2.4 0H2.4L3.4 4H-3.4Z", place(76.3, 82.5, -15)) + xf("M-2.4 0H2.4L3.4 4H-3.4Z", place(66.2, 92.5, -18)) +
                               xf("M-2.4 0H2.4L3.4 4H-3.4Z", place(13.5, 84.8, 52)) + xf("M-2.4 0H2.4L3.4 4H-3.4Z", place(39.2, 92.5, 8));
    const std::string tail = flameTongue({25, 50}, {15, 47}, {8, 55}, {3, 68}, 7.5, 0.25) + flameTongue({24.5, 53}, {17, 55}, {13, 62}, {11, 72}, 5, 0.25);
    const std::string body = "M43 47.5C42 40 42.5 33 44.5 27.5H55C57 33 57.5 40 56.5 47.5Z";
    const std::string thigh = band({47, 46}, {52, 48}, {56, 52}, {57, 57}, [](double t) { return 8 - 2 * t; }) +
                              band({57, 56}, {57, 60}, {56, 63}, {55, 67}, [](double t) { return 6 - 2 * t; }) + "M53 65.5H59.5L60 68.5H53Z";
    const std::string helm = "M44.5 13C44.5 9 47 7 50.5 7C54 7 56.5 9 56.5 13V23.5C56.5 25.5 54 27 50.5 27.5C47 27 44.5 25.5 44.5 23.5Z";
    const std::string mantle = flameTongue({47, 9}, {40, 5}, {31, 7}, {24, 15}, 7, 0.3) + flameTongue({45.5, 14}, {40, 13}, {34, 17}, {30, 24}, 5, 0.3);
    const std::string lance = band({27, 42}, {50, 32}, {74, 21.5}, {98.5, 10.5}, [](double t) { return 4.2 - 2.2 * t; }) +
                              xf("M0 -2.2C3 -2.2 6 -1.2 9 0C6 1.2 3 2.2 0 2.2Z", place(89, 14.6, -24.2));
    const std::string shield = "M36 29H51V38.5C51 44 47.5 47.5 43.5 49.5C39.5 47.5 36 44 36 38.5Z";
    c.add("knight-rider", "Конный рыцарь",
          {G::F(tail), G::XS(drape + horse, 2.2), G::F(horse + legs + hooves), G::XS(drape, 2.2), G::F(drape),
           G::XS("M65 33.5C68 30 71 27.5 74 26M68 37.5C71 34 74 31.5 76.5 30M88.5 32.5 83.5 39M89 43.6 71 50", 1.4),
           G::X(lozenge(86, 31.5, 1.8, 1.2) + crossPattee(47, 60, 8, 2.2, 6.5)), rim(drape, 2, 1.3), G::XS(body + thigh, 2.2),
           G::F(body + thigh), G::XS(mantle + helm, 2), G::F(mantle), G::F(helm), G::XS("M51 15.5H57M50.5 9V26", 1.5),
           G::XS(lance, 2), G::F(lance), G::XS(shield, 2.2), G::F(shield), G::X(crossPattee(43.5, 38.5, 7.5, 2, 5.6))});
  }
  // Турнирное копьё: листовидный наконечник, древко, конусная гарда с гранями, рукоять; струящийся
  // флажок-ласточкин хвост с лапчатым крестом.
  {
    const Affine m = Affine::translate(-7, 1) * rotAt(41, 50, 50) * scaleAt(1.06f, 50, 50);
    const std::string lance = "M50 0.5C53.5 5 54.8 10 54 15H46C45.2 10 46.5 5 50 0.5Z" + rrect(45.6, 14.5, 8.8, 3, 1) +
                              band({50, 17}, {50, 32}, {50, 47}, {50, 62}, [](double t) { return 5.6 + 3.6 * t; }) +
                              "M50 56.5C58.5 58 66.5 66 69 77C62 79.5 38 79.5 31 77C33.5 66 41.5 58 50 56.5Z" + rrect(45.2, 77.5, 9.6, 10, 1.8) +
                              "M45.2 87H54.8L53 96Q50 98.5 47 96Z";
    const std::string pennon = "M53 16C61 12 67 19 74 16.5C78.5 15 82.5 14 87 15L78.5 25 87 35C81 35.5 77.5 33.5 72 35"
                               "C65 37 59 37.5 53 34Z";
    c.add("knight-lance", "Копьё с флажком",
          {G::F(xf(lance, m)),
           engr(xf("M50 3V13M39 67C45 64.5 55 64.5 61 67M35 72.5C43 70 57 70 65 72.5M45.5 80.5 54.5 84M45.5 84.5 54.5 88", m), 1.4),
           G::XS(xf(pennon, m), 2.4), G::F(xf(pennon, m)), G::X(xf(crossPattee(65, 25.5, 7, 2, 6), m)),
           engr(xf("M56 21.5C62 19.5 68 23 74.5 21.5", m), 1.3)});
  }
  // Турнирный шлем: решётчатое забрало, кайма, коронка с зубцами-лилиями, плюмаж из трёх перьев, намёт
  // листьями-аканфами по сторонам.
  {
    const std::string helm = symPath("M50 33C41.5 33 35 39 33.8 48L33 63C33 71 35.5 77 40 81L44 84L50 85");
    const std::string gorget = symPath("M50 83L42 84C37 87 33 91 29 96.5L50 96.5");
    // Намёт: ниспадающие лопасти, загнутые кончиками вверх-наружу.
    std::vector<std::string> mant = {flameTongue({39, 39}, {30, 34}, {19, 36}, {10, 45}, 10, 0.3),
                                     flameTongue({36, 50}, {26, 50}, {15, 56}, {7, 67}, 11, 0.3),
                                     flameTongue({35, 62}, {27, 66}, {19, 74}, {14, 86}, 10.5, 0.3),
                                     flameTongue({37.5, 73}, {32, 80}, {28, 87}, {26, 97}, 8.5, 0.3)};
    std::vector<L> v;
    v.push_back(G::F(sym(band({40, 36}, {33, 50}, {31, 68}, {34, 88}, [](double t) { return 9 - 3 * t; }))));
    for (const std::string& m : mant) {
      v.push_back(G::XS(sym(m), 2));
      v.push_back(G::F(sym(m)));
    }
    v.push_back(G::XS(sym(curve({34, 40}, {28, 37.5}, {20, 39}, {13, 45}) + curve({32, 51}, {24, 52}, {16, 57}, {10.5, 65}) +
                          curve({32, 63}, {26, 67}, {20, 74}, {16.5, 82}) + curve({35, 74}, {31, 80}, {29, 86}, {27.5, 93})),
                      1.3));
    // Плюмаж и коронка.
    const std::string plumes = flameTongue({50, 27}, {49, 18}, {51.5, 10}, {50, 1}, 10, 0.35) +
                               sym(flameTongue({46, 28}, {42, 21}, {37, 15}, {31, 11}, 7.5, 0.35));
    v.push_back(G::XS(plumes, 2));
    v.push_back(G::F(plumes));
    v.push_back(G::XS("M50 6V24" + sym(curve({44.5, 26}, {41.5, 21}, {38, 17}, {34, 14})), 1.2));
    const std::string crown = "M37.5 34.5L36.5 26 41 30.5 43.5 24 47 29.5 50 22.5 53 29.5 56.5 24 59 30.5 63.5 26 62.5 34.5Z";
    v.push_back(G::XS(crown + helm + gorget, 2.2));
    v.push_back(G::F(crown + helm + gorget));
    v.push_back(G::XS("M37.5 31.5H62.5", 1.3));
    v.push_back(rim(helm, 2, 1.4));
    v.push_back(G::X(ell(50, 58, 11.5, 14)));
    v.push_back(G::F(rrect(43, 44, 2.4, 28, 0.8) + rrect(48.8, 43, 2.4, 30, 0.8) + rrect(54.6, 44, 2.4, 28, 0.8) + rrect(38, 56.8, 24, 2.4, 0.8)));
    v.push_back(G::XS("M36 90.5C42 88.5 58 88.5 64 90.5", 1.3));
    v.push_back(G::X(lozenge(50, 79, 1.5, 2.2)));
    c.add("knight-tourney", "Турнирный шлем", v);
  }
  // Меч в камне: гранёное навершие, обмотка, гарда с опущенными остриями, клинок с долом; гранёная скала
  // с изломами, тонкие лучи сияния.
  {
    std::string glow;
    for (int i = 0; i < 6; i++)
      for (int sd : {-1, 1}) {
        const double a = -90 + sd * (22 + 14 * i), r = i % 2 ? 31 : 40;
        const Pt p0 = polar(50, 62, 19, a), p1 = polar(50, 62, r, a);
        glow += horn(p0, {p0.x + (p1.x - p0.x) / 3, p0.y + (p1.y - p0.y) / 3}, {p0.x + (p1.x - p0.x) * 2 / 3, p0.y + (p1.y - p0.y) * 2 / 3}, p1,
                     3.2, 1.2);
      }
    const std::string sword = lozenge(50, 7.5, 5, 6.5) + rrect(47, 13, 6, 13.5, 1.2) +
                              sym(horn({50.5, 28}, {40, 28}, {30, 28.5}, {21, 35}, 6, 1.5)) + "M45.8 28H54.2L53.6 66H46.4Z";
    const std::string rock = "M5 96.5L8 85 15.5 77.5 26 72 35 66.5 45 63.5H55L65 66 74 70.5 84.5 76 91.5 84.5 95 96.5Z";
    c.add("knight-excalibur", "Меч в камне",
          {G::F(glow), G::XS(sword, 3.6), G::F(rock), engr("M45 63.5 41.5 74.5 47 84 43.5 96.5M26 72 30 83.5 22.5 96.5M65 66 61 77.5 68.5 87 66 96.5"
                                                            "M84.5 76 78.5 86 82 96.5M15.5 77.5 19 87M35 66.5 38 71.5",
                                                            1.5),
           rimX(rock, 2, 1.3), G::XS(sword, 2.6), G::F(sword), G::X(lozenge(50, 7.5, 2, 3)), G::XS("M50 31.5V62", 1.4),
           engr("M47.5 16 52.5 18.5M47.5 20 52.5 22.5", 1.3)});
  }
  // Крепостная башня с гербом: флажок на шпиле, вогнутый шатёр с черепицей, машикули, щит с шевроном, бойницы,
  // стрельчатые ворота, цоколь.
  {
    const std::string shield = "M40.5 50H59.5V60C59.5 66.5 55.5 70.5 50 73C44.5 70.5 40.5 66.5 40.5 60Z";
    c.add("knight-keep", "Башня с гербом",
          {G::F(rrect(49.2, 1, 1.6, 12, 0.6) + "M50.8 1.6C56 0.4 61 3.4 67 2L62.5 6 67.5 10C61.5 11.2 56 8.6 50.8 9.8Z"),
           G::F("M50 8.5C52 18 58 28 74 36.5H26C42 28 48 18 50 8.5Z" + std::string("M27 36H73V43L68 47.5H32L27 43Z") +
                "M33 47H67L69 88H31Z" + "M27 87.5H73L75.5 96.5H24.5Z"),
           G::XS("M37.5 30.5Q50 28.5 62.5 30.5M43 22.5Q50 21 57 22.5M27 43H73M31 88H69", 1.4),
           G::X("M31 44.5H34V46.5H31ZM37.2 44.5H40.2V46.5H37.2ZM43.4 44.5H46.4V46.5H43.4ZM49.6 44.5H52.6V46.5H49.6ZM55.8 44.5H58.8V46.5"
                "H55.8ZM62 44.5H65V46.5H62Z"),
           G::XS(shield, 1.6), G::XS("M43.5 65 50 58.5 56.5 65", 2.4), G::XS("M36.5 54V68M34.5 61H38.5M63.5 54V68M61.5 61H65.5", 1.4),
           G::X("M45 88V82C45 78.5 47 76.5 50 75C53 76.5 55 78.5 55 82V88Z")});
  }
}

// ---------------------------------------------------------------- святые
void holy(Catalog& c) {
  c.group("Святые");
  // Лапчатый крест во славе: вогнутые плечи с каймой, медальон со звездой в средокрестии, тонкие лучи между плечами.
  {
    const std::string cross = crossPattee(50, 50, 46, 12, 28);
    std::string glow;
    for (int k = 0; k < 4; k++) {
      const double a = 45 + 90 * k;
      glow += horn(polar(50, 50, 20, a), polar(50, 50, 28, a), polar(50, 50, 36, a), polar(50, 50, 45, a), 4.4, 1.2);
      for (int sd : {-1, 1}) {
        const double b = a + sd * 11;
        glow += horn(polar(50, 50, 22, b), polar(50, 50, 26, b), polar(50, 50, 30, b), polar(50, 50, 35, b), 2.8, 1.2);
      }
    }
    c.add("holy-cross", "Крест",
          {G::F(glow), G::XS(cross, 3), G::F(cross), rim(cross, 2.2, 1.4), G::X(circ(50, 50, 9.5)), G::F(circ(50, 50, 7.2)),
           G::X(star(50, 50, 5.8, 1.8, 4, -90))});
  }
  // Крылья и нимб: воздетые геральдические крылья — плечо, ряд кроющих, маховые перья с острыми кончиками и
  // гравированными линиями раздела; нимб и четырёхлучевая звезда между крыльями.
  {
    const Wing w = heraldicWing({48.5, 56}, {37, 42}, {20, 22}, {7, 4},
                                {{2.5, 15}, {2, 28}, {3.5, 41}, {7, 53}, {12, 64}, {18.5, 74}, {26.5, 83}, {35.5, 90}, {45.5, 96}}, 0.3, 0.6, 8);
    c.add("holy-wings", "Крылья и нимб",
          {G::F(sym(w.fill)), G::XS(sym(w.cuts), 1.4), G::XS("M50 56V97", 2.4), G::S(ell(50, 13, 12, 4.4), 3),
           G::F(star(50, 37, 10, 2.4, 4, -90))});
  }
  // Голубь нисходящий с оливковой ветвью: распростёртые крылья из заострённых перьев, веер хвоста, тело-капля,
  // голова клювом вниз, ветвь с острыми листьями.
  {
    std::vector<L> v;
    std::vector<std::string> tail;
    for (Pt t : {Pt{37.5, 5}, Pt{43.5, 2}, Pt{50, 1}, Pt{56.5, 2}, Pt{62.5, 5}}) tail.push_back(quill({50, 25}, t, 9.6));
    layeredFeathers(v, tail, 1.8f, false);
    const Pt a0{45, 36}, a1{34, 27}, a2{20, 18}, a3{5, 14};
    layeredFeathers(v, wingQuills(a0, a1, a2, a3, {{40, 60}, {31, 62}, {22, 60}, {14, 55}, {8, 48}, {4, 40}, {2, 31}, {2.5, 22}}, 8.6, 0.06, 0.95, -0.03),
                    2.0f, true);
    const std::string arm = horn({47, 38}, {35, 27}, {21, 18.5}, {4, 13.5}, 8.6, 1.5);
    v.push_back(G::XS(sym(arm), 2));
    v.push_back(G::F(sym(arm)));
    const std::string body = symPath("M50 17C46 17 43 21 42 27C41 33 41.5 40 43.5 46C45 51 46.3 55 46.8 58L50 58.5");
    const std::string head = ell(50, 63.5, 5.4, 7) + "M47.6 69L50 76.5 52.4 69Z";
    v.push_back(G::XS(body + head, 2.2));
    v.push_back(G::F(body + head));
    v.push_back(G::X(sym(leaf({45.6, 62}, {46.4, 61}, {47.4, 60.8}, {48.3, 61.2}, 1.8, 0.4, 0.5))));
    // Оливковая ветвь.
    const Pt s0{50, 75}, s1{53, 81}, s2{56, 88}, s3{61, 95};
    std::string sprig = band(s0, s1, s2, s3, [](double t) { return 2.2 - t; });
    const double lt[] = {0.2, 0.42, 0.64, 0.86};
    for (int i = 0; i < 4; i++) {
      const CurvePt q = bezier(s0, s1, s2, s3, lt[i]);
      const int sd = i % 2 ? 1 : -1;
      const Pt b{float(q.x), float(q.y)};
      sprig += quill(b, {float(q.x + sd * q.nx * 9 + q.ny * 2.5), float(q.y + sd * q.ny * 9 - q.nx * 2.5)}, 3.6, sd * 0.06);
    }
    sprig += quill(s3, {63.5, 98.8}, 3.2);
    v.push_back(G::F(sprig));
    c.add("holy-dove", "Голубь", v);
  }
  // Святой Грааль: гостия с крестом в сиянии, чаша-тюльпан с ободом и лапчатым крестом, гранёный узел, тонкая
  // ножка, расширенная стопа с каймой.
  {
    const std::string cup = "M21 32H79V35C78 50 69.5 60 55.5 63.5L50 64.5 44.5 63.5C30.5 60 22 50 21 35Z";
    const std::string foot = "M46.5 78H53.5C54 84 57 87.5 64 90.5C69 92.5 73 94 74 97H26C27 94 31 92.5 36 90.5C43 87.5 46 84 46.5 78Z";
    c.add("holy-grail", "Святой Грааль",
          {G::F(glory(50, 19, 12.5, 22, 17, -170, -10, 9, 2.6)), G::XS(circ(50, 19, 9.5), 3), G::F(circ(50, 19, 9.5)),
           G::X(crossPattee(50, 19, 7.2, 2.2, 6.2)), G::F(cup + rrect(47.6, 63, 4.8, 16, 1) + "M50 67L58.5 72 50 77 41.5 72Z" + foot),
           engr("M22 37.5H78M42.5 72H57.5M30 93.5C40 91.5 60 91.5 70 93.5", 1.4),
           rimX("M23.5 40H76.5C75 51 67 58.5 55 61.3L50 62.3 45 61.3C33 58.5 25 51 23.5 40Z", 0, 1.4), G::X(crossPattee(50, 49.5, 8.5, 2.4, 7)),
           G::X(lozenge(50, 72, 2, 1.8))});
  }
  // Ангел с мечом: воздетые крылья из широких заострённых перьев, нимб, одеяние с острыми складками, меч остриём вниз.
  {
    const Pt a0{43, 37}, a1{36, 27}, a2{26, 15}, a3{14, 3};
    std::vector<L> v;
    layeredFeathers(v, wingQuills(a0, a1, a2, a3, {{27, 76}, {17, 68}, {9, 58}, {4, 46}, {2.5, 33}, {4, 21}, {8, 10}}, 11, 0.05, 0.92, 0.03), 2.0f,
                    true);
    const std::string armW = horn({44, 36}, {37, 26}, {27, 14}, {14, 3}, 8.5, 1.6);
    v.push_back(G::XS(sym(armW), 2));
    v.push_back(G::F(sym(armW)));
    const std::string robe = "M43 24H57C60.5 36 64.5 62 72 96.5H28C35.5 62 39.5 36 43 24Z";
    const std::string sword = "M47.6 43H52.4V87L50 93 47.6 87Z" + sym(horn({50.5, 42}, {46, 42}, {42, 42}, {38, 45}, 3.4, 1.2)) +
                              rrect(48, 33, 4, 9, 1.2) + lozenge(50, 31, 2.6, 3.2);
    v.push_back(G::XS(robe + circ(50, 15, 6), 2.4f));
    v.push_back(G::S(circ(50, 13.5, 10.5), 2.2f));
    v.push_back(G::F(robe + circ(50, 15, 6)));
    v.push_back(G::XS("M42 24C44 29 47 31 50 31C53 31 56 29 58 24", 1.4f));
    v.push_back(G::X(sym(horn({40, 60}, {38.5, 70}, {36, 82}, {33, 95}, 1.8, 1) + horn({45, 66}, {44.5, 75}, {43.5, 85}, {42, 95}, 1.6, 1))));
    v.push_back(G::XS(sword + "M42 34L50 39.5 58 34", 2.4f));
    v.push_back(G::F(sword));
    v.push_back(G::S("M42 34L47.5 38M58 34 52.5 38", 3.4f));
    v.push_back(G::XS("M50 46V84", 1.3f));
    c.add("holy-angel", "Ангел с мечом", v);
  }
  // Солнце во славе с ликом: прямые и пламенеющие лучи, кайма диска, строгий лик — дуги бровей, миндалевидные
  // глаза, тонкий нос, сомкнутые губы.
  {
    std::string rs;
    for (int i = 0; i < 16; i++) {
      const double a = -90 + 22.5 * i;
      if (i % 2 == 0) {
        rs += "M" + pt(50, 50, 24, a - 6.5) + "L" + pt(50, 50, 48, a) + "L" + pt(50, 50, 24, a + 6.5) + "Z";
      } else {
        const Pt p0 = polar(50, 50, 24, a), p1 = polar(50, 50, 44, a), c1 = polar(50, 50, 31, a + 10), c2 = polar(50, 50, 37, a - 10);
        rs += horn(p0, c1, c2, p1, 6.4, 1.1);
      }
    }
    c.add("holy-sun", "Солнце с ликом",
          {G::F(rs + circ(50, 50, 27)), G::XS(circ(50, 50, 24.5), 1.5), G::XS(sym("M36.5 42C39 39.5 43 39 46.5 40.5"), 1.4),
           G::X(sym(leaf({37, 46.5}, {39.5, 44.8}, {43, 44.6}, {46, 46}, 3, 0.5, 0.5))), G::F(sym(circ(41.8, 45.6, 1.1))),
           G::XS("M50 43.5V54.5M47 56C48.5 57 51.5 57 53 56", 1.3), G::X("M44 61.5C46.5 60.5 48 60.8 50 61.5C52 60.8 53.5 60.5 56 61.5C53.5 63.5 46.5 63.5 44 61.5Z")});
  }
  // Священная книга: лапчатый крест в сиянии над раскрытой книгой, изогнутые страницы со строками, обрез стопкой,
  // закладка ласточкиным хвостом.
  {
    const std::string cross = crossPattee(50, 25, 17, 4, 10.5);
    const std::string cover = "M50 56C40 50 26 48.5 6 50.5V81C26 79 40 80.5 50 86.5C60 80.5 74 79 94 81V50.5C74 48.5 60 50 50 56Z";
    const std::string pages = "M50 52.5C41 46.5 28 45 11 46.5V76C28 75 41 76.5 50 82C59 76.5 72 75 89 76V46.5C72 45 59 46.5 50 52.5Z";
    c.add("holy-book", "Священная книга",
          {G::F(glory(50, 25, 15, 34, 27, -170, -10, 9, 2.6)), G::XS(cross, 3), G::F(cross), G::X(circ(50, 25, 2.4)),
           G::F(cover + "M47 84H53L54.5 97 50 93.5 45.5 97Z"), G::XS(pages, 2.2), G::F(pages), G::XS("M50 53V82", 2),
           G::XS(sym("M8.5 53.5V78.5M17 52.5C26 52 35 53.5 43 57.5M17 59.5C26 59 35 60.5 43 64.5M17 66.5C26 66 35 67.5 43 71.5"), 1.4)});
  }
  // Лучистая звезда: четыре длинных луча креста (нижний длиннее), четыре коротких косых, грань по оси длинных лучей,
  // кольцо позади лучей, камень в центре, малые звёзды-искры.
  {
    std::vector<Pt> p;
    const double rr[8] = {46, 32, 49, 32, 46, 32, 47, 32};
    for (int i = 0; i < 8; i++) {
      p.push_back(polar(50, 49, rr[i], -90 + 45.0 * i));
      p.push_back(polar(50, 49, 15.5, -90 + 45.0 * i + 22.5));
    }
    std::string facets;
    for (int i = 0; i < 8; i += 2) facets += "M" + pt(50, 49, 11, -90 + 45.0 * i) + "L" + pt(50, 49, rr[i] * 0.8, -90 + 45.0 * i);
    c.add("holy-star", "Лучистая звезда",
          {G::S(circ(50, 49, 25), 3.4), G::XS(polyPts(p), 2.4), G::F(polyPts(p) + star(15, 14, 10, 2.2, 4, -90) + star(85, 85, 8.5, 2, 4, -90) +
                                                                    star(85, 14, 7, 1.8, 4, -90) + star(15, 85, 7, 1.8, 4, -90)),
           G::XS(facets, 1.3), G::X(circ(50, 49, 7.5)), G::F(star(50, 49, 5.8, 2.4, 4, -45))});
  }
  // Агнец со знаменем: идущий агнец с головой, обращённой назад, руно завитками-насечками, нимб, тонкие ноги
  // с копытцами; древко с крестом и раздвоенным знаменем.
  {
    const std::string bodyD = "M30 50C30 44 35 41 42 41C48 40 56 40 63 41C71 41.5 76 45 77 51C80 52 82 55 82 58C80 58 78 57.5 77 57"
                              "C76 63 71 67 64 67.5H40C33 67 30 62 30 56Z";
    const std::string headD = "M36 44C32 40 31 34 33 29.5C35 25 39 22.5 43 23L49 24.5C51 25 52 27 51 28.5L44.5 36C43.5 39.5 42 42.5 40 45Z";
    const std::string ear = flameTongue({39, 26}, {35.5, 24}, {32, 24.5}, {28.5, 27.5}, 4.4, 0.4);
    auto legD = [](Pt a, Pt b, Pt cc, Pt d) { return band(a, b, cc, d, [](double t) { return 4.6 - 1.6 * t; }); };
    const std::string legs = legD({38, 64}, {37, 72}, {37, 79}, {38, 88}) + legD({45, 65}, {46, 72}, {46, 80}, {45, 89}) +
                             legD({64, 64}, {66, 72}, {66, 80}, {64, 89}) + legD({71, 63}, {73, 70}, {73, 79}, {71.5, 88}) +
                             legD({37, 52}, {30, 56}, {25, 58}, {20, 57}) + "M35.5 87.5H40.5L41.5 91H35ZM42.5 88.5H47.5L48 92H42ZM61.5 88.5H66.5L67 92H61Z"
                             "M69 87.5H74L74.5 91H68.5Z";
    const std::string flag = "M22.5 7C29 4 35 9 42 6.5C46 5 50 5 54 6.5L48 13 54 20C48 22 43 18.5 37 20.5C32 22 27 23 22.5 20.5Z";
    // Руно: волнистые гравированные пряди вдоль тела.
    const std::string curls = "M36 47.5C40 45.5 43 49 47 47S54 45 58 47 65 49 69 47 73.5 46 75 48M33 54C37 52 40 55.5 44 53.5S51 51.5 55 53.5 62 55.5 66 53.5"
                              " 72 52 75.5 54.5M36 60.5C40 58.5 43 62 47 60S54 58 58 60 65 62 69 60 72.5 58.5 74 60";
    c.add("holy-lamb", "Агнец со знаменем",
          {G::F(rrect(20, 3, 2.6, 92, 1) + rrect(16.5, 5.5, 9.6, 2.4, 1) + lozenge(21.3, 2.2, 1.6, 2)), G::XS(flag, 2.2), G::F(flag),
           G::X(crossPattee(36.5, 13.5, 5.6, 1.6, 4.8)), G::S(circ(41, 31, 11.5), 2.2), G::XS(bodyD + headD + legs, 2.2),
           G::F(bodyD + headD + legs), G::XS(curls, 1.3), G::X(leaf({42.5, 29.5}, {44, 28.5}, {45.5, 28.4}, {47, 29}, 2, 0.4, 0.5)),
           G::XS(ear, 1.8), G::F(ear), G::XS("M49.5 27.5C48.5 29 47.5 30 46 30.5", 1.2)});
  }
}

// ---------------------------------------------------------------- демоны
void demons(Catalog& c) {
  c.group("Демоны");
  // Голова демона: хищная маска анфас — рога с кольцами, острые уши, гневные брови, раскосые глаза, скулы,
  // ноздри, оскал с клыками, знак на лбу, кайма.
  {
    const std::string face = symPath("M50 25C46 24.5 42 23.5 38.5 22L33.5 27C31 31 29.5 35.5 28.8 40L11 34.5 27.5 49C27.2 55 28.5 61 31 66"
                                     "C34.5 74 40 82 45.5 88L50 93");
    const Pt h0{40, 26}, h1{28, 19}, h2{15, 17}, h3{11, 1.5};
    const std::string hornL = horn(h0, h1, h2, h3, 11);
    c.add("demon-head", "Голова демона",
          {G::XS(sym(hornL), 2), G::F(sym(hornL)),
           engr(sym(notches(h0, h1, h2, h3, {0.36, 0.52, 0.66}, [](double t) { return 11 * (1 - std::pow(t, 1.3)); }, -1.2)), 1.3), G::F(face),
           rimX(face, 2, 1.3), G::XS(sym("M30.5 41L47.5 47.8M28 52C30.5 59 35 63.5 41 66M18 38.5 27 45"), 1.5),
           G::X(sym(leaf({33, 45.5}, {37, 47.5}, {41.5, 49.8}, {46.5, 52.6}, 4.4, 0.6, 0.45) + leaf({45.5, 67}, {46.5, 65.5}, {47.5, 64}, {48.5, 62}, 2.2, 0.4, 0.5)) +
                "M50 31L52.2 36 50 42 47.8 36Z" + "M37 74C42 76.5 46 77 50 77C54 77 58 76.5 63 74C60 81 55 84.5 50 84.5C45 84.5 40 81 37 74Z"),
           G::F(sym("M40.5 75.5 42.8 82.5 45 76.6Z" + std::string("M45.5 84.2 46.8 80 48.3 84.4Z"))), G::XS("M50 52V60", 1.3)});
  }
  // Бес распростёртый: перепончатые крылья с пальцами и фестонами, рогатая голова с острыми ушами, поджарое тело,
  // руки с когтями, согнутые ноги, хвост с остриём-пикой.
  {
    const std::string wing = "M44 37C36 29 26 19 18.5 12L15 4.5 20.5 9.5C21.2 10.2 21.2 11 20.5 11.6L3 27Q9.5 34 4.5 44Q12.5 49 12 59.5"
                             "Q22 55 31 60.5Q35.5 53 43.5 52Z";
    const std::string head = symPath("M50 13.5C46 13.5 42.5 16 42.5 20.5C42.5 25 45 29.5 50 32") + horn({45.5, 16}, {43, 10}, {40, 6}, {35.5, 1.5}, 4.6) +
                             horn({54.5, 16}, {57, 10}, {60, 6}, {64.5, 1.5}, 4.6) + horn({44, 21}, {41, 19.5}, {38.5, 19}, {35, 18.5}, 4) +
                             horn({56, 21}, {59, 19.5}, {61.5, 19}, {65, 18.5}, 4);
    const std::string body = "M41 34.5C40.5 40.5 42 47 45 52L44 58.5H56L55 52C58 47 59.5 40.5 59 34.5C56 32.5 44 32.5 41 34.5Z";
    auto limb = [](Pt a, Pt b, Pt cc, Pt d, double w0, double w1) { return band(a, b, cc, d, [=](double t) { return w0 + (w1 - w0) * t; }); };
    const std::string arms = sym(limb({42, 37}, {38, 40}, {35, 43}, {32.5, 45}, 4.6, 3.6) + limb({32.5, 45}, {31.5, 41}, {31, 37}, {31, 33}, 3.6, 3) +
                                 horn({31, 33.5}, {30, 30}, {28.5, 28}, {26.5, 26}, 2.2) + horn({31, 33}, {31, 30}, {31.5, 28}, {32, 25.5}, 2.2) +
                                 horn({31.5, 33.5}, {33, 31}, {34.5, 29.5}, {36, 28}, 2.2));
    const std::string legs = sym(limb({45.5, 56}, {42, 61}, {38.5, 65}, {35.5, 69}, 6, 4.4) + limb({35.5, 69}, {36, 74}, {37.5, 79}, {39.5, 84}, 4.4, 3) +
                                 horn({40, 83}, {37, 84.5}, {34, 85.5}, {30, 86}, 3.4) + horn({40.5, 84}, {39.5, 86.5}, {38.5, 88.5}, {37, 91}, 2.8));
    const Pt t0{53, 57}, t1{63, 65}, t2{57, 78}, t3{70, 86};
    const std::string tail = band(t0, t1, t2, t3, [](double t) { return 3.4 - 1.4 * t; }) +
                             xf("M0 -1.2C3 -3 5 -5.5 4 -7C8 -5 11.5 -2 14 0C11.5 2 8 5 4 7C5 5.5 3 3 0 1.2Z", place(69, 85.4, 33));
    c.add("demon-imp", "Бес",
          {G::F(sym(wing)), G::XS(sym("M19.5 11.5 4.5 27M19.5 12 6.5 43M20 12.5 13 57.5M19.5 12 30 58"), 1.3), G::XS(arms + legs + tail + head + body, 2.2),
           G::F(arms + legs + tail + head + body),
           G::X(sym(leaf({44.5, 21.5}, {45.8, 22.5}, {47, 23.3}, {48.4, 24}, 2.2, 0.4, 0.5)) + "M46.5 27.5C48.5 28.3 51.5 28.3 53.5 27.5L52.6 29C51 29.6 49 29.6 47.4 29Z"),
           G::XS("M45 40C47.5 42 52.5 42 55 40M50 42V50M46.5 45.5H53.5", 1.3)});
  }
  // Когтистая лапа: жилистые пальцы со складками суставов, крючковатые когти, большой палец, сухожилия на тыльной
  // стороне, рваный обшлаг запястья.
  {
    struct Fg {
      Pt b, c1, c2, t;
      double w0, w1;
      int in;
    };
    const Fg fs[] = {{{39, 47}, {35, 37}, {31.5, 29}, {28, 21}, 8.2, 5.6, 1},
                     {{46, 44}, {44.5, 33}, {42.5, 23}, {41, 13}, 8.6, 5.8, 1},
                     {{54, 44}, {55.5, 33}, {57, 23}, {58.5, 12.5}, 8.6, 5.8, -1},
                     {{61, 47}, {64.5, 38}, {67.5, 30}, {71, 22}, 8.2, 5.6, -1},
                     {{39, 62}, {33, 58}, {28, 52}, {24, 45}, 8.6, 5.8, 1}};
    std::string fingers, claws, joints;
    for (const Fg& f : fs) {
      fingers += band(f.b, f.c1, f.c2, f.t, [&](double t) { return f.w0 + (f.w1 - f.w0) * t; });
      const CurvePt q = bezier(f.b, f.c1, f.c2, f.t, 1);
      const double dx = q.ny, dy = -q.nx;  // касательная
      double sx = q.nx, sy = q.ny;
      if ((sx * f.in) < 0) {
        sx = -sx;
        sy = -sy;
      }
      const Pt p0 = f.t;
      claws += horn(p0, {float(p0.x + dx * 5), float(p0.y + dy * 5)}, {float(p0.x + dx * 9 + sx * 1.5), float(p0.y + dy * 9 + sy * 1.5)},
                    {float(p0.x + dx * 10.5 + sx * 6), float(p0.y + dy * 10.5 + sy * 6)}, f.w1, 1.1);
      joints += notches(f.b, f.c1, f.c2, f.t, {0.42, 0.78}, [&](double t) { return f.w0 + (f.w1 - f.w0) * t; }, -1.6);
    }
    const std::string palm = "M36 50C36 45 42 42.5 50 42.5C58 42.5 64 45 64 50L63 69C62 75 58.5 79 56 81.5H44C41.5 79 38 75 37 69Z";
    const std::string cuff = "M42 80.5H58L60 98.5H40Z";
    c.add("demon-claw", "Когтистая лапа",
          moved({G::F(fingers + claws + palm), G::XS(joints, 1.4), G::XS("M45 77C44 68 43 60 41.5 51M50 78.5V50M55 77C56 68 57 60 58.5 51", 1.4),
                 G::XS(cuff, 2.4), G::F(cuff + "M40 98.5 38 91 42 93 41 86 45 89 46 83 50 87 54 83 55 89 59 86 58 93 62 91 60 98.5Z"),
                 G::XS("M41.5 86.5H58.5", 1.3)},
                Affine::translate(3.5, 0) * scaleAt(0.96f, 50, 100)));
  }
  // Вилы: зубья с зазубренными наконечниками, боковые — дугой наружу, полумесяц-перекладина, ромб-узел,
  // древко с обмоткой и пикой.
  {
    auto head = [](double x, double y, double deg) {
      return xf("M0 0L8 15.5 3.2 13 2.8 18.5H-2.8L-3.2 13-8 15.5Z", place(float(x), float(y), float(deg)));
    };
    const std::string fork = head(50, 0.5, 0) + band({50, 50}, {50, 38}, {50, 27}, {50, 16}, [](double t) { return 7 - 1.4 * t; }) +
                             band({32.5, 43}, {27.5, 35}, {27, 26}, {30.5, 19}, [](double t) { return 6.6 - 1.4 * t; }) + head(31.8, 5, 15) +
                             band({67.5, 43}, {72.5, 35}, {73, 26}, {69.5, 19}, [](double t) { return 6.6 - 1.4 * t; }) + head(68.2, 5, -15) +
                             "M29 38C33 50.5 41.5 57 50 57C58.5 57 67 50.5 71 38L66 41C62.5 48 57 52 50 52C43 52 37.5 48 34 41Z" +
                             lozenge(50, 60, 6.6, 7.2) + band({50, 64}, {50, 74}, {50, 83}, {50, 90}, [](double) { return 7.6; }) +
                             "M46.2 89.5H53.8L50 99.5Z";
    c.add("demon-pitchfork", "Вилы",
          moved({G::F(fork), G::X(lozenge(50, 60, 2.4, 3)), engr("M46.5 69 53.5 72.5M46.5 74 53.5 77.5M46.5 79 53.5 82.5", 1.4),
                 engr("M50 4V13M32.5 9 31 15M67.5 9 69 15", 1.3)},
                Affine::translate(-6.5, 1) * rotAt(24, 50, 50)));
  }
  // Крылатый демон: рогатая голова с хищным ликом, перепончатые крылья с когтем на сгибе и пальцами, фестоны
  // перепонки, могучий торс с гравированными мышцами.
  {
    const std::string wing = "M40 56C34 44 25 30 17 18L13 8.5 19 15.5C19.5 16 19.6 16.8 19 17.5L2 31Q9.5 40 3 50Q13 56 11 67Q21 66 25 76"
                             "Q31 69 38 70Z";
    const std::string body = "M30 66C32.5 57 40 52.5 50 52.5C60 52.5 67.5 57 70 66L76 97H24Z";
    const std::string head = symPath("M50 17C44.5 17 40.5 20.5 40 26.5L41 36C42.5 43 46 47.5 50 49");
    const std::string hornL = horn({44.5, 22}, {38, 15}, {30, 12}, {28, 1}, 6.6);
    c.add("demon-winged", "Крылатый демон",
          {G::F(sym(wing)), G::XS(sym("M18 17 3 31M18 17.5 5 49M18.5 18 12 65M19 18.5 25 74"), 1.3), G::XS(sym(hornL) + body + head, 2.4),
           G::F(sym(hornL) + body + head + sym("M40.5 29 33 25.5 40.5 34.5Z")),
           G::XS(sym(notches({44.5, 22}, {38, 15}, {30, 12}, {28, 1}, {0.4, 0.6}, [](double t) { return 6.6 * (1 - std::pow(t, 1.3)); }, -0.8)), 1.2),
           G::X(sym(leaf({42, 31}, {44, 32.2}, {46, 33.4}, {48.2, 34.6}, 2.6, 0.4, 0.5)) + "M44.5 41C47.5 42 52.5 42 55.5 41L54.5 43C51.5 44 48.5 44 45.5 43Z" +
                "M50 22L51.5 25 50 28.5 48.5 25Z"),
           G::F(sym("M46 42.3 47 45.5 48 42.6Z")),
           G::XS(sym("M38 67C42 71.5 46 72.5 50 70.5M42 78.5 48 79.5M42.5 85 48 86M43 91.5 48 92.5M33 61C36 58 41 56.5 46 57") + "M50 70.5V97", 1.5)});
  }
  // Демонический трон: стрельчатая спинка с языками-шпилями, рога, череп в навершии, кайма, подлокотники
  // с когтями, сиденье, ножки с когтистыми лапами.
  {
    const std::string back = "M29 62V24L35 27 38 14 43 21 50 4 57 21 62 14 65 27 71 24V62Z";
    const Pt h0{31.5, 30}, h1{21, 26}, h2{13, 18}, h3{12, 4};
    const std::string hornL = horn(h0, h1, h2, h3, 8.5);
    const std::string skull = symPath("M50 21C44.5 21 41 24.8 41 29.5C41 32.8 42.6 35 44.3 36.2L45 41H50");
    const std::string seat = rrect(17, 59, 66, 8.5, 2) + sym("M12 45C12 42 14 41 17 41H33V51H17C14 51 12 49 12 47Z");
    const std::string legs = sym("M23 67H33V84C33 88 35 90.5 37 92.5H33.5L31.5 89.5 29.5 93.5 27.5 89.5 25.5 93.5 23.5 89.5 20.5 92.5H18"
                                 "C21 89 23 86.5 23 82Z");
    c.add("demon-throne", "Демонический трон",
          {G::XS(sym(hornL), 2), G::F(sym(hornL) + back),
           G::XS(sym(notches(h0, h1, h2, h3, {0.4, 0.6}, [](double t) { return 8.5 * (1 - std::pow(t, 1.3)); }, -1)), 1.3),
           G::XS("M34 57V31L38 32.5 40.5 23.5 45 28.5 50 15.5 55 28.5 59.5 23.5 62 32.5 66 31V57Z", 1.4), G::XS(skull, 2.4), G::F(skull),
           G::X(sym("M43 28.3L48.6 30.6 47.8 33.4 44 32.2Z") + "M50 33.6 51.4 36.4H48.6Z"),
           G::XS("M47.6 37.5V41M50 37.5V41M52.4 37.5V41", 1.2), G::XS(seat, 2.4), G::F(seat + legs + rrect(8, 93, 84, 4, 1.5)),
           G::X(sym(lozenge(15.5, 46, 1.4, 1.8))), G::XS("M20 63.5H80", 1.3)});
  }
  // Глаз в пламени: венец огненных языков S-образными изгибами, миндалевидный глаз с двойным веком,
  // радужка-кольцо, вертикальный зрачок, блик.
  {
    const std::string eye = "M7 57C21 42 35 36 50 36C65 36 79 42 93 57C79 72 65 78 50 78C35 78 21 72 7 57Z";
    const std::string flames = flameTongue({50, 40}, {45, 28}, {55, 16}, {50, 1}, 13, 0.3) +
                               sym(flameTongue({40, 41}, {33, 30}, {39, 19}, {31, 6}, 11.5, 0.3) + flameTongue({29, 45}, {22, 36}, {26, 26}, {16, 16}, 10.5, 0.3) +
                                   flameTongue({19, 51}, {12, 45}, {13, 37}, {4, 32}, 8.5, 0.3) + flameTongue({38, 73}, {34, 80}, {38, 87}, {32, 96}, 8, 0.3) +
                                   flameTongue({25, 68}, {20, 73}, {21, 79}, {14, 85}, 6.5, 0.3));
    c.add("demon-eye", "Глаз в пламени",
          {G::F(flames), G::XS(sym(curve({39, 39}, {34, 31}, {37, 22}, {32.5, 12})) + curve({50, 37}, {47.5, 28}, {52.5, 18}, {50, 8}), 1.4),
           G::XS(eye, 3.4), G::F(eye), G::XS("M13 57C26 46 38 41.5 50 41.5C62 41.5 74 46 87 57", 1.4), G::X(circ(50, 58, 15)),
           G::F(circ(50, 58, 12.5)), G::XS(circ(50, 58, 9), 1.3), G::X("M50 46C53.5 51 53.5 65 50 70C46.5 65 46.5 51 50 46Z" + circ(57, 52, 1.8))});
  }
  // Рогатый череп: изогнутые рога с кольцами, угловатые раскосые глазницы, нос «сердцем», скулы, верхние зубы,
  // нижняя челюсть, трещина.
  {
    const std::string skull = symPath("M50 19C37.5 19 27.5 27.5 26.5 41C26 49 28 55 31.5 60L34 70C35 73 37.5 74.5 40.5 74.5L50 74.5");
    const std::string jaw = symPath("M50 77L38.5 77C37.5 81 38 85 40 88C42.5 91 46 92.5 50 92.5");
    const Pt h0{31, 35}, h1{12, 36}, h2{3, 23}, h3{11, 1.5};
    const std::string hornL = horn(h0, h1, h2, h3, 12.5);
    c.add("demon-skull", "Рогатый череп",
          {G::XS(sym(hornL), 2), G::F(sym(hornL)),
           G::XS(sym(notches(h0, h1, h2, h3, {0.3, 0.46, 0.62, 0.76}, [](double t) { return 12.5 * (1 - std::pow(t, 1.3)); }, -1)), 1.4),
           G::F(skull + jaw), rim(skull, 2, 1.3), G::X(sym("M32 43.5L46.5 48 45 56C41 57.5 35.5 56.5 33 52Z") + "M50 58.5L53.5 65.5 50 69 46.5 65.5Z"),
           G::XS("M41 70V74.5M45.5 70V74.5M50 70V74.5M54.5 70V74.5M59 70V74.5M41.5 77V81M45.5 77V82M50 77V82.5M54.5 77V82M58.5 77V81"
                 "M57 20.5 54.5 27 58.5 31 56.5 36M30 58C32.5 61.5 35.5 63 39.5 63.5",
                 1.4)});
  }
  // Адские врата: стрельчатая арка из клиньев, замковый камень-череп, пламя языками в проёме, ступень.
  {
    std::string joints;
    for (int i = 1; i < 5; i++) {
      const double t = i / 5.0;
      const CurvePt o = bezier({10, 48}, {10, 28}, {27, 10}, {50, 4}, t), in = bezier({24, 50}, {24, 34}, {35, 21}, {50, 16}, t);
      joints += sym("M" + ptS({float(o.x), float(o.y)}) + "L" + ptS({float(in.x), float(in.y)}));
    }
    const std::string arch = "M10 93V48C10 28 27 10 50 4C73 10 90 28 90 48V93ZM24 93V50C24 34 35 21 50 16C65 21 76 34 76 50V93Z";
    const std::string flames = flameTongue({50, 93}, {43, 76}, {56, 58}, {48, 30}, 16, 0.4) + sym(flameTongue({37, 93}, {30, 82}, {39, 68}, {32, 50}, 12, 0.4)) +
                               sym(flameTongue({29, 93}, {26, 88}, {29, 80}, {27, 70}, 7, 0.4));
    c.add("demon-gate", "Адские врата",
          {G::FE(arch), rim("M10 93V48C10 28 27 10 50 4C73 10 90 28 90 48V93Z", 2, 1.3),
           G::XS(joints + "M10 60H24M76 60H90M10 72H24M76 72H90M10 84H24M76 84H90", 1.6),
           G::X(sym(leaf({44.5, 11}, {45.5, 9.5}, {47, 9.5}, {48.3, 12}, 2.8, 0.5, 0.5))), G::XS("M50 13.5V15", 1.2),
           G::F(flames + rrect(6, 92, 88, 6, 1.5)), G::XS(curve({50, 88}, {45, 72}, {54, 56}, {48.5, 38}), 1.4), G::XS("M8 95H92", 1.2)});
  }
}

// ---------------------------------------------------------------- культисты
void cultists(Catalog& c) {
  c.group("Культисты");
  // Пентаграмма (перевёрнутая) с переплетёнными лучами в кольце с угловатыми рунами.
  {
    std::vector<Pt> v5;
    for (int i = 0; i < 5; i++) v5.push_back(polar(50, 50, 35, 90 + 144 * i));
    std::string lines, unders, overs;
    bool over = true;
    for (int i = 0; i < 5; i++) {
      const Pt a = v5[i], b = v5[(i + 1) % 5];
      lines += "M" + ptS(a) + "L" + ptS(b);
      std::vector<std::pair<double, Pt>> hits;
      for (int j = 0; j < 5; j++) {
        if (j == i || j == (i + 1) % 5 || (j + 1) % 5 == i) continue;
        const Pt c0 = v5[j], c1 = v5[(j + 1) % 5];
        const double d = (b.x - a.x) * (c1.y - c0.y) - (b.y - a.y) * (c1.x - c0.x);
        if (std::fabs(d) < 1e-9) continue;
        const double t = ((c0.x - a.x) * (c1.y - c0.y) - (c0.y - a.y) * (c1.x - c0.x)) / d;
        const double u = ((c0.x - a.x) * (b.y - a.y) - (c0.y - a.y) * (b.x - a.x)) / d;
        if (t > 0 && t < 1 && u > 0 && u < 1) hits.push_back({t, {float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t)}});
      }
      std::sort(hits.begin(), hits.end(), [](const auto& p, const auto& q) { return p.first < q.first; });
      const float len = std::hypot(b.x - a.x, b.y - a.y), ux = (b.x - a.x) / len, uy = (b.y - a.y) / len;
      for (const auto& h : hits) {
        const float d = over ? 6.f : 4.4f;
        (over ? overs : unders) += "M" + ptS({h.second.x - ux * d, h.second.y - uy * d}) + "L" + ptS({h.second.x + ux * d, h.second.y + uy * d});
        over = !over;
      }
    }
    // Руны: угловатые знаки поперёк кольца (локально — вертикальные, высота 6).
    const char* glyphs[] = {"M0 -3V3M0 -3 2.2 -1.2M0 -0.6 2.2 1.2", "M-1.6 -3V3M-1.6 -3 1.6 -0.6-1.6 1.6", "M0 -3V3M-2 -0.8 0 1.2 2 -0.8",
                            "M0 -3V3M-2 -1 0 -3 2 -1", "M-1.8 3 0 -3 1.8 3M-1 0.6H1", "M-1.8 -3 1.8 3M1.8 -3-1.8 3"};
    std::string runes;
    for (int i = 0; i < 20; i++) {
      const double a = 90 + 18 * i;
      if (i % 4 == 0) continue;  // против концов звезды
      const Pt p = polar(50, 50, 40.2, a);
      runes += xf(glyphs[i % 6], place(p.x, p.y, float(a - 90)));
    }
    c.add("cult-pentagram", "Пентаграмма",
          {G::FE(circ(50, 50, 45) + circ(50, 50, 35.5)), G::XS(runes, 1.3), G::XS(circ(50, 50, 43.2), 1.2), G::XS(circ(50, 50, 37.3), 1.2),
           G::S(lines, 3.6), G::XS(unders, 4.6), G::S(overs, 3.6)});
  }
  // Капюшон: острый клобук, лицо во тьме с горящими раскосыми глазами, двойной край капюшона, ниспадающая мантия
  // с острыми складками, амулет со звездой на цепи.
  {
    const std::string robe = symPath("M50 3C46 8 38 14 33.5 22C29 30 27 40 27.5 50C28 57 29.5 62 31.5 66C24 70 18 77 14 85L9 97.5H50");
    const std::string hood = symPath("M50 21C45 24 39 31 37.5 41C36.5 49 38 57 42 61.5C44.5 64 47 65 50 65");
    c.add("cult-hood", "Капюшон",
          {G::F(robe), rim(robe, 2, 1.3), G::XS(hood, 3.4), G::X(hood), G::F(sym(leaf({41, 47.2}, {42.8, 47.8}, {44.6, 48.6}, {46.6, 49.8}, 1.9, 0.4, 0.4))),
           G::X(sym(horn({31, 70}, {29, 78}, {26, 86}, {22, 97}, 2.2, 1) + horn({38, 74}, {37, 82}, {35.5, 90}, {34, 98}, 2, 1)) +
                horn({50, 70}, {50, 78}, {50, 86}, {50, 98}, 2, 1)),
           G::XS(sym("M42.5 68.5 50 76.5"), 1.4), G::XS(circ(50, 84, 6.4), 3), G::F(circ(50, 84, 6.4)), G::X(star(50, 84.6, 5, 2, 5, 90))});
  }
  // Всевидящее око: треугольник с гравированной каймой, миндалевидный глаз с радужкой и бликом, сияние тонкими лучами.
  {
    const std::string tri = "M50 8L93 83H7Z";
    c.add("cult-eye", "Всевидящее око",
          {G::F(glory(50, 58, 30, 50, 43, -175, -5, 15, 2.8)), G::XS(tri, 5),
           G::FE(tri + inset(tri, 5.5)), G::XS(inset(tri, 2.75), 1.3), G::F(inset(tri, 8.5)), G::X("M27 60C33 51.5 41 47.5 50 47.5C59 47.5 67 51.5 73 60C67 68.5 59 72.5 50 72.5C41 72.5 33 68.5 27 60Z"),
           G::F(circ(50, 60, 9)), G::X(circ(50, 60, 4.6) + circ(54, 56.5, 1.6)), G::XS(circ(50, 60, 7), 1.2)});
  }
  // Голова козла (Бафомет): кольчатые рога, уши в стороны, раскосые глаза с поперечным зрачком, перевёрнутая
  // пентаграмма на лбу, узкая морда с ноздрями, борода прядями.
  {
    const std::string face = symPath("M50 27C44.5 27 40 28.5 37.5 32L35.5 40.5C30 41.5 23 44.5 16 51C23.5 52 29.5 51.5 34.5 50L37 65C38.5 73 41.5 79 44.5 83.5L46 86.5 50 99");
    const Pt h0{42.5, 31}, h1{34, 18}, h2{24, 10}, h3{17, 1.5};
    const std::string hornL = horn(h0, h1, h2, h3, 14);
    c.add("cult-goat", "Голова козла",
          {G::XS(sym(hornL), 2), G::F(sym(hornL)),
           engr(sym(notches(h0, h1, h2, h3, {0.32, 0.5}, [](double t) { return 14 * (1 - std::pow(t, 1.3)); }, -1.6)), 1.3), G::F(face),
           G::X(sym(leaf({36.5, 49.5}, {39.5, 50.5}, {42.5, 52}, {46, 54.5}, 4.2, 0.6, 0.45)) + star(50, 41.5, 6.8, 2.7, 5, 90)),
           G::F(sym(rrect(38.8, 50.4, 4.6, 1.6, 0.6))), G::X(sym(leaf({45, 79.5}, {45.6, 77.5}, {46.4, 76}, {47.4, 74.5}, 2.2, 0.4, 0.5))),
           G::XS(sym("M21 47.5 33 49.5M38.5 63C40 69.5 43 74.5 46.5 77") + "M50 88V96M47 87.5 48.3 93M53 87.5 51.7 93", 1.3)});
  }
  // Ритуальный кинжал: навершие-полумесяц, рукоять с кольцами, гарда рогами-полумесяцем, волнистый клинок
  // с долом, тонкое кольцо со знаками.
  {
    std::vector<Pt> lft, rgt;
    std::string fuller = "M";
    for (int i = 0; i <= 48; i++) {
      const double t = i / 48.0, y = 42 + 55 * t, cx = 50 + 3.2 * std::sin(t * 3 * kPi), hw = 7.4 * std::pow(1 - t, 0.75) + 0.15;
      lft.push_back({float(cx - hw), float(y)});
      rgt.push_back({float(cx + hw), float(y)});
      if (t > 0.04 && t < 0.72) fuller += (fuller.size() > 1 ? "L" : "") + ptS({float(cx), float(y)});
    }
    Pts blade = lft;
    blade.push_back({50, 98.5});
    for (size_t i = rgt.size(); i-- > 0;) blade.push_back(rgt[i]);
    const std::string guard = sym(horn({50.5, 39}, {40, 37}, {31, 39}, {26.5, 47}, 6.5, 1.2));
    const std::string hilt = guard + rrect(46, 22, 8, 18.5, 2) + "M41.5 11C43 15.5 46.5 18 50 18C53.5 18 57 15.5 58.5 11C56 13.5 53 14.5 50 14.5"
                                                                "C47 14.5 44 13.5 41.5 11Z" + lozenge(50, 7, 3.4, 5.5);
    std::string marks;
    for (int i = 0; i < 12; i++) {
      const double a = -90 + 30 * i;
      if (i == 0 || i == 6) continue;
      const Pt p = polar(50, 55, 37.5, a);
      marks += xf("M0 -2.4V2.4M-1.6 -0.8H1.6", place(p.x, p.y, float(a - 90)));
    }
    c.add("cult-dagger", "Ритуальный кинжал",
          {G::FE(circ(50, 55, 40.5) + circ(50, 55, 34.5)), G::XS(marks, 1.2), G::XS(polyPts(blade) + hilt, 3.6), G::F(polyPts(blade) + hilt),
           G::XS(fuller, 1.3), G::XS("M46 27 54 29.5M46 31.5 54 34", 1.3), G::X(lozenge(50, 7, 1.3, 2.4))});
  }
  // Свеча на черепе: пламя с сердцевиной и тонкими лучами, высокая свеча с потёками воска остриями, череп
  // с широкими скулами, угловатыми глазницами, носом и зубами.
  {
    const std::string candle = "M43.5 58V27.5C43.5 26 45 25.2 46.5 26C47.5 25 49 25.2 50 26.4C51.3 25 53.5 25.2 54.5 26.4C55.5 25.6 56.5 26.4 56.5 27.5V58Z";
    const std::string skull = symPath("M50 52C40 52 31.5 58 30 67C29.5 72 31 75.5 34 77.5L37 79.5V88.5C37 91 39 92.5 41.5 92.5L50 92.5");
    const std::string drips = horn({43.6, 31}, {43.4, 37}, {43, 43}, {41.8, 49}, 3, 1.4) + horn({56.4, 29}, {56.6, 34}, {57, 38}, {58, 43}, 2.8, 1.4);
    c.add("cult-candle", "Свеча и череп",
          {G::F(glory(50, 13, 11, 19, 15, -170, -10, 7, 2.4)), G::XS(skull, 2.4), G::F(skull + candle + drips),
           engr("M47 29V46.5C47 48.5 49 48.5 49 46.5V30M52.5 29V38C52.5 40 54.3 40 54.3 38V29.5", 1.3),
           G::X(sym("M35.5 64.5L46.5 67.5 45.5 72.5C42 74 38 73 36 70.5Z") + "M50 73.5L52.6 79H47.4Z"),
           engr("M41 83H59M44.5 83V92.5M50 83V92.5M55.5 83V92.5M61 59.5C63.5 61.5 65 64.5 65.5 67.5", 1.4), G::S("M50 26.5V21.5", 1.4),
           G::F(flameTongue({50, 23.5}, {45, 18}, {55, 10}, {50, 0.5}, 12, 0.35)),
           G::X(flameTongue({50, 21.5}, {48, 18}, {52, 14}, {50, 9}, 4.2, 0.35))});
  }
  // Щупальца: око в сфере с веками, шесть извивающихся S-образных щупалец с присосками по одной стороне.
  {
    struct Arm {
      Pt p0, c1, c2, p1;
      double side;
    };
    const Arm arms[] = {{{41, 39}, {33, 22}, {13, 33}, {11, 6}, 1}, {{35.5, 50}, {21, 43}, {15, 64}, {2.5, 52}, -1},
                        {{41, 61}, {33, 78}, {15, 70}, {13, 95}, 1}};
    std::string body, suckers;
    auto w = [](double t) { return 11.5 * (1 - std::pow(t, 1.15)) + 0.3; };
    for (const Arm& a : arms) {
      body += band(a.p0, a.c1, a.c2, a.p1, w, 48);
      for (double t : {0.28, 0.4, 0.51, 0.61, 0.7, 0.78}) {
        const CurvePt q = bezier(a.p0, a.c1, a.c2, a.p1, t);
        const double hw = w(t) * 0.5;
        suckers += circ(q.x + q.nx * hw * 0.42 * a.side, q.y + q.ny * hw * 0.42 * a.side, std::max(0.75, hw * 0.3));
      }
    }
    c.add("cult-tentacles", "Щупальца",
          {G::F(sym(body)), G::X(sym(suckers)), G::XS(circ(50, 50, 16.5), 2.6), G::F(circ(50, 50, 16.5)), G::XS(circ(50, 50, 13.8), 1.2),
           G::X("M37.5 50C41.5 44 45.5 42 50 42C54.5 42 58.5 44 62.5 50C58.5 56 54.5 58 50 58C45.5 58 41.5 56 37.5 50Z"), G::F(circ(50, 50, 5.6)),
           G::X("M50 45.2C51.6 47.4 51.6 52.6 50 54.8C48.4 52.6 48.4 47.4 50 45.2Z")});
  }
  // знак на лбу, зашитый рот.
  {
    const std::string mask = symPath("M50 16C40 16 32 21.5 28.5 31C25.5 39 25.5 50 28.5 60C32 72 40 82 50 89");
    std::string blades;
    for (int i = 0; i < 11; i++) {
      const double a = -165 + 15 * i, r = i % 2 ? 37 : 46;
      blades += horn(polar(50, 48, 20, a), polar(50, 48, 28, a), polar(50, 48, 36, a), polar(50, 48, r, a), 4.4, 1.1);
    }
    c.add("cult-mask", "Маска культиста",
          moved({G::F(blades), G::XS(mask, 3), G::F(mask), rim(mask, 2, 1.3), G::X(sym("M31.5 47C35 42.5 41.5 42 46.5 47.5C42.5 51 36 51.5 31.5 47Z")),
                 G::XS(sym("M37.5 52C37 58 38 64 37 71M42 52.5C42 56 42.5 59 42 63"), 1.4), G::X(crossPattee(50, 31, 6.5, 1.6, 4.6, 1.6)),
                 G::XS("M41 74C46 75.5 54 75.5 59 74M43.5 71.5V77.5M47.5 72.5V78.5M52.5 72.5V78.5M56.5 71.5V77.5", 1.3)},
                Affine::translate(0, 5)));
  }
  // Уроборос: змей, кусающий свой хвост, — сужающееся тело с чешуйными шевронами, голова с бровью и глазом,
  // внутри алхимический знак — перевёрнутый треугольник в круге.
  {
    std::string chevrons;
    for (int i = 0; i < 20; i++) {
      const double a = -55 + 15 * i, t = (a + 80) / 335.0, w = (14 + (3 - 14) * t) * 0.5;
      if (a > 250) break;
      const Pt o = polar(50, 52, 36 - w + 0.6, a - 3), m = polar(50, 52, 36, a + 3), in = polar(50, 52, 36 + w - 0.6, a - 3);
      chevrons += "M" + ptS(o) + "L" + ptS(m) + "L" + ptS(in);
    }
    const Affine hp = place(57, 16, 9, 1.15f);
    const std::string head = xf("M-14 0C-12.5 -6.5 -5 -9 3 -8C9 -7 14 -3.5 14 0C14 3.5 9 7 3 8C-5 9 -12.5 6.5 -14 0Z", hp);
    c.add("cult-ouroboros", "Уроборос",
          {G::F(taperArc(50, 52, 36, -80, 255, 14, 3)), G::XS(chevrons, 1.3), G::XS(head, 2.4), G::F(head),
           G::X(xf(leaf({-4, -2.6}, {-2.5, -3.8}, {-0.5, -4}, {1.5, -3.2}, 2.6, 0.5, 0.5) + "M-14 0.4H3L-14 3Z", hp)),
           G::XS(xf("M-8 -5.5C-4.5 -6.8 -0.5 -6.8 3 -5.6", hp), 1.2), G::S(circ(50, 52, 17), 2.2), G::S("M37.5 45.5H62.5L50 67Z", 2.4),
           G::F(circ(50, 52.5, 2.6))});
  }
}

// ---------------------------------------------------------------- магия
void magic(Catalog& c) {
  c.group("Магия");
  // Шляпа мага: вытянутый колпак с изогнутым остриём, поля-«линза» с острыми краями, лента с пряжкой, вырезы —
  // звёзды и месяц.
  {
    const std::string cone = "M30 78C36 61 41 43 45 29C48 19 53 12 62 8C70 5 78 7 85 12.5C77 11.5 70 13 65.5 17C61 21 58.5 27 59 35"
                             "C60.5 49 64.5 63 70.5 78Z";
    const std::string brim = "M5 83C20 75.5 36 74 50 74C64 74 80 75.5 95 83C80 89.5 64 91.5 50 91.5C36 91.5 20 89.5 5 83Z";
    c.add("magic-hat", "Шляпа мага",
          {G::F(cone), G::XS(brim, 2.6), G::F(brim), G::XS("M13 84C27 80.5 39 79.5 50 79.5C61 79.5 73 80.5 87 84", 1.3),
           G::XS("M31.5 68C43 71 57 71 68.5 68M30.5 74C43 77 57 77 69.5 74", 1.4), G::XS(rrect(45, 67.5, 10, 8.5, 1.2), 1.4),
           G::X(star(49.5, 52, 7, 2.7, 5, -90) + star(55, 31, 4, 1.6, 5, -90) + star(43.5, 62.5, 2.6, 1, 4, -90) + circ(48.5, 40, 4)),
           G::F(circ(50.4, 38.8, 3.5)), G::XS("M64 10.5C69 8.5 75 9 80 11", 1.2)});
  }
  // закладки; над книгой — парящая четырёхлучевая звезда с искрами.
  {
    const std::string cover = "M50 50C40 44 26 42.5 6 44.5V84C26 82 40 83.5 50 89.5C60 83.5 74 82 94 84V44.5C74 42.5 60 44 50 50Z";
    const std::string pages = "M50 47C41 41 28 39.5 11 41V78.5C28 77.5 41 79 50 84.5C59 79 72 77.5 89 78.5V41C72 39.5 59 41 50 47Z";
    std::string lines;
    for (int i = 0; i < 4; i++) {
      const double y = 47 + 7.5 * i;
      lines += "M17 " + n(y) + "C24 " + n(y - 1.2) + " 30 " + n(y + 1) + " 36 " + n(y + 0.2) + "S42 " + n(y + 1.6) + " 44 " + n(y + 3);
    }
    c.add("magic-book", "Книга заклинаний",
          {G::F(cover + "M53 86 52 98 55.5 95 59 98 58 85.5Z"), G::XS("M7 81.5C26 79.5 40 81 50 87C60 81 74 79.5 93 81.5", 1.3),
           G::XS(pages, 2.4), G::F(pages), G::XS("M50 48V84", 2.2), G::XS(lines, 1.4), G::XS(circ(69.5, 60, 10.5), 1.4),
           G::X(star(69.5, 60, 7.4, 2.9, 5, -90)), G::XS("M6.5 51 14 43.5M93.5 51 86 43.5M6.5 77 13 83.5M93.5 77 87 83.5", 1.4),
           G::F(star(50, 19, 17, 2.6, 4, -90) + star(32, 24, 5.5, 1.3, 4, -90) + star(68, 24, 5.5, 1.3, 4, -90) + star(40, 7, 3, 0.9, 4, -90) +
                star(61, 7.5, 2.6, 0.8, 4, -90))});
  }
  // Хрустальный шар: светлая кромка, блик-серп, вихри тумана, искра внутри; когтистая лапа-подставка обхватывает
  // шар снизу, основание с каймой.
  {
    const std::string talons = sym(horn({39, 83}, {28, 79}, {20, 70}, {21, 56}, 8.5, 1.3) + horn({45, 82}, {39, 77}, {35, 73}, {33, 68.5}, 6.4, 1.3));
    const std::string base = "M30 87C30 82.5 39 80.5 50 80.5C61 80.5 70 82.5 70 87V90H30Z" + rrect(23, 89, 54, 8.5, 2.5);
    c.add("magic-orb", "Хрустальный шар",
          {G::XS(circ(50, 40, 32), 3.6), G::F(circ(50, 40, 32)), G::XS(circ(50, 40, 28.8), 1.3), G::XS(arc(50, 40, 22, 200, 250), 3.4),
           G::XS("M26 46C34 39 43 50 52 43C58 39 65 42 74 36M30 56C37 51 44 58 51 54C56 51 62 53 70 49", 2.2), G::X(star(62, 27, 6, 1.5, 4, -90)),
           G::XS(talons, 2.4), G::F(talons + base), G::XS("M25.5 93H74.5", 1.3)});
  }
  // Посох: древко с обмоткой и лентой, суковатое навершие — две ветви обвивают вытянутый гранёный кристалл
  // в сиянии тонких лучей.
  {
    const Affine m = Affine::translate(-1, 1) * rotAt(30, 50, 50);
    const std::string staff = band({50, 99}, {50, 76}, {50, 52}, {50, 36}, [](double t) { return 11.5 + 3 * t; }) +
                              band({50, 38}, {38, 33}, {36, 19}, {42, 9}, [](double t) { return 9.5 - 7 * t; }) +
                              band({50, 38}, {62, 33}, {64, 21}, {57, 12}, [](double t) { return 9.5 - 7 * t; });
    const std::string ribbon = flameTongue({52, 58}, {62, 60}, {66, 70}, {74, 76}, 6.5, 0.2) + flameTongue({52, 60}, {58, 66}, {58, 76}, {64, 84}, 5.5, 0.2);
    const std::string crystal = "M50 0.5L60.5 17.5 50 35 39.5 17.5Z";
    const Pt cc = m.apply({50, 17});
    c.add("magic-staff", "Посох",
          {G::F(glory(cc.x, cc.y, 19, 31, 25, -200, 40, 13, 3.6)), G::XS(xf(staff + ribbon, m), 1.8), G::F(xf(staff, m)),
           engr(xf("M45.5 70 54.5 73M45.5 75 54.5 78M45.5 80 54.5 83", m), 1.4), G::XS(xf(ribbon, m), 1.8), G::F(xf(ribbon, m)),
           G::XS(xf(crystal, m), 2.2), G::F(xf(crystal, m)), engr(xf(inset(crystal, 3.2), m), 1.3),
           G::F(star(14, 64, 5.5, 1.2, 4, -90) + star(28, 90, 3.6, 0.9, 4, -90))});
  }
  // Зелье: круглая колба с длинным горлом, пробка с сургучом и бечёвкой, жидкость с волной и пузырьками,
  // блик на стекле, искры.
  {
    const std::string flask = circ(50, 64, 29) + rrect(42.5, 20, 15, 30, 2);
    const std::string inner = circ(50, 64, 25.5) + rrect(46, 22, 8, 26, 1);
    c.add("magic-potion", "Зелье",
          {G::F(flask + rrect(38, 16.5, 24, 6, 3) + "M43.5 17V8C43.5 5.5 45.5 4 48 4H52C54.5 4 56.5 5.5 56.5 8V17Z"), G::X(inner),
           G::XS("M41 19.5H59M45.5 10H54.5", 1.3),
           G::F("M25.5 61C31.5 57 38.5 65 46 61.5C53 58 60 65 67 61.5C70 60 72.5 60 74.4 61A24.5 24.5 0 1 1 25.5 61Z"),
           G::X(circ(57, 74, 2.6) + circ(43, 80, 2) + circ(51, 69, 1.5) + circ(62, 83, 1.4)), G::S(arc(50, 64, 20, 200, 240), 2.2),
           G::F(circ(44, 50, 1.8) + circ(53, 43, 1.4)), G::F(star(19, 28, 7.5, 1.6, 4, -90) + star(82, 22, 6, 1.4, 4, -90) + star(84, 40, 3.4, 0.9, 4, -90))});
  }
  // Магический круг: кольцо со знаками, гексаграмма с узлами, внутренний круг со звездой.
  {
    std::string glyphs, dots;
    for (int i = 0; i < 24; i++) {
      const double a = 7.5 + 15.0 * i;
      if (i % 3 == 0) dots += lozenge(polar(50, 50, 42.5, a).x, polar(50, 50, 42.5, a).y, 1.4, 1.4);
      if (i % 3 == 1) glyphs += "M" + pt(50, 50, 40.3, a) + "L" + pt(50, 50, 44.7, a) + "M" + pt(50, 50, 42.5, a - 3) + "L" + pt(50, 50, 42.5, a + 3);
      if (i % 3 == 2) glyphs += "M" + pt(50, 50, 40.3, a - 2.5) + "L" + pt(50, 50, 44.7, a) + "L" + pt(50, 50, 40.3, a + 2.5);
    }
    std::string hex, nodes, holes;
    for (int k = 0; k < 2; k++) {
      for (int i = 0; i < 3; i++) hex += (i ? "L" : "M") + pt(50, 50, 34, -90 + 180 * k + 120 * i);
      hex += "Z";
    }
    for (int i = 0; i < 6; i++) {
      const Pt q = polar(50, 50, 34, -90 + 60 * i);
      nodes += circ(q.x, q.y, 4);
      holes += circ(q.x, q.y, 1.6);
    }
    c.add("magic-circle", "Магический круг",
          {G::FE(circ(50, 50, 47) + circ(50, 50, 38)), G::XS(glyphs, 1.3), G::X(dots), G::S(circ(50, 50, 34), 1.8), G::S(hex, 2.6),
           G::XS(nodes, 2.2), G::F(nodes), G::X(holes), G::S(circ(50, 50, 13), 1.8), G::F(star(50, 50, 9, 2.4, 4, -90))});
  }
  // Волшебная палочка: палочка с кольцами на рукояти, на конце — сверкающая звезда с длинными лучами, от неё —
  // сужающийся шлейф дугой и искры.
  {
    const std::string wand = band({10, 93.5}, {24, 79.5}, {38, 66}, {56, 48}, [](double t) { return 13 - 5.5 * t; });
    const std::string sparkle = star(63, 37, 32, 5, 4, -90) + star(63, 37, 16, 3.6, 4, -45);
    const std::string trail = flameTongue({70, 44}, {91, 56}, {91, 81}, {65, 93}, 11, 0.15);
    c.add("magic-wand", "Волшебная палочка",
          {G::F(wand + trail), engr("M14 76.5 27.5 90M21.5 70 34 82.5", 1.5), engr(curve({72, 47}, {87, 57}, {87, 77}, {70, 88}), 1.3),
           G::XS(sparkle, 2.2), G::F(sparkle), G::X(circ(63, 37, 4.4)), G::F(circ(63, 37, 2.4)),
           G::F(star(24, 22, 8, 1.7, 4, -90) + star(14, 44, 4, 1, 4, -90) + star(38, 8, 4, 1, 4, -90) + star(48, 84, 4.4, 1.1, 4, -90) +
                star(80, 97, 2.6, 0.8, 4, -90))});
  }
  // Сфера с рунами: ядро с руной и бликом, две орбиты — за ядром и перед ним, луны, искры.
  {
    auto orbit = [](float deg, bool front) {
      std::string s;
      for (int i = 0; i <= 36; i++) {
        const double t = (front ? 0 : 180) + 5.0 * i;
        const Pt q = polar(0, 0, 1, t);
        s += (i ? "L" : "M") + ptS({q.x * 46, q.y * 12.5f});
      }
      return xf(s, place(50, 50, deg));
    };
    const Pt mq = polar(0, 0, 1, 40);
    const Pt m1 = place(50, 50, -28).apply({mq.x * 46, mq.y * 12.5f});
    c.add("magic-sphere", "Сфера с рунами",
          {G::S(orbit(-28, false) + orbit(28, false), 2.6), G::XS(circ(50, 50, 22), 2.6), G::F(circ(50, 50, 22)), G::XS(circ(50, 50, 19), 1.2),
           G::XS("M50 37V63M50 44 57 37.5M50 44 43 37.5M50 54 57 60.5M50 54 43 60.5", 1.8), G::XS(arc(50, 50, 15, 205, 245), 1.8),
           G::XS(orbit(-28, true) + orbit(28, true), 5.4), G::S(orbit(-28, true) + orbit(28, true), 2.6), G::XS(circ(m1.x, m1.y, 4.5), 2),
           G::F(circ(m1.x, m1.y, 4.5) + circ(100 - m1.x, m1.y, 3.5) + star(84, 13, 6, 1.4, 4, -90) + star(16, 87, 5, 1.2, 4, -90))});
  }
  // Сова-фамильяр анфас: острые перьевые ушки, строгий клин бровей к клюву, глаза в лицевом диске, крючковатый
  // клюв, грудь чешуёй-шевронами, сложенные крылья с острыми концами, когти на книге.
  {
    const std::string body = symPath("M50 23C43 22.5 37 23.5 33 25.5L21.5 8 25.5 29C23.5 34 23 39 23.5 46C24 58 28 70 35 79L40 86H50");
    const std::string wingL = "M24.5 46C21 57 22 70 30 82L41 91 36 80C31 70 29 58 30 47Z";
    std::string scales;
    for (int r = 0; r < 4; r++)
      for (int k = -1; k <= 1; k++) {
        if (r == 3 && k != 0) continue;
        const double x = 50 + 7 * k + (r % 2) * 3.5 - (r % 2 && k == 1 ? 7 : 0), y = 63 + 5.5 * r;
        scales += "M" + n(x - 3) + " " + n(y) + "L" + n(x) + " " + n(y + 2.6) + "L" + n(x + 3) + " " + n(y);
      }
    c.add("magic-owl", "Сова-фамильяр",
          {G::F(body + rrect(14, 87, 72, 9.5, 2)), G::XS(sym(wingL), 2.2), G::F(sym(wingL)), G::XS(sym("M28.5 53C27.5 62 30 72 36 80"), 1.3),
           G::XS("M16 90.5H84M16 93.5H84M22 87V96.5", 1.3), G::XS(sym("M24.5 14.5 29.5 27"), 1.2),
           G::XS(sym("M31 39C30 47 34 54.5 41.5 56C45 56.5 47.5 55 49 53"), 1.4), G::X(sym(circ(40, 46.5, 6.4))), G::F(sym(circ(40, 46.5, 4.4))),
           G::X(sym(circ(41, 46, 1.6))), G::X(sym("M28 36.5L48 45.5 47 48.5 29.5 41Z")), G::F("M46.8 48L50 60 53.2 48C51.8 49.8 48.2 49.8 46.8 48Z"),
           G::XS(scales, 1.3), G::S(sym("M43 85.5 42 91.5M47 86V92"), 2.2)});
  }
  // Песочные часы: плиты с навершиями-шпилями, витые стойки, колбы-обводка, песок сверху, струйка и горка внизу.
  {
    const std::string glass = "M33 14C33 30 44 40 47.5 50C44 60 33 70 33 86H67C67 70 56 60 52.5 50C56 40 67 30 67 14Z";
    std::string twist;
    for (int i = 0; i < 7; i++) {
      const double y = 19 + 9.5 * i;
      twist += "M20.5 " + n(y) + "L27.5 " + n(y + 4.5) + "M72.5 " + n(y) + "L79.5 " + n(y + 4.5);
    }
    c.add("magic-hourglass", "Песочные часы",
          {G::F("M12 11L16 6H84L88 11 84 16H16Z" + std::string("M12 89L16 84H84L88 89 84 94H16Z") + "M47 6 50 0.5 53 6ZM47 94 50 99.5 53 94Z" +
                rrect(20.5, 15, 7, 70, 2) + rrect(72.5, 15, 7, 70, 2)),
           G::XS(twist + "M16 11H84M16 89H84", 1.3), G::S(glass, 2.4),
           G::F("M37.5 29C40.5 36 45.5 42.5 49 48H51C54.5 42.5 59.5 36 62.5 29Z" + rrect(49.2, 49, 1.6, 30, 0.8) + "M37 84C38.5 77 45 72.5 50 72.5"
                "C55 72.5 61.5 77 63 84Z"),
           G::XS("M41 33.5H59", 1.2)});
  }
}

// ---------------------------------------------------------------- орки
void orcs(Catalog& c) {
  c.group("Орки");
  // Голова орка: грубая маска анфас — пучок волос языком пламени с завязкой, тяжёлые надбровья, глубоко посаженные
  // раскосые глаза, шрам, широкий нос, уши с надрезом и серьгами, выпяченная челюсть, клыки полумесяцами.
  {
    const std::string face = symPath("M50 16C42 16 35 17.5 31 21.5C27.5 25 26.5 30 26.5 35L25.5 41 8.5 39.5 26 51C25 59 25.5 67 28 74"
                                     "C30.5 81 37 88 45 91L50 92");
    const std::string tusks = sym(horn({38.5, 77}, {36, 71}, {34.5, 66}, {34.5, 58}, 7, 1.2));
    c.add("orc-head", "Голова орка",
          {G::F(flameTongue({50, 19}, {43, 12}, {57, 7}, {51, 0.5}, 15, 0.3)), G::F(face), rim(face, 2, 1.3), G::XS("M43 17C47.5 19 52.5 19 57 17", 1.5),
           G::XS(sym("M28 42 47.5 47.8"), 2.4), G::XS(sym("M14.5 41 25.5 46.5M30.5 57C32.5 63 36 67 41 69.5"), 1.5),
           G::X(sym(leaf({33.5, 50}, {37, 51}, {41, 52.2}, {45.5, 54}, 3.2, 0.6, 0.45) + leaf({43, 65}, {44.5, 63.8}, {46, 63.2}, {47.8, 63}, 2.6, 0.4, 0.5)) +
                "M12 39 16 39.5 13.5 43Z" + circ(87.5, 42.5, 1.5) + circ(84, 45, 1.5)),
           G::XS("M33.5 75C41 71.5 59 71.5 66.5 75M58 40 64.5 58", 1.6), G::XS(tusks, 2.4), G::F(tusks)});
  }
  {
    const std::string head = "M5.5 50L7.5 44.5C12 42.5 20 40.5 28 36.5C34 33.5 40 30 45.5 27L49 12.5 56.5 24.5C62 23.5 67 24 72 25.5L77 18 80 27"
                             "L87 23 86.5 32.5 94 32 89 40.5 96 44 89 49.5 95 55 88.5 58C90 63 90 67 88 71L93 77 85 77.5 86.5 85 79 81 76.5 88.5"
                             "L71.5 82 66 89 62.5 81.5 56 87 54.5 79 48 82C44.5 76.5 40.5 72 36 69C30 67 24 65 18 63L12 61.5C8.5 59.5 6 56 5.5 50Z";
    const std::string tusk = horn({21, 63}, {12, 58.5}, {9.5, 46}, {15.5, 34.5}, 7.5, 1.2);
    c.add("orc-boar", "Вепрь",
          {G::F(head), rim(head, 2, 1.2), G::X(ell(9.5, 51.5, 1.8, 3.4)), G::XS("M12.5 45.5C13.5 49 13.5 54 12 58.5", 1.3),
           G::X(leaf({33, 40.5}, {35.5, 39.5}, {38.5, 39.5}, {41, 40.5}, 3, 0.5, 0.5)), G::XS("M30.5 36.5C35 34 40 33.5 44.5 35M50 17 53 25", 1.6),
           G::XS("M19 43C20.5 46 21 49 20.5 52M24.5 41C26 44 26.5 47 26 50M10 58C16 61 22 62.5 28 63", 1.3),
           G::XS("M57 31C63 33 69 37 72 43M62 42C67 45 71 50 72 56M57 53C63 57 67 63 68 70M47 60C52 65 56 71 57 78", 1.4), G::XS(tusk, 2.2),
           G::F(tusk)});
  }
  // Скрещённые тесаки: зазубренный обух, отверстие, выпуклое лезвие с подточкой, обмотка рукояти, кольцо на конце.
  c.add("orc-cleavers", "Скрещённые тесаки",
        crossed("M40 5H58C63 5 66 8.5 66 13.5C66 30 65 46 63 61H41L39 55 43 51 39 46 43 42 39 37 43 33 39 28 43 24 39 19 43 15 39 10Z" +
                    rrect(45.5, 60, 9, 27, 2.5) + circ(50, 91, 5.5),
                circ(56.5, 14.5, 3.4) + circ(50, 91, 2.4), "M45.5 66H54.5M45.5 71.5H54.5M45.5 77H54.5M45.5 82.5H54.5M61 22C60.5 35 60 47 58.5 57", 1.4,
                36, 50, 51, 1.07f));
  // Тотем: рогатый бычий череп на шесте, кольца рогов, перекладина с перьями на шнурах и бусинами, обмотка,
  // острый конец.
  {
    const std::string skull = symPath("M50 13C42 13 36 18 35.5 26C35 32 37.5 37 41 41L43 52H50");
    const Pt h0{38, 22}, h1{28, 23}, h2{16, 18}, h3{11, 3};
    const std::string hornL = horn(h0, h1, h2, h3, 8.5);
    std::string feathers, cords;
    for (double x : {21.0, 31.0}) {
      feathers += quill({float(x), 70}, {float(x - 1.5), 95}, 6.4, 0.02);
      cords += "M" + n(x) + " 62V70";
    }
    c.add("orc-totem", "Тотем",
          {G::F(rrect(45.5, 44, 9, 47, 2.5) + "M45.5 90H54.5L50 99.5Z" + rrect(14, 56, 72, 7, 3.5)), G::S(sym(cords), 1.6),
           G::F(sym(feathers + circ(26, 66, 2.6))), G::XS(sym("M20.5 76.5V90.5M30 76.5V90.5"), 1.2),
           G::XS("M45.5 68 54.5 71M45.5 73 54.5 76M45.5 78 54.5 81", 1.4), G::XS(sym(hornL), 2), G::F(sym(hornL)),
           G::XS(sym(notches(h0, h1, h2, h3, {0.35, 0.52, 0.68}, [](double t) { return 8.5 * (1 - std::pow(t, 1.3)); }, -1)), 1.3),
           G::XS(skull, 2.2), G::F(skull), G::X(sym("M39 26L47 29.5 45.5 34 40.5 32Z") + "M48 38.5 50 35 52 38.5 50 42Z"),
           G::XS("M46 45V51M50 45V52M54 45V51M55 14 52.5 20 55.5 23", 1.3)});
  }
  // Кулак: сжатые пальцы с гранёными костяшками, складки суставов, большой палец поперёк, шрамы от когтей,
  // наруч с шипами, заклёпками и каймой.
  {
    auto finger = [](double x0, double x1, double top) {
      return "M" + n(x0) + " " + n(top + 5) + "L" + n(x0 + 4) + " " + n(top) + "H" + n(x1 - 4) + "L" + n(x1) + " " + n(top + 5) + "V44H" + n(x0) + "Z";
    };
    const std::string fingers = finger(22, 36, 15) + finger(36.5, 50.5, 10.5) + finger(51, 65, 11.5) + finger(65.5, 78, 16.5);
    const std::string hand = "M22 38H78V56C78 60 75 63 71 63H29C25 63 22 60 22 56Z" + std::string("M30 61H70L68 68H32Z");
    const std::string thumb = "M19.5 48C19.5 42.5 23.5 40 29.5 40H52L59.5 44.5V50.5L54 56.5H28C23 56.5 19.5 52.5 19.5 48Z";
    const std::string bracer = "M27 67H73L75.5 89H24.5Z";
    const std::string spikes = sym("M27.5 69.5 10 72.5 27 76ZM26.5 79.5 9 82.5 26 86Z");
    c.add("orc-fist", "Кулак",
          {G::F(fingers + hand + "M31 89H69L71 99H29Z"), G::XS("M36.25 13V38M50.75 11V38M65.25 13V38M23 29.5H77", 1.6),
           G::XS("M26.5 22.5H32M41 18.5H46M55.5 19.5H60.5M69.5 24.5H74", 1.3), G::XS("M58 18 66 31M62.5 16 70.5 29M54 20.5 61 32", 1.4), G::XS(thumb, 3),
           G::F(thumb), G::XS("M44 41V55.5", 1.4), G::XS(bracer + spikes, 2.4), G::F(bracer + spikes), rim(bracer, 1.8, 1.3),
           G::X(lozenge(36, 78, 1.8, 1.8) + lozenge(50, 78, 1.8, 1.8) + lozenge(64, 78, 1.8, 1.8)), G::XS("M30 94H70", 1.3)});
  }
  // Череп орка: тяжёлые надбровья, широкая носовая впадина, клыки нижней челюсти над губой, зубы, трещины,
  // следы когтей.
  {
    const std::string skull = symPath("M50 10C35 10 23 20 22.5 36C22.3 44 25 50 29 55V63H50");
    const std::string jaw = symPath("M50 66H27V75C27 83 37.5 89.5 50 89.5");
    const std::string tusks = sym("M32 75C30 63 31 53 35.5 45C37 54 39 64 41.5 75Z");
    c.add("orc-skull", "Череп орка",
          moved({G::F(skull + jaw), rim(skull, 2, 1.3), G::XS("M28 64.5H72M38 56V63M44 56V63M50 56V63M56 56V63M62 56V63M44 66V74M50 66V75M56 66V74", 1.5),
                 G::X(sym("M29.5 38C32.5 34 41 34 45.5 39C44.5 45 39.5 48 34.5 47C30.5 46 28.5 42 29.5 38Z") + "M50 45 54.5 54.5H45.5Z"),
                 G::XS(sym("M27 33.5C33 29.5 41.5 29.5 48 34.5"), 2), G::XS("M58 11.5 55 18.5 60 23.5 57 29.5M38 14.5 35.5 25.5M44.5 13.5 43 25.5", 1.5),
                 G::XS(tusks, 2.4), G::F(tusks)},
                scaleAt(1.1f, 50, 50)));
  }
  // Боевой волк: оскаленная морда анфас — острые уши с прорезью, воротник шерсти языками, раскосые глаза под
  // нахмуренной бровью, складки на переносице, нос, клыки.
  {
    const std::string half = "M50 22C44 22 40 23.5 36.5 26L26 5 23.5 30C20 34 18 38 16 42L5 41 14.5 49 6 56 18.5 59.5 11 67 23.5 67"
                             "C27 72 31 76 35 79L39 89C43 93 47 95 50 95H50.5V22Z";
    c.add("orc-wolf", "Боевой волк",
          {G::F(sym(half)), G::X(sym("M27 12.5 29.5 27.5 34.5 25.5Z" + std::string("M33 45 45 49.5 43 53.5 35.5 51Z"))),
           G::XS(sym("M30.5 40 46.5 45.5M39 57 45.5 59M38 61.5 44.5 64") + "M45 31 50 37 55 31", 1.5),
           G::X("M42.5 66C42.5 62 57.5 62 57.5 66C57.5 70 53.5 72 50 72C46.5 72 42.5 70 42.5 66Z" +
                std::string("M36 76C41 80 45.5 81.5 50 81.5C54.5 81.5 59 80 64 76L61 87C57 90.5 53.5 91.5 50 91.5C46.5 91.5 43 90.5 39 87Z")),
           G::F(sym("M39 77 42.5 87 45 79.5Z" + std::string("M42.5 90.5 44 83.5 46.5 91Z")) + ell(50, 64.6, 3.6, 1.3)), G::XS("M50 71V77", 1.4)});
  }
  // Топор и щит: круглый дощатый щит с окованным ободом, заклёпками и умбоном; за ним бородовидный топор.
  {
    const Affine ax = place(68, 29, 221.7f, 0.95f);
    std::string planks;
    for (double x : {34.0, 44.0, 54.0}) {
      const double h = std::sqrt(25.5 * 25.5 - (x - 44) * (x - 44));
      planks += "M" + n(x) + " " + n(58 - h) + "V" + n(58 + h);
    }
    std::string rivets;
    for (int i = 0; i < 8; i++) rivets += lozenge(polar(44, 58, 28.3, 22.5 + 45 * i).x, polar(44, 58, 28.3, 22.5 + 45 * i).y, 1.5, 1.5);
    const std::string shield = circ(44, 58, 31);
    c.add("orc-axe-shield", "Топор и щит",
          {G::F(band({15, 93}, {38, 67}, {61, 41}, {84, 15}, [](double t) { return 6.4 - 1.6 * t; })),
           G::F(xf("M2 -8C10 -10 18 -15 23 -23C29 -12 29 9 23 20C18 13 10 9 2 8ZM-2 -4.5-12 0-2 4.5Z", ax)),
           G::XS(xf("M5 -6C12 -7 18 -10 22 -15M5 6C12 7 18 9 22 13", ax), 1.4), G::XS("M18 82.5 25 89.5M22 78 29 85", 1.5), G::XS(shield, 3.4),
           G::F(shield), G::XS(circ(44, 58, 25.5), 1.6), G::XS(planks, 1.5), G::X(rivets + "M15 64 19.5 61.5 18 67Z"), G::X(circ(44, 58, 11)),
           G::F(circ(44, 58, 8.4)), G::X(star(44, 58, 4.4, 1.6, 4, -90))});
  }
  // Шлем с бивнями: шипастый гребень, обод с заклёпками, швы, прорези глаз, наносник, нащёчники, бивни по сторонам.
  {
    const std::string helm = symPath("M50 14C34 14 25 25 25 40V64C25 70 29 74 35 76L41 78V86H50");
    const std::string crest = "M44 15 50 1 56 15Z" + sym("M32 21 31 8.5 40 17Z");
    const std::string tusks = sym(horn({28.5, 68}, {16, 66}, {8, 52}, {10, 27}, 10.5, 1.2));
    c.add("orc-helm", "Шлем с бивнями",
          moved({G::F(tusks), G::XS(sym(notches({28.5, 68}, {16, 66}, {8, 52}, {10, 27}, {0.4, 0.62}, [](double t) { return 10.5 * (1 - std::pow(t, 1.2)); }, -1.2)), 1.3),
                 G::XS(helm + crest, 2.6), G::F(helm + crest), rim(helm, 2, 1.3), G::XS("M26 30C38 26 62 26 74 30M26.5 36C38 32 62 32 73.5 36", 1.5),
                 G::X(sym(lozenge(31.5, 33, 1.3, 1.3) + lozenge(40.5, 30.5, 1.3, 1.3))),
                 G::X(sym("M29.5 45.5C33.5 41.5 40 41.5 45 44.5L44.5 51.5C40.5 54 34 54 30.5 51.5Z") + "M40 66H60L56.5 78H43.5Z"),
                 G::XS("M45.5 36V64M54.5 36V64", 1.5), G::F(sym("M41.5 78 40.5 67 45.5 77Z"))},
                Affine::translate(0, 6)));
  }
}

// ---------------------------------------------------------------- ящеры
void lizards(Catalog& c) {
  c.group("Ящеры");
  // Ящерица (сверху): тело S-образным изгибом, заострённая морда, глаза, гребень ромбами по хребту, лапы
  // с длинными растопыренными пальцами, хвост, свёрнутый в завиток.
  {
    const Pt a0{50, 1.5}, a1{50.5, 15}, a2{45, 28}, a3{47.5, 41};
    const Pt b0{47.5, 41}, b1{50, 54}, b2{57.5, 61}, b3{53, 72};
    const Pt d0{53, 72}, d1{46.5, 86}, d2{63, 98}, d3{78, 86};
    auto wa = [](double t) { return t < 0.28 ? 13 * std::sin(t / 0.28 * kPi / 2) : t < 0.6 ? 13 - 5.5 * (t - 0.28) / 0.32 : 7.5 + 7 * (t - 0.6) / 0.4; };
    auto wb = [](double t) { return 14.5 + 4 * std::sin(t * kPi) - 3 * t; };
    auto wd = [](double t) { return 11.5 * (1 - std::pow(t, 0.85)) + 0.3; };
    const std::string body = band(a0, a1, a2, a3, wa) + band(b0, b1, b2, b3, wb) + band(d0, d1, d2, d3, wd, 48);
    auto limb = [](Pt p, Pt k, Pt f, double w) {
      return band(p, {p.x + (k.x - p.x) * 0.7f, p.y + (k.y - p.y) * 0.7f}, {k.x + (f.x - k.x) * 0.3f, k.y + (f.y - k.y) * 0.3f}, f,
                  [=](double t) { return w - 2.6 * t; });
    };
    auto toes = [](Pt f, double dir) {
      std::string s;
      for (double i : {-1.5, -0.5, 0.5, 1.5}) {
        const double a = dir + 30 * i;
        s += horn(f, polar(f.x, f.y, 3, a), polar(f.x, f.y, 6, a), polar(f.x, f.y, std::fabs(i) < 1 ? 10 : 8.5, a), 3, 1);
      }
      return s;
    };
    const std::string legs = limb({44, 37}, {31, 37}, {26, 27}, 8) + toes({26, 27}, -100) + limb({52, 38}, {64, 40}, {70, 31}, 8) + toes({70, 31}, -70) +
                             limb({50.5, 63}, {37, 64}, {31, 75}, 8.4) + toes({31, 75}, 115) + limb({57, 62}, {69, 63}, {74, 74}, 8.4) + toes({74, 74}, 75);
    std::string spine;
    for (double t : {0.15, 0.4, 0.65, 0.9}) {
      const CurvePt q = bezier(b0, b1, b2, b3, t);
      spine += lozenge(q.x, q.y, 1.6, 2.3);
    }
    for (double t : {0.75, 0.92}) {
      const CurvePt q = bezier(a0, a1, a2, a3, t);
      spine += lozenge(q.x, q.y, 1.3, 1.9);
    }
    for (double t : {0.1, 0.24, 0.38}) {
      const CurvePt q = bezier(d0, d1, d2, d3, t);
      spine += lozenge(q.x, q.y, 1.2, 1.7);
    }
    c.add("lizard-crawl", "Ящерица",
          {G::XS(legs, 2), G::F(legs), G::XS(body, 2.2), G::F(body), G::X(spine + leaf({42.5, 13}, {43.5, 11.8}, {45, 11.5}, {46.5, 12.2}, 2.4, 0.4, 0.5) +
                                                                       leaf({57.5, 13}, {56.5, 11.8}, {55, 11.5}, {53.5, 12.2}, 2.4, 0.4, 0.5)),
           G::XS(notches(d0, d1, d2, d3, {0.5, 0.6, 0.7}, wd, -0.8), 1.2)});
  }
  // глаз с вертикальным зрачком под бровью, ноздря, пасть с зубами.
  {
    const double cx = 58, cy = 50;
    std::string frill = "M" + pt(cx, cy, 24, -150), ribs;
    const int k = 9;
    for (int i = 0; i <= k; i++) {
      const double a = -150 + 300.0 * i / k;
      frill += "L" + pt(cx, cy, 41.5, a);
      if (i < k) frill += "Q" + pt(cx, cy, 33, a + 150.0 / k) + " " + pt(cx, cy, 41.5, a + 300.0 / k);
      ribs += "M" + pt(cx, cy, 12, a) + "L" + pt(cx, cy, 36, a);
    }
    frill += "L" + pt(cx, cy, 24, 150) + "Z";
    const std::string head = "M5 51C6 46 12 42 20 40C29 37.5 39 35 47 36C55 37 60 43 60 50C60 58 54 62 46 62C37 62 25 60.5 15 58.5C9 57 5 55 5 51Z";
    c.add("lizard-head", "Голова ящера",
          {G::F(frill), G::XS(ribs, 1.5), G::XS(inset(frill, 3.5), 1.2), G::XS(head + "M22 39.5 26 31 30.5 37.5ZM31 37 35.5 28.5 38.5 36Z", 3.6),
           G::F(head + "M22 39.5 26 31 30.5 37.5ZM31 37 35.5 28.5 38.5 36Z"), G::XS("M7.5 52.5C17 54 30 54 42 51.5", 2),
           G::XS("M12 53.5 13.5 56.5M18 54 19.5 57M24 54.2 25.5 57.2M30 54 31.5 57", 1.2), G::X(leaf({34.5, 44.5}, {36.5, 42.6}, {39.5, 42.2}, {42, 43.8}, 4.4, 0.6, 0.5)),
           G::F(xf(ell(0, 0, 0.8, 2.2), place(38.3, 43.6))), G::X(leaf({8.5, 46.5}, {9.5, 45.6}, {10.8, 45.4}, {12, 45.8}, 1.8, 0.4, 0.5)),
           G::XS("M30 40C34 37.5 40 37 44.5 38.5M47 44C51 47 52 53 49 58", 1.4)});
  }
  // Крокодил в профиль: вытянутая морда с зубами и глазом на бугре, тело со щитками, согнутые лапы с когтями,
  // хвост дугой вверх и назад с гребнем зубцов.
  {
    const std::string body = "M3 60C3 57.5 5 56 8 55.5L28 53C32 49.5 37 47.5 43 47.5C55 47 65 46 73 43C80 40 85 33 87 25C88.5 19 87 13 82 9"
                             "C88 10 93.5 15 95 22C97 32 93 43 85 50C80 54.5 75 58 72 62C66 67 56 69.5 46 69.5C38 69.5 32 68 27 65.5"
                             "L8 64.5C5 64 3 62.5 3 60Z";
    std::string scutes;
    for (int i = 0; i < 6; i++) scutes += xf("M-2.2 0 0 -4 2.2 0Z", place(float(36 + 7 * i), float(48 - 0.6 * i), 0));
    const Pt t0{73, 43}, t1{80, 40}, t2{85, 33}, t3{87, 25};
    for (double t : {0.15, 0.45, 0.75})
      scutes += xf("M-2 0 0 -3.6 2 0Z", place(bezier(t0, t1, t2, t3, t).x, bezier(t0, t1, t2, t3, t).y, -20 - 40 * float(t)));
    auto leg = [](Pt a, Pt k, Pt f) { return band(a, k, k, f, [](double t) { return 7 - 3 * t; }); };
    const std::string legs = leg({36, 66}, {32, 74}, {38, 80}) + leg({62, 66}, {60, 74}, {66, 80}) + "M33 79H44L45.5 83H31.5Z" + "M61 79H72L73.5 83H59.5Z";
    c.add("lizard-croc", "Крокодил",
          moved({G::F(scutes), G::XS(legs, 2), G::F(legs), G::XS(body, 2), G::F(body), G::X(leaf({27, 51.5}, {28.5, 50.5}, {30.5, 50.4}, {32, 51.2}, 2.6, 0.4, 0.5)),
                 G::X(circ(6.5, 57, 1.1)),
                 engr("M6 60 9 58 12 61 15 58.5 18 61.5 21 59 24 62 27 59.5M30 56C40 58 52 58 62 56M68 60C76 56 83 49 87 40M44 62.5 47 66M52 62.5 55 66.5M60 61.5 63 65.5",
                       1.3),
                 engr("M33 81.5V83M37 81.5V83M41 81.5V83M61 81.5V83M65 81.5V83M69 81.5V83", 1.2)},
                Affine::translate(0, 5)));
  }
  // Храм-пирамида: ступенчатые ярусы с откосами и резными блоками, лестница с балюстрадами-змеями, святилище
  // с гребнем, солнечный диск с лучами.
  {
    std::string blocks;
    const double rows[][3] = {{88.5, 11, 4}, {75, 20, 3}, {62, 29, 2}};
    for (const auto& r : rows)
      for (int k = 0; k < int(r[2]); k++) blocks += lozenge(r[1] + 2.4 + 6.4 * k, r[0], 1.8, 1.8);
    c.add("lizard-pyramid", "Храм-пирамида",
          {G::F(glory(50, 10, 8.5, 15, 12, -180, 0, 9, 2.4)), G::XS(circ(50, 10, 7), 2.4), G::F(circ(50, 10, 7)), G::X(circ(50, 10, 3.6)),
           G::F("M4 97L8 82H92L96 97Z" + std::string("M13 82L17 69H83L87 82Z") + "M22 69L26 56H74L78 69Z" + "M31 56L34.5 44H65.5L69 56Z" +
                "M38 44V31H62V44Z" + "M35 31.5 50 21.5 65 31.5Z"),
           G::X(rrect(46, 34, 8, 10, 1.5) + sym(blocks)), G::XS("M44 97V44M56 97V44M8.5 82H91.5M17.5 69H82.5M26.5 56H73.5M35 44H65M38 31.5H62", 1.6),
           G::XS("M44 91H56M44 85H56M44 79H56M44 73H56M44 67H56M44 61H56M44 55H56M44 49H56", 1.3)});
  }
  // След ящера: три вытянутых пальца с подушечками и длинными серповидными когтями, чешуйчатая пятка.
  {
    const Pt p0{50, 72}, p1{50, 55}, p2{49.5, 38}, p3{50, 22};
    auto wf = [](double t) { return 15 - 6 * t; };
    const std::string toe = band(p0, p1, p2, p3, wf) + horn({50, 23}, {50, 15}, {49, 9}, {47, 1.5}, 8.4, 1.1);
    const std::string pads = notches(p0, p1, p2, p3, {0.42, 0.72}, wf, -1);
    std::string all = toe, cuts = pads;
    for (float d : {-38.f, 38.f}) {
      all += xf(toe, rotAt(d, 50, 74));
      cuts += xf(pads, rotAt(d, 50, 74));
    }
    std::string scales;
    for (Pt p : {Pt{43.5, 77}, Pt{50, 77}, Pt{56.5, 77}, Pt{46.7, 83}, Pt{53.3, 83}, Pt{50, 89}}) scales += lozenge(p.x, p.y, 2, 2.4);
    c.add("lizard-track", "След ящера",
          moved({G::F(ell(50, 79, 15, 16) + all), G::XS(cuts, 1.5), G::X(scales),
                 G::XS(xf("M50 23V10", rotAt(-38, 50, 74)) + "M50 23V10" + xf("M50 23V10", rotAt(38, 50, 74)), 1.2)},
                Affine::translate(0, 1)));
  }
  // Пернатый змей: голова с раскрытой пастью и гребнем из заострённых перьев, тело S-образными изгибами
  // с брюшными щитками, хвост — веер перьев.
  {
    const Pt a0{30, 27}, a1{48, 38}, a2{80, 26}, a3{79, 49};
    const Pt b0{79, 49}, b1{78, 70}, b2{30, 56}, b3{28, 74};
    const Pt d0{28, 74}, d1{26, 88}, d2{56, 97}, d3{78, 87};
    auto plates = [](Pt p0, Pt p1, Pt p2, Pt p3, double w0, double w1) {
      return notches(p0, p1, p2, p3, {0.15, 0.32, 0.5, 0.68, 0.85}, [=](double t) { return w0 + (w1 - w0) * t; }, -1.2);
    };
    std::vector<L> v;
    layeredFeathers(v, {quill({34, 24}, {62, 24}, 7.5, 0.04), quill({31, 20}, {60, 11}, 8.5, 0.05), quill({27, 17}, {50, 2}, 9, 0.06),
                        quill({23, 16}, {34, 1.5}, 7.5, 0.06)},
                    2.0f, false);
    const std::string body = taper(a0, a1, a2, a3, 12.5, 11) + taper(b0, b1, b2, b3, 11, 8.5) + taper(d0, d1, d2, d3, 8.5, 4);
    const std::string head = "M2 21C7 16.5 15 14.5 23 14.5C31 14.5 36 18.5 36 25C36 31 31.5 34.5 25 34.5L15 34L4 36.5 10 29.5 3 27Z";
    std::vector<std::string> tail = {quill({77, 87}, {97, 72}, 6.5, 0.05), quill({77, 88}, {99, 84}, 6.5, 0), quill({76, 89}, {95, 97}, 6.5, -0.05)};
    layeredFeathers(v, tail, 1.8f, false);
    v.push_back(G::XS(body, 2));
    v.push_back(G::F(body));
    v.push_back(G::XS(plates(a0, a1, a2, a3, 12.5, 11) + plates(b0, b1, b2, b3, 11, 8.5) + plates(d0, d1, d2, d3, 8.5, 4), 1.3));
    v.push_back(G::XS(head, 2.4));
    v.push_back(G::F(head));
    v.push_back(G::X(leaf({20, 21.5}, {22.5, 20}, {25.5, 19.8}, {28, 21}, 3.6, 0.6, 0.5) + "M3.5 27.5 20 28.5 9.5 31Z"));
    v.push_back(G::F(xf(ell(0, 0, 0.7, 1.9), place(24, 21))));
    v.push_back(G::XS("M17 17.5C21 16 26 16.2 30 18.5M26 30.5C29 31.5 31.5 31.5 34 30", 1.3));
    v.push_back(G::F("M10 27.7 11.2 30.2 12.4 28ZM14.5 28 15.6 30.1 16.7 28.2Z"));
    c.add("lizard-quetzal", "Пернатый змей", v);
  }
  // Солнечный календарь: кольцо с насечками, двадцать знаков дней, восемь лучей-клиньев, строгий лик
  // с прямоугольными глазами и языком-клинком.
  {
    std::string signs, dots, notchesS;
    for (int i = 0; i < 20; i++) {
      signs += "M" + pt(50, 50, 31.5, 18.0 * i) + "L" + pt(50, 50, 41.5, 18.0 * i);
      const Pt d = polar(50, 50, 36.5, 18.0 * i + 9);
      dots += i % 2 ? lozenge(d.x, d.y, 1.8, 1.8) : rrect(d.x - 1.6, d.y - 1.6, 3.2, 3.2, 0.5);
    }
    for (int i = 0; i < 24; i++) notchesS += lozenge(polar(50, 50, 44, 15.0 * i + 7.5).x, polar(50, 50, 44, 15.0 * i + 7.5).y, 1.2, 1.2);
    std::string sunRays;
    for (int i = 0; i < 8; i++) {
      const double a = -90 + 45 * i, r = i % 2 ? 27 : 30.5;
      sunRays += "M" + pt(50, 50, 18.5, a - 13) + "L" + pt(50, 50, r, a) + "L" + pt(50, 50, 18.5, a + 13) + "Z";
    }
    c.add("lizard-calendar", "Солнечный календарь",
          {G::F(circ(50, 50, 47)), G::XS(circ(50, 50, 41.5), 1.5), G::XS(signs, 1.4), G::X(dots + notchesS),
           G::X(circ(50, 50, 30.8)), G::F(sunRays + circ(50, 50, 17.5)),
           G::X(sym(rrect(40.5, 43.5, 6.5, 3.5, 1) + lozenge(37.5, 51, 1.8, 1.8)) + rrect(43.5, 55, 13, 3.6, 1.6)),
           G::F("M47.5 57H52.5L51.2 66 50 68.5 48.8 66Z"), G::XS("M38.5 39.5H61.5", 1.4)});
  }
  // Раптор в беге: вытянутый череп с зубами, S-образная шея, поджарое тело, когтистые лапки, ноги с серповидным
  // когтем, хвост-противовес, полосы.
  {
    const std::string head = "M3 26C7 20.5 15 18 23 18C29 18 33 21 33.5 25.5C34 29.5 30 32.5 25 33L18 33.5 6 31.5C4 30.5 3 28.5 3 26Z";
    const std::string neck = band({30, 28}, {38, 32}, {38, 40}, {43, 46}, [](double t) { return 7 + 4 * t; });
    const std::string body = xf(ell(0, 0, 17, 10.5), place(53, 50, -12)) + band({64, 46}, {76, 41}, {87, 35}, {99, 29}, [](double t) { return 11 * (1 - t) + 0.4; });
    const std::string farLeg = band({60, 55}, {66, 64}, {70, 72}, {71, 80}, [](double t) { return 6.5 - 2.5 * t; }) +
                               band({71, 79}, {70, 84}, {68, 88}, {66, 92}, [](double t) { return 4 - 1 * t; }) + "M63 90.5H71.5L72 94H62Z";
    const std::string nearLeg = xf(ell(0, 0, 7.5, 12), place(52, 60, 28)) + band({47, 68}, {42, 76}, {38, 82}, {35, 88}, [](double t) { return 6 - 2 * t; }) +
                                "M28 87H37.5L38.5 91H27Z" + horn({36, 86.5}, {37, 82}, {39.5, 80}, {43, 80.5}, 2.6, 1.1);
    const std::string arm = band({45, 52}, {41, 55}, {38, 57}, {35, 57}, [](double t) { return 3.4 - 1 * t; }) + horn({35.5, 56.5}, {33, 57}, {31, 59}, {30, 62}, 2.4, 1.1) +
                            horn({35.5, 57}, {33, 56}, {31, 55.5}, {28.5, 56}, 2.2, 1.1);
    c.add("lizard-raptor", "Раптор",
          moved({G::F(farLeg), G::XS(body + neck + head, 2.2), G::F(body + neck + head), G::XS(arm + nearLeg, 2.2), G::F(arm + nearLeg),
                 G::X("M2 28.5 20 30 4 32.5Z" + leaf({21, 23.3}, {22.6, 22.2}, {24.8, 22}, {26.6, 22.8}, 2.8, 0.4, 0.5)),
                 G::F("M7 29.3 8.2 31.6 9.4 29.6ZM11.5 29.6 12.6 31.6 13.7 29.8Z"), engr("M18 20.5C22 19.2 26.5 19.4 30 21.5", 1.3),
                 engr("M47 41 50 47.5M54 39.5 56.5 46M61 38.5 63 44.5M70 37.5 71.5 42.5M79 35 80 39.5M88 32 88.7 35.5", 1.5)},
                Affine::translate(0, -4)));
  }
  // Маска ящера: гребень из шипов, перепончатые уши-плавники с лучами, глаза с вертикальным зрачком под бровью,
  // чешуя рядами, ноздри, зубчатый оскал, кайма.
  {
    const std::string mask = symPath("M50 11C38 11 30 21 28 35L24.5 60C24.5 72 36 84 50 92");
    std::string scales;
    for (int r = 0; r < 3; r++)
      for (int k = -r; k <= r; k++) {
        const double x = 50 + 6 * k, y = 18 + 5 * r;
        scales += "M" + n(x - 2.6) + " " + n(y) + "L" + n(x) + " " + n(y + 3) + "L" + n(x + 2.6) + " " + n(y);
      }
    const std::string fins = sym("M29 34 8 26 14.5 38.5 4.5 45 15.5 50 8 59 25.5 60Z");
    c.add("lizard-mask", "Маска ящера",
          {G::F(sym("M41 13.5 37 1.5 46.5 11Z") + "M46 11 50 0 54 11Z"), G::XS(fins, 2), G::F(fins), G::XS(sym("M27.5 39 12 31.5M26.5 49 9 46.5M25.5 56 13 57.5"), 1.4),
           G::XS(mask, 2.6), G::F(mask), rim(mask, 2, 1.3), G::XS(scales, 1.4),
           G::X(sym(leaf({31.5, 42.5}, {36, 44}, {41, 46.5}, {46.5, 50.5}, 5.2, 0.6, 0.5))), G::F(sym(xf(ell(0, 0, 1, 3.2), place(39.5, 46.2, -10)))),
           G::XS(sym("M29.5 37.5 47 44.5"), 1.8), G::X(sym(xf(leaf({-1.5, 0}, {-0.5, -1}, {0.5, -1}, {1.5, 0}, 2.2, 0.4, 0.5), place(46.5, 68, 70)))),
           G::XS("M50 53V62M34 77 38 81 42 77 46 81 50 77 54 81 58 77 62 81 66 77", 1.6)});
  }
  // Яйцо в гнезде: чешуйчатая скорлупа фестонами, зигзаг трещины, гнездо из перекрещённых прутьев.
  {
    const std::string egg = "M50 5C33 5 22 32 22 54C22 73 34 86 50 86C66 86 78 73 78 54C78 32 67 5 50 5Z";
    std::string sc;
    for (int r = 0; r < 6; r++) {
      const double y = 40 + 7.5 * r, off = r % 2 ? 5.5 : 0;
      for (double x = 17 + off; x < 84; x += 11) sc += "M" + n(x - 5.5) + " " + n(y) + "A5.5 5.5 0 0 0 " + n(x + 5.5) + " " + n(y);
    }
    std::string twigs;
    const double tw[][4] = {{4, 82, 52, 97}, {48, 97, 96, 82}, {10, 92, 62, 86}, {38, 86, 90, 92}, {6, 86, 40, 95.5}, {60, 95.5, 94, 86}};
    for (const auto& t : tw) twigs += band({float(t[0]), float(t[1])}, {float(t[0] + (t[2] - t[0]) * 0.33), float(t[1] + (t[3] - t[1]) * 0.33 + 2)},
                                           {float(t[0] + (t[2] - t[0]) * 0.66), float(t[1] + (t[3] - t[1]) * 0.66 + 2)}, {float(t[2]), float(t[3])},
                                           [](double u) { return 3.6 * std::pow(std::sin(kPi * u), 0.4) + 0.3; });
    c.add("lizard-egg", "Яйцо",
          {G::F(egg), rim(egg, 2, 1.3), G::XS(sc, 1.5), G::XS("M31 25 37.5 30 43.5 24 50.5 31 56.5 25 62.5 30 68 26", 1.8),
           G::XS(twigs, 2.4), G::F(twigs)});
  }
}

// ---------------------------------------------------------------- зверолюды
void beastmen(Catalog& c) {
  c.group("Зверолюды");
  // Минотавр: бычья голова анфас с рогами-полумесяцами в кольцах, чуб языками, уши, гневные глаза, ноздри,
  // кольцо в носу; могучие плечи и грудь с гравировкой мышц.
  {
    const std::string head = symPath("M50 19C43 19 37 20.5 34 23L31.5 30 33 44C33.5 50 35 55 36 60C35.5 66 36 71 39 74C42 77 46 78 50 78");
    const Pt h0{35.5, 27}, h1{23, 25}, h2{11, 19}, h3{8, 3};
    const std::string hornL = horn(h0, h1, h2, h3, 10.5);
    const std::string ear = leaf({33.5, 33}, {28, 32}, {22, 34}, {15, 39}, 6.5, 2, 0.6);
    const std::string body = "M3 97.5C5 85 15 77 29 73L40 71H60L71 73C85 77 95 85 97 97.5Z";
    c.add("beast-minotaur", "Минотавр",
          {G::F(body), G::XS("M50 81V97.5" + sym("M26 85C34 89.5 43 89.5 50 86M12 89C16 83 22 79 29 77M38 92 41 97"), 1.6), G::XS(sym(hornL + ear), 2),
           G::F(sym(hornL + ear)), G::XS(sym(notches(h0, h1, h2, h3, {0.3, 0.46, 0.62}, [](double t) { return 10.5 * (1 - std::pow(t, 1.3)); }, -1.2)), 1.3),
           G::XS(head, 2.6), G::F(head), G::F(flameTongue({50, 25}, {46, 20}, {53, 15}, {50, 9}, 7, 0.35) + sym(flameTongue({45, 25}, {41, 21}, {44, 16}, {41, 11.5}, 5.5, 0.35))),
           G::XS(sym("M34.5 36.5 46 41.5M37 55C40 57 43.5 58 47 58"), 1.6), G::X(sym(leaf({36, 41}, {39, 42}, {42.5, 43.5}, {45.8, 45.6}, 3.4, 0.5, 0.45) +
                                                                                     leaf({43, 67}, {43.8, 65.4}, {45, 64.4}, {46.6, 64}, 2.6, 0.4, 0.5))),
           G::XS("M39.5 60.5C44 59 56 59 60.5 60.5", 1.3), G::XS(circ(50, 74.5, 5.4), 5.4), G::S(circ(50, 74.5, 5.4), 2.4)});
  }
  // Кентавр-стрелец: конское тело с поднятой передней ногой и хвостом языками, торс лучника, натянутый лук
  // с загнутыми концами, стрела с оперением.
  {
    auto leg = [](Pt a, Pt k, Pt f) { return band(a, k, k, f, [](double t) { return 7.2 - 3.4 * t; }); };
    const std::string legs = leg({48, 64}, {38, 70}, {41, 80}) + leg({53, 66}, {52, 79}, {51, 91}) + leg({80, 63}, {88, 75}, {84, 91}) +
                             leg({73, 65}, {75, 79}, {71, 91});
    const std::string hooves = xf("M-2.6 0H2.6L3.4 3.8H-3.4Z", place(41.2, 79.5, 15)) + "M48 90.5H54.5L55 94H47.5Z" + "M81 90.5H87.5L88 94H80.5Z" +
                               "M68 90.5H74.5L75 94H67.5Z";
    const std::string horse = xf(ell(0, 0, 21, 10.5), place(65, 58, -3)) + circ(48, 57.5, 9.5);
    const std::string torso = "M38.5 54C36.5 46 36.5 38 39 30.5H49.5C51 38 51 46 50 54Z";
    const std::string head = circ(44, 22, 6.2) + flameTongue({46.5, 18.5}, {52, 19}, {55, 24}, {59, 30}, 5, 0.3);
    const std::string bow = band({17, 5}, {3, 18}, {3, 38}, {17, 51}, [](double t) { return 1.6 + 3.2 * std::sin(kPi * t); }) +
                            horn({17, 5}, {18.5, 6}, {19.5, 7.5}, {20, 9.5}, 1.8, 1) + horn({17, 51}, {18.5, 50}, {19.5, 48.5}, {20, 46.5}, 1.8, 1);
    const std::string arms = band({41, 32}, {32, 30}, {20, 28.5}, {8.5, 28}, [](double t) { return 4.6 - 1.2 * t; }) +
                             band({47, 32}, {54, 30}, {57, 27}, {50, 26.5}, [](double t) { return 4.4 - 1 * t; });
    c.add("beast-centaur", "Кентавр",
          moved({G::F(flameTongue({85, 52}, {96, 54}, {99, 68}, {92, 83}, 10, 0.25)), G::XS("M89 56C94 61 95 69 92.5 76", 1.3), G::XS(horse + legs, 2),
                 G::F(horse + legs + hooves), G::XS("M50 64C56 68 64 70 74 69M80 50C83 54 84 59 83 64", 1.4), G::XS(torso + head + arms, 2.2),
                 G::F(torso + head + arms + bow), G::S("M20 9.5 50 27 20 46.5", 1.2), G::S("M50 27H6", 1.6),
                 G::F("M0.5 27 8 23.5V30.5Z" + std::string("M48 27 54.5 23 52.5 27 54.5 31Z")), G::X(circ(41.2, 21.2, 1.2)), G::XS("M39 41.5H50", 1.3)},
                Affine::translate(0, 2)));
  }
  // Сатир в профиль: изогнутый кольчатый рог, острое ухо, кудри языками, строгий нос, глаз под бровью,
  // острая бородка прядями.
  {
    const Pt h0{44, 22}, h1{47, 7}, h2{65, 1.5}, h3{86, 8};
    const std::string hornS = horn(h0, h1, h2, h3, 12.5);
    const std::string head = "M47 14C40 14 35 18 33 24L30.5 33 22 43.5C21 45 21.5 46.5 23 47L27.5 47.8 26.5 51 28.5 52.8 27.5 55.5 30 58"
                             "C30.5 63 32 68 35.5 72L40 95 44 79C47 74 52 72 56 71C64 69 70 62 72 52C74 40 72 28 66 21C61 16 54 14 47 14Z";
    const std::string curls = flameTongue({62, 30}, {70, 30}, {76, 36}, {79, 44}, 8, 0.3) + flameTongue({66, 44}, {74, 46}, {78, 53}, {79, 62}, 8, 0.3) +
                              flameTongue({63, 58}, {70, 62}, {72, 69}, {71, 78}, 7.5, 0.3);
    const std::string ear = flameTongue({55, 37}, {62, 33}, {69, 27}, {77, 25}, 7, 0.35);
    c.add("beast-satyr", "Сатир",
          moved({G::F(curls), G::XS(head, 2.4), G::F(head), G::XS(ear, 2), G::F(ear), G::XS(hornS, 2.4), G::F(hornS),
                 G::XS(notches(h0, h1, h2, h3, {0.3, 0.45, 0.6, 0.74}, [](double t) { return 12.5 * (1 - std::pow(t, 1.3)); }, -1.2), 1.3),
                 G::X(leaf({33.5, 33.8}, {35.5, 33}, {38, 33}, {40.5, 34}, 2.8, 0.4, 0.5) + "M23.5 44.5 27.5 44.6 25 46.3Z"),
                 G::XS("M30.5 29.5C34 27.8 38 27.6 42 29M28 50.2C29.5 50.8 31 51 32.5 50.6M33 62 37.5 80M38 66 40.5 78M43 68 43.5 74M58 38C62 36.5 66 34 70 30",
                       1.4)},
                Affine::translate(-2, 1)));
  }
  // Гарпия распростёртая: геральдические крылья с маховыми перьями и кроющими, девичья голова с ниспадающими
  // прядями, стан, веер хвоста, птичьи ноги с когтями.
  {
    const Wing w = heraldicWing({43, 40}, {32, 28}, {18, 16}, {5, 6}, {{1.5, 18}, {2, 30}, {4.5, 42}, {9, 53}, {15.5, 62}, {23.5, 69}, {32.5, 74}}, 0.3, 0.6, 7.5);
    std::vector<std::string> tail;
    for (Pt t : {Pt{38, 86}, Pt{44, 89}, Pt{50, 90}, Pt{56, 89}, Pt{62, 86}}) tail.push_back(quill({50, 62}, t, 6.6));
    std::vector<L> v{G::F(sym(w.fill)), G::XS(sym(w.cuts), 1.3)};
    layeredFeathers(v, tail, 1.8f, false);
    const std::string legs = sym(band({46, 60}, {44, 68}, {42, 74}, {40.5, 82}, [](double t) { return 4.6 - 1.6 * t; }) +
                                 horn({40.5, 81}, {37, 83}, {34, 85.5}, {31, 89.5}, 2.8, 1.1) + horn({40.5, 81.5}, {39.5, 85}, {39, 88.5}, {39, 93}, 2.8, 1.1) +
                                 horn({41, 81.5}, {42.5, 84.5}, {44, 87}, {45.5, 90}, 2.4, 1.1));
    const std::string torso = "M43 31H57L55.5 50C54 55 53.5 59 54 63H46C46.5 59 46 55 44.5 50Z";
    const std::string hair = sym(flameTongue({45.5, 14}, {40, 22}, {40, 32}, {35.5, 42}, 7, 0.3) + flameTongue({46.5, 18}, {43.5, 26}, {44, 34}, {41, 44}, 5, 0.3));
    v.push_back(G::XS(legs + torso, 2.2));
    v.push_back(G::F(legs + torso));
    v.push_back(G::XS(hair, 2));
    v.push_back(G::F(hair));
    v.push_back(G::XS(ell(50, 19.5, 6.2, 7.6), 2.2));
    v.push_back(G::F(ell(50, 19.5, 6.2, 7.6)));
    v.push_back(G::X(sym(leaf({46.2, 19}, {47, 18.3}, {48, 18.3}, {48.8, 19}, 1.6, 0.3, 0.5)) + "M48.6 24.2H51.4L50 25Z"));
    v.push_back(G::XS("M45.5 13.5C48.5 11.5 51.5 11.5 54.5 13.5M47 45.5C48.5 47 51.5 47 53 45.5M47 51 50 54 53 51", 1.3));
    c.add("beast-harpy", "Гарпия", v);
  }
  // Русалка: лицо в обрамлении длинных волос языками, поднятая рука с зеркалом, стройный стан, хвост изгибом
  // с чешуёй фестонами, раздвоенный плавник с лучами.
  {
    const Pt t0{41, 47}, t1{38.5, 68}, t2{48, 84}, t3{66, 86};
    auto wt = [](double t) { return 16.5 - 8.5 * t; };
    std::string scales;
    for (double t : {0.14, 0.3, 0.46, 0.62, 0.78})
      for (int s : {-1, 1}) {
        const CurvePt q = bezier(t0, t1, t2, t3, t);
        const double w = wt(t) * 0.22 * s;
        scales += xf("M-2.4 0A2.4 2.4 0 0 0 2.4 0", place(float(q.x + q.nx * w), float(q.y + q.ny * w), float(std::atan2(q.nx, -q.ny) * 180 / kPi) + 90));
      }
    const std::string tail = band(t0, t1, t2, t3, wt) + band(t3, {76, 87}, {84, 80}, {86, 68}, [](double t) { return 8 - 3 * t; });
    const std::string fin = flameTongue({86, 69}, {88, 60}, {92, 53}, {98, 48}, 8.5, 0.3) + flameTongue({86, 69}, {93, 68}, {97, 72}, {99.5, 80}, 8.5, 0.3);
    const std::string torso = "M33 27.5C34.5 33 37.5 37 37.5 42C37.5 45 36 47 35.5 49.5H46.5C46 47 44.5 45 44.5 42C44.5 37 47.5 33 48.5 27.5C44.5 25 37 25 33 27.5Z";
    const std::string hair = flameTongue({36, 13}, {29, 24}, {33, 36}, {30, 48}, 9, 0.25) + flameTongue({45.5, 13}, {53, 22}, {50, 34}, {55, 46}, 9, 0.25) +
                             flameTongue({45, 10}, {54, 10}, {60, 18}, {64, 30}, 5.5, 0.3);
    const std::string arm = band({34.5, 29}, {28, 29}, {23.5, 22}, {21, 14.5}, [](double t) { return 4.6 - 1.2 * t; }) +
                            band({47, 29}, {51, 34}, {50, 40}, {46, 44}, [](double t) { return 4.4 - 1 * t; });
    c.add("beast-mermaid", "Русалка",
          moved({G::XS(hair, 2), G::F(hair), engr(tail + fin, 2), G::F(tail + fin), engr(scales, 1.3), engr("M87 66.5 96 51.5M88 70 97.5 78.5M86.5 64 93 57.5", 1.3),
                 engr(torso + arm, 2), G::F(torso + arm), engr(ell(40.5, 16.5, 5.4, 6.8), 2), G::F(ell(40.5, 16.5, 5.4, 6.8)),
                 G::XS(circ(15.5, 8.5, 6), 2.4), G::S(circ(15.5, 8.5, 5.3), 2), G::F(rrect(18, 12.5, 2.6, 4.4, 1)),
                 G::X(leaf({37.2, 16}, {37.9, 15.4}, {38.7, 15.4}, {39.4, 15.9}, 1.5, 0.3, 0.5) + leaf({43.8, 16}, {43.1, 15.4}, {42.3, 15.4}, {41.6, 15.9}, 1.5, 0.3, 0.5) + "M39.6 20.3H41.4L40.5 21Z"), engr("M36.5 47H46", 1.3)},
                Affine::translate(-2.5, 3)));
  }
  // Гнолл: голова гиены в профиль с оскаленной пастью и клыками, круглое ухо, грива языками по хребту,
  // пятна, рваный срез шеи.
  {
    const std::string head = "M3.5 43C8 39.5 16 37 24 34C30 31.5 36 26.5 42 22.5C47 19.5 53 18.5 58 19.5C59 12.5 62.5 6.5 68 6.5C73 7 74 14 69.5 22"
                             "L77 26 74 31.5 84 34 79 40 90 45 83 50 93 56 85 60 96 68 87 71 97 80 88 83 96 93 87 97.5H45C46 89 45 80 42 74"
                             "C38 69 33 66 27 64.5L11 62C7 61 5 59 5 57L9 53 4 50Z";
    std::string spots;
    for (Pt p : {Pt{47, 37}, Pt{56, 31}, Pt{59, 45}, Pt{66, 55}, Pt{55, 59}, Pt{73, 66}, Pt{63, 73}, Pt{76, 81}, Pt{65, 87}, Pt{53, 81}})
      spots += lozenge(p.x, p.y, 2, 2.4);
    c.add("beast-gnoll", "Гнолл",
          moved({G::F(head), rim(head, 2, 1.2), G::XS("M64 18.5C63 14 64.5 10.5 67.5 9.5", 1.6),
                 G::X("M6 50.5 32 54.5 9.5 58Z" + leaf({35, 30.8}, {37.5, 29.2}, {40.5, 28.8}, {43, 29.6}, 3, 0.4, 0.5) + spots + ell(8, 43.5, 1.6, 1.2)),
                 G::F("M12 51.5 13.5 55 15 51.8ZM18 52.2 19.5 55.6 21 52.5ZM14.5 57.4 16 54.4 17.5 57.6Z"),
                 G::XS("M30 28.5C34 26.5 39 25.8 43.5 27M21 60C25 61.5 29 61.5 33 60", 1.4)},
                Affine::translate(0, 0)));
  }
  // Человек-волк: воет на полную луну с кратерами — вскинутая морда с раскрытой пастью и клыками, острое ухо,
  // глаз под бровью, грива и грудь языками шерсти.
  {
    const std::string wolf = "M3 6C12 12 22 20 30 27C34 24 39 23 44 24L55 4.5 57.5 27C60 31 62 35 63 40L72 40 65 46 75 51 67 56 76 62 68 66 75 72 67 74"
                             "C68 82 68 90 68 97.5H6C6 90 7 84 9 79L3 75 11 72 5 66 13 63 9 57 17 55C19 51 21 47 24 44"
                             "C17 38 10 33 3 29L3.5 25C11 27 19 31 27 35C21 27 13 19 6 12L3 10Z";
    c.add("beast-werewolf", "Человек-волк",
          {G::F(circ(66, 42, 32)), G::X(circ(83, 22, 5) + circ(90, 47, 3.6) + circ(82, 62, 3) + circ(90, 66, 2.2) + circ(70, 14, 2.4)),
           G::XS(wolf, 3.6), G::F(wolf), G::X(leaf({33, 31.5}, {35.5, 30.5}, {38.5, 30.6}, {41, 31.8}, 3, 0.4, 0.5)),
           G::XS("M31.5 27.5C35 26 40 26 44 27.5M51 11 52.5 23M28 54C33 59 40 62 48 62M20 80C26 85 34 87 43 87M52 34C57 40 59 47 59 55"
                 "M8.5 8.5C14 13 20 18 25 23",
                 1.4),
           G::F("M9 13 14.5 16.5 11 18.5ZM16 20 20.5 23 17.5 25ZM6.5 28 11 28.4 9 24.6Z")});
  }
  // Минотавр с секирой: рогатая бычья голова, могучий торс, рука на бедре, лабрис в поднятой руке, набедренник,
  // ноги с копытами.
  {
    auto symAt = [](const std::string& d) { return d + mirrorX(d, 58); };
    const std::string blade = "M16 11C11 9 6.5 6 4 2.5C1 10 1 27 4 34C6.5 30.5 11 27 16 25Z";
    const std::string labrys = blade + mirrorX(blade, 18);
    const std::string figure = "M58 10C51.5 10 48 14 48 19.5L50 31C51 36 54 39 58 39C62 39 65 36 66 31L68 19.5C68 14 64.5 10 58 10Z" +
                               symAt(horn({49.5, 17}, {41, 15}, {36.5, 9.5}, {36, 1.5}, 7, 1.2) + "M48 19 40 21 48 25Z") +
                               "M40 42C44 39 51 38 58 38C65 38 72 39 76 42L80 52 74 68H42L36 52Z" + "M41 66H75L77 73H39Z" + "M50 73H66L63 86H53Z" +
                               symAt("M43 72L38.5 91H50.5L53.5 74Z" + std::string("M37 90.5H52L52.5 96.5H36.5Z"));
    const std::string arms = band({40, 45}, {33, 50}, {28, 53}, {21, 46}, [](double t) { return 8 - 2.4 * t; }) +
                             band({76, 45}, {85, 52}, {87, 58}, {76, 64}, [](double t) { return 8 - 2.4 * t; });
    c.add("beast-bullman", "Минотавр с секирой",
          moved({G::F(rrect(16.2, 3, 3.6, 93, 1.5) + "M14.5 96H21.5L18 99.5Z"), G::XS(labrys, 2), G::F(labrys), G::XS("M6.5 9C9.5 15 9.5 22 6.5 28M29.5 9C26.5 15 26.5 22 29.5 28", 1.4),
           G::XS(figure + arms, 2.4), G::F(figure + arms), G::F(circ(19.5, 45, 4.6)), G::XS("M14.5 43H24.5M14.5 47H24.5", 1.3),
           G::X(symAt(leaf({50.5, 23}, {52, 24}, {53.5, 25.2}, {55, 26.6}, 2.4, 0.4, 0.5) + leaf({55, 34.5}, {55.6, 33.4}, {56.4, 32.6}, {57.2, 32.2}, 1.8, 0.4, 0.5)) +
                "M44 92.5V96.5M72 92.5V96.5"),
           G::S(circ(58, 38.5, 3.2), 1.6),
           G::XS(symAt("M47 50C50.5 53.5 54.5 54 58 52") + "M58 53V65M52 58H64M52 62.5H64M42.5 69.5H73.5" + symAt("M41 12 39 18M38 7 35.5 11"), 1.4)},
                Affine::translate(4, 0)));
  }
  // Мантикора идущая: львиное тело, грива языками вокруг лика, перепончатое крыло с пальцами, членистый хвост
  // скорпиона дугой с жалом, лапы с когтями.
  {
    const std::string wing = "M44 46C46 35 50 23 56 11L59 3 60 12C64 15 68 18 73 19Q66 25 68 33Q60 33 60 41Q54 41 52 48Z";
    const Pt s0{80, 52}, s1{97, 44}, s2{99, 13}, s3{80, 9};
    auto ws = [](double t) { return 11 - 5 * t; };
    const std::string tail = band(s0, s1, s2, s3, ws) + horn({80.5, 9}, {74, 8}, {70, 12}, {68.5, 21}, 6, 1.1);
    const std::string body = "M26 58C26 50 32 46 40 45L66 44C74 44 80 48 82 56C84 64 80 70 74 72L36 72C30 71 26 66 26 60Z";
    auto leg = [](Pt a, Pt k, Pt f, double w) { return band(a, k, k, f, [=](double t) { return w - 3 * t; }); };
    const std::string legs = leg({33, 68}, {31, 79}, {28, 91}, 9.5) + leg({42, 68}, {44, 79}, {41, 91}, 8.5) + leg({73, 64}, {81, 76}, {75, 91}, 11) +
                             leg({64, 68}, {66, 79}, {63, 91}, 8.5);
    const std::string paws = "M22.5 89.5H33L34 94.5H21.5ZM36 89.5H45.5L46.5 94.5H35ZM58 89.5H67.5L68.5 94.5H57ZM70 89.5H80L81 94.5H69Z";
    std::string mane;
    for (int i = 0; i < 12; i++) {
      const double a = -180 + 30 * i;
      mane += flameTongue(polar(20, 49, 9, a), polar(20, 49, 13, a + 8), polar(20, 49, 16, a + 14), polar(20, 49, 19, a + 22), 7.5, 0.35);
    }
    const std::string face = "M8 49C8 42.5 13 38 19.5 38C26 38 30 42.5 30 49C30 55.5 26 60 19.5 61H13C10 59 8 55 8 49Z";
    c.add("beast-manticore", "Мантикора",
          {G::F(wing), G::XS("M58.5 13 72.5 19M58.5 14 67 32.5M58 15 59 40.5", 1.4), G::F(tail),
           G::XS(notches(s0, s1, s2, s3, {0.16, 0.3, 0.44, 0.58, 0.72, 0.86}, ws, -0.8), 1.4), G::XS(wing, 2.4), G::XS(body + legs, 2), G::F(body + legs + paws),
           G::XS("M50 66C56 69 64 69 70 66M30 55C32 60 35 63 39 65", 1.4), G::XS("M25 92.5V94.5M29.5 92.5V94.5M38.5 92.5V94.5M43 92.5V94.5M60.5 92.5V94.5M65 92.5V94.5M72.5 92.5V94.5M77 92.5V94.5", 1.2),
           G::XS(mane, 1.6), G::F(mane), G::XS(face, 2.2), G::F(face), G::X(leaf({11.5, 45.5}, {13, 44.5}, {15, 44.3}, {16.8, 45}, 2.4, 0.4, 0.5) + "M8 51.5 11.5 50.8 11 53.5Z"),
           G::XS("M9.5 55C13 56.5 17 56.5 21 55M12.5 41.5C14.5 40.5 17.5 40.5 19.5 41.5", 1.3), G::F("M12 55.5 13.3 58.5 14.6 55.8Z")});
  }
  // Сфинкс лежащий: голова в платке-немесе с полосами и уреем, накладная борода, львиное тело с вытянутыми
  // лапами, бедро, хвост, постамент с каймой.
  {
    const std::string head = "M17 20C21 14 28 11 34 11C41 11 46 16 47 23L50 48H36L33 40C30 40 27 39 24 38.5L20 38.5 18.5 37C17 35 16.5 33.5 15 33"
                             "L12.5 31C11.5 30 12 28.5 13.5 27.5L16.5 25Z" + std::string("M17 37.5H22L22.5 47.5H17.5Z");
    const std::string body = "M28 50C36 47 47 47.5 57 50.5C68 53.5 79 55 86 61C92 66 94 75 92 83L91 88H22L24 80C22 70 22.5 58 28 50Z" +
                             std::string("M8 79.5H38V88H6C4 88 4 79.5 8 79.5Z");
    std::vector<L> v{G::F(body + rrect(3, 88, 94, 8, 1.5)),
                     G::XS("M70 88C67.5 79 71.5 70 82 68M29 58C31 66 33.5 72 38 79.5M8.5 83.5H17M8.5 86H17M42 88H72M6 91.5H94M48 58C56 61 64 61.5 70 60", 1.5),
                     G::F(flameTongue({91, 80}, {98, 77}, {98, 70}, {94, 66}, 4.4, 0.3))};
    for (const L& l : moved({G::XS(head, 2.4), G::F(head),
                             G::XS("M17.5 21C22 19.5 26 21 28.5 26C30 30 30 35 28 39.5M35 15.5C38 20 40 30 41 46M39 13.5C42 20 44 30 45.5 46"
                                   "M31 12.5C33 15 34 19 34.5 22.5M18.5 41H21.5M18.5 44H22",
                                   1.3),
                             G::X(leaf({18.5, 26.5}, {19.8, 25.6}, {21.4, 25.5}, {22.8, 26.2}, 2.2, 0.4, 0.5)),
                             G::F("M16 19.5C15 16.5 16.5 14 18.5 14.5C19 16.5 18.5 18.5 17.5 20Z")},
                            scaleAt(1.16f, 32, 48)))
      v.push_back(l);
    c.add("beast-sphinx", "Сфинкс", moved(v, Affine::translate(0, -2)));
  }
}

}  // namespace

void fantasyEmblemsB(Catalog& c) {
  knights(c);
  holy(c);
  demons(c);
  cultists(c);
  magic(c);
  orcs(c);
  lizards(c);
  beastmen(c);
}

}  // namespace rg::gfx::emb
