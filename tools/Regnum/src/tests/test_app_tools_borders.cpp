// Сценарии правки границ (ТЗ 1.a.ii, 1.a.iv): режим правки и закреплённые границы (вне его Delete не удаляет
// провинцию; у «галочки» правки свой значок; прошлый ход — только чтение), перетаскивание общей точки (одна провинция
// растёт, соседняя уменьшается, отмена одним шагом), недопустимое перемещение, вставка и удаление точки (двойной
// щелчок, Delete, меню), сдвиг стрелками, скольжение стыка вдоль берега, ручки выбранной провинции на малом окне.
#include "tests/test_app_tools_util.h"

using namespace rg;
using namespace rg::toolstest;

namespace {

struct SharedPoint {
  Id edge = 0;
  int index = -1;
  Id a = 0, b = 0;     // a — слева (pl), b — справа (pr)
  Vec2 p, prev, next;
};

// Общая граница двух сухопутных провинций с владельцами: промежуточная точка с длинными соседними звеньями.
std::optional<SharedPoint> findShared(const World& w) {
  std::optional<SharedPoint> best;
  double bestScore = 0;
  w.edges.each([&](const Edge& e) {
    if (e.kind != EdgeKind::Border || !e.pl || !e.pr || e.pts.size() < 2) return;
    const Province* pa = w.province(e.pl);
    const Province* pb = w.province(e.pr);
    if (!pa || !pb || pa->sea || pb->sea || !pa->owner || !pb->owner) return;
    std::vector<Vec2> co = geo::edgeCoords(w, e);
    for (size_t i = 1; i + 1 < co.size(); i++) {
      double s = std::min(dist(co[i - 1], co[i]), dist(co[i], co[i + 1]));
      if (s > bestScore) {
        bestScore = s;
        best = SharedPoint{e.id, int(i) - 1, e.pl, e.pr, co[i], co[i - 1], co[i + 1]};
      }
    }
  });
  return best;
}

geo::Handle pointHandle(Id edge, int index) {
  geo::Handle h;
  h.kind = geo::Handle::Point;
  h.edge = edge;
  h.index = index;
  return h;
}

// Точка экрана рядом с s внутри провинции prov (не на ручке и не под панелями).
std::optional<gfx::Pt> pointInside(Harness& h, gfx::Pt s, Id prov, float r0 = 40) {
  for (float r = r0; r < 400; r += 20)
    for (int k = 0; k < 16; k++) {
      double ang = k * 3.14159265 / 8;
      gfx::Pt q{float(s.x + r * std::cos(ang)), float(s.y + r * std::sin(ang))};
      if (!freeOnScreen(h, q.x, q.y, 12)) continue;
      if (h->map().provinceAt(q.x, q.y) != prov) continue;
      Vec2 m = h->map().view().toMap(q.x, q.y);
      if (geo::hitEdge(h->world(), m, h->map().view().toMapLen(14), 0)) continue;
      return q;
    }
  return std::nullopt;
}

}  // namespace

TEST(app_tools_borders_view_mode_fixed) {
  // Правка выключена (ТЗ 1.a.ii): инструменты правки недоступны, границы не меняются, щелчок — сведения.
  Harness h("tools_view_mode");
  h.demo();
  CHECK(!h->ui.editBorders);
  CHECK(h->uiRect("tool.new-province") == nullptr);
  CHECK(h->uiRect("tool.route") != nullptr);   // маршрут — не инструмент правки границ
  for (Key k : {Key::B, Key::P, Key::G, Key::X, Key::U, Key::K, Key::J, Key::D}) {
    h.key(k);
    CHECK(h->ui.tool == app::ToolId::Select);
  }
  auto sp = findShared(h->world());
  CHECK(sp.has_value());
  focus(h, Box2{sp->p.x - 150, sp->p.y - 100, sp->p.x + 150, sp->p.y + 100});
  u64 v0 = h->store.version();
  gfx::Pt s = screenOf(h, sp->p);
  h.drag(s.x, s.y, s.x + 40, s.y + 30);
  CHECK_EQ(h->store.version(), v0);   // границы закреплены
  auto in = pointInside(h, s, sp->a);
  CHECK(in.has_value());
  h.click(in->x, in->y);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Province, sp->a}));
  CHECK(h->uiRect("inspector") != nullptr);
  CHECK(h->ui.tool == app::ToolId::Select);
  // Включение правки показывает инструменты правки.
  h.key(Key::E);
  CHECK(h->ui.editBorders);
  for (const char* n : {"tool.borders-tool", "tool.new-province", "tool.add-area", "tool.remove-area", "tool.fill", "tool.knife", "tool.merge", "tool.delete"})
    CHECK_MSG(h->uiRect(n) != nullptr, n);
}

