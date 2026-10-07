// Итог и отчёт хода после ТЗ 2026-10-07: изменения эссенций элементов (TurnFactionLine::essences), недостача
// провизии (provisionDebt, «Голод»), предупреждения «не хватает эссенций на содержание элементалей». Записи хроники
// экономики разбираются по смыслу: «провизия закончилась… — голод» — голод (предупреждение), «недостача провизии
// покрыта — голод закончился» и преобразование ресурсов — события экономики, долг казны — долги.
#include "app/dialogs/turn_ui.h"
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;
namespace tu = rg::app::turnui;

namespace {

Id resByName(const World& w, std::string_view name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}
Id essByName(const World& w, std::string_view name) {
  for (const CatalogItem& c : w.catalogs->essences)
    if (c.name == name) return c.id;
  return 0;
}

struct Setup {
  Id famine = 0, relief = 0, ess = 0, convert = 0, debt = 0;
  Id water = 0, fuel = 0;
};

// Пять государств: A — провизия кончится (голод), B — недостача прошлых ходов будет покрыта, C — эссенции не хватит
// на элементалей, D — постройка преобразования, E — казна уйдёт в долг.
Setup prepare(Harness& h) {
  Setup s;
  s.famine = findFaction(h->world(), "Королевство Альмарин");
  s.relief = findFaction(h->world(), "Северный союз Хельдвиг");
  s.ess = findFaction(h->world(), "Республика Корвен");
  s.convert = findFaction(h->world(), "Вольные города Ольсты");
  s.debt = findFaction(h->world(), "Орда Таргаш");
  s.water = essByName(h->world(), "Эссенция воды");
  s.fuel = resByName(h->world(), "Топливо");
  const Id grain = resByName(h->world(), "Зерно"), wood = resByName(h->world(), "Древесина");
  CHECK(s.famine && s.relief && s.ess && s.convert && s.debt && s.water && s.fuel && grain && wood);
  CHECK(h->act("Сценарий хода", [&](Tx& tx) {
    const std::vector<Id> provs = rules::provisionResources(tx.w());
    auto isProv = [&](Id r) { return std::find(provs.begin(), provs.end(), r) != provs.end(); };
    for (Id pid : tx.w().provinces.ids()) {
      const Province* p = tx.w().province(pid);
      if (p->owner == s.famine && isProv(p->resource)) tx.province(pid).resource = 0;
    }
    for (Id r : provs) tx.faction(s.famine).res.erase(r);
    tx.faction(s.relief).provisionDebt = 10;
    tx.faction(s.relief).res[grain] = 100000;
    const Id row = rules::addArmyRow(tx, s.ess, UnitType::Elementals, "Водные элементали", 10, 0);
    rules::setRowEssUpkeep(tx, s.ess, row, s.water, 2);
    rules::setEssence(tx, s.ess, s.water, 5);
    tx.faction(s.debt).res[kGold] = 0.5;
    rules::addFleetRow(tx, s.debt, ShipType::ShipOfLine, "Великая армада", 50, 100);
    tx.faction(s.convert).res[wood] = 1000;
  }));
  Id b = 0;
  CHECK(h->act("Углежогня", [&](Tx& tx) {
    b = rules::createBuilding(tx, 0, "Углежогня");
    rules::setBuildingRole(tx, b, rules::BuildingRole::Convert, true);
    Recipe r;
    r.in = {ResAmount{wood, 40}};
    r.out = ResAmount{s.fuel, 10};
    r.turns = 1;
    rules::setRecipe(tx, b, r);
  }));
  bool placed = false;
  for (Id pid : h->world().provinces.ids()) {
    const Province* p = h->world().province(pid);
    if (placed || !p || p->sea || p->owner != s.convert) continue;
    placed = h->act("Постройка", [&](Tx& tx) { rules::placeBuilding(tx, pid, b, 1); });
  }
  CHECK(placed);
  h.dropToasts();
  return s;
}

bool mentions(const std::vector<const LogEntry*>& list, const World& w, Id faction) {
  for (const LogEntry* e : list)
    if (std::find(e->factions.begin(), e->factions.end(), faction) != e->factions.end() &&
        e->text.find(w.factionName(faction)) != std::string::npos)
      return true;
  return false;
}

const rules::TurnFactionLine* lineOf(const rules::TurnReport& rep, Id f) {
  for (const auto& l : rep.factions)
    if (l.faction == f) return &l;
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------- разбор хроники хода
TEST(app_eco_turn_digest_economy) {
  Harness h("eco_turn_digest");
  h.demo();
  const Setup s = prepare(h);
  Tx tx(h->world());
  const rules::TurnReport rep = rules::endTurn(tx);
  const World after = std::move(tx).finish();
  const tu::TurnDigest d = tu::digest(after, rep, tu::kPageAll);
  CHECK(mentions(d.famine, after, s.famine));     // провизия закончилась — голод
  CHECK(mentions(d.economy, after, s.relief));    // недостача покрыта — голод закончился: не предупреждение
  CHECK(!mentions(d.famine, after, s.relief) && !mentions(d.debts, after, s.relief));
  CHECK(mentions(d.essences, after, s.ess));      // не хватает эссенций на содержание элементалей
  CHECK(mentions(d.economy, after, s.convert));   // преобразование ресурсов — событие
  CHECK(mentions(d.debts, after, s.debt));        // казна ушла в долг
  CHECK(!mentions(d.debts, after, s.famine) && !mentions(d.debts, after, s.ess));
  CHECK(d.warnings() >= 3);
  CHECK(d.events() >= 2);
  // Строки итога: недостача провизии после хода и изменение эссенций.
  const rules::TurnFactionLine* la = lineOf(rep, s.famine);
  const rules::TurnFactionLine* lc = lineOf(rep, s.ess);
  CHECK(la && lc);
  if (la) {
    CHECK(la->provisionDebt > 0);
    CHECK(tu::hasStockChanges(*la));
  }
  if (lc) {
    CHECK(lc->essences.count(s.water) == 1);
    if (lc->essences.count(s.water)) CHECK(lc->essences.at(s.water) < 0);
  }
  CHECK(after.faction(s.ess)->essence(s.water) < 0);
  CHECK_NEAR(after.faction(s.relief)->provisionDebt, 0, 1e-9);
}

// ---------------------------------------------------------------- окна «Завершить ход» и «Итоги хода»
TEST(app_eco_turn_confirm_and_report) {
  HideTestRegs hide;
  Harness h("eco_turn_windows", 1440, 1000);
  h.demo();
  h.waitMap();
  const Setup s = prepare(h);
  h->endTurn();
  h.settle();
  CHECK(h->hasDialog("turn.confirm"));
  // Предупреждения: голод и эссенции — отдельными группами.
  CHECK(h.clickUi("turn.confirm.warnings"));
  h.settle();
  CHECK(h->uiRect("turn.confirm.warn.famine") != nullptr);
  CHECK(h->uiRect("turn.confirm.warn.essences") != nullptr);
  CHECK(h->uiRect("turn.confirm.warn.debts") != nullptr);
  CHECK(h.shot("eco_turn_confirm_warnings"));
  // Вкладка «Ресурсы»: строки с изменениями запасов — голод, эссенции, ресурсы.
  const RectF* tabs = h->uiRect("turn.confirm.tabs");
  CHECK(tabs != nullptr);
  if (tabs) {
    const RectF r = *tabs;
    h.click(r.x + r.w * 0.375f, r.cy());
    h.settle();
  }
  CHECK(h->uiRect("turn.confirm.stock." + std::to_string(s.famine)) != nullptr);
  CHECK(h->uiRect("turn.confirm.stock." + std::to_string(s.ess)) != nullptr);
  CHECK(h.shot("eco_turn_confirm_stocks"));
  // Завершить ход и открыть отчёт.
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK_EQ(h->world().turn(), 2);
  h.dropToasts();
  CHECK(h->openDialog("turn.report"));
  h.settle();
  CHECK(h->hasDialog("turn.report"));
  CHECK(h->uiRect("turn.report.warnings") != nullptr);
  CHECK(h.shot("eco_turn_report"));
  // Карточки фракций с запасами: прокрутить тело отчёта до карточки государства с голодом.
  bool card = false;
  for (int i = 0; i < 30 && !card; i++) {
    card = h->uiRect("turn.report.stock." + std::to_string(s.famine)) != nullptr && h->uiRect("turn.report.stock." + std::to_string(s.ess)) != nullptr;
    if (card) break;
    const RectF* body = h->uiRect("turn.report.body");
    if (!body) break;
    h.wheel(body->cx(), body->cy(), -2);
    h.frames(10);
  }
  CHECK(card);
  CHECK(h.shot("eco_turn_report_cards"));
}
