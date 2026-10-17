// Regnum — флаг государства (данные): узор из трёх цветов и эмблема или изображение PNG.
// Отрисовка — gfx/flag.h, хранение в мире — Faction::flag (core/world.h).
#pragma once
#include "base/base.h"

namespace rg {

enum class FlagPattern : u8 { Solid, H2, H3, V2, V3, Cross, Saltire, Quarters, Bend, Chevron, Border, Canton, Chief, Pale, Count };
struct Flag {
  bool image = false;            // true — изображение PNG, иначе конструктор
  FlagPattern pattern = FlagPattern::Solid;
  std::array<Color, 3> colors{Color::hex(0x7a4fd6), Color::hex(0xf2e3b3), Color::hex(0x1d2333)};
  std::string emblem;            // имя эмблемы (gfx/emblems) или пусто
  Color emblemColor = Color::hex(0xf2e3b3);
  std::string png;               // байты PNG, если image
};

}  // namespace rg
