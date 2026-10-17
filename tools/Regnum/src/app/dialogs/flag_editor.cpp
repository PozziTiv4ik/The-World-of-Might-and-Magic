// Regnum — окно «Флаг» (ТЗ 1.b.iv: флаг государства и гильдии; «Доработки №2», п.1 — эмблемы тем и рас). Слева —
// живой предпросмотр (крупно и в размерах списков), четыре цвета (поле, узор, третий, эмблема — геральдическая
// палитра), изображение PNG/JPEG (системный диалог, без него — ввод пути; хранится как PNG в Flag::png) и сетка узоров
// мини-флагами. Справа — выбор эмблемы: разделы «Темы» (классические и фантазийные группы) и «Расы» (группа на расу, по
// алфавиту), поиск по подписи эмблемы и названию группы (во всех разделах), «Без эмблемы»; слева от сетки — группы
// раздела для быстрого перехода (подсвечена группа в начале видимой части), сетка — крупные превью на цвете поля под
// эмблемой, выбранная обведена золотом. Узкое окно — эмблемы под настройками. Применение — одним действием (правка
// мира, Ctrl+Z отменяет).
#include "app/panels/faction_common.h"
#include "base/fs.h"
#include "codec/jpeg.h"
#include "codec/png.h"
#include "gfx/emblems.h"
#include "gfx/flag.h"

namespace rg::app {
namespace {

using namespace fac;

constexpr int kMaxImageSide = 640;   // больше не нужно: флаг рисуется не крупнее предпросмотра
constexpr float kChromeH = 32 + 74 + 86;   // поля экрана, шапка и подвал окна
constexpr float kLeftW = 300;              // колонка настроек в широком окне
constexpr float kWideMin = 860;            // ширина содержимого для двух колонок

struct Shared {
  Flag flag;
  std::string fileName;
};

// Прочитать изображение PNG/JPEG и записать во флаг как PNG (уменьшив при необходимости).
bool loadImageInto(App& a, Shared& st, const std::string& path) {
  auto bytes = fs::readFile(path);
  if (!bytes) {
    a.toast("Не удалось прочитать файл «" + fs::filename(path) + "»", ToastKind::Warning, "image");
    return false;
  }
  auto rgba = codec::decodePng(*bytes);
  if (!rgba) rgba = codec::decodeJpeg(*bytes);
  if (!rgba || rgba->empty()) {
    a.toast("Файл не похож на изображение PNG или JPEG", ToastKind::Warning, "image");
    return false;
  }
  codec::RgbaImage out;
  if (std::max(rgba->w, rgba->h) > kMaxImageSide) {
    gfx::Image img = gfx::Image::fromRgba(rgba->rgba.data(), rgba->w, rgba->h);
    double k = double(kMaxImageSide) / std::max(img.w, img.h);
    img = img.scaled(std::max(1, int(std::lround(img.w * k))), std::max(1, int(std::lround(img.h * k))));
    out = img.toRgbaImage();
  } else {
    out = std::move(*rgba);
  }
  std::vector<u8> png = codec::encodePng(out, 6);
  if (png.empty() || !codec::decodePng(png.data(), png.size())) {
    a.toast("Не удалось подготовить изображение флага", ToastKind::Warning, "image");
    return false;
  }
  st.flag.image = true;
  st.flag.png.assign(png.begin(), png.end());
  st.fileName = fs::filename(path);
  return true;
}

// Цвет поля под эмблемой (gfx/flag.h: эмблема — по центру или по месту узора): фон ячеек сетки эмблем.
Color emblemGround(const Flag& f) {
  const Color c0 = f.colors[0], c1 = f.colors[1], c2 = f.colors[2];
  switch (f.pattern) {
    case FlagPattern::H3:
    case FlagPattern::V3:
    case FlagPattern::Bend:
    case FlagPattern::Canton:
    case FlagPattern::Pale: return c1;
    case FlagPattern::Cross:
    case FlagPattern::Saltire: return c2 == c1 ? c1 : c2;
    case FlagPattern::Chevron: return c2;
    default: return c0;
  }
}

// Ячейка сетки: весь прямоугольник — цель щелчка и фокуса, подсказка — название.
struct CellHit {
  bool clicked = false, hovered = false, focused = false;
};
CellHit cell(std::string_view key, RectF r, std::string_view tip, bool selected, bool disabled, float radius = 8) {
  const ui::Theme& t = ui::theme();
  ui::WidgetId wid = ui::id(key);
  ui::Interaction it = disabled ? ui::Interaction{} : ui::interact(wid, r, ui::IfFocusable);
  float hv = ui::animate(wid ^ 0xce11ull, it.hovered ? 1.f : 0.f, 0.1f);
  if (selected) {
    ui::draw::rect(r, t.accent.alpha(0.18f), radius);
    ui::draw::rectStroke(r, t.accent, radius, 2);
  } else {
    ui::draw::rect(r, Color::mix(t.stripe, t.hover, hv), radius);
    ui::draw::rectStroke(r, hv > 0.01f ? t.borderStrong : t.border, radius, 1);
  }
  if (it.focused) focusRing(r, radius);
  if (it.hovered) ui::setCursor(platform::Cursor::Hand);
  if (!disabled) ui::hoverTip(std::string(key) + "#tip", r, tip);
  return {it.clicked, it.hovered, it.focused};
}

// Раздел группы эмблем.
int sectionOf(const gfx::EmblemGroup& g) { return g.section == gfx::EmblemSection::Races ? 1 : 0; }

class FlagEditor final : public Dialog {
 public:
  FlagEditor(Id faction, const Faction& f) : id_(faction), name_(displayName(f)), orig_(f.flag), st_(std::make_shared<Shared>()) {
    st_->flag = f.flag;
    // Раздел выбранной эмблемы.
    for (const gfx::EmblemGroup& g : gfx::emblemGroups())
      if (std::find(g.names.begin(), g.names.end(), f.flag.emblem) != g.names.end()) section_ = sectionOf(g);
  }
  const char* id() const override { return "flag"; }
  Style style(App&) override { return {"Флаг · " + name_, "flag", ui::Tone::Accent, 1000}; }

