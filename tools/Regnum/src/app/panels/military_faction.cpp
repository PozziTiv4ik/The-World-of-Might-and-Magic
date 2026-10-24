// Regnum — вкладки фракции «Войска» и «Флот» (ТЗ 1.c.i–ii, 1.d.iii; «Общие доработки», п.10–11; «Виды государств»,
// п.2; «Ввод новых механик», п.1–5) и полноэкранные таблицы войск и флота.
//
// Таблица строк: тип (14 типов войск или 3 типа судов), наименование, раса отряда, ключевой ресурс юнита, цена найма
// за юнит, численность общая, в поле (в море), в резерве (и формируется), содержание одного и общее (с модификатором
// содержания). Отряды формируются из населения (нежить — из трупов, демоны — из демонической энергии; звери, чудовища,
// военные механизмы и элементали — без людей) за ключевой и дополнительные ресурсы и эссенции элементов; корабли — за
// ресурсы констант стоимости; через 2 хода — в резерв; резерв распускается обратно в население. Элементали
// содержатся эссенциями, а не золотом. Особые отряды справочника нанимаются, пока у государства достроена постройка
// доступа; их тип, раса и цены — из справочника. Общая численность правится напрямую только в режиме «Правка
// резерва» (без расхода людей, ресурсов и эссенций). Широкая таблица правится в ячейках и показывает столбцы по
// ширине (наименованию — не меньше kNameMin); узкая — в две строки, правка — в карточке выбранной строки (там же
// списки ресурсов и эссенций на юнит). Во вкладке флота — участие флота в торговле.
// Роли (ТЗ «Доработки №4», п.5): найм и роспуск, новые строки, наёмники, флот в торговле и у верфи, объекты на карте —
// игровые действия за фракцию (App::play); «Правка резерва» и показатели строк — правка мира (администратор). Чужие
// войска игрок видит только для чтения.
#include "app/panels/military_internal.h"

