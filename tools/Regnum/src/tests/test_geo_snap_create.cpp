// Тесты geo: новая провинция прилегает к соседям, не обрезая их (ТЗ «Доработки №1», п.1). Контур, зашедший на
// существующую провинцию, даёт границу ровно по её границе (общие дуги), без висячих обрывков, щелей и лишних
// узлов: частично на соседе, через двух соседей, вдоль границы с отклонениями туда-сюда (узкие щели достаются
// новой провинции, широкие остаются свободными), почти по границе, касание в точке, у берега, вокруг провинции
// и острова, море у суши, насквозь через узкого соседа, узкий проход между соседями, отказы, отмена и
// детерминизм, случайные серии.
#include <cstdlib>
#include <map>

#include "tests/test_geo_util.h"

using namespace rg;
using namespace rg::geo;

namespace {

constexpr double W = 1000, H = 600;

std::vector<Vec2> rect(double x0, double y0, double x1, double y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }

// Берег: большой квадратный остров [100, 900] × [100, 500] и островок [930, 970] × [30, 70].
Coast squareCoast() {
  Coast c;
  c.width = W;
  c.height = H;
  c.landRings.push_back(rect(100, 100, 900, 500));
  c.landRings.push_back(rect(930, 30, 970, 70));
  return c;
}
constexpr double kLand = 800.0 * 400 + 40.0 * 40;

double provArea(const World& w, Id p) {
  auto fs = buildFaces(w);
  auto* s = fs->shape(p);
  return s ? s->area : 0;
}

size_t provFaces(const World& w, Id p) {
  auto fs = buildFaces(w);
  auto* s = fs->shape(p);
  return s ? s->faces.size() : 0;
}

size_t faceCount(const World& w) { return buildFaces(w)->faces.size(); }

double terrainArea(const World& w, Terrain t, Id prov = Id(-1)) {
  auto fs = buildFaces(w);
  double a = 0;
  for (auto& f : fs->faces)
    if (f.terrain == t && (prov == Id(-1) || f.province == prov)) a += f.area;
  return a;
}

bool allTerrain(const World& w, Id p, Terrain t) {
  auto fs = buildFaces(w);
  auto* s = fs->shape(p);
  if (!s) return false;
  for (int f : s->faces)
    if (fs->faces[size_t(f)].terrain != t) return false;
  return true;
}

bool hasPair(const World& w, Id a, Id b) {
  auto nb = buildFaces(w)->neighbors();
  return std::find(nb.begin(), nb.end(), std::make_pair(std::min(a, b), std::max(a, b))) != nb.end();
}

bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

// Кольца всех граней провинции.
std::vector<std::vector<Vec2>> ringsOf(const World& w, Id p) {
  auto fs = buildFaces(w);
  std::vector<std::vector<Vec2>> r;
  if (auto* sh = fs->shape(p))
    for (int f : sh->faces)
      for (auto& ring : fs->faces[size_t(f)].rings) r.push_back(ring);
  return r;
}

// Область провинции не изменилась: та же площадь, каждая точка колец лежит на прежней границе и наоборот
// (новые точки допустимы только как узлы стыков на прежних отрезках).
bool sameShape(const World& before, const World& after, Id p) {
  auto ra = ringsOf(before, p), rb = ringsOf(after, p);
  if (ra.empty() || rb.empty()) return false;
  double a0 = provArea(before, p), a1 = provArea(after, p);
  if (std::fabs(a0 - a1) > 1e-9 * std::max(1.0, a0)) return false;
  for (auto& r : rb)
    for (Vec2 q : r)
      if (distToRings(q, ra) > 1e-9) return false;
  for (auto& r : ra)
    for (Vec2 q : r)
      if (distToRings(q, rb) > 1e-9) return false;
  return true;
}

// Лишние узлы: узел степени 2 между двумя разными пограничными дугами с одинаковыми метками сторон, которые
// можно слить без петли, — очистка обязана их убирать (точки разрезов контуром, висячие стыки).
int extraNodes(const World& w) {
  std::map<Id, std::vector<const Edge*>> inc;
  w.edges.each([&](const Edge& e) {
    inc[e.a].push_back(&e);
    if (e.b != e.a) inc[e.b].push_back(&e);
  });
  int n = 0;
  for (auto& [node, es] : inc) {
    if (es.size() != 2 || es[0] == es[1]) continue;
    const Edge &x = *es[0], &y = *es[1];
    if (x.kind != EdgeKind::Border || y.kind != EdgeKind::Border || x.a == x.b || y.a == y.b) continue;
    // x входит в узел, y выходит из него
    bool xIn = x.b == node, yOut = y.a == node;
    Id xFar = xIn ? x.a : x.b, yFar = yOut ? y.b : y.a;
    if (xFar == yFar) continue;  // слияние дало бы петлю
    auto lbl = [](const Edge& e, bool fwd, bool left) {
      bool l = fwd ? left : !left;
      return l ? std::pair{e.pl, int(e.tl)} : std::pair{e.pr, int(e.tr)};
    };
    if (lbl(x, xIn, true) == lbl(y, yOut, true) && lbl(x, xIn, false) == lbl(y, yOut, false)) n++;
  }
  return n;
}

// Граф корректен, грани покрывают карту, суша сохранена, лишних узлов нет.
#define SNAP_CHECK_WORLD(w)                                                     \
  do {                                                                          \
    GEO_CHECK_WORLD(w, W, H);                                                   \
    CHECK_NEAR(terrainArea(w, Terrain::Land), kLand, 1e-6 * kLand);              \
    CHECK_EQ(extraNodes(w), 0);                                                 \
  } while (0)

// Дуги между провинциями a и b: суммарная длина; all — все точки удовлетворяют условию.
template <class Pred> double sharedLength(const World& w, Id a, Id b, Pred&& pred, bool* all) {
  double len = 0;
  *all = true;
  w.edges.each([&](const Edge& e) {
    if (!((e.pl == a && e.pr == b) || (e.pl == b && e.pr == a))) return;
    auto c = edgeCoords(w, e);
    for (size_t i = 0; i + 1 < c.size(); i++) len += dist(c[i], c[i + 1]);
    for (Vec2 p : c) *all = *all && pred(p);
  });
  return len;
}

struct Fix {
  Store s;
  Fix() {
    s.transact("init", [](Tx& tx) { initFromCoast(tx, squareCoast()); });
    s.clearHistory();
  }
  const World& w() const { return s.world(); }
  Id create(const std::vector<Vec2>& p, Terrain t = Terrain::Land, double snap = 1.0) {
    return s.transact("create", [&](Tx& tx) { return createProvince(tx, p, t, EditOptions{snap}); });
  }
  // Сообщение отказа (пусто — операция прошла); отказ не меняет мир.
  std::string error(const std::vector<Vec2>& p, Terrain t = Terrain::Land, double snap = 1.0) {
    World before = s.world();
    try {
      s.transact("probe", [&](Tx& tx) { createProvince(tx, p, t, EditOptions{snap}); });
    } catch (const UserError& e) {
      CHECK_EQ(World::diff(before, s.world()), 0u);
      return e.what();
    }
    return {};
  }
};

}  // namespace

