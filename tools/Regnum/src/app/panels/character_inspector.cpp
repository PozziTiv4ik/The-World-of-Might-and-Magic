// Regnum — инспектор персонажа: шапка (портрет, имя, фракция, титул; заметные состояния «Мертв» с местом захоронения
// и «Взят в плен» с пленившим государством), вкладка «Сведения» (портрет из PNG/JPEG, имя, титул, фракция, отметка
// героя, содержание — расход на специалистов ТЗ 1.e.i, золото до тысячных; модификаторы героя со сроками — ТЗ
// «Модификаторы»; воскрешение погибшего — ТЗ «Механика героев»; заметки и раздел «Канон» — связь с карточкой,
// выбор среди одноимённых, портрет из карточки) и вкладка «Роли» (правитель, лорд провинций, места в совете, герой и
// полководец войск — ТЗ 1.a.vi, 1.b.iv, 1.c.iii) с назначением только в своей фракции и снятием.
#include <algorithm>

#include "app/app_internal.h"
#include "app/canon.h"
#include "app/widgets.h"

namespace rg::app::chars {   // объявления из character_common.cpp (struct Roles — точная копия)
struct Roles {
  std::vector<Id> rulerOf;
  std::vector<Id> lordOf;
  std::vector<std::pair<Id, Id>> seats;
  std::vector<Id> armies;
  std::vector<Id> commands;
  size_t total() const { return rulerOf.size() + lordOf.size() + seats.size() + armies.size(); }
};
Roles rolesOf(const World& w, Id character);
std::string rolesText(const World& w, const Roles& r);
const gfx::Image* portraitImage(const Character& c, bool square);
void avatar(const Character& c, float size, bool ring, std::string_view tip);
void askDelete(App& a, Id character);
void loadPortrait(App& a, Id character);
void askClearPortrait(App& a, Id character);
std::string positionOf(const World& w, Id faction, Id seat);
}  // namespace rg::app::chars

namespace rg::app::edkit {   // editors/modifiers.cpp — фишки с переносом
void chipsBegin();
ui::ChipAction chip(std::string_view label, const ui::ChipOpt& o);
void chipsEnd();
}  // namespace rg::app::edkit

