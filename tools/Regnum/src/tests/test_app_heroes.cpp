// Герои: инвентарь с реликвиями стопками — уникальная из справочника, копируемая от другого героя («у <имя>») и из
// инвентаря государства у двух героев с количеством, «Вернуть одну», справочник реликвий; игрок — только игровые
// действия; значки и число штук во вкладке «Герои»; гарнизон в ролях героя. Портреты в кружочках:
// шапка государства, «Герои», «Совет», список персонажей, страница персонажа, «Воскресить» — портрет (квадрат по
// лицу), а не инициалы.
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
  return test::pngBytes(test::makeRgba(400, 600, [](int x, int y, u8* p) {
    const double fx = (x - 200) / 60.0, fy = (y - 150) / 78.0, bx = (x - 200) / 170.0, by = (y - 560) / 330.0;
    const bool face = fx * fx + fy * fy < 1, body = bx * bx + by * by < 1;
    p[0] = u8(face ? 228 : body ? 110 : 40 + y / 8);
    p[1] = u8(face ? 184 : body ? 36 : 50 + y / 10);
    p[2] = u8(face ? 150 : body ? 44 : 90 + x / 8);
    p[3] = 255;
  }));
}

}  // namespace

// ---------------------------------------------------------------- инвентарь
// Администратор: уникальная из справочника, копируемая от другого героя («передана от»), копируемая из инвентаря
// государства у двух героев с количеством (поле количества, «Вернуть одну»), отмена по шагу; вкладка «Герои» — значки и
// число штук; значок заголовка — справочник реликвий на реликвии.
TEST(app_heroes_inventory) {
  Harness h("eco_hero_inventory", 1440, 1100);
  h.demo();
  h.waitMap();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id heroA = stateHero(h->world(), st), heroB = stateHero(h->world(), st, 1);
  CHECK(st && heroA && heroB && heroA != heroB);
  Id crown = 0, staff = 0, ring = 0;
  const rules::RelicPlace inState{rules::RelicPlace::State, st, 0, st};
  CHECK(h->act("Реликвии", [&](Tx& tx) {
    crown = rules::addRelic(tx, "Корона Зари", Rarity::Legendary);
    staff = rules::addRelic(tx, "Посох Бурь", Rarity::Epic);
    ring = rules::addRelic(tx, "Кольцо Странника", Rarity::Common);
    rules::giveRelic(tx, heroB, staff);
    rules::setRelicCount(tx, ring, inState, 5);
  }));
  h->ui.tabOf[app::SelType::Character] = "character.info";
  h->select(app::SelType::Character, heroA);
  h.settle();
  h.dropToasts();
  CHECK(ensureVisible(h, "character.inventory"));
  CHECK(ensureVisible(h, "character.addRelic"));
  // Уникальная — из справочника (у администратора).
  CHECK(pickInCombo(h, "character.addRelic", "Корона Зари"));
  h.settle();
  CHECK((h->world().character(heroA)->inventory == std::vector<RelicStack>{{crown, 1}}));
  CHECK(ensureVisible(h, "character.relic." + std::to_string(crown)));
  CHECK(h->uiRect("character.relicCount." + std::to_string(crown)) == nullptr);   // у уникальной количества нет
  // Копируемая у другого героя государства: в списке «у …», выбор передаёт одну штуку этому герою.
  CHECK(pickInCombo(h, "character.addRelic", "Посох Бурь"));
  CHECK(hasToast(h, "передана от"));
  h.settle();
  CHECK_EQ(relicCount(h->world().character(heroA)->inventory, staff), i64(1));
  CHECK(h->world().character(heroB)->inventory.empty());
  // Копируемая из инвентаря государства: первым в списке — инвентарь государства (5 шт.).
  CHECK(pickInCombo(h, "character.addRelic", "Кольцо Странника"));
  h.settle();
  CHECK_EQ(relicCount(h->world().character(heroA)->inventory, ring), i64(1));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, inState), i64(4));
  // Количество у администратора: 3 штуки у героя (новые — из справочника).
  CHECK(typeNumber(h, "character.relicCount." + std::to_string(ring), "3"));
  h.settle();
  CHECK_EQ(relicCount(h->world().character(heroA)->inventory, ring), i64(3));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, inState), i64(4));
  // Второй герой с тем же кольцом: выдача из инвентаря государства.
  h->select(app::SelType::Character, heroB);
  h.settle();
  CHECK(pickInCombo(h, "character.addRelic", "Кольцо Странника"));
  h.settle();
  CHECK(pickInCombo(h, "character.addRelic", "Кольцо Странника"));
  h.settle();
  CHECK_EQ(relicCount(h->world().character(heroB)->inventory, ring), i64(2));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, inState), i64(2));
  {
    i64 heroes = 0, total = 0;
    for (const rules::RelicAt& at : rules::relicPlaces(h->world(), ring)) {
      heroes += at.place.kind == rules::RelicPlace::Hero ? 1 : 0;
      total += at.count;
    }
    CHECK_EQ(heroes, i64(2));
    CHECK_EQ(total, i64(7));
  }
  // «Вернуть одну» у первого героя: 3 → 2, инвентарь государства 2 → 3.
  h->select(app::SelType::Character, heroA);
  h.settle();
  h.dropToasts();
  CHECK(clickIn(h, "character.relicReturn." + std::to_string(ring)));
  h.settle();
  CHECK_EQ(relicCount(h->world().character(heroA)->inventory, ring), i64(2));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, inState), i64(3));
  h->undo();
  h.step();
  CHECK_EQ(relicCount(h->world().character(heroA)->inventory, ring), i64(3));
  h.dropToasts();
  h.settle();
  CHECK(ensureVisible(h, "character.inventory"));
  h.move(8, 500);
  h.settle();
  CHECK(h.shot("eco_hero_inventory"));
  // Вкладка «Герои» государства: значки реликвий и число штук.
  openTab(h, st, "faction.heroes");
  CHECK(ensureVisible(h, "heroes.relics." + std::to_string(heroA)));
  CHECK(h->uiRect("heroes.relics." + std::to_string(heroB)) != nullptr);
  h.move(8, 500);
  h.settle();
  CHECK(h.shot("eco_hero_relics_column"));
  // Справочник реликвий — значок в заголовке инвентаря: вкладка «Реликвии», карточка выбранной реликвии.
  h->select(app::SelType::Character, heroA);
  h.settle();
  CHECK(ensureVisible(h, "character.relicsCatalog"));
  CHECK(h.clickUi("character.relicsCatalog"));
  h.settle();
  CHECK_EQ(h->ui.editor, std::string("catalogs"));
  CHECK(h->uiRect("catalogs.relicCard.places") != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("eco_hero_relics_catalog"));
}

