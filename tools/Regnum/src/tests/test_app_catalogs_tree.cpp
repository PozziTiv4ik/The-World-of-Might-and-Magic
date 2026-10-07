// Сценарии справочника «Ресурсы» деревом групп (ТЗ «Добавления в справочники», п.3–4): группы верхнего уровня
// свёрнуты, шеврон и двойной щелчок сворачивают и раскрывают, поиск раскрывает подходящие ветви; новая группа,
// подгруппа и ресурс в группе; ресурс в другую группу; удаление группы с переносом подгрупп и ресурсов в родителя
// (Ctrl+Z возвращает); группы правил с замком не удаляются; родительская группа без циклов; сортировка внутри групп;
// «Где используется» (строки войск, особые отряды, рецепты); время кадра и покой; снимки состояний.
#include <cstdio>
#include <functional>

#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;

namespace {

std::string gmark(Id g) { return "catalogs.group." + std::to_string(g); }
std::string rmark(Id r) { return "catalogs.res." + std::to_string(r); }

Id groupKey(const World& w, const char* key) { return w.catalogs->groupId(key); }
Id resNamed(const World& w, const std::string& name) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.name == name) return c.id;
  return 0;
}
Id groupNamed(const World& w, const std::string& name) {
  for (const ResGroup& g : w.catalogs->resGroups)
    if (g.name == name) return g.id;
  return 0;
}

// Прокрутить таблицу к началу, затем вниз, пока не построится строка mark (таблица строит только видимые строки).
bool scrollFind(Harness& h, const std::string& mark) {
  if (h->uiRect(mark)) return true;
  const RectF* t = h->uiRect("catalogs.table");
  if (!t) return false;
  const RectF tr = *t;
  h.wheel(tr.cx(), tr.cy(), 1000);
  h.settle();
  for (int i = 0; i < 80 && !h->uiRect(mark); i++) {
    h.wheel(tr.cx(), tr.cy(), -4);
    quick(h);
  }
  return h->uiRect(mark) != nullptr;
}

// Щелчок по строке таблицы справа (числа): выделение строки без правки названия.
bool clickRow(Harness& h, const std::string& mark) {
  if (!scrollFind(h, mark) || !reveal(h, mark, "catalogs.table", 40)) return false;
  const RectF* r = h->uiRect(mark);
  if (!r) return false;
  const RectF c = *r;
  h.click(c.right() - 130, c.cy());
  quick(h);
  return true;
}

void openResources(Harness& h) {
  h->openEditor("catalogs", 1);
  quick(h);
}

}  // namespace

