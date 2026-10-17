// Сценарии карты в приложении: стопка наложившихся войск (щелчок приближает карту так, что объекты расходятся,
// наведение — на верхний объект), фигурки войск не перекрываются на экране; «Показать всю карту» (Home) вписывает
// мир в свободную часть между панелями (и при открытом инспекторе); сухопутная провинция на морской части карты
// закрашивается цветом владельца.
#include "tests/test_app_military_util.h"
#include "tests/test_app_tools_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::toolstest;

namespace {

// Отметки не пересекаются, каждый объект — ровно в одной.
void checkNoOverlap(Harness& h) {
  std::vector<map::ArmyMark> marks = h->map().armyMarks();
  std::vector<Id> seen;
  for (const map::ArmyMark& m : marks) seen.insert(seen.end(), m.members.begin(), m.members.end());
  std::sort(seen.begin(), seen.end());
  CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
  CHECK_EQ(seen.size(), size_t(h->world().armies.size()));
  for (size_t i = 0; i < marks.size(); i++)
    for (size_t j = i + 1; j < marks.size(); j++) CHECK(marks[i].bounds.intersect(marks[j].bounds).empty());
}

// Морская провинция с наибольшей площадью.
Id largestSea(const World& w) {
  auto fs = geo::faces(w);
  Id best = 0;
  double ba = 0;
  w.provinces.each([&](const Province& p) {
    if (!p.sea) return;
    const geo::ProvinceShape* s = fs->shape(p.id);
    if (s && s->area > ba) {
      ba = s->area;
      best = p.id;
    }
  });
  return best;
}

Id firstState(const World& w, Id skip = 0) {
  Id best = 0;
  std::string bn;
  w.factions.each([&](const Faction& f) {
    if (!f.isState() || f.id == skip) return;
    if (!best || compareRu(f.name, bn) < 0) {
      best = f.id;
      bn = f.name;
    }
  });
  return best;
}

}  // namespace

TEST(app_map_view_stack_click_separates) {
  Harness h("fix_map_stack", 1600, 900);
  h.demo();
  h.waitMap();
  h.dropToasts();
  checkNoOverlap(h);
  h.key(Key::Home);
  h.settle();
  h.waitMap();
  checkNoOverlap(h);
  // Стопка в свободной части карты.
  std::optional<map::ArmyMark> stack;
  for (const map::ArmyMark& m : h->map().armyMarks()) {
    const gfx::Pt s = screenOf(h, m.pos);
    if (m.cluster() && freeOnScreen(h, s.x, s.y)) stack = m;
  }
  CHECK(stack.has_value());
  if (!stack) return;
  const gfx::Pt s = screenOf(h, stack->pos);
  CHECK(h.shot("fix_map_stack_before"));
  // Наведение — верхний объект стопки.
  h.move(s.x, s.y);
  CHECK(h->ui.hover == (app::Selection{app::SelType::Army, stack->top}));
  const double z0 = h->map().view().zoom;
  h.click(s.x, s.y);
  h.settle();
  h.waitMap();
  h.dropToasts();
  CHECK(h->map().view().zoom > z0);
  CHECK(!h->ui.sel);   // щелчок по стопке не выделяет объект
  // Объекты стопки — отдельные отметки, каждая в свободной части окна.
  for (Id id : stack->members) {
    std::optional<map::ArmyMark> own;
    for (const map::ArmyMark& m : h->map().armyMarks())
      if (std::find(m.members.begin(), m.members.end(), id) != m.members.end()) own = m;
    CHECK(own.has_value());
    if (!own) continue;
    for (Id other : stack->members)
      if (other != id) CHECK(std::find(own->members.begin(), own->members.end(), other) == own->members.end());
    const gfx::Pt p = screenOf(h, own->pos);
    CHECK(h->mapArea().contains(p.x, p.y));
  }
  checkNoOverlap(h);
  CHECK(h.shot("fix_map_stack_after"));
  // Теперь щелчок по объекту выделяет его.
  const gfx::Pt p = screenOf(h, h->world().army(stack->top)->pos);
  h.click(p.x, p.y);
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Army, stack->top}));
}

