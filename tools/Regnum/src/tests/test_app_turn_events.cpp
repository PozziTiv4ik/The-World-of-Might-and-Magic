// Сценарии событий хода (ТЗ «Механика мятежа», п.1.2 и 4): после «Завершить ход» окна идут по очереди — мятеж
// войска с верностью −100 % («Судьба героя» верного героя, затем захват провинции мятежниками) и восстание
// провинции («Мятежные крестьяне»; без войск и гарнизона — сразу выбор захвата).
#include "app/flows.h"
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

TEST(app_turn_events_full_mutiny) {
  Harness h("turn_events_mutiny", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel, 100);
  CHECK(pid != 0);
  if (!pid) return;
  const Id hero = freeHero(h, hel);
  CHECK(hero != 0);
  const Id army = spawnArmy(h, hel, geo::faces(h->world())->shape(pid)->label, 300, hero);
  CHECK(army != 0);
  CHECK(h->act("Верность −100 %", [&](Tx& tx) { rules::setArmyLoyalty(tx, army, -100); }));
  CHECK(h->endTurnNow());
  h.settle();
  CHECK(!h->world().army(army));
  Id rebel = 0;
  h->world().factions.each([&](const Faction& f) {
    if (f.rebelOf == hel) rebel = f.id;
  });
  CHECK(rebel != 0);
  // Верный герой — «Судьба героя»; «сбежал» — герой снова у Хельдвига.
  CHECK(h->hasDialog("hero.fate"));
  CHECK(h.clickUi("fate." + std::to_string(hero) + ".0"));
  CHECK(h.clickUi("fate.ok"));
  h.settle();
  CHECK(!h->hasDialog("hero.fate"));
  CHECK_EQ(h->world().character(hero)->faction, hel);
  CHECK(rules::heroAvailable(h->world(), hero));
  // Затем мятежники в провинции без войск и гарнизона — выбор захвата.
  CHECK(h->hasDialog("capture"));
  CHECK(h.clickUi("capture.cancel"));
  h.settle();
  CHECK(!h->hasDialog());
  CHECK(!h->world().province(pid)->occupied);
}

