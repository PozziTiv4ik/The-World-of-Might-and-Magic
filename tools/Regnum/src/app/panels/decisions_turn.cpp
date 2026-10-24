// Regnum — блоки раздела «Решения», связанные с ходом (app/panels/decisions.h; аудит 2026-10-23, 2.5, 2.8 и раздел 11):
// - «События хода» (администратору): неразобранные события последнего хода из мира (Meta::turnEvents) — восстания,
//   мятежи, войны мятежников; «Продолжить» снова открывает их окна по очереди (flow::processTurnEvents);
// - «Археологи в опасности»: группы в смертельной опасности (администратору — всех государств, игроку — своего), строка
//   ведёт во вкладку «Археология» государства, где решается «божественное вмешательство»;
// - «Журнал игроков» (администратору): игровые действия игроков — ход, государство, действие, время (turn/journal.h).
#include "app/panels/decisions.h"

#include "app/app_internal.h"
#include "app/flows.h"
#include "app/panels/military.h"
#include "app/turn/journal.h"
#include "app/turn/turn_ui.h"
#include "app/widgets.h"

namespace rg::app::decisions {

namespace {

// ---------------------------------------------------------------- события хода
struct EventView {
  const char* icon = "rebellion";
  std::string text, sub;
};

EventView eventView(const World& w, const TurnEvent& e) {
  EventView v;
  const Army* ar = w.army(e.army);
  switch (e.kind) {
    case TurnEvent::Uprising:
      v.text = "Восстание · " + w.provinceName(e.province);
      v.sub = w.factionName(e.rebelState) + " против " + w.factionName(e.origin);
      break;
    case TurnEvent::Mutiny:
      v.text = "Мятеж · " + (ar ? mil::objectName(*ar) : w.provinceName(e.province));
      v.sub = w.factionName(e.rebelState) + " против " + w.factionName(e.origin);
      if (!e.heroes.empty()) v.sub += " · судьба героев: " + std::to_string(e.heroes.size());
      break;
    case TurnEvent::RebelWar:
      v.icon = "war";
      v.text = "Война мятежников · " + w.factionName(e.rebelState);
      v.sub = "против " + w.factionName(e.origin) + " · вассалы и сюзерены";
      break;
  }
  return v;
}

void eventsBlock(App& a, const World& w) {
  if (!a.admin()) return;   // события хода (битвы, штурмы, вассалитет) разбирает администратор
  const std::vector<TurnEvent>& q = w.meta->turnEvents;
  if (q.empty()) return;
  const std::string badge = std::to_string(q.size());
  ui::Section s("События хода", "next-turn", {.badge = badge});
  a.markUi("decisions.events");
  if (!s) return;
  for (size_t i = 0; i < q.size(); i++) {
    const TurnEvent& e = q[i];
    ui::IdScope is{i64(i)};
    const EventView v = eventView(w, e);
    if (ui::listItem(v.text, {.icon = v.icon, .subtitle = v.sub, .tooltip = v.text})) {
      const Id army = w.army(e.army) ? e.army : 0, province = w.province(e.province) ? e.province : 0;
      detail::later(a, [army, province](App& x) {
        if (army) x.select(SelType::Army, army, true);
        else if (province) x.select(SelType::Province, province, true);
      });
    }
    a.markUi("decisions.event." + std::to_string(i));
  }
  const bool running = flow::turnEventsRunning(a);
  ui::Disabled dis(!a.canEdit() || running);
  if (ui::button(running ? "Окна событий открыты" : "Продолжить", {.variant = ui::Variant::Primary, .icon = "next-turn",
                                                                     .tooltip = "Открыть окна событий хода по очереди"}))
    flow::processTurnEvents(a);
  a.markUi("decisions.events.continue");
}

// ---------------------------------------------------------------- археологи в опасности
struct DangerRow {
  Id state = 0, group = 0;
};

// Группы в смертельной опасности: администратору — всех государств (state 0), игроку — его государства.
std::vector<DangerRow> dangers(const App& a, const World& w, Id only = 0) {
  std::vector<DangerRow> out;
  w.factions.each([&](const Faction& f) {
    if (!f.isState() || (only && f.id != only) || (a.player() && f.id != a.ui.playerState)) return;
    for (const ArchGroup& g : f.archGroups)
      if (g.danger) out.push_back({f.id, g.id});
  });
  return out;
}

void dangerBlock(App& a, const World& w) {
  const std::vector<DangerRow> rows = dangers(a, w);
  if (rows.empty()) return;
  const std::string badge = std::to_string(rows.size());
  ui::Section s("Археологи в опасности", "skull", {.badge = badge});
  a.markUi("decisions.arch");
  if (!s) return;
  for (const DangerRow& r : rows) {
    const Faction* f = w.faction(r.state);
    const ArchGroup* g = f ? f->archGroup(r.group) : nullptr;
    if (!g) continue;
    ui::IdScope is{i64(r.group)};
    if (ui::listItem("Группа «" + w::orName(g->name) + "»", {.dot = f->color, .subtitle = w::orName(f->name), .hint = "Божественное вмешательство"})) {
      const Id st = r.state;
      detail::later(a, [st](App& x) {
        x.ui.tabOf[SelType::Faction] = "faction.arch";
        x.select(SelType::Faction, st);
      });
    }
    a.markUi("decisions.arch." + std::to_string(r.state) + "." + std::to_string(r.group));
  }
}

// ---------------------------------------------------------------- журнал игроков
void journalBlock(App& a, const World& w) {
  if (!a.admin()) return;
  const std::vector<journal::Entry> list = journal::entries(a);
  if (list.empty()) return;   // действий игроков нет — блока нет
  const std::string badge = std::to_string(list.size());
  ui::Section s("Журнал игроков", "list", {.defaultOpen = false, .badge = badge});   // справка — свёрнут, пока не нужен
  a.markUi("decisions.journal");
  if (!s) return;
  ui::Column cols[] = {{"Ход", nullptr, ui::px(52), ui::Align::Right},
                       {"Государство", nullptr, ui::fr(1, 130)},
                       {"Действие", nullptr, ui::fr(1.6f, 160)},
                       {"Время", nullptr, ui::px(118), ui::Align::Right}};
  ui::Table t("journal", cols, int(list.size()), {.rowHeight = 36, .selectable = false});
  for (int i : t) {
    const journal::Entry& e = list[size_t(i)];
    a.markUi("decisions.journal." + std::to_string(i), t.rowRect());
    t.text(std::to_string(e.turn), ui::Ink::Dim);
    t.cell();
    if (w.faction(e.state)) turnui::factionLabel(w, e.state, 24);
    else ui::label(e.stateName.empty() ? std::string("—") : e.stateName, {.ink = ui::Ink::Muted});
    t.text(e.action);
    t.text(detail::localTime(e.at), ui::Ink::Muted);
  }
}

}  // namespace

void turnBlocks(App& a, const World& w) {
  eventsBlock(a, w);
  dangerBlock(a, w);
  journalBlock(a, w);
}

int turnPending(App& a) {
  const World& w = a.world();
  const int events = a.admin() ? int(w.meta->turnEvents.size()) : 0;
  return events + int(dangers(a, w).size());
}

int archDangerBadge(App& a, Id faction) {
  if (a.player() && faction != a.ui.playerState) return 0;
  return dangers(a, a.world(), faction).empty() ? 0 : -1;
}

}  // namespace rg::app::decisions
