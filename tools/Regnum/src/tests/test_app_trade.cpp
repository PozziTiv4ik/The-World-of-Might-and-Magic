// Сценарии торговли: составление сделки в редакторе «Торговля» щелчками (стороны, позиции, режим, срок; ресурс —
// через группы; ресурс с однозначным названием), разовый обмен и отмена Ctrl+Z, сделка «каждый ход» на протяжении ходов, подарок, отказ при нехватке
// ресурса, расторжение с подтверждением, дань через диалог на протяжении ходов, вкладка фракции и выдвижная панель.
#include "app/editors/trade.h"
#include "tests/test_app_util.h"
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
namespace ed = rg::apptest::editors;

namespace {

// Выбрать пункт выпадающего списка с поиском: щелчок, ввод, Enter.
void pick(Harness& h, const std::string& name, const std::string& text) {
  CHECK_MSG(h.clickUi(name), name);
  h.type(text);
  h.key(Key::Enter);
  h.settle();
}

void clickRight(Harness& h, const std::string& name) {
  const RectF* r = h->uiRect(name);
  CHECK_MSG(r != nullptr, name);
  if (!r) return;
  RectF c = *r;
  h.click(c.x + c.w * 0.75f, c.cy());
}

// Название ресурса однозначно в поиске списка: ни одно другое название его не содержит (поиск «Железо» нашёл бы и
// «Алое железо», которое по алфавиту выше).
bool uniqueName(const World& w, Id res) {
  const CatalogItem* c = w.resource(res);
  if (!c || c->name.empty()) return false;
  for (const CatalogItem& o : w.catalogs->resources)
    if (o.id != res && utf8::matches(o.name, c->name)) return false;
  return true;
}

struct Pair {
  Id a = 0, b = 0;
  std::string na, nb;
  Id res = 0;   // ресурс, который есть у обеих сторон (не золото и не провизия: её едят жители — запас уходит в минус)
  std::string rn;
};

Pair twoStates(const World& w) {
  Pair p;
  std::vector<const Faction*> st;
  w.factions.each([&](const Faction& f) {
    if (f.isState()) st.push_back(&f);
  });
  if (st.size() < 2) return p;
  p.a = st[0]->id;
  p.b = st[1]->id;
  p.na = st[0]->name;
  p.nb = st[1]->name;
  const std::vector<Id> provs = rules::provisionResources(w);   // провизию расходует население — не для сделок теста
  for (auto& [r, v] : st[0]->res)
    if (r != kGold && std::find(provs.begin(), provs.end(), r) == provs.end() && v >= 60 && st[1]->stock(r) >= 60 && !p.res && uniqueName(w, r))
      p.res = r;
  if (const CatalogItem* c = w.resource(p.res)) p.rn = c->name;
  return p;
}

void openEditor(Harness& h, const Pair& p) {
  h->openEditor("trade");
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("trade"));
  pick(h, "trade.party.a", p.na);
  pick(h, "trade.party.b", p.nb);
  CHECK_EQ(app::trade::draft().a, p.a);
  CHECK_EQ(app::trade::draft().b, p.b);
}

u64 addItem(Harness& h, char side) {
  size_t n = app::trade::draft().items.size();
  CHECK(h.clickUi(std::string("trade.add.") + side));
  h.settle();
  CHECK_EQ(app::trade::draft().items.size(), n + 1);
  return app::trade::draft().items.empty() ? 0 : app::trade::draft().items.back().key;
}

std::string item(u64 key, const char* field) { return "trade.item." + std::to_string(key) + "." + field; }

}  // namespace

