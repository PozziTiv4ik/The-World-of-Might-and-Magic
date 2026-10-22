// Regnum — окно модификаторов: эффекты (группы, единицы, шаги, пределы ТЗ), цели дипломатии и генерация за ход.
#include "app/editors/modifiers_internal.h"

namespace rg::app::modedit {

// ---------------------------------------------------------------- эффекты: группы, единицы и шаги
u32 archMask() {
  u32 m = 0;
  for (int f = 0; f < kFxCount; f++)
    if (schema::kEffects[f].arch) m |= 1u << f;
  return m;
}
u32 heroMask() {
  u32 m = 0;
  for (int f = 0; f < kFxCount; f++)
    if (schema::kEffects[f].hero) m |= 1u << f;
  return m;
}

namespace {

// Arch — эффекты археологических групп (у модификатора группы — эта группа, у глобального — все группы государства);
// Hero — эффекты самого героя (модификатор героя и таланты).
FxGroup groupOf(int f) {
  const auto& e = schema::kEffects[f];
  return e.arch ? FxGroup::Arch : e.hero ? FxGroup::Hero : e.army ? FxGroup::Army : e.local ? FxGroup::Local : FxGroup::Global;
}

const char* fxUnit(Fx f) {
  switch (f) {
    case Fx::PopGrowthPct:
    case Fx::LoyaltyPerTurn: return "% за ход";
    case Fx::ContentmentPerTurn:
    case Fx::DiplomacyPerTurn: return "за ход";
    case Fx::ResourceFlat: return "ед.";
    case Fx::Slots: return "слот|слота|слотов";
    case Fx::ArchStartLevel: return "уровень|уровня|уровней";
    default: return schema::effect(f).unit[0] == '%' ? "%" : "";
  }
}
int fxDigits(Fx f) { return f == Fx::TradeFlat || f == Fx::Slots || f == Fx::ArchStartLevel ? 0 : 1; }
double fxStep(Fx f) { return schema::effect(f).max >= 1000 ? 10 : 1; }
// Начальное значение при включении эффекта (заметное, в пределах ТЗ).
double fxDefault(Fx f) {
  double mx = schema::effect(f).max;
  if (mx <= 5) return 1;
  if (mx <= 50) return 5;
  if (mx <= 100) return 10;
  return 100;
}
std::string rangeText(Fx f) {
  const auto& e = schema::effect(f);
  std::string u = e.unit[0] == '%' ? "\xC2\xA0%" : "";
  return fmtSigned(e.min) + "…" + fmtSigned(e.max) + u;
}

// ---------------------------------------------------------------- карточка: эффект
void effectRow(App& a, const Modifier& m, Fx f, bool ro) {
  const auto& e = schema::effect(f);
  const ui::Theme& th = ui::theme();
  ui::IdScope s{int(f)};
  bool on = m.has(f);
  double v = on ? m.fx[size_t(f)] : 0;
  RectF row = ui::next(0, 46);
  if (on) ui::draw::rect(row, th.accent.alpha(0.06f), 8);
  ui::Area ar(row.inset(8, 0), 0);
  ui::Row r({ui::px(22), ui::px(30), ui::fr(1.2f, 150), ui::fr(1, 90), ui::px(136)}, 46, 10);
  bool on2 = on;
  if (ui::checkbox("##on", on2, ro)) {
    editMod(a, m, on2 ? "Включить эффект модификатора" : "Выключить эффект модификатора", [&](Modifier& x) {
      if (on2) {
        x.fxMask |= 1u << int(f);
        double cur = x.fx[size_t(f)];
        x.fx[size_t(f)] = cur == 0 || cur < e.min || cur > e.max ? fxDefault(f) : cur;
      } else {
        x.fxMask &= ~(1u << int(f));
        x.fx[size_t(f)] = 0;
      }
    });
  }
  ui::tooltip(on ? "Выключить эффект" : "Включить эффект");
  a.markUi(std::string("modifiers.fx.") + e.id + ".on");
  {
    RectF ic = ui::next(30, 30);
    Color c = !on ? th.textMuted : v == 0 ? th.accent : w::effectGood(f, v) ? th.success : th.danger;
    ui::draw::rect(ic, c.alpha(on ? 0.16f : 0.08f), 8);
    ui::draw::icon(e.icon, ic.inset(6), c);
  }
  {
    ui::Group g(0, 0);
    ui::label(e.name, {.font = ui::Font::Body, .ink = on ? ui::Ink::Normal : ui::Ink::Dim});
    std::string sub = rangeText(f);
    if (e.perTurn) sub += " · каждый ход";
    ui::label(sub, {.font = ui::Font::Caption, .ink = ui::Ink::Muted});
  }
  bool dis = ro || !on;
  double sv = v;
  ui::Tone tone = sv == 0 ? ui::Tone::Accent : (w::effectGood(f, sv) ? ui::Tone::Success : ui::Tone::Danger);
  const std::string key = mergeKey(m, e.id);
  if (on) {
    if (ui::slider("s", sv, e.min, e.max, {.step = fxStep(f), .digits = fxDigits(f), .showValue = false, .disabled = ro, .tone = tone})) {
      double nv = clamp(sv, e.min, e.max);
      editMod(a, m, "Эффект модификатора", [&](Modifier& x) { x.fx[size_t(f)] = nv; }, key);
    }
  } else {
    // Выключенный эффект: только шкала с отметкой нуля (без ползунка; у пределов без нуля — без отметки).
    RectF tr = ui::next(0, 30).inset(8, 0);
    ui::draw::rect(RectF{tr.x, tr.cy() - 2, tr.w, 4}, th.track, 2);
    if (e.min < 0 && e.max > 0) {
      float zx = tr.x + float((0 - e.min) / (e.max - e.min)) * tr.w;
      ui::draw::rect(RectF{std::round(zx) - 1, tr.cy() - 6, 2, 12}, th.borderStrong, 1);
    }
  }
  a.markUi(std::string("modifiers.fx.") + e.id + ".slider");
  double nv = v;
  if (ui::numberField("v", nv, {.min = e.min, .max = e.max, .step = fxStep(f), .digits = fxDigits(f), .unit = fxUnit(f), .sign = true,
                                .disabled = dis, .tooltip = "Значение в пределах ТЗ: " + rangeText(f)})) {
    nv = clamp(nv, e.min, e.max);
    editMod(a, m, "Эффект модификатора", [&](Modifier& x) { x.fx[size_t(f)] = nv; }, key);
  }
  a.markUi(std::string("modifiers.fx.") + e.id + ".value");
}

// Цели дипломатии: несколько фракций (обязательны при включённом эффекте).
void targetsRow(App& a, const Modifier& m, bool ro) {
  ui::IdScope scope("targets");
  const World& w = a.world();
  std::vector<const Faction*> fs;
  w.factions.each([&](const Faction& f) { fs.push_back(&f); });
  std::sort(fs.begin(), fs.end(), [](const Faction* x, const Faction* y) {
    if (x->kind != y->kind) return x->kind < y->kind;
    return compareRu(x->name, y->name) < 0;
  });
  ui::Indent ind(70);
  {
    ui::HStack hs(22, ui::Align::Left, 6);
    ui::icon("target", missingTargets(m) ? ui::Ink::Warning : ui::Ink::Muted, 14);
    ui::label("Государства-цели", {.font = ui::Font::Small, .ink = ui::Ink::Dim});
    if (missingTargets(m)) ui::tag("Укажите хотя бы одно", ui::Tone::Warning);
  }
  // Выбранные цели — фишки (крестик убирает), ниже — выбор следующей цели с поиском.
  if (!m.targets.empty()) {
    ui::ChipFlow chipFlow;
    for (Id t : m.targets) {
      const Faction* f = w.faction(t);
      if (!f) continue;
      ui::IdScope s{i64(t)};
      ui::ChipOpt co;
      co.color = f->color;
      co.removable = !ro;
      co.clickable = true;
      co.tooltip = f->isGuild() ? "Гильдия — открыть" : "Государство — открыть";
      auto act = ui::flowChip(orName(f->name, "Без названия"), co);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, t});
      if (act == ui::ChipAction::Remove)
        editMod(a, m, "Цели модификатора дипломатии", [&](Modifier& x) { x.targets.erase(std::remove(x.targets.begin(), x.targets.end(), t), x.targets.end()); });
    }
  }
  if (!ro) {
    std::vector<const Faction*> left;
    for (const Faction* f : fs)
      if (!has(m.targets, f->id)) left.push_back(f);
    int idx = -1;
    if (ui::combo("addtarget", idx, int(left.size()),
                  [&](int i) {
                    const Faction* f = left[size_t(i)];
                    return ui::Option{f->name, nullptr, f->color, f->isGuild() ? "гильдия" : ""};
                  },
                  {.placeholder = "Добавить государство-цель", .search = 1, .icon = "plus", .disabled = left.empty()}) &&
        idx >= 0 && idx < int(left.size())) {
      Id t = left[size_t(idx)]->id;
      editMod(a, m, "Цели модификатора дипломатии", [&](Modifier& x) { x.targets.push_back(t); });
    }
    a.markUi("modifiers.targets");
  }
}

}  // namespace

