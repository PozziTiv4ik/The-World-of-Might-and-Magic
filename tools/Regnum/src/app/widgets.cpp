// Regnum — общие виджеты предметной области (см. widgets.h).
#include "app/widgets.h"

#include "base/jobs.h"
#include "codec/jpeg.h"
#include "gfx/icons.h"

#include <algorithm>
#include <future>
#include <map>
#include <unordered_map>

namespace rg::app {
namespace edkit {   // поток фишек с переносом строк (editors/modifiers.cpp)
void chipsBegin();
ui::ChipAction chip(std::string_view label, const ui::ChipOpt& o);
void chipsEnd();
}  // namespace edkit
}  // namespace rg::app

namespace rg::app::w {

namespace {

// Запись справочника, созданная через «Добавить…»: выбирается при следующем вызове того же виджета.
std::map<ui::WidgetId, Id>& pendingCreated() {
  static std::map<ui::WidgetId, Id> m;
  return m;
}

// Хранилище подписей на время вызова виджета (ui::Option ссылается на строки).
struct Options {
  std::vector<std::string> labels, hints;
  std::vector<ui::Option> opts;
  std::vector<Id> ids;
  void reserve(size_t n) { labels.reserve(n); hints.reserve(n); opts.reserve(n); ids.reserve(n); }
  void add(Id id, std::string label, const char* icon, Color color, std::string hint = {}, bool disabled = false) {
    ids.push_back(id);
    labels.push_back(std::move(label));
    hints.push_back(std::move(hint));
    ui::Option o;
    o.icon = icon;
    o.color = color;
    o.disabled = disabled;
    opts.push_back(o);
  }
  // Строки переезжают при росте векторов — ссылки проставляются в конце.
  std::span<const ui::Option> finish() {
    for (size_t i = 0; i < opts.size(); i++) { opts[i].label = labels[i]; opts[i].hint = hints[i]; }
    return opts;
  }
  int indexOf(Id id) const {
    if (id == 0) return -1;
    for (size_t i = 0; i < ids.size(); i++) if (ids[i] == id) return int(i);
    return -1;
  }
};

std::string orUnnamed(const std::string& s, const char* fallback) { return s.empty() ? std::string(fallback) : s; }

constexpr Id kCreate = 0xFFFFFFFFu;

}  // namespace

Color factionColor(const World& w, Id faction) {
  const Faction* f = w.faction(faction);
  return f ? f->color : Color::hex(0x8a8f99);
}

bool factionPicker(std::string_view id, Id& value, FactionFilter filter, std::string_view noneLabel, Id exclude, bool disabled) {
  const World& w = app().world();
  std::vector<const Faction*> list;
  w.factions.each([&](const Faction& f) {
    if (f.id == exclude) return;
    if (f.isWild() && f.id != value) return;   // «Без государства» не выбирается
    if (filter == FactionFilter::States && !f.isState()) return;
    if (filter == FactionFilter::Guilds && !f.isGuild()) return;
    list.push_back(&f);
  });
  std::sort(list.begin(), list.end(), [](const Faction* a, const Faction* b) {
    if (a->kind != b->kind) return a->kind < b->kind;
    return compareRu(a->name, b->name) < 0;
  });
  Options o;
  o.reserve(list.size());
  for (const Faction* f : list) o.add(f->id, orUnnamed(f->name, "Без названия"), nullptr, f->color, f->isGuild() ? "гильдия" : "");
  int idx = o.indexOf(value);
  ui::ComboOpt co;
  co.noneLabel = noneLabel;
  co.disabled = disabled || app().readOnly();
  co.icon = filter == FactionFilter::Guilds ? "guild" : "crown";
  co.placeholder = noneLabel.empty() ? std::string_view("—") : noneLabel;
  if (!ui::combo(id, idx, o.finish(), co)) return false;
  Id nv = idx >= 0 && idx < int(o.ids.size()) ? o.ids[size_t(idx)] : 0;
  if (nv == value) return false;
  value = nv;
  return true;
}

bool provincePicker(std::string_view id, Id& value, Id owner, std::string_view noneLabel, bool disabled) {
  const World& w = app().world();
  std::vector<const Province*> list;
  w.provinces.each([&](const Province& p) {
    if (p.sea) return;
    if (owner && p.owner != owner) return;
    list.push_back(&p);
  });
  std::sort(list.begin(), list.end(), [](const Province* a, const Province* b) { return compareRu(a->name, b->name) < 0; });
  Options o;
  o.reserve(list.size());
  for (const Province* p : list) o.add(p->id, orUnnamed(p->name, "Без названия"), "province", factionColor(w, p->owner), p->capital);
  int idx = o.indexOf(value);
  ui::ComboOpt co;
  co.noneLabel = noneLabel;
  co.disabled = disabled || app().readOnly();
  co.icon = "province";
  if (!ui::combo(id, idx, o.finish(), co)) return false;
  Id nv = idx >= 0 && idx < int(o.ids.size()) ? o.ids[size_t(idx)] : 0;
  if (nv == value) return false;
  value = nv;
  return true;
}

bool characterPicker(std::string_view id, Id& value, Id faction, std::string_view noneLabel, bool allowCreate, bool disabled) {
  App& a = app();
  const World& w = a.world();
  std::vector<const Character*> list;
  // Назначать можно только доступных героев своей фракции (ТЗ «Фиксы», п.9; «Модификаторы», 1.6 и 1.12).
  w.characters.each([&](const Character& c) {
    if (faction && (c.faction != faction || !rules::heroAvailable(w, c.id))) return;
    list.push_back(&c);
  });
  std::sort(list.begin(), list.end(), [](const Character* x, const Character* y) { return compareRu(x->name, y->name) < 0; });
  Options o;
  o.reserve(list.size() + 2);
  // Уже назначенный, но недоступный (чужой, мёртвый, пленный) — виден в поле, выбрать его снова нельзя.
  if (const Character* cur = w.character(value); faction && cur && std::find(list.begin(), list.end(), cur) == list.end()) {
    std::string hint = rules::characterHas(w, cur->id, schema::mod::Dead)      ? std::string("мёртв")
                       : rules::characterHas(w, cur->id, schema::mod::Captive) ? std::string("в плену")
                                                                               : w.factionName(cur->faction);
    o.add(cur->id, orUnnamed(cur->name, "Без имени"), "character", cur->faction ? factionColor(w, cur->faction) : Color(0, 0, 0, 0), hint, true);
  }
  for (const Character* c : list) {
    std::string hint = c->title;
    if (c->faction && c->faction != faction) {
      if (!hint.empty()) hint += " · ";
      hint += w.factionName(c->faction);
    }
    o.add(c->id, orUnnamed(c->name, "Без имени"), c->hero ? "hero" : "character", c->faction ? factionColor(w, c->faction) : Color(0, 0, 0, 0), hint);
  }
  if (allowCreate) o.add(kCreate, "Новый персонаж", "user-plus", Color(0, 0, 0, 0));
  int idx = o.indexOf(value);
  ui::ComboOpt co;
  co.noneLabel = noneLabel;
  co.disabled = disabled || a.readOnly();
  co.icon = "character";
  if (!ui::combo(id, idx, o.finish(), co)) return false;
  Id nv = idx >= 0 && idx < int(o.ids.size()) ? o.ids[size_t(idx)] : 0;
  if (nv == kCreate) {
    Id created = 0;
    if (!a.act("Новый персонаж", [&](Tx& tx) { created = rules::createCharacter(tx, faction, "Новый персонаж"); })) return false;
    nv = created;
  }
  if (nv == value) return false;
  value = nv;
  return true;
}

bool catalogPicker(std::string_view id, rules::CatalogList list, Id& value, std::string_view noneLabel, bool allowCreate, bool disabled) {
  App& a = app();
  const World& w = a.world();
  const auto& items = rules::catalogList(*w.catalogs, list);
  Options o;
  o.reserve(items.size() + 1);
  const char* defIcon = nullptr;
  switch (list) {
    case rules::CatalogList::Resources: defIcon = "resource"; break;
    case rules::CatalogList::Races: defIcon = "race"; break;
    case rules::CatalogList::Cultures: defIcon = "culture"; break;
    case rules::CatalogList::Religions: defIcon = "religion"; break;
    case rules::CatalogList::Governments: defIcon = "crown"; break;
    case rules::CatalogList::Positions: defIcon = "council"; break;
    case rules::CatalogList::Essences: defIcon = "essence"; break;
  }
  bool colored = list != rules::CatalogList::Governments && list != rules::CatalogList::Positions;
  for (const auto& c : items) {
    const char* icon = !c.icon.empty() && gfx::hasIcon(c.icon) ? c.icon.c_str() : (colored ? nullptr : defIcon);
    o.add(c.id, orUnnamed(c.name, "Без названия"), icon, colored ? c.color : Color(0, 0, 0, 0));
  }
  if (allowCreate) o.add(kCreate, "Добавить…", "plus", Color(0, 0, 0, 0));
  const ui::WidgetId wid = ui::id(id);
  if (auto it = pendingCreated().find(wid); it != pendingCreated().end()) {
    Id created = it->second;
    pendingCreated().erase(it);
    if (created && created != value && Catalogs::find(items, created)) {
      value = created;
      return true;
    }
  }
  int idx = o.indexOf(value);
  ui::ComboOpt co;
  co.noneLabel = noneLabel;
  co.disabled = disabled || a.readOnly();
  co.icon = defIcon;
  if (!ui::combo(id, idx, o.finish(), co)) return false;
  Id nv = idx >= 0 && idx < int(o.ids.size()) ? o.ids[size_t(idx)] : 0;
  if (nv == kCreate) {
    // Создание — асинхронно (запрос названия); текущее значение не меняется в этом кадре.
    a.prompt("Новая запись справочника", "Название", "", [list, wid](App& app2, const std::string& name) {
      Id created = 0;
      if (app2.act("Добавить в справочник", [&](Tx& tx) { created = rules::addCatalogItem(tx, list, name); }))
        pendingCreated()[wid] = created;   // выбирается в следующем кадре вызовом того же виджета
    });
    return false;
  }
  if (nv == value) return false;
  value = nv;
  return true;
}

bool modifierInert(const Modifier& m, ModScope where) {
  if (where == ModScope::Any || where == ModScope::Faction) return false;
  bool any = false, used = false;
  for (int f = 0; f < kFxCount; f++) {
    if (!m.has(Fx(f))) continue;
    any = true;
    if (where == ModScope::Local && schema::kEffects[f].local) used = true;
    if (where == ModScope::Army && schema::kEffects[f].army) used = true;
    if (where == ModScope::ArchGroup && schema::kEffects[f].arch) used = true;
  }
  return any && !used;   // у героя эффекты не действуют вовсе
}

bool modifierFits(const Modifier& m, ModScope where) {
  if (!m.key.empty() && schema::isAutoKey(m.key)) return false;   // ставятся и снимаются сами
  if (where == ModScope::ArchGroup) return m.kind == ModKind::ArchGroup;   // группам — только модификаторы групп
  if (m.kind == ModKind::Any) return true;
  switch (where) {
    case ModScope::Any: return m.kind == ModKind::Province || m.kind == ModKind::Faction;
    case ModScope::Local: return m.kind == ModKind::Province;
    case ModScope::Faction: return m.kind == ModKind::Faction;
    case ModScope::Army: return m.kind == ModKind::Army;
    case ModScope::Hero: return m.kind == ModKind::Hero;
    case ModScope::ArchGroup: return false;
  }
  return false;
}

namespace {

// Подсказка фишки модификатора, который здесь не действует.
const char* inertTip(ModScope where) {
  switch (where) {
    case ModScope::Local:
      return "Не действует в провинции: у модификатора только глобальные эффекты, они применяются к государству. "
             "Добавьте его государству (вкладка «Модификаторы»).";
    case ModScope::Army: return "Не действует на войско: у модификатора нет эффектов войск.";
    case ModScope::Hero: return "Эффекты модификатора на героя не действуют.";
    case ModScope::ArchGroup: return "Не действует на группу: у модификатора нет эффектов археологических групп.";
    default: return "";
  }
}

}  // namespace

bool modifierList(std::string_view id, std::vector<Id>& ids, bool disabled, ModScope where, const ModTurns* turns, ModEdit* edit) {
  App& a = app();
  const World& w = a.world();
  const std::string mark(id);
  ui::IdScope scope(id);
  bool changed = false;
  disabled = disabled || a.readOnly();
  bool anyChip = false;
  for (Id mid : ids) if (w.modifier(mid)) anyChip = true;
  if (anyChip) {
    edkit::chipsBegin();
    for (size_t i = 0; i < ids.size(); i++) {
      const Modifier* m = w.modifier(ids[i]);
      if (!m) continue;
      ui::IdScope s2{i64(ids[i])};
      ui::ChipOpt co;
      co.icon = m->icon.empty() || !gfx::hasIcon(m->icon) ? "sparkles" : m->icon.c_str();
      co.color = m->color;
      co.removable = !disabled;
      co.clickable = true;
      std::string tip;
      for (int f = 0; f < kFxCount; f++) {
        if (!m->has(Fx(f))) continue;
        if (!tip.empty()) tip += "\n";
        tip += effectText(Fx(f), m->fx[size_t(f)]);
      }
      if (!m->desc.empty()) tip = m->desc + (tip.empty() ? "" : "\n" + tip);
      int left = 0;
      if (turns)
        if (auto it = turns->find(m->id); it != turns->end()) left = it->second;
      if (left > 0) tip = "Осталось: " + nTurns(left) + (tip.empty() ? "" : "\n" + tip);
      // Модификатор, эффекты которого здесь ни на что не влияют, — предупреждение на фишке.
      const bool inert = modifierInert(*m, where);
      if (inert) {
        co.icon = "warning";
        co.color = Color(0, 0, 0, 0);
        co.tone = ui::Tone::Warning;
        tip = std::string(inertTip(where)) + "\n" + tip;
      }
      co.tooltip = tip;
      const std::string label = orUnnamed(m->name, "Модификатор") + (left > 0 ? " · " + std::to_string(left) : std::string());
      ui::ChipAction act = edkit::chip(label, co);
      a.markUi(mark + ".chip." + std::to_string(i));
      if (inert) a.markUi(mark + ".warn." + std::to_string(m->id));
      if (act == ui::ChipAction::Remove) {
        ids.erase(ids.begin() + long(i));
        changed = true;
        break;
      }
      if (act == ui::ChipAction::Click) {
        if (turns) ui::openPopup("term");   // срок и переход в редактор
        else a.openEditor("modifiers", m->id);
      }
      if (turns && ui::beginPopup("term", {.side = ui::Side::Below, .width = 280})) {
        ui::label(orUnnamed(m->name, "Модификатор"), {.font = ui::Font::Strong, .icon = co.icon});
        ui::prop("Срок", "hourglass");
        int n = left;
        if (ui::numberField("turns", n, {.min = 0, .max = 1000, .unit = "ход|хода|ходов", .steppers = true, .disabled = disabled || !edit,
                                         .tooltip = "Ходов действия; 0 — бессрочно"}) &&
            edit) {
          edit->termOf = m->id;
          edit->turns = std::max(0, n);
        }
        a.markUi(mark + ".term");
        if (ui::button("Открыть в редакторе", {.icon = "sparkles", .fill = true})) {
          ui::closePopup();
          a.openEditor("modifiers", m->id);
        }
        ui::endPopup();
      }
    }
    edkit::chipsEnd();
  }
  if (!disabled) {
    Options o;
    int hidden = 0;   // не действующие здесь (в провинции — только с глобальными эффектами) не предлагаются
    w.modifiers.each([&](const Modifier& m) {
      if (std::find(ids.begin(), ids.end(), m.id) != ids.end()) return;
      if (!modifierFits(m, where)) return;
      if (modifierInert(m, where)) {
        hidden++;
        return;
      }
      o.add(m.id, orUnnamed(m.name, "Модификатор"), m.icon.empty() || !gfx::hasIcon(m.icon) ? "sparkles" : m.icon.c_str(), m.color);
    });
    // Встроенные модификаторы, которых ещё нет в мире (запись создаёт вызывающий — edit->addKey).
    constexpr Id kTpl = 0xF0000000u;
    if (edit) {
      const auto& tpl = schema::builtinModifiers();
      for (size_t k = 0; k < tpl.size(); k++) {
        const Modifier& t = tpl[k];
        if (rules::builtinModId(w, t.key) || !modifierFits(t, where) || modifierInert(t, where)) continue;
        o.add(kTpl + Id(k), t.name, t.icon.empty() || !gfx::hasIcon(t.icon) ? "sparkles" : t.icon.c_str(), t.color,
              t.duration > 0 ? nTurns(t.duration) : std::string());
      }
    }
    // По алфавиту (записи мира и шаблоны вместе).
    {
      std::vector<size_t> order(o.ids.size());
      for (size_t k = 0; k < order.size(); k++) order[k] = k;
      std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return compareRu(o.labels[x], o.labels[y]) < 0; });
      Options s;
      s.reserve(order.size());
      for (size_t k : order) s.add(o.ids[k], o.labels[k], o.opts[k].icon, o.opts[k].color, o.hints[k], o.opts[k].disabled);
      o = std::move(s);
    }
    const std::string hiddenTip = where == ModScope::Local
                                      ? "Модификаторы только с глобальными эффектами не предлагаются: в провинции они не действуют (скрыто: " +
                                            std::to_string(hidden) + ")"
                                      : "Модификаторы, эффекты которых здесь не действуют, не предлагаются (скрыто: " + std::to_string(hidden) + ")";
    if (!o.ids.empty()) {
      int idx = -1;
      ui::ComboOpt co;
      co.placeholder = "Добавить модификатор";
      co.icon = "plus";
      co.search = 1;   // поиск по названию при любой длине списка
      if (hidden > 0) co.tooltip = hiddenTip;
      if (ui::combo("add", idx, o.finish(), co) && idx >= 0 && idx < int(o.ids.size())) {
        const Id pick = o.ids[size_t(idx)];
        if (pick >= kTpl && edit) {
          edit->addKey = schema::builtinModifiers()[size_t(pick - kTpl)].key;
        } else {
          ids.push_back(pick);
        }
        changed = true;
      }
    } else if (w.modifiers.empty()) {
      if (ui::link("Создать модификатор", "sparkles")) a.openEditor("modifiers", 0);
    }
  }
  return changed;
}

