// Regnum — правка объектов карты, нарисованных кодом: знаки (горы, замки, башни) и фигуры (воды, островки, стены,
// реки). Суша и берег — граф провинций (geo::paintTerrain, ручки берега).
#include "core/schema.h"
#include "geo/geom.h"
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

Vec2 onMap(Vec2 p) {
  if (!geo::finite(p)) fail("Недопустимые координаты точки");
  return {clamp(p.x, 0.0, schema::kMapWidth), clamp(p.y, 0.0, schema::kMapHeight)};
}

const MapSymbol& needSymbol(const World& w, Id id) {
  const MapSymbol* s = w.symbol(id);
  if (!s) fail("Знак карты не найден");
  return *s;
}

const MapShape& needShape(const World& w, Id id) {
  const MapShape* s = w.shape(id);
  if (!s) fail("Фигура карты не найдена");
  return *s;
}

float scaleOf(float s) {
  if (!std::isfinite(s)) fail("Недопустимый размер знака");
  return clamp(s, schema::kMinSymbolScale, schema::kMaxSymbolScale);
}

// Новые объекты добавляются только в мир, который уже хранит свои (иначе объекты базовой карты пропали бы).
void needOwn(const World& w) {
  if (!w.ownMapObjects()) fail("Объекты карты ещё не перенесены в мир (ensureMapObjects)");
}

u8 variantOf(SymbolKind kind, u8 v) { return kind == SymbolKind::Mountain || kind == SymbolKind::Peak ? u8(std::min<int>(v, 1)) : u8(0); }

// Точки кольца фигуры: ring −1 — контур или линия, иначе остров воды.
std::vector<Vec2>& ringOf(MapShape& s, int ring) {
  if (ring < 0) return s.pts;
  if (ring >= int(s.holes.size())) fail("Остров фигуры не найден");
  return s.holes[size_t(ring)];
}

// Контур после правки не должен пересекать сам себя (если пересекал и до неё — правка не запрещается: данные,
// разобранные из изображения, могут касаться себя в отдельных точках).
void checkRing(const MapShape& s, const std::vector<Vec2>& before, const std::vector<Vec2>& after) {
  if (!s.closed()) return;
  if (!geo::isSimple(after, true) && geo::isSimple(before, true)) fail("Контур не может пересекать сам себя");
}

}  // namespace

void ensureMapObjects(Tx& tx, const std::vector<MapSymbol>& baseSymbols, const std::vector<MapShape>& baseShapes) {
  if (tx.w().ownMapObjects()) return;
  for (const MapSymbol& s : baseSymbols) tx.add(s);
  for (const MapShape& s : baseShapes) tx.add(s);
  tx.meta().mapObjects = true;
}

Id addSymbol(Tx& tx, MapSymbol s) {
  if (int(s.kind) < 0 || s.kind >= SymbolKind::Count) fail("Неизвестный вид знака");
  s.id = 0;
  s.p = onMap(s.p);
  s.s = scaleOf(s.s);
  s.v = variantOf(s.kind, s.v);
  if (!std::isfinite(s.z)) s.z = 0;
  needOwn(tx.w());
  return tx.add(s).id;
}

void placeSymbol(Tx& tx, Id id, Vec2 p, double z) {
  needSymbol(tx.w(), id);
  const Vec2 q = onMap(p);
  MapSymbol& s = tx.symbol(id);
  s.p = q;
  if (std::isfinite(z)) s.z = z;
}

void setSymbol(Tx& tx, Id id, SymbolKind kind, float scale, u8 variant) {
  needSymbol(tx.w(), id);
  if (int(kind) < 0 || kind >= SymbolKind::Count) fail("Неизвестный вид знака");
  const float sc = scaleOf(scale);
  MapSymbol& s = tx.symbol(id);
  s.kind = kind;
  s.s = sc;
  s.v = variantOf(kind, variant);
}

void removeSymbol(Tx& tx, Id id) {
  needSymbol(tx.w(), id);
  tx.eraseSymbol(id);
}

