// Regnum — сведения о звуковом файле: разбор контейнеров и тегов без декодирования звука.
#include "codec/audioinfo.h"

#include "codec/zlib.h"

namespace rg::codec {

namespace {

u16 be16(const u8* p) { return u16((p[0] << 8) | p[1]); }
u32 be24(const u8* p) { return (u32(p[0]) << 16) | (u32(p[1]) << 8) | p[2]; }
u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
u64 be64(const u8* p) { return (u64(be32(p)) << 32) | be32(p + 4); }
u16 le16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
u32 le32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
u64 le64(const u8* p) { return u64(le32(p)) | (u64(le32(p + 4)) << 32); }
u32 syncsafe(const u8* p) { return (u32(p[0] & 0x7f) << 21) | (u32(p[1] & 0x7f) << 14) | (u32(p[2] & 0x7f) << 7) | (p[3] & 0x7f); }

constexpr size_t kMaxText = 1 << 20;        // предел текста тега (защита от мусора)
constexpr size_t kMaxTag = 16 << 20;        // предел тега ID3v2, читаемого целиком (рассинхронизация)

// Файл: чтение кусков с проверкой границ.
struct Src {
  const ReadAt& read;
  u64 size;
  bool at(u64 off, u8* dst, size_t n) const { return off <= size && n <= size - off && read(off, dst, n) == n; }
  std::vector<u8> bytes(u64 off, size_t n) const {
    std::vector<u8> b;
    if (off >= size) return b;
    b.resize(size_t(std::min<u64>(n, size - off)));
    b.resize(read(off, b.data(), b.size()));
    return b;
  }
};

// Найденные теги: первое непустое значение каждого поля.
struct Tags {
  std::string title, artist, albumArtist;
  void set(std::string& field, std::string v) {
    if (field.empty()) field = std::move(v);
  }
};

// ---------------------------------------------------------------- текст
constexpr u16 kCp1251[64] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};

// Значение тега: без управляющих символов, пробелы по краям сжаты.
std::string clean(std::string s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(u8(c) < 0x20 ? ' ' : c);
  return trim(out);
}

std::string utf16(const u8* p, size_t n, bool bigEndian) {
  std::string out;
  size_t i = 0;
  if (n >= 2 && ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF))) {
    bigEndian = p[0] == 0xFE;
    i = 2;
  }
  for (; i + 1 < n; i += 2) {
    u32 u = bigEndian ? be16(p + i) : le16(p + i);
    if (u == 0) break;
    if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {
      u32 lo = bigEndian ? be16(p + i + 2) : le16(p + i + 2);
      if (lo >= 0xDC00 && lo < 0xE000) {
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      } else {
        u = 0xFFFD;
      }
    } else if (u >= 0xD800 && u < 0xE000) {
      u = 0xFFFD;
    }
    utf8::append(out, u);
  }
  return clean(std::move(out));
}

std::string utf8z(const u8* p, size_t n) {   // UTF-8 до нуля; некорректный — как однобайтовый
  size_t k = 0;
  while (k < n && p[k]) k++;
  std::string_view s(reinterpret_cast<const char*>(p), k);
  return utf8::valid(s) ? clean(std::string(s)) : tagText8(s);
}

// Строки Vorbis comment: «КЛЮЧ=значение», ключ без учёта регистра.
void vorbisComment(const u8* p, size_t n, Tags& t) {
  if (n < 8) return;
  size_t pos = 0;
  u32 vendor = le32(p);
  if (vendor > n - 4) return;
  pos = 4 + vendor;
  if (pos + 4 > n) return;
  u32 count = le32(p + pos);
  pos += 4;
  for (u32 i = 0; i < count && pos + 4 <= n; i++) {
    u32 len = le32(p + pos);
    pos += 4;
    if (len > n - pos) break;   // обрезанный пакет: что успели — то и есть
    std::string_view s(reinterpret_cast<const char*>(p + pos), len);
    pos += len;
    size_t eq = s.find('=');
    if (eq == std::string_view::npos || len > kMaxText) continue;
    std::string key = utf8::upper(s.substr(0, eq));
    std::string val = utf8z(reinterpret_cast<const u8*>(s.data() + eq + 1), s.size() - eq - 1);
    if (val.empty()) continue;
    if (key == "TITLE") t.set(t.title, val);
    else if (key == "ARTIST") t.set(t.artist, val);
    else if (key == "ALBUMARTIST" || key == "ALBUM ARTIST") t.set(t.albumArtist, val);
  }
}

