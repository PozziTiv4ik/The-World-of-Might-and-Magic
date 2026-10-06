// Сценарии окна модификаторов (ТЗ «Модификаторы»): встроенные 1.1–1.26 видны всегда — шаблон до первой правки,
// первая правка создаёт одну запись мира (Ctrl+Z возвращает шаблон), встроенные не удаляются, автоматические
// помечены и вручную не добавляются; «Где действует», срок по умолчанию, новые эффекты «Время исследования
// технологий» и «Верность войска за ход» (эффект войск), добавление войску со сроком по умолчанию.
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;

namespace {

constexpr Id kTemplateIds = 0xFFFF0000u;   // выбор шаблона без записи мира (editors/modifiers.cpp)

const Modifier* rec(const World& w, Id id) { return w.modifier(id); }

// Короткий список без поиска: открыть, сдвинуться стрелками от текущего пункта на steps, Enter.
bool comboStep(Harness& h, const std::string& name, int steps) {
  if (!clickRevealed(h, name)) return false;
  for (int i = 0; i < std::abs(steps); i++) h.key(steps > 0 ? Key::Down : Key::Up);
  h.key(Key::Enter);
  quick(h);
  return true;
}

}  // namespace

TEST(app_editors_modifiers_builtin) {
  Harness h("editors_mods_builtin");
  h.demo();
  h.dropToasts();
  h->openEditor("modifiers", 0);
  quick(h);
  // Все встроенные (ТЗ 1.1–1.26) — в списке, даже без записей мира.
  CHECK_EQ(schema::builtinModifiers().size(), size_t(26));
  for (const Modifier& m : schema::builtinModifiers()) CHECK_MSG(h->uiRect("modifiers.builtin." + m.key) != nullptr, m.key);
  CHECK(h->uiRect("modifiers.builtins") != nullptr);
  // «Патриотизм» — шаблон: записи мира нет, удалить нельзя.
  CHECK_EQ(rules::builtinModId(h->world(), schema::mod::Patriotism), Id(0));
  CHECK(clickRevealed(h, "modifiers.builtin.patriotism"));
  CHECK(h->ui.editorArg >= kTemplateIds);
  CHECK(h->uiRect("modifiers.builtinTag") != nullptr);
  CHECK(h->uiRect("modifiers.autoTag") == nullptr);
  CHECK(h->uiRect("modifiers.delete") == nullptr);
  shotClean(h, "editors_mods_template");
  // Первая правка — запись мира по шаблону, окно переходит на неё.
  CHECK(enterValue(h, "modifiers.fx.loyaltyPerTurn.value", "7"));
  Id rid = rules::builtinModId(h->world(), schema::mod::Patriotism);
  CHECK(rid != 0);
  CHECK_EQ(h->ui.editorArg, rid);
  CHECK(rec(h->world(), rid) && rec(h->world(), rid)->kind == ModKind::Army);
  CHECK_NEAR(rec(h->world(), rid) ? rec(h->world(), rid)->fx[size_t(Fx::LoyaltyPerTurn)] : 0, 7.0, 1e-9);
  // Вторая правка — та же запись; срок по умолчанию.
  CHECK(enterValue(h, "modifiers.duration", "4"));
  CHECK_EQ(rules::builtinModId(h->world(), schema::mod::Patriotism), rid);
  CHECK_EQ(rec(h->world(), rid) ? rec(h->world(), rid)->duration : -1, 4);
  int n = 0;
  h->world().modifiers.each([&](const Modifier& m) { n += m.key == schema::mod::Patriotism ? 1 : 0; });
  CHECK_EQ(n, 1);
  CHECK(h->uiRect("modifiers.delete") == nullptr);
  // Ctrl+Z дважды — записи снова нет, действует шаблон; выделение осталось на «Патриотизме».
  h.key(Key::Z, ctrl());
  h.key(Key::Z, ctrl());
  quick(h);
  CHECK_EQ(rules::builtinModId(h->world(), schema::mod::Patriotism), Id(0));
  CHECK(h->ui.editorArg >= kTemplateIds);
  {
    const RectF* selRow = h->uiRect("modifiers.selected");
    const RectF* patRow = h->uiRect("modifiers.builtin.patriotism");
    CHECK(selRow && patRow && std::fabs(selRow->y - patRow->y) < 1);
  }
  // Копия встроенного — свой модификатор без ключа.
  CHECK(clickRevealed(h, "modifiers.builtin.ravaged"));
  h.key(Key::D, ctrl());
  quick(h);
  const Modifier* copy = rec(h->world(), h->ui.editorArg);
  CHECK(copy && copy->key.empty() && copy->has(Fx::ResourcePct));
  CHECK(h->uiRect("modifiers.delete") != nullptr);
  // Автоматический «Слабый контроль»: помечен, действует у государств демо-мира (три советника), вручную не добавляется.
  CHECK(clickRevealed(h, "modifiers.builtin.weakControl"));
  CHECK(h->uiRect("modifiers.autoTag") != nullptr);
  CHECK(h->uiRect("modifiers.addFaction") == nullptr);
  CHECK(h->uiRect("modifiers.addProvince") == nullptr);
  CHECK(reveal(h, "modifiers.usage"));
  shotClean(h, "editors_mods_auto");
  // Раздел «Справочники» открывает модификаторы (последний открытый редактор раздела); повторно — к карте.
  h->closeEditor();
  quick(h);
  CHECK(h.clickUi("section.reference"));
  quick(h);
  CHECK_EQ(h->ui.editor, std::string("modifiers"));
  CHECK(h.clickUi("section.reference"));
  quick(h);
  CHECK(h->ui.editor.empty());
}

