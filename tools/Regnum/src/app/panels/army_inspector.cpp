// Regnum — инспектор войска и флота (ТЗ 1.c.iii–v): значок (портрет главного полководца в круге или фигурка),
// название, флаги фракций; верность −100…100 % с изменением за ход и модификаторы войска со сроками («Общие
// доработки», п.12; «Модификаторы»), кнопка «Мятеж» при отрицательной верности («Механика мятежа», п.1); состав по
// фракциям (союзное войско — отдельные плитки), отряды из резерва своей фракции, герои (портреты в кругах) и главный
// полководец (флотоводец), кнопки «Разделить», «Распустить союз», «Расформировать»; провинция под объектом; события.
// Флот (ТЗ «Доработки №3», п.6): вместимость и вкладка «Войско на борту» с «Высадкой войска»; войско на борту — флот
// вместо провинции. Союзный объект (ТЗ «Доработки №4», п.9): плитки фракций, у лидера — корона, у остальных —
// «Вернуть войско».
#include "app/app_internal.h"
#include "app/flows.h"
#include "app/panels/military.h"
#include "gfx/icons.h"

namespace rg::app {
namespace edkit {   // поток фишек с переносом строк (editors/modifiers.cpp)
void chipsBegin();
ui::ChipAction chip(std::string_view label, const ui::ChipOpt& o);
void chipsEnd();
}  // namespace edkit
}  // namespace rg::app

