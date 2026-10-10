// Regnum — флот: вместимость (ТЗ «Доработки №3», п.6) и «Поставить флот» у верфи (п.3). Без «Свободного
// редактирования флотов» новый флот появляется только у берега провинции фракции с достроенной верфью: кнопка открывает
// список таких провинций (приморские, с верфью), выбор ставит флот на свободное место моря у её берега.
#include "app/panels/military.h"

namespace rg::app::mil {

void capacityRow(App& a, Id fleet, i64 used) {
  const World& w = frameWorld(a);
  const i64 cap = rules::fleetCapacity(w, fleet);
  if (used < 0) used = rules::armySize(w, rules::cargoOf(w, fleet));
  const bool over = used > cap;
  const std::string tip = "Вместимость флота: фрегат — " + fmtCount(rules::shipCapacity(w, ShipType::Frigate)) + ", линкор — " +
                          fmtCount(rules::shipCapacity(w, ShipType::ShipOfLine)) + " воинов" + (over ? "\nПерегруз: войско больше вместимости" : "");
  ui::IdScope s(i64(fleet) + 0x79000000LL);
  ui::Row r({ui::px(18), ui::fr(1), ui::px(128)}, 22, 8);
  ui::icon("embark", over ? ui::Ink::Danger : ui::Ink::Dim, 16, tip);
  ui::progress(cap > 0 ? std::min(1.0, double(used) / double(cap)) : (used > 0 ? 1.0 : 0.0), {.tone = over ? ui::Tone::Danger : ui::Tone::Accent});
  ui::label(fmtCount(used) + " / " + fmtCount(cap), {.font = ui::Font::Strong, .ink = over ? ui::Ink::Danger : ui::Ink::Normal, .align = ui::Align::Right,
                                                     .tooltip = tip});
  a.markUi("fleet.capacity");
}

namespace {

// Действующий уровень верфи провинции (наибольший среди построек-верфей).
int shipyardLevel(const World& w, const Province& p) {
  int lvl = 0;
  for (const ProvBuilding& pb : p.buildings)
    if (const Building* b = w.building(pb.building); b && b->shipyard) lvl = std::max(lvl, pb.builtLevel());
  return lvl;
}

}  // namespace

void placeFleetButton(App& a, Id fid, bool empty) {
  const World& w = frameWorld(a);
  const bool free = w.settings->freeFleets;
  bool clicked = false;
  if (empty) clicked = ui::emptyState("fleet", "Флотов на карте нет.", "Поставить флот", "tool-fleet");
  else
    clicked = ui::button("Поставить флот", {.variant = ui::Variant::Ghost, .icon = "tool-fleet", .size = ui::Size::Small,
                                            .tooltip = free ? std::string_view() : std::string_view("У верфи приморской провинции")});
  a.markUi(empty ? "mil.fleet.placeEmpty" : "mil.fleet.place");
  if (clicked) {
    if (free) startPlacing(a, ArmyKind::Fleet, fid);
    else ui::openPopup("placefleet");
  }
  if (free || !ui::beginPopup("placefleet", {.width = 300, .maxHeight = 360})) return;
  ui::caption("Верфи у моря");
  std::vector<Id> list = rules::shipyardProvinces(w, fid);
  std::sort(list.begin(), list.end(), [&](Id x, Id y) { return compareRu(w.provinceName(x), w.provinceName(y)) < 0; });
  if (list.empty()) {
    ui::label("Нет приморских провинций с верфью", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "shipyard"});
    a.markUi("mil.fleet.place.none");
  }
  for (Id pid : list) {
    const Province* p = w.province(pid);
    if (!p) continue;
    ui::IdScope s{i64(pid)};
    const std::string sub = "Верфь · " + std::to_string(shipyardLevel(w, *p)) + " ур.";
    if (ui::listItem(w.provinceName(pid), {.icon = "shipyard", .subtitle = sub})) {
      Id id = 0;
      if (a.act("Поставить флот: " + w.provinceName(pid), [&](Tx& tx) { id = rules::placeFleet(tx, fid, pid); })) {
        ui::closePopup();
        a.select(SelType::Army, id, true);
        a.ui.tabOf[SelType::Army] = "army.units";
      }
    }
    a.markUi("mil.fleet.place." + std::to_string(pid));
  }
  ui::endPopup();
}

}  // namespace rg::app::mil
