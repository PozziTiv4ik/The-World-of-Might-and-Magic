// Regnum — захват провинции после победы над гарнизоном или без гарнизона (ТЗ «Механика войн», п.5): захватить
// (оккупация); захватить и разграбить (золото ТекущаяЦенность × Население / 10 000, «Разграбленная провинция»,
// отношения −5); разорить (вдвое больше золота, «Разоренная провинция», войско отступает, отношения −10); опустошить
// (0–75 % населения — в рабы, остальные гибнут, «Опустошенная провинция», войску — «Неправедное деяние» и «Мучения
// совести», отношения −20). Доступность и причины — rules::captureOptions, решение — rules::capture.
#include "app/app_internal.h"
#include "app/flows.h"
#include "app/panels/military.h"

namespace rg::app {

namespace {

using namespace mil;
using rules::Capture;

struct OptionInfo {
  const char* icon;
  const char* title;
  const char* button;
};
constexpr OptionInfo kOptions[int(Capture::Count)] = {
    {"occupied", "Захватить", "Захватить"},
    {"coins", "Захватить и разграбить", "Разграбить"},
    {"flame", "Разорить", "Разорить"},
    {"skull", "Опустошить", "Опустошить"},
};

std::string modName(const World& w, const char* key) {
  const Modifier* m = rules::builtinMod(w, key);
  return "«" + (m ? m->name : std::string(key)) + "»";
}

struct CaptureDialog final : Dialog {
  Id army = 0, province = 0;
  std::function<void(App&)> done;
  int choice = -1;
  int slavesPct = 50;
  bool decided = false;

  const char* id() const override { return "capture"; }
  Style style(App& a) override { return {"Захват · " + frameWorld(a).provinceName(province), "occupied", ui::Tone::Danger, 620}; }

  void close(App& a) {
    if (decided) return;
    decided = true;
    if (done) {
      auto fn = done;
      detail::later(a, [fn](App& x) { fn(x); });
    }
  }
  void dismissed(App& a) override { close(a); }

  // Войско без «Армия нежити», «Армия демонов», «Безжалостная армия» мучается совестью.
  bool guilty(const World& w) const {
    return !rules::armyHas(w, army, schema::mod::UndeadArmy) && !rules::armyHas(w, army, schema::mod::DemonArmy) &&
           !rules::armyHas(w, army, schema::mod::Ruthless);
  }

  void apply(App& a, Capture how) {
    const World& w = frameWorld(a);
    const std::string pn = w.provinceName(province);
    const rules::CaptureOptions o = rules::captureOptions(w, army, province);
    const Id ar = army, pid = province;
    const int pct = slavesPct;
    if (!a.act(std::string(kOptions[int(how)].button) + ": " + pn, [&](Tx& tx) { rules::capture(tx, ar, pid, how, pct); })) return;
    switch (how) {
      case Capture::Occupy: a.toast("Провинция оккупирована: " + pn, ToastKind::Success, "occupied"); break;
      case Capture::Plunder: a.toast("Разграблено: +" + fmtGold(o.plunderGold) + " золота", ToastKind::Success, "coins"); break;
      case Capture::Raze: a.toast("Разорено: +" + fmtGold(o.razeGold) + " золота", ToastKind::Success, "flame"); break;
      default: {
        const i64 slaves = i64(std::floor(double(o.population) * pct / 100.0));
        a.toast("Опустошено: в рабство " + fmtCount(slaves) + ", погибли " + fmtCount(o.population - slaves), ToastKind::Warning, "skull");
        break;
      }
    }
    if (a.world().army(army)) a.select(SelType::Army, army);
    close(a);
  }

