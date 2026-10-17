// Тесты сведений о звуковом файле: вид по сигнатуре, длительность и теги MP3 (ID3v2.2–2.4, ID3v1, Xing, CBR), WAV,
// FLAC, Ogg Vorbis и Opus, MP4, WMA, AIFF, AAC (ADTS); однобайтовые теги (Windows-1251 и Latin-1); устойчивость к порче.
#include "codec/audioinfo.h"
#include "codec/zlib.h"
#include "tests/test.h"

using namespace rg;
using namespace rg::codec;

namespace {

using Bytes = std::vector<u8>;

void add(Bytes& o, std::string_view s) { o.insert(o.end(), s.begin(), s.end()); }
void add(Bytes& o, const Bytes& b) { o.insert(o.end(), b.begin(), b.end()); }
void be16(Bytes& o, u32 v) {
  o.push_back(u8(v >> 8));
  o.push_back(u8(v));
}
void be32(Bytes& o, u32 v) {
  be16(o, v >> 16);
  be16(o, v & 0xffff);
}
void le16(Bytes& o, u32 v) {
  o.push_back(u8(v));
  o.push_back(u8(v >> 8));
}
void le32(Bytes& o, u32 v) {
  le16(o, v & 0xffff);
  le16(o, v >> 16);
}
void le64(Bytes& o, u64 v) {
  le32(o, u32(v));
  le32(o, u32(v >> 32));
}
void syncsafe(Bytes& o, u32 v) {
  for (int s : {21, 14, 7, 0}) o.push_back(u8((v >> s) & 0x7f));
}
Bytes utf16le(std::string_view s, bool bom) {
  Bytes o;
  if (bom) o = {0xFF, 0xFE};
  for (char16_t c : utf8::toWide(s)) le16(o, u32(c));
  return o;
}

// Кадр ID3v2.3 (размер обычный) или v2.4 (syncsafe).
Bytes frame(int ver, const char* id, const Bytes& body, u16 flags = 0) {
  Bytes o;
  add(o, std::string_view(id, 4));
  if (ver == 4) syncsafe(o, u32(body.size()));
  else be32(o, u32(body.size()));
  be16(o, flags);
  add(o, body);
  return o;
}
Bytes textFrame(int ver, const char* id, u8 enc, const Bytes& text) {
  Bytes b{enc};
  add(b, text);
  return frame(ver, id, b);
}
Bytes id3(int ver, const Bytes& frames, size_t padding = 64) {
  Bytes o;
  add(o, "ID3");
  o.push_back(u8(ver));
  o.push_back(0);
  o.push_back(0);
  syncsafe(o, u32(frames.size() + padding));
  add(o, frames);
  o.resize(o.size() + padding, 0);
  return o;
}
Bytes cp1251(std::initializer_list<u8> b) { return Bytes(b); }

// Кадры MPEG-1 Layer III 128 кбит/с 44,1 кГц стерео (417 байт); в первом — заголовок Xing с числом кадров.
Bytes mpegFrames(int count, int xingFrames) {
  Bytes o;
  for (int i = 0; i < count; i++) {
    Bytes f(417, 0);
    f[0] = 0xFF;
    f[1] = 0xFB;
    f[2] = 0x90;
    f[3] = 0x00;
    if (i == 0 && xingFrames > 0) {
      std::memcpy(&f[36], "Xing", 4);
      f[43] = 1;   // флаги: есть число кадров
      f[44] = u8(xingFrames >> 24);
      f[45] = u8(xingFrames >> 16);
      f[46] = u8(xingFrames >> 8);
      f[47] = u8(xingFrames);
    }
    add(o, f);
  }
  return o;
}

Bytes vorbisComment(std::initializer_list<std::string_view> items) {
  Bytes o;
  le32(o, 6);
  add(o, "Regnum");
  le32(o, u32(items.size()));
  for (std::string_view s : items) {
    le32(o, u32(s.size()));
    add(o, s);
  }
  return o;
}

// Страница Ogg (CRC не проверяется разбором).
Bytes oggPage(u32 serial, u32 seq, u64 granule, u8 type, const Bytes& packet) {
  Bytes o;
  add(o, "OggS");
  o.push_back(0);
  o.push_back(type);
  le64(o, granule);
  le32(o, serial);
  le32(o, seq);
  le32(o, 0);
  std::vector<u8> lacing;
  size_t n = packet.size();
  while (n >= 255) {
    lacing.push_back(255);
    n -= 255;
  }
  lacing.push_back(u8(n));
  o.push_back(u8(lacing.size()));
  add(o, lacing);
  add(o, packet);
  return o;
}

Bytes box(const char* type, const Bytes& body) {
  Bytes o;
  be32(o, u32(8 + body.size()));
  add(o, std::string_view(type, 4));
  add(o, body);
  return o;
}
Bytes ilstItem(const char* type, std::string_view text) {
  Bytes d;
  be32(d, 1);   // UTF-8
  be32(d, 0);
  add(d, text);
  return box(type, box("data", d));
}

AudioInfo probe(const Bytes& b) { return probeAudio(std::span<const u8>(b)); }

}  // namespace

