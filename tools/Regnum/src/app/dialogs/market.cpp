// Regnum — окно рынка «Продать реликвии» (ТЗ «Доработки №2–4»; DialogReg «market», arg — государство): стопки реликвий
// из достроенных хранилищ реликвий государства (rules::sellableRelics) — реликвия цветом редкости, где лежит, сколько,
// цена за штуку (археологические находки дороже — rules::relicPrice) и итог; выбор количества по стопкам, «Продать» —
// одной транзакцией (rules::sellRelics), выручка — в казну. Игровое действие государства (App::canPlay, App::play).
#include <algorithm>
#include <tuple>

#include "app/editors/buildings.h"
#include "app/widgets.h"

namespace rg::app {

namespace {

struct Market : Dialog {
  Id state = 0;
  // Сколько продать: (провинция, постройка, реликвия) → штук.
  std::map<std::tuple<Id, Id, Id>, i64> qty;
  const char* id() const override { return "market"; }
  Style style(App& a) override {
    const Faction* f = a.world().faction(state);
    return {"Рынок · " + (f ? w::orName(f->name) : std::string("государство")), "scales", ui::Tone::Accent, 720};
  }
  bool draw(App& a) override;
};

// Стопка к показу: место, количество, цена.
struct Lot {
  rules::RelicAt at;
  const Relic* r = nullptr;
  double price = 0;
  bool find = false;   // археологическая находка
  i64 sell = 0;
};

bool Market::draw(App& a) {
  const World& w = a.world();
  const Faction* f = w.faction(state);
  if (!f || !f->isState() || !a.knows(state)) return false;   // туман войны: хранилища чужого государства скрыты
  const bool can = a.canPlay(state);
  const bool market = rules::hasMarket(w, state);
  std::vector<Lot> lots;
  for (const rules::RelicAt& x : rules::sellableRelics(w, state)) {
    const Relic* r = w.relic(x.relic);
    if (!r) continue;
    Lot l{x, r, rules::relicPrice(w, x.relic), rules::isArchFind(w, x.relic), 0};
    auto it = qty.find({x.place.id, x.place.building, x.relic});
    if (it != qty.end()) l.sell = std::clamp<i64>(it->second, 0, x.count);
    lots.push_back(l);
  }
  // Дороже и реже — выше; затем по названию и провинции.
  std::stable_sort(lots.begin(), lots.end(), [&](const Lot& x, const Lot& y) {
    if (x.r->rarity != y.r->rarity) return rules::rarityAbove(x.r->rarity, y.r->rarity);
    if (x.price != y.price) return x.price > y.price;
    if (int c = compareRu(x.r->name, y.r->name); c != 0) return c < 0;
    return x.at.place.id < y.at.place.id;
  });
  i64 pieces = 0, selected = 0;
  double worth = 0, total = 0;
  for (const Lot& l : lots) {
    pieces += l.at.count;
    worth += l.price * double(l.at.count);
    selected += l.sell;
    total += l.price * double(l.sell);
  }
  // Показатели: казна, всего в хранилищах, к продаже.
  {
    ui::Row r({ui::fr(1), ui::fr(1), ui::fr(1)}, 64, 10);
    ui::stat(fmtGold(f->treasury()), "Казна", {.icon = "treasury", .tone = ui::Tone::Accent, .tooltip = "Казна государства сейчас"});
    a.markUi("market.treasury");
    ui::stat(fmtNum(double(pieces)), "В хранилищах", {.icon = "relic", .tone = ui::Tone::Info, .tooltip = "Всего штук на " + fmtGold(worth)});
    ui::stat(total > 0 ? "+" + fmtGold(total) : std::string("0"), "К продаже",
             {.icon = "coins", .tone = total > 0 ? ui::Tone::Success : ui::Tone::Neutral, .tooltip = fmtNum(double(selected)) + " шт. — выручка в казну"});
    a.markUi("market.total");
  }
  if (!market) {
    ui::label("У государства нет достроенного рынка", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
    a.markUi("market.noMarket");
  }
  if (lots.empty()) {
    ui::emptyState("relic", "В хранилищах реликвий государства нет реликвий.");
    a.markUi("market.empty");
  } else {
    // Высота — по строкам (шапка, строки, итог), не больше видимой части окна.
    const float maxH = std::min(420.f, std::max(200.f, ui::viewport().h - 360));
    const float h = std::min(maxH, 34 + 46 * float(lots.size()) + 40);
    ui::Column cols[] = {{"Реликвия", nullptr, ui::fr(1.6f, 160)},
                         {"Где", nullptr, ui::fr(1.1f, 110)},
                         {"Есть", nullptr, ui::px(52), ui::Align::Right, false, "Штук в хранилище"},
                         {"Цена", nullptr, ui::px(76), ui::Align::Right, false, "Золота за штуку; археологические находки дороже"},
                         {"Продать", nullptr, ui::px(112), ui::Align::Left, false, "Сколько штук продать"},
                         {"Итог", nullptr, ui::px(84), ui::Align::Right, false, "Выручка за выбранные штуки"}};
    ui::Table t("lots", cols, int(lots.size()), {.rowHeight = 46, .height = h, .striped = true, .selectable = false});
    for (int i : t) {
      const Lot& l = lots[size_t(i)];
      const auto key = std::make_tuple(l.at.place.id, l.at.place.building, l.at.relic);
      const std::string mark = "market.lot." + std::to_string(l.at.place.id) + "." + std::to_string(l.at.relic);
      ui::IdScope s{i64(l.at.place.id) * 1000003 + i64(l.at.relic)};
      t.cell();
      RectF rr;
      w::relicRow(*l.r, l.find ? std::string("Находка · ") + utf8::lower(schema::rarity(l.r->rarity).name) : std::string(schema::rarity(l.r->rarity).name), 0, &rr);
      a.markUi(mark, rr);
      RectF where = t.cell();
      {
        const Building* b = w.building(l.at.place.building);
        const std::string prov = w.provinceName(l.at.place.id), bn = b ? w::orName(b->name) : std::string("Хранилище");
        const float lh = ui::lineHeight(ui::Font::Small);
        const float y0 = std::round(where.cy() - lh);
        ui::draw::text(prov, RectF{where.x, y0, where.w, lh}, ui::Font::Small, ui::theme().text);
        ui::draw::text(bn, RectF{where.x, y0 + lh, where.w, lh}, ui::Font::Small, ui::theme().textMuted);
        ui::hoverTip("##where", where, prov + " · " + bn);
      }
      t.text(fmtNum(double(l.at.count)), ui::Ink::Dim);
      t.cell();
      ui::label(fmtGold(l.price), {.font = ui::Font::Small, .ink = l.find ? ui::Ink::Accent : ui::Ink::Normal, .align = ui::Align::Right,
                                   .tooltip = l.find ? "Археологическая находка: дороже обычной реликвии той же редкости" : "Золота за штуку"});
      t.cell();
      {
        ui::Row row({ui::fr(1), ui::px(26)}, 30, 4);
        i64 n = l.sell;
        if (ui::numberField("n", n, {.min = 0, .max = double(l.at.count), .step = 1, .disabled = !can,
                                     .tooltip = "Не больше " + fmtNum(double(l.at.count)) + " шт."}))
          qty[key] = std::clamp<i64>(n, 0, l.at.count);
        a.markUi(mark + ".qty");
        const bool all = l.sell == l.at.count;
        if (ui::iconButton(all ? "close" : "arrow-up", all ? "Не продавать" : "Все штуки", {.size = ui::Size::Small, .disabled = !can}))
          qty[key] = all ? 0 : l.at.count;
        a.markUi(mark + ".all");
      }
      t.text(l.sell > 0 ? "+" + fmtGold(l.price * double(l.sell)) : std::string("—"), l.sell > 0 ? ui::Ink::Success : ui::Ink::Muted);
    }
    if (t.footer()) {
      t.text("Итого", ui::Ink::Normal, ui::Font::Strong);
      t.cell();
      t.text(fmtNum(double(pieces)), ui::Ink::Dim, ui::Font::Strong);
      t.cell();
      t.text(selected > 0 ? fmtNum(double(selected)) + " шт." : std::string("—"), ui::Ink::Normal, ui::Font::Strong);
      t.text(total > 0 ? "+" + fmtGold(total) : std::string("—"), total > 0 ? ui::Ink::Success : ui::Ink::Muted, ui::Font::Strong);
    }
  }
  ui::ModalFooter footer;
  if (ui::button("Закрыть")) return false;
  a.markUi("dialog.cancel");
  std::string why;
  if (a.readOnly()) why = "Открыт прошлый ход";
  else if (!can) why = "Продавать может только само государство";
  else if (!market) why = "У государства нет достроенного рынка";
  else if (selected <= 0) why = "Выберите, сколько штук продать";
  const std::string label = total > 0 ? "Продать · +" + fmtGold(total) : std::string("Продать");
  if (ui::button(label, {.variant = ui::Variant::Primary, .icon = "coins", .disabled = !why.empty(), .isDefault = true,
                         .tooltip = why.empty() ? std::string_view("Выручка — в казну государства") : std::string_view(why)})) {
    double gold = 0;
    i64 sold = 0;
    const Id sid = state;
    if (a.play("Продать реликвии", [&](Tx& tx) {
          for (const Lot& l : lots)
            if (l.sell > 0) {
              gold += rules::sellRelics(tx, sid, l.at.place, l.at.relic, l.sell);
              sold += l.sell;
            }
        })) {
      qty.clear();
      a.toast("Продано " + fmtNum(double(sold)) + " шт.: +" + fmtGold(gold) + " золота в казну", ToastKind::Success, "coins");
    }
  }
  a.markUi("market.sell");
  return true;
}

DialogReg regMarket({"market", [](App&, Id state) -> std::unique_ptr<Dialog> {
                       auto d = std::make_unique<Market>();
                       d->state = state;
                       return d;
                     }});

}  // namespace

std::unique_ptr<Dialog> marketDialog(Id state) {
  auto d = std::make_unique<Market>();
  d->state = state;
  return d;
}

}  // namespace rg::app
