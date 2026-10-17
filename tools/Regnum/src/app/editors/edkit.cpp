// Regnum — общие элементы редакторов (edkit.h): выбор значка из сетки, плитка значка, строка списка сущности, переход
// к сущности на карте, фишки эффектов модификатора.
#include "app/editors/edkit.h"

#include "app/app_internal.h"
#include "app/widgets.h"
#include "gfx/icons.h"

namespace rg::app::edkit {

namespace {

struct IconChoice {
  const char* name;
  const char* title;
};
const IconChoice kIcons[] = {
    {"sparkles", "Чудо"},        {"star", "Звезда"},          {"crown", "Корона"},        {"scroll", "Указ"},
    {"book", "Знание"},          {"tech", "Наука"},           {"research", "Исследование"}, {"wand", "Магия"},
    {"bolt", "Молния"},          {"sun", "Солнце"},           {"moon", "Луна"},           {"eye", "Око"},
    {"coins", "Монеты"},         {"treasury", "Казна"},       {"income", "Доход"},        {"expense", "Расход"},
    {"trade", "Торговля"},       {"trade-value", "Торговая ценность"}, {"route", "Тракт"}, {"scales", "Весы"},
    {"handshake", "Сделка"},     {"diplomacy", "Дипломатия"}, {"alliance", "Союз"},       {"war", "Война"},
    {"population", "Население"}, {"contentment", "Довольство"}, {"discontent", "Недовольство"}, {"rebellion", "Мятеж"},
    {"heart", "Здоровье"},       {"religion", "Вера"},        {"culture", "Культура"},    {"race", "Народ"},
    {"grain", "Зерно"},          {"wood", "Древесина"},       {"stone", "Камень"},        {"iron", "Железо"},
    {"gem", "Самоцвет"},         {"pickaxe", "Добыча"},       {"resource", "Ресурс"},     {"magnet", "Притяжение"},
    {"hammer", "Ремесло"},       {"factory", "Мастерская"},   {"building", "Постройка"},  {"house", "Жильё"},
    {"castle", "Замок"},         {"tower", "Башня"},          {"slots", "Слоты"},         {"shield", "Защита"},
    {"sword", "Меч"},            {"swords", "Битва"},         {"army", "Войско"},         {"bow", "Стрелки"},
    {"horse", "Конница"},        {"fleet", "Флот"},           {"anchor", "Гавань"},       {"skull", "Гибель"},
    {"mountain", "Горы"},        {"sea", "Море"},             {"island", "Остров"},       {"globe", "Мир"},
    {"compass", "Путь"},         {"flag", "Флаг"},            {"banner", "Знамя"},        {"hourglass", "Время"},
    {"dice", "Удача"},           {"lock", "Запрет"},          {"chronicle", "Летопись"},  {"capital", "Столица"},
    {"flame", "Пламя"},          {"hero", "Герой"},           {"council", "Совет"},       {"warning", "Беда"},
};

}  // namespace

// Генерация за ход подписями: «+10 эссенция смерти», «+25 археологические сокровища» (у шаблона встроенного — по
// умолчанию).
std::vector<std::string> genTexts(const World& w, const Modifier& m) {
  std::vector<std::string> out;
  for (auto& [e, v] : rules::modEssGen(w, m))
    if (const CatalogItem* c = w.essence(e); c && v > 0) out.push_back("+" + fmtNum(v, 3) + " " + utf8::lower(c->name));
  for (auto& [r, v] : rules::modResGen(w, m))
    if (const CatalogItem* c = w.resource(r); c && v > 0) out.push_back("+" + fmtNum(v, 3) + (r == kGold ? " тыс. золота" : " " + utf8::lower(c->name)));
  return out;
}

const char* iconTitle(std::string_view icon) {
  for (auto& c : kIcons)
    if (icon == c.name) return c.title;
  return icon.empty() ? "Без значка" : "Свой значок";
}

// Кнопка-значок с выбором из сетки во всплывающем окне. true — выбран другой значок.
bool iconPicker(std::string_view id, std::string& icon, bool allowNone, bool disabled, std::string_view tip) {
  ui::IdScope scope(id);
  bool none = icon.empty() || !gfx::hasIcon(icon);
  std::string t = std::string(tip) + ": " + iconTitle(none ? std::string_view() : std::string_view(icon));
  if (ui::iconButton(none ? "image" : icon.c_str(), t, {.variant = ui::Variant::Secondary, .disabled = disabled})) ui::openPopup("grid");
  bool changed = false;
  if (ui::beginPopup("grid", {.side = ui::Side::Below, .width = 8 * 34 + 7 * 4 + 16})) {
    ui::caption(tip);
    {
      ui::Row g({ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34)}, 34, 4);
      if (allowNone && ui::iconButton("close", "Без значка", {.toggled = none})) {
        changed = !icon.empty();
        icon.clear();
        ui::closePopup();
      }
      for (auto& c : kIcons) {
        if (!gfx::hasIcon(c.name)) continue;
        if (ui::iconButton(c.name, c.title, {.toggled = icon == c.name})) {
          changed = icon != c.name;
          icon = c.name;
          ui::closePopup();
        }
      }
    }
    ui::endPopup();
  }
  return changed;
}