TEST(app_catalogs_tree_collapse_search) {
  Harness h("catalogs_tree");
  h.demo();
  h.dropToasts();
  openResources(h);
  const World& w0 = h->world();
  const Id ore = groupKey(w0, schema::grp::Ore), oreCommon = groupKey(w0, schema::grp::OreCommon);
  const Id beasts = groupKey(w0, schema::grp::Beasts), mounts = groupKey(w0, schema::grp::MountsGround);
  const Id iron = resNamed(w0, "Железо"), horses = resNamed(w0, "Лошади");
  CHECK(ore && oreCommon && beasts && mounts && iron && horses);
  // Верхний уровень свёрнут: видны строки групп, подгрупп и ресурсов нет; в конце — «Без группы».
  for (const char* k : {schema::grp::Ore, schema::grp::Beasts, schema::grp::Provisions, schema::grp::Materials})
    CHECK_MSG(h->uiRect(gmark(groupKey(h->world(), k))) != nullptr, k);
  CHECK(h->uiRect(gmark(oreCommon)) == nullptr);
  CHECK(h->uiRect(rmark(iron)) == nullptr);
  CHECK(h->uiRect("catalogs.nogroup") != nullptr);
  {
    const RectF* a = h->uiRect(gmark(ore));
    const RectF* b = h->uiRect("catalogs.nogroup");
    CHECK(a && b && b->y > a->y);
  }
  shotClean(h, "catalogs_tree_collapsed");
  // Шеврон «Руда» — подгруппы раскрыты, ресурсы видны.
  CHECK(h.clickUi(gmark(ore) + ".toggle"));
  quick(h);
  CHECK(h->uiRect(gmark(oreCommon)) != nullptr);
  CHECK(h->uiRect(rmark(iron)) != nullptr);
  // Подгруппа «Обычная» свёрнута двойным щелчком по строке (не по названию) — железа нет.
  {
    const RectF* r = h->uiRect(gmark(oreCommon));
    CHECK(r != nullptr);
    if (r) h.doubleClick(r->right() - 140, r->cy());
    quick(h);
  }
  CHECK(h->uiRect(rmark(iron)) == nullptr);
  CHECK(h->uiRect(gmark(oreCommon)) != nullptr);
  // ← → при фокусе таблицы: выбранная группа раскрывается и сворачивается.
  CHECK(clickRow(h, gmark(oreCommon)));
  h.key(Key::Right);
  quick(h);
  CHECK(h->uiRect(rmark(iron)) != nullptr);
  h.key(Key::Left);
  quick(h);
  CHECK(h->uiRect(rmark(iron)) == nullptr);
  // Поиск раскрывает подходящие ветви: «желез» — железо в свёрнутой подгруппе, лошадей нет.
  h.key(Key::F, ctrl());
  h.type("желез");
  quick(h);
  CHECK(h->uiRect(rmark(iron)) != nullptr);
  CHECK(h->uiRect(gmark(oreCommon)) != nullptr);
  CHECK(h->uiRect(gmark(beasts)) == nullptr);
  CHECK(h->uiRect(rmark(horses)) == nullptr);
  // Поиск по названию группы показывает её содержимое: «ездовые наземные» — лошади.
  h.retype("ездовые наземные");
  quick(h);
  CHECK(h->uiRect(rmark(horses)) != nullptr);
  CHECK(h->uiRect(rmark(iron)) == nullptr);
  shotClean(h, "catalogs_tree_search");
  // Поиск очищен — прежнее раскрытие: «Обычная» снова свёрнута, «Звери» — тоже.
  h.key(Key::Escape);
  quick(h);
  h->openEditor("catalogs", 2);
  quick(h);
  openResources(h);
  CHECK(h->uiRect(rmark(iron)) == nullptr);
  CHECK(h->uiRect(gmark(oreCommon)) != nullptr);
  CHECK(h->uiRect(rmark(horses)) == nullptr);
  // «Развернуть все» и «Свернуть все».
  CHECK(h.clickUi("catalogs.expandAll"));
  quick(h);
  CHECK(h->uiRect(rmark(iron)) != nullptr);
  shotClean(h, "catalogs_tree_expanded");
  CHECK(scrollFind(h, rmark(horses)));   // ниже — «Звери / Ездовые наземные»
  CHECK(h.clickUi("catalogs.collapseAll"));
  quick(h);
  CHECK(h->uiRect(gmark(oreCommon)) == nullptr);
  CHECK(h->uiRect(gmark(mounts)) == nullptr);
  CHECK(h->uiRect(gmark(ore)) != nullptr);
}