// ТЗ 1.c.iv: «все эти объекты не должны наслаиваться друг на друга». Минимальный зазор задан в единицах карты
// (2 × 34), а фигурка рисуется не меньше 24 точек экрана: при обзорном масштабе соседние фигурки перекрывались.
// Исправлено (карта): наложившиеся объекты рисуются одной отметкой-стопкой — проверяются нарисованные отметки
// (регрессия): они не пересекаются, каждый объект — ровно в одной отметке.
TEST(app_map_view_figures_dont_overlap) {
  Harness h("audit_tzmil_overlap");
  h.demo();
  h.waitMap();
  auto count = [&](const char* when) {
    const World& w = h->world();
    const map::View& v = h->map().view();
    std::vector<map::ArmyMark> marks = h->map().armyMarks();
    int overlaps = 0, stacks = 0;
    std::vector<Id> seen;
    for (const map::ArmyMark& m : marks) {
      stacks += m.cluster() ? 1 : 0;
      seen.insert(seen.end(), m.members.begin(), m.members.end());
    }
    std::sort(seen.begin(), seen.end());
    CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
    CHECK_EQ(seen.size(), size_t(w.armies.size()));
    for (size_t i = 0; i < marks.size(); i++)
      for (size_t j = i + 1; j < marks.size(); j++) {
        const RectF a = marks[i].bounds, b = marks[j].bounds;
        if (a.intersect(b).empty()) continue;
        overlaps++;
        std::printf("  [audit tzmil] %s: отметки «%s» и «%s» перекрываются (масштаб %.3f)\n", when, w.army(marks[i].top)->name.c_str(),
                    w.army(marks[j].top)->name.c_str(), v.zoom);
        RectF r{std::min(a.x, b.x) - 10, std::min(a.y, b.y) - 10, std::max(a.right(), b.right()) - std::min(a.x, b.x) + 20,
                std::max(a.bottom(), b.bottom()) - std::min(a.y, b.y) + 20};
        cropShot(std::string("audit_tzmil_overlap_") + when + "_" + std::to_string(overlaps), r, 4);
      }
    std::printf("  [audit tzmil] %s: масштаб %.3f, объектов %u, отметок %zu, из них стопок %d\n", when, v.zoom, w.armies.size(), marks.size(), stacks);
    return overlaps;
  };
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tzmil_overlap_default"));
  int n1 = count("default");
  h->focusMap(geo::faces(h->world())->bounds);
  h.settle();
  h.waitMap();
  h.dropToasts();
  h.settle();
  int n2 = count("world");
  CHECK(h.shot("audit_tzmil_overlap_world"));
  CHECK_MSG(n1 == 0, "фигурки войск перекрываются на экране при масштабе по умолчанию");
  CHECK_MSG(n2 == 0, "фигурки войск перекрываются на экране при показе всей карты");
}

TEST(app_map_view_fit_with_inspector) {
  Harness h("fix_map_fit_inspector", 1600, 900);
  h.demo();
  Id prov = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!prov && !p.sea) prov = p.id;
  });
  h->select(app::SelType::Province, prov);
  h.settle();
  CHECK(h->uiRect("inspector") != nullptr);
  h.key(Key::Home);
  h.settle();
  h.waitMap();
  h.dropToasts();
  CHECK(h.shot("fix_map_fit_inspector"));
  const map::View& v = h->map().view();
  const RectF area = h->mapArea();
  const gfx::Pt tl = v.toScreen({0, 0}), br = v.toScreen({8000, 4500});
  std::printf("  map %.1f,%.1f — %.1f,%.1f; area %.1f,%.1f %.1fx%.1f; zoom %.4f\n", tl.x, tl.y, br.x, br.y, area.x, area.y, area.w, area.h, v.zoom);
  CHECK(tl.x >= area.x - 1 && tl.y >= area.y - 1);
  CHECK(br.x <= area.right() + 1 && br.y <= area.bottom() + 1);
  if (const RectF* ins = h->uiRect("inspector")) CHECK(br.x <= ins->x + 1);
  CHECK_NEAR(v.zoom, h->map().minZoom(), 1e-9);
  // Колесо дальше не отдаляет: весь мир уже виден.
  h.wheel(area.cx(), area.cy(), -3);
  h.settle();
  CHECK(h->map().view().zoom >= v.zoom - 1e-9);
  // Инспектор закрыт — Home снова вписывает мир в (большую) свободную часть.
  h->clearSelection();
  h.settle();
  h.key(Key::Home);
  h.settle();
  const RectF area2 = h->mapArea();
  CHECK(area2.w > area.w);
  const gfx::Pt tl2 = h->map().view().toScreen({0, 0}), br2 = h->map().view().toScreen({8000, 4500});
  CHECK(tl2.x >= area2.x - 1 && tl2.y >= area2.y - 1 && br2.x <= area2.right() + 1 && br2.y <= area2.bottom() + 1);
}

