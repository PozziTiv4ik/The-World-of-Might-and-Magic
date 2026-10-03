// Regnum — разбор исходного изображения карты (regnum-cli build-map, map/art_extract.cpp): разложение на слои
// (открытое море, внутренние воды, знаки) и береговая линия.
//
// Модель исходника: каждый пиксель (поверх белого) = t · синий #0026FF + (1 − t) · серый(g),
// где t = (B − R) / 255 — доля воды, а 255 − B = (1 − t)(255 − g) — количество «краски» условных знаков.
//
// Слои (белая суша → ocean → inland → symbols) поверх белого дают исходник:
//   ocean   — синий с альфой воды, только открытое море: вода (t > 0,5) с мелкими островками, эрозия на r,
//             связь с краем карты, дилатация на r + 2, мягкий край берега (0 < t ≤ 0,5) рядом с морем;
//   inland  — то же для рек и озёр (вся прочая вода);
//   symbols — нейтральный серый с альфой (горы со снежными шапками, башни, замки, стены).
// Под непрозрачными знаками вода и поле берега продолжаются от соседей.
// Береговая линия: marching squares по полю берега, упрощение с сохранением топологии, проверка ядром геометрии.
// Всё детерминировано: один и тот же исходник даёт тот же результат.
#pragma once
#include "base/base.h"
#include "codec/png.h"
#include "geo/ops.h"

