// Сценарии торговых маршрутов (ТЗ 1.d.v): построение щелчками с выбором гильдии, +10 % базовой торговой ценности
// каждой провинции на пути (линия на карте — ровно та ломаная, по которой считаются провинции), правка точек
// (перетаскивание, вставка, удаление), инспектор маршрута (название, провинции на пути), удаление через меню и
// инспектор с подтверждением, отмена; щелчок по маршруту поверх провинции.
#include "map/map_internal.h"
#include "tests/test_app_tools_util.h"

using namespace rg;
using namespace rg::toolstest;

namespace {

double tradeValue(const World& w, Id p) {
  auto c = rules::calc(w);
  const rules::ProvinceCalc* pc = c->province(p);
  return pc ? pc->tradeValue : 0;
}

// Цепочка из трёх соседних сухопутных провинций с владельцами и заметной базовой ценностью.
std::vector<Id> chain(const World& w) {
  auto fs = geo::faces(w);
  auto nb = fs->neighbors();
  // Точка подписи далеко от существующих маршрутов (щелчок не попадёт в чужую линию).
  auto clear = [&](Vec2 q) {
    bool far = true;
    w.routes.each([&](const Route& r) {
      for (size_t i = 1; i < r.pts.size(); i++)
        if (geo::distToSeg(q, r.pts[i - 1], r.pts[i]) < 70) far = false;
    });
    return far;
  };
  auto ok = [&](Id p) {
    const Province* pr = w.province(p);
    return pr && !pr->sea && pr->owner && pr->baseTrade > 1 && fs->shape(p) && clear(fs->shape(p)->label);
  };
  for (auto [a, b] : nb) {
    if (!ok(a) || !ok(b)) continue;
    for (auto [c, d] : nb) {
      Id third = c == b ? d : d == b ? c : 0;
      if (!third || third == a || !ok(third)) continue;
      return {a, b, third};
    }
  }
  return {};
}

}  // namespace