// Контур частично на соседе: сосед цел, граница новой провинции — его прежняя граница (общая дуга по x = 400).
TEST(geo_snap_create_partial_on_neighbor) {
  Fix f;
  Id x = f.create(rect(300, 200, 400, 300));
  const World w0 = f.w();
  size_t faces0 = faceCount(w0);
  Id n = f.create(rect(370, 220, 460, 280));
  SNAP_CHECK_WORLD(f.w());
  CHECK_NEAR(provArea(f.w(), n), 60.0 * 60, 1e-6);
  CHECK(sameShape(w0, f.w(), x));
  CHECK(hasPair(f.w(), x, n));
  CHECK_EQ(faceCount(f.w()), faces0 + 1);
  CHECK_EQ(provFaces(f.w(), n), size_t(1));
  bool onBorder = false;
  double len = sharedLength(f.w(), x, n, [](Vec2 p) { return p.x == 400; }, &onBorder);
  CHECK_NEAR(len, 60, 1e-9);
  CHECK(onBorder);
  geotest::renderWorld(f.w(), "geo_snap_partial.png", 3.0, Box2(280, 180, 480, 320), rect(370, 220, 460, 280));
}

// Через двух соседей: оба целы, общий узел трёх областей остаётся одним узлом.
TEST(geo_snap_create_two_neighbors) {
  Fix f;
  Id x = f.create(rect(300, 200, 400, 300));
  Id y = f.create(rect(400, 200, 500, 300));
  const World w0 = f.w();
  Id n = f.create(rect(350, 150, 450, 250));
  SNAP_CHECK_WORLD(f.w());
  CHECK_NEAR(provArea(f.w(), n), 100.0 * 50, 1e-6);
  CHECK(sameShape(w0, f.w(), x));
  CHECK(sameShape(w0, f.w(), y));
  CHECK(hasPair(f.w(), x, n) && hasPair(f.w(), y, n) && hasPair(f.w(), x, y));
  bool onX = false, onY = false;
  CHECK_NEAR(sharedLength(f.w(), x, n, [](Vec2 p) { return p.y == 200; }, &onX), 50, 1e-9);
  CHECK_NEAR(sharedLength(f.w(), y, n, [](Vec2 p) { return p.y == 200; }, &onY), 50, 1e-9);
  CHECK(onX && onY);
  int at = 0;
  f.w().nodes.each([&](const Node& nd) { at += nd.p == Vec2(400, 200); });
  CHECK_EQ(at, 1);
  geotest::renderWorld(f.w(), "geo_snap_two.png", 3.0, Box2(280, 130, 520, 320), rect(350, 150, 450, 250));
}

