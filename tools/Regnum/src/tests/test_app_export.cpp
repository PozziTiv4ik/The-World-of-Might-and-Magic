// Экспорт карты в картинку (Ctrl+E, «Экспорт карты»): размер, файл PNG через системный диалог, фоновая отрисовка
// тайлов и запись, уведомление; экспорт без окна (map::exportMap) — вся карта в кадре, без линии края.
#include <thread>

#include "map/export.h"
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Кадры с короткой паузой (тайлы и запись PNG идут в фоне), пока открыто окно экспорта.
bool waitExport(Harness& h, double sec = 60) {
  const double t0 = nowSeconds();
  while (h->hasDialog("map.export") && nowSeconds() - t0 < sec) {
    h.frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  return !h->hasDialog("map.export");
}

}  // namespace

TEST(app_export_map_png) {
  Harness h("export");
  h.demo();
  h.dropToasts();
  h.key(Key::E, ctrl());
  CHECK(h->hasDialog("map.export"));
  CHECK(h->uiRect("export.size") != nullptr);
  h.settle();
  CHECK(h.shot("export_dialog"));
  // Full HD — первый сегмент.
  const RectF size = *h->uiRect("export.size");
  h.click(size.x + size.w * 0.08f, size.cy());
  const std::string file = fs::join(h.root, "Карта мира.png");
  hl::setDialogsSupported(true);
  hl::queueDialogResult(file);
  CHECK(h.clickUi("export.save"));
  CHECK(waitExport(h));
  CHECK(fs::isFile(file));
  auto bytes = fs::readFile(file);
  CHECK(bytes.has_value());
  auto img = codec::decodePng(*bytes);
  CHECK(img.has_value());
  CHECK_EQ(img->w, 1920);
  CHECK_EQ(img->h, 1080);
  // Карта целиком: в углу — море цвета палитры редактора, в кадре много цветов (суша, провинции, знаки, подписи).
  const Color sea = h->basemap()->style(h->map().palette()).sea;
  const u8* c0 = &img->rgba[0];
  CHECK(std::abs(int(c0[0]) - sea.r) < 24 && std::abs(int(c0[1]) - sea.g) < 24 && std::abs(int(c0[2]) - sea.b) < 24);
  std::vector<u32> colors;
  for (size_t i = 0; i < img->rgba.size(); i += 4 * 997) colors.push_back(u32(img->rgba[i]) << 16 | u32(img->rgba[i + 1]) << 8 | img->rgba[i + 2]);
  std::sort(colors.begin(), colors.end());
  colors.erase(std::unique(colors.begin(), colors.end()), colors.end());
  CHECK(colors.size() > 100);
  // Уведомление с кнопкой «Показать».
  bool toast = false;
  for (const app::Toast& t : h->toasts()) toast = toast || (t.text.find("Карта сохранена") != std::string::npos && t.actionLabel == "Показать");
  CHECK(toast);
}

TEST(app_export_cancel_and_sizes) {
  Harness h("export_cancel");
  h.demo();
  h.key(Key::E, ctrl());
  CHECK(h->hasDialog("map.export"));
  // Своя ширина: поле ширины; Отмена закрывает окно без файла.
  const RectF size = *h->uiRect("export.size");
  h.click(size.right() - size.w * 0.08f, size.cy());
  CHECK(h->uiRect("export.width") != nullptr);
  h.key(Key::Escape);
  CHECK(!h->hasDialog("map.export"));
  // Высота — по пропорциям карты.
  CHECK_EQ(map::MapExport::heightFor(7680), 4320);
  CHECK_EQ(map::MapExport::heightFor(3840), 2160);
  CHECK_EQ(map::MapExport::heightFor(8000), 4500);
}

TEST(app_export_map_offscreen) {
  // Экспорт без окна: кадр ровно по карте, края — карта (а не фон редактора).
  Harness h("export_offscreen");
  h.demo();
  map::RenderOptions o;
  o.labels = false;
  const double t0 = nowSeconds();
  gfx::Image img = map::exportMap(h->basemap(), h->world(), 1280, o);
  std::printf("  экспорт 1280 × 720: %.0f мс\n", (nowSeconds() - t0) * 1000);
  CHECK_EQ(img.w, 1280);
  CHECK_EQ(img.h, 720);
  const Color corner = gfx::unpremul(img.at(0, 0)), last = gfx::unpremul(img.at(1279, 719));
  CHECK(corner.b > 150 && corner.r < 100);   // море в углу карты
  CHECK(last.b > 150 && last.r < 100);
}

TEST(bench_export_8k) {
  // Замер экспорта 8K с подписями и записью PNG (не в обычном прогоне: REGNUM_BENCH=1).
  if (!std::getenv("REGNUM_BENCH")) {
    std::printf("  пропущено: запуск с REGNUM_BENCH=1\n");
    return;
  }
  Harness h("export_8k");
  h.demo();
  map::RenderOptions o;
  double t0 = nowSeconds();
  gfx::Image img = map::exportMap(h->basemap(), h->world(), 7680, o);
  const double tr = (nowSeconds() - t0) * 1000;
  t0 = nowSeconds();
  codec::RgbaImage out;
  out.w = img.w;
  out.h = img.h;
  out.rgba = img.toRgba();
  const std::string file = fs::join(h.root, "8k.png");
  CHECK(codec::writePngFile(file, out, 6));
  const double tw = (nowSeconds() - t0) * 1000;
  std::printf("  экспорт 7680 × 4320: отрисовка %.0f мс, PNG %.0f мс, файл %.1f МБ\n", tr, tw, double(fs::fileSize(file).value_or(0)) / 1048576.0);
}
