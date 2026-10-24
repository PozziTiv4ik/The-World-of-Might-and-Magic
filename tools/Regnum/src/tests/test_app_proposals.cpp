// Предложения другой стороне и раздел «Решения» (аудит 2026-10-23, 1.1, 1.2, 5.15, 5.16): игрок предлагает сделку с
// позициями другой стороны, дань, перемирие и союз — мир не меняется, пока другая сторона или администратор не ответят
// в «Решениях»; подарок своего — сразу. Дань и репарации отменяет только получатель, расторжение сделки игроком —
// штраф отношений. Война из «Дипломатии» — после подтверждения. Вопросы вассалитета государствам не за этим
// компьютером — предложения; принятый вопрос (вступление в войну) — тот же разбор для нового участника.
#include "app/editors/trade.h"
#include "app/flows.h"
#include "app/shell/role.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;
namespace role = rg::app::detail::role;

namespace {

constexpr const char* kOwn = "Королевство Альмарин";
constexpr const char* kOther = "Вольные города Ольсты";

Id proposalWhere(const World& w, const std::function<bool(const Proposal&)>& fn) { return w.proposals.firstId(fn); }

std::string pmark(Id p, const char* what) { return "decisions.proposal." + std::to_string(p) + "." + what; }

// Ресурс (не золото), которого у фракции не меньше need.
Id stockOf(const World& w, Id faction, double need) {
  const Faction* f = w.faction(faction);
  if (!f) return 0;
  for (auto& [r, v] : f->res)
    if (r != kGold && v >= need && w.resource(r)) return r;
  return 0;
}

// Выбор фракции в окне: щелчок по полю, название, Enter.
void pickFaction(Harness& h, const std::string& field, const std::string& name) {
  CHECK_MSG(h.clickUi(field), field);
  h.type(name);
  h.key(Key::Enter);
  h.settle();
}

// Число в поле окна с фиксацией уходом фокуса.
void enterTab(Harness& h, const std::string& field, const std::string& value) {
  CHECK_MSG(h.clickUi(field), field);
  h.key(Key::A, ctrl());
  h.type(value);
  h.key(Key::Tab);
  h.settle();
}

// Состояние отношений из «Дипломатии» государства us к them (война — с подтверждением).
void pickStatus(Harness& h, Id us, Id them, RelStatus s) {
  openTab(h, us, "faction.diplomacy");
  CHECK(clickIn(h, "dip.status." + std::to_string(them)));
  CHECK(h.clickUi("status.item." + std::to_string(int(s))));
  h.settle();
}

}  // namespace

// Сделка игрока с позициями другой стороны — предложение; другая сторона принимает в «Решениях» — сделка исполнена.
// Только свои позиции — подарок сразу.
TEST(app_proposals_trade_offer_and_gift) {
  Harness h("proposals_trade", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id own = findFaction(h->world(), kOwn), other = findFaction(h->world(), kOther);
  CHECK(own && other);
  const Id res = stockOf(h->world(), other, 20);
  CHECK(res != 0);
  role::becomePlayer(h.a(), own);
  h.settle();
  app::trade::startDraft(own, other);
  {
    app::trade::DraftItem& it = app::trade::addItem(DealSide::A);
    it.res = kGold;
    it.amount = 50;
  }
  {
    app::trade::DraftItem& it = app::trade::addItem(DealSide::B);
    it.res = res;
    it.amount = 10;
  }
  h->openEditor("trade");
  h.settle();
  const double gold0 = h->world().faction(own)->treasury(), stock0 = h->world().faction(other)->stock(res);
  const Id deal0 = test::newestDeal(h->world());
  CHECK(h.clickUi("trade.conclude"));   // «Предложить»
  h.settle();
  CHECK_EQ(test::newestDeal(h->world()), deal0);
  CHECK_NEAR(h->world().faction(own)->treasury(), gold0, 1e-9);
  const Id p = proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Deal && x.from == own && x.to == other; });
  CHECK(p != 0);
  CHECK(app::trade::draft().items.empty());
  // У игрока: исходящее — «Отозвать», ответить на своё нельзя; входящих нет — значка нет.
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect(pmark(p, "withdraw")) != nullptr);
  CHECK(h->uiRect(pmark(p, "accept")) == nullptr);
  CHECK(h->uiRect("section.decisions.badge") == nullptr);
  shotClean(h, "proposals_decisions_outgoing");
  // Ольсты (игрок за другим компьютером): входящее на значке, «Принять» — сделка исполнена.
  role::becomePlayer(h.a(), other);
  h.settle();
  CHECK(h->uiRect("section.decisions.badge") != nullptr);
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect(pmark(p, "withdraw")) == nullptr);
  shotClean(h, "proposals_decisions_incoming");
  CHECK(h.clickUi(pmark(p, "accept")));
  h.settle();
  CHECK(h->world().proposal(p) == nullptr);
  CHECK(test::newestDeal(h->world()) > deal0);
  CHECK_NEAR(h->world().faction(own)->treasury(), gold0 - 50, 1e-9);
  CHECK_NEAR(h->world().faction(other)->stock(res), stock0 - 10, 1e-9);
  CHECK(h->uiRect("section.decisions.badge") == nullptr);

  // Подарок своего — сразу, без предложения.
  role::becomePlayer(h.a(), own);
  h.settle();
  app::trade::startDraft(own, other);
  {
    app::trade::DraftItem& it = app::trade::addItem(DealSide::A);
    it.res = kGold;
    it.amount = 5;
  }
  h->openEditor("trade");
  h.settle();
  const Id deal1 = test::newestDeal(h->world());
  CHECK(h.clickUi("trade.conclude"));   // «Подарить»
  h.settle();
  CHECK(test::newestDeal(h->world()) > deal1);
  CHECK(h->world().proposals.empty());
}

