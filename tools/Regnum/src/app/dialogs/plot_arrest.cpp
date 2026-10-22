// Regnum — окно «Арестовать заговорщиков» контрразведки (ТЗ «Доработки №2», п.8): что будет на раскрытом этапе
// обнаруженного заговора (plotui::arrestLines: приверженность, жители в тюрьму, «Аресты заговорщиков» или «Раскрытый
// заговор», герои под арест, заговор снова скроется), затем rules::arrestPlotters — игровое действие государства-цели;
// итог — уведомлением. Заказчик заговора не показывается.
#include "app/panels/plots_common.h"

namespace rg::app {
namespace {

using namespace plotui;

struct ArrestDlg final : Dialog {
  Id state = 0, plot = 0;
  std::string name;

  const char* id() const override { return "plot.arrest"; }
  Style style(App&) override { return {"Арестовать заговорщиков", "shackles", ui::Tone::Danger, 500}; }

  bool draw(App& a) override {
    const World& w = w::frameWorld(a);
    const Plot* p = w.plot(plot);
    if (!p || p->target != state || !p->found || p->stage < 1) return false;
    const bool play = a.canPlay(state);
    {
      ui::Row r({ui::fr(1), ui::px(80)}, 24, 8);
      ui::label(name, {.font = ui::Font::Strong, .icon = "eye-off"});
      ui::label("этап " + std::to_string(p->stage) + " из " + std::to_string(schema::kPlotStages),
                {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Right, .tooltip = std::string("Раскрыто: ") + stageName(p->stage)});
    }
    {
      ui::Card card({.pad = 10});
      const auto lines = arrestLines(w, *p);
      for (size_t i = 0; i < lines.size(); i++) {
        ui::IdScope s{int(i)};
        ui::Row r({ui::px(18), ui::fr(1)}, ui::kAuto, 8);
        ui::icon(lines[i].first, ui::Ink::Dim, 16);
        ui::label(lines[i].second, {.wrap = true});
      }
      a.markUi("plot.arrest.lines");
    }
    ui::ModalFooter footer;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    if (ui::button("Арестовать", {.variant = ui::Variant::Danger, .icon = "shackles", .disabled = !play, .isDefault = true})) {
      rules::ArrestResult res;
      const Id sid = state, pl = plot;
      if (a.play("Аресты заговорщиков", [&](Tx& tx) { res = rules::arrestPlotters(tx, sid, pl); })) {
        std::string t = "Арестовано жителей: " + fmtInt(res.people);
        if (!res.heroes.empty()) {
          std::vector<std::string> names;
          for (Id h : res.heroes) names.push_back(heroName(a.world(), h));
          t += "; под арестом: " + join(names, ", ");
        }
        if (res.removed) t += "; заговор уничтожен";
        a.toast(t, ToastKind::Success, "shackles");
        return false;
      }
    }
    a.markUi("plot.arrest.ok");
    return true;
  }
};

}  // namespace

void plotui::openArrest(App& a, Id state, Id plot, const std::string& name) {
  auto d = std::make_unique<ArrestDlg>();
  d->state = state;
  d->plot = plot;
  d->name = name;
  a.openDialog(std::move(d));
}

}  // namespace rg::app
