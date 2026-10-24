// Regnum — подтверждение завершения хода с предварительным итогом: пробный ход (rules::endTurn на черновике,
// мир не меняется) — казна каждой фракции до и после с доходом и расходом, изменения запасов ресурсов и эссенций
// элементов, недостача провизии (голод), стройки и исследования, которые завершатся, истекающие сделки и выплаты,
// преобразование ресурсов, предупреждения (долги, голод, нехватка эссенций на содержание элементалей, недостачи,
// восстания). Две страницы (ТЗ «Фиксы», п.16): основные игровые государства (Faction::mainState) и все остальные.
// Вкладка «Готовность» (ТЗ «Доработки №1», п.1): государства, игроки которых отметили готовность завершить ход, и —
// приглушённо — основные государства без отметки; ход завершается для всех, отметки сбрасываются сами. Пока готовы
// не все основные государства, вкладка — «Готовность 2/5» с отметкой предупреждения.
// Игрок (окно «turn.ready»): тот же предпросмотр только со строкой своего государства и записями о нём, без исходов
// бросков (восстания, распространение чумы, покушения, мятежники); кнопка «Готов» отмечает готовность.
#include "app/app_internal.h"
#include "app/turn/turn_ui.h"
#include "app/widgets.h"

namespace rg::app::turnui {

namespace {

// Итог одной страницы: строки фракций и записи хроники, которые её касаются.
struct Page {
  std::vector<const TurnFactionLine*> lines;
  TurnDigest dg;
  double scale = 1;                          // общий максимум дохода и расхода (полосы)
  int up = 0, down = 0, resRows = 0;
};

struct Preview {
  rules::TurnReport rep;
  World before, after;                       // мир до и после пробного хода (записи хроники — в after)
  std::shared_ptr<const rules::Calc> calc;   // расчёт на начало хода (разбор доходов)
  Page pages[2];                             // kPageMain, kPageOther (у игрока — одна страница его государства)
  int ready = 0;                             // государств, отметивших готовность завершить ход
  MainReady main;                            // основные государства: готовы / всего
};

constexpr int kReadyTab = 4;   // вкладка «Готовность»: отметки игроков (ход завершается для всех)

// Игроку — без исходов бросков пробного хода: восстания, распространение чумы, покушения и мятежники (вид «Война»).
void hideRolls(TurnDigest& d) {
  auto rolled = [](const LogEntry* e) { return e->kind == LogKind::War || e->kind == LogKind::Battle; };
  d.rebellions.clear();
  d.plague.clear();
  d.other.erase(std::remove_if(d.other.begin(), d.other.end(), rolled), d.other.end());
  d.all.erase(std::remove_if(d.all.begin(), d.all.end(), [&](const LogEntry* e) {
                return rolled(e) || e->kind == LogKind::Province;
              }),
              d.all.end());
}

std::unique_ptr<Preview> makePreview(const World& w, Id state) {
  auto p = std::make_unique<Preview>();
  p->before = w;
  {
    Tx tx(w);   // черновик: исходный мир неизменяем
    p->rep = rules::endTurn(tx);
    p->after = std::move(tx).finish();
  }
  p->calc = rules::calc(w);
  p->ready = readyCount(w);
  p->main = mainReady(w);
  for (int k = 0; k < (state ? 1 : 2); k++) {
    Page& pg = p->pages[k];
    pg.lines = sortedLines(w, p->rep, k, state);
    pg.dg = digest(p->after, p->rep, k, state);
    if (state) hideRolls(pg.dg);
    for (auto* l : pg.lines) {
      pg.scale = std::max({pg.scale, l->income, l->expenses});
      double d = l->treasuryAfter - l->treasuryBefore;
      if (d > 0.5) pg.up++;
      if (d < -0.5) pg.down++;
      if (hasStockChanges(*l)) pg.resRows++;
    }
  }
  return p;
}

// «Ход N → Ход N+1»
void turnArrow(int from, int to) {
  ui::HStack hs(26, ui::Align::Left, 8);
  ui::tag("Ход " + std::to_string(from), ui::Tone::Neutral, "hourglass");
  ui::icon("arrow-right", ui::Ink::Muted, 16);
  ui::tag("Ход " + std::to_string(to), ui::Tone::Accent, "next-turn");
}

void groupHeader(const char* icon, ui::Tone tone, std::string_view title, size_t n) {
  ui::HStack hs(26, ui::Align::Left, 8);
  RectF r = ui::next(22, 26);
  iconTile(RectF{r.x, r.cy() - 11, 22, 22}, icon, tone);
  ui::label(title, {.font = ui::Font::Strong});
  ui::badge(std::to_string(n), ui::Tone::Neutral);
}

struct ConfirmDlg : Dialog {
  std::unique_ptr<Preview> p;
  Id state = 0;   // игрок: предпросмотр своего государства (окно «turn.ready»)
  int page = kPageMain;
  int tab = 0;
  const char* id() const override { return state ? "turn.ready" : "turn.confirm"; }
  Style style(App& a) override {
    Style s;
    const std::string n = std::to_string(a.store.world().turn());
    s.title = state ? "Готов завершить ход " + n + "?" : "Завершить ход " + n + "?";
    s.icon = state ? "check" : "next-turn";
    s.width = 800;
    return s;
  }

