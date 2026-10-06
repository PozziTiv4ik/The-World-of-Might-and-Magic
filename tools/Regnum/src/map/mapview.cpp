// Regnum — отрисовка карты: камера, мир и инвалидация, композиция кадра, попадание мышью.
#include "map/mapview.h"

#include <unordered_set>

#include "base/fs.h"
#include "base/jobs.h"
#include "codec/png.h"
#include "map/art_scene.h"
#include "map/map_internal.h"

namespace rg::map {

using namespace detail;

namespace {

constexpr double kAnimSec = 0.18;
constexpr size_t kTileBudget = size_t(256) << 20;
constexpr double kSeaMargin = 10;        // мягкий край моря у берега (3 σ), единицы карты: столько тайлов вокруг правки берега
constexpr double kPreviewDelay = 0.3;    // превью карты мира перерисовывается, когда правка затихла на столько секунд

// Дуга, правка которой меняет сушу или море (берег, рамка; граница с разным рельефом по сторонам).
bool coastEdge(const Edge& e) { return e.kind != EdgeKind::Border || (e.tl == Terrain::Land) != (e.tr == Terrain::Land); }

bool coastChanged(const World& before, const World& after) {
  bool r = false;
  before.edges.diff(after.edges, [&](Id id) {
    if (r) return;
    const Edge* a = before.edges.get(id);
    const Edge* b = after.edges.get(id);
    r = (a && coastEdge(*a)) || (b && coastEdge(*b));
  });
  if (r || before.nodes.same(after.nodes)) return r;
  std::unordered_set<Id> moved;
  before.nodes.diff(after.nodes, [&](Id id) { moved.insert(id); });
  after.edges.each([&](const Edge& e) { r = r || (coastEdge(e) && (moved.count(e.a) || moved.count(e.b))); });
  return r;
}

bool sameSymbol(const MapSymbol& a, const MapSymbol& b) { return a.kind == b.kind && a.p == b.p && a.s == b.s && a.v == b.v && a.z == b.z; }
bool sameShape(const MapShape& a, const MapShape& b) {
  return a.kind == b.kind && a.pts == b.pts && a.holes == b.holes && a.w == b.w && a.dash == b.dash;
}

double easeOut(double t) {
  t = clamp(t, 0.0, 1.0);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

Box2 edgeBox(const World& w, const Edge& e) {
  Box2 b;
  if (const Node* a = w.nodes.get(e.a)) b.add(a->p);
  if (const Node* c = w.nodes.get(e.b)) b.add(c->p);
  for (Vec2 p : e.pts) b.add(p);
  return b;
}

}  // namespace

// ================================================================ состояние
struct MapView::Impl {
  const Basemap* bm = nullptr;
  World world;
  u64 gen = 0;
  View v;

  // анимация камеры
  bool anim = false, pending = false;
  double t0 = 0, now = 0;
  double fx = 0, fy = 0, fz = 1, tx = 0, ty = 0, tz = 1;
  bool anchored = false;
  float ax = 0, ay = 0;
  Vec2 amap;

  // тайлы
  TileStore store;
  TileStyle style;
  bool styleSet = false;
  std::shared_ptr<const Looks> looks;
  std::shared_ptr<const TileScene> scene;
  u64 frame = 0;
  std::function<void()> wake;

  // сцена карты (суша, воды, знаки — нарисованные кодом по миру)
  std::shared_ptr<const art::Scene> art;
  u64 artVersion = 0;
  double artChanged = 0;       // время последней смены сцены

  // палитра (меняется только под previewMu: фоновое превью сверяет с ней свою)
  Palette palette = Palette::Source;

  // карта целиком (запасной слой, мини-карта): превью и миниатюра сцены рисуются в фоне
  std::shared_ptr<const gfx::Image> thumb;   // миниатюра базовой карты в палитре (пока нет миниатюры мира)
  std::mutex previewMu;
  std::shared_ptr<const gfx::Image> preview, worldThumb;
  u64 previewFor = 0;          // версия сцены, по которой нарисованы preview и worldThumb
  Palette previewPal = Palette::Source;   // и палитра
  bool previewBusy = false;
  std::atomic<bool> previewArrived{false};

  LabelCache labels;
  bool labelsPending = false;
  RenderStats stats;

  // свободная часть области просмотра между панелями (пустая — вся область)
  RectF safe;

  // отметки войск и флота: раскладка пересчитывается при смене мира, масштаба, выделения и скрытых объектов
  Id lastSel = 0;
  std::vector<Id> lastHidden;   // по возрастанию
  mutable MarkLayout marks;
  mutable bool marksValid = false;
  mutable u64 marksGen = 0;
  mutable double marksZoom = 0;
  mutable Id marksSel = 0;
  mutable std::vector<Id> marksHidden;

  // мини-карта
  gfx::Image miniTint;
  const gfx::Image* miniThumb = nullptr;   // миниатюра, по которой собран miniTint
  Palette miniPal = Palette::Source;       // палитра miniTint
  u64 miniVer = 0;              // растёт при каждой пересборке miniTint
  gfx::Image miniDev;           // miniTint в пикселях устройства со скруглёнными углами
  u64 miniDevVer = ~u64(0);
  float miniDevDpi = 0;
  u64 miniGen = ~u64(0);
  std::shared_ptr<const Looks> miniLooks;
  Table<Node> miniNodes;
  Table<Edge> miniEdges;

  explicit Impl(const Basemap* b) : bm(b && b->loaded() ? b : nullptr), store(bm, kTileBudget) {}

  double mapW() const { return bm ? bm->width() : schema::kMapWidth; }
  double mapH() const { return bm ? bm->height() : schema::kMapHeight; }
  art::Style colors() const { return mapStyle(bm, palette); }

  // ---------------------------------------------------------------- камера
  // Свободная часть области просмотра (между панелями); слишком маленькая или не заданная — вся область.
  RectF fitArea() const {
    if (safe.empty()) return v.viewport;
    const RectF r = safe.intersect(v.viewport);
    return r.w < 40 || r.h < 40 ? v.viewport : r;
  }
  static double fitZoomIn(const RectF& area, Box2 b, float margin) {
    const double vw = std::max(1.0, double(area.w) - 2 * margin), vh = std::max(1.0, double(area.h) - 2 * margin);
    return std::min(vw / std::max(1.0, b.w()), vh / std::max(1.0, b.h()));
  }
  // Весь мир помещается в свободную часть (а значит, и во всю область просмотра).
  double minZoom() const { return std::max(1e-4, fitZoomIn(fitArea(), Box2(0, 0, mapW(), mapH()), 16)); }
  double maxZoom() const { return std::max(minZoom(), 3.0); }
  // Центр камеры, при котором точка карты p видна в центре свободной части при масштабе z.
  Vec2 centerFor(Vec2 p, double z) const {
    const RectF a = fitArea();
    return {p.x - (double(a.cx()) - double(v.viewport.cx())) / z, p.y - (double(a.cy()) - double(v.viewport.cy())) / z};
  }

