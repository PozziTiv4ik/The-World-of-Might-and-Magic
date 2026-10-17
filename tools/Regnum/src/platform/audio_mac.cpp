// Regnum — звук macOS: вывод AudioQueue (float 32 стерео), декодирование ExtAudioFile (MP3, AAC/M4A, AIFF, FLAC,
// CAF); AudioToolbox и CoreFoundation загружаются через dlopen (без заголовков и фреймворков при сборке).
// Проверка синтаксиса на Windows: clang++ -std=c++20 -fsyntax-only -DRG_PLATFORM_SYNTAX_CHECK -I src src/platform/audio_mac.cpp
#ifndef RG_PLATFORM_SYNTAX_CHECK
#include <dlfcn.h>
#endif

#include <array>
#include <condition_variable>
#include <mutex>

#include "platform/audio_internal.h"

#ifdef RG_PLATFORM_SYNTAX_CHECK
namespace rg::platform::audio::posix {
constexpr int kRtldLazy = 1, kRtldLocal = 4;
void* dlopen(const char*, int);
void* dlsym(void*, const char*);
}  // namespace rg::platform::audio::posix
#else
namespace rg::platform::audio::posix {
using ::dlopen;
using ::dlsym;
constexpr int kRtldLazy = RTLD_LAZY, kRtldLocal = RTLD_LOCAL;
}  // namespace rg::platform::audio::posix
#endif

namespace rg::platform::audio::detail {

namespace {

// ---------------------------------------------------------------- типы AudioToolbox (ABI)
using OSStatus = i32;
using Boolean = unsigned char;
struct AudioStreamBasicDescription {
  double mSampleRate;
  u32 mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame, mChannelsPerFrame, mBitsPerChannel, mReserved;
};
static_assert(sizeof(AudioStreamBasicDescription) == 40, "AudioStreamBasicDescription");
struct AudioBuffer {
  u32 mNumberChannels;
  u32 mDataByteSize;
  void* mData;
};
struct AudioBufferList {
  u32 mNumberBuffers;
  AudioBuffer mBuffers[1];
};
struct AudioQueueBuffer {
  u32 mAudioDataBytesCapacity;
  void* mAudioData;
  u32 mAudioDataByteSize;
  void* mUserData;
  u32 mPacketDescriptionCapacity;
  void* mPacketDescriptions;
  u32 mPacketDescriptionCount;
};
struct OpaqueAudioQueue;
struct OpaqueExtAudioFile;
using AudioQueueRef = OpaqueAudioQueue*;
using AudioQueueBufferRef = AudioQueueBuffer*;
using ExtAudioFileRef = OpaqueExtAudioFile*;
using CFTypeRef = const void*;
using OutputCallback = void (*)(void*, AudioQueueRef, AudioQueueBufferRef);

constexpr u32 fourcc(const char (&s)[5]) { return (u32(u8(s[0])) << 24) | (u32(u8(s[1])) << 16) | (u32(u8(s[2])) << 8) | u32(u8(s[3])); }
constexpr u32 kLinearPcm = fourcc("lpcm");
constexpr u32 kFloatPacked = 1 | 8;   // kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked
constexpr u32 kFileDataFormat = fourcc("ffmt"), kClientDataFormat = fourcc("cfmt"), kFileLengthFrames = fourcc("#frm");

struct Toolbox {
  OSStatus (*newOutput)(const AudioStreamBasicDescription*, OutputCallback, void*, void*, CFTypeRef, u32, AudioQueueRef*) = nullptr;
  OSStatus (*allocBuffer)(AudioQueueRef, u32, AudioQueueBufferRef*) = nullptr;
  OSStatus (*enqueue)(AudioQueueRef, AudioQueueBufferRef, u32, const void*) = nullptr;
  OSStatus (*start)(AudioQueueRef, const void*) = nullptr;
  OSStatus (*pause)(AudioQueueRef) = nullptr;
  OSStatus (*dispose)(AudioQueueRef, Boolean) = nullptr;
  OSStatus (*fileOpen)(CFTypeRef, ExtAudioFileRef*) = nullptr;
  OSStatus (*fileDispose)(ExtAudioFileRef) = nullptr;
  OSStatus (*fileRead)(ExtAudioFileRef, u32*, AudioBufferList*) = nullptr;
  OSStatus (*fileSeek)(ExtAudioFileRef, i64) = nullptr;
  OSStatus (*fileGet)(ExtAudioFileRef, u32, u32*, void*) = nullptr;
  OSStatus (*fileSet)(ExtAudioFileRef, u32, u32, const void*) = nullptr;
  CFTypeRef (*urlFromPath)(CFTypeRef, const u8*, long, Boolean) = nullptr;
  void (*release)(CFTypeRef) = nullptr;
  bool ok = false;
};

template <class F>
bool sym(void* lib, F& f, const char* name) {
  f = lib ? reinterpret_cast<F>(posix::dlsym(lib, name)) : nullptr;
  return f != nullptr;
}

const Toolbox& toolbox() {
  static const Toolbox api = [] {
    Toolbox t;
    void* at = posix::dlopen("/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox", posix::kRtldLazy | posix::kRtldLocal);
    void* cf = posix::dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", posix::kRtldLazy | posix::kRtldLocal);
    t.ok = sym(at, t.newOutput, "AudioQueueNewOutput") && sym(at, t.allocBuffer, "AudioQueueAllocateBuffer") &&
           sym(at, t.enqueue, "AudioQueueEnqueueBuffer") && sym(at, t.start, "AudioQueueStart") &&
           sym(at, t.pause, "AudioQueuePause") && sym(at, t.dispose, "AudioQueueDispose") &&
           sym(at, t.fileOpen, "ExtAudioFileOpenURL") && sym(at, t.fileDispose, "ExtAudioFileDispose") &&
           sym(at, t.fileRead, "ExtAudioFileRead") && sym(at, t.fileSeek, "ExtAudioFileSeek") &&
           sym(at, t.fileGet, "ExtAudioFileGetProperty") && sym(at, t.fileSet, "ExtAudioFileSetProperty") &&
           sym(cf, t.urlFromPath, "CFURLCreateFromFileSystemRepresentation") && sym(cf, t.release, "CFRelease");
    return t;
  }();
  return api;
}

// ---------------------------------------------------------------- AudioQueue
class QueueDevice final : public Device {
 public:
  ~QueueDevice() override {
    if (q_) toolbox().dispose(q_, 1);   // сразу: буферы освобождаются вместе с очередью
  }

