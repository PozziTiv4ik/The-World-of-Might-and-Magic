// Regnum — обмен отрядами (ТЗ «Доработки №3», п.6): два объекта одного вида и одного государства в двух столбцах —
// численность каждой строки (поля и ползунок) и герои (выбор столбца) перекладываются между ними; итоги строк
// сохраняются, войско на борту — не больше вместимости флота (rules::exchangeUnits). Окно открывают встреча
// («Обмен отрядами») и подход войска к флоту, на борту которого уже есть войско.
#include "app/app_internal.h"
#include "app/panels/military.h"

namespace rg::app::mil {

namespace {

using Key = std::pair<Id, Id>;   // (фракция, строка)

constexpr float kNumW = 100, kGap = 10;

i64 countIn(const Army& x, Id f, Id row) {
  i64 n = 0;
  for (const ArmyGroup& g : x.groups)
    if (g.faction == f)
      for (const ArmyUnit& u : g.units)
        if (u.row == row) n += u.count;
  return n;
}

// Строки обоих объектов по фракциям: сначала строки первого, затем недостающие строки второго.
std::vector<std::pair<Id, std::vector<Id>>> layout(const Army& A, const Army& B) {
  std::vector<std::pair<Id, std::vector<Id>>> out;
  for (const Army* x : {&A, &B})
    for (const ArmyGroup& g : x->groups)
      for (const ArmyUnit& u : g.units) {
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == g.faction; });
        if (it == out.end()) {
          out.push_back({g.faction, {}});
          it = out.end() - 1;
        }
        if (std::find(it->second.begin(), it->second.end(), u.row) == it->second.end()) it->second.push_back(u.row);
      }
  return out;
}

// Герои обоих объектов (по порядку групп) и фракции их групп.
std::vector<std::pair<Id, Id>> heroesOf(const Army& A, const Army& B) {
  std::vector<std::pair<Id, Id>> out;
  for (const Army* o : {&A, &B})
    for (const ArmyGroup& g : o->groups)
      for (Id h : g.heroes)
        if (std::none_of(out.begin(), out.end(), [&](const auto& p) { return p.first == h; })) out.push_back({h, g.faction});
  return out;
}

struct ExchangeDialog final : Dialog {
  Id a = 0, b = 0;
  std::map<Key, i64> first;           // итог в первом объекте
  std::vector<Id> heroesFirst;        // герои первого объекта после обмена
  bool ready = false;
  bool finished = false;
  std::function<void(App&, bool)> done;

  const char* id() const override { return "army.exchange"; }
  Style style(App& x) override {
    const Army* A = x.world().army(a);
    return {A && A->isFleet() ? "Обмен кораблями" : "Обмен отрядами", "exchange", ui::Tone::Accent, 700};
  }

  void finish(App& x, bool changed) {
    if (finished) return;
    finished = true;
    if (done) {
      auto fn = done;
      detail::later(x, [fn, changed](App& y) { fn(y, changed); });
    }
  }
  void dismissed(App& x) override { finish(x, false); }

  // Исходный состав: всё — как сейчас.
  void reset(const World& w) {
    first.clear();
    heroesFirst.clear();
    if (const Army* A = w.army(a))
      for (const ArmyGroup& g : A->groups) {
        for (const ArmyUnit& u : g.units) first[{g.faction, u.row}] += u.count;
        for (Id h : g.heroes) heroesFirst.push_back(h);
      }
    ready = true;
  }

  // Что-то поменялось относительно текущего состава.
  bool changed(const Army& A, const Army& B) const {
    for (auto& [f, list] : layout(A, B))
      for (Id row : list) {
        auto it = first.find({f, row});
        if ((it == first.end() ? 0 : it->second) != countIn(A, f, row)) return true;
      }
    for (auto& [h, f] : heroesOf(A, B)) {
      bool inA = false;
      for (const ArmyGroup& g : A.groups) inA = inA || std::find(g.heroes.begin(), g.heroes.end(), h) != g.heroes.end();
      if (inA != (std::find(heroesFirst.begin(), heroesFirst.end(), h) != heroesFirst.end())) return true;
    }
    return false;
  }

