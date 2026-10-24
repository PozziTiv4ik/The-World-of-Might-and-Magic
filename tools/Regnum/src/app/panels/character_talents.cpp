// Regnum — классы, подклассы и таланты героя (ТЗ «Доработки №1», п.1; «№4», п.6, 8), «Возвысить до Лича» (ТЗ
// «Доработки №1», п.11): поля «Класс», «Подкласс» и «Уровень» вкладки «Сведения» (смена класса или подкласса с
// изученными талантами и уровень ниже вложенных очков — с подтверждением сброса), вкладка «Таланты»: без класса —
// карточки классов, у класса с несколькими подклассами до выбора — карточки подклассов (описание, ярусы, финальные
// таланты), с подклассом — шапка (класс, уровень, подкласс; смена подкласса, сброс талантов), очки «вложено / всего /
// свободно» и дерево подкласса (app/common/talent_tree.h: щелчок — следующий ранг, правый щелчок — снять ранг).
// Кнопка «Возвысить до Лича» (только у «Некроманта») и окно выбора филактерии с ценой и проверками.
// Роли: класс, подкласс, изучение и снятие рангов, сброс и «Возвысить до Лича» — игровые действия за фракцию героя
// (App::canPlay, App::play; чужой герой у игрока — только просмотр); уровень героя — правка мира (App::canEdit).
#include <algorithm>

#include "app/app_internal.h"
#include "app/common/talent_tree.h"
#include "app/panels/character_common.h"
#include "gfx/icons.h"

