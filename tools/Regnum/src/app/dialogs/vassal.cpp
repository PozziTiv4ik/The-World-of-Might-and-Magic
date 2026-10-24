// Regnum — вассалитет после объявления войны (ТЗ «Механика вассалитета»; аудит 2026-10-23, 1.1 и 5.15): вопросы по
// очереди.
//  1. Атакован вассал, его сюзерен не зачинщик и ещё не в войне с ним — «Государство «S» вступает в войну на стороне
//     вассала «T»?» Да/Нет (rules::suzerainDefends: война и +15 к отношениям с вассалом или −20). Отвечает сюзерен.
//  2. У зачинщика есть вассалы — призыв против атакованного: каждый вассал отвечает сам «согласился» или «отказался»
//     (rules::vassalAnswers: ±10, согласные вступают в войну). Сюзерен атакованного, если он вассал зачинщика, в призыв
//     не попадает: он отвечает на вопрос 1 (противоречивых призывов нет).
//  3. У атакованного есть вассалы — их призыв на оборону против зачинщика (то же окно).
// Отвечает государство, которое можно вести в этом сеансе (App::canPlay: администратор — любое, игрок — своё). Вопрос
// государству, которого нет за этим компьютером, становится предложением в разделе «Решения» (rules::propose: Defend,
// Call) — окно не показывается, вопрос не теряется. Каждое вступление в войну (сюзерен за вассала или согласившийся
// вассал) запускает тот же разбор для нового участника войны. Тому, кто не может объявить войну (срок мира после
// перемирия, вассал против своего сюзерена — rules::canDeclareWar), вопрос не задаётся. Закрытие окна без ответа ничего
// не меняет.
#include "app/flows.h"
#include "app/panels/faction_common.h"

namespace rg::app {

namespace {

using namespace fac;

// Знак между флагами: значок в цветном круге.
void bigIcon(const char* icon, ui::Tone tone) {
  RectF ic = ui::next(64, 78);
  RectF b{ic.cx() - 22, ic.y + 4, 44, 44};
  ui::draw::rect(b, ui::toneColor(tone).alpha(0.16f), 22);
  ui::draw::icon(icon, b.inset(10), ui::toneColor(tone));
}

// Вопросы государствам не за этим компьютером — предложения. Без отдельного шага отмены: отмена объявления войны
// снимает и их. Уже заданный вопрос (такое же предложение ждёт ответа) не повторяется.
void proposeAll(App& a, std::vector<Proposal> list) {
  if (list.empty()) return;
  a.play("Вопросы вассалитета", [&](Tx& tx) {
    for (Proposal& p : list)
      if (rules::proposalProblems(tx.w(), p).empty()) rules::propose(tx, p);
  }, {.history = false});
}

Proposal question(ProposalKind kind, Id from, Id to, Id other) {
  Proposal p;
  p.kind = kind;
  p.from = from;
  p.to = to;
  p.other = other;
  return p;
}

// Вступившие в войну по очереди (зачинщик, цель): для каждого — тот же разбор вассалитета, затем done.
void chain(App& a, std::vector<std::pair<Id, Id>> wars, std::function<void(App&)> done) {
  if (wars.empty()) {
    if (done) done(a);
    return;
  }
  const std::pair<Id, Id> war = wars.front();
  wars.erase(wars.begin());
  flow::afterWarDeclared(a, war.first, war.second, [wars, done](App& x) { chain(x, wars, done); });
}

// Вопрос сюзерену атакованного вассала.
struct DefendDlg final : Dialog {
  Id suzerain = 0, vassal = 0, attacker = 0;
  std::function<void(App&)> next;
  bool decided = false;

  const char* id() const override { return "vassal.defend"; }
  Style style(App&) override { return {"Сюзерен и вассал", "banner", ui::Tone::Accent, 520}; }

  // joined — сюзерен вступил в войну: сначала разбор вассалитета для него, затем дальше по очереди.
  void finish(App& a, bool joined) {
    if (decided) return;
    decided = true;
    std::function<void(App&)> fn = next;
    if (joined) {
      const Id s = suzerain, at = attacker;
      detail::later(a, [s, at, fn](App& x) { chain(x, {{s, at}}, fn); });
    } else if (fn) {
      detail::later(a, fn);
    }
  }
  void dismissed(App& a) override { finish(a, false); }

