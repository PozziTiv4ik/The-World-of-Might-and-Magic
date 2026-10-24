// Строки расчёта экономики в интерфейсе (аудит 2026-10-23, передача агента «Экономика»): в провизии — жители и живые
// воины армии, «Погибнет N» при голоде; «Невыплата жалованья» при казне в долгу; прирост населения провинции —
// естественный, модификаторы, предел населения и голод владельца (ProvinceCalc::growthPct); довольство за ход —
// модификаторы и налог сверх нормы (ProvinceCalc::contentDelta).
#include "app/panels/province_common.h"
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

std::shared_ptr<const rules::Calc> calcNow(Harness& h) { return rules::calc(h->world()); }

// Подсказка над элементом после наведения (указатель сначала уходит: после щелчка подсказка ждёт движения).
std::string tipOf(Harness& h, const std::string& mark) {
  const RectF* r = h->uiRect(mark);
  if (!r) return {};
  const RectF rr = *r;
  h.move(rr.x - 40, rr.cy() + 40);
  h.frames(4);
  h.move(rr.cx(), rr.cy());
  h.frames(40);
  return ui::tooltipText();
}

bool hasLine(const std::vector<app::prov::TipLine>& L, std::string_view prefix) {
  for (const app::prov::TipLine& l : L)
    if (l.label.rfind(prefix, 0) == 0) return true;
  return false;
}

// Провинции государства без добычи провизии, запас провизии — только amount одного ресурса.
void provisionOnly(Harness& h, Id st, Id res, double amount) {
  CHECK(h->act("Провизия", [&](Tx& tx) {
    const std::vector<Id> provs = rules::provisionResources(tx.w());
    for (Id pid : tx.w().provinces.ids()) {
      const Province* p = tx.w().province(pid);
      if (p->owner == st && std::find(provs.begin(), provs.end(), p->resource) != provs.end()) tx.province(pid).resource = 0;
    }
    for (Id r : provs) tx.faction(st).res.erase(r);
    tx.faction(st).res[res] = amount;
  }));
}

}  // namespace

// Провизия: едят жители и живые воины; недостача после хода — «Погибнет N»; казна в долгу — «Невыплата жалованья».
TEST(app_economy_lines_provision_unpaid) {
  Harness h("economy_lines_provision", 1440, 1300);
  h.demo();
  const Id st = biggestState(h->world());
  const Id grain = test::idByName(h->world().catalogs->resources, "Зерно");
  CHECK(st && grain);
  provisionOnly(h, st, grain, 1e9);
  {
    auto c = calcNow(h);
    const rules::FactionCalc& fc = *c->faction(st);
    CHECK(fc.provisionSoldiers > 0);
    CHECK_NEAR(fc.provisionNeed, double(fc.population + fc.provisionSoldiers) * schema::kProvisionsPerPerson, 1e-9);
    CHECK_EQ(fc.famineDeaths, i64(0));
    CHECK(!fc.unpaid);
  }
  openTab(h, st, "faction.economy");
  CHECK(ensureVisible(h, "economy.provision.people"));
  CHECK(ensureVisible(h, "economy.provision.soldiers"));
  CHECK(h->uiRect("economy.provision.deaths") == nullptr);
  CHECK(h->uiRect("economy.unpaid") == nullptr);
  CHECK(ensureVisible(h, "economy.provision.need"));
  {
    const std::string tip = tipOf(h, "economy.provision.soldiers");
    CHECK_MSG(tip.find("воины") != std::string::npos, tip);
  }
  // Провизии почти нет: после хода недостача, часть голодающих погибнет.
  provisionOnly(h, st, grain, 1);
  i64 deaths = 0;
  {
    auto c = calcNow(h);
    const rules::FactionCalc& fc = *c->faction(st);
    CHECK(fc.provisionDebtNext > 0);
    deaths = fc.famineDeaths;
    CHECK(deaths > 0);
    CHECK(deaths <= fc.population);
  }
  openTab(h, st, "faction.economy");
  CHECK(ensureVisible(h, "economy.provision.deaths"));
  {
    const std::string tip = tipOf(h, "economy.provision.deaths");
    CHECK_MSG(tip.find("Гибель от голода") != std::string::npos, tip);
  }
  shotClean(h, "economy_lines_famine");
  // Казна в долгу — метка «Невыплата жалованья» с суммой долга.
  CHECK(h->act("Долг", [&](Tx& tx) { tx.faction(st).res[kGold] = -5; }));
  CHECK(calcNow(h)->faction(st)->unpaid);
  openTab(h, st, "faction.economy");
  CHECK(h->uiRect("economy.unpaid") != nullptr);
  {
    const std::string tip = tipOf(h, "economy.unpaid");
    CHECK_MSG(tip.find("Невыплата жалованья") != std::string::npos, tip);
  }
  CHECK(h->act("Казна", [&](Tx& tx) { tx.faction(st).res[kGold] = 100; }));
  openTab(h, st, "faction.economy");
  CHECK(h->uiRect("economy.unpaid") == nullptr);
}

