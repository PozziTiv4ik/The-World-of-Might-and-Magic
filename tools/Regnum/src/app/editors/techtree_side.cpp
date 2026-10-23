// Regnum — дерево технологий: панель свойств справа (технология, связь, обзор дерева): изучение, срок и стоимость,
// требуемые постройки, условия, что открывает, модификаторы.
#include "app/editors/techtree_internal.h"

namespace rg::app::techtree {

namespace {

// Общее дерево: изучение по фракциям — сколько изучено и исследуется; щелчок — изучение за неё.
void learnersSection(App& a, Ed& ed, const std::vector<TN>& ts) {
  const World& w = a.world();
  std::vector<const Faction*> list;
  w.factions.each([&](const Faction& f) {
    if (!f.isWild()) list.push_back(&f);   // «Без государства» технологий не изучает
  });
  std::sort(list.begin(), list.end(), [](const Faction* x, const Faction* y) {
    if (x->kind != y->kind) return x->kind < y->kind;
    return compareRu(x->name, y->name) < 0;
  });
  if (list.empty()) return;
  ui::Section s("Изучение по государствам", "crown", {.defaultOpen = list.size() <= 12, .badge = std::to_string(list.size())});
  if (!s) return;
  for (const Faction* f : list) {
    int studied = 0, research = 0;
    for (const TN& t : ts) {
      const TechProgress p = rules::techState(w, t.id, f->id);
      studied += p.studied;
      research += p.research && !p.studied;
    }
    ui::IdScope fs{i64(f->id)};
    std::string hint = std::to_string(studied) + " / " + std::to_string(ts.size());
    std::string sub = research ? std::to_string(research) + " " + plural(research, "исследуется", "исследуются", "исследуются") : std::string();
    if (ui::listItem(f->name.empty() ? std::string("Без названия") : f->name,
                     {.dot = f->color, .subtitle = sub, .hint = hint, .selected = ed.learner == f->id, .tooltip = "Изучение за это государство"}))
      ed.learner = f->id;
    a.markUi("tt.learner." + std::to_string(f->id));
  }
}

}  // namespace

void sideOverview(App& a, Ed& ed, Id faction, const std::vector<TN>& ts) {
  const World& w = a.world();
  const bool common = faction == 0;
  const Faction* f = w.faction(common ? ed.learner : faction);
  int studied = 0, research = 0, avail = 0;
  for (const TN& t : ts) {
    studied += t.studied;
    research += t.st == St::Research;
    avail += t.st == St::Available;
  }
  {
    ui::Row r({ui::px(54), ui::fr(1)}, 40, 12);
    {
      RectF fr = ui::next(54, 40);
      if (common) {
        ui::draw::rect(RectF{fr.x, fr.y + 2, 54, 36}, ui::theme().accent.alpha(0.14f), 7);
        ui::draw::icon("globe", RectF{fr.x + 15, fr.y + 8, 24, 24}, ui::theme().accent);
      } else {
        ui::at(RectF{fr.x, fr.y + 2, 54, 36});
        ui::flag(ed.flag, 54, 36, 5);
      }
    }
    ui::Group g(0, 0);
    if (common) {
      ui::caption("Общее дерево");
      ui::label(f ? "Изучение за: " + f->name : std::string("Изучать некому"), {.font = ui::Font::Subtitle});
    } else {
      ui::caption(f && f->isGuild() ? "Торговая гильдия" : "Государство");
      ui::label(f ? f->name : std::string("—"), {.font = ui::Font::Subtitle});
    }
  }
  ui::spacer(4);
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 10);
    ui::stat(std::to_string(studied) + " / " + std::to_string(ts.size()), "Изучено", {.icon = "check-circle", .tone = ui::Tone::Success});
    ui::stat(std::to_string(research), "Исследуется", {.icon = "hourglass", .tone = ui::Tone::Info});
  }
  if (!ts.empty()) ui::progress(double(studied) / double(ts.size()), {.tone = ui::Tone::Success, .height = 5, .label = true});
  if (research > 0) {
    if (ui::Section s("Исследуются", "hourglass", {.badge = std::to_string(research)}); s) {
      for (const TN& t : ts) {
        if (t.st != St::Research) continue;
        ui::IdScope sc{i64(t.id)};
        if (ui::listItem(t.name, {.icon = "research", .subtitle = "Осталось " + nTurns(turnsLeft(t.need, t.progress)),
                                  .hint = std::to_string(t.progress) + "/" + std::to_string(t.need)})) {
          ed.sel = t.id;
          ed.revealSel = true;
        }
        ui::progress(double(t.progress) / double(std::max(1, t.need)), {.tone = ui::Tone::Info, .height = 4});
      }
    }
  }
  if (avail > 0) {
    if (ui::Section s("Можно исследовать", "research", {.badge = std::to_string(avail)}); s) {
      for (const TN& t : ts) {
        if (t.st != St::Available) continue;
        ui::IdScope sc{i64(t.id)};
        if (ui::listItem(t.name, {.icon = "research", .hint = nTurns(turnsLeft(t.need, t.progress))})) {
          ed.sel = t.id;
          ed.revealSel = true;
        }
      }
    }
  }
  if (common) learnersSection(a, ed, ts);
  if (ui::Section s("Обозначения", "info", {.defaultOpen = ts.size() < 3}); s) {
    for (St st : {St::Studied, St::Research, St::Available, St::Locked}) {
      ui::IdScope sc{int(st)};
      ui::Row r({ui::px(18), ui::fr(1)}, 22, 8);
      ui::iconColored(stIcon(st), ui::toneColor(stTone(st)), 16);
      ui::label(stName(st), {.ink = ui::Ink::Dim});
    }
  }
}

