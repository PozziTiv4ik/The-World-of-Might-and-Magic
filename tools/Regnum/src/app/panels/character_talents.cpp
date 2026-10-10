// Regnum — классы и таланты героя (ТЗ «Доработки №4», п.6, 8), «Возвысить до Лича» (ТЗ «Доработки №1», п.11):
// поля «Класс» и «Уровень» вкладки «Сведения» (смена класса и уровень ниже вложенных очков — с подтверждением сброса
// талантов), вкладка «Таланты» (дерево класса героя: изученные подсвечены, доступные — с золотой рамкой, закрытые
// приглушены; щелчок — изучить, правый щелчок — отменить; очки «вложено / всего», «Сбросить таланты»), кнопка
// «Возвысить до Лича» (только у «Некроманта») и окно выбора филактерии с ценой и проверками.
#include <algorithm>

#include "app/app_internal.h"
#include "app/talent_tree.h"
#include "app/widgets.h"
#include "gfx/icons.h"

namespace rg::app::chars {

namespace {

std::string orName(const std::string& s, const char* fallback = "Без названия") { return s.empty() ? std::string(fallback) : s; }
const char* classIcon(const HeroClass& c) { return !c.icon.empty() && gfx::hasIcon(c.icon) ? c.icon.c_str() : "hero-class"; }
Color legible(Color c) { return c.luminance() < 0.1f ? c.lighten(0.45f) : c; }

void setClass(App& a, Id cid, Id cls) {
  a.act(cls ? "Класс героя" : "Снять класс героя", [&](Tx& tx) { rules::setCharacterClass(tx, cid, cls); });
}

void askReset(App& a, Id cid) {
  const World& w = a.world();
  const int spent = rules::talentPointsSpent(w, cid);
  if (!spent) return;
  a.confirm("Сбросить таланты?",
            "Все изученные таланты «" + orName(w.characterName(cid), "Без имени") + "» будут сняты, " + std::to_string(spent) + " " +
                plural(spent, "очко вернётся", "очка вернутся", "очков вернутся") + ". Действие можно отменить Ctrl+Z.",
            "Сбросить", true, [cid](App& x) { x.act("Сбросить таланты", [&](Tx& tx) { rules::resetTalents(tx, cid); }); });
}

// Плитка значка класса цвета класса.
void classTile(const HeroClass& c, RectF r, float radius) {
  const Color col = legible(c.color);
  ui::draw::rect(r, col.alpha(0.2f), radius);
  ui::draw::rectStroke(r, col.alpha(0.5f), radius, 1);
  ui::draw::icon(classIcon(c), r.inset(std::round(r.w * 0.24f)), col);
}

}  // namespace

// ================================================================ «Сведения»: класс и уровень
// Поля «Класс» и «Уровень» (в разделе «Основное» вкладки «Сведения»).
void classFields(App& a, const Character& c, bool ro) {
  const World w = a.world();
  const Id cid = c.id;
  ui::prop("Класс", "hero-class");
  {
    std::vector<const HeroClass*> list;
    for (const HeroClass& k : w.catalogs->classes) list.push_back(&k);
    std::vector<ui::Option> opts;
    for (const HeroClass* k : list) opts.push_back(ui::Option{k->name, classIcon(*k), Color(0, 0, 0, 0)});
    int idx = -1;
    for (size_t i = 0; i < list.size(); i++)
      if (list[i]->id == c.heroClass) idx = int(i);
    if (ui::combo("heroClass", idx, std::span<const ui::Option>(opts),
                  {.placeholder = "Без класса", .noneLabel = "Без класса", .search = 1, .icon = "hero-class", .disabled = ro, .popupWidth = 280,
                   .tooltip = "Класс героя: дерево талантов"})) {
      const Id cls = idx >= 0 && idx < int(list.size()) ? list[size_t(idx)]->id : 0;
      if (cls != c.heroClass) {
        if (c.talents.empty()) {
          setClass(a, cid, cls);
        } else {
          const std::string name = cls ? orName(w.heroClass(cls)->name) : std::string("без класса");
          a.confirm("Сменить класс?", "Изученные таланты (" + std::to_string(c.talents.size()) + ") будут сброшены: у класса «" + name +
                                          "» своё дерево. Действие можно отменить Ctrl+Z.",
                    "Сменить", true, [cid, cls](App& x) { setClass(x, cid, cls); });
        }
      }
    }
    a.markUi("character.class");
  }
  ui::prop("Уровень", "star");
  {
    int level = std::clamp(c.level, 1, schema::kMaxHeroLevel);
    const int spent = rules::talentPointsSpent(w, cid);
    if (ui::numberField("level", level, {.min = 1, .max = double(schema::kMaxHeroLevel), .steppers = true, .disabled = ro,
                                         .tooltip = "Уровень героя 1…60: одно очко талантов за уровень"}) &&
        level != c.level) {
      if (level >= spent) {
        a.act("Уровень героя", [&](Tx& tx) { rules::setHeroLevel(tx, cid, level); }, {.coalesce = "herolevel:" + std::to_string(cid)});
      } else if (!ui::lastItem().active) {
        const int lv = level;
        a.confirm("Снизить уровень?",
                  "Вложено " + std::to_string(spent) + " " + plural(spent, "очко", "очка", "очков") + " талантов — на уровне " + std::to_string(lv) +
                      " их не хватит. Таланты будут сброшены. Действие можно отменить Ctrl+Z.",
                  "Сбросить и снизить", true, [cid, lv](App& x) { x.act("Уровень героя", [&](Tx& tx) { rules::setHeroLevel(tx, cid, lv, true); }); });
      }
    }
    a.markUi("character.level");
  }
}

// ================================================================ «Возвысить до Лича»
// Кнопка (видна только с «Некромантом», ещё не личу).
void lichButton(App& a, const Character& c, bool ro) {
  if (!rules::lichOffered(a.world(), c.id)) return;
  const Id cid = c.id;
  if (ui::button("Возвысить до Лича", {.variant = ui::Variant::Secondary, .icon = "lich", .fill = true, .disabled = ro,
                                        .tooltip = "20 000 трупов, 5000 эссенции смерти и реликвия не ниже эпической"}))
    a.openDialog("hero.lich", cid);
  a.markUi("character.lich");
}

namespace {

struct LichDlg final : Dialog {
  Id hero = 0;
  Id relic = 0;
  const char* id() const override { return "hero.lich"; }
  Style style(App&) override { return {"Возвысить до Лича", "lich", ui::Tone::Accent, 560}; }

