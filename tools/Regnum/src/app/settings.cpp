// Regnum — настройки: интерфейс (цветокор, масштаб, цвета карты, проводник) — для программы;
// карта, правила и сохранение — настройки мира (World::settings, изменения отменяются Ctrl+Z).
#include "app/app_internal.h"

namespace rg::app::detail {

namespace {

// Образец цветокора: строка и лента интерфейса, море и суша карты, акцент; под ним — название.
bool schemeCard(int i, bool selected, RectF r, bool schemeMap) {
  const ui::Theme t = ui::schemeTheme(i);
  const ui::Theme& cur = ui::theme();
  const map::art::Style ms = map::art::paletteStyle(map::art::Style{}, schemeMap ? schemePalette(i) : map::Palette::Source);
  const ui::SchemeInfo& info = ui::schemeInfo(i);
  ui::WidgetId wid = ui::id(std::string("##scheme.") + info.id);
  ui::Interaction it = ui::interact(wid, r, ui::IfFocusable);
  float hv = ui::animate(wid ^ 0x5c4e, it.hovered ? 1.f : 0.f);
  RectF pr{r.x, r.y, r.w, r.h - 22};
  ui::draw::rect(pr, t.surface1, 8);
  ui::draw::rect(RectF{pr.x + 8, pr.y + 5, 16, 4}, t.accent, 2);
  ui::draw::rect(RectF{pr.x + 28, pr.y + 5, 12, 4}, t.textDim.alpha(0.55f), 2);
  RectF mr{pr.x + 5, pr.y + 13, pr.w - 10, pr.h - 18};
  ui::draw::rect(mr, ms.sea, 5);
  ui::draw::rect(RectF{mr.x + mr.w * 0.12f, mr.y + mr.h * 0.2f, mr.w * 0.4f, mr.h * 0.6f}, ms.land, 8);
  ui::draw::rect(RectF{mr.x + mr.w * 0.6f, mr.y + mr.h * 0.42f, mr.w * 0.26f, mr.h * 0.36f}, ms.land, 6);
  ui::draw::line(mr.x + mr.w * 0.2f, mr.y + mr.h * 0.62f, mr.x + mr.w * 0.44f, mr.y + mr.h * 0.48f, ms.water, 1.5f);
  if (selected) ui::draw::rectStroke(pr.expand(2), cur.accent, 10, 2);
  else ui::draw::rectStroke(pr, Color::mix(cur.border, cur.borderStrong, hv), 8, 1);
  ui::draw::text(info.name, RectF{r.x, pr.bottom() + 4, r.w, 18}, selected ? ui::Font::Strong : ui::Font::Small,
                 selected ? cur.accent : Color::mix(cur.textDim, cur.text, hv), ui::Align::Center);
  if (it.hovered) ui::setCursor(platform::Cursor::Hand);
  return it.clicked;
}

struct SettingsDlg : Dialog {
  int section = 0;
  const char* id() const override { return "settings"; }
  Style style(App&) override { return {"Настройки", "settings", ui::Tone::Accent, 600}; }

  void interfaceSection(App& a) {
    App::Impl& d = a.impl();
    // Цветокор: тон интерфейса и цвета карты вместе.
    ui::caption("Цветокор");
    {
      const int n = ui::schemeCount();
      const float gap = 10, w = std::floor((ui::avail().w - gap * float(n - 1)) / float(n));
      RectF row = ui::next(86);
      for (int i = 0; i < n; i++) {
        ui::IdScope s(i);
        RectF r{row.x + float(i) * (w + gap), row.y, w, row.h};
        if (schemeCard(i, a.ui.scheme == i, r, a.ui.schemeMap)) a.setScheme(i);
        a.markUi(std::string("settings.scheme.") + ui::schemeInfo(i).id, r);
      }
    }
    ui::spacer(4);
    ui::prop("Масштаб интерфейса", "zoom-in");
    static const float scales[] = {0.9f, 1.0f, 1.1f, 1.25f, 1.5f};
    int si = 1;
    for (int i = 0; i < 5; i++)
      if (std::fabs(a.ui.uiScale - scales[i]) < 0.01f) si = i;
    if (ui::segmented("scale", si, {{nullptr, "90 %"}, {nullptr, "100 %"}, {nullptr, "110 %"}, {nullptr, "125 %"}, {nullptr, "150 %"}})) a.setUiScale(scales[si]);
    a.markUi("settings.scale");
    ui::prop("Цвета карты", "map");
    int pal = a.ui.schemeMap ? 1 : 0;
    if (ui::segmented("palette", pal, {{nullptr, "Исходные"}, {nullptr, "Цветокора"}})) a.setSchemeMap(pal == 1);
    ui::tooltip("Исходные — синее море и белая суша исходного изображения; цветокора — море и суша в тон интерфейса");
    a.markUi("settings.palette");
    bool native = platform::dialogsSupported();
    bool own = d.builtinBrowser || !native;
    if (ui::toggle("Встроенный проводник вместо системных окон", own, !native)) {
      d.builtinBrowser = own;
      d.prefsDirty = true;
    }
    a.markUi("settings.browser");
  }