// Гарнизон с верностью −100 % восстаёт в конце хода целиком (ТЗ «Доработки», п.1): событие хода — мятежники в
// провинции без войск и гарнизона, сразу выбор захвата.
TEST(app_turn_events_garrison_mutiny) {
  Harness h("turn_events_garrison", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel, 100);
  CHECK(pid != 0);
  if (!pid) return;
  const Id row = h->world().faction(hel)->army[0].id;
  CHECK(h->act("Гарнизон −100 %", [&](Tx& tx) {
    for (ArmyRow& r : tx.faction(hel).army)
      if (r.id == row) r.total += 500;
    rules::setGarrison(tx, pid, row, 500);
    rules::setGarrisonLoyalty(tx, pid, -100);
  }));
  CHECK(h->endTurnNow());
  h.settle();
  Id rebel = 0;
  h->world().factions.each([&](const Faction& f) {
    if (f.rebelOf == hel) rebel = f.id;
  });
  CHECK(rebel != 0);
  CHECK(h->world().province(pid)->garrison.empty());
  const Id rebelArmy = armyOf(h->world(), rebel, ArmyKind::Army);
  CHECK(rebelArmy != 0);
  CHECK_EQ(unitsOf(h->world(), rebelArmy), 500);
  // Мятежники в провинции без войск и гарнизона — выбор захвата; «захватить» — оккупация мятежниками.
  CHECK(h->hasDialog("capture"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("turn_events_garrison_capture"));
  CHECK(h.clickUi("capture.option.0"));
  CHECK(h.clickUi("capture.ok"));
  h.settle();
  const Province* p = h->world().province(pid);
  CHECK(p->occupied && p->occupier == rebel);
}

// Войско с верностью −100 % восстаёт в конце хода, а в провинции стоит другое войско государства: мятежники
// нападают на него (окно битвы), без войска — штурм или захват.
TEST(app_turn_events_mutiny_attacks_loyal) {
  Harness h("turn_events_mutiny_loyal", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel, 100);
  CHECK(pid != 0);
  if (!pid) return;
  const Vec2 at = geo::faces(h->world())->shape(pid)->label;
  const Id traitors = spawnArmy(h, hel, at, 300);
  const Id loyal = spawnArmy(h, hel, at + Vec2(120, 0), 400);
  CHECK(traitors && loyal);
  CHECK_EQ(app::mil::provinceUnder(h->world(), h->world().army(loyal)->pos), pid);
  CHECK(h->act("Верность −100 %", [&](Tx& tx) { rules::setArmyLoyalty(tx, traitors, -100); }));
  CHECK(h->endTurnNow());
  h.settle();
  CHECK(!h->world().army(traitors));
  CHECK(h->hasDialog("battle"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("turn_events_mutiny_battle"));
  CHECK(h.clickUi("battle.retreat"));
  h.settle();
  CHECK(!h->hasDialog("battle"));
}

TEST(app_turn_events_province_uprising) {
  Harness h("turn_events_uprising", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel, 100);
  CHECK(pid != 0);
  if (!pid) return;
  i64 pop = 0;
  for (const RacePop& r : h->world().province(pid)->races) pop += r.pop;
  // Риск восстания 100 % и бросок восстания в конце хода.
  CHECK(h->act("Риск восстания", [&](Tx& tx) {
    tx.settings().rebellionRoll = true;
    Id m = rules::createModifier(tx, "Смута");
    Modifier& md = tx.modifier(m);
    md.fx[size_t(Fx::RebellionPct)] = 100;
    md.fxMask |= 1u << unsigned(Fx::RebellionPct);
    rules::addModifier(tx, rules::ModTarget::Province, pid, m);
  }));
  CHECK(h->endTurnNow());
  h.settle();
  Id rebelArmy = 0;
  h->world().armies.each([&](const Army& a) {
    if (a.name == "Мятежные крестьяне" && app::mil::provinceUnder(h->world(), a.pos) == pid) rebelArmy = a.id;
  });
  CHECK(rebelArmy != 0);
  i64 after = 0;
  for (const RacePop& r : h->world().province(pid)->races) after += r.pop;
  CHECK(after < pop);
  // Ни войска, ни гарнизона — сразу выбор захвата; «захватить» — оккупация мятежниками.
  CHECK(h->hasDialog("capture"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("turn_events_uprising_capture"));
  CHECK(h.clickUi("capture.option.0"));
  CHECK(h.clickUi("capture.ok"));
  h.settle();
  const Province* p = h->world().province(pid);
  CHECK(p->occupied);
  CHECK(h->world().faction(p->occupier) && h->world().faction(p->occupier)->rebelOf == hel);
}

namespace {

// Закрыть окна вассалитета войны мятежников (если у прежнего государства есть сюзерен или вассалы): Esc.
void skipVassalWindows(Harness& h) {
  for (int i = 0; i < 6 && h->hasDialog(); i++) {
    h.key(Key::Escape);
    h.settle();
  }
}

// Мятеж войска с верным героем: событие хода в очереди мира и открытое окно «Судьба героя».
struct MutinyEvent {
  Id state = 0, province = 0, hero = 0, army = 0;
};
std::optional<MutinyEvent> mutinyEvent(Harness& h) {
  MutinyEvent m;
  m.state = factionByName(h->world(), "Хельдвиг");
  m.province = calmProvince(h, m.state, 100);
  m.hero = freeHero(h, m.state);
  if (!m.province || !m.hero) return std::nullopt;
  m.army = spawnArmy(h, m.state, geo::faces(h->world())->shape(m.province)->label, 300, m.hero);
  if (!m.army || !h->act("Верность −100 %", [&](Tx& tx) { rules::setArmyLoyalty(tx, m.army, -100); })) return std::nullopt;
  if (!h->endTurnNow()) return std::nullopt;
  h.settle();
  return m;
}

}  // namespace

// Очередь событий хода живёт в мире (аудит 2026-10-23, 2.5): F1 закрывает только справку; закрытие всех окон не
// теряет событие — «Решения» показывают его с «Продолжить»; разобранное событие уходит из очереди без записи в отмену.
TEST(app_turn_events_queue_survives_close_all) {
  Harness h("turn_events_queue", 1440, 1000);
  h.demo();
  auto m = mutinyEvent(h);
  CHECK(m.has_value());
  if (!m) return;
  CHECK_EQ(h->world().meta->turnEvents.size(), size_t(1));
  CHECK(h->hasDialog("hero.fate"));
  // Справка открывается поверх окна события и закрывается, окно события остаётся.
  h.key(Key::F1);
  CHECK(h->hasDialog("help"));
  h.key(Key::F1);
  CHECK(!h->hasDialog("help"));
  CHECK(h->hasDialog("hero.fate"));
  h->showPalette();   // команда Ctrl+K (под модальным окном сочетание не срабатывает — вызов напрямую)
  h.settle();
  CHECK(h->hasDialog("palette"));
  h->showPalette();
  h.settle();
  CHECK(!h->hasDialog("palette"));
  CHECK(h->hasDialog("hero.fate"));
  // Все окна закрыты (как при возврате к ходу): событие не разобрано и остаётся в мире, цепочка не продолжается.
  h->closeDialogs();
  h.settle();
  CHECK(!h->hasDialog());
  CHECK_EQ(h->world().meta->turnEvents.size(), size_t(1));
  CHECK(!app::flow::turnEventsRunning(h.a()));
  // «Решения»: блок «События хода», «Продолжить» — окно события снова открыто.
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect("decisions.events") != nullptr);
  CHECK(h->uiRect("decisions.event.0") != nullptr);
  const RectF* badge = h->uiRect("section.decisions.badge");
  CHECK(badge != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("turn_events_decisions"));
  CHECK(h.clickUi("decisions.events.continue"));
  h.settle();
  CHECK(h->hasDialog("hero.fate"));
  CHECK(app::flow::turnEventsRunning(h.a()));
  // Судьба героя решена — событие остаётся без героев (после перерыва окно не повторится), затем захват.
  CHECK(h.clickUi("fate." + std::to_string(m->hero) + ".0"));
  CHECK(h.clickUi("fate.ok"));
  h.settle();
  CHECK(h->hasDialog("capture"));
  CHECK_EQ(h->world().meta->turnEvents.size(), size_t(1));
  CHECK(h->world().meta->turnEvents.front().heroes.empty());
  const std::string undo = h->store.undoLabel();
  CHECK(h.clickUi("capture.cancel"));
  h.settle();
  skipVassalWindows(h);
  // Событие и война мятежников разобраны; разбор не попал в отмену.
  CHECK(h->world().meta->turnEvents.empty());
  CHECK(!app::flow::turnEventsRunning(h.a()));
  CHECK_EQ(h->store.undoLabel(), undo);
  CHECK(h->uiRect("decisions.events") == nullptr);
}

// Неразобранные события переживают сохранение и перезапуск: открыв мир, администратор продолжает их в «Решениях».
TEST(app_turn_events_saved_with_world) {
  Harness h("turn_events_saved", 1440, 1000);
  h.demo();
  const std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  auto m = mutinyEvent(h);
  CHECK(m.has_value());
  if (!m) return;
  h->closeDialogs();
  h.settle();
  CHECK(h->save());
  CHECK(h->loadProject(dir));
  h.settle();
  CHECK(!h->hasDialog());
  CHECK_EQ(h->world().meta->turnEvents.size(), size_t(1));
  h->openSection("decisions");
  h.settle();
  CHECK(h.clickUi("decisions.events.continue"));
  h.settle();
  CHECK(h->hasDialog("hero.fate"));
  // Esc у окна события — событие разобрано (как раньше: окно закрыто без решения), дальше — следующее окно.
  h.key(Key::Escape);
  h.settle();
  for (int i = 0; i < 8 && h->hasDialog(); i++) {
    h.key(Key::Escape);
    h.settle();
  }
  CHECK(h->world().meta->turnEvents.empty());
}
