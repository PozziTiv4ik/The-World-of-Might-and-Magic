// Regnum — вкладка фракции «Технологии» (ТЗ 1.b.v): сводка дерева (изучено, исследуется), ход исследований,
// доступные технологии и кнопка «Открыть дерево технологий» (у каждого государства и гильдии — своё дерево).
// «Общие технологии» (ТЗ «Доработки», п.6): общее дерево фракция изучает сама — состояние для неё, исследование,
// отметка изученности, переход в общее дерево. Сроки — с модификатором «Время исследования технологий» государства
// (rules::researchTurns).
#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/widgets.h"

namespace rg::app {

namespace {

// need — срок с модификатором «Время исследования технологий» государства (rules::researchTurns); progress — пройдено.
struct T {
  Id id = 0;
  std::string name;
  int need = 1, progress = 0;
  bool studied = false, research = false, available = false;
  std::vector<Id> missing;   // общие: не изученные условия
  std::vector<std::string> problems;   // не достроены нужные постройки, не хватает ресурсов (ТЗ «Доработки №4», п.1)
  std::map<Id, double> cost;           // стоимость исследования
  int left() const { return std::max(1, need - progress); }
};

// Технологии дерева tree (0 — общее) с состоянием для фракции faction.
std::vector<T> techsOf(const World& w, Id tree, Id faction) {
  std::vector<T> out;
  std::map<int, int> needOf;   // базовый срок → срок с модификатором (один расчёт эффектов на значение)
  w.techs.each([&](const Tech& t) {
    if (t.faction != tree) return;
    T x;
    x.id = t.id;
    x.name = t.name.empty() ? std::string("Без названия") : t.name;
    const int base = std::max(1, t.turns);
    auto it = needOf.find(base);
    if (it == needOf.end()) it = needOf.emplace(base, rules::researchTurns(w, t, faction)).first;
    x.need = it->second;
    const TechProgress p = rules::techState(w, t.id, faction);
    x.progress = std::max(0, p.progress);
    x.studied = p.studied;
    x.research = p.research && !p.studied;
    x.cost = t.cost;
    if (!x.studied) {
      rules::ResearchCheck rc = rules::canResearch(w, t.id, faction);
      x.available = !x.research && rc.ok;
      x.missing = rc.missing;
      x.problems = rc.problems;
    }
    out.push_back(std::move(x));
  });
  std::stable_sort(out.begin(), out.end(), [](const T& a, const T& b) { return compareRu(a.name, b.name) < 0; });
  return out;
}

std::string names(const World& w, const std::vector<Id>& ids) {
  std::vector<std::string> n;
  for (Id id : ids)
    if (const Tech* t = w.tech(id)) n.push_back(t->name.empty() ? std::string("Без названия") : t->name);
  return join(n, ", ");
}

// Общие технологии: исследуемые и доступные — выше, изученные — ниже.
void commonSection(App& a, Id fid, bool ro) {
  const World& w = a.world();
  std::vector<T> ts = techsOf(w, 0, fid);
  auto rank = [](const T& t) { return t.research ? 0 : t.available ? 1 : !t.studied ? 2 : 3; };
  std::stable_sort(ts.begin(), ts.end(), [&](const T& x, const T& y) { return rank(x) < rank(y); });
  int studied = 0;
  for (const T& t : ts) studied += t.studied;
  ui::Section s("Общие технологии", "globe", {.defaultOpen = true, .badge = std::to_string(studied) + " / " + std::to_string(ts.size())});
  a.markUi("faction.ctech");
  if (!s) return;
  for (const T& t : ts) {
    ui::IdScope sc{i64(t.id)};
    const std::string id = std::to_string(t.id);
    const char* icon = t.studied ? "check" : t.research ? "hourglass" : t.available ? "research" : "lock";
    const ui::Tone tone = t.studied ? ui::Tone::Success : t.research ? ui::Tone::Info : t.available ? ui::Tone::Accent : ui::Tone::Neutral;
    // Закрыта: не изучены условия, не достроены нужные постройки, не хватает ресурсов на стоимость.
    const bool lockedByNeeds = !t.studied && !t.research && !t.available && t.missing.empty() && !t.problems.empty();
    std::string state = t.studied       ? std::string("изучено")
                        : t.research    ? "исследуется " + std::to_string(t.progress) + " из " + std::to_string(t.need)
                        : t.available   ? "доступно · " + nTurns(t.left())
                        : lockedByNeeds ? t.problems.front()
                                        : std::string("закрыто");
    const std::string lockTip = !t.missing.empty() ? "Сначала изучите: " + names(w, t.missing) : join(t.problems, "\n");
    const Faction* payer = w.faction(fid);
    ui::Row row({ui::px(18), ui::fr(1), ui::px(30), ui::px(30)}, ui::kAuto, 6);
    ui::iconColored(icon, ui::toneColor(tone), 16);
    {
      ui::Group g(0, 1);
      if (ui::link(t.name)) openTechTree(a, 0, t.id, fid);
      a.markUi("faction.ctech.row." + id);
      ui::label(state, {.font = ui::Font::Small, .ink = t.research ? ui::Ink::Info : t.available ? ui::Ink::Accent : lockedByNeeds ? ui::Ink::Warning : ui::Ink::Muted,
                        .tooltip = lockTip});
      a.markUi("faction.ctech.state." + id);
      if (t.research) ui::progress(double(t.progress) / double(std::max(1, t.need)), {.tone = ui::Tone::Info, .height = 3});
      // Стоимость исследования (у идущего — уплачено, без отметки нехватки).
      if (!t.studied && !t.cost.empty()) {
        ui::IdScope cs("cost");
        bld::costChips(t.cost, t.research ? nullptr : payer, false);
        a.markUi("faction.ctech.cost." + id);
      }
    }
    const Id tid = t.id;
    // Исследование: начать / остановить (стоимость возвращается); изученную — снять отметку.
    if (t.research) {
      if (ui::iconButton("close", t.cost.empty() ? std::string("Остановить исследование") : std::string("Остановить исследование: стоимость вернётся"),
                         {.disabled = ro}))
        a.act("Остановить исследование", [&](Tx& tx) { rules::stopResearch(tx, tid, fid); });
      a.markUi("faction.ctech.stop." + id);
    } else if (t.studied) {
      if (ui::iconButton("check-circle", "Снять отметку изучения", {.toggled = true, .disabled = ro, .tone = ui::Tone::Success}))
        a.act("Снять отметку изучения", [&](Tx& tx) { rules::setStudied(tx, tid, false, fid); });
      a.markUi("faction.ctech.unstudy." + id);
    } else {
      const std::string startTip = t.available ? std::string("Начать исследование: ") + nTurns(t.left()) +
                                                     (t.cost.empty() ? std::string() : "; стоимость спишется сразу")
                                   : lockTip.empty() ? std::string("Начать исследование")
                                                     : lockTip;
      if (ui::iconButton("play", startTip, {.disabled = ro || !t.available, .tone = ui::Tone::Accent}))
        a.act("Начать исследование", [&](Tx& tx) { rules::startResearch(tx, tid, fid); });
      a.markUi("faction.ctech.start." + id);
    }
    // Отметить изученной сразу.
    if (!t.studied) {
      if (ui::iconButton("check", lockTip.empty() ? std::string("Отметить изученной") : lockTip, {.disabled = ro || !t.missing.empty(), .tone = ui::Tone::Success}))
        a.act("Отметить изученной", [&](Tx& tx) { rules::setStudied(tx, tid, true, fid); });
      a.markUi("faction.ctech.study." + id);
    } else {
      ui::next(28);
    }
  }
  if (ts.empty()) ui::label("Общее дерево пусто", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "globe"});
  if (ui::link("Общее дерево технологий", "globe")) openTechTree(a, 0, 0, fid);
  a.markUi("faction.ctech.open");
}

void drawTech(App& a, Id fid) {
  const World& w = a.world();
  const Faction* f = w.faction(fid);
  if (!f) return;
  const bool ro = a.readOnly();
  std::vector<T> ts = techsOf(w, fid, 0);
  if (ts.empty()) {
    ui::spacer(8);
    if (ui::emptyState("tech-tree", f->isGuild() ? "У гильдии пока нет технологий." : "Дерево технологий пусто.", "Открыть дерево технологий", "tech-tree"))
      openTechTree(a, fid);
    a.markUi("faction.tech.open", tree::emptyActionRect("Открыть дерево технологий", "tech-tree"));
    commonSection(a, fid, ro);
    return;
  }
  int studied = 0, research = 0, avail = 0;
  for (const T& t : ts) {
    studied += t.studied;
    research += t.research;
    avail += t.available;
  }
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
    ui::stat(std::to_string(studied) + " / " + std::to_string(ts.size()), "Изучено", {.icon = "check-circle", .tone = ui::Tone::Success});
    ui::stat(std::to_string(research), "Исследуется", {.icon = "hourglass", .tone = ui::Tone::Info});
  }
  ui::progress(double(studied) / double(ts.size()), {.tone = ui::Tone::Success, .height = 5, .label = true});
  if (ui::button("Открыть дерево технологий", {.variant = ui::Variant::Primary, .icon = "tech-tree", .fill = true})) openTechTree(a, fid);
  a.markUi("faction.tech.open");
  if (research > 0) {
    if (ui::Section s("Исследуются", "hourglass", {.badge = std::to_string(research)}); s) {
      for (const T& t : ts) {
        if (!t.research) continue;
        ui::IdScope sc{i64(t.id)};
        ui::Row row({ui::fr(1), ui::px(30)}, ui::kAuto, 8);
        {
          ui::Group g(0, 4);
          if (ui::link(t.name)) openTechTree(a, fid, t.id);
          ui::progress(double(t.progress) / double(t.need),
                       {.tone = ui::Tone::Info, .height = 5, .text = std::to_string(t.progress) + "/" + std::to_string(t.need) + " · ещё " + nTurns(t.left())});
          a.markUi("faction.tech.progress." + std::to_string(t.id));
        }
        Id tid = t.id;
        if (ui::iconButton("close", "Остановить исследование", {.disabled = ro})) a.act("Остановить исследование", [&](Tx& tx) { rules::stopResearch(tx, tid); });
        a.markUi("faction.tech.stop." + std::to_string(t.id));
      }
    }
  }
  if (avail > 0) {
    if (ui::Section s("Можно исследовать", "research", {.badge = std::to_string(avail)}); s) {
      for (const T& t : ts) {
        if (!t.available) continue;
        ui::IdScope sc{i64(t.id)};
        ui::Row row({ui::fr(1), ui::px(76), ui::px(30)}, 28, 8);
        if (ui::link(t.name)) openTechTree(a, fid, t.id);
        ui::label(nTurns(t.left()), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Right});
        a.markUi("faction.tech.left." + std::to_string(t.id));
        Id tid = t.id;
        if (ui::iconButton("play", "Начать исследование", {.disabled = ro, .tone = ui::Tone::Accent}))
          a.act("Начать исследование", [&](Tx& tx) { rules::startResearch(tx, tid); });
        a.markUi("faction.tech.start." + std::to_string(t.id));
      }
    }
  }
  if (studied > 0) {
    if (ui::Section s("Изучены", "check-circle", {.defaultOpen = studied <= 12, .badge = std::to_string(studied)}); s) {
      tree::ChipFlow flow;
      for (const T& t : ts) {
        if (!t.studied) continue;
        ui::IdScope sc{i64(t.id)};
        if (tree::chip(t.name, {.icon = "check", .tone = ui::Tone::Success, .clickable = true, .tooltip = "Показать в дереве"}) == ui::ChipAction::Click)
          openTechTree(a, fid, t.id);
      }
    }
  }
  commonSection(a, fid, ro);
}

// Исследуются: своё дерево и общие технологии этой фракции.
int badgeTech(App& a, Id fid) {
  int n = 0;
  a.world().techs.each([&](const Tech& t) { n += t.faction == fid && t.research && !t.studied; });
  if (const Faction* f = a.world().faction(fid))
    for (auto& [tid, s] : f->techs) n += s.research && !s.studied && a.world().tech(tid) != nullptr;
  return n;
}

TabReg regTab({"faction.tech", "tech-tree", "Технологии", 60, SelType::Faction, nullptr, drawTech, badgeTech});

}  // namespace
}  // namespace rg::app
