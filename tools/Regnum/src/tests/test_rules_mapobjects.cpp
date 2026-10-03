// Тесты правки объектов карты (rules/mapobjects.cpp): перенос объектов базовой карты в мир, знаки, фигуры и их
// точки, пределы, отказ на самопересечении, отмена; суша и море контуром (rules::paintTerrain) удаляют провинции
// без области.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

// «Базовая карта»: два знака и озеро с островом, стена.
std::vector<MapSymbol> baseSymbols() {
  return {MapSymbol{1, SymbolKind::Mountain, {100, 100}, 1, 0, 0}, MapSymbol{2, SymbolKind::Castle, {200, 120}, 1, 0, 1}};
}
std::vector<MapShape> baseShapes() {
  MapShape lake;
  lake.id = 1;
  lake.kind = ShapeKind::Water;
  lake.pts = {{300, 300}, {400, 300}, {400, 400}, {300, 400}};
  lake.holes = {{{340, 340}, {340, 360}, {360, 360}, {360, 340}}};
  MapShape wall;
  wall.id = 2;
  wall.kind = ShapeKind::Wall;
  wall.pts = {{10, 10}, {50, 10}};
  wall.w = 2;
  return {lake, wall};
}

}  // namespace

TEST(rules_map_objects_materialize) {
  Store s;
  s.replace(newWorld("Карта"), "test");
  CHECK(!s.world().ownMapObjects());
  // До переноса добавлять нельзя (объекты базовой карты пропали бы).
  CHECK(!errorOf([&] { s.transact("add", [](Tx& tx) { addSymbol(tx, MapSymbol{}); }); }).empty());
  // Первая правка: перенос с теми же ID, затем правка.
  s.transact("edit", [](Tx& tx) {
    ensureMapObjects(tx, baseSymbols(), baseShapes());
    placeSymbol(tx, 2, {210, 130}, 1);
  });
  const World& w = s.world();
  CHECK(w.meta->mapObjects);
  CHECK_EQ(w.symbols.size(), 2u);
  CHECK_EQ(w.shapes.size(), 2u);
  CHECK(w.symbol(2)->p == Vec2(210, 130));
  CHECK_EQ(w.meta->seq[size_t(Seq::Symbol)], 2u);
  CHECK_EQ(w.meta->seq[size_t(Seq::Shape)], 2u);
  // Повторный перенос ничего не делает.
  World before = w;
  s.transact("again", [](Tx& tx) { ensureMapObjects(tx, baseSymbols(), baseShapes()); });
  CHECK_EQ(World::diff(before, s.world()), 0u);
  // Отмена возвращает мир без своих объектов.
  s.undo();
  s.undo();
  CHECK(!s.world().ownMapObjects());
}

TEST(rules_map_symbols) {
  Store s;
  s.replace(newWorld("Карта"), "test");
  Id a = 0, b = 0;
  s.transact("add", [&](Tx& tx) {
    ensureMapObjects(tx, {}, {});
    a = addSymbol(tx, MapSymbol{77, SymbolKind::Peak, {-50, 9000}, 99, 7, 3});
    b = addSymbol(tx, MapSymbol{0, SymbolKind::Tower, {10, 20}, 1, 1, 0});
  });
  const World& w = s.world();
  CHECK_EQ(a, 1u);   // ID выдаётся по порядку, присланный не учитывается
  CHECK(w.symbol(a)->p == Vec2(0, schema::kMapHeight));   // на краю карты
  CHECK_EQ(w.symbol(a)->s, schema::kMaxSymbolScale);
  CHECK_EQ(int(w.symbol(a)->v), 1);
  CHECK_EQ(int(w.symbol(b)->v), 0);   // у башни рисунок один
  s.transact("set", [&](Tx& tx) { setSymbol(tx, b, SymbolKind::Mountain, 0.01f, 1); });
  CHECK(s.world().symbol(b)->kind == SymbolKind::Mountain);
  CHECK_EQ(s.world().symbol(b)->s, schema::kMinSymbolScale);
  CHECK_EQ(int(s.world().symbol(b)->v), 1);
  s.transact("del", [&](Tx& tx) { removeSymbol(tx, a); });
  CHECK(!s.world().symbol(a));
  CHECK(!errorOf([&] { s.transact("x", [&](Tx& tx) { removeSymbol(tx, a); }); }).empty());
  CHECK(!errorOf([&] { s.transact("x", [&](Tx& tx) { placeSymbol(tx, b, {NAN, 1}, 0); }); }).empty());
}

