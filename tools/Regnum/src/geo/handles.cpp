// Regnum — ручки правки границ и берега (режимы «Правка границ» и «Правка карты»): попадание, блокировка, проверка
// и перемещение точек и узлов без пересечений, вставка и удаление точек, скольжение стыка границы с берегом.
// Изменения пишутся прямо в узлы и дуги транзакции (без пересборки граней, как в geo/ops.cpp); общее с ней — метки
// сторон дуги (detail::Side).
#include <map>
#include <unordered_map>

#include "geo/graph.h"
#include "geo/ops.h"

namespace rg::geo {
using detail::Side;

// ================================================================ ручки
namespace {

// Тождество вершины: узел или промежуточная точка дуги (idx — индекс в pts). Особые значения idx < -1 — новые вершины.
struct VKey {
  Id node = 0;
  Id edge = 0;
  int idx = -1;
  bool operator==(const VKey&) const = default;
};
VKey nodeKey(Id n) { return VKey{n, 0, -1}; }

// Обойти координаты дуги мира: f(k, p, m). false — у дуги нет узла.
template <class F> bool forCoords(const World& w, const Edge& e, F&& f) {
  const Node* a = w.nodes.get(e.a);
  const Node* b = w.nodes.get(e.b);
  if (!a || !b) return false;
  int m = int(e.pts.size()) + 2;
  f(0, a->p, m);
  for (int k = 0; k < int(e.pts.size()); k++) f(k + 1, e.pts[size_t(k)], m);
  f(m - 1, b->p, m);
  return true;
}
std::vector<Vec2> coordsOf(const World& w, const Edge& e) {
  std::vector<Vec2> c;
  forCoords(w, e, [&](int, Vec2 p, int) { c.push_back(p); });
  return c;
}
VKey keyOfCoord(const Edge& e, int k) {
  int m = int(e.pts.size()) + 2;
  if (k == 0) return nodeKey(e.a);
  if (k == m - 1) return nodeKey(e.b);
  return VKey{0, e.id, k - 1};
}

struct NewSeg { Vec2 a, b; VKey ka, kb; };
// Поворот отрезка вокруг вершины v: луч v→a поворачивается на угол sweep (радианы, со знаком).
// Другие отрезки из v не должны оказаться внутри заметённого угла — иначе меняется порядок дуг в вершине.
struct Pivot {
  VKey k;
  Vec2 v, a;
  double sweep = 0;
};
double wrapPi(double x) {
  while (x > kPi) x -= 2 * kPi;
  while (x <= -kPi) x += 2 * kPi;
  return x;
}
double rayAngle(Vec2 d) { return std::atan2(d.y, d.x); }
Pivot pivotOf(VKey k, Vec2 v, const std::vector<Vec2>& path) {  // path: a, ..., b — положения конца луча
  Pivot p{k, v, path.front(), 0};
  for (size_t i = 0; i + 1 < path.size(); i++) p.sweep += wrapPi(rayAngle(path[i + 1] - v) - rayAngle(path[i] - v));
  return p;
}
bool inSweep(const Pivot& p, Vec2 w) {
  constexpr double eps = 1e-12;
  if (std::fabs(p.sweep) < eps || w == p.v) return false;
  double phi = wrapPi(rayAngle(w - p.v) - rayAngle(p.a - p.v));
  if (p.sweep > 0) {
    if (phi <= 0) phi += 2 * kPi;
    return phi > eps && phi < p.sweep - eps;
  }
  if (phi >= 0) phi -= 2 * kPi;
  return phi < -eps && phi > p.sweep + eps;
}

// Заметаемая область, её собственные вершины-углы (не проверяются на попадание внутрь этой области) и точки поворота.
struct Swept {
  std::vector<Vec2> poly;
  std::vector<VKey> own;
  std::vector<Pivot> pivots;
};
struct SegId {
  Id edge = 0;
  int k = 0;
  bool operator==(const SegId&) const = default;
};

Box2 frameBoxOf(const World& w) {
  Box2 b;
  w.edges.each([&](const Edge& e) {
    if (e.kind != EdgeKind::Frame) return;
    forCoords(w, e, [&](int, Vec2 p, int) { b.add(p); });
  });
  return b;
}

bool contactAllowed(SegRel r, const NewSeg& s, Vec2 c, Vec2 d, VKey kc, VKey kd) {
  if (r == SegRel::None) return true;
  if (r != SegRel::Touch) return false;
  int n = 0;
  bool geom = false;
  if (s.ka == kc) { n++; geom = geom || s.a == c; }
  if (s.ka == kd) { n++; geom = geom || s.a == d; }
  if (s.kb == kc) { n++; geom = geom || s.b == c; }
  if (s.kb == kd) { n++; geom = geom || s.b == d; }
  return n == 1 && geom;
}

// Вершина v (ключ kv) не ближе kMinLen к отрезку ab, концом которого не является.
bool clearOf(Vec2 v, VKey kv, Vec2 a, Vec2 b, VKey ka, VKey kb) {
  if (kv == ka || kv == kb) return true;
  return distToSeg2(v, a, b) >= kMinLen * kMinLen;
}

bool inPolyClosed(Vec2 p, const std::vector<Vec2>& poly) {
  size_t n = poly.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++)
    if (onSegment(p, poly[j], poly[i])) return true;
  return pointInRing(p, poly);
}

// Локальная проверка правки: новые отрезки не пересекают и не касаются чужих (кроме общих вершин),
// заметаемые области не содержат чужих вершин, mustInside — строго внутри рамки.
bool localOk(const World& w, const std::vector<NewSeg>& ns, const std::vector<SegId>& removed,
             const std::vector<Swept>& swept, const std::vector<VKey>& exclude, const std::vector<Vec2>& mustInside) {
  Box2 fb = frameBoxOf(w);
  for (Vec2 p : mustInside) {
    if (!finite(p)) return false;
    if (!fb.empty() && !(p.x > fb.x0 && p.x < fb.x1 && p.y > fb.y0 && p.y < fb.y1)) return false;
  }
  for (auto& s : ns)
    if (!finite(s.a) || !finite(s.b) || dist(s.a, s.b) < kMinLen) return false;
  for (size_t i = 0; i < ns.size(); i++)
    for (size_t j = 0; j < ns.size(); j++) {
      if (i == j) continue;
      const NewSeg &x = ns[i], &y = ns[j];
      if (i < j && !contactAllowed(segRelation(x.a, x.b, y.a, y.b), x, y.a, y.b, y.ka, y.kb)) return false;
      if (!clearOf(y.a, y.ka, x.a, x.b, x.ka, x.kb) || !clearOf(y.b, y.kb, x.a, x.b, x.ka, x.kb)) return false;
    }
  Box2 region;
  for (auto& s : ns) { region.add(s.a); region.add(s.b); }
  region = region.inflated(kMinLen);
  Box2 sweptBox;
  for (auto& sw : swept)
    for (auto& q : sw.poly) sweptBox.add(q);
  bool ok = true;
  w.edges.each([&](const Edge& e) {
    if (!ok) return;
    Vec2 prev;
    forCoords(w, e, [&](int k, Vec2 p, int) {
      if (!ok) return;
      if (k > 0 && !ns.empty()) {
        Vec2 a = prev, b = p;
        bool rem = false;
        for (auto& r : removed) rem = rem || (r.edge == e.id && r.k == k - 1);
        if (!rem && !(std::max(a.x, b.x) < region.x0 || std::min(a.x, b.x) > region.x1 || std::max(a.y, b.y) < region.y0 ||
                      std::min(a.y, b.y) > region.y1)) {
          VKey ka = keyOfCoord(e, k - 1), kb = keyOfCoord(e, k);
          for (auto& s : ns) {
            if (!contactAllowed(segRelation(s.a, s.b, a, b), s, a, b, ka, kb)) { ok = false; return; }
            // зазор: новые вершины от старых отрезков и старые вершины от новых отрезков
            if (!clearOf(s.a, s.ka, a, b, ka, kb) || !clearOf(s.b, s.kb, a, b, ka, kb) || !clearOf(a, ka, s.a, s.b, s.ka, s.kb) ||
                !clearOf(b, kb, s.a, s.b, s.ka, s.kb)) {
              ok = false;
              return;
            }
          }
          for (auto& sw : swept)
            for (auto& pv : sw.pivots)
              if ((ka == pv.k && inSweep(pv, b)) || (kb == pv.k && inSweep(pv, a))) { ok = false; return; }
        }
      }
      prev = p;
    });
  });
  if (!ok || swept.empty()) return ok;
  auto test = [&](Vec2 p, VKey k) {
    if (!sweptBox.contains(p)) return true;
    for (auto& x : exclude)
      if (x == k) return true;
    for (auto& sw : swept) {
      if (std::find(sw.own.begin(), sw.own.end(), k) != sw.own.end()) continue;
      if (inPolyClosed(p, sw.poly)) return false;
    }
    return true;
  };
  w.nodes.each([&](const Node& n) { if (ok && !test(n.p, nodeKey(n.id))) ok = false; });
  w.edges.each([&](const Edge& e) {
    for (int i = 0; ok && i < int(e.pts.size()); i++)
      if (!test(e.pts[size_t(i)], VKey{0, e.id, i})) ok = false;
  });
  return ok;
}

struct Star {
  std::vector<const Edge*> edges;
  int coast = 0, frame = 0, border = 0;
};
Star starOf(const World& w, Id node) {
  Star s;
  w.edges.each([&](const Edge& e) {
    int ends = int(e.a == node) + int(e.b == node);
    if (!ends) return;
    s.edges.push_back(&e);
    (e.kind == EdgeKind::Coast ? s.coast : (e.kind == EdgeKind::Frame ? s.frame : s.border)) += ends;
  });
  return s;
}

// Новые отрезки и заметаемые треугольники при перемещении ручки в точку to.
bool planMove(const World& w, const Handle& h, Vec2 to, std::vector<NewSeg>& ns, std::vector<SegId>& removed,
              std::vector<Swept>& swept, std::vector<VKey>& exclude) {
  if (h.kind == Handle::Point) {
    const Edge* e = w.edges.get(h.edge);
    if (!e || h.index < 0 || h.index >= int(e->pts.size())) return false;
    auto c = coordsOf(w, *e);
    if (c.empty()) return false;
    int ci = h.index + 1;
    VKey me{0, e->id, h.index};
    Vec2 p0 = c[size_t(ci)], prev = c[size_t(ci - 1)], next = c[size_t(ci + 1)];
    VKey kp = keyOfCoord(*e, ci - 1), kn = keyOfCoord(*e, ci + 1);
    ns.push_back({prev, to, kp, me});
    ns.push_back({to, next, me, kn});
    removed.push_back({e->id, ci - 1});
    removed.push_back({e->id, ci});
    swept.push_back({{prev, p0, to}, {kp}, {pivotOf(kp, prev, {p0, to})}});
    swept.push_back({{next, p0, to}, {kn}, {pivotOf(kn, next, {p0, to})}});
    exclude.push_back(me);
    return true;
  }
  if (h.kind == Handle::Node) {
    const Node* n = w.nodes.get(h.node);
    if (!n) return false;
    VKey me = nodeKey(h.node);
    exclude.push_back(me);
    Star s = starOf(w, h.node);
    if (s.edges.empty()) return false;
    for (const Edge* e : s.edges) {
      auto c = coordsOf(w, *e);
      if (c.empty()) return false;
      int m = int(c.size());
      if (e->a == h.node) {
        VKey kq = keyOfCoord(*e, 1);
        Vec2 q = c[1];
        if (kq == me) q = to;  // прямая петля невозможна (вырожденная дуга)
        ns.push_back({to, q, me, kq});
        removed.push_back({e->id, 0});
        swept.push_back({{q, n->p, to}, {kq}, {pivotOf(kq, q, {n->p, to})}});
      }
      if (e->b == h.node) {
        VKey kq = keyOfCoord(*e, m - 2);
        Vec2 q = c[size_t(m - 2)];
        ns.push_back({q, to, kq, me});
        removed.push_back({e->id, m - 2});
        swept.push_back({{q, n->p, to}, {kq}, {pivotOf(kq, q, {n->p, to})}});
      }
    }
    return true;
  }
  return false;
}

}  // namespace

