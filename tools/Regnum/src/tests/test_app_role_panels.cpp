// Сценарии ролей в панелях государства и войск (ТЗ «Доработки №4», п.5): игрок за своё государство — совет
// (назначение, новое место), найм отряда, гарнизон, флот в торговле, исследование, сделка торговли, объявление войны,
// окно битвы за свою сторону; поля правки мира (название, вид государства, значение отношений, содержание советника,
// «Правка резерва», казна и ресурсы) ему недоступны; чужое государство — только просмотр; администратор — всё как раньше.
#include "app/editors/trade.h"
#include "tests/test_app_faction_util.h"
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

namespace {

constexpr const char* kOwn = "Королевство Альмарин";
constexpr const char* kForeign = "Республика Корвен";

// Роль «игрок за государство state» (как после выбора роли при открытии мира).
void asPlayer(Harness& h, Id state) {
  h->ui.role = app::Role::Player;
  h->ui.playerState = state;
  h.settle();
}

// Вкладка фракции в панели справа от карты (узкие таблицы войск — правка в карточке строки).
void panelTab(Harness& h, Id faction, const char* tab) {
  h->ui.tabOf[app::SelType::Faction] = tab;
  h->select(app::SelType::Faction, faction, true);
  h.settle();
  h.dropToasts();
  h.settle();
}

// Ввести текст в поле: щелчок, текст, Tab (без Ctrl+A: у недоступного поля сочетание ушло бы карте).
void typeInto(Harness& h, const std::string& mark, const std::string& text) {
  CHECK_MSG(clickIn(h, mark), mark);
  h.type(text);
  h.key(Key::Tab);
  h.settle();
}

}  // namespace

// Совет своего государства: новое место и назначение советника — игровые действия; своя должность и «Новый персонаж»
// не предлагаются, содержание советника — только для чтения.
TEST(app_role_panels_council) {
  Harness h("role_council");
  h.demo();
  const Id own = findFaction(h->world(), kOwn);
  CHECK(own != 0);
  asPlayer(h, own);
  openTab(h, own, "faction.council");
  const size_t n0 = h->world().faction(own)->council.size();
  CHECK(n0 > 0 && n0 < size_t(schema::kMaxCouncilSeats));
  CHECK(clickIn(h, "council.add"));
  h.step();
  CHECK_EQ(h->world().faction(own)->council.size(), n0 + 1);
  CHECK(h->world().faction(own)->council.back().character == 0);
  // Назначить свободного героя на новое место: список — «Вакантно», затем доступные персонажи государства по
  // алфавиту; «Нового персонажа» у игрока нет.
  const Id hero = freeHero(h, own);
  CHECK(hero != 0);
  std::vector<const Character*> pool;
  h->world().characters.each([&](const Character& c) {
    if (c.faction == own && rules::heroAvailable(h->world(), c.id)) pool.push_back(&c);
  });
  std::sort(pool.begin(), pool.end(), [](const Character* x, const Character* y) { return compareRu(x->name, y->name) < 0; });
  size_t at = 0;
  while (at < pool.size() && pool[at]->id != hero) at++;
  CHECK(at < pool.size());
  const u32 chars0 = h->world().characters.size();
  CHECK(clickIn(h, "council.who." + std::to_string(n0)));
  h.key(Key::PageUp);
  for (size_t k = 0; k <= at; k++) h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(own)->council.back().character, hero);
  // Последний пункт списка — персонаж, а не «Новый персонаж»: персонажи не создаются.
  CHECK(clickIn(h, "council.who." + std::to_string(n0)));
  h.key(Key::PageDown);
  h.key(Key::PageDown);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().characters.size(), chars0);
  // Содержание советника — показатель мира: ввод не меняет его.
  const Id who0 = h->world().faction(own)->council[0].character;
  CHECK(who0 != 0);
  const double up0 = h->world().character(who0)->upkeep;
  CHECK(typeNumber(h, "council.upkeep.0", "99"));
  CHECK_NEAR(h->world().character(who0)->upkeep, up0, 1e-9);
  // «Своя должность…» — правка мира: последний пункт списка должностей — из справочника, запроса названия нет.
  CHECK(clickIn(h, "council.pos.0"));
  h.key(Key::PageDown);
  h.key(Key::PageDown);
  h.key(Key::Enter);
  h.step();
  CHECK(!h->hasDialog("prompt"));
  CHECK(!hasToast(h, "администратор"));
  shotClean(h, "role_panels_council");
}

