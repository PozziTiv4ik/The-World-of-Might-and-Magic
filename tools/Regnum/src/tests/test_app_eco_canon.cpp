// Связь персонажа с каноном (ТЗ «Исправления», п.1): поиск карточки по части имени — «эйганн» находит «Капитан
// Эйганн» (03_Персонажи/Капитан_Эйганн.md, CHAR-0070) и после вопроса «Точно ли это…» связывает (заметки и портрет из
// карточки); несколько найденных — окно выбора со списком и полем поиска, заполненным именем (уточнение запроса).
// Карточки — настоящие карточки кампании в корне репозитория (тесты запускаются из него).
#include "app/canon.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

namespace {

Id newCharacter(Harness& h, const std::string& name) {
  Id cid = 0;
  CHECK(h->act("Персонаж", [&](Tx& tx) { cid = rules::createCharacter(tx, 0, name); }));
  h->ui.tabOf[app::SelType::Character] = "character.info";
  h->select(app::SelType::Character, cid);
  h.settle();
  h.dropToasts();
  return cid;
}

// Щелчок по «Найти карточку» после прокрутки к нему (прокрутка плавная — дождаться её конца).
bool clickFind(Harness& h) {
  if (!ensureVisible(h, "canon.find")) return false;
  h.settle();
  return h.clickUi("canon.find");
}

bool hasToast(Harness& h, std::string_view part) {
  for (auto& t : h->toasts())
    if (t.text.find(part) != std::string::npos) return true;
  return false;
}

}  // namespace

// Поиск карточек по части строки: регистр, ё/е, псевдонимы, порядок (точные — начинающиеся — содержащие).
TEST(app_eco_canon_find_substring) {
  Harness h("eco_canon_find");
  h.demo();
  CHECK(!app::canon::projectRoot(h.a()).empty());
  using app::canon::Kind;
  auto ids = [&](std::string_view q) {
    std::vector<std::string> r;
    for (const auto& c : app::canon::find(h.a(), Kind::Character, q)) r.push_back(c.id);
    return r;
  };
  // «эйганн», «ЭЙГАНН», «Эйган» — одна карточка; «капитан эйганн» — точное совпадение.
  for (const char* q : {"эйганн", "ЭЙГАНН", "Эйган", "капитан эйганн", "  Капитан   Эйганн "}) {
    const std::vector<std::string> r = ids(q);
    CHECK_MSG(r.size() == 1 && r[0] == "CHAR-0070", q);
  }
  // Слова в другом порядке — тоже находит.
  {
    const std::vector<std::string> r = ids("эйганн капитан");
    CHECK(r.size() == 1 && r[0] == "CHAR-0070");
  }
  // «Капитан» — несколько карточек: сначала начинающиеся с запроса (по заголовку), затем содержащие.
  const auto list = app::canon::find(h.a(), Kind::Character, "капитан");
  CHECK(list.size() >= 6);
  bool starts = true, sorted = true;
  for (size_t i = 0; i < list.size() && i < 6; i++) {
    starts = starts && utf8::searchKey(list[i].title).rfind("капитан", 0) == 0;
    if (i > 0) sorted = sorted && compareRu(list[i - 1].title, list[i].title) <= 0;
  }
  CHECK(starts);
  CHECK(sorted);
  // По ID и по имени файла.
  CHECK(ids("CHAR-0070") == std::vector<std::string>{"CHAR-0070"});
  CHECK(ids("Капитан_Эйганн").size() == 1);
  CHECK(ids("нет-такого-персонажа-xyz").empty());
  CHECK(ids("   ").empty());
}

