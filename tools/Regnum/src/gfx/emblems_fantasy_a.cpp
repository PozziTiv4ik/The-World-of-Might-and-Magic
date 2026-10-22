// Regnum — фантазийные эмблемы каталога: драконы, вампиры, природа, стихии, гномы, эльфы, некроманты, тёмные эльфы, наги.
// Сетка 100 × 100, стиль «гравированной геральдики»: одноцветный силуэт из каллиграфических сужающихся мазков с острыми
// концами, детали — тонкие вырезы (двойная кайма, прожилки, штриховка). Симметричные фигуры описаны левой половиной.
#include <algorithm>
#include <tuple>

#include "gfx/emblems_internal.h"

namespace rg::gfx::emb {

namespace {

// ---------------------------------------------------------------- общие помощники

Pt lerp(Pt a, Pt b, double t) { return {float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t)}; }
double dist(Pt a, Pt b) { return std::hypot(double(b.x - a.x), double(b.y - a.y)); }

// Полоса вдоль ломаной c (плотная выборка кривой), ширина по точкам w.
std::string strip(const std::vector<Pt>& c, const std::vector<double>& w) {
  std::vector<Pt> lft, rgt;
  for (size_t i = 0; i < c.size(); i++) {
    const Pt a = c[i > 0 ? i - 1 : 0], b = c[i + 1 < c.size() ? i + 1 : i];
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1e-6, std::hypot(dx, dy)), h = w[i] * 0.5;
    lft.push_back({float(c[i].x - dy / len * h), float(c[i].y + dx / len * h)});
    rgt.push_back({float(c[i].x + dy / len * h), float(c[i].y - dx / len * h)});
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Подпути пути d ломаными (замкнутые — без повтора первой точки).
std::vector<std::vector<Pt>> polylines(std::string_view d) {
  std::vector<std::vector<Pt>> out;
  svgPath(d).flatten(0.04f, [&](const std::vector<Pt>& pts, bool closed) {
    out.push_back(pts);
    if (closed && out.back().size() > 2 && out.back().front() == out.back().back()) out.back().pop_back();
  });
  return out;
}

// Первый подпуть d, равномерно передискретизированный по длине: n + 1 точек (closed — с замыкающим отрезком).
std::vector<Pt> resample(std::string_view d, int n, bool closed = false) {
  std::vector<std::vector<Pt>> ps = polylines(d);
  if (ps.empty()) return {};
  std::vector<Pt> poly = ps[0];
  if (closed) poly.push_back(poly.front());
  std::vector<double> acc{0};
  for (size_t i = 1; i < poly.size(); i++) acc.push_back(acc.back() + dist(poly[i - 1], poly[i]));
  std::vector<Pt> out;
  size_t j = 0;
  for (int k = 0; k <= n; k++) {
    const double s = acc.back() * k / n;
    while (j + 2 < poly.size() && acc[j + 1] < s) j++;
    const double seg = acc[j + 1] - acc[j];
    out.push_back(lerp(poly[j], poly[j + 1], seg > 1e-9 ? (s - acc[j]) / seg : 0));
  }
  return out;
}

// Гладкий путь через точки (кривая Катмулла — Рома кубическими сегментами).
std::string smooth(const std::vector<Pt>& p) {
  std::string s = "M" + ptS(p[0]);
  for (size_t i = 0; i + 1 < p.size(); i++) {
    const Pt a = p[i > 0 ? i - 1 : 0], b = p[i], c = p[i + 1], d = p[i + 2 < p.size() ? i + 2 : i + 1];
    s += "C" + ptS(b + (c - a) * (1.f / 6)) + " " + ptS(c - (d - b) * (1.f / 6)) + " " + ptS(c);
  }
  return s;
}

// Каллиграфический мазок по пути d: ширина w(t), t — доля длины от начала (0) до конца (1).
template <class W>
std::string brush(std::string_view d, W w, int n = 72) {
  const std::vector<Pt> c = resample(d, n);
  std::vector<double> ws;
  for (int i = 0; i <= n; i++) ws.push_back(std::max(0.0, double(w(double(i) / n))));
  return strip(c, ws);
}
// Профили ширины: к острию (от w0, степень p), веретено (острые концы, wm в середине), линейный.
auto toPoint(double w0, double p = 1) {
  return [=](double t) { return w0 * std::pow(1 - t, p); };
}
auto spindle(double wm, double p = 0.7) {
  return [=](double t) { return wm * std::pow(std::sin(kPi * t), p); };
}
auto linear(double w0, double w1) {
  return [=](double t) { return w0 + (w1 - w0) * t; };
}
// Ломаная внутри замкнутого контура d на расстоянии off от края — внутренняя линия «двойной обводки».
// t0…t1 — часть контура (доли длины от начальной точки d); по умолчанию — весь контур, замкнуто.
std::string inset(std::string_view d, double off, double t0 = 0, double t1 = 1, int n = 200) {
  std::vector<Pt> c = resample(d, n, true);
  c.pop_back();
  double area = 0;
  for (size_t i = 0; i < c.size(); i++) area += double(c[i].x) * c[(i + 1) % c.size()].y - double(c[(i + 1) % c.size()].x) * c[i].y;
  const double sgn = area > 0 ? 1 : -1;
  const size_t m = c.size();
  std::vector<Pt> o(m);
  for (size_t i = 0; i < m; i++) {
    const Pt a = c[(i + m - 2) % m], b = c[(i + 2) % m];
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1e-6, std::hypot(dx, dy));
    o[i] = {float(c[i].x - dy / len * off * sgn), float(c[i].y + dx / len * off * sgn)};
  }
  const bool full = t0 <= 0 && t1 >= 1;
  std::string s;
  const int i0 = int(std::lround(t0 * m)), i1 = int(std::lround(t1 * m));
  for (int i = i0; i <= (full ? int(m) - 1 : i1); i++) s += (i == i0 ? "M" : "L") + ptS(o[size_t(i) % m]);
  return full ? s + "Z" : s;
}

// Штриховка-«гравировка» области d (все подпути, чёт-нечет): линии под углом deg (0 — горизонталь) с шагом step,
// концы отступают от края на pad; короче minLen — пропуск.
std::string hatch(std::string_view d, double deg, double step, double pad, double phase = 0.5, double minLen = 1.5) {
  const double a = deg * kPi / 180, cs = std::cos(a), sn = std::sin(a);
  auto toL = [&](Pt p) { return Pt{float(p.x * cs + p.y * sn), float(-p.x * sn + p.y * cs)}; };
  auto fromL = [&](double x, double y) { return Pt{float(x * cs - y * sn), float(x * sn + y * cs)}; };
  std::vector<std::vector<Pt>> ps = polylines(d);
  double y0 = 1e9, y1 = -1e9;
  for (auto& p : ps)
    for (Pt& q : p) {
      q = toL(q);
      y0 = std::min(y0, double(q.y));
      y1 = std::max(y1, double(q.y));
    }
  std::string s;
  for (double y = y0 + step * phase; y < y1; y += step) {
    std::vector<double> xs;
    for (const auto& p : ps)
      for (size_t i = 0; i < p.size(); i++) {
        const Pt u = p[i], v = p[(i + 1) % p.size()];
        if ((u.y <= y) != (v.y <= y)) xs.push_back(u.x + (v.x - u.x) * (y - u.y) / (v.y - u.y));
      }
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2) {
      const double xa = xs[i] + pad, xb = xs[i + 1] - pad;
      if (xb - xa >= minLen) s += "M" + ptS(fromL(xa, y)) + "L" + ptS(fromL(xb, y));
    }
  }
  return s;
}

// Двояко-острый лист (язык пламени, лепесток): от основания b к острию t, наибольшая ширина w на доле m, изгиб bend
// (сдвиг вбок в долях длины; знак — сторона).
std::string leaf(Pt b, Pt t, double w, double bend = 0, double m = 0.45) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::max(1e-6, std::hypot(dx, dy)), nx = -dy / len, ny = dx / len;
  auto p = [&](double along, double side) {
    return ptS({float(b.x + dx * along + nx * (side + bend * len * std::sin(kPi * along))),
                float(b.y + dy * along + ny * (side + bend * len * std::sin(kPi * along)))});
  };
  return "M" + ptS(b) + "C" + p(m * 0.45, w * 0.62) + " " + p(m + (1 - m) * 0.35, w * 0.55) + " " + ptS(t) + "C" + p(m + (1 - m) * 0.35, -w * 0.55) +
         " " + p(m * 0.45, -w * 0.62) + " " + ptS(b) + "Z";
}

// Часть фигуры: заливка, вырезы-заливки и вырезы-линии (толщина линий задаётся при выводе — не зависит от масштаба).
struct Fig {
  std::string fill, holes, lines;
  Fig moved(const Affine& m) const {
    return {fill.empty() ? "" : xf(fill, m), holes.empty() ? "" : xf(holes, m), lines.empty() ? "" : xf(lines, m)};
  }
  Fig mirrored(double axis = 50) const {
    return {fill.empty() ? "" : mirrorX(fill, axis), holes.empty() ? "" : mirrorX(holes, axis), lines.empty() ? "" : mirrorX(lines, axis)};
  }
};
// Слои фигуры: gap > 0 — зазор вокруг силуэта (фигура лежит поверх уже нарисованного).
void put(std::vector<L>& v, const Fig& f, float lw, float gap = 0) {
  if (gap > 0) v.push_back(G::XS(f.fill, gap));
  v.push_back(G::F(f.fill));
  if (!f.holes.empty()) v.push_back(G::X(f.holes));
  if (!f.lines.empty()) v.push_back(G::XS(f.lines, lw));
}

// Линия, параллельная кривой на расстоянии off (по нормали), от t0 до t1.
std::string offsetCurve(Pt p0, Pt c1, Pt c2, Pt p1, double off, double t0 = 0, double t1 = 1, int steps = 20) {
  std::string s;
  for (int i = 0; i <= steps; i++) {
    const CurvePt q = bezier(p0, c1, c2, p1, t0 + (t1 - t0) * i / steps);
    s += (i ? "L" : "M") + ptS({float(q.x + q.nx * off), float(q.y + q.ny * off)});
  }
  return s;
}

// Чешуя: ряды заострённых книзу чешуек внутри эллипса (cx, cy, rx, ry), шаг step, полуширина r, шаг рядов rowStep.
std::string scaleRows(double cx, double cy, double rx, double ry, double step, double r, double rowStep) {
  std::string s;
  int row = 0;
  for (double y = cy - ry; y <= cy + ry; y += rowStep, row++) {
    for (double x = cx - rx - (row % 2 ? step * 0.5 : 0); x <= cx + rx + step; x += step) {
      const double ex = (x - cx) / rx, ey = (y + r * 0.5 - cy) / ry;
      if (ex * ex + ey * ey > 1) continue;
      s += "M" + n(x - r) + " " + n(y) + "C" + n(x - r) + " " + n(y + r * 0.7) + " " + n(x - r * 0.25) + " " + n(y + r * 1.05) + " " + n(x) + " " +
           n(y + r * 1.35) + "C" + n(x + r * 0.25) + " " + n(y + r * 1.05) + " " + n(x + r) + " " + n(y + r * 0.7) + " " + n(x + r) + " " + n(y);
    }
  }
  return s;
}

// Наконечник-острие (стрела языка, жало хвоста): острие tip, направление из from, длина len, полуширина hw; основание вогнуто.
std::string barb(Pt from, Pt tip, double len, double hw) {
  const double dx = tip.x - from.x, dy = tip.y - from.y, l = std::max(1e-6, std::hypot(dx, dy)), ux = dx / l, uy = dy / l;
  auto p = [&](double a, double s) { return ptS({float(tip.x - ux * a - uy * s), float(tip.y - uy * a + ux * s)}); };
  return "M" + ptS(tip) + "Q" + p(len * 0.45, hw * 0.75) + " " + p(len * 1.05, hw) + "Q" + p(len * 0.62, 0) + " " + p(len * 1.05, -hw) + "Q" +
         p(len * 0.45, -hw * 0.75) + " " + ptS(tip) + "Z";
}

// Шип: от основания b шириной w к острию t, выгнут на bend (доля длины, знак — сторона).
std::string thorn(Pt b, Pt t, double w, double bend = 0, double p = 1) {
  const double dx = t.x - b.x, dy = t.y - b.y;
  const Pt m{float(b.x + dx * 0.5 - dy * bend), float(b.y + dy * 0.5 + dx * bend)};
  return brush("M" + ptS(b) + "Q" + ptS(m) + " " + ptS(t), toPoint(w, p), 24);
}

// Язык пламени: S-образный мазок по пути d от основания шириной w к острию.
std::string tongue(std::string_view d, double w) { return brush(d, toPoint(w, 1.15)); }

// Змеиное тело вдоль пути d (от головы к хвосту) шириной w(t). Гребень: count острых плавников высотой h на долях
// t0…t1 со стороны side (±1 — по нормали пути, наклон назад, к хвосту); пластины брюха — вырезами с другой стороны.
template <class W>
Fig serpent(std::string_view d, W w, int side, double t0, double t1, int count, double h, double p0 = 0, double p1 = 0, int plates = 0) {
  const int n = 120;
  const std::vector<Pt> c = resample(d, n);
  Fig f;
  f.fill = brush(d, w, n);
  auto frame = [&](double t, Pt& p, Pt& tg, Pt& nr) {
    const int i = std::clamp(int(std::lround(t * n)), 1, n - 1);
    p = c[size_t(i)];
    const double dx = c[size_t(i + 1)].x - c[size_t(i - 1)].x, dy = c[size_t(i + 1)].y - c[size_t(i - 1)].y, l = std::max(1e-6, std::hypot(dx, dy));
    tg = {float(dx / l), float(dy / l)};
    nr = {float(-dy / l * side), float(dx / l * side)};
  };
  for (int k = 0; k < count; k++) {
    const double t = count > 1 ? t0 + (t1 - t0) * k / (count - 1) : t0, hw = w(t) * 0.5;
    Pt p, tg, nr;
    frame(t, p, tg, nr);
    const double hk = h * (1 - 0.35 * (count > 1 ? double(k) / (count - 1) : 0));
    const Pt b = p + nr * float(hw - 2), tip = p + nr * float(hw + hk) + tg * float(hk * 0.9);
    f.fill += thorn(b, tip, hk * 0.75, 0.12 * side);
  }
  for (int k = 0; k < plates; k++) {
    const double t = plates > 1 ? p0 + (p1 - p0) * k / (plates - 1) : p0, hw = w(t) * 0.5;
    Pt p, tg, nr;
    frame(t, p, tg, nr);
    const Pt a = p - nr * float(hw + 1), m = p - nr * float(hw * 0.55) + tg * float(hw * 0.2), e = p - nr * float(hw * 0.2);
    f.lines += "M" + ptS(a) + "Q" + ptS(m) + " " + ptS(e);
  }
  return f;
}

// Голова дракона в профиль (мордой влево, пасть раскрыта, tongue — язык со стрелой): локальная рамка ≈ −9…66 × 0…43;
// затылок и челюстной сустав — около (44…52, 12…30), к ним крепится шея.
Fig dragonHeadFig(bool tongue) {
  Fig f;
  // верхняя челюсть и череп: острый кончик морды, завиток ноздри, переносица, надбровный шип, темя, затылок, сустав,
  // губа с клыком
  f.fill = "M0 18C2 15 4 13 7 12.5L9 14C16 12.5 24 10 30 7L38 1 37.5 7.5C42 8 47 10 50 13C52.5 15.5 53 19 51.5 22.5L45 27.5 33 25"
           "C24 23.5 15 22.8 9 23L10.5 28.5 6.5 23C3.5 22.5 1 21 0 18Z";
  // нижняя челюсть с клыком и шипом-бородкой
  f.fill += "M34 27C27 29.5 18 33 10.5 37L6.5 39.5C15 41 27 40 36.5 36.5C41 35 45 32 46.5 29L40 27.2Z"
            "M15 35.6 16.2 30.6 18.6 34.4Z" +
            brush("M21 39.5C22.5 42 25 44 28 45", toPoint(3.4, 0.9));
  // рога и плавник-ухо за челюстью
  f.fill += brush("M38 8.5C46 2.5 55 0 66 2", toPoint(6.5, 0.9)) + brush("M45 11.5C52 8.5 59 9 65 12.5", toPoint(4.5, 0.9)) +
            brush("M45 20C51 20.5 57 22.5 62 26", toPoint(5, 0.9));
  // язык со стрелой на конце
  if (tongue)
    f.fill += brush("M33 26.6C26 27.3 18 28.8 11 31.3C6 33 2.5 34.8 -2 34.4", linear(2.4, 1.9)) + barb({-1, 34.4}, {-9, 32}, 6, 3);
  f.holes = "M22 16.5C25 13.8 29.5 12.6 34 13C31.5 15.8 27 17.5 22 16.5Z"   // глаз
            "M3.5 16.5C4.5 15 6.3 14.4 8 14.7C7 16 5.5 16.8 3.5 16.5Z";      // ноздря
  f.lines = "M35 17.5C39 18.5 42.5 20.5 45 23.5";                          // скула
  return f;
}

// ---------------------------------------------------------------- драконы
// Перепончатое крыло: корень у тела, плечо, дуга руки (c1, c2) до запястья, коготь большого пальца, концы пальцев tips
// (от переднего края к заднему); перепонка между пальцами провисает острыми фестонами к запястью (sag).
// Кости пальцев и кость руки — линии-вырезы.
Fig wingFig(Pt root, Pt shoulder, Pt c1, Pt c2, Pt wrist, Pt thumb, const std::vector<Pt>& tips, double sag) {
  Fig f;
  f.fill = "M" + ptS(root) + "L" + ptS(shoulder) + "C" + ptS(c1) + " " + ptS(c2) + " " + ptS(wrist) + "L" + ptS(tips[0]);
  for (size_t i = 0; i + 1 < tips.size(); i++) f.fill += "Q" + ptS(lerp(lerp(tips[i], tips[i + 1], 0.5), wrist, sag)) + " " + ptS(tips[i + 1]);
  f.fill += "Q" + ptS(lerp(lerp(tips.back(), root, 0.5), wrist, sag * 0.75)) + " " + ptS(root) + "Z";
  f.fill += thorn(wrist, thumb, 3.4, 0.18);
  for (size_t i = 1; i < tips.size(); i++) f.lines += "M" + ptS(lerp(wrist, tips[i], 0.12)) + "L" + ptS(lerp(wrist, tips[i], 0.84));
  // кость руки — вдоль переднего края, со стороны перепонки
  const CurvePt q = bezier(shoulder, c1, c2, wrist, 0.5);
  const double side = q.nx * (tips[1].x - q.x) + q.ny * (tips[1].y - q.y) > 0 ? 1 : -1;
  f.lines += offsetCurve(shoulder, c1, c2, wrist, 3.2 * side, 0.08, 0.92);
  return f;
}

// Лапа: путь d от бедра к запястью (ширина w0 → w1), на конце — count когтей веером: направление dir (градусы),
// разброс spread, длина len; когти загнуты в сторону curl (±1).
std::string limb(std::string_view d, double w0, double w1, double dir, double spread, double len, int curl = 1, int count = 3) {
  std::string s = brush(d, linear(w0, w1), 40);
  const std::vector<Pt> c = resample(d, 8);
  const Pt e = c.back();
  for (int k = 0; k < count; k++) {
    const double a = dir + spread * (count > 1 ? k - (count - 1) * 0.5 : 0);
    s += thorn(e, polar(e.x, e.y, len, a), w1 * 0.62, 0.22 * curl);
  }
  return s;
}

