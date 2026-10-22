// Сценарии заговоров на демонстрационном мире (ТЗ «Доработки №2», п.1, 3–5, 7, 8): вкладка государства «Заговоры» —
// видна игроку только у своего государства; создание заговора игроком (только «Против другого государства») и
// администратором (внутренний), провинции и герои по правилам игрока (довольство ниже 0, «Недовольство правителем» —
// приглашение) и администратора, «Спонсировать заговор», «Снабдить вооружением», слот лидера, «Угрозы заговору» и окно
// «Заказать убийство» (ползунок золота и вероятность), «Осуществить заговор» с итогом и «Показать на карте»;
// контрразведка цели — обнаружение, сведения под «?» по этапам, раскрытие, аресты, тюрьмы. Снимки вкладки (широкое и
// узкое окно) и окон.
#include "app/panels/plots_common.h"
#include "core/content.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;
namespace plotui = rg::app::plotui;

namespace {

// Заказчик «Королевство Альмарин» и цель «Империя Валь-Кетра»: у заказчика золото, порох и сталь; провинции цели —
// довольство −30, −10 и +20; новые герои цели — с «Недовольством правителем» (Тайрен, Гаррет, значимая Сейра) и без
// него (Олбрик).
struct Scene {
  Id owner = 0, target = 0;
  std::vector<Id> provs;
  Id angry = 0, calm = 0, vip = 0, spare = 0;
  Id powder = 0, steel = 0;
};

Scene setup(Harness& h) {
  Scene s;
  const World& w0 = h->world();
  s.owner = findFaction(w0, "Королевство Альмарин");
  s.target = findFaction(w0, "Империя Валь-Кетра");
  CHECK(s.owner && s.target);
  s.powder = test::idByName(w0.catalogs->resources, "Порох");
  s.steel = test::idByName(w0.catalogs->resources, "Сталь");
  CHECK(s.powder && s.steel);
  w0.provinces.each([&](const Province& p) {
    if (p.owner == s.target && !p.sea) s.provs.push_back(p.id);
  });
  CHECK(s.provs.size() >= 3);
  CHECK(h->act("test", [&](Tx& tx) {
    tx.faction(s.owner).res[kGold] = 100000;
    tx.faction(s.owner).res[s.powder] = 500;
    tx.faction(s.owner).res[s.steel] = 1000;
    const double content[3] = {-30, -10, 20};
    for (size_t i = 0; i < s.provs.size(); i++) tx.province(s.provs[i]).contentment = i < 3 ? content[i] : 10;
    const Id discontent = rules::ensureBuiltinMod(tx, schema::mod::Discontent);
    s.angry = rules::createCharacter(tx, s.target, "Тайрен Ворон");
    s.calm = rules::createCharacter(tx, s.target, "Олбрик Верный");
    s.spare = rules::createCharacter(tx, s.target, "Гаррет Тень");
    s.vip = rules::createCharacter(tx, s.target, "Сейра Звёздная");
    tx.character(s.vip).hero = true;
    for (Id c : {s.angry, s.spare, s.vip}) rules::addModifier(tx, rules::ModTarget::Character, c, discontent, 0);
  }));
  h.settle();
  return s;
}

// «Штаб Контрразведки» уровня level у государства state.
void counterHq(Harness& h, Id state, int level) {
  CHECK(h->act("test", [&](Tx& tx) {
    content::addIntelBuildings(tx, state);
    const Id b = test::buildingByKey(tx.w(), schema::bld::CounterIntelHq, state);
    CHECK(b != 0);
    tx.building(b).requires_.clear();
    const Id pid = tx.w().provinces.firstId([&](const Province& p) { return p.owner == state && !p.sea; });
    rules::placeBuilding(tx, pid, b, level);
  }));
}

void asPlayer(Harness& h, Id state) {
  h->ui.role = app::Role::Player;
  h->ui.playerState = state;
  h.settle();
}

void asAdmin(Harness& h) {
  h->ui.role = app::Role::Admin;
  h->ui.playerState = 0;
  h.settle();
}

// Правка мира от имени администратора посреди сценария игрока.
bool adminAct(Harness& h, const std::function<void(Tx&)>& fn) {
  const app::Role role = h->ui.role;
  const Id ps = h->ui.playerState;
  h->ui.role = app::Role::Admin;
  const bool ok = h->act("test", fn);
  h->ui.role = role;
  h->ui.playerState = ps;
  h.settle();
  return ok;
}

// Указатель — на пустое место шапки страницы (снимки без подсказок).
void parkMouse(Harness& h) {
  const platform::Frame& f = hl::lastFrame();
  h.move(float(f.w) / f.scale * 0.85f, 110);
}

const app::TabDef* plotsTab() {
  for (const app::TabDef& t : app::tabs())
    if (std::string_view(t.id) == plotui::kTab) return &t;
  return nullptr;
}

std::vector<Id> plotsOf(const World& w, Id owner) {
  std::vector<Id> out;
  w.plots.each([&](const Plot& p) {
    if (p.owner == owner) out.push_back(p.id);
  });
  return out;
}

// Выбор в выпадающем списке с поиском: открыть, набрать, Enter; пункт недоступен (picks = false) — закрыть Esc.
bool comboType(Harness& h, const std::string& name, const std::string& text, bool picks = true) {
  if (!clickIn(h, name)) return false;
  h.type(text);
  h.key(Key::Enter);
  if (!picks) h.key(Key::Escape);
  h.settle();
  return true;
}

// Щелчок по ползунку в доле k его дорожки; valueText — подпись максимума справа (пусто — без значения).
void clickSlider(Harness& h, const std::string& name, float k, const std::string& valueText = {}) {
  const RectF* r = h->uiRect(name);
  CHECK_MSG(r != nullptr, name);
  if (!r) return;
  const RectF s = *r;
  const float val = valueText.empty() ? 0.f : std::max(44.f, ui::measure(valueText, ui::Font::Body) + 6) + 8;
  h.click(s.x + 8 + (s.w - 16 - val) * k, s.cy());
}

// Заговор Альмарина против Валь-Кетры (правилами, для сценариев контрразведки и снимков): провинции 0 и 1 с
// приверженностью и снабжением, участники (обычный и значимый, лидер — обычный), приглашённый, угроза с заказом.
Id richPlot(Harness& h, const Scene& s) {
  Id plot = 0;
  CHECK(adminAct(h, [&](Tx& tx) {
    plot = rules::createPlot(tx, PlotKind::External, s.owner, s.target, "Тень над Кетрой");
    rules::addPlotProvince(tx, plot, s.provs[0], false);
    rules::addPlotProvince(tx, plot, s.provs[1], false);
    rules::setPlotCommitment(tx, plot, s.provs[0], 60);
    rules::setPlotCommitment(tx, plot, s.provs[1], 25);
    rules::supplyPlot(tx, plot, s.provs[0], {{s.powder, 300}, {s.steel, 400}});
    rules::addPlotHero(tx, plot, s.angry, false);
    rules::addPlotHero(tx, plot, s.vip, false);
    rules::setPlotLeader(tx, plot, s.angry);
    tx.plot(plot).invited.push_back(s.calm);
    rules::addPlotHero(tx, plot, s.spare, true);
    rules::answerPlotInvite(tx, plot, s.spare, false);
    rules::orderHit(tx, plot, s.spare, 2500);
  }));
  return plot;
}

}  // namespace