void sideLink(App& a, Ed& ed, const std::vector<TN>& ts) {
  const TN* pre = find(ts, ed.link.pre);
  const TN* tech = find(ts, ed.link.tech);
  if (!pre || !tech) return;
  ui::caption("Зависимость");
  {
    // Требуемая → зависимая, по строке на каждую (названия бывают длинными).
    ui::Group g(0, 4);
    if (ui::chip(pre->name + "##pre", {.icon = stIcon(pre->st), .tone = stTone(pre->st), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
      ed.sel = pre->id;
      ed.link = {};
      ed.revealSel = true;
    }
    {
      ui::Indent in(10);
      ui::icon("arrow-down", ui::Ink::Muted, 16);
    }
    if (ui::chip(tech->name + "##tech", {.icon = stIcon(tech->st), .tone = stTone(tech->st), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
      ed.sel = tech->id;
      ed.link = {};
      ed.revealSel = true;
    }
  }
  ui::text("Для изучения «" + tech->name + "» нужна «" + pre->name + "».", ui::Font::Small, ui::Ink::Dim);
  ui::spacer(4);
  ui::Disabled d(!a.canEdit());
  if (ui::button("Удалить связь", {.variant = ui::Variant::Danger, .icon = "unlink", .fill = true, .tooltip = "Удалить зависимость (Delete)"})) {
    removeLink(a, ed.link);
    ed.link = {};
  }
  a.markUi("tt.side.unlink");
}

namespace {

// «Открывает постройки» (ТЗ «Доработки», п.4): постройки, которым технология нужна. Общей технологии можно назначить
// любую постройку, технологии государства — только его уникальные постройки (rules::canRequireTech).
void unlocksSection(App& a, const TN& t, bool ro) {
  const World& w = a.world();
  const Tech* rec = w.tech(t.id);
  if (!rec) return;
  const Id tid = t.id, tfaction = rec->faction;
  struct B {
    Id id = 0, owner = 0;
    std::string name;
    const char* icon = nullptr;
    BuildingCat cat = BuildingCat::Economic;
  };
  std::vector<B> have, cand;
  const std::vector<Id> unlocks = rules::techUnlocks(w, tid);
  w.buildings.each([&](const Building& b) {
    B x{b.id, b.owner, b.name.empty() ? std::string("Без названия") : b.name, bld::iconOf(b), b.cat};
    if (std::find(unlocks.begin(), unlocks.end(), b.id) != unlocks.end()) have.push_back(std::move(x));
    else if (rules::canRequireTech(w, b.id, tid)) cand.push_back(std::move(x));
  });
  if (have.empty() && cand.empty()) return;   // некому открывать (гильдия: построек нет)
  ui::Section s("Открывает постройки", "building", {.badge = have.empty() ? std::string() : std::to_string(have.size())});
  a.markUi("tt.side.unlocks");
  if (!s) return;
  if (!have.empty()) {
    ui::IdScope us("unlocks");
    ui::ChipFlow flow(40);
    for (const B& b : have) {
      ui::IdScope s2{i64(b.id)};
      std::string tip = (b.owner ? "Уникальная · " + w.factionName(b.owner) : std::string("Общее дерево")) + " — показать в дереве построек";
      ui::ChipAction act = ui::flowChip(b.name, {.icon = b.icon, .color = bld::catColor(b.cat), .removable = !ro, .clickable = true, .tooltip = tip});
      a.markUi("tt.unlock." + std::to_string(b.id));
      if (act == ui::ChipAction::Click) {
        openBuildingTree(a, b.owner, b.id);
      } else if (act == ui::ChipAction::Remove) {
        const Id bid = b.id;
        a.act("Убрать постройку из открываемых", [&](Tx& tx) { rules::setBuildingTech(tx, bid, tid, false); });
      }
    }
  }
  if (ro || cand.empty()) return;
  // Общей технологии — постройки общего дерева первыми, затем уникальные по государствам.
  std::stable_sort(cand.begin(), cand.end(), [&](const B& x, const B& y) {
    if ((x.owner == 0) != (y.owner == 0)) return x.owner == 0;
    if (x.owner != y.owner) return compareRu(w.factionName(x.owner), w.factionName(y.owner)) < 0;
    if (x.cat != y.cat) return x.cat < y.cat;
    return compareRu(x.name, y.name) < 0;
  });
  std::vector<std::string> hints;
  for (const B& b : cand) hints.push_back(b.owner ? w.factionName(b.owner) : std::string(bld::catName(b.cat)));
  std::vector<ui::Option> opts;
  for (size_t i = 0; i < cand.size(); i++) opts.push_back(ui::Option{cand[i].name, cand[i].icon, Color(0, 0, 0, 0), hints[i]});
  int idx = -1;
  if (ui::combo("addunlock", idx, std::span<const ui::Option>(opts), {.placeholder = "Добавить постройку", .search = 1, .icon = "plus"}) && idx >= 0 &&
      idx < int(cand.size())) {
    const Id bid = cand[size_t(idx)].id;
    a.act(tfaction ? "Технология открывает постройку" : "Общая технология открывает постройку", [&](Tx& tx) { rules::setBuildingTech(tx, bid, tid, true); });
  }
  a.markUi("tt.side.addunlock");
}

// Стоимость исследования (ТЗ «Доработки №4», п.1): ресурсы через группы и количество (rules::setTechCost; 0 — убрать).
void costEditor(App& a, const TN& t, bool ro) {
  const World& w = a.world();
  const Id tid = t.id;
  ui::IdScope scope("cost");
  if (ro && t.cost.empty()) return;
  ui::caption("Стоимость исследования");
  for (auto [res, amount] : t.cost) {
    ui::IdScope rs{i64(res)};
    const Id r = res;
    ui::Row row({ui::px(16), ui::fr(1), ui::px(104), ui::px(30)}, 30, 6);
    const CatalogItem* c = w.resource(r);
    const Color rc = w::resourceColor(w, r);
    ui::iconColored(w::resourceIcon(w, r), w::legible(rc, 0.12f), 16);   // тёмный ресурс — светлее
    ui::label(c && !c->name.empty() ? c->name : std::string("Ресурс"));
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0.001, .max = 1e12, .step = 10, .digits = 3, .unit = w::amountUnit(r), .disabled = ro,
                                      .tooltip = "Количество"}))
      a.act("Стоимость исследования", [&](Tx& tx) { rules::setTechCost(tx, tid, r, v); },
            {.coalesce = "tt-cost:" + std::to_string(tid) + ":" + std::to_string(r)});
    a.markUi("tt.side.cost." + std::to_string(r));
    if (ui::iconButton("close", "Убрать ресурс из стоимости", {.disabled = ro}))
      a.act("Убрать ресурс из стоимости исследования", [&](Tx& tx) { rules::setTechCost(tx, tid, r, 0); });
    a.markUi("tt.side.cost." + std::to_string(r) + ".remove");
  }
  if (ro) return;
  std::vector<Id> left;
  for (const CatalogItem& c : w.catalogs->resources)
    if (!t.cost.count(c.id)) left.push_back(c.id);
  Id pick = 0;
  if (w::resourceByGroup("addcost", pick, false, &left) && pick)
    a.act("Стоимость исследования", [&](Tx& tx) { rules::setTechCost(tx, tid, pick, 100); });
  a.markUi("tt.side.addcost");
}

// Требуемые постройки государства (по ключу встроенной постройки, ТЗ «Доработки №4», п.1): фишки с названием
// (rules::buildingKeyName) и выбор из построек с ключом (rules::setTechNeedKey).
void needsEditor(App& a, const TN& t, Id faction, bool ro) {
  const World& w = a.world();
  const Id tid = t.id;
  // Ключи встроенных построек мира (без повторов).
  std::vector<std::pair<std::string, std::string>> keys;   // ключ, название
  w.buildings.each([&](const Building& b) {
    if (b.key.empty() || std::any_of(keys.begin(), keys.end(), [&](const auto& k) { return k.first == b.key; })) return;
    keys.push_back({b.key, rules::buildingKeyName(w, faction, b.key)});
  });
  if ((keys.empty() || ro) && t.needKeys.empty()) return;
  ui::IdScope scope("needs");
  ui::caption("Нужны постройки");
  if (!t.needKeys.empty()) {
    ui::ChipFlow flow(40);
    for (const std::string& k : t.needKeys) {
      ui::IdScope s2(k);
      const std::string key = k;
      const ui::ChipAction act = ui::flowChip(rules::buildingKeyName(w, faction, k),
                                            {.icon = "building", .tone = ui::Tone::Warning, .removable = !ro, .tooltip = "Должна быть достроена в государстве"});
      a.markUi("tt.side.need." + k);
      if (act == ui::ChipAction::Remove) a.act("Убрать требуемую постройку", [&](Tx& tx) { rules::setTechNeedKey(tx, tid, key, false); });
    }
  }
  if (ro) return;
  std::vector<std::pair<std::string, std::string>> cand;
  for (const auto& k : keys)
    if (std::find(t.needKeys.begin(), t.needKeys.end(), k.first) == t.needKeys.end()) cand.push_back(k);
  if (cand.empty()) return;
  std::stable_sort(cand.begin(), cand.end(), [](const auto& x, const auto& y) { return compareRu(x.second, y.second) < 0; });
  std::vector<ui::Option> opts;
  for (const auto& c : cand) opts.push_back(ui::Option{c.second, "building"});
  int idx = -1;
  if (ui::combo("addneed", idx, std::span<const ui::Option>(opts), {.placeholder = "Добавить постройку", .search = 1, .icon = "plus"}) && idx >= 0 &&
      idx < int(cand.size())) {
    const std::string key = cand[size_t(idx)].first;
    a.act("Требуемая постройка технологии", [&](Tx& tx) { rules::setTechNeedKey(tx, tid, key, true); });
  }
  a.markUi("tt.side.addneed");
}

}  // namespace

void sideTech(App& a, Ed& ed, Id faction, Id who, const std::vector<TN>& ts, const TN& t) {
  const World& w = a.world();
  const bool ro = !a.canEdit();                       // правка дерева — администратору
  const bool noResearch = !canResearch(a, faction, who);   // исследование — и игроку за своё государство
  const bool common = faction == 0;
  const Color none(0, 0, 0, 0);
  ui::IdScope scope{i64(t.id)};
  {
    ui::HStack hs(28, ui::Align::Left, 6);
    ui::tag(stName(t.st), stTone(t.st), stIcon(t.st));
    if (common) ui::tag("Общая", ui::Tone::Accent, "globe");
    ui::flex();
    if (ui::iconButton("target", "Показать на схеме")) ed.revealSel = true;
    if (ui::iconButton("trash", "Удалить технологию", {.disabled = ro, .shortcut = {Key::Delete, 0}, .tone = ui::Tone::Danger})) askDelete(a, t.id, t.name);
    a.markUi("tt.side.delete");
  }
  {
    ui::Disabled dis(ro);
    // Название
    if (ed.focusName == t.id) {
      ui::setKeyboardFocus(ui::id("name"));
      ed.focusName = 0;
    }
    std::string name = t.name;
    if (ui::textField("name", name, {.placeholder = "Название технологии", .maxLength = 80, .selectAllOnFocus = true}) && !trim(name).empty() &&
        trim(name) != t.name) {
      std::string n = trim(name);
      a.act("Переименовать технологию", [&](Tx& tx) { tx.tech(t.id).name = n; });
    }
    a.markUi("tt.side.name");
  }
  {
    // «Галочка» изученности (ТЗ 1.b.v); у общей — за изучающую фракцию.
    ui::Disabled dis(ro || (common && !who));
    bool studied = t.studied;
    if (ui::checkbox("Изучена", studied)) setStudied(a, t.id, studied, who);
    a.markUi("tt.side.studied");
  }
  // Исследование
  {
    ui::Card card({.pad = 12, .tone = stTone(t.st)});
    if (common) {
      if (const Faction* lf = w.faction(who)) {
        ui::HStack hs(22, ui::Align::Left, 6);
        ui::icon(lf->isGuild() ? "guild" : "crown", ui::Ink::Muted, 14, "Изучение за");
        ui::label(lf->name, {.font = ui::Font::Small, .color = lf->color});
      } else {
        ui::label("Изучать некому", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "crown"});
      }
    }
    if (t.studied) {
      ui::label("Изучена", {.ink = ui::Ink::Success, .icon = "check-circle", .wrap = true});
    } else if (t.research) {
      ui::Row r({ui::fr(1), ui::px(60)}, 22, 8);
      ui::label("Исследуется · ещё " + nTurns(turnsLeft(t.need, t.progress)), {.font = ui::Font::Strong, .ink = ui::Ink::Info});
      ui::label(std::to_string(t.progress) + " / " + std::to_string(t.need), {.ink = ui::Ink::Dim, .align = ui::Align::Right});
    } else if (t.st == St::Available) {
      ui::label("Условия выполнены. Изучение займёт " + nTurns(turnsLeft(t.need, t.progress)) + ".", {.ink = ui::Ink::Dim, .wrap = true});
    } else if (!t.missing.empty()) {
      ui::label("Сначала изучите:", {.ink = ui::Ink::Dim, .icon = "lock"});
      ui::IdScope ms("missing");
      ui::ChipFlow flow(40);
      for (Id m : t.missing) {
        ui::IdScope s2{i64(m)};
        if (const TN* mt = find(ts, m)) {
          if (ui::flowChip(mt->name, {.icon = stIcon(mt->st), .tone = stTone(mt->st), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
            ed.sel = m;
            ed.revealSel = true;
          }
        } else if (const Tech* x = w.tech(m)) {
          // Общая технология — условие технологии фракции: состояние для этой фракции, переход в общее дерево.
          St ms2 = stateOf(w, m, faction);
          if (ui::flowChip(w::orName(x->name), {.icon = stIcon(ms2), .tone = stTone(ms2), .clickable = true, .tooltip = "Общее дерево — показать"}) == ui::ChipAction::Click)
            openTechTree(a, 0, m, faction);
        }
      }
    }
    // Не достроены нужные постройки, не хватает ресурсов на стоимость (ТЗ «Доработки №4», п.1).
    if (!t.studied && !t.research)
      for (size_t i = 0; i < t.problems.size(); i++) {
        ui::IdScope ps{int(i)};
        ui::label(t.problems[i], {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
        a.markUi("tt.side.problem." + std::to_string(i));
      }
    // Стоимость: при начале списывается, при остановке возвращается (у идущего — уплачено).
    const Faction* payer = w.faction(common ? who : faction);
    if (!t.cost.empty() && !t.studied) {
      ui::IdScope cs("price");
      bld::costChips(t.cost, t.research ? nullptr : payer, false);
      a.markUi("tt.side.price");
    }
    if (t.research) {
      ui::progress(double(t.progress) / double(std::max(1, t.need)), {.tone = ui::Tone::Info, .height = 6});
      if (ui::button("Остановить исследование", {.icon = "close", .fill = true, .disabled = noResearch,
                                                 .tooltip = t.cost.empty() ? std::string_view() : std::string_view("Уплаченная стоимость вернётся")}))
        stopResearch(a, t.id, who);
      a.markUi("tt.side.research");
    } else if (!t.studied) {
      if (t.progress > 0) ui::progress(double(t.progress) / double(std::max(1, t.need)), {.tone = ui::Tone::Neutral, .height = 4});
      bool can = t.st == St::Available;
      const std::string why = can ? (t.cost.empty() ? std::string("Исследование продвигается на один ход при завершении хода")
                                                    : "Стоимость спишется сразу: " + bld::costText(w, t.cost))
                              : !t.missing.empty()  ? std::string("Не изучены предшествующие технологии")
                              : !t.problems.empty() ? t.problems.front()
                                                    : std::string("Исследование недоступно");
      if (ui::button("Начать исследование", {.variant = ui::Variant::Primary, .icon = "play", .fill = true, .disabled = noResearch || !can, .tooltip = why}))
        startResearch(a, t.id, who);
      a.markUi("tt.side.research");
    }
  }
  {
    ui::Disabled dis(ro);
    // Срок изучения
    ui::prop("Срок изучения", "hourglass");
    int turns = t.turns;
    if (ui::numberField("turns", turns, {.min = 1, .max = 999, .unit = "ход|хода|ходов", .steppers = true}))
      a.act("Срок изучения технологии", [&](Tx& tx) { tx.tech(t.id).turns = std::max(1, turns); }, {.coalesce = "tt-turns:" + std::to_string(t.id)});
    a.markUi("tt.side.turns");
    if (t.need != t.turns) {   // срок с модификатором государства
      ui::label("с модификаторами — " + nTurns(t.need),
                {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Right, .icon = "hourglass",
                 .tooltip = "Модификатор «Время исследования технологий» государства"});
      a.markUi("tt.side.need");
    }
  }
  costEditor(a, t, ro);
  needsEditor(a, t, faction, ro);
  {
    ui::Disabled dis(ro);
    // Бонус (описание)
    const Faction* owner = w.faction(faction);
    ui::caption(common ? "Бонус изучившему" : owner && owner->isGuild() ? "Бонус для гильдии" : "Бонус для государства");
    std::string desc = t.desc;
    if (ui::textArea("desc", desc, 76, {.placeholder = "Что даёт технология: «+10 % к торговле», «осадные орудия»…", .maxLength = 600}) && desc != t.desc)
      a.act("Описание технологии", [&](Tx& tx) { tx.tech(t.id).desc = desc; });
    a.markUi("tt.side.desc");
  }
  // Модификаторы и их эффекты (бонус технологии в расчётах)
  ui::caption("Модификаторы");
  std::vector<Id> mods = t.mods;
  if (tree::modifierList("mods", mods, ro)) a.act("Модификаторы технологии", [&](Tx& tx) { tx.tech(t.id).modifiers = mods; });
  a.markUi("tt.side.mods");
  {
    ui::IdScope fs("fx");
    tree::effectChips(w, t.mods);
  }
  // Зависимости. У технологии фракции условиями могут быть и общие технологии (ТЗ «Доработки», п.7); у общей — только
  // общие.
  if (ui::Section s("Требует", "link", {.badge = t.prereqs.empty() ? std::string() : std::to_string(t.prereqs.size())}); s) {
    if (!t.prereqs.empty()) {
      ui::IdScope ps("pre");
      ui::ChipFlow flow(40);
      for (Id p : t.prereqs) {
        ui::IdScope s2{i64(p)};
        if (const TN* pt = find(ts, p)) {
          ui::ChipAction ca = ui::flowChip(pt->name, {.icon = stIcon(pt->st), .tone = stTone(pt->st), .removable = !ro, .clickable = true, .tooltip = "Выбрать"});
          if (ca == ui::ChipAction::Click) {
            ed.sel = p;
            ed.revealSel = true;
          } else if (ca == ui::ChipAction::Remove) {
            removeLink(a, {p, t.id});
          }
        } else if (const Tech* x = w.tech(p)) {
          St ps2 = stateOf(w, p, faction);
          ui::ChipAction ca = ui::flowChip(w::orName(x->name) + "##common",
                                         {.icon = stIcon(ps2), .tone = stTone(ps2), .removable = !ro, .clickable = true, .tooltip = "Общее дерево — показать"});
          a.markUi("tt.side.pre." + std::to_string(p));
          if (ca == ui::ChipAction::Click) openTechTree(a, 0, p, faction);
          else if (ca == ui::ChipAction::Remove) removeLink(a, {p, t.id});
        }
      }
    } else {
      ui::label("Нет условий — можно изучать сразу.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    }
    // Добавить условие: технологии этого дерева (по порядку), затем — у дерева фракции — общие. Цикл недоступен.
    struct Cand {
      Id id = 0;
      std::string label;
      const char* icon = nullptr;
      bool bad = false, common = false;
    };
    std::vector<Cand> cand;
    for (const TN& o : ts) {
      if (o.id == t.id || std::find(t.prereqs.begin(), t.prereqs.end(), o.id) != t.prereqs.end()) continue;
      cand.push_back(Cand{o.id, o.name.empty() ? std::string("Без названия") : o.name, stIcon(o.st), rules::wouldCycle(w, t.id, o.id), false});
    }
    if (!common) {
      std::vector<Cand> extra;
      w.techs.each([&](const Tech& o) {
        if (o.faction != 0 || std::find(t.prereqs.begin(), t.prereqs.end(), o.id) != t.prereqs.end()) return;
        extra.push_back(Cand{o.id, w::orName(o.name), stIcon(stateOf(w, o.id, faction)), rules::wouldCycle(w, t.id, o.id), true});
      });
      std::stable_sort(extra.begin(), extra.end(), [](const Cand& x, const Cand& y) { return compareRu(x.label, y.label) < 0; });
      for (Cand& c : extra) cand.push_back(std::move(c));
    }
    if (!cand.empty()) {
      std::vector<ui::Option> opts;
      for (const Cand& c : cand)
        opts.push_back(ui::Option{c.label, c.icon, none, c.bad ? std::string_view("цикл") : c.common ? std::string_view("общая") : std::string_view(), c.bad});
      int idx = -1;
      if (ui::combo("addpre", idx, std::span<const ui::Option>(opts), {.placeholder = "Добавить условие", .search = 1, .icon = "plus", .disabled = ro}) && idx >= 0 &&
          idx < int(cand.size())) {
        Id pre = cand[size_t(idx)].id;
        a.act("Связать технологии", [&](Tx& tx) { rules::setPrereq(tx, t.id, pre, true); });
      }
      a.markUi("tt.side.addpre");
    }
  }
  // Открывает: технологии этого дерева; у общей — и технологии фракций, которым она нужна.
  {
    struct Dep {
      Id id = 0, faction = 0;
      std::string name;
      St st = St::Locked;
    };
    std::vector<Dep> deps;
    for (const TN& o : ts)
      if (std::find(o.prereqs.begin(), o.prereqs.end(), t.id) != o.prereqs.end()) deps.push_back(Dep{o.id, faction, o.name, o.st});
    if (common)
      w.techs.each([&](const Tech& o) {
        if (o.faction && std::find(o.prereqs.begin(), o.prereqs.end(), t.id) != o.prereqs.end())
          deps.push_back(Dep{o.id, o.faction, w::orName(o.name), stateOf(w, o.id, o.faction)});
      });
    if (!deps.empty()) {
      if (ui::Section s("Открывает", "arrow-right", {.badge = std::to_string(deps.size())}); s) {
        ui::IdScope ds("deps");
        ui::ChipFlow flow(40);
        for (const Dep& d : deps) {
          ui::IdScope s2{i64(d.id)};
          if (d.faction == faction) {
            if (ui::flowChip(d.name, {.icon = stIcon(d.st), .tone = stTone(d.st), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
              ed.sel = d.id;
              ed.revealSel = true;
            }
          } else {
            const std::string tip = w.factionName(d.faction) + " — показать в её дереве";
            if (ui::flowChip(d.name, {.color = w::factionColor(w, d.faction), .clickable = true, .tooltip = tip}) == ui::ChipAction::Click)
              openTechTree(a, d.faction, d.id);
            a.markUi("tt.dep." + std::to_string(d.id));
          }
        }
      }
    }
  }
  unlocksSection(a, t, ro);
}

}  // namespace rg::app::techtree
