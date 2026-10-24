// Режимы карты «Дипломатия» и «Чума и пустоши» (аудит 2026-10-23, 8.1 и раздел 11): заливка провинций по отношению
// владельца к государству в фокусе (своё, союз, война, сюзерен, вассал, прочие) и по чуме, иммунитету, карантину
// (штриховка), пустоши, осквернению и соседству с чумой; цвета — как в легенде режима.
#include "map/map_internal.h"
#include "rules/rules.h"
#include "tests/test_map_view_util.h"
#include "tests/test_world_util.h"

using namespace rg;
using namespace rg::mvtest;
using map::detail::TileStyle;
using schema::MapMode;

namespace {

// Первая сухопутная провинция государства (0 — нет).
Id landOf(const World& w, Id state) {
  Id r = 0;
  w.provinces.each([&](const Province& p) {
    if (!r && !p.sea && p.owner == state) r = p.id;
  });
  return r;
}

Color legendColor(const World& w, MapMode m, size_t i) {
  const auto items = map::detail::legendFor(w, m, map::art::Style{});
  return i < items.size() ? items[i].color.withA(255) : Color(0, 0, 0, 0);
}

}  // namespace

TEST(map_style_modes_diplomacy) {
  World w = demo();
  std::vector<Id> states;
  w.factions.each([&](const Faction& f) {
    if (f.isState() && landOf(w, f.id)) states.push_back(f.id);
  });
  CHECK(states.size() >= 5);
  if (states.size() < 5) return;
  const Id focus = states[0], enemy = states[1], ally = states[2], vassal = states[3], other = states[4];
  {
    Tx tx(w);
    for (Id s : states) {
      if (s != focus) rules::setRelation(tx, focus, s, 0, RelStatus::Neutral);
      if (tx.w().faction(s)->suzerain == focus) rules::setSuzerain(tx, s, 0);
    }
    rules::setSuzerain(tx, focus, 0);
    rules::setRelation(tx, focus, enemy, -80, RelStatus::War);
    rules::setRelation(tx, focus, ally, 70, RelStatus::Alliance);
    rules::setSuzerain(tx, vassal, focus);
    w = std::move(tx).finish();
  }
  TileStyle st;
  st.mode = MapMode::Diplomacy;
  st.focus = focus;
  auto L = map::detail::computeLooks(w, st);
  auto fill = [&](Id state) { return L->prov.at(landOf(w, state)).fill; };
  CHECK(fill(focus) == legendColor(w, MapMode::Diplomacy, 0));
  CHECK(fill(ally) == legendColor(w, MapMode::Diplomacy, 1));
  CHECK(fill(enemy) == legendColor(w, MapMode::Diplomacy, 2));
  CHECK(fill(vassal) == legendColor(w, MapMode::Diplomacy, 4));
  CHECK(fill(other) == legendColor(w, MapMode::Diplomacy, 5));
  // Фокус вассала: его сюзерен — «Сюзерен»; фокус — часть стиля тайлов.
  TileStyle sv = st;
  sv.focus = vassal;
  auto LV = map::detail::computeLooks(w, sv);
  CHECK(LV->prov.at(landOf(w, focus)).fill == legendColor(w, MapMode::Diplomacy, 3));
  CHECK(sv.key() != st.key());
}

TEST(map_style_modes_plague) {
  World w = demo();
  Id sick = 0, quarantined = 0;
  w.provinces.each([&](const Province& p) {
    if (!sick && !p.sea && p.owner && rules::canPlague(w, p.id)) sick = p.id;
  });
  CHECK(sick != 0);
  {
    Tx tx(w);
    CHECK(rules::infectPlague(tx, sick));
    w = std::move(tx).finish();
  }
  // Соседи чумной провинции: один — на карантине, остальные — «Соседство с чумой».
  std::vector<Id> near;
  auto fs = geo::faces(w);
  for (const auto& [x, y] : fs->neighbors()) {
    const Id o = x == sick ? y : y == sick ? x : 0;
    const Province* p = o ? w.province(o) : nullptr;
    if (p && !p->sea && std::find(near.begin(), near.end(), o) == near.end()) near.push_back(o);
  }
  CHECK(near.size() >= 2);
  if (near.size() < 2) return;
  for (Id n : near)
    if (!quarantined && w.province(n)->owner) quarantined = n;
  CHECK(quarantined != 0);
  if (!quarantined) return;
  const Id risk = near[0] == quarantined ? near[1] : near[0];
  {
    Tx tx(w);
    rules::setQuarantine(tx, quarantined, true);
    w = std::move(tx).finish();
  }
  TileStyle st;
  st.mode = MapMode::Plague;
  auto L = map::detail::computeLooks(w, st);
  CHECK(L->hatch);
  CHECK(L->prov.at(sick).fill == legendColor(w, MapMode::Plague, 0));
  CHECK(L->prov.at(risk).fill == legendColor(w, MapMode::Plague, 1));
  CHECK(L->prov.at(quarantined).fill == legendColor(w, MapMode::Plague, 3));
  CHECK(L->prov.at(quarantined).hatch.a > 0);
  CHECK(L->prov.at(sick).hatch.a == 0);
}
