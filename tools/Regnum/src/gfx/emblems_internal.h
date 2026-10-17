// Regnum — внутреннее устройство каталога эмблем (только gfx/emblems*.cpp): общие помощники построения силуэтов
// на сетке 100 × 100 и каталог (группы, подписи). Части каталога по файлам: классические — emblems.cpp, фантазийные
// группы — emblems_fantasy_a.cpp и emblems_fantasy_b.cpp, эмблемы рас — emblems_races_a/b/c.cpp; реестр —
// emblems_registry.cpp.
#pragma once
#include <cmath>
#include <string>
#include <vector>

#include "gfx/emblems.h"
#include "gfx/glyph_builder.h"
#include "gfx/svgpath.h"

namespace rg::gfx::emb {

using namespace shapes;
using G = GlyphBuilder;
using L = GlyphBuilder::L;

// Путь после аффинного преобразования.
inline std::string xf(std::string_view d, const Affine& m) {
  Path p = svgPath(d);
  p.transform(m);
  return toSvgPath(p, 3);
}
inline Affine rotAt(float deg, float cx, float cy) {
  return Affine::translate(cx, cy) * Affine::rotate(deg * float(kPi) / 180.f) * Affine::translate(-cx, -cy);
}
inline Affine scaleAt(float s, float cx, float cy) { return Affine::translate(cx, cy) * Affine::scale(s) * Affine::translate(-cx, -cy); }
// Локальные координаты с началом в (x, y), повёрнутые на deg градусов.
inline Affine place(float x, float y, float deg = 0, float s = 1) {
  return Affine::translate(x, y) * Affine::rotate(deg * float(kPi) / 180.f) * Affine::scale(s);
}
// Путь и его отражение относительно x = 50.
inline std::string sym(const std::string& d) { return d + mirrorX(d, 50); }

inline Pt polar(double cx, double cy, double r, double deg) {
  const double t = deg * kPi / 180.0;
  return {float(cx + r * std::cos(t)), float(cy + r * std::sin(t))};
}
inline std::string ptS(Pt p) { return n(p.x) + " " + n(p.y); }
inline std::string pt(double cx, double cy, double r, double deg) { return ptS(polar(cx, cy, r, deg)); }

// Точка кубической кривой и единичная нормаль к ней при параметре t.
struct CurvePt {
  double x, y, nx, ny;
};
inline CurvePt bezier(Pt p0, Pt c1, Pt c2, Pt p1, double t) {
  const double u = 1 - t;
  const double x = u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p1.x;
  const double y = u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p1.y;
  double dx = 3 * u * u * (c1.x - p0.x) + 6 * u * t * (c2.x - c1.x) + 3 * t * t * (p1.x - c2.x);
  double dy = 3 * u * u * (c1.y - p0.y) + 6 * u * t * (c2.y - c1.y) + 3 * t * t * (p1.y - c2.y);
  if (std::hypot(dx, dy) < 1e-6) {
    dx = p1.x - p0.x;
    dy = p1.y - p0.y;
  }
  const double len = std::max(1e-6, std::hypot(dx, dy));
  return {x, y, -dy / len, dx / len};
}

// Сужающаяся полоса вдоль кубической кривой (хвосты, рога): ширина от w0 до w1.
inline std::string taper(Pt p0, Pt c1, Pt c2, Pt p1, double w0, double w1, int steps = 28) {
  std::vector<Pt> lft, rgt;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, w = (w0 + (w1 - w0) * t) * 0.5;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    lft.push_back({float(q.x + q.nx * w), float(q.y + q.ny * w)});
    rgt.push_back({float(q.x - q.nx * w), float(q.y - q.ny * w)});
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Перо: вытянутый лист от основания b к скруглённо-заострённому кончику t, ширина w.
inline std::string feather(Pt b, Pt t, double w) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::max(1e-6, std::hypot(dx, dy)), nx = -dy / len, ny = dx / len;
  auto p = [&](double along, double side) { return ptS({float(b.x + dx * along + nx * side), float(b.y + dy * along + ny * side)}); };
  return "M" + ptS(b) + "C" + p(0.3, w * 0.66) + " " + p(0.85, w * 0.5) + " " + ptS(t) + "C" + p(0.85, -w * 0.5) + " " + p(0.3, -w * 0.66) +
         " " + ptS(b) + "Z";
}

// Крыло из перьев внахлёст (от нижнего к верхнему): каждое перо отделено зазором от лежащих под ним.
inline void layeredFeathers(std::vector<L>& v, const std::vector<std::string>& feathers, float gap, bool mirror) {
  for (const std::string& f : feathers) {
    const std::string d = mirror ? sym(f) : f;
    v.push_back(G::XS(d, gap));
    v.push_back(G::F(d));
  }
}

// Сужающаяся полоса по дуге окружности (от a0 до a1 градусов).
inline std::string taperArc(double cx, double cy, double r, double a0, double a1, double w0, double w1, int steps = 24) {
  std::vector<Pt> in, ou;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, a = a0 + (a1 - a0) * t, w = (w0 + (w1 - w0) * t) * 0.5;
    ou.push_back(polar(cx, cy, r + w, a));
    in.push_back(polar(cx, cy, r - w, a));
  }
  std::string s = "M" + ptS(ou[0]);
  for (size_t i = 1; i < ou.size(); i++) s += "L" + ptS(ou[i]);
  for (size_t i = in.size(); i-- > 0;) s += "L" + ptS(in[i]);
  return s + "Z";
}

