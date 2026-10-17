// Regnum — уникальность ID виджетов: места, где возникали предупреждения журнала «повторяющийся ID виджета», и их
// следствия (кнопка с повторным ID теряет щелчок). Каждый тест обходит одно место; регрессионные проверки — повторов
// нет, кнопки работают. Полный обход всех вкладок, панелей и окон — test_app_fuzz_sweep.cpp.
#include <cstdio>

#include "rules/rules.h"
#include "tests/test_app_military_util.h"

using namespace rg;
using namespace rg::apptest;
using app::SelType;

namespace {

constexpr std::string_view kDupId = "повторяющийся ID";

std::string pctText(double v) { return fmtPct(v, std::fabs(v - std::round(v)) > 1e-6 ? 1 : 0); }

}  // namespace

// Вкладка «Экономика» провинции: подписи «Налог государства» и «Общий налог» — ui::label с подсказкой, ID = текст.
// При местном налоге 0 % тексты совпадают («10 %» и «10 %») → один ID на два элемента.
TEST(app_widget_ids_province_tax) {
  Harness h("widget_ids_tax");
  h.demo();
  h.waitMap();
  LogTap tap("widget-ids-tax");
  auto calc = rules::calc(h->world());
  int same = 0, sameWarn = 0, diff = 0, diffWarn = 0;
  std::vector<Id> ids;
  h->world().provinces.each([&](const Province& p) {
    if (!p.sea) ids.push_back(p.id);
  });
  for (Id pid : ids) {
    const rules::ProvinceCalc* pc = calc->province(pid);
    if (!pc) continue;
    h->select(SelType::Province, pid);
    h->ui.tabOf[SelType::Province] = "province.economy";
    h.settle();
    auto w = tap.warnings(kDupId);
    bool eq = pctText(pc->taxState) == pctText(pc->taxTotal);
    (eq ? same : diff)++;
    if (!w.empty()) (eq ? sameWarn : diffWarn)++;
    if (!w.empty() && (sameWarn + diffWarn) <= 3)
      std::printf("  [widget ids] провинция %u: налог государства «%s», общий «%s» → %s\n", unsigned(pid), pctText(pc->taxState).c_str(),
                  pctText(pc->taxTotal).c_str(), w[0].c_str());
  }
  std::printf("  [widget ids] экономика провинций: тексты налогов совпадают в %d (предупреждений %d), различаются в %d (предупреждений %d)\n",
              same, sameWarn, diff, diffWarn);
  CHECK_EQ(sameWarn + diffWarn, 0);
}

// Вкладка фракции «Торговля» и выдвижная панель «Торговля»: повтор ID в карточках сделок.
TEST(app_widget_ids_trade) {
  Harness h("widget_ids_trade");
  h.demo();
  h.waitMap();
  LogTap tap("widget-ids-trade");
  int warns = 0;
  for (Id fid : h->world().factions.ids()) {
    h->select(SelType::Faction, fid);
    h->ui.tabOf[SelType::Faction] = "faction.trade";
    h.settle();
    if (const RectF* r = h->uiRect("inspector"))
      for (int k = 0; k < 3; k++) h.wheel(r->cx(), r->y + r->h * 0.6f, -6);
    for (auto& m : tap.warnings(kDupId)) {
      warns++;
      std::printf("  [widget ids] вкладка «Торговля» фракции %u (%s): %s\n", unsigned(fid), h->world().factionName(fid).c_str(), m.c_str());
      h.shot("fuzz_dup_trade_" + std::to_string(fid));
    }
  }
  h->clearSelection();
  h->openSection("economy");   // окно переговоров и торговли
  h.settle();
  for (auto& m : tap.warnings(kDupId)) {
    warns++;
    std::printf("  [widget ids] окно «Переговоры и торговля»: %s\n", m.c_str());
  }
  std::printf("  [widget ids] торговля: предупреждений %d\n", warns);
  CHECK_EQ(warns, 0);
}

// Следствие повтора ID на вкладке «Торговля» фракции без сделок: верхняя кнопка «Новая сделка» и кнопка пустого
// состояния «Новая сделка» имеют один ID. Нажатие на нижнюю: при отпускании первая (верхняя) кнопка с тем же ID
// снимает «активный» элемент раньше, щелчок теряется — редактор сделки не открывается.
TEST(app_widget_ids_trade_empty_click) {
  Harness h("widget_ids_trade_click");
  h.demo();
  h.waitMap();
  Id fid = 0;
  for (Id f : h->world().factions.ids()) {
    bool any = false;
    h->world().deals.each([&](const Deal& d) {
      if (d.a == f || d.b == f) any = true;
    });
    if (!any && !fid) fid = f;
  }
  CHECK(fid != 0);
  auto openTrade = [&] {
    h->select(SelType::Faction, fid, true);   // панель справа от карты
    h->ui.tabOf[SelType::Faction] = "faction.trade";
    h.settle();
  };
  openTrade();
  const RectF* top = h->uiRect("faction.trade.new");
  CHECK(top != nullptr);
  if (!top) return;
  RectF t = *top;
  // Кнопка пустого состояния — под значком и подписью «Сделок пока нет.» по центру вкладки; ищем её по пикселям
  // не надёжно, поэтому перебираем точки по центральной оси ниже верхней кнопки.
  const RectF* ins = h->uiRect("inspector");
  CHECK(ins != nullptr);
  if (!ins) return;
  const RectF insR = *ins;   // копия: указатель на прямоугольник живёт только до следующего кадра
  bool opened = false;
  float hitY = -1;
  for (float y = t.bottom() + 120; y < t.bottom() + 220 && !opened; y += 6) {
    h->closeEditor();
    h.settle();
    openTrade();
    h.click(insR.cx(), y);
    if (h->ui.editor == "trade") {
      opened = true;
      hitY = y;
    }
  }
  // Контроль: верхняя кнопка работает.
  h->closeEditor();
  h.settle();
  openTrade();
  h.shot("fuzz_dup_trade_empty");
  h.click(t.cx(), t.cy());
  bool topOk = h->ui.editor == "trade";
  std::printf("  [widget ids] фракция %u без сделок: верхняя «Новая сделка» %s; кнопка пустого состояния %s (y=%.0f)\n", unsigned(fid),
              topOk ? "открывает редактор" : "НЕ открывает", opened ? "открывает редактор" : "НЕ открывает редактор ни в одной точке", double(hitY));
  CHECK(topOk);
  CHECK(opened);
}

