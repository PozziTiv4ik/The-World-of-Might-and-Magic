// Сценарии флота (ТЗ «Доработки №3», п.3, 6, 8; «Доработки №4», п.9): посадка войска на флот перетаскиванием,
// вкладка «Войско на борту», высадка инструментом карты, обмен отрядами (окно из встречи и у флота с войском на
// борту), союзное войско под управлением цели и «Вернуть войско», «Поставить флот» у верфи и «Свободное
// редактирование флотов», ключевой ресурс морского чудовища. Снимки — .wmma/regnum-tests/app_naval_*.png.
#include "tests/test_app_military_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Перетаскивание по шагам; snapshot — снимок, пока кнопка ещё зажата.
void dragTo(Harness& h, gfx::Pt from, gfx::Pt to, const std::string& snapshot = {}) {
  hl::mouseMove(from.x, from.y);
  h.step();
  hl::mouseDown(from.x, from.y, platform::MouseLeft);
  h.step();
  for (int i = 1; i <= 10; i++) {
    hl::mouseMove(from.x + (to.x - from.x) * float(i) / 10, from.y + (to.y - from.y) * float(i) / 10);
    h.step();
  }
  if (!snapshot.empty()) {
    h.dropToasts();
    h.settle();
    CHECK(h.shot(snapshot));
  }
  hl::mouseUp(to.x, to.y, platform::MouseLeft);
  h.step();
  hl::advance(0.6);
}

Vec2 labelOf(const World& w, Id province) {
  auto fs = geo::faces(w);
  const geo::ProvinceShape* sh = fs ? fs->shape(province) : nullptr;
  return sh ? sh->label : Vec2{};
}

// Строка армии государства со свободным резервом не меньше n (обычный отряд с людьми).
Id armyRowWithReserve(const World& w, Id state, i64 n) {
  const Faction* f = w.faction(state);
  if (!f) return 0;
  for (const ArmyRow& r : f->army)
    if (!r.special && !r.merc && app::mil::reserveOf(w, state, r.id, false) >= n) return r.id;
  return 0;
}

// Берег для посадки: приморская провинция государства, флот у её берега (rules::fleetSpot), войско в ней.
struct Shore {
  Id province = 0, fleet = 0, army = 0;
  Vec2 armyPos, fleetPos;
};

std::optional<Shore> makeShore(Harness& h, Id state, i64 units) {
  const World& w = h->world();
  Id fleet = armyOf(w, state, ArmyKind::Fleet);
  const Id row = armyRowWithReserve(w, state, units);
  if (!fleet || !row) return std::nullopt;
  std::vector<Id> provs;
  w.provinces.each([&](const Province& p) {
    if (!p.sea && p.owner == state && rules::isCoastal(w, p.id)) provs.push_back(p.id);
  });
  for (Id pid : provs) {
    auto sea = rules::fleetSpot(w, pid);
    if (!sea) continue;
    const Vec2 lab = labelOf(w, pid);
    auto land = rules::findFreeSpot(w, ArmyKind::Army, lab);
    if (!land || app::mil::provinceUnder(w, *land) != pid || dist(*land, *sea) > 700 || dist(*land, *sea) < 120) continue;
    Shore s;
    s.province = pid;
    s.fleet = fleet;
    bool ok = h->act("Берег для теста", [&](Tx& tx) {
      rules::moveArmy(tx, fleet, *sea);
      s.army = rules::createArmy(tx, ArmyKind::Army, state, *land);
      rules::setUnits(tx, s.army, state, row, units);
      if (!rules::canEmbark(tx.w(), s.army, fleet)) fail("нельзя посадить");
    });
    if (!ok) continue;
    s.armyPos = *land;
    s.fleetPos = *sea;
    return s;
  }
  return std::nullopt;
}