TEST(app_tools_route_create_edit_delete) {
  Harness h("tools_route");
  h.demo();
  const World w0 = h->world();
  std::vector<Id> ch = chain(w0);
  CHECK_EQ(ch.size(), size_t(3));
  auto fs = geo::faces(w0);
  std::vector<Vec2> pts;
  Box2 box;
  for (Id p : ch) {
    pts.push_back(fs->shape(p)->label);
    box.add(fs->shape(p)->label);
  }
  std::vector<double> before;
  for (Id p : ch) before.push_back(tradeValue(w0, p));

  // Инструмент доступен и без правки границ; режим карты — «Гильдии».
  CHECK(!h->ui.editBorders);
  h.key(Key::R);
  CHECK(h->ui.tool == app::ToolId::Route);
  CHECK(h->ui.mapMode == schema::MapMode::Guilds);
  focus(h, box.inflated(60));
  for (Vec2 p : pts) {
    gfx::Pt s = screenOf(h, p);
    CHECK(freeOnScreen(h, s.x, s.y, 12));
    CHECK(h->map().routeAt(s.x, s.y) == 0);
  }
  // Гильдия-владелец — на панели инструмента.
  CHECK(h.clickUi("tool.options.guild"));
  h.key(Key::Down);
  h.key(Key::Enter);
  h.settle();
  size_t r0 = h->world().routes.size();
  clickMap(h, pts[0]);
  clickMap(h, pts[1]);
  moveMap(h, pts[2]);
  CHECK(cleanShot(h, "tools_route_draw"));
  clickMap(h, pts[2]);
  h.key(Key::Enter);
  CHECK_EQ(h->world().routes.size(), r0 + 1);
  CHECK(h->ui.sel.type == app::SelType::Route);
  Id rid = h->ui.sel.id;
  const Route* rt = h->world().route(rid);
  CHECK(rt != nullptr);
  CHECK_EQ(rt->pts.size(), size_t(3));
  const std::vector<Vec2> made = rt->pts;
  const Faction* g = h->world().faction(rt->guild);
  CHECK(g != nullptr && g->isGuild());
  // +10 % базовой ценности каждой провинции на пути (ТЗ 1.d.v) и ещё +2,5 % за каждое государство на пути
  // (ТЗ «Общие доработки», п.8).
  std::vector<Id> states;
  for (Id p : geo::faces(h->world())->provincesOnPolyline(made))
    if (const Province* pr = h->world().province(p); pr && !pr->sea)
      if (const Faction* o = h->world().faction(pr->owner); o && o->isState() && std::find(states.begin(), states.end(), o->id) == states.end())
        states.push_back(o->id);
  const double bonus = schema::kRouteBonus + schema::kRouteStateBonus * double(states.size());
  for (size_t i = 0; i < ch.size(); i++) {
    double base = h->world().province(ch[i])->baseTrade;
    CHECK_NEAR(tradeValue(h->world(), ch[i]) - before[i], base * bonus, 1e-6);
  }
  CHECK(h->uiRect("route.stats") != nullptr);
  CHECK(h->uiRect("route.provinces") != nullptr);
  CHECK(cleanShot(h, "tools_route_inspector"));

  // Перетаскивание точки — один шаг отмены.
  Vec2 moved = pts[1] + Vec2{h->map().view().toMapLen(30), h->map().view().toMapLen(24)};
  dragMap(h, pts[1], moved, 6);
  rt = h->world().route(rid);
  CHECK(dist(rt->pts[1], moved) < h->map().view().toMapLen(1.5));
  CHECK_EQ(h->store.undoLabel(), std::string("Изменить маршрут"));
  h.key(Key::Z, ctrl());
  CHECK(h->world().route(rid)->pts[1] == made[1]);

  // Двойной щелчок по линии — новая точка.
  Vec2 mid = (pts[0] + pts[1]) * 0.5;
  gfx::Pt ms = screenOf(h, mid);
  bool onLine = false;
  for (float d = 0; d < 30 && !onLine; d += 1)
    for (float sgn : {1.f, -1.f}) {
      gfx::Pt q{ms.x, ms.y + d * sgn};
      if (h->map().routeAt(q.x, q.y, 3) == rid) {
        ms = q;
        onLine = true;
        break;
      }
    }
  CHECK(onLine);
  h.move(ms.x, ms.y);
  CHECK(cleanShot(h, "tools_route_edit"));
  h.doubleClick(ms.x, ms.y);
  CHECK_EQ(h->world().route(rid)->pts.size(), size_t(4));
  // Выбрать точку щелчком и удалить Delete.
  Vec2 nv = h->world().route(rid)->pts[1];
  clickMap(h, nv);
  h.key(Key::Delete);
  CHECK_EQ(h->world().route(rid)->pts.size(), size_t(3));
  CHECK(!h->hasDialog());

  // Инспектор: переименование.
  CHECK(h.clickUi("route.name"));
  h.retype("Янтарный путь");
  h.key(Key::Enter);
  CHECK_EQ(h->world().route(rid)->name, std::string("Янтарный путь"));

  // Меню правой кнопки на линии — удалить маршрут (с подтверждением).
  gfx::Pt vs = screenOf(h, h->world().route(rid)->pts[2]);
  h.click(vs.x, vs.y, platform::MouseRight);
  h.settle();
  CHECK(cleanShot(h, "tools_route_menu"));
  CHECK(h.clickUi("route.menu.remove"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->world().route(rid) == nullptr);
  CHECK(!h->ui.sel);
  for (size_t i = 0; i < ch.size(); i++) CHECK_NEAR(tradeValue(h->world(), ch[i]), before[i], 1e-6);
  h.key(Key::Z, ctrl());
  CHECK(h->world().route(rid) != nullptr);

  // Удаление из инспектора.
  h->select(app::SelType::Route, rid);
  h.settle();
  CHECK(h.clickUi("route.delete"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->world().route(rid) == nullptr);
  h.key(Key::Z, ctrl());
  CHECK(h->world().route(rid) != nullptr);
}

TEST(app_tools_route_light_and_cancel) {
  Harness h("tools_route_light", 1440, 900, 1, kAltScheme);
  h.demo();
  // Маршрут демонстрационного мира: выбор щелчком в режиме маршрутов, инспектор в другом цветокоре.
  Id rid = 0;
  h->world().routes.each([&](const Route& r) {
    if (!rid && r.pts.size() >= 3) rid = r.id;
  });
  CHECK(rid != 0);
  h.key(Key::R);
  h->select(app::SelType::Route, rid);
  h.frames(2);
  h->focusSelection();
  h.settle();
  CHECK(cleanShot(h, "tools_route_light"));
  // Начатый маршрут отменяется Esc; Enter с одной точкой ничего не создаёт.
  h->clearSelection();
  h.frames(2);
  size_t n = h->world().routes.size();
  RectF area = h->mapArea();
  gfx::Pt p{area.cx(), area.cy() + 60};
  for (int i = 0; i < 40 && h->map().routeAt(p.x, p.y, 12); i++) p.y += 9;
  h.click(p.x, p.y);
  h.key(Key::Enter);
  CHECK_EQ(h->world().routes.size(), n);
  h.key(Key::Escape);
  CHECK(h->ui.tool == app::ToolId::Route);
  h.key(Key::Enter);
  CHECK_EQ(h->world().routes.size(), n);
  // Esc без начатого маршрута — к инструменту выбора.
  h.key(Key::Escape);
  CHECK(h->ui.tool == app::ToolId::Select);
}

// ТЗ 1.d.v: «За каждый торговый маршрут, который проходит через провинцию, ...+10 %». Раньше маршрут рисовался
// сглаженной кривой (Катмулл — Ром), а провинции маршрута (бонус, подсветка, список в инспекторе) считались по
// исходной ломаной. Исправлено (карта): линия на карте — ровно та ломаная (map::detail::routeLine) — регрессия.
TEST(app_tools_route_drawn_matches_counted) {
  Harness h("audit_tzmil_routes");
  h.demo();
  const World& w = h->world();
  auto fs = geo::faces(w);
  int mismatches = 0;
  auto land = [&](const std::vector<Id>& ids) {
    std::vector<Id> out;
    for (Id p : ids)
      if (const Province* pr = w.province(p); pr && !pr->sea) out.push_back(p);
    std::sort(out.begin(), out.end());
    return out;
  };
  w.routes.each([&](const Route& r) {
    std::vector<Id> raw = land(fs->provincesOnPolyline(r.pts));
    std::vector<Id> drawn = land(fs->provincesOnPolyline(map::detail::routeLine(r.pts)));
    if (raw != drawn) {
      mismatches++;
      std::string onlyDrawn, onlyRaw;
      for (Id p : drawn)
        if (!std::binary_search(raw.begin(), raw.end(), p)) onlyDrawn += " «" + w.provinceName(p) + "»";
      for (Id p : raw)
        if (!std::binary_search(drawn.begin(), drawn.end(), p)) onlyRaw += " «" + w.provinceName(p) + "»";
      std::printf("  [audit tzmil] маршрут «%s»: линия на карте проходит через%s без бонуса; бонус без линии:%s\n", r.name.c_str(),
                  onlyDrawn.empty() ? " —" : onlyDrawn.c_str(), onlyRaw.empty() ? " —" : onlyRaw.c_str());
    }
  });
  // Синтетический маршрут-зигзаг через центр провинции: изгиб кривой выходит за ломаную.
  Id pid = 0;
  Vec2 c;
  double best = 0;
  w.provinces.each([&](const Province& p) {
    if (p.sea) return;
    const geo::ProvinceShape* sh = fs->shape(p.id);
    if (sh && sh->area > best) {
      best = sh->area;
      pid = p.id;
      c = sh->label;
    }
  });
  CHECK(pid != 0);
  std::vector<Vec2> zig = {c + Vec2(-600, 0), c + Vec2(-200, -500), c + Vec2(200, 500), c + Vec2(600, 0)};
  std::vector<Id> raw = land(fs->provincesOnPolyline(zig));
  std::vector<Id> drawn = land(fs->provincesOnPolyline(map::detail::routeLine(zig)));
  if (raw != drawn) mismatches++;
  std::printf("  [audit tzmil] зигзаг: провинций по ломаной %zu, по нарисованной кривой %zu\n", raw.size(), drawn.size());
  CHECK_MSG(mismatches == 0, "провинции с бонусом маршрута не совпадают с провинциями, через которые маршрут нарисован на карте");
  // Снимок: маршрут «Восточный тракт» и провинция «Рассветный берег» (бонус есть, линии нет).
  Id route = 0, prov = 0;
  w.routes.each([&](const Route& r) {
    if (r.name == "Восточный тракт") route = r.id;
  });
  w.provinces.each([&](const Province& p) {
    if (p.name == "Рассветный берег") prov = p.id;
  });
  if (route && prov) {
    h->setMapMode(schema::MapMode::Guilds);
    h->select(app::SelType::Route, route);
    h.settle();
    showAt(h, fs->shape(prov)->label, 0.35);
    h.dropToasts();
    h.settle();
    CHECK(h.shot("audit_tzmil_route_mismatch"));
    auto c = rules::calc(h->world());
    std::printf("  [audit tzmil] «Рассветный берег»: маршрутов по расчёту %d, текущая ценность %.2f при базе %.2f\n", c->province(prov)->routes,
                c->province(prov)->tradeValue, c->province(prov)->tradeBase);
  }
}

// ---------------------------------------------------------------- щелчок по торговому маршруту поверх провинции
TEST(app_tools_route_click_over_province) {
  Harness h("audit_tzprov_route", 1440, 900);
  h.demo();
  const World& w = h->world();
  CHECK(!w.routes.empty());
  auto fs = geo::faces(w);
  // Вершина маршрута над сухопутной провинцией, не под фигуркой войска.
  Id rid = 0;
  Vec2 at;
  gfx::Pt s;
  w.routes.each([&](const Route& r) {
    // Середины звеньев (вдали от центров провинций с диаграммами гильдий).
    for (size_t i = 1; i < r.pts.size() && !rid; i++) {
      Vec2 m = r.pts[i - 1] + (r.pts[i] - r.pts[i - 1]) * 0.4;
      const Province* p = w.province(fs->provinceAt(m));
      if (!p || p->sea) continue;
      h->map().centerOn(m, 0.8, false);
      h.frames(2);
      gfx::Pt q0 = h->map().view().toScreen(m);
      for (int dy = -20; dy <= 20 && !rid; dy++)
        for (int dx = -20; dx <= 20 && !rid; dx++) {
          gfx::Pt q{q0.x + float(dx), q0.y + float(dy)};
          if (h->map().armyAt(q.x, q.y) || h->map().routeAt(q.x, q.y, 2) != r.id || !freeOnScreen(h, q.x, q.y, 12)) continue;
          const Province* pq = w.province(h->map().provinceAt(q.x, q.y));
          if (!pq || pq->sea) continue;
          rid = r.id;
          at = h->map().view().toMap(q.x, q.y);
          s = q;
        }
    }
  });
  CHECK(rid != 0);
  h.settle();
  h.waitMap();
  s = h->map().view().toScreen(at);
  CHECK(h->map().routeAt(s.x, s.y) == rid);
  CHECK(h->map().armyAt(s.x, s.y) == 0);
  h.click(s.x, s.y);
  h.settle();
  std::printf("  click on route %lld at %.0f,%.0f -> selection type %d id %lld\n", (long long)rid, s.x, s.y, int(h->ui.sel.type), (long long)h->ui.sel.id);
  // Политическая карта: маршруты не нарисованы — щелчок выбирает провинцию под (невидимой) линией.
  CHECK(h->ui.mapMode == schema::MapMode::Political);
  CHECK(h->ui.sel.type == app::SelType::Province);
  // Регрессия: режим гильдий (маршруты на виду) — щелчок точно по линии выбирает маршрут, а не провинцию.
  h->clearSelection();
  h.key(Key::D2);
  h.settle();
  std::printf("  map mode %d\n", int(h->ui.mapMode));
  CHECK(h->ui.mapMode == schema::MapMode::Guilds);
  h.click(s.x, s.y);
  h.settle();
  std::printf("  guild mode click -> selection type %d\n", int(h->ui.sel.type));
  CHECK_MSG(h->ui.sel == (app::Selection{app::SelType::Route, rid}),
            "щелчок точно по линии торгового маршрута над провинцией выбирает провинцию, маршрут не выбрать");
  CHECK(cleanShot(h, "audit_tzprov_route_click"));
  // Выбранный маршрут виден и на политической карте — повторный щелчок по линии оставляет его выбранным.
  h.key(Key::D1);
  h.settle();
  h.frames(30);   // не двойной щелчок
  h.click(s.x, s.y);
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Route, rid}));
}
