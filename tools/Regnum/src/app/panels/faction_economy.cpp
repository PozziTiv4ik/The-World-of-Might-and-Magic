// Regnum — вкладка «Экономика» (ТЗ 1.e.i, 1.e.ii, 1.d.iv, 1.b.iv): казна и чистый доход, структура доходов и
// расходов с формулами в подсказках (рабы на работах, торговый флот, маршруты гильдии, содержание рабов), налог
// государства (≥ 0), провизия государства живых (группа «Провизия» с подгруппами: запас, расход населения поровну с
// её ресурсов, недостача — «Голод», что будет после хода), таблица ресурсов по группам справочника (сворачиваемые
// группы с суммами, поиск, «скрыть пустые»; запас правится в строке; приход — добыча, постройки и преобразование,
// расход — провизия и преобразование; «Золото», «Трупы» и «Демоническая энергия» — всегда первыми), эссенции
// элементов (запас, генерация построек, содержание элементалей, долг — красным), рабы по расам («Механика войн»,
// п.1), дань и репарации и вход в диалог «Навязать дань / репарации». Золото и ресурсы — до тысячных.
#include "app/panels/faction_common.h"

namespace rg::app::edkit {   // editors/modifiers.cpp — фишки с переносом
void chipsBegin();
ui::ChipAction chip(std::string_view label, const ui::ChipOpt& o);
void chipsEnd();
}  // namespace rg::app::edkit

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

// ---------------------------------------------------------------- ресурсы, провизия, эссенции
constexpr double kEps = 5e-4;   // меньше половины тысячной — ноль

// Предпочтения вкладки на время работы программы (раскрытие групп, «скрыть пустые», поиск); другой мир или папка
// данных (сценарий теста) — начинаются заново. Ресурсов ~150: по умолчанию пустые (запас 0 и нет потоков) скрыты —
// видно, что у государства есть; значок в заголовке показывает все, поиск находит и пустые.
struct EcoPrefs {
  u64 session = 0;
  std::map<Id, bool> open;   // группа ресурсов раскрыта (нет записи — раскрыта, если в ней что-то есть)
  bool hideEmpty = true, hideEmptyEss = false;
  std::string query;
};
EcoPrefs& prefs(App& a) {
  static EcoPrefs p;
  const Meta& m = *a.store.world().meta;
  const u64 tag = hashMix(hash64(a.dataDir()), hash64(m.createdAt + "|" + m.basemap));
  if (p.session != tag) p = EcoPrefs{tag};
  return p;
}

// Приход (добыча и постройки, выход преобразования) и расход (провизия населения, вход преобразования) ресурса.
double inflow(const rules::ResourceFlow& f) { return f.production + f.conversionOut; }
double outflow(const rules::ResourceFlow& f) { return f.consumption + f.conversionIn; }
bool flowEmpty(const rules::ResourceFlow& f) {
  return std::fabs(f.stock) < kEps && std::fabs(f.production) < kEps && f.tradeIn < kEps && f.tradeOut < kEps && f.consumption < kEps &&
         f.conversionIn < kEps && f.conversionOut < kEps;
}

// Ресурсы, которые видны всегда и первыми (ТЗ «Исправления», п.5: «я хочу сразу видеть наличие… "Трупы" и
// "Демоническая энергия"… что бы я мог сразу начислять изначальные значения»): золото, трупы, демоническая энергия.
bool pinned(const World& w, Id r) {
  if (r == kGold) return true;
  const CatalogItem* c = w.resource(r);
  return c && (c->key == schema::kResCorpses || c->key == schema::kResEnergy);
}

// Числа таблицы: до тысячных, лишние нули не пишутся.
std::string amount(double v) { return std::fabs(v) < kEps ? std::string("0") : money(v); }
std::string amountSigned(double v) { return std::fabs(v) < kEps ? std::string("0") : moneySigned(v); }
ui::Ink netInk(double v) { return v > kEps ? ui::Ink::Success : v < -kEps ? ui::Ink::Danger : ui::Ink::Muted; }

// Сумма по группе (с подгруппами): запас, приход, торговля, расход, итог; ресурсов и непустых.
struct Sums {
  double stock = 0, in = 0, trade = 0, out = 0, net = 0;
  int count = 0, filled = 0;
};

