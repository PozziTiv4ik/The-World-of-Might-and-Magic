// Regnum — звук: общий движок плеера саундтреков (не зависит от ОС). Поток воспроизведения читает декодер, приводит
// звук к стерео и частоте устройства, применяет громкость и затухания и отдаёт устройству; позиция считается по тому,
// что устройство уже проиграло. Системный вывод и декодеры — audio_<ОС>.cpp (audio_internal.h).
#include "platform/audio.h"

#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

#include "base/fs.h"
#include "codec/audioinfo.h"
#include "codec/wav.h"
#include "platform/audio_internal.h"
#include "platform/platform.h"

namespace rg::platform::audio {

namespace {

using namespace std::chrono_literals;

std::string extOf(const std::string& path) {
  std::string e = fs::ext(path);
  if (!e.empty() && e[0] == '.') e.erase(0, 1);
  for (char& c : e)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return e;
}

// ---------------------------------------------------------------- свой декодер WAV
class WavDecoder final : public detail::Decoder {
 public:
  bool open(const std::string& path, std::string* error) {
    f_.open(fs::path(path), std::ios::binary);
    if (!f_) {
      if (error) *error = "файл не открывается";
      return false;
    }
    const u64 size = fs::fileSize(path).value_or(0);
    auto fmt = codec::parseWav([this](u64 off, u8* dst, size_t n) { return readAt(off, dst, n); }, size, error);
    if (!fmt) return false;
    fmt_ = *fmt;
    rate = fmt_.rate;
    channels = fmt_.channels;
    frames = i64(fmt_.frames());
    return true;
  }
  size_t read(float* dst, size_t n) override {
    n = size_t(std::min<u64>(n, fmt_.frames() - pos_));
    if (n == 0) return 0;
    buf_.resize(n * size_t(fmt_.blockAlign));
    const size_t got = readAt(fmt_.dataOffset + pos_ * u64(fmt_.blockAlign), buf_.data(), buf_.size()) / size_t(fmt_.blockAlign);
    if (got < n) error = "ошибка чтения файла";
    codec::wavToFloat(fmt_, buf_.data(), got, dst);
    pos_ += got;
    return got;
  }
  bool seek(i64 frame) override {
    pos_ = u64(clamp<i64>(frame, 0, frames));
    return true;
  }

