// Regnum — внутреннее устройство звука (только audio*.cpp): декодер и устройство вывода, которые реализует каждая ОС.
#pragma once
#include <atomic>

#include "platform/audio.h"

namespace rg::platform::audio::detail {

// Декодер файла: отсчёты float, каналы подряд; частота и число каналов постоянны.
class Decoder {
 public:
  virtual ~Decoder() = default;
  int rate = 0, channels = 0;
  i64 frames = -1;                 // длина в кадрах; −1 — неизвестна
  std::string error;               // ошибка чтения посреди файла (read вернул 0 раньше конца)
  // До n кадров в dst (n × channels); 0 — конец файла или ошибка.
  virtual size_t read(float* dst, size_t n) = 0;
  virtual bool seek(i64 frame) = 0;
};

// Устройство вывода: стерео float, частота rate. Вызывается только из потока воспроизведения.
class Device {
 public:
  virtual ~Device() = default;
  int rate = 48000;
  // Отдать до n кадров (каналы подряд): ждёт места в очереди устройства, пока не поднят stop. Вернуть принятые кадры
  // (0 — прервано или сбой: failed()).
  virtual size_t write(const float* frames, size_t n, const std::atomic<bool>& stop) = 0;
  virtual i64 queued() = 0;        // кадров в очереди, ещё не прозвучавших
  virtual void idle() = 0;         // очередь доиграна, звука какое-то время не будет; следующий write продолжит
  virtual bool failed() const { return false; }   // устройство пропало (отключили наушники): открыть заново
};

// Системная часть (audio_<ОС>.cpp).
std::unique_ptr<Device> openDevice(std::string* error);
std::unique_ptr<Decoder> openSystemDecoder(const std::string& path, std::string* error);
std::vector<std::string> systemFormats();   // расширения системных декодеров (без «wav»)
void threadBegin();                         // начало и конец потока воспроизведения (COM и Media Foundation)
void threadEnd();

}  // namespace rg::platform::audio::detail
