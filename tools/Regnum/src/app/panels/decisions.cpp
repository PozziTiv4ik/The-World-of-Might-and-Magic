// Regnum — раздел «Решения» правой ленты (ТЗ «Доработки №2», п.1 и п.6; аудит 2026-10-23, 1.1): предложения
// государств (rules/proposals.h) — у игрока входящие его государства («Принять», «Отклонить») и исходящие
// («Отозвать»), у администратора — все (решает за любую сторону); у администратора ещё готовность игроков завершить ход
// (ТЗ «Доработки №1», п.1), приглашения героев в заговоры — решение администратора (герой согласился — участник
// заговора, отказался — «Угрозы заговору»), заказные убийства на конец хода с вероятностью и «Защитить от убийства»;
// блоки хода — decisions_turn.cpp. Принятый вопрос вассалитета (сюзерен или вассал вступает в войну) запускает разбор
// вассалитета для нового участника войны (flow::afterWarDeclared). Число на значке ленты — ожидающие решения текущей
// роли: у игрока — входящие предложения, у администратора — все предложения, приглашения и незащищённые заказы.
#include "app/panels/decisions.h"

#include "app/editors/trade.h"
#include "app/flows.h"
#include "app/shell/role.h"
#include "app/turn/turn_ui.h"
#include "app/widgets.h"