  bool draw(App& a) override {
    if (!a.world().faction(id_)) return false;   // фракцию удалили (например, отменой)
    Flag& fl = st_->flag;
    const bool img = fl.image && !fl.png.empty();
    const float W = ui::avail().w;
    const float H = std::max(360.f, ui::viewport().h - kChromeH);   // высота содержимого окна без шапки и подвала
    if (W >= kWideMin) {
      // Две колонки: настройки и эмблемы той же высоты (не выше окна).
      // Предпросмотр по высоте окна: настройки под ним должны поместиться.
      const float rest = 52 + 30 + 18 + patternsH(kLeftW) + 4 * 12;
      const bool small = H - rest >= 120 + 44;
      const float ph = std::clamp(H - rest - (small ? 44.f : 0.f), 96.f, 200.f);
      const float pickH = std::clamp(ph + (small ? 44.f : 0.f) + rest, 440.f, H);
      ui::Row row({ui::px(kLeftW), ui::fr(1)}, ui::kAuto, 24);
      {
        ui::Group g(kLeftW, 12);
        preview(a, fl, std::min(kLeftW, ph * 1.5f), ph, small);
        colors(a, fl, img);
        imageRow(a, img);
        patterns(a, fl, img);
      }
      emblems(a, fl, img, pickH);
    } else {
      // Узкое окно: предпросмотр и цвета в ряд, узоры, ниже — эмблемы.
      {
        ui::Row row({ui::px(228), ui::fr(1)}, ui::kAuto, 18);
        preview(a, fl, 228, 152, false);
        ui::Group g(0, 10);
        colors(a, fl, img);
        imageRow(a, img);
      }
      patterns(a, fl, img);
      const float top = 152 + patternsH(W) + 3 * 12;
      emblems(a, fl, img, std::max(220.f, std::min(H - top, 520.f)));
    }

    ui::ModalFooter foot;
    if (ui::button("Отмена")) return false;
    a.markUi("flag.cancel");
    const bool changed = !sameFlag(fl, orig_);
    if (ui::button("Применить", {.variant = ui::Variant::Primary, .icon = "check", .disabled = !a.canEdit(), .isDefault = true})) {
      if (!changed) return false;
      Flag nf = fl;
      if (a.act("Флаг: " + name_, [&](Tx& tx) { tx.faction(id_).flag = nf; })) return false;
    }
    a.markUi("flag.ok");
    return true;
  }

