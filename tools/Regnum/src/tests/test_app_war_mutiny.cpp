// Сценарии верности и мятежа (ТЗ «Общие доработки», п.12; «Модификаторы»; «Механика мятежа», п.1–3): верность
// войска −100…100 % и изменение за ход, модификаторы войска со сроком, «Мятеж» при отрицательной верности — битва
// мятежников с верными, «Судьба героев», захват провинции мятежниками; мятеж целиком; переход неверных войск к
// мятежникам перед боем.
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Провинция государства без войск, гарнизона и оккупации, где у точки подписи можно поставить войско.
Id quietProvince(Harness& h, Id state) {
  const World& w = h->world();
  auto fs = geo::faces(w);
  Id found = 0;
  w.provinces.each([&](const Province& p) {
    if (found || p.sea || p.owner != state || p.occupied || !p.garrison.empty()) return;
    const geo::ProvinceShape* sh = fs->shape(p.id);
    if (!sh || !rules::validPosition(w, ArmyKind::Army, sh->label)) return;
    bool busy = false;
    w.armies.each([&](const Army& a) { busy = busy || app::mil::provinceUnder(w, a.pos) == p.id; });
    if (!busy) found = p.id;
  });
  return found;
}

struct MutinySetup {
  Id hel = 0, province = 0, army = 0, hero = 0;
};

std::optional<MutinySetup> prepare(Harness& h, i64 units) {
  MutinySetup s;
  s.hel = factionByName(h->world(), "Хельдвиг");
  s.province = quietProvince(h, s.hel);
  CHECK_MSG(s.province != 0, "нет провинции Хельдвига без войск и гарнизона");
  if (!s.province) return std::nullopt;
  s.hero = freeHero(h, s.hel);
  CHECK(s.hero != 0);
  s.army = spawnArmy(h, s.hel, geo::faces(h->world())->shape(s.province)->label, units, s.hero);
  CHECK(s.army != 0);
  if (!s.army) return std::nullopt;
  h->ui.tabOf[app::SelType::Army] = "army.units";
  h->select(app::SelType::Army, s.army);
  h.settle();
  showAt(h, h->world().army(s.army)->pos, 0.6);
  h.dropToasts();
  return s;
}

}  // namespace

