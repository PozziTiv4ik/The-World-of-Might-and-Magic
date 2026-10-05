// Сценарии перемирия (ТЗ «Механика войн», п.2) на демонстрационном мире: война государств во вкладке «Дипломатия»
// заблокирована, окно перемирия — провинции, репарации за ход и срок, разовые выплаты золота (до тысячных) и
// ресурсов, рабы (не больше 5 % населения), пленные герои, вассалитет, итоговое состояние; причины отказа; отмена.
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

namespace {

Id characterByName(const World& w, std::string_view name) {
  Id r = 0;
  w.characters.each([&](const Character& c) {
    if (!r && c.name == name) r = c.id;
  });
  return r;
}

// Ввод в поле окна (окно поверх инспектора — без прокрутки инспектора).
bool typeIn(Harness& h, const std::string& name, const std::string& value) {
  if (!h.clickUi(name)) return false;
  h.retype(value);
  h.key(Key::Enter);
  h.step();
  return true;
}

void shotClean(Harness& h, const std::string& name) {
  h.waitMap();
  h.settle();
  CHECK(h.shot(name));
}

i64 slavesOf(const World& w, Id state) {
  i64 n = 0;
  if (const Faction* f = w.faction(state))
    for (const SlaveGroup& g : f->slaves) n += g.count;
  return n;
}

}  // namespace