// ---------------------------------------------------------------- ТЗ 1.a.ii: выключенная правка — Delete удаляет провинцию
TEST(app_tools_borders_view_mode_delete_key) {
  Harness h("audit_tzprov_viewdel", 1440, 900);
  h.demo();
  CHECK(!h->ui.editBorders);
  auto vp = h.visibleProvince();
  CHECK(vp.has_value());
  h.click(vp->second.x, vp->second.y);
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Province, vp->first}));
  size_t n0 = h->world().provinces.size();
  h.key(Key::Delete);
  h.settle();
  bool dlg = h->hasDialog();
  std::printf("  dialog after Delete in view mode: %d\n", int(dlg));
  if (dlg) {
    CHECK(h.shot("audit_tzprov_view_mode_delete"));
    h.key(Key::Enter);
    h.settle();
  }
  std::printf("  provinces %zu -> %zu\n", n0, size_t(h->world().provinces.size()));
  // Регрессия (ТЗ 1.a.ii): вне режима правки Delete не удаляет провинцию и не спрашивает об этом.
  CHECK(!dlg);
  CHECK_MSG(h->world().provinces.size() == n0, "при выключенной «Правке границ» клавиша Delete удаляет провинцию с карты");
  // В режиме правки — подтверждение удаления.
  h->setEditBorders(true);
  h.settle();
  h->select(app::SelType::Province, vp->first);
  h.settle();
  h.move(4, 4);
  h.key(Key::Delete);
  h.settle();
  CHECK(h->hasDialog("confirm"));
}

// ---------------------------------------------------------------- ТЗ 1.a.ii: «галочка» правки и инструмент «Правка границ» — один и тот же значок
TEST(app_tools_borders_toggle_icon) {
  Harness h("audit_tzprov_toggle", 1440, 900);
  h.demo();
  h.key(Key::E);
  h.settle();
  CHECK(h->ui.editBorders);
  CHECK(h->ui.tool == app::ToolId::EditBorders);
  const app::ToolDef* bt = app::findTool(app::ToolId::EditBorders);
  CHECK(bt != nullptr);
  std::printf("  borders tool icon «%s», toggle icon when on «%s»\n", bt->icon, app::detail::bordersToggleIcon(true));
  const RectF* a = h->uiRect("tool.borders");
  const RectF* b = h->uiRect("tool.borders-tool");
  CHECK(a && b);
  CHECK(cleanShot(h, "audit_tzprov_edit_toggle"));
  // Регрессия: у «галочки» режима правки свой значок (замок), отличный от инструмента и в обоих состояниях.
  CHECK_MSG(std::string(bt->icon) != app::detail::bordersToggleIcon(true),
            "включённая «галочка» правки границ и соседний инструмент «Правка границ провинции» показаны одинаковым значком");
  CHECK(std::string(app::detail::bordersToggleIcon(true)) != app::detail::bordersToggleIcon(false));
}

// Просмотр прошлого хода — только чтение, но кнопка-«галочка» правки границ на панели инструментов включает
// режим правки (команда E при этом недоступна): появляются инструменты правки, инструмент границ активен.
TEST(app_tools_borders_readonly_past_turn) {
  Harness h("audit_cr_ro_borders");
  h.demo();
  int t0 = h->store.world().turn();
  CHECK(h->endTurnNow());
  h.settle();
  CHECK(h->viewTurn(t0));
  h.settle();
  CHECK(h->readOnly());
  CHECK(!h->ui.editBorders);
  CHECK(h->uiRect("tool.borders") != nullptr);
  CHECK(h.clickUi("tool.borders"));
  h.settle();
  CHECK_MSG(!h->ui.editBorders, "в режиме только просмотра кнопка правки границ включила правку");
  CHECK_MSG(h->ui.tool != app::ToolId::EditBorders, "в режиме только просмотра активен инструмент правки границ");
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_cr_ro_borders"));
}

