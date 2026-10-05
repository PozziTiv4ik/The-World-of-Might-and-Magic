// Regnum — воскрешение погибшего героя (ТЗ «Механика героев», п.1.2; DialogReg «hero.resurrect», arg — персонаж):
// способ — как живого, как нежить, как демона, как механизм (природа героя заменяется) и государство (по умолчанию
// своё, можно другое); rules::resurrect одним действием.
#include "app/panels/faction_common.h"

namespace rg::app {

namespace {

using namespace fac;

struct Way {
  const char* key;
  const char* label;
  const char* icon;
};
constexpr Way kWays[] = {{schema::mod::Living, "Как живого", "heart"},
                         {schema::mod::Undead, "Как нежить", "skull"},
                         {schema::mod::Demon, "Как демона", "flame"},
                         {schema::mod::Mechanism, "Как механизм", "u-machines"}};

struct ResurrectDlg final : Dialog {
  Id hero = 0;
  int way = 0;
  Id state = 0;

  const char* id() const override { return "hero.resurrect"; }
  Style style(App&) override { return {"Воскресить героя", "sparkles", ui::Tone::Accent, 620}; }

  bool draw(App& a) override {
    const World& w = frameWorld(a);
    const Character* c = w.character(hero);
    if (!c || !rules::characterHas(w, hero, schema::mod::Dead)) return false;
    {
      ui::Row r({ui::px(48), ui::fr(1)}, ui::kAuto, 12);
      ui::avatar(c->name.empty() ? std::string("?") : c->name, {.image = portraitOf(*c), .size = 44});
      ui::Group g(0, 2);
      ui::label(c->name.empty() ? std::string("Без имени") : c->name, {.font = ui::Font::Title});
      ui::label(w::heroState(w, *c), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "skull"});
    }
    ui::caption("Способ");
    ui::segmented("way", way, {{kWays[0].icon, kWays[0].label, {}}, {kWays[1].icon, kWays[1].label, {}}, {kWays[2].icon, kWays[2].label, {}},
                               {kWays[3].icon, kWays[3].label, {}}});
    a.markUi("resurrect.way");
    ui::caption("Государство");
    {
      // Своё государство — по умолчанию; герой гильдии или без фракции остаётся там же, если не выбрать другое.
      const Faction* own = w.faction(c->faction);
      const std::string keep = own && own->isState() ? std::string() : own ? displayName(*own) : std::string("Без фракции");
      w::factionPicker("state", state, w::FactionFilter::States, keep);
    }
    a.markUi("resurrect.state");
    ui::ModalFooter f;
    if (ui::button("Отмена")) return false;
    a.markUi("resurrect.cancel");
    if (ui::button("Воскресить", {.variant = ui::Variant::Primary, .icon = "sparkles", .disabled = a.readOnly(), .isDefault = true})) {
      const Id h = hero, st = state;
      const char* key = kWays[std::clamp(way, 0, 3)].key;
      const std::string name = w.characterName(h);
      if (a.act("Воскресить героя", [&](Tx& tx) { rules::resurrect(tx, h, key, st); })) {
        a.toast("«" + name + "» воскрешён", ToastKind::Success, "sparkles", "Открыть", [h](App& x) { x.select(SelType::Character, h); });
        return false;
      }
    }
    a.markUi("resurrect.ok");
    return true;
  }
};

DialogReg reg({"hero.resurrect", [](App& a, Id arg) -> std::unique_ptr<Dialog> {
                 if (a.readOnly()) {
                   a.toast("Открыт прошлый ход — изменения недоступны", ToastKind::Warning, "lock", "К текущему ходу", [](App& x) { x.backToCurrent(); });
                   return nullptr;
                 }
                 const World& w = a.world();
                 const Character* c = w.character(arg);
                 if (!c || !rules::characterHas(w, arg, schema::mod::Dead)) return nullptr;
                 auto d = std::make_unique<ResurrectDlg>();
                 d->hero = arg;
                 // Способ по умолчанию — прежняя природа героя (иначе — по виду его государства).
                 int way = 0;
                 bool found = false;
                 for (int i = 0; i < 4; i++)
                   if (rules::characterHas(w, arg, kWays[i].key)) {
                     way = i;
                     found = true;
                   }
                 if (!found) {
                   StateKind k = rules::stateKindOf(w, c->faction);
                   way = k == StateKind::Undead ? 1 : k == StateKind::Demonic ? 2 : 0;
                 }
                 d->way = way;
                 const Faction* f = w.faction(c->faction);
                 d->state = f && f->isState() ? f->id : 0;
                 return d;
               }});

}  // namespace
}  // namespace rg::app