std::string heroState(const World& w, const Character& c) {
  if (rules::hasModKey(w, c.modifiers, schema::mod::Dead)) return c.burial ? "Мертв · " + w.provinceName(c.burial) : std::string("Мертв");
  if (rules::hasModKey(w, c.modifiers, schema::mod::Captive)) return c.captor ? "В плену · " + w.factionName(c.captor) : std::string("В плену");
  return {};
}

void factionChip(Id faction, bool showKind) {
  App& a = app();
  const World& w = a.world();
  const Faction* f = w.faction(faction);
  ui::IdScope s(i64(faction) + 0x10000000LL);
  if (!f) {
    ui::chip("—", {});
    return;
  }
  ui::ChipOpt co;
  co.color = f->color;
  co.clickable = !f->isWild();   // у войск без государства нет страницы
  co.icon = showKind ? (f->isGuild() ? "guild" : f->isWild() ? "skull" : "crown") : nullptr;
  co.tooltip = f->isWild() ? "Войска без государства: враждебны всем государствам"
               : f->isGuild() ? "Торговая гильдия — открыть" : "Государство — открыть";
  if (ui::chip(orUnnamed(f->name, "Без названия"), co) == ui::ChipAction::Click) a.select(SelType::Faction, faction);
}

void provinceChip(Id province) {
  App& a = app();
  const World& w = a.world();
  const Province* p = w.province(province);
  ui::IdScope s(i64(province) + 0x20000000LL);
  if (!p) {
    ui::chip("—", {});
    return;
  }
  ui::ChipOpt co;
  co.icon = p->sea ? "sea" : "province";
  co.color = p->owner ? factionColor(w, p->owner) : Color(0, 0, 0, 0);
  co.clickable = true;
  co.tooltip = "Провинция — открыть и показать на карте";
  if (ui::chip(orUnnamed(p->name, "Без названия"), co) == ui::ChipAction::Click) a.select(SelType::Province, province, true);
}

