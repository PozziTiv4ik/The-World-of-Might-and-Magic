// Сценарии производства (ТЗ «Доработки №1»): множитель производства в карточке постройки преобразования (выбор,
// недоступен без технологий «Стимуляция промышленности», после их изучения — ×N, во время цикла — только показ, конец
// хода даёт ×N), рецепты с несколькими ресурсами на выходе (редактор, карточка провинции, выбор из двенадцати рецептов
// бойни), «Добыча не обязательна» (ферма), слагаемое «Население» торговой ценности, ветка «Стимуляция промышленности» в
// общем дереве технологий, новые постройки общего дерева и штабы государства, значок археологических сокровищ.
#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/panels/province_common.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;

namespace {

Id resNamed(const World& w, const char* name) { return test::idByName(w.catalogs->resources, name); }

// Технология ветки «Стимуляция промышленности», открывающая множитель ×mult.
Id industryTech(const World& w, int mult) {
  return w.techs.firstId([&](const Tech& t) { return t.faction == 0 && t.key == schema::industryKey(mult); });
}

void openProv(Harness& h, Id pid, const char* tab) {
  openTab(h, app::SelType::Province, pid, tab);
  h.settle(40);
}

// Шаг плана преобразования хода для постройки в провинции (nullptr — шага нет).
const rules::ConvertStep* stepOf(const World& w, Id state, Id pid, Id bid) {
  const rules::FactionCalc* fc = rules::calc(w)->faction(state);
  if (!fc) return nullptr;
  for (const rules::ConvertStep& s : fc->conversions)
    if (s.province == pid && s.building == bid) return &s;
  return nullptr;
}

// Подсказка элемента: указатель над ним, ожидание появления (350 мс).
std::string tipOf(Harness& h, const std::string& mark) {
  const RectF* r = h->uiRect(mark);
  if (!r) return {};
  const RectF rr = *r;
  h.move(rr.cx(), rr.cy());
  h.frames(40);
  return ui::tooltipText();
}

// Указатель — на пустую часть верхней строки (снимок без всплывающих подсказок).
void away(Harness& h) {
  h.move(900, 20);
  h.settle(40);
}

// Полоса панели выделения (во всю её ширину и ещё right справа — всплывающие списки) вокруг отмеченного элемента.
RectF band(Harness& h, const std::string& mark, float up, float down, float right = 0) {
  const RectF x = rectOf(h.a(), mark), in = rectOf(h.a(), "inspector");
  return RectF{in.x, x.y - up, in.w + right, x.h + up + down};
}

// Область вокруг отмеченного элемента (снимок части экрана): поля по сторонам.
RectF around(Harness& h, const std::string& mark, float l, float t, float r, float b) {
  const RectF x = rectOf(h.a(), mark);
  return RectF{x.x - l, x.y - t, x.w + l + r, x.h + t + b};
}

}  // namespace