namespace rg::app {
namespace {

std::string orName(const std::string& s, const char* fallback) { return s.empty() ? std::string(fallback) : s; }
std::string armyName(const World& w, Id army) { return detail::entityName(w, {SelType::Army, army}); }

// ---------------------------------------------------------------- шапка
void drawHeader(App& a, Id cid) {
  const World& w = a.world();
  const Character* c = w.character(cid);
  if (!c) return;
  chars::Roles r = chars::rolesOf(w, cid);
  bool ruler = !r.rulerOf.empty();
  const bool dead = rules::characterHas(w, cid, schema::mod::Dead);
  const bool captive = rules::characterHas(w, cid, schema::mod::Captive);
  ui::Row row({ui::px(64), ui::fr(1)}, ui::kAuto, 12);
  {
    ui::Group g(64, 0);
    chars::avatar(*c, 56, ruler, ruler ? "Правитель" : std::string_view());
    if (dead) {   // погибший — приглушённый портрет со знаком
      RectF ar = ui::lastItem().rect;
      const ui::Theme& th = ui::theme();
      ui::draw::circle(ar.cx(), ar.cy(), 28, th.surface1.alpha(0.5f));
      ui::draw::icon("skull", RectF{ar.cx() - 11, ar.cy() - 11, 22, 22}, th.text);
    }
  }
  {
    ui::Group g(0, 4);
    ui::caption(c->hero ? "Персонаж · значимый герой" : "Персонаж");
    ui::label(orName(c->name, "Без имени"), {.font = ui::Font::Heading, .ink = dead ? ui::Ink::Dim : ui::Ink::Normal});
    // Фракция, титул и состояние — фишками с переносом (длинные названия не обрезаются).
    edkit::chipsBegin();
    if (const Faction* f = w.faction(c->faction)) {
      ui::ChipOpt co;
      co.color = f->color;
      co.clickable = true;
      co.tooltip = f->isGuild() ? "Торговая гильдия — открыть" : "Государство — открыть";
      if (edkit::chip(orName(f->name, "Без названия"), co) == ui::ChipAction::Click) a.select(SelType::Faction, f->id);
    } else {
      edkit::chip("Без фракции", {.icon = "unlink"});
    }
    if (!c->title.empty()) edkit::chip(c->title, {.icon = ruler ? "crown" : "character", .tone = ruler ? ui::Tone::Accent : ui::Tone::Neutral});
    if (dead) {
      edkit::chip("Мертв", {.icon = "skull", .tone = ui::Tone::Danger, .tooltip = "Недоступен для назначений"});
      a.markUi("character.dead");
      if (const Province* p = w.province(c->burial)) {
        if (edkit::chip(orName(p->name, "Без названия"), {.icon = "map-pin", .clickable = true, .tooltip = "Место захоронения — показать"}) == ui::ChipAction::Click)
          a.select(SelType::Province, p->id, true);
        a.markUi("character.burialChip");
      }
    } else if (captive) {
      const Faction* cf = w.faction(c->captor);
      ui::ChipOpt co;
      co.icon = "shackles";
      co.tone = ui::Tone::Warning;
      co.clickable = cf != nullptr;
      co.tooltip = cf ? "Взят в плен — открыть пленившее государство" : "Взят в плен";
      if (edkit::chip(cf ? "В плену · " + orName(cf->name, "Без названия") : std::string("В плену"), co) == ui::ChipAction::Click && cf)
        a.select(SelType::Faction, cf->id);
      a.markUi("character.captive");
    }
    edkit::chipsEnd();
  }
}

// ---------------------------------------------------------------- вкладка «Сведения»
void portraitBlock(App& a, const Character& c, const chars::Roles& roles, bool ro) {
  const ui::Theme& th = ui::theme();
  Id cid = c.id;
  ui::Row row({ui::px(132), ui::fr(1)}, ui::kAuto, 14);
  {
    ui::Group g(132, 0);
    RectF pr = ui::next(132, 176);
    const gfx::Image* img = chars::portraitImage(c, false);
    if (img && !img->empty()) {
      ui::draw::shadow(pr, 10, 14, th.shadow.alpha(0.6f), 4);
      ui::draw::image(*img, pr, 10);
      ui::draw::rectStroke(pr, th.borderStrong.alpha(0.6f), 10, 1);
    } else {
      ui::draw::rect(pr, th.surface3, 10);
      ui::draw::rectStroke(pr, th.border, 10, 1);
      RectF ic{pr.cx() - 22, pr.cy() - 34, 44, 44};
      ui::draw::icon("portrait", ic, th.textMuted);
      ui::draw::text(c.portrait.empty() ? "Нет портрета" : "Загрузка…", RectF{pr.x, ic.bottom() + 8, pr.w, 18}, ui::Font::Small, th.textMuted,
                     ui::Align::Center);
    }
    a.markUi("character.portrait", pr);
  }
  {
    ui::Group g(0, 8);
    {
      ui::Disabled dis(ro);
      if (ui::button(c.portrait.empty() ? "Загрузить портрет" : "Заменить портрет", {.icon = "upload", .fill = true})) chars::loadPortrait(a, cid);
      ui::tooltip("Изображение PNG или JPEG; большие уменьшаются до 512 точек");
      a.markUi("character.loadPortrait");
      if (!c.portrait.empty()) {
        if (ui::button("Убрать портрет", {.variant = ui::Variant::Subtle, .icon = "trash", .fill = true})) chars::askClearPortrait(a, cid);
        a.markUi("character.clearPortrait");
      }
    }
    ui::stat(fmtNum(c.upkeep, 3), "Содержание за ход",
             {.icon = "coins", .tone = ui::Tone::Warning, .tooltip = "Платит фракция, которой персонаж служит: правитель, советник или герой — расход «специалисты»"});
    ui::stat(fmtInt(i64(roles.total())), plural(i64(roles.total()), "Роль", "Роли", "Ролей"),
             {.icon = "council", .tone = ui::Tone::Info, .tooltip = roles.total() ? chars::rolesText(a.world(), roles) : std::string_view("Пока без ролей")});
  }
}

// Модификаторы героя (ТЗ «Модификаторы»): фишки со сроками, добавление — «Везде» и «Для героев»; погибший — место
// захоронения и «Воскресить», пленный — пленившее государство.
void modifiersSection(App& a, const World& w, const Character& c, bool ro) {
  const Id cid = c.id;
  const bool dead = rules::characterHas(w, cid, schema::mod::Dead);
  const bool captive = rules::characterHas(w, cid, schema::mod::Captive);
  ui::Section s("Модификаторы", "sparkles", {.badge = c.modifiers.empty() ? std::string() : std::to_string(c.modifiers.size())});
  if (!s) return;
  std::vector<Id> ids = c.modifiers;
  w::ModEdit ed;
  if (w::modifierList("hero.mods", ids, ro, w::ModScope::Hero, &c.modTurns, &ed))
    a.act("Модификаторы героя", [&](Tx& tx) {
      if (!ed.addKey.empty()) ids.push_back(rules::ensureBuiltinMod(tx, ed.addKey));
      rules::setModifiers(tx, rules::ModTarget::Character, cid, ids);
    });
  else if (ed.termOf)
    a.act("Срок модификатора", [&](Tx& tx) { rules::setModTurns(tx, rules::ModTarget::Character, cid, ed.termOf, ed.turns); },
          {.coalesce = "hero.modturns:" + std::to_string(cid) + ":" + std::to_string(ed.termOf)});
  a.markUi("character.mods");
  if (dead) {
    ui::Disabled d(ro);
    ui::prop("Захоронение", "map-pin");
    Id b = c.burial;
    if (w::provincePicker("burial", b, 0, "Не указано")) a.act("Место захоронения", [&](Tx& tx) { tx.character(cid).burial = b; });
    a.markUi("character.burial");
    if (ui::button("Воскресить", {.variant = ui::Variant::Primary, .icon = "sparkles", .fill = true})) a.openDialog("hero.resurrect", cid);
    a.markUi("character.resurrect");
  } else if (captive) {
    ui::Disabled d(ro);
    ui::prop("В плену у", "shackles");
    Id who = c.captor;
    if (w::factionPicker("captor", who, w::FactionFilter::States, "Не указано", c.faction))
      a.act("Пленившее государство", [&](Tx& tx) { tx.character(cid).captor = who; });
    a.markUi("character.captor");
  }
}

void drawInfo(App& a, Id cid) {
  const World& w = a.world();
  const Character* cp = w.character(cid);
  if (!cp) return;
  const Character& c = *cp;
  bool ro = a.readOnly();
  chars::Roles roles = chars::rolesOf(w, cid);
  portraitBlock(a, c, roles, ro);
  if (ui::Section s("Основное", "user"); s) {
    ui::prop("Имя", "edit");
    std::string name = c.name;
    if (ui::textField("name", name, {.placeholder = "Имя персонажа", .maxLength = 80, .readOnly = ro}) && trim(name) != c.name)
      a.act("Имя персонажа", [&](Tx& tx) { tx.character(cid).name = trim(name); });
    a.markUi("character.name");
    ui::prop("Титул", "crown");
    std::string title = c.title;
    if (ui::textField("title", title, {.placeholder = "Король, леди, магистр…", .maxLength = 80, .readOnly = ro}) && trim(title) != c.title)
      a.act("Титул персонажа", [&](Tx& tx) { tx.character(cid).title = trim(title); });
    a.markUi("character.title");
    ui::prop("Фракция", "flag");
    Id fac = c.faction;
    if (w::factionPicker("faction", fac, w::FactionFilter::Any, "Без фракции", 0, ro)) {
      int left = 0;
      a.act("Фракция персонажа", [&](Tx& tx) {
        // Герой сопровождает только отряды своей фракции: из чужих войск он уходит (rules::setHero).
        for (Id army : roles.armies) {
          const Army* ar = tx.w().army(army);
          if (!ar) continue;
          bool inOther = false;
          for (const ArmyGroup& g : ar->groups)
            if (g.faction != fac && std::find(g.heroes.begin(), g.heroes.end(), cid) != g.heroes.end()) inOther = true;
          if (inOther) {
            rules::setHero(tx, army, cid, false);
            left++;
          }
        }
        tx.character(cid).faction = fac;
      });
      if (left) a.toast("«" + orName(c.name, "Персонаж") + "» покинул войска прежней фракции: " + std::to_string(left), ToastKind::Info, "army");
    }
    a.markUi("character.faction");
    bool hero = c.hero;
    if (ui::toggle("Значимый герой фракции", hero, ro)) a.act(hero ? "Отметить героем" : "Снять отметку героя", [&](Tx& tx) { tx.character(cid).hero = hero; });
    a.markUi("character.hero");
    ui::prop("Содержание", "coins");
    double up = c.upkeep;
    if (ui::numberField("upkeep", up, {.min = 0, .max = 1e12, .step = 1, .digits = 3, .unit = "за ход", .disabled = ro,
                                       .tooltip = "Начисляется, пока персонаж правитель, советник или герой фракции"}))
      a.act("Содержание персонажа", [&](Tx& tx) { tx.character(cid).upkeep = std::max(0.0, up); }, {.coalesce = "upkeep:" + std::to_string(cid)});
    a.markUi("character.upkeep");
    if (c.faction) {
      // Содержание входит в расход «специалисты», только пока персонаж служит фракции (правило RULES.md §3).
      const Faction* f = w.faction(c.faction);
      bool serves = f && (f->ruler == cid || (c.hero && c.faction == f->id));
      if (f && !serves)
        for (const auto& seat : f->council) if (seat.character == cid) serves = true;
      auto calc = rules::calc(w);
      const rules::FactionCalc* fc = calc->faction(c.faction);
      if (fc && serves) {
        double share = fc->expSpecialists > 0 ? c.upkeep / fc->expSpecialists : 0;
        ui::label("Специалисты «" + w.factionName(c.faction) + "»: " + fmtNum(fc->expSpecialists, 3) + " за ход", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
        ui::progress(share, {.tone = ui::Tone::Warning, .height = 4, .text = fmtPct(share * 100)});
      } else if (fc) {
        ui::label("Без должности и не герой — содержание не начисляется", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .wrap = true});
      }
    }
  }
  modifiersSection(a, w, c, ro);
  if (ui::Section s("Заметки", "note", {.defaultOpen = !c.notes.empty() || !c.entity.empty()}); s) {
    canon::notesField(a, SelType::Character, cid);
    a.markUi("character.notes");
  }
  if (ui::Section s("Канон", "book", {.defaultOpen = true}); s) canon::section(a, SelType::Character, cid);
  ui::spacer(4);
  {
    ui::Disabled dis(ro);
    if (ui::button("Удалить персонажа", {.variant = ui::Variant::Danger, .icon = "trash", .fill = true})) chars::askDelete(a, cid);
    a.markUi("character.delete");
  }
}

// ---------------------------------------------------------------- вкладка «Роли»
void removeButton(const char* tip, bool ro, const std::function<void()>& fn) {
  if (ui::iconButton("close", tip, {.size = ui::Size::Small, .disabled = ro})) fn();
}

void drawRoles(App& a, Id cid) {
  const World& w = a.world();
  const Character* cp = w.character(cid);
  if (!cp) return;
  const Character& c = *cp;
  bool ro = a.readOnly();
  chars::Roles r = chars::rolesOf(w, cid);
  const Faction* own = w.faction(c.faction);
  bool state = own && own->isState();
  std::string who = "«" + orName(c.name, "Персонаж") + "»";
  // Мёртвого и пленного назначать нельзя (ТЗ «Модификаторы», 1.6 и 1.12).
  const bool avail = rules::heroAvailable(w, cid);
  if (!avail) ui::label(w::heroState(w, c) + " — недоступен для назначений", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "lock", .wrap = true});