Id addShape(Tx& tx, MapShape s) {
  if (int(s.kind) < 0 || s.kind >= ShapeKind::Count) fail("Неизвестный вид фигуры");
  s.id = 0;
  std::vector<Vec2> pts;
  for (Vec2 p : s.pts) {
    const Vec2 q = onMap(p);
    if (pts.empty() || dist(pts.back(), q) > geo::kMinLen) pts.push_back(q);
  }
  if (s.closed())
    while (pts.size() > 1 && dist(pts.front(), pts.back()) <= geo::kMinLen) pts.pop_back();
  s.pts = std::move(pts);
  if (s.closed()) {
    if (s.pts.size() < 3) fail("Контур должен содержать не менее трёх точек");
    if (!geo::isSimple(s.pts, true)) fail("Контур не может пересекать сам себя");
    if (std::fabs(geo::signedArea(s.pts)) < geo::kMinArea) fail("Контур вырожден: нулевая площадь");
    s.w = s.dash = 0;
  } else {
    if (s.pts.size() < 2) fail("Линия должна содержать не менее двух точек");
    const float def = s.kind == ShapeKind::River ? schema::kRiverWidth : schema::kWallWidth;
    s.w = std::isfinite(s.w) && s.w > 0 ? clamp(s.w, schema::kMinShapeWidth, schema::kMaxShapeWidth) : def;
    s.dash = s.kind == ShapeKind::Wall && std::isfinite(s.dash) ? clamp(s.dash, 0.f, schema::kMaxShapeWidth) : 0.f;
  }
  if (s.kind != ShapeKind::Water) s.holes.clear();
  for (auto& h : s.holes)
    for (Vec2& p : h) p = onMap(p);
  needOwn(tx.w());
  return tx.add(std::move(s)).id;
}

void setShapePoint(Tx& tx, Id id, int ring, int index, Vec2 p) {
  MapShape s = needShape(tx.w(), id);
  std::vector<Vec2>& r = ringOf(s, ring);
  if (index < 0 || index >= int(r.size())) fail("Точка фигуры не найдена");
  const std::vector<Vec2> before = r;
  r[size_t(index)] = onMap(p);
  checkRing(s, before, r);
  tx.shape(id) = std::move(s);
}

void insertShapePoint(Tx& tx, Id id, int ring, int segment, Vec2 p) {
  MapShape s = needShape(tx.w(), id);
  std::vector<Vec2>& r = ringOf(s, ring);
  const int n = int(r.size()), ns = s.closed() ? n : n - 1;
  if (segment < 0 || segment >= ns) fail("Неверный номер отрезка фигуры");
  const Vec2 q = onMap(p);
  if (dist(q, r[size_t(segment)]) <= geo::kMinLen || dist(q, r[size_t((segment + 1) % n)]) <= geo::kMinLen) fail("Точка слишком близко к существующей вершине");
  const std::vector<Vec2> before = r;
  r.insert(r.begin() + segment + 1, q);
  checkRing(s, before, r);
  tx.shape(id) = std::move(s);
}

void removeShapePoint(Tx& tx, Id id, int ring, int index) {
  MapShape s = needShape(tx.w(), id);
  std::vector<Vec2>& r = ringOf(s, ring);
  if (index < 0 || index >= int(r.size())) fail("Точка фигуры не найдена");
  if (int(r.size()) <= (s.closed() ? 3 : 2)) fail(s.closed() ? "В контуре должно остаться не менее трёх точек" : "В линии должно остаться не менее двух точек");
  const std::vector<Vec2> before = r;
  r.erase(r.begin() + index);
  checkRing(s, before, r);
  tx.shape(id) = std::move(s);
}

void moveShape(Tx& tx, Id id, Vec2 delta) {
  MapShape s = needShape(tx.w(), id);
  if (!geo::finite(delta)) fail("Недопустимый сдвиг");
  // Сдвиг ограничивается так, чтобы фигура осталась на карте целиком.
  Box2 b = geo::bounds(s.pts);
  for (const auto& h : s.holes) b.add(geo::bounds(h));
  delta.x = clamp(delta.x, -b.x0, schema::kMapWidth - b.x1);
  delta.y = clamp(delta.y, -b.y0, schema::kMapHeight - b.y1);
  for (Vec2& p : s.pts) p = p + delta;
  for (auto& h : s.holes)
    for (Vec2& p : h) p = p + delta;
  tx.shape(id) = std::move(s);
}

void setShapeLine(Tx& tx, Id id, float width, float dash) {
  const MapShape& s0 = needShape(tx.w(), id);
  if (s0.closed()) fail("Ширина задаётся только у стены и реки");
  if (!std::isfinite(width) || !std::isfinite(dash)) fail("Недопустимая ширина линии");
  MapShape& s = tx.shape(id);
  s.w = clamp(width, schema::kMinShapeWidth, schema::kMaxShapeWidth);
  s.dash = s.kind == ShapeKind::Wall ? clamp(dash, 0.f, schema::kMaxShapeWidth) : 0.f;
}

void removeShape(Tx& tx, Id id) {
  needShape(tx.w(), id);
  tx.eraseShape(id);
}

}  // namespace rg::rules
