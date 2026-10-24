// Сценарии дерева построек (ТЗ 1.h.i–ii, 1.f.ii): общее дерево и уникальные постройки государства (дерево
// государства показывает и общие), дорожки категорий, уровни (срок, стоимость, модификаторы; удаление уровня правит
// требования других построек), требования (цикл запрещён), вход из ленты и вкладки государства.
#include "app/editors/buildings.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;

namespace {

std::vector<Id> buildingsOf(const World& w, Id owner) {
  std::vector<Id> r;
  w.buildings.each([&](const Building& b) {
    if (b.owner == owner) r.push_back(b.id);
  });
  return r;
}

bool needs(const World& w, Id b, Id req) {
  const Building* x = w.building(b);
  if (!x) return false;
  for (const BuildingReq& r : x->requires_)
    if (r.building == req) return true;
  return false;
}

// Государство с уникальной постройкой (в демонстрационном мире — одно).
Id uniqueOwner(const World& w, Id* building = nullptr) {
  Id owner = 0;
  w.buildings.each([&](const Building& b) {
    if (!owner && b.owner) {
      owner = b.owner;
      if (building) *building = b.id;
    }
  });
  return owner;
}

// Плотное общее дерево: к четырём постройкам демонстрационного мира — ещё десять с требованиями.
void denseBuildings(app::App& a) {
  CHECK(a.act("Плотное дерево построек", [&](Tx& tx) {
    Id market = 0, barracks = 0, forge = 0, quarter = 0;
    tx.w().buildings.each([&](const Building& b) {
      if (b.owner) return;
      if (b.name == "Рынок") market = b.id;
      if (b.name == "Казармы") barracks = b.id;
      if (b.name == "Кузница") forge = b.id;
      if (b.name == "Плодородные угодья") quarter = b.id;
    });
    auto add = [&](const char* name, const char* icon, BuildingCat cat, int levels, std::initializer_list<std::pair<Id, int>> reqs) {
      Id id = rules::createBuilding(tx, 0, name);
      Building& b = tx.building(id);
      b.icon = icon;
      b.cat = cat;
      b.levels.clear();
      for (int l = 0; l < levels; l++) {
        BuildingLevel lv;
        lv.turns = 2 + l;
        lv.cost[kGold] = 120.0 * (l + 1);
        b.levels.push_back(lv);
      }
      for (auto [r, lvl] : reqs) b.requires_.push_back(BuildingReq{r, lvl});
      return id;
    };
    Id range = add("Стрельбище", "bow", BuildingCat::Military, 2, {{barracks, 1}});
    add("Конюшни", "horse", BuildingCat::Military, 3, {{barracks, 2}});
    add("Крепостные стены", "castle", BuildingCat::Military, 3, {});
    add("Банк", "treasury", BuildingCat::Economic, 2, {{market, 2}});
    add("Порт", "anchor", BuildingCat::Economic, 3, {});
    Id saw = add("Лесопилка", "wood", BuildingCat::Industrial, 1, {});
    Id quarry = add("Каменоломня", "stone", BuildingCat::Industrial, 2, {});
    add("Литейная", "factory", BuildingCat::Industrial, 2, {{forge, 1}, {quarry, 1}});
    add("Храм", "religion", BuildingCat::Residential, 2, {});
    add("Акведук", "house", BuildingCat::Residential, 1, {{quarter, 1}, {quarry, 2}});
    (void)range;
    (void)saw;
  }));
}

}  // namespace

TEST(app_trees_buildings_visual) {
  Harness h("trees_buildings_visual", 1600, 1000);
  h.demo();
  h.dropToasts();
  denseBuildings(h.a());
  app::openBuildingTree(h.a(), 0);
  h.settle();
  // Авторасстановка по требованиям.
  clickRect(h, "bt.layout");
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("buildings"));
  CHECK(h.shot("trees_buildings"));
  // Выбор постройки: панель свойств с уровнями.
  Id market = test::idByName(h->world().buildings, "Рынок");
  CHECK(market != 0);
  RectF nr = rectOf(h.a(), "bt.node." + std::to_string(market));
  h.click(nr.x + nr.w * 0.5f, nr.y + nr.h * 0.3f);
  h.settle();
  CHECK(h->uiRect("bt.side.name") != nullptr);
  h.move(nr.x + nr.w * 0.5f, nr.y + nr.h * 0.3f);
  h.frames(40);
  h.settle();
  CHECK(h.shot("trees_buildings_selected"));
}

