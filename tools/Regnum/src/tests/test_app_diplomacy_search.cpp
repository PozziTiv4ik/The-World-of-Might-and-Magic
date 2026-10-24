// Вкладка «Дипломатия» (аудит 2026-10-23, 8.6 и 5.16): поиск по названию; порядок — войны и союзы первыми (по
// умолчанию), по названию, по отношениям; разрыв союза — с подтверждением.
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

namespace {

float rowY(Harness& h, Id other) {
  const RectF* r = h->uiRect("dip.row." + std::to_string(other));
  return r ? r->y : -1e9f;
}

}  // namespace

TEST(app_diplomacy_search_and_sort) {
  Harness h("diplomacy_search", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id hel = findFaction(h->world(), "Северный союз Хельдвиг");
  const Id alm = findFaction(h->world(), "Королевство Альмарин");
  const Id ketra = findFaction(h->world(), "Империя Валь-Кетра");
  const Id korven = findFaction(h->world(), "Республика Корвен");
  const Id solmar = findFaction(h->world(), "Теократия Солмар");
  CHECK(hel && alm && ketra && korven && solmar);
  CHECK(h->world().relation(hel, ketra).s == RelStatus::War);
  CHECK(h->world().relation(hel, alm).s == RelStatus::Alliance);
  h->ui.tabOf[app::SelType::Faction] = "faction.diplomacy";
  h->select(app::SelType::Faction, hel);
  h.settle();
  h->setPage(true);
  h.settle();
  // По умолчанию: войны, затем союзы, затем остальные.
  CHECK(rowY(h, ketra) < rowY(h, alm));
  CHECK(rowY(h, alm) < rowY(h, korven));
  CHECK(rowY(h, alm) < rowY(h, solmar));
  shotClean(h, "diplomacy_sorted_wars");
  // Поиск.
  CHECK(h.clickUi("dip.search"));
  h.type("Корв");
  h.settle();
  CHECK(h->uiRect("dip.row." + std::to_string(korven)) != nullptr);
  CHECK(h->uiRect("dip.row." + std::to_string(alm)) == nullptr);
  CHECK(h->uiRect("dip.row." + std::to_string(ketra)) == nullptr);
  h.key(Key::A, ctrl());
  h.key(Key::Backspace);
  h.settle();
  CHECK(h->uiRect("dip.row." + std::to_string(alm)) != nullptr);
  // По названию.
  CHECK(h.clickUi("dip.sort"));
  h.settle();
  CHECK(h.clickUi("dip.sort.1"));
  h.settle();
  CHECK((rowY(h, korven) < rowY(h, solmar)) == (compareRu(h->world().factionName(korven), h->world().factionName(solmar)) < 0));
  CHECK((rowY(h, ketra) < rowY(h, alm)) == (compareRu(h->world().factionName(ketra), h->world().factionName(alm)) < 0));
  // По отношениям: союзник (+72) — первым, противник (−64) — ниже.
  CHECK(h.clickUi("dip.sort"));
  h.settle();
  CHECK(h.clickUi("dip.sort.2"));
  h.settle();
  CHECK(rowY(h, alm) < rowY(h, korven));
  CHECK(rowY(h, korven) < rowY(h, ketra) || rules::relationValue(h->world(), hel, korven) <= rules::relationValue(h->world(), hel, ketra));
  // Разрыв союза — с подтверждением.
  CHECK(clickIn(h, "dip.status." + std::to_string(alm)));
  CHECK(h.clickUi("status.item." + std::to_string(int(RelStatus::Neutral))));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h->world().relation(hel, alm).s == RelStatus::Alliance);
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->world().relation(hel, alm).s == RelStatus::Neutral);
}
