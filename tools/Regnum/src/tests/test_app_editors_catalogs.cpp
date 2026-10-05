// Сценарии справочника «Должности» (ТЗ «Общие доработки», п.4–5): у должности — «Модификатор должности» и
// «Модификатор отсутствия должности» (глобальные модификаторы). Занятая должность даёт государству первый,
// пустующая — второй; модификаторы войск в списках не предлагаются.
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;

namespace {

bool hasAuto(const World& w, Id state, Id mod) {
  for (const rules::AutoMod& am : rules::autoModifiers(w, state))
    if (am.modifier == mod) return true;
  return false;
}

}  // namespace

TEST(app_editors_positions_modifiers) {
  Harness h("editors_positions");
  h.demo();
  h.dropToasts();
  // Глобальный «Мудрый канцлер» (+10 % дохода), «Пустой трон» (везде, −5 % дохода) и модификатор войск.
  Id wise = 0, vacant = 0, drill = 0;
  CHECK(h->act("Модификаторы", [&](Tx& tx) {
    wise = rules::createModifier(tx, "Мудрый канцлер");
    Modifier& a = tx.modifier(wise);
    a.kind = ModKind::Faction;
    a.fxMask = 1u << int(Fx::IncomePct);
    a.fx[size_t(Fx::IncomePct)] = 10;
    vacant = rules::createModifier(tx, "Пустой трон");
    Modifier& b = tx.modifier(vacant);
    b.fxMask = 1u << int(Fx::IncomePct);
    b.fx[size_t(Fx::IncomePct)] = -5;
    drill = rules::createModifier(tx, "Мудрая муштра");
    Modifier& c = tx.modifier(drill);
    c.kind = ModKind::Army;
    c.fxMask = 1u << int(Fx::LoyaltyPerTurn);
    c.fx[size_t(Fx::LoyaltyPerTurn)] = 3;
  }));
  const CatalogItem pos = h->world().catalogs->positions.front();
  // Государство, где должность занята, и государство, где её нет.
  Id taken = 0, empty = 0, seat = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!f.isState()) return;
    bool has = false;
    for (const CouncilSeat& s : f.council)
      if (s.position == pos.name && s.character) {
        has = true;
        if (!taken) seat = s.id;
      }
    if (has && !taken) taken = f.id;
    if (!has && !empty) empty = f.id;
  });
  CHECK(taken != 0 && empty != 0);
  // Справочник «Должности»: первая должность выбрана, в карточке — оба поля.
  h->openEditor("catalogs", 6);
  quick(h);
  CHECK(reveal(h, "catalogs.position.mods"));
  CHECK(pickInCombo(h, "catalogs.position.mods", "Мудр"));
  {
    const CatalogItem* p = Catalogs::find(h->world().catalogs->positions, pos.id);
    CHECK(p && p->modifiers.size() == 1 && p->modifiers[0] == wise);   // «Мудрая муштра» (войска) не предлагается
  }
  CHECK(pickInCombo(h, "catalogs.position.vacant", "Пустой"));
  {
    const CatalogItem* p = Catalogs::find(h->world().catalogs->positions, pos.id);
    CHECK(p && p->vacantModifiers.size() == 1 && p->vacantModifiers[0] == vacant);
  }
  // Занятая должность — «Мудрый канцлер», пустующая — «Пустой трон».
  CHECK(hasAuto(h->world(), taken, wise));
  CHECK(!hasAuto(h->world(), taken, vacant));
  CHECK(hasAuto(h->world(), empty, vacant));
  CHECK(!hasAuto(h->world(), empty, wise));
  const double inc0 = rules::factionEffects(h->world(), taken)[Fx::IncomePct];
  shotClean(h, "editors_positions_card");
  // Место освободилось — модификатор отсутствия должности.
  CHECK(h->act("Совет", [&](Tx& tx) { rules::setCouncilMember(tx, taken, seat, 0); }));
  bool still = false;
  for (const CouncilSeat& s : h->world().faction(taken)->council) still = still || (s.position == pos.name && s.character);
  if (!still) {
    CHECK(!hasAuto(h->world(), taken, wise));
    CHECK(hasAuto(h->world(), taken, vacant));
    CHECK_NEAR(rules::factionEffects(h->world(), taken)[Fx::IncomePct], inc0 - 15, 1e-9);
  }
  // Убрать модификатор должности крестиком фишки; Ctrl+Z возвращает.
  CHECK(reveal(h, "catalogs.position.mods.chip.0"));
  {
    const RectF* r = h->uiRect("catalogs.position.mods.chip.0");
    CHECK(r != nullptr);
    if (r) h.click(r->right() - 9, r->cy());
  }
  quick(h);
  CHECK(Catalogs::find(h->world().catalogs->positions, pos.id)->modifiers.empty());
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK_EQ(Catalogs::find(h->world().catalogs->positions, pos.id)->modifiers.size(), size_t(1));
}
