// Сценарии пустоши нежити и осквернения (ТЗ «Виды государств», п.5–11; «Модификаторы», 1.24–1.25): кнопки видны,
// только когда правило разрешает (вид владельца, модификаторы провинции); «Обратить в пустошь нежити» — население в
// трупы, население пустоши только 0, стройка и доход с ценности — только государству нежити; «Очистить пустошь» —
// окно числа переселенцев; «Осквернить» — демоническая энергия, энергия за ход; «Очистить осквернённую провинцию».
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

void setKind(Harness& h, Id state, StateKind k) {
  CHECK(h->act("Вид государства", [&](Tx& tx) { rules::setStateKind(tx, state, k); }));
  h.settle();
}

double stockOf(Harness& h, Id f, std::string_view key) {
  Id r = rules::resourceId(h->world(), key);
  return r ? h->world().faction(f)->stock(r) : 0.0;
}

}  // namespace

TEST(app_economy_wasteland) {
  HideTestRegs hide;
  Harness h("economy_wasteland", 1440, 1200);
  h.demo();
  const Id st = biggestState(h->world());
  const Id pid = populousProvince(h->world(), st);
  CHECK(st && pid);
  const i64 pop0 = popOf(*prov(h, pid));
  const i64 statePop0 = rules::statePopulation(h->world(), st);
  CHECK(pop0 > 1000);
  // Государство живых: обратить в пустошь нельзя — кнопки нет.
  openProvince(h, pid, "province.overview");
  CHECK(h->uiRect("province.waste") == nullptr);
  CHECK(h->uiRect("province.desecrate") == nullptr);
  CHECK(h->uiRect("province.cleanseWaste") == nullptr);
  // Государство нежити: кнопка есть, подтверждение, население — в трупы владельцу.
  setKind(h, st, StateKind::Undead);
  CHECK(clickIn(h, "province.waste"));
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(rules::provinceHas(h->world(), pid, schema::mod::UndeadWaste));
  CHECK_EQ(popOf(*prov(h, pid)), i64(0));
  CHECK_NEAR(stockOf(h, st, schema::kResCorpses), double(pop0), 1e-9);
  CHECK(h->uiRect("province.waste") == nullptr);    // уже пустошь
  CHECK(h->uiRect("province.desecrate") == nullptr);
  CHECK(h->uiRect("province.land") != nullptr);     // метка «Пустошь нежити»
  CHECK(!rules::calc(h->world())->province(pid)->tradeBlocked);   // государство нежити получает доход
  shotClean(h, "economy_wasteland_undead");
  // Население пустоши — только 0: поле ограничено, правило отказывает.
  openProvince(h, pid, "province.population");
  CHECK(typeNumber(h, "province.racePop.0", "5000"));
  CHECK_EQ(popOf(*prov(h, pid)), i64(0));
  CHECK(!h->act("Численность", [&](Tx& tx) { rules::setRacePop(tx, pid, prov(h, pid)->races[0].race, 10); }));

  // Владелец — государство живых: доход с ценности не идёт, строить нельзя, кнопка «Очистить пустошь нежити».
  setKind(h, st, StateKind::Living);
  CHECK(rules::calc(h->world())->province(pid)->tradeBlocked);
  openProvince(h, pid, "province.economy");
  CHECK(h->uiRect("province.tradeBlocked") != nullptr);
  shotClean(h, "economy_wasteland_trade_blocked");
  openProvince(h, pid, "province.buildings");
  CHECK(h->uiRect("prov.buildBlock") != nullptr);
  u64 v = h->store.version();
  CHECK(h.clickUi("prov.build"));
  CHECK(!h->hasDialog("build.picker"));
  CHECK_EQ(h->store.version(), v);
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), pid)) {
    CHECK(!o.can);
    CHECK(!o.reasons.empty() && o.reasons.front().find("пустоши нежити") != std::string::npos);
  }
  shotClean(h, "economy_wasteland_build_block");
  openProvince(h, pid, "province.overview");
  CHECK(h->uiRect("province.waste") == nullptr);
  CHECK(clickIn(h, "province.cleanseWaste"));
  CHECK(h->hasDialog("province.settlers"));
  CHECK(h->uiRect("settlers.count") != nullptr);
  shotClean(h, "economy_wasteland_settlers");
  CHECK(h.clickUi("settlers.count"));
  h.retype("1000");
  h.key(Key::Tab);
  h.settle();
  CHECK(h.clickUi("settlers.ok"));
  h.settle();
  CHECK(!h->hasDialog("province.settlers"));
  CHECK(!rules::provinceHas(h->world(), pid, schema::mod::UndeadWaste));
  CHECK_EQ(popOf(*prov(h, pid)), i64(1000));
  CHECK_EQ(rules::statePopulation(h->world(), st), statePop0 - pop0);   // переселенцы — из других провинций
  CHECK_EQ(h->store.undoLabel(), std::string("Очистить пустошь нежити"));
  h.key(Key::Z, ctrl());
  CHECK(rules::provinceHas(h->world(), pid, schema::mod::UndeadWaste));
  CHECK_EQ(popOf(*prov(h, pid)), i64(0));
}