void characterChip(Id character) {
  App& a = app();
  const World& w = a.world();
  const Character* c = w.character(character);
  ui::IdScope s(i64(character) + 0x30000000LL);
  if (!c) {
    ui::chip("—", {});
    return;
  }
  ui::ChipOpt co;
  co.icon = c->hero ? "hero" : "character";
  co.color = c->faction ? factionColor(w, c->faction) : Color(0, 0, 0, 0);
  co.clickable = true;
  co.tooltip = c->title.empty() ? std::string_view("Персонаж — открыть") : std::string_view(c->title);
  if (ui::chip(orUnnamed(c->name, "Без имени"), co) == ui::ChipAction::Click) a.select(SelType::Character, character);
}

std::string effectText(Fx f, double v) {
  const auto& e = schema::effect(f);
  std::string num = fmtSigned(v, std::fabs(v - std::round(v)) > 1e-9 ? 1 : 0);
  if (e.unit[0] == '%') num += "\xC2\xA0%";
  switch (f) {
    case Fx::PopGrowthPct: return num + " прироста населения за ход";
    case Fx::TradePct: return num + " торговой ценности";
    case Fx::TradeFlat: return num + " к торговой ценности";
    case Fx::BuildCostPct: return num + " к стоимости строительства";
    case Fx::ContentmentPerTurn: return num + " довольства за ход";
    case Fx::RebellionPct: return num + " к вероятности восстания";
    case Fx::ResourcePct: return num + " добычи ресурса";
    case Fx::ResourceFlat: return num + " к добыче ресурса";
    case Fx::Slots: return num + " " + plural(i64(std::fabs(v)), "слот", "слота", "слотов") + " построек";
    case Fx::IncomePct: return num + " дохода в казну";
    case Fx::DiplomacyPerTurn: return num + " к отношениям за ход";
    case Fx::ArmyUpkeepPct: return num + " содержания войск";
    case Fx::FleetUpkeepPct: return num + " содержания флота";
    case Fx::ResearchTimePct: return num + " времени исследования технологий";
    case Fx::LoyaltyPerTurn: return num + " верности войск за ход";
    case Fx::ArchSuccessPct: return num + " к шансу успеха археологических групп";
    case Fx::ArchVitalityPct: return num + " к живучести археологических групп";
    case Fx::ArchExpPct: return num + " опыта археологических групп";
    case Fx::ArchUpkeepPct: return num + " содержания археологических групп";
    case Fx::ArchTreasurePct: return num + " археологических сокровищ";
    case Fx::ArchDiscoveryPct: return num + " к шансу обнаружения археологического места";
    case Fx::ArchStartLevel: return num + " к начальному уровню археологических групп";
    default: return num;
  }
}

