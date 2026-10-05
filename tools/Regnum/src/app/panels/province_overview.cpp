// Regnum — вкладка «Обзор» инспектора провинции (ТЗ 1.a.vi, 1.a.ix, 1.f.i): показатели, владелец и колонизация
// провинции без владельца («Общие доработки», п.3), лорд, столица, пустошь нежити и осквернение («Виды государств»,
// п.6, 7, 10, 11), величина и тип города со слотами, оккупация с оккупационным гарнизоном («Общие доработки», п.14),
// заметки и связь с каноном («Фиксы», п.7–8). Вкладка «Море» — короткая заметка для морской провинции (ТЗ 1.a.iii).
#include "app/canon.h"
#include "app/panels/military.h"
#include "app/widgets.h"

namespace rg::app::prov {
// province_common.cpp
const World& frameWorld(App& a);
struct TipLine {
  std::string label, value;
  ui::Tone tone = ui::Tone::Neutral;
  bool total = false;
};
struct ChipSpec {
  std::string label;
  const char* icon = nullptr;
  Color color{0, 0, 0, 0};
  ui::Tone tone = ui::Tone::Neutral;
  std::string tooltip;
  bool clickable = false;
};
std::string num(double v);
std::string pct(double v, bool sign = false);
std::string popText(double v);
std::string gold(double v);
void breakdown(std::string_view title, const std::vector<TipLine>& lines, float width = 300);
std::vector<TipLine> rebellionLines(const World& wd, const Province& p, const rules::ProvinceCalc& pc);
std::vector<TipLine> tradeLines(const World& wd, const Province& p, const rules::ProvinceCalc& pc);
std::vector<TipLine> productionLines(const World& wd, const Province& p, const rules::ProvinceCalc& pc);
std::vector<TipLine> slotLines(const World& wd, const rules::ProvinceCalc& pc);
int flowChips(std::string_view key, const std::vector<ChipSpec>& chips);
void hatchSwatch(RectF r, Color c);
Id defaultOccupier(const World& wd, const Province& p);
}  // namespace rg::app::prov