 private:
  size_t readAt(u64 off, u8* dst, size_t n) {
    f_.clear();
    f_.seekg(std::streamoff(off));
    f_.read(reinterpret_cast<char*>(dst), std::streamsize(n));
    return size_t(f_.gcount());
  }
  std::ifstream f_;
  codec::WavFormat fmt_;
  u64 pos_ = 0;
  std::vector<u8> buf_;
};

std::unique_ptr<detail::Decoder> openDecoder(const std::string& path, std::string* error) {
  std::unique_ptr<detail::Decoder> d;
  std::string err;
  if (extOf(path) == "wav") {
    auto w = std::make_unique<WavDecoder>();
    if (w->open(path, &err)) d = std::move(w);
    else d = detail::openSystemDecoder(path, nullptr);   // сжатый WAV (ADPCM и др.) — системному декодеру
  } else {
    d = detail::openSystemDecoder(path, &err);
  }
  if (d && (d->rate < 1000 || d->rate > 768000 || d->channels < 1 || d->channels > 32)) {
    d.reset();
    err = "неподдерживаемый формат звука";
  }
  if (!d && error) *error = err.empty() ? std::string("формат не поддерживается") : err;
  return d;
}

// ---------------------------------------------------------------- каналы и частота
// Каналы источника → стерео (порядок WAVE: L R C LFE BL BR SL SR); многоканальный звук сводится с −3 дБ для центра и
// тыла, LFE отбрасывается, сумма нормируется (без перегрузки).
void toStereo(const float* in, size_t frames, int ch, std::vector<float>& out) {
  out.resize(frames * 2);
  if (ch == 2) {
    std::memcpy(out.data(), in, frames * 2 * sizeof(float));
    return;
  }
  if (ch == 1) {
    for (size_t i = 0; i < frames; i++) out[2 * i] = out[2 * i + 1] = in[i];
    return;
  }
  constexpr float h = 0.70710678f;
  // Доли каналов в левом и правом выходе: 1 — свой край, h — центр и тыл, 0 — LFE.
  float wl[32] = {}, wr[32] = {};
  auto side = [&](int c, float w) {   // левый канал пары — чётный
    if (c % 2 == 0) wl[c] = w;
    else wr[c] = w;
  };
  const int n = std::min(ch, 32);
  side(0, 1);
  side(1, 1);
  if (n == 4) {             // L R BL BR
    side(2, h);
    side(3, h);
  } else {                  // центр — в оба
    wl[2] = wr[2] = h;
  }
  if (n == 5) {             // L R C BL BR
    wl[3] = h;
    wr[4] = h;
  } else if (n == 7) {      // L R C LFE BC SL SR
    wl[4] = wr[4] = 0.5f;
    wl[5] = h;
    wr[6] = h;
  } else if (n >= 6) {      // L R C LFE BL BR SL SR …
    for (int c = 4; c < n; c++) side(c, h);
  }
  float norm = 0;
  for (int c = 0; c < n; c++) norm += wl[c];
  const float k = norm > 1 ? 1.f / norm : 1.f;
  for (size_t i = 0; i < frames; i++) {
    float l = 0, r = 0;
    const float* s = in + i * size_t(ch);
    for (int c = 0; c < n; c++) {
      l += s[c] * wl[c];
      r += s[c] * wr[c];
    }
    out[2 * i] = l * k;
    out[2 * i + 1] = r * k;
  }
}

// Ресемплер стерео: windowed sinc (32 отсчёта, окно Кайзера), 256 фаз с линейной интерполяцией. Первый выходной кадр
// совпадает с первым входным (история заполнена нулями), flush() выдаёт хвост — длительность сохраняется.
class Resampler {
 public:
  void reset(int inRate, int outRate) {
    bypass_ = inRate == outRate || inRate <= 0 || outRate <= 0;
    hist_.assign(size_t(kHalf) * 2, 0.f);
    x_ = kHalf;
    if (bypass_) return;
    step_ = double(inRate) / double(outRate);
    const double cutoff = std::min(1.0, double(outRate) / double(inRate)) * 0.92;
    if (cutoff != cutoff_) build(cutoff);
  }
  void process(const float* in, size_t frames, std::vector<float>& out) {
    if (bypass_) {
      out.insert(out.end(), in, in + frames * 2);
      return;
    }
    hist_.insert(hist_.end(), in, in + frames * 2);
    const i64 total = i64(hist_.size() / 2);
    float w[kTaps];
    for (;;) {
      const i64 i0 = i64(std::floor(x_));
      if (i0 + kHalf >= total) break;
      const double pf = (x_ - double(i0)) * kPhases;
      const int p = std::min(int(pf), kPhases - 1);
      const float t = float(pf - p);
      const float* r0 = &table_[size_t(p) * kTaps];
      const float* r1 = r0 + kTaps;
      for (int j = 0; j < kTaps; j++) w[j] = r0[j] + (r1[j] - r0[j]) * t;
      const float* s = &hist_[size_t(i0 - kHalf + 1) * 2];
      float l = 0, r = 0;
      for (int j = 0; j < kTaps; j++) {
        l += s[2 * j] * w[j];
        r += s[2 * j + 1] * w[j];
      }
      out.push_back(l);
      out.push_back(r);
      x_ += step_;
    }
    const i64 drop = i64(std::floor(x_)) - kHalf + 1;
    if (drop > 0) {
      hist_.erase(hist_.begin(), hist_.begin() + std::ptrdiff_t(drop * 2));
      x_ -= double(drop);
    }
  }
  void flush(std::vector<float>& out) {
    if (bypass_) return;
    std::vector<float> zeros(size_t(kHalf) * 2, 0.f);
    process(zeros.data(), size_t(kHalf), out);
  }