bool effectGood(Fx f, double v) {
  bool bad = f == Fx::BuildCostPct || f == Fx::RebellionPct || f == Fx::ArmyUpkeepPct || f == Fx::FleetUpkeepPct || f == Fx::ResearchTimePct ||
             f == Fx::ArchUpkeepPct;
  return bad ? v < 0 : v > 0;
}

void effectChips(const Modifier& m) {
  ui::IdScope s(i64(m.id) + 0x40000000LL);
  bool any = false;
  for (int f = 0; f < kFxCount; f++) if (m.has(Fx(f)) && m.fx[size_t(f)] != 0) any = true;
  if (!any) {
    ui::label("Без эффектов", {.ink = ui::Ink::Muted});
    return;
  }
  edkit::chipsBegin();
  for (int f = 0; f < kFxCount; f++) {
    if (!m.has(Fx(f))) continue;
    double v = m.fx[size_t(f)];
    if (v == 0) continue;
    ui::IdScope s2(f);
    ui::ChipOpt co;
    co.icon = schema::effect(Fx(f)).icon;
    co.tone = effectGood(Fx(f), v) ? ui::Tone::Success : ui::Tone::Danger;
    edkit::chip(effectText(Fx(f), v), co);
  }
  edkit::chipsEnd();
}

const char* resourceIcon(const World& w, Id res) {
  if (res == kGold) return "coins";
  const CatalogItem* c = w.resource(res);
  if (c && !c->icon.empty() && gfx::hasIcon(c->icon)) return c->icon.c_str();
  return "resource";
}

