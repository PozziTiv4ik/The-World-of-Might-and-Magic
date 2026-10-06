// Regnum — карта мира, нарисованная кодом (assets/basemap/map.json): береговая линия, островки, озёра, реки, стены
// и знаки (горы, замки, башни) как объекты. Данные собираются из исходного изображения командой regnum-cli build-map
// (map/art_extract.cpp); само изображение редактор не показывает и не загружает. Отрисовка — map/art_render.cpp.
//
// Координаты — единицы карты (пиксели исходника 8000 × 4500, 0,0 — левый верхний угол).
#pragma once
#include "base/base.h"
#include "gfx/canvas.h"

namespace rg::map {

// Палитра карты, нарисованной кодом. Source — цвета исходника (стиль map.json: ярко-синее море, белая суша,
// чёрная краска знаков и стен); Parchment — приглушённое тёмно-синее море, суша цвета пергамента, реки и озёра
// глубже моря, знаки мягкой тёмно-бурой краской.
enum class Palette : u8 { Source, Parchment, Count };

}  // namespace rg::map

namespace rg::map::art {

using Ring = std::vector<Vec2>;

// Знаки карты. Точка привязки знака — середина основания (низ значка), s — масштаб относительно обычного размера.
enum class Sym : u8 { Mountain, Peak, Castle, Tower, Count };
const char* symName(Sym k);   // "mountain", "peak", "castle", "tower"

struct Symbol {
  Sym kind = Sym::Mountain;
  float x = 0, y = 0, s = 1;
  u8 v = 0;                // вариант рисунка (у гор — разная вершина)
};

struct River {
  std::vector<Vec2> pts;   // осевая линия от истока к устью
  std::vector<float> w;    // ширина в каждой точке, единицы карты
};

struct Line {
  std::vector<Vec2> pts;
  float w = 2;             // ширина линии
  float dash = 0;          // > 0 — пунктир: длина штриха (и промежутка), единицы карты
};

struct Style {
  Color sea{0, 38, 255, 255};
  Color land{255, 255, 255, 255};
  Color water{0, 38, 255, 255};   // реки и озёра
  float coastSoft = 2.5f;         // ширина светлой каймы моря у берега: σ размытия суши, единицы карты
  float coastGlow = 0.3f;         // сила каймы: доля размытой суши, на которую светлеет море (0 — нет)
  // От палитры, в map.json не хранятся. Знаки нарисованы серыми: серый g рисуется как mix(ink, paper, g / 255)
  // (у исходника краска чёрная и бумага белая, то есть серый остаётся собой).
  Color ink{0, 0, 0, 255};        // краска стен и знаков
  Color paper{255, 255, 255, 255};
  Color outside{0, 0, 0, 0};      // фон окна за краем карты; a == 0 — цвет темы интерфейса
  Color tone(u8 g) const;
};

// Стиль карты в палитре p: Source — s как есть, иначе цвета палитры (ширина каймы — из s).
Style paletteStyle(const Style& s, Palette p);

struct MapArt {
  std::string id, source, sourceSha256;
  int width = 0, height = 0;
  Style style;
  std::vector<Ring> land;       // кольца суши (береговая линия мира), ориентация не важна
  std::vector<Ring> islets;     // мелкие островки и скалы у берега (только рисунок, не суша мира)
  std::vector<Ring> water;      // внутренние воды (реки и озёра): кольца, острова — внутренние кольца (чёт-нечет)
  std::vector<River> rivers;
  std::vector<Line> lines;      // стены и пунктиры
  std::vector<Symbol> symbols;  // в порядке отрисовки (сверху вниз по основанию)

  bool empty() const { return width <= 0 || height <= 0; }
};

// Чтение и запись map.json. Ошибка — UserError.
MapArt load(const std::string& path);
void save(const MapArt& a, const std::string& path);
std::string toJson(const MapArt& a);
MapArt fromJson(std::string_view json);

}  // namespace rg::map::art
