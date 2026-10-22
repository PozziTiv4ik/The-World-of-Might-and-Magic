// Regnum — эмблемы рас справочника: «Огры» … «Энты» (раздел «Расы», группа на расу, по алфавиту; имена race-<раса>-…).
// Сетка 100 × 100, одноцветный силуэт, детали — вырезы; симметричные силуэты описаны левой половиной.
#include "gfx/emblems_internal.h"

namespace rg::gfx::emb {

namespace {

// ---------------------------------------------------------------- гравировка: штрихи, внутренний контур, отсечение
Pt lerp(Pt a, Pt b, double t) { return {float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t)}; }
// Точка на отрезке ab в доле t, сдвинутая поперёк на off (положительное — влево от направления a → b при оси Y вниз).
Pt across(Pt a, Pt b, double t, double off) {
  const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1e-6, std::hypot(dx, dy));
  return {float(a.x + dx * t + dy / len * off), float(a.y + dy * t - dx / len * off)};
}

// Ширина каллиграфического штриха в доле t: от w0 плавно к пику wm в tm, затем к w1.
double brushW(double t, double w0, double wm, double w1, double tm) {
  if (t < tm) return w0 + (wm - w0) * std::sin(kPi / 2 * t / tm);
  return w1 + (wm - w1) * std::cos(kPi / 2 * (t - tm) / (1 - tm));
}

// Каллиграфический штрих вдоль кубической кривой: ширина w0 → wm (в доле tm) → w1; нулевая ширина — острый конец.
std::string brush(Pt p0, Pt c1, Pt c2, Pt p1, double w0, double wm, double w1, double tm = 0.4, int steps = 32) {
  std::vector<Pt> lft, rgt;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, w = brushW(t, w0, wm, w1, tm) * 0.5;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    lft.push_back({float(q.x + q.nx * w), float(q.y + q.ny * w)});
    rgt.push_back({float(q.x - q.nx * w), float(q.y - q.ny * w)});
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Лист-ланцет от a к b с острыми концами: наибольшая ширина wm, изгиб bend (смещение середины поперёк).
std::string lancet(Pt a, Pt b, double wm, double bend = 0, double tm = 0.45) {
  return brush(a, across(a, b, 1 / 3.0, bend * 1.33), across(a, b, 2 / 3.0, bend * 1.33), b, 0, wm, 0, tm);
}

// Язык пламени, локон гривы, коготь: от основания a (ширина w) к острому кончику b; s — S-изгиб, bend — общий изгиб.
std::string flame(Pt a, Pt b, double w, double s, double bend = 0) {
  return brush(a, across(a, b, 1 / 3.0, s + bend * 1.33), across(a, b, 2 / 3.0, -s + bend * 1.33), b, w, w * 1.08, 0, 0.18);
}

// Полумесяц: внешний круг (cx, cy, r), рога смотрят в сторону dir (градусы) и разведены на ±spread; th — толщина спинки.
std::string crescent(double cx, double cy, double r, double dir, double spread, double th) {
  std::vector<Pt> pts;
  for (int i = 0; i <= 40; i++) pts.push_back(polar(cx, cy, r, dir + spread + (360 - 2 * spread) * i / 40.0));
  // Внутренняя дуга — окружность через концы рогов, смещённая к рогам на th.
  const Pt a = pts.back(), b = pts.front(), back = polar(cx, cy, r - th, dir + 180);
  const double mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2, ux = std::cos(dir * kPi / 180), uy = std::sin(dir * kPi / 180);
  const double h = std::hypot(a.x - mx, a.y - my), dd = (mx - back.x) * ux + (my - back.y) * uy;
  const double off = (dd * dd - h * h) / (2 * dd);  // центр внутренней окружности: от середины хорды назад на off
  const double icx = mx - ux * off, icy = my - uy * off, ir = std::hypot(a.x - icx, a.y - icy);
  const double a0 = std::atan2(a.y - icy, a.x - icx) * 180 / kPi;
  auto norm = [](double v) { return v - 360 * std::floor(v / 360); };
  const double da = norm(std::atan2(b.y - icy, b.x - icx) * 180 / kPi - a0), db = norm(std::atan2(back.y - icy, back.x - icx) * 180 / kPi - a0);
  const double a1 = db < da ? a0 + da : a0 + da - 360;  // дуга через спинку
  std::string s = "M" + ptS(pts[0]);
  for (size_t i = 1; i < pts.size(); i++) s += "L" + ptS(pts[i]);
  for (int i = 1; i < 40; i++) s += "L" + pt(icx, icy, ir, a0 + (a1 - a0) * i / 40.0);
  return s + "Z";
}

// Гутта (геральдическая капля): круглое тело радиуса r в c, острый хвост длины len в сторону dir.
std::string goutte(Pt c, double r, double dir, double len) {
  const double phi = std::acos(std::min(0.99, r / len)) * 180 / kPi;
  std::string s = "M" + pt(c.x, c.y, len, dir);
  for (int i = 0; i <= 20; i++) s += "L" + pt(c.x, c.y, r, dir + phi + (360 - 2 * phi) * i / 20.0);
  return s + "Z";
}

// Контур заливки ломаными: рёбра с номером контура и признаком «внешнее» (лежит на границе объединения контуров),
// сетки для быстрых запросов «внутри?» и «далеко ли до края?».
class Outline {
 public:
  explicit Outline(std::string_view d) {
    svgPath(d).flatten(0.12f, [&](const std::vector<Pt>& pts, bool) {
      std::vector<Pt> p;
      for (size_t i = 0; i < pts.size(); i++) {
        const Pt a = pts[i], b = pts[(i + 1) % pts.size()];
        const double len = std::hypot(b.x - a.x, b.y - a.y);
        if (len < 1e-4) continue;
        const int k = std::max(1, int(std::ceil(len / 0.8)));
        for (int j = 0; j < k; j++) p.push_back(lerp(a, b, double(j) / k));
      }
      if (p.size() >= 3) polys_.push_back(std::move(p));
    });
    for (size_t k = 0; k < polys_.size(); k++)
      for (size_t i = 0; i < polys_[k].size(); i++) edges_.push_back({polys_[k][i], polys_[k][(i + 1) % polys_[k].size()], int(k), true});
    rows_.resize(kRows);
    for (size_t e = 0; e < edges_.size(); e++) {
      const int r0 = rowOf(std::min(edges_[e].a.y, edges_[e].b.y)), r1 = rowOf(std::max(edges_[e].a.y, edges_[e].b.y));
      for (int r = r0; r <= r1; r++) rows_[r].push_back(int(e));
    }
    cells_.resize(kCells * kCells);
    for (size_t e = 0; e < edges_.size(); e++) {
      Edge& g = edges_[e];
      g.ext = wind(lerp(g.a, g.b, 0.5), g.poly) == 0;
      if (!g.ext) continue;
      const int x0 = cellOf(std::min(g.a.x, g.b.x)), x1 = cellOf(std::max(g.a.x, g.b.x));
      const int y0 = cellOf(std::min(g.a.y, g.b.y)), y1 = cellOf(std::max(g.a.y, g.b.y));
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) cells_[y * kCells + x].push_back(int(e));
    }
  }
  const std::vector<std::vector<Pt>>& polys() const { return polys_; }
  bool edgeExt(size_t poly, size_t i) const {
    size_t base = 0;
    for (size_t k = 0; k < poly; k++) base += polys_[k].size();
    return edges_[base + i].ext;
  }
  bool inside(Pt q) const { return wind(q, -1) != 0; }
  // Внутри и не ближе r к границе объединения.
  bool clear(Pt q, double r) const {
    if (!inside(q)) return false;
    const int x0 = cellOf(float(q.x - r)), x1 = cellOf(float(q.x + r)), y0 = cellOf(float(q.y - r)), y1 = cellOf(float(q.y + r));
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++)
        for (int e : cells_[y * kCells + x])
          if (segDist(q, edges_[e].a, edges_[e].b) < r) return false;
    return true;
  }

 private:
  struct Edge {
    Pt a, b;
    int poly;
    bool ext;
  };
  static constexpr int kRows = 160, kCells = 40;
  static int rowOf(float y) { return std::clamp(int(std::floor((y + 30) / 1.0f)), 0, kRows - 1); }
  static int cellOf(float v) { return std::clamp(int(std::floor((v + 30) / 4.0f)), 0, kCells - 1); }
  static double segDist(Pt q, Pt a, Pt b) {
    const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    const double t = l2 > 0 ? std::clamp(((q.x - a.x) * dx + (q.y - a.y) * dy) / l2, 0.0, 1.0) : 0;
    return std::hypot(q.x - a.x - dx * t, q.y - a.y - dy * t);
  }
  // Число обхода точки q (без контура skip).
  int wind(Pt q, int skip) const {
    int w = 0;
    for (int e : rows_[rowOf(q.y)]) {
      const Edge& g = edges_[e];
      if (g.poly == skip) continue;
      const double cr = (g.b.x - g.a.x) * (q.y - g.a.y) - (q.x - g.a.x) * (g.b.y - g.a.y);
      if (g.a.y <= q.y) {
        if (g.b.y > q.y && cr > 0) w++;
      } else if (g.b.y <= q.y && cr < 0) {
        w--;
      }
    }
    return w;
  }
  std::vector<std::vector<Pt>> polys_;
  std::vector<Edge> edges_;
  std::vector<std::vector<int>> rows_, cells_;
};

// Ломаные из последовательных годных точек (цикл — замкнутый, если годны все); короче minRun — отбрасываются.
std::string runs(const std::vector<Pt>& p, const std::vector<char>& ok, bool cyclic, double minRun) {
  const size_t n = p.size();
  std::string out;
  if (!n) return out;
  size_t start = 0;
  if (cyclic) {
    while (start < n && ok[start]) start++;
    if (start == n) {
      out += "M" + ptS(p[0]);
      for (size_t i = 1; i < n; i++) out += "L" + ptS(p[i]);
      return out + "Z";
    }
  }
  std::vector<Pt> cur;
  auto flush = [&] {
    double len = 0;
    for (size_t i = 1; i < cur.size(); i++) len += std::hypot(cur[i].x - cur[i - 1].x, cur[i].y - cur[i - 1].y);
    if (cur.size() >= 2 && len >= minRun) {
      out += "M" + ptS(cur[0]);
      for (size_t i = 1; i < cur.size(); i++) out += "L" + ptS(cur[i]);
    }
    cur.clear();
  };
  for (size_t k = 0; k < n; k++) {
    const size_t i = cyclic ? (start + k) % n : k;
    if (ok[i])
      cur.push_back(p[i]);
    else
      flush();
  }
  flush();
  return out;
}

// Внутренний контур (гравированная линия вдоль края) на расстоянии dist внутрь от границы заливки d.
std::string inner(std::string_view d, double dist, double minRun = 3) {
  const Outline o(d);
  std::string out;
  for (size_t k = 0; k < o.polys().size(); k++) {
    const std::vector<Pt>& p = o.polys()[k];
    const size_t n = p.size();
    std::vector<Pt> q(n);
    std::vector<char> ok(n, 0);
    for (size_t i = 0; i < n; i++) {
      if (!o.edgeExt(k, i) || !o.edgeExt(k, (i + n - 1) % n)) continue;
      const Pt a = p[(i + n - 1) % n], b = p[i], c = p[(i + 1) % n];
      double n1x = a.y - b.y, n1y = b.x - a.x, n2x = b.y - c.y, n2y = c.x - b.x;
      const double l1 = std::max(1e-6, std::hypot(n1x, n1y)), l2 = std::max(1e-6, std::hypot(n2x, n2y));
      n1x /= l1, n1y /= l1, n2x /= l2, n2y /= l2;
      double nx = n1x + n2x, ny = n1y + n2y;
      const double ln = std::hypot(nx, ny);
      if (ln < 1e-3) continue;
      nx /= ln, ny /= ln;
      const double m = dist / std::max(0.4, nx * n1x + ny * n1y);
      const Pt in{float(b.x + nx * m), float(b.y + ny * m)}, out2{float(b.x - nx * m), float(b.y - ny * m)};
      const Pt cand = o.inside(in) ? in : out2;
      if (o.clear(cand, dist * 0.8)) {
        q[i] = cand;
        ok[i] = 1;
      }
    }
    out += runs(q, ok, true, minRun);
  }
  return out;
}

// Линии lines, обрезанные по заливке shape с отступом margin от края (гравировка не рвёт силуэт).
std::string clip(std::string_view shape, std::string_view lines, double margin, double minRun = 1.5) {
  const Outline o(shape);
  std::string out;
  svgPath(lines).flatten(0.1f, [&](const std::vector<Pt>& pts, bool closed) {
    std::vector<Pt> p;
    const size_t m = closed ? pts.size() : pts.size() - 1;
    for (size_t i = 0; i < m; i++) {
      const Pt a = pts[i], b = pts[(i + 1) % pts.size()];
      const int k = std::max(1, int(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / 0.5)));
      for (int j = 0; j < k; j++) p.push_back(lerp(a, b, double(j) / k));
    }
    if (!closed) p.push_back(pts.back());
    std::vector<char> ok(p.size());
    for (size_t i = 0; i < p.size(); i++) ok[i] = o.clear(p[i], margin);
    out += runs(p, ok, false, minRun);
  });
  return out;
}

// Треугольный шип от точки p наружу под углом deg: длина len, полуширина основания hw.
std::string spike(Pt p, double deg, double len, double hw) {
  return "M" + ptS(polar(p.x, p.y, hw, deg - 90)) + "L" + ptS(polar(p.x, p.y, len, deg)) + "L" + ptS(polar(p.x, p.y, hw, deg + 90)) + "Z";
}

// Сужающаяся полоса по спирали вокруг (cx, cy): радиус от r0 до r1, угол от a0 до a1, ширина от w0 до w1.
std::string spiral(double cx, double cy, double r0, double r1, double a0, double a1, double w0, double w1, int steps = 24) {
  std::vector<Pt> in, ou;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, r = r0 + (r1 - r0) * t, a = a0 + (a1 - a0) * t, w = (w0 + (w1 - w0) * t) * 0.5;
    ou.push_back(polar(cx, cy, r + w, a));
    in.push_back(polar(cx, cy, std::max(0.0, r - w), a));
  }
  std::string s = "M" + ptS(ou[0]);
  for (size_t i = 1; i < ou.size(); i++) s += "L" + ptS(ou[i]);
  for (size_t i = in.size(); i-- > 0;) s += "L" + ptS(in[i]);
  return s + "Z";
}

// Томоэ-запятая в круге радиуса R: голова у центра под углом rot, хвост уходит по спирали к краю.
std::string tomoe(double cx, double cy, double R, double rot) {
  const Pt h = polar(cx, cy, R * 0.45, rot);
  return circ(h.x, h.y, R * 0.31) + spiral(cx, cy, R * 0.45, R * 0.9, rot, rot + 150, R * 0.62, 0.2, 20);
}

// Три томоэ (мицудомоэ).
std::string mitsudomoe(double cx, double cy, double R, double rot) {
  return tomoe(cx, cy, R, rot) + tomoe(cx, cy, R, rot + 120) + tomoe(cx, cy, R, rot + 240);
}

// ---------------------------------------------------------------- огры
void ogres(Catalog& c) {
  c.group("Огры", EmblemSection::Races);
  // Голова огра: тяжёлая угловатая голова анфас, железный обруч с заклёпками на лбу, шрам на темени, глаза-щели под надбровьем,
  // широкий нос, нижние зубы и клыки-полумесяцы.
  {
    const std::string head =
        sym("M50 11C41 11 34.5 13 31.5 17.5L28.5 29.5 24.5 35.5 21.5 51C17 56 15 62.5 15 69.5C15 78.5 20 85.5 28 89.5L40 95"
            "C43.5 96.2 47 96.6 50.5 96.6V11Z" +
            std::string("M24 44C19.5 42 15.5 43.5 14 47C13.5 50.5 15.5 53.5 20 54.5Z"));
    const std::string band = "M23 29.5C32 26.5 68 26.5 77 29.5L76 37.5C67 34.5 33 34.5 24 37.5Z";
    const std::string tusk = brush({37.5, 76}, {34, 71}, {30, 64}, {31.5, 53.5}, 6.6, 6.8, 0, 0.25);
    const std::string teeth = "M42 72 43.8 66.5 45.6 72ZM47.4 72 49.2 65.8 51 72ZM53 72 54.8 66.5 56.6 72Z";
    c.add(
        "race-ogres-head", "Голова огра",
        moved(
            {G::XS(head, 2.4), G::F(head), G::XS(inner(head, 3.2, 6), 1.5), G::XS(band, 2.4), G::F(band),
             G::X(lancet({28, 32.2}, {32, 32.2}, 2.2) + lancet({39, 31}, {43, 31}, 2.2) + lancet({57, 31}, {61, 31}, 2.2) +
                  lancet({68, 32.2}, {72, 32.2}, 2.2)),
             G::XS("M57 14.5C59.5 19 61 23 61.5 27", 2), G::XS(sym("M17 51C16.5 48 18 46 21 46"), 1.4),
             G::XS(sym("M27 40.5C34 39.5 41.5 41.5 47.5 47M24 54C27.5 57.5 32 59.5 36.5 59.5M46 49.5C45.5 54 44 57.5 41 60C41.5 63.5 44.5 65 47 63"),
                   2.3),
             G::X(sym(lancet({32, 45.5}, {44, 49}, 3.4, 0.7))), G::XS("M47 63C48.5 64.1 51.5 64.1 53 63", 1.6),
             G::X("M26.5 71.5C37 67.5 63 67.5 73.5 71.5C63 74.5 37 74.5 26.5 71.5Z"), G::F(teeth), G::XS("M41 86C46 84.5 54 84.5 59 86", 1.6),
             G::XS(sym(tusk), 2.6), G::F(sym(tusk))},
            Affine::translate(0, -3) * scaleAt(1.04f, 50, 54)));
  }
  // Дубина с шипами: веретено навершия с волокнами, кованый обруч, длинные гранёные шипы, обмотка рукояти, острое навершие.
  {
    const std::string head = "M44 58C40 50 32.5 42 32 30C31.5 18 39.5 7.5 50 4C60.5 7.5 68.5 18 68 30C67.5 42 60 50 56 58Z";
    std::string spikes = spike({50, 6}, 270, 9.5, 2.4);
    const struct {
      double x, y, a, len;
    } sp[] = {{43.2, 8.6, 235, 9.5}, {37.4, 14.5, 210, 10.5}, {33.2, 24, 190, 11}, {33, 35, 172, 11}, {37.4, 45.5, 152, 10}};
    for (const auto& s : sp) spikes += sym(spike({float(s.x), float(s.y)}, s.a, s.len, 2.4));
    const std::string grip = "M43.8 57H56.2L55.2 86H44.8Z";
    const std::string pommel = "M43 85.5H57L53.8 91.5 50 98.5 46.2 91.5Z";
    c.add(
        "race-ogres-club", "Дубина с шипами",
        moved(
            {G::F(head + spikes + grip + pommel), G::XS(clip(head, "M36 44C44 47.5 56 47.5 64 44M38.5 50.5C45 53.5 55 53.5 61.5 50.5", 0.8), 1.6),
             G::XS(clip(head, "M46.5 40C43.5 32 44 22 48.5 12M53.5 40C56.5 32 56 22 51.5 12M41 33C40 27 41 21 43.5 17M59 33C60 27 59 21 56.5 17", 3),
                   1.4),
             G::XS(clip(grip, "M44 63 56 59M44 69 56 65M44 75 56 71M44 81 56 77", 0.2), 1.6), G::XS("M44 86H56", 1.4)},
            Affine::translate(-4.5, 3) * rotAt(40, 50, 50) * scaleAt(1.07f, 50, 50)));
  }
  // Котёл: кованый котёл на когтистых ножках с ободом, поясом и кольцами-ушками; под ним огонь на скрещённых поленьях, над ним пар.
  {
    const std::string body = "M15 30C12 48 22 67 50 69C78 67 88 48 85 30Z";
    const std::string rim = rrect(10, 22.5, 80, 9.5, 4.5);
    const std::string legs = sym(brush({28, 62}, {26, 70}, {22, 75}, {15, 79}, 6, 6, 0, 0.3)) + "M46.5 66 46 76 50 80.5 54 76 53.5 66Z";
    // Пламя: три языка с общим основанием, кончики загибаются наружу; внутри — вырезанные языки.
    const std::string fire =
        brush({50, 94}, {45, 86}, {55, 79}, {50, 70}, 14, 14.5, 0, 0.18) + sym(brush({39, 94}, {34, 88}, {39, 82}, {30, 74}, 11, 11.5, 0, 0.18));
    const std::string fireCuts =
        brush({50, 91}, {48, 87}, {52.5, 83}, {50, 78}, 0, 3.4, 0, 0.5) + sym(brush({39.5, 91}, {37.5, 88}, {39.5, 85}, {35.5, 81}, 0, 2.8, 0, 0.5));
    const std::string logs = xf(rrect(-25, -3, 50, 6, 3), place(50, 92.4, 9)) + xf(rrect(-25, -3, 50, 6, 3), place(50, 92.4, -9));
    const std::string steam =
        brush({50, 21}, {45, 14.5}, {55, 8.5}, {50, 0.5}, 0, 4.2, 0, 0.45) + sym(brush({36, 21}, {31, 15.5}, {39, 9.5}, {33, 2.5}, 0, 3.6, 0, 0.45));
    c.add("race-ogres-cauldron", "Котёл",
          {G::F(fire), G::X(fireCuts), G::XS(logs, 2.2), G::F(logs), G::XS("M50 89.5V97.5", 1.4), G::F(steam), G::XS(legs, 2.2), G::F(legs),
           G::S(sym("M15.5 35C5 34 3 48 13.5 49"), 2.8), G::XS(body, 2.6), G::F(body), G::XS(inner(body, 3, 6), 1.4),
           G::XS(clip(body, "M16 41C30 47.5 70 47.5 84 41M18 50C32 56.5 68 56.5 82 50", 0.5), 1.5),
           G::XS(clip(body, "M22 45 27 52 32 47 37 54 42 49 47 55.5 50 51 53 55.5 58 49 63 54 68 47 73 52 78 45", 1.2), 1.3), G::XS(rim, 2.4),
           G::F(rim), G::XS("M14 27.2H86", 1.5)});
  }
  // Окорок: окорок с кромкой сала и волокнами мяса, обвязка бечёвкой, мосол с двумя мыщелками.
  {
    const std::string meat = "M73 42.5C62 37 52 26 34 26C17 26 6 37 6 50C6 63 17 74 34 74C52 74 62 63 73 57.5Z";
    const std::string bn = rrect(70, 45.5, 18.5, 9, 2.5) +
                           "M86 43C86.5 37.5 95.5 36 97 41.5C97.8 44.5 96 47.2 93.5 49.6V50.4C96 52.8 97.8 55.5 97 58.5C95.5 64 86.5 62.5 86 57Z";
    c.add("race-ogres-haunch", "Окорок",
          moved({G::F(bn), G::XS("M87.5 50H95", 1.5), G::XS(meat, 2.6), G::F(meat), G::XS(inner(meat, 4.2, 6), 1.8),
                 G::XS(clip(meat, "M14 45C25 38.5 41 38.5 54 45.5M16 56C28 61.5 44 60 57 53M24 50.5C34 48.5 44 49 52 51.5", 6.5), 1.5),
                 G::XS(clip(meat, "M63.5 37L66.5 63M68.5 39.5 70.5 60.5", 0), 1.6)},
                Affine::translate(50, 50) * Affine::rotate(-0.66f) * Affine::scale(1.08f) * Affine::translate(-51.5, -50)));
  }
}