  bool open(std::string* error) {
    const Toolbox& t = toolbox();
    if (!t.ok) return fail(error, "нет AudioToolbox");
    const AudioStreamBasicDescription f{double(rate), kLinearPcm, kFloatPacked, 8, 1, 8, 2, 32, 0};
    // Обратные вызовы — во внутреннем потоке очереди (без цикла событий).
    if (t.newOutput(&f, &QueueDevice::done, this, nullptr, nullptr, 0, &q_) != 0) {
      q_ = nullptr;
      return fail(error, "нет устройства вывода звука");
    }
    for (int i = 0; i < kBufs; i++) {
      AudioQueueBufferRef b = nullptr;
      if (t.allocBuffer(q_, u32(kFrames * 8), &b) != 0 || !b) return fail(error, "нет устройства вывода звука");
      free_.push_back(b);
    }
    return true;
  }

  size_t write(const float* f, size_t n, const std::atomic<bool>& stop) override {
    const Toolbox& t = toolbox();
    size_t done = 0;
    while (done < n && !failed_) {
      AudioQueueBufferRef b = nullptr;
      {
        std::unique_lock lk(m_);
        if (free_.empty()) {
          if (stop) break;
          cv_.wait_for(lk, std::chrono::milliseconds(10), [&] { return !free_.empty(); });
          continue;
        }
        b = free_.back();
        free_.pop_back();
      }
      const size_t k = std::min(n - done, kFrames);
      float* dst = static_cast<float*>(b->mAudioData);
      for (size_t i = 0; i < k * 2; i++) dst[i] = clamp(f[done * 2 + i], -1.f, 1.f);
      b->mAudioDataByteSize = u32(k * 8);
      {
        std::lock_guard lk(m_);
        queued_ += i64(k);
      }
      if (t.enqueue(q_, b, 0, nullptr) != 0) {
        std::lock_guard lk(m_);
        queued_ -= i64(k);
        free_.push_back(b);
        failed_ = true;
        break;
      }
      if (!running_) {
        running_ = t.start(q_, nullptr) == 0;
        if (!running_) failed_ = true;
      }
      done += k;
    }
    return done;
  }

