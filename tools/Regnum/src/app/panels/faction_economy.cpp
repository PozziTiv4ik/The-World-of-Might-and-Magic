// Regnum — вкладка «Экономика» (ТЗ 1.e.i, 1.e.ii, 1.d.iv, 1.b.iv): казна и чистый доход, структура доходов и
// расходов с формулами в подсказках (рабы на работах, торговый флот, маршруты гильдии, содержание рабов), налог
// государства (≥ 0), таблица ресурсов (запас, добыча, торговля, расход провизии, итог; «Голод»), рабы по расам
// («Механика войн», п.1: численность, довольство −100…100, содержание 0,001 за раба), дань и репарации и вход в
// диалог «Навязать дань / репарации». Золото — до тысячных («Фиксы», п.13).
#include "app/panels/faction_common.h"

namespace rg::app {
namespace {

using namespace fac;

// Золото до тысячных: лишние нули не пишутся.
std::string gold(double v) { return fmtNum(std::fabs(v) < 5e-4 ? 0.0 : v, 3); }
std::string goldSigned(double v) { return fmtSigned(std::fabs(v) < 5e-4 ? 0.0 : v, 3); }

// Строка суммы (как fac::moneyRow, но до тысячных).
void goldRow(const char* icon, Color iconColor, std::string_view label, double value, std::string_view tip, bool sign = false,
             ui::Ink valueInk = ui::Ink::Normal) {
  ui::IdScope s(label);
  ui::Row row({ui::px(18), ui::fr(1), ui::px(108)}, 26, 8);
  ui::iconColored(icon, iconColor, 16);
  ui::label(label, {.ink = ui::Ink::Dim, .tooltip = tip});
  std::string v = sign ? goldSigned(value) : gold(value);
  ui::label(v, {.font = ui::Font::Strong, .ink = std::fabs(value) < 5e-4 ? ui::Ink::Muted : valueInk, .align = ui::Align::Right});
}

void moneyCards(const Faction& f, const rules::FactionCalc& fc) {
  const ui::Theme& t = ui::theme();
  const bool state = f.isState();
  // Доходы
  {
    Color cProv = t.accent, cGuildTax = t.info, cHq = t.success, cTrade = Color::mix(t.info, t.success, 0.5f), cTrib = t.warning;
    Color cSlaves = Color::mix(t.warning, t.danger, 0.4f), cFleet = Color::mix(t.info, t.accent, 0.5f), cRoutes = Color::mix(t.success, t.accent, 0.5f);
    if (ui::Section s("Доходы", "income", {.badge = gold(fc.incTotal)}); s) {
      ui::IdScope sc("income");
      Share parts[] = {{fc.incProvinces, cProv}, {fc.incGuildTax, cGuildTax}, {fc.incGuilds, cHq}, {fc.incTrade, cTrade}, {fc.incTribute, cTrib},
                       {fc.incSlaves, cSlaves}, {fc.incTradeFleet, cFleet}, {fc.incRoutes, cRoutes}};
      shareBar(parts);
      ui::spacer(2);
      if (state || fc.incProvinces != 0)
        goldRow("province", cProv, "Налог с провинций", fc.incProvinces,
                "Σ по провинциям: (торговая ценность − валовой доход гильдий со штабом) × общий налог.\n"
                "Общий налог = налог государства + местный, не меньше 1 %. Добыча золота и золото построек — тоже сюда.");
      if (state || fc.incGuildTax != 0)
        goldRow("guild", cGuildTax, "Налог гильдий", fc.incGuildTax, "Σ по провинциям: валовой доход гильдий со штабом × общий налог провинции");
      if (!state || fc.incGuilds != 0)
        goldRow("hq", cHq, "Доход штабов", fc.incGuilds, "Σ по штабам: торговая ценность × влияние гильдии − налог провинции");
      if (!state || fc.incRoutes != 0)
        goldRow("route", cRoutes, "Маршруты", fc.incRoutes, "5 % текущей торговой ценности каждой сухопутной провинции маршрутов гильдии");
      if ((state && fc.slaves > 0) || fc.incSlaves != 0)
        goldRow("shackles", cSlaves, "Рабы на работах", fc.incSlaves, "0,002 золота за раба на работах в провинциях за ход");
      if (fc.incTradeFleet != 0)
        goldRow("s-galleon", cFleet, "Торговый флот", fc.incTradeFleet, "Торговые галеоны в торговле: их содержание × 2");
      goldRow("trade", cTrade, "Торговля", fc.incTrade, "Поступления золота по торговым сделкам «каждый ход»");
      goldRow("tribute", cTrib, "Дань и репарации", fc.incTribute, "Поступления золота от плательщиков дани и репараций");
      if (fc.incomePct != 0)
        goldRow("percent", fc.incomePct > 0 ? t.success : t.danger, "Модификатор дохода " + fmtPct(fc.incomePct, 0, true), fc.incTotal - fc.incGross,
                "Собственный доход × (1 + Σ «доход в казну» / 100) — модификаторы, технологии, постройки", true,
                fc.incomePct > 0 ? ui::Ink::Success : ui::Ink::Danger);
      ui::separator();
      goldRow("coins", t.textDim, "Итого доходов", fc.incTotal, "Сумма доходов за ход с учётом модификатора дохода");
    }
  }
  // Расходы
  {
    Color cArmy = t.danger, cFleet = t.info, cSpec = t.accent, cTrade = Color::mix(t.info, t.success, 0.5f), cTrib = t.warning;
    Color cSlaves = Color::mix(t.warning, t.danger, 0.4f);
    if (ui::Section s("Расходы", "expense", {.badge = gold(fc.expTotal)}); s) {
      ui::IdScope sc("expense");
      Share parts[] = {{fc.expArmy, cArmy}, {fc.expFleet, cFleet}, {fc.expSpecialists, cSpec}, {fc.expTrade, cTrade}, {fc.expTribute, cTrib}, {fc.expSlaves, cSlaves}};
      shareBar(parts);
      ui::spacer(2);
      goldRow("army", cArmy, "Содержание войск", fc.expArmy,
              "Σ по строкам войск: численность общая × содержание одного × (1 + Σ «содержание войск» / 100)");
      goldRow("fleet", cFleet, "Содержание флота", fc.expFleet,
              "Σ по строкам флота: численность общая × содержание одного × (1 + Σ «содержание флота» / 100)");
      goldRow("council", cSpec, "Специалисты", fc.expSpecialists, "Σ содержания правителя, советников (любой фракции) и героев фракции; каждый учитывается один раз");
      if ((state && fc.slaves > 0) || fc.expSlaves != 0)
        goldRow("shackles", cSlaves, "Содержание рабов", fc.expSlaves, "0,001 золота за раба в ход");
      goldRow("trade", cTrade, "Торговля", fc.expTrade, "Выплаты золота по торговым сделкам «каждый ход»");
      goldRow("tribute", cTrib, "Дань и репарации", fc.expTribute, "Выплаты золота получателям дани и репараций");
      ui::separator();
      goldRow("coins", t.textDim, "Итого расходов", fc.expTotal, "Сумма расходов за ход");
    }
  }
}

void taxSection(App& a, const Faction& f, Id id, bool ro) {
  if (!f.isState()) return;
  if (ui::Section s("Налог", "percent"); s) {
    ui::prop("Налог государства", "percent");
    double tax = f.tax;
    if (ui::numberField("tax", tax, {.min = 0, .max = 100, .step = 1, .digits = 1, .unit = "%", .disabled = ro,
                                     .tooltip = "Одинаков для всех провинций государства, не меньше 0"}))
      a.act("Налог государства", [&](Tx& tx) { tx.faction(id).tax = std::max(0.0, tax); }, {.coalesce = "faction.tax:" + std::to_string(id)});
    a.markUi("economy.tax");
  }
}

// Количество за ход в узких столбцах: золото — до тысячных у малых сумм, прочие ресурсы — дробная часть только у малых
// нецелых значений.
int flowDigits(Id r, double v) {
  const double a = std::fabs(v);
  if (r == kGold) return a < 100 ? 3 : a < 10000 ? 1 : 0;
  return a < 10 && std::fabs(a - std::round(a)) > 0.05 ? 1 : 0;
}
std::string flowNum(Id r, double v) { return fmtNum(v, flowDigits(r, v)); }
std::string flowSigned(Id r, double v) { return fmtSigned(v, flowDigits(r, v)); }

void resourcesSection(App& a, const World& w, const Faction& f, const rules::FactionCalc& fc, Id id, bool ro) {
  std::vector<Id> res;
  for (auto& [r, flow] : fc.resources) {
    if (!w.resource(r) && r != kGold) continue;
    res.push_back(r);
  }
  // Золото первым, дальше — по справочнику.
  auto order = [&](Id r) {
    const auto& cat = w.catalogs->resources;
    for (size_t i = 0; i < cat.size(); i++)
      if (cat[i].id == r) return int(i);
    return 1 << 20;
  };
  std::stable_sort(res.begin(), res.end(), [&](Id x, Id y) {
    if ((x == kGold) != (y == kGold)) return x == kGold;
    return order(x) < order(y);
  });
  ui::Section s("Ресурсы", "resource", {.badge = std::to_string(res.size())});
  if (!s) return;
  {   // таблица заканчивается (и занимает место в потоке) до строк расхода под ней
    ui::Column cols[] = {{"Ресурс", nullptr, ui::fr(1, 92), ui::Align::Left, true},
                         {"Запас", nullptr, ui::px(72), ui::Align::Left, true, "Запас ресурса (казна — для золота)"},
                         {{}, "factory", ui::px(42), ui::Align::Right, true, "Добыча и постройки за ход"},
                         {{}, "trade", ui::px(46), ui::Align::Right, false, "Торговля за ход: приход / расход"},
                         {{}, "trend-up", ui::px(64), ui::Align::Right, true, "Итого за ход (с расходом)"}};
    ui::Table t("resources", cols, int(res.size()), {.rowHeight = 36, .selectable = false, .emptyIcon = "resource", .emptyText = "Ресурсов нет"});
    auto flowOf = [&](int i) -> const rules::ResourceFlow& { return fc.resources.at(res[size_t(i)]); };
    t.sort([&](int x, int y, int col) {
      auto num = [](double u, double v) { return u < v ? -1 : u > v ? 1 : 0; };
      switch (col) {
        case 1: return num(flowOf(x).stock, flowOf(y).stock);
        case 2: return num(flowOf(x).production, flowOf(y).production);
        case 4: return num(flowOf(x).net, flowOf(y).net);
        default: return compareRu(resourceName(w, res[size_t(x)]), resourceName(w, res[size_t(y)]));
      }
    });
    for (int i : t) {
      const Id r = res[size_t(i)];
      const rules::ResourceFlow& fl = flowOf(i);
      ui::IdScope sc{i64(r)};
      t.cell();
      {
        ui::Row rr({ui::px(16), ui::fr(1)}, 22, 5);
        ui::iconColored(w::resourceIcon(w, r), w::resourceColor(w, r), 16);
        ui::label(resourceName(w, r), {.font = r == kGold ? ui::Font::Strong : ui::Font::Body});
      }
      t.cell();
      double stock = r == kGold ? f.treasury() : f.stock(r);
      ui::NumberOpt no;
      no.min = r == kGold ? -1e12 : 0;   // казна может уйти в долг, прочие ресурсы — нет
      no.max = 1e12;
      no.step = r == kGold ? 10 : 1;
      no.digits = 3;                      // золото — до тысячных
      no.disabled = ro;
      no.tooltip = r == kGold ? "Казна" : "Запас ресурса";
      if (ui::numberField("stock", stock, no))
        a.act(r == kGold ? "Казна" : "Запас ресурса", [&](Tx& tx) { tx.faction(id).res[r] = r == kGold ? stock : std::max(0.0, stock); },
              {.coalesce = "faction.res:" + std::to_string(id) + ":" + std::to_string(r)});
      a.markUi("economy.stock." + std::to_string(r));
      t.text(fl.production > 0 ? flowNum(r, fl.production) : std::string("—"), fl.production > 0 ? ui::Ink::Normal : ui::Ink::Muted, ui::Font::Small);
      std::string tr;
      if (fl.tradeIn > 0) tr += "+" + flowNum(r, fl.tradeIn);
      if (fl.tradeOut > 0) tr += (tr.empty() ? "" : " ") + flowSigned(r, -fl.tradeOut);
      t.text(tr.empty() ? std::string("—") : tr, tr.empty() ? ui::Ink::Muted : ui::Ink::Dim, ui::Font::Small);
      ui::Ink ni = fl.net > 5e-4 ? ui::Ink::Success : fl.net < -5e-4 ? ui::Ink::Danger : ui::Ink::Muted;
      t.text(std::fabs(fl.net) < 5e-4 ? std::string("0") : flowSigned(r, fl.net), ni, ui::Font::Small);
    }
  }
  // Расход ресурсов (провизия государства живых: 0,001 на жителя за ход) — строкой под таблицей.
  for (Id r : res) {
    const rules::ResourceFlow& fl = fc.resources.at(r);
    if (!(fl.consumption > 0)) continue;
    ui::IdScope sc{i64(r)};
    {
      ui::Row row({ui::px(18), ui::fr(1), ui::px(108)}, 26, 8);
      ui::iconColored(w::resourceIcon(w, r), w::resourceColor(w, r), 16);
      ui::label("Расход: " + resourceName(w, r), {.ink = ui::Ink::Dim, .tooltip = "За ход: 0,001 на жителя государства живых"});
      ui::label(fmtSigned(-fl.consumption, 3), {.font = ui::Font::Strong, .ink = ui::Ink::Danger, .align = ui::Align::Right});
    }
    a.markUi("economy.use." + std::to_string(r));
  }
}

// Рабы государства по расам: численность, довольство, содержание (rules::setSlaves).
void slavesSection(App& a, const World& w, const Faction& f, const rules::FactionCalc& fc, Id id, bool ro) {
  if (!f.isState()) return;
  ui::Section s("Рабы", "shackles", {.badge = fc.slaves > 0 ? fmtNum(double(fc.slaves)) : std::string()});
  a.markUi("economy.slaves");
  if (!s) return;
  // Рабы на работах по расам (во всех провинциях государства).
  std::map<Id, i64> busy;
  w.provinces.each([&](const Province& p) {
    if (p.owner != id) return;
    for (const SlaveWork& sw : p.slaves) busy[sw.race] += std::max<i64>(0, sw.count);
  });
  const std::vector<SlaveGroup> groups = f.slaves;   // копия: действия посреди кадра меняют мир
  if (groups.empty()) ui::label("Рабов нет.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  else {
    ui::Column cols[] = {{"Раса", nullptr, ui::fr(1, 70)},
                         {{}, "population", ui::px(92), ui::Align::Left, false, "Численность рабов расы"},
                         {{}, "contentment", ui::px(72), ui::Align::Left, false, "Довольство рабов: −100…100"},
                         {{}, "expense", ui::px(56), ui::Align::Right, false, "Содержание за ход: 0,001 золота за раба"},
                         {"", nullptr, ui::px(ro ? 0.f : 24.f)}};
    ui::Table t("slaves", cols, int(groups.size()), {.rowHeight = 38, .striped = false, .selectable = false});
    for (int i : t) {
      const SlaveGroup g = groups[size_t(i)];
      ui::IdScope sc{i64(g.race)};
      const CatalogItem* rc = Catalogs::find(w.catalogs->races, g.race);
      const i64 atWork = busy.count(g.race) ? busy[g.race] : 0;
      t.cell();
      {
        ui::HStack hs(22, ui::Align::Left, 6);
        RectF d = ui::next(10, 10);
        ui::draw::circle(d.cx(), d.cy(), 4.5f, rc ? rc->color : ui::theme().textMuted);
        ui::label(rc ? (rc->name.empty() ? std::string("Без названия") : rc->name) : std::string("Раса"),
                  {.font = ui::Font::Small, .tooltip = atWork > 0 ? "На работах: " + fmtNum(double(atWork)) : std::string()});
      }
      t.cell();
      i64 n = g.count;
      if (ui::numberField("count", n, {.min = double(atWork), .max = 1e15, .step = 100, .disabled = ro,
                                       .tooltip = atWork > 0 ? "Не меньше занятых на работах: " + fmtNum(double(atWork)) : std::string()})) {
        Id race = g.race;
        double c = g.contentment;
        a.act("Рабы", [&](Tx& tx) { rules::setSlaves(tx, id, race, std::max<i64>(0, n), c); },
              {.coalesce = "faction.slaves:" + std::to_string(id) + ":" + std::to_string(race)});
      }
      a.markUi("economy.slave." + std::to_string(g.race) + ".count");
      t.cell();
      double c = g.contentment;
      if (ui::numberField("content", c, {.min = -100, .max = 100, .step = 1, .sign = true, .disabled = ro, .tooltip = "Довольство рабов: −100…100"})) {
        Id race = g.race;
        i64 cnt = g.count;
        a.act("Довольство рабов", [&](Tx& tx) { rules::setSlaves(tx, id, race, cnt, clamp(c, -100.0, 100.0)); },
              {.coalesce = "faction.slaveContent:" + std::to_string(id) + ":" + std::to_string(race)});
      }
      a.markUi("economy.slave." + std::to_string(g.race) + ".content");
      t.text(gold(double(std::max<i64>(0, g.count)) * schema::kSlaveUpkeep), ui::Ink::Dim, ui::Font::Small);
      t.cell();
      if (!ro) {
        if (ui::iconButton("close", "Убрать расу рабов", {.size = ui::Size::Small, .disabled = atWork > 0})) {
          Id race = g.race;
          double cc = g.contentment;
          a.act("Убрать рабов", [&](Tx& tx) {
            rules::setSlaves(tx, id, race, 0, cc);
            auto& list = tx.faction(id).slaves;
            list.erase(std::remove_if(list.begin(), list.end(), [&](const SlaveGroup& x) { return x.race == race; }), list.end());
          });
        }
        if (atWork > 0) ui::tooltip("Сначала снимите рабов этой расы с работ");
        a.markUi("economy.slaveDel." + std::to_string(g.race));
      }
    }
    if (t.footer()) {
      t.text("Итого", ui::Ink::Normal, ui::Font::Strong);
      t.text(fmtNum(double(fc.slaves)), ui::Ink::Normal, ui::Font::Strong);
      t.cell();
      t.text(gold(fc.expSlaves), ui::Ink::Dim, ui::Font::Strong);
      t.cell();
    }
  }
  // Добавить расу рабов (справочник рас общий с населением).
  if (!ro) {
    std::vector<const CatalogItem*> cand;
    for (const CatalogItem& c : w.catalogs->races)
      if (std::none_of(groups.begin(), groups.end(), [&](const SlaveGroup& g) { return g.race == c.id; })) cand.push_back(&c);
    if (!cand.empty()) {
      std::vector<std::string> labels;
      for (const CatalogItem* c : cand) labels.push_back(c->name.empty() ? std::string("Без названия") : c->name);
      std::vector<ui::Option> opts;
      for (size_t i = 0; i < cand.size(); i++) opts.push_back(ui::Option{labels[i], nullptr, cand[i]->color});
      int idx = -1;
      if (ui::combo("addSlaves", idx, opts, {.placeholder = "Добавить расу", .icon = "plus"}) && idx >= 0 && idx < int(cand.size())) {
        Id race = cand[size_t(idx)]->id;
        a.act("Рабы", [&](Tx& tx) { rules::setSlaves(tx, id, race, 0, 0); });
      }
      a.markUi("economy.slaveAdd");
    }
  }
}

// Дань и репарации, где фракция платит или получает (ТЗ 1.e.ii).
void tributeSection(App& a, const World& w, Id id, bool ro) {
  struct Row {
    const Deal* d;
    bool receive;
    double amount;
    int left, turns;
  };
  std::vector<Row> rows;
  w.deals.each([&](const Deal& d) {
    if (d.kind == DealKind::Trade || d.status != DealStatus::Active) return;
    if (d.a != id && d.b != id) return;
    Row r{&d, d.a == id, 0, 0, 0};
    for (const DealItem& it : d.items)
      if (it.res == kGold && it.mode == DealMode::PerTurn) {
        r.amount += it.amount;
        r.left = std::max(r.left, it.left);
        r.turns = std::max(r.turns, it.turns);
      }
    rows.push_back(r);
  });
  std::stable_sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) {
    if (x.receive != y.receive) return x.receive;
    return x.d->id < y.d->id;
  });
  ui::Section s("Дань и репарации", "tribute", {.badge = rows.empty() ? std::string() : std::to_string(rows.size())});
  if (!s) return;
  const ui::Theme& t = ui::theme();
  if (rows.empty()) ui::label("Нет действующих выплат.", {.ink = ui::Ink::Muted});
  for (const Row& r : rows) {
    const Deal& d = *r.d;
    const Id other = r.receive ? d.b : d.a;
    const Faction* of = w.faction(other);
    ui::IdScope sc{i64(d.id)};
    ui::Card c({.pad = 10, .tone = r.receive ? ui::Tone::Success : ui::Tone::Danger});
    const auto& kind = schema::kDealKinds[int(d.kind)];
    {
      ui::Row row({ui::px(20), ui::fr(1), ui::px(24)}, 24, 8);
      ui::icon(kind.icon, r.receive ? ui::Ink::Success : ui::Ink::Danger, 18, kind.name);
      ui::label(std::string(kind.name) + (r.receive ? " от" : " в пользу"), {.ink = ui::Ink::Dim});
      if (ui::iconButton("close", d.kind == DealKind::Tribute ? "Отменить дань" : "Отменить репарации",
                         {.size = ui::Size::Small, .disabled = ro, .tone = ui::Tone::Danger})) {
        Id did = d.id;
        std::string what = d.kind == DealKind::Tribute ? "дань" : "репарации";
        a.confirm(d.kind == DealKind::Tribute ? "Отменить дань?" : "Отменить репарации?",
                  "Выплаты прекратятся со следующего хода. Отменить можно сочетанием Ctrl+Z.", "Отменить " + what, true,
                  [did](App& x) { x.act("Отменить выплаты", [&](Tx& tx) { rules::cancelDeal(tx, did); }); });
      }
      a.markUi("tribute.cancel." + std::to_string(d.id));
    }
    {
      ui::Row row({ui::fr(1), ui::px(104)}, 26, 8);
      if (of) w::factionChip(other);   // в ячейке: естественная ширина, длинное название — с многоточием
      else ui::label("—", {.ink = ui::Ink::Muted});
      ui::label((r.receive ? "+" : "−") + gold(r.amount) + " / ход", {.font = ui::Font::Strong, .ink = r.receive ? ui::Ink::Success : ui::Ink::Danger,
                                                                           .align = ui::Align::Right, .tooltip = "Золото за ход"});
    }
    double k = r.turns > 0 ? double(r.left) / double(r.turns) : 0;
    ui::progress(k, {.color = r.receive ? t.success : t.danger, .height = 5, .text = "ещё " + nTurns(r.left)});
  }
  ui::spacer(2);
  {
    ui::Disabled dis(ro);
    if (ui::button("Навязать дань / репарации", {.icon = "tribute", .fill = true})) {
      if (!a.openDialog("tribute", id)) a.toast("Диалог дани и репараций пока недоступен", ToastKind::Warning, "tribute");
    }
    a.markUi("economy.impose");
  }
}