// Входящие игрока: «Принять» союз, «Отклонить» (запись в хронике), исходящее — «Отозвать». Администратор видит все
// предложения и решает за любую сторону.
TEST(app_proposals_decisions_answers) {
  Harness h("proposals_answers", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id own = findFaction(h->world(), kOwn), other = findFaction(h->world(), kOther);
  const Id solmar = findFaction(h->world(), "Теократия Солмар");
  CHECK(own && other && solmar);
  Id ally = 0, refuse = 0, mine = 0;
  CHECK(h->act("Предложения", [&](Tx& tx) {
    Proposal a;
    a.kind = ProposalKind::Alliance;
    a.from = other;
    a.to = own;
    ally = rules::propose(tx, a);
    Proposal b;
    b.kind = ProposalKind::Alliance;
    b.from = solmar;
    b.to = own;
    refuse = rules::propose(tx, b);
    Proposal c;
    c.kind = ProposalKind::Alliance;
    c.from = own;
    c.to = findFaction(tx.w(), "Республика Корвен");
    mine = rules::propose(tx, c);
  }));
  CHECK(ally && refuse && mine);
  // Администратор: все три с решениями за любую сторону.
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect("decisions.proposals") != nullptr);
  for (Id p : {ally, refuse, mine}) CHECK_MSG(h->uiRect(pmark(p, "accept")) != nullptr, std::to_string(p));
  shotClean(h, "proposals_decisions_admin");
  // Игрок: два входящих (значок), одно исходящее.
  role::becomePlayer(h.a(), own);
  h.settle();
  CHECK(h->uiRect("section.decisions.badge") != nullptr);
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect(pmark(ally, "accept")) != nullptr);
  CHECK(h->uiRect(pmark(refuse, "decline")) != nullptr);
  CHECK(h->uiRect(pmark(mine, "withdraw")) != nullptr);
  CHECK(h->uiRect(pmark(mine, "accept")) == nullptr);
  CHECK(h.clickUi(pmark(ally, "accept")));
  h.settle();
  CHECK(h->world().relation(own, other).s == RelStatus::Alliance);
  CHECK(h->world().proposal(ally) == nullptr);
  const u32 log0 = h->world().log.size();
  CHECK(h.clickUi(pmark(refuse, "decline")));
  h.settle();
  CHECK(h->world().proposal(refuse) == nullptr);
  CHECK(h->world().relation(own, solmar).s != RelStatus::Alliance);
  CHECK(h->world().log.size() > log0);
  CHECK(h.clickUi(pmark(mine, "withdraw")));
  h.settle();
  CHECK(h->world().proposals.empty());
  CHECK(h->uiRect("section.decisions.badge") == nullptr);
  // Отмена — у игрока своя: принятый союз снимается Ctrl+Z.
  h->undo();
  h->undo();
  h->undo();
  h.settle();
  CHECK(h->world().relation(own, other).s != RelStatus::Alliance);
  CHECK(h->world().proposal(ally) != nullptr);
}

