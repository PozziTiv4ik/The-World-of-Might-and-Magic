// Герои: инвентарь с реликвиями (ТЗ «Доработки», п.10: «Добавить героям дополнительный список: "Инвентарь". В
// инвентарь можно будет добавлять реликвии из соответствующего справочника») — фишки с подсветкой редкости, добавить
// свободную, передать чужую («у <имя>»), убрать, справочник реликвий; число реликвий во вкладке «Герои»; гарнизон в
// ролях героя. Портреты в кружочках (ТЗ «Исправления», п.2): шапка государства, «Герои», «Совет», список персонажей,
// страница персонажа, «Воскресить» — портрет (квадрат по лицу), а не инициалы.
#include <chrono>
#include <thread>

#include "app/widgets.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

namespace {

Id stateHero(const World& w, Id st, int skip = 0) {
  Id r = 0;
  w.characters.each([&](const Character& c) {
    if (r || c.faction != st || !c.hero || w.faction(st)->ruler == c.id) return;
    if (skip-- > 0) return;
    r = c.id;
  });
  return r;
}

// Портрет 2:3 по пояс: лицо в верхней трети (если в репозитории нет портрета кампании).
std::string syntheticPortrait() {
  codec::RgbaImage img;
  img.w = 400;
  img.h = 600;
  img.rgba.resize(size_t(img.w) * size_t(img.h) * 4);
  for (int y = 0; y < img.h; y++)
    for (int x = 0; x < img.w; x++) {
      u8* p = &img.rgba[(size_t(y) * size_t(img.w) + size_t(x)) * 4];
      const double fx = (x - 200) / 60.0, fy = (y - 150) / 78.0, bx = (x - 200) / 170.0, by = (y - 560) / 330.0;
      const bool face = fx * fx + fy * fy < 1, body = bx * bx + by * by < 1;
      p[0] = u8(face ? 228 : body ? 110 : 40 + y / 8);
      p[1] = u8(face ? 184 : body ? 36 : 50 + y / 10);
      p[2] = u8(face ? 150 : body ? 44 : 90 + x / 8);
      p[3] = 255;
    }
  const std::vector<u8> png = codec::encodePng(img, 6);
  return std::string(png.begin(), png.end());
}

bool hasToast(Harness& h, std::string_view part) {
  for (auto& t : h->toasts())
    if (t.text.find(part) != std::string::npos) return true;
  return false;
}

// Щелчок по крестику фишки (правая часть фишки).
bool clickChipRemove(Harness& h, const std::string& name) {
  if (!ensureVisible(h, name)) return false;
  h.settle();
  const RectF* r = h->uiRect(name);
  if (!r) return false;
  const RectF c = *r;
  h.click(c.right() - 14, c.cy());
  return true;
}

}  // namespace

// ---------------------------------------------------------------- инвентарь
TEST(app_eco_hero_inventory) {
  HideTestRegs regs;
  Harness h("eco_hero_inventory", 1440, 1100);
  h.demo();
  h.waitMap();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id heroA = stateHero(h->world(), st), heroB = stateHero(h->world(), st, 1);
  CHECK(st && heroA && heroB && heroA != heroB);
  Id crown = 0, staff = 0, ring = 0;
  CHECK(h->act("Реликвии", [&](Tx& tx) {
    crown = rules::addRelic(tx, "Корона Зари", Rarity::Legendary);
    staff = rules::addRelic(tx, "Посох Бурь", Rarity::Epic);
    ring = rules::addRelic(tx, "Кольцо Странника", Rarity::Common);
    rules::giveRelic(tx, heroB, staff);
  }));
  h->ui.tabOf[app::SelType::Character] = "character.info";
  h->select(app::SelType::Character, heroA);
  h.settle();
  h.dropToasts();
  CHECK(ensureVisible(h, "character.inventory"));
  CHECK(ensureVisible(h, "character.addRelic"));
  // Свободная реликвия — из списка справочника.
  CHECK(pickInCombo(h, "character.addRelic", "Корона"));
  h.settle();
  CHECK(h->world().character(heroA)->inventory == std::vector<Id>{crown});
  CHECK(ensureVisible(h, "character.relic." + std::to_string(crown)));
  // Реликвия другого героя: в списке с подписью «у …», выбор передаёт её этому герою.
  CHECK(pickInCombo(h, "character.addRelic", "Посох"));
  CHECK(hasToast(h, "передана от"));
  h.settle();
  CHECK(rules::relicHolder(h->world(), staff) == heroA);
  CHECK(h->world().character(heroB)->inventory.empty());
  CHECK(ensureVisible(h, "character.relic." + std::to_string(staff)));
  CHECK(pickInCombo(h, "character.addRelic", "Кольцо"));
  h.settle();
  CHECK_EQ(h->world().character(heroA)->inventory.size(), size_t(3));
  h.dropToasts();
  h.settle();
  CHECK(ensureVisible(h, "character.inventory"));
  CHECK(h.shot("eco_hero_inventory"));
  // Убрать крестиком фишки.
  CHECK(clickChipRemove(h, "character.relic." + std::to_string(ring)));
  h.settle();
  CHECK(rules::relicHolder(h->world(), ring) == 0);
  CHECK_EQ(h->world().character(heroA)->inventory.size(), size_t(2));
  // Отмена — по шагу.
  h->undo();
  h.step();
  CHECK(rules::relicHolder(h->world(), ring) == heroA);
  // Вкладка «Герои» государства: число реликвий у героя.
  openTab(h, st, "faction.heroes");
  CHECK(ensureVisible(h, "heroes.relics." + std::to_string(heroA)));
  CHECK(h->uiRect("heroes.relics." + std::to_string(heroB)) == nullptr);
  CHECK(h.shot("eco_hero_relics_column"));
  // Справочник реликвий — значок в заголовке инвентаря.
  h->select(app::SelType::Character, heroA);
  h.settle();
  CHECK(ensureVisible(h, "character.relicsCatalog"));
  CHECK(h.clickUi("character.relicsCatalog"));
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("catalogs"));   // вкладка 8 — «Реликвии» (редактор берёт её из аргумента)
  h.dropToasts();
  h.settle();
  CHECK(h.shot("eco_hero_relics_catalog"));
}

