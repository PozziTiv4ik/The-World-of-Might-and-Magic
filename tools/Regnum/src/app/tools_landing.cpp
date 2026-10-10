// Regnum — инструмент «Высадка войска» (ТЗ «Доработки №3», п.6): кнопка панели флота с войском на борту включает его;
// за указателем — фигурка войска, щелчок по суше высаживает его туда (rules::land): в провинцию, соседнюю с морской
// провинцией флота (они подсвечены), а если флот вне морской провинции — на сушу недалеко от флота (круг). Неверное
// место — красное кольцо и причина, курсор «нельзя». Esc или «Закрыть» — отмена.
#include "app/panels/military.h"
#include "app/tools_edit.h"
#include "gfx/figures.h"

namespace rg::app {

namespace {

Id gLandingFleet = 0;   // флот, с которого высаживают (задаёт startLanding до выбора инструмента)

class LandingTool final : public MapTool {
 public:
  void activate(App& a) override {
    fleet_ = gLandingFleet;
    gLandingFleet = 0;
    if (!rules::cargoOf(a.world(), fleet_)) close(a);
  }

  bool pointerDown(App& a, const PointerEvent& e) override {
    if (e.button != 0) return false;
    if (a.readOnly()) {
      a.act("Высадка войска", [](Tx&) {});   // сообщение о просмотре прошлого хода
      return true;
    }
    std::string why;
    if (!valid(a, e.map, &why)) {
      a.toast(why, ToastKind::Warning, "warning");
      return true;
    }
    const Id fleet = fleet_;
    Id army = 0;
    if (!a.act("Высадка войска", [&](Tx& tx) { army = rules::land(tx, fleet, e.map); })) return true;
    a.select(SelType::Army, army);
    a.ui.tabOf[SelType::Army] = "army.units";
    close(a);
    return true;
  }

  bool pointerMove(App& a, const PointerEvent&) override {
    a.requestRedraw();
    return true;
  }

  bool key(App& a, const platform::Event& e) override {
    if (e.key == platform::Key::Escape && e.mods == 0) {
      close(a);
      return true;
    }
    return false;
  }

  void drawOverlay(App& a, gfx::Canvas& c, const map::View& v) override {
    const World& w = mil::frameWorld(a);
    const Army* fl = w.army(fleet_);
    const Army* cargo = w.army(rules::cargoOf(w, fleet_));
    if (!fl || !cargo) {
      close(a);
      return;
    }
    options(a, w, *cargo);
    const tools::Palette P = tools::palette();
    const float size = a.map().figureSize();
    // Куда можно высадить: провинции у морской провинции флота или круг недалеко от флота.
    const std::vector<Id> provs = rules::landingProvinces(w, fleet_);
    gfx::Stroke s;
    s.join = gfx::Join::Round;
    for (Id pid : provs) {
      const gfx::Path p = tools::provincePath(w, v, pid);
      c.fillPath(p, P.success.alpha(0.14f), gfx::FillRule::EvenOdd);
      s.width = 1.6f;
      s.dash = {7, 5};
      c.strokePath(p, s, P.success.alpha(0.85f));
    }
    const gfx::Pt fp = v.toScreen(fl->pos);
    if (provs.empty()) {
      const float r = float(rules::kLandingReach * v.zoom);
      c.fillCircle(fp.x, fp.y, r, P.success.alpha(0.10f));
      gfx::Stroke cs;
      cs.width = 1.6f;
      cs.dash = {7, 5};
      gfx::Path circle;
      const int n = 72;
      for (int i = 0; i <= n; i++) {
        const float ang = float(2 * kPi) * float(i) / float(n);
        const float x = fp.x + r * std::cos(ang), y = fp.y + r * std::sin(ang);
        if (i == 0) circle.moveTo(x, y);
        else circle.lineTo(x, y);
      }
      c.strokePath(circle, cs, P.success.alpha(0.85f));
    }
    c.strokeCircle(fp.x, fp.y, size * 0.66f, 2, P.accent);   // флот, с которого высаживают
    drawGhost(a, c, v, w, *cargo);
  }

  platform::Cursor cursor(App& a) override {
    if (!a.ui.cursorMap) return platform::Cursor::Crosshair;
    return valid(a, *a.ui.cursorMap, nullptr) ? platform::Cursor::Crosshair : platform::Cursor::NotAllowed;
  }

 private:
  Id fleet_ = 0;

  static void close(App& a) { detail::later(a, [](App& x) { x.setTool(ToolId::Select); }); }