// ---------------------------------------------------------------- ID3
// Текстовый кадр ID3v2: байт кодировки и строка (у v2.4 — первое из значений через ноль).
std::string id3Text(const std::vector<u8>& b) {
  if (b.size() < 2) return {};
  const u8* p = b.data() + 1;
  const size_t n = b.size() - 1;
  switch (b[0]) {
    case 0: {
      size_t k = 0;
      while (k < n && p[k]) k++;
      return tagText8(std::string_view(reinterpret_cast<const char*>(p), k));
    }
    case 1: return utf16(p, n, false);
    case 2: return utf16(p, n, true);
    case 3: return utf8z(p, n);
    default: return {};
  }
}

void unsync(std::vector<u8>& b) {   // 0xFF 0x00 → 0xFF
  size_t w = 0;
  for (size_t r = 0; r < b.size(); r++) {
    b[w++] = b[r];
    if (b[r] == 0xFF && r + 1 < b.size() && b[r + 1] == 0) r++;
  }
  b.resize(w);
}

bool frameIdOk(const u8* id, int n) {
  for (int i = 0; i < n; i++)
    if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))) return false;
  return true;
}

// Тег ID3v2 с позиции at: поля в t; вернуть полный размер тега (0 — тега нет).
u64 readId3v2(const Src& s, u64 at, Tags& t) {
  u8 h[10];
  if (!s.at(at, h, 10) || std::memcmp(h, "ID3", 3) != 0) return 0;
  const int ver = h[3];
  if (ver < 2 || ver > 4 || h[4] == 0xFF || (h[6] | h[7] | h[8] | h[9]) & 0x80) return 0;
  const u8 flags = h[5];
  const u32 size = syncsafe(h + 6);
  const u64 total = 10 + u64(size) + (ver == 4 && (flags & 0x10) ? 10 : 0);
  if (ver == 2 && (flags & 0x40)) return total;   // сжатие ID3v2.2 так и не было определено
  // Рассинхронизация всего тега (v2.2, v2.3) — тег читается целиком; иначе кадры читаются по одному (обложки
  // в несколько мегабайт не загружаются).
  std::vector<u8> whole;
  const bool all = (flags & 0x80) && ver < 4;
  if (all) {
    whole = s.bytes(at + 10, std::min<size_t>(size, kMaxTag));
    unsync(whole);
  }
  const u64 bodyLen = all ? whole.size() : std::min<u64>(size, s.size > at + 10 ? s.size - at - 10 : 0);
  auto fetch = [&](u64 off, size_t n) -> std::vector<u8> {
    if (all) {
      if (off >= whole.size()) return {};
      return std::vector<u8>(whole.begin() + std::ptrdiff_t(off), whole.begin() + std::ptrdiff_t(std::min<u64>(whole.size(), off + n)));
    }
    return off < bodyLen ? s.bytes(at + 10 + off, size_t(std::min<u64>(n, bodyLen - off))) : std::vector<u8>{};
  };
  u64 pos = 0;
  if ((flags & 0x40) && ver >= 3) {   // расширенный заголовок
    std::vector<u8> e = fetch(0, 4);
    if (e.size() < 4) return total;
    pos = ver == 3 ? 4 + u64(be32(e.data())) : u64(syncsafe(e.data()));
  }
  const int hdr = ver == 2 ? 6 : 10;
  for (int guard = 0; pos + u64(hdr) <= bodyLen && guard < 10000; guard++) {
    std::vector<u8> fh = fetch(pos, size_t(hdr));
    if (fh.size() < size_t(hdr) || fh[0] == 0) break;   // заполнение
    if (!frameIdOk(fh.data(), ver == 2 ? 3 : 4)) break;
    u64 len;
    if (ver == 2) len = be24(fh.data() + 3);
    else if (ver == 3 || (fh[4] | fh[5] | fh[6] | fh[7]) & 0x80) len = be32(fh.data() + 4);   // v2.4 с размером не syncsafe
    else len = syncsafe(fh.data() + 4);
    const std::string id(reinterpret_cast<const char*>(fh.data()), ver == 2 ? 3 : 4);
    const u64 body = pos + u64(hdr);
    pos = body + len;
    if (pos > bodyLen) break;
    std::string* field = nullptr;
    if (id == "TIT2" || id == "TT2") field = &t.title;
    else if (id == "TPE1" || id == "TP1") field = &t.artist;
    else if (id == "TPE2" || id == "TP2") field = &t.albumArtist;
    if (!field || !field->empty() || len < 2 || len > kMaxText) continue;
    std::vector<u8> b = fetch(body, size_t(len));
    const u8 ff = ver == 2 ? 0 : fh[9];
    bool zip = false;
    if (ver == 3) {
      if (ff & 0x40) continue;   // зашифрован
      size_t skip = (ff & 0x80 ? 4 : 0) + (ff & 0x20 ? 1 : 0);
      zip = ff & 0x80;
      if (skip >= b.size()) continue;
      b.erase(b.begin(), b.begin() + std::ptrdiff_t(skip));
    } else if (ver == 4) {
      if (ff & 0x04) continue;
      size_t skip = (ff & 0x40 ? 1 : 0) + (ff & 0x01 ? 4 : 0);
      if (skip >= b.size()) continue;
      b.erase(b.begin(), b.begin() + std::ptrdiff_t(skip));
      if (ff & 0x02) unsync(b);
      zip = ff & 0x08;
    }
    if (zip) {
      InflateOptions o;
      o.maxOutput = kMaxText;
      auto plain = inflate(std::span<const u8>(b), ZFormat::Zlib, nullptr, o);
      if (!plain) continue;
      b = std::move(*plain);
    }
    std::string v = id3Text(b);
    if (!v.empty()) *field = std::move(v);
  }
  return total;
}

