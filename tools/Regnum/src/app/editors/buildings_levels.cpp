// Regnum — дерево построек: карточка уровня в панели свойств (срок, стоимость ресурсами и эссенциями, корабли верфи,
// снижение содержания армии, добыча, ресурсы за ход и при ресурсе провинции, эссенции за ход, иссушение населения,
// описание) и окно модификаторов уровня (ТЗ «Доработки», п.8).
#include "app/editors/buildings_internal.h"

namespace rg::app::bldtree {

namespace {

// «Даёт за ход» (ТЗ «Виды государств», п.4, 8): ресурсы, которые достроенный уровень даёт владельцу каждый ход, —
// любые ресурсы справочника, в том числе трупы и демоническая энергия (встроенный ресурс создаётся при выборе).
void levelProduce(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const std::map<Id, double> produce = b.levels[size_t(li)].produce;
  const Id bid = b.id;
  ui::IdScope ps("produce");
  ui::caption("Даёт за ход");
  int ri = 0;
  for (auto [res, amount] : produce) {
    ui::IdScope rs(ri++);
    ui::Row row({ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    Id nr = res;
    if (w::catalogPicker("res", rules::CatalogList::Resources, nr, "", false, ro) && nr != res) {
      Id old = res;
      double v = amount;
      a.act("Ресурс уровня", [&](Tx& tx) {
        auto& pr = tx.building(bid).levels[size_t(li)].produce;
        if (pr.count(nr)) fail("Этот ресурс уровень уже даёт");
        pr.erase(old);
        pr[nr] = v;
      });
    }
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .tooltip = "Количество за ход"}))
      a.act("Ресурс за ход", [&](Tx& tx) { tx.building(bid).levels[size_t(li)].produce[res] = std::max(0.0, v); },
            {.coalesce = "bt-prod:" + std::to_string(bid) + ":" + std::to_string(li) + ":" + std::to_string(res)});
    a.markUi("bt.level." + std::to_string(li) + ".produce." + std::to_string(res));
    if (ui::iconButton("close", "Убрать ресурс")) {
      Id r = res;
      a.act("Убрать ресурс за ход", [&](Tx& tx) { tx.building(bid).levels[size_t(li)].produce.erase(r); });
    }
  }
  if (ro) return;
  // Добавить: ресурсы справочника и встроенные, которых в мире ещё нет (провизия, трупы, демоническая энергия).
  struct Pick {
    Id id = 0;
    const char* key = nullptr;
    std::string label;
    const char* icon = nullptr;
    Color color;
  };
  std::vector<Pick> cand;
  for (const CatalogItem& c : w.catalogs->resources)
    if (!produce.count(c.id)) cand.push_back({c.id, nullptr, c.name.empty() ? std::string("Без названия") : c.name, w::resourceIcon(w, c.id), w::resourceColor(w, c.id)});
  for (const schema::BuiltinResource& br : schema::kBuiltinResources)
    if (std::string_view(br.key) != schema::kResGold && !rules::resourceId(w, br.key)) cand.push_back({0, br.key, br.name, br.icon, Color::hex(br.color)});
  if (cand.empty()) return;
  std::vector<ui::Option> opts;
  for (const Pick& o : cand) opts.push_back(ui::Option{o.label, o.icon, o.color});
  int idx = -1;
  if (ui::combo("add", idx, opts, {.placeholder = "Ресурс за ход", .search = 1, .icon = "plus"}) && idx >= 0 && idx < int(cand.size())) {
    const Pick o = cand[size_t(idx)];
    a.act("Ресурс за ход", [&](Tx& tx) {
      Id r = o.id ? o.id : rules::ensureResource(tx, o.key);
      tx.building(bid).levels[size_t(li)].produce[r] = 1;
    });
  }
  a.markUi("bt.level." + std::to_string(li) + ".addproduce");
}

// «Эссенция за ход» (постройка генерации эссенции, ТЗ «Ввод новых механик»): эссенции, которые достроенный уровень
// даёт владельцу каждый ход (rules::setLevelEssence; 0 — убрать).
void levelEssence(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const std::map<Id, double> ess = b.levels[size_t(li)].essence;
  const Id bid = b.id;
  const int level = li + 1;
  const std::string mark = "bt.level." + std::to_string(li);
  ui::IdScope es("essence");
  ui::caption("Эссенция за ход");
  for (auto [e, amount] : ess) {
    ui::IdScope rs{i64(e)};
    ui::Row row({ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    Id ne = e;
    if (w::essencePicker("ess", ne, {}, ro) && ne && ne != e) {
      const Id old = e;
      const double v = amount;
      a.act("Эссенция уровня", [&](Tx& tx) {
        if (tx.w().building(bid)->levels[size_t(li)].essence.count(ne)) fail("Эту эссенцию уровень уже даёт");
        rules::setLevelEssence(tx, bid, level, old, 0);
        rules::setLevelEssence(tx, bid, level, ne, v);
      });
    }
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0.001, .max = 1e9, .step = 1, .digits = 3, .tooltip = "Эссенции за ход"}))
      a.act("Эссенция за ход", [&](Tx& tx) { rules::setLevelEssence(tx, bid, level, e, v); },
            {.coalesce = "bt-ess:" + std::to_string(bid) + ":" + std::to_string(li) + ":" + std::to_string(e)});
    a.markUi(mark + ".ess." + std::to_string(e));
    if (ui::iconButton("close", "Убрать эссенцию", {.disabled = ro})) {
      const Id x = e;
      a.act("Убрать эссенцию уровня", [&](Tx& tx) { rules::setLevelEssence(tx, bid, level, x, 0); });
    }
  }
  if (ro) return;
  // Добавить: эссенции справочника, которых у уровня ещё нет.
  std::vector<const CatalogItem*> cand;
  for (const CatalogItem& c : w.catalogs->essences)
    if (!ess.count(c.id)) cand.push_back(&c);
  if (cand.empty()) {
    if (w.catalogs->essences.empty() && ui::link("Справочник эссенций", "essence")) a.openEditor("catalogs", 7);
    return;
  }
  w::IdOptions opts;
  for (const CatalogItem* c : cand) opts.add(c->id, w::orName(c->name), "essence", c->color);
  if (const Id e = w::addCombo("add", opts, {.placeholder = "Эссенция за ход", .search = 1, .icon = "plus"}))
    a.act("Эссенция за ход", [&](Tx& tx) { rules::setLevelEssence(tx, bid, level, e, 1); });
  a.markUi(mark + ".addess");
}

