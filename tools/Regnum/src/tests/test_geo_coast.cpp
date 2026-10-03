// Тесты правки берега (режим «Правка карты»): контуры суши из графа, суша и море контуром (перекраска рельефа
// граней), ручки берега — перемещение, вставка и удаление точек, стыки с границами.
#include "tests/test_geo_util.h"

using namespace rg;
using namespace rg::geo;
using geotest::sampleCoast;

namespace {

constexpr double W = 1000, H = 600;

Store initStore() {
  Store s;
  s.transact("init", [](Tx& tx) { initFromCoast(tx, sampleCoast(W, H)); });
  return s;
}

std::vector<Vec2> rect(double x0, double y0, double x1, double y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }

double terrainArea(const World& w, Terrain t) {
  auto fs = buildFaces(w);   // держать указатель: грани живут, пока жив FaceSet
  double a = 0;
  for (auto& f : fs->faces)
    if (f.terrain == t) a += f.area;
  return a;
}

double provArea(const World& w, Id p) {
  auto fs = buildFaces(w);
  auto* s = fs->shape(p);
  return s ? s->area : 0;
}

bool allTerrain(const World& w, Id p, Terrain t) {
  auto fs = buildFaces(w);
  auto* s = fs->shape(p);
  if (!s) return false;
  for (int f : s->faces)
    if (fs->faces[size_t(f)].terrain != t) return false;
  return true;
}

// Сумма знаковых площадей контуров суши и знаки их площадей.
double ringsArea(const std::vector<std::vector<Vec2>>& rings, int* pos = nullptr, int* neg = nullptr) {
  double s = 0;
  for (auto& r : rings) {
    const double a = signedArea(r);
    s += a;
    if (pos && a > 0) ++*pos;
    if (neg && a < 0) ++*neg;
  }
  return s;
}

// Первая промежуточная точка береговой дуги, ближайшая к c.
Handle coastPointNear(const World& w, Vec2 c) {
  Handle best;
  double bd = kInf;
  w.edges.each([&](const Edge& e) {
    if (e.kind != EdgeKind::Coast) return;
    for (int i = 0; i < int(e.pts.size()); i++) {
      const double d = dist(e.pts[size_t(i)], c);
      if (d < bd) {
        bd = d;
        best = Handle{Handle::Point, 0, e.id, i};
      }
    }
  });
  return best;
}

}  // namespace

TEST(geo_land_rings) {
  Store s = initStore();
  const World& w = s.world();
  CHECK(hasGeometry(w));
  CHECK(!hasGeometry(newWorld("Пусто")));
  CHECK(landRings(newWorld("Пусто")).empty());
  auto rings = landRings(w);
  CHECK_EQ(rings.size(), size_t(6));   // 5 островов и материк (его контур идёт и по рамке)
  int pos = 0, neg = 0;
  const double sum = ringsArea(rings, &pos, &neg);
  const double land = terrainArea(w, Terrain::Land);
  CHECK_NEAR(std::fabs(sum), land, 1e-6 * land);
  CHECK(pos == 0 || neg == 0);   // у внешних контуров одна ориентация
  // Материк касается рамки: его контур включает отрезок по краю x = 0.
  bool onFrame = false;
  for (auto& r : rings)
    for (Vec2 p : r) onFrame = onFrame || p.x == 0;
  CHECK(onFrame);
}

