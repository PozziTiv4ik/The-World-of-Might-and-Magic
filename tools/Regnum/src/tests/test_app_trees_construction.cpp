// Сценарии строительства в провинции (ТЗ 1.f.i–ii): слоты, выбор постройки (нехватка ресурсов — недоступно),
// начало строительства списывает стоимость, отмена возвращает ровно уплаченное и тому, кто платил (после смены
// владельца, правки цены и множителя), завершение по ходам, улучшение, снос, уникальные постройки — только у своего
// государства (чужая — с причиной), морская провинция не строит, одна стройка в провинции, чума и лечение.
#include "app/editors/buildings.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;
namespace tr = rg::apptest::trees;

namespace {

// Провинция государства с двумя свободными слотами и постройкой общего дерева, которой в ней нет (многоуровневой).
struct Pick {
  Id pid = 0, owner = 0, fresh = 0, built = 0;
};
Pick pickProvince(const World& w) {
  Pick r;
  auto calc = rules::calc(w);
  w.provinces.each([&](const Province& p) {
    if (r.pid || p.sea || !p.owner) return;
    const Faction* f = w.faction(p.owner);
    if (!f || !f->isState()) return;
    const rules::ProvinceCalc* pc = calc->province(p.id);
    if (!pc || pc->slots < int(p.buildings.size()) + 2) return;
    Id fresh = 0, built = 0;
    w.buildings.each([&](const Building& b) {
      if (b.owner || b.levels.size() < 2) return;
      bool have = false;
      for (const ProvBuilding& pb : p.buildings) have = have || pb.building == b.id;
      if (!have && !fresh && b.requires_.empty()) fresh = b.id;
    });
    for (const ProvBuilding& pb : p.buildings)
      if (!pb.constructing && !built) built = pb.building;
    if (fresh && built) r = Pick{p.id, p.owner, fresh, built};
  });
  return r;
}

void openTab(Harness& h, Id pid) {
  h->ui.tabOf[app::SelType::Province] = "province.buildings";
  h->select(app::SelType::Province, pid);
  h.settle();
}

// Открыть выбор строительства и оставить в списке одну постройку (поиск по названию).
void openPicker(Harness& h, const std::string& name) {
  clickRect(h, "prov.build");
  CHECK(h->hasDialog("build.picker"));
  clickRect(h, "build.search");
  h.type(name);
  h.settle();
}

Id addCostModifier(app::App& a, Id pid, double pct) {
  Id mid = 0;
  CHECK(a.act("Стоимость строительства", [&](Tx& tx) {
    mid = rules::createModifier(tx, "Аудит: стоимость");
    Modifier& m = tx.modifier(mid);
    m.fxMask |= 1u << int(Fx::BuildCostPct);
    m.fx[size_t(int(Fx::BuildCostPct))] = pct;
    tx.province(pid).modifiers.push_back(mid);
  }));
  return mid;
}

Id otherState(const World& w, Id not1) {
  Id r = 0;
  w.factions.each([&](const Faction& f) {
    if (!r && f.isState() && f.id != not1) r = f.id;
  });
  return r;
}

}  // namespace

