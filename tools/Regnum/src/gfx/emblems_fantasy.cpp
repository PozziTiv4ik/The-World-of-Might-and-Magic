// Regnum — фантазийные эмблемы каталога по группам (драконы, вампиры, природа, стихии, гномы, эльфы, некроманты,
// тёмные эльфы, наги, рыцари, святые, демоны, культисты, магия, орки, ящеры, зверолюды). Сетка 100 × 100, стиль
// классических эмблем: одноцветный силуэт, детали — вырезы. Симметричные силуэты описаны левой половиной.
#include <cmath>

#include "gfx/emblems.h"
#include "gfx/icons_shapes.h"
#include "gfx/svgpath.h"

namespace rg::gfx {

namespace {

using namespace shapes;
using G = GlyphBuilder;
using L = GlyphBuilder::L;

// Путь после аффинного преобразования.
std::string xf(std::string_view d, const Affine& m) {
  Path p = svgPath(d);
  p.transform(m);
  return toSvgPath(p, 3);
}
Affine rotAt(float deg, float cx, float cy) {
  return Affine::translate(cx, cy) * Affine::rotate(deg * float(kPi) / 180.f) * Affine::translate(-cx, -cy);
}
Affine scaleAt(float s, float cx, float cy) { return Affine::translate(cx, cy) * Affine::scale(s) * Affine::translate(-cx, -cy); }
// Локальные координаты с началом в (x, y), повёрнутые на deg градусов.
Affine place(float x, float y, float deg = 0, float s = 1) {
  return Affine::translate(x, y) * Affine::rotate(deg * float(kPi) / 180.f) * Affine::scale(s);
}
// Путь и его отражение относительно x = 50.
std::string sym(const std::string& d) { return d + mirrorX(d, 50); }

Pt polar(double cx, double cy, double r, double deg) {
  const double t = deg * kPi / 180.0;
  return {float(cx + r * std::cos(t)), float(cy + r * std::sin(t))};
}
std::string ptS(Pt p) { return n(p.x) + " " + n(p.y); }
std::string pt(double cx, double cy, double r, double deg) { return ptS(polar(cx, cy, r, deg)); }

// Точка кубической кривой и единичная нормаль к ней при параметре t.
struct CurvePt {
  double x, y, nx, ny;
};
CurvePt bezier(Pt p0, Pt c1, Pt c2, Pt p1, double t) {
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
std::string taper(Pt p0, Pt c1, Pt c2, Pt p1, double w0, double w1, int steps = 28) {
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
std::string feather(Pt b, Pt t, double w) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::max(1e-6, std::hypot(dx, dy)), nx = -dy / len, ny = dx / len;
  auto p = [&](double along, double side) { return ptS({float(b.x + dx * along + nx * side), float(b.y + dy * along + ny * side)}); };
  return "M" + ptS(b) + "C" + p(0.3, w * 0.66) + " " + p(0.85, w * 0.5) + " " + ptS(t) + "C" + p(0.85, -w * 0.5) + " " + p(0.3, -w * 0.66) +
         " " + ptS(b) + "Z";
}

// Крыло из перьев внахлёст (от нижнего к верхнему): каждое перо отделено зазором от лежащих под ним.
void layeredFeathers(std::vector<L>& v, const std::vector<std::string>& feathers, float gap, bool mirror) {
  for (const std::string& f : feathers) {
    const std::string d = mirror ? sym(f) : f;
    v.push_back(G::XS(d, gap));
    v.push_back(G::F(d));
  }
}

// Сужающаяся полоса по дуге окружности (от a0 до a1 градусов).
std::string taperArc(double cx, double cy, double r, double a0, double a1, double w0, double w1, int steps = 24) {
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
std::vector<L> moved(std::vector<L> v, const Affine& m) {
  for (L& l : v) l = G::T(l, m);
  return v;
}

// Две одинаковые фигуры крест-накрест (как скрещённые мечи): задняя, зазор вокруг передней, передняя.
// fill — силуэт остриём вверх, holes — вырезы заливкой, cuts — вырезы линией толщины cutW.
std::vector<L> crossed(const std::string& fill, const std::string& holes, const std::string& cuts, float cutW, float deg, float cx,
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

// Каталог: группа, эмблемы с подписями.
struct Catalog {
  GlyphBuilder& b;
  std::vector<EmblemGroup>& groups;
  std::vector<std::pair<const char*, const char*>>& titles;
  void group(std::string_view title) { groups.push_back({title, {}}); }
  void add(const char* name, const char* title, const std::vector<L>& layers) {
    b.addList(name, layers);
    groups.back().names.push_back(name);
    titles.push_back({name, title});
  }
};

// ---------------------------------------------------------------- драконы
void dragons(Catalog& c) {
  c.group("Драконы");
  // Дракон в полёте: голова с рогами, крылья с перепонками, хвост с остриём.
  {
    const std::string wing = "M46 40C40 30 31 18 22 8L19 2 18 9 2 30Q16 38 8 52Q22 54 20 66Q34 56 46 60Z";
    const std::string head = "M50 2C47 2 45 4.5 44.5 8L43.5 13 32 5 41 18.5C42.5 22.5 44 26 45 30L46 38H50.5V2Z";
    const std::string tail = taper({50, 58}, {50, 72}, {64, 74}, {58, 88}, 10, 2.5);
    c.add("dragon-wings", "Дракон в полёте",
          {G::F(sym(wing) + sym(head) + ell(50, 48, 9, 14) + tail + "M50 86 64 88 54 97Z"), G::S(sym("M44 57 37 65 35 72"), 4.5),
           G::XS(sym("M20 13 10 46M21 13 19 60"), 1.5)});
  }
  // Свернувшийся дракон (уроборос): кольцо с шипами, голова кусает хвост.
  {
    const double cx = 50, cy = 53, R = 30;
    std::string spines;
    for (int a = -30; a <= 190; a += 22)
      spines += "M" + pt(cx, cy, R + 4, a - 7) + "L" + pt(cx, cy, R + 13, a) + "L" + pt(cx, cy, R + 4, a + 7) + "Z";
    const std::string head = "M73 32L69 19 84 8 70 13 76 4 63 11C55 9 45 10 35 15L38 17.5 52 21 39 27.5C47 31.5 56 34 64 37Z";
    c.add("dragon-coiled", "Свернувшийся дракон",
          {G::S(arc(cx, cy, R, -62, 200), 12), G::F(taperArc(cx, cy, R, 200, 266, 12, 1.5) + spines), G::XS(head, 3.5), G::F(head),
           G::X(circ(61, 15.5, 1.9))});
  }
  // Глаз дракона: миндалевидный глаз, вертикальный зрачок, бровь с шипами.
  {
    const std::string eye = "M4 58C22 34 58 26 96 42C78 68 42 78 4 58Z";
    const std::string brow = "M6 47C18 30 40 20 63 18L73 5 74 17 88 11 83 23C88 25 92 29 96 33C80 28 60 27 42 31C28 35 16 41 6 47Z";
    c.add("dragon-eye", "Глаз дракона",
          moved({G::F(eye + brow), G::X(circ(51, 51.5, 17)), G::F(circ(51, 51.5, 14)), G::X("M51 39C55 46 55 57 51 64C47 57 47 46 51 39Z"),
                 G::X(circ(57, 45, 2))},
                Affine::translate(0, 12)));
  }
  // Череп дракона: вытянутая морда, косые глазницы, рога.
  {
    const std::string half = "M50 14C41 14 34 19 31 28L24 44 31 52 36 75 41 92H50.5V14Z";
    const std::string horn = "M37 24C29 15 20 9 8 4C13 13 21 23 29 33Z";
    const std::string spike = "M26 46 11 50 28 53Z";
    const std::string fang = "M41 90 43.5 97 46 90Z";
    c.add("dragon-skull", "Череп дракона",
          {G::F(sym(half + horn + spike + fang)),
           G::X(sym("M33 36 46 41 43 47 34 44Z" + ell(45.5, 85, 1.8, 3.2)))});
  }
}

// ---------------------------------------------------------------- вампиры
void vampires(Catalog& c) {
  c.group("Вампиры");
  // Летучая мышь.
  {
    const std::string wing = "M47 42C40 34 32 27 24 24C16 26 9 31 3 38Q11 42 12 52Q19 47 25 57Q31 52 36 64Q40 58 47 59Z";
    const std::string ear = "M44 34 41.5 20 49 31Z";
    c.add("vampire-bat", "Летучая мышь",
          moved({G::F(sym(wing + ear) + circ(50, 38, 8) + ell(50, 52, 7, 13)), G::X(sym("M44.5 37 48.5 38.6 45 40Z"))},
                Affine::translate(0, 6)));
  }
  // Клыки: губы, клыки поверх нижней губы, капли крови.
  {
    const std::string fang = "M29.5 41.5 40.5 42.5 36 74Z";
    const std::string drop = "M36 79C38 83 41 86 41 89.5A5 5 0 0 1 31 89.5C31 86 34 83 36 79Z";
    c.add("vampire-fangs", "Клыки",
          moved({G::F("M6 42C18 30 34 26 50 33C66 26 82 30 94 42C82 60 66 68 50 68C34 68 18 60 6 42Z"),
                 G::X("M14 43C28 39 40 40 50 42C60 40 72 39 86 43C74 51 62 54 50 54C38 54 26 51 14 43Z"),
                 G::XS(sym("M30.8 48 36 74 39.7 48"), 4), G::F(sym(fang)), G::F(sym(drop))},
                Affine::translate(0, -10)));
  }
  // Кубок крови: капля над кубком, потёки у края.
  c.add("vampire-chalice", "Кубок крови",
        {G::F("M50 4C55 12 61 18 61 25A11 11 0 0 1 39 25C39 18 45 12 50 4Z" + std::string("M20 40H80C80 58 68 70 50 72C32 70 20 58 20 40Z") +
              rrect(46.5, 70, 7, 17, 2) + ell(50, 78, 6, 3.5) + "M30 96C32 90 40 86 50 86C60 86 68 90 70 96Z"),
         G::XS("M21 47H31C31 53 37 53 37 47H53C53 56 60 56 60 47H79", 2.6)});
  // Гроб с крестом.
  c.add("vampire-coffin", "Гроб",
        {G::F("M37 4H63L78 26 67 96H33L22 26Z"), G::XS("M38.5 9.5H61.5L72.5 26.5 62.5 90.5H37.5L27.5 26.5Z", 2.4),
         G::X(rrect(46.5, 22, 7, 48, 1.5) + rrect(35, 34, 30, 7, 1.5))});
}

// ---------------------------------------------------------------- природа
void nature(Catalog& c) {
  c.group("Природа");
  // Дубовый лист.
  {
    const std::string lobes = circ(50, 15, 8) + circ(39, 24, 8) + circ(61, 24, 8) + circ(35, 38, 10) + circ(65, 38, 10) + circ(33, 54, 11) +
                              circ(67, 54, 11) + circ(38, 68, 8.5) + circ(62, 68, 8.5) + ell(50, 46, 14, 32) + "M41 72Q50 86 59 72Z";
    const std::string veins = "M50 31 40 25M50 31 60 25M50 45 37 38M50 45 63 38M50 61 35 54M50 61 65 54M50 73 40 68M50 73 60 68";
    c.add("nature-leaf", "Дубовый лист",
          moved({G::F(lobes + "M48.6 78H51.4L53 96H47Z"), G::XS("M50 84V17", 2.4), G::XS(veins, 1.8)}, rotAt(32, 50, 50) * scaleAt(1.04f, 50, 50)));
  }
  // Олень: голова анфас с ветвистыми рогами.
  {
    const std::string antler = "M45 51C40 44 33 39 24 34C17 30 11 24 8 14M36 42C34 35 33 29 34 22M22 33C20 27 19 21 20 14";
    const std::string ear = "M42 54C34 52 26 48 21 44C27 52 33 58 41 60Z";
    c.add("nature-stag", "Олень",
          {G::S(sym(antler), 5.5),
           G::F(sym(ear) + "M50 95C45.5 95 43.5 91 43 86L39 62C38 57 40 52 44 50H56C60 52 62 57 61 62L57 86C56.5 91 54.5 95 50 95Z"),
           G::X(sym(ell(44.5, 64, 2.2, 3) + ell(47.6, 90, 1.4, 2)))});
  }
  // Мухомор.
  c.add("nature-mushroom", "Мухомор",
        {G::F("M8 54C8 28 28 10 50 10C72 10 92 28 92 54C78 60 64 62 50 62C36 62 22 60 8 54Z" +
              std::string("M38 58C38 70 36 82 32 92C44 96 56 96 68 92C64 82 62 70 62 58Z")),
         G::XS("M30 61Q50 67 70 61", 3), G::XS("M37.5 70Q50 74 62.5 70", 2.4),
         G::X(circ(30, 30, 5) + circ(50, 22, 6) + circ(70, 32, 5.5) + circ(40, 44, 4.5) + circ(62, 48, 4) + circ(20, 46, 3.5) + circ(80, 47, 3.5))});
  // Ель.
  {
    auto tier = [](double y0, double y1, double w) {
      return "M50 " + n(y0) + "L" + n(50 + w) + " " + n(y1) + "Q50 " + n(y1 - 7) + " " + n(50 - w) + " " + n(y1) + "Z";
    };
    c.add("nature-pine", "Ель", {G::F(tier(4, 30, 17) + tier(17, 53, 27) + tier(34, 80, 38) + rrect(44, 74, 12, 21, 2))});
  }
}

// ---------------------------------------------------------------- стихии
void elements(Catalog& c) {
  c.group("Стихии");
  // Огонь: огненный шар с языками пламени, внутри — вырез пламени.
  c.add("element-fire", "Огонь",
        moved({G::F(circ(42, 60, 25) + taper({48, 42}, {56, 28}, {68, 16}, {86, 6}, 26, 2) + taper({60, 50}, {70, 40}, {82, 34}, {96, 32}, 22, 2) +
                    taper({32, 42}, {36, 28}, {44, 16}, {56, 6}, 16, 2)),
               G::X(xf("M42 44C46 52 55 57 55 66C55 74 49 79 42 79C35 79 29 74 29 66C29 60 33 56 35 50C37 54 40 55 42 56C41 52 41 48 42 44Z",
                       scaleAt(0.72f, 42, 66)))},
              Affine::translate(-6, 4) * scaleAt(1.05f, 50, 50)));
  // Вода: капля с волнами.
  c.add("element-water", "Вода",
        {G::F("M50 4C60 22 80 40 80 62A30 30 0 0 1 20 62C20 40 40 22 50 4Z"),
         G::XS("M27 62C32 57 38 57 43 62S54 67 59 62S70 57 74 62M30 75C35 70 40 70 45 75S55 80 60 75S69 70 71 74", 3.6),
         G::XS("M34 36C30 42 28 48 28 54", 3.4)});
  // Воздух: вихри ветра.
  c.add("element-air", "Воздух",
        {G::S("M8 33H54C62 33 68 27 68 20C68 13 62 8 55 8C49 8 45 12 45 17M8 51H77C85 51 91 57 91 65C91 73 85 79 77 79C70 79 65 74 65 68"
              "M18 70H44C50 70 54 74 54 80C54 86 49 90 44 90",
              7)});
  // Земля: горы со снежными шапками.
  c.add("element-earth", "Земля",
        {G::F("M2 90L24 44 33 56 50 12 66 46 76 34 98 90Z"),
         G::XS("M40 38 45 33 50 39 55 33 60 38M19.5 55 22 52 25 56 28 52.5 31 55.5M68 45 71 42 74 46 77 42 80 45", 3)});
  // Молния.
  c.add("element-lightning", "Молния", {G::F("M60 3L22 55H45L35 97 80 40H56L70 3Z")});
  // Лёд: снежинка.
  {
    std::string s;
    for (int k = 0; k < 6; k++) {
      const double a = -90 + 60 * k;
      s += "M50 50L" + pt(50, 50, 44, a);
      for (auto [d, len] : {std::pair{21.0, 11.0}, std::pair{32.0, 8.0}}) {
        const Pt q = polar(50, 50, d, a);
        s += "M" + pt(q.x, q.y, len, a - 45) + "L" + ptS(q) + "L" + pt(q.x, q.y, len, a + 45);
      }
    }
    c.add("element-frost", "Лёд", {G::S(s, 5.5)});
  }
}

// ---------------------------------------------------------------- гномы
void dwarves(Catalog& c) {
  c.group("Гномы");
  // Наковальня.
  c.add("dwarf-anvil", "Наковальня",
        moved({G::F("M4 32C14 27 22 26 30 26H94V39C86 40 80 42 74 45C68 48 64 53 63 59L68 72H78V84H26V72H36L39 59C38 52 32 46 24 44"
                    "C16 42 9 38 4 32Z"),
               G::XS("M30 32.5H93.5", 2.2)},
              Affine::translate(0, -5)));
  // Скрещённые кирки.
  c.add("dwarf-pickaxes", "Скрещённые кирки",
        crossed("M10 32C24 19 37 14 50 14C63 14 76 19 90 32C77 25 63 22 50 22C37 22 23 25 10 32Z" + rrect(43, 10, 14, 17, 3) +
                    rrect(46.5, 20, 7, 76, 3),
                "", "", 0, 38, 50, 54, 0.96f));
  // Гном: шлем с рогами, глаза, усы и борода.
  {
    const std::string horn = "M27 30C19 28 13 22 11 12C17 18 23 20 29 22Z";
    c.add("dwarf-face", "Гном",
          {G::F("M24 40C24 20 36 8 50 8C64 8 76 20 76 40Z" + rrect(18, 35, 64, 9, 3) + sym(horn) +
                "M22 46H78L80 62C80 78 70 88 58 93L50 97 42 93C30 88 20 78 20 62Z"),
           G::X(ell(38, 51, 5, 2.6) + ell(62, 51, 5, 2.6) + circ(24, 39.5, 1.6) + circ(76, 39.5, 1.6)), G::XS("M50 11V34", 2.6),
           G::XS("M46 47V56C46 60 48 62 50 62S54 60 54 56V47", 2.2), G::XS("M30 67C38 61 45 62 50 67C55 62 62 61 70 67", 3),
           G::XS("M50 72V93M41 73 38 89M59 73 62 89", 2.4)});
  }
  // Врата в горе.
  c.add("dwarf-gate", "Врата в горе",
        {G::F("M3 92L38 28 45 36 56 10 97 92Z"), G::XS("M50 25 53 22 56 26 59 22 62.5 25", 3),
         G::X("M36 92V70C36 61 42 55 50 55C58 55 64 61 64 70V92Z" + std::string("M50 40 54 46 50 52 46 46Z")),
         G::F("M40.5 92V70C40.5 63.5 44.5 59.5 50 59.5C55.5 59.5 59.5 63.5 59.5 70V92Z"), G::XS("M50 60V92", 2.2)});
}

// ---------------------------------------------------------------- эльфы
void elves(Catalog& c) {
  c.group("Эльфы");
  // Эльфийский лист: длинный лист с завитком стебля.
  {
    const std::string veins = "M50 28 59 20M50 42 62 33M50 56 62 47M50 68 59 61";
    c.add("elf-leaf", "Эльфийский лист",
          moved({G::F("M50 2C64 16 70 33 67 51C64 64 58 72 50 78C42 72 36 64 33 51C30 33 36 16 50 2Z"),
                 G::S("M50 76C50 88 41 95 33 92C27 90 27 83 33 81", 3.4), G::XS("M50 8C51 30 51 54 50 75", 2.2), G::XS(sym(veins), 1.8)},
                Affine::translate(8, 3) * rotAt(32, 50, 50) * scaleAt(1.14f, 50, 50)));
  }
  // Звезда эльфов: восемь лучей, длинные и короткие.
  {
    std::string s;
    for (int i = 0; i < 16; i++) {
      const double r = i % 2 ? 15.5 : (i % 4 == 0 ? 48 : 35);
      s += (i == 0 ? "M" : "L") + pt(50, 50, r, -90 + 22.5 * i);
    }
    s += "Z";
    c.add("elf-star", "Звезда эльфов", {G::F(s), G::XS(circ(50, 50, 9), 2), G::X(circ(50, 50, 3))});
  }
  // Венок: лавровые ветви, звезда сверху.
  {
    std::string leaves;
    for (double a = 108; a <= 234; a += 14) {
      const Pt p = polar(50, 52, 36, a);
      const double tdeg = a + 90;   // касательная по ходу ветви
      for (double side : {-34.0, 34.0}) {
        const double d = tdeg + side;
        const Pt cpt = polar(p.x, p.y, 7, d);
        leaves += xf(ell(0, 0, 3.6, 8.5), place(cpt.x, cpt.y, float(d - 90)));
      }
    }
    c.add("elf-wreath", "Венок",
          {G::S(sym(arc(50, 52, 36, 100, 238)), 2.8), G::F(sym(leaves) + star(50, 12, 9, 2.8, 4, -90) + "M43 86 57 93V86L43 93Z")});
  }
  // Лебедь: поднятое крыло из перьев внахлёст.
  {
    const Pt tips[] = {{92, 54}, {88, 41}, {81, 29}, {71, 19}, {60, 12}};
    std::vector<std::string> feathers;
    for (int k = 0; k < 5; k++) feathers.push_back(feather({66 - 6.f * k, 64 - 0.5f * k}, tips[k], 13));
    feathers.push_back("M34 66C32 50 40 36 52 30C58 42 68 52 82 58C70 64 54 68 34 66Z");
    std::vector<L> v{G::F("M20 66C22 80 36 88 56 88C74 88 88 80 94 64C86 68 76 70 66 68C50 66 34 64 20 66Z" +
                          xf(ell(0, 0, 7, 5.5), place(22, 13, -10)) + "M16 11 4 15 16 17Z"),
                     G::S("M28 68C16 54 16 40 24 30C30 22 30 15 24 12", 8), G::X(circ(23, 12, 1.4))};
    layeredFeathers(v, feathers, 2.2f, false);
    c.add("elf-swan", "Лебедь", v);
  }
}

// ---------------------------------------------------------------- некроманты
void necromancers(Catalog& c) {
  c.group("Некроманты");
  // Череп и скрещённые кости.
  {
    const std::string skull = "M50 6C34 6 24 17 24 32C24 41 28 47 34 51V58A4 4 0 0 0 38 62H62A4 4 0 0 0 66 58V51C72 47 76 41 76 32"
                              "C76 17 66 6 50 6Z";
    const std::string bone = rrect(-38, -4, 76, 8, 4) + circ(-39, -4.5, 5.5) + circ(-39, 4.5, 5.5) + circ(39, -4.5, 5.5) + circ(39, 4.5, 5.5);
    c.add("necro-crossbones", "Череп и кости",
          {G::F(xf(bone, place(50, 64, 34)) + xf(bone, place(50, 64, -34))), G::XS(skull, 5), G::F(skull),
           G::X(ell(40, 33, 6.5, 7) + ell(60, 33, 6.5, 7) + "M50 41 53.5 48H46.5Z" + rrect(42.5, 54, 3, 8, 1) + rrect(48.5, 54, 3, 8, 1) +
                rrect(54.5, 54, 3, 8, 1))});
  }
  // Коса.
  c.add("necro-scythe", "Коса",
        {G::S("M22 96C38 66 58 36 76 12", 6.5), G::S("M41 67 54 73M60 38 67 43", 5), G::F("M83 10C64 0 34 4 10 36C30 21 54 19 76 26Z")});
  // Рука скелета.
  {
    struct Finger {
      Pt k, t;
    };
    const Finger fs[] = {{{38, 47}, {30, 11}}, {{48, 45}, {47, 6}}, {{58, 47}, {63, 10}}, {{66, 52}, {77, 24}}, {{33, 63}, {15, 43}}};
    std::string bones, joints;
    for (const Finger& f : fs) {
      bones += "M" + ptS(f.k) + "L" + ptS(f.t) + "M48 70L" + ptS(f.k);
      const float dx = f.t.x - f.k.x, dy = f.t.y - f.k.y, len = std::hypot(dx, dy), nx = -dy / len * 5, ny = dx / len * 5;
      for (float t : {0.f, 0.45f, 0.75f}) {
        const Pt j{f.k.x + dx * t, f.k.y + dy * t};
        joints += "M" + ptS({j.x - nx, j.y - ny}) + "L" + ptS({j.x + nx, j.y + ny});
      }
    }
    c.add("necro-hand", "Рука скелета",
          {G::S(bones, 7), G::F(ell(48, 72, 9, 6)), G::S("M44 76V90M54 76V90", 6.5), G::XS(joints, 2.2),
           G::F("M10 98C18 88 34 86 50 86C66 86 82 88 90 98Z")});
  }
  // Надгробие с крестом и трещиной.
  c.add("necro-tomb", "Надгробие",
        {G::F("M24 88V38C24 20 36 8 50 8C64 8 76 20 76 38V88Z" + rrect(14, 84, 72, 10, 3)),
         G::X(rrect(46.5, 24, 7, 36, 1.5) + rrect(37, 33, 26, 7, 1.5)), G::XS("M70 46 64 54 68 60 62 70", 2.2), G::XS("M16 84.5H84", 2)});
}

// ---------------------------------------------------------------- тёмные эльфы
void darkElves(Catalog& c) {
  c.group("Тёмные эльфы");
  // Паук (сверху), песочные часы на брюшке.
  {
    const std::string legs = "M44 33 31 17 27 4M43 37 23 27 8 26M43 42 22 47 8 60M44 47 29 63 21 88";
    c.add("dark-elf-spider", "Паук",
          moved({G::S(sym(legs), 5), G::F(ell(50, 38, 9, 10) + ell(50, 66, 15, 20) + sym("M46 29 43.5 22 49 27Z")),
                 G::X("M44.5 58H55.5L51.2 65 55.5 72H44.5L48.8 65Z")},
                Affine::translate(0, 3)));
  }
  // Паутина.
  {
    std::string radial, rings;
    for (int k = 0; k < 8; k++) {
      const double a = -90 + 45 * k;
      radial += "M50 50L" + pt(50, 50, 46, a);
      for (double r : {13.0, 25.0, 37.0}) rings += "M" + pt(50, 50, r, a) + "Q" + pt(50, 50, r * 0.8, a + 22.5) + " " + pt(50, 50, r, a + 45);
    }
    c.add("dark-elf-web", "Паутина", {G::S(radial, 4.4), G::S(rings, 3.8)});
  }
  // Скрещённые кинжалы с изогнутыми клинками.
  c.add("dark-elf-daggers", "Скрещённые кинжалы",
        crossed("M50 2C59 14 62 30 58 55H43C45.5 39 45.5 21 50 2Z" + std::string("M27 51 42 56H58L73 51 67 61H33Z") +
                    rrect(45.5, 60, 9, 22, 2.5) + "M50 80 56 88 50 97 44 88Z",
                "", "M50.5 12C53 24 54 36 52 50M45.5 66H54.5M45.5 71H54.5M45.5 76H54.5", 1.8, 36, 50, 50, 1.06f));
  // Полумесяц и паук на нити.
  c.add("dark-elf-crescent", "Полумесяц и паук",
        moved({G::F(circ(44, 50, 42)), G::X(circ(60, 44, 36)), G::S("M53 12V45", 1.8),
               G::S("M50 50 43 46 41 41M50 52 42 53 39 57M60 50 67 46 69 41M60 52 68 53 71 57", 2.6),
               G::F(ell(55, 51, 6, 5) + ell(55, 59, 7.5, 7))},
              Affine::translate(9, 0)));
}

// ---------------------------------------------------------------- наги
void nagas(Catalog& c) {
  c.group("Наги");
  // Кобра с раскрытым капюшоном, кольца хвоста.
  c.add("naga-cobra", "Кобра",
        {G::F("M50 8C58 8 63 13 64 20C72 24 78 32 78 42C78 54 68 62 58 68L56 76H44L42 68C32 62 22 54 22 42C22 32 28 24 36 20"
              "C37 13 42 8 50 8Z" +
              rrect(44.5, 74, 11, 14, 4)),
         G::S(ell(50, 88.5, 28, 6.5), 6.5), G::X(sym("M40.5 13 47 15.5 46 18.5 41 16.5Z")),
         G::XS("M44 34H56M43 40H57M43 46H57M44 52H56M45 58H55", 1.8)});
  // Трезубец.
  c.add("naga-trident", "Трезубец",
        {G::F("M50 3 58 17 54 16V45H46V16L42 17Z" + sym("M22 12 29.5 25 26 24V42H18V24L14.5 25Z") +
              "M18 38C18 50 30 56 50 56C70 56 82 50 82 38H74C74 46 64 48.5 50 48.5C36 48.5 26 46 26 38Z" + rrect(46, 50, 8, 40, 2) +
              "M45 88H55L50 97Z")});
  // Раковина-гребешок.
  {
    const double cx = 50, cy = 46, rv = 40, rb = 46;
    std::string edge = "M50 86L" + pt(cx, cy, rv, 160), ribs;
    for (int j = 0; j < 10; j++) {
      const double v = 160 + 22 * j;
      edge += "Q" + pt(cx, cy, rb, v + 11) + " " + pt(cx, cy, rv, v + 22);
      if (j > 0) {
        const Pt e = polar(cx, cy, rv, v);
        ribs += "M" + ptS({50 + (e.x - 50) * 0.12f, 84 + (e.y - 84) * 0.12f}) + "L" + ptS({50 + (e.x - 50) * 0.86f, 84 + (e.y - 84) * 0.86f});
      }
    }
    c.add("naga-shell", "Раковина", {G::F(edge + "Z" + sym("M36 80 46 84V92H33Z")), G::XS(ribs, 2.6)});
  }
  // Два змея вокруг жезла с жемчужиной, головы друг к другу.
  {
    const std::string body = "M50 92C34 88 30 80 50 72C70 64 72 54 50 46C32 40 26 32 32 27";
    const std::string head = xf(ell(0, 0, 8.5, 5.5), place(36.5, 24.5, -40));
    c.add("naga-serpents", "Два змея",
          {G::F(rrect(47, 14, 6, 82, 3) + circ(50, 8, 6)), G::XS(sym(body), 10), G::S(sym(body), 6.5), G::XS(sym(head), 3), G::F(sym(head)),
           G::X(sym(circ(36, 22.5, 1.4)))});
  }
}

// ---------------------------------------------------------------- рыцари
void knights(Catalog& c) {
  c.group("Рыцари");
  // Топфхельм: смотровые щели, крестовая накладка, отверстия для дыхания.
  {
    std::string holes;
    for (double x : {32.0, 37.5}) {
      for (double y : {56.0, 63.0, 70.0}) holes += circ(x, y, 1.8);
    }
    c.add("knight-helm", "Шлем рыцаря",
          {G::F("M24 26C24 14 36 6 50 6C64 6 76 14 76 26V80C76 88 64 93 50 95C36 93 24 88 24 80Z"),
           G::X(rrect(27, 38, 19, 6, 2) + rrect(54, 38, 19, 6, 2) + sym(holes)), G::XS("M45 30V93M55 30V93M25 30H75", 2)});
  }
  // Шахматный конь.
  c.add("knight-chess", "Шахматный конь",
        {G::F(rrect(18, 86, 64, 10, 3) + "M24 86C26 80 30 76 36 74H66C70 76 74 80 76 86Z"),
         G::XS("M34 75.5H70", 2.4),
         G::F("M33 76C33 66 37 58 43 52L39 50C33 54 27 56 21 58C15 59 11 55 13 49L29 30C31 22 37 16 43 12L45 4 51 9 55 4 59 11"
              "C71 14 79 26 81 42C83 56 79 68 71 76Z"),
         G::X(ell(37, 30, 2.6, 2) + circ(17, 51, 1.6)), G::XS("M66 18 72 20M72 26 78 28M76 36 81 38M78 46 83 47M78 56 83 56", 2.2)});
  // Латная перчатка (кулак).
  {
    const std::string thumb = "M18 44C18 38 22 36 28 36H54C58 36 60 40 58 44L56 50C55 53 52 54 49 54H26C21 54 18 50 18 44Z";
    c.add("knight-gauntlet", "Латная перчатка",
          {G::F(rrect(22, 12, 14, 30, 6.5) + rrect(36.5, 9, 14, 32, 6.5) + rrect(51, 9, 14, 32, 6.5) + rrect(65.5, 12, 13, 29, 6.5) +
                rrect(22, 30, 56.5, 32, 7) + "M28 60H72L80 92H20Z"),
           G::XS("M36.25 14V36M50.75 12V36M65.25 14V36", 2.2), G::XS("M23 26H78", 2), G::XS("M27 60.5H73M27 70H73M25 80H75", 2.2),
           G::XS(thumb, 3), G::F(thumb)});
  }
  // Щит и меч.
  {
    const std::string shield = "M22 30H78V54C78 70 67 80 50 88C33 80 22 70 22 54Z";
    c.add("knight-shield", "Щит и меч",
          {G::F(circ(50, 7, 5) + rrect(47, 11, 6, 11, 1.5) + rrect(26, 21, 48, 6, 3) + "M45 27H55V88L50 98 45 88Z"), G::XS(shield, 4.5),
           G::F(shield), G::X(rrect(47, 37, 6, 40, 1) + rrect(31, 47, 38, 6, 1))});
  }
}

// ---------------------------------------------------------------- святые
void holy(Catalog& c) {
  c.group("Святые");
  // Крест с расширенными концами.
  {
    const std::string arm = "M44 45C42.5 31 39.5 17 33 5H67C60.5 17 57.5 31 56 45Z";
    c.add("holy-cross", "Крест",
          {G::F(arm + xf(arm, rotAt(90, 50, 50)) + xf(arm, rotAt(180, 50, 50)) + xf(arm, rotAt(270, 50, 50)) + rrect(43, 43, 14, 14, 0)),
           G::X(circ(50, 50, 4.5))});
  }
  // Крылья и нимб: маховые перья внахлёст, кроющие сверху.
  {
    const Pt bases[] = {{16, 25}, {20, 30}, {25, 36}, {30, 41}, {35, 46}, {40, 51}, {45, 56}};
    const Pt tips[] = {{3, 42}, {6, 55}, {12, 67}, {20, 77}, {29, 84}, {38, 88}, {46, 87}};
    std::vector<std::string> feathers;
    for (int k = 0; k < 7; k++) feathers.push_back(feather(bases[k], tips[k], k == 6 ? 10 : 12));
    feathers.push_back("M47 62C40 46 30 34 14 21C8 24 9 32 16 37C26 45 36 53 43 65Z");
    std::vector<L> v;
    layeredFeathers(v, feathers, 2.2f, true);
    v.push_back(G::S(ell(50, 13, 15, 5.5), 4.5));
    c.add("holy-wings", "Крылья и нимб", moved(v, Affine::translate(0, 2)));
  }
  // Голубь с оливковой ветвью.
  c.add("holy-dove", "Голубь",
        moved({G::F(circ(20, 44, 9) + "M12 42 3 46 12 48Z" +
                    std::string("M18 50C26 60 40 66 56 66C66 66 76 63 86 60L97 62 92 54 97 46 84 50C74 50 64 46 56 42C44 36 30 36 22 38Z") +
                    "M40 46C42 30 52 14 72 4C70 12 72 16 78 16C72 22 72 26 80 28C72 32 72 36 78 40C68 46 54 50 40 46Z" +
                    "M34 42C30 30 30 18 36 6C38 14 42 18 48 20C44 24 44 28 50 32C46 36 42 40 34 42Z"),
               G::S("M7 47C10 54 16 58 24 60", 2),
               G::F(xf(ell(0, 0, 2, 4.5), place(11, 55, -40)) + xf(ell(0, 0, 2, 4.5), place(17, 60, 60)) + xf(ell(0, 0, 2, 4.5), place(23, 63, -60))),
               G::X(circ(20, 42, 1.6))},
              Affine::translate(0, 14)));
  // Святой Грааль: чаша с крестом и сиянием.
  {
    std::string rays;
    for (int i = 0; i < 7; i++) {
      const double a = -162 + 24 * i, r = i % 2 ? 22 : 28;
      rays += "M" + pt(50, 30, 10, a - 5) + "L" + pt(50, 30, r, a) + "L" + pt(50, 30, 10, a + 5) + "Z";
    }
    c.add("holy-grail", "Святой Грааль",
          {G::F(rays + "M24 30H76C76 50 66 62 50 64C34 62 24 50 24 30Z" + rrect(46.5, 62, 7, 18, 2) + ell(50, 70, 6.5, 3.5) +
                "M30 94C32 86 40 82 50 82C60 82 68 86 70 94Z"),
           G::X(rrect(47.5, 36, 5, 18, 1) + rrect(41, 41, 18, 5, 1))});
  }
}

// ---------------------------------------------------------------- демоны
void demons(Catalog& c) {
  c.group("Демоны");
  // Голова демона.
  c.add("demon-head", "Голова демона",
        {G::F(sym("M50 24C42 24 35 27 31 34L28 45 17 41 27 53C29 64 35 74 43 84L50 95H50.5V24Z" +
                  std::string("M37 31C29 23 23 13 24 2C17 11 15 23 22 33C25 37 29 39 32 41Z"))),
         G::X(sym("M33 47 46 51 44 56 35 53Z") + "M46.5 64 50 60 53.5 64 50 66Z" + "M35 71Q50 80 65 71L61 78 57 74.5 50 81 43 74.5 39 78Z")});
  // Бес: рогатая голова, перепончатые крылья, хвост с остриём.
  {
    const std::string half = "M43 19C38 13 36 7 38 2C33 7 31 14 35 21Z" + std::string("M41 26 31 22 40 31Z") +
                             "M43 40C36 32 26 25 10 22C13 28 13 34 9 40C15 40 19 43 21 49C25 47 29 47 32 51C35 48 39 48 42 50Z";
    c.add("demon-imp", "Бес",
          {G::F(sym(half) + circ(50, 26, 10) + "M42 37H58C60 37 61 39 60.5 41L57 60H43L39.5 41C39 39 40 37 42 37Z" + "M75 86 85 88 78 96Z"),
           G::S(sym("M41 41 33 51 31 60"), 5), G::S(sym("M46 58 42 72 45 81 41 92"), 5.5),
           G::S("M55 59C66 66 72 76 67 85C64 91 70 95 77 91", 3.4),
           G::X(sym("M43.5 24.5 48.5 26.5 47.5 29 44 28Z") + "M44 31Q50 35 56 31L54.5 33.5 50 35.8 45.5 33.5Z")});
  }
  // Когтистая лапа: сужающиеся пальцы, суставы, крючковатые когти (загнуты к ладони).
  {
    const Pt ks[] = {{36, 50}, {45, 46}, {55, 46}, {64, 50}, {34, 66}};
    const Pt ts[] = {{27, 25}, {42, 17}, {60, 17}, {75, 28}, {17, 53}};
    std::string fingers, claws, cuts;
    for (int i = 0; i < 5; i++) {
      const Pt k = ks[i], t = ts[i];
      const float dx = t.x - k.x, dy = t.y - k.y, len = std::hypot(dx, dy), ux = dx / len, uy = dy / len;
      fingers += taper(k, {k.x + dx / 3, k.y + dy / 3}, {k.x + dx * 2 / 3, k.y + dy * 2 / 3}, t, 10, 6.5);
      float hx = -uy, hy = ux;
      if ((t.x < 50) != (hx > 0)) {
        hx = -hx;
        hy = -hy;
      }
      const Pt tip{t.x + ux * 15 + hx * 5, t.y + uy * 15 + hy * 5};
      claws += "M" + ptS({t.x - hx * 4.5f, t.y - hy * 4.5f}) + "Q" + ptS({t.x + ux * 12 - hx * 2, t.y + uy * 12 - hy * 2}) + " " + ptS(tip) +
               "Q" + ptS({t.x + ux * 6 + hx * 5, t.y + uy * 6 + hy * 5}) + " " + ptS({t.x + hx * 4.5f, t.y + hy * 4.5f}) + "Z";
      for (float f : {0.5f, 1.f}) {
        const Pt j{k.x + dx * f, k.y + dy * f};
        cuts += "M" + ptS({j.x - hx * 6, j.y - hy * 6}) + "L" + ptS({j.x + hx * 6, j.y + hy * 6});
      }
    }
    c.add("demon-claw", "Когтистая лапа",
          moved({G::F(fingers + claws +
                      "M31 56C31 47 39 42 50 42C61 42 69 48 69 57C69 66 65 75 59 81C55 85 52 87 48 87C42 87 37 83 34 76C32 70 31 62 31 56Z" +
                      "M41 84H59L57 98H43Z"),
                 G::XS(cuts, 1.8)},
                Affine::translate(3, 0)));
  }
  // Вилы с зазубренными остриями.
  c.add("demon-pitchfork", "Вилы",
        moved({G::S("M30 20C30 38 38 46 50 46C62 46 70 38 70 20M50 46V16", 7.5), G::S("M50 46V86", 8.5),
               G::F("M50 1 60 18 50 13 40 18Z" + std::string("M30 5 40 22 30 17 20 22ZM70 5 80 22 70 17 60 22Z") +
                    "M50 82 57 90 50 98 43 90Z")},
              Affine::translate(-6, 0) * rotAt(22, 50, 50) * scaleAt(0.95f, 50, 50)));
}

// ---------------------------------------------------------------- культисты
void cultists(Catalog& c) {
  c.group("Культисты");
  // Пентаграмма (перевёрнутая) в круге.
  {
    std::string s;
    for (int i = 0; i < 5; i++) s += (i == 0 ? "M" : "L") + pt(50, 50, 42, 90 + 144 * i);
    c.add("cult-pentagram", "Пентаграмма", {G::S(circ(50, 50, 42), 5.5), G::S(s + "Z", 4)});
  }
  // Капюшон: лицо в тени, светящиеся глаза.
  c.add("cult-hood", "Капюшон",
        {G::F("M50 4C64 6 74 18 76 34C78 46 76 56 72 62C82 70 90 82 94 96H6C10 82 18 70 28 62C24 56 22 46 24 34C26 18 36 6 50 4Z"),
         G::X("M50 24C60 24 66 32 66 44C66 56 58 64 50 66C42 64 34 56 34 44C34 32 40 24 50 24Z"), G::F(sym("M38.5 44 47 46.5 45 49.5 39.5 47.5Z")),
         G::XS("M28 62C34 70 42 74 50 74C58 74 66 70 72 62", 2.4), G::XS("M40 78 34 96M60 78 66 96", 2)});
  // Всевидящее око.
  c.add("cult-eye", "Всевидящее око",
        moved({G::S("M50 12L91 84H9Z", 6.5), G::F("M29 62C35 54 42 51 50 51C58 51 65 54 71 62C65 70 58 73 50 73C42 73 35 70 29 62Z"),
               G::X(circ(50, 62, 8.5)), G::F(circ(50, 62, 5.5))},
              Affine::translate(0, 3)));
  // Голова козла с пентаграммой.
  c.add("cult-goat", "Голова козла",
        {G::F(sym("M50 28C44 28 40 30 38 35L36 44 22 44 34 53L38 72C40 80 44 86 50 88H50.5V28Z" +
                  std::string("M40 33C34 25 30 16 30 5C24 14 22 24 26 34C28 38 32 41 36 43Z")) +
              "M44 86 50 98 56 86Z"),
         G::X(sym(xf(ell(0, 0, 4.5, 2.6), place(41.5, 52, 15)) + ell(46, 80, 1.6, 2.4)) + star(50, 42, 6, 2.4, 5, 90))});
}

// ---------------------------------------------------------------- магия
void magic(Catalog& c) {
  c.group("Магия");
  // Шляпа мага.
  c.add("magic-hat", "Шляпа мага",
        {G::F("M28 76C33 56 39 34 45 20C49 11 57 6 70 7C61 12 57 18 57 27C59 43 65 61 72 76Z"), G::XS("M24 76C40 80 60 80 76 76", 2.6),
         G::F(ell(50, 79, 45, 10.5)), G::XS("M31 68C43 72 57 72 69 68", 3.2),
         G::X(star(47, 46, 7, 2.8, 5, -90) + star(56, 28, 4.5, 1.8, 5, -90) + circ(38, 58, 1.8) + circ(61, 54, 1.5))});
  // Книга заклинаний.
  {
    const std::string pages = "M50 42C40 36 27 34 11 35V82C27 81 40 83 50 89C60 83 73 81 89 82V35C73 34 60 36 50 42Z";
    const std::string lines = "M17 46C26 45 36 46 44 50M17 55C26 54 36 55 44 59M17 64C26 63 36 64 44 68";
    c.add("magic-book", "Книга заклинаний",
          {G::F("M50 46C40 40 26 38 6 40V88C26 86 40 88 50 94C60 88 74 86 94 88V40C74 38 60 40 50 46Z"), G::XS(pages, 3), G::F(pages),
           G::XS("M50 43V90", 2.4), G::XS(sym(lines), 1.8),
           G::F(star(50, 18, 14, 3.6, 4, -90) + star(31, 22, 5, 1.5, 4, -90) + star(69, 22, 5, 1.5, 4, -90))});
  }
  // Хрустальный шар.
  c.add("magic-orb", "Хрустальный шар",
        {G::F("M27 68C30 76 37 82 44 84H56C63 82 70 76 73 68L81 74C77 83 70 88 63 90H37C30 88 23 83 19 74Z" + rrect(27, 88, 46, 8, 3)),
         G::XS(circ(50, 40, 31), 4), G::F(circ(50, 40, 31)), G::XS(arc(50, 40, 23, 200, 250), 4), G::X(circ(37.5, 28, 2.6)),
         G::XS("M30 47C37 41 45 50 53 44C59 40 65 43 70 39", 2.6)});
  // Посох с кристаллом.
  c.add("magic-staff", "Посох",
        moved({G::S("M20 96C32 78 48 56 62 38", 8), G::S("M59 42C50 33 50 20 58 11M66 44C76 46 86 40 88 30", 5),
               G::F(xf("M0 -20L10 0 0 20-10 0Z", place(71, 23, 34)) + star(88, 9, 8, 2.4, 4, -90) + star(46, 12, 5.5, 1.7, 4, -90) +
                    star(90, 48, 5, 1.5, 4, -90))},
              Affine::translate(-5, 0)));
  // Зелье.
  c.add("magic-potion", "Зелье",
        {G::F(circ(50, 64, 28) + rrect(41, 20, 18, 26, 3) + rrect(37, 16, 26, 7, 3.5) + rrect(43, 6, 14, 12, 3)),
         G::X(circ(50, 64, 24) + rrect(45, 23, 10, 22, 2)), G::XS("M42 16.5H58", 1.6),
         G::F("M29.1 62C35 58 43 66 50 62C57 58 65 66 70.9 62A21 21 0 1 1 29.1 62Z" + circ(44, 52, 2.4) + circ(55, 47, 1.8) + circ(50, 36, 1.8))});
}

// ---------------------------------------------------------------- орки
void orcs(Catalog& c) {
  c.group("Орки");
  // Голова орка с клыками.
  {
    const std::string tusk = "M35 80 37.5 63 43 79Z";
    c.add("orc-head", "Голова орка",
          {G::F(sym("M50 12C40 12 33 16 29 24L27 34 9 29 25 45C23 57 23 67 27 75C31 85 40 92 50 92H50.5V12Z") + ell(50, 9, 7, 6)),
           G::X(sym("M33 45 45.5 49 43.5 54 36 52Z" + ell(45.5, 63, 2.2, 1.7)) + "M31 71C40 75 60 75 69 71C65 81 58 86 50 86C42 86 35 81 31 71Z"),
           G::XS(sym("M30 41 47 47"), 2.6), G::XS(sym(tusk), 3), G::F(sym(tusk))});
  }
  // Вепрь.
  c.add("orc-boar", "Вепрь",
        {G::F(sym("M50 16C41 16 32 20 28 27L16 19 20 36C17 45 21 55 30 63C32 71 34 78 37 84H50.5V16Z") + "M38 18 42 7 46 16 50 5 54 16 58 7 62 18Z"),
         G::X(sym("M36 45 43.5 47.5 42 50.5 37 49Z")), G::XS(ell(50, 81, 16.5, 12), 3), G::F(ell(50, 81, 16.5, 12)),
         G::X(ell(44, 81, 3.2, 4.6) + ell(56, 81, 3.2, 4.6)), G::F(sym("M35 84C28 84 22 78 20 64C25 71 31 74 36 74Z"))});
  // Скрещённые тесаки.
  c.add("orc-cleavers", "Скрещённые тесаки",
        crossed("M38 6H60C64 6 66 9 66 13L64 60H40L38 54 42 49 38 44 42 39 38 34 42 29 38 24 42 19 38 14Z" + rrect(45, 59, 10, 28, 3) +
                    circ(50, 90, 5),
                circ(57, 15, 3.5) + circ(50, 90, 2), "M45 66H55M45 72H55M45 78H55", 1.8, 36, 50, 52, 1.0f));
  // Тотем: рогатый череп на шесте, перья.
  c.add("orc-totem", "Тотем",
        {G::F(rrect(47, 44, 6, 54, 2) + rrect(22, 58, 56, 5, 2.5) + ell(26, 77, 3.8, 10) + ell(74, 77, 3.8, 10)), G::S("M26 63V68M74 63V68", 1.8),
         G::XS("M26 70V86M74 70V86", 1.4),
         G::F(sym("M50 14C42 14 36 19 36 27C36 33 39 37 42 41L44 52H50.5V14Z" + std::string("M39 22C30 23 20 18 14 6C23 12 31 14 41 15Z"))),
         G::X(sym("M40 26 47 29 45.5 33 41 31.5Z") + "M48 38 50 35 52 38 50 41Z" + rrect(45.5, 46, 2.2, 6, 1) + rrect(49, 46, 2.2, 6, 1) +
              rrect(52.5, 46, 2.2, 6, 1))});
}

// ---------------------------------------------------------------- ящеры
void lizards(Catalog& c) {
  c.group("Ящеры");
  // Ящерица (сверху).
  {
    const std::string legs = "M43 33 32 28 27 18M43 55 31 60 28 72";
    const std::string toes = "M27 18 22 15M27 18 25 12M27 18 30 13M28 72 22 74M28 72 24 78M28 72 30 78";
    c.add("lizard-crawl", "Ящерица",
          {G::F("M50 4C55 4 58 9 58 15C58 21 55 25 50 27C45 25 42 21 42 15C42 9 45 4 50 4Z" + ell(50, 44, 9.5, 19) +
                taper({50, 58}, {49, 74}, {66, 78}, {62, 96}, 12, 1)),
           G::S(sym(legs), 5), G::S(sym(toes), 3), G::X(sym(circ(45.5, 12, 1.5)))});
  }
  // Голова плащеносной ящерицы: воротник с шипами, тупая морда.
  {
    const double cx = 56, cy = 50;
    std::string frill = "M" + pt(cx, cy, 29, -160), ribs;
    for (int k = 0; k < 10; k++) {
      const double a = -160 + 32 * k;
      frill += "L" + pt(cx, cy, 40, a + 16) + "Q" + pt(cx, cy, 33, a + 28) + " " + pt(cx, cy, 29, a + 32);
      ribs += "M" + pt(cx, cy, 9, a + 16) + "L" + pt(cx, cy, 33, a + 16);
    }
    const std::string head = "M8 50C8 44 14 40 22 38C30 36 40 34 48 36C56 38 60 44 60 50C60 58 54 62 46 62C36 62 24 60 14 58C10 57 8 54 8 50Z";
    c.add("lizard-head", "Голова ящера",
          {G::F(frill + "Z"), G::XS(ribs, 2.2), G::XS(head, 4), G::F(head), G::XS("M10 52C20 53 32 53 42 51", 2.4), G::X(circ(38, 44, 3.4)),
           G::F(circ(38.5, 44, 1.5)), G::X(circ(14, 46.5, 1.3))});
  }
  // Крокодил: длинная морда, глаз на бугре, зубы по линии челюстей, щитки на шее.
  c.add("lizard-croc", "Крокодил",
        {G::F("M4 47C4 43 6 41 9 41C11 38 15 38 17 41L42 39C44 33 49 29 55 29C60 29 63 32 65 36L80 38C86 39 92 42 96 47L96 66"
              "C84 66 72 64 60 62L20 58C12 57 6 54 4 50Z" +
              std::string("M70 37.5 73 32.5 76 38ZM78 38.5 81 33.5 84 39.2ZM86 40 89.5 35.5 91.5 42Z")),
         G::X(ell(56, 34, 3.2, 2) + circ(10.5, 43, 1.2)),
         G::XS("M8 50 12 47.5 16 51 20 48 24 51.5 28 48.5 32 52 36 49 40 52.5 44 49.5 48 53 52 50.5 60 53.5 68 54.5", 2)});
  // Храм-пирамида.
  c.add("lizard-pyramid", "Храм-пирамида",
        {G::F(rrect(6, 80, 88, 14, 1.5) + rrect(16, 66, 68, 15, 1.5) + rrect(26, 52, 48, 15, 1.5) + rrect(35, 40, 30, 13, 1.5) +
              "M39 40V27H61V40ZM36 27 50 19 64 27Z" + circ(50, 9, 5.5)),
         G::X(rrect(46, 31, 8, 9, 1.5)), G::XS("M44 94V40M56 94V40", 2),
         G::XS("M44 88H56M44 82H56M44 76H56M44 70H56M44 64H56M44 58H56M44 52H56M44 46H56", 1.6)});
  // След ящера: три пальца с когтями от пятки.
  {
    const std::string toe = taper({50, 70}, {50, 52}, {50, 36}, {50, 20}, 17, 9.5) + "M45.5 21.5 50 5 54.5 21.5Z";
    c.add("lizard-track", "След ящера",
          {G::F(ell(50, 78, 14, 15) + toe + xf(toe, rotAt(-36, 50, 72)) + xf(toe, rotAt(36, 50, 72)))});
  }
}

// ---------------------------------------------------------------- зверолюды
void beastmen(Catalog& c) {
  c.group("Зверолюды");
  // Минотавр: бычья голова с кольцом в носу, могучие плечи.
  {
    const std::string head = "M39 22H61C66 22 69 27 68 33L65 52C65 58 62 61 60 64C59 70 56 74 50 74C44 74 41 70 40 64C38 61 35 58 35 52"
                             "L32 33C31 27 34 22 39 22Z";
    const std::string hornEar = "M36 30C26 31 14 27 8 14C6 10 7 7 9 4C12 12 18 17 28 19C32 20 35 21 37 22Z" + std::string("M33 36 22 39 33 45Z");
    c.add("beast-minotaur", "Минотавр",
          {G::F("M6 96C8 82 18 74 32 70H68C82 74 92 82 94 96Z" + rrect(37, 56, 26, 18, 4)),
           G::XS("M50 80V96M30 84C38 88 44 88 50 86M70 84C62 88 56 88 50 86", 2.2), G::XS(head, 3), G::F(head + sym(hornEar)),
           G::X(sym("M38 40 46 43 44.5 46.5 39.5 45Z" + ell(46, 66, 1.8, 2.4))), G::XS(circ(50, 77, 5), 5.5), G::S(circ(50, 77, 5), 2.4)});
  }
  // Кентавр-лучник.
  c.add("beast-centaur", "Кентавр",
        {G::F(ell(60, 58, 25, 12.5) + "M30 58C28 48 28 38 31 28H43C45 38 45 48 44 58Z" + circ(37, 18, 7) + "M2 31 8 28V34Z"),
         G::S("M42 64 38 80 39 94M47 66 40 76 30 79M76 64 82 79 79 94M71 66 71 80 67 94", 5.5), G::S("M84 54C93 55 96 64 93 74", 5),
         G::S("M33 31H12", 4.5), G::S("M16 10C9 18 9 44 16 52", 3.4), G::S("M16 10 26 31 16 52", 1.2), G::S("M6 31H28", 1.8)});
  // Сатир: профиль с кольчатыми козлиными рогами, острым ухом, кудрями и бородкой.
  {
    const Pt h0{40, 26}, h1{44, 11}, h2{62, 4}, h3{80, 11};
    std::string rings;
    for (double t : {0.3, 0.46, 0.62, 0.78}) {
      const CurvePt q = bezier(h0, h1, h2, h3, t);
      rings += "M" + ptS({float(q.x - q.nx * 7), float(q.y - q.ny * 7)}) + "L" + ptS({float(q.x + q.nx * 7), float(q.y + q.ny * 7)});
    }
    const std::string horn = taper(h0, h1, h2, h3, 13, 2);
    c.add("beast-satyr", "Сатир",
          moved({G::F(xf(horn, Affine::translate(7, 3)) + "M46 14C36 14 30 20 30 28L28 34 20 42 27 45 25 48 28 51 26 54 29 57C29 64 30 72 34 80"
                                                         "L38 92 42 80C46 76 52 74 56 74C64 72 70 64 72 54C74 42 72 30 66 22C61 16 54 14 46 14Z" +
                      "M61 33 87 24 67 47Z" + circ(66, 19, 5.5) + circ(72, 27, 5.5) + circ(75, 37, 5.5) + circ(74, 47, 5.5) + circ(70, 56, 5)),
                 G::XS(horn, 3), G::F(horn), G::XS(rings, 1.6), G::X("M33 33 39 34 35 36Z"), G::XS("M32 62 36 80M38 66 40 80", 1.8),
                 G::XS("M27 51.5H33", 1.6)},
                Affine::translate(-3, 2)));
  }
  // Гарпия: голова с длинными волосами, крылья вместо рук, птичьи лапы с когтями.
  {
    const Pt bases[] = {{17, 17}, {22, 21}, {27, 25}, {32, 29}, {37, 33}, {42, 37}};
    const Pt tips[] = {{2, 31}, {4, 43}, {9, 54}, {16, 62}, {25, 67}, {35, 68}};
    std::vector<std::string> feathers;
    for (int k = 0; k < 6; k++) feathers.push_back(feather(bases[k], tips[k], 10));
    feathers.push_back("M45 36C39 26 29 18 15 12C10 15 11 22 17 26C26 31 35 37 42 43Z");
    std::vector<L> v;
    layeredFeathers(v, feathers, 2.2f, true);
    v.push_back(G::F(sym("M50 9C44 9 40 13 40 19C40 27 38 35 33 44C40 43 44 38 46 31Z") +
                     "M42 29C46 31 54 31 58 29L56 40C55 44 55 48 57 54H43C45 48 45 44 44 40Z" + "M44 52 50 74 56 52Z"));
    v.push_back(G::XS(ell(50, 20, 6, 7.5), 2.4));
    v.push_back(G::F(ell(50, 20, 6, 7.5)));
    v.push_back(G::S(sym("M46 54 44 68 46 80"), 5));
    v.push_back(G::S(sym("M46 80 39 88M46 80 45 91M46 80 51 88"), 3));
    c.add("beast-harpy", "Гарпия", v);
  }
  // Русалка: волосы по спине, рука у волос, хвост с плавником.
  c.add("beast-mermaid", "Русалка",
        moved({G::F(circ(28, 16, 8.5) + "M20.5 14 17 19 21 20Z" +
                    std::string("M20 14C20 6 26 4 31 5C38 6 41 12 41 20C41 28 44 34 48 40C42 40 38 36 36 32C36 38 38 42 41 46"
                                "C35 45 31 40 30 34Z") +
                    "M23 27C21 37 23 47 29 56H47C45 46 42 36 37 26Z" + taper({38, 54}, {38, 74}, {48, 88}, {66, 86}, 20, 10) +
                    taper({66, 86}, {76, 85}, {82, 78}, {86, 68}, 10, 6) + "M84 72C82 60 88 50 98 46C95 54 95 60 98 68C93 67 88 68 84 72Z"),
               G::S("M26 32C19 37 16 43 18 50", 4.5), G::XS("M36 66Q41 68 46 65M39 74Q45 76 50 72M46 81Q52 82 56 78", 1.8)},
              Affine::translate(-8, 0)));
}

}  // namespace

void defineFantasyEmblems(GlyphBuilder& b, std::vector<EmblemGroup>& groups, std::vector<std::pair<const char*, const char*>>& titles) {
  Catalog c{b, groups, titles};
  dragons(c);
  vampires(c);
  nature(c);
  elements(c);
  dwarves(c);
  elves(c);
  necromancers(c);
  darkElves(c);
  nagas(c);
  knights(c);
  holy(c);
  demons(c);
  cultists(c);
  magic(c);
  orcs(c);
  lizards(c);
  beastmen(c);
}

}  // namespace rg::gfx