 private:
  static constexpr int kHalf = 16, kTaps = 2 * kHalf, kPhases = 256;
  static double bessel0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 40; k++) {
      term *= (x / (2.0 * k)) * (x / (2.0 * k));
      sum += term;
      if (term < sum * 1e-12) break;
    }
    return sum;
  }
  void build(double cutoff) {
    cutoff_ = cutoff;
    constexpr double beta = 8.0;
    const double i0b = bessel0(beta);
    table_.assign(size_t(kPhases + 1) * kTaps, 0.f);
    for (int p = 0; p <= kPhases; p++) {
      const double f = double(p) / kPhases;
      double sum = 0;
      double row[kTaps];
      for (int j = 0; j < kTaps; j++) {
        const double t = f + double(kHalf - 1 - j);
        const double u = t / kHalf;
        double v = 0;
        if (std::fabs(u) < 1) {
          const double x = kPi * cutoff * t;
          const double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(x) / x;
          v = cutoff * sinc * bessel0(beta * std::sqrt(1 - u * u)) / i0b;
        }
        row[j] = v;
        sum += v;
      }
      for (int j = 0; j < kTaps; j++) table_[size_t(p) * kTaps + size_t(j)] = float(sum != 0 ? row[j] / sum : 0);
    }
  }
  bool bypass_ = true;
  double step_ = 1, x_ = kHalf, cutoff_ = -1;
  std::vector<float> table_, hist_;
};

// Линейное изменение к цели на step за кадр.
struct Ramp {
  float v = 0, target = 0, step = 1;
  float at(size_t i) const {
    const float d = target - v, m = step * float(i);
    return std::fabs(d) <= m ? target : v + (d > 0 ? m : -m);
  }
  void advance(size_t n) { v = at(n); }
};

enum class Cmd : u8 { Play, Toggle, Pause, Stop, Next, Prev, Seek, Select };
struct Command {
  Cmd kind;
  int track = -1;
  double value = 0;
};

// Отрезок вывода: с кадра устройства dev звучит трек track с кадра источника src.
struct Segment {
  i64 dev = 0;
  int track = -1;
  double src = 0, ratio = 1, rate = 1;   // ratio — кадров источника на кадр устройства
};

constexpr size_t kChunk = 1024;           // кадров устройства за одну запись
constexpr size_t kReadFrames = 1024;      // кадров источника за одно чтение декодера
constexpr double kFadeSec = 0.006;        // затухание и нарастание при паузе, перемотке и смене трека
constexpr double kVolumeSec = 0.03;       // плавность громкости

}  // namespace

// ================================================================ форматы и папка
const std::vector<std::string>& formats() {
  static const std::vector<std::string> list = [] {
    std::vector<std::string> v = detail::systemFormats();
    v.push_back("wav");
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
  }();
  return list;
}

bool supported(const std::string& path) {
  const std::string e = extOf(path);
  const auto& f = formats();
  return !e.empty() && std::find(f.begin(), f.end(), e) != f.end();
}

std::string titleFromFile(const std::string& file) {
  std::string s = fs::filename(file);
  const size_t dot = s.rfind('.');
  if (dot != std::string::npos && dot > 0) s.resize(dot);
  std::string out;
  for (char c : s) {
    if (c == '_') c = ' ';
    if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
    out.push_back(c);
  }
  out = trim(out);
  return out.empty() ? fs::filename(file) : out;
}

std::vector<Track> scanFolder(const std::string& dir) {
  std::vector<Track> out;
  if (dir.empty()) return out;
  for (const fs::DirEntry& e : fs::list(dir)) {
    if (e.dir || e.name.empty() || e.name[0] == '.' || !supported(e.name)) continue;
    Track t;
    t.path = e.path;
    t.file = e.name;
    std::ifstream f(fs::path(e.path), std::ios::binary);
    if (f) {
      auto readAt = [&f](u64 off, u8* dst, size_t n) -> size_t {
        f.clear();
        f.seekg(std::streamoff(off));
        f.read(reinterpret_cast<char*>(dst), std::streamsize(n));
        return size_t(f.gcount());
      };
      const codec::AudioInfo info = codec::probeAudio(readAt, e.size);
      t.title = info.title;
      t.artist = info.artist;
      t.duration = info.duration;
    }
    if (t.title.empty()) t.title = titleFromFile(e.name);
    out.push_back(std::move(t));
  }
  std::sort(out.begin(), out.end(), [](const Track& a, const Track& b) {
    const int c = compareRu(a.file, b.file);
    return c != 0 ? c < 0 : a.file < b.file;
  });
  return out;
}