  // Центр ограничен так, что край карты может зайти не дальше 40 % области просмотра или 40 % свободной части
  // (место под панели; берётся более свободное из двух ограничений).
  void clampCenter(double& cx, double& cy, double z) const {
    auto range = [](double size, double vc, double r0, double r1, double zz, double& lo, double& hi) {
      const double m = 0.4 * (r1 - r0);
      lo = (vc - r0 - m) / zz;          // ближний край карты — не дальше r0 + m от начала области
      hi = size + (vc - r1 + m) / zz;   // дальний край — не ближе r1 - m
      if (lo > hi) std::swap(lo, hi);
    };
    auto axis = [&](double c, double size, double vc, double v0, double v1, double s0, double s1) {
      double lo, hi, lo2, hi2;
      range(size, vc, v0, v1, z, lo, hi);
      range(size, vc, s0, s1, z, lo2, hi2);
      return clamp(c, std::min(lo, lo2), std::max(hi, hi2));
    };
    const RectF& vp = v.viewport;
    const RectF a = fitArea();
    cx = axis(cx, mapW(), vp.cx(), vp.x, vp.right(), a.x, a.right());
    cy = axis(cy, mapH(), vp.cy(), vp.y, vp.bottom(), a.y, a.bottom());
  }

  const MarkLayout& markLayout() const {
    if (!marksValid || marksGen != gen || marksZoom != v.zoom || marksSel != lastSel || marksHidden != lastHidden) {
      std::vector<const Army*> list;
      list.reserve(world.armies.size());
      // «Скрыть войска» (ТЗ «Фиксы», п.2): ни фигурок, ни попадания мышью.
      if (world.settings->showArmies)
        world.armies.each([&](const Army& a) {
          if (!std::binary_search(lastHidden.begin(), lastHidden.end(), a.id)) list.push_back(&a);
        });
      marks = layoutMarks(list, v.zoom, lastSel, figureSizeOf(*world.settings));
      marksValid = true;
      marksGen = gen;
      marksZoom = v.zoom;
      marksSel = lastSel;
      marksHidden = lastHidden;
    }
    return marks;
  }
  ArmyMark publicMark(const MarkLayout::Mark& m) const {
    ArmyMark r;
    r.top = m.members.front();
    r.members = m.members;
    r.pos = m.pos;
    r.units = m.units;
    const gfx::Pt s = v.toScreen(m.pos);
    r.bounds = RectF(s.x + m.rel.x, s.y + m.rel.y, m.rel.w, m.rel.h);
    return r;
  }
  void startAnim(double cx, double cy, double z) {
    fx = v.cx;
    fy = v.cy;
    fz = v.zoom;
    tx = cx;
    ty = cy;
    tz = z;
    anim = true;
    pending = true;
  }
  void apply(double cx, double cy, double z) {
    v.cx = cx;
    v.cy = cy;
    v.zoom = z;
  }
  void target(double& cx, double& cy, double& z) const {
    if (anim) { cx = tx; cy = ty; z = tz; }
    else { cx = v.cx; cy = v.cy; z = v.zoom; }
  }
  void step(double t) {
    now = t;
    if (!anim) return;
    if (pending) {
      t0 = t;
      pending = false;
      return;
    }
    const double k = (t - t0) / kAnimSec;
    if (k >= 1) {
      anim = false;
      apply(tx, ty, tz);
      return;
    }
    const double e = easeOut(k);
    const double z = std::exp(std::log(fz) + (std::log(tz) - std::log(fz)) * e);
    if (anchored) {
      apply(amap.x - (double(ax) - double(v.viewport.cx())) / z, amap.y - (double(ay) - double(v.viewport.cy())) / z, z);
    } else {
      // при смене масштаба центр движется согласованно с 1/z (без «дуги»), иначе — по кривой замедления
      const double den = 1 / tz - 1 / fz;
      const double kk = std::fabs(den) > 1e-12 ? clamp((1 / z - 1 / fz) / den, 0.0, 1.0) : e;
      apply(fx + (tx - fx) * kk, fy + (ty - fy) * kk, z);
    }
  }

  // ---------------------------------------------------------------- мир и стиль
  void rebuildScene() {
    auto sc = std::make_shared<TileScene>();
    sc->world = world;
    sc->style = style;
    sc->styleKey = style.key();
    sc->looks = looks;
    sc->art = art;
    sc->gen = gen;
    scene = sc;
    store.setScene(sc);
  }

  // Сцена карты по миру: перестраивается, когда меняются знаки и фигуры мира или берег. Габариты изменённых
  // объектов добавляются в dirty (all — перерисовать всё).
  void updateArt(const World& before, const World& after, u32 what, std::vector<Box2>& dirty, bool& all) {
    const bool ownB = before.ownMapObjects(), ownA = after.ownMapObjects();
    const bool objs = !art || (what & TB_MAPART) || ownB != ownA;
    const bool coast = !art || ((what & TB_GEO) && coastChanged(before, after));
    if (!objs && !coast) return;
    art = art::Scene::build(after, bm ? &bm->art() : nullptr, bm ? &bm->objects() : nullptr, coast ? nullptr : art->land());
    artVersion++;
    artChanged = now;
    if (all || !objs) return;
    auto sym = [&](const MapSymbol* s) {
      if (s) dirty.push_back(art::symbolBox(*s));
    };
    auto shp = [&](const MapShape* s) {
      if (s) dirty.push_back(art::shapeBox(*s));
    };
    if (ownB == ownA) {
      if (!ownA) return;   // оба — объекты базовой карты
      before.symbols.diff(after.symbols, [&](Id id) { sym(before.symbol(id)); sym(after.symbol(id)); });
      before.shapes.diff(after.shapes, [&](Id id) { shp(before.shape(id)); shp(after.shape(id)); });
      return;
    }
    // Перенос объектов базовой карты в мир (или его отмена): перерисовать только то, что отличается от них.
    if (!bm) {
      all = true;
      return;
    }
    const World& own = ownA ? after : before;
    const art::Objects& base = bm->objects();
    std::unordered_set<Id> seen;
    for (const MapSymbol& b : base.symbols) {
      seen.insert(b.id);
      const MapSymbol* x = own.symbol(b.id);
      if (x && sameSymbol(*x, b)) continue;
      dirty.push_back(art::symbolBox(b));
      sym(x);
    }
    own.symbols.each([&](const MapSymbol& x) {
      if (!seen.count(x.id)) sym(&x);
    });
    seen.clear();
    for (const MapShape& b : base.shapes) {
      seen.insert(b.id);
      const MapShape* x = own.shape(b.id);
      if (x && sameShape(*x, b)) continue;
      dirty.push_back(art::shapeBox(b));
      shp(x);
    }
    own.shapes.each([&](const MapShape& x) {
      if (!seen.count(x.id)) shp(&x);
    });
  }