Color resourceColor(const World& w, Id res) {
  const CatalogItem* c = w.resource(res);
  return c ? c->color : Color::hex(0x9aa0a8);
}

// ---------------------------------------------------------------- группы ресурсов, эссенции, реликвии, портреты
namespace {
// Группы в порядке дерева (родитель, затем его подгруппы) с глубиной.
void groupTree(const World& w, Id parent, int depth, std::vector<std::pair<Id, int>>& out) {
  for (Id g : rules::childGroups(w, parent)) {
    out.push_back({g, depth});
    if (depth < 16) groupTree(w, g, depth + 1, out);
  }
}
}  // namespace

std::vector<std::pair<Id, int>> groupOrder(const World& w) {
  std::vector<std::pair<Id, int>> out;
  groupTree(w, 0, 0, out);
  return out;
}

bool resGroupPicker(std::string_view id, Id& group, std::string_view noneLabel, bool disabled) {
  const World& w = app().world();
  Options o;
  for (auto [g, depth] : groupOrder(w)) {
    const ResGroup* x = w.catalogs->group(g);
    std::string pad;
    for (int i = 0; i < depth; i++) pad += "    ";
    o.add(g, pad + orUnnamed(x ? x->name : std::string(), "Без названия"), depth ? nullptr : "folder", Color(0, 0, 0, 0),
          std::to_string(rules::resourcesIn(w, g).size()));
  }
  int idx = o.indexOf(group);
  ui::ComboOpt co;
  co.noneLabel = noneLabel;
  co.placeholder = noneLabel.empty() ? std::string_view("Группа") : noneLabel;
  co.disabled = disabled;
  co.icon = "folder";
  co.tooltip = "Группа ресурсов";
  if (!ui::combo(id, idx, o.finish(), co)) return false;
  const Id nv = idx >= 0 && idx < int(o.ids.size()) ? o.ids[size_t(idx)] : 0;
  if (nv == group) return false;
  group = nv;
  return true;
}