namespace rg::app::mil {
namespace forces {

// ---------------------------------------------------------------- флот в торговле
// ТЗ «Общие доработки», п.11: корабли из резерва участвуют в торговле, не выходя на карту. Торговые галеоны приносят
// своё содержание × 2; каждые 10 галеонов без охраны (5 фрегатов или 1 линкор) — 1 % вероятности нападения пиратов.
void tradeFleetSection(App& a, Id fid) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(fid);
  if (!f) return;
  auto c = rules::calc(w);
  const rules::FactionCalc* fc = c->faction(fid);
  i64 inTrade = 0;
  for (const GarrisonEntry& g : f->tradeFleet) inTrade += g.count;
  ui::Section s("Участие флота в торговле", "trade", {.badge = fmtCount(inTrade), .card = false});
  a.markUi("mil.trade");
  if (!s) return;
  if (f->fleet.empty()) {
    ui::label("Кораблей нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "anchor"});
    return;
  }
  const double income = fc ? fc->incTradeFleet : 0, next = fc ? fc->pirateRisk : 0;
  // Доход торгового галеона — константа мира за каждый корабль в торговле (от содержания не зависит, как в rules::calc).
  const double each = std::max(0.0, rules::constantOf(w, schema::cst::GalleonIncome).num);
  {
    // Риск пиратов: бросок в конце этого хода (сгенерирован в конце прошлого) и новый — по кораблям в торговле.
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
    ui::stat((income > 0 ? "+" : "") + fmtGold(income), "Прибыль за ход", {.icon = "coins", .tone = ui::Tone::Success,
                                                                            .tooltip = "Торговые галеоны в торговле × «Доход торгового галеона» " + fmtGold(each)});
    const std::string nextText = "→ " + fmtPct(next, 1);
    ui::stat(fmtPct(f->pirateRisk, 1), "Пираты", {.icon = "dice", .tone = f->pirateRisk > 0 ? ui::Tone::Danger : ui::Tone::Neutral,
                                                  .delta = next - f->pirateRisk, .deltaText = nextText, .invertDelta = true,
                                                  .tooltip = "Вероятность нападения пиратов в конце этого хода (погибнет 5 % галеонов в торговле); "
                                                             "→ сгенерируется в конце хода: 1 % за каждые 10 галеонов без охраны (5 фрегатов или 1 линкор)"});
    a.markUi("mil.trade.pirates");
  }
  struct L {
    UnitRow row;
    i64 trade, reserve;
    double income;
  };
  std::vector<L> lines;
  for (const UnitRow& r : unitRows(w, fid, true)) {
    const rules::RowCalc* rc = fc ? rowCalc(*c, fid, r.id, true) : nullptr;
    const i64 tr = rc ? rc->trade : 0;
    const double inc = r.type == int(ShipType::Galleon) ? double(tr) * each : 0;
    lines.push_back(L{r, tr, rc ? std::max<i64>(0, rc->reserve) : 0, inc});
  }
  const bool ro = !a.canPlay(fid);   // участие флота в торговле — игровое действие
  ui::Column cols[] = {{"Судно", nullptr, ui::fr(1, 110)},
                       {"В торговле", nullptr, ui::px(104), ui::Align::Left, false, "Корабли из резерва в торговле"},
                       {"Резерв", nullptr, ui::px(64), ui::Align::Right, false, "Свободно в резерве"},
                       {"", "coins", ui::px(70), ui::Align::Right, false, "Прибыль за ход"}};
  ui::Table t("trade", cols, int(lines.size()), {.rowHeight = 40, .selectable = false});
  for (int i : t) {
    const L& l = lines[size_t(i)];
    RectF cr = t.cell();
    unitCell(cr, l.row, l.trade > 0);
    t.cell();
    {
      ui::Disabled dis(ro);
      i64 n = l.trade;
      if (ui::numberField("n", n, {.min = 0, .max = double(l.trade + l.reserve), .tooltip = "Не больше резерва: " + fmtCount(l.reserve) + " свободно"})) {
        const Id row = l.row.id;
        a.play("Флот в торговле", [&](Tx& tx) { rules::setTradeFleet(tx, fid, row, n); }, {.coalesce = "tradefleet:" + std::to_string(row)});
      }
      a.markUi("mil.trade." + std::to_string(l.row.id));
    }
    t.text(fmtCount(l.reserve), l.reserve > 0 ? ui::Ink::Success : ui::Ink::Muted);
    t.text(l.income > 0 ? "+" + fmtGold(l.income) : std::string("—"), l.income > 0 ? ui::Ink::Success : ui::Ink::Muted);
  }
}

// ---------------------------------------------------------------- объекты и содержание
// Объекты фракции на карте (щелчок — выделить и показать).
void objectsOnMap(App& a, Id fid, bool fleet) {
  const World& w = frameWorld(a);
  std::vector<const Army*> list;
  w.armies.each([&](const Army& ar) {
    if (ar.isFleet() != fleet) return;
    for (const ArmyGroup& g : ar.groups)
      if (g.faction == fid) {
        list.push_back(&ar);
        return;
      }
  });
  std::sort(list.begin(), list.end(), [](const Army* x, const Army* y) { return compareRu(x->name, y->name) < 0; });
  std::string badge = std::to_string(list.size());
  ui::Section s(fleet ? "Флоты на карте" : "Войска на карте", fleet ? "fleet" : "army", {.badge = badge, .card = false});
  if (!s) return;
  const bool ro = !a.canPlay(fid);   // объекты на карте — игровое действие за фракцию
  if (list.empty()) {
    if (ro && !a.admin()) {   // чужие войска у игрока — без кнопки
      ui::emptyState(fleet ? "fleet" : "army", fleet ? "Флотов на карте нет." : "Войск на карте нет.");
      return;
    }
    ui::Disabled dis(ro);
    if (fleet) {   // флот — у верфи, если свободное редактирование флотов выключено (ТЗ «Доработки №3», п.3)
      placeFleetButton(a, fid, true);
      return;
    }
    if (ui::emptyState("army", "Войск на карте нет.", "Поставить войско", "tool-army")) startPlacing(a, ArmyKind::Army, fid);
    a.markUi("mil.army.placeEmpty");
    return;
  }
  for (const Army* ar : list) {
    std::string sub = w.provinceName(provinceUnder(w, ar->pos));
    if (const Id fl = rules::carrierOf(w, ar->id)) sub = "на борту «" + objectName(*w.army(fl)) + "»";   // войско на борту флота
    else if (rules::cargoOf(w, ar->id)) sub += " · с войском";
    if (ar->allied()) {
      std::vector<std::string> names;
      for (Id f : factionsIn(*ar))
        if (f != fid) names.push_back(w.factionName(f));
      sub = "союз с " + join(names, ", ") + " · " + sub;
    }
    // Численность чужого объекта у игрока скрыта (туман войны: только «есть войско»).
    if (objectItem(w, *ar, a.ui.sel == Selection{SelType::Army, ar->id}, sub, !a.knows(fid))) a.select(SelType::Army, ar->id, true);
    a.markUi("mil.object." + std::to_string(ar->id));
  }
  if (ro) return;
  ui::HStack hs(0, ui::Align::Left, 6);
  if (fleet) placeFleetButton(a, fid, false);
  else if (ui::button("Поставить войско", {.variant = ui::Variant::Ghost, .icon = "tool-army", .size = ui::Size::Small}))
    startPlacing(a, ArmyKind::Army, fid);
}

// Списание из казны за ход: войска + флот (ТЗ 1.c.ii).
void upkeepCard(App& a, Id fid, bool fleet) {
  auto c = rules::calc(frameWorld(a));
  const rules::FactionCalc* fc = c->faction(fid);
  double army = fc ? fc->expArmy : 0, fl = fc ? fc->expFleet : 0;
  ui::Card card({.pad = 12});
  ui::Row r({ui::px(36), ui::fr(1), ui::px(120)}, 40, 10);
  {
    RectF ic = ui::next(36, 36);
    ui::draw::rect(ic, ui::toneColor(ui::Tone::Danger).alpha(0.14f), 10);
    ui::draw::icon("expense", ic.inset(9), ui::toneColor(ui::Tone::Danger));
  }
  {
    ui::Group g(0, 0);
    ui::label("Из казны за ход", {.font = ui::Font::Strong});
    std::string parts = std::string(fleet ? "флот " : "войска ") + fmtGold(fleet ? fl : army) + " + " + (fleet ? "войска " : "флот ") + fmtGold(fleet ? army : fl);
    ui::label(parts, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  ui::label(fmtGoldSigned(-(army + fl)), {.font = ui::Font::Number, .ink = army + fl > 0 ? ui::Ink::Danger : ui::Ink::Muted, .align = ui::Align::Right,
                                         .tooltip = "Сумма содержания общего из таблиц войск и флота"});
}

void drawForces(App& a, Id fid, bool fleet, bool fullscreen) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(fid);
  if (!f) return;
  ui::IdScope scope(fleet ? "fleet" : "army");
  // Туман войны: у чужой фракции игрок видит только её объекты на карте, без численности; таблицы, резерв, найм,
  // содержание и торговля флота скрыты.
  if (!a.knows(fid)) {
    w::hiddenMark();
    a.markUi(fleet ? "mil.fleet.hidden" : "mil.army.hidden");
    if (!fullscreen) {
      ui::spacer(4);
      objectsOnMap(a, fid, fleet);
    }
    return;
  }
  auto& st = ui::state<ForcesState>(ui::id("forces"));
  const float aw = ui::avail().w;
  bool wide = aw >= kWide;
  size_t n = fleet ? f->fleet.size() : f->army.size();
  const bool play = a.canPlay(fid);   // найм, роспуск, новые строки — игровые действия
  if (!a.admin()) st.editReserve = false;   // «Правка резерва» — правка мира
  if (n > 0) {   // пустая таблица — без нулевых показателей
    statTiles(a, fid, fleet, wide);
    ui::spacer(4);
  }
  {
    ui::Section s(fleet ? "Корабли" : "Отряды", fleet ? "fleet" : "army",
                  {.badge = std::to_string(n), .card = false, .actionIcon = play ? "plus" : nullptr,
                   .actionTooltip = fleet ? "Добавить судно" : "Добавить отряд"});
    if (s.action()) ui::openPopup("addrow");
    a.markUi(fleet ? "mil.fleet.add" : "mil.army.add");
    addRowMenu(a, "addrow", fid, fleet, st);
    if (s) {
      if (!fleet) mercBar(a, fid);   // «Создать войско наемников» и лимит (military_merc.cpp)
      if (n == 0) {
        ui::Disabled dis(!play);
        const bool action = play || a.admin();   // игрок у чужой фракции — без кнопки
        if (ui::emptyState(fleet ? "fleet" : "army", fleet ? "В таблице флота пока нет судов." : "В таблице войск пока нет отрядов.",
                           action ? (fleet ? "Добавить судно" : "Добавить отряд") : "", "plus"))
          ui::openPopup("addrow2");
        a.markUi(fleet ? "mil.fleet.empty" : "mil.army.empty");
        addRowMenu(a, "addrow2", fid, fleet, st);
      } else {
        if (a.admin()) {
          ui::HStack hs(30, ui::Align::Left, 8);
          ui::Disabled dis(a.readOnly());
          ui::toggle("Правка резерва", st.editReserve);
          ui::tooltip(fleet ? "Общая численность строк правится напрямую, без формирования и роспуска: ресурсы не расходуются"
                            : "Общая численность строк правится напрямую, без формирования и роспуска: население, ресурсы и эссенции "
                              "не расходуются");
          a.markUi(fleet ? "mil.fleet.editReserve" : "mil.army.editReserve");
        }
        const Shown shown = forcesTable(a, fid, fleet, st, wide);
        if (st.selRow) {
          if (rowCard(a, fid, fleet, st, !wide, shown)) {
            if (st.shownRow != st.selRow) ui::scrollToItem();   // новая карточка — в видимую часть
            st.shownRow = st.selRow;
          }
        }
      }
    }
  }
  formingList(a, fid, fleet);
  if (fleet && n > 0) {
    ui::spacer(4);
    tradeFleetSection(a, fid);
  }
  if (!fullscreen) {   // во весь экран списание за ход — одна карточка над обеими таблицами
    ui::spacer(4);
    upkeepCard(a, fid, fleet);
    ui::spacer(4);
    objectsOnMap(a, fid, fleet);
    ui::spacer(2);
    if (ui::button("Таблицы войск и флота", {.variant = ui::Variant::Ghost, .icon = "maximize", .size = ui::Size::Small,
                                             .tooltip = "Открыть таблицы во весь экран"}))
      a.openEditor("military", fid);
    a.markUi("mil.fullscreen");
  }
}

void drawArmyTab(App& a, Id fid) { drawForces(a, fid, false, false); }
void drawFleetTab(App& a, Id fid) { drawForces(a, fid, true, false); }

// ---------------------------------------------------------------- полноэкранные таблицы
void drawEditor(App& a, Id arg) {
  const World& w = frameWorld(a);
  struct EditorState {
    Id cur = 0;   // показанная фракция (меняется выбором вверху)
    Id arg = 0;   // фракция, с которой редактор открыт
  };
  auto& es = ui::state<EditorState>(ui::id("faction"));
  if (arg != es.arg) {
    es.arg = arg;
    if (arg) es.cur = arg;
  }
  Id& cur = es.cur;
  if (!w.faction(cur)) {
    cur = 0;
    std::vector<const Faction*> all = w.factions.all();
    std::sort(all.begin(), all.end(), [](const Faction* x, const Faction* y) {
      if (x->kind != y->kind) return x->kind < y->kind;
      return compareRu(x->name, y->name) < 0;
    });
    if (!all.empty()) cur = all.front()->id;
  }
  if (!cur) {
    ui::emptyState("crown", "Фракций пока нет.");
    return;
  }
  const Faction& f = *w.faction(cur);
  {
    ui::Row head({ui::px(54), ui::fr(1), ui::px(300)}, 40, 12);
    factionFlag(w, cur, 54, 36);
    {
      ui::Group g(0, 0);
      ui::caption(f.isGuild() ? "Торговая гильдия" : "Государство");
      ui::label(f.name.empty() ? std::string("Без названия") : f.name, {.font = ui::Font::Heading});
    }
    Id pick = cur;
    if (w::factionPicker("pick", pick, w::FactionFilter::Any, "")) cur = pick;
    a.markUi("mil.editor.faction");
  }
  ui::spacer(4);
  ui::Scroll sc("body");
  ui::IdScope s{i64(cur)};
  if (a.knows(cur)) upkeepCard(a, cur, false);   // ТЗ 1.c.ii: содержание из обеих таблиц — из казны каждый ход
  ui::spacer(8);
  ui::label("Войска", {.font = ui::Font::Title, .icon = "army"});
  drawForces(a, cur, false, true);
  ui::spacer(12);
  ui::label("Флот", {.font = ui::Font::Title, .icon = "fleet"});
  drawForces(a, cur, true, true);
  ui::spacer(8);
}

}  // namespace forces

namespace {
using namespace forces;
TabReg armyTab({kTabArmy, "army", "Войска", 50, SelType::Faction, nullptr, drawArmyTab, nullptr, true});
TabReg fleetTab({kTabFleet, "fleet", "Флот", 52, SelType::Faction, nullptr, drawFleetTab, nullptr, true});
EditorReg editorReg({"military", "Войска и флот", drawEditor, "army"});

}  // namespace
}  // namespace rg::app::mil
