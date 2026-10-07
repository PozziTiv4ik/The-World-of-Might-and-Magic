// Сценарии особых возможностей построек в дереве (ТЗ «Доработки», п.2–4, 7–8; «Ввод новых механик», п.4–5):
// категории «Религиозные» и «Культовые», преобразование ресурсов по рецепту, генерация эссенции по уровням, доступ к
// особым отрядам, требуемые технологии (общая постройка — только общие), «Открывает постройки» в дереве технологий,
// окно выбора модификаторов уровня с созданием нового.
#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;

namespace {

Id buildingByName(const World& w, const std::string& name) {
  Id r = 0;
  w.buildings.each([&](const Building& b) {
    if (b.name == name) r = b.id;
  });
  return r;
}

Id resourceByName(const World& w, const std::string& name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}

Id essenceByName(const World& w, const std::string& name) {
  for (const CatalogItem& c : w.catalogs->essences)
    if (c.name == name) return c.id;
  return 0;
}

Id specialByName(const World& w, const std::string& name) {
  for (const SpecialUnit& s : w.catalogs->specials)
    if (s.name == name) return s.id;
  return 0;
}

Id modifierByName(const World& w, const std::string& name) {
  Id r = 0;
  w.modifiers.each([&](const Modifier& m) {
    if (m.name == name) r = m.id;
  });
  return r;
}

// Два первых государства мира (по порядку ID).
std::pair<Id, Id> twoStates(const World& w) {
  Id a = 0, b = 0;
  w.factions.each([&](const Faction& f) {
    if (!f.isState()) return;
    if (!a) a = f.id;
    else if (!b) b = f.id;
  });
  return {a, b};
}

bool has(const std::vector<Id>& v, Id x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// Ресурс через категорию (w::resourceByGroup): слева от списка ресурса — список групп; сначала группа, затем ресурс.
bool pickByGroup(Harness& h, const std::string& resCombo, const std::string& group, const std::string& resource) {
  if (!revealSide(h, resCombo)) return false;
  RectF r = rectOf(h.a(), resCombo);
  h.click(r.x - 66, r.cy());   // список групп — ячейка левее
  h.type(group);
  h.key(Key::Enter);
  h.settle(40);
  return pickSide(h, resCombo, resource);
}

}  // namespace

TEST(app_trees_roles_editor) {
  HideTestRegs hide;
  Harness h("trees_roles_editor", 1600, 1100);
  h.demo();
  h.dropToasts();
  app::openBuildingTree(h.a(), 0);
  h.settle(40);
  // Новая постройка общего дерева.
  clickQuick(h, "bt.add");
  Id b = 0;
  h->world().buildings.each([&](const Building& x) {
    if (x.owner == 0) b = std::max(b, x.id);
  });
  CHECK(b != 0);
  h.retype("Алхимическая палата");
  h.key(Key::Enter);
  h.settle(40);
  CHECK_EQ(h->world().building(b)->name, std::string("Алхимическая палата"));
  // Шесть категорий: пятая — «Религиозные», шестая — «Культовые» (одна на всю карту).
  RectF cat = rectOf(h.a(), "bt.side.cat");
  h.click(cat.x + cat.w * (4.5f / 6), cat.cy());
  h.settle(40);
  CHECK(h->world().building(b)->cat == BuildingCat::Religious);
  CHECK(h->uiRect("bt.side.cult") == nullptr);
  h.click(cat.x + cat.w * (5.5f / 6), cat.cy());
  h.settle(40);
  CHECK(h->world().building(b)->cat == BuildingCat::Cult);
  CHECK(h->uiRect("bt.side.cult") != nullptr);
  CHECK(h->uiRect("bt.side.cultAt") == nullptr);   // ещё нигде не стоит

  // Преобразование ресурсов: рецепт — до трёх ресурсов на входе, выход, срок цикла.
  CHECK(clickSide(h, "bt.role.convert"));
  CHECK(h->world().building(b)->convert);
  CHECK(clickSide(h, "bt.recipe.addin"));
  CHECK_EQ(h->world().building(b)->recipe.in.size(), size_t(1));
  CHECK(typeSide(h, "bt.recipe.inamount.0", "4"));
  CHECK_NEAR(h->world().building(b)->recipe.in[0].amount, 4, 1e-12);
  const Id iron = resourceByName(h->world(), "Железо"), steel = resourceByName(h->world(), "Сталь");
  CHECK(iron && steel);
  CHECK(clickSide(h, "bt.recipe.addin"));
  CHECK_EQ(h->world().building(b)->recipe.in.size(), size_t(2));
  CHECK(pickByGroup(h, "bt.recipe.in.1", "Руда", "Железо"));
  CHECK_EQ(h->world().building(b)->recipe.in[1].res, iron);
  // Выход — через категорию ресурсов; количество до тысячных.
  CHECK(pickSide(h, "bt.recipe.out", "Сталь"));
  CHECK_EQ(h->world().building(b)->recipe.out.res, steel);
  CHECK_NEAR(h->world().building(b)->recipe.out.amount, 1, 1e-12);
  CHECK(typeSide(h, "bt.recipe.outamount", "2,5"));
  CHECK_NEAR(h->world().building(b)->recipe.out.amount, 2.5, 1e-12);
  // Срок цикла: «+» поля.
  CHECK(revealSide(h, "bt.recipe.turns"));
  RectF tr = rectOf(h.a(), "bt.recipe.turns");
  h.click(tr.right() - 10, tr.cy());
  h.settle(40);
  h.click(tr.right() - 10, tr.cy());
  h.settle(40);
  CHECK_EQ(h->world().building(b)->recipe.turns, 3);
  // Одинаковые ресурсы на входе запрещены правилами: отказ с причиной, рецепт прежний.
  const Recipe before = h->world().building(b)->recipe;
  CHECK(!h->act("Рецепт", [&](Tx& tx) {
    Recipe r = before;
    r.in[0].res = r.in[1].res;
    rules::setRecipe(tx, b, r);
  }));
  CHECK(h->world().building(b)->recipe == before);
  CHECK(hasToast(h.a(), "разными", app::ToastKind::Warning));
  h->toasts().clear();
  // Не больше трёх ресурсов на входе: третий — последний.
  CHECK(clickSide(h, "bt.recipe.addin"));
  CHECK_EQ(h->world().building(b)->recipe.in.size(), size_t(3));
  CHECK(h->uiRect("bt.recipe.addin") == nullptr);
  CHECK(clickSide(h, "bt.recipe.inremove.2"));
  CHECK_EQ(h->world().building(b)->recipe.in.size(), size_t(2));

  // Генерация эссенции: у уровня — «Эссенция за ход».
  CHECK(clickSide(h, "bt.role.essence"));
  CHECK(h->world().building(b)->essenceGen);
  const Id flame = essenceByName(h->world(), "Эссенция пламени");
  CHECK(flame != 0);
  CHECK(pickSide(h, "bt.level.0.addess", "пламени"));
  CHECK_NEAR(h->world().building(b)->levels[0].essence.at(flame), 1, 1e-12);
  CHECK(typeSide(h, "bt.level.0.ess." + std::to_string(flame), "2,5"));
  CHECK_NEAR(h->world().building(b)->levels[0].essence.at(flame), 2.5, 1e-12);

  // Доступ к особым отрядам: «Драконы Бездны» из справочника.
  CHECK(clickSide(h, "bt.role.special"));
  CHECK(h->world().building(b)->specialAccess);
  const Id dragons = specialByName(h->world(), "Драконы Бездны");
  CHECK(dragons != 0);
  CHECK(pickSide(h, "bt.addspecial", "Драконы"));
  CHECK(has(h->world().building(b)->specials, dragons));
  CHECK(revealSide(h, "bt.side.roles"));
  shotTrees(h, "trees_roles_editor");
  // Карточка на схеме: значки возможностей (снимок) и карточка сведений.
  {
    RectF nr = rectOf(h.a(), "bt.node." + std::to_string(b));
    h.move(nr.x + nr.w * 0.5f, nr.y + nr.h * 0.3f);
    h.frames(40);
    h.settle(40);
    CHECK(h.shot("trees_roles_card"));
  }

  // Фишка особого отряда — справочник (вкладка «Особые отряды»).
  CHECK(clickSide(h, "bt.special." + std::to_string(dragons), "bt.canvas", 0.3f, 0.5f));
  CHECK_EQ(h->ui.editor, std::string("catalogs"));
  shotTrees(h, "trees_roles_specials_catalog");
  app::openBuildingTree(h.a(), 0, b);
  h.settle(40);

  // Выключение возможности снимает её данные; Ctrl+Z возвращает.
  CHECK(clickSide(h, "bt.role.convert"));
  CHECK(!h->world().building(b)->convert);
  CHECK(h->world().building(b)->recipe.in.empty());
  undo(h);
  CHECK(h->world().building(b)->convert);
  CHECK_EQ(h->world().building(b)->recipe.in.size(), size_t(2));
  CHECK(clickSide(h, "bt.role.essence"));
  CHECK(h->world().building(b)->levels[0].essence.empty());
  undo(h);
  CHECK_NEAR(h->world().building(b)->levels[0].essence.at(flame), 2.5, 1e-12);

  // Культовая постройка в провинции: в карточке — где она уже стоит; в другой провинции её не поставить.
  StateProv sp = stateWithFreeSlots(h->world(), 1);
  CHECK(sp.province != 0);
  CHECK(h->act("Поставить", [&](Tx& tx) { rules::placeBuilding(tx, sp.province, b, 1); }));
  h.settle(40);
  CHECK(revealSide(h, "bt.side.cultAt"));
  StateProv other = stateWithFreeSlots(h->world(), 1, sp.state);
  CHECK(other.province != 0);
  bool reason = false;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), other.province))
    if (o.building == b) {
      CHECK(!o.canPlace);
      for (const std::string& r : o.placeReasons) reason = reason || r.find("одна на всю карту") != std::string::npos;
    }
  CHECK(reason);
  shotTrees(h, "trees_roles_cult");
}