namespace rg::app {
namespace {

bool landOnly(App& a, Id pid) {
  const Province* p = a.world().province(pid);
  return p && !p->sea;
}
bool seaOnly(App& a, Id pid) {
  const Province* p = a.world().province(pid);
  return p && p->sea;
}

const char* plSlots(int n) { return plural(n, "слот", "слота", "слотов"); }

i64 populationOf(const Province& p) {
  i64 n = 0;
  for (const RacePop& r : p.races) n += std::max<i64>(0, r.pop);
  return n;
}

// Название и значок встроенного модификатора (запись мира или шаблон).
std::string modName(const World& wd, std::string_view key) {
  const Modifier* m = rules::builtinMod(wd, key);
  return m && !m->name.empty() ? m->name : std::string("Модификатор");
}
const char* modIcon(const World& wd, std::string_view key, const char* fallback) {
  const Modifier* m = rules::builtinMod(wd, key);
  return m && !m->icon.empty() ? m->icon.c_str() : fallback;
}

// Величина провинции и тип города: значки с подсказками, под ними — выбранное значение и вклад в слоты.
void settlement(App& a, const Province& p, bool ro) {
  Id pid = p.id;
  ui::Row row({ui::fr(3), ui::fr(4)}, ui::kAuto, 12);
  {
    ui::Group g(0, 6);
    ui::caption("Величина");
    std::array<std::string, 3> tips;
    std::array<ui::Segment, 3> segs;
    for (int i = 0; i < 3; i++) {
      const schema::EnumInfo& e = schema::kProvSizes[i];
      tips[size_t(i)] = std::string(e.name) + " · " + fmtSigned(e.value) + " " + plSlots(e.value);
      segs[size_t(i)] = ui::Segment{e.icon, {}, tips[size_t(i)]};
    }
    int size = int(p.size);
    if (ui::segmented("size", size, std::span<const ui::Segment>(segs), {.disabled = ro}))
      a.act("Величина провинции", [&](Tx& tx) { tx.province(pid).size = ProvSize(clamp(size, 0, 2)); });
    a.markUi("province.size");
    const schema::EnumInfo& cur = schema::provSize(p.size);
    ui::label(std::string(cur.name) + " · " + fmtSigned(cur.value), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  {
    ui::Group g(0, 6);
    ui::caption("Тип города");
    std::array<std::string, 4> tips;
    std::array<ui::Segment, 4> segs;
    for (int i = 0; i < 4; i++) {
      const schema::EnumInfo& e = schema::kCityTypes[i];
      tips[size_t(i)] = std::string(e.name) + " · " + fmtSigned(e.value) + " " + plSlots(e.value);
      segs[size_t(i)] = ui::Segment{e.icon, {}, tips[size_t(i)]};
    }
    int city = int(p.city);
    if (ui::segmented("city", city, std::span<const ui::Segment>(segs), {.disabled = ro}))
      a.act("Тип города", [&](Tx& tx) { tx.province(pid).city = CityType(clamp(city, 0, 3)); });
    a.markUi("province.city");
    const schema::EnumInfo& cur = schema::cityType(p.city);
    ui::label(std::string(cur.name) + " · " + fmtSigned(cur.value), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
}

// ---------------------------------------------------------------- колонизация
// Выбор государства-колонизатора: казна справа; не хватает золота — пункт недоступен. Отказ правила (опустошённая
// провинция) показывается уведомлением.
void colonizePopup(App& a, Id pid, double cost) {
  if (!ui::beginPopup("colonize", {.width = 300, .maxHeight = 380})) return;
  const World& wd = prov::frameWorld(a);
  std::vector<const Faction*> states;
  wd.factions.each([&](const Faction& f) {
    if (f.isState()) states.push_back(&f);
  });
  std::sort(states.begin(), states.end(), [](const Faction* x, const Faction* y) { return compareRu(x->name, y->name) < 0; });
  ui::caption("Колонизировать");
  if (states.empty()) ui::label("Государств нет.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  for (const Faction* f : states) {
    ui::IdScope s{i64(f->id)};
    const bool lack = cost > 0 && f->treasury() + 1e-9 < cost;
    const std::string hint = prov::gold(f->treasury());
    const std::string tip = lack ? "В казне " + hint + ", нужно " + prov::gold(cost) : std::string();
    if (ui::listItem(f->name.empty() ? std::string("Без названия") : f->name, {.dot = f->color, .hint = hint, .disabled = lack, .tooltip = tip})) {
      Id fid = f->id;
      ui::closePopup();
      a.act("Колонизация", [&](Tx& tx) { rules::colonize(tx, pid, fid); });
    }
    a.markUi("province.colonize." + std::to_string(f->id));
  }
  ui::endPopup();
}

void colonizeRow(App& a, const Province& p, bool ro) {
  const World& wd = prov::frameWorld(a);
  const double cost = std::max(0.0, rules::constantOf(wd, schema::cst::ColonizationCost).num);
  {
    ui::Row r({ui::fr(1), ui::px(104)}, 30, 8);
    if (ui::button("Колонизировать", {.icon = "flag", .fill = true, .disabled = ro, .tooltip = "Сделать государство владельцем за золото казны"}))
      ui::openPopup("colonize");
    a.markUi("province.colonize");
    colonizePopup(a, p.id, cost);
    ui::HStack hs(30, ui::Align::Right, 6);
    ui::iconColored(w::resourceIcon(wd, kGold), w::resourceColor(wd, kGold), 16, "Стоимость колонизации");
    ui::label(prov::gold(cost), {.font = ui::Font::Strong, .tooltip = "Стоимость колонизации"});
    a.markUi("province.colonizeCost");
  }
  // Опустошённая провинция: назначить государству нельзя, пока действует модификатор.
  if (const Modifier* m = rules::builtinMod(wd, schema::mod::Devastated); m && rules::provinceHas(wd, p.id, schema::mod::Devastated)) {
    std::string label = m->name;
    Id mid = rules::builtinModId(wd, schema::mod::Devastated);
    if (auto it = p.modTurns.find(mid); it != p.modTurns.end()) label += " · " + nTurns(it->second);
    ui::HStack hs(26, ui::Align::Left, 6);
    ui::tag(label, ui::Tone::Danger, m->icon.empty() ? "warning" : m->icon.c_str());
    ui::tooltip("Пока действует, провинцию нельзя назначить ни одному государству");
  }
}

// ---------------------------------------------------------------- пустошь нежити и осквернение
// Действия, которые правило разрешает владельцу (вид государства и модификаторы провинции).
struct LandActs {
  bool waste = false, cleanseWaste = false, desecrate = false, cleanseDesecration = false;
  bool any() const { return waste || cleanseWaste || desecrate || cleanseDesecration; }
};
LandActs landActs(const World& wd, const Province& p) {
  LandActs r;
  const Faction* o = wd.faction(p.owner);
  if (p.sea || !o || !o->isState()) return r;
  const bool waste = rules::hasModKey(wd, p.modifiers, schema::mod::UndeadWaste);
  const bool desecr = rules::hasModKey(wd, p.modifiers, schema::mod::Desecrated);
  r.waste = o->stateKind == StateKind::Undead && !waste;
  r.cleanseWaste = o->stateKind == StateKind::Living && waste;
  r.desecrate = o->stateKind == StateKind::Demonic && !waste && !desecr;
  r.cleanseDesecration = o->stateKind != StateKind::Demonic && desecr;
  return r;
}

// Окно «Очистить пустошь нежити»: сколько жителей переселить из других провинций государства (целое число).
struct Settlers final : Dialog {
  Id pid = 0;
  i64 count = 0;
  const char* id() const override { return "province.settlers"; }
  Style style(App&) override { return {"Очистить пустошь нежити", "sun", ui::Tone::Accent, 420}; }
  bool draw(App& a) override {
    const World& wd = prov::frameWorld(a);
    const Province* p = wd.province(pid);
    if (!p || !landActs(wd, *p).cleanseWaste) return false;
    const i64 avail = std::max<i64>(0, rules::statePopulation(wd, p->owner) - populationOf(*p));
    count = clamp<i64>(count, 0, avail);
    ui::prop("Переселенцев", "population");
    ui::numberField("count", count, {.min = 0, .max = double(avail), .step = 100, .steppers = true, .tooltip = "Не больше населения государства"});
    a.markUi("settlers.count");
    {
      ui::HStack hs(22, ui::Align::Right, 6);
      ui::label("Население государства", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      ui::label(fmtNum(double(avail)), {.font = ui::Font::Small, .ink = ui::Ink::Dim});
    }
    ui::ModalFooter f;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    if (ui::button("Очистить", {.variant = ui::Variant::Primary, .icon = "sun", .isDefault = true})) {
      Id id = pid;
      i64 n = count;
      a.act("Очистить пустошь нежити", [&](Tx& tx) { rules::cleanseWasteland(tx, id, n); });
      return false;
    }
    a.markUi("settlers.ok");
    return true;
  }
};

// Состояние земли (пустошь, осквернение, доход с ценности) и действия владельца.
void landSection(App& a, const Province& p, const rules::ProvinceCalc& pc, bool ro) {
  const World& wd = prov::frameWorld(a);
  const Id pid = p.id;
  const bool waste = rules::hasModKey(wd, p.modifiers, schema::mod::UndeadWaste);
  const bool desecr = rules::hasModKey(wd, p.modifiers, schema::mod::Desecrated);
  std::vector<prov::ChipSpec> chips;
  if (waste) chips.push_back({modName(wd, schema::mod::UndeadWaste), modIcon(wd, schema::mod::UndeadWaste, "skull"), {}, ui::Tone::Danger,
                              "Население — только 0; строит и получает доход с ценности только государство нежити"});
  if (desecr) chips.push_back({modName(wd, schema::mod::Desecrated), modIcon(wd, schema::mod::Desecrated, "flame"), {}, ui::Tone::Danger,
                               "Строит и получает доход с ценности только государство демонов"});
  if (pc.energy > 0) {
    const Id e = rules::resourceId(wd, schema::kResEnergy);
    chips.push_back({"+" + fmtNum(pc.energy) + " / ход", e ? w::resourceIcon(wd, e) : "flame", e ? w::resourceColor(wd, e) : Color(0, 0, 0, 0), ui::Tone::Info,
                     "Демоническая энергия владельцу за ход: 100 × (10 % населения)"});
  }
  if (pc.tradeBlocked) chips.push_back({"Без дохода с ценности", "warning", {}, ui::Tone::Warning, "Получатель дохода не того вида государства"});
  const LandActs acts = landActs(wd, p);
  if (chips.empty() && !acts.any()) return;
  ui::spacer(2);
  if (!chips.empty()) {
    RectF at = ui::avail();
    prov::flowChips("land", chips);
    a.markUi("province.land", RectF{at.x, at.y, at.w, std::max(0.f, ui::avail().y - at.y - ui::theme().gap)});
  }
  ui::Disabled dis(ro);
  const std::string name = p.name.empty() ? std::string("Провинция") : p.name;
  if (acts.waste) {
    if (ui::button("Обратить в пустошь нежити", {.variant = ui::Variant::Danger, .icon = "skull", .fill = true}))
      a.confirm("Обратить в пустошь нежити?", "Население «" + name + "» (" + fmtNum(double(populationOf(p))) + ") станет трупами владельца. Отменить — Ctrl+Z.",
                "Обратить", true, [pid](App& x) { x.act("Обратить в пустошь нежити", [&](Tx& tx) { rules::makeWasteland(tx, pid); }); });
    a.markUi("province.waste");
  }
  if (acts.desecrate) {
    if (ui::button("Осквернить провинцию", {.variant = ui::Variant::Danger, .icon = "flame", .fill = true}))
      a.confirm("Осквернить провинцию?", "«" + name + "» станет осквернённой, владелец получит демоническую энергию. Отменить — Ctrl+Z.", "Осквернить", true,
                [pid](App& x) { x.act("Осквернить провинцию", [&](Tx& tx) { rules::desecrate(tx, pid); }); });
    a.markUi("province.desecrate");
  }
  if (acts.cleanseWaste) {
    if (ui::button("Очистить пустошь нежити", {.icon = "sun", .fill = true})) {
      auto d = std::make_unique<Settlers>();
      d->pid = pid;
      a.openDialog(std::move(d));
    }
    a.markUi("province.cleanseWaste");
  }
  if (acts.cleanseDesecration) {
    if (ui::button("Очистить осквернённую провинцию", {.icon = "sun", .fill = true}))
      a.act("Очистить осквернённую провинцию", [&](Tx& tx) { rules::cleanseDesecration(tx, pid); });
    a.markUi("province.cleanseDesecration");
  }
}

// ---------------------------------------------------------------- оккупационный гарнизон
// Отряды из резерва оккупанта (не больше резерва) и счётчик ходов без гарнизона и войск оккупанта.
void occupationGarrison(App& a, const Province& p, bool ro) {
  const World& wd = prov::frameWorld(a);
  const Id pid = p.id, occ = p.occupier;
  const Faction* of = wd.faction(occ);
  if (!of || !of->isState()) return;
  ui::spacer(2);
  ui::caption("Оккупационный гарнизон");
  std::vector<mil::UnitRow> rows = mil::unitRows(wd, occ, false);
  if (rows.empty()) {
    ui::label("У оккупанта нет отрядов.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  } else {
    auto c = rules::calc(wd);
    struct L {
      mil::UnitRow row;
      i64 count, reserve;
    };
    std::vector<L> lines;
    for (const mil::UnitRow& r : rows) {
      i64 n = 0;
      for (const GarrisonEntry& g : p.occGarrison)
        if (g.row == r.id) n += g.count;
      const rules::RowCalc* rc = mil::rowCalc(*c, occ, r.id, false);
      lines.push_back(L{r, n, rc ? std::max<i64>(0, rc->reserve) : 0});
    }
    ui::Column cols[] = {{"Отряд", nullptr, ui::fr(1, 100)},
                         {"В гарнизоне", nullptr, ui::px(100), ui::Align::Left, false, "Не больше резерва оккупанта"},
                         {"Резерв", nullptr, ui::px(64), ui::Align::Right, false, "Свободно в резерве оккупанта"}};
    ui::Table t("occGarrison", cols, int(lines.size()), {.rowHeight = 42, .selectable = false});
    i64 total = 0;
    for (const L& l : lines) total += l.count;
    for (int i : t) {
      const L& l = lines[size_t(i)];
      RectF cr = t.cell();
      mil::unitCell(cr, l.row, false, l.row.name != l.row.typeName ? std::string_view(l.row.typeName) : std::string_view());
      t.cell();
      i64 n = l.count;
      if (ui::numberField("n", n, {.min = 0, .max = double(l.count + l.reserve), .disabled = ro,
                                   .tooltip = "Не больше резерва: " + mil::fmtCount(l.reserve) + " свободно"})) {
        Id row = l.row.id;
        a.act("Оккупационный гарнизон", [&](Tx& tx) { rules::setOccupationGarrison(tx, pid, row, n); },
              {.coalesce = "occGarrison:" + std::to_string(pid) + ":" + std::to_string(row)});
      }
      a.markUi("province.occGarrison." + std::to_string(l.row.id));
      t.text(mil::fmtCount(l.reserve), l.reserve > 0 ? ui::Ink::Success : ui::Ink::Muted);
    }
    if (t.footer()) {
      t.text("Итого", ui::Ink::Normal, ui::Font::Strong);
      t.text(mil::fmtCount(total), ui::Ink::Normal, ui::Font::Strong);
      t.cell();
    }
  }
  // ТЗ: без гарнизона и войск оккупанта 5 ходов подряд — оккупация снимается.
  ui::prop("Без гарнизона и войск", "hourglass", 0.55f);
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    const int idle = std::max(0, p.occIdle);
    ui::label(fmtNum(idle) + " из " + nTurns(schema::kOccupationIdleTurns),
              {.font = ui::Font::Strong, .ink = idle > 0 ? ui::Ink::Warning : ui::Ink::Dim,
               .tooltip = "Ходов подряд без гарнизона и войск оккупанта: на пятом оккупация снимается"});
    a.markUi("province.occIdle");
  }
}

// ---------------------------------------------------------------- вкладка
void drawOverview(App& a, Id pid) {
  const World& wd = prov::frameWorld(a);
  const Province* p = wd.province(pid);
  if (!p) return;
  ui::IdScope ps{i64(pid)};
  bool ro = a.readOnly();
  auto calc = rules::calc(wd);
  rules::ProvinceCalc none;
  const rules::ProvinceCalc* pcp = calc->province(pid);
  const rules::ProvinceCalc& pc = pcp ? *pcp : none;

  // Показатели
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
    ui::stat(prov::popText(double(pc.population)), "Население", {.icon = "population", .tooltip = "Сумма численности рас"});
    ui::stat(prov::num(pc.tradeValue), "Торговля", {.icon = "trade-value", .tone = ui::Tone::Info, .tooltip = "Торговая ценность"});
    prov::breakdown("Текущая торговая ценность", prov::tradeLines(wd, *p, pc));
    ui::stat(prov::pct(pc.rebellion), "Восстание", {.icon = "rebellion", .tone = ui::Tone::Danger, .tooltip = "Вероятность восстания"});
    prov::breakdown("Вероятность восстания", prov::rebellionLines(wd, *p, pc));
    a.markUi("province.overview.rebellion");
    const CatalogItem* res = wd.resource(p->resource);
    ui::stat(prov::num(pc.production), "Добыча за ход",
             {.icon = res ? w::resourceIcon(wd, p->resource) : "resource", .tone = ui::Tone::Success, .tooltip = "Добыча за ход"});
    if (res) prov::breakdown("Добыча за ход · " + res->name, prov::productionLines(wd, *p, pc));
  }

  if (ui::Section s("Владение", "crown"); s) {
    ui::prop("Государство", "crown");
    Id owner = p->owner;
    if (w::factionPicker("owner", owner, w::FactionFilter::States, "Без владельца", 0, ro))
      a.act("Владелец провинции", [&](Tx& tx) { rules::setProvinceOwner(tx, pid, owner); });
    a.markUi("province.owner");
    if (!p->owner) colonizeRow(a, *p, ro);
    ui::prop("Лорд", "lord");
    Id lord = p->lord;
    // Назначать можно только доступных героев государства-владельца (rules::setLord).
    if (w::characterPicker("lord", lord, p->owner, "Не назначен", p->owner != 0, ro || (!p->owner && !p->lord)))
      a.act("Лорд провинции", [&](Tx& tx) { rules::setLord(tx, pid, lord); });
    a.markUi("province.lord");
    ui::prop("Столица", "capital");
    std::string cap = p->capital;
    if (ui::textField("capital", cap, {.placeholder = "Название города", .maxLength = 80, .disabled = ro}))
      a.act("Столица провинции", [&](Tx& tx) { tx.province(pid).capital = trim(cap); });
    a.markUi("province.capital");
    landSection(a, *p, pc, ro);
  }

  if (ui::Section s("Поселение и слоты", "slots"); s) {
    settlement(a, *p, ro);
    ui::spacer(2);
    std::string val = fmtNum(pc.slotsUsed) + " / " + fmtNum(pc.slots);
    ui::stat(val, "Слоты построек: занято / всего", {.icon = "slots", .tone = pc.slotsUsed > pc.slots ? ui::Tone::Danger : ui::Tone::Success,
                                                    .tooltip = "Слоты построек"});
    prov::breakdown("Слоты построек", prov::slotLines(wd, pc), 280);
    a.markUi("province.slots");
  }

  if (ui::Section s("Оккупация", "occupied", {.defaultOpen = true}); s) {
    bool occ = p->occupied;
    if (ui::toggle("Оккупирована", occ, ro)) {
      if (occ) {
        Id def = p->occupier && p->occupier != p->owner ? p->occupier : prov::defaultOccupier(wd, *p);
        if (!def) a.toast("Нет государства, которое могло бы оккупировать провинцию", ToastKind::Warning, "occupied");
        else a.act("Оккупация провинции", [&](Tx& tx) { rules::setOccupied(tx, pid, def); });
      } else {
        a.act("Снять оккупацию", [&](Tx& tx) { rules::setOccupied(tx, pid, 0); });
      }
    }
    a.markUi("province.occupied");
    {
      ui::Row r({ui::fr(1), ui::px(30)}, 30, 8);
      Id oc = p->occupied ? p->occupier : 0;
      if (w::factionPicker("occupier", oc, w::FactionFilter::States, "", p->owner, ro || !p->occupied) && oc)
        a.act("Оккупант провинции", [&](Tx& tx) { rules::setOccupied(tx, pid, oc); });
      a.markUi("province.occupier");
      RectF sw = ui::next(30, 30);
      if (const Faction* of = p->occupied ? wd.faction(p->occupier) : nullptr) prov::hatchSwatch(sw.inset(5), of->color);
      else ui::draw::rectStroke(sw.inset(5), ui::theme().border, 3, 1);
    }
    if (p->occupied && p->occupier) occupationGarrison(a, *p, ro);
  }

  // Заметки и канон (ТЗ «Фиксы», п.7–8): при связи с карточкой заметки — только чтение.
  if (ui::Section s("Заметки", "note", {.defaultOpen = true}); s) canon::notesField(a, SelType::Province, pid);
  if (ui::Section s("Канон", "book", {.defaultOpen = true}); s) canon::section(a, SelType::Province, pid);
}

// Морская провинция: без сведений, одна строка и действие.
void drawSea(App& a, Id pid) {
  ui::spacer(8);
  bool ro = a.readOnly();
  if (ui::emptyState("sea", "Морская провинция.", ro ? std::string_view() : "Сделать сухопутной", "land"))
    a.act("Сделать провинцию сухопутной", [&](Tx& tx) { rules::setProvinceSea(tx, pid, false); });
  a.markUi("province.seanote");
}

TabReg tabSea({"province.sea", "sea", "Морская провинция", 5, SelType::Province, seaOnly, drawSea});
TabReg tabOverview({"province.overview", "info", "Обзор", 10, SelType::Province, landOnly, drawOverview});

}  // namespace
}  // namespace rg::app
