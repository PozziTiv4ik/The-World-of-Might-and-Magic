// Сценарии дипломатии государств: симметричные отношения (значение и состояние у обеих сторон, отмена), щелчок по
// полоске отношения округляет до целого, «+10 одна вера», дань (окно, доход и модификатор дохода получателя).
#include "app/widgets.h"
#include "tests/test_app_faction_util.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;
namespace tr = rg::apptest::trees;

// ---------------------------------------------------------------- отношения (ТЗ 1.b.vi; «Механика войн», п.2)
TEST(app_diplomacy_relations_symmetric) {
  Harness h("faction_relations");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Королевство Альмарин");
  Id b = findFaction(h->world(), "Вольные города Ольсты");
  CHECK(a && b);
  CHECK(h->world().relation(a, b).s != RelStatus::War);
  openTab(h, a, "faction.diplomacy");
  // Таблица заполнена всеми прочими фракциями.
  h->world().factions.each([&](const Faction& f) {
    if (f.id != a) CHECK(h->uiRect("dip.row." + std::to_string(f.id)) != nullptr);
  });
  // «В войне» отсюда — объявление войны: отношения −25 (ТЗ 1.c.iv), запись хроники.
  CHECK(clickIn(h, "dip.status." + std::to_string(b)));
  CHECK(h.clickUi("status.item." + std::to_string(int(RelStatus::War))));
  h.step();
  CHECK(h->world().relation(a, b).s == RelStatus::War);
  CHECK_NEAR(h->world().relation(a, b).v, schema::kWarRelation, 1e-9);
  // Поле — действующее значение (с «+10 одна вера», ТЗ «Доработки №4», п.3); хранится значение без смещения.
  CHECK(typeNumber(h, "dip.value." + std::to_string(b), "-40"));
  CHECK_NEAR(rules::relationValue(h->world(), a, b), -40, 1e-9);
  CHECK_NEAR(h->world().relation(a, b).v, -40 - rules::relationBonus(h->world(), a, b), 1e-9);
  CHECK(h.shot("faction_diplomacy_war_a"));
  // Вкладка другой стороны показывает то же самое; состояние заблокировано, есть «Заключить перемирие».
  openTab(h, b, "faction.diplomacy");
  CHECK(h->uiRect("dip.status." + std::to_string(a)) != nullptr);
  CHECK(h->uiRect("dip.truce." + std::to_string(a)) != nullptr);
  bool seen = false;
  for (auto& r : rules::relationsOf(h->world(), b))
    if (r.other == a) {
      seen = true;
      CHECK(r.status == RelStatus::War);
      CHECK_NEAR(r.value + r.bonus, -40, 1e-9);
    }
  CHECK(seen);
  CHECK(h.shot("faction_diplomacy_war_b"));
  // Щелчок по заблокированному состоянию меню не открывает; правило тоже отказывает.
  CHECK(clickIn(h, "dip.status." + std::to_string(a)));
  CHECK(h->uiRect("status.item." + std::to_string(int(RelStatus::Alliance))) == nullptr);
  CHECK(h->world().relation(b, a).s == RelStatus::War);
  CHECK(!h->act("Союз", [&](Tx& tx) { rules::setRelation(tx, b, a, -40, RelStatus::Alliance); }));
  // Перемирие со стороны B: итог — союз; видно у A, значение отношений прежнее.
  CHECK(clickIn(h, "dip.truce." + std::to_string(a)));
  h.settle();
  CHECK(h->hasDialog("truce"));
  {
    const RectF* st = h->uiRect("truce.status");
    CHECK(st != nullptr);
    if (st) {
      RectF r = *st;
      h.click(r.x + r.w * 0.5f, r.cy());   // «В союзе» — средний из трёх
    }
  }
  CHECK(h.clickUi("truce.ok"));
  h.settle();
  CHECK(!h->hasDialog("truce"));
  CHECK(h->world().relation(b, a).s == RelStatus::Alliance);
  CHECK_NEAR(rules::relationValue(h->world(), a, b), -40, 1e-9);
  // Фильтр «Гильдии» скрывает государства.
  CHECK(h->uiRect("dip.filter") != nullptr);
  {
    RectF fr = *h->uiRect("dip.filter");
    h.click(fr.x + fr.w * 5 / 6, fr.cy());
    h.step();
    CHECK(h->uiRect("dip.row." + std::to_string(a)) == nullptr);
    h.click(fr.x + fr.w / 6, fr.cy());
    h.step();
  }
  // Отмена трёх шагов возвращает исходное.
  h->undo();
  h->undo();
  h->undo();
  h.step();
  CHECK(h->world().relation(a, b).s != RelStatus::War);
  CHECK(h->world().relation(a, b).s != RelStatus::Alliance);
}