// Показать обе точки в свободной части карты.
void showBoth(Harness& h, Vec2 a, Vec2 b, Id select = 0) {
  if (select) {
    h->select(app::SelType::Army, select);
    h.settle();
  }
  const double span = std::max(1.0, dist(a, b) + 260);
  showAt(h, (a + b) * 0.5, std::clamp(0.55 * h->mapArea().h / span, 0.25, 1.2));
  h.dropToasts();
}

}  // namespace

// Посадка перетаскиванием (окно с вместимостью), вкладка «Войско на борту», фигурка войска на карте не рисуется,
// высадка инструментом (подсвеченные провинции, неверное место — причина), отмена Ctrl+Z, войско на борту в окне битвы.
TEST(app_naval_embark_and_land) {
  Harness h("naval_embark");
  h.demo();
  h.waitMap();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  CHECK(hel != 0);
  auto shore = makeShore(h, hel, 600);
  CHECK(shore.has_value());
  if (!shore) return;
  const Id army = shore->army, fleet = shore->fleet;
  showBoth(h, shore->armyPos, shore->fleetPos, army);
  // Перетаскивание войска на флот: подсказка «На борт» и окно посадки.
  dragTo(h, screenOf(h, shore->armyPos), screenOf(h, shore->fleetPos), "naval_embark_drag");
  h.settle();
  CHECK(h->hasDialog("army.encounter"));
  CHECK(h->uiRect("encounter.capacity") != nullptr);
  CHECK(h.shot("naval_embark_dialog"));
  CHECK(h.clickUi("encounter.ok"));
  h.settle();
  CHECK_EQ(rules::cargoOf(h->world(), fleet), army);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Army, fleet}));
  // Войско на борту: на карте только флот (со значком войска), в панели — вкладка «Войско на борту».
  const gfx::Pt fs = screenOf(h, h->world().army(fleet)->pos);
  CHECK_EQ(h->map().armyAt(fs.x, fs.y), fleet);
  const gfx::Pt as = screenOf(h, shore->armyPos);
  CHECK(h->map().armyAt(as.x, as.y) != army);
  h.waitMap();
  h.settle();
  CHECK(h->uiRect("army.cargo.land") != nullptr);
  CHECK(h->uiRect("fleet.capacity") != nullptr);
  CHECK(h.shot("naval_cargo_tab"));
  if (const RectF* r = h->uiRect("inspector")) CHECK(cropShot("naval_cargo_panel", *r));
  CHECK(cropShot("naval_fleet_badge", RectF{fs.x - 50, fs.y - 50, 100, 90}, 3));
  // Состав флота: вместимость.
  h->ui.tabOf[app::SelType::Army] = "army.units";
  h.settle();
  CHECK(h->uiRect("fleet.capacity") != nullptr);
  CHECK(h.shot("naval_fleet_units"));
  // Войско на борту в своей панели: «На борту» и «Высадка».
  h->select(app::SelType::Army, army);
  h.settle();
  CHECK(h->uiRect("army.carrier") != nullptr);
  CHECK(h->uiRect("army.land") != nullptr);
  CHECK(h->uiRect("army.split") == nullptr);
  CHECK(h.shot("naval_aboard_army"));
  // Высадка: инструмент карты, неверное место (море) — причина, верное — суша у флота.
  h->ui.tabOf[app::SelType::Army] = "army.cargo";
  h->select(app::SelType::Army, fleet);
  h.settle();
  CHECK(h.clickUi("army.cargo.land"));
  h.settle();
  CHECK(h->ui.tool == app::ToolId::Landing);
  CHECK(h->uiRect("landing.army") != nullptr);
  const Vec2 fpos = h->world().army(fleet)->pos;
  gfx::Pt bad = screenOf(h, fpos + Vec2{0, 2});
  h.move(bad.x, bad.y);
  h.move(bad.x + 1, bad.y);
  CHECK(h.shot("naval_landing_invalid"));
  auto spot = rules::findFreeSpot(h->world(), ArmyKind::Army, fpos);
  CHECK(spot.has_value());
  if (!spot) return;
  std::string why;
  CHECK_MSG(rules::canLand(h->world(), fleet, *spot, &why), why);
  const gfx::Pt good = screenOf(h, *spot);
  h.move(good.x, good.y);
  h.move(good.x + 1, good.y);
  h.dropToasts();
  CHECK(h.shot("naval_landing_valid"));
  // Неверное место щелчком — уведомление, войско на борту.
  h.click(bad.x, bad.y);
  CHECK_EQ(rules::cargoOf(h->world(), fleet), army);
  h.dropToasts();
  const Vec2 at = h->map().view().toMap(good.x + 1, good.y);
  CHECK(rules::canLand(h->world(), fleet, at, &why));
  h.click(good.x + 1, good.y);
  h.settle();
  CHECK_EQ(rules::cargoOf(h->world(), fleet), Id(0));
  CHECK_EQ(rules::carrierOf(h->world(), army), Id(0));
  CHECK(dist(h->world().army(army)->pos, at) < 1);
  CHECK(h->ui.tool == app::ToolId::Select);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Army, army}));
  h.key(Key::Z, ctrl());
  CHECK_EQ(rules::cargoOf(h->world(), fleet), army);
  h.key(Key::Y, ctrl());
  CHECK_EQ(rules::cargoOf(h->world(), fleet), Id(0));
  // Esc отменяет высадку.
  h->act("Снова на борт", [&](Tx& tx) {
    rules::moveArmy(tx, army, shore->armyPos);
    rules::embark(tx, army, fleet);
  });
  app::mil::startLanding(h.a(), fleet);
  h.settle();
  CHECK(h->ui.tool == app::ToolId::Landing);
  h.key(Key::Escape);
  h.settle();
  CHECK(h->ui.tool == app::ToolId::Select);
  CHECK_EQ(rules::cargoOf(h->world(), fleet), army);
  // Битва флота: войско на борту — в карточке стороны.
  Id enemy = 0;
  h->world().armies.each([&](const Army& x) {
    if (!enemy && x.isFleet() && x.id != fleet && x.leader() != hel) enemy = x.id;
  });
  CHECK(enemy != 0);
  if (!enemy) return;
  app::mil::openBattle(h.a(), enemy, fleet, h->world().army(enemy)->pos);
  h.settle();
  CHECK(h->hasDialog("battle"));
  CHECK(h->uiRect("battle.cargo.defender") != nullptr);
  CHECK(h->uiRect("battle.cargo.attacker") == nullptr);
  h.dropToasts();
  CHECK(h.shot("naval_battle_cargo"));
}