  // Превью и миниатюра карты мира: по сцене, в фоне; после правки — когда она затихнет; после смены палитры — сразу.
  bool previewDue() {
    std::lock_guard<std::mutex> lk(previewMu);
    return art && (previewFor != artVersion || previewPal != palette) && !previewBusy;
  }
  void ensurePreview() {
    {
      std::lock_guard<std::mutex> lk(previewMu);
      if (!art || (previewFor == artVersion && previewPal == palette) || previewBusy) return;
      if (previewFor != 0 && previewFor != artVersion && now - artChanged < kPreviewDelay) return;
      previewBusy = true;
    }
    if (previewJob.valid()) previewJob.wait();
    auto sc = art;
    const u64 ver = artVersion;
    const Palette pal = palette;
    auto self = this;
    // Impl живёт дольше задачи: деструктор ждёт её (см. ~Impl).
    previewJob = jobs::submit([self, sc, ver, pal] {
      std::shared_ptr<const gfx::Image> pv, th;
      try {
        pv = std::make_shared<gfx::Image>(art::renderScene(*sc, 2000, pal));
        th = std::make_shared<gfx::Image>(art::renderScene(*sc, 480, pal));
      } catch (const std::exception& e) {
        logError("Превью карты не нарисовано: %s", e.what());
      }
      std::function<void()> w;
      {
        std::lock_guard<std::mutex> lk(self->previewMu);
        // Палитра сменилась, пока рисовалось, — изображения не нужны (следующее превью — в новой).
        if (pal == self->palette) {
          if (pv) self->preview = pv;
          if (th) self->worldThumb = th;
        }
        self->previewFor = ver;
        self->previewPal = pal;
        self->previewBusy = false;
        w = self->wake;
      }
      self->previewArrived = true;
      if (w) w();
    });
  }

  void setStyle(const TileStyle& s) {
    if (styleSet && s == style) return;
    style = s;
    styleSet = true;
    looks = computeLooks(world, style);
    rebuildScene();
  }

  void applyWorld(const World& after) {
    const World before = world;
    const u32 what = World::diff(before, after);
    world = after;
    if (!what && gen) return;
    gen++;
    if (!styleSet) {
      style.fillOpacity = world.settings->fillOpacity;
      styleSet = true;
    }
    // Прозрачность заливки — часть стиля тайлов.
    if (world.settings->fillOpacity != style.fillOpacity) style.fillOpacity = world.settings->fillOpacity;
    auto oldLooks = looks;
    looks = computeLooks(world, style);

    std::vector<Box2> dirty;
    bool all = !oldLooks || gen == 1;
    const bool coast = (what & TB_GEO) && coastChanged(before, after);
    updateArt(before, after, what, dirty, all);
    const size_t artDirty = dirty.size();
    if (!all && (what & TB_GEO)) {
      std::unordered_set<Id> nodes;
      before.nodes.diff(after.nodes, [&](Id id) { nodes.insert(id); });
      before.edges.diff(after.edges, [&](Id id) {
        if (const Edge* e = before.edges.get(id)) dirty.push_back(edgeBox(before, *e));
        if (const Edge* e = after.edges.get(id)) dirty.push_back(edgeBox(after, *e));
      });
      if (!nodes.empty()) {
        auto touch = [&](const World& w) {
          w.edges.each([&](const Edge& e) {
            if (nodes.count(e.a) || nodes.count(e.b)) dirty.push_back(edgeBox(w, e));
          });
        };
        touch(before);
        touch(after);
      }
      // Правка берега меняет и мягкий край моря вокруг.
      if (coast)
        for (size_t i = artDirty; i < dirty.size(); i++) dirty[i] = dirty[i].inflated(kSeaMargin);
    }
    if (!all) {
      if (oldLooks->fillAlpha != looks->fillAlpha) all = true;
      auto fsA = geo::faces(after);
      auto fsB = (what & TB_GEO) ? geo::faces(before) : fsA;
      auto addProv = [&](Id pid) {
        if (const geo::ProvinceShape* s = fsA->shape(pid)) dirty.push_back(s->box);
        if (fsB != fsA)
          if (const geo::ProvinceShape* s = fsB->shape(pid)) dirty.push_back(s->box);
      };
      std::unordered_set<Id> lineChanged;
      for (const auto& [sid, col] : looks->stateLine) {
        auto it = oldLooks->stateLine.find(sid);
        if (it == oldLooks->stateLine.end() || !(it->second == col)) lineChanged.insert(sid);
      }
      for (const auto& [sid, col] : oldLooks->stateLine)
        if (!looks->stateLine.count(sid)) lineChanged.insert(sid);
      for (const auto& [pid, look] : looks->prov) {
        auto it = oldLooks->prov.find(pid);
        if (it == oldLooks->prov.end() || !(it->second == look) || lineChanged.count(look.state)) addProv(pid);
      }
      for (const auto& [pid, look] : oldLooks->prov)
        if (!looks->prov.count(pid) || lineChanged.count(look.state)) addProv(pid);
      if (dirty.size() > 4000) all = true;
    }
    store.invalidate(dirty, gen, style.key(), all);
    rebuildScene();
  }

  // ---------------------------------------------------------------- превью (асинхронно)
  std::shared_ptr<const gfx::Image> backdrop() {
    ensurePreview();
    std::lock_guard<std::mutex> lk(previewMu);
    return preview ? preview : thumb;
  }
  std::future<void> previewJob;

  ~Impl() {
    if (previewJob.valid()) {
      try {
        previewJob.wait();
      } catch (...) {
      }
    }
  }

