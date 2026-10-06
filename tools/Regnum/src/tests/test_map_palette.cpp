// Тесты палитры карты: Source — как исходник (цвета map.json, чёрная краска), Parchment — приглушённое море и
// пергамент. Цвета моря, суши, вод и краски знаков в отрисовке, серые знаков — mix(ink, paper, g / 255), смена
// палитры перерисовывает тайлы, превью, миниатюры и мини-карту, легенда и экспорт — в палитре; снимки для глаза.
#include <chrono>
#include <thread>

#include "geo/geom.h"
#include "map/art_render.h"
#include "map/art_scene.h"
#include "map/export.h"
#include "tests/test_map_view_util.h"

using namespace rg;
using map::Palette;

namespace {

constexpr int W = 1920, H = 1080;
constexpr float DPI = 1.25f;

int maxDiff(u32 a, u32 b) {
  int e = 0;
  for (int s = 0; s < 32; s += 8) e = std::max(e, std::abs(int((a >> s) & 255) - int((b >> s) & 255)));
  return e;
}

// Пиксель кадра (пиксели устройства) в точке карты m.
u32 pixelAt(const gfx::Image& img, const map::View& v, Vec2 m) {
  const gfx::Pt s = v.toScreen(m);
  return img.at(int(s.x * v.dpi), int(s.y * v.dpi));
}

// Точка суши, вокруг которой на r единиц нет моря, вод, стен и знаков (цвет — ровно суша).
Vec2 plainLand(double r) {
  const map::Basemap& bm = mvtest::basemap();
  const map::art::Index& idx = bm.index();
  std::vector<u32> ids;
  for (double y = 300; y < 4200; y += 37)
    for (double x = 300; x < 7700; x += 41) {
      const Box2 b(x - r, y - r, x + r, y + r);
      bool ok = true;
      for (double yy = b.y0; yy <= b.y1 && ok; yy += 4)
        for (double xx = b.x0; xx <= b.x1 && ok; xx += 4) ok = !bm.isOcean(Vec2(xx, yy));
      idx.symbols(b, ids);
      ok = ok && ids.empty();
      idx.water(b, ids);
      ok = ok && ids.empty();
      idx.rivers(b, ids);
      ok = ok && ids.empty();
      idx.lines(b, ids);
      ok = ok && ids.empty();
      if (ok) return Vec2(x, y);
    }
  test::fail(__FILE__, __LINE__, "нет чистой суши");
  return {};
}

// Точка открытого моря: на r единиц вокруг ни суши, ни островков.
Vec2 openSea(double r) {
  const map::Basemap& bm = mvtest::basemap();
  for (double y = r + 20; y < 4500 - r; y += 20)
    for (double x = r + 20; x < 8000 - r; x += 20) {
      bool ok = true;
      for (double yy = y - r; yy <= y + r && ok; yy += 8)
        for (double xx = x - r; xx <= x + r && ok; xx += 8) ok = bm.isOcean(Vec2(xx, yy));
      if (ok) return Vec2(x, y);
    }
  test::fail(__FILE__, __LINE__, "нет открытого моря");
  return {};
}

// Середина большого озера без островов.
Vec2 lakeCenter() {
  for (const MapShape& sh : mvtest::basemap().objects().shapes)
    if (sh.kind == ShapeKind::Water && sh.holes.empty() && std::fabs(geo::signedArea(sh.pts)) > 4000) return geo::polylabel({sh.pts}, 0.5);
  test::fail(__FILE__, __LINE__, "нет озера");
  return {};
}

// Дождаться фонового превью карты мира (миниатюра мира вместо миниатюры базовой карты).
void waitWorldThumb(map::MapView& mv, Palette p) {
  const map::RenderOptions opt;
  for (int i = 0; i < 300 && mv.mapThumbnail().get() == mvtest::basemap().thumb(p).get(); i++) {
    gfx::Image tmp(64, 64, 0xff000000u);
    gfx::Canvas c(tmp);
    mv.render(c, opt);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}  // namespace

// ================================================================ палитра: цвета и серые знаков
TEST(map_palette_style) {
  const map::art::Style base = mvtest::basemap().art().style;
  const map::art::Style src = map::art::paletteStyle(base, Palette::Source);
  CHECK(src.sea == base.sea && src.land == base.land && src.water == base.water);
  CHECK(src.ink == Color(0, 0, 0) && src.paper == Color(255, 255, 255));
  CHECK_EQ(int(src.outside.a), 0);
  // У исходника серый остаётся собой до бита.
  for (int g = 0; g < 256; g++) CHECK(src.tone(u8(g)) == Color(u8(g), u8(g), u8(g)));
  const map::art::Style par = map::art::paletteStyle(base, Palette::Parchment);
  CHECK(par.tone(0) == par.ink);
  CHECK(par.tone(255) == par.paper);
  CHECK(par.paper == par.land);
  CHECK_NEAR(par.coastSoft, base.coastSoft, 1e-6);
  // Море тёмное и приглушённое, суша светлая и тёплая, воды темнее моря, краска тёмная.
  CHECK(par.sea.luminance() < 0.12 && par.land.luminance() > 0.7);
  CHECK(par.land.r > par.land.b);
  CHECK(par.water.luminance() < par.sea.luminance());
  CHECK(par.ink.luminance() < 0.05);
  CHECK(par.outside.a == 255 && par.outside.luminance() < par.sea.luminance());
  int prev = -1;
  for (int g = 0; g < 256; g += 5) {
    const int l = int(par.tone(u8(g)).luminance() * 10000);
    CHECK(l >= prev);
    prev = l;
  }
  CHECK(mvtest::basemap().style(Palette::Parchment).sea == par.sea);
}

TEST(map_palette_symbols_tone) {
  // Башня — ровно краской палитры, без полутонов; серые горы — mix(ink, paper, g / 255) серого исходника.
  const map::art::Style par = map::art::paletteStyle(map::art::Style{}, Palette::Parchment);
  {
    gfx::Image img(30, 30, gfx::premul(par.paper));
    gfx::Canvas c(img);
    map::art::drawSymbol(c, map::art::Sym::Tower, gfx::Pt{15, 22}, 1, 0, Palette::Parchment);
    int ink = 0, other = 0;
    for (u32 p : img.px) {
      ink += p == gfx::premul(par.ink);
      other += p != gfx::premul(par.ink) && p != gfx::premul(par.paper);
    }
    CHECK_EQ(ink, 142);
    CHECK_EQ(other, 0);
  }
  for (int v = 0; v < 2; v++)
    for (float s : {1.f, 2.6f, 6.f}) {
      gfx::Image a(160, 140, gfx::premul(Color(255, 255, 255))), b(160, 140, gfx::premul(par.paper));
      gfx::Canvas ca(a), cb(b);
      map::art::drawSymbol(ca, map::art::Sym::Mountain, gfx::Pt{80.3f, 120.6f}, s, v);
      map::art::drawSymbol(cb, map::art::Sym::Mountain, gfx::Pt{80.3f, 120.6f}, s, v, Palette::Parchment);
      int worst = 0, dark = 255;
      for (size_t i = 0; i < a.px.size(); i++) {
        const u8 g = u8(a.px[i] & 255);
        dark = std::min<int>(dark, g);
        worst = std::max(worst, maxDiff(b.px[i], gfx::premul(par.tone(g))));
      }
      CHECK_MSG(worst <= 2, strf("design %d scale %.1f: %d", v, s, worst));
      CHECK(dark < 128);   // у горы есть тёмные части
    }
}

TEST(map_palette_art_layers) {
  // Слои карты в палитре: открытое море, вода озера, суша внутри замка.
  const map::Basemap& bm = mvtest::basemap();
  const map::art::Style par = bm.style(Palette::Parchment);
  gfx::Image a(64, 64, gfx::premul(par.land));
  map::art::drawSea(a, bm.index(), map::art::Xf{1, 10, 10}, Palette::Parchment);
  for (u32 p : a.px) CHECK_EQ(p, gfx::premul(par.sea));
  const Vec2 lake = lakeCenter();
  const gfx::Image l = map::art::render(bm.index(), 1, 9, 9, std::floor(lake.x) - 4, std::floor(lake.y) - 4, Palette::Parchment);
  CHECK_EQ(l.at(4, 4), gfx::premul(par.water));
  const Vec2 g = plainLand(30);
  const gfx::Image p = map::art::render(bm.index(), 1, 9, 9, std::floor(g.x) - 4, std::floor(g.y) - 4, Palette::Parchment);
  for (u32 px : p.px) CHECK_EQ(px, gfx::premul(par.land));
  // Источник не меняется: открытое море — ровно цвет моря map.json.
  gfx::Image s(16, 16, gfx::premul(Color(255, 255, 255)));
  map::art::drawSea(s, bm.index(), map::art::Xf{1, 10, 10});
  for (u32 px : s.px) CHECK_EQ(px, gfx::premul(Color(0, 38, 255)));
  // Превью и миниатюра базовой карты — свои в каждой палитре, рисуются один раз.
  CHECK(bm.preview(Palette::Parchment).get() == bm.preview(Palette::Parchment).get());
  CHECK(bm.thumb(Palette::Parchment).get() != bm.thumb(Palette::Source).get());
  CHECK_EQ(bm.thumb(Palette::Parchment)->at(2, 2), gfx::premul(par.sea));
  CHECK_EQ(bm.thumb()->at(2, 2), gfx::premul(Color(0, 38, 255)));
}

// ================================================================ карта: цвета в кадре
TEST(map_palette_view_colors) {
  const map::Basemap& bm = mvtest::basemap();
  const map::art::Style par = bm.style(Palette::Parchment);
  map::MapView mv(&bm);
  CHECK(mv.palette() == Palette::Source);
  mv.setPalette(Palette::Parchment);
  CHECK(mv.palette() == Palette::Parchment);
  mv.setWorld(mvtest::emptyWorld());
  map::RenderOptions opt;
  // Вся карта: море, за краем карты — море чуть темнее.
  const gfx::Image fit = mvtest::renderFull(mv, opt, W, H, DPI);
  CHECK(!mv.stats().fallback);
  CHECK_EQ(pixelAt(fit, mv.view(), {60, 60}), gfx::premul(par.sea));
  CHECK_EQ(fit.at(3, 3), gfx::premul(par.outside));
  CHECK_EQ(fit.at(fit.w - 3, fit.h - 3), gfx::premul(par.outside));
  // 1 : 1 — суша, озеро.
  const Vec2 g = plainLand(30), lake = lakeCenter();
  mv.centerOn(g, 1.0, false);
  const gfx::Image a = mvtest::renderFull(mv, opt, 800, 600, 1);
  CHECK_EQ(pixelAt(a, mv.view(), g), gfx::premul(par.land));
  mv.centerOn(lake, 1.0, false);
  const gfx::Image b = mvtest::renderFull(mv, opt, 800, 600, 1);
  CHECK_EQ(pixelAt(b, mv.view(), lake), gfx::premul(par.water));
  // Замок крупно: самый тёмный пиксель — краска палитры.
  const map::art::Symbol* castle = nullptr;
  for (const map::art::Symbol& s : bm.art().symbols)
    if (s.kind == map::art::Sym::Castle && s.s == 1) {
      castle = &s;
      break;
    }
  CHECK(castle != nullptr);
  mv.centerOn(Vec2(castle->x, castle->y - 15), 3.0, false);
  const gfx::Image c = mvtest::renderFull(mv, opt, 400, 300, 1);
  u32 darkest = c.at(200, 150);
  for (u32 p : c.px)
    if (gfx::unpremul(p).luminance() < gfx::unpremul(darkest).luminance()) darkest = p;
  CHECK_MSG(maxDiff(darkest, gfx::premul(par.ink)) <= 2, strf("%08x", darkest));
}

TEST(map_palette_switch_repaints) {
  // Смена палитры перерисовывает тайлы; возврат к исходнику даёт тот же кадр, что у новой карты (по умолчанию).
  const map::Basemap& bm = mvtest::basemap();
  map::RenderOptions opt;
  map::MapView fresh(&bm);
  fresh.setWorld(mvtest::demo());
  const gfx::Image ref = mvtest::renderFull(fresh, opt, 960, 540, DPI);
  map::MapView mv(&bm);
  mv.setWorld(mvtest::demo());
  const gfx::Image a = mvtest::renderFull(mv, opt, 960, 540, DPI);
  CHECK(a.px == ref.px);
  mv.setPalette(Palette::Parchment);
  const gfx::Image b = mvtest::renderFull(mv, opt, 960, 540, DPI);
  CHECK(!mv.stats().fallback);
  CHECK_EQ(pixelAt(b, mv.view(), {60, 60}), gfx::premul(bm.style(Palette::Parchment).sea));
  CHECK_EQ(pixelAt(a, mv.view(), {60, 60}), gfx::premul(Color(0, 38, 255)));
  i64 changed = 0;
  for (size_t i = 0; i < a.px.size(); i++) changed += a.px[i] != b.px[i];
  CHECK(changed > i64(a.px.size()) * 9 / 10);
  mv.setPalette(Palette::Source);
  const gfx::Image c = mvtest::renderFull(mv, opt, 960, 540, DPI);
  CHECK(c.px == ref.px);
  mv.setPalette(Palette::Parchment);
  const gfx::Image d = mvtest::renderFull(mv, opt, 960, 540, DPI);
  CHECK(d.px == b.px);
  // Та же палитра ещё раз — ничего не сбрасывается.
  mv.setPalette(Palette::Parchment);
  CHECK(mv.palette() == Palette::Parchment);
}

TEST(map_palette_minimap_thumbnail_legend) {
  const map::Basemap& bm = mvtest::basemap();
  const map::art::Style par = bm.style(Palette::Parchment);
  map::MapView mv(&bm);
  mv.setWorld(mvtest::demo());
  mv.setViewport(RectF(0, 0, W, H), DPI);
  const RectF rect(12, 12, 360, 202.5f);
  auto mini = [&]() {
    gfx::Image img(int(384 * DPI), int(227 * DPI), gfx::premul(Color::hex(0x151a22)));
    gfx::Canvas c(img);
    mv.renderMinimap(c, rect, DPI);
    return img;
  };
  // Открытое море на мини-карте.
  const Vec2 open = openSea(160);
  auto seaAt = [&](const gfx::Image& img) {
    return img.at(int((rect.x + open.x / 8000 * rect.w) * DPI), int((rect.y + open.y / 4500 * rect.h) * DPI));
  };
  waitWorldThumb(mv, Palette::Source);
  const gfx::Image m0 = mini();
  CHECK(maxDiff(seaAt(m0), gfx::premul(Color(0, 38, 255))) <= 2);
  // Сразу после смены — миниатюра базовой карты в новой палитре, затем миниатюра карты мира.
  mv.setPalette(Palette::Parchment);
  CHECK(mv.mapThumbnail().get() == bm.thumb(Palette::Parchment).get());
  const gfx::Image m1 = mini();
  CHECK(maxDiff(seaAt(m1), gfx::premul(par.sea)) <= 2);
  waitWorldThumb(mv, Palette::Parchment);
  const std::shared_ptr<const gfx::Image> th = mv.mapThumbnail();
  CHECK(th.get() != bm.thumb(Palette::Parchment).get());
  CHECK_EQ(th->at(2, 2), gfx::premul(par.sea));
  const gfx::Image m2 = mini();
  CHECK(maxDiff(seaAt(m2), gfx::premul(par.sea)) <= 2);
  mvtest::savePng(m2, "map_palette_minimap.png");
  // Назад: мини-карта снова как была.
  mv.setPalette(Palette::Source);
  waitWorldThumb(mv, Palette::Source);
  CHECK_EQ(mv.mapThumbnail()->at(2, 2), gfx::premul(Color(0, 38, 255)));
  CHECK(mini().px == m0.px);
  // Легенда: суша, море и знаки — цветами палитры.
  auto colorOf = [&](schema::MapMode mode, const std::string& label) {
    for (const map::LegendItem& li : mv.legend(mvtest::demo(), mode))
      if (li.label == label) return li.color;
    test::fail(__FILE__, __LINE__, "нет строки легенды " + label);
    return Color();
  };
  CHECK(colorOf(schema::MapMode::Political, "Без владельца") == Color(255, 255, 255));
  CHECK(colorOf(schema::MapMode::Terrain, "Море, реки и озёра") == Color(0, 38, 255));
  CHECK(colorOf(schema::MapMode::Terrain, "Горы, замки и башни") == Color::hex(0x8c8c8c));
  mv.setPalette(Palette::Parchment);
  CHECK(colorOf(schema::MapMode::Political, "Без владельца") == par.land);
  CHECK(colorOf(schema::MapMode::Resources, "Без ресурса") == par.land);
  CHECK(colorOf(schema::MapMode::Terrain, "Суша") == par.land);
  CHECK(colorOf(schema::MapMode::Terrain, "Море, реки и озёра") == par.sea);
  CHECK(colorOf(schema::MapMode::Terrain, "Горы, замки и башни") == par.tone(0x8c));
  // Без базовой карты: суша и море по граням — тоже в палитре.
  map::MapView nb(nullptr);
  nb.setPalette(Palette::Parchment);
  nb.setWorld(mvtest::demo());
  map::RenderOptions opt;
  opt.labels = false;
  const gfx::Image f = mvtest::renderFull(nb, opt, 960, 540, 1);
  const map::art::Style def = map::art::paletteStyle(map::art::Style{}, Palette::Parchment);
  CHECK_EQ(pixelAt(f, nb.view(), {60, 60}), gfx::premul(def.sea));
  CHECK_EQ(pixelAt(f, nb.view(), {1000, 500}), gfx::premul(def.land));
  gfx::Image nm(200, 120, 0xff000000u);
  gfx::Canvas nc(nm);
  nb.renderMinimap(nc, RectF(0, 0, 200, 112.5f), 1);
  CHECK(maxDiff(nm.at(int(open.x / 40), int(open.y / 40)), gfx::premul(def.sea)) <= 2);
}

TEST(map_palette_export) {
  // Экспорт — в палитре редактора; по умолчанию (командная строка) — исходник.
  const map::Basemap& bm = mvtest::basemap();
  map::RenderOptions opt;
  opt.labels = false;
  const gfx::Image p = map::exportMap(&bm, mvtest::demo(), 960, opt, Palette::Parchment);
  const gfx::Image s = map::exportMap(&bm, mvtest::demo(), 960, opt);
  const int x = int(60 * 960 / 8000.0) + 1, y = int(60 * 960 / 8000.0) + 1;
  CHECK_EQ(p.at(x, y), gfx::premul(bm.style(Palette::Parchment).sea));
  CHECK_EQ(s.at(x, y), gfx::premul(Color(0, 38, 255)));
  mvtest::savePng(p, "map_palette_export.png");
}

// ================================================================ снимки для глаза
TEST(map_palette_shots) {
  map::MapView mv(&mvtest::basemap());
  mv.setPalette(Palette::Parchment);
  mv.setWorld(mvtest::demo());
  map::RenderOptions opt;
  const gfx::Image fit = mvtest::renderFull(mv, opt, W, H, DPI);
  mvtest::savePng(fit, "map_palette_fit_political.png");
  mvtest::saveCrop(fit, gfx::RectI(0, 0, 160, 110), 4, "map_palette_detail_edge.png");
  map::RenderOptions terrain;
  terrain.mode = schema::MapMode::Terrain;
  mvtest::savePng(mvtest::renderFull(mv, terrain, W, H, DPI), "map_palette_fit_terrain.png");
  const struct { double zoom; Vec2 at; const char* name; } shots[] = {
      {0.55, {4300, 2300}, "map_palette_zoom_055.png"},
      {0.8, {3950, 1180}, "map_palette_zoom_1.png"},
      {1.6, {3830, 1260}, "map_palette_zoom_2.png"},
      {3.0, {3800, 1300}, "map_palette_zoom_3.png"},
  };
  for (const auto& s : shots) {
    mv.centerOn(s.at, s.zoom, false);
    const gfx::Image img = mvtest::renderFull(mv, opt, W, H, DPI);
    CHECK(!mv.stats().fallback);
    mvtest::savePng(img, s.name);
  }
  // Подробности: берег с мягкой каймой, стык государств, подпись морской провинции.
  mv.centerOn({2938, 977}, 3.0, false);
  mvtest::saveCrop(mvtest::renderFull(mv, opt, W, H, DPI), gfx::RectI(1050, 525, 300, 300), 3, "map_palette_detail_coast.png");
  mv.centerOn({3651, 840}, 3.0, false);
  mvtest::saveCrop(mvtest::renderFull(mv, opt, W, H, DPI), gfx::RectI(1050, 525, 300, 300), 3, "map_palette_detail_border.png");
  {
    const World& w = mvtest::demo();
    const geo::ProvinceShape* sea = nullptr;
    w.provinces.each([&](const Province& p) {
      if (p.sea && !sea) sea = geo::faces(w)->shape(p.id);
    });
    CHECK(sea != nullptr);
    mv.centerOn(sea->label, 0.9, false);
    mvtest::saveCrop(mvtest::renderFull(mv, opt, W, H, DPI), gfx::RectI(1050, 575, 300, 200), 3, "map_palette_detail_sea_label.png");
  }
  map::RenderOptions guilds;
  guilds.mode = schema::MapMode::Guilds;
  mv.centerOn({4300, 2300}, 0.55, false);
  mvtest::savePng(mvtest::renderFull(mv, guilds, W, H, DPI), "map_palette_guilds.png");
  map::RenderOptions light;
  light.darkUi = false;
  mv.fitAll(false);
  const gfx::Image fl = mvtest::renderFull(mv, light, W, H, DPI);
  mvtest::saveCrop(fl, gfx::RectI(0, 0, 160, 110), 4, "map_palette_detail_edge_light_ui.png");
}