// Одна найденная карточка: вопрос «Точно ли это…», связь, заметки и портрет из карточки.
TEST(app_eco_canon_link_single) {
  HideTestRegs regs;
  Harness h("eco_canon_single", 1440, 1000);
  h.demo();
  h.waitMap();
  const Id cid = newCharacter(h, "Эйганн");
  CHECK(h->world().character(cid)->entity.empty());
  CHECK(clickFind(h));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(!h->hasDialog("canon.pick"));
  CHECK(h.shot("eco_canon_confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  const Character* c = h->world().character(cid);
  CHECK_EQ(c->entity, std::string("CHAR-0070"));
  CHECK(c->notes.find("Багровой Гвардии") != std::string::npos);
  CHECK(!c->portrait.empty());   // портрет из поля portrait карточки
  h.dropToasts();
  h.settle();
  CHECK(h.shot("eco_canon_linked"));
  // Отмена — одно действие.
  h->undo();
  h.step();
  CHECK(h->world().character(cid)->entity.empty());
  CHECK(h->world().character(cid)->portrait.empty());
  // Имени нет в каноне — уведомление, окна нет.
  const Id other = newCharacter(h, "Нет-такого-героя-xyz");
  CHECK(clickFind(h));
  CHECK(hasToast(h, "не найдена"));   // сразу: уведомление живёт 4 с
  h.settle();
  CHECK(!h->hasDialog("confirm") && !h->hasDialog("canon.pick"));
  CHECK(h->world().character(other)->entity.empty());
}

// Несколько найденных: окно выбора со списком; поиск заполнен именем, уточнение сужает список.
TEST(app_eco_canon_pick_list) {
  HideTestRegs regs;
  Harness h("eco_canon_pick", 1440, 1000);
  h.demo();
  h.waitMap();
  const Id cid = newCharacter(h, "Капитан");
  CHECK(clickFind(h));
  h.settle();
  CHECK(h->hasDialog("canon.pick"));
  CHECK(!h->hasDialog("confirm"));
  CHECK(h->uiRect("canon.pick.search") != nullptr);
  CHECK(h->uiRect("canon.pick.count") != nullptr);
  for (int i = 0; i < 6; i++) CHECK(h->uiRect("canon.pick." + std::to_string(i)) != nullptr);
  CHECK(h.shot("eco_canon_pick"));
  // Уточнить: поле поиска в фокусе — новый запрос заменяет имя.
  h.retype("эйг");
  h.settle();
  CHECK(h->uiRect("canon.pick.0") != nullptr);
  CHECK(h->uiRect("canon.pick.1") == nullptr);
  CHECK(h.shot("eco_canon_pick_refined"));
  CHECK(h.clickUi("canon.pick.ok"));
  h.settle();
  CHECK(!h->hasDialog("canon.pick"));
  CHECK_EQ(h->world().character(cid)->entity, std::string("CHAR-0070"));
  // Снова: выбор из списка стрелкой и двойным щелчком по строке.
  h->undo();
  h.step();
  CHECK(clickFind(h));
  h.settle();
  CHECK(h->hasDialog("canon.pick"));
  const RectF* second = h->uiRect("canon.pick.1");
  CHECK(second != nullptr);
  if (second) {
    const RectF r = *second;
    h.doubleClick(r.cx(), r.cy());
    h.settle();
  }
  CHECK(!h->hasDialog("canon.pick"));
  const std::string linked = h->world().character(cid)->entity;
  const auto list = app::canon::find(h.a(), app::canon::Kind::Character, "Капитан");
  CHECK(list.size() > 1 && linked == list[1].id);
  // Пустой запрос — все карточки; отмена окна ничего не меняет.
  h->undo();
  h.step();
  CHECK(clickFind(h));
  h.settle();
  h.retype("");
  h.key(Key::Backspace);
  h.settle();
  CHECK(h->uiRect("canon.pick.0") != nullptr);
  CHECK(h->hasDialog("canon.pick"));
  // Esc: сначала поле поиска теряет фокус, затем закрывается окно.
  for (int i = 0; i < 3 && h->hasDialog("canon.pick"); i++) {
    h.key(Key::Escape);
    h.settle();
  }
  CHECK(!h->hasDialog("canon.pick"));
  CHECK(h->world().character(cid)->entity.empty());
}
