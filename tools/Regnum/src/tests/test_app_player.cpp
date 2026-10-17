// Плеер саундтреков в верхней строке: пустая папка (один неактивный значок), список треков по имени с тегами,
// воспроизведение, пауза, следующий трек и переход по окончании, громкость, перемешивание и повтор, настройки в app.json,
// узкое окно и параметры инструмента. Звук — тихое устройство headless: время трека идёт по виртуальному времени.
#include <thread>

#include "app/shell/player.h"
#include "codec/wav.h"
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;
namespace audio = rg::platform::audio;
namespace shell = rg::app::detail::shell;

namespace {

// WAV с тоном: seconds секунд, 44,1 кГц стерео; непустые title/artist — теги LIST/INFO.
void writeTone(const std::string& path, double seconds, std::string_view title = {}, std::string_view artist = {}) {
  codec::PcmAudio p;
  p.channels = 2;
  p.rate = 44100;
  const size_t n = size_t(seconds * p.rate);
  for (size_t i = 0; i < n; i++) {
    const float v = float(0.3 * std::sin(2 * kPi * 330.0 * double(i) / p.rate));
    p.samples.push_back(v);
    p.samples.push_back(v);
  }
  CHECK(fs::writeFileAtomic(path, codec::encodeWav(p, codec::WavSample::Pcm16, title, artist)));
}

audio::Player& engine() {
  audio::Player* e = shell::playerEngine();
  if (!e) test::fail(__FILE__, __LINE__, "плеер не создан");
  return *e;
}

// Ждать (реальное время), пока условие не выполнится: движок работает в своём потоке.
template <class F>
bool waitFor(F&& cond, double seconds = 5) {
  const double t0 = nowSeconds();
  while (nowSeconds() - t0 < seconds) {
    if (cond()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }
  return cond();
}

// Папка саундтреков теста: разобрать и показать.
void useFolder(Harness& h, const std::string& dir) {
  shell::playerUseFolder(dir);
  CHECK(waitFor([] { return !engine().status().scanning; }));
  CHECK(waitFor([] {
    const audio::Status s = engine().status();
    return !s.scanning && (s.track >= 0 || engine().tracks().empty());
  }));
  h.settle();
}

audio::Status st() { return engine().status(); }

// Две мелодии по 3 с (щелчок сценария сдвигает виртуальное время на 0,6 с).
std::string twoTracks(const std::string& name) {
  const std::string dir = tempDir(name);
  writeTone(fs::join(dir, "1_первая.wav"), 3.0);
  writeTone(fs::join(dir, "2_вторая.wav"), 3.0, "Вторая тема", "Оркестр");
  return dir;
}

bool inTopBar(Harness& h, const char* name) {
  const RectF* top = h->uiRect("topbar");
  const RectF* r = h->uiRect(name);
  return top && r && r->y >= top->y - 0.5f && r->bottom() <= top->bottom() + 0.5f;
}

}  // namespace

TEST(app_player_empty_folder_collapsed) {
  Harness h("player_empty");
  h.demo();
  useFolder(h, tempDir("player_empty_tracks"));
  const RectF* r = h->uiRect("topbar.player");
  CHECK(r != nullptr);
  CHECK(r && r->w <= 31);
  CHECK(h->uiRect("topbar.player.play") == nullptr);
  CHECK(h->uiRect("topbar.player.list") == nullptr);
  CHECK(inTopBar(h, "topbar.player"));
  // Подсказка значка — где взять музыку.
  h.move(r->cx(), r->cy());
  hl::advance(0.5);
  h.settle();
  CHECK(ui::tooltipText().find("нет саундтреков") != std::string::npos);
  CHECK_EQ(st().state, audio::State::Stopped);
}

TEST(app_player_scans_supported_files_by_name) {
  const std::string dir = tempDir("player_scan");
  writeTone(fs::join(dir, "b_Вторая__тема.wav"), 0.25);
  writeTone(fs::join(dir, "a_track.wav"), 0.5, "Рассвет", "Оркестр");
  writeTone(fs::join(dir, "10_last.wav"), 0.1);
  writeTone(fs::join(dir, "2_mid.wav"), 0.1);
  writeTone(fs::join(dir, ".hidden.wav"), 0.1);
  fs::makeDirs(fs::join(dir, "sub"));
  writeTone(fs::join(dir, "sub/inner.wav"), 0.1);
  CHECK(fs::writeFileAtomic(fs::join(dir, "notes.txt"), std::string("не музыка")));
  CHECK(fs::writeFileAtomic(fs::join(dir, "broken.wav"), std::string("RIFF0000WAVEjunk")));
  const std::vector<audio::Track> t = audio::scanFolder(dir);
  std::vector<std::string> files;
  for (const audio::Track& x : t) files.push_back(x.file);
  // Порядок — compareRu по имени (числа по значению); скрытые, вложенные и чужие файлы не попадают.
  CHECK_EQ(join(files, "|"), std::string("2_mid.wav|10_last.wav|a_track.wav|b_Вторая__тема.wav|broken.wav"));
  CHECK_EQ(t[0].title, std::string("2 mid"));
  CHECK_EQ(t[2].title, std::string("Рассвет"));
  CHECK_EQ(t[2].artist, std::string("Оркестр"));
  CHECK_NEAR(t[2].duration, 0.5, 1e-3);
  CHECK_EQ(t[3].title, std::string("b Вторая тема"));
  CHECK_EQ(audio::titleFromFile("Battle__theme_.MP3"), std::string("Battle theme"));
  CHECK_EQ(audio::titleFromFile("dir/Тема.flac"), std::string("Тема"));
  CHECK(audio::supported("x.WAV"));
  CHECK(!audio::supported("x.txt"));

  // В приложении: полный плеер, текущий — первый трек, без воспроизведения; испорченный файл помечается при попытке.
  Harness h("player_scan_app");
  h.demo();
  useFolder(h, dir);
  CHECK_EQ(st().track, 0);
  CHECK_EQ(st().state, audio::State::Stopped);
  for (const char* n : {"topbar.player.prev", "topbar.player.play", "topbar.player.next", "topbar.player.title",
                        "topbar.player.seek", "topbar.player.volume", "topbar.player.list"})
    CHECK_MSG(inTopBar(h, n), n);
  engine().play(4);
  CHECK(waitFor([] { return engine().tracks().size() == 5 && engine().tracks()[4].broken; }));
  CHECK(waitFor([] { return st().track == 0 && st().state == audio::State::Playing; }));   // следующий исправный по кругу
  h.settle();
  CHECK(hasToast(h, "broken.wav"));
  engine().stop();
  CHECK(waitFor([] { return st().state == audio::State::Stopped; }));
}

TEST(app_player_play_pause_next_and_auto_advance) {
  Harness h("player_play");
  h.demo();
  useFolder(h, twoTracks("player_play_tracks"));
  CHECK(h.clickUi("topbar.player.play"));
  CHECK(waitFor([] { return st().state == audio::State::Playing && st().track == 0; }));
  h.settle();
  CHECK(h.shot("player_playing"));
  if (const RectF* top = h->uiRect("topbar")) CHECK(cropShot("player_topbar", RectF{top->right() - 620, top->y, 620, top->h}));

  // Позиция идёт по времени устройства.
  const double p0 = st().position;
  hl::advance(0.3);
  CHECK(waitFor([&] { return st().position >= p0 + 0.25; }));

  // Пауза: позиция стоит (после хвоста в очереди устройства).
  CHECK(h.clickUi("topbar.player.play"));
  CHECK(waitFor([] { return st().state == audio::State::Paused; }));
  hl::advance(0.3);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  const double p1 = st().position;
  hl::advance(1.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  CHECK_NEAR(st().position, p1, 1e-9);
  CHECK(st().track == 0 && p1 < 3.0);

  // Продолжить, затем следующий трек.
  CHECK(h.clickUi("topbar.player.play"));
  CHECK(waitFor([] { return st().state == audio::State::Playing; }));
  CHECK(h.clickUi("topbar.player.next"));
  CHECK(waitFor([] { return st().track == 1 && st().state == audio::State::Playing && st().position < 1.5; }));

  // Конец списка с повтором (по умолчанию) — снова первый трек.
  hl::advance(3.0);
  CHECK(waitFor([] { return st().track == 0 && st().state == audio::State::Playing; }));

  // Без повтора — остановка после последнего, текущим становится первый. Сначала стоп: тихое устройство догоняет
  // виртуальное время, и долг большого шага не должен перейти на новый трек.
  engine().stop();
  CHECK(waitFor([] { return st().state == audio::State::Stopped; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  engine().setRepeat(false);
  engine().play(1);
  CHECK(waitFor([] { return st().track == 1 && st().state == audio::State::Playing && st().position < 0.3; }));
  hl::advance(3.5);
  CHECK(waitFor([] { return st().state == audio::State::Stopped && st().track == 0; }));

  // «Предыдущий» в начале трека — предыдущий по кругу; перемотка на остановленном треке ставит паузу в этом месте.
  CHECK(h.clickUi("topbar.player.prev"));
  CHECK(waitFor([] { return st().track == 1; }));
  engine().seek(0.4);
  CHECK(waitFor([] { return st().state == audio::State::Paused && std::fabs(st().position - 0.4) < 0.02; }));
}

TEST(app_player_seek_bar_click) {
  Harness h("player_seek");
  h.demo();
  useFolder(h, twoTracks("player_seek_tracks"));
  const RectF* bar = h->uiRect("topbar.player.seek");
  CHECK(bar != nullptr);
  if (!bar) return;
  const RectF b = *bar;
  // Щелчок на трёх четвертях полосы — перемотка туда (остановленный трек встаёт там на паузу).
  h.click(b.x + b.w * 0.75f, b.cy());
  CHECK(waitFor([] { return std::fabs(st().position - 2.25) < 0.05 && st().state == audio::State::Paused; }));
}

TEST(app_player_volume_shuffle_repeat_saved) {
  Harness h("player_prefs");
  h.demo();
  const std::string dir = twoTracks("player_prefs_tracks");
  useFolder(h, dir);
  CHECK_NEAR(st().volume, 0.6, 1e-6);
  CHECK(st().repeat && !st().shuffle && !st().muted);

  // Колесо над значком громкости: −5 % за щелчок.
  const RectF* vol = h->uiRect("topbar.player.volume");
  CHECK(vol != nullptr);
  if (!vol) return;
  const RectF v = *vol;
  h.wheel(v.cx(), v.cy(), -2);
  CHECK_NEAR(st().volume, 0.5, 1e-4);

  // Всплывающее окно громкости: ползунок (щелчок на четверти дорожки) и «Без звука».
  CHECK(h.clickUi("topbar.player.volume"));
  h.settle();
  const RectF* sl = h->uiRect("topbar.player.volume.slider");
  CHECK(sl != nullptr);
  if (sl) {
    const float track = sl->w - 16 - 52;   // дорожка ползунка: отступы и место под значение
    h.click(sl->x + 8 + track * 0.25f, sl->cy());
    CHECK(st().volume > 0.15f && st().volume < 0.35f);
  }
  CHECK(h.clickUi("topbar.player.mute"));
  CHECK(st().muted);
  h.key(platform::Key::Escape);

  // Список: перемешать, без повтора, щелчок по треку — играть.
  CHECK(h.clickUi("topbar.player.list"));
  h.settle();
  CHECK(h->uiRect("topbar.player.track.0") != nullptr && h->uiRect("topbar.player.track.1") != nullptr);
  CHECK(h.clickUi("topbar.player.shuffle"));
  CHECK(st().shuffle);
  CHECK(h.clickUi("topbar.player.repeat"));
  CHECK(!st().repeat);
  CHECK(h.clickUi("topbar.player.track.1"));
  CHECK(waitFor([] { return st().track == 1 && st().state == audio::State::Playing; }));
  h.settle();
  CHECK(h.shot("player_list"));
  h.key(platform::Key::Escape);
  h.settle();

  // app.json: объект «player».
  app::detail::savePrefs(h.a());
  auto text = fs::readFile(fs::join(h.dataDir, "app.json"));
  CHECK(text.has_value());
  auto j = json::tryParse(text.value_or(""));
  CHECK(j.has_value());
  const json::Value& p = j ? j->obj("player") : json::Value::object();
  CHECK_NEAR(p.num("volume", -1), double(std::round(st().volume * 100) / 100), 1e-9);
  CHECK(p.boolean("muted", false));
  CHECK(p.boolean("shuffle", false));
  CHECK(!p.boolean("repeat", true));
  CHECK_EQ(p.str("track"), std::string("2_вторая.wav"));

  // Новый запуск: настройки на месте, текущий — последний трек, без воспроизведения.
  const float savedVolume = st().volume;
  hl::attach(nullptr);
  h.app.reset();
  ui::shutdown();
  ui::init();
  app::AppConfig cfg;
  cfg.dataDir = h.dataDir;
  h.app = std::make_unique<app::App>(cfg);
  hl::attach(h.app.get());
  h.frames(2);
  h.demo();
  useFolder(h, dir);
  const audio::Status s = st();
  CHECK_NEAR(s.volume, savedVolume, 0.006);
  CHECK(s.muted && s.shuffle && !s.repeat);
  CHECK_EQ(s.track, 1);
  CHECK_EQ(s.state, audio::State::Stopped);
}

TEST(app_player_narrow_and_tool_options) {
  {  // Узкое окно: только кнопки; название, полоса и громкость — во всплывающем списке.
    Harness h("player_narrow", 1024, 700);
    h.demo();
    useFolder(h, twoTracks("player_narrow_tracks"));
    CHECK(inTopBar(h, "topbar.player.play") && inTopBar(h, "topbar.player.next") && inTopBar(h, "topbar.player.list"));
    CHECK(h->uiRect("topbar.player.title") == nullptr);
    CHECK(h->uiRect("topbar.player.volume") == nullptr);
    CHECK(h.clickUi("topbar.player.list"));
    h.settle();
    CHECK(h->uiRect("topbar.player.now.title") != nullptr && h->uiRect("topbar.player.mute") != nullptr);
    CHECK(h.shot("player_narrow_list"));
    h.key(platform::Key::Escape);
  }
  {  // Параметры инструмента главнее: плеер сжимается до кнопок и возвращается после.
    Harness h("player_tool_options", 1600, 900);
    h.demo();
    useFolder(h, twoTracks("player_tool_options_tracks"));
    CHECK(h->uiRect("topbar.player.title") != nullptr);
    h->setTool(app::ToolId::NewArmy);
    h.settle();
    CHECK(h->uiRect("topbar.player.title") == nullptr);
    CHECK(inTopBar(h, "tool.options"));
    CHECK(inTopBar(h, "topbar.player.play"));
    h->setTool(app::ToolId::Select);
    h.settle();
    CHECK(h->uiRect("topbar.player.title") != nullptr);
  }
  {  // Места нет и для кнопок — плеер скрывается, пока выбран инструмент; при пустой папке — и значок.
    Harness h("player_tool_options_hide");
    h.demo();
    useFolder(h, twoTracks("player_tool_options_hide_tracks"));
    h->setTool(app::ToolId::NewArmy);
    h.settle();
    CHECK(inTopBar(h, "tool.options"));
    CHECK(h->uiRect("topbar.player") == nullptr);
    useFolder(h, tempDir("player_tool_options_hide_empty"));
    h.settle();
    CHECK(inTopBar(h, "tool.options"));
    CHECK(h->uiRect("topbar.player") == nullptr);
    h->setTool(app::ToolId::Select);
    h.settle();
    CHECK(h->uiRect("topbar.player") != nullptr);
  }
}

TEST(app_player_redraws_only_with_progress) {
  // Пока звучит трек, окно будит только ход полосы и времени — не каждый кадр.
  Harness h("player_redraw");
  h.demo();
  const std::string dir = tempDir("player_redraw_tracks");
  writeTone(fs::join(dir, "long.wav"), 4.0);
  useFolder(h, dir);
  CHECK(h.clickUi("topbar.player.play"));
  CHECK(waitFor([] { return st().state == audio::State::Playing; }));
  h.settle();
  h.dropToasts();
  h.settle();
  // Без событий и с замершим временем кадр не нужен.
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  hl::renderFrame();
  CHECK(!ui::needsRedraw());
  CHECK(!h->animating());
  engine().stop();
  CHECK(waitFor([] { return st().state == audio::State::Stopped; }));
}