namespace {

constexpr int kBuildingParts = 3;   // большое окно — тесты app_widget_ids_buildings_wide_1..3

// Дерево построек: выбор каждой карточки (боковая панель уровня) с прокруткой; предупреждения журнала печатаются,
// REGNUM_AUDIT=1 — роняют тест. small — малое окно 900×600 с интерфейсом 150 %; карточки k, k + n, ... общего дерева.
void buildingCards(const std::string& tag, bool small, int k, int n) {
  const char* view = small ? "900x600 ui1.5" : "1440x900";
  Harness h("widget_ids_bt_" + tag, small ? 900 : 1440, small ? 600 : 900, 1, !small);
  if (small) h->setUiScale(1.5f);
  h.demo();
  LogTap tap("widget-ids-bt-" + tag);
  h->openEditor("buildings", 0);
  h.settle();
  int warns = 0, i = 0, cards = 0;
  std::vector<Id> ids;
  h->world().buildings.each([&](const Building& b) {
    if (!b.owner && i++ % n == k) ids.push_back(b.id);
  });
  for (Id id : ids) {
    const RectF* r = h->uiRect("bt.node." + std::to_string(id));
    if (!r) continue;
    h.click(r->cx(), r->cy());
    cards++;
    for (auto& m : tap.warnings()) {
      warns++;
      std::printf("  [widget ids] buildings(0) %s, карточка %u: %s\n", view, unsigned(id), m.c_str());
    }
    if (const RectF* side = h->uiRect("editor")) {
      for (int s = 0; s < 4; s++) {
        h.wheel(side->right() - 120, side->y + side->h * 0.6f, -6);
        for (auto& m : tap.warnings()) {
          warns++;
          std::printf("  [widget ids] buildings(0) %s, карточка %u, прокрутка %d: %s\n", view, unsigned(id), s, m.c_str());
        }
      }
    }
  }
  std::printf("  [widget ids] buildings(0) %s, часть %d/%d: карточек %d из %d, предупреждений %d\n", view, k + 1, n, cards, int(ids.size()), warns);
  if (test::auditStrict()) CHECK_EQ(warns, 0);
}

void buildingsWide(int k) { buildingCards("wide_" + std::to_string(k + 1), false, k, kBuildingParts); }

}  // namespace

TEST(app_widget_ids_buildings_wide_1) { buildingsWide(0); }
TEST(app_widget_ids_buildings_wide_2) { buildingsWide(1); }
TEST(app_widget_ids_buildings_wide_3) { buildingsWide(2); }
TEST(app_widget_ids_buildings_small) { buildingCards("small", true, 0, 1); }

// Вкладки государства, гильдии и государственной гильдии (в том числе скрытые для неё) и инспектор войска, флота,
// союзного войска: повторов ID нет.
TEST(app_widget_ids_faction_tabs) {
  Harness h("widget_ids_faction_tabs");
  h.demo();
  LogTap tap("widget-ids-faction-tabs");
  const World& w = h->world();
  Id hel = factionByName(w, "Хельдвиг");
  Id amber = factionByName(w, "Янтарная");
  Id comp = factionByName(w, "Альмарская");
  int dups = 0;
  auto open = [&](app::SelType t, Id id, const char* tabId) {
    h->ui.tabOf[t] = tabId;
    h->select(t, id);
    h.settle();
    h.dropToasts();
    h.settle();
    for (auto& m : tap.warnings(kDupId)) {
      dups++;
      std::printf("  [widget ids] вкладка %s, %lld: %s\n", tabId, (long long)id, m.c_str());
    }
  };
  for (Id f : {hel, amber, comp})
    for (const char* t : {"faction.overview", "faction.council", "faction.provinces", "faction.hqs", "faction.economy", "faction.diplomacy",
                          "faction.trade", "faction.army", "faction.fleet", "faction.heroes", "faction.tech", "faction.modifiers", "faction.guilds",
                          "faction.buildings"})
      open(SelType::Faction, f, t);
  Id allied = 0;
  w.armies.each([&](const Army& a) {
    if (!allied && a.allied()) allied = a.id;
  });
  for (Id ar : {armyOf(w, hel, ArmyKind::Army), armyOf(w, hel, ArmyKind::Fleet), allied}) open(SelType::Army, ar, "army.units");
  CHECK_EQ(dups, 0);
}