// Прирост населения и довольство за ход провинции — из расчёта: естественный прирост, предел, голод владельца; налог
// сверх нормы снижает довольство.
TEST(app_economy_lines_province_growth_content) {
  Harness h("economy_lines_province", 1440, 1100);
  h.demo();
  const Id st = biggestState(h->world());
  const Id grain = test::idByName(h->world().catalogs->resources, "Зерно");
  CHECK(st && grain);
  provisionOnly(h, st, grain, 1e9);
  // Провинция государства ниже предела населения: положительный прирост.
  Id pid = 0;
  {
    auto c = calcNow(h);
    h->world().provinces.each([&](const Province& p) {
      if (pid || p.sea || p.owner != st || p.occupied) return;
      const rules::ProvinceCalc* pc = c->province(p.id);
      if (pc && pc->population > 0 && pc->growthPct > 0 && pc->population < pc->popCap) pid = p.id;
    });
  }
  CHECK(pid != 0);
  CHECK(h->act("Налог", [&](Tx& tx) {
    tx.faction(st).tax = 30;
    tx.province(pid).localTax = 0;
  }));
  {
    auto c = calcNow(h);
    const rules::ProvinceCalc& pc = *c->province(pid);
    const double norm = rules::constantOf(h->world(), schema::cst::TaxNorm).num;
    const double unrest = rules::constantOf(h->world(), schema::cst::TaxUnrest).num;
    CHECK_NEAR(pc.taxUnrest, -unrest * (pc.taxTotal - norm), 1e-9);
    CHECK(pc.taxUnrest < 0);
    CHECK_NEAR(pc.contentDelta, pc.fx[Fx::ContentmentPerTurn] + pc.taxUnrest, 1e-9);
    const auto content = app::prov::contentLines(h->world(), pc);
    CHECK(hasLine(content, "Налог"));
    CHECK(content.back().total);
    CHECK(pc.growthBase > 0);
    const auto growth = app::prov::growthLines(h->world(), pc);
    CHECK(hasLine(growth, "Естественный прирост"));
    CHECK(hasLine(growth, "Предел"));
    CHECK(!hasLine(growth, "Голод владельца"));
  }
  openProvince(h, pid, "province.population");
  CHECK(ensureVisible(h, "province.growth"));
  CHECK(ensureVisible(h, "province.contentDelta"));
  shotClean(h, "economy_lines_province");
  // Голод владельца обнуляет положительный прирост — отметка остаётся с итогом 0.
  provisionOnly(h, st, grain, 0);
  {
    auto c = calcNow(h);
    const rules::ProvinceCalc& pc = *c->province(pid);
    CHECK(c->faction(st)->provisionDebtNext > 0);
    CHECK_NEAR(pc.growthPct, 0.0, 1e-12);
    CHECK(hasLine(app::prov::growthLines(h->world(), pc), "Голод владельца"));
  }
  openProvince(h, pid, "province.population");
  CHECK(ensureVisible(h, "province.growth"));
}