void dragons(Catalog& c) {
  c.group("Драконы");
  // Дракон в полёте («распростёртый»): крылья с острыми фестонами перепонок, голова в профиль на изогнутой шее,
  // стройное тело с пластинами, лапы с когтями, S-образный хвост с жалом.
  {
    const Fig w = wingFig({45, 50}, {44, 37}, {38, 27}, {30, 15}, {22, 8}, {21, 1.5}, {{3, 11}, {2, 30}, {8, 47}, {21, 59}}, 0.32);
    const Fig head = dragonHeadFig(false).moved(place(29, 3.5, 0, 0.5f));
    const std::string body = brush("M50 30C50 42 50 54 50 67", [](double t) { return 7 + 11 * std::exp(-std::pow((t - 0.32) / 0.3, 2)); });
    const std::string neck = brush("M50 42C50.5 34 57 29 55 19", linear(11, 9));
    const std::string legs = limb("M46 59C40 62 36 67 33 74", 8.5, 4.6, 115, 32, 7.5) + limb("M54 59C60 62 64 67 67 74", 8.5, 4.6, 65, 32, 7.5, -1) +
                             xf(ell(0, 0, 5, 7), place(43, 61, 40)) + xf(ell(0, 0, 5, 7), place(57, 61, -40));
    const std::string tail = brush("M50 64C50 74 61 76 60.5 84C60 90 55 93 47 94", linear(7.5, 2.4)) + barb({49, 94}, {39, 95.5}, 7, 3.6);
    std::vector<L> v{G::F(w.fill + w.mirrored().fill), G::XS(w.lines + w.mirrored().lines, 1.5)};
    const std::string front = body + neck + legs + tail;
    v.push_back(G::XS(front, 2.6));
    v.push_back(G::F(front));
    v.push_back(G::XS("M44.5 44Q50 47 55.5 44M44 50Q50 53 56 50M44.5 56Q50 59 55.5 56M46 62Q50 64.5 54 62", 1.5));
    put(v, head, 1.4f, 2.2f);
    c.add("dragon-wings", "Дракон в полёте", v);
  }
  // Свернувшийся дракон (уроборос): кольцо тела с гребнем острых плавников и пластинами брюха, голова кусает хвост.
  {
    const double cx = 50, cy = 55, R = 33, aEnd = 232;
    // голова наклонена мордой вниз-влево, пасть — на кончике хвоста
    const Pt tip = polar(cx, cy, R, aEnd + 6);
    const Affine h0 = place(0, 0, -24, 0.52f);
    const Pt mo = h0.apply({17, 30});
    const Affine hx = place(tip.x - mo.x, tip.y - mo.y, -24, 0.52f);
    const Pt nape = hx.apply({44, 21});
    const double a0 = std::atan2(nape.y - cy, nape.x - cx) * 180 / kPi + 14;
    std::string ring = "M" + ptS(nape);
    for (double a = a0; a <= aEnd + 0.01; a += (aEnd - a0) / 40) ring += "L" + pt(cx, cy, R, a);
    const Fig body = serpent(ring, [](double t) { return 14 - 10.5 * t; }, -1, 0.06, 0.86, 13, 8.5, 0.07, 0.9, 15);
    std::vector<L> v;
    put(v, body, 1.5f);
    v.push_back(G::F(barb(polar(cx, cy, R, aEnd - 2), polar(cx, cy, R + 1.5, aEnd + 10), 7, 4)));
    put(v, dragonHeadFig(false).moved(hx), 1.5f, 2.4f);
    c.add("dragon-coiled", "Свернувшийся дракон", moved(v, Affine::translate(-2.5f, -1)));
  }
  // Глаз дракона: миндалевидный глаз с двойным веком, радужка с лучами, щелевой зрачок, бровь с откинутыми шипами,
  // чешуйчатое нижнее веко.
  {
    const std::string eye = "M4 56C24 39 62 33 96 46C80 64 42 73 4 56Z";
    // надбровные гребни — два заострённых рога-пластины, уходящие назад
    const std::string brow = brush("M4 46C24 31 58 22 98 16", [](double t) { return 1 + 9.5 * std::pow(std::sin(kPi * std::pow(t, 0.8)), 0.8); }) +
                             brush("M26 28C44 17 68 9 95 5", [](double t) { return 0.8 + 6.5 * std::pow(std::sin(kPi * std::pow(t, 0.8)), 0.8); });
    const std::string lid = brush("M10 66C30 80 64 80 90 59", spindle(7.5, 0.8));
    std::string rays, scales;
    for (int k = 0; k < 16; k++) rays += "M" + pt(52, 53, 8.5, 11.25 + 22.5 * k) + "L" + pt(52, 53, 12.5, 11.25 + 22.5 * k);
    for (int k = 0; k < 6; k++) {
      const CurvePt q = bezier({10, 66}, {30, 80}, {64, 80}, {90, 59}, 0.16 + 0.136 * k);
      scales += "M" + ptS({float(q.x - 3), float(q.y + 3)}) + "L" + ptS({float(q.x + 1), float(q.y - 2.5)});
    }
    c.add("dragon-eye", "Глаз дракона",
          moved({G::F(eye + lid), G::XS(inset(eye, 4.2, 0.05, 0.45) + inset(eye, 4.2, 0.55, 0.95), 1.6), G::XS(brow, 2.4), G::F(brow),
                 G::X(circ(52, 53, 17.5)),
                 G::F(circ(52, 53, 15)), G::XS(rays, 1.4), G::X("M52 39C56.5 46 56.5 60 52 67C47.5 60 47.5 46 52 39Z"),
                 G::XS(scales, 1.5)},
                Affine::translate(0, 5)));
  }
  // Череп дракона (анфас): изогнутые рога с кольцами, скуловые шипы, узкие раскосые глазницы, гребень, вытянутая морда
  // со щелями ноздрей и клыками, швы черепа.
  {
    const std::string half = "M50 12C44 12 39.5 15 37 20L33 31 25 34.5 32 38.5C34.5 46 37 55 39 64L41 79 44 91 47.5 96H50.5V12Z";
    const std::string horn = brush("M39 22C29 20 18 17 11 11C7.5 8 5.5 5 5.5 1.5", toPoint(10.5, 1.05));
    const std::string spike = thorn({32, 35}, {16, 44}, 5.5, 0.12) + thorn({36, 50}, {26, 59}, 4, 0.12);
    const std::string fang = thorn({41.5, 75}, {38.5, 89}, 4.2, 0.12) + thorn({43.5, 84}, {42, 93}, 3, 0.1);
    c.add("dragon-skull", "Череп дракона",
          {G::F(sym(half + horn + spike + fang)),
           G::X(sym("M33.5 39C38.5 37.8 43.5 38.8 46.8 42.6C45.2 46.4 42.4 48 39.4 47.4C36.2 46.6 34.2 43.2 33.5 39Z" +
                    xf(ell(0, 0, 1.1, 3.8), place(46.5, 89.5, -14)))),
           G::XS(sym("M28.5 19.6 26.8 14.6M21 16.4 20 11.4M14 11.4 14 6.6M40.5 53C42 62 43.5 71 45 80M42.5 30 46 35") + "M50 16V31M50 37V58", 1.5)});
  }
  // Голова дракона («оторванная» по-геральдически): голова с раскрытой пастью и языком-стрелой, рога, S-образная
  // шея с гребнем острых плавников, брюшные пластины, рваный край шеи языками.
  {
    const Fig head = dragonHeadFig(true).moved(place(10, 5, 0, 0.88f));
    const std::string spine = "M52 22C70 26 80 44 76 60C73 72 62 76 62 88";
    Fig neck = serpent(spine, linear(18, 14), -1, 0.08, 0.8, 7, 9, 0.2, 0.86, 8);
    neck.fill += thorn({55, 85}, {49, 97}, 6, -0.15) + thorn({59.5, 86}, {57.5, 99}, 6, -0.1) + thorn({64.5, 86}, {66, 98.5}, 6, 0.1) +
                 thorn({69, 85}, {74, 95}, 5.5, 0.15);
    std::vector<L> v;
    put(v, neck, 1.6f);
    put(v, head, 1.6f, 2.4f);
    c.add("dragon-head", "Голова дракона", v);
  }
  // Дракон на задних лапах (геральдический «восстающий»): поднятое крыло с фестонами, гребень по шее и спине, когтистые
  // лапы, хвост завитком с жалом.
  {
    const Fig w = wingFig({58, 60}, {52, 38}, {56, 26}, {62, 14}, {71, 6}, {70, 0.5}, {{96, 9}, {98, 29}, {94, 46}, {82, 58}}, 0.3);
    const Fig head = dragonHeadFig(true).moved(place(8.5, 4, 0, 0.5f));
    auto girth = [](double t) { return t < 0.4 ? 11 + 3 * t / 0.4 : 14 + 4.5 * std::sin(kPi * (t - 0.4) / 0.6); };
    Fig body = serpent("M33 13C42 15 46 24 44 36C42 48 47 62 60 70", girth, -1, 0.08, 0.62, 5, 6.5, 0.45, 0.95, 5);
    const std::string fore = limb("M44 44C38 41 34 38 31 34C29 31 27 30 23.5 30", 7.5, 4.6, 185, 34, 8, -1) +
                             limb("M46 54C40 54 35 52 31 50.5C28 49.5 26 49.5 23 51", 7.5, 4.6, 195, 34, 8, -1);
    const std::string hindA = limb("M60 62C65 72 61 80 55 84C52 86 51 89 50.5 93", 12, 5.5, 182, 30, 8.5, -1);
    const std::string hindB = limb("M54 66C48 72 44 74 40 74C37 74 35 76 33 80", 10, 5, 205, 32, 8, -1);
    const std::string tail = brush("M61 70C73 85 86 93 92 85C96 79 92 71 85 71", linear(9, 2.6)) + barb({86.5, 71.5}, {79, 75}, 7, 3.6);
    std::vector<L> v{G::F(w.fill), G::XS(w.lines, 1.5), G::F(hindB)};
    body.fill += fore + hindA + tail;
    put(v, body, 1.5f, 2.6f);
    put(v, head, 1.5f, 2.4f);
    c.add("dragon-rampant", "Дракон на задних лапах", v);
  }
  // Драконье яйцо: из расколотой чешуйчатой скорлупы поднимается дракончик, верхушка скорлупы отлетела.
  {
    const std::string egg = "M21 62C21 54 22.5 47 25 41L31 47 37 39.5 43 47 49 40 55 47.5 61 40.5 67 47 74.5 40.5C77.5 47 79 54 79 62"
                            "C79 82 66 97 50 97C34 97 21 82 21 62Z";
    const std::string cap = xf("M-15 6C-14 -4 -8 -10 0 -10C8 -10 14 -4 15 6L10.5 1.5 5 7.5 0 2 -5 7.5 -10.5 1.5Z", place(78, 20, 26));
    const Fig head = dragonHeadFig(false).moved(place(6, 8, -8, 0.5f));
    const Fig neck = serpent("M31 21C40 24 46 32 47 48", linear(10.5, 15), -1, 0.12, 0.7, 3, 5.5);
    std::vector<L> v;
    put(v, neck, 1.5f);
    put(v, head, 1.5f, 2.2f);
    v.push_back(G::F(cap));
    v.push_back(G::XS(xf("M-11 -1C-8 -6 -4 -7.5 0 -7.5", place(78, 20, 26)), 1.5));
    v.push_back(G::XS(egg, 3.2));
    v.push_back(G::F(egg));
    v.push_back(G::XS(scaleRows(50, 72, 21, 21, 11, 5.5, 8.6), 1.5));
    c.add("dragon-egg", "Драконье яйцо", v);
  }
  // Драконья лапа (в профиль, «оторванная»): чешуйчатая голень с рваным краем и шпорой, три пальца вперёд и один назад,
  // фаланги с суставами, крючковатые когти.
  {
    struct Toe {
      Pt b, k1, k2, tip;
      double w, bend;
    };
    const Toe toes[] = {{{52, 50}, {38, 43}, {23, 43}, {7, 57}, 11, 0.3},
                        {{53, 54}, {42, 60}, {31, 68}, {24, 87}, 11, 0.3},
                        {{57, 56}, {56, 68}, {52, 78}, {39, 93}, 10.5, 0.32},
                        {{61, 52}, {71, 59}, {76, 68}, {66, 83}, 10, -0.32}};
    std::string fill, joints;
    for (const Toe& t : toes) {
      fill += brush(smooth({t.b, t.k1, t.k2}), linear(t.w, t.w * 0.8), 30) + thorn(t.k2, t.tip, t.w * 0.78, t.bend, 1.15);
      for (const Pt& k : {t.k1, t.k2}) {
        const Pt d = k == t.k1 ? t.k2 - t.b : t.tip - t.k1;
        const float l = std::hypot(d.x, d.y), hw = float(t.w * 0.55);
        joints += "M" + ptS(k + Pt{-d.y / l * hw, d.x / l * hw}) + "L" + ptS(k + Pt{d.y / l * hw, -d.x / l * hw});
      }
    }
    const std::string shin = "M83 13L59 47";
    fill += brush(shin, linear(19, 16)) + ell(56.5, 51.5, 11, 10) + thorn({67, 40}, {81, 45}, 6, -0.15) + thorn({88, 19}, {96, 14}, 6, -0.1) +
            thorn({84.5, 14}, {90, 4}, 6, -0.05) + thorn({79.5, 10.5}, {80.5, 1.5}, 6, 0.05) + thorn({75, 8}, {70, 1.5}, 5, 0.15);
    // поперечные щитки голени
    std::string scutes;
    for (int k = 0; k < 6; k++) {
      const Pt q = lerp({83, 13}, {59, 47}, 0.16 + 0.12 * k), dir{-0.577f, 0.817f}, nr{0.817f, 0.577f};
      scutes += "M" + ptS(q - nr * 6.5f) + "Q" + ptS(q + dir * 2.6f) + " " + ptS(q + nr * 6.5f);
    }
    c.add("dragon-claw", "Драконья лапа", {G::F(fill), G::XS(joints + scutes, 1.5)});
  }
  // Два дракона: змеевидные драконы с гребнями, головы обращены к пылающей жемчужине.
  {
    const Fig head = dragonHeadFig(false).moved(place(56.5, 13, 0, 0.46f));
    const Fig body = serpent("M75 23C88 29 89 48 78 59C67 70 59 79 64 93", linear(15, 3.5), -1, 0.05, 0.62, 6, 6, 0.08, 0.7, 9);
    const std::string paw = limb("M81 40C77 42 73 43 68 42", 5.5, 3.6, 205, 32, 5, -1);
    const std::string pearl = circ(50, 34, 7.5) + tongue("M45.5 29C42 24 45.5 19 42 13", 5) + tongue("M50 27C47 20 53 15 50 7", 6) +
                              tongue("M54.5 29C58 24 54.5 19 58 13", 5);
    std::vector<L> v;
    for (const Fig& f : {body, body.mirrored()}) put(v, f, 1.4f);
    v.push_back(G::F(paw + mirrorX(paw, 50)));
    for (const Fig& f : {head, head.mirrored()}) put(v, f, 1.4f, 2.2f);
    v.push_back(G::XS(pearl, 2.6));
    v.push_back(G::F(pearl));
    v.push_back(G::XS(arc(50, 34, 4.6, 200, 280), 1.5));
    c.add("dragon-twin", "Два дракона", v);
  }
}

// ---------------------------------------------------------------- вампиры
// Капля (кровь, вода): острый верх в (cx, top), круглый низ радиуса r с центром на cy.
std::string drop(double cx, double top, double cy, double r) {
  return "M" + n(cx) + " " + n(top) + "C" + n(cx + r * 0.35) + " " + n(top + (cy - top) * 0.45) + " " + n(cx + r) + " " + n(cy - r * 0.6) + " " +
         n(cx + r) + " " + n(cy) + "A" + n(r) + " " + n(r) + " 0 0 1 " + n(cx - r) + " " + n(cy) + "C" + n(cx - r) + " " + n(cy - r * 0.6) + " " +
         n(cx - r * 0.35) + " " + n(top + (cy - top) * 0.45) + " " + n(cx) + " " + n(top) + "Z";
}

// Летучая мышь (распростёртая, анфас): крылья с острыми фестонами и костями пальцев, острые уши, узкие глаза,
// шерсть на груди. Сетка 100, размах 2…98.
struct BatFig {
  Fig wings, body;
};
BatFig batFig() {
  const Fig w = wingFig({47.5, 60}, {45.5, 40}, {41, 33}, {33, 24}, {25, 16}, {23, 7.5}, {{3, 19}, {1.5, 38}, {8, 55}, {21, 66}, {36, 68.5}}, 0.38);
  BatFig b;
  b.wings = {w.fill + w.mirrored().fill, "", w.lines + w.mirrored().lines};
  b.body.fill = brush("M50 38C50 48 50 60 50 73", [](double t) { return 5 + 8.5 * std::pow(std::sin(kPi * (0.12 + 0.88 * t)), 0.7); }) +
                ell(50, 35.5, 6.2, 6) + "M44.6 37.5 50 45.5 55.4 37.5Z" + thorn({46.3, 32}, {42, 17.5}, 5.2, 0.12) + thorn({53.7, 32}, {58, 17.5}, 5.2, -0.12) +
                thorn({47.5, 71}, {44.5, 79}, 3, 0.2) + thorn({52.5, 71}, {55.5, 79}, 3, -0.2);
  b.body.holes = sym("M45.2 34.4 48.8 35.8 45.8 36.8Z");
  b.body.lines = "M46 50 50 53 54 50M46.5 56 50 59 53.5 56M47.5 62 50 64.5 52.5 62";
  return b;
}