TEST(app_trade_once_and_undo) {
  Harness h("trade_once");
  h.demo();
  app::trade::startDraft(0, 0);
  World w0 = h->store.world();
  Pair p = twoStates(w0);
  CHECK(p.a && p.b && p.res);
  openEditor(h, p);
  u64 ka = addItem(h, 'a');   // А: золото 100 разово
  u64 kb = addItem(h, 'b');   // Б: ресурс 50 разово
  pick(h, item(kb, "res"), p.rn);
  enterInDialog(h, item(kb, "amount"), "50");
  CHECK_EQ(app::trade::draft().items.back().res, p.res);
  CHECK_NEAR(app::trade::draft().items.back().amount, 50, 1e-9);
  CHECK(ka != 0);
  shotClean(h, "trade_editor");
  Id before = test::newestDeal(w0);
  CHECK(h.clickUi("trade.conclude"));
  h.settle();
  const World& w1 = h->store.world();
  Id did = test::newestDeal(w1);
  CHECK(did > before);
  const Deal* d = w1.deal(did);
  CHECK(d && d->status == DealStatus::Done && d->kind == DealKind::Trade);
  CHECK_NEAR(w1.faction(p.a)->treasury(), w0.faction(p.a)->treasury() - 100, 1e-6);
  CHECK_NEAR(w1.faction(p.b)->treasury(), w0.faction(p.b)->treasury() + 100, 1e-6);
  CHECK_NEAR(w1.faction(p.a)->stock(p.res), w0.faction(p.a)->stock(p.res) + 50, 1e-6);
  CHECK_NEAR(w1.faction(p.b)->stock(p.res), w0.faction(p.b)->stock(p.res) - 50, 1e-6);
  CHECK(app::trade::draft().items.empty());
  bool logged = false;
  w1.log.each([&](const LogEntry& e) {
    if (e.kind == LogKind::Trade && e.turn == w1.turn() && e.factions.size() == 2) logged = true;
  });
  CHECK(logged);
  shotClean(h, "trade_editor_done");
  // Отмена Ctrl+Z.
  h.key(Key::Z, ctrl());
  CHECK(!h->store.world().deal(did));
  CHECK_NEAR(h->store.world().faction(p.a)->treasury(), w0.faction(p.a)->treasury(), 1e-6);
}

TEST(app_trade_per_turn_lifecycle_and_cancel) {
  Harness h("trade_turns");
  h.demo();
  app::trade::startDraft(0, 0);
  Pair p = twoStates(h->store.world());
  CHECK(p.a && p.b && p.res);
  openEditor(h, p);
  u64 ka = addItem(h, 'a');   // А: ресурс 20 за ход, 2 хода
  pick(h, item(ka, "res"), p.rn);
  enterInDialog(h, item(ka, "amount"), "20");
  clickRight(h, item(ka, "mode"));
  CHECK(app::trade::draft().items.back().mode == DealMode::PerTurn);
  enterInDialog(h, item(ka, "turns"), "2");
  u64 kb = addItem(h, 'b');   // Б: золото 30 за ход, 2 хода
  enterInDialog(h, item(kb, "amount"), "30");
  clickRight(h, item(kb, "mode"));
  enterInDialog(h, item(kb, "turns"), "2");
  CHECK_EQ(app::trade::draft().items[0].turns, 2);
  CHECK_EQ(app::trade::draft().items[1].turns, 2);
  shotClean(h, "trade_editor_per_turn");
  CHECK(h.clickUi("trade.conclude"));
  h.settle();
  Id did = test::newestDeal(h->store.world());
  const Deal* d = h->store.world().deal(did);
  CHECK(d && d->status == DealStatus::Active);
  CHECK_EQ(app::trade::dealLeft(*d), 2);
  // Золото «каждый ход» — в доходе и расходе сторон.
  auto c0 = rules::calc(h->store.world());
  CHECK(c0->faction(p.a)->incTrade >= 30 - 1e-9);
  CHECK(c0->faction(p.b)->expTrade >= 30 - 1e-9);
  // Два хода: ресурс передаётся после добычи (и расхода: провизию едят жители государства живых), остаток
  // уменьшается, затем «выполнена».
  for (int turn = 0; turn < 2; turn++) {
    World w = h->store.world();
    auto c = rules::calc(w);
    double prodA = 0, prodB = 0;
    if (auto it = c->faction(p.a)->resources.find(p.res); it != c->faction(p.a)->resources.end()) prodA = it->second.production - it->second.consumption;
    if (auto it = c->faction(p.b)->resources.find(p.res); it != c->faction(p.b)->resources.end()) prodB = it->second.production - it->second.consumption;
    CHECK(h->endTurnNow());
    h.settle();
    const World& w2 = h->store.world();
    CHECK_NEAR(w2.faction(p.a)->stock(p.res), w.faction(p.a)->stock(p.res) + prodA - 20, 1e-6);
    CHECK_NEAR(w2.faction(p.b)->stock(p.res), w.faction(p.b)->stock(p.res) + prodB + 20, 1e-6);
    CHECK_EQ(app::trade::dealLeft(*w2.deal(did)), 1 - turn);
  }
  CHECK(h->store.world().deal(did)->status == DealStatus::Done);
  // Новая сделка «каждый ход» и расторжение из списка (с подтверждением).
  h->openEditor("trade");
  h.settle();
  u64 k = addItem(h, 'a');
  clickRight(h, item(k, "mode"));
  CHECK(h.clickUi("trade.conclude"));
  h.settle();
  Id d2 = test::newestDeal(h->store.world());
  CHECK(h->store.world().deal(d2)->status == DealStatus::Active);
  shotClean(h, "trade_editor_list");
  CHECK(h.clickUi("trade.cancel." + std::to_string(d2)));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->store.world().deal(d2)->status == DealStatus::Cancelled);
}

