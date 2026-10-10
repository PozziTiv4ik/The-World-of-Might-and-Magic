// Regnum — вкладка «Обзор» государства и гильдии (ТЗ 1.b.i, 1.b.iv, 1.d.ii, 1.d.vi): сводка, название, цвет, флаг,
// основная культура; у государства — форма правления, религия, вид государства и «Основное игровое государство»
// (ТЗ «Виды государств», п.1; «Фиксы», п.16), у гильдии устройства и религии нет (ТЗ «Фиксы», п.14); «Добавить…»
// создаёт запись справочника и сразу назначает её. Правитель (только доступные герои своей фракции) и титул, столица
// (государство), сюзерен и восстание вассала (ТЗ «Вассалитет»), государство расположения (гильдия; у государственной
// закреплено), заметки и связь с каноном (только чтение заметок при связи).
#include "app/canon.h"
#include "app/flows.h"
#include "app/panels/faction_common.h"

namespace rg::app {
namespace {

using namespace fac;

// Перейти на вкладку другого раздела (войска, флот), если она зарегистрирована.
void goTab(App& a, const char* id) {
  for (const TabDef& t : tabs())
    if (t.type == SelType::Faction && std::string_view(t.id) == id) {
      showTab(a, id);
      return;
    }
}

void summary(App& a, const Faction& f, const rules::FactionCalc* fc) {
  ui::Row r({ui::fr(1), ui::fr(1)}, 64, 10);
  if (f.isState()) {
    ui::stat(fmtInt(fc ? i64(fc->provinces.size()) : 0), "Провинции", {.icon = "province", .tone = ui::Tone::Info, .tooltip = "Владения государства — открыть список"});
    if (ui::lastItem().clicked) showTab(a, kTabProvinces);
    ui::stat(fmtShort(double(fc ? fc->population : 0)), "Население", {.icon = "population", .tone = ui::Tone::Accent});
  } else {
    ui::stat(fmtInt(fc ? i64(fc->provinces.size()) : 0), "Штабы", {.icon = "hq", .tone = ui::Tone::Info, .tooltip = "Провинции со штабами гильдии — открыть список"});
    if (ui::lastItem().clicked) showTab(a, kTabHqs);
    ui::stat(fmtGoldSigned(fc ? fc->incGuilds : 0), "Доход штабов", {.icon = "income", .tone = ui::Tone::Success});
  }
  ui::stat(fmtShort(double(fc ? fc->armyTotal : 0)), "Войска", {.icon = "army", .tone = ui::Tone::Danger,
                                                                  .tooltip = "Численность войск: в поле и в резерве"});
  if (ui::lastItem().clicked) goTab(a, "faction.army");
  ui::stat(fmtShort(double(fc ? fc->fleetTotal : 0)), "Флот", {.icon = "fleet", .tone = ui::Tone::Info,
                                                                 .tooltip = "Численность флота: военные и торговые суда"});
  if (ui::lastItem().clicked) goTab(a, "faction.fleet");
}

// Вид государства: живых, нежити, демонов (значок и подпись из schema).
void stateKindField(App& a, const Faction& f, bool ro) {
  const Id id = f.id;
  std::vector<ui::Option> opts;
  for (int k = 0; k < int(StateKind::Count); k++) opts.push_back(ui::Option{schema::kStateKinds[k].name, schema::kStateKinds[k].icon});
  int idx = int(f.stateKind);
  ui::ComboOpt co;
  co.disabled = ro;
  co.icon = "crown";
  co.tooltip = "Природа новых героев и раса новых отрядов";
  if (ui::combo("kind", idx, std::span<const ui::Option>(opts), co) && idx >= 0 && idx < int(StateKind::Count) && StateKind(idx) != f.stateKind) {
    StateKind k = StateKind(idx);
    a.act("Вид государства", [&](Tx& tx) { rules::setStateKind(tx, id, k); });
  }
}

// Сюзерен (ТЗ «Вассалитет»): «Вассал «X»», выбор сюзерена, восстание при отношениях −50 и ниже.
void suzerainSection(App& a, const World& w, const Faction& f, bool ro) {
  const Id id = f.id;
  ui::Section s("Сюзерен", "banner", {.defaultOpen = true});
  a.markUi("overview.suzerain.section");
  if (!s) return;
  if (const Faction* sz = w.faction(f.suzerain)) {
    if (ui::chip("Вассал «" + displayName(*sz) + "»", {.icon = "banner", .color = sz->color, .clickable = true, .tooltip = "Сюзерен — открыть"}) ==
        ui::ChipAction::Click)
      a.select(SelType::Faction, sz->id);
    a.markUi("overview.suzerain.chip");
  }
  ui::prop("Сюзерен", "banner");
  Id cur = f.suzerain;
  if (w::factionPicker("suzerain", cur, w::FactionFilter::States, "Нет", id, ro))
    a.act(cur ? "Сюзерен" : "Снять вассалитет", [&](Tx& tx) { rules::setSuzerain(tx, id, cur); });
  a.markUi("overview.suzerain");
  if (!f.suzerain) return;
  std::string why;
  const bool can = rules::canVassalRebel(w, id, &why);
  if (ui::button("Восстать", {.variant = ui::Variant::Danger, .icon = "rebellion", .fill = true, .disabled = ro || !can,
                              .tooltip = can ? std::string_view("Выйти из вассалитета — война с сюзереном") : std::string_view(why)})) {
    const Id suz = f.suzerain;
    a.confirm("Восстать против «" + w.factionName(suz) + "»?", "«" + displayName(f) + "» выходит из вассалитета и вступает в войну с сюзереном.",
              "Восстать", true, [id, suz](App& x) {
                if (x.act("Восстание вассала", [&](Tx& tx) { rules::vassalRebels(tx, id); })) flow::afterWarDeclared(x, id, suz);
              });
  }
  a.markUi("overview.rebel");
}

void drawOverview(App& a, Id id) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(id);
  if (!f) return;
  const bool ro = a.readOnly();
  auto calc = rules::calc(w);
  const rules::FactionCalc* fc = calc->faction(id);
  const bool state = f->isState();