 private:
  static bool sameFlag(const Flag& x, const Flag& y) {
    return x.image == y.image && x.pattern == y.pattern && x.colors == y.colors && x.emblem == y.emblem && x.emblemColor == y.emblemColor &&
           x.png == y.png;
  }

  // ---------------------------------------------------------------- настройки
  void preview(App& a, const Flag& fl, float pw, float ph, bool small) {
    ui::Group g(pw, 8);
    const RectF pr = ui::next(pw, ph);
    drawFlagCopy(fl, pr, 8);
    a.markUi("flag.preview", pr);
    if (!small) return;
    ui::HStack hs(32, ui::Align::Left, 12);
    for (float w : {48.f, 36.f, 27.f, 21.f}) {
      const RectF r = ui::next(w, 32);
      drawFlagCopy(fl, RectF{r.x, std::round(r.cy() - w / 3), w, std::round(w * 2 / 3)}, w > 30 ? 3.f : 2.f);
    }
  }

  // Четыре цвета в ряд: подпись и образец (щелчок — палитра).
  void colors(App& a, Flag& fl, bool img) {
    const ui::Theme& t = ui::theme();
    static const char* const kNames[] = {"Поле", "Узор", "Третий", "Эмблема"};
    const RectF area = ui::next(52);
    const float gap = 8, cw = std::floor((std::min(area.w, kLeftW) - 3 * gap) / 4);
    ui::Disabled d(img);
    for (int i = 0; i < 4; i++) {
      ui::IdScope s{i};
      const RectF c{area.x + float(i) * (cw + gap), area.y, cw, area.h};
      ui::draw::text(kNames[i], RectF{c.x, c.y, c.w, 16}, ui::Font::Caption, img ? t.textMuted : t.textDim, ui::Align::Center);
      Color col = i < 3 ? fl.colors[size_t(i)] : fl.emblemColor;
      ui::at(RectF{std::round(c.cx() - 17), c.y + 18, 34, 34});
      if (ui::colorButton("c", col, {.tooltip = i < 3 ? std::string_view(kNames[i]) : std::string_view("Цвет эмблемы"), .hex = false})) {
        if (i < 3) fl.colors[size_t(i)] = col;
        else fl.emblemColor = col;
      }
      a.markUi(i < 3 ? "flag.color." + std::to_string(i) : std::string("flag.color.emblem"));
    }
  }

  void imageRow(App& a, bool img) {
    Flag& fl = st_->flag;
    ui::HStack hs(30, ui::Align::Left, 8);
    if (ui::button(img ? "Другое изображение…" : "Изображение…", {.icon = "upload", .size = ui::Size::Small, .tooltip = "Своё изображение флага: PNG или JPEG"}))
      pickImage(a);
    a.markUi("flag.upload");
    if (img) {
      if (ui::button("К узору", {.icon = "refresh", .size = ui::Size::Small, .tooltip = "Убрать изображение и вернуть узор с эмблемой"})) {
        fl.image = false;
        fl.png.clear();
        st_->fileName.clear();
      }
      a.markUi("flag.reset");
      ui::label(st_->fileName.empty() ? std::string("Изображение") : st_->fileName, {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "image"});
    }
  }

