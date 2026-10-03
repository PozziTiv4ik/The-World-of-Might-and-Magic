// Сценарии правки карты (режим «Правка карты», T): горы, замки и башни — выбор, перетаскивание, рамка, удаление,
// кисть; озеро, река и стена; точки фигуры; суша и море контуром; точки берега; инспектор; перенос объектов
// базовой карты в мир при первой правке и его отмена; сохранение и чтение data/map.json.
#include "app/tools_map.h"
#include "tests/test_app_tools_util.h"

using namespace rg;
using namespace rg::toolstest;

namespace {

constexpr double kZoom = 2.0;   // логических пикселей на единицу карты: замок 40 × 31 — 80 × 62 точки

// Демонстрационный мир, режим правки карты.
void mapMode(Harness& h) {
  h.demo();
  h.dropToasts();
  h.key(Key::T);
  CHECK(h->ui.editMap);
  CHECK(h->ui.tool == app::ToolId::MapObjects);
}

const map::art::Objects& baseObjs(Harness& h) { return h->basemap()->objects(); }

// Знак базовой карты вида kind, габарит которого (с запасом) не пересекает других знаков.
const MapSymbol* isolated(Harness& h, SymbolKind kind, double margin = 25) {
  const auto& list = baseObjs(h).symbols;
  for (const MapSymbol& s : list) {
    if (s.kind != kind) continue;
    const Box2 b = map::art::symbolBox(s).inflated(margin);
    if (b.x0 < 200 || b.y0 < 200 || b.x1 > 7800 || b.y1 > 4300) continue;
    bool alone = true;
    for (const MapSymbol& o : list)
      if (o.id != s.id && map::art::symbolBox(o).intersects(b)) {
        alone = false;
        break;
      }
    if (alone) return &s;
  }
  return nullptr;
}

void look(Harness& h, Vec2 p, double zoom = kZoom) {
  h->map().centerOn(p, zoom, false);
  h.settle();
}

gfx::Pt centerOf(Harness& h, const MapSymbol& s) { return scr(h, map::art::symbolBox(s).center()); }

double landArea(const World& w) {
  auto fs = geo::buildFaces(w);
  double a = 0;
  for (auto& f : fs->faces)
    if (f.terrain == Terrain::Land) a += f.area;
  return a;
}

}  // namespace

TEST(app_mapedit_mode_and_tools) {
  ToolGuard guard;
  Harness h("mapedit_mode");
  mapMode(h);
  for (const char* n : {"tool.mapedit", "tool.map-objects", "tool.symbol", "tool.lake", "tool.river", "tool.wall", "tool.land-add", "tool.land-remove",
                        "tool.coast"})
    CHECK_MSG(h->uiRect(n) != nullptr, n);
  // Правка границ выключает правку карты и наоборот; инструменты карты без режима не включаются.
  h.key(Key::E);
  CHECK(h->ui.editBorders);
  CHECK(!h->ui.editMap);
  CHECK(h->uiRect("tool.symbol") == nullptr);
  h.key(Key::S);
  CHECK(h->ui.tool != app::ToolId::MapSymbol);
  h.key(Key::T);
  CHECK(h->ui.editMap);
  CHECK(!h->ui.editBorders);
  h.key(Key::S);
  CHECK(h->ui.tool == app::ToolId::MapSymbol);
  h.key(Key::W, platform::ModShift);
  CHECK(h->ui.tool == app::ToolId::MapRiver);
  h.key(Key::Y);
  CHECK(h->ui.tool == app::ToolId::Coast);
  h.key(Key::T);
  CHECK(!h->ui.editMap);
  CHECK(h->ui.tool == app::ToolId::Select);
}

