// Regnum — WAV (RIFF/WAVE и RF64): разбор заголовка, отсчёты PCM 8/16/24/32 бит и float 32/64 в float, запись.
//
//   auto fmt = codec::parseWav(readAt, fileSize, &err);        // заголовок по частям файла (потоковое чтение)
//   codec::wavToFloat(*fmt, bytes, frames, out);               // кадры → float −1…1, каналы подряд
//   auto pcm = codec::decodeWav(bytes);                        // весь файл в памяти
//   auto file = codec::encodeWav(pcm, codec::WavSample::Pcm16, "Название", "Исполнитель");
#pragma once
#include <span>

#include "base/base.h"

namespace rg::codec {

// Чтение файла по смещению: до n байт с offset в dst; вернуть прочитанное (меньше n — конец данных).
using ReadAt = std::function<size_t(u64 offset, u8* dst, size_t n)>;
// ReadAt над байтами в памяти (байты должны жить, пока используется функция).
ReadAt memoryReader(std::span<const u8> bytes);

struct WavFormat {
  int channels = 0;
  int rate = 0;            // Гц
  int bits = 0;            // размер отсчёта в контейнере: 8, 16, 24, 32, 64
  bool isFloat = false;
  int blockAlign = 0;      // байт на кадр (все каналы)
  u64 dataOffset = 0;      // начало отсчётов в файле
  u64 dataBytes = 0;       // длина данных (не больше остатка файла, кратна blockAlign)
  u64 frames() const { return blockAlign > 0 ? dataBytes / u64(blockAlign) : 0; }
  double seconds() const { return rate > 0 ? double(frames()) / double(rate) : 0; }
};

// Заголовок: блоки fmt (PCM, IEEE float, WAVE_FORMAT_EXTENSIBLE) и data, у RF64 — размеры из ds64.
// Ошибка — nullopt и сообщение по-русски.
std::optional<WavFormat> parseWav(const ReadAt& read, u64 size, std::string* error = nullptr);
std::optional<WavFormat> parseWav(std::span<const u8> bytes, std::string* error = nullptr);

// frames кадров отсчётов src (формат fmt) — в float −1…1, каналы подряд (dst: frames × channels).
void wavToFloat(const WavFormat& fmt, const u8* src, size_t frames, float* dst);

// Звук в памяти: float −1…1, каналы подряд.
struct PcmAudio {
  int channels = 0, rate = 0;
  std::vector<float> samples;
  size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
};
std::optional<PcmAudio> decodeWav(std::span<const u8> bytes, std::string* error = nullptr);

enum class WavSample : u8 { Pcm16, Pcm24, Float32 };
// Файл WAV: PCM 16/24 бит (округление и ограничение −1…1) или float 32; непустые title/artist — блок LIST/INFO
// (INAM, IART, UTF-8).
std::vector<u8> encodeWav(const PcmAudio& pcm, WavSample sample = WavSample::Pcm16, std::string_view title = {},
                          std::string_view artist = {});

}  // namespace rg::codec
