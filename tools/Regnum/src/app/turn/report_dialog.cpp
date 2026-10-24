// Regnum — отчёт о завершённом ходе (DialogReg «turn.report»): ход N → N+1, карточки фракций с изменением
// казны, доходом и расходом, изменения запасов ресурсов и эссенций элементов, голод (недостача провизии); записи
// хроники хода по видам (щелчок — показать на карте); предупреждения: долги, голод, нехватка эссенций на содержание
// элементалей, недостачи по сделкам, восстания, снос построек сверх слотов, чума; исходы заказных убийств заговоров
// (администратору: вероятность, погиб или выжил). Две страницы, как в подтверждении хода (ТЗ «Фиксы», п.16): основные
// игровые государства и остальные фракции. Итоги берутся из мира (Meta::lastTurn) и переживают перезапуск; игроку —
// «Итоги хода N» его государства: его карточка и записи хода о нём (без страниц и исходов покушений).
#include "app/app_internal.h"
#include "app/turn/turn_ui.h"
#include "app/widgets.h"

namespace rg::app::turnui {

namespace {

// Порядок видов в отчёте: сначала тревожное, затем экономика и события.
constexpr LogKind kOrder[] = {LogKind::Province, LogKind::Economy, LogKind::Trade, LogKind::Diplomacy, LogKind::Build, LogKind::Tech,
                              LogKind::War, LogKind::Battle, LogKind::Army, LogKind::Fleet, LogKind::Guild, LogKind::Population, LogKind::Archaeology,
                              LogKind::Note};

void kindHeader(LogKind k, size_t n) {
  ui::HStack hs(26, ui::Align::Left, 8);
  RectF r = ui::next(22, 26);
  iconTile(RectF{r.x, r.cy() - 11, 22, 22}, logIcon(k), logTone(k));
  ui::label(logName(k), {.font = ui::Font::Strong});
  ui::badge(std::to_string(n), ui::Tone::Neutral);
}

// Исход заказного убийства — на странице государства героя (основные или остальные).
bool hitOnPage(const World& w, const HitOutcome& o, int page) {
  const Character* c = w.character(o.character);
  return page == kPageAll || isMainState(w, c ? c->faction : 0) == (page == kPageMain);
}

// Строка исхода: портрет, имя и государство героя с заговором, вероятность (подсказка — бросок), «Погиб» или «Выжил».
void hitRow(App& a, const World& w, const HitOutcome& o) {
  const ui::Theme& th = ui::theme();
  const Character* c = w.character(o.character);
  ui::IdScope s{i64(o.character)};
  const RectF top = ui::avail();
  {
    ui::Row r({ui::px(30), ui::fr(1), ui::px(64), ui::px(84)}, 36, 10);
    if (c) w::heroAvatar(*c, 30);
    else ui::next(30);
    {
      const RectF cell = ui::next(36);
      const std::string name = c ? w::orName(c->name, "Без имени") : std::string("Герой");
      std::string sub = c ? w.factionName(c->faction) : std::string();
      if (const Plot* p = w.plot(o.plot)) sub += " · " + rules::plotTitle(w, *p);
      ui::draw::text(name, RectF{cell.x, cell.y, cell.w, 19}, ui::Font::Strong, th.text);
      ui::draw::text(sub, RectF{cell.x, cell.y + 19, cell.w, 16}, ui::Font::Small, th.textMuted);
      ui::hoverTip("##who", cell, name + "\n" + sub);
    }
    ui::label(fmtPct(o.chance), {.ink = ui::Ink::Dim, .align = ui::Align::Right,
                                 .tooltip = "Вероятность успеха · бросок " + fmtNum(o.roll, 1)});
    {
      ui::HStack hs(24, ui::Align::Right, 0);
      ui::tag(o.killed ? "Погиб" : "Выжил", o.killed ? ui::Tone::Danger : ui::Tone::Success, o.killed ? "skull" : "shield");
    }
  }
  a.markUi("turn.report.hit." + std::to_string(o.character), RectF{top.x, top.y, top.w, 36});
}

struct ReportDlg : Dialog {
  TurnRecord rep;
  Id state = 0;    // итоги игрока: только его государство
  int page = -2;   // kPageMain / kPageOther; −2 — выбрать при первом показе
  const char* id() const override { return "turn.report"; }
  Style style(App&) override {
    Style s;
    s.title = "Итоги хода " + std::to_string(rep.turnFrom);
    s.icon = "scroll";
    s.width = state ? 780 : 940;
    s.dismissOnBackdrop = true;
    return s;
  }

