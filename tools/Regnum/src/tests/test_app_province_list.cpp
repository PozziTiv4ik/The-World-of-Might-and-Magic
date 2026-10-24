// Сценарии панели «Провинции» (поиск, отбор по владельцу и морские, сортировка, выбор с показом на карте,
// контекстное меню с удалением) и вкладки «Модификаторы» (добавление модификатора, сводка эффектов, модификатор только
// с глобальными эффектами помечен и не предлагается), снимки.
#include "app/widgets.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;

namespace {

// Провинции, строки которых видны в списке (по отметкам прошлого кадра), сверху вниз.
std::vector<Id> visibleRows(Harness& h) {
  std::vector<std::pair<float, Id>> rows;
  h->world().provinces.each([&](const Province& p) {
    if (const RectF* r = h->uiRect("provinces.row." + std::to_string(p.id))) rows.push_back({r->y, p.id});
  });
  std::sort(rows.begin(), rows.end());
  std::vector<Id> out;
  for (auto& r : rows) out.push_back(r.second);
  return out;
}

}  // namespace

TEST(app_province_list_filter_search_select) {
  Harness h("province_list", 1440, 1000);
  h.demo();
  // Ctrl+1 — панель «Провинции».
  h.key(Key::D1, ctrl());
  h.settle();
  CHECK_EQ(h->ui.drawer, std::string("provinces"));
  std::vector<Id> all = visibleRows(h);
  CHECK(all.size() >= 8);
  CHECK(h->uiRect("provinces.count") != nullptr);
  // По умолчанию — по названию.
  for (size_t i = 1; i < all.size(); i++) CHECK(compareRu(prov(h, all[i - 1])->name, prov(h, all[i])->name) <= 0);
  // Поиск по уникальному названию.
  const World& w = h->world();
  Id pick = 0;
  std::string name;
  w.provinces.each([&](const Province& p) {
    if (pick || p.sea || p.name.size() < 10) return;
    int same = 0;
    w.provinces.each([&](const Province& q) { same += utf8::matches(q.name + " " + q.capital + " " + w.factionName(q.owner), p.name) ? 1 : 0; });
    if (same == 1) {
      pick = p.id;
      name = p.name;
    }
  });
  CHECK(pick != 0);
  CHECK(h.clickUi("provinces.search"));
  h.type(name);
  h.settle();
  std::vector<Id> found = visibleRows(h);
  CHECK_EQ(found.size(), size_t(1));
  CHECK_EQ(found[0], pick);
  // Щелчок — выбор и показ на карте (камера подходит к провинции).
  Vec2 label = geo::faces(h->world())->shape(pick)->label;
  auto dist = [&] {
    const map::View& v = h->map().view();
    return std::hypot(v.cx - label.x, v.cy - label.y);
  };
  double d0 = dist();
  CHECK(h.clickUi("provinces.row." + std::to_string(pick)));
  CHECK(h->ui.sel == (app::Selection{app::SelType::Province, pick}));
  h.settle();
  CHECK(dist() < d0);
  CHECK(h->uiRect("inspector") != nullptr);
  // Очистить поиск.
  CHECK(h.clickUi("provinces.search"));
  h.key(Key::A, ctrl());
  h.key(Key::Backspace);
  h.settle();
  CHECK(visibleRows(h).size() >= 8);
  // Отбор по государству: поиск в списке и Enter.
  Id state = prov(h, pick)->owner;
  CHECK(state != 0);
  CHECK(h.clickUi("provinces.filter"));
  h.type(w.factionName(state));
  h.key(Key::Enter);
  h.settle();
  std::vector<Id> mine = visibleRows(h);
  CHECK(!mine.empty());
  for (Id id : mine) CHECK_EQ(prov(h, id)->owner, state);
  int owned = 0;
  w.provinces.each([&](const Province& p) { owned += !p.sea && p.owner == state ? 1 : 0; });
  CHECK(int(mine.size()) == owned || int(mine.size()) >= 10);
  // Сортировка по населению (меню): по убыванию.
  CHECK(h.clickUi("provinces.sort"));
  h.settle();
  CHECK(h.clickUi("provinces.sort.1"));
  h.settle();
  h.move(10, 500);
  std::vector<Id> byPop = visibleRows(h);
  auto calc = rules::calc(h->world());
  for (size_t i = 1; i < byPop.size(); i++) CHECK(calc->province(byPop[i - 1])->population >= calc->province(byPop[i])->population);
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("province_list"));
  // Направление — по возрастанию.
  CHECK(h.clickUi("provinces.dir"));
  std::vector<Id> asc = visibleRows(h);
  for (size_t i = 1; i < asc.size(); i++) CHECK(calc->province(asc[i - 1])->population <= calc->province(asc[i])->population);
  // Морские провинции.
  CHECK(h.clickUi("provinces.filter"));
  h.type("Морские");
  h.key(Key::Enter);
  h.settle();
  std::vector<Id> sea = visibleRows(h);
  CHECK(!sea.empty());
  for (Id id : sea) CHECK(prov(h, id)->sea);
  // Контекстное меню: удалить (в режиме правки границ, ТЗ 1.a.ii) — подтверждение — отмена Ctrl+Z.
  h->setEditBorders(true);
  h.settle();
  Id victim = sea.front();
  const RectF* row = h->uiRect("provinces.row." + std::to_string(victim));
  CHECK(row != nullptr);
  h.click(row->cx(), row->cy(), platform::MouseRight);
  h.settle();
  CHECK(h.clickUi("provinces.ctx.delete"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(prov(h, victim) == nullptr);
  CHECK(h->uiRect("provinces.row." + std::to_string(victim)) == nullptr);
  h.key(Key::Z, ctrl());
  h.settle();
  CHECK(prov(h, victim) != nullptr);
  CHECK(h->uiRect("provinces.row." + std::to_string(victim)) != nullptr);
}

TEST(app_province_modifiers_tab) {
  Harness h("province_modifiers", 1440, 1000);
  h.demo();
  // Провинция с собственным модификатором и модификаторами государства.
  Id pid = 0;
  h->world().provinces.each([&](const Province& p) {
    const Faction* f = h->world().faction(p.owner);
    if (!pid && !p.sea && !p.modifiers.empty() && f && !f->modifiers.empty()) pid = p.id;
  });
  if (!pid)
    h->world().provinces.each([&](const Province& p) {
      if (!pid && !p.sea && !p.modifiers.empty()) pid = p.id;
    });
  CHECK(pid != 0);
  h->ui.tabOf[app::SelType::Province] = "province.modifiers";
  h->select(app::SelType::Province, pid, true);
  h.settle();
  CHECK(h->uiRect("province.effects") != nullptr);
  auto calc = rules::calc(h->world());
  CHECK(!calc->province(pid)->fx.sources.empty());
  // Добавить модификатор из списка (правило: срок по умолчанию модификатора).
  size_t n0 = prov(h, pid)->modifiers.size();
  CHECK(h->uiRect("province.mods") != nullptr);
  const RectF* add = h->uiRect("province.modAdd");
  CHECK(add != nullptr);
  h.click(add->cx(), add->cy());
  h.key(Key::Down);
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(prov(h, pid)->modifiers.size(), n0 + 1);
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("province_modifiers"));
  h.key(Key::Z, ctrl());
  CHECK_EQ(prov(h, pid)->modifiers.size(), n0);
}

