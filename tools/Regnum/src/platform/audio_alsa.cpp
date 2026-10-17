// Regnum — звук Linux: вывод ALSA (libasound.so.2), MP3 — libmpg123.so.0, Ogg Vorbis — libvorbisfile.so.3; все
// через dlopen во время выполнения (без заголовков и библиотек при сборке). Нет libmpg123 или libvorbisfile — такие
// файлы не попадают в список плеера (WAV декодируется своим кодом).
// Проверка синтаксиса на Windows: clang++ -std=c++20 -fsyntax-only -DRG_PLATFORM_SYNTAX_CHECK -I src src/platform/audio_alsa.cpp
#ifndef RG_PLATFORM_SYNTAX_CHECK
#include <dlfcn.h>
#endif

#include <array>

#include "platform/audio_internal.h"

#ifdef RG_PLATFORM_SYNTAX_CHECK
namespace rg::platform::audio::posix {
constexpr int kRtldNow = 2, kRtldLocal = 0;
void* dlopen(const char*, int);
void* dlsym(void*, const char*);
}  // namespace rg::platform::audio::posix
#else
namespace rg::platform::audio::posix {
using ::dlopen;
using ::dlsym;
constexpr int kRtldNow = RTLD_NOW, kRtldLocal = RTLD_LOCAL;
}  // namespace rg::platform::audio::posix
#endif

namespace rg::platform::audio::detail {

namespace {

void* openLib(std::initializer_list<const char*> names) {
  for (const char* n : names)
    if (void* h = posix::dlopen(n, posix::kRtldNow | posix::kRtldLocal)) return h;
  return nullptr;
}

template <class F>
bool sym(void* lib, F& f, std::initializer_list<const char*> names) {
  f = nullptr;
  for (const char* n : names)
    if (lib && !f) f = reinterpret_cast<F>(posix::dlsym(lib, n));
  return f != nullptr;
}

// ---------------------------------------------------------------- ALSA
struct snd_pcm_t;
constexpr int kStreamPlayback = 0, kNonblock = 1, kAccessRwInterleaved = 3, kFormatS16 = 2, kFormatFloat = 14;
constexpr int kStatePrepared = 2;
constexpr int kEAgain = -11;

struct Alsa {
  int (*open)(snd_pcm_t**, const char*, int, int) = nullptr;
  int (*setParams)(snd_pcm_t*, int, int, unsigned, unsigned, int, unsigned) = nullptr;
  long (*writei)(snd_pcm_t*, const void*, unsigned long) = nullptr;
  int (*recover)(snd_pcm_t*, int, int) = nullptr;
  int (*delay)(snd_pcm_t*, long*) = nullptr;
  int (*drop)(snd_pcm_t*) = nullptr;
  int (*prepare)(snd_pcm_t*) = nullptr;
  int (*start)(snd_pcm_t*) = nullptr;
  int (*close)(snd_pcm_t*) = nullptr;
  long (*availUpdate)(snd_pcm_t*) = nullptr;
  int (*wait)(snd_pcm_t*, int) = nullptr;
  int (*state)(snd_pcm_t*) = nullptr;
  bool ok = false;
};

const Alsa& alsa() {
  static const Alsa api = [] {
    Alsa a;
    void* lib = openLib({"libasound.so.2", "libasound.so"});
    a.ok = sym(lib, a.open, {"snd_pcm_open"}) && sym(lib, a.setParams, {"snd_pcm_set_params"}) &&
           sym(lib, a.writei, {"snd_pcm_writei"}) && sym(lib, a.recover, {"snd_pcm_recover"}) &&
           sym(lib, a.delay, {"snd_pcm_delay"}) && sym(lib, a.drop, {"snd_pcm_drop"}) &&
           sym(lib, a.prepare, {"snd_pcm_prepare"}) && sym(lib, a.start, {"snd_pcm_start"}) &&
           sym(lib, a.close, {"snd_pcm_close"}) && sym(lib, a.availUpdate, {"snd_pcm_avail_update"}) &&
           sym(lib, a.wait, {"snd_pcm_wait"}) && sym(lib, a.state, {"snd_pcm_state"});
    return a;
  }();
  return api;
}

class AlsaDevice final : public Device {
 public:
  ~AlsaDevice() override {
    if (pcm_) {
      alsa().drop(pcm_);
      alsa().close(pcm_);
    }
  }