// Флот в морской провинции: высадка — в провинции у её берегов (подсвечены), в чужой глубине суши — нельзя.
TEST(app_naval_landing_provinces) {
  Harness h("naval_landing_sea");
  h.demo();
  h.waitMap();
  // Море у берегов демонстрационного мира почти не назначено провинциям: морская провинция у берега приморской
  // провинции государства (контуром вокруг места флота у её берега), флот государства — в ней.
  const Id state = factionByName(h->world(), "Хельдвиг");
  const Id fleet = armyOf(h->world(), state, ArmyKind::Fleet);
  Id coast = 0;
  CHECK(fleet != 0);
  std::vector<Id> cands;
  h->world().provinces.each([&](const Province& p) {
    if (!p.sea && p.owner == state && rules::isCoastal(h->world(), p.id)) cands.push_back(p.id);
  });
  for (Id pid : cands) {
    if (coast) break;
    auto sea = rules::fleetSpot(h->world(), pid);
    if (!sea) continue;
    const Vec2 s = *sea;
    const bool ok = h->act("Морская провинция у берега", [&](Tx& tx) {
      const Id sp = rules::createProvince(tx, {{s.x - 160, s.y - 160}, {s.x + 160, s.y - 160}, {s.x + 160, s.y + 160}, {s.x - 160, s.y + 160}},
                                          Terrain::Sea).province;
      if (!sp) fail("нет области");
      tx.province(sp).name = "Тестовый залив";
    });
    if (!ok) continue;
    auto spot = rules::fleetSpot(h->world(), pid);
    if (!spot || !h->act("Флот в заливе", [&](Tx& tx) { rules::moveArmy(tx, fleet, *spot); })) continue;
    const std::vector<Id> provs = rules::landingProvinces(h->world(), fleet);
    if (std::find(provs.begin(), provs.end(), pid) != provs.end()) coast = pid;
  }
  CHECK(coast != 0);
  if (!coast) return;
  const World& w0 = h->world();
  const Id row = armyRowWithReserve(w0, state, 100);
  CHECK(row != 0);
  Id army = 0;
  h->act("Войско на борту", [&](Tx& tx) {
    army = rules::createArmy(tx, ArmyKind::Army, state, *rules::findFreeSpot(tx.w(), ArmyKind::Army, labelOf(tx.w(), coast)));
    rules::setUnits(tx, army, state, row, 100);
    rules::embark(tx, army, fleet);
  });
  CHECK_EQ(rules::cargoOf(h->world(), fleet), army);
  const Vec2 fpos = h->world().army(fleet)->pos;
  showBoth(h, fpos, labelOf(h->world(), coast), fleet);
  app::mil::startLanding(h.a(), fleet);
  h.settle();
  CHECK(h->ui.tool == app::ToolId::Landing);
  auto spot = rules::findFreeSpot(h->world(), ArmyKind::Army, labelOf(h->world(), coast));
  CHECK(spot.has_value());
  if (!spot) return;
  CHECK(rules::canLand(h->world(), fleet, *spot));
  const gfx::Pt sp = screenOf(h, *spot);
  h.move(sp.x, sp.y);
  h.move(sp.x + 1, sp.y);
  h.dropToasts();
  CHECK(h.shot("naval_landing_provinces"));
  const std::vector<Id> allowed = rules::landingProvinces(h->world(), fleet);
  h.click(sp.x + 1, sp.y);
  h.settle();
  CHECK_EQ(rules::cargoOf(h->world(), fleet), Id(0));
  const Id at = app::mil::provinceUnder(h->world(), h->world().army(army)->pos);
  CHECK(std::find(allowed.begin(), allowed.end(), at) != allowed.end());
}