TEST(app_tools_borders_shared_drag) {
  Harness h("tools_borders");
  h.demo();
  const World w0 = h->world();
  auto sp = findShared(w0);
  CHECK(sp.has_value());
  h.key(Key::E);
  CHECK(h->ui.editBorders);
  // Выбрать провинцию щелчком (инспектор открывается), затем показать точку крупно.
  h->select(app::SelType::Province, sp->a);
  h.frames(2);
  focus(h, Box2{sp->p.x - 120, sp->p.y - 80, sp->p.x + 120, sp->p.y + 80});
  gfx::Pt s = screenOf(h, sp->p);
  auto in = pointInside(h, s, sp->a);
  CHECK(in.has_value());
  h.click(in->x, in->y);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Province, sp->a}));
  CHECK(h->ui.tool == app::ToolId::EditBorders);
  CHECK(h->uiRect("tool.options") != nullptr);
  // Наведение на ручку.
  h.move(s.x, s.y);
  CHECK(cleanShot(h, "tools_borders"));
  CHECK(hl::cursor() == platform::Cursor::Move);

  // Направление в сторону соседа b.
  Vec2 dir = (sp->next - sp->prev).perp().norm();
  const map::View& v = h->map().view();
  if (geo::faces(h->world())->provinceAt(sp->p + dir * v.toMapLen(6)) != sp->b) dir = -dir;
  CHECK(geo::faces(h->world())->provinceAt(sp->p + dir * v.toMapLen(6)) == sp->b);
  double step = std::min(v.toMapLen(22), 0.3 * std::min(dist(sp->p, sp->prev), dist(sp->p, sp->next)));
  Vec2 target = sp->p + dir * step;
  geo::Handle hd = pointHandle(sp->edge, sp->index);
  CHECK(geo::canMove(h->world(), hd, target));
  double a0 = provArea(h->world(), sp->a), b0 = provArea(h->world(), sp->b);
  // Живой предпросмотр: мир меняется во время перетаскивания.
  std::vector<gfx::Pt> path;
  for (int i = 0; i <= 6; i++) path.push_back(screenOf(h, sp->p + (target - sp->p) * (i / 6.0)));
  stroke(h, path, false);
  CHECK(dist(geo::handlePos(h->world(), hd), sp->p) > step * 0.9);
  CHECK(cleanShot(h, "tools_borders_drag"));
  release(h, path.back());
  double a1 = provArea(h->world(), sp->a), b1 = provArea(h->world(), sp->b);
  CHECK(a1 > a0 + 1);                     // провинция растёт
  CHECK(b1 < b0 - 1);                     // соседняя уменьшается
  CHECK_NEAR(a1 + b1, a0 + b0, 1e-3 * (a0 + b0));
  CHECK(geo::validate(h->world()).empty());
  // Всё перетаскивание — один шаг отмены.
  CHECK_EQ(h->store.undoLabel(), std::string("Переместить точку границы"));
  h.key(Key::Z, ctrl());
  CHECK(geo::handlePos(h->world(), hd) == sp->p);
  CHECK_NEAR(provArea(h->world(), sp->a), a0, 1e-6);
  CHECK_NEAR(provArea(h->world(), sp->b), b0, 1e-6);
  CHECK(h->world().edges.same(w0.edges));

  // Недопустимое перемещение: граница пересекла бы другую линию — ручка красная, мир не меняется.
  // Ближайшая к ручке свободная точка экрана, куда перемещение недопустимо.
  Vec2 bad;
  bool found = false;
  double bestD = 1e18;
  for (float dy = -220; dy <= 220; dy += 8)
    for (float dx = -320; dx <= 320; dx += 8) {
      gfx::Pt q{s.x + dx, s.y + dy};
      if (!freeOnScreen(h, q.x, q.y, 12) || dx * dx + dy * dy >= bestD || dx * dx + dy * dy < 30 * 30) continue;
      Vec2 m = v.toMap(q.x, q.y);
      if (geo::canMove(h->world(), hd, m)) continue;
      bestD = dx * dx + dy * dy;
      bad = m;
      found = true;
    }
  CHECK(found);
  u64 v1 = h->store.version();
  stroke(h, {s, screenOf(h, bad)}, false);
  CHECK_EQ(h->store.version(), v1);
  CHECK(hl::cursor() == platform::Cursor::NotAllowed);
  CHECK(cleanShot(h, "tools_borders_invalid"));
  release(h, screenOf(h, bad));
  CHECK_EQ(h->store.version(), v1);
  CHECK(geo::handlePos(h->world(), hd) == sp->p);
  CHECK(!h->toasts().empty());
  CHECK(h->toasts().back().kind == app::ToastKind::Warning);
}

