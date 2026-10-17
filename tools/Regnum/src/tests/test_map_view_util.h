// Regnum — общие средства тестов карты (test_map_*.cpp): настоящая базовая карта, демонстрационный и пустой миры, кадр с
// дорисовкой тайлов, войско для отметок, запись кадров и увеличенных фрагментов в PNG.
#pragma once
#include "base/fs.h"
#include "gfx/canvas.h"
#include "map/basemap.h"
#include "map/demo_world.h"
#include "map/mapview.h"
#include "tests/test.h"
#include "tests/test_png.h"

namespace rg::mvtest {

// Настоящая базовая карта репозитория (тесты запускаются из корня репозитория).
inline const map::Basemap& basemap() {
  static const map::Basemap bm = [] {
    map::Basemap b;
    std::string err;
    if (!b.load("tools/Regnum/assets/basemap", &err)) test::fail(__FILE__, __LINE__, "базовая карта: " + err);
    return b;
  }();
  return bm;
}

inline const World& demo() {
  static const World w = map::makeDemoWorld(basemap());
  return w;
}

// Пустой мир на берегу базовой карты (без провинций).
inline const World& emptyWorld() {
  static const World w = [] {
    World x = newWorld("Пустой");
    Tx tx(x);
    geo::initFromCoast(tx, basemap().coast());
    return std::move(tx).finish();
  }();
  return w;
}

// Кадр с дорисовкой: рендер, ожидание фоновых тайлов, повтор — пока всё не готово.
inline gfx::Image renderFull(map::MapView& mv, const map::RenderOptions& opt, int w, int h, float dpi) {
  mv.setViewport(RectF(0, 0, float(w), float(h)), dpi);
  gfx::Image img(int(std::lround(w * dpi)), int(std::lround(h * dpi)), 0xff000000u);
  for (int i = 0; i < 20; i++) {
    gfx::Canvas c(img);
    mv.render(c, opt);
    if (!mv.loading() && !mv.stats().fallback) break;
    mv.waitIdle(20);
  }
  return img;
}

// Войско (или флот) с одной группой фракции faction: units отрядов строки 1.
inline Army makeArmy(Id id, Vec2 pos, Id faction, i64 units, ArmyKind kind = ArmyKind::Army) {
  Army a;
  a.id = id;
  a.kind = kind;
  a.pos = pos;
  a.groups.push_back(ArmyGroup{faction, {ArmyUnit{1, units}}, {}});
  return a;
}

// Кадр в каталог артефактов (сжатие 4 — крупные снимки пишутся быстрее); неудача записи роняет тест.
inline void savePng(const gfx::Image& img, const std::string& name) {
  if (!test::savePng(name, img, 4)) test::fail(__FILE__, __LINE__, "не записать " + name);
}

// Увеличенный фрагмент (каждый пиксель — k × k) для разглядывания швов, берега и подписей.
inline void saveCrop(const gfx::Image& img, gfx::RectI r, int k, const std::string& name) {
  r = r.intersect(gfx::RectI(0, 0, img.w, img.h));
  gfx::Image out(r.w * k, r.h * k, 0);
  for (int y = 0; y < out.h; y++)
    for (int x = 0; x < out.w; x++) out.row(y)[x] = img.at(r.x + x / k, r.y + y / k);
  savePng(out, name);
}

}  // namespace rg::mvtest
