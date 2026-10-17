// Тесты WAV: запись и чтение PCM 16/24 и float, 8/32 бита и float 64, WAVE_FORMAT_EXTENSIBLE, порядок блоков,
// потоковая запись без длины, RF64, отказ на чужих и испорченных файлах, ограничение при записи.
#include "codec/wav.h"
#include "tests/test.h"

using namespace rg;
using namespace rg::codec;

namespace {

PcmAudio tone(int channels, int rate, double seconds) {
  PcmAudio p;
  p.channels = channels;
  p.rate = rate;
  const size_t n = size_t(seconds * rate);
  for (size_t i = 0; i < n; i++)
    for (int c = 0; c < channels; c++) p.samples.push_back(float(0.8 * std::sin(2 * kPi * (440.0 + 110 * c) * double(i) / rate)));
  return p;
}

void put16(std::vector<u8>& o, u32 v) {
  o.push_back(u8(v));
  o.push_back(u8(v >> 8));
}
void put32(std::vector<u8>& o, u32 v) {
  put16(o, v & 0xffff);
  put16(o, v >> 16);
}
void tag(std::vector<u8>& o, const char* t) { o.insert(o.end(), t, t + 4); }
void chunk(std::vector<u8>& o, const char* id, const std::vector<u8>& body) {
  tag(o, id);
  put32(o, u32(body.size()));
  o.insert(o.end(), body.begin(), body.end());
  if (body.size() & 1) o.push_back(0);
}
std::vector<u8> fmtBody(u16 format, int ch, int rate, int bits) {
  std::vector<u8> b;
  put16(b, format);
  put16(b, u32(ch));
  put32(b, u32(rate));
  put32(b, u32(rate * ch * bits / 8));
  put16(b, u32(ch * bits / 8));
  put16(b, u32(bits));
  return b;
}
std::vector<u8> riff(const std::vector<u8>& chunks, const char* kind = "RIFF") {
  std::vector<u8> o;
  tag(o, kind);
  put32(o, u32(4 + chunks.size()));
  tag(o, "WAVE");
  o.insert(o.end(), chunks.begin(), chunks.end());
  return o;
}

}  // namespace

TEST(codec_wav_roundtrip_pcm16) {
  const PcmAudio src = tone(2, 44100, 0.25);
  const std::vector<u8> file = encodeWav(src, WavSample::Pcm16);
  std::string err;
  auto fmt = parseWav(file, &err);
  CHECK_MSG(fmt.has_value(), err);
  CHECK_EQ(fmt->channels, 2);
  CHECK_EQ(fmt->rate, 44100);
  CHECK_EQ(fmt->bits, 16);
  CHECK(!fmt->isFloat);
  CHECK_EQ(fmt->blockAlign, 4);
  CHECK_EQ(fmt->frames(), u64(src.frames()));
  CHECK_NEAR(fmt->seconds(), 0.25, 1e-4);
  auto pcm = decodeWav(file);
  CHECK(pcm.has_value());
  CHECK_EQ(pcm->samples.size(), src.samples.size());
  float worst = 0;
  for (size_t i = 0; i < src.samples.size(); i++) worst = std::max(worst, std::fabs(pcm->samples[i] - src.samples[i]));
  CHECK(worst <= 2.0f / 32768);   // запись ×32767, чтение ÷32768 и округление
}

TEST(codec_wav_roundtrip_24_and_float) {
  const PcmAudio src = tone(1, 48000, 0.1);
  auto p24 = decodeWav(encodeWav(src, WavSample::Pcm24));
  CHECK(p24.has_value() && p24->samples.size() == src.samples.size());
  float worst = 0;
  for (size_t i = 0; i < src.samples.size(); i++) worst = std::max(worst, std::fabs(p24->samples[i] - src.samples[i]));
  CHECK(worst <= 2.0f / 8388608);
  const std::vector<u8> ff = encodeWav(src, WavSample::Float32, "Название", "Исполнитель");
  auto fmt = parseWav(ff);
  CHECK(fmt && fmt->isFloat && fmt->bits == 32);
  auto pf = decodeWav(ff);
  CHECK(pf.has_value() && pf->samples == src.samples);
}