TEST(codec_audioinfo_wav_tags_and_duration) {
  PcmAudio p;
  p.channels = 2;
  p.rate = 32000;
  p.samples.assign(32000 * 2 * 3 / 2, 0.f);
  const AudioInfo a = probe(encodeWav(p, WavSample::Pcm16, "Битва у реки", "Оркестр"));
  CHECK_EQ(a.kind, std::string("wav"));
  CHECK_EQ(a.title, std::string("Битва у реки"));
  CHECK_EQ(a.artist, std::string("Оркестр"));
  CHECK_NEAR(a.duration, 1.5, 1e-6);
  CHECK_EQ(a.rate, 32000);
  CHECK_EQ(a.channels, 2);
  // Тег ID3 внутри WAV (блок «id3 ») — тоже теги.
  Bytes w = encodeWav(p, WavSample::Pcm16);
  Bytes tagBody = id3(3, textFrame(3, "TIT2", 3, Bytes{'Z', 'e', 'n'}));
  Bytes ch;
  add(ch, "id3 ");
  le32(ch, u32(tagBody.size()));
  add(ch, tagBody);
  add(w, ch);
  w[4] = u8(w.size() - 8);
  w[5] = u8((w.size() - 8) >> 8);
  w[6] = u8((w.size() - 8) >> 16);
  CHECK_EQ(probe(w).title, std::string("Zen"));
}

TEST(codec_audioinfo_mp3_id3v23_xing) {
  Bytes frames;
  add(frames, textFrame(3, "TIT2", 1, utf16le("Тема замка", true)));
  add(frames, frame(3, "APIC", Bytes(200000, 0x55)));   // крупная обложка между тегами
  add(frames, textFrame(3, "TPE1", 0, cp1251({0xC0, 0xF0, 0xE8, 0xFF})));   // «Ария» в Windows-1251
  Bytes f = id3(3, frames);
  add(f, mpegFrames(3, 1000));
  const AudioInfo a = probe(f);
  CHECK_EQ(a.kind, std::string("mp3"));
  CHECK_EQ(a.title, std::string("Тема замка"));
  CHECK_EQ(a.artist, std::string("Ария"));
  CHECK_NEAR(a.duration, 1000.0 * 1152 / 44100, 1e-6);
  CHECK_EQ(a.rate, 44100);
  CHECK_EQ(a.channels, 2);
}

TEST(codec_audioinfo_mp3_cbr_and_id3v1) {
  Bytes f = mpegFrames(100, 0);
  Bytes v1(128, 0);
  std::memcpy(v1.data(), "TAG", 3);
  const u8 march[] = {0xCC, 0xE0, 0xF0, 0xF8};   // «Марш»
  std::memcpy(&v1[3], march, 4);
  std::memcpy(&v1[33], "Caf\xE9", 4);             // «Café» в Latin-1
  add(f, v1);
  const AudioInfo a = probe(f);
  CHECK_EQ(a.kind, std::string("mp3"));
  CHECK_EQ(a.title, std::string("Марш"));
  CHECK_EQ(a.artist, std::string("Café"));
  CHECK_NEAR(a.duration, 100.0 * 417 * 8 / 128000, 1e-6);
  // Мусор перед первым кадром (до 64 КБ) пропускается.
  Bytes junk(1000, 0x11);
  add(junk, mpegFrames(10, 0));
  CHECK_EQ(probe(junk).kind, std::string("mp3"));
}

