// Regnum — окно перемирия воюющих государств (ТЗ «Механика войн», п.2): две колонки — что отдаёт каждая сторона:
// провинции (свои), репарации золотом за ход и срок (как в окне «Дань или репарации»), разовые выплаты золота и
// ресурсов, рабы (не больше 5 % населения стороны), пленные герои, которых сторона держит, и «Стать вассалом».
// Итоговое состояние (по умолчанию «Статус-кво»), причины, мешающие заключению (rules::truceProblems), —
// rules::concludeTruce одним действием (отменяется Ctrl+Z).
#include "app/flows.h"
#include "app/panels/faction_common.h"
#include "gfx/icons.h"

namespace rg::app {

namespace edkit {   // поток фишек с переносом строк (editors/modifiers.cpp)
void chipsBegin();
ui::ChipAction chip(std::string_view label, const ui::ChipOpt& o);
void chipsEnd();
}  // namespace edkit

namespace {

using namespace fac;

struct TruceDlg final : Dialog {
  rules::Truce t;
  float contentH = 0;   // высота колонок прошлого кадра (окно — по содержимому, не выше экрана)

  const char* id() const override { return "truce"; }
  Style style(App&) override { return {"Перемирие", "handshake", ui::Tone::Accent, 780}; }

  // Колонка стороны giver (key — «a» или «b»): что она отдаёт taker.
  void side(App& a, const World& w, const char* key, Id giver, Id taker, rules::TruceTerms& terms) {
    const std::string k(key);
    ui::IdScope scope(key);
    const Faction* g = w.faction(giver);
    if (!g) return;
    ui::Card card({.pad = 12, .icon = "export", .title = "Отдаёт «" + displayName(*g) + "»"});
    // Провинции.
    ui::caption("Провинции");
    if (!terms.provinces.empty()) {
      edkit::chipsBegin();
      for (size_t i = 0; i < terms.provinces.size(); i++) {
        ui::IdScope ps{i64(terms.provinces[i])};
        if (edkit::chip(w.provinceName(terms.provinces[i]), {.icon = "province", .color = g->color, .removable = true,
                                                             .tooltip = "Провинция перейдёт к другой стороне"}) == ui::ChipAction::Remove) {
          terms.provinces.erase(terms.provinces.begin() + long(i));
          break;
        }
      }
      edkit::chipsEnd();
    }
    {
      std::vector<const Province*> free;
      w.provinces.each([&](const Province& p) {
        if (p.owner == giver && !p.sea && std::find(terms.provinces.begin(), terms.provinces.end(), p.id) == terms.provinces.end()) free.push_back(&p);
      });
      std::sort(free.begin(), free.end(), [](const Province* x, const Province* y) { return compareRu(x->name, y->name) < 0; });
      int idx = -1;
      if (ui::combo("province", idx, int(free.size()), [&](int i) { return ui::Option{free[size_t(i)]->name, "province", g->color}; },
                    {.placeholder = free.empty() ? "Нет провинций" : "Добавить провинцию", .search = 1, .icon = "plus", .disabled = free.empty()}) &&
          idx >= 0 && idx < int(free.size()))
        terms.provinces.push_back(free[size_t(idx)]->id);
      a.markUi("truce." + k + ".province");
    }
    // Репарации: золото за ход и срок (как «Дань или репарации»).
    ui::caption("Репарации");
    {
      ui::Row r({ui::fr(1), ui::fr(1)}, 30, 8);
      if (ui::numberField("rep", terms.reparations, {.min = 0, .max = 1e9, .step = 10, .digits = 3, .icon = "reparations", .tooltip = "Золото за ход"}) &&
          terms.reparations > 0 && terms.reparationsTurns < 1)
        terms.reparationsTurns = 5;
      a.markUi("truce." + k + ".rep");
      ui::numberField("repTurns", terms.reparationsTurns, {.min = 0, .max = 999, .unit = "ход|хода|ходов", .icon = "hourglass", .tooltip = "Срок репараций"});
      a.markUi("truce." + k + ".repTurns");
    }
    // Разовые выплаты: золото из казны и ресурсы.
    ui::caption("Выплаты");
    {
      ui::Row r({ui::fr(1), ui::px(84)}, 30, 8);
      double gold = terms.resources.count(kGold) ? terms.resources[kGold] : 0;
      if (ui::numberField("gold", gold, {.min = 0, .max = 1e12, .step = 10, .digits = 3, .unit = "тыс.", .icon = "coins", .tooltip = "Золото из казны"})) {
        if (gold > 0) terms.resources[kGold] = gold;
        else terms.resources.erase(kGold);
      }
      a.markUi("truce." + k + ".gold");
      ui::label("есть " + fmtGold(g->treasury()), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Right});
    }
    for (auto it = terms.resources.begin(); it != terms.resources.end();) {
      const Id res = it->first;
      if (res == kGold) {
        ++it;
        continue;
      }
      ui::IdScope rs{i64(res)};
      ui::Row r({ui::px(18), ui::fr(1), ui::px(110), ui::px(24)}, 30, 6);
      ui::iconColored(w::resourceIcon(w, res), w::resourceColor(w, res), 16);
      ui::label(resourceName(w, res), {.tooltip = "Есть: " + money(g->stock(res))});
      double v = it->second;
      if (ui::numberField("amount", v, {.min = 0, .max = 1e12, .step = 10, .digits = 3, .tooltip = "Количество"})) it->second = std::max(0.0, v);
      a.markUi("truce." + k + ".res." + std::to_string(res));
      if (ui::iconButton("close", "Убрать", {.size = ui::Size::Small})) {
        it = terms.resources.erase(it);
        continue;
      }
      ++it;
    }
    {
      std::vector<const CatalogItem*> list;
      for (const CatalogItem& c : w.catalogs->resources)
        if (c.id != kGold && !terms.resources.count(c.id)) list.push_back(&c);
      std::vector<std::string> hints;
      for (const CatalogItem* c : list) hints.push_back("есть " + money(g->stock(c->id)));
      int idx = -1;
      if (!list.empty() &&
          ui::combo("res", idx, int(list.size()),
                    [&](int i) {
                      const CatalogItem* c = list[size_t(i)];
                      return ui::Option{c->name, !c->icon.empty() && gfx::hasIcon(c->icon) ? c->icon.c_str() : "resource", c->color, hints[size_t(i)]};
                    },
                    {.placeholder = "Добавить ресурс", .icon = "plus"}) &&
          idx >= 0 && idx < int(list.size()))
        terms.resources[list[size_t(idx)]->id] = 0;
      a.markUi("truce." + k + ".resAdd");
    }
    // Рабы из населения: не больше 5 % населения стороны.
    ui::caption("Рабы");
    {
      const i64 cap = rules::truceSlavesMax(w, giver);
      ui::Row r({ui::fr(1), ui::px(84)}, 30, 8);
      ui::numberField("slaves", terms.slaves, {.min = 0, .max = double(cap), .step = 100, .icon = "shackles", .disabled = cap <= 0,
                                               .tooltip = "Из населения — не больше 5 %"});
      a.markUi("truce." + k + ".slaves");
      ui::label("≤ " + fmtInt(cap), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Right});
    }
    // Пленные герои, которых сторона держит.
    std::vector<Id> held = rules::captivesOf(w, giver);
    if (!held.empty()) {
      ui::caption("Пленные герои");
      for (Id h : held) {
        ui::IdScope hs{i64(h)};
        const Character* c = w.character(h);
        if (!c) continue;
        bool on = std::find(terms.heroes.begin(), terms.heroes.end(), h) != terms.heroes.end();
        std::string label = (c->name.empty() ? std::string("Без имени") : c->name) + (c->faction ? " · " + w.factionName(c->faction) : std::string());
        if (ui::checkbox(label, on)) {
          if (on) terms.heroes.push_back(h);
          else terms.heroes.erase(std::remove(terms.heroes.begin(), terms.heroes.end(), h), terms.heroes.end());
        }
        a.markUi("truce." + k + ".hero." + std::to_string(h));
      }
    }
    // Вассалитет (ТЗ «Механика вассалитета»).
    ui::spacer(2);
    ui::checkbox("Стать вассалом «" + w.factionName(taker) + "»", terms.vassal);
    a.markUi("truce." + k + ".vassal");
  }

