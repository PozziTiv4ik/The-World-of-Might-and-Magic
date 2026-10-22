// Regnum — разбор исходной карты: поиск штампов знаков (башни, замки, горы) в целых пикселях и со сдвигом на долю
// пикселя (см. art_extract_internal.h).
#include <cstring>
#include <unordered_map>

#include "base/jobs.h"
#include "mapbake/art_extract_internal.h"

namespace rg::map::art {

using namespace detail;

namespace {

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

namespace detail {

namespace {

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

const std::vector<const char*> kTower = {"##..##..##", "##..##..##", "##########", "##########", "##########", "##########", "##########",
                                         "##########", "##########", "##########", "##########", "##########", "##########", "##########",
                                         "##########"};
const std::vector<const char*> kCastle = {
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

}  // namespace

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

SymbolSearch::SymbolSearch(const codec::RgbaImage& img) : flat(img), w(img.w), h(img.h), F(img) {
  mountains = mountainStamps(flat, &mountainVariants);
  tower = binaryStamp(Sym::Tower, kTower, 5, 15);
  castle = binaryStamp(Sym::Castle, kCastle, 20, 31);
  explained.assign(size_t(w) * size_t(h), 0);
}

void SymbolSearch::mark(const Stamp& s, int x0, int y0) {
  for (int j = 0; j < s.h; j++)
    for (int i = 0; i < s.w; i++) {
      const int x = x0 + i, y = y0 + j;
      if (x < 0 || y < 0 || x >= w || y >= h) continue;
      const int t = s.ink[size_t(j) * size_t(s.w) + size_t(i)];
      if (t >= 24 && std::abs(F.ink(x, y) - t) <= 48) explained[size_t(y) * size_t(w) + size_t(x)] = 1;
    }
}

bool SymbolSearch::stampRemnant(const std::vector<u32>& comp, int bx0, int by0, int bx1, int by1) const {
  std::vector<const Placed*> near;
  for (const Placed& p : placed)
    if ((p.st->kind == Sym::Castle || p.st->kind == Sym::Tower) && p.x <= bx1 + 1 && p.x + p.st->w >= bx0 - 1 && p.y <= by1 + 1 && p.y + p.st->h >= by0 - 1)
      near.push_back(&p);
  if (near.empty()) return false;
  size_t on = 0;
  for (u32 i : comp) {
    const int x = int(i % u32(w)), y = int(i / u32(w));
    bool hit = false;
    for (size_t k = 0; k < near.size() && !hit; k++) {
      const Stamp& s = *near[k]->st;
      for (int dy = -1; dy <= 1 && !hit; dy++)
        for (int dx = -1; dx <= 1 && !hit; dx++) {
          const int u = x + dx - near[k]->x, v = y + dy - near[k]->y;
          hit = u >= 0 && v >= 0 && u < s.w && v < s.h && s.ink[size_t(v) * size_t(s.w) + size_t(u)] >= 100;
        }
    }
    on += hit;
  }
  return on * 10 >= comp.size() * 9;
}

void placeStamps(SymbolSearch& ctx, MapArt& art, ExtractReport& rep) {
  const int w = ctx.w, h = ctx.h;
  const StampFinder& F = ctx.F;
  const Stamp &tower = ctx.tower, &castle = ctx.castle;
  const std::vector<Stamp>& mountains = ctx.mountains;
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
      const Stamp& s = ctx.stampOf(f);
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
  for (const Found& f : found) {
    const Stamp& s = ctx.stampOf(f);
    art.symbols.push_back(Symbol{f.kind, float(f.x + s.ax), float(f.y + s.ay), 1, u8(f.kind == Sym::Mountain ? s.design : 0)});
    ctx.placed.push_back({&s, f.x, f.y});
    rep.symbols[size_t(f.kind)]++;
  }
  for (const Found& f : found) ctx.mark(ctx.stampOf(f), f.x, f.y);
}

namespace {

// Бледный или сглаженный замок или башня в области comp (габарит bx0..bx1 × by0..by1): линии серые, рисунок бывает
// уменьшен с размытием, и сравнение краски с допуском его не узнаёт. Масштаб — по габариту области, положение — сдвиги
// на долю пикселя около её угла, сходство — корреляция формы образца с краской (не зависит от яркости линий).
bool placeFadedStamp(SymbolSearch& ctx, const std::vector<u32>& comp, int bx0, int by0, int bx1, int by1, MapArt& art, ExtractReport& rep) {
  constexpr double kMinCorrelation = 0.9;
  const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
  const Stamp* bestT = nullptr;
  double best = kMinCorrelation;
  int bestK = 0, bestS = 0, bestX = 0, bestY = 0;
  for (const Stamp* T : {&ctx.castle, &ctx.tower})
    for (int k = 85; k <= 115; k++) {   // масштаб k / 100
      const double sc = k / 100.0;
      if (std::fabs(T->w * sc - bw) > 2 || std::fabs(T->h * sc - bh) > 2) continue;
      for (int s = 0; s < 16; s++) {
        const Stamp S = resampleStamp(*T, sc, (s % 4) * 0.25, (s / 4) * 0.25);
        for (int y = by0 - 2; y <= by0 + 1; y++)
          for (int x = bx0 - 2; x <= bx0 + 1; x++) {
            const double c = ctx.F.correlation(S, x, y);
            if (c > best) {
              best = c;
              bestT = T;
              bestK = k;
              bestS = s;
              bestX = x;
              bestY = y;
            }
          }
      }
    }
  if (!bestT) return false;
  const double sc = bestK / 100.0, fx = (bestS % 4) * 0.25, fy = (bestS / 4) * 0.25;
  const double ax = bestX + fx + bestT->ax * sc, ay = bestY + fy + bestT->ay * sc;
  for (const Symbol& q : art.symbols)   // не второй знак на месте уже найденного
    if (q.kind == bestT->kind && std::fabs(q.x - ax) < 0.4 * bestT->w * sc && std::fabs(q.y - ay) < 0.4 * bestT->h * sc) return false;
  art.symbols.push_back(Symbol{bestT->kind, float(ax), float(ay), float(sc), 0});
  rep.symbols[size_t(bestT->kind)]++;
  rep.subpixel++;
  ctx.faded.push_back(resampleStamp(*bestT, sc, fx, fy));
  ctx.placed.push_back({&ctx.faded.back(), bestX, bestY});
  ctx.mark(ctx.faded.back(), bestX, bestY);
  for (u32 i : comp) ctx.explained[i] = 1;
  return true;
}

}  // namespace

// Поиск идёт в необъяснённых областях краски; из подходящих вариантов берётся объясняющий больше краски (крупная
// гора, а не три мелких). Где ни один вариант не подошёл — бледный или сглаженный замок или башня.
void placeShiftedStamps(SymbolSearch& ctx, MapArt& art, ExtractReport& rep) {
  using Base = SymbolSearch::Base;
  const int w = ctx.w, h = ctx.h;
  const StampFinder& F = ctx.F;
  std::vector<Base>& bases = ctx.bases;
  std::vector<u8>& explained = ctx.explained;
  auto isInk = [&](size_t i) { return ctx.isInk(i); };
  auto addBase = [&](const Stamp& st, double sc) {
    Base b{&st, sc, {}, 0};
    for (int sy = 0; sy < 4; sy++)
      for (int sx = 0; sx < 4; sx++) b.shifts.push_back(resampleStamp(st, sc, sx * 0.25, sy * 0.25));
    for (i16 t : b.shifts[0].ink) b.dark += t >= 100;
    bases.push_back(std::move(b));
  };
  for (double sc : {0.92, 0.96, 1.0, 1.04, 1.08}) {
    addBase(ctx.castle, sc);
    addBase(ctx.tower, sc);
  }
  for (int d = 0; d < 2; d++)
    for (const Stamp& st : ctx.mountains)
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
    const size_t before = art.symbols.size();
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
      ctx.placed.push_back({&B.shifts[size_t(bestS)], bestX, bestY});
      ctx.mark(B.shifts[size_t(bestS)], bestX, bestY);
      if (kind == Sym::Peak) {  // размытый ореол крупной горы — тоже она (а не новые знаки)
        const Stamp& S = B.shifts[size_t(bestS)];
        for (int y = std::max(0, bestY - 3); y < std::min(h, bestY + S.h + 3); y++)
          for (int x = std::max(0, bestX - 3); x < std::min(w, bestX + S.w + 3); x++) explained[size_t(y) * size_t(w) + size_t(x)] = 1;
      }
    }
    if (art.symbols.size() == before) placeFadedStamp(ctx, comp, bx0, by0, bx1, by1, art, rep);
  }
}

}  // namespace detail

}  // namespace rg::map::art
