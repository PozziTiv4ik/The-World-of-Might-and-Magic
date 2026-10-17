// Окно «Флаг» (ТЗ «Доработки №2», п.1): эмблемы разделами «Темы» и «Расы», группы раздела для быстрого перехода, поиск
// по подписи эмблемы и названию группы, «Без эмблемы», выбранная эмблема подсвечена и видна при открытии; окно
// помещается в низкое окно и при интерфейсе 125 %; игроку «Применить» недоступно (правка мира). Снимки экранов.
#include "gfx/emblems.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

namespace {

Id almarin(Harness& h) { return findFaction(h->world(), "Королевство Альмарин"); }

void openFlag(Harness& h, Id st) {
  CHECK(h->openDialog("flag", st));
  h.settle();
  CHECK(h->hasDialog("flag"));
}

// Элемент окна целиком в области эмблем (видимая часть сетки).
bool inGrid(Harness& h, const std::string& name) {
  const RectF* r = h->uiRect(name);
  const RectF* g = h->uiRect("flag.emblems");
  return r && g && r->y >= g->y - 1 && r->bottom() <= g->bottom() + 1;
}

// Группы раздела в порядке каталога.
std::vector<size_t> sectionGroups(gfx::EmblemSection s) {
  std::vector<size_t> out;
  const auto& gs = gfx::emblemGroups();
  for (size_t i = 0; i < gs.size(); i++)
    if (gs[i].section == s) out.push_back(i);
  return out;
}

}  // namespace

TEST(app_flag_races_section) {
  Harness h("flag_races", 1440, 900);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id st = almarin(h);
  const std::vector<size_t> races = sectionGroups(gfx::EmblemSection::Races);
  CHECK(races.size() >= 51);
  openFlag(h, st);
  CHECK(h->uiRect("flag.emblems.section") && h->uiRect("flag.emblems.search") && h->uiRect("flag.emblem.none"));
  // Раздел «Темы»: первая группа — «Классические».
  CHECK(h->uiRect("flag.group." + std::to_string(sectionGroups(gfx::EmblemSection::Themes).front())) != nullptr);
  h.move(4, 450);
  h.settle();
  CHECK(h.shot("flag_editor_themes"));
  // Раздел «Расы»: группы рас слева, первая — в начале сетки.
  CHECK(clickPart(h, "flag.emblems.section", 1, 2));
  h.settle();
  CHECK(h->uiRect("flag.group." + std::to_string(races.front())) != nullptr);
  CHECK(inGrid(h, "flag.emblem." + gfx::emblemGroups()[races.front()].names.front()));
  // Быстрый переход к группе ниже по списку: её эмблемы в видимой части сетки.
  const size_t target = races[std::min<size_t>(12, races.size() - 1)];
  const std::string first = gfx::emblemGroups()[target].names.front();
  CHECK(!inGrid(h, "flag.emblem." + first));
  CHECK(h.clickUi("flag.group." + std::to_string(target)));
  h.settle();
  CHECK(inGrid(h, "flag.emblem." + first));
  CHECK(h.clickUi("flag.emblem." + first));
  h.settle();
  h.move(4, 450);
  h.settle();
  CHECK(h.shot("flag_editor_races"));
  CHECK(h.clickUi("flag.ok"));
  h.settle();
  CHECK(!h->hasDialog("flag"));
  CHECK_EQ(h->world().faction(st)->flag.emblem, first);
  // Снова открыть: раздел «Расы», выбранная эмблема видна.
  openFlag(h, st);
  CHECK(inGrid(h, "flag.emblem." + first));
  CHECK(h.clickUi("flag.cancel"));
  h.settle();
}

TEST(app_flag_search_and_none) {
  Harness h("flag_search", 1440, 900);
  h.demo();
  h.waitMap();
  h.dropToasts();
  const Id st = almarin(h);
  openFlag(h, st);
  // Поиск по подписи: «волк» — эмблемы с волком во всех разделах.
  CHECK(h.clickUi("flag.emblems.search"));
  h.type("волк");
  h.settle();
  CHECK(inGrid(h, "flag.emblem.wolf"));
  CHECK(h->uiRect("flag.emblem.crown") == nullptr);
  h.move(4, 450);
  h.settle();
  CHECK(h.shot("flag_editor_search"));
  CHECK(h.clickUi("flag.emblem.wolf"));
  h.settle();
  // Поиск по названию группы: «люди» — вся группа людей.
  CHECK(h.clickUi("flag.emblems.search"));
  h.key(Key::A, ctrl());
  h.type("люди");
  h.settle();
  size_t people = 0;
  for (size_t i = 0; i < gfx::emblemGroups().size(); i++)
    if (utf8::lower(std::string(gfx::emblemGroups()[i].title)) == "люди") people = i + 1;
  CHECK(people != 0);
  if (people) CHECK(inGrid(h, "flag.emblem." + gfx::emblemGroups()[people - 1].names.back()));
  CHECK(h.clickUi("flag.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(st)->flag.emblem, std::string("wolf"));
  // «Без эмблемы».
  openFlag(h, st);
  CHECK(h.clickUi("flag.emblem.none"));
  CHECK(h.clickUi("flag.ok"));
  h.settle();
  CHECK(h->world().faction(st)->flag.emblem.empty());
}

// Низкое окно и интерфейс 125 %: окно целиком на экране, сетка эмблем видна.
TEST(app_flag_small_windows) {
  struct Size {
    int w, h;
    float scale;
    const char* tag;
  };
  for (const Size s : {Size{1100, 700, 1.f, "s"}, Size{1440, 900, 1.25f, "z"}, Size{900, 760, 1.f, "n"}}) {
    Harness h(std::string("flag_small_") + s.tag, s.w, s.h);
    if (s.scale != 1.f) h->setUiScale(s.scale);
    h.demo();
    h.waitMap();
    h.dropToasts();
    openFlag(h, almarin(h));
    const RectF* ok = h->uiRect("flag.ok");
    const RectF* grid = h->uiRect("flag.emblems");
    CHECK_MSG(ok && ok->bottom() <= float(s.h) - 8, s.tag);
    CHECK_MSG(grid && grid->h >= 150, s.tag);
    h.move(4, float(s.h) / 2);
    h.settle();
    CHECK(h.shot(std::string("flag_editor_") + s.tag));
  }
}

// Игрок: флаг — правка мира, «Применить» недоступно.
TEST(app_flag_player_readonly) {
  Harness h("flag_player");
  h.demo();
  h.dropToasts();
  const Id st = almarin(h);
  h->ui.role = app::Role::Player;
  h->ui.playerState = st;
  const Flag before = h->world().faction(st)->flag;
  openFlag(h, st);
  CHECK(h.clickUi("flag.emblem.none"));
  CHECK(h.clickUi("flag.ok"));
  h.settle();
  CHECK_EQ(h->world().faction(st)->flag.emblem, before.emblem);
}