// Цена уровня в эссенциях (ТЗ «Доработки №4», п.5, 7: соборы, постройки доступа): списывается с запасов государства
// вместе со стоимостью ресурсами (rules::setLevelEssCost; 0 — убрать).
void levelEssCost(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const std::map<Id, double> ess = b.levels[size_t(li)].essCost;
  const Id bid = b.id;
  const int level = li + 1;
  const std::string mark = "bt.level." + std::to_string(li);
  ui::IdScope es("esscost");
  if (!ess.empty() || !ro) ui::caption("Цена в эссенциях");
  for (auto [e, amount] : ess) {
    ui::IdScope rs{i64(e)};
    ui::Row row({ui::px(16), ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    const CatalogItem* c = w.essence(e);
    ui::iconColored("essence", w::essenceColor(w, e), 16);
    ui::label(c && !c->name.empty() ? c->name : std::string("Эссенция"));
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0.001, .max = 1e9, .step = 10, .digits = 3, .tooltip = "Эссенции за уровень"}))
      a.act("Цена в эссенции", [&](Tx& tx) { rules::setLevelEssCost(tx, bid, level, e, v); },
            {.coalesce = "bt-esscost:" + std::to_string(bid) + ":" + std::to_string(li) + ":" + std::to_string(e)});
    a.markUi(mark + ".esscost." + std::to_string(e));
    if (ui::iconButton("close", "Убрать эссенцию из цены", {.disabled = ro})) {
      const Id x = e;
      a.act("Убрать эссенцию из цены", [&](Tx& tx) { rules::setLevelEssCost(tx, bid, level, x, 0); });
    }
  }
  if (ro) return;
  std::vector<const CatalogItem*> cand;
  for (const CatalogItem& c : w.catalogs->essences)
    if (!ess.count(c.id)) cand.push_back(&c);
  if (cand.empty()) return;
  w::IdOptions opts;
  for (const CatalogItem* c : cand) opts.add(c->id, w::orName(c->name), "essence", c->color);
  if (const Id e = w::addCombo("add", opts, {.placeholder = "Эссенция в цене", .search = 1, .icon = "plus"}))
    a.act("Цена в эссенции", [&](Tx& tx) { rules::setLevelEssCost(tx, bid, level, e, 100); });
  a.markUi(mark + ".addesscost");
}