void readId3v1(const Src& s, Tags& t) {
  u8 b[128];
  if (s.size < 128 || !s.at(s.size - 128, b, 128) || std::memcmp(b, "TAG", 3) != 0) return;
  auto field = [&](int off) {
    size_t k = 0;
    while (k < 30 && b[off + k]) k++;
    return tagText8(std::string_view(reinterpret_cast<const char*>(b + off), k));
  };
  t.set(t.title, field(3));
  t.set(t.artist, field(33));
}

// ---------------------------------------------------------------- MP3
struct Mpeg {
  int version = 0;      // 3 — MPEG-1, 2 — MPEG-2, 0 — MPEG-2.5
  int layer = 0;        // 1, 2, 3
  int kbps = 0, rate = 0, spf = 0, len = 0;
  bool mono = false;
};

bool mpegHeader(const u8* h, Mpeg& m) {
  if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
  const int ver = (h[1] >> 3) & 3, lay = (h[1] >> 1) & 3, bri = h[2] >> 4, sri = (h[2] >> 2) & 3;
  if (ver == 1 || lay == 0 || bri == 0 || bri == 15 || sri == 3) return false;
  static const int kBr[2][3][15] = {
      {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
       {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
       {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}},
      {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}}};
  static const int kRate[3] = {44100, 48000, 32000};
  m.version = ver;
  m.layer = 4 - lay;
  m.kbps = kBr[ver == 3 ? 0 : 1][m.layer - 1][bri];
  m.rate = kRate[sri] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
  m.spf = m.layer == 1 ? 384 : (m.layer == 2 || ver == 3) ? 1152 : 576;
  const int pad = (h[2] >> 1) & 1;
  m.len = m.layer == 1 ? (12 * m.kbps * 1000 / m.rate + pad) * 4 : m.spf / 8 * m.kbps * 1000 / m.rate + pad;
  m.mono = (h[3] >> 6) == 3;
  return m.len >= 21;
}

