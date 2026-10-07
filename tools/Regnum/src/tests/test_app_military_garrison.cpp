// Сценарии вкладки «Гарнизон» (ТЗ 1.a.vi; «Доработки», п.1): несколько строк отрядов разных типов из резерва
// владельца, герои владельца в гарнизоне (портрет в круге, занятый герой не выбирается для войска), верность
// гарнизона −100…100 % и мятеж при отрицательной верности — мятежники штурмуют оставшийся гарнизон; полное восстание
// гарнизона — захват; подсказка с полным названием отряда в выпадающем списке назначения.
#include "tests/test_app_war_util.h"
#include "ui/ui_internal.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Провинция государства без войск, гарнизона и оккупации, где у точки подписи можно поставить войско.
Id calmProvince(Harness& h, Id state) {
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

void openGarrison(Harness& h, Id pid) {
  h->ui.tabOf[app::SelType::Province] = "province.garrison";
  h->select(app::SelType::Province, pid);
  h.settle();
  h.dropToasts();
  h.settle();
}

i64 garrisonOf(const World& w, Id pid) {
  i64 n = 0;
  if (const Province* p = w.province(pid))
    for (const GarrisonEntry& g : p->garrison) n += g.count;
  return n;
}

std::string tipAt(Harness& h, float x, float y) {
  h.move(2, 2);
  h.move(x, y);
  hl::advance(0.5);
  h.frames(4);
  const ui::in::Ctx& c = ui::in::C();
  return c.tipRequested ? c.tipText : std::string();
}

}  // namespace

