// Regnum — контрразведка государства во вкладке «Заговоры» (ТЗ «Доработки №2», п.8): выделенная зона при «Штабе
// Контрразведки» — уровень и шанс, «Обнаружить заговоры» (игрок — раз в ход; итог — уведомлением, неудача броска не
// раскрывается), обнаруженные заговоры против государства без заказчика: сведения под «?» по раскрытым этапам
// (провинции, приверженность, герои, значимые герои, лидер), «Раскрыть заговор» и «Арестовать заговорщиков» с
// подтверждением по этапу; «Тюрьмы государства» — заключённые по расам и герои под арестом, освобождение. Действия —
// игровые за государство (App::canPlay, App::play).
#include "app/panels/plots_common.h"

namespace rg::app::plotui {

namespace {

// Раскрытые этапы — точки (раскрытые — золотом).
void stagePips(int stage) {
  const ui::Theme& th = ui::theme();
  const RectF r = ui::next(30);
  const float step = 12, x0 = r.x + 4;
  for (int i = 0; i < schema::kPlotStages; i++) {
    const float cx = x0 + step * float(i) + 4;
    if (i < stage) ui::draw::circle(cx, r.cy(), 4, th.accent);
    else ui::draw::ring(cx, r.cy(), 3.5f, 1.25f, th.borderStrong);
  }
  std::string tip = "Раскрыто этапов: " + std::to_string(stage) + " из " + std::to_string(schema::kPlotStages);
  if (stage > 0) tip += std::string(" — ") + stageName(stage);
  ui::hoverTip("##stages", RectF{r.x, r.y, step * schema::kPlotStages + 8, r.h}, tip);
}

void unknown(const std::string& mark) {
  ui::tag("?", ui::Tone::Neutral, "lock");
  app().markUi(mark);
}

void heroLines(App& a, const World& w, const Plot& p, bool vip) {
  bool any = false;
  for (Id h : p.heroes) {
    const Character* c = w.character(h);
    if (!c || c->hero != vip) continue;
    any = true;
    ui::IdScope s{i64(h)};
    ui::label(heroName(w, h), {.font = ui::Font::Small, .icon = vip ? "star-filled" : "hero"});
    a.markUi("ci.info.hero." + std::to_string(h));
  }
  if (!any) ui::label("—", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
}

// Сведения контрразведки о заговоре — только раскрытое, остальное «?»; заказчик не показывается.
void infoPopup(App& a, const World& w, const Plot& p, const std::string& name) {
  if (!ui::beginPopup("info", {.side = ui::Side::Below, .width = 300})) return;
  const int st = p.stage;
  ui::label(name, {.font = ui::Font::Strong, .icon = "eye-off"});
  ui::label("Раскрыто этапов: " + std::to_string(st) + " из " + std::to_string(schema::kPlotStages), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  ui::caption("Провинции");
  if (st < 1) {
    unknown("ci.info.unknown.provinces");
  } else {
    if (p.provinces.empty()) ui::label("—", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    for (const PlotProvince& pp : p.provinces) {
      ui::IdScope s{i64(pp.province)};
      ui::Row r({ui::fr(1), ui::px(56)}, 22, 6);
      ui::label(provName(w, pp.province), {.font = ui::Font::Small, .icon = "province"});
      a.markUi("ci.info.prov." + std::to_string(pp.province));
      ui::label(st >= 2 ? fmtPct(pp.commitment) : std::string("?"), {.font = ui::Font::Small, .ink = st >= 2 ? ui::Ink::Normal : ui::Ink::Muted,
                                                                    .align = ui::Align::Right, .tooltip = "Приверженность заговору"});
      a.markUi("ci.info.commit." + std::to_string(pp.province));
    }
  }
  ui::caption("Герои");
  if (st < 3) unknown("ci.info.unknown.heroes");
  else heroLines(a, w, p, false);
  ui::caption("Значимые герои фракции");
  if (st < 4) unknown("ci.info.unknown.vip");
  else heroLines(a, w, p, true);
  ui::caption("Лидер");
  if (st < 5) {
    unknown("ci.info.unknown.leader");
  } else if (p.leader) {
    ui::label(heroName(w, p.leader), {.font = ui::Font::Small, .ink = ui::Ink::Accent, .icon = "crown"});
    a.markUi("ci.info.leader");
  } else {
    ui::label("—", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  ui::endPopup();
}

void reveal(App& a, Id state, Id plot) {
  const bool pl = a.player();
  bool ok = false;
  int stage = 0;
  if (!a.play("Раскрыть заговор", [&](Tx& tx) {
        ok = rules::revealPlot(tx, state, plot, pl);
        stage = tx.w().plot(plot)->stage;
      }))
    return;
  if (ok) a.toast("Заговор раскрыт: этап " + std::to_string(stage) + " — " + stageName(stage), ToastKind::Success, "eye");
  else a.toast("Раскрыть заговор не удалось", ToastKind::Warning, "eye-off");
}

void foundRow(App& a, const World& w, Id state, const Plot& p, int n, bool play) {
  const Id plot = p.id;
  ui::IdScope sc{i64(plot)};
  const std::string mark = "ci.plot." + std::to_string(plot);
  const std::string name = "Заговор " + std::to_string(n);
  ui::Row r({ui::px(16), ui::fr(1, 70), ui::px(70), ui::px(26), ui::px(26), ui::px(26)}, 30, 6);
  ui::icon("eye-off", ui::Ink::Warning, 16);
  ui::label(name, {.font = ui::Font::Strong});
  a.markUi(mark);
  stagePips(p.stage);
  a.markUi(mark + ".stages");
  if (ui::iconButton("help", "Сведения о заговоре", {.size = ui::Size::Small})) ui::openPopup("info");
  a.markUi(mark + ".info");
  infoPopup(a, w, p, name);
  const std::string why = rules::revealProblem(w, state, plot, a.player());
  if (ui::iconButton("eye", why.empty() ? std::string("Раскрыть заговор") : "Раскрыть заговор\n" + why, {.size = ui::Size::Small, .disabled = !play || !why.empty()}))
    reveal(a, state, plot);
  a.markUi(mark + ".reveal");
  if (p.stage >= 1) {
    if (ui::iconButton("shackles", "Арестовать заговорщиков", {.size = ui::Size::Small, .disabled = !play, .tone = ui::Tone::Danger})) openArrest(a, state, plot, name);
    a.markUi(mark + ".arrest");
  } else {
    ui::next(26, 26);
  }
}

// «Тюрьмы государства»: заключённые по расам и герои под арестом.
void prisons(App& a, const World& w, const Faction& f, bool play) {
  const Id sid = f.id;
  const std::vector<Id> heroes = rules::prisonHeroes(w, sid);
  i64 total = 0;
  for (const RacePop& r : f.prison) total += std::max<i64>(0, r.pop);
  if (total <= 0 && heroes.empty()) return;
  ui::separator();
  {
    const std::string lbl = "Освободить";
    ui::Row r({ui::fr(1, 80), ui::px(std::round(ui::measure(lbl, ui::Font::Body) + 44))}, 26, 8);
    ui::label("Тюрьмы государства", {.font = ui::Font::Strong, .icon = "shackles"});
    a.markUi("prison");
    if (total > 0) {
      if (ui::button(lbl, {.icon = "unlock", .size = ui::Size::Small, .fill = true, .disabled = !play, .tooltip = "Освободить заключённых"})) openRelease(a, sid);
      a.markUi("prison.release");
    } else {
      ui::next(0, 0);
    }
  }
  if (total > 0) {
    ui::ChipFlow flow;
    for (const RacePop& r : f.prison) {
      if (r.pop <= 0) continue;
      ui::IdScope s{i64(r.race)};
      ui::flowChip(raceName(w, r.race) + " · " + fmtInt(r.pop), {.icon = "population", .tooltip = "Заключённые"});
    }
  }
  if (total > 0) a.markUi("prison.people");
  for (Id h : heroes) {
    const Character* c = w.character(h);
    if (!c) continue;
    ui::IdScope s{i64(h)};
    ui::Row r({ui::px(30), ui::fr(1, 80), ui::px(26)}, 34, 8);
    w::heroAvatar(*c, 28, false, {}, w::factionColor(w, c->faction));
    {
      ui::Group g(0, 0);
      ui::label(heroName(w, h), {.font = ui::Font::Strong, .tooltip = "Персонаж — открыть"});
      if (ui::lastItem().hovered) ui::setCursor(platform::Cursor::Hand);
      if (ui::lastItem().clicked) a.select(SelType::Character, h);
      ui::label("Под арестом", {.font = ui::Font::Small, .ink = ui::Ink::Warning});
    }
    if (ui::iconButton("unlock", "Освободить", {.size = ui::Size::Small, .disabled = !play}))
      a.play("Освободить героя из тюрьмы", [&](Tx& tx) { rules::releaseHero(tx, sid, h); });
    a.markUi("prison.hero." + std::to_string(h));
  }
}

}  // namespace

void counterIntel(App& a, const World& w, const Faction& f) {
  const Id sid = f.id;
  const bool play = a.canPlay(sid);
  const int level = rules::counterIntelLevel(w, sid);
  ui::IdScope scope("ci");
  {
    ui::Card zone({.pad = 12, .icon = "shield", .title = "Контрразведка", .tone = ui::Tone::Info});
    if (level < 1) {
      ui::label("Нужен «" + rules::buildingKeyName(w, sid, schema::bld::CounterIntelHq) + "»", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "lock"});
      a.markUi("ci.locked");
    } else {
      const std::string why = rules::detectProblem(w, sid, a.player());
      const std::string lbl = "Обнаружить заговоры";
      const float bw = std::min(std::round(ui::measure(lbl, ui::Font::Body) + 48), std::round(ui::avail().w * 0.6f));
      {
        ui::Row r({ui::fr(1, 60), ui::px(bw)}, 30, 8);
        {
          ui::HStack hs(24, ui::Align::Left, 6);
          tagTip("Ур. " + std::to_string(level), ui::Tone::Info, "shield", "«" + rules::buildingKeyName(w, sid, schema::bld::CounterIntelHq) + "» " + std::to_string(level) + " уровня");
          tagTip(fmtPct(rules::detectChance(w, sid)), ui::Tone::Neutral, "target", "Шанс обнаружить и раскрыть заговор");
          a.markUi("ci.chance");
        }
        if (ui::button(lbl, {.icon = "search", .fill = true, .disabled = !play || !why.empty(), .tooltip = why})) {
          const bool pl = a.player();
          Id found = 0;
          if (a.play("Обнаружить заговоры", [&](Tx& tx) { found = rules::detectPlots(tx, sid, pl); })) {
            if (found) a.toast("Обнаружен заговор против государства", ToastKind::Warning, "eye");
            else a.toast("Заговоров не обнаружено", ToastKind::Info, "shield");
          }
        }
        a.markUi("ci.detect");
      }
      const std::vector<Id> found = rules::foundPlots(w, sid);
      if (found.empty()) ui::label("Обнаруженных заговоров нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      for (size_t i = 0; i < found.size(); i++)
        if (const Plot* p = w.plot(found[i])) foundRow(a, w, sid, *p, int(i) + 1, play);
    }
    prisons(a, w, f, play);
  }
  a.markUi("ci.zone");
}

}  // namespace rg::app::plotui
