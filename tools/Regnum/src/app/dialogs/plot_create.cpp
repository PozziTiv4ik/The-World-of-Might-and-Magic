// Regnum — окно «Создать заговор» (ТЗ «Доработки №2», п.1; DialogReg «plot.create», arg — государство): вид заговора
// (игроку — только «Против другого государства», внутренний — администратору), государство-цель с поиском (не своё;
// внутренний — против самого государства) и название (пусто — «Заговор против …»). Создание — игровое действие
// государства-заказчика (App::play), внутренний — правка мира (App::act).
#include "app/panels/plots_common.h"

namespace rg::app {
namespace {

using namespace plotui;

struct CreatePlot final : Dialog {
  Id state = 0;
  int kind = 0;   // 0 — против другого государства, 1 — внутренний
  Id target = 0;
  std::string name;

  const char* id() const override { return "plot.create"; }
  Style style(App&) override { return {"Новый заговор", "eye-off", ui::Tone::Accent, 500}; }

  bool draw(App& a) override {
    const World& w = w::frameWorld(a);
    const Faction* f = w.faction(state);
    if (!f || !f->isState()) return false;
    const bool admin = a.admin();
    if (!admin) kind = 0;
    static const ui::Segment kinds[2] = {{"target", schema::plotKind(PlotKind::External).name, "Заказчик — это государство"},
                                         {"eye-off", schema::plotKind(PlotKind::Internal).name, "Против этого государства, без заказчика"}};
    ui::segmented("kind", kind, std::span<const ui::Segment>(kinds, admin ? 2 : 1));
    a.markUi("plot.create.kind");
    if (kind == 1) target = state;
    else if (target == state) target = 0;
    ui::prop("Цель", "crown");
    if (kind == 0) {
      std::vector<const Faction*> states;
      w.factions.each([&](const Faction& x) {
        if (x.isState() && x.id != state) states.push_back(&x);
      });
      std::sort(states.begin(), states.end(), [](const Faction* x, const Faction* y) { return compareRu(x->name, y->name) < 0; });
      w::IdOptions o;
      o.reserve(states.size());
      for (const Faction* x : states) o.add(x->id, w::orName(x->name), nullptr, x->color);
      w::idCombo("target", target, o, {.placeholder = "Государство", .search = 1, .icon = "crown"});
    } else {
      w::factionChip(state);
    }
    a.markUi("plot.create.target");
    const Faction* t = w.faction(target);
    Plot probe;
    probe.kind = kind == 1 ? PlotKind::Internal : PlotKind::External;
    probe.target = target;
    const std::string def = t ? rules::plotTitle(w, probe) : std::string("Заговор");
    ui::prop("Название", "edit");
    ui::textField("name", name, {.placeholder = def, .live = true, .maxLength = 80});
    a.markUi("plot.create.name");
    if (t) {
      ui::spacer(2);
      ui::Row r({ui::px(66), ui::fr(1)}, ui::kAuto, 12);
      ui::flag(t->flag, 66, 44, 4);
      ui::Group g(0, 2);
      ui::label(w::orName(t->name), {.font = ui::Font::Title});
      ui::label(rulerLine(w, *t), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    }
    ui::ModalFooter footer;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    const bool can = kind == 1 ? a.canEdit() : a.canPlay(state);
    if (ui::button("Создать", {.variant = ui::Variant::Primary, .icon = "plus", .disabled = !t || !can, .isDefault = true})) {
      Id id = 0;
      const PlotKind k = probe.kind;
      const Id s = state, tg = target;
      const std::string n = name;
      auto fn = [&](Tx& tx) { id = rules::createPlot(tx, k, k == PlotKind::External ? s : 0, tg, n); };
      if (k == PlotKind::External ? a.play("Создать заговор", fn) : a.act("Создать заговор", fn)) {
        selected(state) = id;
        return false;
      }
    }
    a.markUi("plot.create.ok");
    return true;
  }

  // Правитель государства-цели — подпись под названием.
  static std::string rulerLine(const World& w, const Faction& t) {
    const Character* r = w.character(t.ruler);
    if (!r) return "Государство";
    return (t.rulerTitle.empty() ? std::string("Правитель") : t.rulerTitle) + " · " + w::orName(r->name, "Без имени");
  }
};

DialogReg reg({"plot.create", [](App&, Id state) -> std::unique_ptr<Dialog> {
                 auto d = std::make_unique<CreatePlot>();
                 d->state = state;
                 return d;
               }});

}  // namespace

void plotui::openCreate(App& a, Id state) { a.openDialog("plot.create", state); }

}  // namespace rg::app
