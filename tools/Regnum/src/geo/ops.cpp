// Regnum — операции над провинциями.
//
// Все операции изменения геометрии работают по одной схеме: копия графа (detail::Graph) → вставка линий ввода
// (прилипание, разрезание существующих дуг в точках пересечения, отказ от дублей) → новые грани → перемаркировка →
// очистка (лишние границы, висячие дуги, слияние узлов степени 2) → полная проверка → запись в транзакцию.
// Любая ошибка — rg::fail, транзакция откатывается.
#include "geo/ops.h"

#include <map>
#include <unordered_map>

#include "geo/graph.h"

namespace rg::geo {
using namespace detail;

namespace {

[[noreturn]] void failBuild(const std::string& why) { fail("Не удалось построить границу: " + why); }

// ================================================================ ввод
std::vector<Vec2> cleanInput(const std::vector<Vec2>& in, bool closed) {
  std::vector<Vec2> out;
  out.reserve(in.size());
  for (auto& p : in) {
    if (!finite(p)) fail("Недопустимые координаты точки");
    if (!out.empty() && dist(out.back(), p) < kMinLen) continue;
    out.push_back(p);
  }
  if (closed)
    while (out.size() > 1 && dist(out.front(), out.back()) < kMinLen) out.pop_back();
  return out;
}

// Многоугольник: без повторов, простой, против часовой (внутренность слева).
std::vector<Vec2> preparePolygon(const std::vector<Vec2>& poly) {
  auto p = cleanInput(poly, true);
  if (p.size() < 3) fail("Контур должен содержать не менее трёх точек");
  double a = signedArea(p);
  bool collinear = true;  // все точки на одной прямой (точно)
  for (size_t i = 2; i < p.size() && collinear; i++) collinear = orient(p[0], p[1], p[i]) == 0;
  if (std::fabs(a) < kMinArea && collinear) fail("Контур вырожден: нулевая площадь");
  if (!isSimple(p, true)) fail("Контур пересекает сам себя");
  if (std::fabs(a) < kMinArea) fail("Контур вырожден: нулевая площадь");
  if (a < 0) std::reverse(p.begin(), p.end());
  return p;
}

std::vector<Vec2> prepareLine(const std::vector<Vec2>& line) {
  auto p = cleanInput(line, false);
  if (p.size() < 2) fail("Линия ножа должна содержать не менее двух точек");
  if (!isSimple(p, false)) fail("Линия ножа пересекает сама себя");
  return p;
}

// Точка на осевом отрезке (рамка, ступеньки берега) — точно на его прямой.
Vec2 onAxis(Vec2 p, Vec2 a, Vec2 b) {
  if (a.x == b.x) p.x = a.x;
  if (a.y == b.y) p.y = a.y;
  return p;
}

double snapOf(const EditOptions& opt) {
  double s = opt.snap;
  if (!std::isfinite(s) || s < kMinLen) s = kMinLen;
  return std::min(s, 1000.0);
}

// ================================================================ помощники графа
inline auto halfSide(const Graph& g) {
  return [&g](int h) { return g.edges[size_t(h >> 1)].side((h & 1) == 0); };
}

std::vector<Side> faceSides(const Graph& g, const FaceBuild& fb) {
  std::vector<Side> out(static_cast<size_t>(fb.nfaces()));
  auto hs = halfSide(g);
  for (int f = 0; f < fb.nfaces(); f++) out[size_t(f)] = faceSide(fb, f, hs);
  return out;
}

void relabelFace(Graph& g, const FaceBuild& fb, int f, Side s) {
  fb.eachHalf(f, [&](int h) { g.edges[size_t(h >> 1)].setSide((h & 1) == 0, s); });
}

void requireMap(const Graph& g) {
  if (g.frameBox().empty()) fail("Карта ещё не создана: нет рамки и береговой линии");
}

// Проверить итоговый граф и записать в транзакцию.
void finalize(Tx& tx, const Graph& g) {
  auto issues = checkGraph(g, [&](Id p) { return tx.w().provinces.has(p); }, 1);
  if (!issues.empty()) failBuild(issues[0].msg);
  g.store(tx);
}

// ---------------------------------------------------------------- очистка
// Удалить пограничные дуги с одинаковыми сторонами и висячие пограничные дуги, слить узлы степени 2
// (одинаковый вид дуг и согласованные метки; петли не создаются; углы рамки сохраняются), убрать изолированные узлы.
void cleanup(Graph& g) {
  const int nn = int(g.nodes.size());
  std::vector<std::vector<int>> inc(static_cast<size_t>(nn));
  std::vector<int> deg(size_t(nn), 0);
  for (int e = 0; e < int(g.edges.size()); e++) {
    if (!g.valid(e)) continue;
    const GEdge& E = g.edges[size_t(e)];
    inc[size_t(E.a)].push_back(e);
    if (E.b != E.a) inc[size_t(E.b)].push_back(e);
    deg[size_t(E.a)]++;
    deg[size_t(E.b)]++;
  }
  auto kill = [&](int e) {
    GEdge& E = g.edges[size_t(e)];
    E.alive = false;
    deg[size_t(E.a)]--;
    deg[size_t(E.b)]--;
  };
  for (int e = 0; e < int(g.edges.size()); e++) {
    if (!g.valid(e)) continue;
    const GEdge& E = g.edges[size_t(e)];
    if (E.kind == EdgeKind::Border && E.side(true) == E.side(false)) kill(e);
  }
  // висячие пограничные дуги
  std::vector<int> st;
  for (int n = 0; n < nn; n++)
    if (g.nodes[size_t(n)].alive && deg[size_t(n)] == 1) st.push_back(n);
  while (!st.empty()) {
    int n = st.back();
    st.pop_back();
    if (deg[size_t(n)] != 1) continue;
    for (int e : inc[size_t(n)]) {
      if (!g.valid(e)) continue;
      GEdge& E = g.edges[size_t(e)];
      if (E.a != n && E.b != n) continue;
      if (E.kind != EdgeKind::Border) continue;
      int o = E.a == n ? E.b : E.a;
      kill(e);
      if (deg[size_t(o)] == 1) st.push_back(o);
      break;
    }
  }
  // слияние узлов степени 2
  Box2 fb = g.frameBox();
  auto corner = [&](Vec2 p) { return (p.x == fb.x0 || p.x == fb.x1) && (p.y == fb.y0 || p.y == fb.y1); };
  auto ageKey = [&](int n) { return g.nodes[size_t(n)].id ? u64(g.nodes[size_t(n)].id) : ~u64(0); };  // новый — «младше» всех
  // Дуги узла (без повторов); false — петля или не две дуги.
  auto twoEnds = [&](int n, int& e1, int& e2, EdgeKind* only) {
    int ec = 0;
    for (int e : inc[size_t(n)]) {
      if (!g.valid(e)) continue;
      const GEdge& E = g.edges[size_t(e)];
      if (E.a != n && E.b != n) continue;
      if (only && E.kind != *only) continue;
      if (E.a == n && E.b == n) return false;
      if (ec == 1 && e1 == e) continue;
      if (ec == 2 && (e1 == e || e2 == e)) continue;
      if (ec == 0) e1 = e;
      else if (ec == 1) e2 = e;
      ec++;
    }
    return ec == 2;
  };
  // Опорный узел берегового кольца: один из двух старейших узлов кольца (их создаёт initFromCoast).
  // Он не сливается, иначе после снятия границ на кольце остались бы узлы-точки разрезов.
  auto coastAnchor = [&](int n) {
    EdgeKind coast = EdgeKind::Coast;
    int e1 = -1, e2 = -1;
    if (!twoEnds(n, e1, e2, &coast)) return false;
    u64 a1 = ~u64(0), a2 = ~u64(0);  // два наименьших возраста на кольце
    auto note = [&](int v) {
      u64 k = ageKey(v);
      if (k < a1) { a2 = a1; a1 = k; } else if (k < a2) a2 = k;
    };
    int cur = n, e = e1;
    for (size_t guard = 0; guard <= g.edges.size(); guard++) {
      note(cur);
      const GEdge& E = g.edges[size_t(e)];
      int nx = E.a == cur ? E.b : E.a;
      if (nx == n) {
        u64 k = ageKey(n);
        return k == a1 || k == a2;
      }
      int f1 = -1, f2 = -1;
      if (!twoEnds(nx, f1, f2, &coast)) return false;  // цепочка упирается в рамку: опоры не нужны
      e = f1 == e ? f2 : f1;
      cur = nx;
    }
    return false;
  };
  // Порядок: сначала узлы — точки разрезов (лежат на прямой между соседями), затем остальные;
  // внутри прохода — от новых узлов к старым.
  std::vector<int> order(static_cast<size_t>(nn));
  for (int n = 0; n < nn; n++) order[size_t(n)] = n;
  std::sort(order.begin(), order.end(), [&](int x, int y) { return ageKey(x) != ageKey(y) ? ageKey(x) > ageKey(y) : x > y; });
  auto tryMerge = [&](int n, bool cutPointsOnly) {
    if (!g.nodes[size_t(n)].alive || deg[size_t(n)] != 2) return false;
    int e1 = -1, e2 = -1;
    if (!twoEnds(n, e1, e2, nullptr)) return false;
    const GEdge &E1 = g.edges[size_t(e1)], &E2 = g.edges[size_t(e2)];
    if (E1.kind != E2.kind) return false;
    if (E1.kind == EdgeKind::Frame && corner(g.nodes[size_t(n)].p)) return false;
    GEdge A = E1, B = E2;
    bool revA = A.b != n, revB = B.a != n;
    if (revA) A.reverse();
    if (revB) B.reverse();
    if (A.a == B.b) return false;  // получилась бы петля
    if (!(A.side(true) == B.side(true)) || !(A.side(false) == B.side(false))) return false;
    // точка узла остаётся вершиной дуги, если не лежит на прямой между соседями (точки разрезов — лежат)
    Vec2 np = g.nodes[size_t(n)].p;
    Vec2 prev = A.pts.empty() ? g.nodes[size_t(A.a)].p : A.pts.back();
    Vec2 next = B.pts.empty() ? g.nodes[size_t(B.b)].p : B.pts.front();
    bool keepPoint = distToSeg(np, prev, next) > 1e-9;
    if (cutPointsOnly && keepPoint) return false;
    if (keepPoint && E1.kind == EdgeKind::Coast && coastAnchor(n)) return false;
    GEdge M = A;
    M.b = B.b;
    if (keepPoint) M.pts.push_back(np);
    M.pts.insert(M.pts.end(), B.pts.begin(), B.pts.end());
    // сохраняется дуга с меньшим ненулевым ID и её направление
    bool keepFirst = E1.id != 0 && (E2.id == 0 || E1.id < E2.id);
    if (E1.id == 0 && E2.id == 0) keepFirst = e1 < e2;
    int keep = keepFirst ? e1 : e2, drop = keepFirst ? e2 : e1;
    if (keepFirst ? revA : revB) M.reverse();
    M.id = g.edges[size_t(keep)].id;
    g.edges[size_t(keep)] = std::move(M);
    g.edges[size_t(drop)].alive = false;
    g.nodes[size_t(n)].alive = false;
    deg[size_t(n)] = 0;
    const GEdge& K = g.edges[size_t(keep)];
    inc[size_t(K.a)].push_back(keep);
    inc[size_t(K.b)].push_back(keep);
    return true;
  };
  for (int pass = 0; pass < 2; pass++) {
    bool changed = true;
    while (changed) {
      changed = false;
      for (int n : order) changed = tryMerge(n, pass == 0) || changed;
    }
  }
  for (int n = 0; n < nn; n++)
    if (g.nodes[size_t(n)].alive && deg[size_t(n)] == 0) g.nodes[size_t(n)].alive = false;
}

// ================================================================ вставка линий в граф
struct Att {
  enum Kind : u8 { Free, Node, Vertex, OnSeg } kind = Free;
  int node = -1;
  int edge = -1, k = -1;  // Vertex: индекс координаты дуги; OnSeg: индекс отрезка
  double u = 0;           // OnSeg: параметр на отрезке
  bool same(const Att& o) const {
    if (kind != o.kind) return false;
    switch (kind) {
      case Free: return false;
      case Node: return node == o.node;
      case Vertex: return edge == o.edge && k == o.k;
      case OnSeg: return edge == o.edge && k == o.k && u == o.u;
    }
    return false;
  }
};
struct PPt {
  Vec2 p;
  Att att;
  int node = -1;
};
struct PathIn {
  std::vector<Vec2> pts;
  bool closed = false;
};
struct PathArc {
  int edge = -1;
  bool same = true;   // направление дуги совпадает с направлением линии ввода
  bool fresh = false; // новая дуга (иначе — существующая дуга, по которой идёт линия)
};
struct InsertResult {
  std::vector<std::vector<PathArc>> arcs;
  std::vector<std::vector<Vec2>> paths;  // итоговая геометрия линий (после прилипания)
};

class Inserter {
 public:
  Inserter(Graph& g, double snap) : g_(g), snap_(std::max(snap, kMinLen)) {}

