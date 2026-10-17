// Сценарии проекта: сохранение и повторное чтение без потерь, Ctrl+S только изменённых таблиц, вопрос о
// несохранённых изменениях при закрытии, внешние изменения файлов (перечитать / оставить мои), автосохранение,
// архив .regnum и встроенный проводник (колесо листает список и при выбранной записи, в покое окно не
// перерисовывается). Файлы мира между сохранениями — test_app_project_files.cpp.
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;

TEST(app_project_save_reload) {
  Harness h("project_save");
  h.demo();
  CHECK(h->dirty());   // мир без файла
  std::string dir = fs::join(h.root, "Мир Ардена");
  CHECK(h->saveTo(dir));
  CHECK(!h->dirty());
  CHECK_EQ(h->projectPath(), fs::absolute(dir));
  h.step();
  CHECK_EQ(hl::title(), std::string("Regnum"));
  // Сохранённое совпадает с памятью.
  io::LoadResult r = io::load(dir);
  CHECK(r.warnings.empty());
  CHECK(io::toJson(r.world) == io::toJson(h->store.world()));
  // Правка → «•» в заголовке → Ctrl+S пишет только файл провинций и world.json.
  Id pid = test::firstOwnedLand(h->world());
  i64 geoTime = fs::mtime(fs::join(dir, "data/geo.json")).value_or(0);
  CHECK(h->act("Переименовать", [&](Tx& tx) { tx.province(pid).name = "Новое имя"; }));
  h.step();
  CHECK(h->dirty());
  CHECK_EQ(hl::title(), std::string("Regnum"));
  hl::advance(1);
  h.key(Key::S, ctrl());
  CHECK(!h->dirty());
  CHECK_EQ(fs::mtime(fs::join(dir, "data/geo.json")).value_or(-1), geoTime);
  r = io::load(dir);
  CHECK_EQ(r.world.province(pid)->name, std::string("Новое имя"));
  CHECK(io::toJson(r.world) == io::toJson(h->store.world()));
  // Открытие через приложение — тот же мир, в недавних.
  CHECK(h->loadProject(dir));
  CHECK(io::toJson(h->store.world()) == io::toJson(r.world));
  auto recent = io::recentProjects(h.dataDir);
  CHECK(!recent.empty());
  CHECK_EQ(recent.front().path, fs::absolute(dir));
  // Повторный Ctrl+S без изменений ничего не пишет.
  h.key(Key::S, ctrl());
  CHECK(!h->dirty());
  // Ошибка открытия — уведомление, мир прежний.
  CHECK(!h->loadProject(fs::join(h.root, "нет такого")));
  CHECK_EQ(h->projectPath(), fs::absolute(dir));
}