TEST(app_trees_buildings_edit) {
  Harness h("trees_buildings_edit");
  h.demo();
  h.dropToasts();
  size_t n0 = buildingsOf(h->world(), 0).size();
  app::openBuildingTree(h.a(), 0);
  h.settle();
  // Новая постройка: в общем дереве, выбрана, имя в фокусе.
  clickRect(h, "bt.add");
  std::vector<Id> list = buildingsOf(h->world(), 0);
  CHECK_EQ(list.size(), n0 + 1);
  Id b = list.back();
  CHECK_EQ(h->world().building(b)->owner, Id(0));
  CHECK(h->uiRect("bt.side.name") != nullptr);
  h.retype("Мастерская");
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(h->world().building(b)->name, std::string("Мастерская"));
  // Категория — семь сегментов-значков (третий — промышленные, седьмой — инфраструктурные).
  RectF cat = rectOf(h.a(), "bt.side.cat");
  constexpr float kSegs = float(int(BuildingCat::Count));
  CHECK_EQ(int(BuildingCat::Count), 7);
  h.click(cat.x + cat.w * (2.5f / kSegs), cat.cy());
  h.settle();
  CHECK(h->world().building(b)->cat == BuildingCat::Industrial);
  // Уровни: добавить второй (копия первого), срок первого «+», стоимость — новый ресурс.
  CHECK_EQ(h->world().building(b)->levels.size(), size_t(1));
  CHECK(revealSide(h, "bt.side.addlevel"));
  RectF lv = rectOf(h.a(), "bt.side.addlevel");
  h.click(lv.right() - 21, lv.cy());
  h.settle();
  CHECK_EQ(h->world().building(b)->levels.size(), size_t(2));
  int t0 = h->world().building(b)->levels[0].turns;
  CHECK(revealSide(h, "bt.level.0.turns"));
  RectF tr = rectOf(h.a(), "bt.level.0.turns");
  h.click(tr.right() - 10, tr.cy());
  h.settle();
  CHECK_EQ(h->world().building(b)->levels[0].turns, t0 + 1);
  CHECK(clickSide(h, "bt.level.0.addcost"));
  CHECK(h->world().building(b)->levels[0].cost.count(kGold) == 1);
  CHECK_NEAR(h->world().building(b)->levels[0].cost.at(kGold), 100, 1e-9);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_buildings_levels"));
  // Удалить последний уровень — с подтверждением (панель свойств прокручена вниз).
  CHECK(clickSide(h, "bt.level.1.delete"));
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK_EQ(h->world().building(b)->levels.size(), size_t(1));
  // Требование: от выхода «Кузницы» к «Мастерской».
  Id forge = test::idByName(h->world().buildings, "Кузница");
  CHECK(forge != 0);
  h.key(Key::F);
  h.settle();
  RectF out = rectOf(h.a(), "bt.out." + std::to_string(forge));
  RectF nb = rectOf(h.a(), "bt.node." + std::to_string(b));
  h.drag(out.cx(), out.cy(), nb.cx(), nb.cy());
  h.settle();
  CHECK(needs(h->world(), b, forge));
  // Обратное требование — цикл, отказ.
  u64 v = h->store.version();
  RectF outB = rectOf(h.a(), "bt.out." + std::to_string(b));
  RectF nf = rectOf(h.a(), "bt.node." + std::to_string(forge));
  h.drag(outB.cx(), outB.cy(), nf.cx(), nf.cy());
  h.settle();
  CHECK(!needs(h->world(), forge, b));
  CHECK_EQ(h->store.version(), v);
  CHECK(hasToast(h, "цикл", app::ToastKind::Warning));
  h->toasts().clear();
  // Перетаскивание на другую дорожку меняет категорию (одно действие, отменяется).
  nb = rectOf(h.a(), "bt.node." + std::to_string(b));
  const RectF* cv = h->uiRect("bt.canvas");
  CHECK(cv != nullptr);
  BuildingCat c0 = h->world().building(b)->cat;
  CHECK(c0 == BuildingCat::Industrial);
  // К нижнему краю холста — последняя дорожка («Инфраструктурные»), какой бы высоты ни были дорожки выше.
  h.drag(nb.x + nb.w * 0.5f, nb.y + nb.h * 0.25f, nb.x + nb.w * 0.5f, cv->bottom() - nb.h * 0.75f);
  h.settle();
  CHECK(h->world().building(b)->cat != c0);
  undo(h);
  CHECK(h->world().building(b)->cat == c0);
  // Удаление постройки — с подтверждением, требования к ней снимаются.
  clickRect(h, "bt.node." + std::to_string(forge), 0.5f, 0.25f);
  h.key(Key::Delete);
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->world().building(forge) == nullptr);
  CHECK(!needs(h->world(), b, forge));
  undo(h);
  CHECK(h->world().building(forge) != nullptr);
}