  // keep(староеЛицо) — оставлять ли новую дугу, лежащую в этой грани (nullptr — все).
  InsertResult run(const std::vector<PathIn>& paths, const FaceBuild& oldF, const std::vector<Side>& oldSide, EdgeKind kind,
                   const std::function<bool(int)>& keep) {
    buildIndex();
    const size_t np = paths.size();
    std::vector<std::vector<PPt>> P;
    std::vector<bool> closed(np);
    for (size_t i = 0; i < np; i++) closed[i] = paths[i].closed;
    // Прилипание не должно портить ввод: если линия после него касается себя или дважды проходит через
    // одну вершину, допуск уменьшается (в пределе — без прилипания).
    const double snap0 = snap_;
    for (int attempt = 0;; attempt++) {
      snap_ = attempt < 3 ? snap0 / std::pow(4.0, attempt) : kMinLen;
      snap_ = std::max(snap_, kMinLen);
      overlap_ = false;
      bool collapsed = false;
      P = snapPaths(paths, closed, collapsed);
      if (collapsed) {
        if (attempt < 3) continue;
        fail("Контур слишком мал: его вершины слились при прилипании к границам");
      }
      crossPaths(P, closed);
      bool good = !overlap_ && !repeatedVertex(P) && pathsClear(P, closed);
      if (good) break;
      if (attempt >= 3) {
        if (overlap_) failBuild("линия идёт вдоль существующей границы");
        if (repeatedVertex(P)) failBuild("линия дважды проходит через одну точку существующей границы");
        fail("Линия касается сама себя: раздвиньте её части или уменьшите прилипание");
      }
    }
    snap_ = snap0;
    // 4) разрезание существующих дуг в точках прилипания и пересечения
    struct Cut { int k; double u; Vec2 p; int node; };
    std::map<int, std::vector<Cut>> cuts;
    for (auto& pp : P)
      for (auto& q : pp) {
        if (q.att.kind == Att::Vertex) cuts[q.att.edge].push_back({q.att.k, 0.0, q.p, -1});
        else if (q.att.kind == Att::OnSeg) cuts[q.att.edge].push_back({q.att.k, q.att.u, q.p, -1});
      }
    for (auto& [e, list] : cuts) {
      std::sort(list.begin(), list.end(), [](const Cut& x, const Cut& y) { return x.k < y.k || (x.k == y.k && x.u < y.u); });
      list.erase(std::unique(list.begin(), list.end(), [](const Cut& x, const Cut& y) { return x.k == y.k && x.u == y.u; }),
                 list.end());
      for (size_t c = 0; c < list.size(); c++) {
        if (c > 0 && dist(list[c].p, list[c - 1].p) < kMinLen) list[c].node = list[c - 1].node;
        else list[c].node = g_.addNode(list[c].p);
      }
      splitEdge(e, list);
    }
    auto nodeOf = [&](const Att& a) -> int {
      if (a.kind == Att::Node) return a.node;
      if (a.kind == Att::Free) return -1;
      const auto& list = cuts[a.edge];
      double u = a.kind == Att::Vertex ? 0.0 : a.u;
      for (auto& c : list)
        if (c.k == a.k && c.u == u) return c.node;
      return -1;
    };
    // 5) узлы линий и куски между ними
    std::unordered_map<u64, std::vector<int>> straight;
    for (int e = 0; e < int(g_.edges.size()); e++) {
      if (!g_.valid(e) || !g_.edges[size_t(e)].pts.empty()) continue;
      const GEdge& E = g_.edges[size_t(e)];
      straight[pairKey(E.a, E.b)].push_back(e);
    }
    InsertResult res;
    res.arcs.resize(np);
    res.paths.resize(np);
    for (size_t i = 0; i < np; i++) {
      auto& pp = P[i];
      for (auto& q : pp) {
        q.node = nodeOf(q.att);
        q.p = q.node >= 0 ? g_.nodes[size_t(q.node)].p : q.p;
        res.paths[i].push_back(q.p);
      }
      size_t n = pp.size();
      if (!closed[i]) {
        if (pp.front().node < 0) pp.front().node = g_.addNode(pp.front().p);
        if (pp.back().node < 0) pp.back().node = g_.addNode(pp.back().p);
      } else {
        // не менее двух узлов на кольце (иначе петля)
        int cnt = 0, first = -1;
        for (size_t k = 0; k < n; k++)
          if (pp[k].node >= 0) { cnt++; if (first < 0) first = int(k); }
        if (cnt == 0) {
          pp[0].node = g_.addNode(pp[0].p);
          first = 0;
          cnt = 1;
        }
        if (cnt == 1) {
          // узел в вершине, ближайшей к середине периметра от первого узла
          double per = 0;
          std::vector<double> acc(n + 1, 0);
          for (size_t k = 0; k < n; k++) {
            acc[k + 1] = acc[k] + dist(pp[(size_t(first) + k) % n].p, pp[(size_t(first) + k + 1) % n].p);
          }
          per = acc[n];
          size_t best = 1;
          for (size_t k = 1; k < n; k++)
            if (std::fabs(acc[k] - per * 0.5) < std::fabs(acc[best] - per * 0.5)) best = k;
          size_t j = (size_t(first) + best) % n;
          pp[j].node = g_.addNode(pp[j].p);
        }
        size_t rot = 0;
        while (pp[rot].node < 0) rot++;
        std::rotate(pp.begin(), pp.begin() + long(rot), pp.end());
      }
      size_t last = closed[i] ? n : n - 1;
      size_t start = 0;
      for (size_t j = 1; j <= last; j++) {
        const PPt& q = pp[j % n];
        if (q.node < 0) continue;
        std::vector<Vec2> interior;
        for (size_t k = start + 1; k < j; k++) interior.push_back(pp[k].p);
        piece(pp[start].node, interior, q.node, oldF, oldSide, kind, keep, straight, res.arcs[i]);
        start = j;
      }
    }
    return res;
  }