// Дань: игрок-получатель предлагает её плательщику; администратор принимает. Отменяет выплаты только получатель (у
// плательщика кнопки нет), расторжение сделки игроком — отношения «Расторжение сделки».
TEST(app_proposals_tribute_and_cancel) {
  Harness h("proposals_tribute", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id own = findFaction(h->world(), kOwn), other = findFaction(h->world(), kOther);
  CHECK(own && other);
  role::becomePlayer(h.a(), own);
  h.settle();
  const Id deal0 = test::newestDeal(h->world());
  CHECK(h->openDialog("tribute", own));
  h.settle();
  pickFaction(h, "tribute.payer", h->world().factionName(other));
  enterTab(h, "tribute.amount", "10");
  enterTab(h, "tribute.turns", "3");
  shotClean(h, "proposals_tribute_dialog");
  CHECK(h.clickUi("tribute.ok"));   // «Предложить дань»
  h.settle();
  CHECK(!h->hasDialog("tribute"));
  CHECK_EQ(test::newestDeal(h->world()), deal0);
  const Id p = proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Deal && x.deal.kind == DealKind::Tribute; });
  CHECK(p != 0);
  // Администратор принимает за плательщика.
  role::becomeAdmin(h.a());
  h->openSection("decisions");
  h.settle();
  CHECK(h.clickUi(pmark(p, "accept")));
  h.settle();
  const Id tribute = test::newestDeal(h->world());
  const Deal* d = h->world().deal(tribute);
  CHECK(d && d->kind == DealKind::Tribute && d->a == own && d->b == other && d->status == DealStatus::Active);
  // Плательщик (игрок) отменить не может: кнопок нет ни в окне сделок, ни во вкладке экономики; правило отказывает.
  role::becomePlayer(h.a(), other);
  h.settle();
  h->openEditor("trade");
  h.settle();
  CHECK(h->uiRect("trade.cancel." + std::to_string(tribute)) == nullptr);
  h->closeEditor();
  openTab(h, other, "faction.economy");
  CHECK(h->uiRect("tribute.cancel." + std::to_string(tribute)) == nullptr);
  CHECK(!h->play("Отменить", [&](Tx& tx) { rules::cancelDeal(tx, tribute, other); }));
  // Получатель (игрок) отменяет.
  role::becomePlayer(h.a(), own);
  h.settle();
  h->openEditor("trade");
  h.settle();
  CHECK(h.clickUi("trade.cancel." + std::to_string(tribute)));
  h.settle();
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->world().deal(tribute)->status == DealStatus::Cancelled);
  h->closeEditor();
  h.settle();

  // Сделка «каждый ход»: расторгает сторона — отношения «Расторжение сделки».
  Id per = 0;
  role::becomeAdmin(h.a());
  CHECK(h->act("Сделка", [&](Tx& tx) {
    Deal x;
    x.a = own;
    x.b = other;
    x.items.push_back(DealItem{DealSide::A, kGold, 1, DealMode::PerTurn, 5, 5});
    per = rules::concludeDeal(tx, x);
  }));
  const double rel0 = h->world().relation(own, other).v;
  const double pen = rules::constantOf(h->world(), schema::cst::DealBreak).num;
  role::becomePlayer(h.a(), other);
  h.settle();
  h->openEditor("trade");
  h.settle();
  CHECK(h.clickUi("trade.cancel." + std::to_string(per)));
  h.settle();
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(h->world().deal(per)->status == DealStatus::Cancelled);
  CHECK_NEAR(h->world().relation(own, other).v, std::clamp(rel0 + pen, -100.0, 100.0), 1e-9);
}

// Перемирие и союз у игрока — предложения; война — после подтверждения (отмена окна ничего не меняет).
TEST(app_proposals_truce_alliance_war) {
  Harness h("proposals_truce", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id held = findFaction(h->world(), "Северный союз Хельдвиг");
  const Id ketra = findFaction(h->world(), "Империя Валь-Кетра");
  const Id other = findFaction(h->world(), kOther);
  CHECK(held && ketra && other);
  CHECK(h->world().relation(held, ketra).s == RelStatus::War);
  role::becomePlayer(h.a(), held);
  h.settle();
  // Перемирие: окно с «Предложить перемирие»; война продолжается до ответа.
  openTab(h, held, "faction.diplomacy");
  CHECK(clickIn(h, "dip.truce." + std::to_string(ketra)));
  h.settle();
  CHECK(h->hasDialog("truce"));
  CHECK(h.clickUi("truce.ok"));
  h.settle();
  CHECK(!h->hasDialog("truce"));
  CHECK(h->world().relation(held, ketra).s == RelStatus::War);
  const Id truce = proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Truce; });
  CHECK(truce != 0);
  if (const Proposal* x = h->world().proposal(truce)) CHECK(x->from == held && x->to == ketra && x->truce.a == held && x->truce.b == ketra);
  // Песочные часы у строки: предложение ждёт ответа.
  openTab(h, held, "faction.diplomacy");
  CHECK(h->uiRect("dip.pending." + std::to_string(ketra)) != nullptr);
  // Союз — предложение.
  pickStatus(h, held, other, RelStatus::Alliance);
  CHECK(h->world().relation(held, other).s != RelStatus::Alliance);
  CHECK(proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Alliance && x.from == held && x.to == other; }) != 0);
  // Война: подтверждение; «Отмена» ничего не меняет.
  const World w0 = h->world();
  pickStatus(h, held, other, RelStatus::War);
  CHECK(h->hasDialog("dip.war"));
  shotClean(h, "proposals_war_confirm");
  CHECK(h.clickUi("dip.war.cancel"));
  h.settle();
  CHECK(World::diff(w0, h->world()) == 0);
  pickStatus(h, held, other, RelStatus::War);
  CHECK(h.clickUi("dip.war.ok"));
  h.settle();
  CHECK(h->world().relation(held, other).s == RelStatus::War);
  // Перемирие за Валь-Кетру принимает администратор.
  role::becomeAdmin(h.a());
  h->openSection("decisions");
  h.settle();
  CHECK(h.clickUi(pmark(truce, "accept")));
  h.settle();
  CHECK(h->world().relation(held, ketra).s == RelStatus::Neutral);
}