TEST(app_mapedit_symbol_select_drag_undo) {
  ToolGuard guard;
  Harness h("mapedit_symbol");
  mapMode(h);
  const MapSymbol* c = isolated(h, SymbolKind::Castle);
  CHECK(c != nullptr);
  const MapSymbol castle = *c;
  look(h, castle.p);
  CHECK(!h->world().ownMapObjects());
  // Щелчок — замок в инспекторе; мир ещё не хранит своих объектов, выделение держится.
  gfx::Pt at = centerOf(h, castle);
  h.click(at.x, at.y);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Symbol, castle.id}));
  CHECK(h->uiRect("inspector") != nullptr);
  CHECK(h->uiRect("mapobject.kind") != nullptr);
  h.frames(3);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Symbol, castle.id}));
  CHECK(!h->world().ownMapObjects());
  CHECK(cleanShot(h, "mapedit_symbol_selected"));
  // Перетаскивание на 60 × 30 точек: при отпускании — перенос объектов в мир и новое место, одним шагом отмены.
  const size_t undo0 = h->store.canUndo() ? 1 : 0;
  h.drag(at.x, at.y, at.x + 60, at.y + 30);
  const World& w = h->world();
  CHECK(w.ownMapObjects());
  CHECK_EQ(size_t(w.symbols.size()), baseObjs(h).symbols.size());
  CHECK_EQ(size_t(w.shapes.size()), baseObjs(h).shapes.size());
  const MapSymbol* moved = w.symbol(castle.id);
  CHECK(moved != nullptr);
  CHECK_NEAR(moved->p.x, castle.p.x + 60 / kZoom, 0.6);
  CHECK_NEAR(moved->p.y, castle.p.y + 30 / kZoom, 0.6);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Symbol, castle.id}));
  CHECK_EQ(h->store.undoLabel(), std::string("Переместить знак"));
  (void)undo0;
  h.waitMap();
  CHECK(cleanShot(h, "mapedit_symbol_moved"));
  h.key(Key::Z, ctrl());
  CHECK(!h->world().ownMapObjects());   // отмена вернула и перенос
  CHECK(h->ui.sel == (app::Selection{app::SelType::Symbol, castle.id}));
  h.key(Key::Y, ctrl());
  CHECK(h->world().ownMapObjects());
  // Стрелки сдвигают выбранный знак, Delete удаляет.
  const Vec2 p1 = h->world().symbol(castle.id)->p;
  h.key(Key::Right);
  CHECK_NEAR(h->world().symbol(castle.id)->p.x, p1.x + 1 / kZoom, 1e-6);
  h.key(Key::Delete);
  CHECK(!h->world().symbol(castle.id));
  CHECK(!h->ui.sel);
}

TEST(app_mapedit_rubber_select_delete) {
  ToolGuard guard;
  Harness h("mapedit_rubber");
  mapMode(h);
  // Гуща гор: самый населённый квадрат 120 × 120.
  const auto& list = baseObjs(h).symbols;
  Box2 best;
  size_t bestN = 0;
  for (const MapSymbol& s : list) {
    if (s.kind != SymbolKind::Mountain || s.p.x < 300 || s.p.y < 300 || s.p.x > 7700 || s.p.y > 4200) continue;
    const Box2 b(s.p.x - 60, s.p.y - 60, s.p.x + 60, s.p.y + 60);
    size_t n = 0;
    for (const MapSymbol& o : list) n += b.contains(o.p) ? 1 : 0;
    if (n > bestN) {
      bestN = n;
      best = b;
    }
    if (bestN >= 12) break;
  }
  CHECK(bestN >= 5);
  look(h, best.center(), 1.5);
  // Рамка по пустому месту ведётся от угла за пределами знаков: начинаем с точки без знака.
  Vec2 from(best.x0, best.y0), to(best.x1, best.y1);
  for (int k = 0; k < 20 && h->map().artScene()->symbolAt(from, 2); k++) from = from + Vec2(-3, -3);
  dragMap(h, from, to);
  const std::vector<Id> sel = app::mapedit::selectedSymbols(h.a());
  CHECK(sel.size() >= 5);
  CHECK(h->uiRect("mapobject.delete") != nullptr);   // инспектор группы
  h.key(Key::Delete);
  for (Id id : sel) CHECK(!h->world().symbol(id));
  CHECK_EQ(size_t(h->world().symbols.size()), list.size() - sel.size());
  CHECK(!h->ui.sel);
  h.key(Key::Z, ctrl());
  CHECK(!h->world().ownMapObjects());
}

