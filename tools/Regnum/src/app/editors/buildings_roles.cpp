// Regnum — дерево построек: особые возможности постройки в панели свойств. Значки-переключатели всех возможностей
// (rules::setBuildingRole, rules::setBuildingFlag; выключение снимает их данные — Ctrl+Z возвращает), виды государства,
// которым доступна постройка, и правка включённых возможностей: рецепты преобразования (несколько: название, до трёх
// ресурсов и трёх эссенций на входе, до трёх ресурсов на выходе, срок цикла), особые отряды, группы добычи и исключения,
// «Добыча не обязательна» (ферма).
// Числа по уровням (иссушение, снижение содержания, добыча) — в карточках уровней (buildings_levels.cpp).
#include "app/editors/buildings_internal.h"

namespace rg::app::bldtree {

namespace {

// Возможность постройки: роль (rules::BuildingRole) или флаг (rules::BuildingFlag).
struct Feature {
  bool role;
  int code;
  const char* icon;
  const char* name;
  const char* tip;
  const char* mark;
};

using R = rules::BuildingRole;
using F = rules::BuildingFlag;

const Feature kFeatures[] = {
    {true, int(R::Convert), "convert", "Преобразование ресурсов",
     "Рецепты: до трёх ресурсов и трёх эссенций на входе — до трёх ресурсов на выходе за цикл; в провинции выбирается рецепт и множитель",
     "bt.role.convert"},
    {true, int(R::Essence), "essence", "Генерация эссенции", "Эссенция за ход — у каждого уровня", "bt.role.essence"},
    {true, int(R::Drain), "population", "Иссушение населения", "Жители выбранной расы провинции каждый ход превращаются в эссенцию — по уровням",
     "bt.role.drain"},
    {true, int(R::Extract), "pickaxe", "Добыча ресурса провинции",
     "Строится при ресурсе провинции из выбранных групп (необязательная добыча — при любом); уровни увеличивают его добычу", "bt.role.extract"},
    {true, int(R::UpkeepCut), "army", "Снижение содержания армии",
     "Содержание войск государства меньше на % уровня; сумма по государству — не больше 50 %", "bt.role.upkeep"},
    {false, int(F::Market), "scales", "Рынок", "«Продать реликвии» из хранилищ реликвий государства за золото", "bt.flag.market"},
    {true, int(R::Special), "special-unit", "Доступ к особым отрядам", "Пока постройка достроена, государство может нанимать особые отряды",
     "bt.role.special"},
    {false, int(F::RelicStore), "relic", "Хранилище реликвий", "В постройку можно положить реликвии государства и его героев", "bt.flag.relics"},
    {false, int(F::Healing), "heal", "Здание целительства", "«Вылечить провинцию» от чумы: 500 любой эссенции или 500 лечебных трав",
     "bt.flag.healing"},
    {false, int(F::Plague), "plague", "Здание чумы", "Чума в провинции даёт прирост населения +2,5 %; «Заразить чумой» за 2500 эссенции чумы",
     "bt.flag.plague"},
    {false, int(F::Shipyard), "shipyard", "Верфь", "Уровни открывают найм типов кораблей", "bt.flag.shipyard"},
    {false, int(F::Mercenary), "mercenary", "Гильдия наёмников", "Каждая такая постройка поднимает лимит наёмников государства", "bt.flag.merc"},
    {false, int(F::Coastal), "anchor", "Только в приморской провинции", "Строится только в провинции, граничащей с морем", "bt.flag.coastal"},
};

bool featureOn(const BN& b, const Feature& f) {
  if (f.role) {
    switch (R(f.code)) {
      case R::Convert: return b.convert;
      case R::Essence: return b.essenceGen;
      case R::Special: return b.specialAccess;
      case R::Drain: return b.drain;
      case R::UpkeepCut: return b.upkeepCut;
      case R::Extract: return b.extract;
    }
    return false;
  }
  switch (F(f.code)) {
    case F::RelicStore: return b.relicStore;
    case F::Healing: return b.healing;
    case F::Plague: return b.plague;
    case F::Shipyard: return b.shipyard;
    case F::Mercenary: return b.mercenary;
    case F::Coastal: return b.coastal;
    case F::Market: return b.market;
  }
  return false;
}

Color featureColor(const Feature& f) {
  const ui::Theme& th = ui::theme();
  if (f.role) return roleColor(R(f.code));
  switch (F(f.code)) {
    case F::RelicStore: return Color::hex(kRelicInk);
    case F::Healing: return th.success;
    case F::Plague: return Color::hex(kPlagueInk);
    case F::Shipyard: return th.info;
    case F::Mercenary: return th.accent;
    case F::Coastal: return th.textDim;
    case F::Market: return th.accent;
  }
  return th.textDim;
}

void setFeature(App& a, Id bid, const Feature& f, bool on) {
  a.act(std::string(on ? "Включить: " : "Выключить: ") + utf8::lower(f.name), [&](Tx& tx) {
    if (f.role) rules::setBuildingRole(tx, bid, R(f.code), on);
    else rules::setBuildingFlag(tx, bid, F(f.code), on);
  });
}

// ---------------------------------------------------------------- рецепты
// Раскрытый рецепт (правка); остальные — строкой «вход → выход».
struct RecipeUi {
  Id building = 0;
  int open = -1;
  size_t count = 0;
};

bool listed(const std::vector<ResAmount>& v, Id res) {
  return std::any_of(v.begin(), v.end(), [&](const ResAmount& x) { return x.res == res; });
}
// Следующий ресурс справочника, которого ещё нет на входе и выходе (не золото).
Id nextInput(const World& w, const Recipe& rc) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.id != kGold && !listed(rc.out, c.id) && !listed(rc.in, c.id)) return c.id;
  return 0;
}
Id nextOutput(const World& w, const Recipe& rc) {
  for (const CatalogItem& c : w.catalogs->resources)
    if (c.id != kGold && !listed(rc.out, c.id) && !listed(rc.in, c.id)) return c.id;
  return 0;
}
Id nextEssence(const World& w, const Recipe& rc) {
  for (const CatalogItem& c : w.catalogs->essences)
    if (std::none_of(rc.essIn.begin(), rc.essIn.end(), [&](const ResAmount& x) { return x.res == c.id; })) return c.id;
  return 0;
}