bool resourceFrom(std::string_view id, Id& value, const std::vector<Id>& ids, std::string_view placeholder, bool disabled,
                  std::string_view tooltip, std::string_view noneLabel) {
  const World& w = app().world();
  Options o;
  o.reserve(ids.size());
  for (Id r : ids) {
    const CatalogItem* c = w.resource(r);
    if (!c) continue;
    // Справа — группа ресурса (без пути: путь виден в справочнике и в подсказке поля).
    const ResGroup* g = w.catalogs->group(c->group);
    o.add(r, orUnnamed(c->name, "Без названия"), resourceIcon(w, r), Color(0, 0, 0, 0), g ? orUnnamed(g->name, "Без названия") : std::string());
  }
  int idx = o.indexOf(value);
  ui::ComboOpt co;
  co.placeholder = placeholder;
  co.noneLabel = noneLabel;
  co.disabled = disabled;
  co.icon = "resource";
  co.tooltip = tooltip;
  co.popupWidth = 340;
  co.search = 1;   // поиск по названию в любом списке (Enter выбирает найденное)
  if (!ui::combo(id, idx, o.finish(), co)) return false;
  const Id nv = idx >= 0 && idx < int(o.ids.size()) ? o.ids[size_t(idx)] : 0;
  if (nv == value) return false;
  value = nv;
  return true;
}