namespace rg::app {
namespace {

struct InviteRow {
  Id plot = 0, hero = 0;
};
struct HitRow {
  Id plot = 0, hero = 0;
  double gold = 0;
  bool guarded = false;
  bool reviewed = false;   // администратор рассмотрел заказ — решения не ждёт
};

std::vector<InviteRow> invites(const World& w) {
  std::vector<InviteRow> out;
  w.plots.each([&](const Plot& p) {
    for (Id c : p.invited) out.push_back({p.id, c});
  });
  return out;
}

std::vector<HitRow> hits(const World& w) {
  std::vector<HitRow> out;
  w.plots.each([&](const Plot& p) {
    for (const PlotHit& h : p.hits) out.push_back({p.id, h.character, h.gold, h.guarded, h.reviewed});
  });
  return out;
}

// Предложения, которые видит роль, по возрастанию ID: игрок — входящие (incoming) или исходящие своего государства,
// администратор — все (incoming не важен).
std::vector<const Proposal*> proposalsOf(App& a, const World& w, bool incoming) {
  std::vector<const Proposal*> out;
  const Id own = a.playerState();
  w.proposals.each([&](const Proposal& p) {
    if (a.admin() || (incoming ? p.to == own : p.from == own)) out.push_back(&p);
  });
  std::sort(out.begin(), out.end(), [](const Proposal* x, const Proposal* y) { return x->id < y->id; });
  return out;
}

// Ожидают решения: у игрока — входящие предложения его государства; у администратора — все предложения, приглашения в
// заговоры и заказные убийства без защиты и отметки «Рассмотрено»; у обоих — решения хода.
int pending(App& a) {
  const World& w = a.world();
  int n = int(proposalsOf(a, w, true).size()) + decisions::turnPending(a);
  if (!a.admin()) return n;
  n += int(invites(w).size());
  for (const HitRow& h : hits(w))
    if (!h.guarded && !h.reviewed) n++;
  return n;
}

// Ячейка героя: портрет, имя, флаг и название его государства.
void heroCell(const World& w, Id hero, RectF cell) {
  const ui::Theme& th = ui::theme();
  const Character* c = w.character(hero);
  const float av = 34;
  ui::at(RectF{cell.x, std::round(cell.cy() - av * 0.5f), av, av});
  if (c) w::heroAvatar(*c, av);
  else ui::next(av, av);
  const float tx = cell.x + av + 10, tw = std::max(0.f, cell.right() - tx);
  const std::string name = c ? w::orName(c->name, "Без имени") : std::string("Герой");
  const Faction* f = c ? w.faction(c->faction) : nullptr;
  const float top = std::round(cell.cy() - 19);
  ui::draw::text(name, RectF{tx, top, tw, 20}, ui::Font::Strong, th.text);
  std::string tip = name;
  if (f) {
    ui::at(RectF{tx, top + 24, 18, 12});
    ui::flag(f->flag, 18, 12, 2);
    ui::draw::text(w::orName(f->name), RectF{tx + 24, top + 21, std::max(0.f, tw - 24), 18}, ui::Font::Small, th.textDim);
    tip += "\n" + w::orName(f->name);
  }
  ui::hoverTip("##hero", RectF{tx, cell.y, tw, cell.h}, tip);
}

// Ячейка заговора: название и заказчик (флаг и название; внутренний заговор — без заказчика).
void plotCell(const World& w, Id plot, RectF cell) {
  const ui::Theme& th = ui::theme();
  const Plot* p = w.plot(plot);
  if (!p) return;
  const std::string title = rules::plotTitle(w, *p);
  const Faction* o = p->external() ? w.faction(p->owner) : nullptr;
  const float top = std::round(cell.cy() - 19);
  ui::draw::text(title, RectF{cell.x, top, cell.w, 20}, ui::Font::Body, th.text);
  std::string tip = title;
  if (o) {
    ui::at(RectF{cell.x, top + 24, 18, 12});
    ui::flag(o->flag, 18, 12, 2);
    ui::draw::text(w::orName(o->name), RectF{cell.x + 24, top + 21, std::max(0.f, cell.w - 24), 18}, ui::Font::Small, th.textDim);
    tip += "\nЗаказчик: " + w::orName(o->name);
  } else {
    ui::draw::text("Без заказчика", RectF{cell.x, top + 21, cell.w, 18}, ui::Font::Small, th.textMuted);
  }
  ui::hoverTip("##plot", cell, tip);
}

// Разбор вероятности заказа для подсказки.
std::string chanceText(const rules::HitChance& c, double gold) {
  std::vector<std::string> lines;
  lines.push_back("Вероятность успеха: " + fmtPct(c.total));
  if (c.guarded) {
    lines.push_back("Защищён от убийства");
    return join(lines, "\n");
  }
  lines.push_back("Золото " + fmtGold(gold) + ": " + fmtPct(c.base, 1));
  if (std::fabs(c.intel) > 1e-9) lines.push_back("Разведка заказчика: " + fmtPct(c.intel, 1, true));
  if (std::fabs(c.guard) > 1e-9) lines.push_back("Защита государства героя: " + fmtPct(c.guard, 1, true));
  if (std::fabs(c.hero) > 1e-9) lines.push_back("Герой: " + fmtPct(c.hero, 1, true));
  return join(lines, "\n");
}

// ---------------------------------------------------------------- предложения
const char* proposalIcon(const Proposal& p) {
  switch (p.kind) {
    case ProposalKind::Deal: return trade::dealIcon(p.deal.kind);
    case ProposalKind::Truce: return "handshake";
    case ProposalKind::Alliance: return "alliance";
    case ProposalKind::Defend: return "shield";
    case ProposalKind::Call: return "banner";
    case ProposalKind::Count: break;
  }
  return "gavel";
}

ui::Tone proposalTone(const Proposal& p) {
  switch (p.kind) {
    case ProposalKind::Deal: return p.deal.kind == DealKind::Trade ? ui::Tone::Accent : ui::Tone::Warning;
    case ProposalKind::Alliance:
    case ProposalKind::Truce: return ui::Tone::Success;
    default: return ui::Tone::Danger;
  }
}

// Позиция сделки строкой: «Золото 100 тыс. разово», «провинция Ясень», «пленный «Имя»».
std::string itemText(const World& w, const DealItem& it) {
  if (it.kind == DealItemKind::Province) return "провинция " + w.provinceName(it.ref);
  if (it.kind == DealItemKind::Hero) return "пленный «" + w.characterName(it.ref) + "»";
  return trade::resName(w, it.res) + " " + trade::amountText(it.res, it.amount) +
         (it.mode == DealMode::PerTurn ? " за ход, " + nTurns(it.turns) : std::string(" разово"));
}

// Условия перемирия, которые отдаёт сторона: провинции, репарации, выплаты, рабы, пленные, вассалитет.
std::string termsText(const World& w, const TruceTerms& t) {
  std::vector<std::string> parts;
  for (Id pid : t.provinces) parts.push_back("провинция " + w.provinceName(pid));
  if (t.reparations > 0) parts.push_back("репарации " + fmtGold(t.reparations) + " за ход, " + nTurns(std::max(1, t.reparationsTurns)));
  for (auto& [res, v] : t.resources)
    if (v > 0) parts.push_back(trade::resName(w, res) + " " + trade::amountText(res, v));
  if (t.slaves > 0) parts.push_back("рабы " + fmtInt(t.slaves));
  for (Id h : t.heroes) parts.push_back("пленный «" + w.characterName(h) + "»");
  if (t.vassal) parts.push_back("вассалитет");
  return join(parts, "; ");
}

// Что предлагается: строки «Сторона: что отдаёт» (сделка и перемирие), выплаты дани.
std::vector<std::pair<Id, std::string>> proposalLines(const World& w, const Proposal& p) {
  std::vector<std::pair<Id, std::string>> out;
  if (p.kind == ProposalKind::Deal) {
    const Deal& d = p.deal;
    if (d.kind != DealKind::Trade) {
      for (const DealItem& it : d.items)
        out.push_back({d.b, trade::amountText(it.res, it.amount) + " за ход, " + nTurns(std::max(1, it.turns))});
      return out;
    }
    for (int sd = 0; sd < 2; sd++) {
      std::vector<std::string> parts;
      for (const DealItem& it : d.items)
        if (int(it.from) == sd) parts.push_back(itemText(w, it));
      if (!parts.empty()) out.push_back({sd == 0 ? d.a : d.b, join(parts, "; ")});
    }
  } else if (p.kind == ProposalKind::Truce) {
    const std::string a = termsText(w, p.truce.fromA), b = termsText(w, p.truce.fromB);
    if (!a.empty()) out.push_back({p.truce.a, a});
    if (!b.empty()) out.push_back({p.truce.b, b});
  }
  return out;
}

struct Pick {
  enum Kind { None, Accept, Decline, Withdraw } kind = None;
  Id id = 0;
};

// Карточка предложения: вид и название, стороны, условия, причина невозможности; справа — решения роли.
void proposalCard(App& a, const World& w, const Proposal& p, Pick& pick) {
  ui::IdScope s{i64(p.id)};
  const std::string mark = "decisions.proposal." + std::to_string(p.id);
  const bool admin = a.admin();
  const bool answer = admin || (a.canPlay(p.to) && p.to == a.playerState());
  // Вопрос сюзерену о защите вассала задала сама война: напавший его не отзывает.
  const bool withdraw = admin || (a.canPlay(p.from) && p.from == a.playerState() && p.kind != ProposalKind::Defend);
  std::vector<std::string> problems = rules::proposalProblems(w, p);
  if (!admin)   // туман войны: без чужих запасов в причинах
    for (std::string& x : problems) x = w::fogText(a, w, x, p.from == a.playerState() ? p.to : p.from);
  {
    ui::Card c({.pad = 12, .tone = proposalTone(p)});
    {
      const int btns = (answer ? 2 : 0) + (withdraw && !answer ? 1 : 0) + (admin ? 1 : 0);
      std::vector<ui::Len> cols = {ui::px(28), ui::fr(1)};
      if (btns) cols.push_back(ui::px(answer ? (admin ? 252.f : 220.f) : 104.f));
      ui::Row row(std::span<const ui::Len>(cols), ui::kAuto, 10);
      {
        const RectF ic = ui::next(28, 34);
        turnui::iconTile(RectF{ic.x, ic.cy() - 14, 28, 28}, proposalIcon(p), proposalTone(p));
      }
      {
        ui::Group g(0, 0);
        ui::label(rules::proposalTitle(w, p), {.font = ui::Font::Strong});
        ui::label("ход " + std::to_string(p.turn), {.font = ui::Font::Caption, .ink = ui::Ink::Muted, .icon = "clock"});
      }
      if (btns) {
        ui::HStack hs(30, ui::Align::Right, 6);
        if (answer) {
          const std::string why = join(problems, "\n");
          if (ui::button("Принять", {.variant = ui::Variant::Primary, .icon = "check", .size = ui::Size::Small, .disabled = !problems.empty(),
                                     .tooltip = why}))
            pick = {Pick::Accept, p.id};
          a.markUi(mark + ".accept");
          if (ui::button("Отклонить", {.icon = "close", .size = ui::Size::Small})) pick = {Pick::Decline, p.id};
          a.markUi(mark + ".decline");
        }
        if (withdraw && (!answer || admin)) {
          if (admin) {
            if (ui::iconButton("trash", "Отозвать", {.size = ui::Size::Small, .tone = ui::Tone::Danger})) pick = {Pick::Withdraw, p.id};
          } else if (ui::button("Отозвать", {.icon = "undo", .size = ui::Size::Small})) {
            pick = {Pick::Withdraw, p.id};
          }
          a.markUi(mark + ".withdraw");
        }
      }
    }
    {
      ui::HStack hs(26, ui::Align::Left, 6);
      w::factionChip(p.from);
      ui::icon("arrow-right", ui::Ink::Muted, 14);
      w::factionChip(p.to);
      if (p.other && (p.kind == ProposalKind::Defend || p.kind == ProposalKind::Call)) {
        ui::icon("war", ui::Ink::Danger, 14);
        w::factionChip(p.other);
      }
    }
    for (const auto& [side, text] : proposalLines(w, p)) {
      ui::IdScope ls{i64(side)};
      ui::label(w.factionName(side) + ": " + text, {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "export", .wrap = true});
    }
    if (p.kind == ProposalKind::Truce)
      ui::label(std::string("После: ") + utf8::lower(schema::relStatus(p.truce.status).name),
                {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = schema::relStatus(p.truce.status).icon});
    if (!problems.empty()) ui::label(problems.front(), {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
  }
  a.markUi(mark, ui::lastItem().rect);
}

// Решение по предложению. Принятый вопрос вассалитета: вступивший в войну — разбор вассалитета для него (5.15).
void decide(App& a, const Pick& pick) {
  const Proposal* cur = a.world().proposal(pick.id);
  if (!cur) return;
  const Proposal p = *cur;
  const std::string title = rules::proposalTitle(a.world(), p);
  switch (pick.kind) {
    case Pick::Accept: {
      if (!a.play("Принять: " + title, [&](Tx& tx) { rules::acceptProposal(tx, p.id); })) return;
      a.toast("Принято: " + title, ToastKind::Success, proposalIcon(p));
      const World& w = a.world();
      if (p.kind == ProposalKind::Defend && w.relation(p.to, p.from).s == RelStatus::War) flow::afterWarDeclared(a, p.to, p.from);
      if (p.kind == ProposalKind::Call && w.relation(p.to, p.other).s == RelStatus::War) flow::afterWarDeclared(a, p.to, p.other);
      break;
    }
    case Pick::Decline:
      if (a.play("Отклонить: " + title, [&](Tx& tx) { rules::declineProposal(tx, p.id); }))
        a.toast("Отклонено: " + title, ToastKind::Info, "close");
      break;
    case Pick::Withdraw:
      if (a.play("Отозвать: " + title, [&](Tx& tx) { rules::withdrawProposal(tx, p.id); }))
        a.toast("Отозвано: " + title, ToastKind::Info, "undo");
      break;
    case Pick::None: break;
  }
}

// Раздел предложений: карточки или строка «нет».
void proposalBlock(App& a, const World& w, const char* title, const char* icon, bool incoming, const char* mark, const char* none) {
  const std::vector<const Proposal*> list = proposalsOf(a, w, incoming);
  const std::string badge = list.empty() ? std::string() : std::to_string(list.size());
  ui::Section s(title, icon, {.badge = badge});
  a.markUi(mark);
  if (!s) return;
  if (list.empty()) {
    ui::label(none, {.ink = ui::Ink::Muted});
    return;
  }
  Pick pick;
  for (const Proposal* p : list) proposalCard(a, w, *p, pick);
  if (pick.kind != Pick::None) decide(a, pick);
}

// ---------------------------------------------------------------- блоки администратора
void readyBlock(App& a, const World& w) {
  const int n = turnui::readyCount(w);
  const std::string badge = n > 0 ? std::to_string(n) : std::string();
  ui::Section s("Готовность игроков", "check", {.badge = badge});
  a.markUi("decisions.ready");
  if (!s) return;
  if (turnui::readiness(w).empty()) {
    ui::label("Отметок готовности нет", {.ink = ui::Ink::Muted});
    return;
  }
  turnui::readinessTable(a, w, "ready", 0, "decisions.ready");
}

void inviteBlock(App& a, const World& w) {
  const std::vector<InviteRow> rows = invites(w);
  const std::string badge = rows.empty() ? std::string() : std::to_string(rows.size());
  ui::Section s("Приглашения в заговоры", "user-plus", {.badge = badge});
  a.markUi("decisions.invites");
  if (!s) return;
  if (rows.empty()) {
    ui::label("Приглашений нет", {.ink = ui::Ink::Muted});
    return;
  }
  ui::Column cols[] = {{"Герой", nullptr, ui::fr(1, 140)}, {"Заговор", nullptr, ui::fr(1, 140)}, {"", nullptr, ui::px(256), ui::Align::Right}};
  ui::Table t("invites", cols, int(rows.size()), {.rowHeight = 54, .selectable = false});
  struct Pick {
    Id plot = 0, hero = 0;
    bool agree = false;
  } pick;
  for (int i : t) {
    const InviteRow& r = rows[size_t(i)];
    const std::string mark = "decisions.invite." + std::to_string(r.plot) + "." + std::to_string(r.hero);
    a.markUi(mark, t.rowRect());
    heroCell(w, r.hero, turnui::cellRect(t));
    plotCell(w, r.plot, turnui::cellRect(t));
    t.cell();
    ui::HStack hs(30, ui::Align::Right, 6);
    ui::Disabled dis(!a.canEdit());
    if (ui::button("Согласился", {.icon = "check", .tooltip = "Этот герой согласился: участник заговора"})) pick = {r.plot, r.hero, true};
    a.markUi(mark + ".yes");
    if (ui::button("Отказался", {.icon = "close", .tooltip = "Этот герой отказался: угроза заговору"})) pick = {r.plot, r.hero, false};
    a.markUi(mark + ".no");
  }
  if (int d = t.doubleClicked(); d >= 0 && d < int(rows.size())) a.select(SelType::Character, rows[size_t(d)].hero);
  if (pick.hero)
    a.act(pick.agree ? "Герой согласился на заговор" : "Герой отказался от заговора",
          [&](Tx& tx) { rules::answerPlotInvite(tx, pick.plot, pick.hero, pick.agree); });
}

void hitBlock(App& a, const World& w) {
  const std::vector<HitRow> rows = hits(w);
  const std::string badge = rows.empty() ? std::string() : std::to_string(rows.size());
  ui::Section s("Заказные убийства на следующий ход", "skull", {.badge = badge});
  a.markUi("decisions.hits");
  if (!s) return;
  if (rows.empty()) {
    ui::label("Заказов нет", {.ink = ui::Ink::Muted});
    return;
  }
  ui::Column cols[] = {{"Герой", nullptr, ui::fr(1.15f, 150)},
                       {"Заговор", nullptr, ui::fr(1, 130)},
                       {"", "coins", ui::px(72), ui::Align::Right, false, "Золото на операцию"},
                       {"", "dice", ui::px(56), ui::Align::Right, false, "Вероятность успеха"},
                       {"", nullptr, ui::px(162), ui::Align::Right}};
  ui::Table t("hits", cols, int(rows.size()), {.rowHeight = 54, .selectable = false});
  struct Pick {
    Id plot = 0, hero = 0;
    bool on = false;
  } pick, review;
  for (int i : t) {
    const HitRow& r = rows[size_t(i)];
    const std::string mark = "decisions.hit." + std::to_string(r.plot) + "." + std::to_string(r.hero);
    a.markUi(mark, t.rowRect());
    heroCell(w, r.hero, turnui::cellRect(t));
    plotCell(w, r.plot, turnui::cellRect(t));
    t.text(fmtGoldShort(r.gold), ui::Ink::Dim);
    const rules::HitChance c = rules::hitChance(w, r.plot, r.hero, r.gold);
    t.cell();
    ui::label(fmtPct(c.total), {.font = r.guarded ? ui::Font::Body : ui::Font::Strong,
                                .ink = r.guarded ? ui::Ink::Muted : c.total >= 50 ? ui::Ink::Danger : ui::Ink::Normal,
                                .align = ui::Align::Right, .tooltip = chanceText(c, r.gold)});
    a.markUi(mark + ".chance");
    t.cell();
    ui::Disabled dis(!a.canEdit());
    ui::HStack hs(30, ui::Align::Right, 6);
    bool click = false;
    if (r.guarded) click = turnui::statePill("guard", "Защищён", "shield", "Защищён от убийства · снять защиту");
    else click = ui::button("Защитить", {.icon = "shield", .tooltip = "Защитить от убийства: вероятность 0 %"});
    a.markUi(mark + ".guard");
    if (click) pick = {r.plot, r.hero, !r.guarded};
    // «Рассмотрено» (аудит 2026-10-23, 2.8): исход не меняется, заказ больше не ждёт решения.
    bool seen = r.reviewed;
    if (ui::iconToggle("check-circle", seen ? "Рассмотрено · снять отметку" : "Рассмотрено", seen)) review = {r.plot, r.hero, seen};
    a.markUi(mark + ".reviewed");
  }
  if (int d = t.doubleClicked(); d >= 0 && d < int(rows.size())) a.select(SelType::Character, rows[size_t(d)].hero);
  if (pick.hero)
    a.act(pick.on ? "Защитить от убийства" : "Снять защиту от убийства", [&](Tx& tx) { rules::guardHit(tx, pick.plot, pick.hero, pick.on); });
  if (review.hero)
    a.act(review.on ? "Заказ рассмотрен" : "Снять отметку «Рассмотрено»", [&](Tx& tx) { rules::setHitReviewed(tx, review.plot, review.hero, review.on); });
}

// Страница раздела. Игрок: решения хода, входящие и исходящие предложения своего государства. Администратор:
// готовность, решения хода, все предложения, приглашения, заказы (пустой блок — одной строкой).
void drawDecisions(App& a) {
  const World& w = w::frameWorld(a);
  if (!a.admin()) {
    decisions::turnBlocks(a, w);
    proposalBlock(a, w, "Входящие предложения", "import", true, "decisions.incoming", "Предложений нет");
    proposalBlock(a, w, "Наши предложения", "export", false, "decisions.outgoing", "Предложений нет");
    return;
  }
  readyBlock(a, w);
  decisions::turnBlocks(a, w);
  proposalBlock(a, w, "Предложения", "handshake", true, "decisions.proposals", "Предложений нет");
  inviteBlock(a, w);
  hitBlock(a, w);
}

SectionReg section({.id = "decisions", .icon = "gavel", .title = "Решения", .order = 65, .shortcut = "Ctrl+0", .directory = drawDecisions,
                    .allTitle = "Ждут решения", .badge = pending});

}  // namespace
}  // namespace rg::app
