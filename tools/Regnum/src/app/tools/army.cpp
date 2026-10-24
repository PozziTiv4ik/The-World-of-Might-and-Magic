// Regnum — инструменты карты «Новое войско» и «Новый флот» (ТЗ 1.c.iii, 1.c.v): щелчок по суше (по морю) ставит
// объект выбранной фракции. Фракция выбирается в параметрах инструмента в верхней строке (по умолчанию — выделенная
// или последняя). Под указателем — фигурка-призрак; недопустимое место — красное кольцо и причина. «Новый флот» доступен
// только при «Свободном редактировании флотов» (ТЗ «Доработки №3», п.3); иначе флот ставится у верфи во вкладке «Флот».
// Игрок (ТЗ «Доработки №4», п.5): инструменты не на ленте и не по клавишам — только кнопкой своего государства
// («Поставить войско»); объект — своего государства, войско — в своей провинции, флот — у её берега.
// Администратор (аудит 2026-10-23, 8.7): в выборе фракции войска есть «Без государства» — войско без государства
// (rules::spawnWildArmy, враждебно всем) с отрядом выбранного типа и численности.
#include "app/panels/military.h"
#include "app/shell/role.h"
#include "app/tools/edit.h"
#include "gfx/figures.h"

namespace rg::app {

namespace mil {

namespace {
Id gForcedFaction = 0;   // фракция, заданная кнопкой «Поставить войско» до выбора инструмента
bool gPanelStart = false;   // инструмент включает кнопка панели (игроку — только так)
// «Без государства» (администратор, только войско): тип и численность отряда нового войска.
bool gWild = false;
UnitType gWildType = UnitType::LightInf;
i64 gWildCount = 100;
constexpr Id kWildPick = 0xFFFFFFF0u;   // пункт «Без государства» в выборе фракции
const Color kWildColor = Color::hex(0x4a4a4f);
}

std::string fleetToolBlocked(App& a) {
  if (a.world().settings->freeFleets) return {};
  return "Свободное редактирование флотов выключено — флот ставится у верфи во вкладке «Флот» государства";
}

Id& lastPlaceFaction() {
  static Id last = 0;
  return last;
}

void startPlacing(App& a, ArmyKind kind, Id faction) {
  ToolId tool = kind == ArmyKind::Fleet ? ToolId::NewFleet : ToolId::NewArmy;
  if (a.readOnly()) {
    a.act("Новый объект", [](Tx&) {});   // сообщение о просмотре прошлого хода
    return;
  }
  if (a.player()) {   // игрок ставит только объекты своего государства
    if (faction && faction != a.ui.playerState) {
      a.toast(detail::role::kAdminOnly, ToastKind::Info, "lock");
      return;
    }
    faction = a.ui.playerState;
  }
  if (a.ui.tool == tool) {
    lastPlaceFaction() = faction;
    return;
  }
  gForcedFaction = faction;
  gPanelStart = true;
  a.setTool(tool);
  gPanelStart = false;
}

}  // namespace mil

namespace {

using namespace mil;

// Игрок: «Новое войско» и «Новый флот» — только кнопкой панели своего государства.
std::string armyToolBlocked(App& a) { return a.player() && !gPanelStart ? std::string(detail::role::kAdminOnly) : std::string(); }
std::string fleetBlocked(App& a) {
  if (std::string why = fleetToolBlocked(a); !why.empty()) return why;
  return armyToolBlocked(a);
}

// Игрок: войско — в своей провинции; флот — в морской провинции у своей (без морских провинций — у своего берега).
std::string playerSpot(App& a, ArmyKind kind, Vec2 p) {
  if (!a.player()) return {};
  const World& w = a.world();
  const Id own = a.ui.playerState;
  const Id pid = rules::provinceAtPos(w, p);
  if (kind == ArmyKind::Army) {
    const Province* pr = w.province(pid);
    return pr && pr->owner == own ? std::string() : std::string("Войско ставится в провинции своего государства");
  }
  bool near = false;
  if (pid) {
    w.provinces.each([&](const Province& q) {
      if (!near && q.owner == own && rules::provincesAdjacent(w, pid, q.id)) near = true;
    });
  } else {
    for (double r : {rules::kLandingReach * 0.5, double(rules::kLandingReach)})
      for (int k = 0; k < 16 && !near; k++) {
        const Province* q = w.province(rules::provinceAtPos(w, p + Vec2{r * std::cos(k * kPi / 8), r * std::sin(k * kPi / 8)}));
        near = q && q->owner == own;
      }
  }
  return near ? std::string() : std::string("Флот ставится у берега своего государства");
}

class PlaceTool final : public MapTool {
 public:
  explicit PlaceTool(ArmyKind k) : kind_(k) {}

