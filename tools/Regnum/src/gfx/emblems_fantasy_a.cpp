// Regnum — фантазийные эмблемы каталога: драконы, вампиры, природа, стихии, гномы, эльфы, некроманты, тёмные эльфы, наги. Сетка 100 × 100, стиль классических эмблем:
// одноцветный силуэт, детали — вырезы. Симметричные силуэты описаны левой половиной.
#include "gfx/emblems_internal.h"

namespace rg::gfx::emb {

namespace {

// ---------------------------------------------------------------- общие помощники

Pt lerp(Pt a, Pt b, double t) { return {float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t)}; }

// Путь и его отражение относительно вертикали x = axis.
std::string symAt(const std::string& d, double axis) { return d + mirrorX(d, axis); }

// Перепончатое крыло: корень у тела, плечо, кривая руки до запястья, коготь большого пальца, концы пальцев;
// перепонка между пальцами провисает дугами к запястью (sag). bones — линии пальцев для вырезов.
struct Wing {
  std::string fill, bones;
};
Wing membraneWing(Pt root, Pt shoulder, Pt c1, Pt c2, Pt wrist, Pt thumb, const std::vector<Pt>& tips, double sag) {
  Wing w;
  w.fill = "M" + ptS(root) + "L" + ptS(shoulder) + "C" + ptS(c1) + " " + ptS(c2) + " " + ptS(wrist) + "L" + ptS(thumb) + "L" +
           ptS(lerp(wrist, tips[0], 0.16)) + "L" + ptS(tips[0]);
  for (size_t i = 0; i + 1 < tips.size(); i++)
    w.fill += "Q" + ptS(lerp(lerp(tips[i], tips[i + 1], 0.5), wrist, sag)) + " " + ptS(tips[i + 1]);
  w.fill += "Q" + ptS(lerp(lerp(tips.back(), root, 0.5), wrist, sag * 0.8)) + " " + ptS(root) + "Z";
  for (size_t i = 1; i < tips.size(); i++) w.bones += "M" + ptS(lerp(wrist, tips[i], 0.1)) + "L" + ptS(lerp(wrist, tips[i], 0.84));
  return w;
}

// Шипы вдоль кубической кривой от t0 до t1: side — сторона нормали (±1), высота h0…h1, основание w, наклон по ходу lean.
std::string spikes(Pt p0, Pt c1, Pt c2, Pt p1, double t0, double t1, int count, double h0, double h1, double w, double lean, int side) {
  std::string s;
  for (int k = 0; k < count; k++) {
    const double f = count > 1 ? double(k) / (count - 1) : 0, t = t0 + (t1 - t0) * f, h = h0 + (h1 - h0) * f;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    const double tx = q.ny, ty = -q.nx, nx = q.nx * side, ny = q.ny * side;
    const Pt a{float(q.x - tx * w * 0.5 - nx), float(q.y - ty * w * 0.5 - ny)}, b{float(q.x + tx * w * 0.5 - nx), float(q.y + ty * w * 0.5 - ny)};
    const Pt tip{float(q.x + nx * h + tx * h * lean), float(q.y + ny * h + ty * h * lean)};
    s += "M" + ptS(a) + "Q" + ptS(lerp(lerp(a, b, 0.3), tip, 0.6)) + " " + ptS(tip) + "L" + ptS(b) + "Z";
  }
  return s;
}

// Поперечные вырезы-пластины вдоль кривой: от края внутрь на глубину d0…d1 (side — сторона нормали внутрь), чуть выгнутые.
std::string plates(Pt p0, Pt c1, Pt c2, Pt p1, double t0, double t1, int count, double d0, double d1, int side) {
  std::string s;
  for (int k = 0; k < count; k++) {
    const double f = count > 1 ? double(k) / (count - 1) : 0, t = t0 + (t1 - t0) * f, d = d0 + (d1 - d0) * f;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    const double tx = q.ny, ty = -q.nx, nx = q.nx * side, ny = q.ny * side;
    s += "M" + ptS({float(q.x - nx), float(q.y - ny)}) + "Q" +
         ptS({float(q.x + nx * d * 0.5 + tx * d * 0.25), float(q.y + ny * d * 0.5 + ty * d * 0.25)}) + " " + ptS({float(q.x + nx * d), float(q.y + ny * d)});
  }
  return s;
}

// Линия, параллельная кривой на расстоянии off (по нормали), от t0 до t1.
std::string offsetCurve(Pt p0, Pt c1, Pt c2, Pt p1, double off, double t0 = 0, double t1 = 1, int steps = 20) {
  std::string s;
  for (int i = 0; i <= steps; i++) {
    const CurvePt q = bezier(p0, c1, c2, p1, t0 + (t1 - t0) * i / steps);
    s += (i ? "L" : "M") + ptS({float(q.x + q.nx * off), float(q.y + q.ny * off)});
  }
  return s;
}

// Чешуя: ряды дуг (выпуклостью вниз) внутри эллипса (cx, cy, rx, ry), шаг step, радиус r, шаг рядов rowStep.
std::string scaleRows(double cx, double cy, double rx, double ry, double step, double r, double rowStep) {
  std::string s;
  int row = 0;
  for (double y = cy - ry; y <= cy + ry; y += rowStep, row++) {
    for (double x = cx - rx - (row % 2 ? step * 0.5 : 0); x <= cx + rx + step; x += step) {
      const double ex = (x - cx) / rx, ey = (y + r * 0.5 - cy) / ry;
      if (ex * ex + ey * ey > 1) continue;
      s += "M" + n(x - r) + " " + n(y) + "A" + n(r) + " " + n(r) + " 0 0 0 " + n(x + r) + " " + n(y);
    }
  }
  return s;
}

// Голова дракона в профиль (мордой влево, пасть открыта): морда у (6, 31), затылок (52, 20), угол челюсти (38, 54).
const std::string kDragonHead =
    "M6 31C7 27 9 24.5 13 23.5C20 21.5 26 18.5 31 15L35 8.5 38 14C43 13 48 15.5 52 20L47 50 38 54C30 55 21 54.5 15 52.5"
    "L8 47.5 10.5 45 32 40 8 33.5Z";
const std::string kDragonFangs = "M11 34.4 15.5 35.6 13 41ZM18 43.5 22 42.6 20 37.6Z";
std::string dragonHorns() {
  return taper({39, 15.5}, {49, 9}, {60, 4}, {75, 2}, 7.5, 1) + taper({46, 19}, {54, 16}, {63, 15}, {72, 17}, 5.5, 1);
}
const std::string kDragonEye = "M22 25.5C24.5 22.5 29 21 33 21.5C31 24.5 27 26.5 22 25.5Z";
const std::string kDragonNostril = "M9.5 27.5C10.5 26 12.5 25.3 14 25.6C13 27 11.5 27.8 9.5 27.5Z";

// ---------------------------------------------------------------- драконы
void dragons(Catalog& c) {
  c.group("Драконы");
  // Дракон в полёте: голова с рогами, крылья с перепонками и пальцами, чешуя брюха, лапы с когтями, хвост с остриём.
  {
    const Wing w = membraneWing({44, 58}, {43, 37}, {38, 27}, {32, 17}, {24, 10}, {20, 1.5}, {{3, 18}, {2, 38}, {7, 56}, {21, 67}}, 0.27);
    const std::string head = "M50 6C46 6 42.5 8 41 11.5L39.5 16 35.5 19.5 40.5 21C42 25 44 29 46 32L47.5 35H50.5V6Z" +
                             taper({42, 12.5}, {38.5, 8}, {35, 4}, {29.5, 1.5}, 5.5, 1) + taper({40, 17}, {36, 15.5}, {33, 14}, {29, 14}, 3.6, 0.8);
    const std::string leg = taper({45.5, 55}, {41, 60}, {38, 64}, {36, 70}, 7, 4.2) + taper({36, 70}, {33, 71}, {31, 73}, {29.5, 76}, 3.2, 0.8) +
                            taper({36, 70}, {35, 73}, {34.5, 75}, {35, 78}, 3.2, 0.8) + taper({36.5, 70}, {38.5, 71.5}, {40, 73}, {40.5, 76}, 3, 0.8);
    const std::string tail = taper({50, 58}, {51, 72}, {65, 72}, {61, 84}, 9.5, 3.5) +
                             "M61.5 81.5C64 86 63 90 57.5 97C55.5 92 53 89 50 87.5C54 86.5 58 84.5 61.5 81.5Z";
    c.add("dragon-wings", "Дракон в полёте",
          {G::F(sym(w.fill)), G::XS(sym(w.bones + "M40.5 41C36 33 31 24 26 16"), 1.6), G::XS(ell(50, 48, 9, 14) + sym(head), 3),
           G::F(sym(head) + ell(50, 48, 9, 14) + sym(leg) + tail), G::X(sym("M42.5 14.5 47.8 17 44 18.2Z" + ell(47.6, 30.5, 1, 1.6))),
           G::XS("M44.5 41Q50 43.5 55.5 41M43 46.5Q50 49 57 46.5M43.5 52Q50 54.5 56.5 52M45.5 57.5Q50 59.5 54.5 57.5", 1.6)});
  }
  // Свернувшийся дракон (уроборос): кольцо с плавниками и чешуёй, рогатая голова кусает хвост.
  {
    const double cx = 50, cy = 54, R = 30;
    std::string fins, chevrons;
    for (int a = -28; a <= 236; a += 22) {
      const double f = (a + 28) / 264.0, wb = 13 - 9.5 * f, h = 9 - 4 * f;
      const double r0 = R + wb * 0.5 - 1;
      fins += "M" + pt(cx, cy, r0, a - 8) + "Q" + pt(cx, cy, r0 + h * 0.7, a - 2) + " " + pt(cx, cy, r0 + h, a + 7) + "L" + pt(cx, cy, r0, a + 6) + "Z";
    }
    for (int a = -30; a <= 240; a += 11) {
      const double f = (a + 50) / 316.0, wb = 13 - 10.8 * f;
      if (wb < 5) continue;
      chevrons += "M" + pt(cx, cy, R - wb * 0.5 - 1, a) + "L" + pt(cx, cy, R - wb * 0.05, a + 1.5);
    }
    const Affine hx = place(46.2f, -6.2f, 21, 0.7f);
    c.add("dragon-coiled", "Свернувшийся дракон",
          moved({G::F(taperArc(cx, cy, R, -50, 266, 13, 2.2) + fins), G::XS(chevrons, 1.5), G::XS(xf(kDragonHead, hx), 3),
                 G::F(xf(kDragonHead + dragonHorns() + kDragonFangs, hx)), G::F(taperArc(cx, cy, R, -52, -36, 12.5, 12.5)),
                 G::X(xf(kDragonEye + kDragonNostril, hx))},
                Affine::translate(-3.5f, -1.5f)));
  }
  // Глаз дракона: миндалевидный глаз с полосчатой радужкой и щелевым зрачком, бровь с шипами и чешуёй, нижнее веко.
  {
    const std::string eye = "M6 56C24 36 58 30 94 44C76 66 42 74 6 56Z";
    const std::string brow = "M4 45C16 28 38 18 62 16L72 3 74 15 87 8 83 21C88 23 92 27 96 32C80 28 60 27 42 30C28 33 14 39 4 45Z";
    const std::string lid = "M10 63C30 74 60 74 88 56C84 68 70 80 50 82C32 82 18 74 10 63Z";
    std::string rays;
    for (int k = 0; k < 12; k++) {
      const double a = 15 + 30 * k;
      rays += "M" + pt(51, 52, 9.5, a) + "L" + pt(51, 52, 13, a);
    }
    c.add("dragon-eye", "Глаз дракона",
          moved({G::F(eye + brow + lid), G::XS("M12 40C28 30 46 25 64 24", 1.6),
                 G::XS("M30 72C36 75 42 76.5 48 77M58 76 63 74", 1.6), G::X(circ(51, 52, 17)), G::F(circ(51, 52, 14.5)), G::XS(rays, 1.5),
                 G::X("M51 38.5C55.5 46 55.5 58 51 65.5C46.5 58 46.5 46 51 38.5Z"), G::X(circ(58, 45, 2.3))},
                Affine::translate(0, 7)));
  }
  // Череп дракона: изогнутые рога с кольцами, скулы с шипами, глазницы под надбровьем, клыки, ноздри, трещина.
  {
    const std::string half = "M50 13C43 13 37.5 16.5 35 22L29 33 18 38 29 42C32 50 35 58 37.5 67L40 80 43.5 93 47 96H50.5V13Z";
    const std::string horn = taper({37, 24}, {27, 14}, {15, 9}, {4, 4}, 11, 1.2);
    const std::string horn2 = taper({30, 36}, {22, 34}, {16, 30}, {11, 23}, 6, 1);
    const std::string fang = "M39 71 35.5 82 41.5 77ZM41.5 82 39.5 90 43.5 87Z";
    c.add("dragon-skull", "Череп дракона",
          {G::F(sym(half + horn + horn2 + fang)),
           G::X(sym("M31.5 33C37 34.5 42.5 37.5 46.5 41.5C46 45 43.5 48 40 48C35.5 47 32.5 42 31.5 33Z" + xf(ell(0, 0, 1.8, 3.4), place(46, 87, -12)))),
           G::XS(sym("M29.5 19 23 16.5M24 14 19 11M18 10.5 14 8.5M39 52C41 60 43 70 44.5 79"), 1.6), G::XS("M47 14 45 20 48.5 24 46.5 30", 1.6)});
  }
  // Голова дракона: ревущая пасть с клыками, рога, гребень шипов по шее, брюшные пластины.
  {
    const Pt b0{50, 19}, b1{66, 27}, b2{78, 43}, b3{82, 61}, b4{85, 74}, b5{88, 86}, b6{90, 97};
    const Pt f0{61, 97}, f1{59, 85}, f2{54, 72}, f3{47, 63};
    const std::string neck = "M" + ptS(b0) + "C" + ptS(b1) + " " + ptS(b2) + " " + ptS(b3) + "C" + ptS(b4) + " " + ptS(b5) + " " + ptS(b6) + "H" +
                             n(f0.x) + "C" + ptS(f1) + " " + ptS(f2) + " " + ptS(f3) + "C43 59 40 56 37 53Z";
    const std::string crest = spikes(b0, b1, b2, b3, 0.12, 0.92, 5, 9, 8, 7, 0.55, -1) + spikes(b3, b4, b5, b6, 0.25, 0.75, 2, 7, 6, 6.5, 0.55, -1);
    c.add("dragon-head", "Голова дракона",
          {G::F(kDragonHead + kDragonFangs + dragonHorns() + neck + crest), G::X(kDragonEye + kDragonNostril),
           G::XS("M45 24C41 33 40 42 39 52" + offsetCurve(f0, f1, f2, f3, 9, 0, 0.9) + plates(f0, f1, f2, f3, 0.1, 0.85, 5, 9, 9, 1), 1.6),
           G::XS("M16 51.5C22 52 27 51.5 31 50.5", 1.6)});
  }
  // Дракон на задних лапах (геральдический «восстающий»): поднятое крыло, передние лапы с когтями, хвост с остриём.
  {
    const Affine hx = place(2, 6, 0, 0.6f);
    const Wing w = membraneWing({58, 56}, {51, 40}, {55, 28}, {61, 17}, {70, 10}, {70.5, 2.5}, {{96, 11}, {98, 30}, {93, 48}, {81, 58}}, 0.3);
    auto talons = [](Pt p, Pt a, Pt b, Pt c) {
      std::string s;
      for (Pt t : {a, b, c}) s += taper(p, lerp(p, t, 0.4), lerp(p, t, 0.75) + Pt{0, 1.2f}, t, 3.4, 0.6);
      return s;
    };
    const std::string body = "M31 16C41 19 48 27 51 37C58 41 64 49 66 58C68 66 66 73 62 78L52 80C45 76 40 70 37 63C34 56 32 50 30 46"
                             "C27 43 25 40 23 38Z" + xf(ell(0, 0, 8.5, 12), place(59, 70, -25));
    const std::string fore = taper({36, 51}, {30, 50}, {25, 48}, {19, 46}, 7, 4.5) + talons({19, 46}, {10, 42}, {9.5, 48}, {13, 53.5}) +
                             taper({39, 62}, {33, 63}, {28, 64}, {23, 63}, 7, 4.5) + talons({23, 63}, {14, 59}, {13.5, 65}, {17, 70});
    const std::string legA = taper({60, 77}, {57, 84}, {53, 88}, {49, 92}, 7, 5) + talons({49, 92}, {39, 95}, {43, 98}, {50, 98.5});
    const std::string legB = taper({55, 71}, {48, 75}, {43, 78}, {38, 82}, 7.5, 5) + talons({38, 82}, {29, 79}, {28.5, 85}, {32, 90});
    const std::string tail = taper({62, 74}, {70, 92}, {88, 97}, {93, 82}, 9, 3) + feather({93, 85}, {94.5, 68}, 9);
    const std::string front = xf(kDragonHead + dragonHorns() + kDragonFangs, hx) + body + fore + legA + tail;
    c.add("dragon-rampant", "Дракон на задних лапах",
          {G::F(w.fill), G::XS(w.bones + "M55 44C58 33 63 23 69 15", 1.6), G::XS(front, 3), G::F(front), G::XS(legB, 2.6), G::F(legB),
           G::X(xf(kDragonEye + kDragonNostril, hx)),
           G::XS(plates({52, 80}, {45, 76}, {40, 70}, {37, 63}, 0.15, 0.95, 4, 6, 6, 1) + plates({37, 63}, {34, 56}, {32, 50}, {30, 46}, 0.3, 0.9, 2, 5, 5, 1),
                 1.5)});
  }
  // Драконье яйцо: из треснувшей чешуйчатой скорлупы выглядывает дракончик, верхушка скорлупы отлетела.
  {
    const Affine hx = place(6.2f, 8.3f, -10, 0.6f);
    const std::string egg = "M22 60C22 51 23.5 45 26 40L31 46 36 38.5 42 46 48 39 54 47 60 40 66 46 71.5 39C75.5 45 78 52 78 60"
                            "C78 81 66 97 50 97C34 97 22 81 22 60Z";
    const std::string cap = xf("M-15 5C-14 -4 -8 -10 0 -10C8 -10 14 -4 15 5L10 1 5 7 0 1.5 -5 7 -10 1Z", place(76, 21, 28));
    const std::string head = xf(kDragonHead + dragonHorns() + kDragonFangs, hx) + taper({37, 22}, {47, 26}, {53, 33}, {55, 48}, 15, 14);
    c.add("dragon-egg", "Драконье яйцо",
          {G::F(head + cap), G::X(xf(kDragonEye + kDragonNostril, hx)), G::XS(cap, 1), G::XS(xf("M-11 -1C-8 -6 -4 -7.5 0 -7.5", place(76, 21, 28)), 1.6),
           G::XS(egg, 3.5), G::F(egg), G::XS(scaleRows(50, 72, 21, 22, 10, 5, 7.5), 1.6), G::XS("M42 46 39.5 55 45 61 42 68", 1.8)});
  }
  // Драконья лапа: поднятая чешуйчатая лапа, четыре пальца и большой палец с загнутыми когтями, шипы запястья.
  {
    struct Finger {
      Pt b, k, t;
    };
    const Finger fs[] = {{{39, 54}, {30.5, 40}, {14, 17}}, {{45.5, 50}, {42.5, 34}, {33, 8}}, {{54.5, 50}, {57.5, 34}, {67, 8}},
                         {{61, 54}, {69.5, 40}, {86, 17}}, {{36, 68}, {27, 63.5}, {9, 51}}};
    std::string fingers, knuckles;
    for (const Finger& f : fs) {
      // коготь выгибается наружу и загибается остриём к середине лапы
      const Pt dir = f.t - f.k;
      const float out = f.t.x < 50 ? -1.f : 1.f;
      const Pt c1 = f.k + dir * 0.45f + Pt{out * 3, 0}, c2 = f.t + Pt{out * 2, 0}, tip = f.t + Pt{-out * 4, 2};
      fingers += taper(f.b, lerp(f.b, f.k, 0.35), lerp(f.b, f.k, 0.7), f.k, 11, 9.5) + taper(f.k, c1, c2, tip, 9.5, 0.8);
      const float dx = f.k.x - f.b.x, dy = f.k.y - f.b.y, l = std::hypot(dx, dy), nx = -dy / l * 6, ny = dx / l * 6;
      knuckles += "M" + ptS({f.k.x - nx, f.k.y - ny}) + "Q" + ptS(f.k + Pt{dx, dy} * (2.5f / l)) + " " + ptS({f.k.x + nx, f.k.y + ny});
    }
    const std::string palm = "M37 50C41 46 59 46 63 50C68 56 68 70 64 78L61 97H39L36 78C32 70 32 56 37 50Z" + sym("M38.5 80 30 85 38.8 89Z");
    c.add("dragon-claw", "Драконья лапа",
          moved({G::F(palm + fingers), G::XS(knuckles, 1.6), G::XS(scaleRows(50, 70, 11, 19, 7, 3.5, 5.5), 1.5)}, Affine::translate(3, 0)));
  }
  // Два дракона: змеевидные драконы с гребнями, головы обращены к жемчужине.
  {
    const Affine hx{-0.5f, 0, 0, 0.5f, 45, 6.5f};
    const Pt p0{24, 28}, p1{3, 42}, p2{8, 75}, p3{35, 83};
    const std::string body = taper(p0, p1, p2, p3, 15, 10) + taper(p3, {46, 87}, {56, 90}, {71, 95.5}, 10, 2) +
                             spikes(p0, p1, p2, p3, 0.12, 0.8, 5, 8, 6, 7, 0.5, 1);
    const std::string paw = taper({14, 47}, {20, 46}, {25, 44}, {30, 42}, 6.5, 4.5) + taper({30, 42}, {32, 39}, {34, 37.5}, {36.5, 37}, 3, 0.6) +
                            taper({30, 42}, {33, 41.5}, {35, 41.5}, {37.5, 42.5}, 3, 0.6) + taper({30, 42.5}, {32, 44.5}, {33.5, 46}, {34.5, 48}, 3, 0.6);
    const std::string pearl = sym(taper({46.5, 25}, {43, 21}, {44, 16}, {41.5, 12}, 4.5, 0.8)) + taper({50, 24}, {47, 19}, {53, 14}, {50, 6}, 7, 1);
    const std::string half = xf(kDragonHead + dragonHorns() + kDragonFangs, hx) + body + paw;
    c.add("dragon-twin", "Два дракона",
          {G::F(sym(half)), G::X(sym(xf(kDragonEye + kDragonNostril, hx))), G::XS(sym(plates(p0, p1, p2, p3, 0.15, 0.9, 6, 6, 4.5, -1)), 1.5),
           G::XS(circ(50, 31, 7.5) + pearl, 3), G::F(circ(50, 31, 7.5) + pearl), G::XS("M45.5 29A5 5 0 0 1 49.5 25.8", 1.6)});
  }
}

// ---------------------------------------------------------------- вампиры
// Капля (кровь, вода): острый верх в (cx, top), круглый низ радиуса r с центром на cy.
std::string drop(double cx, double top, double cy, double r) {
  return "M" + n(cx) + " " + n(top) + "C" + n(cx + r * 0.35) + " " + n(top + (cy - top) * 0.45) + " " + n(cx + r) + " " + n(cy - r * 0.6) + " " +
         n(cx + r) + " " + n(cy) + "A" + n(r) + " " + n(r) + " 0 0 1 " + n(cx - r) + " " + n(cy) + "C" + n(cx - r) + " " + n(cy - r * 0.6) + " " +
         n(cx - r * 0.35) + " " + n(top + (cy - top) * 0.45) + " " + n(cx) + " " + n(top) + "Z";
}

// Летучая мышь (левая половина): крыло, ухо, голова и тело; глаза и шерсть — вырезы. Сетка 100, размах 3…97.
struct Bat {
  std::string fill, cuts, bones;
};
Bat batHalf() {
  const Wing w = membraneWing({46, 60}, {44, 42}, {40, 35}, {34, 28}, {27, 23}, {24, 15.5}, {{5, 29}, {3, 47}, {12, 62}, {28, 70}}, 0.3);
  Bat b;
  b.fill = w.fill + "M44.5 34C42.5 28 42 22 42.5 15.5C45.5 20 48 25 49.5 31Z" + "M46.5 64 44.5 71.5 48.5 67.5Z";
  b.bones = w.bones + "M41 45C37 38 33 32 29 27";
  b.cuts = "M44.3 37.2 48.6 38.8 45 40.5Z";
  return b;
}

void vampires(Catalog& c) {
  c.group("Вампиры");
  // Летучая мышь: перепончатые крылья с пальцами, острые уши, глаза, шерсть на груди.
  {
    const Bat b = batHalf();
    c.add("vampire-bat", "Летучая мышь",
          moved({G::F(sym(b.fill) + circ(50, 38, 8) + ell(50, 53, 7.5, 13)), G::XS(sym(b.bones), 1.6), G::X(sym(b.cuts)),
                 G::XS("M46 49 50 52 54 49M46.5 55 50 58 53.5 55", 1.5), G::X("M47.5 42.5H52.5L51.6 45 50 43.6 48.4 45Z")},
                Affine::translate(0, 7)));
  }
  // Клыки: губы с изгибом, ряд зубов, длинные клыки, капли крови.
  {
    const std::string fang = "M29.5 41.5 40.5 42.5 36 74Z";
    c.add("vampire-fangs", "Клыки",
          moved({G::F("M6 42C18 30 34 26 50 33C66 26 82 30 94 42C82 60 66 68 50 68C34 68 18 60 6 42Z"),
                 G::X("M14 43C28 39 40 40 50 42C60 40 72 39 86 43C74 51 62 54 50 54C38 54 26 51 14 43Z"),
                 G::F("M24 42.6C33 40.8 42 41.2 50 43C58 41.2 67 40.8 76 42.6L74 46.5C66 48 58 48.6 50 48.6C42 48.6 34 48 26 46.5Z"),
                 G::XS("M44 41.5V49.5M50 42V50M56 41.5V49.5", 1.6), G::XS(sym("M30.8 48 36 74 39.7 48"), 4), G::F(sym(fang)),
                 G::F(sym(drop(36, 79, 89.5, 5))), G::XS("M38 61.5Q50 65 62 61.5", 1.8), G::XS(sym("M33.5 81.5Q32.5 84 33 86.5"), 1.4)},
                Affine::translate(0, -10)));
  }
  // Кубок крови: капля над кубком, кровь стекает с ободка, самоцвет на чаше, узел ножки, ступенчатое основание.
  c.add("vampire-chalice", "Кубок крови",
        {G::F(drop(50, 3, 25, 11) + "M18 38H82C82 58 69 70 50 72C31 70 18 58 18 38Z" + rrect(46.5, 70, 7, 17, 2) + ell(50, 76.5, 7, 3.5) +
              ell(50, 84, 5, 2.5) + "M28 97C30 91 39 87 50 87C61 87 70 91 72 97Z"),
         G::XS("M19 44H81", 2.2), G::XS("M20 49H30C30 55.5 37 55.5 37 49H52C52 59 60 59 60 49H80", 2.4), G::X("M50 58 54.5 63.5 50 69 45.5 63.5Z"),
         G::XS("M43.5 21A7 7 0 0 1 47 16", 2), G::XS("M31 93.2H69", 1.8)});
  // Гроб: кайма, крест с почками на концах, заклёпки.
  {
    const std::string cross = rrect(46.8, 24, 6.4, 44, 1.5) + rrect(36, 34.3, 28, 6.4, 1.5) + circ(50, 22, 4.4) + circ(35, 37.5, 4.4) +
                              circ(65, 37.5, 4.4) + "M50 64 44.5 74H55.5Z";
    c.add("vampire-coffin", "Гроб",
          {G::F("M37 4H63L78 26 67 96H33L22 26Z"), G::XS("M38.5 9.5H61.5L72.5 26.5 62.5 90.5H37.5L27.5 26.5Z", 2.4), G::X(cross),
           G::X(circ(41.5, 15, 1.8) + circ(58.5, 15, 1.8) + circ(32, 27.5, 1.8) + circ(68, 27.5, 1.8) + circ(40.5, 85.5, 1.8) + circ(59.5, 85.5, 1.8))});
  }
  // Роза с каплей крови: бутон из лепестков, стебель с шипами и листьями, падающие капли.
  {
    const std::string bud = "M36 33C34 19 41 9 50 6C59 9 66 19 64 33Z";
    const std::string petals = sym("M50 56C36.5 56 25.5 48 23 35C22 29 20.5 24 16.5 19C25.5 19.5 33.5 23 38.5 29.5C42.5 35 46.5 40 50.5 42.5V56Z");
    const std::string sepals = sym("M50 52C44 55 38 58 31 58C36 54 42 50 49 49Z");
    const std::string leaves = feather({48.5, 71}, {27, 64}, 12) + feather({48, 83}, {71, 77}, 12);
    const std::string stem = taper({50, 53}, {53, 68}, {44, 81}, {48, 97}, 5, 4.2) + "M51.6 61 57 58.5 51.9 64.5ZM45.6 77 40 76 46 80.5Z";
    c.add("vampire-rose", "Роза с каплей крови",
          {G::F(bud), G::XS("M44.5 32C42 23 46 15.5 53 14.5C58 15 59.5 21 55.5 24.5M50 32C49.5 27 51 24 54 23", 1.8), G::XS(petals, 2.6),
           G::F(petals + sepals + stem + leaves), G::XS(sym("M29 31C33 40 40 46 47 48"), 1.6), G::XS("M45 70.5 31 65.5M51.5 82.5 67 78", 1.6),
           G::F(drop(77, 50, 60, 5) + drop(80, 70, 77, 3.5))});
  }
  // Герб с летучими мышами: щит с каплей крови, по сторонам щита — две летучие мыши.
  {
    const std::string shield = "M50 26 75 32V54C75 72 64 84 50 92C36 84 25 72 25 54V32Z";
    const Bat b = batHalf();
    const std::string bat = sym(b.fill) + circ(50, 38, 8) + ell(50, 53, 7.5, 13);
    const Affine bx = place(22, 22, 20, 0.44f) * Affine::translate(-50, -45);
    c.add("vampire-crest", "Герб с летучими мышами",
          {G::F(sym(xf(bat, bx))), G::X(sym(xf(sym(b.cuts), bx))),
           G::XS(shield, 4), G::F(shield), G::XS("M50 33.5 68 38V54C68 68 60 78 50 84.5C40 78 32 68 32 54V38Z", 2.2),
           G::X(drop(50, 40, 64, 9.5)), G::F(drop(50, 50, 65, 5))});
  }
  // Луна и замок: готический замок со шпилями на утёсе, полумесяц и летучие мыши.
  {
    const std::string castle = "M4 97C9 91 14 87 20 85L26 81 33 83 41 79H59L67 83 74 81 80 85C86 87 91 91 96 97Z" +
                               std::string("M42 80V34H40L50 9 60 34H58V80Z") + "M24 82V48H22L29 29 36 48H34V82Z" + "M66 82V45H64L71 27 78 45H76V82Z" +
                               "M34 82V60H36V57H38.5V60H40V57H42V82ZM58 82V57H60V60H62V57H64.5V60H66V82Z" + "M13 86V64H11.5L16.5 51 21.5 64H20V86Z";
    const std::string moon = circ(78, 20, 17);
    const std::string bats = "M14 22Q18 18 21 21Q24 18 28 22Q24 21 21 24Q18 21 14 22ZM24 8Q27 5 29.5 7.5Q32 5 35 8Q32 7.5 29.5 10Q27 7.5 24 8Z";
    c.add("vampire-castle", "Луна и замок",
          {G::F(moon), G::X(circ(85, 15, 15)), G::XS(castle, 3.5), G::F(castle + bats),
           G::X("M46 80V71A4 4 0 0 1 54 71V80Z" + rrect(48, 40, 4, 9, 2) + rrect(27.5, 55, 3, 7, 1.5) + rrect(69.5, 52, 3, 7, 1.5) +
                rrect(15.2, 69, 2.6, 6, 1.3)),
           G::XS("M42 64H58M24 68H36M64 66H76", 1.6)});
  }
  // Плащ с высоким воротником: острые концы воротника, складки подкладки, зубчатый подол, застёжка-медальон.
  {
    const std::string cape = "M50 31L42.5 25C38 19 34 11 30 3C26 14 22 22 16 28C12 32 8 36 6.5 42C4 60 5 80 7 96L15.5 89.5 22.5 97 30 90 36.5 97"
                             " 43.5 90.5 50.5 97V31Z";
    c.add("vampire-collar", "Плащ с воротником",
          {G::F(sym(cape)), G::XS(sym("M40.5 27.5C37 21.5 34 15 31.5 9M37.5 29C35 24 32 19 29.5 14.5"), 1.6),
           G::XS(sym("M15 38C14 56 14 74 15.5 89M27 36C25 54 25 72 26.5 91M39 36C38 52 38 70 39.5 91"), 1.8),
           G::XS(circ(50, 35, 7.5), 2.6), G::F(circ(50, 35, 6.2)), G::X("M50 30.5 54 35 50 39.5 46 35Z"), G::XS(sym("M42 32.5 34 30"), 1.6)});
  }
  // Вампир: лицо с острым подбородком, волосы мысом, острые уши, раскосые глаза, клыки, высокий воротник.
  {
    const std::string face = "M50 17C63 17 70 27 70 41C70 56 64 69 58 77L50 83 42 77C36 69 30 56 30 41C30 27 37 17 50 17Z";
    const std::string hair = "M29 41C27 22 37 9 50 9C63 9 73 22 71 41C68 33 62 28 56 27L50 37 44 27C38 28 32 33 29 41Z";
    const std::string ears = sym(feather({31.5, 45}, {16, 30}, 8.5));
    c.add("vampire-lord", "Вампир",
          {G::F(sym("M38 72 24 64 6 30 13 72 42 94Z") + "M12 99C18 87 33 81 50 81C67 81 82 87 88 99Z"), G::XS(face + hair + ears, 2.6),
           G::F(face + hair + ears), G::XS("M29.5 40C32 33 38 28.5 44 27.5L50 37 56 27.5C62 28.5 68 33 70.5 40", 2),
           G::X(sym("M37.5 46 46 49.5 39 51.5Z") + "M41 65C46 66.5 54 66.5 59 65C56 69.5 53 71 50 71C47 71 44 69.5 41 65Z"),
           G::F(sym("M44 65.6 46.2 72 47.8 66.2Z")), G::XS(sym("M35.5 41.5 46 45.5") + "M50 50 48 59.5 51.5 60.5", 1.6),
           G::XS(sym("M33 47 22 36"), 1.5)});
  }
}

// ---------------------------------------------------------------- природа
void nature(Catalog& c) {
  c.group("Природа");
  // Дубовый лист: лопасти на кривых Безье, сужающаяся средняя жилка, боковые жилки, черешок.
  {
    const std::string half = "M50 5C45 6 40.5 9.5 39.5 14.5C35 14.5 31.5 18 32.5 22.5C33.5 26 38 27 41 26.5C34 28.5 24.5 31 24.5 38.5C25 44 32 45 38 42.5"
                             "C30 46.5 20.5 50 21.5 57.5C22.5 63.5 31 63.5 37 60.5C31 65 25.5 70 27.5 75.5C30 80.5 38 79.5 43 76C44.5 80 46.5 83 48.6 85"
                             "V97H50.5V5Z";
    const std::string veins = "M50 22 40 18.5M50 35 33 33M50 49 30 52M50 63 34 70";
    c.add("nature-leaf", "Дубовый лист",
          moved({G::F(sym(half)), G::X(taper({50, 86}, {50, 60}, {50, 35}, {50, 12}, 3.2, 0.8)), G::XS(sym(veins), 1.7)},
                rotAt(32, 50, 50) * scaleAt(1.02f, 50, 50)));
  }
  // Олень: голова анфас, ветвистые рога с отростками, уши, глаза, тёмный нос.
  {
    const Pt a0{45, 42}, a1{36, 31}, a2{24, 24}, a3{19, 5};
    std::string antler = taper(a0, a1, a2, a3, 7.5, 2.6);
    const double ts[] = {0.3, 0.55, 0.78};
    const Pt tips[] = {{39, 14}, {11, 22}, {28, 6}};
    for (int k = 0; k < 3; k++) {
      const CurvePt q = bezier(a0, a1, a2, a3, ts[k]);
      const Pt b{float(q.x), float(q.y)};
      antler += taper(b, lerp(b, tips[k], 0.45) + Pt{0, -1.5f}, lerp(b, tips[k], 0.8), tips[k], 5.2, 1.6);
    }
    const std::string head = "M50 38C43 38 38 41.5 37 47.5C36.5 53 39 60 41.5 68C43.5 76 44.5 84 45.5 89C46.5 93 48 95 50 95H50.5V38Z";
    c.add("nature-stag", "Олень",
          {G::F(sym(antler + head + feather({40.5, 47}, {19, 40}, 11))), G::X(sym(feather({38, 46.5}, {25, 42}, 4))),
           G::X(sym(xf(ell(0, 0, 3.4, 2), place(43, 58, 28)))), G::X(ell(50, 89.5, 4.6, 3.2)), G::XS("M50 92.5V96", 1.6),
           G::XS(sym("M45.5 69C46.5 75 47.5 80 48 84"), 1.6), G::XS("M50 41V52", 1.6)});
  }
  // Мухомор: пятнистая шляпка, пластинки, юбочка на ножке, клубень, трава и маленький гриб рядом.
  c.add("nature-mushroom", "Мухомор",
        {G::F("M6 52C6 27 26 9 48 9C70 9 90 27 90 52C76 58 62 60 48 60C34 60 20 58 6 52Z" + std::string("M37 56C37 68 35 79 32 88C42 92 54 92 64 88C61 79 59 68 59 56Z") +
              ell(48, 89, 19, 6) + taper({10, 97}, {12, 90}, {12, 84}, {9, 77}, 4, 0.8) + taper({16, 97}, {17, 91}, {20, 86}, {24, 82}, 3.6, 0.8) +
              "M72 80C72 72 78 67 85 67C92 67 97 72 97 80Z" + rrect(82, 78, 6, 15, 2) + ell(85, 93, 8, 3)),
         G::XS("M28 59Q48 66 68 59", 3), G::XS("M34 70C42 75 54 75 62 70L63.5 76C54 80 42 80 32.5 76Z", 2),
         G::X(circ(28, 29, 5) + circ(48, 20, 6) + circ(68, 30, 5.5) + circ(38, 42, 4.5) + circ(60, 46, 4) + circ(18, 45, 3.5) + circ(80, 46, 3.5) +
              circ(80, 73.5, 1.8) + circ(89, 72, 1.5)),
         G::XS("M74 80H96", 1.6)});
  // Ель: ярусы с провисающими лапами внахлёст, ствол с корнями.
  {
    // ярус: слегка вогнутые бока, нижний край — пологая дуга вверх, концы лап чуть опущены
    auto tier = [](double y0, double y1, double w) {
      return "M50 " + n(y0) + "C" + n(50 + w * 0.25) + " " + n(y0 + (y1 - y0) * 0.45) + " " + n(50 + w * 0.65) + " " + n(y1 - 2) + " " + n(50 + w) + " " +
             n(y1) + "C" + n(50 + w * 0.6) + " " + n(y1 - 9) + " " + n(50 - w * 0.6) + " " + n(y1 - 9) + " " + n(50 - w) + " " + n(y1) + "C" +
             n(50 - w * 0.65) + " " + n(y1 - 2) + " " + n(50 - w * 0.25) + " " + n(y0 + (y1 - y0) * 0.45) + " 50 " + n(y0) + "Z";
    };
    const std::string t1 = tier(3, 35, 19), t2 = tier(18, 60, 30), t3 = tier(36, 85, 42);
    c.add("nature-pine", "Ель",
          {G::F(t3 + "M44.5 74H55.5L56.5 87C59 90.5 63.5 92.5 69 94H31C36.5 92.5 41 90.5 43.5 87Z"), G::XS(t2, 3.2), G::F(t2), G::XS(t1, 3.2), G::F(t1),
           G::XS("M50 82V88", 1.6)});
  }
  // Дуб с корнями: пышная крона с ветвями, ствол, линия земли и раскидистые корни.
  {
    const std::string crown = circ(50, 25, 18) + circ(31, 30, 15) + circ(69, 30, 15) + circ(19, 44, 12) + circ(81, 44, 12) + circ(35, 46, 15) +
                              circ(65, 46, 15) + circ(50, 42, 17);
    const std::string trunk = "M43.5 52C45 59 45 66 43 74H57C55 66 55 59 56.5 52Z";
    const std::string roots = taper({46, 75}, {40, 80}, {26, 82}, {10, 91}, 7, 1.4) + taper({47, 76}, {42, 84}, {36, 90}, {29, 98}, 6, 1.3) +
                              taper({49, 77}, {48, 85}, {46, 91}, {43, 98}, 5, 1.2);
    c.add("nature-oak", "Дуб с корнями",
          {G::F(crown + trunk + sym(roots)), G::XS("M50 62V36M50 50 39 40M50 45 61 35M46 55 31 47M54 55 69 47M39 40 33 32M61 35 66 27", 3),
           G::XS("M28 18Q33 16 37 19M60 15Q65 13 69 16M14 38Q18 36 22 38M78 37Q82 35 86 37", 1.6), G::XS("M8 76.5H92", 2.2),
           G::XS("M48 66 46 71M53 58 54 63", 1.5)});
  }
  // Воющий волк: сидящий волк на камне, морда к полумесяцу, шерсть на груди, хвост по земле.
  {
    const std::string wolf = "M12 13L19 9.5C23 8.5 26.5 8.8 29 10L32 1.5 35.5 11C38.5 14 40.5 19 42 25C47 32 55 41 59.5 51C64 60 67.5 69 67 78"
                             "C70 83 76 86 84 87.5C89.5 88.5 94 90 97 94C90 96 82 95.5 74 93.5L66 93H28C26 93 25.5 91.5 27 90.5L30 89C30 82 29.5 74 29 67"
                             "C27 57 25 46 23.5 36C22.5 30 21 25.5 19.5 21.5L13 16.5 18 16.8Z";
    const std::string rock = "M14 93H90L95 97.5H9Z";
    c.add("nature-wolf", "Воющий волк",
          moved({G::F(circ(78, 25, 19)), G::X(circ(70, 21, 16.5)), G::XS(wolf, 3), G::F(wolf + rock), G::X("M23 12.8 27.5 13.4 24.3 15Z"),
                 G::XS("M53 61C47 69 46 81 51 93M38 62C37 72 37.5 82 39 93", 1.8),
                 G::XS("M24 37 27.5 39.5M25.5 45 29 47M27 53 30.5 55M93 92.5C87 91 81 91 75 92", 1.6), G::XS("M14 93H90", 1.6)},
                Affine::translate(-2, 2)));
  }
  // Сова: большие глаза с лицевым диском, ушные пучки, клюв, перья на груди, крылья, ветка с когтями.
  {
    const std::string body = "M50 20C66 20 77 33 77 52C77 72 66 85 50 87C34 85 23 72 23 52C23 33 34 20 50 20Z" +
                             sym("M33 27C29 21 27 15 27 8C33 13 38 18 42 22Z");
    c.add("nature-owl", "Сова",
          {G::F(body + taper({3, 91}, {30, 88}, {70, 93}, {97, 87}, 7, 4) + feather({20, 89}, {11, 79}, 7)),
           G::XS(sym(circ(38.5, 41, 11.5)), 2), G::X(sym(circ(38.5, 41, 8))), G::F(sym(circ(38.5, 41, 4.6))), G::X(sym(circ(40, 39.5, 1.5))),
           G::XS("M47 50 50 55 53 50M50 55V60", 1.6), G::XS(sym("M30.5 52C31 64 36 75 44 82M27 58C28 66 31 72 35 77"), 1.8),
           G::XS(scaleRows(50, 68, 11, 10, 7, 3.2, 5.5), 1.5), G::XS(sym("M44 85.5 43 92M47.5 86 47.5 92.5"), 1.6)});
  }
  // Медведь-друид: голова анфас в венке из листьев, лист на лбу, морда с носом.
  {
    std::string wreath, veins;
    for (int k = 0; k < 7; k++) {
      const double a = -160 + k * 23.3;
      const Pt b = polar(50, 56, 28, a), t = polar(50, 56, 45, a);
      wreath += feather(b, t, 10.5);
      veins += "M" + ptS(lerp(b, t, 0.45)) + "L" + ptS(lerp(b, t, 0.85));
    }
    const std::string head = "M50 27C68 27 80 39 80 56C80 72 70 84 58 89L50 93 42 89C30 84 20 72 20 56C20 39 32 27 50 27Z" + circ(25, 33, 10) +
                             circ(75, 33, 10);
    c.add("nature-bear", "Медведь-друид",
          {G::F(wreath), G::XS(veins, 1.6), G::XS(head, 3), G::F(head), G::X(circ(25, 33, 5) + circ(75, 33, 5)), G::XS(ell(50, 74, 14, 11.5), 2.2),
           G::X("M43 66C43 63 57 63 57 66C57 70 53 73 50 73C47 73 43 70 43 66Z" + circ(38, 54, 3) + circ(62, 54, 3) +
                "M50 35C54 39 54.5 45 50 49C45.5 45 46 39 50 35Z"),
           G::XS("M50 73V78M44 80Q50 84 56 80", 1.8)});
  }
  // Папоротник: изогнутый стебель с перистыми листочками и закрученной улиткой на конце.
  {
    const Pt r0{47, 97}, r1{45, 68}, r2{40, 40}, r3{55, 22};
    std::vector<std::string> pinnae;
    for (int k = 8; k >= 0; k--) {
      const double t = 0.07 + 0.096 * k, len = 37 * (1 - 0.7 * t / 0.84);
      const CurvePt q = bezier(r0, r1, r2, r3, t);
      const Pt b{float(q.x), float(q.y)}, up{float(q.ny), float(-q.nx)};
      for (int side : {-1, 1}) {
        const Pt dir{float(q.nx * side * 0.82 + up.x * 0.57), float(q.ny * side * 0.82 + up.y * 0.57)};
        pinnae.push_back(feather(b, b + dir * float(len), len * 0.36));
      }
    }
    std::vector<L> v;
    layeredFeathers(v, pinnae, 1.8f, false);
    const std::string stem = taper(r0, r1, r2, r3, 5, 3.2) + taperArc(62, 19, 7.5, 180, 470, 4.5, 2.4) + circ(59.5, 17, 3.2);
    v.push_back(G::XS(stem, 1.6));
    v.push_back(G::F(stem));
    c.add("nature-fern", "Папоротник", moved(v, Affine::translate(5, -1.5f)));
  }
}

// ---------------------------------------------------------------- стихии
// Полоса вдоль ломаной c (плотная выборка кривой), ширина по точкам w.
std::string strip(const std::vector<Pt>& c, const std::vector<double>& w) {
  std::vector<Pt> lft, rgt;
  for (size_t i = 0; i < c.size(); i++) {
    const Pt a = c[i > 0 ? i - 1 : 0], b = c[i + 1 < c.size() ? i + 1 : i];
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(1e-6, std::hypot(dx, dy)), h = w[i] * 0.5;
    lft.push_back({float(c[i].x - dy / len * h), float(c[i].y + dx / len * h)});
    rgt.push_back({float(c[i].x + dy / len * h), float(c[i].y - dx / len * h)});
  }
  std::string s = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) s += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) s += "L" + ptS(rgt[i]);
  return s + "Z";
}

