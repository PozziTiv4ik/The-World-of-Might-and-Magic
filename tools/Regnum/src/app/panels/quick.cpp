// Regnum — самое нужное о выделении в нижней строке (QuickReg): название правится на месте, рядом — владелец,
// культура и вера провинции, численность и полководец войска, гильдия маршрута, правитель и казна фракции,
// фракция персонажа. Поля не шире нужного; что не помещается в строку — не показывается.
#include "app/panels/military.h"
#include "app/widgets.h"

namespace rg::app {
namespace {

// Название: фиксация по Enter или уходу фокуса, пустое не принимается.
void nameField(App& a, std::string_view label, const std::string& value, float width, std::string_view placeholder,
               const std::function<void(Tx&, const std::string&)>& set) {
  ui::Group g(width);
  std::string v = value;
  if (ui::textField("name", v, {.placeholder = placeholder, .maxLength = 80, .readOnly = a.readOnly(), .tooltip = placeholder})) {
    std::string n = trim(v);
    if (!n.empty() && n != value) a.act(label, [&](Tx& tx) { set(tx, n); });
  }
  a.markUi("quick.name");
}

bool room(float w) { return ui::avail().w >= w; }

// ---------------------------------------------------------------- провинция
void provinceQuick(App& a, Id pid) {
  const World& w = a.world();
  const Province* p = w.province(pid);
  if (!p) return;
  const bool ro = a.readOnly();
  nameField(a, "Переименовать провинцию", p->name, 180, "Название провинции", [pid](Tx& tx, const std::string& n) { tx.province(pid).name = n; });
  if (p->sea) return;
  if (room(140)) {
    if (p->owner && w.faction(p->owner)) w::factionChip(p->owner);
    else ui::chip("Без владельца", {.icon = "flag", .tooltip = "Владелец не назначен"});
    a.markUi("quick.owner");
  }
  if (room(160)) {
    ui::Group g(150);
    Id cu = p->culture;
    if (w::catalogPicker("culture", rules::CatalogList::Cultures, cu, "Культура", false, ro))
      a.act("Культура провинции", [&](Tx& tx) { tx.province(pid).culture = cu; });
    a.markUi("quick.culture");
  }
  if (room(160)) {
    ui::Group g(150);
    Id re = p->religion;
    if (w::catalogPicker("religion", rules::CatalogList::Religions, re, "Религия", false, ro))
      a.act("Религия провинции", [&](Tx& tx) { tx.province(pid).religion = re; });
    a.markUi("quick.religion");
  }
  if (room(90)) {
    i64 pop = 0;
    for (const RacePop& r : p->races) pop += std::max<i64>(0, r.pop);
    ui::label(fmtShort(double(pop)), {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "population", .tooltip = "Население"});
  }
}

// ---------------------------------------------------------------- войско и флот
void armyQuick(App& a, Id id) {
  const World& w = a.world();
  const Army* ar = w.army(id);
  if (!ar) return;
  const bool fleet = ar->isFleet();
  nameField(a, fleet ? "Переименовать флот" : "Переименовать войско", ar->name, 180, fleet ? "Название флота" : "Название войска",
            [id](Tx& tx, const std::string& n) { rules::renameArmy(tx, id, n); });
  for (const ArmyGroup& g : ar->groups) {
    if (!room(140)) break;
    ui::IdScope s{i64(g.faction)};
    w::factionChip(g.faction);
  }
  if (room(90)) ui::label(mil::fmtCount(mil::unitCount(*ar)), {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = fleet ? "fleet" : "army", .tooltip = fleet ? "Кораблей" : "Воинов"});
  if (ar->commander && w.character(ar->commander) && room(140)) w::characterChip(ar->commander);
}

// ---------------------------------------------------------------- торговый маршрут
void routeQuick(App& a, Id id) {
  const World& w = a.world();
  const Route* r = w.route(id);
  if (!r) return;
  nameField(a, "Переименовать маршрут", r->name, 180, "Название маршрута", [id](Tx& tx, const std::string& n) { tx.route(id).name = n; });
  if (r->guild && w.faction(r->guild) && room(140)) w::factionChip(r->guild);
}

// ---------------------------------------------------------------- государство и гильдия
void factionQuick(App& a, Id id) {
  const World& w = a.world();
  const Faction* f = w.faction(id);
  if (!f) return;
  nameField(a, f->isState() ? "Переименовать государство" : "Переименовать гильдию", f->name, 200, "Название",
            [id](Tx& tx, const std::string& n) { tx.faction(id).name = n; });
  if (f->ruler && w.character(f->ruler) && room(150)) w::characterChip(f->ruler);
  if (room(110)) {
    const double t = f->treasury();
    ui::label(fmtGoldShort(t), {.font = ui::Font::Small, .ink = t < 0 ? ui::Ink::Danger : ui::Ink::Dim, .icon = "coins", .tooltip = "Казна"});
  }
}

// ---------------------------------------------------------------- персонаж
void characterQuick(App& a, Id id) {
  const World& w = a.world();
  const Character* c = w.character(id);
  if (!c) return;
  nameField(a, "Имя персонажа", c->name, 200, "Имя", [id](Tx& tx, const std::string& n) { tx.character(id).name = n; });
  if (c->faction && w.faction(c->faction) && room(140)) w::factionChip(c->faction);
  if (!c->title.empty() && room(80)) ui::label(c->title, {.font = ui::Font::Small, .ink = ui::Ink::Dim});
}

QuickReg qProvince({"province.quick", SelType::Province, 10, provinceQuick});
QuickReg qArmy({"army.quick", SelType::Army, 10, armyQuick});
QuickReg qRoute({"route.quick", SelType::Route, 10, routeQuick});
QuickReg qFaction({"faction.quick", SelType::Faction, 10, factionQuick});
QuickReg qCharacter({"character.quick", SelType::Character, 10, characterQuick});

}  // namespace
}  // namespace rg::app
