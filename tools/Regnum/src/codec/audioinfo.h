// Regnum — сведения о звуковом файле по заголовку, без декодирования: вид, название и исполнитель из тегов,
// длительность (плеер саундтреков показывает их до воспроизведения).
//
//   auto info = codec::probeAudio(readAt, fileSize);   // info.kind пусто — формат не распознан
//
// Вид — по сигнатуре: MP3 (кадры MPEG, длительность — по заголовку Xing/Info или VBRI, иначе по битрейту), WAV и RF64,
// FLAC (STREAMINFO), Ogg Vorbis и Opus (последняя страница), MP4/M4A (mvhd), WMA (ASF), AIFF, AAC в ADTS (по кадрам).
// Теги: ID3v2.2–2.4 (с распаковкой и снятием рассинхронизации) и ID3v1 у MP3 и AAC, LIST/INFO и блок id3 у WAV,
// Vorbis comment у FLAC, Vorbis и Opus, iTunes ilst у MP4, Content Description у ASF, NAME/AUTH и ID3 у AIFF.
#pragma once
#include "codec/wav.h"

namespace rg::codec {

struct AudioInfo {
  std::string kind;          // «mp3», «wav», «flac», «ogg», «opus», «m4a», «wma», «aiff», «aac»; пусто — не распознан
  std::string title;         // название из тегов (UTF-8; пусто — нет)
  std::string artist;        // исполнитель (при отсутствии — исполнитель альбома)
  double duration = 0;       // секунды; 0 — неизвестна
  int rate = 0, channels = 0;   // если известны из заголовка
};

AudioInfo probeAudio(const ReadAt& read, u64 size);
AudioInfo probeAudio(std::span<const u8> bytes);

// Однобайтовый текст тега → UTF-8: корректный UTF-8 — как есть, иначе Windows-1251 (если по составу это кириллица —
// частый случай старых русских тегов) или Latin-1. Нули и пробелы по краям отбрасываются.
std::string tagText8(std::string_view bytes);

}  // namespace rg::codec