// ---------------------------------------------------------------- они
void oni(Catalog& c) {
  c.group("Они", EmblemSection::Races);
  // Маска они: рога-полумесяцы с кольцами, грива языками пламени, нахмуренный лоб, раскосые глаза, оскал с клыками.
  {
    const std::string horn = brush({37.5, 25}, {31, 16}, {21, 12}, {18, 0.5}, 9, 9.4, 0, 0.2);
    const std::string face =
        sym("M50 19C42 19 35 21 30 25C25.5 29 22.5 35 21.5 42L15.5 47.5C18.5 52.5 20 58 21 63.5C23 72 28 80 34 86"
            "C39 91 44.5 94 50.5 95V19Z");
    const std::string mane = sym(flame({29, 30}, {5, 22}, 7, 2.2) + flame({23, 46}, {2.5, 50}, 7, -2) + flame({23, 60}, {6, 72}, 7, 2) +
                                 flame({28, 74}, {15, 89}, 6, -1.8));
    const std::string mouth = "M28.5 68C36 64.5 44 66 50 66C56 66 64 64.5 71.5 68C69.5 79 61 87.5 50 87.5C39 87.5 30.5 79 28.5 68Z";
    c.add("race-oni-mask", "Маска они",
          {G::F(mane), G::XS(face, 2.8), G::F(face), G::XS(inner(face, 3, 6), 1.4), G::XS(sym(horn), 2.6), G::F(sym(horn)),
           G::XS(clip(sym(horn), sym("M27 14 33 19M24 9 30.5 13"), 0.5), 1.5),
           G::XS(sym("M24.5 40C31 33.5 40 33 47.5 39.5M46 26C47.5 29 47.5 32 46 35M22.5 55C26 61 28 65 29 69M45.5 51C43.5 55 42.5 58.5 44 61"
                     "C45.5 62.5 47.5 62 48.5 60.5"),
                 2.3),
           G::X(sym(lancet({29.5, 45}, {46, 48.5}, 6, -1.4))), G::F(sym(lancet({38.5, 43.6}, {38.5, 50.6}, 2))), G::X(mouth),
           G::F(sym("M33.5 68 37 80 40 67.2ZM42 87.6 44.8 77.5 47.6 87.6Z"))});
  }
  // Скрещённые канабо: гранёные палицы, утолщённые кверху, с рядами шипов-ромбов, гарды и обмотка рукоятей.
  {
    std::string holes;
    for (int r = 0; r < 8; r++) {
      const double y = 11 + r * 6.6, hw = 7.4 - r * 0.25;
      for (double x : r % 2 ? std::vector<double>{50 - hw / 2.6, 50 + hw / 2.6} : std::vector<double>{50 - hw / 1.3, 50, 50 + hw / 1.3})
        holes += "M" + n(x) + " " + n(y - 2.1) + "L" + n(x + 1.6) + " " + n(y) + "L" + n(x) + " " + n(y + 2.1) + "L" + n(x - 1.6) + " " + n(y) + "Z";
    }
    const std::string club = "M40.5 7L44 2.5H56L59.5 7 57.5 63H42.5Z" + rrect(40, 62.5, 20, 5, 1.6) + "M45.8 67H54.2L53.6 89H46.4Z" +
                             "M44 88.5H56L53.5 93.5 50 98 46.5 93.5Z";
    c.add("race-oni-kanabo", "Скрещённые канабо",
          crossed(club, holes, "M46 72.5 54 75.5M46 78.5 54 81.5M46 84.5 54 87.5M43 6.5H57", 1.6, 34, 50, 50, 1.0f));
  }
  // Шлем они: кабуто с рогами-кувагата и гребнем-пламенем, рёбра купола, отвороты, расходящийся нашейник, личина с клыками.
  {
    const std::string horn = brush({37, 29}, {25, 22}, {14, 11}, {21, 0.5}, 6.5, 7, 0, 0.18);
    const std::string bowl = "M24 45C24 31 36 21 50 21C64 21 76 31 76 45Z";
    const std::string shikoro = sym("M22 51.5H50.5V59.5H15.5Z") + sym("M15 61H50.5V69.5H8Z") + sym("M8.5 71H50.5V80H2.5Z");
    const std::string menpo = "M35 52H65V68C65 80 58.5 89 50 92C41.5 89 35 80 35 68Z";
    c.add("race-oni-helmet", "Шлем они",
          {G::F(shikoro), G::XS(clip(shikoro, sym("M30 55.5H14M28 65H8M26 75.5H2"), 2.2), 1.4), G::F(sym(horn)),
           G::F(sym("M25.5 38L13 31.5 15.5 45.5 25.5 46.5Z")), G::XS(bowl, 2.6), G::F(bowl),
           G::XS(clip(bowl, "M50 23V44M42.5 23.5 40.5 44M57.5 23.5 59.5 44M35 27 31.5 44M65 27 68.5 44", 1.2), 1.6),
           G::F(lancet({50, 26}, {50, 2.5}, 9)), G::X("M50 21C47.5 18 47 14 50 9.5C53 14 52.5 18 50 21Z"), G::XS(rrect(17, 42.5, 66, 8, 2), 2.4),
           G::F(rrect(17, 42.5, 66, 8, 2)), G::XS(menpo, 2.8), G::F(menpo),
           G::XS(sym("M36.5 56C40 54.5 44 55.5 47.5 59M47.5 62C46 66 44 68.5 41 69.5"), 2), G::X(sym(lancet({37.5, 59}, {46.5, 61.5}, 3.2, -0.6))),
           G::X("M39 74C44 71.5 56 71.5 61 74C59 82 55 86 50 86C45 86 41 82 39 74Z"), G::F(sym("M41.5 73.4 43.5 80 45.5 72.6Z"))});
  }
  // Гром-барабаны: кольцо бога грома с барабанами-томоэ, между ними языки пламени, в центре — молния.
  {
    std::string drums, rims, marks, studs;
    for (int i = 0; i < 6; i++) {
      const double a = -90 + 60 * i;
      const Pt p = polar(50, 50, 36, a);
      drums += circ(p.x, p.y, 12.5);
      rims += circ(p.x, p.y, 9.6);
      marks += mitsudomoe(p.x, p.y, 8.4, a);
      studs += spike(polar(50, 50, 36, a + 30), a + 30, 9, 2.4) + spike(polar(50, 50, 36, a + 30), a + 210, 6, 2.4);
    }
    const std::string bolt = "M57.5 25 42 52H50L42.5 77 58 47.5H50.5L59.5 25Z";
    c.add("race-oni-drums", "Гром-барабаны",
          {G::S(circ(50, 50, 36), 3.4), G::F(studs), G::XS(drums, 2.8), G::F(drums), G::XS(rims, 1.5), G::X(marks), G::F(bolt)});
  }
}

// ---------------------------------------------------------------- орки
void orcs(Catalog& c) {
  c.group("Орки", EmblemSection::Races);
  // Орочий вождь: бюст анфас — голова с острыми ушами в короне из клыков, глаза-щели, клыки, латные наплечники с изогнутыми шипами.
  {
    const std::string bust = "M3 98C3 85 9 76 20 71L37 65H63L80 71C91 76 97 85 97 98Z";
    const std::string pauldron = sym("M2.5 80C3.5 68 12 60 24 59C32 58.5 38 61 40.5 66C35 70 30.5 74 27.5 80.5C19.5 78.5 10 79.5 2.5 84Z");
    const std::string thorns =
        sym(brush({10, 66}, {7, 61}, {4.5, 56}, {5, 49}, 5.2, 5.4, 0, 0.2) + brush({17.5, 62}, {15, 57}, {13, 52}, {13.5, 45.5}, 5.2, 5.4, 0, 0.2) +
            brush({26, 60}, {24.5, 55.5}, {23, 51.5}, {24, 46}, 4.8, 5, 0, 0.2));
    const std::string head = sym("M50 21C43 21 38.5 23 36.5 27L35 35 33 39.5C32 46 32 52 33 57C34 63 37 68 41.5 71C44 72.8 47 73.5 50.5 73.5V21Z" +
                                 std::string("M34.5 40C28 35 23 31 16.5 28.5C19 35.5 25 42 33 47.5Z"));
    const std::string crown = rrect(34, 17.5, 32, 6.5, 1.5) +
                              sym(brush({38, 18.5}, {37.5, 15}, {36.5, 12}, {34.5, 8}, 4.6, 4.7, 0, 0.2) +
                                  brush({44, 18.5}, {44, 13}, {43.5, 8.5}, {42, 3.5}, 4.8, 4.9, 0, 0.2)) +
                              lancet({50, 19}, {50, 0.5}, 6);
    const std::string tusk = brush({42.5, 67}, {41, 62.5}, {39.5, 58.5}, {40, 52.5}, 4.4, 4.5, 0, 0.25);
    c.add("race-orcs-chief", "Орочий вождь",
          {G::F(bust), G::XS(inner(bust, 3.2, 6), 1.5), G::XS(sym("M37 70C40 80 45 88 50 92"), 2), G::F(thorns), G::XS(pauldron, 2.6), G::F(pauldron),
           G::XS(clip(pauldron, sym("M3 76C10 70 19 68 30 69M5 71C12 65 21 63 33 64"), 1.6), 1.6), G::XS(head, 2.6), G::F(head),
           G::XS(sym("M31.5 41.5C27 38 23.5 35 20.5 33"), 1.5), G::XS(crown, 2.4), G::F(crown), G::XS("M34.5 20.7H65.5", 1.3),
           G::XS(sym("M36 39.5 47.5 44M46.5 46C46 50 44.5 53 43 55C44 57.5 46 58 47.5 57"), 2.2), G::X(sym(lancet({37.5, 43.6}, {46, 46.6}, 3, 0.5))),
           G::XS("M39 62.5C45 60.5 55 60.5 61 62.5", 2.2), G::XS(sym(tusk), 2.2), G::F(sym(tusk))});
  }
  // Кровавая рука: поднятая когтистая длань с рунными прорезями, потёки крови и гутты-брызги.
  {
    struct Finger {
      Pt b, t;
      double w, bend;
    };
    const Finger fs[] = {{{40.5, 47}, {35.5, 19}, 9.4, -2.2},
                         {{49.5, 45}, {49, 12}, 9.8, -1},
                         {{58.5, 47}, {62.5, 17}, 9.4, 1.4},
                         {{66, 53}, {74, 30}, 8.4, 2.4},
                         {{36, 64}, {17, 46}, 9.6, -2.4}};
    std::string fingers, creases;
    for (const Finger& f : fs) {
      // Палец сужается в коготь: прямой у основания, загнутый к острию; поперёк — суставы и лунка когтя.
      const double dx = f.t.x - f.b.x, dy = f.t.y - f.b.y, len = std::hypot(dx, dy), ux = dx / len, uy = dy / len;
      const Pt tip{float(f.t.x + ux * 9 + uy * f.bend * 1.5), float(f.t.y + uy * 9 - ux * f.bend * 1.5)};
      fingers += brush(f.b, lerp(f.b, f.t, 0.5), across(f.b, tip, 0.85, f.bend), tip, f.w, f.w * 0.9, 0, 0.55, 40);
      for (double t : {0.4, 0.68}) creases += "M" + ptS(across(f.b, tip, t, f.w * 0.4)) + "L" + ptS(across(f.b, tip, t, -f.w * 0.4));
      creases += "M" + ptS(across(f.b, tip, 0.8, f.w * 0.4)) + "Q" + ptS(across(f.b, tip, 0.86, 0)) + " " + ptS(across(f.b, tip, 0.8, -f.w * 0.4));
    }
    const std::string palm = "M33 47C33 42 40 40 50 40C60 40 68 43 69 50L68 72C66 80 60 84 50 85C40 84 34 80 33 72Z";
    std::string drops = goutte({39, 92}, 3.2, -95, 12) + goutte({50.5, 95}, 3.4, -90, 14) + goutte({61, 91}, 3, -85, 11);
    const struct {
      double x, y, r;
    } sp[] = {{14, 80, 2.6}, {85, 42, 2.4}, {84, 70, 2.8}, {20, 26, 2}, {78, 88, 2}, {24, 92, 1.8}};
    for (const auto& s : sp) drops += goutte({float(s.x), float(s.y)}, s.r, std::atan2(55 - s.y, 50 - s.x) * 180 / kPi, s.r * 3);
    c.add("race-orcs-hand", "Кровавая рука",
          {G::F(fingers + palm + drops), G::XS(clip(fingers, creases, 1.1), 1.5),
           G::XS(clip(palm, "M37 55C44 52 56 52 65 56M39 77C43 69 45.5 62 46 52M53 80C55 73 58 68 64 64", 2.5), 1.6)});
  }
  // Боевое знамя: древко с наконечником и рогатой перекладиной, полотнище с языками и каймой, клыкастый череп.
  {
    const std::string cloth = "M21 22.5H79V71L71 85 64 74 50 92 36 74 29 85 21 71Z";
    const std::string skull = "M50 30C42 30 36.5 35.5 36.5 43C36.5 48 39 51.5 42 53.5V58.5H58V53.5C61 51.5 63.5 48 63.5 43C63.5 35.5 58 30 50 30Z";
    const std::string tusk = sym(brush({43.5, 61}, {42, 57.5}, {40.5, 54}, {41, 49.5}, 3.8, 3.9, 0, 0.25));
    c.add("race-orcs-banner", "Боевое знамя",
          {G::F(rrect(47.5, 8, 5, 86, 2) + lancet({50, 14}, {50, 0.5}, 8) + "M47.5 92.5H52.5L50 99.5Z" + rrect(14, 17, 72, 5.5, 2.5) +
                sym(brush({17, 19.5}, {10, 18.5}, {6, 13}, {8.5, 3.5}, 5.5, 5.6, 0, 0.2))),
           G::XS(cloth, 2.4), G::F(cloth), G::XS(inner(cloth, 3.2, 6), 1.5), G::XS(skull, 2.2),
           G::X(sym("M40.5 41 47.5 43.5 46.5 47.5 41.5 46.5Z") + "M50 48 52.2 52.5H47.8Z"), G::XS("M46 54.5V58.5M50 54.5V58.5M54 54.5V58.5", 1.4),
           G::XS(tusk, 2), G::F(tusk)});
  }
  // Боевой барабан: барабан в обручах со шнуровкой, кожа с кольцом, шипы по бокам, ножки-клыки, колотушки крест-накрест.
  {
    const std::string drum = "M15 36V66C15 74 31 80 50 80C69 80 85 74 85 66V36C85 43 69 47 50 47C31 47 15 43 15 36Z";
    const std::string top = ell(50, 36, 35, 10.5);
    const std::string legs = sym(brush({26, 76}, {24, 84}, {20, 90}, {13, 95}, 6.5, 6.6, 0, 0.25)) + "M46 79H54L50 97Z";
    const std::string stick = taper({0, 0}, {0, -14}, {0, -28}, {0, -40}, 3, 4.4) + xf(ell(0, 0, 3.8, 5.6), Affine::translate(0, -43.5));
    const std::string sticks = xf(stick, place(76, 40, -52)) + xf(stick, place(24, 40, 52));
    c.add("race-orcs-drum", "Боевой барабан",
          {G::F(legs), G::XS(drum, 2.6), G::F(drum + sym(spike({15, 50}, 180, 8, 2.4) + spike({15, 62}, 180, 7, 2.2))),
           G::XS("M15 42C15 49 31 53 50 53C69 53 85 49 85 42M15 62C15 69 31 74 50 74C69 74 85 69 85 62", 2),
           G::XS("M19 50 27 70 35 53.5 43 73 50 54.5 57 73 65 53.5 73 70 81 50", 1.6), G::XS(top, 2.6), G::F(top), G::XS(ell(50, 36, 29.5, 7.6), 1.5),
           G::XS(sticks, 2.4), G::F(sticks), G::XS(xf("M-3 -36H3", place(76, 40, -52)) + xf("M-3 -36H3", place(24, 40, 52)), 1.3)});
  }
}