  bool open(std::string* error) {
    const Alsa& a = alsa();
    if (!a.ok) return fail(error, "нет ALSA (libasound.so.2)");
    if (a.open(&pcm_, "default", kStreamPlayback, kNonblock) < 0) {
      pcm_ = nullptr;
      return fail(error, "нет устройства вывода звука");
    }
    // float 32 (ALSA сама приведёт к формату устройства), иначе PCM 16; очередь ≈ 100 мс.
    float_ = a.setParams(pcm_, kFormatFloat, kAccessRwInterleaved, 2, unsigned(rate), 1, 100000) >= 0;
    if (!float_ && a.setParams(pcm_, kFormatS16, kAccessRwInterleaved, 2, unsigned(rate), 1, 100000) < 0)
      return fail(error, "устройство вывода звука не настраивается");
    return true;
  }

  size_t write(const float* f, size_t n, const std::atomic<bool>& stop) override {
    const Alsa& a = alsa();
    if (needPrepare_) {
      a.prepare(pcm_);
      needPrepare_ = false;
    }
    size_t done = 0;
    while (done < n && !failed_) {
      const long avail = a.availUpdate(pcm_);
      if (avail < 0) {
        if (a.recover(pcm_, int(avail), 1) < 0) failed_ = true;
        continue;
      }
      if (avail == 0) {
        if (stop) break;
        a.wait(pcm_, 10);
        continue;
      }
      const size_t k = std::min(n - done, size_t(avail));
      const void* data = f + done * 2;
      if (!float_) {
        s16_.resize(k * 2);
        for (size_t i = 0; i < k * 2; i++) s16_[i] = i16(std::lround(clamp(f[done * 2 + i], -1.f, 1.f) * 32767.f));
        data = s16_.data();
      }
      const long w = a.writei(pcm_, data, k);
      if (w == kEAgain) {
        if (stop) break;
        a.wait(pcm_, 10);
        continue;
      }
      if (w < 0) {
        if (a.recover(pcm_, int(w), 1) < 0) failed_ = true;
        continue;
      }
      done += size_t(w);
    }
    return done;
  }

  i64 queued() override {
    const Alsa& a = alsa();
    long d = 0;
    if (a.delay(pcm_, &d) < 0 || d <= 0) return 0;
    // Поток стартует, когда буфер заполнится; короткий хвост (пауза) запускается сам.
    if (a.state(pcm_) == kStatePrepared) a.start(pcm_);
    return d;
  }

  void idle() override {
    alsa().drop(pcm_);
    needPrepare_ = true;
  }
  bool failed() const override { return failed_; }