  void price(App& a, const Faction* f, const char* icon, Color color, const std::string& what, double need, double have, const std::string& mark) {
    ui::HStack hs(28, ui::Align::Left, 8);
    ui::iconColored(icon, legible(color), 18, what);
    ui::label(what, {.ink = ui::Ink::Dim});
    ui::flex();
    ui::label(fmtNum(need, 0), {.font = ui::Font::Strong});
    ui::label(f ? "есть " + fmtNum(std::max(0.0, have), 0) : std::string("нет государства"),
              {.font = ui::Font::Small, .ink = f && have >= need ? ui::Ink::Success : ui::Ink::Danger});
    a.markUi(mark);
  }

  bool draw(App& a) override {
    const World w = a.world();
    const Character* c = w.character(hero);
    if (!c || !rules::lichOffered(w, hero)) return false;
    {
      ui::Row r({ui::px(48), ui::fr(1)}, ui::kAuto, 12);
      w::heroAvatar(*c, 44);
      ui::Group g(0, 2);
      ui::label(orName(c->name, "Без имени"), {.font = ui::Font::Title});
      ui::label(c->faction ? w.factionName(c->faction) : std::string("Без фракции"), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "flag"});
    }
    const Faction* f = w.faction(c->faction);
    if (f && !f->isState()) f = nullptr;
    ui::caption("Цена");
    {
      const Id corpses = rules::resourceId(w, schema::kResCorpses);
      price(a, f, w::resourceIcon(w, corpses), w::resourceColor(w, corpses), "Трупы", schema::kLichCorpses, f && corpses ? f->stock(corpses) : 0,
            "lich.corpses");
      const Id death = rules::deathEssence(w);
      price(a, f, "essence", w::essenceColor(w, death), "Эссенция смерти", schema::kLichDeathEssence, f && death ? f->essence(death) : 0,
            "lich.essence");
    }
    ui::caption("Филактерия");
    const std::vector<Id> list = rules::phylacteryRelics(w, hero);
    if (relic && std::find(list.begin(), list.end(), relic) == list.end()) relic = 0;
    if (!relic && list.size() == 1) relic = list[0];
    if (list.empty()) {
      ui::label("В инвентаре нет реликвий не ниже эпической", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "relic"});
    } else {
      const ui::Theme& th = ui::theme();
      for (Id rid : list) {
        const Relic* r = w.relic(rid);
        if (!r) continue;
        ui::IdScope s{i64(rid)};
        if (relic == rid) {
          const RectF nr = ui::avail();
          ui::draw::rect(RectF{nr.x, nr.y, nr.w, 40}, th.accent.alpha(0.1f), 8);
          ui::draw::rectStroke(RectF{nr.x, nr.y, nr.w, 40}, th.accent.alpha(0.7f), 8, 1);
        }
        RectF rr;
        if (w::relicRow(*r, relic == rid ? std::string_view("Станет филактерией") : std::string_view(), 0, &rr)) relic = rid;
        a.markUi("lich.relic." + std::to_string(rid), rr);
      }
      if (const Relic* r = w.relic(relic))
        ui::label(rules::phylacteryName(r->name, c->name), {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "edit", .tooltip = "Новое название реликвии"});
    }
    const std::vector<std::string> problems = rules::lichProblems(w, hero, relic);
    for (const std::string& p : problems)
      if (relic || p.find("филактери") == std::string::npos) ui::label(p, {.font = ui::Font::Small, .ink = ui::Ink::Danger, .icon = "warning", .wrap = true});
    a.markUi("lich.problems");
    ui::ModalFooter foot;
    if (ui::button("Отмена")) return false;
    if (ui::button("Возвысить", {.variant = ui::Variant::Primary, .icon = "lich", .disabled = !problems.empty() || a.readOnly(), .isDefault = true})) {
      const Id h = hero, rr = relic;
      const std::string name = w.characterName(h);
      if (a.act("Возвысить до Лича", [&](Tx& tx) { rules::ascendLich(tx, h, rr); })) {
        a.toast("«" + name + "» возвысился до Лича", ToastKind::Success, "lich");
        return false;
      }
    }
    a.markUi("lich.ok");
    return true;
  }
};