// Несколько строк отрядов (из пустого состояния, кнопкой в заголовке раздела и под таблицей), герои (добавить —
// занят для войска — убрать), верность гарнизона, подсказка обрезанного названия в списке назначения.
TEST(app_military_garrison_rows_heroes_loyalty) {
  Harness h("military_garrison_full", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel);
  CHECK(pid != 0);
  if (!pid) return;
  // Длинное название строки — в списке «Назначить из резерва» (поле узкое).
  const Faction* f = h->world().faction(hel);
  CHECK(f->army.size() >= 3);
  const std::string longName = "Средняя пехота пограничных застав северного побережья";
  const Id inf = f->army[0].id;
  CHECK(h->act("Название и резерв", [&](Tx& tx) {
    for (ArmyRow& r : tx.faction(hel).army) {
      if (r.id == inf) r.name = longName;
      r.total += 600;   // свободный резерв каждой строки
    }
  }));
  openGarrison(h, pid);
  CHECK(h.shot("military_garrison_empty"));
  // Пустое состояние: «Назначить из резерва» (кнопка внизу пустого состояния) — первая строка.
  const RectF* empty = h->uiRect("garrison.add");
  CHECK(empty != nullptr);
  if (empty) {
    const RectF r = *empty;
    h.click(r.cx(), r.bottom() - 31);
    h.settle();
  }
  if (const RectF* row = h->uiRect("garrison.add.row")) {
    const RectF r = *row;
    CHECK_MSG(tipAt(h, r.cx(), r.cy()) == longName, "подсказка выбранного пункта");
    h.dropToasts();
    h.settle();
    CHECK(h.shot("military_garrison_add_long"));
  }
  CHECK(enterNumber(h, "garrison.add.count", 300));
  CHECK(h.clickUi("garrison.add.ok"));
  h.settle();
  CHECK_EQ(h->world().province(pid)->garrison.size(), size_t(1));
  // Ещё строка — кнопкой «+» в заголовке раздела «Отряды».
  const RectF* plus = h->uiRect("garrison.units.add");
  CHECK(plus != nullptr);
  if (plus) {
    const RectF r = *plus;
    h.click(r.right() - 13, r.cy());
    h.settle();
  }
  CHECK(enterNumber(h, "garrison.add.count", 200));
  CHECK(h.clickUi("garrison.add.ok"));
  h.settle();
  CHECK_EQ(h->world().province(pid)->garrison.size(), size_t(2));
  // И третья — кнопкой под таблицей (строки разных типов).
  CHECK(reveal(h, "garrison.add"));
  CHECK(h.clickUi("garrison.add"));
  h.settle();
  CHECK(enterNumber(h, "garrison.add.count", 100));
  CHECK(h.clickUi("garrison.add.ok"));
  h.settle();
  const Province* p = h->world().province(pid);
  CHECK_EQ(p->garrison.size(), size_t(3));
  CHECK_EQ(garrisonOf(h->world(), pid), 600);
  std::vector<UnitType> types;
  for (const GarrisonEntry& g : p->garrison) types.push_back(h->world().faction(hel)->armyRow(g.row)->type);
  CHECK(types[0] != types[1] || types[1] != types[2]);
  // Строка — обратно в резерв крестиком.
  const Id last = p->garrison.back().row;
  CHECK(reveal(h, "garrison.row." + std::to_string(last) + ".remove"));
  CHECK(h.clickUi("garrison.row." + std::to_string(last) + ".remove"));
  h.settle();
  CHECK_EQ(h->world().province(pid)->garrison.size(), size_t(2));
  h.key(Key::Z, ctrl());
  CHECK_EQ(h->world().province(pid)->garrison.size(), size_t(3));

  // Герой владельца в гарнизоне: выбор из доступных героев; в войско его уже не назначить.
  const Id hero = freeHero(h, hel);
  CHECK(hero != 0);
  CHECK(reveal(h, "garrison.addhero"));
  CHECK(h.clickUi("garrison.addhero"));
  h.type(h->world().characterName(hero));
  h.key(Key::Enter);
  h.settle();
  const std::vector<Id>& gh = h->world().province(pid)->garrisonHeroes;
  CHECK(std::find(gh.begin(), gh.end(), hero) != gh.end());
  CHECK_EQ(rules::heroGarrison(h->world(), hero), pid);
  CHECK(app::mil::heroBusy(h->world(), hero).find("гарнизон") == 0);
  const Id army = armyOf(h->world(), hel, ArmyKind::Army);
  CHECK(!h->act("Герой из гарнизона в войско", [&](Tx& tx) { rules::setHero(tx, army, hero, true); }));
  h.dropToasts();
  CHECK(reveal(h, "garrison.hero." + std::to_string(hero)));
  // Верность гарнизона: −40 % — кнопка «Мятеж»; изменение за ход — рядом.
  CHECK(reveal(h, "garrison.loyalty"));
  CHECK(enterNumber(h, "garrison.loyalty", -40));
  CHECK_NEAR(h->world().province(pid)->garrisonLoyalty, -40, 1e-9);
  CHECK(enterNumber(h, "garrison.loyalty", -250));   // предел −100
  CHECK_NEAR(h->world().province(pid)->garrisonLoyalty, -100, 1e-9);
  CHECK(enterNumber(h, "garrison.loyalty", -40));
  CHECK(h->uiRect("garrison.loyalty.delta") != nullptr);
  CHECK(h->uiRect("garrison.mutiny") != nullptr);
  h.dropToasts();
  h.settle();
  if (const RectF* insp = h->uiRect("inspector")) CHECK(cropShot("military_garrison_full", *insp));
  CHECK(h.shot("military_garrison_full_screen"));
  // Убрать героя.
  CHECK(reveal(h, "garrison.hero." + std::to_string(hero) + ".remove"));
  CHECK(h.clickUi("garrison.hero." + std::to_string(hero) + ".remove"));
  h.settle();
  CHECK(h->world().province(pid)->garrisonHeroes.empty());
  CHECK(app::mil::heroBusy(h->world(), hero).empty());
}