// Первый кадр MPEG от start (до 64 КБ мусора): два кадра подряд с одинаковым видом — не случайное совпадение.
std::optional<std::pair<u64, Mpeg>> firstMpeg(const Src& s, u64 start) {
  std::vector<u8> b = s.bytes(start, 65536 + 4);
  for (size_t i = 0; i + 4 <= b.size(); i++) {
    Mpeg m;
    if (b[i] != 0xFF || !mpegHeader(&b[i], m)) continue;
    const u64 off = start + i, next = off + u64(m.len);
    if (next + 4 > s.size) {
      if (next <= s.size) return std::make_pair(off, m);   // единственный кадр файла
      continue;
    }
    u8 nh[4];
    Mpeg m2;
    if (s.at(next, nh, 4) && mpegHeader(nh, m2) && m2.version == m.version && m2.layer == m.layer && m2.rate == m.rate)
      return std::make_pair(off, m);
  }
  return std::nullopt;
}

void probeMp3(const Src& s, u64 start, AudioInfo& info) {
  auto first = firstMpeg(s, start);
  if (!first) return;
  const auto [off, m] = *first;
  info.kind = "mp3";
  info.rate = m.rate;
  info.channels = m.mono ? 1 : 2;
  // Xing/Info (LAME) — число кадров; VBRI (Fraunhofer) — тоже.
  const int side = m.version == 3 ? (m.mono ? 17 : 32) : (m.mono ? 9 : 17);
  u8 x[18];   // Xing — 12 байт, VBRI — 18 (число кадров в байтах 14…17)
  if (s.at(off + 4 + u64(side), x, 12) && (std::memcmp(x, "Xing", 4) == 0 || std::memcmp(x, "Info", 4) == 0) && (be32(x + 4) & 1)) {
    info.duration = double(be32(x + 8)) * m.spf / m.rate;
    return;
  }
  if (s.at(off + 36, x, 18) && std::memcmp(x, "VBRI", 4) == 0) {
    info.duration = double(be32(x + 14)) * m.spf / m.rate;
    return;
  }
  u64 end = s.size;
  u8 tag[3];
  if (s.size >= 128 && s.at(s.size - 128, tag, 3) && std::memcmp(tag, "TAG", 3) == 0) end -= 128;
  if (end > off && m.kbps > 0) info.duration = double(end - off) * 8.0 / (m.kbps * 1000.0);
}

// ---------------------------------------------------------------- AAC (ADTS)
bool adtsHeader(const u8* p, int& rate, int& channels, int& len, int& samples) {
  static const int kRates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
  if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0) return false;
  const int sri = (p[2] >> 2) & 0xF;
  if (sri >= 13) return false;
  rate = kRates[sri];
  channels = ((p[2] & 1) << 2) | (p[3] >> 6);
  len = ((p[3] & 3) << 11) | (p[4] << 3) | (p[5] >> 5);
  samples = 1024 * ((p[6] & 3) + 1);
  return len >= 7;
}

void probeAdts(const Src& s, u64 start, AudioInfo& info) {
  u64 off = start, bytes = 0, samples = 0;
  int rate = 0, channels = 0;
  int frames = 0;
  for (; frames < 400; frames++) {
    u8 h[7];
    int r, c, len, n;
    if (!s.at(off, h, 7) || !adtsHeader(h, r, c, len, n)) break;
    if (!rate) {
      rate = r;
      channels = c;
    }
    off += u64(len);
    bytes += u64(len);
    samples += u64(n);
  }
  if (frames < 1 || !rate) return;
  info.kind = "aac";
  info.rate = rate;
  info.channels = channels;
  double sec = double(samples) / rate;
  if (off < s.size && bytes > 0) sec *= double(s.size - start) / double(bytes);   // по средней длине кадра
  info.duration = sec;
}