TEST(codec_audioinfo_id3v24_v22_compressed) {
  {  // v2.4: UTF-8, несколько значений через ноль — первое; размер syncsafe; исполнитель альбома как запасной
    Bytes text;
    add(text, "Песнь");
    text.push_back(0);
    add(text, "Вторая");
    Bytes frames = textFrame(4, "TIT2", 3, text);
    add(frames, textFrame(4, "TPE2", 3, Bytes{'B', 'a', 'n', 'd'}));
    Bytes f = id3(4, frames);
    add(f, mpegFrames(3, 0));
    const AudioInfo a = probe(f);
    CHECK_EQ(a.title, std::string("Песнь"));
    CHECK_EQ(a.artist, std::string("Band"));
  }
  {  // v2.4: кадр с рассинхронизацией и указателем длины
    Bytes body;
    syncsafe(body, 5);
    body.push_back(3);
    add(body, "Do\xFF");
    body.push_back(0);   // вставленный байт рассинхронизации
    add(body, "e");
    Bytes f = id3(4, frame(4, "TPE1", body, 0x0003));
    add(f, mpegFrames(3, 0));
    const AudioInfo a = probe(f);
    CHECK(a.artist.size() >= 2 && a.artist.substr(0, 2) == "Do");
  }
  {  // v2.3: сжатый кадр (zlib)
    Bytes plain{3};
    add(plain, "Сжатый");
    const std::vector<u8> z = deflate(std::span<const u8>(plain), 6, ZFormat::Zlib);
    Bytes body;
    be32(body, u32(plain.size()));
    add(body, z);
    Bytes f = id3(3, frame(3, "TIT2", body, 0x0080));
    add(f, mpegFrames(3, 0));
    CHECK_EQ(probe(f).title, std::string("Сжатый"));
  }
  {  // v2.2: трёхбуквенные кадры
    Bytes frames;
    auto f22 = [&](const char* id, std::string_view text) {
      add(frames, std::string_view(id, 3));
      const u32 n = u32(text.size() + 1);
      frames.push_back(u8(n >> 16));
      frames.push_back(u8(n >> 8));
      frames.push_back(u8(n));
      frames.push_back(0);
      add(frames, text);
    };
    f22("TT2", "Old song");
    f22("TP1", "Old band");
    Bytes f = id3(2, frames);
    add(f, mpegFrames(3, 0));
    const AudioInfo a = probe(f);
    CHECK_EQ(a.title, std::string("Old song"));
    CHECK_EQ(a.artist, std::string("Old band"));
  }
}

TEST(codec_audioinfo_flac) {
  Bytes f;
  add(f, "fLaC");
  Bytes si(34, 0);
  // 48000 Гц, 2 канала, 16 бит, 96000 кадров.
  const u32 rate = 48000;
  si[10] = u8(rate >> 12);
  si[11] = u8(rate >> 4);
  si[12] = u8(((rate & 0xF) << 4) | ((2 - 1) << 1) | ((16 - 1) >> 4));
  si[13] = u8(((16 - 1) & 0xF) << 4);
  si[16] = u8(96000 >> 8 & 0xFF);
  si[17] = u8(96000 & 0xFF);
  si[15] = u8(96000 >> 16);
  f.push_back(0x00);
  f.push_back(0);
  f.push_back(0);
  f.push_back(34);
  add(f, si);
  const Bytes vc = vorbisComment({"TITLE=Рассвет", "artist=Хор", "ALBUM=Не то"});
  f.push_back(0x84);   // последний блок, VORBIS_COMMENT
  f.push_back(u8(vc.size() >> 16));
  f.push_back(u8(vc.size() >> 8));
  f.push_back(u8(vc.size()));
  add(f, vc);
  add(f, Bytes(100, 0xAA));
  const AudioInfo a = probe(f);
  CHECK_EQ(a.kind, std::string("flac"));
  CHECK_EQ(a.rate, 48000);
  CHECK_EQ(a.channels, 2);
  CHECK_NEAR(a.duration, 2.0, 1e-9);
  CHECK_EQ(a.title, std::string("Рассвет"));
  CHECK_EQ(a.artist, std::string("Хор"));
}

