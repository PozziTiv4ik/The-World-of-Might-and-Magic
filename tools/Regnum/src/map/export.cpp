// Regnum — экспорт всей карты в изображение (см. export.h).
#include "map/export.h"

#include "map/basemap.h"

namespace rg::map {

struct MapExport::Impl {
  MapView mv;
  RenderOptions opt;
  gfx::Image img;
  int tiles = 1;
  int renders = 0;
  bool done = false;
  explicit Impl(const Basemap* bm) : mv(bm) {}
};

int MapExport::heightFor(int width) { return std::max(1, int(std::lround(double(width) * schema::kMapHeight / schema::kMapWidth))); }

MapExport::MapExport(const Basemap* bm, const World& w, int width, const RenderOptions& opt) : d_(std::make_unique<Impl>(bm)) {
  Impl& d = *d_;
  const int W = clamp(width, kMinWidth, kMaxWidth), H = heightFor(W);
  const double mw = bm && bm->loaded() ? bm->width() : schema::kMapWidth, mh = bm && bm->loaded() ? bm->height() : schema::kMapHeight;
  d.opt = opt;
  d.opt.selProvince = d.opt.selFaction = d.opt.selArmy = d.opt.selRoute = 0;
  d.opt.hoverProvince = d.opt.hoverArmy = 0;
  d.opt.editBorders = false;
  d.opt.edgeLine = false;
  d.img = gfx::Image(W, H, gfx::premul(Color(255, 255, 255)));
  d.mv.setWorld(w);
  d.mv.setViewport(RectF(0, 0, float(W), float(H)), 1);
  d.mv.centerOn(Vec2(mw / 2, mh / 2), double(W) / mw, false);
  d.tiles = std::max(1, int(std::ceil(W / 512.0)) * int(std::ceil(H / 512.0)));
}

MapExport::~MapExport() = default;

int MapExport::width() const { return d_->img.w; }
int MapExport::height() const { return d_->img.h; }
const gfx::Image& MapExport::image() const { return d_->img; }

bool MapExport::step() {
  Impl& d = *d_;
  if (d.done) return true;
  // Пока тайлы рисуются в фоне, кадр не перерисовывается (каждая отрисовка — вся картинка); отложенные подписи
  // дорисовываются следующими отрисовками.
  if (d.renders > 0 && d.mv.tilesBusy()) return false;
  gfx::Canvas c(d.img);
  d.mv.render(c, d.opt);
  d.renders++;
  const RenderStats& st = d.mv.stats();
  d.done = !st.fallback && !st.labelsDeferred && !d.mv.tilesBusy();
  return d.done;
}

double MapExport::progress() const {
  if (d_->done) return 1;
  if (d_->renders == 0) return 0;
  const int pending = d_->mv.stats().pending;
  return clamp(0.05 + 0.9 * (1.0 - double(pending) / d_->tiles), 0.05, 0.95);
}

bool MapExport::wait(double timeoutSec) { return d_->mv.waitIdle(timeoutSec); }

gfx::Image MapExport::take() { return std::move(d_->img); }

gfx::Image exportMap(const Basemap* bm, const World& w, int width, const RenderOptions& opt) {
  MapExport e(bm, w, width, opt);
  for (int i = 0; i < 200 && !e.step(); i++) e.wait(30);
  return e.image();
}

}  // namespace rg::map