Handle hitHandle(const World& w, Vec2 p, double tol, Id province, bool coast) {
  if (!finite(p) || !(tol >= 0)) return {};
  struct Cand { Handle h; double d; };
  std::vector<Cand> cands;
  std::vector<Id> seenNodes;
  w.edges.each([&](const Edge& e) {
    if (coast ? e.kind != EdgeKind::Coast : (province != 0 && e.pl != province && e.pr != province)) return;
    for (Id nid : {e.a, e.b}) {
      const Node* n = w.nodes.get(nid);
      if (!n) continue;
      double d = dist(n->p, p);
      if (d > tol || std::find(seenNodes.begin(), seenNodes.end(), nid) != seenNodes.end()) continue;
      seenNodes.push_back(nid);
      Handle h;
      h.kind = Handle::Node;
      h.node = nid;
      cands.push_back({h, d});
    }
    for (int i = 0; i < int(e.pts.size()); i++) {
      double d = dist(e.pts[size_t(i)], p);
      if (d > tol) continue;
      Handle h;
      h.kind = Handle::Point;
      h.edge = e.id;
      h.index = i;
      cands.push_back({h, d});
    }
  });
  if (cands.empty()) return {};
  // Сначала подвижные, затем узлы, затем ближайшие.
  Handle best;
  int bl = 0, bk = 0;
  double bd = kInf;
  for (auto& c : cands) {
    int locked = handleLocked(w, c.h, coast) ? 1 : 0;
    int kind = c.h.kind == Handle::Node ? 0 : 1;
    bool better = !best || locked < bl || (locked == bl && (kind < bk || (kind == bk && c.d < bd)));
    if (better) { best = c.h; bl = locked; bk = kind; bd = c.d; }
  }
  return best;
}

