// Regnum — предложения другой стороне и ожидающие решения государств (rules/proposals.h): сделка, подарок от другой
// стороны, дань, перемирие, союз, защита вассала и призыв вассала; отказ, отзыв, очистка невозможных; расторжение сделок
// стороной (rules::cancelDeal с расторгающим государством).
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

Deal tradeDeal(Id a, Id b, Id res, double fromA, double fromB) {
  Deal d;
  d.a = a;
  d.b = b;
  d.items.push_back(DealItem{DealSide::A, kGold, fromA});
  d.items.push_back(DealItem{DealSide::B, res, fromB});
  return d;
}

Id ironOf(const World& w) { return test::idByName(w.catalogs->resources, "Железо"); }

}  // namespace

TEST(rules_proposals_deal_accept_and_decline) {
  Fix f;
  const Id iron = ironOf(f.w());
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[kGold] = 100;
    tx.faction(f.B).res[iron] = 50;
  });
  Proposal p;
  p.kind = ProposalKind::Deal;
  p.from = f.A;
  p.to = f.B;
  p.deal = tradeDeal(f.A, f.B, iron, 30, 20);
  Id id = 0;
  f.tx([&](Tx& tx) { id = propose(tx, p); });
  CHECK(f.w().proposal(id) != nullptr);
  CHECK(proposalsTo(f.w(), f.B) == std::vector<Id>{id});
  CHECK(proposalsFrom(f.w(), f.A) == std::vector<Id>{id});
  CHECK(has(lastLog(f.w()), "предлагает"));
  // Предложение ещё ничего не передало.
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 100, 1e-9);
  f.tx([&](Tx& tx) { acceptProposal(tx, id); });
  CHECK(f.w().proposal(id) == nullptr);
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 70, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(iron), 20, 1e-9);
  CHECK_NEAR(f.w().faction(f.B)->stock(iron), 30, 1e-9);
  // Отказ: ничего не передаётся, запись в хронике.
  f.tx([&](Tx& tx) { id = propose(tx, p); });
  f.tx([&](Tx& tx) { declineProposal(tx, id); });
  CHECK(f.w().proposal(id) == nullptr);
  CHECK(has(lastLog(f.w()), "отклоняет"));
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 70, 1e-9);
  // Нехватка ресурсов при принятии — отказ, предложение остаётся.
  Proposal big = p;
  big.deal = tradeDeal(f.A, f.B, iron, 30, 500);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { propose(tx, big); }); }), "Недостаточно"));
  f.tx([&](Tx& tx) { id = propose(tx, p); });
  f.tx([&](Tx& tx) { tx.faction(f.B).res[iron] = 0; });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { acceptProposal(tx, id); }); }), "Недостаточно"));
  CHECK(f.w().proposal(id) != nullptr);
  f.tx([&](Tx& tx) { withdrawProposal(tx, id); });
  CHECK(f.w().proposal(id) == nullptr);
}

TEST(rules_proposals_tribute_truce_alliance) {
  Fix f;
  // Дань: получатель A требует с B.
  Proposal t;
  t.kind = ProposalKind::Deal;
  t.from = f.A;
  t.to = f.B;
  t.deal.kind = DealKind::Tribute;
  t.deal.a = f.A;
  t.deal.b = f.B;
  t.deal.items.push_back(DealItem{DealSide::B, kGold, 5, DealMode::PerTurn, 4, 4});
  Id id = 0;
  f.tx([&](Tx& tx) { id = propose(tx, t); });
  CHECK(has(proposalTitle(f.w(), *f.w().proposal(id)), "Дань"));
  f.tx([&](Tx& tx) { acceptProposal(tx, id); });
  CHECK_NEAR(f.fc(f.A).incTribute, 5, 1e-9);
  // Перемирие: только при войне; принятие заключает его.
  Proposal tr;
  tr.kind = ProposalKind::Truce;
  tr.from = f.A;
  tr.to = f.C;
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { propose(tx, tr); }); }), "не в войне"));
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.C;
    declareWar(tx, f.A, f.C);
  });
  tr.truce.fromB.provinces = {f.p[0]};
  f.tx([&](Tx& tx) { id = propose(tx, tr); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { propose(tx, tr); }); }), "уже ждёт"));
  f.tx([&](Tx& tx) { acceptProposal(tx, id); });
  CHECK(f.w().relation(f.A, f.C).s == RelStatus::Neutral);
  CHECK_EQ(f.w().province(f.p[0])->owner, f.A);
  // Союз.
  Proposal al;
  al.kind = ProposalKind::Alliance;
  al.from = f.B;
  al.to = f.C;
  f.tx([&](Tx& tx) { id = propose(tx, al); });
  f.tx([&](Tx& tx) { acceptProposal(tx, id); });
  CHECK(f.w().relation(f.B, f.C).s == RelStatus::Alliance);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { propose(tx, al); }); }), "уже в союзе"));
}

