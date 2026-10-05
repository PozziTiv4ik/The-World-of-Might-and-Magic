// Regnum — вкладка «Модификаторы» (ТЗ 1.b.iii, 1.g; ТЗ «Модификаторы»): список модификаторов фракции фишками с
// переносом строк (действуют во всех провинциях государства; у гильдии — в провинциях штабов; щелчок — срок и переход
// в редактор, крестик — убрать; предлагаются модификаторы «Везде» и глобальные), автоматические модификаторы — только
// чтение с причиной (совет: «Децентрализация», «Слабый контроль», «Централизованная власть»; «Голод»; должности) и
// сводка действующих эффектов: глобальные (доход, содержание войск и флота, время исследований, верность войск),
// дипломатия по целям, локальные во всех провинциях и источники (модификаторы, технологии, постройки, автоматические).
#include <tuple>

#include "app/panels/faction_common.h"
#include "gfx/icons.h"

namespace rg::app {
namespace {

using namespace fac;

const char* fxIcon(Fx f) {
  const char* ic = schema::effect(f).icon;
  return ic && gfx::hasIcon(ic) ? ic : "sparkles";
}
const char* modIcon(const Modifier* m) { return m && !m->icon.empty() && gfx::hasIcon(m->icon) ? m->icon.c_str() : "sparkles"; }

// Ряды фишек с переносом по ширине (ряд ui::HStack не переносит).
struct ChipItem {
  std::string label;
  const char* icon = nullptr;
  Color color{0, 0, 0, 0};
  ui::Tone tone = ui::Tone::Neutral;
  std::string tip;
  bool removable = false, clickable = false;
};

// Возвращает индекс фишки и действие (щелчок или крестик); {-1, None} — ничего.
// mark — префикс имён прямоугольников фишек для App::uiRect (тесты); пусто — не запоминать.
std::pair<int, ui::ChipAction> chipFlow(std::string_view id, const std::vector<ChipItem>& items, std::string_view mark = {}) {
  ui::IdScope scope(id);
  std::pair<int, ui::ChipAction> res{-1, ui::ChipAction::None};
  const float W = ui::avail().w, gap = 6;
  size_t i = 0;
  int line = 0;
  while (i < items.size()) {
    ui::IdScope ls(line++);
    ui::HStack hs(26, ui::Align::Left, gap);
    float x = 0;
    bool first = true;
    while (i < items.size()) {
      const ChipItem& c = items[i];
      // Ширина фишки — как в ui::chip: текст + поля, значок или точка, крестик.
      float w = ui::measure(c.label, ui::Font::Small) + 20 + ((c.icon || c.color.a) ? 16 : 0) + (c.removable ? 18 : 0);
      if (!first && x + w > W) break;
      ui::IdScope cs{int(i)};
      ui::ChipAction act = ui::chip(c.label, {.icon = c.icon, .color = c.color, .tone = c.tone, .removable = c.removable, .clickable = c.clickable,
                                              .tooltip = c.tip});
      if (act != ui::ChipAction::None) res = {int(i), act};
      if (!mark.empty()) app().markUi(std::string(mark) + std::to_string(i));
      x += w + gap;
      first = false;
      i++;
    }
  }
  return res;
}

std::string modName(const Modifier& m) { return m.name.empty() ? std::string("Модификатор") : m.name; }

std::string effectsTip(const Modifier& m) {
  std::string tip;
  for (int f = 0; f < kFxCount; f++)
    if (m.has(Fx(f))) tip += (tip.empty() ? "" : "\n") + w::effectText(Fx(f), m.fx[size_t(f)]);
  return tip;
}

// Модификатор источника: запись мира или, для автоматического без записи, шаблон по ключу.
const Modifier* sourceMod(const World& w, const rules::EffectSource& s) {
  if (const Modifier* m = w.modifier(s.modifier)) return m;
  return s.key.empty() ? nullptr : rules::builtinMod(w, s.key);
}

std::string sourceName(const World& w, const rules::EffectSource& s, Id self, const std::vector<rules::AutoMod>& autos) {
  switch (s.kind) {
    case rules::EffectSource::Province: return "провинция " + w.provinceName(s.id);
    case rules::EffectSource::Faction: return s.id == self ? std::string("фракция") : w.factionName(s.id);
    case rules::EffectSource::Tech: {
      const Tech* t = w.tech(s.id);
      return "технология «" + (t ? t->name : std::string("?")) + "»";
    }
    case rules::EffectSource::Building: {
      const Building* b = w.building(s.id);
      return "постройка «" + (b ? b->name : std::string("?")) + "»";
    }
    case rules::EffectSource::Guild: return "гильдия " + w.factionName(s.id);
    case rules::EffectSource::Army: {
      const Army* ar = w.army(s.id);
      return std::string(ar && ar->isFleet() ? "флот «" : "войско «") + (ar && !ar->name.empty() ? ar->name : std::string("?")) + "»";
    }
    case rules::EffectSource::Auto:
      for (const rules::AutoMod& am : autos)
        if ((s.modifier && am.modifier == s.modifier) || (!s.key.empty() && am.key == s.key)) return utf8::lower(am.why);
      return "автоматически";
  }
  return {};
}

const char* sourceIcon(rules::EffectSource::Kind k) {
  switch (k) {
    case rules::EffectSource::Province: return "province";
    case rules::EffectSource::Faction: return "crown";
    case rules::EffectSource::Tech: return "tech";
    case rules::EffectSource::Building: return "building";
    case rules::EffectSource::Guild: return "guild";
    case rules::EffectSource::Army: return "army";
    case rules::EffectSource::Auto: return "lock";
  }
  return "sparkles";
}

// Автоматические модификаторы (совет, голод, должности): только чтение, причина в подсказке.
void autoSection(App& a, const std::vector<rules::AutoMod>& autos) {
  if (autos.empty()) return;
  ui::Section s("Автоматические", "lock", {.badge = std::to_string(autos.size())});
  a.markUi("mods.auto.section");
  if (!s) return;
  std::vector<ChipItem> chips;
  for (const rules::AutoMod& am : autos) {
    ChipItem c;
    c.label = am.m ? modName(*am.m) : std::string("Модификатор");
    c.icon = modIcon(am.m);
    c.color = am.m ? am.m->color : Color(0, 0, 0, 0);
    c.tip = am.why;
    if (am.m)
      if (std::string fx = effectsTip(*am.m); !fx.empty()) c.tip += "\n" + fx;
    c.clickable = am.modifier != 0;
    chips.push_back(std::move(c));
  }
  auto [k, act] = chipFlow("auto", chips, "mods.auto.");
  if (k >= 0 && act == ui::ChipAction::Click && autos[size_t(k)].modifier) a.openEditor("modifiers", autos[size_t(k)].modifier);
}

void drawModifiers(App& a, Id id) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(id);
  if (!f) return;
  const bool ro = a.readOnly();
  const bool state = f->isState();
  const std::vector<rules::AutoMod> autos = rules::autoModifiers(w, id);