// ---------------------------------------------------------------- падшие эльфы
// Обломок кольца-эллипса (cx, cy, rx, ry) толщиной w от a0 до a1 градусов с зазубренными краями (сдвиг излома jag градусов).
std::string ringShard(double cx, double cy, double rx, double ry, double w, double a0, double a1, double jag0, double jag1) {
  auto at = [&](double ra, double rb, double deg) {
    const double t = deg * kPi / 180;
    return Pt{float(cx + ra * std::cos(t)), float(cy + rb * std::sin(t))};
  };
  std::vector<Pt> ou, in;
  const int steps = 24;
  for (int i = 0; i <= steps; i++) {
    const double a = a0 + (a1 - a0) * i / steps;
    ou.push_back(at(rx + w / 2, ry + w / 2, a));
    in.push_back(at(rx - w / 2, ry - w / 2, a));
  }
  std::string s = "M" + ptS(ou[0]);
  for (size_t i = 1; i < ou.size(); i++) s += "L" + ptS(ou[i]);
  s += "L" + ptS(at(rx, ry, a1 + jag1));
  for (size_t i = in.size(); i-- > 0;) s += "L" + ptS(in[i]);
  return s + "L" + ptS(at(rx, ry, a0 - jag0)) + "Z";
}

void fallenElves(Catalog& c) {
  c.group("Падшие Эльфы", EmblemSection::Races);
  // Падший эльф: фигура в длинном одеянии, голова склонена, уши поникли; крылья опущены, перья острые, местами обломаны.
  {
    const Pt a0{43, 43}, a1{33, 27}, a2{21, 15}, a3{7, 13};
    const double ts[] = {0.98, 0.82, 0.66, 0.5, 0.34, 0.18};
    const Pt tips[] = {{2.5, 52}, {5, 66}, {11.5, 76}, {18, 89}, {27, 90}, {35.5, 95}};
    std::vector<std::string> feathers;
    for (int k = 0; k < 6; k++) {
      const CurvePt q = bezier(a0, a1, a2, a3, ts[k]);
      feathers.push_back(lancet({float(q.x), float(q.y)}, tips[k], 9.5 - k * 0.2, k % 2 ? 1.2 : -1.2, 0.3));
    }
    // Кроющие перья вдоль плечевой дуги.
    for (int k = 0; k < 5; k++) {
      const double t = 0.9 - k * 0.17;
      const CurvePt q = bezier(a0, a1, a2, a3, t);
      const Pt b{float(q.x), float(q.y)};
      feathers.push_back(lancet(b, {float(q.x - 3 - k * 0.6), float(q.y + 15 - k)}, 7, -0.8, 0.3));
    }
    feathers.push_back(brush(a0, a1, a2, a3, 8, 7.5, 1.5, 0.25));
    std::vector<L> v;
    layeredFeathers(v, feathers, 2.2f, true);
    const std::string fig = sym("M50 17C43 17 39.5 22 40 29C40.5 37 38.5 45 35 51C39.5 51 42.5 48 44 44L44.8 33C45.3 28.5 47.3 26 50.5 25.5V17Z" +
                                std::string("M44.5 30.5 32 39.5 44.5 35Z")) +
                            ell(50, 29, 5.6, 7.4) +
                            "M38 40C42 38 46 37.5 50 37.5C54 37.5 58 38 62 40C63 46 62 52 59 58L65 93C60 95.5 55 96 50 96C45 96 40 95.5 35 93L41 58"
                            "C38 52 37 46 38 40Z";
    for (const L& l :
         std::vector<L>{G::XS(fig, 3), G::F(fig), G::XS(sym("M41 42C40 48 42 53.5 46 56M44.8 33C45.3 28.5 47.3 26 50 25.5"), 1.7),
                        G::XS(clip(fig, "M47 63 44.5 94M53 63 55.5 94M43 66 39 92M57 66 61 92", 1.5), 1.5),
                        G::XS(sym("M45.8 30.4C46.6 31.3 47.6 31.5 48.5 31.2"), 1.3), G::XS(ell(50, 56, 4.6, 3.6), 2), G::F(ell(50, 56, 4.6, 3.6))})
      v.push_back(l);
    c.add("race-fallen-elves-angel", "Падший эльф", moved(v, Affine::translate(0, -3) * scaleAt(0.93f, 50, 52)));
  }
  // Сломанный нимб: кольцо в перспективе разломано, правый обломок осел; над уцелевшей частью — лучи, ниже — осколки и падающее перо.
  {
    const std::string a = ringShard(50, 27, 40, 15, 11, 150, 300, 6, -7);
    const Affine drop = rotAt(14, 70, 40) * Affine::translate(2, 7);
    const std::string b = xf(ringShard(50, 27, 40, 15, 11, 312, 488, -7, 6), drop);
    auto ep = [](double r, double deg) {
      const double t = deg * kPi / 180;
      return Pt{float(50 + (40 + r) * std::cos(t)), float(27 + (15 + r) * std::sin(t))};
    };
    std::string rays;
    for (int i = 0; i < 6; i++) {
      const double deg = 196 + i * 19;
      const Pt b = ep(5, deg), t = ep(i % 2 ? 12 : 18, deg);
      rays += "M" + ptS(across(b, t, 0, 1.6)) + "L" + ptS(t) + "L" + ptS(across(b, t, 0, -1.6)) + "Z";
    }
    const Pt fb{61, 95}, ft{39, 47};
    const std::string fea = lancet(fb, ft, 18, 2, 0.35);
    // Расщепы опахала: косые надрезы от края к стержню.
    const std::string barbs = "M" + ptS(across(fb, ft, 0.6, 10)) + "L" + ptS(across(fb, ft, 0.5, 2.2)) + "M" + ptS(across(fb, ft, 0.4, -10)) + "L" +
                              ptS(across(fb, ft, 0.3, -2.2)) + "M" + ptS(across(fb, ft, 0.78, -8)) + "L" + ptS(across(fb, ft, 0.7, -2));
    c.add("race-fallen-elves-halo", "Сломанный нимб",
          moved({G::F(a + b + rays + spike({12, 56}, 240, 8, 2) + spike({86, 66}, 300, 7, 1.8) + spike({22, 66}, 250, 6, 1.6)), G::XS(fea, 3.2),
                 G::F(fea + brush({59.5, 92}, {61, 95}, {62, 97}, {63.5, 99.5}, 2.6, 2.4, 1.6)),
                 G::XS(clip(fea, "M" + ptS(across(fb, ft, 0.06, 0)) + "Q" + ptS(across(fb, ft, 0.5, 1)) + " " + ptS(across(fb, ft, 0.92, 0)), 0.8),
                       1.4),
                 G::XS(clip(fea, barbs, -1), 1.6)},
                Affine::translate(50, 50) * Affine::scale(0.9f) * Affine::translate(-46, -47.5)));
  }
  // Падающая звезда: восьмилучевая гранёная звезда летит вниз, за ней — три каллиграфических шлейфа и искры.
  {
    const double cx = 31, cy = 68;
    std::string s, facets;
    for (int i = 0; i < 16; i++) {
      const double a = -90 + 22.5 * i;
      s += (i == 0 ? "M" : "L") + pt(cx, cy, i % 4 == 0 ? 29 : i % 2 ? 7.5 : 17, a);
    }
    for (int i = 0; i < 8; i++) facets += "M" + n(cx) + " " + n(cy) + "L" + pt(cx, cy, i % 2 ? 14 : 25, -90 + 45 * i);
    const std::string trails =
        brush({40, 58}, {55, 40}, {72, 21}, {95, 5}, 16, 14, 0, 0.1) + brush({27, 47}, {35, 31}, {46, 16}, {62, 3}, 10, 9.5, 0, 0.1) +
        brush({52, 72}, {68, 66}, {80, 56}, {97, 40}, 10, 9.5, 0, 0.1) + brush({36, 52}, {45, 36}, {56, 22}, {76, 7}, 6, 5.6, 0, 0.1);
    c.add("race-fallen-elves-star", "Падающая звезда",
          {G::F(trails), G::XS(s + "Z", 3.4), G::F(s + "Z"), G::XS(clip(s + "Z", facets, 0.9), 1.5),
           G::F(star(84, 76, 6, 1.3, 4, -90) + star(15, 29, 5, 1.2, 4, -90) + star(64, 89, 4, 1, 4, -90))});
  }
  // Увядшее древо: искривлённый ствол с трещиной и дуплом, ветви дугой вверх и поникшие к земле, корни, опадающие листья.
  {
    const std::string trunk = "M41.5 96C43.5 84 45 70 44.5 58C44 50 45 44 47.5 37H52.5C55 44 56 50 55.5 58C55 70 56.5 84 58.5 96Z";
    const std::string limbs =
        sym(brush({47.5, 44}, {42, 33}, {35, 25}, {25, 21}, 8, 7.4, 5, 0.2) + brush({26, 21}, {16, 18}, {9, 24}, {7, 38}, 5, 4.6, 0, 0.15) +
            brush({30, 23}, {28, 16}, {29, 11}, {34, 6}, 3.6, 3.4, 0, 0.2) + brush({17, 19.5}, {13, 13}, {9, 11}, {4, 12}, 3.2, 3, 0, 0.2) +
            brush({45.5, 56}, {38, 50}, {29, 50}, {22, 58}, 5, 4.6, 0, 0.2) + brush({30, 51}, {25, 48}, {20, 49}, {16, 53}, 2.8, 2.6, 0, 0.2) +
            brush({45, 88}, {39, 92}, {30, 95}, {18, 97}, 6, 5.5, 0, 0.2) + brush({47, 92}, {44, 95}, {40, 97}, {35, 99}, 4.5, 4.2, 0, 0.2)) +
        brush({50, 39}, {48, 28}, {53, 18}, {50, 4}, 6.4, 6, 0, 0.2);
    const std::string leaves = lancet({13, 70}, {19, 79}, 4.6, 1) + lancet({84, 66}, {90, 75}, 4.6, -1) + lancet({27, 84}, {35, 87}, 4.2, 1) +
                               lancet({77, 82}, {69, 86}, 4.2, -1);
    c.add("race-fallen-elves-tree", "Увядшее древо",
          {G::F(limbs + trunk + leaves), G::XS(clip(trunk + limbs, "M50 52 48 59 52 65 49 72 51 80", 1.2), 1.6), G::X(ell(53, 70, 1.6, 3)),
           G::XS(clip(trunk, "M46.5 92C48 80 47.5 68 47 58M54 92C52.5 82 53 74 54.5 64", 1.3), 1.4)});
  }
}

// ---------------------------------------------------------------- пустынные эльфы
void desertElves(Catalog& c) {
  c.group("Пустынные Эльфы", EmblemSection::Races);
  // Скорпион: гербовый скорпион головой вверх — клешни-полумесяцы, суставчатые ноги, сегменты брюшка, хвост дугой и крючок жала.
  {
    // Клешня: ладонь-веретено и два загнутых пальца остриями навстречу (в местных координатах от запястья вдоль оси x).
    const std::string chela = "M0 -3.6C4 -6 9 -6.2 12.5 -3.4V3.4C9 6.2 4 6 0 3.6Z" +
                              brush({11.5, -2.4}, {15.5, -4.6}, {19.5, -4.4}, {22.5, -0.8}, 3.8, 3.6, 0, 0.3) +
                              brush({11.5, 2.4}, {15, 5}, {18.5, 5.2}, {21, 1.6}, 3.6, 3.4, 0, 0.3);
    const std::string arm = sym(brush({44, 31}, {39, 27}, {34, 24.5}, {29.5, 22.5}, 5.4, 5, 4.2, 0.3) +
                                brush({30, 23}, {28.5, 21}, {27.5, 19.5}, {26.5, 18}, 4.6, 4.8, 4, 0.5) + xf(chela, place(27, 19, -122)));
    std::string legs;
    for (int i = 0; i < 4; i++) {
      const Pt b{42.5f, float(36 + i * 5.2)}, k{float(28 - i * 1.5), float(27 + i * 10)}, t{float(19 + i * 1.2), float(38 + i * 13.5)};
      legs += sym(brush(b, lerp(b, k, 0.33), lerp(b, k, 0.66), k, 3.4, 3.2, 2.8, 0.5) +
                  brush(k, lerp(k, t, 0.33), across(k, t, 0.66, 1.2), t, 2.8, 2.6, 0, 0.3));
    }
    const std::string head =
        "M44.5 26.5H55.5C58 26.5 59.8 28.5 59.8 31.5L59 43.5C58.5 45.8 56.5 47 54 47H46C43.5 47 41.5 45.8 41 43.5L40.2 31.5C40.2 28.5 42 26.5 44.5 "
        "26.5Z";
    const std::string belly = "M41 45C39.8 52 40.3 60 43 66.5C45 71 47.5 74 50 74C52.5 74 55 71 57 66.5C59.7 60 60.2 52 59 45Z";
    const Pt t0{50, 72}, t1{50, 93}, t2{80, 99}, t3{82, 64};
    const std::string tail = taper(t0, t1, t2, t3, 9.5, 6.5);
    std::string rings;
    for (double t : {0.18, 0.36, 0.54, 0.72}) {
      const CurvePt q = bezier(t0, t1, t2, t3, t);
      rings += "M" + ptS({float(q.x - q.nx * 6), float(q.y - q.ny * 6)}) + "L" + ptS({float(q.x + q.nx * 6), float(q.y + q.ny * 6)});
    }
    const std::string sting = xf(ell(0, 0, 4.6, 6), place(82, 61, 10)) + brush({83, 58}, {85, 50}, {80, 45}, {72, 47}, 5.6, 5.4, 0, 0.25);
    c.add("race-desert-elves-scorpion", "Скорпион",
          moved({G::F(legs + arm), G::XS(sym("M21 6.5C25 9.5 26.5 13 26 17"), 1.4), G::XS(head + belly, 2.4), G::F(head + belly + tail + sting),
                 G::XS(clip(belly, "M40 51.5H60M40 58H60M41 64.5H59M43 70H57", 0.4), 1.8), G::XS(rings, 1.6), G::XS(clip(head, "M50 33V46", 1), 1.5),
                 G::X(sym(lancet({46, 30}, {48.6, 31.6}, 2)))},
                Affine::translate(0.5, 2)));
  }
  // Полумесяц над дюнами: тонкий месяц со звездой над слоями барханов с острыми гребнями; теневые склоны заштрихованы.
  {
    const std::string moon = crescent(42, 31, 25, -35, 58, 9.5);
    std::string st;
    for (int i = 0; i < 16; i++) st += (i ? "L" : "M") + pt(67, 25, i % 4 == 0 ? 11 : i % 2 ? 2.4 : 5.5, -90 + 22.5 * i);
    const std::string back = "M2 82C12 74 24 64 36 57C41 64 52 72 66 76C77 79 88 79 98 79V86H2Z";
    const std::string front = "M2 92C17 85 34 77 55 70C61 78 73 86 86 89C90 90 94 90.5 98 90.5V98H2Z";
    c.add("race-desert-elves-dunes", "Полумесяц над дюнами",
          {G::F(moon + st + "Z"), G::XS(clip(moon, arc(42, 31, 21, 90, 260), 1), 1.5), G::F(back), G::XS("M36 57C39 66 45 74 53 80", 2),
           G::XS(clip(back, "M38 64 33 69M42 69 36 75M46 73 39 79M50 76 44 82", 1.2), 1.4), G::XS(front, 2.8), G::F(front),
           G::XS("M55 70C58 79 64 87 71 93", 2), G::XS(clip(front, "M58 78 51 82M62 84 54 88M66 88 57 93M70 92 61 96", 1.2), 1.4)});
  }
  // Скрещённые шамширы: сильно изогнутые узкие клинки остриями вверх, крестовины с острыми загнутыми усами, рукояти с навершием-крюком.
  {
    const std::string blade = brush({48.5, 72}, {44.5, 50}, {47.5, 26}, {64, 2}, 8.4, 10.6, 0, 0.55, 40);
    const std::string hilt = "M37.5 70H62.5L60.5 74H39.5Z" + sym(brush({39.5, 72}, {35.5, 73.5}, {33, 77}, {34, 81.5}, 3.6, 3.2, 0, 0.3)) +
                             "M47 74H53L52.5 87H47.5Z" + "M46 86.5H54C57 87.5 58.5 90.5 56.5 94L52 92.5 50.5 89.5H46Z";
    const Affine m = rotAt(30, 50, 56);
    const std::string sword = xf(blade + hilt, m), back = mirrorX(sword, 50);
    const std::string fuller = xf(clip(blade, "M50.5 68C48.5 52 50 36 57 20", 1.2), m);
    c.add("race-desert-elves-shamshir", "Скрещённые шамширы",
          moved({G::F(back), G::XS(mirrorX(fuller, 50), 1.4), G::XS(sword, 3.6), G::F(sword), G::XS(fuller, 1.4),
                 G::F(crescent(50, 10, 8, -90, 52, 3.6))},
                Affine::translate(0, 2)));
  }
  // Сокол пустыни: гербовый сокол с раскрытыми крыльями, голова в профиль с крюком клюва и «усом», острые маховые перья, хвост веером.
  {
    const Pt a0{45, 40}, a1{38, 27}, a2{28, 17}, a3{17, 11};
    std::vector<std::string> feathers;
    // Хвост веером.
    feathers.push_back(sym(lancet({48, 64}, {39, 93}, 9, 1, 0.35)));
    feathers.push_back(lancet({50, 64}, {50, 97}, 10, 0, 0.35));
    // Маховые: от внешних к внутренним.
    for (int k = 0; k < 7; k++) {
      const double t = 0.97 - k * 0.12, ang = 205 - k * 14.5, len = 18 - k * 0.6;
      const CurvePt q = bezier(a0, a1, a2, a3, t);
      const Pt b{float(q.x), float(q.y)};
      feathers.push_back(sym(lancet(b, polar(b.x, b.y, len, ang), 8.2, 1, 0.3)));
    }
    feathers.push_back(sym(brush(a0, a1, a2, a3, 9, 8, 1.6, 0.25)));
    std::vector<L> v;
    layeredFeathers(v, feathers, 2.1f, false);
    const std::string body = "M50 30C44 30 40.5 36 40.5 44C40.5 54 44 62 46 68H54C56 62 59.5 54 59.5 44C59.5 36 56 30 50 30Z";
    const std::string head =
        "M50 15C45 15 41.5 18.5 41 22.5C38 21.5 35.5 23.5 35 26.5C34.8 28.5 35.5 30 36.5 31C37 29 38.5 28 41 28C42.5 31 45.5 33 50 33"
        "C55 33 58.5 29 58.5 23.5C58.5 18.5 55 15 50 15Z";
    const std::string talons = sym(brush({45.5, 66}, {42, 70}, {38, 73}, {34, 74}, 4, 3.6, 3) + crescent(33, 78.5, 4.5, -10, 40, 2.4));
    for (const L& l : std::vector<L>{
             G::XS(body + talons, 2.4), G::F(body + talons), G::XS(clip(body, "M45 42 50 45 55 42M44.5 49 50 52 55.5 49M45 56 50 59 55 56", 1), 1.4),
             G::XS(head, 2.4), G::F(head), G::X(lancet({42.5, 22.5}, {47, 23.2}, 2.6)), G::XS("M44 25C43.5 28 44 30.5 45.5 32.5M36.5 27H40", 1.5)})
      v.push_back(l);
    c.add("race-desert-elves-falcon", "Сокол пустыни", v);
  }
}