TEST(app_diplomacy_truce_terms) {
  HideTestRegs regs;
  Harness h("diplomacy_truce", 1440, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id hel = findFaction(h->world(), "Северный союз Хельдвиг");
  const Id vk = findFaction(h->world(), "Империя Валь-Кетра");
  CHECK(hel && vk);
  CHECK(h->world().relation(hel, vk).s == RelStatus::War);
  // Пленный герой Хельдвига у Валь-Кетры — вернётся по перемирию.
  const Id bjorn = characterByName(h->world(), "Бьорн Медведь");
  CHECK(h->act("Плен", [&](Tx& tx) { rules::heroFate(tx, bjorn, rules::Fate::Captured, vk, 0); }));
  CHECK_EQ(rules::captivesOf(h->world(), vk).size(), size_t(1));

  openTab(h, hel, "faction.diplomacy");
  CHECK(ensureVisible(h, "dip.truce." + std::to_string(vk)));
  CHECK(h.clickUi("dip.truce." + std::to_string(vk)));
  h.settle();
  CHECK(h->hasDialog("truce"));
  shotClean(h, "diplomacy_truce_empty");

  // Валь-Кетра отдаёт провинцию, платит репарации и разово золото, отдаёт рабов и пленного, становится вассалом.
  Id prov = 0;
  std::string pname;
  std::map<std::string, int> names;
  h->world().provinces.each([&](const Province& p) { names[p.name]++; });
  h->world().provinces.each([&](const Province& p) {
    if (!prov && p.owner == vk && !p.sea && p.id != h->world().faction(vk)->capital && names[p.name] == 1) {
      prov = p.id;
      pname = p.name;
    }
  });
  CHECK(prov != 0);
  CHECK(h.clickUi("truce.b.province"));
  h.type(pname);
  h.key(Key::Enter);
  h.step();
  CHECK(typeIn(h, "truce.b.rep", "12,345"));
  CHECK(typeIn(h, "truce.b.repTurns", "4"));
  CHECK(typeIn(h, "truce.b.gold", "100,5"));
  const i64 cap = rules::truceSlavesMax(h->world(), vk);
  CHECK(cap > 1000);
  CHECK(typeIn(h, "truce.b.slaves", "1000"));
  // Больше 5 % населения — поле не пускает выше предела; итог без рабов от Хельдвига.
  {
    const i64 capA = rules::truceSlavesMax(h->world(), hel);
    CHECK(capA > 0);
    CHECK(typeIn(h, "truce.a.slaves", std::to_string(capA + 500)));
    h.settle();
    CHECK(h->uiRect("truce.problems") == nullptr);   // значение ограничено пределом
    CHECK(typeIn(h, "truce.a.slaves", "0"));
  }
  CHECK(h.clickUi("truce.b.hero." + std::to_string(bjorn)));
  CHECK(h.clickUi("truce.b.vassal"));
  h.settle();
  CHECK(h->uiRect("truce.problems") == nullptr);
  shotClean(h, "diplomacy_truce_terms");

  const double goldH = h->world().faction(hel)->treasury(), goldV = h->world().faction(vk)->treasury();
  const i64 popV = rules::statePopulation(h->world(), vk);
  const u32 deals0 = h->world().deals.size();
  CHECK(h.clickUi("truce.ok"));
  h.settle();
  CHECK(!h->hasDialog("truce"));
  {
    const World& w = h->world();
    CHECK(w.relation(hel, vk).s == RelStatus::Neutral);   // по умолчанию — «Статус-кво»
    CHECK_EQ(w.province(prov)->owner, hel);
    CHECK_NEAR(w.faction(hel)->treasury(), goldH + 100.5, 1e-9);
    CHECK_NEAR(w.faction(vk)->treasury(), goldV - 100.5, 1e-9);
    CHECK_EQ(slavesOf(w, hel), i64(1000));
    i64 provPop = 0;
    for (const RacePop& rp : h->store.world().province(prov)->races) provPop += rp.pop;
    CHECK_EQ(rules::statePopulation(w, vk), popV - 1000 - provPop);
    CHECK(!rules::characterHas(w, bjorn, schema::mod::Captive));
    CHECK(rules::captivesOf(w, vk).empty());
    CHECK_EQ(w.faction(vk)->suzerain, hel);
    // Репарации — как в окне «Дань или репарации»: получатель Хельдвиг, плательщик Валь-Кетра.
    CHECK_EQ(w.deals.size(), deals0 + 1);
    bool rep = false;
    w.deals.each([&](const Deal& d) {
      if (d.kind == DealKind::Reparations && d.a == hel && d.b == vk && d.status == DealStatus::Active && !d.items.empty()) {
        rep = true;
        CHECK_NEAR(d.items[0].amount, 12.345, 1e-9);
        CHECK_EQ(d.items[0].turns, 4);
      }
    });
    CHECK(rep);
  }
  // У сюзерена появилась вкладка «Вассалы».
  openTab(h, hel, "faction.vassals");
  CHECK_EQ(h->ui.tabOf[app::SelType::Faction], std::string("faction.vassals"));
  CHECK(h->uiRect("vassals.row." + std::to_string(vk)) != nullptr);
  shotClean(h, "diplomacy_truce_vassals_tab");
  // Отмена одним шагом возвращает войну и всё отданное.
  h->undo();
  h.step();
  CHECK(h->world().relation(hel, vk).s == RelStatus::War);
  CHECK_EQ(h->world().province(prov)->owner, vk);
  CHECK_EQ(h->world().faction(vk)->suzerain, Id(0));
  CHECK(rules::characterHas(h->world(), bjorn, schema::mod::Captive));
}

TEST(app_diplomacy_truce_problems) {
  HideTestRegs regs;
  Harness h("diplomacy_truce_problems", 1440, 1000);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id orda = findFaction(h->world(), "Орда Таргаш");
  const Id korven = findFaction(h->world(), "Республика Корвен");
  CHECK(h->world().relation(orda, korven).s == RelStatus::War);
  openTab(h, orda, "faction.diplomacy");
  CHECK(clickIn(h, "dip.truce." + std::to_string(korven)));
  h.settle();
  CHECK(h->hasDialog("truce"));
  // Обе стороны — вассалы друг друга: причина видна, «Заключить» недоступно.
  CHECK(h.clickUi("truce.a.vassal"));
  CHECK(h.clickUi("truce.b.vassal"));
  h.settle();
  CHECK(h->uiRect("truce.problems") != nullptr);
  CHECK(h.clickUi("truce.ok"));
  h.settle();
  CHECK(h->hasDialog("truce"));
  CHECK(h->world().relation(orda, korven).s == RelStatus::War);
  // Золото сверх казны — тоже причина.
  CHECK(h.clickUi("truce.b.vassal"));
  CHECK(typeIn(h, "truce.a.gold", std::to_string(i64(h->world().faction(orda)->treasury()) + 1000)));
  h.settle();
  CHECK(h->uiRect("truce.problems") != nullptr);
  shotClean(h, "diplomacy_truce_problems");
  CHECK(typeIn(h, "truce.a.gold", "0"));
  h.settle();
  CHECK(h->uiRect("truce.problems") == nullptr);
  // Итог «В союзе» (средний из трёх).
  {
    const RectF* st = h->uiRect("truce.status");
    CHECK(st != nullptr);
    if (st) {
      RectF r = *st;
      h.click(r.x + r.w * 0.5f, r.cy());
    }
  }
  CHECK(h.clickUi("truce.ok"));
  h.settle();
  CHECK(!h->hasDialog("truce"));
  CHECK(h->world().relation(orda, korven).s == RelStatus::Alliance);
  CHECK_EQ(h->world().faction(orda)->suzerain, korven);
  // После перемирия состояние снова меняется во вкладке (кнопки перемирия нет).
  openTab(h, orda, "faction.diplomacy");
  CHECK(h->uiRect("dip.truce." + std::to_string(korven)) == nullptr);
}
