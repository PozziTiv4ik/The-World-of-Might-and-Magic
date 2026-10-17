// Знак приложения (gfx/logo.h): плитка значка программы во всех размерах ICO и золотой знак верхней панели.
#include "gfx/logo.h"
#include "tests/test.h"
#include "tests/test_png.h"

using namespace rg;

TEST(gfx_logo_sheet) {
  const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
  gfx::Image sheet(980, 300, gfx::premul(Color::hex(0x0e1117)));
  gfx::Canvas c(sheet);
  c.fillRect(RectF{0, 150, 980, 150}, gfx::Paint(Color::hex(0xf4f1ea)));
  float x = 16;
  for (int s : sizes) {
    float sz = float(std::min(s, 128));
    gfx::Image tile(s, s, 0);
    gfx::Canvas tc(tile);
    gfx::drawLogoTile(tc, RectF{0, 0, float(s), float(s)});
    // Углы прозрачны (скругление), центр непрозрачен.
    CHECK((tile.at(0, 0) >> 24) < 40);
    CHECK((tile.at(s / 2, s / 2) >> 24) == 255);
    // В плитке есть золото (знак).
    int gold = 0;
    for (u32 p : tile.px) {
      Color col = gfx::unpremul(p);
      if (col.a > 200 && col.r > 170 && col.g > 110 && col.b < 110 && col.r > col.b + 80) gold++;
    }
    CHECK_MSG(gold > s * s / 40, "размер " + std::to_string(s));
    c.drawImage(tile, RectF{x, 75 - sz / 2, sz, sz});
    c.drawImage(tile, RectF{x, 225 - sz / 2, sz, sz});
    x += sz + 18;
  }
  // Знак верхней панели (без плитки) на тёмном и светлом.
  for (int i = 0; i < 2; i++) {
    gfx::drawLogoMark(c, RectF{900, 40.f + 150.f * float(i), 64, 64});
    gfx::drawLogoMark(c, RectF{868, 52.f + 150.f * float(i), 22, 22}, true);
  }
  CHECK(test::savePng("gfx_logo.png", sheet));
}
