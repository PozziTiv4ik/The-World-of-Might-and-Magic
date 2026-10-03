// Regnum — базовая карта мира во время работы: объекты карты, нарисованные кодом (assets/basemap/map.json).
//
// Папка (assets/basemap, собирается командой regnum-cli build-map из исходного изображения):
//   map.json   береговая линия, островки, реки и озёра, стены, горы, замки и башни (см. art.h)
// Береговая линия мира (geo::Coast) — кольца суши map.json. Маска моря и уменьшенные изображения карты (превью для
// экрана запуска и запасного слоя, миниатюра для мини-карты и списка миров) рисуются кодом: маска — при загрузке,
// изображения — при первом обращении. Исходное изображение редактор не загружает.
// После load() объект не меняется, поэтому его методы можно вызывать из нескольких потоков.
#pragma once
#include <memory>

#include "geo/ops.h"
#include "gfx/image.h"
#include "map/art.h"

namespace rg::map {

namespace art {
class Index;
}

class Basemap {
 public:
  Basemap();
  ~Basemap();
  Basemap(Basemap&&) noexcept;
  Basemap& operator=(Basemap&&) noexcept;

  // Прочитать map.json и построить маску моря. false и сообщение по-русски при ошибке.
  bool load(const std::string& dir, std::string* error = nullptr);
  bool loaded() const { return art_ != nullptr; }

  const std::string& dir() const { return dir_; }
  const std::string& id() const;
  const std::string& sourceSha256() const;
  int width() const;
  int height() const;
  Color oceanColor() const;
  Color landColor() const;
  const art::MapArt& art() const { return *art_; }
  const art::Index& index() const { return *index_; }

  geo::Coast coast() const;            // кольца суши; не загружено — UserError
  bool isOcean(Vec2 p) const;          // по маске; вне карты — false
  int maskWidth() const { return maskW_; }
  int maskHeight() const { return maskH_; }
  int maskScale() const { return kMaskScale; }

  // Карта целиком (суша, море, воды, знаки), нарисованная кодом: превью шириной 2000 и миниатюра шириной 480.
  // Рисуются при первом обращении (потокобезопасно); не загружено — nullptr.
  std::shared_ptr<const gfx::Image> preview() const;
  std::shared_ptr<const gfx::Image> thumb() const;

  static constexpr int kMaskScale = 4;   // пиксель маски моря = 4 × 4 единицы карты

 private:
  struct Lazy;
  std::string dir_;
  std::shared_ptr<const art::MapArt> art_;
  std::shared_ptr<const art::Index> index_;
  int maskW_ = 0, maskH_ = 0;
  std::vector<u8> mask_;  // 1 — море
  std::shared_ptr<Lazy> lazy_;
};

}  // namespace rg::map
