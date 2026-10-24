// Regnum — где действуют модификаторы героя (свои, класса, реликвий, талантов по рангам): войско, провинция лорда,
// государство правителя и совета; модификаторы культуры, религии, расы и формы правления; глобальные эффекты
// модификаторов провинции — владельцу; «Чужая вера»; «Влияние совета» по занятым местам; лич вместо некроманта;
// смена класса игроком; пленники и арест; инвентарь пленного; филактерия.
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

// Герой A с классом «Воитель» (единственный подкласс «Страж»): талант «Стойкость» на 3 ранга — верность войска +2 за
// ранг; свои модификаторы — довольство +3 (локальный) и доход +10 % (глобальный); герой в войске A.
struct HeroFix {
  Fix f;
  Id hero = 0, cls = 0, sub = 0, tal = 0, army = 0, mLoy = 0, mCont = 0, mInc = 0;
  HeroFix() {
    f.tx([&](Tx& tx) {
      mLoy = makeMod(tx, {{Fx::LoyaltyPerTurn, 2}});
      mCont = makeMod(tx, {{Fx::ContentmentPerTurn, 3}});
      mInc = makeMod(tx, {{Fx::IncomePct, 10}});
      for (int i = 0; i < 4; i++) tx.province(f.p[i]).owner = f.A;
      hero = createCharacter(tx, f.A, "Герой");
      tx.character(hero).modifiers.push_back(mCont);
      tx.character(hero).modifiers.push_back(mInc);
      cls = addHeroClass(tx, "Воитель");
      sub = addSubClass(tx, cls, "Страж");
      tal = addTalent(tx, sub, 0, 0, "Стойкость");
      Talent t = *tx.w().catalogs->subclass(sub)->talent(tal);
      t.ranks = 3;
      t.modifiers = {mLoy};
      setTalent(tx, t);
      setCharacterClass(tx, hero, cls);
      tx.character(hero).level = 10;
      for (int i = 0; i < 3; i++) learnTalent(tx, hero, tal);
      const Id row = addArmyRow(tx, f.A, UnitType::LightInf, "", 100, 1);
      army = createArmy(tx, ArmyKind::Army, f.A, center(0));
      setUnits(tx, army, f.A, row, 10);
      setHero(tx, army, hero, true);
    });
  }
};

double scaleOf(const World& w, Id hero, Id mod) {
  for (const HeroMod& hm : heroModifiers(w, hero))
    if (hm.modifier == mod) return hm.scale;
  return 0;
}

}  // namespace

TEST(rules_hero_effects_army_lord_ruler) {
  HeroFix x;
  Fix& f = x.f;
  // Талант на 3 ранга — 3 × 2 верности в войске героя; доход героя без должности — нигде.
  CHECK_NEAR(scaleOf(f.w(), x.hero, x.mLoy), 3, 1e-9);
  CHECK_NEAR(armyEffects(f.w(), x.army)[Fx::LoyaltyPerTurn], 6, 1e-9);
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::IncomePct], 0, 1e-9);
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::ContentmentPerTurn], 0, 1e-9);
  // Лорд провинции: её локальные эффекты.
  f.tx([&](Tx& tx) { setLord(tx, f.p[0], x.hero); });
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::ContentmentPerTurn], 3, 1e-9);
  CHECK_NEAR(provinceEffects(f.w(), f.p[1])[Fx::ContentmentPerTurn], 0, 1e-9);
  // Правитель: глобальные эффекты государству, кроме эффектов войск (верность — только своему войску).
  f.tx([&](Tx& tx) { setRuler(tx, f.A, x.hero); });
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::IncomePct], 10, 1e-9);
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::LoyaltyPerTurn], 0, 1e-9);
  // Модификатор класса складывается с собственным (вес 2).
  f.tx([&](Tx& tx) {
    for (HeroClass& c : tx.catalogs().classes)
      if (c.id == x.cls) c.modifiers = {x.mInc};
  });
  CHECK_NEAR(scaleOf(f.w(), x.hero, x.mInc), 2, 1e-9);
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::IncomePct], 20, 1e-9);
  // Плен: герой снят со всех назначений — эффекты лорда, правителя и войска пропадают.
  f.tx([&](Tx& tx) { heroFate(tx, x.hero, Fate::Captured, f.B, 0); });
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::ContentmentPerTurn], 0, 1e-9);
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::IncomePct], 0, 1e-9);
}