  // Правитель (ТЗ 1.b.iv).
  {
    std::string badge = std::to_string(r.rulerOf.size());
    ui::Section s("Правитель", "crown", {.badge = r.rulerOf.empty() ? std::string_view() : std::string_view(badge)});
    a.markUi("character.roles.ruler");
    if (s) {
      for (Id fid : r.rulerOf) {
        const Faction* f = w.faction(fid);
        if (!f) continue;
        ui::IdScope sc{i64(fid)};
        ui::Row row({ui::fr(1), ui::px(24)}, 28, 6);
        {
          ui::HStack hs(28, ui::Align::Left, 6);
          w::factionChip(fid, true);
          if (!f->rulerTitle.empty()) ui::label(f->rulerTitle, {.ink = ui::Ink::Dim});
        }
        removeButton("Снять с правления", ro, [&] { a.act("Снять правителя", [&](Tx& tx) { rules::setRuler(tx, fid, 0); }); });
      }
      if (state && own->ruler != cid) {
        std::string label = "Сделать правителем «" + orName(own->name, "государства") + "»";
        ui::Disabled dis(ro || !avail);
        if (ui::button(label, {.icon = "crown", .fill = true})) {
          Id fid = own->id;
          std::string title = c.title;
          auto apply = [fid, cid, title](App& x) {
            x.act("Новый правитель", [&](Tx& tx) {
              rules::setRuler(tx, fid, cid);
              if (!title.empty()) tx.faction(fid).rulerTitle = title;
            });
          };
          if (own->ruler && w.character(own->ruler))
            a.confirm("Сменить правителя?", "Сейчас правит «" + w.characterName(own->ruler) + "». Трон перейдёт к " + who + ".", "Сменить", false, apply);
          else
            apply(a);
        }
        a.markUi("character.makeRuler");
      } else if (r.rulerOf.empty()) {
        ui::label(own ? "Правители бывают только у государств." : "Назначьте фракцию, чтобы сделать правителем.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      }
    }
  }
  // Лорд провинций (ТЗ 1.a.vi): только провинции своего государства.
  {
    std::string badge = std::to_string(r.lordOf.size());
    ui::Section s("Лорд провинций", "lord", {.badge = r.lordOf.empty() ? std::string_view() : std::string_view(badge)});
    a.markUi("character.roles.lord");
    if (s) {
      if (!r.lordOf.empty()) {
        edkit::chipsBegin();
        for (Id pid : r.lordOf) {
          const Province* p = w.province(pid);
          ui::IdScope sc{i64(pid)};
          ui::ChipOpt co;
          co.icon = "province";
          co.color = w::factionColor(w, p ? p->owner : 0);
          co.clickable = true;
          co.removable = !ro;
          co.tooltip = "Открыть провинцию";
          auto act = edkit::chip(orName(p ? p->name : std::string(), "Без названия"), co);
          if (act == ui::ChipAction::Click) a.select(SelType::Province, pid, true);
          if (act == ui::ChipAction::Remove) a.act("Снять лорда провинции", [&](Tx& tx) { rules::setLord(tx, pid, 0); });
        }
        edkit::chipsEnd();
      }
      if (!ro) {
        std::vector<const Province*> ps;
        if (state && avail)
          w.provinces.each([&](const Province& p) {
            if (!p.sea && p.owner == c.faction && p.lord != cid) ps.push_back(&p);
          });
        std::sort(ps.begin(), ps.end(), [&](const Province* x, const Province* y) { return compareRu(x->name, y->name) < 0; });
        std::vector<std::string> hints(ps.size());
        for (size_t i = 0; i < ps.size(); i++) hints[i] = ps[i]->lord ? "лорд: " + w.characterName(ps[i]->lord) : std::string("без лорда");
        int idx = -1;
        if (ui::combo("lordof", idx, int(ps.size()),
                      [&](int i) { return ui::Option{ps[size_t(i)]->name, "province", w::factionColor(w, ps[size_t(i)]->owner), hints[size_t(i)]}; },
                      {.placeholder = ps.empty() ? "Нет провинций своего государства" : "Назначить лордом провинции", .search = 1, .icon = "plus",
                       .disabled = ps.empty(), .popupWidth = 320}) &&
            idx >= 0 && idx < int(ps.size())) {
          Id pid = ps[size_t(idx)]->id;
          a.act("Назначить лорда провинции", [&](Tx& tx) { rules::setLord(tx, pid, cid); });
        }
        a.markUi("character.addLord");
      }
    }
  }
  // Совет государства (ТЗ 1.b.iv: «Назначение в совете, назначенный лорд»).
  {
    std::string badge = std::to_string(r.seats.size());
    ui::Section s("Совет", "council", {.badge = r.seats.empty() ? std::string_view() : std::string_view(badge)});
    a.markUi("character.roles.council");
    if (s) {
      for (auto [fid, sid] : r.seats) {
        ui::IdScope sc{i64(sid)};
        ui::Row row({ui::fr(1), ui::fr(1), ui::px(24)}, 28, 6);
        ui::label(chars::positionOf(w, fid, sid), {.icon = "council"});
        w::factionChip(fid);
        removeButton("Освободить место в совете", ro, [&, fid = fid, sid = sid] {
          a.act("Освободить место в совете", [&](Tx& tx) { rules::setCouncilMember(tx, fid, sid, 0); });
        });
      }
      if (state && !ro) {
        // Пункты: свободные места государства, затем должности справочника (новое место).
        struct Opt {
          Id seat = 0;
          std::string position, label, hint;
        };
        std::vector<Opt> opts;
        if (avail) {
          for (const CouncilSeat& st : own->council)
            if (!st.character) opts.push_back({st.id, st.position, orName(st.position, "Советник"), "свободно"});
          for (const CatalogItem& pos : w.catalogs->positions) {
            bool exists = false;
            for (const CouncilSeat& st : own->council) exists = exists || utf8::searchKey(st.position) == utf8::searchKey(pos.name);
            if (!exists) opts.push_back({0, pos.name, pos.name, "новое место"});
          }
        }
        int idx = -1;
        if (ui::combo("seat", idx, int(opts.size()), [&](int i) { return ui::Option{opts[size_t(i)].label, "council", {}, opts[size_t(i)].hint}; },
                      {.placeholder = "Занять место в совете", .icon = "plus", .disabled = opts.empty(), .popupWidth = 300}) &&
            idx >= 0 && idx < int(opts.size())) {
          Opt o = opts[size_t(idx)];
          Id fid = own->id;
          a.act("Место в совете", [&](Tx& tx) {
            Id seat = o.seat;
            if (!seat) {
              CouncilSeat st;
              st.id = tx.nextId(Seq::Council);
              st.position = o.position;
              tx.faction(fid).council.push_back(st);
              seat = st.id;
            }
            rules::setCouncilMember(tx, fid, seat, cid);
          });
        }
        a.markUi("character.addSeat");
      } else if (r.seats.empty()) {
        ui::label(own ? "Совет есть только у государств." : "Назначьте государство, чтобы занять место в совете.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      }
    }
  }
  // Войска и флот: герой и главный полководец (ТЗ 1.c.iii).
  {
    std::string badge = std::to_string(r.armies.size());
    ui::Section s("Войска и флот", "army", {.badge = r.armies.empty() ? std::string_view() : std::string_view(badge)});
    a.markUi("character.roles.armies");
    if (s) {
      for (Id aid : r.armies) {
        const Army* ar = w.army(aid);
        if (!ar) continue;
        ui::IdScope sc{i64(aid)};
        ui::Row row({ui::fr(1), ui::px(120), ui::px(24)}, 30, 6);
        ui::ChipOpt co;
        co.icon = ar->isFleet() ? "fleet" : "army";
        co.color = w::factionColor(w, ar->leader());
        co.clickable = true;
        co.tooltip = "Открыть и показать на карте";
        if (ui::chip(armyName(w, aid), co) == ui::ChipAction::Click) a.select(SelType::Army, aid, true);
        bool cmd = ar->commander == cid;
        if (ui::checkbox(ar->isFleet() ? "Флотоводец" : "Полководец", cmd, ro || !avail)) {
          a.act(cmd ? "Главный полководец" : "Снять главного полководца", [&](Tx& tx) { rules::setCommander(tx, aid, cmd ? cid : 0); });
        }
        removeButton(ar->isFleet() ? "Покинуть флот" : "Покинуть войско", ro,
                     [&] { a.act("Герой покинул войско", [&](Tx& tx) { rules::setHero(tx, aid, cid, false); }); });
      }
      if (c.faction && !ro) {
        std::vector<const Army*> list;
        if (avail)
          w.armies.each([&](const Army& ar) {
            if (std::find(r.armies.begin(), r.armies.end(), ar.id) != r.armies.end()) return;
            for (const ArmyGroup& g : ar.groups)
              if (g.faction == c.faction) {
                list.push_back(&ar);
                return;
              }
          });
        std::vector<std::string> names(list.size());
        for (size_t i = 0; i < list.size(); i++) names[i] = armyName(w, list[i]->id);
        int idx = -1;
        if (ui::combo("army", idx, int(list.size()),
                      [&](int i) {
                        const Army* ar = list[size_t(i)];
                        return ui::Option{names[size_t(i)], ar->isFleet() ? "fleet" : "army", w::factionColor(w, ar->leader()),
                                          ar->commander ? std::string_view("есть полководец") : std::string_view()};
                      },
                      {.placeholder = list.empty() ? "Нет войск фракции на карте" : "Сопровождать войско или флот", .icon = "plus",
                       .disabled = list.empty(), .popupWidth = 320}) &&
            idx >= 0 && idx < int(list.size())) {
          Id aid = list[size_t(idx)]->id;
          a.act("Герой в войске", [&](Tx& tx) { rules::setHero(tx, aid, cid, true); });
        }
        a.markUi("character.addArmy");
      } else if (r.armies.empty()) {
        ui::label("Героями войск бывают персонажи фракции.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      }
    }
  }
}

int rolesBadge(App& a, Id cid) { return int(chars::rolesOf(a.world(), cid).total()); }

HeaderReg header({"character.header", SelType::Character, 0, drawHeader});
TabReg tabInfo({"character.info", "user", "Сведения", 10, SelType::Character, nullptr, drawInfo});
TabReg tabRoles({"character.roles", "council", "Роли", 20, SelType::Character, nullptr, drawRoles, rolesBadge});

}  // namespace
}  // namespace rg::app
