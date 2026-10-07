// Regnum — раскладка отметок войск и флота. ТЗ 1.c.iv: объекты на карте не наслаиваются друг на друга. Фигурка
// имеет постоянный размер на экране, поэтому при обзорном масштабе соседние объекты перекрылись бы: такие объекты
// рисуются одной отметкой-стопкой (верхняя фигурка, за ней — фигурки других объектов цветами их фракций, значок с
// числом объектов). При приближении стопка распадается на отдельные фигурки.
//
// ТЗ «Исправления», п.3 («при отдалении войско не должно шатать по карте»): стопки — срез дерева слияния
// (MarkTree), построенного один раз для набора объектов. Порог слияния двух кластеров — наибольший масштаб, при
// котором их отметки пересекаются; кластеры сливаются в порядке убывания порога, поэтому раскладка монотонна по
// масштабу: при отдалении стопки только сливаются, при приближении только распадаются, а одинаковый масштаб даёт
// одинаковую раскладку. Стопка стоит в точке своего верхнего объекта.
// Раскладка считается в логических пикселях пространства «карта × масштаб», поэтому не зависит от сдвига камеры.
#include <queue>

#include "gfx/figures.h"
#include "map/map_internal.h"

namespace rg::map::detail {

namespace {

constexpr float kGap = 3;   // наименьший зазор между отметками, логические пиксели

RectF unite(const RectF& a, const RectF& b) {
  const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
  return {x0, y0, std::max(a.right(), b.right()) - x0, std::max(a.bottom(), b.bottom()) - y0};
}

// Порог по одной оси: отрезки [p·z + a0, p·z + a1] и [q·z + b0, q·z + b1] пересекаются (с зазором) при z ниже него.
double axisZoom(double p, double a0, double a1, double q, double b0, double b1) {
  const double d = q - p;
  if (d > 0) return (a1 - b0 + kGap) / d;
  if (d < 0) return (b1 - a0 + kGap) / -d;
  return kInf;
}

// Кандидат слияния (очередь по убыванию порога; при равенстве — по номерам узлов: раскладка детерминирована).
struct Pair {
  double zoom;
  i32 a, b;
  bool operator<(const Pair& o) const {
    if (zoom != o.zoom) return zoom < o.zoom;
    if (a != o.a) return a > o.a;
    return b > o.b;
  }
};

}  // namespace

float figureSizeOf(const Settings& s) { return float(clamp(s.figureSize, schema::kFigureSizeMin, schema::kFigureSizeMax)); }

gfx::TextStyle markBadgeStyle(float dpi) {
  gfx::TextStyle ts;
  ts.size = 10.5f * dpi;
  ts.weight = gfx::FontWeight::Semibold;
  return ts;
}

int stackDepth(size_t count) { return count > 1 ? int(std::min<size_t>(count - 1, 2)) : 0; }

gfx::Pt stackOffset(int k, float size) {
  // Фигурки стопки — левее и выше верхней (значок числа объектов — справа сверху, численность — справа снизу).
  return k <= 1 ? gfx::Pt(std::round(-0.22f * size), std::round(-0.13f * size)) : gfx::Pt(std::round(-0.42f * size), std::round(-0.25f * size));
}

RectF unitBadgeRect(gfx::Pt c, float size, const std::string& text, float dpi) {
  const float tw = gfx::measureText(text, markBadgeStyle(dpi));
  const float bh = std::round(14.5f * dpi), bw = std::max(bh, std::round(tw + 9 * dpi));
  return {c.x + std::round(size * 0.16f), c.y + std::round(size * 0.2f), bw, bh};
}

std::string countBadgeText(size_t count) { return count > 99 ? std::string("99+") : std::to_string(count); }

RectF countBadgeRect(gfx::Pt c, float size, const std::string& text, float dpi) {
  const float tw = gfx::measureText(text, markBadgeStyle(dpi));
  const float h = std::round(16 * dpi), w = std::max(h, std::round(tw + 9 * dpi));
  return {c.x + std::round(size * 0.46f - w * 0.5f), c.y + std::round(-size * 0.56f - h * 0.5f), w, h};
}

RectF markFootprint(float size, size_t count, i64 units) {
  const gfx::Pt o(0, 0);
  RectF r = gfx::figureBounds(o, size);
  for (int k = 1; k <= stackDepth(count); k++) r = unite(r, gfx::figureBounds(stackOffset(k, size), size));
  r = unite(r, unitBadgeRect(o, size, compactCount(units), 1));
  if (count > 1) r = unite(r, countBadgeRect(o, size, countBadgeText(count), 1));
  return r.expand(1);   // сглаживание краёв при отрисовке
}

bool markHit(const MarkLayout::Mark& m, float size, float dx, float dy) {
  const float r = size * 0.5f;
  // Постамент фигурки (как раньше у одиночной фигурки: круг чуть выше центра).
  auto disc = [&](gfx::Pt o) {
    const float x = dx - o.x, y = dy - (o.y - r * 0.08f);
    return x * x + y * y <= r * r * 1.1f;
  };
  if (disc(gfx::Pt(0, 0))) return true;
  for (int k = 1; k <= stackDepth(m.members.size()); k++)
    if (disc(stackOffset(k, size))) return true;
  if (unitBadgeRect(gfx::Pt(0, 0), size, compactCount(m.units), 1).contains(dx, dy)) return true;
  return m.members.size() > 1 && countBadgeRect(gfx::Pt(0, 0), size, countBadgeText(m.members.size()), 1).contains(dx, dy);
}

double mergeZoom(Vec2 p, const RectF& relP, Vec2 q, const RectF& relQ) {
  const double zx = axisZoom(p.x, relP.x, relP.right(), q.x, relQ.x, relQ.right());
  const double zy = axisZoom(p.y, relP.y, relP.bottom(), q.y, relQ.y, relQ.bottom());
  return std::min(zx, zy);
}

MarkTree buildMarkTree(const std::vector<const Army*>& armies, Id sel, float figure, double floor) {
  MarkTree T;
  T.figure = figure;
  T.floor = std::max(0.0, floor);
  struct Item {
    const Army* a;
    i64 units;
  };
  std::vector<Item> items;
  items.reserve(armies.size());
  for (const Army* a : armies)
    if (a) items.push_back({a, armyCount(*a)});
  // Приоритет (он же порядок «кто сверху»): выделенный, затем многочисленный, затем меньший ID.
  std::sort(items.begin(), items.end(), [&](const Item& x, const Item& y) {
    const bool sx = sel && x.a->id == sel, sy = sel && y.a->id == sel;
    if (sx != sy) return sx;
    if (x.units != y.units) return x.units > y.units;
    return x.a->id < y.a->id;
  });
  const i32 n = i32(items.size());
  T.ids.reserve(size_t(n));
  T.pos.reserve(size_t(n));
  T.nodes.reserve(size_t(std::max(0, 2 * n - 1)));
  float maxW = 0, maxH = 0;
  for (i32 i = 0; i < n; i++) {
    T.ids.push_back(items[size_t(i)].a->id);
    T.pos.push_back(items[size_t(i)].a->pos);
    MarkTree::Node nd;
    nd.top = i;
    nd.units = items[size_t(i)].units;
    nd.merge = kInf;
    nd.rel = markFootprint(figure, 1, nd.units);
    maxW = std::max(maxW, nd.rel.w);
    maxH = std::max(maxH, nd.rel.h);
    T.nodes.push_back(nd);
  }
  if (n < 2) return T;
  auto posOf = [&](i32 node) { return T.pos[size_t(T.nodes[size_t(node)].top)]; };
  auto zoomOf = [&](i32 x, i32 y) { return mergeZoom(posOf(x), T.nodes[size_t(x)].rel, posOf(y), T.nodes[size_t(y)].rel); };

  std::priority_queue<Pair> heap;
  // Начальные пары: только те, что сливаются выше floor (по x — проход по отсортированным точкам).
  {
    std::vector<i32> byX(static_cast<size_t>(n));
    for (i32 i = 0; i < n; i++) byX[size_t(i)] = i;
    std::sort(byX.begin(), byX.end(), [&](i32 x, i32 y) { return T.pos[size_t(x)].x != T.pos[size_t(y)].x ? T.pos[size_t(x)].x < T.pos[size_t(y)].x : x < y; });
    const double reachX = T.floor > 0 ? (2.0 * maxW + kGap) / T.floor : kInf;
    const double reachY = T.floor > 0 ? (2.0 * maxH + kGap) / T.floor : kInf;
    for (size_t k = 0; k < byX.size(); k++) {
      const i32 i = byX[k];
      for (size_t m = k + 1; m < byX.size(); m++) {
        const i32 j = byX[m];
        if (T.pos[size_t(j)].x - T.pos[size_t(i)].x > reachX) break;
        if (std::fabs(T.pos[size_t(j)].y - T.pos[size_t(i)].y) > reachY) continue;
        const double z = zoomOf(i, j);
        if (z > T.floor) heap.push({z, std::min(i, j), std::max(i, j)});
      }
    }
  }
  std::vector<i32> alive(static_cast<size_t>(n));
  for (i32 i = 0; i < n; i++) alive[size_t(i)] = i;
  std::vector<char> dead(static_cast<size_t>(2 * n), 0);
  double current = kInf;
  while (!heap.empty()) {
    const Pair p = heap.top();
    heap.pop();
    if (dead[size_t(p.a)] || dead[size_t(p.b)]) continue;
    current = std::min(current, p.zoom);
    const MarkTree::Node& A = T.nodes[size_t(p.a)];
    const MarkTree::Node& B = T.nodes[size_t(p.b)];
    MarkTree::Node c;
    c.a = p.a;
    c.b = p.b;
    c.top = std::min(A.top, B.top);   // меньший номер листа — больший приоритет
    c.count = A.count + B.count;
    c.units = A.units + B.units;
    c.merge = current;
    c.rel = markFootprint(figure, c.count, c.units);
    const i32 ci = i32(T.nodes.size());
    T.nodes[size_t(p.a)].parent = ci;
    T.nodes[size_t(p.b)].parent = ci;
    T.nodes.push_back(c);
    dead[size_t(p.a)] = dead[size_t(p.b)] = 1;
    // Новый кластер: пороги со всеми живыми (не выше текущего — слитый кластер растёт, а не «расходится»).
    std::erase_if(alive, [&](i32 x) { return dead[size_t(x)] != 0; });
    for (i32 k : alive) {
      const double z = std::min(zoomOf(ci, k), current);
      if (z > T.floor) heap.push({z, std::min(ci, k), std::max(ci, k)});
    }
    alive.push_back(ci);
  }
  return T;
}

MarkLayout cutMarks(const MarkTree& T, double zoom) {
  MarkLayout L;
  L.figure = T.figure;
  const size_t n = T.ids.size();
  if (n == 0) return L;
  // Представитель листа при масштабе zoom: подъём, пока слияние родителя выше масштаба (с запоминанием пути).
  std::vector<i32> rep(T.nodes.size(), -1);
  std::vector<i32> path;
  auto find = [&](i32 x) {
    path.clear();
    i32 y = x;
    while (rep[size_t(y)] < 0) {
      const i32 p = T.nodes[size_t(y)].parent;
      if (p < 0 || !(T.nodes[size_t(p)].merge > zoom)) {
        rep[size_t(y)] = y;
        break;
      }
      path.push_back(y);
      y = p;
    }
    const i32 r = rep[size_t(y)];
    for (i32 z : path) rep[size_t(z)] = r;
    return r;
  };
  std::vector<i32> markOf(T.nodes.size(), -1);
  for (size_t i = 0; i < n; i++) {
    const i32 r = find(i32(i));
    i32& slot = markOf[size_t(r)];
    if (slot < 0) {
      slot = i32(L.marks.size());
      const MarkTree::Node& nd = T.nodes[size_t(r)];
      MarkLayout::Mark m;
      m.members.reserve(nd.count);
      m.pos = T.pos[size_t(nd.top)];
      m.rel = nd.rel;
      m.units = nd.units;
      L.marks.push_back(std::move(m));
    }
    // Листья — по приоритету: первый лист кластера — его верхний объект.
    L.marks[size_t(slot)].members.push_back(T.ids[i]);
  }
  // Порядок отрисовки: сверху вниз по экрану, затем по ID верхнего объекта.
  std::sort(L.marks.begin(), L.marks.end(), [](const MarkLayout::Mark& a, const MarkLayout::Mark& b) {
    return a.pos.y != b.pos.y ? a.pos.y < b.pos.y : a.members.front() < b.members.front();
  });
  return L;
}

double splitZoom(const std::vector<const Army*>& members, Id sel, float figure) {
  const MarkTree T = buildMarkTree(members, sel, figure, 0);
  double z = 0;
  for (size_t i = T.ids.size(); i < T.nodes.size(); i++) z = std::max(z, T.nodes[i].merge);
  return z;
}

MarkLayout layoutMarks(const std::vector<const Army*>& armies, double zoom, Id sel, float figure) {
  return cutMarks(buildMarkTree(armies, sel, figure, 0), zoom);
}

gfx::Pt markAnchor(double X0, double Y0, double ds, Vec2 pos) {
  return {float(std::round((X0 + pos.x * ds) * 4) / 4), float(std::round((Y0 + pos.y * ds) * 4) / 4)};
}

}  // namespace rg::map::detail