// Верфь (ТЗ «Доработки №3», п.2): какие типы кораблей открывает уровень (rules::setLevelShips) — значки-переключатели.
void levelShips(App& a, const BN& b, int li, bool ro) {
  const u32 ships = b.levels[size_t(li)].ships;
  const Id bid = b.id;
  const int level = li + 1;
  ui::IdScope ss("ships");
  ui::caption("Открывает корабли");
  ui::HStack hs(30, ui::Align::Left, 4);
  for (int t = 0; t < int(ShipType::Count); t++) {
    ui::IdScope s(t);
    const bool on = (ships >> t) & 1u;
    const schema::EnumInfo& si = schema::shipType(ShipType(t));
    if (ui::iconButton(si.icon, std::string(si.name) + (on ? " — открыт" : " — закрыт"), {.variant = ui::Variant::Secondary, .toggled = on, .disabled = ro})) {
      const u32 nv = on ? ships & ~(1u << t) : ships | (1u << t);
      a.act(on ? "Верфь: закрыть тип кораблей" : "Верфь: открыть тип кораблей", [&](Tx& tx) { rules::setLevelShips(tx, bid, level, nv); });
    }
    a.markUi("bt.level." + std::to_string(li) + ".ship." + std::to_string(t));
  }
}

// Производство при ресурсе провинции (лесопилка: 20 древесины, в провинции с ресурсом «Древесина» — 100): ресурс
// провинции-ключ и количество вместо обычного производства этого ресурса (rules::setLevelProduceOnRes; 0 — убрать).
void levelProduceOnRes(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const std::map<Id, double> on = b.levels[size_t(li)].produceOnRes;
  const Id bid = b.id;
  const int level = li + 1;
  const std::string mark = "bt.level." + std::to_string(li);
  ui::IdScope ps("onres");
  // Пусто — раздел только у уровня, который что-то производит (лесопилка: древесина), иначе он лишь мешает.
  if (on.empty() && (ro || b.levels[size_t(li)].produce.empty())) return;
  {
    ui::HStack hs(20, ui::Align::Left, 6);
    ui::caption("При ресурсе провинции");
    ui::icon("info", ui::Ink::Muted, 14, "Если ресурс провинции — этот, уровень даёт за ход указанное количество его же вместо обычного");
  }
  for (auto [res, amount] : on) {
    ui::IdScope rs{i64(res)};
    ui::Row row({ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    Id nr = res;
    if (w::catalogPicker("res", rules::CatalogList::Resources, nr, "", false, ro) && nr && nr != res) {
      const Id old = res;
      const double v = amount;
      a.act("Ресурс провинции уровня", [&](Tx& tx) {
        if (tx.w().building(bid)->levels[size_t(li)].produceOnRes.count(nr)) fail("Этот ресурс провинции уже задан");
        rules::setLevelProduceOnRes(tx, bid, level, old, 0);
        rules::setLevelProduceOnRes(tx, bid, level, nr, v);
      });
    }
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0.001, .max = 1e9, .step = 1, .digits = 3, .tooltip = "За ход при этом ресурсе провинции"}))
      a.act("Производство при ресурсе провинции", [&](Tx& tx) { rules::setLevelProduceOnRes(tx, bid, level, res, v); },
            {.coalesce = "bt-onres:" + std::to_string(bid) + ":" + std::to_string(li) + ":" + std::to_string(res)});
    a.markUi(mark + ".onres." + std::to_string(res));
    if (ui::iconButton("close", "Убрать")) {
      const Id x = res;
      a.act("Убрать производство при ресурсе", [&](Tx& tx) { rules::setLevelProduceOnRes(tx, bid, level, x, 0); });
    }
  }
  if (ro) return;
  // Добавить: сначала ресурсы, которые уровень уже даёт (лесопилка — древесина), затем остальные.
  const std::map<Id, double>& prod = b.levels[size_t(li)].produce;
  std::vector<Id> cand;
  for (auto& [r, v] : prod)
    if (!on.count(r) && w.resource(r)) cand.push_back(r);
  for (const CatalogItem& c : w.catalogs->resources)
    if (!on.count(c.id) && std::find(cand.begin(), cand.end(), c.id) == cand.end()) cand.push_back(c.id);
  w::IdOptions opts;
  for (Id r : cand) opts.add(r, w::resourceName(w, r), w::resourceIcon(w, r), w::resourceColor(w, r), prod.count(r) ? "даёт" : "");
  if (const Id r = w::addCombo("add", opts, {.placeholder = "Ресурс провинции", .search = 1, .icon = "plus"})) {
    auto it = prod.find(r);
    const double v = it != prod.end() && it->second > 0 ? it->second * 5 : 1;
    a.act("Производство при ресурсе провинции", [&](Tx& tx) { rules::setLevelProduceOnRes(tx, bid, level, r, v); });
  }
  a.markUi(mark + ".addonres");
}

