// Regnum — звук без устройства (тесты): «тихое» устройство, которое проигрывает очередь по времени platform::time()
// (в headless — виртуальное время сценария: позиция трека детерминирована). Системных декодеров нет — только WAV.
#include <thread>

#include "platform/audio_internal.h"
#include "platform/platform.h"

namespace rg::platform::audio::detail {

namespace {

class SilentDevice final : public Device {
 public:
  size_t write(const float*, size_t n, const std::atomic<bool>& stop) override {
    // Места нет — ждём хода времени, но не дольше 20 мс: виртуальное время может стоять (поток плеера не должен
    // зависнуть, пока сценарий ждёт его состояния).
    for (int i = 0; i < 20 && queued() >= kCapacity; i++) {
      if (stop) return 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const i64 room = kCapacity - queued();
    if (room <= 0) return 0;
    if (!running_) {
      running_ = true;
      t0_ = platform::time();
    }
    const size_t k = size_t(std::min<i64>(room, i64(n)));
    written_ += i64(k);
    return k;
  }
  i64 queued() override { return written_ - played(); }
  void idle() override {
    base_ = played();
    running_ = false;
  }

 private:
  static constexpr i64 kCapacity = 4800;   // 0,1 с при 48 кГц
  // Проиграно: с запуска очереди — по времени, но не больше записанного. Время недостачи не теряется: дописанное
  // потом считается проигранным сразу — большой шаг виртуального времени проматывает трек на столько же (тесты).
  // idle() (очередь доиграна после паузы или остановки) этот долг сбрасывает.
  i64 played() const {
    if (!running_) return base_;
    const double dt = std::max(0.0, platform::time() - t0_);
    return std::min(written_, base_ + i64(dt * rate));
  }
  i64 written_ = 0, base_ = 0;
  double t0_ = 0;
  bool running_ = false;
};

}  // namespace

std::unique_ptr<Device> openDevice(std::string*) { return std::make_unique<SilentDevice>(); }

std::unique_ptr<Decoder> openSystemDecoder(const std::string&, std::string* error) {
  if (error) *error = "формат не поддерживается";
  return nullptr;
}

std::vector<std::string> systemFormats() { return {}; }
void threadBegin() {}
void threadEnd() {}

}  // namespace rg::platform::audio::detail