namespace rg::app::mil {

namespace {

const char* leaderWord(const Army& a) { return a.isFleet() ? "Главный флотоводец" : "Главный полководец"; }

// ---------------------------------------------------------------- шапка
void drawHeader(App& a, Id id) {
  const World& w = frameWorld(a);
  const Army* ar = w.army(id);
  if (!ar) return;
  ui::Row r({ui::px(54), ui::fr(1)}, ui::kAuto, 10);
  const RectF fig = ui::next(54, 54);
  objectBadgeIn(w, *ar, fig, false);   // портрет главного полководца или фигурка
  a.markUi("army.figure", fig);
  ui::Group g(0, 2);
  std::string cap = objectCaption(*ar);
  std::string name = objectName(*ar);
  // Название совпадает с видом («Союзное войско») — в подписи провинция.
  if (utf8::searchKey(name) == utf8::searchKey(cap)) {
    Id pid = provinceUnder(w, ar->pos);
    cap = pid ? w.provinceName(pid) : std::string(ar->isFleet() ? "Открытое море" : "Вне провинций");
  }
  if (const Id fl = rules::carrierOf(w, id)) cap = "На борту «" + objectName(*w.army(fl)) + "»";
  ui::caption(cap);
  {
    float aw = ui::avail().w;
    const float full = ui::measure(name, ui::Font::Heading) + 2;
    float nw = std::min(full, std::max(40.f, aw - 34));
    ui::Row nr({ui::px(nw), ui::px(26)}, 32, 4);
    ui::label(name, {.font = ui::Font::Heading, .tooltip = full > nw ? std::string_view(name) : std::string_view()});   // не поместилось — полностью
    ui::Disabled dis(a.readOnly());
    if (ui::iconButton("edit", "Переименовать", {.size = ui::Size::Small, .shortcut = {platform::Key::F2, 0}})) {
      bool fleet = ar->isFleet();
      a.prompt(fleet ? "Переименовать флот" : "Переименовать войско", "Название", ar->name, [id, fleet](App& x, const std::string& v) {
        x.act(fleet ? "Переименовать флот" : "Переименовать войско", [&](Tx& tx) { rules::renameArmy(tx, id, v); });
      });
    }
    a.markUi("army.rename");
  }
  std::vector<Id> fs = factionsIn(*ar);
  if (fs.size() == 1) {
    factionLabel(w, fs[0], 22);
  } else {
    ui::HStack hs(24, ui::Align::Left, 6);
    for (Id f : fs) {
      ui::IdScope s{i64(f)};
      factionFlag(w, f, 26, 17);
    }
    ui::label(std::to_string(fs.size()) + " " + plural(i64(fs.size()), "фракция", "фракции", "фракций") + " в союзе",
              {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
}

// ---------------------------------------------------------------- отряды группы
struct AddUnit {
  int row = -1;
  i64 count = 0;
  Id lastRow = 0;
};

void setCount(App& a, const Army& ar, Id faction, Id row, i64 n) {
  Id id = ar.id;
  a.act(ar.isFleet() ? "Корабли во флоте" : "Отряды в войске", [&](Tx& tx) { rules::setUnits(tx, id, faction, row, n); },
        {.coalesce = "units:" + std::to_string(id) + ":" + std::to_string(row)});
}

void unitsTable(App& a, const Army& ar, const ArmyGroup& g) {
  const World& w = frameWorld(a);
  bool fleet = ar.isFleet();
  bool ro = a.readOnly();
  auto c = rules::calc(w);
  struct L {
    UnitRow row;
    i64 count;
    i64 reserve;
  };
  std::vector<L> lines;
  for (const ArmyUnit& u : g.units) {
    auto r = unitRow(w, g.faction, u.row, fleet);
    if (!r) continue;
    const rules::RowCalc* rc = rowCalc(*c, g.faction, u.row, fleet);
    lines.push_back(L{*r, u.count, rc ? std::max<i64>(0, rc->reserve) : 0});
  }
  if (!lines.empty()) {
    ui::Column cols[] = {{fleet ? "Судно" : "Отряд", nullptr, ui::fr(1, 100)},
                         {"Численность", nullptr, ui::px(100), ui::Align::Left, false, "Назначено из резерва фракции"},
                         {"Резерв", nullptr, ui::px(64), ui::Align::Right, false, "Свободно в резерве фракции"},
                         {"", nullptr, ui::px(36)}};
    ui::Table t("units", cols, int(lines.size()), {.rowHeight = 40, .selectable = false});
    for (int i : t) {
      const L& l = lines[size_t(i)];
      RectF cr = t.cell();
      unitCell(cr, l.row, false, l.row.name != l.row.typeName ? std::string_view(l.row.typeName) : std::string_view());
      ui::Disabled dis(ro);
      t.cell();
      i64 n = l.count;
      if (ui::numberField("n", n, {.min = 0, .max = double(l.count + l.reserve),
                                   .tooltip = "Не больше резерва: " + fmtCount(l.reserve) + " свободно"}))
        setCount(a, ar, g.faction, l.row.id, n);
      a.markUi("army.unit." + std::to_string(g.faction) + "." + std::to_string(l.row.id));
      t.text(fmtCount(l.reserve), l.reserve > 0 ? ui::Ink::Success : ui::Ink::Muted);
      t.cell();
      if (ui::iconButton("close", fleet ? "Вернуть корабли в резерв" : "Вернуть отряд в резерв")) setCount(a, ar, g.faction, l.row.id, 0);
    }
    if (t.footer()) {
      t.text("Итого");
      t.text(fmtCount(groupCount(g)));
    }
  }
  // Добавить строку из резерва своей фракции (ТЗ 1.c.iii: только из списка государства и не больше резерва).
  std::vector<UnitRow> all = unitRows(w, g.faction, fleet);
  std::vector<UnitRow> avail;
  for (const UnitRow& r : all)   // торговые галеоны к флотам на карте не присоединяются (ТЗ «Доработки №3», п.6)
    if (rowCount(g, r.id) == 0 && !(fleet && r.type == int(ShipType::Galleon))) avail.push_back(r);
  ui::Disabled dis(ro);
  if (all.empty()) {
    ui::HStack hs(0, ui::Align::Left, 6);
    ui::label(std::string("В таблице ") + (fleet ? "флота" : "войск") + " фракции нет строк", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "info"});
    if (ui::link("Открыть", fleet ? "fleet" : "army")) {
      a.ui.tabOf[SelType::Faction] = fleet ? "faction.fleet" : "faction.army";
      a.select(SelType::Faction, g.faction);
    }
    return;
  }
  if (avail.empty()) return;
  if (lines.empty())
    ui::label(fleet ? "Кораблей нет" : "Отрядов нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  auto& st = ui::state<AddUnit>(ui::id("addunit"));
  if (ui::button(fleet ? "Корабли из резерва" : "Отряд из резерва", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small}))
    ui::openPopup("addunit");
  a.markUi("army.addunit." + std::to_string(g.faction));
  if (ui::beginPopup("addunit", {.width = 300})) {
    ui::caption(std::string("Резерв · ") + w.factionName(g.faction));
    std::vector<std::string> hints;
    std::vector<i64> reserves;
    for (const UnitRow& r : avail) {
      i64 res = reserveOf(w, g.faction, r.id, fleet);
      reserves.push_back(res);
      hints.push_back(fmtCount(res));
    }
    std::vector<ui::Option> opts;
    for (size_t i = 0; i < avail.size(); i++) opts.push_back(ui::Option{avail[i].name, avail[i].icon, Color(0, 0, 0, 0), hints[i], reserves[i] <= 0});
    if (st.row < 0 || st.row >= int(avail.size()) || reserves[size_t(st.row)] <= 0) {
      st.row = -1;
      for (size_t i = 0; i < avail.size(); i++)
        if (reserves[i] > 0) {
          st.row = int(i);
          break;
        }
    }
    int before = st.row;
    ui::combo("row", st.row, opts, {.placeholder = "Строка таблицы"});
    a.markUi("army.addunit.row");
    i64 res = st.row >= 0 ? reserves[size_t(st.row)] : 0;
    Id rowId = st.row >= 0 ? avail[size_t(st.row)].id : 0;
    if (st.row != before || rowId != st.lastRow) st.count = res;   // по умолчанию — весь резерв строки
    st.lastRow = rowId;
    st.count = clamp<i64>(st.count, 0, res);
    ui::prop("Численность", fleet ? "fleet" : "users");
    ui::numberField("count", st.count, {.min = 0, .max = double(res), .steppers = true, .tooltip = "Не больше резерва: " + fmtCount(res)});
    a.markUi("army.addunit.count");
    if (res <= 0) ui::label("В резерве нет свободных отрядов", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
    if (ui::button("Назначить", {.variant = ui::Variant::Primary, .icon = "check", .fill = true, .disabled = rowId == 0 || st.count <= 0})) {
      Id id = ar.id, fac = g.faction;
      i64 n = st.count;
      if (a.act(fleet ? "Корабли во флоте" : "Отряды в войске", [&](Tx& tx) { rules::setUnits(tx, id, fac, rowId, n); })) ui::closePopup();
    }
    a.markUi("army.addunit.ok");
    ui::endPopup();
  }
}

// ---------------------------------------------------------------- герои
void setHeroes(App& a, const Army& ar, Id character, bool on) {
  Id id = ar.id;
  a.act(on ? "Герой в войске" : "Убрать героя", [&](Tx& tx) {
    rules::setHero(tx, id, character, on);
    // Главный полководец — всегда один из героев, если они есть.
    const Army* cur = tx.w().army(id);
    if (cur && cur->commander == 0)
      for (const ArmyGroup& g : cur->groups)
        if (!g.heroes.empty()) {
          rules::setCommander(tx, id, g.heroes.front());
          break;
        }
  });
}

void heroesList(App& a, const Army& ar, const ArmyGroup& g) {
  const World& w = frameWorld(a);
  bool ro = a.readOnly();
  for (Id h : g.heroes) {
    const Character* ch = w.character(h);
    if (!ch) continue;
    ui::IdScope s{i64(h)};
    bool cmd = ar.commander == h;
    ui::Row r({ui::px(22), ui::px(30), ui::fr(1), ui::px(30)}, 38, 8);
    {
      ui::Disabled dis(ro);
      int v = cmd ? 1 : 0;
      if (ui::radio("##cmd", v, 1)) {
        Id id = ar.id;
        a.act(ar.isFleet() ? "Главный флотоводец" : "Главный полководец", [&](Tx& tx) { rules::setCommander(tx, id, h); });
      }
      ui::tooltip(leaderWord(ar));
      a.markUi("army.cmd." + std::to_string(h));
    }
    w::heroAvatar(*ch, 28, cmd);   // портрет в круге (лицо — ближе к верху), без портрета — инициалы
    a.markUi("army.hero." + std::to_string(h));
    {
      ui::Group gg(0, 0);
      const std::string name = ch->name.empty() ? std::string("Без имени") : ch->name;
      const ui::Font nf = cmd ? ui::Font::Strong : ui::Font::Body;
      const float room = ui::avail().w;
      ui::label(name, {.font = nf, .tooltip = ui::measure(name, nf) > room ? std::string_view(name) : std::string_view()});
      ui::label(cmd ? std::string(leaderWord(ar)) : (ch->title.empty() ? std::string("Герой") : ch->title),
                {.font = ui::Font::Caption, .ink = cmd ? ui::Ink::Accent : ui::Ink::Muted});
    }
    ui::Disabled dis(ro);
    if (ui::iconButton("close", "Убрать героя из войска")) setHeroes(a, ar, h, false);
  }
  if (ro) return;
  // Добавить героя: доступные персонажи этой фракции (не «Мертв» и не «Взят в плен»), не сопровождающие другие объекты.
  std::vector<const Character*> list;
  w.characters.each([&](const Character& c) {
    if (c.faction != g.faction || !rules::heroAvailable(w, c.id)) return;
    for (const ArmyGroup& x : ar.groups)
      if (std::find(x.heroes.begin(), x.heroes.end(), c.id) != x.heroes.end()) return;
    list.push_back(&c);
  });
  std::sort(list.begin(), list.end(), [](const Character* x, const Character* y) {
    if (x->hero != y->hero) return x->hero;
    return compareRu(x->name, y->name) < 0;
  });
  // Последний пункт — новый герой фракции (сразу в составе; если полководца нет — он и главный). Герой в другом войске
  // или в гарнизоне провинции виден, но не выбирается.
  std::vector<std::string> hints;
  std::vector<Id> ids;
  std::vector<bool> busy;
  for (const Character* c : list) {
    const std::string at = heroBusy(w, c->id, ar.id);
    busy.push_back(!at.empty());
    ids.push_back(c->id);
    hints.push_back(!at.empty() ? at : (c->title.empty() ? std::string(c->hero ? "герой" : "") : c->title));
  }
  Color fc = w::factionColor(w, g.faction);
  std::vector<ui::Option> opts;
  for (size_t i = 0; i < list.size(); i++)
    opts.push_back(ui::Option{list[i]->name, list[i]->hero ? "hero" : "character", fc, hints[i], bool(busy[i])});
  opts.push_back(ui::Option{"Новый герой", "user-plus", Color(0, 0, 0, 0), {}, false});
  int idx = -1;
  if (ui::combo("addhero", idx, opts, {.placeholder = "Добавить героя", .search = 1, .icon = "user-plus"})) {
    if (idx >= 0 && idx < int(ids.size()) && !busy[size_t(idx)]) {
      setHeroes(a, ar, ids[size_t(idx)], true);
    } else if (idx == int(ids.size())) {
      Id id = ar.id, fac = g.faction, nh = 0;
      a.act("Новый герой в войске", [&](Tx& tx) {
        nh = rules::createCharacter(tx, fac, "Новый герой");
        tx.character(nh).hero = true;
        rules::setHero(tx, id, nh, true);
        if (!tx.w().army(id)->commander) rules::setCommander(tx, id, nh);
      });
    }
  }
  a.markUi("army.addhero." + std::to_string(g.faction));
}

// ---------------------------------------------------------------- плитка фракции
void groupTile(App& a, const Army& ar, const ArmyGroup& g) {
  const World& w = frameWorld(a);
  ui::IdScope s(i64(g.faction) + 0x60000000LL);
  {
    ui::Card card({.pad = 12});
    {
      i64 n = groupCount(g);
      std::string cnt = fmtCount(n);
      float cw = ui::measure(cnt, ui::Font::Strong) + 4;
      // Союзный объект (ТЗ «Доработки №4», п.9): лидер — корона (управление), остальные — «Вернуть войско».
      const bool leader = ar.allied() && g.faction == ar.leader();
      const bool back = ar.allied() && !leader;
      const ui::Len cols[] = {ui::px(30), ui::fr(1), ui::px(cw), ui::px(26)};
      ui::Row head(std::span<const ui::Len>(cols, ar.allied() ? 4 : 3), 26, 10);
      factionFlag(w, g.faction, 30, 20);
      const Faction* f = w.faction(g.faction);
      if (f && f->isWild()) ui::label(f->name, {.font = ui::Font::Strong, .tooltip = "Войска без государства: враждебны всем государствам"});
      else if (ui::link(f ? (f->name.empty() ? std::string("Без названия") : f->name) : std::string("—"))) a.select(SelType::Faction, g.faction);
      ui::label(cnt, {.font = ui::Font::Strong, .align = ui::Align::Right,
                      .tooltip = ar.isFleet() ? "Кораблей этой фракции" : "Воинов этой фракции"});
      if (leader) {
        RectF ic = ui::next(26, 26);
        ui::at(RectF{ic.x + 4, ic.y + 4, 18, 18});
        ui::icon("crown", ui::Ink::Accent, 18, ar.isFleet() ? "Управляет союзным флотом" : "Управляет союзным войском");
      } else if (back) {
        std::string why;
        const bool can = rules::canReturnGroup(w, ar.id, g.faction, &why);
        const char* tip = ar.isFleet() ? "Вернуть флот" : "Вернуть войско";
        ui::Disabled dis(a.readOnly() || !can);
        if (ui::iconButton("group-leave", can ? std::string(tip) : std::string(tip) + "\n" + why, {.size = ui::Size::Small})) {
          const Id army = ar.id, fac = g.faction;
          Id nid = 0;
          if (a.act(tip, [&](Tx& tx) { nid = rules::returnGroup(tx, army, fac); })) {
            a.toast(std::string(ar.isFleet() ? "Флот возвращён: «" : "Войско возвращено: «") + objectName(*a.world().army(nid)) + "»", ToastKind::Success, "group-leave");
          }
        }
        a.markUi("army.return." + std::to_string(g.faction));
      }
    }
    unitsTable(a, ar, g);
    ui::spacer(2);
    {
      ui::HStack hs(18, ui::Align::Left, 6);
      ui::caption("Герои");
      if (ar.commander == 0 && &g == &ar.groups.front()) {
        bool any = false;
        for (const ArmyGroup& x : ar.groups) any = any || !x.heroes.empty();
        if (!any) ui::badge(ar.isFleet() ? "нет флотоводца" : "нет полководца", ui::Tone::Warning);
      }
    }
    heroesList(a, ar, g);
  }
  // Полоска цвета фракции слева на плитке.
  RectF cr = ui::lastItem().rect;
  ui::draw::rect(RectF{cr.x, cr.y + 12, 3, std::max(0.f, cr.h - 24)}, w::factionColor(w, g.faction), 1.5f);
}

// ---------------------------------------------------------------- верность и модификаторы
// Откуда изменение верности за ход: модификаторы войска, государства-лидера (его технологии, совет, голод),
// «Непреклонный лоялист» у героев, «Армия нежити».
void loyaltySources(const World& w, const Army& ar, double delta) {
  ui::label("Верность за ход: " + fmtSigned(delta, 1) + " %", {.font = ui::Font::Strong});
  if (rules::armyHas(w, ar.id, schema::mod::UndeadArmy)) {
    ui::label("Армия нежити — всегда 100 %", {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "skull"});
    return;
  }
  const rules::Effects fx = rules::armyEffects(w, ar.id);
  for (const rules::EffectSource& s : fx.sources) {
    const Modifier* m = s.modifier ? w.modifier(s.modifier) : (s.key.empty() ? nullptr : rules::builtinMod(w, s.key));
    if (!m || !m->has(Fx::LoyaltyPerTurn)) continue;
    std::string from;
    switch (s.kind) {
      case rules::EffectSource::Army: from = "войско"; break;
      case rules::EffectSource::Tech:
        if (const Tech* t = w.tech(s.id)) from = "технология «" + t->name + "»";
        break;
      case rules::EffectSource::Building:
        if (const Building* b = w.building(s.id)) from = "постройка «" + b->name + "»";
        break;
      default: from = w.factionName(s.id); break;
    }
    const double v = m->get(Fx::LoyaltyPerTurn);
    ui::label(fmtSigned(v, 1) + " % · " + (m->name.empty() ? std::string("Модификатор") : m->name) + (from.empty() ? "" : " (" + from + ")"),
              {.font = ui::Font::Small, .ink = v >= 0 ? ui::Ink::Success : ui::Ink::Danger});
  }
  int loyalists = 0;
  for (const ArmyGroup& g : ar.groups)
    for (Id h : g.heroes)
      if (rules::characterHas(w, h, schema::mod::Loyalist)) loyalists++;
  if (loyalists)
    ui::label(fmtSigned(schema::kLoyalistBonus * loyalists, 1) + " % · непреклонный лоялист" + (loyalists > 1 ? " ×" + std::to_string(loyalists) : "") +
                  ", верность не уменьшается",
              {.font = ui::Font::Small, .ink = ui::Ink::Success});
}

// Модификатор для добавления войску: запись мира (id) или встроенный, которого в мире ещё нет (key).
struct ModChoice {
  Id id = 0;
  std::string key, name, hint;
  const char* icon = "sparkles";
  Color color{0, 0, 0, 0};
  bool disabled = false;
};

std::vector<ModChoice> modChoices(const World& w, const Army& ar) {
  std::vector<ModChoice> out;
  const bool cruel = rules::armyHas(w, ar.id, schema::mod::DemonArmy) || rules::armyHas(w, ar.id, schema::mod::Ruthless);
  auto add = [&](Id id, const Modifier& m) {
    ModChoice c;
    c.id = id;
    c.key = m.key;
    c.name = m.name.empty() ? std::string("Модификатор") : m.name;
    c.icon = m.icon.empty() || !gfx::hasIcon(m.icon) ? "sparkles" : m.icon.c_str();
    c.color = m.color;
    if (m.key == schema::mod::Sadism && !cruel) {   // ТЗ «Модификаторы», 1.19
      c.disabled = true;
      c.hint = "армии демонов и безжалостные";
    } else if (m.duration > 0) {
      c.hint = nTurns(m.duration);
    }
    out.push_back(std::move(c));
  };
  w.modifiers.each([&](const Modifier& m) {
    if (std::find(ar.modifiers.begin(), ar.modifiers.end(), m.id) != ar.modifiers.end()) return;
    if (!w::modifierFits(m, w::ModScope::Army) || w::modifierInert(m, w::ModScope::Army)) return;
    add(m.id, m);
  });
  for (const Modifier& t : schema::builtinModifiers())
    if (t.kind == ModKind::Army && !rules::builtinModId(w, t.key)) add(0, t);
  std::sort(out.begin(), out.end(), [](const ModChoice& x, const ModChoice& y) { return compareRu(x.name, y.name) < 0; });
  return out;
}

void setArmyMods(App& a, Id army, const char* label, const std::function<void(Tx&, std::vector<Id>&)>& edit) {
  a.act(label, [&](Tx& tx) {
    std::vector<Id> mods = tx.w().army(army)->modifiers;
    edit(tx, mods);
    rules::setModifiers(tx, rules::ModTarget::Army, army, mods);
  });
}

// Модификаторы войска фишками: срок в ходах на фишке (щелчок — правка срока), крестик — снять, «Добавить».
void armyModifiers(App& a, const Army& ar) {
  const World& w = frameWorld(a);
  const bool ro = a.readOnly();
  const Id id = ar.id;
  ui::IdScope scope("mods");
  bool any = false;
  for (Id mid : ar.modifiers) any = any || w.modifier(mid);
  if (any) {
    edkit::chipsBegin();
    for (size_t i = 0; i < ar.modifiers.size(); i++) {
      const Id mid = ar.modifiers[i];
      const Modifier* m = w.modifier(mid);
      if (!m) continue;
      ui::IdScope s{i64(mid)};
      auto it = ar.modTurns.find(mid);
      const int left = it == ar.modTurns.end() ? 0 : it->second;
      std::string tip;
      for (int f = 0; f < kFxCount; f++)
        if (m->has(Fx(f))) tip += (tip.empty() ? "" : "\n") + w::effectText(Fx(f), m->fx[size_t(f)]);
      if (!m->desc.empty()) tip = m->desc + (tip.empty() ? "" : "\n" + tip);
      tip = (left > 0 ? "Осталось: " + nTurns(left) : std::string("Бессрочно")) + (tip.empty() ? "" : "\n" + tip);
      ui::ChipOpt co;
      co.icon = m->icon.empty() || !gfx::hasIcon(m->icon) ? "sparkles" : m->icon.c_str();
      co.color = m->color;
      co.removable = !ro;
      co.clickable = true;
      co.tooltip = tip;
      const std::string name = (m->name.empty() ? std::string("Модификатор") : m->name) + (left > 0 ? " · " + std::to_string(left) : std::string());
      ui::ChipAction act = edkit::chip(name, co);
      a.markUi("army.mods.chip." + std::to_string(i));
      if (act == ui::ChipAction::Remove) {
        setArmyMods(a, id, "Снять модификатор войска", [mid](Tx&, std::vector<Id>& mods) { mods.erase(std::remove(mods.begin(), mods.end(), mid), mods.end()); });
        break;
      }
      if (act == ui::ChipAction::Click) ui::openPopup("term");
      if (ui::beginPopup("term", {.side = ui::Side::Below, .width = 280})) {
        ui::label(m->name.empty() ? std::string("Модификатор") : m->name, {.font = ui::Font::Strong, .icon = co.icon});
        ui::prop("Срок", "hourglass");
        int n = left;
        if (ui::numberField("turns", n, {.min = 0, .max = 1000, .unit = "ход|хода|ходов", .steppers = true, .disabled = ro,
                                         .tooltip = "Ходов действия; 0 — бессрочно"}))
          a.act("Срок модификатора войска", [&](Tx& tx) { rules::setModTurns(tx, rules::ModTarget::Army, id, mid, std::max(0, n)); },
                {.coalesce = "armymod:" + std::to_string(id) + ":" + std::to_string(mid)});
        a.markUi("army.mods.term");
        if (ui::button("Открыть в редакторе", {.icon = "sparkles", .fill = true})) {
          ui::closePopup();
          a.openEditor("modifiers", mid);
        }
        ui::endPopup();
      }
    }
    edkit::chipsEnd();
  }
  if (ro) return;
  std::vector<ModChoice> choices = modChoices(w, ar);
  if (choices.empty()) return;
  std::vector<ui::Option> opts;
  for (const ModChoice& c : choices) opts.push_back(ui::Option{c.name, c.icon, c.color, c.hint, c.disabled});
  int idx = -1;
  if (ui::combo("add", idx, opts, {.placeholder = "Добавить модификатор", .search = 1, .icon = "plus"}) && idx >= 0 && idx < int(choices.size()) &&
      !choices[size_t(idx)].disabled) {
    const ModChoice c = choices[size_t(idx)];
    setArmyMods(a, id, "Модификатор войска", [c](Tx& tx, std::vector<Id>& mods) { mods.push_back(c.id ? c.id : rules::ensureBuiltinMod(tx, c.key)); });
  }
  a.markUi("army.mods.add");
}

// Верность войска −100…100 % (ТЗ «Общие доработки», п.12), изменение за ход, модификаторы; при верности ниже нуля —
// «Мятеж» (ТЗ «Механика мятежа», п.1).
void loyaltyCard(App& a, const Army& ar) {
  const World& w = frameWorld(a);
  const bool ro = a.readOnly();
  const Id id = ar.id;
  const bool undead = rules::armyHas(w, id, schema::mod::UndeadArmy);
  const double delta = rules::loyaltyDelta(w, id);
  ui::IdScope scope("loyalty");
  ui::Card card({.pad = 12, .tone = ar.loyalty < 0 ? ui::Tone::Danger : ui::Tone::Neutral});
  {
    ui::Row r({ui::fr(1), ui::px(112), ui::px(92)}, 30, 8);
    ui::label("Верность", {.font = ui::Font::Strong, .icon = "heart"});
    {
      ui::Disabled dis(ro || undead);
      double v = ar.loyalty;
      if (ui::numberField("value", v, {.min = schema::kMinLoyalty, .max = schema::kMaxLoyalty, .step = 5, .digits = 1, .unit = "%",
                                       .tooltip = undead ? "Армия нежити — верность всегда 100 %" : "Верность войска: от −100 до 100 %"}))
        a.act("Верность войска", [&](Tx& tx) { rules::setArmyLoyalty(tx, id, v); }, {.coalesce = "loyalty:" + std::to_string(id)});
      a.markUi("army.loyalty");
    }
    const std::string d = (std::fabs(delta) < 1e-9 ? std::string("0") : fmtSigned(delta, 1)) + " %/ход";
    ui::label(d, {.font = ui::Font::Small, .ink = delta > 1e-9 ? ui::Ink::Success : delta < -1e-9 ? ui::Ink::Danger : ui::Ink::Muted,
                  .align = ui::Align::Right, .tooltip = "Изменение верности за ход"});
    a.markUi("army.loyalty.delta");
    if (ui::beginTooltip(320)) {
      loyaltySources(w, ar, delta);
      ui::endTooltip();
    }
  }
  ui::meter(ar.loyalty, {.label = false});
  armyModifiers(a, ar);
  if (ar.loyalty < 0) {
    std::string why;
    const bool can = rules::canMutiny(w, id, &why);
    {
      ui::Disabled dis(ro || !can);
      if (ui::button("Мятеж", {.variant = ui::Variant::Danger, .icon = "rebellion", .fill = true,
                               .tooltip = can ? std::string("Неверная часть войск государства в провинции отделится и восстанет") : why}))
        flow::startMutiny(a, id);
      a.markUi("army.mutiny");
    }
    if (!can && !ro) ui::label(why, {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "info", .wrap = true});   // почему недоступно
  }
}

// ---------------------------------------------------------------- мятеж
// После мятежа: битва мятежников с верными, или (войско восстало целиком) судьба верных героев и штурм/захват;
// затем — объявленная война (вассалитет).
void runMutiny(App& a, Id army) {
  const World before = a.world();
  const Army* ar = before.army(army);
  if (!ar) return;
  const Id origin = ar->leader();
  const Id pid = provinceUnder(before, ar->pos);
  rules::MutinyResult res;
  if (!a.act("Мятеж: " + objectName(*ar), [&](Tx& tx) { res = rules::mutiny(tx, army); })) return;
  const World& w = a.world();
  a.toast("Мятеж: " + w.factionName(res.rebelState) + (res.full ? " — войско восстало целиком" : ""), ToastKind::Warning, "rebellion");
  const Id rebelState = res.rebelState, rebel = res.rebelArmy;
  auto war = [rebelState, origin](App& x) {
    if (x.world().faction(rebelState) && x.world().faction(origin)) flow::afterWarDeclared(x, rebelState, origin);
  };
  if (rebel && res.loyalArmy && w.army(rebel) && w.army(res.loyalArmy)) {
    // Мятежники нападают на верных (ТЗ «Мятеж», п.1.1).
    a.select(SelType::Army, rebel);
    openBattle(a, rebel, res.loyalArmy, w.army(rebel)->pos, {}, war);
    return;
  }
  auto aftermath = [rebel, war](App& x) {
    if (x.world().army(rebel)) flow::rebelAftermath(x, rebel, war);
    else war(x);
  };
  if (rebel && w.army(rebel)) a.select(SelType::Army, rebel);
  // Войско восстало целиком (п.1.2): его верные герои — «Судьба героя», пленившее — мятежное государство (и когда
  // в провинции остался верный гарнизон: героям не к кому присоединиться).
  if (!res.loyalHeroes.empty()) flow::openHeroFate(a, res.loyalHeroes, rebelState, pid, aftermath);
  else aftermath(a);
}

// ---------------------------------------------------------------- вкладка «Состав»
void drawUnits(App& a, Id id) {
  const World& w = frameWorld(a);
  const Army* ar = w.army(id);
  if (!ar) return;
  bool fleet = ar->isFleet();
  bool ro = a.readOnly();
  i64 heroes = 0;
  for (const ArmyGroup& g : ar->groups) heroes += i64(g.heroes.size());
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
    i64 n = unitCount(*ar);
    ui::stat(fmtCount(n), fleet ? plural(n, "корабль", "корабля", "кораблей") : plural(n, "воин", "воина", "воинов"),
             {.icon = fleet ? "fleet" : "army", .tone = ui::Tone::Accent});
    std::string cmd = ar->commander ? w.characterName(ar->commander) : std::string();
    ui::stat(std::to_string(heroes), plural(heroes, "герой", "героя", "героев"),
             {.icon = "commander", .tone = ui::Tone::Info,
              .tooltip = ar->commander ? std::string(leaderWord(*ar)) + ": " + cmd : std::string(ar->isFleet() ? "Флотоводец не назначен" : "Полководец не назначен")});
  }
  const Id carrier = rules::carrierOf(w, id);   // войско на борту флота (ТЗ «Доработки №3», п.6)
  if (carrier) {
    ui::prop("На борту", "embark");
    if (ui::link(objectName(*w.army(carrier)), "fleet")) a.select(SelType::Army, carrier);
    a.markUi("army.carrier");
  } else {
    ui::prop(fleet ? "Воды" : "Провинция", "map-pin");
    Id pid = provinceUnder(w, ar->pos);
    if (pid) w::provinceChip(pid);
    else ui::label(fleet ? "открытое море" : "вне провинций", {.ink = ui::Ink::Muted});
  }
  if (fleet && (rules::fleetCapacity(w, id) > 0 || rules::cargoOf(w, id))) capacityRow(a, id);
  loyaltyCard(a, *ar);
  // Действия: войско на борту вместо «Разделить» — «Высадка войска»; флот с войском на борту не расформировывается.
  const bool loaded = fleet && rules::cargoOf(w, id);
  auto splitOrLand = [&] {
    if (carrier) {
      if (ui::button("Высадка", {.icon = "disembark", .fill = true, .tooltip = "Высадить войско на сушу у флота"})) startLanding(a, carrier);
      a.markUi("army.land");
      return;
    }
    if (ui::button("Разделить", {.icon = "split", .fill = true, .tooltip = "Выделить часть в отдельный объект"})) openSplit(a, id);
    a.markUi("army.split");
  };
  const char* disbandTip = loaded ? "На борту войско — сначала высадите его" : nullptr;
  {
    ui::Disabled dis(ro);
    if (ar->allied()) {
      ui::Row r({ui::fr(1), ui::fr(1.25f), ui::px(36)}, 30, 8);
      splitOrLand();
      if (ui::button("Распустить союз", {.icon = "dissolve", .fill = true,
                                         .tooltip = fleet ? "Флоты фракций станут отдельными объектами рядом" : "Войска фракций станут отдельными объектами рядом"}))
        dissolveAllied(a, id);
      a.markUi("army.dissolve");
      if (ui::iconButton("disband", disbandTip ? disbandTip : fleet ? "Расформировать флот" : "Расформировать войско",
                         {.disabled = loaded, .shortcut = {platform::Key::Delete, 0}, .tone = ui::Tone::Danger}))
        askDisband(a, id);
      a.markUi("army.disband");
    } else {
      ui::Row r({ui::fr(1), ui::fr(1)}, 30, 8);
      splitOrLand();
      if (ui::button("Расформировать", {.variant = ui::Variant::Danger, .icon = "disband", .fill = true, .disabled = loaded,
                                        .tooltip = disbandTip ? disbandTip : fleet ? "Убрать флот с карты, корабли — в резерв" : "Убрать войско с карты, отряды — в резерв",
                                        .shortcut = {platform::Key::Delete, 0}}))
        askDisband(a, id);
      a.markUi("army.disband");
    }
  }
  ui::spacer(2);
  for (const ArmyGroup& g : ar->groups) groupTile(a, *ar, g);
}

// ---------------------------------------------------------------- вкладка флота «Войско на борту»
// ТЗ «Доработки №3», п.6: вместимость (занято / всего), войско на борту — значок, название, численность, его отряды и
// герои по фракциям (правятся как у войска), «Высадка войска».
void drawCargo(App& a, Id id) {
  const World& w = frameWorld(a);
  const Army* fl = w.army(id);
  if (!fl || !fl->isFleet()) return;
  capacityRow(a, id);
  const Army* c = w.army(rules::cargoOf(w, id));
  if (!c) {
    ui::emptyState("embark", "Войска на борту нет.");
    a.markUi("army.cargo.empty");
    return;
  }
  const i64 n = unitCount(*c);
  // Флот потерял корабли (бой) и войско больше вместимости: высадка остаётся возможной.
  if (n > rules::fleetCapacity(w, id)) ui::label("Перегруз: войско больше вместимости флота", {.font = ui::Font::Small, .ink = ui::Ink::Danger, .icon = "warning"});
  {
    ui::Card card({.pad = 12});
    ui::Row r({ui::px(44), ui::fr(1)}, ui::kAuto, 10);
    figure(w, *c, 44);
    ui::Group g(0, 1);
    ui::caption(objectCaption(*c));
    if (ui::link(objectName(*c), "army")) a.select(SelType::Army, c->id);
    a.markUi("army.cargo.army");
    ui::label(fmtCount(n) + " " + plural(n, "воин", "воина", "воинов"), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  {
    ui::Disabled dis(a.readOnly());
    if (ui::button("Высадка войска", {.variant = ui::Variant::Primary, .icon = "disembark", .fill = true,
                                      .tooltip = "Высадить войско на сушу у флота (Esc — отмена)"}))
      startLanding(a, id);
    a.markUi("army.cargo.land");
  }
  ui::spacer(2);
  for (const ArmyGroup& g : c->groups) groupTile(a, *c, g);
}

bool cargoVisible(App& a, Id id) {
  const Army* ar = frameWorld(a).army(id);
  return ar && ar->isFleet();
}

int cargoBadge(App& a, Id id) { return rules::cargoOf(frameWorld(a), id) ? -1 : 0; }

// ---------------------------------------------------------------- вкладка «События»
void drawLog(App& a, Id id) {
  const World& w = frameWorld(a);
  std::vector<const LogEntry*> list;
  w.log.each([&](const LogEntry& e) {
    if (e.army == id) list.push_back(&e);
  });
  std::sort(list.begin(), list.end(), [](const LogEntry* x, const LogEntry* y) { return x->turn != y->turn ? x->turn > y->turn : x->id > y->id; });
  if (list.empty()) {
    ui::emptyState("chronicle", "Событий с этим объектом пока нет.");
    return;
  }
  int lastTurn = -1;
  for (const LogEntry* e : list) {
    ui::IdScope s(i64(e->id));
    if (e->turn != lastTurn) {
      ui::caption("Ход " + std::to_string(e->turn));
      lastTurn = e->turn;
    }
    ui::Row r({ui::px(18), ui::fr(1)}, ui::kAuto, 10);
    ui::icon(schema::logKind(e->kind).icon, e->kind == LogKind::Battle || e->kind == LogKind::War ? ui::Ink::Danger : ui::Ink::Dim, 16);
    ui::text(e->text, ui::Font::Body, ui::Ink::Normal);
  }
}


HeaderReg header({"army.header", SelType::Army, 0, drawHeader});
TabReg unitsTab({"army.units", "army", "Состав", 10, SelType::Army, nullptr, drawUnits});
TabReg cargoTab({"army.cargo", "embark", "Войско на борту", 15, SelType::Army, cargoVisible, drawCargo, cargoBadge});
TabReg logTab({"army.log", "chronicle", "События", 20, SelType::Army, nullptr, drawLog});

}  // namespace
}  // namespace rg::app::mil

namespace rg::app {

// ТЗ «Механика мятежа», п.1: мятеж всех войск государства в провинции (каждое — по своей верности) — с подтверждением.
void flow::startMutiny(App& a, Id army) {
  const World& w = a.world();
  std::string why;
  if (!rules::canMutiny(w, army, &why)) {
    a.toast(why, ToastKind::Warning, "warning");
    return;
  }
  if (a.readOnly()) {
    a.act("Мятеж", [](Tx&) {});   // сообщение о просмотре прошлого хода
    return;
  }
  const Army& ar = *w.army(army);
  const Faction* f = w.faction(ar.leader());
  const std::string state = f && !f->name.empty() ? f->name : std::string("Без названия");
  const double p = std::min(100.0, -ar.loyalty);
  // Другие войска государства в той же провинции восстают вместе (ТЗ «Мятеж», п.1.3) — каждое по своей верности.
  const Id pid = mil::provinceUnder(w, ar.pos);
  int others = 0;
  if (pid)
    w.armies.each([&](const Army& x) {
      if (x.id != army && !x.allied() && x.leader() == ar.leader() && x.kind == ar.kind && x.loyalty < 0 && mil::provinceUnder(w, x.pos) == pid) others++;
    });
  std::string text = p >= 100 ? "Войско восстанет целиком" : "Восстанет " + fmtPct(p, 1) + " войска";
  if (others > 0) text += "; другие войска государства в провинции (" + std::to_string(others) + ") — по своей верности";
  text += ". Мятежники перейдут к «Мятеж (" + state + ")», начнётся война.";
  a.confirm("Мятеж?", text, "Мятеж", true, [army](App& x) { mil::runMutiny(x, army); });
}

}  // namespace rg::app