TEST(rules_map_shapes) {
  Store s;
  s.replace(newWorld("Карта"), "test");
  s.transact("base", [](Tx& tx) { ensureMapObjects(tx, baseSymbols(), baseShapes()); });
  // Новая река и озеро; повтор точек и самопересечение отклоняются.
  Id river = 0, lake = 0;
  s.transact("add", [&](Tx& tx) {
    MapShape r;
    r.kind = ShapeKind::River;
    r.pts = {{500, 500}, {500, 500}, {600, 650}};
    river = addShape(tx, r);
    MapShape l;
    l.kind = ShapeKind::Water;
    l.pts = {{1000, 1000}, {1100, 1000}, {1100, 1100}, {1000, 1100}, {1000, 1000}};
    lake = addShape(tx, l);
  });
  CHECK_EQ(river, 3u);
  CHECK_EQ(s.world().shape(river)->pts.size(), size_t(2));
  CHECK_EQ(s.world().shape(river)->w, schema::kRiverWidth);
  CHECK_EQ(s.world().shape(lake)->pts.size(), size_t(4));
  CHECK(!errorOf([&] {
          s.transact("bow", [](Tx& tx) {
            MapShape b;
            b.kind = ShapeKind::Islet;
            b.pts = {{0, 0}, {10, 10}, {10, 0}, {0, 10}};
            addShape(tx, b);
          });
        }).empty());
  CHECK(!errorOf([&] {
          s.transact("short", [](Tx& tx) {
            MapShape b;
            b.kind = ShapeKind::Wall;
            b.pts = {{0, 0}};
            addShape(tx, b);
          });
        }).empty());
  // Точки: перемещение, вставка, удаление; самопересечение отклоняется, минимум точек сохраняется.
  s.transact("pt", [&](Tx& tx) { setShapePoint(tx, lake, -1, 2, {1150, 1150}); });
  CHECK(s.world().shape(lake)->pts[2] == Vec2(1150, 1150));
  CHECK(!errorOf([&] { s.transact("cross", [&](Tx& tx) { setShapePoint(tx, lake, -1, 0, {1200, 1050}); }); }).empty());
  s.transact("ins", [&](Tx& tx) { insertShapePoint(tx, lake, -1, 0, {1050, 990}); });
  CHECK_EQ(s.world().shape(lake)->pts.size(), size_t(5));
  CHECK(s.world().shape(lake)->pts[1] == Vec2(1050, 990));
  s.transact("rm", [&](Tx& tx) { removeShapePoint(tx, lake, -1, 1); });
  s.transact("rm", [&](Tx& tx) { removeShapePoint(tx, lake, -1, 1); });
  CHECK_EQ(s.world().shape(lake)->pts.size(), size_t(3));
  CHECK(!errorOf([&] { s.transact("rm3", [&](Tx& tx) { removeShapePoint(tx, lake, -1, 0); }); }).empty());
  // Остров озера базовой карты.
  s.transact("hole", [](Tx& tx) { setShapePoint(tx, 1, 0, 0, {335, 335}); });
  CHECK(s.world().shape(1)->holes[0][0] == Vec2(335, 335));
  CHECK(!errorOf([&] { s.transact("nohole", [](Tx& tx) { setShapePoint(tx, 1, 3, 0, {1, 1}); }); }).empty());
  // Сдвиг целиком — в пределах карты.
  s.transact("move", [&](Tx& tx) { moveShape(tx, river, {-10000, 0}); });
  CHECK_NEAR(s.world().shape(river)->pts[0].x, 0, 1e-9);
  CHECK_NEAR(s.world().shape(river)->pts[1].x, 100, 1e-9);
  // Ширина и пунктир: пунктир только у стены, у озера ширины нет.
  s.transact("w", [&](Tx& tx) {
    setShapeLine(tx, river, 1000, 5);
    setShapeLine(tx, 2, 3, 4);
  });
  CHECK_EQ(s.world().shape(river)->w, schema::kMaxShapeWidth);
  CHECK_EQ(s.world().shape(river)->dash, 0.f);
  CHECK_EQ(s.world().shape(2)->dash, 4.f);
  CHECK(!errorOf([&] { s.transact("lw", [&](Tx& tx) { setShapeLine(tx, lake, 3, 0); }); }).empty());
  s.transact("del", [&](Tx& tx) { removeShape(tx, lake); });
  CHECK(!s.world().shape(lake));
}

TEST(rules_paint_terrain_drops_empty_provinces) {
  Fix f;
  // Провинция целиком уходит под море: запись удаляется, название — в ответе.
  const auto fs = geo::buildFaces(f.w());
  const geo::ProvinceShape* sh = fs->shape(f.p[0]);
  CHECK(sh != nullptr);
  const Box2 b = sh->box.inflated(5);
  AreaEdit r;
  f.tx([&](Tx& tx) { r = paintTerrain(tx, {{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}}, Terrain::Sea); });
  CHECK(!f.w().province(f.p[0]));
  CHECK_EQ(r.removed.size(), size_t(1));
  CHECK(geo::validate(f.w()).empty());
}
