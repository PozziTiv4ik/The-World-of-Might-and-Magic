// Regnum — окно «Заказать убийство» угрозы заговору (ТЗ «Доработки №2», п.5): золото на операцию ползунком
// (rules::hitRange: 100–5000 с шагом 100, 1 % за 100; правитель, советник или значимый герой — 500–25000 с шагом 500,
// 1 % за 500) и вероятность успеха по слагаемым (rules::hitChance: база, «Штаб Разведки» заказчика, защита
// государства героя, модификаторы героя, итог). Исход броска — только в конце хода. Повторный заказ заменяет прежний
// (доплата или возврат разницы). Внешний заговор — игровое действие заказчика, внутренний — администратор.
#include "app/panels/plots_common.h"

namespace rg::app {
namespace {

using namespace plotui;

struct HitDlg final : Dialog {
  Id plot = 0, character = 0;
  double gold = -1;   // < 0 — прежний заказ или минимум

  const char* id() const override { return "plot.hit"; }
  Style style(App& a) override {
    const Plot* p = a.world().plot(plot);
    return {p && p->hit(character) ? "Изменить заказ убийства" : "Заказать убийство", "crosshair", ui::Tone::Danger, 500};
  }

  // Строка слагаемого вероятности: значок, подпись, значение со знаком.
  static void part(const char* icon, std::string_view label, double v, std::string_view tip) {
    ui::IdScope s(label);
    ui::Row r({ui::px(18), ui::fr(1), ui::px(70)}, 22, 8);
    ui::icon(icon, ui::Ink::Dim, 16);
    ui::label(label, {.font = ui::Font::Small, .ink = ui::Ink::Dim, .tooltip = tip});
    ui::label(fmtPct(v, 0, true), {.font = ui::Font::Strong, .ink = v > 0 ? ui::Ink::Success : v < 0 ? ui::Ink::Danger : ui::Ink::Muted, .align = ui::Align::Right});
  }

  bool draw(App& a) override {
    const World& w = w::frameWorld(a);
    const Plot* p = w.plot(plot);
    const Character* c = w.character(character);
    if (!p || !c || std::find(p->threats.begin(), p->threats.end(), character) == p->threats.end()) return false;
    const bool manage = canManage(a, *p);
    const rules::HitRange range = rules::hitRange(w, character);
    const PlotHit* was = p->hit(character);
    if (gold < 0) gold = was ? was->gold : range.min;
    gold = std::clamp(gold, range.min, range.max);
    // Герой: портрет, имя, государство, значимая цель.
    {
      ui::Row r({ui::px(52), ui::fr(1)}, ui::kAuto, 12);
      w::heroAvatar(*c, 48, false, {}, w::factionColor(w, c->faction));
      ui::Group g(0, 2);
      ui::label(heroName(w, character), {.font = ui::Font::Title});
      ui::HStack hs(24, ui::Align::Left, 6);
      w::factionChip(c->faction);
      const std::string vip = vipReason(w, character);
      if (!vip.empty()) {
        tagTip("Значимая цель", ui::Tone::Warning, "star-filled", vip + ": 1 % за " + fmtGold(range.perPct));
        a.markUi("plot.hit.vip");
      }
    }
    ui::spacer(4);
    {
      ui::Row r({ui::px(110), ui::fr(1)}, 30, 8);
      ui::label("Золото", {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "coins", .tooltip = "1 % за " + fmtGold(range.perPct)});
      ui::slider("gold", gold, range.min, range.max, {.step = range.perPct, .unit = "тыс.", .disabled = !manage});
      a.markUi("plot.hit.gold");
    }
    const rules::HitChance hc = rules::hitChance(w, plot, character, gold);
    {
      ui::Card card({.pad = 10});
      part("coins", "База", hc.base, "1 % за " + fmtGold(range.perPct));
      if (p->external()) part("eye", "Штаб Разведки", hc.intel, "«Штаб Разведки» заказчика");
      part("shield", "Защита государства", hc.guard, "«Штаб Контрразведки» государства героя");
      part("hero", "Модификаторы героя", hc.hero, "Модификаторы и таланты героя");
      ui::separator();
      ui::Row r({ui::fr(1), ui::px(110)}, 34, 8);
      ui::label(hc.guarded ? "Защищён от убийства" : "Вероятность успеха",
                {.font = ui::Font::Strong, .ink = hc.guarded ? ui::Ink::Danger : ui::Ink::Normal, .icon = hc.guarded ? "shield" : "crosshair"});
      ui::label(fmtPct(hc.total), {.font = ui::Font::Number, .ink = hc.total >= 50 ? ui::Ink::Success : hc.total > 0 ? ui::Ink::Warning : ui::Ink::Danger,
                                   .align = ui::Align::Right});
      a.markUi("plot.hit.chance");
    }
    // Оплата: заказчик внешнего заговора — разница с прежним заказом.
    std::string why;
    if (p->external()) {
      const Faction* f = w.faction(p->owner);
      const double treasury = f ? f->treasury() : 0;
      const double delta = gold - (was ? was->gold : 0);
      ui::Row r({ui::fr(1), ui::fr(1)}, 22, 8);
      ui::label("Казна · " + fmtGold(treasury), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "treasury"});
      if (delta > 1e-9) ui::label("Оплата · " + fmtGold(delta), {.font = ui::Font::Small, .ink = ui::Ink::Warning, .align = ui::Align::Right});
      else if (delta < -1e-9) ui::label("Вернётся · " + fmtGold(-delta), {.font = ui::Font::Small, .ink = ui::Ink::Success, .align = ui::Align::Right});
      else ui::next(0, 0);
      if (delta > 1e-9 && treasury + 1e-9 < delta) why = "Недостаточно золота: нужно " + fmtGold(delta) + ", в казне " + fmtGold(treasury);
    }
    ui::ModalFooter footer;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    const bool same = was && std::fabs(was->gold - gold) < 1e-9;
    if (!manage) why = "Заказ — за государство-заказчика";
    else if (same && why.empty()) why = "Заказ уже такой";
    if (ui::button(was ? "Изменить заказ" : "Заказать убийство", {.variant = ui::Variant::Danger, .icon = "crosshair", .disabled = !why.empty(),
                                                                  .isDefault = true, .tooltip = why})) {
      const Id pl = plot, ch = character;
      const double g = gold;
      if (plotAct(a, *p, "Заказать убийство", [&](Tx& tx) { rules::orderHit(tx, pl, ch, g); })) return false;
    }
    a.markUi("plot.hit.ok");
    return true;
  }
};

}  // namespace

void plotui::openHit(App& a, Id plot, Id character) {
  auto d = std::make_unique<HitDlg>();
  d->plot = plot;
  d->character = character;
  a.openDialog(std::move(d));
}

}  // namespace rg::app
