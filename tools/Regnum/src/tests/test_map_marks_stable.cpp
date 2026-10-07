// Стабильность войск на карте (ТЗ «Исправления», п.3: «Сделать размер войска и положение в покое фиксированным, так
// что бы при отдалении его не "шатало" по карте»):
//  * раскладка стопок монотонна по масштабу: при плавном отдалении стопки только сливаются (ни один объект не уходит
//    из стопки), при приближении только распадаются; отметка, состав которой не изменился, стоит на той же точке;
//  * одинаковый масштаб — одинаковая раскладка (при любом сдвиге камеры, при отдалении и приближении);
//  * фигурка постоянного размера рисуется ровно на точке войска при любом масштабе: смещение её пикселей от точки
//    карты на устройстве не меняется (отметка двигается с картой как одно целое).
#include <map>
#include <set>

#include "map/map_internal.h"
#include "tests/test_map_view_util.h"

using namespace rg;

namespace {

constexpr int W = 1600, H = 900;
constexpr float DPI = 1.25f;

Army makeArmy(Id id, Vec2 pos, Id faction, i64 units, ArmyKind kind = ArmyKind::Army) {
  Army a;
  a.id = id;
  a.kind = kind;
  a.pos = pos;
  a.groups.push_back(ArmyGroup{faction, {ArmyUnit{1, units}}, {}});
  return a;
}

// Состав отметок раскладки: объект → номер отметки.
std::map<Id, size_t> groupsOf(const map::detail::MarkLayout& L) {
  std::map<Id, size_t> g;
  for (size_t k = 0; k < L.marks.size(); k++)
    for (Id id : L.marks[k].members) g[id] = k;
  return g;
}

// fine — раскладка при большем масштабе, coarse — при меньшем: каждая отметка fine целиком лежит в одной отметке
// coarse; отметка с тем же составом стоит на той же точке и с тем же верхним объектом. Возвращает число нарушений.
int coarsening(const map::detail::MarkLayout& fine, const map::detail::MarkLayout& coarse) {
  const std::map<Id, size_t> gc = groupsOf(coarse);
  int bad = 0;
  for (const auto& m : fine.marks) {
    const size_t k = gc.at(m.members.front());
    for (Id id : m.members)
      if (gc.at(id) != k) bad++;
    const auto& c = coarse.marks[k];
    if (c.members.size() == m.members.size() && (!(c.pos == m.pos) || c.members.front() != m.members.front())) bad++;
  }
  return bad;
}

// Отметки не пересекаются в пространстве «карта × масштаб».
int overlaps(const map::detail::MarkLayout& L, double zoom) {
  int n = 0;
  for (size_t i = 0; i < L.marks.size(); i++)
    for (size_t j = i + 1; j < L.marks.size(); j++) {
      const auto& a = L.marks[i];
      const auto& b = L.marks[j];
      const RectF ra(float(a.pos.x * zoom) + a.rel.x, float(a.pos.y * zoom) + a.rel.y, a.rel.w, a.rel.h);
      const RectF rb(float(b.pos.x * zoom) + b.rel.x, float(b.pos.y * zoom) + b.rel.y, b.rel.w, b.rel.h);
      if (!ra.intersect(rb).empty()) n++;
    }
  return n;
}

bool sameLayout(const std::vector<map::ArmyMark>& a, const std::vector<map::ArmyMark>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (a[i].members != b[i].members || !(a[i].pos == b[i].pos) || a[i].units != b[i].units) return false;
  return true;
}

// Плотные скопления (разные численности — верхний объект стопки меняется при слияниях) и демо-войска.
std::vector<Army> crowd() {
  std::vector<Army> list;
  Id id = 1;
  const Vec2 centers[] = {{1500, 1200}, {3100, 2000}, {5200, 1500}, {6400, 3100}};
  Rng rng(0x5ab1e);
  for (Vec2 c : centers)
    for (int k = 0; k < 14; k++) {
      const Vec2 p{c.x + (rng.uniform() - 0.5) * 520, c.y + (rng.uniform() - 0.5) * 360};
      list.push_back(makeArmy(id, p, Id(1 + k % 5), i64(rng.range(1, 60)) * 100, k % 4 == 0 ? ArmyKind::Fleet : ArmyKind::Army));
      id++;
    }
  // Цепочка Z — X — Y (приоритет Y > X > Z): X ближе к Z, чем к Y. Жадное слияние по проходам при отдалении
  // забирало X к Y и оставляло Z одного — Z «выпрыгивал» из стопки.
  list.push_back(makeArmy(id++, {7000, 700}, 3, 100));    // Z
  list.push_back(makeArmy(id++, {7060, 700}, 4, 500));    // X
  list.push_back(makeArmy(id++, {7200, 700}, 5, 9000));   // Y
  return list;
}

std::vector<double> zoomOut(double from, double to, int steps) {
  std::vector<double> z;
  for (int i = 0; i <= steps; i++) z.push_back(from * std::pow(to / from, double(i) / steps));
  return z;
}

}  // namespace