void drawEconomy(App& a, Id id) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(id);
  if (!f) return;
  const bool ro = a.readOnly();
  auto calc = rules::calc(w);
  const rules::FactionCalc* fc = calc->faction(id);
  if (!fc) return;
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 10);
    ui::stat(gold(f->treasury()), f->treasury() < 0 ? "Казна в долгу" : "Казна",
             {.icon = "treasury", .tone = f->treasury() < 0 ? ui::Tone::Danger : ui::Tone::Accent,
              .tooltip = "Текущая казна; при завершении хода прибавляется чистый доход"});
    a.markUi("economy.treasury");
    ui::stat(goldSigned(fc->net), "Чистый доход", {.icon = fc->net >= 0 ? "trend-up" : "trend-down",
                                                    .tone = fc->net >= 0 ? ui::Tone::Success : ui::Tone::Danger,
                                                    .tooltip = "Доходы − расходы за ход; прибавляется к казне при завершении хода"});
  }
  // Голод (ТЗ «Общие доработки», п.7): запас провизии меньше нуля.
  if (fc->famine) {
    ui::HStack hs(26, ui::Align::Left, 6);
    const Modifier* m = rules::builtinMod(w, schema::mod::Famine);
    ui::tag(m && !m->name.empty() ? m->name : std::string("Голод"), ui::Tone::Danger, "warning");
    ui::tooltip("Запас провизии меньше нуля: действует модификатор «Голод»");
    a.markUi("economy.famine");
  }
  ui::spacer(2);
  moneyCards(*f, *fc);
  taxSection(a, *f, id, ro);
  resourcesSection(a, w, *f, *fc, id, ro);
  slavesSection(a, w, *f, *fc, id, ro);
  tributeSection(a, w, id, ro);
}

int economyBadge(App& a, Id id) {
  auto calc = rules::calc(a.world());
  const rules::FactionCalc* fc = calc->faction(id);
  return fc && (fc->net < -0.5 || fc->famine) ? -1 : 0;
}

TabReg tab({kTabEconomy, "treasury", "Экономика", 30, SelType::Faction, nullptr, drawEconomy, economyBadge});

}  // namespace
}  // namespace rg::app