// ---------------------------------------------------------------- гарнизон в ролях героя
TEST(app_eco_hero_garrison_role) {
  HideTestRegs regs;
  Harness h("eco_hero_garrison", 1440, 1000);
  h.demo();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id cap = h->world().faction(st)->capital;
  Id hero = 0;   // новый герой: герои демо-мира сопровождают войска
  CHECK(h->act("Герой", [&](Tx& tx) {
    hero = rules::createCharacter(tx, st, "Страж Ворот");
    tx.character(hero).hero = true;
  }));
  CHECK(st && hero && cap);
  CHECK(h->act("В гарнизон", [&](Tx& tx) { rules::setGarrisonHero(tx, cap, hero, true); }));
  h->ui.tabOf[app::SelType::Character] = "character.roles";
  h->select(app::SelType::Character, hero);
  h.settle();
  CHECK(ensureVisible(h, "character.garrison"));
  CHECK(h.shot("eco_hero_garrison_role"));
  // «Покинуть гарнизон» — крестик в строке.
  CHECK(h.clickUi("character.garrisonLeave"));
  h.settle();
  CHECK(rules::heroGarrison(h->world(), hero) == 0);
  // Вкладка «Герои»: значок гарнизона у героя в гарнизоне.
  CHECK(h->act("В гарнизон", [&](Tx& tx) { rules::setGarrisonHero(tx, cap, hero, true); }));
  openTab(h, st, "faction.heroes");
  bool found = false;
  for (int i = 0; i < 8; i++) found = found || h->uiRect("heroes.garrison." + std::to_string(i)) != nullptr;
  CHECK(found);
}

// ---------------------------------------------------------------- портреты в кружочках
TEST(app_eco_portraits_in_circles) {
  HideTestRegs regs;
  Harness h("eco_portraits", 1440, 1000);
  h.demo();
  h.waitMap();
  // Портрет кампании (2:3, по пояс); без него — такой же по форме синтетический.
  auto bytes = fs::readFile("11_Медиа/Портреты_персонажей/Капитан_Эйганн/Капитан_Эйганн_основной_портрет.png");
  if (!bytes) bytes = syntheticPortrait();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Faction* f = h->world().faction(st);
  const Id ruler = f->ruler, hero = stateHero(h->world(), st), dead = stateHero(h->world(), st, 1);
  const Id counselor = f->council.empty() ? 0 : f->council[0].character;
  CHECK(ruler && hero && dead && counselor);
  CHECK(h->act("Портреты", [&](Tx& tx) {
    for (Id c : {ruler, hero, dead, counselor}) tx.character(c).portrait = *bytes;
    rules::heroFate(tx, dead, rules::Fate::Killed, 0, f->capital);
  }));
  // Портреты декодируются в фоне: кадры, пока все не готовы.
  auto ready = [&] {
    for (Id c : {ruler, hero, dead, counselor})
      if (!app::w::faceImage(*h->world().character(c))) return false;
    return true;
  };
  for (int i = 0; i < 400 && !ready(); i++) {
    h.frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(ready());
  // Квадрат по лицу: у портрета 2:3 — уже ширины (лицо крупнее, чем при вписывании всего портрета).
  if (const gfx::Image* img = app::w::faceImage(*h->world().character(hero))) CHECK(img->w == img->h && img->w > 0);
  // Шапка государства (правитель), «Герои», «Совет».
  openTab(h, st, "faction.heroes");
  h.settle();
  CHECK(h.shot("eco_portraits_heroes"));
  openTab(h, st, "faction.council");
  h.settle();
  CHECK(h.shot("eco_portraits_council"));
  // Список персонажей и страница героя.
  h->ui.tabOf[app::SelType::Character] = "character.info";
  h->select(app::SelType::Character, hero);
  h.settle();
  CHECK(h.shot("eco_portraits_character"));
  h->openDirectory("characters");
  h.settle();
  // Отбор по государству героев с портретами: поиск по имени.
  CHECK(h.clickUi("characters.search"));
  h.type(h->world().character(hero)->name);
  h.move(8, 500);
  h.settle();
  CHECK(h->uiRect("characters.first") != nullptr);
  CHECK(h.shot("eco_portraits_list"));
  // «Воскресить» погибшего.
  CHECK(h->openDialog("hero.resurrect", dead));
  h.settle();
  CHECK(h->hasDialog("hero.resurrect"));
  CHECK(h.shot("eco_portraits_resurrect"));
}
