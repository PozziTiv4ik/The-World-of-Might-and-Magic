// Regnum — вкладка государства «Заговоры» (ТЗ «Доработки №2», п.1, 3, 4, 5, 7): слева — заговоры государства
// (внешние, где оно заказчик; администратору — и внутренние против него) и контрразведка с тюрьмами
// (panels/plots_counter.cpp), справа — выбранный заговор: государство-цель, «Заговор частично раскрыт!», провинции
// заговора (приверженность, «Спонсировать заговор», «Снабдить вооружением», восстание в подсказке), герои заговора
// (участники, приглашённые, слот лидера), «Угрозы заговору» с заказными убийствами и «Осуществить заговор». Игроку
// вкладка видна только у своего государства; внутренние заговоры и правка приверженности — только администратору.
#include "app/panels/faction_common.h"
#include "app/panels/plots_common.h"

namespace rg::app {
namespace {

using namespace plotui;

constexpr float kWide = 780;     // шире — список и контрразведка слева, заговор справа
constexpr float kTiles2 = 560;   // шире — плитки провинций в два столбца
constexpr float kLeader = 64;    // слот лидера

// ---------------------------------------------------------------- список заговоров
// Строка заговора: флаг цели, название, состав; частично раскрыт — глаз, приглашённые ждут решения (администратор).
bool plotRow(App& a, const World& w, const Plot& p, bool selected) {
  ui::IdScope sc{i64(p.id)};
  const ui::Theme& th = ui::theme();
  const RectF r = ui::next(48);
  const ui::Interaction it = ui::interact(ui::id("##row"), r, ui::IfFocusable);
  const float hv = ui::animate(ui::id("##hover"), it.hovered ? 1.f : 0.f, 0.1f);
  if (selected) {
    ui::draw::rect(r, th.accent.alpha(0.14f), 7);
    ui::draw::rect(RectF{r.x, r.y + 9, 3, r.h - 18}, th.accent, 1.5f);
  } else if (hv > 0.01f) {
    ui::draw::rect(r, (it.held ? th.pressed : th.hover).alpha(std::min(1.f, hv * 1.4f)), 7);
  }
  if (it.hovered) ui::setCursor(platform::Cursor::Hand);
  float x = r.x + 12;
  if (const Faction* t = w.faction(p.target)) {
    ui::at(RectF{x, r.cy() - 11, 33, 22});
    ui::flag(t->flag, 33, 22, 3);
  }
  x += 45;
  float right = r.right() - 10;
  if (a.admin() && !p.invited.empty()) {
    const std::string n = std::to_string(p.invited.size());
    const float bw = std::max(20.f, ui::measure(n, ui::Font::Caption) + 12);
    const RectF br{right - bw, r.cy() - 9, bw, 18};
    ui::draw::rect(br, th.warning.alpha(0.2f), 9);
    ui::draw::text(n, br, ui::Font::Caption, th.warning, ui::Align::Center);
    ui::hoverTip("##invited", br, "Приглашены в заговор — ждут решения: " + n);
    right = br.x - 6;
  }
  if (p.alarm) {
    const RectF ir{right - 16, r.cy() - 8, 16, 16};
    ui::draw::icon("eye", ir, th.danger);
    ui::hoverTip("##alarm", ir, "Заговор частично раскрыт");
    right = ir.x - 8;
  }
  if (!p.external()) {
    const RectF ir{right - 16, r.cy() - 8, 16, 16};
    ui::draw::icon("eye-off", ir, th.info);
    ui::hoverTip("##internal", ir, schema::plotKind(PlotKind::Internal).name);
    right = ir.x - 8;
  }
  const size_t nh = p.heroes.size();
  const std::string sub = std::to_string(p.provinces.size()) + " " + plural(i64(p.provinces.size()), "провинция", "провинции", "провинций") + " · " +
                          std::to_string(nh) + " " + plural(i64(nh), "герой", "героя", "героев");
  const float lh = ui::lineHeight(ui::Font::Body), sh = ui::lineHeight(ui::Font::Small);
  const float y0 = std::round(r.cy() - (lh + sh) * 0.5f);
  const std::string title = rules::plotTitle(w, p);
  ui::draw::text(title, RectF{x, y0, right - x, lh}, selected ? ui::Font::Strong : ui::Font::Body, th.text);
  ui::draw::text(sub, RectF{x, y0 + lh, right - x, sh}, ui::Font::Small, th.textMuted);
  if (ui::measure(title, ui::Font::Strong) > right - x) ui::hoverTip("##title", r, title);
  a.markUi("plots.row." + std::to_string(p.id), r);
  return it.clicked;
}

void listSection(App& a, const World& w, const Faction& f, const std::vector<Id>& list, Id& sel, bool wide) {
  const Id fid = f.id;
  const bool create = a.canPlay(fid);
  ui::Section s("Заговоры", "eye-off", {.badge = list.empty() ? std::string() : std::to_string(list.size()), .actionIcon = create ? "plus" : nullptr,
                                        .actionTooltip = "Создать заговор"});
  const RectF hdr = ui::lastItem().rect;
  a.markUi("plots.list");
  if (create) a.markUi("plots.create", RectF{hdr.right() - ui::theme().padCard + 4 - 26, hdr.cy() - 13, 26, 26});
  if (s.action()) openCreate(a, fid);
  if (!s) return;
  if (list.empty()) {
    if (!wide) {
      ui::Disabled d(!create);
      if (ui::emptyState("eye-off", "Заговоров нет.", "Создать заговор", "plus")) openCreate(a, fid);
      a.markUi("plots.empty");
    }
    return;
  }
  for (Id id : list)
    if (const Plot* p = w.plot(id))
      if (plotRow(a, w, *p, id == sel)) sel = id;
}

// ---------------------------------------------------------------- заговор: шапка
void header(App& a, const World& w, const Plot& p, bool manage) {
  const Id plot = p.id;
  const Faction* t = w.faction(p.target);
  const std::string title = rules::plotTitle(w, p);
  {
    ui::Row r({ui::px(54), ui::fr(1, 120), ui::px(28), ui::px(28)}, ui::kAuto, 10);
    if (t) ui::flag(t->flag, 54, 36, 4, "Государство-цель: " + w::orName(t->name));
    else ui::next(54, 36);
    a.markUi("plot.flag");
    {
      ui::Group g(0, 2);
      ui::label(title, {.font = ui::Font::Title});
      a.markUi("plot.title");
      ui::HStack hs(24, ui::Align::Left, 6);
      if (p.external()) ui::label("против", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      else ui::tag("Внутренний заговор", ui::Tone::Info, "eye-off");
      w::factionChip(p.target);
    }
    if (ui::iconButton("edit", "Переименовать заговор", {.size = ui::Size::Small, .disabled = !manage})) {
      const bool ext = p.external();
      a.prompt("Название заговора", {}, p.name.empty() ? title : p.name, [plot, ext](App& x, const std::string& v) {
        const Plot* q = x.world().plot(plot);
        if (q && q->external() == ext) plotAct(x, *q, "Название заговора", [&](Tx& tx) { rules::renamePlot(tx, plot, v); });
      });
    }
    a.markUi("plot.rename");
    if (ui::iconButton("trash", "Удалить заговор", {.size = ui::Size::Small, .disabled = !manage, .tone = ui::Tone::Danger}))
      plotConfirm(a, p, "Удалить заговор «" + title + "»?", p.hits.empty() ? std::string() : "Золото заказов вернётся заказчику.", "Удалить",
                  "Удалить заговор", [plot](Tx& tx) { rules::removePlot(tx, plot); });
    a.markUi("plot.delete");
  }
  if (p.alarm) {
    {
      ui::Card c({.pad = 10, .tone = ui::Tone::Danger});
      ui::label("Заговор частично раскрыт! Государство «" + (t ? w::orName(t->name) : std::string("—")) + "» расследует этот заговор",
                {.font = ui::Font::Strong, .ink = ui::Ink::Danger, .icon = "eye", .wrap = true});
    }
    a.markUi("plot.alarm");
  }
}

// ---------------------------------------------------------------- провинции
// Восставшие и отряды провинции — подсказка над значком.
void riseTip(const World& w, const PlotProvince& pp, RectF r) {
  if (!ui::beginTooltipOver("##rise", r, 300)) return;
  const i64 rebels = rules::plotRebels(w, pp);
  ui::label("Восстанут: " + fmtInt(rebels), {.font = ui::Font::Strong, .icon = "rebellion"});
  const std::vector<rules::PlotSquad> squads = rules::plotSquads(w, pp, rebels);
  if (squads.empty()) ui::label("Отрядов нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  for (size_t i = 0; i < squads.size(); i++) {
    ui::IdScope s{int(i)};
    ui::Row row({ui::px(18), ui::fr(1), ui::px(64)}, 20, 6);
    ui::icon(squadIcon(squads[i]), ui::Ink::Dim, 16);
    ui::label(squads[i].name, {.font = ui::Font::Small});
    ui::label(fmtInt(squads[i].count), {.font = ui::Font::Small, .ink = ui::Ink::Dim, .align = ui::Align::Right});
  }
  ui::endTooltip();
}

// Отданное снабжение значками (сколько поместится в ширину room, остальное — «+N»).
void suppliesRow(const World& w, const PlotProvince& pp, float room) {
  std::vector<std::pair<Id, double>> list;
  for (auto& [r, v] : pp.supplies)
    if (v > 0) list.push_back({r, v});
  std::stable_sort(list.begin(), list.end(), [](auto& x, auto& y) { return x.second > y.second; });
  float used = 0;
  std::vector<std::string> rest;
  for (size_t i = 0; i < list.size(); i++) {
    const auto [r, v] = list[i];
    const std::string n = w::resourceName(w, r);
    const std::string amt = fmtNum(v, 3);
    const float need = 16 + 3 + ui::measure(amt, ui::Font::Small) + 8;
    if (used + need > room - (i + 1 < list.size() ? 30 : 0)) {
      rest.push_back(n + " — " + amt);
      continue;
    }
    used += need;
    ui::IdScope s{i64(r)};
    ui::iconColored(w::resourceIcon(w, r), w::legible(w::resourceColor(w, r)), 16, "Отдано заговорщикам: " + n + " — " + amt);
    ui::label(amt, {.font = ui::Font::Small, .ink = ui::Ink::Dim});
  }
  if (!rest.empty()) ui::label("+" + std::to_string(rest.size()), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .tooltip = join(rest, "\n")});
}

void provinceTile(App& a, const World& w, const Plot& p, const PlotProvince& pp, bool manage) {
  const Id plot = p.id, pid = pp.province;
  ui::IdScope sc{i64(pid)};
  const std::string mark = "plot.prov." + std::to_string(pid);
  {
    ui::Card card({.pad = 10});
    {
      ui::Row r({ui::fr(1, 60), ui::px(26), ui::px(26)}, 26, 4);
      ui::label(provName(w, pid), {.font = ui::Font::Strong, .icon = "province", .tooltip = "Провинция — открыть и показать на карте"});
      if (ui::lastItem().hovered) ui::setCursor(platform::Cursor::Hand);
      if (ui::lastItem().clicked) a.select(SelType::Province, pid, true);
      if (ui::iconButton("swords", "Снабдить вооружением", {.size = ui::Size::Small, .disabled = !manage})) openSupply(a, plot, pid);
      a.markUi(mark + ".supply");
      if (ui::iconButton("close", "Убрать провинцию из заговора", {.size = ui::Size::Small, .disabled = !manage, .tone = ui::Tone::Danger}))
        plotAct(a, p, "Убрать провинцию из заговора", [&](Tx& tx) { rules::removePlotProvince(tx, plot, pid); });
      a.markUi(mark + ".remove");
    }
    {
      ui::Row r({ui::fr(1), ui::px(52)}, 18, 4);
      ui::label("Приверженность заговору", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      ui::label(fmtPct(pp.commitment), {.font = ui::Font::Strong, .align = ui::Align::Right});
      a.markUi(mark + ".value");
    }
    if (a.canEdit()) {
      double v = pp.commitment;
      if (ui::slider("commit", v, 0, 100, {.step = 1, .unit = "%", .showValue = false}))
        a.act("Приверженность заговору", [&](Tx& tx) { rules::setPlotCommitment(tx, plot, pid, v); },
              {.coalesce = "plot.commit:" + std::to_string(plot) + ":" + std::to_string(pid)});
      a.markUi(mark + ".commit");
    } else {
      ui::progress(std::clamp(pp.commitment / 100.0, 0.0, 1.0), {.tone = ui::Tone::Accent, .height = 6});
      a.markUi(mark + ".bar");
    }
    {
      const std::string why = rules::sponsorProblem(w, plot, pid);
      const double cost = p.external() ? rules::sponsorCost(w, pid) : 0;
      const std::string label = cost > 0 ? "Спонсировать · " + fmtGold(cost) : std::string("Спонсировать");
      const std::string step = "+" + fmtNum(schema::kPlotSponsorStep) + " % приверженности";
      const std::string tip = !why.empty() ? "Спонсировать заговор\n" + why
                              : cost > 0   ? "Спонсировать заговор: " + step + " за " + fmtGold(cost) + " золота, раз в ход"
                                           : "Спонсировать заговор: " + step + ", раз в ход";
      const float total = ui::avail().w;
      const float bw = std::min(std::round(ui::measure(label, ui::Font::Body) + 44), std::round(total * 0.62f));
      ui::Row r({ui::fr(1, 40), ui::px(bw)}, 26, 6);
      {
        ui::HStack hs(24, ui::Align::Left, 4);
        const i64 rebels = rules::plotRebels(w, pp);
        ui::tag(fmtInt(rebels), rebels > 0 ? ui::Tone::Warning : ui::Tone::Neutral, "rebellion");
        const RectF tr = ui::lastItem().rect;
        a.markUi(mark + ".rise");
        riseTip(w, pp, tr);
        suppliesRow(w, pp, total - bw - 6 - tr.w - 10);
      }
      if (ui::button(label, {.icon = "coins", .size = ui::Size::Small, .fill = true, .disabled = !manage || !why.empty(), .tooltip = tip}))
        plotAct(a, p, "Спонсировать заговор", [&](Tx& tx) { rules::sponsorPlot(tx, plot, pid); });
      a.markUi(mark + ".sponsor");
    }
  }
  a.markUi(mark);
}

void addProvinceCombo(App& a, const World& w, const Plot& p) {
  const bool pl = a.player();
  struct Cand {
    Id id;
    std::string name, why;
    double content;
  };
  std::vector<Cand> list;
  w.provinces.each([&](const Province& pr) {
    if (pr.owner != p.target || pr.sea || p.province(pr.id)) return;
    std::string why = rules::plotProvinceProblem(w, p.id, pr.id, pl);
    // Игроку — без чужих тайн: что провинция в другом заговоре, не раскрывается.
    if (pl && why.find("другом заговоре") != std::string::npos) why = "Недоступна для заговора";
    list.push_back(Cand{pr.id, w::orName(pr.name), why, pr.contentment});
  });
  std::sort(list.begin(), list.end(), [](const Cand& x, const Cand& y) {
    if (x.why.empty() != y.why.empty()) return x.why.empty();
    return compareRu(x.name, y.name) < 0;
  });
  w::IdOptions o;
  o.reserve(list.size());
  for (const Cand& c : list) o.add(c.id, c.name, "province", Color(0, 0, 0, 0), c.why.empty() ? "довольство " + fmtSigned(c.content) : c.why, !c.why.empty());
  const Id pick = w::addCombo("addProvince", o,
                              {.placeholder = list.empty() ? "Провинций цели нет" : "Добавить провинцию…", .search = 1, .icon = "plus",
                               .disabled = list.empty(), .popupWidth = 440, .tooltip = "Добавить провинцию в заговор"});
  a.markUi("plot.addProvince");
  if (pick) {
    const Id plot = p.id;
    plotAct(a, p, "Провинция в заговор", [&](Tx& tx) { rules::addPlotProvince(tx, plot, pick, pl); });
  }
}

void provincesSection(App& a, const World& w, const Plot& p, bool manage) {
  ui::Section s("Провинции", "province", {.badge = p.provinces.empty() ? std::string() : std::to_string(p.provinces.size())});
  a.markUi("plot.provinces");
  if (!s) return;
  if (p.provinces.empty()) ui::label("Провинций нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  if (ui::avail().w >= kTiles2 && p.provinces.size() > 1) {
    ui::Row r({ui::fr(1), ui::fr(1)}, ui::kAuto, 8);
    for (const PlotProvince& pp : p.provinces) {
      ui::Group g(0, 0);
      provinceTile(a, w, p, pp, manage);
    }
  } else {
    for (const PlotProvince& pp : p.provinces) provinceTile(a, w, p, pp, manage);
  }
  if (manage) addProvinceCombo(a, w, p);
}

// ---------------------------------------------------------------- герои
enum class HeroKind { Member, Invited, Threat };

// Кто герой в своём государстве: состояние («Мертв», «В плену», «Под арестом»), правитель, советник или титул; пусто — никто.
std::string roleOf(const World& w, const Character& c) {
  if (std::string st = w::heroState(w, c); !st.empty()) return st;
  if (rules::characterHas(w, c.id, schema::mod::Arrested)) return "Под арестом";
  if (const Faction* f = w.faction(c.faction)) {
    if (f->ruler == c.id) return f->rulerTitle.empty() ? std::string("Правитель") : f->rulerTitle;
    for (const CouncilSeat& s : f->council)
      if (s.character == c.id) return s.position.empty() ? std::string("Советник") : s.position;
  }
  return c.title;
}

void heroRow(App& a, const World& w, const Plot& p, Id cid, HeroKind kind, bool manage) {
  const Character* c = w.character(cid);
  if (!c) return;
  ui::IdScope sc{i64(cid)};
  const Id plot = p.id;
  const bool admin = a.canEdit();
  const PlotHit* hit = p.hit(cid);
  const std::string mark = std::string(kind == HeroKind::Threat ? "plot.threat." : kind == HeroKind::Invited ? "plot.invite." : "plot.hero.") + std::to_string(cid);
  int buttons = 0;
  if (manage) {
    if (kind == HeroKind::Member) buttons = 1;
    else if (kind == HeroKind::Invited) buttons = admin ? 2 : 1;
    else buttons = 2 + (hit ? 1 : 0) + (admin && hit ? 1 : 0);
  }
  const std::string name = heroName(w, cid);
  ui::Row r({ui::px(36), ui::fr(1, 80), ui::px(float(buttons) * 28)}, 44, 8);
  w::heroAvatar(*c, 34, p.leader == cid, {}, w::factionColor(w, c->faction));
  if (kind == HeroKind::Invited) {
    const RectF ar = ui::lastItem().rect;
    ui::draw::circle(ar.cx(), ar.cy(), 17, ui::theme().surface2.alpha(0.5f));
  }
  {
    ui::Group g(0, 0);
    const bool second = (kind == HeroKind::Member && p.leader == cid) || kind == HeroKind::Threat || !roleOf(w, *c).empty();
    if (!second) ui::next(0, 10);
    {
      ui::HStack hs(20, ui::Align::Left, 5);
      ui::label(name, {.font = ui::Font::Strong, .ink = kind == HeroKind::Invited ? ui::Ink::Dim : ui::Ink::Normal, .tooltip = "Персонаж — открыть"});
      if (ui::lastItem().hovered) ui::setCursor(platform::Cursor::Hand);
      if (ui::lastItem().clicked) a.select(SelType::Character, cid);
      if (kind == HeroKind::Member && manage) ui::dragSource("plot.hero", u64(cid), name, "hero");
      a.markUi(mark + ".name");
      if (c->hero) ui::icon("star-filled", ui::Ink::Accent, 14, "Значимый герой фракции");
      if (kind == HeroKind::Invited) ui::label("приглашен в заговор", {.font = ui::Font::Small, .ink = ui::Ink::Warning});
    }
    if (kind == HeroKind::Threat && hit) {
      const rules::HitChance hc = rules::hitChance(w, plot, cid, hit->gold);
      ui::label("Выполняется заказное убийство · " + fmtGold(hit->gold) + " · " + fmtPct(hc.total),
                {.font = ui::Font::Small, .ink = ui::Ink::Danger, .icon = "crosshair",
                 .tooltip = hc.guarded ? std::string_view("Защищён от убийства") : std::string_view("Исход — после хода")});
      a.markUi(mark + ".status");
    } else if (kind == HeroKind::Member && p.leader == cid) {
      ui::label("Лидер заговора", {.font = ui::Font::Small, .ink = ui::Ink::Accent, .icon = "crown"});
    } else if (kind == HeroKind::Threat) {
      ui::label("Отказался от заговора", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    } else if (const std::string role = roleOf(w, *c); !role.empty()) {
      ui::label(role, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    }
  }
  ui::HStack hs(28, ui::Align::Right, 2);
  if (!manage) return;
  auto remove = [&](const char* tip, const char* label) {
    if (ui::iconButton("close", tip, {.size = ui::Size::Small, .tone = ui::Tone::Danger}))
      plotAct(a, p, label, [&](Tx& tx) { rules::removePlotHero(tx, plot, cid); });
    a.markUi(mark + ".remove");
  };
  switch (kind) {
    case HeroKind::Member: remove("Убрать из заговора", "Убрать героя из заговора"); break;
    case HeroKind::Invited:
      if (admin) {
        if (ui::iconButton("check", "Согласился — участник заговора", {.size = ui::Size::Small, .tone = ui::Tone::Success}))
          a.act("Герой согласился участвовать в заговоре", [&](Tx& tx) { rules::answerPlotInvite(tx, plot, cid, true); });
        a.markUi(mark + ".yes");
        if (ui::iconButton("close", "Отказался — угроза заговору", {.size = ui::Size::Small, .tone = ui::Tone::Danger}))
          a.act("Герой отказался участвовать в заговоре", [&](Tx& tx) { rules::answerPlotInvite(tx, plot, cid, false); });
        a.markUi(mark + ".no");
      } else {
        remove("Отозвать приглашение", "Отозвать приглашение в заговор");
      }
      break;
    case HeroKind::Threat:
      if (ui::iconButton("crosshair", hit ? "Изменить заказ убийства" : "Заказать убийство", {.size = ui::Size::Small, .tone = ui::Tone::Danger}))
        openHit(a, plot, cid);
      a.markUi(mark + ".hit");
      if (hit) {
        if (ui::iconButton("undo", "Отменить заказ — золото вернётся", {.size = ui::Size::Small}))
          plotAct(a, p, "Отменить заказное убийство", [&](Tx& tx) { rules::cancelHit(tx, plot, cid); });
        a.markUi(mark + ".cancel");
        if (admin) {
          bool on = hit->guarded;
          if (ui::iconToggle("shield", "Защитить от убийства", on, {.size = ui::Size::Small}))
            a.act("Защитить от убийства", [&](Tx& tx) { rules::guardHit(tx, plot, cid, on); });
          a.markUi(mark + ".guard");
        }
      }
      if (ui::iconButton("trash", "Убрать из угроз заговору", {.size = ui::Size::Small}))
        plotAct(a, p, "Убрать угрозу заговору", [&](Tx& tx) { rules::removePlotHero(tx, plot, cid); });
      a.markUi(mark + ".remove");
      break;
  }
}

// Слот лидера: портрет в круге; щелчок — выбор из участников, участника можно перетащить на слот.
void leaderSlot(App& a, const World& w, const Plot& p, bool manage) {
  const ui::Theme& th = ui::theme();
  const Id plot = p.id;
  const Character* L = w.character(p.leader);
  ui::Group g(0, 4);
  const RectF slot = ui::next(kLeader + 4);
  const RectF r{std::round(slot.cx() - kLeader * 0.5f), slot.y + 2, kLeader, kLeader};
  ui::at(r);
  const std::string tip = L ? "Лидер заговора: " + heroName(w, p.leader) : std::string("Лидер заговора");
  if (L) {
    w::heroAvatar(*L, kLeader, false, tip, w::factionColor(w, L->faction));
  } else {
    ui::iconColored("crown", Color(0, 0, 0, 0), kLeader, tip);
  }
  const ui::Item it = ui::lastItem();
  const float hv = ui::animate(ui::id("##leaderHover"), it.hovered && manage ? 1.f : 0.f, 0.1f);
  if (L) {
    ui::draw::ring(r.cx(), r.cy(), kLeader * 0.5f + 3, 2, th.accent);
  } else {
    ui::draw::circle(r.cx(), r.cy(), kLeader * 0.5f, th.surface3.alpha(0.6f + 0.4f * hv));
    ui::draw::ring(r.cx(), r.cy(), kLeader * 0.5f - 1, 1.5f, th.borderStrong);
    ui::draw::icon("crown", r.inset(20), th.textMuted);
  }
  if (hv > 0.01f) ui::draw::ring(r.cx(), r.cy(), kLeader * 0.5f + 3, 2, th.accentHover.alpha(hv));
  a.markUi("plot.leader", r);
  std::optional<Id> pick;
  if (manage) {
    if (it.hovered) ui::setCursor(platform::Cursor::Hand);
    if (auto d = ui::dropTarget("plot.hero")) pick = Id(*d);
    if (it.clicked) ui::openPopup("leaderMenu");
    if (ui::beginMenu("leaderMenu")) {
      ui::menuHeader("Лидер заговора");
      if (p.heroes.empty()) ui::menuItem("Нет участников", {.icon = "hero", .disabled = true});
      for (Id h : p.heroes) {
        ui::IdScope s{i64(h)};
        const Character* c = w.character(h);
        if (ui::menuItem(heroName(w, h), {.icon = c && c->hero ? "hero" : "character", .checked = p.leader == h})) pick = h;
        a.markUi("plot.leader.pick." + std::to_string(h));
      }
      if (p.leader) {
        ui::menuSeparator();
        if (ui::menuItem("Снять лидера", {.icon = "close"})) pick = Id(0);
        a.markUi("plot.leader.clear");
      }
      ui::endMenu();
    }
  }
  ui::label(L ? heroName(w, p.leader) : std::string("Лидер"), {.font = ui::Font::Small, .ink = L ? ui::Ink::Accent : ui::Ink::Muted, .align = ui::Align::Center});
  if (pick && *pick != p.leader) {
    const Id h = *pick;
    plotAct(a, p, h ? "Лидер заговора" : "Снять лидера заговора", [&](Tx& tx) { rules::setPlotLeader(tx, plot, h); });
  }
}

void addHeroCombo(App& a, const World& w, const Plot& p) {
  const bool pl = a.player();
  struct Cand {
    Id id;
    std::string name, why;
    bool hero;
    std::string title;
  };
  std::vector<Cand> list;
  w.characters.each([&](const Character& c) {
    if (c.faction != p.target || rules::characterHas(w, c.id, schema::mod::Dead)) return;
    if (rules::plotOfHero(w, c.id) == p.id) return;
    std::string why = rules::plotHeroProblem(w, p.id, c.id, pl);
    if (pl && why.find("другом заговоре") != std::string::npos) why = "Недоступен для заговора";
    list.push_back(Cand{c.id, w::orName(c.name, "Без имени"), why, c.hero, c.title});
  });
  std::sort(list.begin(), list.end(), [](const Cand& x, const Cand& y) {
    if (x.why.empty() != y.why.empty()) return x.why.empty();
    if (x.hero != y.hero) return x.hero;
    return compareRu(x.name, y.name) < 0;
  });
  w::IdOptions o;
  o.reserve(list.size());
  for (const Cand& c : list) o.add(c.id, c.name, c.hero ? "hero" : "character", Color(0, 0, 0, 0), c.why.empty() ? c.title : c.why, !c.why.empty());
  const Id pick = w::addCombo("addHero", o,
                              {.placeholder = list.empty() ? "Героев цели нет" : pl ? "Пригласить героя…" : "Добавить героя…", .search = 1, .icon = "user-plus",
                               .disabled = list.empty(), .popupWidth = 440, .tooltip = pl ? "Пригласить героя в заговор" : "Добавить героя в заговор"});
  a.markUi("plot.addHero");
  if (pick) {
    const Id plot = p.id;
    plotAct(a, p, pl ? "Пригласить героя в заговор" : "Герой в заговор", [&](Tx& tx) { rules::addPlotHero(tx, plot, pick, pl); });
  }
}

void heroesSection(App& a, const World& w, const Plot& p, bool manage) {
  const size_t n = p.heroes.size() + p.invited.size();
  ui::Section s("Герои заговора", "hero", {.badge = n ? std::to_string(n) : std::string()});
  a.markUi("plot.heroes");
  if (!s) return;
  auto rows = [&] {
    if (p.heroes.empty() && p.invited.empty()) ui::label("Героев нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    for (Id h : p.heroes) heroRow(a, w, p, h, HeroKind::Member, manage);
    for (Id h : p.invited) heroRow(a, w, p, h, HeroKind::Invited, manage);
    if (manage) addHeroCombo(a, w, p);
  };
  if (ui::avail().w >= 420) {
    ui::Row r({ui::px(96), ui::fr(1)}, ui::kAuto, 12);
    leaderSlot(a, w, p, manage);
    ui::Group g(0, 4);
    rows();
  } else {
    {
      ui::Row r({ui::fr(1), ui::px(96), ui::fr(1)}, ui::kAuto, 0);
      ui::next(0, 0);
      leaderSlot(a, w, p, manage);
      ui::next(0, 0);
    }
    rows();
  }
}

void threatsSection(App& a, const World& w, const Plot& p, bool manage) {
  if (p.threats.empty()) return;
  ui::Section s("Угрозы заговору", "warning", {.badge = std::to_string(p.threats.size())});
  a.markUi("plot.threats");
  if (!s) return;
  for (Id h : p.threats) heroRow(a, w, p, h, HeroKind::Threat, manage);
}

// ---------------------------------------------------------------- осуществление
void executeBar(App& a, const World& w, const Plot& p, bool manage) {
  const std::string why = rules::executeProblem(w, p.id);
  i64 rebels = 0, machines = 0;
  for (const PlotProvince& pp : p.provinces) {
    const i64 r = rules::plotRebels(w, pp);
    for (const rules::PlotSquad& s : rules::plotSquads(w, pp, r))
      if (s.race == schema::kRaceMechanical) machines += s.count;
      else rebels += s.count;
  }
  {
    ui::Card c({.pad = 10});
    const float bw = std::min(220.f, std::round(ui::avail().w * 0.55f));
    ui::Row r({ui::fr(1, 80), ui::px(bw)}, 30, 8);
    {
      ui::HStack hs(24, ui::Align::Left, 6);
      tagTip(fmtInt(rebels), rebels > 0 ? ui::Tone::Warning : ui::Tone::Neutral, "rebellion", "Восстанут в провинциях заговора");
      if (machines > 0) tagTip(fmtInt(machines), ui::Tone::Neutral, "u-machines", "Баллисты и големы восстания");
      tagTip(std::to_string(p.heroes.size()), ui::Tone::Neutral, "hero", "Героев уйдёт к заговору");
    }
    if (ui::button("Осуществить заговор", {.variant = ui::Variant::Primary, .icon = "flame", .fill = true, .disabled = !manage || !why.empty(),
                                           .tooltip = why}))
      openExecute(a, p.id);
    a.markUi("plot.execute");
  }
}

void plotCard(App& a, const World& w, const Plot& p) {
  ui::IdScope sc{i64(p.id)};
  const bool manage = canManage(a, p);
  header(a, w, p, manage);
  provincesSection(a, w, p, manage);
  heroesSection(a, w, p, manage);
  threatsSection(a, w, p, manage);
  executeBar(a, w, p, manage);
}

// ---------------------------------------------------------------- вкладка
void drawPlots(App& a, Id fid) {
  const World& w = fac::frameWorld(a);
  const Faction* f = w.faction(fid);
  if (!f || !f->isState()) return;
  ui::IdScope scope("plots");
  const std::vector<Id> list = visiblePlots(a, w, fid);
  Id& sel = selected(fid);
  if (!has(list, sel)) sel = list.empty() ? 0 : list.front();
  const float aw = ui::avail().w;
  const bool wide = aw >= kWide;
  auto card = [&] {
    if (const Plot* p = w.plot(sel)) {
      plotCard(a, w, *p);
    } else if (wide) {
      ui::Disabled d(!a.canPlay(fid));
      if (ui::emptyState("eye-off", "Заговоров нет.", "Создать заговор", "plus")) openCreate(a, fid);
      a.markUi("plots.empty");
    }
  };
  if (wide) {
    const float left = std::clamp(std::round(aw * 0.34f), 320.f, 400.f);
    ui::Row r({ui::px(left), ui::fr(1)}, ui::kAuto, 24);
    {
      ui::Group g(0, 8);
      listSection(a, w, *f, list, sel, true);
      counterIntel(a, w, *f);
    }
    {
      ui::Group g(0, 8);
      card();
    }
  } else {
    listSection(a, w, *f, list, sel, false);
    ui::spacer(4);
    card();
    ui::spacer(4);
    counterIntel(a, w, *f);
  }
}

// Игрок видит вкладку только у своего государства.
bool tabVisible(App& a, Id id) {
  const Faction* f = a.world().faction(id);
  return f && f->isState() && (a.admin() || id == a.ui.playerState);
}

// Точка на значке: заговор частично раскрыт или (администратору) приглашённые ждут решения.
int plotsBadge(App& a, Id id) {
  const World& w = a.world();
  for (Id pid : visiblePlots(a, w, id))
    if (const Plot* p = w.plot(pid); p && (p->alarm || (a.admin() && !p->invited.empty()))) return -1;
  return 0;
}

TabReg tab({kTab, "eye-off", "Заговоры", 47, SelType::Faction, tabVisible, drawPlots, plotsBadge, true});

}  // namespace
}  // namespace rg::app