TEST(rules_proposals_vassal_questions) {
  Fix f;
  // B — вассал C; A объявил войну B: сюзерен C решает, вступить ли.
  f.tx([&](Tx& tx) {
    setSuzerain(tx, f.B, f.C);
    declareWar(tx, f.A, f.B);
  });
  Proposal d;
  d.kind = ProposalKind::Defend;
  d.from = f.A;
  d.to = f.C;
  d.other = f.B;
  Id id = 0;
  f.tx([&](Tx& tx) { id = propose(tx, d); });
  CHECK(proposalsTo(f.w(), f.C) == std::vector<Id>{id});
  f.tx([&](Tx& tx) { acceptProposal(tx, id); });
  CHECK(f.w().relation(f.C, f.A).s == RelStatus::War);
  // Призыв вассала: C воюет с A, вассал B уже в войне с A — вопрос не задаётся; новый вассал — отказ с отношениями −10.
  Proposal c;
  c.kind = ProposalKind::Call;
  c.from = f.C;
  c.to = f.B;
  c.other = f.A;
  CHECK(has(join(proposalProblems(f.w(), c), "; "), "уже в войне"));
  Id v = 0;
  f.tx([&](Tx& tx) {
    v = createFaction(tx, FactionKind::State, "Вассал");
    setSuzerain(tx, v, f.C);
  });
  c.to = v;
  f.tx([&](Tx& tx) { id = propose(tx, c); });
  const double before = f.w().relation(f.C, v).v;
  f.tx([&](Tx& tx) { declineProposal(tx, id); });
  CHECK_NEAR(f.w().relation(f.C, v).v, before + schema::kVassalCallRefuse, 1e-9);
  CHECK(f.w().relation(v, f.A).s != RelStatus::War);
  // Вопрос теряет смысл (вассалитет снят) — fitProposals убирает его.
  f.tx([&](Tx& tx) { id = propose(tx, c); });
  f.tx([&](Tx& tx) { setSuzerain(tx, v, 0); });
  f.tx([&](Tx& tx) { fitProposals(tx); });
  CHECK(f.w().proposal(id) == nullptr);
}

TEST(rules_proposals_cleanup_on_faction_removal_and_war) {
  Fix f;
  const Id iron = ironOf(f.w());
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[kGold] = 100;
    tx.faction(f.B).res[iron] = 50;
  });
  Proposal p;
  p.kind = ProposalKind::Deal;
  p.from = f.A;
  p.to = f.B;
  p.deal = tradeDeal(f.A, f.B, iron, 10, 10);
  Id id = 0;
  f.tx([&](Tx& tx) { id = propose(tx, p); });
  // Война: сделка между воюющими невозможна — предложение снимается при очистке.
  f.tx([&](Tx& tx) {
    declareWar(tx, f.A, f.B);
    fitProposals(tx);
  });
  CHECK(f.w().proposal(id) == nullptr);
  Proposal al;
  al.kind = ProposalKind::Alliance;
  al.from = f.A;
  al.to = f.C;
  f.tx([&](Tx& tx) { id = propose(tx, al); });
  f.tx([&](Tx& tx) { removeFaction(tx, f.C); });
  CHECK(f.w().proposal(id) == nullptr);
}

TEST(rules_proposals_cancel_deal_by_side) {
  Fix f;
  const Id iron = ironOf(f.w());
  Id tribute = 0, trade = 0;
  f.tx([&](Tx& tx) {
    tribute = imposeTribute(tx, DealKind::Tribute, f.A, f.B, 5, 3);
    Deal d;
    d.a = f.A;
    d.b = f.B;
    d.items.push_back(DealItem{DealSide::A, iron, 1, DealMode::PerTurn, 5, 5});
    tx.faction(f.A).res[iron] = 100;
    trade = concludeDeal(tx, d);
  });
  // Дань отменяет только получатель; плательщик — отказ.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { cancelDeal(tx, tribute, f.B); }); }), "только получатель"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { cancelDeal(tx, tribute, f.C); }); }), "не сторона"));
  f.tx([&](Tx& tx) { cancelDeal(tx, tribute, f.A); });
  CHECK(f.w().deal(tribute)->status == DealStatus::Cancelled);
  // Сторона, расторгнувшая сделку, теряет доверие: отношения −10.
  const double before = f.w().relation(f.A, f.B).v;
  f.tx([&](Tx& tx) { cancelDeal(tx, trade, f.B); });
  CHECK(f.w().deal(trade)->status == DealStatus::Cancelled);
  CHECK_NEAR(f.w().relation(f.A, f.B).v, before - 10, 1e-9);
  CHECK(has(lastLog(f.w(), "Расторгнута"), "расторгло"));
}