 private:
  // 1–2) Прилипание вершин ввода и изгиб линии через существующие вершины ближе snap.
  // collapsed — после прилипания у линии осталось слишком мало вершин.
  std::vector<std::vector<PPt>> snapPaths(const std::vector<PathIn>& paths, const std::vector<bool>& closed, bool& collapsed) {
    const size_t np = paths.size();
    std::vector<std::vector<PPt>> P(np);
    for (size_t i = 0; i < np; i++) {
      std::vector<PPt> out;
      for (Vec2 v : paths[i].pts) {
        PPt q;
        q.p = v;
        q.att = snapPoint(q.p);
        if (!out.empty() && (out.back().att.same(q.att) || dist(out.back().p, q.p) < kMinLen)) {
          if (out.back().att.kind == Att::Free && q.att.kind != Att::Free) out.back() = q;
          continue;
        }
        out.push_back(q);
      }
      if (closed[i])
        while (out.size() > 1 && (out.front().att.same(out.back().att) || dist(out.front().p, out.back().p) < kMinLen)) {
          if (out.front().att.kind == Att::Free && out.back().att.kind != Att::Free) out.front() = out.back();
          out.pop_back();
        }
      if (out.size() < (closed[i] ? 3u : 2u)) collapsed = true;
      P[i] = std::move(out);
    }
    // существующие вершины ближе snap к линии становятся её вершинами: каждая — один раз,
    // у ближайшего звена ввода (один проход: изгиб ограничен допуском и не «ползёт» вдоль берега)
    if (collapsed) return P;
    std::vector<std::pair<double, PPt>> ev;
    for (size_t i = 0; i < np; i++) {
      auto& pp = P[i];
      size_t n = pp.size(), ns = closed[i] ? n : n - 1;
      struct Best { size_t seg; double t, d2; PPt q; };
      std::map<std::pair<int, int>, Best> best;  // (узел | дуга, индекс) -> ближайшее звено
      for (size_t s = 0; s < ns; s++) {
        const PPt &A = pp[s], &B = pp[(s + 1) % n];
        ev.clear();
        vertexEvents(A, B, ev);
        for (auto& [t, q] : ev) {
          bool onPath = false;
          for (auto& x : pp) onPath = onPath || x.att.same(q.att);
          if (onPath) continue;
          std::pair<int, int> key = q.att.kind == Att::Node ? std::pair{-1, q.att.node} : std::pair{q.att.edge, q.att.k};
          double d2 = distToSeg2(q.p, A.p, B.p);
          auto it = best.find(key);
          if (it == best.end() || d2 < it->second.d2) best[key] = Best{s, t, d2, q};
        }
      }
      std::vector<std::vector<std::pair<double, PPt>>> perSeg(ns);
      for (auto& [k, b] : best) perSeg[b.seg].push_back({b.t, b.q});
      std::vector<PPt> out;
      bool changed = false;
      for (size_t s = 0; s < ns; s++) {
        out.push_back(pp[s]);
        spliceEvents(perSeg[s], out, changed);
      }
      if (!closed[i]) out.push_back(pp.back());
      pp = std::move(out);
    }
    return P;
  }

