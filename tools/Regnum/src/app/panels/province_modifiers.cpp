// Regnum — вкладка «Модификаторы» инспектора провинции (ТЗ 1.g, 1.b.iii; «Модификаторы», п.1): автоматические
// модификаторы («Столица государства» — только чтение, с причиной), список модификаторов провинции через правило
// (срок по умолчанию, оставшиеся ходы с правкой) и сводка всех действующих локальных эффектов с источниками —
// провинция, государство, технологии, постройки, гильдии, автоматические.
#include "app/widgets.h"

namespace rg::app::prov {
// province_common.cpp
const World& frameWorld(App& a);
struct ChipSpec {
  std::string label;
  const char* icon = nullptr;
  Color color{0, 0, 0, 0};
  ui::Tone tone = ui::Tone::Neutral;
  std::string tooltip;
  bool clickable = false;
};
int flowChips(std::string_view key, const std::vector<ChipSpec>& chips);
const char* sourceIcon(rules::EffectSource::Kind k);
const char* sourceKind(rules::EffectSource::Kind k);
const Modifier* sourceMod(const World& wd, const rules::EffectSource& s);
std::string sourceName(const World& wd, const rules::EffectSource& s);
}  // namespace rg::app::prov

namespace rg::app {
namespace {

bool landOnly(App& a, Id pid) {
  const Province* p = a.world().province(pid);
  return p && !p->sea;
}

int modBadge(App& a, Id pid) {
  const Province* p = a.world().province(pid);
  return p && !p->sea ? int(p->modifiers.size()) : 0;
}

std::string modTitle(const Modifier& m) { return m.name.empty() ? std::string("Модификатор") : m.name; }

// Подсказка модификатора: описание и эффекты.
std::string modTip(const Modifier& m) {
  std::string tip = m.desc;
  for (int f = 0; f < kFxCount; f++) {
    if (!m.has(Fx(f))) continue;
    if (!tip.empty()) tip += "\n";
    tip += w::effectText(Fx(f), m.fx[size_t(f)]);
  }
  return tip;
}

// Локальные эффекты модификатора — фишки (польза — зелёная, вред — красная).
std::vector<prov::ChipSpec> localChips(const Modifier& m) {
  std::vector<prov::ChipSpec> out;
  for (int f = 0; f < kFxCount; f++) {
    if (!m.has(Fx(f)) || !schema::kEffects[f].local) continue;
    double v = m.fx[size_t(f)];
    if (v == 0 || !std::isfinite(v)) continue;
    prov::ChipSpec c;
    c.label = w::effectText(Fx(f), v);
    c.icon = schema::kEffects[f].icon;
    c.tone = w::effectGood(Fx(f), v) ? ui::Tone::Success : ui::Tone::Danger;
    c.tooltip = schema::kEffects[f].name;
    out.push_back(std::move(c));
  }
  return out;
}

// Модификаторы провинции (общий список модификаторов): срок на фишке, щелчок — правка оставшихся ходов; добавленные —
// по правилу (срок по умолчанию модификатора); встроенные без записи мира создаются при выборе.
void modifierRows(App& a, const Province& p, bool ro) {
  const Id pid = p.id;
  std::vector<Id> ids = p.modifiers;
  w::ModEdit ed;
  if (w::modifierList("mods", ids, ro, w::ModScope::Local, &p.modTurns, &ed))
    a.act("Модификаторы провинции", [&](Tx& tx) {
      if (!ed.addKey.empty()) ids.push_back(rules::ensureBuiltinMod(tx, ed.addKey));
      rules::setModifiers(tx, rules::ModTarget::Province, pid, ids);
    });
  else if (ed.termOf)
    a.act("Срок модификатора", [&](Tx& tx) { rules::setModTurns(tx, rules::ModTarget::Province, pid, ed.termOf, ed.turns); },
          {.coalesce = "province.modturns:" + std::to_string(pid) + ":" + std::to_string(ed.termOf)});
  a.markUi("province.modAdd");
  if (ro && ids.empty()) ui::label("Нет модификаторов.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
}

// Автоматические модификаторы провинции («Столица государства»): только чтение, причина в подсказке.
void autoSection(App& a, const World& wd, Id pid) {
  const std::vector<rules::AutoMod> autos = rules::autoProvinceModifiers(wd, pid);
  if (autos.empty()) return;
  ui::Section s("Автоматические", "lock", {.badge = std::to_string(autos.size())});
  a.markUi("province.autoMods");
  if (!s) return;
  std::vector<prov::ChipSpec> chips;
  for (const rules::AutoMod& am : autos) {
    prov::ChipSpec c;
    c.label = am.m ? modTitle(*am.m) : std::string("Модификатор");
    c.icon = am.m && !am.m->icon.empty() ? am.m->icon.c_str() : "sparkles";
    c.color = am.m ? am.m->color : Color(0, 0, 0, 0);
    c.tooltip = am.why;
    if (am.m)
      if (std::string mt = modTip(*am.m); !mt.empty()) c.tooltip += "\n" + mt;
    c.clickable = am.modifier != 0;
    chips.push_back(std::move(c));
  }
  RectF at = ui::avail();
  int k = prov::flowChips("auto", chips);
  RectF area{at.x, at.y, at.w, std::max(0.f, ui::avail().y - at.y - ui::theme().gap)};
  for (const rules::AutoMod& am : autos) a.markUi("province.autoMod." + (am.key.empty() ? std::to_string(am.modifier) : am.key), area);
  if (k >= 0 && autos[size_t(k)].modifier) a.openEditor("modifiers", autos[size_t(k)].modifier);
}

// Карточка источника: значок вида, название (щелчок — открыть), модификатор и его эффекты.
void sourceCard(App& a, const World& wd, const rules::EffectSource& s, int idx) {
  const Modifier* m = prov::sourceMod(wd, s);
  if (!m) return;
  ui::IdScope sc{idx};
  ui::Card card({.pad = 10});
  {
    ui::Row r({ui::px(22), ui::fr(1), ui::fr(0.8f)}, 24, 6);
    ui::icon(prov::sourceIcon(s.kind), ui::Ink::Dim, 16, prov::sourceKind(s.kind));
    std::string name = prov::sourceName(wd, s);
    bool canOpen = s.kind == rules::EffectSource::Faction || s.kind == rules::EffectSource::Guild;
    if (canOpen) {
      if (ui::link(name)) a.select(SelType::Faction, s.id);
    } else {
      ui::label(name, {.font = ui::Font::Strong});
    }
    ui::label(prov::sourceKind(s.kind), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Right});
  }
  {
    std::vector<prov::ChipSpec> chips;
    prov::ChipSpec head;
    head.label = modTitle(*m);
    head.icon = m->icon.empty() ? "sparkles" : m->icon.c_str();
    head.clickable = m->id != 0;
    head.tooltip = m->desc.empty() ? std::string("Открыть в редакторе модификаторов") : m->desc;
    chips.push_back(head);
    for (auto& c : localChips(*m)) chips.push_back(std::move(c));
    if (prov::flowChips("chips", chips) == 0 && m->id) a.openEditor("modifiers", m->id);
  }
}

void drawModifiers(App& a, Id pid) {
  const World& wd = prov::frameWorld(a);
  const Province* p = wd.province(pid);
  if (!p) return;
  ui::IdScope ps{i64(pid)};
  bool ro = a.readOnly();
  auto calc = rules::calc(wd);
  rules::ProvinceCalc none;
  const rules::ProvinceCalc* pcp = calc->province(pid);
  const rules::ProvinceCalc& pc = pcp ? *pcp : none;

  {
    std::string badge = p->modifiers.empty() ? std::string() : fmtNum(double(p->modifiers.size()));
    ui::Section s("Модификаторы провинции", "sparkles", {.badge = badge});
    a.markUi("province.mods");
    if (s) modifierRows(a, *p, ro);
  }
  autoSection(a, wd, pid);

  // Итог всех локальных эффектов (провинция + государство + технологии + постройки + гильдии + автоматические).
  if (ui::Section s("Действующие эффекты", "bolt"); s) {
    std::vector<prov::ChipSpec> chips;
    for (int f = 0; f < kFxCount; f++) {
      if (!schema::kEffects[f].local) continue;
      double v = pc.fx.v[size_t(f)];
      if (std::fabs(v) < 1e-9) continue;
      prov::ChipSpec c;
      c.label = w::effectText(Fx(f), v);
      c.icon = schema::kEffects[f].icon;
      c.tone = w::effectGood(Fx(f), v) ? ui::Tone::Success : ui::Tone::Danger;
      c.tooltip = schema::kEffects[f].name;
      chips.push_back(std::move(c));
    }
    if (chips.empty()) ui::label("Эффектов нет.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    else prov::flowChips("total", chips);
    a.markUi("province.effects");
  }

  if (!pc.fx.sources.empty()) {
    std::string badge = fmtNum(double(pc.fx.sources.size()));
    if (ui::Section s("Источники", "layers", {.badge = badge}); s) {
      // По видам источников: провинция, государство, технологии, постройки, гильдии, автоматические.
      std::vector<int> order(pc.fx.sources.size());
      for (size_t i = 0; i < order.size(); i++) order[i] = int(i);
      std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return pc.fx.sources[size_t(x)].kind < pc.fx.sources[size_t(y)].kind; });
      for (int i : order) sourceCard(a, wd, pc.fx.sources[size_t(i)], i);
    }
  }
}

TabReg tabModifiers({"province.modifiers", "sparkles", "Модификаторы", 60, SelType::Province, landOnly, drawModifiers, modBadge});

}  // namespace
}  // namespace rg::app