void effectGroup(App& a, const Modifier& m, FxGroup group, bool ro) {
  int on = 0, total = 0;
  for (int f = 0; f < kFxCount; f++) {
    if (groupOf(f) != group) continue;
    total++;
    on += m.has(Fx(f)) ? 1 : 0;
  }
  std::string badge = std::to_string(on) + " из " + std::to_string(total);
  const char* title = group == FxGroup::Local    ? "Локальные (провинция)"
                      : group == FxGroup::Global ? "Глобальные (государство)"
                      : group == FxGroup::Arch   ? "Археологические группы"
                      : group == FxGroup::Hero   ? "Герой"
                                                 : "Войско";
  const char* icon = group == FxGroup::Local    ? "province"
                     : group == FxGroup::Global ? "crown"
                     : group == FxGroup::Arch   ? "pickaxe"
                     : group == FxGroup::Hero   ? "hero"
                                                : "army";
  ui::Section sec(title, icon, {.badge = badge});
  a.markUi(group == FxGroup::Local    ? "modifiers.local"
           : group == FxGroup::Global ? "modifiers.global"
           : group == FxGroup::Arch   ? "modifiers.arch"
           : group == FxGroup::Hero   ? "modifiers.hero"
                                      : "modifiers.army");
  if (!sec) return;
  ui::gap(2);
  for (int f = 0; f < kFxCount; f++) {
    if (groupOf(f) != group) continue;
    effectRow(a, m, Fx(f), ro);
    if (Fx(f) == Fx::DiplomacyPerTurn && m.has(Fx::DiplomacyPerTurn)) targetsRow(a, m, ro);
  }
}