  bool draw(App& a) override {
    const World& w = frameWorld(a);
    if (!w.faction(suzerain) || !w.faction(vassal) || !w.faction(attacker)) {
      finish(a, false);
      return false;
    }
    {
      ui::Row r({ui::fr(1), ui::px(64), ui::fr(1)}, ui::kAuto, 12);
      factionBig(w, suzerain);
      bigIcon("shield", ui::Tone::Accent);
      factionBig(w, vassal);
    }
    ui::text("Государство «" + w.factionName(suzerain) + "» вступает в войну на стороне вассала «" + w.factionName(vassal) + "»?", ui::Font::Body,
             ui::Ink::Normal);
    {
      ui::HStack hs(22, ui::Align::Left, 6);
      ui::icon("war", ui::Ink::Danger, 16);
      ui::label("Против «" + w.factionName(attacker) + "»", {.font = ui::Font::Small, .ink = ui::Ink::Dim});
    }
    // Вступить нельзя (срок мира после перемирия с нападавшим) — «Да» выключено, причина в подсказке.
    std::string why;
    const bool can = w.relation(suzerain, attacker).s == RelStatus::War || rules::canDeclareWar(w, suzerain, attacker, &why);
    ui::ModalFooter f;
    const Id s = suzerain, v = vassal, at = attacker;
    ui::Disabled dis(!a.canPlay(s));   // отвечает сюзерен
    if (ui::button("Нет", {.icon = "close"})) {
      if (a.play("Сюзерен не вступает в войну", [&](Tx& tx) { rules::suzerainDefends(tx, s, v, at, false); })) {
        finish(a, false);
        return false;
      }
    }
    a.markUi("vassal.defend.no");
    if (ui::button("Да", {.variant = ui::Variant::Primary, .icon = "war", .disabled = !can, .isDefault = true, .tooltip = why})) {
      if (a.play("Сюзерен вступает в войну", [&](Tx& tx) { rules::suzerainDefends(tx, s, v, at, true); })) {
        finish(a, a.world().relation(s, at).s == RelStatus::War);
        return false;
      }
    }
    a.markUi("vassal.defend.yes");
    return true;
  }
};

// Призыв вассалов сюзерена против врага: зачинщика войны (defense — на сюзерена напали) или атакованного им
// государства (сюзерен — зачинщик). В окне — вассалы, которых можно вести в этом сеансе.
struct CallDlg final : Dialog {
  Id suzerain = 0, enemy = 0;
  bool defense = false;
  std::vector<Id> vassals;
  std::vector<int> answer;   // −1 — нет ответа, 0 — согласился, 1 — отказался
  std::function<void(App&)> done;   // после ответа или закрытия окна
  bool finished = false;

  const char* id() const override { return "vassal.call"; }
  Style style(App&) override { return {"Призыв вассалов", "banner", ui::Tone::Accent, 560}; }

  // joined — согласившиеся вассалы вступили в войну: разбор вассалитета для каждого по очереди, затем done.
  void finish(App& a, std::vector<Id> joined = {}) {
    if (finished) return;
    finished = true;
    std::vector<std::pair<Id, Id>> wars;
    for (Id v : joined) wars.push_back({v, enemy});
    std::function<void(App&)> fn = done;
    if (!wars.empty()) detail::later(a, [wars, fn](App& x) { chain(x, wars, fn); });
    else if (fn) detail::later(a, fn);
  }
  void dismissed(App& a) override { finish(a); }

