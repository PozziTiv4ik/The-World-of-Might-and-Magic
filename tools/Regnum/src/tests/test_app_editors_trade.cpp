// Сценарии окна «Переговоры и торговля» (ТЗ «Механика героев», п.2; «Механика войн», п.3): название окна,
// обмен провинцией и пленным героем (разово), золото до тысячных, возврат пленника домой (теряет «Взят в плен»,
// уходит из пленников), отмена Ctrl+Z, провинцию не получает гильдия, дань с тысячными.
#include "app/editors/trade.h"
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;

namespace {

void pick(Harness& h, const std::string& name, const std::string& text) {
  CHECK_MSG(h.clickUi(name), name);
  h.type(text);
  h.key(Key::Enter);
  quick(h);
}

// Число в поле с фиксацией уходом фокуса (Enter в модальном окне нажал бы основную кнопку).
void enterTab(Harness& h, const std::string& name, const std::string& value) {
  CHECK_MSG(h.clickUi(name), name);
  h.key(Key::A, ctrl());
  h.type(value);
  h.key(Key::Tab);
  quick(h);
}

Id newestDeal(const World& w) {
  Id id = 0;
  w.deals.each([&](const Deal& d) { id = std::max(id, d.id); });
  return id;
}

// Доступный персонаж государства (не правитель, не мёртв, не в плену).
Id heroOf(const World& w, Id state) {
  Id r = 0;
  const Faction* f = w.faction(state);
  w.characters.each([&](const Character& c) {
    if (!r && c.faction == state && (!f || f->ruler != c.id) && rules::heroAvailable(w, c.id)) r = c.id;
  });
  return r;
}

}  // namespace

TEST(app_editors_trade_province_and_captive) {
  Harness h("editors_trade");
  h.demo();
  h.dropToasts();
  app::trade::startDraft(0, 0);
  auto [A, B] = twoStates(h->world());
  CHECK(A && B);
  // Герой Б — в плену у А.
  Id hero = heroOf(h->world(), B);
  CHECK(hero != 0);
  CHECK(h->act("Плен", [&](Tx& tx) { rules::heroFate(tx, hero, rules::Fate::Captured, A, 0); }));
  CHECK_EQ(rules::captivesOf(h->world(), A).size(), size_t(1));
  World w0 = h->store.world();
  // Окно переименовано.
  CHECK(app::findEditor("trade") != nullptr);
  CHECK_EQ(std::string(app::findEditor("trade")->title), std::string("Переговоры и торговля"));
  h->openEditor("trade");
  quick(h);
  pick(h, "trade.party.a", w0.factionName(A));
  pick(h, "trade.party.b", w0.factionName(B));
  CHECK_EQ(app::trade::draft().a, A);
  CHECK_EQ(app::trade::draft().b, B);
  // У Б пленников нет — кнопка недоступна.
  CHECK(h.clickUi("trade.addhero.b"));
  quick(h);
  CHECK(app::trade::draft().items.empty());
  // А отдаёт свою провинцию и пленного героя.
  CHECK(h.clickUi("trade.addprov.a"));
  quick(h);
  CHECK_EQ(app::trade::draft().items.size(), size_t(1));
  const app::trade::DraftItem prov = app::trade::draft().items.back();
  CHECK(prov.kind == DealItemKind::Province);
  CHECK(w0.province(prov.ref) && w0.province(prov.ref)->owner == A);
  CHECK(h.clickUi("trade.addhero.a"));
  quick(h);
  CHECK(app::trade::draft().items.back().kind == DealItemKind::Hero);
  CHECK_EQ(app::trade::draft().items.back().ref, hero);
  // Б отдаёт золото до тысячных.
  CHECK(h.clickUi("trade.add.b"));
  quick(h);
  const u64 kb = app::trade::draft().items.back().key;
  enterTab(h, "trade.item." + std::to_string(kb) + ".amount", "12,345");
  CHECK_NEAR(app::trade::draft().items.back().amount, 12.345, 1e-9);
  const Deal planned = app::trade::toDeal(app::trade::draft());
  rules::DealCheck chk = rules::validateDeal(h->store.world(), planned);
  CHECK_MSG(chk.ok, chk.problems.empty() ? std::string() : chk.problems.front());
  shotClean(h, "editors_trade_draft");
  CHECK(h.clickUi("trade.conclude"));
  quick(h);
  const World& w1 = h->store.world();
  Id did = newestDeal(w1);
  const Deal* d = w1.deal(did);
  CHECK(d && d->status == DealStatus::Done && d->items.size() == 3);
  // Провинция перешла к Б, пленник вернулся домой: без «Взят в плен», не в списке пленников А.
  CHECK_EQ(w1.province(prov.ref)->owner, B);
  CHECK(!rules::characterHas(w1, hero, schema::mod::Captive));
  CHECK_EQ(w1.character(hero)->captor, Id(0));
  CHECK(rules::captivesOf(w1, A).empty());
  bool gold = false;
  if (d)
    for (const DealItem& it : d->items) gold = gold || (it.kind == DealItemKind::Resource && it.res == kGold && std::fabs(it.amount - 12.345) < 1e-12);
  CHECK(gold);
  {
    Tx tx(w0);   // то же правило на прежнем мире — та же казна
    rules::concludeDeal(tx, planned);
    World ref = std::move(tx).finish();
    CHECK_NEAR(w1.faction(A)->treasury(), ref.faction(A)->treasury(), 1e-9);
    CHECK_NEAR(w1.faction(B)->treasury(), ref.faction(B)->treasury(), 1e-9);
  }
  shotClean(h, "editors_trade_done");
  // Ctrl+Z — всё как было.
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK_EQ(h->store.world().province(prov.ref)->owner, A);
  CHECK(rules::characterHas(h->store.world(), hero, schema::mod::Captive));
  // Гильдия провинцию не получает: кнопка недоступна; при смене стороны позиции-провинции убираются.
  Id guild = firstGuild(h->world());
  CHECK(guild != 0);
  app::trade::startDraft(A, B);
  quick(h);
  CHECK(h.clickUi("trade.addprov.a"));
  quick(h);
  CHECK_EQ(app::trade::draft().items.size(), size_t(1));
  pick(h, "trade.party.b", h->world().factionName(guild));
  CHECK_EQ(app::trade::draft().b, guild);
  CHECK(app::trade::draft().items.empty());
  CHECK(h.clickUi("trade.addprov.a"));
  quick(h);
  CHECK(app::trade::draft().items.empty());
}

TEST(app_editors_trade_tribute_thousandths) {
  Harness h("editors_trade_tribute");
  h.demo();
  h.dropToasts();
  auto [A, B] = twoStates(h->world());
  h->openDialog("tribute", A);
  quick(h);
  CHECK(h->hasDialog("tribute"));
  pick(h, "tribute.payer", h->world().factionName(B));
  enterTab(h, "tribute.amount", "0,125");
  enterTab(h, "tribute.turns", "2");
  CHECK(h.clickUi("tribute.ok"));
  quick(h);
  const Deal* d = h->store.world().deal(newestDeal(h->store.world()));
  CHECK(d && d->kind == DealKind::Tribute && d->items.size() == 1);
  CHECK_NEAR(d ? d->items[0].amount : 0, 0.125, 1e-12);
  // Карточка во вкладке фракции «Переговоры и торговля».
  h->ui.tabOf[app::SelType::Faction] = "faction.trade";
  h->select(app::SelType::Faction, A);
  quick(h);
  shotClean(h, "editors_trade_tab");
}
