// Regnum — вкладка провинции «Гарнизон» (ТЗ 1.a.vi; «Доработки», п.1): отряды из таблицы войск владельца —
// несколько строк, каждая не больше резерва (rules::setGarrison); герои владельца в гарнизоне; верность гарнизона
// −100…100 % с изменением за ход (как у войска: модификаторы государства, «Непреклонный лоялист», у государства
// нежити — всегда 100 %) и мятеж при отрицательной верности (flow::startGarrisonMutiny).
#include "app/app_internal.h"
#include "app/flows.h"
#include "app/panels/military.h"

namespace rg::app::mil {

namespace {

struct AddState {
  int row = -1;
  i64 count = 0;
  Id lastRow = 0;
};

void setGarrison(App& a, Id pid, Id row, i64 n, bool coalesce) {
  TxOptions o;
  if (coalesce) o.coalesce = "garrison:" + std::to_string(pid) + ":" + std::to_string(row);
  a.act("Гарнизон провинции", [&](Tx& tx) { rules::setGarrison(tx, pid, row, n); }, o);
}

i64 garrisonTotal(const Province& p) {
  i64 n = 0;
  for (const GarrisonEntry& g : p.garrison) n += std::max<i64>(0, g.count);
  return n;
}

bool undeadOwner(const World& w, const Province& p) {
  const Faction* o = w.faction(p.owner);
  return o && o->isState() && o->stateKind == StateKind::Undead;
}

// Всплывающая панель «Назначить из резерва»: любая строка таблицы владельца, которой ещё нет в гарнизоне, и
// численность (по умолчанию — весь её резерв).
void addPopup(App& a, const Province& p, const std::vector<UnitRow>& avail) {
  const World& w = frameWorld(a);
  auto& st = ui::state<AddState>(ui::id("garrison-add"));
  if (!ui::beginPopup("garrison-add", {.width = 300})) return;
  ui::caption(std::string("Резерв · ") + w.factionName(p.owner));
  std::vector<std::string> hints;
  std::vector<i64> reserves;
  for (const UnitRow& r : avail) {
    reserves.push_back(reserveOf(w, p.owner, r.id, false));
    hints.push_back(fmtCount(reserves.back()));
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
  ui::combo("row", st.row, opts, {.placeholder = "Отряд"});
  a.markUi("garrison.add.row");
  i64 res = st.row >= 0 ? reserves[size_t(st.row)] : 0;
  Id rowId = st.row >= 0 ? avail[size_t(st.row)].id : 0;
  if (st.row != before || rowId != st.lastRow) st.count = res;   // по умолчанию — весь резерв строки
  st.lastRow = rowId;
  st.count = clamp<i64>(st.count, 0, res);
  ui::prop("Численность", "users");
  ui::numberField("count", st.count, {.min = 0, .max = double(res), .steppers = true, .tooltip = "Не больше резерва: " + fmtCount(res)});
  a.markUi("garrison.add.count");
  if (res <= 0) ui::label("В резерве нет свободных отрядов", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
  if (ui::button("Назначить", {.variant = ui::Variant::Primary, .icon = "check", .fill = true, .disabled = rowId == 0 || st.count <= 0})) {
    Id pid = p.id;
    i64 n = st.count;
    if (a.act("Гарнизон провинции", [&](Tx& tx) { rules::setGarrison(tx, pid, rowId, n); })) ui::closePopup();
  }
  a.markUi("garrison.add.ok");
  ui::endPopup();
}

// ---------------------------------------------------------------- верность
// Откуда изменение верности гарнизона за ход: модификаторы государства-владельца (его технологии, постройки, совет),
// «Непреклонный лоялист» у героев гарнизона; у государства нежити — всегда 100 %.
void loyaltySources(const World& w, const Province& p, double delta) {
  ui::label("Верность за ход: " + fmtSigned(delta, 1) + " %", {.font = ui::Font::Strong});
  if (undeadOwner(w, p)) {
    ui::label("Государство нежити — гарнизон верен всегда (100 %)", {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "skull"});
    return;
  }
  const rules::Effects fx = rules::factionEffects(w, p.owner);
  for (const rules::EffectSource& s : fx.sources) {
    const Modifier* m = s.modifier ? w.modifier(s.modifier) : (s.key.empty() ? nullptr : rules::builtinMod(w, s.key));
    if (!m || !m->has(Fx::LoyaltyPerTurn)) continue;
    std::string from;
    switch (s.kind) {
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
  for (Id h : p.garrisonHeroes)
    if (rules::characterHas(w, h, schema::mod::Loyalist)) loyalists++;
  if (loyalists)
    ui::label(fmtSigned(schema::kLoyalistBonus * loyalists, 1) + " % · непреклонный лоялист" + (loyalists > 1 ? " ×" + std::to_string(loyalists) : "") +
                  ", верность не уменьшается",
              {.font = ui::Font::Small, .ink = ui::Ink::Success});
}

// Верность гарнизона −100…100 % (как у войска в инспекторе), изменение за ход, полоса; при отрицательной — «Мятеж».
void loyaltyCard(App& a, const Province& p) {
  const World& w = frameWorld(a);
  const bool ro = a.readOnly();
  const Id pid = p.id;
  const bool undead = undeadOwner(w, p);
  const double delta = rules::garrisonLoyaltyDelta(w, pid);
  ui::IdScope scope("loyalty");
  ui::Card card({.pad = 12, .tone = p.garrisonLoyalty < 0 ? ui::Tone::Danger : ui::Tone::Neutral});
  {
    ui::Row r({ui::fr(1), ui::px(112), ui::px(92)}, 30, 8);
    ui::label("Верность", {.font = ui::Font::Strong, .icon = "heart"});
    {
      ui::Disabled dis(ro || undead);
      double v = p.garrisonLoyalty;
      if (ui::numberField("value", v, {.min = schema::kMinLoyalty, .max = schema::kMaxLoyalty, .step = 5, .digits = 1, .unit = "%",
                                       .tooltip = undead ? "Гарнизон государства нежити верен всегда (100 %)" : "Верность гарнизона: от −100 до 100 %"}))
        a.act("Верность гарнизона", [&](Tx& tx) { rules::setGarrisonLoyalty(tx, pid, v); }, {.coalesce = "garrisonloyalty:" + std::to_string(pid)});
      a.markUi("garrison.loyalty");
    }
    const std::string d = (std::fabs(delta) < 1e-9 ? std::string("0") : fmtSigned(delta, 1)) + " %/ход";
    ui::label(d, {.font = ui::Font::Small, .ink = delta > 1e-9 ? ui::Ink::Success : delta < -1e-9 ? ui::Ink::Danger : ui::Ink::Muted,
                  .align = ui::Align::Right, .tooltip = "Изменение верности гарнизона за ход"});
    a.markUi("garrison.loyalty.delta");
    if (ui::beginTooltip(320)) {
      loyaltySources(w, p, delta);
      ui::endTooltip();
    }
  }
  ui::meter(p.garrisonLoyalty, {.label = false});
  if (p.garrisonLoyalty < 0) {
    std::string why;
    const bool can = rules::canGarrisonMutiny(w, pid, &why);
    {
      ui::Disabled dis(ro || !can);
      if (ui::button("Мятеж", {.variant = ui::Variant::Danger, .icon = "rebellion", .fill = true,
                               .tooltip = "Неверная часть гарнизона и войск владельца в провинции отделится и восстанет"}))
        flow::startGarrisonMutiny(a, pid);
      a.markUi("garrison.mutiny");
    }
    if (!can && !ro) ui::label(why, {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "info", .wrap = true});
  }
}

// ---------------------------------------------------------------- отряды
void unitsSection(App& a, const Province& p, const std::vector<UnitRow>& rows) {
  const World& w = frameWorld(a);
  const bool ro = a.readOnly();
  const Id pid = p.id;
  auto c = rules::calc(w);
  std::vector<UnitRow> avail;   // строки владельца, которых ещё нет в гарнизоне
  for (const UnitRow& r : rows) {
    bool in = false;
    for (const GarrisonEntry& g : p.garrison) in = in || g.row == r.id;
    if (!in) avail.push_back(r);
  }
  struct L {
    UnitRow row;
    i64 count;
    i64 reserve;
  };
  std::vector<L> lines;
  for (const GarrisonEntry& g : p.garrison) {
    auto r = unitRow(w, p.owner, g.row, false);
    if (!r) continue;
    const rules::RowCalc* rc = rowCalc(*c, p.owner, g.row, false);
    lines.push_back(L{*r, g.count, rc ? std::max<i64>(0, rc->reserve) : 0});
  }
  {
    ui::Section s("Отряды", "army", {.badge = std::to_string(lines.size()), .card = false, .actionIcon = ro || avail.empty() ? nullptr : "plus",
                                    .actionTooltip = "Назначить отряд из резерва"});
    if (s.action()) ui::openPopup("garrison-add");
    a.markUi("garrison.units.add");
    if (s) {
      if (lines.empty()) {
        ui::Disabled dis(ro);
        if (ui::emptyState("castle", "Гарнизона нет.", "Назначить из резерва", "plus")) ui::openPopup("garrison-add");
        a.markUi("garrison.add");
      } else {
        {   // таблица заканчивается (и занимает место в потоке) до кнопки под ней
          ui::Column cols[] = {{"Отряд", nullptr, ui::fr(1, 110)},
                               {"Численность", nullptr, ui::px(104), ui::Align::Left, false, "Не больше резерва владельца"},
                               {"Резерв", nullptr, ui::px(68), ui::Align::Right, false, "Свободно в резерве владельца"},
                               {"", nullptr, ui::px(36)}};
          ui::Table t("garrison", cols, int(lines.size()), {.rowHeight = 44, .selectable = false});
          for (int i : t) {
            const L& l = lines[size_t(i)];
            RectF cr = t.cell();
            unitCell(cr, l.row, false, l.row.name != l.row.typeName ? std::string_view(l.row.typeName) : std::string_view());
            ui::Disabled dis(ro);
            t.cell();
            i64 n = l.count;
            if (ui::numberField("n", n, {.min = 0, .max = double(l.count + l.reserve), .tooltip = "Не больше резерва: " + fmtCount(l.reserve) + " свободно"}))
              setGarrison(a, pid, l.row.id, n, true);
            a.markUi("garrison.row." + std::to_string(l.row.id));
            t.text(fmtCount(l.reserve), l.reserve > 0 ? ui::Ink::Success : ui::Ink::Muted);
            t.cell();
            if (ui::iconButton("close", "Вернуть в резерв")) setGarrison(a, pid, l.row.id, 0, false);
            a.markUi("garrison.row." + std::to_string(l.row.id) + ".remove");
          }
          if (t.footer()) {
            t.text("Итого");
            t.text(fmtCount(garrisonTotal(p)));
          }
        }
        if (!avail.empty()) {
          ui::Disabled dis(ro);
          if (ui::button("Назначить из резерва", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small,
                                                  .tooltip = "Ещё строка таблицы войск владельца — любого типа"}))
            ui::openPopup("garrison-add");
          a.markUi("garrison.add");
        }
      }
    }
  }
  addPopup(a, p, avail);   // открывается кнопкой в заголовке раздела, пустым состоянием или кнопкой под таблицей
}

// ---------------------------------------------------------------- герои
// Герои владельца в гарнизоне (портрет в круге, имя, титул, убрать) и добавление: доступные герои владельца (не
// «Мертв» и не «Взят в плен»); занятые в войске или другом гарнизоне видны, но не выбираются; «Новый герой».
void heroesSection(App& a, const Province& p) {
  const World& w = frameWorld(a);
  const bool ro = a.readOnly();
  const Id pid = p.id;
  ui::Section s("Герои", "hero", {.badge = std::to_string(p.garrisonHeroes.size()), .card = false});
  a.markUi("garrison.heroes");
  if (!s) return;
  ui::IdScope scope("heroes");
  for (Id h : p.garrisonHeroes) {
    const Character* ch = w.character(h);
    if (!ch) continue;
    ui::IdScope sc{i64(h)};
    ui::Row r({ui::px(34), ui::fr(1), ui::px(30)}, 38, 8);
    w::heroAvatar(*ch, 30, false, ch->name.empty() ? std::string_view("Без имени") : std::string_view(ch->name));
    a.markUi("garrison.hero." + std::to_string(h));
    {
      ui::Group gg(0, 0);
      const std::string name = ch->name.empty() ? std::string("Без имени") : ch->name;
      const float room = ui::avail().w;
      ui::label(name, {.font = ui::Font::Strong, .tooltip = ui::measure(name, ui::Font::Strong) > room ? std::string_view(name) : std::string_view()});
      const bool loyalist = rules::characterHas(w, h, schema::mod::Loyalist);
      const bool discontent = rules::characterHas(w, h, schema::mod::Discontent);
      std::string sub = ch->title.empty() ? std::string("Герой") : ch->title;
      if (loyalist) sub += " · непреклонный лоялист";
      if (discontent) sub += " · недоволен правителем";
      ui::label(sub, {.font = ui::Font::Caption, .ink = discontent ? ui::Ink::Warning : loyalist ? ui::Ink::Success : ui::Ink::Muted});
    }
    ui::Disabled dis(ro);
    if (ui::iconButton("close", "Убрать героя из гарнизона")) {
      const Id hero = h;
      a.act("Убрать героя из гарнизона", [&](Tx& tx) { rules::setGarrisonHero(tx, pid, hero, false); });
    }
    a.markUi("garrison.hero." + std::to_string(h) + ".remove");
  }
  if (ro) return;
  std::vector<const Character*> list;
  w.characters.each([&](const Character& c) {
    if (c.faction != p.owner || !rules::heroAvailable(w, c.id)) return;
    if (std::find(p.garrisonHeroes.begin(), p.garrisonHeroes.end(), c.id) != p.garrisonHeroes.end()) return;
    list.push_back(&c);
  });
  std::sort(list.begin(), list.end(), [](const Character* x, const Character* y) {
    if (x->hero != y->hero) return x->hero;
    return compareRu(x->name, y->name) < 0;
  });
  std::vector<std::string> hints;
  std::vector<Id> ids;
  std::vector<bool> busy;
  for (const Character* c : list) {
    const std::string at = heroBusy(w, c->id, 0, pid);
    busy.push_back(!at.empty());
    ids.push_back(c->id);
    hints.push_back(!at.empty() ? at : (c->title.empty() ? std::string(c->hero ? "герой" : "") : c->title));
  }
  const Color fc = w::factionColor(w, p.owner);
  std::vector<ui::Option> opts;
  for (size_t i = 0; i < list.size(); i++)
    opts.push_back(ui::Option{list[i]->name, list[i]->hero ? "hero" : "character", fc, hints[i], bool(busy[i])});
  opts.push_back(ui::Option{"Новый герой", "user-plus", Color(0, 0, 0, 0), {}, false});
  int idx = -1;
  if (ui::combo("addhero", idx, opts, {.placeholder = "Добавить героя", .search = 1, .icon = "user-plus"})) {
    if (idx >= 0 && idx < int(ids.size()) && !busy[size_t(idx)]) {
      const Id hero = ids[size_t(idx)];
      a.act("Герой в гарнизоне", [&](Tx& tx) { rules::setGarrisonHero(tx, pid, hero, true); });
    } else if (idx == int(ids.size())) {
      const Id owner = p.owner;
      a.act("Новый герой в гарнизоне", [&](Tx& tx) {
        const Id nh = rules::createCharacter(tx, owner, "Новый герой");
        tx.character(nh).hero = true;
        rules::setGarrisonHero(tx, pid, nh, true);
      });
    }
  }
  a.markUi("garrison.addhero");
}

// ---------------------------------------------------------------- вкладка
void drawGarrison(App& a, Id pid) {
  const World& w = frameWorld(a);
  const Province* p = w.province(pid);
  if (!p || p->sea) return;
  if (!p->owner || !w.faction(p->owner)) {
    ui::emptyState("castle", "У провинции нет владельца — гарнизон некому назначить.");
    return;
  }
  const Faction& owner = *w.faction(p->owner);
  std::vector<UnitRow> rows = unitRows(w, p->owner, false);
  auto c = rules::calc(w);
  i64 reserve = 0;
  for (const UnitRow& r : rows)
    if (const rules::RowCalc* rc = rowCalc(*c, p->owner, r.id, false)) reserve += std::max<i64>(0, rc->reserve);
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
    ui::stat(fmtCount(garrisonTotal(*p)), "В гарнизоне", {.icon = "castle", .tone = ui::Tone::Accent});
    ui::stat(fmtCount(reserve), "Резерв владельца", {.icon = "shield", .tone = ui::Tone::Success,
                                                      .tooltip = "Свободные отряды " + (owner.name.empty() ? std::string("владельца") : owner.name)});
  }
  {
    ui::prop("Владелец", "crown");
    w::factionChip(p->owner);
  }
  if (owner.isState()) loyaltyCard(a, *p);
  ui::spacer(2);
  if (rows.empty()) {
    if (ui::emptyState("army", "В таблице войск владельца нет отрядов.", "Открыть войска", "army")) {
      a.ui.tabOf[SelType::Faction] = "faction.army";
      a.select(SelType::Faction, p->owner);
    }
  } else {
    unitsSection(a, *p, rows);
  }
  ui::spacer(2);
  heroesSection(a, *p);
}

// Морская провинция не содержит сведений (ТЗ 1.a.iii) — вкладки нет; провинция без владельца — пустое состояние.
bool visibleGarrison(App& a, Id pid) {
  const Province* p = a.world().province(pid);
  return p && !p->sea;
}

// Точка на вкладке: гарнизон с отрядами при отрицательной верности (возможен мятеж).
int garrisonBadge(App& a, Id pid) {
  const Province* p = a.world().province(pid);
  return p && !p->sea && p->owner && !p->garrison.empty() && p->garrisonLoyalty < 0 ? -1 : 0;
}

TabReg garrisonTab({"province.garrison", "castle", "Гарнизон", 35, SelType::Province, visibleGarrison, drawGarrison, garrisonBadge});

// ---------------------------------------------------------------- мятеж гарнизона
// После мятежа: битва мятежников с верными войсками владельца в провинции, или судьба верных героев целиком
// восставших войск и штурм оставшегося гарнизона или захват; затем — объявленная война (вассалитет).
void runGarrisonMutiny(App& a, Id province) {
  const World before = a.world();
  const Province* p = before.province(province);
  if (!p) return;
  const Id origin = p->owner;
  rules::MutinyResult res;
  if (!a.act("Мятеж гарнизона: " + before.provinceName(province), [&](Tx& tx) { res = rules::garrisonMutiny(tx, province); })) return;
  const World& w = a.world();
  a.toast("Мятеж: " + w.factionName(res.rebelState) + (res.full ? " — гарнизон восстал целиком" : ""), ToastKind::Warning, "rebellion");
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
    if (rebel && x.world().army(rebel)) flow::rebelAftermath(x, rebel, war);
    else war(x);
  };
  if (rebel && w.army(rebel)) a.select(SelType::Army, rebel);
  // Верные герои войск, восставших целиком, — «Судьба героя» (пленившее — мятежное государство).
  if (!res.loyalHeroes.empty()) flow::openHeroFate(a, res.loyalHeroes, rebelState, province, aftermath);
  else aftermath(a);
}

}  // namespace
}  // namespace rg::app::mil

namespace rg::app {

// ТЗ «Доработки», п.1: мятеж гарнизона с отрицательной верностью (и войск владельца в провинции — каждое по своей
// верности) — с подтверждением.
void flow::startGarrisonMutiny(App& a, Id province) {
  const World& w = a.world();
  std::string why;
  if (!rules::canGarrisonMutiny(w, province, &why)) {
    a.toast(why, ToastKind::Warning, "warning");
    return;
  }
  if (a.readOnly()) {
    a.act("Мятеж гарнизона", [](Tx&) {});   // сообщение о просмотре прошлого хода
    return;
  }
  const Province& p = *w.province(province);
  const Faction* f = w.faction(p.owner);
  const std::string state = f && !f->name.empty() ? f->name : std::string("Без названия");
  const double pct = std::min(100.0, -p.garrisonLoyalty);
  int others = 0;   // войска владельца в провинции восстают вместе — каждое по своей верности
  w.armies.each([&](const Army& x) {
    if (!x.isFleet() && !x.allied() && x.leader() == p.owner && x.loyalty < 0 && mil::provinceUnder(w, x.pos) == province) others++;
  });
  std::string text = pct >= 100 ? "Гарнизон восстанет целиком" : "Восстанет " + fmtPct(pct, 1) + " гарнизона";
  if (others > 0) text += "; войска государства в провинции (" + std::to_string(others) + ") — по своей верности";
  text += ". Мятежники перейдут к «Мятеж (" + state + ")», начнётся война.";
  a.confirm("Мятеж гарнизона?", text, "Мятеж", true, [province](App& x) { mil::runGarrisonMutiny(x, province); });
}

}  // namespace rg::app
