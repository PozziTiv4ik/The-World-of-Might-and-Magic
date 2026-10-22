// Сценарии вкладки «Экономика» инспектора провинции: сумма влияния гильдий больше 100 % отклоняется с сообщением,
// предел штабов (5 на провинцию), добавление гильдии, местный налог (общий не меньше 1 % и не больше 100 % — при
// налоге государства 100 % и местном до +100 %), количество ресурса ≥ 0, снимок вкладки, узкий инспектор.
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

namespace {

// Открыть провинцию pid (выбор и показ на карте) на вкладке tab.
void open(Harness& h, Id pid, const char* tab) {
  h->ui.tabOf[app::SelType::Province] = tab;
  h->select(app::SelType::Province, pid, true);
  h.settle();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Province, pid}));
}

double influenceOf(const Province& p, Id g) {
  double s = 0;
  for (auto& i : p.influence)
    if (i.guild == g) s += i.pct;
  return s;
}

// Прибавка маршрутов к базе провинции (доля): +10 % за маршрут и +2,5 % за каждое государство на его пути.
double routeBonusOf(const World& w, Id pid) {
  auto fs = geo::faces(w);
  double bonus = 0;
  w.routes.each([&](const Route& r) {
    if (r.pts.empty()) return;
    std::vector<Id> provs = fs->provincesOnPolyline(r.pts), states;
    if (std::find(provs.begin(), provs.end(), pid) == provs.end()) return;
    for (Id p : provs)
      if (const Province* pr = w.province(p); pr && !pr->sea)
        if (const Faction* o = w.faction(pr->owner); o && o->isState() && std::find(states.begin(), states.end(), o->id) == states.end()) states.push_back(o->id);
    bonus += 0.10 + 0.025 * double(states.size());
  });
  return bonus;
}

void setNumber(Harness& h, const std::string& mark, const std::string& text) {
  const RectF* r = h->uiRect(mark);
  CHECK_MSG(r != nullptr, mark);
  h.click(r->cx(), r->cy());
  h.retype(text);
  h.key(Key::Enter);
  h.settle();
}

}  // namespace

TEST(app_province_influence_over_100_rejected) {
  Harness h("province_influence", 1440, 1500);
  h.demo();
  // Провинция с влиянием двух и более гильдий.
  Id pid = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid && !p.sea && p.influence.size() >= 2) pid = p.id;
  });
  CHECK(pid != 0);
  open(h, pid, "province.economy");
  Province before = *prov(h, pid);
  Id g1 = before.influence[0].guild;
  double others = 0;
  for (auto& i : before.influence)
    if (i.guild != g1) others += i.pct;
  CHECK(others > 0);
  CHECK(h->uiRect("province.inf." + std::to_string(g1)) != nullptr);
  // 100 % первой гильдии при занятой доле других — отказ с сообщением, мир не изменён.
  h.dropToasts();
  setNumber(h, "province.inf." + std::to_string(g1), "100");
  CHECK(hasToast(h, "превысит 100"));
  CHECK_NEAR(influenceOf(*prov(h, pid), g1), influenceOf(before, g1), 1e-9);
  // Ровно свободная доля — принято.
  double free = 100 - others;
  setNumber(h, "province.inf." + std::to_string(g1), fmtNum(free, 1));
  CHECK_NEAR(influenceOf(*prov(h, pid), g1), std::round(free * 10) / 10, 1e-6);
  h.key(Key::Z, ctrl());
  CHECK_NEAR(influenceOf(*prov(h, pid), g1), influenceOf(before, g1), 1e-9);
  // Добавить гильдию без влияния в провинции: свободная доля (до 10 %).
  double sum = 0;
  for (auto& i : prov(h, pid)->influence) sum += i.pct;
  Id missing = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!missing && f.isGuild() && influenceOf(*prov(h, pid), f.id) == 0 &&
        std::find(prov(h, pid)->hqs.begin(), prov(h, pid)->hqs.end(), f.id) == prov(h, pid)->hqs.end())
      missing = f.id;
  });
  if (missing && sum < 99.5) {
    const RectF* add = h->uiRect("province.addGuild");
    CHECK(add != nullptr);
    size_t n0 = prov(h, pid)->influence.size();
    h.click(add->cx(), add->cy());
    h.key(Key::Enter);   // первая гильдия списка
    h.settle();
    CHECK_EQ(prov(h, pid)->influence.size(), n0 + 1);
    const Influence& added = prov(h, pid)->influence.back();
    CHECK(added.pct > 0 && added.pct <= 10);
    double total = 0;
    for (auto& i : prov(h, pid)->influence) total += i.pct;
    CHECK(total <= 100 + 1e-9);
  }
}