// Войска своего государства: найм отряда (окно «Сформировать»), гарнизон провинции из резерва, флот в торговле;
// «Правки резерва» нет, название строки не правится.
TEST(app_role_panels_military) {
  Harness h("role_military", 1440, 1000);
  h.demo();
  const Id own = findFaction(h->world(), kOwn);
  CHECK(own != 0);
  asPlayer(h, own);
  // Найм: первая строка (средняя пехота — из населения, без ключевого ресурса).
  const Id row = h->world().faction(own)->army[0].id;
  CHECK(rules::recruitCost(h->world(), own, row, 10).problems.empty());
  panelTab(h, own, "faction.army");
  CHECK(h->uiRect("mil.army.editReserve") == nullptr);
  CHECK(h.clickUi("mil.row.0"));
  h.settle();
  CHECK(h->uiRect("mil.detail.name") == nullptr);   // наименование — правка мира
  CHECK(h->uiRect("mil.detail.total") == nullptr);
  CHECK(reveal(h, "mil.detail.recruit"));
  CHECK(h.clickUi("mil.detail.recruit"));
  h.settle();
  CHECK(enterNumber(h, "mil.recruit.count", 10));
  CHECK(h.clickUi("mil.recruit.ok"));
  h.settle();
  {
    const Faction* f = h->world().faction(own);
    CHECK_EQ(f->forming.size(), size_t(1));
    if (!f->forming.empty()) {
      CHECK_EQ(f->forming[0].row, row);
      CHECK_EQ(f->forming[0].count, 10);
    }
  }
  h.key(Key::Escape);
  h.settle();

  // Гарнизон: провинция своего государства без гарнизона — отряд из резерва.
  const Id pid = calmProvince(h, own);
  CHECK(pid != 0);
  i64 reserve = 0;
  for (const app::mil::UnitRow& r : app::mil::unitRows(h->world(), own, false)) reserve += app::mil::reserveOf(h->world(), own, r.id, false);
  CHECK(reserve > 0);
  provinceTab(h, pid, "province.garrison");
  CHECK(reveal(h, "garrison.add"));
  if (const RectF* empty = h->uiRect("garrison.add")) {   // кнопка внизу пустого состояния
    const RectF r = *empty;
    h.click(r.cx(), r.bottom() - 31);
    h.settle();
  }
  CHECK(h.clickUi("garrison.add.ok"));
  h.settle();
  CHECK(!h->world().province(pid)->garrison.empty());
  CHECK(h->uiRect("garrison.loyalty") != nullptr);
  // Верность гарнизона — показатель мира: ввод не меняет её.
  const double loyal0 = h->world().province(pid)->garrisonLoyalty;
  CHECK(enterNumber(h, "garrison.loyalty", -50));
  CHECK_NEAR(h->world().province(pid)->garrisonLoyalty, loyal0, 1e-9);

  // Флот в торговле: фрегаты из резерва (флот государства не на карте).
  const Faction* f = h->world().faction(own);
  Id frig = 0;
  for (const FleetRow& r : f->fleet)
    if (r.type == ShipType::Frigate) frig = r.id;
  CHECK(frig != 0);
  CHECK(app::mil::reserveOf(h->world(), own, frig, true) >= 2);
  panelTab(h, own, "faction.fleet");
  CHECK(reveal(h, "mil.trade." + std::to_string(frig)));
  CHECK(enterNumber(h, "mil.trade." + std::to_string(frig), 2));
  i64 inTrade = 0;
  for (const GarrisonEntry& g : h->world().faction(own)->tradeFleet)
    if (g.row == frig) inTrade += g.count;
  CHECK_EQ(inTrade, 2);
  shotClean(h, "role_panels_fleet");
}

