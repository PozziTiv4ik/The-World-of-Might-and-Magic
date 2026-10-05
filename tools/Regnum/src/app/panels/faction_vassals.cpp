// Regnum — вкладка «Вассалы» сюзерена (ТЗ «Механика вассалитета»): видна, пока у государства есть вассалы; вассалы
// с флагом и названием (щелчок — открыть), отношения с ними — значение, двуполярный индикатор и состояние.
#include "app/panels/faction_common.h"

namespace rg::app {
namespace {

using namespace fac;

void drawVassals(App& a, Id id) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(id);
  if (!f) return;
  const ui::Theme& t = ui::theme();
  std::vector<Id> list = rules::vassalsOf(w, id);
  std::sort(list.begin(), list.end(), [&](Id x, Id y) { return compareRu(w.factionName(x), w.factionName(y)) < 0; });
  int rebels = 0;
  double sum = 0;
  for (Id v : list) {
    sum += w.relation(id, v).v;
    if (rules::canVassalRebel(w, v)) rebels++;
  }
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 10);
    ui::stat(fmtInt(i64(list.size())), plural(i64(list.size()), "вассал", "вассала", "вассалов"), {.icon = "banner", .tone = ui::Tone::Accent});
    ui::stat(list.empty() ? std::string("—") : fmtSigned(sum / double(list.size())), "Отношения", {.icon = "diplomacy", .tone = rebels ? ui::Tone::Danger : ui::Tone::Info,
              .tooltip = rebels ? "Могут восстать: " + std::to_string(rebels) : std::string("Средние отношения с вассалами")});
  }
  ui::spacer(2);
  ui::Section sec("Вассалы", "banner", {.badge = list.empty() ? std::string() : std::to_string(list.size())});
  if (!sec) return;
  if (list.empty()) {
    ui::emptyState("banner", "Вассалов нет.");
    return;
  }
  for (Id v : list) {
    const Faction* vf = w.faction(v);
    if (!vf) continue;
    ui::IdScope sc{i64(v)};
    const Relation rel = w.relation(id, v);
    const bool rebel = rules::canVassalRebel(w, v);
    RectF rr = ui::next(74);
    a.markUi("vassals.row." + std::to_string(v), rr);
    ui::draw::rect(rr, rebel ? t.danger.alpha(t.dark ? 0.09f : 0.07f) : (t.dark ? t.stripe : t.surface3.alpha(0.55f)), t.radiusCard);
    ui::draw::rectStroke(rr, rebel ? t.danger.alpha(0.35f) : (t.dark ? t.hover : t.border), t.radiusCard, 1);
    ui::Area area(rr.inset(12, 8), 0);
    ui::gap(4);
    {
      ui::Row row({ui::px(30), ui::fr(1), ui::px(118)}, 26, 10);
      ui::flag(vf->flag, 30, 20, 2.5f);
      ui::label(displayName(*vf), {.font = ui::Font::Strong, .tooltip = "Вассал — открыть"});
      if (ui::lastItem().hovered) ui::setCursor(platform::Cursor::Hand);
      if (ui::lastItem().clicked) a.select(SelType::Faction, v);
      ui::tag(relLabel(rel.s), rel.s == RelStatus::War ? ui::Tone::Danger : rel.s == RelStatus::Alliance ? ui::Tone::Success : rel.s == RelStatus::Neutral ? ui::Tone::Info : ui::Tone::Neutral,
              relIcon(rel.s));
    }
    {
      ui::Row row({ui::px(30), ui::fr(1), ui::px(56)}, 28, 10);
      RectF k = ui::next(30, 28);
      if (rebel) ui::draw::icon("rebellion", RectF{k.cx() - 7, k.cy() - 7, 14, 14}, t.danger);
      ui::meter(rel.v, {.label = false});
      ui::label(fmtSigned(rel.v), {.font = ui::Font::Strong, .ink = rel.v < 0 ? ui::Ink::Danger : rel.v > 0 ? ui::Ink::Success : ui::Ink::Muted,
                                   .align = ui::Align::Right, .tooltip = rebel ? "Может восстать" : "Отношения"});
    }
  }
}

bool hasVassals(App& a, Id id) {
  const Faction* f = a.world().faction(id);
  return f && f->isState() && !rules::vassalsOf(a.world(), id).empty();
}

TabReg tab({kTabVassals, "banner", "Вассалы", 45, SelType::Faction, hasVassals, drawVassals});

}  // namespace
}  // namespace rg::app
