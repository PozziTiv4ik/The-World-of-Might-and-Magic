// Regnum — декодирование изображения по сигнатуре.
#include "codec/image.h"

#include "codec/jpeg.h"
#include "codec/png.h"

namespace rg::codec {

std::optional<RgbaImage> decodeImage(const u8* data, size_t size, std::string* error) {
  if (data && size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return decodePng(data, size, error);
  if (data && size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return decodeJpeg(data, size, JpegDecodeOptions{}, error);
  if (error) *error = "Неизвестный формат изображения (ожидается PNG или JPEG)";
  return std::nullopt;
}

}  // namespace rg::codec
