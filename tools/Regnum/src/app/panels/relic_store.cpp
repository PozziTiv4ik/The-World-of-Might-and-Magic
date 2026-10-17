// Regnum — хранилище реликвий постройки в провинции (ТЗ «Доработки №1», п.9; «Доработки №4», п.3–4): у достроенной
// постройки с возможностью «Хранилище реликвий» — её реликвии стопками (значок с изображением и обводкой цвета
// редкости, «× N» у копируемых). Игровые действия за государство-владельца (canPlay): «Вынуть» — штуки в инвентарь
// государства (rules::unstoreRelic; несколько — окно «Сколько»), «Положить реликвию» — из инвентаря государства, его
// других хранилищ и инвентаря его героев (rules::storeRelic с местом), сколько — поле рядом. Администратор (правка
// мира): количество в стопке и новые реликвии из справочника.
#include <algorithm>

#include "app/app_internal.h"
#include "app/common/relics.h"
#include "app/editors/buildings.h"
#include "app/widgets.h"

namespace rg::app::bld {

using w::orName;

void relicStoreBlock(App& a, Id pid, Id bid) {
  const World w = a.world();
  const Province* p = w.province(pid);
  if (!p) return;
  const ProvBuilding* pb = nullptr;
  for (const ProvBuilding& x : p->buildings)
    if (x.building == bid) pb = &x;
  if (!pb || pb->builtLevel() < 1) return;
  const Faction* owner = w.faction(p->owner);
  const Id state = owner && owner->isState() ? owner->id : 0;
  const bool play = state && a.canPlay(state), admin = a.admin(), edit = a.canEdit();
  const std::string mark = "prov.store." + std::to_string(bid);
  const rules::RelicPlace here{rules::RelicPlace::Building, pid, bid, p->owner};
  ui::IdScope scope("relicstore");
  ui::separator();
  const std::vector<RelicStack> stored = relics::sorted(w, pb->relics);
  const i64 total = relics::pieces(w, pb->relics);
  {
    ui::HStack hs(22, ui::Align::Left, 6);
    ui::icon("relic", ui::Ink::Dim, 16);
    ui::label("Хранилище реликвий", {.font = ui::Font::Small, .ink = ui::Ink::Dim});
    if (total) ui::badge(fmtInt(total), ui::Tone::Neutral);
    a.markUi(mark);
  }
  for (const RelicStack& st : stored) {
    const Relic& r = *w.relic(st.relic);
    const Id rid = r.id;
    ui::IdScope s{i64(rid)};
    const bool count = admin && !r.unique();
    const float right = (play ? 30.f : 0.f) + (count ? relics::kCountW + 6 : 0.f);
    RectF rr;
    if (relics::stackRow(r, count ? 0 : st.count, relics::kindText(r), right, &rr) && admin) relics::openCatalog(a, rid);
    a.markUi(mark + ".relic." + std::to_string(rid), rr);
    float x = rr.right() - 2;
    if (play) {
      x -= 28;
      ui::at(RectF{x, rr.cy() - 13, 26, 26});
      const std::string tip = "Вынуть в инвентарь государства «" + orName(owner->name) + "»";
      if (ui::iconButton("upload", tip, {.size = ui::Size::Small})) {
        if (st.count > 1) ui::openPopup("take");
        else a.play("Вынуть реликвию из хранилища", [&](Tx& tx) { rules::unstoreRelic(tx, pid, bid, rid, 1); });
      }
      a.markUi(mark + ".take." + std::to_string(rid));
      i64 n = 1;
      if (relics::countPopup(a, "take", "Вынуть · " + orName(r.name), "Вынуть", st.count, n, mark + ".takeN"))
        a.play("Вынуть реликвии из хранилища", [&](Tx& tx) { rules::unstoreRelic(tx, pid, bid, rid, n); });
    }
    if (count) {
      x -= relics::kCountW + 6;
      i64 n = st.count;
      ui::Disabled d(!edit);
      if (relics::countField("count", RectF{x, rr.cy() - 15, relics::kCountW, 30}, n))
        a.act("Количество реликвий в хранилище", [&](Tx& tx) { rules::setRelicCount(tx, rid, here, n); },
              {.coalesce = "store.relicCount:" + std::to_string(pid) + ":" + std::to_string(bid) + ":" + std::to_string(rid)});
      a.markUi(mark + ".count." + std::to_string(rid));
    }
  }
  if (!play) return;
  // Положить: владения государства-владельца; администратору — ещё новые из справочника. Сколько — поле справа.
  const relics::Places pl(w);
  const std::vector<relics::Source> src = relics::sources(w, pl, state, here, relics::Target::Store, edit);
  i64& howMany = ui::state<i64>(ui::id("##putCount"));
  howMany = std::max<i64>(1, howMany);
  int idx = -1;
  {
    ui::Row row({ui::fr(1), ui::px(relics::kCountW)}, 30, 6);
    idx = relics::sourceCombo("put", w, state, src, src.empty() ? "Нет реликвий, которые можно положить" : "Положить реликвию",
                              "Из инвентаря государства, его хранилищ и героев");
    a.markUi(mark + ".put");
    ui::numberField("putCount", howMany, {.min = 1, .max = double(schema::kMaxCount), .steppers = true, .disabled = src.empty(), .tooltip = "Сколько положить"});
    a.markUi(mark + ".putCount");
  }
  if (idx < 0) return;
  const relics::Source pick = src[size_t(idx)];
  const Relic* r = w.relic(pick.relic);
  // Уникальная — одна штука; из места — не больше, чем там лежит.
  const i64 n = r && r->unique() ? 1 : pick.from.kind == rules::RelicPlace::Free ? howMany : std::min(howMany, pick.count);
  const Id rid = pick.relic;
  const rules::RelicPlace from = pick.from;
  auto put = [&](Tx& tx) { rules::storeRelic(tx, pid, bid, rid, from, n); };
  if (pick.rank <= 2) a.play("Положить реликвию в хранилище", put);
  else a.act("Реликвия в хранилище из справочника", put);
}

}  // namespace rg::app::bld