TEST(app_province_hq_limit_enforced) {
  Harness h("province_hq", 1440, 1500);
  h.demo();
  Id pid = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid && !p.sea && p.owner) pid = p.id;
  });
  CHECK(pid != 0);
  // Шесть гильдий с влиянием по 10 %, у пяти — штабы.
  std::vector<Id> guilds;
  h->world().factions.each([&](const Faction& f) {
    if (f.isGuild()) guilds.push_back(f.id);
  });
  h->store.transact("Подготовка", [&](Tx& tx) {
    while (guilds.size() < 6) guilds.push_back(rules::createFaction(tx, FactionKind::Guild, "Гильдия " + std::to_string(guilds.size() + 1)));
    Province& p = tx.province(pid);
    p.influence.clear();
    p.hqs.clear();
    for (size_t i = 0; i < 6; i++) rules::setInfluence(tx, pid, guilds[i], 10);
    for (size_t i = 0; i < 5; i++) rules::buildHq(tx, guilds[i], pid);
  });
  open(h, pid, "province.economy");
  CHECK_EQ(prov(h, pid)->hqs.size(), size_t(5));
  // Шестой штаб — отказ с сообщением.
  std::string sixth = "province.hq." + std::to_string(guilds[5]);
  CHECK(h->uiRect(sixth) != nullptr);
  h.dropToasts();
  CHECK(h.clickUi(sixth));
  h.settle();
  CHECK(hasToast(h, "штабов"));
  CHECK_EQ(prov(h, pid)->hqs.size(), size_t(5));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("province_economy_hq"));
  // Закрыть штаб первой гильдии — тогда шестой открывается; у гильдии один штаб (повтор — закрытие).
  CHECK(h.clickUi("province.hq." + std::to_string(guilds[0])));
  h.settle();
  CHECK_EQ(prov(h, pid)->hqs.size(), size_t(4));
  CHECK(h.clickUi(sixth));
  h.settle();
  CHECK_EQ(prov(h, pid)->hqs.size(), size_t(5));
  CHECK(std::find(prov(h, pid)->hqs.begin(), prov(h, pid)->hqs.end(), guilds[5]) != prov(h, pid)->hqs.end());
  // Доход получают только гильдии со штабом.
  auto calc = rules::calc(h->world());
  for (auto& s : calc->province(pid)->guilds) {
    if (s.hq) CHECK(s.gross > 0 || calc->province(pid)->tradeValue == 0);
    else CHECK_EQ(s.gross, 0.0);
  }
  // Убрать гильдию из провинции: влияние и штаб.
  CHECK(h.clickUi("province.infDel." + std::to_string(guilds[5])));
  h.settle();
  CHECK_EQ(influenceOf(*prov(h, pid), guilds[5]), 0.0);
  CHECK(std::find(prov(h, pid)->hqs.begin(), prov(h, pid)->hqs.end(), guilds[5]) == prov(h, pid)->hqs.end());
  h.key(Key::Z, ctrl());
  CHECK_NEAR(influenceOf(*prov(h, pid), guilds[5]), 10, 1e-9);
}