TEST(app_trees_construction_flow) {
  Harness h("trees_construction", 1440, 1200);
  h.demo();
  h.dropToasts();
  Pick pk = pickProvince(h->world());
  CHECK(pk.pid != 0);
  const std::string freshName = h->world().building(pk.fresh)->name;
  openTab(h, pk.pid);
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK_EQ(h->ui.tabOf[app::SelType::Province], std::string("province.buildings"));
  CHECK(h.shot("trees_province_buildings"));

  // Нехватка ресурсов — постройка недоступна: кнопка не действует, мир прежний.
  setStock(h.a(), pk.owner, 0);
  h.settle();
  openPicker(h, freshName);
  CHECK(h->uiRect("build.option." + std::to_string(pk.fresh)) != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_build_picker_lack"));
  u64 v = h->store.version();
  clickRect(h, "build.option." + std::to_string(pk.fresh));
  CHECK_EQ(h->store.version(), v);
  CHECK(provBuilding(h->world(), pk.pid, pk.fresh) == nullptr);
  h.key(Key::Escape);
  h.settle();
  CHECK(!h->hasDialog("build.picker"));

  // Ресурсов достаточно — строительство начато, стоимость списана.
  setStock(h.a(), pk.owner, 10000);
  std::map<Id, double> cost;
  int turns = 0;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), pk.pid))
    if (o.building == pk.fresh) {
      cost = o.cost;
      turns = o.turns;
      CHECK(o.can);
    }
  CHECK(!cost.empty());
  h.settle();
  clickRect(h, "prov.build");
  CHECK(h->hasDialog("build.picker"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_build_picker"));
  CHECK(h.clickUi("dialog.cancel"));
  h.settle();
  openPicker(h, freshName);
  clickRect(h, "build.option." + std::to_string(pk.fresh));
  CHECK(!h->hasDialog("build.picker"));
  const ProvBuilding* pb = provBuilding(h->world(), pk.pid, pk.fresh);
  CHECK(pb != nullptr);
  CHECK(pb->constructing);
  CHECK_EQ(pb->left, turns);
  for (auto& [res, c] : cost) CHECK_NEAR(h->world().faction(pk.owner)->stock(res), 10000 - c, 1e-6);
  h->toasts().clear();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_province_constructing"));

  // Отмена — стоимость возвращается полностью (с подтверждением).
  clickRect(h, "prov.cancel." + std::to_string(pk.fresh));
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(provBuilding(h->world(), pk.pid, pk.fresh) == nullptr);
  for (auto& [res, c] : cost) CHECK_NEAR(h->world().faction(pk.owner)->stock(res), 10000, 1e-6);
  // Отмена отмены — снова строится; ходы доводят до конца.
  undo(h);
  CHECK(provBuilding(h->world(), pk.pid, pk.fresh) != nullptr);
  for (int t = 0; t < turns; t++) {
    CHECK(provBuilding(h->world(), pk.pid, pk.fresh)->constructing);
    CHECK(h->endTurnNow());
    h->toasts().clear();   // уведомления хода не держат кадры
  }
  pb = provBuilding(h->world(), pk.pid, pk.fresh);
  CHECK(pb != nullptr && !pb->constructing);
  CHECK_EQ(pb->level, 1);
  h.settle();

  // Улучшение до второго уровня — стоимость следующего уровня.
  setStock(h.a(), pk.owner, 10000);
  std::map<Id, double> up;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), pk.pid))
    if (o.building == pk.fresh) {
      CHECK(o.upgrade);
      CHECK_EQ(o.level, 2);
      up = o.cost;
    }
  h.settle();
  clickRect(h, "prov.upgrade." + std::to_string(pk.fresh));
  pb = provBuilding(h->world(), pk.pid, pk.fresh);
  CHECK(pb && pb->constructing && pb->level == 2);
  for (auto& [res, c] : up) CHECK_NEAR(h->world().faction(pk.owner)->stock(res), 10000 - c, 1e-6);
  h->toasts().clear();

  // Снос готовой постройки — с подтверждением; отменяется Ctrl+Z.
  h.settle();
  clickRect(h, "prov.demolish." + std::to_string(pk.built));
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(provBuilding(h->world(), pk.pid, pk.built) == nullptr);
  undo(h);
  CHECK(provBuilding(h->world(), pk.pid, pk.built) != nullptr);
}