// ================================================================ плеер
struct Player::Impl {
  // ---- общее состояние (под m)
  mutable std::mutex m;
  std::condition_variable cv;
  std::vector<Track> tracks;
  std::vector<int> order;            // порядок воспроизведения: по списку или перемешанный
  u64 listVersion = 1;
  std::deque<Command> cmds;
  bool quit = false;
  std::atomic<bool> closing{false};  // плеер завершается: окно больше не будим
  float volume = 0.6f;
  bool muted = false, shuffle = false, repeat = true;
  double tick = 0, tickUntil = 0;
  State state = State::Stopped;
  int shown = -1;                    // звучащий (показываемый) трек
  double pos = 0, dur = 0;
  std::string error;
  u64 errorSerial = 0;
  bool scanning = false;
  std::atomic<bool> interrupt{false};   // ждёт команда: прервать ожидание устройства
  std::atomic<bool> scanStop{false};
  std::atomic<float> volTarget{0.36f};
  Rng rng{u64(nowSeconds() * 1e6) ^ 0x5eedull};
  std::thread th, scanTh;

  // ---- поток воспроизведения (только он)
  std::unique_ptr<detail::Device> dev;
  std::unique_ptr<detail::Decoder> dec;
  int cur = -1;                      // трек декодера
  Resampler rs;
  std::vector<float> src, stereo, out, tmp;
  size_t outPos = 0;                 // кадров out уже отдано
  double srcPos = 0;                 // кадров источника прочитано
  bool ended = false;                // декодер дочитан, хвост ресемплера в out
  Ramp fade, vol;
  i64 written = 0;                   // кадров отдано устройству всего
  std::deque<Segment> segs;
  bool devBusy = false;              // после паузы или остановки в устройстве ещё звучит хвост
  i64 lastTick = -1, lastSecond = -1;

  // ---------------------------------------------------------------- общее
  void post(Command c) {
    {
      std::lock_guard lk(m);
      cmds.push_back(c);
      interrupt = true;
    }
    cv.notify_one();
  }
  void notify() {   // перерисовать окно (не во время завершения)
    if (!closing) platform::invalidate();
  }
  void updateVolTarget() { volTarget = muted ? 0.f : volume * volume; }
  // Порядок: по списку или перемешанный; first — первым (текущий трек остаётся на месте в начале).
  void rebuildOrder(int first) {
    const int n = int(tracks.size());
    order.resize(size_t(n));
    for (int i = 0; i < n; i++) order[size_t(i)] = i;
    if (!shuffle || n < 2) return;
    for (int i = n - 1; i > 0; i--) std::swap(order[size_t(i)], order[size_t(rng.range(0, i))]);
    if (first >= 0 && first < n) {
      auto it = std::find(order.begin(), order.end(), first);
      std::rotate(order.begin(), it, it + 1);
    }
  }
  // Следующий исправный трек в порядке воспроизведения от from (dir = ±1). wrapped — переход через край списка.
  int neighbor(int from, int dir, bool& wrapped) {
    wrapped = false;
    const int n = int(tracks.size());
    if (n == 0) return -1;
    if (int(order.size()) != n) rebuildOrder(from);
    int p = -1;
    for (int i = 0; i < n; i++)
      if (order[size_t(i)] == from) p = i;
    if (p < 0) p = dir > 0 ? -1 : 0;
    for (int k = 1; k <= n; k++) {
      int q = p + dir * k;
      if (q < 0 || q >= n) wrapped = true;
      q = ((q % n) + n) % n;
      const int t = order[size_t(q)];
      if (!tracks[size_t(t)].broken) return t;
    }
    return -1;
  }
  int firstPlayable() {
    for (int t : order)
      if (t >= 0 && t < int(tracks.size()) && !tracks[size_t(t)].broken) return t;
    for (int i = 0; i < int(tracks.size()); i++)
      if (!tracks[size_t(i)].broken) return i;
    return -1;
  }

  // ---------------------------------------------------------------- поток воспроизведения
  void run() {
    detail::threadBegin();
    std::unique_lock lk(m);
    while (!quit) {
      if (!cmds.empty()) {
        std::deque<Command> batch;
        batch.swap(cmds);
        interrupt = false;
        lk.unlock();
        for (const Command& c : batch) apply(c);
        lk.lock();
        continue;
      }
      if (state == State::Playing) {
        lk.unlock();
        pump();
        lk.lock();
        continue;
      }
      if (devBusy) {   // доиграть хвост и отпустить устройство
        lk.unlock();
        const bool done = drainStep();
        lk.lock();
        if (!done) cv.wait_for(lk, 15ms, [&] { return quit || !cmds.empty(); });
        continue;
      }
      cv.wait(lk, [&] { return quit || !cmds.empty(); });
    }
    lk.unlock();
    dec.reset();
    dev.reset();
    detail::threadEnd();
  }