  void activate(App& a) override {
    if (a.player() || gForcedFaction) gWild = false;   // кнопка государства и игрок — объект государства
    lastPlaceFaction() = defaultFaction(a);
    if (a.player()) lastPlaceFaction() = a.ui.playerState;   // игрок — только своё государство
  }

  bool pointerDown(App& a, const PointerEvent& e) override {
    if (e.button != 0) return false;
    if (a.readOnly()) {
      a.act("Новый объект", [](Tx&) {});
      return true;
    }
    if (kind_ == ArmyKind::Fleet)
      if (std::string why = fleetToolBlocked(a); !why.empty()) {
        a.toast(why, ToastKind::Info, "tool-fleet");
        return true;
      }
    const bool wild = wildPick(a);
    Id fac = faction(a);
    if (!fac && !wild) {
      a.toast("Сначала выберите фракцию на панели инструмента", ToastKind::Info, "crown");
      return true;
    }
    std::string why;
    if (!rules::validPosition(a.world(), kind_, e.map, 0, &why)) {
      a.toast(why, ToastKind::Warning, "warning");
      return true;
    }
    if (std::string no = playerSpot(a, kind_, e.map); !no.empty()) {
      a.toast(no, ToastKind::Warning, "warning");
      return true;
    }
    Id id = 0;
    bool fleet = kind_ == ArmyKind::Fleet;
    if (wild) {
      // Войско без государства — правка мира (администратор): отряд выбранного типа и численности.
      const rules::WildUnitSpec u{schema::unitType(gWildType).name, gWildType, {}, gWildCount};
      if (!a.act("Новое войско без государства", [&](Tx& tx) { id = rules::spawnWildArmy(tx, e.map, {}, {u}); })) return true;
    } else if (!a.play(fleet ? "Новый флот" : "Новое войско", [&](Tx& tx) { id = rules::createArmy(tx, kind_, fac, e.map); })) {
      return true;
    }
    a.select(SelType::Army, id);
    a.ui.tabOf[SelType::Army] = mil::kTabUnits;
    // Shift — поставить ещё один; иначе — к выбору (новый объект можно сразу перетаскивать).
    if (!(e.mods & platform::ModShift)) detail::later(a, [](App& x) { x.setTool(ToolId::Select); });
    return true;
  }

  bool pointerMove(App& a, const PointerEvent&) override {
    a.requestRedraw();
    return true;
  }

  bool key(App& a, const platform::Event& e) override {
    if (e.key == platform::Key::Escape && e.mods == 0) {
      detail::later(a, [](App& x) { x.setTool(ToolId::Select); });
      return true;
    }
    return false;
  }

  void drawOverlay(App& a, gfx::Canvas& c, const map::View& v) override {
    drawPanel(a);
    drawGhost(a, c, v);
  }

  platform::Cursor cursor(App& a) override {
    if (!a.ui.cursorMap) return platform::Cursor::Crosshair;
    return valid(a, *a.ui.cursorMap, nullptr) ? platform::Cursor::Crosshair : platform::Cursor::NotAllowed;
  }

 private:
  ArmyKind kind_;

  // Выбрано «Без государства» (только администратору и только войско).
  bool wildPick(App& a) const { return gWild && a.admin() && kind_ == ArmyKind::Army; }

  static Id defaultFaction(App& a) {
    const World& w = frameWorld(a);
    auto ok = [&](Id f) { return f != 0 && w.faction(f) != nullptr && !w.faction(f)->isWild(); };
    if (ok(gForcedFaction)) {
      Id f = gForcedFaction;
      gForcedFaction = 0;
      return f;
    }
    gForcedFaction = 0;
    Selection s = a.ui.sel;
    if (s.type == SelType::Faction && ok(s.id)) return s.id;
    if (s.type == SelType::Army)
      if (const Army* ar = w.army(s.id); ar && ok(ar->leader())) return ar->leader();
    if (s.type == SelType::Province)
      if (const Province* p = w.province(s.id); p && ok(p->owner)) return p->owner;
    if (ok(lastPlaceFaction())) return lastPlaceFaction();
    // Первое государство по алфавиту.
    const Faction* best = nullptr;
    w.factions.each([&](const Faction& f) {
      if (!f.isState()) return;
      if (!best || compareRu(f.name, best->name) < 0) best = &f;
    });
    return best ? best->id : 0;
  }

  Id faction(App& a) const {
    if (a.player()) return a.world().faction(a.ui.playerState) ? a.ui.playerState : 0;
    Id f = lastPlaceFaction();
    return f && a.world().faction(f) ? f : 0;
  }