  float bodyH() const { return std::round(clamp(ui::viewport().h - 470, 160.f, 320.f)); }
  const Page& cur() const { return p->pages[page == kPageOther && !state ? kPageOther : kPageMain]; }
  std::string mark(std::string_view part) const { return std::string(state ? "turn.ready." : "turn.confirm.") + std::string(part); }

  void treasuryTab(App& a) {
    const World& w = p->before;
    const Page& pg = cur();
    ui::Column cols[] = {{page == kPageMain || state ? "Государство" : "Фракция", nullptr, ui::fr(1.7f)},
                         {"Доход", "income", ui::fr(1), ui::Align::Right},
                         {"Расход", "expense", ui::fr(1), ui::Align::Right},
                         {"Казна", "treasury", ui::fr(1.5f), ui::Align::Right},
                         {"Изменение", nullptr, ui::fr(0.9f), ui::Align::Right}};
    ui::Table t("treasury", cols, int(pg.lines.size()),
                {.rowHeight = 40, .height = state ? 0.f : bodyH(), .selectable = false, .emptyIcon = "crown",
                 .emptyText = page == kPageMain ? "Основных государств нет" : "Других фракций нет"});
    for (int i : t) {
      const TurnFactionLine& l = *pg.lines[size_t(i)];
      const rules::FactionCalc* fc = p->calc->faction(l.faction);
      a.markUi(mark("row.") + std::to_string(l.faction), t.rowRect());
      t.cell();
      factionLabel(w, l.faction);
      for (int k = 0; k < 2; k++) {
        double v = k == 0 ? l.income : l.expenses;
        RectF cr = cellRect(t);
        std::string tip = fc ? flowText(*fc, k == 0) : std::string();
        ui::at(RectF{cr.x, std::round(cr.cy() - 12), cr.w, 18});
        ui::label(fmtGold(v), {.ink = ui::Ink::Dim, .align = ui::Align::Right, .tooltip = tip});
        RectF br{cr.x + cr.w * 0.25f, std::round(cr.cy() + 8), cr.w * 0.75f, 4};
        float bw = float(clamp(v / std::max(1e-9, pg.scale), 0.0, 1.0)) * br.w;
        if (v > 0) bw = std::max(bw, 2.f);
        const ui::Theme& th = ui::theme();
        ui::draw::rect(br, th.track, 2);
        if (bw > 0) ui::draw::rect(RectF{br.right() - bw, br.y, bw, br.h}, k == 0 ? th.success : th.danger, 2);
      }
      t.cell();
      {
        ui::HStack hs(24, ui::Align::Right, 6);
        ui::label(fmtGold(l.treasuryBefore), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
        ui::icon("arrow-right", ui::Ink::Muted, 14);
        ui::label(fmtGold(l.treasuryAfter), {.font = ui::Font::Strong, .ink = l.treasuryAfter < 0 ? ui::Ink::Danger : ui::Ink::Normal});
      }
      double d = l.treasuryAfter - l.treasuryBefore;
      t.text(fmtSigned(d), deltaInk(d));
    }
  }

  // Изменения запасов: ресурсы, эссенции элементов и недостача провизии после хода (голод).
  void resourcesTab(App& a) {
    const World& w = p->before;
    std::vector<const TurnFactionLine*> rows;
    for (auto* l : cur().lines)
      if (hasStockChanges(*l)) rows.push_back(l);
    ui::Column cols[] = {{page == kPageMain || state ? "Государство" : "Фракция", nullptr, ui::fr(1.2f)},
                         {"Изменение запасов", "resource", ui::fr(2.2f)}};
    ui::Table t("resources", cols, int(rows.size()),
                {.rowHeight = 40, .height = bodyH(), .selectable = false, .emptyIcon = "resource", .emptyText = "Запасы не изменятся"});
    for (int i : t) {
      const TurnFactionLine& l = *rows[size_t(i)];
      a.markUi(mark("stock.") + std::to_string(l.faction), t.rowRect());
      t.cell();
      factionLabel(w, l.faction);
      const RectF cr = t.cell();
      stockDeltas(w, l, cr.w, 24);
    }
  }

  void eventsTab(App&) {
    const TurnDigest& d = cur().dg;
    if (d.events() == 0) {
      RectF r = ui::next(bodyH());
      ui::Area ar(RectF{r.x, r.y + r.h * 0.5f - 70, r.w, 140}, 0);
      ui::emptyState("hourglass", "В этом ходу ничего не завершится.");
      return;
    }
    ui::Scroll sc("events", bodyH());
    auto group = [&](const char* icon, ui::Tone tone, std::string_view title, const std::vector<const LogEntry*>& list) {
      if (list.empty()) return;
      ui::IdScope s(title);
      groupHeader(icon, tone, title, list.size());
      for (const LogEntry* e : list) logRow(p->after, *e, {.clickable = false, .showTime = false, .maxLines = 3});
      ui::spacer(4);
    };
    group("build", ui::Tone::Info, "Стройки завершатся", d.builds);
    group("research", ui::Tone::Info, "Исследования", d.techs);
    group("handshake", ui::Tone::Accent, "Сделки и выплаты завершатся", d.deals);
    group("convert", ui::Tone::Success, "Экономика", d.economy);
    group("list", ui::Tone::Neutral, "Прочее", d.other);
  }

  void warningsTab(App& a) {
    const TurnDigest& d = cur().dg;
    if (d.warnings() == 0) {
      RectF r = ui::next(bodyH());
      ui::Area ar(RectF{r.x, r.y + r.h * 0.5f - 70, r.w, 140}, 0);
      ui::emptyState("check-circle", state ? "Всё спокойно: долгов, голода, нехватки эссенций, недостач и сноса построек не будет."
                                           : "Всё спокойно: долгов, голода, нехватки эссенций, недостач, восстаний, чумы и сноса построек не будет.");
      return;
    }
    ui::Scroll sc("warnings", bodyH());
    auto group = [&](const char* icon, ui::Tone tone, std::string_view title, const std::vector<const LogEntry*>& list, std::string_view part) {
      if (list.empty()) return;
      ui::IdScope s(title);
      groupHeader(icon, tone, title, list.size());
      a.markUi(mark(part));
      for (const LogEntry* e : list) logRow(p->after, *e, {.clickable = false, .showTime = false, .maxLines = 3});
      ui::spacer(4);
    };
    group("rebellion", ui::Tone::Danger, "Восстания", d.rebellions, "warn.rebellions");
    group("build", ui::Tone::Danger, "Снос построек", d.demolished, "warn.demolished");
    group("plague", ui::Tone::Warning, "Чума", d.plague, "warn.plague");
    group("treasury", ui::Tone::Danger, "Долги казны", d.debts, "warn.debts");
    group("grain", ui::Tone::Danger, "Голод", d.famine, "warn.famine");
    group("essence", ui::Tone::Danger, "Эссенции на содержание элементалей", d.essences, "warn.essences");
    group("warning", ui::Tone::Warning, "Недостачи по сделкам", d.shortfalls, "warn.shortfalls");
  }

  // Игрок: казна своего государства после хода и изменение — крупно.
  void playerStats(const Page& pg, const TurnDigest& d) {
    const TurnFactionLine* l = pg.lines.empty() ? nullptr : pg.lines.front();
    const double after = l ? l->treasuryAfter : 0, delta = l ? l->treasuryAfter - l->treasuryBefore : 0;
    ui::Row r({ui::fr(1), ui::fr(1), ui::fr(1), ui::fr(1)}, 64, 10);
    // Крупно — коротко («2,1 млн»), точно — в подсказке.
    ui::stat(fmtGoldShort(after), "Казна после хода",
             {.icon = "treasury", .tone = after < 0 ? ui::Tone::Danger : ui::Tone::Accent, .tooltip = fmtGold(after)});
    ui::stat((delta > 0.0005 ? "+" : "") + fmtGoldShort(delta), "Изменение казны",
             {.icon = delta < -0.0005 ? "trend-down" : "trend-up", .tone = delta < -0.0005 ? ui::Tone::Warning : ui::Tone::Success,
              .tooltip = fmtGoldSigned(delta)});
    ui::stat(std::to_string(d.events()), "Завершится", {.icon = "check-circle", .tone = ui::Tone::Info});
    ui::stat(std::to_string(d.warnings()), "Внимание", {.icon = "warning", .tone = d.warnings() ? ui::Tone::Danger : ui::Tone::Neutral});
  }

  bool draw(App& a) override {
    if (!p) return false;
    if (state && (!a.player() || a.ui.playerState != state)) return false;   // роль сменилась
    turnArrow(p->rep.turnFrom, p->rep.turnTo);
    // Страницы: основные игровые государства и остальные фракции (у игрока — только своё государство).
    if (!state) {
      const std::string l0 = pageLabel(kPageMain, p->pages[kPageMain].lines.size());
      const std::string l1 = pageLabel(kPageOther, p->pages[kPageOther].lines.size());
      int pg = page;
      if (ui::segmented("pages", pg, {{"crown", l0, "Основные игровые государства"}, {"list", l1, "Остальные государства и гильдии"}}))
        page = pg == kPageOther ? kPageOther : kPageMain;
      a.markUi("turn.confirm.pages");
    }
    const Page& pg = cur();
    const TurnDigest& d = pg.dg;
    if (state) {
      playerStats(pg, d);
    } else {
      ui::Row r({ui::fr(1), ui::fr(1), ui::fr(1), ui::fr(1)}, 64, 10);
      ui::stat(std::to_string(pg.lines.size()), page == kPageMain ? "Государств" : "Фракций", {.icon = page == kPageMain ? "crown" : "list", .tone = ui::Tone::Accent});
      ui::stat(std::to_string(pg.up), "Казна растёт", {.icon = "trend-up", .tone = ui::Tone::Success});
      ui::stat(std::to_string(pg.down), "Казна убывает", {.icon = "trend-down", .tone = pg.down ? ui::Tone::Warning : ui::Tone::Neutral});
      ui::stat(std::to_string(d.events()), "Завершится", {.icon = "check-circle", .tone = ui::Tone::Info});
    }
    if (d.warnings() > 0) {
      std::vector<std::string> parts;
      if (!d.debts.empty()) parts.push_back("долги казны: " + std::to_string(d.debts.size()));
      if (!d.famine.empty()) parts.push_back("голод: " + std::to_string(d.famine.size()));
      if (!d.essences.empty()) parts.push_back("эссенции: " + std::to_string(d.essences.size()));
      if (!d.shortfalls.empty()) parts.push_back("недостачи: " + std::to_string(d.shortfalls.size()));
      if (!d.rebellions.empty()) parts.push_back("восстания: " + std::to_string(d.rebellions.size()));
      if (!d.demolished.empty()) parts.push_back("снос построек: " + std::to_string(d.demolished.size()));
      if (!d.plague.empty()) parts.push_back("чума: " + std::to_string(d.plague.size()));
      const ui::Theme& th = ui::theme();
      RectF r = ui::next(40);
      ui::draw::rect(r, th.warning.alpha(0.10f), th.radiusCard);
      ui::draw::rectStroke(r, th.warning.alpha(0.35f), th.radiusCard, 1);
      ui::draw::icon("warning", RectF{r.x + 12, r.cy() - 9, 18, 18}, th.warning);
      ui::draw::text("Внимание · " + join(parts, " · "), RectF{r.x + 40, r.y, r.w - 170, r.h}, ui::Font::Strong, th.text);
      ui::at(RectF{r.right() - 120, r.cy() - 12, 110, 24});
      if (ui::button("Подробнее", {.variant = ui::Variant::Ghost, .size = ui::Size::Small, .iconRight = "chevron-right"})) tab = 3;
      a.markUi(mark("warnings"));
    }
    // «Готовность»: пока основные государства готовы не все — «Готовность 2/5» с отметкой предупреждения.
    const bool waiting = p->main.total > 0 && !p->main.all();
    const std::string readyLabel = waiting ? "Готовность " + std::to_string(p->main.ready) + "/" + std::to_string(p->main.total) : std::string("Готовность");
    const std::string readyTip = waiting ? "Готовы завершить ход не все основные государства" : std::string("Готовность игроков завершить ход");
    ui::tabs("tabs", tab,
             {{"treasury", "Казна"},
              {"resource", "Ресурсы", {}, pg.resRows},
              {"check-circle", "События", {}, d.events()},
              {"warning", "Внимание", {}, d.warnings(), ui::Tone::Danger},
              {"check", readyLabel, readyTip, waiting ? -1 : p->ready, waiting ? ui::Tone::Warning : ui::Tone::Success}},
             {.style = ui::TabStyle::Pill, .fill = true});
    a.markUi(mark("tabs"));
    switch (tab) {
      case 1: resourcesTab(a); break;
      case 2: eventsTab(a); break;
      case 3: warningsTab(a); break;
      case kReadyTab: readinessTable(a, p->before, "ready", bodyH(), mark("ready")); break;
      default: treasuryTab(a); break;
    }
    if (a.store.world().settings->rebellionRoll && !state)
      ui::label("Бросок восстаний включён", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "dice"});
    ui::ModalFooter f;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    if (state) {
      if (ui::button("Готов", {.variant = ui::Variant::Primary, .icon = "check", .isDefault = true, .tooltip = "Готов завершить ход"})) {
        const Id st = state;
        detail::later(a, [st](App& x) {
          // Отметка ставится, только если её ещё нет (окно могло пережить отмену или смену роли).
          if (x.player() && x.ui.playerState == st && !rules::turnReady(x.store.world(), st)) toggleReady(x);
        });
        return false;
      }
      a.markUi("dialog.ok");
      return true;
    }
    if (ui::button("Завершить ход", {.variant = ui::Variant::Primary, .icon = "next-turn", .isDefault = true})) {
      detail::later(a, [](App& x) { x.endTurnNow(); });
      return false;
    }
    a.markUi("dialog.ok");
    return true;
  }
};

}  // namespace

std::unique_ptr<Dialog> makeConfirm(App& a) {
  auto d = std::make_unique<ConfirmDlg>();
  d->p = makePreview(a.store.world(), 0);
  // Первая страница — основные государства; если их нет — сразу остальные.
  d->page = d->p->pages[kPageMain].lines.empty() ? kPageOther : kPageMain;
  return d;
}

std::unique_ptr<Dialog> makeReadyPreview(App& a) {
  auto d = std::make_unique<ConfirmDlg>();
  d->state = a.ui.playerState;
  d->p = makePreview(a.store.world(), d->state);
  d->page = kPageMain;
  return d;
}

}  // namespace rg::app::turnui