// Обмен отрядами: встреча двух войск — «Объединить» или «Обмен отрядами»; окно обмена; войско к флоту с войском на
// борту — сразу окно обмена с вместимостью.
TEST(app_naval_exchange) {
  Harness h("naval_exchange");
  h.demo();
  h.waitMap();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  auto shore = makeShore(h, hel, 400);
  CHECK(shore.has_value());
  if (!shore) return;
  const Id x = shore->army;
  // Второе войско рядом.
  const World& w0 = h->world();
  const Id row = w0.army(x)->groups[0].units[0].row;
  auto near = rules::findFreeSpot(w0, ArmyKind::Army, shore->armyPos + Vec2{150, 0}, 0);
  CHECK(near.has_value());
  if (!near) return;
  Id y = 0, hero = 0;
  h->act("Второе войско", [&](Tx& tx) {
    y = rules::createArmy(tx, ArmyKind::Army, hel, *near);
    rules::setUnits(tx, y, hel, row, 200);
    hero = rules::createCharacter(tx, hel, "Капитан Ордо");
    tx.character(hero).hero = true;
    rules::setHero(tx, y, hero, true);
    rules::setCommander(tx, y, hero);
  });
  CHECK(y != 0);
  const Vec2 ypos = h->world().army(y)->pos;
  showBoth(h, shore->armyPos, ypos, y);
  dragTo(h, screenOf(h, ypos), screenOf(h, shore->armyPos));
  h.settle();
  CHECK(h->hasDialog("army.encounter"));
  CHECK(h->uiRect("encounter.exchange") != nullptr);
  CHECK(h.shot("naval_merge_or_exchange"));
  CHECK(h.clickUi("encounter.exchange"));
  h.settle();
  CHECK(h->hasDialog("army.exchange"));
  CHECK(h->world().army(y)->pos == ypos);   // перемещённое войско вернулось на место
  const std::string mark = "exchange.a." + std::to_string(hel) + "." + std::to_string(row);
  CHECK(h.clickUi(mark));   // первый столбец — перемещённое войско: 200 → 50
  h.retype("50");
  h.key(Key::Enter);
  h.settle();
  CHECK(h.clickUi("exchange.hero.b." + std::to_string(hero)));   // герой (полководец) — во второе войско
  h.settle();
  CHECK(h.shot("naval_exchange_dialog"));
  CHECK(h.clickUi("exchange.ok"));
  h.settle();
  CHECK(!h->hasDialog());
  const World& w = h->world();
  CHECK_EQ(app::mil::unitCount(*w.army(y)), i64(50));
  CHECK_EQ(app::mil::unitCount(*w.army(x)), i64(550));
  CHECK_EQ(w.army(x)->commander, hero);   // полководец следует за героем
  CHECK(w.army(y)->pos == ypos);
  h.key(Key::Z, ctrl());
  CHECK_EQ(app::mil::unitCount(*h->world().army(y)), i64(200));
  h.key(Key::Y, ctrl());
  // Войско x на борту, войско y подходит к флоту — окно обмена с войском на борту.
  h->act("На борт", [&](Tx& tx) { rules::embark(tx, x, shore->fleet); });
  auto beside = rules::findFreeSpot(h->world(), ArmyKind::Army, shore->armyPos, 0);
  CHECK(beside.has_value());
  if (!beside) return;
  h->act("К берегу", [&](Tx& tx) { rules::moveArmy(tx, y, *beside); });
  CHECK(rules::canBoardExchange(h->world(), y, shore->fleet));
  showBoth(h, *beside, shore->fleetPos, y);
  dragTo(h, screenOf(h, *beside), screenOf(h, shore->fleetPos), "naval_board_exchange_drag");
  h.settle();
  CHECK(h->hasDialog("army.exchange"));
  CHECK(h.shot("naval_exchange_aboard"));
  CHECK(h.clickUi("exchange.cancel"));
  h.settle();
  CHECK(!h->hasDialog());
  CHECK(h->world().army(y)->pos == *beside);
}