// Правка одного рецепта (ri — индекс): название, входы, выход, срок. Проверки — rules::setRecipe.
void recipeEdit(App& a, const BN& b, int ri, bool ro, RecipeUi& st) {
  const World& w = a.world();
  const Recipe rc = b.recipes[size_t(ri)];
  const Id bid = b.id;
  const std::string m = "bt.recipe." + std::to_string(ri);
  auto set = [&](std::string_view label, const Recipe& nr, std::string coalesce = {}) {
    a.act(label, [&](Tx& tx) { rules::setRecipe(tx, bid, ri, nr); }, {.coalesce = std::move(coalesce)});
  };
  const std::string co = std::to_string(bid) + ":" + std::to_string(ri);
  ui::Card card({.pad = 10, .tone = ui::Tone::Info});
  {
    ui::Row row({ui::fr(1), ui::px(30), ui::px(30)}, 30, 6);
    std::string name = rc.name;
    const std::string auto_ = rules::recipeTitle(w, Recipe{{}, {}, {}, rc.out, 1});   // по первому ресурсу на выходе
    if (ui::textField("name", name, {.placeholder = auto_.empty() ? std::string_view("Название рецепта") : std::string_view(auto_), .maxLength = 60,
                                     .disabled = ro}) &&
        trim(name) != rc.name) {
      Recipe nr = rc;
      nr.name = trim(name);
      set("Название рецепта", nr);
    }
    a.markUi(m + ".name");
    if (ui::iconButton("trash", "Удалить рецепт", {.disabled = ro, .tone = ui::Tone::Danger})) {
      // Провинции с этим рецептом переходят на первый (цикл прерывается) — как и прочие удаления, с подтверждением.
      i64 used = 0;
      w.provinces.each([&](const Province& p) {
        for (const ProvBuilding& pb : p.buildings)
          if (pb.building == bid && pb.recipe == ri) used++;
      });
      const std::string title = rules::recipeTitle(w, rc);
      std::string text = "Рецепт «" + (title.empty() ? std::string("без названия") : title) + "» будет удалён.";
      if (used)
        text += " " + fmtInt(used) + " " + plural(used, "провинция перейдёт", "провинции перейдут", "провинций перейдут") +
                " на первый рецепт, их циклы прервутся.";
      a.confirmAct("Удалить рецепт?", text, "Удалить", "Удалить рецепт", [bid, ri](Tx& tx) { rules::removeRecipe(tx, bid, ri); });
    }
    a.markUi(m + ".remove");
    if (ui::iconButton("chevron-up", "Свернуть")) st.open = -1;
    a.markUi(m + ".collapse");
  }
  ui::caption("На входе");
  for (size_t i = 0; i < rc.in.size(); i++) {
    ui::IdScope s{i64(rc.in[i].res) * 4 + i64(i)};
    Id res = rc.in[i].res;
    if (w::resourceByGroup("res", res, ro) && res && res != rc.in[i].res) {
      Recipe nr = rc;
      nr.in[i].res = res;
      set("Ресурс на входе", nr);
    }
    a.markUi(m + ".in." + std::to_string(i));
    ui::Row row({ui::fr(1), ui::px(30)}, 30, 6);
    double v = rc.in[i].amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .label = "×", .disabled = ro, .tooltip = "Количество на входе за цикл"})) {
      Recipe nr = rc;
      nr.in[i].amount = v;
      set("Количество на входе", nr, "bt-rin:" + co + ":" + std::to_string(i));
    }
    a.markUi(m + ".inamount." + std::to_string(i));
    if (ui::iconButton("close", "Убрать ресурс на входе", {.disabled = ro})) {
      Recipe nr = rc;
      nr.in.erase(nr.in.begin() + long(i));
      set("Убрать ресурс на входе", nr);
    }
    a.markUi(m + ".inremove." + std::to_string(i));
  }
  for (size_t i = 0; i < rc.essIn.size(); i++) {
    ui::IdScope s{i64(rc.essIn[i].res) * 4 + i64(i) + 1000003};
    ui::Row row({ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    Id e = rc.essIn[i].res;
    if (w::essencePicker("ess", e, {}, ro) && e && e != rc.essIn[i].res) {
      Recipe nr = rc;
      nr.essIn[i].res = e;
      set("Эссенция на входе", nr);
    }
    a.markUi(m + ".ess." + std::to_string(i));
    double v = rc.essIn[i].amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .disabled = ro, .tooltip = "Эссенции на входе за цикл"})) {
      Recipe nr = rc;
      nr.essIn[i].amount = v;
      set("Эссенция на входе", nr, "bt-ress:" + co + ":" + std::to_string(i));
    }
    a.markUi(m + ".essamount." + std::to_string(i));
    if (ui::iconButton("close", "Убрать эссенцию на входе", {.disabled = ro})) {
      Recipe nr = rc;
      nr.essIn.erase(nr.essIn.begin() + long(i));
      set("Убрать эссенцию на входе", nr);
    }
    a.markUi(m + ".essremove." + std::to_string(i));
  }
  if (!ro && (rc.in.size() < 3 || rc.essIn.size() < 3)) {
    ui::HStack hs(28, ui::Align::Left, 6);
    if (rc.in.size() < 3) {
      const Id next = nextInput(w, rc);
      if (ui::button("Ресурс##in", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small, .disabled = !next,
                                .tooltip = "До трёх разных ресурсов на входе"})) {
        Recipe nr = rc;
        nr.in.push_back(ResAmount{next, 1});
        set("Ресурс на входе", nr);
      }
      a.markUi(m + ".addin");
    }
    if (rc.essIn.size() < 3) {
      const Id next = nextEssence(w, rc);
      if (ui::button("Эссенция", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small, .disabled = !next,
                                  .tooltip = "До трёх разных эссенций на входе"})) {
        Recipe nr = rc;
        nr.essIn.push_back(ResAmount{next, 1});
        set("Эссенция на входе", nr);
      }
      a.markUi(m + ".addess");
    }
  }
  // На выходе — до трёх разных ресурсов (первый — отметки «.out» и «.outamount», следующие — с номером). Без выхода —
  // сразу выбор первого ресурса.
  ui::caption("На выходе");
  if (rc.out.empty()) {
    ui::IdScope os("out-empty");
    Id res = 0;
    if (w::resourceByGroup("res", res, ro) && res) {
      Recipe nr = rc;
      nr.out = {ResAmount{res, 1}};
      set("Ресурс на выходе", nr);
    }
    a.markUi(m + ".out");
  }
  for (size_t i = 0; i < rc.out.size(); i++) {
    ui::IdScope os{i64(rc.out[i].res) * 4 + i64(i) + 2000003};
    const std::string k = i == 0 ? std::string() : "." + std::to_string(i);
    Id res = rc.out[i].res;
    if (w::resourceByGroup("res", res, ro) && res && res != rc.out[i].res) {
      Recipe nr = rc;
      nr.out[i].res = res;
      if (!(nr.out[i].amount > 0)) nr.out[i].amount = 1;
      set("Ресурс на выходе", nr);
    }
    a.markUi(m + ".out" + k);
    ui::Row row({ui::fr(1), ui::px(30)}, 30, 6);
    double v = rc.out[i].amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .label = "×", .disabled = ro || !rc.out[i].res,
                                      .tooltip = "Количество на выходе за цикл"})) {
      Recipe nr = rc;
      nr.out[i].amount = v;
      set("Количество на выходе", nr, "bt-rout:" + co + ":" + std::to_string(i));
    }
    a.markUi(m + ".outamount" + k);
    if (ui::iconButton("close", "Убрать ресурс на выходе", {.disabled = ro})) {
      Recipe nr = rc;
      nr.out.erase(nr.out.begin() + long(i));
      set("Убрать ресурс на выходе", nr);
    }
    a.markUi(m + ".outremove" + k);
  }
  if (!ro && !rc.out.empty() && rc.out.size() < 3) {
    ui::HStack hs(28, ui::Align::Left, 6);
    const Id next = nextOutput(w, rc);
    if (ui::button("Ресурс##out", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small, .disabled = !next,
                              .tooltip = "До трёх разных ресурсов на выходе"})) {
      Recipe nr = rc;
      nr.out.push_back(ResAmount{next, 1});
      set("Ресурс на выходе", nr);
    }
    a.markUi(m + ".addout");
  }
  ui::prop("Цикл", "hourglass", 0.4f);
  int turns = std::max(1, rc.turns);
  if (ui::numberField("turns", turns, {.min = 1, .max = 1000, .unit = "ход|хода|ходов", .steppers = true, .disabled = ro,
                                       .tooltip = "Ходов на один цикл преобразования"})) {
    Recipe nr = rc;
    nr.turns = turns;
    set("Срок преобразования", nr, "bt-rturns:" + co);
  }
  a.markUi(m + ".turns");
  if (!bld::recipeValid(w, rc)) ui::label("Рецепт не задан", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
}

