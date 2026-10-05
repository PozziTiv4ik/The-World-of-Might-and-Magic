// Regnum — встроенные инструменты карты: выбор (ТЗ 1.a.iv) с перетаскиванием войск и флотов (ТЗ 1.c.iv) и панорама.
//
// Перетаскивание: нажатие на фигурку и сдвиг — за указателем идёт фигурка-призрак (настоящая скрыта), исходная
// позиция отмечена пунктиром, подпись у призрака сообщает, что произойдёт. Отпускание — rules::encounter:
// свободное место — перемещение; своя фракция — предложение объединить; союзник — союзное войско; статус-кво
// или незнакомы — объявить войну, затем битва; война — панель битвы; иначе (море, суша, флот, занято) —
// возврат с причиной. Войско на знаке «замок» или «башня» провинции государства, с которым его фракция в войне, —
// штурм (ТЗ «Механика войн», п.4): войско встаёт у стен, окно штурма гарнизона или захвата. Отказ в диалоге плавно
// возвращает объект на исходную позицию.
#include "app/tools_edit.h"
#include "app/flows.h"
#include "app/panels/military.h"
#include "gfx/figures.h"
#include "map/art_scene.h"

namespace rg::app {

namespace {

constexpr float kDragThreshold = 4;     // точек экрана до начала перетаскивания
constexpr double kReturnSec = 0.22;     // возврат призрака на исходную позицию

// Перетаскивание объекта. Общее с колбэками диалогов встречи и битвы (они переживают инструмент).
struct DragState {
  enum Phase : u8 { Idle, Pressed, Dragging, Pending, Returning };
  Phase phase = Idle;
  Id army = 0;
  Vec2 origin;               // исходная позиция (карта)
  Vec2 grab;                 // центр фигурки относительно указателя (карта)
  Vec2 pos;                  // призрак (карта)
  float sx0 = 0, sy0 = 0;    // точка нажатия (экран)
  Vec2 returnFrom;
  double returnStart = 0;
  rules::Encounter preview;  // что произойдёт при отпускании
  Id siegeProvince = 0;      // штурм: провинция замка или башни под призраком (0 — нет)
  Id siegeSymbol = 0;
  std::optional<map::ArmyMark> stack;   // нажата стопка: щелчок без перетаскивания приближает её
  bool active() const { return phase == Dragging || phase == Pending || phase == Returning; }
};

// Штурм (ТЗ «Механика войн», п.4): призрак войска над знаком «замок» или «башня» провинции государства, с которым
// фракция войска в войне (rules::canSiege). Встреча с другим объектом важнее штурма.
void siegeTarget(App& a, DragState& d) {
  d.siegeProvince = d.siegeSymbol = 0;
  if (d.preview.target) return;
  const World& w = mil::frameWorld(a);
  const Army* ar = w.army(d.army);
  if (!ar || ar->isFleet()) return;
  auto sc = a.map().artScene();
  if (!sc) return;
  // Замок или башня под призраком — даже если поверх нарисована гора: ближайший по середине значка.
  const double tol = a.map().view().toMapLen(6), reach = tol + 160;   // знаки — до ×8 обычного размера
  Id sym = 0;
  const MapSymbol* s = nullptr;
  double best = 1e300;
  for (Id id : sc->symbolsIn(Box2(d.pos.x - reach, d.pos.y - reach, d.pos.x + reach, d.pos.y + reach))) {
    const MapSymbol* x = detail::mapSymbol(w, id);
    if (!x || (x->kind != SymbolKind::Castle && x->kind != SymbolKind::Tower)) continue;
    const Box2 b = map::art::symbolBox(*x);
    if (!b.inflated(tol).contains(d.pos) || dist2(b.center(), d.pos) >= best) continue;
    best = dist2(b.center(), d.pos);
    sym = id;
    s = x;
  }
  if (!s) return;
  Id pid = mil::provinceUnder(w, s->p);
  if (!pid) pid = mil::provinceUnder(w, d.pos);
  if (!pid || !rules::canSiege(w, d.army, pid)) return;
  d.siegeProvince = pid;
  d.siegeSymbol = sym;
}

// Приблизить карту к стопке наложившихся объектов так, чтобы они разошлись на отдельные фигурки.
void expandStack(App& a, const map::ArmyMark& mk) {
  const double z = a.map().separateZoom(mk);
  if (z <= 0) return;
  Box2 b;
  for (Id id : mk.members)
    if (const Army* ar = a.world().army(id)) b.add(ar->pos);
  a.focusMap(b.empty() ? mk.pos : b.center(), z);
}

void startReturn(App& a, DragState& d) {
  d.returnFrom = d.pos;
  d.returnStart = a.time();
  d.phase = DragState::Returning;
  a.requestRedraw();
}

// Фигурка под точкой экрана, кроме exclude (порядок как у карты: верхняя — нарисованная последней).
Id armyAtScreen(App& a, float sx, float sy, Id exclude) {
  const map::View& v = a.map().view();
  const float r = a.map().figureSize() * 0.5f;
  std::vector<const Army*> list = a.world().armies.all();
  std::stable_sort(list.begin(), list.end(), [](const Army* x, const Army* y) { return x->pos.y < y->pos.y; });
  Id best = 0;
  for (const Army* ar : list) {
    if (ar->id == exclude) continue;
    gfx::Pt s = v.toScreen(ar->pos);
    float dx = sx - s.x, dy = sy - (s.y - r * 0.08f);
    if (dx * dx + dy * dy <= r * r * 1.1f) best = ar->id;
  }
  return best;
}

// Встреча в точке призрака: фигурка, на которую его «положили», — её позиция; иначе — сама точка.
rules::Encounter encounterAt(App& a, const DragState& d) {
  const World& w = mil::frameWorld(a);
  gfx::Pt s = a.map().view().toScreen(d.pos);
  if (Id t = armyAtScreen(a, s.x, s.y, d.army))
    if (const Army* ta = w.army(t)) return rules::encounter(w, d.army, ta->pos);
  return rules::encounter(w, d.army, d.pos);
}

struct Status {
  const char* icon;
  std::string text;
  ui::Tone tone;
};

Status statusOf(App& a, const DragState& d) {
  const World& w = mil::frameWorld(a);
  const rules::Encounter& e = d.preview;
  const Army* t = w.army(e.target);
  std::string tn = t ? "«" + mil::objectName(*t) + "»" : std::string();
  if (d.siegeProvince) {
    const MapSymbol* s = detail::mapSymbol(w, d.siegeSymbol);
    return {s && s->kind == SymbolKind::Tower ? "tower" : "castle", "Штурм: " + w.provinceName(d.siegeProvince), ui::Tone::Danger};
  }
  switch (e.type) {
    case rules::EncounterType::None: {
      Id pid = mil::provinceUnder(w, d.pos);
      return {"map-pin", pid ? w.provinceName(pid) : std::string("Переместить"), ui::Tone::Neutral};
    }
    case rules::EncounterType::Merge: return {"merge", "Объединить с " + tn, ui::Tone::Accent};
    case rules::EncounterType::Alliance: return {"alliance", "Союз с " + tn, ui::Tone::Success};
    case rules::EncounterType::DeclareWar: return {"war", "Объявить войну: " + w.factionName(e.them), ui::Tone::Danger};
    case rules::EncounterType::Battle: return {"battle", "Битва: " + tn + " · " + w.factionName(t ? t->leader() : 0), ui::Tone::Danger};
    default: return {"warning", e.reason.empty() ? std::string("Сюда нельзя") : e.reason, ui::Tone::Danger};
  }
}

class SelectTool final : public MapTool {
 public:
  void deactivate(App& a) override {
    if (d_->phase != DragState::Pending) *d_ = DragState{};
    a.requestRedraw();
  }