  summary(a, *f, fc);
  ui::spacer(2);

  if (ui::Section s("Основное", "info"); s) {
    ui::Disabled d(ro);
    ui::prop("Название", "edit");
    std::string name = f->name;
    if (ui::textField("name", name, {.placeholder = "Название", .maxLength = 80})) {
      std::string n = trim(name);
      if (n.empty()) a.toast("Название не может быть пустым", ToastKind::Warning, "edit");
      else if (n != f->name) a.act(state ? "Переименовать государство" : "Переименовать гильдию", [&](Tx& tx) { tx.faction(id).name = n; });
    }
    a.markUi("overview.name");
    ui::prop("Цвет", "palette");
    Color c = f->color;
    if (ui::colorButton("color", c, {.tooltip = "Цвет на карте"}))
      a.act("Цвет фракции", [&](Tx& tx) { tx.faction(id).color = c; }, {.coalesce = "faction.color:" + std::to_string(id)});
    ui::prop("Флаг", "flag");
    {
      ui::HStack hs(30, ui::Align::Left, 10);
      ui::flag(f->flag, 39, 26, 3, "Флаг");
      if (ui::lastItem().clicked) openFlagEditor(a, id);
      if (ui::button("Изменить…", {.icon = "palette", .size = ui::Size::Small})) openFlagEditor(a, id);
      a.markUi("overview.flag");
    }
    // «Добавить…» в списке создаёт запись справочника и сразу назначает её фракции.
    ui::prop("Культура", "culture");
    catalogField(a, "culture", rules::CatalogList::Cultures, f->culture, "Не указана", "Культура",
                 [id](Tx& tx, Id v) { tx.faction(id).culture = v; });
    a.markUi("overview.culture");
    if (state) {   // у гильдий устройства и религии нет (ТЗ «Фиксы», п.14)
      ui::prop("Форма правления", "crown");
      catalogField(a, "gov", rules::CatalogList::Governments, f->government, "Не указана", "Форма правления",
                   [id](Tx& tx, Id v) { tx.faction(id).government = v; });
      a.markUi("overview.gov");
      ui::prop("Религия", "religion");
      catalogField(a, "religion", rules::CatalogList::Religions, f->religion, "Не указана", "Религия",
                   [id](Tx& tx, Id v) { tx.faction(id).religion = v; });
      a.markUi("overview.religion");
      ui::prop("Вид государства", schema::stateKind(f->stateKind).icon);
      stateKindField(a, *f, ro);
      a.markUi("overview.kind");
      bool main = f->mainState;
      if (ui::checkbox("Основное игровое государство", main, ro))
        a.act(main ? "Основное игровое государство" : "Не основное государство", [&](Tx& tx) { rules::setMainState(tx, id, main); });
      a.markUi("overview.main");
    }
  }

  if (ui::Section s(state ? "Правитель" : "Глава", "ruler"); s) {
    ui::Disabled d(ro);
    ui::prop(state ? "Правитель" : "Глава", "character");
    Id r = f->ruler;
    if (w::characterPicker("ruler", r, id, "Не назначен")) a.act(state ? "Правитель" : "Глава гильдии", [&](Tx& tx) { rules::setRuler(tx, id, r); });
    a.markUi("overview.ruler");
    ui::prop("Титул", "crown");
    std::string title = f->rulerTitle;
    if (ui::textField("title", title, {.placeholder = state ? "Король, императрица…" : "Магистр, глава…", .maxLength = 60}))
      a.act("Титул правителя", [&](Tx& tx) { tx.faction(id).rulerTitle = trim(title); });
    a.markUi("overview.title");
  }

  if (state) {
    if (ui::Section s("Столица", "capital"); s) {
      ui::Disabled d(ro);
      ui::prop("Провинция", "capital");
      Id cap = f->capital;
      if (w::provincePicker("capital", cap, id, "Не выбрана")) a.act("Столица", [&](Tx& tx) { rules::setCapital(tx, id, cap); });
      a.markUi("overview.capital");
    }
    suzerainSection(a, w, *f, ro);
  } else {
    if (ui::Section s("Расположение", "map-pin"); s) {
      ui::prop("Государство", "crown");
      Id home = f->homeState;
      if (w::factionPicker("home", home, w::FactionFilter::States, "Без государства", 0, ro || f->stateGuild))
        a.act("Государство гильдии", [&](Tx& tx) { rules::setHomeState(tx, id, home); });
      a.markUi("overview.home");
      ui::prop("Государственная", "lock");
      bool sg = f->stateGuild;
      ui::checkbox(sg ? "Да, закреплена" : "Нет", sg, true);
      ui::tooltip(sg ? "Государственная гильдия не меняет государство расположения" : "Гильдия может сменить государство расположения");
      a.markUi("overview.stateguild");
    }
  }

  if (ui::Section s("Заметки", "note", {.defaultOpen = !f->notes.empty() || !f->entity.empty()}); s) canon::notesField(a, SelType::Faction, id);
  if (ui::Section s("Канон", "book", {.defaultOpen = !f->entity.empty()}); s) canon::section(a, SelType::Faction, id);
}

TabReg tab({kTabOverview, "info", "Обзор", 10, SelType::Faction, nullptr, drawOverview});

}  // namespace
}  // namespace rg::app
