// Regnum — сцена карты мира (см. art_scene.h).
#include "map/art_scene.h"

#include <algorithm>

#include "core/schema.h"
#include "geo/geom.h"
#include "geo/topo.h"

namespace rg::map::art {

namespace {

// Кольцо с заданной ориентацией: sign > 0 — против часовой (математически), иначе по часовой.
Ring oriented(const std::vector<Vec2>& r, int sign) {
  Ring out = r;
  if ((geo::signedArea(out) > 0) != (sign > 0)) std::reverse(out.begin(), out.end());
  return out;
}

// Расстояние от точки до ломаной (closed — с замыкающим звеном).
double distToLine(Vec2 p, const std::vector<Vec2>& pts, bool closed) {
  double best = kInf;
  const size_t n = pts.size();
  if (n == 1) return dist(p, pts[0]);
  const size_t ns = closed ? n : n - 1;
  for (size_t i = 0; i < ns; i++) best = std::min(best, geo::distToSeg(p, pts[i], pts[(i + 1) % n]));
  return best;
}

}  // namespace

Objects baseObjects(const MapArt& a) {
  Objects o;
  o.symbols.reserve(a.symbols.size());
  for (size_t i = 0; i < a.symbols.size(); i++) {
    const Symbol& s = a.symbols[i];
    MapSymbol m;
    m.id = Id(i + 1);
    m.kind = SymbolKind(int(s.kind));
    m.p = Vec2(s.x, s.y);
    m.s = s.s;
    m.v = s.v;
    m.z = double(i);
    o.symbols.push_back(m);
  }
  Id sid = 0;
  // Вода: глубина вложенности каждого кольца и ближайшее охватывающее кольцо.
  const size_t n = a.water.size();
  std::vector<int> depth(n, 0), parent(n, -1);
  std::vector<Box2> box(n);
  std::vector<double> area(n);
  for (size_t i = 0; i < n; i++) {
    box[i] = geo::bounds(a.water[i]);
    area[i] = std::fabs(geo::signedArea(a.water[i]));
  }
  for (size_t i = 0; i < n; i++) {
    if (a.water[i].empty()) continue;
    for (size_t j = 0; j < n; j++) {
      const Box2 &bi = box[i], &bj = box[j];
      const bool inBox = bi.x0 >= bj.x0 && bi.y0 >= bj.y0 && bi.x1 <= bj.x1 && bi.y1 <= bj.y1;
      if (i == j || !inBox || !geo::pointInRing(a.water[i][0], a.water[j])) continue;
      depth[i]++;
      if (parent[i] < 0 || area[j] < area[size_t(parent[i])]) parent[i] = int(j);
    }
  }
  for (size_t i = 0; i < n; i++) {
    if (depth[i] % 2 != 0 || a.water[i].size() < 3) continue;
    MapShape s;
    s.id = ++sid;
    s.kind = ShapeKind::Water;
    s.pts = a.water[i];
    for (size_t k = 0; k < n; k++)
      if (parent[k] == int(i) && depth[k] == depth[i] + 1 && a.water[k].size() >= 3) s.holes.push_back(a.water[k]);
    o.shapes.push_back(std::move(s));
  }
  for (const Ring& r : a.islets) {
    if (r.size() < 3) continue;
    MapShape s;
    s.id = ++sid;
    s.kind = ShapeKind::Islet;
    s.pts = r;
    o.shapes.push_back(std::move(s));
  }
  for (const Line& l : a.lines) {
    if (l.pts.size() < 2) continue;
    MapShape s;
    s.id = ++sid;
    s.kind = ShapeKind::Wall;
    s.pts = l.pts;
    s.w = l.w;
    s.dash = l.dash;
    o.shapes.push_back(std::move(s));
  }
  for (const River& r : a.rivers) {
    if (r.pts.size() < 2) continue;
    MapShape s;
    s.id = ++sid;
    s.kind = ShapeKind::River;
    s.pts = r.pts;
    float w = 0;
    for (float x : r.w) w += x;
    s.w = r.w.empty() ? 4.f : w / float(r.w.size());
    o.shapes.push_back(std::move(s));
  }
  return o;
}

Box2 symbolBox(const MapSymbol& s) { return symbolBox(symOf(s.kind), s.p.x, s.p.y, s.s); }

Box2 shapeBox(const MapShape& s) {
  Box2 b = geo::bounds(s.pts);
  for (const auto& h : s.holes) b.add(geo::bounds(h));
  return s.closed() ? b : b.inflated(std::max(0.f, s.w) * 0.5);
}

std::shared_ptr<const Scene> Scene::build(const World& w, const MapArt* base, const Objects* baseObj,
                                          std::shared_ptr<const std::vector<Ring>> land) {
  std::shared_ptr<Scene> sc(new Scene());
  MapArt& a = sc->art_;
  if (base) {
    a.id = base->id;
    a.width = base->width;
    a.height = base->height;
    a.style = base->style;
  } else {
    a.width = int(schema::kMapWidth);
    a.height = int(schema::kMapHeight);
  }
  // Суша: граф мира, без графа — береговая линия базовой карты.
  const bool geom = geo::hasGeometry(w);
  if (!land) land = std::make_shared<const std::vector<Ring>>(geom ? geo::landRings(w) : base ? base->land : std::vector<Ring>{});
  sc->land_ = land;
  sc->hasLand_ = geom || base != nullptr;
  a.land = *land;
  // Знаки и фигуры: мира или базовой карты.
  sc->fromWorld_ = w.ownMapObjects();
  std::vector<const MapSymbol*> syms;
  std::vector<const MapShape*> shapes;
  if (sc->fromWorld_) {
    syms.reserve(w.symbols.size());
    w.symbols.each([&](const MapSymbol& s) { syms.push_back(&s); });
    w.shapes.each([&](const MapShape& s) { shapes.push_back(&s); });
  } else if (baseObj) {
    syms.reserve(baseObj->symbols.size());
    for (const MapSymbol& s : baseObj->symbols) syms.push_back(&s);
    for (const MapShape& s : baseObj->shapes) shapes.push_back(&s);
  }
  std::stable_sort(syms.begin(), syms.end(), [](const MapSymbol* x, const MapSymbol* y) { return x->z != y->z ? x->z < y->z : x->id < y->id; });
  a.symbols.reserve(syms.size());
  sc->symbolIds_.reserve(syms.size());
  for (const MapSymbol* s : syms) {
    a.symbols.push_back(Symbol{symOf(s->kind), float(s->p.x), float(s->p.y), s->s, s->v});
    sc->symbolIds_.push_back(s->id);
  }
  for (const MapShape* s : shapes) {
    switch (s->kind) {
      case ShapeKind::Water:
        if (s->pts.size() < 3) break;
        a.water.push_back(oriented(s->pts, 1));
        sc->waterIds_.push_back(s->id);
        for (const auto& h : s->holes) {
          if (h.size() < 3) continue;
          a.water.push_back(oriented(h, -1));
          sc->waterIds_.push_back(s->id);
        }
        break;
      case ShapeKind::Islet:
        if (s->pts.size() < 3) break;
        a.islets.push_back(s->pts);
        sc->isletIds_.push_back(s->id);
        break;
      case ShapeKind::Wall:
        if (s->pts.size() < 2) break;
        a.lines.push_back(Line{s->pts, s->w, s->dash});
        sc->lineIds_.push_back(s->id);
        break;
      case ShapeKind::River:
        if (s->pts.size() < 2) break;
        a.rivers.push_back(River{s->pts, std::vector<float>(s->pts.size(), s->w)});
        sc->riverIds_.push_back(s->id);
        break;
      default: break;
    }
  }
  sc->index_.build(a);
  return sc;
}

Id Scene::symbolAt(Vec2 p, double tol) const {
  std::vector<u32> ids;
  index_.symbols(Box2(p.x - tol, p.y - tol, p.x + tol, p.y + tol), ids);
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {   // верхний — нарисованный последним
    const Symbol& s = art_.symbols[*it];
    if (symbolBox(s.kind, s.x, s.y, s.s).inflated(tol).contains(p)) return symbolIds_[*it];
  }
  return 0;
}

Id Scene::shapeAt(Vec2 p, double tol) const {
  const Box2 q(p.x - tol, p.y - tol, p.x + tol, p.y + tol);
  std::vector<u32> ids;
  // Стены и реки — по линии (поверх воды).
  index_.lines(q, ids);
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
    const Line& l = art_.lines[*it];
    if (distToLine(p, l.pts, false) <= l.w * 0.5 + tol) return lineIds_[*it];
  }
  index_.rivers(q, ids);
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
    const River& r = art_.rivers[*it];
    float w = 0;
    for (float x : r.w) w = std::max(w, x);
    if (distToLine(p, r.pts, false) <= w * 0.5 + tol) return riverIds_[*it];
  }
  // Островки — по площади и краю.
  index_.land(q, ids);
  const size_t nl = art_.land.size();
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
    if (*it < nl) continue;
    const Ring& r = art_.islets[*it - nl];
    if (geo::pointInRing(p, r) || distToLine(p, r, true) <= tol) return isletIds_[*it - nl];
  }
  // Вода: внутри внешнего контура и не на острове (правило NonZero по кольцам фигуры), или у края.
  index_.water(q, ids);
  for (auto it = ids.rbegin(); it != ids.rend(); ++it)
    if (distToLine(p, art_.water[*it], true) <= tol) return waterIds_[*it];
  index_.water(Box2(p.x, p.y, p.x, p.y), ids);
  std::vector<std::pair<Id, int>> wind;
  for (u32 i : ids) {
    if (!geo::pointInRing(p, art_.water[i])) continue;
    const int d = geo::signedArea(art_.water[i]) > 0 ? 1 : -1;
    auto f = std::find_if(wind.begin(), wind.end(), [&](const auto& x) { return x.first == waterIds_[i]; });
    if (f == wind.end()) wind.push_back({waterIds_[i], d});
    else f->second += d;
  }
  for (auto it = wind.rbegin(); it != wind.rend(); ++it)
    if (it->second != 0) return it->first;
  return 0;
}

gfx::Image renderScene(const Scene& s, int width, Palette pal) {
  const MapArt& a = s.art();
  const double ds = double(width) / std::max(1, a.width);
  const int h = std::max(1, int(std::lround(a.height * ds)));
  gfx::Image img(width, h, gfx::premul(paletteStyle(a.style, pal).land));
  const Xf P{ds, 0, 0};
  if (s.hasLand()) drawSea(img, s.index(), P, pal);
  drawWater(img, s.index(), P, pal);
  drawSymbols(img, s.index(), P, pal);
  return img;
}

std::vector<Id> Scene::symbolsIn(const Box2& b) const {
  std::vector<u32> ids;
  index_.symbols(b, ids);
  std::vector<Id> out;
  for (u32 i : ids) {
    const Symbol& s = art_.symbols[i];
    if (b.contains(Vec2(s.x, s.y))) out.push_back(symbolIds_[i]);
  }
  return out;
}

}  // namespace rg::map::art