  bool pointerDown(App& a, const PointerEvent& e) override {
    if (e.button != 0) return false;
    if (d_->phase == DragState::Pending) return true;   // решение ещё в диалоге
    if (d_->phase == DragState::Returning) d_->phase = DragState::Idle;
    map::MapView& m = a.map();
    // Стопка наложившихся объектов (обзорный масштаб): щелчок приближает карту, чтобы они разошлись;
    // перетаскивание переносит верхний объект.
    std::optional<map::ArmyMark> mk = m.markAt(e.sx, e.sy);
    if (mk && (!mk->cluster() || m.separateZoom(*mk) <= 0)) mk.reset();
    Id army = m.armyAt(e.sx, e.sy);
    Id prov = army ? 0 : m.provinceAt(e.sx, e.sy);
    if (army && !a.readOnly()) {
      if (const Army* ar = a.world().army(army)) {
        DragState& d = *d_;
        d = DragState{};
        d.phase = DragState::Pressed;
        d.army = army;
        d.origin = ar->pos;
        d.grab = ar->pos - e.map;
        d.pos = ar->pos;
        d.sx0 = e.sx;
        d.sy0 = e.sy;
        d.stack = mk;
      }
    }
    if (mk) {
      if (a.readOnly()) expandStack(a, *mk);   // без перетаскивания — сразу
      return true;
    }
    if (a.ui.editBorders && !a.readOnly()) {
      // Правка включена: щелчок по провинции открывает её границы для правки.
      if (prov) {
        a.select(SelType::Province, prov);
        if (findTool(ToolId::EditBorders)) a.setTool(ToolId::EditBorders);
      } else if (army) {
        a.select(SelType::Army, army);
      } else {
        a.clearSelection();
      }
      return true;
    }
    // Правка выключена: щелчок — сведения (инспектор), двойной щелчок — ещё и показать.
    bool focus = e.clicks >= 2;
    // Линия маршрута лежит поверх провинции: когда маршруты на карте видны (режимы гильдий и торговли или этот
    // маршрут выбран), щелчок точно по линии выбирает маршрут, а не провинцию под ним.
    Id routeHit = army ? 0 : m.routeAt(e.sx, e.sy);
    bool routesShown = a.ui.mapMode == schema::MapMode::Guilds || a.ui.mapMode == schema::MapMode::Trade;
    bool routeVisible = routeHit && (routesShown || a.ui.sel == Selection{SelType::Route, routeHit});
    if (army) a.select(SelType::Army, army, focus);
    else if (routeVisible) a.select(SelType::Route, routeHit, focus);
    else if (prov) a.select(SelType::Province, prov, focus);
    else if (Id route = m.routeAt(e.sx, e.sy)) a.select(SelType::Route, route, focus);
    else a.clearSelection();
    return true;
  }

