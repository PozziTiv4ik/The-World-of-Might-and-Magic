// Regnum — помощники тестов geo (и правил, которые от geo зависят): синтетические берега, проверка инвариантов,
// запросы к граням, граф вручную, отладочные PNG.
//
//   Store s = geotest::initStore();                       // мир на синтетическом береге sampleCoast(), без истории
//   geotest::provArea(w, p), terrainArea(w, Terrain::Land), allTerrain(w, p, Terrain::Sea), countKind(w, EdgeKind::Coast)
//   geotest::errorOf(s, [&](Tx& tx) { ... })              // текст отказа; отказ не меняет мир
//   geotest::addRing(tx, circle({50, 50}, 20, 12), EdgeKind::Coast, {0, Terrain::Land}, {0, Terrain::Sea})
#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/world.h"
#include "geo/geom.h"
#include "geo/ops.h"
#include "geo/topo.h"
#include "tests/test.h"
#include "tests/test_png.h"

namespace rg::geotest {

// Звёздное кольцо: r(θ) = R·(1 + Σ aₖ·sin(kθ + φₖ)), простое при Σ|aₖ| < 1.
inline std::vector<Vec2> starRing(Vec2 c, double R, int n, Rng& rng, double rough = 0.25) {
  double a[4], ph[4];
  double s = 0;
  for (int k = 0; k < 4; k++) {
    a[k] = rng.uniform();
    ph[k] = rng.uniform() * 2 * kPi;
    s += a[k];
  }
  for (int k = 0; k < 4; k++) a[k] *= rough / s;
  std::vector<Vec2> r;
  for (int i = 0; i < n; i++) {
    double t = 2 * kPi * i / n;
    double rr = 1;
    for (int k = 0; k < 4; k++) rr += a[k] * std::sin((k + 2) * t + ph[k]);
    r.push_back({c.x + R * rr * std::cos(t), c.y + R * rr * std::sin(t)});
  }
  return r;
}

// Остров-подкова («лагуна»): сектор кольца от a0 до a1 (радианы), открытый залив.
inline std::vector<Vec2> horseshoe(Vec2 c, double r0, double r1, double a0, double a1, int n) {
  std::vector<Vec2> r;
  for (int i = 0; i <= n; i++) {
    double t = a0 + (a1 - a0) * i / n;
    r.push_back({c.x + r1 * std::cos(t), c.y + r1 * std::sin(t)});
  }
  for (int i = n; i >= 0; i--) {
    double t = a0 + (a1 - a0) * i / n;
    r.push_back({c.x + r0 * std::cos(t), c.y + r0 * std::sin(t)});
  }
  return r;
}

// Сцена: большой остров, малые острова, подкова, материк, обрезанный левым краем карты.
inline geo::Coast sampleCoast(double W = 1000, double H = 600) {
  geo::Coast c;
  c.width = W;
  c.height = H;
  Rng rng(42);
  c.landRings.push_back(starRing({480, 300}, 170, 360, rng, 0.3));            // большой остров
  c.landRings.push_back(starRing({820, 140}, 40, 60, rng, 0.3));              // малые острова
  c.landRings.push_back(starRing({860, 470}, 35, 50, rng, 0.3));
  c.landRings.push_back(starRing({720, 520}, 18, 30, rng, 0.2));
  c.landRings.push_back(horseshoe({820, 300}, 40, 75, 0.9, 2 * kPi - 0.9, 40));  // подкова с заливом
  // материк за левым краем: часть кольца вне карты обрезается рамкой
  std::vector<Vec2> m;
  for (int i = 0; i <= 80; i++) {
    double t = -kPi / 2 + kPi * i / 80;
    m.push_back({-60 + 180 * std::cos(t) + 8 * std::sin(7 * t), 300 + 260 * std::sin(t)});
  }
  c.landRings.push_back(m);
  return c;
}

inline double faceAreaSum(const geo::FaceSet& fs) {
  double s = 0;
  for (auto& f : fs.faces) s += f.area;
  return s;
}

inline std::string issuesText(const std::vector<geo::Issue>& is, size_t max = 5) {
  std::string s;
  for (size_t i = 0; i < is.size() && i < max; i++)
    s += is[i].code + ": " + is[i].msg + " @(" + std::to_string(is[i].at.x) + "," + std::to_string(is[i].at.y) + "); ";
  return s;
}

// Граф корректен и грани покрывают рамку W × H.
#define GEO_CHECK_WORLD(w, W, H)                                                                  \
  do {                                                                                             \
    auto _is = ::rg::geo::validate(w);                                                             \
    CHECK_MSG(_is.empty(), ::rg::geotest::issuesText(_is));                                       \
    auto _fs = ::rg::geo::buildFaces(w);                                                           \
    CHECK_NEAR(::rg::geotest::faceAreaSum(*_fs), double(W) * double(H), 1e-6 * double(W) * double(H)); \
  } while (0)

// ---------------------------------------------------------------- контуры
// Прямоугольник [x0, x1] × [y0, y1].
inline std::vector<Vec2> rect(double x0, double y0, double x1, double y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }

// Правильный n-угольник, вписанный в окружность (c, r).
inline std::vector<Vec2> circle(Vec2 c, double r, int n = 40) {
  std::vector<Vec2> v;
  for (int i = 0; i < n; i++) v.push_back({c.x + r * std::cos(2 * kPi * i / n), c.y + r * std::sin(2 * kPi * i / n)});
  return v;
}

// ---------------------------------------------------------------- мир и запросы
// Хранилище с миром на синтетическом береге sampleCoast(W, H), история отмены пуста.
inline Store initStore(double W = 1000, double H = 600) {
  Store s;
  s.transact("init", [&](Tx& tx) { geo::initFromCoast(tx, sampleCoast(W, H)); });
  s.clearHistory();
  return s;
}

// Площадь области провинции (0 — области нет).
inline double provArea(const World& w, Id p) {
  auto fs = geo::buildFaces(w);
  auto* s = fs->shape(p);
  return s ? s->area : 0;
}

// Число граней провинции.
inline size_t provFaces(const World& w, Id p) {
  auto fs = geo::buildFaces(w);
  auto* s = fs->shape(p);
  return s ? s->faces.size() : 0;
}

// Площадь граней рельефа t: всех или только провинции prov.
inline double terrainArea(const World& w, Terrain t, Id prov = Id(-1)) {
  auto fs = geo::buildFaces(w);   // держать указатель: грани живут, пока жив FaceSet
  double a = 0;
  for (auto& f : fs->faces)
    if (f.terrain == t && (prov == Id(-1) || f.province == prov)) a += f.area;
  return a;
}

// Все грани провинции имеют рельеф t.
inline bool allTerrain(const World& w, Id p, Terrain t) {
  auto fs = geo::buildFaces(w);
  auto* s = fs->shape(p);
  if (!s) return false;
  for (int f : s->faces)
    if (fs->faces[size_t(f)].terrain != t) return false;
  return true;
}

// Площадь грани, содержащей точку (0 — точка вне граней).
inline double faceAreaAt(const World& w, Vec2 p) {
  auto fs = geo::buildFaces(w);
  int f = fs->locate(p);
  return f < 0 ? 0 : fs->faces[size_t(f)].area;
}

// Число дуг вида k.
inline int countKind(const World& w, EdgeKind k) {
  int n = 0;
  w.edges.each([&](const Edge& e) { n += e.kind == k; });
  return n;
}

// Среди замечаний проверки графа есть замечание с кодом code.
inline bool hasCode(const std::vector<geo::Issue>& is, const std::string& code) {
  for (auto& i : is)
    if (i.code == code) return true;
  return false;
}

inline bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

// Сообщение отказа операции над s (пусто — операция прошла). Отказ не меняет мир.
template <class F> std::string errorOf(Store& s, F&& fn) {
  World before = s.world();
  try {
    s.transact("probe", fn);
  } catch (const UserError& e) {
    CHECK_EQ(World::diff(before, s.world()), 0u);
    return e.what();
  }
  return {};
}

// ---------------------------------------------------------------- граф вручную
// Сторона дуги: провинция и рельеф.
struct Lbl { Id p = 0; Terrain t = Terrain::Land; };

inline Id addNode(Tx& tx, Vec2 p) { return tx.add(Node{0, p}).id; }

inline Id addEdge(Tx& tx, Id a, Id b, std::vector<Vec2> pts, EdgeKind k, Lbl l, Lbl r) {
  Edge e;
  e.a = a;
  e.b = b;
  e.pts = std::move(pts);
  e.kind = k;
  e.pl = l.p;
  e.tl = l.t;
  e.pr = r.p;
  e.tr = r.t;
  return tx.add(e).id;
}

// Кольцо против часовой (математически) из двух дуг: слева — inside, справа — outside.
inline void addRing(Tx& tx, const std::vector<Vec2>& ring, EdgeKind k, Lbl inside, Lbl outside) {
  size_t h = ring.size() / 2;
  Id n0 = addNode(tx, ring[0]), n1 = addNode(tx, ring[h]);
  std::vector<Vec2> p1(ring.begin() + 1, ring.begin() + long(h)), p2(ring.begin() + long(h) + 1, ring.end());
  addEdge(tx, n0, n1, p1, k, inside, outside);
  addEdge(tx, n1, n0, p2, k, inside, outside);
}

// ---------------------------------------------------------------- отладочный растр
struct Raster {
  int w = 0, h = 0;
  double scale = 1;
  Vec2 org;  // точка карты в левом верхнем углу
  std::vector<u8> rgba;
  Raster(int W, int H, double s, Vec2 o = {}) : w(W), h(H), scale(s), org(o), rgba(size_t(W) * size_t(H) * 4, 255) {}
  void px(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    u8* p = &rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255;
  }
  // Заливка многоугольника с дырами (чёт-нечет), выборка в центрах пикселей.
  void fill(const std::vector<std::vector<Vec2>>& rings, Color c) {
    Box2 b;
    for (auto& r : rings) for (auto& p : r) b.add(p);
    int y0 = std::max(0, int(std::floor((b.y0 - org.y) * scale))), y1 = std::min(h - 1, int(std::ceil((b.y1 - org.y) * scale)));
    std::vector<double> xs;
    for (int y = y0; y <= y1; y++) {
      double fy = (y + 0.5) / scale + org.y;
      xs.clear();
      for (auto& r : rings) {
        size_t n = r.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
          Vec2 a = r[j], q = r[i];
          if ((a.y > fy) != (q.y > fy)) xs.push_back(a.x + (fy - a.y) * (q.x - a.x) / (q.y - a.y) - org.x);
        }
      }
      std::sort(xs.begin(), xs.end());
      for (size_t k = 0; k + 1 < xs.size(); k += 2) {
        int xa = std::max(0, int(std::ceil(xs[k] * scale - 0.5))), xb = std::min(w - 1, int(std::floor(xs[k + 1] * scale - 0.5)));
        for (int x = xa; x <= xb; x++) px(x, y, c);
      }
    }
  }
  void line(Vec2 a, Vec2 b, Color c) {
    double x0 = (a.x - org.x) * scale, y0 = (a.y - org.y) * scale, x1 = (b.x - org.x) * scale, y1 = (b.y - org.y) * scale;
    if (std::max(x0, x1) < -1 || std::min(x0, x1) > w + 1 || std::max(y0, y1) < -1 || std::min(y0, y1) > h + 1) return;
    int n = int(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0))) + 1;
    for (int i = 0; i <= n; i++) {
      double t = double(i) / n;
      px(int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t), c);
    }
  }
  void dot(Vec2 p, int r, Color c) {
    int cx = int((p.x - org.x) * scale), cy = int((p.y - org.y) * scale);
    for (int y = -r; y <= r; y++)
      for (int x = -r; x <= r; x++) px(cx + x, cy + y, c);
  }
  bool save(const std::string& name) const {
    codec::RgbaImage img;
    img.w = w;
    img.h = h;
    img.rgba = rgba;
    return test::savePng(name, img);
  }
};

