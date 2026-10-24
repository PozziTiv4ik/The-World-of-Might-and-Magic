// Роль за редактором (ТЗ «Доработки №4», п.5): окно выбора роли при открытии мира, панель государств (основные игровые —
// первыми, поиск), игрок за государство — выделение и показ столицы, признак роли в верхней строке, смена роли из меню
// мира, мир без государств, роль при перечитывании мира; ограничения игрока: справочники и инструменты правки скрыты и
// недоступны по клавишам и палитре, act() отказывает, история ходов — только просмотр, дерево технологий — просмотр
// и исследование за своё государство; администратор — без ограничений.
#include "app/shell/role.h"
#include "tests/test_app_role_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;

namespace {

// Видимые на экране пункты списка государств по порядку сверху вниз.
std::vector<Id> listedStates(Harness& h) {
  std::vector<std::pair<float, Id>> rows;
  h->world().factions.each([&](const Faction& f) {
    if (const RectF* r = h->uiRect("role.state." + std::to_string(f.id))) rows.push_back({r->y, f.id});
  });
  std::sort(rows.begin(), rows.end());
  std::vector<Id> out;
  for (auto& [y, id] : rows) out.push_back(id);
  return out;
}

}  // namespace

// Открытие мира — окно роли; «Игрок» — список государств (основные первыми, поиск); выбор — роль игрока, своё
// государство выделено, карта показывает его столицу, в верхней строке — флаг и название.
TEST(app_role_choose_on_open) {
  Harness h("role_choose");
  h->impl().cfg.askRole = true;
  const map::Basemap* bm = h->basemap();
  CHECK(bm != nullptr);
  World w = map::makeDemoWorld(*bm);
  // Два основных игровых государства с названиями в конце алфавита и столица выбранного государства.
  Id hel = 0, solmar = 0, target = 0, capital = 0;
  {
    Tx tx(w);
    tx.w().factions.each([&](const Faction& f) {
      if (f.name.find("Хельдвиг") != std::string::npos) hel = f.id;
      if (f.name.find("Солмар") != std::string::npos) solmar = f.id;
    });
    CHECK(hel && solmar);
    tx.faction(hel).mainState = true;
    tx.faction(solmar).mainState = true;
    target = hel;
    tx.w().provinces.each([&](const Province& p) {
      if (p.owner == hel && p.name == "Хельдгард") capital = p.id;
    });
    CHECK(capital != 0);
    tx.faction(hel).capital = capital;
    w = std::move(tx).finish();
  }
  h->loadWorld(std::move(w), "Демонстрационный мир");
  h.settle();
  CHECK(h->hasDialog("role"));
  CHECK(h->admin());
  CHECK(h->uiRect("role.admin") != nullptr);
  CHECK(h->uiRect("role.player") != nullptr);
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("role_dialog"));

  // «Игрок» — список государств: основные первыми, дальше остальные по названию.
  CHECK(h.clickUi("role.player"));
  h.settle();
  CHECK(h->uiRect("role.search") != nullptr);
  const std::vector<Id> order = role::playableStates(h->world());
  CHECK(order.size() >= 3);
  CHECK(h->world().faction(order[0])->mainState && h->world().faction(order[1])->mainState);
  for (size_t i = 2; i < order.size(); i++) CHECK(!h->world().faction(order[i])->mainState);
  const std::vector<Id> shown = listedStates(h);
  CHECK(shown.size() >= 3);
  for (size_t i = 0; i < shown.size() && i < order.size(); i++) CHECK_EQ(shown[i], order[i]);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("role_states"));

  // Поиск сужает список.
  h.type("Корвен");
  h.settle();
  const std::vector<Id> found = listedStates(h);
  CHECK_EQ(found.size(), size_t(1));
  CHECK(found.size() == 1 && h->world().faction(found[0])->name.find("Корвен") != std::string::npos);
  h.key(Key::A, ctrl());
  h.key(Key::Backspace);
  h.settle();
  CHECK(listedStates(h).size() >= 3);

  // Выбор государства.
  CHECK(h.clickUi("role.state." + std::to_string(target)));
  h.settle();
  h.waitMap();
  h.settle();
  CHECK(!h->hasDialog("role"));
  CHECK(h->player());
  CHECK_EQ(h->playerState(), target);
  CHECK(h->ui.sel == (app::Selection{app::SelType::Faction, target}));
  CHECK(h->view() == app::View::Map);
  // Карта показывает столицу: середина свободной части карты — у столичной провинции.
  const RectF area = h->mapArea();
  const Vec2 mid = h->map().view().toMap(area.cx(), area.cy());
  const Box2 cb = app::detail::selectionBox(h->world(), {app::SelType::Province, capital});
  CHECK_MSG(dist(mid, cb.center()) < 60, strf("середина карты %.0f,%.0f, столица %.0f,%.0f", mid.x, mid.y, cb.center().x, cb.center().y));
  // Признак роли в верхней строке: щелчок — страница государства.
  const RectF* badge = h->uiRect("topbar.role");
  CHECK(badge != nullptr);
  shotClean(h, "role_player");
  if (badge) {
    CHECK(h.clickUi("topbar.role"));
    h.settle();
    CHECK(h->view() == app::View::Entity);
    CHECK(h->ui.sel == (app::Selection{app::SelType::Faction, target}));
    CHECK(h->uiRect("topbar.role") != nullptr);   // и на странице
    shotClean(h, "role_player_page");
  }
}