// Иссушение населения (постройка иссушения): жителей выбранной расы за ход и эссенции за них (rules::setLevelDrain).
void levelDrain(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const BuildingLevel& L = b.levels[size_t(li)];
  const i64 pop = L.drainPop;
  const std::map<Id, double> ess = L.drainEss;
  const Id bid = b.id;
  const int level = li + 1;
  const std::string mark = "bt.level." + std::to_string(li);
  ui::IdScope ds("drain");
  ui::caption("Иссушение");
  {
    ui::prop("Жителей за ход", "population", 0.5f);
    i64 n = pop;
    if (ui::numberField("pop", n, {.min = 0, .max = 1e12, .step = 10, .tooltip = "Жителей выбранной расы провинции, которые отнимаются каждый ход"}))
      a.act("Иссушение: жителей за ход", [&](Tx& tx) { rules::setLevelDrain(tx, bid, level, std::max<i64>(0, n)); },
            {.coalesce = "bt-drainpop:" + std::to_string(bid) + ":" + std::to_string(li)});
    a.markUi(mark + ".drainpop");
  }
  auto popNow = [&](Tx& tx) { return tx.w().building(bid)->levels[size_t(li)].drainPop; };
  for (auto [e, amount] : ess) {
    ui::IdScope rs{i64(e)};
    ui::Row row({ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    Id ne = e;
    if (w::essencePicker("ess", ne, {}, ro) && ne && ne != e) {
      const Id old = e;
      const double v = amount;
      a.act("Иссушение: эссенция", [&](Tx& tx) {
        if (tx.w().building(bid)->levels[size_t(li)].drainEss.count(ne)) fail("Эта эссенция уже есть");
        const i64 p = popNow(tx);
        rules::setLevelDrain(tx, bid, level, p, old, 0);
        rules::setLevelDrain(tx, bid, level, p, ne, v);
      });
    }
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0.001, .max = 1e9, .step = 1, .digits = 3,
                                      .tooltip = "Эссенции за " + fmtNum(double(pop)) + " жителей; отнято меньше — меньше"}))
      a.act("Иссушение: эссенция", [&](Tx& tx) { rules::setLevelDrain(tx, bid, level, popNow(tx), e, v); },
            {.coalesce = "bt-drainess:" + std::to_string(bid) + ":" + std::to_string(li) + ":" + std::to_string(e)});
    a.markUi(mark + ".drainess." + std::to_string(e));
    if (ui::iconButton("close", "Убрать эссенцию")) {
      const Id x = e;
      a.act("Иссушение: убрать эссенцию", [&](Tx& tx) { rules::setLevelDrain(tx, bid, level, popNow(tx), x, 0); });
    }
  }
  if (ro) return;
  w::IdOptions opts;
  for (const CatalogItem& c : w.catalogs->essences)
    if (!ess.count(c.id)) opts.add(c.id, w::orName(c.name), "essence", c.color);
  if (const Id e = w::addCombo("add", opts, {.placeholder = "Эссенция за жителей", .search = 1, .icon = "plus"}))
    a.act("Иссушение: эссенция", [&](Tx& tx) { rules::setLevelDrain(tx, bid, level, popNow(tx), e, 1); });
  a.markUi(mark + ".adddrainess");
}

// ---------------------------------------------------------------- окно модификаторов уровня (ТЗ «Доработки», п.8)
// Все модификаторы мира, которые может давать постройка: виды «Везде», «Для провинций», «Глобальный» (не для армий
// и героев, не автоматические), по видам, с поиском. Отметка — модификатор в уровне: добавляется и убирается сразу
// (Ctrl+Z отменяет). «Новый модификатор» создаёт запись вида «Для провинций» и сразу добавляет её в уровень.
const char* modIconOf(const Modifier& m) { return m.icon.empty() || !gfx::hasIcon(m.icon) ? "sparkles" : m.icon.c_str(); }

