// Сценарии штурма и захвата (ТЗ «Механика войн», п.4–5): войско перетаскивают на замок или башню провинции
// государства, с которым его фракция в войне, — войско встаёт у стен, окно штурма гарнизона (потери, победитель),
// победа — окно захвата (захватить, разграбить, разорить, опустошить); без гарнизона — сразу захват; уничтожение
// войска — «Судьба героев».
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

struct Setup {
  Id hel = 0, vk = 0, army = 0, hero = 0;
  SiegeSpot spot;
};

// Войско Хельдвига (2000 воинов и герой) у замка или башни провинции Валь-Кетры (они в войне); гарнизон — garrison.
std::optional<Setup> prepare(Harness& h, i64 garrison, Id skipProvince = 0) {
  Setup s;
  s.hel = factionByName(h->world(), "Хельдвиг");
  s.vk = factionByName(h->world(), "Валь-Кетра");
  CHECK(h->world().relation(s.hel, s.vk).s == RelStatus::War);
  auto spot = siegeSpot(h, s.vk, s.hel, skipProvince);
  CHECK_MSG(spot.has_value(), "нет замка или башни в провинции Валь-Кетры");
  if (!spot) return std::nullopt;
  s.spot = *spot;
  const Id vrow = h->world().faction(s.vk)->army[0].id;
  CHECK(h->act("Гарнизон", [&](Tx& tx) {
    for (const GarrisonEntry& g : std::vector<GarrisonEntry>(tx.w().province(s.spot.province)->garrison)) rules::setGarrison(tx, s.spot.province, g.row, 0);
    if (garrison > 0) rules::setGarrison(tx, s.spot.province, vrow, garrison);
  }));
  s.hero = freeHero(h, s.hel);
  CHECK(s.hero != 0);
  s.army = spawnArmy(h, s.hel, s.spot.center + Vec2(230, 40), 2000, s.hero);
  CHECK(s.army != 0);
  if (!s.army) return std::nullopt;
  h->select(app::SelType::Army, s.army);
  h.settle();
  showAt(h, (s.spot.center + h->world().army(s.army)->pos) * 0.5, 0.6);
  h.dropToasts();
  return s;
}

}  // namespace

