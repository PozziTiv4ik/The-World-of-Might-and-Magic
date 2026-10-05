// Regnum — вассалитет после объявления войны (ТЗ «Механика вассалитета»): окна по очереди.
//  1. Атакован вассал, его сюзерен не зачинщик и ещё не в войне с ним — «Государство «S» вступает в войну на стороне
//     вассала «T»?» Да/Нет (rules::suzerainDefends: война и +15 к отношениям с вассалом или −20).
//  2. У зачинщика есть вассалы — призыв: каждому «согласился» или «отказался» (rules::vassalAnswers: ±10, согласные
//     вступают в войну).
// Закрытие окна без ответа ничего не меняет.
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

// Вопрос сюзерену атакованного вассала.
struct DefendDlg final : Dialog {
  Id suzerain = 0, vassal = 0, attacker = 0;
  std::function<void(App&)> next;
  bool decided = false;

  const char* id() const override { return "vassal.defend"; }
  Style style(App&) override { return {"Сюзерен и вассал", "banner", ui::Tone::Accent, 520}; }

  void finish(App& a) {
    if (decided) return;
    decided = true;
    if (next) {
      auto fn = next;
      detail::later(a, fn);
    }
  }
  void dismissed(App& a) override { finish(a); }

  bool draw(App& a) override {
    const World& w = frameWorld(a);
    if (!w.faction(suzerain) || !w.faction(vassal) || !w.faction(attacker)) {
      finish(a);
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
    ui::ModalFooter f;
    const Id s = suzerain, v = vassal, at = attacker;
    if (ui::button("Нет", {.icon = "close"})) {
      a.act("Сюзерен не вступает в войну", [&](Tx& tx) { rules::suzerainDefends(tx, s, v, at, false); });
      finish(a);
      return false;
    }
    a.markUi("vassal.defend.no");
    if (ui::button("Да", {.variant = ui::Variant::Primary, .icon = "war", .isDefault = true})) {
      a.act("Сюзерен вступает в войну", [&](Tx& tx) { rules::suzerainDefends(tx, s, v, at, true); });
      finish(a);
      return false;
    }
    a.markUi("vassal.defend.yes");
    return true;
  }
};

// Призыв вассалов зачинщика войны.
struct CallDlg final : Dialog {
  Id suzerain = 0, enemy = 0;
  std::vector<Id> vassals;
  std::vector<int> answer;   // −1 — нет ответа, 0 — согласился, 1 — отказался
  std::function<void(App&)> done;   // после ответа или закрытия окна
  bool finished = false;

  const char* id() const override { return "vassal.call"; }
  Style style(App&) override { return {"Призыв вассалов", "banner", ui::Tone::Accent, 560}; }

  void finish(App& a) {
    if (finished) return;
    finished = true;
    if (done) detail::later(a, done);
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
      bigIcon("war", ui::Tone::Danger);
      factionBig(w, enemy);
    }
    const ui::Theme& t = ui::theme();
    int answered = 0;
    for (size_t i = 0; i < vassals.size(); i++) {
      const Faction* v = w.faction(vassals[i]);
      if (!v) continue;
      ui::IdScope sc{i64(v->id)};
      const Relation rel = w.relation(suzerain, v->id);
      RectF rr = ui::next(46);
      ui::draw::rect(rr, t.dark ? t.stripe : t.surface3.alpha(0.55f), t.radiusCard);
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
    if (ui::button("Готово", {.variant = ui::Variant::Primary, .icon = "check", .disabled = answered == 0, .isDefault = true})) {
      const Id s = suzerain, e = enemy;
      std::vector<std::pair<Id, bool>> list;
      for (size_t i = 0; i < vassals.size(); i++)
        if (answer[i] >= 0) list.push_back({vassals[i], answer[i] == 0});
      if (a.act("Призыв вассалов", [&](Tx& tx) {
            for (auto [v, agree] : list) rules::vassalAnswers(tx, s, v, e, agree);
          })) {
        finish(a);
        return false;
      }
    }
    a.markUi("vassal.call.ok");
    return true;
  }
};

// Призыв: вассалы зачинщика, кроме самой цели и уже воюющих с целью или с зачинщиком.
void openCall(App& a, Id declarer, Id target, std::function<void(App&)> done) {
  auto finish = [&] {
    if (done) done(a);
  };
  const World& w = a.world();
  if (!w.faction(declarer) || !w.faction(target)) return finish();
  std::vector<Id> list;
  for (Id v : rules::vassalsOf(w, declarer)) {
    if (v == target || w.relation(v, target).s == RelStatus::War || w.relation(v, declarer).s == RelStatus::War) continue;
    list.push_back(v);
  }
  if (list.empty()) return finish();
  std::sort(list.begin(), list.end(), [&](Id x, Id y) { return compareRu(w.factionName(x), w.factionName(y)) < 0; });
  auto d = std::make_unique<CallDlg>();
  d->suzerain = declarer;
  d->enemy = target;
  d->vassals = list;
  d->answer.assign(list.size(), -1);
  d->done = std::move(done);
  a.openDialog(std::move(d));
}

}  // namespace

namespace flow {

void afterWarDeclared(App& a, Id declarer, Id target, std::function<void(App&)> done) {
  const World& w = a.world();
  const Faction* t = w.faction(target);
  if (!w.faction(declarer) || !t) {
    if (done) done(a);
    return;
  }
  auto call = [declarer, target, done](App& x) { openCall(x, declarer, target, done); };
  // Сюзерен атакованного вассала: не зачинщик и ещё не в войне с ним.
  const Id s = t->isState() ? t->suzerain : 0;
  if (s && s != declarer && w.faction(s) && w.relation(s, declarer).s != RelStatus::War) {
    auto d = std::make_unique<DefendDlg>();
    d->suzerain = s;
    d->vassal = target;
    d->attacker = declarer;
    d->next = call;
    a.openDialog(std::move(d));
    return;
  }
  call(a);
}

}  // namespace flow

}  // namespace rg::app
