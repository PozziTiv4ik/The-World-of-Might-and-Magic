// Regnum — вкладка «Герои» (ТЗ 1.b.iv: список значимых героев государства): портрет, имя, титул или состояние
// («Мертв» с местом захоронения, «В плену» с пленившим государством — ТЗ «Механика героев»), содержание за ход
// (расход «специалисты», золото до тысячных), отметка «в войске»; воскрешение погибшего (окно «hero.resurrect»);
// пленники государства — чужие герои в плену и чьи они; новый герой, назначение персонажа героем, снятие, переход.
#include "app/panels/faction_common.h"

namespace rg::app {
namespace {

using namespace fac;

std::string charName(const Character& c) { return c.name.empty() ? std::string("Без имени") : c.name; }

void newHero(App& a, Id id) {
  Id cid = 0;
  if (!a.act("Новый герой", [&](Tx& tx) {
        cid = rules::createCharacter(tx, id, "Новый герой");
        Character& c = tx.character(cid);
        c.hero = true;
        c.title = "Герой";
      }))
    return;
  a.toast("Добавлен новый герой", ToastKind::Success, "hero", "Открыть", [cid](App& x) { x.select(SelType::Character, cid); });
}

// Пленники государства (ТЗ «Механика героев», п.1.3): чужие герои, взятые в плен, и их государства.
void captivesSection(App& a, const World& w, Id id) {
  std::vector<Id> list = rules::captivesOf(w, id);
  if (list.empty()) return;
  std::sort(list.begin(), list.end(), [&](Id x, Id y) { return compareRu(w.characterName(x), w.characterName(y)) < 0; });
  ui::Section sec("Пленники", "shackles", {.badge = std::to_string(list.size())});
  a.markUi("captives.section");
  if (!sec) return;
  for (Id cid : list) {
    const Character* c = w.character(cid);
    if (!c) continue;
    ui::IdScope sc{i64(cid)};
    ui::Row row({ui::px(32), ui::fr(1), ui::fr(1), ui::px(26)}, 36, 8);
    ui::avatar(charName(*c), {.image = portraitOf(*c), .size = 28});
    ui::label(charName(*c), {.font = ui::Font::Strong, .tooltip = c->title.empty() ? std::string_view("Пленник") : std::string_view(c->title)});
    if (c->faction) w::factionChip(c->faction);
    else ui::label("Без фракции", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    if (ui::iconButton("arrow-right", "Открыть персонажа", {.size = ui::Size::Small})) a.select(SelType::Character, cid);
    a.markUi("captives.row." + std::to_string(cid));
  }
}

void drawHeroes(App& a, Id id) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(id);
  if (!f) return;
  const ui::Theme& th = ui::theme();
  const bool ro = a.readOnly();
  std::vector<const Character*> heroes, candidates;
  w.characters.each([&](const Character& c) {
    if (c.faction == id && c.hero) heroes.push_back(&c);
    else if (!c.hero && (c.faction == id || c.faction == 0)) candidates.push_back(&c);
  });
  std::sort(heroes.begin(), heroes.end(), [](const Character* x, const Character* y) { return compareRu(x->name, y->name) < 0; });
  std::sort(candidates.begin(), candidates.end(), [id](const Character* x, const Character* y) {
    if ((x->faction == id) != (y->faction == id)) return x->faction == id;
    return compareRu(x->name, y->name) < 0;
  });
  double upkeep = 0;
  int inArmy = 0, dead = 0, captive = 0;
  for (const Character* c : heroes) {
    upkeep += std::max(0.0, c->upkeep);
    if (armyOfCharacter(w, c->id)) inArmy++;
    if (rules::characterHas(w, c->id, schema::mod::Dead)) dead++;
    else if (rules::characterHas(w, c->id, schema::mod::Captive)) captive++;
  }
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 10);
    std::string tip = "В войсках и флотах: " + std::to_string(inArmy);
    if (dead) tip += "\nПогибли: " + std::to_string(dead);
    if (captive) tip += "\nВ плену: " + std::to_string(captive);
    ui::stat(fmtInt(i64(heroes.size())), plural(i64(heroes.size()), "герой", "героя", "героев"), {.icon = "hero", .tone = ui::Tone::Accent, .tooltip = tip});
    ui::stat(money(upkeep), "Содержание за ход", {.icon = "coins", .tone = ui::Tone::Warning, .tooltip = "Входит в расход «специалисты»"});
  }
  ui::spacer(2);
  {
    ui::Section sec("Герои", "hero", {.badge = heroes.empty() ? std::string() : std::to_string(heroes.size()), .actionIcon = ro ? nullptr : "user-plus",
                                      .actionTooltip = "Новый герой"});
    if (sec.action()) newHero(a, id);
    if (sec) {
      if (heroes.empty()) {
        ui::emptyState("hero", "Значимых героев пока нет.");
      } else {
        ui::Column cols[] = {{"Герой", nullptr, ui::fr(1, 120), ui::Align::Left, true},
                             {{}, "coins", ui::px(66), ui::Align::Left, true, "Содержание за ход"},
                             {{}, "army", ui::px(40), ui::Align::Center, true, "В войске или флоте"},
                             {{}, nullptr, ui::px(70)}};
        ui::Table t("heroes", cols, int(heroes.size()), {.rowHeight = 46, .selectable = false});
        t.sort([&](int x, int y, int col) {
          const Character& A = *heroes[size_t(x)];
          const Character& B = *heroes[size_t(y)];
          if (col == 1) return A.upkeep < B.upkeep ? -1 : A.upkeep > B.upkeep ? 1 : 0;
          if (col == 2) return int(armyOfCharacter(w, A.id) != 0) - int(armyOfCharacter(w, B.id) != 0);
          return compareRu(A.name, B.name);
        });
        for (int i : t) {
          const Character& c = *heroes[size_t(i)];
          const Id cid = c.id;
          const bool isDead = rules::characterHas(w, cid, schema::mod::Dead);
          const std::string state = w::heroState(w, c);
          ui::IdScope sc{i64(cid)};
          t.cell();
          {
            bool ruler = f->ruler == cid;
            ui::Row rr({ui::px(38), ui::fr(1)}, 40, 8);
            std::string nm = charName(c);
            ui::avatar(nm, {.image = portraitOf(c), .size = 32, .ring = ruler, .tooltip = ruler ? "Правитель" : ""});
            if (isDead) {   // погибший — приглушённый портрет
              RectF ar = ui::lastItem().rect;
              ui::draw::circle(ar.cx(), ar.cy(), 16, th.surface1.alpha(0.55f));
            }
            ui::Group g(0, 0);
            const std::string tip = (c.title.empty() ? nm : nm + " · " + c.title) + (state.empty() ? std::string() : "\n" + state);
            ui::label(nm, {.font = ui::Font::Strong, .ink = isDead ? ui::Ink::Dim : ui::Ink::Normal, .tooltip = tip});
            if (!state.empty())
              ui::label(state, {.font = ui::Font::Small, .ink = isDead ? ui::Ink::Danger : ui::Ink::Warning, .icon = isDead ? "skull" : "shackles"});
            else
              ui::label(c.title.empty() ? std::string("Без титула") : c.title, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
          }
          t.cell();
          double up = c.upkeep;
          if (ui::numberField("upkeep", up, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .disabled = ro, .tooltip = "Содержание за ход"}))
            a.act("Содержание героя", [&](Tx& tx) { tx.character(cid).upkeep = std::max(0.0, up); }, {.coalesce = "hero.upkeep:" + std::to_string(cid)});
          if (Id army = armyOfCharacter(w, cid)) {
            t.cell();
            const Army* ar = w.army(army);
            std::string an = ar && !ar->name.empty() ? ar->name : std::string(ar && ar->isFleet() ? "Флот" : "Войско");
            ui::iconColored(ar && ar->isFleet() ? "fleet" : "army", th.accent, 16, (ar && ar->commander == cid ? "Командует: " : "В составе: ") + an + " — показать");
            if (ui::lastItem().hovered) ui::setCursor(platform::Cursor::Hand);
            if (ui::lastItem().clicked) a.select(SelType::Army, army, true);
            a.markUi("heroes.army." + std::to_string(i));
          } else {
            t.text("—", ui::Ink::Muted);
          }
          t.cell();
          {
            ui::HStack hs(24, ui::Align::Right, 4);
            if (isDead) {
              if (ui::iconButton("sparkles", "Воскресить", {.size = ui::Size::Small, .disabled = ro, .tone = ui::Tone::Accent}))
                a.openDialog("hero.resurrect", cid);
              a.markUi("heroes.resurrect." + std::to_string(cid));
            } else {
              if (ui::iconButton("arrow-right", "Открыть персонажа", {.size = ui::Size::Small})) a.select(SelType::Character, cid);
            }
            if (ui::iconButton("close", "Убрать из героев", {.size = ui::Size::Small, .disabled = ro, .tone = ui::Tone::Danger})) {
              std::string nm = charName(c);
              if (a.act("Убрать из героев", [&](Tx& tx) { tx.character(cid).hero = false; }))
                a.toast(nm + " больше не в списке героев", ToastKind::Info, "hero", "Отменить", [](App& x) { x.undo(); });
            }
            a.markUi("heroes.remove." + std::to_string(i));
          }
        }
        if (t.footer()) {
          t.text("Итого");
          t.text(money(upkeep));
          t.text({});
          t.text({});
        }
      }
      if (!ro) {
        ui::spacer(2);
        ui::Row r({ui::fr(1), ui::px(132)}, 30, 8);
        if (!candidates.empty()) {
          std::vector<std::string> labels, hints;
          std::vector<ui::Option> opts;
          for (const Character* c : candidates) {
            labels.push_back(charName(*c));
            hints.push_back(c->faction == id ? c->title : std::string("без фракции"));
          }
          for (size_t k = 0; k < candidates.size(); k++) opts.push_back(ui::Option{labels[k], "character", Color(0, 0, 0, 0), hints[k]});
          int pick = -1;
          if (ui::combo("assign", pick, std::span<const ui::Option>(opts), {.placeholder = "Назначить героем…", .icon = "hero"}) && pick >= 0) {
            Id cid = candidates[size_t(pick)]->id;
            a.act("Назначить героем", [&](Tx& tx) {
              Character& c = tx.character(cid);
              c.hero = true;
              c.faction = id;
            });
          }
          a.markUi("heroes.assign");
        } else {
          ui::label("Все персонажи фракции — герои", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
        }
        if (ui::button("Новый герой", {.icon = "user-plus", .fill = true})) newHero(a, id);
        a.markUi("heroes.new");
      }
    }
  }
  if (f->isState()) captivesSection(a, w, id);
}

// Точка на значке вкладки — у государства есть пленники.
int captivesBadge(App& a, Id id) { return rules::captivesOf(a.world(), id).empty() ? 0 : -1; }

TabReg tab({kTabHeroes, "hero", "Герои", 60, SelType::Faction, nullptr, drawHeroes, captivesBadge});

}  // namespace
}  // namespace rg::app