TEST(codec_audioinfo_ogg_vorbis_and_opus) {
  {
    Bytes id = {0x01};
    add(id, "vorbis");
    le32(id, 0);
    id.push_back(2);
    le32(id, 44100);
    id.resize(30, 0);
    Bytes com = {0x03};
    add(com, "vorbis");
    add(com, vorbisComment({"TITLE=Лесная тропа", "ARTIST=Лютнист"}));
    com.push_back(1);
    Bytes f = oggPage(77, 0, 0, 2, id);
    add(f, oggPage(77, 1, 0, 0, com));
    add(f, oggPage(77, 2, 220500, 0, Bytes(300, 0x11)));
    add(f, oggPage(77, 3, 441000, 4, Bytes(300, 0x22)));
    const AudioInfo a = probe(f);
    CHECK_EQ(a.kind, std::string("ogg"));
    CHECK_EQ(a.title, std::string("Лесная тропа"));
    CHECK_EQ(a.artist, std::string("Лютнист"));
    CHECK_NEAR(a.duration, 10.0, 1e-9);
  }
  {
    Bytes id;
    add(id, "OpusHead");
    id.push_back(1);
    id.push_back(2);
    le16(id, 312);
    le32(id, 44100);
    le16(id, 0);
    id.push_back(0);
    Bytes com;
    add(com, "OpusTags");
    add(com, vorbisComment({"title=Опус"}));
    Bytes f = oggPage(5, 0, 0, 2, id);
    add(f, oggPage(5, 1, 0, 0, com));
    add(f, oggPage(5, 2, 48000 * 3 + 312, 4, Bytes(500, 0x33)));
    const AudioInfo a = probe(f);
    CHECK_EQ(a.kind, std::string("opus"));
    CHECK_EQ(a.title, std::string("Опус"));
    CHECK_NEAR(a.duration, 3.0, 1e-9);
  }
}

TEST(codec_audioinfo_m4a) {
  Bytes ftyp;
  add(ftyp, "M4A ");
  be32(ftyp, 0);
  add(ftyp, "M4A mp42isom");
  Bytes mvhd(100, 0);
  mvhd[15] = 0xE8;   // масштаб 1000
  mvhd[14] = 0x03;
  const u32 dur = 125000;
  mvhd[16] = u8(dur >> 24);
  mvhd[17] = u8(dur >> 16);
  mvhd[18] = u8(dur >> 8);
  mvhd[19] = u8(dur);
  Bytes hdlr(25, 0);
  std::memcpy(&hdlr[8], "mdir", 4);
  Bytes ilst = ilstItem("\xA9nam", "Дорога");
  add(ilst, ilstItem("\xA9" "ART", "Бард"));
  Bytes meta = {0, 0, 0, 0};
  add(meta, box("hdlr", hdlr));
  add(meta, box("ilst", ilst));
  Bytes moov = box("mvhd", mvhd);
  add(moov, box("udta", box("meta", meta)));
  Bytes f = box("ftyp", ftyp);
  add(f, box("mdat", Bytes(2000, 0x77)));   // данные раньше moov — moov в конце файла
  add(f, box("moov", moov));
  const AudioInfo a = probe(f);
  CHECK_EQ(a.kind, std::string("m4a"));
  CHECK_NEAR(a.duration, 125.0, 1e-9);
  CHECK_EQ(a.title, std::string("Дорога"));
  CHECK_EQ(a.artist, std::string("Бард"));
}