  bool valid(App& a, Vec2 p, std::string* why) const {
    if (kind_ == ArmyKind::Fleet)
      if (std::string no = fleetToolBlocked(a); !no.empty()) {
        if (why) *why = "Свободное редактирование флотов выключено";
        return false;
      }
    if (!faction(a) && !wildPick(a)) {
      if (why) *why = "Выберите фракцию";
      return false;
    }
    if (std::string no = playerSpot(a, kind_, p); !no.empty()) {
      if (why) *why = no;
      return false;
    }
    return rules::validPosition(a.world(), kind_, p, 0, why);
  }

  // Выбор фракции: государства и гильдии; у администратора для войска — ещё «Без государства».
  void factionCombo(App& a, const World& w, Id fid) {
    const bool wildOk = a.admin() && kind_ == ArmyKind::Army;
    if (!wildOk) {
      Id f = fid;
      // Игрок ставит объекты только своего государства: выбор фракции недоступен.
      if (w::factionPicker("faction", f, w::FactionFilter::Any, "", 0, a.player())) {
        lastPlaceFaction() = f;
        gWild = false;
      }
      return;
    }
    std::vector<const Faction*> list;
    w.factions.each([&](const Faction& f) {
      if (!f.isWild()) list.push_back(&f);
    });
    std::sort(list.begin(), list.end(), [](const Faction* x, const Faction* y) {
      if (x->kind != y->kind) return x->kind < y->kind;
      return compareRu(x->name, y->name) < 0;
    });
    w::IdOptions o;
    o.reserve(list.size() + 1);
    for (const Faction* f : list) o.add(f->id, w::orName(f->name), nullptr, f->color, f->isGuild() ? "гильдия" : "");
    o.add(kWildPick, schema::kFactionKinds[int(FactionKind::Wild)].name, "skull", Color(0, 0, 0, 0), "враждебно всем");
    Id pick = wildPick(a) ? kWildPick : fid;
    ui::ComboOpt co;
    co.icon = "crown";
    co.placeholder = "—";
    co.disabled = a.readOnly();
    if (w::idCombo("faction", pick, o, co)) {
      gWild = pick == kWildPick;
      if (!gWild) lastPlaceFaction() = pick;
    }
  }

  // «Без государства»: тип отряда и численность нового войска.
  void wildUnit(App& a) {
    {
      ui::Group g(150);
      int t = int(gWildType);
      std::vector<ui::Option> opts;
      for (int i = 0; i < int(UnitType::Count); i++) opts.push_back(ui::Option{schema::kUnitTypes[i].name, schema::kUnitTypes[i].icon});
      if (ui::combo("wildType", t, opts, {.icon = "army"}) && t >= 0) gWildType = UnitType(t);
      ui::tooltip("Тип отряда войска без государства");
    }
    a.markUi("place.wild.type");
    {
      ui::Group g(110);
      ui::numberField("wildCount", gWildCount, {.min = 1, .max = 1e9, .tooltip = "Численность отряда"});
    }
    a.markUi("place.wild.count");
  }