namespace rg::app::chars {

namespace {

using w::legible;
using w::orName;

const char* classIcon(const HeroClass& c) { return !c.icon.empty() && gfx::hasIcon(c.icon) ? c.icon.c_str() : "hero-class"; }
const char* subIcon(const HeroClass& c, const SubClass& s) { return !s.icon.empty() && gfx::hasIcon(s.icon) ? s.icon.c_str() : classIcon(c); }

std::string pts(int n) { return std::to_string(n) + " " + plural(n, "очко", "очка", "очков"); }
std::string ranksOf(size_t n) { return std::to_string(n) + " " + plural(i64(n), "ранг", "ранга", "рангов"); }

// Игровое действие с подтверждением сброса (confirmAct — только администратору).
void confirmPlay(App& a, std::string title, std::string text, std::string ok, std::string label, std::function<void(Tx&)> fn) {
  a.confirm(std::move(title), text + " Действие можно отменить Ctrl+Z.", std::move(ok), true,
            [label = std::move(label), fn = std::move(fn)](App& x) { x.play(label, fn); });
}

// Почему игрок не может сменить класс, подкласс или таланты героя (аудит 2026-10-23, 1.9): мёртвый, пленный,
// арестованный или не прошло «Ходов между сменами класса» с прошлой смены. Пусто — можно; администратору — всегда.
std::string respecWhy(App& a, const Character& c) {
  if (!a.player()) return {};
  const World& w = a.world();
  if (rules::characterHas(w, c.id, schema::mod::Dead)) return "Герой мёртв";
  if (rules::characterHas(w, c.id, schema::mod::Captive)) return "Герой в плену";
  if (rules::characterHas(w, c.id, schema::mod::Arrested)) return "Герой под арестом";
  if (const int n = rules::respecWait(w, c.id)) return "Сменить можно через " + nTurns(n);
  return {};
}

// Класс героя: с изученными талантами — подтверждение (у другого класса своё дерево).
void askClass(App& a, const Character& c, Id cls) {
  const Id cid = c.id;
  if (cls == c.heroClass) return;
  const bool player = a.player();   // игроку — не чаще «Ходов между сменами класса» (rules)
  auto fn = [cid, cls, player](Tx& tx) { rules::setCharacterClass(tx, cid, cls, player); };
  const std::string label = cls ? "Класс героя" : "Снять класс героя";
  if (c.talents.empty()) {
    a.play(label, fn);
    return;
  }
  const HeroClass* hc = a.world().heroClass(cls);
  confirmPlay(a, "Сменить класс?",
              "Изученные таланты (" + ranksOf(c.talents.size()) + ") будут сброшены" +
                  (hc ? ": у класса «" + orName(hc->name) + "» свои подклассы и деревья." : std::string(".")),
              "Сменить", label, fn);
}

// Подкласс героя: с изученными талантами — подтверждение сброса.
void askSub(App& a, const Character& c, Id sub) {
  const Id cid = c.id;
  if (sub == c.subclass) return;
  const bool player = a.player();
  auto fn = [cid, sub, player](Tx& tx) { rules::setCharacterSubclass(tx, cid, sub, player); };
  if (c.talents.empty()) {
    a.play("Подкласс героя", fn);
    return;
  }
  const SubClass* s = a.world().catalogs->subclass(sub);
  confirmPlay(a, "Сменить подкласс?",
              "Изученные таланты (" + ranksOf(c.talents.size()) + ") будут сброшены" +
                  (s ? ": у подкласса «" + orName(s->name) + "» своё дерево." : std::string(".")),
              "Сменить", "Подкласс героя", fn);
}

void askReset(App& a, Id cid) {
  const World& w = a.world();
  const int spent = rules::talentPointsSpent(w, cid);
  if (!spent) return;
  confirmPlay(a, "Сбросить таланты?",
              "Все изученные таланты «" + orName(w.characterName(cid), "Без имени") + "» будут сняты, " + std::to_string(spent) + " " +
                  plural(spent, "очко вернётся", "очка вернутся", "очков вернутся") + ".",
              "Сбросить", "Сбросить таланты", [cid, player = a.player()](Tx& tx) { rules::resetTalents(tx, cid, player); });
}

// Плитка значка цвета класса.
void classTile(const char* icon, Color c, RectF r, float radius) {
  const Color col = legible(c);
  ui::draw::gradient(r, col.alpha(0.3f), col.alpha(0.12f), radius);
  ui::draw::rectStroke(r, col.alpha(0.6f), radius, 1);
  ui::draw::icon(icon, r.inset(std::round(r.w * 0.24f)), col);
}

// Столбцов сетки из n карточек по ширине области: не больше трёх, без одинокой карточки в последней строке, когда
// можно разложить ровнее (четыре — по две).
int gridCols(float width, float minW, size_t n) {
  int c = std::clamp(int((width + 10) / (minW + 10)), 1, 3);
  c = std::min(c, std::max(1, int(n)));
  if (c == 3 && n % 3 != 0 && n % 2 == 0) c = 2;
  return c;
}

// Карточка выбора (класс или подкласс): плитка, название, подпись, строки ниже; щелчок — выбор. true — выбрана.
bool pickCard(App& a, const std::string& mark, const char* icon, Color color, const std::string& title, const std::string& sub, bool enabled,
              const std::function<void()>& body, std::string_view tip) {
  {
    ui::Card card({.pad = 12, .hoverable = enabled});
    {
      ui::Row head({ui::px(40), ui::fr(1)}, 40, 10);
      classTile(icon, color, ui::next(40, 40), 9);
      ui::Group g(0, 0);
      ui::label(title, {.font = ui::Font::Strong});
      ui::label(sub, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    }
    if (body) body();
  }
  const RectF r = ui::lastItem().rect;
  a.markUi(mark, r);
  if (!enabled) {
    if (!tip.empty()) ui::hoverTip("##tip", r, tip);
    return false;
  }
  const ui::Interaction it = ui::interact(ui::id("##pick"), r);
  if (it.hovered) ui::setCursor(platform::Cursor::Hand);
  if (!tip.empty()) ui::hoverTip("##tip", r, tip);
  return it.clicked;
}

// Карточки классов (герой без класса).
void classPicker(App& a, const World& w, const Character& c, bool play) {
  ui::caption("Класс героя");
  const float W = ui::avail().w;
  const int cols = gridCols(W, 220, w.catalogs->classes.size());
  std::vector<ui::Len> lens(size_t(cols), ui::fr(1));
  ui::Row grid(std::span<const ui::Len>(lens), ui::kAuto, 10);
  for (const HeroClass& k : w.catalogs->classes) {
    ui::IdScope s{i64(k.id)};
    const std::string sub = std::to_string(k.subclasses.size()) + " " + plural(i64(k.subclasses.size()), "подкласс", "подкласса", "подклассов");
    std::vector<std::string> names;
    for (const SubClass& x : k.subclasses) names.push_back(orName(x.name));
    std::string tip = names.empty() ? std::string() : join(names, ", ");
    if (!k.desc.empty()) tip = k.desc + (tip.empty() ? "" : "\n" + tip);
    if (pickCard(a, "character.pickClass." + std::to_string(k.id), classIcon(k), k.color, orName(k.name), sub, play, {}, tip)) askClass(a, c, k.id);
  }
}

// Карточки подклассов класса (до выбора подкласса).
void subPicker(App& a, const HeroClass& hc, const Character& c, bool play) {
  const ui::Theme& th = ui::theme();
  ui::caption("Подкласс · " + std::to_string(hc.subclasses.size()));
  const float W = ui::avail().w;
  const int cols = gridCols(W, 260, hc.subclasses.size());
  std::vector<ui::Len> lens(size_t(cols), ui::fr(1));
  ui::Row grid(std::span<const ui::Len>(lens), ui::kAuto, 10);
  for (const SubClass& s : hc.subclasses) {
    ui::IdScope is{i64(s.id)};
    const int tiers = rules::talentTiers(s);
    const std::string sub = std::to_string(s.talents.size()) + " " + plural(i64(s.talents.size()), "талант", "таланта", "талантов") + " · " +
                            std::to_string(tiers) + " " + plural(tiers, "ярус", "яруса", "ярусов");
    std::vector<std::string> ults;
    for (const Talent& t : s.talents)
      if (talents::isUltimate(t)) ults.push_back(orName(t.name));
    auto body = [&] {
      if (!s.desc.empty()) ui::label(s.desc, {.font = ui::Font::Small, .ink = ui::Ink::Dim, .wrap = true, .maxLines = 3});
      if (!ults.empty())
        ui::label(join(ults, " · "), {.font = ui::Font::Small, .color = th.accent, .icon = "crown", .wrap = true, .maxLines = 2});
    };
    if (pickCard(a, "character.pickSub." + std::to_string(s.id), subIcon(hc, s), hc.color, orName(s.name), sub, play, body,
                 ults.empty() ? std::string_view() : std::string_view("Финальные таланты подкласса")))
      askSub(a, c, s.id);
  }
}

// Шапка вкладки: плитка класса, «Класс · уровень», название подкласса или класса; справа — действия.
void talentsHead(App& a, const HeroClass& hc, const SubClass* sc, const Character& c, bool play) {
  const World& w = a.world();
  const Id cid = c.id;
  const bool canSwap = play && sc && hc.subclasses.size() > 1;
  const int btns = (canSwap ? 1 : 0) + (play && sc ? 1 : 0) + (a.admin() ? 1 : 0);
  std::vector<ui::Len> lens = {ui::px(44), ui::fr(1)};
  if (btns) lens.push_back(ui::px(float(btns) * 30 + float(btns - 1) * 4));
  ui::Row head(std::span<const ui::Len>(lens), 44, 10);
  classTile(sc ? subIcon(hc, *sc) : classIcon(hc), hc.color, ui::next(44, 44), 10);
  {
    ui::Group g(0, 2);
    ui::caption((sc ? orName(hc.name) : std::string("Класс героя")) + " · уровень " + std::to_string(rules::talentPoints(w, cid)));
    ui::label(sc ? orName(sc->name) : orName(hc.name), {.font = ui::Font::Title, .color = legible(hc.color), .tooltip = sc ? sc->desc : hc.desc});
    a.markUi("character.talents.title");
  }
  if (!btns) return;
  ui::HStack hs(30, ui::Align::Right, 4);
  const std::string why = respecWhy(a, c);   // игрок: недоступный герой или не прошёл срок с прошлой смены
  if (canSwap) {
    if (ui::iconButton("repeat", why.empty() ? std::string("Сменить подкласс") : "Сменить подкласс\n" + why, {.disabled = !why.empty()})) ui::openPopup("swap");
    a.markUi("character.talents.swap");
    if (ui::beginPopup("swap", {.side = ui::Side::Below, .width = 260})) {
      ui::caption("Подкласс");
      for (const SubClass& s : hc.subclasses) {
        ui::IdScope is{i64(s.id)};
        if (ui::listItem(orName(s.name), {.icon = subIcon(hc, s), .hint = std::to_string(s.talents.size()), .selected = s.id == c.subclass})) {
          ui::closePopup();
          askSub(a, c, s.id);
        }
        a.markUi("character.talents.swap." + std::to_string(s.id));
      }
      ui::endPopup();
    }
  }
  if (play && sc) {
    if (ui::iconButton("undo", why.empty() ? std::string("Сбросить таланты") : "Сбросить таланты\n" + why,
                       {.disabled = c.talents.empty() || !why.empty(), .tone = ui::Tone::Danger}))
      askReset(a, cid);
    a.markUi("character.talents.reset");
  }
  if (a.admin()) {
    if (ui::iconButton("book", "Классы героев в справочнике")) a.openEditor("catalogs", 10);
    a.markUi("character.talents.catalog");
  }
}

}  // namespace

// ================================================================ «Сведения»: класс, подкласс и уровень
// Поля «Класс», «Подкласс» и «Уровень» (раздел «Основное» вкладки «Сведения»). Права — по роли: класс и подкласс —
// игровое действие за фракцию героя, уровень — правка мира.
void classFields(App& a, const Character& c, bool /*ro*/) {
  const World w = a.world();
  const Id cid = c.id;
  const bool play = a.canPlay(c.faction);
  // Игрок: смена (не первый выбор) — не у недоступного героя и не чаще срока; причина — в подсказке.
  const std::string why = respecWhy(a, c);
  ui::prop("Класс", "hero-class");
  {
    std::vector<const HeroClass*> list;
    for (const HeroClass& k : w.catalogs->classes) list.push_back(&k);
    std::vector<std::string> hints;
    for (const HeroClass* k : list) hints.push_back(std::to_string(k->subclasses.size()));
    std::vector<ui::Option> opts;
    for (size_t i = 0; i < list.size(); i++) opts.push_back(ui::Option{list[i]->name, classIcon(*list[i]), Color(0, 0, 0, 0), hints[i]});
    int idx = -1;
    for (size_t i = 0; i < list.size(); i++)
      if (list[i]->id == c.heroClass) idx = int(i);
    const bool locked = c.heroClass && !why.empty();
    const std::string tip = std::string("Класс героя: подклассы и их деревья талантов") + (locked ? "\n" + why : std::string());
    if (ui::combo("heroClass", idx, std::span<const ui::Option>(opts),
                  {.placeholder = "Без класса", .noneLabel = "Без класса", .search = 1, .icon = "hero-class", .disabled = !play || locked, .popupWidth = 280,
                   .tooltip = tip})) {
      const Id cls = idx >= 0 && idx < int(list.size()) ? list[size_t(idx)]->id : 0;
      askClass(a, c, cls);
    }
    a.markUi("character.class");
  }
  // Подкласс выбранного класса: его дерево талантов.
  if (const HeroClass* hc = w.heroClass(c.heroClass)) {
    ui::prop("Подкласс", "talent");
    std::vector<std::string> hints;
    for (const SubClass& s : hc->subclasses) hints.push_back(std::to_string(s.talents.size()));
    std::vector<ui::Option> opts;
    int idx = -1;
    for (size_t i = 0; i < hc->subclasses.size(); i++) {
      opts.push_back(ui::Option{hc->subclasses[i].name, subIcon(*hc, hc->subclasses[i]), Color(0, 0, 0, 0), hints[i]});
      if (hc->subclasses[i].id == c.subclass) idx = int(i);
    }
    const bool locked = c.subclass && !why.empty();
    const std::string tip = std::string("Подкласс: своя ветка талантов") + (locked ? "\n" + why : std::string());
    if (ui::combo("subclass", idx, std::span<const ui::Option>(opts),
                  {.placeholder = hc->subclasses.empty() ? "Нет подклассов" : "Не выбран", .noneLabel = "Не выбран", .search = 1, .icon = "talent",
                   .disabled = !play || hc->subclasses.empty() || locked, .popupWidth = 280, .tooltip = tip})) {
      const Id sub = idx >= 0 && idx < int(hc->subclasses.size()) ? hc->subclasses[size_t(idx)].id : 0;
      askSub(a, c, sub);
    }
    a.markUi("character.subclass");
  }
  ui::prop("Уровень", "star");
  {
    int level = std::clamp(c.level, 1, schema::kMaxHeroLevel);
    const int spent = rules::talentPointsSpent(w, cid);
    if (ui::numberField("level", level, {.min = 1, .max = double(schema::kMaxHeroLevel), .steppers = true, .disabled = !a.canEdit(),
                                         .tooltip = "Уровень героя 1…60: одно очко талантов за уровень"}) &&
        level != c.level) {
      if (level >= spent) {
        a.act("Уровень героя", [&](Tx& tx) { rules::setHeroLevel(tx, cid, level); }, {.coalesce = "herolevel:" + std::to_string(cid)});
      } else if (!ui::lastItem().active) {
        const int lv = level;
        a.confirmAct("Снизить уровень?",
                     "Вложено " + pts(spent) + " талантов — на уровне " + std::to_string(lv) + " их не хватит. Таланты будут сброшены.",
                     "Сбросить и снизить", "Уровень героя", [cid, lv](Tx& tx) { rules::setHeroLevel(tx, cid, lv, true); });
      }
    }
    a.markUi("character.level");
  }
}

// ================================================================ «Возвысить до Лича»
// Кнопка (видна только с «Некромантом», ещё не личу): игровое действие за государство героя.
void lichButton(App& a, const Character& c, bool /*ro*/) {
  if (!rules::lichOffered(a.world(), c.id)) return;
  const Id cid = c.id;
  if (ui::button("Возвысить до Лича", {.variant = ui::Variant::Secondary, .icon = "lich", .fill = true, .disabled = !a.canPlay(c.faction),
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
    const bool can = a.canPlay(c->faction);
    if (ui::button("Возвысить", {.variant = ui::Variant::Primary, .icon = "lich", .disabled = !problems.empty() || !can, .isDefault = true})) {
      const Id h = hero, rr = relic;
      const std::string name = w.characterName(h);
      if (a.play("Возвысить до Лича", [&](Tx& tx) { rules::ascendLich(tx, h, rr); })) {
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
                     const Character* c = a.world().character(arg);
                     if (!c || !a.canPlay(c->faction)) return nullptr;
                     auto d = std::make_unique<LichDlg>();
                     d->hero = arg;
                     return d;
                   }});

// ================================================================ вкладка «Таланты»
void drawTalents(App& a, Id cid) {
  const World w = a.world();
  const Character* c = w.character(cid);
  if (!c) return;
  const bool play = a.canPlay(c->faction);
  const HeroClass* hc = w.heroClass(c->heroClass);
  if (!hc) {
    if (!play || w.catalogs->classes.empty()) {
      ui::spacer(16);
      ui::emptyState("hero-class", "Класс не выбран");
      return;
    }
    classPicker(a, w, *c, play);
    return;
  }
  const SubClass* sc = rules::heroSubClass(w, cid);
  talentsHead(a, *hc, sc, *c, play);
  if (!sc) {
    ui::spacer(4);
    if (hc->subclasses.empty()) {
      ui::spacer(12);
      ui::emptyState("talent", "У класса нет подклассов");
      return;
    }
    subPicker(a, *hc, *c, play);
    return;
  }
  const int total = rules::talentPoints(w, cid), spent = rules::talentPointsSpent(w, cid), free = std::max(0, total - spent);
  if (ui::avail().w >= 480) {
    ui::Row tiles({ui::fr(1), ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(std::to_string(spent), "Вложено", {.icon = "talent", .tone = ui::Tone::Accent, .tooltip = ranksOf(c->talents.size()) + " в дереве"});
    a.markUi("character.talents.spent");
    ui::stat(std::to_string(total), "Всего", {.icon = "star", .tone = ui::Tone::Info, .tooltip = "Одно очко за уровень героя"});
    a.markUi("character.talents.points");
    ui::stat(std::to_string(free), "Свободно", {.icon = "plus", .tone = free ? ui::Tone::Success : ui::Tone::Neutral});
    a.markUi("character.talents.free");
  } else {
    // Узкая панель: «вложено / всего» и свободные — двумя плитками.
    ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
    ui::stat(std::to_string(spent) + " / " + std::to_string(total), "Вложено",
             {.icon = "talent", .tone = ui::Tone::Accent, .tooltip = ranksOf(c->talents.size()) + " в дереве · одно очко за уровень героя"});
    a.markUi("character.talents.points");
    ui::stat(std::to_string(free), "Свободно", {.icon = "plus", .tone = free ? ui::Tone::Success : ui::Tone::Neutral});
    a.markUi("character.talents.free");
  }
  if (sc->talents.empty()) {
    ui::spacer(8);
    ui::emptyState("talent", "В дереве подкласса пока нет талантов");
    return;
  }
  talents::Options o;
  o.cls = hc;
  o.sub = sc;
  o.hero = cid;
  o.interactive = play;
  o.mark = "character.tree";
  const talents::Events ev = talents::tree(a, o);
  if (play && ev.clicked) {
    std::string why;
    const Id t = ev.clicked;
    if (rules::canLearnTalent(w, cid, t, &why)) a.play("Изучить талант", [&](Tx& tx) { rules::learnTalent(tx, cid, t); });
    else a.toast(why, ToastKind::Warning, "lock");
  }
  if (play && ev.rightClicked && talentRank(c->talents, ev.rightClicked) > 0) {
    const Id t = ev.rightClicked;
    std::string why = respecWhy(a, *c);   // снятие ранга игроком — как смена талантов (не чаще срока)
    const bool player = a.player();
    if (why.empty() && rules::canUnlearnTalent(w, cid, t, &why)) a.play("Снять ранг таланта", [&](Tx& tx) { rules::unlearnTalent(tx, cid, t, player); });
    else a.toast(why, ToastKind::Warning, "lock");
  }
}

// Свободные очки талантов — число на значке вкладки (только тому, кто может их вложить).
int talentsBadge(App& a, Id cid) {
  const World& w = a.world();
  const Character* c = w.character(cid);
  if (!c || !rules::heroSubClass(w, cid) || !a.canPlay(c->faction)) return 0;
  return std::max(0, rules::talentPoints(w, cid) - rules::talentPointsSpent(w, cid));
}

TabReg tabTalents({"character.talents", "talent", "Таланты", 15, SelType::Character, nullptr, drawTalents, talentsBadge});

}  // namespace
}  // namespace rg::app::chars