// ---------------------------------------------------------------- ракшасы
void rakshasas(Catalog& c) {
  c.group("Ракшасы", EmblemSection::Races);
  // Голова ракшасы: тигриная морда анфас в высоком венце, бакенбарды языками пламени, раскосые глаза с вертикальным зрачком, полосы, клыки.
  {
    const std::string face = sym("M50 27C42 27 35 29 30.5 33C26 37 23 43 22 50C21 58 23 66 27 73C32 82 40 89 50.5 92V27Z" +
                                 std::string("M31 35C28 27 22 23 15 23.5C14.5 30 17.5 36 24 40Z"));
    const std::string ruff = sym(flame({24, 49}, {5, 54}, 7.5, 2) + flame({24, 59}, {7, 71}, 7.5, -2) + flame({29, 71}, {16, 86}, 6.5, 2));
    const std::string crown = sym("M33 29H50.5V1.5L46 12 42 8.5 40.5 18 35.5 14.5Z");
    const std::string stripes = brush({45, 30.5}, {44.5, 33.5}, {43, 36}, {40.5, 38}, 3, 2.8, 0, 0.3) +
                                brush({39, 30.5}, {37.5, 33}, {35, 34.5}, {31.5, 35.5}, 2.8, 2.6, 0, 0.3) +
                                brush({22.5, 54}, {26, 54}, {29.5, 55}, {33, 57.5}, 3.2, 3, 0, 0.3) +
                                brush({23.5, 62}, {27, 62}, {30, 63}, {33, 65.5}, 3, 2.8, 0, 0.3);
    c.add("race-rakshasas-head", "Голова ракшасы",
          {G::F(ruff), G::XS(face, 2.6), G::F(face), G::XS(crown, 2.4), G::F(crown), G::XS("M33.5 25.5H66.5", 1.4),
           G::X(circ(50, 19, 2.6) + sym(lancet({40.5, 22.2}, {44.5, 22.2}, 2.4))), G::XS(sym("M19.5 33.5C21 30 23 28 25.5 27.5"), 1.4),
           G::X(sym(stripes)), G::X(sym(lancet({32.5, 46}, {45, 49.2}, 5.2, -1.2))), G::F(sym(lancet({39.6, 44.2}, {39.6, 50.6}, 1.9))),
           G::XS(sym("M46.5 49.5C47 52.5 46.5 55 45 56.5M41 69C44 70.5 47.5 70 50 67"), 1.6), G::X("M44 56.5H56L50 63Z"), G::XS("M50 63V67", 1.6),
           G::X("M40.5 72C45 70.4 55 70.4 59.5 72C58 80 54 84 50 84C46 84 42 80 40.5 72Z"), G::F(sym("M41.8 71.8 44 80.5 46.2 71.2Z"))});
  }
  // Когтистая длань: ладонь в жесте защиты, длинные пальцы с загнутыми когтями, око с вертикальным зрачком, браслет с камнями, полосатое запястье.
  {
    struct Finger {
      Pt b, t;
      double w, bend;
    };
    const Finger fs[] = {{{39.5, 46}, {35.5, 15}, 8.4, -1.8},
                         {{48, 44}, {47.5, 10}, 8.8, -0.6},
                         {{56.5, 45}, {59, 13}, 8.4, 1},
                         {{63.5, 50}, {68.5, 24}, 7.4, 1.8},
                         {{35, 64}, {19, 46}, 8.8, -2}};
    std::string fingers, creases;
    for (const Finger& f : fs) {
      const double dx = f.t.x - f.b.x, dy = f.t.y - f.b.y, len = std::hypot(dx, dy), ux = dx / len, uy = dy / len;
      const Pt tip{float(f.t.x + ux * 10 + uy * f.bend * 2), float(f.t.y + uy * 10 - ux * f.bend * 2)};
      fingers += brush(f.b, lerp(f.b, f.t, 0.5), across(f.b, tip, 0.85, f.bend), tip, f.w, f.w * 0.88, 0, 0.6, 40);
      for (double t : {0.36, 0.6}) creases += "M" + ptS(across(f.b, tip, t, f.w * 0.4)) + "L" + ptS(across(f.b, tip, t, -f.w * 0.4));
      creases += "M" + ptS(across(f.b, tip, 0.74, f.w * 0.4)) + "Q" + ptS(across(f.b, tip, 0.8, 0)) + " " + ptS(across(f.b, tip, 0.74, -f.w * 0.4));
    }
    const std::string palm = "M33 46C33 42 40 40 50 40C60 40 67 43 67.5 49L67 70C66 77 61 81 55 82H45C39 81 34 77 33 70Z";
    const std::string band = rrect(34, 80.5, 32, 7.5, 3);
    const std::string wrist = "M38.5 87H61.5L60.5 99H39.5Z";
    c.add("race-rakshasas-claws", "Когтистая длань",
          moved({G::F(fingers + palm + wrist), G::XS(clip(fingers, creases, 1.1), 1.4), G::X(lancet({37.5, 62}, {62.5, 62}, 11)),
                 G::F(circ(50, 62, 4.4)), G::X(lancet({50, 58.2}, {50, 65.8}, 1.8)), G::XS(band, 2.4), G::F(band),
                 G::X(lancet({42, 84.2}, {46, 84.2}, 2.6) + lancet({54, 84.2}, {58, 84.2}, 2.6) + circ(50, 84.2, 1.7)),
                 G::X(sym(brush({39.5, 91}, {43, 91}, {46, 92.5}, {48.5, 95}, 2.8, 2.6, 0, 0.3)))},
                Affine::translate(8, 0)));
  }
  // Тришула: лезвие-лист посередине, боковые зубья дугой наружу и вверх, барабан-дамару на древке, ленты с подвесками.
  {
    const std::string prongs = lancet({50, 34}, {50, 0.5}, 16, 0, 0.4) + sym(brush({45, 36}, {30, 38.5}, {19, 30}, {23.5, 5.5}, 9, 9.6, 0, 0.3));
    const std::string drum =
        "M36 46C36 44.3 42 43.2 50 43.2C58 43.2 64 44.3 64 46L54.5 55 64 64C64 65.7 58 66.8 50 66.8C42 66.8 36 65.7 36 64L45.5 55Z";
    const std::string ribbons = sym(brush({45, 55}, {37, 59}, {35, 67}, {29, 74}, 3.2, 3, 2) + lancet({29.5, 73}, {27, 83}, 4.2));
    c.add("race-rakshasas-trishula", "Тришула",
          {G::F(prongs + "M33 31H67L60.5 40H39.5Z" + rrect(46, 38, 8, 54, 2.4) + "M46 91H54L50 99.5Z"), G::XS("M38 35.8H62", 1.4), G::F(ribbons),
           G::XS(drum, 2.6), G::F(drum), G::XS(clip(drum, "M40 47 60 63M60 47 40 63", 0.8), 1.4), G::XS("M38.5 47.4H61.5M38.5 62.6H61.5", 1.4),
           G::XS("M47 75H53M47 80H53", 1.4)});
  }
  // Тигриный трон: стрельчатая килевидная спинка с сиянием, полосатое сиденье, подлокотники-волюты, ножки — тигриные лапы.
  {
    const std::string back = "M23 62V34C23 24 33 18 43 13C46.5 11 49 8 50 2.5C51 8 53.5 11 57 13C67 18 77 24 77 34V62Z";
    const std::string arch = "M31 60V36C31 29 38 24 45 20.5C47.5 19 49.2 17.5 50 15.5C50.8 17.5 52.5 19 55 20.5C62 24 69 29 69 36V60Z";
    std::string rays;
    for (int i = 0; i < 12; i++) {
      const double a = -90 + 30 * i;
      rays += lancet(polar(50, 38, 7, a), polar(50, 38, i % 2 ? 12 : 16, a), 3.2);
    }
    const std::string seat = rrect(13, 56, 74, 11, 3);
    const std::string arms = sym(rrect(9, 50, 16, 7, 3) + spiral(13, 47, 1, 6.5, 270, 630, 4.2, 3, 32));
    const std::string paws = sym("M18 67H30V82C30 84 33 85 35 86C35.5 89 34 91 32 91H16C14 91 13 89.5 13.5 87.5C14 85 17 84 18 82Z");
    c.add(
        "race-rakshasas-throne", "Тигриный трон",
        {G::F(back), G::XS(inner(back, 2.6, 6), 1.4), G::X(arch), G::F(rays + circ(50, 38, 5.2)), G::X(circ(50, 38, 2)), G::F(arms), G::XS(seat, 2.4),
         G::F(seat),
         G::X(sym(brush({14, 59}, {19, 59}, {22, 60}, {25, 63}, 3, 2.8, 0, 0.3) + brush({32, 59}, {36, 59}, {38.5, 60}, {40.5, 63}, 3, 2.8, 0, 0.3))),
         G::F(paws), G::XS(sym("M21 87V90.5M25 87V90.5M29 87V90.5"), 1.4), G::F(rrect(6, 91, 88, 6, 2.4)), G::XS("M6 91H94", 1.6)});
  }
}

// ---------------------------------------------------------------- сатиры
// Лист плюща: три острые лопасти от черенка в (0, 0) вверх (по оси −y).
const char* const kIvyLeaf = "M0 0C-2-1-5-2-7.5-5L-5-6C-6-9-4-12 0-15C4-12 6-9 5-6L7.5-5C5-2 2-1 0 0Z";
// Лист лозы с острыми лопастями от черенка в (0, 0) вверх (по оси −y), длина ~15,5; жилки — отдельно.
const char* const kVineLeaf =
    "M0 0C-2-1-4.5-1.2-6.5-2.2L-10-4.6-7.4-5.6-9.6-9.4-5.2-8.6-5-12.6-2.2-10.2 0-15.5 2.2-10.2 5-12.6 5.2-8.6 9.6-9.4 7.4-5.6"
    " 10-4.6 6.5-2.2C4.5-1.2 2-1 0 0Z";
const char* const kVineVeins = "M0 -1.5V-12M0 -4 -6.4 -7.6M0 -4 6.4 -7.6";

// Гроздь винограда: ряды ягод-овалов (counts сверху вниз) от верхнего ряда в (x, y), радиус ягоды r; блики — дугами.
void grapeBunch(double x, double y, double r, std::initializer_list<int> counts, std::string& berries, std::string& glints) {
  const double d = r * 1.95;
  int row = 0;
  for (int k : counts) {
    for (int i = 0; i < k; i++) {
      const double cx = x + (i - (k - 1) / 2.0) * d, cy = y + row * d * 0.86;
      berries += ell(cx, cy, r * 0.94, r);
      glints += arc(cx, cy, r * 0.58, 200, 265);
    }
    row++;
  }
}

void satyrs(Catalog& c) {
  c.group("Сатиры", EmblemSection::Races);
  // Флейта Пана: семь тростниковых трубок со скошенными срезами, две обвязки-шнура, развевающаяся лента.
  {
    std::string tubes, holes;
    for (int i = 0; i < 7; i++) {
      const double x = 17 + i * 9.6, top = 10, bot = 84 - i * 7, w = 8.2;
      tubes += "M" + n(x) + " " + n(top + 2.6) + "L" + n(x + w) + " " + n(top) + "V" + n(bot - 4) + "Q" + n(x + w) + " " + n(bot) + " " +
               n(x + w / 2) + " " + n(bot) + "Q" + n(x) + " " + n(bot) + " " + n(x) + " " + n(bot - 4) + "Z";
      holes += ell(x + w / 2, top + 5, 2.1, 1.4);
    }
    const std::string bands = rrect(13, 25.5, 75, 7, 2) + rrect(13, 45.5, 75, 7, 2);
    std::string cords;
    for (double x = 15.5; x < 88; x += 4.6) cords += "M" + n(x) + " 26.5L" + n(x + 2.6) + " 31.5M" + n(x) + " 46.5L" + n(x + 2.6) + " 51.5";
    const std::string ribbon =
        brush({87, 29}, {97, 37}, {84, 50}, {94, 68}, 5, 4.6, 0, 0.2) + brush({87, 49}, {98, 56}, {89, 66}, {98, 78}, 4.4, 4, 0, 0.2);
    c.add("race-satyrs-pipes", "Флейта Пана",
          moved({G::F(tubes), G::X(holes), G::XS(bands, 2.2), G::F(bands + ribbon), G::XS(clip(bands, cords, 0.8), 1.2)},
                Affine::translate(-5, 3.5) * rotAt(-12, 50, 50) * scaleAt(1.02f, 50, 50)));
  }
  // Рога и венок: козлиные рога с кольчатыми гребнями взмывают над венком из острых листьев лозы с гроздью внизу.
  {
    const Pt h0{42, 47}, h1{37.5, 27}, h2{24, 9}, h3{6, 10};
    const std::string horn = brush(h0, h1, h2, h3, 14, 12.5, 0, 0.12, 40);
    std::string rings;
    for (double t = 0.12; t < 0.82; t += 0.075) {
      const CurvePt q = bezier(h0, h1, h2, h3, t);
      const double w = brushW(t, 14, 12.5, 0, 0.12) * 0.5;
      rings += "M" + ptS({float(q.x + q.nx * (w + 1)), float(q.y + q.ny * (w + 1))}) + "L" +
               ptS({float(q.x - q.nx * w * 0.25), float(q.y - q.ny * w * 0.25)});
    }
    std::string leaves, veins;
    for (double a : {214.0, 184.0, 154.0, 124.0}) {
      const Pt p = polar(50, 60, 28, a);
      leaves += sym(xf(kIvyLeaf, place(p.x, p.y, float(a + 70), 1.15f)));
      veins += sym(xf("M0 -1.5V-11", place(p.x, p.y, float(a + 70), 1.15f)));
    }
    std::string berries, glints;
    grapeBunch(50, 79, 3.6, {3, 2, 1}, berries, glints);
    c.add("race-satyrs-horns", "Рога и венок",
          moved({G::XS(sym(horn), 2.6), G::F(sym(horn)), G::XS(sym(rings), 1.5), G::S(sym(arc(50, 60, 28, 96, 226)), 3.2), G::XS(leaves, 2.2),
                 G::F(leaves), G::XS(veins, 1.4), G::XS(berries, 2), G::F(berries), G::XS(glints, 1.2)},
                Affine::translate(0, -3)));
  }
  // Кубок вина: канфар с высокими ручками-петлями, пояс меандра на чаше, ножка с узлом, над краем — взлетающие капли вина.
  {
    const std::string bowl = "M24 30H76C76 48 66 58 50 60C34 58 24 48 24 30Z";
    const std::string rim = rrect(20, 25, 60, 6, 3);
    const std::string foot =
        rrect(46.5, 59, 7, 18, 2) + ell(50, 67, 6.5, 2.8) + "M30 92C32 84 40 79 50 79C60 79 68 84 70 92Z" + rrect(27, 91, 46, 4.5, 2);
    const std::string drops = goutte({50, 13}, 3.6, 90, 12) + sym(goutte({36, 17}, 3, 115, 9.5) + goutte({25.5, 9}, 2.2, 125, 7));
    c.add(
        "race-satyrs-goblet", "Кубок вина",
        {G::S(sym("M26.5 35C12 34 7 18 16.5 11.5C24 7 31 13 30 23"), 3.6), G::F(drops), G::XS(bowl, 2.4), G::F(bowl + foot),
         G::XS(clip(bowl, "M27 35.5H73M28 47H72", 0.4), 1.4), G::XS(clip(bowl, "M29 44V39H34V44H39V39H44V44H49V39H54V44H59V39H64V44H69V39", 1), 1.4),
         G::XS(clip(bowl, "M33 53C41 57 59 57 67 53", 1.6), 1.4), G::XS(rim, 2.2), G::F(rim), G::XS("M35 87.5C41 84.5 59 84.5 65 87.5", 1.4)});
  }
  // Виноградная гроздь: ягоды-овалы рядами 5–4–3–2–1 с бликами, черенок, два острых листа лозы с жилками, завиток усика.
  {
    std::string berries, glints;
    grapeBunch(50, 43, 5.7, {5, 4, 3, 2, 1}, berries, glints);
    const std::string leafA = xf(kVineLeaf, place(46, 33, -52, 2.1f)), leafB = xf(kVineLeaf, place(55, 31, 50, 1.55f));
    c.add("race-satyrs-grapes", "Виноградная гроздь",
          moved({G::F(brush({50, 38}, {50.5, 29}, {52, 21}, {56, 14}, 4.6, 4, 3, 0.4)), G::F(spiral(66, 9, 0.5, 6, 160, 560, 1.2, 2.6, 32)),
                 G::S("M56 15.5C59 13 61 12 63.5 12.5", 2.4), G::F(leafA + leafB),
                 G::XS(xf(kVineVeins, place(46, 33, -52, 2.1f)) + xf(kVineVeins, place(55, 31, 50, 1.55f)), 1.6), G::XS(berries, 2.2), G::F(berries),
                 G::XS(glints, 1.4)},
                Affine::translate(52.5, 50) * Affine::scale(1.07f) * Affine::translate(-50, -45.5)));
  }
}

