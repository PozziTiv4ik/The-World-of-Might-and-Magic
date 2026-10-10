// Regnum — вкладка государства «Археология» (ТЗ «Доработки №2», п.1–3, 14–17): слева — археологические группы
// («Сформировать археологическую группу» при достроенной «Гильдии Археологов», название, опыт и уровень, содержание,
// шанс успеха и живучесть, модификаторы групп со сроками, отметки «занята в этом ходу», «ранена», «заражена чумой»,
// роспуск) и реликвии государства (находки археологов: герою, в хранилище постройки, освободить); справа — провинции
// государства с четырьмя слотами: «Найти археологическое место», щелчок по найденному месту — «Назначить
// археологическую группу», «Проводить раскопки», когда все места провинции исследованы полностью.
#include "app/panels/arch_common.h"
#include "app/panels/faction_common.h"

namespace rg::app {
namespace {

using namespace archui;

constexpr float kWide = 760;    // шире — группы слева, провинции справа
constexpr float kSlot = 34, kSlotGap = 6;

std::string goldText(double v) { return fmtNum(v, 3) + " тыс."; }

void stats(const World& w, const Faction& f) {
  const Id tr = w.catalogs->resourceId(schema::kResArchTreasure), sc = w.catalogs->resourceId(schema::kResShantiriScrolls);
  auto calc = rules::calc(w);
  const rules::FactionCalc* fc = calc->faction(f.id);
  ui::Row r({ui::fr(1), ui::fr(1), ui::fr(1)}, 64, 8);
  ui::stat(fmtNum(tr ? f.stock(tr) : 0, 3), "Сокровища", {.icon = "coins", .tone = ui::Tone::Accent, .tooltip = "Археологические сокровища"});
  ui::stat(fmtNum(sc ? f.stock(sc) : 0, 3), "Свитки", {.icon = "scroll", .tone = ui::Tone::Info, .tooltip = "Древние свитки Шантири"});
  ui::stat(goldText(fc ? fc->expArch : 0), "Расход", {.icon = "expense", .tone = ui::Tone::Warning, .tooltip = "Содержание археологических групп за ход"});
}

void tagTip(std::string_view text, ui::Tone tone, const char* icon, std::string_view tip) {
  ui::tag(text, tone, icon);
  ui::hoverTip(std::string("##tag") + std::string(text), ui::lastItem().rect, tip);
}

void groupCard(App& a, const World& w, Id fid, const ArchGroup& g, bool ro) {
  ui::IdScope sc{i64(g.id)};
  const Id gid = g.id;
  const rules::ArchStats st = rules::archStats(w, fid, gid);
  ui::Card card({.pad = 10});
  {
    ui::Row r({ui::fr(1), ui::px(92), ui::px(30)}, 30, 6);
    std::string name = g.name;
    if (ui::textField("name", name, {.placeholder = "Название группы", .icon = "pickaxe", .disabled = ro}))
      a.act("Название археологической группы", [&](Tx& tx) { rules::renameArchGroup(tx, fid, gid, name); });
    a.markUi("arch.group.name." + std::to_string(gid));
    int exp = g.exp;
    if (ui::numberField("exp", exp, {.min = 0, .max = arch::kMaxExp, .icon = "star", .disabled = ro, .tooltip = "Опыт группы (0–1000)"}))
      a.act("Опыт археологической группы", [&](Tx& tx) { rules::setArchGroupExp(tx, fid, gid, exp); },
            {.coalesce = "arch.exp:" + std::to_string(gid)});
    a.markUi("arch.group.exp." + std::to_string(gid));
    if (ui::iconButton("trash", "Распустить группу", {.disabled = ro, .tone = ui::Tone::Danger})) {
      const std::string nm = groupName(g);
      a.confirm("Распустить группу «" + nm + "»?", "Население и золото не вернутся.", "Распустить", true,
                [fid, gid](App& x) { x.act("Распустить археологическую группу", [&](Tx& tx) { rules::disbandArchGroup(tx, fid, gid); }); });
    }
    a.markUi("arch.group.disband." + std::to_string(gid));
  }
  {
    const arch::LevelInfo& L = arch::kLevels[st.level - 1];
    const double k = st.level >= arch::kMaxLevel ? double(g.exp - L.minExp) / double(std::max(1, arch::kMaxExp - L.minExp))
                                                  : double(g.exp - L.minExp) / double(std::max(1, L.maxExp + 1 - L.minExp));
    ui::Row r({ui::px(46), ui::fr(1)}, 20, 8);
    ui::label("Ур. " + std::to_string(st.level), {.font = ui::Font::Strong, .ink = ui::Ink::Accent, .tooltip = "Уровень группы"});
    ui::progress(std::clamp(k, 0.0, 1.0), {.tone = ui::Tone::Accent, .height = 6, .text = fmtInt(g.exp) + " / " + fmtInt(st.level >= arch::kMaxLevel ? arch::kMaxExp : L.maxExp)});
  }
  {
    ui::HStack hs(22, ui::Align::Left, 6);
    tagTip(goldText(st.upkeep), ui::Tone::Neutral, "expense", "Содержание за ход");
    tagTip(fmtPct(st.success, 0, true), ui::Tone::Success, "target", "Дополнительный шанс успеха");
    tagTip(fmtPct(st.vitality, 0, true), ui::Tone::Info, "heart", "Дополнительная живучесть");
    auto left = [&](std::string_view key) {
      const Id m = rules::builtinModId(w, key);
      auto it = m ? g.modTurns.find(m) : g.modTurns.end();
      return it == g.modTurns.end() ? 0 : it->second;
    };
    if (g.danger) tagTip("Смертельная опасность", ui::Tone::Danger, "skull", "Группу может спасти только божественное вмешательство");
    if (g.busy == w.turn()) tagTip("Занята", ui::Tone::Info, "hourglass", "Уже участвовала в исследовании в этом ходу");
    if (st.wounded) {
      const int n = left(schema::mod::ArchWounded);
      tagTip(n > 0 ? "Ранена · " + std::to_string(n) : std::string("Ранена"), ui::Tone::Danger, "warning", "Ранение в ходе исследования");
    }
    if (st.plague) {
      const int n = left(schema::mod::ArchPlague);
      tagTip(n > 0 ? "Чума · " + std::to_string(n) : std::string("Чума"), ui::Tone::Warning, "plague", "Заражение чумой");
    }
  }
  if (g.danger) {
    if (ui::button("Божественное вмешательство", {.variant = ui::Variant::Danger, .icon = "sparkles", .fill = true, .disabled = ro}))
      showDivine(a, fid, gid);
    a.markUi("arch.group.divine." + std::to_string(gid));
  }
  {
    std::vector<Id> ids = g.modifiers;
    w::ModEdit ed;
    if (w::modifierList("mods", ids, ro, w::ModScope::ArchGroup, &g.modTurns, &ed))
      a.act("Модификаторы археологической группы", [&](Tx& tx) {
        if (!ed.addKey.empty()) ids.push_back(rules::ensureBuiltinMod(tx, ed.addKey));
        rules::setArchGroupModifiers(tx, fid, gid, ids);
      });
    else if (ed.termOf)
      a.act("Срок модификатора группы", [&](Tx& tx) { rules::setArchGroupModTurns(tx, fid, gid, ed.termOf, ed.turns); },
            {.coalesce = "arch.modturns:" + std::to_string(gid) + ":" + std::to_string(ed.termOf)});
    a.markUi("arch.group.mods." + std::to_string(gid));
  }
}

void groupsSection(App& a, const World& w, const Faction& f, bool ro) {
  const Id fid = f.id;
  const rules::ArchGroupCost cost = rules::archGroupCost(w, fid);
  const bool guild = rules::builtWithKey(w, fid, schema::bld::ArchGuild) > 0;
  const std::string costText = fmtInt(cost.people) + (cost.corpses ? " трупов" : " жителей") + " и " + goldText(cost.gold) + " золота";
  const std::string tip = "Сформировать археологическую группу · " + costText + (cost.problems.empty() ? std::string() : "\n" + cost.problems.front());
  ui::Section s("Археологические группы", "pickaxe",
                {.badge = f.archGroups.empty() ? std::string() : std::to_string(f.archGroups.size()), .actionIcon = !ro && guild ? "plus" : nullptr,
                 .actionTooltip = tip});
  a.markUi("arch.groups");
  if (s.action()) a.act("Сформировать археологическую группу", [&](Tx& tx) { rules::createArchGroup(tx, fid); });
  if (!s) return;
  if (!guild) ui::label("Нет «" + rules::buildingKeyName(w, fid, schema::bld::ArchGuild) + "»", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
  if (f.archGroups.empty()) {
    if (guild) {
      ui::Disabled d(ro);
      if (ui::emptyState("pickaxe", "Групп нет.", "Сформировать", "plus"))
        a.act("Сформировать археологическую группу", [&](Tx& tx) { rules::createArchGroup(tx, fid); });
      ui::tooltip(tip);
      a.markUi("arch.groups.create");
    }
    return;
  }
  for (const ArchGroup& g : f.archGroups) groupCard(a, w, fid, g, ro);
}

void relicsSection(App& a, const World& w, const Faction& f, bool ro) {
  const Id fid = f.id;
  ui::Section s("Реликвии государства", "relic", {.badge = f.relics.empty() ? std::string() : std::to_string(f.relics.size())});
  a.markUi("arch.relics");
  if (!s) return;
  if (f.relics.empty()) {
    ui::label("Нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    return;
  }
  const std::vector<Id> list = f.relics;
  for (Id rid : list) {
    const Relic* r = w.relic(rid);
    if (!r) continue;
    ui::IdScope sc{i64(rid)};
    const RectF top = ui::avail();
    const RectF row{top.x, top.y, top.w, 40};
    w::relicRow(*r, rules::isArchFind(w, rid) ? std::string("Археологическая находка") : std::string(), 3 * 30 + 12);
    float x = row.right() - 3 * 30 - 8;
    ui::at(RectF{x, row.cy() - 15, 30, 30});
    if (ui::iconButton("hero", "Отдать герою", {.disabled = ro})) ui::openPopup("give");
    a.markUi("arch.relic.give." + std::to_string(rid));
    if (ui::beginMenu("give")) {
      ui::menuHeader("Отдать герою");
      int n = 0;
      w.characters.each([&](const Character& c) {
        if (c.faction != fid || !rules::heroAvailable(w, c.id)) return;
        ui::IdScope s2{i64(c.id)};
        const Id cid = c.id;
        if (ui::menuItem(c.name.empty() ? std::string("Без имени") : c.name, {.icon = "hero"}))
          a.act("Отдать реликвию герою", [&](Tx& tx) { rules::giveRelic(tx, cid, rid); });
        n++;
      });
      if (!n) ui::menuItem("Нет героев", {.disabled = true});
      ui::endMenu();
    }
    x += 32;
    ui::at(RectF{x, row.cy() - 15, 30, 30});
    if (ui::iconButton("building", "Положить в хранилище постройки", {.disabled = ro})) ui::openPopup("store");
    a.markUi("arch.relic.store." + std::to_string(rid));
    if (ui::beginMenu("store")) {
      ui::menuHeader("Положить в хранилище");
      int n = 0;
      w.provinces.each([&](const Province& p) {
        if (p.owner != fid || p.sea) return;
        for (const ProvBuilding& pb : p.buildings) {
          const Building* b = w.building(pb.building);
          if (!b || !b->relicStore || pb.builtLevel() < 1) continue;
          ui::IdScope s2{i64(p.id) * 100000 + i64(b->id)};
          const Id pid = p.id, bid = b->id;
          if (ui::menuItem(b->name + " · " + p.name, {.icon = "building"}))
            a.act("Положить реликвию в хранилище", [&](Tx& tx) { rules::storeRelic(tx, pid, bid, rid); });
          n++;
        }
      });
      if (!n) ui::menuItem("Нет хранилищ реликвий", {.disabled = true});
      ui::endMenu();
    }
    x += 32;
    ui::at(RectF{x, row.cy() - 15, 30, 30});
    if (ui::iconButton("unlink", "Освободить реликвию", {.disabled = ro}))
      a.act("Освободить реликвию", [&](Tx& tx) { rules::releaseStateRelic(tx, fid, rid); });
    a.markUi("arch.relic.release." + std::to_string(rid));
  }
}

// Малые слоты провинции: щелчок по найденному неисследованному месту — «Назначить археологическую группу».
void slotRow(App& a, const World& w, Id fid, const Province& p, RectF area, bool ro) {
  for (int i = 0; i < kArchSlots; i++) {
    ui::IdScope sc(i);
    const ArchSlot& s = p.arch[size_t(i)];
    const RectF r{area.x + float(i) * (kSlot + kSlotGap), area.y, kSlot, kSlot};
    const bool active = !ro && rules::canExplore(w, fid, p.id, i);
    std::string tip = slotTip(w, s, i, false);
    if (active) tip += "\nНазначить археологическую группу";
    ui::at(r);
    ui::iconColored("pickaxe", Color(0, 0, 0, 0), kSlot, tip);
    const ui::Item it = ui::lastItem();
    drawSlotSmall(w, s, i, r, it.hovered, active);
    if (active && it.hovered) ui::setCursor(platform::Cursor::Hand);
    if (active && it.clicked) ui::openPopup("assign");
    a.markUi("arch.prov." + std::to_string(p.id) + ".slot." + std::to_string(i), r);
    const Id pid = p.id;
    if (active) {
      const std::string head = "Назначить археологическую группу · " + siteName(w, s.site) + " · этап " + std::to_string(s.stage + 1) + " из " +
                               std::to_string(arch::stagesOf(i));
      if (Id g = groupMenu(a, w, fid, "assign", head, [&](Id gid) { return rules::exploreChance(w, fid, gid, pid, i); })) explore(a, fid, g, pid, i);
    }
  }
}

void provinceRow(App& a, const World& w, Id fid, const Province& p, bool ro, bool wide) {
  ui::IdScope sc{i64(p.id)};
  const Id pid = p.id;
  int found = 0, done = 0;
  for (int i = 0; i < kArchSlots; i++) {
    found += p.arch[size_t(i)].site && p.arch[size_t(i)].open;
    done += arch::slotDone(p.arch[size_t(i)], i);
  }
  const float slotsW = kArchSlots * kSlot + (kArchSlots - 1) * kSlotGap;
  auto name = [&] {
    ui::Group g(0, 0);
    ui::label(p.name.empty() ? std::string("Без названия") : p.name, {.font = ui::Font::Strong, .tooltip = "Провинция — открыть"});
    if (ui::lastItem().hovered) ui::setCursor(platform::Cursor::Hand);
    if (ui::lastItem().clicked) a.select(SelType::Province, pid);
    ui::label("Найдено " + std::to_string(found) + " из " + std::to_string(kArchSlots) + " · исследовано " + std::to_string(done),
              {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  };
  auto buttons = [&] {
    std::string why;
    const bool can = rules::canDiscover(w, fid, pid, &why);
    if (ui::iconButton("search", can ? std::string("Найти археологическое место") : "Найти археологическое место\n" + why, {.disabled = ro || !can}))
      ui::openPopup("discover");
    a.markUi("arch.prov." + std::to_string(pid) + ".discover");
    if (can && !ro)
      if (Id g = groupMenu(a, w, fid, "discover", "Найти археологическое место", [&](Id gid) { return rules::discoveryChance(w, fid, gid); }))
        discover(a, fid, g, pid);
    if (arch::allDone(p)) {
      if (ui::iconButton("shovel", "Проводить раскопки", {.disabled = ro})) ui::openPopup("dig");
      a.markUi("arch.prov." + std::to_string(pid) + ".dig");
      if (!ro)
        if (Id g = groupMenu(a, w, fid, "dig", "Проводить раскопки", {})) excavate(a, fid, g, pid);
    } else {
      ui::next(30, 30);
    }
  };
  if (wide) {
    ui::Row r({ui::fr(1, 100), ui::px(slotsW), ui::px(30), ui::px(30)}, kSlot + 4, 10);
    name();
    slotRow(a, w, fid, p, ui::next(slotsW, kSlot), ro);
    buttons();
  } else {
    name();
    ui::Row r({ui::px(slotsW), ui::fr(1), ui::px(30), ui::px(30)}, kSlot, 8);
    slotRow(a, w, fid, p, ui::next(slotsW, kSlot), ro);
    ui::next(0, kSlot);
    buttons();
  }
  ui::separator();
}

void provincesSection(App& a, const World& w, const Faction& f, bool ro, bool wide) {
  std::vector<const Province*> list;
  w.provinces.each([&](const Province& p) {
    if (p.owner == f.id && !p.sea) list.push_back(&p);
  });
  std::sort(list.begin(), list.end(), [](const Province* x, const Province* y) { return compareRu(x->name, y->name) < 0; });
  ui::Section s("Провинции", "province", {.badge = list.empty() ? std::string() : std::to_string(list.size())});
  a.markUi("arch.provinces");
  if (!s) return;
  if (list.empty()) {
    ui::emptyState("province", "Провинций нет.");
    return;
  }
  for (const Province* p : list) provinceRow(a, w, f.id, *p, ro, wide);
}

void drawArch(App& a, Id fid) {
  const World& w = fac::frameWorld(a);
  const Faction* f = w.faction(fid);
  if (!f || !f->isState()) return;
  ui::IdScope scope("arch");
  const bool ro = a.readOnly();
  const float aw = ui::avail().w;
  if (aw >= kWide) {
    const float left = std::clamp(std::round(aw * 0.4f), 360.f, 440.f);
    ui::Row r({ui::px(left), ui::fr(1)}, ui::kAuto, 24);
    {
      ui::Group g(0, 8);
      stats(w, *f);
      groupsSection(a, w, *f, ro);
      relicsSection(a, w, *f, ro);
    }
    {
      ui::Group g(0, 8);
      provincesSection(a, w, *f, ro, aw - left - 24 >= 420);
    }
  } else {
    stats(w, *f);
    groupsSection(a, w, *f, ro);
    relicsSection(a, w, *f, ro);
    provincesSection(a, w, *f, ro, aw >= 420);
  }
}

TabReg tab({kTabFaction, "pickaxe", "Археология", 62, SelType::Faction, fac::isState, drawArch, nullptr, true});

}  // namespace
}  // namespace rg::app