// ================================================================ монотонность раскладки
TEST(map_marks_stable_monotone_zoom_out) {
  std::vector<Army> list = crowd();
  const World& demo = mvtest::demo();
  Id next = 1000;
  demo.armies.each([&](const Army& a) {
    Army b = a;
    b.id = next++;
    list.push_back(b);
  });
  std::vector<const Army*> ptrs;
  for (const Army& a : list) ptrs.push_back(&a);
  for (Id sel : {Id(0), Id(5), Id(58)}) {
    const map::detail::MarkTree T = map::detail::buildMarkTree(ptrs, sel, 34, 0);
    const std::vector<double> zs = zoomOut(3.0, 0.05, 240);
    map::detail::MarkLayout prev = map::detail::cutMarks(T, zs[0]);
    CHECK(prev.marks.size() * 10 >= ptrs.size() * 9);   // крупный масштаб: почти все по отдельности
    CHECK_EQ(overlaps(prev, zs[0]), 0);
    int bad = 0, overlap = 0, merges = 0;
    for (size_t i = 1; i < zs.size(); i++) {
      const map::detail::MarkLayout cur = map::detail::cutMarks(T, zs[i]);
      bad += coarsening(prev, cur);
      overlap += overlaps(cur, zs[i]);
      CHECK(cur.marks.size() <= prev.marks.size());
      merges += int(prev.marks.size() - cur.marks.size());
      // Одинаковый масштаб — одинаковая раскладка (срез того же дерева и новое дерево).
      if (i % 40 == 0) {
        const map::detail::MarkLayout again = map::detail::layoutMarks(ptrs, zs[i], sel, 34);
        CHECK_EQ(again.marks.size(), cur.marks.size());
        for (size_t k = 0; k < std::min(again.marks.size(), cur.marks.size()); k++) {
          CHECK(again.marks[k].members == cur.marks[k].members);
          CHECK(again.marks[k].pos == cur.marks[k].pos);
        }
      }
      prev = cur;
    }
    std::printf("  sel %u: масштабов %zu, слияний %d, нарушений %d, пересечений %d, отметок в конце %zu\n", unsigned(sel), zs.size(), merges, bad,
                overlap, prev.marks.size());
    CHECK_EQ(bad, 0);
    CHECK_EQ(overlap, 0);
    CHECK(merges > 20);
    // Обратный путь (приближение): стопки только распадаются — та же проверка в обратном порядке.
    int back = 0;
    for (size_t i = zs.size() - 1; i > 0; i--) back += coarsening(map::detail::cutMarks(T, zs[i - 1]), map::detail::cutMarks(T, zs[i]));
    CHECK_EQ(back, 0);
  }
  // Цепочка Z — X — Y: Z не покидает стопку с X ни при каком масштабе после их слияния.
  const Id z = Id(57), x = Id(58), y = Id(59);
  CHECK(list[56].id == z && list[57].id == x && list[58].id == y);
  const map::detail::MarkTree T = map::detail::buildMarkTree(ptrs, 0, 34, 0);
  bool together = false;
  for (double zz : zoomOut(3.0, 0.05, 400)) {
    const auto g = groupsOf(map::detail::cutMarks(T, zz));
    if (g.at(z) == g.at(x)) together = true;
    else CHECK_MSG(!together, "Z ушёл из стопки при отдалении");
  }
  CHECK(together);
}

// Масштаб распада стопки — наибольший порог слияний внутри неё; выше него все объекты по отдельности.
TEST(map_marks_stable_split_zoom) {
  std::vector<Army> list = crowd();
  std::vector<const Army*> ptrs;
  for (const Army& a : list) ptrs.push_back(&a);
  const map::detail::MarkTree T = map::detail::buildMarkTree(ptrs, 0, 34, 0);
  int stacks = 0;
  for (const auto& m : map::detail::cutMarks(T, 0.12).marks) {
    if (m.members.size() < 2) continue;
    stacks++;
    std::vector<const Army*> mem;
    for (Id id : m.members)
      for (const Army& a : list)
        if (a.id == id) mem.push_back(&a);
    const double s = map::detail::splitZoom(mem, 0, 34);
    CHECK(s > 0.12);
    const auto g = groupsOf(map::detail::cutMarks(T, s * 1.0001));
    std::set<size_t> distinct;
    for (Id id : m.members) distinct.insert(g.at(id));
    CHECK_EQ(distinct.size(), m.members.size());
  }
  CHECK(stacks > 0);
  // Совпадающие точки — стопка при любом масштабе.
  std::vector<Army> same = {makeArmy(1, {100, 100}, 1, 10), makeArmy(2, {100, 100}, 2, 20)};
  const std::vector<const Army*> sp = {&same[0], &same[1]};
  CHECK(std::isinf(map::detail::splitZoom(sp, 0, 34)));
  CHECK_EQ(map::detail::layoutMarks(sp, 50.0, 0, 34).marks.size(), size_t(1));
}