// ---------------------------------------------------------------- множитель производства
TEST(app_production_mult_card) {
  Harness h("production_mult", 1440, 1100);
  h.demo();
  h.dropToasts();
  const StateProv sp = stateWithFreeSlots(h->world(), 2);
  CHECK(sp.province != 0);
  const Id pit = test::buildingByKey(h->world(), schema::bld::CharcoalPit);
  const Id wood = resNamed(h->world(), "Древесина"), coal = resNamed(h->world(), "Уголь");
  CHECK(pit && wood && coal);
  // Углежогная яма: древесина → уголь; цикл — 2 хода (множитель меняется только между циклами).
  CHECK(h->act("Яма", [&](Tx& tx) {
    tx.building(pit).recipes[0].turns = 2;
    rules::placeBuilding(tx, sp.province, pit, 1);
    tx.faction(sp.state).res[wood] = 100;
    tx.faction(sp.state).res[coal] = 0;
  }));
  const std::string mark = "prov.mult." + std::to_string(pit);
  openProv(h, sp.province, "province.buildings");
  CHECK(revealIn(h, mark));
  CHECK_EQ(rules::maxProductionMult(h->world(), sp.state), 1);
  // Без технологий: ×2 — недоступный пункт с названием нужной технологии; выбор не меняет множитель.
  {
    const std::string tip = tipOf(h, mark);
    CHECK_MSG(tip.find("Стимуляция промышленности") != std::string::npos, tip);
  }
  clickQuick(h, mark);
  h.key(Key::Down);
  h.key(Key::Enter);
  h.settle(40);
  CHECK_EQ(provBuilding(h->world(), sp.province, pit)->mult, 1);
  CHECK(cropShot("production_mult_locked", band(h, mark, 120, 140, 80)));
  h.key(Key::Escape);
  h.settle(40);
  // Правило отказывает с той же причиной.
  CHECK(!h->act("Множитель", [&](Tx& tx) { rules::setPbMult(tx, sp.province, pit, 2); }));
  CHECK(hasToast(h, "Стимуляция промышленности", app::ToastKind::Warning));
  h.dropToasts();
  // Изучены «Стимуляция промышленности 1…3» — открыт ×4: выбор ×3.
  CHECK(h->act("Изучить", [&](Tx& tx) {
    for (int m = 2; m <= 4; m++) rules::setStudied(tx, industryTech(tx.w(), m), true, sp.state);
  }));
  CHECK_EQ(rules::maxProductionMult(h->world(), sp.state), 4);
  h.settle(40);
  CHECK(revealIn(h, mark));
  clickQuick(h, mark);
  CHECK(cropShot("production_mult_open", band(h, mark, 120, 230, 80)));
  h.key(Key::Down);
  h.key(Key::Down);
  h.key(Key::Enter);
  h.settle(40);
  CHECK_EQ(provBuilding(h->world(), sp.province, pit)->mult, 3);
  // План хода: вход и выход — ×3; карточка — рецепт с множителем.
  {
    const rules::ConvertStep* s = stepOf(h->world(), sp.state, sp.province, pit);
    CHECK(s != nullptr);
    if (s) {
      CHECK_EQ(s->mult, 3);
      CHECK(s->start);
      CHECK_NEAR(s->in.at(wood), 3, 1e-9);
      CHECK_EQ(s->out.size(), size_t(1));
      if (!s->out.empty()) CHECK_NEAR(s->out[0].amount, 3, 1e-9);
    }
  }
  away(h);
  CHECK(revealIn(h, "prov.recipe." + std::to_string(pit)));
  CHECK(cropShot("production_mult_card", band(h, "prov.recipe." + std::to_string(pit), 170, 110)));
  // Ход: цикл начат — множитель только для показа (причина в подсказке), Ctrl+Z не нужен.
  CHECK(h->endTurnNow());
  h->toasts().clear();
  h.settle(40);
  CHECK_EQ(provBuilding(h->world(), sp.province, pit)->cycle, 1);
  CHECK_NEAR(h->world().faction(sp.state)->stock(wood), 97, 1e-9);
  CHECK(revealIn(h, mark));
  {
    const std::string tip = tipOf(h, mark);
    CHECK_MSG(tip.find("Идёт цикл") != std::string::npos, tip);
  }
  clickQuick(h, mark);
  h.key(Key::Up);
  h.key(Key::Enter);
  h.settle(40);
  CHECK_EQ(provBuilding(h->world(), sp.province, pit)->mult, 3);
  CHECK(!h->act("Множитель", [&](Tx& tx) { rules::setPbMult(tx, sp.province, pit, 2); }));
  h.dropToasts();
  // Конец цикла: уголь ×3.
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK_NEAR(h->world().faction(sp.state)->stock(coal), 3, 1e-9);
  // Экономика государства: в подсказке прихода — постройка, провинция и множитель.
  CHECK(h->act("Яма снова", [&](Tx& tx) { tx.faction(sp.state).res[wood] = 100; }));
  // Игрок другого государства — множитель только для показа.
  Id other = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!other && f.isState() && f.id != sp.state) other = f.id;
  });
  CHECK(other != 0);
  h->ui.role = app::Role::Player;
  h->ui.playerState = other;
  h.settle(40);
  CHECK(revealIn(h, mark));
  clickQuick(h, mark);
  h.key(Key::Up);
  h.key(Key::Enter);
  h.settle(40);
  CHECK_EQ(provBuilding(h->world(), sp.province, pit)->mult, 3);
  // Своё государство — выбирает.
  h->ui.playerState = sp.state;
  h.settle(40);
  CHECK(revealIn(h, mark));
  clickQuick(h, mark);
  h.key(Key::Up);
  h.key(Key::Enter);
  h.settle(40);
  CHECK_EQ(provBuilding(h->world(), sp.province, pit)->mult, 2);
  // Экономика государства: в подсказке расхода древесины — постройка, провинция и множитель плана хода.
  h->ui.tabOf[app::SelType::Faction] = "faction.economy";
  h->select(app::SelType::Faction, sp.state);
  h.settle(60);
  CHECK(revealIn(h, "economy.search"));
  CHECK(h.clickUi("economy.search"));
  h.type("Древесина");
  h.settle(40);
  CHECK(revealIn(h, "economy.search", "inspector", 60, 160));
  const std::string use = "economy.use." + std::to_string(wood);
  CHECK(revealIn(h, use));
  {
    const std::string tip = tipOf(h, use);
    CHECK_MSG(tip.find(h->world().building(pit)->name) != std::string::npos && tip.find("×2") != std::string::npos, tip);
  }
}

