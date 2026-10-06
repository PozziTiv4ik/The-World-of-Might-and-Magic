// Regnum — отрисовка карты мира кодом (объекты map.json, см. art.h): море с мягким краем берега, внутренние воды,
// стены и знаки. Слои рисуются в изображение устройства: точка карты m -> (m.x · ds − ox, m.y · ds − oy), пиксель
// (i, j) — квадрат [i, i + 1) × [j, j + 1). Функции потокобезопасны (тайлы рисуются в нескольких потоках).
#pragma once
#include "gfx/canvas.h"
#include "map/art.h"

namespace rg::map::art {

struct Xf {
  double ds = 1, ox = 0, oy = 0;
  gfx::Pt operator()(Vec2 m) const { return {float(m.x * ds - ox), float(m.y * ds - oy)}; }
  Box2 mapBox(int w, int h) const { return Box2(ox / ds, oy / ds, (ox + w) / ds, (oy + h) / ds); }
};

// Пространственный индекс объектов карты (строится один раз; только чтение). Хранит указатель на MapArt: тот
// должен жить дольше индекса и не перемещаться.
class Index {
 public:
  Index() = default;
  explicit Index(const MapArt& a) { build(a); }
  void build(const MapArt& a);
  const MapArt& art() const { return *a_; }
  // Номера объектов, габарит которых пересекает b (без повторов, по возрастанию).
  void land(const Box2& b, std::vector<u32>& out) const { query(land_, b, out); }      // land, затем islets (номер ≥ land.size())
  void water(const Box2& b, std::vector<u32>& out) const { query(water_, b, out); }
  void rivers(const Box2& b, std::vector<u32>& out) const { query(rivers_, b, out); }
  void lines(const Box2& b, std::vector<u32>& out) const { query(lines_, b, out); }
  void symbols(const Box2& b, std::vector<u32>& out) const { query(symbols_, b, out); }

 private:
  struct Grid {
    double cell = 128;
    int cols = 0, rows = 0;
    std::vector<Box2> box;                     // габарит объекта
    std::vector<std::vector<u32>> cells;
  };
  void build(Grid& g, const std::vector<Box2>& boxes) const;
  static void query(const Grid& g, const Box2& b, std::vector<u32>& out);
  const MapArt* a_ = nullptr;
  Grid land_, water_, rivers_, lines_, symbols_;
};

// Цвета слоёв — стиль карты idx (map.json) в палитре pal (paletteStyle).
// Море: цвет моря поверх img с непрозрачностью «море минус суша» и мягким краем берега.
void drawSea(gfx::Image& img, const Index& idx, const Xf& P, Palette pal = Palette::Source);
// Реки и озёра: кольца воды (внешние контуры против часовой, острова — по часовой; правило NonZero, так что
// пересекающиеся озёра сливаются) и реки линией.
void drawWater(gfx::Image& img, const Index& idx, const Xf& P, Palette pal = Palette::Source);
// Стены и знаки (горы, замки, башни) — в порядке отрисовки.
void drawSymbols(gfx::Image& img, const Index& idx, const Xf& P, Palette pal = Palette::Source);
// Карта целиком (суша, море, воды, знаки) в изображение w × h.
gfx::Image render(const Index& idx, double ds, int w, int h, double ox = 0, double oy = 0, Palette pal = Palette::Source);

// Значок знака в пикселях устройства: точка привязки (середина основания) в at, масштаб ds · s; variant — рисунок;
// краска — палитры pal.
void drawSymbol(gfx::Canvas& c, Sym kind, gfx::Pt at, float scale, int variant = 0, Palette pal = Palette::Source);
// Габарит значка относительно точки привязки (единицы карты при s = 1) и габарит знака на карте.
RectF symbolBounds(Sym kind);
Box2 symbolBox(Sym kind, double x, double y, double s);
// Разработка: подбор параметров рисунка горы (design 0 или 1) под образец исходника ink (w × h, −1 — не важно) с
// точкой привязки (ax, ay). Возвращает параметры как инициализатор C++; error — итоговая ошибка.
std::string fitMountainIcon(const std::vector<i16>& ink, int w, int h, int ax, int ay, int design, int maxEvals, double* error);

}  // namespace rg::map::art
