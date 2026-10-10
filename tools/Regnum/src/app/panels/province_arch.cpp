// Regnum — вкладка провинции «Археология» (ТЗ «Доработки №2», п.1, 13, 16): четыре слота археологических мест (в ряд
// или 2 × 2) с ячейками сокровищ, режим правки (открыть и закрыть слот, заменить место, поле «Спрятанная реликвия»),
// «Спрятать реликвию» и «Откопать спрятанную реликвию» (только спрятавшему государству, пока оно владеет провинцией),
// переход к археологии государства-владельца.
#include "app/panels/arch_common.h"

namespace rg::app::prov {
const World& frameWorld(App& a);   // province_common.cpp
}  // namespace rg::app::prov

namespace rg::app {
namespace {

using namespace archui;

bool g_edit = false;   // режим правки слотов (состояние интерфейса, не мира)

bool landOnly(App& a, Id pid) {
  const Province* p = a.world().province(pid);
  return p && !p->sea;
}

const Faction* ownerState(const World& w, const Province& p) {
  const Faction* f = w.faction(p.owner);
  return f && f->isState() ? f : nullptr;
}

// Где лежит реликвия (подпись в списке «Спрятать реликвию»).
std::string placeText(const World& w, Id relic) {
  const rules::RelicPlace pl = rules::relicPlace(w, relic);
  switch (pl.kind) {
    case rules::RelicPlace::Hero: return w.characterName(pl.id);
    case rules::RelicPlace::Building: {
      const Building* b = w.building(pl.building);
      return (b ? b->name : std::string("Постройка")) + " · " + w.provinceName(pl.id);
    }
    case rules::RelicPlace::State: return "Реликвии государства";
    default: break;
  }
  return {};
}

void hideMenu(App& a, const World& w, Id pid) {
  if (!ui::beginPopup("hide", {.side = ui::Side::Below, .width = 320, .maxHeight = 360})) return;
  ui::label("Спрятать реликвию", {.font = ui::Font::Strong, .icon = "eye-off"});
  for (Id rid : rules::hideableRelics(w, pid)) {
    const Relic* r = w.relic(rid);
    if (!r) continue;
    const RectF top = ui::avail();
    if (w::relicRow(*r, placeText(w, rid))) {
      a.act("Спрятать реликвию", [&](Tx& tx) { rules::hideRelic(tx, pid, rid); });
      ui::closePopup();
    }
    a.markUi("arch.hide.relic." + std::to_string(rid), RectF{top.x, top.y, top.w, 40});
  }
  ui::endPopup();
}

void unearthMenu(App& a, const World& w, const Province& p) {
  if (!ui::beginMenu("unearth")) return;
  const Id pid = p.id, owner = p.owner;
  ui::menuHeader("Откопать спрятанную реликвию");
  int n = 0;
  std::vector<const Character*> heroes;
  w.characters.each([&](const Character& c) {
    if (c.faction == owner && rules::heroAvailable(w, c.id)) heroes.push_back(&c);
  });
  std::sort(heroes.begin(), heroes.end(), [](const Character* x, const Character* y) { return compareRu(x->name, y->name) < 0; });
  for (const Character* c : heroes) {
    ui::IdScope s{i64(c->id)};
    const Id cid = c->id;
    if (ui::menuItem(c->name.empty() ? std::string("Без имени") : c->name, {.icon = "hero"})) {
      a.act("Откопать спрятанную реликвию", [&](Tx& tx) { rules::unearthRelic(tx, pid, rules::RelicPlace{rules::RelicPlace::Hero, cid, 0, owner}); });
    }
    a.markUi("arch.unearth.hero." + std::to_string(cid));
    n++;
  }
  w.provinces.each([&](const Province& q) {
    if (q.owner != owner || q.sea) return;
    for (const ProvBuilding& pb : q.buildings) {
      const Building* b = w.building(pb.building);
      if (!b || !b->relicStore || pb.builtLevel() < 1) continue;
      ui::IdScope s{i64(q.id) * 100000 + i64(b->id)};
      const Id qid = q.id, bid = b->id;
      if (ui::menuItem(b->name + " · " + q.name, {.icon = "building"}))
        a.act("Откопать спрятанную реликвию", [&](Tx& tx) { rules::unearthRelic(tx, pid, rules::RelicPlace{rules::RelicPlace::Building, qid, bid, owner}); });
      a.markUi("arch.unearth.building." + std::to_string(bid));
      n++;
    }
  });
  if (n == 0) ui::menuItem("Нет героев и хранилищ реликвий", {.disabled = true});
  ui::endMenu();
}

void headerRow(App& a, const World& w, const Province& p, bool ro) {
  const Faction* o = ownerState(w, p);
  const Id pid = p.id;
  ui::HStack hs(30, ui::Align::Left, 4);
  ui::iconToggle("edit", "Режим правки", g_edit);
  a.markUi("arch.edit");
  // Спрятать реликвию: реликвия героя, постройки или государства-владельца.
  const std::vector<Id> can = o ? rules::hideableRelics(w, pid) : std::vector<Id>{};
  std::string tip = "Спрятать реликвию";
  if (!o) tip += " — у провинции нет государства-владельца";
  else if (p.hiddenRelic) tip += " — в провинции уже спрятана реликвия";
  else if (can.empty()) tip += " — у государства нет реликвий";
  if (ui::iconButton("eye-off", tip, {.disabled = ro || !o || p.hiddenRelic || can.empty()})) ui::openPopup("hide");
  a.markUi("arch.hide");
  hideMenu(a, w, pid);
  if (p.hiddenRelic && rules::canUnearth(w, pid)) {
    if (ui::iconButton("shovel", "Откопать спрятанную реликвию", {.disabled = ro})) ui::openPopup("unearth");
    a.markUi("arch.unearth");
    unearthMenu(a, w, p);
  }
  ui::flex();
  if (o) {
    if (ui::link(o->name.empty() ? std::string("Без названия") : o->name, "pickaxe")) openStateArch(a, o->id);
    ui::tooltip("Археология государства");
    a.markUi("arch.state");
  }
}

// Режим правки: замок в углу слота — открыть или закрыть слот (закрытие сбрасывает этапы).
void slotLock(App& a, const Province& p, int i, RectF cell, bool ro) {
  const ArchSlot& s = p.arch[size_t(i)];
  const Id pid = p.id;
  ui::at(RectF{cell.x + 4, cell.y + 4, 24, 24});
  if (ui::iconButton(s.open ? "unlock" : "lock", s.open ? "Слот исследован — закрыть" : "Слот не исследован — открыть",
                     {.size = ui::Size::Small, .toggled = s.open, .disabled = ro || !s.site})) {
    const bool open = !s.open;
    a.act(open ? "Открыть археологический слот" : "Закрыть археологический слот", [&](Tx& tx) { rules::setArchSlotOpen(tx, pid, i, open); });
  }
  a.markUi("arch.slotOpen." + std::to_string(i));
}

// Режим правки: выбор места слота (места других слотов провинции недоступны).
void slotSite(App& a, const World& w, const Province& p, int i, RectF r, bool ro) {
  const ArchSlot& s = p.arch[size_t(i)];
  const Id pid = p.id;
  std::vector<ui::Option> opts;
  std::vector<Id> ids;
  int idx = -1;
  for (const ArchSite& site : w.catalogs->archSites) {
    bool taken = false;
    for (int k = 0; k < kArchSlots; k++) taken = taken || (k != i && p.arch[size_t(k)].site == site.id);
    if (site.id == s.site) idx = int(ids.size());
    ids.push_back(site.id);
    opts.push_back(ui::Option{site.name, siteIcon(w, site.id), Color(0, 0, 0, 0), taken ? std::string_view("в провинции") : std::string_view(), taken});
  }
  ui::at(r);
  if (ui::combo("site", idx, opts, {.placeholder = "Место", .disabled = ro, .popupWidth = 280, .tooltip = "Археологическое место"}) && idx >= 0 &&
      idx < int(ids.size()) && ids[size_t(idx)] != s.site) {
    const Id site = ids[size_t(idx)];
    a.act("Заменить археологическое место", [&](Tx& tx) { rules::setArchSlotSite(tx, pid, i, site); });
  }
  a.markUi("arch.slotSite." + std::to_string(i));
}

void slotGrid(App& a, const World& w, const Province& p, bool ro) {
  const float aw = ui::avail().w, gap = 10;
  const bool row4 = aw >= 4 * 150 + 3 * gap;
  const int cols = row4 ? 4 : 2;
  const float cw = std::floor(std::min(row4 ? 196.f : 230.f, (aw - gap * float(cols - 1)) / float(cols)));
  const float ch = std::clamp(std::round(cw * 0.84f), 124.f, 156.f);
  const float tH = 28, eH = g_edit ? 30 + 6 : 0;
  const float cellH = ch + 6 + tH + eH;
  const int rows = (kArchSlots + cols - 1) / cols;
  const RectF area = ui::next(float(rows) * cellH + float(rows - 1) * gap);
  for (int i = 0; i < kArchSlots; i++) {
    ui::IdScope sc(i);
    const ArchSlot& s = p.arch[size_t(i)];
    const RectF cell{area.x + float(i % cols) * (cw + gap), area.y + float(i / cols) * (cellH + gap), cw, ch};
    const ui::Interaction it = ui::interact(ui::id("##slot"), cell, ui::IfAllowOverlap);
    drawSlotLarge(w, s, i, cell, it.hovered);
    ui::hoverTip("##slottip", cell, slotTip(w, s, i, g_edit));
    a.markUi("arch.slot." + std::to_string(i), cell);
    const RectF tc{cell.x, cell.bottom() + 6, cw, tH};
    treasureCell(w, s, i, tc, g_edit);
    a.markUi("arch.treasure." + std::to_string(i), tc);
    if (g_edit) {
      slotLock(a, p, i, cell, ro);
      slotSite(a, w, p, i, RectF{cell.x, tc.bottom() + 6, cw, 30}, ro);
    }
  }
}

void hiddenSection(App& a, const World& w, const Province& p) {
  ui::Section s("Спрятанная реликвия", "eye-off");
  a.markUi("arch.hidden");
  if (!s) return;
  const Relic* r = p.hiddenRelic ? w.relic(p.hiddenRelic) : nullptr;
  if (!r) {
    ui::label("Нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    return;
  }
  w::relicRow(*r, "Спрятало: " + w.factionName(p.hiddenBy));
}

void drawArch(App& a, Id pid) {
  const World& w = prov::frameWorld(a);
  const Province* p = w.province(pid);
  if (!p || p->sea) return;
  ui::IdScope ps{i64(pid)};
  const bool ro = a.readOnly();
  headerRow(a, w, *p, ro);
  ui::spacer(2);
  slotGrid(a, w, *p, ro);
  if (g_edit) {
    ui::spacer(4);
    hiddenSection(a, w, *p);
  }
}

TabReg tab({kTabProvince, "pickaxe", "Археология", 50, SelType::Province, landOnly, drawArch});

}  // namespace
}  // namespace rg::app
