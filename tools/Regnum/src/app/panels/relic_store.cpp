// Regnum — хранилище реликвий постройки в провинции (ТЗ «Доработки №1», п.9): у достроенной постройки с возможностью
// «Хранилище реликвий» — её реликвии строками (значок с изображением и обводкой цвета редкости), «Вынуть» (реликвия
// переходит государству-владельцу, rules::unstoreRelic) и «Положить реликвию» — из свободных, реликвий государства-
// владельца и инвентаря его героев (rules::storeRelic).
#include <algorithm>

#include "app/app_internal.h"
#include "app/widgets.h"

namespace rg::app {

namespace {

std::string orName(const std::string& s, const char* fallback = "Без названия") { return s.empty() ? std::string(fallback) : s; }

}  // namespace

void relicStoreBlock(App& a, Id pid, Id bid) {
  const World w = a.world();
  const Province* p = w.province(pid);
  if (!p) return;
  const ProvBuilding* pb = nullptr;
  for (const ProvBuilding& x : p->buildings)
    if (x.building == bid) pb = &x;
  if (!pb || pb->builtLevel() < 1) return;
  const bool ro = a.readOnly();
  const Faction* owner = w.faction(p->owner);
  const std::string mark = "prov.store." + std::to_string(bid);
  ui::IdScope scope("relicstore");
  ui::separator();
  std::vector<Id> stored;
  for (Id r : pb->relics)
    if (w.relic(r)) stored.push_back(r);
  rules::sortByRarity(w, stored);
  {
    ui::HStack hs(22, ui::Align::Left, 6);
    ui::icon("relic", ui::Ink::Dim, 16);
    ui::label("Хранилище реликвий", {.font = ui::Font::Small, .ink = ui::Ink::Dim});
    if (!stored.empty()) ui::badge(std::to_string(stored.size()), ui::Tone::Neutral);
    a.markUi(mark);
  }
  for (Id rid : stored) {
    const Relic& r = *w.relic(rid);
    ui::IdScope s{i64(rid)};
    RectF rr;
    w::relicRow(r, {}, ro ? 0 : 32, &rr);
    a.markUi(mark + ".relic." + std::to_string(rid), rr);
    if (ro) continue;
    ui::at(RectF{rr.right() - 28, rr.cy() - 12, 24, 24});
    if (ui::iconButton("upload", owner && owner->isState() ? "Вынуть — реликвия перейдёт государству" : "Вынуть", {.size = ui::Size::Small}))
      a.act("Вынуть реликвию из хранилища", [&](Tx& tx) { rules::unstoreRelic(tx, pid, bid, rid); });
    a.markUi(mark + ".take." + std::to_string(rid));
  }
  if (ro || !owner) return;
  // Кандидаты: свободные, реликвии государства-владельца, инвентарь его героев (по главенству редкости).
  struct Cand {
    const Relic* r;
    int rank;
    std::string hint;
  };
  std::vector<Cand> cand;
  for (const Relic& r : w.catalogs->relics) {
    const rules::RelicPlace pl = rules::relicPlace(w, r.id);
    if (pl.kind == rules::RelicPlace::Free) cand.push_back({&r, 0, schema::rarity(r.rarity).name});
    else if (pl.kind == rules::RelicPlace::State && pl.id == p->owner) cand.push_back({&r, 1, "у государства"});
    else if (pl.kind == rules::RelicPlace::Hero && pl.owner == p->owner) cand.push_back({&r, 2, "у " + orName(w.characterName(pl.id), "Без имени")});
  }
  std::stable_sort(cand.begin(), cand.end(), [](const Cand& x, const Cand& y) {
    if (x.rank != y.rank) return x.rank < y.rank;
    if (x.r->rarity != y.r->rarity) return rules::rarityAbove(x.r->rarity, y.r->rarity);
    return compareRu(x.r->name, y.r->name) < 0;
  });
  std::vector<std::string> labels;
  labels.reserve(cand.size());
  for (const Cand& k : cand) labels.push_back(orName(k.r->name));
  std::vector<ui::Option> opts;
  for (size_t i = 0; i < cand.size(); i++) opts.push_back(ui::Option{labels[i], "relic", w::rarityColor(cand[i].r->rarity), cand[i].hint});
  int idx = -1;
  if (ui::combo("put", idx, std::span<const ui::Option>(opts),
                {.placeholder = cand.empty() ? "Нет реликвий, которые можно положить" : "Положить реликвию", .search = 1, .icon = "plus",
                 .disabled = cand.empty(), .popupWidth = 340, .tooltip = "Свободные реликвии, реликвии государства и его героев"}) &&
      idx >= 0 && idx < int(cand.size())) {
    const Id rid = cand[size_t(idx)].r->id;
    a.act("Положить реликвию в хранилище", [&](Tx& tx) { rules::storeRelic(tx, pid, bid, rid); });
  }
  a.markUi(mark + ".put");
}

}  // namespace rg::app