TEST(app_trees_construction_slots_and_unique) {
  Harness h("trees_construction_unique", 1440, 1000);
  h.demo();
  h.dropToasts();
  // Уникальная постройка и её государство.
  Id uniq = 0, owner = 0;
  h->world().buildings.each([&](const Building& b) {
    if (!uniq && b.owner) {
      uniq = b.id;
      owner = b.owner;
    }
  });
  CHECK(uniq && owner);
  const std::string uname = h->world().building(uniq)->name;
  // Провинция владельца (без этой постройки, со свободным слотом) и провинция другого государства.
  auto calc = rules::calc(h->world());
  Id own = 0, other = 0;
  h->world().provinces.each([&](const Province& p) {
    if (p.sea || !p.owner) return;
    const rules::ProvinceCalc* pc = calc->province(p.id);
    if (!pc || pc->slots <= int(p.buildings.size())) return;
    bool has = false;
    for (const ProvBuilding& pb : p.buildings) has = has || pb.building == uniq;
    if (p.owner == owner && !has && !own) own = p.id;
    const Faction* f = h->world().faction(p.owner);
    if (p.owner != owner && f && f->isState() && !other) other = p.id;
  });
  CHECK(own && other);
  setStock(h.a(), owner, 10000);
  openTab(h, own);
  openPicker(h, uname);
  CHECK(h->uiRect("build.option." + std::to_string(uniq)) != nullptr);
  CHECK(h.clickUi("dialog.cancel"));
  h.settle();
  CHECK(!h->hasDialog("build.picker"));
  openTab(h, other);
  openPicker(h, uname);
  CHECK(h->uiRect("build.option." + std::to_string(uniq)) == nullptr);
  CHECK(h.clickUi("dialog.cancel"));
  h.settle();
  CHECK(!h->hasDialog("build.picker"));
  // Свободный слот в сетке — тоже вход в выбор строительства.
  int used = int(h->world().province(other)->buildings.size());
  clickRect(h, "prov.slot." + std::to_string(used));
  CHECK(h->hasDialog("build.picker"));
  CHECK(h.clickUi("dialog.cancel"));
  h.settle();
  // Переполнение слотов (модификатор отнял слоты, например в данных прежней версии): предупреждение, «Построить»
  // недоступно. Отнимается с запасом (у столицы ещё +3 слота «Столицы государства») и в обход App::act — действие в
  // редакторе спросило бы о сносе лишних построек (ТЗ «Фиксы», п.10).
  Id crowded = 0;
  h->world().provinces.each([&](const Province& p) {
    const Faction* f = h->world().faction(p.owner);
    if (!crowded && !p.sea && f && f->isState() && p.buildings.size() >= 2) crowded = p.id;
  });
  CHECK(crowded != 0);
  h->store.transact("Минус слоты", [&](Tx& tx) {
    Id m = rules::createModifier(tx, "Разруха");
    Modifier& md = tx.modifier(m);
    md.fx[size_t(int(Fx::Slots))] = -20;
    md.fxMask |= 1u << int(Fx::Slots);
    tx.province(crowded).modifiers.push_back(m);
  });
  openTab(h, crowded);
  CHECK(rules::calc(h->world())->province(crowded)->slots < int(h->world().province(crowded)->buildings.size()));
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_province_overfull"));
  clickRect(h, "prov.build");
  CHECK(!h->hasDialog("build.picker"));
}

TEST(app_trees_construction_sea_owner_filters) {
  Harness h("trees_construction_filters", 1440, 1000);
  h.demo();
  h.dropToasts();
  // Морская провинция — вкладки «Постройки» нет (ТЗ 1.a.iii: без информации); сухопутная — есть.
  const app::TabDef* tab = tabDef("province.buildings");
  CHECK(tab != nullptr && tab->visible != nullptr);
  Id sea = 0, land = 0;
  h->world().provinces.each([&](const Province& p) {
    if (p.sea && !sea) sea = p.id;
    if (!p.sea && p.owner && !land) land = p.id;
  });
  CHECK(sea && land);
  CHECK(!tab->visible(h.a(), sea));
  CHECK(tab->visible(h.a(), land));
  // Выбор строительства по имени диалога для морской провинции сразу закрывается.
  CHECK(h->openDialog("build.picker", sea));
  h.settle();
  CHECK(!h->hasDialog("build.picker"));
  // «Только доступные»: при пустой казне постройки с ценой скрываются; без фильтра видны с причинами.
  Pick pk = pickProvince(h->world());
  CHECK(pk.pid != 0);
  setStock(h.a(), pk.owner, 0);
  openTab(h, pk.pid);
  clickRect(h, "prov.build");
  CHECK(h->hasDialog("build.picker"));
  CHECK(h->uiRect("build.option." + std::to_string(pk.fresh)) != nullptr);
  clickRect(h, "build.onlyAvail", 0.9f, 0.5f);
  CHECK(h->uiRect("build.option." + std::to_string(pk.fresh)) == nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_build_picker_only_avail"));
  CHECK(h.clickUi("dialog.cancel"));
  h.settle();
  // Провинция без владельца: строить некому — «Построить» недоступна.
  CHECK(h->act("Без владельца", [&](Tx& tx) { rules::setProvinceOwner(tx, pk.pid, 0); }));
  h.settle();
  clickRect(h, "prov.build");
  CHECK(!h->hasDialog("build.picker"));
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), pk.pid)) CHECK(!o.can);
}

