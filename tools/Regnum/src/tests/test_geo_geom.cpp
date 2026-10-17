// Тесты geo: точные предикаты, отрезки, кольца, polylabel, упрощение.
#include "geo/geom.h"
#include "tests/test.h"

using namespace rg;
using namespace rg::geo;

TEST(geo_geom_orient_basic) {
  CHECK_EQ(orient({0, 0}, {1, 0}, {0, 1}), 1);
  CHECK_EQ(orient({0, 0}, {1, 0}, {0, -1}), -1);
  CHECK_EQ(orient({0, 0}, {1, 1}, {2, 2}), 0);
  CHECK_EQ(orient({0, 0}, {0, 0}, {5, 7}), 0);
  CHECK_EQ(orient({3, 3}, {3, 3}, {3, 3}), 0);
}

// Почти коллинеарные точки: наивная формула ошибается, точный предикат согласован при перестановках.
TEST(geo_geom_orient_exact_near_degenerate) {
  // Классический пример: точки около прямой y = x с возмущением в младший бит.
  int mismatches = 0;
  for (int i = 0; i < 64; i++)
    for (int j = 0; j < 64; j++) {
      Vec2 p{0.5 + i * std::ldexp(1.0, -53), 0.5 + j * std::ldexp(1.0, -53)};
      Vec2 q{12, 12}, r{24, 24};
      int s = orient(p, q, r);
      // циклические перестановки сохраняют знак, транспозиция меняет
      CHECK_EQ(orient(q, r, p), s);
      CHECK_EQ(orient(r, p, q), s);
      CHECK_EQ(orient(q, p, r), -s);
      // знак совпадает с точным: p выше прямой y = x ⇔ j > i
      int expect = j > i ? 1 : (j < i ? -1 : 0);
      if (s != expect) mismatches++;
    }
  CHECK_EQ(mismatches, 0);
  // Большие координаты и крошечное смещение.
  Vec2 a{8000.123456789, 4500.987654321}, b{-7999.5, -4499.25};
  Vec2 m = (a + b) * 0.5;
  int s0 = orient(a, b, m);
  CHECK_EQ(orient(b, a, m), -s0);
  Vec2 up = m + Vec2(0, 1e-9), dn = m - Vec2(0, 1e-9);
  CHECK(orient(a, b, up) != orient(a, b, dn));
}

TEST(geo_geom_seg_relation) {
  CHECK(segRelation({0, 0}, {2, 2}, {0, 2}, {2, 0}) == SegRel::Proper);
  CHECK(segRelation({0, 0}, {1, 1}, {2, 2}, {3, 3}) == SegRel::None);
  CHECK(segRelation({0, 0}, {2, 2}, {2, 2}, {3, 0}) == SegRel::Touch);       // общий конец
  CHECK(segRelation({0, 0}, {2, 0}, {1, 0}, {1, 5}) == SegRel::Touch);       // Т-образное касание
  CHECK(segRelation({0, 0}, {2, 0}, {1, 0}, {3, 0}) == SegRel::Overlap);
  CHECK(segRelation({0, 0}, {2, 0}, {2, 0}, {3, 0}) == SegRel::Touch);       // коллинеарны, касание концом
  CHECK(segRelation({0, 0}, {2, 0}, {2.5, 0}, {3, 0}) == SegRel::None);
  CHECK(segRelation({0, 0}, {0, 2}, {0, 1}, {0, 1}) == SegRel::Touch);       // точка на отрезке
  CHECK(segRelation({0, 0}, {0, 2}, {1, 1}, {1, 1}) == SegRel::None);
  CHECK(segRelation({0, 0}, {0, 4}, {0, 1}, {0, 3}) == SegRel::Overlap);     // вертикальное наложение
  CHECK(onSegment({1, 1}, {0, 0}, {2, 2}));
  CHECK(!onSegment({1, 1.0000001}, {0, 0}, {2, 2}));
  CHECK(!onSegment({3, 3}, {0, 0}, {2, 2}));
  double t = 0, u = 0;
  Vec2 x = crossPoint({0, 0}, {4, 0}, {1, -1}, {1, 3}, &t, &u);
  CHECK_NEAR(x.x, 1, 1e-12);
  CHECK_NEAR(x.y, 0, 1e-12);
  CHECK_NEAR(t, 0.25, 1e-12);
  CHECK_NEAR(u, 0.25, 1e-12);
  CHECK_NEAR(segSegDist2({0, 0}, {1, 0}, {0, 2}, {1, 2}), 4, 1e-12);
  CHECK(segmentsNear({0, 0}, {1, 0}, {0.5, 0.05}, {0.5, 1}, 0.1));
  CHECK(!segmentsNear({0, 0}, {1, 0}, {0.5, 0.2}, {0.5, 1}, 0.1));
}

TEST(geo_geom_project_and_distance) {
  Proj p = project({1, 1}, {0, 0}, {2, 0});
  CHECK_NEAR(p.t, 0.5, 1e-12);
  CHECK_NEAR(p.d2, 1, 1e-12);
  p = project({-3, 4}, {0, 0}, {2, 0});
  CHECK_EQ(p.t, 0.0);
  CHECK_NEAR(p.d2, 25, 1e-12);
  p = project({1, 1}, {5, 5}, {5, 5});
  CHECK_EQ(p.t, 0.0);
  CHECK_NEAR(distToSeg({0, 3}, {-1, 0}, {1, 0}), 3, 1e-12);
}