namespace rg::map::bake {

constexpr Color kOcean{0, 38, 255, 255};
constexpr Color kLand{255, 255, 255, 255};
constexpr int kCoordScale = 100;  // координаты берега хранятся в сотых долях пикселя

struct Options {
  std::string id = "wmm-expanded-v1";
  std::string sourceName = "Expanded Map.png";
  std::string sourceSha256;     // SHA-256 файла исходника (hex), пусто — не записывать
  int erodeRadius = 6;          // эрозия воды: протоки уже 2r+1 не соединяют воду с морем
  int isletFillArea = 160;      // островки до этой площади (пиксели²) при поиске моря считаются водой
  int isletFillSide = 17;       // ... и с габаритом меньше этого (пиксели): дельты рек крупнее
  double simplifyTol = 0.75;    // допуск упрощения берега, пиксели
  double minIsletArea = 12;     // острова меньшей площади (пиксели²) не входят в береговую линию
};

// ---------------------------------------------------------------- слои
enum Kind : u8 { KindOcean = 1, KindInland = 2, KindInk = 4, KindMountain = 8 };

struct Layers {
  int w = 0, h = 0;
  std::vector<u8> ocean, inland;  // альфа синего kOcean
  std::vector<u8> symA, symV;     // символы: альфа и серый (не premultiplied; при альфе 0 серый = 0)
  std::vector<u8> field;          // поле берега: доля открытого моря 0..255 (вне моря 0); море — field ≥ 128
  std::vector<u8> kind;           // биты Kind (разбор и проверки)
};

struct SegmentStats {
  i64 oceanPx = 0, inlandPx = 0, inkPx = 0, opaquePx = 0;
  int components = 0;   // связные области краски
  int mountains = 0;    // из них горы
  int snowCaps = 0;     // замкнутых белых областей (снежных шапок), непрозрачных внутри гор
  int pockets = 0;      // замкнутые «карманы» моря, переведённые во внутренние воды
  i64 pocketPx = 0;
  int isletsFilled = 0; // островков, не мешавших поиску моря
  i64 isletFillPx = 0;
  i64 haloPx = 0;       // пикселей мягкого края берега, присоединённых к морю за пределами дилатации
  i64 inpaintedPx = 0;  // пикселей под непрозрачными знаками, где вода и поле продолжены от соседей
  double seconds = 0;
};

// Исходник поверх белого (альфа становится 255).
void flattenOnWhite(codec::RgbaImage& img);
// Разложение на слои. Исходник должен быть уже поверх белого.
Layers segment(const codec::RgbaImage& flat, const Options& opt, SegmentStats* stats = nullptr);

// Цвет пикселя i композиции слоёв поверх белого.
void compositePixel(const Layers& L, size_t i, u8 rgb[3]);
// Композиция прямоугольника поверх цвета base; tint (альфа > 0) — заливка провинции между сушей и морем.
codec::RgbaImage composite(const Layers& L, int x0, int y0, int w, int h, Color tint = Color(0, 0, 0, 0));

struct CompositeError {
  double mean = 0;              // средняя абсолютная ошибка по каналам
  int max = 0;                  // наибольшая ошибка канала
  i64 pixels = 0, over1 = 0, over2 = 0;   // пикселей всего, с ошибкой > 1 и > 2
  bool symbolsNeutral = true;   // в слое символов нет цвета (R = G = B)
};
CompositeError compareComposite(const codec::RgbaImage& flat, const Layers& L);

// ---------------------------------------------------------------- берег
struct IPt {
  i32 x = 0, y = 0;  // сотые доли пикселя
  bool operator==(const IPt&) const = default;
};
using IRing = std::vector<IPt>;

struct CoastStats {
  int rawRings = 0;      // контуров суши после marching squares
  i64 rawPoints = 0;
  int islets = 0;        // отброшено малых островов
  int holes = 0;         // отброшено дыр (замкнутых участков моря внутри суши)
  int rings = 0;
  i64 points = 0;        // после упрощения
  int rounds = 0;        // итераций исправления топологии
  i64 refined = 0;       // точек, возвращённых ради топологии
  double seconds = 0;
};

// Контуры суши (field < 128) по полю берега; за краем — море. Кольца с площадью < minArea отбрасываются.
// Ориентация — площадь > 0 по формуле Гаусса в координатах карты (как geo::signedArea). Координаты округлены.
std::vector<IRing> traceCoast(const u8* field, int w, int h, double minArea, CoastStats* stats = nullptr);
// Дуглас — Пекер с допуском tol (пиксели) без новых пересечений, касаний и вложений. w × h — размер карты:
// углы рамки в кольцах остаются вершинами.
std::vector<IRing> simplifyCoast(const std::vector<IRing>& rings, double tol, int w, int h, CoastStats* stats = nullptr);

struct RingCheck {
  i64 selfHits = 0;    // самопересечения и самокасания
  i64 ringHits = 0;    // пересечения и касания разных колец
  i64 nested = 0;      // кольцо внутри другого
  i64 degenerate = 0;  // < 3 точек, повторы, нулевая площадь, неверная ориентация
  i64 outside = 0;     // точки за пределами карты
  double minGap = 0;   // наименьшее расстояние между разными кольцами (пиксели, не больше 1)
  bool ok() const { return selfHits == 0 && ringHits == 0 && nested == 0 && degenerate == 0 && outside == 0; }
};
RingCheck checkRings(const std::vector<IRing>& rings, int w, int h);
double ringArea(const IRing& r);  // пиксели², знак как у geo::signedArea
// Маска моря mw × mh: 255 — море (центр клетки вне колец суши), 0 — суша.
std::vector<u8> oceanMask(const std::vector<IRing>& rings, int mw, int mh, double scale);

// Кольца в координатах карты (пиксели) для ядра геометрии.
geo::Coast toCoast(const std::vector<IRing>& rings, int w, int h);

// Проверка настоящим ядром геометрии: новый мир, geo::initFromCoast, geo::validate, грани.
struct GeoCheck {
  bool built = false;            // initFromCoast выполнен без ошибки
  std::string error;             // сообщение initFromCoast, если не выполнен
  std::vector<std::string> issues;  // geo::validate: «код: сообщение (x, y)»
  int nodes = 0, edges = 0, coastEdges = 0;
  int landFaces = 0, seaFaces = 0;
  double landArea = 0, ringArea = 0;  // площадь суши по граням и по кольцам (пиксели²)
  double seconds = 0;
  bool ok() const { return built && issues.empty() && landFaces > 0 && std::fabs(landArea - ringArea) <= 1e-6 * std::max(1.0, ringArea); }
};
GeoCheck geoCheck(const geo::Coast& coast);

// Запись PNG из RGBA с проверкой.
void writePng(const std::string& path, const codec::RgbaImage& img, int level);
// SHA-256 в нижнем регистре.
std::string sha256Hex(const void* data, size_t size);

}  // namespace rg::map::bake
