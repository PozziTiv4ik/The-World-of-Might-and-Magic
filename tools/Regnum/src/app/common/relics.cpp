// Regnum — реликвии в интерфейсе: места за один проход, подписи, строка стопки, источники выдачи, окна выбора.
#include "app/common/relics.h"

#include <algorithm>

#include "app/app_internal.h"
#include "app/widgets.h"

namespace rg::app::relics {

using w::orName;

namespace {

const std::vector<RelicAt> kNoPlaces;

std::string heroName(const World& w, Id c) { return orName(w.characterName(c), "Без имени"); }

std::string buildingText(const World& w, const RelicPlace& p) {
  const Building* b = w.building(p.building);
  return orName(b ? b->name : std::string(), "Постройка") + " · " + orName(w.provinceName(p.id));
}

// Где лежит источник — относительно государства state (своё — короче).
std::string sourceHint(const World& w, const Source& s, Id state) {
  std::string t;
  switch (s.from.kind) {
    case RelicPlace::Free: {
      const Relic* r = w.relic(s.relic);
      t = std::string("справочник · ") + (r ? schema::rarity(r->rarity).name : "");
      break;
    }
    case RelicPlace::State: t = s.from.id == state ? std::string("инвентарь государства") : "инвентарь · " + orName(w.factionName(s.from.id)); break;
    case RelicPlace::Building: t = "хранилище · " + orName(w.provinceName(s.from.id)); break;
    case RelicPlace::Hero:
      t = "у " + heroName(w, s.from.id);
      if (s.from.owner != state && s.from.owner) t += " · " + orName(w.factionName(s.from.owner));
      break;
    case RelicPlace::Hidden: t = "тайник"; break;
  }
  if (s.count > 1) t += " · " + times(s.count);
  return t;
}

}  // namespace

std::string times(i64 n) { return "\xC3\x97\xE2\x80\xAF" + fmtInt(n); }   // «×», узкий неразрывный пробел, число

// ---------------------------------------------------------------- места
Places::Places(const World& w) {
  auto add = [&](Id relic, const RelicPlace& p, i64 n) {
    if (!relic || n <= 0) return;
    std::vector<RelicAt>& v = at_[relic];
    if (!v.empty() && v.back().place == p) v.back().count += n;   // повтор реликвии в одном списке — одна стопка
    else v.push_back(RelicAt{p, n, relic});
  };
  w.characters.each([&](const Character& c) {
    for (const RelicStack& s : c.inventory) add(s.relic, RelicPlace{RelicPlace::Hero, c.id, 0, c.faction}, s.count);
  });
  w.provinces.each([&](const Province& p) {
    if (p.hiddenRelic) add(p.hiddenRelic, RelicPlace{RelicPlace::Hidden, p.id, 0, p.owner}, 1);
    for (const ProvBuilding& pb : p.buildings)
      for (const RelicStack& s : pb.relics) add(s.relic, RelicPlace{RelicPlace::Building, p.id, pb.building, p.owner}, s.count);
  });
  w.factions.each([&](const Faction& f) {
    for (const RelicStack& s : f.relics) add(s.relic, RelicPlace{RelicPlace::State, f.id, 0, f.id}, s.count);
  });
}

const std::vector<RelicAt>& Places::of(Id relic) const {
  auto it = at_.find(relic);
  return it == at_.end() ? kNoPlaces : it->second;
}

i64 Places::total(Id relic) const {
  i64 n = 0;
  for (const RelicAt& a : of(relic)) n += a.count;
  return n;
}

PlaceView placeView(const World& w, const RelicPlace& p) {
  PlaceView v;
  switch (p.kind) {
    case RelicPlace::Hero: {
      const Character* c = w.character(p.id);
      v.text = heroName(w, p.id);
      v.icon = c && c->hero ? "hero" : "character";
      v.color = w::factionColor(w, p.owner);
      v.go = {SelType::Character, p.id};
      v.tip = "Инвентарь героя — открыть";
      break;
    }
    case RelicPlace::Building:
      v.text = buildingText(w, p);
      v.icon = "building";
      v.color = w::factionColor(w, p.owner);
      v.go = {SelType::Province, p.id};
      v.tip = "Хранилище постройки — открыть провинцию";
      break;
    case RelicPlace::State:
      v.text = orName(w.factionName(p.id));
      v.icon = "crown";
      v.color = w::factionColor(w, p.id);
      v.go = {SelType::Faction, p.id};
      v.tip = "Инвентарь государства — открыть";
      break;
    case RelicPlace::Hidden:
      v.text = "спрятана";   // место тайника не показывается
      v.icon = "eye-off";
      break;
    case RelicPlace::Free:
      v.text = "нигде";
      v.icon = "relic";
      break;
  }
  return v;
}

void placeChip(App& a, const World& w, const RelicPlace& p, i64 count) {
  const PlaceView v = placeView(w, p);
  ui::IdScope s{i64(p.kind) * 0x1000000000LL + i64(p.id) * 0x10000LL + i64(p.building)};
  ui::ChipOpt co;
  co.icon = v.icon;
  co.color = v.color;
  co.clickable = bool(v.go);
  co.tooltip = v.tip;
  if (ui::flowChip(count > 1 ? v.text + " · " + times(count) : v.text, co) != ui::ChipAction::Click || !v.go) return;
  const Selection go = v.go;
  detail::later(a, [go](App& x) {
    if (!x.ui.editor.empty()) x.closeEditor();
    x.select(go, go.type == SelType::Province);
  });
}

// ---------------------------------------------------------------- стопки
i64 pieces(const World& w, const std::vector<RelicStack>& v) {
  i64 n = 0;
  for (const RelicStack& s : v)
    if (w.relic(s.relic)) n += std::max<i64>(0, s.count);
  return n;
}

std::vector<RelicStack> sorted(const World& w, const std::vector<RelicStack>& v) {
  std::vector<RelicStack> out;
  for (const RelicStack& s : v) {
    if (!w.relic(s.relic) || s.count <= 0) continue;
    auto it = std::find_if(out.begin(), out.end(), [&](const RelicStack& x) { return x.relic == s.relic; });
    if (it != out.end()) it->count += s.count;
    else out.push_back(s);
  }
  std::stable_sort(out.begin(), out.end(), [&](const RelicStack& x, const RelicStack& y) {
    const Relic& a = *w.relic(x.relic);
    const Relic& b = *w.relic(y.relic);
    if (a.rarity != b.rarity) return rules::rarityAbove(a.rarity, b.rarity);
    return compareRu(a.name, b.name) < 0;
  });
  return out;
}

std::string kindText(const Relic& r) { return std::string(schema::rarity(r.rarity).name) + (r.unique() ? " · уникальная" : ""); }

bool stackRow(const Relic& r, i64 count, std::string_view sub, float right, RectF* rowOut) {
  const ui::Theme& th = ui::theme();
  const std::string n = count > 1 ? times(count) : std::string();
  const float pw = n.empty() ? 0.f : std::ceil(ui::measure(n, ui::Font::Strong)) + 14;
  RectF row;
  const bool clicked = w::relicRow(r, sub, right + (pw > 0 ? pw + 6 : 0), &row);
  if (rowOut) *rowOut = row;
  if (pw > 0) {
    const RectF pr{row.right() - right - pw - 2, row.cy() - 11, pw, 22};
    ui::draw::rect(pr, th.surface3, 11);
    ui::draw::rectStroke(pr, w::rarityColor(r.rarity).alpha(0.45f), 11, 1);
    ui::draw::text(n, pr, ui::Font::Strong, th.text, ui::Align::Center);
  }
  return clicked;
}

bool countField(std::string_view id, RectF r, i64& value, i64 min) {
  ui::at(r);
  i64 v = value;
  if (!ui::numberField(id, v, {.min = double(min), .max = double(schema::kMaxCount), .steppers = true, .tooltip = "Количество"})) return false;
  if (v == value) return false;
  value = v;
  return true;
}

// ---------------------------------------------------------------- источники
std::vector<Source> sources(const World& w, const Places& pl, Id state, const RelicPlace& to, Target kind, bool admin) {
  std::vector<Source> out;
  for (const Relic& r : w.catalogs->relics) {
    const std::vector<RelicAt>& at = pl.of(r.id);
    const bool here = std::any_of(at.begin(), at.end(), [&](const RelicAt& x) { return x.place == to; });
    if (r.unique() && here) continue;   // уже здесь
    const bool find = rules::isArchFind(w, r.id);
    for (const RelicAt& x : at) {
      if (x.place == to || x.place.kind == RelicPlace::Hidden) continue;
      int rank = 4;
      if (state && x.place.owner == state) rank = x.place.kind == RelicPlace::State ? 0 : x.place.kind == RelicPlace::Building ? 1 : 2;
      if (rank == 4 && (!admin || kind == Target::Store || find)) continue;   // чужое — только администратору и не находки
      out.push_back(Source{r.id, x.place, x.count, rank});
    }
    // Новая из справочника (правка мира): находки героям напрямую не даются; копируемая, которая уже у героя, —
    // полем количества.
    if (admin && rules::relicFree(w, r.id) && !(kind == Target::Hero && (find || here))) out.push_back(Source{r.id, RelicPlace{}, 0, 3});
  }
  std::stable_sort(out.begin(), out.end(), [&](const Source& x, const Source& y) {
    if (x.rank != y.rank) return x.rank < y.rank;
    const Relic& a = *w.relic(x.relic);
    const Relic& b = *w.relic(y.relic);
    if (a.rarity != b.rarity) return rules::rarityAbove(a.rarity, b.rarity);
    return compareRu(a.name, b.name) < 0;
  });
  return out;
}

int sourceCombo(std::string_view id, const World& w, Id state, const std::vector<Source>& src, std::string_view placeholder,
                std::string_view tooltip, bool disabled) {
  std::vector<std::string> labels, hints;
  labels.reserve(src.size());
  hints.reserve(src.size());
  for (const Source& s : src) {
    const Relic* r = w.relic(s.relic);
    labels.push_back(orName(r ? r->name : std::string()));
    hints.push_back(sourceHint(w, s, state));
  }
  std::vector<ui::Option> opts;
  opts.reserve(src.size());
  for (size_t i = 0; i < src.size(); i++) {
    const Relic* r = w.relic(src[i].relic);
    opts.push_back(ui::Option{labels[i], "relic", r ? w::rarityColor(r->rarity) : Color(0, 0, 0, 0), hints[i]});
  }
  int idx = -1;
  if (ui::combo(id, idx, std::span<const ui::Option>(opts),
                {.placeholder = placeholder, .search = 1, .icon = "plus", .disabled = disabled || src.empty(), .popupWidth = 420, .tooltip = tooltip}) &&
      idx >= 0 && idx < int(src.size()))
    return idx;
  return -1;
}

// ---------------------------------------------------------------- окна выбора
namespace {

// Поле «Сколько» внутри всплывающего окна: значение живёт, пока окно открыто.
i64& countState(i64 max) {
  i64& n = ui::state<i64>(ui::id("##count"));
  n = std::clamp<i64>(n, 1, std::max<i64>(1, max));
  return n;
}

void countRow(App& a, i64 max, std::string_view mark) {
  i64& n = countState(max);
  ui::Row r({ui::fr(1), ui::px(kCountW + 16)}, 30, 8);
  ui::label("Сколько", {.ink = ui::Ink::Dim, .icon = "relic"});
  ui::numberField("count", n, {.min = 1, .max = double(max), .steppers = true, .tooltip = "Сколько штук (до " + fmtInt(max) + ")"});
  n = std::clamp<i64>(n, 1, max);
  a.markUi(std::string(mark) + ".count");
}

}  // namespace

int destPopup(App& a, std::string_view id, std::string_view header, const std::vector<Dest>& dests, i64 max, i64& count, std::string_view mark) {
  if (!ui::beginPopup(id, {.side = ui::Side::Below, .width = 320, .maxHeight = 420})) return -1;
  int pick = -1;
  ui::label(header, {.font = ui::Font::Strong});
  if (max > 1) countRow(a, max, mark);
  if (dests.empty()) ui::label("Некуда", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  for (size_t i = 0; i < dests.size(); i++) {
    const Dest& d = dests[i];
    ui::IdScope s{i64(i)};
    if (ui::listItem(d.label, {.icon = d.color.a > 0 ? nullptr : d.icon, .dot = d.color, .hint = d.hint, .disabled = d.disabled}) && !d.disabled) pick = int(i);
    a.markUi(std::string(mark) + "." + std::to_string(i));
  }
  if (pick >= 0) {
    count = max > 1 ? countState(max) : 1;
    ui::closePopup();
  }
  ui::endPopup();
  return pick;
}

bool countPopup(App& a, std::string_view id, std::string_view header, std::string_view okLabel, i64 max, i64& count, std::string_view mark) {
  if (!ui::beginPopup(id, {.side = ui::Side::Below, .width = 280})) return false;
  ui::label(header, {.font = ui::Font::Strong});
  countRow(a, std::max<i64>(1, max), mark);
  bool ok = false;
  if (ui::button(okLabel, {.variant = ui::Variant::Primary, .fill = true, .isDefault = true})) {
    ok = true;
    count = countState(std::max<i64>(1, max));
    ui::closePopup();
  }
  a.markUi(std::string(mark) + ".ok");
  ui::endPopup();
  return ok;
}

}  // namespace rg::app::relics