// ---------------------------------------------------------------- несколько ресурсов на выходе
TEST(app_production_recipe_outputs) {
  Harness h("production_outputs", 1600, 1100);
  h.demo();
  h.dropToasts();
  app::openBuildingTree(h.a(), 0);
  h.settle(40);
  clickQuick(h, "bt.add");
  Id b = 0;
  h->world().buildings.each([&](const Building& x) {
    if (x.owner == 0) b = std::max(b, x.id);
  });
  CHECK(b != 0);
  h.retype("Разделочная");
  h.key(Key::Enter);
  h.settle(40);
  auto rec = [&]() -> Recipe {
    const Building* x = h->world().building(b);
    return x && !x->recipes.empty() ? x->recipes[0] : Recipe{};
  };
  CHECK(clickSide(h, "bt.role.convert"));
  CHECK(clickSide(h, "bt.recipe.add"));
  CHECK(clickSide(h, "bt.recipe.0.addin"));
  // Пустой выход — сразу выбор первого ресурса; «+ Ресурс» появляется после него.
  CHECK(h->uiRect("bt.recipe.0.addout") == nullptr);
  const Id bronze = resNamed(h->world(), "Бронза");
  CHECK(pickSide(h, "bt.recipe.0.out", "Бронза"));
  CHECK_EQ(rec().out.size(), size_t(1));
  CHECK_EQ(rec().out[0].res, bronze);
  // Второй и третий выход; больше трёх нельзя.
  CHECK(clickSide(h, "bt.recipe.0.addout"));
  CHECK_EQ(rec().out.size(), size_t(2));
  CHECK(typeSide(h, "bt.recipe.0.outamount.1", "4"));
  CHECK_NEAR(rec().out[1].amount, 4, 1e-12);
  CHECK(clickSide(h, "bt.recipe.0.addout"));
  CHECK_EQ(rec().out.size(), size_t(3));
  CHECK(h->uiRect("bt.recipe.0.addout") == nullptr);
  const Recipe three = rec();
  CHECK(three.out[0].res != three.out[1].res && three.out[1].res != three.out[2].res && three.out[0].res != three.out[2].res);
  CHECK(revealSide(h, "bt.recipe.0.outremove.2"));
  {
    const RectF top = rectOf(h.a(), "bt.recipe.0.name"), bot = rectOf(h.a(), "bt.recipe.0.turns"), cv = rectOf(h.a(), "bt.canvas");
    CHECK(cropShot("production_recipe_editor", RectF{cv.right() + 12, top.y - 14, 372, bot.bottom() - top.y + 50}));
  }
  // Убрать второй: остались первый и третий.
  CHECK(clickSide(h, "bt.recipe.0.outremove.1"));
  CHECK_EQ(rec().out.size(), size_t(2));
  CHECK_EQ(rec().out[0].res, three.out[0].res);
  CHECK_EQ(rec().out[1].res, three.out[2].res);
  CHECK(h->uiRect("bt.recipe.0.addout") != nullptr);
  undo(h);
  CHECK_EQ(rec().out.size(), size_t(3));
  // В провинции: рецепт с тремя ресурсами на выходе — в карточке и плане хода.
  const StateProv sp = stateWithFreeSlots(h->world(), 2);
  CHECK(sp.province != 0);
  CHECK(h->act("Поставить", [&](Tx& tx) {
    rules::placeBuilding(tx, sp.province, b, 1);
    for (const ResAmount& x : tx.w().building(b)->recipes[0].in) tx.faction(sp.state).res[x.res] = 50;
  }));
  h->closeEditor();
  openProv(h, sp.province, "province.buildings");
  const rules::ConvertStep* s = stepOf(h->world(), sp.state, sp.province, b);
  CHECK(s != nullptr);
  if (s) CHECK_EQ(s->out.size(), size_t(3));
  CHECK(revealIn(h, "prov.recipe." + std::to_string(b)));
  away(h);
  CHECK(cropShot("production_outputs_card", band(h, "prov.recipe." + std::to_string(b), 190, 110)));
}

