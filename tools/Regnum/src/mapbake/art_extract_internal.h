// Regnum — внутреннее разбора исходной карты (mapbake/art_extract*.cpp): штампы знаков, их поиск и отладочные
// изображения. Не для использования вне mapbake.
#pragma once
#include <deque>

#include "mapbake/art_extract.h"
#include "mapbake/basemap_build.h"

namespace rg::map::art::detail {

inline int inkOf(const u8* px, size_t i) { return 255 - int(px[i * 4 + 2]); }
inline int minRgb(const u8* px, size_t i) { return std::min({int(px[i * 4]), int(px[i * 4 + 1]), int(px[i * 4 + 2])}); }

// ---------------------------------------------------------------- штампы
// Штампы исходника одинаковы и стоят в целых пикселях; поздние перекрывают ранние. Образец сравнивается с краской
// (255 − B) в «значимых» пикселях; перекрытый соседями штамп узнаётся по большинству своих пикселей.
struct Stamp {
  Sym kind = Sym::Mountain;
  int w = 0, h = 0;
  int ax = 0, ay = 0;            // точка привязки (середина основания) от левого верхнего угла
  std::vector<i16> ink;          // ожидаемая краска; −1 — не важно
  int care = 0;                  // значимых пикселей
  int kx = 0, ky = 0, k0 = 0, k1 = 0;   // ключ поиска: верхний значимый пиксель (k0) и пиксель под ним (k1)
  int variant = 0;               // форма с точностью до бледных пикселей края
  int design = 0;                // рисунок горы: 0 — светлая вершина, 1 — тёмный кончик вершины
  std::vector<u8> solid;         // непрозрачные пиксели штампа (у гор — весь силуэт с белой шапкой)
};

// Образцы гор — частые формы одиночных гор (variants — число вариантов рисунка).
std::vector<Stamp> mountainStamps(const codec::RgbaImage& flat, int* variants);

struct Found {
  Sym kind;
  int x, y;       // левый верхний угол штампа
  double score;
  int form = 0;   // номер образца горы
};

class StampFinder {
 public:
  explicit StampFinder(const codec::RgbaImage& flat) : px_(flat.rgba.data()), w_(flat.w), h_(flat.h) {}
  int ink(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
    return 255 - int(px_[(size_t(y) * size_t(w_) + size_t(x)) * 4 + 2]);
  }
  // Доли совпавших (|разница| ≤ tol) тёмных пикселей штампа (краска ≥ 100) и светлых (значимых, краска < 100).
  struct Score {
    double dark = 0, light = 0;
  };
  Score score(const Stamp& s, int x, int y, int tol) const {
    int dg = 0, dn = 0, lg = 0, ln = 0;
    for (int j = 0; j < s.h; j++)
      for (int i = 0; i < s.w; i++) {
        const int t = s.ink[size_t(j) * size_t(s.w) + size_t(i)];
        if (t < 0) continue;
        const bool ok = std::abs(ink(x + i, y + j) - t) <= tol;
        if (t >= 100) {
          dn++;
          dg += ok;
        } else {
          ln++;
          lg += ok;
        }
      }
    return {dn ? double(dg) / dn : 1.0, ln ? double(lg) / ln : 1.0};
  }
  // Корреляция (Пирсона) образца с краской в значимых пикселях: сходство формы независимо от яркости линий
  // (бледный или сглаженный штамп). 0 — образец или краска однородны.
  double correlation(const Stamp& s, int x, int y) const {
    i64 n = 0, sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    for (int j = 0; j < s.h; j++)
      for (int i = 0; i < s.w; i++) {
        const i64 t = s.ink[size_t(j) * size_t(s.w) + size_t(i)];
        if (t < 0) continue;
        const i64 v = ink(x + i, y + j);
        n++;
        sa += t;
        sb += v;
        saa += t * t;
        sbb += v * v;
        sab += t * v;
      }
    const double va = double(n * saa - sa * sa), vb = double(n * sbb - sb * sb);
    return va > 0 && vb > 0 ? double(n * sab - sa * sb) / std::sqrt(va * vb) : 0.0;
  }

 private:
  const u8* px_;
  int w_, h_;
};

// ---------------------------------------------------------------- поиск знаков
// Состояние поиска знаков: штампы, их положения на исходнике и объяснённая ими краска. Указатели placed ведут в
// штампы этого объекта, поэтому он не копируется.
struct SymbolSearch {
  // Штамп с вариантами сдвига на долю пикселя для второго прохода.
  struct Base {
    const Stamp* st;
    double scale;
    std::vector<Stamp> shifts;   // 16 сдвигов по 1/4 пикселя
    int dark = 0;                // тёмных пикселей без сдвига
  };
  // Знак и его штамп на исходнике (для порядка наложения).
  struct Placed {
    const Stamp* st;
    int x, y;
  };

  explicit SymbolSearch(const codec::RgbaImage& flat);
  SymbolSearch(const SymbolSearch&) = delete;
  SymbolSearch& operator=(const SymbolSearch&) = delete;

  const Stamp& stampOf(const Found& f) const { return f.kind == Sym::Tower ? tower : f.kind == Sym::Castle ? castle : mountains[size_t(f.form)]; }
  // Объяснены пиксели, где штамп ждёт краску и она совпала (части, закрытые соседями, остаются необъяснёнными).
  void mark(const Stamp& s, int x0, int y0);
  // Пиксель — краска знака (не вода).
  bool isInk(size_t i) const { return 255 - int(flat.rgba[i * 4 + 2]) >= 32 && int(flat.rgba[i * 4 + 2]) - int(flat.rgba[i * 4]) <= 40; }
  // Область comp (габарит bx0..bx1 × by0..by1) — бледные края уже найденного замка или башни: почти все её пиксели
  // на линиях штампа или рядом с ними (краска там светлее штампа, и mark их не объяснил).
  bool stampRemnant(const std::vector<u32>& comp, int bx0, int by0, int bx1, int by1) const;

  const codec::RgbaImage& flat;
  const int w, h;
  const StampFinder F;
  std::vector<Stamp> mountains;
  int mountainVariants = 0;
  Stamp tower, castle;
  std::vector<Base> bases;
  std::deque<Stamp> faded;       // штампы бледных знаков (deque — указатели placed на них не сдвигаются)
  std::vector<Placed> placed;    // штамп каждого знака art.symbols того же номера (у знаков этапа 6 штампа нет)
  std::vector<u8> explained;     // объяснённые штампами пиксели (для поиска стен, крупных гор и проверки)
};

// 4. Штампы в целых пикселях: поиск по ключевым пикселям, подтверждение по всему штампу, отсев повторов.
void placeStamps(SymbolSearch& S, MapArt& art, ExtractReport& rep);
// 5. Второй проход: штампы со сдвигом на долю пикселя, другого размера и частично перекрытые; где ни один не подошёл —
//    бледные и сглаженные замки и башни (по корреляции формы).
void placeShiftedStamps(SymbolSearch& S, MapArt& art, ExtractReport& rep);

// ---------------------------------------------------------------- отладка (art_extract_dev.cpp)
// Образцы гор: по одному на вариант рисунка, ×10 — dir/mountains.png.
void writeMountainSheet(const std::string& dir, const std::vector<Stamp>& mountains, int variants);
// Необъяснённая краска (dir/unexplained.png) и точки привязки знаков (dir/anchors.png).
void writeExplainedImages(const std::string& dir, const codec::RgbaImage& flat, const std::vector<u8>& explained, const MapArt& art);

}  // namespace rg::map::art::detail