// Мир без государств: «Игрок» недоступен; смена роли — пункт меню мира и команда палитры, на игрока — выбор государства.
TEST(app_role_menu_and_no_states) {
  Harness h("role_menu");
  h->impl().cfg.askRole = true;
  CHECK(h->newWorld("Пустой мир"));
  h.settle();
  CHECK(h->hasDialog("role"));
  CHECK(h.clickUi("role.player"));   // недоступен
  h.settle();
  CHECK(h->hasDialog("role"));
  CHECK(h->uiRect("role.search") == nullptr);
  CHECK(h.clickUi("role.admin"));
  h.settle();
  CHECK(!h->hasDialog("role"));
  CHECK(h->admin());

  // Демонстрационный мир: смена роли через меню мира.
  h.demo();
  CHECK(h->hasDialog("role"));
  h->closeDialogs();
  h.settle();
  CHECK(h.clickUi("topbar.world"));
  h.settle();
  bool found = false;
  for (const app::CommandDef& c : app::commands()) found = found || std::string_view(c.id) == "app.role";
  CHECK(found);
  h.key(Key::Escape);
  CHECK(app::runCommand(h.a(), "app.role"));
  h.settle();
  CHECK(h->hasDialog("role"));
  CHECK(h.clickUi("role.player"));
  h.settle();
  const std::vector<Id> order = role::playableStates(h->world());
  CHECK(!order.empty());
  h.key(Key::Enter);   // первое государство списка
  h.settle();
  CHECK(h->player());
  CHECK_EQ(h->playerState(), order[0]);
  // Снова: окно открывается со списком на месте текущего государства; «Администратор редактора» — снять роль.
  CHECK(app::runCommand(h.a(), "app.role"));
  h.settle();
  CHECK(h.clickUi("role.admin"));
  h.settle();
  CHECK(h->admin());
  CHECK_EQ(h->playerState(), Id(0));
}

// Роль — на сеанс мира: открыт другой мир — снова окно; тот же мир перечитан — роль остаётся; последнее государство
// мира запоминается (roles.json) и окно предлагает его первым.
TEST(app_role_session_and_memory) {
  Harness h("role_session");
  h.demo();
  const std::string folder = fs::join(h.root, "Мир");
  CHECK(h->saveTo(folder));
  h->impl().cfg.askRole = true;
  CHECK(h->loadProject(folder));
  h.settle();
  CHECK(h->hasDialog("role"));
  const Id hel = factionByName(h->world(), "Хельдвиг");
  h->closeDialogs();
  role::becomePlayer(h.a(), hel);
  h.settle();
  CHECK(h->player());
  // Перечитать тот же мир (как «Перечитать с диска») — роль остаётся, окна нет.
  CHECK(h->loadProject(folder));
  h.settle();
  CHECK(!h->hasDialog("role"));
  CHECK(h->player());
  CHECK_EQ(h->playerState(), hel);
  // Закрыть и открыть снова — окно роли, запомненное государство — на карточке «Игрок».
  h->closeWorld();
  h.settle();
  CHECK(h->admin());
  CHECK(h->loadProject(folder));
  h.settle();
  CHECK(h->hasDialog("role"));
  CHECK(h->admin());
  auto text = fs::readFile(fs::join(h->dataDir(), "roles.json"));
  CHECK(text.has_value() && text->find("player") != std::string::npos);
  h.waitMap();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("role_dialog_remembered"));
  CHECK(h.clickUi("role.player"));
  h.settle();
  h.key(Key::Enter);   // подсвечено запомненное государство
  h.settle();
  CHECK(h->player());
  CHECK_EQ(h->playerState(), hel);
}