// ---------------------------------------------------------------- WAV
void probeWav(const Src& s, AudioInfo& info, Tags& t) {
  auto f = parseWav(s.read, s.size);
  if (!f) return;
  info.kind = "wav";
  info.rate = f->rate;
  info.channels = f->channels;
  info.duration = f->seconds();
  u64 off = 12;
  for (int guard = 0; off + 8 <= s.size && guard < 4096; guard++) {
    u8 ch[12];
    if (!s.at(off, ch, 8)) break;
    const u64 len = le32(ch + 4), body = off + 8;
    if (std::memcmp(ch, "LIST", 4) == 0 && len >= 4 && len <= kMaxText) {
      std::vector<u8> b = s.bytes(body, size_t(len));
      if (b.size() >= 4 && std::memcmp(b.data(), "INFO", 4) == 0) {
        for (size_t p = 4; p + 8 <= b.size();) {
          const size_t n = le32(&b[p + 4]);
          if (n > b.size() - p - 8) break;
          size_t k = 0;
          while (k < n && b[p + 8 + k]) k++;
          std::string v = tagText8(std::string_view(reinterpret_cast<const char*>(&b[p + 8]), k));
          if (std::memcmp(&b[p], "INAM", 4) == 0) t.set(t.title, v);
          else if (std::memcmp(&b[p], "IART", 4) == 0) t.set(t.artist, v);
          p += 8 + n + (n & 1);
        }
      }
    } else if (std::memcmp(ch, "id3 ", 4) == 0 || std::memcmp(ch, "ID3 ", 4) == 0) {
      readId3v2(s, body, t);
    } else if (std::memcmp(ch, "data", 4) == 0 && body >= f->dataOffset) {
      off = f->dataOffset + f->dataBytes;
      off += off & 1;
      continue;
    }
    off = body + len + (len & 1);
  }
}

// ---------------------------------------------------------------- FLAC
void probeFlac(const Src& s, u64 start, AudioInfo& info, Tags& t) {
  u64 off = start + 4;
  info.kind = "flac";
  for (int guard = 0; guard < 128; guard++) {
    u8 h[4];
    if (!s.at(off, h, 4)) break;
    const bool last = h[0] & 0x80;
    const int type = h[0] & 0x7f;
    const u32 len = be24(h + 1);
    if (type == 0 && len >= 34) {
      u8 b[34];
      if (s.at(off + 4, b, 34)) {
        info.rate = int((u32(b[10]) << 12) | (u32(b[11]) << 4) | (b[12] >> 4));
        info.channels = ((b[12] >> 1) & 7) + 1;
        const u64 total = (u64(b[13] & 0x0f) << 32) | be32(b + 14);
        if (info.rate > 0) info.duration = double(total) / info.rate;
      }
    } else if (type == 4) {
      std::vector<u8> b = s.bytes(off + 4, std::min<size_t>(len, kMaxText));
      vorbisComment(b.data(), b.size(), t);
    }
    off += 4 + u64(len);
    if (last) break;
  }
}