// Бойня: двенадцать рецептов — выбор поиском, на выходе до трёх ресурсов.
TEST(app_production_slaughterhouse) {
  Harness h("production_slaughter", 1440, 1100);
  h.demo();
  h.dropToasts();
  const StateProv sp = stateWithFreeSlots(h->world(), 2);
  CHECK(sp.province != 0);
  const Id sh = test::buildingByKey(h->world(), schema::bld::Slaughterhouse);
  CHECK(sh != 0);
  CHECK_EQ(h->world().building(sh)->recipes.size(), size_t(12));
  CHECK(h->act("Бойня", [&](Tx& tx) { rules::placeBuilding(tx, sp.province, sh, 1); }));
  openProv(h, sp.province, "province.buildings");
  const std::string pick = "prov.recipePick." + std::to_string(sh);
  CHECK(revealIn(h, pick));
  clickQuick(h, pick);
  CHECK(cropShot("production_slaughter_recipes", band(h, pick, 160, 470, 80)));
  h.type("мамонт");
  h.key(Key::Enter);
  h.settle(40);
  const ProvBuilding* pb = provBuilding(h->world(), sp.province, sh);
  CHECK(pb != nullptr);
  if (pb) {
    CHECK_EQ(pb->recipe, 6);
    CHECK_EQ(h->world().building(sh)->recipes[size_t(pb->recipe)].out.size(), size_t(3));
  }
  away(h);
  CHECK(revealIn(h, "prov.recipe." + std::to_string(sh)));
  CHECK(cropShot("production_slaughter_card", band(h, "prov.recipe." + std::to_string(sh), 200, 110)));
}