// Игрок: раздел «Справочники», правка границ и карты, новое войско и флот, торговый маршрут скрыты и недоступны
// клавишами и палитрой; act() отказывает с причиной, play() — нет; история ходов — без возврата; «Завершить ход»,
// сохранение и экспорт доступны. Администратор — всё как прежде.
TEST(app_role_player_restrictions) {
  Harness h("role_restrict");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  // Администратор: всё на месте.
  CHECK(h->uiRect("section.reference") != nullptr);
  CHECK(h->uiRect("tool.borders") != nullptr);
  CHECK(h->uiRect("tool.mapedit") != nullptr);
  CHECK(h->uiRect("tool.army") != nullptr);
  CHECK(h->uiRect("tool.route") != nullptr);

  role::becomePlayer(h.a(), hel);
  h.settle();
  h.dropToasts();
  CHECK(h->player());
  CHECK(h->uiRect("section.reference") == nullptr);
  CHECK(h->uiRect("section.states") != nullptr);
  for (const char* t : {"tool.borders", "tool.mapedit", "tool.army", "tool.fleet", "tool.route", "tool.borders-tool"}) CHECK_MSG(h->uiRect(t) == nullptr, t);
  CHECK(h->uiRect("tool.select") != nullptr);
  CHECK(h->uiRect("tool.pan") != nullptr);
  // Клавиши: E, T, A, Shift+A, R, Ctrl+7, Ctrl+8 — без реакции.
  h->clearSelection();
  h.settle();
  h.key(Key::E);
  CHECK(!h->ui.editBorders);
  h.key(Key::T);
  CHECK(!h->ui.editMap);
  h.key(Key::A);
  CHECK(h->ui.tool == app::ToolId::Select);
  h.key(Key::A, platform::ModShift);
  CHECK(h->ui.tool == app::ToolId::Select);
  h.key(Key::R);
  CHECK(h->ui.tool == app::ToolId::Select);
  h.key(Key::D7, ctrl());
  CHECK(h->view() == app::View::Map);
  h.key(Key::D8, ctrl());
  CHECK(h->ui.editor.empty());
  // Прямые вызовы — отказ.
  h->setEditBorders(true);
  CHECK(!h->ui.editBorders);
  h->setEditMap(true);
  CHECK(!h->ui.editMap);
  h->setTool(app::ToolId::Route);
  CHECK(h->ui.tool == app::ToolId::Select);
  h->setTool(app::ToolId::NewArmy);   // не кнопкой своего государства
  CHECK(h->ui.tool == app::ToolId::Select);
  h->openSection("reference");
  CHECK(h->view() == app::View::Map);
  for (const char* e : {"catalogs", "modifiers", "constants"}) {
    h->openEditor(e);
    CHECK_MSG(h->ui.editor.empty(), e);
  }
  for (const char* c : {"editor.catalogs", "editor.modifiers", "editor.constants", "map.borders", "map.edit"}) CHECK_MSG(!app::runCommand(h.a(), c), c);
  // Палитра: справочников и режимов правки нет.
  h->showPalette();
  h.settle();
  h.type("Справочники");
  h.settle();
  CHECK(h->uiRect("palette.search") != nullptr);
  bool any = false;
  for (const app::CommandDef& c : app::commands())
    if (std::string_view(c.id) == "editor.catalogs") any = role::commandAllowed(h.a(), c.id);
  CHECK(!any);
  h.key(Key::Escape);
  h.settle();
  // act() — отказ с причиной, play() — выполняется.
  h.dropToasts();
  const int turn = h->world().turn();
  CHECK(!h->act("Правка", [&](Tx& tx) { tx.faction(hel).name = "Чужое имя"; }));
  CHECK(hasToast(h, role::kAdminOnly));
  CHECK(h->world().faction(hel)->name != "Чужое имя");
  CHECK(h->play("Игровое", [&](Tx& tx) { tx.faction(hel).tax = 12; }));
  CHECK_EQ(h->world().faction(hel)->tax, 12.0);
  // «Завершить ход» (отметка готовности), сохранение и экспорт — доступны; сам ход завершает только администратор.
  CHECK(app::findCommand("turn.end")->enabled(h.a()));
  CHECK(app::findCommand("file.save")->enabled(h.a()));
  CHECK(app::findCommand("file.export")->enabled(h.a()));
  h.dropToasts();
  CHECK(!h->endTurnNow());
  CHECK_EQ(h->world().turn(), turn);
  CHECK(hasToast(h, "администратор"));
  CHECK(adminEndTurn(h));
  CHECK_EQ(h->world().turn(), turn + 1);
  h->closeDialogs();
  h.settle();
  // История ходов — просмотр без возврата.
  CHECK(h->openDialog("turn.history"));
  h.settle();
  CHECK(h->uiRect("history.open." + std::to_string(turn)) != nullptr);
  CHECK(h->uiRect("history.rollback." + std::to_string(turn)) == nullptr);
  CHECK(h->uiRect("history.rollback." + std::to_string(turn + 1)) == nullptr);
  h->closeDialogs();
  h.settle();
  // Дерево технологий — только просмотр.
  h->openEditor("techtree", hel);
  h.settle();
  CHECK(h->ui.editor == "techtree");
  CHECK(h->uiRect("tt.add") != nullptr);
  shotClean(h, "role_player_techtree");
  h->toMap();
  h.settle();

  // Администратор: всё снова доступно.
  role::becomeAdmin(h.a());
  h.settle();
  CHECK(h->uiRect("section.reference") != nullptr);
  CHECK(h->uiRect("tool.borders") != nullptr);
  h.key(Key::E);
  CHECK(h->ui.editBorders);
  h.key(Key::E);
  CHECK(h->act("Правка", [&](Tx& tx) { tx.faction(hel).tax = 11; }));
  CHECK(h->openDialog("turn.history"));
  h.settle();
  CHECK(h->uiRect("history.rollback." + std::to_string(turn)) != nullptr);
}

