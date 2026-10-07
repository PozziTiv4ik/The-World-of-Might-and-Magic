// Общее для сценариев деревьев технологий и построек (test_app_trees_*.cpp): скрытие тестовых регистраций
// других сценариев, отмена/повтор сочетаниями, щелчки по отмеченным элементам, поиск уведомлений.
#pragma once
#include "tests/test_app_util.h"

namespace rg::apptest::trees {

// Тестовые регистрации других сценариев («test.*»: вкладки, шапки, панели) на время сценария убираются из реестров,
// чтобы инспектор и снимки были как в приложении.
struct HideTestRegs {
  std::vector<app::TabDef> tabs;
  std::vector<app::HeaderDef> headers;
  std::vector<app::DrawerDef> drawers;
  template <class T>
  static void strip(std::vector<T>& v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](const T& d) { return std::string_view(d.id).starts_with("test."); }), v.end());
  }
  HideTestRegs() {
    auto& t = const_cast<std::vector<app::TabDef>&>(app::tabs());
    auto& h = const_cast<std::vector<app::HeaderDef>&>(app::headers());
    auto& d = const_cast<std::vector<app::DrawerDef>&>(app::drawers());
    tabs = t;
    headers = h;
    drawers = d;
    strip(t);
    strip(h);
    strip(d);
  }
  ~HideTestRegs() {
    const_cast<std::vector<app::TabDef>&>(app::tabs()) = tabs;
    const_cast<std::vector<app::HeaderDef>&>(app::headers()) = headers;
    const_cast<std::vector<app::DrawerDef>&>(app::drawers()) = drawers;
  }
  HideTestRegs(const HideTestRegs&) = delete;
  HideTestRegs& operator=(const HideTestRegs&) = delete;
};

// Отмена и повтор сочетаниями; уведомления «Отменено…» убираются, чтобы не держать кадры анимации.
inline void undo(Harness& h) {
  h.key(Key::Z, ctrl());
  h->toasts().clear();
}
inline void redo(Harness& h) {
  h.key(Key::Y, ctrl());
  h->toasts().clear();
}

inline RectF rectOf(app::App& a, const std::string& name) {
  const RectF* r = a.uiRect(name);
  if (!r) test::fail(__FILE__, __LINE__, "нет элемента " + name);
  return *r;
}

// Щелчок по отмеченному элементу (доли ширины и высоты) и ожидание покоя.
inline void clickRect(Harness& h, const std::string& name, float fx = 0.5f, float fy = 0.5f) {
  RectF r = rectOf(h.a(), name);
  h.click(r.x + r.w * fx, r.y + r.h * fy);
  h.settle();
}

// То же, но покой — не дольше 40 кадров (анимации окон, камеры и прокрутки успевают; мигание каретки — нет).
inline void clickQuick(Harness& h, const std::string& name, float fx = 0.5f, float fy = 0.5f) {
  RectF r = rectOf(h.a(), name);
  h.click(r.x + r.w * fx, r.y + r.h * fy);
  h.settle(40);
}

inline bool hasToast(app::App& a, const std::string& part, app::ToastKind kind) {
  for (const app::Toast& t : a.toasts())
    if (t.kind == kind && t.text.find(part) != std::string::npos) return true;
  return false;
}

// Вкладка инспектора по ID (для проверки видимости).
inline const app::TabDef* tabDef(std::string_view id) {
  for (const app::TabDef& t : app::tabs())
    if (id == t.id) return &t;
  return nullptr;
}

// Панель свойств полноэкранного дерева (справа от холста canvas): прокрутить колесом, пока элемент не окажется в её
// видимой части (по высоте холста).
// Щелчков колеса, чтобы сдвинуть содержимое на dist точек (щелчок — 64 точки).
inline float wheelNotches(float dist) { return std::clamp(std::ceil(dist / 64.f), 1.f, 10.f); }

