// Regnum — выбор строительства в провинции (ТЗ 1.f.ii, 1.h): постройки общего дерева и уникальные постройки
// владельца по категориям, стоимость (нехватка — красным), срок, эффекты уровня, особые возможности, причины
// недоступности (в том числе требуемые технологии и культовая — одна на карту или государство). «Поставить готовой» —
// изначальная постройка без цены и срока, у многоуровневой — с выбором уровня (ТЗ «Доработки», п.5).
#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/widgets.h"

namespace rg::app {

namespace {

struct BuildPicker : Dialog {
  Id pid = 0;
  std::string query;
  bool onlyAvail = false;
  int tab = 0;   // 0 — все, далее категории
  std::map<Id, int> placeLevel;   // постройка → уровень для «Поставить готовой»
  const char* id() const override { return "build.picker"; }
  Style style(App& a) override {
    const Province* p = a.world().province(pid);
    return {"Строительство · " + (p ? p->name : std::string("провинция")), "build", ui::Tone::Accent, 740};
  }
  bool draw(App& a) override;
};

// Особые возможности постройки: рецепт преобразования, эссенции уровня, особые отряды; культовая — одна на карту.
void roleLines(App& a, const Building& b, const BuildingLevel* L) {
  const World& w = a.world();
  if (const char* rule = bld::cultRule(b))
    ui::label(rule, {.font = ui::Font::Small, .color = bld::catColor(BuildingCat::Cult), .icon = "b-cult", .tooltip = "Культовая постройка"});
  if (b.convert) {
    std::vector<bld::Token> tk = bld::recipeTokens(w, b.recipe);
    tk.insert(tk.begin(), bld::Token{"convert", ui::theme().info, {}, ui::Ink::Normal, "Преобразование ресурсов"});
    bld::tokens(tk);
  }
  if (b.essenceGen && L) {
    std::vector<bld::Token> tk = bld::essenceTokens(w, L->essence);
    if (!tk.empty()) {
      tk.insert(tk.begin(), bld::Token{"repeat", ui::theme().textMuted, {}, ui::Ink::Muted, "Даёт за ход"});
      bld::tokens(tk, 6);
    }
  }
  if (b.specialAccess && !b.specials.empty())
    ui::label(bld::specialsText(w, b.specials), {.font = ui::Font::Small, .ink = ui::Ink::Accent, .icon = "special-unit", .tooltip = "Доступ к особым отрядам"});
}

void optionCard(App& a, BuildPicker& d, const rules::BuildOption& o, const Faction* owner, bool& close) {
  const World& w = a.world();
  const Building* b = w.building(o.building);
  if (!b) return;
  const bool ro = a.readOnly();
  ui::IdScope s{i64(o.building)};
  std::string name = b->name;
  const int maxL = std::max(1, int(b->levels.size()));
  int lvl = clamp(o.level, 1, maxL);
  const BuildingLevel* L = b->levels.empty() ? nullptr : &b->levels[size_t(lvl - 1)];
  ui::Card card({.pad = 12, .tone = o.can ? ui::Tone::Accent : ui::Tone::Neutral});
  ui::Row row({ui::px(44), ui::fr(1), ui::px(140)}, ui::kAuto, 12);
  bld::iconTile(*b, 44, !o.can);
  {
    ui::Group g(0, 5);
    {
      ui::HStack hs(22, ui::Align::Left, 6);
      ui::label(name, {.font = ui::Font::Strong, .ink = o.can ? ui::Ink::Normal : ui::Ink::Dim});
      if (o.upgrade) ui::tag("Улучшение " + tree::roman(lvl - 1) + " → " + tree::roman(lvl), ui::Tone::Info, "trend-up");
      else ui::tag("Уровень " + tree::roman(lvl), ui::Tone::Neutral);
      if (b->owner) ui::tag("Уникальная", ui::Tone::Accent, "crown");
    }
    std::string desc = L && !trim(L->desc).empty() ? L->desc : b->desc;
    if (!trim(desc).empty()) ui::label(desc, {.font = ui::Font::Small, .ink = ui::Ink::Dim, .wrap = true, .maxLines = 2});
    bld::costChips(o.cost, owner);
    if (L) {
      bld::levelEffects(w, *L);
      bld::produceChips(L->produce);
    }
    roleLines(a, *b, L);
    for (size_t i = 0; i < o.reasons.size() && i < 3; i++) {
      ui::IdScope rs{int(i)};
      ui::label(o.reasons[i], {.font = ui::Font::Small, .ink = ui::Ink::Danger, .icon = "warning", .wrap = true});
    }
  }
  {
    ui::Group g(0, 6);
    std::string tip = o.reasons.empty() ? std::string("Ресурсы спишутся сразу; бонусы — по завершении") : join(o.reasons, "\n");
    // Кнопки карточек — обычные (золотых на виду не больше одной); доступные карточки отмечены золотой полоской.
    if (ui::button(o.upgrade ? "Улучшить" : "Построить", {.icon = o.upgrade ? "trend-up" : "build", .fill = true, .disabled = !o.can || ro, .tooltip = tip})) {
      Id pid = d.pid, bid = o.building;
      if (a.act(o.upgrade ? "Улучшить постройку" : "Построить", [&](Tx& tx) { rules::startBuilding(tx, pid, bid); })) {
        a.toast((o.upgrade ? "Улучшение начато: " : "Строительство начато: ") + name + " — " + nTurns(o.turns), ToastKind::Success, "build");
        close = true;
      }
    }
    a.markUi("build.option." + std::to_string(o.building));
    ui::label(nTurns(o.turns), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Center, .icon = "hourglass"});
    // Поставить готовой: изначальная постройка провинции — без цены и срока (те же условия, кроме цены). Уровень —
    // от следующего (у уже стоящей) до наибольшего.
    const int from = clamp(o.level, 1, maxL);
    int& place = d.placeLevel[o.building];
    if (place < from || place > maxL) place = from;
    if (o.canPlace && maxL > from) {
      std::vector<std::string> romans;
      for (int l = from; l <= maxL; l++) romans.push_back(tree::roman(l));
      int idx = place - from;
      if (romans.size() <= 5) {
        std::vector<ui::Segment> segs;
        for (const std::string& r : romans) segs.push_back(ui::Segment{nullptr, r, "Уровень готовой постройки"});
        if (ui::segmented("placelvl", idx, std::span<const ui::Segment>(segs), {.size = ui::Size::Small, .disabled = ro})) place = from + idx;
      } else {
        std::vector<ui::Option> opts;
        for (const std::string& r : romans) opts.push_back(ui::Option{r});
        if (ui::combo("placelvl", idx, std::span<const ui::Option>(opts), {.placeholder = "Уровень", .disabled = ro, .tooltip = "Уровень готовой постройки"}) &&
            idx >= 0)
          place = from + idx;
      }
      a.markUi("build.placeLevel." + std::to_string(o.building));
    }
    const std::vector<std::string>& why = !o.placeReasons.empty() ? o.placeReasons : o.reasons;
    const std::string placeTip = o.canPlace ? std::string("Сразу готова — без цены и срока (изначальная постройка провинции)") : join(why, "\n");
    if (ui::button("Поставить готовой", {.variant = ui::Variant::Ghost, .icon = "bolt", .size = ui::Size::Small, .fill = true, .disabled = !o.canPlace || ro,
                                         .tooltip = placeTip})) {
      const Id pid = d.pid, bid = o.building;
      const int level = place;
      if (a.act("Поставить постройку готовой", [&](Tx& tx) { rules::placeBuilding(tx, pid, bid, level); }))
        a.toast("Поставлена готовой: " + name + (maxL > 1 ? " " + tree::roman(level) : std::string()), ToastKind::Success, "bolt");
    }
    a.markUi("build.place." + std::to_string(o.building));
  }
}

bool BuildPicker::draw(App& a) {
  const World& w = a.world();
  const Province* p = w.province(pid);
  if (!p || p->sea) return false;
  const Faction* owner0 = w.faction(p->owner);
  const Faction* owner = owner0 && owner0->isState() ? owner0 : nullptr;
  auto calc = rules::calc(w);
  const rules::ProvinceCalc* pc = calc->province(pid);
  int slots = pc ? pc->slots : 0, used = int(p->buildings.size());
  std::vector<rules::BuildOption> opts = rules::buildOptions(w, pid);
  // Шапка: владелец, слоты, запасы ресурсов, нужных для строительства.
  {
    ui::HStack hs(28, ui::Align::Left, 8);
    if (owner) w::factionChip(owner->id);
    ui::tag("Слоты " + std::to_string(used) + " / " + std::to_string(slots), used >= slots ? ui::Tone::Danger : ui::Tone::Neutral, "slots");
    if (pc && std::fabs(pc->buildCostFactor - 1) > 1e-9) ui::tag("Стоимость ×" + fmtNum(pc->buildCostFactor, 2), pc->buildCostFactor > 1 ? ui::Tone::Warning : ui::Tone::Success, "percent");
    ui::flex();
    if (owner) {
      std::map<Id, double> need;
      need[kGold] = 0;
      for (const auto& o : opts)
        for (auto& [res, v] : o.cost) need[res] = 0;
      for (auto& [res, v] : need) {
        ui::IdScope s{i64(res)};
        const CatalogItem* c = w.resource(res);
        ui::spacer(6);
        std::string tip = (c ? c->name : std::string("Ресурс")) + " в запасе государства";
        ui::iconColored(w::resourceIcon(w, res), w::resourceColor(w, res), 16, tip);
        ui::label(fmtNum(owner->stock(res), 3), {.font = ui::Font::Strong, .tooltip = tip});
      }
    }
  }
  // Фильтры
  int counts[int(BuildingCat::Count) + 1] = {};
  for (const auto& o : opts) {
    const Building* b = w.building(o.building);
    if (!b) continue;
    if (onlyAvail && !o.can) continue;
    if (!query.empty() && !utf8::matches(b->name, query) && !utf8::matches(b->desc, query)) continue;
    counts[0]++;
    counts[1 + clamp(int(b->cat), 0, int(BuildingCat::Count) - 1)]++;
  }
  {
    ui::Row r({ui::fr(1), ui::px(190)}, 30, 10);
    ui::searchField("q", query, "Найти постройку");
    a.markUi("build.search");
    ui::toggle("Только доступные", onlyAvail);
    a.markUi("build.onlyAvail");
  }
  {
    std::vector<ui::Tab> tabs;
    tabs.push_back(ui::Tab{"list", "Все", {}, counts[0]});
    for (int c = 0; c < int(BuildingCat::Count); c++) tabs.push_back(ui::Tab{bld::catIcon(BuildingCat(c)), {}, bld::catName(BuildingCat(c)), counts[c + 1]});
    ui::tabs("cats", tab, std::span<const ui::Tab>(tabs), {.style = ui::TabStyle::Pill, .fill = true, .size = ui::Size::Small});
  }
  bool close = false;
  {
    float h = std::min(470.f, std::max(220.f, ui::viewport().h - 330));
    ui::Scroll sc("list", h);
    int shown = 0;
    for (int c = 0; c < int(BuildingCat::Count); c++) {
      if (tab != 0 && tab != c + 1) continue;
      std::vector<const rules::BuildOption*> list;
      for (const auto& o : opts) {
        const Building* b = w.building(o.building);
        if (!b || int(b->cat) != c) continue;
        if (onlyAvail && !o.can) continue;
        if (!query.empty() && !utf8::matches(b->name, query) && !utf8::matches(b->desc, query)) continue;
        list.push_back(&o);
      }
      if (list.empty()) continue;
      // Доступные — выше.
      std::stable_sort(list.begin(), list.end(), [](const rules::BuildOption* x, const rules::BuildOption* y) { return x->can > y->can; });
      if (tab == 0) {
        ui::IdScope cs(c);
        ui::HStack hs(24, ui::Align::Left, 8);
        ui::iconColored(bld::catIcon(BuildingCat(c)), bld::catColor(BuildingCat(c)), 18);
        ui::label(bld::catName(BuildingCat(c)), {.font = ui::Font::Strong});
        ui::badge(std::to_string(list.size()), ui::Tone::Neutral);
      }
      for (const rules::BuildOption* o : list) {
        optionCard(a, *this, *o, owner, close);
        shown++;
      }
    }
    if (shown == 0) {
      if (opts.empty()) {
        if (ui::emptyState("building", "В дереве построек государства пока ничего нет.", "Открыть дерево построек", "building")) {
          openBuildingTree(a, 0);
          close = true;
        }
      } else if (onlyAvail) {
        if (ui::emptyState("search", "Сейчас ничего не доступно для строительства.", "Показать все", "list")) onlyAvail = false;
      } else {
        ui::emptyState("search", "Ничего не найдено.");
      }
    }
  }
  ui::ModalFooter f;
  if (ui::button("Дерево построек", {.variant = ui::Variant::Ghost, .icon = "building"})) {
    openBuildingTree(a, 0);
    return false;
  }
  if (ui::button("Закрыть", {.isDefault = true})) return false;
  a.markUi("dialog.cancel");
  return !close;
}

std::unique_ptr<Dialog> makePicker(App&, Id province) {
  auto d = std::make_unique<BuildPicker>();
  d->pid = province;
  return d;
}

DialogReg regPicker({"build.picker", makePicker});

}  // namespace

std::unique_ptr<Dialog> buildPickerDialog(Id province) {
  auto d = std::make_unique<BuildPicker>();
  d->pid = province;
  return d;
}

}  // namespace rg::app