// Удаление последнего уровня постройки проверяет только провинции, но не требования других построек
// (app/editors/buildings.cpp, кнопка «Удалить уровень»). Требование «R уровня N» к удалённому уровню остаётся,
// и зависящая постройка становится недоступной навсегда; после сохранения и повторного открытия нормализация
// молча меняет требование на другой уровень (поведение мира меняется от перезагрузки).
TEST(app_trees_buildings_delete_level_requirement) {
  Harness h("audit_tbmt_req_level", 1440, 2400);
  h.demo();
  // Требование к последнему уровню постройки, этот уровень нигде не построен.
  Id dep = 0, req = 0;
  int lvl = 0;
  h->world().buildings.each([&](const Building& b) {
    for (const BuildingReq& r : b.requires_) {
      const Building* rb = h->world().building(r.building);
      if (dep || !rb || r.level < 2 || r.level != int(rb->levels.size())) continue;
      bool built = false;
      h->world().provinces.each([&](const Province& p) {
        for (const ProvBuilding& pb : p.buildings) built = built || (pb.building == rb->id && pb.level >= r.level);
      });
      if (!built) {
        dep = b.id;
        req = rb->id;
        lvl = r.level;
      }
    }
  });
  if (!dep) {   // создать такое требование самим: постройка с двумя уровнями (уровни помещаются на панели)
    CHECK(h->act("Постройка", [&](Tx& tx) {
      req = rules::createBuilding(tx, 0, "Аудит: требуемая");
      tx.building(req).levels.push_back(BuildingLevel{});
    }));
    CHECK(req != 0);
    lvl = int(h->world().building(req)->levels.size());
    CHECK(h->act("Требование", [&](Tx& tx) {
      // Уровень lvl нигде не построен и не строится.
      for (Id pid : tx.w().provinces.ids())
        for (const ProvBuilding& pb : tx.w().province(pid)->buildings)
          if (pb.building == req && pb.level >= lvl) {
            auto& v = tx.province(pid).buildings;
            for (ProvBuilding& x : v)
              if (x.building == req) {
                x.level = lvl - 1;
                x.constructing = false;
                x.left = 0;
              }
          }
      dep = rules::createBuilding(tx, 0, "Аудит: зависимая");
      tx.building(dep).requires_.push_back(BuildingReq{req, lvl});
    }));
  }
  CHECK(dep && req && lvl >= 2);
  const Id owner = h->world().building(req)->owner;
  app::openBuildingTree(h.a(), owner, req);
  h.dropToasts();
  h.settle();
  h.settle();
  CHECK(h.shot("audit_tbmt_req_level_side"));
  std::string del = "bt.level." + std::to_string(lvl - 1) + ".delete";
  const RectF* r = h->uiRect(del);
  CHECK(r != nullptr);
  if (!r) return;
  h.click(r->cx(), r->cy());
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK_EQ(int(h->world().building(req)->levels.size()), lvl - 1);
  int still = 0;
  for (const BuildingReq& q : h->world().building(dep)->requires_)
    if (q.building == req) still = q.level;
  CHECK_MSG(still <= lvl - 1, "после удаления уровня " + std::to_string(lvl) + " постройка «" + h->world().building(dep)->name +
                                     "» по-прежнему требует несуществующий уровень " + std::to_string(still));
}