// Исследование, сделка торговли и объявление войны — игровые действия своего государства.
TEST(app_role_panels_research_trade_war) {
  Harness h("role_play", 1440, 1000);
  h.demo();
  const Id own = findFaction(h->world(), kOwn);
  const Id foreign = findFaction(h->world(), kForeign);
  CHECK(own && foreign);
  asPlayer(h, own);
  // Технология своего дерева, доступная к исследованию.
  Id tech = 0;
  h->world().techs.each([&](const Tech& t) {
    if (!tech && t.faction == own && !rules::techStudied(h->world(), t.id) && !rules::techState(h->world(), t.id).research &&
        rules::canResearch(h->world(), t.id).ok)
      tech = t.id;
  });
  CHECK(tech != 0);
  openTab(h, own, "faction.tech");
  CHECK(clickIn(h, "faction.tech.start." + std::to_string(tech)));
  h.step();
  CHECK(rules::techState(h->world(), tech).research);
  // Отметка изученности общего дерева — правка мира: кнопок нет.
  bool anyStudyMark = false;
  h->world().techs.each([&](const Tech& t) {
    if (t.faction == 0 && h->uiRect("faction.ctech.study." + std::to_string(t.id))) anyStudyMark = true;
  });
  CHECK(!anyStudyMark);

  // Сделка: сторона А — своё государство (закреплена), сторона Б — другая; золото разово.
  app::trade::startDraft(0, 0);
  h->openEditor("trade");
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("trade"));
  CHECK_EQ(app::trade::draft().a, own);
  CHECK(h.clickUi("trade.party.a"));   // своя сторона не меняется
  h.type(h->world().faction(foreign)->name);
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(app::trade::draft().a, own);
  CHECK(h.clickUi("trade.party.b"));
  h.type(h->world().faction(foreign)->name);
  h.key(Key::Enter);
  h.settle();
  CHECK_EQ(app::trade::draft().b, foreign);
  CHECK(h.clickUi("trade.add.a"));
  h.settle();
  CHECK_EQ(app::trade::draft().items.size(), size_t(1));
  const double gold0 = h->world().faction(own)->treasury();
  const Id before = test::newestDeal(h->world());
  CHECK(h.clickUi("trade.conclude"));
  h.settle();
  const Id did = test::newestDeal(h->world());
  CHECK(did > before);
  if (const Deal* d = h->world().deal(did)) CHECK(d->a == own && d->b == foreign);
  CHECK_NEAR(h->world().faction(own)->treasury(), gold0 - 100, 1e-6);
  h->closeEditor();
  h.settle();

  // Объявление войны государству со статус-кво.
  Id target = 0;
  for (const rules::RelationRow& r : rules::relationsOf(h->world(), own))
    if (!target && r.status == RelStatus::Neutral && h->world().faction(r.other)->isState()) target = r.other;
  CHECK(target != 0);
  openTab(h, own, "faction.diplomacy");
  CHECK(clickIn(h, "dip.status." + std::to_string(target)));
  CHECK(h.clickUi("status.item." + std::to_string(int(RelStatus::War))));
  h.settle();
  CHECK(h.clickUi("dip.war.ok"));   // подтверждение войны (аудит 2026-10-23, 5.16)
  h.settle();
  CHECK(h->world().relation(own, target).s == RelStatus::War);
  for (int i = 0; i < 4 && h->hasDialog(); i++) {   // окна вассалитета (если есть) — закрыть без ответа
    h.key(Key::Escape);
    h.settle();
  }
  // Значение отношений — правка мира: поле недоступно.
  const double v0 = h->world().relation(own, target).v;
  CHECK(typeNumber(h, "dip.value." + std::to_string(target), "50"));
  CHECK_NEAR(h->world().relation(own, target).v, v0, 1e-9);
}