void vampires(Catalog& c) {
  c.group("Вампиры");
  // Летучая мышь: распростёртые крылья с острыми фестонами и костями пальцев, острые уши, узкие глаза, шерсть.
  {
    const BatFig b = batFig();
    std::vector<L> v;
    put(v, b.wings, 1.5f);
    put(v, b.body, 1.5f, 2.4f);
    c.add("vampire-bat", "Летучая мышь", moved(v, Affine::translate(0, 9)));
  }
  // Клыки: верхняя губа с изгибом «лука» и острыми уголками, ряд резцов, два длинных изогнутых клыка, капли крови.
  {
    const std::string lip = "M3 40C13 29 26 22.5 38 24.5C44 25.5 47.5 28 50 30.5C52.5 28 56 25.5 62 24.5C74 22.5 87 29 97 40C82 37.5 66 38.5 50 41"
                            "C34 38.5 18 37.5 3 40Z";
    std::string teeth;
    for (double x : {39.5, 46.2, 53.8, 60.5}) teeth += thorn({float(x), 38}, {float(x + (50 - x) * 0.06), 48}, 6.2, 0);
    for (double x : {18.5, 81.5}) teeth += thorn({float(x), 37}, {float(x + (x < 50 ? 1.5 : -1.5)), 45}, 5.6, 0);
    const std::string fang = brush("M27.5 37C28 50 30.5 62 37 75", [](double t) { return 11.5 * std::pow(1 - t, 0.8); });
    c.add("vampire-fangs", "Клыки",
          moved({G::F(lip + teeth + sym(fang)), G::XS("M12 37C24 32 36 32.5 50 36.2C64 32.5 76 32 88 37", 1.5),
                 G::XS(sym("M30 41C31 50 33 58 36.5 65"), 1.5), G::F(sym(drop(38.5, 80, 89, 5.5)) + drop(50, 56, 65, 4))},
                Affine::translate(0, -8)));
  }
  // Кубок крови: готическая чаша-тюльпан с каймой и самоцветом, кровь стекает с края, капля над чашей, узел ножки,
  // ступенчатое основание.
  {
    const std::string bowl = "M21 30H79C79 49 69 61 50 64C31 61 21 49 21 30Z";
    const std::string stem = "M47.5 63H52.5L51.8 70H48.2ZM48.2 79H51.8L52.5 86H47.5Z" + std::string("M50 69 56 74.5 50 80 44 74.5Z") +
                             "M28 97C30 91 39 86.5 50 86.5C61 86.5 70 91 72 97Z";
    std::string drips = "M23 37.5H31";
    for (auto [x, len] : {std::pair{31.0, 8.0}, std::pair{40.0, 13.0}, std::pair{50.0, 6.0}, std::pair{60.0, 11.0}, std::pair{69.0, 7.0}}) {
      drips += "V" + n(37.5 + len) + "M" + n(x) + " 37.5H" + n(std::min(77.0, x + 9));
    }
    c.add("vampire-chalice", "Кубок крови",
          {G::F(drop(50, 2, 19.5, 7.5) + bowl + rrect(18, 26.5, 64, 5.5, 1.5) + stem), G::XS("M20 32.2H80", 1.5),
           G::XS(drips, 1.6), G::X("M50 48 54 53.5 50 59 46 53.5Z"), G::XS("M44.5 74.5H55.5M32 92.5H68", 1.5), G::XS(arc(50, 18.5, 4.2, 200, 260), 1.5)});
  }
  // Гроб: шестигранная крышка с двойной каймой, готический крест с лилейными концами.
  {
    const std::string lid = "M36 3H64L79 26 68 97H32L21 26Z";
    // конец креста: острие с двумя загнутыми лепестками
    auto finial = [](double x, double y, double deg) {
      return xf("M0 -8.5 2.6 -3.5 6.5 -5 4.5 0H-4.5L-6.5 -5 -2.6 -3.5Z", place(float(x), float(y), float(deg)));
    };
    const std::string cross = rrect(47.2, 22, 5.6, 54, 1) + rrect(35, 34.2, 30, 5.6, 1) + finial(50, 22.5, 0) + finial(50, 75.5, 180) +
                              finial(35.5, 37, -90) + finial(64.5, 37, 90);
    c.add("vampire-coffin", "Гроб", {G::F(lid), G::XS(inset(lid, 4.5), 1.6), G::XS(cross, 5.2), G::F(cross), G::XS("M50 44V68", 1.5)});
  }
  // Роза с каплей крови (геральдическая роза на стебле): два ряда лепестков с разделами, острые чашелистики между ними,
  // сердцевина с семенами; стебель с шипами, листья с жилками, падающие капли.
  {
    const Pt cn{50, 29};
    std::string outer, inner, barbs, splits, seeds;
    for (int k = 0; k < 5; k++) {
      const double a = -90 + 72 * k;
      outer += circ(polar(cn.x, cn.y, 12.5, a).x, polar(cn.x, cn.y, 12.5, a).y, 11.5);
      inner += circ(polar(cn.x, cn.y, 7, a + 36).x, polar(cn.x, cn.y, 7, a + 36).y, 6.6);
      barbs += thorn(polar(cn.x, cn.y, 17, a + 36), polar(cn.x, cn.y, 27.5, a + 36), 5.5, 0);
      splits += "M" + pt(cn.x, cn.y, 13, a + 36) + "L" + pt(cn.x, cn.y, 21.5, a + 36);
      seeds += circ(polar(cn.x, cn.y, 3.6, a).x, polar(cn.x, cn.y, 3.6, a).y, 1.1);
    }
    const std::string stem = brush("M50 50C53 62 45 70 47 82C48 89 49.5 93 48.5 98", linear(5.5, 4.5)) + thorn({51.5, 61}, {58, 58.5}, 3.4, -0.2) +
                             thorn({46.2, 74}, {39.5, 72}, 3.4, 0.2) + thorn({48.4, 89}, {54.5, 87}, 3.2, -0.2);
    const std::string leaves = leaf({47.5, 70}, {21, 63}, 14, 0.07) + leaf({48, 80}, {76, 74}, 14, -0.07);
    c.add("vampire-rose", "Роза с каплей крови",
          moved({G::F(stem + leaves), G::XS("M45 69.5 25 64M51 79.5 71 75" + std::string("M36 66 31 70M60 76 64 80"), 1.5), G::XS(outer + barbs, 2.4),
                 G::F(outer + barbs), G::XS(splits, 1.6), G::XS(inner, 2), G::F(inner), G::X(circ(cn.x, cn.y, 4.6)), G::F(circ(cn.x, cn.y, 2.6)),
                 G::F(drop(80, 52, 62, 5.5) + drop(84, 72, 79.5, 3.8))},
                Affine::translate(-3.5f, 0)));
  }
  // Герб с летучими мышами: щит с двойной каймой и каплей крови, по сторонам — две летучие мыши.
  {
    const std::string shield = "M50 25 76 31V53C76 71 65 84 50 93C35 84 24 71 24 53V31Z";
    const BatFig b = batFig();
    const Affine bx = place(21, 22, 18, 0.44f) * Affine::translate(-50, -45);
    const Fig bat = Fig{b.wings.fill + b.body.fill, b.body.holes, ""}.moved(bx);
    std::vector<L> v;
    put(v, bat, 1.5f);
    put(v, bat.mirrored(), 1.5f);
    v.push_back(G::XS(shield, 4));
    v.push_back(G::F(shield));
    v.push_back(G::XS(inset(shield, 4.5), 1.6));
    v.push_back(G::X(drop(50, 37, 63, 11)));
    v.push_back(G::F(drop(50, 47, 64.5, 6.5)));
    c.add("vampire-crest", "Герб с летучими мышами", v);
  }
  // Луна и замок: готический замок со шпилями и стрельчатыми окнами на скале, полумесяц, летучие мыши.
  {
    // башня: стены от y0 до низа, остроконечный шпиль высотой h
    auto tower = [](double x, double w, double y0, double h) {
      return "M" + n(x - w / 2) + " 90V" + n(y0) + "H" + n(x - w / 2 - 1.5) + "L" + n(x) + " " + n(y0 - h) + "L" + n(x + w / 2 + 1.5) + " " + n(y0) + "H" +
             n(x + w / 2) + "V90Z";
    };
    auto window = [](double x, double y, double w, double h) {
      return "M" + n(x - w / 2) + " " + n(y + h) + "V" + n(y + w * 0.6) + "Q" + n(x - w / 2) + " " + n(y + w * 0.1) + " " + n(x) + " " + n(y) + "Q" +
             n(x + w / 2) + " " + n(y + w * 0.1) + " " + n(x + w / 2) + " " + n(y + w * 0.6) + "V" + n(y + h) + "Z";
    };
    const std::string castle = tower(50, 12, 36, 32) + tower(34, 9, 50, 24) + tower(66, 9, 50, 24) + tower(21, 7, 62, 18) + tower(79, 7, 62, 18) +
                               "M24 90V68H76V90Z";
    const std::string rock = "M3 98C8 92 14 89 20 88L28 85 36 87 44 84H56L64 87 72 85 80 88C86 89 92 92 97 98Z";
    const std::string windows = window(50, 42, 4.6, 9) + window(34, 56, 3.4, 7) + window(66, 56, 3.4, 7) + window(21, 68, 2.8, 6) +
                                window(79, 68, 2.8, 6) + window(50, 74, 9, 16);
    auto bat = [](double x, double y, double s) {
      return xf("M0 0C-3 -3 -7 -4 -11 -2C-8 -1 -7 1 -7 3C-5 1 -3 1 -1.5 2.5L0 1 1.5 2.5C3 1 5 1 7 3C7 1 8 -1 11 -2C7 -4 3 -3 0 0Z",
                place(float(x), float(y), 0, float(s)));
    };
    c.add("vampire-castle", "Луна и замок",
          {G::F(circ(76, 21, 18)), G::X(circ(84, 15, 16)), G::XS(castle + rock, 3), G::F(castle + rock + bat(17, 18, 0.9) + bat(29, 8, 0.7)),
           G::X(windows), G::XS("M24 80H44M56 80H76M31 50H37M63 50H69", 1.5)});
  }
  // Плащ с высоким воротником: стоячий воротник крыльями с подкладкой, плащ расходится складками от плеч, подол
  // острыми фестонами, застёжка-медальон с самоцветом и цепочкой.
  {
    const std::string collar = "M46 38C42 32 39.5 25 39 18L36 3C31 8 27 11 21 13L18 6C14 13 11 20 10 27C14 33 19 37.5 25 40C32 42.5 39 43.5 46 43.5Z";
    std::string cape = "M50 38C43 38 34 40 26 44C17 49 11 57 9.5 67C8 77 8.5 87 10 97";
    for (int k = 0; k < 4; k++) {
      const double x0 = 10 + k * 10, x1 = x0 + 10;
      cape += "Q" + n((x0 + x1) / 2) + " " + n(89 - (k % 2) * 1.5) + " " + n(x1) + " " + n(97 - (k == 3 ? 0 : 0));
    }
    cape += "V38Z";
    std::string folds;
    for (const char* d : {"M30 50C24 62 21 76 20.5 90", "M38 48C34 62 32 76 31.5 90", "M45.5 47C43.5 62 42.5 76 42 90"}) folds += brush(d, spindle(2.6, 0.55));
    c.add("vampire-collar", "Плащ с воротником",
          {G::F(sym(cape)), G::X(sym(folds)), G::F("M35 44C36 29 42 18 50 12C58 18 64 29 65 44Z"), G::XS(sym(collar), 2.6), G::F(sym(collar)),
           G::X("M41.5 44C42.5 33 45.5 26 50 21.5C54.5 26 57.5 33 58.5 44Z"),
           G::XS(sym("M43 40.5 37 41.5 31 44.5M38 36C31 33 24.5 28.5 17.5 21M41 31C36.5 25.5 33.5 19 31.5 11"), 1.4), G::XS(circ(50, 41, 7), 2.6),
           G::F(circ(50, 41, 5.8)), G::X("M50 37 53.5 41 50 45 46.5 41Z")});
  }
  // Вампир (камея в профиль): гладко зачёсанные назад волосы штрихами, острое ухо, орлиный нос, острый подбородок,
  // клык, впалая скула, высокий острый воротник плаща с застёжкой.
  {
    const std::string head = "M36 21C32 25.5 29 31 28.5 37C28.4 38.5 27.6 39.6 27 40.4C25 44 22 48 19 51.5C21 53.2 23.5 53.5 25.6 53L24.8 55.8 26.6 57.6"
                             " 25.2 59.6C24 62.5 23.6 65.5 25 68.5C26 70.6 28 71.6 31 71.2C37 70.2 43 66.5 47 61.5L47.5 76H60.5"
                             "C61 68 64 62 66 55C68.5 46 68.5 32 62 23C55.5 14 44 13 36 21Z" +
                             std::string("M25.8 56.6 24.2 61.2 27 58.2Z");
    const std::string ear = "M50.5 54.5C49.5 47.5 52 40.5 57.5 33.5C58 41.5 56.5 48.5 54 56Z";
    const std::string cape = "M11 99C17 87 31 80 45 78L61 76C67 60 70 42 85 17C82 40 84 72 91 99Z";
    // пряди: линии между линией роста волос и контуром затылка, сходятся ко лбу и к шее
    const std::vector<Pt> hl = resample("M35.5 21.5C39 27 42.5 32 46.5 36.5C49.5 40 52 45 54 52C55.5 57 58 61 62 62.5", 60),
                          ol = resample("M35.5 21.5C43 13.5 55 14 62 23C68 31 68.5 44 66 54C65 57 63.5 60 62 62.5", 60);
    std::string strands;
    for (int k = 1; k <= 4; k++)
      for (int i = 6 + k; i <= 55 - k; i++) strands += (i == 6 + k ? "M" : "L") + ptS(lerp(hl[size_t(i)], ol[size_t(i)], k / 5.0));
    c.add("vampire-lord", "Вампир",
          moved({G::F(cape), G::XS(inset(cape, 4.2, 0.42, 0.72), 1.5), G::XS(head, 2.4), G::F(head), G::XS(strands, 1.4),
                 G::XS("M35.5 21.5C39 27 42.5 32 46.5 36.5C49.5 40 52 45 54 52C55.5 57 58 61 62 62.5", 1.6), G::XS(ear, 2), G::F(ear),
                 G::XS("M53.2 50C53.8 45 55 41 56.6 37.5", 1.4), G::X("M30.4 41.6 37.4 40 33.5 43.2Z"),
                 G::XS("M29 37.6 38 35.6M36 50C38.5 55 41.5 58.5 45 60.5", 1.5), G::XS(circ(53, 83, 5), 2.2), G::F(circ(53, 83, 3.8))},
                Affine::translate(-2, -4)));
  }
}

// ---------------------------------------------------------------- природа
// Лопастной лист (дуб): средняя жилка от основания b к кончику t, огибающая ширины env, lobes лопастей на сторону,
// глубина выемок deep (доля), сдвиг лопастей справа phase. Возвращает контур; жилки лопастей — в veins.
std::string lobedLeaf(Pt b, Pt t, double env, double lobes, double deep, double phase, std::string* veins) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::hypot(dx, dy), ux = dx / len, uy = dy / len;
  auto at = [&](double s, double side) { return Pt{float(b.x + dx * s - uy * side), float(b.y + dy * s + ux * side)}; };
  auto width = [&](double s, double ph) {
    const double e = env * std::pow(std::sin(kPi * std::pow(s, 0.85)), 0.9);
    return e * (1 - deep + deep * std::pow(std::fabs(std::sin(kPi * (lobes * s + ph))), 0.55));
  };
  const int n = 160;
  std::string l = "M" + ptS(b), r;
  for (int i = 1; i <= n; i++) l += "L" + ptS(at(double(i) / n, width(double(i) / n, 0)));
  for (int i = n - 1; i >= 1; i--) l += "L" + ptS(at(double(i) / n, -width(double(i) / n, phase)));
  if (veins)
    for (int side : {1, -1})
      for (int k = 0; k < int(lobes); k++) {
        const double s = (k + 0.5 - (side < 0 ? phase : 0)) / lobes;
        if (s < 0.08 || s > 0.9) continue;
        *veins += "M" + ptS(at(s - 0.05, 0)) + "L" + ptS(at(s, side * width(s, side < 0 ? phase : 0) * 0.72));
      }
  return l + "Z";
}