inline Color provinceColor(Id p, Terrain t) {
  if (p == 0) return t == Terrain::Sea ? Color::hex(0xbcd4e6) : Color::hex(0xf2efe6);
  Color c = Color::palette(int(p));
  return t == Terrain::Sea ? Color::mix(c, Color::hex(0x2f6fa8), 0.55f) : c;
}

// Карта провинций: заливка граней, границы (берег — синий, рамка — серая, граница — чёрная), узлы, подписи.
// frame — показываемая область карты (пусто — вся), extra — дополнительная ломаная (красным).
inline void renderWorld(const World& w, const std::string& name, double scale = 1.0, Box2 frame = {},
                        const std::vector<Vec2>& extra = {}) {
  auto fs = geo::buildFaces(w);
  Box2 b = frame.empty() ? fs->bounds : frame;
  Raster r(std::max(1, int(std::ceil(b.w() * scale)) + 1), std::max(1, int(std::ceil(b.h() * scale)) + 1), scale, {b.x0, b.y0});
  for (auto& f : fs->faces) r.fill(f.rings, provinceColor(f.province, f.terrain));
  w.edges.each([&](const Edge& e) {
    auto c = geo::edgeCoords(w, e);
    Color col = e.kind == EdgeKind::Coast ? Color::hex(0x1d4f91) : (e.kind == EdgeKind::Frame ? Color::hex(0x777777) : Color::hex(0x111111));
    for (size_t i = 0; i + 1 < c.size(); i++) r.line(c[i], c[i + 1], col);
  });
  w.nodes.each([&](const Node& n) { r.dot(n.p, 1, Color::hex(0xd02020)); });
  for (auto& f : fs->faces)
    if (f.area > 50) r.dot(f.label, 1, Color::hex(0x10a010));
  for (size_t i = 0; i < extra.size(); i++) r.line(extra[i], extra[(i + 1) % extra.size()], Color::hex(0xff2020));
  r.save(name);
}

}  // namespace rg::geotest
