// Regnum — предложения и ожидающие решения государств (RULES.md §26).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

const Proposal& needProposal(const World& w, Id id) {
  const Proposal* p = w.proposal(id);
  if (!p) fail(id ? "Предложение не найдено" : "Не выбрано предложение");
  return *p;
}

bool sameParties(Id a, Id b, Id x, Id y) { return (a == x && b == y) || (a == y && b == x); }

// Условия, без которых предложение теряет смысл (их проверяет и fitProposals): стороны, война для перемирия, союз,
// вассалитет и война для вопросов вассалитета, сделка не между воюющими.
std::vector<std::string> structural(const World& w, const Proposal& p) {
  std::vector<std::string> out;
  const Faction* F = w.faction(p.from);
  const Faction* T = w.faction(p.to);
  if (!F || !T) {
    out.push_back("Сторона предложения не найдена");
    return out;
  }
  if (p.from == p.to) {
    out.push_back("Стороны предложения совпадают");
    return out;
  }
  if (int(p.kind) < 0 || p.kind >= ProposalKind::Count) {
    out.push_back("Неизвестный вид предложения");
    return out;
  }
  const RelStatus rel = w.relation(p.from, p.to).s;
  switch (p.kind) {
    case ProposalKind::Deal:
      if (!sameParties(p.deal.a, p.deal.b, p.from, p.to)) out.push_back("Стороны сделки — не стороны предложения");
      else if (rel == RelStatus::War) out.push_back(facName(w, p.from) + " и " + facName(w, p.to) + " в войне: сделки невозможны");
      break;
    case ProposalKind::Truce:
      if (!F->isState() || !T->isState()) out.push_back("Перемирие заключают государства");
      else if (!sameParties(p.truce.a, p.truce.b, p.from, p.to)) out.push_back("Стороны перемирия — не стороны предложения");
      else if (rel != RelStatus::War) out.push_back(facName(w, p.from) + " и " + facName(w, p.to) + " не в войне");
      break;
    case ProposalKind::Alliance:
      if (rel == RelStatus::War) out.push_back(facName(w, p.from) + " и " + facName(w, p.to) + " в войне: сначала перемирие");
      else if (rel == RelStatus::Alliance) out.push_back(facName(w, p.from) + " и " + facName(w, p.to) + " уже в союзе");
      break;
    case ProposalKind::Defend: {
      // to — сюзерен вассала other, которому объявил войну from.
      const Faction* V = w.faction(p.other);
      if (!F->isState() || !T->isState() || !V || !V->isState()) out.push_back("Вопрос вассалитета — между государствами");
      else if (V->suzerain != p.to) out.push_back(facName(w, p.other) + " — не вассал " + facName(w, p.to));
      else if (w.relation(p.from, p.other).s != RelStatus::War) out.push_back(facName(w, p.from) + " и " + facName(w, p.other) + " не в войне");
      else if (rel == RelStatus::War) out.push_back(facName(w, p.to) + " уже в войне с " + facName(w, p.from));
      break;
    }
    case ProposalKind::Call: {
      // to — вассал сюзерена from, который воюет с other.
      const Faction* E = w.faction(p.other);
      if (!F->isState() || !T->isState() || !E) out.push_back("Вопрос вассалитета — между государствами");
      else if (T->suzerain != p.from) out.push_back(facName(w, p.to) + " — не вассал " + facName(w, p.from));
      else if (w.relation(p.from, p.other).s != RelStatus::War) out.push_back(facName(w, p.from) + " и " + facName(w, p.other) + " не в войне");
      else if (p.other == p.to || w.relation(p.to, p.other).s == RelStatus::War)
        out.push_back(facName(w, p.to) + " уже в войне с " + facName(w, p.other));
      break;
    }
    case ProposalKind::Count: break;
  }
  return out;
}

std::string dealTitle(const World& w, const Deal& d) {
  if (d.kind == DealKind::Tribute) return "Дань: " + facName(w, d.b) + " платит " + facName(w, d.a);
  if (d.kind == DealKind::Reparations) return "Репарации: " + facName(w, d.b) + " платит " + facName(w, d.a);
  bool fromA = false, fromB = false;
  for (const DealItem& it : d.items) (it.from == DealSide::A ? fromA : fromB) = true;
  if (fromA && !fromB) return "Подарок: " + facName(w, d.a) + " → " + facName(w, d.b);
  if (fromB && !fromA) return "Подарок: " + facName(w, d.b) + " → " + facName(w, d.a);
  return "Сделка: " + facName(w, d.a) + " и " + facName(w, d.b);
}

}  // namespace

std::string proposalTitle(const World& w, const Proposal& p) {
  switch (p.kind) {
    case ProposalKind::Deal: return dealTitle(w, p.deal);
    case ProposalKind::Truce: return "Перемирие: " + facName(w, p.from) + " и " + facName(w, p.to);
    case ProposalKind::Alliance: return "Союз: " + facName(w, p.from) + " и " + facName(w, p.to);
    case ProposalKind::Defend: return "Защита вассала: " + facName(w, p.from) + " объявил войну " + facName(w, p.other);
    case ProposalKind::Call: return "Призыв вассала: " + facName(w, p.from) + " против " + facName(w, p.other);
    case ProposalKind::Count: break;
  }
  return "Предложение";
}