// Дуга-взмах вдоль кубической кривой: толще всего посередине (wMid), к концам — wEnd.
std::string swoosh(Pt p0, Pt c1, Pt c2, Pt p1, double wMid, double wEnd, int steps = 30) {
  std::vector<Pt> c;
  std::vector<double> w;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps;
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    c.push_back({float(q.x), float(q.y)});
    w.push_back(wEnd + (wMid - wEnd) * std::sin(kPi * t));
  }
  return strip(c, w);
}

// Логарифмическая спираль: центр, угол a0 → a1 (градусы), радиус r0 → r1, ширина w0 → w1.
std::string spiral(double cx, double cy, double a0, double a1, double r0, double r1, double w0, double w1, int steps = 48) {
  std::vector<Pt> c;
  std::vector<double> w;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps;
    c.push_back(polar(cx, cy, r0 * std::pow(r1 / r0, t), a0 + (a1 - a0) * t));
    w.push_back(w0 + (w1 - w0) * t);
  }
  return strip(c, w);
}

// Язык пламени: основание (bx, by) шириной w, кончик (tx, ty), изгиб bend (сдвиг середины вбок).
std::string flameTongue(double bx, double by, double tx, double ty, double w, double bend) {
  return taper({float(bx), float(by)}, {float(bx + (tx - bx) * 0.3 + bend), float(by + (ty - by) * 0.35)},
               {float(bx + (tx - bx) * 0.75 - bend * 0.6), float(by + (ty - by) * 0.7)}, {float(tx), float(ty)}, w, 1);
}

