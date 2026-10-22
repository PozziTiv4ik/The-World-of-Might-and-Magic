// Regnum — раздел «Решения» правой ленты (ТЗ «Доработки №2», п.1 и п.6; только администратору): готовность игроков
// завершить ход (ТЗ «Доработки №1», п.1), приглашения героев в заговоры — решение администратора (герой согласился —
// участник заговора, отказался — «Угрозы заговору»), заказные убийства на конец хода с вероятностью и «Защитить от
// убийства». Число на значке ленты — ожидающие решения: приглашения и незащищённые заказы.
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
    for (const PlotHit& h : p.hits) out.push_back({p.id, h.character, h.gold, h.guarded});
  });
  return out;
}

// Ожидают решения: приглашения в заговоры и незащищённые заказные убийства.
int pending(App& a) {
  if (!a.admin()) return 0;
  const World& w = a.world();
  int n = int(invites(w).size());
  for (const HitRow& h : hits(w))
    if (!h.guarded) n++;
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
  if (std::fabs(c.intel) > 1e-9) lines.push_back("Штаб Разведки заказчика: " + fmtPct(c.intel, 1, true));
  if (std::fabs(c.guard) > 1e-9) lines.push_back("Контрразведка государства героя: " + fmtPct(c.guard, 1, true));
  if (std::fabs(c.hero) > 1e-9) lines.push_back("Герой: " + fmtPct(c.hero, 1, true));
  return join(lines, "\n");
}

// ---------------------------------------------------------------- блоки
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
                       {"", nullptr, ui::px(128), ui::Align::Right}};
  ui::Table t("hits", cols, int(rows.size()), {.rowHeight = 54, .selectable = false});
  struct Pick {
    Id plot = 0, hero = 0;
    bool on = false;
  } pick;
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
    bool click = false;
    if (r.guarded) click = turnui::statePill("guard", "Защищён", "shield", "Защищён от убийства · снять защиту");
    else click = ui::button("Защитить", {.icon = "shield", .tooltip = "Защитить от убийства: вероятность 0 %"});
    a.markUi(mark + ".guard");
    if (click) pick = {r.plot, r.hero, !r.guarded};
  }
  if (int d = t.doubleClicked(); d >= 0 && d < int(rows.size())) a.select(SelType::Character, rows[size_t(d)].hero);
  if (pick.hero)
    a.act(pick.on ? "Защитить от убийства" : "Снять защиту от убийства", [&](Tx& tx) { rules::guardHit(tx, pick.plot, pick.hero, pick.on); });
}

// Страница раздела: три блока подряд (пустой — одной строкой).
void drawDecisions(App& a) {
  if (!a.admin()) {   // раздел скрыт у игрока; на случай прямого открытия каталога
    ui::label(detail::role::kAdminOnly, {.ink = ui::Ink::Muted, .icon = "lock"});
    return;
  }
  const World& w = w::frameWorld(a);
  readyBlock(a, w);
  inviteBlock(a, w);
  hitBlock(a, w);
}

SectionReg section({.id = "decisions", .icon = "gavel", .title = "Решения", .order = 65, .shortcut = "Ctrl+0", .directory = drawDecisions,
                    .allTitle = "Ждут решения", .badge = pending, .adminOnly = true});

}  // namespace
}  // namespace rg::app
