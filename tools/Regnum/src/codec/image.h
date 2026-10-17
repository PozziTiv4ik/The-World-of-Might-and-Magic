// Regnum — растровое изображение кодеков (RGBA8) и декодирование по сигнатуре (PNG или JPEG).
#pragma once
#include "base/base.h"

namespace rg::codec {

// Обычный (не premultiplied) RGBA8, строки сверху вниз.
struct RgbaImage {
  int w = 0, h = 0;
  std::vector<u8> rgba;
  bool empty() const { return w <= 0 || h <= 0; }
};

// Формат по сигнатуре: PNG или JPEG. Иначе — ошибка «неизвестный формат».
std::optional<RgbaImage> decodeImage(const u8* data, size_t size, std::string* error = nullptr);
inline std::optional<RgbaImage> decodeImage(const std::string& bytes, std::string* error = nullptr) {
  return decodeImage(reinterpret_cast<const u8*>(bytes.data()), bytes.size(), error);
}

}  // namespace rg::codec