  // ---------------------------------------------------------------- кадр
  void drawTiles(gfx::Image& target, const gfx::RectI& clip, double X0, double Y0, double ds, bool idle);
};

// ================================================================ составные тайлы в кадре
void MapView::Impl::drawTiles(gfx::Image& target, const gfx::RectI& clipIn, double X0, double Y0, double ds, bool idle) {
  const int T = kTile;
  const u64 sk = style.key();
  // Область карты на устройстве.
  const gfx::RectI mapDev(int(X0), int(Y0), int(std::lround(X0 + mapW() * ds)) - int(X0), int(std::lround(Y0 + mapH() * ds)) - int(Y0));
  const gfx::RectI clip = clipIn.intersect(mapDev);
  if (clip.empty()) return;

  struct Op {
    std::shared_ptr<const gfx::Image> img;
    double x0, y0, x1, y1;
    gfx::RectI clip;
    bool copy;   // точный масштаб: копирование
  };
  std::vector<Op> ops;
  std::vector<TileRequest> reqs;
  std::vector<TileKey> used;

  // 1. Тайлы текущего масштаба (в покое).
  const int cols = int(std::ceil(mapW() * ds / T)), rows = int(std::ceil(mapH() * ds / T));
  const int tx0 = std::max(0, int(std::floor((clip.x - X0) / T))), tx1 = std::min(cols - 1, int(std::floor((clip.right() - 1 - X0) / T)));
  const int ty0 = std::max(0, int(std::floor((clip.y - Y0) / T))), ty1 = std::min(rows - 1, int(std::floor((clip.bottom() - 1 - Y0) / T)));
  std::vector<gfx::RectI> missing;
  if (idle) {
    const double cxs = (clip.x + clip.w * 0.5 - X0) / T, cys = (clip.y + clip.h * 0.5 - Y0) / T;
    for (int ty = ty0; ty <= ty1; ty++)
      for (int tx = tx0; tx <= tx1; tx++) {
        const TileKey k{sk, ds, tx, ty};
        const gfx::RectI r = gfx::RectI(int(X0) + tx * T, int(Y0) + ty * T, T, T).intersect(clip);
        reqs.push_back({k, std::hypot(tx + 0.5 - cxs, ty + 0.5 - cys)});
        used.push_back(k);
        TileView tv;
        if (store.get(k, tv)) ops.push_back({tv.img, X0 + tx * T, Y0 + ty * T, X0 + (tx + 1) * T, Y0 + (ty + 1) * T, r, true});
        else missing.push_back(r);
      }
    // Кольцо вокруг видимой области — заранее (для панорамы).
    for (int ty = ty0 - 1; ty <= ty1 + 1; ty++)
      for (int tx = tx0 - 1; tx <= tx1 + 1; tx++) {
        if (tx < 0 || ty < 0 || tx >= cols || ty >= rows) continue;
        if (tx >= tx0 && tx <= tx1 && ty >= ty0 && ty <= ty1) continue;
        reqs.push_back({TileKey{sk, ds, tx, ty}, 100 + std::hypot(tx + 0.5 - cxs, ty + 0.5 - cys)});
      }
  } else {
    missing.push_back(clip);
    // Во время анимации — тайлы конечного масштаба для конечной области.
    if (anim) {
      const double tds = tz * double(v.dpi);
      const double tX0 = std::round(double(v.viewport.cx()) * v.dpi - tx * tds), tY0 = std::round(double(v.viewport.cy()) * v.dpi - ty * tds);
      const int tc = int(std::ceil(mapW() * tds / T)), tr = int(std::ceil(mapH() * tds / T));
      const double vx0 = v.viewport.x * v.dpi, vy0 = v.viewport.y * v.dpi, vx1 = v.viewport.right() * v.dpi, vy1 = v.viewport.bottom() * v.dpi;
      const int a0 = std::max(0, int(std::floor((vx0 - tX0) / T))), a1 = std::min(tc - 1, int(std::floor((vx1 - 1 - tX0) / T)));
      const int b0 = std::max(0, int(std::floor((vy0 - tY0) / T))), b1 = std::min(tr - 1, int(std::floor((vy1 - 1 - tY0) / T)));
      const double cxs = ((vx0 + vx1) * 0.5 - tX0) / T, cys = ((vy0 + vy1) * 0.5 - tY0) / T;
      for (int y = b0; y <= b1; y++)
        for (int x = a0; x <= a1; x++) reqs.push_back({TileKey{sk, tds, x, y}, std::hypot(x + 0.5 - cxs, y + 0.5 - cys)});
    }
  }
  // Опорный уровень (вся карта) — всегда, после видимого.
  const int bc = int(std::ceil(mapW() * kBaseScale / T)), br = int(std::ceil(mapH() * kBaseScale / T));
  for (int y = 0; y < br; y++)
    for (int x = 0; x < bc; x++) {
      reqs.push_back({TileKey{sk, kBaseScale, x, y}, double(1000 + y * bc + x)});
      used.push_back(TileKey{sk, kBaseScale, x, y});
    }
  store.request(reqs, frame);

  // 2. Запасные слои для недостающих тайлов.
  stats.fallback = !missing.empty();
  std::vector<Op> back;
  if (!missing.empty()) {
    gfx::RectI need;
    for (const gfx::RectI& r : missing) need = need.unite(r);
    const Box2 nb((need.x - X0) / ds, (need.y - Y0) / ds, (need.right() - X0) / ds, (need.bottom() - Y0) / ds);
    std::vector<TileView> cand;
    store.available(0, nb, cand);
    // Группы по (стиль, масштаб); ищем лучшую, покрывающую всю нужную область.
    struct Group { u64 style; double ds; std::vector<TileView> tiles; double score; bool covers; };
    std::vector<Group> groups;
    for (TileView& t : cand) {
      if (idle && t.key.style == sk && t.key.ds == ds) continue;
      auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.style == t.key.style && g.ds == t.key.ds; });
      if (it == groups.end()) { groups.push_back({t.key.style, t.key.ds, {}, 0, false}); it = groups.end() - 1; }
      it->tiles.push_back(std::move(t));
    }
    for (Group& g : groups) {
      const double lr = std::log2(g.ds / ds);
      g.score = (g.style == sk ? 0 : 10) + (lr >= 0 ? lr * 0.6 : -lr) ;   // чем меньше, тем лучше; мельче — предпочтительнее
      // покрытие: все тайлы группы, пересекающие nb, на месте
      const int gc = int(std::ceil(mapW() * g.ds / T)), gr = int(std::ceil(mapH() * g.ds / T));
      const int a0 = std::max(0, int(std::floor(nb.x0 * g.ds / T))), a1 = std::min(gc - 1, int(std::floor(nb.x1 * g.ds / T - 1e-9)));
      const int b0 = std::max(0, int(std::floor(nb.y0 * g.ds / T))), b1 = std::min(gr - 1, int(std::floor(nb.y1 * g.ds / T - 1e-9)));
      int have = 0;
      for (const TileView& t : g.tiles)
        if (t.key.tx >= a0 && t.key.tx <= a1 && t.key.ty >= b0 && t.key.ty <= b1) have++;
      g.covers = have >= (a1 - a0 + 1) * (b1 - b0 + 1);
    }
    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.score > b.score; });  // худшие первыми
    size_t firstCover = groups.size();
    for (size_t i = groups.size(); i-- > 0;)
      if (groups[i].covers) { firstCover = i; break; }
    // Лучшее полное покрытие и всё, что лучше него, поверх.
    const size_t from = firstCover < groups.size() ? firstCover : 0;
    if (firstCover >= groups.size()) {
      if (auto bd = backdrop())
        for (const gfx::RectI& r : missing) back.push_back({bd, X0, Y0, X0 + mapW() * ds, Y0 + mapH() * ds, r, false});
    }
    for (size_t i = from; i < groups.size(); i++)
      for (const TileView& t : groups[i].tiles) {
        const double k = ds / t.key.ds;
        const double x0 = X0 + t.key.tx * T * k, y0 = Y0 + t.key.ty * T * k;
        for (const gfx::RectI& r : missing) {
          const gfx::RectI rr = r.intersect(gfx::RectI(int(std::floor(x0)), int(std::floor(y0)), int(std::ceil(T * k)) + 2, int(std::ceil(T * k)) + 2));
          if (!rr.empty()) back.push_back({t.img, x0, y0, x0 + T * k, y0 + T * k, rr, false});
        }
      }
    if (back.empty())
      for (const gfx::RectI& r : missing) {
        // Ничего нет: суша цветом палитры.
        gfx::Image one(1, 1, gfx::premul(colors().land));
        back.push_back({std::make_shared<gfx::Image>(one), double(r.x), double(r.y), double(r.right()), double(r.bottom()), r, false});
      }
  }
  store.touch(used, frame);

  // 3. Исполнение: запасные (масштабирование) — полосами в пуле, точные — копированием.
  std::vector<Op> all;
  all.reserve(back.size() + ops.size());
  for (Op& o : back) all.push_back(std::move(o));
  for (Op& o : ops) all.push_back(std::move(o));
  stats.tilesDrawn = int(ops.size());
  stats.fallbackOps = int(back.size());
  if (all.empty()) return;
  const bool heavy = !back.empty();
  const int bands = heavy ? std::max(1, std::min(16, clip.h / 48)) : std::max(1, std::min(8, clip.h / 128));
  auto run = [&](size_t bi) {
    const int y0 = clip.y + int(clip.h * bi / size_t(bands)), y1 = clip.y + int(clip.h * (bi + 1) / size_t(bands));
    const gfx::RectI band(clip.x, y0, clip.w, y1 - y0);
    for (const Op& o : all) {
      const gfx::RectI rc = o.clip.intersect(band);
      if (rc.empty()) continue;
      if (o.copy) copyImage(target, *o.img, int(o.x0), int(o.y0), rc);
      else scaleImage(target, *o.img, o.x0, o.y0, o.x1, o.y1, rc, true);
    }
  };
  if (bands == 1) run(0);
  else jobs::parallelFor(size_t(bands), run, 1);
}