// ---------------------------------------------------------------- необязательная добыча
TEST(app_production_extract_optional) {
  Harness h("production_farm", 1600, 1100);
  h.demo();
  h.dropToasts();
  const Id farm = test::buildingByKey(h->world(), schema::bld::Farm);
  CHECK(farm != 0);
  CHECK(h->world().building(farm)->extractOptional);
  app::openBuildingTree(h.a(), 0, farm);
  h.settle(40);
  CHECK(revealSide(h, "bt.role.extractOptional"));
  CHECK(cropShot("production_farm_editor", around(h, "bt.role.extractOptional", 14, 40, 14, 120)));
  CHECK(clickSide(h, "bt.role.extractOptional"));
  CHECK(!h->world().building(farm)->extractOptional);
  undo(h);
  CHECK(h->world().building(farm)->extractOptional);
  // Подсказка карточки на схеме — добыча не обязательна.
  h.key(Key::F);
  h.settle(40);
  // Ферма в провинции с ресурсом не из групп добычи: ставится, без предупреждения — добыча не растёт.
  Id pid = 0, state = 0;
  {
    auto calc = rules::calc(h->world());
    const Building* fb = h->world().building(farm);
    h->world().provinces.each([&](const Province& p) {
      if (pid || p.sea || !p.owner || !p.resource) return;
      const Faction* f = h->world().faction(p.owner);
      const rules::ProvinceCalc* pc = calc->province(p.id);
      if (!f || !f->isState() || !pc || pc->slots <= int(p.buildings.size())) return;
      if (rules::extractAllowed(h->world(), *fb, p.resource)) return;
      pid = p.id;
      state = p.owner;
    });
  }
  CHECK(pid != 0);
  bool reasonOk = true;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), pid))
    if (o.building == farm)
      for (const std::string& r : o.reasons) reasonOk = reasonOk && r.find("добывается") == std::string::npos && r.find("ресурс провинции") == std::string::npos;
  CHECK(reasonOk);
  CHECK(h->act("Ферма", [&](Tx& tx) { rules::placeBuilding(tx, pid, farm, 1); }));
  h->closeEditor();
  openProv(h, pid, "province.buildings");
  const std::string ex = "prov.extract." + std::to_string(farm);
  CHECK(revealIn(h, ex));
  away(h);
  CHECK(cropShot("production_farm_card", band(h, ex, 130, 110)));
  (void)state;
}

// ---------------------------------------------------------------- торговая ценность: население
TEST(app_production_trade_pop) {
  Harness h("production_trade", 1440, 1000);
  h.demo();
  h.dropToasts();
  Id pid = 0;
  i64 best = 0;
  {
    auto calc = rules::calc(h->world());
    h->world().provinces.each([&](const Province& p) {
      const rules::ProvinceCalc* pc = calc->province(p.id);
      if (p.sea || !p.owner || !pc || pc->population <= best) return;
      best = pc->population;
      pid = p.id;
    });
  }
  CHECK(pid != 0);
  const rules::ProvinceCalc* pc = rules::calc(h->world())->province(pid);
  CHECK(pc != nullptr);
  if (!pc) return;
  CHECK_NEAR(pc->tradePop, double(pc->population) / 100, 1e-9);
  const std::vector<app::prov::TipLine> lines = app::prov::tradeLines(h->world(), *h->world().province(pid), *pc);
  bool popLine = false;
  for (const app::prov::TipLine& l : lines)
    if (l.label.find("Население") != std::string::npos) popLine = l.value == fmtSigned(pc->tradePop, std::fabs(pc->tradePop - std::round(pc->tradePop)) > 1e-6 ? 1 : 0);
  CHECK(popLine);
  openProv(h, pid, "province.economy");
  CHECK(revealIn(h, "province.tradeValue"));
  const RectF tv = rectOf(h.a(), "province.tradeValue");
  h.move(tv.x + 10, tv.cy());
  h.frames(40);
  CHECK(cropShot("production_trade_pop", RectF{tv.x - 180, tv.y - 40, 520, 260}));
}

