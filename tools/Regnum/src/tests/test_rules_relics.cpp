// Regnum — правила реликвий: уникальные и копируемые реликвии, количество, места хранения (хранилище постройки,
// инвентарь государства, герой), находки археологов, продажа на рынке, группы реликвий, изображение и карточка.
#include "tests/test_png.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

// ---------------------------------------------------------------- владелец и места хранения
TEST(rules_relics_unique_and_holder) {
  Fix f;
  Id a = 0, b = 0, crown = 0;
  f.tx([&](Tx& tx) {
    a = createCharacter(tx, f.A, "Король");
    b = createCharacter(tx, f.B, "Маг");
    crown = addRelic(tx, "Корона Вечности", Rarity::Legendary);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addRelic(tx, "корона вечности"); }); }), "уже есть"));
  f.tx([&](Tx& tx) { giveRelic(tx, a, crown); });
  CHECK_EQ(relicHolder(f.w(), crown), a);
  f.tx([&](Tx& tx) { giveRelic(tx, b, crown); });   // уникальная реликвия одна — переходит
  CHECK(f.w().character(a)->inventory.empty());
  CHECK((f.w().character(b)->inventory == std::vector<RelicStack>{{crown, 1}}));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelicCount(tx, crown, RelicPlace{RelicPlace::Hero, b, 0, f.B}, 2); }); }), "уникальная"));
  f.tx([&](Tx& tx) { removeRelic(tx, crown); });
  CHECK(f.w().character(b)->inventory.empty());
}

// Эпическая и ниже — копируемые: у скольких угодно героев, с количеством.
TEST(rules_relics_copyable_counts) {
  Fix f;
  Id a = 0, b = 0, sword = 0;
  f.tx([&](Tx& tx) {
    a = createCharacter(tx, f.A, "Ратник");
    b = createCharacter(tx, f.A, "Копейщик");
    sword = addRelic(tx, "Стальной меч", Rarity::Common);
    setRelicCount(tx, sword, RelicPlace{RelicPlace::State, f.A, 0, f.A}, 5);
  });
  CHECK_EQ(relicCountAt(f.w(), sword, RelicPlace{RelicPlace::State, f.A, 0, f.A}), i64(5));
  // Героям — из инвентаря государства, по одной штуке; у обоих одновременно.
  f.tx([&](Tx& tx) {
    giveRelic(tx, a, sword);
    giveRelic(tx, b, sword);
  });
  CHECK_EQ(relicCount(f.w().character(a)->inventory, sword), i64(1));
  CHECK_EQ(relicCount(f.w().character(b)->inventory, sword), i64(1));
  CHECK_EQ(relicCountAt(f.w(), sword, RelicPlace{RelicPlace::State, f.A, 0, f.A}), i64(3));
  CHECK_EQ(relicPlaces(f.w(), sword).size(), size_t(3));
  CHECK(relicFree(f.w(), sword));
  // Убрать у героя — обратно в инвентарь государства; количество в инвентаре — 0 убирает стопку.
  f.tx([&](Tx& tx) { takeRelic(tx, a, sword); });
  CHECK_EQ(relicCountAt(f.w(), sword, RelicPlace{RelicPlace::State, f.A, 0, f.A}), i64(4));
  f.tx([&](Tx& tx) { setRelicCount(tx, sword, RelicPlace{RelicPlace::State, f.A, 0, f.A}, 0); });
  CHECK(f.w().faction(f.A)->relics.empty());
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { moveRelic(tx, sword, RelicPlace{RelicPlace::Hero, b, 0, f.A}, RelicPlace{}, 2); }); }), "Не хватает"));
  // Уникальная, которая где-то лежит, не свободна.
  Id crown = 0;
  f.tx([&](Tx& tx) {
    crown = addRelic(tx, "Корона Света", Rarity::Epochal);
    giveRelic(tx, a, crown);
  });
  CHECK(!relicFree(f.w(), crown));
}