TEST(app_mapedit_place_symbols_brush) {
  ToolGuard guard;
  Harness h("mapedit_brush");
  mapMode(h);
  // Свободная суша без знаков: середина самой большой ничьей грани, где знаков нет.
  auto fs = geo::faces(h->world());
  Vec2 spot;
  bool found = false;
  auto sc = h->map().artScene();
  for (const geo::Face& f : fs->faces) {
    if (f.terrain != Terrain::Land || f.area < 40000) continue;
    std::vector<u32> ids;
    sc->index().symbols(Box2(f.label.x - 80, f.label.y - 80, f.label.x + 80, f.label.y + 80), ids);
    if (ids.empty()) {
      spot = f.label;
      found = true;
      break;
    }
  }
  CHECK(found);
  look(h, spot, 1.5);
  h.key(Key::S);
  CHECK(h->ui.tool == app::ToolId::MapSymbol);
  CHECK(h->uiRect("tool.options.kind") != nullptr);
  const size_t n0 = baseObjs(h).symbols.size();
  clickMap(h, spot);
  CHECK(h->world().ownMapObjects());
  CHECK_EQ(size_t(h->world().symbols.size()), n0 + 1);
  CHECK(h->ui.sel.type == app::SelType::Symbol);
  const MapSymbol* s = h->world().symbol(h->ui.sel.id);
  CHECK(s && s->kind == SymbolKind::Mountain);
  CHECK(dist(s->p, spot) < 1);
  // Кисть: протяжка на 120 единиц — горный хребет одним шагом отмены.
  dragMap(h, spot + Vec2(-60, 30), spot + Vec2(60, 30), 12);
  const size_t added = h->world().symbols.size() - (n0 + 1);
  CHECK(added >= 4);
  CHECK_EQ(app::mapedit::selectedSymbols(h.a()).size(), added);
  CHECK(cleanShot(h, "mapedit_brush"));
  h.key(Key::Z, ctrl());
  CHECK_EQ(size_t(h->world().symbols.size()), n0 + 1);
}