std::optional<EdgeHit> hitEdge(const World& w, Vec2 p, double tol, Id province, bool coast) {
  if (!finite(p) || !(tol >= 0)) return std::nullopt;
  std::optional<EdgeHit> best;
  w.edges.each([&](const Edge& e) {
    if (coast ? e.kind != EdgeKind::Coast : (province != 0 && e.pl != province && e.pr != province)) return;
    Vec2 prev;
    forCoords(w, e, [&](int k, Vec2 q, int) {
      if (k > 0) {
        Proj pr = project(p, prev, q);
        double d = std::sqrt(pr.d2);
        if (d <= tol && (!best || d < best->dist)) best = EdgeHit{e.id, k - 1, pr.p, d};
      }
      prev = q;
    });
  });
  return best;
}

Vec2 handlePos(const World& w, const Handle& h) {
  if (h.kind == Handle::Node) {
    const Node* n = w.nodes.get(h.node);
    return n ? n->p : Vec2{};
  }
  if (h.kind == Handle::Point) {
    const Edge* e = w.edges.get(h.edge);
    if (e && h.index >= 0 && h.index < int(e->pts.size())) return e->pts[size_t(h.index)];
  }
  return {};
}

bool isCoastJunction(const World& w, Id node) {
  if (!w.nodes.get(node)) return false;
  Star s = starOf(w, node);
  return s.coast == 2 && s.frame == 0 && s.border >= 1;
}