 private:
  static bool fail(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
  }
  snd_pcm_t* pcm_ = nullptr;
  bool float_ = true, needPrepare_ = false, failed_ = false;
  std::vector<i16> s16_;
};

// ---------------------------------------------------------------- MP3: libmpg123
struct mpg123_handle;
constexpr int kMpgOk = 0, kMpgDone = -12, kMpgNewFormat = -11, kMpgNeedMore = -10;
constexpr int kMpgAddFlags = 2, kMpgQuiet = 0x20;
constexpr int kEncFloat32 = 0x200, kEncSigned16 = 0xD0;

struct Mpg {
  int (*init)() = nullptr;   // в новых версиях ничего не делает
  mpg123_handle* (*make)(const char*, int*) = nullptr;
  void (*del)(mpg123_handle*) = nullptr;
  int (*param)(mpg123_handle*, int, long, double) = nullptr;
  int (*formatNone)(mpg123_handle*) = nullptr;
  int (*format)(mpg123_handle*, long, int, int) = nullptr;
  int (*open)(mpg123_handle*, const char*) = nullptr;
  int (*close)(mpg123_handle*) = nullptr;
  int (*getformat)(mpg123_handle*, long*, int*, int*) = nullptr;
  int (*read)(mpg123_handle*, void*, size_t, size_t*) = nullptr;
  i64 (*seek)(mpg123_handle*, i64, int) = nullptr;   // off_t на 64-битном Linux — 64 бита
  i64 (*length)(mpg123_handle*) = nullptr;
  bool ok = false;
};

const Mpg& mpg() {
  static const Mpg api = [] {
    Mpg m;
    void* lib = openLib({"libmpg123.so.0", "libmpg123.so"});
    sym(lib, m.init, {"mpg123_init"});
    m.ok = sym(lib, m.make, {"mpg123_new"}) && sym(lib, m.del, {"mpg123_delete"}) && sym(lib, m.param, {"mpg123_param"}) &&
           sym(lib, m.formatNone, {"mpg123_format_none"}) && sym(lib, m.format, {"mpg123_format"}) &&
           sym(lib, m.open, {"mpg123_open", "mpg123_open_64"}) && sym(lib, m.close, {"mpg123_close"}) &&
           sym(lib, m.getformat, {"mpg123_getformat"}) && sym(lib, m.read, {"mpg123_read"}) &&
           sym(lib, m.seek, {"mpg123_seek64", "mpg123_seek", "mpg123_seek_64"}) &&
           sym(lib, m.length, {"mpg123_length64", "mpg123_length", "mpg123_length_64"});
    if (m.ok && m.init) m.init();
    return m;
  }();
  return api;
}

class MpgDecoder final : public Decoder {
 public:
  ~MpgDecoder() override {
    if (h_) {
      mpg().close(h_);
      mpg().del(h_);
    }
  }
  bool open(const std::string& path, std::string* error) {
    const Mpg& m = mpg();
    int err = 0;
    h_ = m.make(nullptr, &err);
    if (!h_) return fail(error, "libmpg123 не запускается");
    m.param(h_, kMpgAddFlags, kMpgQuiet, 0);
    // Вывод — float 32 на всех частотах MPEG; сборка libmpg123 без float — PCM 16.
    static const long kRates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
    for (int enc : {kEncFloat32, kEncSigned16}) {
      m.formatNone(h_);
      bool all = true;
      for (long r : kRates) all = m.format(h_, r, 1 | 2, enc) == kMpgOk && all;
      if (all) break;
    }
    if (m.open(h_, path.c_str()) != kMpgOk) return fail(error, "файл MP3 не открывается");
    long r = 0;
    int ch = 0, enc = 0;
    if (m.getformat(h_, &r, &ch, &enc) != kMpgOk || (enc != kEncFloat32 && enc != kEncSigned16))
      return fail(error, "звук файла не декодируется");
    rate = int(r);
    channels = ch;
    float_ = enc == kEncFloat32;
    const i64 len = m.length(h_);
    if (len > 0) frames = len;
    return true;
  }
  size_t read(float* dst, size_t n) override {
    const Mpg& m = mpg();
    const size_t sampleBytes = float_ ? 4 : 2, frameBytes = sampleBytes * size_t(channels);
    buf_.resize(n * frameBytes);
    size_t total = 0;
    for (int guard = 0; total < buf_.size() && guard < 64; guard++) {
      size_t got = 0;
      const int r = m.read(h_, buf_.data() + total, buf_.size() - total, &got);
      total += got;
      if (r == kMpgDone) break;
      if (r == kMpgNewFormat) {
        long nr = 0;
        int nc = 0, ne = 0;
        m.getformat(h_, &nr, &nc, &ne);
        if (nr != rate || nc != channels) {
          error = "формат звука сменился посреди файла";
          break;
        }
        continue;
      }
      if (r != kMpgOk && r != kMpgNeedMore) {
        error = "ошибка декодирования MP3";
        break;
      }
    }
    const size_t got = total / frameBytes;
    const u8* p = buf_.data();
    for (size_t i = 0; i < got * size_t(channels); i++) {
      if (float_) std::memcpy(&dst[i], p + i * 4, 4);
      else {
        i16 v;
        std::memcpy(&v, p + i * 2, 2);
        dst[i] = float(v) * (1.f / 32768);
      }
    }
    return got;
  }
  bool seek(i64 frame) override { return mpg().seek(h_, std::max<i64>(0, frame), 0) >= 0; }

