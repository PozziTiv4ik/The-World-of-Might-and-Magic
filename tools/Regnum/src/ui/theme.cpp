// Regnum — тема интерфейса: цветовые схемы (токены цветов, ARCHITECTURE.md §6), роли шрифтов, тоны, сочетания клавиш.
#include "ui/ui_internal.h"

namespace rg::ui {

using namespace in;

namespace {

// Основа схемы: фон страниц, поверхности (строки и панели, карточки, поля), рамки, текст, акцент и смысловые цвета;
// наложения наведения, нажатия, выделения и чередования строк — из текста и акцента.
struct Tokens {
  u32 bg, surface1, surface2, surface3, border, borderStrong;
  u32 text, textDim, textMuted;
  u32 accent, accentHover, onAccent;
  u32 success, warning, danger, info;
  u32 track;
};

constexpr SchemeInfo kSchemes[] = {
    {"sapphire", "Сапфир"}, {"emerald", "Изумруд"}, {"crimson", "Багрянец"}, {"amethyst", "Аметист"}, {"obsidian", "Обсидиан"},
};

// Насыщенные, но не светлые: поверхности несут тон схемы (не серые), текст почти белый, акцент — чистое золото
// (у обсидиана — янтарь); смысловые цвета яркие, чтобы читаться на тёмном.
constexpr Tokens kTokens[] = {
    // Сапфир: глубокий синий и золото.
    {0x0a1426, 0x0f1d38, 0x152649, 0x1b3159, 0x22396a, 0x30508c, 0xf4f2ec, 0xb5c4e0, 0x7b8fb8, 0xf5b83d, 0xffcb5c, 0x1a1103,
     0x3ccf82, 0xf6a623, 0xf25a52, 0x4cabff, 0x22365e},
    // Изумруд: глубокий сине-зелёный и золото.
    {0x061b18, 0x0a2723, 0x0f332d, 0x154038, 0x1a4b42, 0x25685b, 0xf2f4ee, 0xb0d2c6, 0x739c8f, 0xf2b33d, 0xffc75a, 0x1a1103,
     0x4fd08f, 0xf6a623, 0xf25a52, 0x4cabff, 0x17433b},
    // Багрянец: густой винный и золото.
    {0x180910, 0x230e18, 0x2e1420, 0x3b1a2a, 0x4a2133, 0x672e48, 0xf7f0ec, 0xdcbcc6, 0xa07c88, 0xf5b83d, 0xffcb5c, 0x1a1103,
     0x45c97f, 0xf6a623, 0xff6b5e, 0x5ab0ff, 0x3d1a2b},
    // Аметист: королевский фиолетовый и золото.
    {0x110b22, 0x18102f, 0x20163e, 0x2a1e4f, 0x33255e, 0x4a3684, 0xf4f1fb, 0xc4b9e2, 0x8b80b2, 0xf5b83d, 0xffcb5c, 0x1a1103,
     0x45c98a, 0xf6a623, 0xf25a6c, 0x5aa9ff, 0x2b1f50},
    // Обсидиан: почти чёрный нейтральный и янтарь — наибольший контраст.
    {0x09090b, 0x111216, 0x18191f, 0x212329, 0x272a31, 0x383c46, 0xf6f6f3, 0xbabec7, 0x7d828f, 0xffb21f, 0xffc44f, 0x1a1103,
     0x35cc7c, 0xffa62b, 0xff5a4f, 0x3ea6ff, 0x25282e},
};
static_assert(std::size(kSchemes) == std::size(kTokens));

int clampScheme(int i) { return i >= 0 && i < int(std::size(kSchemes)) ? i : 0; }

}  // namespace

int schemeCount() { return int(std::size(kSchemes)); }
const SchemeInfo& schemeInfo(int i) { return kSchemes[clampScheme(i)]; }

Theme schemeTheme(int i) {
  const Tokens& k = kTokens[clampScheme(i)];
  Theme t;
  t.dark = true;
  t.bg = Color::hex(k.bg);
  t.surface1 = Color::hex(k.surface1);
  t.surface2 = Color::hex(k.surface2);
  t.surface3 = Color::hex(k.surface3);
  t.border = Color::hex(k.border);
  t.borderStrong = Color::hex(k.borderStrong);
  t.text = Color::hex(k.text);
  t.textDim = Color::hex(k.textDim);
  t.textMuted = Color::hex(k.textMuted);
  t.accent = Color::hex(k.accent);
  t.accentHover = Color::hex(k.accentHover);
  t.onAccent = Color::hex(k.onAccent);
  t.success = Color::hex(k.success);
  t.warning = Color::hex(k.warning);
  t.danger = Color::hex(k.danger);
  t.info = Color::hex(k.info);
  t.track = Color::hex(k.track);
  t.shadow = Color(0, 0, 0, 120);
  t.scrim = Color(u8(t.bg.r / 2), u8(t.bg.g / 2), u8(t.bg.b / 2), 150);
  t.hover = t.text.withA(16);
  t.pressed = t.text.withA(28);
  t.selection = t.accent.withA(82);
  t.stripe = t.text.withA(7);
  return t;
}

const Theme& theme() { return C().th; }

void setScheme(int i) {
  C().scheme = clampScheme(i);
  C().th = schemeTheme(C().scheme);
  C().redraw = true;
}

int scheme() { return C().scheme; }

void setUiScale(float s) {
  if (!std::isfinite(s)) return;
  C().uiScale = clamp(s, 0.9f, 1.5f);
  C().redraw = true;
}

float uiScale() { return C().uiScale; }

namespace in {

float easeOut(float t) {
  t = clamp(t, 0.f, 1.f);
  float u = 1 - t;
  return 1 - u * u * u;
}

const gfx::TextStyle& styleOf(Font f) {
  using gfx::FontFamily;
  using gfx::FontWeight;
  static const gfx::TextStyle styles[] = {
      {FontFamily::UI, FontWeight::Regular, 11, 0.15f, 0},      // Caption
      {FontFamily::UI, FontWeight::Regular, 12, 0, 0},          // Small
      {FontFamily::UI, FontWeight::Regular, 13, 0, 0},          // Body
      {FontFamily::UI, FontWeight::Semibold, 13, 0, 0},         // Strong
      {FontFamily::UI, FontWeight::Semibold, 15, 0, 0},         // Subtitle
      {FontFamily::UI, FontWeight::Semibold, 18, 0, 0},         // Title
      {FontFamily::Display, FontWeight::Bold, 22, 0, 0},        // Heading
      {FontFamily::Display, FontWeight::Regular, 28, 0, 0},     // Display
      {FontFamily::Mono, FontWeight::Regular, 12, 0, 0},        // Mono
      {FontFamily::UI, FontWeight::Semibold, 20, -0.2f, 0},     // Number
  };
  size_t i = size_t(f);
  return styles[i < std::size(styles) ? i : 2];
}

gfx::TextStyle styleWith(Font f, gfx::FontWeight w) {
  gfx::TextStyle s = styleOf(f);
  s.weight = w;
  return s;
}

}  // namespace in

gfx::TextStyle textStyle(Font f) { return styleOf(f); }

float measure(std::string_view text, Font f) { return textWidth(text, styleOf(f)); }

float lineHeight(Font f) {
  static float cache[16] = {};
  size_t i = size_t(f);
  if (i >= 16) return 16;
  if (cache[i] <= 0) {
    float lh = gfx::metrics(styleOf(f)).lineHeight;
    cache[i] = lh > 0 ? lh : styleOf(f).size * 1.3f;
  }
  return cache[i];
}

Color inkColor(Ink k) {
  const Theme& t = C().th;
  switch (k) {
    case Ink::Normal: return t.text;
    case Ink::Dim: return t.textDim;
    case Ink::Muted: return t.textMuted;
    case Ink::Accent: return t.accent;
    case Ink::Success: return t.success;
    case Ink::Warning: return t.warning;
    case Ink::Danger: return t.danger;
    case Ink::Info: return t.info;
    case Ink::OnAccent: return t.onAccent;
  }
  return t.text;
}

Color toneColor(Tone tone) {
  const Theme& t = C().th;
  switch (tone) {
    case Tone::Neutral: return t.textDim;
    case Tone::Accent: return t.accent;
    case Tone::Success: return t.success;
    case Tone::Warning: return t.warning;
    case Tone::Danger: return t.danger;
    case Tone::Info: return t.info;
  }
  return t.textDim;
}

std::string shortcutText(Shortcut s) {
  if (!s) return {};
  u32 mods = s.mods & ~ModPrimary;
  if (s.mods & ModPrimary) mods |= platform::primaryMod();
  return platform::shortcutText(s.key, mods);
}

std::span<const Color> heraldicPalette() {
  // Геральдические эмали и металлы с приглушёнными вариантами.
  static const Color pal[] = {
      Color::hex(0xa4262c), Color::hex(0xc8553d), Color::hex(0xd9a441), Color::hex(0xe8d8a8), Color::hex(0x2e7d4f), Color::hex(0x6b8f3a),
      Color::hex(0x1f4e9c), Color::hex(0x4a86c8), Color::hex(0x5b3a8c), Color::hex(0x8c3a6b), Color::hex(0x7a1f2b), Color::hex(0xb46a2a),
      Color::hex(0x1c1d21), Color::hex(0x4b4f58), Color::hex(0x9aa0a8), Color::hex(0xefece4),
  };
  return pal;
}

}  // namespace rg::ui

namespace rg::ui::in {

// «ход|хода|ходов» — склонение по числу; дробные — вторая форма («2,5 хода»).
std::string unitFor(const char* unit, double v, int digits) {
  if (!unit) return {};
  std::string_view u(unit);
  size_t p1 = u.find('|');
  if (p1 == std::string_view::npos) return std::string(u);
  size_t p2 = u.find('|', p1 + 1);
  std::string one(u.substr(0, p1));
  std::string few(p2 == std::string_view::npos ? u.substr(p1 + 1) : u.substr(p1 + 1, p2 - p1 - 1));
  std::string many(p2 == std::string_view::npos ? few : std::string(u.substr(p2 + 1)));
  double p = std::pow(10.0, std::max(0, std::min(digits, 9)));
  double r = std::round(v * p) / p;
  if (std::fabs(r - std::round(r)) > 1e-9) return few;
  i64 n = i64(std::llround(r));
  return plural(n < 0 ? -n : n, one.c_str(), few.c_str(), many.c_str());
}

}  // namespace rg::ui::in