bool handleLocked(const World& w, const Handle& h, bool coast) {
  if (h.kind == Handle::Point) {
    const Edge* e = w.edges.get(h.edge);
    return !e || e->kind != (coast ? EdgeKind::Coast : EdgeKind::Border) || h.index < 0 || h.index >= int(e->pts.size());
  }
  if (h.kind == Handle::Node) {
    if (!w.nodes.get(h.node)) return true;
    Star s = starOf(w, h.node);
    if (s.frame > 0) return true;
    if (coast) return s.coast == 0;
    if (s.coast > 0) return !(s.coast == 2 && s.border >= 1);
    return s.edges.empty();
  }
  return true;
}

bool canMove(const World& w, const Handle& h, Vec2 to, bool coast) {
  if (!h || !finite(to) || handleLocked(w, h, coast)) return false;
  if (!coast && h.kind == Handle::Node && isCoastJunction(w, h.node)) return false;
  if (handlePos(w, h) == to) return true;
  std::vector<NewSeg> ns;
  std::vector<SegId> removed;
  std::vector<Swept> swept;
  std::vector<VKey> exclude;
  if (!planMove(w, h, to, ns, removed, swept, exclude)) return false;
  return localOk(w, ns, removed, swept, exclude, {to});
}

void moveHandle(Tx& tx, const Handle& h, Vec2 to, bool coast) {
  const World& w = tx.w();
  if (!h) fail(coast ? "Не выбрана точка берега" : "Не выбрана точка границы");
  if (handleLocked(w, h, coast)) fail(coast ? "Двигать можно только точки берега (рамка карты закреплена)" : "Береговая линия и рамка карты не редактируются");
  if (!coast && h.kind == Handle::Node && isCoastJunction(w, h.node)) fail("Узел на берегу перемещается только вдоль берега");
  if (!finite(to)) fail("Недопустимые координаты точки");
  Box2 fb = frameBoxOf(w);
  if (!fb.empty() && !(to.x > fb.x0 && to.x < fb.x1 && to.y > fb.y0 && to.y < fb.y1)) fail("Точку нельзя вынести на край или за пределы карты");
  if (!canMove(w, h, to, coast))
    fail(coast ? "Точку берега нельзя переместить сюда: берег пересечёт другую линию" : "Точку нельзя переместить сюда: граница пересечёт другую линию");
  if (handlePos(w, h) == to) return;
  if (h.kind == Handle::Node) tx.node(h.node).p = to;
  else tx.edge(h.edge).pts[size_t(h.index)] = to;
}

Handle insertPoint(Tx& tx, Id edge, int segment, Vec2 p, bool coast) {
  const World& w = tx.w();
  const Edge* e = w.edges.get(edge);
  if (!e) fail(coast ? "Берег не найден" : "Граница не найдена");
  if (e->kind != (coast ? EdgeKind::Coast : EdgeKind::Border))
    fail(coast ? "Точку можно добавить только на берег" : "Береговая линия и рамка карты не редактируются");
  auto c = coordsOf(w, *e);
  if (c.empty()) fail("Граница повреждена: нет узла");
  if (segment < 0 || segment + 1 >= int(c.size())) fail("Неверный номер отрезка границы");
  if (!finite(p)) fail("Недопустимые координаты точки");
  Vec2 a = c[size_t(segment)], b = c[size_t(segment + 1)];
  Proj pr = project(p, a, b);
  if (dist(pr.p, a) < kMinLen || dist(pr.p, b) < kMinLen) fail("Точка слишком близко к существующей вершине");
  VKey nk{0, 0, -2};
  std::vector<NewSeg> ns{{a, pr.p, keyOfCoord(*e, segment), nk}, {pr.p, b, nk, keyOfCoord(*e, segment + 1)}};
  if (!localOk(w, ns, {SegId{edge, segment}}, {}, {}, {pr.p})) fail("Точку нельзя вставить: граница слишком близко к другой линии");
  Edge& me = tx.edge(edge);
  me.pts.insert(me.pts.begin() + segment, pr.p);
  Handle h;
  h.kind = Handle::Point;
  h.edge = edge;
  h.index = segment;
  return h;
}