  i64 queued() override {
    std::lock_guard lk(m_);
    return queued_;
  }
  void idle() override {
    if (running_) toolbox().pause(q_);
    running_ = false;
  }
  bool failed() const override { return failed_; }

 private:
  static constexpr size_t kFrames = 1024;   // ≈ 21 мс при 48 кГц
  static constexpr int kBufs = 6;
  static bool fail(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
  }
  static void done(void* user, AudioQueueRef, AudioQueueBufferRef b) {
    auto* self = static_cast<QueueDevice*>(user);
    {
      std::lock_guard lk(self->m_);
      self->queued_ = std::max<i64>(0, self->queued_ - i64(b->mAudioDataByteSize / 8));
      self->free_.push_back(b);
    }
    self->cv_.notify_one();
  }

  AudioQueueRef q_ = nullptr;
  std::mutex m_;
  std::condition_variable cv_;
  std::vector<AudioQueueBufferRef> free_;
  i64 queued_ = 0;
  bool running_ = false, failed_ = false;
};

// ---------------------------------------------------------------- ExtAudioFile
class FileDecoder final : public Decoder {
 public:
  ~FileDecoder() override {
    if (f_) toolbox().fileDispose(f_);
  }
  bool open(const std::string& path, std::string* error) {
    const Toolbox& t = toolbox();
    if (!t.ok) return fail(error, "нет AudioToolbox");
    CFTypeRef url = t.urlFromPath(nullptr, reinterpret_cast<const u8*>(path.data()), long(path.size()), 0);
    if (!url) return fail(error, "файл не открывается");
    const OSStatus st = t.fileOpen(url, &f_);
    t.release(url);
    if (st != 0 || !f_) {
      f_ = nullptr;
      return fail(error, "формат не поддерживается");
    }
    AudioStreamBasicDescription file{};
    u32 size = sizeof file;
    if (t.fileGet(f_, kFileDataFormat, &size, &file) != 0 || file.mChannelsPerFrame == 0 || file.mSampleRate <= 0)
      return fail(error, "звук файла не декодируется");
    channels = int(file.mChannelsPerFrame);
    rate = int(std::lround(file.mSampleRate));
    // Читать в float 32 с частотой и каналами файла (ресемплинг и сведение — общий движок).
    const u32 bpf = u32(4 * channels);
    const AudioStreamBasicDescription client{file.mSampleRate, kLinearPcm, kFloatPacked, bpf, 1, bpf, u32(channels), 32, 0};
    if (t.fileSet(f_, kClientDataFormat, sizeof client, &client) != 0) return fail(error, "звук файла не декодируется");
    i64 len = 0;
    size = sizeof len;
    if (t.fileGet(f_, kFileLengthFrames, &size, &len) == 0 && len > 0) frames = len;
    return true;
  }
  size_t read(float* dst, size_t n) override {
    AudioBufferList abl{1, {{u32(channels), u32(n * 4 * size_t(channels)), dst}}};
    u32 got = u32(n);
    if (toolbox().fileRead(f_, &got, &abl) != 0) {
      error = "ошибка декодирования";
      return 0;
    }
    return got;
  }
  bool seek(i64 frame) override { return toolbox().fileSeek(f_, std::max<i64>(0, frame)) == 0; }

 private:
  static bool fail(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
  }
  ExtAudioFileRef f_ = nullptr;
};

}  // namespace

std::unique_ptr<Device> openDevice(std::string* error) {
  auto d = std::make_unique<QueueDevice>();
  if (!d->open(error)) return nullptr;
  return d;
}

std::unique_ptr<Decoder> openSystemDecoder(const std::string& path, std::string* error) {
  auto d = std::make_unique<FileDecoder>();
  if (!d->open(path, error)) return nullptr;
  return d;
}

std::vector<std::string> systemFormats() {
  if (!toolbox().ok) return {};
  return {"aac", "aif", "aifc", "aiff", "caf", "flac", "m4a", "mp3"};
}

void threadBegin() {}
void threadEnd() {}

}  // namespace rg::platform::audio::detail