namespace {

// ---------------------------------------------------------------- карточка: генерация за ход
// Эссенции и ресурсы, которые модификатор даёт каждый ход (ТЗ «Доработки №1», п.5): владельцу провинции, государству,
// фракции героя. У встроенного без записи — значения по умолчанию (некромант, лич); первая правка создаёт запись.
void genRows(App& a, const Modifier& m, bool essence, const std::map<Id, double>& cur, bool ro) {
  const World& w = a.world();
  ui::IdScope scope(essence ? "ess" : "res");
  const std::string mark = std::string("modifiers.gen.") + (essence ? "ess" : "res");
  auto setMap = [&](std::string_view label, std::map<Id, double> nm, std::string coalesce = {}) {
    editMod(a, m, label, [&](Modifier& x) { (essence ? x.essGen : x.resGen) = nm; }, std::move(coalesce));
  };
  for (auto [id, amount] : cur) {
    ui::IdScope s{i64(id)};
    const Id key = id;
    ui::Row r({ui::px(16), ui::fr(1, 100), ui::px(132), ui::px(28)}, 30, 6);
    const CatalogItem* c = essence ? w.essence(key) : w.resource(key);
    Color col = essence ? w::essenceColor(w, key) : w::resourceColor(w, key);
    col = w::legible(col, 0.12f);   // тёмный цвет — светлее на тёмном фоне
    ui::iconColored(essence ? "essence" : w::resourceIcon(w, key), col, 16);
    ui::label(c && !c->name.empty() ? c->name : std::string(essence ? "Эссенция" : "Ресурс"));
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .unit = essence ? nullptr : w::amountUnit(key),
                                      .disabled = ro, .tooltip = "За ход"})) {
      std::map<Id, double> nm = cur;
      nm[key] = std::max(0.0, v);
      setMap("Генерация модификатора", nm, mergeKey(m, mark + "." + std::to_string(key)));
    }
    a.markUi(mark + "." + std::to_string(key));
    if (ui::iconButton("close", essence ? "Убрать эссенцию" : "Убрать ресурс", {.size = ui::Size::Small, .disabled = ro})) {
      std::map<Id, double> nm = cur;
      nm.erase(key);
      setMap("Убрать генерацию модификатора", nm);
    }
    a.markUi(mark + "." + std::to_string(key) + ".remove");
  }
  if (ro) return;
  Id pick = 0;
  bool picked = false;
  if (essence) {
    std::vector<const CatalogItem*> left;
    for (const CatalogItem& e : w.catalogs->essences)
      if (!cur.count(e.id)) left.push_back(&e);
    int idx = -1;
    picked = ui::combo("addess", idx, int(left.size()),
                       [&](int i) {
                         const std::string& n = left[size_t(i)]->name;   // подпись — вид на запись справочника, не на временную строку
                         return ui::Option{n.empty() ? std::string_view("Без названия") : std::string_view(n), "essence", left[size_t(i)]->color};
                       },
                       {.placeholder = "Эссенция за ход", .search = 1, .icon = "plus", .disabled = left.empty()}) &&
             idx >= 0 && idx < int(left.size());
    if (picked) pick = left[size_t(idx)]->id;
  } else {
    std::vector<Id> left;
    for (const CatalogItem& r : w.catalogs->resources)
      if (!cur.count(r.id)) left.push_back(r.id);
    picked = w::resourceByGroup("addres", pick, false, &left) && pick;
  }
  a.markUi(mark + ".add");
  if (picked) {
    std::map<Id, double> nm = cur;
    nm[pick] = 1;
    setMap(essence ? "Эссенция за ход" : "Ресурс за ход", nm);
  }
}

}  // namespace

void genSection(App& a, const Modifier& m, bool ro) {
  const World& w = a.world();
  const std::map<Id, double> ess = rules::modEssGen(w, m), res = rules::modResGen(w, m);
  const size_t n = ess.size() + res.size();
  ui::Section sec("Генерация за ход", "repeat", {.badge = n ? std::to_string(n) : std::string()});
  a.markUi("modifiers.gen");
  if (!sec) return;
  ui::caption("Эссенции");
  genRows(a, m, true, ess, ro);
  ui::caption("Ресурсы");
  genRows(a, m, false, res, ro);
}


}  // namespace rg::app::modedit