TEST(app_project_dirty_close) {
  Harness h("project_close");
  h.demo();
  Id pid = test::firstOwnedLand(h->world());
  CHECK(h->act("Правка", [&](Tx& tx) { tx.province(pid).contentment = 12; }));
  CHECK(!h->onCloseRequest());
  h.settle();
  CHECK(h->hasDialog("choice"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("close_prompt"));
  CHECK(h.clickUi("dialog.button.1"));   // Отмена
  h.settle();
  CHECK(!hl::quitRequested());
  CHECK(!h->hasDialog());
  // «Сохранить» для мира без файла — «Сохранить как», папка, затем выход.
  hl::queueDialogResult(fs::join(h.root, "При выходе"));
  CHECK(!h->onCloseRequest());
  h.settle();
  CHECK(h.clickUi("dialog.button.2"));
  h.settle();
  CHECK(h->hasDialog("world.saveas"));
  CHECK(h.clickUi("saveas.choose"));
  h.settle();
  CHECK(hl::quitRequested());
  CHECK(fs::isFile(fs::join(h.root, "При выходе/world.json")));
  CHECK(!h->dirty());
}

TEST(app_project_close_discard) {
  Harness h("project_discard");
  h.demo();
  CHECK(!h->onCloseRequest());
  h.settle();
  CHECK(h.clickUi("dialog.button.0"));   // Не сохранять
  h.settle();
  CHECK(hl::quitRequested());
  // Закрытие мира (Ctrl+W) без изменений — сразу к экрану запуска.
  Harness g("project_close_world");
  g.demo();
  CHECK(g->saveTo(fs::join(g.root, "Мир")));
  g.key(Key::W, ctrl());
  g.settle();
  CHECK(g->ui.screen == app::Screen::Start);
}

TEST(app_project_external_change) {
  Harness h("project_external");
  h.demo();
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  Id pid = test::firstOwnedLand(h->world());
  auto writeName = [&](const std::string& name) {
    World w = h->store.world();
    Tx tx(w);
    tx.province(pid).name = name;
    World w2 = std::move(tx).finish();
    CHECK(fs::writeFileAtomic(fs::join(dir, "data/provinces.json"), io::fileText(w2, "data/provinces.json")));
  };
  // Перечитать с диска.
  writeName("Извне");
  hl::setFocus(false);
  hl::setFocus(true);
  h.settle();
  CHECK(h->hasDialog("external"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("external_change"));
  CHECK(h.clickUi("external.reload"));
  h.settle();
  CHECK_EQ(h->world().province(pid)->name, std::string("Извне"));
  CHECK(!h->dirty());
  // Оставить мои: следующее сохранение заменяет файл на диске.
  writeName("Снова извне");
  hl::setFocus(true);
  h.settle();
  CHECK(h->hasDialog("external"));
  CHECK(h.clickUi("external.keep"));
  h.settle();
  CHECK(h->dirty());
  hl::setFocus(true);   // о тех же изменениях второй раз не спрашивает
  h.settle();
  CHECK(!h->hasDialog("external"));
  h.key(Key::S, ctrl());
  CHECK(!h->dirty());
  io::LoadResult r = io::load(dir);
  CHECK_EQ(r.world.province(pid)->name, std::string("Извне"));
}

TEST(app_project_autosave) {
  Harness h("project_autosave");
  h.demo();
  CHECK(h->act("Период", [](Tx& tx) { tx.settings().autosaveSec = 10; }));
  Id pid = test::firstOwnedLand(h->world());
  CHECK(h->act("Правка", [&](Tx& tx) { tx.province(pid).contentment = 33; }));
  hl::advance(11);
  h.frames(3);
  h->waitBackground();
  h.frames(2);
  auto list = io::listAutosaves(h.dataDir);
  CHECK(!list.empty());
  CHECK(list.front().project.empty());
  std::string err;
  auto as = io::loadAutosaveFor("", h.dataDir, &err);   // новый несохранённый мир
  CHECK(as.has_value());
  CHECK_NEAR(as->world.province(pid)->contentment, 33, 1e-9);
  // Папка проекта: изменения записываются сами (настройка «автосохранение в папку мира»).
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  CHECK(io::listAutosaves(h.dataDir).empty());   // после сохранения копия не нужна
  CHECK(h->act("Правка 2", [&](Tx& tx) { tx.province(pid).contentment = -20; }));
  CHECK(h->dirty());
  hl::advance(11);
  h.frames(3);
  h->waitBackground();
  h.frames(2);
  CHECK(!h->dirty());
  io::LoadResult r = io::load(dir);
  CHECK_NEAR(r.world.province(pid)->contentment, -20, 1e-9);
  // Без настройки — только копия в данных пользователя.
  CHECK(h->act("Без папки", [](Tx& tx) { tx.settings().autosaveFolder = false; }));
  CHECK(h->act("Правка 3", [&](Tx& tx) { tx.province(pid).contentment = 5; }));
  hl::advance(11);
  h.frames(3);
  h->waitBackground();
  h.frames(2);
  CHECK(h->dirty());
  CHECK(!io::listAutosaves(h.dataDir).empty());
}

TEST(app_project_bundle_browser) {
  Harness h("project_bundle");
  hl::setDialogsSupported(false);   // как на Linux без системных окон: встроенный проводник
  h.demo();
  h->saveAs();
  h.settle();
  CHECK(h->hasDialog("world.saveas"));
  CHECK(h.shot("saveas"));
  CHECK(h.clickUi("saveas.bundle"));
  CHECK(h.clickUi("saveas.choose"));
  h.settle();
  CHECK(h->hasDialog("browser"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("browser_save"));
  CHECK(h.clickUi("browser.ok"));
  h.settle();
  CHECK(h->projectIsBundle());
  std::string bundle = fs::join(h.root, "Демонстрационный мир.regnum");
  CHECK(fs::isFile(bundle));
  CHECK(!h->dirty());
  // Ещё мир-папка рядом.
  World w = h->store.world();
  CHECK(h->saveTo(fs::join(h.root, "Папка мира")));
  h->closeWorld();
  h.settle();
  CHECK(h->ui.screen == app::Screen::Start);
  // Открыть архив проводником: ↓ до архива (папки идут первыми), Enter.
  h->openBundleDialog();
  h.settle();
  CHECK(h->hasDialog("browser"));
  h.key(Key::Down);
  h.key(Key::Down);
  h.settle();
  CHECK(h.shot("browser_open"));
  h.key(Key::Enter);
  h.settle();
  CHECK(!h->hasDialog("browser"));
  CHECK(h->ui.screen == app::Screen::Editor);
  CHECK(h->projectIsBundle());
  CHECK(io::toJson(h->store.world()) == io::toJson(w));
  // Открыть папку мира: двойной щелчок по папке с world.json.
  h->closeWorld();
  h.settle();
  h->openWorldDialog();
  h.settle();
  const RectF* list = h->uiRect("browser.list");
  CHECK(list != nullptr);
  // В режиме папки мира видны только папки: единственная строка — «Папка мира».
  h.key(Key::Down);
  h.key(Key::Enter);
  h.settle();
  CHECK(h->ui.screen == app::Screen::Editor);
  CHECK(!h->projectIsBundle());
  CHECK(endsWith(h->projectPath(), "Папка мира"));
}

TEST(app_project_drop_files) {
  Harness h("project_drop");
  h.demo();
  std::string dir = fs::join(h.root, "Брошенный мир");
  CHECK(h->saveTo(dir));
  h->closeWorld();
  h.settle();
  hl::dropFiles(400, 300, {fs::join(dir, "world.json")});
  h.settle();
  CHECK(h->ui.screen == app::Screen::Editor);
  CHECK_EQ(h->projectPath(), fs::absolute(dir));
}

TEST(app_project_saveas_replace_cancel) {
  // «Сохранить» при выходе → «Сохранить как» → папка с чужим миром → «Заменить?» → Отмена: выхода нет,
  // и следующее сохранение не продолжает отменённый выход.
  Harness h("project_replace");
  h.demo();
  std::string other = fs::join(h.root, "Чужой мир");
  io::save(other, h->store.world(), TB_ALL);
  hl::queueDialogResult(other);
  CHECK(!h->onCloseRequest());
  h.settle();
  CHECK(h.clickUi("dialog.button.2"));   // Сохранить
  h.settle();
  CHECK(h.clickUi("saveas.choose"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.cancel"));
  h.settle();
  CHECK(!hl::quitRequested());
  CHECK(!h->impl().afterSave);
  CHECK(h->saveTo(fs::join(h.root, "Свой мир")));
  h.settle();
  CHECK(!hl::quitRequested());
}

TEST(app_project_autosave_wakes_idle_window) {
  // Окно простаивает (событий нет), а срок автосохранения наступает — будильник будит цикл событий.
  Harness h("project_wake");
  h.demo();
  hl::pump();
  h->impl().wakeTimer.arm(0.02);
  int got = 0;
  for (int i = 0; i < 100 && got == 0; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    got = hl::pump();
  }
  CHECK(got >= 1);
  // Новый срок позже уже взведённого не откладывает его, раньше — переносит.
  h->impl().wakeTimer.arm(30);
  h->impl().wakeTimer.arm(0.01);
  got = 0;
  for (int i = 0; i < 100 && got == 0; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    got = hl::pump();
  }
  CHECK(got >= 1);
}

// Встроенный проводник (на Linux — единственный способ открыть/сохранить мир): после щелчка по записи список
// каждый кадр прокручивается к ней (browser.cpp: if (sel >= 0) vl.scrollToRow(sel)) — колесо мыши не листает,
// окно перерисовывается без остановки.
TEST(app_project_browser_scroll_not_locked) {
  Harness h("audit_cr_browser");
  std::string dir = tempDir("audit_cr_browser_dirs");
  for (int i = 0; i < 40; i++) fs::makeDirs(fs::join(dir, strf("Папка %02d", i)));
  h->openDialog(app::detail::browserDialog(app::detail::BrowseMode::PickFolder, "Папка", dir, [](app::App&, const std::string&) {}));
  h.settle();
  CHECK(h->hasDialog("browser"));
  // Список — правая часть окна проводника (слева «Места»), строки по 32 точки.
  const RectF* lr = h->uiRect("browser.list");
  CHECK(lr != nullptr);
  std::printf("  browser.list rect: %.0f %.0f %.0f %.0f\n", lr->x, lr->y, lr->w, lr->h);
  const RectF* ok = h->uiRect("browser.ok");
  CHECK(ok != nullptr);
  RectF list{ok->right() - 540, ok->y - 400, 540, 352};
  // Щелчок по первой видимой папке — выбрана (золотая полоска слева).
  h.click(list.x + 80, list.y + 30);
  h.frames(20);
  auto accent = [&] {
    int n = 0;
    for (float y = list.y; y < list.bottom(); y += 1)
      for (float x = list.x; x < list.x + 10; x += 1) {
        u32 p = h.pixel(x, y);
        int r = int((p >> 16) & 255), g = int((p >> 8) & 255), b = int(p & 255);
        if (r > 190 && g > 130 && g < 190 && b < 110) n++;
      }
    return n;
  };
  int accBefore = accent();
  CHECK(h.shot("audit_cr_browser_before"));
  h.move(list.cx(), list.cy());
  hl::wheel(list.cx(), list.cy(), -15);
  h.step();
  h.frames(40);
  int accAfter = accent();
  CHECK(h.shot("audit_cr_browser_after_wheel"));
  std::printf("  browser: selected-row accent pixels before=%d after wheel=%d\n", accBefore, accAfter);
  CHECK(accBefore > 0);
  CHECK_MSG(!(accBefore > 0 && accAfter > 0), "встроенный проводник: колесо мыши не уводит список от выбранной записи (scrollToRow каждый кадр)");
  h.settle();   // полоса прокрутки гаснет после колеса — дождаться
  int busy = 0;
  for (int i = 0; i < 30; i++) {
    h.frame();
    if (h->animating()) busy++;
  }
  std::printf("  browser: busy frames while idle = %d/30\n", busy);
  CHECK_MSG(busy == 0, "встроенный проводник с выбранной записью перерисовывает окно каждый кадр без событий");
}