// Рецепты постройки преобразования: свёрнутые — названием и строкой «вход → выход» (щелчок по шеврону — правка),
// раскрытый — полями; «Новый рецепт» в конец списка.
void recipesEditor(App& a, const BN& b, bool ro) {
  const World& w = a.world();
  const Id bid = b.id;
  ui::IdScope rs("recipes");
  RecipeUi& st = ui::state<RecipeUi>(ui::id("ui"));
  if (st.building != bid) st = RecipeUi{bid, b.recipes.size() == 1 ? 0 : -1, b.recipes.size()};
  if (b.recipes.size() > st.count) st.open = int(b.recipes.size()) - 1;   // новый рецепт — сразу в правке
  else if (b.recipes.size() < st.count) st.open = -1;                      // рецепт удалён — правка закрывается
  st.count = b.recipes.size();
  if (st.open >= int(b.recipes.size())) st.open = -1;
  for (int ri = 0; ri < int(b.recipes.size()); ri++) {
    ui::IdScope s(ri);
    const std::string m = "bt.recipe." + std::to_string(ri);
    if (ri == st.open) {
      recipeEdit(a, b, ri, ro, st);
      continue;
    }
    const Recipe& rc = b.recipes[size_t(ri)];
    {
      ui::Row row({ui::fr(1), ui::px(30)}, ui::kAuto, 6);
      {
        ui::Group g(0, 2);
        ui::label(rules::recipeTitle(w, rc).empty() ? "Рецепт " + std::to_string(ri + 1) : rules::recipeTitle(w, rc),
                  {.font = ui::Font::Strong, .ink = bld::recipeValid(w, rc) ? ui::Ink::Normal : ui::Ink::Warning});
        bld::tokens(bld::recipeTokens(w, rc), 6);
      }
      if (ui::iconButton(ro ? "chevron-down" : "edit", ro ? "Подробнее" : "Изменить рецепт")) st.open = ri;
      a.markUi(m + ".open");
    }
    a.markUi(m + ".row");
  }
  if (b.recipes.empty()) ui::label("Рецептов нет", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
  if (!ro) {
    if (ui::button("Новый рецепт", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small}))
      a.act("Новый рецепт", [&](Tx& tx) { rules::setRecipe(tx, bid, int(tx.w().building(bid)->recipes.size()), Recipe{}); });
    a.markUi("bt.recipe.add");
  }
}