bool resourceByGroup(std::string_view id, Id& value, bool disabled, const std::vector<Id>* only) {
  const World& w = app().world();
  struct St {
    Id group = 0;
    bool init = false;
  };
  ui::IdScope scope(id);
  St& st = ui::state<St>(ui::id("##resgroup"));
  if (!st.init) {
    st.init = true;
    if (const CatalogItem* c = w.resource(value)) st.group = c->group;
  }
  if (st.group && !w.catalogs->group(st.group)) st.group = 0;
  bool changed = false;
  ui::Row row({ui::fr(1, 120), ui::fr(1, 140)}, 30, 6);
  resGroupPicker("group", st.group, "Все ресурсы", disabled);
  std::vector<Id> list;
  for (Id r : rules::resourcesIn(w, st.group))
    if (!only || std::find(only->begin(), only->end(), r) != only->end()) list.push_back(r);
  if (value && std::find(list.begin(), list.end(), value) == list.end() && w.resource(value)) list.insert(list.begin(), value);
  changed = resourceFrom("res", value, list, "Ресурс", disabled, {});
  return changed;
}

bool essencePicker(std::string_view id, Id& value, std::string_view noneLabel, bool disabled) {
  const World& w = app().world();
  Options o;
  for (const CatalogItem& e : w.catalogs->essences) o.add(e.id, orUnnamed(e.name, "Без названия"), nullptr, e.color);
  int idx = o.indexOf(value);
  ui::ComboOpt co;
  co.noneLabel = noneLabel;
  co.placeholder = noneLabel.empty() ? std::string_view("Эссенция") : noneLabel;
  co.disabled = disabled;
  co.icon = "essence";
  co.tooltip = "Эссенция элемента";
  if (!ui::combo(id, idx, o.finish(), co)) return false;
  const Id nv = idx >= 0 && idx < int(o.ids.size()) ? o.ids[size_t(idx)] : 0;
  if (nv == value) return false;
  value = nv;
  return true;
}

Color essenceColor(const World& w, Id essence) {
  const CatalogItem* c = w.essence(essence);
  return c ? c->color : Color::hex(0x8b6bff);
}

Color rarityColor(Rarity r) { return Color::hex(schema::rarity(r).color); }

ui::ChipAction relicChip(const Relic& r, bool removable, std::string_view tooltip) {
  ui::ChipOpt co;
  co.icon = "relic";
  co.glow = rarityColor(r.rarity);   // подсветка редкости
  co.removable = removable;
  co.clickable = true;
  std::string tip = std::string(schema::rarity(r.rarity).name) + " реликвия" + (r.desc.empty() ? std::string() : "\n" + r.desc);
  if (!tooltip.empty()) tip = std::string(tooltip);
  co.tooltip = tip;
  ui::IdScope s{i64(r.id) + 0x7e000000LL};
  return edkit::chip(orUnnamed(r.name, "Без названия"), co);
}

// ---------------------------------------------------------------- значок реликвии с изображением
namespace {

struct RelicPic {
  const char* data = nullptr;   // тождество строки изображения (быстрая проверка без хеша)
  size_t size = 0;
  u64 hash = 0;
  std::future<std::shared_ptr<gfx::Image>> job;
  std::shared_ptr<gfx::Image> img;
  bool failed = false;
};
std::unordered_map<Id, RelicPic>& relicPics() {
  static std::unordered_map<Id, RelicPic> m;
  return m;
}

// Квадрат по середине изображения, 128 × 128.
std::shared_ptr<gfx::Image> decodeRelic(const std::string& bytes) {
  auto rgba = codec::decodeImage(bytes);
  if (!rgba || rgba->empty()) return nullptr;
  gfx::Image full = gfx::Image::fromRgba(rgba->rgba.data(), rgba->w, rgba->h);
  const int s = std::min(full.w, full.h);
  gfx::Image sq = full.cropped(gfx::RectI{(full.w - s) / 2, (full.h - s) / 2, s, s});
  const int side = std::min(128, s);
  return std::make_shared<gfx::Image>(sq.scaled(side, side));
}

std::string relicTip(const Relic& r) {
  std::string tip = orUnnamed(r.name, "Без названия") + "\n" + schema::rarity(r.rarity).name + " реликвия";
  if (!r.desc.empty()) tip += "\n" + r.desc;
  return tip;
}

}  // namespace

