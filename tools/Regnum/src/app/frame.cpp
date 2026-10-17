// Regnum — события окна и кадр: ввод, анимации, закрытие, сборка интерфейса, карта с кешем и отрисовка.
#include "app/app_internal.h"
#include "app/shell/role.h"
#include "base/jobs.h"

namespace rg::app {

// ================================================================ события
void App::onEvent(const platform::Event& e) {
  using T = platform::EventType;
  Impl& d = *d_;
  switch (e.type) {
    case T::KeyDown:
      if (e.key == platform::Key::Space && !ui::wantsKeyboard()) d.spaceDown = true;
      break;
    case T::KeyUp:
      if (e.key == platform::Key::Space) d.spaceDown = false;
      break;
    case T::FocusOut:
      d.spaceDown = false;
      break;
    case T::FocusIn:
      d_->later.push_back([](App& a) { a.checkExternalChanges(); });
      break;
    case T::FilesDropped:
      if (!e.files.empty()) {
        std::string p = e.files.front();
        d_->later.push_back([p](App& a) { a.openPath(p); });
      }
      break;
    default:
      break;
  }
  ui::onEvent(e);
  platform::invalidate();
}

bool App::animating() {
  Impl& d = *d_;
  if (ui::needsRedraw()) return true;
  if (!toasts_.empty()) return true;
  if (!d_->later.empty()) return true;
  if (mapShown()) {
    if (d.map->animating() || d.map->needsRedraw()) return true;
    if (tool_ && tool_->animating(*this)) return true;
  }
  if (d.autosaveJob.valid()) return true;
  return false;
}

bool App::onCloseRequest() {
  if (!dirty()) {
    waitBackground();
    if (ui.screen == Screen::Editor) io::clearAutosave(d_->path, d_->dataDir);
    if (d_->prefsDirty) detail::savePrefs(*this);
    return true;
  }
  requestQuit();
  return false;
}

// ================================================================ кадр
void App::onFrame(platform::Frame& f) {
  Impl& d = *d_;
  const double p0 = nowSeconds();
  frames_++;
  time_ = platform::time();
  d.lw = f.logicalW();
  d.lh = f.logicalH();
  d.dpi = f.scale;
  if (ui::scheme() != ui.scheme) {   // цветокор задан полем (настройки, тесты)
    ui::setScheme(ui.scheme);
    d.backdropW = 0;
    d.mapKey = 0;
  }
  if (const map::Palette pal = detail::mapPalette(*this); d.map->palette() != pal) {
    d.map->setPalette(pal);
    d.mapGen++;
  }
  if (std::fabs(ui::uiScale() - ui.uiScale) > 1e-3f) ui::setUiScale(ui.uiScale);
  if (d.frameImg.w != f.w || d.frameImg.h != f.h) d.frameImg.resize(f.w, f.h);
  d.map->setViewport(RectF{0, 0, d.lw, d.lh}, d.dpi);
  d.map->update(time_);
  d.rectsNext.clear();

  ui::beginFrame(d.lw, d.lh, d.dpi, time_);
  try {
    buildFrame();
  } catch (const std::exception& e) {
    error(e);
  }
  // Свободная часть карты между панелями (раскладка этого кадра): в неё «Показать всю карту» вписывает мир,
  // по ней же минимальный масштаб. Вся карта при открытии мира — когда эта часть уже известна.
  {
    auto it = d.rectsNext.find("map.area");
    d.map->setSafeArea(it != d.rectsNext.end() ? it->second : RectF{});
  }
  if (d.fitPending && d.lw > 1 && d.lh > 1) {
    d.map->fitAll(false);
    d.fitPending = false;
  }
  const double p1 = nowSeconds();
  gfx::Canvas c(d.frameImg);
  try {
    if (mapShown()) detail::renderMap(*this, c);
    else if (ui.screen == Screen::Start) detail::renderStartBackdrop(*this, c);
    else c.clear(ui::theme().bg);
  } catch (const std::exception& e) {
    c.clear(ui::theme().bg);
    error(e);
  }
  const double p2 = nowSeconds();
  ui::endFrame(c);
  const double p3 = nowSeconds();
  // Прямоугольники элементов обновляются на месте: указатель uiRect на элемент, который рисуется и в новом кадре,
  // остаётся действительным (тесты держат его между кадрами); исчезнувшие элементы удаляются.
  for (auto it = d.rects.begin(); it != d.rects.end();)
    it = d.rectsNext.count(it->first) ? std::next(it) : d.rects.erase(it);
  for (const auto& [name, r] : d.rectsNext) d.rects[name] = r;

  // Курсор: интерфейс, иначе инструмент карты.
  platform::Cursor cur = ui::cursor();
  if (cur == platform::Cursor::Arrow && d.mapHovered && mapShown() && !hasDialog()) {
    if (d.panning) cur = platform::Cursor::Grabbing;
    else if (d.spaceDown || ui.tool == ToolId::Pan) cur = platform::Cursor::Grab;
    else if (tool_) cur = tool_->cursor(*this);
  }
  platform::setCursor(cur);
  if (auto r = ui::textInputRect()) platform::setTextInputRect(*r);

  // Заголовок окна — просто «Regnum» (название мира и состояние сохранения — в верхней панели).
  if (d.title.empty()) {
    d.title = "Regnum";
    platform::setTitle(d.title);
  }

  // Кадр для окна — прямо из буфера кадра (Windows выводит его без копии, прочие платформы копируют сами). Если ждут
  // отложенные действия, кадр копируется в буфер окна: системный диалог крутит свой цикл сообщений, и окно под ним
  // перерисовывается из этого буфера.
  const double p4 = nowSeconds();
  if (d.later.empty())
    f.source = d.frameImg.px.data();
  else
    for (int y = 0; y < f.h; y++) std::memcpy(f.row(y), d.frameImg.row(y), size_t(f.w) * sizeof(u32));
  const double p5 = nowSeconds();
  profile_ = FrameProfile{(p1 - p0) * 1000, (p2 - p1) * 1000, (p3 - p2) * 1000, (p5 - p4) * 1000, (p5 - p0) * 1000};

  // Отложенные действия (после сведения кадра: системные диалоги, открытие файлов).
  Impl& fs = *d_;
  if (!fs.later.empty()) {
    auto later = std::move(fs.later);
    fs.later.clear();
    for (auto& fn : later) {
      try {
        fn(*this);
      } catch (const std::exception& e) {
        error(e);
      }
    }
    platform::invalidate();
  }
  if (d.prefsDirty && frames_ % 120 == 0) detail::savePrefs(*this);
}

void App::buildFrame() {
  Impl& d = *d_;
  if (ui.sel && !detail::selectionExists(world(), ui.sel)) ui.sel = Selection{};
  if (!ui.sel) ui.page = false;
  if (ui.hover && !detail::selectionExists(world(), ui.hover)) ui.hover = Selection{};
  if (!ui.editor.empty() && (!findEditor(ui.editor) || !detail::role::editorAllowed(*this, ui.editor))) ui.editor.clear();
  if (!ui.directory.empty() && !findSection(ui.directory)) ui.directory.clear();
  d.mapHovered = false;

  if (ui.screen == Screen::Start) {
    detail::drawStartScreen(*this);
  } else {
    if (mapShown()) detail::mapInput(*this);
    detail::drawEditorScreen(*this);
    // Со страницы на карту: показать выделение, когда раскладка карты (панели справа) уже известна.
    if (d.focusAfter > 0 && --d.focusAfter == 0 && mapShown()) focusSelection();
  }
  // Диалоги
  Impl& fs = *d_;
  fs.drawingDialogs = true;
  try {
    detail::drawDialogs(*this);
  } catch (...) {
    fs.drawingDialogs = false;
    throw;
  }
  fs.drawingDialogs = false;
  if (fs.closeAllDialogs) {
    dialogs_.clear();
    fs.closeAllDialogs = false;
  }
  auto pending = std::move(fs.pendingDialogs);
  fs.pendingDialogs.clear();
  for (auto& p : pending) openDialog(std::move(p));

  detail::globalKeys(*this);
  if (mapShown()) detail::mapWheel(*this);
  if (ui.screen == Screen::Editor) detail::autosaveTick(*this);
}

}  // namespace rg::app