  bool draw(App& a) override {
    const World& w = frameWorld(a);
    const Army* A = w.army(army);
    const Province* P = w.province(province);
    if (!A || !P) {
      close(a);
      return false;
    }
    const rules::CaptureOptions o = rules::captureOptions(w, army, province);
    if (choice < 0 || choice >= int(Capture::Count) || !o.can[choice]) {
      choice = -1;
      for (int i = 0; i < int(Capture::Count) && choice < 0; i++)
        if (o.can[i]) choice = i;
    }
    auto c = rules::calc(w);
    const rules::ProvinceCalc* pc = c->province(province);
    {
      ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
      ui::stat(fmtCount(o.population), "Население", {.icon = "population", .tone = ui::Tone::Info});
      ui::stat(fmtMoney(pc ? pc->tradeValue : 0), "Текущая ценность", {.icon = "trade-value", .tone = ui::Tone::Accent});
    }
    {
      ui::HStack hs(24, ui::Align::Left, 6);
      ui::icon(objectIcon(*A), ui::Ink::Dim, 16, "Войско");
      ui::label(objectName(*A), {.font = ui::Font::Small, .ink = ui::Ink::Dim});
      ui::flex();
      if (P->owner) {
        ui::icon("crown", ui::Ink::Dim, 16, "Владелец провинции");
        w::factionChip(P->owner);
      }
    }
    const bool guilt = guilty(w);
    for (int i = 0; i < int(Capture::Count); i++) {
      ui::IdScope s(i);
      const Capture how = Capture(i);
      ui::Card card({.pad = 10, .tone = choice == i ? ui::Tone::Accent : ui::Tone::Neutral});
      {
        ui::Row r({ui::fr(1), ui::px(150)}, 26, 8);
        {
          ui::HStack hs(26, ui::Align::Left, 6);
          ui::radio(kOptions[i].title, choice, i, !o.can[i]);
          a.markUi("capture.option." + std::to_string(i));
          ui::icon(kOptions[i].icon, o.can[i] ? ui::Ink::Dim : ui::Ink::Muted, 16);
        }
        const double gold = how == Capture::Plunder ? o.plunderGold : how == Capture::Raze ? o.razeGold : 0;
        if (gold > 0) ui::label("+" + fmtGold(gold), {.font = ui::Font::Strong, .ink = o.can[i] ? ui::Ink::Success : ui::Ink::Muted, .align = ui::Align::Right,
                                                       .icon = "coins", .tooltip = "Золото в казну"});
        else ui::next(0, 0);
      }
      if (!o.can[i]) {
        ui::label(o.why[i], {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
        continue;
      }
      {
        ui::HStack hs(0, ui::Align::Left, 6);
        const std::string turns = " · " + nTurns(schema::kCaptureModTurns);
        switch (how) {
          case Capture::Occupy: ui::tag("Оккупация", ui::Tone::Info, "occupied"); break;
          case Capture::Plunder:
            ui::tag("Оккупация", ui::Tone::Info, "occupied");
            ui::tag(modName(w, schema::mod::Plundered) + turns, ui::Tone::Warning, "sparkles");
            ui::tag("Отношения " + fmtSigned(schema::kPlunderRelation), ui::Tone::Danger, "diplomacy");
            break;
          case Capture::Raze:
            ui::tag(modName(w, schema::mod::Ravaged) + turns, ui::Tone::Warning, "sparkles");
            ui::tag("Отношения " + fmtSigned(schema::kRazeRelation), ui::Tone::Danger, "diplomacy");
            ui::tag("Отступление", ui::Tone::Neutral, "retreat");
            break;
          default:
            ui::tag(modName(w, schema::mod::Devastated) + turns, ui::Tone::Danger, "sparkles");
            ui::tag("Отношения " + fmtSigned(schema::kDevastateRelation), ui::Tone::Danger, "diplomacy");
            break;
        }
      }
      if (how == Capture::Devastate && choice == i) {
        // Доля населения в рабы (0–75 %), остальные гибнут; войску — неправедное деяние и мучения совести.
        ui::Row r({ui::px(70), ui::fr(1)}, 30, 10);
        ui::label("В рабы", {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "shackles"});
        ui::slider("slaves", slavesPct, 0, schema::kDevastateMaxSlavesPct, {.step = 1, .unit = "%"});
        a.markUi("capture.slaves");
        const i64 slaves = i64(std::floor(double(o.population) * slavesPct / 100.0));
        ui::HStack hs(0, ui::Align::Left, 6);
        ui::tag("Рабов " + fmtCount(slaves), ui::Tone::Neutral, "shackles");
        ui::tag("Погибнет " + fmtCount(o.population - slaves), ui::Tone::Danger, "skull");
        if (guilt) {
          ui::tag(modName(w, schema::mod::Unrighteous), ui::Tone::Danger, "army");
          if (slavesPct < schema::kConscienceBelowPct) ui::tag(modName(w, schema::mod::Conscience), ui::Tone::Danger, "army");
        }
        a.markUi("capture.devastate");
      }
    }
    ui::ModalFooter f;
    if (ui::button("Не захватывать", {.variant = ui::Variant::Ghost})) {
      close(a);
      return false;
    }
    a.markUi("capture.cancel");
    {
      ui::Disabled dis(a.readOnly());
      const bool can = choice >= 0 && o.can[choice];
      const Capture how = can ? Capture(choice) : Capture::Occupy;
      if (ui::button(can ? kOptions[choice].button : "Захватить",
                     {.variant = how == Capture::Devastate ? ui::Variant::Danger : ui::Variant::Primary, .icon = can ? kOptions[choice].icon : "occupied",
                      .disabled = !can, .isDefault = true})) {
        if (how == Capture::Devastate) {
          // Опасное действие — с подтверждением.
          const i64 slaves = i64(std::floor(double(o.population) * slavesPct / 100.0));
          auto self = this;
          const Id ar = army, pid = province;
          a.confirm("Опустошить провинцию?",
                    "Погибнет " + fmtCount(o.population - slaves) + ", в рабство — " + fmtCount(slaves) +
                        ". Постройки будут уничтожены, провинция останется без владельца.",
                    "Опустошить", true, [self, ar, pid](App& x) {
                      // Окно захвата ещё открыто (подтверждение — поверх него).
                      for (auto& d : x.dialogStack())
                        if (d.get() == self && self->army == ar && self->province == pid && !self->decided) {
                          self->apply(x, Capture::Devastate);
                          break;
                        }
                    });
        } else {
          apply(a, how);
          if (decided) return false;
        }
      }
      a.markUi("capture.ok");
    }
    return !decided;
  }
};

}  // namespace

void flow::openCapture(App& a, Id army, Id province, std::function<void(App&)> done) {
  if (!a.world().army(army) || !a.world().province(province)) {
    if (done) detail::later(a, [done](App& x) { done(x); });
    return;
  }
  auto d = std::make_unique<CaptureDialog>();
  d->army = army;
  d->province = province;
  d->done = std::move(done);
  a.openDialog(std::move(d));
}

}  // namespace rg::app
