// Сценарии общего дерева технологий (ТЗ «Доработки», п.6–7): общее дерево в редакторе (создание, «Изучение за» —
// каждое государство изучает его отдельно), общая технология не зависит от уникальной, уникальная может зависеть от
// общей (плашка-ссылка в дереве государства с состоянием для него), раздел «Общие технологии» во вкладке государства.
#include "app/editors/techtree.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::apptest::trees;

namespace {

// Два первых государства мира (по порядку ID).
std::pair<Id, Id> twoStates(const World& w) {
  Id a = 0, b = 0;
  w.factions.each([&](const Faction& f) {
    if (!f.isState()) return;
    if (!a) a = f.id;
    else if (!b) b = f.id;
  });
  return {a, b};
}

std::vector<Id> commonTechs(const World& w) {
  std::vector<Id> r;
  w.techs.each([&](const Tech& t) {
    if (t.faction == 0) r.push_back(t.id);
  });
  return r;
}

bool hasPrereq(const World& w, Id tech, Id pre) {
  const Tech* t = w.tech(tech);
  return t && std::find(t->prereqs.begin(), t->prereqs.end(), pre) != t->prereqs.end();
}

// Общее дерево: «Строительство» → «Архитектура» (2 хода каждая).
std::pair<Id, Id> commonPair(app::App& a) {
  Id t1 = 0, t2 = 0;
  CHECK(a.act("Общие технологии", [&](Tx& tx) {
    t1 = rules::createTech(tx, 0, "Строительство");
    t2 = rules::createTech(tx, 0, "Архитектура");
    tx.tech(t1).turns = 2;
    tx.tech(t2).turns = 2;
    rules::setPrereq(tx, t2, t1, true);
    rules::autoLayout(tx, 0);
  }));
  return {t1, t2};
}

}  // namespace