void nature(Catalog& c) {
  c.group("Природа");
  // Дубовый лист: округлые лопасти с глубокими выемками, сужающаяся средняя жилка, жилки лопастей, черешок.
  {
    std::string veins;
    const std::string leafD = lobedLeaf({50, 84}, {50, 5}, 27, 4.6, 0.5, 0.18, &veins);
    c.add("nature-leaf", "Дубовый лист",
          moved({G::F(leafD + brush("M50 82C50 88 49 93 47 98", linear(4, 2.6))), G::X(brush("M50 79C50 58 50 36 50 10", linear(3.2, 0.6))),
                 G::XS(veins, 1.5)},
                rotAt(30, 50, 50) * scaleAt(1.04f, 50, 50)));
  }
  // Олень (голова анфас, «кабошон»): ветвистые рога с отростками, острые уши, узкая морда, раскосые глаза, ноздри.
  {
    std::string antler = brush("M45 41C37 35 29 31 25 22C22.5 16 23 9 26 2", linear(7.6, 2.6));
    antler += thorn({33.5, 33.5}, {40.5, 16.5}, 5.4, -0.12) + thorn({27, 27}, {13, 18}, 5, -0.12) + thorn({24.5, 19}, {11.5, 7}, 4.4, -0.1) +
              thorn({40, 38}, {45, 29.5}, 4.2, -0.1);
    const std::string head = "M50 36C43 36 38.5 40.5 38 47C37.5 54 40 62 43 71C44.5 77 45.5 84 46.5 89C47.4 93 48.6 95 50 95H50.5V36Z";
    const std::string ear = leaf({40, 47}, {19, 39}, 11, -0.05);
    c.add("nature-stag", "Олень",
          {G::F(sym(antler + head)), G::XS(sym(ear), 2.2), G::F(sym(ear)), G::X(sym(leaf({37.5, 46.6}, {25, 42}, 4))),
           G::X(sym("M41.3 55.5 47.2 57.8 42 59.4Z") + sym(xf(ell(0, 0, 1.3, 2.6), place(47.6, 88.5, -18)))), G::XS("M50 39V51", 1.5),
           G::XS(sym("M44.5 63C46 70 47 76 47.8 82"), 1.5)});
  }
  // Мухоморы: высокий гриб с колоколом шляпки и малый — шляпки в мелких бородавках, юбочка, клубневидное основание,
  // острые травинки.
  {
    const std::string cap1 = "M14 47C14 27 28 9 46 9C64 9 78 27 78 47C70 50.5 58 52 46 52C34 52 22 50.5 14 47Z";
    const std::string stem1 = "M41.5 51C42 64 41 76 39 86C42.5 89.5 50.5 89.5 54 86C52 76 51 64 51.5 51Z" + ell(46.5, 88.5, 10.5, 6) +
                              "M35.5 59C39 55.5 54 55.5 57.5 59L55.5 64.5C51.5 62.5 41.5 62.5 37.5 64.5Z";
    const std::string cap2 = "M65 73C65 63.5 71.5 57 79.5 57C87.5 57 94 63.5 94 73C89.5 75 84.5 75.5 79.5 75.5C74.5 75.5 69.5 75 65 73Z";
    const std::string stem2 = "M77 74.5C77 82 76.5 88 75.5 92.5H84C83 88 82.5 82 82.5 74.5Z" + ell(79.5, 93.5, 7, 3.2);
    const std::string grass = thorn({11, 97}, {6, 77}, 3.6, -0.15) + thorn({16, 97}, {21, 80}, 3.4, 0.15) + thorn({62, 97}, {59, 85}, 3, -0.1) +
                              thorn({92, 97}, {97, 86}, 3, 0.15) + brush("M3 97H97", linear(2.6, 2.6), 4);
    std::string spots;
    const double sp[][3] = {{33, 19, 2.2}, {46, 14.5, 2.6}, {59, 19, 2.2}, {25, 30, 2}, {38, 27, 1.8}, {53, 26.5, 2}, {67, 29, 2}, {20, 41, 1.7},
                            {32, 38.5, 1.9}, {46, 37, 2.1}, {60, 38, 1.9}, {72, 40, 1.6}, {74, 63, 1.6}, {84.5, 62, 1.6}, {79.5, 68.5, 1.3}, {89, 68.5, 1.2}};
    for (const auto& q : sp) spots += circ(q[0], q[1], q[2]);
    c.add("nature-mushroom", "Мухомор",
          moved({G::F(stem1 + stem2 + grass), G::XS(cap1 + cap2, 2.6), G::F(cap1 + cap2), G::X(spots), G::XS("M44.5 68 44 82M48.5 68 49 82M79.5 79V90", 1.4)},
                Affine::translate(0, -3)));
  }
  // Ель: ярусы с острыми опущенными концами лап и выемками между ними, ствол.
  {
    // ярус: вершина (50, y0), низ на y1 полушириной w; нижний край — count + 1 острых концов лап с выемками
    auto tier = [](double y0, double y1, double w, int count) {
      std::string s = "M50 " + n(y0) + "C" + n(50 + w * 0.28) + " " + n(y0 + (y1 - y0) * 0.5) + " " + n(50 + w * 0.7) + " " + n(y1 - 2) + " " +
                      n(50 + w) + " " + n(y1 + 2);
      for (int k = 1; k <= count; k++) {
        const double x = 50 + w - 2 * w * k / count, xm = x + w / count, f = std::fabs(x - 50) / w;
        s += "Q" + n(xm + w / count * 0.3) + " " + n(y1 - 1) + " " + n(xm) + " " + n(y1 - 5) + "Q" + n(xm - w / count * 0.3) + " " + n(y1 - 1) + " " + n(x) +
             " " + n(y1 + 2 * f);
      }
      return s + "C" + n(50 - w * 0.7) + " " + n(y1 - 2) + " " + n(50 - w * 0.28) + " " + n(y0 + (y1 - y0) * 0.5) + " 50 " + n(y0) + "Z";
    };
    const std::string t1 = tier(2, 28, 15, 2), t2 = tier(14, 48, 25, 3), t3 = tier(30, 68, 35, 4), t4 = tier(48, 86, 44, 5);
    const std::string trunk = "M45.5 82H54.5L55.5 91C58 94.5 62 96 67 97.5H33C38 96 42 94.5 44.5 91Z";
    c.add("nature-pine", "Ель",
          {G::F(t4 + trunk), G::XS(t3, 3), G::F(t3), G::XS(t2, 3), G::F(t2), G::XS(t1, 3), G::F(t1),
           G::XS("M50 84V91M50 9V22M50 30V42M50 50V62M50 70V80", 1.5)});
  }
  // Дуб с корнями: крона из дубовых листьев внахлёст с жёлудями, ствол с ветвями и корой, раскидистые корни.
  {
    std::vector<L> v;
    std::string roots;
    for (int s : {-1, 1}) {
      roots += brush("M" + n(50 + s * 4) + " 80C" + n(50 + s * 12) + " 84 " + n(50 + s * 26) + " 85 " + n(50 + s * 44) + " 94", linear(6.5, 0.8));
      roots += brush("M" + n(50 + s * 3) + " 82C" + n(50 + s * 8) + " 88 " + n(50 + s * 14) + " 93 " + n(50 + s * 22) + " 98.5", linear(5.5, 0.8));
    }
    roots += brush("M50 82C50 88 49 93 48 98.5", linear(5, 0.8));
    const std::string trunk = "M43.5 81C46.5 72 46.5 63 43 55L48.5 59 50 48 51.5 59 57 55C53.5 63 53.5 72 56.5 81Z" +
                              brush("M46 58C40 52 34 48 26 46", linear(5, 2)) + brush("M54 58C60 52 66 48 74 46", linear(5, 2));
    v.push_back(G::F(trunk + roots));
    v.push_back(G::XS("M48 66 47.5 75M52 68 52.5 77", 1.5));
    // листья: внешнее кольцо наружу, затем внутренние — поверх
    struct Lf {
      double a, r0, r1;
    };
    std::vector<Lf> ls;
    for (int k = 0; k < 9; k++) ls.push_back({-180 + k * 22.5, 19, 39});
    for (int k = 0; k < 5; k++) ls.push_back({-162 + k * 36.0, 6, 25});
    ls.push_back({-90, -2, 15});
    for (const Lf& f : ls) {
      const Pt b = polar(50, 41, f.r0, f.a), t = polar(50, 41, f.r1, f.a);
      const std::string lf = lobedLeaf(b, t, 8.4, 3.6, 0.42, 0.2, nullptr);
      v.push_back(G::XS(lf, 1.8));
      v.push_back(G::F(lf));
      v.push_back(G::XS("M" + ptS(lerp(b, t, 0.15)) + "L" + ptS(lerp(b, t, 0.8)), 1.3));
    }
    c.add("nature-oak", "Дуб с корнями", v);
  }
  // Воющий волк: сидящий волк в профиль, морда к полумесяцу, грива острыми прядями, пушистый хвост, камень.
  {
    const std::string wolf =
        "M21 12L30 13.5C33 12.5 36 13 38.5 14.5L43 5 45 16.5C48 20 50 25 52 31C56 38 61 46 64.5 55C68 63 71 70 71 78"
        "C74 82 79 85 86 86.5C91 87.5 95 89.5 97 93C90 95 83 95 76 93.5L69 93H33C31.5 93 31 91.5 32.5 90.5L35 89C35 82 34.5 74 33.5 67"
        "L28 64 32.5 60 27 56 31.5 52.5 26 48 30.5 45 26 40.5 30 37.5C28.5 33 27 29 25.5 26L33 22.5 22.5 16Z";
    const std::string rock = "M18 93H90L95 98H12Z";
    c.add("nature-wolf", "Воющий волк",
          moved({G::F(circ(76, 24, 19)), G::X(circ(67.5, 19.5, 17)), G::XS(wolf, 2.6), G::F(wolf + rock), G::X("M33.5 17.4 38 17.2 34.5 19.6Z"),
                 G::XS("M56 62C50 70 49 81 53 93M42 64C41 74 41.5 83 43 93M83 89C87.5 89.5 91 90.5 93.5 92M62 50C58 47 55 42 53 37", 1.5),
                 G::XS("M18 93H90", 1.5)},
                Affine::translate(-4, 0)));
  }
  // Сова: голова с острыми ушными пучками и лицевым диском (кайма сердечком, лучи перьев), глаза-кольца, клюв; грудь
  // в чешуе перьев, сложенные крылья с острыми маховыми перьями, перекрещёнными внизу, когти на ветке.
  {
    const std::string body = "M50 15C59 15 66 18.5 70 25L76 12C79 20 79 29 76.5 36C79.5 45 79 57 75 67C71 77 62 84 50 86C38 84 29 77 25 67"
                             "C21 57 20.5 45 23.5 36C21 29 21 20 24 12L30 25C34 18.5 41 15 50 15Z";
    const std::string wing = "M23.5 42C19 55 20 70 27 82L33 97 34.5 86 39 95 39.5 83C36 71 34 57 33.5 45Z";
    const std::string branch = brush("M3 90C30 86 70 92 97 86", spindle(7, 0.5)) + leaf({84, 88}, {95, 76}, 6, 0.1);
    const std::string disc = "M50 59C44 61 33 60 28.5 51.5C24.5 43 28 33.5 35.5 31.5C41.5 30 47 32.5 50 37C53 32.5 58.5 30 64.5 31.5C72 33.5 75.5 43 71.5 51.5"
                             "C67 60 56 61 50 59Z";
    std::string rays;
    for (int k = 0; k < 9; k++) rays += "M" + pt(40.5, 45.5, 8.2, 120 + k * 17) + "L" + pt(40.5, 45.5, 11, 120 + k * 17);
    std::string breast;
    for (int r = 0; r < 4; r++)
      for (int k = 0; k <= r % 2 + 1; k++) {
        const double x = 50 + (k - (r % 2 + 1) * 0.5) * 7, y = 64 + r * 5.2;
        breast += "M" + n(x - 3.2) + " " + n(y) + "L" + n(x) + " " + n(y + 3) + "L" + n(x + 3.2) + " " + n(y);
      }
    c.add("nature-owl", "Сова",
          moved({G::F(body + branch), G::XS(sym(wing), 1.8), G::F(sym(wing)), G::XS(sym("M28 56C28.5 66 30.5 74 33.5 82M35.5 88 34.5 84"), 1.4),
                 G::XS(disc, 1.6), G::XS(sym(rays), 1.3), G::X(sym(circ(40.5, 45.5, 5.6))), G::F(sym(circ(40.5, 45.5, 3.6))), G::X(sym(circ(40.5, 45.5, 1.6))),
                 G::F("M50 46 53 51 50 58 47 51Z"), G::XS(breast, 1.4), G::XS(sym("M43 86 42 92M47.5 86.5 47.5 93"), 1.6)},
                Affine::translate(0, -3.5f)));
  }
  // Медведь-друид: тяжёлая голова в профиль («оторванная» — рваный край шеи), круглое ухо, прямой нос, грива прядями
  // по загривку, узкий глаз под бровью, пасть; на голове — венок из листьев.
  {
    const std::string head = "M5 52C5 48.5 7 46.5 10.5 46C17 45 23.5 43.5 29 40.5C34 35.5 40 32 47 31C52 30.5 57 31.5 61 34"
                             "C67 39 72 50 76 62C78.5 70 80 77 81.5 84L77.5 89 73 85 68.5 91 64 86 59 92 54.5 86.5 49.5 91"
                             "C48 82 43 75.5 36 72C31.5 70 27 68.5 22.5 66.5C17 64.5 12.5 62 9 59C6.5 57 5 55 5 52Z";
    const std::string ear = "M52 34C51 28 54.5 23.5 59.5 23.5C64.5 23.5 67 28.5 65.5 34.5Z";
    std::string mane;
    for (int k = 0; k < 4; k++) {
      const CurvePt q = bezier({62, 35}, {68, 44}, {75, 60}, {80.5, 80}, 0.16 + k * 0.23);
      mane += thorn({float(q.x - 2.5), float(q.y)}, {float(q.x + 8), float(q.y + 6)}, 7, -0.15);
    }
    // венок на голове: листья парами вдоль обруча, откинуты назад
    std::string crown, veins;
    const Pt w0{30, 42}, w1{38, 31}, w2{53, 26}, w3{67, 32};
    for (int k = 0; k < 6; k++) {
      const CurvePt q = bezier(w0, w1, w2, w3, 0.04 + k * 0.17);
      const Pt b{float(q.x), float(q.y)}, tg{float(q.ny), float(-q.nx)}, up{float(-q.nx), float(-q.ny)};
      for (const Pt& tip : {b + tg * 10.5f + up * 6.5f, b + tg * 10.5f - up * 3.5f}) {
        crown += leaf(b, tip, 5.2, 0, 0.42);
        veins += "M" + ptS(lerp(b, tip, 0.25)) + "L" + ptS(lerp(b, tip, 0.8));
      }
    }
    c.add("nature-bear", "Медведь-друид",
          moved({G::XS(head + mane + ear, 2.4), G::F(head + mane + ear), G::XS(crown, 1.8), G::F(crown), G::XS(veins, 1.3),
                 G::X("M5 51.5C6 48.5 9 47.5 12.5 48C12 51 9.5 53 5.5 53.5Z" + std::string("M29.5 46.5 38.5 44.6 32 49Z")),
                 G::XS("M28 42.8 38.5 40.6M9 57 21 60.5M38 56C44 59 49 64 52 71M60 46C63 55 66 64 67.5 73M56.5 31.5C56 28.5 58 26.5 60.5 26.5", 1.5)},
                Affine::translate(4.5f, -5.5f) * scaleAt(1.08f, 45, 55)));
  }
  // Папоротник: S-образный стебель с перистыми заострёнными листочками, на конце — закрученная улитка.
  {
    const Pt r0{47, 97}, r1{46, 68}, r2{36, 42}, r3{53, 24};
    std::vector<std::string> pinnae;
    for (int k = 9; k >= 0; k--) {
      const double t = 0.06 + 0.088 * k, len = 36 * (1 - 0.72 * t / 0.85);
      const CurvePt q = bezier(r0, r1, r2, r3, t);
      const Pt b{float(q.x), float(q.y)}, up{float(q.ny), float(-q.nx)};
      for (int side : {-1, 1}) {
        const Pt dir{float(q.nx * side * 0.84 + up.x * 0.54), float(q.ny * side * 0.84 + up.y * 0.54)};
        pinnae.push_back(leaf(b, b + dir * float(len), len * 0.3, 0.04 * side, 0.4));
      }
    }
    std::vector<L> v;
    layeredFeathers(v, pinnae, 1.6f, false);
    const std::string stem = brush("M47 97C46 68 36 42 53 24C57 19.5 63 18 67 21C70.5 24 70 29 66 30.5C63 31.5 60.5 29 61.5 26.5", linear(5, 2.4));
    v.push_back(G::XS(stem, 1.6));
    v.push_back(G::F(stem));
    c.add("nature-fern", "Папоротник", moved(v, Affine::translate(3, -6) * scaleAt(1.06f, 50, 60)));
  }
}

// ---------------------------------------------------------------- стихии
// Логарифмическая спираль: центр, угол a0 → a1 (градусы), радиус r0 → r1, ширина w0 → w1.
std::string spiral(double cx, double cy, double a0, double a1, double r0, double r1, double w0, double w1, int steps = 48) {
  std::vector<Pt> c;
  std::vector<double> w;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps;
    c.push_back(polar(cx, cy, r0 * std::pow(r1 / r0, t), a0 + (a1 - a0) * t));
    w.push_back(w0 + (w1 - w0) * t);
  }
  return strip(c, w);
}

// Пламя: капля-основание и языки, внутреннее пламя вырезом с сердцевиной (вписано в 100 × 100 от y = top до низа 96).
std::vector<L> flameLayers() {
  const std::string core = "M50 96C35 96 27 86 27 74C27 62 37 54 50 40C63 54 73 62 73 74C73 86 65 96 50 96Z";
  const std::string tongues = tongue("M50 64C43 50 58 37 50 20C47.5 13.5 49.5 8 54 2", 17) + tongue("M38 72C29 62 35 50 28 40C26 35.5 22.5 32.5 18 31", 12) +
                              tongue("M62 72C71 62 65 50 72 40C74 35.5 77.5 32.5 82 31", 12) + tongue("M31 82C21 76 19 66 11 58", 8) +
                              tongue("M69 82C79 76 81 66 89 58", 8);
  const std::string inner = "M50 90C42 90 37 84.5 37 77.5C37 70 42 66 45 59.5C46.5 64 48 66 49.5 67C48.5 60.5 50.5 54 55 48.5C56.5 56.5 61.5 61 62.5 69"
                            "C64 79 58.5 90 50 90Z";
  return {G::F(core + tongues), G::XS("M50 30C48 38 47 44 47.5 50M30 46C33 52 34.5 58 34.5 63M70 46C67 52 65.5 58 65.5 63", 1.5), G::X(inner),
          G::F(xf(inner, scaleAt(0.5f, 50, 82)))};
}

void elements(Catalog& c) {
  c.group("Стихии");
  // Огонь: геральдическое пламя — языки S-образными мазками с острыми концами, внутреннее пламя с сердцевиной.
  c.add("element-fire", "Огонь", flameLayers());
  // Вода: капля с двойной каймой и бликом, внутри — волнистые полосы; брызги по сторонам.
  {
    const std::string big = drop(50, 3, 64, 30);
    std::string waves;
    for (int k = 0; k < 3; k++) {
      const double y = 62 + k * 9.5, x0 = 26 + (k == 2 ? 6 : 0), x1 = 74 - (k == 2 ? 6 : 0);
      waves += "M" + n(x0) + " " + n(y);
      for (double x = x0; x < x1 - 0.1; x += 8)
        waves += "Q" + n(x + 2) + " " + n(y - 3) + " " + n(x + 4) + " " + n(y) + "T" + n(std::min(x1, x + 8)) + " " + n(y);
    }
    c.add("element-water", "Вода",
          {G::F(big + drop(13, 32, 44, 5.5) + drop(87, 32, 44, 5.5)), G::XS(inset(big, 4.2, 0.1, 0.9), 1.6), G::XS(waves, 2.2),
           G::X(brush("M38 30C33.5 37 30.5 45 30 54", spindle(3.4, 0.7)))});
  }
  // Воздух: три порыва ветра — каллиграфические мазки от острого хвоста к завитку.
  {
    // порыв: волнистая линия от x0 к спирали (центр cx, cy; up — завиток вверх), толще всего у входа в завиток
    auto gust = [](double x0, double cx, double cy, double r, bool up, double turn, double w) {
      std::vector<Pt> pts;
      std::vector<double> ws;
      const double y = up ? cy + r : cy - r;
      for (int i = 0; i <= 16; i++) {
        const double t = i / 16.0;
        pts.push_back({float(x0 + (cx - x0) * t), float(y + std::sin(t * kPi * 1.5) * 1.6 * (1 - t))});
        ws.push_back(0.6 + (w - 0.6) * std::pow(t, 0.8));
      }
      for (int i = 1; i <= 44; i++) {
        const double t = i / 44.0, a = up ? 90 - turn * t : -90 + turn * t;
        pts.push_back(polar(cx, cy, r * (1 - 0.55 * t), a));
        ws.push_back(w - (w - 1.6) * std::pow(t, 0.8));
      }
      return strip(pts, ws);
    };
    c.add("element-air", "Воздух",
          moved({G::F(gust(6, 60, 21, 13, true, 330, 9.5) + gust(3, 76, 63, 13, false, 310, 10) + gust(16, 48, 83, 9.5, false, 290, 8))},
                Affine::translate(3, -1)));
  }
  // Земля: горы со снежными шапками, гравировка теней на склонах, подножие.
  {
    const std::string hills = "M2 92L23 46 31 56 50 10 64 39 76 30 98 92Z";
    const std::string shadow = "M50 12 63.5 39 60 47 66 64 61 92H50L53.5 70 47.5 50 51 34ZM76 32 97 91H82L85 74 79 56 80 44Z"
                               "M23 48 30.5 57 28 66 31 92H22L25.5 72 21 60Z";
    c.add("element-earth", "Земля",
          {G::F(hills), G::XS("M39.5 34 44 38.5 48 33 52.5 39.5 57.5 34M17 59 20 56 23.5 60 27 56 30 59.5M69.5 43 73 40 76.5 44 80 40 83 43.5", 2.4),
           G::XS(hatch(shadow, -55, 3.2, 1.2), 1.4), G::XS("M4 86H96", 1.6)});
  }
  // Молния: грозовая туча с завитками-волютами и разящая острая молния.
  {
    const std::string cloud = "M15 46C10 46 6.5 42.5 6.5 38C6.5 33 10.5 29.5 15.5 30C16 22 23 17 31 19C34 11 42 7 50.5 8.5C58 3.5 70 5.5 74.5 13.5"
                              "C82.5 12.5 89.5 19 88.5 27C94 28.5 96.5 34 94 39C92.5 42.5 89.5 46 85 46Z";
    const std::string bolt = "M60 36L37 65 50 65.5 35 98 70 56.5 56.5 56 70 36Z";
    c.add("element-lightning", "Молния",
          {G::F(bolt), G::XS("M58.5 44 47 59M55 72 45 86", 1.5), G::XS(cloud, 3), G::F(cloud),
           G::XS(inset(cloud, 4, 0.12, 0.8) + "M12 40.5H88", 1.5)});
  }
  // Лёд: снежинка — шесть лучей с отростками и ромбами на концах, шестигранное сердце с вырезом-звездой.
  {
    std::string arms;
    for (int k = 0; k < 6; k++) {
      const double a = -90 + 60 * k;
      arms += brush("M" + pt(50, 50, 8, a) + "L" + pt(50, 50, 38, a), linear(6.6, 3.6));
      for (auto [d, len, w] : {std::tuple{18.0, 12.5, 4.6}, std::tuple{28.0, 9.5, 3.8}}) {
        const Pt q = polar(50, 50, d, a);
        arms += thorn(q, polar(q.x, q.y, len, a - 50), w, 0) + thorn(q, polar(q.x, q.y, len, a + 50), w, 0);
      }
      arms += "M" + pt(50, 50, 35, a - 5) + "L" + pt(50, 50, 47.5, a) + "L" + pt(50, 50, 35, a + 5) + "L" + pt(50, 50, 31.5, a) + "Z";
    }
    c.add("element-frost", "Лёд", {G::F(arms + star(50, 50, 14, 12, 6, -90)), G::X(star(50, 50, 8.5, 4, 6, -90)), G::F(circ(50, 50, 2.6))});
  }
  // Вулкан: конус с кратером и потоками лавы, извержение языками пламени, летящие камни.
  {
    const std::string cone = "M2 97L30 52C34 46 38 45 42 46.5L50 50.5 58 46.5C62 45 66 46 70 52L98 97Z";
    const std::string plume = tongue("M44 50C38 44 36 36 30 30C26.5 26.5 22 25 18 26", 9) + tongue("M50 50C45 38 56 27 50 15C48 10 49.5 5.5 53 1.5", 12) +
                              tongue("M56 50C62 44 64 36 70 30C73.5 26.5 78 25 82 26", 9) + tongue("M47 50C42 42 43 34 38 26", 6) +
                              tongue("M53 50C58 42 57 34 62 26", 6);
    auto rock = [](double x, double y, double s, double deg) {
      return xf("M-3 -2 1 -3.5 3.5 0 1.5 3 -2.5 2.5Z", place(float(x), float(y), float(deg), float(s)));
    };
    c.add("element-magma", "Вулкан",
          {G::F(plume + rock(19, 9, 1, 20) + rock(81, 9, 1, -30) + rock(32, 12, 0.75, 60) + rock(68, 13, 0.75, 10)), G::XS(cone, 3), G::F(cone),
           G::X(brush("M45 51C41 61 44 69 38 79C34 86 30 91 26 97", linear(3.4, 1.6)) + brush("M55 51C59 63 56 72 62 82C65 88 68 92 70 97", linear(3.4, 1.6)) +
                brush("M50 53V68", linear(2.6, 1.2))),
           G::XS("M16 86 22 83M82 85 77 81M30 70 34 68", 1.5)});
  }
  // Смерч: воронка из закрученных полос-веретён, сужается книзу, вокруг — острые обломки.
  {
    std::string bands;
    for (int i = 0; i < 8; i++) {
      const double y = 8 + i * 10.6, w = 42 - i * 4.9, x = 50 + std::sin(i * 0.9) * 3.5;
      const std::string d = "M" + n(x - w) + " " + n(y) + "C" + n(x - w * 0.5) + " " + n(y + 6) + " " + n(x + w * 0.5) + " " + n(y + 6) + " " + n(x + w) + " " +
                            n(y - 1.5);
      bands += brush(d, spindle(7 - i * 0.4, 0.8));
    }
    bands += tongue("M52 86C50 90 48 93 44 97", 4.5);
    auto shard = [](double x, double y, double s, double deg) { return xf("M0 -4 2.5 1 0.5 4 -2.5 0.5Z", place(float(x), float(y), float(deg), float(s))); };
    c.add("element-tornado", "Смерч", {G::F(bands + shard(13, 40, 1.1, 30) + shard(87, 55, 1, -20) + shard(20, 70, 0.9, 60) + shard(84, 26, 0.8, 10))});
  }
  // Волна: закрученный гребень с острыми когтями пены над морем.
  {
    const std::string sea = "M2 97V84C12 80 24 80 34 83C46 86 60 82 70 72C76 66 82 60 90 58L98 64V97Z";
    std::string claws;
    for (int k = 0; k < 5; k++) {
      const double a = -150 + k * 26;
      const Pt b = polar(44, 40, 33, a);
      claws += thorn(b, polar(44, 40, 39, a - 20), 4.6, -0.25);
    }
    c.add("element-wave", "Волна",
          moved({G::F(sea + spiral(44, 40, 25, -300, 44, 10, 20, 4) + claws), G::XS(spiral(44, 40, 10, -250, 36, 15, 1.6, 1.6), 1.6),
                 G::XS("M8 90C18 87 28 88 36 91M50 92C62 90 74 86 84 79", 1.6)},
                Affine::translate(0, 1.5f) * scaleAt(0.98f, 50, 50)));
  }
  // Кристалл льда: высокая шестигранная призма с гранями, две боковые призмы, искра, основание.
  {
    const std::string prism = "M0 -1L1 -0.72V0.72L0 1-1 0.72V-0.72Z";
    const std::string mid = xf(prism, Affine{12, 0, 0, 44, 50, 47});
    const std::string side = xf(prism, place(27, 61, -28) * Affine::scale(8, 25)) + xf(prism, place(74, 63, 26) * Affine::scale(7.5, 22));
    c.add("element-crystal", "Кристалл льда",
          {G::F(side), G::XS(xf("M0 -25 0 18", place(27, 61, -28)) + xf("M0 -22 0 16", place(74, 63, 26)), 1.5), G::XS(mid, 3.2), G::F(mid),
           G::XS("M38 15.3 50 23.5 62 15.3M50 23.5V78.5M38 78.7 50 87 62 78.7", 1.6), G::XS("M44 30 44 70", 1.3),
           G::F(star(84, 15, 9, 1.8, 4, -90) + star(15, 30, 6, 1.3, 4, -90) + "M20 97H80L75 92H25Z")});
  }
  // Стихийный круг: двойное кольцо, внутри — огонь, вода, воздух и земля вокруг четырёхлучевой звезды.
  {
    const std::string fl = "M50 11C53 16 58 20 58 26.5A8 8 0 0 1 42 26.5C42 22.5 45 20 46.5 17C47.5 20 49 21 50 22C49.5 18 49.5 14.5 50 11Z";
    c.add("element-circle", "Стихийный круг",
          {G::S(circ(50, 50, 45), 5), G::XS(circ(50, 50, 45), 1.4), G::S(circ(50, 50, 38.5), 1.8),
           G::F(fl + drop(50, 70, 81.5, 7.5) + "M66 58.5L76 40 86 58.5Z" + spiral(24.5, 49, 180, -150, 9, 3.2, 4.6, 1.8) + star(50, 50, 14, 3.4, 4, -90)),
           G::X("M73 45.6 76 40 79 45.6 77.5 44.2 76 46.4 74.5 44.2Z" + circ(50, 27.5, 3) + circ(50, 50, 2))});
  }
}