TEST(app_trees_roles_modifiers_dialog) {
  HideTestRegs hide;
  Harness h("trees_roles_mods", 1600, 1000);
  h.demo();
  h.dropToasts();
  // Модификаторы всех видов: в окне — только «Везде», «Для провинций», «Глобальный».
  Id mAny = 0, mProv = 0, mFac = 0, mArmy = 0, mHero = 0;
  CHECK(h->act("Модификаторы", [&](Tx& tx) {
    auto make = [&](const char* name, ModKind k, Fx fx, double v) {
      Id id = rules::createModifier(tx, name);
      Modifier& m = tx.modifier(id);
      m.kind = k;
      m.fx[size_t(int(fx))] = v;
      m.fxMask |= 1u << int(fx);
      return id;
    };
    mAny = make("Аа Всеобщий подъём", ModKind::Any, Fx::IncomePct, 5);
    mProv = make("Аа Ярмарка", ModKind::Province, Fx::TradePct, 10);
    mFac = make("Аа Казённый заказ", ModKind::Faction, Fx::IncomePct, 3);
    mArmy = make("Аа Боевой дух", ModKind::Army, Fx::IncomePct, 1);
    mHero = make("Аа Слава героя", ModKind::Hero, Fx::IncomePct, 1);
  }));
  const Id market = buildingByName(h->world(), "Рынок");
  CHECK(market != 0);
  app::openBuildingTree(h.a(), 0, market);
  h.settle(40);
  CHECK(clickSide(h, "bt.level.0.mods"));
  CHECK(h->hasDialog("bt.mods"));
  CHECK(h->uiRect("bt.mods.row." + std::to_string(mAny)) != nullptr);
  CHECK(h->uiRect("bt.mods.row." + std::to_string(mProv)) != nullptr);
  CHECK(h->uiRect("bt.mods.row." + std::to_string(mFac)) != nullptr);
  CHECK(h->uiRect("bt.mods.row." + std::to_string(mArmy)) == nullptr);
  CHECK(h->uiRect("bt.mods.row." + std::to_string(mHero)) == nullptr);
  // Группы по видам: «Везде», «Для провинций», «Глобальный»; армий и героев — нет.
  CHECK(h->uiRect("bt.mods.kind." + std::to_string(int(ModKind::Any))) != nullptr);
  CHECK(h->uiRect("bt.mods.kind." + std::to_string(int(ModKind::Province))) != nullptr);
  CHECK(h->uiRect("bt.mods.kind." + std::to_string(int(ModKind::Faction))) != nullptr);
  CHECK(h->uiRect("bt.mods.kind." + std::to_string(int(ModKind::Army))) == nullptr);
  h.dropToasts();
  h.settle(40);
  CHECK(h.shot("trees_roles_mods_dialog"));
  // Отметка — сразу в уровне; повторный щелчок — убрать.
  auto inLevel = [&](Id m) { return has(h->world().building(market)->levels[0].modifiers, m); };
  auto clickRow = [&](Id m) {
    const std::string row = "bt.mods.row." + std::to_string(m);
    CHECK(revealIn(h, row, "bt.mods.list", 4, 0));
    clickQuick(h, row, 0.3f, 0.5f);
  };
  CHECK(!inLevel(mProv));
  clickRow(mProv);
  CHECK(inLevel(mProv));
  clickRow(mFac);
  CHECK(inLevel(mFac));
  clickRow(mFac);
  CHECK(!inLevel(mFac));
  // Поиск.
  CHECK(h.clickUi("bt.mods.search"));
  h.type("Ярмарка");
  h.settle(40);
  CHECK(h->uiRect("bt.mods.row." + std::to_string(mProv)) != nullptr);
  CHECK(h->uiRect("bt.mods.row." + std::to_string(mAny)) == nullptr);
  h.key(Key::A, ctrl());
  h.key(Key::Backspace);
  h.settle(40);
  CHECK(h->uiRect("bt.mods.row." + std::to_string(mAny)) != nullptr);
  // Новый модификатор: вид «Для провинций», сразу в уровне.
  CHECK(h.clickUi("bt.mods.name"));
  h.type("Благословение рынка");
  h.settle(40);
  CHECK(h.clickUi("bt.mods.create"));
  h.settle(40);
  const Id made = modifierByName(h->world(), "Благословение рынка");
  CHECK(made != 0);
  CHECK(h->world().modifier(made)->kind == ModKind::Province);
  CHECK(inLevel(made));
  CHECK(h->uiRect("bt.mods.row." + std::to_string(made)) != nullptr);
  h.dropToasts();
  h.settle(40);
  CHECK(h.shot("trees_roles_mods_created"));
  // Создание и добавление — один шаг отмены.
  h->undo();
  h.settle(40);
  CHECK(h->world().modifier(made) == nullptr);
  CHECK(!inLevel(made));
  h->redo();
  h.settle(40);
  CHECK(h->world().modifier(made) != nullptr);
  CHECK(inLevel(made));
  h->toasts().clear();
  // «Готово» закрывает окно; фишки модификаторов — в карточке уровня.
  CHECK(h.clickUi("bt.mods.done"));
  h.settle(40);
  CHECK(!h->hasDialog("bt.mods"));
  // Рядом с «Новый модификатор» — переход в редактор модификаторов (на созданном). Уведомления «Отменено…» над
  // окном — в слое интерфейса справа внизу: убрать, чтобы не закрывали панель свойств.
  for (int i = 0; i < 6; i++) {   // уведомления живут 4 с
    hl::advance(1);
    h.frame();
  }
  h.dropToasts();
  CHECK(clickSide(h, "bt.level.0.mods"));
  CHECK(h->hasDialog("bt.mods"));
  CHECK(h.clickUi("bt.mods.name"));
  h.type("Рыночная стража");
  h.key(Key::Enter);   // Enter в поле названия — создать, окно остаётся
  h.settle(40);
  const Id made2 = modifierByName(h->world(), "Рыночная стража");
  CHECK(made2 != 0);
  CHECK(inLevel(made2));
  CHECK(h->hasDialog("bt.mods"));
  CHECK(h.clickUi("bt.mods.editor"));
  h.settle(40);
  CHECK(!h->hasDialog("bt.mods"));
  CHECK_EQ(h->ui.editor, std::string("modifiers"));
  CHECK_EQ(h->ui.editorArg, made2);
}

