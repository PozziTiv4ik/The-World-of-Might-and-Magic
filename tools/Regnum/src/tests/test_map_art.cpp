// Тесты карты, нарисованной кодом: map.json (чтение и запись), базовая карта (береговая линия, маска моря, превью),
// слои и знаки, разбор исходного изображения (повторный разбор совпадает с map.json в репозитории, карта кодом
// повторяет исходник).
#include <cstdio>

#include "base/fs.h"
#include "map/art.h"
#include "map/art_extract.h"
#include "map/art_render.h"
#include "geo/geom.h"
#include "map/art_scene.h"
#include "map/basemap_build.h"
#include "map/mapview.h"
#include "rules/rules.h"
#include "tests/test_map_view_util.h"

using namespace rg;
using namespace rg::map;

TEST(map_art_sha256) {
  CHECK_EQ(bake::sha256Hex("", 0), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  CHECK_EQ(bake::sha256Hex("abc", 3), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

TEST(map_art_json_roundtrip) {
  art::MapArt a;
  a.id = "test";
  a.source = "src.png";
  a.width = 100;
  a.height = 50;
  a.style.coastSoft = 2;
  a.style.coastGlow = 0.25f;
  a.land = {{{0, 0}, {10.25, 0}, {10, 10.5}}};
  a.islets = {{{50, 20}, {51, 20}, {51, 21}}};
  a.water = {{{20, 20}, {30, 20}, {30, 30}}, {{22, 22}, {24, 22}, {24, 24}}};
  a.lines = {{{{1, 1}, {5, 5}, {9, 4}}, 2.5f, 0}, {{{1, 9}, {9, 9}}, 1, 3}};
  a.symbols = {{art::Sym::Tower, 10.25f, 20, 1, 0}, {art::Sym::Mountain, 5, 5, 1, 1}, {art::Sym::Castle, 30, 40, 0.94f, 0}, {art::Sym::Peak, 60, 45, 2.2f, 0}};
  const art::MapArt b = art::fromJson(art::toJson(a));
  CHECK_EQ(b.id, std::string("test"));
  CHECK_EQ(b.width, 100);
  CHECK_EQ(b.land.size(), size_t(1));
  CHECK_NEAR(b.land[0][1].x, 10.25, 1e-9);
  CHECK_EQ(b.islets.size(), size_t(1));
  CHECK_EQ(b.water.size(), size_t(2));
  CHECK_EQ(b.lines.size(), size_t(2));
  CHECK_NEAR(b.lines[1].dash, 3, 1e-6);
  CHECK_NEAR(b.style.coastGlow, 0.25, 1e-6);
  // Порядок знаков — порядок отрисовки — сохраняется (виды перемешаны).
  CHECK_EQ(b.symbols.size(), size_t(4));
  CHECK(b.symbols[0].kind == art::Sym::Tower && b.symbols[1].kind == art::Sym::Mountain && b.symbols[2].kind == art::Sym::Castle &&
        b.symbols[3].kind == art::Sym::Peak);
  CHECK_NEAR(b.symbols[0].x, 10.25, 1e-6);
  CHECK_EQ(int(b.symbols[1].v), 1);
  CHECK_NEAR(b.symbols[2].s, 0.94, 1e-6);
  CHECK_EQ(art::toJson(b), art::toJson(a));
  CHECK_THROWS(art::fromJson("{}"));
  CHECK_THROWS(art::fromJson(R"({"format": "regnum-map", "width": 0, "height": 10})"));
}

TEST(map_art_basemap_loads) {
  const Basemap& bm = mvtest::basemap();
  CHECK(bm.loaded());
  CHECK_EQ(bm.width(), 8000);
  CHECK_EQ(bm.height(), 4500);
  CHECK_EQ(bm.id(), std::string("wmm-expanded-v1"));
  const geo::Coast c = bm.coast();
  CHECK_EQ(c.landRings.size(), size_t(338));
  CHECK_EQ(c.landRings.size(), bm.art().land.size());
  // Маска моря: угол карты — море, внутри замка — суша.
  CHECK(bm.isOcean(Vec2(10, 10)));
  bool castle = false;
  for (const art::Symbol& s : bm.art().symbols)
    if (s.kind == art::Sym::Castle && s.s == 1) {
      CHECK(!bm.isOcean(Vec2(s.x, s.y - 10)));
      castle = true;
      break;
    }
  CHECK(castle);
  CHECK(!bm.isOcean(Vec2(-5, 10)));
  const auto pv = bm.preview(), th = bm.thumb();
  CHECK(pv && pv->w == 2000 && pv->h == 1125);
  CHECK(th && th->w == 480 && th->h == 270);
  CHECK(bm.preview().get() == pv.get());   // рисуется один раз
  Basemap missing;
  std::string err;
  CHECK(!missing.load(fs::join(test::outDir(), "no-such-basemap"), &err));
  CHECK(!err.empty());
}

TEST(map_art_layers) {
  const Basemap& bm = mvtest::basemap();
  const u32 sea = gfx::premul(bm.art().style.sea), land = gfx::premul(Color(255, 255, 255));
  // Открытое море у угла карты — ровно цвет моря.
  gfx::Image a(64, 64, land);
  art::drawSea(a, bm.index(), art::Xf{1, 10, 10});
  for (u32 p : a.px) CHECK_EQ(p, sea);
  // Внутри замка — суша без моря, вод и знаков.
  for (const art::Symbol& s : bm.art().symbols) {
    if (s.kind != art::Sym::Castle || s.s != 1) continue;
    gfx::Image b(4, 4, land);
    const art::Xf P{1, std::floor(s.x) - 2, std::floor(s.y - 10) - 2};
    art::drawSea(b, bm.index(), P);
    art::drawWater(b, bm.index(), P);
    for (u32 p : b.px) CHECK_EQ(p, land);
    break;
  }
}

TEST(map_art_tower_is_pixel_exact) {
  // Башня при масштабе 1 в целых пикселях — ровно штамп исходника: 142 чёрных пикселя, без полутонов.
  gfx::Image img(30, 30, gfx::premul(Color(255, 255, 255)));
  gfx::Canvas c(img);
  art::drawSymbol(c, art::Sym::Tower, gfx::Pt{15, 22}, 1);
  int black = 0, gray = 0;
  for (u32 p : img.px) {
    const int v = int(p & 255);
    black += v == 0;
    gray += v != 0 && v != 255;
  }
  CHECK_EQ(black, 142);
  CHECK_EQ(gray, 0);
  CHECK_EQ(int(img.at(10, 7) & 255), 0);     // левый зубец
  CHECK_EQ(int(img.at(12, 7) & 255), 255);   // просвет между зубцами
}

TEST(map_art_extract_matches_assets) {
  // Повторный разбор исходника даёт тот же map.json, что лежит в репозитории, а карта, нарисованная кодом, при
  // масштабе 1 повторяет исходник.
  const std::string dir = mvtest::basemap().dir();
  const std::string srcPath = fs::join(dir, "../source/Expanded Map.png");
  auto bytes = fs::readFile(srcPath);
  CHECK_MSG(bytes.has_value(), "нет исходника " + srcPath);
  auto img = codec::decodePng(*bytes);
  CHECK(img.has_value());
  bake::flattenOnWhite(*img);
  art::ExtractReport rep;
  art::MapArt a = art::extract(*img, &rep);
  a.id = "wmm-expanded-v1";
  a.source = "Expanded Map.png";
  a.sourceSha256 = bake::sha256Hex(bytes->data(), bytes->size());
  std::printf("%s", rep.text().c_str());
  CHECK(rep.unexplainedInk * 100 < rep.inkPixels);   // меньше 1 % краски не объяснено объектами
  auto committed = fs::readFile(fs::join(dir, "map.json"));
  CHECK(committed.has_value());
  CHECK_MSG(art::toJson(a) == *committed, "map.json в репозитории устарел: пересоберите regnum-cli build-map");
  // Вся карта при масштабе 1 против исходника.
  const art::Index idx(a);
  const gfx::Image r = art::render(idx, 1, a.width, a.height);
  double sum = 0;
  i64 over32 = 0;
  for (int y = 0; y < a.height; y++)
    for (int x = 0; x < a.width; x++) {
      const u32 p = r.at(x, y);
      const u8* s = &img->rgba[(size_t(y) * size_t(a.width) + size_t(x)) * 4];
      const int e0 = std::abs(int((p >> 16) & 255) - s[0]), e1 = std::abs(int((p >> 8) & 255) - s[1]), e2 = std::abs(int(p & 255) - s[2]);
      sum += e0 + e1 + e2;
      over32 += std::max({e0, e1, e2}) > 32;
    }
  const double px = double(a.width) * a.height, mean = sum / (3 * px), share = 100.0 * double(over32) / px;
  std::printf("  карта кодом против исходника: средняя ошибка %.3f на канал, пикселей с ошибкой > 32: %.2f %%\n", mean, share);
  CHECK(mean < 0.7);
  CHECK(share < 1.0);
}

TEST(map_art_scene_world_objects) {
  // Сцена мира без своих объектов показывает объекты базовой карты; перенос их в мир картинку не меняет.
  const Basemap& bm = mvtest::basemap();
  const World& w0 = mvtest::emptyWorld();
  const double t0 = nowSeconds();
  auto s0 = art::Scene::build(w0, &bm.art(), &bm.objects());
  const double ms0 = (nowSeconds() - t0) * 1000;
  CHECK(!s0->fromWorld());
  CHECK(s0->hasLand());
  CHECK_EQ(s0->art().symbols.size(), bm.art().symbols.size());
  CHECK_EQ(s0->art().land.size(), bm.art().land.size());
  World w1 = w0;
  {
    Tx tx(w1);
    rules::ensureMapObjects(tx, bm.objects().symbols, bm.objects().shapes);
    w1 = std::move(tx).finish();
  }
  CHECK(w1.ownMapObjects());
  const double t1 = nowSeconds();
  auto s1 = art::Scene::build(w1, &bm.art(), &bm.objects());
  const double ms1 = (nowSeconds() - t1) * 1000;
  std::printf("  сцена: базовая карта %.1f мс, объекты мира %.1f мс\n", ms0, ms1);
  CHECK(s1->fromWorld());
  CHECK(ms1 < test::perf(60));
  // Кусок карты при масштабе 1 с городами и горами: попиксельно одинаково.
  const art::Symbol& c = bm.art().symbols[bm.art().symbols.size() / 2];
  const double ox = std::floor(c.x) - 200, oy = std::floor(c.y) - 150;
  gfx::Image a(400, 300, gfx::premul(Color(255, 255, 255))), b = a;
  for (auto [img, sc] : {std::pair{&a, s0}, std::pair{&b, s1}}) {
    const art::Xf P{1, ox, oy};
    art::drawSea(*img, sc->index(), P);
    art::drawWater(*img, sc->index(), P);
    art::drawSymbols(*img, sc->index(), P);
  }
  CHECK(a.px == b.px);
  // Попадание: знак по середине значка, вода внутри озера, ничего — в открытом море.
  const MapSymbol& ms = bm.objects().symbols[123];
  CHECK_EQ(s1->symbolAt(art::symbolBox(ms).center(), 0.5), ms.id);
  for (const MapShape& sh : bm.objects().shapes) {
    if (sh.kind != ShapeKind::Water || !sh.holes.empty() || std::fabs(geo::signedArea(sh.pts)) < 2000) continue;
    const Vec2 in = geo::polylabel({sh.pts}, 0.5);
    CHECK_EQ(s1->shapeAt(in, 0.1), sh.id);
    break;
  }
  CHECK_EQ(s1->shapeAt(Vec2(5, 5), 0.5), Id(0));
  CHECK_EQ(s1->symbolAt(Vec2(5, 5), 0.5), Id(0));
  CHECK(!s1->symbolsIn(art::symbolBox(ms).inflated(1)).empty());
}

TEST(map_art_scene_edit_invalidates_little) {
  // Правка одного знака перерисовывает только тайлы вокруг него, и картинка следует за миром.
  const Basemap& bm = mvtest::basemap();
  map::MapView mv(&bm);
  World w = mvtest::emptyWorld();
  {
    Tx tx(w);
    rules::ensureMapObjects(tx, bm.objects().symbols, bm.objects().shapes);
    w = std::move(tx).finish();
  }
  mv.setWorld(w);
  const MapSymbol s = bm.objects().symbols[500];
  mv.setViewport(RectF(0, 0, 800, 600), 1);
  mv.centerOn(s.p, 1.0, false);
  map::RenderOptions opt;
  gfx::Image before = mvtest::renderFull(mv, opt, 800, 600, 1);
  World w2 = w;
  {
    Tx tx(w2);
    rules::placeSymbol(tx, s.id, s.p + Vec2(40, 0), s.z);
    w2 = std::move(tx).finish();
  }
  const double t0 = nowSeconds();
  mv.worldChanged(w, w2, World::diff(w, w2));
  const double ms = (nowSeconds() - t0) * 1000;
  std::printf("  смена мира после правки знака: %.1f мс\n", ms);
  CHECK(ms < test::perf(40));
  CHECK(mv.artScene()->fromWorld());
  gfx::Image after = mvtest::renderFull(mv, opt, 800, 600, 1);
  // Знак сдвинут на 40 точек вправо: в старом месте и в новом картинка изменилась, вдали — нет.
  const gfx::Pt at = mv.view().toScreen(s.p);
  auto changed = [&](int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = std::max(0, y0); y < std::min(600, y1); y++)
      for (int x = std::max(0, x0); x < std::min(800, x1); x++) n += before.at(x, y) != after.at(x, y);
    return n;
  };
  CHECK(changed(int(at.x) - 8, int(at.y) - 12, int(at.x) + 8, int(at.y)) > 10);
  CHECK(changed(int(at.x) + 32, int(at.y) - 12, int(at.x) + 48, int(at.y)) > 10);
  CHECK_EQ(changed(0, 0, 150, 150), 0);
}