// ---------------------------------------------------------------- гномы
// Руны (футарк) для гравировки: локальные координаты, высота 12, центр в (0, 0).
const char* const kRunes[] = {"M-2 -6V6M-2 -6 3 -3M-2 -1 3 2", "M-3 6V-6L3 -2V6", "M-2 -6V6M-2 -3 2 0-2 3", "M-3 6V-6L2 -3-3 0 3 6",
                              "M0 6V-6M0 0-4 -5M0 0 4 -5", "M0 -6 4 -2-3 5M0 -6-4 -2 3 5"};

// Кирка остриём вверх: боёк-полумесяц с острыми концами, втулка с кольцами, сужающаяся рукоять, навершие.
Fig pickFig() {
  Fig f;
  f.fill = brush("M6 35C22 17 78 17 94 35", [](double t) { return 0.6 + 10 * std::pow(std::sin(kPi * t), 0.85); }) + rrect(43, 10, 14, 17, 2.5) +
           "M46.5 26H53.5L52.8 89H47.2Z" + "M45.5 88H54.5L52.5 97H47.5Z";
  f.holes = circ(50, 14, 1.6);
  f.lines = "M43 20.5H57M47 74H53M47 78.5H53M47 83H53";
  return f;
}

void dwarves(Catalog& c) {
  c.group("Гномы");
  // Наковальня: острый рог, грань рабочей плиты, гравированная руна на стане, ступенчатое основание, искры.
  {
    const std::string anvil = "M2 29C13 26 23 24.5 32 24.5H95V37.5C87 38.5 81 40.5 75 43.5C69 46.5 65 51.5 64 57.5L68.5 70H79V81H25V70H35.5L38.5 57.5"
                              "C37.5 51 31.5 45 23.5 43C15 41 8 36.5 2 29Z";
    const std::string sparks = star(36, 10, 6.5, 1.3, 4, -90) + star(51, 5, 4.5, 1, 4, -90) + star(64, 12, 5, 1.1, 4, -90) + star(78, 7, 3.5, 0.9, 4, -90);
    c.add("dwarf-anvil", "Наковальня",
          {G::F(anvil + rrect(20, 84, 64, 6, 1.5) + rrect(14, 91, 76, 6, 1.5) + sparks), G::XS("M31 29.5H94", 1.5),
           G::XS("M6.5 31.5C14 34.5 21 37.5 28 39.5", 1.5), G::X(rrect(81, 27, 5, 4, 1)), G::XS(xf(kRunes[3], place(51.5, 60, 0, 0.95f)), 1.8),
           G::XS("M25 73.5H79", 1.5)});
  }
  // Скрещённые кирки: бойки-полумесяцы с острыми концами, втулки с кольцами, рукояти с обмоткой и навершием.
  {
    const Fig p = pickFig();
    c.add("dwarf-pickaxes", "Скрещённые кирки", crossed(p.fill, p.holes, p.lines, 1.5, 38, 50, 53, 0.96f));
  }
  // Гном: строгая маска-шлем с рожками, налобник с заклёпками, наносник, глаза под тяжёлыми бровями, усы клинками,
  // борода тремя косами с кольцами.
  {
    const std::string helm = "M26 37C26 19 37 8 50 8C63 8 74 19 74 37Z" + rrect(21, 33, 58, 8, 2) + "M47.5 40H52.5L51.5 55H48.5Z" +
                             sym(brush("M27 31C19 28 14 22 12 12C11 8 11.5 5 13 2", toPoint(8.5, 1.05)));
    const std::string face = "M27 43H73L71.5 54C69 60 61 62.5 50 62.5C39 62.5 31 60 28.5 54Z";
    const std::string beard = "M24 52C22 68 29 80 38 87L50 92 62 87C71 80 78 68 76 52C69 61 60 64 50 64C40 64 31 61 24 52Z";
    const std::string braids = brush("M50 70V91", linear(9, 8)) + thorn({50, 90}, {50, 99}, 6, 0) + sym(brush("M36 70C35 78 35.5 84 37 90", linear(7.5, 6.5)) +
                                                                                                         thorn({37, 89}, {38.5, 97}, 5, -0.05));
    const std::string moust = sym(brush("M49 57C42 57.5 34 60 27 66C24 68.5 21 69.5 17 69.5", [](double t) { return 7.5 * std::pow(1 - t, 0.75) + 0.3; }));
    std::vector<L> v{G::F(helm), G::XS("M50 10V31M39 11.5C36 18 35 25 35 31M61 11.5C64 18 65 25 65 31", 1.6),
                     G::X(circ(25, 37, 1.5) + circ(37, 37, 1.5) + circ(63, 37, 1.5) + circ(75, 37, 1.5)), G::XS(face, 2), G::F(face),
                     G::XS("M47.5 41.5V54.5M52.5 41.5V54.5", 1.5), G::X(sym("M32.5 45.5 45.5 47 44 49.5 34.5 49Z")), G::F(sym("M37 47.5 42.5 48.2 39 49Z")),
                     G::F(beard)};
    v.push_back(G::XS(braids, 2.2));
    v.push_back(G::F(braids));
    v.push_back(G::XS("M45.5 74H54.5M45.5 79H54.5M45.5 84H54.5" + sym("M32 74.5H39.5M32 79.5H39.5M32.5 84.5H40"), 1.5));
    v.push_back(G::XS(sym("M27 60C28 68 31 75 34 80"), 1.5));
    v.push_back(G::XS(moust, 2.2));
    v.push_back(G::F(moust));
    c.add("dwarf-face", "Гном", v);
  }
  // Врата в горе: двуглавая гора со снежными кромками, стрельчатая арка с клинчатыми камнями, колонны, окованные
  // створки, ступени, руна-ромб над входом.
  {
    std::string stones;
    for (int k = 1; k < 6; k++) stones += "M" + pt(50, 70, 14.5, 180 + 30 * k) + "L" + pt(50, 70, 20, 180 + 30 * k);
    c.add("dwarf-gate", "Врата в горе",
          {G::F("M2 93L29 37 37 46 52 8 66 35 72 29 98 93Z"), G::XS("M44.5 26 48.5 22.5 52 27 56 22.5 59.5 25.5M24 48 28 44.5 32 48.5", 2.2),
           G::X("M29 93V69C29 57 38.5 49 50 49C61.5 49 71 57 71 69V93Z" + std::string("M50 35.5 53.5 40 50 44.5 46.5 40Z")),
           G::F("M34.5 93V69.5C34.5 60 41 54 50 54C59 54 65.5 60 65.5 69.5V93Z"), G::XS(stones, 1.6),
           G::X("M39.5 93V70C39.5 63.5 44 58.5 50 58.5C56 58.5 60.5 63.5 60.5 70V93Z"), G::F("M42 93V70.5C42 65.5 45.5 61 50 61C54.5 61 58 65.5 58 70.5V93Z"),
           G::XS("M50 61V93M42 73H58M42 83H58", 1.5), G::X(circ(47.5, 78, 1) + circ(52.5, 78, 1)), G::XS("M22 87.5H78M17 93H83", 1.6)});
  }
  // Рунический щит: круглый щит с окованным ободом и заклёпками, крестовина оковки, руны по четвертям, умбон со звездой.
  {
    std::string runes, rivets;
    for (int k = 0; k < 4; k++) {
      const Pt p = polar(50, 50, 25, -135 + 90 * k);
      runes += xf(kRunes[k == 0 ? 0 : k == 1 ? 3 : k == 2 ? 4 : 5], place(p.x, p.y, 0, 1.15f));
    }
    for (int k = 0; k < 12; k++) {
      const Pt p = polar(50, 50, 44, 15 + 30 * k);
      rivets += circ(p.x, p.y, 1.5);
    }
    c.add("dwarf-runeshield", "Рунический щит",
          {G::F(circ(50, 50, 48)), G::XS(circ(50, 50, 40), 1.6), G::X(rivets), G::XS("M44.5 10.5V39M55.5 10.5V39M44.5 61V89.5M55.5 61V89.5"
                                                                                     "M10.5 44.5H39M10.5 55.5H39M61 44.5H89.5M61 55.5H89.5",
                                                                                     1.5),
           G::XS(runes, 2), G::XS(circ(50, 50, 12.5), 2.2), G::X(star(50, 50, 7, 2.6, 4, -90))});
  }
  // Топор с рунами: бородатый топор с изогнутым лезвием, фаской вдоль кромки и рунами, шип обуха, обмотка рукояти, навершие.
  {
    const std::string blade = "M46 14C37 13.5 25 10.5 15 4C7.5 16 5 33 9 48C11 54 14 58.5 18.5 62C24 56 33 48 46 41Z";
    const std::string axe = blade + "M46.5 8H53.5L53 92H47Z" + thorn({53, 22}, {67, 26}, 9, 0.05) + "M50 0 54 7H46Z" + "M45 90H55L53 98H47Z";
    c.add("dwarf-axe", "Топор с рунами",
          moved({G::F(axe), G::XS("M16.5 9.5C11 21 9 35 12.5 49.5", 1.5), G::XS(xf(kRunes[0], place(30, 24, 0, 1)) + xf(kRunes[4], place(29, 39, 0, 1)), 1.8),
                 G::XS("M47 72H53M47 76.5H53M47 81H53M47 85.5H53M46.5 15H53.5M46.5 34H53.5", 1.5), G::X(circ(59, 24, 1.4))},
                rotAt(28, 50, 50) * scaleAt(0.93f, 50, 50) * Affine::translate(9, 0)));
  }
  // Вагонетка с рудой: короб с досками и окованными углами, груда камней с кристаллом, колёса со спицами, рельс.
  {
    const std::string rocks = "M16 41 20 30 29 25.5 36 30 39.5 41ZM35.5 41 38.5 26 48.5 19.5 57.5 25 60.5 41ZM57.5 41 60.5 29 70.5 26 78.5 32 82.5 41Z";
    const std::string crystal = "M47 22 50.5 4 54 22Z";
    const std::string cart = "M12 41H88L80 78H20Z" + rrect(8, 37, 84, 6.5, 1.5);
    std::string spokes;
    for (int k = 0; k < 3; k++)
      for (double x : {32.0, 68.0}) spokes += "M" + pt(x, 82, 7, 60 * k) + "L" + pt(x, 82, 7, 60 * k + 180);
    c.add("dwarf-cart", "Вагонетка с рудой",
          moved({G::F(rocks), G::XS(crystal, 2.2), G::F(crystal + star(71, 13, 6, 1.3, 4, -90)), G::XS("M38.5 30 36.5 41M58.5 28 60.5 41", 1.6),
                 G::XS(cart, 3), G::F(cart), G::XS("M17 53H83M15.5 65H84.5", 1.5), G::XS("M27 43.5 29.5 75.5M73 43.5 70.5 75.5", 1.5),
                 G::X(circ(16, 40.2, 1.3) + circ(84, 40.2, 1.3) + circ(50, 40.2, 1.3)), G::XS(circ(32, 82, 9.5) + circ(68, 82, 9.5), 2.6),
                 G::F(circ(32, 82, 9.5) + circ(68, 82, 9.5) + rrect(4, 90.5, 92, 4, 1.2)), G::XS(circ(32, 82, 7.2) + circ(68, 82, 7.2), 1.4),
                 G::XS(spokes, 1.3), G::F(circ(32, 82, 2.4) + circ(68, 82, 2.4)),
                 G::F(rrect(10, 94.5, 8, 3.5, 0.8) + rrect(46, 94.5, 8, 3.5, 0.8) + rrect(82, 94.5, 8, 3.5, 0.8))},
                Affine::translate(0, -1.5f)));
  }
  // Бородатый шлем: шлем-личина с гребнем, налобником и нащёчниками, прорези глаз, кованая борода с усами и косами.
  {
    const std::string helm = "M24 39C24 19 36 7 50 7C64 7 76 19 76 39Z" + rrect(19, 35, 62, 8.5, 2) + sym("M21 42 13 64 24.5 68 27 44Z") +
                             "M25 44H75V55C75 61 70 65 64 66H36C30 65 25 61 25 55Z";
    const std::string beard = "M27 64C26 76 31 86 38 92L50 98 62 92C69 86 74 76 73 64Z";
    const std::string braids = sym(brush("M33 84C31.5 88.5 31 92 32 96", linear(6.5, 5.5)) + thorn({32, 95}, {33, 99.5}, 4.5, 0));
    c.add("dwarf-helm", "Бородатый шлем",
          {G::F(helm), G::XS("M46.5 9V35M53.5 9V35M38 10.5C35.5 17 34.5 26 34.5 35M62 10.5C64.5 17 65.5 26 65.5 35", 1.6),
           G::X(sym("M30 47.5H45.5L44.5 52.5H33C31.5 52 30.5 50 30 47.5Z") + sym(circ(24, 39.2, 1.5) + circ(36, 39.2, 1.5))),
           G::XS(sym("M22 48 18.5 61"), 1.5), G::XS(beard, 2.6), G::F(beard), G::XS("M35 61C42 55.5 47 56.5 50 60C53 56.5 58 55.5 65 61", 2.4),
           G::XS("M50 69V92M43 68.5 42 86M57 68.5 58 86M36.5 68 36 80M63.5 68 64 80", 1.5), G::XS(braids, 2), G::F(braids),
           G::XS(sym("M30 88.5H35.5M30.5 92H35"), 1.4)});
  }
  // Кирка и самоцвет: огранённый камень с гранями и бликом поверх кирки, блёстки.
  {
    const std::string gem = "M30 42H70L82 55 50 92 18 55Z";
    const Fig p = pickFig().moved(rotAt(-42, 50, 50) * scaleAt(0.98f, 50, 50) * Affine::translate(0, -4));
    std::vector<L> v;
    put(v, p, 1.5f);
    v.push_back(G::XS(gem, 4));
    v.push_back(G::F(gem));
    v.push_back(G::XS("M18 55H82M30 42 39 55 50 42 61 55 70 42M39 55 50 92 61 55", 1.6));
    v.push_back(G::X("M33.5 46.5 36 50.5H28Z"));
    v.push_back(G::F(star(84, 22, 7.5, 1.6, 4, -90) + star(14, 82, 6, 1.3, 4, -90)));
    c.add("dwarf-gem", "Кирка и самоцвет", v);
  }
}

// ---------------------------------------------------------------- эльфы
// Лист по изогнутой средней жилке d: ширина wm·профиль; жилки — линии от середины к краю, наклонённые к кончику.
struct LeafFig {
  std::string fill, midrib, veins;
};
LeafFig curvedLeaf(std::string_view d, double wm, int veinCount) {
  auto w = [=](double t) { return wm * std::pow(std::sin(kPi * std::pow(t, 0.9)), 0.75) * (1 - 0.12 * t); };
  LeafFig f;
  f.fill = brush(d, w, 90);
  f.midrib = brush(d, [=](double t) { return t < 0.04 ? 0.0 : 2.8 * (1 - t) + 0.3; }, 60);
  const std::vector<Pt> c = resample(d, 100);
  for (int k = 0; k < veinCount; k++) {
    const double t = 0.18 + 0.66 * k / std::max(1, veinCount - 1);
    const size_t i = size_t(t * 100);
    const Pt p = c[i], q = c[std::min<size_t>(i + 8, 100)];
    const double dx = q.x - p.x, dy = q.y - p.y, l = std::max(1e-6, std::hypot(dx, dy)), hw = w(t) * 0.5;
    for (int s : {-1, 1}) {
      const Pt e{float(p.x - dy / l * hw * 0.78 * s + dx / l * hw * 0.7), float(p.y + dx / l * hw * 0.78 * s + dy / l * hw * 0.7)};
      f.veins += "M" + ptS(p) + "Q" + ptS(lerp(p, e, 0.5) + Pt{float(-dy / l * hw * 0.12 * s), float(dx / l * hw * 0.12 * s)}) + " " + ptS(e);
    }
  }
  return f;
}