TEST(app_trees_buildings_unique_and_entries) {
  Harness h("trees_buildings_unique");
  h.demo();
  Id uniq = 0;
  Id owner = uniqueOwner(h->world(), &uniq);
  CHECK(owner != 0 && uniq != 0);
  // Раздел «Экономика» правой ленты: «Дерево построек» — общее дерево по категориям.
  CHECK(h.clickUi("section.economy"));
  h.settle();
  CHECK(h.clickUi("page.nav.buildings"));
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("buildings"));
  CHECK_EQ(h->ui.editorArg, Id(0));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_buildings_drawer"));
  Id market = test::idByName(h->world().buildings, "Рынок");
  clickRect(h, "bt.node." + std::to_string(market), 0.5f, 0.3f);
  CHECK(h->uiRect("bt.side.name") != nullptr);
  // Уникальная постройка в общем дереве не показывается.
  CHECK(h->uiRect("bt.node." + std::to_string(uniq)) == nullptr);
  h.key(Key::Escape);
  // Вкладка государства «Постройки».
  h->ui.tabOf[app::SelType::Faction] = "faction.buildings";
  h->select(app::SelType::Faction, owner);
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_buildings_faction_tab"));
  clickRect(h, "faction.buildings.unique");
  CHECK_EQ(h->ui.editor, std::string("buildings"));
  CHECK_EQ(h->ui.editorArg, owner);
  h.settle();
  CHECK(h->uiRect("bt.node." + std::to_string(uniq)) != nullptr);
  CHECK(h->uiRect("bt.node." + std::to_string(market)) == nullptr);
  // Новая постройка здесь — уникальная для государства (кроме неё — уникальные базовых записей: гильдия археологов,
  // штабы разведки и контрразведки).
  const size_t before = buildingsOf(h->world(), owner).size();
  clickRect(h, "bt.add");
  std::vector<Id> mine = buildingsOf(h->world(), owner);
  CHECK_EQ(mine.size(), before + 1);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_buildings_unique"));
  // Уникальная постройка доступна только провинциям владельца.
  bool inOwn = false, inOther = false;
  h->world().provinces.each([&](const Province& p) {
    if (p.sea || !p.owner) return;
    for (const rules::BuildOption& o : rules::buildOptions(h->world(), p.id))
      if (o.building == uniq) (p.owner == owner ? inOwn : inOther) = true;
  });
  CHECK(inOwn);
  CHECK(!inOther);
}

// ТЗ 1.h.i: дерево построек государства = общее дерево + уникальные постройки. Ссылка «Дерево построек государства»
// во вкладке «Постройки» провинции открывает дерево владельца, где видны ТОЛЬКО уникальные постройки
// (app/editors/buildings.cpp drawBuildingTree: `if (b.owner != owner) return;`). У государства без уникальных
// построек дерево пустое.
TEST(app_trees_buildings_state_tree_shows_common) {
  Harness h("audit_tbmt_state_tree", 1440, 900);
  h.demo();
  // Государство без уникальных построек.
  Id state = 0;
  h->world().factions.each([&](const Faction& f) {
    if (state || !f.isState()) return;
    bool uniq = false;
    h->world().buildings.each([&](const Building& b) { uniq = uniq || b.owner == f.id; });
    if (!uniq) state = f.id;
  });
  Id common = 0;
  h->world().buildings.each([&](const Building& b) {
    if (!common && b.owner == 0) common = b.id;
  });
  CHECK(common != 0);
  if (!state) {   // в демонстрационном мире у всех есть уникальные — берём любое государство
    h->world().factions.each([&](const Faction& f) {
      if (!state && f.isState()) state = f.id;
    });
  }
  CHECK(state != 0);
  app::openBuildingTree(h.a(), state);
  h.dropToasts();
  h.settle();
  h.settle();
  CHECK(h.shot("audit_tbmt_state_tree"));
  // Регрессия: дерево государства показывает и постройки общего дерева (только просмотр, со ссылкой на правку).
  CHECK_MSG(h->uiRect("bt.common." + std::to_string(common)) != nullptr,
            "дерево построек государства «" + h->world().faction(state)->name + "» не показывает постройки общего дерева");
  CHECK(h->uiRect("bt.common.edit") != nullptr);
  // Щелчок по постройке общего дерева открывает её в общем дереве.
  CHECK(h.clickUi("bt.common." + std::to_string(common)));
  h.settle();
  CHECK(h->ui.editor == "buildings");
  CHECK(h->uiRect("bt.common." + std::to_string(common)) == nullptr);   // общее дерево — без этого списка
  // Вкладка государства «Постройки» — тоже общее дерево вместе с уникальными.
  h->closeEditor();
  h->select(app::SelType::Faction, state);
  h->ui.tabOf[app::SelType::Faction] = "faction.buildings";
  h.settle();
  CHECK(h->uiRect("faction.buildings.commonItem." + std::to_string(common)) != nullptr);
}

