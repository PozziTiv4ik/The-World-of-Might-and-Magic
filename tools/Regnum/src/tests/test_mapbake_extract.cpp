// Тест разбора исходной карты: повторный разбор совпадает с map.json в репозитории, карта кодом повторяет исходник.
#include <cstdio>

#include "base/fs.h"
#include "codec/png.h"
#include "codec/sha256.h"
#include "map/art_render.h"
#include "mapbake/art_extract.h"
#include "mapbake/basemap_build.h"
#include "tests/test.h"

using namespace rg;
using namespace rg::map;

TEST(mapbake_extract_matches_assets) {
  // Повторный разбор исходника даёт тот же map.json, что лежит в репозитории, а карта, нарисованная кодом, при
  // масштабе 1 повторяет исходник. Тесты запускаются из корня репозитория.
  const std::string dir = "tools/Regnum/assets/basemap";
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
  a.sourceSha256 = codec::sha256Hex(bytes->data(), bytes->size());
  std::printf("%s", rep.text().c_str());
  CHECK(rep.unexplainedInk * 100 < rep.inkPixels);   // меньше 1 % краски не объяснено объектами
  // Серый сглаженный замок (уменьшен с размытием) у (3802, 1566) — знак, а не стены; ни одна стена не лежит целиком
  // в габарите замка или башни (бледные края найденного знака — не стена).
  bool grayCastle = false;
  for (const art::Symbol& q : a.symbols) grayCastle = grayCastle || (q.kind == art::Sym::Castle && std::fabs(q.x - 3802) < 1 && std::fabs(q.y - 1566) < 1);
  CHECK(grayCastle);
  for (const art::Line& l : a.lines)
    for (const art::Symbol& q : a.symbols) {
      if (q.kind != art::Sym::Castle && q.kind != art::Sym::Tower) continue;
      const RectF b = q.kind == art::Sym::Tower ? RectF(-5, -15, 10, 15) : RectF(-20, -31, 40, 31);
      bool inside = true;
      for (const Vec2& p : l.pts)
        inside = inside && p.x >= q.x + b.x * q.s - 1 && p.x <= q.x + b.right() * q.s + 1 && p.y >= q.y + b.y * q.s - 1 && p.y <= q.y + b.bottom() * q.s + 1;
      CHECK_MSG(!inside, strf("стена у (%.1f, %.1f) целиком внутри знака", l.pts[0].x, l.pts[0].y));
    }
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