std::string effectsLine(const Modifier& m) {
  std::vector<std::string> p;
  for (int f = 0; f < kFxCount; f++)
    if (m.has(Fx(f)) && m.fx[size_t(f)] != 0) p.push_back(w::effectText(Fx(f), m.fx[size_t(f)]));
  return join(p, "; ");
}

struct LevelMods : Dialog {
  Id bid = 0;
  int li = 0;                  // уровень с 0
  std::string query, name;
  Id made = 0;                 // созданный в окне модификатор: подсветка и переход в редактор
  bool scrollMade = false;     // показать его строку в списке (после создания)
  std::optional<RectF> madeRow;   // строка созданного модификатора в этом кадре
  const char* id() const override { return "bt.mods"; }
  Style style(App& a) override {
    const Building* b = a.world().building(bid);
    return {"Модификаторы · " + (b ? bname(*b) : std::string("постройка")) + " · " + roman(li + 1), "sparkles", ui::Tone::Accent, 620};
  }
  bool draw(App& a) override;

  // Копия модификатора на кадр (действие посреди кадра заменяет мир — указатели на записи не держим).
  struct Item {
    Id id = 0;
    std::string name, icon, fx;
    Color color;
    ModKind kind = ModKind::Any;
    bool in = false, fits = true;
  };
  bool row(App& a, const Item& it, bool ro);   // false — закрыть окно (переход в редактор)
  void toggle(App& a, Id mid, bool on);
};

void LevelMods::toggle(App& a, Id mid, bool on) {
  const Id b = bid;
  const int l = li;
  a.act(on ? "Модификатор в уровне постройки" : "Убрать модификатор из уровня", [&](Tx& tx) {
    const Building* x = tx.w().building(b);
    if (!x || l >= int(x->levels.size())) fail("Уровень постройки не найден");
    auto& mods = tx.building(b).levels[size_t(l)].modifiers;
    const bool has = std::find(mods.begin(), mods.end(), mid) != mods.end();
    if (on && !has) mods.push_back(mid);
    if (!on && has) mods.erase(std::remove(mods.begin(), mods.end(), mid), mods.end());
  });
}

bool LevelMods::row(App& a, const Item& it, bool ro) {
  const ui::Theme& th = ui::theme();
  ui::IdScope s{i64(it.id)};
  const RectF r = ui::next(42);
  // Строка — цель для мыши (Enter в окне остаётся за «Готово»).
  const ui::Interaction in = ui::interact(ui::id("row"), r, ui::IfAllowOverlap);
  const bool fresh = made == it.id;
  if (it.in || fresh) ui::draw::rect(r, th.accent.alpha(fresh ? 0.15f : 0.07f), 8);
  if (in.hovered && !ro) {
    ui::draw::rect(r, th.hover, 8);
    ui::setCursor(platform::Cursor::Hand);
  }
  // Отметка «в уровне»
  const RectF cb{r.x + 10, r.cy() - 9, 18, 18};
  if (it.in) {
    ui::draw::rect(cb, th.accent, 5);
    ui::draw::icon("check", cb.inset(2), th.onAccent);
  } else {
    ui::draw::rectStroke(cb, in.hovered && !ro ? th.accent : th.borderStrong, 5, 1.4f);
  }
  // Значок, название, эффекты
  ui::draw::icon(it.icon, RectF{r.x + 38, r.cy() - 9, 18, 18}, it.color);
  const float tx = r.x + 66, right = r.right() - 46;
  ui::draw::text(it.name.empty() ? std::string("Модификатор") : it.name, RectF{tx, r.y + 4, right - tx, 18}, ui::Font::Strong, th.text);
  std::string sub = it.fits ? (it.fx.empty() ? std::string("Без эффектов") : it.fx) : std::string("Не для построек: ") + schema::modKind(it.kind).name;
  ui::draw::text(sub, RectF{tx, r.y + 22, right - tx, 16}, ui::Font::Small, it.fits ? th.textMuted : th.warning);
  a.markUi("bt.mods.row." + std::to_string(it.id), r);
  // Переход в редактор модификаторов
  ui::at(RectF{r.right() - 38, r.cy() - 15, 30, 30});
  const bool open = ui::iconButton("edit", "Открыть в редакторе модификаторов");
  a.markUi("bt.mods.open." + std::to_string(it.id));
  if (fresh) madeRow = r;
  if (open) {
    a.openEditor("modifiers", it.id);
    return false;
  }
  if (in.clicked && !ro) toggle(a, it.id, !it.in);
  return true;
}

