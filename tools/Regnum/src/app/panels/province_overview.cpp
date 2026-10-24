// Regnum — вкладка «Обзор» инспектора провинции (ТЗ 1.a.vi, 1.a.ix, 1.f.i): показатели, владелец и колонизация
// провинции без владельца («Общие доработки», п.3; окно: государство и переселенцы — не меньше 500), лорд, столица,
// пустошь нежити и осквернение («Виды государств», п.6, 7, 10, 11; очищение — с переселенцами), величина и тип города
// по населению со слотами, оккупация с оккупационным гарнизоном («Общие доработки», п.14), заметки и связь с каноном
// («Фиксы», п.7–8). Вкладка «Море» — короткая заметка для морской провинции (ТЗ 1.a.iii).
// Роли: владелец, величина, столица, оккупация — правка мира (App::canEdit); лорд, колонизация, пустошь и осквернение —
// игровые действия государства (App::canPlay, App::play), оккупационный гарнизон — оккупанта.
#include "app/common/canon.h"
#include "app/panels/military.h"
#include "app/panels/province_common.h"

namespace rg::app {
namespace {

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

// Величина провинции (правка мира) и тип города по населению (только показ: четыре ступени, текущая — золотом,
// пороги — в подсказках): значки, под ними — значение и вклад в слоты.
void settlement(App& a, const Province& p, bool edit) {
  Id pid = p.id;
  const ui::Theme& th = ui::theme();
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
    if (ui::segmented("size", size, std::span<const ui::Segment>(segs), {.disabled = !edit}))
      a.act("Величина провинции", [&](Tx& tx) { tx.province(pid).size = ProvSize(clamp(size, 0, 2)); });
    a.markUi("province.size");
    const schema::EnumInfo& cur = schema::provSize(p.size);
    ui::label(std::string(cur.name) + " · " + fmtSigned(cur.value), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  {
    ui::Group g(0, 6);
    ui::caption("Тип города");
    const CityType ct = rules::cityType(p);
    const i64 pop = populationOf(p);
    // Пороги ступеней: аванпост до 1000, деревня 1000–10 000, небольшой город 10 000–50 000, большой город от 50 000.
    const std::array<std::string, 4> ranges = {"до " + fmtNum(double(schema::kVillagePop)),
                                               fmtNum(double(schema::kVillagePop)) + "–" + fmtNum(double(schema::kTownPop)),
                                               fmtNum(double(schema::kTownPop)) + "–" + fmtNum(double(schema::kCityPop)),
                                               "от " + fmtNum(double(schema::kCityPop))};
    std::string all = "Тип города — по населению: " + fmtNum(double(pop)) + " жителей";
    for (int i = 0; i < 4; i++) all += std::string("\n") + schema::kCityTypes[i].name + " — " + ranges[size_t(i)];
    {
      const RectF row0 = ui::avail();
      ui::HStack hs(30, ui::Align::Left, 4);
      for (int i = 0; i < 4; i++) {
        const schema::EnumInfo& e = schema::kCityTypes[i];
        const bool cur = i == int(ct);
        const RectF r = ui::next(30, 30);
        ui::draw::rect(r, cur ? th.accent.alpha(0.18f) : th.surface3.alpha(0.5f), 7);
        ui::draw::rectStroke(r, cur ? th.accent.alpha(0.7f) : th.border, 7, 1);
        ui::draw::icon(e.icon, r.inset(6), cur ? th.accent : th.textMuted);
        ui::hoverTip("##city" + std::to_string(i), r,
                     std::string(e.name) + ": " + ranges[size_t(i)] + " жителей · " + fmtSigned(e.value) + " " + plSlots(e.value));
      }
      a.markUi("province.city", RectF{row0.x, row0.y, 4 * 30 + 3 * 4, 30});
    }
    const schema::EnumInfo& cur = schema::cityType(ct);
    ui::label(std::string(cur.name) + " · " + fmtSigned(cur.value), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .tooltip = all});
    a.markUi("province.cityName");
  }
}

// ---------------------------------------------------------------- колонизация
// Окно «Колонизация»: государство-колонизатор (игрок — только своё; казна и сколько жителей можно переселить — справа,
// не хватает золота или жителей — пункт недоступен), переселенцы — не меньше 500 и не больше доступных
// (rules::settlersAvailable), цена — константа «Стоимость колонизации». Отказ правила — уведомлением.
struct Colonize final : Dialog {
  Id pid = 0, state = 0;
  i64 settlers = schema::kMinSettlers;
  const char* id() const override { return "province.colonize"; }
  Style style(App& a) override {
    const Province* p = a.world().province(pid);
    return {"Колонизация · " + (p ? prov::provinceTitle(*p) : std::string("провинция")), "flag", ui::Tone::Accent, 460};
  }
  bool draw(App& a) override {
    const World& wd = prov::frameWorld(a);
    const Province* p = wd.province(pid);
    if (!p || p->sea || p->owner) return false;
    const double cost = std::max(0.0, rules::constantOf(wd, schema::cst::ColonizationCost).num);
    // Государства, за которые можно действовать.
    std::vector<const Faction*> states;
    wd.factions.each([&](const Faction& f) {
      if (f.isState() && a.canPlay(f.id)) states.push_back(&f);
    });
    std::sort(states.begin(), states.end(), [](const Faction* x, const Faction* y) { return compareRu(x->name, y->name) < 0; });
    if (state && std::none_of(states.begin(), states.end(), [&](const Faction* f) { return f->id == state; })) state = 0;
    if (!state && states.size() == 1) state = states[0]->id;
    ui::caption("Государство");
    {
      w::IdOptions opts;
      for (const Faction* f : states) {
        const bool lack = cost > 0 && f->treasury() + 1e-9 < cost;
        const i64 avail = rules::settlersAvailable(wd, f->id, pid);
        const bool few = avail < schema::kMinSettlers;
        opts.add(f->id, w::orName(f->name), nullptr, f->color, lack ? "казна " + prov::gold(f->treasury()) : few ? "жителей " + fmtNum(double(avail)) : prov::gold(f->treasury()),
                 lack || few);
      }
      Id pick = state;
      if (w::idCombo("state", pick, opts, {.placeholder = states.empty() ? "Нет государств" : "Выберите государство", .search = 1, .disabled = states.empty()}))
        state = pick;
      a.markUi("colonize.state");
    }
    const Faction* f = wd.faction(state);
    const i64 avail = f ? rules::settlersAvailable(wd, state, pid) : 0;
    ui::caption("Переселенцы");
    {
      ui::Row r({ui::fr(1), ui::px(150)}, 30, 8);
      const double hi = double(std::max<i64>(schema::kMinSettlers, avail));
      settlers = clamp<i64>(settlers, schema::kMinSettlers, i64(hi));
      ui::numberField("settlers", settlers, {.min = double(schema::kMinSettlers), .max = hi, .step = 100, .icon = "population", .steppers = true,
                                             .disabled = !f || avail < schema::kMinSettlers,
                                             .tooltip = "Не меньше " + fmtNum(double(schema::kMinSettlers)) + " и не больше жителей в других провинциях государства"});
      a.markUi("colonize.settlers");
      ui::label("из " + fmtNum(double(avail)), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Right,
                                              .tooltip = "Жителей в провинциях государства — переселяются поровну"});
    }
    {
      ui::Row r({ui::fr(1), ui::fr(1)}, 64, 10);
      ui::stat(prov::gold(cost), "Стоимость", {.icon = "coins", .tone = ui::Tone::Accent, .tooltip = "Константа «Стоимость колонизации»"});
      a.markUi("colonize.cost");
      if (f) {
        const double after = f->treasury() - cost;
        ui::stat(prov::gold(after), "Казна после", {.icon = "treasury", .tone = after < 0 ? ui::Tone::Danger : ui::Tone::Neutral,
                                                    .tooltip = "Сейчас " + prov::gold(f->treasury())});
      } else {
        ui::stat("—", "Казна после", {.icon = "treasury", .tone = ui::Tone::Neutral});
      }
    }
    // Опустошённая провинция: назначить государству нельзя, пока действует модификатор.
    const bool devastated = rules::provinceHas(wd, pid, schema::mod::Devastated);
    std::string why;
    if (a.readOnly()) why = "Открыт прошлый ход";
    else if (devastated) why = "Провинция опустошена: пока действует модификатор, назначить её государству нельзя";
    else if (!f) why = "Выберите государство";
    else if (cost > 0 && f->treasury() + 1e-9 < cost) why = "В казне " + prov::gold(f->treasury()) + ", нужно " + prov::gold(cost);
    else if (avail < schema::kMinSettlers) why = "Нужно не меньше " + fmtNum(double(schema::kMinSettlers)) + " переселенцев: у государства " + fmtNum(double(avail));
    if (!why.empty()) {
      ui::label(why, {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
      a.markUi("colonize.why");
    }
    ui::ModalFooter footer;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    if (ui::button("Колонизировать", {.variant = ui::Variant::Primary, .icon = "flag", .disabled = !why.empty(), .isDefault = true})) {
      const Id id = pid, sid = state;
      const i64 n = settlers;
      if (a.play("Колонизация", [&](Tx& tx) { rules::colonize(tx, id, sid, n); })) {
        a.toast(w::orName(f->name) + " колонизирует " + prov::provinceTitle(*p) + ": " + fmtNum(double(n)) + " переселенцев", ToastKind::Success, "flag");
        return false;
      }
    }
    a.markUi("colonize.ok");
    return true;
  }
};

void colonizeRow(App& a, const Province& p) {
  const World& wd = prov::frameWorld(a);
  const double cost = std::max(0.0, rules::constantOf(wd, schema::cst::ColonizationCost).num);
  // Колонизировать может администратор (за любое государство) или игрок (за своё).
  bool any = false;
  wd.factions.each([&](const Faction& f) { any = any || (f.isState() && a.canPlay(f.id)); });
  {
    ui::Row r({ui::fr(1), ui::px(104)}, 30, 8);
    if (ui::button("Колонизировать", {.icon = "flag", .fill = true, .disabled = !any,
                                      .tooltip = any ? std::string_view("Сделать государство владельцем: золото казны и переселенцы")
                                                     : std::string_view(a.readOnly() ? "Открыт прошлый ход" : "Нет государства, за которое можно действовать")})) {
      auto d = std::make_unique<Colonize>();
      d->pid = p.id;
      a.openDialog(std::move(d));
    }
    a.markUi("province.colonize");
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
    w::tagTip("Пока действует, провинцию нельзя назначить ни одному государству");
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

// Окно «Очистить пустошь нежити»: сколько жителей переселить из других провинций государства — не меньше 500 и не
// больше доступных (rules::settlersAvailable).
struct Settlers final : Dialog {
  Id pid = 0;
  i64 count = schema::kMinSettlers;
  const char* id() const override { return "province.settlers"; }
  Style style(App&) override { return {"Очистить пустошь нежити", "sun", ui::Tone::Accent, 420}; }
  bool draw(App& a) override {
    const World& wd = prov::frameWorld(a);
    const Province* p = wd.province(pid);
    if (!p || !landActs(wd, *p).cleanseWaste) return false;
    const i64 avail = rules::settlersAvailable(wd, p->owner, pid);
    const bool enough = avail >= schema::kMinSettlers;
    count = clamp<i64>(count, schema::kMinSettlers, std::max<i64>(schema::kMinSettlers, avail));
    ui::prop("Переселенцев", "population");
    ui::numberField("count", count, {.min = double(schema::kMinSettlers), .max = double(std::max<i64>(schema::kMinSettlers, avail)), .step = 100,
                                     .steppers = true, .disabled = !enough,
                                     .tooltip = "Не меньше " + fmtNum(double(schema::kMinSettlers)) + " и не больше жителей в других провинциях государства"});
    a.markUi("settlers.count");
    {
      ui::HStack hs(22, ui::Align::Right, 6);
      ui::label("Можно переселить", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      ui::label(fmtNum(double(avail)), {.font = ui::Font::Small, .ink = enough ? ui::Ink::Dim : ui::Ink::Warning});
      a.markUi("settlers.avail");
    }
    if (!enough) {
      ui::label("Нужно не меньше " + fmtNum(double(schema::kMinSettlers)) + " жителей в других провинциях государства",
                {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
      a.markUi("settlers.why");
    }
    ui::ModalFooter f;
    if (ui::button("Отмена")) return false;
    a.markUi("dialog.cancel");
    if (ui::button("Очистить", {.variant = ui::Variant::Primary, .icon = "sun", .disabled = !enough || !a.canPlay(p->owner), .isDefault = true})) {
      Id id = pid;
      i64 n = count;
      if (a.play("Очистить пустошь нежити", [&](Tx& tx) { rules::cleanseWasteland(tx, id, n); })) return false;
    }
    a.markUi("settlers.ok");
    return true;
  }
};

// Состояние земли (пустошь, осквернение, доход с ценности) и действия владельца.
void landSection(App& a, const Province& p, const rules::ProvinceCalc& pc, bool can) {
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
  ui::Disabled dis(!can);
  const std::string name = p.name.empty() ? std::string("Провинция") : p.name;
  if (acts.waste) {
    if (ui::button("Обратить в пустошь нежити", {.variant = ui::Variant::Danger, .icon = "skull", .fill = true}))
      a.confirm("Обратить в пустошь нежити?", "Население «" + name + "» (" + fmtNum(double(populationOf(p))) + ") станет трупами владельца. Отменить — Ctrl+Z.",
                "Обратить", true, [pid](App& x) { x.play("Обратить в пустошь нежити", [&](Tx& tx) { rules::makeWasteland(tx, pid); }); });
    a.markUi("province.waste");
  }
  if (acts.desecrate) {
    if (ui::button("Осквернить провинцию", {.variant = ui::Variant::Danger, .icon = "flame", .fill = true}))
      a.confirm("Осквернить провинцию?", "«" + name + "» станет осквернённой, владелец получит демоническую энергию. Отменить — Ctrl+Z.", "Осквернить", true,
                [pid](App& x) { x.play("Осквернить провинцию", [&](Tx& tx) { rules::desecrate(tx, pid); }); });
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
      a.play("Очистить осквернённую провинцию", [&](Tx& tx) { rules::cleanseDesecration(tx, pid); });
    a.markUi("province.cleanseDesecration");
  }
}

// ---------------------------------------------------------------- оккупационный гарнизон
// В провинции стоит войско оккупанта (не на борту флота): только тогда гарнизон пополняется (rules::setOccupationGarrison).
bool occupierHere(const World& w, Id province, Id occ) {
  return w.armies.findIf([&](const Army& ar) {
    if (ar.isFleet() || rules::carrierOf(w, ar.id) || mil::provinceUnder(w, ar.pos) != province) return false;
    return std::any_of(ar.groups.begin(), ar.groups.end(), [&](const ArmyGroup& g) { return g.faction == occ; });
  }) != nullptr;
}

// Отряды из резерва оккупанта (не больше резерва; пополнить — только при его войске в провинции, причина — в подсказке
// поля) и счётчик ходов без гарнизона и войск оккупанта.
void occupationGarrison(App& a, const Province& p) {
  const World& wd = prov::frameWorld(a);
  const Id pid = p.id, occ = p.occupier;
  const Faction* of = wd.faction(occ);
  if (!of || !of->isState()) return;
  const bool ro = !a.canPlay(occ);   // гарнизон ставит оккупант
  const bool here = occupierHere(wd, pid, occ);
  const std::string noArmy = "Пополняется, только когда в провинции стоит войско «" + w::orName(of->name) + "»";
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
      // Без войска оккупанта в провинции — только уменьшить (пустое поле выключено, причина — в подсказке).
      const std::string tip = here ? "Не больше резерва: " + mil::fmtCount(l.reserve) + " свободно" : noArmy;
      if (ui::numberField("n", n, {.min = 0, .max = double(here ? l.count + l.reserve : l.count), .disabled = ro || (!here && l.count == 0),
                                   .tooltip = tip})) {
        Id row = l.row.id;
        a.play("Оккупационный гарнизон", [&](Tx& tx) { rules::setOccupationGarrison(tx, pid, row, n); },
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
  const bool edit = a.canEdit();                            // правка мира
  const bool play = p->owner && a.canPlay(p->owner);        // игровые действия владельца
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
    if (w::factionPicker("owner", owner, w::FactionFilter::States, "Без владельца", 0, !edit))
      a.act("Владелец провинции", [&](Tx& tx) { rules::setProvinceOwner(tx, pid, owner); });
    a.markUi("province.owner");
    if (!p->owner) colonizeRow(a, *p);
    ui::prop("Лорд", "lord");
    Id lord = p->lord;
    // Назначать можно только доступных героев государства-владельца (rules::setLord).
    if (w::characterPicker("lord", lord, p->owner, "Не назначен", p->owner != 0 && edit, !play))
      a.play("Лорд провинции", [&](Tx& tx) { rules::setLord(tx, pid, lord); });
    a.markUi("province.lord");
    ui::prop("Столица", "capital");
    std::string cap = p->capital;
    if (ui::textField("capital", cap, {.placeholder = "Название города", .maxLength = 80, .disabled = !edit}))
      a.act("Столица провинции", [&](Tx& tx) { tx.province(pid).capital = trim(cap); });
    a.markUi("province.capital");
    landSection(a, *p, pc, play);
  }

  if (ui::Section s("Поселение и слоты", "slots"); s) {
    settlement(a, *p, edit);
    ui::spacer(2);
    std::string val = fmtNum(pc.slotsUsed) + " / " + fmtNum(pc.slots);
    ui::stat(val, "Слоты построек: занято / всего", {.icon = "slots", .tone = pc.slotsUsed > pc.slots ? ui::Tone::Danger : ui::Tone::Success,
                                                    .tooltip = "Слоты построек"});
    prov::breakdown("Слоты построек", prov::slotLines(wd, pc), 280);
    a.markUi("province.slots");
  }

  if (ui::Section s("Оккупация", "occupied", {.defaultOpen = true}); s) {
    bool occ = p->occupied;
    if (ui::toggle("Оккупирована", occ, !edit)) {
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
      if (w::factionPicker("occupier", oc, w::FactionFilter::States, "", p->owner, !edit || !p->occupied) && oc)
        a.act("Оккупант провинции", [&](Tx& tx) { rules::setOccupied(tx, pid, oc); });
      a.markUi("province.occupier");
      RectF sw = ui::next(30, 30);
      if (const Faction* of = p->occupied ? wd.faction(p->occupier) : nullptr) prov::hatchSwatch(sw.inset(5), of->color);
      else ui::draw::rectStroke(sw.inset(5), ui::theme().border, 3, 1);
    }
    if (p->occupied && p->occupier) occupationGarrison(a, *p);
  }

  // Заметки и канон (ТЗ «Фиксы», п.7–8): при связи с карточкой заметки — только чтение.
  if (ui::Section s("Заметки", "note", {.defaultOpen = true}); s) canon::notesField(a, SelType::Province, pid);
  if (ui::Section s("Канон", "book", {.defaultOpen = true}); s) canon::section(a, SelType::Province, pid);
}

// Морская провинция: без сведений, одна строка и действие.
void drawSea(App& a, Id pid) {
  ui::spacer(8);
  const bool ro = !a.canEdit();
  if (ui::emptyState("sea", "Морская провинция.", ro ? std::string_view() : "Сделать сухопутной", "land"))
    a.act("Сделать провинцию сухопутной", [&](Tx& tx) { rules::setProvinceSea(tx, pid, false); });
  a.markUi("province.seanote");
}

TabReg tabSea({"province.sea", "sea", "Морская провинция", 5, SelType::Province, prov::seaOnly, drawSea});
TabReg tabOverview({"province.overview", "info", "Обзор", 10, SelType::Province, prov::landOnly, drawOverview});

}  // namespace
}  // namespace rg::app