TEST(app_editors_modifiers_effects_kind_term) {
  Harness h("editors_mods_effects");
  h.demo();
  h.dropToasts();
  // Войско с узнаваемым названием (выбор поиском).
  Id army = 0;
  h->world().armies.each([&](const Army& x) {
    if (!army && !x.isFleet()) army = x.id;
  });
  CHECK(army != 0);
  CHECK(h->act("Название", [&](Tx& tx) { rules::renameArmy(tx, army, "Тестовый легион"); }));
  h->openEditor("modifiers", 0);
  quick(h);
  CHECK(h.clickUi("modifiers.new"));
  quick(h);
  Id mid = h->ui.editorArg;
  CHECK(rec(h->world(), mid) != nullptr);
  h.retype("Железная дисциплина");
  h.key(Key::Enter);
  quick(h);
  // Где действует — «Для армий»; срок по умолчанию — 3 хода.
  CHECK(comboStep(h, "modifiers.kind", 3));   // «Везде» → «Для армий»
  CHECK(rec(h->world(), mid)->kind == ModKind::Army);
  CHECK(enterValue(h, "modifiers.duration", "3"));
  CHECK_EQ(rec(h->world(), mid)->duration, 3);
  // Верность войска за ход (эффект войск) и время исследования технологий (глобальный, предел ТЗ +400 %).
  CHECK(clickRevealed(h, "modifiers.fx.loyaltyPerTurn.on"));
  CHECK(enterValue(h, "modifiers.fx.loyaltyPerTurn.value", "-10"));
  CHECK_NEAR(rec(h->world(), mid)->fx[size_t(Fx::LoyaltyPerTurn)], -10.0, 1e-9);
  CHECK(rec(h->world(), mid)->has(Fx::LoyaltyPerTurn));
  CHECK(clickRevealed(h, "modifiers.fx.researchTimePct.on"));
  CHECK(enterValue(h, "modifiers.fx.researchTimePct.value", "600"));
  CHECK_NEAR(rec(h->world(), mid)->fx[size_t(Fx::ResearchTimePct)], 400.0, 1e-9);
  CHECK(h->uiRect("modifiers.army") != nullptr);
  // Для армий: добавить можно только войску; срок — по умолчанию.
  CHECK(h->uiRect("modifiers.addProvince") == nullptr);
  CHECK(h->uiRect("modifiers.addFaction") == nullptr);
  CHECK(h->uiRect("modifiers.addHero") == nullptr);
  CHECK(pickInCombo(h, "modifiers.addArmy", "Тестовый легион"));
  {
    const Army* a = h->world().army(army);
    CHECK(a && std::find(a->modifiers.begin(), a->modifiers.end(), mid) != a->modifiers.end());
    CHECK(a && a->modTurns.count(mid) && a->modTurns.at(mid) == 3);
  }
  CHECK(rules::armyEffects(h->world(), army)[Fx::LoyaltyPerTurn] <= -10 + 1e-9);
  CHECK(reveal(h, "modifiers.usage"));
  shotClean(h, "editors_mods_army");
  // Снять с войска крестиком — правило dropModifier (срок убирается).
  h.key(Key::Z, ctrl());
  quick(h);
  {
    const Army* a = h->world().army(army);
    CHECK(a && std::find(a->modifiers.begin(), a->modifiers.end(), mid) == a->modifiers.end());
    CHECK(a && !a->modTurns.count(mid));
  }
  // Глобальный модификатор с «Временем исследования» удлиняет исследования государства.
  CHECK(comboStep(h, "modifiers.kind", -1));   // «Для армий» → «Глобальный»
  CHECK(rec(h->world(), mid)->kind == ModKind::Faction);
  Id state = 0, tech = 0;
  h->world().techs.each([&](const Tech& t) {
    const Faction* f = h->world().faction(t.faction);
    if (!tech && f && f->isState()) {
      tech = t.id;
      state = f->id;
    }
  });
  CHECK(tech != 0);
  const int before = rules::researchTurns(h->world(), *h->world().tech(tech));
  CHECK(pickInCombo(h, "modifiers.addFaction", h->world().factionName(state)));
  const int after = rules::researchTurns(h->world(), *h->world().tech(tech));
  CHECK_MSG(after > before, std::to_string(before) + " → " + std::to_string(after));
}