void elements(Catalog& c) {
  c.group("Стихии");
  // Огонь: огненный шар с тремя хвостами, полосы движения, внутреннее пламя с ядром, искры.
  {
    const std::string inner = "M42 44C46 52 55 57 55 66C55 74 49 79 42 79C35 79 29 74 29 66C29 60 33 56 35 50C37 54 40 55 42 56C41 52 41 48 42 44Z";
    c.add("element-fire", "Огонь",
          moved({G::F(circ(42, 60, 25) + taper({48, 42}, {56, 28}, {68, 16}, {86, 6}, 26, 2) + taper({60, 50}, {70, 40}, {82, 34}, {96, 32}, 22, 2) +
                      taper({32, 42}, {36, 28}, {44, 16}, {56, 6}, 16, 2) + circ(92, 16, 2.6) + circ(70, 4, 2)),
                 G::XS(taper({48, 42}, {56, 28}, {68, 16}, {86, 6}, 26, 2), 2.2), G::F(taper({48, 42}, {56, 28}, {68, 16}, {86, 6}, 26, 2)),
                 G::F(circ(42, 60, 25)), G::X(xf(inner, scaleAt(0.74f, 42, 66))), G::F(xf(inner, scaleAt(0.4f, 42, 70)))},
                Affine::translate(-6, 4) * scaleAt(1.05f, 50, 50)));
  }
  // Вода: капля с бликом, волны и брызги.
  c.add("element-water", "Вода",
        {G::F(drop(50, 4, 62, 30) + drop(14, 30, 41, 5.5) + drop(86, 30, 41, 5.5)),
         G::XS("M27 62C32 57 38 57 43 62S54 67 59 62S70 57 74 62M30 75C35 70 40 70 45 75S55 80 60 75S69 70 71 74", 3.6),
         G::X(taper({36, 34}, {30, 40}, {27.5, 47}, {28, 54}, 3.6, 2.4)), G::X(circ(33.5, 59, 1.9))});
  // Воздух: три порыва ветра — сужающиеся полосы, закрученные на концах.
  {
    // порыв: прямая от x0 до начала спирали (центр cx, cy; up — завиток вверх), ширина растёт к завитку и спадает к концу
    auto gust = [](double x0, double cx, double cy, double r, bool up, double turn, double w) {
      std::vector<Pt> pts;
      std::vector<double> ws;
      const double y = up ? cy + r : cy - r;
      for (int i = 0; i <= 12; i++) {
        pts.push_back({float(x0 + (cx - x0) * i / 12), float(y)});
        ws.push_back(2.5 + (w - 2.5) * std::pow(i / 12.0, 0.7));
      }
      for (int i = 1; i <= 40; i++) {
        const double t = i / 40.0, a = up ? 90 - turn * t : -90 + turn * t;
        pts.push_back(polar(cx, cy, r * (1 - 0.5 * t), a));
        ws.push_back(w - (w - 3) * t);
      }
      return strip(pts, ws);
    };
    c.add("element-air", "Воздух",
          moved({G::F(gust(8, 58, 20, 12, true, 320, 9) + gust(4, 76, 64.5, 12.5, false, 300, 9.5) + gust(16, 47, 82.5, 9.5, false, 280, 8))},
                Affine::translate(4, -1)));
  }
  // Земля: горы со снежными шапками, хребты, скальные уступы, подножие.
  c.add("element-earth", "Земля",
        {G::F("M2 90L24 44 33 56 50 12 66 46 76 34 98 90Z"),
         G::XS("M40 38 45 33 50 39 55 33 60 38M19.5 55 22 52 25 56 28 52.5 31 55.5M68 45 71 42 74 46 77 42 80 45", 3),
         G::XS("M50 44 45 56 48 66 44 78M31 64 28 74M70 54 74 64 72 74M60 62 63 72M22 70 19 80", 1.8), G::XS("M6 83H94", 2.2)});
  // Молния: грозовая туча с завитками и разящая молния.
  {
    const std::string cloud = circ(30, 30, 13) + circ(50, 22, 17) + circ(71, 29, 14) + rrect(14, 28, 72, 17, 8.5);
    c.add("element-lightning", "Молния",
          {G::F("M57 38L33 70H48L38 98 73 58H58L69 38Z"), G::XS(cloud, 3.2), G::F(cloud),
           G::XS("M24 26A9 9 0 0 1 34 20M42 17A11 11 0 0 1 56 12M65 23A9 9 0 0 1 76 21", 2), G::XS("M18 39H82", 1.6)});
  }
  // Лёд: снежинка с ветвями, ромбами на концах и шестиугольником в центре.
  {
    std::string s, tips;
    for (int k = 0; k < 6; k++) {
      const double a = -90 + 60 * k;
      s += "M" + pt(50, 50, 9, a) + "L" + pt(50, 50, 38, a);
      for (auto [d, len] : {std::pair{19.0, 11.0}, std::pair{29.0, 8.0}}) {
        const Pt q = polar(50, 50, d, a);
        s += "M" + pt(q.x, q.y, len, a - 45) + "L" + ptS(q) + "L" + pt(q.x, q.y, len, a + 45);
      }
      tips += "M" + pt(50, 50, 37, a - 6) + "L" + pt(50, 50, 47.5, a) + "L" + pt(50, 50, 37, a + 6) + "L" + pt(50, 50, 33, a) + "Z";
    }
    c.add("element-frost", "Лёд", {G::S(s, 5.5), G::F(tips + star(50, 50, 13, 13, 6, -90)), G::X(star(50, 50, 7.5, 7.5, 6, -90)), G::F(circ(50, 50, 3.2))});
  }
  // Вулкан: конус с потоками лавы, извержение языками и вылетающие камни.
  {
    const std::string cone = "M3 97L31 50C35 45 39 44 43 45.5L50 49 57 45.5C61 44 65 45 69 50L97 97Z";
    const std::string plume = flameTongue(46, 47, 25, 14, 10, -7) + flameTongue(50, 47, 49, 3, 11, 6) + flameTongue(54, 47, 75, 14, 10, 7);
    c.add("element-magma", "Вулкан",
          {G::F(plume + circ(17, 26, 3.4) + circ(83, 26, 3.4) + circ(34, 6, 2.6) + circ(66, 6, 2.6)), G::X(sym(flameTongue(47.5, 40, 38, 22, 2.6, -1))),
           G::XS(cone, 3), G::F(cone), G::XS("M44 50C40 60 43 68 37 78C33 85 29 90 25 97M56 50C60 62 57 71 63 81C66 87 69 91 71 97M50 52V64", 2.4),
           G::XS("M18 85 24 82M80 84 76 80", 1.6)});
  }
  // Смерч: воронка из закрученных полос, сужается книзу, вокруг — обломки.
  {
    std::string bands;
    for (int i = 0; i < 7; i++) {
      const double y = 10 + i * 12.2, w = 41 - i * 5.6, x = 50 + std::sin(i * 0.95) * 3.5;
      bands += swoosh({float(x - w), float(y)}, {float(x - w * 0.5), float(y + 6.5)}, {float(x + w * 0.5), float(y + 6.5)}, {float(x + w), float(y - 1)},
                      7.5 - i * 0.45, 2);
    }
    bands += taper({52, 86}, {50, 90}, {47, 93}, {44, 97}, 4, 1.2);
    c.add("element-tornado", "Смерч",
          {G::F(bands + xf(rrect(-2.5, -2.5, 5, 5, 1), place(14, 40, 30)) + xf(rrect(-2, -2, 4, 4, 1), place(86, 56, 15)) +
                xf(rrect(-2, -2, 4, 4, 0.8), place(20, 70, 50)) + feather({82, 28}, {90, 22}, 4))});
  }
  // Волна: закрученный гребень с когтями пены над морем.
  {
    const std::string sea = "M2 97V84C12 80 24 80 34 83C46 86 60 82 70 72C76 66 82 60 90 58L98 64V97Z";
    std::string claws;
    for (int k = 0; k < 5; k++) {
      const double a = -150 + k * 26;
      const Pt b = polar(44, 40, 34, a);
      claws += taper(b, polar(44, 40, 40, a - 6), polar(44, 40, 41, a - 16), polar(44, 40, 38, a - 26), 5, 0.8);
    }
    c.add("element-wave", "Волна",
          moved({G::F(sea + spiral(44, 40, 25, -300, 44, 10, 20, 4) + claws), G::XS(spiral(44, 40, 10, -250, 36, 15, 1.6, 1.6), 1.8),
                 G::XS("M8 90C18 87 28 88 36 91M50 92C62 90 74 86 84 79", 1.8)},
                Affine::translate(0, 1.5f) * scaleAt(0.98f, 50, 50)));
  }
  // Кристалл льда: высокая шестигранная призма с гранями, две боковые призмы, искра.
  {
    const std::string prism = "M0 -1L1 -0.7V0.7L0 1-1 0.7V-0.7Z";
    const std::string mid = xf(prism, Affine{12, 0, 0, 44, 50, 48});
    const std::string side = xf(prism, place(27, 62, -28) * Affine::scale(8, 25)) + xf(prism, place(74, 64, 26) * Affine::scale(7.5, 22));
    c.add("element-crystal", "Кристалл льда",
          {G::F(side), G::XS(xf("M0 -25L0 18", place(27, 62, -28)) + xf("M0 -22L0 16", place(74, 64, 26)), 1.6), G::XS(mid, 3.2), G::F(mid),
           G::XS("M38 17.2 50 25 62 17.2M50 25V80M38 78.8 50 86 62 78.8", 1.8),
           G::F(star(84, 16, 9, 2.4, 4, -90) + star(15, 30, 6, 1.6, 4, -90) + "M20 96H80L76 92H24Z")});
  }
  // Стихийный круг: кольцо, внутри — огонь, вода, воздух и земля вокруг звезды.
  {
    const std::string flame = "M50 12C53 17 58 21 58 27A8 8 0 0 1 42 27C42 23 45 21 46.5 18C47.5 21 49 22 50 23C49.5 19 49.5 15 50 12Z";
    c.add("element-circle", "Стихийный круг",
          {G::S(circ(50, 50, 44), 6), G::F(flame + drop(50, 70, 81, 7.5) + "M66 58L76 40 86 58Z" + spiral(25, 49, 180, -150, 9, 3.5, 4.5, 2) +
                                            star(50, 50, 13, 4, 4, -90)),
           G::X("M73 45.3 76 40 79 45.3 77.5 44 76 46 74.5 44Z" + circ(50, 28, 3.2) + circ(50, 50, 2.2))});
  }
}

