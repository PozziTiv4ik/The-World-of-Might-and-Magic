// Regnum — помощники сценариев инструментов правки карты (test_app_tools_*.cpp, test_app_mapedit.cpp): площадь и
// грани провинции, показ области карты, щелчки и протяжки в координатах карты, снимок без ожидания покоя.
#pragma once
#include "app/tools/edit.h"
#include "tests/test_app_util.h"

namespace rg::toolstest {

using namespace rg::apptest;

inline double provArea(const World& w, Id p) {
  auto fs = geo::buildFaces(w);
  const geo::ProvinceShape* s = fs->shape(p);
  return s ? s->area : 0;
}
inline int provFaces(const World& w, Id p) {
  auto fs = geo::buildFaces(w);
  const geo::ProvinceShape* s = fs->shape(p);
  return s ? int(s->faces.size()) : 0;
}

// Показать область карты и дождаться конца анимации камеры.
inline void focus(Harness& h, Box2 b) {
  h->focusMap(b);
  h.settle();
}

inline void clickMap(Harness& h, Vec2 p, int button = platform::MouseLeft, u32 mods = 0) {
  gfx::Pt s = screenOf(h, p);
  h.click(s.x, s.y, button, mods);
}
inline void moveMap(Harness& h, Vec2 p) {
  gfx::Pt s = screenOf(h, p);
  h.move(s.x, s.y);
}

// Нажать, провести через точки (экран) и (по желанию) отпустить.
inline void stroke(Harness& h, const std::vector<gfx::Pt>& pts, bool release = true) {
  hl::mouseMove(pts[0].x, pts[0].y);
  h.step();
  hl::mouseDown(pts[0].x, pts[0].y);
  h.step();
  for (size_t i = 1; i < pts.size(); i++) {
    hl::mouseMove(pts[i].x, pts[i].y);
    h.step();
  }
  if (release) {
    hl::mouseUp(pts.back().x, pts.back().y);
    h.step();
    hl::advance(0.6);
  }
}
inline void release(Harness& h, gfx::Pt p) {
  hl::mouseUp(p.x, p.y);
  h.step();
  hl::advance(0.6);
}

// Перетащить точку карты в другую точку карты за n шагов.
inline void dragMap(Harness& h, Vec2 from, Vec2 to, int steps = 8, bool rel = true) {
  std::vector<gfx::Pt> pts;
  for (int i = 0; i <= steps; i++) pts.push_back(screenOf(h, from + (to - from) * (double(i) / steps)));
  stroke(h, pts, rel);
}

// Снимок без уведомлений, с готовыми тайлами.
inline bool cleanShot(Harness& h, const std::string& name) {
  h.waitMap();
  h.dropToasts();
  h.frames(3);
  return h.shot(name);
}

}  // namespace rg::toolstest