 private:
  static bool fail(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
  }
  mpg123_handle* h_ = nullptr;
  bool float_ = true;
  std::vector<u8> buf_;
};

// ---------------------------------------------------------------- Ogg Vorbis: libvorbisfile
struct VorbisInfo {   // начало vorbis_info
  int version;
  int channels;
  long rate;
};

struct Vorbis {
  int (*fopen)(const char*, void*) = nullptr;
  int (*clear)(void*) = nullptr;
  VorbisInfo* (*info)(void*, int) = nullptr;
  i64 (*pcmTotal)(void*, int) = nullptr;
  long (*readFloat)(void*, float***, int, int*) = nullptr;
  int (*pcmSeek)(void*, i64) = nullptr;
  bool ok = false;
};

const Vorbis& vorbis() {
  static const Vorbis api = [] {
    Vorbis v;
    void* lib = openLib({"libvorbisfile.so.3", "libvorbisfile.so"});
    v.ok = sym(lib, v.fopen, {"ov_fopen"}) && sym(lib, v.clear, {"ov_clear"}) && sym(lib, v.info, {"ov_info"}) &&
           sym(lib, v.pcmTotal, {"ov_pcm_total"}) && sym(lib, v.readFloat, {"ov_read_float"}) &&
           sym(lib, v.pcmSeek, {"ov_pcm_seek"});
    return v;
  }();
  return api;
}

class VorbisDecoder final : public Decoder {
 public:
  ~VorbisDecoder() override {
    if (open_) vorbis().clear(vf_.data());
  }
  bool open(const std::string& path, std::string* error) {
    const Vorbis& v = vorbis();
    if (v.fopen(path.c_str(), vf_.data()) != 0) {
      if (error) *error = "файл Ogg Vorbis не открывается";
      return false;
    }
    open_ = true;
    const VorbisInfo* i = v.info(vf_.data(), -1);
    if (!i || i->channels < 1) {
      if (error) *error = "звук файла не декодируется";
      return false;
    }
    channels = i->channels;
    rate = int(i->rate);
    const i64 total = v.pcmTotal(vf_.data(), -1);
    if (total > 0) frames = total;
    return true;
  }
  size_t read(float* dst, size_t n) override {
    const Vorbis& v = vorbis();
    size_t done = 0;
    for (int guard = 0; done < n && guard < 64; guard++) {
      float** pcm = nullptr;
      int stream = 0;
      const long got = v.readFloat(vf_.data(), &pcm, int(n - done), &stream);
      if (got == 0) break;
      if (got == -3) continue;   // OV_HOLE: пропуск в данных — читать дальше
      if (got < 0) {
        error = "ошибка декодирования Ogg Vorbis";
        break;
      }
      if (stream != stream_) {
        const VorbisInfo* i = v.info(vf_.data(), stream);
        if (!i || i->channels != channels || int(i->rate) != rate) {
          error = "формат звука сменился посреди файла";
          break;
        }
        stream_ = stream;
      }
      for (long k = 0; k < got; k++)
        for (int c = 0; c < channels; c++) dst[(done + size_t(k)) * size_t(channels) + size_t(c)] = pcm[c][k];
      done += size_t(got);
    }
    return done;
  }
  bool seek(i64 frame) override { return vorbis().pcmSeek(vf_.data(), std::max<i64>(0, frame)) == 0; }

 private:
  // OggVorbis_File — непрозрачная структура (≈ 1 КБ на 64-битных системах): с запасом.
  alignas(16) std::array<unsigned char, 8192> vf_{};
  bool open_ = false;
  int stream_ = 0;
};

std::string extLower(const std::string& path) {
  const size_t dot = path.rfind('.');
  std::string e = dot == std::string::npos ? std::string() : path.substr(dot + 1);
  for (char& c : e)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return e;
}

}  // namespace

std::unique_ptr<Device> openDevice(std::string* error) {
  auto d = std::make_unique<AlsaDevice>();
  if (!d->open(error)) return nullptr;
  return d;
}

std::unique_ptr<Decoder> openSystemDecoder(const std::string& path, std::string* error) {
  const std::string e = extLower(path);
  if (e == "mp3" && mpg().ok) {
    auto d = std::make_unique<MpgDecoder>();
    if (d->open(path, error)) return d;
    return nullptr;
  }
  if (e == "ogg" && vorbis().ok) {
    auto d = std::make_unique<VorbisDecoder>();
    if (d->open(path, error)) return d;
    return nullptr;
  }
  if (error) *error = "формат не поддерживается";
  return nullptr;
}

std::vector<std::string> systemFormats() {
  std::vector<std::string> f;
  if (mpg().ok) f.push_back("mp3");
  if (vorbis().ok) f.push_back("ogg");
  return f;
}

void threadBegin() {}
void threadEnd() {}

}  // namespace rg::platform::audio::detail