TEST(app_trade_gift_and_insufficient) {
  Harness h("trade_gift");
  h.demo();
  app::trade::startDraft(0, 0);
  World w0 = h->store.world();
  Pair p = twoStates(w0);
  openEditor(h, p);
  // Недостаточно золота: сделка не заключается, причина показана.
  u64 k = addItem(h, 'a');
  enterInDialog(h, item(k, "amount"), "999999999");
  rules::DealCheck chk = rules::validateDeal(h->store.world(), app::trade::toDeal(app::trade::draft()));
  CHECK(!chk.ok);
  CHECK(!chk.problems.empty());
  CHECK(h->uiRect("trade.check") != nullptr);
  shotClean(h, "trade_insufficient");
  Id before = test::newestDeal(h->store.world());
  CHECK(h.clickUi("trade.conclude"));
  h.settle();
  CHECK_EQ(test::newestDeal(h->store.world()), before);
  CHECK(World::diff(w0, h->store.world()) == 0);
  // Подарок: позиции только у стороны А.
  pick(h, item(k, "res"), p.rn);
  enterInDialog(h, item(k, "amount"), "15");
  shotClean(h, "trade_gift");
  CHECK(h.clickUi("trade.conclude"));
  h.settle();
  Id did = test::newestDeal(h->store.world());
  CHECK(did > before);
  const World& w1 = h->store.world();
  CHECK(w1.deal(did)->status == DealStatus::Done);
  CHECK_NEAR(w1.faction(p.b)->stock(p.res), w0.faction(p.b)->stock(p.res) + 15, 1e-6);
  CHECK_NEAR(w1.faction(p.a)->stock(p.res), w0.faction(p.a)->stock(p.res) - 15, 1e-6);
}

