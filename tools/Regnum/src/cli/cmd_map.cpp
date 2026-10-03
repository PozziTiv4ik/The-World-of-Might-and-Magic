// Regnum — команда regnum-cli build-map: исходное изображение карты -> объекты карты (map.json), которые редактор
// рисует кодом: береговая линия, островки, реки и озёра, стены, горы, замки, башни.
//
//   regnum-cli build-map <исходник.png> <папка>        разобрать и записать <папка>/map.json (обычно assets/basemap)
//     --render=<png> [--crop=x,y,ш,в] [--diff=<png>]   нарисовать карту кодом и сравнить с исходником
//     --debug=<папка>                                  отладочные изображения разбора
//   regnum-cli build-map <исходник.png> --inventory=<папка> | --icons=<png> | --fit-icons   (разработка)
#include <cstdio>

#include "base/fs.h"
#include "cli/cli.h"
#include "codec/png.h"
#include "map/art_extract.h"
#include "map/art_render.h"
#include "map/basemap_build.h"

namespace rg::cli {

namespace {

int runBuildMap(const std::vector<std::string>& args) {
  const std::vector<std::string> pos = positional(args);
  if (pos.empty() || pos.size() > 2) {
    std::fprintf(stderr, "Использование: regnum-cli build-map <исходник.png> [папка] [--render=<png>] [--debug=<папка>]\n");
    return 2;
  }
  std::string err;
  auto bytes = fs::readFile(pos[0], &err);
  if (!bytes) fail(err);
  auto img = codec::decodePng(*bytes, &err);
  if (!img) fail("Не удалось прочитать PNG «" + pos[0] + "»: " + err);
  const std::string sha = map::bake::sha256Hex(bytes->data(), bytes->size());
  bytes.reset();
  map::bake::flattenOnWhite(*img);
  if (const std::string inv = option(args, "--inventory"); !inv.empty()) {
    map::art::writeInventory(*img, inv);
    return 0;
  }
  if (const std::string sheet = option(args, "--icons"); !sheet.empty()) {
    // Значки в масштабах 1, 2, 4, 8: горы (оба рисунка), замок, башня.
    const float scales[] = {1, 2, 4, 8};
    gfx::Image img(1200, 380, gfx::premul(Color(255, 255, 255)));
    gfx::Canvas c(img);
    float x = 20;
    for (float sc : scales) {
      const float y = 20 + 31 * sc;
      map::art::drawSymbol(c, map::art::Sym::Mountain, gfx::Pt{x + 10 * sc, std::min(360.f, 20 + 14 * sc)}, sc, 0);
      map::art::drawSymbol(c, map::art::Sym::Mountain, gfx::Pt{x + 32 * sc, std::min(360.f, 20 + 14 * sc)}, sc, 1);
      if (sc <= 4) {
        map::art::drawSymbol(c, map::art::Sym::Castle, gfx::Pt{x + 22 * sc, std::min(370.f, y + 10 * sc)}, sc, 0);
        map::art::drawSymbol(c, map::art::Sym::Tower, gfx::Pt{x + 50 * sc, std::min(370.f, y + 10 * sc)}, sc, 0);
      }
      x += 60 * sc;
    }
    codec::RgbaImage out;
    out.w = img.w;
    out.h = img.h;
    out.rgba = img.toRgba();
    codec::writePngFile(sheet, out, 6);
    return 0;
  }
  if (hasFlag(args, "--fit-icons")) {
    for (const map::art::MountainSample& m : map::art::mountainSamples(*img)) {
      double e0 = 0, e1 = 0;
      map::art::fitMountainIcon(m.ink, m.w, m.h, m.ax, m.ay, m.design, 0, &e0);
      const std::string p = map::art::fitMountainIcon(m.ink, m.w, m.h, m.ax, m.ay, m.design, 40000, &e1);
      std::printf("Рисунок горы %d (образец %d × %d, привязка %d, %d): ошибка %.2f -> %.2f\n%s\n", m.design, m.w, m.h, m.ax, m.ay, e0, e1, p.c_str());
    }
    return 0;
  }
  map::art::ExtractReport rep;
  map::art::MapArt art = map::art::extract(*img, &rep, option(args, "--debug"));
  art.id = option(args, "--id", "wmm-expanded-v1");
  art.source = fs::filename(pos[0]);
  art.sourceSha256 = sha;
  std::fputs(rep.text().c_str(), stdout);
  // Сравнение карты, нарисованной кодом, с исходником (вся карта или --crop=x,y,ш,в при масштабе 1).
  if (const std::string out = option(args, "--render"); !out.empty()) {
    int cx = 0, cy = 0, cw = art.width, ch = art.height;
    if (const std::string cr = option(args, "--crop"); !cr.empty()) {
      std::vector<int> v;
      for (const auto& p : split(cr, ','))
        if (auto n = parseNum(p)) v.push_back(int(*n));
      if (v.size() != 4) fail("Параметр --crop: ожидается x,y,ширина,высота.");
      cx = v[0], cy = v[1], cw = v[2], ch = v[3];
    }
    const double t0 = nowSeconds();
    const map::art::Index idx(art);
    const gfx::Image r = map::art::render(idx, 1, cw, ch, cx, cy);
    const double tr = nowSeconds() - t0;
    codec::RgbaImage ri;
    ri.w = cw;
    ri.h = ch;
    ri.rgba = r.toRgba();
    codec::RgbaImage diff = ri;
    double sum = 0;
    i64 over8 = 0, over32 = 0;
    for (int y = 0; y < ch; y++)
      for (int x = 0; x < cw; x++) {
        const u8* a = &ri.rgba[(size_t(y) * size_t(cw) + size_t(x)) * 4];
        u8* d = &diff.rgba[(size_t(y) * size_t(cw) + size_t(x)) * 4];
        const int sx = x + cx, sy = y + cy;
        if (sx < 0 || sy < 0 || sx >= img->w || sy >= img->h) continue;
        const u8* b = &img->rgba[(size_t(sy) * size_t(img->w) + size_t(sx)) * 4];
        int m = 0;
        for (int c = 0; c < 3; c++) {
          const int e = std::abs(int(a[c]) - int(b[c]));
          sum += e;
          m = std::max(m, e);
          d[c] = u8(std::min(255, e * 4));
        }
        over8 += m > 8;
        over32 += m > 32;
      }
    const double px = double(cw) * double(ch);
    std::printf("Отрисовка %d × %d: %.2f с; средняя ошибка %.3f на канал, пикселей с ошибкой > 8: %.3f %%, > 32: %.3f %%\n", cw, ch, tr, sum / (3 * px),
                100.0 * double(over8) / px, 100.0 * double(over32) / px);
    codec::writePngFile(out, ri, 6);
    if (const std::string dd = option(args, "--diff"); !dd.empty()) codec::writePngFile(dd, diff, 6);
  }
  if (pos.size() == 2) {
    fs::makeDirs(pos[1]);
    const std::string out = fs::join(pos[1], "map.json");
    map::art::save(art, out);
    std::printf("Записано: %s\n", fs::absolute(out).c_str());
  }
  return 0;
}

const Command reg("build-map", "<исходник.png> [папка] [--render=<png> [--crop=x,y,ш,в] [--diff=<png>]] [--debug=<папка>] [--id=...]",
                  "Разобрать исходное изображение в объекты карты (map.json), которые редактор рисует кодом", &runBuildMap);

}  // namespace

}  // namespace rg::cli