TEST(rules_hero_effects_relics_and_lich) {
  HeroFix x;
  Fix& f = x.f;
  Id relic = 0, necro = 0, lich = 0;
  f.tx([&](Tx& tx) {
    relic = addRelic(tx, "Кольцо стойкости", Rarity::Rare);
    for (Relic& r : tx.catalogs().relics)
      if (r.id == relic) r.modifiers = {x.mLoy};
    giveRelic(tx, x.hero, relic);
    necro = ensureBuiltinMod(tx, schema::mod::Necromancer);
    lich = ensureBuiltinMod(tx, schema::mod::Lich);
  });
  // Реликвия инвентаря — как модификатор героя (вес 1 сверх трёх рангов таланта).
  CHECK_NEAR(scaleOf(f.w(), x.hero, x.mLoy), 4, 1e-9);
  CHECK_NEAR(armyEffects(f.w(), x.army)[Fx::LoyaltyPerTurn], 8, 1e-9);
  // Лич заменяет некроманта.
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, x.hero, necro, 0); });
  CHECK(characterHas(f.w(), x.hero, schema::mod::Necromancer));
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Character, x.hero, lich, 0); });
  CHECK(characterHas(f.w(), x.hero, schema::mod::Lich));
  CHECK(!characterHas(f.w(), x.hero, schema::mod::Necromancer));
  CHECK_NEAR(scaleOf(f.w(), x.hero, necro), 0, 1e-9);
}

TEST(rules_hero_effects_traits_province_globals_foreign_faith) {
  Fix f;
  Id cu = 0, mLocal = 0, mGlobal = 0, mProv = 0;
  const Id r1 = f.w().catalogs->religions.at(0).id, r2 = f.w().catalogs->religions.at(1).id;
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 3; i++) tx.province(f.p[i]).owner = f.A;
    mLocal = makeMod(tx, {{Fx::ContentmentPerTurn, 1}});
    mGlobal = makeMod(tx, {{Fx::IncomePct, 5}});
    mProv = makeMod(tx, {{Fx::IncomePct, 7}, {Fx::TradePct, 4}});
    cu = addCatalogItem(tx, CatalogList::Cultures, "Горная");
    for (CatalogItem& c : tx.catalogs().cultures)
      if (c.id == cu) c.modifiers = {mLocal, mGlobal};
    tx.province(f.p[1]).culture = cu;
    tx.province(f.p[2]).modifiers.push_back(mProv);
  });
  // Культура провинции — её локальные эффекты; глобальные эффекты культуры провинции не действуют.
  CHECK_NEAR(provinceEffects(f.w(), f.p[1])[Fx::ContentmentPerTurn], 1, 1e-9);
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::ContentmentPerTurn], 0, 1e-9);
  // Глобальные эффекты модификатора провинции — владельцу; локальные остаются у провинции.
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::IncomePct], 7, 1e-9);
  CHECK_NEAR(provinceEffects(f.w(), f.p[2])[Fx::TradePct], 4, 1e-9);
  // Культура государства: глобальные — ему, локальные — во всех его провинциях.
  f.tx([&](Tx& tx) { tx.faction(f.A).culture = cu; });
  CHECK_NEAR(factionEffects(f.w(), f.A)[Fx::IncomePct], 12, 1e-9);
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::ContentmentPerTurn], 1, 1e-9);
  // Культура провинции совпадает с государственной — её локальные эффекты считаются один раз.
  CHECK_NEAR(provinceEffects(f.w(), f.p[1])[Fx::ContentmentPerTurn], 1, 1e-9);
  // «Чужая вера»: религия провинции — не религия государства.
  f.tx([&](Tx& tx) {
    tx.faction(f.A).religion = r1;
    tx.province(f.p[0]).religion = r2;
    tx.province(f.p[1]).religion = r1;
  });
  bool foreign = false, same = false;
  for (const AutoMod& a : autoProvinceModifiers(f.w(), f.p[0])) foreign = foreign || a.key == schema::mod::ForeignFaith;
  for (const AutoMod& a : autoProvinceModifiers(f.w(), f.p[1])) same = same || a.key == schema::mod::ForeignFaith;
  CHECK(foreign);
  CHECK(!same);
  CHECK_NEAR(provinceEffects(f.w(), f.p[0])[Fx::ContentmentPerTurn], 0, 1e-9);   // +1 культуры государства, −1 чужой веры
}

TEST(rules_hero_effects_council_influence_occupied_seats) {
  Fix f;
  f.tx([&](Tx& tx) {
    for (int i = 0; i < 4; i++) addCouncilSeat(tx, f.A, "Советник " + std::to_string(i), createCharacter(tx, f.A, "Член " + std::to_string(i)));
    for (int i = 0; i < 6; i++) addCouncilSeat(tx, f.A, "Пустое " + std::to_string(i));
  });
  double scale = 0;
  for (const AutoMod& a : autoModifiers(f.w(), f.A))
    if (a.key == schema::mod::CouncilInfluence) scale = a.scale;
  CHECK_NEAR(scale, 4, 1e-9);   // пустые места влияния не дают
}