TEST(app_trade_tribute_over_turns) {
  Harness h("trade_tribute");
  h.demo();
  Pair p = twoStates(h->store.world());
  // Дань от лица государства А (вкладка фракции).
  h->ui.tabOf[app::SelType::Faction] = "faction.trade";
  h->select(app::SelType::Faction, p.a);
  h.settle();
  CHECK(h->uiRect("faction.trade.tribute") != nullptr);
  shotClean(h, "trade_faction_tab");
  CHECK(h.clickUi("faction.trade.tribute"));
  h.settle();
  CHECK(h->hasDialog("tribute"));
  pick(h, "tribute.payer", p.nb);
  enterInDialog(h, "tribute.amount", "40");
  enterInDialog(h, "tribute.turns", "3");
  shotClean(h, "trade_tribute_dialog");
  Id before = test::newestDeal(h->store.world());
  CHECK(h.clickUi("tribute.ok"));
  h.settle();
  CHECK(!h->hasDialog("tribute"));
  Id did = test::newestDeal(h->store.world());
  CHECK(did > before);
  const Deal* d = h->store.world().deal(did);
  CHECK(d && d->kind == DealKind::Tribute && d->a == p.a && d->b == p.b);
  CHECK(d && d->items.size() == 1 && std::fabs(d->items[0].amount - 40) < 1e-9 && d->items[0].turns == 3);
  auto c = rules::calc(h->store.world());
  CHECK(c->faction(p.a)->incTribute >= 40 - 1e-9);
  CHECK(c->faction(p.b)->expTribute >= 40 - 1e-9);
  for (int i = 0; i < 3; i++) {
    CHECK(h->store.world().deal(did)->status == DealStatus::Active);
    CHECK(h->endTurnNow());
  }
  CHECK(h->store.world().deal(did)->status == DealStatus::Done);
  bool done = false;
  h->store.world().log.each([&](const LogEntry& e) {
    if (e.kind == LogKind::Diplomacy && startsWith(e.text, "Завершена выплата дани")) done = true;
  });
  CHECK(done);
  // «Новая сделка» во вкладке — редактор со стороной А.
  h.settle();
  CHECK(h.clickUi("faction.trade.new"));
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("trade"));
  CHECK_EQ(app::trade::draft().a, p.a);
}

TEST(app_trade_drawer_and_light) {
  Harness h("trade_light", 1440, 900, 1, kAltScheme);
  h.demo();
  // Правая лента → «Экономика»: переговоры и торговля.
  CHECK(h.clickUi("section.economy"));
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("trade"));
  shotClean(h, "trade_drawer_light");
  Pair p = twoStates(h->store.world());
  pick(h, "trade.party.a", p.na);
  pick(h, "trade.party.b", p.nb);
  u64 k = addItem(h, 'a');
  clickRight(h, item(k, "mode"));
  addItem(h, 'b');
  shotClean(h, "trade_editor_light");
}

// ---------------------------------------------------------------- торговля: ресурс через группы
TEST(app_trade_groups) {
  Harness h("econ2_trade", 1600, 1000);
  h.demo();
  h.dropToasts();
  auto [a, b] = test::twoStates(h->world());
  CHECK(a && b);
  app::trade::startDraft(a, b);
  h->openEditor("trade", a);
  h.settle();
  CHECK(h.clickUi("trade.add.a"));
  h.settle();
  CHECK(!app::trade::draft().items.empty());
  const u64 key = app::trade::draft().items.back().key;
  const std::string base = "trade.item." + std::to_string(key);
  // Группа «Руда» → в списке ресурсов только её ресурсы; выбор ресурса руды с однозначным названием («Медь»).
  CHECK(ed::pickInCombo(h, base + ".group", "Руда"));
  const Id ore = h->world().catalogs->groupId(schema::grp::Ore);
  CHECK(ore != 0);
  CHECK_EQ(app::trade::draft().items.back().group, ore);
  Id metal = 0;
  for (Id r : rules::resourcesIn(h->world(), ore))
    if (!metal && uniqueName(h->world(), r)) metal = r;
  CHECK(metal != 0);
  CHECK(ed::pickInCombo(h, base + ".res", h->world().resource(metal)->name));
  CHECK_EQ(app::trade::draft().items.back().res, metal);
  CHECK(h->world().catalogs->resourceIn(metal, ore));
  shotClean(h, "econ2_trade_groups");
}