TEST(app_mapedit_lake_river_wall_points) {
  ToolGuard guard;
  Harness h("mapedit_shapes");
  mapMode(h);
  auto fs = geo::faces(h->world());
  // Ничья суша: середина крупной грани.
  Vec2 c;
  for (const geo::Face& f : fs->faces)
    if (f.terrain == Terrain::Land && f.area > 60000) {
      c = f.label;
      break;
    }
  look(h, c, 1.5);
  const size_t n0 = baseObjs(h).shapes.size();
  // Озеро: четыре вершины и Enter.
  h.key(Key::W);
  CHECK(h->ui.tool == app::ToolId::MapLake);
  for (Vec2 d : {Vec2(-40, -30), Vec2(40, -30), Vec2(40, 30), Vec2(-40, 30)}) clickMap(h, c + d);
  h.key(Key::Enter);
  CHECK_EQ(size_t(h->world().shapes.size()), n0 + 1);
  CHECK(h->ui.sel.type == app::SelType::Shape);
  const Id lake = h->ui.sel.id;
  CHECK(h->world().shape(lake)->kind == ShapeKind::Water);
  CHECK_EQ(h->world().shape(lake)->pts.size(), size_t(4));
  // Река и стена линиями.
  h.key(Key::W, platform::ModShift);
  for (Vec2 d : {Vec2(-120, -80), Vec2(-60, -60), Vec2(-45, -32)}) clickMap(h, c + d);
  h.key(Key::Enter);
  const MapShape* river = h->world().shape(h->ui.sel.id);
  CHECK(river && river->kind == ShapeKind::River && river->pts.size() == 3);
  CHECK_EQ(river->w, schema::kRiverWidth);
  h.key(Key::Q);
  for (Vec2 d : {Vec2(60, 60), Vec2(140, 70)}) clickMap(h, c + d);
  h.key(Key::Enter);
  const MapShape* wall = h->world().shape(h->ui.sel.id);
  CHECK(wall && wall->kind == ShapeKind::Wall && wall->pts.size() == 2);
  CHECK_EQ(size_t(h->world().shapes.size()), n0 + 3);
  // Точки озера: выбрать, перетащить вершину, двойной щелчок по контуру — новая, Delete — удалить её.
  h.key(Key::O);
  clickMap(h, c);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Shape, lake}));
  const Vec2 v0 = h->world().shape(lake)->pts[2];
  dragMap(h, v0, v0 + Vec2(15, 10));
  CHECK(dist(h->world().shape(lake)->pts[2], v0 + Vec2(15, 10)) < 0.6);
  const Vec2 mid = (h->world().shape(lake)->pts[0] + h->world().shape(lake)->pts[1]) * 0.5;
  gfx::Pt ms = scr(h, mid);
  h.doubleClick(ms.x, ms.y);
  CHECK_EQ(h->world().shape(lake)->pts.size(), size_t(5));
  h.key(Key::Delete);
  CHECK_EQ(h->world().shape(lake)->pts.size(), size_t(4));
  // Перетаскивание фигуры целиком.
  const Vec2 a0 = h->world().shape(lake)->pts[0];
  dragMap(h, c + Vec2(0, 5), c + Vec2(20, 5));
  CHECK(dist(h->world().shape(lake)->pts[0], a0 + Vec2(20, 0)) < 0.6);
  CHECK(cleanShot(h, "mapedit_shapes"));
  // Инспектор: ширина стены.
  h->select(app::SelType::Shape, wall->id);
  h.settle();
  CHECK(h->uiRect("mapobject.width") != nullptr);
}

TEST(app_mapedit_land_and_coast) {
  ToolGuard guard;
  Harness h("mapedit_land");
  mapMode(h);
  const World& w0 = h->world();
  auto fs = geo::faces(w0);
  // Открытое море: квадрат 80 × 80 целиком в море.
  Vec2 sea;
  bool found = false;
  for (double y = 600; y < 4000 && !found; y += 200)
    for (double x = 600; x < 7400 && !found; x += 200) {
      bool all = true;
      for (Vec2 d : {Vec2(-60, -60), Vec2(60, -60), Vec2(60, 60), Vec2(-60, 60), Vec2(0, 0)}) all = all && fs->terrainAt(Vec2(x, y) + d) == Terrain::Sea;
      if (all) {
        sea = {x, y};
        found = true;
      }
    }
  CHECK(found);
  look(h, sea, 1.5);
  const double land0 = landArea(h->world());
  h.key(Key::N);
  CHECK(h->ui.tool == app::ToolId::LandAdd);
  for (Vec2 d : {Vec2(-40, -40), Vec2(40, -40), Vec2(40, 40), Vec2(-40, 40)}) clickMap(h, sea + d);
  h.key(Key::Enter);
  CHECK_NEAR(landArea(h->world()), land0 + 6400, 1);
  CHECK(geo::validate(h->world()).empty());
  CHECK(geo::faces(h->world())->terrainAt(sea) == Terrain::Land);
  h.waitMap();
  CHECK(cleanShot(h, "mapedit_land_added"));
  // Миниатюра карты мира (мини-карта, список миров) догоняет правку: новый остров на ней — суша.
  auto islandShown = [&] {
    std::shared_ptr<const gfx::Image> th = h->map().mapThumbnail();
    if (!th) return false;
    const double k = th->w / schema::kMapWidth;
    return gfx::unpremul(th->at(int(sea.x * k), int(sea.y * k))).r > 150;   // было море (0, 38, 255)
  };
  bool shown = false;
  for (int i = 0; i < 400 && !shown; i++) {
    hl::advance(0.05);
    h.frame();
    shown = islandShown();
  }
  CHECK(shown);
  CHECK(h->map().mapThumbnail().get() != h->basemap()->thumb().get());
  // Залив в новом острове: «Убрать сушу».
  h.key(Key::N, platform::ModShift);
  for (Vec2 d : {Vec2(-10, -50), Vec2(10, -50), Vec2(10, 0), Vec2(-10, 0)}) clickMap(h, sea + d);
  h.key(Key::Enter);
  CHECK_NEAR(landArea(h->world()), land0 + 6400 - 20 * 40, 1);
  CHECK(geo::validate(h->world()).empty());
  // Берег: точка нового острова — тянуть наружу.
  h.key(Key::Y);
  CHECK(h->ui.tool == app::ToolId::Coast);
  geo::Handle corner = geo::hitHandle(h->world(), sea + Vec2(40, 40), 2, 0, true);
  CHECK(corner);
  const double land1 = landArea(h->world());
  dragMap(h, geo::handlePos(h->world(), corner), sea + Vec2(55, 55));
  CHECK(landArea(h->world()) > land1 + 100);
  CHECK(geo::validate(h->world()).empty());
  // Двойной щелчок по берегу — новая точка, Delete — удалить.
  const Vec2 edgeMid = sea + Vec2(-40, 5);
  const size_t pts0 = [&] {
    size_t n = 0;
    h->world().edges.each([&](const Edge& e) { n += e.kind == EdgeKind::Coast ? e.pts.size() : 0; });
    return n;
  }();
  gfx::Pt em = scr(h, edgeMid);
  h.doubleClick(em.x, em.y);
  size_t pts1 = 0;
  h->world().edges.each([&](const Edge& e) { pts1 += e.kind == EdgeKind::Coast ? e.pts.size() : 0; });
  CHECK_EQ(pts1, pts0 + 1);
  h.key(Key::Delete);
  size_t pts2 = 0;
  h->world().edges.each([&](const Edge& e) { pts2 += e.kind == EdgeKind::Coast ? e.pts.size() : 0; });
  CHECK_EQ(pts2, pts0);
  CHECK(cleanShot(h, "mapedit_coast"));
}