  bool pointerMove(App& a, const PointerEvent& e) override {
    DragState& d = *d_;
    if (d.phase == DragState::Pressed) {
      if (std::hypot(e.sx - d.sx0, e.sy - d.sy0) < kDragThreshold) return true;
      if (!a.world().army(d.army)) {
        d = DragState{};
        return true;
      }
      d.phase = DragState::Dragging;
    }
    if (d.phase != DragState::Dragging) return false;
    if (!a.world().army(d.army)) {
      d = DragState{};
      return true;
    }
    d.pos = e.map + d.grab;
    d.preview = encounterAt(a, d);
    siegeTarget(a, d);
    a.requestRedraw();
    return true;
  }

  bool pointerUp(App& a, const PointerEvent& e) override {
    DragState& d = *d_;
    if (d.phase == DragState::Pressed) {
      std::optional<map::ArmyMark> stack = std::move(d.stack);
      d = DragState{};
      if (stack) expandStack(a, *stack);
      return true;
    }
    if (d.phase != DragState::Dragging) return false;
    d.pos = e.map + d.grab;
    drop(a);
    return true;
  }

  bool key(App& a, const platform::Event& e) override {
    using platform::Key;
    if (e.key == Key::Escape && e.mods == 0 && d_->phase == DragState::Dragging) {
      startReturn(a, *d_);
      return true;
    }
    if (e.key == Key::Delete && e.mods == 0 && a.ui.sel.type == SelType::Army && a.world().army(a.ui.sel.id) && d_->phase == DragState::Idle) {
      mil::askDisband(a, a.ui.sel.id);
      return true;
    }
    return false;
  }