// Вдоль границы с отклонениями туда-сюда: узкие щели между контуром и соседом достаются новой провинции —
// граница ровно по соседу; широкий карман (вписанный круг больше полутора допусков) остаётся свободным.
TEST(geo_snap_create_weave_along_border) {
  Fix f;
  Id x = f.create(rect(300, 200, 400, 300));
  Id x2 = f.create(rect(300, 350, 400, 450));
  const World w0 = f.w();
  size_t faces0 = faceCount(w0);
  const std::vector<Vec2> narrow{{480, 210}, {394, 210}, {406, 230}, {394, 250}, {406, 270}, {394, 290}, {480, 290}};
  Id n = f.create(narrow, Terrain::Land, 4.0);
  SNAP_CHECK_WORLD(f.w());
  CHECK_NEAR(provArea(f.w(), n), 80.0 * 80, 1e-6);
  CHECK_EQ(provFaces(f.w(), n), size_t(1));
  CHECK_EQ(faceCount(f.w()), faces0 + 1);  // без щелевых граней
  CHECK(sameShape(w0, f.w(), x));
  bool onBorder = false;
  CHECK_NEAR(sharedLength(f.w(), x, n, [](Vec2 p) { return p.x == 400; }, &onBorder), 80, 1e-6);
  CHECK(onBorder);
  // широкий карман: треугольник между границей x = 400 и вершиной (440, 400)
  const std::vector<Vec2> wide{{480, 360}, {394, 360}, {440, 400}, {394, 440}, {480, 440}};
  const World w1 = f.w();
  Id m = f.create(wide, Terrain::Land, 4.0);
  SNAP_CHECK_WORLD(f.w());
  const double y1 = 360 + 40.0 * 6 / 46, y2 = 400 + 40.0 * 40 / 46;
  const double pocket = 0.5 * (y2 - y1) * 40;
  CHECK_NEAR(provArea(f.w(), m), 80.0 * 80 - pocket, 1e-6);
  auto fs = buildFaces(f.w());
  CHECK_EQ(fs->provinceAt({410, 400}), Id(0));
  CHECK(fs->terrainAt({410, 400}) == Terrain::Land);
  CHECK_NEAR(fs->faces[size_t(fs->locate({410, 400}))].area, pocket, 1e-6);
  CHECK(sameShape(w1, f.w(), x2));
  CHECK(sameShape(w1, f.w(), x));
  CHECK(sameShape(w1, f.w(), n));
  geotest::renderWorld(f.w(), "geo_snap_weave.png", 3.0, Box2(280, 180, 500, 470));
}