// Гарнизон 300: перетаскивание на замок — «Штурм: …», окно штурма, гарнизон разбит, «Захватить и разграбить».
TEST(app_war_siege_drag_plunder) {
  Harness h("war_siege_plunder", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  auto s = prepare(h, 300);
  if (!s) return;
  const Vec2 from = h->world().army(s->army)->pos;
  dragArmy(h, screenOf(h, from), screenOf(h, s->spot.center), "war_siege_ghost");
  h.settle();
  CHECK(h->hasDialog("siege"));
  // Войско встало у стен.
  CHECK(dist(h->world().army(s->army)->pos, s->spot.center) < 80);
  CHECK_EQ(app::mil::provinceUnder(h->world(), h->world().army(s->army)->pos), s->spot.province);
  const Id vrow = h->world().faction(s->vk)->army[0].id;
  const Id hrow = h->world().faction(s->hel)->army[0].id;
  CHECK(enterNumber(h, "siege.loss.garrison." + std::to_string(vrow), 300));
  CHECK(enterNumber(h, "siege.loss.army." + std::to_string(hrow), 120));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_siege_dialog"));
  const i64 vkTotal = h->world().faction(s->vk)->armyRow(vrow)->total;
  CHECK(h.clickUi("siege.apply"));
  h.settle();
  CHECK(!h->hasDialog("siege"));
  CHECK(h->world().province(s->spot.province)->garrison.empty());
  CHECK_EQ(h->world().faction(s->vk)->armyRow(vrow)->total, vkTotal - 300);
  CHECK_EQ(unitsOf(h->world(), s->army), 2000 - 120);
  // Окно захвата: разграбление — оккупация, золото, «Разграбленная провинция», отношения −5.
  CHECK(h->hasDialog("capture"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_capture_dialog"));
  const rules::CaptureOptions o = rules::captureOptions(h->world(), s->army, s->spot.province);
  CHECK(o.can[int(rules::Capture::Plunder)]);
  const double gold0 = h->world().faction(s->hel)->treasury();
  const double rel0 = h->world().relation(s->hel, s->vk).v;
  CHECK(h.clickUi("capture.option.1"));
  CHECK(h.clickUi("capture.ok"));
  h.settle();
  CHECK(!h->hasDialog("capture"));
  const Province* p = h->world().province(s->spot.province);
  CHECK(p->occupied && p->occupier == s->hel);
  CHECK(rules::provinceHas(h->world(), s->spot.province, schema::mod::Plundered));
  CHECK_NEAR(h->world().faction(s->hel)->treasury(), gold0 + o.plunderGold, 1e-6);
  CHECK_NEAR(h->world().relation(s->hel, s->vk).v, std::max(-100.0, rel0 + schema::kPlunderRelation), 1e-9);
  // Повторно разграбить нельзя (модификатор «Разграбленная провинция»).
  const rules::CaptureOptions o2 = rules::captureOptions(h->world(), s->army, s->spot.province);
  CHECK(!o2.can[int(rules::Capture::Plunder)]);
  // Отмена — захват, затем штурм.
  h.key(Key::Z, ctrl());
  CHECK(!h->world().province(s->spot.province)->occupied);
  h.key(Key::Z, ctrl());
  CHECK_EQ(h->world().province(s->spot.province)->garrison.size(), size_t(1));
}

// Без гарнизона — сразу окно захвата; «Опустошить» с 0 % в рабы: население погибает, провинция без владельца,
// войску — «Неправедное деяние» и «Мучения совести».
TEST(app_war_siege_no_garrison_devastate) {
  Harness h("war_siege_devastate", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  auto s = prepare(h, 0);
  if (!s) return;
  const Vec2 from = h->world().army(s->army)->pos;
  dragArmy(h, screenOf(h, from), screenOf(h, s->spot.center));
  h.settle();
  CHECK(!h->hasDialog("siege"));
  CHECK(h->hasDialog("capture"));
  CHECK(h.clickUi("capture.option.3"));
  h.settle();
  const RectF* sl = h->uiRect("capture.slaves");
  CHECK(sl != nullptr);
  if (sl) h.click(sl->x + 1, sl->cy());   // к началу ползунка — 0 %
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_capture_devastate"));
  CHECK(h.clickUi("capture.ok"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(!h->hasDialog("capture"));
  const Province* p = h->world().province(s->spot.province);
  CHECK_EQ(p->owner, Id(0));
  i64 pop = 0;
  for (const RacePop& r : p->races) pop += r.pop;
  CHECK_EQ(pop, 0);
  CHECK(rules::provinceHas(h->world(), s->spot.province, schema::mod::Devastated));
  CHECK(rules::armyHas(h->world(), s->army, schema::mod::Unrighteous));
  CHECK(rules::armyHas(h->world(), s->army, schema::mod::Conscience));
  // Войско осталось в провинции.
  CHECK_EQ(app::mil::provinceUnder(h->world(), h->world().army(s->army)->pos), s->spot.province);
}

// Гарнизон устоял, войско уничтожено: «Судьба героев» — убит, место захоронения — провинция штурма.
TEST(app_war_siege_defeat_hero_fate) {
  Harness h("war_siege_defeat", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  auto s = prepare(h, 300);
  if (!s) return;
  const Vec2 from = h->world().army(s->army)->pos;
  dragArmy(h, screenOf(h, from), screenOf(h, s->spot.center));
  h.settle();
  CHECK(h->hasDialog("siege"));
  const Id hrow = h->world().faction(s->hel)->army[0].id;
  CHECK(enterNumber(h, "siege.loss.army." + std::to_string(hrow), 2000));
  CHECK(h.clickUi("siege.winner.attacker"));   // без отрядов — победителем не выбрать
  h.settle();
  CHECK(h.clickUi("siege.apply"));
  h.settle();
  CHECK(h->world().army(s->army) == nullptr);
  CHECK(h->hasDialog("hero.fate"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_siege_hero_fate"));
  CHECK(h.clickUi("fate." + std::to_string(s->hero) + ".1"));
  CHECK(h.clickUi("fate.ok"));
  h.settle();
  CHECK(!h->hasDialog("hero.fate"));
  CHECK(rules::characterHas(h->world(), s->hero, schema::mod::Dead));
  CHECK_EQ(h->world().character(s->hero)->burial, s->spot.province);
  CHECK(!rules::heroAvailable(h->world(), s->hero));
}

// «Отступить» из окна штурма: войско возвращается на исходную позицию, гарнизон цел.
TEST(app_war_siege_retreat) {
  Harness h("war_siege_retreat", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  auto s = prepare(h, 200);
  if (!s) return;
  const Vec2 from = h->world().army(s->army)->pos;
  dragArmy(h, screenOf(h, from), screenOf(h, s->spot.center));
  h.settle();
  CHECK(h->hasDialog("siege"));
  CHECK(h.clickUi("siege.retreat"));
  h.settle();
  CHECK(!h->hasDialog("siege"));
  CHECK(dist(h->world().army(s->army)->pos, from) < 1e-6);
  CHECK_EQ(h->world().province(s->spot.province)->garrison.size(), size_t(1));
}