// Мятеж гарнизона −40 %: подтверждение; 40 % каждой строки — к «Мятеж (Хельдвиг)» (войско рядом), у оставшихся
// верность 0 %; мятежники в провинции без войск прежнего государства штурмуют оставшийся гарнизон.
TEST(app_military_garrison_mutiny) {
  Harness h("military_garrison_mutiny", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel);
  CHECK(pid != 0);
  if (!pid) return;
  const Id r0 = h->world().faction(hel)->army[0].id, r2 = h->world().faction(hel)->army[2].id;
  CHECK(h->act("Гарнизон", [&](Tx& tx) {
    for (ArmyRow& r : tx.faction(hel).army) r.total += 1000;
    rules::setGarrison(tx, pid, r0, 500);
    rules::setGarrison(tx, pid, r2, 300);
    rules::setGarrisonLoyalty(tx, pid, -40);
  }));
  openGarrison(h, pid);
  CHECK(reveal(h, "garrison.mutiny"));
  CHECK(h.clickUi("garrison.mutiny"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("military_garrison_mutiny_confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  Id rebel = 0;
  h->world().factions.each([&](const Faction& f) {
    if (f.rebelOf == hel) rebel = f.id;
  });
  CHECK(rebel != 0);
  CHECK(h->world().relation(rebel, hel).s == RelStatus::War);
  const Id rebelArmy = armyOf(h->world(), rebel, ArmyKind::Army);
  CHECK(rebelArmy != 0);
  CHECK_EQ(unitsOf(h->world(), rebelArmy), 320);      // 40 % от 500 и 300
  CHECK_EQ(garrisonOf(h->world(), pid), 480);
  CHECK_NEAR(h->world().province(pid)->garrisonLoyalty, 0, 1e-9);
  // Мятежники штурмуют оставшийся гарнизон (войск Хельдвига в провинции нет).
  CHECK(h->hasDialog("siege"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("military_garrison_mutiny_siege"));
  CHECK(h.clickUi("siege.retreat"));
  h.settle();
  CHECK(!h->hasDialog("siege"));
  CHECK_EQ(garrisonOf(h->world(), pid), 480);
}

// Мятеж гарнизона, когда в провинции стоит войско владельца с отрицательной верностью: оно восстаёт вместе с гарнизоном
// (по своей верности), мятежники нападают на верную часть — окно битвы.
TEST(app_military_garrison_mutiny_with_army) {
  Harness h("military_garrison_mutiny_army", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel);
  CHECK(pid != 0);
  if (!pid) return;
  const Id army = spawnArmy(h, hel, geo::faces(h->world())->shape(pid)->label, 1000);
  CHECK(army != 0);
  CHECK_EQ(app::mil::provinceUnder(h->world(), h->world().army(army)->pos), pid);
  const Id r0 = h->world().faction(hel)->army[0].id;
  CHECK(h->act("Гарнизон и верность", [&](Tx& tx) {
    for (ArmyRow& r : tx.faction(hel).army) r.total += 1000;
    rules::setGarrison(tx, pid, r0, 400);
    rules::setGarrisonLoyalty(tx, pid, -50);
    rules::setArmyLoyalty(tx, army, -20);
  }));
  openGarrison(h, pid);
  app::flow::startGarrisonMutiny(h.a(), pid);
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  Id rebel = 0;
  h->world().factions.each([&](const Faction& f) {
    if (f.rebelOf == hel) rebel = f.id;
  });
  const Id rebelArmy = armyOf(h->world(), rebel, ArmyKind::Army);
  CHECK(rebelArmy != 0);
  CHECK_EQ(unitsOf(h->world(), rebelArmy), 200 + 200);   // половина гарнизона и 20 % войска
  CHECK_EQ(unitsOf(h->world(), army), 800);
  CHECK(h->hasDialog("battle"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("military_garrison_mutiny_battle"));
  CHECK(h.clickUi("battle.retreat"));
  h.settle();
  CHECK(!h->hasDialog("battle"));
}
