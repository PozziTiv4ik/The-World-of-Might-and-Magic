// Сценарии вассалитета (ТЗ «Механика вассалитета») на демонстрационном мире: сюзерен в карточке вассала и выбор
// сюзерена, «Восстать» при отношениях −50 и ниже (война с сюзереном), вопрос сюзерену атакованного вассала, призыв
// вассалов зачинщика войны и вассалов атакованного сюзерена (окна по очереди), вкладка «Вассалы» сюзерена.
#include "app/flows.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

namespace {

void shotClean(Harness& h, const std::string& name) {
  h.waitMap();
  h.settle();
  CHECK(h.shot(name));
}

bool clickAt(Harness& h, const std::string& name, float fx) {
  const RectF* r = h->uiRect(name);
  if (!r) return false;
  RectF rr = *r;
  h.click(rr.x + rr.w * fx, rr.cy());
  return true;
}

}  // namespace

// ---------------------------------------------------------------- сюзерен в карточке вассала и восстание
TEST(app_diplomacy_vassal_rebel) {
  HideTestRegs regs;
  Harness h("diplomacy_vassal_rebel");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id korven = findFaction(h->world(), "Республика Корвен");
  const Id mirel = findFaction(h->world(), "Княжество Мирель");
  CHECK(korven && mirel);
  // Сюзерен — выбором в обзоре вассала: «Нет», затем государства по алфавиту (без самой Мирели) — Корвен пятый.
  openTab(h, mirel, "faction.overview");
  CHECK(clickIn(h, "overview.suzerain"));
  h.key(Key::PageUp);
  for (int i = 0; i < 5; i++) h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(mirel)->suzerain, korven);
  h.settle();
  CHECK(h->uiRect("overview.suzerain.chip") != nullptr);
  CHECK(h->uiRect("faction.suzerain") != nullptr);   // значок в шапке
  // Отношения союзные — «Восстать» недоступно.
  CHECK(h->world().relation(mirel, korven).v > schema::kVassalRebelAt);
  CHECK(clickIn(h, "overview.rebel"));
  h.settle();
  CHECK(!h->hasDialog("confirm"));
  CHECK_EQ(h->world().faction(mirel)->suzerain, korven);
  shotClean(h, "diplomacy_vassal_overview");
  // Вкладка «Вассалы» у сюзерена.
  openTab(h, korven, "faction.vassals");
  CHECK_EQ(h->ui.tabOf[app::SelType::Faction], std::string("faction.vassals"));
  CHECK(h->uiRect("vassals.row." + std::to_string(mirel)) != nullptr);
  // Отношения −60: восстание доступно — подтверждение, война с сюзереном, вассалитет снят.
  CHECK(h->act("Отношения", [&](Tx& tx) { rules::setRelation(tx, mirel, korven, -60, RelStatus::Neutral); }));
  openTab(h, korven, "faction.vassals");
  shotClean(h, "diplomacy_vassals_tab_rebel");
  openTab(h, mirel, "faction.overview");
  CHECK(clickIn(h, "overview.rebel"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(mirel)->suzerain, Id(0));
  CHECK(h->world().relation(mirel, korven).s == RelStatus::War);
  // У бывшего сюзерена вкладки «Вассалы» больше нет.
  for (const app::TabDef& t : app::tabs())
    if (std::string_view(t.id) == "faction.vassals") CHECK(!t.visible(h.a(), korven));
  h->undo();
  h.step();
  CHECK_EQ(h->world().faction(mirel)->suzerain, korven);
  CHECK(h->world().relation(mirel, korven).s != RelStatus::War);
}