TEST(app_catalogs_tree_create_move_remove) {
  Harness h("catalogs_tree_edit");
  h.demo();
  h.dropToasts();
  openResources(h);
  const size_t groups0 = h->world().catalogs->resGroups.size();
  // «Новая группа» — верхний уровень, название сразу в правке.
  CHECK(h.clickUi("catalogs.newGroup"));
  quick(h);
  CHECK_EQ(h->world().catalogs->resGroups.size(), groups0 + 1);
  const Id g = h->world().catalogs->resGroups.back().id;
  CHECK_EQ(h->world().catalogs->group(g)->parent, Id(0));
  h.retype("Трофеи");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().catalogs->group(g)->name, std::string("Трофеи"));
  // Повторное название рядом — отказ правила, название прежнее (F2 — правка выбранной группы).
  h.key(Key::F2);
  quick(h);
  h.retype("Руда");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().catalogs->group(g)->name, std::string("Трофеи"));
  h.dropToasts();
  // Подгруппа: кнопка в строке выбранной группы.
  CHECK(clickRow(h, gmark(g)));
  CHECK(h.clickUi(gmark(g) + ".addSub"));
  quick(h);
  const Id sub = h->world().catalogs->resGroups.back().id;
  CHECK(sub != g);
  CHECK_EQ(h->world().catalogs->group(sub)->parent, g);
  h.retype("Драконьи");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().catalogs->group(sub)->name, std::string("Драконьи"));
  // Новый ресурс в подгруппе: кнопка строки; название — сразу в правке.
  CHECK(clickRow(h, gmark(sub)));
  CHECK(h.clickUi(gmark(sub) + ".addRes"));
  quick(h);
  const CatalogItem scale = h->world().catalogs->resources.back();
  CHECK_EQ(scale.group, sub);
  h.retype("Драконья чешуя");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().resource(scale.id)->name, std::string("Драконья чешуя"));
  CHECK(h->uiRect(rmark(scale.id)) != nullptr);
  // Число ресурсов группы — с подгруппами.
  CHECK(rules::resourcesIn(h->world(), g).size() == 1);
  // Ресурс без группы → в «Трофеи» через карточку (выбор группы).
  const Id spice = resNamed(h->world(), "Пряности");
  CHECK(spice != 0);
  CHECK_EQ(h->world().resource(spice)->group, Id(0));
  CHECK(h.clickUi("catalogs.nogroup.toggle"));
  quick(h);
  CHECK(clickRow(h, rmark(spice)));
  CHECK(pickInCombo(h, "catalogs.resCard.group", "Трофеи"));
  CHECK_EQ(h->world().resource(spice)->group, g);
  CHECK(h->uiRect(rmark(spice)) != nullptr);   // строка ресурса видна в новой группе
  shotClean(h, "catalogs_tree_resource_card");
  // Карточка группы: подгруппа «Драконьи» → родитель — верхний уровень (без циклов: сама и подгруппы не предлагаются).
  CHECK(clickRow(h, gmark(sub)));
  CHECK(h->uiRect("catalogs.groupCard.parent") != nullptr);
  shotClean(h, "catalogs_tree_group_card");
  CHECK(pickInCombo(h, "catalogs.groupCard.parent", "Звери"));
  CHECK_EQ(h->world().catalogs->group(sub)->parent, groupKey(h->world(), schema::grp::Beasts));
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK_EQ(h->world().catalogs->group(sub)->parent, g);
  // Удаление группы «Трофеи» с подтверждением: подгруппа и ресурсы уходят на верхний уровень (ресурсы — без группы).
  CHECK(clickRow(h, gmark(g)));
  CHECK(h.clickUi(gmark(g) + ".delete"));
  quick(h);
  CHECK(h->hasDialog("confirm"));
  shotClean(h, "catalogs_tree_delete_confirm");
  CHECK(confirmDialog(h));
  CHECK(h->world().catalogs->group(g) == nullptr);
  CHECK_EQ(h->world().catalogs->group(sub)->parent, Id(0));
  CHECK_EQ(h->world().resource(spice)->group, Id(0));
  CHECK_EQ(h->world().resource(scale.id)->group, sub);
  // Удаление подгруппы с ресурсом: ресурс переходит в родителя (здесь — без группы).
  CHECK(clickRow(h, gmark(sub)));
  h.key(Key::Delete);
  CHECK(confirmDialog(h));
  CHECK(h->world().catalogs->group(sub) == nullptr);
  CHECK_EQ(h->world().resource(scale.id)->group, Id(0));
  // Ctrl+Z дважды — обе группы и связи вернулись.
  h.key(Key::Z, ctrl());
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK(h->world().catalogs->group(g) != nullptr);
  CHECK(h->world().catalogs->group(sub) != nullptr);
  CHECK_EQ(h->world().catalogs->group(sub)->parent, g);
  CHECK_EQ(h->world().resource(scale.id)->group, sub);
  CHECK_EQ(h->world().resource(spice)->group, g);
}

TEST(app_catalogs_tree_rule_groups) {
  Harness h("catalogs_tree_rules");
  h.demo();
  h.dropToasts();
  openResources(h);
  const Id prov = groupKey(h->world(), schema::grp::Provisions), beasts = groupKey(h->world(), schema::grp::Beasts);
  const Id ore = groupKey(h->world(), schema::grp::Ore);
  // Группы правил — замок вместо удаления; обычная группа — корзина.
  CHECK(h->uiRect(gmark(prov) + ".lock") != nullptr);
  CHECK(h->uiRect(gmark(prov) + ".delete") == nullptr);
  CHECK(h->uiRect(gmark(beasts) + ".lock") != nullptr);
  CHECK(h->uiRect(gmark(ore) + ".delete") != nullptr);
  // Delete по группе правил — уведомление, подтверждения нет; в карточке — «Группа правил», удаления нет.
  CHECK(clickRow(h, gmark(prov)));
  CHECK(h->uiRect("catalogs.groupCard.rule") != nullptr);
  CHECK(h->uiRect("catalogs.groupCard.delete") == nullptr);
  const size_t toasts = h->toasts().size();
  h.key(Key::Delete);
  quick(h);
  CHECK(!h->hasDialog("confirm"));
  CHECK(h->toasts().size() > toasts);
  CHECK(h->world().catalogs->group(prov) != nullptr);
  shotClean(h, "catalogs_tree_rule_group");
  // Подгруппа правил «Чудовища» (внутри «Звери»; поиск по названию раскрывает её): тоже замок; правило не даёт
  // удалить и напрямую.
  const Id monsters = groupKey(h->world(), schema::grp::Monsters);
  h.key(Key::F, ctrl());
  h.type("чудовища");
  quick(h);
  CHECK(h->uiRect(gmark(beasts)) != nullptr);
  CHECK(h->uiRect(gmark(monsters) + ".lock") != nullptr);
  CHECK(!h->act("Удалить", [&](Tx& tx) { rules::removeResGroup(tx, monsters); }));
  CHECK(h->world().catalogs->group(monsters) != nullptr);
  h.dropToasts();
  h->openEditor("catalogs", 2);   // смена вкладки очищает поиск
  quick(h);
  openResources(h);
  // Переименовать группу правил можно (ключ правил остаётся): двойной щелчок по названию — правка на месте.
  {
    const RectF* r = h->uiRect(gmark(prov) + ".name");
    CHECK(r != nullptr);
    if (r) h.doubleClick(r->x + 8, r->cy());
    quick(h);
  }
  h.retype("Припасы");
  h.key(Key::Enter);
  quick(h);
  CHECK_EQ(h->world().catalogs->group(prov)->name, std::string("Припасы"));
  CHECK_EQ(h->world().catalogs->groupId(schema::grp::Provisions), prov);
  CHECK(groupNamed(h->world(), "Провизия") == 0);
}

