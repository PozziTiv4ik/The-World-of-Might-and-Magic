// Regnum — WAV: разбор RIFF/RF64, приведение отсчётов к float, запись PCM и float.
#include "codec/wav.h"

namespace rg::codec {

namespace {

u16 le16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
u32 le32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
u64 le64(const u8* p) { return u64(le32(p)) | (u64(le32(p + 4)) << 32); }

std::nullopt_t bad(std::string* error, const char* msg) {
  if (error) *error = msg;
  return std::nullopt;
}

// Подформат WAVE_FORMAT_EXTENSIBLE: {XXXXXXXX-0000-0010-8000-00AA00389B71}, где XXXXXXXX — обычный код формата.
bool ksSubformat(const u8* g, u16& tag) {
  static const u8 kTail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
  if (std::memcmp(g + 2, kTail, sizeof kTail) != 0) return false;
  tag = le16(g);
  return true;
}

void put16(std::vector<u8>& o, u32 v) {
  o.push_back(u8(v));
  o.push_back(u8(v >> 8));
}
void put32(std::vector<u8>& o, u32 v) {
  put16(o, v & 0xffff);
  put16(o, v >> 16);
}
void putTag(std::vector<u8>& o, const char* t) { o.insert(o.end(), t, t + 4); }

}  // namespace

ReadAt memoryReader(std::span<const u8> bytes) {
  return [bytes](u64 off, u8* dst, size_t n) -> size_t {
    if (off >= bytes.size()) return 0;
    size_t k = size_t(std::min<u64>(n, bytes.size() - off));
    std::memcpy(dst, bytes.data() + off, k);
    return k;
  };
}

std::optional<WavFormat> parseWav(const ReadAt& read, u64 size, std::string* error) {
  u8 h[12];
  if (size < 12 || read(0, h, 12) != 12) return bad(error, "файл WAV слишком короткий");
  const bool rf64 = std::memcmp(h, "RF64", 4) == 0;
  if ((std::memcmp(h, "RIFF", 4) != 0 && !rf64) || std::memcmp(h + 8, "WAVE", 4) != 0)
    return bad(error, "это не файл WAV");
  WavFormat f;
  bool haveFmt = false, haveData = false;
  u64 ds64Data = 0;
  u16 tag = 0;
  u64 off = 12;
  // Блоки по порядку; data может стоять раньше fmt — тогда поиск продолжается за данными.
  for (int guard = 0; off + 8 <= size && guard < 4096 && !(haveFmt && haveData); guard++) {
    u8 ch[8];
    if (read(off, ch, 8) != 8) break;
    u64 len = le32(ch + 4);
    const u64 body = off + 8;
    if (std::memcmp(ch, "ds64", 4) == 0 && len >= 16) {
      u8 b[16];
      if (read(body, b, 16) == 16) ds64Data = le64(b + 8);
    } else if (std::memcmp(ch, "fmt ", 4) == 0) {
      if (len < 16) return bad(error, "повреждён блок fmt файла WAV");
      u8 b[40] = {};
      size_t got = read(body, b, size_t(std::min<u64>(len, sizeof b)));
      if (got < 16) return bad(error, "повреждён блок fmt файла WAV");
      tag = le16(b);
      f.channels = le16(b + 2);
      f.rate = int(le32(b + 4));
      f.bits = le16(b + 14);
      if (tag == 0xFFFE) {
        if (got < 40 || !ksSubformat(b + 24, tag)) return bad(error, "неизвестный подформат WAV");
      }
      haveFmt = true;
    } else if (std::memcmp(ch, "data", 4) == 0) {
      f.dataOffset = body;
      u64 avail = size > body ? size - body : 0;
      if (rf64 && len == 0xFFFFFFFFu) len = ds64Data;
      // 0 и 0xFFFFFFFF пишут программы потоковой записи, не знающие длины заранее: данные — до конца файла.
      if (len == 0 || len == 0xFFFFFFFFu || len > avail) len = avail;
      f.dataBytes = len;
      haveData = true;
    }
    off = body + len + (len & 1);
  }
  if (!haveFmt) return bad(error, "в файле WAV нет блока fmt");
  if (!haveData) return bad(error, "в файле WAV нет звука (блок data)");
  if (tag == 1) {
    if (f.bits != 8 && f.bits != 16 && f.bits != 24 && f.bits != 32) return bad(error, "неподдерживаемая разрядность WAV");
  } else if (tag == 3) {
    if (f.bits != 32 && f.bits != 64) return bad(error, "неподдерживаемая разрядность WAV");
    f.isFloat = true;
  } else {
    return bad(error, "сжатый WAV не поддерживается");
  }
  if (f.channels < 1 || f.channels > 32) return bad(error, "неверное число каналов WAV");
  if (f.rate < 1000 || f.rate > 768000) return bad(error, "неверная частота WAV");
  f.blockAlign = f.channels * f.bits / 8;
  f.dataBytes -= f.dataBytes % u64(f.blockAlign);
  return f;
}

std::optional<WavFormat> parseWav(std::span<const u8> bytes, std::string* error) {
  return parseWav(memoryReader(bytes), bytes.size(), error);
}

void wavToFloat(const WavFormat& fmt, const u8* src, size_t frames, float* dst) {
  const size_t n = frames * size_t(fmt.channels);
  if (fmt.isFloat && fmt.bits == 32) {
    for (size_t i = 0; i < n; i++, src += 4) {
      u32 u = le32(src);
      float v;
      std::memcpy(&v, &u, 4);
      dst[i] = std::isfinite(v) ? v : 0.f;
    }
  } else if (fmt.isFloat) {
    for (size_t i = 0; i < n; i++, src += 8) {
      u64 u = le64(src);
      double v;
      std::memcpy(&v, &u, 8);
      dst[i] = std::isfinite(v) ? float(v) : 0.f;
    }
  } else if (fmt.bits == 8) {
    for (size_t i = 0; i < n; i++) dst[i] = float(int(src[i]) - 128) * (1.f / 128);
  } else if (fmt.bits == 16) {
    for (size_t i = 0; i < n; i++, src += 2) dst[i] = float(i16(le16(src))) * (1.f / 32768);
  } else if (fmt.bits == 24) {
    for (size_t i = 0; i < n; i++, src += 3) {
      i32 v = i32(u32(src[0]) << 8 | u32(src[1]) << 16 | u32(src[2]) << 24) >> 8;
      dst[i] = float(v) * (1.f / 8388608);
    }
  } else {
    for (size_t i = 0; i < n; i++, src += 4) dst[i] = float(double(i32(le32(src))) * (1.0 / 2147483648.0));
  }
}

std::optional<PcmAudio> decodeWav(std::span<const u8> bytes, std::string* error) {
  auto f = parseWav(bytes, error);
  if (!f) return std::nullopt;
  PcmAudio pcm;
  pcm.channels = f->channels;
  pcm.rate = f->rate;
  pcm.samples.resize(size_t(f->frames()) * size_t(f->channels));
  wavToFloat(*f, bytes.data() + f->dataOffset, size_t(f->frames()), pcm.samples.data());
  return pcm;
}

std::vector<u8> encodeWav(const PcmAudio& pcm, WavSample sample, std::string_view title, std::string_view artist) {
  const int ch = std::max(1, pcm.channels);
  const int bits = sample == WavSample::Pcm16 ? 16 : sample == WavSample::Pcm24 ? 24 : 32;
  const u32 block = u32(ch * bits / 8);
  const size_t frames = pcm.samples.size() / size_t(ch);
  const u32 dataBytes = u32(frames * block);
  std::vector<u8> info;   // LIST/INFO
  auto infoItem = [&](const char* id, std::string_view s) {
    if (s.empty()) return;
    putTag(info, id);
    put32(info, u32(s.size() + 1));
    info.insert(info.end(), s.begin(), s.end());
    info.push_back(0);
    if ((s.size() + 1) & 1) info.push_back(0);
  };
  infoItem("INAM", title);
  infoItem("IART", artist);
  std::vector<u8> o;
  o.reserve(64 + info.size() + dataBytes);
  putTag(o, "RIFF");
  put32(o, 0);   // размер — в конце
  putTag(o, "WAVE");
  putTag(o, "fmt ");
  const bool flt = sample == WavSample::Float32;
  put32(o, flt ? 18 : 16);
  put16(o, flt ? 3 : 1);
  put16(o, u32(ch));
  put32(o, u32(pcm.rate));
  put32(o, u32(pcm.rate) * block);
  put16(o, block);
  put16(o, u32(bits));
  if (flt) {
    put16(o, 0);
    putTag(o, "fact");
    put32(o, 4);
    put32(o, u32(frames));
  }
  if (!info.empty()) {
    putTag(o, "LIST");
    put32(o, u32(4 + info.size()));
    putTag(o, "INFO");
    o.insert(o.end(), info.begin(), info.end());
  }
  putTag(o, "data");
  put32(o, dataBytes);
  for (float v : pcm.samples) {
    if (!std::isfinite(v)) v = 0;
    if (flt) {
      u32 u;
      std::memcpy(&u, &v, 4);
      put32(o, u);
    } else if (bits == 16) {
      put16(o, u32(i32(std::lround(clamp(v, -1.f, 1.f) * 32767.f))) & 0xffff);
    } else {
      u32 u = u32(i32(std::lround(clamp(v, -1.f, 1.f) * 8388607.f)));
      o.push_back(u8(u));
      o.push_back(u8(u >> 8));
      o.push_back(u8(u >> 16));
    }
  }
  if (dataBytes & 1) o.push_back(0);
  const u32 riff = u32(o.size() - 8);
  o[4] = u8(riff);
  o[5] = u8(riff >> 8);
  o[6] = u8(riff >> 16);
  o[7] = u8(riff >> 24);
  return o;
}

}  // namespace rg::codec
