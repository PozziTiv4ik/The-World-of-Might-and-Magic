// Regnum — окно «Освободить заключённых» «Тюрем государства» (ТЗ «Доработки №2», п.8; DialogReg «plot.prison», arg —
// государство): расы в тюрьме, сколько каждой отпустить; освобождённые возвращаются в провинции государства к своей
// расе (rules::releasePrisoners). Игровое действие государства (App::canPlay, App::play).
#include "app/panels/plots_common.h"

namespace rg::app {
namespace {

using namespace plotui;

struct ReleaseDlg final : Dialog {
  Id state = 0;
  std::map<Id, i64> n;   // раса → сколько освободить

  const char* id() const override { return "plot.prison"; }
  Style style(App&) override { return {"Освободить заключённых", "unlock", ui::Tone::Accent, 480}; }

  bool draw(App& a) override {
    const World& w = w::frameWorld(a);
    const Faction* f = w.faction(state);
    if (!f || !f->isState()) return false;
    const bool play = a.canPlay(state);
    std::vector<RacePop> races;
    for (const RacePop& r : f->prison)
      if (r.pop > 0) races.push_back(r);
    if (races.empty()) return false;
    std::sort(races.begin(), races.end(), [&](const RacePop& x, const RacePop& y) { return x.pop > y.pop; });
    i64 total = 0;
    for (const RacePop& r : races) {
      i64& v = n[r.race];
      v = std::clamp<i64>(v, 0, r.pop);
      total += v;
    }
    for (const RacePop& r : races) {
      ui::IdScope s{i64(r.race)};
      ui::Row row({ui::fr(1, 100), ui::px(84), ui::px(130), ui::px(26)}, 32, 8);
      ui::label(raceName(w, r.race), {.font = ui::Font::Strong, .icon = "population"});
      ui::label(fmtInt(r.pop), {.ink = ui::Ink::Dim, .align = ui::Align::Right, .tooltip = "В тюрьме"});
      i64 v = n[r.race];
      if (ui::numberField("n", v, {.min = 0, .max = double(r.pop), .step = 100, .disabled = !play, .tooltip = "Освободить"}))
        n[r.race] = std::clamp<i64>(v, 0, r.pop);
      a.markUi("prison.race." + std::to_string(r.race));
      const bool all = n[r.race] == r.pop;
      if (ui::iconButton(all ? "close" : "arrow-up", all ? "Никого" : "Всех", {.size = ui::Size::Small, .disabled = !play})) n[r.race] = all ? 0 : r.pop;
      a.markUi("prison.all." + std::to_string(r.race));
    }
    ui::ModalFooter footer;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    const std::string label = total > 0 ? "Освободить · " + fmtInt(total) : std::string("Освободить");
    if (ui::button(label, {.variant = ui::Variant::Primary, .icon = "unlock", .disabled = !play || total <= 0, .isDefault = true})) {
      std::map<Id, i64> give;
      for (const RacePop& r : races)
        if (n[r.race] > 0) give[r.race] = n[r.race];
      const Id sid = state;
      if (a.play("Освободить заключённых", [&](Tx& tx) { rules::releasePrisoners(tx, sid, give); })) {
        a.toast("Освобождено из тюрьмы: " + fmtInt(total), ToastKind::Success, "unlock");
        return false;
      }
    }
    a.markUi("prison.ok");
    return true;
  }
};

DialogReg reg({"plot.prison", [](App&, Id state) -> std::unique_ptr<Dialog> {
                 auto d = std::make_unique<ReleaseDlg>();
                 d->state = state;
                 return d;
               }});

}  // namespace

void plotui::openRelease(App& a, Id state) { a.openDialog("plot.prison", state); }

}  // namespace rg::app