// ---------------------------------------------------------------- гномы
// Руны (футарк) для гравировки: локальные координаты, высота 12, центр в (0, 0).
const char* const kRunes[] = {"M-2 -6V6M-2 -6 3 -3M-2 -1 3 2", "M-3 6V-6L3 -2V6", "M-2 -6V6M-2 -3 2 0-2 3", "M-3 6V-6L2 -3-3 0 3 6",
                              "M0 6V-6M0 0-4 -5M0 0 4 -5", "M0 -6 4 -2-3 5M0 -6-4 -2 3 5"};

// Кирка остриём вверх: изогнутый боёк с остриями, втулка, рукоять; вырезы — кольцо втулки и обмотка.
const std::string kPickHead = "M8 33C22 19 36 13 50 13C64 13 78 19 92 33C78 26 64 22 50 22C36 22 22 26 8 33Z";
const std::string kPick = kPickHead + rrect(42.5, 9, 15, 17, 3) + rrect(46.5, 20, 7, 72, 3) + "M45 90H55L53 97H47Z";
const std::string kPickCuts = "M43 17.5H57M46 77H54M46 81.5H54M46 86H54";

void dwarves(Catalog& c) {
  c.group("Гномы");
  // Наковальня: рог, отверстие, руна на стане, колода с обручем, искры.
  {
    const std::string sparks = star(38, 9.5, 6, 1.6, 4, -90) + star(52, 6, 4.5, 1.3, 4, -90) + star(64, 11.5, 5, 1.4, 4, -90);
    c.add("dwarf-anvil", "Наковальня",
          {G::F(xf("M4 32C14 27 22 26 30 26H94V39C86 40 80 42 74 45C68 48 64 53 63 59L68 72H78V84H26V72H36L39 59C38 52 32 46 24 44"
                   "C16 42 9 38 4 32Z",
                   Affine::translate(0, -1)) +
                rrect(22, 86, 60, 11, 2.5) + sparks),
           G::XS("M30 31.5H93.5", 2.2), G::X(rrect(80, 28, 5, 4.5, 1) + "M51 50 56 57 51 64 46 57Z"), G::XS("M26 75.5H78M22 91.5H82", 2),
           G::XS("M51 54V60", 1.6)});
  }
  // Скрещённые кирки: изогнутые бойки, втулки с кольцом, рукояти с обмоткой и навершием.
  c.add("dwarf-pickaxes", "Скрещённые кирки", crossed(kPick, circ(50, 13, 1.8), kPickCuts, 1.6, 38, 50, 54, 0.96f));
  // Гном: рогатый шлем с пластинами и заклёпками, наносник, глаза, усы, борода с косами в кольцах.
  {
    const std::string horn = taper({27, 30}, {19, 27}, {13, 21}, {11, 9}, 9, 1.5);
    c.add("dwarf-face", "Гном",
          {G::F("M24 40C24 20 36 8 50 8C64 8 76 20 76 40Z" + rrect(18, 35, 64, 9, 3) + sym(horn) +
                "M22 46H78L80 62C80 78 70 88 58 93L50 97 42 93C30 88 20 78 20 62Z"),
           G::X(ell(37.5, 51, 5, 2.6) + ell(62.5, 51, 5, 2.6) + circ(24, 39.5, 1.6) + circ(76, 39.5, 1.6) + circ(37, 39.5, 1.5) + circ(63, 39.5, 1.5)),
           G::XS("M50 11V34M37 13C34 20 33 28 33 34M63 13C66 20 67 28 67 34", 2), G::XS("M46 45V56C46 60 48 62 50 62S54 60 54 56V45", 2.2),
           G::XS("M28 67C37 60 45 61 50 66C55 61 63 60 72 67", 3), G::XS("M50 71V86M41 72 39 84M59 72 61 84", 2.2),
           G::XS("M38 87.5H47M53 87.5H62M40 92H47M53 92H60", 1.8)});
  }
  // Врата в горе: арка с клинчатыми камнями, колонны, ступени, окованные створки, руна над входом.
  {
    std::string stones;
    for (int k = 1; k < 6; k++) stones += "M" + pt(50, 70, 14, 180 + 30 * k) + "L" + pt(50, 70, 19.5, 180 + 30 * k);
    c.add("dwarf-gate", "Врата в горе",
          {G::F("M3 92L38 28 45 36 56 10 97 92Z"), G::XS("M50 25 53 22 56 26 59 22 62.5 25", 3),
           G::X("M31 92V70C31 58 39 50.5 50 50.5C61 50.5 69 58 69 70V92Z" + std::string("M50 38 53.5 42.5 50 47 46.5 42.5Z")),
           G::F("M35.5 92V70C35.5 61 42 55 50 55C58 55 64.5 61 64.5 70V92Z"), G::XS(stones, 1.8),
           G::X("M40.5 92V70C40.5 63.5 44.5 59.5 50 59.5C55.5 59.5 59.5 63.5 59.5 70V92Z"),
           G::F("M42.5 92V70.5C42.5 65.5 45.5 61.5 50 61.5C54.5 61.5 57.5 65.5 57.5 70.5V92Z"), G::XS("M50 62V92M42 74H58M42 84H58", 1.8),
           G::XS("M22 87H78M18 92H82", 1.8)});
  }
  // Рунический щит: восьмигранный щит с окованной кромкой и заклёпками, крестовина оковки с рунами, умбон.
  {
    std::string runes, rivets;
    const Pt at[] = {{50, 24}, {76, 50}, {50, 76}, {24, 50}};
    for (int k = 0; k < 4; k++) runes += xf(kRunes[k == 0 ? 0 : k == 1 ? 3 : k == 2 ? 4 : 5], place(at[k].x, at[k].y, 0, 1.15f));
    for (int k = 0; k < 8; k++) rivets += circ(polar(50, 50, 42.5, -67.5 + 45 * k).x, polar(50, 50, 42.5, -67.5 + 45 * k).y, 1.8);
    c.add("dwarf-runeshield", "Рунический щит",
          {G::F(star(50, 50, 48, 48, 4, -67.5)), G::XS(star(50, 50, 39, 39, 4, -67.5), 2.4), G::X(rivets),
           G::XS("M42 12.5V37.5M58 12.5V37.5M42 62.5V87.5M58 62.5V87.5M12.5 42H37.5M12.5 58H37.5M62.5 42H87.5M62.5 58H87.5", 2),
           G::XS(runes, 2.2), G::XS(circ(50, 50, 13), 2.4), G::X(circ(50, 50, 4.2))});
  }
  // Топор с рунами: бородатый топор с гравировкой и фаской, обух, обмотка рукояти, навершие.
  {
    const std::string axe = "M46 15C36 14 24 10.5 14 5C7 18 5 34 9 48C11 53 13 56 16.5 59.5C24 54 34 46 46 40Z" + rrect(46, 6, 8, 86, 3) +
                            "M54 18H61L65 25.5 61 33H54Z" + "M50 0 54.5 7H45.5Z" + "M45 90H55L53 98H47Z";
    c.add("dwarf-axe", "Топор с рунами",
          moved({G::F(axe), G::XS("M17.5 10C12 22 10.5 36 13.5 50", 1.8), G::XS(xf(kRunes[0], place(29, 24, 0, 1.05f)) + xf(kRunes[4], place(28, 40, 0, 1.05f)), 2),
                 G::XS("M46 70H54M46 75H54M46 80H54M46 85H54M46 15H54M46 33H54", 1.6), G::X(circ(59, 25.5, 1.8))},
                rotAt(28, 50, 50) * scaleAt(0.93f, 50, 50) * Affine::translate(9, 0)));
  }
  // Вагонетка с рудой: клёпаный короб с обручем, груда камней с кристаллом, колёса, рельс со шпалами.
  {
    const std::string rocks = "M17 41 21 30 30 25.5 37 30 40 41Z" + std::string("M36 41 39 26 49 19.5 58 25 61 41Z") + "M58 41 61 29 71 26 79 32 83 41Z";
    const std::string crystal = "M47 22 50 6 53.5 22Z";
    const std::string cart = "M12 41H88L79 78H21Z" + rrect(8, 37, 84, 7, 2);
    c.add("dwarf-cart", "Вагонетка с рудой",
          moved({G::F(rocks), G::XS(crystal, 2.2), G::F(crystal + star(70, 12, 6, 1.6, 4, -90)), G::XS("M39 30 37 41M59 28 61 41", 2), G::XS(cart, 3),
                 G::F(cart), G::XS("M30 44 32 75M50 44V76M70 44 68 75", 1.8), G::X(circ(16, 40.5, 1.5) + circ(84, 40.5, 1.5) + circ(50, 40.5, 1.5)),
                 G::XS(circ(32, 82, 9) + circ(68, 82, 9), 2.6), G::F(circ(32, 82, 9) + circ(68, 82, 9) + rrect(4, 90, 92, 4.5, 1.5)),
                 G::X(circ(32, 82, 4.2) + circ(68, 82, 4.2)), G::F(circ(32, 82, 2) + circ(68, 82, 2)),
                 G::F(rrect(10, 94, 8, 4, 1) + rrect(46, 94, 8, 4, 1) + rrect(82, 94, 8, 4, 1))},
                Affine::translate(0, -2)));
  }
  // Бородатый шлем: шлем-личина с гребнем, налобником и нащёчниками, прорези глаз, кованая борода с косами.
  {
    const std::string helm = "M24 39C24 19 36 7 50 7C64 7 76 19 76 39Z" + rrect(19, 35, 62, 9, 3) + sym("M21 42 13 63 24.5 67 27 44Z") +
                             "M25 44H75V55C75 61 70 65 64 66H36C30 65 25 61 25 55Z";
    const std::string beard = "M27 64C26 79 35 91 50 98C65 91 74 79 73 64Z" + sym(taper({37, 84}, {33, 89}, {32, 93}, {34, 98.5}, 7, 5.5));
    c.add("dwarf-helm", "Бородатый шлем",
          {G::F(helm), G::XS("M50 9V35M42 10C39 17 38 26 38 35M58 10C61 17 62 26 62 35", 2),
           G::X(sym("M30 47.5H45.5L44.5 52.5H33C31.5 52 30.5 50 30 47.5Z") + sym(circ(24, 39.5, 1.6) + circ(36, 39.5, 1.6))),
           G::XS(sym("M22 48 18.5 61"), 1.6), G::XS(beard, 2.6), G::F(beard), G::XS("M35 61C42 55.5 47 56.5 50 60C53 56.5 58 55.5 65 61", 2.6),
           G::XS("M50 69V92M42 69 40 85M58 69 60 85" + sym("M31.5 88.5H38.5M32 93H38"), 1.8)});
  }
  // Кирка и самоцвет: огранённый камень с гранями поверх кирки, блёстки.
  {
    const std::string gem = "M30 42H70L82 55 50 92 18 55Z";
    const Affine px = rotAt(-42, 50, 50) * scaleAt(0.98f, 50, 50) * Affine::translate(0, -4);
    c.add("dwarf-gem", "Кирка и самоцвет",
          {G::F(xf(kPick, px)), G::X(xf(circ(50, 13, 1.8), px)), G::XS(xf(kPickCuts, px), 1.6), G::XS(gem, 4), G::F(gem),
           G::XS("M18 55H82M30 42 39 55 50 42 61 55 70 42M39 55 50 92 61 55", 1.8), G::X("M33 47 36 50.5H28Z"),
           G::F(star(84, 22, 7.5, 2, 4, -90) + star(14, 82, 6, 1.6, 4, -90))});
  }
}

