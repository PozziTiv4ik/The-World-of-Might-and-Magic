// Regnum — правила: объекты карты — знаки и фигуры (rules/mapobjects.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include "core/world.h"

namespace rg::rules {

// ================================================================ объекты карты (знаки и фигуры)
// Мир без своих объектов показывает объекты базовой карты; первая правка карты переносит их в мир с теми же ID
// (ensureMapObjects — в начале каждой правки знаков и фигур, до неё добавлять нельзя; base — art::baseObjects).
// Точки ограничиваются картой; контур фигуры не может пересекать сам себя; масштаб и ширина — в пределах schema.
void ensureMapObjects(Tx& tx, const std::vector<MapSymbol>& baseSymbols, const std::vector<MapShape>& baseShapes);
Id addSymbol(Tx& tx, MapSymbol s);                                 // ID выдаётся (s.id не учитывается)
void placeSymbol(Tx& tx, Id symbol, Vec2 p, double z);             // точка привязки и порядок отрисовки
void setSymbol(Tx& tx, Id symbol, SymbolKind kind, float scale, u8 variant);
void removeSymbol(Tx& tx, Id symbol);
Id addShape(Tx& tx, MapShape s);                                   // ID выдаётся; контур — от 3 точек, линия — от 2
// Точки фигуры: ring −1 — контур или линия, ring ≥ 0 — остров воды.
void setShapePoint(Tx& tx, Id shape, int ring, int index, Vec2 p);
void insertShapePoint(Tx& tx, Id shape, int ring, int segment, Vec2 p);   // после точки segment
void removeShapePoint(Tx& tx, Id shape, int ring, int index);
void moveShape(Tx& tx, Id shape, Vec2 delta);                      // сдвиг целиком (в пределах карты)
void setShapeLine(Tx& tx, Id shape, float width, float dash);      // стена и река: ширина, пунктир (только стена)
void removeShape(Tx& tx, Id shape);

}  // namespace rg::rules