std::vector<std::string> proposalProblems(const World& w, const Proposal& p) {
  std::vector<std::string> out = structural(w, p);
  if (!out.empty()) return out;
  auto add = [&](const std::vector<std::string>& v) {
    for (const std::string& s : v)
      if (!contains(out, s)) out.push_back(s);
  };
  switch (p.kind) {
    case ProposalKind::Deal: add(validateDeal(w, p.deal).problems); break;
    case ProposalKind::Truce: add(truceProblems(w, p.truce)); break;
    default: break;
  }
  // Повторное предложение того же вида тем же сторонам, ещё не отвеченное (кроме сделок).
  if (p.kind != ProposalKind::Deal && !p.id) {
    const Id dup = w.proposals.firstId([&](const Proposal& x) {
      return x.kind == p.kind && x.from == p.from && x.to == p.to && x.other == p.other;
    });
    if (dup) out.push_back("Такое предложение уже ждёт ответа");
  }
  return out;
}

Id propose(Tx& tx, Proposal p) {
  p.id = 0;
  if (p.kind == ProposalKind::Truce) {
    p.truce.a = p.from;
    p.truce.b = p.to;
  }
  const std::vector<std::string> why = proposalProblems(tx.w(), p);
  if (!why.empty()) fail(why.front());
  p.turn = tx.w().turn();
  const std::string title = proposalTitle(tx.w(), p);
  const Id from = p.from, to = p.to, other = p.other;
  const ProposalKind kind = p.kind;
  const Id id = tx.add(std::move(p)).id;
  if (kind == ProposalKind::Defend || kind == ProposalKind::Call)
    addLog(tx, LogKind::Diplomacy, facName(tx.w(), to) + " должно решить: " + title, LogRefs{0, 0, {to, from, other}});
  else
    addLog(tx, LogKind::Diplomacy, facName(tx.w(), from) + " предлагает " + facName(tx.w(), to) + ": " + title, LogRefs{0, 0, {from, to}});
  return id;
}

void acceptProposal(Tx& tx, Id proposal) {
  const Proposal p = needProposal(tx.w(), proposal);
  const std::vector<std::string> why = proposalProblems(tx.w(), p);
  if (!why.empty()) fail(why.front());
  tx.eraseProposal(proposal);
  switch (p.kind) {
    case ProposalKind::Deal: {
      const Deal& d = p.deal;
      if (d.kind == DealKind::Trade) {
        concludeDeal(tx, d);
      } else {
        const DealItem& it = d.items.at(0);
        imposeTribute(tx, d.kind, d.a, d.b, it.amount, it.turns);
      }
      break;
    }
    case ProposalKind::Truce: concludeTruce(tx, p.truce); break;
    case ProposalKind::Alliance: {
      const Relation r = tx.w().relation(p.from, p.to);
      setRelation(tx, p.from, p.to, r.v, RelStatus::Alliance);
      addLog(tx, LogKind::Diplomacy, "Союз " + facName(tx.w(), p.from) + " и " + facName(tx.w(), p.to), LogRefs{0, 0, {p.from, p.to}});
      break;
    }
    case ProposalKind::Defend: suzerainDefends(tx, p.to, p.other, p.from, true); break;
    case ProposalKind::Call: vassalAnswers(tx, p.from, p.to, p.other, true); break;
    case ProposalKind::Count: break;
  }
  fitProposals(tx);
}

void declineProposal(Tx& tx, Id proposal) {
  const Proposal p = needProposal(tx.w(), proposal);
  tx.eraseProposal(proposal);
  if (!structural(tx.w(), p).empty()) return;   // уже невозможное предложение снимается без последствий
  switch (p.kind) {
    case ProposalKind::Defend: suzerainDefends(tx, p.to, p.other, p.from, false); break;
    case ProposalKind::Call: vassalAnswers(tx, p.from, p.to, p.other, false); break;
    default:
      addLog(tx, LogKind::Diplomacy, facName(tx.w(), p.to) + " отклоняет предложение " + facName(tx.w(), p.from) + ": " + proposalTitle(tx.w(), p),
             LogRefs{0, 0, {p.from, p.to}});
      break;
  }
}

void withdrawProposal(Tx& tx, Id proposal) {
  needProposal(tx.w(), proposal);
  tx.eraseProposal(proposal);
}

std::vector<Id> proposalsTo(const World& w, Id state) {
  return idsWhere(w.proposals, [&](const Proposal& p) { return p.to == state; });
}

std::vector<Id> proposalsFrom(const World& w, Id state) {
  return idsWhere(w.proposals, [&](const Proposal& p) { return p.from == state; });
}

void fitProposals(Tx& tx) {
  if (tx.w().proposals.empty()) return;
  for (Id id : idsWhere(tx.w().proposals, [&](const Proposal& p) { return !structural(tx.w(), p).empty(); })) tx.eraseProposal(id);
}

}  // namespace rg::rules