// ================================================================ MapView: камера, дерево и срез
TEST(map_marks_stable_view_same_zoom) {
  const World& w = mvtest::demo();
  map::MapView mv(&mvtest::basemap());
  mv.setWorld(w);
  mv.setViewport(RectF(0, 0, W, H), DPI);
  mv.fitAll(false);
  const double zmin = mv.minZoom(), zmax = mv.maxZoom();
  const std::vector<double> zs = zoomOut(zmax, zmin, 160);
  std::vector<std::vector<map::ArmyMark>> down;
  std::vector<map::ArmyMark> prev;
  int bad = 0;
  for (double z : zs) {
    mv.centerOn({4000, 2250}, z, false);
    std::vector<map::ArmyMark> cur = mv.armyMarks();
    // Каждая отметка прошлого (большего) масштаба целиком в одной отметке текущего.
    std::map<Id, size_t> g;
    for (size_t k = 0; k < cur.size(); k++)
      for (Id id : cur[k].members) g[id] = k;
    for (const map::ArmyMark& m : prev)
      for (Id id : m.members)
        if (g.at(id) != g.at(m.top)) bad++;
    // Экранная точка отметки — точка карты её верхнего объекта.
    for (const map::ArmyMark& m : cur) {
      CHECK(m.pos == w.army(m.top)->pos);
      const gfx::Pt s = mv.view().toScreen(m.pos);
      CHECK(m.bounds.contains(s.x, s.y));
    }
    down.push_back(cur);
    prev = std::move(cur);
  }
  CHECK_EQ(bad, 0);
  // Тот же масштаб при другом центре камеры и на обратном пути (приближение) — та же раскладка.
  int differ = 0;
  for (size_t i = zs.size(); i-- > 0;) {
    mv.centerOn({2500 + double(i % 7) * 400, 1500 + double(i % 5) * 300}, zs[i], false);
    if (std::fabs(mv.view().zoom - zs[i]) > 1e-12) continue;   // центр у края карты ограничен — масштаб тот же
    if (!sameLayout(mv.armyMarks(), down[i])) differ++;
  }
  CHECK_EQ(differ, 0);
}