// ---------------------------------------------------------------- ветка «Стимуляция промышленности»
TEST(app_production_industry_tree) {
  Harness h("production_industry", 1600, 1000);
  h.demo();
  h.dropToasts();
  const Id t1 = industryTech(h->world(), 2), t99 = industryTech(h->world(), 100), t50 = industryTech(h->world(), 51);
  CHECK(t1 && t99 && t50);
  Id state = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!state && f.isState()) state = f.id;
  });
  app::openTechTree(h.a(), 0, 0, state);
  h.settle(40);
  h.key(Key::F);
  h.settle(60);
  CHECK(h.shot("production_industry_tree"));
  // Поиск: «Стимуляция промышленности 5» — 11 совпадений (5, 50…59); «Следующая найденная» идёт по ним.
  CHECK(h.clickUi("tt.search"));
  h.type("Стимуляция промышленности 5");
  h.settle(40);
  CHECK(h->uiRect("tt.searchNext") != nullptr);
  CHECK(h.clickUi("tt.searchNext"));
  h.settle(40);
  CHECK(h.clickUi("tt.searchNext"));
  h.settle(60);
  CHECK(h.shot("production_industry_search"));
  // Связи «змейкой»: ряд справа налево — из левого края в правый, переход в следующий ряд — сверху вниз.
  const Tech* a = h->world().tech(industryTech(h->world(), 12));   // второй ряд, справа налево
  const Tech* b = h->world().tech(industryTech(h->world(), 13));
  CHECK(a && b);
  if (a && b) {
    CHECK(b->pos.x < a->pos.x);
    const app::tree::Curve k = app::tree::linkCurve(Box2{a->pos.x, a->pos.y, a->pos.x + 232, a->pos.y + 96}, Box2{b->pos.x, b->pos.y, b->pos.x + 232, b->pos.y + 96});
    CHECK_NEAR(k.p0.x, a->pos.x, 1e-9);
    CHECK_NEAR(k.p1.x, b->pos.x + 232, 1e-9);
  }
  const Tech* e = h->world().tech(industryTech(h->world(), 11));   // конец первого ряда
  const Tech* f = h->world().tech(industryTech(h->world(), 12));
  CHECK(e && f);
  if (e && f) {
    CHECK_NEAR(e->pos.x, f->pos.x, 1e-9);
    const app::tree::Curve k = app::tree::linkCurve(Box2{e->pos.x, e->pos.y, e->pos.x + 232, e->pos.y + 96}, Box2{f->pos.x, f->pos.y, f->pos.x + 232, f->pos.y + 96});
    CHECK_NEAR(k.p0.y, e->pos.y + 96, 1e-9);
    CHECK_NEAR(k.p1.y, f->pos.y, 1e-9);
  }
  // Ближе: ряды ветки со связями.
  // Ближе: поиск снят, приближение к концу первого ряда ветки — связи вправо, вниз и влево.
  CHECK(h.clickUi("tt.search"));
  h.key(Key::A, ctrl());
  h.key(Key::Backspace);
  h.settle(40);
  CHECK(h->uiRect("tt.searchNext") == nullptr);
  app::openTechTree(h.a(), 0, industryTech(h->world(), 11), state);
  h.settle(60);
  {
    const RectF nr = rectOf(h.a(), "tt.node." + std::to_string(industryTech(h->world(), 11)));
    h.wheel(nr.cx(), nr.bottom() + 20, 7);
    h.settle(60);
  }
  away(h);
  CHECK(h.shot("production_industry_rows"));
  // Вкладка государства «Технологии»: далёкие закрытые свёрнуты в ссылку.
  h->closeEditor();
  h->ui.tabOf[app::SelType::Faction] = "faction.tech";
  h->select(app::SelType::Faction, state);
  h.settle(60);
  CHECK(revealIn(h, "faction.ctech.more"));
  CHECK(h->uiRect("faction.ctech.row." + std::to_string(t1)) != nullptr);
  CHECK(h->uiRect("faction.ctech.row." + std::to_string(t50)) == nullptr);
  CHECK(h.shot("production_industry_tab"));
  clickQuick(h, "faction.ctech.more");
  CHECK(revealIn(h, "faction.ctech.row." + std::to_string(t50)));
}