TEST(app_tools_borders_insert_delete_nudge) {
  Harness h("tools_points");
  h.demo();
  auto sp = findShared(h->world());
  CHECK(sp.has_value());
  h.key(Key::E);
  h->select(app::SelType::Province, sp->a);
  h.frames(2);
  focus(h, Box2{sp->p.x - 120, sp->p.y - 80, sp->p.x + 120, sp->p.y + 80});
  h.key(Key::B);
  CHECK(h->ui.tool == app::ToolId::EditBorders);
  size_t n0 = h->world().edges.get(sp->edge)->pts.size();
  // Двойной щелчок по середине звена — новая точка (и она выбрана).
  Vec2 mid = (sp->p + sp->next) * 0.5;
  gfx::Pt ms = screenOf(h, mid);
  h.move(ms.x, ms.y);
  CHECK(hl::cursor() == platform::Cursor::Crosshair);
  CHECK(cleanShot(h, "tools_borders_insert_hover"));
  h.doubleClick(ms.x, ms.y);
  CHECK_EQ(h->world().edges.get(sp->edge)->pts.size(), n0 + 1);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Province, sp->a}));   // выделение не перешло к соседу
  CHECK(h->uiRect("tool.options.handle") != nullptr);
  // Delete — удалить выбранную точку.
  h.key(Key::Delete);
  CHECK_EQ(h->world().edges.get(sp->edge)->pts.size(), n0);
  CHECK(h->hasDialog() == false);   // удалена точка, а не провинция
  CHECK(h->world().province(sp->a) != nullptr);
  // Отмена возвращает точку, повтор — снова удаляет.
  h.key(Key::Z, ctrl());
  CHECK_EQ(h->world().edges.get(sp->edge)->pts.size(), n0 + 1);
  h.key(Key::Y, ctrl());
  CHECK_EQ(h->world().edges.get(sp->edge)->pts.size(), n0);

  // Меню правой кнопки: удалить точку.
  gfx::Pt s = screenOf(h, sp->p);
  h.click(s.x, s.y, platform::MouseRight);
  h.settle();
  CHECK(cleanShot(h, "tools_borders_menu"));
  CHECK(h.clickUi("borders.menu.delete"));
  CHECK_EQ(h->world().edges.get(sp->edge)->pts.size(), n0 - 1);
  h.key(Key::Z, ctrl());
  CHECK_EQ(h->world().edges.get(sp->edge)->pts.size(), n0);
  // Меню на границе: добавить точку.
  h.click(ms.x, ms.y, platform::MouseRight);
  h.settle();
  CHECK(h.clickUi("borders.menu.insert"));
  CHECK_EQ(h->world().edges.get(sp->edge)->pts.size(), n0 + 1);
  h.key(Key::Z, ctrl());

  // Выбор ручки щелчком и сдвиг стрелками (Shift — ×10).
  h.click(s.x, s.y);
  geo::Handle hd = pointHandle(sp->edge, sp->index);
  double px = h->map().view().toMapLen(1);
  h.key(Key::Right);
  CHECK_NEAR(geo::handlePos(h->world(), hd).x, sp->p.x + px, 1e-6);
  h.key(Key::Down, platform::ModShift);
  CHECK_NEAR(geo::handlePos(h->world(), hd).y, sp->p.y + 10 * px, 1e-6);
  // Подряд идущие сдвиги — один шаг отмены.
  h.key(Key::Z, ctrl());
  CHECK(geo::handlePos(h->world(), hd) == sp->p);
  // Esc: сначала ручка, затем выделение провинции.
  h.click(s.x, s.y);
  CHECK(h->uiRect("tool.options.handle") != nullptr);
  h.key(Key::Escape);
  CHECK(h->uiRect("tool.options.handle") == nullptr);
  CHECK(h->ui.sel.type == app::SelType::Province);
  h.key(Key::Escape);
  CHECK(!h->ui.sel);
}

