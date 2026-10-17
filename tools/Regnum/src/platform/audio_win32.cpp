// Regnum — звук Windows: вывод waveOut (winmm, float 32 или PCM 16 с подмешиванием шума), декодирование Media
// Foundation: mfplat.dll и mfreadwrite.dll загружаются при первом обращении (LoadLibrary + GetProcAddress), без них
// (Windows N без Media Feature Pack) остаётся только свой WAV.
#include <windows.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <array>

#include "platform/audio_internal.h"

namespace rg::platform::audio::detail {

namespace {

// ---------------------------------------------------------------- Media Foundation
// GUID — свои копии (библиотека mfuuid не нужна).
constexpr GUID kNull = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
constexpr GUID kMajorType = {0x48eba18e, 0xf8c9, 0x4687, {0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f}};
constexpr GUID kSubtype = {0xf7e34c9a, 0x42e8, 0x4714, {0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5}};
constexpr GUID kAudio = {0x73647561, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kChannels = {0x37e48bf5, 0x645e, 0x4c5b, {0x89, 0xde, 0xad, 0xa9, 0xe2, 0x9b, 0x69, 0x6a}};
constexpr GUID kRate = {0x5faeeae7, 0x0290, 0x4c31, {0x9e, 0x8a, 0xc5, 0x34, 0xf6, 0x8d, 0x9d, 0xba}};
constexpr GUID kBits = {0xf2deb57f, 0x40fa, 0x4764, {0xaa, 0x33, 0xed, 0x4f, 0x2d, 0x1f, 0xf6, 0x69}};
constexpr GUID kDuration = {0x6c990d33, 0xbb8e, 0x477a, {0x85, 0x98, 0x0d, 0x5d, 0x96, 0xfc, 0xd8, 0x8a}};
constexpr DWORD kFirstAudio = DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

template <class F>
F loadProc(HMODULE m, const char* name) {
  FARPROC p = m ? GetProcAddress(m, name) : nullptr;
  return reinterpret_cast<F>(reinterpret_cast<void (*)()>(p));
}

HMODULE systemLibrary(const wchar_t* name) {
  HMODULE h = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  return h ? h : LoadLibraryW(name);   // системы без LOAD_LIBRARY_SEARCH_SYSTEM32
}

struct MfApi {
  HRESULT(WINAPI* startup)(ULONG, DWORD) = nullptr;
  HRESULT(WINAPI* shutdown)() = nullptr;
  HRESULT(WINAPI* createMediaType)(IMFMediaType**) = nullptr;
  HRESULT(WINAPI* createReader)(LPCWSTR, IMFAttributes*, IMFSourceReader**) = nullptr;
  bool ok = false;
};

const MfApi& mf() {
  static const MfApi api = [] {
    MfApi a;
    HMODULE plat = systemLibrary(L"mfplat.dll"), rw = systemLibrary(L"mfreadwrite.dll");
    a.startup = loadProc<decltype(a.startup)>(plat, "MFStartup");
    a.shutdown = loadProc<decltype(a.shutdown)>(plat, "MFShutdown");
    a.createMediaType = loadProc<decltype(a.createMediaType)>(plat, "MFCreateMediaType");
    a.createReader = loadProc<decltype(a.createReader)>(rw, "MFCreateSourceReaderFromURL");
    a.ok = a.startup && a.shutdown && a.createMediaType && a.createReader;
    return a;
  }();
  return api;
}

thread_local bool tCom = false, tMf = false;   // поток воспроизведения: COM и Media Foundation запущены

template <class T>
void release(T*& p) {
  if (p) p->Release();
  p = nullptr;
}

class MfDecoder final : public Decoder {
 public:
  ~MfDecoder() override { release(reader_); }

  bool open(const std::string& path, std::string* error) {
    const MfApi& a = mf();
    if (!a.ok || !tMf) return fail(error, "Media Foundation недоступна");
    const std::wstring w = utf8::toWide(path);
    if (FAILED(a.createReader(w.c_str(), nullptr, &reader_))) return fail(error, "формат не поддерживается");
    reader_->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader_->SetStreamSelection(kFirstAudio, TRUE))) return fail(error, "в файле нет звука");
    IMFMediaType* t = nullptr;
    if (FAILED(a.createMediaType(&t))) return fail(error, "Media Foundation недоступна");
    t->SetGUID(kMajorType, kAudio);
    t->SetGUID(kSubtype, kFloat);
    const HRESULT hr = reader_->SetCurrentMediaType(kFirstAudio, nullptr, t);
    release(t);
    if (FAILED(hr) || !readFormat()) return fail(error, "звук файла не декодируется");
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(reader_->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE), kDuration, &v)) && v.vt == VT_UI8)
      frames = i64(double(v.uhVal.QuadPart) / 1e7 * rate);
    PropVariantClear(&v);
    return true;
  }

  size_t read(float* dst, size_t n) override {
    size_t done = 0;
    while (done < n) {
      if (fifoPos_ < fifo_.size()) {
        const size_t k = std::min(n - done, (fifo_.size() - fifoPos_) / size_t(channels));
        std::memcpy(dst + done * size_t(channels), fifo_.data() + fifoPos_, k * size_t(channels) * sizeof(float));
        fifoPos_ += k * size_t(channels);
        done += k;
        continue;
      }
      if (eof_ || !fill()) break;
    }
    return done;
  }

  bool seek(i64 frame) override {
    PROPVARIANT v;
    PropVariantInit(&v);
    v.vt = VT_I8;
    v.hVal.QuadPart = LONGLONG(double(std::max<i64>(0, frame)) * 1e7 / rate);
    const HRESULT hr = reader_->SetCurrentPosition(kNull, v);
    PropVariantClear(&v);
    if (FAILED(hr)) return false;
    fifo_.clear();
    fifoPos_ = 0;
    eof_ = false;
    // Первая порция после перемотки начинается не позже цели: лишнее отбрасывается по её отметке времени.
    seekTarget_ = frame;
    align_ = true;
    skip_ = 0;
    return true;
  }

 private:
  static bool fail(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
  }

  bool readFormat() {
    IMFMediaType* t = nullptr;
    if (FAILED(reader_->GetCurrentMediaType(kFirstAudio, &t))) return false;
    UINT32 ch = 0, sr = 0, bits = 0;
    GUID sub = kNull;
    t->GetUINT32(kChannels, &ch);
    t->GetUINT32(kRate, &sr);
    t->GetUINT32(kBits, &bits);
    t->GetGUID(kSubtype, &sub);
    release(t);
    if (sub != kFloat || bits != 32 || ch == 0 || sr == 0) return false;
    channels = int(ch);
    rate = int(sr);
    return true;
  }

  // Следующая порция декодера в fifo_ (false — конец или ошибка).
  bool fill() {
    fifo_.clear();
    fifoPos_ = 0;
    for (int guard = 0; guard < 64 && fifo_.empty() && !eof_; guard++) {
      DWORD stream = 0, flags = 0;
      LONGLONG ts = 0;
      IMFSample* s = nullptr;
      const HRESULT hr = reader_->ReadSample(kFirstAudio, 0, &stream, &flags, &ts, &s);
      if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
        error = "ошибка декодирования";
        eof_ = true;
        release(s);
        return false;
      }
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM) eof_ = true;
      if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
        const int r = rate, c = channels;
        if (!readFormat() || r != rate || c != channels) {
          error = "формат звука сменился посреди файла";
          rate = r;
          channels = c;
          eof_ = true;
          release(s);
          return false;
        }
      }
      if (!s) continue;
      if (align_) {
        skip_ = std::max<i64>(0, seekTarget_ - i64(double(ts) * rate / 1e7));
        align_ = false;
      }
      IMFMediaBuffer* b = nullptr;
      if (SUCCEEDED(s->ConvertToContiguousBuffer(&b))) {
        BYTE* data = nullptr;
        DWORD len = 0;
        if (SUCCEEDED(b->Lock(&data, nullptr, &len))) {
          size_t count = len / sizeof(float);
          count -= count % size_t(channels);
          const size_t skipFrames = size_t(std::min<i64>(skip_, i64(count / size_t(channels))));
          skip_ -= i64(skipFrames);
          const float* f = reinterpret_cast<const float*>(data);
          fifo_.assign(f + skipFrames * size_t(channels), f + count);
          b->Unlock();
        }
        release(b);
      }
      release(s);
    }
    return !fifo_.empty();
  }

  IMFSourceReader* reader_ = nullptr;
  std::vector<float> fifo_;
  size_t fifoPos_ = 0;
  bool eof_ = false, align_ = false;
  i64 seekTarget_ = 0, skip_ = 0;
};