// ================================================================ MapView
MapView::MapView(const Basemap* basemap) : d_(std::make_unique<Impl>(basemap)) {
  d_->v.cx = d_->mapW() / 2;
  d_->v.cy = d_->mapH() / 2;
}

MapView::~MapView() = default;

void MapView::setWorld(const World& w) { d_->applyWorld(w); }
void MapView::worldChanged(const World&, const World& after, u32) { d_->applyWorld(after); }

void MapView::setPalette(Palette p) {
  Impl& d = *d_;
  if (p >= Palette::Count || p == d.palette) return;
  {
    std::lock_guard<std::mutex> lk(d.previewMu);
    d.palette = p;
    // Превью и миниатюра мира — в прежней палитре: до новых (ensurePreview) запасной слой и мини-карта берут
    // миниатюру базовой карты в новой. Тайлы — другого стиля (TileStyle::palette, см. render).
    d.preview = nullptr;
    d.worldThumb = nullptr;
  }
  d.thumb = nullptr;
}

Palette MapView::palette() const { return d_->palette; }

void MapView::setWakeCallback(std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lk(d_->previewMu);
    d_->wake = fn;
  }
  d_->store.setWake(std::move(fn));
}

void MapView::setViewport(RectF logical, float dpi) {
  const bool first = d_->v.viewport.empty();
  const bool resized = first || logical.w != d_->v.viewport.w || logical.h != d_->v.viewport.h;
  d_->v.viewport = logical;
  d_->v.dpi = dpi > 0 ? dpi : 1;
  if (logical.empty()) return;
  if (first) {
    d_->v.zoom = d_->minZoom();
    d_->v.cx = d_->mapW() / 2;
    d_->v.cy = d_->mapH() / 2;
  }
  // Пределы камеры проверяются при смене размера окна; смена свободной части (открылась или закрылась панель)
  // камеру не дёргает.
  if (!resized) return;
  double z = clamp(d_->v.zoom, d_->minZoom(), d_->maxZoom()), cx = d_->v.cx, cy = d_->v.cy;
  d_->clampCenter(cx, cy, z);
  d_->apply(cx, cy, z);
  if (d_->anim) {
    d_->tz = clamp(d_->tz, d_->minZoom(), d_->maxZoom());
    d_->clampCenter(d_->tx, d_->ty, d_->tz);
  }
}

void MapView::setSafeArea(RectF logical) { d_->safe = logical; }
RectF MapView::safeArea() const { return d_->safe; }

const View& MapView::view() const { return d_->v; }

void MapView::zoomAt(float sx, float sy, double factor, bool animate) {
  if (!(factor > 0) || d_->v.viewport.empty()) return;
  double cx, cy, z;
  d_->target(cx, cy, z);
  // Если текущий масштаб меньше minZoom (свободная часть выросла), отдаление его не увеличивает.
  const double nz = clamp(z * factor, std::min(d_->minZoom(), z), d_->maxZoom());
  // Точка под курсором — по видимому состоянию (при серии щелчков колеса цель накапливается).
  const bool sameAnchor = d_->anim && d_->anchored && d_->ax == sx && d_->ay == sy;
  const Vec2 m = sameAnchor ? d_->amap : d_->v.toMap(sx, sy);
  double ncx = m.x - (double(sx) - double(d_->v.viewport.cx())) / nz, ncy = m.y - (double(sy) - double(d_->v.viewport.cy())) / nz;
  const double rx = ncx, ry = ncy;
  d_->clampCenter(ncx, ncy, nz);
  if (!animate) {
    d_->anim = false;
    d_->apply(ncx, ncy, nz);
    return;
  }
  if (sameAnchor) {
    // продолжить текущую анимацию к новой цели
    d_->fx = d_->v.cx; d_->fy = d_->v.cy; d_->fz = d_->v.zoom;
    d_->tx = ncx; d_->ty = ncy; d_->tz = nz;
    d_->pending = true;
  } else {
    d_->startAnim(ncx, ncy, nz);
  }
  d_->anchored = std::fabs(rx - ncx) < 1e-9 && std::fabs(ry - ncy) < 1e-9;
  d_->ax = sx;
  d_->ay = sy;
  d_->amap = m;
}