// Строка таблицы: ресурс или заголовок группы (depth — вложенность).
struct DispRow {
  bool group = false;
  Id id = 0;
  int depth = 0;
};

struct ResourceView {
  std::vector<DispRow> rows;
  std::map<Id, Sums> sums;
  int filled = 0;               // непустых ресурсов
  int hidden = 0;               // скрыто пустых («скрыть пустые»)
  bool anyClosed = false;       // есть свёрнутые группы (для «Развернуть все»)
};

ResourceView buildRows(App& a, const World& w, const rules::FactionCalc& fc) {
  EcoPrefs& pf = prefs(a);
  ResourceView v;
  const Catalogs& cat = *w.catalogs;
  auto flowOf = [&](Id r) -> const rules::ResourceFlow* {
    auto it = fc.resources.find(r);
    return it == fc.resources.end() ? nullptr : &it->second;
  };
  auto empty = [&](Id r) {
    const rules::ResourceFlow* f = flowOf(r);
    return !f || flowEmpty(*f);
  };
  const std::string q = trim(pf.query);
  auto matches = [&](Id r) {
    if (q.empty()) return true;
    const CatalogItem* c = w.resource(r);
    const std::string name = r == kGold ? std::string("Золото") : c ? c->name : std::string();
    return utf8::matches(name, q) || (c && c->group && utf8::matches(rules::groupPath(w, c->group), q));
  };
  auto shown = [&](Id r) { return matches(r) && (pinned(w, r) || !q.empty() || !pf.hideEmpty || !empty(r)); };
  // Суммы групп: ресурс — во всех группах-предках.
  for (const CatalogItem& c : cat.resources) {
    const rules::ResourceFlow* f = flowOf(c.id);
    if (!empty(c.id)) v.filled++;
    else if (matches(c.id) && !shown(c.id)) v.hidden++;
    if (pinned(w, c.id) || !c.group) continue;
    Id g = c.group;
    for (int guard = 0; g && guard < 64; guard++) {
      Sums& s = v.sums[g];
      s.count++;
      if (f) {
        s.stock += f->stock;
        s.in += inflow(*f);
        s.trade += f->tradeIn - f->tradeOut;
        s.out += outflow(*f);
        s.net += f->net;
        if (!flowEmpty(*f)) s.filled++;
      }
      const ResGroup* x = cat.group(g);
      g = x ? x->parent : 0;
    }
  }
  if (!w.resource(kGold) || shown(kGold)) v.rows.push_back({false, kGold, 0});
  for (const CatalogItem& c : cat.resources)
    if (c.id != kGold && pinned(w, c.id) && shown(c.id)) v.rows.push_back({false, c.id, 0});
  for (const CatalogItem& c : cat.resources)
    if (!pinned(w, c.id) && (!c.group || !cat.group(c.group)) && shown(c.id)) v.rows.push_back({false, c.id, 0});
  // Группы деревом: заголовок, подгруппы, затем свои ресурсы группы.
  std::function<bool(Id, int, std::vector<DispRow>&)> walk = [&](Id g, int depth, std::vector<DispRow>& out) {
    std::vector<DispRow> inner;
    bool any = false;
    for (Id c : rules::childGroups(w, g))
      if (depth < 16) any = walk(c, depth + 1, inner) || any;
    for (const CatalogItem& c : cat.resources)
      if (c.group == g && !pinned(w, c.id) && shown(c.id)) {
        inner.push_back({false, c.id, depth + 1});
        any = true;
      }
    // Пустая для фильтра группа (поиск, «скрыть пустые») не показывается.
    if ((!q.empty() || pf.hideEmpty) && !any) return false;
    out.push_back({true, g, depth});
    auto it = pf.open.find(g);
    const bool open = !q.empty() || (it != pf.open.end() ? it->second : v.sums[g].filled > 0);
    if (!open) v.anyClosed = true;
    if (open) out.insert(out.end(), inner.begin(), inner.end());
    return true;
  };
  for (Id g : rules::childGroups(w, 0)) walk(g, 0, v.rows);
  return v;
}