  bool valid(App& a, Vec2 p, std::string* why) const { return rules::canLand(a.world(), fleet_, p, why); }

  // Параметры сверху: войско на борту (значок, название, численность) и «Закрыть».
  void options(App& a, const World& w, const Army& cargo) {
    if (a.hasDialog()) return;
    const std::string name = mil::objectName(cargo);
    const std::string count = mil::fmtCount(mil::unitCount(cargo));
    const float width = 24 + 6 + std::min(tools::textW(name, ui::Font::Strong), 220.f) + 8 + tools::textW(count, ui::Font::Small) + 12 + 30;
    tools::OptionsBar bar(a, "disembark", "Высадка войска", width);
    if (!bar) return;
    mil::figure(w, cargo, 24);
    ui::label(name, {.font = ui::Font::Strong, .tooltip = tools::textW(name, ui::Font::Strong) > 220 ? std::string_view(name) : std::string_view()});
    ui::label(count, {.font = ui::Font::Small, .ink = ui::Ink::Muted, .tooltip = "Численность войска"});
    a.markUi("landing.army");
    ui::flex();
    if (ui::iconButton("close", "Отменить высадку")) close(a);
    ui::tooltip("Отменить высадку", {platform::Key::Escape, 0});
    a.markUi("landing.close");
  }

  // Фигурка войска под указателем; неверное место — красное кольцо и причина под фигуркой.
  void drawGhost(App& a, gfx::Canvas& c, const map::View& v, const World& w, const Army& cargo) {
    if (!a.ui.cursorMap || a.hasDialog() || a.readOnly()) return;
    const Vec2 p = *a.ui.cursorMap;
    std::string why;
    const bool ok = valid(a, p, &why);
    const float size = a.map().figureSize();
    const gfx::Pt sp = v.toScreen(p);
    const ui::Theme& th = ui::theme();
    const gfx::Pt fp = v.toScreen(w.army(fleet_)->pos);
    gfx::Stroke dash;
    dash.width = 1.6f;
    dash.cap = gfx::Cap::Round;
    dash.dash = {6, 5};
    const gfx::Pt line[2] = {fp, sp};
    c.polyline(line, 2, false, dash, tools::palette().light.alpha(0.67f));
    if (!ok) {
      c.fillCircle(sp.x, sp.y, size * 0.62f, th.danger.alpha(0.18f));
      c.strokeCircle(sp.x, sp.y, size * 0.62f, 2, th.danger);
    } else {
      c.fillCircle(sp.x, sp.y, size * 0.62f, tools::palette().light.alpha(0.18f));
    }
    c.save();
    c.setOpacity(ok ? 0.9f : 0.5f);
    gfx::drawArmyFigure(c, sp, size, mil::leaderColor(w, cargo), ok, cargo.allied(), mil::allyColor(w, cargo));
    c.restore();
    if (!ok && !why.empty()) {
      const float s = ui::uiScale();
      const float tw = std::ceil(ui::measure(why, ui::Font::Small)) + 2;
      const RectF pr{std::round(sp.x / s - (tw + 36) * 0.5f), std::round((sp.y + size * 0.72f) / s + 4), tw + 36, 24};
      ui::draw::shadow(pr, 12, 10, th.shadow, 2);
      ui::draw::rect(pr, th.surface1, 12);
      ui::draw::rectStroke(pr, th.danger.alpha(0.7f), 12, 1);
      ui::draw::icon("warning", RectF{pr.x + 8, pr.cy() - 7, 14, 14}, th.danger);
      ui::draw::text(why, RectF{pr.x + 26, pr.y, tw, pr.h}, ui::Font::Small, th.text);
    }
  }
};

ToolReg landingReg({ToolId::Landing, "disembark", "Высадка войска", nullptr, false, [] { return std::make_unique<LandingTool>(); }, 90, false, true});

}  // namespace

void mil::startLanding(App& a, Id fleet) {
  if (a.readOnly()) {
    a.act("Высадка войска", [](Tx&) {});   // сообщение о просмотре прошлого хода
    return;
  }
  if (!rules::cargoOf(a.world(), fleet)) {
    a.toast("На борту нет войска", ToastKind::Info, "disembark");
    return;
  }
  if (a.ui.tool == ToolId::Landing) a.setTool(ToolId::Select);   // другой флот — инструмент заново
  gLandingFleet = fleet;
  a.setTool(ToolId::Landing);
}

}  // namespace rg::app