  State getState() {
    std::lock_guard lk(m);
    return state;
  }
  void setState(State s) {
    {
      std::lock_guard lk(m);
      state = s;
    }
    notify();
  }
  void report(const std::string& msg) {
    {
      std::lock_guard lk(m);
      error = msg;
      errorSerial++;
    }
    logWarn("Плеер: %s", msg.c_str());
    notify();
  }
  void markBroken(int t, const std::string& why) {
    std::string file;
    {
      std::lock_guard lk(m);
      if (t < 0 || t >= int(tracks.size())) return;
      tracks[size_t(t)].broken = true;
      file = tracks[size_t(t)].file;
      listVersion++;
    }
    report("файл не воспроизводится: " + file + (why.empty() ? std::string() : " (" + why + ")"));
  }
  // Показать трек t с позиции sec (решение пользователя — сразу, не дожидаясь хвоста в устройстве).
  void show(int t, double sec) {
    {
      std::lock_guard lk(m);
      shown = t;
      pos = sec;
      dur = t >= 0 && t < int(tracks.size()) ? tracks[size_t(t)].duration : 0;
    }
    lastTick = lastSecond = -1;
    notify();
  }

  bool ensureDevice() {
    if (dev && dev->failed()) {
      dev.reset();
      devBusy = false;
    }
    if (dev) return true;
    std::string err;
    dev = detail::openDevice(&err);
    if (!dev) {
      report(err.empty() ? std::string("нет устройства вывода звука") : err);
      return false;
    }
    written = 0;
    segs.clear();
    fade.step = float(1.0 / (kFadeSec * dev->rate));
    vol.step = float(1.0 / (kVolumeSec * dev->rate));
    vol.v = vol.target = volTarget;
    return true;
  }

  // Открыть трек t для воспроизведения; не открылся — помечается испорченным.
  bool openTrack(int t) {
    std::string path;
    {
      std::lock_guard lk(m);
      if (t < 0 || t >= int(tracks.size())) return false;
      path = tracks[size_t(t)].path;
    }
    std::string err;
    auto d = openDecoder(path, &err);
    if (!d) {
      markBroken(t, err);
      return false;
    }
    if (d->frames > 0) {   // длительность декодера точнее оценки по заголовку
      const double sec = double(d->frames) / d->rate;
      std::lock_guard lk(m);
      Track& tr = tracks[size_t(t)];
      if (std::fabs(tr.duration - sec) > 0.25) {
        tr.duration = sec;
        listVersion++;
      }
    }
    dec = std::move(d);
    cur = t;
    rs.reset(dec->rate, dev->rate);
    out.clear();
    outPos = 0;
    srcPos = 0;
    ended = false;
    return true;
  }
  double trackDuration(int t) {
    std::lock_guard lk(m);
    return t >= 0 && t < int(tracks.size()) ? tracks[size_t(t)].duration : 0;
  }
  void pushSegment() {
    segs.push_back(Segment{written, cur, srcPos, double(dec->rate) / dev->rate, double(dec->rate)});
  }

  // Начать трек t с позиции sec (решение пользователя: текущий звук плавно уходит, новый плавно входит).
  void start(int t, double sec) {
    if (!ensureDevice()) {
      setState(State::Stopped);
      return;
    }
    if (getState() == State::Playing) fadeOutTail();
    dec.reset();
    for (int tries = 0; t >= 0 && !openTrack(t) && tries < 1000; tries++) {
      bool wrapped;
      std::lock_guard lk(m);
      t = neighbor(t, +1, wrapped);
    }
    if (!dec) {
      setState(State::Stopped);
      return;
    }
    if (sec > 0) {
      const i64 f = i64(sec * dec->rate);
      if (dec->seek(f)) srcPos = double(f);
    }
    segs.clear();
    pushSegment();
    fade.v = 0;
    fade.target = 1;
    show(cur, srcPos / dec->rate);
    setState(State::Playing);
  }