  // Узоры: мини-флаги по ширине колонки (не шире 92 точек).
  // Столбцов узоров: сколько помещается, строки поровну (без одинокого узора в последней строке).
  static int patternCols(float w) {
    const int n = int(FlagPattern::Count), fit = std::max(4, std::min(n, int((w + 7) / (54 + 7))));
    const int rows = (n + fit - 1) / fit;
    return (n + rows - 1) / rows;
  }
  static float patternsH(float w) {
    const int per = patternCols(w), rows = (int(FlagPattern::Count) + per - 1) / per;
    return 18 + 4 + float(rows) * 40 + float(rows - 1) * 7;
  }
  void patterns(App& a, Flag& fl, bool img) {
    const ui::Theme& t = ui::theme();
    const int n = int(FlagPattern::Count);
    const float W = ui::avail().w, gap = 7;
    const int per = patternCols(W);
    const float cw = std::min(92.f, std::floor((W - gap * float(per - 1)) / float(per))), ch = 40;
    const int rows = (n + per - 1) / per;
    int hovered = -1;
    const RectF capR = ui::next(18);
    const RectF area = ui::next(float(rows) * ch + float(rows - 1) * gap);
    for (int i = 0; i < n; i++) {
      ui::IdScope s{i};
      const RectF r{area.x + float(i % per) * (cw + gap), area.y + float(i / per) * (ch + gap), cw, ch};
      const CellHit h = cell("pat", r, schema::kFlagPatterns[i].name, !img && int(fl.pattern) == i, img, 7);
      if (h.hovered) hovered = i;
      Flag mini = fl;
      mini.image = false;
      mini.png.clear();
      mini.pattern = FlagPattern(i);
      mini.emblem.clear();
      const float fh = ch - 12, fw = std::min(cw - 12, std::round(fh * 1.5f));
      drawFlagCopy(mini, RectF{std::round(r.cx() - fw / 2), r.y + 6, fw, fh}, 3);
      if (img) ui::draw::rect(r, t.surface2.alpha(0.55f), 7);
      if (h.clicked) fl.pattern = FlagPattern(i);
      a.markUi("flag.pattern." + std::to_string(i), r);
    }
    const int shown = hovered >= 0 ? hovered : int(fl.pattern);
    ui::draw::text(utf8::upper("Узор · " + std::string(schema::kFlagPatterns[shown].name)), capR, ui::Font::Caption, t.textMuted);
  }

  // ---------------------------------------------------------------- эмблемы
  // Группа сетки: индекс в gfx::emblemGroups() и показанные эмблемы (после поиска).
  struct Shown {
    size_t group;
    std::vector<std::string_view> names;
  };
  std::vector<Shown> shownGroups() const {
    const auto& groups = gfx::emblemGroups();
    std::vector<Shown> out;
    for (size_t gi = 0; gi < groups.size(); gi++) {
      const gfx::EmblemGroup& g = groups[gi];
      Shown s{gi, {}};
      if (query_.empty()) {
        if (sectionOf(g) != section_) continue;
        for (const std::string& nm : g.names) s.names.push_back(nm);
      } else {
        // Поиск во всех разделах: по подписи эмблемы или по названию группы (вся группа).
        const bool all = utf8::matches(g.title, query_);
        for (const std::string& nm : g.names)
          if (all || utf8::matches(gfx::emblemTitle(nm), query_)) s.names.push_back(nm);
      }
      if (!s.names.empty()) out.push_back(std::move(s));
    }
    return out;
  }