TEST(app_mapedit_inspector_and_save) {
  ToolGuard guard;
  Harness h("mapedit_save");
  mapMode(h);
  const MapSymbol* t = isolated(h, SymbolKind::Tower);
  CHECK(t != nullptr);
  const MapSymbol tower = *t;
  look(h, tower.p);
  gfx::Pt at = centerOf(h, tower);
  h.click(at.x, at.y);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Symbol, tower.id}));
  // Вид в инспекторе: «Замок» (четвёртый сегмент).
  const RectF* kind = h->uiRect("mapobject.kind");
  CHECK(kind != nullptr);
  h.click(kind->x + kind->w * 0.7f, kind->cy());
  CHECK(h->world().ownMapObjects());
  CHECK(h->world().symbol(tower.id)->kind == SymbolKind::Castle);
  // Сохранение: data/map.json и флаг в world.json; чтение возвращает те же объекты.
  const std::string dir = fs::join(h.root, "Мир с картой");
  CHECK(h->saveTo(dir));
  CHECK(fs::isFile(fs::join(dir, "data/map.json")));
  io::LoadResult r = io::load(dir);
  CHECK_MSG(r.warnings.empty(), r.warnings.empty() ? std::string() : r.warnings[0].text());
  CHECK(r.world.meta->mapObjects);
  CHECK_EQ(r.world.symbols.size(), h->world().symbols.size());
  CHECK(r.world.symbol(tower.id) && r.world.symbol(tower.id)->kind == SymbolKind::Castle);
  // Мир без своих объектов сохраняет пустой data/map.json и при чтении показывает базовую карту.
  h->loadWorld(map::makeDemoWorld(*h->basemap()), "Демо");
  h.settle();
  const std::string dir2 = fs::join(h.root, "Мир без правок");
  CHECK(h->saveTo(dir2));
  io::LoadResult r2 = io::load(dir2);
  CHECK(!r2.world.ownMapObjects());
}