// Рынок: реликвии хранилищ государства продаются за золото по редкости (находки — дороже).
TEST(rules_relics_market_sale) {
  Fix f;
  Id store = 0, market = 0, find = 0, plain = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    store = simpleBuilding(tx, "Хранилище", f.p[0], BuildingFlag::RelicStore, true);
    plain = addRelic(tx, "Кубок", Rarity::Epic);
    find = addRelic(tx, "Черепок", Rarity::Legendary);
    for (Relic& r : tx.catalogs().relics)
      if (r.id == find) r.group = tx.w().catalogs->relicGroupId(schema::kRelicArchFinds);
    storeRelic(tx, f.p[0], store, plain, RelicPlace{}, 4);
    storeRelic(tx, f.p[0], store, find, RelicPlace{});
  });
  CHECK_NEAR(relicPrice(f.w(), plain), 1, 1e-12);
  CHECK_NEAR(relicPrice(f.w(), find), 50, 1e-12);
  CHECK_EQ(sellableRelics(f.w(), f.A).size(), size_t(2));
  const RelicPlace at{RelicPlace::Building, f.p[0], store, f.A};
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { sellRelics(tx, f.A, at, plain, 1); }); }), "нет достроенного рынка"));
  f.tx([&](Tx& tx) {
    tx.province(f.p[1]).owner = f.A;
    market = simpleBuilding(tx, "Рынок", f.p[1], BuildingFlag::Market, true);
  });
  CHECK(hasMarket(f.w(), f.A));
  const double gold0 = f.w().faction(f.A)->treasury();
  f.tx([&](Tx& tx) { sellRelics(tx, f.A, at, plain, 3); });
  CHECK_NEAR(f.w().faction(f.A)->treasury() - gold0, 3, 1e-9);
  CHECK_EQ(relicCountAt(f.w(), plain, at), i64(1));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { sellRelics(tx, f.A, at, plain, 2); }); }), "В хранилище"));
  f.tx([&](Tx& tx) { sellRelics(tx, f.A, at, find, 1); });
  CHECK_NEAR(f.w().faction(f.A)->treasury() - gold0, 53, 1e-9);
  CHECK(relicFree(f.w(), find));
  (void)market;
}

TEST(rules_relics_places) {
  Fix f;
  Id store = 0, hero = 0, other = 0, find = 0, plain = 0;
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.A;
    store = simpleBuilding(tx, "Сокровищница", f.p[0], BuildingFlag::RelicStore, true);
    hero = createCharacter(tx, f.A, "Хранитель");
    other = createCharacter(tx, f.B, "Чужой");
    plain = addRelic(tx, "Кубок", Rarity::Rare);
    find = addRelic(tx, "Черепок", Rarity::Epic);
    for (Relic& r : tx.catalogs().relics)
      if (r.id == find) r.group = tx.w().catalogs->relicGroupId(schema::kRelicArchFinds);
  });
  CHECK(isArchFind(f.w(), find) && !isArchFind(f.w(), plain));
  CHECK(rarityAbove(Rarity::Epochal, Rarity::Legendary) && rarityAbove(Rarity::Rare, Rarity::Common));
  // Находку археологов нельзя дать герою из свободных.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { giveRelic(tx, hero, find); }); }), "археологическая находка"));
  // В хранилище — из справочника; оттуда — государству; от государства — герою (и находку тоже).
  f.tx([&](Tx& tx) { storeRelic(tx, f.p[0], store, find, RelicPlace{}); });
  CHECK(relicPlace(f.w(), find).kind == RelicPlace::Building);
  f.tx([&](Tx& tx) { unstoreRelic(tx, f.p[0], store, find); });
  CHECK(relicPlace(f.w(), find).kind == RelicPlace::State && relicPlace(f.w(), find).id == f.A);
  f.tx([&](Tx& tx) { giveRelic(tx, hero, find); });
  CHECK_EQ(relicHolder(f.w(), find), hero);
  // Реликвию героя другого государства в хранилище положить нельзя.
  f.tx([&](Tx& tx) { giveRelic(tx, other, plain); });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { storeRelic(tx, f.p[0], store, plain, RelicPlace{RelicPlace::Hero, other, 0, f.B}); }); }), "В хранилище"));
  // Снос хранилища: реликвии — государству-владельцу.
  f.tx([&](Tx& tx) { storeRelic(tx, f.p[0], store, find, RelicPlace{RelicPlace::Hero, hero, 0, f.A}); });
  f.tx([&](Tx& tx) { demolish(tx, f.p[0], store); });
  CHECK(relicPlace(f.w(), find).kind == RelicPlace::State);
  // Удаление реликвии убирает её отовсюду.
  f.tx([&](Tx& tx) { removeRelic(tx, find); });
  CHECK(f.w().faction(f.A)->relics.empty());
}

