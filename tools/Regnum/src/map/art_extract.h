// Regnum — разбор исходной карты в объекты карты (regnum-cli build-map): береговая линия, островки, озёра, реки,
// горы, замки, башни, стены. Исходное изображение — только вход разбора; редактор рисует карту кодом по map.json.
#pragma once
#include <array>

#include "codec/png.h"
#include "map/art.h"

namespace rg::map::art {

// ---------------------------------------------------------------- разбор
struct ExtractReport {
  int landRings = 0, isletRings = 0, waterRings = 0;
  i64 landPoints = 0, waterPoints = 0;
  std::array<int, size_t(Sym::Count)> symbols{};   // найдено знаков по видам
  int mountainForms = 0, mountainVariants = 0;     // образцов гор и вариантов рисунка
  int subpixel = 0, walls = 0;                     // знаков второго прохода (сдвиг на долю пикселя), линий стен
  int orderEdges = 0, orderCycles = 0;             // правил порядка наложения знаков и разорванных циклов
  i64 inkPixels = 0, unexplainedInk = 0;           // пикселей краски и не объяснённых найденными объектами
  std::vector<double> glowIn, glowOut;   // средняя доля воды (0..1) на расстоянии d = 0, 1, … от берега внутрь суши и в море
  int glowSamples = 0;
  double seconds = 0;
  std::string text() const;
};
// Разбор исходника (уже поверх белого, см. bake::flattenOnWhite). debugDir непусто — отладочные изображения.
MapArt extract(const codec::RgbaImage& flat, ExtractReport* report = nullptr, const std::string& debugDir = {});

// Образцы гор по рисункам (0 — светлая вершина, 1 — тёмный кончик) для подбора значков: краска w × h (−1 — не
// важно), точка привязки (середина основания).
struct MountainSample {
  int design = 0, w = 0, h = 0, ax = 0, ay = 0;
  std::vector<i16> ink;
};
std::vector<MountainSample> mountainSamples(const codec::RgbaImage& flat);

// ---------------------------------------------------------------- инвентаризация знаков (отладка разбора)
// Связная (8-связность) область «краски» (255 − B ≥ 16) исходника поверх белого.
struct InkComponent {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // габарит, включительно
  int pixels = 0;
  int dark = 0;       // пикселей темнее 60 по всем каналам (линии замков, башни, стены)
  bool mountain = false;  // есть «тело» ровного серого (горы)
  u64 shape = 0;      // хеш формы: уровни краски в габарите (одинаковые штампы — один хеш)
};
std::vector<InkComponent> inkComponents(const codec::RgbaImage& flat);
// Отчёт о группах одинаковых форм: dir/inventory.txt и dir/inventory.png (образцы групп, увеличенные).
void writeInventory(const codec::RgbaImage& flat, const std::string& dir);

}  // namespace rg::map::art