TEST(geo_geom_ring_area_pip) {
  std::vector<Vec2> sq{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  CHECK_NEAR(signedArea(sq), 100, 1e-12);
  std::vector<Vec2> rsq(sq.rbegin(), sq.rend());
  CHECK_NEAR(signedArea(rsq), -100, 1e-12);
  std::vector<Vec2> hole{{3, 3}, {7, 3}, {7, 7}, {3, 7}};
  CHECK_NEAR(polygonArea({sq, hole}), 84, 1e-12);
  CHECK(pointInRing({5, 5}, sq));
  CHECK(!pointInRing({15, 5}, sq));
  CHECK(pointInPolygon({1, 1}, {sq, hole}));
  CHECK(!pointInPolygon({5, 5}, {sq, hole}));
  CHECK_NEAR(signedDist({1, 5}, {sq, hole}), 1, 1e-12);
  CHECK_NEAR(signedDist({5, 5}, {sq, hole}), -2, 1e-12);
  Vec2 c = ringCentroid(sq);
  CHECK_NEAR(c.x, 5, 1e-12);
  CHECK_NEAR(c.y, 5, 1e-12);
  Box2 b = bounds(sq);
  CHECK_EQ(b.x1, 10.0);
  // половинное правило: вершина на луче считается один раз
  std::vector<Vec2> diamond{{0, 5}, {5, 0}, {10, 5}, {5, 10}};
  CHECK(pointInRing({5, 5}, diamond));
  CHECK(!pointInRing({-1, 5}, diamond));
  CHECK(!pointInRing({11, 5}, diamond));
}

TEST(geo_geom_is_simple) {
  CHECK(isSimple({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true));
  CHECK(!isSimple({{0, 0}, {10, 10}, {10, 0}, {0, 10}}, true));        // бабочка
  CHECK(!isSimple({{0, 0}, {10, 0}, {5, 0}, {5, 5}}, false));          // наложение соседних звеньев
  CHECK(!isSimple({{0, 0}, {10, 0}, {10, 10}, {5, 0}, {0, 10}}, true)); // касание вершиной
  CHECK(isSimple({{0, 0}, {1, 1}, {2, 0}}, false));
  CHECK(!isSimple({{0, 0}, {1, 1}, {1, 1}, {2, 0}}, false));           // повтор точки
  // большой простой (через сетку) и с одним самопересечением
  std::vector<Vec2> big;
  for (int i = 0; i < 2000; i++) {
    double t = 2 * kPi * i / 2000;
    big.push_back({std::cos(t) * (100 + 10 * std::sin(13 * t)), std::sin(t) * (100 + 10 * std::sin(13 * t))});
  }
  CHECK(isSimple(big, true));
  std::swap(big[100], big[900]);
  CHECK(!isSimple(big, true));
}

TEST(geo_geom_polylabel) {
  std::vector<Vec2> sq{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
  double d = 0;
  Vec2 p = polylabel({sq}, 0.5, &d);
  CHECK_NEAR(p.x, 50, 1);
  CHECK_NEAR(p.y, 50, 1);
  CHECK_NEAR(d, 50, 1);
  // L-образный: метка внутри и не ближе 15 к границе
  std::vector<Vec2> L{{0, 0}, {100, 0}, {100, 30}, {30, 30}, {30, 100}, {0, 100}};
  p = polylabel({L}, 0.5, &d);
  CHECK(pointInRing(p, L));
  CHECK(d >= 14.5);
  // кольцо с дырой в центре: метка в «бублике»
  std::vector<Vec2> hole{{20, 20}, {80, 20}, {80, 80}, {20, 80}};
  p = polylabel({sq, hole}, 0.5, &d);
  CHECK(pointInPolygon(p, {sq, hole}));
  CHECK(d >= 9.0);
  // крупный многоугольник (ветка с сеткой) совпадает с прямым перебором по качеству
  std::vector<Vec2> big;
  for (int i = 0; i < 3000; i++) {
    double t = 2 * kPi * i / 3000;
    double r = 500 + 60 * std::sin(5 * t) + 15 * std::sin(41 * t);
    big.push_back({1000 + r * std::cos(t), 1000 + r * std::sin(t)});
  }
  p = polylabel({big}, 1.0, &d);
  CHECK(pointInRing(p, big));
  CHECK_NEAR(d, distToRings(p, {big}), 1e-9);
  CHECK(d > 400);
  // очень тонкий
  std::vector<Vec2> thin{{0, 0}, {1000, 0}, {1000, 0.01}, {0, 0.01}};
  p = polylabel({thin}, 1, &d);
  CHECK(pointInRing(p, thin));
  CHECK(d > 0);
}

TEST(geo_geom_simplify_dp) {
  std::vector<Vec2> line;
  for (int i = 0; i <= 100; i++) line.push_back({double(i), std::sin(i * 0.1) * 0.01});
  auto s = simplifyDP(line, 0.1);
  CHECK_EQ(s.size(), size_t(2));
  CHECK(s.front() == line.front());
  CHECK(s.back() == line.back());
  std::vector<Vec2> zig;
  for (int i = 0; i <= 10; i++) zig.push_back({double(i), (i % 2) ? 5.0 : 0.0});
  CHECK_EQ(simplifyDP(zig, 1).size(), zig.size());
  std::vector<Vec2> circle;
  for (int i = 0; i < 400; i++) circle.push_back({100 * std::cos(2 * kPi * i / 400), 100 * std::sin(2 * kPi * i / 400)});
  auto c = simplifyDP(circle, 0.5, true);
  CHECK(c.size() >= 3 && c.size() < 100);
  for (auto& p : circle) CHECK(distToRings(p, {c}) <= 0.5 + 1e-9);
}