  bool draw(App& a) override {
    const World& w = frameWorld(a);
    if (!w.faction(suzerain) || !w.faction(enemy)) {
      finish(a);
      return false;
    }
    {
      ui::Row r({ui::fr(1), ui::px(64), ui::fr(1)}, ui::kAuto, 12);
      factionBig(w, suzerain);
      bigIcon(defense ? "shield" : "war", defense ? ui::Tone::Accent : ui::Tone::Danger);
      factionBig(w, enemy);
    }
    {
      // Кто начал войну: сюзерен напал или на сюзерена напали.
      ui::HStack hs(22, ui::Align::Left, 6);
      ui::icon(defense ? "shield" : "war", defense ? ui::Ink::Accent : ui::Ink::Danger, 16);
      ui::label((defense ? "Оборона от «" : "Нападение на «") + w.factionName(enemy) + "»", {.font = ui::Font::Small, .ink = ui::Ink::Dim});
      a.markUi(defense ? "vassal.call.defense" : "vassal.call.attack");
    }
    const ui::Theme& t = ui::theme();
    int answered = 0;
    for (size_t i = 0; i < vassals.size(); i++) {
      const Faction* v = w.faction(vassals[i]);
      if (!v) continue;
      ui::IdScope sc{i64(v->id)};
      const Relation rel = w.relation(suzerain, v->id);
      RectF rr = ui::next(46);
      ui::draw::rect(rr, t.stripe, t.radiusCard);
      ui::Area area(rr.inset(10, 8), 0);
      ui::Row row({ui::px(30), ui::fr(1), ui::px(52), ui::px(210)}, 30, 10);
      ui::flag(v->flag, 30, 20, 2.5f);
      ui::label(displayName(*v), {.font = ui::Font::Strong});
      ui::label(fmtSigned(rel.v), {.ink = rel.v < 0 ? ui::Ink::Danger : rel.v > 0 ? ui::Ink::Success : ui::Ink::Muted, .align = ui::Align::Right,
                                   .tooltip = "Отношения с вассалом"});
      ui::segmented("answer", answer[i], {{nullptr, "Согласился", {}}, {nullptr, "Отказался", {}}}, {.size = ui::Size::Small});
      a.markUi("vassal.call." + std::to_string(v->id));
      if (answer[i] >= 0) answered++;
    }
    ui::ModalFooter f;
    if (ui::button("Отмена")) {
      finish(a);
      return false;
    }
    a.markUi("vassal.call.cancel");
    bool party = false;   // отвечают сами вассалы
    for (Id v : vassals) party = party || a.canPlay(v);
    if (ui::button("Готово", {.variant = ui::Variant::Primary, .icon = "check", .disabled = answered == 0 || !party, .isDefault = true})) {
      const Id s = suzerain, e = enemy;
      std::vector<std::pair<Id, bool>> list;
      for (size_t i = 0; i < vassals.size(); i++)
        if (answer[i] >= 0 && a.canPlay(vassals[i])) list.push_back({vassals[i], answer[i] == 0});
      if (a.play(defense ? "Призыв вассалов на защиту" : "Призыв вассалов", [&](Tx& tx) {
            for (auto [v, agree] : list) rules::vassalAnswers(tx, s, v, e, agree);
          })) {
        std::vector<Id> joined;
        for (auto [v, agree] : list)
          if (agree && a.world().relation(v, e).s == RelStatus::War) joined.push_back(v);
        finish(a, joined);
        return false;
      }
    }
    a.markUi("vassal.call.ok");
    return true;
  }
};

// Призыв вассалов suzerain против enemy. Не зовутся сам враг (вассал, напавший на сюзерена), skip (сюзерен
// атакованного: он отвечает, вступится ли за вассала), уже воюющие с врагом, воюющие со своим сюзереном и те, кто не
// может объявить врагу войну (срок мира после перемирия — rules::canDeclareWar). Вассалы не за этим компьютером
// получают предложение; остальным — окно. Никого в окне — сразу done.
void openCall(App& a, Id suzerain, Id enemy, bool defense, Id skip, std::function<void(App&)> done) {
  auto finish = [&] {
    if (done) done(a);
  };
  const World& w = a.world();
  const Faction* s = w.faction(suzerain);
  if (!s || !s->isState() || !w.faction(enemy)) return finish();
  std::vector<Id> here;
  std::vector<Proposal> away;
  for (Id v : rules::vassalsOf(w, suzerain)) {
    if (v == enemy || v == skip || w.relation(v, enemy).s == RelStatus::War || w.relation(v, suzerain).s == RelStatus::War) continue;
    if (!rules::canDeclareWar(w, v, enemy)) continue;
    if (a.canPlay(v)) here.push_back(v);
    else away.push_back(question(ProposalKind::Call, suzerain, v, enemy));
  }
  proposeAll(a, std::move(away));
  if (here.empty()) return finish();
  std::sort(here.begin(), here.end(), [&](Id x, Id y) { return compareRu(w.factionName(x), w.factionName(y)) < 0; });
  auto d = std::make_unique<CallDlg>();
  d->suzerain = suzerain;
  d->enemy = enemy;
  d->defense = defense;
  d->vassals = here;
  d->answer.assign(here.size(), -1);
  d->done = std::move(done);
  a.openDialog(std::move(d));
}

}  // namespace

namespace flow {

void afterWarDeclared(App& a, Id declarer, Id target, std::function<void(App&)> done) {
  const World& w = a.world();
  const Faction* t = w.faction(target);
  if (!w.faction(declarer) || !t || w.relation(declarer, target).s != RelStatus::War) {
    if (done) done(a);
    return;
  }
  // Сюзерен атакованного вассала: не зачинщик и ещё не в войне с ним. В призыв зачинщика он не попадает (противоречивых
  // призывов нет), а вопрос о защите получает, только если может объявить зачинщику войну (срок мира после перемирия,
  // вассал против своего сюзерена — rules::canDeclareWar).
  Id s = t->isState() ? t->suzerain : 0;
  if (s && (s == declarer || !w.faction(s) || w.relation(s, declarer).s == RelStatus::War)) s = 0;
  const bool ask = s && rules::canDeclareWar(w, s, declarer);
  // По очереди: вассалы зачинщика (против атакованного; без сюзерена атакованного), затем вассалы атакованного (против
  // зачинщика).
  auto defenders = [declarer, target, done](App& x) { openCall(x, target, declarer, true, 0, done); };
  auto attackers = [declarer, target, s, defenders](App& x) { openCall(x, declarer, target, false, s, defenders); };
  if (!ask) return attackers(a);
  if (!a.canPlay(s)) {   // сюзерена нет за этим компьютером — вопрос в «Решения»
    proposeAll(a, {question(ProposalKind::Defend, declarer, s, target)});
    return attackers(a);
  }
  auto d = std::make_unique<DefendDlg>();
  d->suzerain = s;
  d->vassal = target;
  d->attacker = declarer;
  d->next = attackers;
  a.openDialog(std::move(d));
}

}  // namespace flow

}  // namespace rg::app