// ---------------------------------------------------------------- 1.b.vi: щелчок по полю отношений меняет отношения
// Модификатор «Отношения за ход» задаётся с одним знаком после запятой (+2,5). После хода отношения дробные (+2,5),
// поле отношений показывает целое. Щелчок в поле и Tab без ввода записывают округлённое значение — у обеих сторон
// (отношения симметричны), появляется шаг отмены «Отношения».
TEST(app_diplomacy_relation_click_rounds) {
  Harness h("audit_tzstates_relround");
  h.demo();
  h.waitMap();
  Id alm = findFaction(h->world(), "Королевство Альмарин");
  Id hel = findFaction(h->world(), "Северный союз Хельдвиг");
  CHECK(alm && hel);
  CHECK(h->act("Модификатор дипломатии", [&](Tx& tx) {
    Id m = rules::createModifier(tx, "Посольство");
    Modifier& md = tx.modifier(m);
    md.fx[size_t(Fx::DiplomacyPerTurn)] = 2.5;
    md.fxMask |= 1u << int(Fx::DiplomacyPerTurn);
    md.targets.push_back(hel);
    tx.faction(alm).modifiers.push_back(m);
  }));
  const double r0 = h->world().relation(alm, hel).v;
  CHECK(h->endTurnNow());
  h.settle();
  h.dropToasts();
  const double r1 = h->world().relation(alm, hel).v;
  CHECK_NEAR(r1, r0 + 2.5, 1e-9);
  const std::string undo0 = h->store.canUndo() ? h->store.undoLabel() : std::string();
  openTab(h, alm, "faction.diplomacy");
  CHECK(clickIn(h, "dip.value." + std::to_string(hel)));
  h.key(Key::Tab);
  h.settle();
  const double r2 = h->world().relation(alm, hel).v;
  const std::string undo1 = h->store.canUndo() ? h->store.undoLabel() : std::string();
  CHECK_MSG(r2 == r1, "щелчок по полю отношений и Tab без ввода изменили отношения Альмарина и Хельдвига: " + fmtNum(r1, 2) + " → " + fmtNum(r2, 2) +
                      " (шаг отмены «" + undo1 + "»)");
  CHECK_MSG(undo1 == undo0, "после щелчка по полю отношений появился шаг отмены «" + undo1 + "»");
  shotClean(h, "audit_tzstates_relation_round");
}

// ---------------------------------------------------------------- дипломатия: одна вера
TEST(app_diplomacy_faith) {
  Harness h("econ2_faith", 1600, 1000);
  h.demo();
  h.dropToasts();
  auto [a, b] = test::twoStates(h->world());
  CHECK(a && b);
  const Id rel = h->world().catalogs->religions.front().id;
  CHECK(h->act("Одна вера", [&](Tx& tx) {
    tx.faction(a).religion = rel;
    tx.faction(b).religion = rel;
    rules::setRelation(tx, a, b, 20, RelStatus::Neutral);
  }));
  CHECK_NEAR(rules::relationValue(h->world(), a, b), 30.0, 1e-9);
  factionTab(h, a, "faction.diplomacy");
  const std::string sb = std::to_string(b);
  CHECK(tr::revealIn(h, "dip.faith." + sb));
  // Поле — действующее значение; правка меняет хранимое на ту же разницу.
  CHECK(tr::revealIn(h, "dip.value." + sb));
  CHECK(h.clickUi("dip.value." + sb));
  h.key(Key::A, ctrl());
  h.type("50");
  h.key(Key::Enter);
  h.settle(20);
  CHECK_NEAR(h->world().relation(a, b).v, 40.0, 1e-9);
  CHECK_NEAR(rules::relationValue(h->world(), a, b), 50.0, 1e-9);
  shotClean(h, "econ2_diplomacy_faith", 60);
}