TEST(app_tools_borders_slide_junction) {
  Harness h("tools_slide");
  h.demo();
  const World& w = h->world();
  // Стык границы с берегом у сухопутной провинции: соседняя береговая точка достаточно далеко.
  struct J {
    Id node = 0, prov = 0;
    Vec2 p, along;
  };
  std::optional<J> best;
  double bestLen = 0;
  w.nodes.each([&](const Node& n) {
    if (!geo::isCoastJunction(w, n.id)) return;
    Id prov = 0;
    Vec2 along;
    double len = 0;
    w.edges.each([&](const Edge& e) {
      if (e.a != n.id && e.b != n.id) return;
      if (e.kind == EdgeKind::Border && !prov) prov = e.pl ? e.pl : e.pr;
      if (e.kind != EdgeKind::Coast) return;
      std::vector<Vec2> co = geo::edgeCoords(w, e);
      if (co.size() < 3) return;
      Vec2 q = e.a == n.id ? co[2] : co[co.size() - 3];
      double l = dist(q, n.p);
      if (l > len && l < 60) {
        len = l;
        along = q;
      }
    });
    const Province* p = w.province(prov);
    if (!p || p->sea || !p->owner) return;
    if (len > bestLen) {
      bestLen = len;
      best = J{n.id, prov, n.p, along};
    }
  });
  CHECK(best.has_value());
  h.key(Key::E);
  h->select(app::SelType::Province, best->prov);
  h.frames(2);
  focus(h, Box2{best->p.x - 60, best->p.y - 40, best->p.x + 60, best->p.y + 40});
  h.key(Key::B);
  gfx::Pt s = screenOf(h, best->p);
  h.move(s.x, s.y);
  CHECK(hl::cursor() == platform::Cursor::Move);
  double a0 = provArea(h->world(), best->prov);
  dragMap(h, best->p, best->along, 6, false);
  CHECK(cleanShot(h, "tools_borders_slide"));
  release(h, screenOf(h, best->along));
  const Node* n = h->world().nodes.get(best->node);
  CHECK(n != nullptr);
  CHECK(dist(n->p, best->p) > 1);              // стык сдвинулся
  CHECK(geo::isCoastJunction(h->world(), best->node));   // и остался на берегу
  CHECK(std::fabs(provArea(h->world(), best->prov) - a0) > 1e-3);
  CHECK(geo::validate(h->world()).empty());
  CHECK_EQ(h->store.undoLabel(), std::string("Сдвинуть стык границы с берегом"));
  h.key(Key::Z, ctrl());
  CHECK(h->world().nodes.get(best->node)->p == best->p);
  CHECK_NEAR(provArea(h->world(), best->prov), a0, 1e-6);
}

// ---------------------------------------------------------------- ТЗ 1.a.iv: режим правки на небольшом окне — снимок ручек выбранной провинции
TEST(app_tools_borders_small_window_shot) {
  Harness h("audit_tzprov2_small", 1280, 720);
  h.demo();
  h.key(Key::E);
  auto vp = h.visibleProvince();
  CHECK(vp.has_value());
  h.click(vp->second.x, vp->second.y);
  h.settle();
  CHECK(h->ui.sel.id == vp->first);
  CHECK(h->ui.tool == app::ToolId::EditBorders);
  CHECK(cleanShot(h, "audit_tzprov2_edit_small"));
  const RectF* tb = h->uiRect("toolbar");
  const RectF* lg = h->uiRect("legend");
  if (tb && lg) std::printf("  toolbar %.0f,%.0f-%.0f,%.0f legend %.0f,%.0f-%.0f,%.0f\n", tb->x, tb->y, tb->right(), tb->bottom(), lg->x, lg->y, lg->right(), lg->bottom());
}