// ---------------------------------------------------------------- «Показать всю карту» (Home) прячет края карты под панелями
TEST(app_map_view_fit_empty_world) {
  Harness h("audit_tzprov_fit", 1920, 1080, 1);
  CHECK(h->newWorld("Пустой мир"));
  h.settle();
  h.dropToasts();
  h.key(Key::Home);
  h.settle();
  h.waitMap();
  h.dropToasts();
  h.frames(3);
  CHECK(h.shot("audit_tzprov_fit_1920"));
  const map::View& v = h->map().view();
  gfx::Pt tl = v.toScreen({0, 0}), br = v.toScreen({8000, 4500});
  const RectF* top = h->uiRect("topbar");
  const RectF* rail = h->uiRect("rail");
  const RectF* tb = h->uiRect("toolbar");
  RectF area = h->mapArea();
  std::printf("  map on screen %.1f,%.1f — %.1f,%.1f; mapArea %.1f,%.1f %.1fx%.1f; topbar bottom %.1f; rail right %.1f; toolbar right %.1f\n", tl.x, tl.y,
              br.x, br.y, area.x, area.y, area.w, area.h, top ? top->bottom() : -1.f, rail ? rail->right() : -1.f, tb ? tb->right() : -1.f);
  // Северный остров базовой карты (≈ 5300…5800 × 20…260) после «Показать всю карту» должен быть виден.
  gfx::Pt isl = v.toScreen({5560, 120});
  bool hidden = top && top->contains(isl.x, isl.y);
  std::printf("  north island centre on screen %.1f,%.1f hidden by top bar: %d; min zoom %.4f (whole map in mapArea needs %.4f)\n", isl.x, isl.y, int(hidden),
              h->map().minZoom(), std::min(area.w / 8000.0, area.h / 4500.0));
  // Исправлено (карта): мир вписывается в свободную часть между панелями — регрессия.
  CHECK_MSG(!hidden && tl.y >= area.y - 1 && tl.x >= area.x - 1,
            "«Показать всю карту» вписывает карту в окно целиком, а не в свободную область: верх и левый край мира под верхней панелью и лентами");
  CHECK(br.x <= area.right() + 1 && br.y <= area.bottom() + 1);
  CHECK(h->map().minZoom() <= std::min(area.w / 8000.0, area.h / 4500.0) + 1e-9);
}

// ---------------------------------------------------------------- ТЗ 1.a.vii: «сухопутная» провинция на море не закрашивается
TEST(app_map_view_land_province_on_sea_filled) {
  Harness h("audit_tzprov_sealand", 1440, 900);
  h.demo();
  Id pid = largestSea(h->world());
  CHECK(pid != 0);
  h->select(app::SelType::Province, pid);
  h.settle();
  CHECK(h->uiRect("province.sea") != nullptr);
  CHECK(h.clickUi("province.sea"));   // снять галочку «Морская провинция»
  h.settle();
  CHECK(!h->world().province(pid)->sea);
  Id st = firstState(h->world());
  CHECK(h->act("Владелец", [&](Tx& tx) { rules::setProvinceOwner(tx, pid, st); }));
  CHECK_EQ(h->world().province(pid)->owner, st);
  auto fs = geo::faces(h->world());
  const geo::ProvinceShape* sh = fs->shape(pid);
  CHECK(sh != nullptr);
  h->focusMap(sh->box);
  h.settle();
  h->clearSelection();   // без подсветки выделения — видна только заливка
  h.settle();
  h.waitMap();
  h.dropToasts();
  h.frames(3);
  CHECK(h.shot("audit_tzprov_sea_area_land_owner"));
  // Точки внутри провинции: должны быть подкрашены цветом государства (ТЗ 1.a.vii), а не чистым морем.
  int pure = 0, n = 0;
  for (int k = 0; k < 64; k++) {
    Vec2 m{sh->box.x0 + sh->box.w() * ((k % 8) + 0.5) / 8, sh->box.y0 + sh->box.h() * ((k / 8) + 0.5) / 8};
    if (fs->provinceAt(m) != pid) continue;
    gfx::Pt s = h->map().view().toScreen(m);
    if (!freeOnScreen(h, s.x, s.y, 12)) continue;
    u32 px = h.pixel(s.x, s.y);
    int r = int((px >> 16) & 255), g = int((px >> 8) & 255), b = int(px & 255);
    n++;
    if (r < 14 && std::abs(g - 38) < 14 && b > 240) pure++;
  }
  Color c = h->world().faction(st)->color;
  std::printf("  samples %d, pure sea %d, state color %02x%02x%02x\n", n, pure, c.r, c.g, c.b);
  CHECK(n > 5);
  // Исправлено (карта): морские грани сухопутной провинции закрашиваются поверх моря — регрессия.
  CHECK_MSG(pure * 2 < n, "сухопутная (без галочки «Морская») провинция с владельцем на морской части карты не закрашена цветом государства");
}