// ---------------------------------------------------------------- эльфы
void elves(Catalog& c) {
  c.group("Эльфы");
  // Эльфийский лист: длинный лист с жилками, стебель с завитком и малым листком.
  {
    const std::string veins = "M50 28 59 20M50 42 62 33M50 56 62 47M50 68 59 61";
    c.add("elf-leaf", "Эльфийский лист",
          moved({G::F("M50 2C64 16 70 33 67 51C64 64 58 72 50 78C42 72 36 64 33 51C30 33 36 16 50 2Z" + feather({47, 85}, {58, 88}, 6)),
                 G::S("M50 76C50 88 41 95 33 92C27 90 27 83 33 81C36.5 80 38.5 83 36.5 85", 3.4), G::XS("M50 8C51 30 51 54 50 75", 2.2),
                 G::XS(sym(veins), 1.8), G::XS("M36.5 26C35 34 35 44 37 52", 1.6)},
                Affine::translate(8, 1) * rotAt(32, 50, 50) * scaleAt(1.1f, 50, 50)));
  }
  // Звезда эльфов: восемь лучей с гранями, кольцо и точки между лучами.
  {
    std::string s, facets, dots;
    for (int i = 0; i < 16; i++) {
      const double r = i % 2 ? 17 : (i % 4 == 0 ? 48 : 36);
      s += (i == 0 ? "M" : "L") + pt(50, 50, r, -90 + 22.5 * i);
      if (i % 4 == 0) facets += "M" + pt(50, 50, 14, -90 + 22.5 * i) + "L" + pt(50, 50, 34, -90 + 22.5 * i);
      else if (i % 2) dots += circ(polar(50, 50, 28.5, -90 + 22.5 * i).x, polar(50, 50, 28.5, -90 + 22.5 * i).y, 2.4);
    }
    s += "Z";
    c.add("elf-star", "Звезда эльфов", {G::F(s), G::XS(facets, 1.5), G::XS(circ(50, 50, 9.5), 2), G::X(circ(50, 50, 3)), G::XS(dots, 3), G::F(dots)});
  }
  // Венок: лавровые ветви с ягодами, звезда сверху, лента с концами.
  {
    std::string leaves, berries;
    for (double a = 108; a <= 234; a += 14) {
      const Pt p = polar(50, 52, 36, a);
      const double tdeg = a + 90;   // касательная по ходу ветви
      for (double side : {-34.0, 34.0}) {
        const double d = tdeg + side;
        const Pt cpt = polar(p.x, p.y, 7, d);
        leaves += xf(ell(0, 0, 3.6, 8.5), place(cpt.x, cpt.y, float(d - 90)));
      }
      if (int(a) % 28 == 24) berries += circ(polar(50, 52, 44.5, a + 7).x, polar(50, 52, 44.5, a + 7).y, 2.3);
    }
    c.add("elf-wreath", "Венок",
          {G::S(sym(arc(50, 52, 36, 100, 238)), 2.8),
           G::F(sym(leaves + berries + "M47 88 38 98 42 98.5 49 91Z") + star(50, 12, 9, 2.8, 4, -90) + "M42 85 58 93V85L42 93Z" + circ(50, 89, 3.2))});
  }
  // Лебедь: поднятое крыло из перьев внахлёст, глаз, рябь на воде.
  {
    const Pt tips[] = {{92, 54}, {88, 41}, {81, 29}, {71, 19}, {60, 12}};
    std::vector<std::string> feathers;
    for (int k = 0; k < 5; k++) feathers.push_back(feather({66 - 6.f * k, 64 - 0.5f * k}, tips[k], 13));
    feathers.push_back("M34 66C32 50 40 36 52 30C58 42 68 52 82 58C70 64 54 68 34 66Z");
    std::vector<L> v{G::F("M20 66C22 80 36 88 56 88C74 88 88 80 94 64C86 68 76 70 66 68C50 66 34 64 20 66Z" +
                          xf(ell(0, 0, 7, 5.5), place(22, 13, -10)) + "M16 11 4 15 16 17Z" + taper({8, 95}, {20, 92}, {32, 93}, {42, 95}, 1.5, 3) +
                          taper({50, 95}, {64, 92}, {80, 93}, {94, 95}, 3, 1.5)),
                     G::S("M28 68C16 54 16 40 24 30C30 22 30 15 24 12", 8), G::X(circ(23, 12, 1.4))};
    layeredFeathers(v, feathers, 2.2f, false);
    v.push_back(G::XS("M44 75C54 79 66 79 78 74", 1.6));
    c.add("elf-swan", "Лебедь", moved(v, Affine::translate(0, -1)));
  }
  // Лук и стрелы: изогнутый эльфийский лук с листьями на концах, тетива, две скрещённые стрелы.
  {
    auto arrow = [](Pt tail, Pt head) {
      const Pt d = head - tail;
      const float len = std::hypot(d.x, d.y);
      const Pt u = d * (1 / len), nrm{-u.y, u.x};
      std::string s = taper(tail, lerp(tail, head, 0.3), lerp(tail, head, 0.7), head - u * 9, 3.6, 3.6) + feather(head - u * 13, head, 10);
      for (float k : {0.f, 6.f}) {
        const Pt b = tail + u * (k + 1);
        s += "M" + ptS(b) + "L" + ptS(b + u * 8 + nrm * 5.5f) + "L" + ptS(b + u * 10.5f + nrm * 1.5f) + "Z";
        s += "M" + ptS(b) + "L" + ptS(b + u * 8 - nrm * 5.5f) + "L" + ptS(b + u * 10.5f - nrm * 1.5f) + "Z";
      }
      return s;
    };
    const std::string limb = taper({40, 50}, {36, 30}, {44, 13}, {60, 7}, 10, 3);
    const std::string bow = limb + xf(limb, Affine{1, 0, 0, -1, 0, 100}) + rrect(34.5, 41, 10, 18, 3.5) + feather({59, 7.5}, {69, 3}, 7) +
                            feather({59, 92.5}, {69, 97}, 7);
    c.add("elf-bow", "Лук и стрелы",
          {G::F(arrow({7, 71}, {95, 21}) + arrow({7, 29}, {95, 79})), G::XS(bow, 3), G::F(bow), G::S("M59.5 8V92", 2),
           G::XS("M34.5 46H44.5M34.5 50H44.5M34.5 54H44.5", 1.5), G::XS("M39 35C40.5 26 43.5 19 50 13.5M39 65C40.5 74 43.5 81 50 86.5", 1.6)});
  }
  // Лунный олень: стоящий олень в профиль с ветвистыми рогами, над спиной — полумесяц, звёзды.
  {
    const std::string deer = "M9 35C9 32 11 29.5 15 28.5L22 26.5 28 19 28.5 27C32 32 36.5 40 43 45C52 47 64 47 76 44.5L81.5 41.5 80 48"
                             "C81.5 53 81.5 59 79 63.5C80.5 70 81.5 80 83 93H79C78 83 76.5 75 74 69C71.5 66 69 65 66 65C58 66 50 66 43 64.5"
                             "C41.5 73 41 83 41.5 93H37.5C37 85 36.3 78 35.3 72L33.5 93H29.5C30 83 30.5 73 31 65C30 59 28.5 53 27 49"
                             "C24.5 44 22 40 20 37.5L12 38Z";
    const std::string hind = "M70 64C71.5 72 72 82 72.5 93H68.5C68 84 67 76 65 69Z";
    std::string antlers = taper({26.5, 24}, {22, 14}, {27, 6.5}, {39, 3}, 4.6, 1.6) + taper({24, 24.5}, {17.5, 17}, {12, 12.5}, {5, 12}, 4, 1.4);
    antlers += taper({24.5, 14.5}, {27, 10.5}, {30, 8}, {31, 3}, 3, 1.2) + taper({31, 5.5}, {34, 6.5}, {37, 9}, {40.5, 10.5}, 2.6, 1) +
               taper({17.5, 17}, {14, 14.5}, {12.5, 11}, {12.5, 5.5}, 3, 1.2);
    c.add("elf-deer", "Лунный олень",
          moved({G::F(circ(63, 22, 16)), G::X(circ(69, 16.5, 14)), G::F(hind), G::XS(deer, 2.2),
                 G::F(deer + antlers + star(88, 14, 6, 1.6, 4, -90) + star(46, 12, 4, 1.1, 4, -90)), G::X("M15.5 31 20 31.5 16.8 33.2Z"),
                 G::XS("M75 62C73 56 73 51 75.5 46.5M33 60C31.5 55 31 51 32 47", 1.6)},
                Affine::translate(3, 1.5f)));
  }
  // Рог: охотничий рог с оковкой и гравировкой листьев, перевязь с кистью.
  {
    const Pt h0{12, 86}, h1{34, 97}, h2{72, 66}, h3{80, 22};
    const CurvePt e = bezier(h0, h1, h2, h3, 1);
    const float ea = float(std::atan2(e.ny, e.nx) * 180 / kPi) - 90;   // направление касательной в конце
    const std::string horn = taper(h0, h1, h2, h3, 6, 27) + xf(ell(0, 0, 5, 15.5), place(float(e.x), float(e.y), ea));
    std::string bands;
    for (double t : {0.12, 0.5, 0.82}) {
      const CurvePt q = bezier(h0, h1, h2, h3, t);
      const double w = (6 + 21 * t) * 0.5 + 1;
      bands += "M" + ptS({float(q.x - q.nx * w), float(q.y - q.ny * w)}) + "L" + ptS({float(q.x + q.nx * w), float(q.y + q.ny * w)});
    }
    const std::string mouth = xf(rrect(-4, -3.5, 8, 7, 1.5), place(9, 84.5, 24));
    const std::string sprig = feather({62, 64}, {84, 72}, 9) + feather({60, 70}, {74, 86}, 8) + feather({57, 62}, {64, 46}, 7);
    c.add("elf-horn", "Рог",
          moved({G::F(horn + mouth), G::XS(bands, 2.4), G::X(xf(ell(0, 0, 2.4, 11), place(float(e.x), float(e.y), ea))),
                 G::XS("M24 86C34 87 44 82 51 75M37 69C40 64 44 58 47 50", 1.6), G::XS(sprig, 2.4), G::F(sprig),
                 G::XS("M65 66 79 71M62 72 71 82M58 60 62 51", 1.6)},
                Affine::translate(0, -3.5f)));
  }
  // Феникс-лист: птица с поднятыми крыльями из листьев, хвост из трёх листьев, хохолок.
  {
    std::vector<std::string> wing;
    for (int k = 0; k < 6; k++) {
      const double a = -168 + k * 15, len = 42 - k * 3.5;
      const Pt b{43, 50};
      wing.push_back(feather(b, polar(b.x, b.y, len, a), 13.5 - k * 0.6));
    }
    std::vector<L> v;
    layeredFeathers(v, wing, 1.7f, true);
    std::string ribs;
    for (int k = 0; k < 6; k++) ribs += "M" + pt(43, 50, 10, -168 + k * 15) + "L" + pt(43, 50, 34 - k * 3.5, -168 + k * 15);
    std::string tail, tailRibs;
    for (int k = 0; k < 5; k++) {
      const double a = 62 + k * 14, len = k == 2 ? 36 : (k == 1 || k == 3 ? 33 : 28);
      const Pt b{50, 64}, t = polar(50, 64, len, a);
      tail += feather(b, t, 11);
      tailRibs += "M" + ptS(lerp(b, t, 0.25)) + "L" + ptS(lerp(b, t, 0.85));
    }
    const std::string body = "M50 38C57 38 60.5 46 59.5 54C58.5 62 54.5 68 50 70C45.5 68 41.5 62 40.5 54C39.5 46 43 38 50 38Z" + circ(50, 32, 6.5) +
                             "M44 30.5 35.5 34 44.5 35.5Z" + feather({52, 28}, {64, 15}, 6) + feather({54, 30}, {68, 22}, 5) + feather({50, 27}, {56, 12}, 5);
    v.push_back(G::XS(sym(ribs), 1.5));
    v.push_back(G::XS(tail, 2.2));
    v.push_back(G::F(tail));
    v.push_back(G::XS(tailRibs, 1.5));
    v.push_back(G::XS(body, 2.2));
    v.push_back(G::F(body));
    v.push_back(G::X(circ(47.5, 31, 1.5)));
    c.add("elf-phoenix", "Феникс-лист", moved(v, Affine::translate(0, -4)));
  }
  // Древо жизни: стройное дерево в кольце, ветви с листьями расходятся вверх, корни — вниз.
  {
    std::string branches, leaves;
    const double ys[] = {62, 52, 43}, as[] = {-158, -132, -108};
    for (int k = 0; k < 3; k++) {
      const Pt b{49, float(ys[k])}, e = polar(50, 50, 35, as[k]);
      const Pt m = lerp(b, e, 0.5) + Pt{0, -6};
      branches += taper(b, m, lerp(m, e, 0.5), e, 4.5, 1.6);
      leaves += feather(e, polar(e.x, e.y, 9, as[k]), 6) + feather(lerp(m, e, 0.4), lerp(m, e, 0.4) + Pt{-3, -7}, 5.5) +
                feather(lerp(b, m, 0.7), lerp(b, m, 0.7) + Pt{-1, -8}, 5);
    }
    std::string roots;
    for (double a : {118.0, 145.0, 168.0}) {
      const Pt e = polar(50, 50, 39, a), b{49, 80};
      roots += taper(b, lerp(b, e, 0.3) + Pt{0, 9}, lerp(b, e, 0.7) + Pt{0, 4}, e, 4.5, 1.4);
    }
    c.add("elf-tree", "Древо жизни",
          {G::S(circ(50, 50, 44), 4.5),
           G::F(taper({50, 86}, {50, 70}, {50, 52}, {50, 30}, 9, 4) + sym(branches + leaves + roots) + feather({50, 30}, {50, 12}, 9))});
  }
}

