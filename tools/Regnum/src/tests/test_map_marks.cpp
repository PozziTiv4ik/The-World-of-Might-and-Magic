// Отметки войск и флота на карте (исправления аудита ТЗ): наложившиеся объекты не перекрываются — стопка, при
// приближении распадается; снимок стопок при обзорном масштабе.
#include "map/map_internal.h"
#include "tests/test_map_view_util.h"

using namespace rg;
using mvtest::makeArmy;

namespace {

constexpr int W = 1920, H = 1080;
constexpr float DPI = 1.25f;

// Отметки при текущей камере: каждый объект ровно в одной, габариты не пересекаются, щелчок по отметке — её верх.
int checkMarks(const map::MapView& mv, const World& w, const char* when) {
  const std::vector<map::ArmyMark> marks = mv.armyMarks();
  std::vector<Id> seen;
  int stacks = 0;
  for (const map::ArmyMark& m : marks) {
    CHECK(!m.members.empty());
    CHECK_EQ(m.members.front(), m.top);
    seen.insert(seen.end(), m.members.begin(), m.members.end());
    if (m.cluster()) stacks++;
    i64 units = 0;
    for (Id id : m.members) units += map::detail::armyCount(*w.army(id));
    CHECK_EQ(units, m.units);
    const gfx::Pt s = mv.view().toScreen(m.pos);
    if (s.x > 0 && s.y > 0 && s.x < W && s.y < H) {
      CHECK_EQ(mv.armyAt(s.x, s.y), m.top);
      const std::optional<map::ArmyMark> hit = mv.markAt(s.x, s.y);
      CHECK(hit.has_value() && hit->members == m.members);
    }
  }
  std::sort(seen.begin(), seen.end());
  CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
  CHECK_EQ(seen.size(), size_t(w.armies.size()));
  int overlaps = 0;
  for (size_t i = 0; i < marks.size(); i++)
    for (size_t j = i + 1; j < marks.size(); j++)
      if (!marks[i].bounds.intersect(marks[j].bounds).empty()) overlaps++;
  CHECK_EQ(overlaps, 0);
  std::printf("  %s: zoom %.3f, marks %zu, stacks %d\n", when, mv.view().zoom, marks.size(), stacks);
  return stacks;
}

}  // namespace

// ================================================================ раскладка отметок
TEST(map_marks_layout_stack) {
  // Три войска на минимальном допустимом расстоянии (2 × 34 единицы карты).
  std::vector<Army> list = {makeArmy(1, {1000, 1000}, 7, 10), makeArmy(2, {1068, 1000}, 8, 500), makeArmy(3, {1136, 1000}, 9, 20),
                            makeArmy(4, {3000, 1000}, 7, 5)};
  std::vector<const Army*> ptrs;
  for (const Army& a : list) ptrs.push_back(&a);
  // Обзорный масштаб: фигурка 24 точки, войска в 14 точках друг от друга — одна стопка из трёх и одиночная фигурка.
  map::detail::MarkLayout L = map::detail::layoutMarks(ptrs, 0.2, 0);
  CHECK_EQ(L.marks.size(), size_t(2));
  const map::detail::MarkLayout::Mark* stack = nullptr;
  for (const auto& m : L.marks)
    if (m.members.size() > 1) stack = &m;
  CHECK(stack != nullptr);
  if (stack) {
    CHECK_EQ(stack->members.size(), size_t(3));
    CHECK_EQ(stack->members.front(), Id(2));   // сверху — самое многочисленное
    CHECK_EQ(stack->units, i64(530));
    CHECK(stack->pos == list[1].pos);
  }
  // Выделенный объект — всегда сверху своей стопки.
  L = map::detail::layoutMarks(ptrs, 0.2, 3);
  for (const auto& m : L.marks)
    if (m.members.size() > 1) CHECK_EQ(m.members.front(), Id(3));
  // Крупный масштаб: все по отдельности.
  L = map::detail::layoutMarks(ptrs, 3.0, 0);
  CHECK_EQ(L.marks.size(), size_t(4));
  // Габариты стопки шире одиночной фигурки (фигурки за верхней и значок числа объектов).
  const RectF one = map::detail::markFootprint(24, 1, 10), three = map::detail::markFootprint(24, 3, 530);
  CHECK(three.x < one.x && three.y < one.y);
  CHECK(three.w > one.w && three.h > one.h);
}