// ---------------------------------------------------------------- новые постройки
TEST(app_production_new_buildings) {
  Harness h("production_buildings", 1600, 1100);
  h.demo();
  h.dropToasts();
  for (const char* k : {schema::bld::Farm, schema::bld::Slaughterhouse, schema::bld::CultAltar, schema::bld::HolyTemple, schema::bld::UnholyTemple,
                        schema::bld::AbyssEnricher, schema::bld::NecroEnricher, schema::bld::Housing, schema::bld::WarWorkshop, schema::bld::EngWorkshop,
                        schema::bld::CharcoalPit, schema::bld::BuildersGuild})
    CHECK_MSG(test::buildingByKey(h->world(), k) != 0, k);
  app::openBuildingTree(h.a(), 0);
  h.settle(40);
  h.key(Key::F);
  h.settle(60);
  CHECK(h.shot("production_buildings_tree"));
  // Храм освящения: эссенция на входе рецепта — в карточке сведений.
  const Id temple = test::buildingByKey(h->world(), schema::bld::HolyTemple);
  app::openBuildingTree(h.a(), 0, temple);
  h.settle(60);
  {
    const RectF nr = rectOf(h.a(), "bt.node." + std::to_string(temple));
    h.move(nr.x + nr.w * 0.5f, nr.y + nr.h * 0.3f);
    h.frames(40);
    h.settle(40);
    CHECK(h.shot("production_buildings_temple"));
  }
  // Уникальное дерево государства: штабы разведки и контрразведки.
  Id state = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!state && f.isState()) state = f.id;
  });
  CHECK(test::buildingByKey(h->world(), schema::bld::IntelHq, state) != 0);
  CHECK(test::buildingByKey(h->world(), schema::bld::CounterIntelHq, state) != 0);
  app::openBuildingTree(h.a(), state);
  h.settle(40);
  h.key(Key::F);
  h.settle(60);
  away(h);
  CHECK(h.shot("production_buildings_state"));
  // Окно строительства: «Храм» — эссенции на входе рецептов; «Жилой» — уровни с модификаторами.
  h->closeEditor();
  const StateProv sp = stateWithFreeSlots(h->world(), 2);
  CHECK(sp.province != 0);
  setStock(h.a(), sp.state, 5000);
  CHECK(h->openDialog("build.picker", sp.province));
  h.settle(40);
  CHECK(h.clickUi("build.search"));
  h.type("освящения");
  h.settle(40);
  away(h);
  CHECK(h.shot("production_picker_temples"));
  CHECK(h.clickUi("build.search"));
  h.key(Key::A, ctrl());
  h.type("Бойня");
  h.settle(40);
  away(h);
  CHECK(h.shot("production_picker_slaughter"));
  CHECK(h.clickUi("build.search"));
  h.key(Key::A, ctrl());
  h.type("Жилой");
  h.settle(40);
  away(h);
  CHECK(h.shot("production_picker_housing"));
}

// ---------------------------------------------------------------- значок археологических сокровищ
TEST(app_production_amphora) {
  Harness h("production_amphora", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id tr = h->world().catalogs->resourceId(schema::kResArchTreasure);
  CHECK(tr != 0);
  CHECK_EQ(std::string(app::w::resourceIcon(h->world(), tr)), std::string("amphora"));
  CHECK(std::string(app::w::resourceIcon(h->world(), kGold)) != "amphora");
  Id state = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!state && f.isState()) state = f.id;
  });
  CHECK(h->act("Сокровища", [&](Tx& tx) { tx.faction(state).res[tr] = 25; }));
  // Справочник ресурсов: «Археологические сокровища» рядом с золотом.
  h->openEditor("catalogs", 1);
  h.settle(40);
  CHECK(h.clickUi("catalogs.search"));
  h.type("сокровищ");
  h.settle(40);
  away(h);
  CHECK(h.shot("production_amphora_catalog"));
  h->closeEditor();
  // Экономика государства: строка ресурса со значком амфоры.
  h->ui.tabOf[app::SelType::Faction] = "faction.economy";
  h->select(app::SelType::Faction, state);
  h.settle(60);
  CHECK(revealIn(h, "economy.search"));
  CHECK(h.clickUi("economy.search"));
  h.type("сокровищ");
  h.settle(40);
  away(h);
  // Поле поиска — к верху страницы: строки таблицы ниже него строятся, только когда видны.
  CHECK(revealIn(h, "economy.search", "inspector", 60, 160));
  CHECK(revealIn(h, "economy.res." + std::to_string(tr)));
  CHECK(cropShot("production_amphora_economy", around(h, "economy.res." + std::to_string(tr), 10, 80, 10, 20)));
}