// Вопросы вассалитета государствам не за этим компьютером — предложения (окон нет); отмена объявления войны снимает и
// их. Принятый вопрос — вступление в войну и тот же разбор для нового участника (5.15).
TEST(app_proposals_vassal_questions) {
  Harness h("proposals_vassal", 1440, 1000);
  h.demo();
  h.dropToasts();
  const Id alm = findFaction(h->world(), kOwn);
  const Id olsta = findFaction(h->world(), kOther);
  const Id mirel = findFaction(h->world(), "Княжество Мирель");
  const Id solmar = findFaction(h->world(), "Теократия Солмар");
  CHECK(alm && olsta && mirel && solmar);
  CHECK(h->act("Вассалы", [&](Tx& tx) {
    rules::setSuzerain(tx, mirel, solmar);
    rules::setSuzerain(tx, olsta, alm);
  }));
  role::becomePlayer(h.a(), alm);
  h.settle();
  auto declare = [&] {
    pickStatus(h, alm, mirel, RelStatus::War);
    CHECK(h.clickUi("dip.war.ok"));
    h.settle();
    CHECK(h->world().relation(alm, mirel).s == RelStatus::War);
  };
  declare();
  CHECK(!h->hasDialog());
  const Id defend = proposalWhere(h->world(), [&](const Proposal& x) {
    return x.kind == ProposalKind::Defend && x.from == alm && x.to == solmar && x.other == mirel;
  });
  const Id call = proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Call && x.from == alm && x.to == olsta && x.other == mirel; });
  CHECK(defend != 0);
  CHECK(call != 0);
  // Отмена объявления войны снимает и вопросы.
  h->undo();
  h.settle();
  CHECK(h->world().relation(alm, mirel).s != RelStatus::War);
  CHECK(h->world().proposals.empty());
  declare();
  const Id defend2 = proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Defend && x.to == solmar; });
  const Id call2a = proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Call && x.to == olsta; });
  CHECK(defend2 != 0);
  CHECK(call2a != 0);
  // Вопрос о защите напавший не отзывает (видит без кнопок); свой призыв вассала — может отозвать.
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect("decisions.proposal." + std::to_string(defend2)) != nullptr);
  CHECK(h->uiRect(pmark(defend2, "withdraw")) == nullptr);
  CHECK(h->uiRect(pmark(call2a, "withdraw")) != nullptr);
  shotClean(h, "proposals_vassal_questions");
  // Солмар (игрок) вступается за вассала: война с Альмарином, затем — призыв вассала Альмарина на оборону (предложение).
  role::becomePlayer(h.a(), solmar);
  h.settle();
  h->openSection("decisions");
  h.settle();
  CHECK(h.clickUi(pmark(defend2, "accept")));
  h.settle();
  CHECK(h->world().relation(solmar, alm).s == RelStatus::War);
  CHECK(!h->hasDialog());
  const Id callDef = proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Call && x.from == alm && x.to == olsta && x.other == solmar; });
  CHECK(callDef != 0);
  // Ольсты: согласие воевать с Мирелью — сюзерен Мирели получает вопрос об Ольстах; отказ от второго призыва — −10.
  role::becomePlayer(h.a(), olsta);
  h.settle();
  h->openSection("decisions");
  h.settle();
  const Id call2 = proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Call && x.to == olsta && x.other == mirel; });
  CHECK(call2 != 0);
  CHECK(h.clickUi(pmark(call2, "accept")));
  h.settle();
  CHECK(h->world().relation(olsta, mirel).s == RelStatus::War);
  // Солмар уже воюет с Альмарином, но не с Ольстами: вопрос о защите Мирели от Ольст.
  CHECK(proposalWhere(h->world(), [&](const Proposal& x) { return x.kind == ProposalKind::Defend && x.from == olsta && x.to == solmar && x.other == mirel; }) != 0);
  const double rel0 = h->world().relation(alm, olsta).v;
  CHECK(h.clickUi(pmark(callDef, "decline")));
  h.settle();
  CHECK(h->world().relation(olsta, solmar).s != RelStatus::War);
  CHECK_NEAR(h->world().relation(alm, olsta).v, std::max(-100.0, rel0 + schema::kVassalCallRefuse), 1e-9);
}