void deletePoint(Tx& tx, const Handle& h, bool coast) {
  const World& w = tx.w();
  if (h.kind == Handle::Point) {
    const Edge* e = w.edges.get(h.edge);
    if (!e || h.index < 0 || h.index >= int(e->pts.size())) fail(coast ? "Точка берега не найдена" : "Точка границы не найдена");
    if (e->kind != (coast ? EdgeKind::Coast : EdgeKind::Border))
      fail(coast ? "Удалить можно только точку берега" : "Береговая линия и рамка карты не редактируются");
    if (e->a == e->b && e->pts.size() <= 2) fail("Замкнутая граница должна содержать не менее двух промежуточных точек");
    auto c = coordsOf(w, *e);
    if (c.empty()) fail("Граница повреждена: нет узла");
    int ci = h.index + 1;
    Vec2 prev = c[size_t(ci - 1)], p0 = c[size_t(ci)], next = c[size_t(ci + 1)];
    VKey kp = keyOfCoord(*e, ci - 1), kn = keyOfCoord(*e, ci + 1), me{0, e->id, h.index};
    std::vector<NewSeg> ns{{prev, next, kp, kn}};
    if (!localOk(w, ns, {SegId{e->id, ci - 1}, SegId{e->id, ci}}, {Swept{{prev, p0, next}, {kp, kn}, {pivotOf(kp, prev, {p0, next}), pivotOf(kn, next, {p0, prev})}}}, {me}, {}))
      fail("Точку нельзя удалить: граница пересечёт другую линию");
    Edge& me2 = tx.edge(h.edge);
    me2.pts.erase(me2.pts.begin() + h.index);
    return;
  }
  if (h.kind != Handle::Node) fail("Не выбрана точка границы");
  const Node* n = w.nodes.get(h.node);
  if (!n) fail("Узел не найден");
  Star s = starOf(w, h.node);
  if (coast) {
    if (s.frame || s.border) fail("Узел, где к берегу подходит граница провинции, удалить нельзя");
    if (s.coast != 2 || s.edges.size() != 2) fail("Удалить можно только узел, соединяющий две части одного берега");
  } else {
    if (s.coast || s.frame) fail("Узел на берегу или рамке удалить нельзя");
    if (s.border != 2 || s.edges.size() != 2) fail("Удалить можно только узел, соединяющий две части одной границы");
  }
  const Edge &E1 = *s.edges[0], &E2 = *s.edges[1];
  Edge A = E1, B = E2;
  auto rev = [](Edge& e) {
    std::swap(e.a, e.b);
    std::reverse(e.pts.begin(), e.pts.end());
    std::swap(e.pl, e.pr);
    std::swap(e.tl, e.tr);
  };
  bool revA = A.b != h.node, revB = B.a != h.node;
  if (revA) rev(A);
  if (revB) rev(B);
  if (A.a == B.b) fail("Нельзя удалить: замкнутая граница должна содержать не менее двух узлов");
  if (A.pl != B.pl || A.pr != B.pr || A.tl != B.tl || A.tr != B.tr) fail("Узел разделяет разные границы");
  auto c1 = coordsOf(w, E1), c2 = coordsOf(w, E2);
  if (c1.empty() || c2.empty()) fail("Граница повреждена: нет узла");
  int m1 = int(c1.size()), m2 = int(c2.size());
  int k1 = E1.b == h.node ? m1 - 2 : 1, k2 = E2.a == h.node ? 1 : m2 - 2;
  Vec2 prev = c1[size_t(k1)], next = c2[size_t(k2)];
  VKey kp = keyOfCoord(E1, k1), kn = keyOfCoord(E2, k2), me = nodeKey(h.node);
  std::vector<SegId> removed{{E1.id, E1.b == h.node ? m1 - 2 : 0}, {E2.id, E2.a == h.node ? 0 : m2 - 2}};
  std::vector<NewSeg> ns{{prev, next, kp, kn}};
  if (!localOk(w, ns, removed, {Swept{{prev, n->p, next}, {kp, kn}, {pivotOf(kp, prev, {n->p, next}), pivotOf(kn, next, {n->p, prev})}}}, {me}, {})) fail("Узел нельзя удалить: граница пересечёт другую линию");
  Edge M = A;
  M.b = B.b;
  M.pts.insert(M.pts.end(), B.pts.begin(), B.pts.end());
  bool keepFirst = E1.id < E2.id;
  if (keepFirst ? revA : revB) rev(M);
  Id keep = keepFirst ? E1.id : E2.id, drop = keepFirst ? E2.id : E1.id;
  M.id = keep;
  tx.edge(keep) = M;
  tx.eraseEdge(drop);
  tx.eraseNode(h.node);
}

