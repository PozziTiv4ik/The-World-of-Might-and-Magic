// Regnum — общие помощники тестов текста (test_gfx_text_*.cpp): системные шрифты и стиль текста.
//
//   if (!texttest::fontsOk()) return;                     // без системных шрифтов тест пропускает проверки
//   drawText(c, "Арден", texttest::style(FontFamily::Display, FontWeight::Semibold, 24), x, y, col);
#pragma once
#include <cstdio>
#include <string>

#include "gfx/text.h"

namespace rg::texttest {

// Системные шрифты загружены (один раз на процесс; если их нет — причина печатается).
inline bool fontsOk() {
  static const bool ok = [] {
    std::string err;
    bool r = gfx::initFonts(&err);
    if (!r) std::printf("  [шрифты] системных шрифтов нет: %s\n", err.c_str());
    return r;
  }();
  return ok;
}

inline gfx::TextStyle style(gfx::FontFamily f = gfx::FontFamily::UI, gfx::FontWeight w = gfx::FontWeight::Regular, float size = 13) {
  gfx::TextStyle s;
  s.family = f;
  s.weight = w;
  s.size = size;
  return s;
}

}  // namespace rg::texttest