TEST(app_province_tax_and_resource) {
  Harness h("province_tax", 1440, 1500);
  h.demo();
  Id pid = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid && !p.sea && p.owner && p.resource) pid = p.id;
  });
  CHECK(pid != 0);
  open(h, pid, "province.economy");
  double stateTax = rules::calc(h->world())->province(pid)->taxState;
  // Местный налог может быть отрицательным; общий — не меньше 1 %.
  setNumber(h, "province.localTax", "-90");
  CHECK_NEAR(prov(h, pid)->localTax, -90, 1e-9);
  CHECK_NEAR(rules::calc(h->world())->province(pid)->taxTotal, std::max(1.0, stateTax - 90), 1e-9);
  setNumber(h, "province.localTax", "5");
  CHECK_NEAR(rules::calc(h->world())->province(pid)->taxTotal, stateTax + 5, 1e-9);
  // Количество ресурса не бывает отрицательным.
  setNumber(h, "province.amount", "-5");
  CHECK_NEAR(prov(h, pid)->resourceAmount, 0, 1e-9);
  CHECK_NEAR(rules::calc(h->world())->province(pid)->production, std::max(0.0, rules::calc(h->world())->province(pid)->fx[Fx::ResourceFlat]), 1e-9);
  setNumber(h, "province.amount", "40");
  CHECK_NEAR(prov(h, pid)->resourceAmount, 40, 1e-9);
  // Базовая торговая ценность → текущая (маршруты и модификаторы — по формуле).
  setNumber(h, "province.baseTrade", "200");
  const rules::ProvinceCalc* pc = rules::calc(h->world())->province(pid);
  CHECK_NEAR(pc->tradeBase, 200, 1e-9);
  CHECK_NEAR(pc->tradeValue, std::max(0.0, 200 * (1 + pc->fx[Fx::TradePct] / 100 + routeBonusOf(h->world(), pid)) + pc->fx[Fx::TradeFlat] +
                                              double(pc->population) / 100),
             1e-6);
  CHECK_NEAR(pc->tradePop, double(pc->population) / 100, 1e-9);
  CHECK(h->uiRect("province.tradeValue") != nullptr);
  CHECK(h->uiRect("province.income") != nullptr);
  // Золото — до тысячных (ТЗ «Фиксы», п.13): количество ресурса и базовая ценность принимают три знака.
  setNumber(h, "province.amount", "12,345");
  CHECK_NEAR(prov(h, pid)->resourceAmount, 12.345, 1e-9);
  setNumber(h, "province.baseTrade", "150,125");
  CHECK_NEAR(prov(h, pid)->baseTrade, 150.125, 1e-9);
}

// ТЗ 1.d.iv: общий налог = налог государства (0…100 %) + местный (−100…100 %) без верхней границы. При общем
// налоге больше 100 % чистый доход гильдии со штабом отрицателен, а налог государству превышает ценность провинции.
TEST(app_province_total_tax_capped) {
  Harness h("audit_tzmil_tax");
  h.demo();
  const World& w = h->world();
  // Провинция со штабом гильдии и её влиянием.
  Id pid = 0, guild = 0;
  w.provinces.each([&](const Province& p) {
    if (pid || p.sea || !p.owner) return;
    for (const Influence& in : p.influence)
      if (in.pct > 0 && std::find(p.hqs.begin(), p.hqs.end(), in.guild) != p.hqs.end()) {
        pid = p.id;
        guild = in.guild;
        return;
      }
  });
  CHECK(pid && guild);
  Id owner = w.province(pid)->owner;
  // Значения в пределах полей интерфейса: налог государства до 100 %, местный налог до 100 %.
  CHECK(h->act("Налоги", [&](Tx& tx) {
    tx.faction(owner).tax = 100;
    tx.province(pid).localTax = 100;
  }));
  auto c = rules::calc(h->world());
  const rules::ProvinceCalc* pc = c->province(pid);
  CHECK(pc != nullptr);
  double net = 0;
  for (const rules::GuildShare& s : pc->guilds)
    if (s.guild == guild) net = s.net;
  std::printf("  [audit tzmil] общий налог %.1f %%, ценность %.2f, налог провинции %.2f, налог гильдий %.2f, чистый доход гильдии %.2f\n", pc->taxTotal,
              pc->tradeValue, pc->provinceTax, pc->guildTax, net);
  CHECK_MSG(pc->taxTotal <= 100.0 + 1e-9, "общий налог провинции больше 100 %");
  CHECK_MSG(net >= 0, "чистый доход гильдии со штабом отрицателен (налог больше валового дохода)");
  CHECK_MSG(pc->provinceTax + pc->guildTax <= pc->tradeValue + 1e-6, "налог государству больше торговой ценности провинции");
  // Снимок вкладки «Экономика» провинции.
  h->ui.tabOf[app::SelType::Province] = "province.economy";
  h->select(app::SelType::Province, pid);
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tzmil_tax_over_100"));
}

