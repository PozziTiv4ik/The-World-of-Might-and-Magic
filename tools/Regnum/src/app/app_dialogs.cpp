// Regnum — окна и уведомления приложения: стек модальных окон, окна по реестру, уведомления, ошибки, подтверждения,
// выбор, запрос строки, сообщения.
#include "app/app_internal.h"

namespace rg::app {

void App::openDialog(std::unique_ptr<Dialog> dlg) {
  if (!dlg) return;
  Impl& f = *d_;
  if (f.drawingDialogs) {
    f.pendingDialogs.push_back(std::move(dlg));
  } else {
    std::string_view id = dlg->id();
    dialogs_.erase(std::remove_if(dialogs_.begin(), dialogs_.end(), [&](auto& x) { return id == x->id(); }), dialogs_.end());
    dialogs_.push_back(std::move(dlg));
  }
  requestRedraw();
}

bool App::openDialog(std::string_view id, Id arg) {
  if (const DialogDef* def = findDialog(id)) {
    try {
      openDialog(def->make(*this, arg));
    } catch (const std::exception& e) {
      error(e);
    }
    return true;
  }
  if (id == "settings") {
    openDialog(detail::settingsDialog());
    return true;
  }
  if (id == "palette") {
    openDialog(detail::paletteDialog());
    return true;
  }
  if (id == "help") {
    openDialog(detail::helpDialog());
    return true;
  }
  return false;
}

void App::closeDialogs() {
  Impl& f = *d_;
  if (f.drawingDialogs) f.closeAllDialogs = true;
  else dialogs_.clear();
  f.pendingDialogs.clear();
  requestRedraw();
}

bool App::hasDialog(std::string_view id) const {
  for (auto& x : dialogs_)
    if (id == x->id()) return true;
  for (auto& x : d_->pendingDialogs)
    if (id == x->id()) return true;
  return false;
}

void App::toast(std::string text, ToastKind kind, std::string icon, std::string actionLabel, std::function<void(App&)> action) {
  // Над модальным окном интерфейсные панели не видны: уведомление без действия — в слой ui.
  if (ui::anyModalOpen() || hasDialog()) {
    if (actionLabel.empty()) {
      ui::toast(text, detail::toastTone(kind), icon.empty() ? nullptr : icon.c_str(), kind == ToastKind::Danger ? 8 : 4);
      requestRedraw();
      return;
    }
  }
  Toast t;
  t.id = ++toastSeq_;
  t.kind = kind;
  t.text = std::move(text);
  t.icon = std::move(icon);
  t.start = time_;
  t.duration = kind == ToastKind::Danger ? 8 : !actionLabel.empty() ? 7 : kind == ToastKind::Warning ? 5 : 3.5;
  t.actionLabel = std::move(actionLabel);
  t.action = std::move(action);
  // Не больше пяти; одинаковые подряд не повторяются.
  if (!toasts_.empty() && toasts_.back().text == t.text && toasts_.back().closeAt < 0) {
    toasts_.back().start = time_;
    toasts_.back().paused = 0;
    return;
  }
  toasts_.push_back(std::move(t));
  int live = 0;
  for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it)
    if (it->closeAt < 0 && ++live > 5) it->closeAt = time_;
  requestRedraw();
}

void App::error(const std::exception& e) {
  bool user = dynamic_cast<const UserError*>(&e) != nullptr;
  std::string msg = e.what();
  Impl& f = *d_;
  if (msg == f.lastError && time_ - f.lastErrorAt < 2) return;   // повторяющаяся ошибка каждого кадра
  f.lastError = msg;
  f.lastErrorAt = time_;
  if (user) {
    toast(msg, ToastKind::Warning, "warning");
  } else {
    logError("Ошибка: %s", msg.c_str());
    toast("Что-то пошло не так: " + msg + ". Мир не изменён.", ToastKind::Danger, "error");
  }
}

void App::confirm(std::string title, std::string text, std::string okLabel, bool danger, std::function<void(App&)> onYes) {
  openDialog(detail::confirmDialog(std::move(title), std::move(text), std::move(okLabel), danger, std::move(onYes)));
}

void App::confirmAct(std::string title, std::string text, std::string okLabel, std::string actLabel, std::function<void(Tx&)> fn,
                     std::function<void(App&)> then) {
  confirm(std::move(title), std::move(text) + " Действие можно отменить Ctrl+Z.", std::move(okLabel), true,
          [label = std::move(actLabel), fn = std::move(fn), then = std::move(then)](App& x) {
            if (x.act(label, fn) && then) then(x);
          });
}

void App::confirmPlay(std::string title, std::string text, std::string okLabel, std::string actLabel, std::function<void(Tx&)> fn,
                      std::function<void(App&)> then) {
  confirm(std::move(title), std::move(text) + " Действие можно отменить Ctrl+Z.", std::move(okLabel), true,
          [label = std::move(actLabel), fn = std::move(fn), then = std::move(then)](App& x) {
            if (x.play(label, fn) && then) then(x);
          });
}

void App::choose(std::string title, std::string text, std::vector<std::string> buttons, std::function<void(App&, int)> onPick,
                 const char* icon, bool firstSubtle) {
  openDialog(detail::choiceDialog(std::move(title), std::move(text), std::move(buttons), std::move(onPick), icon, firstSubtle));
}

void App::prompt(std::string title, std::string label, std::string initial, std::function<void(App&, const std::string&)> onOk) {
  openDialog(detail::promptDialog(std::move(title), std::move(label), std::move(initial), std::move(onOk)));
}

void App::message(std::string title, std::string text, std::vector<std::string> lines, ToastKind kind) {
  openDialog(detail::messageDialog(std::move(title), std::move(text), std::move(lines), kind));
}

}  // namespace rg::app