  // 3) Точки пересечения с существующими отрезками (геометрия линии не меняется).
  void crossPaths(std::vector<std::vector<PPt>>& P, const std::vector<bool>& closed) {
    std::vector<std::pair<double, PPt>> ev;
    for (size_t i = 0; i < P.size(); i++) {
      auto& pp = P[i];
      size_t n = pp.size(), ns = closed[i] ? n : n - 1;
      std::vector<PPt> out;
      bool changed = false;
      for (size_t s = 0; s < ns; s++) {
        const PPt &A = pp[s], &B = pp[(s + 1) % n];
        out.push_back(A);
        ev.clear();
        crossEvents(A, B, ev);
        spliceEvents(ev, out, changed);
      }
      if (!closed[i]) out.push_back(pp.back());
      pp = std::move(out);
    }
  }

  // Линии простые, не касаются друг друга, и каждая вершина отстоит от несмежных звеньев не меньше kMinLen.
  static bool pathsClear(const std::vector<std::vector<PPt>>& P, const std::vector<bool>& closed) {
    std::vector<Vec2> a, b;
    std::vector<int> pi, si;
    for (size_t i = 0; i < P.size(); i++) {
      size_t n = P[i].size(), ns = closed[i] ? n : n - 1;
      for (size_t k = 0; k < ns; k++) {
        a.push_back(P[i][k].p);
        b.push_back(P[i][(k + 1) % n].p);
        pi.push_back(int(i));
        si.push_back(int(k));
        if (a.back() == b.back()) return false;
      }
    }
    SegGrid g;
    g.build(a, b, {}, 0, 3.0);
    auto adjacent = [&](size_t x, size_t y) {  // соседние звенья одной линии
      if (pi[x] != pi[y]) return false;
      size_t n = P[size_t(pi[x])].size(), ns = closed[size_t(pi[x])] ? n : n - 1;
      size_t u = size_t(si[x]), v = size_t(si[y]);
      return u + 1 == v || v + 1 == u || (closed[size_t(pi[x])] && ((u == 0 && v + 1 == ns) || (v == 0 && u + 1 == ns)));
    };
    std::vector<u32> stamp(a.size(), 0);
    for (size_t x = 0; x < a.size(); x++) {
      Box2 q;
      q.add(a[x]);
      q.add(b[x]);
      bool ok = true;
      g.queryUnique(q.inflated(kMinLen), stamp, u32(x + 1), [&](int yy) {
        size_t y = size_t(yy);
        if (y == x || !ok) return;
        if (adjacent(x, y)) {
          if (segRelation(a[x], b[x], a[y], b[y]) == SegRel::Overlap) ok = false;
          return;
        }
        if (y > x && segRelation(a[x], b[x], a[y], b[y]) != SegRel::None) ok = false;
      });
      if (!ok) return false;
    }
    // вершины не ближе kMinLen к звеньям, концами которых не являются
    for (size_t i = 0; i < P.size(); i++) {
      size_t n = P[i].size();
      for (size_t k = 0; k < n; k++) {
        Vec2 v = P[i][k].p;
        Box2 q(v.x - kMinLen, v.y - kMinLen, v.x + kMinLen, v.y + kMinLen);
        bool ok = true;
        g.query(q, [&](int xx) {
          size_t x = size_t(xx);
          if (!ok) return;
          if (size_t(pi[x]) == i && (size_t(si[x]) == k || (size_t(si[x]) + 1) % n == k)) return;
          if (distToSeg2(v, a[x], b[x]) < kMinLen * kMinLen) ok = false;
        });
        if (!ok) return false;
      }
    }
    return true;
  }

  // Линия проходит через одну вершину или узел дважды.
  static bool repeatedVertex(const std::vector<std::vector<PPt>>& P) {
    std::vector<std::pair<i64, i64>> seen;
    for (auto& pp : P)
      for (auto& q : pp) {
        if (q.att.kind == Att::Node) seen.push_back({-1, q.att.node});
        else if (q.att.kind == Att::Vertex) seen.push_back({q.att.edge, q.att.k});
      }
    std::sort(seen.begin(), seen.end());
    return std::adjacent_find(seen.begin(), seen.end()) != seen.end();
  }

  static u64 pairKey(int a, int b) { return (u64(u32(std::min(a, b))) << 32) | u64(u32(std::max(a, b))); }

  void buildIndex() {
    std::vector<Vec2> a, b;
    for (int e = 0; e < int(g_.edges.size()); e++) {
      if (!g_.valid(e)) continue;
      int m = g_.edges[size_t(e)].ncoords();
      for (int k = 0; k + 1 < m; k++) {
        Vec2 p = g_.coord(e, k), q = g_.coord(e, k + 1);
        if (p == q) continue;
        a.push_back(p);
        b.push_back(q);
        segE_.push_back(e);
        segK_.push_back(k);
      }
    }
    seg_.build(a, b, {}, 0, 2.5);
    std::vector<Vec2> v;
    for (int n = 0; n < int(g_.nodes.size()); n++) {
      if (!g_.nodes[size_t(n)].alive) continue;
      v.push_back(g_.nodes[size_t(n)].p);
      vNode_.push_back(n);
      vEdge_.push_back(-1);
      vK_.push_back(-1);
    }
    for (int e = 0; e < int(g_.edges.size()); e++) {
      if (!g_.valid(e)) continue;
      int m = g_.edges[size_t(e)].ncoords();
      for (int k = 1; k + 1 < m; k++) {
        v.push_back(g_.coord(e, k));
        vNode_.push_back(-1);
        vEdge_.push_back(e);
        vK_.push_back(k);
      }
    }
    vtx_.build(v, v, {}, 0, 2.5);
  }

  Att attOfVertexItem(int i) const {
    Att a;
    if (vNode_[size_t(i)] >= 0) {
      a.kind = Att::Node;
      a.node = vNode_[size_t(i)];
    } else {
      a.kind = Att::Vertex;
      a.edge = vEdge_[size_t(i)];
      a.k = vK_[size_t(i)];
    }
    return a;
  }

  Att attOfCoord(int e, int k) const {
    const GEdge& E = g_.edges[size_t(e)];
    Att a;
    if (k == 0 || k == E.ncoords() - 1) {
      a.kind = Att::Node;
      a.node = k == 0 ? E.a : E.b;
    } else {
      a.kind = Att::Vertex;
      a.edge = e;
      a.k = k;
    }
    return a;
  }