// Слои, сдвинутые преобразованием.
inline std::vector<L> moved(std::vector<L> v, const Affine& m) {
  for (L& l : v) l = G::T(l, m);
  return v;
}

// Две одинаковые фигуры крест-накрест (как скрещённые мечи): задняя, зазор вокруг передней, передняя.
// fill — силуэт остриём вверх, holes — вырезы заливкой, cuts — вырезы линией толщины cutW.
inline std::vector<L> crossed(const std::string& fill, const std::string& holes, const std::string& cuts, float cutW, float deg, float cx,
                       float cy, float s, float gap = 5) {
  const Affine k = scaleAt(s, cx, cy);
  const Affine back = rotAt(-deg, cx, cy) * k, front = rotAt(deg, cx, cy) * k;
  const std::string f = xf(fill, front);
  std::vector<L> v{G::F(xf(fill, back))};
  if (!holes.empty()) v.push_back(G::X(xf(holes, back)));
  if (!cuts.empty()) v.push_back(G::XS(xf(cuts, back), cutW));
  v.push_back(G::XS(f, gap));
  v.push_back(G::X(f));
  v.push_back(G::F(f));
  if (!holes.empty()) v.push_back(G::X(xf(holes, front)));
  if (!cuts.empty()) v.push_back(G::XS(xf(cuts, front), cutW));
  return v;
}

// Каталог: группа (section — раздел выбора: темы или расы), эмблемы с подписями.
struct Catalog {
  GlyphBuilder& b;
  std::vector<EmblemGroup>& groups;
  std::vector<std::pair<const char*, const char*>>& titles;
  void group(std::string_view title, EmblemSection section = EmblemSection::Themes) { groups.push_back({title, {}, section}); }
  void add(const char* name, const char* title, const std::vector<L>& layers) {
    b.addList(name, layers);
    groups.back().names.push_back(name);
    titles.push_back({name, title});
  }
};

// Части каталога в порядке показа.
void classicEmblems(Catalog& c);      // emblems.cpp: «Классические»
void fantasyEmblemsA(Catalog& c);     // emblems_fantasy_a.cpp: драконы, вампиры, природа, стихии, гномы, эльфы, некроманты, тёмные эльфы, наги
void fantasyEmblemsB(Catalog& c);     // emblems_fantasy_b.cpp: рыцари, святые, демоны, культисты, магия, орки, ящеры, зверолюды
void raceEmblemsA(Catalog& c);        // emblems_races_a.cpp: расы «Ангелы» … «Кентавры»
void raceEmblemsB(Catalog& c);        // emblems_races_b.cpp: расы «Кецали» … «Наги»
void raceEmblemsC(Catalog& c);        // emblems_races_c.cpp: расы «Огры» … «Энты»

}  // namespace rg::gfx::emb