TEST(geo_paint_terrain_land_and_sea) {
  Store s = initStore();
  const double land0 = terrainArea(s.world(), Terrain::Land);
  // Новый остров в открытом море.
  double a = s.transact("land", [](Tx& tx) { return paintTerrain(tx, rect(920, 30, 980, 90), Terrain::Land); });
  GEO_CHECK_WORLD(s.world(), W, H);
  CHECK_NEAR(a, 3600, 1e-6);
  CHECK_NEAR(terrainArea(s.world(), Terrain::Land), land0 + 3600, 1e-6 * land0);
  CHECK(buildFaces(s.world())->terrainAt({950, 60}) == Terrain::Land);
  CHECK_EQ(landRings(s.world()).size(), size_t(7));
  // Внутреннее море посреди большого острова: контур суши с дырой противоположной ориентации.
  double b = s.transact("sea", [](Tx& tx) { return paintTerrain(tx, rect(460, 280, 500, 320), Terrain::Sea); });
  GEO_CHECK_WORLD(s.world(), W, H);
  CHECK_NEAR(b, 1600, 1e-6);
  CHECK(buildFaces(s.world())->terrainAt({480, 300}) == Terrain::Sea);
  auto rings = landRings(s.world());
  int pos = 0, neg = 0;
  const double sum = ringsArea(rings, &pos, &neg);
  const double land = terrainArea(s.world(), Terrain::Land);
  CHECK_NEAR(std::fabs(sum), land, 1e-6 * land);
  CHECK(pos >= 1 && neg >= 1);
  // Повтор ничего не меняет — отказ, мир не тронут.
  World before = s.world();
  CHECK_THROWS(s.transact("again", [](Tx& tx) { paintTerrain(tx, rect(460, 280, 500, 320), Terrain::Sea); }));
  CHECK_EQ(World::diff(before, s.world()), 0u);
  CHECK_THROWS(s.transact("none", [](Tx& tx) { paintTerrain(tx, rect(10, 10, 20, 20), Terrain::None); }));
  // Суша внахлёст с существующей: добавляется только море внутри контура, дуг внутри суши не остаётся.
  const double land1 = terrainArea(s.world(), Terrain::Land);
  double c = s.transact("join", [](Tx& tx) { return paintTerrain(tx, rect(940, 60, 990, 120), Terrain::Land); });
  GEO_CHECK_WORLD(s.world(), W, H);
  CHECK_NEAR(c, 50.0 * 60 - 40.0 * 30, 1e-6);
  CHECK_NEAR(terrainArea(s.world(), Terrain::Land), land1 + c, 1e-6 * land1);
  CHECK_EQ(landRings(s.world()).size(), size_t(8));   // остров стал одним контуром, дыра — отдельным
  s.world().edges.each([&](const Edge& e) {
    if (e.kind == EdgeKind::Border) CHECK(!(e.pl == e.pr && e.tl == e.tr));
  });
  geotest::renderWorld(s.world(), "geo_paint_terrain.png", 1.0, Box2(0, 0, W, H));
}

TEST(geo_paint_terrain_provinces) {
  Store s = initStore();
  Id p = s.transact("p", [](Tx& tx) { return createProvince(tx, rect(400, 200, 500, 300), Terrain::Land); });
  Id q = s.transact("q", [](Tx& tx) { return createProvince(tx, rect(900, 20, 990, 100), Terrain::Sea); });
  CHECK_NEAR(provArea(s.world(), p), 10000, 1e-6);
  CHECK_NEAR(provArea(s.world(), q), 7200, 1e-6);
  // Море внутри сухопутной провинции: провинция теряет площадь, её грани остаются сушей.
  s.transact("sea", [](Tx& tx) { paintTerrain(tx, rect(420, 220, 460, 260), Terrain::Sea); });
  GEO_CHECK_WORLD(s.world(), W, H);
  CHECK_NEAR(provArea(s.world(), p), 10000 - 1600, 1e-6);
  CHECK(allTerrain(s.world(), p, Terrain::Land));
  auto fs = buildFaces(s.world());
  CHECK(fs->terrainAt({440, 240}) == Terrain::Sea);
  CHECK_EQ(fs->provinceAt({440, 240}), Id(0));
  // Суша внутри морской провинции: новая суша ничья, провинция теряет площадь.
  s.transact("land", [](Tx& tx) { paintTerrain(tx, rect(940, 40, 960, 60), Terrain::Land); });
  GEO_CHECK_WORLD(s.world(), W, H);
  CHECK_NEAR(provArea(s.world(), q), 7200 - 400, 1e-6);
  CHECK(allTerrain(s.world(), q, Terrain::Sea));
  fs = buildFaces(s.world());
  CHECK(fs->terrainAt({950, 50}) == Terrain::Land);
  CHECK_EQ(fs->provinceAt({950, 50}), Id(0));
}