// ---------------------------------------------------------------- waveOut
class WaveOutDevice final : public Device {
 public:
  ~WaveOutDevice() override {
    if (wo_) {
      waveOutReset(wo_);
      for (Buf& b : bufs_)
        if (b.h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wo_, &b.h, sizeof(WAVEHDR));
      waveOutClose(wo_);
    }
    if (ev_) CloseHandle(ev_);
  }

  bool open(std::string* error) {
    ev_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!ev_) return fail(error);
    // Сначала float 32 (смеситель Windows работает в float), иначе PCM 16.
    for (int pass = 0; pass < 2 && !wo_; pass++) {
      float_ = pass == 0;
      WAVEFORMATEX wf{};
      wf.wFormatTag = float_ ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
      wf.nChannels = 2;
      wf.nSamplesPerSec = DWORD(rate);
      wf.wBitsPerSample = float_ ? 32 : 16;
      wf.nBlockAlign = WORD(wf.nChannels * wf.wBitsPerSample / 8);
      wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
      if (waveOutOpen(&wo_, WAVE_MAPPER, &wf, DWORD_PTR(ev_), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) wo_ = nullptr;
    }
    if (!wo_) return fail(error);
    frameBytes_ = float_ ? 8 : 4;
    for (Buf& b : bufs_) {
      b.data.resize(kFrames * frameBytes_);
      b.h.lpData = reinterpret_cast<LPSTR>(b.data.data());
      b.h.dwBufferLength = DWORD(b.data.size());
      if (waveOutPrepareHeader(wo_, &b.h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return fail(error);
    }
    // Поток воспроизведения — чуть выше обычного: интерфейс и фоновые задачи не должны прерывать звук.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    return true;
  }

  size_t write(const float* f, size_t n, const std::atomic<bool>& stop) override {
    size_t done = 0;
    while (done < n && !failed_) {
      Buf* b = freeBuf();
      if (!b) {
        if (stop) break;
        WaitForSingleObject(ev_, 10);
        continue;
      }
      const size_t k = std::min(n - done, kFrames);
      convert(f + done * 2, k, b->data.data());
      b->h.dwBufferLength = DWORD(k * frameBytes_);
      b->h.dwFlags &= ~DWORD(WHDR_DONE);
      if (waveOutWrite(wo_, &b->h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        failed_ = true;
        break;
      }
      b->frames = k;
      b->queued = true;
      done += k;
    }
    return done;
  }

  i64 queued() override {
    i64 q = 0;
    for (Buf& b : bufs_) {
      if (b.queued && (b.h.dwFlags & WHDR_DONE)) b.queued = false;
      if (b.queued) q += i64(b.frames);
    }
    return q;
  }
  void idle() override {}
  bool failed() const override { return failed_; }

 private:
  static constexpr size_t kFrames = 1024;   // ≈ 21 мс при 48 кГц
  static constexpr int kBufs = 6;
  struct Buf {
    WAVEHDR h{};
    std::vector<u8> data;
    size_t frames = 0;
    bool queued = false;
  };
  static bool fail(std::string* error) {
    if (error) *error = "нет устройства вывода звука";
    return false;
  }
  Buf* freeBuf() {
    for (Buf& b : bufs_)
      if (!b.queued || (b.h.dwFlags & WHDR_DONE)) {
        b.queued = false;
        return &b;
      }
    return nullptr;
  }
  void convert(const float* src, size_t frames, u8* dst) {
    const size_t n = frames * 2;
    if (float_) {
      for (size_t i = 0; i < n; i++) {
        const float v = clamp(src[i], -1.f, 1.f);
        std::memcpy(dst + i * 4, &v, 4);
      }
      return;
    }
    for (size_t i = 0; i < n; i++) {   // треугольный шум ±1 младший разряд — без искажений тихих мест
      const float d = float(rng_.uniform() - rng_.uniform());
      const i32 s = i32(std::lround(clamp(src[i], -1.f, 1.f) * 32766.f + d));
      const i16 v = i16(clamp(s, -32768, 32767));
      std::memcpy(dst + i * 2, &v, 2);
    }
  }

  HWAVEOUT wo_ = nullptr;
  HANDLE ev_ = nullptr;
  std::array<Buf, kBufs> bufs_;
  bool float_ = true, failed_ = false;
  size_t frameBytes_ = 8;
  Rng rng_{0x9e3779b97f4a7c15ull};
};

}  // namespace

std::unique_ptr<Device> openDevice(std::string* error) {
  auto d = std::make_unique<WaveOutDevice>();
  if (!d->open(error)) return nullptr;
  return d;
}

std::unique_ptr<Decoder> openSystemDecoder(const std::string& path, std::string* error) {
  auto d = std::make_unique<MfDecoder>();
  if (!d->open(path, error)) return nullptr;
  return d;
}

std::vector<std::string> systemFormats() {
  if (!mf().ok) return {};
  return {"aac", "flac", "m4a", "mp3", "wma"};
}

void threadBegin() {
  tCom = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  tMf = mf().ok && SUCCEEDED(mf().startup(MF_VERSION, MFSTARTUP_LITE));
}

void threadEnd() {
  if (tMf) mf().shutdown();
  if (tCom) CoUninitialize();
  tMf = tCom = false;
}

}  // namespace rg::platform::audio::detail
