// Regnum — запись PNG-артефактов тестов в каталог test::outDir(). Только base и codec: годится тестам любого модуля.
//
//   test::savePng("gfx_icons.png", image);                    // gfx::Image (всё, у чего есть toRgbaImage())
//   test::savePng("map_view_fit.png", rgba, 4);               // codec::RgbaImage, уровень сжатия (по умолчанию 6)
//   test::argbToRgba(frame, x0, y0, x1, y1, k)                // кадр окна (строки 0xAARRGGBB) → RgbaImage
//   test::makeRgba(w, h, [](int x, int y, u8* p) { ... })     // входная картинка сценария; pngBytes — её файл PNG
//
// Имя файла — путь относительно каталога артефактов вместе с расширением; префикс модуля добавляет вызывающий.
#pragma once
#include <concepts>

#include "base/fs.h"
#include "codec/png.h"
#include "tests/test.h"

namespace rg::test {

inline bool savePng(const std::string& file, const codec::RgbaImage& img, int level = 6) {
  return codec::writePngFile(outDir() + "/" + file, img, level);
}

// Изображение с переводом в RGBA (gfx::Image: предумноженные пиксели → обычный RGBA8).
template <class Img>
  requires requires(const Img& i) { { i.toRgbaImage() } -> std::same_as<codec::RgbaImage>; }
inline bool savePng(const std::string& file, const Img& img, int level = 6) {
  return savePng(file, img.toRgbaImage(), level);
}

// Пиксели 0xAARRGGBB без предумножения, доступные построчно (src.row(y) — указатель на строку, как у кадра окна
// platform::Frame), → RGBA: прямоугольник [x0, x1) × [y0, y1), каждый пиксель — k × k (ближайший сосед, для
// разглядывания мелких деталей); opaque — альфа 255 (кадр окна непрозрачен).
template <class Rows>
codec::RgbaImage argbToRgba(const Rows& src, int x0, int y0, int x1, int y1, int k = 1, bool opaque = true) {
  codec::RgbaImage img;
  img.w = std::max(0, x1 - x0) * k;
  img.h = std::max(0, y1 - y0) * k;
  img.rgba.resize(size_t(img.w) * size_t(img.h) * 4);
  for (int y = 0; y < img.h; y++) {
    const u32* row = src.row(y0 + y / k);
    for (int x = 0; x < img.w; x++) {
      const u32 p = row[x0 + x / k];
      u8* d = &img.rgba[(size_t(y) * size_t(img.w) + size_t(x)) * 4];
      d[0] = u8(p >> 16);
      d[1] = u8(p >> 8);
      d[2] = u8(p);
      d[3] = opaque ? 255 : u8(p >> 24);
    }
  }
  return img;
}

// Входное изображение сценария (портрет, флаг, значок) w × h: px(x, y, rgba) задаёт четыре байта пикселя.
template <class Px> codec::RgbaImage makeRgba(int w, int h, Px&& px) {
  codec::RgbaImage img;
  img.w = w;
  img.h = h;
  img.rgba.resize(size_t(w) * size_t(h) * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) px(x, y, &img.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4]);
  return img;
}
// Содержимое файла PNG (для записи в мир или подачи в диалог открытия).
inline std::string pngBytes(const codec::RgbaImage& img) {
  const std::vector<u8> png = codec::encodePng(img, 6);
  return std::string(png.begin(), png.end());
}

// Самоцвет 96 × 96 цвета (r0, g0, b0) на тёмном фоне с бликом (изображение реликвии), файл PNG.
inline std::string gemPng(u8 r0, u8 g0, u8 b0) {
  return pngBytes(makeRgba(96, 96, [&](int x, int y, u8* p) {
    const double dx = (x - 48) / 30.0, dy = (y - 50) / 34.0, d = dx * dx + dy * dy;
    const double hl = std::max(0.0, 1 - ((x - 40) * (x - 40) + (y - 38) * (y - 38)) / 90.0);
    const double k = d < 1 ? 1 - 0.55 * d : 0.12;
    p[0] = u8(std::min(255.0, r0 * k + 255 * hl * 0.6));
    p[1] = u8(std::min(255.0, g0 * k + 255 * hl * 0.6));
    p[2] = u8(std::min(255.0, b0 * k + 255 * hl * 0.6));
    p[3] = 255;
  }));
}

}  // namespace rg::test
