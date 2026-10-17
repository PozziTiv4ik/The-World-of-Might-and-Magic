// Тесты geo: сетка отрезков и прямоугольников (geo/grid.h) — запросы совпадают с полным перебором.
#include "geo/geom.h"
#include "geo/grid.h"
#include "tests/test.h"

using namespace rg;
using namespace rg::geo;

TEST(geo_grid_queries_match_bruteforce) {
  Rng rng(7);
  std::vector<Vec2> a, b;
  for (int i = 0; i < 3000; i++) {
    Vec2 p{rng.uniform() * 1000, rng.uniform() * 600};
    double len = rng.uniform() < 0.1 ? 300 : 8;
    Vec2 q = p + Vec2(rng.uniform() - 0.5, rng.uniform() - 0.5) * len;
    a.push_back(p);
    b.push_back(q);
  }
  SegGrid g;
  g.build(a, b);
  std::vector<u32> stamp;
  for (int k = 0; k < 200; k++) {
    Box2 q;
    q.add({rng.uniform() * 1000, rng.uniform() * 600});
    q.add(q.center() + Vec2(rng.uniform() * 50, rng.uniform() * 50));
    std::vector<int> got, want;
    g.queryUnique(q, stamp, u32(k + 1), [&](int i) {
      Box2 sb;
      sb.add(a[size_t(i)]);
      sb.add(b[size_t(i)]);
      if (sb.intersects(q)) got.push_back(i);
    });
    for (int i = 0; i < 3000; i++) {
      // отрезок, реально пересекающий прямоугольник, обязан найтись
      Box2 sb;
      sb.add(a[size_t(i)]);
      sb.add(b[size_t(i)]);
      if (!sb.intersects(q)) continue;
      Vec2 c[4] = {{q.x0, q.y0}, {q.x1, q.y0}, {q.x1, q.y1}, {q.x0, q.y1}};
      bool hit = q.contains(a[size_t(i)]) || q.contains(b[size_t(i)]);
      for (int e = 0; e < 4 && !hit; e++) hit = segRelation(a[size_t(i)], b[size_t(i)], c[e], c[(e + 1) % 4]) != SegRel::None;
      if (hit) want.push_back(i);
    }
    std::sort(got.begin(), got.end());
    for (int i : want) CHECK(std::binary_search(got.begin(), got.end(), i));
    // ближайший
    Vec2 p{rng.uniform() * 1000, rng.uniform() * 600};
    double d = 0;
    int ni = g.nearest(p, kInf, &d);
    double best = kInf;
    for (int i = 0; i < 3000; i++) best = std::min(best, distToSeg(p, a[size_t(i)], b[size_t(i)]));
    CHECK(ni >= 0);
    CHECK_NEAR(d, best, 1e-9);
  }
}

TEST(geo_grid_boxes) {
  Rng rng(11);
  std::vector<Box2> boxes;
  for (int i = 0; i < 500; i++) {
    Vec2 a{rng.uniform() * 1000, rng.uniform() * 1000};
    boxes.push_back(Box2(a.x, a.y, a.x + rng.uniform() * 80, a.y + rng.uniform() * 80));
  }
  boxes.push_back(Box2());  // пустой не регистрируется
  SegGrid g;
  g.buildBoxes(boxes);
  std::vector<u32> stamp;
  for (int k = 0; k < 300; k++) {
    Vec2 p{rng.uniform() * 1000, rng.uniform() * 1000};
    Box2 q(p.x, p.y, p.x, p.y);
    std::vector<int> got;
    g.queryUnique(q, stamp, u32(k + 1), [&](int i) { if (boxes[size_t(i)].contains(p)) got.push_back(i); });
    std::sort(got.begin(), got.end());
    std::vector<int> want;
    for (int i = 0; i < int(boxes.size()); i++)
      if (boxes[size_t(i)].contains(p)) want.push_back(i);
    CHECK(got == want);
  }
}
