// Regnum — отрисовка карты мира кодом (см. art_render.h).
//
// Знаки нарисованы геометрией в единицах карты относительно точки привязки (середина основания): так на исходнике
// стоят штампы, и при масштабе 1 значки ложатся пиксель в пиксель, а при приближении остаются чёткими.
#include "map/art_render.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "gfx/blend.h"
#include "gfx/stroke.h"

namespace rg::map::art {

// ================================================================ индекс
void Index::build(const MapArt& a) {
  a_ = &a;
  auto ringBox = [](const Ring& r) {
    Box2 b;
    for (Vec2 p : r) b.add(p);
    return b;
  };
  std::vector<Box2> boxes;
  for (const Ring& r : a.land) boxes.push_back(ringBox(r));
  for (const Ring& r : a.islets) boxes.push_back(ringBox(r));
  build(land_, boxes);
  boxes.clear();
  for (const Ring& r : a.water) boxes.push_back(ringBox(r));
  build(water_, boxes);
  boxes.clear();
  for (const River& r : a.rivers) {
    Box2 b;
    for (Vec2 p : r.pts) b.add(p);
    float w = 0;
    for (float x : r.w) w = std::max(w, x);
    boxes.push_back(b.inflated(w));
  }
  build(rivers_, boxes);
  boxes.clear();
  for (const Line& l : a.lines) {
    Box2 b;
    for (Vec2 p : l.pts) b.add(p);
    boxes.push_back(b.inflated(l.w));
  }
  build(lines_, boxes);
  boxes.clear();
  for (const Symbol& s : a.symbols) boxes.push_back(symbolBox(s.kind, s.x, s.y, s.s));
  build(symbols_, boxes);
}

void Index::build(Grid& g, const std::vector<Box2>& boxes) const {
  g.box = boxes;
  g.cell = 128;
  g.cols = std::max(1, int(std::ceil(std::max(1, a_->width) / g.cell)));
  g.rows = std::max(1, int(std::ceil(std::max(1, a_->height) / g.cell)));
  g.cells.assign(size_t(g.cols) * size_t(g.rows), {});
  for (u32 i = 0; i < boxes.size(); i++) {
    const Box2& b = boxes[i];
    if (b.empty()) continue;
    const int c0 = clamp(int(std::floor(b.x0 / g.cell)), 0, g.cols - 1), c1 = clamp(int(std::floor(b.x1 / g.cell)), 0, g.cols - 1);
    const int r0 = clamp(int(std::floor(b.y0 / g.cell)), 0, g.rows - 1), r1 = clamp(int(std::floor(b.y1 / g.cell)), 0, g.rows - 1);
    for (int r = r0; r <= r1; r++)
      for (int c = c0; c <= c1; c++) g.cells[size_t(r) * size_t(g.cols) + size_t(c)].push_back(i);
  }
}

void Index::query(const Grid& g, const Box2& b, std::vector<u32>& out) {
  out.clear();
  if (g.cols <= 0 || b.empty()) return;
  const int c0 = clamp(int(std::floor(b.x0 / g.cell)), 0, g.cols - 1), c1 = clamp(int(std::floor(b.x1 / g.cell)), 0, g.cols - 1);
  const int r0 = clamp(int(std::floor(b.y0 / g.cell)), 0, g.rows - 1), r1 = clamp(int(std::floor(b.y1 / g.cell)), 0, g.rows - 1);
  for (int r = r0; r <= r1; r++)
    for (int c = c0; c <= c1; c++)
      for (u32 i : g.cells[size_t(r) * size_t(g.cols) + size_t(c)])
        if (g.box[i].intersects(b)) out.push_back(i);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

// ================================================================ значки
namespace {

struct Part {
  gfx::Path path;
  Color color;
  float stroke = 0;   // 0 — заливка, иначе ширина обводки (единицы карты)
};
struct Icon {
  std::vector<Part> parts;
  RectF bounds;       // относительно точки привязки
};

void addRect(gfx::Path& p, float x, float y, float w, float h) {
  p.moveTo(x, y);
  p.lineTo(x + w, y);
  p.lineTo(x + w, y + h);
  p.lineTo(x, y + h);
  p.close();
}

// Пиксельный рисунок ('#' — краска) в прямоугольники: отрезки строк, одинаковые отрезки соседних строк сливаются.
gfx::Path rectsFromRows(const std::vector<const char*>& rows, float ax, float ay) {
  struct Run { int x0, x1, y0, y1; };
  std::vector<Run> done, open;
  for (int y = 0; y < int(rows.size()); y++) {
    std::vector<Run> cur;
    const char* s = rows[size_t(y)];
    for (int x = 0; s[x];) {
      if (s[x] != '#') { x++; continue; }
      int e = x;
      while (s[e] == '#') e++;
      cur.push_back({x, e, y, y + 1});
      x = e;
    }
    std::vector<Run> next;
    for (Run& r : cur) {
      auto it = std::find_if(open.begin(), open.end(), [&](const Run& o) { return o.x0 == r.x0 && o.x1 == r.x1; });
      if (it != open.end()) {
        r.y0 = it->y0;
        open.erase(it);
      }
      next.push_back(r);
    }
    for (const Run& o : open) done.push_back(o);
    open = std::move(next);
  }
  for (const Run& o : open) done.push_back(o);
  gfx::Path p;
  for (const Run& r : done) addRect(p, float(r.x0) - ax, float(r.y0) - ay, float(r.x1 - r.x0), float(r.y1 - r.y0));
  return p;
}

gfx::Path poly(std::initializer_list<gfx::Pt> pts, bool close, float ax, float ay) {
  gfx::Path p;
  bool first = true;
  for (gfx::Pt q : pts) {
    if (first) p.moveTo(q.x - ax, q.y - ay);
    else p.lineTo(q.x - ax, q.y - ay);
    first = false;
  }
  if (close) p.close();
  return p;
}

// Башня: штамп 10 × 15, три зубца по 2 пикселя; привязка — середина основания.
Icon makeTower() {
  Icon ic;
  ic.parts.push_back({rectsFromRows({"##..##..##", "##..##..##", "##########", "##########", "##########", "##########", "##########",
                                     "##########", "##########", "##########", "##########", "##########", "##########", "##########",
                                     "##########"},
                                    5, 15),
                      Color(0, 0, 0), 0});
  ic.bounds = RectF(-5, -15, 10, 15);
  return ic;
}

// Замок: штамп 40 × 31 — донжон с тремя башенками, боковые башни, стена с зубцами, ворота. Внутренность прозрачна.
Icon makeCastle() {
  static const std::vector<const char*> rows = {
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
  Icon ic;
  ic.parts.push_back({rectsFromRows(rows, 20, 31), Color(0, 0, 0), 0});
  ic.bounds = RectF(-20, -31, 40, 31);
  return ic;
}

// Гора: штамп 18 × 13 — серый треугольник с тёмными склонами и основанием, снежная шапка с тремя зубцами и тень
// под ней; у рисунка 1 — тёмный кончик вершины. Вся геометрия выводится из треугольника тела и нескольких величин,
// поэтому значок аккуратен при любом приближении; величины подобраны по штампам исходника (regnum-cli build-map
// --fit-icons). Координаты — пиксели штампа (левый верхний угол габарита — 0, 0), привязка — (9, 13).
//   0 x вершины, 1 y вершины, 2–3 полуширина основания слева и справа, 4 y основания, 5 серый тела;
//   6–7 ширина и серый склонов (внутри тела), 8–9 ширина и серый основания;
//   10 глубина шапки (до кончиков зубцов), 11 высота выемок между зубцами, 12 серый шапки, 13–14 ширина и серый
//   контура шапки; 15 отступ тени от низа шапки, 16–17 ширина и серый тени; 18–19 размер и серый тёмного кончика
//   (0 — нет); 20 сдвиг зубцов вдоль шапки.
constexpr int kMountainParams = 21;
using MountainParams = std::array<float, kMountainParams>;

Color gray(float v) {
  const u8 g = u8(clamp(int(std::lround(v)), 0, 255));
  return Color(g, g, g);
}

Icon makeMountain(const MountainParams& p) {
  const float ax = 9, ay = 13;
  const float cx = p[0], top = p[1], hl = std::max(1.f, p[2]), hr = std::max(1.f, p[3]), base = std::max(top + 2, p[4]);
  auto xl = [&](float y) { return cx - hl * (y - top) / (base - top); };
  auto xr = [&](float y) { return cx + hr * (y - top) / (base - top); };
  Icon ic;
  // Тело.
  ic.parts.push_back({poly({{cx, top}, {cx + hr, base}, {cx - hl, base}}, true, ax, ay), gray(p[5]), 0});
  // Склоны: полосы шириной ew вдоль сторон, внутри тела.
  const float ew = clamp(p[6], 0.f, 2.5f);
  if (ew > 0.05f) {
    const float lenL = std::hypot(hl, base - top), lenR = std::hypot(hr, base - top);
    const float nlx = (base - top) / lenL * ew, nly = -hl / lenL * ew;    // внутрь от левой стороны
    const float nrx = -(base - top) / lenR * ew, nry = -hr / lenR * ew;   // внутрь от правой стороны
    gfx::Path side = poly({{cx, top}, {cx - hl, base}, {cx - hl + nlx, base + nly}, {cx + nlx, top + nly}}, true, ax, ay);
    gfx::Path right = poly({{cx, top}, {cx + nrx, top + nry}, {cx + hr + nrx, base + nry}, {cx + hr, base}}, true, ax, ay);
    side.addPath(right);
    ic.parts.push_back({side, gray(p[7]), 0});
  }
  // Основание: полоса шириной bw по низу.
  const float bw = clamp(p[8], 0.f, 2.5f);
  if (bw > 0.05f) ic.parts.push_back({poly({{xl(base - bw), base - bw}, {xr(base - bw), base - bw}, {cx + hr, base}, {cx - hl, base}}, true, ax, ay), gray(p[9]), 0});
  // Шапка: от вершины по склонам до выемок, низ — три зубца.
  const float depth = clamp(p[10], 1.f, (base - top) * 0.8f), amp = clamp(p[11], 0.f, depth * 0.7f), skew = clamp(p[20], -1.5f, 1.5f);
  const float yb = top + depth, yn = yb - amp;
  const float L = xl(yn), R = xr(yn), wcap = R - L;
  auto tx = [&](float f) { return clamp(L + wcap * f + skew, L + 0.2f, R - 0.2f); };
  const std::vector<gfx::Pt> cap = {{cx, top}, {R, yn}, {tx(5 / 6.f), yb}, {tx(4 / 6.f), yn}, {tx(3 / 6.f), yb}, {tx(2 / 6.f), yn}, {tx(1 / 6.f), yb}, {L, yn}};
  gfx::Path capPath;
  for (size_t i = 0; i < cap.size(); i++) {
    if (i == 0) capPath.moveTo(cap[i].x - ax, cap[i].y - ay);
    else capPath.lineTo(cap[i].x - ax, cap[i].y - ay);
  }
  capPath.close();
  // Тень под шапкой — та же ломаная зубцов ниже на отступ (концы — на склонах).
  const float off = clamp(p[15], 0.f, 3.f), bandW = clamp(p[16], 0.f, 2.5f);
  if (bandW > 0.05f) {
    const float yo = yn + off;
    ic.parts.push_back({poly({{xl(yo) + 0.3f, yo}, {tx(1 / 6.f), yb + off}, {tx(2 / 6.f), yn + off}, {tx(3 / 6.f), yb + off}, {tx(4 / 6.f), yn + off},
                              {tx(5 / 6.f), yb + off}, {xr(yo) - 0.3f, yo}},
                             false, ax, ay),
                        gray(p[17]), bandW});
  }
  ic.parts.push_back({capPath, gray(p[12]), 0});
  if (p[13] > 0.05f) ic.parts.push_back({capPath, gray(p[14]), clamp(p[13], 0.f, 1.5f)});
  // Тёмный кончик вершины (рисунок 1).
  if (p[18] > 0.05f) {
    const float t = std::min(p[18], depth * 0.6f);
    ic.parts.push_back({poly({{cx, top}, {xr(top + t), top + t}, {xl(top + t), top + t}}, true, ax, ay), gray(p[19]), 0});
  }
  ic.bounds = RectF(-9.5f, -13.6f, 19.5f, 14.2f);
  return ic;
}

// Обводки значка — заранее в контуры заливки (в отрисовке остаются только заливки).
Icon baked(Icon ic) {
  for (Part& p : ic.parts) {
    if (p.stroke <= 0) continue;
    gfx::Stroke st;
    st.width = p.stroke;
    st.join = gfx::Join::Round;
    st.cap = gfx::Cap::Round;
    p.path = gfx::strokeToPath(p.path, st, 0.02f);
    p.stroke = 0;
  }
  return ic;
}

// Подобранные рисунки гор: 0 — светлая вершина, 1 — тёмный кончик вершины.
// Подобрано по штампам исходника (regnum-cli build-map --fit-icons).
const MountainParams kMountainA = {8.688f, 0.625f, 8.275f, 9.225f, 13.000f, 92.000f, -0.100f, 124.000f, 1.950f, 88.000f, 4.175f, 1.300f, 217.000f, 1.300f, 230.000f, -0.100f, 0.300f, 34.000f, 0.000f, 40.000f, -0.088f};
const MountainParams kMountainB = {9.250f, 0.150f, 9.256f, 9.800f, 12.600f, 94.000f, 1.150f, 182.000f, 0.575f, 46.000f, 4.400f, 0.688f, 244.000f, 0.724f, 224.000f, 0.250f, 1.275f, 26.000f, 0.956f, -1.000f, -0.400f};

const MountainParams& mountainParams(int design) { return design == 1 ? kMountainB : kMountainA; }

const Icon& iconOf(Sym k, int v = 0) {
  static const Icon mountainA = baked(makeMountain(mountainParams(0))), mountainB = baked(makeMountain(mountainParams(1)));
  static const Icon castle = baked(makeCastle()), tower = baked(makeTower());
  switch (k) {
    case Sym::Castle: return castle;
    case Sym::Tower: return tower;
    default: return v == 1 ? mountainB : mountainA;   // Peak — та же гора крупнее
  }
}

void drawIcon(gfx::Canvas& c, const Icon& ic, gfx::Pt at, float scale) {
  c.save();
  c.concat(gfx::Affine{scale, 0, 0, scale, at.x, at.y});
  for (const Part& p : ic.parts) c.fillPath(p.path, p.color);
  c.restore();
}

// Среднеквадратичная ошибка краски (255 − синий) рисунка горы против образца w × h с привязкой (ax, ay); вне образца
// и в незначимых пикселях ожидается белое. Штраф — за вывернутые шапку и тень.
double mountainError(const MountainParams& p, const std::vector<i16>& ink, int w, int h, int ax, int ay) {
  const int M = 3, W = w + 2 * M, H = h + 2 * M;
  gfx::Image img(W, H, gfx::premul(Color(255, 255, 255)));
  gfx::Canvas c(img);
  drawIcon(c, baked(makeMountain(p)), gfx::Pt{float(ax + M), float(ay + M)}, 1);
  double e = 0;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const int r = 255 - int(img.at(x, y) & 255);
      const int tx = x - M, ty = y - M;
      int t = 0;
      if (tx >= 0 && ty >= 0 && tx < w && ty < h) t = std::max<int>(0, ink[size_t(ty) * size_t(w) + size_t(tx)]);
      e += double(r - t) * double(r - t);
    }
  // Вершина — у середины основания (форма не перекашивается ради отдельных пикселей).
  const double pen = std::max(0.0, std::fabs(double(p[0]) - (double(p[0]) - p[2] + p[0] + p[3]) * 0.5) - 0.6);
  return std::sqrt(e / double(W * H)) + 50 * pen;
}

gfx::Stroke strokeOf(float w) {
  gfx::Stroke s;
  s.width = w;
  s.join = gfx::Join::Round;
  s.cap = gfx::Cap::Round;
  return s;
}

void addRing(gfx::Path& p, const Ring& r, const Xf& P) {
  if (r.size() < 3) return;
  const gfx::Pt a = P(r[0]);
  p.moveTo(a.x, a.y);
  for (size_t i = 1; i < r.size(); i++) {
    const gfx::Pt q = P(r[i]);
    p.lineTo(q.x, q.y);
  }
  p.close();
}

// Размытие 8-битной маски тремя ящиками (≈ гаусс σ) по строкам и столбцам.
void blurMask(std::vector<u8>& m, int w, int h, double sigma) {
  // Радиусы трёх ящиков для гаусса σ (W. Jarosz, «Fast Image Convolutions»).
  const double wIdeal = std::sqrt(12.0 * sigma * sigma / 3.0 + 1.0);
  int wl = int(std::floor(wIdeal));
  if (wl % 2 == 0) wl--;
  const int wu = wl + 2;
  const double mIdeal = (12.0 * sigma * sigma - 3.0 * wl * wl - 12.0 * wl - 9.0) / (-4.0 * wl - 4.0);
  const int mm = int(std::lround(mIdeal));
  int rad[3];
  for (int i = 0; i < 3; i++) rad[i] = ((i < mm ? wl : wu) - 1) / 2;
  std::vector<u8> tmp(m.size());
  auto pass = [&](const u8* src, u8* dst, int n, int stride, int count, int cstride, int r) {
    if (r <= 0) {
      for (int k = 0; k < count; k++)
        for (int i = 0; i < n; i++) dst[size_t(k) * size_t(cstride) + size_t(i) * size_t(stride)] = src[size_t(k) * size_t(cstride) + size_t(i) * size_t(stride)];
      return;
    }
    const int win = 2 * r + 1;
    for (int k = 0; k < count; k++) {
      const u8* s = src + size_t(k) * size_t(cstride);
      u8* d = dst + size_t(k) * size_t(cstride);
      int sum = 0;
      auto at = [&](int i) { return int(s[size_t(clamp(i, 0, n - 1)) * size_t(stride)]); };
      for (int i = -r; i <= r; i++) sum += at(i);
      for (int i = 0; i < n; i++) {
        d[size_t(i) * size_t(stride)] = u8((sum + win / 2) / win);
        sum += at(i + r + 1) - at(i - r);
      }
    }
  };
  for (int i = 0; i < 3; i++) {
    pass(m.data(), tmp.data(), w, 1, h, w, rad[i]);
    pass(tmp.data(), m.data(), h, w, w, 1, rad[i]);
  }
}

}  // namespace

RectF symbolBounds(Sym kind) { return iconOf(kind).bounds; }

Box2 symbolBox(Sym kind, double x, double y, double s) {
  const RectF r = symbolBounds(kind);
  return Box2(x + r.x * s, y + r.y * s, x + r.right() * s, y + r.bottom() * s);
}

void drawSymbol(gfx::Canvas& c, Sym kind, gfx::Pt at, float scale, int variant) { drawIcon(c, iconOf(kind, variant), at, scale); }

std::string fitMountainIcon(const std::vector<i16>& ink, int w, int h, int ax, int ay, int design, int maxEvals, double* error) {
  MountainParams p = mountainParams(design);
  double best = mountainError(p, ink, w, h, ax, ay);
  std::array<float, kMountainParams> step;
  for (int i = 0; i < kMountainParams; i++) {
    const bool grayIdx = i == 5 || i == 7 || i == 9 || i == 12 || i == 14 || i == 17 || i == 19;
    const bool widthIdx = i == 6 || i == 8 || i == 13 || i == 16 || i == 18;
    step[size_t(i)] = grayIdx ? 16.f : widthIdx ? 0.2f : 0.4f;
  }
  int evals = 0;
  for (int round = 0; round < 12 && evals < maxEvals; round++) {
    bool improved = true;
    while (improved && evals < maxEvals) {
      improved = false;
      for (int i = 0; i < kMountainParams && evals < maxEvals; i++) {
        if (design == 0 && (i == 18 || i == 19)) continue;   // у рисунка 0 кончика нет
        for (float sgn : {1.f, -1.f}) {
          MountainParams q = p;
          q[size_t(i)] += sgn * step[size_t(i)];
          const double e = mountainError(q, ink, w, h, ax, ay);
          evals++;
          if (e < best - 1e-6) {
            best = e;
            p = q;
            improved = true;
            break;
          }
        }
      }
    }
    for (float& s : step) s *= 0.5f;
  }
  if (error) *error = best;
  std::string out = "{";
  for (int i = 0; i < kMountainParams; i++) out += strf("%s%.3ff", i ? ", " : "", p[size_t(i)]);
  return out + "}";
}

// ================================================================ слои
void drawSea(gfx::Image& img, const Index& idx, const Xf& P) {
  const MapArt& a = idx.art();
  const int W = img.w, H = img.h;
  const double sigma = double(a.style.coastSoft) * P.ds;
  const int m = sigma >= 0.35 ? int(std::ceil(sigma * 3)) : 0;
  const int CW = W + 2 * m, CH = H + 2 * m;
  const Xf Pm{P.ds, P.ox - m, P.oy - m};
  std::vector<u32> ids;
  idx.land(Pm.mapBox(CW, CH), ids);
  const u32 sea = gfx::premul(a.style.sea);
  if (ids.empty()) {  // одно море
    for (u32& p : img.px) p = sea;
    return;
  }
  gfx::Image cov(CW, CH, 0);
  {
    gfx::Canvas c(cov);
    gfx::Path path;
    const size_t nl = a.land.size();
    for (u32 i : ids) addRing(path, i < nl ? a.land[i] : a.islets[i - nl], Pm);
    c.fillPath(path, Color(255, 255, 255));
  }
  std::vector<u8> land(size_t(CW) * size_t(CH));
  bool full = true;
  for (size_t i = 0; i < land.size(); i++) {
    land[i] = u8(cov.px[i] >> 24);
    full = full && land[i] == 255;
  }
  if (full) return;   // одна суша
  std::vector<u8> glow;
  const int k = int(std::lround(clamp(double(a.style.coastGlow), 0.0, 1.0) * 256));
  if (m > 0 && k > 0) {
    glow = land;
    blurMask(glow, CW, CH, sigma);
  }
  for (int y = 0; y < H; y++) {
    u32* d = img.row(y);
    const u8* lr = land.data() + size_t(y + m) * size_t(CW) + size_t(m);
    const u8* gr = glow.empty() ? nullptr : glow.data() + size_t(y + m) * size_t(CW) + size_t(m);
    for (int x = 0; x < W; x++) {
      u32 sa = 255u - lr[x];                                     // море
      if (gr) sa = sa * (65280u - u32(k) * gr[x]) / 65280u;       // светлая кайма у берега
      if (sa == 0) continue;
      d[x] = sa == 255 ? sea : gfx::px::srcOver(gfx::px::mul(sea, sa), d[x]);
    }
  }
}

void drawWater(gfx::Image& img, const Index& idx, const Xf& P) {
  const MapArt& a = idx.art();
  const Box2 box = P.mapBox(img.w, img.h).inflated(1);
  std::vector<u32> ids, rivers;
  idx.water(box, ids);
  idx.rivers(box, rivers);
  if (ids.empty() && rivers.empty()) return;
  gfx::Canvas c(img);
  if (!ids.empty()) {
    gfx::Path path;
    for (u32 i : ids) addRing(path, a.water[i], P);
    c.fillPath(path, a.style.water, gfx::FillRule::NonZero);
  }
  for (u32 i : rivers) {
    const River& r = a.rivers[i];
    if (r.pts.size() < 2) continue;
    float w = 0;
    for (float x : r.w) w += x;
    w = r.w.empty() ? 2.f : w / float(r.w.size());
    gfx::Path p;
    for (size_t k = 0; k < r.pts.size(); k++) {
      const gfx::Pt q = P(r.pts[k]);
      if (k == 0) p.moveTo(q.x, q.y);
      else p.lineTo(q.x, q.y);
    }
    c.strokePath(p, strokeOf(float(w * P.ds)), a.style.water);
  }
}

void drawSymbols(gfx::Image& img, const Index& idx, const Xf& P) {
  const MapArt& a = idx.art();
  const Box2 box = P.mapBox(img.w, img.h).inflated(2);
  gfx::Canvas c(img);
  std::vector<u32> ids;
  idx.lines(box, ids);
  for (u32 i : ids) {
    const Line& l = a.lines[i];
    gfx::Path p;
    for (size_t k = 0; k < l.pts.size(); k++) {
      const gfx::Pt q = P(l.pts[k]);
      if (k == 0) p.moveTo(q.x, q.y);
      else p.lineTo(q.x, q.y);
    }
    gfx::Stroke st = strokeOf(float(l.w * P.ds));
    if (l.dash > 0) st.dash = {float(l.dash * P.ds), float(l.dash * P.ds)};
    c.strokePath(p, st, Color(0, 0, 0));
  }
  idx.symbols(box, ids);
  for (u32 i : ids) {
    const Symbol& s = a.symbols[i];
    drawSymbol(c, s.kind, P(Vec2(s.x, s.y)), float(P.ds * s.s), s.v);
  }
}

gfx::Image render(const Index& idx, double ds, int w, int h, double ox, double oy) {
  gfx::Image img(w, h, gfx::premul(idx.art().style.land));
  const Xf P{ds, ox, oy};
  drawSea(img, idx, P);
  drawWater(img, idx, P);
  drawSymbols(img, idx, P);
  return img;
}

}  // namespace rg::map::art