  void selectTrack(int t) {
    if (getState() == State::Playing) fadeOutTail();
    if (dev) devBusy = true;
    dec.reset();
    out.clear();
    outPos = 0;
    segs.clear();
    cur = t;
    show(t, 0);
    setState(State::Stopped);
  }

  void apply(const Command& c) {
    switch (c.kind) {
      case Cmd::Play: {
        bool ok;
        {
          std::lock_guard lk(m);
          ok = c.track >= 0 && c.track < int(tracks.size());
        }
        if (ok) start(c.track, 0);
        break;
      }
      case Cmd::Toggle: {
        const State s = getState();
        if (s == State::Playing) pauseNow();
        else if (s == State::Paused && dec) resume();
        else {
          int t;
          {
            std::lock_guard lk(m);
            t = shown >= 0 && shown < int(tracks.size()) && !tracks[size_t(shown)].broken ? shown : firstPlayable();
          }
          if (t >= 0) start(t, 0);
        }
        break;
      }
      case Cmd::Pause:
        if (getState() == State::Playing) pauseNow();
        break;
      case Cmd::Stop: stopNow(); break;
      case Cmd::Next:
      case Cmd::Prev: {
        const int dir = c.kind == Cmd::Next ? 1 : -1;
        int base, t;
        double at;
        bool wrapped;
        {
          std::lock_guard lk(m);
          base = shown >= 0 ? shown : cur;
          at = pos;
          t = neighbor(base, dir, wrapped);
        }
        const State s = getState();
        if (dir < 0 && at > 3 && dec && s != State::Stopped) {
          seekNow(0);
          break;
        }
        if (t < 0) break;
        if (s == State::Playing) start(t, 0);
        else selectTrack(t);
        break;
      }
      case Cmd::Seek: seekNow(c.value); break;
      case Cmd::Select:
        if (getState() != State::Playing || c.track != cur) selectTrack(c.track);
        break;
    }
  }

  void pauseNow() {
    fadeOutTail();
    devBusy = true;
    setState(State::Paused);
  }

  void resume() {
    if (!ensureDevice()) {
      setState(State::Stopped);
      return;
    }
    if (segs.empty()) pushSegment();
    fade.v = 0;
    fade.target = 1;
    setState(State::Playing);
  }

  void stopNow() {
    if (getState() == State::Playing) fadeOutTail();
    if (dev) devBusy = true;
    dec.reset();
    out.clear();
    outPos = 0;
    segs.clear();
    int t;
    {
      std::lock_guard lk(m);
      t = shown;
    }
    show(t, 0);
    setState(State::Stopped);
  }

  // Перемотка звучащего (показанного) трека; декодер мог уже перейти к следующему, пока доигрывается хвост.
  void seekNow(double sec) {
    const State s = getState();
    int t;
    {
      std::lock_guard lk(m);
      t = shown >= 0 ? shown : firstPlayable();
    }
    if (s == State::Playing) fadeOutTail();
    if (!dec || cur != t) {
      dec.reset();
      if (t < 0 || !ensureDevice() || !openTrack(t)) {
        if (!dec) setState(State::Stopped);
        return;
      }
    }
    const double d = dec->frames > 0 ? double(dec->frames) / dec->rate : trackDuration(cur);
    if (d > 0) sec = std::min(sec, std::max(0.0, d - 0.05));
    sec = std::max(0.0, sec);
    const i64 f = i64(sec * dec->rate);
    if (!dec->seek(f)) {
      report("перемотка не удалась");
      return;
    }
    rs.reset(dec->rate, dev->rate);
    out.clear();
    outPos = 0;
    srcPos = double(f);
    ended = false;
    segs.clear();
    pushSegment();
    show(cur, sec);
    if (s == State::Playing) {
      fade.v = 0;
      fade.target = 1;
    } else {
      setState(State::Paused);
    }
  }