TEST(map_marks_demo_zooms) {
  const World& w = mvtest::demo();
  map::MapView mv(&mvtest::basemap());
  mv.setWorld(w);
  mv.setViewport(RectF(0, 0, W, H), DPI);
  int atMin = -1, atMax = -1;
  for (double z : {0.0, 0.2, 0.3, 0.45, 0.6, 1.0, 2.0, 3.0}) {
    if (z == 0) mv.fitAll(false);
    else mv.centerOn({4300, 1700}, z, false);
    const int stacks = checkMarks(mv, w, z == 0 ? "fit" : "zoom");
    if (z == 0) atMin = stacks;
    if (z == 3.0) atMax = stacks;
  }
  CHECK(atMin > 0);    // при обзорном масштабе есть стопки (иначе фигурки перекрывались бы)
  CHECK_EQ(atMax, 0);  // при наибольшем — все объекты по отдельности
  // Стопка распадается при масштабе separateZoom.
  mv.fitAll(false);
  std::vector<map::ArmyMark> marks = mv.armyMarks();
  int tried = 0;
  for (const map::ArmyMark& m : marks) {
    if (!m.cluster() || tried >= 4) continue;
    tried++;
    mv.fitAll(false);
    const double z = mv.separateZoom(m);
    CHECK(z > mv.view().zoom);
    mv.centerOn(m.pos, z, false);
    std::vector<map::ArmyMark> after = mv.armyMarks();
    for (Id id : m.members)
      for (const map::ArmyMark& a : after)
        if (std::find(a.members.begin(), a.members.end(), id) != a.members.end())
          for (Id other : m.members)
            if (other != id) CHECK_MSG(std::find(a.members.begin(), a.members.end(), other) == a.members.end(), "объекты стопки не разошлись");
  }
  CHECK(tried > 0);
  // При наибольшем масштабе приближать некуда.
  mv.centerOn({4300, 1700}, mv.maxZoom(), false);
  for (const map::ArmyMark& m : mv.armyMarks()) CHECK(!m.cluster() || mv.separateZoom(m) == 0);
}

// Снимок стопок при обзорном масштабе: видны значок числа объектов (золотой круг) и фигурки за верхней.
TEST(map_marks_render_stacks) {
  const World& w = mvtest::demo();
  map::MapView mv(&mvtest::basemap());
  mv.setWorld(w);
  mv.setViewport(RectF(0, 0, W, H), DPI);
  mv.fitAll(false);
  map::RenderOptions opt;
  gfx::Image img = mvtest::renderFull(mv, opt, W, H, DPI);
  mvtest::savePng(img, "map_view_marks_fit.png");
  int stacks = 0, gold = 0;
  for (const map::ArmyMark& m : mv.armyMarks()) {
    if (!m.cluster()) continue;
    const gfx::RectI r(int(m.bounds.x * DPI) - 6, int(m.bounds.y * DPI) - 6, int(m.bounds.w * DPI) + 12, int(m.bounds.h * DPI) + 12);
    if (stacks < 3) mvtest::saveCrop(img, r, 4, "map_view_marks_stack_" + std::to_string(stacks + 1) + ".png");
    stacks++;
    // Золотой значок числа объектов справа сверху от верхней фигурки.
    const gfx::Pt c = mv.view().toScreen(m.pos);
    const float s = mv.figureSize();
    const int bx = int((c.x + s * 0.46f) * DPI), by = int((c.y - s * 0.56f) * DPI);
    bool found = false;
    for (int y = by - 6; y <= by + 6 && !found; y++)
      for (int x = bx - 6; x <= bx + 6 && !found; x++) {
        if (x < 0 || y < 0 || x >= img.w || y >= img.h) continue;
        const Color p = gfx::unpremul(img.at(x, y));
        found = p.r > 220 && p.g > 170 && p.g < 215 && p.b < 120;
      }
    gold += found ? 1 : 0;
  }
  CHECK(stacks > 0);
  CHECK_EQ(gold, stacks);
  // Выделенный объект стопки — сверху (нарисован с выделением), щелчок по стопке — он.
  for (const map::ArmyMark& m : mv.armyMarks()) {
    if (!m.cluster()) continue;
    opt.selArmy = m.members.back();
    gfx::Image sel = mvtest::renderFull(mv, opt, W, H, DPI);
    bool top = false;
    for (const map::ArmyMark& a : mv.armyMarks())
      if (std::find(a.members.begin(), a.members.end(), opt.selArmy) != a.members.end()) {
        top = a.top == opt.selArmy;
        const gfx::Pt c = mv.view().toScreen(a.pos);
        CHECK_EQ(mv.armyAt(c.x, c.y), opt.selArmy);
        mvtest::saveCrop(sel, gfx::RectI(int(a.bounds.x * DPI) - 6, int(a.bounds.y * DPI) - 6, int(a.bounds.w * DPI) + 12, int(a.bounds.h * DPI) + 12), 4,
                         "map_view_marks_stack_selected.png");
      }
    CHECK(top);
    break;
  }
}

