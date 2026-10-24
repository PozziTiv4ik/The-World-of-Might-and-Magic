// Regnum — вкладка «События» провинции, государства (гильдии) и героя (аудит 2026-10-23, 8.3): записи хроники о
// сущности по ходам, новые сверху, общими строками хроники (turnui::entityEvents; щелчок — показать объект записи).
// Войско — та же вкладка в panels/army_inspector.cpp. Туман войны: игрок видит общие записи и записи с участием
// известных ему государств (turnui::entryVisible).
//   Провинция — записи с этой провинцией; государство — записи, где оно среди фракций; герой — записи, где его имя
//   упомянуто словом целиком (у записей хроники нет ссылки на героя).
#include "app/app_internal.h"
#include "app/turn/turn_ui.h"
#include "app/widgets.h"

namespace rg::app {
namespace {

void provinceEvents(App& a, Id pid) {
  const World& w = w::frameWorld(a);
  turnui::entityEvents(a, w, [pid](const LogEntry& e) { return e.province == pid; }, "province.events." + std::to_string(pid));
}

void factionEvents(App& a, Id fid) {
  const World& w = w::frameWorld(a);
  turnui::entityEvents(a, w, [fid](const LogEntry& e) { return std::find(e.factions.begin(), e.factions.end(), fid) != e.factions.end(); },
                       "faction.events." + std::to_string(fid));
}

void characterEvents(App& a, Id cid) {
  const World& w = w::frameWorld(a);
  const Character* c = w.character(cid);
  const std::string name = c ? trim(c->name) : std::string();
  if (name.empty()) {
    ui::emptyState("chronicle", "Событий нет.");
    return;
  }
  turnui::entityEvents(a, w, [name](const LogEntry& e) { return turnui::mentions(e.text, name); }, "character.events." + std::to_string(cid),
                       hash64(name));
}

TabReg provinceTab({"province.events", "chronicle", "События", 70, SelType::Province, nullptr, provinceEvents});
TabReg factionTab({"faction.events", "chronicle", "События", 80, SelType::Faction, nullptr, factionEvents});
TabReg characterTab({"character.events", "chronicle", "События", 30, SelType::Character, nullptr, characterEvents});

}  // namespace
}  // namespace rg::app
