// Regnum — помощники сценариев редакторов (константы, модификаторы, должности, переговоры и торговля, итоги хода,
// сроки технологий): прокрутка к элементу, выбор в списке с поиском, ввод числа, подтверждение.
#pragma once
#include "tests/test_app_util.h"

namespace rg::apptest::editors {

// Короткое ожидание: события и плавная прокрутка (settle ждал бы и мигание каретки после ввода).
inline void quick(Harness& h) {
  h.step();
  h.frames(12);
}

// Прокрутить область area колесом, пока элемент name не окажется в её видимой части.
inline bool reveal(Harness& h, const std::string& name, const char* area = "editor", float top = 8) {
  for (int i = 0; i < 40; i++) {
    const RectF* r = h->uiRect(name);
    const RectF* ar = h->uiRect(area);
    if (!r || !ar) return false;
    float lo = ar->y + top, hi = ar->bottom() - 12;
    if (r->y >= lo && r->bottom() <= hi) return true;
    float notches = r->bottom() > hi ? -2.f : 2.f;
    h.wheel(r->cx(), (lo + hi) * 0.5f, notches);
    quick(h);
  }
  return false;
}

inline bool clickRevealed(Harness& h, const std::string& name, const char* area = "editor", float top = 8) {
  if (!reveal(h, name, area, top)) return false;
  return h.clickUi(name);
}

// Выбрать в поле со списком пункт по тексту поиска (щелчок, ввод, Enter).
inline bool pickInCombo(Harness& h, const std::string& name, const std::string& query, const char* area = "editor", float top = 8) {
  if (!clickRevealed(h, name, area, top)) return false;
  h.type(query);
  h.key(Key::Enter);
  quick(h);
  return true;
}

// Ввести значение в поле: щелчок (всё выделено), ввод, Enter.
inline bool enterValue(Harness& h, const std::string& name, const std::string& value, const char* area = "editor", float top = 8) {
  if (!clickRevealed(h, name, area, top)) return false;
  h.retype(value);
  h.key(Key::Enter);
  quick(h);
  return true;
}

// Сдвинуть выбор в поле со списком на steps пунктов (вниз — плюс) и подтвердить.
inline bool comboStep(Harness& h, const std::string& name, int steps) {
  if (!clickRevealed(h, name)) return false;
  for (int i = 0; i < std::abs(steps); i++) h.key(steps > 0 ? Key::Down : Key::Up);
  h.key(Key::Enter);
  quick(h);
  return true;
}

inline bool confirmDialog(Harness& h) {
  quick(h);
  if (!h->hasDialog("confirm")) return false;
  if (!h.clickUi("dialog.ok")) return false;
  quick(h);
  return !h->hasDialog("confirm");
}

inline Id firstGuild(const World& w) {
  Id g = 0;
  w.factions.each([&](const Faction& f) {
    if (!g && f.isGuild()) g = f.id;
  });
  return g;
}

}  // namespace rg::apptest::editors