// ---------------------------------------------------------------- светлые эльфы
void lightElves(Catalog& c) {
  c.group("Светлые Эльфы", EmblemSection::Races);
  // Солнце в листве: диск с двойным кольцом и восьмилучевой звездой, лучи — острые эльфийские листья с жилкой, между ними — прямые лучи.
  {
    std::string rays, ribs;
    std::vector<std::string> leaves;
    for (int i = 0; i < 8; i++) {
      const double a = -90 + 45 * i, b = a + 22.5;
      leaves.push_back(lancet(polar(50, 50, 18, a), polar(50, 50, 48, a), 10.5, 0, 0.42));
      ribs += "M" + pt(50, 50, 22, a) + "L" + pt(50, 50, 42, a);
      rays += "M" + pt(50, 50, 18, b - 7) + "L" + pt(50, 50, 37, b) + "L" + pt(50, 50, 18, b + 7) + "Z";
    }
    std::vector<L> v{G::F(rays)};
    layeredFeathers(v, leaves, 2, false);
    for (const L& l : std::vector<L>{G::XS(ribs, 1.4), G::XS(circ(50, 50, 17), 2.6), G::F(circ(50, 50, 17)), G::XS(circ(50, 50, 13.6), 1.4),
                                     G::X(star(50, 50, 10.5, 3.2, 8, -90)), G::F(circ(50, 50, 2.4))})
      v.push_back(l);
    c.add("race-light-elves-sun", "Солнце в листве", v);
  }
  // Единорог: гербовая голова в профиль — витой рог, грива языками, козлиная бородка, миндалевидный глаз, шея с рваным («оторванным») краем.
  {
    const std::string head =
        "M47.5 17L54 3.5 57.5 17.5C65.5 24 73.5 36 78.5 50C83.5 64 85.5 79 87.5 92L81 88.5 76 96 69.5 89.5 63 96.5 57 90 52.5 94"
        "C50.5 82 46.5 72 39.5 64.5C35.5 63.5 31.5 62.5 27.5 62.5C22 62.5 17 60.5 14 57.5C11 54.5 11 50 13.5 47L32 30.5C36 25.5 41 21 47.5 17Z";
    const std::string horn = brush({42, 23}, {36, 16}, {29, 9}, {21, 1.5}, 7, 6.4, 0, 0.12);
    std::string grooves;
    for (double t = 0.15; t < 0.85; t += 0.14) {
      const CurvePt q = bezier({42, 23}, {36, 16}, {29, 9}, {21, 1.5}, t);
      grooves +=
          "M" + ptS({float(q.x + q.nx * 4 - 1.5), float(q.y + q.ny * 4 - 1)}) + "L" + ptS({float(q.x - q.nx * 4 + 1), float(q.y - q.ny * 4 + 1.5)});
    }
    const std::string mane = flame({57, 20}, {74, 13}, 8, 2.4) + flame({64, 31}, {84, 27}, 9, 2.4) + flame({70, 44}, {92, 43}, 9.5, 2.4) +
                             flame({76, 58}, {97, 61}, 9, 2.4) + flame({80, 72}, {97, 79}, 8, 2.2) + flame({51, 21}, {44, 33}, 6.5, -1.6);
    const std::string beard = flame({25, 61}, {19, 76}, 6, 1.6);
    c.add("race-light-elves-unicorn", "Единорог",
          moved({G::F(mane + beard), G::XS(head, 2.6), G::F(head), G::XS(horn, 2.4), G::F(horn), G::XS(clip(horn, grooves, 0.7), 1.4),
                 G::XS(clip(head, "M55.5 9.5 55 16", 0.9), 1.4), G::X(lancet({34.5, 35.5}, {42.5, 36.5}, 3.4, -0.8)),
                 G::XS("M32.5 32C36 30.5 40 30.5 43 32.5M16.5 49.5C18 47.5 20.5 47.5 21.5 50M12 55.5 19 56.5", 1.6),
                 G::XS(clip(head, "M45 64C50 72 53 82 55 92M58 26C63 38 67 50 70 62M30 41C34 46 38 52 39 60", 1.8), 1.5)},
                Affine::translate(-4, 0.5)));
  }
  // Эльфийская арфа: шейка-волна с завитком, стройная колонна с листом, дека с резным краем и голосниками, тонкие струны.
  {
    const Pt n0{27, 21}, n1{42, 5}, n2{58, 34}, n3{82, 22};
    const std::string neck = brush(n0, n1, n2, n3, 7, 7.6, 5.6, 0.5);
    const std::string pillar = brush({28, 90}, {20.5, 66}, {22.5, 40}, {27, 19}, 7.6, 6.4, 6.2, 0.5);
    const std::string box = brush({83, 21}, {75, 44}, {55, 70}, {33, 91}, 7, 12.5, 15, 0.8);
    std::string strings;
    for (double t = 0.2; t < 0.96; t += 0.095) {
      const CurvePt q = bezier(n0, n1, n2, n3, t);
      const double yb = 22 + (82 - q.x) * 1.41 - 6;
      if (yb > q.y + 8) strings += "M" + n(q.x) + " " + n(q.y + 3) + "V" + n(yb);
    }
    c.add("race-light-elves-harp", "Эльфийская арфа",
          {G::S(strings, 1.4), G::F(neck + pillar + box + circ(25.5, 15.5, 6.2) + rrect(17, 86.5, 23, 9, 3)),
           G::X(spiral(25.5, 15.5, 0.5, 4.4, 0, 400, 1.6, 1.6, 30)), G::X(lancet({25.5, 32}, {24.5, 78}, 4.2, 1.6)),
           G::F(lancet({25.5, 38}, {24.8, 72}, 1.6, 1.4)), G::XS(clip(box, "M79 26C71 45 55 64 38 82", 1.4), 1.4),
           G::X(lancet({58, 56}, {64, 50}, 3.4) + lancet({45, 71}, {51, 65}, 3.4)), G::XS("M18 90.5H39", 1.4)});
  }
  // Крылатый шлем: стрельчатый эльфийский шлем с наносником и вырезами глаз, по бокам — поднятые крылья из острых перьев, на лбу — звезда.
  {
    const Pt bases[] = {{32, 60}, {32, 54}, {32, 48}, {33, 42}, {35, 36}};
    const Pt tips[] = {{13, 63}, {6, 52}, {3.5, 40}, {5.5, 27}, {12, 14}};
    std::vector<std::string> feathers;
    for (int k = 0; k < 5; k++) feathers.push_back(sym(lancet(bases[k], tips[k], 9.6, k % 2 ? 0.8 : -0.8, 0.32)));
    feathers.push_back(sym(brush({35, 40}, {29, 30}, {22, 20}, {13, 12}, 7.5, 6.5, 0, 0.25)));
    std::vector<L> v;
    layeredFeathers(v, feathers, 2.2f, false);
    const std::string helm = sym("M50 8C42 12.5 34 21 31 33C29.5 41 30 51 31 60L32 70C33 76 37 81 42 84L46 91H50.5V8Z");
    for (const L& l : std::vector<L>{G::XS(helm, 3), G::F(helm), G::XS(inner(helm, 2.8, 6), 1.4),
                                     G::X(sym("M37 49C37 45.5 41 43.5 45 44.5L47.6 46V70C46 74 42 75 39.5 71C37.5 66 37 58 37 49Z")),
                                     G::XS("M50 11V37M31.5 40C38 37 62 37 68.5 40", 1.8), G::X(star(50, 28.5, 6, 1.7, 4, -90)),
                                     G::XS(sym("M34 64C36 72 40 78 44.5 82"), 1.5)})
      v.push_back(l);
    c.add("race-light-elves-helm", "Крылатый шлем", v);
  }
}

// ---------------------------------------------------------------- сумеречные эльфы
void twilightElves(Catalog& c) {
  c.group("Сумеречные Эльфы", EmblemSection::Races);
  // Солнце и луна: левая половина диска — солнце с острыми лучами, правая — месяц со звёздами; половины разделены просветом.
  {
    std::string rays;
    for (int i = 0; i < 9; i++) {
      const double a = 100 + 20 * i;
      const Pt b0 = polar(50, 50, 31, a - 5.5), b1 = polar(50, 50, 31, a + 5.5), t = polar(50, 50, i % 2 ? 41 : 48, a);
      rays += "M" + ptS(b0) + "L" + ptS(t) + "L" + ptS(b1) + "Z";
    }
    const std::string sun = "M50 21A29 29 0 0 0 50 79Z";
    const std::string moon = "M50 21A29 29 0 0 1 50 79A17 29 0 0 0 50 21Z";
    c.add("race-twilight-elves-sunmoon", "Солнце и луна",
          {G::F(rays + sun + moon + star(86, 30, 7, 1.6, 4, -90) + star(89, 67, 5, 1.3, 4, -90) + star(71, 49, 4, 1.1, 4, -90)),
           G::XS("M50 14V86", 3), G::XS(inner(sun, 3.2, 6), 1.5), G::XS(inner(moon, 2.8, 6), 1.4)});
  }
  // Двуликая маска: эльфийская маска, левая половина светлая с вырезанными чертами и солнцем, правая — контур с залитыми чертами и месяцем.
  {
    const std::string half = "M50 8C34 8 21.5 20 20.5 38C19.5 56 28 75 41.5 88.5L50 96Z";
    const std::string rim = "M50 8C66 8 78.5 20 79.5 38C80.5 56 72 75 58.5 88.5L50 96";
    std::string sun;
    for (int i = 0; i < 8; i++) sun += "M" + pt(35, 24, 4.6, 45 * i - 13) + "L" + pt(35, 24, 8.5, 45 * i) + "L" + pt(35, 24, 4.6, 45 * i + 13) + "Z";
    c.add("race-twilight-elves-mask", "Двуликая маска",
          {G::F(half), G::S(rim, 3.6), G::X(circ(35, 24, 3.4) + sun), G::X(lancet({26, 45}, {44, 44}, 5.6, 1.2)),
           G::XS("M25.5 37.5C31 34 38 34 44 37.5", 2), G::XS("M48 48C47 55 45 60 47 64M39 74C43 75.6 47 75.8 50 75", 2),
           G::F(lancet({56, 44}, {74, 45}, 5.6, 1.2)), G::S("M56 37.5C62 34 69 34 74.5 37.5M50 75C53 75.8 57 75.6 61 74", 2.2),
           G::F(crescent(66, 23, 7.5, 200, 55, 3.4) + goutte({65, 55}, 1.8, -90, 5.5)), G::X(lancet({63.5, 42.6}, {63.5, 47}, 1.6))});
  }
  // Весы равновесия: стройная колонна со звездой, коромысло с завитками, цепи, на чашах — солнце и месяц.
  {
    std::string sunRays;
    for (int i = 0; i < 8; i++)
      sunRays += "M" + pt(15, 46, 7.5, 45 * i - 13) + "L" + pt(15, 46, 12, 45 * i) + "L" + pt(15, 46, 7.5, 45 * i + 13) + "Z";
    const std::string pan = "M2 63H28C28 69.5 22 74.5 15 74.5C8 74.5 2 69.5 2 63Z";
    const std::string beam =
        brush({50, 28}, {36, 25.5}, {22, 26.5}, {10, 30}, 5, 4.2, 2.6, 0.3) + brush({50, 28}, {64, 25.5}, {78, 26.5}, {90, 30}, 5, 4.2, 2.6, 0.3);
    c.add("race-twilight-elves-scales", "Весы равновесия",
          {G::F(rrect(47, 20, 6, 68, 2.4) + "M29 94C31 88 39 85 50 85C61 85 69 88 71 94Z" + rrect(25, 92.5, 50, 4.5, 2.2) +
                lancet({50, 22}, {50, 1}, 7) + ell(50, 52, 5.5, 2.6)),
           G::XS(beam, 2.2), G::F(beam + sym(spiral(9, 33.5, 0.5, 4, 250, 610, 2.4, 2.4, 30))), G::XS(circ(50, 27.5, 5.4), 2),
           G::F(circ(50, 27.5, 5.4)), G::X(circ(50, 27.5, 2)), G::S(sym("M12 33 4 63M14.5 33 26 63"), 1.6), G::XS(sym(pan), 2), G::F(sym(pan)),
           G::XS(sym("M3.5 67H26.5"), 1.4), G::XS(circ(15, 46, 6) + crescent(85, 50, 9, -90, 50, 4.4), 2.4),
           G::F(circ(15, 46, 6) + sunRays + crescent(85, 50, 9, -90, 50, 4.4))});
  }
  // Звезда сумерек: восьмилучевая звезда с гранями над заходящим полосатым солнцем, малые звёзды по сторонам.
  {
    std::string sp, facets;
    for (int i = 0; i < 16; i++) sp += (i ? "L" : "M") + pt(50, 30, i % 4 == 0 ? 28 : i % 2 ? 5.5 : 14, -90 + 22.5 * i);
    sp += "Z";
    for (int i = 0; i < 8; i++) facets += "M50 30L" + pt(50, 30, i % 2 ? 9 : 22, -90 + 45 * i);
    c.add("race-twilight-elves-star", "Звезда сумерек",
          {G::F("M16 87A34 34 0 0 1 84 87Z" + rrect(4, 87.5, 92, 5.5, 2.5)), G::XS("M12 67H88M10 77H90", 2.4), G::XS(sp, 3.6), G::F(sp),
           G::XS(clip(sp, facets, 0.8), 1.4),
           G::F(star(15, 17, 7, 1.6, 4, -90) + star(85, 14, 5.5, 1.3, 4, -90) + star(88, 47, 4, 1, 4, -90) + star(12, 48, 4, 1, 4, -90))});
  }
}

// ---------------------------------------------------------------- сянмаорены
// Строгое лицо панды (лицо в круге с центром (cx, cy) радиуса r): косые пятна-капли вокруг узких глаз, нос, сжатый рот.
std::vector<L> pandaFace(float cx, float cy, float r) {
  const Affine m = Affine::translate(cx, cy) * Affine::scale(r / 30.f) * Affine::translate(-50, -55);
  return moved({G::X(sym(lancet({27.5, 47}, {46.5, 59}, 12.5, 1.4, 0.55))), G::F(sym(lancet({33.5, 51}, {44, 55.5}, 3.6, 0.5))),
                G::X("M43.5 62H56.5L52 68H48Z"), G::XS("M50 68V72.5M41.5 75C45 74 48 73.2 50 72.5C52 73.2 55 74 58.5 75", 1.8)},
               m);
}

void xianmaoren(Catalog& c) {
  c.group("Сянмаорены", EmblemSection::Races);
  // Голова панды: голова воина-монаха анфас — уши, повязка с развевающимися концами, косые пятна, узкие глаза, бакенбарды языками.
  {
    const std::string head = sym("M50 22C38 22 28 27 22 36C18 42 16.5 49 16.5 56C17 75 31 90 50 92.5H50.5V22Z");
    const std::string ears = sym(xf(ell(0, 0, 10.5, 9.5), place(24.5, 25, -30)));
    const std::string tufts = sym(flame({19, 57}, {4, 63}, 7.5, 1.6) + flame({21, 68}, {7, 79}, 7, -1.6) + flame({27, 79}, {17, 91}, 6, 1.6));
    const std::string band = "M18.5 41C28 34.5 72 34.5 81.5 41L80.5 47.5C71 41.5 29 41.5 19.5 47.5Z";
    const std::string tails =
        brush({79, 43}, {86, 38}, {91, 33}, {97, 27}, 5.5, 5, 0, 0.3) + brush({79.5, 45}, {87, 46}, {92, 49}, {98, 46}, 5, 4.6, 0, 0.3);
    std::vector<L> v{G::F(ears + tufts),
                     G::XS(sym(xf(arc(0, 0, 5.5, 200, 340), place(24.5, 25, -30))), 1.6),
                     G::XS(head, 2.6),
                     G::F(head),
                     G::XS(inner(head, 3, 6), 1.4),
                     G::XS(band, 2.2),
                     G::F(band + tails),
                     G::XS(clip(band, "M20 44.2C30 38.2 70 38.2 80 44.2", 0.6), 1.3)};
    for (const L& l : pandaFace(50, 61, 31)) v.push_back(l);
    c.add("race-xianmaoren-head", "Голова панды", v);
  }
  // Панда-монах: панда в рясе сидит в позе лотоса, руки в мудре, на груди чётки; за спиной — посох-кхаккхара с кольцами.
  {
    const std::string robe = "M50 44C37 44 29 53 27.5 65C26.5 73 28 78 31 81H69C72 78 73.5 73 72.5 65C71 53 63 44 50 44Z" + ell(50, 85, 34, 10);
    const std::string head = circ(50, 29, 15.5) + sym(xf(ell(0, 0, 6.4, 5.8), place(37.5, 16, -30)));
    // Посох монаха: древко, сверху — лезвие-полумесяц, снизу — лопатка.
    const std::string staff = "M20.5 91.8 80.5 15.5 82.5 17.2 23 92.2 19 97Z" + crescent(84, 11, 8, -38, 52, 4);
    std::vector<L> v{G::F(staff), G::XS(robe + head, 3.2), G::F(robe + head)};
    for (const L& l : pandaFace(50, 31, 15.5)) v.push_back(l);
    for (const L& l : std::vector<L>{G::XS(clip(robe, sym("M35 54C31 62 32 71 39 77M27 86C35 82 44 82 50 84"), 1.5), 1.8),
                                     G::XS(clip(robe, "M50 47C46 52 44 58 43 66M50 47C54 52 56 58 57 66", 1.5), 1.6),
                                     G::XS(lancet({43, 78}, {57, 78}, 9), 2), G::F(lancet({43, 78}, {57, 78}, 9)), G::XS("M45 78H55", 1.3)})
      v.push_back(l);
    std::string beads;
    for (int i = 0; i < 9; i++)
      beads += circ(50 + 13 * std::cos((25 + 16.25 * i) * kPi / 180), 46 + 8.5 * std::sin((25 + 16.25 * i) * kPi / 180), 1.6);
    v.push_back(G::X(beads));
    c.add("race-xianmaoren-monk", "Панда-монах", v);
  }
  // Бамбук: три стебля с коленцами-перехватами, листья-ланцеты пучками, как в живописи тушью.
  {
    struct Stalk {
      double x, top, w;
      std::vector<double> nodes;
    };
    const Stalk st[] = {{33, 5, 7, {28, 54, 78}}, {51, 17, 7.5, {40, 64, 86}}, {69, 31, 6.5, {50, 72}}};
    std::string stalks, gaps, ridges;
    for (const Stalk& s : st) {
      stalks +=
          "M" + n(s.x - s.w / 2 - 0.6) + " 97L" + n(s.x - s.w / 2) + " " + n(s.top) + "H" + n(s.x + s.w / 2) + "L" + n(s.x + s.w / 2 + 0.6) + " 97Z";
      for (double y : s.nodes) {
        gaps += "M" + n(s.x - s.w) + " " + n(y) + "H" + n(s.x + s.w);
        ridges += "M" + n(s.x - s.w / 2 - 1.2) + " " + n(y + 2.2) + "H" + n(s.x + s.w / 2 + 1.2);
      }
    }
    std::vector<std::string> leaves;
    const struct {
      double x, y, a, len;
    } lv[] = {{36.5, 28, -22, 23}, {36.5, 28, 10, 25}, {36.5, 28, 38, 19}, {29.5, 54, 202, 23}, {29.5, 54, 172, 25},
              {29.5, 54, 146, 18}, {55, 40, -14, 23},  {55, 40, 18, 21},   {47, 64, 196, 21},   {47, 64, 164, 19},
              {72.5, 50, -10, 21}, {72.5, 50, 22, 23}, {72.5, 50, 50, 17}, {65.5, 72, 192, 19}, {33, 12, 205, 17}};
    for (const auto& l : lv) leaves.push_back(lancet({float(l.x), float(l.y)}, polar(l.x, l.y, l.len, l.a), 6.4, l.a > 90 ? 1 : -1, 0.3));
    std::vector<L> v{G::F(stalks), G::XS(gaps, 1.6), G::S(ridges, 1.4),
                     G::XS(clip(stalks, "M31.5 31V51M31 57V75M49.5 43V61M49 67V83M67.5 53V69", 1.4), 1.3)};
    layeredFeathers(v, leaves, 1.8f, false);
    c.add("race-xianmaoren-bamboo", "Бамбук", v);
  }
  // Инь-ян с драконом: круг инь-ян, вокруг — змеевидный дракон: голова наверху, гребень, рога назад, усы, тело обвивает круг до хвоста-пламени.
  {
    const double cx = 50, cy = 52, R = 34;
    const std::string body = taperArc(cx, cy, R, -104, -398, 12, 3, 60);
    std::string spines, scales;
    for (double a = -122; a >= -380; a -= 14.5) {
      const double k = (a + 104) / -294, h = 7.5 - 4 * k;
      spines +=
          "M" + pt(cx, cy, R + 4.6 - 3 * k, a + 4) + "L" + pt(cx, cy, R + 4.6 - 3 * k + h, a - 3) + "L" + pt(cx, cy, R + 4.6 - 3 * k, a - 5) + "Z";
      scales += "M" + pt(cx, cy, R - 4.5 + 3 * k, a + 4) + "Q" + pt(cx, cy, R, a - 2) + " " + pt(cx, cy, R + 4.5 - 3 * k, a + 4);
    }
    // Голова в местных координатах мордой вдоль +x: череп с длинной мордой, раскрытая пасть, рога назад, грива, ус.
    const Affine hm = place(45, 17.5, -4, 1.18f);
    const std::string head = xf("M-7-6C0-9.5 8-8.5 13-5.5L20.5-3.5C21.5-1.5 20.5 0 18.5 0H9C5 2-2 4-7 3Z" +
                                    std::string("M-1 3.5C5 4.5 10 5.5 16 6.5L17 8.5C11 9.5 4 9.5-3 7.5Z"),
                                hm);
    const std::string horns = xf(
        brush({-4, -6}, {-9, -9}, {-14, -11}, {-21, -10}, 3.6, 3.2, 0, 0.3) + brush({1, -7}, {-3, -11}, {-7, -13.5}, {-13, -14}, 3, 2.8, 0, 0.3), hm);
    const std::string mane =
        xf(flame({-6, 0}, {-18, 3}, 4.6, 1) + flame({-5, 4}, {-14, 10}, 4, -1) + brush({19, -1}, {25, 3}, {27, 8}, {32, 9}, 2.4, 2.2, 0, 0.3), hm);
    const std::string tail =
        flame(polar(cx, cy, R, -398), polar(cx, cy, R + 11, -372), 6, -1.6) + flame(polar(cx, cy, R - 2, -396), polar(cx, cy, R - 12, -374), 5, 1.4);
    const char* foot = "M-2.6 0H2.6L3.4 5 7 6.4 3 7 2.4 10.5 0 7.4-2.4 10.5-3 7-7 6.4-3.4 5Z";
    const std::string feet = xf(foot, place(polar(cx, cy, R - 5, -168).x, polar(cx, cy, R - 5, -168).y, -168 + 90 - 180)) +
                             xf(foot, place(polar(cx, cy, R - 5, -288).x, polar(cx, cy, R - 5, -288).y, -288 + 90 - 180));
    c.add("race-xianmaoren-yinyang", "Инь-ян с драконом",
          {G::F(body + spines + feet + tail), G::XS(scales, 1.3), G::F(horns + mane), G::XS(head, 2.4), G::F(head),
           G::X(xf(lancet({2, -4.5}, {7.5, -4}, 2.4), hm)), G::XS(circ(cx, cy, 23), 2.6), G::F(circ(cx, cy, 23)),
           G::X("M50 29A23 23 0 0 1 50 75A11.5 11.5 0 0 1 50 52A11.5 11.5 0 0 0 50 29Z"), G::F(circ(50, 63.5, 3.8)), G::X(circ(50, 40.5, 3.8))});
  }
}