// ---------------------------------------------------------------- скольжение по берегу
namespace {

struct SlidePlan {
  Vec2 x;
  bool noop = false;
  Edge arc1, arc2;             // новые береговые дуги (ID переиспользуются; 0 — новая)
  std::vector<Id> eraseEdges, eraseNodes;
  bool newMid = false;         // создать узел середины кольца (arc1.b / arc2.a = 0 заменяются им)
  Vec2 midPos;
};

// Обход береговой цепочки от стыка J по дуге first через «прозрачные» узлы (степень 2, только берег) до упора.
struct Walk {
  std::vector<Vec2> pts;       // от J (не включая) до упора (включая)
  std::vector<VKey> keys;
  std::vector<SegId> segs;     // мировые отрезки по порядку обхода
  std::vector<Id> edges, mids;
  Id stop = 0;
  Side left, right;            // метки сторон по направлению обхода (одинаковы у всех дуг цепочки)
  bool uniform = true;
};

std::optional<SlidePlan> planSlide(const World& w, Id J, Vec2 to) {
  if (!finite(to) || !isCoastJunction(w, J)) return std::nullopt;
  const Node* jn = w.nodes.get(J);
  std::unordered_map<Id, std::vector<const Edge*>> adj;
  w.edges.each([&](const Edge& e) {
    adj[e.a].push_back(&e);
    if (e.b != e.a) adj[e.b].push_back(&e);
  });
  auto transparent = [&](Id n) {
    if (n == J) return false;
    const auto& v = adj[n];
    if (v.size() != 2) return false;
    for (const Edge* e : v)
      if (e->kind != EdgeKind::Coast || e->a == e->b) return false;
    return true;
  };
  std::vector<const Edge*> coastAtJ;
  for (const Edge* e : adj[J])
    if (e->kind == EdgeKind::Coast) coastAtJ.push_back(e);
  bool selfLoop = coastAtJ.size() == 1 && coastAtJ[0]->a == J && coastAtJ[0]->b == J;
  if (coastAtJ.size() != 2 && !selfLoop) return std::nullopt;

  auto walk = [&](const Edge* first) {
    Walk W;
    Id cur = J;
    const Edge* e = first;
    for (size_t guard = 0; guard <= w.edges.size(); guard++) {
      auto c = coordsOf(w, *e);
      int m = int(c.size());
      if (m < 2) { W.uniform = false; break; }
      bool fwd = e->a == cur;
      Side l = fwd ? Side{e->pl, e->tl} : Side{e->pr, e->tr};
      Side r = fwd ? Side{e->pr, e->tr} : Side{e->pl, e->tl};
      if (W.edges.empty()) { W.left = l; W.right = r; }
      else if (!(l == W.left) || !(r == W.right)) W.uniform = false;
      for (int i = 1; i < m; i++) {
        int k = fwd ? i : m - 1 - i;
        W.pts.push_back(c[size_t(k)]);
        W.keys.push_back(keyOfCoord(*e, k));
        W.segs.push_back(SegId{e->id, fwd ? i - 1 : m - 1 - i});
      }
      W.edges.push_back(e->id);
      Id nxt = fwd ? e->b : e->a;
      if (nxt == J || !transparent(nxt)) { W.stop = nxt; break; }
      W.mids.push_back(nxt);
      const auto& v = adj[nxt];
      e = v[0] == e ? v[1] : v[0];
      cur = nxt;
    }
    return W;
  };

  // Цепочка pts[0..n-1]: для разомкнутой — от упора P0 через J (индекс j) к упору N0;
  // для кольца с единственным стыком — от J по кругу обратно к J (pts[n-1] = pts[0], j = 0).
  Walk A = walk(coastAtJ[0]);
  if (!A.uniform) return std::nullopt;
  const bool closed = A.stop == J;
  std::vector<Vec2> pts;
  std::vector<VKey> keys;
  std::vector<SegId> segs;
  int j = 0;
  Side L1l, L1r, L2l, L2r;
  Id p0 = J, n0 = J, e1 = 0, e2 = 0;
  std::vector<Id> mids, chainEdges;
  if (closed) {
    pts.push_back(jn->p);
    keys.push_back(nodeKey(J));
    pts.insert(pts.end(), A.pts.begin(), A.pts.end());
    keys.insert(keys.end(), A.keys.begin(), A.keys.end());
    segs = A.segs;
    L1l = L2l = A.left;
    L1r = L2r = A.right;
    mids = A.mids;
    chainEdges = A.edges;
    e1 = A.edges.front();
    e2 = A.edges.size() > 1 ? A.edges.back() : 0;
  } else {
    if (coastAtJ.size() != 2) return std::nullopt;
    Walk B = walk(coastAtJ[1]);
    if (!B.uniform) return std::nullopt;
    for (size_t i = A.pts.size(); i-- > 0;) {
      pts.push_back(A.pts[i]);
      keys.push_back(A.keys[i]);
    }
    for (size_t i = A.segs.size(); i-- > 0;) segs.push_back(A.segs[i]);
    j = int(pts.size());
    pts.push_back(jn->p);
    keys.push_back(nodeKey(J));
    pts.insert(pts.end(), B.pts.begin(), B.pts.end());
    keys.insert(keys.end(), B.keys.begin(), B.keys.end());
    segs.insert(segs.end(), B.segs.begin(), B.segs.end());
    L1l = A.right;  // часть до J идёт навстречу обходу A
    L1r = A.left;
    L2l = B.left;
    L2r = B.right;
    p0 = A.stop;
    n0 = B.stop;
    e1 = coastAtJ[0]->id;
    e2 = coastAtJ[1]->id;
    mids = A.mids;
    mids.insert(mids.end(), B.mids.begin(), B.mids.end());
    chainEdges = A.edges;
    chainEdges.insert(chainEdges.end(), B.edges.begin(), B.edges.end());
  }
  const int n = int(pts.size());
  if (n < 3 || int(segs.size()) != n - 1) return std::nullopt;

  // Проекция to на цепочку; прилипание к вершине ближе 0,5 (кроме упоров).
  int bs = -1;
  Proj bp;
  for (int s = 0; s + 1 < n; s++) {
    Proj pr = project(to, pts[size_t(s)], pts[size_t(s + 1)]);
    if (bs < 0 || pr.d2 < bp.d2) { bs = s; bp = pr; }
  }
  Vec2 X = bp.p;
  int snapV = -1;
  for (int v : {bs, bs + 1}) {
    double d = dist(X, pts[size_t(v)]);
    bool stop = !closed && (v == 0 || v == n - 1);
    if (d < kMinLen || (d < 0.5 && !stop)) {
      if (stop) return std::nullopt;  // упор (соседний стык) не проходится
      if (snapV < 0 || d < dist(X, pts[size_t(snapV)])) snapV = v;
    }
  }
  if (closed && snapV == n - 1) snapV = 0;
  if (snapV >= 0) X = pts[size_t(snapV)];
  SlidePlan plan;
  plan.x = X;
  if (snapV == j || X == jn->p) {
    plan.x = jn->p;
    plan.noop = true;
    return plan;
  }

  // Индексы цепочки по модулю кольца.
  const int ring = closed ? n - 1 : n;
  auto wrap = [&](int i) { return closed ? ((i % ring) + ring) % ring : i; };
  const int jPrev = closed ? n - 2 : j - 1, jNext = j + 1;
  // Старое место стыка остаётся точкой берега, кроме случая, когда оно лежит на прямой между соседями.
  const bool dropJ0 = snapV != jPrev && snapV != jNext && !(snapV < 0 && (bs == jPrev || bs == j)) &&
                      distToSeg(pts[size_t(j)], pts[size_t(jPrev)], pts[size_t(jNext)]) <= 1e-9;
  const VKey kJ0{0, 0, -3}, kJ = nodeKey(J);
  struct CP { Vec2 p; VKey k; };
  std::vector<CP> nc;
  int xIndex = -1;
  for (int i = 0; i < ring; i++) {
    if (i == j) {
      if (!dropJ0) nc.push_back({pts[size_t(i)], kJ0});
    } else if (i == snapV) {
      xIndex = int(nc.size());
      nc.push_back({X, kJ});
    } else {
      nc.push_back({pts[size_t(i)], keys[size_t(i)]});
    }
    if (snapV < 0 && i == bs) {
      xIndex = int(nc.size());
      nc.push_back({X, kJ});
    }
  }
  if (xIndex < 0) return std::nullopt;
  const int ncn = int(nc.size());

  // Изменённые отрезки цепочки: у старого места стыка и у новой точки.
  std::vector<NewSeg> ns;
  std::vector<SegId> removed;
  {
    std::vector<int> T{wrap(j - 1), j};
    if (snapV >= 0) { T.push_back(wrap(snapV - 1)); T.push_back(snapV); }
    else T.push_back(bs);
    std::sort(T.begin(), T.end());
    T.erase(std::unique(T.begin(), T.end()), T.end());
    for (int t : T)
      if (t >= 0 && t < n - 1) removed.push_back(segs[size_t(t)]);
    int segCount = closed ? ncn : ncn - 1;
    for (int s = 0; s < segCount; s++) {
      const CP &a = nc[size_t(s)], &b = nc[size_t((s + 1) % ncn)];
      bool changed = a.k == kJ0 || b.k == kJ0 || a.k == kJ || b.k == kJ ||
                     (dropJ0 && a.k == keys[size_t(jPrev)] && b.k == keys[size_t(jNext)]);
      if (changed) ns.push_back({a.p, b.p, a.k, b.k});
    }
  }

  // Путь по берегу от старого места стыка к новому (для кольца — более короткий).
  auto pathDir = [&](int dir, double& len) {
    std::vector<int> r;
    len = 0;
    int i = j;
    for (int guard = 0; guard < n; guard++) {
      int seg = dir > 0 ? i : wrap(i - 1);
      if (!closed && (seg < 0 || seg > n - 2)) return std::optional<std::vector<int>>{};
      if (snapV < 0 && seg == bs) { len += dist(pts[size_t(i)], X); return std::optional<std::vector<int>>{r}; }
      int nx = wrap(i + dir);
      len += dist(pts[size_t(i)], pts[size_t(nx)]);
      if (nx == snapV) return std::optional<std::vector<int>>{r};
      r.push_back(nx);
      i = nx;
    }
    return std::optional<std::vector<int>>{};
  };
  double lf = 0, lb = 0;
  auto fw = pathDir(1, lf), bw = pathDir(-1, lb);
  std::vector<int> path;
  if (fw && bw) path = lf <= lb ? *fw : *bw;
  else if (fw) path = *fw;
  else if (bw) path = *bw;
  else return std::nullopt;

  // Концевые отрезки пограничных дуг стыка и заметаемые ими области.
  std::vector<Swept> swept;
  std::vector<VKey> exclude{kJ};
  std::vector<VKey> pathKeys;
  for (int i : path) pathKeys.push_back(keys[size_t(i)]);
  if (snapV >= 0) pathKeys.push_back(keys[size_t(snapV)]);
  for (const Edge* e : adj[J]) {
    if (e->kind != EdgeKind::Border) continue;
    auto c = coordsOf(w, *e);
    if (c.size() < 2) return std::nullopt;
    int m = int(c.size());
    for (int end = 0; end < 2; end++) {
      bool atA = end == 0;
      if (atA ? e->a != J : e->b != J) continue;
      int qk = atA ? 1 : m - 2;
      Vec2 q = c[size_t(qk)];
      VKey kq = keyOfCoord(*e, qk);
      ns.push_back(atA ? NewSeg{X, q, kJ, kq} : NewSeg{q, X, kq, kJ});
      removed.push_back(SegId{e->id, atA ? 0 : m - 2});
      Swept sw;
      sw.poly = {q, jn->p};
      for (int i : path) sw.poly.push_back(pts[size_t(i)]);
      sw.poly.push_back(X);
      sw.own = pathKeys;
      sw.own.push_back(kq);
      sw.pivots.push_back(pivotOf(kq, q, std::vector<Vec2>(sw.poly.begin() + 1, sw.poly.end())));
      swept.push_back(std::move(sw));
    }
  }
  if (dropJ0) {
    Vec2 a = pts[size_t(jPrev)], m = pts[size_t(j)], b = pts[size_t(jNext)];
    swept.push_back({{a, m, b}, {keys[size_t(jPrev)], keys[size_t(jNext)]},
                     {pivotOf(keys[size_t(jPrev)], a, {m, b}), pivotOf(keys[size_t(jNext)], b, {m, a})}});
  }
  if (!localOk(w, ns, removed, swept, exclude, {X})) return std::nullopt;

  // Новые береговые дуги.
  auto mkArc = [&](Id id, Id a, Id b, std::vector<Vec2> ip, Side l, Side r) {
    Edge e;
    e.id = id;
    e.a = a;
    e.b = b;
    e.pts = std::move(ip);
    e.kind = EdgeKind::Coast;
    e.pl = l.prov;
    e.tl = l.ter;
    e.pr = r.prov;
    e.tr = r.ter;
    return e;
  };
  if (!closed) {
    std::vector<Vec2> ip1, ip2;
    for (int i = 1; i < xIndex; i++) ip1.push_back(nc[size_t(i)].p);
    for (int i = xIndex + 1; i + 1 < ncn; i++) ip2.push_back(nc[size_t(i)].p);
    plan.arc1 = mkArc(e1, p0, J, std::move(ip1), L1l, L1r);
    plan.arc2 = mkArc(e2, J, n0, std::move(ip2), L2l, L2r);
    for (Id e : chainEdges)
      if (e != e1 && e != e2) plan.eraseEdges.push_back(e);
    plan.eraseNodes = mids;
  } else {
    // кольцо от X: J -> M -> J; M — первый прозрачный узел цепочки или новый узел посередине
    std::vector<CP> loop;
    for (int i = 0; i < ncn; i++) loop.push_back(nc[size_t((xIndex + i) % ncn)]);
    int mi = -1;
    Id midId = 0;
    for (int i = 1; i < ncn && mi < 0; i++)
      for (Id mn : mids)
        if (loop[size_t(i)].k == nodeKey(mn)) { mi = i; midId = mn; break; }
    if (mi < 0) {
      std::vector<double> acc(size_t(ncn) + 1, 0);
      for (int i = 0; i < ncn; i++) acc[size_t(i) + 1] = acc[size_t(i)] + dist(loop[size_t(i)].p, loop[size_t((i + 1) % ncn)].p);
      double half = acc[size_t(ncn)] * 0.5;
      mi = 1;
      for (int i = 1; i < ncn; i++)
        if (std::fabs(acc[size_t(i)] - half) < std::fabs(acc[size_t(mi)] - half)) mi = i;
      plan.newMid = true;
      plan.midPos = loop[size_t(mi)].p;
    }
    std::vector<Vec2> ip1, ip2;
    for (int i = 1; i < mi; i++) ip1.push_back(loop[size_t(i)].p);
    for (int i = mi + 1; i < ncn; i++) ip2.push_back(loop[size_t(i)].p);
    plan.arc1 = mkArc(e1, J, midId, std::move(ip1), L1l, L1r);
    plan.arc2 = mkArc(e2, midId, J, std::move(ip2), L2l, L2r);
    for (Id e : chainEdges)
      if (e != e1 && e != e2) plan.eraseEdges.push_back(e);
    for (Id mn : mids)
      if (mn != midId) plan.eraseNodes.push_back(mn);
  }
  return plan;
}

}  // namespace

std::optional<Vec2> slideTarget(const World& w, Id node, Vec2 to) {
  auto plan = planSlide(w, node, to);
  if (!plan) return std::nullopt;
  return plan->x;
}

void slideJunction(Tx& tx, Id node, Vec2 to) {
  if (!isCoastJunction(tx.w(), node)) fail("Скользить вдоль берега может только стык границы с берегом");
  auto plan = planSlide(tx.w(), node, to);
  if (!plan) fail("Стык нельзя сдвинуть сюда: граница пересечёт другую линию или упрётся в соседний стык");
  if (plan->noop) return;
  Edge a1 = plan->arc1, a2 = plan->arc2;
  for (Id e : plan->eraseEdges) tx.eraseEdge(e);
  for (Id n : plan->eraseNodes) tx.eraseNode(n);
  tx.node(node).p = plan->x;
  if (plan->newMid) {
    Id mid = tx.add(Node{0, plan->midPos}).id;
    a1.b = mid;
    a2.a = mid;
  }
  for (Edge* e : {&a1, &a2}) {
    if (e->id != 0) tx.edge(e->id) = *e;
    else tx.add(*e);
  }
}

}  // namespace rg::geo