  void emblems(App& a, Flag& fl, bool img, float height) {
    const ui::Theme& t = ui::theme();
    ui::Group root(0, 8);
    const std::string cur = fl.emblem.empty() ? std::string("без эмблемы") : std::string(gfx::emblemTitle(fl.emblem));
    // Разделы, поиск, «Без эмблемы».
    {
      ui::Row row({ui::px(204), ui::fr(1), ui::px(34)}, 34, 8);
      int sec = section_;
      if (ui::segmented("section", sec, {{"sparkles", "Темы", "Классические и фантазийные эмблемы"}, {"race", "Расы", "Эмблемы рас"}})) {
        section_ = sec;
        query_.clear();
        jump_ = -2;   // к началу раздела
      }
      a.markUi("flag.emblems.section");
      const std::string was = query_;
      ui::searchField("q", query_, "Поиск эмблемы");
      if (query_ != was) jump_ = -2;
      a.markUi("flag.emblems.search");
      const RectF nr = ui::next(34, 34);
      const CellHit h = cell("none", nr, "Без эмблемы", !img && fl.emblem.empty(), img, 7);
      const RectF in = nr.inset(7);
      ui::draw::rect(in, emblemGround(fl), 4);
      ui::draw::line(in.x + 4, in.bottom() - 4, in.right() - 4, in.y + 4, fl.emblemColor.alpha(0.9f), 2);
      if (img) ui::draw::rect(nr, t.surface2.alpha(0.55f), 7);
      if (h.clicked) fl.emblem.clear();
      if (h.hovered) hovered_ = "без эмблемы";
      a.markUi("flag.emblem.none", nr);
    }
    const std::vector<Shown> shown = shownGroups();
    size_t found = 0;
    for (const Shown& s : shown) found += s.names.size();
    const RectF capR = ui::next(16);
    {
      const std::string cap = utf8::upper("Эмблема · " + (hovered_.empty() ? cur : hovered_));
      ui::draw::text(cap, capR, ui::Font::Caption, t.textMuted);
      if (!query_.empty())
        ui::draw::text(found ? "найдено " + fmtInt(i64(found)) : std::string("ничего не найдено"), capR, ui::Font::Caption, t.textMuted, ui::Align::Right);
    }
    hovered_.clear();
    const float bodyH = std::max(160.f, height - 34 - 16 - 16);
    const RectF body = ui::next(bodyH);
    a.markUi("flag.emblems", body);
    if (shown.empty()) {
      ui::Area ea(body, 0);
      ui::spacer(std::max(0.f, bodyH * 0.3f));
      ui::emptyState("search", "Ничего не найдено");
      return;
    }
    const float navW = std::clamp(std::round(body.w * 0.27f), 132.f, 176.f);
    const RectF navR{body.x, body.y, navW, body.h};
    const RectF gridR{navR.right() + 12, body.y, body.right() - navR.right() - 12, body.h};
    ui::draw::line(navR.right() + 6, body.y, navR.right() + 6, body.bottom(), t.border, 1);
    const int active = grid(a, fl, img, gridR, shown);
    nav(a, navR, shown, active);
  }