// Союзное войско (ТЗ «Доработки №4», п.9): войско наводят на войско союзника — управление у союзника (корона на его
// плитке), у второй плитки — «Вернуть войско».
TEST(app_naval_allied_return) {
  Harness h("naval_allied");
  h.demo();
  h.waitMap();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id alm = factionByName(h->world(), "Альмарин");
  CHECK(h->world().relation(hel, alm).s == RelStatus::Alliance);
  const Id x = armyOf(h->world(), hel, ArmyKind::Army);
  const Vec2 xpos = h->world().army(x)->pos;
  const Id row = armyRowWithReserve(h->world(), alm, 300);
  auto spot = rules::findFreeSpot(h->world(), ArmyKind::Army, xpos + Vec2{-170, 60}, 0);
  CHECK(row != 0 && spot.has_value());
  if (!row || !spot) return;
  Id z = 0;
  h->act("Войско союзника", [&](Tx& tx) {
    z = rules::createArmy(tx, ArmyKind::Army, alm, *spot);
    rules::setUnits(tx, z, alm, row, 300);
  });
  showBoth(h, xpos, *spot, z);
  dragTo(h, screenOf(h, *spot), screenOf(h, xpos));
  h.settle();
  CHECK(h->hasDialog("army.encounter"));
  CHECK(h->uiRect("encounter.leader") != nullptr);
  CHECK(h.shot("naval_alliance_dialog"));
  CHECK(h.clickUi("encounter.ok"));
  h.settle();
  const Army* al = h->world().army(x);
  CHECK(al && al->allied());
  CHECK_EQ(al->leader(), hel);   // управление — у того, на кого навели
  CHECK(!h->world().army(z));
  h->ui.tabOf[app::SelType::Army] = "army.units";
  h->select(app::SelType::Army, x);
  h.settle();
  CHECK(reveal(h, "army.return." + std::to_string(alm)));
  CHECK(h.shot("naval_allied_groups"));
  if (const RectF* r = h->uiRect("inspector")) CHECK(cropShot("naval_allied_panel", *r));
  CHECK(h.clickUi("army.return." + std::to_string(alm)));
  h.settle();
  CHECK(!h->world().army(x)->allied());
  Id back = 0;
  h->world().armies.each([&](const Army& a) {
    if (!a.isFleet() && a.leader() == alm && app::mil::unitCount(a) == 300) back = a.id;
  });
  CHECK(back != 0);
}