// Контур почти по границе (ошибка ввода меньше допуска): вершины прилипают к соседу, щелей и лишних узлов нет.
TEST(geo_snap_create_almost_on_border) {
  Fix f;
  Id x = f.create(rect(300, 200, 400, 300));
  const World w0 = f.w();
  size_t faces0 = faceCount(w0), nodes0 = w0.nodes.size();
  Id n = f.create({{400.4, 200.3}, {470, 200}, {470, 300}, {399.7, 299.6}, {400.3, 280}, {399.6, 250}, {400.2, 220}});
  SNAP_CHECK_WORLD(f.w());
  CHECK_NEAR(provArea(f.w(), n), 70.0 * 100, 1e-6);
  CHECK(sameShape(w0, f.w(), x));
  CHECK_EQ(faceCount(f.w()), faces0 + 1);
  bool onBorder = false;
  CHECK_NEAR(sharedLength(f.w(), x, n, [](Vec2 p) { return p.x == 400; }, &onBorder), 100, 1e-9);
  CHECK(onBorder);
  // узлы: стыки трёх областей в углах (400, 200) и (400, 300); точек разрезов на границе x = 400 не осталось
  CHECK(f.w().nodes.size() <= nodes0 + 2);
  f.w().edges.each([&](const Edge& e) {
    if ((e.pl == x && e.pr == n) || (e.pl == n && e.pr == x)) CHECK(e.pts.empty());
  });
}

// Касание в точке: в узле соседа и в середине его стороны. Сосед цел, общей дуги нет.
TEST(geo_snap_create_touch_point) {
  Fix f;
  Id x = f.create(rect(300, 200, 400, 300));
  World w0 = f.w();
  Id a = f.create({{400.3, 300.2}, {460, 340}, {420, 370}});
  SNAP_CHECK_WORLD(f.w());
  CHECK_NEAR(provArea(f.w(), a), 0.5 * std::fabs(60.0 * 70 - 40.0 * 20), 1e-6);
  CHECK(sameShape(w0, f.w(), x));
  CHECK(!hasPair(f.w(), x, a));
  w0 = f.w();
  Id b = f.create({{350, 300.4}, {380, 350}, {320, 350}});
  SNAP_CHECK_WORLD(f.w());
  CHECK_NEAR(provArea(f.w(), b), 0.5 * 60 * 50, 1e-6);
  CHECK(sameShape(w0, f.w(), x));
  CHECK(sameShape(w0, f.w(), a));
  CHECK(!hasPair(f.w(), x, b));
  geotest::renderWorld(f.w(), "geo_snap_touch.png", 3.0, Box2(280, 180, 480, 390));
}

// Контур вокруг провинции и вокруг острова: охваченная провинция цела, новая — кольцо вокруг неё;
// занятый остров суша не берёт (отказ), морская провинция тем же контуром — море вокруг острова.
TEST(geo_snap_create_encloses_province_and_island) {
  Fix f;
  Id x = f.create(rect(400, 350, 460, 410));
  World w0 = f.w();
  Id ring = f.create(rect(380, 330, 480, 430));
  SNAP_CHECK_WORLD(f.w());
  CHECK_NEAR(provArea(f.w(), ring), 100.0 * 100 - 60.0 * 60, 1e-6);
  CHECK(sameShape(w0, f.w(), x));
  {
    auto fs = buildFaces(f.w());
    CHECK_EQ(fs->shape(ring)->faces.size(), size_t(1));
    CHECK_EQ(fs->faces[size_t(fs->shape(ring)->faces[0])].rings.size(), size_t(2));
  }
  Id isl = f.s.transact("isl", [](Tx& tx) { return fillAt(tx, {950, 50}, 0); });
  std::string err = f.error(rect(910, 10, 990, 90));
  CHECK_MSG(contains(err, "нет свободной суши"), err);
  w0 = f.w();
  Id sea = f.create(rect(910, 10, 990, 90), Terrain::Sea);
  SNAP_CHECK_WORLD(f.w());
  CHECK(f.w().province(sea)->sea);
  CHECK_NEAR(provArea(f.w(), sea), 80.0 * 80 - 40.0 * 40, 1e-6);
  CHECK(allTerrain(f.w(), sea, Terrain::Sea));
  CHECK(sameShape(w0, f.w(), isl));
  CHECK(hasPair(f.w(), isl, sea));
}

