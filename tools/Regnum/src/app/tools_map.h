// Regnum — правка карты (режим «Правка карты», T): суша и берег, воды (озёра и реки), островки, стены, знаки (горы,
// крупные горы, замки, башни). Общие помощники инструментов карты (tools_map_*.cpp) и инспектора объектов карты.
//
// Объекты карты мира рисуются кодом по сцене MapView::artScene(). Пока мир не хранит своих объектов, сцена
// показывает объекты базовой карты; первая правка переносит их в мир (rules::ensureMapObjects) в той же
// транзакции, поэтому одна отмена возвращает и правку, и перенос.
#pragma once
#include "app/tools_edit.h"
#include "map/art_scene.h"

namespace rg::app::mapedit {

// Объекты базовой карты приложения (nullptr — базовой карты нет).
const map::art::Objects* base(const App& a);
// Правка объектов карты: перенос объектов базовой карты в мир (если нужно) и fn — одной транзакцией.
bool act(App& a, std::string_view label, const std::function<void(Tx&)>& fn, const TxOptions& opt = {});

// Порядок отрисовки знака s (поставленного или передвинутого): выше знаков, чьё основание выше по карте, и ниже
// тех, что стоят ниже, — среди пересекающихся с ним. Не пересекается ни с кем — прежний s.z. self — ID самого знака.
double placeZ(const App& a, const World& w, Id self, const MapSymbol& s);
// Наибольший z среди знаков мира (новый знак без соседей — поверх всех).
double topZ(const App& a, const World& w);
// Знаки ids — поверх (front) или под всеми пересекающимися с ними; одна запись отмены.
void restack(App& a, const std::vector<Id>& ids, bool front);

// Названия и значки видов.
const char* symbolName(SymbolKind k);   // «Гора», «Крупная гора», «Замок», «Башня»
const char* shapeName(ShapeKind k);     // «Озеро», «Островок», «Стена», «Река»
const char* symbolIcon(SymbolKind k);
const char* shapeIcon(ShapeKind k);

// Выделенные знаки: группа и выделение (без повторов).
std::vector<Id> selectedSymbols(const App& a);
// Выделить знаки (пусто — снять выделение); последний — в инспекторе.
void selectSymbols(App& a, const std::vector<Id>& ids);

// Подсветка на экране (холст — логические пиксели окна): рамка знака, контур или линия фигуры.
void outlineSymbol(gfx::Canvas& c, const map::View& v, const MapSymbol& s, Color col, float width);
void outlineShape(gfx::Canvas& c, const map::View& v, const MapShape& s, Color col, float width, Vec2 shift = {});
// Значок знака на экране (предпросмотр): точка привязки — на карте at, полупрозрачно alpha.
void ghostSymbol(gfx::Canvas& c, const map::View& v, SymbolKind kind, Vec2 at, float scale, u8 variant, float alpha);

// Регистрация инструментов (статическая; тесты с подставными инструментами вызывают installAll).
void registerObjectsTool();
void registerDrawTools();
void registerCoastTool();

}  // namespace rg::app::mapedit