// Флот у верфи (ТЗ «Доработки №3», п.3): без свободного редактирования «Новый флот» недоступен, «Поставить флот» во
// вкладке «Флот» — выбор провинции с верфью у моря; переключатель в настройках правил.
TEST(app_naval_shipyard_place) {
  Harness h("naval_shipyard");
  h.demo();
  h.waitMap();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  CHECK(!h->world().settings->freeFleets);
  // Shift+A — недоступно, причина в уведомлении.
  h.dropToasts();
  h.move(h->mapArea().cx(), h->mapArea().cy());
  h.key(Key::A, platform::ModShift);   // уведомление проверяется сразу: settle дождался бы его исчезновения
  bool told = false;
  for (auto& t : h->toasts()) told = told || t.text.find("Свободное редактирование флотов") != std::string::npos;
  CHECK(told);
  CHECK(h->ui.tool == app::ToolId::Select);
  h.dropToasts();
  if (const RectF* r0 = h->uiRect("tool.fleet")) {
    const RectF r = *r0;   // отметки пересобираются каждый кадр
    h.move(r.cx(), r.cy());
    hl::advance(0.5);
    h.settle();
    CHECK(h.shot("naval_fleet_tool_blocked"));
    CHECK(cropShot("naval_fleet_tool_tip", RectF{r.x - 4, r.y - 8, 320, 110}, 2));
  }
  // Верфь в приморской провинции государства.
  Id coast = 0, yard = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!coast && !p.sea && p.owner == hel && rules::isCoastal(h->world(), p.id) && rules::fleetSpot(h->world(), p.id)) coast = p.id;
  });
  CHECK(coast != 0);
  h->act("Верфь", [&](Tx& tx) {
    yard = rules::createBuilding(tx, 0, "Верфь у моря");
    rules::setBuildingFlag(tx, yard, rules::BuildingFlag::Shipyard, true);
    rules::setLevelShips(tx, yard, 1, 1u << int(ShipType::Frigate));
    tx.province(coast).size = ProvSize::Large;
    tx.province(coast).city = CityType::City;
    rules::placeBuilding(tx, coast, yard, 1);
  });
  CHECK((rules::shipyardProvinces(h->world(), hel) == std::vector<Id>{coast}));
  h->ui.tabOf[app::SelType::Faction] = "faction.fleet";
  h->select(app::SelType::Faction, hel, true);
  h.settle();
  CHECK(reveal(h, "mil.fleet.place"));
  CHECK(h.clickUi("mil.fleet.place"));
  h.settle();
  CHECK(h->uiRect("mil.fleet.place." + std::to_string(coast)) != nullptr);
  CHECK(h.shot("naval_place_fleet_popup"));
  const size_t n0 = h->world().armies.size();
  CHECK(h.clickUi("mil.fleet.place." + std::to_string(coast)));
  h.settle();
  CHECK_EQ(h->world().armies.size(), n0 + 1);
  CHECK(h->ui.sel.type == app::SelType::Army);
  const Army* f = h->world().army(h->ui.sel.id);
  CHECK(f && f->isFleet() && f->leader() == hel);
  if (f) CHECK(rules::validPosition(h->world(), ArmyKind::Fleet, f->pos, f->id));
  // Настройки правил: «Свободное редактирование флотов» — инструмент снова доступен.
  h->showSettings();
  h.settle();
  if (const RectF* t = h->uiRect("settings.tabs")) h.click(t->x + t->w * 2.5f / 4, t->cy());
  h.settle();
  CHECK(h->uiRect("settings.freeFleets") != nullptr);
  CHECK(h.shot("naval_settings_free_fleets"));
  CHECK(h.clickUi("settings.freeFleets"));
  h.settle();
  CHECK(h->world().settings->freeFleets);
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  h.key(Key::A, platform::ModShift);
  h.settle();
  CHECK(h->ui.tool == app::ToolId::NewFleet);
}