TEST(codec_audioinfo_wma) {
  const u8 header[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
  const u8 props[16] = {0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11, 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
  const u8 content[16] = {0x33, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
  Bytes fp(104, 0);
  std::memcpy(fp.data(), props, 16);
  Bytes sz;
  le64(sz, 104);
  std::memcpy(&fp[16], sz.data(), 8);
  Bytes play, pre;
  le64(play, 680000000ull);   // 68 с
  le64(pre, 3000);            // 3 с предзагрузки
  std::memcpy(&fp[64], play.data(), 8);
  std::memcpy(&fp[80], pre.data(), 8);
  const Bytes title = utf16le("Ночь", false), author = utf16le("Лютня", false);
  Bytes cd(content, content + 16);
  le64(cd, 34 + title.size() + 2 + author.size() + 2);
  le16(cd, u32(title.size() + 2));
  le16(cd, u32(author.size() + 2));
  le16(cd, 0);
  le16(cd, 0);
  le16(cd, 0);
  add(cd, title);
  le16(cd, 0);
  add(cd, author);
  le16(cd, 0);
  Bytes f(header, header + 16);
  le64(f, 30 + fp.size() + cd.size());
  le32(f, 2);
  f.push_back(1);
  f.push_back(2);
  add(f, fp);
  add(f, cd);
  add(f, Bytes(64, 0));
  const AudioInfo a = probe(f);
  CHECK_EQ(a.kind, std::string("wma"));
  CHECK_NEAR(a.duration, 65.0, 1e-9);
  CHECK_EQ(a.title, std::string("Ночь"));
  CHECK_EQ(a.artist, std::string("Лютня"));
}

TEST(codec_audioinfo_aiff_and_adts) {
  {
    Bytes comm;
    be16(comm, 2);
    be32(comm, 88200);
    be16(comm, 16);
    const u8 rate[10] = {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0};   // 44100 (80 бит)
    comm.insert(comm.end(), rate, rate + 10);
    Bytes body;
    add(body, "AIFF");
    add(body, "COMM");
    be32(body, u32(comm.size()));
    add(body, comm);
    add(body, "NAME");
    be32(body, 7);
    add(body, "Fanfare");
    body.push_back(0);
    Bytes f;
    add(f, "FORM");
    be32(f, u32(body.size()));
    add(f, body);
    const AudioInfo a = probe(f);
    CHECK_EQ(a.kind, std::string("aiff"));
    CHECK_NEAR(a.duration, 2.0, 1e-9);
    CHECK_EQ(a.rate, 44100);
    CHECK_EQ(a.title, std::string("Fanfare"));
  }
  {  // AAC в ADTS: 50 кадров по 200 байт, 44,1 кГц, стерео
    Bytes f;
    for (int i = 0; i < 50; i++) {
      Bytes fr(200, 0);
      const u32 len = 200;
      fr[0] = 0xFF;
      fr[1] = 0xF1;
      fr[2] = u8((1 << 6) | (4 << 2));   // AAC LC, 44100
      fr[3] = u8((2 << 6) | (len >> 11));
      fr[4] = u8(len >> 3);
      fr[5] = u8((len & 7) << 5) | 0x1F;
      fr[6] = 0xFC;
      add(f, fr);
    }
    const AudioInfo a = probe(f);
    CHECK_EQ(a.kind, std::string("aac"));
    CHECK_EQ(a.channels, 2);
    CHECK_NEAR(a.duration, 50.0 * 1024 / 44100, 1e-9);
  }
}

TEST(codec_audioinfo_tag_text_encodings) {
  CHECK_EQ(tagText8("Привет"), std::string("Привет"));                         // UTF-8 как есть
  CHECK_EQ(tagText8("\xCF\xF0\xE8\xE2\xE5\xF2"), std::string("Привет"));       // Windows-1251
  CHECK_EQ(tagText8("\xA8\xEB\xEA\xE0"), std::string("Ёлка"));
  CHECK_EQ(tagText8("Caf\xE9 del Mar"), std::string("Café del Mar"));         // Latin-1
  CHECK_EQ(tagText8("M\xF6tley Cr\xFC" "e"), std::string("Mötley Crüe"));
  CHECK_EQ(tagText8(std::string_view("  Name \0\0", 9)), std::string("Name"));
  CHECK_EQ(tagText8(""), std::string());
}

TEST(codec_audioinfo_unknown_and_broken) {
  CHECK(probe(Bytes{}).kind.empty());
  CHECK(probe(Bytes(5000, 0x41)).kind.empty());
  Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  png.resize(4000, 0);
  CHECK(probe(png).kind.empty());
  // Тег ID3 с размером больше файла и без кадров звука — не MP3.
  Bytes t = id3(3, textFrame(3, "TIT2", 0, Bytes{'X'}));
  t[9] = 0x7f;
  CHECK(probe(t).kind.empty());
  // Порча настоящих файлов: разбор не падает и не уходит за границы.
  Bytes mp3 = id3(3, textFrame(3, "TIT2", 1, utf16le("Тест", true)));
  add(mp3, mpegFrames(8, 8));
  Bytes ogg = oggPage(1, 0, 0, 2, Bytes{0x01, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, 2, 0x44, 0xAC, 0, 0});
  add(ogg, oggPage(1, 1, 1000, 4, Bytes(50, 1)));
  Rng rng(11);
  for (const Bytes* src : {&mp3, &ogg}) {
    for (int k = 0; k < 300; k++) {
      Bytes b = *src;
      for (int j = 0; j < 6; j++) b[size_t(rng.range(0, int(b.size()) - 1))] = u8(rng.next());
      b.resize(size_t(rng.range(0, int(b.size()))));
      const AudioInfo a = probe(b);
      CHECK(std::isfinite(a.duration) && a.duration >= 0);
      CHECK(utf8::valid(a.title) && utf8::valid(a.artist));
    }
  }
}
