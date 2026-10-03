// Regnum — карта мира, нарисованная кодом, как часть мира: сцена для тайлов и попадания мышью.
//
// Сцена строится по миру. Контуры суши — из графа провинций (geo::landRings: берег правится в режиме «Правка
// карты»). Знаки и фигуры (горы, замки, башни, воды, островки, стены, реки) — из таблиц мира World::symbols и
// World::shapes; пока мир их не хранит (World::ownMapObjects() == false), — объекты базовой карты (baseObjects):
// те же записи с теми же ID, которые первая правка карты переносит в мир, поэтому выделение и ссылки не меняются.
#pragma once
#include <memory>

#include "core/world.h"
#include "map/art_render.h"

namespace rg::map::art {

// Объекты базовой карты как записи мира. Знаки — в порядке отрисовки: ID 1…n, z = номер. Фигуры: вода (кольца
// по вложенности: чётная глубина — внешний контур, нечётная — остров ближайшего охватывающего), островки, стены,
// реки — ID 1…m в этом порядке.
struct Objects {
  std::vector<MapSymbol> symbols;
  std::vector<MapShape> shapes;
};
Objects baseObjects(const MapArt& base);

// Габарит записи мира на карте (знак — по значку и масштабу, фигура — по точкам с половиной ширины линии).
Box2 symbolBox(const MapSymbol& s);
Box2 shapeBox(const MapShape& s);
inline Sym symOf(SymbolKind k) { return Sym(int(k)); }

class Scene {
 public:
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  // Сцена мира w. base — базовая карта (стиль, размер, объекты для мира без своих; может быть nullptr), baseObj —
  // её объекты (baseObjects), land — готовые контуры суши (nullptr — по графу мира, без графа — суша base).
  static std::shared_ptr<const Scene> build(const World& w, const MapArt* base, const Objects* baseObj,
                                            std::shared_ptr<const std::vector<Ring>> land = nullptr);

  const MapArt& art() const { return art_; }
  const Index& index() const { return index_; }
  // Контуры суши (для следующей сцены, если берег не менялся).
  const std::shared_ptr<const std::vector<Ring>>& land() const { return land_; }
  bool hasLand() const { return hasLand_; }      // есть суша и море (иначе море не рисуется)
  bool fromWorld() const { return fromWorld_; }  // объекты из таблиц мира
  // ID знака мира для art().symbols[i] (в порядке отрисовки).
  const std::vector<Id>& symbolIds() const { return symbolIds_; }

  // Попадание мышью (единицы карты, tol — допуск): верхний знак под точкой; фигура под точкой (стены и реки —
  // по линии, островки и вода — по площади или краю); знаки, чья точка привязки внутри b.
  Id symbolAt(Vec2 p, double tol) const;
  Id shapeAt(Vec2 p, double tol) const;
  std::vector<Id> symbolsIn(const Box2& b) const;

 private:
  Scene() = default;
  MapArt art_;
  Index index_;
  std::shared_ptr<const std::vector<Ring>> land_;
  bool hasLand_ = false, fromWorld_ = false;
  std::vector<Id> symbolIds_, waterIds_, isletIds_, lineIds_, riverIds_;
};

// Вся карта сцены шириной width (превью, миниатюра): суша, море (если в сцене есть суша), воды, знаки.
gfx::Image renderScene(const Scene& s, int width);

}  // namespace rg::map::art
