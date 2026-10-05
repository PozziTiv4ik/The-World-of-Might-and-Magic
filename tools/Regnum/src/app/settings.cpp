// Regnum — настройки: интерфейс (тема, масштаб, мини-карта, легенда, проводник) — для программы;
// карта, правила и сохранение — настройки мира (World::settings, изменения отменяются Ctrl+Z).
#include "app/app_internal.h"

namespace rg::app::detail {

namespace {

struct SettingsDlg : Dialog {
  int section = 0;
  const char* id() const override { return "settings"; }
  Style style(App&) override { return {"Настройки", "settings", ui::Tone::Accent, 600}; }

  void interfaceSection(App& a) {
    App::Impl& d = a.impl();
    ui::prop("Тема", a.ui.darkTheme ? "moon" : "sun");
    int theme = a.ui.darkTheme ? 0 : 1;
    if (ui::segmented("theme", theme, {{"moon", "Тёмная"}, {"sun", "Светлая"}})) a.setTheme(theme == 0);
    a.markUi("settings.theme");
    ui::prop("Масштаб интерфейса", "zoom-in");
    static const float scales[] = {0.9f, 1.0f, 1.1f, 1.25f, 1.5f};
    int si = 1;
    for (int i = 0; i < 5; i++)
      if (std::fabs(a.ui.uiScale - scales[i]) < 0.01f) si = i;
    if (ui::segmented("scale", si, {{nullptr, "90 %"}, {nullptr, "100 %"}, {nullptr, "110 %"}, {nullptr, "125 %"}, {nullptr, "150 %"}})) a.setUiScale(scales[si]);
    a.markUi("settings.scale");
    ui::spacer(4);
    bool mm = a.ui.showMinimap, lg = a.ui.showLegend;
    if (ui::toggle("Мини-карта", mm)) {
      a.ui.showMinimap = mm;
      d.prefsDirty = true;
    }
    if (ui::toggle("Легенда режима карты", lg)) {
      a.ui.showLegend = lg;
      d.prefsDirty = true;
    }
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
