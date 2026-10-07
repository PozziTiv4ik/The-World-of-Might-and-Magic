// Regnum — штурм гарнизона провинции (ТЗ «Механика войн», п.4): войско у замка или башни провинции государства, с
// которым его фракция в войне, сражается с гарнизоном — окно как у битвы (отряды сторон, потери, победитель).
// Победа — окно захвата (dialogs/capture.cpp), уничтожение войска — «Судьба героев», без гарнизона — сразу захват.
// Мятежники после битвы, мятежа или восстания в провинции прежнего государства без его войск — штурм или захват
// (ТЗ «Механика мятежа», п.2 и 4); перед штурмом гарнизона с отрицательной верностью его неверная часть переходит к
// мятежникам (ТЗ «Доработки», п.1). Герои гарнизона — в карточке гарнизона и в «Судьбе героев», если он уничтожен.
#include "app/app_internal.h"
#include "app/flows.h"
#include "app/panels/military.h"

namespace rg::app {

namespace {

using namespace mil;

using Losses = std::map<std::pair<Id, Id>, i64>;   // (фракция, строка) → потери нападающего

i64 sumOf(const Losses& L) {
  i64 n = 0;
  for (auto& [k, v] : L) n += v;
  return n;
}
i64 sumOf(const std::map<Id, i64>& L) {
  i64 n = 0;
  for (auto& [k, v] : L) n += v;
  return n;
}
i64 garrisonCount(const Province& p) {
  i64 n = 0;
  for (const GarrisonEntry& g : p.garrison) n += std::max<i64>(0, g.count);
  return n;
}

struct SiegeDialog final : Dialog {
  Id army = 0, province = 0;
  Vec2 origin;
  std::function<void(App&, bool)> done;
  bool decided = false;
  int winner = 0;   // 0 — нападающий, 1 — гарнизон
  Losses armyLosses;
  std::map<Id, i64> garrisonLosses;   // строка гарнизона → потери
  float cardsH = 0;
  std::shared_ptr<bool> retreatOk = std::make_shared<bool>(false);   // подтверждено «Отступить без потерь»

  const char* id() const override { return "siege"; }
  Style style(App& a) override {
    const World& w = frameWorld(a);
    return {"Штурм · " + w.provinceName(province), "castle", ui::Tone::Danger, 880};
  }

  void finish(App& a, bool applied) {
    if (decided) return;
    decided = true;
    if (done) {
      auto fn = done;
      detail::later(a, [fn, applied](App& x) { fn(x, applied); });
    }
  }

  // Отступление: войско возвращается туда, откуда пришло (или рядом); мир иначе не меняется.
  void retreat(App& a) {
    const World& w = a.world();
    if (const Army* ar = w.army(army); ar && dist2(ar->pos, origin) > 1e-6 && std::isfinite(origin.x) && std::isfinite(origin.y)) {
      std::optional<Vec2> to = rules::validPosition(w, ar->kind, origin, army) ? std::optional<Vec2>(origin) : rules::findFreeSpot(w, ar->kind, origin, army);
      const Id id = army;
      if (to) a.act("Отступить от стен: " + w.provinceName(province), [&](Tx& tx) { rules::moveArmy(tx, id, *to); });
      if (const Army* moved = a.world().army(army)) a.toast("«" + objectName(*moved) + "» отступает от стен", ToastKind::Info, "retreat");
    }
    finish(a, false);
  }

  void dismissed(App& a) override { retreat(a); }