// ТЗ 1.f.ii / RULES §9: отмена строительства должна возвращать ровно уплаченное. Возврат считается по ТЕКУЩЕМУ
// множителю стоимости провинции (rules/build.cpp cancelBuilding → levelCost(..., ctxOf(...).factor)), поэтому:
// начать при −100 % (бесплатно), снять модификатор, отменить — государство получает полную цену из ничего.
TEST(app_trees_construction_refund_equals_paid) {
  Harness h("audit_tbmt_refund");
  h.demo();
  BuildPick pk = pickBuild(h->world());
  CHECK(pk.pid != 0);
  setStock(h.a(), pk.owner, 10000);
  // Строительство бесплатно: итог −100 % с учётом эффектов государства (в демо-мире «Слабый контроль»: +10 %).
  const double extra = (rules::calc(h->world())->province(pk.pid)->buildCostFactor - 1) * 100;
  Id mid = addCostModifier(h.a(), pk.pid, -100 - extra);
  CHECK_NEAR(rules::calc(h->world())->province(pk.pid)->buildCostFactor, 0.0, 1e-9);
  const double before = test::stockSum(h->world(), pk.owner);
  CHECK(h->act("Построить", [&](Tx& tx) { rules::startBuilding(tx, pk.pid, pk.building); }));
  const double paid = before - test::stockSum(h->world(), pk.owner);
  CHECK_NEAR(paid, 0, 1e-6);
  // Модификатор убран (например, технология забыта или провинция сменила модификаторы).
  CHECK(h->act("Убрать модификатор", [&](Tx& tx) { { auto& v = tx.province(pk.pid).modifiers; v.erase(std::remove(v.begin(), v.end(), mid), v.end()); } }));
  CHECK(h->act("Отменить", [&](Tx& tx) { rules::cancelBuilding(tx, pk.pid, pk.building); }));
  const double after = test::stockSum(h->world(), pk.owner);
  CHECK_MSG(std::fabs(after - before) < 1e-6,
               "отмена бесплатной стройки вернула " + fmtNum(after - before, 2) + " ресурсов, хотя уплачено " + fmtNum(paid, 2));

  // Обратный случай: заплачено по полной цене, затем стоимость −50 % — возвращается половина.
  const double b2 = test::stockSum(h->world(), pk.owner);
  CHECK(h->act("Построить", [&](Tx& tx) { rules::startBuilding(tx, pk.pid, pk.building); }));
  const double paid2 = b2 - test::stockSum(h->world(), pk.owner);
  CHECK(paid2 > 0);
  addCostModifier(h.a(), pk.pid, -50);
  CHECK(h->act("Отменить", [&](Tx& tx) { rules::cancelBuilding(tx, pk.pid, pk.building); }));
  const double lost = b2 - test::stockSum(h->world(), pk.owner);
  CHECK_MSG(std::fabs(lost) < 1e-6, "после отмены потеряно " + fmtNum(lost, 2) + " из уплаченных " + fmtNum(paid2, 2) +
                                           " — интерфейс обещает «Стоимость вернётся полностью»");
}

