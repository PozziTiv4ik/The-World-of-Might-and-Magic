// Раздел «Решения» правой ленты (ТЗ «Доработки №2», п.1 и п.6): у игрока — только предложения его государства
// (сценарии предложений — test_app_proposals.cpp), число ожидающих решений на значке; готовность игроков;
// приглашения героев в заговоры — «Этот герой согласился» (участник) и «Этот герой отказался» («Угрозы заговору»);
// заказные убийства на конец хода с вероятностью и «Защитить от убийства» (переключатель: 0 %); после хода — исходы в
// отчёте и уведомлении.
#include "app/shell/role.h"
#include "app/turn/turn_ui.h"
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;

namespace {

bool has(const std::vector<Id>& v, Id x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// Заговор Альмарина против Хельдвига: два героя приглашены игроком (ждут решения), третий — угроза с заказом убийства;
// Альмарин отметил готовность завершить ход.
struct Setup {
  Id owner = 0, target = 0, plot = 0;
  Id agree = 0, refuse = 0, threat = 0;
};

Setup prepare(Harness& h) {
  Setup s;
  s.owner = factionByName(h->world(), "Альмарин");
  s.target = factionByName(h->world(), "Хельдвиг");
  CHECK(s.owner && s.target);
  CHECK(h->act("Заговор", [&](Tx& tx) {
    tx.faction(s.owner).mainState = true;
    tx.faction(s.target).mainState = true;
    tx.faction(s.owner).res[kGold] = 100000;
    s.agree = rules::createCharacter(tx, s.target, "Бран Тихий");
    s.refuse = rules::createCharacter(tx, s.target, "Вейла Стойкая");
    s.threat = rules::createCharacter(tx, s.target, "Гор Верный");
    const Id discontent = rules::ensureBuiltinMod(tx, schema::mod::Discontent);
    for (Id c : {s.agree, s.refuse}) rules::addModifier(tx, rules::ModTarget::Character, c, discontent, 0);
    s.plot = rules::createPlot(tx, PlotKind::External, s.owner, s.target);
    rules::addPlotHero(tx, s.plot, s.agree, true);
    rules::addPlotHero(tx, s.plot, s.refuse, true);
    tx.plot(s.plot).threats.push_back(s.threat);
    rules::orderHit(tx, s.plot, s.threat, 3000);
    rules::setTurnReady(tx, s.owner, true);
  }));
  h.dropToasts();
  return s;
}

std::string key(const Setup& s, const char* what, Id hero) {
  return std::string("decisions.") + what + "." + std::to_string(s.plot) + "." + std::to_string(hero);
}

}  // namespace

// Раздел — на ленте со счётчиком ожидающих решений: у администратора — готовность, предложения, приглашения и заказы;
// у игрока (аудит 2026-10-23, 1.1) — только предложения его государства, значок — входящие предложения.
TEST(app_decisions_roles) {
  Harness h("decisions_role");
  h.demo();
  const Setup s = prepare(h);
  h.settle();
  CHECK(h->uiRect("section.decisions") != nullptr);
  CHECK(h->uiRect("section.decisions.badge") != nullptr);   // 2 приглашения и заказ
  if (const RectF* r = h->uiRect("section.decisions"))
    CHECK(cropShot("decisions_rail", RectF{r->x - 6, r->y - 120, r->w + 12, r->h + 240}, 3));
  // Ctrl+0 — страница раздела.
  h.key(Key::D0, ctrl());
  h.settle();
  CHECK(h->view() == app::View::Directory);
  CHECK_EQ(h->ui.directory, std::string("decisions"));
  h->toMap();
  h.settle();

  // Игрок: раздел есть, но на странице — только предложения его государства (приглашений, заказов и готовности нет);
  // значка нет — входящих предложений нет.
  role::becomePlayer(h.a(), s.owner);
  h.settle();
  CHECK(role::sectionAllowed(h.a(), "decisions"));
  CHECK(!role::sectionAllowed(h.a(), "reference"));
  CHECK(role::sectionAllowed(h.a(), "states"));
  CHECK(h->uiRect("section.decisions") != nullptr);
  CHECK(h->uiRect("section.decisions.badge") == nullptr);
  h.key(Key::D0, ctrl());
  h.settle();
  CHECK(h->view() == app::View::Directory);
  CHECK(h->uiRect("decisions.incoming") != nullptr);
  CHECK(h->uiRect("decisions.outgoing") != nullptr);
  for (const char* m : {"decisions.ready", "decisions.invites", "decisions.hits", "decisions.proposals"}) CHECK_MSG(h->uiRect(m) == nullptr, m);
  h->toMap();
  h.settle();
  // Администратору — снова всё.
  role::becomeAdmin(h.a());
  h.settle();
  CHECK(h->uiRect("section.decisions") != nullptr);
  CHECK(h->uiRect("section.decisions.badge") != nullptr);
}

// Приглашения: согласился — участник, отказался — угроза; заказ: «Защитить» — 0 %, повторно — снять, после хода герой
// жив, исход — в отчёте и уведомлении.
TEST(app_decisions_invites_and_hits) {
  Harness h("decisions_page");
  h.demo();
  LogTap tap("decisions-page");
  const Setup s = prepare(h);
  h->openSection("decisions");
  h.settle();
  CHECK(h->view() == app::View::Directory);
  for (const char* m : {"decisions.ready", "decisions.invites", "decisions.hits"}) CHECK_MSG(h->uiRect(m) != nullptr, m);
  CHECK(h->uiRect("decisions.ready." + std::to_string(s.owner)) != nullptr);    // готов
  CHECK(h->uiRect("decisions.ready." + std::to_string(s.target)) != nullptr);   // основное без отметки
  CHECK(h->uiRect(key(s, "invite", s.agree)) != nullptr);
  CHECK(h->uiRect(key(s, "invite", s.refuse)) != nullptr);
  CHECK(h->uiRect(key(s, "hit", s.threat)) != nullptr);
  shotClean(h, "decisions_page");

  // Вероятность — с разбивкой в подсказке: 3000 золота — 30 %, остальное — эффекты сторон (засеянные модификаторы).
  const rules::HitChance hc = rules::hitChance(h->world(), s.plot, s.threat, 3000);
  CHECK_NEAR(hc.base, 30, 1e-9);
  if (const RectF* r = h->uiRect(key(s, "hit", s.threat) + ".chance")) {
    h.move(r->cx(), r->cy());
    h.frames(40);
    CHECK_MSG(ui::tooltipText().find("Вероятность успеха: " + fmtPct(hc.total)) != std::string::npos, ui::tooltipText());
  }

  // Приглашения.
  CHECK(h.clickUi(key(s, "invite", s.agree) + ".yes"));
  h.settle();
  CHECK(has(h->world().plot(s.plot)->heroes, s.agree));
  CHECK(!has(h->world().plot(s.plot)->invited, s.agree));
  CHECK(h.clickUi(key(s, "invite", s.refuse) + ".no"));
  h.settle();
  CHECK(has(h->world().plot(s.plot)->threats, s.refuse));
  CHECK(h->world().plot(s.plot)->invited.empty());
  CHECK(h->uiRect(key(s, "invite", s.agree)) == nullptr);

  // Защита: 0 %, повторное нажатие снимает, ещё раз — защищён.
  CHECK(h.clickUi(key(s, "hit", s.threat) + ".guard"));
  h.settle();
  CHECK(h->world().plot(s.plot)->hit(s.threat)->guarded);
  CHECK_NEAR(rules::hitChance(h->world(), s.plot, s.threat, 3000).total, 0, 1e-9);
  CHECK(h.clickUi(key(s, "hit", s.threat) + ".guard"));
  h.settle();
  CHECK(!h->world().plot(s.plot)->hit(s.threat)->guarded);
  CHECK(h.clickUi(key(s, "hit", s.threat) + ".guard"));
  h.settle();
  CHECK(h->world().plot(s.plot)->hit(s.threat)->guarded);
  // Счётчик: приглашений нет, заказ защищён — значка нет. Второй заказ — на отказавшегося (угроза), без защиты.
  CHECK(h->uiRect("section.decisions.badge") == nullptr);
  CHECK(h->act("Заказ", [&](Tx& tx) { rules::orderHit(tx, s.plot, s.refuse, 5000); }));
  h.settle();
  // 5000 золота — 50 %, остальное — эффекты сторон (засеянные модификаторы демонстрационного мира).
  const rules::HitChance refuseChance = rules::hitChance(h->world(), s.plot, s.refuse, 5000);
  CHECK_NEAR(refuseChance.base, 50, 1e-9);
  CHECK(h->uiRect("section.decisions.badge") != nullptr);
  h.move(100, 600);   // без подсказки под указателем
  shotClean(h, "decisions_page_guarded");

  // Ход: защищённый жив (0 %), исходы — в уведомлении и отчёте.
  CHECK(h->endTurnNow());
  CHECK(hasToast(h, "покушений: 2"));
  h->closeDialogs();
  h.settle();
  const auto& rep = h->lastTurnReport();
  CHECK(rep.has_value());
  CHECK_EQ(rep ? rep->hits.size() : size_t(0), size_t(2));
  CHECK(rules::heroAvailable(h->world(), s.threat));
  bool killed = false;
  if (rep)
    for (const rules::HitOutcome& o : rep->hits) {
      if (o.character == s.threat) {
        CHECK(!o.killed);
        CHECK_NEAR(o.chance, 0, 1e-9);
      }
      if (o.character == s.refuse) {
        CHECK_NEAR(o.chance, refuseChance.total, 1e-9);
        killed = o.killed;
        CHECK_EQ(rules::heroAvailable(h->world(), s.refuse), !o.killed);
      }
    }
  CHECK(h->world().plot(s.plot) == nullptr || h->world().plot(s.plot)->hits.empty());
  CHECK(h->openDialog("turn.report"));
  h.settle();
  CHECK(h->uiRect("turn.report.hits") != nullptr);
  CHECK(h->uiRect("turn.report.hit." + std::to_string(s.threat)) != nullptr);
  CHECK(h->uiRect("turn.report.hit." + std::to_string(s.refuse)) != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot(killed ? "decisions_report_killed" : "decisions_report"));
  h.key(Key::Escape);
  h.settle();
  // Страница после хода: заказов нет — одной строкой.
  h->openSection("decisions");
  h.settle();
  CHECK(h->uiRect(key(s, "hit", s.threat)) == nullptr);
  // Игроку исходы в отчёте не показываются.
  role::becomePlayer(h.a(), s.owner);
  h.settle();
  CHECK(h->openDialog("turn.report"));
  h.settle();
  CHECK(h->uiRect("turn.report.hits") == nullptr);
  const auto dup = tap.warnings("повторяющийся ID");
  CHECK_MSG(dup.empty(), dup.empty() ? std::string() : dup[0]);
}