  bool draw(App& a) override {
    const World& w = frameWorld(a);
    if (!w.faction(t.a) || !w.faction(t.b)) return false;
    {
      ui::Row r({ui::fr(1), ui::px(64), ui::fr(1)}, ui::kAuto, 12);
      factionBig(w, t.a);
      {
        RectF ic = ui::next(64, 78);
        const ui::Theme& th = ui::theme();
        RectF b{ic.cx() - 22, ic.y + 4, 44, 44};
        ui::draw::rect(b, th.accent.alpha(0.16f), 22);
        ui::draw::icon("handshake", b.inset(10), th.accent);
      }
      factionBig(w, t.b);
    }
    // Итоговое состояние: в войне остаться нельзя.
    {
      static const RelStatus kAfter[] = {RelStatus::Neutral, RelStatus::Alliance, RelStatus::Unknown};
      int idx = 0;
      for (int i = 0; i < 3; i++)
        if (kAfter[i] == t.status) idx = i;
      ui::Row r({ui::px(150), ui::fr(1)}, 30, 10);
      ui::label("После перемирия", {.ink = ui::Ink::Dim, .icon = "diplomacy"});
      if (ui::segmented("status", idx, {{relIcon(RelStatus::Neutral), relLabel(RelStatus::Neutral), {}}, {relIcon(RelStatus::Alliance), relLabel(RelStatus::Alliance), {}},
                                        {relIcon(RelStatus::Unknown), relLabel(RelStatus::Unknown), {}}},
                        {.size = ui::Size::Small}))
        t.status = kAfter[std::clamp(idx, 0, 2)];
      a.markUi("truce.status");
    }
    {
      const float maxH = std::clamp(ui::viewport().h - 330, 220.f, 640.f);
      ui::Scroll sc("body", contentH > 0 ? std::min(maxH, contentH + 2) : maxH);
      ui::Row r({ui::fr(1), ui::fr(1)}, ui::kAuto, 12);
      float h = 0;
      {
        ui::Group g(0, 8);
        side(a, w, "a", t.a, t.b, t.fromA);
        h = std::max(h, ui::lastItem().rect.h);   // карточка стороны — последний элемент
      }
      {
        ui::Group g(0, 8);
        side(a, w, "b", t.b, t.a, t.fromB);
        h = std::max(h, ui::lastItem().rect.h);
      }
      if (h > 0 && std::fabs(h - contentH) > 0.5f) {
        contentH = h;
        ui::requestRedraw();
      }
    }
    // Почему нельзя заключить: список причин правил.
    std::vector<std::string> problems = rules::truceProblems(w, t);
    for (size_t i = 0; i < problems.size() && i < 4; i++) {
      ui::IdScope ps{int(i)};
      ui::label(problems[i], {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
    }
    if (!problems.empty()) a.markUi("truce.problems");
    ui::ModalFooter f;
    if (ui::button("Отмена")) return false;
    a.markUi("truce.cancel");
    // Без кнопки по умолчанию: Enter в полях условий только фиксирует значение, а не заключает перемирие.
    if (ui::button("Заключить перемирие", {.variant = ui::Variant::Primary, .icon = "handshake", .disabled = !problems.empty() || a.readOnly()})) {
      rules::Truce tr = t;
      if (a.act("Перемирие", [&](Tx& tx) { rules::concludeTruce(tx, tr); })) {
        a.toast("Перемирие: " + w.factionName(tr.a) + " и " + w.factionName(tr.b), ToastKind::Success, "handshake");
        return false;
      }
    }
    a.markUi("truce.ok");
    return true;
  }
};

}  // namespace

namespace flow {

void openTruce(App& a, Id stateA, Id stateB) {
  if (a.readOnly()) {
    a.act("Перемирие", [](Tx&) {});   // покажет подсказку о прошлом ходе
    return;
  }
  const World& w = a.world();
  const Faction* A = w.faction(stateA);
  const Faction* B = w.faction(stateB);
  if (!A || !B || !A->isState() || !B->isState() || stateA == stateB) {
    a.toast("Перемирие заключают два государства", ToastKind::Warning, "handshake");
    return;
  }
  if (w.relation(stateA, stateB).s != RelStatus::War) {
    a.toast("«" + w.factionName(stateA) + "» и «" + w.factionName(stateB) + "» не в войне", ToastKind::Info, "handshake");
    return;
  }
  auto d = std::make_unique<TruceDlg>();
  d->t.a = stateA;
  d->t.b = stateB;
  d->t.status = RelStatus::Neutral;
  a.openDialog(std::move(d));
}

}  // namespace flow

}  // namespace rg::app