  // Сетка групп: крупные превью по ширине области; группа, что помещается в один ряд (расы — по 4–6 эмблем), — с
  // подписью слева, иначе подпись над сеткой. Возвращает группу в начале видимой части.
  int grid(App& a, Flag& fl, bool img, RectF R, const std::vector<Shown>& shown) {
    const ui::Theme& t = ui::theme();
    constexpr float minCs = 64, gap = 8, capH = 24, groupGap = 14, capW = 128, pad = 3;   // pad — рамка выбранной у края
    ui::Area ga(R, 0);
    ui::Scroll sc("grid", R.h);
    const float W = ui::avail().w;
    const int per = std::max(1, int((W + gap) / (minCs + gap)));
    const float cs = std::floor((W + gap) / float(per) - gap);   // ячейки заполняют ширину
    const int perInline = std::max(1, int((W - capW + gap) / (cs + gap)));
    // Положение групп от начала области.
    struct Block {
      float y;
      bool inl;   // подпись слева, эмблемы в один ряд
    };
    std::vector<Block> blocks(shown.size());
    float y = pad;
    for (size_t k = 0; k < shown.size(); k++) {
      const int n = int(shown[k].names.size());
      const bool inl = n <= perInline;
      if (k > 0) y += inl && blocks[k - 1].inl ? gap : groupGap;
      blocks[k] = Block{y, inl};
      y += inl ? cs : capH + float((n + per - 1) / per) * (cs + gap) - gap;
    }
    // Последняя группа может прокрутиться к верху области: запас снизу.
    const float tail = std::max(0.f, R.h - (y - blocks.back().y));
    const RectF area = ui::next(y + tail);
    if (jump_ == -2) {
      sc.scrollTo(0, false);
      jump_ = -1;
    } else if (jump_ >= 0 && jump_ < int(shown.size())) {
      sc.scrollTo(blocks[size_t(jump_)].y - pad, true);
      jump_ = -1;
    }
    const float off = sc.offset(), vis0 = off - 2, vis1 = off + R.h + 2;
    int active = 0;
    for (size_t k = 0; k < shown.size(); k++)
      if (blocks[k].y <= off + pad + 8) active = int(k);
    bool anyFocused = false;
    for (size_t k = 0; k < shown.size(); k++) {
      const Shown& s = shown[k];
      const gfx::EmblemGroup& g = gfx::emblemGroups()[s.group];
      const Block b = blocks[k];
      const float x0 = b.inl ? capW : 0, y0 = b.y + (b.inl ? 0 : capH);
      const int cols = b.inl ? perInline : per;
      if (b.y + (b.inl ? cs : capH) > vis0 && b.y < vis1) {
        const std::string title = utf8::upper(std::string(g.title));
        const bool other = !query_.empty() && sectionOf(g) != section_;   // найдено в другом разделе
        if (b.inl) {
          caption(title, other ? (sectionOf(g) ? "Расы" : "Темы") : std::string_view(), RectF{area.x, area.y + b.y, capW - 12, cs});
        } else {
          const RectF cap{area.x, area.y + b.y, area.w, capH - 6};
          ui::draw::text(title, cap, ui::Font::Caption, t.textDim);
          if (other) ui::draw::text(sectionOf(g) ? "Расы" : "Темы", cap, ui::Font::Caption, t.textMuted, ui::Align::Right);
        }
      }
      for (size_t i = 0; i < s.names.size(); i++) {
        const std::string_view nm = s.names[i];
        ui::IdScope scope(nm);
        const float ry = y0 + float(int(i) / cols) * (cs + gap);
        const RectF r{area.x + x0 + float(int(i) % cols) * (cs + gap), area.y + ry, cs, cs};
        const std::string_view title = gfx::emblemTitle(nm);
        const bool sel = fl.emblem == nm;
        const bool visible = ry + cs >= vis0 && ry <= vis1;
        // Выбранная эмблема видна при открытии; ячейка, получившая фокус (Tab), прокручивается в видимую часть.
        if (visible || (sel && !scrolledToCurrent_) || focused_ == nm) {
          const CellHit h = cell("emb", r, title, !img && sel, img);
          a.markUi("flag.emblem." + std::string(nm), r);
          const bool focusMoved = h.focused && nm != focused_;
          if (h.focused) {
            anyFocused = true;
            focused_ = std::string(nm);
          }
          if (((sel && !scrolledToCurrent_) || focusMoved) && (ry < off || ry + cs > off + R.h))
            sc.scrollTo(std::max(0.f, int(i) < cols ? b.y - pad : ry - gap), focusMoved);   // первая строка — с подписью группы
          if (!visible) continue;
          if (h.hovered) hovered_ = std::string(title);
          const RectF in = r.inset(6);
          ui::draw::rect(in, emblemGround(fl), 6);
          const Color ec = fl.emblemColor;
          const std::string name(nm);
          ui::custom(in.inset(5), [name, ec](gfx::Canvas& c, RectF d, float) { gfx::drawEmblem(c, name, d, ec); });
          if (img) ui::draw::rect(r, t.surface2.alpha(0.55f), 8);
          if (h.clicked) fl.emblem = std::string(nm);
        }
      }
    }
    if (!anyFocused) focused_.clear();
    scrolledToCurrent_ = true;
    return active;
  }

  // Подпись группы слева от ряда эмблем: одна или две строки по ширине (перенос по пробелу), по центру по высоте;
  // note — раздел (при поиске в другом разделе) строкой ниже.
  static void caption(const std::string& text, std::string_view note, RectF r) {
    const ui::Theme& t = ui::theme();
    const float lh = ui::lineHeight(ui::Font::Caption);
    std::vector<std::string> lines{text};
    if (ui::measure(text, ui::Font::Caption) > r.w)
      if (const size_t sp = text.find(' '); sp != std::string::npos) lines = {text.substr(0, sp), text.substr(sp + 1)};
    const float h = lh * float(lines.size() + (note.empty() ? 0 : 1));
    float y = std::round(r.cy() - h / 2);
    for (const std::string& l : lines) {
      ui::draw::text(l, RectF{r.x, y, r.w, lh}, ui::Font::Caption, t.textDim);
      y += lh;
    }
    if (!note.empty()) ui::draw::text(note, RectF{r.x, y, r.w, lh}, ui::Font::Caption, t.textMuted);
  }