TEST(codec_wav_formats_8_32_64_extensible) {
  {  // 8 бит без знака, моно
    std::vector<u8> c;
    chunk(c, "fmt ", fmtBody(1, 1, 8000, 8));
    chunk(c, "data", {0, 128, 255});
    auto p = decodeWav(riff(c));
    CHECK(p.has_value() && p->samples.size() == 3);
    CHECK_NEAR(p->samples[0], -1.0, 1e-6);
    CHECK_NEAR(p->samples[1], 0.0, 1e-6);
    CHECK_NEAR(p->samples[2], 127.0 / 128, 1e-6);
  }
  {  // 32 бита целые
    std::vector<u8> c, d;
    chunk(c, "fmt ", fmtBody(1, 1, 8000, 32));
    put32(d, 0x40000000u);
    put32(d, 0x80000000u);
    chunk(c, "data", d);
    auto p = decodeWav(riff(c));
    CHECK(p.has_value() && p->samples.size() == 2);
    CHECK_NEAR(p->samples[0], 0.5, 1e-6);
    CHECK_NEAR(p->samples[1], -1.0, 1e-6);
  }
  {  // float 64
    std::vector<u8> c, d;
    chunk(c, "fmt ", fmtBody(3, 1, 8000, 64));
    const double v = -0.25;
    u64 u;
    std::memcpy(&u, &v, 8);
    put32(d, u32(u));
    put32(d, u32(u >> 32));
    chunk(c, "data", d);
    auto p = decodeWav(riff(c));
    CHECK(p.has_value() && p->samples.size() == 1);
    CHECK_NEAR(p->samples[0], -0.25, 1e-7);
  }
  {  // WAVE_FORMAT_EXTENSIBLE: 24 значащих бита в контейнере 32, подформат PCM
    std::vector<u8> f = fmtBody(0xFFFE, 2, 96000, 32), c, d;
    put16(f, 22);
    put16(f, 24);
    put32(f, 3);   // маска каналов: L R
    const u8 guid[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    f.insert(f.end(), guid, guid + 16);
    chunk(c, "fmt ", f);
    put32(d, 0x20000000u);
    put32(d, 0xE0000000u);
    chunk(c, "data", d);
    auto fmt = parseWav(riff(c));
    CHECK(fmt && fmt->channels == 2 && fmt->rate == 96000 && fmt->bits == 32 && !fmt->isFloat);
    auto p = decodeWav(riff(c));
    CHECK(p.has_value() && p->samples.size() == 2);
    CHECK_NEAR(p->samples[0], 0.25, 1e-6);
    CHECK_NEAR(p->samples[1], -0.25, 1e-6);
  }
}

TEST(codec_wav_chunk_order_streaming_rf64) {
  std::vector<u8> d;
  for (int i = 0; i < 6; i++) put16(d, u32(i * 1000));
  {  // data раньше fmt, между ними — LIST нечётной длины
    std::vector<u8> c;
    chunk(c, "data", d);
    chunk(c, "LIST", {'I', 'N', 'F', 'O', 'x'});
    chunk(c, "fmt ", fmtBody(1, 2, 22050, 16));
    auto fmt = parseWav(riff(c));
    CHECK(fmt && fmt->frames() == 3 && fmt->dataOffset == 20);
  }
  {  // длина data не записана (0) — данные до конца файла; последний неполный кадр отбрасывается
    std::vector<u8> c;
    chunk(c, "fmt ", fmtBody(1, 2, 22050, 16));
    tag(c, "data");
    put32(c, 0);
    c.insert(c.end(), d.begin(), d.end());
    c.push_back(7);
    auto fmt = parseWav(riff(c));
    CHECK(fmt && fmt->frames() == 3 && fmt->dataBytes == 12);
  }
  {  // RF64: размеры в ds64
    std::vector<u8> c, ds;
    put32(ds, 0xFFFFFFFFu);
    put32(ds, 0);
    put32(ds, 12);   // размер data
    put32(ds, 0);
    put32(ds, 3);    // кадров
    put32(ds, 0);
    put32(ds, 0);    // таблица
    chunk(c, "ds64", ds);
    chunk(c, "fmt ", fmtBody(1, 2, 22050, 16));
    tag(c, "data");
    put32(c, 0xFFFFFFFFu);
    c.insert(c.end(), d.begin(), d.end());
    auto fmt = parseWav(riff(c, "RF64"));
    CHECK(fmt && fmt->frames() == 3);
    auto p = decodeWav(riff(c, "RF64"));
    CHECK(p.has_value() && p->samples.size() == 6);
    CHECK_NEAR(p->samples[5], 5000.0 / 32768, 1e-6);
  }
}

TEST(codec_wav_rejects_foreign_and_broken) {
  std::string err;
  CHECK(!parseWav(std::vector<u8>{}, &err));
  CHECK(!err.empty());
  const std::string png = "\x89PNG\r\n\x1a\n0000000000";
  CHECK(!parseWav(std::span<const u8>(reinterpret_cast<const u8*>(png.data()), png.size())));
  std::vector<u8> d = {1, 2, 3, 4};
  {  // без fmt
    std::vector<u8> c;
    chunk(c, "data", d);
    err.clear();
    CHECK(!parseWav(riff(c), &err));
    CHECK(err.find("fmt") != std::string::npos);
  }
  {  // сжатый (IMA ADPCM)
    std::vector<u8> c;
    chunk(c, "fmt ", fmtBody(0x11, 1, 8000, 4));
    chunk(c, "data", d);
    CHECK(!parseWav(riff(c)));
  }
  {  // ноль каналов и неверная разрядность
    std::vector<u8> c, c2;
    chunk(c, "fmt ", fmtBody(1, 0, 8000, 16));
    chunk(c, "data", d);
    CHECK(!parseWav(riff(c)));
    chunk(c2, "fmt ", fmtBody(1, 1, 8000, 12));
    chunk(c2, "data", d);
    CHECK(!parseWav(riff(c2)));
  }
  {  // обрезанный заголовок fmt
    std::vector<u8> o = riff({});
    tag(o, "fmt ");
    put32(o, 16);
    put16(o, 1);
    CHECK(!parseWav(o));
  }
  // Случайная порча настоящего файла не роняет разбор.
  std::vector<u8> good = encodeWav(tone(2, 8000, 0.01), WavSample::Pcm16, "Т", "И");
  Rng rng(7);
  for (int k = 0; k < 400; k++) {
    std::vector<u8> b = good;
    for (int j = 0; j < 4; j++) b[size_t(rng.range(0, int(b.size()) - 1))] = u8(rng.next());
    b.resize(size_t(rng.range(0, int(b.size()))));
    auto p = decodeWav(b);
    if (p) CHECK(p->samples.size() == p->frames() * size_t(p->channels));
  }
}

TEST(codec_wav_encode_clamps_and_tags) {
  PcmAudio p;
  p.channels = 1;
  p.rate = 8000;
  p.samples = {2.f, -3.f, std::nanf(""), 0.5f};
  auto back = decodeWav(encodeWav(p, WavSample::Pcm16));
  CHECK(back.has_value() && back->samples.size() == 4);
  CHECK_NEAR(back->samples[0], 32767.0 / 32768, 1e-6);
  CHECK_NEAR(back->samples[1], -32767.0 / 32768, 1e-6);
  CHECK_NEAR(back->samples[2], 0.0, 1e-6);
  // LIST/INFO с названием и исполнителем — перед data, чётные длины.
  const std::vector<u8> f = encodeWav(p, WavSample::Pcm16, "Марш", "Хор");
  const std::string s(f.begin(), f.end());
  CHECK(s.find("LIST") != std::string::npos && s.find("INAM") != std::string::npos && s.find("Марш") != std::string::npos);
  CHECK(s.find("IART") != std::string::npos && s.find("Хор") != std::string::npos);
  CHECK(s.find("LIST") < s.find("data"));
  CHECK_EQ(f.size() % 2, size_t(0));
  CHECK(parseWav(f).has_value());
}
