// Помощники сценариев войск и флота: поиск фракций, провинций и объектов демонстрационного мира, точки карты на
// экране, перетаскивание войска, подсказка под указателем, прокрутка инспектора к элементу.
#pragma once
#include "app/panels/military.h"
#include "tests/test_app_util.h"

namespace rg::apptest {

// Провинция государства без войск, гарнизона и оккупации, где у точки подписи можно поставить войско (minPop — не
// меньше стольких жителей).
inline Id calmProvince(Harness& h, Id state, i64 minPop = 0) {
  const World& w = h->world();
  auto fs = geo::faces(w);
  Id found = 0;
  w.provinces.each([&](const Province& p) {
    if (found || p.sea || p.owner != state || p.occupied || !p.garrison.empty()) return;
    i64 pop = 0;
    for (const RacePop& r : p.races) pop += r.pop;
    if (pop < minPop) return;
    const geo::ProvinceShape* sh = fs->shape(p.id);
    if (!sh || !rules::validPosition(w, ArmyKind::Army, sh->label)) return;
    bool busy = false;
    w.armies.each([&](const Army& a) { busy = busy || app::mil::provinceUnder(w, a.pos) == p.id; });
    if (!busy) found = p.id;
  });
  return found;
}

// Перетаскивание по шагам (snapshot — снимок, пока кнопка ещё зажата).
inline void dragTo(Harness& h, gfx::Pt from, gfx::Pt to, const std::string& snapshot = {}) {
  hl::mouseMove(from.x, from.y);
  h.step();
  hl::mouseDown(from.x, from.y, platform::MouseLeft);
  h.step();
  for (int i = 1; i <= 10; i++) {
    hl::mouseMove(from.x + (to.x - from.x) * float(i) / 10, from.y + (to.y - from.y) * float(i) / 10);
    h.step();
  }
  if (!snapshot.empty()) {
    h.dropToasts();
    h.settle();
    CHECK(h.shot(snapshot));
  }
  hl::mouseUp(to.x, to.y, platform::MouseLeft);
  h.step();
  hl::advance(0.6);
}

// Подсказка, показанная сейчас: увести указатель (после щелчка подсказка элемента ждёт нового наведения), навести
// и подождать дольше задержки подсказки.
inline std::string tipAt(Harness& h, float x, float y) {
  h.move(2, 2);
  h.move(x, y);
  hl::advance(0.5);
  h.frames(4);
  return ui::tooltipText();
}

// Первое союзное войско (или флот) мира.
inline Id alliedArmy(const World& w, ArmyKind kind) {
  Id found = 0;
  w.armies.each([&](const Army& a) {
    if (!found && a.allied() && a.kind == kind) found = a.id;
  });
  return found;
}

// Объект фракции (лидер) данного вида, не союзный.
inline Id armyOf(const World& w, Id faction, ArmyKind kind) {
  Id found = 0;
  w.armies.each([&](const Army& a) {
    if (!found && a.kind == kind && !a.allied() && a.leader() == faction) found = a.id;
  });
  return found;
}

// Ищет на экране точку карты, где можно поставить объект вида kind (сетка с шагом step).
inline std::optional<std::pair<Vec2, gfx::Pt>> freeSpot(Harness& h, ArmyKind kind, float step = 23) {
  RectF area = h->mapArea();
  const World& w = h->world();
  for (float y = area.y + 40; y < area.bottom() - 40; y += step)
    for (float x = area.x + 40; x < area.right() - 40; x += step) {
      if (!freeOnScreen(h, x, y)) continue;
      Vec2 m = h->map().view().toMap(x, y);
      if (!rules::validPosition(w, kind, m)) continue;
      // Запас: соседние точки тоже допустимы (щелчок не попадёт на берег или край).
      bool ok = true;
      for (Vec2 d : {Vec2{20, 0}, Vec2{-20, 0}, Vec2{0, 20}, Vec2{0, -20}}) ok = ok && rules::validPosition(w, kind, m + d);
      if (!ok || h->map().armyAt(x, y)) continue;
      return std::make_pair(m, gfx::Pt{x, y});
    }
  return std::nullopt;
}

// Прокрутить инспектор колесом, пока элемент не окажется в его видимой части (true — виден).
// Элемент ещё не построен (строки таблиц ниже видимой части не строятся) — сначала вниз, затем вверх.
inline bool reveal(Harness& h, const std::string& mark) {
  h.settle();   // прокрутка к новой карточке (scrollToItem) идёт плавно — дождаться её
  for (int i = 0; i < 60; i++) {
    const RectF* r = h->uiRect(mark);
    const RectF* insp = h->uiRect("inspector");
    if (!insp) return r != nullptr;
    // Видимая часть — под полосой вкладок; ещё 44 точки — на закреплённую шапку таблицы.
    const RectF* tabs = h->uiRect("inspector.tabs");
    const float top = (tabs ? tabs->bottom() : insp->y + 160) + 44;
    if (r && r->y >= top && r->bottom() <= insp->bottom() - 12) return true;
    const float dir = r ? (r->y < top ? 2.f : -2.f) : (i < 30 ? -2.f : 2.f);
    h.wheel(insp->cx(), insp->cy() + 60, dir);
    h.settle();
  }
  return false;
}

}  // namespace rg::apptest