// ---------------------------------------------------------------- некроманты
void necromancers(Catalog& c) {
  c.group("Некроманты");
  // Череп и скрещённые кости: глазницы под насупленными бровями, скулы, трещина, зубы.
  {
    const std::string skull = "M50 6C34 6 24 17 24 32C24 41 28 47 34 51V58A4 4 0 0 0 38 62H62A4 4 0 0 0 66 58V51C72 47 76 41 76 32"
                              "C76 17 66 6 50 6Z";
    const std::string bone = rrect(-38, -4, 76, 8, 4) + circ(-39, -4.5, 5.5) + circ(-39, 4.5, 5.5) + circ(39, -4.5, 5.5) + circ(39, 4.5, 5.5);
    c.add("necro-crossbones", "Череп и кости",
          {G::F(xf(bone, place(50, 64, 34)) + xf(bone, place(50, 64, -34))), G::XS(skull, 5), G::F(skull),
           G::X(sym("M31 28C36 26.5 42 28 46.5 32.5C46 38.5 42 41 37.5 40.5C33 40 30.5 35 31 28Z") + "M50 41 53.5 48C51.5 49.5 48.5 49.5 46.5 48Z" +
                rrect(42.5, 54, 3, 8, 1) + rrect(48.5, 54, 3, 8, 1) + rrect(54.5, 54, 3, 8, 1)),
           G::XS("M58 7 55 14 60 18.5 56.5 24", 1.8), G::XS(sym("M29 44C32 48 35 50 38.5 50.5"), 1.6)});
  }
  // Коса: изогнутое лезвие с фаской, обоймица, древко с двумя рукоятями.
  c.add("necro-scythe", "Коса",
        moved({G::F(taper({20, 97}, {36, 68}, {56, 38}, {76, 11}, 7.5, 6) + taper({39, 67}, {44, 69}, {50, 71}, {55, 74}, 6, 5) +
                    taper({59, 37}, {62, 39}, {65, 41}, {68, 44}, 5.5, 4.5) + "M85 10C66 -1 34 3 9 37C30 20 54 18 77 27Z"),
               G::XS("M77 14.5C58 9 36 14 19 28.5", 1.8), G::XS("M70 20.5 80 26.5M66.5 25.5 76 31.5", 2), G::XS("M23.5 88 30 91.5", 2)},
              Affine::translate(3, -0.5f)));
  // Рука скелета: фаланги с суставами, пясть, две кости предплечья, могильный холм с камнями.
  {
    struct Finger {
      Pt k, t;
    };
    const Finger fs[] = {{{38, 47}, {30, 11}}, {{48, 45}, {47, 6}}, {{58, 47}, {63, 10}}, {{66, 52}, {77, 24}}, {{33, 63}, {15, 43}}};
    std::string bones, knuckles, joints;
    for (const Finger& f : fs) {
      bones += "M" + ptS(f.k) + "L" + ptS(f.t) + "M48 70L" + ptS(f.k);
      const float dx = f.t.x - f.k.x, dy = f.t.y - f.k.y, len = std::hypot(dx, dy), nx = -dy / len * 5.5f, ny = dx / len * 5.5f;
      for (float t : {0.f, 0.45f, 0.75f}) {
        const Pt j{f.k.x + dx * t, f.k.y + dy * t};
        knuckles += circ(j.x, j.y, 4.6);
        joints += "M" + ptS({j.x - nx, j.y - ny}) + "L" + ptS({j.x + nx, j.y + ny});
      }
    }
    c.add("necro-hand", "Рука скелета",
          {G::S(bones, 6.5), G::F(knuckles + ell(48, 71, 10, 6.5)), G::S("M44 76V90M54 76V90", 6.5), G::XS(joints, 1.8), G::XS("M49 77V90", 2),
           G::F("M8 98C16 88 33 85.5 50 85.5C67 85.5 84 88 92 98Z"), G::X(ell(24, 94, 3, 2) + ell(74, 93, 3.5, 2.2) + ell(64, 96.5, 2, 1.3)),
           G::XS("M30 90 34 88.5M60 89 66 88", 1.6)});
  }
  // Надгробие: арка с каймой, крест с расширенными концами, трещина, ступенчатое основание, трава.
  c.add("necro-tomb", "Надгробие",
        {G::F("M24 86V38C24 20 36 8 50 8C64 8 76 20 76 38V86Z" + rrect(18, 82, 64, 8, 2.5) + rrect(12, 89, 76, 8, 2.5) +
              taper({10, 97}, {9, 92}, {8, 88}, {5, 84}, 3.4, 0.8) + taper({90, 97}, {91, 92}, {92, 88}, {95, 84}, 3.4, 0.8)),
         G::XS("M29.5 80V38.5C29.5 24.5 39 14 50 14C61 14 70.5 24.5 70.5 38.5V80", 2),
         G::X("M47 22H53L52 30.5 61.5 29.5V35.5L52 34.5 53 58H47L48 34.5 38.5 35.5V29.5L48 30.5Z"), G::XS("M70 44 64 52 68 58 62 68", 2.2),
         G::XS("M40 66H60M43 72H57", 2)});
  // Лич в короне: череп под капюшоном, светящиеся глаза, зубчатая корона с камнями.
  {
    const std::string hood = "M50 4C70 4 84 20 86 44C88 64 84 82 78 97H22C16 82 12 64 14 44C16 20 30 4 50 4Z";
    const std::string skull = "M50 31C61 31 67 39 67 50C67 57 64.5 62 60.5 64.5V71H39.5V64.5C35.5 62 33 57 33 50C33 39 39 31 50 31Z";
    const std::string crown = "M34.5 37L31 16 39.5 25.5 44 10 50 22.5 56 10 60.5 25.5 69 16 65.5 37Z";
    c.add("necro-lich", "Лич в короне",
          {G::F(hood), G::X("M50 22C65 22 73 34 73 50C73 67 64 80 50 85C36 80 27 67 27 50C27 34 35 22 50 22Z"), G::F(skull),
           G::X(sym("M37 46C40 44 44 45 46.5 48C46 52.5 43.5 54.5 40.5 54.5C37.5 54 36 50.5 37 46Z") + "M50 55 52.5 60.5H47.5Z" +
                rrect(42.5, 65, 2.6, 6, 1) + rrect(48.7, 65, 2.6, 6, 1) + rrect(54.9, 65, 2.6, 6, 1)),
           G::F(sym(circ(41.5, 49.5, 1.9))), G::XS(crown, 3), G::F(crown), G::X(circ(50, 31, 2.2) + sym(circ(40, 31.5, 1.6))),
           G::XS(sym("M22 90C21 80 21 70 23 62M31 94C29 88 28.5 84 29 80"), 1.8)});
  }
  // Гримуар с черепом: переплёт с корешком и уголками, череп на обложке, застёжка, закладки.
  {
    const std::string cover = "M21 5H79C82 5 84 7 84 10V87C84 90 82 92 79 92H21Z";
    const std::string corners = "M21 5H33L21 17ZM84 17V10C84 7 82 5 79 5H72ZM21 92H33L21 80ZM84 80V87C84 90 82 92 79 92H72Z";
    c.add("necro-grimoire", "Гримуар с черепом",
          {G::F("M14 7C14 5.5 15.5 4 17 4H23V93H17C15.5 93 14 91.5 14 90Z" + cover + taper({40, 91}, {39.5, 94}, {39, 96}, {38, 99}, 4, 4) +
                taper({47, 91}, {47.5, 94}, {48, 96}, {49, 98.5}, 4, 4)),
           G::XS("M23 5V92M14 18H23M14 79H23", 2), G::XS("M27 11H78V86H27Z", 1.6), G::XS(corners, 1.6),
           G::X("M52 26C64 26 71 34 71 45C71 52 68 56 64 58V65H40V58C36 56 33 52 33 45C33 34 40 26 52 26Z"),
           G::F(symAt("M36.5 43C40 40.5 45 41.5 48 45C47.5 49.5 44.5 52 41 51.5C37.5 51 36 47.5 36.5 43Z", 52) + "M52 51.5 55 57.5H49Z"),
           G::XS("M45.5 59.5V65M52 59.5V65M58.5 59.5V65", 1.6), G::XS(rrect(76, 41, 12, 14, 2.5), 2.4), G::F(rrect(76, 41, 12, 14, 2.5)),
           G::X(circ(82, 48, 2))});
  }
  // Фонарь душ: кольцо, крыша, окно с призрачным пламенем-черепом, вокруг — блуждающие души.
  {
    const std::string flame = "M50 37C54 43 60 47 60 56C60 63 55.5 67 50 67C44.5 67 40 63 40 56C40 50 44 47 45.5 42C46.5 45.5 48 46.5 49 47C48.5 43.5 49 40 50 37Z";
    const std::string souls = circ(18, 34, 7) + taper({18, 34}, {22, 46}, {12, 52}, {17, 64}, 13, 1) + circ(82, 54, 6.5) +
                              taper({82, 54}, {78, 66}, {88, 72}, {83, 84}, 12, 1);
    c.add("necro-lantern", "Фонарь душ",
          {G::S(circ(50, 9.5, 5.2), 3.4), G::F("M34 25 50 14 66 25Z" + rrect(32, 24, 36, 5.5, 2) + rrect(35, 28, 30, 45, 2.5) + rrect(32, 72, 36, 5.5, 2) +
                                             "M38 77H62L57.5 85H42.5Z" + circ(50, 89, 3.4) + souls),
           G::X(rrect(39.5, 32, 21, 37, 3)), G::F(flame), G::X(sym(ell(46.5, 56, 2, 2.6)) + "M50 59.5 51.5 62.5H48.5Z"), G::XS("M36 25H64", 1.6),
           G::X(ell(15.5, 33, 1.6, 2.2) + ell(20.5, 33, 1.6, 2.2) + ell(79.8, 53, 1.5, 2.1) + ell(84.4, 53, 1.5, 2.1))});
  }
  // Костяной дракон: череп дракона с глазницей и зубами, позвонки шеи с остистыми отростками.
  {
    const Pt b0{46, 34}, b1{60, 42}, b2{70, 58}, b3{76, 96};
    std::string spine;
    for (int k = 0; k < 7; k++) {
      const double t = 0.06 + k * 0.145;
      const CurvePt q = bezier(b0, b1, b2, b3, t);
      const float a = float(std::atan2(q.ny, q.nx) * 180 / kPi);
      spine += xf(rrect(-4.5, -4, 9, 8, 2.5) + "M-3 -4 1 -12.5 3 -4Z", place(float(q.x), float(q.y), a + 90 + 180));
    }
    c.add("necro-bonedragon", "Костяной дракон",
          moved({G::F(spine), G::XS(kDragonHead, 3), G::F(kDragonHead + dragonHorns() + kDragonFangs + "M24 37.5 27 38.5 25 42.5ZM27 44 30.5 43 28.5 39.5Z"),
                 G::X("M21 22C24 18.5 31 17 36 19C35 24.5 30 27.5 24 27C21.5 26.5 20.5 24.5 21 22Z" + ell(11.5, 27.5, 2.4, 1.5) +
                      "M38 26C41 25 44.5 26 46 29C44.5 33 42 35 39.5 35C37.5 33.5 37 29 38 26Z"),
                 G::XS("M14 51.5C22 52.5 30 51.5 37 49M33 41 46 47", 1.8), G::XS("M16 30C20 29 24 30 27 32", 1.6)},
                Affine::translate(3.5f, 3)));
  }
  // Ворон на черепе: ворон в профиль с перьями крыла и хвоста, когти на черепе.
  {
    const std::string raven = "M13 25L22 19.5C25 15 30 13 35 13.5C40 14 45 18 49 23C56 27 66 34 74 44C80 50 87 56 96 62L91 64.5 95 69 86 67"
                              "C80 66 74 64 68 63C62 65 55 66 49 65C42 62.5 37 57 35 50C33.5 44 33 38 31 33L28 30 22 28.5Z";
    const std::string skull = "M52 67C62 67 68 73 68 81C68 86 66 89 63 90V95H41V90C38 89 36 86 36 81C36 73 42 67 52 67Z";
    c.add("necro-raven", "Ворон на черепе",
          moved({G::F(raven + "M44 63 42 70 45.5 69 46 63ZM54 64 54 70.5 57.5 69 57 63Z"), G::XS(skull, 3), G::F(skull),
                 G::X(circ(30, 20.5, 1.7) + symAt("M41 77C44 75.5 47 76 49 78.5C48.5 82 46.5 83.5 44 83.5C41.5 83 40.5 80 41 77Z", 52) +
                      "M52 84 54 88H50Z" + rrect(45.6, 90.5, 2.2, 4.5, 0.8) + rrect(50.9, 90.5, 2.2, 4.5, 0.8) + rrect(56.2, 90.5, 2.2, 4.5, 0.8)),
                 G::XS("M44 36C54 37 64 44 72 53C77 58 82 61 89 63.5M55 42C60 47 64 52 66 58M63 46 70 56", 1.8),
                 G::XS("M33 35 37.5 36M34 41 38.5 42", 1.5)},
                Affine::translate(-4, -3.5f)));
  }
}

