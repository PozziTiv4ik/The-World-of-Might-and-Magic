// Regnum — окно «Осуществить заговор» (ТЗ «Доработки №2», п.7): подтверждение — что произойдёт (государство
// «Заговор <лидер>», война с целью и союз с заказчиком, восстание провинций с отрядами, герои, уходящие к заговору, и
// части войск, которые они уведут), затем итог — новое государство, восставшие и войска с «Показать на карте».
// Осуществление — rules::executePlot (внешний заговор — игровое действие заказчика, внутренний — администратор).
#include "app/panels/plots_common.h"

namespace rg::app {
namespace {

using namespace plotui;

// Где герой: войско, флот или гарнизон (их часть уйдёт к заговору); пусто — свободен.
std::string heroPlace(const World& w, Id hero) {
  std::string out;
  w.armies.each([&](const Army& a) {
    if (!out.empty()) return;
    for (const ArmyGroup& g : a.groups)
      if (std::find(g.heroes.begin(), g.heroes.end(), hero) != g.heroes.end())
        out = std::string(a.isFleet() ? "Флот" : "Войско") + (a.name.empty() ? std::string() : " «" + a.name + "»");
  });
  if (!out.empty()) return out;
  w.provinces.each([&](const Province& p) {
    if (out.empty() && std::find(p.garrisonHeroes.begin(), p.garrisonHeroes.end(), hero) != p.garrisonHeroes.end())
      out = "Гарнизон «" + w::orName(p.name) + "»";
  });
  return out;
}

i64 unitsIn(const Army& a) {
  i64 n = 0;
  for (const ArmyGroup& g : a.groups)
    for (const ArmyUnit& u : g.units) n += u.count;
  return n;
}

struct ExecDlg final : Dialog {
  Id plot = 0;
  bool done = false;
  rules::PlotResult res;
  std::string title;

  const char* id() const override { return "plot.execute"; }
  Style style(App&) override {
    return {done ? "Заговор осуществлён" : "Осуществить заговор", done ? "check-circle" : "flame", done ? ui::Tone::Success : ui::Tone::Danger, 620};
  }

  static void showArmy(App& a, Id army) {
    detail::later(a, [army](App& x) {
      x.toMap();
      x.select(SelType::Army, army, true);
    });
  }