// ---------------------------------------------------------------- объявление войны: сюзерен вассала, затем призыв
TEST(app_diplomacy_vassal_war_flow) {
  HideTestRegs regs;
  Harness h("diplomacy_vassal_war");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id alm = findFaction(h->world(), "Королевство Альмарин");
  const Id korven = findFaction(h->world(), "Республика Корвен");
  const Id mirel = findFaction(h->world(), "Княжество Мирель");
  const Id olsta = findFaction(h->world(), "Вольные города Ольсты");
  const Id solmar = findFaction(h->world(), "Теократия Солмар");
  CHECK(alm && korven && mirel && olsta && solmar);
  CHECK(h->act("Вассалы", [&](Tx& tx) {
    rules::setSuzerain(tx, mirel, korven);
    rules::setSuzerain(tx, olsta, alm);
    rules::setSuzerain(tx, solmar, alm);
  }));
  const double relKM = h->world().relation(korven, mirel).v;
  const double relAO = h->world().relation(alm, olsta).v, relAS = h->world().relation(alm, solmar).v;
  // Альмарин объявляет войну Мирели из вкладки «Дипломатия».
  openTab(h, alm, "faction.diplomacy");
  CHECK(clickIn(h, "dip.status." + std::to_string(mirel)));
  CHECK(h.clickUi("status.item." + std::to_string(int(RelStatus::War))));
  h.settle();
  CHECK(h->world().relation(alm, mirel).s == RelStatus::War);
  // 1. Вопрос сюзерену атакованного вассала (призыв — после ответа).
  CHECK(h->hasDialog("vassal.defend"));
  CHECK(!h->hasDialog("vassal.call"));
  shotClean(h, "diplomacy_vassal_defend");
  CHECK(h.clickUi("vassal.defend.no"));
  h.settle();
  CHECK(h->world().relation(korven, alm).s != RelStatus::War);
  CHECK_NEAR(h->world().relation(korven, mirel).v, std::max(-100.0, relKM + schema::kVassalAbandonPenalty), 1e-9);
  // 2. Призыв вассалов Альмарина: Ольсты согласны, Солмар отказывается.
  CHECK(h->hasDialog("vassal.call"));
  CHECK(clickAt(h, "vassal.call." + std::to_string(olsta), 0.25f));
  CHECK(clickAt(h, "vassal.call." + std::to_string(solmar), 0.75f));
  shotClean(h, "diplomacy_vassal_call");
  CHECK(h.clickUi("vassal.call.ok"));
  h.settle();
  CHECK(!h->hasDialog("vassal.call"));
  CHECK(h->world().relation(olsta, mirel).s == RelStatus::War);
  CHECK(h->world().relation(solmar, mirel).s != RelStatus::War);
  CHECK_NEAR(h->world().relation(alm, olsta).v, std::min(100.0, relAO + schema::kVassalCallAgree), 1e-9);
  CHECK_NEAR(h->world().relation(alm, solmar).v, std::max(-100.0, relAS + schema::kVassalCallRefuse), 1e-9);
  // Вкладка «Вассалы» Альмарина: оба вассала и отношения с ними.
  openTab(h, alm, "faction.vassals");
  CHECK(h->uiRect("vassals.row." + std::to_string(olsta)) != nullptr);
  CHECK(h->uiRect("vassals.row." + std::to_string(solmar)) != nullptr);
  shotClean(h, "diplomacy_vassals_tab");
}

// ---------------------------------------------------------------- сюзерен вступает в войну за вассала
TEST(app_diplomacy_vassal_defend_yes) {
  HideTestRegs regs;
  Harness h("diplomacy_vassal_defend");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id orda = findFaction(h->world(), "Орда Таргаш");
  const Id mirel = findFaction(h->world(), "Княжество Мирель");
  const Id solmar = findFaction(h->world(), "Теократия Солмар");
  CHECK(orda && mirel && solmar);
  CHECK(h->act("Вассал", [&](Tx& tx) { rules::setSuzerain(tx, mirel, solmar); }));
  const double rel0 = h->world().relation(solmar, mirel).v;
  openTab(h, orda, "faction.diplomacy");
  CHECK(clickIn(h, "dip.status." + std::to_string(mirel)));
  CHECK(h.clickUi("status.item." + std::to_string(int(RelStatus::War))));
  h.settle();
  CHECK(h->hasDialog("vassal.defend"));
  CHECK(h.clickUi("vassal.defend.yes"));
  h.settle();
  CHECK(!h->hasDialog("vassal.defend"));
  CHECK(!h->hasDialog("vassal.call"));   // у Орды вассалов нет
  CHECK(h->world().relation(solmar, orda).s == RelStatus::War);
  CHECK_NEAR(h->world().relation(solmar, orda).v, schema::kWarRelation, 1e-9);
  CHECK_NEAR(h->world().relation(solmar, mirel).v, std::min(100.0, rel0 + schema::kVassalDefendBonus), 1e-9);
}

