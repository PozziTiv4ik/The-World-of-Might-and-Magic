// Regnum — общие внутренние помощники реализаций платформы (не для кода вне src/platform).
#pragma once
#include <atomic>
#include <deque>
#include <exception>
#include <optional>
#include <string>

#include "platform/platform.h"

namespace rg::platform::detail {

// Очередь событий и вызовы приложения — общее ядро оконных реализаций (win32, x11, cocoa); только главный поток,
// кроме dirty. Состояние реализации наследует Dispatch.
struct Dispatch {
  App* app = nullptr;
  bool ready = false;              // окно создано, события можно доставлять
  std::atomic<bool> dirty{true};   // нужен новый кадр
  int callbackDepth = 0;           // мы внутри обработчика приложения (вложенные циклы диалогов)
  bool quitting = false;
  bool closePending = false;       // закрытие запрошено внутри обработчика — выполнить после выхода из него
  int exitCode = 0;
  std::exception_ptr error;        // первое исключение обработчика (run() пробрасывает его)
  std::deque<Event> queue;

  // В очередь; подряд идущий ввод текста склеивается (IME, вставка суррогатов).
  void push(Event e);
  // Вызов обработчика приложения: исключение запоминается и завершает цикл событий.
  template <class F>
  void guarded(F&& f) {
    callbackDepth++;
    try {
      f();
    } catch (...) {
      if (!error) error = std::current_exception();
      quitting = true;
    }
    callbackDepth--;
  }
  // Доставить очередь приложению (не изнутри обработчика); каждое событие — новый кадр.
  void flushEvents();
  bool callAnimating();
  // Запрос закрытия окна: App::onCloseRequest; изнутри обработчика — отложить (closePending).
  void requestClose();
};

// Монотонные секунды (steady_clock).
double monotonicSeconds();
// Кадр, нарисованный приложением в свой буфер (Frame::source), — в буфер платформы px (если это другой буфер).
void adoptSource(Frame& f);
// Записать показанный кадр для presentStats() (реальные мс; главный поток).
void recordPresent(double frameMs, double presentMs, double waitMs, double periodMs);

// Счётчик щелчков подряд: двойной/тройной щелчок по времени и расстоянию.
class ClickCounter {
 public:
  // t — секунды, interval — предел паузы, slop — предел смещения по каждой оси (в тех же единицах, что x, y).
  int press(int button, float x, float y, double t, double interval, float slop);
  void reset() { count_ = 0; button_ = -1; }

 private:
  int button_ = -1, count_ = 0;
  float x_ = 0, y_ = 0;
  double t_ = 0;
};

// Сборка UTF-8 из единиц UTF-16 (WM_CHAR и т. п.): суррогатные пары, отсев управляющих символов.
class Utf16Input {
 public:
  // Вернуть готовый текст (может быть пустым, если ждём вторую половину пары или символ управляющий).
  std::string push(u16 unit);

 private:
  u16 high_ = 0;
};

bool isTextCodepoint(u32 cp);                       // не управляющий символ (C0, DEL, C1), не суррогат
std::string filterText(std::string_view utf8);      // убрать управляющие символы
std::string toCrlf(std::string_view s);             // \n -> \r\n (буфер обмена Windows)
std::string fromCrlf(std::string_view s);           // \r\n и \r -> \n
bool isSafeUrl(std::string_view url);               // http://, https://, mailto:

// Сохранённое положение окна (физические пиксели экрана).
struct Placement {
  int x = 0, y = 0, w = 0, h = 0;
  bool maximized = false;
  int dpi = 96;
};
std::optional<Placement> loadPlacement(const std::string& path);
bool savePlacement(const std::string& path, const Placement& p);
// Полный путь файла положения: абсолютный placementFile как есть, иначе dataDir/placementFile; пусто — не запоминать.
std::string placementPath(const WindowConfig& cfg, const std::string& dataDir);

}  // namespace rg::platform::detail