// Одна новая постройка в провинции за ход (правило игрока через play и buildOptions(…, true)).
TEST(app_role_player_one_building_per_turn) {
  Harness h("role_build");
  h.demo();
  // Провинция государства с двумя свободными слотами и две постройки, которые там можно начать (запасы — с избытком).
  const trees::StateProv sp = trees::stateWithFreeSlots(h->world(), 2);
  CHECK(sp.province != 0);
  if (!sp.province) return;
  trees::setStock(h.a(), sp.state, 1e6);
  std::vector<Id> can;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province, true))
    if (o.can && !o.upgrade) can.push_back(o.building);
  CHECK(can.size() >= 2);
  if (can.size() < 2) return;
  role::becomePlayer(h.a(), sp.state);
  h.settle();
  h.dropToasts();
  // Через окно строительства: первая видимая доступная постройка.
  CHECK(h->openDialog("build.picker", sp.province));
  h.settle();
  Id started = 0;
  for (Id b : can)
    if (const RectF* r = h->uiRect("build.option." + std::to_string(b)); r && r->y > 0 && r->bottom() < 900) {
      started = b;
      h.click(r->cx(), r->cy());
      break;
    }
  h.settle();
  CHECK(started != 0);
  if (!started) {
    h->closeDialogs();
    CHECK(h->play("Строить", [&](Tx& tx) { rules::startBuilding(tx, sp.province, can[0], true); }));
    started = can[0];
  }
  CHECK_EQ(h->world().province(sp.province)->buildTurn, h->world().turn());
  CHECK(trees::provBuilding(h->world(), sp.province, started) != nullptr);
  const Id other = started == can[0] ? can[1] : can[0];
  // Стройка убрана (иначе мешает общее правило «одновременно — одна постройка»): ход строительства в провинции — тот же.
  h->store.transact("Убрать стройку", [&](Tx& tx) {
    auto& bs = tx.province(sp.province).buildings;
    bs.erase(std::remove_if(bs.begin(), bs.end(), [&](const ProvBuilding& pb) { return pb.building == started; }), bs.end());
  });
  can = {started, other};
  // Вторая в той же провинции в этом ходу — нельзя (администратору — можно).
  bool blocked = true, adminCan = false;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province, true))
    if (!o.upgrade) blocked = blocked && !o.can;
  for (const rules::BuildOption& o : rules::buildOptions(h->world(), sp.province, false)) adminCan = adminCan || (o.building == can[1] && o.can);
  CHECK(blocked);
  CHECK(adminCan);
  CHECK(!h->play("Строить ещё", [&](Tx& tx) { rules::startBuilding(tx, sp.province, can[1], true); }));
  // Следующий ход — снова можно.
  CHECK(adminEndTurn(h));
  h->closeDialogs();
  h.settle();
  CHECK(h->play("Строить ещё", [&](Tx& tx) { rules::startBuilding(tx, sp.province, can[1], true); }));
}
