// Regnum — «Проверить мир» (палитра команд и меню мира, администратор): нормализация копии текущего мира без
// перезагрузки — окно с найденными исправлениями и ошибками геометрии; «Исправить» — все исправления одной транзакцией
// (отмена — Ctrl+Z).
#include "app/app_internal.h"
#include "geo/topo.h"

namespace rg::app {

using namespace detail;

namespace {

struct Found {
  io::Warnings fixes;                 // исправления нормализации
  std::vector<std::string> geometry;  // ошибки геометрии (нормализация их не исправляет)
};

Found inspect(const World& w) {
  Found f;
  World copy = w;
  io::normalize(copy, f.fixes);
  for (const geo::Issue& i : geo::validate(w)) f.geometry.push_back("Геометрия: " + i.msg + strf(" (%.0f, %.0f)", i.at.x, i.at.y));
  return f;
}

struct CheckDialog : Dialog {
  Found found;
  std::vector<std::string> lines;
  const char* id() const override { return "world.check"; }
  Style style(App&) override {
    const bool clean = found.fixes.empty() && found.geometry.empty();
    return {"Проверка мира", clean ? "check-circle" : "warning", clean ? ui::Tone::Success : ui::Tone::Warning, lines.empty() ? 440.f : 600.f};
  }
  bool draw(App& a) override {
    std::vector<std::string> parts;
    if (!found.fixes.empty()) parts.push_back("Исправлений — " + std::to_string(found.fixes.size()));
    if (!found.geometry.empty())
      parts.push_back(std::string(parts.empty() ? "Ошибок" : "ошибок") + " геометрии — " + std::to_string(found.geometry.size()));
    std::string summary = parts.empty() ? std::string("Исправлений нет") : join(parts, " · ");
    ui::label(summary, {.font = ui::Font::Strong});
    a.markUi("check.summary");
    if (!lines.empty()) {
      float h = std::min(300.f, float(lines.size()) * 22 + 16);
      ui::Card c({.pad = 8});
      ui::Scroll sc("lines", h);
      for (size_t i = 0; i < lines.size(); i++) {
        ui::IdScope s{int(i)};
        ui::label(lines[i], {.font = ui::Font::Small, .ink = ui::Ink::Dim, .wrap = true});
      }
    }
    ui::ModalFooter f;
    if (!lines.empty() && ui::button("Скопировать", {.icon = "copy"})) {
      platform::setClipboardText(join(lines, "\n"));
      a.toast("Скопировано", ToastKind::Success, "copy");
    }
    if (found.fixes.empty()) {
      if (ui::button("Понятно", {.variant = ui::Variant::Primary, .isDefault = true})) return false;
      a.markUi("dialog.ok");
      return true;
    }
    if (ui::button("Закрыть")) return false;
    a.markUi("dialog.cancel");
    const bool can = a.canEdit();
    if (ui::button("Исправить", {.variant = ui::Variant::Primary, .icon = "check", .disabled = !can, .isDefault = can,
                                 .tooltip = can ? std::string() : std::string(a.readOnly() ? "Открыт прошлый ход" : "Только администратору")})) {
      // Мир за время окна не меняется (окно модальное), но исправления — по текущему миру на момент нажатия.
      later(a, [](App& x) {
        World fixed = x.store.world();
        io::Warnings warns;
        io::normalize(fixed, warns);
        if (warns.empty()) {
          x.toast("Исправлений нет", ToastKind::Info, "check");
          return;
        }
        if (x.act("Исправить мир", [&](Tx& tx) { tx.replaceWorld(fixed); }))
          x.toast("Исправлено: " + std::to_string(warns.size()), ToastKind::Success, "check-circle");
      });
      return false;
    }
    a.markUi("check.fix");
    return true;
  }
};

void openCheck(App& a) {
  auto dlg = std::make_unique<CheckDialog>();
  dlg->found = inspect(a.store.world());
  for (size_t i = 0; i < dlg->found.fixes.size() && i < 300; i++) dlg->lines.push_back(dlg->found.fixes[i].text());
  if (dlg->found.fixes.size() > 300) dlg->lines.push_back("… и ещё " + std::to_string(dlg->found.fixes.size() - 300));
  for (size_t i = 0; i < dlg->found.geometry.size() && i < 100; i++) dlg->lines.push_back(dlg->found.geometry[i]);
  if (dlg->found.geometry.size() > 100) dlg->lines.push_back("… и ещё " + std::to_string(dlg->found.geometry.size() - 100));
  a.openDialog(std::move(dlg));
}

// Только администратору: исправления показывают подробности всех фракций (у игрока — туман войны).
CommandReg cCheck({"file.check", "Проверить мир", "check-circle", nullptr, openCheck,
                   [](App& a) { return a.ui.screen == Screen::Editor && a.admin(); }, false, "Файл"});

}  // namespace

}  // namespace rg::app