  // Звук трека кончился: следующий трек без паузы или остановка в конце списка.
  void trackEnded() {
    int t;
    bool wrapped, rep, shuf;
    {
      std::lock_guard lk(m);
      rep = repeat;
      shuf = shuffle;
      t = neighbor(cur, +1, wrapped);
      if (wrapped && rep && shuf) {   // новый круг — новый порядок
        rebuildOrder(-1);
        if (order.size() > 1 && order[0] == cur) std::swap(order[0], order[1]);
        t = firstPlayable();
      }
    }
    dec.reset();
    if (t < 0 || (wrapped && !rep)) {
      devBusy = true;
      segs.clear();
      int first;
      {
        std::lock_guard lk(m);
        first = firstPlayable();
      }
      cur = first;
      show(first, 0);
      setState(State::Stopped);
      return;
    }
    for (int tries = 0; !openTrack(t) && tries < 1000; tries++) {
      std::lock_guard lk(m);
      t = neighbor(t, +1, wrapped);
      if (t < 0 || (wrapped && !rep)) break;
    }
    if (!dec) {
      devBusy = true;
      segs.clear();
      setState(State::Stopped);
      return;
    }
    pushSegment();   // трек сменится на экране, когда устройство доиграет предыдущий
  }

  // Следующая порция: декодер → стерео → частота устройства (в out).
  void refill() {
    out.clear();
    outPos = 0;
    if (!dec || ended) return;
    src.resize(kReadFrames * size_t(dec->channels));
    const size_t got = dec->read(src.data(), kReadFrames);
    if (got == 0) {
      if (!dec->error.empty()) logWarn("Плеер: %s", dec->error.c_str());
      rs.flush(out);
      ended = true;
      return;
    }
    toStereo(src.data(), got, dec->channels, stereo);
    rs.process(stereo.data(), got, out);
    srcPos += double(got);
  }

  size_t outFrames() const { return out.size() / 2; }

  // Отдать устройству до maxFrames кадров out с громкостью и затуханием.
  size_t writeOut(size_t maxFrames, const std::atomic<bool>& stop) {
    size_t n = std::min(outFrames() - outPos, maxFrames);
    if (fade.target <= 0) {
      if (fade.v <= 0) return 0;
      n = std::min(n, size_t(std::ceil(fade.v / fade.step)));
    }
    if (n == 0) return 0;
    vol.target = volTarget;
    tmp.resize(n * 2);
    const float* s = &out[outPos * 2];
    for (size_t i = 0; i < n; i++) {
      const float g = fade.at(i) * vol.at(i);
      tmp[2 * i] = s[2 * i] * g;
      tmp[2 * i + 1] = s[2 * i + 1] * g;
    }
    const size_t w = dev->write(tmp.data(), n, stop);
    fade.advance(w);
    vol.advance(w);
    outPos += w;
    written += i64(w);
    return w;
  }

  // Короткое затухание текущего звука (без щелчка); не дописанное — пропадает.
  void fadeOutTail() {
    if (!dec || !dev || fade.v <= 0) {
      fade.v = 0;
      return;
    }
    fade.target = 0;
    const std::atomic<bool> never{false};
    for (int guard = 0; fade.v > 0 && guard < 64; guard++) {
      if (outPos >= outFrames()) {
        refill();
        if (outPos >= outFrames()) break;
      }
      if (writeOut(kChunk, never) == 0) break;   // устройство занято — хвост не ждём
    }
    fade.v = 0;
  }

  void pump() {
    if (!ensureDevice()) {
      setState(State::Stopped);
      return;
    }
    if (!dec) {
      setState(State::Stopped);
      return;
    }
    if (outPos >= outFrames()) {
      refill();
      if (outPos >= outFrames() && ended) {
        trackEnded();
        return;
      }
    }
    writeOut(kChunk, interrupt);
    if (dev->failed()) {
      report("устройство вывода звука отключено");
      dev.reset();
      dec.reset();
      setState(State::Stopped);
      return;
    }
    updatePosition();
  }

  bool drainStep() {
    if (!dev) {
      devBusy = false;
      return true;
    }
    updatePosition();
    if (dev->queued() > 0 && !dev->failed()) return false;
    dev->idle();
    devBusy = false;
    return true;
  }