// ---------------------------------------------------------------- Ogg
void probeOgg(const Src& s, AudioInfo& info, Tags& t) {
  std::vector<u8> b = s.bytes(0, 512 << 10);
  std::vector<std::vector<u8>> packets(1);
  u32 serial = 0;
  bool haveSerial = false;
  for (size_t p = 0; p + 27 <= b.size() && packets.size() <= 2;) {
    if (std::memcmp(&b[p], "OggS", 4) != 0) break;
    const size_t nseg = b[p + 26], data = p + 27 + nseg;
    if (data > b.size()) break;
    const u32 sn = le32(&b[p + 14]);
    if (!haveSerial) {
      serial = sn;
      haveSerial = true;
    }
    size_t q = data;
    for (size_t i = 0; i < nseg; i++) {
      const size_t lace = b[p + 27 + i];
      if (sn == serial && packets.size() <= 2) {
        auto& pk = packets.back();
        if (q < b.size()) {
          const size_t k = std::min(lace, b.size() - q);
          pk.insert(pk.end(), b.begin() + std::ptrdiff_t(q), b.begin() + std::ptrdiff_t(q + k));
        }
        if (lace < 255) packets.emplace_back();
      }
      q += lace;
    }
    p = q;
  }
  // Пакет комментариев мог не поместиться в прочитанное — разбирается, что есть.
  const std::vector<u8>& id = packets[0];
  int preskip = 0;
  if (id.size() >= 16 && std::memcmp(id.data(), "\x01vorbis", 7) == 0) {
    info.kind = "ogg";
    info.channels = id[11];
    info.rate = int(le32(&id[12]));
  } else if (id.size() >= 19 && std::memcmp(id.data(), "OpusHead", 8) == 0) {
    info.kind = "opus";
    info.channels = id[9];
    preskip = le16(&id[10]);
    info.rate = 48000;
  } else {
    return;
  }
  if (packets.size() >= 2) {
    const std::vector<u8>& c = packets[1];
    if (info.kind == "ogg" && c.size() > 7 && std::memcmp(c.data(), "\x03vorbis", 7) == 0) vorbisComment(c.data() + 7, c.size() - 7, t);
    if (info.kind == "opus" && c.size() > 8 && std::memcmp(c.data(), "OpusTags", 8) == 0) vorbisComment(c.data() + 8, c.size() - 8, t);
  }
  // Длительность — по номеру отсчёта последней страницы этого потока.
  const u64 tailLen = std::min<u64>(s.size, 65536);
  std::vector<u8> tail = s.bytes(s.size - tailLen, size_t(tailLen));
  for (size_t p = tail.size() >= 27 ? tail.size() - 26 : 0; p-- > 0;) {
    if (std::memcmp(&tail[p], "OggS", 4) != 0 || le32(&tail[p + 14]) != serial) continue;
    const u64 g = le64(&tail[p + 6]);
    if (g == ~u64(0)) continue;
    const double frames = double(g) - preskip;
    if (frames > 0 && info.rate > 0) info.duration = frames / info.rate;
    break;
  }
}

// ---------------------------------------------------------------- MP4
struct Box {
  u64 off = 0, size = 0, hdr = 8;
  char type[4]{};
  u64 body() const { return off + hdr; }
  u64 end() const { return off + size; }
  bool is(const char* t) const { return std::memcmp(type, t, 4) == 0; }
};

bool readBox(const Src& s, u64 off, u64 end, Box& b) {
  u8 h[16];
  if (off + 8 > end || !s.at(off, h, 8)) return false;
  b.off = off;
  b.size = be32(h);
  std::memcpy(b.type, h + 4, 4);
  b.hdr = 8;
  if (b.size == 1) {
    if (!s.at(off + 8, h + 8, 8)) return false;
    b.size = be64(h + 8);
    b.hdr = 16;
  } else if (b.size == 0) {
    b.size = end - off;
  }
  return b.size >= b.hdr && b.size <= end - off;
}

template <class F>
void eachBox(const Src& s, u64 from, u64 to, F&& fn) {
  Box b;
  for (int guard = 0; guard < 4096 && readBox(s, from, to, b); guard++) {
    fn(b);
    from = b.end();
  }
}

std::string mp4Text(const Src& s, const Box& item) {
  std::string out;
  eachBox(s, item.body(), item.end(), [&](const Box& d) {
    if (!out.empty() || !d.is("data") || d.size < d.hdr + 8) return;
    std::vector<u8> b = s.bytes(d.body(), size_t(std::min<u64>(d.size - d.hdr, 4096)));
    if (b.size() < 8) return;
    const u32 type = be32(b.data()) & 0xFFFFFF;
    if (type == 1) out = utf8z(b.data() + 8, b.size() - 8);
    else if (type == 2) out = utf16(b.data() + 8, b.size() - 8, true);
  });
  return out;
}