TEST(geo_coast_handles) {
  Store s = initStore();
  const World& w = s.world();
  // Точка берега малого острова (820, 140): в режиме границ заблокирована, в режиме берега подвижна.
  Handle h = coastPointNear(w, {820, 100});
  CHECK(h.kind == Handle::Point);
  CHECK(handleLocked(w, h));
  CHECK(!handleLocked(w, h, true));
  const Vec2 p0 = handlePos(w, h);
  Handle hit = hitHandle(w, p0 + Vec2(0.4, 0.3), 2, 0, true);
  CHECK(hit == h);
  // Наружу от центра острова: суши больше.
  const Vec2 out = p0 + (p0 - Vec2(820, 140)) * (6.0 / dist(p0, {820, 140}));
  CHECK(!canMove(w, h, out));
  CHECK(canMove(w, h, out, true));
  const double land0 = terrainArea(w, Terrain::Land);
  CHECK_THROWS(s.transact("locked", [&](Tx& tx) { moveHandle(tx, h, out); }));
  s.transact("move", [&](Tx& tx) { moveHandle(tx, h, out, true); });
  GEO_CHECK_WORLD(s.world(), W, H);
  CHECK(terrainArea(s.world(), Terrain::Land) > land0);
  CHECK(handlePos(s.world(), h) == out);
  // Через соседний остров берег не пройдёт.
  CHECK(!canMove(s.world(), h, {860, 470}, true));
  // Вставка и удаление точки берега.
  auto eh = hitEdge(s.world(), out, 1, 0, true);
  CHECK(eh.has_value());
  CHECK(!hitEdge(s.world(), out, 1, 0, false).has_value() || hitEdge(s.world(), out, 1, 0, false)->edge == eh->edge);
  const Edge* ce = s.world().edges.get(eh->edge);
  CHECK(ce && ce->kind == EdgeKind::Coast);
  std::vector<Vec2> co = edgeCoords(s.world(), *ce);
  const int seg = h.index;   // отрезок от точки out к следующей
  const Vec2 mid = (co[size_t(seg + 1)] + co[size_t(seg + 2)]) * 0.5;
  CHECK_THROWS(s.transact("ins-border", [&](Tx& tx) { insertPoint(tx, eh->edge, seg + 1, mid); }));
  Handle np = s.transact("ins", [&](Tx& tx) { return insertPoint(tx, eh->edge, seg + 1, mid, true); });
  GEO_CHECK_WORLD(s.world(), W, H);
  CHECK(np.kind == Handle::Point);
  CHECK_THROWS(s.transact("del-border", [&](Tx& tx) { deletePoint(tx, np); }));
  s.transact("del", [&](Tx& tx) { deletePoint(tx, np, true); });
  GEO_CHECK_WORLD(s.world(), W, H);
  // Узлы рамки закреплены и в режиме берега.
  s.world().nodes.each([&](const Node& n) {
    if (n.p == Vec2(0, 0)) CHECK(handleLocked(s.world(), Handle{Handle::Node, n.id, 0, -1}, true));
  });
}

TEST(geo_coast_junction_moves_freely) {
  Store s = initStore();
  // Провинция на восточной части большого острова: стыки границы с берегом.
  Id a = s.transact("a", [](Tx& tx) { return createProvince(tx, {{560, 60}, {735, 60}, {735, 480}, {560, 480}}, Terrain::Land); });
  std::vector<Id> junctions;
  s.world().nodes.each([&](const Node& n) {
    if (isCoastJunction(s.world(), n.id)) junctions.push_back(n.id);
  });
  CHECK(!junctions.empty());
  const Id j = junctions[0];
  const Handle h{Handle::Node, j, 0, -1};
  const Vec2 p0 = s.world().nodes.get(j)->p;
  CHECK(!handleLocked(s.world(), h, true));
  // В режиме границ стык скользит только вдоль берега, в режиме берега двигается свободно (берег за ним).
  const Vec2 to = p0 + Vec2(0, p0.y < 300 ? -4 : 4);
  CHECK(!canMove(s.world(), h, to));
  const double land0 = terrainArea(s.world(), Terrain::Land);
  s.transact("move", [&](Tx& tx) { moveHandle(tx, h, to, true); });
  GEO_CHECK_WORLD(s.world(), W, H);
  CHECK(s.world().nodes.get(j)->p == to);
  CHECK(terrainArea(s.world(), Terrain::Land) > land0);
  CHECK(isCoastJunction(s.world(), j));
  CHECK(allTerrain(s.world(), a, Terrain::Land));
  // Узел-стык удалить нельзя (к нему подходит граница).
  CHECK_THROWS(s.transact("del", [&](Tx& tx) { deletePoint(tx, h, true); }));
}