  // Карточка фракции: флаг, название, изменение казны, казна после, полосы дохода и расхода, запасы.
  // true — щелчок (открыть фракцию).
  bool factionCard(App& a, const World& w, const TurnFactionLine& l, double scale) {
    const Faction* f = w.faction(l.faction);
    if (!f) return false;
    const ui::Theme& th = ui::theme();
    ui::IdScope s{i64(l.faction)};
    double d = l.treasuryAfter - l.treasuryBefore;
    {
      ui::Card c({.pad = 10});
      {
        ui::Row r({ui::px(24), ui::fr(1), ui::px(78)}, 22, 8);
        if (f->isState()) {
          ui::flag(f->flag, 24, 16, 3);
        } else {
          RectF dr = ui::next(22);
          ui::draw::rect(RectF{dr.x, dr.cy() - 8, 24, 16}, f->color.alpha(0.22f), 3);
          ui::draw::icon("guild", RectF{dr.x + 6, dr.cy() - 6, 12, 12}, f->color);
        }
        ui::label(f->name.empty() ? std::string("Без названия") : f->name, {.font = ui::Font::Strong});
        ui::label(fmtSigned(d), {.font = ui::Font::Small, .ink = deltaInk(d), .align = ui::Align::Right,
                                 .icon = d > 0.5 ? "trend-up" : d < -0.5 ? "trend-down" : nullptr});
      }
      {
        ui::Row r({ui::fr(1), ui::px(96)}, 30, 10);
        ui::label(fmtGold(l.treasuryAfter), {.font = ui::Font::Number, .ink = l.treasuryAfter < 0 ? ui::Ink::Danger : ui::Ink::Normal,
                                             .tooltip = "Казна после хода; была " + fmtGold(l.treasuryBefore)});
        RectF br = ui::next(30);
        flowBars(br, l.income, l.expenses, scale);
      }
      // Запасы: голод (недостача провизии), ресурсы, эссенции элементов — что не помещается, «ещё N».
      if (hasStockChanges(l)) {
        const float cw = ui::avail().w;
        stockDeltas(w, l, cw, 22, ui::Font::Small);
        a.markUi("turn.report.stock." + std::to_string(l.faction));
      }
    }
    RectF cr = ui::lastItem().rect;
    a.markUi("turn.report.card." + std::to_string(l.faction), cr);
    ui::WidgetId wid = ui::id("##card");
    ui::Interaction it = ui::interact(wid, cr, ui::IfAllowOverlap);
    float hv = ui::animate(wid ^ 0xca7dull, it.hovered ? 1.f : 0.f);
    if (hv > 0.01f) ui::draw::rectStroke(cr, th.accent.alpha(0.6f * hv), th.radiusCard, 1.5f);
    if (it.hovered) ui::setCursor(platform::Cursor::Hand);
    return it.clicked;
  }