// ---------------------------------------------------------------- особые отряды
// Особые отряды постройки доступа (ТЗ «Ввод новых механик», п.4–5): фишки (щелчок — справочник «Особые отряды»,
// крестик — убрать) и выбор из справочника.
void specialsEditor(App& a, const BN& b, bool ro) {
  const World& w = a.world();
  const Id bid = b.id;
  ui::IdScope ss("specials");
  bool any = false;
  for (Id sid : b.specials) any = any || w.special(sid) != nullptr;
  if (any) {
    ui::ChipFlow flow(40);
    for (Id sid : b.specials) {
      const SpecialUnit* su = w.special(sid);
      if (!su) continue;
      ui::IdScope s2{i64(sid)};
      std::string tip = std::string(schema::unitType(su->type).name) + (su->desc.empty() ? std::string() : "\n" + su->desc) + "\nОткрыть в справочнике";
      ui::ChipAction act = ui::flowChip(w::orName(su->name), {.icon = schema::unitType(su->type).icon, .tone = ui::Tone::Accent, .removable = !ro,
                                                              .clickable = true, .tooltip = tip});
      a.markUi("bt.special." + std::to_string(sid));
      if (act == ui::ChipAction::Click) a.openEditor("catalogs", 9);
      else if (act == ui::ChipAction::Remove) a.act("Убрать особый отряд", [&](Tx& tx) { rules::setBuildingSpecial(tx, bid, sid, false); });
    }
  }
  if (ro) return;
  std::vector<const SpecialUnit*> cand;
  for (const SpecialUnit& su : w.catalogs->specials)
    if (std::find(b.specials.begin(), b.specials.end(), su.id) == b.specials.end()) cand.push_back(&su);
  if (cand.empty()) {
    if (w.catalogs->specials.empty() && ui::link("Справочник особых отрядов", "special-unit")) a.openEditor("catalogs", 9);
    return;
  }
  w::IdOptions opts;
  for (const SpecialUnit* su : cand) opts.add(su->id, w::orName(su->name), schema::unitType(su->type).icon, Color(0, 0, 0, 0), schema::unitType(su->type).name);
  if (const Id sid = w::addCombo("add", opts, {.placeholder = "Особый отряд", .search = 1, .icon = "plus"}))
    a.act("Особый отряд постройки", [&](Tx& tx) { rules::setBuildingSpecial(tx, bid, sid, true); });
  a.markUi("bt.addspecial");
}