void probeMp4(const Src& s, AudioInfo& info, Tags& t) {
  info.kind = "m4a";
  eachBox(s, 0, s.size, [&](const Box& moov) {
    if (!moov.is("moov")) return;
    eachBox(s, moov.body(), moov.end(), [&](const Box& b) {
      if (b.is("mvhd")) {
        u8 h[32];
        if (!s.at(b.body(), h, 32)) return;
        const bool v1 = h[0] == 1;
        const u32 scale = v1 ? be32(h + 20) : be32(h + 12);
        const u64 dur = v1 ? be64(h + 24) : be32(h + 16);
        if (scale > 0 && dur != ~u64(0) && (v1 || dur != 0xFFFFFFFFu)) info.duration = double(dur) / scale;
      } else if (b.is("udta")) {
        eachBox(s, b.body(), b.end(), [&](const Box& meta) {
          if (!meta.is("meta")) return;
          // ISO: meta — полный блок (версия и флаги); у QuickTime сразу дочерние блоки.
          u8 q[8];
          u64 from = meta.body();
          if (s.at(from, q, 8) && std::memcmp(q + 4, "hdlr", 4) != 0) from += 4;
          eachBox(s, from, meta.end(), [&](const Box& ilst) {
            if (!ilst.is("ilst")) return;
            eachBox(s, ilst.body(), ilst.end(), [&](const Box& item) {
              if (item.is("\xA9nam")) t.set(t.title, mp4Text(s, item));
              else if (item.is("\xA9" "ART")) t.set(t.artist, mp4Text(s, item));
              else if (item.is("aART")) t.set(t.albumArtist, mp4Text(s, item));
            });
          });
        });
      }
    });
  });
}

