// Regnum — звук: плеер саундтреков редактора (папка с мелодиями, фоновое воспроизведение, плейлист, громкость).
//
// Общий движок — audio.cpp: поток воспроизведения, порядок треков (по списку или перемешанный, повтор), переходы без
// щелчков (короткое затухание и нарастание), треки подряд без паузы, ресемплинг к частоте устройства (windowed sinc),
// приведение каналов к стерео, плавная громкость, свой декодер WAV (codec/wav.h). Вывод и системные декодеры —
// по ОС: audio_win32.cpp (waveOut и Media Foundation), audio_alsa.cpp (ALSA, libmpg123 и libvorbisfile через dlopen),
// audio_mac.cpp (AudioQueue и ExtAudioFile через dlopen), audio_headless.cpp (тихое устройство: время идёт по
// platform::time(), тесты).
//
// Потоки: все методы Player потокобезопасны и не ждут звука; разбор папки и воспроизведение идут в своих потоках.
// Когда меняется то, что показывает интерфейс (трек, состояние, список, ошибка, позиция — см. setTick), плеер
// вызывает platform::invalidate().
#pragma once
#include "base/base.h"

namespace rg::platform::audio {

// Расширения файлов, которые умеет эта сборка (без точки, нижний регистр, по алфавиту): WAV всегда, остальное —
// по системным декодерам (Windows: mp3, m4a, aac, wma, flac; macOS: mp3, m4a, aac, flac, aif, aiff, aifc, caf;
// Linux: mp3 при libmpg123, ogg при libvorbisfile).
const std::vector<std::string>& formats();
bool supported(const std::string& path);           // по расширению

struct Track {
  std::string path;          // UTF-8
  std::string file;          // имя файла
  std::string title;         // из тегов, иначе из имени файла
  std::string artist;        // из тегов (может быть пусто)
  double duration = 0;       // секунды; 0 — неизвестна (уточняется при воспроизведении)
  bool broken = false;       // не открылся — пропускается
};

// «Battle_theme-02.mp3» → «Battle theme-02»: имя без расширения, «_» — пробел, пробелы сжаты.
std::string titleFromFile(const std::string& file);
// Треки папки dir (без вложенных папок и скрытых файлов): поддерживаемые файлы по имени (compareRu), теги и
// длительность по заголовку (codec::probeAudio). Нет папки — пусто.
std::vector<Track> scanFolder(const std::string& dir);

enum class State : u8 { Stopped, Playing, Paused };

struct Status {
  State state = State::Stopped;
  int track = -1;            // звучащий (выбранный) трек — индекс в tracks(); −1 — нет
  double position = 0;       // секунды
  double duration = 0;       // секунды; 0 — неизвестна
  float volume = 0.6f;       // 0…1
  bool muted = false, shuffle = false, repeat = true;
  bool scanning = false;     // разбор папки идёт
  u64 listVersion = 0;       // меняется вместе со списком треков (разбор, сбой трека, уточнённая длительность)
  u64 errorSerial = 0;       // номер последней ошибки (новая ошибка — новый номер)
  std::string error;         // текст последней ошибки по-русски
};

class Player {
 public:
  Player();
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  // Разобрать папку в фоне и заменить список; текущим станет трек с именем файла select (иначе первый). Воспроизведение
  // не начинается.
  void scan(const std::string& dir, const std::string& select = {});
  std::vector<Track> tracks() const;
  Status status() const;

  void play(int track);      // играть трек с начала
  void toggle();             // играть или пауза; остановлен — текущий трек
  void pause();
  void stop();
  void next();               // следующий по порядку (в конце списка — первый)
  void prev();               // дальше 3 с от начала — к началу трека, иначе предыдущий
  void seek(double seconds); // перемотка; остановлен — трек встаёт на паузу в этом месте
  void select(int track);    // сделать текущим без воспроизведения

  void setVolume(float v);   // 0…1 (громкость на слух: множитель v²)
  void setMuted(bool on);
  void setShuffle(bool on);
  void setRepeat(bool on);   // по окончании списка — сначала; иначе остановка
  // Пока звучит трек, позиция, пересёкшая кратное seconds или целую секунду, вызывает platform::invalidate(): окно
  // перерисовывается не чаще, чем меняется полоса хода. Действует 3 с после вызова (интерфейс вызывает каждый кадр:
  // не показанный плеер не будит окно); 0 — только при смене трека и состояния.
  void setTick(double seconds);

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace rg::platform::audio