bool LevelMods::draw(App& a) {
  const World& w = a.world();
  const Building* b = w.building(bid);
  if (!b || li < 0 || li >= int(b->levels.size())) return false;
  const bool ro = !a.canEdit();
  const std::vector<Id> inLevel = b->levels[size_t(li)].modifiers;
  int n = 0;
  for (Id mid : inLevel) n += w.modifier(mid) != nullptr;
  {
    ui::Row r({ui::fr(1), ui::px(130)}, 30, 10);
    ui::searchField("q", query, "Найти модификатор");
    a.markUi("bt.mods.search");
    ui::tag("В уровне: " + std::to_string(n), n ? ui::Tone::Accent : ui::Tone::Neutral, "check");
  }
  // Модификаторы по видам: «Везде», «Для провинций», «Глобальный»; модификатор другого вида — только если он уже в
  // уровне (его можно убрать).
  std::vector<Item> groups[int(ModKind::Count)];
  w.modifiers.each([&](const Modifier& m) {
    const bool in = std::find(inLevel.begin(), inLevel.end(), m.id) != inLevel.end();
    const bool fits = w::modifierFits(m, w::ModScope::Any);
    if (!fits && !in) return;
    std::string fx = effectsLine(m);
    if (!query.empty() && !utf8::matches(m.name, query) && !utf8::matches(m.desc, query) && !utf8::matches(fx, query)) return;
    const int k = clamp(int(m.kind), 0, int(ModKind::Count) - 1);
    groups[k].push_back(Item{m.id, m.name, modIconOf(m), std::move(fx), m.color, ModKind(k), in, fits});
  });
  bool keep = true;
  {
    const float h = std::min(440.f, std::max(220.f, ui::viewport().h - 360));
    const RectF lr = ui::avail();
    a.markUi("bt.mods.list", RectF{lr.x, lr.y, lr.w, h});
    ui::Scroll sc("list", h);
    madeRow.reset();
    int shown = 0;
    for (int k = 0; k < int(ModKind::Count); k++) {
      auto& list = groups[k];
      if (list.empty()) continue;
      std::stable_sort(list.begin(), list.end(), [](const Item& x, const Item& y) { return compareRu(x.name, y.name) < 0; });
      ui::IdScope ks(k);
      {
        ui::HStack hs(26, ui::Align::Left, 8);
        ui::icon(schema::kModKinds[k].icon, ui::Ink::Muted, 16);
        ui::label(schema::kModKinds[k].name, {.font = ui::Font::Strong});
        ui::badge(std::to_string(list.size()), ui::Tone::Neutral);
      }
      a.markUi("bt.mods.kind." + std::to_string(k));
      for (const Item& it : list) {
        if (!keep) break;
        keep = row(a, it, ro);
        shown++;
      }
    }
    if (shown == 0) ui::emptyState("search", w.modifiers.empty() ? "Модификаторов пока нет." : "Ничего не найдено.");
    // Созданный модификатор — строкой целиком в видимой части списка.
    if (scrollMade && madeRow) {
      const float top = lr.y + 4, bottom = lr.y + h - 4;
      if (madeRow->bottom() > bottom) sc.scrollTo(sc.offset() + madeRow->bottom() - bottom);
      else if (madeRow->y < top) sc.scrollTo(std::max(0.f, sc.offset() - (top - madeRow->y)));
      scrollMade = false;
    }
  }
  if (!keep) return false;
  // Новый модификатор: запись вида «Для провинций», сразу в уровне; рядом — переход в редактор модификаторов.
  if (!ro) {
    ui::Row r({ui::fr(1), ui::px(184), ui::px(34)}, 30, 8);
    // Enter в поле названия — создать (а не «Готово»).
    const bool enter = ui::keyboardFocus() == ui::id("newname") && ui::keyPressed(Key::Enter);
    if (enter) ui::consumeKey(Key::Enter);
    ui::textField("newname", name, {.placeholder = "Название нового модификатора", .live = true, .maxLength = 80});
    a.markUi("bt.mods.name");
    if (ui::button("Новый модификатор", {.icon = "plus", .fill = true}) || enter) {
      const Id b0 = bid;
      const int l = li;
      const std::string nm = trim(name);
      Id nid = 0;
      if (a.act("Новый модификатор уровня", [&](Tx& tx) {
            if (!tx.w().building(b0) || l >= int(tx.w().building(b0)->levels.size())) fail("Уровень постройки не найден");
            nid = rules::createModifier(tx, nm);
            tx.modifier(nid).kind = ModKind::Province;
            tx.building(b0).levels[size_t(l)].modifiers.push_back(nid);
          })) {
        made = nid;
        scrollMade = true;
        name.clear();
        query.clear();
      }
    }
    a.markUi("bt.mods.create");
    const Modifier* fresh = w.modifier(made);
    if (ui::iconButton("sparkles", fresh ? "Открыть «" + (fresh->name.empty() ? std::string("Модификатор") : fresh->name) + "» в редакторе модификаторов"
                                         : std::string("Открыть в редакторе модификаторов"))) {
      a.openEditor("modifiers", fresh ? made : 0);
      return false;
    }
    a.markUi("bt.mods.editor");
  }
  ui::ModalFooter f;
  if (ui::button("Готово", {.variant = ui::Variant::Primary, .isDefault = true})) return false;
  a.markUi("bt.mods.done");
  return true;
}

}  // namespace