  // Группы раздела для быстрого перехода: название и число эмблем; подсвечена группа в начале видимой части сетки.
  void nav(App& a, RectF R, const std::vector<Shown>& shown, int active) {
    const ui::Theme& t = ui::theme();
    constexpr float rh = 28;
    ui::Area na(R, 0);
    ui::Scroll sc("nav", R.h);
    const RectF area = ui::next(float(shown.size()) * rh);
    // Активная группа сменилась — держать её видимой в списке.
    if (active != navActive_) {
      navActive_ = active;
      const float y = float(active) * rh, off = sc.offset();
      if (y < off) sc.scrollTo(y);
      else if (y + rh > off + R.h) sc.scrollTo(y + rh - R.h);
    }
    const float off = sc.offset();
    for (size_t k = 0; k < shown.size(); k++) {
      const float y = float(k) * rh;
      if (y + rh < off - 2 || y > off + R.h + 2) continue;
      const gfx::EmblemGroup& g = gfx::emblemGroups()[shown[k].group];
      ui::IdScope s{i64(shown[k].group)};
      const RectF r{area.x, area.y + y, area.w - 4, rh - 2};
      const ui::Interaction it = ui::interact(ui::id("##group"), r);
      const bool on = int(k) == active;
      if (on) {
        ui::draw::rect(r, t.accent.alpha(0.14f), 6);
        ui::draw::rect(RectF{r.x, r.y + 5, 3, r.h - 10}, t.accent, 1.5f);
      } else if (it.hovered) {
        ui::draw::rect(r, t.hover, 6);
      }
      const std::string cnt = std::to_string(shown[k].names.size());
      const float cw = std::ceil(ui::measure(cnt, ui::Font::Caption)) + 4;
      ui::draw::text(g.title, RectF{r.x + 10, r.y, r.w - 18 - cw, r.h}, ui::Font::Small, on ? t.accent : it.hovered ? t.text : t.textDim);
      ui::draw::text(cnt, RectF{r.right() - cw - 6, r.y, cw, r.h}, ui::Font::Caption, t.textMuted, ui::Align::Right);
      if (it.hovered) ui::setCursor(platform::Cursor::Hand);
      if (it.clicked) jump_ = int(k);
      a.markUi("flag.group." + std::to_string(shown[k].group), r);
    }
  }

  void pickImage(App& a) {
    std::shared_ptr<Shared> st = st_;
    detail::later(a, [st](App& x) {
      if (platform::dialogsSupported()) {
        auto path = platform::openFileDialog("Изображение флага", {{"Изображения PNG и JPEG", {"png", "jpg", "jpeg"}}}, fs::documentsDir());
        if (path) loadImageInto(x, *st, *path);
        return;
      }
      // Системных диалогов нет (а свой проводник выбирает только миры) — ввод пути к файлу.
      x.prompt("Изображение флага", "Путь к файлу PNG или JPEG", "", [st](App& y, const std::string& p) {
        std::string path = trim(p);
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"') path = path.substr(1, path.size() - 2);
        loadImageInto(y, *st, path);
      });
    });
  }

  Id id_;
  std::string name_;
  Flag orig_;
  std::shared_ptr<Shared> st_;
  int section_ = 0;                  // раздел эмблем: 0 — темы, 1 — расы
  std::string query_;                // поиск эмблем (во всех разделах)
  int jump_ = -1;                    // перейти к группе сетки в этом кадре (−2 — к началу)
  int navActive_ = -1;               // группа, подсвеченная в списке групп в прошлом кадре
  std::string hovered_;              // подпись эмблемы под указателем (подпись над сеткой)
  bool scrolledToCurrent_ = false;   // выбранная эмблема прокручена в видимую часть при открытии
  std::string focused_;              // эмблема в фокусе в прошлом кадре
};

std::unique_ptr<Dialog> makeFlagEditor(App& a, Id arg) {
  const Faction* f = a.world().faction(arg);
  if (!f) fail("Фракция не найдена");
  return std::make_unique<FlagEditor>(arg, *f);
}

DialogReg reg({"flag", makeFlagEditor});

}  // namespace
}  // namespace rg::app