void MapView::panBy(float dx, float dy) {
  if (d_->v.viewport.empty()) return;
  double cx = d_->v.cx - dx / d_->v.zoom, cy = d_->v.cy - dy / d_->v.zoom;
  d_->clampCenter(cx, cy, d_->v.zoom);
  const double mx = cx - d_->v.cx, my = cy - d_->v.cy;
  d_->apply(cx, cy, d_->v.zoom);
  if (d_->anim) {  // панорама во время анимации сдвигает и цель
    d_->fx += mx; d_->fy += my;
    d_->tx += mx; d_->ty += my;
    d_->amap = d_->amap + Vec2(mx, my);
    d_->clampCenter(d_->tx, d_->ty, d_->tz);
  }
}

void MapView::centerOn(Vec2 p, double zoom, bool animate) {
  const double z = zoom > 0 ? clamp(zoom, d_->minZoom(), d_->maxZoom()) : clamp(d_->anim ? d_->tz : d_->v.zoom, d_->minZoom(), d_->maxZoom());
  double cx = p.x, cy = p.y;
  d_->clampCenter(cx, cy, z);
  if (!animate) {
    d_->anim = false;
    d_->apply(cx, cy, z);
    return;
  }
  d_->startAnim(cx, cy, z);
  d_->anchored = false;
}

void MapView::fit(Box2 box, bool animate) {
  if (box.empty()) return;
  const double minSide = 60;
  if (box.w() < minSide || box.h() < minSide) {
    const Vec2 c = box.center();
    box = Box2(c.x - std::max(box.w(), minSide) / 2, c.y - std::max(box.h(), minSide) / 2, c.x + std::max(box.w(), minSide) / 2,
               c.y + std::max(box.h(), minSide) / 2);
  }
  const double z = clamp(Impl::fitZoomIn(d_->fitArea(), box, 48), d_->minZoom(), d_->maxZoom());
  centerOn(d_->centerFor(box.center(), z), z, animate);
}

void MapView::fitAll(bool animate) {
  const double z = d_->minZoom();
  centerOn(d_->centerFor(Vec2(d_->mapW() / 2, d_->mapH() / 2), z), z, animate);
}
double MapView::minZoom() const { return d_->minZoom(); }
double MapView::maxZoom() const { return d_->maxZoom(); }
void MapView::update(double t) { d_->step(t); }
bool MapView::animating() const { return d_->anim; }

bool MapView::needsRedraw() const {
  if (d_->previewArrived.load() || d_->labelsPending || d_->previewDue()) return true;
  if (d_->store.arrived()) return true;
  bool wakeSet;
  {
    std::lock_guard<std::mutex> lk(d_->previewMu);
    wakeSet = bool(d_->wake);
  }
  return !wakeSet && d_->store.busy();
}

std::shared_ptr<const art::Scene> MapView::artScene() const { return d_->art; }

std::shared_ptr<const gfx::Image> MapView::mapThumbnail() const {
  {
    std::lock_guard<std::mutex> lk(d_->previewMu);
    if (d_->worldThumb) return d_->worldThumb;
  }
  return d_->bm ? d_->bm->thumb(d_->palette) : nullptr;
}

bool MapView::loading() const { return d_->labelsPending || d_->store.busy(); }
bool MapView::tilesBusy() const { return d_->store.busy(); }
bool MapView::waitIdle(double timeoutSec) { return d_->store.wait(timeoutSec); }
const RenderStats& MapView::stats() const { return d_->stats; }

void MapView::render(gfx::Canvas& c, const RenderOptions& opt) {
  Impl& d = *d_;
  const double t0 = nowSeconds();
  d.frame++;
  d.previewArrived = false;
  d.store.takeArrived();
  d.ensurePreview();
  const View& v = d.v;
  if (v.viewport.empty()) return;
  TileStyle st;
  st.mode = opt.mode;
  st.editBorders = opt.editBorders;
  st.fillOpacity = d.world.settings->fillOpacity;
  st.dpi = v.dpi;
  st.palette = d.palette;
  d.setStyle(st);

  const float dpi = v.dpi;
  const double ds = v.zoom * dpi;
  // Холст — в пикселях устройства; сдвиг его преобразования учитывается, прочее игнорируется.
  const gfx::Affine base = c.transform();
  const bool shift = base.isTranslateScale() && base.a == 1 && base.d == 1;
  const double bx = shift ? std::round(base.e) : 0, by = shift ? std::round(base.f) : 0;
  const gfx::RectI vp(int(std::lround(v.viewport.x * dpi + bx)), int(std::lround(v.viewport.y * dpi + by)),
                      int(std::lround(v.viewport.right() * dpi + bx)) - int(std::lround(v.viewport.x * dpi + bx)),
                      int(std::lround(v.viewport.bottom() * dpi + by)) - int(std::lround(v.viewport.y * dpi + by)));
  const gfx::RectI clip = vp.intersect(c.clipBounds()).intersect(gfx::RectI(0, 0, c.target().w, c.target().h));
  if (clip.empty()) return;
  // Начало карты на устройстве — по целому пикселю (тайлы копируются без пересэмплирования).
  const double X0 = std::round(double(v.viewport.cx()) * dpi + bx - v.cx * ds);
  const double Y0 = std::round(double(v.viewport.cy()) * dpi + by - v.cy * ds);

  c.save();
  c.setTransform(gfx::Affine());
  c.clipRect(RectF(float(clip.x), float(clip.y), float(clip.w), float(clip.h)));
  // Фон за пределами карты: по палитре (продолжение моря) или по теме интерфейса.
  const Color outside = d.colors().outside;
  const Color back = outside.a ? outside : opt.background.a ? opt.background : opt.darkUi ? Color::hex(0x0b0e13) : Color::hex(0xe4ded2);
  const gfx::RectI mapDev(int(X0), int(Y0), int(std::lround(X0 + d.mapW() * ds)) - int(X0), int(std::lround(Y0 + d.mapH() * ds)) - int(Y0));
  {
    const gfx::RectI in = clip.intersect(mapDev);
    const u32 bp = gfx::premul(back);
    gfx::Image& img = c.target();
    for (int y = clip.y; y < clip.bottom(); y++) {
      u32* row = img.row(y);
      if (in.empty() || y < in.y || y >= in.bottom()) {
        std::fill(row + clip.x, row + clip.right(), bp);
        continue;
      }
      std::fill(row + clip.x, row + in.x, bp);
      std::fill(row + in.right(), row + clip.right(), bp);
    }
  }
  d.drawTiles(c.target(), clip, X0, Y0, ds, !d.anim);
  // Тонкая рамка края карты.
  if (opt.edgeLine)
    c.strokeRoundRect(RectF(float(mapDev.x) - 0.5f, float(mapDev.y) - 0.5f, float(mapDev.w) + 1, float(mapDev.h) + 1), 0, 1,
                      opt.darkUi ? Color(255, 255, 255, 28) : Color(0, 0, 0, 40));
  const double t1 = nowSeconds();

  // Наложения.
  auto fs = geo::faces(d.world);
  FrameCtx f;
  f.c = &c;
  f.w = &d.world;
  f.fs = fs.get();
  f.opt = &opt;
  f.X0 = X0;
  f.Y0 = Y0;
  f.ds = ds;
  f.dpi = dpi;
  f.zoom = v.zoom;
  f.clip = clip;
  f.visible = Box2((clip.x - X0) / ds, (clip.y - Y0) / ds, (clip.right() - X0) / ds, (clip.bottom() - Y0) / ds);
  d.lastSel = opt.selArmy;
  d.lastHidden = opt.hideArmies;
  std::sort(d.lastHidden.begin(), d.lastHidden.end());
  d.lastHidden.erase(std::unique(d.lastHidden.begin(), d.lastHidden.end()), d.lastHidden.end());
  f.marks = &d.markLayout();
  drawHover(f);
  const bool guilds = opt.mode == schema::MapMode::Guilds;
  const bool capitals = opt.mode != schema::MapMode::Terrain;
  Obstacles obs;
  collectMarkerObstacles(f, obs, capitals, guilds, guilds);
  collectArmyObstacles(f, obs);
  const double tl = nowSeconds();
  // Новые спрайты подписей — в пределах бюджета кадра (при анимации меньше), остальные — в следующих кадрах.
  d.labelsPending = opt.labels && !drawLabels(f, d.labels, obs, figureSize() * dpi, d.anim ? 2.5 : 12.0);
  d.stats.labelsMs = (nowSeconds() - tl) * 1000;
  if (guilds || opt.mode == schema::MapMode::Trade || opt.selRoute) drawRoutes(f);
  if (guilds) drawGuildPies(f, obs);
  drawMarkers(f, obs, capitals, guilds);
  drawArmies(f);
  drawSelection(f);
  c.restore();
  d.labels.trim(600);
  d.store.trim(d.frame, d.style.key());
  d.stats.composeMs = (t1 - t0) * 1000;
  d.stats.frameMs = (nowSeconds() - t0) * 1000;
  d.stats.pending = d.store.pending();
  d.stats.labelsDeferred = d.labelsPending;
}