// ТЗ 1.f.ii: ресурсы «вычитаются из списка ресурсов государства при её возведении». Провинция перешла к другому
// государству во время стройки: стройка продолжается для нового владельца, а «Отменить» возвращает стоимость
// НОВОМУ владельцу (rules/build.cpp cancelBuilding: refund → p.owner на момент отмены), хотя платил прежний.
TEST(app_trees_construction_refund_to_payer) {
  Harness h("audit_tbmt_more_refund_owner");
  h.demo();
  BuildPick pk = pickBuild(h->world());
  CHECK(pk.pid != 0);
  Id other = otherState(h->world(), pk.owner);
  CHECK(other != 0);
  setStock(h.a(), pk.owner, 10000);
  setStock(h.a(), other, 0);
  const double a0 = test::stockSum(h->world(), pk.owner);
  CHECK(h->act("Построить", [&](Tx& tx) { rules::startBuilding(tx, pk.pid, pk.building); }));
  const double paid = a0 - test::stockSum(h->world(), pk.owner);
  CHECK(paid > 0);
  CHECK(h->act("Смена владельца", [&](Tx& tx) { rules::setProvinceOwner(tx, pk.pid, other); }));
  const double b0 = test::stockSum(h->world(), other);
  CHECK(h->act("Отменить", [&](Tx& tx) { rules::cancelBuilding(tx, pk.pid, pk.building); }));
  const double gotB = test::stockSum(h->world(), other) - b0;
  CHECK_NEAR(test::stockSum(h->world(), pk.owner), a0, 1e-6);  // плательщику (прежнему владельцу) — ровно уплаченное
  CHECK_MSG(gotB < 1e-6, "новый владелец получил возврат " + fmtNum(gotB, 2) + " за стройку, которую оплатил прежний (" +
                                fmtNum(paid, 2) + "); прежний владелец ничего не получил");
}

// Возврат считается по ТЕКУЩЕЙ цене уровня в дереве построек, а не по уплаченной: цену уровня поправили в дереве
// во время стройки (обычная работа ведущего) — «Отменить» возвращает новую цену (rules/build.cpp cancelBuilding,
// rules/entities.cpp dropBuildingEverywhere). Из 150 золота получается сколько угодно.
TEST(app_trees_construction_refund_after_cost_edit) {
  Harness h("audit_tbmt_more_refund_cost");
  h.demo();
  BuildPick pk = pickBuild(h->world());
  CHECK(pk.pid != 0);
  setStock(h.a(), pk.owner, 10000);
  const double a0 = test::stockSum(h->world(), pk.owner);
  CHECK(h->act("Построить", [&](Tx& tx) { rules::startBuilding(tx, pk.pid, pk.building); }));
  const double paid = a0 - test::stockSum(h->world(), pk.owner);
  CHECK(paid > 0);
  CHECK(h->act("Цена уровня", [&](Tx& tx) {
    for (auto& [res, v] : tx.building(pk.building).levels[0].cost) v *= 100;
  }));
  CHECK(h->act("Отменить", [&](Tx& tx) { rules::cancelBuilding(tx, pk.pid, pk.building); }));
  const double net = test::stockSum(h->world(), pk.owner) - a0;
  CHECK_MSG(std::fabs(net) < 1e-6, "уплачено " + fmtNum(paid, 2) + ", после отмены государство в плюсе на " + fmtNum(net, 2) +
                                         " — возврат взят из изменённой цены уровня");
}

// ТЗ 1.a.iii: морская провинция «не содержит информации». Галочка «Морская провинция» сохраняет постройки,
// и завершение хода продолжает строительство в морской провинции (rules/turn.cpp, шаг 4 — без проверки sea),
// записывая в хронику «Достроено … в провинции» для моря; отменить такую стройку из интерфейса нельзя
// (вкладка «Постройки» у морской провинции скрыта), ресурсы остаются списанными.
TEST(app_trees_construction_sea_province_stops) {
  Harness h("audit_tbmt_sea_build");
  h.demo();
  BuildPick pk = pickBuild(h->world());
  CHECK(pk.pid != 0);
  setStock(h.a(), pk.owner, 10000);
  CHECK(h->act("Построить", [&](Tx& tx) { rules::startBuilding(tx, pk.pid, pk.building); }));
  CHECK(h->act("Сделать провинцию морской", [&](Tx& tx) { tx.province(pk.pid).sea = true; }));
  const size_t logBefore = h->store.world().log.size();
  for (int i = 0; i < 12; i++) {
    CHECK(h->endTurnNow());
    h->toasts().clear();
  }
  bool built = false;
  for (const ProvBuilding& pb : h->store.world().province(pk.pid)->buildings)
    if (pb.building == pk.building) built = !pb.constructing;
  int seaLogs = 0;
  h->store.world().log.each([&](const LogEntry& e) {
    if (e.province == pk.pid && e.text.find("Достроено") != std::string::npos) seaLogs++;
  });
  (void)logBefore;
  CHECK_MSG(!built && seaLogs == 0, "в морской провинции строительство продолжилось и завершилось; записей хроники «Достроено»: " +
                                           std::to_string(seaLogs));
}