// ---------------------------------------------------------------- тёмные эльфы
void darkElves(Catalog& c) {
  c.group("Темные Эльфы", EmblemSection::Races);
  // Ручной арбалет (вид сверху): суставчатые дуги, как паучьи лапы, с загнутыми концами, тетива, болт с зазубренным жалом, ложе с рукоятью-шипом.
  {
    const Pt a0{46, 30}, a1{36, 22.5}, a2{20, 23.5}, a3{9.5, 36};
    const std::string arms = sym(brush(a0, a1, a2, a3, 13, 10, 4.6, 0.2) + brush({10, 35}, {5.5, 40}, {6, 46.5}, {12, 51}, 5, 4.6, 0, 0.3));
    std::string joints;
    for (double t : {0.32, 0.62}) {
      const CurvePt q = bezier(a0, a1, a2, a3, t);
      joints += sym("M" + ptS({float(q.x + q.nx * 6), float(q.y + q.ny * 6)}) + "L" + ptS({float(q.x - q.nx * 6), float(q.y - q.ny * 6)}));
    }
    const std::string stock = "M42.5 22H57.5L58.6 60C58.6 64.5 56.6 67 55.4 69L59.5 89 50 98.5 40.5 89 44.6 69C43.4 67 41.4 64.5 41.4 60Z";
    const std::string bolt = rrect(48.4, 9, 3.2, 50, 1.2) + "M50 0.5 58 13.5 53.2 12 51.6 16H48.4L46.8 12 42 13.5Z" +
                             sym(lancet({48.6, 50}, {43.5, 58}, 3.4, 0.6) + lancet({48.6, 44}, {43.5, 52}, 3.4, 0.6));
    c.add("race-dark-elves-crossbow", "Ручной арбалет",
          {G::S("M11.5 48 50 63 88.5 48", 1.6), G::F(arms), G::XS(joints, 1.5), G::XS(stock, 2.4), G::F(stock), G::X("M50 71 52.6 76 50 81 47.4 76Z"),
           G::XS(bolt, 2.2), G::F(bolt), G::XS(circ(50, 63, 3.6), 1.8), G::F(circ(50, 63, 3.6))});
  }
  // Шлем с паучьими глазами: стрельчатый шлем стража Подземья, гребень из суставчатых паучьих лап, восемь глазниц-щелей, дыхательные прорези.
  {
    const std::string helm = sym("M50 3C41 13 32 25 30 40L29 70C29 80 35.5 88 43 92L50 96.5H50.5V3Z");
    std::string legs;
    const struct {
      Pt b, k, t;
    } lg[] = {{{32, 30}, {19, 14}, {8, 20}}, {{30, 41}, {14, 32}, {4, 40}}, {{29.5, 53}, {13, 51}, {5, 62}}, {{30, 65}, {17, 70}, {12, 82}}};
    for (const auto& l : lg)
      legs += sym(brush(l.b, lerp(l.b, l.k, 0.33), lerp(l.b, l.k, 0.66), l.k, 4.2, 3.8, 3, 0.4) +
                  brush(l.k, lerp(l.k, l.t, 0.33), across(l.k, l.t, 0.66, -1.2), l.t, 3, 2.8, 0, 0.3));
    c.add("race-dark-elves-helm", "Шлем с паучьими глазами",
          {G::F(legs), G::XS(helm, 2.8), G::F(helm), G::XS(inner(helm, 2.8, 6), 1.4),
           G::X(sym(lancet({39, 50}, {47.6, 53}, 5, -0.6) + lancet({36, 43}, {42.5, 45}, 3.6, -0.4) + lancet({35.5, 36.5}, {40.5, 37.6}, 2.6, -0.3)) +
                lancet({47, 41.5}, {53, 41.5}, 2.6) + lancet({46.6, 34}, {53.4, 34}, 2.4)),
           G::XS("M50 8V27M31.5 31.5C40 28 60 28 68.5 31.5", 1.6), G::XS("M42 70V82M46 72V86M50 72V87M54 72V86M58 70V82", 1.6)});
  }
  // Грибы Подземья: три стройных гриба-светоча с колокольными шляпками, пластинки и кольца на ножках, парящие споры-искры.
  {
    const std::string side = "M8 58C8 48 14 41 24 39C34 41 40 48 40 58C35 56 29 55.5 24 55.5C19 55.5 13 56 8 58Z" +
                             std::string("M22 56C22.5 68 22 80 20.5 92H28C26.5 80 26 68 26.5 56Z") +
                             "M60 52C60 41 67 33 78 31C89 33 96 41 96 52C90 50 84 49.5 78 49.5C72 49.5 66 50 60 52Z" +
                             "M76 50C76.5 66 76 80 75 92H82.5C81.5 80 81 66 81.5 50Z";
    const std::string mid = "M27 37C27 22 37 11 50 8C63 11 73 22 73 37C66 34 58 33 50 33C42 33 34 34 27 37Z" +
                            std::string("M45.5 34C46 52 45 72 43 92H57C55 72 54 52 54.5 34Z");
    const std::string rings = ell(50, 48, 7.5, 2.6) + ell(24, 66, 4.5, 1.8) + ell(78.8, 62, 4.8, 1.8);
    c.add(
        "race-dark-elves-mushrooms", "Грибы Подземья",
        {G::F(side + rings),
         G::XS(clip(side, "M24 43V54M17 46C15 49 14 52 14 56M31 46C33 49 34 52 34 56M78 35V48M71 38C69 41 68 44.5 68 49M85 38C87 41 88 44.5 88 49",
                    0.6),
               1.4),
         G::XS(mid, 2.8), G::F(mid),
         G::XS(clip(mid, "M50 13V31M42 16C38.5 21 37 26 37 32M58 16C61.5 21 63 26 63 32M34 22C31 26 30 29 30 33M66 22C69 26 70 29 70 33", 0.6), 1.4),
         G::XS(rings, 1.6), G::F(rings), G::XS(clip(mid, "M47.5 54C48 66 47.5 78 46 88", 1.2), 1.4),
         G::F(star(14, 30, 4.5, 1.1, 4, -90) + star(30, 16, 3.5, 1, 4, -90) + star(87, 17, 4.5, 1.1, 4, -90) + star(71, 6, 3.6, 1, 4, -90) +
              star(7, 46, 3, 0.9, 4, -90) + rrect(3, 91, 94, 5, 2.5))});
  }
  // Маска-вуаль: полумаска с острыми загнутыми крыльями и миндалевидными прорезями, камень на переносице, кружевная вуаль с бусинами-каплями.
  {
    const std::string mask = sym("M50 26C44 24 36 22 26 23C17 24 10 20 3.5 12C5 25 10 35 18 41C25 46 34 48 41 46C45 45 48 43 50 41.5Z");
    const std::string veil =
        "M17 45C29 51 40 53 50 53C60 53 71 51 83 45L86 79C81 84 76 80 72 84C68 88 63 84 59 88C55 92 45 92 41 88C37 84 32 88 28 84"
        "C24 80 19 84 14 79Z";
    std::string beads;
    for (const Pt& p : {Pt{14, 82}, Pt{28, 87.5}, Pt{41, 91}, Pt{59, 91}, Pt{72, 87.5}, Pt{86, 82}})
      beads += goutte({p.x, p.y + 4.6f}, 2.3, -90, 6.5);
    c.add("race-dark-elves-veil", "Маска-вуаль",
          moved({G::F(veil + beads),
                 G::XS(clip(veil, "M30 54C29 66 30 75 32 84M42 55C41 68 42 79 44 88M58 55C59 68 58 79 56 88M70 54C71 66 70 75 68 84", 1.2), 1.5),
                 G::XS(mask, 3.2), G::F(mask), G::X(sym(lancet({27, 32.5}, {44, 37}, 7.5, -1.6))),
                 G::XS(sym("M9 19C15 26 21 28 27 28M14 35C20 40 27 42 33 42.5"), 1.5), G::X("M50 27.5 53 32.5 50 37.5 47 32.5Z"),
                 G::XS(sym("M45 30C46.5 33 48 35 49 36"), 1.3)},
                Affine::translate(0, -4)));
  }
}

// ---------------------------------------------------------------- тролли
void trolls(Catalog& c) {
  c.group("Тролли", EmblemSection::Races);
  // Голова тролля: вытянутое лицо, гребень-ирокез языками, длинные заострённые уши, крючковатый нос, клыки-полумесяцы, серьги.
  {
    const std::string face = sym("M50 25C43 25 38.5 28 37 33L35 41C34 49 34.5 57 36 63.5C38.5 73.5 43 81.5 50 88H50.5V25Z" +
                                 std::string("M36 41.5C28 36 17 28 3 23C8 34 18 45 35 53Z"));
    const std::string crest =
        lancet({50, 28}, {50, 1}, 9, 0, 0.3) + sym(lancet({45.5, 28}, {38, 5}, 7.5, 1.2, 0.3) + lancet({41.5, 30}, {30.5, 12}, 6, 1.4, 0.3));
    const std::string tusk = brush({41.5, 75.5}, {34, 77}, {27.5, 71}, {25, 57}, 7.4, 7.4, 0, 0.2);
    c.add("race-trolls-head", "Голова тролля",
          moved({G::F(crest), G::XS(face, 2.6), G::F(face), G::XS(inner(face, 3, 6), 1.4), G::XS(sym("M31.5 43C25 39.5 18.5 35.5 12 32"), 1.5),
                 G::XS(sym("M36.5 40.5 47.5 45M47 47C46 54 44.5 59 45 63C46.5 65.5 48.5 65.5 50 64.5M37 56.5 42.5 58.5M37.5 61.5 42.5 63"), 2),
                 G::X(sym(lancet({38, 45.6}, {46.6, 48.2}, 3.2, 0.5))), G::XS("M40 72C45 70.2 55 70.2 60 72", 2), G::XS(sym(tusk), 2.4),
                 G::F(sym(tusk)), G::S(sym(circ(13, 40.5, 3)), 1.8)},
                Affine::translate(0, 5)));
  }
  // Вуду-кукла: тряпичная кукла со швами, глаза-крестики, зашитый рот, пучки соломы на макушке, длинные булавки с гранёными головками.
  {
    const std::string doll = "M50 12C59 12 66 19 66 28C66 36 61 42 55 44H45C39 42 34 36 34 28C34 19 41 12 50 12Z" +
                             std::string(
                                 "M38 46C42 44 58 44 62 46L66 72C64 76 58 78 55 77L57 92C57 95 54 96 51 95L50 86 49 95C46 96 43 95 43 92L45 77"
                                 "C42 78 36 76 34 72Z") +
                             sym(brush({40, 51}, {30, 52}, {20, 50}, {10, 44}, 9.5, 9, 7.5, 0.5));
    const std::string straw = lancet({50, 13}, {50, 2}, 4.6) + sym(lancet({45.5, 14}, {38, 4}, 4.2, 0.6) + lancet({41, 16}, {31, 9}, 3.8, 0.6));
    std::string pins, heads;
    const struct {
      Pt a, b;
    } pn[] = {{{24, 9}, {42, 25}}, {{82, 64}, {59, 56}}, {{74, 93}, {55, 77}}};
    for (const auto& p : pn) {
      pins += "M" + ptS(p.a) + "L" + ptS(p.b);
      heads += lancet(across(p.a, p.b, -0.16, 0), across(p.a, p.b, 0.12, 0), 6.4);
    }
    c.add("race-trolls-doll", "Вуду-кукла",
          {G::F(straw), G::XS(doll, 2.6), G::F(doll), G::XS(clip(doll, "M50 47V84M43 52H57M43 60H57M43 68H57M43 76H57", 1.6), 1.5),
           G::XS(sym("M18 45 20 54M27 47 28 56"), 1.4), G::XS("M38 22 46 30M46 22 38 30M54 22 62 30M62 22 54 30", 2.2),
           G::XS("M40 37H60M43.5 34.5 44.5 39.5M48.5 34.5 49.5 39.5M52.5 34.5 51.5 39.5M56.5 34.5 55.5 39.5", 1.5), G::S(pins, 1.8),
           G::XS(heads, 1.8), G::F(heads)});
  }
  // Каменный тролль: сутулый гранёный великан — малая голова с тяжёлым лбом и клыками, глыбы плеч, длинные руки с кулаками, грани и трещины.
  {
    const std::string body = "M16 45 24 30 38 24H62L76 30 84 45 85 62 79 69 73 66 71 76H29L27 66 21 69 15 62Z" +
                             std::string("M33 74 26 95H43L47 80ZM67 74 74 95H57L53 80Z");
    const std::string arms = sym("M22 38 15 50 10 66 9 76 19 77 20 66 25 53 29 45Z");
    const std::string fists = sym("M3 75 9 70H20L24 76 22 87 16 91H8L3 86Z");
    const std::string head = "M50 11 59 14 62 22 61 33 55 40H45L39 33 38 22 41 14Z";
    c.add("race-trolls-stone", "Каменный тролль",
          {G::F(body), G::XS(arms + fists, 2.6), G::F(arms + fists), G::XS(head, 2.6), G::F(head),
           G::XS(clip(body + arms + fists,
                      "M38 24 42 36 34 48 38 60M62 24 58 36 66 48 62 60M42 36H58L62 48 50 56 38 48M50 56V70M15 50 22 58M85 50 78 58M9 70 15 76 20 "
                      "70M91 70 85 76 80 70M38 82 35 92M62 82 65 92",
                      1.2),
                 1.6),
           G::X(sym(lancet({42, 24.5}, {48, 26}, 2.6, 0.3))), G::XS("M39.5 21 50 23.5 60.5 21M47 27 46 31 50 32.5 54 31 53 27", 1.6),
           G::X("M43 34.5H57L55 38.5H45Z"), G::F(sym("M44.2 35 45.5 30.5 47 35Z"))});
  }
  // Вуду-маска: вытянутая резная маска с венцом из острых перьев, зигзаг на лбу, щели глаз под тяжёлыми бровями, длинный нос, оскал с клыками.
  {
    const std::string mask =
        "M50 14C65 14 74.5 24 75 39C75.5 56 71 70 64 82C59.5 90 54.5 95.5 50 97C45.5 95.5 40.5 90 36 82C29 70 24.5 56 25 39C25.5 24 35 14 50 14Z";
    std::vector<std::string> feathers;
    for (int i = 0; i < 7; i++) {
      const double a = -90 + (i - 3) * 19;
      feathers.push_back(lancet(polar(50, 32, 12, a), polar(50, 32, i == 3 ? 31.5 : 29 - std::abs(i - 3) * 1.5, a), 8, 0, 0.4));
    }
    std::vector<L> v;
    layeredFeathers(v, feathers, 2, false);
    for (const L& l :
         std::vector<L>{G::XS(mask, 2.6), G::F(mask), G::XS(clip(mask, "M27 31 32.5 26 38 31 44 26 50 31 56 26 62 31 67.5 26 73 31", 0.8), 1.8),
                        G::XS(sym("M32 39C36 36 42 36.5 47 40"), 2.2), G::X(sym(lancet({33.5, 44.5}, {46, 45.5}, 4, 0.8))),
                        G::XS("M46.5 44V64C46.5 66.5 53.5 66.5 53.5 64V44", 1.8), G::XS(sym("M32 52 39 56.5M33.5 59 39.5 62.5"), 1.8),
                        G::X("M38 71.5C44 74.5 56 74.5 62 71.5C60 80 55 84.5 50 84.5C45 84.5 40 80 38 71.5Z"),
                        G::F(sym("M41.5 72.6 43.5 79 45.5 73.4ZM47.4 73.8 48.8 77.8 50.2 74Z")),
                        G::F(sym(brush({41, 81}, {35, 81}, {31.5, 75}, {31, 66}, 4.6, 4.6, 0, 0.25))), G::XS("M44 90.5 50 87.5 56 90.5", 1.6)})
      v.push_back(l);
    c.add("race-trolls-mask", "Вуду-маска", v);
  }
}

