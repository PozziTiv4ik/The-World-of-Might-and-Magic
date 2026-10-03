// Regnum — базовая карта во время работы (см. basemap.h).
#include "map/basemap.h"

#include <mutex>

#include "base/fs.h"
#include "gfx/canvas.h"
#include "map/art_render.h"
#include "map/art_scene.h"

namespace rg::map {

// Превью и миниатюра рисуются по первому запросу, каждая под своим замком: миниатюра для мини-карты не ждёт превью,
// которое рисуется в фоне.
struct Basemap::Lazy {
  std::mutex previewMu, thumbMu;
  std::shared_ptr<const gfx::Image> preview, thumb;
};

namespace {

bool setErr(std::string* error, const std::string& msg) {
  if (error) *error = msg;
  return false;
}

const std::string& noText() {
  static const std::string s;
  return s;
}

}  // namespace

Basemap::Basemap() = default;
Basemap::~Basemap() = default;
Basemap::Basemap(Basemap&&) noexcept = default;
Basemap& Basemap::operator=(Basemap&&) noexcept = default;

const std::string& Basemap::id() const { return art_ ? art_->id : noText(); }
const std::string& Basemap::sourceSha256() const { return art_ ? art_->sourceSha256 : noText(); }
int Basemap::width() const { return art_ ? art_->width : 0; }
int Basemap::height() const { return art_ ? art_->height : 0; }
Color Basemap::oceanColor() const { return art_ ? art_->style.sea : Color(0, 38, 255); }
Color Basemap::landColor() const { return art_ ? art_->style.land : Color(255, 255, 255); }

bool Basemap::load(const std::string& dirIn, std::string* error) {
  *this = Basemap();
  const std::string dir = fs::absolute(dirIn);
  const std::string path = fs::join(dir, "map.json");
  if (!fs::isFile(path)) return setErr(error, "Базовая карта не найдена: нет map.json в «" + dir + "».");
  try {
    auto a = std::make_shared<art::MapArt>(art::load(path));
    if (a->land.empty()) return setErr(error, "В карте «" + path + "» нет суши.");
    if (a->id.empty()) return setErr(error, "В карте «" + path + "» нет идентификатора.");
    Basemap b;
    b.dir_ = dir;
    b.art_ = a;
    b.index_ = std::make_shared<art::Index>(*a);
    b.objects_ = std::make_shared<art::Objects>(art::baseObjects(*a));
    // Маска моря: клетка — море, если её центр вне колец суши и островков.
    b.maskW_ = (a->width + kMaskScale - 1) / kMaskScale;
    b.maskH_ = (a->height + kMaskScale - 1) / kMaskScale;
    gfx::Image m(b.maskW_, b.maskH_, 0);
    {
      gfx::Canvas c(m);
      gfx::Path p;
      const float k = 1.0f / kMaskScale;
      for (const auto* rings : {&a->land, &a->islets})
        for (const art::Ring& r : *rings) {
          if (r.size() < 3) continue;
          p.moveTo(float(r[0].x) * k, float(r[0].y) * k);
          for (size_t i = 1; i < r.size(); i++) p.lineTo(float(r[i].x) * k, float(r[i].y) * k);
          p.close();
        }
      c.fillPath(p, Color(255, 255, 255));
    }
    b.mask_.resize(m.px.size());
    for (size_t i = 0; i < m.px.size(); i++) b.mask_[i] = (m.px[i] >> 24) < 128 ? 1 : 0;
    b.lazy_ = std::make_shared<Lazy>();
    *this = std::move(b);
    return true;
  } catch (const std::exception& e) {
    return setErr(error, "Карта «" + path + "» повреждена: " + e.what());
  }
}

geo::Coast Basemap::coast() const {
  if (!loaded()) fail("Базовая карта не загружена.");
  geo::Coast c;
  c.width = art_->width;
  c.height = art_->height;
  c.landRings = art_->land;
  return c;
}

bool Basemap::isOcean(Vec2 p) const {
  if (mask_.empty() || !(p.x >= 0 && p.y >= 0 && p.x < width() && p.y < height())) return false;
  const int mx = std::min(maskW_ - 1, int(p.x / kMaskScale)), my = std::min(maskH_ - 1, int(p.y / kMaskScale));
  return mask_[size_t(my) * size_t(maskW_) + size_t(mx)] != 0;
}

std::shared_ptr<const gfx::Image> Basemap::preview() const {
  if (!loaded()) return nullptr;
  std::lock_guard<std::mutex> lk(lazy_->previewMu);
  if (!lazy_->preview) {
    const double ds = 2000.0 / width();
    lazy_->preview = std::make_shared<gfx::Image>(art::render(*index_, ds, 2000, int(std::lround(height() * ds))));
  }
  return lazy_->preview;
}

std::shared_ptr<const gfx::Image> Basemap::thumb() const {
  if (!loaded()) return nullptr;
  std::lock_guard<std::mutex> lk(lazy_->thumbMu);
  if (!lazy_->thumb) {
    const double ds = 480.0 / width();
    lazy_->thumb = std::make_shared<gfx::Image>(art::render(*index_, ds, 480, int(std::lround(height() * ds))));
  }
  return lazy_->thumb;
}

}  // namespace rg::map