// Поля правки мира своего государства у игрока недоступны: название, вид государства, флаг и меню, значения
// отношений; казну и ресурсы не правит и прямое действие (App::act отказывает).
TEST(app_role_panels_world_edit_locked) {
  Harness h("role_locked");
  h.demo();
  const Id own = findFaction(h->world(), kOwn);
  CHECK(own != 0);
  asPlayer(h, own);
  openTab(h, own, "faction.overview");
  const std::string name0 = h->world().faction(own)->name;
  const StateKind kind0 = h->world().faction(own)->stateKind;
  CHECK(h->uiRect("faction.more") == nullptr);
  CHECK(h->uiRect("overview.flag") == nullptr);
  typeInto(h, "overview.name", "Захваченное имя");
  CHECK_EQ(h->world().faction(own)->name, name0);
  CHECK(clickIn(h, "overview.kind"));
  h.key(Key::Down);
  h.key(Key::Enter);
  h.settle();
  CHECK(h->world().faction(own)->stateKind == kind0);
  shotClean(h, "role_panels_overview");
  // Казна и ресурсы — только администратору.
  const double gold0 = h->world().faction(own)->treasury();
  h.dropToasts();
  CHECK(!h->act("Казна", [&](Tx& tx) { tx.faction(own).res[kGold] += 1000; }));
  CHECK(hasToast(h, "администратор"));
  Id res = 0;
  for (auto& [r, v] : h->world().faction(own)->res)
    if (r != kGold && !res) res = r;
  CHECK(res != 0);
  const double stock0 = h->world().faction(own)->stock(res);
  CHECK(!h->act("Ресурс", [&](Tx& tx) { tx.faction(own).res[res] += 50; }));
  CHECK_NEAR(h->world().faction(own)->treasury(), gold0, 1e-9);
  CHECK_NEAR(h->world().faction(own)->stock(res), stock0, 1e-9);
  // Значение отношений −100…100.
  openTab(h, own, "faction.diplomacy");
  Id other = 0;
  for (const rules::RelationRow& r : rules::relationsOf(h->world(), own))
    if (!other && h->world().faction(r.other)->isState()) other = r.other;
  CHECK(other != 0);
  const double v0 = h->world().relation(own, other).v;
  CHECK(typeNumber(h, "dip.value." + std::to_string(other), "-90"));
  CHECK_NEAR(h->world().relation(own, other).v, v0, 1e-9);
  // Каталог государств — без «Нового государства».
  h->openDirectory("states");
  h.settle();
  CHECK(h->uiRect("states.add") == nullptr);
  CHECK(h->uiRect("states.search") != nullptr);
}

// Чужое государство — только просмотр: ни мест в совете, ни найма, ни смены отношений, ни исследований и сделок; совет
// и состав войск чужого государства скрыты (туман войны, аудит 2026-10-23, 1.5).
TEST(app_role_panels_foreign_readonly) {
  Harness h("role_foreign", 1440, 1000);
  h.demo();
  const Id own = findFaction(h->world(), kOwn);
  const Id foreign = findFaction(h->world(), kForeign);
  CHECK(own && foreign);
  asPlayer(h, own);
  CHECK(!h->knows(foreign));
  const World w0 = h->world();
  openTab(h, foreign, "faction.council");
  CHECK(h->uiRect("faction.more") == nullptr);
  CHECK(h->uiRect("council.add") == nullptr);
  CHECK(h->uiRect("council.who.0") == nullptr);
  CHECK(h->uiRect("fog.hidden") != nullptr);
  shotClean(h, "role_panels_foreign_council");
  // Войска: таблицы, найм и «Правка резерва» скрыты — только объекты на карте.
  panelTab(h, foreign, "faction.army");
  CHECK(h->uiRect("mil.army.editReserve") == nullptr);
  CHECK(h->uiRect("mil.row.0") == nullptr);
  CHECK(h->uiRect("mil.army.hidden") != nullptr);
  CHECK(h->uiRect("mil.detail.recruit") == nullptr);
  CHECK(h->uiRect("mil.detail.disband") == nullptr);
  CHECK(h->uiRect("mil.detail.delete") == nullptr);
  shotClean(h, "role_panels_foreign_army");
  // Отношения: меню состояний не открывается.
  openTab(h, foreign, "faction.diplomacy");
  CHECK(clickIn(h, "dip.status." + std::to_string(own)));
  CHECK(h->uiRect("status.item." + std::to_string(int(RelStatus::War))) == nullptr);
  // Сделки и дань — только своего государства.
  openTab(h, foreign, "faction.trade");
  CHECK(h->uiRect("faction.trade.new") == nullptr);
  CHECK(h->uiRect("faction.trade.tribute") == nullptr);
  // Мир не изменился.
  CHECK(World::diff(w0, h->world()) == 0);
}