  void renderOptions(App& a, map::RenderOptions& o) override {
    if (d_->active() && a.world().army(d_->army)) o.hideArmies.push_back(d_->army);
  }

  void drawOverlay(App& a, gfx::Canvas& c, const map::View& v) override {
    DragState& d = *d_;
    if (!d.active()) return;
    const World& w = mil::frameWorld(a);
    const Army* ar = w.army(d.army);
    if (!ar) {
      d = DragState{};
      return;
    }
    const ui::Theme& th = ui::theme();
    Vec2 pos = d.pos;
    if (d.phase == DragState::Returning) {
      double t = clamp((a.time() - d.returnStart) / kReturnSec, 0.0, 1.0);
      double k = 1 - (1 - t) * (1 - t) * (1 - t);
      pos = d.returnFrom + (d.origin - d.returnFrom) * k;
      if (t >= 1) {
        d = DragState{};
        a.requestRedraw();
        return;
      }
    }
    float size = a.map().figureSize();
    gfx::Pt g = v.toScreen(pos), o = v.toScreen(d.origin);
    Color col = mil::leaderColor(w, *ar), col2 = mil::allyColor(w, *ar);
    Status st = statusOf(a, d);
    Color tone = ui::toneColor(st.tone);
    bool blocked = d.preview.type == rules::EncounterType::Blocked && !d.siegeProvince;
    if (d.phase == DragState::Dragging && d.siegeProvince)
      if (const MapSymbol* s = detail::mapSymbol(w, d.siegeSymbol)) {
        // Цель штурма: замок или башня.
        const Box2 b = map::art::symbolBox(*s);
        const gfx::Pt sc = v.toScreen(b.center());
        const float rr = std::max(size * 0.7f, float(std::max(b.w(), b.h()) * v.zoom * 0.6));
        c.fillCircle(sc.x, sc.y, rr, tone.alpha(0.16f));
        c.strokeCircle(sc.x, sc.y, rr, 2.5f, tone);
      }
    if (d.phase != DragState::Returning) {
      // Исходная позиция и путь.
      gfx::Stroke dash;
      dash.width = 1.6f;
      dash.cap = gfx::Cap::Round;
      dash.dash = {6, 5};
      gfx::Pt line[2] = {o, g};
      c.polyline(line, 2, false, dash, tools::palette().light.alpha(0.67f));
      c.fillCircle(o.x, o.y, size * 0.5f, col.alpha(0.16f));
      c.strokeCircle(o.x, o.y, size * 0.5f, 1.5f, col.alpha(0.75f));
      // Цель встречи.
      if (const Army* t = w.army(d.preview.target); t && !blocked) {
        gfx::Pt tp = v.toScreen(t->pos);
        c.fillCircle(tp.x, tp.y, size * 0.7f, tone.alpha(0.18f));
        c.strokeCircle(tp.x, tp.y, size * 0.7f, 2.5f, tone);
      }
      if (blocked) {
        c.fillCircle(g.x, g.y, size * 0.66f, th.danger.alpha(0.2f));
        c.strokeCircle(g.x, g.y, size * 0.66f, 2.2f, th.danger);
      }
    }
    c.save();
    c.setOpacity(d.phase == DragState::Returning ? 0.7f : (blocked ? 0.6f : 0.94f));
    if (ar->isFleet()) gfx::drawFleetFigure(c, g, size, col, true, ar->allied(), col2);
    else gfx::drawArmyFigure(c, g, size, col, true, ar->allied(), col2);
    c.restore();
    if (d.phase == DragState::Dragging) {
      // Подпись: что произойдёт при отпускании (точки интерфейса).
      float s = ui::uiScale();
      float tw = std::min(ui::measure(st.text, ui::Font::Small), 280.f);
      RectF pr{g.x / s - (tw + 34) * 0.5f, (g.y + size * 0.74f) / s + 6, tw + 34, 26};
      ui::draw::shadow(pr, 13, 12, th.shadow, 2);
      ui::draw::rect(pr, th.surface1, 13);
      ui::draw::rectStroke(pr, st.tone == ui::Tone::Neutral ? th.borderStrong : tone.alpha(0.8f), 13, 1);
      ui::draw::icon(st.icon, RectF{pr.x + 9, pr.cy() - 7, 14, 14}, st.tone == ui::Tone::Neutral ? th.textDim : tone);
      ui::draw::text(st.text, RectF{pr.x + 27, pr.y, tw + 2, pr.h}, ui::Font::Small, th.text);
    }
  }