  bool confirm(App& a, const World& w) {
    const Plot* p = w.plot(plot);
    if (!p) return false;
    title = rules::plotTitle(w, *p);
    const bool manage = canManage(a, *p);
    const std::string why = rules::executeProblem(w, plot);
    const Character* leader = w.character(p->leader);
    // Государство заговора: лидер, война с целью, союз с заказчиком.
    {
      ui::Row r({ui::px(64), ui::fr(1)}, ui::kAuto, 12);
      if (leader) w::heroAvatar(*leader, 56, true, "Лидер заговора", w::factionColor(w, leader->faction));
      else ui::next(56, 56);
      ui::Group g(0, 4);
      ui::label("«Заговор " + (leader ? heroName(w, p->leader) : std::string("—")) + "»", {.font = ui::Font::Title, .tooltip = "Новое государство"});
      a.markUi("plot.execute.state");
      ui::HStack hs(24, ui::Align::Left, 6);
      tagTip("Война", ui::Tone::Danger, "war", "Война с государством-целью");
      w::factionChip(p->target);
      if (p->external() && w.faction(p->owner)) {
        tagTip("Союз", ui::Tone::Success, "alliance", "Союз с заказчиком");
        w::factionChip(p->owner);
      }
    }
    // Восстание провинций.
    i64 rebels = 0;
    {
      ui::caption("Восстание");
      int shown = 0;
      for (const PlotProvince& pp : p->provinces) {
        const Province* pr = w.province(pp.province);
        if (!pr || pr->sea || pr->owner != p->target) continue;
        const i64 n = rules::plotRebels(w, pp);
        const std::vector<rules::PlotSquad> squads = rules::plotSquads(w, pp, n);
        for (const rules::PlotSquad& s : squads)
          if (s.race == schema::kRaceLiving) rebels += s.count;
        ui::IdScope s{i64(pp.province)};
        ui::Row r({ui::fr(1, 120), ui::px(84)}, ui::kAuto, 8);
        {
          ui::Group g(0, 4);
          ui::label(provName(w, pp.province) + " · " + fmtPct(pp.commitment), {.font = ui::Font::Strong, .icon = "province"});
          if (squads.empty()) {
            ui::label("Без восстания", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
          } else {
            ui::ChipFlow flow;
            for (size_t i = 0; i < squads.size(); i++) {
              ui::IdScope k{int(i)};
              ui::flowChip(squads[i].name + " · " + fmtInt(squads[i].count), {.icon = squadIcon(squads[i])});
            }
          }
        }
        ui::label(fmtInt(n), {.font = ui::Font::Strong, .ink = n > 0 ? ui::Ink::Warning : ui::Ink::Muted, .align = ui::Align::Right,
                              .tooltip = "Восставших жителей"});
        a.markUi("plot.execute.prov." + std::to_string(pp.province));
        shown++;
      }
      if (!shown) ui::label("Провинций нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    }
    // Герои заговора: уходят к заговору; в войске, флоте или гарнизоне — уводят часть отрядов.
    {
      ui::caption("Герои");
      std::vector<Id> heroes;
      for (Id h : p->heroes)
        if (const Character* c = w.character(h); c && c->faction == p->target && !rules::characterHas(w, h, schema::mod::Dead)) heroes.push_back(h);
      if (heroes.empty()) ui::label("Героев нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      for (Id h : heroes) {
        const Character& c = *w.character(h);
        ui::IdScope s{i64(h)};
        ui::Row r({ui::px(30), ui::fr(1)}, 30, 8);
        w::heroAvatar(c, 26, h == p->leader, {}, w::factionColor(w, c.faction));
        const std::string place = heroPlace(w, h);
        ui::HStack hs(24, ui::Align::Left, 6);
        ui::label(heroName(w, h), {.font = ui::Font::Strong});
        if (c.hero) ui::icon("star-filled", ui::Ink::Accent, 14, "Значимый герой фракции");
        if (h == p->leader) ui::label("лидер", {.font = ui::Font::Small, .ink = ui::Ink::Accent});
        if (!place.empty()) ui::label(place + " — часть отрядов уйдёт с ним", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "army"});
      }
    }
    if (!why.empty()) ui::label(why, {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
    ui::ModalFooter footer;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    const std::string lbl = rebels > 0 ? "Осуществить · " + fmtInt(rebels) : std::string("Осуществить");
    if (ui::button(lbl, {.variant = ui::Variant::Danger, .icon = "flame", .disabled = !manage || !why.empty(), .isDefault = true,
                         .tooltip = rebels > 0 ? std::string_view("Восстанут в провинциях заговора") : std::string_view()})) {
      rules::PlotResult r;
      const Id pl = plot;
      if (plotAct(a, *p, "Осуществить заговор", [&](Tx& tx) { r = rules::executePlot(tx, pl); })) {
        res = r;
        done = true;
      }
    }
    a.markUi("plot.execute.ok");
    return true;
  }

  bool result(App& a, const World& w) {
    const Faction* s = w.faction(res.state);
    if (!s) return false;
    const Id sid = res.state;
    {
      ui::Row r({ui::px(72), ui::fr(1)}, ui::kAuto, 12);
      ui::flag(s->flag, 72, 48, 4, "Государство — открыть");
      if (ui::lastItem().hovered) ui::setCursor(platform::Cursor::Hand);
      if (ui::lastItem().clicked) {
        detail::later(a, [sid](App& x) { x.select(SelType::Faction, sid); });
        return false;
      }
      ui::Group g(0, 2);
      ui::label(w::orName(s->name), {.font = ui::Font::Title});
      a.markUi("plot.execute.newState");
      ui::label(title, {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "eye-off"});
    }
    {
      ui::Row r({ui::fr(1), ui::fr(1)}, 64, 10);
      ui::stat(fmtInt(res.rebels), "Восставших", {.icon = "rebellion", .tone = ui::Tone::Warning});
      ui::stat(std::to_string(res.armies.size()), plural(i64(res.armies.size()), "Войско", "Войска", "Войск"), {.icon = "army", .tone = ui::Tone::Accent});
    }
    std::vector<const Army*> armies;
    for (Id id : res.armies)
      if (const Army* ar = w.army(id)) armies.push_back(ar);
    if (!armies.empty()) {
      ui::Scroll sc("armies", std::min(240.f, float(armies.size()) * 38.f + 4));
      for (const Army* ar : armies) {
        ui::IdScope k{i64(ar->id)};
        ui::Row r({ui::px(20), ui::fr(1), ui::px(80), ui::px(28)}, 30, 8);
        ui::icon(ar->isFleet() ? "fleet" : "army", ui::Ink::Accent, 16);
        ui::label(ar->name.empty() ? std::string(ar->isFleet() ? "Флот" : "Войско") : ar->name, {.font = ui::Font::Strong});
        ui::label(fmtInt(unitsIn(*ar)), {.font = ui::Font::Small, .ink = ui::Ink::Dim, .align = ui::Align::Right, .tooltip = "Отрядов"});
        if (ui::iconButton("map-pin", "Показать на карте", {.size = ui::Size::Small})) {
          showArmy(a, ar->id);
          return false;
        }
        a.markUi("plot.execute.army." + std::to_string(ar->id));
      }
    }
    ui::ModalFooter footer;
    if (!armies.empty()) {
      if (ui::button("Показать на карте", {.icon = "map-pin"})) {
        showArmy(a, armies.front()->id);
        return false;
      }
      a.markUi("plot.execute.map");
    }
    if (ui::button("Готово", {.variant = ui::Variant::Primary, .icon = "check", .isDefault = true})) return false;
    a.markUi("plot.execute.done");
    return true;
  }

  bool draw(App& a) override {
    const World& w = w::frameWorld(a);
    return done ? result(a, w) : confirm(a, w);
  }
};

}  // namespace

void plotui::openExecute(App& a, Id plot) {
  auto d = std::make_unique<ExecDlg>();
  d->plot = plot;
  a.openDialog(std::move(d));
}

}  // namespace rg::app