// ---------------------------------------------------------------- ASF (WMA)
const u8 kAsfHeader[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
const u8 kAsfFileProps[16] = {0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11, 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
const u8 kAsfContent[16] = {0x33, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};

void probeAsf(const Src& s, AudioInfo& info, Tags& t) {
  u8 h[30];
  if (!s.at(0, h, 30)) return;
  info.kind = "wma";
  const u64 headerEnd = std::min<u64>(le64(h + 16), s.size);
  u64 off = 30;
  for (int guard = 0; off + 24 <= headerEnd && guard < 1024; guard++) {
    u8 o[24];
    if (!s.at(off, o, 24)) break;
    const u64 size = le64(o + 16);
    if (size < 24 || size > headerEnd - off) break;
    if (std::memcmp(o, kAsfFileProps, 16) == 0 && size >= 104) {
      u8 b[104];
      if (s.at(off, b, 104) && !(le32(b + 88) & 1)) {   // не трансляция
        const double sec = double(le64(b + 64)) / 1e7 - double(le64(b + 80)) / 1e3;
        if (sec > 0) info.duration = sec;
      }
    } else if (std::memcmp(o, kAsfContent, 16) == 0 && size >= 34 && size <= kMaxText) {
      std::vector<u8> b = s.bytes(off, size_t(size));
      if (b.size() == size) {
        const size_t lt = le16(&b[24]), la = le16(&b[26]);
        if (34 + lt + la <= b.size()) {
          t.set(t.title, utf16(&b[34], lt, false));
          t.set(t.artist, utf16(&b[34 + lt], la, false));
        }
      }
    }
    off += size;
  }
}

// ---------------------------------------------------------------- AIFF
double ext80(const u8* p) {
  const int e = ((p[0] & 0x7f) << 8) | p[1];
  const u64 m = be64(p + 2);
  if (e == 0 && m == 0) return 0;
  const double v = std::ldexp(double(m), e - 16383 - 63);
  return p[0] & 0x80 ? -v : v;
}

void probeAiff(const Src& s, AudioInfo& info, Tags& t) {
  info.kind = "aiff";
  u64 off = 12;
  for (int guard = 0; off + 8 <= s.size && guard < 4096; guard++) {
    u8 ch[8];
    if (!s.at(off, ch, 8)) break;
    const u64 len = be32(ch + 4), body = off + 8;
    if (std::memcmp(ch, "COMM", 4) == 0 && len >= 18) {
      u8 b[18];
      if (s.at(body, b, 18)) {
        info.channels = be16(b);
        const double rate = ext80(b + 8);
        info.rate = rate > 0 && rate < 1e7 ? int(std::lround(rate)) : 0;
        if (rate > 0) info.duration = double(be32(b + 2)) / rate;
      }
    } else if ((std::memcmp(ch, "NAME", 4) == 0 || std::memcmp(ch, "AUTH", 4) == 0) && len <= 4096) {
      std::vector<u8> b = s.bytes(body, size_t(len));
      std::string v = tagText8(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
      t.set(ch[0] == 'N' ? t.title : t.artist, v);
    } else if (std::memcmp(ch, "ID3 ", 4) == 0 || std::memcmp(ch, "id3 ", 4) == 0) {
      readId3v2(s, body, t);
    }
    off = body + len + (len & 1);
  }
}

}  // namespace

std::string tagText8(std::string_view b) {
  while (!b.empty() && (b.back() == 0 || b.back() == ' ')) b.remove_suffix(1);
  while (!b.empty() && (b.front() == 0 || b.front() == ' ')) b.remove_prefix(1);
  if (utf8::valid(b)) return clean(std::string(b));
  int cyr = 0, letters = 0;
  for (char ch : b) {
    const u8 c = u8(ch);
    if (c >= 0xC0 || c == 0xA8 || c == 0xB8) cyr++;
    if (c >= 0x80 || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letters++;
  }
  const bool cp1251 = cyr > 0 && cyr * 2 >= letters;
  std::string out;
  out.reserve(b.size() * 2);
  for (char ch : b) {
    const u8 c = u8(ch);
    if (c < 0x80) out.push_back(ch);
    else if (!cp1251) utf8::append(out, c);
    else if (c >= 0xC0) utf8::append(out, 0x0410 + (c - 0xC0));
    else utf8::append(out, kCp1251[c - 0x80]);
  }
  return clean(std::move(out));
}

AudioInfo probeAudio(const ReadAt& read, u64 size) {
  Src s{read, size};
  AudioInfo info;
  Tags t;
  u64 start = 0;
  for (int i = 0; i < 4; i++) {   // тег ID3v2 впереди (бывает и несколько подряд)
    const u64 n = readId3v2(s, start, t);
    if (!n || start + n >= size) break;
    start += n;
  }
  u8 h[16] = {};
  const bool head = s.at(0, h, 16);
  u8 g[8] = {};
  s.at(start, g, 8);
  if (head && (std::memcmp(h, "RIFF", 4) == 0 || std::memcmp(h, "RF64", 4) == 0) && std::memcmp(h + 8, "WAVE", 4) == 0) {
    probeWav(s, info, t);
  } else if (std::memcmp(g, "fLaC", 4) == 0) {
    probeFlac(s, start, info, t);
  } else if (head && std::memcmp(h, "OggS", 4) == 0) {
    probeOgg(s, info, t);
  } else if (head && std::memcmp(h, "FORM", 4) == 0 && (std::memcmp(h + 8, "AIFF", 4) == 0 || std::memcmp(h + 8, "AIFC", 4) == 0)) {
    probeAiff(s, info, t);
  } else if (head && std::memcmp(h, kAsfHeader, 16) == 0) {
    probeAsf(s, info, t);
  } else if (head && std::memcmp(h + 4, "ftyp", 4) == 0) {
    probeMp4(s, info, t);
  } else if (g[0] == 0xFF && (g[1] & 0xF6) == 0xF0) {
    probeAdts(s, start, info);
    if (!info.kind.empty()) readId3v1(s, t);
  } else {
    probeMp3(s, start, info);
    if (!info.kind.empty()) readId3v1(s, t);
  }
  if (info.kind.empty()) return AudioInfo{};
  if (!std::isfinite(info.duration) || info.duration < 0 || info.duration > 360000) info.duration = 0;
  info.title = t.title;
  info.artist = !t.artist.empty() ? t.artist : t.albumArtist;
  return info;
}

AudioInfo probeAudio(std::span<const u8> bytes) { return probeAudio(memoryReader(bytes), bytes.size()); }

}  // namespace rg::codec
