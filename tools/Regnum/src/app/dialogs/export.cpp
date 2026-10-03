// Regnum — экспорт карты в картинку (DialogReg «map.export», Ctrl+E): размер (Full HD, 4K, 8K, 1:1 или своя ширина),
// подписи, войска и флот; файл PNG — системным диалогом (без него — в папку мира или «Документы»). Карта рисуется
// шагами в кадрах (тайлы — в фоне), PNG пишется в фоне; по готовности — уведомление с кнопкой «Показать».
#include <future>

#include "app/app_internal.h"
#include "base/fs.h"
#include "base/jobs.h"
#include "codec/png.h"
#include "map/export.h"

namespace rg::app {

namespace {

struct SizeItem {
  const char* label;
  int width;   // 0 — своя
  const char* tip;
};
constexpr SizeItem kSizes[] = {
    {"Full HD", 1920, "1920 × 1080"},
    {"4K", 3840, "3840 × 2160"},
    {"8K", 7680, "7680 × 4320"},
    {"1:1", 8000, "Как исходная карта: 8000 × 4500"},
    {"Свой", 0, "Своя ширина"},
};
constexpr int kCustom = int(std::size(kSizes)) - 1;

struct State {
  int size = 2;          // 8K
  int custom = 6000;
  bool labels = true, armies = true;
  std::string path;
  std::unique_ptr<map::MapExport> exp;
  std::future<std::string> save;   // текст ошибки, пусто — успех
  bool saving = false;
};

int widthOf(const State& s) {
  return s.size < kCustom ? kSizes[s.size].width : clamp(s.custom, map::MapExport::kMinWidth, map::MapExport::kMaxWidth);
}

std::string dims(int w) { return fmtInt(w) + " × " + fmtInt(map::MapExport::heightFor(w)); }

void begin(App& a, State& s) {
  map::RenderOptions o;
  o.mode = a.ui.mapMode;
  o.labels = s.labels;
  o.darkUi = a.ui.darkTheme;
  if (!s.armies) a.world().armies.each([&](const Army& ar) { o.hideArmies.push_back(ar.id); });
  s.exp = std::make_unique<map::MapExport>(a.basemap(), a.world(), widthOf(s), o);
}

class ExportDialog final : public Dialog {
 public:
  const char* id() const override { return "map.export"; }
  Style style(App&) override { return {"Экспорт карты", "image", ui::Tone::Accent, 460}; }
  void dismissed(App&) override { s_->exp.reset(); }

  bool draw(App& a) override {
    State& s = *s_;
    if (s.saving) return saving(a);
    if (s.exp) return rendering(a);
    ui::caption("Размер");
    std::vector<ui::Segment> segs;
    for (const SizeItem& k : kSizes) segs.push_back(ui::Segment{nullptr, k.label, k.tip});
    ui::segmented("size", s.size, std::span<const ui::Segment>(segs));
    a.markUi("export.size");
    if (s.size == kCustom) {
      ui::numberField("width", s.custom,
                      {.min = map::MapExport::kMinWidth, .max = map::MapExport::kMaxWidth, .step = 160, .unit = "px", .label = "Ширина", .steppers = true});
      a.markUi("export.width");
    }
    ui::label(dims(widthOf(s)) + " · PNG", {.font = ui::Font::Small, .ink = ui::Ink::Dim});
    ui::spacer(4);
    ui::checkbox("Подписи", s.labels);
    a.markUi("export.labels");
    ui::checkbox("Войска и флот", s.armies);
    a.markUi("export.armies");
    ui::spacer(8);
    ui::HStack hs(32, ui::Align::Left, 8);
    ui::flex();
    if (ui::button("Отмена")) return false;
    if (ui::button("Сохранить…", {.variant = ui::Variant::Primary, .icon = "download", .isDefault = true})) choose(a);
    a.markUi("export.save");
    return true;
  }

 private:
  std::shared_ptr<State> s_ = std::make_shared<State>();

  // Файл: системный диалог сохранения (без него — папка мира или «Документы»).
  void choose(App& a) {
    auto st = s_;
    std::string name = detail::sanitizeName(a.worldTitle());
    if (name.empty()) name = "Карта";
    name += " " + std::to_string(widthOf(*st)) + ".png";
    const std::string dir = a.projectPath().empty() ? fs::documentsDir() : fs::parent(a.projectPath());
    if (!platform::dialogsSupported()) {
      st->path = fs::join(dir, name);
      begin(a, *st);
      return;
    }
    detail::later(a, [st, name, dir](App& x) {
      auto r = platform::saveFileDialog("Экспорт карты", {{"Изображение PNG", {"png"}}}, dir, name);
      if (!r || r->empty()) return;
      std::string p = *r;
      if (utf8::lower(fs::ext(p)) != ".png") p += ".png";
      st->path = p;
      begin(x, *st);
    });
  }

  bool rendering(App& a) {
    State& s = *s_;
    ui::label("Рисуется карта " + dims(s.exp->width()), {.font = ui::Font::Body});
    ui::spacer(4);
    ui::progress(s.exp->progress());
    a.markUi("export.progress");
    ui::requestRedraw();
    if (s.exp->step()) {
      // Готово: PNG — в фоне (картинка уходит в задачу).
      gfx::Image img = s.exp->take();
      s.exp.reset();
      s.saving = true;
      s.save = jobs::submit([img = std::move(img), path = s.path]() mutable -> std::string {
        codec::RgbaImage out;
        out.w = img.w;
        out.h = img.h;
        out.rgba = img.toRgba();
        img = gfx::Image();
        return codec::writePngFile(path, out, 6) ? std::string() : "не удалось записать «" + path + "»";
      });
    }
    ui::spacer(8);
    ui::HStack hs(32, ui::Align::Left, 8);
    ui::flex();
    if (ui::button("Отмена")) {
      s.exp.reset();
      return false;
    }
    return true;
  }

  bool saving(App& a) {
    State& s = *s_;
    {
      ui::HStack hs(28, ui::Align::Left, 10);
      ui::spinner(18);
      ui::label("Сохраняется " + fs::filename(s.path), {.font = ui::Font::Body});
    }
    a.markUi("export.saving");
    ui::requestRedraw();
    if (s.save.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return true;
    const std::string err = s.save.get();
    if (err.empty()) {
      const std::string dir = fs::parent(s.path);
      a.toast("Карта сохранена: " + fs::filename(s.path), ToastKind::Success, "image", "Показать", [dir](App&) { platform::openPath(dir); });
    } else {
      a.toast("Не удалось сохранить картинку: " + err, ToastKind::Danger, "warning");
    }
    return false;
  }
};

DialogReg reg({"map.export", [](App&, Id) -> std::unique_ptr<Dialog> { return std::make_unique<ExportDialog>(); }});

}  // namespace

}  // namespace rg::app