TEST(rules_hero_respec_captives_inventory) {
  HeroFix x;
  Fix& f = x.f;
  Id cls2 = 0;
  f.tx([&](Tx& tx) {
    cls2 = addHeroClass(tx, "Проверочный класс");
    addSubClass(tx, cls2, "Проверочный подкласс");
  });
  // Игрок: смена класса — отметка хода; повтор раньше «Ходов между сменами класса» (10) — отказ.
  f.tx([&](Tx& tx) { setCharacterClass(tx, x.hero, cls2, true); });
  CHECK_EQ(f.w().character(x.hero)->respecTurn, f.w().turn());
  CHECK_EQ(respecWait(f.w(), x.hero), 10);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setCharacterClass(tx, x.hero, x.cls, true); }); }), "через"));
  f.tx([&](Tx& tx) { setCharacterClass(tx, x.hero, x.cls); });   // администратор — без ограничения
  // Пленник: инвентарь недоступен; игрок не меняет класс; пленившее государство отпускает или казнит.
  Id relic = 0;
  f.tx([&](Tx& tx) {
    relic = addRelic(tx, "Чаша", Rarity::Rare);
    giveRelic(tx, x.hero, relic);
    heroFate(tx, x.hero, Fate::Captured, f.B, 0);
  });
  CHECK(!inventoryReachable(f.w(), x.hero, false));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { takeRelic(tx, x.hero, relic); }); }), "в плену"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setCharacterClass(tx, x.hero, cls2, true); }); }), "в плену"));
  CHECK(captivesOf(f.w(), f.B) == std::vector<Id>{x.hero});
  f.tx([&](Tx& tx) { releaseCaptive(tx, x.hero); });
  CHECK(!characterHas(f.w(), x.hero, schema::mod::Captive));
  CHECK_EQ(f.w().character(x.hero)->captor, Id(0));
  CHECK(inventoryReachable(f.w(), x.hero, true));
  // Арест администратором и казнь.
  f.tx([&](Tx& tx) { arrestCharacter(tx, x.hero, f.B); });
  CHECK(characterHas(f.w(), x.hero, schema::mod::Arrested));
  CHECK_EQ(f.w().character(x.hero)->jailer, f.B);
  const double rel = f.w().relation(f.A, f.B).v;
  f.tx([&](Tx& tx) { executeCaptive(tx, x.hero, f.p[0]); });
  CHECK(characterHas(f.w(), x.hero, schema::mod::Dead));
  CHECK(!characterHas(f.w(), x.hero, schema::mod::Arrested));
  CHECK_EQ(f.w().character(x.hero)->burial, f.p[0]);
  CHECK_NEAR(f.w().relation(f.A, f.B).v, rel - 20, 1e-9);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { giveRelic(tx, x.hero, relic); }); }), "мёртв"));
}

TEST(rules_hero_lich_phylactery_flag_and_nature) {
  HeroFix x;
  Fix& f = x.f;
  Id relic = 0;
  f.tx([&](Tx& tx) {
    addModifier(tx, ModTarget::Character, x.hero, ensureBuiltinMod(tx, schema::mod::Living), 0);
    addModifier(tx, ModTarget::Character, x.hero, ensureBuiltinMod(tx, schema::mod::Necromancer), 0);
    relic = addRelic(tx, "Череп", Rarity::Epic);
    giveRelic(tx, x.hero, relic);
    tx.faction(f.A).res[ensureResource(tx, schema::kResCorpses)] += schema::kLichCorpses;
    tx.faction(f.A).ess[deathEssence(tx.w())] = schema::kLichDeathEssence;
  });
  CHECK(lichProblems(f.w(), x.hero, relic).empty());
  f.tx([&](Tx& tx) { ascendLich(tx, x.hero, relic); });
  CHECK(characterHas(f.w(), x.hero, schema::mod::Lich));
  CHECK(characterHas(f.w(), x.hero, schema::mod::Undead));
  CHECK(!characterHas(f.w(), x.hero, schema::mod::Living));
  // Отдельная реликвия-филактерия: отметка, уникальна, не продаётся.
  const Relic* ph = nullptr;
  for (const Relic& r : f.w().catalogs->relics)
    if (r.phylactery) ph = &r;
  CHECK(ph != nullptr);
  if (ph) {
    CHECK(ph->unique());
    CHECK(isPhylactery(*ph));
    CHECK(ph->id != relic);
  }
}
