// Regnum — плеер саундтреков в верхней строке: предыдущий трек, играть или пауза, следующий, название с полосой хода
// (щелчок и протяжка — перемотка) и временем, громкость (всплывающий ползунок, колесо над значком), список треков
// (щелчок — играть) с переключателями «Перемешать» и «Повтор». Треки — файлы папки assets/soundtracks, разобранные при
// запуске (platform::audio). Пустая папка — один неактивный значок; в узком окне и когда середина строки нужна
// параметрам инструмента — только кнопки (название, полоса и громкость — во всплывающем списке). Пока звучит трек,
// окно перерисовывается по ходу полосы (Player::setTick), без постоянной анимации.
#include "app/shell/player.h"

#include "base/fs.h"

namespace rg::app::detail::shell {

namespace audio = platform::audio;

namespace {

// Hidden — уступил место параметрам инструмента; Collapsed — нет треков (неактивный значок); Mini — играть и список;
// Compact — ещё предыдущий и следующий; Full — ещё название с полосой хода и громкость.
enum class Mode : u8 { Hidden, Collapsed, Mini, Compact, Full };

constexpr float kBtn = 30, kGap = 2;
constexpr float kRowH = 34;          // строка списка треков
constexpr int kListRows = 10;        // строк списка без прокрутки

struct PlayerUi {
  std::unique_ptr<audio::Player> engine;
  std::string dir;                   // папка саундтреков
  std::string lastTrack;             // файл последнего звучавшего трека (app.json)
  float volume = 0.6f;               // запомненное в app.json
  bool muted = false, shuffle = false, repeat = true;
  // кадр
  audio::Status st;
  u64 listVersion = 0, errorSerial = 0;
  std::vector<audio::Track> tracks;
  Mode mode = Mode::Collapsed;
  float infoW = 0;
  // параметры инструмента (раскладка прошлого кадра)
  int optTool = -1;                  // инструмент, к которому относится ширина
  float optW = 0;                    // их ширина в середине строки; 0 — неизвестна
  float overflowFree = -1;           // при такой середине строки они не помещаются и без плеера
  double tick = 0;                   // шаг перерисовки по ходу полосы (этот кадр)
  // перемотка: до ответа движка полоса показывает цель
  double seekTo = -1, seekAt = 0;
};

std::unique_ptr<PlayerUi>& player() {
  static std::unique_ptr<PlayerUi> p;
  return p;
}

// «3:07», «1:02:45».
std::string clockText(double sec) {
  const i64 t = std::isfinite(sec) && sec > 0 ? i64(sec) : 0;
  const i64 h = t / 3600, m = t / 60 % 60, s = t % 60;
  return h > 0 ? strf("%lld:%02lld:%02lld", (long long)h, (long long)m, (long long)s) : strf("%lld:%02lld", (long long)(t / 60), (long long)s);
}

const audio::Track* trackAt(const PlayerUi& p, int i) { return i >= 0 && i < int(p.tracks.size()) ? &p.tracks[size_t(i)] : nullptr; }

double trackDuration(const PlayerUi& p) {
  if (p.st.duration > 0) return p.st.duration;
  const audio::Track* t = trackAt(p, p.st.track);
  return t ? t->duration : 0;
}

// Состояние движка на кадр: список, новая ошибка (уведомление), изменения для app.json.
void sync(App& a, PlayerUi& p) {
  p.st = p.engine->status();
  if (p.st.listVersion != p.listVersion) {
    p.tracks = p.engine->tracks();
    p.listVersion = p.st.listVersion;
  }
  if (p.st.errorSerial != p.errorSerial) {
    p.errorSerial = p.st.errorSerial;
    if (!p.st.error.empty()) a.toast("Плеер: " + p.st.error, ToastKind::Warning, "music");
  }
  bool dirty = p.st.volume != p.volume || p.st.muted != p.muted || p.st.shuffle != p.shuffle || p.st.repeat != p.repeat;
  if (const audio::Track* t = trackAt(p, p.st.track); t && p.st.state == audio::State::Playing && t->file != p.lastTrack) {
    p.lastTrack = t->file;
    dirty = true;
  }
  if (!dirty) return;
  p.volume = p.st.volume;
  p.muted = p.st.muted;
  p.shuffle = p.st.shuffle;
  p.repeat = p.st.repeat;
  D(a).prefsDirty = true;
}

float modeWidth(Mode m, float infoW) {
  switch (m) {
    case Mode::Hidden: return 0;
    case Mode::Collapsed: return kBtn;
    case Mode::Mini: return 2 * kBtn + kGap;
    case Mode::Compact: return 4 * kBtn + 3 * kGap;
    case Mode::Full: return 3 * kBtn + 2 * kGap + 10 + infoW + 8 + 2 * kBtn + kGap;
  }
  return 0;
}

const char* volumeIcon(const audio::Status& st) {
  if (st.muted || st.volume <= 0.001f) return "volume-off";
  return st.volume < 0.5f ? "volume-low" : "volume";
}

std::string volumeTip(const audio::Status& st) {
  return st.muted ? std::string("Громкость: без звука") : "Громкость: " + fmtPct(std::round(st.volume * 100.0));
}

// Колесо над элементом — громкость ±5 % за щелчок.
void volumeWheel(PlayerUi& p) {
  const float wy = ui::mouse().wheelY;
  if (!ui::lastItem().hovered || wy == 0) return;
  p.engine->setVolume(clamp(p.st.volume + wy / 64.f * 0.05f, 0.f, 1.f));
  if (p.st.muted) p.engine->setMuted(false);
  ui::requestRedraw();
}

void volumeControls(App& a, PlayerUi& p);

// Без звука и ползунок громкости (всплывающее окно громкости и список в узком режиме); transport — впереди предыдущий
// и следующий трек (самый узкий режим, где их нет в строке).
void volumeRow(App& a, PlayerUi& p, bool transport = false) {
  if (transport) {
    ui::Row row({ui::px(kBtn), ui::px(kBtn), ui::px(kBtn), ui::fr()}, kBtn, 4);
    if (ui::iconButton("skip-prev", "Предыдущий трек")) p.engine->prev();
    a.markUi("topbar.player.now.prev");
    if (ui::iconButton("skip-next", "Следующий трек")) p.engine->next();
    a.markUi("topbar.player.now.next");
    volumeControls(a, p);
    return;
  }
  ui::Row row({ui::px(kBtn), ui::fr()}, kBtn, 4);
  volumeControls(a, p);
}

void volumeControls(App& a, PlayerUi& p) {
  const bool off = p.st.muted || p.st.volume <= 0.001f;
  if (ui::iconButton(volumeIcon(p.st), off ? "Включить звук" : "Без звука")) {
    if (p.st.muted) p.engine->setMuted(false);
    else if (p.st.volume <= 0.001f) p.engine->setVolume(0.6f);
    else p.engine->setMuted(true);
  }
  a.markUi("topbar.player.mute");
  double pct = std::round(p.st.volume * 100.0);
  if (ui::slider("volume", pct, 0, 100, {.step = 1, .unit = "%"})) {
    p.engine->setVolume(float(pct / 100.0));
    if (p.st.muted && pct > 0) p.engine->setMuted(false);
    ui::requestRedraw();
  }
  a.markUi("topbar.player.volume.slider");
}

// Название трека, время и полоса хода в box (высота 40): щелчок и протяжка по полосе — перемотка.
void nowPlaying(App& a, PlayerUi& p, RectF box, std::string_view mark) {
  const ui::Theme& th = ui::theme();
  const audio::Status& st = p.st;
  const audio::Track* t = trackAt(p, st.track);
  const double dur = trackDuration(p);
  const bool playing = st.state == audio::State::Playing;
  const bool canSeek = t && dur > 0;

  const RectF bar{box.x, box.y + 28, box.w, 3};
  const RectF hit{box.x, box.y + 21, box.w, 16};
  const ui::WidgetId wid = ui::id("seek");
  const ui::Interaction it = ui::interact(wid, hit);
  double at = -1;   // время под указателем
  if (canSeek && (it.hovered || it.held || it.released))
    at = clamp(double((it.mx - bar.x) / std::max(1.f, bar.w)), 0.0, 1.0) * dur;
  if (canSeek && it.released && at >= 0) {
    p.engine->seek(at);
    p.seekTo = at;
    p.seekAt = platform::time();
  }
  if (canSeek && (it.hovered || it.held)) ui::setCursor(platform::Cursor::Hand);
  a.markUi(std::string(mark) + ".seek", hit);

  double pos = st.position;
  if (p.seekTo >= 0) {   // ждём движок после перемотки
    if (platform::time() - p.seekAt < 0.6 && std::fabs(st.position - p.seekTo) > 0.3) pos = p.seekTo;
    else p.seekTo = -1;
  }
  const bool scrub = canSeek && it.held && at >= 0;
  if (scrub) pos = at;
  const bool hot = canSeek && (it.hovered || it.held);
  const float hv = ui::animate(wid ^ 0x5eeull, hot ? 1.f : 0.f);
  const float h = 3 + 2 * hv;
  const RectF track{bar.x, bar.y + 1.5f - h * 0.5f, bar.w, h};
  const float k = dur > 0 ? float(clamp(pos / dur, 0.0, 1.0)) : 0.f;
  ui::draw::rect(track, th.track, h * 0.5f);
  if (k > 0) ui::draw::rect(RectF{track.x, track.y, std::max(h, track.w * k), h}, playing ? th.accent : th.textDim, h * 0.5f);
  if (hv > 0.01f) ui::draw::circle(track.x + track.w * k, track.cy(), 5.f * hv, th.accentHover);

  // Время: под указателем на полосе — время перемотки (акцентом).
  const double shownAt = hot && at >= 0 ? at : pos;
  const std::string time = dur > 0 ? clockText(shownAt) + " / " + clockText(dur) : clockText(shownAt);
  const float tw = std::ceil(ui::measure(time, ui::Font::Small)) + 2;
  const RectF row{box.x, box.y + 4, box.w, 18};
  ui::draw::text(time, RectF{box.right() - tw, row.y, tw, row.h}, ui::Font::Small, hot ? th.accent : th.textMuted, ui::Align::Right);
  const RectF tr{box.x, row.y, std::max(0.f, box.w - tw - 8), row.h};
  const std::string title = t ? t->title : std::string("—");
  ui::draw::text(title, tr, ui::Font::Body, playing ? th.text : th.textDim);
  if (t) {
    std::string tip = t->title;
    if (!t->artist.empty()) tip += "\n" + t->artist;
    ui::hoverTip("title", tr, tip);
  }
  a.markUi(std::string(mark) + ".title", tr);
  if (playing && dur > 0) p.tick = std::max(p.tick, clamp(dur / std::max(1.0, double(bar.w)), 0.05, 1.0));
}

// Список треков: строки (щелчок — играть, по текущему — пауза), справа длительность.
void trackList(App& a, PlayerUi& p) {
  const ui::Theme& th = ui::theme();
  const int n = int(p.tracks.size());
  const bool playing = p.st.state == audio::State::Playing;
  ui::VirtualList list("tracks", n, kRowH, kRowH * float(std::min(n, kListRows)));
  for (int i : list) {
    const audio::Track& t = p.tracks[size_t(i)];
    const RectF rr = ui::next(kRowH);
    const ui::WidgetId rid = ui::id("row");
    const ui::Interaction it = ui::interact(rid, rr);
    const bool cur = i == p.st.track;
    const float hv = ui::animate(rid ^ 0x70ull, it.hovered ? 1.f : 0.f);
    if (cur) ui::draw::rect(rr, th.accent.alpha(0.12f), 6);
    if (hv > 0.01f) ui::draw::rect(rr, th.hover.alpha(hv), 6);
    // Слева: номер, у звучащего — значок звука, под указателем — «играть».
    const RectF lead{rr.x + 4, rr.y, 24, rr.h};
    if (cur && playing) ui::draw::icon("volume", RectF{lead.x + 4, rr.cy() - 8, 16, 16}, th.accent);
    else if ((it.hovered || cur) && !t.broken) ui::draw::icon("play", RectF{lead.x + 4, rr.cy() - 8, 16, 16}, cur ? th.accent : th.text);
    else ui::draw::text(std::to_string(i + 1), lead, ui::Font::Small, th.textMuted, ui::Align::Center);
    // Справа: длительность или знак сбоя.
    const float dw = 46;
    const RectF dr{rr.right() - dw - 6, rr.y, dw, rr.h};
    if (t.broken) {
      ui::draw::icon("warning", RectF{dr.right() - 16, rr.cy() - 8, 16, 16}, th.warning);
      ui::hoverTip("broken", dr, "Файл не воспроизводится");
    } else if (t.duration > 0) {
      ui::draw::text(clockText(t.duration), dr, ui::Font::Small, th.textMuted, ui::Align::Right);
    }
    // Название и исполнитель.
    const RectF text{lead.right() + 6, rr.y, std::max(0.f, dr.x - lead.right() - 12), rr.h};
    const float titleW = std::ceil(ui::measure(t.title, ui::Font::Body)) + 2;
    const float nw = std::min(titleW, text.w);
    const Color tc = t.broken ? th.textMuted : cur ? th.accent : th.text;
    ui::draw::text(t.title, RectF{text.x, text.y, nw, text.h}, ui::Font::Body, tc);
    float used = titleW;
    if (!t.artist.empty() && text.w - nw > 40) {
      ui::draw::text(t.artist, RectF{text.x + nw + 8, text.y, text.w - nw - 8, text.h}, ui::Font::Small, th.textMuted);
      used += 8 + std::ceil(ui::measure(t.artist, ui::Font::Small));
    }
    if (used > text.w) ui::hoverTip("full", text, t.artist.empty() ? t.title : t.title + "\n" + t.artist);
    if (it.clicked && !t.broken) {
      if (cur) p.engine->toggle();
      else p.engine->play(i);
      ui::requestRedraw();
    }
    a.markUi("topbar.player.track." + std::to_string(i), rr);
  }
}

// Всплывающий список: в узком режиме сверху — звучащий трек и громкость; заголовок с переключателями; треки.
void listPopup(App& a, PlayerUi& p) {
  const float maxH = std::max(200.f, ui::viewport().h - 80);
  if (!ui::beginPopup("list", {.side = ui::Side::Below, .width = 380, .maxHeight = maxH})) return;
  if (p.mode == Mode::Compact || p.mode == Mode::Mini) {
    nowPlaying(a, p, ui::next(40), "topbar.player.now");
    volumeRow(a, p, p.mode == Mode::Mini);
    ui::separator();
  }
  {
    ui::HStack hs(kBtn, ui::Align::Left, 4);
    ui::label("Саундтреки", {.font = ui::Font::Strong});
    ui::label(std::to_string(p.tracks.size()), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    ui::flex();
    bool shuffle = p.st.shuffle;
    if (ui::iconToggle("shuffle", "Перемешать", shuffle)) p.engine->setShuffle(shuffle);
    a.markUi("topbar.player.shuffle");
    bool repeat = p.st.repeat;
    if (ui::iconToggle("repeat", "Повтор", repeat)) p.engine->setRepeat(repeat);
    a.markUi("topbar.player.repeat");
    if (ui::iconButton("folder-open", "Открыть папку саундтреков")) platform::openPath(p.dir);
    a.markUi("topbar.player.folder");
  }
  trackList(a, p);
  ui::endPopup();
}

// Пустая папка: неактивный значок, в подсказке — где взять музыку.
void collapsed(App& a, PlayerUi& p, RectF ir) {
  const ui::Theme& th = ui::theme();
  ui::draw::icon("music", RectF{ir.x + 6, ir.y + 6, 18, 18}, th.textMuted.alpha(0.55f));
  std::string tip;
  if (p.st.scanning) {
    tip = "Плеер: поиск саундтреков…";
  } else if (p.dir.empty()) {
    tip = "Плеер: папка саундтреков не найдена (assets/soundtracks)";
  } else {
    std::vector<std::string> f;
    for (const std::string& e : audio::formats()) f.push_back(utf8::upper(e));
    tip = "Плеер: в папке нет саундтреков\n" + p.dir + "\n" + join(f, ", ");
  }
  ui::hoverTip("empty", ir, tip);
  a.markUi("topbar.player", ir);
}

}  // namespace

std::string soundtracksDir() {
  const std::string assets = map::findAssetsDir();
  return assets.empty() ? std::string() : fs::join(assets, "soundtracks");
}

void playerAttach(App& a, const json::Value& prefs) {
  auto& slot = player();
  slot.reset();   // прежний плеер (прежнее приложение) останавливается
  auto p = std::make_unique<PlayerUi>();
  p->engine = std::make_unique<audio::Player>();
  p->volume = float(clamp(prefs.num("volume", 0.6), 0.0, 1.0));   // не объект — значения по умолчанию
  p->muted = prefs.boolean("muted", false);
  p->shuffle = prefs.boolean("shuffle", false);
  p->repeat = prefs.boolean("repeat", true);
  p->lastTrack = prefs.str("track");
  p->engine->setVolume(p->volume);
  p->engine->setMuted(p->muted);
  p->engine->setShuffle(p->shuffle);
  p->engine->setRepeat(p->repeat);
  p->dir = soundtracksDir();
  if (!p->dir.empty()) p->engine->scan(p->dir, p->lastTrack);
  p->st = p->engine->status();
  p->listVersion = p->st.listVersion;
  slot = std::move(p);
}

json::Value playerPrefs() {
  const PlayerUi* p = player().get();
  if (!p) return json::Value();
  const audio::Status st = p->engine->status();
  json::Value v = json::Value::object();
  v.set("volume", std::round(double(st.volume) * 100.0) / 100.0);
  v.set("muted", st.muted);
  v.set("shuffle", st.shuffle);
  v.set("repeat", st.repeat);
  v.set("track", p->lastTrack);
  return v;
}

void playerUseFolder(const std::string& dir) {
  PlayerUi* p = player().get();
  if (!p) return;
  p->dir = dir;
  p->engine->scan(dir, p->lastTrack);
}

audio::Player* playerEngine() {
  PlayerUi* p = player().get();
  return p ? p->engine.get() : nullptr;
}

float playerLayout(App& a, float free, bool narrow) {
  PlayerUi* p = player().get();
  if (!p) return 0;
  sync(a, *p);
  p->infoW = ui::viewport().w >= 1600 ? 210.f : 160.f;
  // Место плеера в строке: сам плеер и разделитель перед ним.
  const auto room = [&](Mode m) { return m == Mode::Hidden ? 0.f : modeWidth(m, p->infoW) + 14; };
  // Варианты от широкого к узкому; Hidden — только чтобы уступить место параметрам инструмента.
  std::vector<Mode> modes;
  if (p->tracks.empty()) modes = {Mode::Collapsed};
  else if (narrow) modes = {Mode::Compact, Mode::Mini};
  else modes = {Mode::Full, Mode::Compact, Mode::Mini};
  // Самый широкий вариант, при котором в середине строки остаётся need точек (иначе самый узкий).
  const auto widest = [&](float need, bool canHide) {
    for (Mode m : modes)
      if (free - room(m) >= need) return m;
    return canHide && free >= need ? Mode::Hidden : modes.back();
  };
  Mode m = widest(0, false);
  if (const RectF* o = a.uiRect("tool.options")) {
    // Параметры инструмента главнее плеера: плеер сжимается (до скрытия), пока они помещаются в середине строки.
    // Их ширина известна, пока они в строке; строкой над картой — пробный кадр без плеера: не поместились и так —
    // плеер занимает свободную строку.
    const float s = ui::uiScale();
    const RectF opt{o->x / s, o->y / s, o->w / s, o->h / s};
    const bool inBar = opt.y < kTopH - 1;
    const int tool = int(a.ui.tool);
    if (tool != p->optTool) {
      p->optTool = tool;
      p->optW = 0;
      p->overflowFree = -1;
    }
    if (inBar) {
      p->optW = opt.w;
      p->overflowFree = -1;
    } else if (p->optW > 0 && free - room(p->mode) >= p->optW) {
      p->optW = 0;   // параметры выросли: прежняя ширина устарела
    }
    if (p->optW > 0) m = free >= p->optW ? widest(p->optW, true) : widest(0, false);
    else if (std::fabs(p->overflowFree - free) < 1) m = widest(0, false);
    else if (p->mode == Mode::Hidden && !inBar) {
      p->overflowFree = free;
      m = widest(0, false);
    } else {
      m = Mode::Hidden;
    }
  }
  if (m != p->mode) {
    p->mode = m;
    ui::requestRedraw();
  }
  return m == Mode::Hidden ? 0.f : modeWidth(m, p->infoW);
}

void playerBar(App& a, RectF r) {
  PlayerUi* p = player().get();
  if (!p) return;
  ui::IdScope scope("player");
  p->tick = 0;
  const float by = r.y + std::round((r.h - kBtn) * 0.5f);
  if (p->mode == Mode::Collapsed) {
    collapsed(a, *p, RectF{r.x, by, kBtn, kBtn});
    p->engine->setTick(0);
    return;
  }
  a.markUi("topbar.player", r);
  const audio::Status& st = p->st;
  const bool playing = st.state == audio::State::Playing;
  float x = r.x;
  auto button = [&](const char* icon, std::string_view tip, const char* mark, ui::Tone tone = ui::Tone::Neutral) {
    ui::at(RectF{x, by, kBtn, kBtn});
    const bool clicked = ui::iconButton(icon, tip, {.tone = tone});
    a.markUi(mark);
    x += kBtn + kGap;
    if (clicked) ui::requestRedraw();
    return clicked;
  };
  const bool mini = p->mode == Mode::Mini;
  if (!mini && button("skip-prev", "Предыдущий трек", "topbar.player.prev")) p->engine->prev();
  if (button(playing ? "pause" : "play", playing ? "Пауза" : "Играть", "topbar.player.play", playing ? ui::Tone::Accent : ui::Tone::Neutral))
    p->engine->toggle();
  if (!mini && button("skip-next", "Следующий трек", "topbar.player.next")) p->engine->next();
  if (p->mode == Mode::Full) {
    x += 10 - kGap;
    nowPlaying(a, *p, RectF{x, r.y, p->infoW, r.h}, "topbar.player");
    x += p->infoW + 8;
    if (button(volumeIcon(st), volumeTip(st), "topbar.player.volume")) ui::openPopup("volume");
    volumeWheel(*p);
    if (ui::beginPopup("volume", {.side = ui::Side::Below, .width = 240})) {
      volumeRow(a, *p);
      ui::endPopup();
    }
  }
  if (button("playlist", "Список треков", "topbar.player.list")) ui::openPopup("list");
  listPopup(a, *p);
  p->engine->setTick(p->tick);
}

// ================================================================ команды (палитра Ctrl+K)
namespace {

bool hasTracks(App&) {
  audio::Player* e = playerEngine();
  return e && !e->tracks().empty();
}

CommandReg cPlayerToggle({"player.toggle", "Музыка: играть или пауза", "play", nullptr,
                          [](App&) {
                            if (audio::Player* e = playerEngine()) e->toggle();
                          }, hasTracks, false, "Музыка", 150});
CommandReg cPlayerNext({"player.next", "Музыка: следующий трек", "skip-next", nullptr,
                        [](App&) {
                            if (audio::Player* e = playerEngine()) e->next();
                          }, hasTracks, false, "Музыка", 150});
CommandReg cPlayerPrev({"player.prev", "Музыка: предыдущий трек", "skip-prev", nullptr,
                        [](App&) {
                            if (audio::Player* e = playerEngine()) e->prev();
                          }, hasTracks, false, "Музыка", 150});

}  // namespace

}  // namespace rg::app::detail::shell
