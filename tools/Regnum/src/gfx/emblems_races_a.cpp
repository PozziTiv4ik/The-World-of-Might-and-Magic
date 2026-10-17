// Regnum — эмблемы рас справочника: «Ангелы» … «Кентавры» (раздел «Расы», группа на расу, по алфавиту; имена race-<раса>-…).
// Сетка 100 × 100, стиль классических эмблем: одноцветный силуэт, детали — вырезы. Симметричные силуэты описаны левой
// половиной. Сюжеты отличаются от тематических групп с теми же названиями («Гномы», «Демоны», «Вампиры»).
#include <tuple>

#include "gfx/emblems_internal.h"

namespace rg::gfx::emb {

namespace {

inline Pt P(double x, double y) { return {float(x), float(y)}; }
inline Pt lerpP(Pt a, Pt b, double t) { return P(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
inline std::string n2(double x, double y) { return n(x) + " " + n(y); }

// Полоса переменной ширины w(t) вдоль кривой f(t), t ∈ [0, 1].
template <class F, class W>
std::string ribbon(F f, W w, int steps = 40) {
  std::vector<Pt> lft, rgt;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, e = 1e-3;
    const Pt p = f(t), q = f(std::min(1.0, t + e)), o = f(std::max(0.0, t - e));
    const double dx = q.x - o.x, dy = q.y - o.y, len = std::max(1e-9, std::hypot(dx, dy));
    const double nx = -dy / len, ny = dx / len, h = w(t) * 0.5;
    lft.push_back(P(p.x + nx * h, p.y + ny * h));
    rgt.push_back(P(p.x - nx * h, p.y - ny * h));
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Неровная окружность: r(a) = r + amp·sin(k·a + ph) + amp/2·sin((2k+1)·a).
std::string wobble(double cx, double cy, double r, double amp, int k, double ph, int steps = 90) {
  std::string s;
  for (int i = 0; i < steps; i++) {
    const double a = 360.0 * i / steps, t = a * kPi / 180;
    const double rr = r + amp * std::sin(k * t + ph) + amp * 0.5 * std::sin((2 * k + 1) * t);
    s += (i ? "L" : "M") + pt(cx, cy, rr, a);
  }
  return s + "Z";
}

// Зубчатый край вдоль кубической кривой (продолжение пути, без начальной точки): teeth зубцов глубиной depth по нормали
// bezier (для левого края, идущего вниз, — наружу), кончики чуть вперёд по ходу кривой.
std::string serrate(Pt p0, Pt c1, Pt c2, Pt p1, int teeth, double depth) {
  std::string s;
  for (int i = 1; i <= 2 * teeth; i++) {
    const CurvePt q = bezier(p0, c1, c2, p1, double(i) / (2 * teeth));
    const double d = i % 2 ? depth : 0, f = i % 2 ? depth * 0.45 : 0;
    s += "L" + n2(q.x + q.nx * d + q.ny * f, q.y + q.ny * d - q.nx * f);
  }
  return s;
}

// Каноническое левое крыло из перьев: плечо в начале координат, маховые свисают влево-вниз; последнее — кроющее перо
// вдоль руки крыла. w — ширина маховых.
std::vector<std::string> wingFeathers(double w = 12) {
  static const Pt bases[] = {{-31, -35}, {-27, -30}, {-22, -24}, {-17, -19}, {-12, -14}, {-7, -9}, {-2, -4}};
  static const Pt tips[] = {{-44, -18}, {-41, -5}, {-35, 7}, {-27, 17}, {-18, 24}, {-9, 28}, {-1, 27}};
  std::vector<std::string> fs;
  for (int k = 0; k < 7; k++) fs.push_back(feather(bases[k], tips[k], k == 6 ? w * 0.83 : w));
  fs.push_back("M0 2C-7 -14 -17 -26 -33 -39C-39 -36 -38 -28 -31 -23C-21 -15 -11 -7 -4 5Z");
  return fs;
}

// Крыло, перенесённое преобразованием m (mirror — пара, отражение относительно x = 50); перья внахлёст с зазором.
void addWing(std::vector<L>& v, const Affine& m, bool mirror, float gap = 2.2f, double w = 12) {
  std::vector<std::string> fs;
  for (const std::string& f : wingFeathers(w)) fs.push_back(xf(f, m));
  layeredFeathers(v, fs, gap, mirror);
}

// ---------------------------------------------------------------- ангелы
void angels(Catalog& c) {
  c.group("Ангелы", EmblemSection::Races);
  // Ангел-воитель: высоко поднятые крылья, нимб, меч наискось вверх в сложенных у груди руках.
  {
    std::vector<L> v;
    addWing(v, place(42, 54, 26, 0.9f), true);
    const std::string robe = sym("M50 33C46 33 42 34 39.5 36.5C37 39 36 43 35.5 48L33 64C31 75 28 86 25 96H50.5V33Z");
    const Affine sm = place(51, 55, 35);
    const std::string sword = xf(circ(0, 9.5, 3.2) + rrect(-2.1, -5, 4.2, 12, 1) + rrect(-12.5, -8, 25, 4.6, 2.3) + "M-3.6 -8V-40L0 -49 3.6 -40V-8Z", sm);
    v.push_back(G::XS(robe + circ(50, 20, 7.5), 3));
    v.push_back(G::F(robe + circ(50, 20, 7.5)));
    v.push_back(G::S(ell(50, 8, 11, 3.6), 3));
    v.push_back(G::XS(sym("M39 38C39 46 42 52 47 55") + "M37 68Q50 72 63 68", 1.8));
    v.push_back(G::XS(sym("M41 74 37 94"), 1.6));
    v.push_back(G::XS(sword, 2.6));
    v.push_back(G::F(sword));
    v.push_back(G::XS(xf("M0 -11V-40", sm), 1.4));
    c.add("race-angels-warrior", "Ангел-воитель", v);
  }
  // Серафим: лик в нимбе, три пары крыльев — верхняя над ликом, средняя в стороны, нижняя под ним.
  {
    std::vector<L> v;
    // Крыло: маховые веером от корня (первое — у передней кромки, самое длинное), сверху — кроющее перо.
    auto pair = [&](Pt root, double dir, double len, double spread, double w) {
      std::vector<std::string> fs;
      for (int i = 0; i < 5; i++) fs.push_back(feather(root, polar(root.x, root.y, len * (1 - 0.12 * i), dir + spread * i), w));
      fs.push_back(feather(root, polar(root.x, root.y, len * 0.55, dir + spread * 0.9), w * 1.7));
      layeredFeathers(v, fs, 1.8f, true);
    };
    pair({45, 58}, 110, 42, 11, 8);
    pair({45, 42}, -110, 42, -11, 8);
    pair({42, 50}, 183, 41, -7, 8.5);
    v.push_back(G::X(circ(50, 50, 18)));
    v.push_back(G::S(circ(50, 50, 15.5), 2.4));
    v.push_back(G::F(circ(50, 50, 11.5)));
    v.push_back(G::X(sym(ell(45.8, 49, 1.7, 1.2)) + "M50 50.5 51.4 54.5H48.6Z"));
    c.add("race-angels-seraph", "Серафим", v);
  }
  // Пылающий меч: клинок в языках пламени, крестовина-крылья, обмотка рукояти.
  {
    const std::string flame = "M50 3C57 14 63 28 62 44C61 54 57 60 50 62C43 60 39 54 38 44C37 28 43 14 50 3Z" +
                              sym(taper({43, 60}, {31, 54}, {26, 41}, {32, 25}, 12, 1.2) + taper({41, 42}, {33, 35}, {32, 24}, {38, 13}, 8, 1));
    const std::string blade = "M46 64V14L50 6 54 14V64Z";
    std::vector<L> v{G::F(flame), G::XS(blade, 3), G::F(blade), G::XS("M50 14V60", 1.6)};
    layeredFeathers(v, {feather({46, 69}, {21, 59}, 9), feather({46, 71}, {19, 70}, 8), feather({46, 73}, {24, 80}, 7)}, 2, true);
    const std::string hilt = rrect(42, 64, 16, 9, 2.5) + rrect(46.5, 72, 7, 16, 2) + circ(50, 92, 4.6);
    v.push_back(G::XS(hilt, 2));
    v.push_back(G::F(hilt));
    v.push_back(G::XS("M46 76.5H54M46 80.5H54M46 84.5H54", 1.4));
    v.push_back(G::X(circ(50, 92, 1.6) + ell(50, 68.5, 2.4, 2)));
    c.add("race-angels-flamesword", "Пылающий меч", v);
  }
  // Труба вестника: длинная труба наискось, знамя со звездой, лучи гласа у раструба.
  {
    const Affine m = place(4, 42, -10, 0.86f);
    const std::string horn = rrect(6, -3.2, 70, 6.4, 3.2) + "M0 -5C3 -5 5 -3.2 7 -2.6V2.6C5 3.2 3 5 0 5Z" + ell(38, 0, 5.8, 5.4) +
                             rrect(17, -4.8, 4.4, 9.6, 2) + rrect(57, -4.8, 4.4, 9.6, 2) + "M73 -3.2C84 -4 92 -9 98 -18V18C92 9 84 4 73 3.2Z";
    const Pt a = m.apply({21, 3}), b = m.apply({62, 3});
    const float drop = 46;
    const Pt mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
    const std::string banner = "M" + ptS(a) + "L" + ptS(b) + "L" + ptS({b.x, b.y + drop}) + "L" + ptS({mid.x, mid.y + drop - 12}) + "L" +
                               ptS({a.x, a.y + drop}) + "Z";
    std::string rays;
    for (int i = -1; i <= 1; i++) rays += xf(taper({102, 0}, {105, 0}, {108, 0}, {112, 0}, 4.4, 1.4), rotAt(float(i * 32), 98, 0));
    c.add("race-angels-trumpet", "Труба вестника",
          {G::F(banner), G::X(star(mid.x, mid.y + 20, 9.5, 3.8, 5, -90)),
           G::XS("M" + ptS({a.x + 3.5f, a.y + 4}) + "V" + n(a.y + drop - 5) + "L" + ptS({mid.x, mid.y + drop - 17}) + "L" + n2(b.x - 3.5f, b.y + drop - 5) +
                     "V" + n(b.y + 4),
                 1.5),
           G::XS(xf(horn, m), 2.6), G::F(xf(horn, m)), G::F(xf(rays, m))});
  }
}

// ---------------------------------------------------------------- арахниды
void arachnids(Catalog& c) {
  c.group("Арахниды", EmblemSection::Races);
  // Паук-воин: человеческий торс на паучьем теле, серпы в поднятых руках, восемь ног.
  {
    struct Leg {
      Pt a, k, f;
    };
    const Leg legs[] = {{{43, 60}, {22, 40}, {7, 57}}, {{42, 64}, {15, 52}, {4, 76}}, {{42, 68}, {20, 64}, {11, 93}}, {{44, 72}, {32, 75}, {27, 97}}};
    std::string ls;
    for (const Leg& g : legs) {
      ls += taper(g.a, lerpP(g.a, g.k, 0.3) + P(0, -6), lerpP(g.a, g.k, 0.7) + P(0, -4), g.k, 5.5, 4) +
            taper(g.k, lerpP(g.k, g.f, 0.33), lerpP(g.k, g.f, 0.66), g.f, 4, 1.2) + circ(g.k.x, g.k.y, 2.6);
    }
    const std::string arm = taper({42, 34}, {37, 35}, {33, 35}, {30, 33}, 5.5, 4.5) + taper({30, 34}, {29, 29}, {28.5, 25}, {28.5, 20}, 4.6, 3.6) +
                            rrect(26.5, 13, 4, 9, 1.5) + taper({28.5, 14}, {26, 4}, {13, 1}, {8, 12}, 5, 0.6);
    const std::string body = "M50 30C46 30 42 31 40 33L38.5 36 41 50 44 56H50.5V30Z";
    c.add("race-arachnids-warrior", "Паук-воин",
          {G::F(sym(ls + arm)), G::XS(ell(50, 64, 11.5, 9.5) + sym(body), 2.6), G::F(ell(50, 64, 11.5, 9.5) + sym(body)),
           G::XS(ell(50, 20, 6.8, 8.2), 2.4), G::F(ell(50, 20, 6.8, 8.2)),
           G::X(sym(ell(47.2, 20, 1.6, 2.1) + circ(46.3, 15.6, 1.1))), G::XS("M42.5 40Q50 44 57.5 40M43.5 47Q50 51 56.5 47", 1.6),
           G::XS("M42 61Q50 67 58 61", 1.6)});
  }
  // Кокон: обмотанный шёлком кокон на нити, растяжки паутины.
  c.add("race-arachnids-cocoon", "Кокон",
        {G::S("M4 6Q28 16 50 18Q72 16 96 6", 2.4), G::S("M16 10 22 3M84 10 78 3M30 15 34 4M70 15 66 4", 1.8), G::S("M50 17V32", 2.4),
         G::F("M50 27C61 31 67 44 67 58C67 74 59 87 50 96C41 87 33 74 33 58C33 44 39 31 50 27Z"),
         G::XS("M35 46Q50 39 65 49M34 59Q50 51 66 63M36 72Q50 65 63 77M41 85Q50 79 58 88", 2.2), G::XS("M44 33Q47 31 50 32", 1.6)});
  // Паучьи жвала: голова с восемью глазами, хелицеры с клыками, педипальпы, передние ноги.
  {
    const std::string head = "M50 6C38 6 27 13 24 26C22 36 25 46 33 52C37 55 43 56 50.5 56V6Z";
    const std::string chel = xf("M0 -13C5 -13 7 -8 7 -2C7 5 4 10 0 13C-4 10 -7 5 -7 -2C-7 -8 -5 -13 0 -13Z", place(41, 66, -14));
    const std::string fang = taper({38, 74}, {33, 83}, {36, 92}, {45, 96}, 7, 0.8);
    const std::string palp = taper({29, 46}, {19, 49}, {14, 58}, {16, 70}, 5.6, 3.4) + taper({16, 69}, {17, 77}, {20, 83}, {25, 88}, 3.4, 1.2);
    const std::string legs = taper({30, 15}, {24, 9}, {15, 5}, {5, 9}, 6, 2.4) + taper({25.5, 25}, {17, 21}, {8, 24}, {3, 35}, 5.6, 2.2);
    c.add("race-arachnids-fangs", "Паучьи жвала",
          {G::F(sym(head + legs + palp)), G::XS(sym(chel + fang), 2.6), G::F(sym(chel + fang)),
           G::X(sym(ell(43.5, 33, 5, 6) + circ(46.5, 21.5, 2.8) + circ(35.5, 24.5, 2.3) + circ(32.5, 34, 2))),
           G::F(sym(circ(45, 31, 1.7))), G::XS(sym(xf("M-2 -6 2 -3M-2.5 0 2.5 3", place(41, 66, -14))), 1.4)});
  }
  // Паутина с глазами: шестиугольная сеть, в центре — паучья голова с восемью глазами.
  {
    std::string radial, rings;
    for (int k = 0; k < 6; k++) {
      const double a = -90 + 60 * k;
      radial += "M" + pt(50, 50, 17, a) + "L" + pt(50, 50, 47, a);
      for (double r : {24.0, 34.0, 44.0}) rings += "M" + pt(50, 50, r, a) + "Q" + pt(50, 50, r * 0.78, a + 30) + " " + pt(50, 50, r, a + 60);
    }
    c.add("race-arachnids-web", "Паутина с глазами",
          {G::S(radial, 3.4), G::S(rings, 3), G::F(circ(50, 50, 15)),
           G::X(sym(circ(45, 53, 3.4) + circ(39.5, 47.5, 1.9) + circ(44.5, 43.5, 1.7) + circ(41.5, 57.5, 1.3)))});
  }
}

// ---------------------------------------------------------------- арборианы
void arborians(Catalog& c) {
  c.group("Арборианы", EmblemSection::Races);
  // Человек-дерево: ствол-тело с ликом, руки-ветви с листьями, корни-ноги.
  {
    const std::string trunk = "M50 24C45 24 42 28 42 35L41 50C41 59 42 66 44 74H50.5V24Z";
    const std::string arms = taper({43, 52}, {34, 48}, {27, 39}, {25, 23}, 10, 4) + taper({29, 36}, {22, 34}, {17, 30}, {12, 23}, 4.4, 1.8) +
                             taper({25.5, 26}, {25, 19}, {22, 14}, {18, 10}, 3.8, 1.6) + taper({46, 27}, {45, 20}, {42, 16}, {39, 12}, 3.8, 1.5);
    const std::string roots = taper({44, 70}, {40, 81}, {32, 88}, {19, 94}, 8, 1.6) + taper({46, 75}, {45, 84}, {42, 91}, {37, 97}, 6, 1.6);
    std::vector<std::string> leaves;
    for (auto [bx, by, tx, ty, w] : {std::tuple{12.5, 23.5, 3.0, 19.0, 7.0}, std::tuple{12.5, 23.0, 7.0, 13.0, 6.5}, std::tuple{18.0, 10.5, 11.0, 4.0, 6.5},
                                    std::tuple{18.5, 10.0, 24.0, 3.0, 6.0}, std::tuple{25.0, 23.0, 31.0, 14.0, 6.0}, std::tuple{39.0, 12.5, 35.0, 3.5, 6.0},
                                    std::tuple{39.5, 12.0, 46.5, 5.0, 5.5}, std::tuple{30.0, 40.0, 19.0, 44.0, 6.0}})
      leaves.push_back(feather(P(bx, by), P(tx, ty), w));
    std::vector<L> v{G::F(sym(trunk + arms + roots))};
    layeredFeathers(v, leaves, 1.4f, true);
    v.push_back(G::X(sym("M43.5 38Q46.5 35 48.5 39Q46 40.5 43.5 38Z") + "M45 48Q50 51 55 48Q50 54.5 45 48Z"));
    v.push_back(G::XS(sym("M47.5 58C46.5 62 47.5 66 46.5 72"), 1.4));
    c.add("race-arborians-treeman", "Человек-дерево", v);
  }
  // Лист-маска: лик из листа с зубчатым краем, прорези глаз, прожилки.
  {
    const std::string half = "M50 18C46 10 39 6 31 7" + serrate({31, 7}, {15, 8}, {5, 24}, {6, 41}, 4, 2.8) +
                             serrate({6, 41}, {7, 62}, {30, 84}, {50, 97}, 7, 2.8) + "L50.5 97V18Z";
    c.add("race-arborians-leafmask", "Лист-маска",
          {G::F(sym(half)), G::S("M50 19C50 13 52 8 56 4", 3), G::X(sym("M27 45C32 39 40 39 45 44C40 48 33 49 27 45Z") + "M40 70Q50 76 60 70Q50 80 40 70Z"),
           G::XS("M50 20V62", 2.2), G::XS(sym("M50 26 33 14M50 33 17 28M47.5 56 22 57M48 63 27 74M50 84 40 88"), 1.6)});
  }
  // Кольца ствола: спил с годичными кольцами, трещина, росток на коре.
  {
    const double cx = 50, cy = 57;
    std::string rings;
    for (double r : {9.0, 16.0, 23.0, 30.0}) rings += wobble(cx + 0.8, cy, r, 0.5 + r * 0.02, 5, r);
    c.add("race-arborians-rings", "Кольца ствола",
          {G::F(wobble(cx, cy, 39, 1.2, 7, 0.4)), G::XS(wobble(cx, cy, 35, 0.9, 7, 0.4), 2.2), G::XS(rings, 1.7), G::X(circ(cx + 0.8, cy, 2.4)),
           G::XS("M52 55 58 49 57 43 64 37 66 30 72 26", 2.2), G::S("M50 19C50 14 50 10 52 6", 2.6),
           G::F(xf(ell(0, 0, 7, 3.3), place(42.5, 10, -25)) + xf(ell(0, 0, 7.5, 3.5), place(60.5, 7.5, 18)))});
  }
  // Корни-щит: щит с деревом, корни уходят вниз, ветви по углам.
  {
    const std::string shield = "M17 10C30 7 40 6 50 6C60 6 70 7 83 10V42C83 62 69 76 50 84C31 76 17 62 17 42Z";
    const std::string roots = taper({45, 78}, {34, 85}, {20, 87}, {5, 91}, 7, 1.2) + taper({47, 81}, {42, 88}, {36, 93}, {30, 98}, 5.5, 1.2) +
                              taper({25, 87}, {21, 90}, {17, 94}, {13, 98}, 2.8, 0.8) + taper({35, 86}, {32, 84}, {28, 80}, {25, 78}, 2.4, 0.8);
    c.add("race-arborians-rootshield", "Корни-щит",
          {G::F(sym(roots) + taper({50, 82}, {50, 88}, {49, 93}, {50, 99}, 5, 1.2)), G::XS(shield, 2.6), G::F(shield),
           G::X(circ(50, 28, 10) + circ(40, 34, 8) + circ(60, 34, 8) + circ(50, 20, 7) + circ(43, 41, 6) + circ(57, 41, 6) + rrect(47, 38, 6, 18, 1.5)),
           G::XS("M50 54 40 66 30 68M50 54 60 66 70 68M50 56V73M45 60 42 72M55 60 58 72", 2.6), G::XS(sym("M20 13.5C31 11 41 10 50 10"), 1.6)});
  }
}

// ---------------------------------------------------------------- безликие
void faceless(Catalog& c) {
  c.group("Безликие", EmblemSection::Races);
  // Маска без лица: гладкая маска с лепкой бровей и носа, за ней — тёмный венец из лучей.
  {
    std::string rays;
    for (int i = 0; i < 32; i++) rays += (i ? "L" : "M") + pt(50, 50, i % 2 ? 17 : (i % 4 == 0 ? 47 : 37), -90 + 11.25 * i);
    const std::string mask = "M50 19C62.5 19 71.5 28.5 71.5 44C71.5 60 62.5 74 50 81C37.5 74 28.5 60 28.5 44C28.5 28.5 37.5 19 50 19Z";
    c.add("race-faceless-mask", "Маска без лица",
          {G::F(rays + "Z"), G::XS(mask, 3.6), G::F(mask), G::XS("M50 23.5C60 23.5 67 31 67 44C67 57 60 69 50 75.5", 1.5),
           G::XS("M38 30C41 26.5 45 25 48 25", 1.4)});
  }
  // Капюшон с пустотой: пустой плащ парит с раскинутыми рукавами, в капюшоне вместо лица — пустота со звездой.
  {
    const std::string hood = "M50 4C61 4 68 12 69 24C70 33 68 41 65 47H35C32 41 30 33 31 24C32 12 39 4 50 4Z";
    const std::string sleeve = "M36 44C27 43 15 39 4 31L5 39C6 47 6 55 4 63L9 60 12 66 16 61 21 65 24 59 30 62C31 56 33 51 36 50Z";
    const std::string robe = "M50 44H36C34 54 30 70 24 90L29 87 33 93 38 88 43 94 50 89H50.5V44Z";
    c.add("race-faceless-hood", "Капюшон с пустотой",
          {G::F(hood + sym(sleeve + robe)), G::X("M50 15C57.5 16.5 61 23 61 31C61 39 56.5 44 50 46C43.5 44 39 39 39 31C39 23 42.5 16.5 50 15Z"),
           G::F(star(50, 31, 8, 1.9, 4, -90) + circ(44.5, 24, 1.1) + circ(56, 38, 1)), G::XS("M34 47C40 52 45 54 50 54C55 54 60 52 66 47", 2),
           G::XS(sym("M30 46.5C22 46 14 43 7 39M42 60 38 86M46.5 62 46 88"), 1.6)});
  }
  // Воронка пустоты: спиральные рукава вокруг тёмного зрачка.
  {
    std::string arms;
    for (int k = 0; k < 6; k++) {
      const double a0 = 60.0 * k;
      arms += ribbon([&](double t) { return polar(50, 50, 8 + 39 * t, a0 + 150 * t); },
                     [](double t) { return 1.2 + 11 * std::sin(kPi * std::pow(t, 0.55)) * (1 - 0.35 * t); });
    }
    c.add("race-faceless-vortex", "Воронка пустоты", {G::F(arms), G::XS(circ(50, 50, 9), 2.4), G::F(circ(50, 50, 4.5))});
  }
  // Зеркало: овальная рама с гребнем, в стекле — силуэт без лица.
  {
    const std::string outer = ell(50, 53, 29, 37), inner = ell(50, 53, 22.5, 30.5);
    const std::string crest = "M50 3C53 8 58 11 63 11C60 14 56 16 50 18C44 16 40 14 37 11C42 11 47 8 50 3Z";
    const std::string foot = "M38 87H62L66 97H34Z";
    c.add("race-faceless-mirror", "Зеркало",
          {G::F(ell(50, 53, 20.5, 28.5)), G::X(circ(50, 45, 8) + "M33 82C33 68 40 59 50 59C60 59 67 68 67 82Z"),
           G::FE(outer + inner), G::F(crest + foot), G::XS(ell(50, 53, 26, 34), 1.6), G::X(circ(50, 11.5, 2))});
  }
}

// Силуэт летучей мыши (как у темы «Вампиры»), центр около (50, 44).
std::string batShape() {
  return sym("M47 42C40 34 32 27 24 24C16 26 9 31 3 38Q11 42 12 52Q19 47 25 57Q31 52 36 64Q40 58 47 59Z" + std::string("M44 34 41.5 20 49 31Z")) +
         circ(50, 38, 8) + ell(50, 52, 7, 13);
}

// Крючковатый коготь на кончике пальца tip: u — направление пальца, side — в какую сторону загнут (±1), len — длина.
std::string hookClaw(Pt tip, Pt u, double side, double len, double w) {
  const double hx = -u.y * side, hy = u.x * side;
  auto q = [&](double a, double b) { return P(tip.x + u.x * a + hx * b, tip.y + u.y * a + hy * b); };
  return "M" + ptS(q(0, -w)) + "Q" + ptS(q(len * 0.8, -w * 0.4)) + " " + ptS(q(len, len * 0.38)) + "Q" + ptS(q(len * 0.45, w * 1.1)) + " " +
         ptS(q(0, w)) + "Z";
}

// ---------------------------------------------------------------- вампиры
void vampires(Catalog& c) {
  c.group("Вампиры", EmblemSection::Races);
  // Вампирский лорд: распахнутый плащ-крылья с зубчатым краем, стоячий воротник, клиновидная чёлка.
  {
    const std::string cloak = "M50 31H42C34 30 22 25 9 16L5 21Q19 33 14 52Q27 57 26 74Q37 75 38 88Q47 87 50 94H50.5V31Z";
    const std::string collar = "M43.5 33L30 8 39.5 14.5 46 29Z";
    const std::string head = ell(50, 23, 6.5, 8.5) + sym("M43.8 21.5 38.5 15.5 44.5 18.5Z");
    c.add("race-vampires-lord", "Вампирский лорд",
          {G::F(sym(cloak + collar)), G::XS(head, 2.4), G::F(head), G::X(sym("M44.8 24.4 48.3 25.6 45.4 26.6Z")),
           G::XS("M43.6 20Q47 16.5 50 20.5Q53 16.5 56.4 20", 1.4), G::XS(sym("M41 37 15 50M43 43 26.5 72M46 49 38 86"), 1.6),
           G::XS("M44.5 33 50 47 55.5 33", 1.8)});
  }
  // Кровавая луна: полный диск, по нижнему краю стекают капли, на диске — летучая мышь.
  {
    auto drip = [](double x, double y0, double y1, double w) {
      const double r = w * 1.15;
      return "M" + n2(x - w, y0) + "C" + n2(x - w, y0 + 5) + " " + n2(x - r, y1 - 7) + " " + n2(x - r, y1 - r) + "A" + n(r) + " " + n(r) +
             " 0 0 0 " + n2(x + r, y1 - r) + "C" + n2(x + r, y1 - 7) + " " + n2(x + w, y0 + 5) + " " + n2(x + w, y0) + "Z";
    };
    c.add("race-vampires-moon", "Кровавая луна",
          {G::F(circ(50, 43, 37) + drip(36.5, 72, 90, 3) + drip(50, 78, 97, 3.4) + drip(63.5, 72, 86, 2.6)),
           G::X(xf(batShape(), Affine::translate(0, -3) * scaleAt(0.62f, 50, 44))),
           G::X(circ(27, 60, 2.6) + circ(73, 26, 2.2) + circ(70, 62, 1.8) + circ(29, 25, 1.6)), G::XS(arc(50, 43, 32.5, 200, 250), 1.8)});
  }
  // Перстень-печать: кольцо, восьмигранный щиток с ободком, на нём летучая мышь.
  {
    std::string oct, oct2;
    for (int i = 0; i < 8; i++) {
      oct += (i ? "L" : "M") + pt(50, 30, 25, -67.5 + 45 * i);
      oct2 += (i ? "L" : "M") + pt(50, 30, 20, -67.5 + 45 * i);
    }
    oct += "Z";
    oct2 += "Z";
    c.add("race-vampires-signet", "Перстень-печать",
          {G::FE(ell(50, 64, 28, 31) + ell(50, 64, 18, 21)), G::XS(sym("M25.5 72C29 77 33 80 38 81.5"), 1.6), G::XS(oct, 3.2), G::F(oct),
           G::XS(oct2, 1.8), G::X(xf(batShape(), Affine::translate(0, -14) * scaleAt(0.4f, 50, 44)))});
  }
  // Клыки в короне: корона, из обода которой свисают клыки, между ними — капля крови.
  {
    const std::string crown = "M20 42L15 17 32 29 50 7 68 29 85 17 80 42Z" + circ(15, 16, 3.6) + circ(50, 6, 3.8) + circ(85, 16, 3.6) +
                              rrect(18, 40, 64, 14, 2.5);
    const std::string fang = "M35 53H45C44.5 66 42.5 78 39.5 90C37 78 35.5 66 35 53Z";
    c.add("race-vampires-crown", "Клыки в короне",
          {G::F(crown + sym(fang)), G::X(ell(50, 47, 4.2, 3.6) + circ(32, 47, 2.6) + circ(68, 47, 2.6) + "M50 19 53 25 50 31 47 25Z"),
           G::XS("M19 41H81", 1.6), G::XS(sym("M38 57C38.5 66 39 74 39.5 82"), 1.4),
           G::F("M50 62C53.5 68 56 72 56 75.5A6 6 0 0 1 44 75.5C44 72 46.5 68 50 62Z")});
  }
}

// ---------------------------------------------------------------- великаны
void giants(Catalog& c) {
  c.group("Великаны", EmblemSection::Races);
  // Великан с дубиной: исполин держит над головой суковатую дубину.
  {
    const std::string half = "M50 36C42 36 34 37 28 40C24 42 22 46 23 51L28 58C32 61 34 64 35 69H50.5V36Z";
    const std::string arm = taper({27, 47}, {23, 38}, {24, 29}, {28.5, 20}, 11, 8.5);
    const std::string leg = taper({42, 74}, {40, 82}, {38, 88}, {36, 93}, 12.5, 10) + ell(33, 94, 8, 3.6);
    const std::string club = taper({5, 16}, {30, 13}, {60, 10}, {94, 12}, 7, 14) + circ(71, 5, 2.4) + circ(85, 5.6, 2.4) + circ(79, 19.5, 2.2);
    const std::string head = circ(50, 28.5, 8) + "M42 30C42 37.5 45 42.5 50 44.5C55 42.5 58 37.5 58 30Z";
    c.add("race-giants-club", "Великан с дубиной",
          {G::F(club), G::XS(sym(circ(29, 15, 6.2)), 2.4), G::F(sym(circ(29, 15, 6.2) + arm + half + leg) + "M35 66H65L67 79 61 76 55 80 50 76 45 80 39 76 33 79Z"),
           G::XS(head, 2.2), G::F(head), G::X(sym(ell(47, 28, 1.3, 1.1))), G::XS("M47 35Q50 37 53 35M50 38V42", 1.3),
           G::XS("M37 47Q44 51 50 48Q56 51 63 47M35 66.5H65", 1.8), G::XS(sym("M26 13.5 32 16.5"), 1.4), G::XS("M20 13C30 11 40 10 46 10", 1.4)});
  }
  // Горный трон: высеченный трон перед тремя вершинами, ступени к подножию.
  {
    const std::string throne = "M36 26L40 19 44 26 50 15 56 26 60 19 64 26V58H36Z" + rrect(27, 46, 9, 16, 2) + rrect(64, 46, 9, 16, 2) +
                               rrect(29, 57, 42, 8, 2) + rrect(33, 64, 34, 12, 1) + rrect(25, 80, 50, 6, 1) + rrect(18, 87, 64, 7, 1);
    c.add("race-giants-throne", "Горный трон",
          {G::F("M2 94L21 50 29 58 50 10 71 58 79 50 98 94Z"), G::XS("M14 66 17 63 20 67 23 64M77 64 80 67 83 63 86 66", 2.2), G::XS(throne, 3),
           G::F(throne), G::X(rrect(41, 31, 18, 22, 2)), G::F("M50 35 54.5 42 50 49 45.5 42Z"), G::XS("M34 70H66M27 86.5H73", 1.4)});
  }
  // Огромный кулак: кулак в наручи обрушивается на землю, разлетаются осколки.
  {
    const std::string fist = rrect(22, 22, 56, 36, 10) + rrect(22, 46, 14.5, 22, 7) + rrect(36, 48, 14.5, 23, 7) + rrect(50, 48, 14.5, 23, 7) +
                             rrect(63.5, 46, 14.5, 22, 7);
    const std::string cuff = rrect(30, 3, 40, 20, 3);
    c.add("race-giants-fist", "Огромный кулак",
          {G::F("M2 86L16 80 30 84 42 78 50 81 58 78 70 84 84 80 98 86V96H2Z" +
                std::string("M9 70 15 63 18 71ZM86 63 93 69 84 72ZM17 56 21 49 25 56ZM76 50 82 54 75 57Z")),
           G::S("M13 40 5 36M87 40 95 36M12 48 3 48M88 48 97 48", 2.6), G::XS(fist + cuff, 3), G::F(fist + cuff),
           G::XS("M36.25 52V67M50 52V69M63.75 52V67", 2.2), G::XS("M30 13H70", 1.6), G::X(circ(36, 8, 1.6) + circ(50, 8, 1.6) + circ(64, 8, 1.6)),
           G::XS("M28 31C34 28 40 28 44 31", 1.6), G::XS("M50 81 46 88 50 93M42 79 36 91M58 79 64 91", 1.8)});
  }
  // Каменная корона: кольцо стоячих камней на тёсаном ободе, самоцвет в средней глыбе.
  {
    auto stone = [](double x, double top, double w, double tilt) {
      return "M" + n2(x - w / 2, 66) + "L" + n2(x - w / 2 + tilt * 0.3, top + w * 0.4) + "Q" + n2(x - w * 0.35 + tilt, top - 1) + " " +
             n2(x + tilt * 0.5, top) + "Q" + n2(x + w * 0.4 + tilt, top + 1) + " " + n2(x + w / 2 + tilt * 0.3, top + w * 0.45) + "L" +
             n2(x + w / 2, 66) + "Z";
    };
    const std::string stones = stone(50, 8, 18, 0) + stone(31, 20, 14, -1.5) + stone(69, 20, 14, 1.5) + stone(15, 33, 11, -2) + stone(85, 33, 11, 2);
    c.add("race-giants-crown", "Каменная корона",
          {G::F(stones), G::XS("M8 62H92L87 86H13Z", 2.6), G::F("M8 62H92L87 86H13Z"),
           G::XS("M10 74H90M28 62V74M50 62V74M72 62V74M39 74V86M61 74V86M19 74V86M81 74V86", 1.8), G::X("M50 25 55.5 33 50 41 44.5 33Z"),
           G::F("M50 28.5 53 33 50 37.5 47 33Z"), G::XS("M33 30 30.5 38 32.5 44M66.5 34 70 40M15.5 42 17 50M84 44 83 52", 1.5)});
  }
}

// ---------------------------------------------------------------- высшие эльфы
void highElves(Catalog& c) {
  c.group("Высшие Эльфы", EmblemSection::Races);
  // Феникс: поднятые крылья, голова с хохолком, огненный хвост.
  {
    std::vector<L> v;
    std::vector<std::string> tail;
    std::string eyes;
    for (int i : {-2, 2, -1, 1, 0}) {
      const double a = 90 + 21 * i, len = 37 - 4 * std::abs(i);
      tail.push_back(feather({50, 60}, polar(50, 60, len, a), 10.5));
      const Pt e = polar(50, 60, len - 7, a);
      eyes += circ(e.x, e.y, 1.9);
    }
    layeredFeathers(v, tail, 2, false);
    v.push_back(G::X(eyes));
    addWing(v, place(43, 50, 20, 0.95f), true);
    const std::string body = ell(50, 52, 8, 14) + "M45 44C45 38 46 35 47 33H53C54 35 55 38 55 44Z";
    const std::string head = circ(50, 29, 6.5) + "M44.5 28 35 32 45 33.5Z" + taper({53, 25}, {56, 18}, {60, 12}, {67, 8}, 4.2, 1) +
                             taper({55.5, 27}, {60, 22}, {66, 19}, {73, 18}, 3.6, 1) + taper({51, 24}, {51, 18}, {53, 13}, {57, 8}, 3.4, 1);
    v.push_back(G::XS(body + head, 2.4));
    v.push_back(G::F(body + head));
    v.push_back(G::X(circ(48.3, 28.2, 1.4)));
    v.push_back(G::XS("M46 50Q50 53 54 50M46.5 56Q50 59 53.5 56", 1.4));
    c.add("race-high-elves-phoenix", "Феникс", v);
  }
  // Высокий шпиль: башня с вогнутым шпилем, две башни пониже, мостики, стрельчатые окна.
  {
    const std::string mainT = rrect(43, 36, 14, 52, 1) + "M41 37C46 31 48.5 19 50 3C51.5 19 54 31 59 37Z" + rrect(40, 34, 20, 4, 2);
    const std::string sideT = rrect(23.5, 54, 11, 34, 1) + "M22 55C26 50 28 40 29 28C30 40 32 50 36 55Z" + rrect(21.5, 52.5, 15, 3.5, 1.7);
    c.add("race-high-elves-spire", "Высокий шпиль",
          {G::F(mainT + sym(sideT) + rrect(12, 84, 76, 12, 1.5)), G::S(sym("M34.5 66Q39 62 43 66"), 2.4),
           G::X(rrect(48.2, 44, 3.6, 10, 1.8) + rrect(48.2, 60, 3.6, 10, 1.8) + sym(rrect(27.8, 62, 2.6, 8, 1.3)) +
                "M44.5 96V90.5C44.5 87.5 47 85 50 85C53 85 55.5 87.5 55.5 90.5V96Z"),
           G::XS("M13 89.5H40M60 89.5H87", 1.4), G::X(circ(50, 24, 2.2)), G::XS(sym("M23 40 29 34"), 1.2)});
  }
  // Эльфийская тиара: изогнутый обод, высокий лист с самоцветом, боковые листья, завитки филиграни.
  {
    const std::string leaf = "M50 4C58 16 62 29 60 43C58 53 54 59 50 63C46 59 42 53 40 43C38 29 42 16 50 4Z";
    const std::string side = xf("M0 -24C5 -16 7 -8 6 0C5 6 3 9 0 11C-3 9 -5 6 -6 0C-7 -8 -5 -16 0 -24Z", place(29, 53, -28));
    c.add("race-high-elves-tiara", "Эльфийская тиара",
          moved({G::F("M8 74Q50 54 92 74L91 83Q50 64 9 83Z" + leaf + sym(side)), G::S(sym("M43 60C37 53 30 57 31 63C32 68 38 67 37 63"), 2.2),
                 G::S(sym("M21 66C16 61 11 64 12 69"), 2), G::X(ell(50, 38, 4.6, 7.6) + sym(xf(ell(0, 0, 2.2, 4), place(29.5, 50, -28)))),
                 G::F(ell(50, 38, 2.4, 4.8)), G::XS("M50 9C51 16 51 24 50 29M50 47C51 52 51 56 50 60", 1.4),
                 G::X(circ(50, 68.5, 2.2) + sym(circ(36, 70.5, 1.6) + circ(23, 74.5, 1.4)))},
                Affine::translate(0, 5)));
  }
  // Магический кристалл: огранённый кристалл с друзой, орбита-кольцо вокруг, искры.
  {
    const std::string cr = "M50 6L62 24V70L50 90 38 70V24Z";
    const std::string side = xf("M0 -18L6 -10V8L0 14-6 8V-10Z", place(31, 74, -24));
    const Affine el = Affine::translate(50, 52) * Affine::scale(1, 0.24f);
    c.add("race-high-elves-crystal", "Магический кристалл",
          {G::S(xf(arc(0, 0, 42, 180, 360), el), 2.6), G::XS(cr + sym(side), 3), G::F(cr + sym(side)),
           G::XS("M50 6V90M38 24 50 32 62 24M38 70 50 62 62 70", 1.6), G::XS(xf(arc(0, 0, 42, 0, 180), el), 6.5),
           G::S(xf(arc(0, 0, 42, 0, 180), el), 2.6), G::F("M24 90Q50 82 76 90L73 97H27Z"),
           G::F(star(17, 22, 6.5, 1.7, 4, -90) + star(84, 16, 5.5, 1.5, 4, -90) + star(86, 80, 4.5, 1.3, 4, -90))});
  }
}

// ---------------------------------------------------------------- гарпии
void harpies(Catalog& c) {
  c.group("Гарпии", EmblemSection::Races);
  // Гарпия в полёте: профиль, развевающиеся волосы, поднятое крыло, лапы с когтями вперёд, хвост веером.
  {
    std::vector<L> v{G::F(taper({21, 15}, {33, 6}, {47, 9}, {58, 20}, 11, 2) + taper({27, 19}, {39, 18}, {47, 26}, {51, 38}, 9, 1.5) +
                          taper({31, 29}, {37, 35}, {38, 43}, {35, 51}, 7, 1.2))};
    layeredFeathers(v, {feather({57, 62}, {88, 67}, 9.5), feather({57, 63}, {87, 77}, 9.5), feather({56, 64}, {80, 86}, 8.5)}, 2, false);
    const Affine mir{-1, 0, 0, 1, 0, 0};
    addWing(v, place(44, 52, -40, 0.95f) * mir, false);
    const std::string body = taper({24, 39}, {32, 46}, {43, 54}, {55, 62}, 11, 10) +
                             "M17 18C20 14 26 13 30 15C34 18 35 23 34 28C33 32 31 35 28 37L27 42H21L22 36C19 35 17 33 16.5 31L14 30.5 15.5 28.5 "
                             "14.5 27 16 25.5C16 23 16 20 17 18Z";
    const std::string legs = xf(ell(0, 0, 6, 9.5), place(54, 67, 40)) + taper({50, 73}, {45, 76}, {41, 79}, {37, 81}, 4.6, 3.4) +
                             hookClaw({37, 81}, {-1, 0.1f}, -1, 9, 1.9) + hookClaw({37.5, 82}, {-0.75f, 0.66f}, -1, 9, 1.9) +
                             taper({57, 74}, {55, 80}, {52, 85}, {49, 89}, 4.2, 3.2) + hookClaw({49, 89}, {-0.8f, 0.6f}, -1, 8.5, 1.8) +
                             hookClaw({49.5, 90}, {-0.35f, 0.94f}, -1, 8, 1.8);
    v.push_back(G::XS(body + legs, 2.4));
    v.push_back(G::F(body + legs));
    v.push_back(G::X(ell(21, 25, 1.7, 1.1)));
    v.push_back(G::XS("M19 21.5Q21.5 20.5 24 21.5", 1.1));
    c.add("race-harpies-flight", "Гарпия в полёте", v);
  }
  // Когтистая лапа: оперённое бедро, чешуйчатая цевка, три пальца с крючковатыми когтями.
  {
    const std::string toes = taper({46, 47}, {38, 54}, {29, 60}, {20, 67}, 12, 8) + taper({50, 49}, {50, 57}, {50, 65}, {50, 73}, 12, 8) +
                             taper({54, 47}, {62, 54}, {71, 60}, {80, 67}, 12, 8) + circ(33, 57, 5.6) + circ(67, 57, 5.6) + circ(50, 62, 5.6);
    const std::string claws = hookClaw({20, 67}, {-0.71f, 0.71f}, -1, 19, 4) + hookClaw({50, 73}, {0, 1}, -1, 19, 4) +
                              hookClaw({80, 67}, {0.71f, 0.71f}, 1, 19, 4);
    std::vector<L> v{G::F(rrect(42.5, 24, 15, 28, 5) + toes)};
    layeredFeathers(v, {feather({37, 4}, {33, 27}, 10), feather({63, 4}, {67, 27}, 10), feather({43, 4}, {41, 31}, 10.5), feather({57, 4}, {59, 31}, 10.5),
                        feather({50, 4}, {50, 35}, 11)},
                    1.8f, false);
    v.push_back(G::F(rrect(31, 2, 38, 8, 2.5)));
    v.push_back(G::XS("M32 10H68", 1.6));
    v.push_back(G::XS(claws, 2.4));
    v.push_back(G::F(claws));
    v.push_back(G::XS("M44 40Q50 43 56 40M44 46Q50 49 56 46", 1.5));
    v.push_back(G::XS("M29 52 36 61M44 60H56M71 52 64 61M23 61 28 66M45 67H55M77 61 72 66", 1.6));
    c.add("race-harpies-talon", "Когтистая лапа", v);
  }
  // Перо-кинжал: клинок-перо с бородками и надрывом, изогнутая гарда, обмотанная рукоять.
  {
    std::string barbs;
    for (int i = 0; i < 6; i++) {
      const double y = 56 - 8 * i;
      barbs += "M50 " + n(y) + "L" + n2(41.5 + i * 0.6, y - 6) + "M50 " + n(y - 3.5) + "L" + n2(58.5 - i * 0.6, y - 9.5);
    }
    const std::string hilt = "M27 62Q50 72 73 62L71 72Q50 81 29 72Z" + rrect(44.5, 74, 11, 14, 2.4) + circ(50, 92, 6);
    c.add("race-harpies-dagger", "Перо-кинжал",
          moved({G::F(feather({50, 68}, {50, 3}, 28)), G::XS(barbs, 1.5), G::XS("M50 8V64", 1.7), G::X("M36 30 46.5 33.5 37 37.5Z"),
                 G::XS(hilt, 2.6), G::F(hilt), G::XS("M44.5 78.5H55.5M44.5 83H55.5", 1.4), G::X(circ(50, 92, 2.2))},
                rotAt(38, 50, 50) * scaleAt(1.14f, 50, 50)));
  }
  // Гнездо на скале: утёс, гнездо из прутьев, три яйца.
  c.add("race-harpies-nest", "Гнездо на скале",
        moved({G::F("M8 98L16 80 24 76 28 66 38 68 46 60 56 64 64 60 72 68 80 72 92 98Z"), G::XS("M30 76 34 86 30 94M62 72 66 82M48 78 52 90", 1.8),
               G::F(ell(37, 40, 7, 9.5) + ell(63, 40, 7, 9.5)), G::XS(ell(50, 36, 7.5, 10.5), 2.4), G::F(ell(50, 36, 7.5, 10.5)),
               G::X(circ(35, 37, 1.3) + circ(52, 31, 1.4) + circ(48, 40, 1) + circ(65, 37, 1.1)),
               G::XS("M16 46Q50 52 84 46Q80 62 64 66H36Q20 62 16 46Z", 2.6), G::F("M16 46Q50 52 84 46Q80 62 64 66H36Q20 62 16 46Z"),
               G::XS("M24 52 33 64M34 53 43 65M44 53 53 65M54 53 63 65M64 52 72 62M30 63 40 52M40 64 50 53M50 64 60 53M60 64 70 52", 1.5),
               G::S("M16 48 7 43M84 48 93 42M20 57 10 60M80 57 90 61", 2.2)},
              Affine::translate(0, -11)));
}

// ---------------------------------------------------------------- гноллы
void gnolls(Catalog& c) {
  c.group("Гноллы", EmblemSection::Races);
  // Голова гиены: округлые уши, грива-гребень, вытянутая морда, пятна.
  {
    const std::string half = "M50 15C42 15 35 17 31 22L27 30C25 38 26 46 30 53C33 58 35 63 36 69C37 77 40 84 46 88L50 89H50.5V15Z" +
                             std::string("M28 41 21 44 28.5 47ZM29 50 23 54 30.5 55Z");
    const std::string ear = xf(ell(0, 0, 9, 12.5), place(24, 17, -35));
    c.add("race-gnolls-head", "Голова гиены",
          {G::F(sym(half + ear) + "M40 19 42 6 46 14 50 2 54 14 58 6 60 19Z"), G::X(sym(xf(ell(0, 0, 4.6, 7.6), place(24.5, 17.5, -35)))),
           G::X(sym("M35 37 46 40.5 44 44.5 37 42Z")), G::F(sym(circ(41.8, 41.6, 1.3))), G::XS(sym("M33.5 33.5 46.5 37.5"), 1.8),
           G::XS(sym("M38.5 64C40 74 44 81 50 83"), 1.6), G::XS(ell(50, 72, 6.5, 4.4), 1.6), G::XS(sym("M50 76.5V80M50 80Q46 83.5 42.5 81"), 1.4),
           G::X(sym(circ(31, 47.5, 1.6) + circ(34.5, 53.5, 1.5) + circ(40, 27, 1.4) + circ(44.5, 31, 1.2)))});
  }
  // Костяной цеп: рукоять из бедренной кости, цепь, шипастое било.
  {
    const Pt a{17, 89}, b{44, 56};
    const float ux = 0.633f, uy = -0.774f, nx = 0.774f, ny = 0.633f;
    const std::string bone = taper(a, lerpP(a, b, 0.33), lerpP(a, b, 0.66), b, 9, 8) + circ(a.x + nx * 4 - ux * 2, a.y + ny * 4 - uy * 2, 5.4) +
                             circ(a.x - nx * 4 - ux * 2, a.y - ny * 4 - uy * 2, 5.4) + circ(b.x + nx * 3.8 + ux * 2, b.y + ny * 3.8 + uy * 2, 5) +
                             circ(b.x - nx * 3.8 + ux * 2, b.y - ny * 3.8 + uy * 2, 5);
    std::string spikes;
    for (int k = 0; k < 9; k++) {
      const double ang = -90 + 40 * k;
      spikes += "M" + pt(69, 29, 13, ang - 13) + "L" + pt(69, 29, 23, ang) + "L" + pt(69, 29, 13, ang + 13) + "Z";
    }
    std::string rings, bars;
    for (int i = 0; i < 3; i++) {
      const Pt p = lerpP({50, 49}, {60, 38}, i / 2.0);
      (i % 2 ? bars : rings) += xf(i % 2 ? rrect(-4.5, -1.3, 9, 2.6, 1.3) : ell(0, 0, 4.4, 2.6), place(p.x, p.y, -47));
    }
    c.add("race-gnolls-flail", "Костяной цеп",
          {G::F(bone + circ(69, 29, 15) + spikes), G::S(rings, 2.2), G::F(bars),
           G::XS("M62 21 68 27 64 33M74 34 77 38", 1.8), G::XS(xf("M-12 -1.6 10 -2.2M-11 2 9 1.6", place(30.5f, 72.5f, -50.7f)), 1.3)});
  }
  // Хохочущая пасть: гиена в профиль с раскрытой пастью, клыки, острое ухо, грива на загривке.
  {
    const std::string head = "M11 45C11 39 17 35 25 33C33 31 39 27 45 22C51 17 57 15 64 15C74 15 82 21 84 31C86 39 84 47 82 53C86 65 90 80 92 96H62"
                             "C62 88 61 80 59 74C50 78 38 82 28 82C23 82 20 80 20 77C31 72 42 66 50 60C40 56 30 54 16 53C13 52 11 49 11 45Z";
    std::string teeth;
    for (int i = 0; i < 5; i++) {
      const double x = 21 + 6 * i, y = 54 + 1.4 * i, h = i == 0 ? 8 : 5.5;
      teeth += "M" + n2(x - 2.4, y - 1) + "L" + n2(x + 2.4, y) + "L" + n2(x + 0.3, y + h) + "Z";
      const double lx = 26 + 5 * i, ly = 75.5 - 2.5 * i, lh = i == 0 ? 7.5 : 5.5;
      teeth += "M" + n2(lx - 2.2, ly + 1) + "L" + n2(lx + 2.2, ly) + "L" + n2(lx + 0.2, ly - lh) + "Z";
    }
    std::string mane;
    for (int i = 0; i < 6; i++) {
      const double y = 26 + 9.5 * i, x = 82.5 + 1.7 * i;
      mane += "M" + n2(x - 1, y - 2.5) + "Q" + n2(x + 7, y - 2) + " " + n2(x + 8, y + 2) + "Q" + n2(x + 4, y + 3) + " " + n2(x + 0.5, y + 6.5) + "Z";
    }
    c.add("race-gnolls-laugh", "Хохочущая пасть",
          {G::F(head + teeth + xf(ell(0, 0, 9, 12), place(69, 15, 20)) + mane), G::X(xf(ell(0, 0, 4.4, 7.4), place(69.5, 16, 20))),
           G::X("M43 30 53 29 51 33.5 45 33Z"), G::XS(ell(15, 44.5, 4.4, 3.6), 1.4), G::XS("M40 26C44 24 49 24 53 25", 1.6),
           G::X(circ(70, 58, 2) + circ(77, 70, 1.7) + circ(69, 79, 2) + circ(79, 86, 1.6) + circ(66, 45, 1.6) + circ(38, 38, 1.4) + circ(32, 42, 1.2)),
           G::XS("M50 60 57 63", 1.6)});
  }
  // Тотем из черепов: шест, череп гиены, человеческий череп на скрещённых костях, рогатый череп зверя с подвесками.
  {
    const std::string bone = rrect(-17, -2.6, 34, 5.2, 2.6) + circ(-17, -2.8, 3.4) + circ(-17, 2.8, 3.4) + circ(17, -2.8, 3.4) + circ(17, 2.8, 3.4);
    const std::string top = sym("M50 4C45 4 41 7 40 12L38.5 20C38.5 24 40 27 43 29L45 35H50.5V4Z" + xf(ell(0, 0, 3.6, 6.4), place(39.5, 7.5, -35)));
    const std::string mid = sym("M50 39C42 39 36 44 36 51C36 56 38 59 41 61V66H50.5V39Z");
    const std::string low = sym("M50 71C44.5 71 40.5 74 40.5 79L42.5 87 46 93H50.5V71Z" + taper({42, 75}, {31, 68}, {19, 72}, {17, 84}, 6.5, 2));
    c.add("race-gnolls-totem", "Тотем из черепов",
          {G::F(rrect(47, 8, 6, 90, 2) + xf(bone, place(50, 53, 32)) + xf(bone, place(50, 53, -32))), G::S(sym("M17 84V90"), 1.6),
           G::F(sym(xf(ell(0, 0, 2.6, 5.5), place(17, 94, 0)))), G::XS(top + mid + low, 2.6), G::F(top + mid + low),
           G::X(sym(ell(44.5, 16, 2.1, 3) + ell(44, 51, 3.6, 4) + ell(45, 80, 2.4, 2.8)) + "M48.2 29H51.8L50 32Z" + "M50 55 52 59H48Z"),
           G::XS("M45 63V66M50 63V66M55 63V66M43 61.5H57", 1.3), G::XS(sym("M28 70 30 75M22 72 25 77"), 1.4)});
  }
}

// ---------------------------------------------------------------- гномы
void dwarvesRace(Catalog& c) {
  c.group("Гномы", EmblemSection::Races);
  // Гномий король: корона, густые брови, усы и борода, заплетённая в две косы с кольцами.
  {
    const std::string head = sym("M50 28H31C30 34 30 40 30 44C27 48 25 54 25 62C25 72 29 80 34 84L36 96H47L48 86H50.5V28Z");
    const std::string crown = "M29 25L27 7 38 15 50 3 62 15 73 7 71 25Z" + rrect(27, 21, 46, 8, 2);
    c.add("race-dwarves-king", "Гномий король",
          {G::F(head), G::XS(crown, 2.4), G::F(crown), G::X(circ(50, 25, 2) + sym(circ(37.5, 25, 1.6)) + "M50 9 52.5 13.5 50 18 47.5 13.5Z"),
           G::X(sym(ell(42.5, 38.5, 2.5, 1.7))), G::XS(sym("M36.5 34.5Q41 32 47 34.5"), 2.2), G::XS(sym("M47.5 38C46.5 42 45.5 46 47.5 49.5"), 1.5),
           G::XS(sym("M50 52.5Q44 50 35 57.5"), 2), G::X("M46 56.5Q50 58.5 54 56.5Q50 61 46 56.5Z"),
           G::XS(sym("M41 63C41 70 42 76 43 82M34 64C33 71 35 77 38 81"), 1.5), G::XS("M36.5 88H47M53 88H63.5M37 92.5H46.5M53.5 92.5H63", 1.6)});
  }
  // Рунный молот: тяжёлый боёк с рунами, окованная рукоять с обмоткой.
  c.add("race-dwarves-hammer", "Рунный молот",
        {G::F("M12 10H88L92 15V33L88 38H12L8 33V15Z" + std::string("M43 10 50 2 57 10Z") + rrect(42, 37, 16, 7, 1.5) + rrect(45, 43, 10, 44, 2.5) +
              "M43 86H57L59 92 50 98 41 92Z"),
         G::XS(rrect(35, 15, 30, 18, 2) + "M24 11V37M76 11V37", 1.6),
         G::XS("M41 18 44.5 21.5 41 25 37.5 21.5ZM41 25 37.5 30M41 25 44.5 30M48.5 18V30M48.5 20.5 52 24 48.5 27.5M59 18V30M59 23 55.5 19M59 23 62.5 19", 1.7),
         G::XS("M12.5 20H20M12.5 28H20M80 20H87.5M80 28H87.5", 1.5), G::XS("M45 52 55 56M45 60 55 64M45 68 55 72M45 76 55 80", 1.5), G::XS("M42 40.5H58", 1.3)});
  // Щит с рунами: круглый щит, руны по ободу, доски, умбон, заклёпки.
  {
    std::string runes;
    const char* glyphs[] = {"M0 -3V3M0 -3 2.4 -0.8", "M-1.6 -3V3M1.6 -3V3M-1.6 0 1.6 0", "M0 -3V3M-2 -1 0 1 2 -1", "M-2 3 0 -3 2 3"};
    for (int k = 0; k < 12; k++) {
      const Pt p = polar(50, 50, 41.5, -90 + 30 * k);
      runes += xf(glyphs[k % 4], place(p.x, p.y, float(30 * k)));
    }
    std::string rivets;
    for (int k = 0; k < 8; k++) {
      const Pt p = polar(50, 50, 33.5, -67.5 + 45 * k);
      rivets += circ(p.x, p.y, 1.4);
    }
    c.add("race-dwarves-shield", "Щит с рунами",
          {G::F(circ(50, 50, 46)), G::XS(circ(50, 50, 37), 2.2), G::XS(runes, 1.6), G::XS("M31 17V83M69 17V83M50 14V36M50 64V86", 1.8),
           G::X(rivets), G::XS(circ(50, 50, 12.5), 2.4), G::X(circ(46.5, 46.5, 2.2))});
  }
  // Кружка эля: деревянная кружка с обручами, ручка, шапка пены с подтёками.
  c.add("race-dwarves-ale", "Кружка эля",
        moved({G::S("M66 44H75C81 44 85 48 85 54V68C85 74 81 78 75 78H66", 8), G::F(rrect(20, 30, 46, 62, 5)),
               G::XS("M20 44H66M20 81H66", 2.4), G::XS("M31.5 48V77M43 48V78M54.5 48V77", 1.6),
               G::X(circ(26, 44, 1.2) + circ(60, 44, 1.2) + circ(26, 81, 1.2) + circ(60, 81, 1.2)),
               G::XS(circ(26, 30, 8) + circ(38, 25, 9) + circ(52, 26, 9) + circ(63, 31, 7.5) + ell(22, 40, 3.4, 6.5) + ell(58, 38, 3, 5), 2.2),
               G::F(circ(26, 30, 8) + circ(38, 25, 9) + circ(52, 26, 9) + circ(63, 31, 7.5) + ell(22, 40, 3.4, 6.5) + ell(58, 38, 3, 5)),
               G::X(circ(36, 21, 1.6) + circ(49, 23, 1.3) + circ(60, 28, 1.2) + circ(28, 27, 1.1))},
              Affine::translate(-3.5f, -4) * scaleAt(1.1f, 53.5f, 54)));
}

// ---------------------------------------------------------------- гоблины
void goblins(Catalog& c) {
  c.group("Гоблины", EmblemSection::Races);
  // Голова гоблина: длинные острые уши (одно надорвано), крючковатый нос, ухмылка с зубами.
  {
    const std::string half = "M50 18C40 18 33 24 32 34L30 46C30 58 36 70 44 76L50 78H50.5V18Z";
    const std::string ear = "M34 40C24 34 13 30 2 30C8 36 14 42 20 46C24 49 28 50 33 50Z";
    const std::string nose = "M50 39C54 43 58 50 57 56C56.5 60 52 61 49 58C47 55 47 47 50 39Z";
    c.add("race-goblins-head", "Голова гоблина",
          moved({G::F(sym(half + ear)), G::X("M85 35.5 89 33 88 38Z"), G::X(sym(ell(41, 43, 4.2, 3))), G::F(sym(circ(41.6, 43.2, 1.5))),
                 G::XS(sym("M35 36.5 46 40"), 2), G::XS(nose, 2), G::F(nose), G::X("M36 63Q50 74 64 63Q50 68 36 63Z"),
                 G::F("M41 65.5 43 69 45 66.6ZM55 66.6 57 69 59 65.5Z"), G::XS(sym("M30 41 22 38"), 1.4),
                 G::S("M48 19C46 13 43 9 38 7M52 19C54 14 57 11 61 10", 2)},
                Affine::translate(0, 6)));
  }
  // Мешок золота: завязанный мешок с монетой-клеймом, рассыпанные монеты.
  c.add("race-goblins-sack", "Мешок золота",
        {G::F("M50 30C38 30 22 46 20 66C18 84 30 94 50 94C70 94 82 84 80 66C78 46 62 30 50 30Z" +
              std::string("M42 25C37 19 37 12 41 8C45 13 48 17 50 20C52 17 55 13 59 8C63 12 63 19 58 25Z") + rrect(41, 24, 18, 8, 3)),
         G::XS("M41 32H59", 2.4), G::S("M57 32C63 35 66 40 63 45", 2), G::X(circ(50, 64, 13)), G::F(circ(50, 64, 10.5)), G::X("M50 57 55 64 50 71 45 64Z"),
         G::XS(ell(12, 90, 8, 3.6) + ell(88, 91, 8, 3.6) + ell(87, 84, 7, 3), 2), G::F(ell(12, 90, 8, 3.6) + ell(88, 91, 8, 3.6) + ell(87, 84, 7, 3)),
         G::XS("M30 46C27 52 26 58 26 64", 1.6)});
  // Бомба с фитилём: чугунный шар с черепом, колпачок, горящий фитиль с искрой.
  c.add("race-goblins-bomb", "Бомба с фитилём",
        {G::F(circ(44, 60, 30) + xf(rrect(-8, -7, 16, 14, 2), place(64, 35, 40))), G::S("M68 29C71 21 77 17 83 19", 3.2),
         G::F(star(86, 14, 10, 3.4, 8, 0)), G::XS(arc(44, 60, 23, 198, 250), 3.4), G::X(circ(44, 59, 9.5) + rrect(38.5, 63, 11, 9, 2.5)),
         G::F(circ(40.3, 58.5, 2.7) + circ(47.7, 58.5, 2.7)), G::XS("M41.5 66V72M44 66V72M46.5 66V72", 1.2),
         G::XS(xf("M-8 -2H8", place(64, 35, 40)), 1.6)});
  // Кривой нож: зазубренный изломанный клинок, обмотанная рукоять, навершие-кольцо.
  c.add("race-goblins-knife", "Кривой нож",
        moved({G::F("M41 59L39 43 44 37 40 26 46 13 53 3 59 12 56.5 22 61 30 57.5 40 60 48 59 59Z" + rrect(31, 57, 38, 8, 4) + rrect(44, 64, 12, 21, 3) +
                    circ(50, 90, 6.5) + "M31 57 25 53 28 63ZM69 57 75 53 72 63Z"),
               G::X("M59.5 23 55 25.5 58.5 28ZM40 47.5 44 50 40 52.5Z" + circ(51, 41, 2.4) + circ(50, 90, 2.6)), G::XS("M52 9 48 22 51 34 49 52", 1.5),
               G::XS("M44 69.5 56 73.5M44 75.5 56 79.5M44 81.5 56 85", 1.6)},
              rotAt(36, 50, 50) * scaleAt(1.12f, 50, 50)));
}

// ---------------------------------------------------------------- демоны
void demonsRace(Catalog& c) {
  c.group("Демоны", EmblemSection::Races);
  // Рогатая корона: обод с самоцветами, вместо зубцов — изогнутые рога.
  {
    const std::string horns = taper({50, 64}, {49, 44}, {46, 24}, {50, 3}, 12, 1.5) +
                              sym(taper({37, 64}, {35, 48}, {27, 34}, {25, 14}, 10, 1.4) + taper({25, 66}, {18, 57}, {10, 48}, {6, 30}, 9, 1.2));
    c.add("race-demons-crown", "Рогатая корона",
          {G::F(horns), G::XS("M46 48H54M46.5 34H53.5M47 22H52.5" + sym("M31.5 50 38 48.5M28 38 33 36M14.5 54 20 52.5M10.5 44 14.5 43"), 1.6),
           G::XS(rrect(16, 62, 68, 18, 3), 2.6), G::F(rrect(16, 62, 68, 18, 3) + "M20 79 25 88 30 79ZM45 79 50 89 55 79ZM70 79 75 88 80 79Z"),
           G::X(ell(50, 71, 5.5, 4.4) + sym(circ(33, 71, 2.8))), G::F("M50 67.5C52 70 52.5 72 51.6 74A2 2 0 0 1 48.4 74C47.5 72 48 70 50 67.5Z"),
           G::XS("M18 66H82", 1.4)});
  }
  // Пентакль ада: круглая печать с перевёрнутой звездой, охваченная пламенем.
  {
    std::string flames, star5;
    for (int k = 0; k < 14; k++) {
      const double a = -90 + 360.0 / 14 * k, len = 6 + 8 * std::max(0.0, -std::sin(a * kPi / 180)) + (k % 2 ? 0 : 2);
      flames += taper(polar(50, 54, 32, a - 9), polar(50, 54, 36 + len * 0.4, a - 4), polar(50, 54, 36 + len * 0.8, a + 2), polar(50, 54, 35 + len, a + 8),
                      10, 1);
    }
    for (int i = 0; i < 5; i++) star5 += (i ? "L" : "M") + pt(50, 54, 27, 90 + 144 * i);
    c.add("race-demons-pentacle", "Пентакль ада",
          {G::F(flames + circ(50, 54, 34)), G::XS(circ(50, 54, 30), 2), G::XS(star5 + "Z", 2.8),
           G::X(sym(circ(30, 40, 1.6) + circ(26, 60, 1.6)) + circ(50, 84.5, 1.6))});
  }
  // Крылатые рога: перепончатые крылья, изогнутые рога-лира, между ними — огненное око.
  {
    const std::string wing = "M40 52C32 40 22 33 9 30C5 36 3 43 2 51Q10 53 12 61Q18 57 22 65Q28 61 32 69Q37 63 43 63Z";
    const std::string horn = taper({45, 56}, {33, 45}, {27, 28}, {37, 8}, 11, 1.4);
    c.add("race-demons-wings", "Крылатые рога",
          {G::F(sym(wing)), G::XS(sym("M40 54 9 32M41 57 12 59M42 60 22 63"), 1.6), G::XS(sym(horn), 2.6), G::F(sym(horn)),
           G::XS(sym("M33.5 46.5 39 43M29 37 35 36M29.5 27 35 29"), 1.6), G::XS(circ(50, 64, 11), 2.6), G::F(circ(50, 64, 11) + "M44 74 50 92 56 74Z"),
           G::X("M50 56C53 60 53 68 50 72C47 68 47 60 50 56Z")});
  }
  // Адский клинок: волнистый клинок, гарда — рогатый череп, шип на навершии.
  {
    const std::string blade = ribbon([](double t) { return P(50, 62 - 59 * t); },
                                     [](double t) { const double w = 17 + 3.8 * std::sin(t * 6 * kPi); return t < 0.8 ? w : w * (1 - t) / 0.2; });
    const std::string guard = circ(50, 68, 9.5) + sym(taper({44, 66}, {31, 65}, {20, 57}, {14, 41}, 10, 1.4));
    c.add("race-demons-blade", "Адский клинок",
          {G::F(blade), G::XS("M50 10V58", 1.8), G::XS(guard, 2.4), G::F(guard + rrect(45.5, 75, 9, 13, 2.2) + "M50 86 57 92 50 99 43 92Z"),
           G::X(sym(ell(46.6, 67.5, 2.1, 2.5)) + "M50 71 51.5 74H48.5Z"), G::XS("M45.5 79.5H54.5M45.5 83.5H54.5", 1.4),
           G::XS(sym("M30 64.5 31.5 60.5M23 57 26 54"), 1.5)});
  }
}

// ---------------------------------------------------------------- дриады
void dryads(Catalog& c) {
  c.group("Дриады", EmblemSection::Races);
  // Дева с волосами-листьями: лик с сомкнутыми веками, венец и пряди из листьев, плечи.
  {
    std::vector<L> v;
    std::vector<std::string> leaves;
    std::string ribs;
    for (int k = 0; k <= 10; k++) {
      const double a = -200 + 22 * k, len = 35 - std::abs(k - 5) * 0.8;
      leaves.push_back(feather(polar(50, 42, 9, a), polar(50, 42, len, a), 10.5));
      ribs += "M" + pt(50, 42, 17, a) + "L" + pt(50, 42, len - 5, a);
    }
    layeredFeathers(v, leaves, 1.8f, false);
    v.push_back(G::XS(ribs, 1.3));
    std::vector<std::string> locks = {feather({39, 52}, {21, 70}, 10), feather({39, 58}, {25, 85}, 10), feather({41, 63}, {34, 95}, 9)};
    layeredFeathers(v, locks, 1.8f, true);
    const std::string body = ell(50, 44, 11, 14.5) + rrect(45, 54, 10, 14, 2) + "M27 96C29 83 38 75 50 73C62 75 71 83 73 96Z";
    v.push_back(G::XS(body, 2.6));
    v.push_back(G::F(body));
    v.push_back(G::XS(sym("M41.5 43Q44.5 45.5 47.5 43"), 1.3));
    v.push_back(G::X(ell(50, 52.5, 2.6, 1.2)));
    v.push_back(G::XS("M36 85Q50 92 64 85", 1.6));
    c.add("race-dryads-maiden", "Дева с волосами-листьями", v);
  }
  // Цветок: шесть лепестков с прожилками, сердцевина с тычинками, стебель и два листа.
  {
    std::vector<L> v{G::S("M50 56C50 70 48 82 50 97", 4)};
    layeredFeathers(v, {feather({49, 79}, {19, 64}, 13), feather({50, 87}, {81, 72}, 13)}, 2, false);
    v.push_back(G::XS("M47 78 24 66M52 86 76 74", 1.4));
    std::vector<std::string> petals;
    std::string veins;
    for (int k = 0; k < 6; k++) {
      const double a = -90 + 60 * k;
      const Pt p = polar(50, 38, 16, a);
      petals.push_back(xf("M0 -16C7 -14 10 -6 9 2C8 9 4 14 0 16C-4 14 -8 9 -9 2C-10 -6 -7 -14 0 -16Z", place(p.x, p.y, float(a + 90))));
      veins += "M" + pt(50, 38, 11, a) + "L" + pt(50, 38, 26, a);
    }
    layeredFeathers(v, petals, 2.2f, false);
    v.push_back(G::XS(veins, 1.4));
    v.push_back(G::XS(circ(50, 38, 8), 2.6));
    v.push_back(G::F(circ(50, 38, 8)));
    std::string dots;
    for (int k = 0; k < 6; k++) dots += circ(polar(50, 38, 4.6, 30 + 60 * k).x, polar(50, 38, 4.6, 30 + 60 * k).y, 1.1);
    v.push_back(G::X(dots + circ(50, 38, 1.3)));
    c.add("race-dryads-flower", "Цветок", v);
  }
  // Лоза-кольцо: два переплетённых стебля, листья плюща, усики, цветок наверху.
  {
    const std::string ivy = "M0 0C-6 -3 -10 -8 -9.5 -12.5C-9 -16.5 -4 -17.5 -1.5 -14.5L0 -17 1.5 -14.5C4 -17.5 9 -16.5 9.5 -12.5C10 -8 6 -3 0 0Z";
    std::string leaves, ribs, curls;
    for (int k = 0; k < 7; k++) {
      const double a = -90 + 45 * (k + 1);
      const Pt p = polar(50, 51, 30, a);
      const Affine lm = place(p.x, p.y, float(a + 90 + (k % 2 ? 35 : -35)), 0.86f);
      leaves += xf(ivy, lm);
      ribs += xf("M0 -3V-12.5", lm);
      const Pt q = polar(50, 51, 21.5, a + 22.5);
      const double a0 = a + 22.5;
      curls += ribbon([&](double t) { return polar(q.x, q.y, 4 * (1 - 0.7 * t), a0 + 420 * t); }, [](double) { return 1.7; }, 30);
    }
    std::string flower;
    for (int k = 0; k < 5; k++) flower += xf(ell(0, -5, 3.6, 5.4), place(50, 14, float(72 * k)));
    c.add("race-dryads-vine", "Лоза-кольцо",
          {G::S(wobble(50, 51, 30, 1.6, 9, 0) + wobble(50, 51, 30, 1.6, 9, kPi), 2.8), G::F(curls), G::XS(leaves + circ(50, 14, 10), 1.6),
           G::F(leaves + flower), G::XS(ribs, 1.3), G::X(circ(50, 14, 2.2))});
  }
  // Олень в листве: голова в профиль, рога-ветви с листьями, лиственная ветвь.
  {
    std::vector<L> v{G::S("M58 33C60 24 64 17 72 12M62 23C58 19 55 15 54 9M67 16C68 12 68 9 66 6M53 33C49 26 45 21 39 17M47 23C43 22 39 23 35 25", 3.8)};
    layeredFeathers(v, {feather({72, 12}, {84, 8}, 7.5), feather({72, 12}, {76, 3}, 7), feather({54, 9}, {48, 3}, 7), feather({39, 17}, {30, 11}, 7.5),
                        feather({35, 25}, {25, 25}, 7)},
                    1.4f, false);
    const std::string head = "M14 58C14 54 17 51 21 50L36 42C40 37 46 34 52 34C58 34 63 37 65 42L68 50C72 60 78 74 82 96H54C54 88 52 82 48 76C42 72 34 68 26 66"
                             "C20 65 14 63 14 58Z";
    const std::string ear = xf(ell(0, 0, 4.2, 10), place(71, 39, 58));
    v.push_back(G::XS(head + ear, 2.4));
    v.push_back(G::F(head + ear));
    v.push_back(G::X(xf(ell(0, 0, 2.6, 1.7), place(43, 46, -10)) + ell(17, 55.5, 1.4, 1)));
    v.push_back(G::XS("M15 61Q22 64 29 63M47 76C54 72 60 72 66 76", 1.4));
    v.push_back(G::S("M3 97C9 89 17 81 30 77", 2.6));
    layeredFeathers(v, {feather({9, 89}, {3, 78}, 7), feather({15, 84}, {11, 72}, 7.5), feather({22, 80}, {22, 68}, 7.5), feather({13, 87}, {25, 92}, 7),
                        feather({21, 82}, {33, 85}, 7)},
                    1.4f, false);
    c.add("race-dryads-deer", "Олень в листве", moved(v, Affine::translate(7, 0)));
  }
}

// ---------------------------------------------------------------- ибисианцы
void ibisians(Catalog& c) {
  c.group("Ибисианцы", EmblemSection::Races);
  // Голова ибиса: изогнутый клюв, полосатый немес, лунный диск с полумесяцем.
  c.add("race-ibisians-head", "Голова ибиса",
        {G::F(circ(58, 10, 7.5) + "M45 13A13 13 0 0 0 71 13A13 9.5 0 0 1 45 13Z"),
         G::F("M46 26C52 20 64 20 70 26C76 32 78 44 78 56L84 94H54L56 74C56 64 54 56 50 48Z" + std::string("M38 84C52 90 70 92 92 87L93 95C70 99 50 97 36 92Z")),
         G::XS("M58 58H80M57.5 66H81M57 74H82M56 82H83M44 86C54 91 70 93 91 89", 1.6),
         G::XS(ell(52, 36, 11, 9.5) + taper({44, 40}, {32, 46}, {20, 56}, {8, 74}, 7.5, 1.4), 2.6),
         G::F(ell(52, 36, 11, 9.5) + taper({44, 40}, {32, 46}, {20, 56}, {8, 74}, 7.5, 1.4)), G::X("M46 33.5 53 33.5 51.5 36.5 47 36Z"),
         G::XS("M53 35.5 60 36.5", 1.3)});
  // Крылатый анкх: анкх с гравировкой, от перекладины — раскинутые крылья.
  {
    std::vector<L> v;
    layeredFeathers(v, {feather({35, 47}, {4, 33}, 9.5), feather({35, 49}, {3, 45}, 9.5), feather({35, 51}, {6, 57}, 9), feather({36, 53}, {13, 67}, 8.5)}, 2,
                    true);
    const std::string ankh = "M28 40 50 43.5 72 40 73 52 50 49 27 52Z" + std::string("M45.5 50H54.5L60 95H40Z");
    v.push_back(G::XS(ankh + ell(50, 22, 14, 17.5), 2.6));
    v.push_back(G::F(ankh));
    v.push_back(G::FE(ell(50, 22, 14, 17.5) + ell(50, 22, 7, 11)));
    v.push_back(G::XS("M50 55V89M32 46 50 48.5 68 46", 1.5));
    c.add("race-ibisians-ankh", "Крылатый анкх", v);
  }
  // Свиток и перо: развёрнутый папирус с иероглифами, наискось — тростниковое перо.
  {
    const std::string quill = feather({28, 90}, {86, 4}, 13);
    c.add("race-ibisians-scroll", "Свиток и перо",
          {G::F(rrect(18, 30, 64, 40, 1) + rrect(7, 25, 14, 50, 7) + rrect(79, 25, 14, 50, 7)), G::XS("M21 28V72M79 28V72", 1.6),
           G::XS("M26 36V44M26 36 30 38M33 44 36 38 39 44M44 37H52M44 41H52M25 50 28 54 31 50 34 54 37 50M42 56Q46 49 50 56M58 49V57M62 49V57M66 49V57"
                 "M25 62H33M38 64Q41 60 44 64Q47 60 50 64M68 37Q72 33 75 37V44",
                 1.6),
           G::X(circ(70, 62, 2.4) + ell(62, 39, 3, 2)), G::XS(quill + taper({28, 90}, {26, 93}, {25, 95}, {24, 98}, 3.4, 0.8), 3),
           G::F(quill + taper({28, 90}, {26, 93}, {25, 95}, {24, 98}, 3.4, 0.8)), G::XS("M33 82 81 10", 1.4)});
  }
  // Глаз-уджат: бровь, глаз с удлинённым уголком, слеза и завиток.
  c.add("race-ibisians-wedjat", "Глаз-уджат",
        moved({G::F("M12 24C30 15 62 14 90 20L88 28C62 23 32 24 14 32Z" + std::string("M16 46C28 33 52 29 70 37C76 40 84 42 94 41L92 48C82 49 74 50 68 53") +
                    "C54 61 32 61 16 46Z" + "M33 58H42L43 84C43 89 38.5 91 35 88L33 86Z" +
                    taper({50, 58}, {56, 70}, {64, 82}, {75, 85}, 7, 4.5) +
                    ribbon([](double t) { return polar(73, 77, 8 * (1 - 0.55 * t), 90 - 290 * t); }, [](double t) { return 4.5 - 1.5 * t; })),
               G::X("M25 46C35 38 51 36 63 42C51 52 37 53 25 46Z"), G::F(circ(45, 44.5, 7)), G::X(circ(43, 42.5, 1.6))},
              Affine::translate(0, 3)));
}

// ---------------------------------------------------------------- канидисы
void canidis(Catalog& c) {
  c.group("Канидисы", EmblemSection::Races);
  // Голова шакала: профиль с высокими ушами, полосатый немес, ожерелье-ускх.
  {
    const std::string head = "M8 50C8 47 10 45 13 45L36 38C38 34 42 31 46 30L48 4 56 24 60 26 64 8 68 30C70 36 68 44 64 50C60 56 54 60 46 62L14 56"
                             "C10 55 8 53 8 50Z";
    c.add("race-canidis-head", "Голова шакала",
          {G::F("M50 32C56 28 66 28 72 34C77 40 79 50 79 60L84 92H56L58 76C58 66 56 58 52 50Z" + std::string("M40 82C54 88 72 90 92 85L93 94C72 98 52 96 36 90Z")),
           G::XS("M60 60H81M59.5 68H82M59 76H83M58 84H84M44 85C56 90 72 92 91 88", 1.6), G::XS(head, 2.6), G::F(head),
           G::X("M38 40.5 46.5 39 44.5 42.5Z" + std::string("M49.5 12 49 25 53 25Z") + circ(11.5, 48.5, 1.1)), G::XS("M12 52.5 34 55.5", 1.4),
           G::XS("M46.5 41 53 43", 1.2)});
  }
  // Скипетры-уас: два скрещённых жезла с головой зверя и раздвоенной пятой, над ними — солнечный диск.
  {
    const std::string was = "M44 21L45.5 6 64 8.5 67 16.5 55 16 53.5 21Z" + rrect(45.5, 19, 9, 69, 3) + "M45.5 86H54.5L61 97 54.5 92.5 50 97 45.5 92.5 39 97Z";
    std::vector<L> v = crossed(was, "", "M45.5 30H54.5M45.5 34.5H54.5M45.5 76H54.5", 1.4, 26, 50, 54, 1.0f, 4);
    v.push_back(G::XS(circ(50, 12, 10), 3));
    v.push_back(G::F(circ(50, 12, 10)));
    v.push_back(G::XS(circ(50, 12, 6), 1.5));
    c.add("race-canidis-was", "Скипетры-уас", v);
  }
  // Страж с копьём: шакалоглавый воин в профиль, копьё, щит, схенти.
  {
    const std::string head = "M23 22C23 20 25 18.5 27 18.5L37 15C38 13 40 12 42 11.5L43 1 47 9.5 49 10 51 2 53 12C54 15 53 19 51 22C49 25 46 27 42 27"
                             "L27 24.5C24.5 24 23 23.5 23 22Z";
    const std::string body = "M38 27H52L56 31 58 52H36L37 31Z" + std::string("M35 51H59L62 68H32Z") + taper({41, 66}, {38, 76}, {34, 86}, {30, 95}, 7, 5) +
                             taper({53, 66}, {55, 76}, {57, 86}, {60, 95}, 7, 5) + "M24 95H36V98H24ZM56 95H68V98H56Z";
    const std::string arm = taper({40, 31}, {34, 38}, {28, 40}, {22, 40}, 6, 4.5) + circ(21, 40, 3.2);
    const std::string shield = "M58 30H74C76 30 77 31 77 33V56C77 64 72 70 66 72C60 70 55 64 55 56V33C55 31 56 30 58 30Z";
    c.add("race-canidis-guard", "Страж с копьём",
          {G::F(rrect(19.5, 8, 3.4, 88, 1.7) + "M21.2 0C24.5 5 25.5 10 21.2 17C17 10 18 5 21.2 0Z"), G::XS(head + body + arm, 2.4),
           G::F(head + body + arm), G::X("M30 21 37 20 35.5 22.5Z" + std::string("M44.5 4.5 44 10 46.5 10Z")),
           G::XS("M36 51.5H59M40 56 38 67M47 56V67M54 56 56 67M38 38H56", 1.5), G::XS(shield, 2.6), G::F(shield),
           G::X("M66 37 69 43 66 49 63 43Z" + circ(66, 58, 3))});
  }
  // Шакал на ларце: лежащий шакал с поднятой головой на ковчеге с карнизом.
  {
    const std::string jackal = "M30 62C30 54 34 48 42 45C52 42 64 42 74 45C82 48 86 54 84 62Z" + std::string("M36 62H14C11 62 10 59 12 57H36Z") +
                               "M36 57H18C15 57 14 54 16 52H38Z" +
                               "M44 48C40 44 37 38 35 32L34 30C32 30 30 29 28 28L10 32C8 32 7 30 8 28L26 20C27 17 29 15 31 14L32 3 36 12 38 4 40 14"
                               "C44 20 46 28 48 38L50 46Z" +
                               taper({82, 58}, {88, 60}, {89, 68}, {86, 80}, 5, 3);
    c.add("race-canidis-shrine", "Шакал на ларце",
          {G::XS(jackal, 2.4), G::F(jackal), G::X("M27 22.5 33 21.5 31.5 24Z" + std::string("M33.5 6.5 33 12 35.5 12Z")), G::XS("M12 30 26 27", 1.2),
           G::XS("M70 47C76 50 79 55 78 60", 1.6), G::F(rrect(8, 63, 84, 6, 1) + rrect(13, 69, 74, 22, 1) + rrect(10, 91, 80, 5, 1)),
           G::XS(rrect(19, 73, 62, 14, 1), 1.6), G::X("M30 76.5V83.5M27.5 76.5H32.5M27.5 79H32.5M27.5 81.5H32.5" + std::string("M50 75.5A2.6 2.6 0 1 0 50 80.7A2.6 2.6 0 1 0 50 75.5Z")),
           G::XS("M30 76.5V83.5M27.5 77H32.5M28 79.5H32M28.5 82H31.5M50 81V85.5M47 81.5H53M70 76V85M66 80.5H74", 1.6)});
  }
}

// ---------------------------------------------------------------- кархароны
void karcharons(Catalog& c) {
  c.group("Кархароны", EmblemSection::Races);
  // Голова акулы: распахнутая пасть анфас с рядами зубов, глаза, грудные плавники.
  {
    std::string teeth;
    for (int row = 0; row < 2; row++) {
      const double rx = row ? 20 : 26.5, ry = row ? 17 : 23.5, len = row ? 6 : 9;
      for (int i = 0; i < 9; i++) {
        for (double base : {200.0, 20.0}) {
          const double a = base + 17.5 * i + (row ? 8 : 0);
          if (row && (i > 7)) continue;
          const Pt b0 = P(50 + rx * std::cos((a - 6) * kPi / 180), 62 + ry * std::sin((a - 6) * kPi / 180));
          const Pt b1 = P(50 + rx * std::cos((a + 6) * kPi / 180), 62 + ry * std::sin((a + 6) * kPi / 180));
          const Pt tip = P(50 + (rx - len) * std::cos(a * kPi / 180), 62 + (ry - len) * std::sin(a * kPi / 180));
          teeth += "M" + ptS(b0) + "L" + ptS(b1) + "L" + ptS(tip) + "Z";
        }
      }
    }
    c.add("race-karcharons-head", "Голова акулы",
          {G::F("M50 5C62 5 72 13 78 24C86 32 92 46 90 60C88 74 78 86 64 92C56 95 44 95 36 92C22 86 12 74 10 60C8 46 14 32 22 24C28 13 38 5 50 5Z" +
                std::string("M18 76 3 95 30 87ZM82 76 97 95 70 87Z")),
           G::X(ell(50, 62, 27, 24) + sym(circ(24, 37, 3.2) + ell(42.5, 17, 1.6, 2.6))), G::F(teeth), G::XS(sym("M13 47Q11 54 13 61M17 45Q15 54 17 63"), 1.5)});
  }
  // Акулий зуб: треугольный зуб с зазубренными краями, корень, отверстие для шнурка.
  {
    const std::string tooth = "M53 8" + serrate({53, 8}, {40, 24}, {22, 42}, {11, 62}, 14, 1.8) + "L89 62" + serrate({89, 62}, {78, 42}, {62, 24}, {53, 8}, 14, 1.8) + "Z";
    const std::string root = "M9 58C30 66 70 66 91 58C94 70 90 84 80 90C72 92 64 85 58 78C55 75 45 75 42 78C36 85 28 92 20 90C10 84 6 70 9 58Z";
    c.add("race-karcharons-tooth", "Акулий зуб",
          moved({G::F(tooth), G::XS(root, 2.8), G::F(root), G::XS("M46 22C42 30 38 38 36 46", 1.6), G::XS("M14 66C32 72 68 72 86 66", 1.5)},
                rotAt(-12, 50, 50) * Affine::translate(0, 2)));
  }
  // Плавник над волной: спинной плавник с надрывом, кончик хвоста, гребни волн с завитками.
  {
    auto wave = [](double y, double amp) {
      std::string s = "M2 " + n(y + 4);
      for (int i = 0; i < 4; i++) {
        const double x = 2 + 24 * i;
        s += "C" + n2(x + 6, y - amp) + " " + n2(x + 16, y - amp) + " " + n2(x + 20, y) + "C" + n2(x + 22, y + 3) + " " + n2(x + 26, y + 2) + " " + n2(x + 24, y - 1);
      }
      return s + "L98 " + n(y + 12) + "H2Z";
    };
    c.add("race-karcharons-fin", "Плавник над волной",
          {G::F("M26 64C34 45 46 24 70 6C65 26 67 46 78 64Z" + std::string("M83 64 90 48 95 64Z")), G::X("M74.5 49 69.5 53 75.5 56Z"),
           G::XS("M38 60C44 44 52 30 64 18", 1.6), G::XS(wave(64, 8), 2.6), G::F(wave(64, 8)),
           G::XS(xf(wave(64, 8), Affine::translate(0, 14) * Affine{-1, 0, 0, 1, 100, 0}), 2.6),
           G::F(xf(wave(64, 8), Affine::translate(0, 14) * Affine{-1, 0, 0, 1, 100, 0})),
           G::X(circ(21, 62, 2.4) + circ(45, 62, 2.4) + circ(69, 62, 2.4) + circ(79, 76, 2.4) + circ(55, 76, 2.4) + circ(31, 76, 2.4))});
  }
  // Трезубец с акулой: акула наискось позади трезубца с зазубренными зубцами.
  {
    const std::string trident = "M50 2 57 15 53.5 14V31H46.5V14L43 15Z" + sym("M27 8 34 21 30.5 20V36H23.5V20L20 21Z") +
                                "M23.5 33C23.5 44 34 50 50 50C66 50 76.5 44 76.5 33H69.5C69.5 40 61 43.5 50 43.5C39 43.5 30.5 40 30.5 33Z" +
                                rrect(46.5, 46, 7, 46, 2) + "M45 90H55L50 99Z";
    c.add("race-karcharons-trident", "Трезубец с акулой",
          {G::F("M4 72C10 64 22 58 36 54C52 50 66 44 78 34L92 20 90 36 97 46 82 42C71 52 57 60 41 65C27 69 15 72 4 72Z" +
                std::string("M44 52 54 35 60 48ZM28 64 19 78 37 64Z")),
           G::X(circ(14, 66, 1.7)), G::XS("M24 59 22 67M28 58 26 66M32 57 30 65", 1.5), G::XS(trident, 3), G::F(trident),
           G::X("M47.5 18 46.5 22 49 21ZM24.5 24 23.5 28 26 27ZM75.5 24 76.5 28 74 27Z"), G::XS("M46.5 60H53.5M46.5 64H53.5", 1.4)});
  }
}

// ---------------------------------------------------------------- кентавры
void centaurs(Catalog& c) {
  c.group("Кентавры", EmblemSection::Races);
  // Кентавр на дыбах: конь встал на задние ноги, всадник-торс натягивает лук, хвост по ветру.
  {
    const std::string horse = "M38 51C38 44 42 40 48 40C56 41 66 46 74 52C80 56 86 60 86 68C86 76 80 80 74 79C68 78 62 74 56 70C50 66 44 61 40 57"
                              "C38.5 55 38 53 38 51Z" +
                              taper({79, 75}, {81, 83}, {82, 89}, {83, 95}, 8, 4.5) + taper({69, 75}, {67, 84}, {64, 90}, {61, 95}, 7.5, 4.5) +
                              "M78 94H86V98H78ZM57 94H65V98H57Z" + taper({45, 55}, {39, 57}, {35, 57}, {31, 57}, 7, 5.5) +
                              taper({31, 57}, {29, 61}, {29, 64}, {30, 68}, 5.2, 3.6) + taper({43, 47}, {37, 45}, {33, 44}, {29, 43}, 6.5, 5) +
                              taper({29, 43}, {26, 46}, {25, 49}, {25, 53}, 5, 3.6) + taper({50, 47}, {48, 38}, {46, 31}, {44, 24}, 13, 11) + circ(42.5, 15, 6.2) +
                              taper({38, 25}, {31, 24}, {24, 24}, {17, 24}, 5, 3.6) + taper({49, 25}, {52, 22}, {52, 19}, {48, 19}, 4.6, 3.6);
    c.add("race-centaurs-archer", "Кентавр на дыбах",
          {G::F(taper({84, 64}, {93, 68}, {97, 79}, {91, 91}, 8, 1.5)), G::XS(horse, 2), G::F(horse), G::S("M20 2C10 14 10 36 20 48", 3.2),
           G::S("M20 2 47 20 20 48", 1.1), G::S("M47 20H6", 1.6), G::F("M2 20 8 16.5V23.5Z"), G::F(taper({46, 11}, {52, 9}, {56, 12}, {60, 17}, 5, 1)),
           G::XS("M57 68C61 72 66 74 70 74M52 41 56 46", 1.5)});
  }
  // Подкова со звездой: подкова концами вверх с гвоздевой бороздкой, над ней — звезда.
  {
    std::string holes;
    for (int k = 0; k < 8; k++) {
      const double a = -30 + 34.3 * k;
      const Pt p = polar(50, 58, 33, a);
      holes += xf(rrect(-1.4, -2.4, 2.8, 4.8, 0.8), place(p.x, p.y, float(a)));
    }
    c.add("race-centaurs-horseshoe", "Подкова со звездой",
          {G::F(taperArc(50, 58, 33, -58, 238, 15, 15)), G::XS(arc(50, 58, 33, -46, 226), 1.4), G::X(holes), G::F(star(50, 22, 14.5, 5.8, 5, -90)),
           G::XS("M" + pt(50, 58, 25, -50) + "L" + pt(50, 58, 41, -50) + "M" + pt(50, 58, 25, 230) + "L" + pt(50, 58, 41, 230), 1.8)});
  }
  // Копьё и щит: круглый щит с конской головой, за ним — копьё наискось.
  {
    const std::string shield = circ(45, 57, 31);
    c.add("race-centaurs-spear", "Копьё и щит",
          {G::S("M10 94 82 18", 4.2), G::F(xf("M0 -15C5 -8 6 -2 0 9C-6 -2 -5 -8 0 -15Z", place(87, 13, 45)) + xf(rrect(-3, -3, 6, 6, 1), place(80, 20, 45))),
           G::XS(shield, 3.2), G::F(shield), G::XS(circ(45, 57, 26), 2),
           G::X("M31 64C32 58 38 50 45 43L50 35 52.5 41 56 34 58 42C62 48 64 58 63 68L65 80H48C48 74 48 70 46 66C43 67 39 68 36 68C33 68 30.5 66.5 31 64Z"),
           G::F(xf(ell(0, 0, 2.2, 1.4), place(48, 47, -35)) + circ(34.5, 64.5, 1)), G::S("M56.5 45C59 52 59.5 62 59 74", 1.4)});
  }
  // Степной шатёр: купол-юрта с решётчатыми стенами, дверь, навершие с флажком.
  {
    std::string lattice;
    for (double x = 14; x < 86; x += 9) {
      if (x + 9 > 40 && x < 60) continue;
      lattice += "M" + n2(x, 58) + "L" + n2(x + 9, 86) + "M" + n2(x + 9, 58) + "L" + n2(x, 86);
    }
    c.add("race-centaurs-tent", "Степной шатёр",
          {G::S("M50 17V3", 2.2), G::F("M51 2H73L66 8 73 14H51Z"), G::F("M8 54C16 36 32 24 50 22C68 24 84 36 92 54Z" + rrect(43, 15, 14, 9, 2) +
                                                                    rrect(13, 52, 74, 36, 1.5) + rrect(4, 88, 92, 7, 3.5)),
           G::XS("M50 24 22 52M50 24 36 52M50 24V52M50 24 64 52M50 24 78 52", 1.5), G::XS("M11 55.5H89", 2), G::XS(lattice, 1.4), G::X(rrect(40, 61, 20, 27, 2)),
           G::F(rrect(43, 64, 14, 24, 1)), G::XS("M50 64V88", 1.4), G::X(circ(46.5, 77, 1.2) + circ(53.5, 77, 1.2))});
  }
}

}  // namespace

void raceEmblemsA(Catalog& c) {
  angels(c);
  arachnids(c);
  arborians(c);
  faceless(c);
  vampires(c);
  giants(c);
  highElves(c);
  harpies(c);
  gnolls(c);
  dwarvesRace(c);
  goblins(c);
  demonsRace(c);
  dryads(c);
  ibisians(c);
  canidis(c);
  karcharons(c);
  centaurs(c);
}

}  // namespace rg::gfx::emb