// У берега: контур заходит на соседа и в море — берётся только свободная суша, граница по соседу и по берегу.
TEST(geo_snap_create_at_coast) {
  Fix f;
  Id x = f.create(rect(700, 50, 950, 250));
  CHECK_NEAR(provArea(f.w(), x), 200.0 * 150 + 20.0 * 20, 1e-6);  // и угол островка
  const World w0 = f.w();
  Id n = f.create(rect(650, 180, 950, 300));
  SNAP_CHECK_WORLD(f.w());
  CHECK_NEAR(provArea(f.w(), n), 50.0 * 70 + 250.0 * 50, 1e-6);
  CHECK(allTerrain(f.w(), n, Terrain::Land));
  CHECK(sameShape(w0, f.w(), x));
  CHECK(hasPair(f.w(), x, n));
  f.w().edges.each([&](const Edge& e) {
    if (e.kind == EdgeKind::Border) CHECK(e.tl == Terrain::Land && e.tr == Terrain::Land);
  });
  geotest::renderWorld(f.w(), "geo_snap_coast.png", 2.0, Box2(620, 30, 980, 330));
}

// Морская провинция у суши и у другой морской: суша и соседнее море не задеты.
TEST(geo_snap_create_sea_next_to_land) {
  Fix f;
  Id s0 = f.create(rect(910, 200, 990, 300), Terrain::Sea);
  CHECK_NEAR(provArea(f.w(), s0), 80.0 * 100, 1e-6);
  const World w0 = f.w();
  Id n = f.create(rect(860, 150, 960, 350), Terrain::Sea);
  SNAP_CHECK_WORLD(f.w());
  CHECK(f.w().province(n)->sea);
  CHECK_NEAR(provArea(f.w(), n), 60.0 * 200 - 50.0 * 100, 1e-6);
  CHECK(allTerrain(f.w(), n, Terrain::Sea));
  CHECK(sameShape(w0, f.w(), s0));
  CHECK(hasPair(f.w(), s0, n));
  CHECK_EQ(buildFaces(f.w())->provinceAt({880, 250}), Id(0));  // суша не назначена
}

// Насквозь через узкого соседа: участки свободной суши по обе стороны — одна провинция из двух граней.
TEST(geo_snap_create_through_thin_neighbor) {
  Fix f;
  Id x = f.create(rect(400, 200, 410, 300));
  const World w0 = f.w();
  Id n = f.create(rect(350, 220, 420, 280));
  SNAP_CHECK_WORLD(f.w());
  CHECK_NEAR(provArea(f.w(), n), 50.0 * 60 + 10.0 * 60, 1e-6);
  CHECK_EQ(provFaces(f.w(), n), size_t(2));
  CHECK(sameShape(w0, f.w(), x));
}