// ---------------------------------------------------------------- 1.d.iv / 1.e.i: общий налог без верхней границы
// Налог государства до 100 % и местный до +100 % дают общий налог до 200 %: государство забирает больше торговой
// ценности провинции, чистый доход штаба гильдии становится отрицательным.
TEST(app_province_total_tax_capped_by_state_tax) {
  Harness h("audit_tzstates_tax");
  h.demo();
  h.waitMap();
  Id alm = findFaction(h->world(), "Королевство Альмарин");
  CHECK(alm);
  openTab(h, alm, "faction.economy");
  CHECK(typeNumber(h, "economy.tax", "100"));
  CHECK_NEAR(h->world().faction(alm)->tax, 100, 1e-9);
  // Провинция Альмарина со штабом гильдии; местный налог +30 (поле провинции допускает до +100).
  Id pid = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid && p.owner == alm && !p.hqs.empty() && !p.influence.empty()) pid = p.id;
  });
  CHECK(pid);
  CHECK(h->act("Местный налог", [&](Tx& tx) { tx.province(pid).localTax = 30; }));
  h.step();
  auto c = rules::calc(h->store.world());
  const rules::ProvinceCalc* pc = c->province(pid);
  CHECK(pc != nullptr);
  double guildNet = 0;
  for (const rules::GuildShare& s : pc->guilds)
    if (s.hq) guildNet += s.net;
  CHECK_MSG(pc->taxTotal <= 100 + 1e-9, "общий налог провинции " + std::to_string(pc->taxTotal) + " % — больше 100 %");
  CHECK_MSG(pc->provinceTax + pc->guildTax <= pc->tradeValue + 1e-9,
        "налог провинции " + std::to_string(pc->provinceTax + pc->guildTax) + " больше её торговой ценности " + std::to_string(pc->tradeValue));
  CHECK_MSG(guildNet >= -1e-9, "чистый доход штабов гильдий в провинции отрицательный: " + std::to_string(guildNet));
  openTab(h, alm, "faction.economy");
  shotClean(h, "audit_tzstates_tax_over_100");
}

TEST(app_province_economy_screen) {
  Harness h("province_economy_shot", 1440, 1000);
  h.demo();
  // Провинция со штабами и влиянием нескольких гильдий.
  Id pid = 0;
  size_t best = 0;
  h->world().provinces.each([&](const Province& p) {
    size_t score = p.hqs.size() * 10 + p.influence.size();
    if (!p.sea && score > best) {
      best = score;
      pid = p.id;
    }
  });
  CHECK(pid != 0);
  open(h, pid, "province.economy");
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("province_economy"));
  // Нижняя часть вкладки: прокрутка инспектора.
  const RectF* ins = h->uiRect("inspector");
  CHECK(ins != nullptr);
  h.wheel(ins->cx(), ins->bottom() - 120, -12);
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("province_economy_guilds"));
}

TEST(app_province_narrow_inspector) {
  Harness h("province_narrow", 1280, 900);
  h.demo();
  h->ui.inspectorWidth = 320;
  Id pid = 0;
  size_t best = 0;
  h->world().provinces.each([&](const Province& p) {
    size_t score = p.hqs.size() * 10 + p.influence.size();
    if (!p.sea && score > best) {
      best = score;
      pid = p.id;
    }
  });
  CHECK(pid != 0);
  open(h, pid, "province.economy");
  const RectF* ins = h->uiRect("inspector");
  CHECK(ins != nullptr);
  CHECK_NEAR(ins->w, 320, 0.5);
  h.wheel(ins->cx(), ins->bottom() - 120, -16);
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("province_economy_narrow"));
}