// Игрок за государство: выдача из владений государства и «Вернуть одну» — игровые действия; количества и справочника
// нет; герой чужого государства — только просмотр.
TEST(app_heroes_inventory_player) {
  Harness h("eco_hero_inventory_player", 1440, 1000);
  h.demo();
  h.waitMap();
  const Id st = findFaction(h->world(), "Королевство Альмарин");
  const Id hero = stateHero(h->world(), st);
  Id other = 0, foreign = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!other && f.isState() && f.id != st && stateHero(h->world(), f.id)) other = f.id;
  });
  foreign = other ? stateHero(h->world(), other) : 0;
  CHECK(st && hero && other && foreign);
  Id ring = 0, crown = 0;
  const rules::RelicPlace inState{rules::RelicPlace::State, st, 0, st};
  CHECK(h->act("Реликвии", [&](Tx& tx) {
    ring = rules::addRelic(tx, "Кольцо Странника", Rarity::Common);
    crown = rules::addRelic(tx, "Корона Зари", Rarity::Legendary);
    rules::setRelicCount(tx, ring, inState, 3);
    rules::giveRelic(tx, foreign, rules::addRelic(tx, "Посох Бурь", Rarity::Epic));
  }));
  h->ui.role = app::Role::Player;
  h->ui.playerState = st;
  h->ui.tabOf[app::SelType::Character] = "character.info";
  h->select(app::SelType::Character, hero);
  h.settle();
  h.dropToasts();
  CHECK(ensureVisible(h, "character.inventory"));
  CHECK(h->uiRect("character.relicsCatalog") == nullptr);
  CHECK(pickInCombo(h, "character.addRelic", "Кольцо Странника"));
  h.settle();
  CHECK_EQ(relicCount(h->world().character(hero)->inventory, ring), i64(1));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, inState), i64(2));
  // Справочника у игрока нет: свободной короны в списке нет.
  CHECK(pickInCombo(h, "character.addRelic", "Корона Зари"));
  h.key(Key::Escape);   // ничего не найдено — список открыт
  h.settle();
  CHECK(rules::relicHolder(h->world(), crown) == 0);
  CHECK(ensureVisible(h, "character.relic." + std::to_string(ring)));
  CHECK(h->uiRect("character.relicCount." + std::to_string(ring)) == nullptr);
  h.move(8, 500);   // закрытый список не под указателем
  h.settle();
  CHECK(clickIn(h, "character.relicReturn." + std::to_string(ring)));
  h.settle();
  CHECK_EQ(relicCount(h->world().character(hero)->inventory, ring), i64(0));
  CHECK_EQ(rules::relicCountAt(h->world(), ring, inState), i64(3));
  // Герой чужого государства: ни выдачи, ни возврата.
  h->select(app::SelType::Character, foreign);
  h.settle();
  CHECK(ensureVisible(h, "character.inventory"));
  CHECK(h->uiRect("character.addRelic") == nullptr);
  for (const RelicStack& s : h->world().character(foreign)->inventory) CHECK(h->uiRect("character.relicReturn." + std::to_string(s.relic)) == nullptr);
}

// ---------------------------------------------------------------- гарнизон в ролях героя
TEST(app_heroes_garrison_role) {
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
TEST(app_heroes_portraits_in_circles) {
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