DialogReg lichReg({"hero.lich", [](App& a, Id arg) -> std::unique_ptr<Dialog> {
                     if (a.readOnly()) {
                       a.toast("Открыт прошлый ход — изменения недоступны", ToastKind::Warning, "lock", "К текущему ходу", [](App& x) { x.backToCurrent(); });
                       return nullptr;
                     }
                     if (!rules::lichOffered(a.world(), arg)) return nullptr;
                     auto d = std::make_unique<LichDlg>();
                     d->hero = arg;
                     return d;
                   }});

// ================================================================ вкладка «Таланты»
void drawTalents(App& a, Id cid) {
  const World w = a.world();
  const Character* c = w.character(cid);
  if (!c) return;
  const bool ro = a.readOnly();
  const HeroClass* hc = w.heroClass(c->heroClass);
  if (!hc) {
    ui::spacer(16);
    ui::emptyState("hero-class", "Класс не выбран");
    classFields(a, *c, ro);
    return;
  }
  const int total = rules::talentPoints(w, cid), spent = rules::talentPointsSpent(w, cid);
  {
    ui::Row head({ui::px(44), ui::fr(1), ui::px(30)}, 44, 10);
    classTile(*hc, ui::next(44, 44), 10);
    {
      ui::Group g(0, 2);
      ui::caption("Класс · уровень " + std::to_string(total));
      ui::label(orName(hc->name), {.font = ui::Font::Title, .color = legible(hc->color)});
    }
    if (ui::iconButton("book", "Дерево класса в справочнике")) a.openEditor("catalogs", 10);
    a.markUi("character.talents.catalog");
  }
  {
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(std::to_string(spent) + " / " + std::to_string(total), "Вложено очков", {.icon = "talent", .tone = ui::Tone::Accent});
    a.markUi("character.talents.points");
    ui::stat(std::to_string(std::max(0, total - spent)), "Свободно", {.icon = "star", .tone = total > spent ? ui::Tone::Success : ui::Tone::Neutral});
  }
  if (hc->talents.empty()) {
    ui::spacer(8);
    ui::emptyState("talent", "В дереве класса пока нет талантов");
  } else {
    talents::Options o;
    o.cls = hc;
    o.hero = cid;
    o.mark = "character.tree";
    const talents::Events ev = talents::tree(a, o);
    if (!ro && ev.clicked) {
      std::string why;
      if (rules::canLearnTalent(w, cid, ev.clicked, &why)) {
        const Id t = ev.clicked;
        a.act("Изучить талант", [&](Tx& tx) { rules::learnTalent(tx, cid, t); });
      } else if (std::find(c->talents.begin(), c->talents.end(), ev.clicked) == c->talents.end()) {
        a.toast(why, ToastKind::Warning, "lock");
      }
    }
    if (!ro && ev.rightClicked && std::find(c->talents.begin(), c->talents.end(), ev.rightClicked) != c->talents.end()) {
      const Id t = ev.rightClicked;
      a.act("Отменить изучение таланта", [&](Tx& tx) { rules::unlearnTalent(tx, cid, t); });
    }
  }
  {
    ui::Disabled d(ro || spent == 0);
    if (ui::button("Сбросить таланты", {.variant = ui::Variant::Secondary, .icon = "undo", .fill = true})) askReset(a, cid);
    a.markUi("character.talents.reset");
  }
}

// Свободные очки талантов — число на значке вкладки.
int talentsBadge(App& a, Id cid) {
  const World& w = a.world();
  const Character* c = w.character(cid);
  if (!c || !w.heroClass(c->heroClass)) return 0;
  return std::max(0, rules::talentPoints(w, cid) - rules::talentPointsSpent(w, cid));
}

TabReg tabTalents({"character.talents", "talent", "Таланты", 15, SelType::Character, nullptr, drawTalents, talentsBadge});

}  // namespace
}  // namespace rg::app::chars
