// Regnum — настройки: интерфейс (цветокор, масштаб, цвета карты, проводник) — для программы;
// карта, правила и сохранение — настройки мира (World::settings, изменения отменяются Ctrl+Z). Роли (аудит 2026-10-23,
// 1.4, 1.6): вид карты у игрока — свой (app.json, мир не меняется), у администратора — настройки мира; правила,
// автосохранение и пароль администратора меняет только администратор.
#include "app/app_internal.h"
#include "app/shell/role.h"

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

  // Вид карты: у игрока — свой (app.json), у администратора — настройки мира (role::setView).
  void mapSection(App& a) {
    const Settings s = role::viewSettings(a);
    ui::Disabled dis(a.readOnly());
    ui::prop("Прозрачность заливки", "layers");
    double op = std::round(double(s.fillOpacity) * 100);
    if (ui::slider("fill", op, 0, 100, {.step = 5, .unit = "%"})) {
      float v = float(op / 100.0);
      role::setView(a, "Прозрачность заливки", [v](Settings& x) { x.fillOpacity = v; }, "settings.fill");
    }
    a.markUi("settings.fill");
    ui::spacer(4);
    ui::caption("Подписи на карте");
    bool st = s.labelStates, pr = s.labelProvinces, ar = s.labelArmies;
    if (ui::toggle("Государства", st)) role::setView(a, "Подписи государств", [st](Settings& x) { x.labelStates = st; });
    a.markUi("settings.labelStates");
    if (ui::toggle("Провинции", pr)) role::setView(a, "Подписи провинций", [pr](Settings& x) { x.labelProvinces = pr; });
    if (ui::toggle("Войска и флот", ar)) role::setView(a, "Подписи войск", [ar](Settings& x) { x.labelArmies = ar; });
    ui::spacer(4);
    ui::caption("Войска и флот");
    bool hide = !s.showArmies;
    if (ui::toggle("Скрыть войска", hide)) role::setView(a, hide ? "Скрыть войска" : "Показать войска", [hide](Settings& x) { x.showArmies = !hide; });
    a.markUi("settings.hideArmies");
    // Размер значка войска и флота — постоянный на экране при любом масштабе карты (ТЗ «Фиксы», п.3 и 11).
    ui::prop("Размер значков", "army");
    double fs = s.figureSize;
    if (ui::slider("figure", fs, schema::kFigureSizeMin, schema::kFigureSizeMax, {.step = 2})) {
      int v = int(std::lround(fs));
      role::setView(a, "Размер значков войск", [v](Settings& x) { x.figureSize = v; }, "settings.figure");
    }
    a.markUi("settings.figureSize");
  }

  void rulesSection(App& a) {
    const Settings& s = *a.store.world().settings;
    ui::Disabled dis(!a.canEdit());   // правила мира — только администратору редактора
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
    // ТЗ «Доработки №3», п.3: без свободного редактирования флот ставится только у верфи (вкладка «Флот» государства).
    bool freeFleets = s.freeFleets;
    if (ui::toggle("Свободное редактирование флотов", freeFleets))
      a.act("Свободное редактирование флотов", [freeFleets](Tx& tx) { tx.settings().freeFleets = freeFleets; });
    ui::tooltip("Новый флот (Shift+A) — в любом месте моря; без этого — у верфи приморской провинции во вкладке «Флот» государства");
    a.markUi("settings.freeFleets");
    // Пароль администратора (аудит 2026-10-23, 1.4): задать, сменить, снять.
    ui::spacer(4);
    ui::caption("Пароль администратора");
    {
      const bool locked = role::adminLocked(a.store.world());
      ui::HStack hs(30, ui::Align::Left, 8);
      ui::label(locked ? "Задан" : "Не задан", {.ink = locked ? ui::Ink::Success : ui::Ink::Muted, .icon = locked ? "lock" : "unlock"});
      a.markUi("settings.password.state");
      ui::flex();
      if (ui::button(locked ? "Сменить…" : "Задать…", {.icon = "lock", .size = ui::Size::Small})) role::askSetPassword(a);
      a.markUi("settings.password.set");
      if (locked) {
        if (ui::button("Снять", {.variant = ui::Variant::Subtle, .icon = "unlock", .size = ui::Size::Small})) role::askClearPassword(a);
        a.markUi("settings.password.clear");
      }
    }
  }

  void saveSection(App& a) {
    const Settings& s = *a.store.world().settings;
    ui::Disabled dis(!a.canEdit());   // сохранение мира — настройка администратора
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
    else if (world && section >= 2 && !a.admin()) ui::label(role::kAdminOnly, {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "lock"});
    ui::ModalFooter f;
    if (ui::button("Готово", {.variant = ui::Variant::Primary, .isDefault = true})) return false;
    a.markUi("dialog.ok");
    return true;
  }
};

}  // namespace

std::unique_ptr<Dialog> settingsDialog() { return std::make_unique<SettingsDlg>(); }

}  // namespace rg::app::detail