// ---------------------------------------------------------------- троглодиты
void troglodytes(Catalog& c) {
  c.group("Троглодиты", EmblemSection::Races);
  // Троглодит с дубиной: безглазый ящер в профиль, вставший на задние лапы, гребень-веер за головой и шипы по хребту, хвост дугой,
  // дубина занесена над головой.
  {
    const std::string head = "M41 20C34 15 23 14.5 14 17.5L3.5 22.5 12.5 26C11 28.5 12.5 31 15.5 31H30C35 31 39 29 42.5 27Z";
    const std::string torso =
        "M37.5 24C45.5 19.5 56 21.5 61.5 29.5C67.5 39.5 69.5 52.5 66.5 62.5C63.5 70 56 73 48 72C43 62 39 50 36 40C35 34 35 28 37.5 24Z";
    const std::string tail = brush({62, 59}, {80, 61}, {90, 74}, {97, 91}, 13, 12, 0, 0.15);
    const std::string legs = brush({59, 61}, {65.5, 67}, {68, 73}, {69, 79.5}, 12.5, 11, 8, 0.3) +
                             brush({69, 78.5}, {68, 84.5}, {65, 88.5}, {60.5, 91.5}, 8, 7, 6, 0.5) +
                             brush({48, 67}, {46, 73}, {43, 77.5}, {40, 81.5}, 11, 10, 7, 0.3) +
                             brush({40.5, 80.5}, {38.5, 85.5}, {36, 88.5}, {32.5, 91.5}, 7, 6.5, 5.5, 0.5) + lancet({61, 91}, {50, 93}, 4) +
                             lancet({61, 91}, {56, 99}, 3.8) + lancet({33, 91.5}, {22, 93.5}, 4) + lancet({33, 91.5}, {28, 99}, 3.8);
    const std::string arms = brush({52.5, 31}, {57, 26}, {61, 21}, {64.5, 16.5}, 8.4, 7.6, 6.4, 0.4) +
                             brush({42, 36}, {36, 42}, {30, 46}, {24, 48}, 8, 7.4, 5.8, 0.4) + lancet({24.5, 48}, {14, 43.5}, 3.6) +
                             lancet({24.5, 48}, {15.5, 54}, 3.6);
    const std::string club = brush({57, 26}, {65, 18}, {73, 12}, {81.5, 7.5}, 5.4, 8, 12, 0.9) + circ(83.5, 7, 6) + spike({88.5, 3.5}, -40, 5.5, 2) +
                             spike({84, 1.5}, -95, 5, 2) + spike({89.5, 10}, 15, 5, 2);
    const std::string frill = lancet({36, 20}, {29, 3}, 5.4, -1) + lancet({38.5, 20.5}, {37.5, 2.5}, 5.4, 0.6) + lancet({41, 22}, {46, 6.5}, 5, 1.2);
    std::string spikes;
    for (int i = 0; i < 6; i++) {
      const double t = 0.22 + i * 0.13;
      const CurvePt q = bezier({44, 21}, {56, 20}, {66, 34}, {68, 56}, t);
      spikes += spike({float(q.x - q.nx * 1.5), float(q.y - q.ny * 1.5)}, std::atan2(-q.ny, -q.nx) * 180 / kPi, 7.5 - i * 0.5, 2.4);
    }
    c.add("race-troglodytes-brute", "Троглодит с дубиной",
          moved({G::F(frill + club), G::XS(clip(club, "M66 16 72 11.5M69.5 19.5 75 14.5", 0.4), 1.4), G::XS(arms, 2.2),
                 G::F(spikes + tail + legs + torso + head + arms), G::XS("M7 23.5 29 27.5", 1.6), G::XS("M14 18.5C19 17 26 17 31 18.5", 1.5),
                 G::F("M12 25.5 14 29 15.5 25.8ZM19 26.4 20.6 29.4 22 26.8Z"),
                 G::XS(clip(torso, "M41 40C48 48 52 58 52 70M45 33 53 36M48 40 56 43M51 47 59 50M53 54 61 57M54 61 61 63", 1.5), 1.4),
                 G::XS(clip(tail, "M70 63C77 67 81 74 85 81", 1.8), 1.3)},
                scaleAt(0.955f, 50, 50) * Affine::translate(-0.5, 1.8)));
  }
  // Пещера: скала с пластами породы, тёмный стрельчатый зев с клыками сталактитов и сталагмитов, трещины.
  {
    const std::string rock = "M3 92C3 78 7 66 14 57C18 46 26 36 36 31C41 24 50 20 58 23C68 23 78 31 82 41C90 49 96 62 97 76V92Z";
    const std::string mouth = "M24 92C24 77 29 63 38 55C44 50 56 50 62 55C71 63 76 77 76 92Z";
    c.add("race-troglodytes-cave", "Пещера",
          moved({G::F(rock), G::XS(inner(rock, 4, 6), 1.5), G::XS(inner(rock, 9, 6), 1.4), G::XS(mouth, 3), G::X(mouth),
                 G::F("M38.5 56 41.5 70 44.5 52.5ZM47 51 50 67 53 51ZM55.5 52.5 58.5 66 61.5 56ZM30 67 33 77 34.5 63ZM66.5 63 68 74 71 68Z" +
                      std::string("M30 92 33.5 80 37 92ZM42 92 45.5 76 49 92ZM53 92 56.5 82 60 92ZM64 92 66.5 85 69 92Z")),
                 G::XS("M58 23C56 29 60 33 58 39M36 31C38 37 36 43 40 47", 1.8), G::F(rrect(1, 91, 98, 5, 2.5))},
                Affine::translate(0, -5.5)));
  }
  // Слепое око: медальон из рядов чешуи, в нём миндалевидный глаз с веками, мутное глазное яблоко без зрачка, шрам со стежками.
  {
    std::string scales;
    for (int row = 0; row < 12; row++) {
      const double y = 7 + row * 7.6;
      for (int i = -1; i < 12; i++) {
        const double x = 4 + i * 9 + (row % 2 ? 4.5 : 0);
        scales += arc(x, y, 4.2, 20, 160);
      }
    }
    const std::string lids = "M10 52C20 31 36 22 50 22C64 22 80 31 90 52C80 72 64 80 50 80C36 80 20 72 10 52Z";
    const std::string ball = "M19 52C27 37 39 30.5 50 30.5C61 30.5 73 37 81 52C73 66 61 72 50 72C39 72 27 66 19 52Z";
    c.add("race-troglodytes-eye", "Слепое око",
          {G::F(circ(50, 51, 45)), G::XS(clip(circ(50, 51, 45), scales, 2.4), 1.5), G::XS(circ(50, 51, 42.4), 1.5), G::X(lids), G::F(ball),
           G::XS(inner(ball, 4, 6), 1.4), G::XS("M22 33 78 73", 4.4), G::S("M24 34.5 76 71.5", 1.8),
           G::XS("M30 32.5 25.5 41.5M40 39.5 35 47.5M50 46.5 45 54.5M60 53.5 55 61.5M70 60.5 65 68.5", 1.5)});
  }
  // Каменные копья: скрещённые копья с кремнёвыми листовидными наконечниками в сколах, обмотки и ожерелье из клыков между ними.
  {
    std::string head = "M50 1";
    for (int i = 1; i <= 7; i++) {
      const double y = 1 + i * 5.2, w = 7.6 * std::sin(kPi * std::min(1.0, (y - 1) / 40.0)) + 0.6;
      head += "L" + n(50 - w - (i % 2 ? 2 : 0)) + " " + n(y);
    }
    head += "L45.5 41H54.5";
    for (int i = 7; i >= 1; i--) {
      const double y = 1 + i * 5.2, w = 7.6 * std::sin(kPi * std::min(1.0, (y - 1) / 40.0)) + 0.6;
      head += "L" + n(50 + w + (i % 2 ? 0 : 2)) + " " + n(y);
    }
    head += "Z";
    const std::string spear = head + rrect(46.2, 39, 7.6, 58, 3.4);
    std::vector<L> v = crossed(spear, "", "M50 7V34M46 44 54 47M46 49 54 52", 1.5f, 30, 50, 52, 1.0f);
    std::string fangs;
    for (int i = 0; i < 5; i++) {
      const double a = 120 - i * 15;
      const Pt p = polar(50, 46, 15, a);
      fangs += brush(p, polar(p.x, p.y, 3, a), polar(p.x, p.y, 6, a + 4), polar(p.x, p.y, 10.5 - std::abs(i - 2), a + 10), 4, 3.6, 0, 0.25);
    }
    v.push_back(G::S(arc(50, 46, 15, 40, 140), 1.8));
    v.push_back(G::XS(fangs, 1.8));
    v.push_back(G::F(fangs));
    c.add("race-troglodytes-spears", "Каменные копья", v);
  }
}

// ---------------------------------------------------------------- циклопы
// Борода из S-образных прядей: контур от подбородка вниз, острые концы прядей.
std::string beardShape() { return sym("M31 60C28.5 70 30 81 37 89.5L41.5 95 43.5 88.5 47 96.5 50.5 99V60Z"); }

// Голова циклопа анфас (сетка 100 × 100): кудри-улитки, глаз под аркой брови, нос, усы, борода прядями.
std::vector<L> cyclopsHead() {
  const std::string hair = sym("M50 6C35 6 22.5 14.5 19.5 29C18.5 35.5 19.5 42 22 47C24 37 30.5 29.5 40 27C44 26 47.5 26 50.5 26V6Z");
  const std::string face = sym("M50 23.5C38 23.5 29 31.5 28 44C27.5 52 28.5 58 31 63.5H50.5V23.5Z");
  std::string curls;
  for (const Pt& p : {Pt{26, 25}, Pt{34, 16}, Pt{44, 11.5}, Pt{56, 11.5}, Pt{66, 16}, Pt{74, 25}, Pt{23, 37}, Pt{77, 37}})
    curls += spiral(p.x, p.y, 0.6, 4.4, -90, 250, 1.4, 1.6, 26);
  const std::string beard = beardShape();
  const std::string moustache = sym(brush({50, 66.5}, {44, 64}, {37.5, 65.5}, {32, 72}, 5, 4.6, 0, 0.3));
  return {G::F(hair + sym(ell(25.5, 48, 4.6, 7))),
          G::X(curls),
          G::XS(face + beard, 2.4),
          G::F(face + beard),
          G::XS(clip(beard, sym("M34 70C33 78 36 85 41 91M39.5 71C39 79 41.5 86 44.5 92M45 72C45 80 46.5 87 48.5 93"), 1.4), 1.5),
          G::XS(moustache, 2.2),
          G::F(moustache),
          G::X(lancet({33, 46}, {67, 46}, 13.5)),
          G::F(circ(50, 46, 5.8)),
          G::X(circ(50, 46, 2.3)),
          G::XS("M29.5 37C37.5 30.5 45.5 32.5 50 36C54.5 32.5 62.5 30.5 70.5 37", 2.6),
          G::XS(sym("M47.5 53C47 57 46 59.5 46.5 62C48 63.5 49.5 63.5 50.5 63"), 1.8),
          G::X(lancet({44, 73.5}, {56, 73.5}, 3))};
}

void cyclopes(Catalog& c) {
  c.group("Циклопы", EmblemSection::Races);
  // Голова циклопа: как архаическая статуя — шапка кудрей-улиток, единственный миндалевидный глаз под аркой брови, прямой нос, усы и борода прядями.
  c.add("race-cyclopes-head", "Голова циклопа", cyclopsHead());
  // Циклоп-кузнец: могучий бюст в кожаном фартуке, голова с кудрями и бородой, рука с молотом занесена над плечом.
  {
    const Affine hp = place(76, 37, 8);
    const std::string hammer = xf(rrect(-3, -24, 6, 38, 2.5) + "M-11-34H7L12-29 7-24H-11Z" + "M7-32.5 15-29 7-25.5Z", hp);
    const std::string bust = "M5 98C5 81 14 69 29 64.5L40 61H60L71 64.5C86 69 95 81 95 98Z";
    const std::string arm = brush({70, 74}, {80, 62}, {84, 50}, {80, 38}, 16, 14, 12, 0.4) + ell(78.5, 35, 7.5, 7);
    std::vector<L> v{G::F(hammer),
                     G::XS(xf("M-11-29H7", hp), 1.5),
                     G::F(bust),
                     G::XS(inner(bust, 3.2, 6), 1.5),
                     G::XS("M37 64 44.5 98M63 64 55.5 98", 2.2),
                     G::XS(clip(bust, "M17 81C21 77 25 77 29 79", 2), 1.5)};
    const Affine hm = Affine::translate(47, 35) * Affine::scale(0.52f) * Affine::translate(-50, -52);
    for (const L& l :
         moved({G::XS(sym("M50 6C35 6 22.5 14.5 19.5 29C18 40 22 56 31 63.5L37 90 50 99 63 90 69 63.5C78 56 82 40 80.5 29C77.5 14.5 65 6 50 6Z"), 5)},
               hm))
      v.push_back(l);
    for (const L& l : moved(cyclopsHead(), hm)) v.push_back(l);
    for (const L& l : std::vector<L>{G::XS(arm, 2.6), G::F(arm), G::XS(clip(arm, "M76 66C80 58 82 50 80.5 42", 2.5), 1.5)}) v.push_back(l);
    c.add("race-cyclopes-smith", "Циклоп-кузнец", v);
  }
  // Наковальня с молнией: перун-зигзаг с острыми концами бьёт в наковальню с рогом, во все стороны разлетаются искры.
  {
    const std::string anvil = "M4 53C13 57.5 21.5 59.5 30 59.5H94V69.5H74L67 74V82L77.5 92H22.5L33 82V74L28 71C17 68 9 62 4 53Z";
    const std::string bolt = "M61 1 41.5 31H51.5L42 55 66.5 23.5H55.5L66 1Z";
    std::string sparks;
    for (int i = 0; i < 7; i++) {
      const double a = -168 + i * 26;
      if (i == 3) continue;
      sparks += spike(polar(45, 57, 8, a), a, 9 + (i % 2) * 3, 1.6);
    }
    c.add("race-cyclopes-anvil", "Наковальня с молнией",
          {G::F(sparks), G::XS(anvil, 2.6), G::F(anvil), G::XS("M30 64.5H93M37 79H63", 1.6), G::XS(bolt, 3.2), G::F(bolt),
           G::F(star(84, 13, 7, 1.6, 4, -90) + star(19, 26, 5, 1.3, 4, -90) + star(88, 44, 4, 1.1, 4, -90))});
  }
  // Летящий валун: гранёная глыба с рёбрами граней и трещинами, за ней — каллиграфические полосы полёта и осколки.
  {
    const Pt v[] = {{58, 8}, {77, 13}, {91, 30}, {90, 51}, {79, 66}, {60, 72}, {42, 64}, {32, 46}, {37, 24}};
    std::string rock = "M";
    for (const Pt& p : v) rock += (rock.size() > 1 ? "L" : "") + ptS(p);
    rock += "Z";
    const Pt in[] = {{59, 21}, {74, 26}, {78, 42}, {68, 55}, {53, 54}, {46, 38}};
    std::string facets = "M";
    for (const Pt& p : in) facets += (facets.size() > 1 ? "L" : "") + ptS(p);
    facets += "Z";
    facets += "M58 8 59 21M77 13 74 26M91 30 78 42M90 51 78 42M79 66 68 55M60 72 53 54M42 64 53 54M32 46 46 38M37 24 46 38M37 24 59 21";
    c.add("race-cyclopes-boulder", "Летящий валун",
          moved({G::F(brush({40, 62}, {30, 72}, {18, 82}, {3, 93}, 13, 12, 0, 0.12) + brush({33, 48}, {24, 54}, {14, 58}, {2, 61}, 9, 8.4, 0, 0.12) +
                      brush({52, 72}, {46, 82}, {38, 90}, {27, 98}, 9, 8.4, 0, 0.12)),
                 G::XS(rock, 3.4), G::F(rock), G::XS(clip(rock, facets, 1), 1.5), G::X("M60 38 64 44 61 49 65 54 62.5 55 58.5 50 61.5 45 57.5 39Z"),
                 G::F(lancet({22, 40}, {14, 34}, 4.4) + lancet({28, 30}, {23, 22}, 3.8) + lancet({19, 52}, {11, 51}, 3.6))},
                Affine::translate(3, -2.5)));
  }
}

// ---------------------------------------------------------------- ши-цзы
// Завитки-улитки, вырезанные в массе гривы: спиральные бороздки в кругах (cx, cy) радиуса r.
std::string curlCuts(std::initializer_list<Pt> centers, double r) {
  std::string s;
  for (const Pt& p : centers) s += spiral(p.x, p.y, 0.5, r, -90, 250, 1.3, 1.5, 26);
  return s;
}

// Сидящий лев-страж в профиль влево: голова с тяжёлым лбом и раскрытой пастью, грива языками назад, лапа на парчовом шаре, хвост-плюмаж.
std::vector<L> guardianLion(bool plinth) {
  const std::string head =
      "M47 12C42 6 32 4.5 24 6.5C17 8.5 13 13.5 12 19.5L5 23.5C3.5 27.5 5 31.5 8 32.5L10 37.5C14 41.5 21 43.5 29 42.5C36 42 42 39.5 46 35.5"
      "C50 30.5 51 19 47 12Z";
  const std::string mane = flame({45, 9}, {61, 4}, 9, 1.8) + flame({49, 18}, {67, 16}, 10, 2) + flame({49.5, 28}, {68, 31}, 10, 2) +
                           flame({46, 37}, {62, 46}, 9.5, 2) + flame({38, 41}, {50, 55}, 9, 2);
  const std::string tail = brush({76, 72}, {86, 66}, {92, 56}, {88, 44}, 10, 9, 6, 0.3) + spiral(84, 42, 1, 5.5, 20, 330, 4.6, 5.6, 28) +
                           flame({80, 70}, {98, 63}, 6.5, 2) + flame({84, 60}, {98, 50}, 5.5, 1.8) + flame({78, 64}, {74, 46}, 5.5, -1.6);
  const std::string body = "M37 41C49 41 59 47 65.5 57C74.5 59 82 67 82 77C82 83.5 79 88 73 89H46L44.5 74C38.5 66 34 56 34.5 46Z";
  const std::string legs = rrect(36.5, 58, 11, 30, 4.5) + "M33 84.5H48.5L49.5 89H32Z" +
                           brush({41, 60}, {35, 64}, {29.5, 66.5}, {23.5, 67.5}, 10.5, 9.5, 8.5, 0.5) + ell(21.5, 68, 6, 4.6) +
                           "M61 84.5H82L82.5 89H60Z";
  const std::string ball = circ(22, 79.5, 9);
  std::vector<L> v;
  if (plinth) v = {G::F(rrect(8, 89.5, 84, 7.5, 2)), G::XS("M8 93H92", 1.5)};
  for (const L& l :
       std::vector<L>{G::F(tail + mane),
                      G::XS(clip(mane + tail,
                                 "M47 10.5C52 9 56 8 59 6M51 19C56 18 61 17.5 64.5 17M51.5 29C56 29.5 61 30 65 31"
                                 "M47.5 38C52 40 56 42 59.5 44.5M80 68C86 63 89 57 89.5 51",
                                 1.5),
                            1.3),
                      G::XS(body + legs, 2.4), G::F(body + legs), G::X(curlCuts({{70, 76}}, 5)),
                      G::XS(clip(body, "M45 50C49 56 52 62 53 70", 2), 1.5), G::XS("M36.5 85V89M41 85V89M45 85V89M66 85V89M71 85V89M76 85V89", 1.3),
                      G::XS(ball, 2.4), G::F(ball), G::XS(clip(ball, "M14 74 28 88M17 70 31 84M13 80 22 89M20 70 30 80", 1.2), 1.4), G::XS(head, 2.6),
                      G::F(head), G::X(lancet({23, 21}, {34, 22.5}, 4.2, -0.8)), G::X(spiral(31, 13.5, 0.5, 4.2, 150, 470, 1.4, 1.6, 24)),
                      G::XS("M43 15C46 21 46 28 43 33.5", 1.6), G::X(ell(7.6, 26.5, 2, 1.5)),
                      G::X("M6 34C13 37.5 23 38.5 31 36.5C27 41.5 19.5 44 12.5 42Z"), G::F("M11.5 35.6 13.3 40 15.3 36.4ZM22 37 23.4 41.2 25.2 37Z")})
    v.push_back(l);
  return v;
}

// Пламенная жемчужина радиуса r с центром (cx, cy): ореол из языков пламени, спираль на шаре.
std::vector<L> flamingPearl(float cx, float cy, float r) {
  const float k = r / 19.f;
  std::string fire;
  for (int i = 0; i < 7; i++) {
    const double a = -90 + (i - 3) * 30;
    fire += flame(polar(cx, cy, r * 0.8, a), polar(cx, cy, r * (i == 3 ? 2.05 : 1.75 - std::abs(i - 3) * 0.08), a + (i - 3) * 6), r * 0.62,
                  r * 0.12 * (i < 3 ? 1 : -1));
  }
  return {G::F(fire), G::XS(circ(cx, cy, r), 2.6 * k), G::F(circ(cx, cy, r)),
          G::X(spiral(cx, cy, r * 0.08, r * 0.72, -90, 540, r * 0.16, r * 0.16, 60))};
}