// Окно битвы: участник применяет итог (потери и победитель — по правилам окна); чужая битва — только просмотр.
TEST(app_role_panels_battle_side) {
  Harness h("role_battle", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id ket = factionByName(h->world(), "Валь-Кетра");
  const Id own = findFaction(h->world(), kOwn);
  CHECK(hel && ket && own);
  CHECK(h->world().relation(hel, ket).s == RelStatus::War);
  const Id att = armyOf(h->world(), hel, ArmyKind::Army), def = armyOf(h->world(), ket, ArmyKind::Army);
  CHECK(att && def);
  // Не участник (Альмарин): «Применить итог» недоступно.
  asPlayer(h, own);
  const World w0 = h->world();
  app::mil::openBattle(h.a(), att, def, h->world().army(att)->pos);
  h.settle();
  CHECK(h->hasDialog("battle"));
  CHECK(h.clickUi("battle.apply"));
  h.settle();
  CHECK(h->hasDialog("battle"));
  CHECK(World::diff(w0, h->world()) == 0);
  h.key(Key::Escape);   // закрытие — отступление без изменений
  h.settle();
  CHECK(!h->hasDialog("battle"));
  // Участник (Хельдвиг): потери противника вводятся, итог применяется.
  asPlayer(h, hel);
  app::mil::openBattle(h.a(), att, def, h->world().army(att)->pos);
  h.settle();
  CHECK(h->hasDialog("battle"));
  const Army* d = h->world().army(def);
  CHECK(d && !d->groups.empty() && !d->groups[0].units.empty());
  const Id drow = d->groups[0].units[0].row;
  const i64 dcount = d->groups[0].units[0].count;
  CHECK(enterNumber(h, "battle.loss." + std::to_string(def) + "." + std::to_string(drow), 100));
  shotClean(h, "role_panels_battle");
  CHECK(h.clickUi("battle.apply"));
  h.settle();
  for (int i = 0; i < 4 && h->hasDialog(); i++) {   // окна итога (судьба героев) — закрыть
    h.key(Key::Escape);
    h.settle();
  }
  const Army* d2 = h->world().army(def);
  i64 left = 0;
  if (d2)
    for (const ArmyGroup& g : d2->groups)
      for (const ArmyUnit& u : g.units)
        if (u.row == drow) left += u.count;
  CHECK_EQ(left, dcount - 100);
}

// Администратор — всё как раньше: название, вид государства, значение отношений, меню действий и «Правка резерва».
TEST(app_role_panels_admin_unchanged) {
  Harness h("role_admin", 1440, 1000);
  h.demo();
  const Id own = findFaction(h->world(), kOwn);
  const Id foreign = findFaction(h->world(), kForeign);
  CHECK(own && foreign);
  CHECK(h->admin());
  openTab(h, foreign, "faction.overview");
  CHECK(h->uiRect("faction.more") != nullptr);
  CHECK(h->uiRect("overview.flag") != nullptr);
  CHECK(clickIn(h, "overview.name"));
  h.retype("Республика Корвен Новая");
  h.key(Key::Tab);
  h.settle();
  CHECK_EQ(h->world().faction(foreign)->name, std::string("Республика Корвен Новая"));
  openTab(h, foreign, "faction.diplomacy");
  CHECK(typeNumber(h, "dip.value." + std::to_string(own), "-60"));
  CHECK_NEAR(rules::relationValue(h->world(), foreign, own), -60, 1e-9);
  openTab(h, foreign, "faction.council");
  CHECK(h->uiRect("council.add") != nullptr);
  const Id who0 = h->world().faction(foreign)->council[0].character;
  CHECK(typeNumber(h, "council.upkeep.0", "33"));
  CHECK_NEAR(h->world().character(who0)->upkeep, 33, 1e-9);
  panelTab(h, foreign, "faction.army");
  CHECK(h->uiRect("mil.army.editReserve") != nullptr);
  CHECK(h.clickUi("mil.row.0"));
  h.settle();
  CHECK(reveal(h, "mil.detail.name"));
  CHECK(h->uiRect("mil.detail.upkeep") != nullptr);
  h->openDirectory("states");
  h.settle();
  CHECK(h->uiRect("states.add") != nullptr);
}