// Морское чудовище (ТЗ «Доработки №3», п.8): строка флота с ключевым ресурсом подгруппы «Морские чудовища».
TEST(app_naval_sea_monster_key) {
  Harness h("naval_monster");
  h.demo();
  h.waitMap();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  Id row = 0;
  h->act("Морское чудовище", [&](Tx& tx) { row = rules::addFleetRow(tx, hel, ShipType::SeaMonster); });
  CHECK(app::mil::unitRow(h->world(), hel, row, true)->keyMissing);
  h->ui.tabOf[app::SelType::Faction] = "faction.fleet";
  h->select(app::SelType::Faction, hel);   // страница государства: широкая таблица
  h.settle();
  h.waitMap();
  h.settle();
  const Faction* f = h->world().faction(hel);
  int idx = -1;
  for (size_t i = 0; i < f->fleet.size(); i++)
    if (f->fleet[i].id == row) idx = int(i);
  CHECK(idx >= 0);
  const std::string key = "mil.fleet.row." + std::to_string(idx) + ".key";
  CHECK(h->uiRect(key) != nullptr);
  CHECK(h.shot("naval_monster_row"));
  CHECK(h.clickUi(key));
  h.settle();
  CHECK(h.shot("naval_monster_key_menu"));
  h.type("Кракен");
  h.key(Key::Enter);
  h.settle();
  const FleetRow* r = h->world().faction(hel)->fleetRow(row);
  CHECK(r && r->keyRes != 0);
  if (r) CHECK_EQ(h->world().resource(r->keyRes)->name, std::string("Кракены"));
  CHECK(!app::mil::unitRow(h->world(), hel, row, true)->keyMissing);
  // Узкая таблица в панели у карты: ключевой ресурс — в карточке выбранной строки.
  h->select(app::SelType::Faction, hel, true);
  h.settle();
  CHECK(reveal(h, "mil.fleet.row." + std::to_string(idx)));
  CHECK(h.clickUi("mil.fleet.row." + std::to_string(idx)));
  h.settle();
  CHECK(reveal(h, "mil.detail.key"));
  if (const RectF* r = h->uiRect("inspector")) CHECK(cropShot("naval_monster_card", *r));
  // Галеоны не предлагаются в составе флота на карте.
  const Id fl = armyOf(h->world(), hel, ArmyKind::Fleet);
  h->act("Галеоны", [&](Tx& tx) { rules::addFleetRow(tx, hel, ShipType::Galleon, "", 5, 0); });
  h->ui.tabOf[app::SelType::Army] = "army.units";
  h->select(app::SelType::Army, fl);
  h.settle();
  CHECK(reveal(h, "army.addunit." + std::to_string(hel)));
  CHECK(h.clickUi("army.addunit." + std::to_string(hel)));
  h.settle();
  CHECK(h.clickUi("army.addunit.row"));
  h.settle();
  CHECK(h.shot("naval_fleet_add_no_galleons"));
}