// ---------------------------------------------------------------- группы, изображение, карточка
TEST(rules_relics_groups) {
  Fix f;
  const Id finds = f.w().catalogs->relicGroupId(schema::kRelicArchFinds);
  CHECK(finds != 0);
  Id arms = 0, swords = 0, blade = 0, crown = 0, shard = 0;
  f.tx([&](Tx& tx) {
    arms = addRelicGroup(tx, "Оружие");
    swords = addRelicGroup(tx, "Мечи", arms);
    blade = addRelic(tx, "Клинок скорби", Rarity::Legendary);
    crown = addRelic(tx, "Корона", Rarity::Epochal);
    shard = addRelic(tx, "Черепок", Rarity::Common);
    setRelicGroup(tx, blade, swords);
    setRelicGroup(tx, crown, arms);
    setRelicGroup(tx, shard, finds);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { addRelicGroup(tx, "мечи", arms); }); }), "уже есть"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { renameRelicGroup(tx, swords, " "); }); }), "пустым"));
  f.tx([&](Tx& tx) { renameRelicGroup(tx, swords, "Клинки"); });
  CHECK(relicGroupPath(f.w(), swords) == "Оружие / Клинки");
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelicGroupParent(tx, arms, swords); }); }), "саму себя"));
  CHECK((relicsIn(f.w(), arms) == std::vector<Id>{blade, crown}));
  CHECK((childRelicGroups(f.w(), arms) == std::vector<Id>{swords}));
  // Главенство редкости: эпохальная выше.
  std::vector<Id> all = relicsIn(f.w(), 0);
  sortByRarity(f.w(), all);
  CHECK((all == std::vector<Id>{crown, blade, shard}));
  CHECK(isArchFind(f.w(), shard));
  // Группу «Археологические находки» удалить нельзя; удаление группы отдаёт подгруппы и реликвии родителю.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { removeRelicGroup(tx, finds); }); }), "нельзя удалить"));
  f.tx([&](Tx& tx) { removeRelicGroup(tx, swords); });
  CHECK(f.w().relic(blade)->group == arms);
  f.tx([&](Tx& tx) { removeRelicGroup(tx, arms); });
  CHECK(f.w().relic(blade)->group == 0 && f.w().relic(crown)->group == 0);
  // Изображение: только PNG/JPEG; пусто — значок.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setRelicImage(tx, crown, "не картинка"); }); }), "PNG или JPEG"));
  const std::string img = test::pngBytes(test::makeRgba(8, 8, [](int, int, u8* p) { p[0] = p[1] = p[2] = p[3] = 200; }));
  f.tx([&](Tx& tx) { setRelicImage(tx, crown, img); });
  CHECK(f.w().relic(crown)->image == img);
  f.tx([&](Tx& tx) { setRelicImage(tx, crown, ""); });
  CHECK(f.w().relic(crown)->image.empty());
  f.tx([&](Tx& tx) { setRelicEntity(tx, crown, " ASSET-0037 "); });
  CHECK_EQ(f.w().relic(crown)->entity, std::string("ASSET-0037"));
}
