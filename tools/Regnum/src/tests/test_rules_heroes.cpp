// Regnum — правила героев: модификаторы героя (природа одна, несовместимые, «Мертв» снимает назначения), назначения
// только своих героев, судьба героя (плен, обмен пленными, гибель, воскрешение), недоступные герои.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

TEST(rules_heroes_modifier_rules) {
  Fix f;
  Id c = 0, army = 0, row = 0;
  f.tx([&](Tx& tx) {
    c = createCharacter(tx, f.A, "Арн");
    row = addArmyRow(tx, f.A, UnitType::LightInf, "", 100, 1);
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, army, f.A, row, 10);
    setHero(tx, army, c, true);
    setRuler(tx, f.A, c);
    tx.province(f.p[0]).owner = f.A;
    setLord(tx, f.p[0], c);
  });
  CHECK(hasModKey(f.w(), f.w().character(c)->modifiers, schema::mod::Living));   // природа по виду государства
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, ensureBuiltinMod(tx, schema::mod::Undead)); });
  CHECK(!hasModKey(f.w(), f.w().character(c)->modifiers, schema::mod::Living));   // природа — одна
  CHECK(hasModKey(f.w(), f.w().character(c)->modifiers, schema::mod::Undead));
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, ensureBuiltinMod(tx, schema::mod::Loyalist)); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, ensureBuiltinMod(tx, schema::mod::Discontent)); }); }),
            "нельзя назначить вместе"));
  // «Мертв» снимает со всех назначений.
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, c, ensureBuiltinMod(tx, schema::mod::Dead)); });
  CHECK_EQ(f.w().faction(f.A)->ruler, Id(0));
  CHECK_EQ(f.w().province(f.p[0])->lord, Id(0));
  CHECK(f.w().army(army)->groups[0].heroes.empty());
  CHECK(!heroAvailable(f.w(), c));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRuler(tx, f.A, c); }); }), "мёртв"));
}

TEST(rules_heroes_assignment_only_own) {
  Fix f;
  Id mine = 0, other = 0, seat = 0;
  f.tx([&](Tx& tx) {
    mine = createCharacter(tx, f.A, "Свой");
    other = createCharacter(tx, f.B, "Чужой");
    CouncilSeat s;
    s.id = tx.nextId(Seq::Council);
    s.position = "Канцлер";
    tx.faction(f.A).council.push_back(s);
    seat = s.id;
    tx.province(f.p[0]).owner = f.A;
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setCouncilMember(tx, f.A, seat, other); }); }), "только героев"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setLord(tx, f.p[0], other); }); }), "только героев"));
  f.tx([&](Tx& tx) { setCouncilMember(tx, f.A, seat, mine); });
  CHECK_EQ(f.w().faction(f.A)->council[0].character, mine);
  CHECK_EQ(councilAssigned(f.w(), f.A), 1);
}

TEST(rules_heroes_fate_and_resurrect) {
  Fix f;
  Id c = 0;
  f.tx([&](Tx& tx) { c = createCharacter(tx, f.A, "Лорд"); });
  f.tx([&](Tx& tx) { heroFate(tx, c, Fate::Captured, f.B, 0); });
  CHECK(characterHas(f.w(), c, schema::mod::Captive));
  CHECK_EQ(f.w().character(c)->captor, f.B);
  CHECK_EQ(captivesOf(f.w(), f.B).size(), size_t(1));
  // Обмен пленными в переговорах: B передаёт героя A — он свободен.
  Deal d;
  d.a = f.A;
  d.b = f.B;
  d.items.push_back(DealItem{DealSide::B, kGold, 0, DealMode::Once, 1, 0, DealItemKind::Hero, c});
  f.tx([&](Tx& tx) { concludeDeal(tx, d); });
  CHECK(!characterHas(f.w(), c, schema::mod::Captive));
  CHECK_EQ(f.w().character(c)->captor, Id(0));
  f.tx([&](Tx& tx) { heroFate(tx, c, Fate::Killed, 0, f.p[3]); });
  CHECK(characterHas(f.w(), c, schema::mod::Dead));
  CHECK_EQ(f.w().character(c)->burial, f.p[3]);
  f.tx([&](Tx& tx) { resurrect(tx, c, schema::mod::Undead, f.B); });
  CHECK(!characterHas(f.w(), c, schema::mod::Dead));
  CHECK(characterHas(f.w(), c, schema::mod::Undead));
  CHECK(!characterHas(f.w(), c, schema::mod::Living));
  CHECK_EQ(f.w().character(c)->faction, f.B);
  CHECK_EQ(f.w().character(c)->burial, Id(0));
}

TEST(rules_heroes_unavailable_not_assigned_or_paid) {
  Fix f;
  Id row = 0, army = 0, c = 0;
  f.tx([&](Tx& tx) {
    row = addArmyRow(tx, f.A, UnitType::LightInf, "", 10, 0);
    army = createArmy(tx, ArmyKind::Army, f.A, center(0));
    setUnits(tx, army, f.A, row, 10);
    c = createCharacter(tx, f.A, "Сэр");
    tx.character(c).hero = true;
    tx.character(c).upkeep = 2.5;
  });
  CHECK_NEAR(f.fc(f.A).expSpecialists, 2.5, 1e-9);
  f.tx([&](Tx& tx) { heroFate(tx, c, Fate::Captured, f.B, 0); });
  CHECK_NEAR(f.fc(f.A).expSpecialists, 0, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHero(tx, army, c, true); }); }), "в плену"));
  f.tx([&](Tx& tx) { heroFate(tx, c, Fate::Killed, 0, f.p[0]); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setHero(tx, army, c, true); }); }), "мёртв"));
  CHECK_NEAR(f.fc(f.A).expSpecialists, 0, 1e-9);
}