// Плитка значка цвета сущности (крупный значок в шапке карточки).
void iconTile(const std::string& icon, Color tint, float size, const char* fallback) {
  RectF r = ui::next(size, size);
  float rad = size >= 48 ? 14 : 8;
  ui::draw::rect(r, tint.alpha(0.2f), rad);
  ui::draw::rectStroke(r, tint.alpha(0.5f), rad, 1);
  bool ok = !icon.empty() && gfx::hasIcon(icon);
  ui::draw::icon(ok ? std::string_view(icon) : std::string_view(fallback), r.inset(size * 0.24f), tint);
}

// Строка списка сущности (высота 44): плитка значка цвета сущности, название, подпись, число справа,
// предупреждение. Основа — ui::listItem (наведение, выделение, фокус, подсказка, lastItem для меню).
bool entityRow(std::string_view title, std::string_view subtitle, const char* icon, Color tint, std::string_view hint, bool selected,
               bool warn, std::string_view tip) {
  const ui::Theme& th = ui::theme();
  RectF r = ui::next(0, 44);
  ui::at(r);
  bool clicked = ui::listItem("##row", {.subtitle = " ", .selected = selected, .tooltip = tip});
  RectF tile{r.x + 10, r.cy() - 15, 30, 30};
  ui::draw::rect(tile, tint.alpha(0.2f), 8);
  ui::draw::icon(icon && gfx::hasIcon(icon) ? icon : "sparkles", tile.inset(7), tint);
  float x = tile.right() + 12;
  float right = r.right() - 10;
  if (warn) {
    RectF wr{right - 16, r.cy() - 8, 16, 16};
    ui::draw::icon("warning", wr, th.warning);
    right = wr.x - 8;
  }
  if (!hint.empty()) {
    float bw = std::max(22.f, ui::measure(hint, ui::Font::Caption) + 12);
    RectF br{right - bw, r.cy() - 9, bw, 18};
    ui::draw::rect(br, selected ? th.accent.alpha(0.22f) : th.surface3, 9);
    ui::draw::text(hint, br, ui::Font::Caption, selected ? th.accent : th.textDim, ui::Align::Center);
    right = br.x - 8;
  }
  float lh = ui::lineHeight(ui::Font::Body), sh = ui::lineHeight(ui::Font::Small);
  float y0 = std::round(r.cy() - (lh + sh) * 0.5f);
  ui::draw::text(title, RectF{x, y0, right - x, lh}, selected ? ui::Font::Strong : ui::Font::Body, th.text);
  ui::draw::text(subtitle, RectF{x, y0 + lh, right - x, sh}, ui::Font::Small, th.textMuted);
  return clicked;
}

// Перейти к сущности на карте: закрыть окно и выделить (после кадра).
void goTo(App& a, Selection s) {
  detail::later(a, [s](App& x) {
    x.closeEditor();
    x.select(s, true);
  });
}

ListDetail listDetail(RectF R) {
  const float listW = std::round(clamp(R.w * 0.27f, 280.f, 380.f));
  ListDetail d{RectF{R.x, R.y, listW, R.h}, RectF{R.x + listW + 24, R.y, R.w - listW - 24, R.h}};
  ui::draw::line(d.list.right() + 12, R.y, d.list.right() + 12, R.bottom(), ui::theme().border, 1);
  return d;
}

void effectChips(const Modifier& m) {
  ui::IdScope s(i64(m.id) + 0x41000000LL);
  bool any = false;
  {
    ui::ChipFlow chipFlow;
    for (int f = 0; f < kFxCount; f++) {
      if (!m.has(Fx(f))) continue;
      double v = m.fx[size_t(f)];
      if (v == 0) continue;
      any = true;
      ui::IdScope s2{f};
      ui::ChipOpt co;
      co.icon = schema::effect(Fx(f)).icon;
      co.tone = w::effectGood(Fx(f), v) ? ui::Tone::Success : ui::Tone::Danger;
      ui::flowChip(w::effectText(Fx(f), v), co);
    }
    // Генерация за ход (эссенции и ресурсы).
    const std::vector<std::string> gen = genTexts(app().world(), m);
    for (size_t i = 0; i < gen.size(); i++) {
      any = true;
      ui::IdScope s2{i64(i) + 0x100};
      ui::ChipOpt co;
      co.icon = "repeat";
      co.tone = ui::Tone::Success;
      co.tooltip = "Каждый ход";
      ui::flowChip(gen[i] + " за ход", co);
    }
  }
  if (!any) ui::label("Без эффектов", {.ink = ui::Ink::Muted});
}

}  // namespace rg::app::edkit