  bool draw(App& a) override {
    const World& w = a.store.world();
    if (state && !w.faction(state)) return false;
    const size_t nMain = sortedLines(w, rep, kPageMain).size(), nOther = sortedLines(w, rep, kPageOther).size();
    if (page == -2) page = nMain ? kPageMain : kPageOther;   // нет основных государств — сразу остальные
    {
      ui::HStack hs(26, ui::Align::Left, 8);
      ui::tag("Ход " + std::to_string(rep.turnFrom), ui::Tone::Neutral, "hourglass");
      ui::icon("arrow-right", ui::Ink::Muted, 16);
      ui::tag("Ход " + std::to_string(rep.turnTo), ui::Tone::Accent, "next-turn");
      if (const Faction* f = state ? w.faction(state) : nullptr) {
        ui::flex();
        ui::flag(f->flag, 24, 16, 3);
        ui::label(w::orName(f->name), {.font = ui::Font::Strong});
        a.markUi("turn.report.state");
      }
    }
    if (!state) {
      const std::string l0 = pageLabel(kPageMain, nMain), l1 = pageLabel(kPageOther, nOther);
      int pg = page;
      if (ui::segmented("pages", pg, {{"crown", l0, "Основные игровые государства"}, {"list", l1, "Остальные государства и гильдии"}}))
        page = pg == kPageOther ? kPageOther : kPageMain;
      a.markUi("turn.report.pages");
    }
    TurnDigest d = digest(w, rep, page, state);
    {
      ui::Row r({ui::fr(1), ui::fr(1), ui::fr(1), ui::fr(1)}, 64, 10);
      ui::stat(std::to_string(d.all.size()), "Записей хроники", {.icon = "chronicle", .tone = ui::Tone::Info});
      ui::stat(std::to_string(d.builds.size()), "Достроено", {.icon = "build", .tone = ui::Tone::Info});
      ui::stat(std::to_string(d.techs.size()), "Исследования", {.icon = "research", .tone = ui::Tone::Info});
      ui::stat(std::to_string(d.rebellions.size()), "Восстаний", {.icon = "rebellion", .tone = d.rebellions.empty() ? ui::Tone::Neutral : ui::Tone::Danger});
    }
    Id pickFaction = 0;
    const LogEntry* pickEntry = nullptr;
    {
      FitScroll body("body", std::max(200.f, ui::viewport().h - 370));
      // Предупреждения
      std::vector<const TurnFactionLine*> lines = sortedLines(w, rep, page, state);
      std::vector<const TurnFactionLine*> debtors;
      for (auto* l : lines)
        if (l->treasuryAfter < 0) debtors.push_back(l);
      if (!debtors.empty() || !d.famine.empty() || !d.essences.empty() || !d.shortfalls.empty() || !d.rebellions.empty() || !d.plague.empty() ||
          !d.demolished.empty()) {
        {
          ui::Card c({.pad = 12, .icon = "warning", .title = "Требует внимания", .tone = ui::Tone::Danger});
          if (!debtors.empty()) {
            ui::label("Казна в долгу: " + std::to_string(debtors.size()) + " " + plural(i64(debtors.size()), "фракция", "фракции", "фракций"),
                      {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "treasury"});
            ui::Row r({ui::fr(1), ui::fr(1), ui::fr(1)}, 26, 6);
            for (auto* l : debtors) {
              const Faction* f = w.faction(l->faction);
              if (!f) continue;
              ui::IdScope s{i64(l->faction)};
              std::string text = (f->name.empty() ? std::string("Без названия") : f->name) + " · " + fmtGold(l->treasuryAfter);
              if (ui::chip(text, {.color = f->color, .tone = ui::Tone::Danger, .clickable = true, .tooltip = "Открыть фракцию"}) == ui::ChipAction::Click)
                pickFaction = l->faction;
            }
          }
          for (const LogEntry* e : d.rebellions)
            if (logRow(w, *e, {.showTime = false, .maxLines = 3})) pickEntry = e;
          for (const LogEntry* e : d.demolished)
            if (logRow(w, *e, {.showTime = false, .maxLines = 3})) pickEntry = e;
          for (const LogEntry* e : d.plague)
            if (logRow(w, *e, {.showTime = false, .maxLines = 3})) pickEntry = e;
          for (const LogEntry* e : d.famine)
            if (logRow(w, *e, {.showTime = false, .maxLines = 3})) pickEntry = e;
          for (const LogEntry* e : d.essences)
            if (logRow(w, *e, {.showTime = false, .maxLines = 3})) pickEntry = e;
          for (const LogEntry* e : d.shortfalls)
            if (logRow(w, *e, {.showTime = false, .maxLines = 3})) pickEntry = e;
        }
        a.markUi("turn.report.warnings");
      }
      // Заказные убийства заговоров: кто, вероятность, погиб или выжил (только администратору — шансы и броски).
      std::vector<const HitOutcome*> hits;
      if (a.admin() && !state)
        for (const HitOutcome& o : rep.hits)
          if (hitOnPage(w, o, page)) hits.push_back(&o);
      if (!hits.empty()) {
        {
          ui::Card c({.pad = 12, .icon = "skull", .title = "Заказные убийства", .tone = ui::Tone::Danger});
          for (const HitOutcome* o : hits) hitRow(a, w, *o);
        }
        a.markUi("turn.report.hits");
      }
      // Казна фракций (у игрока — его государства)
      if (lines.empty()) {
        ui::label(state ? "Государство не участвовало в ходе" : page == kPageMain ? "Основных государств нет" : "Других фракций нет", {.ink = ui::Ink::Muted});
      } else {
        ui::caption(state ? "Казна" : page == kPageMain ? "Казна государств" : "Казна фракций");
        double scale = 1;
        for (auto* l : lines) scale = std::max({scale, l->income, l->expenses});
        const ui::Len cols3[] = {ui::fr(1), ui::fr(1), ui::fr(1)};
        ui::Row r(std::span<const ui::Len>(cols3, state ? 1 : 3), ui::kAuto, 10);
        for (auto* l : lines)
          if (factionCard(a, w, *l, scale)) pickFaction = l->faction;
      }
      // События по видам
      ui::caption("События хода");
      bool any = false;
      for (LogKind k : kOrder) {
        size_t n = 0;
        for (const LogEntry* e : d.all)
          if (e->kind == k) n++;
        if (!n) continue;
        any = true;
        ui::IdScope s{int(k)};
        kindHeader(k, n);
        for (const LogEntry* e : d.all)
          if (e->kind == k && logRow(w, *e, {.showTime = false, .maxLines = 3})) pickEntry = e;
        ui::spacer(2);
      }
      if (!any) ui::emptyState("hourglass", "За ход ничего не произошло.");
    }
    a.markUi("turn.report.body");
    if (pickFaction) {
      detail::later(a, [pickFaction](App& x) { x.select(SelType::Faction, pickFaction, true); });
      return false;
    }
    if (pickEntry) {
      LogEntry e = *pickEntry;
      detail::later(a, [e](App& x) { focusEntry(x, e); });
      return false;
    }
    ui::ModalFooter f;
    if (ui::button("Хроника хода", {.icon = "chronicle"})) {
      int t = rep.turnFrom;
      detail::later(a, [t](App& x) { showChronicle(x, t); });
      return false;
    }
    a.markUi("turn.report.chronicle");
    if (ui::button("История ходов", {.icon = "history"})) {
      detail::later(a, [](App& x) { x.openDialog("turn.history"); });
      return false;
    }
    a.markUi("turn.report.history");
    if (ui::button("Закрыть", {.variant = ui::Variant::Primary, .isDefault = true})) return false;
    a.markUi("dialog.ok");
    return true;
  }
};

// Итоги последнего хода — из мира (Meta::lastTurn): отмена хода и возврат к другому ходу приводят с собой и его итоги.
// Игроку — итоги его государства.
std::unique_ptr<Dialog> makeRegistered(App& a, Id) {
  const TurnRecord& r = a.store.world().meta->lastTurn;
  if (r.turnTo <= 0) {
    a.toast("Ход ещё не завершался", ToastKind::Info, "scroll");
    return nullptr;
  }
  return makeReport(a, r, a.player() ? a.ui.playerState : 0);
}

DialogReg reg({"turn.report", makeRegistered});

}  // namespace

std::unique_ptr<Dialog> makeReport(App&, const TurnRecord& rep, Id state) {
  auto d = std::make_unique<ReportDlg>();
  d->rep = rep;
  d->state = state;
  return d;
}

}  // namespace rg::app::turnui