// Сортировка по столбцу упорядочивает соседей внутри своих групп: дерево не разрывается.
TEST(app_catalogs_tree_sort) {
  Harness h("catalogs_tree_sort");
  h.demo();
  h.dropToasts();
  openResources(h);
  const World& w0 = h->world();
  const Id ore = groupKey(w0, schema::grp::Ore), common = groupKey(w0, schema::grp::OreCommon), special = groupKey(w0, schema::grp::OreSpecial);
  const Id iron = resNamed(w0, "Железо"), silver = resNamed(w0, "Серебро");
  CHECK(h.clickUi(gmark(ore) + ".toggle"));
  quick(h);
  auto y = [&](const std::string& mark) {
    const RectF* r = h->uiRect(mark);
    return r ? r->y : -1.f;
  };
  // Без сортировки — порядок справочника: серебро выше железа.
  CHECK(y(rmark(silver)) >= 0 && y(rmark(silver)) < y(rmark(iron)));
  // «Добыча» по возрастанию, затем по убыванию: железо (добывается) — ниже, затем выше серебра; группа — над ними.
  CHECK(h.clickUi("catalogs.head.4"));
  quick(h);
  CHECK(y(rmark(silver)) < y(rmark(iron)));
  CHECK(h.clickUi("catalogs.head.4"));
  quick(h);
  CHECK(y(rmark(iron)) >= 0 && y(rmark(iron)) < y(rmark(silver)));
  CHECK(y(gmark(common)) < y(rmark(iron)));
  CHECK(y(gmark(ore)) < y(gmark(common)));
  shotClean(h, "catalogs_tree_sorted");
  // Третий щелчок — без сортировки; «Название» по убыванию: «Особая» над «Обычной», ресурсы — внутри своих групп.
  CHECK(h.clickUi("catalogs.head.4"));
  quick(h);
  CHECK(h.clickUi("catalogs.head.0"));
  quick(h);
  CHECK(h.clickUi("catalogs.head.0"));
  quick(h);
  CHECK(y(gmark(special)) >= 0 && y(gmark(special)) < y(gmark(common)));
  CHECK(y(rmark(silver)) >= 0 && y(gmark(common)) < y(rmark(silver)));
  CHECK(y(gmark(ore)) < y(gmark(special)));
}

