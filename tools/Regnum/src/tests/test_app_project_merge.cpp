// Сценарии совместной работы сессий над одной папкой мира: Ctrl+S при изменениях другой сессии сливает их с нашими
// (окно итога, новые записи с занятым ID — новый ID у нашей), «Объединить» при возврате фокуса, после слияния —
// обычное сохранение без вопросов. Прежний выбор при невозможном слиянии — test_app_project_files.cpp.
#include <functional>

#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

World edited(const World& w, const std::function<void(Tx&)>& fn) {
  Tx tx(w);
  fn(tx);
  return std::move(tx).finish();
}

// Две сухопутные провинции с владельцем.
std::vector<Id> twoLands(const World& w) {
  std::vector<Id> out;
  w.provinces.each([&](const Province& p) {
    if (!p.sea && p.owner && out.size() < 2) out.push_back(p.id);
  });
  return out;
}

// Другая сессия: прочитала мир с диска, изменила и сохранила (с отпечатками своего чтения).
void otherSession(const std::string& dir, const std::function<void(Tx&)>& fn) {
  io::LoadResult r = io::load(dir);
  World w = edited(r.world, fn);
  hl::advance(2);
  io::save(dir, w, World::diff(r.world, w), &r.files);
}

}  // namespace

TEST(app_project_merge_two_sessions) {
  Harness h("project_merge_sessions");
  h.demo();
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  std::vector<Id> land = twoLands(h->world());
  CHECK_EQ(land.size(), size_t(2));
  otherSession(dir, [&](Tx& tx) {
    tx.province(land[1]).name = "Правка другой сессии";
    Character c;
    c.name = "Герой другой сессии";
    tx.add(std::move(c));
  });
  Id hero = 0;
  CHECK(h->act("Правка", [&](Tx& tx) {
    tx.province(land[0]).contentment = 33;
    Character c;
    c.name = "Герой этой сессии";
    hero = tx.add(std::move(c)).id;
  }));
  h->toasts().clear();
  h.key(Key::S, ctrl());
  h.settle();
  CHECK_MSG(h->hasDialog("merge"), "Ctrl+S при изменениях другой сессии не объединил миры");
  CHECK(!h->hasDialog("external"));
  CHECK(!h->dirty());
  CHECK(h->uiRect("merge.summary") != nullptr);
  CHECK(h.shot("project_merge_report"));
  io::LoadResult r = io::load(dir);
  CHECK(r.mixed.empty());
  CHECK_EQ(r.world.province(land[1])->name, std::string("Правка другой сессии"));
  CHECK_NEAR(r.world.province(land[0])->contentment, 33, 1e-9);
  // Запись с диска свой ID сохраняет (её уже видит другая сессия), наша — с новым ID.
  CHECK_EQ(r.world.character(hero)->name, std::string("Герой другой сессии"));
  Id moved = r.world.characters.firstId([](const Character& c) { return c.name == "Герой этой сессии"; });
  CHECK(moved > hero);
  CHECK(io::toJson(r.world) == io::toJson(h->store.world()));
  // Дальше — обычное сохранение без вопросов.
  h->closeDialogs();
  h.settle();
  CHECK(h->act("Ещё правка", [&](Tx& tx) { tx.province(land[0]).contentment = 34; }));
  h.key(Key::S, ctrl());
  h.settle();
  CHECK(!h->hasDialog("merge"));
  CHECK(!h->dirty());
  CHECK_NEAR(io::load(dir).world.province(land[0])->contentment, 34, 1e-9);
}

TEST(app_project_merge_on_focus) {
  Harness h("project_merge_focus");
  h.demo();
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  std::vector<Id> land = twoLands(h->world());
  CHECK_EQ(land.size(), size_t(2));
  CHECK(h->act("Несохранённая правка", [&](Tx& tx) { tx.province(land[0]).contentment = 21; }));
  otherSession(dir, [&](Tx& tx) { tx.province(land[1]).name = "Правка другой сессии"; });
  hl::setFocus(false);
  hl::setFocus(true);
  h.settle();
  CHECK(h->hasDialog("external"));
  CHECK(h->uiRect("external.merge") != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("project_merge_external"));
  CHECK(h.clickUi("external.merge"));
  h.settle();
  CHECK(h->hasDialog("merge"));
  CHECK_EQ(h->world().province(land[1])->name, std::string("Правка другой сессии"));
  CHECK_NEAR(h->world().province(land[0])->contentment, 21, 1e-9);
  CHECK(h->dirty());
  h->closeDialogs();
  h.settle();
  // Изменения с диска уже в мире — Ctrl+S пишет без вопросов.
  h.key(Key::S, ctrl());
  h.settle();
  CHECK(!h->hasDialog("external"));
  CHECK(!h->hasDialog("merge"));
  CHECK(!h->dirty());
  io::LoadResult r = io::load(dir);
  CHECK_EQ(r.world.province(land[1])->name, std::string("Правка другой сессии"));
  CHECK_NEAR(r.world.province(land[0])->contentment, 21, 1e-9);
  // Без несохранённой работы «Объединить» не предлагается — «Перечитать с диска» по умолчанию.
  otherSession(dir, [&](Tx& tx) { tx.province(land[1]).name = "Вторая правка"; });
  hl::setFocus(false);
  hl::setFocus(true);
  h.settle();
  CHECK(h->hasDialog("external"));
  CHECK(h->uiRect("external.merge") == nullptr);
  CHECK(h.clickUi("external.reload"));
  h.settle();
  CHECK_EQ(h->world().province(land[1])->name, std::string("Вторая правка"));
}