TEST(app_trees_common_tech_tree) {
  HideTestRegs hide;
  Harness h("trees_common_tech", 1600, 1000);
  h.demo();
  h.dropToasts();
  auto [A, B] = twoStates(h->world());
  CHECK(A && B);
  // Пустое общее дерево: технология с пустого холста — общая (без фракции).
  app::openTechTree(h.a(), 0, 0, A);
  h.settle(40);
  CHECK_EQ(h->ui.editor, std::string("techtree"));
  CHECK_EQ(h->ui.editorArg, Id(0));
  CHECK(h->uiRect("tt.empty.add") != nullptr);
  CHECK(h->uiRect("tt.empty.alt") == nullptr);   // копировать дерево в общее нельзя
  CHECK(h->uiRect("tt.learner") != nullptr);
  shotTrees(h, "trees_common_tech_empty");
  clickQuick(h, "tt.empty.add");
  std::vector<Id> cs = commonTechs(h->world());
  CHECK_EQ(cs.size(), size_t(1));
  const Id t1 = cs.front();
  h.retype("Строительство");
  h.key(Key::Enter);
  h.settle(40);
  CHECK_EQ(h->world().tech(t1)->name, std::string("Строительство"));
  // Вторая — кнопкой «+», условие — первая (список панели свойств).
  clickQuick(h, "tt.add");
  cs = commonTechs(h->world());
  CHECK_EQ(cs.size(), size_t(2));
  const Id t2 = cs.back();
  h.retype("Архитектура");
  h.key(Key::Enter);
  h.settle(40);
  CHECK(pickSide(h, "tt.side.addpre", "Строительство", "tt.canvas"));
  CHECK(hasPrereq(h->world(), t2, t1));
  CHECK(h->act("Сроки и раскладка", [&](Tx& tx) {
    tx.tech(t1).turns = 3;
    tx.tech(t2).turns = 3;
    rules::autoLayout(tx, 0);
  }));
  h.key(Key::Escape);
  h.settle(40);

  // «Галочка» — изучено только за A.
  app::openTechTree(h.a(), 0, t1, A);
  h.settle(40);
  h.key(Key::F);
  h.settle(40);
  clickQuick(h, "tt.check." + std::to_string(t1));
  CHECK(rules::techStudied(h->world(), t1, A));
  CHECK(!rules::techStudied(h->world(), t1, B));
  // Изучение за B: в обзоре — список государств; у B «Строительство» не изучено — начать исследование.
  RectF cv = rectOf(h.a(), "tt.canvas");
  h.click(cv.x + 30, cv.bottom() - 90);   // фон: снять выделение
  h.settle(40);
  CHECK(clickSide(h, "tt.learner." + std::to_string(B), "tt.canvas"));
  clickQuick(h, "tt.node." + std::to_string(t1), 0.4f, 0.7f);
  CHECK(revealSide(h, "tt.side.research", "tt.canvas"));
  clickQuick(h, "tt.side.research");
  CHECK(rules::techState(h->world(), t1, B).research);
  CHECK(!rules::techState(h->world(), t1, A).research);
  CHECK(rules::techStudied(h->world(), t1, A));
  shotTrees(h, "trees_common_tech");
  // Ход: исследование B продвигается, A не затронуто.
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK_EQ(rules::techState(h->world(), t1, B).progress, 1);
  CHECK(!rules::techStudied(h->world(), t1, B));
  CHECK(rules::techStudied(h->world(), t1, A));
  // «Архитектура» у A доступна (условие изучено), у B — закрыта.
  CHECK(rules::canResearch(h->world(), t2, A).ok);
  CHECK(!rules::canResearch(h->world(), t2, B).ok);

  // Общая технология не может зависеть от уникальной (ТЗ «Доработки», п.7): отказ правила с причиной.
  Id u = 0;
  CHECK(h->act("Уникальная", [&](Tx& tx) { u = rules::createTech(tx, A, "Тайные ремёсла"); }));
  CHECK(!h->act("Связь", [&](Tx& tx) { rules::setPrereq(tx, t1, u, true); }));
  CHECK(hasToast(h.a(), "Общая технология не может зависеть", app::ToastKind::Warning));
  h->toasts().clear();
  CHECK(!hasPrereq(h->world(), t1, u));
  // В общем дереве условия предлагаются только общие: уникальной в списке нет.
  h.settle(40);
  clickQuick(h, "tt.node." + std::to_string(t1), 0.4f, 0.7f);
  CHECK(clickSide(h, "tt.side.addpre", "tt.canvas"));
  h.type("Тайные");
  h.key(Key::Enter);
  h.settle(40);
  CHECK(!hasPrereq(h->world(), t1, u));
}