  bool animating(App&) override { return d_->phase == DragState::Returning; }

  platform::Cursor cursor(App& a) override {
    if (d_->phase == DragState::Dragging)
      return d_->preview.type == rules::EncounterType::Blocked && !d_->siegeProvince ? platform::Cursor::NotAllowed : platform::Cursor::Grabbing;
    if (a.ui.hover.type == SelType::Army) return a.readOnly() ? platform::Cursor::Hand : platform::Cursor::Grab;
    if (a.ui.hover.type == SelType::Route) return platform::Cursor::Hand;
    return platform::Cursor::Arrow;
  }

 private:
  std::shared_ptr<DragState> d_ = std::make_shared<DragState>();

  // Отпускание: решение по встрече (ТЗ 1.c.iv).
  void drop(App& a) {
    DragState& d = *d_;
    const World& w = mil::frameWorld(a);
    const Army* ar = w.army(d.army);
    if (!ar) {
      d = DragState{};
      return;
    }
    d.preview = encounterAt(a, d);
    siegeTarget(a, d);
    rules::Encounter e = d.preview;
    Id id = d.army;
    bool fleet = ar->isFleet();
    auto st = d_;
    auto decided = [st](App& x, bool ok) {
      if (ok) *st = DragState{};
      else startReturn(x, *st);
      x.requestRedraw();
    };
    if (d.siegeProvince) {
      // Штурм: войско встаёт у стен (место занято — ближайшее свободное рядом), затем окно штурма или захвата.
      const Id pid = d.siegeProvince;
      const Vec2 origin = d.origin;
      std::optional<Vec2> to = rules::validPosition(w, ArmyKind::Army, d.pos, id) ? std::optional<Vec2>(d.pos) : rules::findFreeSpot(w, ArmyKind::Army, d.pos, id);
      if (!to) {
        a.toast("Рядом со стенами нет свободного места", ToastKind::Warning, "warning");
        startReturn(a, d);
        return;
      }
      if (!a.act("Войско у стен: " + w.provinceName(pid), [&](Tx& tx) { rules::moveArmy(tx, id, *to); })) {
        startReturn(a, d);
        return;
      }
      d = DragState{};
      a.select(SelType::Army, id);
      flow::openSiege(a, id, pid, origin);
      return;
    }
    switch (e.type) {
      case rules::EncounterType::None: {
        if (dist2(d.pos, d.origin) < 1e-9) {
          d = DragState{};
          return;
        }
        Vec2 to = d.pos;
        if (a.act(fleet ? "Переместить флот" : "Переместить войско", [&](Tx& tx) { rules::moveArmy(tx, id, to); })) d = DragState{};
        else startReturn(a, d);
        return;
      }
      case rules::EncounterType::Merge:
      case rules::EncounterType::Alliance:
      case rules::EncounterType::DeclareWar:
        d.phase = DragState::Pending;
        mil::openEncounter(a, id, e.target, e, d.origin, decided);
        return;
      case rules::EncounterType::Battle:
        d.phase = DragState::Pending;
        mil::openBattle(a, id, e.target, d.origin, decided);
        return;
      default:
        a.toast(e.reason.empty() ? std::string("Сюда нельзя") : e.reason, ToastKind::Warning, "warning");
        startReturn(a, d);
        return;
    }
  }
};

class PanTool final : public MapTool {
 public:
  platform::Cursor cursor(App&) override { return platform::Cursor::Grab; }
};

ToolReg selectReg({ToolId::Select, "tool-select", "Выбор", "V", false, [] { return std::make_unique<SelectTool>(); }, 0});
ToolReg panReg({ToolId::Pan, "tool-pan", "Перемещение карты", "H", false, [] { return std::make_unique<PanTool>(); }, 1});

}  // namespace
}  // namespace rg::app
