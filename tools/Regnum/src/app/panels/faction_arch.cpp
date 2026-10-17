// Regnum — вкладка государства «Археология» (ТЗ «Доработки №2», п.1–3, 14–17): слева — археологические группы
// («Сформировать археологическую группу» при достроенной «Гильдии Археологов», название, опыт и уровень, содержание,
// шанс успеха и живучесть, модификаторы групп со сроками, отметки «занята в этом ходу», «ранена», «заражена чумой»,
// роспуск) и инвентарь государства (ТЗ «Доработки №4», п.3–4: находки и полученные реликвии стопками — отдать героям,
// положить в хранилище постройки, у администратора — количество, «Убрать» и новые из справочника); справа — провинции
// государства с четырьмя слотами: «Найти археологическое место», щелчок по найденному месту — «Назначить
// археологическую группу», «Проводить раскопки», когда все места провинции исследованы полностью. Задания, группы и
// раздача реликвий — игровые действия за государство (canPlay); опыт и модификаторы групп, количество реликвий —
// правка мира (canEdit).
#include "app/common/relics.h"
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

// ro — нет игровых действий за государство; правка опыта и модификаторов — только администратору (canEdit).
void groupCard(App& a, const World& w, Id fid, const ArchGroup& g, bool ro) {
  const bool edit = a.canEdit();
  ui::IdScope sc{i64(g.id)};
  const Id gid = g.id;
  const rules::ArchStats st = rules::archStats(w, fid, gid);
  ui::Card card({.pad = 10});
  {
    ui::Row r({ui::fr(1), ui::px(92), ui::px(30)}, 30, 6);
    std::string name = g.name;
    if (ui::textField("name", name, {.placeholder = "Название группы", .icon = "pickaxe", .disabled = ro}))
      a.play("Название археологической группы", [&](Tx& tx) { rules::renameArchGroup(tx, fid, gid, name); });
    a.markUi("arch.group.name." + std::to_string(gid));
    int exp = g.exp;
    if (ui::numberField("exp", exp, {.min = 0, .max = arch::kMaxExp, .icon = "star", .readOnly = !edit, .tooltip = "Опыт группы (0–1000)"}))
      a.act("Опыт археологической группы", [&](Tx& tx) { rules::setArchGroupExp(tx, fid, gid, exp); },
            {.coalesce = "arch.exp:" + std::to_string(gid)});
    a.markUi("arch.group.exp." + std::to_string(gid));
    if (ui::iconButton("trash", "Распустить группу", {.disabled = ro, .tone = ui::Tone::Danger})) {
      const std::string nm = groupName(g);
      a.confirm("Распустить группу «" + nm + "»?", "Население и золото не вернутся.", "Распустить", true,
                [fid, gid](App& x) { x.play("Распустить археологическую группу", [&](Tx& tx) { rules::disbandArchGroup(tx, fid, gid); }); });
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
    if (w::modifierList("mods", ids, !edit, w::ModScope::ArchGroup, &g.modTurns, &ed))
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
  if (s.action()) a.play("Сформировать археологическую группу", [&](Tx& tx) { rules::createArchGroup(tx, fid); });
  if (!s) return;
  if (!guild) ui::label("Нет «" + rules::buildingKeyName(w, fid, schema::bld::ArchGuild) + "»", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
  if (f.archGroups.empty()) {
    if (guild) {
      ui::Disabled d(ro);
      if (ui::emptyState("pickaxe", "Групп нет.", "Сформировать", "plus"))
        a.play("Сформировать археологическую группу", [&](Tx& tx) { rules::createArchGroup(tx, fid); });
      ui::tooltip(tip);
      a.markUi("arch.groups.create");
    }
    return;
  }
  for (const ArchGroup& g : f.archGroups) groupCard(a, w, fid, g, ro);
}

// Инвентарь государства (ТЗ «Доработки №4», п.3–4): полученные реликвии стопками по главенству редкости. Игровые
// действия (canPlay): «Отдать герою» и «Положить в хранилище» — окно «куда и сколько» (герои государства, достроенные
// хранилища реликвий). Администратор (правка мира): количество в стопке, «Убрать» (штуки исчезают из мира), новые из
// справочника.
void relicsSection(App& a, const World& w, const Faction& f) {
  const Id fid = f.id;
  const bool play = a.canPlay(fid), admin = a.admin(), edit = a.canEdit();
  const std::vector<RelicStack> list = relics::sorted(w, f.relics);
  const i64 total = relics::pieces(w, f.relics);
  ui::Section s("Инвентарь государства", "inventory", {.badge = total ? fmtInt(total) : std::string()});
  a.markUi("arch.relics");
  if (!s) return;
  if (list.empty()) ui::label("Пусто", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  const rules::RelicPlace here{rules::RelicPlace::State, fid, 0, fid};
  // Куда: доступные герои государства (по имени) и достроенные хранилища реликвий его провинций.
  std::vector<const Character*> heroes;
  struct Store {
    Id pid, bid;
    std::string label;
    const std::vector<RelicStack>* relics;
  };
  std::vector<Store> stores;
  if (play && !list.empty()) {
    w.characters.each([&](const Character& c) {
      if (c.faction == fid && rules::heroAvailable(w, c.id)) heroes.push_back(&c);
    });
    std::sort(heroes.begin(), heroes.end(), [](const Character* x, const Character* y) {
      if (x->hero != y->hero) return x->hero;
      return compareRu(x->name, y->name) < 0;
    });
    w.provinces.each([&](const Province& p) {
      if (p.owner != fid || p.sea) return;
      for (const ProvBuilding& pb : p.buildings)
        if (const Building* b = w.building(pb.building); b && b->relicStore && pb.builtLevel() >= 1)
          stores.push_back(Store{p.id, b->id, w::orName(b->name, "Постройка") + " · " + w::orName(p.name), &pb.relics});
    });
    std::sort(stores.begin(), stores.end(), [](const Store& x, const Store& y) { return compareRu(x.label, y.label) < 0; });
  }
  for (const RelicStack& st : list) {
    const Relic& r = *w.relic(st.relic);
    const Id rid = r.id;
    ui::IdScope sc{i64(rid)};
    const bool count = admin && !r.unique();
    const float right = (play ? 60.f : 0.f) + (edit ? 30.f : 0.f) + (count ? relics::kCountW + 6 : 0.f);
    const std::string sub = rules::isArchFind(w, rid) ? std::string("Находка археологов") : relics::kindText(r);
    RectF rr;
    if (relics::stackRow(r, count ? 0 : st.count, sub, right, &rr) && admin) relics::openCatalog(a, rid);
    a.markUi("arch.relic." + std::to_string(rid), rr);
    float x = rr.right() - 2;
    if (edit) {
      x -= 30;
      ui::at(RectF{x, rr.cy() - 14, 28, 28});
      if (ui::iconButton("trash", st.count > 1 ? "Убрать все " + fmtInt(st.count) + " шт. из мира" : std::string("Убрать из мира"),
                         {.size = ui::Size::Small, .tone = ui::Tone::Danger}))
        a.act("Убрать реликвию государства", [&](Tx& tx) { rules::releaseStateRelic(tx, fid, rid, st.count); });
      a.markUi("arch.relic.release." + std::to_string(rid));
    }
    if (play) {
      x -= 30;
      ui::at(RectF{x, rr.cy() - 14, 28, 28});
      if (ui::iconButton("building", "Положить в хранилище постройки", {.size = ui::Size::Small})) ui::openPopup("store");
      a.markUi("arch.relic.store." + std::to_string(rid));
      std::vector<relics::Dest> sd;
      for (const Store& k : stores) {
        const i64 have = relicCount(*k.relics, rid);
        sd.push_back(relics::Dest{k.label, have ? "есть " + fmtInt(have) : std::string(), "building"});
      }
      i64 n = 1;
      if (const int k = relics::destPopup(a, "store", "Положить в хранилище", sd, st.count, n, "arch.storeTo." + std::to_string(rid)); k >= 0) {
        const Id pid = stores[size_t(k)].pid, bid = stores[size_t(k)].bid;
        a.play("Положить реликвию в хранилище", [&](Tx& tx) { rules::storeRelic(tx, pid, bid, rid, here, n); });
      }
      x -= 30;
      ui::at(RectF{x, rr.cy() - 14, 28, 28});
      if (ui::iconButton("hero", "Отдать герою", {.size = ui::Size::Small})) ui::openPopup("give");
      a.markUi("arch.relic.give." + std::to_string(rid));
      std::vector<relics::Dest> hd;
      for (const Character* c : heroes) {
        const i64 have = relicCount(c->inventory, rid);
        hd.push_back(relics::Dest{w::orName(c->name, "Без имени"), have ? "есть " + fmtInt(have) : c->title, c->hero ? "hero" : "character"});
      }
      n = 1;
      if (const int k = relics::destPopup(a, "give", "Отдать герою", hd, r.unique() ? 1 : st.count, n, "arch.giveTo." + std::to_string(rid)); k >= 0) {
        const Id cid = heroes[size_t(k)]->id, faction = heroes[size_t(k)]->faction;
        a.play("Отдать реликвию герою", [&](Tx& tx) {
          rules::giveRelic(tx, cid, rid, here);   // проверки правил и запись в хронике — за первую штуку
          if (n > 1) rules::moveRelic(tx, rid, here, rules::RelicPlace{rules::RelicPlace::Hero, cid, 0, faction}, n - 1);
        });
      }
    }
    if (count) {
      x -= relics::kCountW + 6;
      i64 n = st.count;
      if (relics::countField("count", RectF{x, rr.cy() - 15, relics::kCountW, 30}, n))
        a.act("Количество реликвий государства", [&](Tx& tx) { rules::setRelicCount(tx, rid, here, n); },
              {.coalesce = "state.relicCount:" + std::to_string(fid) + ":" + std::to_string(rid)});
      a.markUi("arch.relic.count." + std::to_string(rid));
    }
  }
  if (!edit) return;
  // Новые из справочника: копируемые — всегда, уникальные — пока нигде не лежат.
  std::vector<relics::Source> src;
  for (const Relic& r : w.catalogs->relics)
    if (rules::relicFree(w, r.id)) src.push_back(relics::Source{r.id, rules::RelicPlace{}, 0, 3});
  std::stable_sort(src.begin(), src.end(), [&](const relics::Source& x, const relics::Source& y) {
    const Relic& p = *w.relic(x.relic);
    const Relic& q = *w.relic(y.relic);
    if (p.rarity != q.rarity) return rules::rarityAbove(p.rarity, q.rarity);
    return compareRu(p.name, q.name) < 0;
  });
  const int idx = relics::sourceCombo("add", w, fid, src, src.empty() ? "Свободных реликвий нет" : "Добавить из справочника",
                                      "Новая реликвия в инвентарь государства");
  a.markUi("arch.relic.add");
  if (idx >= 0) {
    const Id rid = src[size_t(idx)].relic;
    a.act("Реликвия в инвентарь государства", [&](Tx& tx) { rules::moveRelic(tx, rid, rules::RelicPlace{}, here, 1); });
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
  const bool ro = !a.canPlay(fid);   // задания и группы — игровые действия за государство
  const float aw = ui::avail().w;
  if (aw >= kWide) {
    const float left = std::clamp(std::round(aw * 0.4f), 360.f, 440.f);
    ui::Row r({ui::px(left), ui::fr(1)}, ui::kAuto, 24);
    {
      ui::Group g(0, 8);
      stats(w, *f);
      groupsSection(a, w, *f, ro);
      relicsSection(a, w, *f);
    }
    {
      ui::Group g(0, 8);
      provincesSection(a, w, *f, ro, aw - left - 24 >= 420);
    }
  } else {
    stats(w, *f);
    groupsSection(a, w, *f, ro);
    relicsSection(a, w, *f);
    provincesSection(a, w, *f, ro, aw >= 420);
  }
}

TabReg tab({kTabFaction, "pickaxe", "Археология", 62, SelType::Faction, fac::isState, drawArch, nullptr, true});

}  // namespace
}  // namespace rg::app