// «Где используется» ресурса: строки войск (ключевой и дополнительные ресурсы), особые отряды, рецепты построек
// преобразования; фишка особого отряда открывает его вкладку, рецепта — дерево построек.
TEST(app_catalogs_tree_usage) {
  Harness h("catalogs_tree_usage");
  h.demo();
  h.dropToasts();
  const World& w0 = h->world();
  const Id black = resNamed(w0, "Черные Драконы"), steel = resNamed(w0, "Обсидиановая сталь");
  Id dragons = 0, citadel = 0;
  for (const SpecialUnit& s : w0.catalogs->specials)
    if (s.name == "Драконы Бездны") dragons = s.id;
  w0.buildings.each([&](const Building& b) {
    if (b.name == "Цитадель Бездны") citadel = b.id;
  });
  CHECK(black && steel && dragons && citadel);
  auto [state, other] = twoStates(w0);
  (void)other;
  Id row = 0, forge = 0;
  CHECK(h->act("Драконы и преобразование", [&](Tx& tx) {
    Id p = 0;
    tx.w().provinces.each([&](const Province& pr) {
      if (!p && !pr.sea && pr.owner == state) p = pr.id;
    });
    tx.province(p).buildings.push_back(ProvBuilding{citadel, 1, false, 0});
    row = rules::addSpecialRow(tx, state, dragons);
    forge = rules::createBuilding(tx, 0, "Драконья кузня");
    rules::setBuildingRole(tx, forge, rules::BuildingRole::Convert, true);
    Recipe rc;
    rc.in = {ResAmount{steel, 5}};
    rc.out = ResAmount{black, 1};
    rc.turns = 2;
    rules::setRecipe(tx, forge, rc);
  }));
  openResources(h);
  h.key(Key::F, ctrl());
  h.type("Черные Драконы");
  quick(h);
  CHECK(clickRow(h, rmark(black)));
  CHECK(reveal(h, "catalogs.usage.specials"));
  CHECK(h->uiRect("catalogs.usage.rows") != nullptr);
  CHECK(h->uiRect("catalogs.usage.row." + std::to_string(row)) != nullptr);
  CHECK(h->uiRect("catalogs.usage.recipes") != nullptr);
  CHECK(h->uiRect("catalogs.usage.recipe." + std::to_string(forge)) != nullptr);
  shotClean(h, "catalogs_tree_usage");
  // Фишка особого отряда — вкладка «Особые отряды» на нём.
  CHECK(clickRevealed(h, "catalogs.usage.special." + std::to_string(dragons)));
  quick(h);
  CHECK(h->uiRect("catalogs.view.specials") != nullptr);
  CHECK(h->uiRect("catalogs.special.rows") != nullptr);
  // Обратно к ресурсам: фишка рецепта открывает дерево построек.
  h->openEditor("catalogs", 1);
  quick(h);
  CHECK(reveal(h, "catalogs.usage.recipe." + std::to_string(forge)));
  CHECK(h.clickUi("catalogs.usage.recipe." + std::to_string(forge)));
  quick(h);
  CHECK_EQ(h->ui.editor, std::string("buildings"));
}

// Около 150 ресурсов: дерево раскрыто целиком, выбран ресурс (карточка с использованием) — кадр быстрый, в покое
// окно не просит новых кадров (все девять вкладок и окно модификаторов).
TEST(app_catalogs_tree_perf) {
  Harness h("catalogs_tree_perf", 1600, 1000);
  h.demo();
  h.dropToasts();
  openResources(h);
  CHECK(h.clickUi("catalogs.expandAll"));
  h.settle();
  const Id iron = resNamed(h->world(), "Железо");
  CHECK(clickRow(h, rmark(iron)));
  h.settle();
  const RectF* tr = h->uiRect("catalogs.table");
  CHECK(tr != nullptr);
  const RectF t = tr ? *tr : RectF{};
  auto frameMs = [&](int n, const std::function<void(int)>& before) {
    double total = 0;
    for (int i = 0; i < n; i++) {
      before(i);
      hl::pump();
      const double t0 = nowSeconds();
      hl::renderFrame();
      total += nowSeconds() - t0;
      hl::advance(1.0 / 60);
    }
    return total * 1000 / n;
  };
  const double hover = frameMs(30, [&](int i) { hl::mouseMove(t.x + 120 + float(i % 7) * 60, t.y + 60 + float(i) * 17); });
  const double scroll = frameMs(20, [&](int i) { hl::wheel(t.cx(), t.cy(), i % 2 ? 2.f : -2.f); });
  std::printf("  дерево ресурсов: наведение %.2f мс, прокрутка %.2f мс\n", hover, scroll);
  CHECK(hover < rg::test::perf(40));
  CHECK(scroll < rg::test::perf(40));
  // Покой: ни одна вкладка не просит кадров без событий.
  for (int n = 1; n <= 9; n++) {
    h->openEditor("catalogs", Id(n));
    h.settle();
    for (int i = 0; i < 60 && ui::needsRedraw(); i++) h.frame();
    CHECK_MSG(!ui::needsRedraw(), std::to_string(n));
  }
  h->openEditor("modifiers", 0);
  h.settle();
  for (int i = 0; i < 60 && ui::needsRedraw(); i++) h.frame();
  CHECK(!ui::needsRedraw());
}