  // Позиция по тому, что устройство уже проиграло; трек на экране меняется вместе со звуком.
  void updatePosition() {
    if (!dev || segs.empty()) return;
    const i64 played = written - std::max<i64>(0, dev->queued());
    while (segs.size() > 1 && segs[1].dev <= played) segs.pop_front();
    const Segment& sg = segs.front();
    const double sec = (sg.src + double(std::max<i64>(0, played - sg.dev)) * sg.ratio) / sg.rate;
    bool wake = false;
    {
      std::lock_guard lk(m);
      if (shown != sg.track) {
        shown = sg.track;
        dur = shown >= 0 && shown < int(tracks.size()) ? tracks[size_t(shown)].duration : 0;
        wake = true;
      }
      pos = dur > 0 ? std::min(sec, dur) : sec;
      if (state == State::Playing && tick > 0 && nowSeconds() < tickUntil) {
        const i64 k = i64(std::floor(pos / tick)), s = i64(std::floor(pos));
        if (k != lastTick || s != lastSecond) {
          lastTick = k;
          lastSecond = s;
          wake = true;
        }
      }
    }
    if (wake) notify();
  }

  // ---------------------------------------------------------------- разбор папки
  void scanRun(std::string dir, std::string select) {
    std::vector<Track> list = scanFolder(dir);
    if (scanStop) return;
    int pick = -1;
    {
      std::lock_guard lk(m);
      tracks = std::move(list);
      listVersion++;
      scanning = false;
      for (int i = 0; i < int(tracks.size()); i++)
        if (!select.empty() && tracks[size_t(i)].file == select) pick = i;
      rebuildOrder(pick);
      if (pick < 0) pick = firstPlayable();
    }
    post(Command{Cmd::Stop});
    post(Command{Cmd::Select, pick});
    notify();
  }
};

Player::Player() : d_(std::make_unique<Impl>()) {
  d_->updateVolTarget();
  d_->th = std::thread([this] { d_->run(); });
}

Player::~Player() {
  d_->closing = true;
  d_->scanStop = true;
  {
    std::lock_guard lk(d_->m);
    d_->quit = true;
    d_->interrupt = true;
  }
  d_->cv.notify_all();
  if (d_->scanTh.joinable()) d_->scanTh.join();
  if (d_->th.joinable()) d_->th.join();
}

void Player::scan(const std::string& dir, const std::string& select) {
  if (d_->scanTh.joinable()) {
    d_->scanStop = true;
    d_->scanTh.join();
  }
  d_->scanStop = false;
  {
    std::lock_guard lk(d_->m);
    d_->scanning = true;
  }
  d_->scanTh = std::thread([this, dir, select] { d_->scanRun(dir, select); });
}

std::vector<Track> Player::tracks() const {
  std::lock_guard lk(d_->m);
  return d_->tracks;
}

Status Player::status() const {
  std::lock_guard lk(d_->m);
  Status s;
  s.state = d_->state;
  s.track = d_->shown;
  s.position = d_->pos;
  s.duration = d_->dur;
  s.volume = d_->volume;
  s.muted = d_->muted;
  s.shuffle = d_->shuffle;
  s.repeat = d_->repeat;
  s.scanning = d_->scanning;
  s.listVersion = d_->listVersion;
  s.errorSerial = d_->errorSerial;
  s.error = d_->error;
  return s;
}

void Player::play(int track) { d_->post(Command{Cmd::Play, track}); }
void Player::toggle() { d_->post(Command{Cmd::Toggle}); }
void Player::pause() { d_->post(Command{Cmd::Pause}); }
void Player::stop() { d_->post(Command{Cmd::Stop}); }
void Player::next() { d_->post(Command{Cmd::Next}); }
void Player::prev() { d_->post(Command{Cmd::Prev}); }
void Player::seek(double seconds) { d_->post(Command{Cmd::Seek, -1, seconds}); }
void Player::select(int track) { d_->post(Command{Cmd::Select, track}); }

void Player::setVolume(float v) {
  std::lock_guard lk(d_->m);
  d_->volume = clamp(std::isfinite(v) ? v : 0.f, 0.f, 1.f);
  d_->updateVolTarget();
}

void Player::setMuted(bool on) {
  std::lock_guard lk(d_->m);
  d_->muted = on;
  d_->updateVolTarget();
}

void Player::setShuffle(bool on) {
  std::lock_guard lk(d_->m);
  if (d_->shuffle == on) return;
  d_->shuffle = on;
  d_->rebuildOrder(d_->shown);
}

void Player::setRepeat(bool on) {
  std::lock_guard lk(d_->m);
  d_->repeat = on;
}

void Player::setTick(double seconds) {
  std::lock_guard lk(d_->m);
  d_->tick = std::max(0.0, seconds);
  d_->tickUntil = nowSeconds() + 3;
}

}  // namespace rg::platform::audio
