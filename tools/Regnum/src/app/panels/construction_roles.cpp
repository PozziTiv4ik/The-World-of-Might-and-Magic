// Regnum — действия достроенных построек в провинции (ТЗ «Доработки №3», п.4–5; «Доработки №2–4»): у здания
// целительства — «Вылечить провинцию» (500 любой эссенции владельца или 500 «Лечебных трав»: выбор во всплывающем меню;
// rules::healProvince), у здания чумы — «Заразить чумой» (2500 эссенции чумы; rules::plagueProvince), у рынка —
// «Продать реликвии» (окно рынка, dialogs/market.cpp). Игровые действия владельца провинции (App::canPlay, App::play);
// недоступная кнопка объясняет причину в подсказке.
#include <algorithm>

#include "app/editors/buildings.h"
#include "app/widgets.h"

namespace rg::app::bld {

namespace {

Id essenceNamed(const World& w, std::string_view name) {
  const std::string k = utf8::searchKey(name);
  for (const CatalogItem& e : w.catalogs->essences)
    if (utf8::searchKey(e.name) == k) return e.id;
  return 0;
}

const Faction* ownerState(const World& w, const Province& p) {
  const Faction* f = w.faction(p.owner);
  return f && f->isState() ? f : nullptr;
}

// Общая причина недоступности игрового действия владельца (пусто — можно).
std::string playWhy(App& a, const Faction* owner, const char* noOwner) {
  if (a.readOnly()) return "Открыт прошлый ход";
  if (!owner) return noOwner;
  if (!a.canPlay(owner->id)) return "Провинция другого государства";
  return {};
}

// Лечение: причина недоступности (пусто — можно): чума, хотя бы 500 одной эссенции или лечебных трав.
std::string healWhy(App& a, const Province& p, const Faction* owner) {
  const World& w = a.world();
  if (std::string why = playWhy(a, owner, "У провинции нет владельца — платить за лечение некому"); !why.empty()) return why;
  if (!rules::hasModKey(w, p.modifiers, schema::mod::Plague)) return "В провинции нет чумы";
  for (const CatalogItem& e : w.catalogs->essences)
    if (owner->essence(e.id) + 1e-9 >= schema::kHealCost) return {};
  if (const Id herbs = rules::healingHerbs(w); herbs && owner->stock(herbs) + 1e-9 >= schema::kHealCost) return {};
  return "Нужно " + fmtNum(schema::kHealCost) + " любой эссенции или лечебных трав: столько нет";
}

std::string plagueWhy(App& a, const Province& p, const Faction* owner, Id ess) {
  const World& w = a.world();
  if (std::string why = playWhy(a, owner, "У провинции нет владельца — платить за заражение некому"); !why.empty()) return why;
  std::string why;
  if (!rules::canPlague(w, p.id, &why)) return why;
  if (!ess) return "В справочнике нет «Эссенции чумы»";
  if (owner->essence(ess) + 1e-9 < schema::kInfectCost)
    return "Нужно " + fmtNum(schema::kInfectCost) + " эссенции чумы, есть " + fmtNum(std::max(0.0, owner->essence(ess)), 3);
  return {};
}

std::string marketWhy(App& a, const Faction* owner) {
  const World& w = a.world();
  if (std::string why = playWhy(a, owner, "У провинции нет владельца"); !why.empty()) return why;
  if (rules::sellableRelics(w, owner->id).empty()) return "В хранилищах реликвий государства нет реликвий";
  return {};
}

}  // namespace

void roleActions(App& a, Id province, const ProvBuilding& pb, const Building& b) {
  if (!b.healing && !b.plague && !b.market) return;
  if (pb.builtLevel() < 1) return;
  const World& w = a.world();
  const Province* p = w.province(province);
  if (!p) return;
  const Faction* owner = ownerState(w, *p);
  const Id bid = b.id;
  const std::string id = std::to_string(bid);
  ui::IdScope scope("roles");
  ui::HStack hs(30, ui::Align::Left, 6);
  if (b.healing) {
    const std::string why = healWhy(a, *p, owner);
    if (ui::button("Вылечить провинцию", {.icon = "heal", .size = ui::Size::Small, .disabled = !why.empty(),
                                          .tooltip = why.empty() ? "Снять чуму за " + fmtNum(schema::kHealCost) + " любой эссенции или лечебных трав" : why}))
      ui::openPopup("heal");
    a.markUi("prov.heal." + id);
    if (ui::beginMenu("heal")) {
      const Id pid = province;
      // Лечебные травы — первыми, затем эссенции, которых хватает (запас — после названия).
      ui::menuHeader("Цена · " + fmtNum(schema::kHealCost));
      if (const Id herbs = rules::healingHerbs(w)) {
        const double have = owner ? owner->stock(herbs) : 0;
        if (ui::menuItem(w::resourceName(w, herbs) + " · " + fmtNum(have, 3), {.icon = w::resourceIcon(w, herbs), .disabled = have + 1e-9 < schema::kHealCost})) {
          if (a.play("Вылечить провинцию", [&](Tx& tx) { rules::healProvince(tx, pid, bid, 0); }))
            a.toast("Чума излечена: " + w.provinceName(pid), ToastKind::Success, "heal");
        }
        a.markUi("prov.heal." + id + ".herbs");
      }
      for (const CatalogItem& e : w.catalogs->essences) {
        const double have = owner ? owner->essence(e.id) : 0;
        if (have + 1e-9 < schema::kHealCost) continue;
        ui::IdScope s{i64(e.id)};
        const std::string label = w::orName(e.name) + " · " + fmtNum(have, 3);
        if (ui::menuItem(label, {.icon = "essence"})) {
          const Id ess = e.id;
          if (a.play("Вылечить провинцию", [&](Tx& tx) { rules::healProvince(tx, pid, bid, ess); }))
            a.toast("Чума излечена: " + w.provinceName(pid), ToastKind::Success, "heal");
        }
        a.markUi("prov.heal." + id + ".ess." + std::to_string(e.id));
      }
      ui::endMenu();
    }
  }
  if (b.plague) {
    const Id ess = essenceNamed(w, "Эссенция чумы");
    const std::string why = plagueWhy(a, *p, owner, ess);
    if (ui::button("Заразить чумой", {.icon = "plague", .size = ui::Size::Small, .disabled = !why.empty(),
                                      .tooltip = why.empty() ? "Чума в провинции за " + fmtNum(schema::kInfectCost) + " эссенции чумы" : why})) {
      if (a.play("Заразить чумой", [&](Tx& tx) { rules::plagueProvince(tx, province, bid); }))
        a.toast("Чума в провинции " + a.world().provinceName(province), ToastKind::Warning, "plague");
    }
    a.markUi("prov.plague." + id);
  }
  if (b.market) {
    const std::string why = marketWhy(a, owner);
    if (ui::button("Продать реликвии", {.icon = "scales", .size = ui::Size::Small, .disabled = !why.empty(),
                                        .tooltip = why.empty() ? std::string("Реликвии из хранилищ государства — за золото в казну") : why}))
      a.openDialog(marketDialog(owner->id));
    a.markUi("prov.market." + id);
  }
}

}  // namespace rg::app::bld
