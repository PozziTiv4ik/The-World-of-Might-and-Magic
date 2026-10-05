// Объявление войны при встрече войск (ТЗ 1.c.iv) и вассалитет (ТЗ «Механика вассалитета»): после окна битвы —
// окно сюзерена атакованного вассала (flow::afterWarDeclared); окна — по очереди.
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

TEST(app_war_declare_war_then_vassal) {
  Harness h("war_declare_vassal", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  Id alm = factionByName(h->world(), "Альмарин");
  Id vk = factionByName(h->world(), "Валь-Кетра");
  Id mir = factionByName(h->world(), "Мирель");
  CHECK(alm && vk && mir);
  CHECK(h->world().relation(alm, vk).s == RelStatus::Neutral);
  CHECK(h->act("Вассал", [&](Tx& tx) { rules::setSuzerain(tx, vk, mir); }));
  const Id v = armyOf(h->world(), vk, ArmyKind::Army);
  CHECK(v != 0);
  if (!v) return;
  const Vec2 vpos = h->world().army(v)->pos;
  const Id z = spawnArmy(h, alm, vpos + Vec2(-190, 30), 300);
  CHECK(z != 0);
  if (!z) return;
  const Vec2 zpos = h->world().army(z)->pos;
  h->select(app::SelType::Army, z);
  h.settle();
  showAt(h, (vpos + zpos) * 0.5, 0.6);
  h.dropToasts();
  dragArmy(h, screenOf(h, zpos), screenOf(h, vpos));
  h.settle();
  CHECK(h->hasDialog("army.encounter"));
  CHECK(h.clickUi("encounter.ok"));
  h.settle();
  CHECK(h->world().relation(alm, vk).s == RelStatus::War);
  CHECK(h->hasDialog("battle"));
  CHECK(!h->hasDialog("vassal.defend"));   // сначала битва
  CHECK(h.clickUi("battle.retreat"));
  h.settle();
  CHECK(!h->hasDialog("battle"));
  CHECK(h->hasDialog("vassal.defend"));   // затем решение сюзерена атакованного вассала
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_declare_vassal"));
}