// ================================================================ мини-карта
void MapView::renderMinimap(gfx::Canvas& c, RectF rect, float dpi) {
  Impl& d = *d_;
  if (rect.empty()) return;
  // Подкрашенная миниатюра: заливка политической карты поверх миниатюры карты мира (до её готовности — базовой
  // карты; умножение — символы остаются тёмными).
  std::shared_ptr<const gfx::Image> thumb;
  {
    std::lock_guard<std::mutex> lk(d.previewMu);
    thumb = d.worldThumb;
  }
  if (!thumb && !d.thumb && d.bm) d.thumb = d.bm->thumb(d.palette);
  if (!thumb) thumb = d.thumb;
  const bool baseChanged = thumb.get() != d.miniThumb || d.miniPal != d.palette;   // подложка: миниатюра или палитра
  // Перерисовывается, только когда меняются геометрия, политические цвета провинций, миниатюра карты или палитра.
  std::shared_ptr<const Looks> looks;
  if (d.miniGen != d.gen || d.miniTint.empty() || baseChanged) {
    d.miniGen = d.gen;
    TileStyle ps;
    ps.mode = schema::MapMode::Political;
    looks = computeLooks(d.world, ps);
    const bool same = !baseChanged && !d.miniTint.empty() && d.miniLooks && d.miniNodes.same(d.world.nodes) &&
                      d.miniEdges.same(d.world.edges) && d.miniLooks->prov == looks->prov;
    if (same) looks = nullptr;
  }
  if (looks) {
    d.miniVer++;
    d.miniThumb = thumb.get();
    d.miniPal = d.palette;
    d.miniLooks = looks;
    d.miniNodes = d.world.nodes;
    d.miniEdges = d.world.edges;
    auto fs = geo::faces(d.world);
    const art::Style colors = d.colors();
    if (thumb) {
      d.miniTint = *thumb;
    } else {
      // Без базовой карты: суша и море по граням цветами палитры.
      d.miniTint = gfx::Image(480, int(std::lround(480 * d.mapH() / d.mapW())), gfx::premul(colors.land));
      gfx::Canvas sc(d.miniTint);
      const double k = d.miniTint.w / d.mapW();
      gfx::Path sea;
      for (const geo::Face& f : fs->faces) {
        if (f.terrain != Terrain::Sea) continue;
        for (const auto& ring : f.rings) {
          for (size_t i = 0; i < ring.size(); i++) {
            if (i == 0) sea.moveTo(float(ring[i].x * k), float(ring[i].y * k));
            else sea.lineTo(float(ring[i].x * k), float(ring[i].y * k));
          }
          sea.close();
        }
      }
      if (fs->faces.empty()) sea.addRect(RectF(0, 0, float(d.miniTint.w), float(d.miniTint.h)));
      sc.fillPath(sea, colors.sea, gfx::FillRule::EvenOdd);
    }
    gfx::Image layer(d.miniTint.w, d.miniTint.h, 0);
    gfx::Canvas lc(layer);
    const double k = d.miniTint.w / d.mapW();
    for (const auto& [pid, sh] : fs->provinces) {
      auto it = looks->prov.find(pid);
      if (!pid || it == looks->prov.end() || !it->second.fill.a) continue;
      gfx::Path p;
      for (int fi : sh.faces)
        for (const auto& ring : fs->faces[size_t(fi)].rings) {
          for (size_t i = 0; i < ring.size(); i++) {
            if (i == 0) p.moveTo(float(ring[i].x * k), float(ring[i].y * k));
            else p.lineTo(float(ring[i].x * k), float(ring[i].y * k));
          }
          p.close();
        }
      lc.fillPath(p, it->second.fill, gfx::FillRule::EvenOdd);
    }
    // умножение с прозрачностью 0,6: d = d · (1 − a + a · c)
    for (size_t i = 0; i < layer.px.size(); i++) {
      const u32 s = layer.px[i];
      const u32 a = ((s >> 24) * 150) / 255;
      if (!a) continue;
      const Color sc = gfx::unpremul(s);
      const u32 dpx = d.miniTint.px[i];
      auto ch = [&](u32 dv, u32 cv) { return (dv * (255 - a) + dv * cv / 255 * a) / 255; };
      const u32 r = ch((dpx >> 16) & 255, sc.r), g = ch((dpx >> 8) & 255, sc.g), b = ch(dpx & 255, sc.b);
      d.miniTint.px[i] = (dpx & 0xFF000000u) | (r << 16) | (g << 8) | b;
    }
  }
  // Подложка в пикселях устройства (уменьшенная миниатюра со скруглёнными углами) пересобирается только при смене
  // миниатюры, размера или масштаба; в кадре — простое наложение.
  const gfx::Affine base = c.transform();
  const gfx::RectI dev(int(std::lround(rect.x * dpi + base.e)), int(std::lround(rect.y * dpi + base.f)), int(std::lround(rect.w * dpi)),
                       int(std::lround(rect.h * dpi)));
  if (!dev.empty() && base.isTranslateScale() && base.a == 1 && base.d == 1) {
    if (d.miniDev.w != dev.w || d.miniDev.h != dev.h || d.miniDevVer != d.miniVer || d.miniDevDpi != dpi) {
      d.miniDev = gfx::Image(dev.w, dev.h, 0);
      gfx::Canvas mc(d.miniDev);
      mc.clipRoundRect(RectF(0, 0, float(dev.w), float(dev.h)), 8 * dpi);
      mc.drawImage(d.miniTint, RectF(0, 0, float(dev.w), float(dev.h)));
      d.miniDevVer = d.miniVer;
      d.miniDevDpi = dpi;
    }
    detail::blendImage(c.target(), d.miniDev, dev.x, dev.y, c.clipBounds());
  }
  c.save();
  c.scale(dpi, dpi);
  c.save();
  c.clipRoundRect(rect, 6);
  if (dev.empty() || !(base.isTranslateScale() && base.a == 1 && base.d == 1)) c.drawImage(d.miniTint, rect);
  // Видимая область.
  const Box2 vb = d.v.visibleBox();
  const double kx = rect.w / d.mapW(), ky = rect.h / d.mapH();
  RectF r(float(rect.x + vb.x0 * kx), float(rect.y + vb.y0 * ky), float(vb.w() * kx), float(vb.h() * ky));
  r = r.intersect(rect.inset(1));
  if (!r.empty()) {
    gfx::Path outside;
    outside.addRect(rect);
    outside.addRect(r);
    c.fillPath(outside, Color(8, 10, 14, 70), gfx::FillRule::EvenOdd);
    c.strokeRoundRect(r.inset(-0.5f), 2, 2.5f, Color(0, 0, 0, 90));
    c.strokeRoundRect(r, 2, 1.5f, Color::hex(0xe8b75c));
  }
  c.restore();
  c.strokeRoundRect(rect.inset(0.5f), 6, 1, Color(255, 255, 255, 40));
  c.restore();
}