  // Прилипание точки: узел, затем промежуточная точка, затем отрезок (в радиусе snap).
  Att snapPoint(Vec2& v) const {
    double r2 = snap_ * snap_;
    Box2 q(v.x - snap_, v.y - snap_, v.x + snap_, v.y + snap_);
    int bn = -1, bp = -1;
    double dn = kInf, dp = kInf;
    vtx_.query(q, [&](int i) {
      double d = dist2(vtx_.a(i), v);
      if (d > r2) return;
      if (vNode_[size_t(i)] >= 0) {
        if (d < dn || (d == dn && i < bn)) { dn = d; bn = i; }
      } else if (d < dp || (d == dp && i < bp)) {
        dp = d;
        bp = i;
      }
    });
    if (bn >= 0) { v = vtx_.a(bn); return attOfVertexItem(bn); }
    if (bp >= 0) { v = vtx_.a(bp); return attOfVertexItem(bp); }
    int bs = -1;
    double ds = kInf;
    Proj bpj;
    seg_.query(q, [&](int i) {
      Proj pr = project(v, seg_.a(i), seg_.b(i));
      if (pr.d2 > r2) return;
      if (pr.d2 < ds || (pr.d2 == ds && i < bs)) { ds = pr.d2; bs = i; bpj = pr; }
    });
    if (bs < 0) return {};
    int e = segE_[size_t(bs)], k = segK_[size_t(bs)];
    Vec2 a = seg_.a(bs), b = seg_.b(bs);
    if (dist(bpj.p, a) < kMinLen) { v = a; return attOfCoord(e, k); }
    if (dist(bpj.p, b) < kMinLen) { v = b; return attOfCoord(e, k + 1); }
    v = onAxis(bpj.p, a, b);
    Att at;
    at.kind = Att::OnSeg;
    at.edge = e;
    at.k = k;
    at.u = bpj.t;
    return at;
  }

  // Прилегает ли точка ввода к отрезку k дуги e (общая точка — сама точка ввода).
  bool touches(const Att& at, int e, int k) const {
    const GEdge& E = g_.edges[size_t(e)];
    switch (at.kind) {
      case Att::Free: return false;
      case Att::Node: return (k == 0 && E.a == at.node) || (k + 2 == E.ncoords() && E.b == at.node);
      case Att::Vertex: return at.edge == e && (at.k == k || at.k == k + 1);
      case Att::OnSeg: return at.edge == e && at.k == k;
    }
    return false;
  }

  void vertexEvents(const PPt& A, const PPt& B, std::vector<std::pair<double, PPt>>& ev) {
    Box2 q;
    q.add(A.p);
    q.add(B.p);
    q = q.inflated(snap_);
    double r2 = snap_ * snap_;
    vtx_.queryUnique(q, stampV_, ++tag_, [&](int i) {
      Att at = attOfVertexItem(i);
      if (at.same(A.att) || at.same(B.att)) return;
      Vec2 v = vtx_.a(i);
      Proj pr = project(v, A.p, B.p);
      if (pr.d2 > r2 || pr.t <= 0 || pr.t >= 1) return;
      if (dist(v, A.p) < kMinLen || dist(v, B.p) < kMinLen) return;
      PPt q2;
      q2.p = v;
      q2.att = at;
      ev.push_back({pr.t, q2});
    });
  }

  void crossEvents(const PPt& A, const PPt& B, std::vector<std::pair<double, PPt>>& ev) {
    Box2 q;
    q.add(A.p);
    q.add(B.p);
    seg_.queryUnique(q, stampS_, ++tag_, [&](int i) {
      int e = segE_[size_t(i)], k = segK_[size_t(i)];
      if (touches(A.att, e, k) || touches(B.att, e, k)) return;
      Vec2 c0 = seg_.a(i), c1 = seg_.b(i);
      SegRel r = segRelation(A.p, B.p, c0, c1);
      if (r == SegRel::None) return;
      if (r == SegRel::Overlap) { overlap_ = true; return; }
      if (orient(A.p, B.p, c0) == 0 && orient(A.p, B.p, c1) == 0) return;  // касание концами на одной прямой
      double t = 0;
      Vec2 X = crossPoint(A.p, B.p, c0, c1, &t);
      Att at;
      if (dist(X, c0) < kMinLen) { X = c0; at = attOfCoord(e, k); }
      else if (dist(X, c1) < kMinLen) { X = c1; at = attOfCoord(e, k + 1); }
      else {
        at.kind = Att::OnSeg;
        at.edge = e;
        at.k = k;
        X = onAxis(X, c0, c1);
        at.u = project(X, c0, c1).t;
        if (!(at.u > 0 && at.u < 1)) return;
      }
      if (dist(X, A.p) < kTinyLen || dist(X, B.p) < kTinyLen) return;
      if (at.same(A.att) || at.same(B.att)) return;
      t = project(X, A.p, B.p).t;
      PPt q2;
      q2.p = X;
      q2.att = at;
      ev.push_back({t, q2});
    });
  }

