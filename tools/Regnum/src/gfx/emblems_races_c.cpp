// Regnum — эмблемы рас справочника: «Огры» … «Энты» (раздел «Расы», группа на расу, по алфавиту; имена race-<раса>-…).
// Сетка 100 × 100, одноцветный силуэт, детали — вырезы; симметричные силуэты описаны левой половиной.
#include "gfx/emblems_internal.h"

namespace rg::gfx::emb {

namespace {

// Треугольный шип от точки p наружу под углом deg: длина len, полуширина основания hw.
std::string spike(Pt p, double deg, double len, double hw) {
  return "M" + ptS(polar(p.x, p.y, hw, deg - 90)) + "L" + ptS(polar(p.x, p.y, len, deg)) + "L" + ptS(polar(p.x, p.y, hw, deg + 90)) + "Z";
}

// Кость вдоль оси x длиной len (от -len/2 до len/2), толщина w, шишки на концах радиуса r.
std::string bone(double len, double w, double r) {
  const double h = len / 2;
  return rrect(-h, -w / 2, len, w, w / 2) + circ(-h, -r * 0.8, r) + circ(-h, r * 0.8, r) + circ(h, -r * 0.8, r) + circ(h, r * 0.8, r);
}

// Сужающаяся полоса по спирали вокруг (cx, cy): радиус от r0 до r1, угол от a0 до a1, ширина от w0 до w1.
std::string spiral(double cx, double cy, double r0, double r1, double a0, double a1, double w0, double w1, int steps = 24) {
  std::vector<Pt> in, ou;
  for (int i = 0; i <= steps; i++) {
    const double t = double(i) / steps, r = r0 + (r1 - r0) * t, a = a0 + (a1 - a0) * t, w = (w0 + (w1 - w0) * t) * 0.5;
    ou.push_back(polar(cx, cy, r + w, a));
    in.push_back(polar(cx, cy, std::max(0.0, r - w), a));
  }
  std::string s = "M" + ptS(ou[0]);
  for (size_t i = 1; i < ou.size(); i++) s += "L" + ptS(ou[i]);
  for (size_t i = in.size(); i-- > 0;) s += "L" + ptS(in[i]);
  return s + "Z";
}

// Томоэ-запятая в круге радиуса R: голова у центра под углом rot, хвост уходит по спирали к краю.
std::string tomoe(double cx, double cy, double R, double rot) {
  const Pt h = polar(cx, cy, R * 0.45, rot);
  return circ(h.x, h.y, R * 0.31) + spiral(cx, cy, R * 0.45, R * 0.9, rot, rot + 150, R * 0.62, 0.2, 20);
}

// Три томоэ (мицудомоэ).
std::string mitsudomoe(double cx, double cy, double R, double rot) {
  return tomoe(cx, cy, R, rot) + tomoe(cx, cy, R, rot + 120) + tomoe(cx, cy, R, rot + 240);
}

// ---------------------------------------------------------------- огры
void ogres(Catalog& c) {
  c.group("Огры", EmblemSection::Races);
  // Голова огра: лысый череп со шрамом, тяжёлая челюсть, клыки нижней челюсти, серьга.
  {
    const std::string tusk = "M31 77C29.5 70 30.5 62 35 55C36 62 38 69 40.5 75Z";
    c.add("race-ogres-head", "Голова огра",
          {G::F(sym("M50 7C38 7 30 14 29 26C28.5 32 29 37 28 41C21 47 15 57 15 68C15 82 30 93 50 93H50.5V7Z" + ell(21, 42, 6.5, 8.5))),
           G::XS("M57 9C61 14 63.5 21 63 29", 2.2), G::XS("M57.5 13 63 11.5M59.5 18.5 65 17.5M61 24.5 66.5 24", 1.6), G::X(sym("M35 46 45.5 49.5 44 52.5 36.5 50Z" + ell(45.4, 60.5, 2.3, 1.6))),
           G::XS(sym("M30 40C37 41 43 43 49 47.5M23 37C20 40 20 45 23 47M44.5 53C41.5 56.5 41.5 60.5 44 62.5M24 57C27 61 28 65 27 69"), 2.2),
           G::XS("M38 25C43 23.5 50 23.5 54 24.5M40 31C44 30 50 30 53 30.5", 1.8),
           G::X("M25 66C36 71 64 71 75 66C71 76 62 81 50 81C38 81 29 76 25 66Z"),
           G::F(sym("M41.5 81 44 75.5 46.5 81Z")), G::XS(sym(tusk), 3), G::F(sym(tusk)), G::XS("M37 87.5C43 90.5 57 90.5 63 87.5", 2),
           G::S(circ(18.5, 53, 3.6), 2.2)});
  }
  // Дубина с шипами: суковатое навершие, кованые шипы, сучки и трещины, обмотка рукояти.
  {
    const std::string head = "M43 47C39 41 37 36 36.5 31C34 29 34 25 36.5 23C36 15 41 6 50 5C56 5 60 8 62 12C65 13 66 17 64 20C65 26 64 34 61 39"
                             "C60 42 58 45 57 47Z";
    std::string spikes;
    const struct {
      double x, y, a;
    } sp[] = {{36.5, 27, 185}, {40, 39, 155}, {40, 12, 220}, {51, 5.5, 275}, {64, 25, 355}, {60.5, 40, 25}, {61, 10, 310}};
    for (const auto& s : sp) spikes += spike({float(s.x), float(s.y)}, s.a, 9.5, 3.4);
    c.add("race-ogres-club", "Дубина с шипами",
          moved({G::F(head + spikes + taper({50, 92}, {50, 76}, {50, 58}, {50, 44}, 9, 15) + ell(50, 93, 7, 4.5)),
                 G::XS("M47 43C45.5 38 45 33 46 29M54 31C55.5 26 55.5 21 54 17M48 21 46.5 14", 1.8),
                 G::XS("M44 64 56 61M44 70 56 67M44 76 56 73M45 82 55 79", 2)},
                Affine::translate(1, 1) * rotAt(42, 50, 50) * scaleAt(1.06f, 50, 50)));
  }
  // Котёл: похлёбка через край, кость, пузыри, огонь под днищем.
  {
    const std::string body = "M15 46C13 64 25 82 50 84C75 82 87 64 85 46Z";
    const std::string rim = rrect(9, 37, 82, 10, 5);
    const std::string bn = xf(bone(30, 6, 4.2), place(68, 20, -52));
    const std::string flame = "M30 99C25 94 28 88 33 85C33 89 36 91 38 93C39 89 42 86 46 85C44 90 47 94 50 99Z";
    c.add("race-ogres-cauldron", "Котёл",
          {G::F(flame + mirrorX(flame, 50)), G::XS(body, 3), G::F(body),
           G::F(circ(29, 35, 6) + circ(41, 31.5, 7.5) + circ(56, 32, 7) + circ(70, 35, 5.5)), G::XS(bn, 2.4), G::F(bn), G::XS(rim, 2.6),
           G::F(rim), G::S("M15 52C5 52 5 64 15 64M85 52C95 52 95 64 85 64", 3), G::XS("M18 60C30 66 70 66 82 60", 2.2),
           G::X(circ(26, 69, 1.6) + circ(38, 73, 1.6) + circ(50, 74.5, 1.6) + circ(62, 73, 1.6) + circ(74, 69, 1.6)),
           G::F(circ(34, 19, 3.2) + circ(45, 11, 2.4) + circ(26, 26, 2)), G::XS("M14 42H86", 1.6)});
  }
  // Окорок: мясистый край с надкусом, блик, кайма у кости.
  {
    const std::string meat = "M30 25C46 25 58 35 70 42L75 43.5V56.5L70 58C58 65 46 75 30 75C16 75 5 64 5 50C5 36 16 25 30 25Z";
    const std::string bn = rrect(70, 45.5, 17, 9, 3) + circ(89, 45, 5.8) + circ(89, 55, 5.8);
    c.add("race-ogres-haunch", "Окорок",
          moved({G::F(bn), G::XS(meat, 3), G::F(meat), G::X(circ(13, 31, 6.5) + circ(22, 24.5, 6) + circ(7, 40, 5)),
                 G::XS("M14 54C14 47 17 42 22 39M18 63C22 67 27 69 33 69", 2.2), G::XS("M66 44V56", 2.2), G::XS("M42 42C48 44 54 47 58 50", 2)},
                Affine::translate(50, 50) * Affine::rotate(-0.72f) * Affine::scale(1.12f) * Affine::translate(-47, -50)));
  }
}

// ---------------------------------------------------------------- они
void oni(Catalog& c) {
  c.group("Они", EmblemSection::Races);
  // Маска они: рога, грива, выпученные глаза, оскал с клыками.
  {
    const std::string horn = taper({37, 28}, {34, 17}, {30, 9}, {22, 2}, 12, 2);
    const std::string face = "M50 20C40 20 30 23 24 30C20 36 18 44 18 52C18 62 21 72 27 80C33 88 41 94 50 95H50.5V20Z";
    const std::string fang = "M33 70.5 36.5 84 40.5 72.5Z";
    c.add("race-oni-mask", "Маска они",
          {G::F(sym("M28 26 14 22 19 31 5 35 16 41 5 49 17 53 9 63 21 64Z")), G::XS(sym(face), 3), G::F(sym(face)), G::XS(sym(horn), 3),
           G::F(sym(horn)), G::X(sym("M26 43C33 41 41 43 46.5 48C41 51 33 51 27.5 47Z")), G::F(sym(circ(37.5, 46.5, 2.6))),
           G::XS(sym("M23 37C31 31 41 32 48 38M22 56C24 62 26 66 29 68M45 50C44 54 43 56 41.5 59"), 2.4),
           G::XS("M41 28C45 26.5 55 26.5 59 28", 2), G::X(sym("M41.5 63C40 59 43 56.5 46.5 58C47.5 61 46 63 41.5 63Z")),
           G::X("M28 68C36 74 64 74 72 68C70 80 60 88 50 88C40 88 30 80 28 68Z"), G::F(sym(fang + "M42.5 87.5 45 79 47.5 87.5Z"))});
  }
  // Скрещённые канабо: окованные палицы с шипами, гарды и обмотка рукоятей.
  {
    std::string studs, holes;
    for (double y : {14.0, 24.0, 34.0, 44.0, 54.0}) studs += circ(41.5, y, 2.4) + circ(58.5, y, 2.4);
    for (double y : {15.0, 25.0, 35.0, 45.0, 55.0}) holes += circ(46.5, y, 1.5) + circ(53.5, y + 5, 1.5);
    c.add("race-oni-kanabo", "Скрещённые канабо",
          crossed("M46 92V70L42 64.5 41.5 10 45 5H55L58.5 10 58 64.5 54 70V92Z" + studs + rrect(41, 63, 18, 5, 2) + circ(50, 94, 4), holes,
                  "M46 74 54 77M46 80 54 83M46 86 54 89", 1.8, 36, 50, 50, 1.0f));
  }
  // Шлем они: рогатый кабуто с гребнем, нашейник из пластин, личина с клыками.
  {
    const std::string horn = taper({31, 31}, {21, 24}, {13, 14}, {17, 2}, 11, 2);
    const std::string bowl = "M24 50C24 31 36 19 50 19C64 19 76 31 76 50Z";
    c.add("race-oni-helmet", "Шлем они",
          {G::F(sym("M24 52H50.5V72H36C26 72 17 75 9 80Z")), G::XS(sym("M23 60C16 63 13 66 11 70M24 66C19 68 16 70 14 72"), 2),
           G::F(sym(horn)), G::XS(bowl, 3), G::F(bowl), G::XS("M50 21V47M41 22.5 39 47M59 22.5 61 47M32 28 29.5 47M68 28 70.5 47", 2),
           G::F(rrect(18, 46, 64, 8, 3)), G::F("M50 4C55 9 56 14 50 21C44 14 45 9 50 4Z"),
           G::X("M34 55H66V73C66 83 58 89 50 89C42 89 34 83 34 73Z"), G::F(sym("M36.5 59 45 62.5 43.5 64.5 37.5 62Z")),
           G::F("M36 67C41 65 46 66 50 68C54 66 59 65 64 67V74C64 82 58 87 50 87C42 87 36 82 36 74Z"),
           G::X("M41 75C45 77 55 77 59 75C57 80 54 82 50 82C46 82 43 80 41 75Z"), G::F(sym("M42.5 75.8 44.5 80.5 46 76.5Z"))});
  }
  // Гром-барабаны: кольцо барабанов бога грома с томоэ, в центре молния.
  {
    std::string drums, rims, marks;
    for (int i = 0; i < 6; i++) {
      const Pt p = polar(50, 50, 36, -90 + 60 * i);
      drums += circ(p.x, p.y, 13);
      rims += circ(p.x, p.y, 10.2);
      marks += mitsudomoe(p.x, p.y, 9, -90 + 60 * i);
    }
    c.add("race-oni-drums", "Гром-барабаны",
          {G::S(circ(50, 50, 36), 4.5), G::XS(drums, 3), G::F(drums), G::XS(rims, 1.6), G::X(marks), G::F("M56 22 38 52H49L42 78 63 45H52Z")});
  }
}

// ---------------------------------------------------------------- орки
void orcs(Catalog& c) {
  c.group("Орки", EmblemSection::Races);
  // Орочий вождь: голова с клыками в короне из клыков, наплечники с шипами.
  {
    const std::string head = sym("M50 24C42 24 36 27 34.5 33L33.5 40 21 36 31.5 48C31.5 56 34 62 39 66C42 68.5 46 70 50 70H50.5V24Z");
    const std::string crown = rrect(32, 20, 36, 8, 2.5) + sym("M33 22 34.5 9 40 22Z" + std::string("M40 21 43.5 5 47.5 21Z")) + "M46 21 50 1 54 21Z";
    const std::string pauldron = xf(ell(0, 0, 17, 12.5), place(21, 70, -18));
    const std::string spikes = spike({10, 66}, 215, 10, 3.2) + spike({15, 60}, 240, 11, 3.4) + spike({24, 57}, 265, 10, 3.2);
    const std::string tusk = "M39.5 63.5 40.5 51 44.5 61.5Z";
    c.add("race-orcs-chief", "Орочий вождь",
          {G::F("M5 97C5 84 10 74 20 68C27 64 35 63 41 63H59C65 63 73 64 80 68C90 74 95 84 95 97Z"), G::XS("M37 75 50 97M63 75 50 97", 2.2),
           G::XS(sym(pauldron), 3), G::F(sym(pauldron + spikes)), G::XS(sym(xf(arc(0, 0, 11, 190, 350), place(21, 70, -18))), 2),
           G::X(sym(circ(14, 75, 1.4) + circ(21, 79, 1.4) + circ(28, 80, 1.4))), G::XS(head, 3), G::F(head), G::XS(crown, 2.6), G::F(crown),
           G::X(sym("M37 42 46 45 44.5 48 38.5 46.5Z" + ell(46.2, 52.5, 1.5, 1.2))), G::XS(sym("M35.5 38.5 47.5 43"), 2.2),
           G::X("M38 57C44 60 56 60 62 57C60 64 55 67 50 67C45 67 40 64 38 57Z"), G::XS(sym(tusk), 2.4), G::F(sym(tusk)),
           G::X(circ(50, 24, 1.8) + circ(41, 24, 1.4) + circ(59, 24, 1.4))});
  }
  // Кровавая рука: отпечаток ладони с когтистыми пальцами, потёки и брызги.
  {
    const Pt kb[] = {{38, 48}, {46.5, 44}, {55.5, 44}, {63.5, 49}, {35, 66}};
    const Pt kt[] = {{27, 21}, {42, 13}, {60, 14}, {75, 27}, {14, 50}};
    const double kw[] = {14, 14.5, 14, 12.5, 14};
    std::string fingers;
    for (int i = 0; i < 5; i++) {
      const Pt b = kb[i], t = kt[i];
      const float dx = t.x - b.x, dy = t.y - b.y, len = std::hypot(dx, dy), ux = dx / len, uy = dy / len;
      fingers += taper(b, {b.x + dx / 3, b.y + dy / 3}, {b.x + dx * 2 / 3, b.y + dy * 2 / 3}, t, kw[i], kw[i] - 3) +
                 circ(t.x, t.y, (kw[i] - 3) / 2) + spike({t.x + ux * 3.f, t.y + uy * 3.f}, std::atan2(uy, ux) * 180 / kPi, 7, 2.6);
    }
    c.add("race-orcs-hand", "Кровавая рука",
          {G::F(fingers + "M31 54C31 46 38 42 50 42C62 42 70 46 70 54V70C70 79 63 85 50 86C37 85 31 79 31 70Z" +
                "M37 81C36 88 36 92 37.5 95C39 97.5 42 96.5 42 93.5C42 90 41 86 40.5 82Z" +
                "M47 84C46.5 88 46.5 90.5 47.5 92.5C48.5 94 50.5 93.5 50.5 91.5C50.5 89.5 50 87.5 49.5 84Z" +
                "M57 83C56.5 90 56.5 94.5 58 97.5C59.5 99.5 62 98.5 62 96C62 92 61 87.5 60.5 82Z" + circ(20, 79, 2.6) + circ(83, 38, 2.2) +
                circ(80, 68, 3) + circ(17, 31, 1.8) + circ(73, 88, 1.8) + circ(88, 52, 1.5)),
           G::XS("M39 50V57M47.5 46V53M56.5 46V53M64 51V58", 1.6)});
  }
  // Боевое знамя: древко с рогатой перекладиной, рваное полотнище с клыкастым черепом.
  {
    const std::string cloth = "M20 22H80V76L73 84 66 76 58 88 50 78 42 88 34 76 27 84 20 76Z";
    const std::string skull = circ(50, 43, 13) + rrect(42, 50, 16, 11, 4);
    const std::string tusk = sym("M41 61 38.5 51.5 44 59.5Z");
    c.add("race-orcs-banner", "Боевое знамя",
          {G::F(rrect(47, 6, 6, 92, 2) + "M50 0 55.5 10 50 14 44.5 10Z" + rrect(14, 16, 72, 6, 3) +
                sym(taper({17, 19}, {10, 18}, {6, 13}, {7, 5}, 6, 1.5))),
           G::XS(cloth, 2.4), G::F(cloth), G::XS(skull, 2.6),
           G::X(sym(xf(ell(0, 0, 4.2, 3.4), place(44.5, 44, 18))) + "M50 48.5 52.5 53H47.5Z"), G::XS("M46 56V61M50 56V61M54 56V61", 1.6),
           G::XS(tusk, 2), G::F(tusk)});
  }
  // Боевой барабан: обтянутый кожей барабан со шнуровкой и шипами, палки-кости крест-накрест, ножки-клыки.
  {
    const std::string drum = "M15 36V68C15 75 31 81 50 81C69 81 85 75 85 68V36C85 43 69 47 50 47C31 47 15 43 15 36Z";
    const std::string sticks = xf(bone(44, 5.5, 3.8), place(37, 20, 30)) + xf(bone(44, 5.5, 3.8), place(63, 20, -30));
    c.add("race-orcs-drum", "Боевой барабан",
          {G::F(sym("M25 76 20 95 29 92 32 79Z") + "M46 80H54L52 96H48Z"), G::XS(drum, 3), G::F(drum + spike({15, 50}, 180, 7, 2.6) + spike({85, 50}, 0, 7, 2.6)),
           G::XS("M15 42C15 49 31 53 50 53C69 53 85 49 85 42M15 63C15 70 31 75 50 75C69 75 85 70 85 63", 2),
           G::XS("M19 50 26 70 33 53 41 73 50 54 59 73 67 53 74 70 81 50", 1.8), G::XS(ell(50, 36, 35, 11), 3), G::F(ell(50, 36, 35, 11)),
           G::XS(ell(50, 36, 29, 7.5), 1.6), G::X(sym(circ(41, 36, 1.8)) + "M50 33 52.5 38H47.5Z"), G::XS(sticks, 2.6), G::F(sticks)});
  }
}

// ---------------------------------------------------------------- падшие эльфы
// Обломок кольца-эллипса (cx, cy, rx, ry) толщиной w от a0 до a1 градусов с зазубренными краями (сдвиг излома jag градусов).
std::string ringShard(double cx, double cy, double rx, double ry, double w, double a0, double a1, double jag0, double jag1) {
  auto at = [&](double ra, double rb, double deg) {
    const double t = deg * kPi / 180;
    return Pt{float(cx + ra * std::cos(t)), float(cy + rb * std::sin(t))};
  };
  std::vector<Pt> ou, in;
  const int steps = 24;
  for (int i = 0; i <= steps; i++) {
    const double a = a0 + (a1 - a0) * i / steps;
    ou.push_back(at(rx + w / 2, ry + w / 2, a));
    in.push_back(at(rx - w / 2, ry - w / 2, a));
  }
  std::string s = "M" + ptS(ou[0]);
  for (size_t i = 1; i < ou.size(); i++) s += "L" + ptS(ou[i]);
  s += "L" + ptS(at(rx, ry, a1 + jag1));
  for (size_t i = in.size(); i-- > 0;) s += "L" + ptS(in[i]);
  return s + "L" + ptS(at(rx, ry, a0 - jag0)) + "Z";
}

void fallenElves(Catalog& c) {
  c.group("Падшие Эльфы", EmblemSection::Races);
  // Падший эльф: склонённая голова, завеса волос, поникшие уши и рваные крылья, сложенные руки, складки одеяния.
  {
    const Pt bases[] = {{42, 43}, {37, 37}, {32, 31}, {27, 26}, {22, 21}, {17, 18}};
    const Pt tips[] = {{36, 90}, {27, 93}, {19, 86}, {11, 82}, {6, 70}, {3, 57}};
    std::vector<std::string> feathers;
    for (int k = 0; k < 6; k++) feathers.push_back(feather(bases[k], tips[k], 12));
    feathers.push_back("M47 47C40 35 30 23 15 15C9 16 9 23 13 26C23 32 33 41 41 53Z");
    std::vector<L> v;
    layeredFeathers(v, feathers, 2.2f, true);
    const std::string fig = sym("M50 17C43 17 39.5 22 40 29C40.5 37 38.5 45 35 51C39.5 51 42.5 48 44 44L44.8 33C45.3 28.5 47.3 26 50 25.5Z" +
                                std::string("M44.5 30.5 33 38 44.5 34.5Z")) +
                            ell(50, 29, 5.6, 7.4) +
                            "M38 40C42 38 46 37.5 50 37.5C54 37.5 58 38 62 40C63 46 62 52 59 58L64 92C60 94 55 95 50 95C45 95 40 94 36 92L41 58"
                            "C38 52 37 46 38 40Z";
    v.push_back(G::XS(fig, 3));
    v.push_back(G::F(fig));
    v.push_back(G::XS(sym("M41 42C40 48 42 53.5 46 56M44.8 33C45.3 28.5 47.3 26 50 25.5") + "M47 64 45 92M53 64 55 92", 1.7));
    v.push_back(G::XS(sym("M45.8 30.4C46.6 31.3 47.6 31.5 48.5 31.2"), 1.3));
    v.push_back(G::XS(ell(50, 56, 4.6, 3.6), 2));
    v.push_back(G::F(ell(50, 56, 4.6, 3.6)));
    c.add("race-fallen-elves-angel", "Падший эльф", v);
  }
  // Сломанный нимб: три обломка с изломами, лучи сияния, осколки и падающее перо.
  {
    const std::string a = ringShard(50, 30, 40, 16, 8, 200, 330, 7, -6);
    const std::string b = xf(ringShard(50, 30, 40, 16, 8, 22, 162, -6, 7), rotAt(6, 50, 46) * Affine::translate(0, 5));
    const std::string d = xf(ringShard(50, 30, 40, 16, 8, 336, 370, -5, 5), rotAt(22, 90, 32) * Affine::translate(0, 12));
    std::string rays;
    auto ep = [](double r, double deg) {
      const double t = deg * kPi / 180;
      return ptS({float(50 + (40 + r) * std::cos(t)), float(30 + (16 + r) * std::sin(t))});
    };
    for (double deg = 216; deg <= 325; deg += 21.5) rays += "M" + ep(7, deg - 4) + "L" + ep(14, deg) + "L" + ep(7, deg + 4) + "Z";
    const Pt fb{57, 91}, ft{42, 49};
    const std::string fea = feather(fb, ft, 17) + "M56 90.5 59.5 98.5 61 97.5 58.5 90Z";
    const double fl = std::hypot(ft.x - fb.x, ft.y - fb.y), dx = (ft.x - fb.x) / fl, dy = (ft.y - fb.y) / fl;
    std::string barbs;
    for (double t : {0.3, 0.52}) {
      for (int sd : {-1, 1}) {
        const double px = fb.x + dx * fl * t, py = fb.y + dy * fl * t, nx = -dy * sd, ny = dx * sd;
        barbs += "M" + n(px + nx * 2.8) + " " + n(py + ny * 2.8) + "L" + n(px + nx * 9 + dx * 4) + " " + n(py + ny * 9 + dy * 4);
      }
    }
    c.add("race-fallen-elves-halo", "Сломанный нимб",
          {G::F(a + b + d + rays + spike({7, 40}, 170, 7, 2.4) + spike({11, 53}, 140, 6, 2.2) + spike({82, 64}, 50, 6, 2.2)), G::XS(fea, 3.5),
           G::F(fea), G::XS("M56 88C53.5 76 49 63 43.5 52", 1.6), G::XS(barbs, 1.8)});
  }
  // Падающая звезда: перевёрнутая гранёная звезда летит вниз, за ней шлейф.
  {
    const double cx = 33, cy = 66;
    std::string s, facets;
    for (int i = 0; i < 10; i++) s += (i == 0 ? "M" : "L") + pt(cx, cy, i % 2 ? 10 : 25, 126 + 36 * i);
    for (int i = 0; i < 5; i++) facets += "M" + n(cx) + " " + n(cy) + "L" + pt(cx, cy, 21, 126 + 72 * i);
    c.add("race-fallen-elves-star", "Падающая звезда",
          {G::F(taper({40, 56}, {58, 38}, {74, 22}, {93, 5}, 15, 1) + taper({30, 50}, {40, 36}, {50, 22}, {62, 6}, 9, 1) +
                taper({50, 70}, {64, 62}, {78, 52}, {95, 38}, 9, 1)),
           G::XS(s + "Z", 4), G::F(s + "Z"), G::XS(facets, 1.8), G::X(circ(cx, cy, 3)),
           G::F(star(82, 72, 5, 1.5, 4, -90) + star(20, 30, 4, 1.2, 4, -90) + star(68, 86, 3, 1, 4, -90))});
  }
  // Увядшее древо: голые поникшие ветви, трещина в стволе, корни и опадающие листья.
  {
    const std::string branches = taper({48, 48}, {38, 30}, {22, 22}, {9, 35}, 8, 1.5) + taper({52, 46}, {62, 28}, {78, 20}, {91, 33}, 8, 1.5) +
                                 taper({50, 42}, {50, 28}, {46, 18}, {50, 6}, 7, 1.5) + taper({47, 60}, {38, 53}, {27, 53}, {18, 63}, 5.5, 1.2) +
                                 taper({53, 56}, {63, 49}, {74, 50}, {83, 60}, 5.5, 1.2) + taper({33, 28}, {31, 21}, {30, 16}, {33, 10}, 3.4, 1) +
                                 taper({67, 26}, {69, 19}, {70, 14}, {67, 8}, 3.4, 1) + taper({20, 26}, {16, 24}, {12, 22}, {7, 22}, 3, 1) +
                                 taper({81, 24}, {85, 22}, {89, 21}, {94, 22}, 3, 1) + taper({28, 54}, {26, 48}, {24, 45}, {21, 43}, 2.6, 1);
    const std::string leaves = xf(ell(0, 0, 2.6, 5), place(16, 75, 35)) + xf(ell(0, 0, 2.6, 5), place(86, 72, -50)) +
                               xf(ell(0, 0, 2.4, 4.6), place(28, 88, 70)) + xf(ell(0, 0, 2.2, 4.2), place(80, 86, -20));
    c.add("race-fallen-elves-tree", "Увядшее древо",
          {G::F(branches + "M41 95C43 84 44.5 70 44.5 58C44.5 50 46 44 48 40H52C54 44 55.5 50 55.5 58C55.5 70 57 84 59 95Z" +
                "M28 97C35 94 40 90 43 85L47 93ZM72 97C65 94 60 90 57 85L53 93Z" + rrect(24, 94, 52, 4, 2) + leaves),
           G::XS("M50 50 48 58 52 64 49 72 51 80", 1.8), G::X(ell(52.5, 70, 1.6, 2.6))});
  }
}

// ---------------------------------------------------------------- пустынные эльфы
void desertElves(Catalog& c) {
  c.group("Пустынные Эльфы", EmblemSection::Races);
  // Скорпион: клешни, сегменты брюшка, хвост с жалом, загнутый вбок.
  {
    const Pt t0{50, 75}, t1{50, 97}, t2{84, 97}, t3{81, 57};
    const std::string tail = taper(t0, t1, t2, t3, 12, 7.5);
    std::string rings;
    for (double t : {0.16, 0.33, 0.5, 0.67, 0.84}) {
      const CurvePt q = bezier(t0, t1, t2, t3, t);
      rings += "M" + ptS({float(q.x - q.nx * 7), float(q.y - q.ny * 7)}) + "L" + ptS({float(q.x + q.nx * 7), float(q.y + q.ny * 7)});
    }
    const std::string claw = "M30 30C22 30 15 24 15 15C15 8 19 3 24 1C22 6 22 10 25 13C27 9 31 7 35 8C33 12 33 16 35 19C35 25 34 29 30 30Z";
    c.add("race-desert-elves-scorpion", "Скорпион",
          moved({G::S(sym("M43 39 31 34 25 39M43 44 29 44 21 50M43 49 31 53 25 61M44 54 35 60 31 68"), 3.2),
                 G::S(sym("M45 33C40 30 36 27 32 25"), 5.5), G::F(sym(claw)), G::XS(sym("M25 13 30 18"), 2),
                 G::F(ell(50, 36, 9, 10.5) + ell(50, 50.5, 10.5, 6.5) + ell(50, 59, 10, 6) + ell(50, 67, 8.8, 5.5) + ell(50, 74, 7.5, 4.5) + tail +
                      ell(81, 53, 5.5, 6.5) + "M78 49C76 42 71 39 64 40C69 43 71 47 72 52Z"),
                 G::XS("M41 44.5H59M40.5 54.5H59.5M41.5 63H58.5M43 71H57", 2), G::XS(rings, 1.8), G::XS("M77 57.5C79 59 83 59 85 57.5", 1.6),
                 G::X(sym(circ(46.5, 30, 1.4)))},
                Affine::translate(-4, 1)));
  }
  // Полумесяц над дюнами: месяц со звездой, слоистые барханы с острыми гребнями.
  {
    const std::string back = "M3 80C12 74 22 64 34 58C40 64 50 72 64 76C76 79 88 80 97 80V84C97 85.5 96 86.5 94.5 86.5H5.5C4 86.5 3 85.5 3 84Z";
    const std::string mid = "M3 93C18 86 34 78 54 71C60 78 72 86 86 90C90 91 94 91.5 97 91.5V95C97 96.5 96 97.5 94.5 97.5H5.5C4 97.5 3 96.5 3 95Z";
    c.add("race-desert-elves-dunes", "Полумесяц над дюнами",
          {G::F(circ(46, 32, 27)), G::X(circ(57, 25.5, 23)), G::F(star(65, 39, 8, 3, 5, -90)), G::F(back),
           G::XS("M34 58C37 66 43 74 51 80M14 75C19 77 24 80 28 84", 2), G::XS(mid, 3), G::F(mid), G::XS("M54 71C57 80 63 88 70 94", 2),
           G::XS("M75 83C79 84.5 83 85.5 87 86M20 88C24 87.5 28 88 31 89", 1.6)});
  }
  // Скрещённые шамширы: изогнутые клинки остриями вверх, крестовины с шариками, загнутые навершия.
  {
    const std::string sword = "M46.5 72C44 54 46 36 53 21C57 13 62 8 69 3C65 11 63 17 62 25C60 40 57 56 53.5 72Z" + rrect(37, 70.5, 26, 4.6, 2.3) +
                              circ(37, 72.8, 3.2) + circ(63, 72.8, 3.2) + "M47 75H53L52.5 88H47.5Z" + "M46.5 87.5H53C56 87.5 57.5 90 56 93L54.5 96H46.5Z";
    const Affine m = rotAt(30, 50, 56);
    const std::string front = xf(sword, m), back = mirrorX(front, 50);
    const std::string fuller = xf("M49.5 68C48.5 54 50 40 55 28", m);
    c.add("race-desert-elves-shamshir", "Скрещённые шамширы",
          moved({G::F(back), G::XS(mirrorX(fuller, 50), 1.6), G::XS(front, 4), G::F(front), G::XS(fuller, 1.6), G::F(circ(50, 9, 7)),
                 G::X(circ(53.5, 7, 6))},
                Affine::translate(0, 2)));
  }
  // Сокол пустыни: летящий сокол снизу — серповидные острые крылья, клиновидный хвост, пестрины на груди.
  {
    const std::string wing = "M46 35C37 29 24 27 14 31C9 34 5 41 2 51C10 47 18 47 26 51C32 54 38 58 46 61Z";
    c.add("race-desert-elves-falcon", "Сокол пустыни",
          {G::F(sym(wing) + circ(50, 25, 7.5) + "M50 12.5 53.5 19.5H46.5Z" + ell(50, 45, 8, 17) + "M45 58L40 85C46 88 54 88 60 85L55 58Z"),
           G::XS(sym("M10 48.5 16 41M17 47.5 22.5 40.5M24 49.5 28.5 42.5M31 53 34.5 46M38 57 40.5 50"), 1.8),
           G::XS(sym("M44 41C34 37 23 36 13 39"), 1.8), G::XS("M46 43 50 45 54 43M45.5 49 50 51 54.5 49M46 55 50 57 54 55", 1.6),
           G::XS("M43 77C47 79 53 79 57 77", 1.6), G::X(circ(46.5, 24, 1.4) + circ(53.5, 24, 1.4))});
  }
}

// ---------------------------------------------------------------- ракшасы
void rakshasas(Catalog& c) {
  c.group("Ракшасы", EmblemSection::Races);
  // Голова ракшасы: тигриная морда анфас в высокой короне, полосы, клыки.
  {
    const std::string head = sym("M50 24C40 24 32 27 27 33C22 30 16 30 14 34C12 38 14 44 19 47C17 52 16 57 17 62L9 66 18 70 12 76 22 77"
                                 "C26 84 34 90 42 92L50 94H50.5V24Z");
    const std::string crown = "M32 27H68L66 17 59 20 57 9 50 1 43 9 41 20 34 17Z";
    const std::string stripes = taper({29, 37}, {34, 37}, {38, 39}, {42, 44}, 3.6, 0.6) + taper({18, 57}, {24, 57}, {28, 59}, {33, 62}, 3.6, 0.6) +
                                taper({19, 65}, {25, 65}, {29, 67}, {34, 69}, 3.2, 0.6) + taper({24, 42}, {27, 43}, {29, 45}, {31, 48}, 3, 0.6);
    c.add("race-rakshasas-head", "Голова ракшасы",
          {G::XS(head, 3), G::F(head), G::XS(crown, 2.6), G::F(crown), G::X(circ(50, 14, 2.4) + circ(42.5, 22.5, 1.5) + circ(57.5, 22.5, 1.5)),
           G::XS(sym("M18 36C20 34 23 34 25 36"), 1.8),
           G::X(sym(stripes + "M33 49C37 45.5 42 45.5 46 49C42 52 37 52 33 49Z") + "M50 28 47.5 35 50 39 52.5 35Z"),
           G::F(sym(ell(40, 49, 1.1, 2.4))),
           G::X("M42.5 58C42.5 56 44 55 46 55H54C56 55 57.5 56 57.5 58C57.5 61 54 64 50 66C46 64 42.5 61 42.5 58Z" +
                std::string("M38 72C44 70 47 70 50 72C53 70 56 70 62 72C60 80 56 84 50 84C44 84 40 80 38 72Z") +
                sym(circ(40, 67, 1) + circ(37, 64.5, 1) + circ(42, 64, 1))),
           G::XS(sym("M46 50C46.5 52.5 46.5 54 46 55") + "M50 66V71", 1.6), G::F(sym("M41 72.5 43.5 81.5 46 72Z"))});
  }
  // Когтистая длань: поднятая ладонь с оком, длинные когти, браслет с камнями, полосатое запястье.
  {
    std::string fingers, claws;
    const double fx[] = {30, 40.5, 51, 61.5}, fw[] = {9, 9.5, 9.5, 8.5}, top[] = {20, 13, 15, 23};
    for (int i = 0; i < 4; i++) {
      fingers += rrect(fx[i], top[i], fw[i], 52 - top[i], fw[i] / 2);
      const float xm = float(fx[i] + fw[i] / 2), t = float(top[i]);
      claws += taper({xm, t + 4}, {xm, t - 2}, {xm + 1.5f, t - 7}, {xm + 4.5f, t - 11}, 7, 0.4);
    }
    const std::string thumb = taper({33, 70}, {28, 62}, {22, 54}, {18, 47}, 12, 9) + circ(18, 47, 4.5) +
                              taper({17, 44}, {15, 40}, {14, 36}, {16, 31}, 6, 0.4);
    c.add("race-rakshasas-claws", "Когтистая длань",
          moved({G::F(fingers + claws + thumb + "M30 48H70V72C70 79 65 84 58 84H42C35 84 30 79 30 72Z"),
                 G::XS("M40 18V54M50.5 12V54M61 20V54", 1.8), G::XS("M31 36H39M41 30H50M51 32H60M62 38H69", 1.5),
                 G::X("M37 64C41 58.5 59 58.5 63 64C59 69.5 41 69.5 37 64Z"), G::F(circ(50, 64, 4.2)), G::X(circ(50, 64, 1.6)),
                 G::XS(rrect(28, 82, 44, 7, 3), 2.4), G::F(rrect(28, 82, 44, 7, 3) + rrect(33, 89, 34, 9, 2)),
                 G::X(circ(37, 85.5, 1.6) + circ(50, 85.5, 2) + circ(63, 85.5, 1.6) + taper({34, 92}, {38, 92}, {41, 93}, {44, 95}, 2.6, 0.5) +
                      taper({66, 92}, {62, 92}, {59, 93}, {56, 95}, 2.6, 0.5))},
                Affine::translate(7, 0)));
  }
  // Тришула: трезубец с пламенными боковыми зубьями, барабан-дамару на древке, подвески с бусинами.
  {
    const std::string prong = "M45 39C31 42 18 35 18 22C18 15 22 9 28 3C27 11 28 18 33 22C36 25 41 27 46 28Z";
    const std::string drum = "M37 47C37 45.5 42 44.5 50 44.5C58 44.5 63 45.5 63 47L54 55 63 63C63 64.5 58 65.5 50 65.5C42 65.5 37 64.5 37 63L46 55Z";
    c.add("race-rakshasas-trishula", "Тришула",
          {G::F(sym(prong) + "M50 0C55 8 58 17 57 25L54.5 32H45.5L43 25C42 17 45 8 50 0Z" + "M35 28H65L60 39H40Z" + rrect(46.5, 37, 7, 55, 2.5) +
                "M46.5 90H53.5L50 99Z"),
           G::XS("M50 6V27M22 21C24 29 34 34 43 34.5M78 21C76 29 66 34 57 34.5", 1.6), G::XS("M37.5 33.5H62.5", 1.6), G::XS(drum, 2.6),
           G::F(drum), G::XS("M39 47.5 61 62.5M61 47.5 39 62.5", 1.5), G::XS("M37 47H63M37 63H63", 1.4),
           G::S(sym("M45 55C38 58 33 64 31 73"), 2.4), G::F(sym(circ(31, 76, 3.6) + "M28.5 79 31 86 33.5 79Z"))});
  }
  // Тигриный трон: стрельчатая спинка, подлокотники с тигриными головами, лапы-ножки, полосатое сиденье.
  {
    const std::string back = "M24 60V30C24 18 36 11 50 2C64 11 76 18 76 30V60Z";
    std::string tigers;
    for (double x : {15.0, 85.0}) tigers += circ(x, 50, 8) + circ(x - 5.5, 43, 3) + circ(x + 5.5, 43, 3);
    std::string tigerCuts;
    for (double x : {15.0, 85.0})
      tigerCuts += ell(x - 3.2, 49, 1.6, 1.1) + ell(x + 3.2, 49, 1.6, 1.1) + "M" + n(x - 2) + " 53H" + n(x + 2) + "L" + n(x) + " 55.5Z";
    c.add("race-rakshasas-throne", "Тигриный трон",
          {G::F(back), G::X("M32 58V32C32 24 40 18 50 12C60 18 68 24 68 32V58Z"),
           G::F(star(50, 33, 10, 4, 8, -90) + circ(50, 33, 5.5) + "M36 58V40C36 36 39 34 42 34C40 38 40 46 42 50L46 58ZM64 58V40C64 36 61 34 58 34"
                "C60 38 60 46 58 50L54 58Z"),
           G::X(circ(50, 33, 2.2)), G::F(rrect(14, 56, 72, 12, 3) + rrect(10, 54, 10, 22, 3) + rrect(80, 54, 10, 22, 3)), G::XS(tigers, 2.6),
           G::F(tigers), G::X(tigerCuts), G::XS("M14 60.5H86", 1.8),
           G::X(taper({26, 63}, {30, 63}, {33, 64}, {36, 66}, 2.6, 0.5) + taper({44, 63}, {47, 63}, {49, 64}, {51, 66}, 2.6, 0.5) +
                taper({60, 63}, {63, 63}, {65, 64}, {67, 66}, 2.6, 0.5)),
           G::F(rrect(22, 68, 9, 18, 2) + rrect(69, 68, 9, 18, 2) + ell(26.5, 86, 7, 4.5) + ell(73.5, 86, 7, 4.5) + rrect(8, 90, 84, 7, 2.5)),
           G::XS("M23.5 87V90.5M27 87V90.5M71 87V90.5M74.5 87V90.5", 1.4), G::XS("M8 90H92", 2)});
  }
}

// ---------------------------------------------------------------- сатиры
// Гроздь винограда: ряды ягод 3–2–1 от верхней точки (x, y), радиус ягоды r.
std::string grapeBunch(double x, double y, double r) {
  const double d = r * 1.75;
  return circ(x - d, y, r) + circ(x, y, r) + circ(x + d, y, r) + circ(x - d / 2, y + d * 0.85, r) + circ(x + d / 2, y + d * 0.85, r) +
         circ(x, y + d * 1.7, r);
}

// Лист плюща/лозы: три доли от черенка в (0, 0) вверх (по оси −y), жилки — отдельно.
const char* const kVineLeaf = "M0 0C-2 -1-6-1-8.5-3.5C-10-6-8-9-5-8.5C-6-11-4-14 0-15C4-14 6-11 5-8.5C8-9 10-6 8.5-3.5C6-1 2-1 0 0Z";
const char* const kVineVeins = "M0 -1V-11M0 -4.5 -5 -6.5M0 -4.5 5 -6.5";

void satyrs(Catalog& c) {
  c.group("Сатиры", EmblemSection::Races);
  // Флейта Пана: семь трубок разной длины, две обвязки и лента.
  {
    std::string tubes, holes, seams;
    for (int i = 0; i < 7; i++) {
      const double x = 17 + i * 9.6;
      tubes += rrect(x, 10, 8, 82 - i * 6.5 - 10, 3);
      holes += ell(x + 4, 14, 2.3, 1.6);
      if (i) seams += "M" + n(x - 0.8) + " 12V" + n(82 - (i - 1) * 6.5);
    }
    c.add("race-satyrs-pipes", "Флейта Пана",
          moved({G::F(tubes), G::XS(seams, 1.6), G::X(holes), G::XS(rrect(13, 26, 75, 7, 2) + rrect(13, 44, 75, 7, 2), 2.4),
                 G::F(rrect(13, 26, 75, 7, 2) + rrect(13, 44, 75, 7, 2)), G::XS("M13 29.5H88M13 47.5H88", 1.2),
                 G::F(taper({87, 30}, {95, 40}, {84, 52}, {92, 66}, 6, 2.4) + taper({87, 30}, {98, 34}, {95, 46}, {98, 56}, 5, 2))},
                Affine::translate(-1, 1) * rotAt(-12, 50, 50) * scaleAt(1.04f, 50, 50)));
  }
  // Рога и венок: кольчатые козлиные рога над венком из лозы с листьями и гроздьями.
  {
    const Pt h0{41, 47}, h1{36, 27}, h2{23, 12}, h3{7, 10};
    const std::string horn = taper(h0, h1, h2, h3, 15, 2.5);
    std::string rings;
    for (double t : {0.22, 0.38, 0.54, 0.7}) {
      const CurvePt q = bezier(h0, h1, h2, h3, t);
      const double w = (15 + (2.5 - 15) * t) * 0.5;
      rings += "M" + ptS({float(q.x + q.nx * (w + 1)), float(q.y + q.ny * (w + 1))}) + "L" + ptS({float(q.x - q.nx * w * 0.2), float(q.y - q.ny * w * 0.2)});
    }
    std::string leaves, veins;
    for (double a : {204.0, 168.0, 134.0}) {
      const Pt p = polar(50, 62, 28, a);
      leaves += sym(xf(kVineLeaf, place(p.x, p.y, float(a + 90), 1.25f)));
      veins += sym(xf(kVineVeins, place(p.x, p.y, float(a + 90), 1.25f)));
    }
    const std::string grapes = sym(grapeBunch(43.5, 80, 3.2));
    c.add("race-satyrs-horns", "Рога и венок",
          {G::XS(sym(horn), 3), G::F(sym(horn)), G::XS(sym(rings), 1.6), G::S(sym(arc(50, 62, 28, 98, 220)), 3.6), G::XS(leaves, 2.4),
           G::F(leaves), G::XS(veins, 1.4), G::XS(grapes, 2.2), G::F(grapes), G::XS(grapes, 1)});
  }
  // Кубок вина: канфар с высокими ручками, плеск вина, меандр на чаше.
  {
    const std::string bowl = "M25 32H75C75 50 65 60 50 62C35 60 25 50 25 32Z";
    c.add("race-satyrs-goblet", "Кубок вина",
          {G::S(sym("M27 37C13 35 10 16 21 13C28 11 31 19 29 28"), 4.2), G::F(bowl + rrect(21, 27, 58, 6, 3)),
           G::F("M31 27C34 23 38 24 41 21C43 18 41 15 44 13C45 17 49 19 52 17C55 15 56 12 59 12C58 16 60 20 63 22C65 24 67 25 69 27Z" +
                circ(37, 15, 2.2) + circ(66, 15, 1.8) + circ(51, 7, 1.6)),
           G::XS("M21 33H79", 1.8), G::XS("M30 41H34V45H38V41H42V45H46V41H50V45H54V41H58V45H62V41H66V45H70", 1.6),
           G::XS("M36 52C42 55 58 55 64 52", 1.6), G::F(rrect(46, 61, 8, 17, 2) + ell(50, 68, 6.5, 3) + "M31 92C33 83 41 78 50 78C59 78 67 83 69 92Z"),
           G::XS("M35 87C40 84 60 84 65 87", 1.6)});
  }
  // Виноградная гроздь: ягоды рядами 5–4–3–2–1, лозовые листья, черенок и усик.
  {
    std::string berries, shades;
    const double r = 6, d = 11.4;
    for (int row = 0; row < 5; row++)
      for (int i = 0; i < 5 - row; i++) {
        const double x = 50 + (i - (4 - row) / 2.0) * d, y = 41 + row * 9.8;
        berries += circ(x, y, r);
        shades += arc(x, y, r, 15, 165);
      }
    const std::string leafA = xf(kVineLeaf, place(45, 31, -48, 2.05f)), leafB = xf(kVineLeaf, place(56, 30, 52, 1.6f));
    c.add("race-satyrs-grapes", "Виноградная гроздь",
          {G::S("M50 36C50 28 51 22 54 16", 4), G::S("M54 18C62 16 68 10 66 5C65 2 61 3 62 6", 2.2), G::F(leafA + leafB),
           G::XS(xf(kVineVeins, place(45, 31, -48, 2.05f)) + xf(kVineVeins, place(56, 30, 52, 1.6f)), 1.8), G::XS(berries, 2.2), G::F(berries),
           G::XS(shades, 1.4)});
  }
}

// ---------------------------------------------------------------- светлые эльфы
void lightElves(Catalog& c) {
  c.group("Светлые Эльфы", EmblemSection::Races);
  // Солнце в листве: солнечный диск со звездой, лучи — эльфийские листья с прожилками, между ними короткие лучи.
  {
    std::vector<std::string> leaves;
    std::string veins, rays;
    for (int i = 0; i < 10; i++) {
      const double a = -90 + 36 * i;
      const Pt b = polar(50, 50, 20, a), t = polar(50, 50, i % 2 ? 40 : 47, a);
      leaves.push_back(feather(b, t, i % 2 ? 9 : 10.5));
      veins += "M" + ptS(polar(50, 50, 23, a)) + "L" + ptS(polar(50, 50, i % 2 ? 35 : 41, a));
      rays += "M" + pt(50, 50, 21, a + 18 - 4) + "L" + pt(50, 50, 31, a + 18) + "L" + pt(50, 50, 21, a + 18 + 4) + "Z";
    }
    std::vector<L> v{G::F(rays)};
    for (const std::string& l : leaves) v.push_back(G::F(l));
    v.push_back(G::XS(veins, 1.5));
    v.push_back(G::XS(circ(50, 50, 17), 3));
    v.push_back(G::F(circ(50, 50, 17)));
    v.push_back(G::X(star(50, 50, 11, 3.6, 8, -90)));
    v.push_back(G::F(circ(50, 50, 2.6)));
    c.add("race-light-elves-sun", "Солнце в листве", v);
  }
  // Единорог: голова в профиль с витым рогом, волнистой гривой и чёлкой.
  {
    const std::string head = "M57 97C57 82 51 70 43 62C39 63 35 63 31 61C25 61 19 59 15 55C12 52 13 48 16 46L33 31C37 26 43 22 50 21L53 12 58 22"
                             "C65 25 71 33 75 43C80 56 84 76 86 97Z";
    const std::string horn = taper({43, 25}, {38, 17}, {32, 10}, {25, 3}, 7, 0.6);
    const std::string mane = taper({59, 24}, {70, 24}, {77, 29}, {84, 26}, 9, 1) + taper({67, 35}, {78, 38}, {85, 45}, {93, 43}, 10, 1) +
                             taper({73, 50}, {84, 55}, {89, 62}, {96, 62}, 10, 1) + taper({78, 66}, {88, 71}, {91, 79}, {97, 81}, 9, 1) +
                             taper({53, 24}, {48, 28}, {42, 30}, {37, 36}, 8, 1);
    c.add("race-light-elves-unicorn", "Единорог",
          {G::F(mane), G::XS(head, 3), G::F(head), G::XS(horn, 2.6), G::F(horn), G::XS("M41.5 21 37 23.5M37.5 15 33 17.5M33 9.5 29 11.5", 1.4),
           G::X("M33 40C36 37.5 40 37.5 42 40C40 42 36 42 33 40Z" + std::string("M18 51.5C19.5 50 21.5 50 22 52C20.5 53 19 53 18 51.5Z")),
           G::XS("M16 56C21 57 26 57 30 56M44 62C48 70 52 80 54 96M58 30C62 40 66 52 70 62", 1.6)});
  }
  // Эльфийская арфа: изогнутая шейка с завитком, резная колонна, дека и струны.
  {
    const Pt n0{28, 20}, n1{42, 6}, n2{58, 32}, n3{80, 25};
    const std::string frame = taper({27, 88}, {22, 66}, {24, 40}, {28, 18}, 7.5, 5.5) + taper(n0, n1, n2, n3, 7.5, 7) + taper({80, 25}, {73, 45}, {52, 70}, {31, 89}, 8, 14) +
                              circ(26, 15, 6) + rrect(18, 86, 22, 9, 3);
    std::string strings;
    for (double t = 0.22; t < 0.95; t += 0.1) {
      const CurvePt q = bezier(n0, n1, n2, n3, t);
      const double yb = 25 + (80 - q.x) * 1.29 - 7;
      if (yb > q.y + 8) strings += "M" + n(q.x) + " " + n(q.y + 3) + "V" + n(yb);
    }
    c.add("race-light-elves-harp", "Эльфийская арфа",
          {G::S(strings, 1.5), G::F(frame), G::X(spiral(26, 15, 0.5, 4.2, 0, 400, 1.6, 1.6, 30)), G::XS("M25 30C23 46 23 62 25.5 80", 1.5),
           G::XS("M74 33C66 47 54 62 40 76", 1.6), G::X(circ(57, 58, 2.2) + circ(45, 72, 2)), G::XS("M18 90H40", 1.4)});
  }
  // Крылатый шлем: эльфийский шлем с наносником, по бокам — поднятые крылья, на лбу — звезда.
  {
    const Pt bases[] = {{32, 58}, {32, 52}, {32, 46}, {33, 40}, {35, 34}};
    const Pt tips[] = {{14, 60}, {8, 50}, {5, 39}, {6, 27}, {12, 15}};
    std::vector<std::string> feathers;
    for (int k = 0; k < 5; k++) feathers.push_back(feather(bases[k], tips[k], 10));
    feathers.push_back("M36 30C30 22 22 16 14 13C11 16 13 21 18 24C24 28 30 34 34 40Z");
    std::vector<L> v;
    layeredFeathers(v, feathers, 2.2f, true);
    const std::string helm = sym("M50 14C38 14 30 24 30 38V64C30 72 35 78 41 82L45 90H50.5V14Z");
    for (const L& l : std::vector<L>{G::XS(helm, 3), G::F(helm), G::X(sym("M37 50C37 46 41 44 45 45.5L47.5 47V70C46 74 42 75 39.5 71C37.5 66 37 58 37 50Z")),
                                     G::XS("M50 16V36M31 41C38 38 62 38 69 41", 2), G::X(star(50, 28, 5.5, 1.8, 4, -90)),
                                     G::XS(sym("M33 66C35 74 39 80 44 84"), 1.6)})
      v.push_back(l);
    c.add("race-light-elves-helm", "Крылатый шлем", v);
  }
}

// ---------------------------------------------------------------- сумеречные эльфы
void twilightElves(Catalog& c) {
  c.group("Сумеречные Эльфы", EmblemSection::Races);
  // Солнце и луна: половина солнца с лучами слева, месяц со звёздами справа.
  {
    std::string rays;
    for (int i = 0; i < 7; i++) {
      const double a = 108 + 24 * i;
      rays += "M" + pt(50, 50, 30, a - 6) + "L" + pt(50, 50, i % 2 ? 40 : 47, a) + "L" + pt(50, 50, 30, a + 6) + "Z";
    }
    c.add("race-twilight-elves-sunmoon", "Солнце и луна",
          {G::F(rays + "M50 23A27 27 0 0 0 50 77Z" + "M50 21A29 29 0 0 1 50 79A16 29 0 0 0 50 21Z" + star(85, 30, 6, 2, 4, -90) +
                star(88, 66, 4.5, 1.5, 4, -90) + star(64, 50, 4, 1.3, 4, -90)),
           G::XS("M50 14V86", 3), G::XS(arc(50, 50, 19, 110, 250), 1.8)});
  }
  // Двуликая маска: светлая половина с прорезями и солнцем, тёмная — контур со светлыми чертами и месяцем.
  {
    const std::string lightHalf = "M50 8C32 8 20 22 20 42C20 64 32 86 50 94Z";
    std::string sun;
    for (int i = 0; i < 8; i++) sun += "M" + pt(35, 25, 5, 45 * i - 12) + "L" + pt(35, 25, 8, 45 * i) + "L" + pt(35, 25, 5, 45 * i + 12) + "Z";
    c.add("race-twilight-elves-mask", "Двуликая маска",
          {G::F(lightHalf), G::S("M50 8C68 8 80 22 80 42C80 64 68 86 50 94", 4), G::X(circ(35, 25, 3.6) + sun),
           G::X("M27 44C31 39 39 39 43 44C39 48 31 48 27 44Z"), G::XS("M26 37C31 34 38 34 43 37M48 50C47 56 45 61 47 64M36 72C41 75 46 76 50 76", 2),
           G::F("M57 44C61 39 69 39 73 44C69 48 61 48 57 44Z" + std::string("M66 18A7 7 0 1 0 66 32A4.5 7 0 1 1 66 18Z") +
                "M65 50C66.5 53 67 55 65 57C63 55 63.5 53 65 50Z"),
           G::S("M57 37C62 34 69 34 74 37M50 76C54 76 59 75 64 72", 2.2), G::X(circ(65, 44, 1.6))});
  }
  // Весы равновесия: коромысло на колонне со звездой, на чашах — солнце и месяц.
  {
    std::string sunRays;
    for (int i = 0; i < 8; i++) sunRays += "M" + pt(15, 45, 8.5, 45 * i - 14) + "L" + pt(15, 45, 12.5, 45 * i) + "L" + pt(15, 45, 8.5, 45 * i + 14) + "Z";
    const std::string pan = "M3 62H27C27 68.5 21.5 73 15 73C8.5 73 3 68.5 3 62Z";
    c.add("race-twilight-elves-scales", "Весы равновесия",
          {G::F(rrect(46.5, 20, 7, 68, 2.5) + "M30 94C32 88 40 85 50 85C60 85 68 88 70 94Z" + rrect(26, 92.5, 48, 4.5, 2.2) +
                star(50, 11, 10, 2.8, 4, -90)),
           G::XS("M47 30H53M47 80H53", 1.4), G::S("M13 30C30 25 70 25 87 30", 4.6), G::XS(circ(50, 26.5, 5.5), 2), G::F(circ(50, 26.5, 5.5)),
           G::S(sym("M15 31 5 62M15 31 25 62"), 1.8), G::XS(circ(15, 45, 7) + "M86 35A10 10 0 1 0 86 55A6.5 10 0 1 1 86 35Z", 2.4),
           G::F(sym(pan) + circ(15, 45, 6.5) + sunRays + "M86 35A10 10 0 1 0 86 55A6.5 10 0 1 1 86 35Z"),
           G::X(circ(50, 26.5, 2)), G::XS(sym("M5 66.5H25"), 1.4)});
  }
  // Звезда сумерек: сверкающая звезда над заходящим полосатым солнцем, малые звёзды.
  {
    const std::string sparkle = "M50 4Q53 28 74 30Q53 32 50 55Q47 32 26 30Q47 28 50 4Z";
    c.add("race-twilight-elves-star", "Звезда сумерек",
          {G::F("M20 86A30 30 0 0 1 80 86Z" + rrect(6, 87, 88, 5, 2.5)), G::XS("M18 66H82M14 74H86M10 81H90", 2.6), G::XS(sparkle, 4),
           G::F(sparkle + star(19, 18, 7, 1.8, 4, -90) + star(82, 14, 5, 1.4, 4, -90) + star(86, 52, 3.5, 1, 4, -90) + star(14, 54, 3.5, 1, 4, -90)),
           G::XS("M50 10V50M31 30H69", 1.4)});
  }
}

// ---------------------------------------------------------------- сянмаорены
// Лицо панды в круге (cx, cy) радиуса r: пятна вокруг глаз, глаза с бликами, нос и рот (вырезы и заливки поверх силуэта).
std::vector<L> pandaFace(float cx, float cy, float r) {
  const Affine m = Affine::translate(cx, cy) * Affine::scale(r / 30.f);
  const std::string patches = sym(xf(ell(0, 0, 7.5, 10.5), place(38, 52, 38))), eyes = sym(circ(39, 51, 3));
  return moved({G::X(patches), G::F(eyes), G::X(sym(circ(39.8, 50.2, 1.1))),
                G::X("M44 63C44 60.5 47 59.5 50 59.5C53 59.5 56 60.5 56 63C56 65.5 53 67.5 50 67.5C47 67.5 44 65.5 44 63Z"),
                G::XS("M50 67V71M43.5 72.5C46.5 75 50 74 50 71C50 74 53.5 75 56.5 72.5", 1.8)},
               m * Affine::translate(-50, -55));
}

void xianmaoren(Catalog& c) {
  c.group("Сянмаорены", EmblemSection::Races);
  // Голова панды: круглые уши, пушистые щёки, пятна вокруг глаз, нос и улыбка.
  {
    std::vector<L> v{G::F(circ(23, 23, 12) + circ(77, 23, 12)), G::XS(sym(circ(23, 23, 6)), 2),
                     G::XS("M50 22C70 22 86 34 88 52L94 56 88 60C88 78 72 92 50 92C28 92 12 78 12 60L6 56 12 52C14 34 30 22 50 22Z", 3),
                     G::F("M50 22C70 22 86 34 88 52L94 56 88 60C88 78 72 92 50 92C28 92 12 78 12 60L6 56 12 52C14 34 30 22 50 22Z")};
    for (const L& l : pandaFace(50, 58, 33)) v.push_back(l);
    v.push_back(G::XS("M40 30C44 28 56 28 60 30", 1.8));
    c.add("race-xianmaoren-head", "Голова панды", v);
  }
  // Панда-монах: медитирующая панда в позе лотоса с чётками, за спиной посох с кольцами.
  {
    const std::string body = "M50 44C36 44 28 54 27 66C26 74 28 79 32 82H68C72 79 74 74 73 66C72 54 64 44 50 44Z" + ell(50, 85, 33, 10) +
                             circ(50, 28, 15.5) + circ(37, 15.5, 6.2) + circ(63, 15.5, 6.2);
    std::vector<L> v{G::S("M17 97 85 7", 4.2), G::S(circ(88, 6, 4.5), 2.4), G::XS(body, 3.4), G::F(body)};
    for (const L& l : pandaFace(50, 30, 15.5)) v.push_back(l);
    v.push_back(G::XS(sym("M33.5 53C30 62 32 71 40 76M28 86C36 82 44 82 50 84"), 2));
    v.push_back(G::XS(circ(45, 76, 4.6) + circ(55, 76, 4.6), 2.2));
    v.push_back(G::F(circ(45, 76, 4.6) + circ(55, 76, 4.6)));
    v.push_back(G::XS("M50 72V80", 1.4));
    std::string beads;
    for (int i = 0; i < 9; i++) beads += circ(50 + 13 * std::cos((25 + 16.25 * i) * kPi / 180), 44 + 9 * std::sin((25 + 16.25 * i) * kPi / 180), 1.6);
    v.push_back(G::X(beads));
    c.add("race-xianmaoren-monk", "Панда-монах", v);
  }
  // Бамбук: три стебля с коленцами и листья пучками.
  {
    const std::string stalks = "M28 97 30.5 6H37.5L36 97Z" + std::string("M46 97 48 18H55L54 97Z") + "M64 97 66.5 32H73.5L72 97Z";
    const std::string nodes = "M29 30H38M28.5 56H37.5M28 80H37M47 42H56M46.5 66H55.5M46 88H55M65 52H74M64.5 74H73.5";
    std::vector<std::string> leaves;
    const struct {
      double x, y, a, len;
    } lv[] = {{37.5, 30, -20, 22}, {37.5, 30, 12, 24}, {37.5, 30, 42, 19}, {29.5, 56, 200, 22}, {29.5, 56, 168, 24}, {29.5, 56, 140, 18},
              {55, 42, -12, 22}, {55, 42, 20, 20}, {47, 66, 195, 20}, {47, 66, 162, 18}, {73.5, 52, -8, 20}, {73.5, 52, 24, 22},
              {73.5, 52, 52, 16}, {65, 74, 190, 18}, {33.5, 15, 200, 17}};
    for (const auto& l : lv) leaves.push_back(feather({float(l.x), float(l.y)}, polar(l.x, l.y, l.len, l.a), 6.5));
    std::vector<L> v{G::F(stalks), G::XS(nodes, 2.4), G::XS("M33.5 34V52M32.5 60V76M51.5 46V62M50.5 70V84M70 56V70", 1.2)};
    layeredFeathers(v, leaves, 1.8f, false);
    c.add("race-xianmaoren-bamboo", "Бамбук", v);
  }
  // Инь-ян с драконом: круг инь-ян, вокруг — дракон с гребнем, рогатой головой и хвостом.
  {
    const double cx = 50, cy = 50, R = 36;
    const std::string body = taperArc(cx, cy, R, -122, 182, 12, 2.5, 48);
    std::string spines, scales;
    for (double a = -104; a <= 160; a += 16) spines += spike(polar(cx, cy, R + 4.5, a), a, 6.5, 2.6);
    for (double a = -100; a <= 150; a += 16) scales += "M" + pt(cx, cy, R - 5, a - 4) + "Q" + pt(cx, cy, R, a + 2) + " " + pt(cx, cy, R + 5, a - 4);
    const Affine hm = place(22.5, 28.5, -45, 1.15f);
    const std::string head = xf("M-9 -5C-3 -9 6 -8 10 -3L15 5 9 4C7 9 0 11-6 9C-11 7-12 0-9 -5Z" + std::string("M-5 -6C-8 -12-6 -18-1 -21C-2 -15-1 -10 1 -7Z") +
                                    "M2 -7C2 -13 6 -18 12 -19C9 -14 8 -9 6 -5Z" + "M-10 2C-15 4-17 9-15 14C-13 10-10 8-7 7Z",
                                hm);
    const std::string feet = xf("M-3 0H3L4 5 7 6 3 7 2 10 0 7-2 10-3 7-7 6-4 5Z", place(polar(cx, cy, R - 6, -30).x, polar(cx, cy, R - 6, -30).y, 240)) +
                             xf("M-3 0H3L4 5 7 6 3 7 2 10 0 7-2 10-3 7-7 6-4 5Z", place(polar(cx, cy, R - 6, 80).x, polar(cx, cy, R - 6, 80).y, 350));
    c.add("race-xianmaoren-yinyang", "Инь-ян с драконом",
          {G::F(body + spines + feet + xf("M0 0C-4 -4-10 -4-12 0C-8 0-6 3-8 6C-4 5 0 4 0 0Z", place(polar(cx, cy, R, 182).x, polar(cx, cy, R, 182).y, 180, 1.2f))),
           G::XS(scales, 1.4), G::XS(head, 3), G::F(head), G::X(xf(circ(0, -2, 1.7), hm)), G::S(xf("M11 2C17 5 21 11 19 17", hm), 1.6),
           G::F(circ(cx, cy, 24)), G::X("M50 26A24 24 0 0 1 50 74A12 12 0 0 1 50 50A12 12 0 0 0 50 26Z"), G::F(circ(50, 62, 4)), G::X(circ(50, 38, 4))});
  }
}

// ---------------------------------------------------------------- тёмные эльфы
void darkElves(Catalog& c) {
  c.group("Темные Эльфы", EmblemSection::Races);
  // Ручной арбалет: вид сверху — дуга с суставами, как паучьи лапы, натянутая тетива, болт и рукоять.
  {
    const std::string body = sym(taper({47, 31}, {36, 25}, {21, 26}, {9, 37}, 10.5, 4) + taper({9.5, 36}, {6.5, 40}, {6.5, 44}, {10, 47}, 4, 2)) +
                             rrect(38, 24, 24, 12, 4) + rrect(43.5, 22, 13, 58, 4) + "M41.5 77H58.5L64 97H36Z";
    const std::string bolt = rrect(48.4, 10, 3.2, 46, 1.4) + "M50 1 57.5 15 50 11.5 42.5 15Z" + "M48.4 46 42 53V58L48.4 52ZM51.6 46 58 53V58L51.6 52Z";
    c.add("race-dark-elves-crossbow", "Ручной арбалет",
          {G::F(body), G::XS(sym("M35.5 23.5 37.5 31M24 24 25 32"), 1.6), G::S("M10 44L50 64 90 44", 1.8), G::XS(circ(50, 64, 5), 2.2),
           G::F(circ(50, 64, 5)), G::XS(bolt, 2.4), G::F(bolt), G::XS("M42.5 84H57.5M41.5 90H58.5", 1.6),
           G::X("M47 69.5H53L50 73.5 53 77.5H47L50 73.5Z")});
  }
  // Шлем с паучьими глазами: острый шлем стража Подземья, восемь глазниц, гребень из паучьих лап, дыхательные прорези.
  {
    const std::string helm = sym("M50 6C40 14 31 25 29 40L28 70C28 80 35 88 43 92L50 96H50.5V6Z");
    c.add("race-dark-elves-helm", "Шлем с паучьими глазами",
          {G::S(sym("M31 31 20 19 11 23M29 43 16 36 7 41M28.5 55 15 53 8 61M29 67 18 71 14 80"), 3.2),
           G::F(sym(circ(11, 23, 2.2) + circ(7, 41, 2.2) + circ(8, 61, 2.2) + circ(14, 80, 2.2))), G::XS(helm, 3), G::F(helm),
           G::X(sym(circ(44, 51, 4.4) + circ(38.5, 44.5, 2.9) + circ(35.5, 37.5, 2.1) + circ(46.5, 41.5, 1.9)) +
                "M47.5 19H52.5L50 22.5 52.5 26H47.5L50 22.5Z"),
           G::XS("M50 9V16M31.5 32C40 28.5 60 28.5 68.5 32", 1.8), G::XS("M42 70V82M46 72V86M50 72V87M54 72V86M58 70V82", 1.6),
           G::XS(sym("M30.5 62C32 72 36 80 42 86"), 1.6)});
  }
  // Грибы Подземья: три гриба-светоча с ребристыми шляпками, парящие споры.
  {
    const std::string side = "M7 64C7 53 15 46 24 46C33 46 41 53 41 64C35 62 29 61 24 61C19 61 13 62 7 64Z" +
                             std::string("M21 62C21.5 74 21 84 19.5 92H28.5C27 84 26.5 74 27 62Z") +
                             "M61 58C61 45 69 37 79 37C89 37 96 45 96 58C90 56 85 55 79 55C73 55 67 56 61 58Z" + "M76 56C76.5 70 76 82 75 92H83C82 82 81.5 70 82 56Z";
    const std::string mid = "M28 38C28 21 38 10 50 10C62 10 72 21 72 38C65 35 57 34 50 34C43 34 35 35 28 38Z" +
                            std::string("M45.5 35C46 52 45 72 43 92H57C55 72 54 52 54.5 35Z");
    c.add("race-dark-elves-mushrooms", "Грибы Подземья",
          {G::F(side), G::XS("M24 48V59M17 51C15 54 14 57 14 61M31 51C33 54 34 57 34 61M79 39V53M72 42C70 45 69 49 69 54M86 42C88 45 89 49 89 54", 1.5),
           G::XS(mid, 3), G::F(mid), G::XS("M50 13V32M41 16C38 21 37 27 37 33M59 16C62 21 63 27 63 33", 1.6),
           G::XS("M47 44C48 56 47.5 70 46 84", 1.4),
           G::F(circ(15, 34, 2.2) + circ(27, 24, 1.6) + circ(82, 22, 2.2) + circ(91, 29, 1.5) + circ(71, 6, 1.7) + circ(9, 44, 1.4) + circ(36, 6, 1.4) +
                rrect(3, 91, 94, 5, 2.5))});
  }
  // Маска-вуаль: узорная полумаска с заострёнными краями, кружевная вуаль с бусинами.
  {
    const std::string mask = sym("M50 26C44 24 36 22 26 23C17 24 10 20 4 13C5 25 10 35 18 41C25 46 34 48 41 46C45 45 48 43 50 41.5Z");
    const std::string veil = "M17 45C29 51 40 53 50 53C60 53 71 51 83 45L86 80C80 85 76 81 72 85C68 89 64 85 59 89C55 93 45 93 41 89C36 85 32 89 28 85"
                             "C24 81 20 85 14 80Z";
    c.add("race-dark-elves-veil", "Маска-вуаль",
          {G::F(veil), G::XS("M30 54C29 66 30 76 32 86M42 55C41 68 42 80 44 90M58 55C59 68 58 80 56 90M70 54C71 66 70 76 68 86", 1.6),
           G::F(circ(14, 84, 2.4) + circ(28, 89, 2.4) + circ(41, 93, 2.4) + circ(59, 93, 2.4) + circ(72, 89, 2.4) + circ(86, 84, 2.4)),
           G::XS(mask, 3.4), G::F(mask), G::X(sym(xf(ell(0, 0, 7.5, 4.2), place(35, 35, 10)))),
           G::XS(sym("M8 19C14 25 20 27 26 27M14 34C20 39 27 41 33 41.5M44 30C46 33 48 35 50 36"), 1.6),
           G::X(star(50, 30, 4.5, 1.6, 4, -90))});
  }
}

// ---------------------------------------------------------------- тролли
void trolls(Catalog& c) {
  c.group("Тролли", EmblemSection::Races);
  // Голова тролля: гребень-ирокез, длинные уши, крючковатый нос, огромные клыки вверх, серьга.
  {
    const std::string face = sym("M50 27C42 27 36 31 34.5 37L33 45C32 53 33 60 35 66C38 76 43 84 50 89H50.5V27Z");
    const std::string tusk = taper({41, 78}, {32, 80}, {25, 72}, {24, 58}, 9, 2);
    c.add("race-trolls-head", "Голова тролля",
          {G::F("M41 34 35 12 44 21 46 2 50 15 54 2 56 21 65 12 59 34Z"), G::XS(face, 3),
           G::F(face + sym("M35 41C28 36 17 27 4 22C9 34 19 45 34 52Z")), G::XS(sym("M31 43C25 39 18 34 11 30"), 1.8),
           G::X(sym("M37.5 46 47 49.5 46 52.5 38.5 49.5Z")), G::XS(sym("M46.5 47C45 53 44 59 45.5 64M36 43 47.5 47"), 1.8),
           G::XS("M44 65C46.5 68 53.5 68 56 65", 1.8), G::X(sym(ell(47.2, 65, 1.4, 1.1))),
           G::X("M41 75C45.5 77.5 54.5 77.5 59 75C57 81 53.5 83 50 83C46.5 83 43 81 41 75Z"), G::XS(sym(tusk), 3), G::F(sym(tusk)),
           G::S(circ(12, 36, 3.6), 2)});
  }
  // Вуду-кукла: тряпичная кукла со швами, глазом-пуговицей и зашитым ртом, в неё воткнуты булавки.
  {
    const std::string doll = circ(50, 30, 17) + "M36 44C40 42 60 42 64 44L68 72C66 76 60 78 56 77L58 92C58 95 55 96 52 95L50 86 48 95C45 96 42 95 42 92L44 77"
                                                "C40 78 34 76 32 72Z" +
                             taper({38, 50}, {28, 52}, {20, 50}, {12, 44}, 9, 7) + taper({62, 50}, {72, 52}, {80, 50}, {88, 44}, 9, 7) +
                             circ(12, 44, 4.5) + circ(88, 44, 4.5);
    c.add("race-trolls-doll", "Вуду-кукла",
          {G::S("M24 12 40 26M78 66 62 58M70 90 56 78", 2), G::F(circ(23, 11, 4) + circ(79, 66.5, 4) + circ(71, 91, 4)),
           G::F("M38 18 34 8 41 14 44 4 47 14ZM58 16 63 6 62 15 70 10 64 20Z"), G::XS(doll, 3), G::F(doll),
           G::XS(circ(42, 28, 5), 2), G::F(circ(42, 28, 3.2)), G::X(circ(41, 27, 0.9) + circ(43, 29, 0.9)),
           G::XS("M55 24 62 31M62 24 55 31", 2), G::XS("M40 38H60M43 35.5 44 40.5M48 35.5 49 40.5M52 35.5 53 40.5M57 35.5 56 40.5", 1.6),
           G::XS("M50 46V82M46 52H54M46 60H54M46 68H54M46 76H54", 1.6), G::XS("M24 44 26 54M33 46 34 56M76 44 74 54M67 46 66 56", 1.4)});
  }
  // Каменный тролль: сутулый великан — голова с носом-картошкой и клыками, длинные руки, глыбы мышц, трещины.
  {
    const std::string body = "M19 44C21 32 33 25 50 25C67 25 79 32 81 44L83 62C81 67 77 69 73 67L70 75H30L27 67C23 69 19 67 17 62Z" +
                             std::string("M34 73 27 94H43L47 79ZM66 73 73 94H57L53 79Z");
    const std::string arms = taper({23, 40}, {18, 54}, {14, 66}, {13, 78}, 16, 12) + taper({77, 40}, {82, 54}, {86, 66}, {87, 78}, 16, 12);
    const std::string fists = ell(13, 85, 10, 9) + ell(87, 85, 10, 9);
    const std::string head = "M50 14C57 14 62 18 62 25V32C62 37 57 41 50 41C43 41 38 37 38 32V25C38 18 43 14 50 14Z";
    c.add("race-trolls-stone", "Каменный тролль",
          {G::F(body), G::XS(arms + fists, 3), G::F(arms + fists), G::XS(head, 3), G::F(head),
           G::X(sym("M41.5 24 47 26 46.5 28 42 26.5Z") + "M42 34C46 36.5 54 36.5 58 34C56 39 53 40 50 40C47 40 44 39 42 34Z"),
           G::XS("M47.5 26.5C46 31 47 33 50 33.5C53 33 54 31 52.5 26.5", 1.6), G::F(sym("M43 35 44.5 29.5 46.5 36Z")),
           G::XS("M40 20C44 18 56 18 60 20", 1.6),
           G::XS("M33 34 37 42 34 50 39 58M66 34 62 41 66 49 61 57M50 48 46 56 51 62 48 70M21 50 18 58M80 48 83 58M9 84C12 82 15 83 17 86"
                 "M91 84C88 82 85 83 83 86M31 80 35 86M69 80 65 86",
                 1.8)});
  }
  // Вуду-маска: вытянутая резная маска с перьями, глазницами, длинным носом, оскалом и клыками.
  {
    const std::string mask = "M50 14C64 14 72 24 72 40C72 58 68 72 62 84C58 92 54 96 50 96C46 96 42 92 38 84C32 72 28 58 28 40C28 24 36 14 50 14Z";
    std::vector<std::string> feathers;
    for (int i = 0; i < 5; i++) {
      const double a = -90 + (i - 2) * 24;
      const Pt b = polar(50, 30, 12, a);
      feathers.push_back(feather(b, polar(50, 30, i == 2 ? 30 : 27, a), 9));
    }
    std::vector<L> v;
    layeredFeathers(v, feathers, 2, false);
    for (const L& l : std::vector<L>{G::XS(mask, 3), G::F(mask), G::XS("M30 30 35 26 40 30 45 26 50 30 55 26 60 30 65 26 70 30", 1.8),
                                     G::X(sym("M33 42C36 37 42 37 46 42C42 46 37 46 33 42Z")), G::XS("M46 45V66H54V45", 1.8),
                                     G::X("M38 72C44 75 56 75 62 72C60 80 55 84 50 84C45 84 40 80 38 72Z"),
                                     G::F(sym("M41 73 43 79 45 74Z" + std::string("M47 74.5 48.5 78.5 50 75Z"))),
                                     G::F(sym(taper({40, 80}, {34, 80}, {30, 74}, {30, 64}, 5.5, 1.5))),
                                     G::XS(sym("M31 52 37 56M30 58 36 62"), 1.8), G::XS("M44 90 50 87 56 90", 1.6)})
      v.push_back(l);
    c.add("race-trolls-mask", "Вуду-маска", v);
  }
}

// ---------------------------------------------------------------- троглодиты
void troglodytes(Catalog& c) {
  c.group("Троглодиты", EmblemSection::Races);
  // Троглодит с дубиной: сгорбленный безглазый ящер с гребнем, хвостом и занесённой дубиной.
  {
    const std::string club = taper({44, 30}, {50, 22}, {58, 14}, {66, 6}, 5, 11) + circ(67, 6, 5.5);
    const std::string fig = "M40 40C46 32 58 30 66 36C74 42 76 54 72 64C68 72 60 76 52 76H44C38 70 36 56 40 40Z" +
                            std::string("M42 42C36 36 28 34 19 36C13 37 9 40 10 44C13 46 20 46 26 48C32 50 38 50 42 48Z") +
                            "M48 72 40 83 45 94H54L49 85 57 74Z" + "M62 72 67 84 62 94H71L75 82 70 69Z" + taper({70, 62}, {82, 72}, {88, 84}, {96, 90}, 10, 2) +
                            taper({52, 40}, {48, 36}, {46, 32}, {44, 27}, 8, 7) + taper({46, 52}, {42, 58}, {38, 64}, {34, 70}, 7, 6);
    std::string crest;
    const Pt cp[] = {{22, 35}, {29, 33.5}, {36, 34}, {43, 34}, {50, 32}, {57, 32}, {64, 35}, {70, 40}, {74, 47}};
    const double ca[] = {-100, -95, -90, -100, -95, -85, -70, -55, -40};
    for (int i = 0; i < 9; i++) crest += spike(cp[i], ca[i], 6, 2.6);
    c.add("race-troglodytes-brute", "Троглодит с дубиной",
          moved({G::F(club), G::XS(fig + crest, 3), G::F(fig + crest), G::XS("M11 44C16 45 22 45 28 46M56 46C60 54 60 62 56 70", 1.6),
                 G::S("M34 70 29 75M34 70 33 77M34 70 38 76", 2), G::XS("M64 10 61 4M69 9 71 3", 1.4)},
                Affine::translate(-3, 2)));
  }
  // Пещера: каменный холм с тёмным зевом, сталактиты и сталагмиты как зубы, трещины в камне.
  c.add("race-troglodytes-cave", "Пещера",
        moved({G::F("M3 94C3 80 6 66 14 56C18 44 26 34 36 30C40 22 50 18 58 22C68 22 78 30 82 40C90 48 96 62 97 76V94Z"),
         G::X("M24 94C24 78 28 62 38 54C44 49 56 49 62 54C72 62 76 78 76 94Z"),
         G::F("M38 56 41 68 44 52ZM47 51 50 66 53 51ZM56 52 59 64 62 56ZM29 68 32 76 33 64ZM68 64 69 74 72 70Z" +
              std::string("M30 94 33 82 36 94ZM42 94 45 78 48 94ZM53 94 56 84 59 94ZM64 94 66 86 69 94Z")),
         G::XS("M36 30C38 36 36 42 40 46M58 22C56 28 60 32 58 38M82 40C78 46 80 52 76 56M14 56C18 60 18 66 22 70", 1.8),
         G::XS("M84 64 90 66M86 78 93 80M8 76 14 74M10 86 18 86", 1.6), G::F(rrect(2, 93, 96, 4, 2))},
              Affine::translate(0, -7)));
  // Слепое око: глаз ящера с мутным глазным яблоком без зрачка, зашитый шрам, чешуя вокруг.
  {
    std::string scales;
    for (int row = 0; row < 10; row++) {
      const double y = 8 + row * 9;
      for (int i = -1; i < 10; i++) {
        const double x = 6 + i * 10 + (row % 2 ? 5 : 0);
        scales += arc(x, y, 4, 15, 165);
      }
    }
    c.add("race-troglodytes-eye", "Слепое око",
          {G::F(circ(50, 50, 46)), G::XS(scales, 1.6), G::X("M12 52C22 32 38 24 50 24C62 24 78 32 88 52C78 70 62 78 50 78C38 78 22 70 12 52Z"),
           G::F("M20 52C28 38 40 32 50 32C60 32 72 38 80 52C72 64 60 70 50 70C40 70 28 64 20 52Z"),
           G::XS("M24 34 76 72", 4), G::XS("M30 32 26 41M40 39 35 47M50 46 45 54M60 53 55 61M70 60 65 68", 1.6), G::S("M24 34 76 72", 1.6),
           G::XS(circ(50, 50, 44), 2)});
  }
  // Каменные копья: скрещённые копья с кремнёвыми наконечниками, обмоткой и подвесками из клыков.
  {
    const std::string spear = sym("M50 1 45 7.5 46.5 9.5 41.5 16 43 18 38.5 25 40 27 38 33 45.5 40H50.5V1Z") + rrect(46, 38, 8, 59, 3.5);
    std::vector<L> v = crossed(spear, "", "M50 6V34", 1.6f, 32, 50, 52, 1.0f);
    v.push_back(G::XS(xf("M45.5 43 54.5 46M45.5 48 54.5 51", rotAt(32, 50, 52)), 1.6));
    v.push_back(G::XS(xf("M45.5 43 54.5 46M45.5 48 54.5 51", rotAt(-32, 50, 52)), 1.6));
    v.push_back(G::S("M35 53C40 63 60 63 65 53", 2));
    v.push_back(G::F(spike({40, 60}, 100, 11, 3.2) + spike({50, 62.5}, 90, 12, 3.4) + spike({60, 60}, 80, 11, 3.2)));
    c.add("race-troglodytes-spears", "Каменные копья", v);
  }
}

// ---------------------------------------------------------------- циклопы
void cyclopes(Catalog& c) {
  c.group("Циклопы", EmblemSection::Races);
  // Голова циклопа: единственный огромный глаз под тяжёлой бровью, кудри и курчавая борода.
  {
    const std::string head = sym(circ(28, 33, 9) + circ(36, 24, 9.5) + circ(46.5, 20, 10) + circ(31, 79, 8) + circ(39.5, 87, 8.5) + circ(50, 90, 8.5) +
                                 "M50 22C34 22 24 34 24 50C24 58 25 64 27 70C30 80 38 88 50 90H50.5V22Z" + ell(22.5, 53, 5, 8));
    std::string curls;
    for (const auto& p : {std::pair{28.0, 33.0}, {36.0, 24.0}, {46.5, 20.0}, {31.0, 79.0}, {39.5, 87.0}})
      curls += sym(arc(p.first, p.second, 4.2, 150, 380));
    c.add("race-cyclopes-head", "Голова циклопа",
          {G::F(head), G::XS(curls + arc(50, 90, 4.2, 150, 380), 1.6), G::X("M32 47C38 38 62 38 68 47C62 56 38 56 32 47Z"), G::F(circ(50, 47, 7.5)),
           G::X(circ(50, 47, 3.2)), G::F(circ(52.5, 44.5, 1.3)), G::XS("M28 37C38 33 45 36 50 40C55 36 62 33 72 37", 2.6),
           G::XS(sym("M46 57C44.5 61 45.5 64 48 65"), 1.8), G::X(sym(ell(47, 63.5, 1.5, 1.1))), G::XS(sym("M38 70C42 67 47 68 50 70"), 2),
           G::X("M41 73C45 76 55 76 59 73C57 78 54 80 50 80C46 80 43 78 41 73Z"), G::XS(sym("M21 50C23 52 23 55 21 57"), 1.4)});
  }
  // Циклоп-кузнец: могучий торс, одноглазая голова, молот занесён над плечом, кожаный фартук.
  {
    const std::string hammer = xf(rrect(-3, -28, 6, 46, 3) + rrect(-12, -37, 24, 13, 3), place(78, 39, -38));
    const std::string head = "M50 24C60 24 66 31 66 41C66 51 60 59 50 60C40 59 34 51 34 41C34 31 40 24 50 24Z";
    const std::string arm = taper({72, 70}, {80, 58}, {84, 46}, {82, 35}, 15, 12) + circ(81, 32, 8.5);
    c.add("race-cyclopes-smith", "Циклоп-кузнец",
          {G::F(hammer), G::XS(xf("M-12 -29H12", place(78, 39, -38)), 1.8), G::F("M6 97C6 80 15 68 30 63H70C85 68 94 80 94 97Z"),
           G::XS(head, 3), G::F(head), G::XS(arm, 3), G::F(arm), G::X("M39 39C43 33 57 33 61 39C57 45 43 45 39 39Z"), G::F(circ(50, 39, 4.6)),
           G::X(circ(50, 39, 1.8)), G::XS("M37 32C43 29 47 31 50 34C53 31 57 29 63 32M46 47C45 50 47 52 50 52M43 54C47 56 53 56 57 54", 1.8),
           G::XS("M36 64 44 97M64 64 56 97", 2.2), G::XS("M18 80C22 76 26 76 30 78M84 64C80 68 79 72 80 76", 1.6),
           G::X(circ(40, 80, 1.6) + circ(60, 80, 1.6))});
  }
  // Наковальня с молнией: перун бьёт в наковальню, разлетаются искры.
  {
    std::string sparks;
    for (int i = 0; i < 7; i++) {
      const double a = -175 + i * 28;
      if (i == 3) continue;
      sparks += spike(polar(46, 56, 9, a), a, 9, 1.8);
    }
    c.add("race-cyclopes-anvil", "Наковальня с молнией",
          {G::F(sparks + "M6 58C16 60 24 62 30 62H93V72H72L66 76V84L77 94H23L34 84V76L28 74C18 72 10 66 6 58Z"),
           G::XS("M30 66H92M34 80H66", 1.8), G::XS("M60 2 39 32H51L41 59 69 25H56L67 2Z", 3.4), G::F("M60 2 39 32H51L41 59 69 25H56L67 2Z"),
           G::F(star(84, 14, 6, 1.6, 4, -90) + star(20, 30, 4.5, 1.3, 4, -90))});
  }
  // Летящий валун: брошенная циклопом глыба с трещинами и выбоинами, за ней полосы полёта и осколки.
  {
    std::string rock = "M";
    const double rr[] = {31, 28, 33, 29, 31, 27, 32, 28, 30, 29, 32, 27};
    for (int i = 0; i < 12; i++) rock += (i ? "L" : "") + pt(60, 41, rr[i], -90 + 30 * i);
    rock += "Z";
    c.add("race-cyclopes-boulder", "Летящий валун",
          {G::F(taper({40, 62}, {30, 72}, {18, 82}, {4, 92}, 11, 1) + taper({36, 50}, {26, 56}, {16, 60}, {4, 62}, 8, 1) +
                taper({50, 72}, {44, 82}, {36, 90}, {26, 97}, 8, 1)),
           G::XS(rock, 4), G::F(rock), G::XS("M52 13 56 22 51 30 57 38M74 22 70 30 76 36M42 46 50 50 48 58M66 56 72 62 70 70M80 46 86 48", 2),
           G::X(circ(64, 30, 2.6) + circ(46, 34, 2) + circ(74, 50, 2.2) + circ(58, 60, 1.8)),
           G::F(spike({22, 40}, 200, 6, 2.6) + spike({30, 30}, 230, 5, 2.2) + spike({20, 52}, 170, 5, 2.2))});
  }
}

// ---------------------------------------------------------------- ши-цзы
// Сидящий лев-страж в профиль влево: лапа на шаре, кудрявая грива и хвост, постамент.
std::vector<L> guardianLion(bool plinth) {
  const std::string mane = circ(50, 12, 7) + circ(57, 22, 7.5) + circ(59, 34, 7.5) + circ(57, 46, 7) + circ(51, 55, 6.5);
  const std::string tail = taper({78, 66}, {88, 60}, {90, 50}, {85, 42}, 7, 5) + circ(85, 37, 6) + circ(92, 44, 5) + circ(78, 36, 5);
  const std::string body = ell(64, 72, 19, 15.5) + "M33 42C44 38 56 43 62 54C66 62 66 70 62 79L40 81C35 70 33 57 33 42Z" + rrect(39, 62, 10, 27, 4.5) +
                           taper({37, 60}, {32, 63}, {28, 65}, {24, 67}, 11, 9.5) + ell(23, 67, 7, 5.5) + ell(70, 87, 11, 4.5);
  const std::string head = "M22 13C30 7 44 7 50 15C54 21 54 31 50 37C46 43 38 45 30 45C22 45 16 41 14 35L8 33C6 29 8 25 12 25C14 19 17 16 22 13Z";
  std::string curls;
  for (const auto& p : {std::pair{50.0, 12.0}, {57.0, 22.0}, {59.0, 34.0}, {57.0, 46.0}, {85.0, 37.0}, {92.0, 44.0}})
    curls += arc(p.first, p.second, 3.4, 150, 400);
  std::vector<L> v;
  if (plinth) v = {G::F(rrect(10, 89, 80, 8, 2)), G::XS("M10 92.5H90", 1.6)};
  for (const L& l : std::vector<L>{G::F(tail + mane), G::XS(curls, 1.6), G::XS(body, 2.6), G::F(body), G::XS(circ(27, 79, 10), 2.6), G::F(circ(27, 79, 10)),
                                   G::XS(circ(27, 79, 6) + "M17 79H37", 1.6), G::XS(head, 3), G::F(head), G::X(circ(31, 23, 4.4)), G::F(circ(31.5, 23, 2.4)),
                                   G::XS("M24 17C28 14 33 14 37 17M45 18C47 24 47 30 44 35", 1.8), G::X(ell(11, 28.5, 2, 1.6)),
                                   G::X("M10 36C16 40 26 40 33 36C29 42 21 45 14 43Z"), G::F("M14 37.5 16 41.5 18 38.5ZM26 38 27.5 41.5 29.5 37.5Z"),
                                   G::XS("M44 81 44 89M48 81 48 89M64 86 66 91M70 86 71 91", 1.4)})
    v.push_back(l);
  return v;
}

// Пламенная жемчужина радиуса r с центром (cx, cy): ореол пламени с языками, спираль на шаре.
std::vector<L> flamingPearl(float cx, float cy, float r) {
  const Affine m = Affine::translate(cx, cy) * Affine::scale(r / 19.f) * Affine::translate(-50, -52);
  const std::string fire = xf(sym("M50 2C47 10 42 14 41 22C38 16 33 14 28 12C31 20 28 27 25 33C21 30 17 29 12 29C17 37 15 46 17 54C19 66 30 74 42 76H50.5V2Z"), m);
  const float k = r / 19.f;
  return {G::F(fire), G::XS(xf(sym("M41 26C38 30 36 32 33 33M27 40C24 44 23 48 24 52"), m), 1.6f * std::max(1.f, k * 0.8f)),
          G::XS(circ(cx, cy, r), 3 * k), G::F(circ(cx, cy, r)), G::X(spiral(cx, cy, r * 0.08, r * 0.72, -90, 540, r * 0.16, r * 0.16, 60))};
}

void shishi(Catalog& c) {
  c.group("Ши-цзы", EmblemSection::Races);
  // Лев-страж: сидящий страж с лапой на шаре на постаменте.
  c.add("race-shishi-guardian", "Лев-страж", moved(guardianLion(true), Affine::translate(-1, 0)));
  // Голова ши-цзы: анфас в кольце из завитков гривы — выпученные глаза, курчавые брови, оскал с клыками.
  {
    std::string mane, curls;
    for (int i = 0; i < 12; i++) {
      const Pt p = polar(50, 50, 36.5, -90 + 30 * i);
      mane += circ(p.x, p.y, 9.5);
      curls += arc(p.x, p.y, 4.8, -90 + 30 * i + 60, -90 + 30 * i + 330);
    }
    c.add("race-shishi-head", "Голова ши-цзы",
          {G::F(mane), G::XS(curls, 1.8), G::XS(circ(50, 50, 29), 3), G::F(circ(50, 50, 29)),
           G::XS(sym("M28 36C30 29 39 28 42 33C43.5 36 41 38.5 38.5 36.5"), 2.2), G::X(sym(circ(38.5, 44, 7))), G::F(sym(circ(38.5, 44, 4.6))),
           G::X(sym(circ(39.5, 43, 1.5))), G::XS("M40 52C44 49.5 56 49.5 60 52C61 56.5 56.5 60.5 50 61.5C43.5 60.5 39 56.5 40 52Z", 2),
           G::X(sym("M42.5 56.5C43.5 54.5 46.5 54.5 47 57.5C45.5 58.8 43.5 58.5 42.5 56.5Z")), G::XS("M50 61V65", 1.6),
           G::X("M30 64C40 70 60 70 70 64C68 76 60 82 50 82C40 82 32 76 30 64Z"),
           G::F(sym("M33.5 65.5 36.5 74 39.5 67Z") + "M41 68.5C46 70 54 70 59 68.5V71.5C54 73 46 73 41 71.5Z"), G::XS("M45 69V73M50 69.5V73.5M55 69V73", 1.2)});
  }
  // Пара стражей: два льва друг против друга, между ними пламенная жемчужина.
  {
    const std::vector<L> lion = guardianLion(false);
    std::vector<L> v = moved(lion, Affine::translate(49, 43) * Affine::scale(-0.5f, 0.5f));
    for (const L& l : moved(lion, Affine::translate(51, 43) * Affine::scale(0.5f))) v.push_back(l);
    v.push_back(G::F(rrect(3, 87, 94, 9, 2.5)));
    v.push_back(G::XS("M3 91H97", 1.6));
    for (const L& l : flamingPearl(50, 29, 10)) v.push_back(l);
    c.add("race-shishi-pair", "Пара стражей", v);
  }
  // Парчовый шар: узорный шар стража с переплетёнными поясами и цветком, петля сверху, ленты с кистями снизу.
  {
    // Сетка-плетёнка: диагонали, обрезанные по кругу чуть внутри края шара.
    std::string bands;
    for (int k = -4; k <= 4; k++) {
      const double d = k * 9.5, r = 28.5;
      if (std::fabs(d) >= r) continue;
      const double h = std::sqrt(r * r - d * d);
      for (int s : {-1, 1}) {
        const double ux = 0.7071, uy = 0.7071 * s, nx = -uy, ny = ux, cx = 50 + nx * d, cy = 42 + ny * d;
        bands += "M" + n(cx - ux * h) + " " + n(cy - uy * h) + "L" + n(cx + ux * h) + " " + n(cy + uy * h);
      }
    }
    std::string petals;
    for (int i = 0; i < 6; i++) petals += xf(ell(0, 0, 2.6, 5), place(polar(50, 42, 5, -90 + 60 * i).x, polar(50, 42, 5, -90 + 60 * i).y, float(60 * i)));
    const std::string ribbons = sym(taper({47, 77}, {41, 84}, {40, 90}, {31, 95}, 6.5, 4) + "M33 92 26 93 28 99 34 97Z") + "M46.5 79H53.5L56 96H44Z";
    c.add("race-shishi-ball", "Парчовый шар",
          {G::S(circ(50, 7.5, 3.6), 2.4), G::F(ribbons + circ(50, 76, 5)), G::XS(circ(50, 42, 31), 2.6), G::F(circ(50, 42, 31)), G::XS(bands, 2.2),
           G::X(circ(50, 42, 10)), G::F(petals + circ(50, 42, 2.4)), G::XS("M48 86V96M52 86V96M45.5 83H54.5", 1.4)});
  }
}

// ---------------------------------------------------------------- эльфы бездны
// Щупальце вдоль кривой: сужается к кончику, на внутренней стороне — присоски (вырезы), side — сторона присосок (±1).
void tentacle(std::vector<L>& v, Pt p0, Pt c1, Pt c2, Pt p1, double w0, double w1, int side) {
  const std::string body = taper(p0, c1, c2, p1, w0, w1);
  std::string cups;
  for (double t : {0.18, 0.34, 0.5, 0.66, 0.8}) {
    const CurvePt q = bezier(p0, c1, c2, p1, t);
    const double w = (w0 + (w1 - w0) * t) * 0.5;
    cups += circ(q.x + q.nx * side * w * 0.45, q.y + q.ny * side * w * 0.45, std::max(0.9, w * 0.32));
  }
  v.push_back(G::XS(body, 2.6));
  v.push_back(G::F(body));
  v.push_back(G::X(cups));
}

void abyssElves(Catalog& c) {
  c.group("Эльфы Бездны", EmblemSection::Races);
  // Око бездны: глаз с вертикальным зрачком в воронке закрученных лучей.
  {
    std::string arms;
    for (int i = 0; i < 8; i++) arms += spiral(50, 50, 22, 47, 45 * i, 45 * i + 75, 8, 0.6, 20);
    const std::string eye = "M12 50C22 33 36 27 50 27C64 27 78 33 88 50C78 67 64 73 50 73C36 73 22 67 12 50Z";
    c.add("race-abyss-elves-eye", "Око бездны",
          {G::F(arms), G::XS(eye, 4), G::F(eye), G::X("M20 50C28 38 39 34 50 34C61 34 72 38 80 50C72 62 61 66 50 66C39 66 28 62 20 50Z"),
           G::F(circ(50, 50, 14)), G::X("M50 37C54 43 54 57 50 63C46 57 46 43 50 37Z"), G::F(circ(54.5, 45, 1.6)), G::XS(circ(50, 50, 10), 1.2)});
  }
  // Корона и щупальца: тонкий эльфийский венец, из которого поднимаются щупальца с присосками.
  {
    std::vector<L> v;
    tentacle(v, {38, 70}, {30, 52}, {14, 46}, {12, 26}, 10, 2, 1);
    tentacle(v, {62, 70}, {70, 52}, {86, 46}, {88, 26}, 10, 2, -1);
    tentacle(v, {50, 68}, {44, 50}, {58, 30}, {52, 8}, 11, 2, 1);
    v.push_back(G::S("M12 26C12 20 17 17 21 20M88 26C88 20 83 17 79 20M52 8C52 3 57 2 59 5", 2));
    const std::string crown = rrect(16, 70, 68, 11, 3) + "M16 72 22 56 28 72ZM72 72 78 56 84 72Z" + "M30 72 34 60 38 72ZM62 72 66 60 70 72Z";
    v.push_back(G::XS(crown, 3));
    v.push_back(G::F(crown + "M44 72 50 62 56 72Z"));
    v.push_back(G::X(circ(50, 75.5, 2.6) + circ(37, 75.5, 1.8) + circ(63, 75.5, 1.8) + circ(25, 75.5, 1.6) + circ(75, 75.5, 1.6)));
    v.push_back(G::F(spike({30, 81}, 100, 9, 2.6) + spike({50, 81}, 90, 13, 3) + spike({70, 81}, 80, 9, 2.6)));
    c.add("race-abyss-elves-crown", "Корона и щупальца", v);
  }
  // Тёмная звезда: изогнутые лучи-клинки вокруг пустого ядра в кольце.
  {
    std::string rays;
    for (int i = 0; i < 7; i++) {
      const double a = -90 + 360.0 / 7 * i;
      rays += taper(polar(50, 50, 13, a - 8), polar(50, 50, 26, a + 2), polar(50, 50, 37, a + 16), polar(50, 50, 47, a + 38), 13, 0.5);
    }
    c.add("race-abyss-elves-star", "Тёмная звезда",
          {G::F(rays), G::XS(circ(50, 50, 15), 3), G::S(circ(50, 50, 11), 4.6), G::F(circ(50, 50, 3.4)),
           G::XS(dashedCircle(50, 50, 30, 7, 0.35), 1.4)});
  }
  // Клинок пустоты: эльфийский меч с разломом пустоты по клинку, гарда-щупальца, навершие-око.
  {
    const std::string blade = "M50 2L60 16V64H40V16Z";
    const std::string guard = sym(taper({46, 67}, {34, 67}, {24, 62}, {22, 51}, 9, 3) + circ(23.5, 49.5, 4)) + rrect(36, 63.5, 28, 7, 3.5);
    c.add("race-abyss-elves-blade", "Клинок пустоты",
          {G::F(blade), G::X("M50 11 52.5 21 49 30 53 40 49.5 50 52 60H48L46.5 50 50.5 40 46.5 30 50 21Z"), G::XS(guard, 2.6), G::F(guard),
           G::X(sym(circ(23.5, 49.5, 1.6))), G::F(rrect(45.5, 70, 9, 17, 2.5)), G::XS("M45.5 74 54.5 76M45.5 79 54.5 81", 1.5),
           G::XS(ell(50, 91.5, 10, 6.5), 2.4), G::F(ell(50, 91.5, 10, 6.5)), G::X("M50 87C52.4 89 52.4 94 50 96C47.6 94 47.6 89 50 87Z"),
           G::F(star(30, 24, 5, 1.4, 4, -90) + star(70, 34, 4, 1.2, 4, -90) + star(68, 12, 3, 1, 4, -90) + star(32, 40, 3, 1, 4, -90))});
  }
}

// ---------------------------------------------------------------- энты
// Дубовый лист от основания b к кончику t: лопасти по краю, полуширина w.
std::string oakLeaf(Pt b, Pt t, double w) {
  const double dx = t.x - b.x, dy = t.y - b.y, len = std::hypot(dx, dy), ux = dx / len, uy = dy / len;
  std::vector<Pt> lft, rgt;
  const int steps = 40;
  for (int i = 0; i <= steps; i++) {
    const double s = double(i) / steps, base = std::pow(std::sin(kPi * std::min(1.0, s * 1.05)), 0.7),
                 lobe = 0.62 + 0.38 * std::fabs(std::cos(kPi * 3.5 * s)), hw = w * base * lobe;
    const double x = b.x + dx * s, y = b.y + dy * s;
    lft.push_back({float(x - uy * hw), float(y + ux * hw)});
    rgt.push_back({float(x + uy * hw), float(y - ux * hw)});
  }
  std::string p = "M" + ptS(lft[0]);
  for (size_t i = 1; i < lft.size(); i++) p += "L" + ptS(lft[i]);
  for (size_t i = rgt.size(); i-- > 0;) p += "L" + ptS(rgt[i]);
  return p + "Z";
}

void ents(Catalog& c) {
  c.group("Энты", EmblemSection::Races);
  // Лицо энта: ствол с глубоко посаженными глазами, носом-сучком, моховой бородой, ветви-волосы с листьями.
  {
    const std::string trunk = "M22 97C24 80 24 62 22 46C21 36 26 28 34 24H66C74 28 79 36 78 46C76 62 76 80 78 97Z";
    const std::string branches = sym(taper({38, 27}, {33, 18}, {24, 13}, {14, 12}, 7, 1.6) + taper({44, 25}, {44, 17}, {41, 12}, {38, 8}, 5.5, 1.5) +
                                     taper({30, 31}, {22, 28}, {14, 30}, {8, 36}, 5, 1.4)) +
                                 taper({50, 25}, {51, 19}, {49, 14}, {50, 9}, 5, 1.5);
    const std::string leaves = sym(xf(ell(0, 0, 3.4, 6), place(13, 12, -110)) + xf(ell(0, 0, 3, 5.5), place(37, 8, -20)) +
                                   xf(ell(0, 0, 3, 5.5), place(7, 37, -140)) + xf(ell(0, 0, 2.8, 5), place(24, 15, -60)));
    c.add("race-ents-face", "Лицо энта",
          {G::F(branches + leaves), G::XS(trunk, 3), G::F(trunk), G::XS("M28 40C34 35 42 36 47 41M72 40C66 35 58 36 53 41", 2.6),
           G::X(sym("M32 47C35 43.5 41 43.5 44 47C41 50 35 50 32 47Z")), G::F(sym(circ(38.5, 47, 1.8))),
           G::XS("M47 47C46 54 44 60 46 64C48 66.5 52 66.5 54 64C56 60 54 54 53 47", 2), G::X("M37 71C43 74 57 74 63 71C60 76 55 78 50 78C45 78 40 76 37 71Z"),
           G::XS("M36 80 37 92M42 81 41 95M50 82V96M58 81 59 95M64 80 63 92", 1.8), G::XS(sym("M27 56C26 66 27 76 29 90"), 1.8),
           G::X(xf(ell(0, 0, 2, 3.2), place(68, 60, 10)) + xf(ell(0, 0, 1.6, 2.6), place(30, 34, -10)))});
  }
  // Живая роща: три дерева с кронами-клубами, у среднего на стволе открыты глаза.
  {
    const std::string sides = sym(circ(22, 42, 13) + circ(14, 51, 9.5) + circ(30, 53, 9.5) + circ(20, 32, 8) + rrect(19, 56, 6, 36, 2.5));
    const std::string mid = circ(50, 26, 16) + circ(37, 33, 12) + circ(63, 33, 12) + circ(43, 17, 11) + circ(57, 17, 11) +
                            "M41 92C43.5 80 44 64 43 46H57C56 64 56.5 80 59 92Z";
    std::string clumps;
    for (const auto& p : {std::pair{50.0, 28.0}, {37.0, 35.0}, {63.0, 35.0}, {43.0, 19.0}, {57.0, 19.0}, {22.0, 44.0}, {78.0, 44.0}})
      clumps += arc(p.first, p.second, 5.5, 215, 325);
    c.add("race-ents-grove", "Живая роща",
          {G::F(sides), G::XS(mid, 3), G::F(mid), G::XS(clumps, 1.6), G::X(sym("M43.5 61C45.5 58 48 58 49 61C47.5 63 45 63 43.5 61Z")),
           G::XS(sym("M43 55.5C45 54 47.5 54.5 49 56.5") + "M47 71C49 72.5 51 72.5 53 71M46 78 45 88M54 79 55 89", 1.6),
           G::F(rrect(3, 90, 94, 6, 3)), G::XS(sym("M30 90C34 86 38 86 41 88"), 1.4)});
  }
  // Жёлудь и дубовые листья: крупный жёлудь с чешуйчатой шляпкой на фоне двух лопастных листьев.
  {
    std::string scales;
    for (int row = 0; row < 3; row++) {
      const double y = 36 + row * 5.5, x0 = 34 + row * 1.5 - (row == 0 ? -4 : 0), x1 = 66 - row * 1.5 + (row == 0 ? -4 : 0);
      for (double x = x0; x <= x1; x += 5.6) scales += "M" + n(x - 2.6) + " " + n(y) + "Q" + n(x) + " " + n(y + 3.6) + " " + n(x + 2.6) + " " + n(y);
    }
    const std::string cap = "M27 47C27 35 37 28 50 28C63 28 73 35 73 47C73 51 69 53 65 53H35C31 53 27 51 27 47Z";
    const std::string nut = "M33 53H67C67 70 60 84 50 93C40 84 33 70 33 53Z";
    const std::string leaves = oakLeaf({44, 40}, {8, 6}, 13) + oakLeaf({56, 40}, {92, 6}, 13);
    c.add("race-ents-acorn", "Жёлудь и дубовые листья",
          {G::F(leaves), G::XS("M42 38 12 9M58 38 88 9", 1.6), G::XS(cap + nut, 3.4), G::F(nut), G::XS("M42 60C41 70 44 78 49 84", 2),
           G::F(cap + "M48 29C48 23 50 19 54 16L56.5 18.5C53 21 52 24 52 29Z"), G::XS(scales, 1.5)});
  }
  // Корни-кулак: деревянный кулак с древесными волокнами, из запястья расходятся корни, на костяшках — росток.
  {
    const std::string fist = rrect(24, 26, 14, 26, 6.5) + rrect(37.5, 22, 14, 30, 6.5) + rrect(51, 22, 14, 30, 6.5) + rrect(64.5, 26, 12, 26, 6) +
                             rrect(24, 38, 52.5, 26, 8) + "M33 62H67L63 82H37Z";
    const std::string thumb = "M20 50C20 45 24 43 30 43H52C57 43 59 47 57 51L55 56C54 59 51 60 48 60H28C23 60 20 56 20 50Z";
    const std::string roots = taper({40, 80}, {30, 84}, {18, 88}, {6, 96}, 7, 1.5) + taper({46, 82}, {42, 90}, {36, 94}, {28, 98}, 6, 1.5) +
                              taper({54, 82}, {58, 90}, {64, 94}, {72, 98}, 6, 1.5) + taper({60, 80}, {70, 84}, {82, 88}, {94, 96}, 7, 1.5) +
                              taper({50, 82}, {50, 88}, {50, 93}, {50, 98}, 5, 1.5);
    c.add("race-ents-fist", "Корни-кулак",
          {G::F(fist + roots), G::XS("M37.75 30V46M51.25 28V46M64.75 30V46", 2.2), G::XS("M25 34C27 32 30 32 32 34M66 33C68 31 71 31 73 33", 1.4),
           G::XS(thumb, 3), G::F(thumb), G::XS("M40 66C41 72 41 76 40 80M50 66V80M60 66C59 72 59 76 60 80", 1.6),
           G::S("M45 22C44 16 46 10 50 6", 2.2), G::F(xf(ell(0, 0, 3.4, 7), place(56, 8, 60)) + xf(ell(0, 0, 3, 6), place(42, 11, -50)))});
  }
}

}  // namespace

void raceEmblemsC(Catalog& c) {
  ogres(c);
  oni(c);
  orcs(c);
  fallenElves(c);
  desertElves(c);
  rakshasas(c);
  satyrs(c);
  lightElves(c);
  twilightElves(c);
  xianmaoren(c);
  darkElves(c);
  trolls(c);
  troglodytes(c);
  cyclopes(c);
  shishi(c);
  abyssElves(c);
  ents(c);
}

}  // namespace rg::gfx::emb