  // Параметры инструмента в верхней строке (как у остальных инструментов карты): вид объекта, фракция, её резерв.
  void drawPanel(App& a) {
    if (a.hasDialog()) return;
    const World& w = frameWorld(a);
    const bool fleet = kind_ == ArmyKind::Fleet;
    const bool wild = wildPick(a);
    const Id fid = faction(a);
    // Резерв фракции: сколько можно назначить новому объекту (ТЗ 1.c.iii).
    i64 reserve = 0;
    size_t rows = 0;
    if (const Faction* fc = w.faction(fid)) {
      rows = fleet ? fc->fleet.size() : fc->army.size();
      auto c = rules::calc(w);
      if (const rules::FactionCalc* fcc = c->faction(fid))
        for (const rules::RowCalc& r : fleet ? fcc->fleet : fcc->army) reserve += std::max<i64>(0, r.reserve);
    }
    std::string tag = !fid ? std::string("Выберите фракцию") : rows == 0 ? std::string("Таблица пуста") : "Резерв " + fmtCount(reserve);
    const float segW = 2 * 34 + 4, pickW = 220;
    float width = segW + 6 + pickW + 6 + (wild ? 150 + 6 + 110 : tools::textW(tag, ui::Font::Strong) + 34) + 6 + 30;
    tools::OptionsBar bar(a, fleet ? "tool-fleet" : "tool-army", fleet ? "Новый флот" : "Новое войско", width);
    if (!bar) return;
    int k = fleet ? 1 : 0;
    if (ui::segmented("kind", k, {{"army", {}, "Войско — ставится на сушу"}, {"fleet", {}, "Флот — ставится на море"}},
                      {.size = ui::Size::Small, .fill = false})) {
      ArmyKind kk = k == 1 ? ArmyKind::Fleet : ArmyKind::Army;
      detail::later(a, [kk, fid](App& x) { startPlacing(x, kk, fid); });   // фракция сохраняется при смене вида
    }
    a.markUi("place.kind");
    {
      ui::Group g(pickW);
      factionCombo(a, w, fid);
      ui::tooltip(fleet ? "Чей флот" : "Чьё войско");
    }
    a.markUi("place.faction");
    if (wild) {
      wildUnit(a);
    } else if (!fid) {
      ui::tag(tag, ui::Tone::Warning, "warning");
    } else if (rows == 0) {
      ui::tag(tag, ui::Tone::Warning, "warning");
      ui::tooltip(std::string("В таблице ") + (fleet ? "флота" : "войск") + " фракции нет строк — добавьте их во вкладке «" +
                  (fleet ? "Флот" : "Войска") + "»; объект можно поставить и пустым");
    } else {
      ui::tag(tag, reserve > 0 ? ui::Tone::Success : ui::Tone::Neutral, fleet ? "anchor" : "shield");
      ui::tooltip(fleet ? "Свободные корабли фракции — назначаются в инспекторе нового флота"
                        : "Свободные отряды фракции — назначаются в инспекторе нового войска");
    }
    a.markUi("place.reserve");
    ui::flex();
    if (ui::iconButton("close", "Закрыть инструмент")) detail::later(a, [](App& x) { x.setTool(ToolId::Select); });
    ui::tooltip("Закрыть инструмент", {platform::Key::Escape, 0});
    a.markUi("place.close");
  }

  void drawGhost(App& a, gfx::Canvas& c, const map::View& v) {
    if (!a.ui.cursorMap || a.hasDialog() || a.readOnly()) return;
    Vec2 p = *a.ui.cursorMap;
    std::string why;
    bool ok = valid(a, p, &why);
    const World& w = frameWorld(a);
    Color col = wildPick(a) ? kWildColor : w::factionColor(w, faction(a));
    float size = a.map().figureSize();
    gfx::Pt sp = v.toScreen(p);
    const ui::Theme& th = ui::theme();
    if (!ok) {
      c.fillCircle(sp.x, sp.y, size * 0.62f, th.danger.alpha(0.18f));
      c.strokeCircle(sp.x, sp.y, size * 0.62f, 2, th.danger);
    } else {
      c.fillCircle(sp.x, sp.y, size * 0.62f, tools::palette().light.alpha(0.18f));
    }
    c.save();
    c.setOpacity(ok ? 0.88f : 0.5f);
    if (kind_ == ArmyKind::Fleet) gfx::drawFleetFigure(c, sp, size, col, ok);
    else gfx::drawArmyFigure(c, sp, size, col, ok);
    c.restore();
    if (!ok && !why.empty()) {
      // Причина — таблетка под фигуркой (точки интерфейса).
      float s = ui::uiScale();
      float tw = std::ceil(ui::measure(why, ui::Font::Small)) + 2;
      RectF pr{std::round(sp.x / s - (tw + 36) * 0.5f), std::round((sp.y + size * 0.72f) / s + 4), tw + 36, 24};
      ui::draw::shadow(pr, 12, 10, th.shadow, 2);
      ui::draw::rect(pr, th.surface1, 12);
      ui::draw::rectStroke(pr, th.danger.alpha(0.7f), 12, 1);
      ui::draw::icon("warning", RectF{pr.x + 8, pr.cy() - 7, 14, 14}, th.danger);
      ui::draw::text(why, RectF{pr.x + 26, pr.y, tw, pr.h}, ui::Font::Small, th.text);
    }
  }
};

BuiltinToolReg armyReg({ToolId::NewArmy, "tool-army", "Новое войско", "A", false, [] { return std::make_unique<PlaceTool>(ArmyKind::Army); }, 50, false, false,
                        armyToolBlocked});
BuiltinToolReg fleetReg({ToolId::NewFleet, "tool-fleet", "Новый флот", "Shift+A", false, [] { return std::make_unique<PlaceTool>(ArmyKind::Fleet); }, 51, false, false,
                         fleetBlocked});

}  // namespace

std::unique_ptr<MapTool> mil::makePlaceTool(ArmyKind kind) { return std::make_unique<PlaceTool>(kind); }

}  // namespace rg::app