  static void spliceEvents(std::vector<std::pair<double, PPt>>& ev, std::vector<PPt>& out, bool& changed) {
    if (ev.empty()) return;
    std::stable_sort(ev.begin(), ev.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    for (auto& [t, q] : ev) {
      if (out.back().att.same(q.att) || dist(out.back().p, q.p) < kTinyLen) continue;
      out.push_back(q);
      changed = true;
    }
  }

  // Разрезать дугу e в точках list (отсортированы, узлы назначены). Первый кусок сохраняет индекс дуги.
  template <class CutList> void splitEdge(int e, const CutList& list) {
    GEdge orig = g_.edges[size_t(e)];
    int m = orig.ncoords();
    struct Piece { int a, b; std::vector<Vec2> pts; };
    std::vector<Piece> pieces;
    int start = orig.a;
    std::vector<Vec2> cur;
    size_t ci = 0, nc = list.size();
    auto close = [&](int node) {
      if (node == start && cur.empty()) return;
      pieces.push_back({start, node, cur});
      start = node;
      cur.clear();
    };
    for (int k = 0; k + 1 < m; k++) {
      if (k > 0) {
        if (ci < nc && list[ci].k == k && list[ci].u == 0) {
          close(list[ci].node);
          while (ci < nc && list[ci].k == k && list[ci].u == 0) ci++;
        } else {
          cur.push_back(g_.coord(e, k));
        }
      }
      while (ci < nc && list[ci].k == k && list[ci].u > 0) {
        close(list[ci].node);
        ci++;
      }
    }
    close(orig.b);
    if (pieces.empty()) return;
    GEdge& E = g_.edges[size_t(e)];
    E.a = pieces[0].a;
    E.b = pieces[0].b;
    E.pts = pieces[0].pts;
    for (size_t i = 1; i < pieces.size(); i++) {
      GEdge x = orig;
      x.a = pieces[i].a;
      x.b = pieces[i].b;
      x.pts = std::move(pieces[i].pts);
      g_.addEdge(std::move(x));
    }
  }

  void piece(int s, const std::vector<Vec2>& interior, int t, const FaceBuild& oldF, const std::vector<Side>& oldSide,
             EdgeKind kind, const std::function<bool(int)>& keep, std::unordered_map<u64, std::vector<int>>& straight,
             std::vector<PathArc>& arcs) {
    if (interior.empty()) {
      if (s == t) return;
      auto it = straight.find(pairKey(s, t));
      if (it != straight.end())
        for (int e : it->second)
          if (g_.valid(e)) {
            arcs.push_back({e, g_.edges[size_t(e)].a == s, false});
            return;
          }
    }
    // точка для определения старой грани — середина самого длинного звена
    Vec2 prev = g_.nodes[size_t(s)].p, sample = prev;
    double best = -1;
    auto seg = [&](Vec2 q) {
      double l = dist2(prev, q);
      if (l > best) { best = l; sample = (prev + q) * 0.5; }
      prev = q;
    };
    for (auto& p : interior) seg(p);
    seg(g_.nodes[size_t(t)].p);
    int f = oldF.locate(sample);
    if (f < 0) return;
    if (keep && !keep(f)) return;
    GEdge ne;
    ne.a = s;
    ne.b = t;
    ne.pts = interior;
    ne.kind = kind;
    ne.setSide(true, oldSide[size_t(f)]);
    ne.setSide(false, oldSide[size_t(f)]);
    int idx = g_.addEdge(std::move(ne));
    arcs.push_back({idx, true, true});
  }

  Graph& g_;
  double snap_;
  SegGrid seg_, vtx_;
  std::vector<int> segE_, segK_, vNode_, vEdge_, vK_;
  std::vector<u32> stampS_, stampV_;
  u32 tag_ = 0;
  bool overlap_ = false;
};

// Проверить, что линии ввода простые и не касаются друг друга.
void checkInputs(const std::vector<PathIn>& paths, const char* msg) {
  std::vector<Vec2> a, b;
  std::vector<int> pi, si;
  for (size_t i = 0; i < paths.size(); i++) {
    const auto& p = paths[i].pts;
    size_t n = p.size(), ns = paths[i].closed ? n : n - 1;
    for (size_t s = 0; s < ns; s++) {
      a.push_back(p[s]);
      b.push_back(p[(s + 1) % n]);
      pi.push_back(int(i));
      si.push_back(int(s));
    }
  }
  SegGrid g;
  g.build(a, b, {}, 0, 3.0);
  std::vector<u32> stamp(a.size(), 0);
  for (size_t i = 0; i < a.size(); i++) {
    Box2 q;
    q.add(a[i]);
    q.add(b[i]);
    g.queryUnique(q, stamp, u32(i + 1), [&](int jj) {
      size_t j = size_t(jj);
      if (j <= i) return;
      SegRel r = segRelation(a[i], b[i], a[j], b[j]);
      if (r == SegRel::None) return;
      if (pi[i] == pi[j] && r == SegRel::Touch) {
        const auto& P = paths[size_t(pi[i])];
        size_t ns = P.closed ? P.pts.size() : P.pts.size() - 1;
        int x = si[i], y = si[j];
        if (y == x + 1 || (P.closed && x == 0 && size_t(y) + 1 == ns)) return;
      }
      fail(msg);
    });
  }
}

// ---------------------------------------------------------------- внутри/снаружи контура
// inside[грань]: 1 — внутри, 0 — снаружи. Дуги контура задают стороны напрямую (внутренность слева от направления
// линии), остальное распространяется через дуги, не лежащие на контуре; несвязанные части — по чёт-нечет.
std::vector<signed char> classifyInside(const Graph& g, const FaceBuild& fb, const std::vector<PathArc>& arcs,
                                        const std::vector<std::vector<Vec2>>& rings) {
  const int nf = fb.nfaces();
  std::vector<signed char> st(size_t(nf), -1);
  std::vector<char> onPath(g.edges.size(), 0);
  for (auto& a : arcs) onPath[size_t(a.edge)] = 1;
  std::vector<int> queue;
  auto setF = [&](int f, signed char v) {
    if (f < 0) return;
    if (st[size_t(f)] >= 0) {
      if (st[size_t(f)] != v) failBuild("контур неоднозначно делит карту");
      return;
    }
    st[size_t(f)] = v;
    queue.push_back(f);
  };
  for (auto& a : arcs) {
    int hl = 2 * a.edge + (a.same ? 0 : 1);
    setF(fb.faceOfHalf(hl), 1);
    setF(fb.faceOfHalf(hl ^ 1), 0);
  }
  auto spread = [&]() {
    while (!queue.empty()) {
      int f = queue.back();
      queue.pop_back();
      fb.eachHalf(f, [&](int h) {
        if (onPath[size_t(h >> 1)]) return;
        setF(fb.faceOfHalf(h ^ 1), st[size_t(f)]);
      });
    }
  };
  spread();
  for (int f = 0; f < nf; f++) {
    if (st[size_t(f)] >= 0) continue;
    Vec2 sample;
    double best = -1;
    fb.eachHalf(f, [&](int h) {
      int e = h >> 1, m = g.edges[size_t(e)].ncoords();
      for (int k = 0; k + 1 < m; k++) {
        Vec2 p = g.coord(e, k), q = g.coord(e, k + 1);
        double l = dist2(p, q);
        if (l > best) { best = l; sample = (p + q) * 0.5; }
      }
    });
    bool in = false;
    for (auto& r : rings)
      if (pointInRing(sample, r)) in = !in;
    setF(f, in ? 1 : 0);
    spread();
  }
  return st;
}

// ================================================================ операции с контуром
enum class PolyMode { Create, Add, Remove };

// Новая провинция: щель — свободный участок её рельефа вне контура, отрезанный контуром
// (граничит с новой провинцией), прилегающий к соседней провинции того же рельефа и лежащий у контура (в его
// габаритах с запасом). Если она уже kPocketSnaps × 2 допусков прилипания (вписанный круг радиусом не больше
// kPocketSnaps допусков), это след неточной обводки вдоль границы соседа — щель достаётся новой провинции.
// Широкие участки и узкие проходы, уходящие далеко от контура, остаются свободными.
constexpr double kPocketSnaps = 1.5;

// Присоединить щели к новой провинции pid (грани taken — уже её). near — габариты контура.
void absorbPockets(Graph& g, const FaceInput& in, const FaceBuild& fb, const std::vector<signed char>& inside,
                   const std::vector<Side>& side, const std::vector<char>& taken, Id pid, Terrain ter, double snap,
                   Box2 near) {
  const double limit = kPocketSnaps * snap;
  near = near.inflated(2 * limit);
  std::vector<int> pockets;
  for (int f = 0; f < fb.nfaces(); f++) {
    const Side s = side[size_t(f)];
    if (inside[size_t(f)] == 1 || taken[size_t(f)] || s.prov != 0 || s.ter != ter) continue;
    const Box2& fbox = fb.faceBox[size_t(f)];
    if (!(fbox.x0 >= near.x0 && fbox.x1 <= near.x1 && fbox.y0 >= near.y0 && fbox.y1 <= near.y1)) continue;
    bool byNew = false, byOther = false;
    fb.eachHalf(f, [&](int h) {
      int o = fb.faceOfHalf(h ^ 1);
      if (o < 0) return;
      if (taken[size_t(o)]) byNew = true;
      else if (side[size_t(o)].prov != 0 && side[size_t(o)].ter == ter) byOther = true;
    });
    if (!byNew || !byOther) continue;
    std::vector<std::vector<Vec2>> rings{fb.cycleCoords(in, fb.faceOuter[size_t(f)])};
    for (int c : fb.faceHoles[size_t(f)]) rings.push_back(fb.cycleCoords(in, c));
    double r = 0;
    polylabel(rings, limit * 0.25, &r);
    if (r <= limit) pockets.push_back(f);
  }
  for (int f : pockets) relabelFace(g, fb, f, Side{pid, ter});
}

Id polyOp(Tx& tx, PolyMode mode, Id prov, const std::vector<Vec2>& polyIn, Terrain terrain, double snap) {
  if (mode != PolyMode::Create && !tx.w().province(prov)) fail("Провинция не найдена");
  Graph g;
  g.load(tx.w());
  requireMap(g);
  std::vector<Vec2> poly = preparePolygon(polyIn);
  FaceInput in0 = FaceInput::from(g);
  FaceBuild f0 = buildFaceCore(in0);
  std::vector<Side> side0 = faceSides(g, f0);
  Terrain ter = terrain;
  if (mode == PolyMode::Create) {
    if (ter == Terrain::None) {
      int f = f0.locate(poly[0]);
      if (f < 0) fail("Первая точка контура лежит вне карты");
      ter = side0[size_t(f)].ter;
    }
    if (ter == Terrain::None) fail("Не удалось определить сушу или море");
  } else if (mode == PolyMode::Add) {
    ter = tx.w().province(prov)->sea ? Terrain::Sea : Terrain::Land;
  }
  Inserter ins(g, snap);
  InsertResult res = ins.run({PathIn{poly, true}}, f0, side0, EdgeKind::Border, nullptr);
  FaceInput in1 = FaceInput::from(g);
  FaceBuild f1 = buildFaceCore(in1);
  auto inside = classifyInside(g, f1, res.arcs[0], res.paths);
  std::vector<Side> side1 = faceSides(g, f1);
  Id pid = prov;
  if (mode == PolyMode::Create) {
    Province rec;
    rec.sea = ter == Terrain::Sea;
    pid = tx.add(std::move(rec)).id;
  }
  // Новая провинция берёт только свободную (не назначенную) площадь своего рельефа: соседи не уменьшаются,
  // зашедшие на них части контура удаляет очистка, и граница идёт по их границам. Расширение забирает у соседей.
  double changedArea = 0, terArea = 0;
  std::vector<char> taken(size_t(f1.nfaces()), 0);
  for (int f = 0; f < f1.nfaces(); f++) {
    if (inside[size_t(f)] != 1) continue;
    Side s = side1[size_t(f)];
    if (s.ter == ter) terArea += f1.faceArea[size_t(f)];
    bool take = mode == PolyMode::Remove ? s.prov == prov
                : mode == PolyMode::Create ? (s.ter == ter && s.prov == 0)
                                           : (s.ter == ter && s.prov != pid);
    if (!take) continue;
    relabelFace(g, f1, f, Side{mode == PolyMode::Remove ? Id(0) : pid, s.ter});
    taken[size_t(f)] = 1;
    changedArea += f1.faceArea[size_t(f)];
  }
  if (!(changedArea > kMinArea)) {
    if (mode == PolyMode::Create) {
      if (terArea > kMinArea) fail(ter == Terrain::Sea ? "Внутри контура нет свободного моря: оно уже разделено между провинциями"
                                                       : "Внутри контура нет свободной суши: она уже разделена между провинциями");
      fail(ter == Terrain::Sea ? "Контур не захватывает море" : "Контур не захватывает сушу");
    }
    if (mode == PolyMode::Add) fail("Контур не добавляет провинции новой территории");
    fail("Контур не задевает провинцию");
  }
  if (mode == PolyMode::Create) absorbPockets(g, in1, f1, inside, side1, taken, pid, ter, snap, bounds(res.paths[0]));
  cleanup(g);
  finalize(tx, g);
  return pid;
}

}  // namespace

// ================================================================ публичные операции
void initFromCoast(Tx& tx, const Coast& coast) {
  if (!tx.w().nodes.empty() || !tx.w().edges.empty()) fail("Граница карты уже построена");
  double W = coast.width, H = coast.height;
  if (!(std::isfinite(W) && std::isfinite(H) && W > 0 && H > 0)) fail("Неверный размер карты");
  Graph g;
  int c0 = g.addNode({0, 0}), c1 = g.addNode({W, 0}), c2 = g.addNode({W, H}), c3 = g.addNode({0, H});
  // Рамка против часовой (математически): внутренность слева.
  for (auto [a, b] : {std::pair{c0, c1}, std::pair{c1, c2}, std::pair{c2, c3}, std::pair{c3, c0}}) {
    GEdge e;
    e.a = a;
    e.b = b;
    e.kind = EdgeKind::Frame;
    e.setSide(true, Side{0, Terrain::Sea});
    e.setSide(false, Side{0, Terrain::None});
    g.addEdge(std::move(e));
  }
  std::vector<PathIn> rings;
  for (auto& r : coast.landRings) {
    auto p = cleanInput(r, true);
    if (p.size() < 3) continue;
    if (!isSimple(p, true)) fail("Береговая линия пересекает сама себя");
    double a = signedArea(p);
    if (std::fabs(a) < kMinArea) continue;
    if (a < 0) std::reverse(p.begin(), p.end());
    rings.push_back(PathIn{std::move(p), true});
  }
  if (!rings.empty()) {
    checkInputs(rings, "Береговая линия пересекает сама себя или соседний участок суши");
    FaceInput in0 = FaceInput::from(g);
    FaceBuild f0 = buildFaceCore(in0);
    std::vector<Side> side0 = faceSides(g, f0);
    Inserter ins(g, 0.5);
    InsertResult res = ins.run(rings, f0, side0, EdgeKind::Coast, nullptr);
    std::vector<PathArc> all;
    for (auto& a : res.arcs) all.insert(all.end(), a.begin(), a.end());
    FaceInput in1 = FaceInput::from(g);
    FaceBuild f1 = buildFaceCore(in1);
    auto inside = classifyInside(g, f1, all, res.paths);
    for (int f = 0; f < f1.nfaces(); f++)
      relabelFace(g, f1, f, Side{0, inside[size_t(f)] == 1 ? Terrain::Land : Terrain::Sea});
  }
  cleanup(g);
  finalize(tx, g);
}

Id createProvince(Tx& tx, const std::vector<Vec2>& poly, Terrain terrain, const EditOptions& opt) {
  return polyOp(tx, PolyMode::Create, 0, poly, terrain, snapOf(opt));
}
Id createProvince(Tx& tx, const std::vector<Vec2>& poly, Terrain terrain) {
  return createProvince(tx, poly, terrain, EditOptions{});
}
void addArea(Tx& tx, Id province, const std::vector<Vec2>& poly, const EditOptions& opt) {
  polyOp(tx, PolyMode::Add, province, poly, Terrain::None, snapOf(opt));
}
void addArea(Tx& tx, Id province, const std::vector<Vec2>& poly) { addArea(tx, province, poly, EditOptions{}); }
void removeArea(Tx& tx, Id province, const std::vector<Vec2>& poly, const EditOptions& opt) {
  polyOp(tx, PolyMode::Remove, province, poly, Terrain::None, snapOf(opt));
}
void removeArea(Tx& tx, Id province, const std::vector<Vec2>& poly) { removeArea(tx, province, poly, EditOptions{}); }

Id fillAt(Tx& tx, Vec2 p, Id province) {
  if (!finite(p)) fail("Недопустимые координаты точки");
  Graph g;
  g.load(tx.w());
  requireMap(g);
  FaceInput in = FaceInput::from(g);
  FaceBuild fb = buildFaceCore(in);
  int f = fb.locate(p);
  if (f < 0) fail("Точка вне карты");
  Side s = faceSide(fb, f, halfSide(g));
  Id pid = province;
  if (pid == 0) {
    Province rec;
    rec.sea = s.ter == Terrain::Sea;
    pid = tx.add(std::move(rec)).id;
  } else {
    const Province* rec = tx.w().province(pid);
    if (!rec) fail("Провинция не найдена");
    Terrain want = rec->sea ? Terrain::Sea : Terrain::Land;
    if (s.ter != want) fail(rec->sea ? "Морской провинции можно назначить только море" : "Сухопутной провинции можно назначить только сушу");
    if (s.prov == pid) return pid;
  }
  relabelFace(g, fb, f, Side{pid, s.ter});
  cleanup(g);
  finalize(tx, g);
  return pid;
}

double paintTerrain(Tx& tx, const std::vector<Vec2>& polyIn, Terrain ter, const EditOptions& opt) {
  if (ter != Terrain::Land && ter != Terrain::Sea) fail("Контур может сделать только сушу или море");
  Graph g;
  g.load(tx.w());
  requireMap(g);
  std::vector<Vec2> poly = preparePolygon(polyIn);
  FaceInput in0 = FaceInput::from(g);
  FaceBuild f0 = buildFaceCore(in0);
  std::vector<Side> side0 = faceSides(g, f0);
  Inserter ins(g, snapOf(opt));
  InsertResult res = ins.run({PathIn{poly, true}}, f0, side0, EdgeKind::Border, nullptr);
  FaceInput in1 = FaceInput::from(g);
  FaceBuild f1 = buildFaceCore(in1);
  auto inside = classifyInside(g, f1, res.arcs[0], res.paths);
  std::vector<Side> side1 = faceSides(g, f1);
  double changed = 0;
  for (int f = 0; f < f1.nfaces(); f++) {
    if (inside[size_t(f)] != 1) continue;
    const Side s = side1[size_t(f)];
    if (s.ter == ter || s.ter == Terrain::None) continue;
    // Рельеф граней провинции согласован с её типом: провинция другого типа теряет эту площадь.
    Id prov = s.prov;
    if (const Province* p = prov ? tx.w().province(prov) : nullptr; !p || p->sea != (ter == Terrain::Sea)) prov = 0;
    relabelFace(g, f1, f, Side{prov, ter});
    changed += f1.faceArea[size_t(f)];
  }
  if (!(changed > kMinArea)) fail(ter == Terrain::Land ? "Контур не захватывает море" : "Контур не захватывает сушу");
  // Берег — там, где рельеф по сторонам дуги разный; прочие дуги — границы (лишние уберёт очистка).
  for (GEdge& e : g.edges) {
    if (!e.alive || e.kind == EdgeKind::Frame) continue;
    e.kind = (e.tl == Terrain::Land) != (e.tr == Terrain::Land) ? EdgeKind::Coast : EdgeKind::Border;
  }
  cleanup(g);
  finalize(tx, g);
  return changed;
}

Id split(Tx& tx, Id province, const std::vector<Vec2>& lineIn, const NewProvinceFn& makeNew, const EditOptions& opt) {
  const Province* src = tx.w().province(province);
  if (!src) fail("Провинция не найдена");
  const bool sea = src->sea;
  std::vector<Vec2> line = prepareLine(lineIn);
  // Запись новой провинции создаётся до чтения графа: при любом отказе транзакция откатится целиком,
  // а если makeNew (вопреки договору) изменит границы, итоговая проверка учтёт и это.
  Id nid;
  if (makeNew) {
    nid = makeNew(tx, province);
  } else {
    Province rec;
    rec.sea = sea;
    nid = tx.add(std::move(rec)).id;
  }
  if (nid == 0 || nid == province || !tx.w().province(nid)) fail("Не удалось создать провинцию для отрезанной части");
  if (tx.w().province(nid)->sea != sea) fail("Отрезанная часть должна остаться того же типа (суша или море), что и исходная провинция");
  Graph g;
  g.load(tx.w());
  requireMap(g);
  FaceInput in0 = FaceInput::from(g);
  FaceBuild f0 = buildFaceCore(in0);
  std::vector<Side> side0 = faceSides(g, f0);
  bool any = false;
  for (auto& s : side0) any = any || s.prov == province;
  if (!any) fail("У провинции нет территории на карте");
  Inserter ins(g, snapOf(opt));
  InsertResult res = ins.run({PathIn{line, false}}, f0, side0, EdgeKind::Border,
                             [&](int f) { return side0[size_t(f)].prov == province; });
  FaceInput in1 = FaceInput::from(g);
  FaceBuild f1 = buildFaceCore(in1);
  const int nf = f1.nfaces();
  // Грани по сторонам ножа: двухцветная раскраска графа «грань — дуга ножа — грань».
  std::vector<std::vector<int>> adj(static_cast<size_t>(nf));
  std::vector<std::pair<int, int>> pairs;
  for (auto& a : res.arcs[0]) {
    if (!a.fresh) continue;
    int hl = 2 * a.edge + (a.same ? 0 : 1);
    int fl = f1.faceOfHalf(hl), fr = f1.faceOfHalf(hl ^ 1);
    if (fl < 0 || fr < 0 || fl == fr) continue;
    adj[size_t(fl)].push_back(fr);
    adj[size_t(fr)].push_back(fl);
    pairs.push_back({fl, fr});
  }
  if (pairs.empty()) fail("Линия ножа должна пересекать провинцию от края до края");
  std::vector<signed char> color(size_t(nf), -1);
  for (auto [fl, fr] : pairs) {
    if (color[size_t(fl)] >= 0) continue;
    std::vector<int> st{fl};
    color[size_t(fl)] = 0;
    while (!st.empty()) {
      int f = st.back();
      st.pop_back();
      for (int o : adj[size_t(f)]) {
        if (color[size_t(o)] < 0) {
          color[size_t(o)] = static_cast<signed char>(1 - color[size_t(f)]);
          st.push_back(o);
        } else if (color[size_t(o)] == color[size_t(f)]) {
          fail("Линия ножа разрезает провинцию неоднозначно");
        }
      }
    }
    (void)fr;
  }
  double area[2] = {0, 0};
  for (int f = 0; f < nf; f++)
    if (color[size_t(f)] >= 0) area[size_t(color[size_t(f)])] += f1.faceArea[size_t(f)];
  if (!(area[0] > kMinArea) || !(area[1] > kMinArea)) fail("Линия ножа должна пересекать провинцию от края до края");
  int pick = area[1] <= area[0] ? 1 : 0;
  std::vector<Side> side1 = faceSides(g, f1);
  for (int f = 0; f < nf; f++)
    if (color[size_t(f)] == pick) relabelFace(g, f1, f, Side{nid, side1[size_t(f)].ter});
  cleanup(g);
  finalize(tx, g);
  return nid;
}
Id split(Tx& tx, Id province, const std::vector<Vec2>& line, const NewProvinceFn& makeNew) {
  return split(tx, province, line, makeNew, EditOptions{});
}

void merge(Tx& tx, Id target, Id source) {
  if (target == source) fail("Нельзя объединить провинцию с самой собой");
  const Province* t = tx.w().province(target);
  const Province* s = tx.w().province(source);
  if (!t || !s) fail("Провинция не найдена");
  // Рельеф граней провинции согласован с флагом sea (так поддерживают все операции): смешивать нельзя.
  if (t->sea != s->sea) fail("Нельзя объединить сухопутную провинцию с морской");
  Graph g;
  g.load(tx.w());
  for (auto& e : g.edges) {
    if (e.pl == source) e.pl = target;
    if (e.pr == source) e.pr = target;
  }
  cleanup(g);
  finalize(tx, g);
}

void unassign(Tx& tx, Id province) {
  if (province == 0) return;
  Graph g;
  g.load(tx.w());
  for (auto& e : g.edges) {
    if (e.pl == province) e.pl = 0;
    if (e.pr == province) e.pr = 0;
  }
  cleanup(g);
  finalize(tx, g);
}

}  // namespace rg::geo