// Узкий замкнутый проход между соседями: у контура — достаётся новой провинции, далеко от контура — остаётся.
TEST(geo_snap_create_narrow_passage) {
  auto setup = [](Fix& f) {
    f.create(rect(200, 200, 300, 400));
    f.create(rect(302, 200, 400, 400));
    f.create(rect(200, 150, 400, 200));
    f.create(rect(200, 400, 400, 450));
    auto fs = buildFaces(f.w());
    CHECK_EQ(fs->provinceAt({301, 300}), Id(0));
    CHECK_NEAR(fs->faces[size_t(fs->locate({301, 300}))].area, 2.0 * 200, 1e-6);
  };
  {
    Fix f;
    setup(f);
    const World w0 = f.w();
    Id n = f.create(rect(250, 280, 350, 320));
    SNAP_CHECK_WORLD(f.w());
    CHECK_NEAR(provArea(f.w(), n), 2.0 * 40, 1e-6);
    auto fs = buildFaces(f.w());
    CHECK_EQ(fs->provinceAt({301, 240}), Id(0));
    CHECK_EQ(fs->provinceAt({301, 360}), Id(0));
    w0.provinces.each([&](const Province& p) { CHECK(sameShape(w0, f.w(), p.id)); });
  }
  {
    Fix f;
    setup(f);
    const World w0 = f.w();
    Id n = f.create(rect(250, 215, 350, 385), Terrain::Land, 10.0);
    SNAP_CHECK_WORLD(f.w());
    CHECK_NEAR(provArea(f.w(), n), 2.0 * 200, 1e-6);
    CHECK_EQ(provFaces(f.w(), n), size_t(1));
    w0.provinces.each([&](const Province& p) { CHECK(sameShape(w0, f.w(), p.id)); });
  }
}

// Отказы с понятными сообщениями; мир не меняется.
TEST(geo_snap_create_refusals) {
  Fix f;
  f.create(rect(300, 200, 400, 300));
  std::string err = f.error(rect(320, 220, 380, 280));
  CHECK_MSG(contains(err, "нет свободной суши"), err);
  err = f.error(rect(10, 10, 60, 60));
  CHECK_MSG(contains(err, "не захватывает сушу"), err);
  err = f.error(rect(10, 10, 60, 60), Terrain::Sea);
  CHECK(err.empty());
  err = f.error(rect(20, 20, 50, 50), Terrain::Sea);
  CHECK_MSG(contains(err, "нет свободного моря"), err);
  err = f.error({{350, 150}, {450, 250}, {450, 150}, {350, 250}});
  CHECK_MSG(contains(err, "пересекает сам себя"), err);
}

// Отмена и повтор возвращают в точности прежние версии; одинаковые действия дают побайтно одинаковый граф.
TEST(geo_snap_create_undo_determinism) {
  auto run = [] {
    Fix f;
    f.create(rect(300, 200, 400, 300));
    f.create(rect(400, 200, 500, 300));
    std::vector<World> snaps{f.w()};
    f.create({{480, 160}, {394, 160}, {394, 210}, {406, 230}, {394, 250}, {450, 250}}, Terrain::Land, 4.0);
    snaps.push_back(f.w());
    f.create(rect(350, 150, 600, 330), Terrain::Land, 2.0);
    snaps.push_back(f.w());
    SNAP_CHECK_WORLD(f.w());
    for (size_t i = snaps.size() - 1; i > 0; i--) {
      CHECK(f.s.undo());
      CHECK_EQ(World::diff(f.w(), snaps[i - 1]), 0u);
    }
    for (size_t i = 1; i < snaps.size(); i++) {
      CHECK(f.s.redo());
      CHECK_EQ(World::diff(f.w(), snaps[i]), 0u);
    }
    std::vector<std::string> dump;
    f.w().nodes.each([&](const Node& n) { dump.push_back(strf("n%u %.17g %.17g", n.id, n.p.x, n.p.y)); });
    f.w().edges.each([&](const Edge& e) {
      std::string s = strf("e%u %u %u %d %u %u %d %d", e.id, e.a, e.b, int(e.kind), e.pl, e.pr, int(e.tl), int(e.tr));
      for (auto& p : e.pts) s += strf(" %.17g %.17g", p.x, p.y);
      dump.push_back(s);
    });
    return dump;
  };
  auto d1 = run(), d2 = run();
  CHECK(!d1.empty());
  CHECK(d1 == d2);
}