  void mapSection(App& a) {
    const Settings& s = *a.store.world().settings;
    ui::Disabled dis(a.readOnly());
    ui::prop("Прозрачность заливки", "layers");
    double op = std::round(double(s.fillOpacity) * 100);
    if (ui::slider("fill", op, 0, 100, {.step = 5, .unit = "%"})) {
      float v = float(op / 100.0);
      a.act("Прозрачность заливки", [v](Tx& tx) { tx.settings().fillOpacity = v; }, {.coalesce = "settings.fill"});
    }
    a.markUi("settings.fill");
    ui::spacer(4);
    ui::caption("Подписи на карте");
    bool st = s.labelStates, pr = s.labelProvinces, ar = s.labelArmies;
    if (ui::toggle("Государства", st)) a.act("Подписи государств", [st](Tx& tx) { tx.settings().labelStates = st; });
    a.markUi("settings.labelStates");
    if (ui::toggle("Провинции", pr)) a.act("Подписи провинций", [pr](Tx& tx) { tx.settings().labelProvinces = pr; });
    if (ui::toggle("Войска и флот", ar)) a.act("Подписи войск", [ar](Tx& tx) { tx.settings().labelArmies = ar; });
    ui::spacer(4);
    ui::caption("Войска и флот");
    bool hide = !s.showArmies;
    if (ui::toggle("Скрыть войска", hide)) a.act(hide ? "Скрыть войска" : "Показать войска", [hide](Tx& tx) { tx.settings().showArmies = !hide; });
    a.markUi("settings.hideArmies");
    // Размер значка войска и флота — постоянный на экране при любом масштабе карты (ТЗ «Фиксы», п.3 и 11).
    ui::prop("Размер значков", "army");
    double fs = s.figureSize;
    if (ui::slider("figure", fs, schema::kFigureSizeMin, schema::kFigureSizeMax, {.step = 2})) {
      int v = int(std::lround(fs));
      a.act("Размер значков войск", [v](Tx& tx) { tx.settings().figureSize = v; }, {.coalesce = "settings.figure"});
    }
    a.markUi("settings.figureSize");
  }

  void rulesSection(App& a) {
    const Settings& s = *a.store.world().settings;
    ui::Disabled dis(a.readOnly());
    ui::prop("Доход оккупированной провинции", "occupied");
    int oi = int(s.occupiedIncome);
    std::vector<ui::Option> opts;
    for (auto& e : schema::kOccupiedIncome) opts.push_back(ui::Option{e.name, e.icon});
    if (ui::combo("occ", oi, std::span<const ui::Option>(opts))) {
      OccupiedIncome v = OccupiedIncome(clamp(oi, 0, 2));
      a.act("Доход оккупированных провинций", [v](Tx& tx) { tx.settings().occupiedIncome = v; });
    }
    a.markUi("settings.occupied");
    ui::spacer(4);
    bool roll = s.rebellionRoll;
    if (ui::toggle("Бросок восстания в конце хода", roll)) a.act("Бросок восстания", [roll](Tx& tx) { tx.settings().rebellionRoll = roll; });
    ui::tooltip("Каждая провинция восстаёт с вероятностью своего риска восстания; без броска риск только показывается");
    a.markUi("settings.rebellion");
  }

  void saveSection(App& a) {
    const Settings& s = *a.store.world().settings;
    ui::Disabled dis(a.readOnly());
    ui::prop("Автосохранение каждые", "clock");
    int sec = s.autosaveSec;
    if (ui::numberField("autosave", sec, {.min = 10, .max = 3600, .step = 10, .unit = "с", .steppers = true}))
      a.act("Период автосохранения", [sec](Tx& tx) { tx.settings().autosaveSec = sec; }, {.coalesce = "settings.autosave"});
    a.markUi("settings.autosave");
    bool folder = s.autosaveFolder;
    if (ui::toggle("Автосохранение в папку мира", folder)) a.act("Автосохранение в папку", [folder](Tx& tx) { tx.settings().autosaveFolder = folder; });
    ui::tooltip("Копия для восстановления после сбоя сохраняется всегда — в папке данных программы");
    a.markUi("settings.autosaveFolder");
  }

  bool draw(App& a) override {
    bool world = a.ui.screen == Screen::Editor;
    if (!world) section = 0;
    {
      ui::Disabled dis(!world);
      ui::tabs("sec", section, {{"palette", "Интерфейс"}, {"map", "Карта"}, {"scales", "Правила"}, {"save", "Сохранение"}}, {.style = ui::TabStyle::Pill});
    }
    a.markUi("settings.tabs");
    ui::spacer(6);
    switch (section) {
      case 0: interfaceSection(a); break;
      case 1: mapSection(a); break;
      case 2: rulesSection(a); break;
      default: saveSection(a); break;
    }
    if (world && section > 0 && a.readOnly()) ui::label("Открыт прошлый ход — настройки мира только для просмотра", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "lock"});
    ui::ModalFooter f;
    if (ui::button("Готово", {.variant = ui::Variant::Primary, .isDefault = true})) return false;
    a.markUi("dialog.ok");
    return true;
  }
};

}  // namespace

std::unique_ptr<Dialog> settingsDialog() { return std::make_unique<SettingsDlg>(); }

}  // namespace rg::app::detail