TEST(app_trees_buildings_layout_external_icon) {
  Harness h("trees_buildings_external", 1600, 1000);
  h.demo();
  h.dropToasts();
  denseBuildings(h.a());
  // Авторасстановка: в дорожке категории карточки не перекрываются.
  app::openBuildingTree(h.a(), 0);
  h.settle();
  clickRect(h, "bt.layout");
  std::vector<Id> all = buildingsOf(h->world(), 0);
  for (size_t i = 0; i < all.size(); i++)
    for (size_t j = i + 1; j < all.size(); j++) {
      const Building* x = h->world().building(all[i]);
      const Building* y = h->world().building(all[j]);
      if (x->cat == y->cat) CHECK(!(x->pos == y->pos));
    }
  undo(h);
  // Значок постройки — из набора (подписи по-русски).
  Id market = test::idByName(h->world().buildings, "Рынок");
  clickRect(h, "bt.node." + std::to_string(market), 0.5f, 0.3f);
  clickRect(h, "bt.side.icon");
  CHECK(h->uiRect("bt.icon.castle") != nullptr);
  clickRect(h, "bt.icon.castle");
  CHECK_EQ(h->world().building(market)->icon, std::string("castle"));
  undo(h);
  CHECK(h->world().building(market)->icon != "castle");
  // Уровень требования: «Банк» требует «Рынок» II — поле уровня в списке требований.
  Id bank = test::idByName(h->world().buildings, "Банк");
  CHECK(bank != 0 && !h->world().building(bank)->requires_.empty());
  const Id bankMarket = h->world().building(bank)->requires_.front().building;   // «Рынок» демонстрационного мира
  h.key(Key::Escape);
  app::openBuildingTree(h.a(), 0, bank);
  h.settle();
  RectF lv = rectOf(h.a(), "bt.req." + std::to_string(bankMarket) + ".level");
  h.click(lv.right() - 39, lv.cy());   // «−» поля
  h.settle();
  int level = 0;
  for (const BuildingReq& r : h->world().building(bank)->requires_)
    if (r.building == bankMarket) level = r.level;
  CHECK_EQ(level, 1);
  undo(h);
  // Уникальная постройка требует общую («Казармы» II): плашка слева от карточки, щелчок — общее дерево.
  Id uniq = 0;
  Id owner = uniqueOwner(h->world(), &uniq);
  Id barracks = test::idByName(h->world().buildings, "Казармы");
  CHECK(owner && uniq && barracks);
  CHECK(h->act("Требование", [&](Tx& tx) { tx.building(uniq).requires_.push_back(BuildingReq{barracks, 2}); }));
  h.key(Key::Escape);
  app::openBuildingTree(h.a(), owner, uniq);
  h.settle();
  std::string ext = "bt.ext." + std::to_string(uniq) + "." + std::to_string(barracks);
  CHECK(h->uiRect(ext) != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_buildings_external"));
  clickRect(h, ext);
  CHECK_EQ(h->ui.editor, std::string("buildings"));
  CHECK_EQ(h->ui.editorArg, Id(0));
  h.settle();
  // В общем дереве выбраны «Казармы»: Delete спрашивает об удалении именно их.
  CHECK(h->uiRect("bt.side.delete") != nullptr);
  h.key(Key::Delete);
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h->dialogStack().back()->style(h.a()).title.find("Казармы") != std::string::npos);
  CHECK(h.clickUi("dialog.cancel"));
  h.settle();
  CHECK(h->world().building(barracks) != nullptr);
}

TEST(app_trees_buildings_faction_built) {
  Harness h("trees_buildings_faction_built");
  h.demo();
  h.dropToasts();
  // Государство с постройками в провинциях: раздел «В провинциях» — щелчок открывает дерево с постройкой.
  Id fid = 0, bid = 0;
  h->world().provinces.each([&](const Province& p) {
    const Faction* f = h->world().faction(p.owner);
    if (fid || !f || !f->isState()) return;
    for (const ProvBuilding& pb : p.buildings)
      if (!fid && pb.builtLevel() > 0) {
        fid = p.owner;
        bid = pb.building;
      }
  });
  CHECK(fid && bid);
  h->ui.tabOf[app::SelType::Faction] = "faction.buildings";
  h->select(app::SelType::Faction, fid);
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("trees_buildings_faction_built"));
  clickRect(h, "faction.buildings.built." + std::to_string(bid));
  CHECK_EQ(h->ui.editor, std::string("buildings"));
  CHECK_EQ(h->ui.editorArg, h->world().building(bid)->owner);
  CHECK(h->uiRect("bt.side.name") != nullptr);
}