  // Выбор победителя: две половины (нападающий, гарнизон); сторону без отрядов после потерь выбрать нельзя.
  void winnerPicker(App& a, const World& w, const Army& A, const Province& P, const bool can[2]) {
    const ui::Theme& th = ui::theme();
    RectF r = ui::next(58);
    float half = std::floor((r.w - 10) * 0.5f);
    for (int i = 0; i < 2; i++) {
      RectF h{r.x + float(i) * (half + 10), r.y, half, r.h};
      ui::WidgetId wid = ui::id(i == 0 ? "##win-attacker" : "##win-garrison");
      ui::Interaction it = ui::interact(wid, h, can[i] ? ui::IfFocusable : ui::IfNone);
      if (it.clicked && can[i]) winner = i;
      if (it.focused) {
        if (ui::keyPressed(platform::Key::Left)) {
          ui::consumeKey(platform::Key::Left);
          if (can[0]) winner = 0;
        }
        if (ui::keyPressed(platform::Key::Right)) {
          ui::consumeKey(platform::Key::Right);
          if (can[1]) winner = 1;
        }
      }
      bool sel = winner == i;
      float hv = ui::animate(wid ^ 1, it.hovered ? 1.f : 0.f);
      float sk = ui::animate(wid ^ 2, sel ? 1.f : 0.f, 0.14f);
      ui::draw::rect(h, Color::mix(th.surface3, th.accent.alpha(th.dark ? 0.16f : 0.14f), sk), 10);
      if (hv > 0.01f && !sel) ui::draw::rect(h, th.hover.alpha(hv), 10);
      ui::draw::rectStroke(h, Color::mix(th.border, th.accent, sk), 10, 1 + sk * 0.75f);
      if (it.focused) ui::draw::rectStroke(h.expand(3), th.accent.alpha(0.5f), 12, 2);
      ui::at(RectF{h.x + 12, h.cy() - 15, 45, 30});
      factionFlag(w, i == 0 ? A.leader() : P.owner, 45, 30);
      float x = h.x + 12 + 45 + 12, right = h.right() - 12;
      if (!can[i]) {
        const char* why = "Нет отрядов";
        float bw = ui::measure(why, ui::Font::Small) + 30;
        RectF b{right - bw, h.cy() - 12, bw, 24};
        ui::draw::rect(b, th.danger.alpha(th.dark ? 0.18f : 0.12f), 12);
        ui::draw::icon("skull", RectF{b.x + 7, b.cy() - 7, 14, 14}, th.danger);
        ui::draw::text(why, RectF{b.x + 24, b.y, bw - 28, b.h}, ui::Font::Small, th.danger);
        right = b.x - 10;
      }
      if (sel) {
        float bw = ui::measure("Победа", ui::Font::Strong) + 30;
        RectF b{right - bw, h.cy() - 12, bw, 24};
        ui::draw::rect(b, th.accent, 12);
        ui::draw::icon("crown", RectF{b.x + 7, b.cy() - 7, 14, 14}, th.onAccent);
        ui::draw::text("Победа", RectF{b.x + 24, b.y, bw - 28, b.h}, ui::Font::Strong, th.onAccent);
        right = b.x - 10;
      }
      float lh = ui::lineHeight(ui::Font::Caption), nh = ui::lineHeight(ui::Font::Strong);
      float y0 = h.cy() - (lh + nh) * 0.5f;
      std::string side = w.factionName(i == 0 ? A.leader() : P.owner);
      ui::draw::text(i == 0 ? "НАПАДАЮЩИЙ" : "ГАРНИЗОН", RectF{x, y0, right - x, lh}, ui::Font::Caption, th.textMuted);
      ui::draw::text(side, RectF{x, y0 + lh, right - x, nh}, ui::Font::Strong, sel ? th.text : th.textDim);
      a.markUi(i == 0 ? "siege.winner.attacker" : "siege.winner.garrison", h);
      if (!can[i] && it.hovered) ui::tooltip("После потерь у этой стороны не остаётся отрядов — победителем она быть не может");
    }
  }

  // Строки потерь: отряд, было, потери (не больше численности), станет.
  struct LossLine {
    UnitRow row;
    i64 count;
    i64* loss;
    std::string mark;
  };
  void lossTable(App& a, std::vector<LossLine>& lines, bool ro) {
    ui::Column cols[] = {{"Отряд", nullptr, ui::fr(1, 90)},
                         {"Было", nullptr, ui::px(64), ui::Align::Right},
                         {"Потери", "skull", ui::px(108), ui::Align::Left, false, "Не больше численности отряда"},
                         {"Станет", nullptr, ui::px(64), ui::Align::Right}};
    ui::Table t("units", cols, int(lines.size()), {.rowHeight = 38, .selectable = false, .emptyIcon = "army", .emptyText = "Отрядов нет"});
    for (int i : t) {
      LossLine& l = lines[size_t(i)];
      RectF cr = t.cell();
      unitCell(cr, l.row);
      t.text(fmtCount(l.count));
      t.cell();
      i64 loss = *l.loss;
      ui::Disabled dis(ro);
      if (ui::numberField("loss", loss, {.min = 0, .max = double(l.count), .tooltip = "Потери: от 0 до " + fmtCount(l.count)})) *l.loss = clamp<i64>(loss, 0, l.count);
      a.markUi(l.mark);
      i64 after = l.count - *l.loss;
      t.text(fmtCount(after), after == 0 ? ui::Ink::Danger : *l.loss > 0 ? ui::Ink::Warning : ui::Ink::Normal);
    }
  }

