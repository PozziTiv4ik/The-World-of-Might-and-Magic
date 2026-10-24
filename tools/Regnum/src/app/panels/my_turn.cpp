// Regnum — раздел «Мой ход» правой ленты (аудит 2026-10-23, раздел 11): что ждёт игрока в этом ходу — предложения
// других государств (ответ — в разделе «Решения»), археологи в смертельной опасности, голод, долг казны, нехватка
// эссенций; войска и флот с очком действий, провинции, где можно начать стройку, простаивающие исследования и
// археологические группы, свободные очки талантов, войны. Каждая строка ведёт к объекту (войско и провинция — на карте,
// государство и герой — их вкладка, предложение — «Решения»). Игрок видит своё государство; администратор выбирает
// государство вверху страницы.
#include "app/app_internal.h"
#include "app/panels/military.h"
#include "app/shell/role.h"
#include "app/turn/turn_ui.h"
#include "app/widgets.h"

namespace rg::app {
namespace {

// Строка страницы: что показать и куда ведёт щелчок.
struct Item {
  enum Go : u8 { None, Army, Province, Faction, Character, Proposal } go = None;
  Id id = 0;
  const char* tab = nullptr;   // вкладка, которая откроется (провинция, государство, герой)
  const char* icon = nullptr;
  Color dot{0, 0, 0, 0};
  std::string text, sub, hint;
  std::string key;             // метка строки: «myturn.<группа>.<key>»
};

struct Group {
  const char* key;
  const char* title;
  const char* icon;
  std::vector<Item> items;
};

// Модель страницы — по версии мира, просматриваемому ходу и государству.
struct Model {
  u64 version = 0;
  int view = 0;
  Id state = 0;
  bool valid = false;
  std::vector<Group> groups;
};

std::string fmtAmount(double v) { return fmtNum(v, std::fabs(v - std::round(v)) < 1e-9 ? 0 : 3); }

Model build(const World& w, Id state) {
  Model m;
  m.state = state;
  const Faction* f = w.faction(state);
  if (!f) return m;
  auto c = rules::calc(w);
  const rules::FactionCalc* fc = c->faction(state);
  Group decide{"decide", "Ждут решения", "warning", {}};
  // Предложения других государств и вопросы вассалитета.
  for (Id pid : rules::proposalsTo(w, state)) {
    const Proposal* p = w.proposal(pid);
    if (!p) continue;
    Item it{Item::Proposal, pid, nullptr, "handshake"};
    it.key = "p" + std::to_string(pid);
    it.text = rules::proposalTitle(w, *p);
    it.sub = w.factionName(p->from) + " · ход " + std::to_string(p->turn);
    decide.items.push_back(std::move(it));
  }
  // Археологи в смертельной опасности.
  for (const ArchGroup& g : f->archGroups)
    if (g.danger) {
      Item it{Item::Faction, state, "faction.arch", "skull"};
      it.key = "g" + std::to_string(g.id);
      it.text = "Группа «" + w::orName(g.name) + "» в смертельной опасности";
      decide.items.push_back(std::move(it));
    }
  if (fc) {
    if (fc->famine || fc->provisionDebtNext > 1e-9) {
      Item it{Item::Faction, state, "faction.economy", "grain"};
      it.key = "famine";
      it.text = fc->famine ? "Голод" : "Провизии не хватит";
      it.sub = "Недостача после хода: " + fmtAmount(std::max(fc->provisionDebtNext, fc->provisionDebt));
      decide.items.push_back(std::move(it));
    }
    if (fc->treasury < -1e-9 || fc->treasury + fc->net < -1e-9) {
      Item it{Item::Faction, state, "faction.economy", "treasury"};
      it.key = "debt";
      it.text = fc->treasury < -1e-9 ? "Казна в долгу" : "Казна уйдёт в долг";
      it.sub = "После хода: " + fmtGold(fc->treasury + fc->net);
      decide.items.push_back(std::move(it));
    }
    for (const auto& [eid, fl] : fc->essences)
      if (fl.stock + fl.net < -1e-9 && (fl.upkeep > 0 || fl.conversionIn > 0)) {
        Item it{Item::Faction, state, "faction.economy", "essence"};
        it.dot = w::essenceColor(w, eid);
        it.icon = nullptr;
        it.key = "e" + std::to_string(eid);
        it.text = "Не хватит эссенции «" + w::essenceName(w, eid) + "»";
        it.sub = "После хода: " + fmtAmount(fl.stock + fl.net);
        decide.items.push_back(std::move(it));
      }
  }
  // Войска и флот с очком действий.
  Group armies{"armies", "Могут ходить", "bolt", {}};
  w.armies.each([&](const Army& ar) {
    if (ar.leader() != state || !rules::hasAction(w, ar.id)) return;
    Item it{Item::Army, ar.id, nullptr, ar.isFleet() ? "fleet" : "army"};
    it.key = std::to_string(ar.id);
    it.text = mil::objectName(ar);
    const Id pid = mil::provinceUnder(w, ar.pos);
    it.sub = pid ? w.provinceName(pid) : std::string(ar.isFleet() ? "Открытое море" : "Вне провинций");
    it.hint = fmtShort(double(mil::unitCount(ar)));
    armies.items.push_back(std::move(it));
  });
  // Провинции, где можно начать стройку (правила игрока: одна новая постройка в провинции за ход).
  Group builds{"builds", "Можно строить", "build", {}};
  if (fc)
    for (Id pid : fc->provinces) {
      const Province* p = w.province(pid);
      if (!p || p->sea || p->owner != state) continue;
      int n = 0;
      for (const rules::BuildOption& o : rules::buildOptions(w, pid, true)) n += o.can ? 1 : 0;
      if (!n) continue;
      Item it{Item::Province, pid, "province.buildings", "province"};
      it.key = std::to_string(pid);
      it.text = w.provinceName(pid);
      it.sub = "Доступно построек: " + std::to_string(n);
      builds.items.push_back(std::move(it));
    }
  // Исследования: ни одно не идёт, а начать можно.
  Group research{"research", "Исследования", "research", {}};
  {
    int running = 0, available = 0;
    w.techs.each([&](const Tech& t) {
      if (t.faction != 0 && t.faction != state) return;
      const TechProgress st = rules::techState(w, t.id, state);
      if (st.research) running++;
      else if (!st.studied && rules::canResearch(w, t.id, state).ok) available++;
    });
    if (!running && available) {
      Item it{Item::Faction, state, "faction.tech", "research"};
      it.key = "idle";
      it.text = "Исследования не идут";
      it.sub = "Можно начать: " + std::to_string(available);
      research.items.push_back(std::move(it));
    }
  }
  // Археологические группы без задания в этом ходу.
  Group arch{"arch", "Археологи свободны", "pickaxe", {}};
  for (const ArchGroup& g : f->archGroups)
    if (!g.danger && rules::archGroupReady(w, state, g.id)) {
      Item it{Item::Faction, state, "faction.arch", "pickaxe"};
      it.key = std::to_string(g.id);
      it.text = w::orName(g.name);
      arch.items.push_back(std::move(it));
    }
  // Свободные очки талантов у героев с подклассом.
  Group talents{"talents", "Очки талантов", "talent", {}};
  w.characters.each([&](const Character& ch) {
    if (ch.faction != state || !ch.subclass || !rules::heroAvailable(w, ch.id)) return;
    const int free = rules::talentPoints(w, ch.id) - rules::talentPointsSpent(w, ch.id);
    if (free <= 0) return;
    Item it{Item::Character, ch.id, "character.talents", "talent"};
    it.key = std::to_string(ch.id);
    it.text = w::orName(ch.name, "Без имени");
    it.hint = std::to_string(free);
    talents.items.push_back(std::move(it));
  });
  // Войны.
  Group wars{"wars", "Войны", "war", {}};
  w.factions.each([&](const Faction& o) {
    if (o.id == state || o.isWild() || w.relation(state, o.id).s != RelStatus::War) return;
    Item it{Item::Faction, o.id, "faction.diplomacy", nullptr};
    it.dot = o.color;
    it.key = std::to_string(o.id);
    it.text = w::orName(o.name);
    wars.items.push_back(std::move(it));
  });
  auto byText = [](Group& g) {
    std::stable_sort(g.items.begin(), g.items.end(), [](const Item& x, const Item& y) { return compareRu(x.text, y.text) < 0; });
  };
  byText(armies);
  byText(talents);
  byText(wars);
  for (Group* g : {&decide, &armies, &builds, &research, &arch, &talents, &wars})
    if (!g->items.empty()) m.groups.push_back(std::move(*g));
  m.valid = true;
  return m;
}

// Государство страницы: игрок — своё; администратор — выбранное вверху (по умолчанию — выделенное или последнее
// открытое государство, иначе первое основное).
struct AdminPick {
  Id state = 0;
};
Id& adminPick() { return sessionState<AdminPick>().state; }

Id pageState(App& a, const World& w) {
  auto isState = [&](Id id) {
    const Faction* f = w.faction(id);
    return f && f->isState();
  };
  if (a.player()) return isState(a.ui.playerState) ? a.ui.playerState : 0;
  Id& pick = adminPick();
  if (isState(pick)) return pick;
  if (a.ui.sel.type == SelType::Faction && isState(a.ui.sel.id)) return pick = a.ui.sel.id;
  if (auto it = a.ui.lastOf.find("states"); it != a.ui.lastOf.end() && it->second.type == SelType::Faction && isState(it->second.id))
    return pick = it->second.id;
  Id first = 0;
  for (Id id : detail::role::playableStates(w))
    if (!first) first = id;
  return pick = first;
}

const Model& model(App& a, const World& w, Id state) {
  static Model m;
  const int view = a.ui.viewTurn ? *a.ui.viewTurn : 0;
  if (!m.valid || m.version != a.store.version() || m.view != view || m.state != state) {
    m = build(w, state);
    m.version = a.store.version();
    m.view = view;
  }
  return m;
}

// Переход к объекту строки.
void go(App& a, const Item& it) {
  switch (it.go) {
    case Item::Army: a.select(SelType::Army, it.id, true); break;
    case Item::Province:
      if (it.tab) a.ui.tabOf[SelType::Province] = it.tab;
      a.select(SelType::Province, it.id, true);
      break;
    case Item::Faction:
      if (it.tab) a.ui.tabOf[SelType::Faction] = it.tab;
      a.select(SelType::Faction, it.id);
      break;
    case Item::Character:
      if (it.tab) a.ui.tabOf[SelType::Character] = it.tab;
      a.select(SelType::Character, it.id);
      break;
    case Item::Proposal:   // ответ на предложение — в разделе «Решения» (условия, причины, последствия)
      a.openSection("decisions");
      break;
    case Item::None: break;
  }
}

void drawMyTurn(App& a) {
  const World& w = w::frameWorld(a);
  const Id state = pageState(a, w);
  if (a.admin()) {   // администратор смотрит ход любого государства
    Id pick = state;
    if (w::factionPicker("state", pick, w::FactionFilter::States, "")) adminPick() = pick;
    a.markUi("myturn.state");
  }
  if (!state) {
    ui::emptyState("crown", "Государств нет.");
    return;
  }
  const Model& m = model(a, w, state);
  if (m.groups.empty()) {
    ui::emptyState("check-circle", "Всё сделано.");
    a.markUi("myturn.empty");
    return;
  }
  for (const Group& g : m.groups) {
    ui::IdScope gs(g.key);
    const std::string badge = std::to_string(g.items.size());
    ui::Section s(g.title, g.icon, {.badge = badge});
    a.markUi(std::string("myturn.") + g.key);
    if (!s) continue;
    for (size_t i = 0; i < g.items.size(); i++) {
      const Item& it = g.items[i];
      ui::IdScope is{i64(i)};
      const std::string mark = std::string("myturn.") + g.key + "." + it.key;
      if (ui::listItem(it.text, {.icon = it.dot.a ? nullptr : it.icon, .dot = it.dot, .subtitle = it.sub, .hint = it.hint, .tooltip = it.text})) {
        Item copy = it;
        detail::later(a, [copy](App& x) { go(x, copy); });
      }
      a.markUi(mark);
    }
  }
}

// Без числа на значке: ждущие ответа предложения и археологи в опасности — на значке «Решений».
SectionReg section({.id = "myturn", .icon = "hourglass", .title = "Мой ход", .order = 15, .shortcut = "Ctrl+T", .directory = drawMyTurn,
                    .allTitle = "Ход государства"});

}  // namespace
}  // namespace rg::app