TEST(app_economy_desecration) {
  HideTestRegs hide;
  Harness h("economy_desecration", 1440, 1200);
  h.demo();
  const Id st = biggestState(h->world());
  const Id pid = populousProvince(h->world(), st);
  CHECK(st && pid);
  const i64 pop0 = popOf(*prov(h, pid));
  const double tenth = std::floor(double(pop0) * 0.10);
  setKind(h, st, StateKind::Demonic);
  openProvince(h, pid, "province.overview");
  CHECK(h->uiRect("province.waste") == nullptr);
  CHECK(h->uiRect("province.cleanseDesecration") == nullptr);
  // Осквернить: 1000 × (10 % населения) энергии владельцу; осквернённая провинция даёт 100 × (10 %) за ход.
  CHECK(clickIn(h, "province.desecrate"));
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(rules::provinceHas(h->world(), pid, schema::mod::Desecrated));
  CHECK_NEAR(stockOf(h, st, schema::kResEnergy), 1000 * tenth, 1e-6);
  CHECK_EQ(popOf(*prov(h, pid)), pop0);
  auto c = rules::calc(h->world());
  CHECK_NEAR(c->province(pid)->energy, 100 * tenth, 1e-6);
  CHECK(!c->province(pid)->tradeBlocked);
  const Id energy = rules::resourceId(h->world(), schema::kResEnergy);
  CHECK(energy != 0);
  CHECK(c->faction(st)->resources.at(energy).production >= 100 * tenth - 1e-6);
  CHECK(h->uiRect("province.desecrate") == nullptr);
  CHECK(h->uiRect("province.cleanseDesecration") == nullptr);   // демоны не очищают
  CHECK(h->uiRect("province.land") != nullptr);
  shotClean(h, "economy_desecrated_demonic");
  // Ход: энергия пополняется.
  const double before = stockOf(h, st, schema::kResEnergy);
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK(stockOf(h, st, schema::kResEnergy) >= before + 100 * tenth - 1e-6);
  // Государство нежити (и живых) может очистить осквернённую провинцию; пока не очищена — без дохода с ценности.
  setKind(h, st, StateKind::Undead);
  CHECK(rules::calc(h->world())->province(pid)->tradeBlocked);
  openProvince(h, pid, "province.overview");
  CHECK(h->uiRect("province.waste") != nullptr);   // правило: нежить обращает в пустошь любую свою провинцию без пустоши
  CHECK(clickIn(h, "province.cleanseDesecration"));
  h.settle();
  CHECK(!rules::provinceHas(h->world(), pid, schema::mod::Desecrated));
  CHECK_EQ(h->store.undoLabel(), std::string("Очистить осквернённую провинцию"));
  CHECK(h->uiRect("province.cleanseDesecration") == nullptr);
  shotClean(h, "economy_desecration_cleansed");
  // Государство живых тоже очищает осквернённую провинцию.
  h.key(Key::Z, ctrl());
  setKind(h, st, StateKind::Living);
  CHECK(rules::provinceHas(h->world(), pid, schema::mod::Desecrated));
  CHECK(ensureVisible(h, "province.cleanseDesecration"));
  CHECK(h->uiRect("province.waste") == nullptr);
  CHECK(h->uiRect("province.desecrate") == nullptr);
}