// ---------------------------------------------------------------- тёмные эльфы
// Ноги паука (левая половина): от тазика через колено к кончику, сустав-утолщение в колене.
std::string spiderLegs(const Pt (*legs)[3], int count, double w) {
  std::string s;
  for (int i = 0; i < count; i++) {
    const Pt c = legs[i][0], k = legs[i][1], t = legs[i][2];
    s += taper(c, lerp(c, k, 0.4), lerp(c, k, 0.75), k, w, w * 0.8) + taper(k, lerp(k, t, 0.35), lerp(k, t, 0.7), t, w * 0.8, w * 0.3) +
         circ(k.x, k.y, w * 0.55);
  }
  return s;
}

void darkElves(Catalog& c) {
  c.group("Тёмные эльфы");
  // Паук (сверху): коленчатые ноги, хелицеры, глаза, песочные часы и узор на брюшке.
  {
    const Pt legs[][3] = {{{44, 31}, {31, 15}, {25, 2}}, {{43, 35}, {23, 26}, {6, 25}}, {{43, 40}, {23, 46}, {8, 59}}, {{44, 44}, {31, 61}, {23, 87}}};
    c.add("dark-elf-spider", "Паук",
          moved({G::F(sym(spiderLegs(legs, 4, 5.2) + taper({47, 29}, {45, 26}, {45, 23.5}, {47, 21}, 3.6, 1.2)) + ell(50, 37, 9.5, 10.5) +
                      ell(50, 66, 15.5, 20.5)),
                 G::XS(ell(50, 66, 15.5, 20.5), 2), G::F(ell(50, 66, 15.5, 20.5)), G::X("M44.5 58H55.5L51.2 65 55.5 72H44.5L48.8 65Z"),
                 G::X(sym(circ(46.5, 31.5, 1.6) + circ(48.8, 29, 1.1))), G::XS("M44 79Q50 82 56 79M46 51Q50 49 54 51", 1.6)},
                Affine::translate(0, 3)));
  }
  // Паутина: восемь лучей с крепёжными нитями, провисающие кольца, капли росы.
  {
    std::string radial, rings, dew;
    for (int k = 0; k < 8; k++) {
      const double a = -90 + 45 * k;
      radial += "M50 50L" + pt(50, 50, 46, a);
      for (double r : {11.0, 20.5, 30.0, 39.5}) rings += "M" + pt(50, 50, r, a) + "Q" + pt(50, 50, r * 0.78, a + 22.5) + " " + pt(50, 50, r, a + 45);
    }
    dew = circ(polar(50, 50, 31.5, -67.5).x - 3, polar(50, 50, 31.5, -67.5).y + 2, 2.6) + circ(polar(50, 50, 18, 112).x, polar(50, 50, 18, 112).y, 2.2) +
          circ(polar(50, 50, 35, 160).x, polar(50, 50, 35, 160).y + 2, 2.2);
    c.add("dark-elf-web", "Паутина",
          {G::S(radial, 4), G::S(rings, 3.2), G::F(circ(50, 50, 5) + dew),
           G::S(sym("M" + pt(50, 50, 45, -135) + "L4 2M" + pt(50, 50, 45, 135) + "L4 98"), 2.6)});
  }
  // Скрещённые кинжалы: изогнутые клинки с долом и зубцами, гарда с завитками, рукоять с обмоткой, навершие с камнем.
  c.add("dark-elf-daggers", "Скрещённые кинжалы",
        crossed("M50 2C59 14 62 30 58 55H43C45.5 39 45.5 21 50 2Z" + std::string("M27 51 42 56H58L73 51 67 61H33Z") + rrect(45.5, 60, 9, 22, 2.5) +
                    "M50 80 56 88 50 97 44 88Z" + circ(27, 49.5, 3) + circ(73, 49.5, 3),
                circ(50, 88, 2.2), "M50.5 12C53 24 54 36 52 50M45.5 66H54.5M45.5 71H54.5M45.5 76H54.5M59.5 36 56.5 38M59 44 56 46", 1.8, 36, 50, 50, 1.06f));
  // Полумесяц и паук на нити: паук с коленчатыми ногами, звёзды.
  c.add("dark-elf-crescent", "Полумесяц и паук",
        moved({G::F(circ(44, 50, 42)), G::X(circ(60, 44, 36)), G::S("M53 12V45", 1.8),
               G::S("M50 49 44 43.5 43 37M49.5 52 42 51 38.5 55M50 55 44 59 42 65M60 49 66 43.5 67 37M60.5 52 68 51 71.5 55M60 55 66 59 68 65", 2.6),
               G::F(ell(55, 50, 5.5, 4.8) + ell(55, 59, 7.5, 7.5) + star(74, 76, 5, 1.4, 4, -90) + star(82, 26, 4, 1.1, 4, -90)),
               G::X("M53 56.5H57L55.6 59 57 61.5H53L54.4 59Z")},
              Affine::translate(7, 0)));
  // Паучиха-королева: паук с короной, гранёное брюшко с узором-самоцветом, длинные ноги.
  {
    const Pt legs[][3] = {{{44, 36}, {28, 18}, {20, 4}}, {{43, 40}, {20, 32}, {3, 34}}, {{43, 45}, {20, 54}, {6, 70}}, {{45, 49}, {30, 68}, {24, 94}}};
    const std::string abdomen = "M50 52C62 52 68 61 67 72C66 82 58 90 50 98C42 90 34 82 33 72C32 61 38 52 50 52Z";
    const std::string crown = "M40.5 30 38.5 16 44.5 22.5 50 11 55.5 22.5 61.5 16 59.5 30Z";
    c.add("dark-elf-queen", "Паучиха-королева",
          {G::F(sym(spiderLegs(legs, 4, 5)) + ell(50, 41, 10, 11)), G::XS(abdomen, 2.2), G::F(abdomen), G::XS(crown, 2.4), G::F(crown),
           G::X(circ(50, 23, 2) + sym(circ(46.5, 39, 1.6) + circ(48.5, 36, 1.1))),
           G::X("M50 60 57 70 50 84 43 70Z"), G::F("M50 64.5 53.5 70 50 78 46.5 70Z"), G::XS(sym("M40 64 44 70M39 76 44 80"), 1.6)});
  }
  // Хлыст жрицы: рукоять с обмоткой и навершием, три змеиных хвоста-плети с головами.
  {
    std::string lashes, heads, eyes;
    const Pt ends[][3] = {{{40, 40}, {50, 22}, {66, 13}}, {{46, 46}, {62, 40}, {80, 34}}, {{34, 36}, {30, 22}, {36, 9}}};
    for (const auto& e : ends) {
      const Pt b{32, 56};
      lashes += taper(b, e[0], e[1], e[2], 8, 4.6);
      const Pt d = e[2] - e[1];
      const float len = std::hypot(d.x, d.y), ang = float(std::atan2(d.y, d.x) * 180 / kPi);
      heads += xf(ell(0, 0, 7, 4.8) + "M5 -3 11.5 0 5 3Z", place(e[2].x + d.x / len * 4, e[2].y + d.y / len * 4, ang));
      eyes += xf(circ(1.5, -2, 1.3), place(e[2].x + d.x / len * 4, e[2].y + d.y / len * 4, ang));
    }
    const std::string handle = xf(rrect(-6, -2, 12, 36, 3.5), place(32, 56, 35)) + circ(32, 56, 8) + xf("M-8 33H8L0 45Z", place(32, 56, 35));
    c.add("dark-elf-whip", "Хлыст жрицы",
          moved({G::F(lashes + heads + handle), G::X(eyes), G::XS(xf("M-6 12H6M-6 17.5H6M-6 23H6M-6 28.5H6", place(32, 56, 35)), 1.7),
                 G::X(circ(32, 56, 3.2))},
                scaleAt(0.93f, 50, 50) * Affine::translate(0, 6)));
  }
  // Кинжал с ядом: волнистый клинок остриём вниз, гарда-полумесяц, капли яда.
  {
    std::vector<Pt> lft, rgt;
    for (int i = 0; i <= 24; i++) {
      const double t = i / 24.0, y = 34 + 52 * t, w = 13.5 * (1 - std::pow(t, 2)) + 0.4, cx = 50 + 3.5 * std::sin(t * 3 * kPi);
      lft.push_back({float(cx - w), float(y)});
      rgt.push_back({float(cx + w), float(y)});
    }
    std::string blade = "M" + ptS(lft[0]);
    for (size_t i = 1; i < lft.size(); i++) blade += "L" + ptS(lft[i]);
    for (size_t i = rgt.size(); i-- > 0;) blade += "L" + ptS(rgt[i]);
    blade += "Z";
    std::string fuller;
    for (int i = 0; i <= 12; i++) {
      const double t = 0.06 + 0.5 * i / 12.0;
      fuller += (i ? "L" : "M") + ptS({float(50 + 3.5 * std::sin(t * 3 * kPi)), float(34 + 52 * t)});
    }
    const std::string hilt = "M18 20C26 31 37 33 50 33C63 33 74 31 82 20C77 37 64 43 50 43C36 43 23 37 18 20Z" + rrect(43, 8, 14, 28, 3.5) +
                             "M50 0 58 6.5 50 13 42 6.5Z" + circ(18, 20, 4) + circ(82, 20, 4);
    c.add("dark-elf-poison", "Кинжал с ядом",
          {G::F(blade + drop(51, 86, 93.5, 4.6) + drop(73, 52, 60, 4.6)), G::XS(fuller, 2), G::XS("M63 46C67 49.5 69 53 69.5 56", 1.8), G::XS(hilt, 2.6),
           G::F(hilt), G::XS("M43 15.5H57M43 21.5H57M43 27.5H57", 1.8), G::X(circ(50, 6.5, 2.2))});
  }
  // Жрица: лицо с острыми длинными ушами, длинные волосы, диадема с пауком.
  {
    const std::string hair = "M50 8C67 8 77 20 77 38C77 54 81 70 90 88L73 95C68 86 64 76 63 66L50 70V8Z";
    const std::string face = "M50 24C59 24 64 32 64 43C64 54 58 63 50 66C42 63 36 54 36 43C36 32 41 24 50 24Z";
    const std::string tiara = "M50 13 53.5 21 61 18.5 57 27H50Z";
    c.add("dark-elf-priestess", "Жрица",
          {G::F(sym(hair) + "M18 99C21 85 33 76 50 76C67 76 79 85 82 99Z" + rrect(44, 60, 12, 18, 4)), G::XS("M18 99C21 85 33 76 50 76C67 76 79 85 82 99", 2),
           G::XS(face + sym(feather({37.5, 44}, {9, 26}, 8.5)), 2.6), G::F(face + sym(feather({37.5, 44}, {9, 26}, 8.5))),
           G::XS(sym(tiara), 2), G::F(sym(tiara)), G::X(circ(50, 21, 1.8)),
           G::X(sym("M40 41.5 47.5 43.5 41.5 46Z")), G::XS(sym("M38.5 37.5 46.5 39.5") + "M50 46V53M46 57.5Q50 59.5 54 57.5", 1.6),
           G::XS(sym("M31 37 18 31"), 1.6)});
  }
  // Город в сталактитах: свод пещеры со сталактитами, шпили подземного города с окнами, арочные мосты.
  {
    struct Spire {
      double x, top, w;
    };
    const Spire sp[] = {{50, 22, 12}, {35, 42, 9}, {65, 42, 9}, {22, 56, 8}, {78, 56, 8}, {10, 70, 7}, {90, 70, 7}};
    std::string spires, windows;
    for (const Spire& s : sp) {
      const double roof = s.w * 1.5;
      spires += "M" + n(s.x - s.w / 2) + " 92V" + n(s.top + roof) + "L" + n(s.x) + " " + n(s.top) + "L" + n(s.x + s.w / 2) + " " + n(s.top + roof) + "V92Z";
      for (double y = s.top + roof + 5; y < 84; y += 13) windows += rrect(s.x - 1.3, y, 2.6, 6, 1.3);
    }
    std::string stal = "M3 3H97V9H3Z";
    const double sx[] = {42.5, 57.5, 28.5, 71.5, 16, 84, 5, 95}, sl[] = {26, 26, 22, 22, 30, 30, 18, 18};
    for (int k = 0; k < 8; k++) stal += "M" + n(sx[k] - 4) + " 8L" + n(sx[k]) + " " + n(8 + sl[k]) + "L" + n(sx[k] + 4) + " 8Z";
    c.add("dark-elf-city", "Город в сталактитах",
          {G::F(stal + spires + rrect(3, 90, 94, 7, 2)), G::X(windows), G::XS("M39.5 66Q42.5 61 45.5 66M54.5 66Q57.5 61 60.5 66", 2),
           G::S("M39.5 67Q42.5 62 45.5 67M54.5 67Q57.5 62 60.5 67M26 74Q28.5 70 31 74M69 74Q71.5 70 74 74", 2), G::XS("M3 93H97", 1.6)});
  }
}