TEST(app_trees_common_tech_faction) {
  HideTestRegs hide;
  Harness h("trees_common_tech_faction", 1600, 1000);
  h.demo();
  h.dropToasts();
  auto [A, B] = twoStates(h->world());
  auto [t1, t2] = commonPair(h.a());
  CHECK(t1 && t2);
  // Технология государства A требует общую: условие выбирается в панели свойств (общие — после своих).
  Id u = 0;
  CHECK(h->act("Уникальная", [&](Tx& tx) {
    u = rules::createTech(tx, A, "Гильдия зодчих");
    tx.tech(u).pos = {0, -400};
  }));
  app::openTechTree(h.a(), A, u);
  h.settle(40);
  CHECK(pickSide(h, "tt.side.addpre", "Строительство", "tt.canvas"));
  CHECK(hasPrereq(h->world(), u, t1));
  // Условие — плашка-ссылка слева от карточки; у A «Строительство» не изучено — технология закрыта.
  CHECK(!rules::canResearch(h->world(), u).ok);
  h.key(Key::F);
  h.settle(40);
  const std::string ext = "tt.ext." + std::to_string(u) + "." + std::to_string(t1);
  CHECK(h->uiRect(ext) != nullptr);
  CHECK(h->uiRect("tt.side.pre." + std::to_string(t1)) != nullptr);
  shotTrees(h, "trees_common_tech_ext");
  // Изучено за A — уникальная становится доступной.
  CHECK(h->act("Изучить", [&](Tx& tx) { rules::setStudied(tx, t1, true, A); }));
  CHECK(rules::canResearch(h->world(), u).ok);
  // Щелчок по плашке — общее дерево с этой технологией и изучением за A (уже изучена — исследовать нечего).
  h.settle(40);
  clickQuick(h, ext);
  CHECK_EQ(h->ui.editor, std::string("techtree"));
  CHECK_EQ(h->ui.editorArg, Id(0));
  h.settle(40);
  CHECK(h->uiRect("tt.side.name") != nullptr);
  CHECK(h->uiRect("tt.side.research") == nullptr);
  // Вкладка государства B «Технологии»: раздел «Общие технологии» — состояние для B.
  h.key(Key::Escape);
  h.settle(40);
  h->ui.tabOf[app::SelType::Faction] = "faction.tech";
  h->select(app::SelType::Faction, B);
  h.waitMap();
  h.settle(40);
  const std::string s1 = std::to_string(t1), s2 = std::to_string(t2);
  CHECK(revealIn(h, "faction.ctech.row." + s1));
  CHECK(h->uiRect("faction.ctech.row." + s2) != nullptr);
  // Исследовать за B; остановить.
  CHECK(revealIn(h, "faction.ctech.start." + s1));
  clickQuick(h, "faction.ctech.start." + s1);
  CHECK(rules::techState(h->world(), t1, B).research);
  const app::TabDef* tab = tabDef("faction.tech");
  CHECK(tab != nullptr && tab->badge != nullptr);
  CHECK(tab->badge(h.a(), B) >= 1);
  shotTrees(h, "trees_common_tech_tab");
  CHECK(revealIn(h, "faction.ctech.stop." + s1));
  clickQuick(h, "faction.ctech.stop." + s1);
  CHECK(!rules::techState(h->world(), t1, B).research);
  // «Архитектура» закрыта (условие не изучено) — отметить изученной нельзя.
  u64 v = h->store.version();
  CHECK(revealIn(h, "faction.ctech.study." + s2));
  clickQuick(h, "faction.ctech.study." + s2);
  CHECK_EQ(h->store.version(), v);
  // Отметить изученной «Строительство» — за B; A не затронуто.
  CHECK(revealIn(h, "faction.ctech.study." + s1));
  clickQuick(h, "faction.ctech.study." + s1);
  CHECK(rules::techStudied(h->world(), t1, B));
  CHECK(rules::canResearch(h->world(), t2, B).ok);
  // Снять отметку.
  CHECK(revealIn(h, "faction.ctech.unstudy." + s1));
  clickQuick(h, "faction.ctech.unstudy." + s1);
  CHECK(!rules::techStudied(h->world(), t1, B));
  CHECK(rules::techStudied(h->world(), t1, A));
  // Ссылка — общее дерево с изучением за B: выбранное «Строительство» у B не изучено — можно исследовать (у A — изучено).
  CHECK(revealIn(h, "faction.ctech.open"));
  clickQuick(h, "faction.ctech.open");
  CHECK_EQ(h->ui.editor, std::string("techtree"));
  CHECK_EQ(h->ui.editorArg, Id(0));
  h.settle(40);
  CHECK(h->uiRect("tt.side.name") != nullptr);
  CHECK(h->uiRect("tt.side.research") != nullptr);
  // Снять выделение — в обзоре изучение по государствам.
  RectF cv = rectOf(h.a(), "tt.canvas");
  h.click(cv.x + 30, cv.bottom() - 90);
  h.settle(40);
  CHECK(revealSide(h, "tt.learner." + std::to_string(B), "tt.canvas"));
  shotTrees(h, "trees_common_tech_learners");
}