void levelCard(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const BuildingLevel& L = b.levels[size_t(li)];
  ui::IdScope s(li);
  std::string title = "Уровень " + roman(li + 1);
  ui::Card card({.pad = 12, .icon = "slots", .title = title});
  ui::Disabled dis(ro);
  {
    ui::prop("Срок", "hourglass", 0.4f);
    int turns = std::max(1, L.turns);
    if (ui::numberField("turns", turns, {.min = 1, .max = 999, .unit = "ход|хода|ходов", .steppers = true}))
      a.act("Срок строительства", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].turns = std::max(1, turns); },
            {.coalesce = "bt-turns:" + std::to_string(b.id) + ":" + std::to_string(li)});
    a.markUi("bt.level." + std::to_string(li) + ".turns");
  }
  ui::caption("Стоимость");
  int ri = 0;
  for (auto [res, amount] : L.cost) {
    ui::IdScope rs(ri++);
    ui::Row row({ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    Id nr = res;
    if (w::catalogPicker("res", rules::CatalogList::Resources, nr, "", false, ro) && nr != res) {
      Id old = res;
      double v = amount;
      a.act("Ресурс стоимости", [&](Tx& tx) {
        auto& cost = tx.building(b.id).levels[size_t(li)].cost;
        if (cost.count(nr)) fail("Этот ресурс уже есть в стоимости уровня");
        cost.erase(old);
        cost[nr] = v;
      });
    }
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 10, .digits = 3, .unit = w::amountUnit(res)}))
      a.act("Стоимость уровня", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].cost[res] = std::max(0.0, v); },
            {.coalesce = "bt-cost:" + std::to_string(b.id) + ":" + std::to_string(li) + ":" + std::to_string(res)});
    a.markUi("bt.level." + std::to_string(li) + ".cost." + std::to_string(res));
    if (ui::iconButton("close", "Убрать ресурс")) {
      Id r = res;
      a.act("Убрать ресурс из стоимости", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].cost.erase(r); });
    }
  }
  {
    // Следующий ресурс, которого ещё нет в стоимости (золото — первым).
    Id next = 0;
    if (!L.cost.count(kGold)) next = kGold;
    else
      for (const CatalogItem& c : w.catalogs->resources)
        if (!L.cost.count(c.id)) {
          next = c.id;
          break;
        }
    if (ui::button("Ресурс", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small, .disabled = next == 0,
                              .tooltip = next ? std::string_view("Добавить ресурс в стоимость уровня") : std::string_view("Все ресурсы уже в стоимости")})) {
      a.act("Ресурс в стоимости", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].cost[next] = 100; });
    }
    a.markUi("bt.level." + std::to_string(li) + ".addcost");
  }
  levelEssCost(a, b, li, ro);
  if (b.shipyard) levelShips(a, b, li, ro);
  // Числа возможностей уровня: снижение содержания армии, добыча ресурса провинции.
  if (b.upkeepCut) {
    ui::prop("Содержание", "army", 0.5f);
    double v = L.upkeepCut;
    if (ui::numberField("upkeep", v, {.min = 0, .max = schema::kMaxUpkeepCut, .step = 1, .digits = 1, .unit = "%", .label = "−",
                                      .tooltip = "Снижение содержания войск государства; сумма по государству — не больше " + fmtPct(schema::kMaxUpkeepCut)}))
      a.act("Снижение содержания армии", [&](Tx& tx) { rules::setLevelUpkeepCut(tx, b.id, li + 1, clamp(v, 0.0, schema::kMaxUpkeepCut)); },
            {.coalesce = "bt-upkeep:" + std::to_string(b.id) + ":" + std::to_string(li)});
    a.markUi("bt.level." + std::to_string(li) + ".upkeep");
  }
  if (b.extract) {
    ui::prop("Добыча ресурса", "pickaxe", 0.5f);
    double v = L.extractPct;
    if (ui::numberField("extract", v, {.min = 0, .max = 10000, .step = 5, .digits = 1, .unit = "%", .sign = true,
                                       .tooltip = "Добыча ресурса провинции больше на этот процент"}))
      a.act("Увеличение добычи", [&](Tx& tx) { rules::setLevelExtract(tx, b.id, li + 1, clamp(v, 0.0, 10000.0)); },
            {.coalesce = "bt-extract:" + std::to_string(b.id) + ":" + std::to_string(li)});
    a.markUi("bt.level." + std::to_string(li) + ".extract");
  }
  levelProduce(a, b, li, ro);
  levelProduceOnRes(a, b, li, ro);
  if (b.essenceGen) levelEssence(a, b, li, ro);
  if (b.drain) levelDrain(a, b, li, ro);
  // Модификаторы уровня: фишки (щелчок — редактор модификаторов, крестик — убрать) и окно выбора.
  ui::caption("Модификаторы уровня");
  std::vector<Id> mods = L.modifiers;
  if (tree::modifierList("mods", mods, ro, false)) a.act("Модификаторы уровня", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].modifiers = mods; });
  if (!ro) {
    if (ui::button("Выбрать модификаторы…", {.variant = ui::Variant::Ghost, .icon = "sparkles", .size = ui::Size::Small}))
      a.openDialog(levelModsDialog(b.id, li + 1));
    a.markUi("bt.level." + std::to_string(li) + ".mods");
  }
  bld::levelEffects(w, L, true);
  std::string desc = L.desc;
  if (ui::textArea("desc", desc, 48, {.placeholder = "Что даёт уровень", .maxLength = 400}) && desc != L.desc)
    a.act("Описание уровня", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].desc = desc; });
  // Удалить можно только последний уровень, если он нигде не построен.
  if (li == int(b.levels.size()) - 1 && b.levels.size() > 1) {
    ui::HStack hs(28, ui::Align::Right, 4);
    if (ui::iconButton("trash", "Удалить уровень " + roman(li + 1), {.size = ui::Size::Small, .tone = ui::Tone::Danger})) {
      Id bid = b.id;
      int n = int(b.levels.size());
      a.confirm("Удалить уровень " + roman(n) + "?", "Срок, стоимость и модификаторы уровня будут удалены. Действие можно отменить Ctrl+Z.", "Удалить", true,
                [bid, n](App& x) {
                  // Правило проверяет провинции и опускает требования других построек к удалённому уровню.
                  std::vector<Id> lowered;
                  bool ok = x.act("Удалить уровень постройки", [&](Tx& tx) {
                    if (int(tx.w().building(bid)->levels.size()) == n) lowered = rules::removeLastBuildingLevel(tx, bid);
                  });
                  if (ok && !lowered.empty()) {
                    std::vector<std::string> names;
                    for (Id d : lowered)
                      if (const Building* db = x.world().building(d)) names.push_back("«" + (db->name.empty() ? std::string("Без названия") : db->name) + "»");
                    x.toast("Требование к уровню " + roman(n) + " заменено на уровень " + roman(n - 1) + ": " + join(names, ", "), ToastKind::Info,
                            "building");
                  }
                });
    }
    a.markUi("bt.level." + std::to_string(li) + ".delete");
  }
}

}  // namespace rg::app::bldtree

namespace rg::app {

std::unique_ptr<Dialog> levelModsDialog(Id building, int level) {
  auto d = std::make_unique<bldtree::LevelMods>();
  d->bid = building;
  d->li = std::max(0, level - 1);
  return d;
}

}  // namespace rg::app