const gfx::Image* relicImage(const Relic& r) {
  auto& m = relicPics();
  if (r.image.empty()) {
    m.erase(r.id);
    return nullptr;
  }
  RelicPic& e = m[r.id];
  if (e.data != r.image.data() || e.size != r.image.size()) {
    const u64 h = hash64(r.image);
    if (h != e.hash || (!e.job.valid() && !e.img && !e.failed)) {
      e = RelicPic{};
      e.hash = h;
      e.job = jobs::submit([bytes = r.image] { return decodeRelic(bytes); });
    }
    e.data = r.image.data();
    e.size = r.image.size();
  }
  if (e.job.valid()) {
    if (e.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      e.img = e.job.get();
      e.failed = !e.img;
    } else {
      ui::requestRedraw();   // ещё декодируется — следующий кадр покажет изображение
    }
  }
  return e.img ? e.img.get() : nullptr;
}

void drawRelicIcon(const Relic& r, RectF rect, float radius) {
  const ui::Theme& th = ui::theme();
  const Color rc = rarityColor(r.rarity);
  if (radius <= 0) radius = std::max(5.f, std::round(rect.w * 0.22f));
  ui::draw::shadow(rect, radius, std::max(6.f, rect.w * 0.22f), rc.alpha(0.5f), 0, 1);
  if (const gfx::Image* img = relicImage(r)) {
    ui::draw::rect(rect, th.surface1, radius);
    ui::draw::image(*img, rect.inset(2), std::max(2.f, radius - 2));
    ui::draw::rectStroke(rect, rc, radius, rect.w >= 48 ? 2.f : 1.5f);
  } else {
    ui::draw::gradient(rect, rc.alpha(0.34f), rc.alpha(0.12f), radius);
    ui::draw::rectStroke(rect, rc.alpha(0.8f), radius, 1);
    const float pad = std::round(rect.w * 0.24f);
    ui::draw::icon("relic", rect.inset(pad), rc);
  }
}

bool relicIcon(const Relic& r, float size, std::string_view tooltip) {
  ui::IdScope s{i64(r.id) + 0x7e100000LL};
  const RectF rect = ui::next(size, size);
  drawRelicIcon(r, rect);
  const ui::Interaction it = ui::interact(ui::id("##relicicon"), rect);
  if (it.hovered) ui::setCursor(platform::Cursor::Hand);
  ui::hoverTip("##relictip", rect, tooltip.empty() ? relicTip(r) : std::string(tooltip));
  return it.clicked;
}

bool relicRow(const Relic& r, std::string_view sub, float right, RectF* rowOut) {
  const ui::Theme& th = ui::theme();
  ui::IdScope s{i64(r.id) + 0x7e200000LL};
  const RectF row = ui::next(0, 40);
  if (rowOut) *rowOut = row;
  const ui::Interaction it = ui::interact(ui::id("##relicrow"), row, ui::IfAllowOverlap);
  if (it.hovered) ui::draw::rect(row, th.hover, 8);
  const RectF ic{row.x + 4, row.cy() - 16, 32, 32};
  drawRelicIcon(r, ic, 8);
  const float x = ic.right() + 10, w = std::max(20.f, row.right() - right - x - 4);
  const float lh = ui::lineHeight(ui::Font::Strong), sh = ui::lineHeight(ui::Font::Small);
  const float y0 = std::round(row.cy() - (lh + sh) * 0.5f);
  const RectF nameR{x, y0, w, lh};
  ui::draw::text(orUnnamed(r.name, "Без названия"), nameR, ui::Font::Strong, rarityColor(r.rarity));
  ui::draw::text(sub.empty() ? std::string_view(schema::rarity(r.rarity).name) : sub, RectF{x, y0 + lh, w, sh}, ui::Font::Small, th.textMuted);
  ui::hoverTip("##relicrowtip", RectF{row.x, row.y, row.w - right, row.h}, relicTip(r));
  return it.clicked;
}

}  // namespace rg::app::w

namespace rg::app::chars {
const gfx::Image* portraitImage(const Character& c, bool square);   // panels/character_common.cpp
}

namespace rg::app::w {

const gfx::Image* faceImage(const Character& c) { return chars::portraitImage(c, true); }

void heroAvatar(const Character& c, float size, bool ring, std::string_view tooltip, Color initials) {
  ui::avatar(c.name.empty() ? std::string_view("?") : std::string_view(c.name),
             {.image = faceImage(c), .color = initials, .size = size, .ring = ring, .tooltip = tooltip});
}

void resourceAmount(Id res, double amount, ui::Ink ink) {
  const World& w = app().world();
  const CatalogItem* c = w.resource(res);
  ui::HStack row(0, ui::Align::Left, ui::sp::xs);
  ui::iconColored(resourceIcon(w, res), resourceColor(w, res), 16, c ? std::string_view(c->name) : std::string_view("Ресурс"));
  // Золото и ресурсы — до тысячных (лишние нули не пишутся); золото — в тысячах (ТЗ «Доработки №1», п.12).
  ui::label(res == kGold ? fmtGold(amount) : fmtNum(amount, 3), {.ink = ink});
}

}  // namespace rg::app::w