void shishi(Catalog& c) {
  c.group("Ши-цзы", EmblemSection::Races);
  // Лев-страж: сидящий страж с лапой на парчовом шаре на постаменте.
  c.add("race-shishi-guardian", "Лев-страж", moved(guardianLion(true), Affine::translate(-1, 0)));
  // Голова ши-цзы: анфас в венце из закрученных языков гривы — брови-завитки, миндалевидные глаза, широкий нос, оскал с клыками.
  {
    std::string mane, cuts;
    for (int i = 0; i < 14; i++) {
      const double a = -90 + i * 360.0 / 14;
      mane += flame(polar(50, 50, 27, a - 6), polar(50, 50, 48, a + 16), 13, 2.2);
      cuts += "M" + pt(50, 50, 33, a - 2) + "Q" + pt(50, 50, 39, a + 3) + " " + pt(50, 50, 43, a + 13);
    }
    const std::string face = circ(50, 50, 29);
    c.add("race-shishi-head", "Голова ши-цзы",
          {G::F(mane), G::XS(clip(mane, cuts, 1.4), 1.4), G::XS(face, 3), G::F(face), G::XS(inner(face, 2.8, 6), 1.4),
           G::X(sym(spiral(35, 35, 0.5, 5, 200, 520, 1.4, 1.8, 26))), G::XS(sym("M40.5 33C43 35.5 45.5 38.5 47.5 42"), 2),
           G::X(sym(lancet({31, 45}, {45, 46.5}, 6.5, 1.4))), G::F(sym(circ(39, 45.6, 2.4))), G::XS("M42 53H58L54 61H46Z", 2),
           G::X(sym(lancet({44, 56.5}, {48, 57.5}, 2.2, -0.3))), G::XS("M50 62.5V65.5", 1.6),
           G::X("M31 65C40.5 71 59.5 71 69 65C67 76.5 59.5 82.5 50 82.5C40.5 82.5 33 76.5 31 65Z"),
           G::F(sym("M34 66.6 37 75 40 68.2Z") + "M41.5 69.5C46 71 54 71 58.5 69.5V72.5C54 74 46 74 41.5 72.5Z"),
           G::XS("M45.5 70V74M50 70.5V74.5M54.5 70V74", 1.2)});
  }
  // Пара стражей: два льва друг против друга на общем постаменте, между ними пламенная жемчужина.
  {
    const std::vector<L> lion = guardianLion(false);
    std::vector<L> v = moved(lion, Affine::translate(49, 43) * Affine::scale(-0.5f, 0.5f));
    for (const L& l : moved(lion, Affine::translate(51, 43) * Affine::scale(0.5f))) v.push_back(l);
    v.push_back(G::F(rrect(3, 87, 94, 9, 2.5)));
    v.push_back(G::XS("M3 91H97", 1.5));
    for (const L& l : flamingPearl(50, 31, 9.5)) v.push_back(l);
    c.add("race-shishi-pair", "Пара стражей", v);
  }
  // Парчовый шар: шар стража с переплетёнными поясами и цветком в медальоне, петля сверху, ленты-языки с кистями снизу.
  {
    std::string bands;
    for (int k = -4; k <= 4; k++) {
      const double d = k * 9.5, r = 28.5;
      if (std::fabs(d) >= r) continue;
      const double h = std::sqrt(r * r - d * d);
      for (int s : {-1, 1}) {
        const double ux = 0.7071, uy = 0.7071 * s, nx = -uy, ny = ux, cx = 50 + nx * d, cy = 42 + ny * d;
        bands += "M" + n(cx - ux * h) + " " + n(cy - uy * h) + "L" + n(cx + ux * h) + " " + n(cy + uy * h);
      }
    }
    std::string petals;
    for (int i = 0; i < 6; i++) petals += lancet(polar(50, 42, 1.5, -90 + 60 * i), polar(50, 42, 8.5, -90 + 60 * i), 4.2);
    const std::string ribbons =
        sym(brush({47, 76}, {40, 82}, {40, 89}, {31, 97}, 6.5, 6, 0, 0.3) + brush({46, 78}, {36, 80}, {30, 86}, {22, 89}, 5, 4.6, 0, 0.3)) +
        "M46.5 78H53.5L55.5 90 50 98 44.5 90Z";
    c.add("race-shishi-ball", "Парчовый шар",
          {G::S(circ(50, 7.5, 3.6), 2.4), G::F(ribbons + circ(50, 76, 5)), G::XS(clip(ribbons, "M50 82V95", 1), 1.4), G::XS(circ(50, 42, 31), 2.6),
           G::F(circ(50, 42, 31)), G::XS(inner(circ(50, 42, 31), 2.6, 6), 1.4), G::XS(clip(circ(50, 42, 31), bands, 4), 2.2), G::X(circ(50, 42, 11)),
           G::F(petals), G::X(circ(50, 42, 2))});
  }
}

// ---------------------------------------------------------------- эльфы бездны
// Щупальце вдоль кривой: сужается к острому кончику, на внутренней стороне — присоски (вырезы), side — сторона присосок (±1).
void tentacle(std::vector<L>& v, Pt p0, Pt c1, Pt c2, Pt p1, double w0, int side) {
  const std::string body = brush(p0, c1, c2, p1, w0, w0 * 0.95, 0, 0.15, 40);
  std::string cups;
  for (double t : {0.2, 0.34, 0.48, 0.61, 0.73}) {
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    const double w = brushW(t, w0, w0 * 0.95, 0, 0.15) * 0.5;
    cups += ell(q.x + q.nx * side * w * 0.42, q.y + q.ny * side * w * 0.42, std::max(0.9, w * 0.3), std::max(0.9, w * 0.3));
  }
  v.push_back(G::XS(body, 2.4));
  v.push_back(G::F(body));
  v.push_back(G::X(cups));
}

void abyssElves(Catalog& c) {
  c.group("Эльфы Бездны", EmblemSection::Races);
  // Око бездны: миндалевидный глаз с двойным контуром и вертикальным зрачком в воронке из изогнутых лучей-клинков.
  {
    std::string arms;
    for (int i = 0; i < 8; i++) {
      const double a = 45 * i;
      arms += brush(polar(50, 50, 20, a), polar(50, 50, 30, a + 14), polar(50, 50, 40, a + 34), polar(50, 50, 48, a + 62), 9, 7.5, 0, 0.12);
    }
    const std::string eye = "M10 50C20 33 35 26.5 50 26.5C65 26.5 80 33 90 50C80 67 65 73.5 50 73.5C35 73.5 20 67 10 50Z";
    const std::string white = "M19 50C27 38.5 38 33.5 50 33.5C62 33.5 73 38.5 81 50C73 61.5 62 66.5 50 66.5C38 66.5 27 61.5 19 50Z";
    c.add("race-abyss-elves-eye", "Око бездны",
          {G::F(arms), G::XS(eye, 3.6), G::F(eye), G::X(white), G::F(circ(50, 50, 14.5)), G::XS(circ(50, 50, 10.5), 1.4),
           G::X(lancet({50, 36.5}, {50, 63.5}, 7)), G::F(lancet({50, 41}, {50, 59}, 1.8))});
  }
  // Корона и щупальца: тонкий эльфийский венец с острыми зубцами и камнями, из-за него поднимаются щупальца с присосками и загнутыми концами.
  {
    std::vector<L> v;
    tentacle(v, {37, 72}, {29, 54}, {10, 48}, {12, 25}, 10.5, 1);
    tentacle(v, {63, 72}, {71, 54}, {90, 48}, {88, 25}, 10.5, -1);
    tentacle(v, {50, 70}, {43, 50}, {59, 30}, {51, 6}, 11.5, 1);
    v.push_back(G::S("M12.2 26C11.5 19 16 15.5 21 18.5M87.8 26C88.5 19 84 15.5 79 18.5M51.2 7C51.5 2 56 0.5 59 3.5", 2.2));
    const std::string crown =
        rrect(14, 70, 72, 11, 3) + sym(lancet({20.5, 72}, {20.5, 52}, 7.5) + lancet({33, 72}, {33, 58}, 6.5)) + lancet({50, 72}, {50, 56}, 8);
    v.push_back(G::XS(crown, 3));
    v.push_back(G::F(crown + sym(lancet({30, 80}, {29, 91}, 4.4)) + lancet({50, 80}, {50, 96}, 5.4)));
    v.push_back(G::XS("M15 74.2H85", 1.3));
    v.push_back(G::X(lancet({46, 77}, {54, 77}, 3.6) + sym(lancet({34, 77}, {39, 77}, 2.8) + lancet({20, 77}, {25, 77}, 2.8))));
    c.add("race-abyss-elves-crown", "Корона и щупальца", v);
  }
  // Тёмная звезда: изогнутые лучи-клинки закручены вокруг пустого ядра в двойном кольце, по кругу — прерывистый ореол.
  {
    std::string rays;
    for (int i = 0; i < 7; i++) {
      const double a = -90 + 360.0 / 7 * i;
      rays += brush(polar(50, 50, 13, a - 10), polar(50, 50, 26, a + 2), polar(50, 50, 37, a + 16), polar(50, 50, 48, a + 40), 14, 12, 0, 0.12);
    }
    c.add("race-abyss-elves-star", "Тёмная звезда",
          {G::F(rays), G::XS(circ(50, 50, 16), 3), G::S(circ(50, 50, 12), 4.6), G::XS(circ(50, 50, 12), 1.2), G::F(star(50, 50, 6, 1.6, 4, -90)),
           G::XS(dashedCircle(50, 50, 30, 7, 0.35), 1.4)});
  }
  // Клинок пустоты: стройный листовидный эльфийский меч с разломом пустоты, гарда из загнутых щупалец, обмотка, навершие-око; вокруг искры звёзд.
  {
    const std::string blade = "M50 2C57.5 10 64 20 64 34C64 46 60.5 56 58 65H42C39.5 56 36 46 36 34C36 20 42.5 10 50 2Z";
    const std::string guard = sym(brush({47, 67}, {35, 68.5}, {24, 63}, {21, 49}, 9.5, 8.4, 0, 0.2)) + rrect(33, 63, 34, 8, 4);
    const std::string pommel = "M38 91.5C42.5 86 57.5 86 62 91.5C57.5 97 42.5 97 38 91.5Z";
    c.add("race-abyss-elves-blade", "Клинок пустоты",
          {G::F(blade), G::X("M50 11 52.8 21 48.8 30 53.2 40 49.2 50 52 60H47.6L46 50 50.4 40 46.4 30 50 21Z"), G::XS(guard, 2.6), G::F(guard),
           G::XS(sym(clip(guard, "M44 67C36 67.5 29 64 26.5 55", 1)), 1.3), G::F(rrect(44.5, 70, 11, 17, 2.5)),
           G::XS("M44.5 74 55.5 76.5M44.5 79 55.5 81.5M44.5 84 55.5 86", 1.5), G::XS(pommel, 2.4), G::F(pommel),
           G::X(lancet({50, 87.8}, {50, 95.2}, 3.6)),
           G::F(star(24, 22, 7, 1.6, 4, -90) + star(76, 32, 5.4, 1.3, 4, -90) + star(72, 12, 3.6, 1, 4, -90) + star(28, 39, 3.6, 1, 4, -90))});
  }
}

// ---------------------------------------------------------------- энты
// Дубовый лист от основания b к кончику t: лопасти по краю, полуширина w.
std::string oakLeaf(Pt b, Pt t, double w) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::hypot(dx, dy), ux = dx / len, uy = dy / len;
  std::vector<Pt> lft, rgt;
  const int steps = 48;
  for (int i = 0; i <= steps; i++) {
    const double s = double(i) / steps, base = std::pow(std::sin(kPi * std::min(1.0, s * 1.04)), 0.7),
                 lobe = 0.58 + 0.42 * std::pow(std::fabs(std::cos(kPi * 3.5 * s)), 0.6), hw = w * base * lobe;
    const double x = b.x + dx * s, y = b.y + dy * s;
    lft.push_back({float(x - uy * hw), float(y + ux * hw)});
    rgt.push_back({float(x + uy * hw), float(y - ux * hw)});
  }
  std::string p = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) p += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) p += "L" + ptS(rgt[i]);
  return p + "Z";
}
// Жилки дубового листа: средняя и боковые к лопастям.
std::string oakVeins(Pt b, Pt t, double w) {
  std::string s = "M" + ptS(lerp(b, t, 0.04)) + "L" + ptS(lerp(b, t, 0.9));
  for (double k : {0.29, 0.57})
    s += "M" + ptS(lerp(b, t, k - 0.08)) + "L" + ptS(across(b, t, k, w * 0.62)) + "M" + ptS(lerp(b, t, k - 0.08)) + "L" +
         ptS(across(b, t, k, -w * 0.62));
  return s;
}

void ents(Catalog& c) {
  c.group("Энты", EmblemSection::Races);
  // Лицо энта: ствол-голова с бороздами коры, глаза-щели под тяжёлыми наплывами, нос-сучок, борода из мха прядями, ветви-рога с острыми листьями.
  {
    const std::string trunk =
        "M23 86C25 73 25 60 23 47C22 37 27 29 35 25H65C73 29 78 37 77 47C75 60 75 73 77 86C70 88.5 61 89.5 50 89.5C39 89.5 30 88.5 23 86Z";
    const std::string branches =
        sym(brush({38, 27}, {33, 18}, {24, 13}, {13, 12}, 7, 6, 0, 0.2) + brush({44, 25}, {44.5, 17}, {42, 11}, {38, 5}, 5.6, 5, 0, 0.2) +
            brush({30, 32}, {22, 29}, {13, 31}, {6, 38}, 5.4, 4.8, 0, 0.2) + brush({27, 15}, {24, 9}, {25, 5}, {29, 1.5}, 3.4, 3, 0, 0.2));
    const std::string leaves =
        sym(lancet({27, 14.5}, {21, 4}, 5.4, -1.2) + lancet({17, 29.5}, {9, 22}, 5, 1.2) + lancet({41.5, 13}, {48, 6}, 4.4, 1));
    const std::string beard = sym(brush({27, 76}, {26.5, 84}, {29, 90}, {26.5, 96.5}, 6.4, 6, 0, 0.3) +
                                  brush({34.5, 78}, {34, 86}, {37, 92}, {34.5, 99}, 6.6, 6.2, 0, 0.3) +
                                  brush({42, 79}, {41.5, 87}, {44.5, 92}, {42.5, 98}, 6.6, 6.2, 0, 0.3)) +
                              brush({50, 80}, {49, 88}, {52, 93}, {50, 99.5}, 6.6, 6.2, 0, 0.3);
    c.add("race-ents-face", "Лицо энта",
          {G::F(branches + leaves), G::XS(trunk, 2.8), G::F(trunk + beard),
           G::XS(clip(trunk, sym("M28 52C27 64 27.5 76 28.5 92M33 30C30 34 29 38 29.5 42"), 1.5), 1.5),
           G::XS(sym("M27.5 40C33 34.5 41.5 35.5 47.5 41"), 2.8), G::X(sym(lancet({32, 46}, {45, 47.4}, 3.6, -0.6))),
           G::XS("M47.5 46C46.5 53 44.5 59 46 63C48 66 52 66 54 63C55.5 59 53.5 53 52.5 46", 2), G::X(lancet({42, 70.5}, {58, 70.5}, 4.2)),
           G::XS(clip(trunk, sym("M31 74C31 80 31.5 84 30.5 88M38.5 76C38.5 82 39 86 38 89M46 77C46 83 46.5 86 46 89.5"), -1), 1.5),
           G::X(xf(lancet({0, -4}, {0, 4}, 2.6), place(67, 58, 12)))});
  }
  // Живая роща: в центре — дуб с кроной из острых листьев и глазами-щелями на стволе, по бокам — стрельчатые кипарисы; земля.
  {
    const std::string sides =
        sym("M22 23C29 33 33 45 33 57C33 66 28 72 22 74C16 72 11 66 11 57C11 45 15 33 22 23Z" + std::string("M20.5 72H23.5L24.5 92H19.5Z"));
    // Крона: окружность с острыми зубцами-листьями по краю.
    std::string crown = "M";
    for (int i = 0; i <= 160; i++) {
      const double a = -90 + 360.0 * i / 160, k = std::pow(std::fabs(std::sin(kPi * 11 * i / 160.0)), 0.6);
      crown += (i ? "L" : "") + ptS(polar(50, 30, 23 + 4.5 * k, a));
    }
    crown += "Z";
    const std::string trunk =
        "M41.5 92C44 80 44.5 66 43.5 52H56.5C55.5 66 56 80 58.5 92Z" + sym(brush({45, 56}, {41, 50}, {37, 46}, {32, 44}, 4.4, 4, 2, 0.4));
    const std::string sprigs =
        "M50 50V16M50 42C46 36 41 31 35 27M50 42C54 36 59 31 65 27M50 48C44 45 37 42 30 40M50 48C56 45 63 42 70 40M50 33C47.5 29 46 25 45 21"
        "M50 33C52.5 29 54 25 55 21";
    c.add("race-ents-grove", "Живая роща",
          {G::F(sides), G::XS(inner(sides, 2.6, 6), 1.3), G::XS(clip(sides, sym("M22 32V66"), 1), 1.3), G::XS(crown + trunk, 2.8),
           G::F(crown + trunk), G::XS(inner(crown, 3.4, 6), 1.4), G::XS(clip(crown, sprigs, 3.2), 1.5),
           G::X(sym(lancet({44, 63}, {49.2, 64}, 2.6, -0.4))), G::XS(sym("M43.5 58.5C45.5 57 47.5 57.3 49.2 59.1"), 1.5),
           G::XS("M46.5 76 45.5 88M53.5 77 54.5 89", 1.5), G::F(rrect(3, 90, 94, 6, 3)), G::XS(sym("M30 90C34 86 38 86 41 88"), 1.4)});
  }
  // Жёлудь и дубовые листья: жёлудь с чешуйчатой шапкой-сеткой и черенком на фоне двух лопастных листьев с жилками.
  {
    const std::string cap = "M27 47C27 35 37 28 50 28C63 28 73 35 73 47C73 51 69 53 65 53H35C31 53 27 51 27 47Z";
    const std::string nut = "M33 53H67C67 70 60 84 50 94C40 84 33 70 33 53Z";
    const Pt la{44, 40}, lt{7, 5}, ra{56, 40}, rt{93, 5};
    const std::string leaves = oakLeaf(la, lt, 13) + oakLeaf(ra, rt, 13);
    std::string net;
    for (double d = -40; d <= 40; d += 5.5)
      net += "M" + n(50 + d - 14) + " 26L" + n(50 + d + 14) + " 54M" + n(50 + d + 14) + " 26L" + n(50 + d - 14) + " 54";
    c.add("race-ents-acorn", "Жёлудь и дубовые листья",
          {G::F(leaves), G::XS(clip(leaves, oakVeins(la, lt, 13) + oakVeins(ra, rt, 13), 1.2), 1.5), G::XS(cap + nut, 3.2), G::F(nut),
           G::XS(clip(nut, "M40 59C39.5 69 42 78 47.5 86", 2), 1.8), G::F(cap + brush({48, 30}, {48, 24}, {50, 20}, {54, 16}, 4.6, 4.2, 3.4, 0.4)),
           G::XS(clip(cap, net, 1.6), 1.3), G::XS("M30.5 50.5H69.5", 1.4)});
  }
  // Корни-кулак: деревянный кулак, гранёные костяшки, волокна древесины, из запястья расходятся корни, над костяшками — росток.
  {
    const std::string fist =
        "M24 32L28 25H36L38.5 28 41.5 22H49L51.5 26 54.5 22H62L64.5 26.5 67.5 25H74L77 31 77.5 55 74 63 67 64 63 82H37L33 64 26 61 23.5 52Z";
    const std::string thumb = "M20 50C20 45 24 43 30 43H52C57 43 59 47 57 51L55 56C54 59 51 60 48 60H28C23 60 20 56 20 50Z";
    const std::string roots =
        brush({40, 80}, {30, 84}, {18, 88}, {5, 96}, 8, 7, 0, 0.2) + brush({46, 82}, {42, 90}, {36, 94}, {27, 99}, 6.5, 6, 0, 0.2) +
        brush({54, 82}, {58, 90}, {64, 94}, {73, 99}, 6.5, 6, 0, 0.2) + brush({60, 80}, {70, 84}, {82, 88}, {95, 96}, 8, 7, 0, 0.2) +
        brush({50, 82}, {50, 88}, {50.5, 93}, {50, 99.5}, 5.5, 5, 0, 0.2);
    c.add("race-ents-fist", "Корни-кулак",
          {G::F(fist + roots), G::XS("M38.5 28.5V46M51.5 27V46M64.5 27V46", 2.2),
           G::XS(clip(fist, "M28 30 35 31M42 27.5 48.5 27.5M55 27.5 61.5 27.5M68 30 74 31", 1), 1.4), G::XS(thumb, 2.8), G::F(thumb),
           G::XS(clip(thumb, "M27 51.5C34 49 44 49 52 51", 1.5), 1.4),
           G::XS(clip(fist, "M41 66C42 72 42 76 41 81M50 65V81M59 66C58 72 58 76 59 81M70 34C72 42 72 50 70 58", 1.5), 1.5),
           G::F(brush({45, 23}, {44, 16}, {46, 10}, {50, 5}, 3, 2.6, 2, 0.4) + lancet({50, 6}, {61, 2}, 5.6, -1.4) +
                lancet({48, 10}, {38, 5}, 5, 1.2))});
  }
}

}  // namespace

void raceEmblemsC(Catalog& c) {
  ogres(c);
  oni(c);
  orcs(c);
  fallenElves(c);
  desertElves(c);
  rakshasas(c);
  satyrs(c);
  lightElves(c);
  twilightElves(c);
  xianmaoren(c);
  darkElves(c);
  trolls(c);
  troglodytes(c);
  cyclopes(c);
  shishi(c);
  abyssElves(c);
  ents(c);
}

}  // namespace rg::gfx::emb