// Провинция перешла к другому государству вместе с уникальной постройкой прежнего владельца: кнопка «Улучшить»
// недоступна, но подсказка говорит «Начать строительство следующего уровня», причина не показана
// (app/panels/construction.cpp: optionOf() == nullptr для постройки вне дерева нового владельца).
TEST(app_trees_construction_foreign_unique_upgrade_reason) {
  Harness h("audit_tbmt_foreign_unique", 1440, 1200);
  h.demo();
  Id uniq = 0, from = 0;
  h->world().buildings.each([&](const Building& b) {
    if (!uniq && b.owner && b.levels.size() >= 2) {
      uniq = b.id;
      from = b.owner;
    }
  });
  if (!uniq) {
    h->world().buildings.each([&](const Building& b) {
      if (!uniq && b.owner) {
        uniq = b.id;
        from = b.owner;
      }
    });
    CHECK(uniq != 0);
    CHECK(h->act("Второй уровень", [&](Tx& tx) { tx.building(uniq).levels.push_back(BuildingLevel{}); }));
  }
  Id pid = 0, to = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid && !p.sea && p.owner == from) pid = p.id;
  });
  h->world().factions.each([&](const Faction& f) {
    if (!to && f.isState() && f.id != from) to = f.id;
  });
  CHECK(pid && to);
  // Смена владельца сносит уникальные постройки прежнего (ТЗ «Общие доработки», п.13), поэтому постройка ставится
  // после неё — как в данных, правленных вручную; прямо в хранилище, мимо предупреждения о слотах (ТЗ «Фиксы», п.10).
  CHECK(h->act("Смена владельца", [&](Tx& tx) { rules::setProvinceOwner(tx, pid, to); }));
  h->store.transact("Постройка", [&](Tx& tx) {
    auto& v = tx.province(pid).buildings;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const ProvBuilding& x) { return x.building == uniq; }), v.end());
    v.push_back(ProvBuilding{uniq, 1, false, 0});
  });
  setStock(h.a(), to, 10000);
  h->ui.tabOf[app::SelType::Province] = "province.buildings";
  h->select(app::SelType::Province, pid);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tbmt_foreign_unique"));
  const RectF* r = h->uiRect("prov.upgrade." + std::to_string(uniq));
  CHECK(r != nullptr);
  bool offered = false;
  for (auto& o : rules::buildOptions(h->world(), pid)) offered = offered || o.building == uniq;
  CHECK(!offered);
  // Регрессия: причина показана рядом с кнопкой (и в её подсказке).
  CHECK_MSG(h->uiRect("prov.upgradeWhy." + std::to_string(uniq)) != nullptr,
            "«Улучшить» для уникальной постройки прежнего владельца отключена без причины; подсказка — «Начать строительство следующего уровня»");
}