// Случайные серии: прямоугольники внахлёст и обводки вдоль границ соседей с отклонениями, вырезание и снятие
// назначения (свободные участки между провинциями). После каждой новой провинции соседи не меняются, граф
// корректен, суша сохранена, лишних узлов нет; узких щелей у контура между новой провинцией и соседями нет.
// Для расследований: GEO_SNAP_SEEDS — число зёрен (по умолчанию 3, с 7), GEO_SNAP_STEPS — шагов на зерно (90).
TEST(geo_snap_create_random) {
  const char* ns = std::getenv("GEO_SNAP_SEEDS");
  const char* st = std::getenv("GEO_SNAP_STEPS");
  const int seeds = ns ? std::max(1, std::atoi(ns)) : 3, steps = st ? std::max(1, std::atoi(st)) : 90;
  for (u64 seed = 7; seed < 7 + u64(seeds); seed++) {
    Fix f;
    Rng rng(seed);
    auto u = [&](double a, double b) { return a + (b - a) * rng.uniform(); };
    int created = 0, woven = 0;
    for (int step = 0; step < steps; step++) {
      std::map<Id, double> a0;
      for (auto& [id, sh] : buildFaces(f.w())->provinces) a0[id] = sh.area;
      std::vector<Id> live;
      for (auto& [id, v] : a0) live.push_back(id);
      int kind = rng.range(0, 9);
      std::vector<Vec2> poly;
      double snap = rng.uniform() < 0.5 ? 1.0 : 4.0;
      bool weave = false;
      if (kind <= 3 || live.empty()) {
        Vec2 c{u(120, 880), u(120, 480)};
        Vec2 d{u(15, 90), u(15, 70)};
        poly = rect(c.x - d.x, c.y - d.y, c.x + d.x, c.y + d.y);
      } else if (kind <= 7) {
        // обводка части внешнего кольца соседа с отклонениями ±dev, затем отступ наружу
        Id p = live[size_t(rng.range(0, int(live.size()) - 1))];
        auto rings = ringsOf(f.w(), p);
        std::vector<Vec2> ring = rings[0];
        if (signedArea(ring) < 0) std::reverse(ring.begin(), ring.end());  // внутренность слева
        double per = 0;
        for (size_t i = 0; i < ring.size(); i++) per += dist(ring[i], ring[(i + 1) % ring.size()]);
        double start = u(0, per), len = u(0.2, 0.45) * per, step2 = u(6, 14), dev = u(0.4, 2.6) * snap;
        auto at = [&](double s, Vec2& dir) {
          s = std::fmod(s, per);
          for (size_t i = 0;; i = (i + 1) % ring.size()) {
            Vec2 a = ring[i], b = ring[(i + 1) % ring.size()];
            double l = dist(a, b);
            if (s <= l || l == 0) {
              dir = l > 0 ? (b - a) * (1.0 / l) : Vec2(1, 0);
              return l > 0 ? a + dir * s : a;
            }
            s -= l;
          }
        };
        std::vector<Vec2> along;
        int k = 0;
        Vec2 dir, out0, out1;
        for (double s = start; s <= start + len; s += step2, k++) {
          Vec2 q = at(s, dir);
          Vec2 outward{dir.y, -dir.x};  // справа от направления обхода — снаружи
          if (k == 0) out0 = outward;
          out1 = outward;
          along.push_back(q + outward * ((k % 2 ? 1.0 : -1.0) * dev));
        }
        if (along.size() < 3) continue;
        double D = u(30, 70);
        poly = along;
        poly.push_back(along.back() + out1 * D);
        poly.push_back(along.front() + out0 * D);
        std::reverse(poly.begin(), poly.end());
        weave = true;
      } else if (kind == 8 && !live.empty()) {
        Id p = live[size_t(rng.range(0, int(live.size()) - 1))];
        Vec2 c{u(120, 880), u(120, 480)};
        try {
          f.s.transact("cut", [&](Tx& tx) { removeArea(tx, p, rect(c.x - 30, c.y - 20, c.x + 30, c.y + 20)); });
        } catch (const UserError&) {
        }
        SNAP_CHECK_WORLD(f.w());
        continue;
      } else {
        if (live.empty()) continue;
        Id p = live[size_t(rng.range(0, int(live.size()) - 1))];
        f.s.transact("un", [&](Tx& tx) { unassign(tx, p); });
        SNAP_CHECK_WORLD(f.w());
        continue;
      }
      const World before = f.w();
      Id n = 0;
      try {
        n = f.create(poly, Terrain::Land, snap);
      } catch (const UserError&) {
        CHECK_EQ(World::diff(before, f.w()), 0u);
        continue;
      }
      created++;
      woven += weave;
      std::string at = " seed " + std::to_string(seed) + " step " + std::to_string(step);
      auto is = validate(f.w());
      CHECK_MSG(is.empty(), geotest::issuesText(is) + at);
      CHECK_MSG(extraNodes(f.w()) == 0, "лишние узлы" + at);
      CHECK_NEAR(terrainArea(f.w(), Terrain::Land), kLand, 1e-6 * kLand);
      auto fs = buildFaces(f.w());
      CHECK(fs->shape(n) && fs->shape(n)->area > 0);
      CHECK(allTerrain(f.w(), n, Terrain::Land));
      for (auto& [id, a] : a0) {
        const ProvinceShape* sh = fs->shape(id);
        CHECK_MSG(sh && std::fabs(sh->area - a) <= 1e-6 * std::max(1.0, a), "изменилась провинция " + std::to_string(id) + at);
      }
      // у контура не осталось узких свободных щелей между новой провинцией и соседями (у контура — в его габаритах
      // с запасом два допуска: операция берёт запас три допуска от контура после прилипания, сдвигающего вершины
      // не дальше допуска)
      Box2 near = bounds(poly).inflated(2.0 * snap);
      for (const Face& fc : fs->faces) {
        if (fc.province || fc.terrain != Terrain::Land) continue;
        if (!(fc.box.x0 >= near.x0 && fc.box.x1 <= near.x1 && fc.box.y0 >= near.y0 && fc.box.y1 <= near.y1)) continue;
        bool byNew = false, byOther = false;
        for (auto& ring : fc.ringEdges)
          for (const HalfEdge& h : ring) {
            const Edge* e = f.w().edges.get(h.edge);
            Id o = h.forward ? e->pr : e->pl;
            Terrain ot = h.forward ? e->tr : e->tl;
            if (o == n) byNew = true;
            else if (o && ot == Terrain::Land) byOther = true;
          }
        if (!byNew || !byOther) continue;
        double r = 0;
        Vec2 pole = polylabel(fc.rings, 0.05 * snap, &r);
        if (r <= 1.4 * snap) {
          Box2 v = fc.box.inflated(20);
          geotest::renderWorld(before, "geo_snap_random_fail_before.png", 600.0 / std::max(v.w(), v.h()), v, poly);
          geotest::renderWorld(f.w(), "geo_snap_random_fail.png", 600.0 / std::max(v.w(), v.h()), v, poly);
        }
        Box2 pb = bounds(poly);
        CHECK_MSG(r > 1.4 * snap, "узкая щель у новой провинции" + at +
                                      strf(": r %.3f, snap %.0f, щель (%.2f, %.2f), площадь %.3f, габариты %.3f %.3f %.3f %.3f, контур %.3f %.3f %.3f %.3f",
                                           r, snap, pole.x, pole.y, fc.area, fc.box.x0, fc.box.y0, fc.box.x1, fc.box.y1, pb.x0, pb.y0, pb.x1, pb.y1));
      }
    }
    std::printf("  snap-create %llu: created %d (woven %d)\n", (unsigned long long)seed, created, woven);
    CHECK(created > steps / 5);
    CHECK(woven > steps / 20);
    geotest::renderWorld(f.w(), "geo_snap_random_" + std::to_string(seed) + ".png", 1.0, Box2(0, 0, W, H));
  }
}