void resourceTable(App& a, const World& w, const Faction& f, const rules::FactionCalc& fc, Id id, bool ro, ResourceView& v,
                   std::span<const ui::Column> cols, bool wide);

void resourcesSection(App& a, const World& w, const Faction& f, const rules::FactionCalc& fc, Id id, bool ro) {
  EcoPrefs& pf = prefs(a);
  const ui::Theme& th = ui::theme();
  ResourceView v = buildRows(a, w, fc);
  ui::Section s("Ресурсы", "resource",
                {.badge = std::to_string(v.filled), .actionIcon = pf.hideEmpty ? "eye-off" : "eye",
                 .actionTooltip = pf.hideEmpty ? "Показать все ресурсы" : "Скрыть пустые ресурсы"});
  {
    const RectF hr = ui::lastItem().rect;   // заголовок раздела; кнопка — у правого края
    a.markUi("economy.resources", hr);
    a.markUi("economy.hideEmpty", RectF{hr.right() - (th.padCard - 4) - 26, hr.cy() - 13, 26, 26});
  }
  if (s.action()) pf.hideEmpty = !pf.hideEmpty;
  if (!s) return;
  {
    ui::Row r({ui::fr(1), ui::px(30)}, 30, 6);
    ui::searchField("resq", pf.query, "Найти ресурс");
    a.markUi("economy.search");
    // Все группы — раскрыть или свернуть.
    if (ui::iconButton(v.anyClosed ? "expand" : "collapse", v.anyClosed ? "Развернуть все группы" : "Свернуть все группы")) {
      const bool open = v.anyClosed;
      for (const ResGroup& g : w.catalogs->resGroups) pf.open[g.id] = open;
    }
    a.markUi("economy.groupsAll");
  }
  // Сколько пустых скрыто — ссылка показывает все (то же, что значок в заголовке).
  auto hiddenLink = [&] {
    if (!v.hidden) return;
    if (ui::link("Пустые ресурсы: " + std::to_string(v.hidden), "eye")) pf.hideEmpty = false;
    a.markUi("economy.showEmpty");
  };
  if (v.rows.empty()) {
    ui::label("Ничего не найдено", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    hiddenLink();
    return;
  }
  const bool wide = ui::avail().w >= 620;
  ui::Column wideCols[] = {{"Ресурс", nullptr, ui::fr(1, 140)},
                           {"Запас", nullptr, ui::px(110), ui::Align::Left, false, "Запас ресурса (у золота — казна)"},
                           {{}, "factory", ui::px(84), ui::Align::Right, false, "Приход за ход: добыча, постройки, преобразование"},
                           {{}, "trade", ui::px(92), ui::Align::Right, false, "Торговля за ход: приход и расход"},
                           {{}, "expense", ui::px(84), ui::Align::Right, false, "Расход за ход: провизия населения, преобразование"},
                           {{}, "trend-up", ui::px(92), ui::Align::Right, false, "Итого за ход"}};
  // Узкая панель справа от карты: название, запас, итог (приход, торговля и расход — в подсказке итога).
  ui::Column narrowCols[] = {{"Ресурс", nullptr, ui::fr(1, 100)},
                             {"Запас", nullptr, ui::px(88), ui::Align::Left, false, "Запас ресурса (у золота — казна)"},
                             {{}, "trend-up", ui::px(76), ui::Align::Right, false, "Итого за ход"}};
  const std::span<const ui::Column> cols = wide ? std::span<const ui::Column>(wideCols) : std::span<const ui::Column>(narrowCols);
  resourceTable(a, w, f, fc, id, ro, v, cols, wide);
  hiddenLink();
}

// Таблица ресурсов раздела: строки групп (свернуть, суммы) и ресурсов (запас — правка в строке).
void resourceTable(App& a, const World& w, const Faction& f, const rules::FactionCalc& fc, Id id, bool ro, ResourceView& v,
                   std::span<const ui::Column> cols, bool wide) {
  EcoPrefs& pf = prefs(a);
  const ui::Theme& th = ui::theme();
  ui::Table t("resources", cols, int(v.rows.size()), {.rowHeight = 36, .selectable = false, .emptyIcon = "resource", .emptyText = "Ресурсов нет"});
  for (int i : t) {
    const DispRow& row = v.rows[size_t(i)];
    const float indent = float(row.depth) * (wide ? 16 : 10);
    if (row.group) {
      // Заголовок группы: щелчок по строке — свернуть или раскрыть; суммы по группе с подгруппами.
      const Id g = row.id;
      const ResGroup* gr = w.catalogs->group(g);
      const Sums& sm = v.sums[g];
      auto it = pf.open.find(g);
      const bool open = !trim(pf.query).empty() || (it != pf.open.end() ? it->second : sm.filled > 0);
      a.markUi("economy.group." + std::to_string(g), t.rowRect());
      bool toggle = t.clicked() == i, hover = t.hovered() == i;
      ui::draw::rect(t.rowRect(), th.text.alpha(row.depth == 0 ? 0.05f : 0.025f), 6);
      const RectF cr = t.cell();
      float x = cr.x + indent;
      ui::draw::icon(open ? "chevron-down" : "chevron-right", RectF{x, cr.cy() - 7, 14, 14}, th.textMuted);
      x += 18;
      if (wide) {   // в узкой панели место — названию
        ui::draw::icon(open ? "folder-open" : "folder", RectF{x, cr.cy() - 8, 16, 16}, row.depth == 0 ? th.accent : th.textDim);
        x += 22;
      }
      const std::string name = gr && !gr->name.empty() ? gr->name : std::string("Без названия");
      const ui::Font nf = row.depth == 0 ? ui::Font::Strong : ui::Font::Body;
      // Число непустых ресурсов группы из всех — если хватает места после названия.
      const std::string cnt = std::to_string(sm.filled) + "/" + std::to_string(sm.count);
      const float room = std::max(0.f, cr.right() - x), nw = ui::measure(name, nf) + 2, cw = ui::measure(cnt, ui::Font::Caption) + 4;
      ui::draw::text(name, RectF{x, cr.y, std::min(nw, room), cr.h}, nf, th.text);
      if (nw + 8 + cw <= room) ui::draw::text(cnt, RectF{x + nw + 8, cr.y + 1, cw, cr.h}, ui::Font::Caption, th.textMuted);
      ui::at(RectF{cr.x, cr.y, cr.w, cr.h});
      ui::label("##group", {.font = ui::Font::Display, .tooltip = rules::groupPath(w, g) + ": " + cnt + " с запасом или потоком"});
      // Щелчок по строке (и по названию под подсказкой) — свернуть или раскрыть.
      toggle = toggle || ui::lastItem().clicked;
      hover = hover || ui::lastItem().hovered;
      if (toggle) pf.open[g] = !open;
      if (hover) ui::setCursor(platform::Cursor::Hand);
      t.text(amount(sm.stock), ui::Ink::Dim, ui::Font::Strong);
      if (wide) {
        t.text(sm.in > kEps ? amount(sm.in) : std::string("—"), sm.in > kEps ? ui::Ink::Dim : ui::Ink::Muted, ui::Font::Small);
        t.text(std::fabs(sm.trade) > kEps ? amountSigned(sm.trade) : std::string("—"), ui::Ink::Muted, ui::Font::Small);
        t.text(sm.out > kEps ? amountSigned(-sm.out) : std::string("—"), sm.out > kEps ? ui::Ink::Danger : ui::Ink::Muted, ui::Font::Small);
      }
      t.text(amountSigned(sm.net), netInk(sm.net), ui::Font::Small);
      continue;
    }
    // Ресурс: значок цвета ресурса, название (с отступом группы), запас — правка в строке.
    const Id r = row.id;
    const CatalogItem* ci = w.resource(r);
    auto fit = fc.resources.find(r);
    const rules::ResourceFlow fl = fit == fc.resources.end() ? rules::ResourceFlow{} : fit->second;
    ui::IdScope sc{i64(r)};
    a.markUi("economy.res." + std::to_string(r), t.rowRect());
    {
      const RectF cr = t.cell();
      const float x = cr.x + indent;
      ui::draw::icon(w::resourceIcon(w, r), RectF{x, cr.cy() - 8, 16, 16}, w::resourceColor(w, r));
      const std::string name = r == kGold ? std::string("Золото") : resourceName(w, r);
      ui::draw::text(name, RectF{x + 22, cr.y, std::max(0.f, cr.right() - x - 22), cr.h}, pinned(w, r) ? ui::Font::Strong : ui::Font::Body, th.text);
      // Подсказка — название (в узкой панели обрезается) и группа ресурса (видно в поиске).
      ui::at(RectF{x, cr.y, std::max(0.f, cr.right() - x), cr.h});
      ui::label("##name", {.font = ui::Font::Display, .tooltip = ci && ci->group ? name + "\n" + rules::groupPath(w, ci->group) : name});
    }
    t.cell();
    double stock = r == kGold ? f.treasury() : f.stock(r);
    ui::NumberOpt no;
    no.min = r == kGold ? -1e12 : 0;   // казна может уйти в долг, прочие ресурсы — нет
    no.max = 1e12;
    no.step = r == kGold ? 10 : 1;
    no.digits = 3;                      // золото и ресурсы — до тысячных
    no.disabled = ro;
    no.tooltip = r == kGold ? "Казна" : "Запас ресурса";
    if (ui::numberField("stock", stock, no))
      a.act(r == kGold ? "Казна" : "Запас ресурса", [&](Tx& tx) { tx.faction(id).res[r] = r == kGold ? stock : std::max(0.0, stock); },
            {.coalesce = "faction.res:" + std::to_string(id) + ":" + std::to_string(r)});
    a.markUi("economy.stock." + std::to_string(r));
    const double in = inflow(fl), out = outflow(fl);
    std::string inTip = "Добыча и постройки: " + amount(fl.production);
    if (fl.conversionOut > kEps) inTip += "\nПреобразование: +" + amount(fl.conversionOut);
    std::string outTip;
    if (fl.consumption > kEps) outTip = "Провизия населения: " + amount(fl.consumption);
    if (fl.conversionIn > kEps) outTip += (outTip.empty() ? "" : "\n") + std::string("Преобразование: ") + amount(fl.conversionIn);
    std::string tr;
    if (fl.tradeIn > kEps) tr += "+" + amount(fl.tradeIn);
    if (fl.tradeOut > kEps) tr += (tr.empty() ? "" : " ") + amountSigned(-fl.tradeOut);
    if (wide) {
      t.cell();
      ui::label(in > kEps ? amount(in) : std::string("—"),
                {.font = ui::Font::Small, .ink = in > kEps ? ui::Ink::Normal : ui::Ink::Muted, .align = ui::Align::Right, .tooltip = in > kEps ? inTip : std::string()});
      a.markUi("economy.in." + std::to_string(r));
      t.text(tr.empty() ? std::string("—") : tr, tr.empty() ? ui::Ink::Muted : ui::Ink::Dim, ui::Font::Small);
      t.cell();
      ui::label(out > kEps ? amountSigned(-out) : std::string("—"),
                {.font = ui::Font::Small, .ink = out > kEps ? ui::Ink::Danger : ui::Ink::Muted, .align = ui::Align::Right, .tooltip = outTip});
      a.markUi("economy.use." + std::to_string(r));
    }
    t.cell();
    std::string netTip;
    if (!wide) {   // узкая панель: приход, торговля и расход — в подсказке итога
      netTip = inTip;
      if (!tr.empty()) netTip += "\nТорговля: " + tr;
      if (!outTip.empty()) netTip += "\n" + outTip;
    }
    ui::label(amountSigned(fl.net), {.font = ui::Font::Small, .ink = netInk(fl.net), .align = ui::Align::Right, .tooltip = netTip});
    a.markUi("economy.net." + std::to_string(r));
  }
}

// Провизия государства живых (ТЗ «Добавления в справочники», п.3): все ресурсы группы «Провизия» с подгруппами;
// расход населения — поровну с тех, что есть у государства; недостача держит «Голод».
void provisionSection(App& a, const World& w, const Faction& f, const rules::FactionCalc& fc) {
  if (!f.isState() || f.stateKind != StateKind::Living) return;
  const std::vector<Id> provs = rules::provisionResources(w);
  double after = 0;
  std::vector<std::pair<Id, double>> use;
  for (Id r : provs) {
    auto it = fc.resources.find(r);
    if (it == fc.resources.end()) continue;
    after += std::max(0.0, it->second.stock + it->second.net);
    if (it->second.consumption > kEps) use.push_back({r, it->second.consumption});
  }
  const bool famineNow = fc.provisionDebt > kEps, famineNext = fc.provisionDebtNext > kEps;
  ui::Section s("Провизия", "grain", {.badge = famineNow || famineNext ? "Голод" : std::string()});
  a.markUi("economy.provision");
  if (!s) return;
  const std::string stockTip = "Запас ресурсов группы «Провизия» с подгруппами: " + std::to_string(provs.size()) + " " +
                               plural(i64(provs.size()), "ресурс", "ресурса", "ресурсов");
  const std::string needTip = "0,001 на жителя: " + fmtNum(double(fc.population)) + " " + plural(fc.population, "житель", "жителя", "жителей");
  const char* debtTip = "Недостача прошлых ходов: пока она больше нуля — «Голод»";
  if (ui::avail().w >= 420) {
    ui::Row r({ui::fr(1), ui::fr(1), ui::fr(1)}, 64, 10);
    ui::stat(amount(fc.provisionStock), "Запас", {.icon = "grain", .tone = ui::Tone::Accent, .tooltip = stockTip});
    a.markUi("economy.provision.stock");
    ui::stat(amountSigned(-fc.provisionNeed), "Расход за ход", {.icon = "population", .tone = ui::Tone::Warning, .tooltip = needTip});
    a.markUi("economy.provision.need");
    ui::stat(amount(fc.provisionDebt), "Недостача", {.icon = "warning", .tone = famineNow ? ui::Tone::Danger : ui::Tone::Neutral, .tooltip = debtTip});
    a.markUi("economy.provision.debt");
  } else {
    // Узкая панель: строки «значок — подпись — число».
    const ui::Theme& th = ui::theme();
    auto line = [&](const char* icon, Color ic, std::string_view label, const std::string& value, ui::Ink ink, std::string_view tip, const char* mark) {
      ui::IdScope sc(label);
      ui::Row row({ui::px(18), ui::fr(1), ui::px(110)}, 26, 8);
      ui::iconColored(icon, ic, 16);
      ui::label(label, {.ink = ui::Ink::Dim, .tooltip = tip});
      ui::label(value, {.font = ui::Font::Strong, .ink = ink, .align = ui::Align::Right});
      a.markUi(mark);
    };
    line("grain", th.accent, "Запас", amount(fc.provisionStock), ui::Ink::Normal, stockTip, "economy.provision.stock");
    line("population", th.warning, "Расход за ход", amountSigned(-fc.provisionNeed), ui::Ink::Normal, needTip, "economy.provision.need");
    line("warning", famineNow ? th.danger : th.textMuted, "Недостача", amount(fc.provisionDebt), famineNow ? ui::Ink::Danger : ui::Ink::Muted, debtTip,
         "economy.provision.debt");
  }
  {
    // После хода: запас и недостача (расход берётся из запаса и добычи этого хода).
    ui::HStack hs(26, ui::Align::Left, 6);
    ui::icon("next-turn", ui::Ink::Muted, 16, "После хода");
    ui::tag("Запас " + amount(after), ui::Tone::Neutral, "grain");
    a.markUi("economy.provision.after");
    if (famineNext) ui::tag("Недостача " + amount(fc.provisionDebtNext), ui::Tone::Danger, "warning");
    else ui::tag("Без недостачи", ui::Tone::Success, "check");
    a.markUi("economy.provision.debtNext");
  }
  // Расход по ресурсам: поровну со всех ресурсов провизии, что есть у государства.
  if (!use.empty()) {
    const std::string tip = use.size() > 1 ? "Расход провизии поровну: " + std::to_string(use.size()) + " " +
                                                 plural(i64(use.size()), "ресурс", "ресурса", "ресурсов")
                                           : std::string("Весь расход провизии — с этого ресурса");
    edkit::chipsBegin();
    for (auto [r, v] : use) {
      ui::IdScope sc{i64(r)};
      ui::ChipOpt co;
      co.icon = w::resourceIcon(w, r);
      co.tone = ui::Tone::Danger;
      co.tooltip = tip;
      edkit::chip(resourceName(w, r) + " " + amountSigned(-v), co);
      a.markUi("economy.provision.use." + std::to_string(r));
    }
    edkit::chipsEnd();
  }
}

// Эссенции элементов (ТЗ «Добавления в справочники», п.2; «Ввод новых механик», п.3.2): запас — правка числа
// (rules::setEssence), генерация построек, содержание элементалей, итог; долг (отрицательный запас) — красным.
void essenceSection(App& a, const World& w, const Faction& f, const rules::FactionCalc& fc, Id id, bool ro) {
  EcoPrefs& pf = prefs(a);
  const ui::Theme& th = ui::theme();
  struct Row {
    Id e;
    rules::EssenceFlow fl;
  };
  std::vector<Row> rows;
  int filled = 0, debts = 0;
  for (const CatalogItem& e : w.catalogs->essences) {
    auto it = fc.essences.find(e.id);
    const rules::EssenceFlow fl = it == fc.essences.end() ? rules::EssenceFlow{f.essence(e.id)} : it->second;
    const bool empty = std::fabs(fl.stock) < kEps && fl.generation < kEps && fl.upkeep < kEps;
    if (!empty) filled++;
    if (fl.stock < -kEps || fl.stock + fl.net < -kEps) debts++;
    if (pf.hideEmptyEss && empty) continue;
    rows.push_back({e.id, fl});
  }
  ui::Section s("Эссенции элементов", "essence",
                {.defaultOpen = filled > 0, .badge = std::to_string(filled), .actionIcon = pf.hideEmptyEss ? "eye-off" : "eye",
                 .actionTooltip = pf.hideEmptyEss ? "Показать все эссенции" : "Скрыть пустые эссенции"});
  {
    const RectF hr = ui::lastItem().rect;
    a.markUi("economy.ess", hr);
    a.markUi("economy.essHide", RectF{hr.right() - (th.padCard - 4) - 26, hr.cy() - 13, 26, 26});
  }
  if (s.action()) pf.hideEmptyEss = !pf.hideEmptyEss;
  if (!s) return;
  if (w.catalogs->essences.empty()) {
    ui::label("Справочник эссенций пуст", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    return;
  }
  if (debts) ui::tag("Долг: " + std::to_string(debts), ui::Tone::Danger, "warning");
  if (rows.empty()) {
    ui::label("Эссенций нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    return;
  }
  const bool wide = ui::avail().w >= 520;
  ui::Column wideCols[] = {{"Эссенция", nullptr, ui::fr(1, 140)},
                           {"Запас", nullptr, ui::px(110), ui::Align::Left, false, "Запас эссенции; содержание элементалей может увести его в долг"},
                           {{}, "building", ui::px(84), ui::Align::Right, false, "Генерация построек за ход"},
                           {{}, "u-elementals", ui::px(84), ui::Align::Right, false, "Содержание элементалей за ход"},
                           {{}, "trend-up", ui::px(92), ui::Align::Right, false, "Итого за ход"}};
  ui::Column narrowCols[] = {{"Эссенция", nullptr, ui::fr(1, 90)},
                             {"Запас", nullptr, ui::px(88), ui::Align::Left, false, "Запас эссенции; содержание элементалей может увести его в долг"},
                             {{}, "trend-up", ui::px(76), ui::Align::Right, false, "Итого за ход"}};
  const std::span<const ui::Column> cols = wide ? std::span<const ui::Column>(wideCols) : std::span<const ui::Column>(narrowCols);
  ui::Table t("essences", cols, int(rows.size()), {.rowHeight = 36, .selectable = false});
  for (int i : t) {
    const Row& row = rows[size_t(i)];
    const CatalogItem* e = w.essence(row.e);
    const bool debt = row.fl.stock < -kEps, debtNext = row.fl.stock + row.fl.net < -kEps;
    ui::IdScope sc{i64(row.e)};
    {
      const RectF cr = t.cell();
      ui::draw::icon("essence", RectF{cr.x, cr.cy() - 8, 16, 16}, w::essenceColor(w, row.e));
      const std::string name = e && !e->name.empty() ? e->name : std::string("Без названия");
      ui::draw::text(name, RectF{cr.x + 22, cr.y, std::max(0.f, cr.w - 22 - (debt || debtNext ? 20 : 0)), cr.h}, ui::Font::Body,
                     debt ? th.danger : th.text);
      if (debt || debtNext) {
        const RectF ir{cr.right() - 16, cr.cy() - 8, 16, 16};
        ui::draw::icon("warning", ir, debt ? th.danger : th.warning);
        ui::at(ir);
        ui::label("##debt", {.font = ui::Font::Display,
                             .tooltip = debt ? "Долг эссенции: содержание элементалей больше запаса" : "После хода запас уйдёт в долг"});
      }
    }
    t.cell();
    double v = row.fl.stock;
    if (ui::numberField("ess", v, {.min = -1e15, .max = 1e15, .step = 1, .digits = 3, .disabled = ro, .tooltip = "Запас эссенции"})) {
      const Id ess = row.e;
      a.act("Запас эссенции", [&](Tx& tx) { rules::setEssence(tx, id, ess, v); },
            {.coalesce = "faction.ess:" + std::to_string(id) + ":" + std::to_string(ess)});
    }
    if (debt) ui::draw::rectStroke(ui::lastItem().rect, th.danger, th.radiusField, 1.5f);   // долг — красным
    a.markUi("economy.ess." + std::to_string(row.e));
    if (wide) {
      t.text(row.fl.generation > kEps ? "+" + amount(row.fl.generation) : std::string("—"), row.fl.generation > kEps ? ui::Ink::Success : ui::Ink::Muted,
             ui::Font::Small);
      t.text(row.fl.upkeep > kEps ? amountSigned(-row.fl.upkeep) : std::string("—"), row.fl.upkeep > kEps ? ui::Ink::Danger : ui::Ink::Muted,
             ui::Font::Small);
    }
    t.cell();
    std::string tip;
    if (!wide) tip = "Генерация: +" + amount(row.fl.generation) + "\nСодержание элементалей: " + amountSigned(-row.fl.upkeep);
    ui::label(amountSigned(row.fl.net), {.font = ui::Font::Small, .ink = netInk(row.fl.net), .align = ui::Align::Right, .tooltip = tip});
    a.markUi("economy.essNet." + std::to_string(row.e));
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
  // Голод (ТЗ «Добавления в справочники», п.3): недостача провизии — сейчас или после хода.
  if (fc->famine || fc->provisionDebtNext > kEps) {
    ui::HStack hs(26, ui::Align::Left, 6);
    if (fc->famine) {
      const Modifier* m = rules::builtinMod(w, schema::mod::Famine);
      ui::tag(m && !m->name.empty() ? m->name : std::string("Голод"), ui::Tone::Danger, "warning");
      ui::tooltip("Недостача провизии " + amount(fc->provisionDebt) + ": действует модификатор «Голод»");
      a.markUi("economy.famine");
    } else {
      ui::tag("Голод после хода", ui::Tone::Warning, "warning");
      ui::tooltip("Провизии не хватит на расход населения: недостача " + amount(fc->provisionDebtNext));
      a.markUi("economy.famineNext");
    }
  }
  ui::spacer(2);
  moneyCards(*f, *fc);
  taxSection(a, *f, id, ro);
  provisionSection(a, w, *f, *fc);
  resourcesSection(a, w, *f, *fc, id, ro);
  essenceSection(a, w, *f, *fc, id, ro);
  slavesSection(a, w, *f, *fc, id, ro);
  tributeSection(a, w, id, ro);
}

// Точка на значке вкладки: казна убывает, голод (сейчас или после хода), эссенция в долгу (сейчас или после хода).
int economyBadge(App& a, Id id) {
  auto calc = rules::calc(a.world());
  const rules::FactionCalc* fc = calc->faction(id);
  if (!fc) return 0;
  if (fc->net < -0.5 || fc->famine || fc->provisionDebtNext > kEps) return -1;
  for (auto& [e, fl] : fc->essences)
    if (fl.stock + fl.net < -kEps) return -1;
  return 0;
}

TabReg tab({kTabEconomy, "treasury", "Экономика", 30, SelType::Faction, nullptr, drawEconomy, economyBadge});

}  // namespace
}  // namespace rg::app
