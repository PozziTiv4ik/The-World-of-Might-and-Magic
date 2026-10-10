// Regnum — встреча войск при перетаскивании (ТЗ 1.c.iv): предложение объединить объекты одной фракции или обменяться
// отрядами (ТЗ «Доработки №3», п.6), создать союзное войско (флот) для союзников — управление у фракции цели (ТЗ
// «Доработки №4», п.9), посадить войско на флот своего государства или объявить войну фракции со статусом-кво или
// незнакомой (затем битва и вассалитет — flow::afterWarDeclared). Согласие выполняет действие (отменяется Ctrl+Z);
// отказ возвращает перемещённый объект на исходную позицию.
#include "app/app_internal.h"
#include "app/flows.h"
#include "app/panels/military.h"

namespace rg::app::mil {

// Что станет с отрядами при союзе (ТЗ 1.c.iv): отряды фракции, у которой в цели уже есть плитка, сложатся с ней;
// остальные фракции займут свои плитки. Разные фракции не смешиваются.
std::string allianceText(const World& w, const Army& m, const Army& t) {
  bool fleet = m.isFleet();
  auto names = [&](const std::vector<Id>& fs) {
    std::string s;
    for (size_t i = 0; i < fs.size(); i++) s += std::string(i ? (i + 1 == fs.size() ? " и " : ", ") : "") + "«" + w.factionName(fs[i]) + "»";
    return s;
  };
  std::vector<Id> into, fresh;
  for (const ArmyGroup& g : m.groups) {
    bool has = false;
    for (const ArmyGroup& tg : t.groups) has = has || tg.faction == g.faction;
    (has ? into : fresh).push_back(g.faction);
  }
  const char* what = fleet ? "Корабли" : "Отряды";
  const char* obj = fleet ? "союзного флота" : "союзного войска";
  std::string s;
  if (into.empty()) {
    s = std::string(what) + " фракций не сложатся — каждая останется в своей плитке " + obj + ".";
  } else {
    s = std::string(what) + (into.size() > 1 ? " фракций " : " фракции ") + names(into) + " сложатся с " + (into.size() > 1 ? "их плитками" : "её плиткой") + " в «" + objectName(t) + "»";
    if (!fresh.empty()) s += std::string(", ") + (fresh.size() > 1 ? "фракций " : "фракции ") + names(fresh) + " — в " + (fresh.size() > 1 ? "свои новые плитки" : "свою новую плитку");
    s += ". " + std::string(what) + " разных фракций не смешиваются.";
  }
  return s + " Отказ вернёт «" + objectName(m) + "» на исходную позицию.";
}

namespace {

// Карточка объекта: фигурка, название, фракции, численность.
void objectCard(const World& w, const Army& ar, std::string_view role) {
  ui::IdScope s(i64(ar.id) + 0x70000000LL);
  ui::Card c({.pad = 12});
  {
    ui::Row r({ui::px(46), ui::fr(1)}, ui::kAuto, 10);
    figure(w, ar, 46);
    ui::Group g(0, 1);
    ui::caption(role);
    ui::label(objectName(ar), {.font = ui::Font::Strong});
    ui::label(fmtCount(unitCount(ar)) + " " + (ar.isFleet() ? plural(unitCount(ar), "корабль", "корабля", "кораблей") : plural(unitCount(ar), "воин", "воина", "воинов")),
              {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    if (ar.isFleet())
      if (const Army* cargo = w.army(rules::cargoOf(w, ar.id)))
        ui::label("На борту: " + objectName(*cargo) + " · " + fmtCount(rules::armySize(w, cargo->id)), {.font = ui::Font::Small, .ink = ui::Ink::Dim});
  }
  ui::HStack hs(20, ui::Align::Left, 6);
  for (Id f : factionsIn(ar)) {
    ui::IdScope fs{i64(f)};
    factionFlag(w, f, 24, 16);
  }
  if (factionsIn(ar).size() == 1) ui::label(w.factionName(ar.leader()), {.font = ui::Font::Small, .ink = ui::Ink::Dim});
}

// Крупный флаг и название фракции по центру столбца.
void factionBig(const World& w, Id f) {
  ui::IdScope s(i64(f) + 0x71000000LL);
  ui::Group g(0, 6);
  float fw = 78, fh = 52;
  RectF slot = ui::next(fh);
  ui::at(RectF{std::round(slot.x + (slot.w - fw) * 0.5f), slot.y, fw, fh});
  factionFlag(w, f, fw, fh);
  ui::label(w.factionName(f), {.font = ui::Font::Strong, .align = ui::Align::Center});
}

struct EncounterDialog final : Dialog {
  Id moving = 0, target = 0;
  rules::Encounter enc;
  Vec2 origin;
  std::function<void(App&, bool)> done;
  bool decided = false;

  const char* id() const override { return "army.encounter"; }

  Style style(App& a) override {
    const Army* m = a.world().army(moving);
    bool fleet = m && m->isFleet();
    switch (enc.type) {
      case rules::EncounterType::Merge: return {fleet ? "Объединить флоты?" : "Объединить войска?", "merge", ui::Tone::Accent, 560};
      case rules::EncounterType::Embark: return {"Посадить на флот?", "embark", ui::Tone::Accent, 560};
      case rules::EncounterType::Alliance: {
        // Цель уже союзная — присоединение, иначе создаётся новый союзный объект.
        const Army* t = a.world().army(target);
        if (t && t->allied()) return {fleet ? "Присоединиться к союзному флоту?" : "Присоединиться к союзному войску?", "alliance", ui::Tone::Success, 560};
        return {fleet ? "Создать союзный флот?" : "Создать союзное войско?", "alliance", ui::Tone::Success, 560};
      }
      default: return {"Объявить войну?", "war", ui::Tone::Danger, 560};
    }
  }

  void finish(App& a, bool ok) {
    if (decided) return;
    decided = true;
    if (done) {
      auto fn = done;
      detail::later(a, [fn, ok](App& x) { fn(x, ok); });
    }
  }

  void dismissed(App& a) override { finish(a, false); }

  bool accept(App& a) {
    const World& w = frameWorld(a);
    const Army* m = w.army(moving);
    const Army* t = w.army(target);
    if (!m || !t) return false;
    bool fleet = m->isFleet();
    Id mv = moving, tg = target;
    switch (enc.type) {
      case rules::EncounterType::Merge:
        if (!a.act(fleet ? "Объединить флоты" : "Объединить войска", [&](Tx& tx) { rules::mergeArmies(tx, tg, mv); })) return false;
        a.select(SelType::Army, tg);
        return true;
      case rules::EncounterType::Alliance:
        if (!a.act(t->allied() ? (fleet ? "Присоединиться к союзному флоту" : "Присоединиться к союзному войску")
                               : (fleet ? "Создать союзный флот" : "Создать союзное войско"),
                   [&](Tx& tx) { rules::formAllied(tx, tg, mv); }))
          return false;
        a.select(SelType::Army, tg);
        return true;
      case rules::EncounterType::Embark: {
        // Войско на флот или флот к войску: флот остаётся на месте.
        const Id army = fleet ? tg : mv, fl = fleet ? mv : tg;
        const Army* ar = w.army(army);
        if (!ar || !a.act("Посадка на флот: " + objectName(*ar), [&](Tx& tx) { rules::embark(tx, army, fl); })) return false;
        a.ui.tabOf[SelType::Army] = "army.cargo";
        a.select(SelType::Army, fl);
        return true;
      }
      case rules::EncounterType::DeclareWar: {
        Id us = enc.us, them = enc.them;
        if (!a.act("Объявить войну: " + w.factionName(them), [&](Tx& tx) { rules::declareWar(tx, us, them); })) return false;
        // Война объявлена — сразу панель битвы; её итог решает судьбу перемещения. После окон битвы — сюзерены и
        // вассалы воюющих сторон (ТЗ «Механика вассалитета»).
        decided = true;
        auto fn = done;
        Vec2 o = origin;
        detail::later(a, [mv, tg, o, fn, us, them](App& x) {
          openBattle(x, mv, tg, o, fn, [us, them](App& y) { flow::afterWarDeclared(y, us, them); });
        });
        return true;
      }
      default: return false;
    }
  }


  bool draw(App& a) override {
    const World& w = frameWorld(a);
    const Army* m = w.army(moving);
    const Army* t = w.army(target);
    if (!m || !t) {
      finish(a, false);
      return false;
    }
    bool fleet = m->isFleet();
    if (enc.type == rules::EncounterType::DeclareWar) {
      {
        ui::Row r({ui::fr(1), ui::px(64), ui::fr(1)}, ui::kAuto, 12);
        factionBig(w, enc.us);
        {
          RectF ic = ui::next(64, 78);
          const ui::Theme& th = ui::theme();
          RectF b{ic.cx() - 22, ic.y + 4, 44, 44};
          ui::draw::rect(b, th.danger.alpha(0.16f), 22);
          ui::draw::icon("war", b.inset(10), th.danger);
        }
        factionBig(w, enc.them);
      }
      Relation rel = w.relation(enc.us, enc.them);
      {
        ui::Card c({.pad = 10});
        ui::Row r({ui::fr(1), ui::px(150)}, 26, 10);
        ui::label(std::string("Сейчас: ") + utf8::lower(schema::relStatus(rel.s).name) + ", отношения " + fmtSigned(rel.v),
                  {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = schema::relStatus(rel.s).icon});
        ui::label("→ в войне, " + fmtSigned(schema::kWarRelation), {.font = ui::Font::Strong, .ink = ui::Ink::Danger, .align = ui::Align::Right});
      }
      ui::text("Затем начнётся битва: «" + objectName(*m) + "» против «" + objectName(*t) + "». Отказ вернёт " + (fleet ? "флот" : "войско") +
                   " на исходную позицию.",
               ui::Font::Body, ui::Ink::Dim);
    } else {
      const bool embark = enc.type == rules::EncounterType::Embark;
      // Посадка: слева войско, справа флот (флот, наведённый на войско, забирает его на борт).
      const Army* left = embark && fleet ? t : m;
      const Army* right = embark && fleet ? m : t;
      {
        ui::Row r({ui::fr(1), ui::px(40), ui::fr(1)}, ui::kAuto, 10);
        objectCard(w, *left, embark ? "Садится на борт" : "Перемещается");
        {
          RectF ic = ui::next(40, 92);
          const ui::Theme& th = ui::theme();
          bool merge = enc.type == rules::EncounterType::Merge;
          Color col = merge || embark ? th.accent : th.success;
          RectF b{ic.cx() - 17, ic.cy() - 17, 34, 34};
          ui::draw::rect(b, col.alpha(0.16f), 17);
          ui::draw::icon(merge ? "merge" : embark ? "embark" : "alliance", b.inset(8), col);
        }
        objectCard(w, *right, embark ? "Флот" : "На месте");
      }
      if (embark) {
        // Вместимость флота: сколько займёт войско (ТЗ «Доработки №3», п.6).
        const i64 size = unitCount(*left), cap = rules::fleetCapacity(w, right->id);
        ui::Card c({.pad = 10});
        ui::Row r({ui::px(18), ui::fr(1), ui::px(140)}, 22, 8);
        ui::icon("embark", ui::Ink::Dim, 16, "Вместимость флота");
        ui::progress(cap > 0 ? double(size) / double(cap) : 1.0, {.tone = size > cap ? ui::Tone::Danger : ui::Tone::Accent});
        ui::label(fmtCount(size) + " / " + fmtCount(cap), {.font = ui::Font::Strong, .align = ui::Align::Right, .tooltip = "Войско / вместимость флота"});
        a.markUi("encounter.capacity");
      } else if (enc.type == rules::EncounterType::Alliance) {
        // Управление союзным объектом — у фракции цели (её группа первая).
        ui::HStack hs(22, ui::Align::Left, 6);
        ui::icon("crown", ui::Ink::Accent, 16, "Управление союзным объектом");
        ui::label(w.factionName(t->leader()), {.font = ui::Font::Strong, .tooltip = "Управление союзным объектом"});
        a.markUi("encounter.leader");
      }
      if (enc.type == rules::EncounterType::Merge)
        ui::text(std::string(fleet ? "Корабли" : "Отряды") + " и герои сложатся в «" + objectName(*t) + "». Отказ вернёт «" + objectName(*m) +
                     "» на исходную позицию.",
                 ui::Font::Body, ui::Ink::Dim);
      else if (!embark)
        ui::text(allianceText(w, *m, *t), ui::Font::Body, ui::Ink::Dim);
    }
    ui::ModalFooter f;
    if (ui::button("Отмена")) {
      finish(a, false);
      return false;
    }
    a.markUi("encounter.cancel");
    // Объекты одного государства — и обмен отрядами (ТЗ «Доработки №3», п.6): перемещённый вернётся на место.
    if (enc.type == rules::EncounterType::Merge) {
      if (ui::button("Обмен отрядами", {.icon = "exchange"})) {
        finish(a, false);
        const Id mv = moving, tg = target;
        detail::later(a, [mv, tg](App& x) { openExchange(x, mv, tg); });
        return false;
      }
      a.markUi("encounter.exchange");
    }
    std::string ok = enc.type == rules::EncounterType::Merge      ? "Объединить"
                     : enc.type == rules::EncounterType::Embark   ? "Посадить"
                     : enc.type == rules::EncounterType::Alliance ? (t->allied() ? std::string("Присоединиться") : fleet ? "Создать союзный флот" : "Создать союзное войско")
                                                                  : "Объявить войну";
    bool war = enc.type == rules::EncounterType::DeclareWar;
    if (ui::button(ok, {.variant = war ? ui::Variant::Danger : ui::Variant::Primary,
                        .icon = war ? "war" : (enc.type == rules::EncounterType::Merge ? "merge" : enc.type == rules::EncounterType::Embark ? "embark" : "alliance"),
                        .isDefault = true})) {
      bool done2 = accept(a);
      if (enc.type != rules::EncounterType::DeclareWar || !done2) finish(a, done2);
      return false;
    }
    a.markUi("encounter.ok");
    return true;
  }
};

}  // namespace

void openEncounter(App& a, Id moving, Id target, const rules::Encounter& e, Vec2 origin, std::function<void(App&, bool)> done) {
  auto d = std::make_unique<EncounterDialog>();
  d->moving = moving;
  d->target = target;
  d->enc = e;
  d->origin = origin;
  d->done = std::move(done);
  a.openDialog(std::move(d));
}

}  // namespace rg::app::mil