// ---------------------------------------------------------------- наги
// Полоса волн: гребни шагом step от x0 до x1 на высоте y, низ — до bottom.
std::string waveBand(double x0, double x1, double y, double step, double amp, double bottom) {
  std::string s = "M" + n(x0) + " " + n(bottom) + "V" + n(y);
  for (double x = x0; x < x1 - 0.01; x += step)
    s += "Q" + n(x + step * 0.25) + " " + n(y - amp) + " " + n(x + step * 0.5) + " " + n(y) + "Q" + n(x + step * 0.75) + " " + n(y + amp * 0.5) + " " +
         n(std::min(x1, x + step)) + " " + n(y);
  return s + "V" + n(bottom) + "Z";
}

void nagas(Catalog& c) {
  c.group("Наги");
  // Кобра: капюшон с очковым узором, глаза, брюшные щитки, два витка хвоста.
  c.add("naga-cobra", "Кобра",
        moved({G::S(ell(50, 87, 30, 7), 7), G::F(taper({76, 90}, {84, 92}, {90, 90}, {95, 85}, 6, 1.2)), G::XS(rrect(44.5, 70, 11, 23, 4), 3),
         G::F("M50 8C58 8 63 13 64 20C72 24 78 32 78 42C78 54 68 62 58 68L56 76H44L42 68C32 62 22 54 22 42C22 32 28 24 36 20C37 13 42 8 50 8Z" +
              rrect(44.5, 70, 11, 23, 4)),
         G::X(sym("M40.5 13 47 15.5 46 18.5 41 16.5Z")), G::XS(sym("M36 26C31 32 28 38 28 45M41 27C37 34 36 42 37 50"), 1.6),
         G::XS("M44 32H56M43 38H57M43 44H57M43.5 50H56.5M44.5 56H55.5M45.5 62H54.5", 1.7), G::XS("M50 21.5V24.5", 1.5)},
        Affine::translate(-3, -1.5f)));
  // Трезубец: зазубренные зубцы, перекладина с завитками и самоцветом, древко с обмоткой.
  c.add("naga-trident", "Трезубец",
        {G::F("M50 3 58 17 54 16V45H46V16L42 17Z" + sym("M22 12 29.5 25 26 24V42H18V24L14.5 25Z") +
              "M18 38C18 50 30 56 50 56C70 56 82 50 82 38H74C74 46 64 48.5 50 48.5C36 48.5 26 46 26 38Z" + rrect(46, 50, 8, 40, 2) +
              "M45 88H55L50 97Z" + sym(spiral(12.5, 43, 0, -260, 5.5, 2.5, 4, 2))),
         G::X("M50 46 54 52 50 58 46 52Z"), G::XS("M46 66H54M46 71H54M46 76H54M46 81H54", 1.6), G::XS(sym("M44.5 19 41.5 21M24.5 27 21.5 29"), 1.4)});
  // Раковина-гребешок: рёбра веером, линия роста, ушки у замка.
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
    c.add("naga-shell", "Раковина",
          {G::F(edge + "Z" + sym("M36 80 46 84V92H33Z")), G::XS(ribs, 2.6), G::XS(arc(50, 84, 22, 197, 343), 1.8), G::XS(sym("M38 84.5 37 90"), 1.5)});
  }
  // Два змея вокруг жезла с жемчужиной: головы друг к другу, раздвоенные языки, блик жемчуга.
  {
    const std::string body = "M50 92C34 88 30 80 50 72C70 64 72 54 50 46C32 40 26 32 32 27";
    const std::string head = xf(ell(0, 0, 8.5, 5.5), place(36.5, 24.5, -40));
    c.add("naga-serpents", "Два змея",
          {G::F(rrect(47, 14, 6, 82, 3) + circ(50, 8, 6.5)), G::X(circ(48, 6, 1.6)), G::XS(sym(body), 10), G::S(sym(body), 6.5), G::XS(sym(head), 3),
           G::F(sym(head)), G::X(sym(circ(36, 22.5, 1.4))), G::S(sym("M41.5 18.5 44.5 15 44 12.5M44.5 15 47 14"), 1.4)});
  }
  // Кракен: мантия с плавниками и гребнем, глаза со щелевыми зрачками, щупальца с присосками и завитками.
  {
    const std::string mantle = "M50 4C64 4 74 15 74 30C74 42 69 50 62 55H38C31 50 26 42 26 30C26 15 36 4 50 4Z";
    struct Tent {
      Pt p0, c1, c2, p1;
      double cx, cy, a0, a1, r;
    };
    const Tent ts[] = {{{36, 50}, {22, 54}, {10, 52}, {9, 40}, 15, 39, 180, -60, 6},
                       {{40, 54}, {30, 66}, {14, 70}, {12, 82}, 18, 82, 180, 420, 6},
                       {{46, 56}, {45, 70}, {38, 84}, {30, 92}, 31.5, 86, 135, -110, 5.5}};
    std::string tent, suckers;
    for (const Tent& t : ts) {
      tent += taper(t.p0, t.c1, t.c2, t.p1, 9, 4.5) + spiral(t.cx, t.cy, t.a0, t.a1, t.r, t.r * 0.45, 4.5, 2);
      for (double s : {0.3, 0.5, 0.7}) {
        const CurvePt q = bezier(t.p0, t.c1, t.c2, t.p1, s);
        suckers += circ(q.x, q.y, 1.5);
      }
    }
    c.add("naga-kraken", "Кракен",
          {G::F(sym(tent) + mantle), G::X(sym(suckers)), G::XS(mantle, 2.4), G::F(mantle),
           G::X(sym("M35.5 31C39 31 44 33 46.5 36C44 39 39 39 36.5 36.5C35.5 35 35.2 33 35.5 31Z")), G::F(sym(ell(41.5, 35.4, 1, 2.4))),
           G::XS("M50 8V24M42 10C40 15 39.5 20 40 25M58 10C60 15 60.5 20 60 25", 1.6)});
  }
  // Жемчужина: раскрытая раковина-створка, крупная жемчужина с бликом.
  {
    // створки: верхняя раскрыта веером, нижняя — чаша; края фестонами
    std::string upper = "M50 52L" + pt(50, 52, 42, 180), ribsU, lower = "M8 64", ribsL;
    for (int k = 0; k < 8; k++) {
      const double a = 180 + 22.5 * k;
      upper += "Q" + pt(50, 52, 48, a + 11.25) + " " + pt(50, 52, 42, a + 22.5);
      if (k) ribsU += "M" + pt(50, 52, 17, a) + "L" + pt(50, 52, 39, a);
    }
    upper += "Z";
    for (int k = 0; k < 6; k++) lower += "Q" + n(8 + 14 * k + 7) + " 60 " + n(8 + 14 * (k + 1)) + " 64";
    lower += "C90 82 72 96 50 96C28 96 10 82 8 64Z";
    for (int k = 1; k < 6; k++) ribsL += "M" + pt(50, 100, 12, 180 + 30 * k) + "L" + pt(50, 100, 30, 180 + 30 * k);
    c.add("naga-pearl", "Жемчужина",
          {G::F(upper), G::XS(ribsU, 1.8), G::XS(lower, 3), G::F(lower), G::XS(ribsL, 1.8), G::XS(circ(50, 58, 12.5), 3.2), G::F(circ(50, 58, 12.5)),
           G::X(taper({42.5, 57}, {42.5, 53}, {44.5, 50}, {48, 48.5}, 2.8, 1.8))});
  }
  // Наг-воин: голова под капюшоном кобры, торс, руки с изогнутыми клинками, змеиный хвост кольцом.
  {
    const std::string hood = "M50 3C63 3 72 11 73 22C74 31 68 38 60 41H40C32 38 26 31 27 22C28 11 37 3 50 3Z";
    const std::string torso = "M37 33C42 30.5 58 30.5 63 33L62.5 41 58 58H42L37.5 41Z";
    const std::string arm = taper({38, 35}, {30, 40}, {26, 46}, {22, 49}, 7.5, 5.5) + circ(21.5, 48.5, 4.2);
    // ятаган: клинок расширяется к елманю и сходится к острию, выгнут наружу
    std::vector<Pt> bc;
    std::vector<double> bw;
    for (int i = 0; i <= 24; i++) {
      const double t = i / 24.0;
      const CurvePt q = bezier({20, 44}, {16, 34}, {9, 26}, {3, 13}, t);
      bc.push_back({float(q.x), float(q.y)});
      bw.push_back(t < 0.72 ? 4.5 + 5 * t / 0.72 : 9.5 * (1 - t) / 0.28 + 0.6);
    }
    const std::string blade = strip(bc, bw) + xf(rrect(-7.5, -2, 15, 4, 1.6), place(21, 45.5, -28));
    const std::string tail = taper({50, 56}, {50, 62}, {52, 64}, {56, 66}, 15, 14) + spiral(50, 79, -60, 250, 14, 7.5, 14, 4) +
                             taper({50, 72}, {40, 80}, {30, 92}, {12, 94}, 6, 1.2);
    const std::string head = "M50 13C55 13 57.5 17 57.5 21C57.5 26 54 30 50 31C46 30 42.5 26 42.5 21C42.5 17 45 13 50 13Z";
    c.add("naga-warrior", "Наг-воин",
          {G::F(hood), G::XS(sym("M33 13C31 20 32 27 36 33M39 8C37 12 36.5 16 37 20"), 1.6), G::XS(sym(arm + blade), 2.4), G::F(sym(arm + blade)),
           G::XS(tail, 2.6), G::F(tail), G::XS(torso, 2.4), G::F(torso), G::XS(head, 2.2), G::F(head), G::X(sym("M44.5 20 48.5 21.5 45 23Z")),
           G::XS("M44 55.5H56M43 40Q50 43 57 40", 1.6)});
  }
  // Морской змей: голова с гребнем, горбы над волнами, плавники, хвост.
  {
    const Affine hx = place(3, 13, 0, 0.5f);
    const Pt a0{45, 78}, a1{47, 42}, a2{65, 42}, a3{67, 78};
    const std::string body = xf(kDragonHead + dragonHorns() + kDragonFangs, hx) + taper({25, 25}, {34, 32}, {38, 52}, {37, 78}, 12, 11) +
                             taper(a0, a1, a2, a3, 11, 9) + taper({73, 78}, {75, 58}, {87, 58}, {89, 78}, 9, 7) +
                             spikes(a0, a1, a2, a3, 0.3, 0.7, 3, 7, 6, 6, 0, 1) + "M89 76C92 70 96 66 98 60C95 68 95 74 92 79Z";
    c.add("naga-serpent", "Морской змей",
          {G::F(body), G::X(xf(kDragonEye + kDragonNostril, hx)), G::XS(waveBand(2, 98, 80, 12, 6, 98), 3), G::F(waveBand(2, 98, 80, 12, 6, 98)),
           G::XS("M8 90Q14 86 20 90M32 92Q38 88 44 92M58 90Q64 86 70 90M80 93Q86 89 92 93", 1.8)});
  }
  // Волна и трезубец: трезубец поднимается из двух закрученных волн.
  {
    const std::string trident = "M50 2 57.5 15 54 14.5V72H46V14.5L42.5 15Z" + sym("M30 10 37 22 34 21.5V36H27V21.5L23.5 22Z") +
                                "M27 33C27 42 36 47 50 47C64 47 73 42 73 33H67C67 38.5 60 41 50 41C40 41 33 38.5 33 33Z";
    c.add("naga-wave", "Волна и трезубец",
          {G::F(trident), G::XS(sym(spiral(28, 70, 0, -300, 21, 6, 13, 3.5)) + waveBand(3, 97, 86, 23.5, 4, 97), 3),
           G::F(sym(spiral(28, 70, 0, -300, 21, 6, 13, 3.5)) + waveBand(3, 97, 86, 23.5, 4, 97)), G::X("M50 40.5 53 44.5 50 48.5 47 44.5Z"),
           G::XS("M10 93Q20 90 30 93M70 93Q80 90 90 93", 1.6)});
  }
}

}  // namespace

void fantasyEmblemsA(Catalog& c) {
  dragons(c);
  vampires(c);
  nature(c);
  elements(c);
  dwarves(c);
  elves(c);
  necromancers(c);
  darkElves(c);
  nagas(c);
}

}  // namespace rg::gfx::emb