// ================================================================ фигурка на точке войска при любом масштабе
TEST(map_marks_stable_anchor_pixels) {
  // Точка ¼ пикселя: не дальше 1/8 пикселя от точной точки карты.
  for (double ds : {0.123, 0.3337, 1.0, 2.71}) {
    for (double x : {0.0, 17.31, 4000.77, 7999.9}) {
      const gfx::Pt c = map::detail::markAnchor(12, -7, ds, {x, x * 0.5});
      CHECK(std::fabs(c.x - (12 + x * ds)) <= 0.125 + 1e-6);
      CHECK(std::fabs(c.y - (-7 + x * 0.5 * ds)) <= 0.125 + 1e-6);
      CHECK(std::fabs(c.x * 4 - std::round(c.x * 4)) < 1e-4);
    }
  }
  // Одно войско цвета, которого нет на карте; плавное отдаление вокруг точки экрана. Смещение пикселей фигурки и
  // значка от точки войска на устройстве одинаково при всех масштабах (с точностью до сглаживания).
  World w = mvtest::demo();
  {
    Tx tx(w);
    for (Id id : tx.w().armies.ids()) tx.eraseArmy(id);
    Id state = 0;
    tx.w().factions.each([&](const Faction& f) {
      if (!state && f.isState()) state = f.id;
    });
    tx.faction(state).color = Color(255, 0, 255);
    Army a = makeArmy(0, {3517.37, 1733.81}, state, 4200);
    tx.add(a);
    w = std::move(tx).finish();
  }
  const Vec2 pos = w.armies.all().front()->pos;
  map::MapView mv(&mvtest::basemap());
  mv.setWorld(w);
  mv.setViewport(RectF(0, 0, W, H), DPI);
  map::RenderOptions opt;
  opt.labels = false;
  opt.mode = schema::MapMode::Terrain;   // без заливки государств цветом фракции
  const float size = mv.figureSize() * DPI;
  std::vector<std::pair<double, double>> offs;
  std::vector<double> zooms;
  mv.centerOn(pos, 1.6, false);
  for (int i = 0; i < 48; i++) {
    // Колесо у точки экрана в стороне от войска: войско движется по экрану, точка карты под курсором — нет.
    mv.zoomAt(W * 0.37f, H * 0.61f, 0.955, false);
    const map::View& v = mv.view();
    gfx::Image img(int(W * DPI), int(H * DPI), 0xff000000u);
    gfx::Canvas c(img);
    mv.render(c, opt);
    const double ds = v.zoom * DPI;
    const double X0 = std::round(double(v.viewport.cx()) * DPI - v.cx * ds), Y0 = std::round(double(v.viewport.cy()) * DPI - v.cy * ds);
    const double ex = X0 + pos.x * ds, ey = Y0 + pos.y * ds;   // точка войска на устройстве
    double sx = 0, sy = 0, sw = 0;
    const int r = int(size * 1.6f);
    for (int y = int(ey) - r; y <= int(ey) + r; y++)
      for (int x = int(ex) - r; x <= int(ex) + r; x++) {
        if (x < 0 || y < 0 || x >= img.w || y >= img.h) continue;
        // Только фигурка: обводка значка численности (справа снизу) тонкая, её сглаживание зависит от доли пикселя.
        if (x >= ex + size * 0.14 && y >= ey + size * 0.18) continue;
        const Color p = gfx::unpremul(img.at(x, y));
        // Насколько пиксель похож на пурпурный цвет фракции.
        const double k = std::max(0.0, 1.0 - (std::abs(p.r - 255) + std::abs(p.g - 0) + std::abs(p.b - 255)) / 160.0);
        if (k <= 0) continue;
        sx += k * (x + 0.5);
        sy += k * (y + 0.5);
        sw += k;
      }
    CHECK(sw > 20);
    if (sw <= 0) continue;
    offs.push_back({sx / sw - ex, sy / sw - ey});
    zooms.push_back(v.zoom);
    if (i == 0 || i == 47) mvtest::saveCrop(img, gfx::RectI(int(ex) - r, int(ey) - r, 2 * r, 2 * r), 4, "map_marks_stable_" + std::to_string(i) + ".png");
  }
  CHECK(offs.size() == 48);
  double mx = 0, my = 0;
  for (auto [x, y] : offs) {
    mx += x;
    my += y;
  }
  mx /= double(offs.size());
  my /= double(offs.size());
  double worst = 0;
  for (auto [x, y] : offs) worst = std::max(worst, std::hypot(x - mx, y - my));
  std::printf("  масштаб %.3f → %.3f, смещение пикселей фигурки от точки войска %.2f, %.2f; наибольшее отклонение %.3f px\n", zooms.front(),
              zooms.back(), mx, my, worst);
  CHECK(zooms.back() < zooms.front() * 0.2);
  CHECK(worst < 0.35);   // доли пикселя: сглаживание краёв на разном фоне
}

// Значки и фигурки стопки — целыми сдвигами от центра: при любой дробной части центра они на тех же местах
// относительно фигурки (раньше значки округлялись до пикселя отдельно от фигурки и «подрагивали»).
TEST(map_marks_stable_badges_rigid) {
  for (float size : {18.f, 34.f * 1.25f, 72.f * 1.5f}) {
    RectF u0, c0;
    gfx::Pt s10, s20;
    bool first = true;
    for (float fx : {0.f, 0.25f, 0.5f, 0.75f})
      for (float fy : {0.f, 0.25f, 0.5f, 0.75f}) {
        const gfx::Pt c(100 + fx, 200 + fy);
        const RectF u = map::detail::unitBadgeRect(c, size, "12,5К", 1.25f), cb = map::detail::countBadgeRect(c, size, "7", 1.25f);
        const RectF du(u.x - c.x, u.y - c.y, u.w, u.h), dc(cb.x - c.x, cb.y - c.y, cb.w, cb.h);
        const gfx::Pt s1 = map::detail::stackOffset(1, size), s2 = map::detail::stackOffset(2, size);
        if (first) {
          u0 = du;
          c0 = dc;
          s10 = s1;
          s20 = s2;
          first = false;
        }
        CHECK(du == u0);
        CHECK(dc == c0);
        CHECK(s1.x == s10.x && s1.y == s10.y && s2.x == s20.x && s2.y == s20.y);
        CHECK(du.x == std::round(du.x) && du.y == std::round(du.y) && dc.x == std::round(dc.x) && dc.y == std::round(dc.y));
        CHECK(s1.x == std::round(s1.x) && s1.y == std::round(s1.y) && s2.x == std::round(s2.x) && s2.y == std::round(s2.y));
      }
  }
}