// ---------------------------------------------------------------- дань и репарации (ТЗ 1.e.ii)
TEST(app_diplomacy_tributes) {
  Harness h("faction_tribute");
  h.demo();
  h.waitMap();
  h.dropToasts();
  // Плательщик репараций в демонстрационном мире.
  Id payer = 0, deal = 0;
  h->world().deals.each([&](const Deal& d) {
    if (!deal && d.kind == DealKind::Reparations && d.status == DealStatus::Active) {
      deal = d.id;
      payer = d.b;
    }
  });
  CHECK(deal != 0);
  openTab(h, payer, "faction.economy");
  CHECK(ensureVisible(h, "tribute.cancel." + std::to_string(deal)));
  CHECK(h.shot("faction_economy_tribute"));
  CHECK(h.clickUi("tribute.cancel." + std::to_string(deal)));
  h.step();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.step();
  CHECK(h->world().deal(deal)->status == DealStatus::Cancelled);
  h->undo();
  h.step();
  CHECK(h->world().deal(deal)->status == DealStatus::Active);
  // «Навязать дань» открывает диалог «tribute» (или сообщает, что его нет).
  h.dropToasts();
  CHECK(clickIn(h, "economy.impose"));
  h.step();
  if (app::findDialog("tribute")) CHECK(h->hasDialog("tribute"));
  else CHECK(!h->toasts().empty());
}

// ---------------------------------------------------------------- 1.e.ii: дань — «отнимается у одного и отдаётся другому»
// Дань 100 золота за ход от Хельдвига Альмарину через диалог «Навязать дань / репарации» вкладки «Экономика».
// У Альмарина модификатор «Королевский указ» (+6 % дохода в казну): получатель получает 106, плательщик отдаёт 100 —
// выплата не сохраняется (золото создаётся из ничего); предпросмотр диалога при этом обещает +100.
TEST(app_diplomacy_tribute_income_modifier) {
  Harness h("audit_tzstates_tribute");
  h.demo();
  h.waitMap();
  Id alm = findFaction(h->world(), "Королевство Альмарин");
  Id hel = findFaction(h->world(), "Северный союз Хельдвиг");
  CHECK(alm && hel);
  CHECK(rules::factionEffects(h->world(), alm)[Fx::IncomePct] > 0);
  auto c0 = rules::calc(h->store.world());
  const double recv0 = c0->faction(alm)->net, pay0 = c0->faction(hel)->net;
  const double t0r = h->world().faction(alm)->treasury(), t0p = h->world().faction(hel)->treasury();

  openTab(h, alm, "faction.economy");
  CHECK(clickIn(h, "economy.impose"));
  h.settle();
  CHECK(h->hasDialog("tribute"));
  CHECK(h.clickUi("tribute.payer"));
  h.type("Хельдвиг");
  h.key(Key::Enter);
  h.settle();
  enterInDialog(h, "tribute.amount", "100");
  enterInDialog(h, "tribute.turns", "3");
  shotClean(h, "audit_tzstates_tribute_dialog");
  // Что обещает диалог: «Доход получателя» = чистый доход + 100.
  const double promised = recv0 + 100;
  CHECK(h.clickUi("tribute.ok"));
  h.settle();
  CHECK(!h->hasDialog("tribute"));
  auto c1 = rules::calc(h->store.world());
  const double recv1 = c1->faction(alm)->net, pay1 = c1->faction(hel)->net;
  CHECK_NEAR(pay1 - pay0, -100, 1e-6);
  CHECK_MSG(std::fabs((recv1 - recv0) - 100) < 1e-6,
        "дань 100/ход: получатель с +6 % дохода получает " + std::to_string(recv1 - recv0) + " вместо 100 (ТЗ 1.e.ii: выплата отнимается у одного и отдаётся другому)");
  CHECK_MSG(std::fabs(promised - recv1) < 1e-6,
        "диалог дани обещает чистый доход получателя " + std::to_string(promised) + ", фактически " + std::to_string(recv1));
  // После хода: сколько золота ушло и сколько пришло именно по дани.
  CHECK(h->endTurnNow());
  h.settle();
  const double dr = h->world().faction(alm)->treasury() - t0r - recv0;   // прирост сверх прежнего чистого дохода
  const double dp = h->world().faction(hel)->treasury() - t0p - pay0;
  CHECK_MSG(std::fabs(dr + dp) < 1e-6, "после хода по дани получено " + std::to_string(dr) + ", уплачено " + std::to_string(-dp));
}