// ТЗ 1.g.ii.2: глобальные эффекты «применяются только к государству». Глобальные эффекты модификатора провинции
// действуют на её владельца (RULES.md §1), поэтому такой модификатор в списке провинции не помечается «не действует»;
// у рас (ModScope::Local) по-прежнему действуют только локальные эффекты.
TEST(app_province_modifiers_global_only_marked) {
  Harness h("audit_tbmt_global_on_province");
  h.demo();
  // Модификатор только с глобальными эффектами из демонстрационного мира.
  Id global = 0;
  h->world().modifiers.each([&](const Modifier& m) {
    if (global) return;
    bool anyLocal = false, anyGlobal = false;
    for (int f = 0; f < kFxCount; f++)
      if (m.has(Fx(f))) (schema::kEffects[f].local ? anyLocal : anyGlobal) = true;
    if (anyGlobal && !anyLocal) global = m.id;
  });
  CHECK(global != 0);
  BuildPick pk = pickBuild(h->world());
  CHECK(pk.pid != 0);
  const rules::Effects fxBefore = rules::factionEffects(h->world(), pk.owner);
  CHECK(h->act("Модификатор провинции", [&](Tx& tx) {
    auto& v = tx.province(pk.pid).modifiers;
    if (std::find(v.begin(), v.end(), global) == v.end()) v.push_back(global);
  }));
  // Глобальные эффекты модификатора провинции действуют на её владельца.
  const rules::Effects fxAfter = rules::factionEffects(h->world(), pk.owner);
  const Modifier& gm = *h->world().modifier(global);
  for (int f = 0; f < kFxCount; f++)
    if (gm.has(Fx(f)) && !schema::kEffects[f].hero) CHECK_NEAR(fxAfter[Fx(f)] - fxBefore[Fx(f)], gm.get(Fx(f)), 1e-6);
  h->ui.tabOf[app::SelType::Province] = "province.modifiers";
  h->select(app::SelType::Province, pk.pid);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tbmt_global_mod_on_province"));
  // Фишка такого модификатора в провинции без предупреждения; у рас (только локальные эффекты) он не действует.
  CHECK(!app::w::modifierInert(gm, app::w::ModScope::Province));
  CHECK(app::w::modifierInert(gm, app::w::ModScope::Local));
  CHECK(!app::w::modifierInert(gm, app::w::ModScope::Any));
  CHECK_MSG(h->uiRect("mods.warn." + std::to_string(global)) == nullptr,
            std::string("модификатор «") + gm.name + "» с глобальными эффектами помечен в провинции как недействующий");
}