void elves(Catalog& c) {
  c.group("Эльфы");
  // Эльфийский лист: длинный изогнутый лист с острым кончиком, средней жилкой и парными жилками, стебель-усик.
  {
    const LeafFig lf = curvedLeaf("M30 84C38 60 54 32 86 5", 42, 6);
    c.add("elf-leaf", "Эльфийский лист",
          moved({G::F(lf.fill + brush("M31 82C27 90 20 95 14 92.5C9 90 10.5 83 15.5 83.5C18 84 18.5 87 16.5 88", linear(4.4, 2))), G::X(lf.midrib),
                 G::XS(lf.veins, 1.5)},
                Affine::translate(2, 0)));
  }
  // Звезда эльфов: восемь граненых лучей (длинные и короткие), тонкое кольцо за ними, сердцевина с кольцом.
  {
    std::string s, facets;
    for (int i = 0; i < 16; i++) {
      const double r = i % 2 ? 14 : (i % 4 == 0 ? 48 : 34);
      s += (i == 0 ? "M" : "L") + pt(50, 50, r, -90 + 22.5 * i);
      if (i % 2 == 0) facets += "M" + pt(50, 50, 16, -90 + 22.5 * i) + "L" + pt(50, 50, r - 9, -90 + 22.5 * i);
    }
    s += "Z";
    c.add("elf-star", "Звезда эльфов",
          {G::S(circ(50, 50, 29), 3.2), G::XS(s, 3), G::F(s), G::XS(facets, 1.4), G::XS(circ(50, 50, 9.5), 1.8), G::X(circ(50, 50, 3.4))});
  }
  // Венок: две лавровые ветви из острых листьев с ягодами, звезда сверху, лента с раздвоенными концами.
  {
    std::string leaves, berries, stems;
    for (int k = 0; k < 9; k++) {
      const double a = 112 + k * 15;   // от низа к верху по левой дуге
      const Pt p = polar(50, 52, 36, a);
      const double tdeg = a + 90;      // по ходу ветви (к верху)
      leaves += leaf(p, polar(p.x, p.y, 15 - k * 0.4, tdeg - 36), 5.2, 0.05, 0.4) + leaf(p, polar(p.x, p.y, 15 - k * 0.4, tdeg + 36), 5.2, -0.05, 0.4);
      if (k % 3 == 1) berries += circ(polar(50, 52, 29.5, a + 6).x, polar(50, 52, 29.5, a + 6).y, 2.2);
    }
    stems = brush(arc(50, 52, 36, 104, 246), linear(3.2, 1.4));
    const std::string ribbon = "M42 83 58 92V83L42 92Z" + circ(50, 87.5, 3.4) +
                               "M46.5 90 37.5 98.5 41 95.5 43 99.5 50 91ZM53.5 90 62.5 98.5 59 95.5 57 99.5 50 91Z";
    c.add("elf-wreath", "Венок",
          {G::F(sym(stems + leaves + berries) + star(50, 12, 10, 2.2, 4, -90)), G::XS(ribbon, 2), G::F(ribbon)});
  }
  // Лебедь (геральдический, с поднятыми крыльями): S-образная шея, клюв, острые маховые перья внахлёст, волны.
  {
    std::vector<std::string> feathers;
    const Pt tips[] = {{96, 56}, {95, 41}, {90, 27}, {81, 15}, {70, 6}, {58, 3}};
    for (int k = 0; k < 6; k++) feathers.push_back(leaf({float(70 - 4.5 * k), float(66 - 1.2 * k)}, tips[k], 12.5, -0.05, 0.5));
    feathers.push_back("M38 68C36 54 42 42 52 36C58 46 68 54 84 60C72 66 56 70 38 68Z");
    std::vector<L> v{G::F("M20 68C22 81 36 88 56 88C73 88 87 81 94 65C86 69 76 71 66 69C50 67 34 65 20 68Z" + xf(ell(0, 0, 6.5, 5), place(22, 13, -10)) +
                          "M17 10.5 4 15 17 16.5Z" + brush("M8 95C24 91 36 93 46 95.5", spindle(3, 0.6)) +
                          brush("M52 95.5C66 92 80 92 94 95", spindle(3, 0.6))),
                     G::F(brush("M30 70C17 57 17 43 24 33C29 26 30.5 19 25 12", linear(9.5, 6.5))), G::X(circ(23, 12, 1.4))};
    layeredFeathers(v, feathers, 2.2f, false);
    v.push_back(G::XS("M46 75C56 79 68 79 80 74", 1.5));
    v.push_back(G::XS("M50 56C60 60 70 62 80 62", 1.4));
    c.add("elf-swan", "Лебедь", moved(v, Affine::translate(-1, -1)));
  }
  // Лук и стрелы: изогнутый эльфийский лук с листьями на концах и обмоткой, тетива, две скрещённые стрелы с острыми
  // наконечниками-листьями и оперением.
  {
    auto arrow = [](Pt tail, Pt head) {
      const Pt d = head - tail;
      const float len = std::hypot(d.x, d.y);
      const Pt u = d * (1 / len), nrm{-u.y, u.x};
      std::string s = brush("M" + ptS(tail) + "L" + ptS(head - u * 8), linear(3, 3), 8) + leaf(head - u * 11, head, 8, 0, 0.4);
      for (float k : {1.f, 6.5f})
        for (float sd : {-1.f, 1.f}) s += thorn(tail + u * (k + 7), tail + u * k + nrm * (5.5f * sd), 3, 0.15 * sd);
      return s;
    };
    const std::string limb = brush("M38 44C30 35 26 24 32 14C35 9 40 6 47 5", [](double t) { return 8 - 4.5 * t; });
    const std::string bow = limb + xf(limb, Affine{1, 0, 0, -1, 0, 100}) + rrect(34, 40, 9, 20, 3) + leaf({46, 5.5}, {58, 3}, 6, 0.08) +
                            leaf({46, 94.5}, {58, 97}, 6, -0.08);
    c.add("elf-bow", "Лук и стрелы",
          {G::F(arrow({6, 76}, {96, 22}) + arrow({6, 24}, {96, 78})), G::XS(bow, 3), G::F(bow), G::S("M47.5 6.5V93.5", 1.6),
           G::XS("M34 45H43M34 50H43M34 55H43", 1.4), G::XS("M34 36C33.5 28 36 20 42 14M34 64C33.5 72 36 80 42 86", 1.4)});
  }
  // Лунный олень: стройный олень в профиль с ветвистыми рогами, над спиной — полумесяц, звёзды.
  {
    const std::string body = "M30 46C38 47.5 48 48 60 47C68 46.5 74 45 79 47C84 49 86.5 55 85.5 61C85 64.5 83 67 80 68C72 70 60 70.5 50 70"
                             "C44 69.5 38 68.5 34.5 67C30 63 28.5 56 29 50Z";
    const std::string legs = brush("M36 64C36 74 35 84 35 94", linear(4.6, 2.6)) + brush("M42 66C43 75 42.5 85 43.5 94", linear(4.2, 2.4)) +
                             brush("M77 64C80 70 78 76 79 82C79.5 86 80 90 80.5 94", linear(5, 2.6)) +
                             brush("M82 62C86 68 85 75 86 81C86.5 86 87 90 88 94", linear(4.4, 2.4));
    const std::string neck = brush("M33 52C29 44 25 36 22 27", linear(11, 7));
    const std::string head = "M26 22C24 20 20 19.5 17 21L9 25.5C7.5 26.5 8 28.5 10 28.5L17.5 28.5C20 29 23.5 30.5 26 30Z" + leaf({24.5, 23}, {33, 17}, 5, 0.1);
    std::string antlers = brush("M22 21C18 14 20 8 27 4", linear(3.6, 1.4)) + thorn({20, 15}, {12, 9}, 2.8, -0.1) + thorn({21.5, 9.5}, {17, 2.5}, 2.4, -0.1) +
                          brush("M24.5 21C25 15 29 11 36 9", linear(3, 1.2)) + thorn({29, 12}, {31, 5}, 2.4, 0.1);
    const std::string tail = thorn({85, 49}, {91, 44}, 4, -0.2);
    c.add("elf-deer", "Лунный олень",
          moved({G::F(circ(64, 22, 16)), G::X(circ(70, 16.5, 14)), G::F(star(90, 13, 6, 1.3, 4, -90) + star(47, 13, 4.5, 1, 4, -90)),
                 G::XS(body + neck + head + antlers, 2.2), G::F(body + legs + neck + head + antlers + tail), G::X("M15.5 23.8 19.5 24 16.5 25.8Z"),
                 G::XS("M76 63C74 58 74 53 76 48.5M34 62C32.5 57.5 32.5 53 33.5 49", 1.5)},
                Affine::translate(1, 1)));
  }
  // Рог: изогнутый охотничий рог с оковкой и гравировкой, раструб с кромкой, веточка с острыми листьями.
  {
    const Pt h0{12, 86}, h1{34, 97}, h2{72, 66}, h3{80, 22};
    const CurvePt e = bezier(h0, h1, h2, h3, 1);
    const float ea = float(std::atan2(e.ny, e.nx) * 180 / kPi) - 90;
    const std::string horn = taper(h0, h1, h2, h3, 6, 27) + xf(ell(0, 0, 5, 15.5), place(float(e.x), float(e.y), ea));
    std::string bands;
    for (double t : {0.12, 0.5, 0.82}) {
      const CurvePt q = bezier(h0, h1, h2, h3, t);
      const double w = (6 + 21 * t) * 0.5 + 1;
      bands += "M" + ptS({float(q.x - q.nx * w), float(q.y - q.ny * w)}) + "L" + ptS({float(q.x + q.nx * w), float(q.y + q.ny * w)});
    }
    const std::string mouth = xf(rrect(-4, -3.5, 8, 7, 1.5), place(9, 84.5, 24));
    const std::string sprig = leaf({61, 65}, {86, 74}, 8, -0.06) + leaf({59, 70}, {74, 89}, 7.5, 0.05) + leaf({57, 62}, {63, 44}, 7, -0.05);
    c.add("elf-horn", "Рог",
          moved({G::F(horn + mouth), G::XS(bands, 2.4), G::X(xf(ell(0, 0, 2.4, 11), place(float(e.x), float(e.y), ea))),
                 G::XS("M24 86C34 87 44 82 51 75M37 69C40 64 44 58 47 50", 1.5), G::XS(sprig, 2.2), G::F(sprig),
                 G::XS("M64 67 80 72M61 72 70 83M58 59 61.5 50", 1.3)},
                Affine::translate(0, -3.5f)));
  }
  // Феникс-лист: птица с поднятыми крыльями из острых перьев-листьев, голова в профиль с хохолком, хвост из трёх
  // длинных перьев.
  {
    std::vector<std::string> wing;
    for (int k = 0; k < 6; k++) {
      const double a = -174 + k * 12.5, len = 42.5 - k * 2.4;
      const Pt b{44, 48};
      wing.push_back(leaf(b, polar(b.x, b.y, len, a), 11 - k * 0.5, 0.03, 0.55));
    }
    std::vector<L> v;
    layeredFeathers(v, wing, 1.7f, true);
    std::string ribs;
    for (int k = 0; k < 6; k++) ribs += "M" + pt(44, 48, 11, -174 + k * 12.5) + "L" + pt(44, 48, 35.5 - k * 2.4, -174 + k * 12.5);
    v.push_back(G::XS(sym(ribs), 1.3));
    // перо хвоста: тонкий стержень, к концу — заострённое опахало
    auto plume = [](double t) {
      if (t < 0.3) return 2.8 - 0.6 * t / 0.3;
      const double u = (t - 0.3) / 0.7;
      return 2.2 * (1 - u) + 5.2 * std::pow(std::sin(kPi * u), 0.8);
    };
    const std::string tail = brush("M50 64C50 76 50 86 50 99", plume) +
                             sym(brush("M48 64C46 75 41 86 36 97", plume) + brush("M46.5 63C40 70 31 73 24 79C18 84 15 89 16 95", plume));
    v.push_back(G::XS(tail, 2.2));
    v.push_back(G::F(tail));
    const std::string body = std::string("M50 38C56 38 59 45 58.5 52.5C58 60 54.5 66 50 68C45.5 66 42 60 41.5 52.5C41 45 44 38 50 38Z") +
                             "M50 25C53.5 25 55.5 28 55 31.5C54.5 35 52.5 38 50 40C47.5 38 45.5 35 45.5 31.5C45.5 28.5 47 26.5 49 26Z" +
                             "M46.5 27.5C43 27.5 39 29 36 31.5C39.5 31.5 43 31.5 46 32Z" + brush("M53 27C57 21 63 18 70 18", toPoint(3, 1)) +
                             brush("M52 26C54 19 58 14 64 11", toPoint(2.6, 1)) + brush("M50.5 26C50.5 19 52.5 13 56 8", toPoint(2.2, 1));
    v.push_back(G::XS(body, 2.8));
    v.push_back(G::F(body));
    v.push_back(G::X("M46.5 29.5 50 30.3 47.2 31.6Z"));
    v.push_back(G::XS("M46 46.5 50 49.5 54 46.5M46.5 52.5 50 55.5 53.5 52.5M47.5 58.5 50 60.5 52.5 58.5", 1.3));
    c.add("elf-phoenix", "Феникс-лист", moved(v, Affine::translate(0, -2)));
  }
  // Древо жизни: стройное дерево в кольце, изогнутые ветви с острыми листьями расходятся вверх, корни сливаются с кольцом.
  {
    std::string branches, leaves;
    const double ys[] = {63, 52, 42}, as[] = {-160, -134, -110};
    for (int k = 0; k < 3; k++) {
      const Pt b{50, float(ys[k])}, e = polar(50, 50, 36, as[k]);
      const Pt m = lerp(b, e, 0.5) + Pt{0, -6};
      branches += brush("M" + ptS(b) + "Q" + ptS(m) + " " + ptS(e), linear(4.6, 1.6), 30);
      leaves += leaf(e, polar(e.x, e.y, 10, as[k] - 4), 6, 0) + leaf(lerp(m, e, 0.35), lerp(m, e, 0.35) + Pt{-3, -9}, 5.4, 0) +
                leaf(lerp(b, m, 0.75), lerp(b, m, 0.75) + Pt{-1.5f, -9}, 5, 0);
    }
    std::string roots;
    for (double a : {116.0, 140.0, 163.0}) {
      const Pt e = polar(50, 50, 40, a), b{50, 80};
      roots += brush("M" + ptS(b) + "C" + ptS(lerp(b, e, 0.3) + Pt{0, 9}) + " " + ptS(lerp(b, e, 0.7) + Pt{0, 4}) + " " + ptS(e), linear(4.6, 1.4), 30);
    }
    c.add("elf-tree", "Древо жизни",
          {G::S(circ(50, 50, 44.5), 4.2),
           G::F(brush("M50 87C50 70 50 50 50 28", linear(9, 4)) + sym(branches + leaves + roots) + leaf({50, 30}, {50, 11}, 8, 0)),
           G::XS("M50 36V20M47.5 82V70M52.5 82V72", 1.3)});
  }
}

// ---------------------------------------------------------------- некроманты
// Череп анфас (готический): свод с височными линиями, скулы, глазницы, нос-сердечко, верхний ряд зубов, нижняя челюсть
// с зубами. Рамка ≈ 22…78 × 6…78.
Fig skullFig() {
  Fig f;
  f.fill = "M50 5C34 5 23.5 16.5 23.5 32C23.5 40 26.5 46.5 31 50.5L32 59.5C32.3 62 34 63.5 36.5 63.5H63.5C66 63.5 67.7 62 68 59.5L69 50.5"
           "C73.5 46.5 76.5 40 76.5 32C76.5 16.5 66 5 50 5Z" +
           std::string("M35 65.5H65L64 70.5C62.5 75 57.5 77.5 50 77.5C42.5 77.5 37.5 75 36 70.5Z");
  f.holes = sym("M32 32.5C36.5 30.8 42.5 31.3 46.5 34.5C47 39.5 44.5 43.8 40.2 43.8C35.8 43.8 32.6 40.2 32 35.5Z") +
            "M50 45.5C51 48.3 53.6 50.3 53.6 52.8C52.3 53.9 51 53.4 50 52.2C49 53.4 47.7 53.9 46.4 52.8C46.4 50.3 49 48.3 50 45.5Z";
  f.lines = "M38 63.6V57.6M44 63.6V57M50 63.6V57M56 63.6V57M62 63.6V57.6M41 65.9V71M47 65.9V73M53 65.9V73M59 65.9V71" +
            sym("M27.5 27C28 35 30 42 33.5 47M34.5 47.5C37 49.5 40 50.5 43 50.5") + "M57 6.5 54.5 12.5 58.5 16.5 55.5 22";
  return f;
}

// Кость: тонкий стержень от a до b, на концах — по два мыщелка.
std::string bone(Pt a, Pt b, double w, double knob) {
  const Pt d = b - a;
  const float l = std::hypot(d.x, d.y);
  const Pt u = d * (1 / l), nr{-u.y, u.x};
  std::string s = brush("M" + ptS(a + u * float(knob)) + "L" + ptS(b - u * float(knob)), [=](double t) { return w * (1 + 0.35 * std::pow(2 * t - 1, 4)); }, 16);
  for (const Pt& e : {a + u * float(knob * 0.75), b - u * float(knob * 0.75)})
    for (float sd : {-1.f, 1.f}) s += circ((e + nr * float(knob * 0.55 * sd)).x, (e + nr * float(knob * 0.55 * sd)).y, knob * 0.6);
  return s;
}

// Латинский крест с лилейными концами: середина cx, верх top, низ bottom, перекладина на y = arm, полуразмах half, ширина w.
std::string fleuryCross(double cx, double top, double bottom, double arm, double half, double w) {
  auto finial = [&](double x, double y, double deg) {
    return xf("M0 -" + n(w * 1.5) + "L" + n(w * 0.46) + " -" + n(w * 0.62) + "L" + n(w * 1.16) + " -" + n(w * 0.9) + "L" + n(w * 0.8) + " 0H-" + n(w * 0.8) +
                  "L-" + n(w * 1.16) + " -" + n(w * 0.9) + "L-" + n(w * 0.46) + " -" + n(w * 0.62) + "Z",
              place(float(x), float(y), float(deg)));
  };
  return rrect(cx - w / 2, top, w, bottom - top, 0.6) + rrect(cx - half, arm - w / 2, 2 * half, w, 0.6) + finial(cx, top + 0.5, 0) +
         finial(cx, bottom - 0.5, 180) + finial(cx - half + 0.5, arm, -90) + finial(cx + half - 0.5, arm, 90);
}

