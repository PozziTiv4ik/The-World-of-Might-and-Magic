// Regnum — экспорт всей карты в изображение (чтобы показать мир без редактора): тот же вид, что в редакторе, —
// режим карты, подписи, войска и флот, — без выделения, наведения и инструментов. Карта целиком ложится в кадр
// width × heightFor(width) (пропорции карты 16 : 9). Тайлы рисуются в фоне, поэтому экспорт идёт шагами: step()
// вызывается каждый кадр, пока не вернёт true.
#pragma once
#include <memory>

#include "map/mapview.h"

namespace rg::map {

class MapExport {
 public:
  static constexpr int kMinWidth = 640, kMaxWidth = 10000;
  static int heightFor(int width);

  MapExport(const Basemap* bm, const World& w, int width, const RenderOptions& opt);
  ~MapExport();
  MapExport(const MapExport&) = delete;
  MapExport& operator=(const MapExport&) = delete;

  int width() const;
  int height() const;
  // Шаг: запросить недостающие тайлы и дорисовать подписи; true — изображение готово.
  bool step();
  double progress() const;           // 0…1
  bool wait(double timeoutSec);      // дождаться фоновых тайлов (тесты, командная строка)
  const gfx::Image& image() const;   // premultiplied, непрозрачное
  gfx::Image take();                 // забрать готовое изображение (экспорт после этого пуст)

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

// Экспорт целиком, с ожиданием фоновых тайлов (тесты, командная строка).
gfx::Image exportMap(const Basemap* bm, const World& w, int width, const RenderOptions& opt);

}  // namespace rg::map