// Верность: поле −100…100 %, двуполярный индикатор, изменение за ход с источниками; модификаторы войска — добавить
// встроенный «Патриотизм» (+5 % за ход), срок в ходах, снять.
TEST(app_war_loyalty_and_modifiers) {
  Harness h("war_loyalty", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  auto s = prepare(h, 800);
  if (!s) return;
  CHECK(reveal(h, "army.loyalty"));
  CHECK(enterNumber(h, "army.loyalty", -40));
  CHECK_NEAR(h->world().army(s->army)->loyalty, -40, 1e-9);
  CHECK(enterNumber(h, "army.loyalty", -250));   // предел −100
  CHECK_NEAR(h->world().army(s->army)->loyalty, -100, 1e-9);
  CHECK(enterNumber(h, "army.loyalty", -40));
  CHECK(h->uiRect("army.mutiny") != nullptr);
  // «Патриотизм» из встроенных (в мире записи ещё нет — создаётся при добавлении).
  CHECK_EQ(rules::loyaltyDelta(h->world(), s->army), 0.0);
  CHECK(reveal(h, "army.mods.add"));
  CHECK(h.clickUi("army.mods.add"));
  h.type(rules::builtinMod(h->world(), schema::mod::Patriotism)->name);
  h.key(Key::Enter);
  h.settle();
  CHECK(rules::armyHas(h->world(), s->army, schema::mod::Patriotism));
  CHECK_NEAR(rules::loyaltyDelta(h->world(), s->army), 5, 1e-9);
  const Id pat = rules::builtinModId(h->world(), schema::mod::Patriotism);
  CHECK(pat != 0);
  // Срок: щелчок по фишке — 3 хода.
  CHECK(reveal(h, "army.mods.chip.0"));
  CHECK(h.clickUi("army.mods.chip.0"));
  h.settle();
  CHECK(enterNumber(h, "army.mods.term", 3));
  h.key(Key::Escape);
  h.settle();
  auto it = h->world().army(s->army)->modTurns.find(pat);
  CHECK(it != h->world().army(s->army)->modTurns.end() && it->second == 3);
  // Подсказка источников изменения верности.
  if (const RectF* d = h->uiRect("army.loyalty.delta")) {
    h.move(d->cx(), d->cy());
    hl::advance(0.5);
    h.frames(4);
    CHECK(h.shot("war_loyalty_sources"));
  }
  h.move(10, 500);
  // Конец хода: верность +5, срок 2.
  CHECK(h->endTurnNow());
  h.dropToasts();
  CHECK_NEAR(h->world().army(s->army)->loyalty, -35, 1e-9);
  CHECK_EQ(h->world().army(s->army)->modTurns.at(pat), 2);
  // Снять модификатор крестиком фишки.
  h->select(app::SelType::Army, s->army);
  h.settle();
  CHECK(reveal(h, "army.mods.chip.0"));
  const RectF* chip = h->uiRect("army.mods.chip.0");
  CHECK(chip != nullptr);
  if (chip) h.click(chip->right() - 10, chip->cy());
  h.settle();
  CHECK(!rules::armyHas(h->world(), s->army, schema::mod::Patriotism));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_loyalty_card"));
}

// Мятеж −40 %: подтверждение, 40 % — мятежное войско «Мятеж (Хельдвиг)», верные — 0 %; битва (мятежники нападают),
// верные уничтожены — их герой в плену у мятежников; мятежники в провинции Хельдвига без его войск — захват.
TEST(app_war_mutiny_partial) {
  Harness h("war_mutiny_partial", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  auto s = prepare(h, 1000);
  if (!s) return;
  CHECK(h->act("Верность", [&](Tx& tx) { rules::setArmyLoyalty(tx, s->army, -40); }));
  h.settle();
  CHECK(reveal(h, "army.mutiny"));
  CHECK(h.clickUi("army.mutiny"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  // Мятежное государство и войско.
  Id rebel = 0;
  h->world().factions.each([&](const Faction& f) {
    if (f.rebelOf == s->hel) rebel = f.id;
  });
  CHECK(rebel != 0);
  CHECK(h->world().relation(rebel, s->hel).s == RelStatus::War);
  const Id rebelArmy = armyOf(h->world(), rebel, ArmyKind::Army);
  CHECK(rebelArmy != 0);
  CHECK_EQ(unitsOf(h->world(), rebelArmy), 400);
  CHECK_EQ(unitsOf(h->world(), s->army), 600);
  CHECK_NEAR(h->world().army(s->army)->loyalty, 0, 1e-9);
  // Битва: мятежники нападают на верных.
  CHECK(h->hasDialog("battle"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_mutiny_battle"));
  const Id row = h->world().army(s->army)->groups[0].units[0].row;
  CHECK(enterNumber(h, "battle.loss." + std::to_string(s->army) + "." + std::to_string(row), 600));
  CHECK(h.clickUi("battle.apply"));
  h.settle();
  CHECK(h->world().army(s->army) == nullptr);
  // Судьба героя верного войска: в плен к мятежникам.
  CHECK(h->hasDialog("hero.fate"));
  CHECK(h.clickUi("fate." + std::to_string(s->hero) + ".2"));
  CHECK(h.clickUi("fate.ok"));
  h.settle();
  CHECK(rules::characterHas(h->world(), s->hero, schema::mod::Captive));
  CHECK_EQ(h->world().character(s->hero)->captor, rebel);
  const std::vector<Id> captives = rules::captivesOf(h->world(), rebel);
  CHECK(std::find(captives.begin(), captives.end(), s->hero) != captives.end());
  // Мятежники в провинции Хельдвига без его войск и гарнизона — сразу захват (ТЗ «Мятеж», п.2).
  CHECK(h->hasDialog("capture"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_mutiny_capture"));
  CHECK(h.clickUi("capture.option.0"));
  CHECK(h.clickUi("capture.ok"));
  h.settle();
  const Province* p = h->world().province(s->province);
  CHECK(p->occupied && p->occupier == rebel);
}

// Мятеж −100 %: войско восстаёт целиком; герой с «Недовольством правителем» уходит к мятежникам, верному — окно
// «Судьба героя»; затем захват провинции (здесь — отказ).
TEST(app_war_mutiny_full) {
  Harness h("war_mutiny_full", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  auto s = prepare(h, 900);
  if (!s) return;
  const Id rebelHero = freeHero(h, s->hel);
  CHECK(rebelHero != 0);
  CHECK(h->act("Недовольный герой", [&](Tx& tx) {
    rules::setHero(tx, s->army, rebelHero, true);
    rules::addModifier(tx, rules::ModTarget::Character, rebelHero, rules::ensureBuiltinMod(tx, schema::mod::Discontent));
    rules::setArmyLoyalty(tx, s->army, -100);
  }));
  h.settle();
  CHECK(reveal(h, "army.mutiny"));
  CHECK(h.clickUi("army.mutiny"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->world().army(s->army) == nullptr);
  Id rebel = 0;
  h->world().factions.each([&](const Faction& f) {
    if (f.rebelOf == s->hel) rebel = f.id;
  });
  CHECK(rebel != 0);
  const Id rebelArmy = armyOf(h->world(), rebel, ArmyKind::Army);
  CHECK_EQ(unitsOf(h->world(), rebelArmy), 900);
  CHECK_EQ(h->world().character(rebelHero)->faction, rebel);
  // Верный герой — «Судьба героя».
  CHECK(!h->hasDialog("battle"));
  CHECK(h->hasDialog("hero.fate"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_mutiny_full_fate"));
  CHECK(h.clickUi("fate." + std::to_string(s->hero) + ".0"));
  CHECK(h.clickUi("fate.ok"));
  h.settle();
  CHECK(rules::heroAvailable(h->world(), s->hero));
  CHECK_EQ(h->world().character(s->hero)->faction, s->hel);
  CHECK(h->hasDialog("capture"));
  CHECK(h.clickUi("capture.cancel"));
  h.settle();
  CHECK(!h->hasDialog("capture"));
  CHECK(!h->world().province(s->province)->occupied);
}

// ТЗ «Мятеж», п.3: мятежники нападают на войско прежнего государства с отрицательной верностью — до боя неверная
// часть переходит к ним, у оставшихся верность 0 %; окно битвы — с обновлёнными силами.
TEST(app_war_mutiny_defect_before_battle) {
  Harness h("war_mutiny_defect", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  auto s = prepare(h, 1000);
  if (!s) return;
  Id rebel = 0, rebelArmy = 0;
  CHECK(h->act("Мятежники", [&](Tx& tx) {
    rebel = rules::rebelStateFor(tx, s->hel);
    const Id row = rules::addArmyRow(tx, rebel, UnitType::LightInf, "Мятежники", 500, 0);
    auto spot = rules::findFreeSpot(tx.w(), ArmyKind::Army, tx.w().army(s->army)->pos + Vec2(160, 0));
    if (!spot) fail("нет места");
    rebelArmy = rules::createArmy(tx, ArmyKind::Army, rebel, *spot);
    rules::setUnits(tx, rebelArmy, rebel, row, 500);
    rules::setArmyLoyalty(tx, s->army, -50);
  }));
  CHECK(rules::willDefect(h->world(), rebelArmy, s->army));
  app::mil::openBattle(h.a(), rebelArmy, s->army, h->world().army(rebelArmy)->pos);
  h.settle();
  CHECK_EQ(unitsOf(h->world(), s->army), 500);
  CHECK_EQ(unitsOf(h->world(), rebelArmy), 1000);
  CHECK_NEAR(h->world().army(s->army)->loyalty, 0, 1e-9);
  CHECK(h->hasDialog("battle"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_mutiny_defect_battle"));
  CHECK(h.clickUi("battle.retreat"));
  h.settle();
  CHECK(!h->hasDialog("battle"));
}

// ТЗ «Доработки», п.1: мятежники штурмуют гарнизон прежнего государства с отрицательной верностью — до штурма
// неверная часть гарнизона переходит к ним, у оставшихся верность 0 %; окно штурма — с оставшимся гарнизоном. Гарнизон
// перешёл целиком — штурма нет, сразу выбор захвата.
TEST(app_war_mutiny_garrison_defect) {
  Harness h("war_mutiny_garrison_defect", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = quietProvince(h, hel);
  CHECK(pid != 0);
  if (!pid) return;
  const Id row = h->world().faction(hel)->army[0].id;
  Id rebel = 0, rebelArmy = 0;
  CHECK(h->act("Гарнизон и мятежники", [&](Tx& tx) {
    for (ArmyRow& r : tx.faction(hel).army)
      if (r.id == row) r.total += 400;
    rules::setGarrison(tx, pid, row, 400);
    rules::setGarrisonLoyalty(tx, pid, -50);
    rebel = rules::rebelStateFor(tx, hel);
    const Id rrow = rules::addArmyRow(tx, rebel, UnitType::LightInf, "Мятежники", 500, 0);
    auto fs = geo::faces(h->world());
    auto spot = rules::findFreeSpot(tx.w(), ArmyKind::Army, fs->shape(pid)->label);
    if (!spot) fail("нет места");
    rebelArmy = rules::createArmy(tx, ArmyKind::Army, rebel, *spot);
    rules::setUnits(tx, rebelArmy, rebel, rrow, 500);
  }));
  CHECK(rules::willGarrisonDefect(h->world(), rebelArmy, pid));
  app::flow::openSiege(h.a(), rebelArmy, pid, h->world().army(rebelArmy)->pos);
  h.settle();
  i64 garrison = 0;
  for (const GarrisonEntry& g : h->world().province(pid)->garrison) garrison += g.count;
  CHECK_EQ(garrison, 200);
  CHECK_EQ(unitsOf(h->world(), rebelArmy), 700);
  CHECK_NEAR(h->world().province(pid)->garrisonLoyalty, 0, 1e-9);
  CHECK(h->hasDialog("siege"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_mutiny_garrison_defect_siege"));
  CHECK(h.clickUi("siege.retreat"));
  h.settle();
  CHECK(!h->hasDialog("siege"));
  // −100 %: гарнизон переходит целиком — сразу выбор захвата.
  CHECK(h->act("Верность гарнизона −100 %", [&](Tx& tx) { rules::setGarrisonLoyalty(tx, pid, -100); }));
  app::flow::openSiege(h.a(), rebelArmy, pid, h->world().army(rebelArmy)->pos);
  h.settle();
  CHECK(h->world().province(pid)->garrison.empty());
  CHECK_EQ(unitsOf(h->world(), rebelArmy), 900);
  CHECK(!h->hasDialog("siege"));
  CHECK(h->hasDialog("capture"));
  CHECK(h.clickUi("capture.cancel"));
  h.settle();
}
