// Regnum — окно «Снабдить вооружением» провинции заговора (ТЗ «Доработки №2», п.3): ресурсы вооружения
// (rules::plotSupplyResources — порох, стали, железо, обсидианы, лунное серебро, механические детали, запчасти
// механизмов) — запас заказчика, уже отдано заговорщикам провинции и сколько отдать; ниже — отряды восстания после
// снабжения. «Снабдить» — одной транзакцией (rules::supplyPlot); внешний заговор — игровое действие заказчика,
// внутренний — администратор (ресурсы без списания).
#include "app/panels/plots_common.h"

namespace rg::app {
namespace {

using namespace plotui;

struct SupplyPlot final : Dialog {
  Id plot = 0, province = 0;
  std::map<Id, double> add;   // ресурс → сколько отдать

  const char* id() const override { return "plot.supply"; }
  Style style(App& a) override { return {"Снабдить вооружением · " + provName(a.world(), province), "swords", ui::Tone::Accent, 620}; }

  bool draw(App& a) override {
    const World& w = w::frameWorld(a);
    const Plot* p = w.plot(plot);
    const PlotProvince* pp = p ? p->province(province) : nullptr;
    if (!pp) return false;
    const bool manage = canManage(a, *p);
    const Faction* owner = p->external() ? w.faction(p->owner) : nullptr;
    struct Line {
      Id res;
      double stock, given;
    };
    std::vector<Line> lines;
    for (Id r : rules::plotSupplyResources(w)) {
      auto it = pp->supplies.find(r);
      const double given = it == pp->supplies.end() ? 0 : it->second;
      const double stock = owner ? std::max(0.0, owner->stock(r)) : 0;
      lines.push_back(Line{r, stock, given});
    }
    // Что есть у заказчика или уже отдано — выше; остальное — приглушено ниже.
    std::stable_sort(lines.begin(), lines.end(), [&](const Line& x, const Line& y) {
      const bool hx = !owner || x.stock > 0 || x.given > 0, hy = !owner || y.stock > 0 || y.given > 0;
      return hx && !hy;
    });
    for (auto it = add.begin(); it != add.end();) {
      const bool keep = std::any_of(lines.begin(), lines.end(), [&](const Line& l) { return l.res == it->first; });
      it = keep ? std::next(it) : add.erase(it);
    }
    double total = 0;
    PlotProvince after = *pp;
    for (auto& [r, v] : add)
      if (v > 0) {
        total += v;
        after.supplies[r] += v;
      }
    {
      ui::Row r({ui::fr(1), ui::fr(1)}, ui::kAuto, 8);
      ui::label(rules::plotTitle(w, *p), {.font = ui::Font::Strong, .icon = "eye-off"});
      ui::label("Приверженность заговору " + fmtPct(pp->commitment), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Right});
    }
    if (lines.empty()) {
      ui::emptyState("swords", "Ресурсов для вооружения нет.");
      a.markUi("plot.supply.empty");
    } else {
      const float maxH = std::min(360.f, std::max(180.f, ui::viewport().h - 420));
      const float h = std::min(maxH, 34 + 40 * float(lines.size()));
      ui::Column cols[] = {{"Ресурс", nullptr, ui::fr(1.4f, 150)},
                           {"Есть", nullptr, ui::px(84), ui::Align::Right, false, "Запас заказчика"},
                           {"Отдано", nullptr, ui::px(76), ui::Align::Right, false, "Уже у заговорщиков провинции"},
                           {"Отдать", nullptr, ui::px(150), ui::Align::Left, false, "Сколько отдать заговорщикам"}};
      ui::Table t("res", cols, int(lines.size()), {.rowHeight = 40, .height = h, .selectable = false});
      for (int i : t) {
        const Line& l = lines[size_t(i)];
        ui::IdScope s{i64(l.res)};
        t.cell();
        const bool none = owner && l.stock <= 0 && l.given <= 0;
        {
          ui::HStack hs(24, ui::Align::Left, 8);
          ui::iconColored(w::resourceIcon(w, l.res), w::legible(w::resourceColor(w, l.res)).alpha(none ? 0.45f : 1.f), 16);
          ui::label(w::resourceName(w, l.res), {.ink = none ? ui::Ink::Muted : ui::Ink::Normal});
        }
        t.text(owner ? fmtNum(l.stock, 3) : std::string("—"), none ? ui::Ink::Muted : ui::Ink::Dim);
        t.text(l.given > 0 ? fmtNum(l.given, 3) : std::string("—"), l.given > 0 ? ui::Ink::Normal : ui::Ink::Muted);
        t.cell();
        {
          ui::Row row({ui::fr(1), ui::px(26)}, 30, 4);
          double v = add.count(l.res) ? add[l.res] : 0;
          const double mx = owner ? l.stock : 1e9;
          if (ui::numberField("n", v, {.min = 0, .max = mx, .step = 10, .digits = 3, .disabled = !manage || (owner && l.stock <= 0)}))
            add[l.res] = std::clamp(v, 0.0, mx);
          a.markUi("plot.supply.res." + std::to_string(l.res));
          if (owner) {
            const bool all = v >= l.stock - 1e-9 && l.stock > 0;
            if (ui::iconButton(all ? "close" : "arrow-up", all ? "Ничего" : "Весь запас", {.size = ui::Size::Small, .disabled = !manage || l.stock <= 0}))
              add[l.res] = all ? 0 : l.stock;
            a.markUi("plot.supply.all." + std::to_string(l.res));
          } else {
            ui::next(26, 26);
          }
        }
      }
    }
    // Отряды восстания после снабжения.
    {
      const i64 rebels = rules::plotRebels(w, after);
      const std::vector<rules::PlotSquad> squads = rules::plotSquads(w, after, rebels);
      ui::caption(total > 0 ? "Восстание после снабжения" : "Восстание");
      if (squads.empty()) {
        ui::label("Отрядов нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      } else {
        ui::ChipFlow flow;
        for (size_t i = 0; i < squads.size(); i++) {
          ui::IdScope s{int(i)};
          ui::flowChip(squads[i].name + " · " + fmtInt(squads[i].count), {.icon = squadIcon(squads[i])});
        }
      }
      a.markUi("plot.supply.squads");
    }
    ui::ModalFooter footer;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    if (ui::button("Снабдить", {.variant = ui::Variant::Primary, .icon = "swords", .disabled = !manage || total <= 0, .isDefault = true})) {
      std::map<Id, double> give;
      for (auto& [r, v] : add)
        if (v > 0) give[r] = v;
      const Id pl = plot, pr = province;
      if (plotAct(a, *p, "Снабдить вооружением", [&](Tx& tx) { rules::supplyPlot(tx, pl, pr, give); })) return false;
    }
    a.markUi("plot.supply.ok");
    return true;
  }
};

}  // namespace

void plotui::openSupply(App& a, Id plot, Id province) {
  auto d = std::make_unique<SupplyPlot>();
  d->plot = plot;
  d->province = province;
  a.openDialog(std::move(d));
}

}  // namespace rg::app