Vec2 MapView::minimapToMap(RectF rect, float sx, float sy) const {
  const double x = (double(sx) - rect.x) / std::max(1.f, rect.w) * d_->mapW();
  const double y = (double(sy) - rect.y) / std::max(1.f, rect.h) * d_->mapH();
  return {clamp(x, 0.0, d_->mapW()), clamp(y, 0.0, d_->mapH())};
}

std::vector<LegendItem> MapView::legend(const World& w, schema::MapMode mode) const { return legendFor(w, mode, d_->colors()); }

// ================================================================ попадание
Id MapView::provinceAt(float sx, float sy) const {
  const Vec2 p = d_->v.toMap(sx, sy);
  if (p.x < 0 || p.y < 0 || p.x > d_->mapW() || p.y > d_->mapH()) return 0;
  return geo::faces(d_->world)->provinceAt(p);
}

std::optional<ArmyMark> MapView::markAt(float sx, float sy) const {
  const MarkLayout& L = d_->markLayout();
  // Верхняя — нарисованная последней.
  for (size_t i = L.marks.size(); i-- > 0;) {
    const MarkLayout::Mark& m = L.marks[i];
    const gfx::Pt s = d_->v.toScreen(m.pos);
    const float dx = sx - s.x, dy = sy - s.y;
    if (!m.rel.contains(dx, dy)) continue;   // быстрый отсев по габаритам
    if (markHit(m, L.figure, dx, dy)) return d_->publicMark(m);
  }
  return std::nullopt;
}

Id MapView::armyAt(float sx, float sy) const {
  const std::optional<ArmyMark> m = markAt(sx, sy);
  return m ? m->top : 0;
}

std::vector<ArmyMark> MapView::armyMarks() const {
  const MarkLayout& L = d_->markLayout();
  std::vector<ArmyMark> out;
  out.reserve(L.marks.size());
  for (const MarkLayout::Mark& m : L.marks) out.push_back(d_->publicMark(m));
  return out;
}

double MapView::separateZoom(const ArmyMark& m) const {
  std::vector<const Army*> list;
  for (Id id : m.members)
    if (const Army* a = d_->world.army(id)) list.push_back(a);
  if (list.size() < 2) return 0;
  const double cur = d_->anim ? d_->tz : d_->v.zoom, zmax = d_->maxZoom();
  if (cur >= zmax * (1 - 1e-9)) return 0;
  double z = cur;
  while (z < zmax) {
    z = std::min(z * 1.2, zmax);
    if (layoutMarks(list, z, d_->lastSel, figureSizeOf(*d_->world.settings)).marks.size() == list.size()) return z;
  }
  return zmax;
}

Id MapView::routeAt(float sx, float sy, float tolPx) const {
  Id best = 0;
  double bd = double(tolPx) * tolPx;
  d_->world.routes.each([&](const Route& r) {
    const std::vector<Vec2> pts = routeLine(r.pts);
    for (size_t i = 1; i < pts.size(); i++) {
      const gfx::Pt a = d_->v.toScreen(pts[i - 1]), b = d_->v.toScreen(pts[i]);
      const double abx = b.x - a.x, aby = b.y - a.y, l2 = abx * abx + aby * aby;
      const double t = l2 > 0 ? clamp(((sx - a.x) * abx + (sy - a.y) * aby) / l2, 0.0, 1.0) : 0;
      const double dx = a.x + abx * t - sx, dy = a.y + aby * t - sy;
      if (dx * dx + dy * dy <= bd) {
        bd = dx * dx + dy * dy;
        best = r.id;
      }
    }
  });
  return best;
}

float MapView::figureSize() const { return figureSizeOf(*d_->world.settings); }

}  // namespace rg::map