  if (ui::Section s("Модификаторы", "sparkles", {.badge = f->modifiers.empty() ? std::string() : std::to_string(f->modifiers.size())}); s) {
    std::vector<Id> ids = f->modifiers;
    w::ModEdit ed;
    if (w::modifierList("mods", ids, ro, w::ModScope::Faction, &f->modTurns, &ed))
      a.act("Модификаторы фракции", [&](Tx& tx) {
        if (!ed.addKey.empty()) ids.push_back(rules::ensureBuiltinMod(tx, ed.addKey));
        rules::setModifiers(tx, rules::ModTarget::Faction, id, ids);
      });
    else if (ed.termOf)
      a.act("Срок модификатора", [&](Tx& tx) { rules::setModTurns(tx, rules::ModTarget::Faction, id, ed.termOf, ed.turns); },
            {.coalesce = "faction.modturns:" + std::to_string(id) + ":" + std::to_string(ed.termOf)});
    a.markUi("mods.add");
    if (ro && ids.empty()) ui::label("Нет модификаторов.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  autoSection(a, autos);

  rules::Effects fx = rules::factionEffects(w, id);
  if (ui::Section s("Действующие эффекты", "chart-bar"); s) {
    bool any = false;
    // Глобальные эффекты фракции (и эффекты всех её войск).
    std::vector<ChipItem> global;
    for (Fx g : {Fx::IncomePct, Fx::ArmyUpkeepPct, Fx::FleetUpkeepPct, Fx::ResearchTimePct, Fx::LoyaltyPerTurn}) {
      double v = fx[g];
      if (std::fabs(v) < 1e-9) continue;
      global.push_back({w::effectText(g, v), fxIcon(g), Color(0, 0, 0, 0), w::effectGood(g, v) ? ui::Tone::Success : ui::Tone::Danger, schema::effect(g).name});
    }
    if (!global.empty()) {
      ui::caption("Государство");
      chipFlow("global", global);
      any = true;
    }
    // Дипломатия: цели и изменение отношений за ход.
    std::vector<ChipItem> dip;
    for (auto& [target, v] : fx.diplomacy) {
      if (std::fabs(v) < 1e-9 || !w.faction(target)) continue;
      dip.push_back({w.factionName(target) + " · " + fmtSigned(v, std::fabs(v - std::round(v)) > 1e-9 ? 1 : 0) + " за ход", nullptr,
                     w::factionColor(w, target), v > 0 ? ui::Tone::Success : ui::Tone::Danger, "Изменение отношений каждый ход"});
    }
    if (!dip.empty()) {
      ui::caption("Дипломатия");
      chipFlow("dip", dip);
      any = true;
    }
    // Локальные эффекты (ТЗ 1.b.iii, 1.g, 1.h): модификаторы фракции, её изученных технологий и автоматические
    // действуют во всех её провинциях (у гильдии — в провинциях штабов), модификаторы построек — где постройка стоит.
    std::vector<rules::EffectSource> localSrc;
    std::map<std::pair<Id, Id>, int> buildingProvinces;   // (постройка, модификатор) -> число провинций
    auto hasLocal = [&](const Modifier& m) {
      for (int k = 0; k < kFxCount; k++)
        if (schema::kEffects[k].local && m.has(Fx(k)) && std::isfinite(m.fx[size_t(k)])) return true;
      return false;
    };
    std::array<double, kFxCount> local{};
    auto addLocal = [&](rules::EffectSource::Kind kind, Id src, Id mid, const Modifier* m, const std::string& key) {
      if (!m || !hasLocal(*m)) return;
      for (int k = 0; k < kFxCount; k++)
        if (schema::kEffects[k].local && m->has(Fx(k)) && std::isfinite(m->fx[size_t(k)])) local[size_t(k)] += m->fx[size_t(k)];
      localSrc.push_back({kind, src, mid, key});
    };
    for (Id mid : f->modifiers) addLocal(rules::EffectSource::Faction, id, mid, w.modifier(mid), {});
    w.techs.each([&](const Tech& t) {
      if (t.faction != id || !t.studied) return;
      for (Id mid : t.modifiers) addLocal(rules::EffectSource::Tech, t.id, mid, w.modifier(mid), {});
    });
    for (const rules::AutoMod& am : autos) addLocal(rules::EffectSource::Auto, id, am.modifier, am.m, am.key);
    // Постройки: эффекты одного экземпляра (не сумма по провинциям) и число провинций, где они действуют.
    std::map<std::pair<Id, Id>, std::array<double, kFxCount>> perBuilding;
    if (state)
      w.provinces.each([&](const Province& p) {
        if (p.sea || p.owner != id) return;
        for (const ProvBuilding& pb : p.buildings) {
          const Building* b = w.building(pb.building);
          int lvl = pb.builtLevel();
          if (!b || lvl < 1 || lvl > int(b->levels.size())) continue;
          for (Id mid : b->levels[size_t(lvl - 1)].modifiers) {
            const Modifier* m = w.modifier(mid);
            if (!m || !hasLocal(*m)) continue;
            int& n = buildingProvinces[{b->id, mid}];
            if (n++ == 0) {
              localSrc.push_back({rules::EffectSource::Building, b->id, mid, {}});
              auto& arr = perBuilding[{b->id, mid}];
              for (int k = 0; k < kFxCount; k++)
                if (schema::kEffects[k].local && m->has(Fx(k)) && std::isfinite(m->fx[size_t(k)])) arr[size_t(k)] = m->fx[size_t(k)];
            }
          }
        }
      });
    std::vector<ChipItem> loc;
    for (int k = 0; k < kFxCount; k++) {
      double v = local[size_t(k)];
      if (std::fabs(v) < 1e-9) continue;
      loc.push_back({w::effectText(Fx(k), v), fxIcon(Fx(k)), Color(0, 0, 0, 0), w::effectGood(Fx(k), v) ? ui::Tone::Success : ui::Tone::Danger, schema::kEffects[k].name});
    }
    if (!loc.empty()) {
      ui::caption(state ? "В каждой провинции" : "В провинциях штабов");
      chipFlow("local", loc);
      any = true;
    }
    std::vector<ChipItem> bld;
    for (auto& [key, arr] : perBuilding) {
      const Building* b = w.building(key.first);
      int n = buildingProvinces[key];
      for (int k = 0; k < kFxCount; k++) {
        double v = arr[size_t(k)];
        if (std::fabs(v) < 1e-9) continue;
        bld.push_back({w::effectText(Fx(k), v) + " · " + (b ? b->name : std::string("постройка")), fxIcon(Fx(k)), Color(0, 0, 0, 0),
                       w::effectGood(Fx(k), v) ? ui::Tone::Success : ui::Tone::Danger,
                       std::string(schema::kEffects[k].name) + " — в провинциях с постройкой: " + std::to_string(n)});
      }
    }
    if (!bld.empty()) {
      ui::caption("В провинциях с постройками");
      chipFlow("buildings", bld);
      any = true;
    }
    if (!any) ui::label("Действующих эффектов нет", {.ink = ui::Ink::Muted});
    // Источники: глобальные (расчёт правил) и локальные (собраны выше).
    std::vector<rules::EffectSource> sources = fx.sources;
    sources.insert(sources.end(), localSrc.begin(), localSrc.end());
    if (!sources.empty()) {
      ui::caption("Источники");
      std::vector<std::tuple<Id, Id, std::string>> seen;   // (модификатор, источник, ключ) без повторов
      int k = 0;
      for (const rules::EffectSource& src : sources) {
        std::tuple<Id, Id, std::string> key{src.modifier, src.id, src.key};
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        const Modifier* m = sourceMod(w, src);
        ui::IdScope sc(k++);
        ui::Row r({ui::px(18), ui::fr(1)}, 24, 8);
        ui::icon(sourceIcon(src.kind), ui::Ink::Muted, 15);
        std::string text = (m ? modName(*m) : std::string("Модификатор")) + " — " + sourceName(w, src, id, autos);
        if (src.kind == rules::EffectSource::Building)
          if (auto it = buildingProvinces.find({src.id, src.modifier}); it != buildingProvinces.end())
            text += " · провинций: " + std::to_string(it->second);
        ui::label(text, {.font = ui::Font::Small, .ink = ui::Ink::Dim});
        a.markUi("mods.source." + std::to_string(int(src.kind)) + "." + std::to_string(src.id));
      }
    }
  }
}

TabReg tab({kTabModifiers, "sparkles", "Модификаторы", 65, SelType::Faction, nullptr, drawModifiers});

}  // namespace
}  // namespace rg::app