// ---------------------------------------------------------------- игрок: создание, состав, спонсирование, снабжение
TEST(app_plots_player_create_and_members) {
  Harness h("plots_player", 1600, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Scene s = setup(h);
  asPlayer(h, s.owner);
  // Вкладка — только у своего государства.
  const app::TabDef* tab = plotsTab();
  CHECK(tab != nullptr);
  CHECK(tab->visible(h.a(), s.owner));
  CHECK(!tab->visible(h.a(), s.target));
  asAdmin(h);
  CHECK(tab->visible(h.a(), s.target));
  // Внутренний заговор против своего государства игрок не видит.
  Id internal = 0;
  CHECK(h->act("test", [&](Tx& tx) { internal = rules::createPlot(tx, PlotKind::Internal, 0, s.owner); }));
  asPlayer(h, s.owner);
  openTab(h, s.owner, plotui::kTab);
  CHECK(h->uiRect("plots.list") && h->uiRect("ci.zone"));
  CHECK(h->uiRect("plots.row." + std::to_string(internal)) == nullptr);
  CHECK(h->uiRect("plots.empty") != nullptr);
  CHECK(h->uiRect("ci.locked") != nullptr);   // без «Штаба Контрразведки» — одна строка

  // «Создать заговор»: игроку — только «Против другого государства».
  CHECK(h.clickUi("plots.create"));
  h.settle();
  CHECK(h->hasDialog("plot.create"));
  CHECK(h.clickUi("plot.create.target"));
  h.type("Валь-Кетра");
  h.key(Key::Enter);
  h.settle();
  CHECK(h.shot("plots_create"));
  CHECK(h.clickUi("plot.create.ok"));
  h.settle();
  CHECK(!h->hasDialog("plot.create"));
  const std::vector<Id> own = plotsOf(h->world(), s.owner);
  CHECK_EQ(own.size(), size_t(1));
  const Id plot = own.empty() ? 0 : own[0];
  const Plot* p = h->world().plot(plot);
  CHECK(p && p->external() && p->target == s.target);
  CHECK(h->uiRect("plots.row." + std::to_string(plot)) != nullptr);
  CHECK(h->uiRect("plot.title") != nullptr);

  // Провинции: игроку — только с довольством ниже 0.
  CHECK(comboType(h, "plot.addProvince", h->world().provinceName(s.provs[2]), false));
  CHECK(h->world().plot(plot)->provinces.empty());
  CHECK(comboType(h, "plot.addProvince", h->world().provinceName(s.provs[0])));
  CHECK(h->world().plot(plot)->province(s.provs[0]) != nullptr);
  CHECK(h->uiRect("plot.prov." + std::to_string(s.provs[0]) + ".bar") != nullptr);       // игроку — шкала
  CHECK(h->uiRect("plot.prov." + std::to_string(s.provs[0]) + ".commit") == nullptr);    // ползунок — администратору

  // «Спонсировать заговор»: цена — 20 % текущей ценности, +10 %, раз в ход.
  const double cost = rules::sponsorCost(h->world(), s.provs[0]);
  CHECK(cost > 0);
  const double gold0 = h->world().faction(s.owner)->treasury();
  CHECK(clickIn(h, "plot.prov." + std::to_string(s.provs[0]) + ".sponsor"));
  h.settle();
  CHECK_NEAR(h->world().plot(plot)->province(s.provs[0])->commitment, 10, 1e-9);
  CHECK_NEAR(h->world().faction(s.owner)->treasury(), gold0 - cost, 1e-6);
  CHECK(clickIn(h, "plot.prov." + std::to_string(s.provs[0]) + ".sponsor"));   // недоступна до следующего хода
  h.settle();
  CHECK_NEAR(h->world().plot(plot)->province(s.provs[0])->commitment, 10, 1e-9);

  // «Снабдить вооружением»: порох со склада заказчика.
  CHECK(clickIn(h, "plot.prov." + std::to_string(s.provs[0]) + ".supply"));
  h.settle();
  CHECK(h->hasDialog("plot.supply"));
  enterInDialog(h, "plot.supply.res." + std::to_string(s.powder), "120");
  CHECK(h.shot("plots_supply"));
  CHECK(h.clickUi("plot.supply.ok"));
  h.settle();
  CHECK(!h->hasDialog("plot.supply"));
  CHECK_NEAR(h->world().plot(plot)->province(s.provs[0])->supplies.at(s.powder), 120, 1e-9);
  CHECK_NEAR(h->world().faction(s.owner)->stock(s.powder), 380, 1e-9);

  // Герои: игрок приглашает только недовольных правителем; приглашённый ждёт решения администратора.
  CHECK(comboType(h, "plot.addHero", "Олбрик", false));
  CHECK(h->world().plot(plot)->invited.empty());
  CHECK(comboType(h, "plot.addHero", "Тайрен"));
  CHECK(test::has(h->world().plot(plot)->invited, s.angry));
  CHECK(h->uiRect("plot.invite." + std::to_string(s.angry) + ".name") != nullptr);
  CHECK(h->uiRect("plot.invite." + std::to_string(s.angry) + ".yes") == nullptr);   // решение — администратору
  CHECK(comboType(h, "plot.addHero", "Гаррет"));
  CHECK(test::has(h->world().plot(plot)->invited, s.spare));
  // Администратор: Тайрен согласился, Гаррет отказался — «Угрозы заговору».
  asAdmin(h);
  CHECK(clickIn(h, "plot.invite." + std::to_string(s.angry) + ".yes"));
  h.settle();
  CHECK(clickIn(h, "plot.invite." + std::to_string(s.spare) + ".no"));
  h.settle();
  CHECK(test::has(h->world().plot(plot)->heroes, s.angry));
  CHECK(test::has(h->world().plot(plot)->threats, s.spare));
  asPlayer(h, s.owner);
  CHECK(h->uiRect("plot.threats") != nullptr);

  // Слот лидера: выбор из участников.
  CHECK(!rules::executeProblem(h->world(), plot).empty());
  CHECK(clickIn(h, "plot.leader"));
  h.settle();
  CHECK(h.clickUi("plot.leader.pick." + std::to_string(s.angry)));
  h.settle();
  CHECK_EQ(h->world().plot(plot)->leader, s.angry);
  CHECK(rules::executeProblem(h->world(), plot).empty());

  // Название заговора.
  CHECK(clickIn(h, "plot.rename"));
  h.settle();
  CHECK(answerPrompt(h, "Тихий рассвет"));
  h.settle();
  CHECK_EQ(h->world().plot(plot)->name, std::string("Тихий рассвет"));
  CHECK(h.shot("plots_player"));
}

// ---------------------------------------------------------------- заказное убийство и осуществление
TEST(app_plots_hit_and_execute) {
  Harness h("plots_execute", 1600, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Scene s = setup(h);
  const Id plot = richPlot(h, s);
  asPlayer(h, s.owner);
  openTab(h, s.owner, plotui::kTab);
  CHECK(h->uiRect("plot.threat." + std::to_string(s.spare) + ".status") != nullptr);
  // Отмена заказа — золото вернулось.
  const double gold0 = h->world().faction(s.owner)->treasury();
  CHECK(clickIn(h, "plot.threat." + std::to_string(s.spare) + ".cancel"));
  h.settle();
  CHECK(h->world().plot(plot)->hits.empty());
  CHECK_NEAR(h->world().faction(s.owner)->treasury(), gold0 + 2500, 1e-6);
  // Окно заказа: ползунок золота 100…5000 (1 % за 100) и вероятность по слагаемым.
  CHECK(clickIn(h, "plot.threat." + std::to_string(s.spare) + ".hit"));
  h.settle();
  CHECK(h->hasDialog("plot.hit"));
  CHECK(h->uiRect("plot.hit.gold") && h->uiRect("plot.hit.chance"));
  CHECK(h->uiRect("plot.hit.vip") == nullptr);
  clickSlider(h, "plot.hit.gold", 0.5f, fmtNum(5000) + " тыс.");
  h.settle();
  CHECK(h.shot("plots_hit"));
  const double gold1 = h->world().faction(s.owner)->treasury();
  CHECK(h.clickUi("plot.hit.ok"));
  h.settle();
  CHECK(!h->hasDialog("plot.hit"));
  const PlotHit* hit = h->world().plot(plot)->hit(s.spare);
  CHECK(hit != nullptr);
  if (hit) {
    CHECK(hit->gold > 1000 && hit->gold < 4000);
    CHECK_NEAR(std::fmod(hit->gold, 100), 0, 1e-6);
    CHECK_NEAR(h->world().faction(s.owner)->treasury(), gold1 - hit->gold, 1e-6);
    const rules::HitChance hc = rules::hitChance(h->world(), plot, s.spare, hit->gold);
    CHECK_NEAR(hc.base, hit->gold / 100, 1e-9);
  }
  // Значимый герой — от 500 до 25 000, 1 % за 500.
  CHECK(adminAct(h, [&](Tx& tx) {
    rules::removePlotHero(tx, plot, s.vip);
    tx.plot(plot).threats.push_back(s.vip);
  }));
  CHECK(clickIn(h, "plot.threat." + std::to_string(s.vip) + ".hit"));
  h.settle();
  CHECK(h->hasDialog("plot.hit"));
  CHECK(h->uiRect("plot.hit.vip") != nullptr);
  CHECK(h.clickUi("plot.hit.ok"));
  h.settle();
  CHECK(h->world().plot(plot)->hit(s.vip) && std::fabs(h->world().plot(plot)->hit(s.vip)->gold - 500) < 1e-9);
  // Исход — только после хода: в этом ходу оба живы, заказы висят.
  CHECK(!rules::characterHas(h->world(), s.spare, schema::mod::Dead));

  // «Осуществить заговор»: подтверждение с провинциями и героями, затем итог.
  CHECK(clickIn(h, "plot.execute"));
  h.settle();
  CHECK(h->hasDialog("plot.execute"));
  CHECK(h->uiRect("plot.execute.prov." + std::to_string(s.provs[0])) != nullptr);
  CHECK(h.shot("plots_execute_confirm"));
  CHECK(h.clickUi("plot.execute.ok"));
  h.settle();
  CHECK(h->world().plot(plot) == nullptr);
  const Id cons = h->world().character(s.angry)->faction;
  const Faction* S = h->world().faction(cons);
  CHECK(S && S->conspiracy && S->name == std::string("Заговор Тайрен Ворон"));
  CHECK(h->hasDialog("plot.execute") && h->uiRect("plot.execute.newState"));
  CHECK(h.shot("plots_execute_result"));
  CHECK(h.clickUi("plot.execute.map"));
  h.settle();
  CHECK(!h->hasDialog("plot.execute"));
  CHECK(h->ui.sel.type == app::SelType::Army);
  CHECK(h->view() == app::View::Map);
  const Army* ar = h->world().army(h->ui.sel.id);
  CHECK(ar && !ar->groups.empty() && ar->groups[0].faction == cons);
}

// ---------------------------------------------------------------- администратор: внутренний заговор
TEST(app_plots_admin_internal) {
  Harness h("plots_admin", 1600, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Scene s = setup(h);
  openTab(h, s.target, plotui::kTab);
  CHECK(h.clickUi("plots.create"));
  h.settle();
  CHECK(h->hasDialog("plot.create"));
  CHECK(clickPart(h, "plot.create.kind", 1, 2));   // «Внутренний заговор»
  h.settle();
  CHECK(h.clickUi("plot.create.ok"));
  h.settle();
  Id plot = 0;
  h->world().plots.each([&](const Plot& p) {
    if (!p.external() && p.target == s.target) plot = p.id;
  });
  CHECK(plot != 0);
  CHECK_EQ(h->world().plot(plot)->owner, Id(0));
  CHECK(h->uiRect("plots.row." + std::to_string(plot)) != nullptr);
  // Администратору правило довольства не действует; приверженность — ползунком.
  CHECK(comboType(h, "plot.addProvince", h->world().provinceName(s.provs[2])));
  CHECK(h->world().plot(plot)->province(s.provs[2]) != nullptr);
  CHECK(ensureVisible(h, "plot.prov." + std::to_string(s.provs[2]) + ".commit"));
  clickSlider(h, "plot.prov." + std::to_string(s.provs[2]) + ".commit", 0.75f);
  h.settle();
  const double c = h->world().plot(plot)->province(s.provs[2])->commitment;
  CHECK(c > 50 && c < 100);
  // Герой — сразу участник (без «Недовольства правителем»).
  CHECK(comboType(h, "plot.addHero", "Олбрик"));
  CHECK(test::has(h->world().plot(plot)->heroes, s.calm));
  // Лидер — перетаскиванием участника на слот.
  CHECK(ensureVisible(h, "plot.hero." + std::to_string(s.calm) + ".name"));
  const RectF from = *h->uiRect("plot.hero." + std::to_string(s.calm) + ".name");
  const RectF to = *h->uiRect("plot.leader");
  h.drag(from.cx(), from.cy(), to.cx(), to.cy());
  h.settle();
  CHECK_EQ(h->world().plot(plot)->leader, s.calm);
  // Удаление — с подтверждением.
  CHECK(clickIn(h, "plot.delete"));
  h.settle();
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->world().plot(plot) == nullptr);
}

// ---------------------------------------------------------------- контрразведка цели
TEST(app_plots_counterintel) {
  Harness h("plots_ci", 1600, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Scene s = setup(h);
  const Id plot = richPlot(h, s);
  counterHq(h, s.target, 3);
  asPlayer(h, s.target);
  openTab(h, s.target, plotui::kTab);
  // Чужой заговор против себя игрок не видит в списке.
  CHECK(h->uiRect("plots.row." + std::to_string(plot)) == nullptr);
  CHECK(h->uiRect("ci.detect") && h->uiRect("ci.chance"));
  // «Обнаружить заговоры»: итог — уведомлением, раз в ход.
  CHECK(clickIn(h, "ci.detect"));
  CHECK(hasToast(h, "Обнаружен заговор") || hasToast(h, "Заговоров не обнаружено"));
  CHECK_EQ(h->world().faction(s.target)->detectTurn, h->world().turn());
  CHECK(!rules::detectProblem(h->world(), s.target, true).empty());
  // Сведения под «?»: этап 0 — ничего.
  CHECK(adminAct(h, [&](Tx& tx) {
    tx.plot(plot).found = true;
    tx.plot(plot).stage = 0;
  }));
  const std::string row = "ci.plot." + std::to_string(plot);
  CHECK(h->uiRect(row) != nullptr);
  CHECK(h->uiRect(row + ".arrest") == nullptr);   // арест — с этапа 1
  CHECK(clickIn(h, row + ".info"));
  h.settle();
  CHECK(h->uiRect("ci.info.unknown.provinces") && h->uiRect("ci.info.unknown.leader"));
  CHECK(h->uiRect("ci.info.prov." + std::to_string(s.provs[0])) == nullptr);
  h.key(Key::Escape);
  h.settle();
  // Этап 2: провинции и приверженность; герои — «?».
  CHECK(adminAct(h, [&](Tx& tx) { tx.plot(plot).stage = 2; }));
  CHECK(clickIn(h, row + ".info"));
  h.settle();
  CHECK(h->uiRect("ci.info.prov." + std::to_string(s.provs[0])) && h->uiRect("ci.info.commit." + std::to_string(s.provs[0])));
  CHECK(h->uiRect("ci.info.unknown.heroes") && h->uiRect("ci.info.unknown.vip"));
  CHECK(h->uiRect("ci.info.hero." + std::to_string(s.angry)) == nullptr);
  h.key(Key::Escape);
  h.settle();
  // Этап 4: герои и значимые; лидер — «?».
  CHECK(adminAct(h, [&](Tx& tx) { tx.plot(plot).stage = 4; }));
  CHECK(clickIn(h, row + ".info"));
  h.settle();
  CHECK(h->uiRect("ci.info.hero." + std::to_string(s.angry)) && h->uiRect("ci.info.hero." + std::to_string(s.vip)));
  CHECK(h->uiRect("ci.info.unknown.leader") && !h->uiRect("ci.info.leader"));
  CHECK(h.shot("plots_ci_info"));
  h.key(Key::Escape);
  h.settle();
  // «Раскрыть заговор»: итог — уведомлением, раз в ход на заговор.
  h.dropToasts();
  CHECK(adminAct(h, [&](Tx& tx) { tx.plot(plot).stage = 1; }));
  CHECK(clickIn(h, row + ".reveal"));
  CHECK(hasToast(h, "Заговор раскрыт") || hasToast(h, "Раскрыть заговор не удалось"));
  CHECK_EQ(h->world().plot(plot)->revealTurn, h->world().turn());
  // Аресты на этапе 1: подтверждение с эффектом этапа; приверженность −20 %, жители — в тюрьму.
  CHECK(adminAct(h, [&](Tx& tx) { tx.plot(plot).stage = 1; }));
  const double c0 = h->world().plot(plot)->province(s.provs[0])->commitment;
  CHECK(clickIn(h, row + ".arrest"));
  h.settle();
  CHECK(h->hasDialog("plot.arrest") && h->uiRect("plot.arrest.lines"));
  CHECK(h.shot("plots_ci_arrest"));
  CHECK(h.clickUi("plot.arrest.ok"));
  CHECK(ui::toastCount() > 0);   // итог — уведомлением над закрывающимся окном (слой интерфейса)
  h.settle();
  CHECK_NEAR(h->world().plot(plot)->province(s.provs[0])->commitment, c0 - schema::kArrestCommitCut, 1e-9);
  CHECK(rules::provinceHas(h->world(), s.provs[0], schema::mod::PlotArrests));
  CHECK(!h->world().faction(s.target)->prison.empty());
  CHECK(!h->world().plot(plot)->found);
  // Этап 3: обычные герои (не лидер) — под арест.
  CHECK(adminAct(h, [&](Tx& tx) {
    rules::answerPlotInvite(tx, plot, s.calm, true);
    tx.plot(plot).found = true;
    tx.plot(plot).stage = 3;
  }));
  CHECK(clickIn(h, row + ".arrest"));
  h.settle();
  CHECK(h.clickUi("plot.arrest.ok"));
  h.settle();
  CHECK(rules::characterHas(h->world(), s.calm, schema::mod::Arrested));
  CHECK(!rules::characterHas(h->world(), s.angry, schema::mod::Arrested));   // лидер
  CHECK(h->uiRect("prison.hero." + std::to_string(s.calm)) != nullptr);
  CHECK(h.shot("plots_ci_prison"));
  // Тюрьмы: освободить героя и часть заключённых.
  CHECK(clickIn(h, "prison.hero." + std::to_string(s.calm)));
  h.settle();
  CHECK(!rules::characterHas(h->world(), s.calm, schema::mod::Arrested));
  const RacePop jailed = h->world().faction(s.target)->prison.at(0);
  CHECK(clickIn(h, "prison.release"));
  h.settle();
  CHECK(h->hasDialog("plot.prison"));
  enterInDialog(h, "prison.race." + std::to_string(jailed.race), "1");
  CHECK(h.clickUi("prison.ok"));
  h.settle();
  const std::vector<RacePop>& pr = h->world().faction(s.target)->prison;
  const auto it = std::find_if(pr.begin(), pr.end(), [&](const RacePop& r) { return r.race == jailed.race; });
  CHECK_EQ(it == pr.end() ? i64(0) : it->pop, jailed.pop - 1);
}

// ---------------------------------------------------------------- снимки
TEST(app_plots_shots_wide) {
  Harness h("plots_wide", 1600, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  LogTap tap("plots-wide");
  const Scene s = setup(h);
  const Id plot = richPlot(h, s);
  counterHq(h, s.owner, 2);
  CHECK(h->act("test", [&](Tx& tx) {
    tx.plot(plot).alarm = true;
    const Id other = rules::createPlot(tx, PlotKind::Internal, 0, s.owner);
    tx.plot(other).found = true;
    tx.plot(other).stage = 2;
    rules::createPlot(tx, PlotKind::External, s.owner, findFaction(tx.w(), "Республика Корвен"));
    tx.faction(s.owner).prison.push_back(RacePop{tx.w().catalogs->races.front().id, 340});
  }));
  asPlayer(h, s.owner);
  openTab(h, s.owner, plotui::kTab);
  const RectF* list = h->uiRect("plots.list");
  const RectF* card = h->uiRect("plot.title");
  CHECK(list && card && card->x > list->right());   // список слева, заговор справа
  CHECK(h->uiRect("plot.alarm") != nullptr);
  parkMouse(h);
  shotClean(h, "plots_wide");
  scrollInspector(h, -10);
  shotClean(h, "plots_wide_bottom");
  // Администратор: ползунки приверженности, решения о приглашённых, защита от убийства.
  asAdmin(h);
  openTab(h, s.owner, plotui::kTab);
  CHECK(h->uiRect("plot.prov." + std::to_string(s.provs[0]) + ".commit") != nullptr);
  parkMouse(h);
  shotClean(h, "plots_wide_admin");
  CHECK(tap.warnings("повторяющийся ID").empty());
}

TEST(app_plots_shots_narrow) {
  Harness h("plots_narrow", 960, 860);
  h.demo();
  h.waitMap();
  h.dropToasts();
  LogTap tap("plots-narrow");
  const Scene s = setup(h);
  richPlot(h, s);
  counterHq(h, s.owner, 1);
  asPlayer(h, s.owner);
  openTab(h, s.owner, plotui::kTab);
  const RectF* list = h->uiRect("plots.list");
  const RectF* card = h->uiRect("plot.title");
  CHECK(list && card && card->y > list->bottom());   // столбцы друг под другом
  parkMouse(h);
  shotClean(h, "plots_narrow");
  scrollInspector(h, -8);
  shotClean(h, "plots_narrow_mid");
  scrollInspector(h, -8);
  shotClean(h, "plots_narrow_bottom");
  // Панель выделения справа от карты (≈ 350 точек): плитки провинций — одна под другой.
  h->setPage(false);
  h.settle();
  h.waitMap();
  scrollInspector(h, 40);
  const RectF* t0 = h->uiRect("plot.prov." + std::to_string(s.provs[0]));
  const RectF* t1 = h->uiRect("plot.prov." + std::to_string(s.provs[1]));
  CHECK(t0 && t1 && t1->y > t0->bottom());
  parkMouse(h);
  shotClean(h, "plots_panel");
  scrollInspector(h, -8);
  shotClean(h, "plots_panel_mid");
  scrollInspector(h, -8);
  shotClean(h, "plots_panel_bottom");
  CHECK(tap.warnings("повторяющийся ID").empty());
}
