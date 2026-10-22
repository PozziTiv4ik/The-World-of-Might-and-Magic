// Сценарии окна модификаторов (ТЗ «Модификаторы»): встроенные 1.1–1.26 видны всегда — шаблон до первой правки,
// первая правка создаёт одну запись мира (Ctrl+Z возвращает шаблон), встроенные не удаляются, автоматические
// помечены и вручную не добавляются; «Где действует», срок по умолчанию, новые эффекты «Время исследования
// технологий» и «Верность войска за ход» (эффект войск), добавление войску со сроком по умолчанию; модификаторы
// археологических групп и генерация за ход.
#include "tests/test_app_editors_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::editors;
namespace ed = rg::apptest::editors;

namespace {

constexpr Id kTemplateIds = 0xFFFF0000u;   // выбор шаблона без записи мира (editors/modifiers.cpp)

const Modifier* rec(const World& w, Id id) { return w.modifier(id); }

}  // namespace

TEST(app_editors_modifiers_builtin) {
  Harness h("editors_mods_builtin");
  h.demo();
  h.dropToasts();
  h->openEditor("modifiers", 0);
  quick(h);
  // Все встроенные (ТЗ 1.1–1.26, 7 модификаторов доработок 2026-10-10, 8 модификаторов заговоров и 18 модификаторов
  // уровней построек 2026-10-22) — в списке, даже без записей мира.
  CHECK_EQ(schema::builtinModifiers().size(), size_t(59));
  for (const Modifier& m : schema::builtinModifiers()) CHECK_MSG(h->uiRect("modifiers.builtin." + m.key) != nullptr, m.key);
  // Список сгруппирован по виду: для провинций, глобальные, для армий, для героев, для археологических групп, везде
  // (сверху вниз).
  {
    float y = -1;
    for (const char* g : {"province", "faction", "army", "hero", "arch", "any"}) {
      const RectF* r = h->uiRect(std::string("modifiers.group.") + g);
      CHECK_MSG(r != nullptr, g);
      if (r) CHECK_MSG(r->y > y, g);
      if (r) y = r->y;
    }
  }
  // «Патриотизм» — шаблон: записи мира нет, удалить нельзя.
  CHECK_EQ(rules::builtinModId(h->world(), schema::mod::Patriotism), Id(0));
  CHECK(clickRevealed(h, "modifiers.builtin.patriotism", "modifiers.list"));
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
  CHECK(clickRevealed(h, "modifiers.builtin.ravaged", "modifiers.list"));
  h.key(Key::D, ctrl());
  quick(h);
  const Modifier* copy = rec(h->world(), h->ui.editorArg);
  CHECK(copy && copy->key.empty() && copy->has(Fx::ResourcePct));
  CHECK(h->uiRect("modifiers.delete") != nullptr);
  // Автоматический «Слабый контроль»: помечен, действует у государств демо-мира (три советника), вручную не добавляется.
  CHECK(clickRevealed(h, "modifiers.builtin.weakControl", "modifiers.list"));
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

// Список модификаторов сгруппирован по виду: группы сворачиваются (число — в заголовке), поиск их раскрывает, выбор
// модификатора свёрнутой группы извне раскрывает её, ↑↓ идут по видимым строкам.
TEST(app_editors_modifiers_groups) {
  Harness h("editors_mods_groups");
  h.demo();
  h.dropToasts();
  h->openEditor("modifiers", 0);
  quick(h);
  // Шаблон или запись мира встроенного модификатора — ID строки списка.
  auto entryId = [&](const char* key) -> Id {
    const auto& tpl = schema::builtinModifiers();
    for (size_t i = 0; i < tpl.size(); i++)
      if (tpl[i].key == key) {
        const Id rid = rules::builtinModId(h->world(), key);
        return rid ? rid : kTemplateIds + Id(i);
      }
    return 0;
  };
  // Группы раскрыты; «Живой» — в «Для героев», под её заголовком.
  {
    const RectF* g = h->uiRect("modifiers.group.hero");
    const RectF* r = h->uiRect("modifiers.builtin.living");
    CHECK(g && r && r->y > g->y);
  }
  // Свернуть «Для героев»: строк группы нет, заголовок остаётся.
  CHECK(clickRevealed(h, "modifiers.group.hero", "modifiers.list"));
  CHECK(h->uiRect("modifiers.builtin.living") == nullptr);
  CHECK(h->uiRect("modifiers.builtin.dead") == nullptr);
  CHECK(h->uiRect("modifiers.group.hero") != nullptr);
  shotClean(h, "editors_mods_groups");
  // Поиск раскрывает: «Живой» найден в свёрнутой группе.
  h.key(Key::F, ctrl());
  h.type("Живой");
  quick(h);
  CHECK(h->uiRect("modifiers.builtin.living") != nullptr);
  CHECK(h->uiRect("modifiers.group.hero") != nullptr);
  // Поиск выбрал «Живой»; поиск очищен — выбранный виден (его группа раскрыта).
  CHECK_EQ(h->ui.editorArg, entryId(schema::mod::Living));
  h.key(Key::A, ctrl());
  h.key(Key::Backspace);
  h.key(Key::Escape);
  quick(h);
  CHECK(h->uiRect("modifiers.builtin.living") != nullptr);
  CHECK(h->uiRect("modifiers.selected") != nullptr);
  // Снова свернуть: выбранный остаётся выбранным, но строки не видно.
  CHECK(clickRevealed(h, "modifiers.group.hero", "modifiers.list"));
  CHECK(h->uiRect("modifiers.builtin.living") == nullptr);
  CHECK_EQ(h->ui.editorArg, entryId(schema::mod::Living));
  // Выбор модификатора свёрнутой группы извне раскрывает её.
  const Id dead = entryId(schema::mod::Dead);
  CHECK(dead != 0);
  h->openEditor("modifiers", dead);
  quick(h);
  CHECK(h->uiRect("modifiers.builtin.dead") != nullptr);
  CHECK(h->uiRect("modifiers.selected") != nullptr);
  // ↑↓ — по видимым строкам: «Глобальные» свёрнуты, после последнего «Для провинций» идёт первый «Для армий».
  CHECK(clickRevealed(h, "modifiers.group.faction", "modifiers.list"));
  const char* lastProv = nullptr;
  const char* firstArmy = nullptr;
  for (const Modifier& m : schema::builtinModifiers()) {
    if (m.kind == ModKind::Province) lastProv = m.key.c_str();
    if (m.kind == ModKind::Army && !firstArmy) firstArmy = m.key.c_str();
  }
  CHECK(lastProv && firstArmy);
  bool ownArmy = false, ownProv = false;
  h->world().modifiers.each([&](const Modifier& m) {
    if (m.key.empty() && m.kind == ModKind::Army) ownArmy = true;
    if (m.key.empty() && m.kind == ModKind::Province) ownProv = true;
  });
  CHECK(!ownArmy && !ownProv);   // в демо-мире свои модификаторы — «Везде»
  if (lastProv && firstArmy) {
    h->openEditor("modifiers", entryId(lastProv));
    quick(h);
    h.key(Key::Down);
    CHECK_EQ(h->ui.editorArg, entryId(firstArmy));
    h.key(Key::Up);
    CHECK_EQ(h->ui.editorArg, entryId(lastProv));
  }
}

// ---------------------------------------------------------------- модификаторы
TEST(app_editors_modifiers_arch_and_generation) {
  Harness h("econ2_mods", 1600, 1000);
  h.demo();
  h.dropToasts();
  h->openEditor("modifiers", 0);
  ed::quick(h);
  // Встроенные модификаторы групп («Ранение в ходе исследования», «Заражение чумой») — в своей группе списка.
  CHECK(h->uiRect("modifiers.group.arch") != nullptr);
  // Новый модификатор → вид «Для археологических групп»: только эффекты групп, без генерации.
  CHECK(h.clickUi("modifiers.new"));
  ed::quick(h);
  const Id mid = h->ui.editorArg;
  CHECK(h->world().modifier(mid) != nullptr);
  h.retype("Опытные проводники");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK(ed::comboStep(h, "modifiers.kind", 5));   // «Везде» → «Для археологических групп»
  CHECK(h->world().modifier(mid) && h->world().modifier(mid)->kind == ModKind::ArchGroup);
  CHECK(h->uiRect("modifiers.arch") != nullptr);
  CHECK(h->uiRect("modifiers.local") == nullptr);
  CHECK(h->uiRect("modifiers.global") == nullptr);
  CHECK(h->uiRect("modifiers.gen") == nullptr);
  CHECK(ed::clickRevealed(h, "modifiers.fx.archSuccessPct.on"));
  ed::quick(h);
  CHECK(h->world().modifier(mid)->has(Fx::ArchSuccessPct));
  CHECK(ed::enterValue(h, "modifiers.fx.archSuccessPct.value", "9"));   // предел ТЗ: +1…+5 %
  CHECK_NEAR(h->world().modifier(mid)->fx[size_t(Fx::ArchSuccessPct)], 5.0, 1e-9);
  CHECK(ed::clickRevealed(h, "modifiers.fx.archUpkeepPct.on"));
  ed::quick(h);
  CHECK(ed::enterValue(h, "modifiers.fx.archUpkeepPct.value", "-30"));
  CHECK_NEAR(h->world().modifier(mid)->fx[size_t(Fx::ArchUpkeepPct)], -30.0, 1e-9);
  shotClean(h, "econ2_mods_arch");
  // Вид «Для провинций»: эффекты групп остаются (не действуют), генерация за ход — эссенции и ресурсы.
  CHECK(ed::comboStep(h, "modifiers.kind", -4));   // → «Для провинций»
  CHECK(h->world().modifier(mid)->kind == ModKind::Province);
  CHECK(h->uiRect("modifiers.local") != nullptr);
  CHECK(ed::reveal(h, "modifiers.gen"));
  const Id death = test::idByName(h->world().catalogs->essences, "Эссенция смерти");
  CHECK(death != 0);
  CHECK(ed::pickInCombo(h, "modifiers.gen.ess.add", "смерти"));
  CHECK(h->world().modifier(mid)->essGen.count(death) == 1);
  CHECK(ed::enterValue(h, "modifiers.gen.ess." + std::to_string(death), "12,5"));
  CHECK_NEAR(h->world().modifier(mid)->essGen.at(death), 12.5, 1e-9);
  const Id coal = test::idByName(h->world().catalogs->resources, "Уголь");
  CHECK(coal != 0);
  CHECK(ed::pickInCombo(h, "modifiers.gen.res.add", "Уголь"));
  CHECK(h->world().modifier(mid)->resGen.count(coal) == 1);
  shotClean(h, "econ2_mods_generation");
  // Генерация действует: модификатор в провинции государства — эссенция владельцу за ход.
  Id pid = 0, owner = 0;
  h->world().provinces.each([&](const Province& p) {
    const Faction* f = h->world().faction(p.owner);
    if (!pid && !p.sea && f && f->isState()) {
      pid = p.id;
      owner = p.owner;
    }
  });
  CHECK(pid && owner);
  const double before = rules::calc(h->world())->faction(owner)->essences.at(death).generation;
  CHECK(h->act("Модификатор провинции", [&](Tx& tx) { rules::addModifier(tx, rules::ModTarget::Province, pid, mid); }));
  CHECK_NEAR(rules::calc(h->world())->faction(owner)->essences.at(death).generation, before + 12.5, 1e-9);
  // Снова вид групп — прочие эффекты и генерация снимаются.
  CHECK(ed::comboStep(h, "modifiers.kind", 4));
  CHECK(h->world().modifier(mid)->kind == ModKind::ArchGroup);
  CHECK(h->world().modifier(mid)->essGen.empty());
  CHECK(h->world().modifier(mid)->resGen.empty());
  CHECK(h->world().modifier(mid)->has(Fx::ArchSuccessPct));
  // «Некромант» (шаблон): генерация по умолчанию — эссенция смерти +10.
  CHECK(ed::clickRevealed(h, "modifiers.builtin.necromancer", "modifiers.list"));
  ed::quick(h);
  CHECK(ed::reveal(h, "modifiers.gen.ess." + std::to_string(death)));
  shotClean(h, "econ2_mods_necromancer");
}