void necromancers(Catalog& c) {
  c.group("Некроманты");
  // Череп и скрещённые кости: готический череп с височными линиями, зубами и трещиной, за ним — тонкие кости с мыщелками.
  {
    const Fig sk = skullFig();
    std::vector<L> v{G::F(bone({10, 34}, {90, 95}, 5.6, 5.2) + bone({90, 34}, {10, 95}, 5.6, 5.2))};
    put(v, sk, 1.5f, 3.4f);
    c.add("necro-crossbones", "Череп и кости", moved(v, Affine::translate(0, -1)));
  }
  // Коса: длинное изогнутое лезвие с фаской и острым концом, обоймица, S-образное косовище с двумя рукоятями.
  {
    const std::string blade = brush("M84 17C63 3 32 4 5 38", [](double t) { return 16 * std::pow(1 - t, 0.7) + 0.2; });
    const std::string snath = brush("M20 97C30 78 40 64 52 48C62 35 72 24 80 13", linear(7, 5.6));
    const std::string grips = brush("M37 69C41.5 71.5 45.5 73.5 49.5 77", linear(6, 5)) + brush("M60 39C63.5 40.5 67 43 69.5 46", linear(5.6, 4.6));
    c.add("necro-scythe", "Коса",
          moved({G::F(blade + snath + grips + "M76 9 86 15 83 21 73 15Z"), G::XS(offsetCurve({84, 17}, {63, 3}, {32, 4}, {5, 38}, 4, 0.06, 0.72), 1.5),
                 G::XS("M74 13.5 82 18.5M22.5 90.5 27 93.5", 1.5)},
                Affine::translate(3, 0.5f)));
  }
  // Рука скелета: сухие тонкие фаланги с суставами, пясть, две кости предплечья, могильный холм с камнями и травой.
  {
    struct Finger {
      std::vector<Pt> p;
    };
    const Finger fs[] = {{{{44, 61}, {40, 46}, {37, 33}, {35, 24}, {34, 16}}},
                         {{{48, 60}, {48, 44}, {47.5, 30}, {47.5, 20}, {47.5, 11}}},
                         {{{52, 60}, {56, 45}, {58, 33}, {59.5, 24}, {60.5, 17}}},
                         {{{55, 62}, {62.5, 49}, {66, 40}, {68, 33}, {69.5, 27}}},
                         {{{43, 65}, {35, 58}, {28, 51}, {23.5, 45.5}}}};
    std::string bones, joints;
    for (const Finger& f : fs) {
      for (size_t i = 0; i + 1 < f.p.size(); i++) {
        const bool last = i + 2 == f.p.size();
        bones += last ? thorn(f.p[i], f.p[i + 1], 3.6, 0, 0.6) : brush("M" + ptS(f.p[i]) + "L" + ptS(f.p[i + 1]), linear(3.6, 3.2), 8);
        if (i > 0) bones += circ(f.p[i].x, f.p[i].y, 2.6);
        if (i > 0) {
          const Pt d = f.p[i + 1] - f.p[i - 1];
          const float l = std::hypot(d.x, d.y);
          joints += "M" + ptS(f.p[i] + Pt{-d.y / l * 3.2f, d.x / l * 3.2f}) + "L" + ptS(f.p[i] + Pt{d.y / l * 3.2f, -d.x / l * 3.2f});
        }
      }
    }
    const std::string wrist = ell(48.5, 64.5, 8.5, 5.5);
    const std::string arm = brush("M45.5 68L43.5 91", linear(4.6, 4)) + brush("M52 68L54 91", linear(4.2, 3.6)) + circ(45.5, 69, 3) + circ(52, 69, 2.8);
    c.add("necro-hand", "Рука скелета",
          moved({G::F(bones + wrist + arm), G::XS(joints, 1.3), G::XS("M41 66H56", 1.3),
                 G::F("M6 98C14 88 32 85 50 85C68 85 86 88 94 98Z" + thorn({16, 91}, {12, 80}, 3, -0.15) + thorn({20, 90}, {24, 79}, 3, 0.15) +
                      thorn({82, 91}, {86, 81}, 3, 0.15)),
                 G::X(ell(28, 93, 3, 1.8) + ell(70, 92.5, 3.6, 2) + ell(60, 95.5, 2, 1.2)), G::XS("M36 89.5 41 88M58 88.5 64 87.5", 1.3)},
                Affine::translate(0, -3) * scaleAt(1.04f, 50, 98)));
  }
  // Надгробие: готическая стрельчатая плита с двойной каймой и шпилем, лилейный крест, трещина, ступени, трава.
  {
    const std::string slab = "M24 84V44C24 28 36 16 50 8C64 16 76 28 76 44V84Z";
    c.add("necro-tomb", "Надгробие",
          {G::F(slab + thorn({50, 9}, {50, 1}, 4, 0) + rrect(18, 82, 64, 7, 1.5) + rrect(12, 89, 76, 8, 1.5) + thorn({10, 97}, {5, 85}, 3.4, -0.1) +
                thorn({14, 97}, {17, 87}, 3, 0.1) + thorn({90, 97}, {95, 85}, 3.4, 0.1) + thorn({86, 97}, {83, 87}, 3, -0.1)),
           G::XS(inset(slab, 4.5, 0.04, 0.96), 1.5), G::X(fleuryCross(50, 24, 66, 35, 11.5, 5)), G::XS("M70 46 64.5 53 68.5 58.5 63 67", 1.8),
           G::XS("M38 72H62M42 77H58", 1.5)});
  }
  // Лич в короне: череп под стрельчатым капюшоном, горящие глаза, зубчатая корона с острыми зубцами и камнями.
  {
    const std::string hood = "M50 2C66 8 81 23 85 44C88 63 85 81 79 97H21C15 81 12 63 15 44C19 23 34 8 50 2Z";
    const std::string opening = "M50 18C64 24 72 36 72 52C72 68 63 80 50 86C37 80 28 68 28 52C28 36 36 24 50 18Z";
    const std::string skull = "M50 34C60 34 66 41 66 51C66 57 63.5 61.5 60 64V70.5H40V64C36.5 61.5 34 57 34 51C34 41 40 34 50 34Z";
    const std::string crown = "M34 41 31.5 21 39.5 29 43.5 13 50 25 56.5 13 60.5 29 68.5 21 66 41Z";
    c.add("necro-lich", "Лич в короне",
          {G::F(hood), G::XS(sym("M22 92C20.5 80 20.5 68 22.5 58M30 95C28.5 89 28 84 28.5 79"), 1.5), G::X(opening), G::F(skull),
           G::X(sym("M37 48.5C40 46.5 44 47 46.5 50C46 54 43.5 56 40.5 56C37.5 55.5 36 52.5 37 48.5Z") + "M50 56.5 52.5 61.5H47.5Z"),
           G::F(sym(circ(41.5, 51.5, 1.8))), G::XS("M44 64.6V70.5M50 64.6V70.5M56 64.6V70.5", 1.3), G::XS(crown, 2.6), G::F(crown),
           G::X("M50 31 52.2 34.5 50 38 47.8 34.5Z" + sym(circ(40.5, 35.5, 1.5))), G::XS("M34.5 38H65.5", 1.3)});
  }
  // Гримуар с черепом: переплёт с корешком и окованными уголками, рамка, череп на обложке, застёжка, закладки-ленты.
  {
    const std::string cover = "M21 5H79C82 5 84 7 84 10V87C84 90 82 92 79 92H21Z";
    const std::string corners = "M21 5H33L21 17ZM84 17V10C84 7 82 5 79 5H72ZM21 92H33L21 80ZM84 80V87C84 90 82 92 79 92H72Z";
    const Fig sk = skullFig().moved(place(52.5, 20, 0, 0.5f) * Affine::translate(-50, 0));
    c.add("necro-grimoire", "Гримуар с черепом",
          {G::F("M14 7C14 5.5 15.5 4 17 4H23V93H17C15.5 93 14 91.5 14 90Z" + cover + "M38 91H44V99L41 96.5 38 99Z" + "M46 91H52V97.5L49 95 46 97.5Z"),
           G::XS("M23 5V92M14 17H23M14 30H23M14 67H23M14 80H23", 1.6), G::XS("M28 12H77V85H28Z", 1.5), G::XS(corners, 1.5), G::X(sk.fill), G::F(sk.holes),
           G::XS(rrect(76, 41, 12, 14, 2.5), 2.4), G::F(rrect(76, 41, 12, 14, 2.5)), G::X(circ(82, 48, 2))});
  }
  // Фонарь душ: кольцо, стрельчатая крыша со шпилем, клетка со стрельчатым окном и призрачным пламенем, вокруг — души-всполохи.
  {
    const std::string flame = "M50 40C54 46 60 50 60 58C60 64.5 55.5 68 50 68C44.5 68 40 64.5 40 58C40 52 44 49 45.5 44.5C46.5 48 48 49 49 49.5"
                              "C48.5 46 49 43 50 40Z";
    const std::string body = "M33 27 50 13 67 27Z" + rrect(31, 26, 38, 5, 1.5) + rrect(34, 30, 32, 44, 2) + rrect(31, 73, 38, 5, 1.5) +
                             "M37 77.5H63L58 86H42Z" + thorn({50, 87}, {50, 96}, 5, 0);
    const std::string window = "M39 70V44C39 37 44 33.5 50 31.5C56 33.5 61 37 61 44V70Z";
    const std::string souls = tongue("M22 70C14 60 24 50 16 40C13 36 13 31 16 26", 9) + tongue("M78 74C86 64 77 55 84 46C87 42 87 37 84 32", 8);
    c.add("necro-lantern", "Фонарь душ",
          {G::S(circ(50, 8, 4.5), 3), G::F(body + souls), G::XS("M35 27H65", 1.5), G::X(window), G::F(flame), G::X(xf(flame, scaleAt(0.45f, 50, 62))),
           G::XS("M50 31.5V70M39 51H61", 1.5)});
  }
  // Костяной дракон: череп дракона с глазницей и окнами черепа, зубами, рогами; позвонки шеи с остистыми отростками.
  {
    // позвонки вдоль S-образной шеи: тело-валик, остистый отросток откинут назад (к хвосту), к хвосту мельче
    const Pt b0{48, 34}, b1{66, 38}, b2{80, 58}, b3{66, 94};
    std::string spine;
    for (int k = 0; k < 9; k++) {
      const double t = 0.04 + k * 0.118, sc = 1.25 - 0.06 * k;
      const CurvePt q = bezier(b0, b1, b2, b3, t);
      const float a = float(std::atan2(-q.nx, q.ny) * 180 / kPi);
      spine += xf(rrect(-4, -4, 8, 8, 2.6) + thorn({-0.5f, -3}, {5, -12.5f}, 3.6, -0.12) + "M-3 3.5 -1 8 1.5 3.5Z",
                  place(float(q.x), float(q.y), a, float(sc)));
    }
    Fig head = dragonHeadFig(false);
    head.holes = "M22 16C25 12.5 31 11.5 35.5 13C35 18 31 21 25.5 20.5C23 20 21.5 18.5 22 16Z"           // глазница
                 "M37.5 15C41 14 45 15.5 47 18.5C45.5 22 42.5 23.5 39.5 23C37.5 21 36.5 18 37.5 15Z"   // окно черепа
                 "M3.5 16.5C4.5 15 6.3 14.4 8 14.7C7 16 5.5 16.8 3.5 16.5Z"                           // ноздря
                 "M19 33.5C24 32 31 31 36.5 31.5C34 33.5 28 35 21 35.5Z";                             // окно челюсти
    head.lines = "M12 23.2 13.5 26.5M16 23 17.5 26.5M20 23.2 21.5 26.5M24 23.6 25.4 26.8M14 34 15 31M28 35.5 29 32.5";
    head.fill += "M12 23 13.4 27.2 15 23ZM20 23.2 21.4 27.2 23 23.4ZM25 33.4 26.5 29.6 28 33Z";
    std::vector<L> v{G::F(spine), G::XS("M44 35 50 45", 1.5)};
    put(v, head.moved(place(0, 2, 0, 0.98f)), 1.4f, 2.6f);
    c.add("necro-bonedragon", "Костяной дракон", moved(v, Affine::translate(7, 0)));
  }
  // Ворон на черепе: ворон в профиль — мощный клюв, сложенное крыло с острыми маховыми перьями, клиновидный хвост,
  // когти на черепе.
  {
    const std::string raven = "M8 27L20 20.5C23 16.5 28 15 33 15.5C38 16 42 19 45 23C52 27 60 32 68 40C75 47 84 54 95 62L89 64 93 68.5 84 66.5"
                              "C78 65.5 72 64 66 63C60 65 54 66 48 65C41 62.5 36 57 34 50C32.5 44 32 38 30 33.5L27 30.5 20.5 29.5Z";
    const std::string wing = "M40 33C48 33 58 38 66 46C72 52 78 57 86 61C78 62 70 60 63 57C56 54 48 50 43 45C40 41.5 39 37 40 33Z";
    const Fig sk = skullFig().moved(place(50, 63, 0, 0.44f) * Affine::translate(-50, 0));
    c.add("necro-raven", "Ворон на черепе",
          moved({G::F(raven + "M43.5 63 41.5 70.5 45 69.5 46 63ZM53.5 64 53.5 71 57 69.5 56.5 63Z"), G::XS(wing, 1.8), G::F(wing),
                 G::XS("M50 40C56 44 62 49 67 54M55 39C61 43 67 48 73 54M61 40C67 45 73 50 79 56", 1.4), G::XS("M22 22.5C25 24 27.5 26 29 28.5", 1.3),
                 G::X(circ(28.5, 20.5, 1.6)), G::XS(sk.fill, 3), G::F(sk.fill), G::X(sk.holes), G::XS("M41 89.5H63", 1.3)},
                Affine::translate(-3, -3.5f)));
  }
}

// ---------------------------------------------------------------- тёмные эльфы
// Паук сверху (левая половина ног задаётся, правая — зеркально): тонкие сегментированные лапы с суставами и острыми
// лапками, головогрудь, хелицеры, удлинённое брюшко с узором. Рамка 100 × 100, тело по оси x = 50.
struct SpiderFig {
  Fig legs, body;
};
SpiderFig spiderFig(double s = 1) {
  // лапа: от тазика через колено и голень к острию
  struct Leg {
    Pt c, k, j, t;
  };
  const Leg legs[] = {{{45.5, 33}, {34, 19}, {29, 10}, {26, 2}},
                      {{44.5, 36}, {26, 25}, {16, 26}, {5, 33}},
                      {{44.5, 40}, {25, 46}, {16, 54}, {8, 66}},
                      {{45.5, 43.5}, {32, 60}, {27, 73}, {23, 90}}};
  SpiderFig f;
  std::string l, joints;
  for (const Leg& g : legs) {
    l += brush("M" + ptS(g.c) + "L" + ptS(g.k), linear(3.8 * s, 3 * s), 10) + circ(g.k.x, g.k.y, 2.1 * s) +
         brush("M" + ptS(g.k) + "L" + ptS(g.j), linear(2.9 * s, 2.4 * s), 10) + thorn(g.j, g.t, 2.4 * s, 0, 0.8);
    const Pt d = g.t - g.k;
    const float dl = std::hypot(d.x, d.y), h = float(1.8 * s);
    joints += "M" + ptS(g.j + Pt{-d.y / dl * h, d.x / dl * h}) + "L" + ptS(g.j + Pt{d.y / dl * h, -d.x / dl * h});
  }
  f.legs = {sym(l), "", sym(joints)};
  f.body.fill = ell(50, 37, 7.5, 8.5) + "M50 45C60 45 64.5 54 64 63.5C63.5 73 58 82 50 92C42 82 36.5 73 36 63.5C35.5 54 40 45 50 45Z" +
                sym(thorn({47.5, 30}, {46, 23}, 2.6, 0.25)) + sym(brush("M46.5 31.5C44 29 42.5 26.5 42 23.5", linear(2.4, 1.6), 10));
  f.body.holes = sym(circ(47.6, 34, 1.2)) + "M50 55 55 63.5 50 74 45 63.5Z";
  f.body.lines = "M50 76V86M44.5 51.5 50 54.5 55.5 51.5" + sym("M41 64 44.5 68.5M42.5 74 46 77");
  return f;
}

void darkElves(Catalog& c) {
  c.group("Тёмные эльфы");
  // Паук (сверху): тонкие сегментированные лапы с суставами, хелицеры, глаза, удлинённое брюшко с узором-ромбом.
  {
    const SpiderFig sp = spiderFig();
    std::vector<L> v;
    put(v, sp.legs, 1.3f);
    put(v, sp.body, 1.4f, 2.2f);
    c.add("dark-elf-spider", "Паук", moved(v, Affine::translate(0, 3)));
  }
  // Паутина: восемь лучей с крепёжными нитями к углам, провисающие кольца, капли росы.
  {
    std::string radial, rings;
    for (int k = 0; k < 8; k++) {
      const double a = -90 + 45 * k;
      radial += "M50 50L" + pt(50, 50, 46, a);
      for (double r : {10.0, 18.5, 27.0, 35.5, 43.0}) rings += "M" + pt(50, 50, r, a) + "Q" + pt(50, 50, r * 0.8, a + 22.5) + " " + pt(50, 50, r, a + 45);
    }
    const Pt d1 = polar(50, 50, 31, -67.5), d2 = polar(50, 50, 16, 112), d3 = polar(50, 50, 34, 160);
    const std::string dew = circ(d1.x - 3, d1.y + 2, 2.4) + circ(d2.x, d2.y, 2) + circ(d3.x, d3.y + 2, 2);
    c.add("dark-elf-web", "Паутина",
          {G::S(radial, 3), G::S(rings, 2.2), G::F(circ(50, 50, 4.5) + dew),
           G::S(sym("M" + pt(50, 50, 45, -135) + "L3 3M" + pt(50, 50, 45, 135) + "L3 97"), 2.2)});
  }
  // Скрещённые кинжалы: изогнутые клинки с долом, гарда с загнутыми концами-лапами, рукоять с обмоткой, навершие с камнем.
  {
    const std::string guard = brush("M43 56C37 55 31 52 27 47C25 44.5 25.5 41.5 28 41", linear(4.6, 2.2));
    const std::string fill = "M50 2C58.5 14 61 31 57.5 55H43C45.5 39 45 21 50 2Z" + guard + mirrorX(guard, 50) + rrect(41, 54, 18, 5, 2) +
                             rrect(46, 58.5, 8, 22, 2.5) + "M50 79 56 87.5 50 97 44 87.5Z";
    c.add("dark-elf-daggers", "Скрещённые кинжалы",
          crossed(fill, circ(50, 87.5, 2.2), "M50.5 11C53 23 54 35 52.5 50M46 64H54M46 68.5H54M46 73H54M46 77.5H54", 1.4, 36, 50, 50, 1.14f));
  }
  // Полумесяц и паук на нити: острый полумесяц, паук с тонкими лапами спускается на паутинке, звёзды.
  {
    const SpiderFig sp = spiderFig(1.35);
    const Affine m = place(57, 45, 0, 0.42f) * Affine::translate(-50, -50);
    std::vector<L> v{G::F(circ(44, 50, 43)), G::X(circ(60, 44, 37)), G::S("M57 9V30", 1.6)};
    // мелкий паук — без гравировки лап и узора брюшка (на флаге она пропала бы)
    put(v, Fig{sp.legs.fill, "", ""}.moved(m), 1.3f);
    put(v, Fig{sp.body.fill, "M50 55 55 63.5 50 74 45 63.5Z", ""}.moved(m), 1.3f, 1.4f);
    v.push_back(G::F(star(80, 74, 6, 1.3, 4, -90) + star(86, 22, 4.5, 1, 4, -90)));
    c.add("dark-elf-crescent", "Полумесяц и паук", moved(v, Affine::translate(4, 0)));
  }
  // Паучиха-королева: паук с короной, гранёное брюшко с узором-самоцветом, длинные сегментированные лапы.
  {
    const SpiderFig sp = spiderFig();
    Fig body = sp.body;
    body.holes = sym(circ(47.6, 34, 1.2)) + "M50 52 57.5 63 50 79 42.5 63Z";
    body.lines = "M44.5 51.5 50 54.5 55.5 51.5" + sym("M40 62 44.5 68M41.5 74 46 78.5");
    const std::string crown = "M41.5 28 39 12 45 19.5 50 7 55 19.5 61 12 58.5 28Z";
    std::vector<L> v;
    put(v, sp.legs, 1.3f);
    put(v, body, 1.4f, 2.2f);
    v.push_back(G::F("M50 56.5 54 63 50 72.5 46 63Z"));
    v.push_back(G::XS(crown, 2.4));
    v.push_back(G::F(crown));
    v.push_back(G::X(circ(50, 21.5, 1.8)));
    v.push_back(G::XS("M42.5 25.5H57.5", 1.3));
    c.add("dark-elf-queen", "Паучиха-королева", moved(v, Affine::translate(0, 3)));
  }
  // Хлыст жрицы: рукоять с обмоткой и навершием, три плети-змеи с острыми головами и раздвоенными языками.
  {
    std::string lashes, heads, eyes, tongues;
    const Pt ends[][3] = {{{40, 40}, {52, 24}, {68, 13}}, {{46, 46}, {64, 41}, {82, 33}}, {{34, 36}, {30, 22}, {35, 8}}};
    for (const auto& e : ends) {
      const Pt b{32, 56};
      lashes += brush("M" + ptS(b) + "C" + ptS(e[0]) + " " + ptS(e[1]) + " " + ptS(e[2]), linear(9, 5), 40);
      const Pt d = e[2] - e[1];
      const float len = std::hypot(d.x, d.y), ang = float(std::atan2(d.y, d.x) * 180 / kPi);
      const Affine hx = place(e[2].x + d.x / len * 3.5f, e[2].y + d.y / len * 3.5f, ang);
      heads += xf("M-3 -4C1 -5.2 6.5 -3.6 11 0C6.5 3.6 1 5.2 -3 4Z", hx);
      eyes += xf("M2 -2.4 5.4 -1.2 2.6 -0.6Z", hx);
      tongues += xf("M10 0 14.5 0 17 -2M14.5 0 17 2", hx);
    }
    const std::string handle = xf(rrect(-5.5, -2, 11, 36, 3), place(32, 56, 35)) + circ(32, 56, 7.5) + xf("M-7.5 33H7.5L0 45Z", place(32, 56, 35));
    c.add("dark-elf-whip", "Хлыст жрицы",
          moved({G::F(lashes + heads + handle), G::S(tongues, 1.4), G::X(eyes),
                 G::XS(xf("M-5.5 12H5.5M-5.5 17.5H5.5M-5.5 23H5.5M-5.5 28.5H5.5", place(32, 56, 35)), 1.5),
                 G::X(circ(32, 56, 3))},
                scaleAt(0.9f, 50, 50) * Affine::translate(-2, 9)));
  }
  // Кинжал с ядом: волнистый клинок остриём вниз с долом, гарда-полумесяц с острыми концами, рукоять, капли яда.
  {
    std::vector<Pt> lft, rgt;
    for (int i = 0; i <= 30; i++) {
      const double t = i / 30.0, y = 34 + 54 * t, w = 15.5 * std::pow(1 - t, 0.85) + 0.3, cx = 50 + 3.6 * std::sin(t * 3 * kPi);
      lft.push_back({float(cx - w), float(y)});
      rgt.push_back({float(cx + w), float(y)});
    }
    std::string blade = "M" + ptS(lft[0]);
    for (size_t i = 1; i < lft.size(); i++) blade += "L" + ptS(lft[i]);
    for (size_t i = rgt.size(); i-- > 0;) blade += "L" + ptS(rgt[i]);
    blade += "Z";
    std::string fuller;
    for (int i = 0; i <= 12; i++) {
      const double t = 0.05 + 0.55 * i / 12.0;
      fuller += (i ? "L" : "M") + ptS({float(50 + 3.6 * std::sin(t * 3 * kPi)), float(34 + 54 * t)});
    }
    const std::string guard = brush("M12 17C23 32 37 36 50 36C63 36 77 32 88 17", [](double t) { return 1 + 9 * std::pow(std::sin(kPi * t), 0.7); });
    const std::string hilt = guard + rrect(43, 8, 14, 27, 3) + "M50 0 58 6 50 12 42 6Z";
    c.add("dark-elf-poison", "Кинжал с ядом",
          {G::F(blade + drop(51, 88, 94.5, 4) + drop(74, 52, 60, 4.8) + drop(80, 66, 71.5, 3.2)), G::XS(fuller, 1.8), G::XS(hilt, 2.6), G::F(hilt),
           G::XS("M43 15H57M43 20.5H57M43 26H57", 1.5), G::X(circ(50, 6, 2))});
  }
  // Жрица: строгое узкое лицо с острыми длинными ушами, раскосые глаза, длинные прямые волосы прядями, диадема с пауком.
  {
    const std::string hair = "M50 7C66 7 76 18 76 36C76 54 80 72 88 92L72 97C67 87 64 76 63 66L50 69V7Z";
    const std::string face = "M50 23C59 23 64 31 64 42C64 53 58.5 62 50 66.5C41.5 62 36 53 36 42C36 31 41 23 50 23Z";
    const std::string ears = sym(leaf({37.5, 45}, {8, 27}, 8, 0.03, 0.3));
    const std::string tiara = "M38 26C42 22 46 20.5 50 20.5C54 20.5 58 22 62 26L60 27.5C57 24.5 53.5 23.2 50 23.2C46.5 23.2 43 24.5 40 27.5Z" +
                              std::string("M50 11 53 18 50 22 47 18Z");
    std::vector<L> v{G::F(sym(hair) + "M17 99C20 86 33 77 50 77C67 77 80 86 83 99Z" + rrect(44, 60, 12, 18, 4)),
                     G::XS(sym("M64 30C68 44 69 62 74 84M58 26C63 40 64 56 66 70") + "M17 99C20 86 33 77 50 77C67 77 80 86 83 99", 1.5),
                     G::XS(face + ears, 2.4), G::F(face + ears), G::XS(sym("M33 42 16 31.5"), 1.4), G::XS(tiara, 2), G::F(tiara),
                     G::X(sym("M40 41.6 47.5 43.2 41.5 45.8Z")), G::XS(sym("M38.5 37.8 46.5 39.2") + "M50 45.5V52.5M46.5 57.6H53.5", 1.5)};
    c.add("dark-elf-priestess", "Жрица", v);
  }
  // Город в сталактитах: свод пещеры со сталактитами, стрельчатые шпили подземного города с окнами, арочные мосты.
  {
    struct Spire {
      double x, top, w;
    };
    const Spire sp[] = {{50, 26, 11}, {35, 44, 8.5}, {65, 44, 8.5}, {22, 58, 7.5}, {78, 58, 7.5}, {10, 70, 6.5}, {90, 70, 6.5}};
    std::string spires, windows;
    for (const Spire& s : sp) {
      const double roof = s.w * 1.9;
      const double l = s.x - s.w / 2, r = s.x + s.w / 2;
      spires += "M" + n(l) + " 92V" + n(s.top + roof) + "C" + n(l) + " " + n(s.top + roof * 0.5) + " " + n(s.x - 1) + " " + n(s.top + 3) + " " + n(s.x) + " " +
                n(s.top - 3) + "C" + n(s.x + 1) + " " + n(s.top + 3) + " " + n(r) + " " + n(s.top + roof * 0.5) + " " + n(r) + " " + n(s.top + roof) + "V92Z";
      for (double y = s.top + roof + 5; y < 84; y += 12)
        windows += "M" + n(s.x - 1.3) + " " + n(y + 6) + "V" + n(y + 1.3) + "L" + n(s.x) + " " + n(y - 0.5) + "L" + n(s.x + 1.3) + " " + n(y + 1.3) + "V" +
                   n(y + 6) + "Z";
    }
    std::string stal = "M3 3H97V8.5H3Z";
    const double sx[] = {42.5, 57.5, 28.5, 71.5, 16, 84, 5.5, 94.5}, sl[] = {22, 22, 20, 20, 28, 28, 16, 16};
    for (int k = 0; k < 8; k++)
      stal += "M" + n(sx[k] - 4) + " 8C" + n(sx[k] - 3) + " " + n(8 + sl[k] * 0.4) + " " + n(sx[k] - 0.6) + " " + n(8 + sl[k] * 0.8) + " " + n(sx[k]) + " " +
              n(8 + sl[k]) + "C" + n(sx[k] + 0.6) + " " + n(8 + sl[k] * 0.8) + " " + n(sx[k] + 3) + " " + n(8 + sl[k] * 0.4) + " " + n(sx[k] + 4) + " 8Z";
    c.add("dark-elf-city", "Город в сталактитах",
          {G::F(stal + spires + rrect(3, 90, 94, 7, 1.5)), G::X(windows), G::XS("M39.5 68Q42.5 63 45.5 68M54.5 68Q57.5 63 60.5 68", 2),
           G::S("M39.5 69Q42.5 64 45.5 69M54.5 69Q57.5 64 60.5 69M26 76Q28.5 72 31 76M69 76Q71.5 72 74 76", 1.8), G::XS("M3 93H97", 1.5)});
  }
}

