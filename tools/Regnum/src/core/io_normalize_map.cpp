// Regnum — нормализация: узлы и дуги графа провинций, знаки и фигуры карты.
#include "core/io_normalize.h"

namespace rg::io::norm {

void Norm::nodes() {
  const World base = w();
  base.nodes.each([&](const Node& n) {
    if (!std::isfinite(n.p.x) || !std::isfinite(n.p.y)) {
      warn(F_GEO, "nodes." + std::to_string(n.id), "координаты узла не являются числами — узел удалён");
      tx.eraseNode(n.id);
      return;
    }
    Vec2 p = n.p;
    if (fixPoint(p, true, F_GEO, "nodes." + std::to_string(n.id), "p")) tx.node(n.id).p = p;
  });
}

void Norm::edges() {
  const World base = w();
  base.edges.each([&](const Edge& e0) {
    std::string where = "edges." + std::to_string(e0.id);
    if (!exists(Seq::Node, e0.a) || !exists(Seq::Node, e0.b)) {
      warn(F_GEO, where, "дуга ссылается на несуществующий узел " + std::to_string(exists(Seq::Node, e0.a) ? e0.b : e0.a) + " — дуга удалена");
      tx.eraseEdge(e0.id);
      return;
    }
    Edge e = e0;
    bool ch = fixPoints(e.pts, F_GEO, where, "pts");
    if (e.a == e.b && e.pts.size() < 2) {
      warn(F_GEO, where, "замкнутая дуга без промежуточных точек — дуга удалена");
      tx.eraseEdge(e.id);
      return;
    }
    ch |= fixEnum(e.kind, 3, EdgeKind::Border, F_GEO, where, "kind");
    ch |= fixEnum(e.tl, 3, Terrain::Land, F_GEO, where, "tl");
    ch |= fixEnum(e.tr, 3, Terrain::Land, F_GEO, where, "tr");
    ch |= fixRef(e.pl, Seq::Province, F_GEO, where, "pl");
    ch |= fixRef(e.pr, Seq::Province, F_GEO, where, "pr");
    if (ch) tx.edge(e.id) = std::move(e);
  });
}

// Знаки и фигуры карты: пределы масштаба и ширины, точки на карте, достаточное число вершин.
void Norm::mapObjects() {
  const World base = w();
  base.symbols.each([&](const MapSymbol& s0) {
    const std::string where = "symbols." + std::to_string(s0.id);
    MapSymbol s = s0;
    bool ch = fixEnum(s.kind, int(SymbolKind::Count), SymbolKind::Mountain, F_MAP, where, "kind");
    ch |= fixPoint(s.p, true, F_MAP, where, "p");
    ch |= fixNum(s.s, kMinSymbolScale, kMaxSymbolScale, 1.f, F_MAP, where, "s");
    const u8 vmax = s.kind == SymbolKind::Mountain || s.kind == SymbolKind::Peak ? 1 : 0;
    ch |= fixNum(s.v, u8(0), vmax, u8(0), F_MAP, where, "v");
    ch |= fixFinite(s.z, F_MAP, where, "z");
    if (ch) tx.symbol(s.id) = s;
  });
  base.shapes.each([&](const MapShape& s0) {
    const std::string where = "shapes." + std::to_string(s0.id);
    MapShape s = s0;
    bool ch = fixEnum(s.kind, int(ShapeKind::Count), ShapeKind::Water, F_MAP, where, "kind");
    ch |= fixPoints(s.pts, F_MAP, where, "pts");
    const size_t need = s.closed() ? 3 : 2;
    if (s.pts.size() < need) {
      warn(F_MAP, where, std::string(s.closed() ? "контур" : "линия") + " короче " + std::to_string(need) + " точек — фигура удалена");
      tx.eraseShape(s.id);
      return;
    }
    if (!s.holes.empty() && s.kind != ShapeKind::Water) {
      warn(F_MAP, where + ".holes", "острова бывают только у воды — удалены");
      s.holes.clear();
      ch = true;
    }
    for (size_t i = 0; i < s.holes.size();) {
      const std::string hf = "holes[" + std::to_string(i) + "]", hw = where + "." + hf;
      ch |= fixPoints(s.holes[i], F_MAP, where, hf.c_str());
      if (s.holes[i].size() < 3) {
        warn(F_MAP, hw, "контур острова короче 3 точек — удалён");
        s.holes.erase(s.holes.begin() + long(i));
        ch = true;
      } else {
        i++;
      }
    }
    if (s.closed()) {
      if (s.w != 0 || s.dash != 0) {
        s.w = s.dash = 0;
        ch = true;
      }
    } else {
      const float def = s.kind == ShapeKind::River ? schema::kRiverWidth : schema::kWallWidth;
      if (!(s.w > 0)) {
        if (s.w != 0 || std::isnan(s.w)) warn(F_MAP, where + ".w", "ширина " + ns(s.w) + " — взято " + ns(def));
        s.w = def;
        ch = true;
      }
      ch |= fixNum(s.w, kMinShapeWidth, kMaxShapeWidth, def, F_MAP, where, "w");
      if (s.kind == ShapeKind::River && s.dash != 0) {
        s.dash = 0;
        ch = true;
      }
      ch |= fixNum(s.dash, 0.f, kMaxShapeWidth, 0.f, F_MAP, where, "dash");
    }
    if (ch) tx.shape(s.id) = std::move(s);
  });
  if (!w().meta->mapObjects && (!w().symbols.empty() || !w().shapes.empty())) {
    warn(F_WORLD, "meta.mapObjects", "в data/map.json есть объекты карты — мир показывает их (mapObjects = true)");
    tx.meta().mapObjects = true;
  }
}

}  // namespace rg::io::norm