// ---------------------------------------------------------------- добыча
// Группы ресурсов провинции, которые добывает постройка (с подгруппами), и исключённые ресурсы (rules::setExtractGroups).
void extractEditor(App& a, const BN& b, bool ro) {
  const World& w = a.world();
  const Id bid = b.id;
  ui::IdScope es("extract");
  const std::vector<Id> groups = b.extractGroups, excl = b.extractExclude;
  auto set = [&](std::string_view label, const std::vector<Id>& g, const std::vector<Id>& x) {
    a.act(label, [&](Tx& tx) { rules::setExtractGroups(tx, bid, g, x); });
  };
  // Необязательная добыча (ферма): ставится при любом ресурсе провинции; ресурс из групп добывается больше.
  {
    bool opt = b.extractOptional;
    if (ui::toggle("Добыча не обязательна", opt, ro))
      a.act(opt ? "Добыча не обязательна" : "Добыча обязательна", [&](Tx& tx) { rules::setExtractOptional(tx, bid, opt); });
    ui::tooltip("Постройку можно ставить при любом ресурсе провинции; ресурс провинции из групп добычи добывается больше");
    a.markUi("bt.role.extractOptional");
  }
  ui::caption("Группы ресурсов");
  {
    ui::IdScope gs("groups");
    bool any = false;
    for (Id g : groups) any = any || w.catalogs->group(g) != nullptr;
    if (any) {
      ui::ChipFlow flow(40);
      for (Id g : groups) {
        if (!w.catalogs->group(g)) continue;
        ui::IdScope s2{i64(g)};
        const int n = int(rules::resourcesIn(w, g).size());
        ui::ChipAction act = ui::flowChip(rules::groupPath(w, g), {.icon = "layers", .tone = ui::Tone::Neutral, .removable = !ro,
                                                                   .tooltip = std::to_string(n) + " " + plural(n, "ресурс", "ресурса", "ресурсов") + " с подгруппами"});
        a.markUi("bt.extract.group." + std::to_string(g));
        if (act == ui::ChipAction::Remove) {
          std::vector<Id> ng = groups;
          ng.erase(std::remove(ng.begin(), ng.end(), g), ng.end());
          set("Убрать группу добычи", ng, excl);
        }
      }
    } else {
      ui::label(b.extractOptional ? "Группы не заданы: добыча не растёт" : "Группы не заданы: постройку нигде не построить",
                {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
    }
    if (!ro) {
      w::IdOptions opts;
      for (auto [g, depth] : w::groupOrder(w)) {
        if (std::find(groups.begin(), groups.end(), g) != groups.end()) continue;
        const ResGroup* rg = w.catalogs->group(g);
        if (!rg) continue;
        const int n = int(rules::resourcesIn(w, g).size());
        opts.add(g, std::string(size_t(depth) * 2, ' ') + w::orName(rg->name), "layers", Color(0, 0, 0, 0), std::to_string(n));
      }
      if (const Id g = w::addCombo("add", opts, {.placeholder = "Группа ресурсов", .search = 1, .icon = "plus"})) {
        std::vector<Id> ng = groups;
        ng.push_back(g);
        set("Группа добычи", ng, excl);
      }
      a.markUi("bt.extract.addgroup");
    }
  }
  if (excl.empty() && ro) return;
  ui::caption("Кроме ресурсов");
  {
    ui::IdScope xs("exclude");
    bool any = false;
    for (Id r : excl) any = any || w.resource(r) != nullptr;
    if (any) {
      ui::ChipFlow flow(40);
      for (Id r : excl) {
        if (!w.resource(r)) continue;
        ui::IdScope s2{i64(r)};
        ui::ChipAction act = ui::flowChip(w::resourceName(w, r), {.icon = w::resourceIcon(w, r), .removable = !ro, .tooltip = "Не добывается этой постройкой"});
        a.markUi("bt.extract.exclude." + std::to_string(r));
        if (act == ui::ChipAction::Remove) {
          std::vector<Id> nx = excl;
          nx.erase(std::remove(nx.begin(), nx.end(), r), nx.end());
          set("Убрать исключение добычи", groups, nx);
        }
      }
    }
    if (!ro) {
      std::vector<Id> cand;
      for (Id g : groups)
        for (Id r : rules::resourcesIn(w, g))
          if (std::find(excl.begin(), excl.end(), r) == excl.end() && std::find(cand.begin(), cand.end(), r) == cand.end()) cand.push_back(r);
      w::IdOptions opts;
      for (Id r : cand) {
        const CatalogItem* c = w.resource(r);
        opts.add(r, w::resourceName(w, r), w::resourceIcon(w, r), w::resourceColor(w, r), c && c->group ? rules::groupPath(w, c->group) : std::string());
      }
      if (const Id r = w::addCombo("add", opts, {.placeholder = cand.empty() ? "Сначала выберите группы" : "Исключить ресурс", .search = 1, .icon = "plus",
                                                 .disabled = cand.empty()})) {
        std::vector<Id> nx = excl;
        nx.push_back(r);
        set("Исключение добычи", groups, nx);
      }
      a.markUi("bt.extract.addexclude");
    }
  }
}

// Виды государства, которым доступна постройка (ни одного — всем): значки-переключатели.
void stateKindsRow(App& a, const BN& b, bool ro) {
  const Id bid = b.id;
  ui::IdScope ks("kinds");
  ui::Row row({ui::fr(1), ui::px(34), ui::px(34), ui::px(34)}, 34, 4);
  ui::label(b.stateKinds ? "Только для" : "Для всех видов государства",
            {.font = ui::Font::Small, .ink = b.stateKinds ? ui::Ink::Warning : ui::Ink::Dim, .icon = "crown",
             .tooltip = "Виды государства, которые могут строить постройку; ни один не выбран — все"});
  a.markUi("bt.kinds");
  for (int k = 0; k < int(StateKind::Count); k++) {
    ui::IdScope s(k);
    const bool on = (b.stateKinds >> unsigned(k)) & 1u;
    const schema::EnumInfo& e = schema::stateKind(StateKind(k));
    if (ui::iconButton(e.icon, std::string(e.name) + (on ? " — только ему" : ""),
                       {.variant = ui::Variant::Secondary, .toggled = on, .disabled = ro, .tone = on ? ui::Tone::Warning : ui::Tone::Neutral})) {
      const u32 nk = on ? b.stateKinds & ~(1u << unsigned(k)) : b.stateKinds | (1u << unsigned(k));
      a.act("Виды государства постройки", [&](Tx& tx) { rules::setBuildingStateKinds(tx, bid, nk); });
    }
    a.markUi("bt.kind." + std::to_string(k));
  }
}

}  // namespace

void rolesSection(App& a, const BN& b, bool ro) {
  int on = 0;
  for (const Feature& f : kFeatures) on += featureOn(b, f);
  ui::Section s("Особые возможности", "sparkles", {.badge = on ? std::to_string(on) : std::string()});
  a.markUi("bt.side.roles");
  if (!s) return;
  const Id bid = b.id;
  // Все возможности — значками: щелчок включает или выключает (подсказка — что даёт).
  {
    ui::IdScope gs("grid");
    const int per = std::max(1, int((ui::avail().w + 4) / 38));
    std::vector<ui::Len> cols(size_t(std::min<int>(per, int(std::size(kFeatures)))), ui::px(34));
    ui::Row grid(std::span<const ui::Len>(cols), 34, 4);
    for (const Feature& f : kFeatures) {
      ui::IdScope fs(f.mark);
      const bool cur = featureOn(b, f);
      if (ui::iconButton(f.icon, std::string(f.name) + (cur ? " — выключить" : "") + "\n" + f.tip, {.variant = ui::Variant::Secondary, .toggled = cur, .disabled = ro}) )
        setFeature(a, bid, f, !cur);
      // Цвет возможности поверх значка включённой.
      if (cur) {
        const RectF r = ui::lastItem().rect;
        ui::draw::rect(RectF{r.x + 6, r.bottom() - 4, r.w - 12, 2}, featureColor(f), 1);
      }
      a.markUi(f.mark);
    }
  }
  stateKindsRow(a, b, ro);
  // Включённые возможности: название, правка, выключение.
  for (const Feature& f : kFeatures) {
    if (!featureOn(b, f)) continue;
    ui::IdScope fs(f.mark);
    {
      ui::Row row({ui::px(20), ui::fr(1), ui::px(30)}, 28, 8);
      ui::iconColored(f.icon, featureColor(f), 18, f.tip);
      ui::label(f.name, {.font = ui::Font::Strong, .tooltip = f.tip});
      if (ui::iconButton("close", "Выключить", {.size = ui::Size::Small, .disabled = ro})) setFeature(a, bid, f, false);
      a.markUi(std::string(f.mark) + ".off");
    }
    // Правка — с небольшим отступом (рецепты — карточками во всю ширину: ряд «группа + ресурс» широкий).
    ui::Indent in(f.role && R(f.code) == R::Convert ? 0.f : 12.f);
    if (f.role && R(f.code) == R::Convert) recipesEditor(a, b, ro);
    else if (f.role && R(f.code) == R::Special) specialsEditor(a, b, ro);
    else if (f.role && R(f.code) == R::Extract) extractEditor(a, b, ro);
  }
}

}  // namespace rg::app::bldtree