// ---------------------------------------------------------------- наги
// Море: полоса с гребнями-завитками от x0 до x1 (гребень шагом step на высоте y), низ — до bottom.
std::string seaBand(double x0, double x1, double y, double step, double amp, double bottom) {
  std::string s = "M" + n(x0) + " " + n(bottom) + "V" + n(y);
  for (double x = x0; x < x1 - 0.01; x += step) {
    const double e = std::min(x1, x + step);
    s += "C" + n(x + step * 0.2) + " " + n(y - amp * 0.9) + " " + n(x + step * 0.55) + " " + n(y - amp * 1.1) + " " + n(x + step * 0.7) + " " +
         n(y - amp * 0.35) + "C" + n(x + step * 0.62) + " " + n(y - amp * 0.75) + " " + n(x + step * 0.42) + " " + n(y - amp * 0.55) + " " + n(e) + " " + n(y);
  }
  return s + "V" + n(bottom) + "Z";
}

// Зубец трезубца: древко от b к t шириной w, наконечник-лист с двумя загнутыми назад шипами.
std::string tine(Pt b, Pt t, double w, double head) {
  const Pt d = t - b;
  const float l = std::hypot(d.x, d.y);
  const Pt u = d * (1 / l), nr{-u.y, u.x};
  const Pt base = t - u * float(head);
  return brush("M" + ptS(b) + "L" + ptS(base + u * 1.5f), linear(w, w * 0.9), 8) + leaf(base, t, w * 1.25, 0, 0.35) +
         thorn(base + u * float(head * 0.3), base - u * float(head * 0.35) + nr * float(w * 1.25), w * 0.55, 0.1) +
         thorn(base + u * float(head * 0.3), base - u * float(head * 0.35) - nr * float(w * 1.25), w * 0.55, -0.1);
}

// Трезубец: средний зубец, боковые зубцы, перекладина-дуга с завитками и самоцветом, древко с обмоткой, шип на конце.
Fig tridentFig() {
  Fig f;
  f.fill = tine({50, 50}, {50, 2}, 7.5, 16) + tine({25, 45}, {23, 8}, 6.5, 13) + tine({75, 45}, {77, 8}, 6.5, 13) +
           brush("M21 43C28 57 72 57 79 43", spindle(9, 0.3)) + sym(spiral(18, 47, 0, -250, 6, 2.6, 4.4, 1.8)) + "M46.2 52H53.8L53 91H47Z" +
           thorn({50, 90}, {50, 99}, 6, 0) + "M50 45 55 52 50 59 45 52Z";
  f.holes = "M50 49.5 52.4 52.5 50 55.5 47.6 52.5Z";
  f.lines = "M46.5 66H53.5M46.5 70.5H53.5M46.5 75H53.5M46.5 79.5H53.5" + std::string("M50 10V40M24.5 16V40M75.5 16V40");
  return f;
}

// Змеиная голова (сверху/сбоку): острая голова от шеи n к носу t шириной w, глаз — вырез.
std::string snakeHead(Pt neck, Pt tip, double w) { return leaf(neck, tip, w, 0, 0.42); }

void nagas(Catalog& c) {
  c.group("Наги");
  // Кобра: поднявшаяся кобра с раскрытым капюшоном (очковый узор, чешуя по краю), строгая голова, раздвоенный язык,
  // брюшные щитки, кольцо хвоста с острым кончиком.
  {
    const std::string hood = "M50 9C58.5 9 63.5 14 64.5 21C73.5 25 80 35 80 46C80 58 70 65.5 59 71L56.5 79H43.5L41 71C30 65.5 20 58 20 46C20 35 26.5 25 35.5 21"
                             "C36.5 14 41.5 9 50 9Z";
    const std::string head = "M50 11C55.5 11 59 15 59 20C59 25 55.5 29 50 31.5C44.5 29 41 25 41 20C41 15 44.5 11 50 11Z";
    const std::string coil = brush("M50 77C50 85 40 85.5 28 86C18 86.5 15 91.5 24 94C38 96.5 62 96.5 76 94C85 91.5 83 86.5 72 86C64 85.8 58 87 56 90.5",
                                   [](double t) { return 10 - 7 * t; });
    c.add("naga-cobra", "Кобра",
          {G::F(coil), G::XS("M50 80C49.5 83.5 46 85 40 85.5", 1.4), G::XS(hood, 2.6), G::F(hood),
           G::XS(sym("M35.5 27C30 33 27 40 27.5 48M40.5 28C36.5 36 35.5 44 37 52") + "M44.5 37C47 34 53 34 55.5 37C53 40 47 40 44.5 37Z", 1.5),
           G::XS("M44 44H56M43.5 49.5H56.5M43.5 55H56.5M44 60.5H56M45 66H55M46 71.5H54", 1.4), G::XS(head, 2), G::F(head),
           G::X(sym("M43 18.6C44.5 17.6 46.6 17.8 47.8 19.2C46.4 20.4 44.4 20.4 43 18.6Z")), G::S("M50 31.5V35.5M50 35.5 47.5 38M50 35.5 52.5 38", 1.3)});
  }
  // Трезубец: средний и боковые зубцы с зазубренными наконечниками, перекладина-дуга с завитками и самоцветом,
  // древко с обмоткой и шипом.
  {
    std::vector<L> v;
    put(v, tridentFig(), 1.4f);
    c.add("naga-trident", "Трезубец", v);
  }
  // Раковина-гребешок: рёбра веером, фестончатый край, линия роста, ушки у замка.
  {
    const double cx = 50, cy = 47, rv = 40, rb = 46;
    std::string edge = "M50 86L" + pt(cx, cy, rv, 160), ribs;
    for (int j = 0; j < 10; j++) {
      const double v = 160 + 22 * j;
      edge += "Q" + pt(cx, cy, rb, v + 11) + " " + pt(cx, cy, rv, v + 22);
      if (j > 0) {
        const Pt e = polar(cx, cy, rv, v);
        ribs += "M" + ptS({50 + (e.x - 50) * 0.14f, 84 + (e.y - 84) * 0.14f}) + "L" + ptS({50 + (e.x - 50) * 0.88f, 84 + (e.y - 84) * 0.88f});
      }
    }
    c.add("naga-shell", "Раковина",
          {G::F(edge + "Z" + sym("M36 80 46 84.5V93H32Z")), G::XS(ribs, 2.2), G::XS(arc(50, 84, 23, 197, 343) + arc(50, 84, 12, 205, 335), 1.5),
           G::XS(sym("M38 84.5 37 91"), 1.4)});
  }
  // Два змея вокруг жезла с жемчужиной: тела сужаются к хвостам, острые головы обращены друг к другу, языки, блик жемчуга.
  {
    const std::string body = brush("M33 25C27 32 30 40 50 46C72 53 70 63 50 70C34 76 32 84 48 92", [](double t) { return 8 - 5.5 * t; });
    const std::string head = snakeHead({33.5, 27}, {43, 14}, 8.5);
    c.add("naga-serpents", "Два змея",
          {G::F(rrect(47, 16, 6, 81, 2.5) + circ(50, 9, 7)), G::XS(arc(50, 9, 4.4, 200, 260), 1.5), G::XS(sym(body + head), 2.6), G::F(sym(body + head)),
           G::X(sym("M37.5 19.5 40.5 18.6 39.5 21.2Z")), G::S(sym("M43 14 45.5 11.5M45.5 11.5 47 10.6M45.5 11.5 45.8 9.5"), 1.3),
           G::XS(sym("M44 47.5 47 49.5M41 52 44 54.5M44.5 68.5 47.5 67M41 72.5 44 71"), 1.3)});
  }
  // Кракен: заострённая мантия с плавниками и гребнем, строгие глаза под надбровьями, щупальца S-образно с завитками
  // и присосками.
  {
    const std::string mantle = "M50 2C60 9 69 20 70 34C71 44 67 51 61 56H39C33 51 29 44 30 34C31 20 40 9 50 2Z" + sym(leaf({33, 24}, {19, 8}, 9, -0.05));
    struct Tent {
      const char* d;
      double cx, cy, a0, a1, r;
    };
    const Tent ts[] = {{"M37 52C25 56 13 54 10 44", 13.5, 39, 160, -80, 5.5},
                       {"M40 55C32 66 18 70 15 80", 20, 82, 190, 430, 5.5},
                       {"M45.5 57C45 70 38 84 30 91", 32, 86.5, 140, -110, 5}};
    std::string tent, suckers;
    for (const Tent& t : ts) {
      tent += brush(t.d, linear(9, 4.5), 40) + spiral(t.cx, t.cy, t.a0, t.a1, t.r, t.r * 0.42, 4.5, 1.8);
      const std::vector<Pt> pts = resample(t.d, 10);
      for (int k : {3, 5, 7}) suckers += circ(pts[size_t(k)].x, pts[size_t(k)].y, 1.4);
    }
    c.add("naga-kraken", "Кракен",
          {G::F(sym(tent) + mantle), G::X(sym(suckers)), G::XS(mantle, 2.2), G::F(mantle),
           G::X(sym(circ(40.5, 43, 4.2))), G::F(sym(ell(40.5, 43, 3, 1))), G::XS("M50 9V33" + sym("M45 15 50 19.5M42 22 50 28.5"), 1.5)});
  }
  // Жемчужина: раскрытая раковина — верхняя створка веером с фестонами, нижняя — чаша; крупная жемчужина с бликом.
  {
    std::string upper = "M50 52L" + pt(50, 52, 42, 180), ribsU, lower = "M8 64", ribsL;
    for (int k = 0; k < 8; k++) {
      const double a = 180 + 22.5 * k;
      upper += "Q" + pt(50, 52, 48, a + 11.25) + " " + pt(50, 52, 42, a + 22.5);
      if (k) ribsU += "M" + pt(50, 52, 18, a) + "L" + pt(50, 52, 39, a);
    }
    upper += "Z";
    for (int k = 0; k < 6; k++) lower += "Q" + n(8 + 14 * k + 7) + " 60 " + n(8 + 14 * (k + 1)) + " 64";
    lower += "C90 82 72 96 50 96C28 96 10 82 8 64Z";
    for (int k = 1; k < 6; k++) ribsL += "M" + pt(50, 100, 13, 180 + 30 * k) + "L" + pt(50, 100, 30, 180 + 30 * k);
    c.add("naga-pearl", "Жемчужина",
          {G::F(upper), G::XS(ribsU + arc(50, 52, 30, 185, 355), 1.6), G::XS(lower, 3), G::F(lower), G::XS(ribsL, 1.6), G::XS(circ(50, 58, 12.5), 3.2),
           G::F(circ(50, 58, 12.5)), G::X(brush("M43.5 59C42.5 54.5 44.5 50.5 48.5 49", linear(2.6, 1.4), 16))});
  }
  // Наг-воин: голова под капюшоном кобры, торс, руки с выгнутыми ятаганами, змеиный хвост кольцом с острым концом.
  {
    const std::string hood = "M50 2C63 2 72 10 73 21C74 30 68 37 60 40H40C32 37 26 30 27 21C28 10 37 2 50 2Z";
    const std::string torso = "M37 33C42 30.5 58 30.5 63 33L62.5 41 58 58H42L37.5 41Z";
    const std::string arm = brush("M38 35C30 40 26 46 22 49", linear(7.5, 5.5)) + circ(21.5, 48.5, 4);
    // ятаган: клинок расширяется к елманю и сходится к острию, выгнут наружу
    auto yelman = [](double t) { return t < 0.72 ? 4.2 + 5.5 * t / 0.72 : 9.7 * std::pow((1 - t) / 0.28, 0.9) + 0.2; };
    const std::string blade = brush("M20 44C16 34 9 26 3 12", yelman) + xf(rrect(-7.5, -2, 15, 4, 1.6), place(21, 45.5, -28));
    const std::string tail = brush("M50 56C50 62 52 64 56 66", linear(15, 14)) + spiral(50, 79, -60, 250, 14, 7.5, 14, 4) +
                             brush("M50 72C40 80 30 92 11 94", linear(6, 0.6));
    const std::string head = "M50 13C55 13 57.5 17 57.5 21C57.5 26 54 30 50 31C46 30 42.5 26 42.5 21C42.5 17 45 13 50 13Z";
    c.add("naga-warrior", "Наг-воин",
          {G::F(hood), G::XS(sym("M33 13C31 20 32 27 36 33M39 8C37 12 36.5 16 37 20"), 1.5), G::XS(sym(arm + blade), 2.4), G::F(sym(arm + blade)),
           G::XS(sym(offsetCurve({20, 44}, {16, 34}, {9, 26}, {3, 12}, -2.2, 0.08, 0.6)), 1.3), G::XS(tail, 2.6), G::F(tail), G::XS(torso, 2.4), G::F(torso),
           G::XS(head, 2.2), G::F(head), G::X(sym("M44.4 21C45.6 20 47.3 20 48.4 21.2C47.2 22.2 45.5 22.2 44.4 21Z")),
           G::XS("M44 55.5H56M43 40Q50 43 57 40M50 44V52", 1.4)});
  }
  // Морской змей: драконья голова с гребнем, горбы тела над волнами с плавниками-шипами, хвост-плавник, волны с гребнями.
  {
    const Fig head = dragonHeadFig(true).moved(place(9, 10, 0, 0.5f));
    Fig body = serpent("M31 21C40 26 42 44 39 62C37 72 36 78 37 82", linear(12, 11), -1, 0.15, 0.75, 4, 6);
    const Fig hump1 = serpent("M45 80C46 58 66 56 66 80", linear(10.5, 9.5), 1, 0.25, 0.75, 3, 6);
    const Fig hump2 = serpent("M72 80C73 64 87 64 88 80", linear(8.5, 7), 1, 0.3, 0.7, 2, 5);
    std::vector<L> v;
    for (const Fig& f : {body, hump1, hump2}) put(v, f, 1.4f);
    v.push_back(G::F(leaf({88, 78}, {98, 62}, 8, -0.1)));
    put(v, head, 1.4f, 2.2f);
    const std::string sea = seaBand(2, 98, 82, 16, 7, 98);
    v.push_back(G::XS(sea, 3));
    v.push_back(G::F(sea));
    v.push_back(G::XS("M8 91Q14 88 20 91M32 93Q38 90 44 93M58 91Q64 88 70 91M80 94Q86 91 92 94", 1.5));
    c.add("naga-serpent", "Морской змей", v);
  }
  // Волна и трезубец: трезубец поднимается из двух закрученных волн с пеной, внизу — море.
  {
    const Fig tr = tridentFig().moved(place(50, 2, 0, 0.8f) * Affine::translate(-50, 0));
    const std::string waves = sym(spiral(27, 69, 0, -300, 21, 6, 13, 3.4) + thorn({9, 60}, {4, 50}, 4, -0.2) + thorn({14, 52}, {12, 42}, 3.6, -0.2)) +
                              seaBand(3, 97, 87, 23.5, 5, 97);
    std::vector<L> v;
    put(v, tr, 1.3f);
    v.push_back(G::XS(waves, 3));
    v.push_back(G::F(waves));
    v.push_back(G::X(sym(spiral(27, 69, 10, -230, 15, 8.5, 1.7, 1.4))));
    v.push_back(G::XS("M10 94Q20 91 30 94M70 94Q80 91 90 94", 1.4));
    c.add("naga-wave", "Волна и трезубец", v);
  }
}

}  // namespace

void fantasyEmblemsA(Catalog& c) {
  dragons(c);
  vampires(c);
  nature(c);
  elements(c);
  dwarves(c);
  elves(c);
  necromancers(c);
  darkElves(c);
  nagas(c);
}

}  // namespace rg::gfx::emb