  // Шапка столбца: значок объекта, где он (провинция или «на борту»), название, численность после обмена.
  void objectHead(const World& w, const Army& x, i64 after) {
    ui::IdScope s(i64(x.id) + 0x75000000LL);
    ui::Card c({.pad = 10});
    ui::Row r({ui::px(40), ui::fr(1)}, ui::kAuto, 10);
    figure(w, x, 40);
    ui::Group g(0, 1);
    std::string where;
    if (const Id fl = rules::carrierOf(w, x.id)) where = "На борту «" + objectName(*w.army(fl)) + "»";
    else if (const Id p = provinceUnder(w, x.pos)) where = w.provinceName(p);
    else where = x.isFleet() ? "Открытое море" : "Вне провинций";
    ui::caption(where);
    ui::label(objectName(x), {.font = ui::Font::Strong});
    ui::label(fmtCount(after) + " " + (x.isFleet() ? plural(after, "корабль", "корабля", "кораблей") : plural(after, "воин", "воина", "воинов")),
              {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }

  // Вместимость флота стороны после обмена: войско на борту — его численность против вместимости; флот с войском
  // на борту — его войско против вместимости нового состава кораблей. Ничего — пустая ячейка.
  void capacityCell(const World& w, const Army& x, i64 after) {
    Id fleet = 0;
    i64 used = 0, cap = 0;
    if (const Id fl = rules::carrierOf(w, x.id)) {
      fleet = fl;
      used = after;
      cap = rules::fleetCapacity(w, fl);
    } else if (const Id c = x.isFleet() ? rules::cargoOf(w, x.id) : 0) {
      fleet = x.id;
      used = rules::armySize(w, c);
      const Army* A = w.army(a);
      const Army* B = w.army(b);
      for (auto& [k, n] : first) {
        const Faction* f = w.faction(k.first);
        const FleetRow* r = f ? f->fleetRow(k.second) : nullptr;
        if (!r || !A || !B) continue;
        const i64 total = countIn(*A, k.first, k.second) + countIn(*B, k.first, k.second);
        cap += (x.id == a ? n : total - n) * rules::shipCapacity(w, r->type);
      }
    }
    if (!fleet) {
      (void)ui::next(20);
      return;
    }
    ui::IdScope s(i64(x.id) + 0x76000000LL);
    const bool over = used > cap;
    const std::string tip = "На борту / вместимость флота «" + objectName(*w.army(fleet)) + "»";
    ui::HStack hs(20, ui::Align::Left, 6);
    ui::icon("embark", over ? ui::Ink::Danger : ui::Ink::Dim, 14, tip);
    ui::label(fmtCount(used) + " / " + fmtCount(cap), {.font = ui::Font::Small, .ink = over ? ui::Ink::Danger : ui::Ink::Dim, .tooltip = tip});
  }

  bool draw(App& x) override {
    const World& w = frameWorld(x);
    const Army* A = w.army(a);
    const Army* B = w.army(b);
    if (!A || !B) {
      finish(x, false);
      return false;
    }
    if (!ready) reset(w);
    const bool fleet = A->isFleet();
    const bool allied = A->allied() || B->allied();
    const ui::Theme& th = ui::theme();
    const auto rows = layout(*A, *B);
    const auto heroes = heroesOf(*A, *B);
    // Итоги столбцов.
    i64 sumA = 0, sumB = 0;
    for (auto& [f, list] : rows)
      for (Id row : list) {
        const i64 total = countIn(*A, f, row) + countIn(*B, f, row);
        i64& v = first[{f, row}];
        v = clamp<i64>(v, 0, total);
        sumA += v;
        sumB += total - v;
      }
    {
      ui::Row r({ui::fr(1), ui::px(36), ui::fr(1)}, ui::kAuto, kGap);
      objectHead(w, *A, sumA);
      {
        RectF ic = ui::next(36, 76);
        RectF bb{ic.cx() - 16, ic.cy() - 16, 32, 32};
        ui::draw::rect(bb, th.accent.alpha(0.16f), 16);
        ui::draw::icon("exchange", bb.inset(8), th.accent);
      }
      objectHead(w, *B, sumB);
    }
    const bool capA = rules::carrierOf(w, a) || (fleet && rules::cargoOf(w, a));
    const bool capB = rules::carrierOf(w, b) || (fleet && rules::cargoOf(w, b));
    if (capA || capB) {
      ui::Row r({ui::fr(1), ui::px(36), ui::fr(1)}, 20, kGap);
      capacityCell(w, *A, sumA);
      (void)ui::next(20);
      capacityCell(w, *B, sumB);
    }
    // Отряды: поле первого объекта, ползунок (вправо — во второй), поле второго; ниже — герои.
    float bodyH = 0;
    for (auto& [f, list] : rows) bodyH += float(list.size()) * 40 + (allied ? 28 : 0);
    bodyH += float(heroes.size()) * 40 + (heroes.empty() ? 0 : 26);
    const float maxH = std::max(160.f, ui::viewport().h - 440);
    {
      ui::Scroll sc("rows", std::min(bodyH + 4, maxH));
      for (auto& [f, list] : rows) {
        ui::IdScope fs(i64(f) + 0x77000000LL);
        if (allied) {
          ui::HStack hs(20, ui::Align::Left, 6);
          factionFlag(w, f, 24, 16);
          ui::label(w.factionName(f), {.font = ui::Font::Strong});
        }
        for (Id row : list) {
          auto u = unitRow(w, f, row, fleet);
          if (!u) continue;
          ui::IdScope rs{i64(row)};
          const i64 total = countIn(*A, f, row) + countIn(*B, f, row);
          i64& v = first[{f, row}];
          ui::Row r({ui::fr(1, 140), ui::px(kNumW), ui::fr(0.8f, 80), ui::px(kNumW)}, 32, kGap);
          unitCell(ui::next(32), *u, false, "всего " + fmtCount(total));
          i64 na = v;
          if (ui::numberField("a", na, {.min = 0, .max = double(total), .tooltip = "В «" + objectName(*A) + "»"})) v = clamp<i64>(na, 0, total);
          x.markUi("exchange.a." + std::to_string(f) + "." + std::to_string(row));
          i64 nb = total - v;
          if (ui::slider("s", nb, 0, double(total), {.step = 1, .bubble = false, .showValue = false})) v = total - clamp<i64>(nb, 0, total);
          x.markUi("exchange.s." + std::to_string(f) + "." + std::to_string(row));
          nb = total - v;
          if (ui::numberField("b", nb, {.min = 0, .max = double(total), .tooltip = "В «" + objectName(*B) + "»"})) v = total - clamp<i64>(nb, 0, total);
          x.markUi("exchange.b." + std::to_string(f) + "." + std::to_string(row));
        }
      }
      if (!heroes.empty()) {
        ui::caption("Герои");
        for (auto& [h, f] : heroes) {
          const Character* ch = w.character(h);
          if (!ch) continue;
          ui::IdScope hs(i64(h) + 0x78000000LL);
          const bool cmd = A->commander == h || B->commander == h;
          ui::Row r({ui::fr(1, 140), ui::px(kNumW), ui::fr(0.8f, 80), ui::px(kNumW)}, 32, kGap);
          {
            ui::HStack hh(32, ui::Align::Left, 8);
            w::heroAvatar(*ch, 26, cmd);
            ui::label(ch->name.empty() ? std::string("Без имени") : ch->name,
                      {.font = cmd ? ui::Font::Strong : ui::Font::Body,
                       .tooltip = cmd ? std::string_view(fleet ? "Главный флотоводец" : "Главный полководец") : std::string_view()});
          }
          int side = std::find(heroesFirst.begin(), heroesFirst.end(), h) != heroesFirst.end() ? 0 : 1;
          const int before = side;
          ui::radio("##a", side, 0);
          ui::tooltip("В «" + objectName(*A) + "»");
          x.markUi("exchange.hero.a." + std::to_string(h));
          (void)ui::next(32);
          ui::radio("##b", side, 1);
          ui::tooltip("В «" + objectName(*B) + "»");
          x.markUi("exchange.hero.b." + std::to_string(h));
          if (side != before) {
            if (side == 0) heroesFirst.push_back(h);
            else heroesFirst.erase(std::remove(heroesFirst.begin(), heroesFirst.end(), h), heroesFirst.end());
          }
        }
      }
    }
    // Причина, по которой обмен невозможен (вместимость, чужая фракция), и действия.
    const rules::ExchangeSpec spec{first, heroesFirst};
    const std::vector<std::string> problems = rules::exchangeProblems(w, a, b, spec);
    const bool diff = changed(*A, *B);
    {
      ui::HStack hs(26, ui::Align::Left, 8);
      if (ui::button("Сбросить", {.variant = ui::Variant::Ghost, .icon = "undo", .size = ui::Size::Small, .disabled = !diff})) reset(w);
      x.markUi("exchange.reset");
    }
    if (!problems.empty()) ui::label(problems.front(), {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
    ui::ModalFooter foot;
    if (ui::button("Отмена")) {
      finish(x, false);
      return false;
    }
    x.markUi("exchange.cancel");
    ui::Disabled dis(x.readOnly());
    const bool can = problems.empty() && diff;
    // Не кнопка по Enter: Enter фиксирует число в поле строки (правят несколько строк подряд).
    if (ui::button("Обменять", {.variant = ui::Variant::Primary, .icon = "exchange", .disabled = !can}) && can) {
      const Id fa = a, fb = b;
      if (x.act(fleet ? "Обмен кораблями" : "Обмен отрядами", [&](Tx& tx) { rules::exchangeUnits(tx, fa, fb, spec); })) {
        x.select(SelType::Army, x.world().army(fa) ? fa : fb);
        finish(x, true);
        return false;
      }
    }
    x.markUi("exchange.ok");
    return true;
  }
};

}  // namespace

void openExchange(App& a, Id first, Id second, std::function<void(App&, bool)> done) {
  std::string why;
  if (a.readOnly()) {
    a.act("Обмен отрядами", [](Tx&) {});   // сообщение о просмотре прошлого хода
    if (done) done(a, false);
    return;
  }
  if (!rules::canExchange(a.world(), first, second, &why)) {
    a.toast(why, ToastKind::Warning, "warning");
    if (done) done(a, false);
    return;
  }
  auto d = std::make_unique<ExchangeDialog>();
  d->a = first;
  d->b = second;
  d->done = std::move(done);
  a.openDialog(std::move(d));
}

}  // namespace rg::app::mil
