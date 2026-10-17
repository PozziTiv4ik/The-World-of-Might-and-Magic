// Regnum — помощники сценариев классов, подклассов и талантов героев (test_app_heroes_classes.cpp,
// test_app_heroes_talents.cpp): поиск класса, подкласса и таланта по названию, ранг героя, перетаскивание элемента
// на элемент, проверка, что элемент целиком внутри другого.
#pragma once
#include "app/common/talent_tree.h"
#include "tests/test_app_editors_util.h"

namespace rg::herotest {

using namespace rg::apptest;

inline std::string sid(Id id) { return std::to_string(id); }

inline Id classId(const World& w, std::string_view name) { return test::idByName(w.catalogs->classes, std::string(name)); }

inline Id subId(const World& w, Id cls, std::string_view name) {
  const HeroClass* c = w.heroClass(cls);
  if (!c) return 0;
  for (const SubClass& s : c->subclasses)
    if (s.name == name) return s.id;
  return 0;
}

inline const SubClass* subOf(const World& w, Id sub) { return w.catalogs->subclass(sub); }

inline Id talentId(const World& w, Id sub, std::string_view name) {
  const SubClass* s = subOf(w, sub);
  if (!s) return 0;
  for (const Talent& t : s->talents)
    if (t.name == name) return t.id;
  return 0;
}

inline const Talent* talentOf(const World& w, Id id) {
  const SubClass* c = rules::talentSubClass(w, id);
  return c ? c->talent(id) : nullptr;
}

inline int rankOf(Harness& h, Id hero, Id talent) { return rules::heroTalentRank(h->world(), hero, talent); }

// Перетащить центр элемента a в центр элемента b.
inline bool dragUi(Harness& h, const std::string& a, const std::string& b) {
  const RectF* ra = h->uiRect(a);
  const RectF* rb = h->uiRect(b);
  if (!ra || !rb) return false;
  const RectF x = *ra, y = *rb;
  h.drag(x.cx(), x.cy(), y.cx(), y.cy());
  editors::quick(h);
  return true;
}

// Прямоугольник a целиком внутри b (с допуском).
inline bool inside(const RectF* a, const RectF* b) {
  return a && b && a->x >= b->x - 0.5f && a->right() <= b->right() + 0.5f && a->y >= b->y - 0.5f && a->bottom() <= b->bottom() + 0.5f;
}

}  // namespace rg::herotest