// ---------------------------------------------------------------- на сюзерена напали: его вассалы — на защиту
TEST(app_diplomacy_vassal_called_to_defend) {
  HideTestRegs regs;
  Harness h("diplomacy_vassal_defense");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id orda = findFaction(h->world(), "Орда Таргаш");
  const Id solmar = findFaction(h->world(), "Теократия Солмар");
  const Id olsta = findFaction(h->world(), "Вольные города Ольсты");
  const Id mirel = findFaction(h->world(), "Княжество Мирель");
  const Id korven = findFaction(h->world(), "Республика Корвен");
  CHECK(orda && solmar && olsta && mirel && korven);
  CHECK(h->world().relation(korven, orda).s == RelStatus::War);   // уже воюет с зачинщиком — не зовётся
  CHECK(h->act("Вассалы", [&](Tx& tx) {
    rules::setSuzerain(tx, olsta, solmar);
    rules::setSuzerain(tx, mirel, solmar);
    rules::setSuzerain(tx, korven, solmar);
  }));
  const double relO = h->world().relation(solmar, olsta).v, relM = h->world().relation(solmar, mirel).v;
  // Орда (без вассалов) объявляет войну Солмару (не вассал): сразу призыв вассалов Солмара на защиту.
  openTab(h, orda, "faction.diplomacy");
  CHECK(clickIn(h, "dip.status." + std::to_string(solmar)));
  CHECK(h.clickUi("status.item." + std::to_string(int(RelStatus::War))));
  h.settle();
  CHECK(h->world().relation(orda, solmar).s == RelStatus::War);
  CHECK(!h->hasDialog("vassal.defend"));
  CHECK(h->hasDialog("vassal.call"));
  CHECK(h->uiRect("vassal.call.defense") != nullptr);
  CHECK(h->uiRect("vassal.call.attack") == nullptr);
  CHECK(h->uiRect("vassal.call." + std::to_string(olsta)) != nullptr);
  CHECK(h->uiRect("vassal.call." + std::to_string(mirel)) != nullptr);
  CHECK(h->uiRect("vassal.call." + std::to_string(korven)) == nullptr);
  // Ольсты согласны, Мирель отказывается.
  CHECK(clickAt(h, "vassal.call." + std::to_string(olsta), 0.25f));
  CHECK(clickAt(h, "vassal.call." + std::to_string(mirel), 0.75f));
  shotClean(h, "diplomacy_vassal_defense_call");
  CHECK(h.clickUi("vassal.call.ok"));
  h.settle();
  CHECK(!h->hasDialog("vassal.call"));
  CHECK(h->world().relation(olsta, orda).s == RelStatus::War);
  CHECK(h->world().relation(mirel, orda).s != RelStatus::War);
  CHECK_NEAR(h->world().relation(solmar, olsta).v, std::min(100.0, relO + schema::kVassalCallAgree), 1e-9);
  CHECK_NEAR(h->world().relation(solmar, mirel).v, std::max(-100.0, relM + schema::kVassalCallRefuse), 1e-9);
  // Ответы — одно действие: отмена возвращает мир и отношения.
  h->undo();
  h.step();
  CHECK(h->world().relation(olsta, orda).s != RelStatus::War);
  CHECK_NEAR(h->world().relation(solmar, olsta).v, relO, 1e-9);

  // Зачинщик с вассалами нападает на сюзерена с вассалами: сначала призыв вассалов зачинщика, затем — атакованного.
  const Id alm = findFaction(h->world(), "Королевство Альмарин");
  const Id held = findFaction(h->world(), "Северный союз Хельдвиг");
  CHECK(alm && held);
  CHECK(h->act("Вассал", [&](Tx& tx) { rules::setSuzerain(tx, held, alm); }));
  h.dropToasts();
  openTab(h, alm, "faction.diplomacy");
  CHECK(clickIn(h, "dip.status." + std::to_string(solmar)));
  CHECK(h.clickUi("status.item." + std::to_string(int(RelStatus::War))));
  h.settle();
  CHECK(h->world().relation(alm, solmar).s == RelStatus::War);
  CHECK(h->hasDialog("vassal.call"));
  CHECK(h->uiRect("vassal.call.attack") != nullptr);
  CHECK(h->uiRect("vassal.call." + std::to_string(held)) != nullptr);
  CHECK(clickAt(h, "vassal.call." + std::to_string(held), 0.25f));
  CHECK(h.clickUi("vassal.call.ok"));
  h.settle();
  CHECK(h->world().relation(held, solmar).s == RelStatus::War);
  // Второе окно: вассалы Солмара против Альмарина (Корвен теперь тоже зовётся — с Альмарином он не воюет).
  CHECK(h->hasDialog("vassal.call"));
  CHECK(h->uiRect("vassal.call.defense") != nullptr);
  CHECK(h->uiRect("vassal.call." + std::to_string(korven)) != nullptr);
  CHECK(h->uiRect("vassal.call." + std::to_string(held)) == nullptr);
  // Закрытие без ответа ничего не меняет.
  const World before = h->world();
  CHECK(h.clickUi("vassal.call.cancel"));
  h.settle();
  CHECK(!h->hasDialog("vassal.call"));
  CHECK(World::diff(before, h->world()) == 0);

  // Вассал, сам напавший на сюзерена, в призыв не попадает (как и воюющие с сюзереном).
  CHECK(h->act("Война вассала", [&](Tx& tx) { rules::declareWar(tx, mirel, solmar); }));
  app::flow::afterWarDeclared(h.a(), mirel, solmar);
  h.settle();
  CHECK(h->hasDialog("vassal.call"));
  CHECK(h->uiRect("vassal.call." + std::to_string(mirel)) == nullptr);
  CHECK(h->uiRect("vassal.call." + std::to_string(korven)) != nullptr);
  CHECK(h.clickUi("vassal.call.cancel"));
  h.settle();
}