inline bool revealSide(Harness& h, const std::string& name, const std::string& canvas = "bt.canvas") {
  for (int k = 0; k < 40; k++) {
    const RectF* r = h->uiRect(name);
    const RectF* cv = h->uiRect(canvas);
    if (!r || !cv) return false;
    const float lo = cv->y + 8, hi = cv->bottom() - 8;
    if (r->y >= lo && r->bottom() <= hi) return true;
    const bool up = r->y < lo;
    h.wheel(cv->right() + 90, cv->cy(), up ? wheelNotches(lo - r->y) : -wheelNotches(r->bottom() - hi));
    h.frames(16);
  }
  return false;
}

// Область прокрутки area (инспектор, страница, список окна): элемент — в видимой части (top и bottom — поля сверху и
// снизу).
inline bool revealIn(Harness& h, const std::string& name, const std::string& area = "inspector", float top = 60, float bottom = 12) {
  for (int k = 0; k < 40; k++) {
    const RectF* r = h->uiRect(name);
    const RectF* ar = h->uiRect(area);
    if (!r || !ar) return false;
    const float lo = ar->y + top, hi = ar->bottom() - bottom;
    if (r->y >= lo && r->bottom() <= hi) return true;
    h.wheel(ar->cx(), (lo + hi) * 0.5f, r->bottom() > hi ? -wheelNotches(r->bottom() - hi) : wheelNotches(lo - r->y));
    h.frames(16);
  }
  return false;
}

// Несколько кадров после действия: раскладка и отметки элементов обновлены (без ожидания полного покоя — мигание
// каретки после ввода держит перерисовку ещё 10 с).
inline void afterInput(Harness& h) { h.frames(8); }

// Щелчок по элементу панели свойств дерева (доли ширины и высоты).
inline bool clickSide(Harness& h, const std::string& name, const std::string& canvas = "bt.canvas", float fx = 0.5f, float fy = 0.5f) {
  if (!revealSide(h, name, canvas)) return false;
  RectF r = rectOf(h.a(), name);
  h.click(r.x + r.w * fx, r.y + r.h * fy);
  afterInput(h);
  return true;
}

// Выбор в выпадающем списке панели свойств: щелчок, ввод текста поиска, Enter.
inline bool pickSide(Harness& h, const std::string& combo, const std::string& text, const std::string& canvas = "bt.canvas") {
  if (!clickSide(h, combo, canvas)) return false;
  h.type(text);
  h.key(Key::Enter);
  afterInput(h);
  return true;
}

// Ввод в поле панели свойств: щелчок (всё выделено), замена текстом, Enter.
inline bool typeSide(Harness& h, const std::string& field, const std::string& value, const std::string& canvas = "bt.canvas") {
  if (!clickSide(h, field, canvas)) return false;
  h.retype(value);
  h.key(Key::Enter);
  afterInput(h);
  return true;
}

// Снимок без уведомлений и с дорисованной картой.
inline void shotTrees(Harness& h, const std::string& name) {
  h.waitMap();
  h.dropToasts();
  h.settle(60);
  CHECK(h.shot(name));
}

// Государство с провинциями (первое по порядку ID) и его сухопутная провинция со свободными слотами.
struct StateProv {
  Id state = 0, province = 0;
};
inline StateProv stateWithFreeSlots(const World& w, int free = 2, Id except = 0) {
  StateProv r;
  auto c = rules::calc(w);
  w.provinces.each([&](const Province& p) {
    if (r.province || p.sea || !p.owner || p.owner == except || p.occupied) return;
    const Faction* f = w.faction(p.owner);
    if (!f || !f->isState()) return;
    const rules::ProvinceCalc* pc = c->province(p.id);
    if (!pc || pc->slots < int(p.buildings.size()) + free) return;
    if (rules::hasModKey(w, p.modifiers, schema::mod::UndeadWaste) || rules::hasModKey(w, p.modifiers, schema::mod::Desecrated)) return;
    r = StateProv{p.owner, p.id};
  });
  return r;
}

inline const ProvBuilding* provBuilding(const World& w, Id pid, Id bid) {
  const Province* p = w.province(pid);
  if (!p) return nullptr;
  for (const ProvBuilding& pb : p->buildings)
    if (pb.building == bid) return &pb;
  return nullptr;
}

}  // namespace rg::apptest::trees