  void sideTotals(i64 before, i64 lost, const char* destroyed) {
    i64 after = before - lost;
    {
      ui::HStack hs(26, ui::Align::Left, 6);
      ui::label("Итого", {.font = ui::Font::Strong});
      ui::flex();
      ui::label(fmtCount(before), {.ink = ui::Ink::Dim});
      ui::icon("arrow-right", ui::Ink::Muted, 14);
      ui::label(fmtCount(after), {.font = ui::Font::Strong, .ink = after == 0 ? ui::Ink::Danger : ui::Ink::Normal});
      if (lost > 0) ui::tag(fmtSigned(double(-lost)), ui::Tone::Danger);
    }
    if (after == 0) ui::tag(destroyed, ui::Tone::Danger, "skull");
  }

  void attackerCard(App& a, const World& w, const Army& ar) {
    ui::IdScope s("attacker");
    {
      ui::Card card({.pad = 12, .tone = winner == 0 ? ui::Tone::Accent : ui::Tone::Neutral});
      {
        ui::Row hr({ui::px(46), ui::fr(1)}, ui::kAuto, 10);
        const RectF fig = ui::next(46, 46);
        objectBadgeIn(w, ar, fig, winner == 0);   // портрет главного полководца или фигурка
        a.markUi("siege.figure", fig);
        ui::Group g(0, 2);
        ui::caption(objectCaption(ar));
        ui::label(objectName(ar), {.font = ui::Font::Title});
        ui::HStack hs(18, ui::Align::Left, 5);
        factionFlag(w, ar.leader(), 22, 15);
        ui::label(w.factionName(ar.leader()), {.font = ui::Font::Small, .ink = ui::Ink::Dim});
      }
      {
        ui::HStack hs(26, ui::Align::Left, 6);
        ui::icon("commander", ui::Ink::Accent, 16, "Главный полководец");
        if (ar.commander) w::characterChip(ar.commander);
        else ui::label("Без полководца", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
        int others = 0;
        for (const ArmyGroup& g : ar.groups)
          for (Id h : g.heroes)
            if (h != ar.commander) {
              if (others < 2) w::characterChip(h);
              others++;
            }
        if (others > 2) ui::badge("+" + std::to_string(others - 2), ui::Tone::Neutral);
      }
      std::vector<LossLine> lines;
      for (const ArmyGroup& g : ar.groups)
        for (const ArmyUnit& u : g.units)
          if (auto r = unitRow(w, g.faction, u.row, false))
            lines.push_back(LossLine{*r, u.count, &armyLosses[{g.faction, u.row}], "siege.loss.army." + std::to_string(u.row)});
      lossTable(a, lines, a.readOnly());
      sideTotals(unitCount(ar), sumOf(armyLosses), "Войско будет уничтожено");
    }
    cardsH = std::max(cardsH, ui::lastItem().rect.h);
  }

  void garrisonCard(App& a, const World& w, const Province& p) {
    ui::IdScope s("garrison");
    const ui::Theme& th = ui::theme();
    {
      ui::Card card({.pad = 12, .tone = winner == 1 ? ui::Tone::Accent : ui::Tone::Neutral});
      {
        ui::Row hr({ui::px(46), ui::fr(1)}, ui::kAuto, 10);
        typeTile(ui::next(46, 46), "castle", winner == 1 ? th.accent : th.textDim);
        ui::Group g(0, 2);
        ui::caption("Гарнизон");
        ui::label(w.provinceName(p.id), {.font = ui::Font::Title});
        ui::HStack hs(18, ui::Align::Left, 5);
        factionFlag(w, p.owner, 22, 15);
        ui::label(w.factionName(p.owner), {.font = ui::Font::Small, .ink = ui::Ink::Dim});
      }
      // Герои гарнизона (ТЗ «Доработки», п.1): гарнизон уничтожен — «Судьба героев».
      if (!p.garrisonHeroes.empty()) {
        ui::HStack hs(26, ui::Align::Left, 6);
        ui::icon("hero", ui::Ink::Accent, 16, "Герои гарнизона");
        int shown = 0;
        for (Id h : p.garrisonHeroes) {
          if (!w.character(h)) continue;
          if (shown < 2) w::characterChip(h);
          shown++;
        }
        if (shown > 2) ui::badge("+" + std::to_string(shown - 2), ui::Tone::Neutral);
      }
      std::vector<LossLine> lines;
      for (const GarrisonEntry& g : p.garrison)
        if (auto r = unitRow(w, p.owner, g.row, false); r && g.count > 0)
          lines.push_back(LossLine{*r, g.count, &garrisonLosses[g.row], "siege.loss.garrison." + std::to_string(g.row)});
      lossTable(a, lines, a.readOnly());
      sideTotals(garrisonCount(p), sumOf(garrisonLosses), "Гарнизон будет уничтожен");
    }
    cardsH = std::max(cardsH, ui::lastItem().rect.h);
  }

  void apply(App& a) {
    const World& w = frameWorld(a);
    const Army* A = w.army(army);
    const Province* P = w.province(province);
    if (!A || !P) return;
    rules::SiegeResult r;
    r.attacker = army;
    r.province = province;
    r.attackerWins = winner == 0;
    r.attackerOrigin = origin;
    for (auto& [k, v] : armyLosses)
      if (v > 0) r.attackerLosses[k] = v;
    for (auto& [k, v] : garrisonLosses)
      if (v > 0) r.garrisonLosses[k] = v;
    const Id leader = A->leader();
    const std::string pn = w.provinceName(province);
    rules::BattleOutcome out;
    if (!a.act("Штурм: " + pn, [&](Tx& tx) { out = rules::resolveSiege(tx, r); })) return;
    const bool won = out.winner == leader && a.world().army(army) != nullptr;
    if (won) a.toast("Гарнизон разбит: " + pn, ToastKind::Success, "castle");
    else if (out.winner) a.toast("Гарнизон устоял: " + pn, ToastKind::Warning, "castle");
    else a.toast("Обе стороны уничтожены", ToastKind::Warning, "skull");
    if (a.world().army(army)) a.select(SelType::Army, army);
    decided = true;
    auto fn = done;
    const Id ar = army, pid = province;
    detail::later(a, [fn, out, won, ar, pid](App& x) {
      auto finish = [fn](App& y) {
        if (fn) fn(y, true);
      };
      // Победа — захват; уничтожение войска — судьба героев (пленивший — владелец, захоронение — провинция).
      if (won) battleAftermath(x, out, false, [ar, pid, finish](App& y) { flow::openCapture(y, ar, pid, finish); });
      else battleAftermath(x, out, false, finish);
    });
  }

  bool draw(App& a) override {
    const World& w = frameWorld(a);
    const Army* A = w.army(army);
    const Province* P = w.province(province);
    if (!A || !P || !P->owner) {
      finish(a, false);
      return false;
    }
    if (*retreatOk) {
      retreat(a);
      return false;
    }
    const i64 leftA = unitCount(*A) - sumOf(armyLosses), leftG = garrisonCount(*P) - sumOf(garrisonLosses);
    const bool can[2] = {leftA > 0 || leftG <= 0, leftG > 0 || leftA <= 0};
    if (!can[winner]) winner = 1 - winner;
    winnerPicker(a, w, *A, *P, can);
    ui::spacer(2);
    float maxH = std::max(160.f, ui::viewport().h - 330);
    float h = std::min(cardsH > 0 ? cardsH + 2 : 360.f, maxH);
    {
      ui::Scroll sc("sides", h);
      ui::Row cols({ui::fr(1), ui::fr(1)}, ui::kAuto, 14);
      float keep = cardsH;
      cardsH = 0;
      {
        ui::Group g(0, 0);
        attackerCard(a, w, *A);
      }
      {
        ui::Group g(0, 0);
        garrisonCard(a, w, *P);
      }
      if (cardsH <= 0) cardsH = keep;
    }
    const i64 entered = sumOf(armyLosses) + sumOf(garrisonLosses);
    if (entered > 0) {
      ui::label("Введены потери (" + fmtCount(entered) + "): «Отступить» их не учтёт", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
      a.markUi("siege.retreatNote");
    }
    ui::ModalFooter f;
    if (ui::button("Отступить", {.icon = "retreat", .tooltip = "Войско вернётся на исходную позицию без потерь"})) {
      if (entered == 0) {
        retreat(a);
        return false;
      }
      auto ok = retreatOk;
      a.confirm("Отступить без потерь?", "Введённые потери (" + fmtCount(entered) + ") не будут учтены: войско вернётся на исходную позицию.",
                "Отступить без потерь", false, [ok](App&) { *ok = true; });
    }
    a.markUi("siege.retreat");
    {
      ui::Disabled dis(a.readOnly());
      if (ui::button("Применить итог", {.variant = ui::Variant::Primary, .icon = "check"})) {
        apply(a);
        if (decided) return false;
      }
      a.markUi("siege.apply");
    }
    return true;
  }
};

}  // namespace

void flow::openSiege(App& a, Id army, Id province, Vec2 origin, std::function<void(App&, bool)> done) {
  const World& w = a.world();
  auto fin = [done](App& x, bool applied) {
    if (done) done(x, applied);
  };
  std::string why;
  if (!rules::canSiege(w, army, province, &why)) {
    a.toast(why, ToastKind::Warning, "warning");
    detail::later(a, [fin](App& x) { fin(x, false); });
    return;
  }
  // ТЗ «Доработки», п.1 (как «Мятеж», п.3 у войск): мятежники штурмуют гарнизон прежнего государства с отрицательной
  // верностью — неверная часть гарнизона переходит к ним до боя, у оставшихся верность 0 %.
  if (rules::willGarrisonDefect(w, army, province)) {
    const i64 g0 = garrisonCount(*w.province(province));
    const std::string pn = w.provinceName(province);
    if (a.act("Переход к мятежникам: гарнизон " + pn, [&](Tx& tx) { rules::garrisonDefect(tx, army, province); })) {
      const Province* after = a.world().province(province);
      const i64 moved = g0 - (after ? garrisonCount(*after) : 0);
      a.toast("К мятежникам перешло " + fmtCount(moved) + " · гарнизон " + pn, ToastKind::Warning, "rebellion");
    }
  }
  // Гарнизона нет (или перешёл к мятежникам целиком) — битвы нет, сразу выбор захвата (п.4).
  if (garrisonCount(*a.world().province(province)) <= 0) {
    openCapture(a, army, province, [fin](App& x) { fin(x, true); });
    return;
  }
  auto d = std::make_unique<SiegeDialog>();
  d->army = army;
  d->province = province;
  d->origin = origin;
  d->done = std::move(done);
  a.openDialog(std::move(d));
}

void flow::rebelAftermath(App& a, Id rebelArmy, std::function<void(App&)> done) {
  auto fin = [done](App& x) {
    if (done) done(x);
  };
  const World& w = a.world();
  const Army* ar = w.army(rebelArmy);
  const Faction* f = ar ? w.faction(ar->leader()) : nullptr;
  const Id origin = f ? f->rebelOf : 0;
  const Id pid = ar && !ar->isFleet() ? mil::provinceUnder(w, ar->pos) : 0;
  const Province* p = w.province(pid);
  bool ok = origin && p && !p->sea && p->owner == origin;
  // В провинции нет других войск прежнего государства.
  if (ok)
    w.armies.each([&](const Army& x) {
      if (!ok || x.id == rebelArmy || x.isFleet()) return;
      for (const ArmyGroup& g : x.groups)
        if (g.faction == origin && mil::provinceUnder(w, x.pos) == pid) ok = false;
    });
  if (!ok || !rules::canSiege(w, rebelArmy, pid)) {
    detail::later(a, fin);
    return;
  }
  if (garrisonCount(*p) > 0) openSiege(a, rebelArmy, pid, ar->pos, [fin](App& x, bool) { fin(x); });
  else openCapture(a, rebelArmy, pid, fin);
}

}  // namespace rg::app