// ---------------------------------------------------------------- постройки провинции: чума, целительство, одна стройка
TEST(app_trees_construction_plague) {
  Harness h("econ2_plague", 1600, 1000);
  h.demo();
  h.dropToasts();
  const tr::StateProv sp = tr::stateWithFreeSlots(h->world(), 3);
  CHECK(sp.state && sp.province);
  const Id plagueEss = test::idByName(h->world().catalogs->essences, "Эссенция чумы"), water = test::idByName(h->world().catalogs->essences, "Эссенция воды");
  CHECK(plagueEss && water);
  Id heal = 0, plague = 0;
  CHECK(h->act("Постройки чумы и целительства", [&](Tx& tx) {
    heal = rules::createBuilding(tx, 0, "Обитель целителей");
    plague = rules::createBuilding(tx, 0, "Чумной двор");
    rules::setBuildingFlag(tx, heal, rules::BuildingFlag::Healing, true);
    rules::setBuildingFlag(tx, plague, rules::BuildingFlag::Plague, true);
    rules::placeBuilding(tx, sp.province, heal, 1);
    rules::placeBuilding(tx, sp.province, plague, 1);
    rules::setEssence(tx, sp.state, plagueEss, 3000);
    rules::setEssence(tx, sp.state, water, 600);
  }));
  provinceTab(h, sp.province, "province.buildings");
  const std::string healMark = "prov.heal." + std::to_string(heal), plagueMark = "prov.plague." + std::to_string(plague);
  CHECK(tr::revealIn(h, plagueMark));
  // Чумы нет — «Вылечить провинцию» недоступна (щелчок ничего не делает).
  u64 v = h->store.version();
  CHECK(tr::revealIn(h, healMark));
  tr::clickQuick(h, healMark);
  CHECK_EQ(h->store.version(), v);
  // «Заразить чумой» — 2500 эссенции чумы.
  CHECK(tr::revealIn(h, plagueMark));
  tr::clickQuick(h, plagueMark);
  CHECK(rules::provinceHas(h->world(), sp.province, schema::mod::Plague));
  CHECK_NEAR(h->world().faction(sp.state)->essence(plagueEss), 500.0, 1e-9);
  shotClean(h, "econ2_prov_plague", 60);
  // «Вылечить провинцию» — выбор эссенции (500): чума снята, «Временный иммунитет».
  CHECK(tr::revealIn(h, healMark));
  tr::clickQuick(h, healMark);
  shotClean(h, "econ2_prov_heal_menu", 60);
  CHECK(h.clickUi(healMark + ".ess." + std::to_string(water)));
  h.settle(40);
  CHECK(!rules::provinceHas(h->world(), sp.province, schema::mod::Plague));
  CHECK(rules::provinceHas(h->world(), sp.province, schema::mod::PlagueImmunity));
  CHECK_NEAR(h->world().faction(sp.state)->essence(water), 100.0, 1e-9);
  // Иммунитет — заразить снова нельзя.
  v = h->store.version();
  CHECK(tr::revealIn(h, plagueMark));
  tr::clickQuick(h, plagueMark);
  CHECK_EQ(h->store.version(), v);
  // Модификаторы провинции: «Временный иммунитет» со сроком.
  provinceTab(h, sp.province, "province.modifiers");
  shotClean(h, "econ2_prov_immunity", 60);
  // Одна стройка в провинции: пока строится одна постройка, «Построить» не открывает выбор.
  Id quick = 0;
  CHECK(h->act("Стройка", [&](Tx& tx) {
    quick = rules::createBuilding(tx, 0, "Колодец");
    tx.building(quick).levels[0].cost.clear();
    tx.building(quick).levels[0].turns = 3;
    rules::startBuilding(tx, sp.province, quick);
  }));
  provinceTab(h, sp.province, "province.buildings");
  CHECK(tr::revealIn(h, "prov.build"));
  tr::clickQuick(h, "prov.build");
  CHECK(!h->hasDialog("build.picker"));
  bool reasonOne = false;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province))
    for (const std::string& r : o.reasons) reasonOne = reasonOne || r.find("одновременно") != std::string::npos;
  CHECK(reasonOne);
  // Выбор стройки: собор — цена в эссенции (нехватка — красным).
  CHECK(h->act("Отменить стройку", [&](Tx& tx) { rules::cancelBuilding(tx, sp.province, quick); }));
  CHECK(h->openDialog("build.picker", sp.province));
  h.settle(40);
  const Id cathedral = test::idByName(h->world().buildings, "Собор смерти");
  CHECK(cathedral != 0);
  bool essOpt = false;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province))
    if (o.building == cathedral) essOpt = !o.essCost.empty() && !o.can;
  CHECK(essOpt);
  CHECK(h.clickUi("build.search"));
  h.type("Собор смерти");
  h.settle(40);
  shotClean(h, "econ2_build_picker", 60);
}