TEST(app_trees_roles_tech_requirements) {
  HideTestRegs hide;
  Harness h("trees_roles_techs", 1600, 1100);
  h.demo();
  h.dropToasts();
  auto [A, B] = twoStates(h->world());
  CHECK(A && B);
  Id common = 0, own = 0, foreign = 0, uniq = 0, uniq2 = 0;
  CHECK(h->act("Технологии и постройки", [&](Tx& tx) {
    common = rules::createTech(tx, 0, "Сакральная архитектура");
    own = rules::createTech(tx, A, "Храмовые ритуалы");
    foreign = rules::createTech(tx, B, "Чужая тайна");
    uniq = rules::createBuilding(tx, A, "Святилище предков");
    tx.building(uniq).cat = BuildingCat::Religious;
    uniq2 = rules::createBuilding(tx, A, "Сокровищница рода");
  }));
  const Id market = buildingByName(h->world(), "Рынок");
  const Id barracks = buildingByName(h->world(), "Казармы");
  CHECK(market && barracks);
  // Общая постройка: только общие технологии.
  app::openBuildingTree(h.a(), 0, market);
  h.settle(40);
  CHECK(pickSide(h, "bt.side.addtech", "Сакральная"));
  CHECK(has(h->world().building(market)->techs, common));
  CHECK(revealSide(h, "bt.tech." + std::to_string(common)));
  std::string why;
  CHECK(!rules::canRequireTech(h->world(), market, own, &why));
  CHECK_MSG(why.find("только общие") != std::string::npos, why);
  CHECK(!h->act("Технология", [&](Tx& tx) { rules::setBuildingTech(tx, market, own, true); }));
  CHECK(hasToast(h.a(), "только общие", app::ToastKind::Warning));
  h->toasts().clear();
  // Общая постройка не может требовать уникальную (ТЗ «Доработки», п.7) — причина от правил.
  CHECK(!rules::canRequireBuilding(h->world(), market, uniq, &why));
  CHECK_MSG(why.find("уникальной") != std::string::npos, why);
  // Уникальная постройка государства A: общие и свои технологии, чужие — нет.
  app::openBuildingTree(h.a(), A, uniq);
  h.settle(40);
  CHECK(pickSide(h, "bt.side.addtech", "Храмовые"));
  CHECK(has(h->world().building(uniq)->techs, own));
  CHECK(!rules::canRequireTech(h->world(), uniq, foreign));
  CHECK(rules::canRequireTech(h->world(), uniq, common));
  // Уникальная может требовать общую постройку.
  CHECK(pickSide(h, "bt.side.addreq", "Казармы"));
  bool needsBarracks = false;
  for (const BuildingReq& r : h->world().building(uniq)->requires_) needsBarracks = needsBarracks || r.building == barracks;
  CHECK(needsBarracks);
  shotTrees(h, "trees_roles_techs");

  // Строительство: без изученной технологии — причина «Нужна технология …».
  StateProv sp;
  {
    auto c = rules::calc(h->world());
    h->world().provinces.each([&](const Province& p) {
      if (sp.province || p.sea || p.owner != A) return;
      const rules::ProvinceCalc* pc = c->province(p.id);
      if (pc && pc->slots > int(p.buildings.size())) sp = StateProv{A, p.id};
    });
  }
  CHECK(sp.province != 0);
  auto reasonsOf = [&](Id bid) {
    for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province))
      if (o.building == bid) return o.reasons;
    return std::vector<std::string>{};
  };
  auto mentions = [](const std::vector<std::string>& rs, const std::string& part) {
    for (const std::string& r : rs)
      if (r.find(part) != std::string::npos) return true;
    return false;
  };
  CHECK(mentions(reasonsOf(uniq), "Нужна технология «Храмовые ритуалы»"));
  h->toMap();
  h.settle(40);
  h->ui.tabOf[app::SelType::Province] = "province.buildings";
  h->select(app::SelType::Province, sp.province);
  h.settle(40);
  clickQuick(h, "prov.build");
  CHECK(h->hasDialog("build.picker"));
  clickQuick(h, "build.search");
  h.type("Святилище");
  h.settle(40);
  CHECK(h->uiRect("build.option." + std::to_string(uniq)) != nullptr);
  CHECK(h->uiRect("build.place." + std::to_string(uniq)) != nullptr);
  shotTrees(h, "trees_roles_tech_reason");
  CHECK(h.clickUi("dialog.cancel"));
  h.settle(40);
  // Изучена — причины технологии нет.
  CHECK(h->act("Изучить", [&](Tx& tx) { rules::setStudied(tx, own, true); }));
  CHECK(!mentions(reasonsOf(uniq), "Нужна технология «Храмовые"));
  h.key(Key::Escape);
  h.settle(40);

  // Дерево технологий: «Открывает постройки». Технология государства — только его уникальные постройки.
  app::openTechTree(h.a(), A, own);
  h.settle(40);
  CHECK(revealSide(h, "tt.side.unlocks", "tt.canvas"));
  CHECK(h->uiRect("tt.unlock." + std::to_string(uniq)) != nullptr);
  CHECK(pickSide(h, "tt.side.addunlock", "Сокровищница", "tt.canvas"));
  CHECK(has(h->world().building(uniq2)->techs, own));
  CHECK(!rules::canRequireTech(h->world(), barracks, own));
  shotTrees(h, "trees_roles_unlocks");
  // Убрать — крестик фишки.
  CHECK(revealSide(h, "tt.unlock." + std::to_string(uniq2), "tt.canvas"));
  RectF chip = rectOf(h.a(), "tt.unlock." + std::to_string(uniq2));
  h.click(chip.right() - 14, chip.cy());
  h.settle(40);
  CHECK(!has(h->world().building(uniq2)->techs, own));
  // Общая технология открывает любую постройку — общую и уникальную.
  app::openTechTree(h.a(), 0, common, A);
  h.settle(40);
  CHECK_EQ(h->ui.editorArg, Id(0));
  CHECK(pickSide(h, "tt.side.addunlock", "Казармы", "tt.canvas"));
  CHECK(has(h->world().building(barracks)->techs, common));
  CHECK(pickSide(h, "tt.side.addunlock", "Святилище", "tt.canvas"));
  CHECK(has(h->world().building(uniq)->techs, common));
  // Щелчок по фишке — дерево построек с этой постройкой.
  CHECK(clickSide(h, "tt.unlock." + std::to_string(barracks), "tt.canvas", 0.3f, 0.5f));
  CHECK_EQ(h->ui.editor, std::string("buildings"));
  CHECK_EQ(h->ui.editorArg, Id(0));
  h.settle(40);
  CHECK(h->uiRect("bt.side.name") != nullptr);
}
