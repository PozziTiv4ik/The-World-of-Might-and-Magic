// Regnum — чтение и запись полей ТЗ 2026-10 (виды государств, вассалитет, рабы, формирование, флот в торговле, сроки
// модификаторов, оккупационный гарнизон, верность войск, производство построек, позиции сделок, константы) и миграции
// при чтении: базовые записи справочников (core/content.h), природа героев государств.
#include "core/content.h"
#include "tests/test_core_io_util.h"

using namespace rg;
using namespace rg::iotest;

namespace {

Modifier builtin(const char* key, Id id) {
  Modifier m = *schema::builtinModifier(key);
  m.id = id;
  return m;
}

// Богатый мир с заполненными полями ТЗ (значения уже нормализованы).
World tzWorld() {
  World base = richWorld();
  Tx tx(base);
  tx.add(builtin(schema::mod::Plundered, 4));
  tx.add(builtin(schema::mod::Patriotism, 5));
  tx.add(builtin(schema::mod::Captive, 6));
  tx.add(builtin(schema::mod::Undead, 8));

  Settings& s = tx.settings();
  s.showArmies = false;
  s.figureSize = 50;

  Catalogs& c = tx.catalogs();
  for (CatalogItem& p : c.positions)
    if (p.name == "Казначей") {
      p.modifiers = {1};
      p.vacantModifiers = {2};
    }

  Constants& k = tx.constants();
  Constant col = *std::find_if(schema::builtinConstants().begin(), schema::builtinConstants().end(),
                               [](const Constant& x) { return x.key == schema::cst::ColonizationCost; });
  col.num = 250.125;
  k.list.push_back(col);
  Constant own;
  own.key = "user1";
  own.name = "Свои расы";
  own.type = ConstType::Values;
  own.values = {"Гномы", "Великаны"};
  own.desc = "Описание";
  k.list.push_back(own);
  Constant cost;
  cost.key = "user2";
  cost.name = "Стоимость осадной башни";
  cost.type = ConstType::Resources;
  cost.res = {{kGold, 12.345}, {7, 2}};
  k.list.push_back(cost);

  Faction& f1 = tx.faction(1);
  f1.mainState = true;
  f1.stateKind = StateKind::Living;
  f1.modifiers = {1, 2};
  f1.modTurns = {{2, 4}};
  f1.slaves = {SlaveGroup{1, 1500, -12.5}, SlaveGroup{2, 40, 30}};
  f1.forming = {Formation{1, 100, 2, 100, {}}, Formation{3, 2, 1, 0, {{kGold, 300.5}}}};
  f1.tradeFleet = {{3, 4}};
  f1.pirateRisk = 1.5;
  f1.army[0].race = schema::kRaceLiving;
  f1.army[1].race = "Гномы";
  Faction& f3 = tx.faction(3);
  f3.stateKind = StateKind::Undead;
  f3.suzerain = 1;
  f3.rebelOf = 1;
  f3.army[0].race = schema::kRaceUndead;

  Character& c2 = tx.character(2);
  c2.modifiers = {3, 6};
  c2.captor = 3;
  Character& c3 = tx.character(3);
  c3.modifiers = {8};
  c3.burial = 1;
  c3.modTurns = {{8, 9}};

  Province& p1 = tx.province(1);
  p1.modifiers = {1, 2, 4};
  p1.modTurns = {{4, 3}};
  p1.slaves = {SlaveWork{1, 120}};
  Province& p3 = tx.province(3);
  p3.occGarrison = {{5, 20}};
  p3.occIdle = 2;

  Building& b1 = tx.building(1);
  b1.levels[0].produce = {{2, 1.5}, {kGold, 0.125}};

  Army& a1 = tx.army(1);
  a1.loyalty = -35.5;
  a1.modifiers = {5};
  a1.modTurns = {{5, 2}};

  Deal d;
  d.id = 5;
  d.kind = DealKind::Trade;
  d.a = 1;
  d.b = 3;
  d.items = {DealItem{DealSide::A, kGold, 0, DealMode::Once, 1, 0, DealItemKind::Province, 3},
             DealItem{DealSide::B, kGold, 0, DealMode::Once, 1, 0, DealItemKind::Hero, 2},
             DealItem{DealSide::B, kGold, 10.375, DealMode::Once, 1, 0, DealItemKind::Resource, 0}};
  d.status = DealStatus::Done;
  d.turn = 6;
  tx.add(d);

  Modifier& m4 = tx.modifier(4);
  m4.duration = 7;
  m4.kind = ModKind::Province;
  rollArch(tx);
  return std::move(tx).finish();
}

}  // namespace

TEST(io_tz_fields_normal_and_round_trip) {
  World w = tzWorld();
  World n = w;
  io::Warnings warns;
  u32 mask = io::normalize(n, warns);
  CHECK_MSG(warns.empty(), warningsText(warns));
  CHECK_EQ(mask, 0u);

  std::string dir = tempDir("tz_round_trip");
  io::save(dir, w, TB_ALL);
  io::LoadResult r = io::load(dir);
  CHECK_MSG(r.warnings.empty(), warningsText(r.warnings));
  CHECK_EQ(io::toJson(w), io::toJson(r.world));
  const World& x = r.world;
  CHECK(!x.settings->showArmies);
  CHECK_EQ(x.settings->figureSize, 50);
  CHECK_NEAR(x.constants->num(schema::cst::ColonizationCost), 250.125, 1e-12);
  CHECK((x.constants->find("user1")->values == std::vector<std::string>{"Гномы", "Великаны"}));
  CHECK_NEAR(x.constants->find("user2")->res.at(kGold), 12.345, 1e-12);
  const Faction& f1 = *x.faction(1);
  CHECK(f1.mainState);
  CHECK_EQ(f1.slaves.size(), size_t(2));
  CHECK_EQ(f1.slaves[0].count, i64(1500));
  CHECK_NEAR(f1.slaves[0].contentment, -12.5, 1e-12);
  CHECK_EQ(f1.forming.size(), size_t(2));
  CHECK_NEAR(f1.forming[1].paid.at(kGold), 300.5, 1e-12);
  CHECK_EQ(f1.tradeFleet.size(), size_t(1));
  CHECK_EQ(f1.tradeFleet[0].count, i64(4));
  CHECK_NEAR(f1.pirateRisk, 1.5, 1e-12);
  CHECK_EQ(f1.modTurns.at(2), 4);
  CHECK_EQ(f1.army[1].race, std::string("Гномы"));
  const Faction& f3 = *x.faction(3);
  CHECK(f3.stateKind == StateKind::Undead);
  CHECK_EQ(f3.suzerain, Id(1));
  CHECK_EQ(f3.rebelOf, Id(1));
  CHECK_EQ(x.character(2)->captor, Id(3));
  CHECK_EQ(x.character(3)->burial, Id(1));
  CHECK_EQ(x.province(1)->modTurns.at(4), 3);
  CHECK_EQ(x.province(1)->slaves[0].count, i64(120));
  CHECK_EQ(x.province(3)->occGarrison[0].count, i64(20));
  CHECK_EQ(x.province(3)->occIdle, 2);
  CHECK_NEAR(x.building(1)->levels[0].produce.at(kGold), 0.125, 1e-12);
  CHECK_NEAR(x.army(1)->loyalty, -35.5, 1e-12);
  CHECK_EQ(x.army(1)->modTurns.at(5), 2);
  CHECK(x.deal(5)->items[0].kind == DealItemKind::Province);
  CHECK_EQ(x.deal(5)->items[1].ref, Id(2));
  CHECK_EQ(x.modifier(4)->duration, 7);
  CHECK(x.modifier(4)->kind == ModKind::Province);
  CHECK_EQ(x.modifier(4)->key, std::string(schema::mod::Plundered));
}

TEST(io_tz_invalid_fields_fixed) {
  World base = tzWorld();
  Tx tx(base);
  tx.faction(1).suzerain = 3;                       // цикл: 3 — вассал 1
  tx.faction(2).mainState = true;                   // гильдия
  tx.faction(1).slaves.push_back(SlaveGroup{1, 5, 0});   // повтор расы
  tx.faction(1).slaves.push_back(SlaveGroup{99, 5, 0});  // нет расы
  tx.faction(1).forming.push_back(Formation{77, 5, 2, 0, {}});
  tx.character(1).captor = 1;                       // своё государство
  tx.army(1).loyalty = -250;
  tx.province(1).modTurns[99] = 3;                  // срок модификатора, которого нет у провинции
  tx.settings().figureSize = 500;
  World w = std::move(tx).finish();
  io::Warnings warns;
  io::normalize(w, warns);
  CHECK(!warns.empty());
  CHECK(w.faction(1)->suzerain == 0 || w.faction(3)->suzerain == 0);
  CHECK(!w.faction(2)->mainState);
  CHECK_EQ(w.faction(1)->slaves.size(), size_t(2));
  CHECK_EQ(w.faction(1)->slaves[0].count, i64(1505));
  CHECK_EQ(w.faction(1)->forming.size(), size_t(2));
  CHECK_EQ(w.character(1)->captor, Id(0));
  CHECK_NEAR(w.army(1)->loyalty, -100, 1e-12);
  CHECK(!w.province(1)->modTurns.count(99));
  CHECK_EQ(w.settings->figureSize, schema::kFigureSizeMax);
}

// Мир прежней версии (до групп ресурсов): прежние базовые справочники, встроенный ресурс «Провизия».
World legacyWorld(bool provisionsUsed) {
  World base = newWorld("Прежний мир");
  Tx tx(base);
  Meta& m = tx.meta();
  m.content = 0;
  m.seq = {};
  Catalogs& c = tx.catalogs();
  c = Catalogs{};
  auto item = [](Id id, const char* n, const char* key = "") {
    CatalogItem it;
    it.id = id;
    it.name = n;
    it.key = key;
    it.builtin = id == kGold;
    return it;
  };
  c.resources = {item(kGold, "Золото"), item(2, "Провизия", "provisions"), item(3, "Древесина"), item(4, "Железо")};
  const char* pos[] = {"Канцлер", "Казначей", "Маршал", "Адмирал", "Тайный советник", "Придворный маг"};
  for (int i = 0; i < 6; i++) c.positions.push_back(item(Id(i + 1), pos[i]));
  tx.w().buildings.each([&](const Building& b) { tx.eraseBuilding(b.id); });
  Faction f;
  f.id = 1;
  f.name = "Королевство";
  f.council = {CouncilSeat{1, "Канцлер", 0}, CouncilSeat{2, "Своя должность", 0}};
  if (provisionsUsed) f.res[2] = -30;
  f.res[3] = 5;
  tx.add(f);
  return std::move(tx).finish();
}

TEST(io_tz_content_migration_once) {
  World w = legacyWorld(true);
  io::Warnings warns;
  io::normalize(w, warns);
  CHECK_EQ(w.meta->content, content::kVersion);
  // Должности: прежние названия переименованы вместе с местами совета, новые добавлены.
  CHECK(Catalogs::find(w.catalogs->positions, 1) && Catalogs::find(w.catalogs->positions, 1)->name == "Десница");
  CHECK_EQ(w.faction(1)->council[0].position, std::string("Десница"));
  CHECK_EQ(w.faction(1)->council[1].position, std::string("Своя должность"));
  CHECK_EQ(w.catalogs->positions.size(), size_t(10));
  // «Провизия» использовалась (долг −30): ресурс — в группе «Провизия», отрицательный запас — недостача.
  const Id prov = w.catalogs->groupId(schema::grp::Provisions);
  CHECK(prov != 0);
  CHECK(w.catalogs->resourceIn(2, prov));
  CHECK(w.resource(2)->key.empty());
  CHECK_NEAR(w.faction(1)->stock(2), 0, 1e-12);
  CHECK_NEAR(w.faction(1)->provisionDebt, 30, 1e-12);
  // Существующие ресурсы получили группы, а не задвоились.
  int iron = 0;
  for (const CatalogItem& r : w.catalogs->resources) iron += r.name == "Железо";
  CHECK_EQ(iron, 1);
  CHECK(w.catalogs->resourceIn(4, w.catalogs->groupId(schema::grp::OreCommon)));
  CHECK_EQ(w.catalogs->essences.size(), size_t(18));
  CHECK_EQ(w.catalogs->religions.size(), size_t(70));
  CHECK(w.catalogs->resourceId(schema::kResCorpses) != 0 && w.catalogs->resourceId(schema::kResEnergy) != 0);
  CHECK(w.catalogs->resourceId(schema::kResMechParts) != 0);
  CHECK_EQ(w.catalogs->specials.size(), size_t(10));
  // Версия 2: валюты, сундуки, археологические места, классы героев, «Гильдия Археологов» у государства.
  const Id cur = w.catalogs->groupId(schema::grp::Currencies);
  CHECK(cur != 0);
  CHECK(w.catalogs->resourceIn(kGold, cur));
  CHECK(w.catalogs->resourceIn(w.catalogs->resourceId(schema::kResCorpses), cur));
  CHECK(w.catalogs->resourceIn(w.catalogs->resourceId(schema::kResArchTreasure), cur));
  CHECK(w.catalogs->resourceIn(w.catalogs->resourceId(schema::kResShantiriScrolls), cur));
  CHECK_EQ(w.catalogs->chests.size(), size_t(16));
  CHECK_EQ(w.catalogs->archSites.size(), size_t(10));
  CHECK_EQ(w.catalogs->classes.size(), size_t(20));
  CHECK(w.catalogs->relicGroupId(schema::kRelicArchFinds) != 0);
  int guilds = 0;
  w.buildings.each([&](const Building& b) { guilds += b.owner == 1 && b.key == schema::bld::ArchGuild; });
  CHECK_EQ(guilds, 1);
  CHECK(std::any_of(warns.begin(), warns.end(), [](const io::Warning& x) { return x.msg.find("мир дополнен") != std::string::npos; }));
  // Второй раз — ничего не добавляется.
  World again = w;
  io::Warnings w2;
  const u32 m2 = io::normalize(again, w2);
  std::string all;
  for (auto& x : w2) all += x.text() + "\n";
  CHECK_MSG(w2.empty(), all);
  CHECK_EQ(m2, u32(0));
}

TEST(io_tz_legacy_provisions_unused_removed) {
  World w = legacyWorld(false);
  io::Warnings warns;
  io::normalize(w, warns);
  CHECK(!w.resource(2));   // прежний ресурс «Провизия» нигде не использовался
  CHECK(w.catalogs->groupId(schema::grp::Provisions) != 0);
  CHECK(w.catalogs->resourceId(schema::kResLegacyProvisions) == 0);
}

TEST(io_tz_hero_nature_migration_once) {
  World base = newWorld("Природа");
  Tx tx(base);
  Faction s;
  s.id = 1;
  s.name = "Королевство";
  tx.add(s);
  Faction u;
  u.id = 2;
  u.name = "Некрополь";
  u.stateKind = StateKind::Undead;
  tx.add(u);
  Faction g;
  g.id = 3;
  g.kind = FactionKind::Guild;
  g.name = "Гильдия";
  tx.add(g);
  for (Id f : {Id(1), Id(2), Id(3), Id(0)}) {
    Character c;
    c.faction = f;
    c.name = "Герой " + std::to_string(f);
    tx.add(c);
  }
  World w = std::move(tx).finish();
  io::Warnings warns;
  u32 mask = io::normalize(w, warns);
  CHECK(mask & TB_CHARACTERS);
  CHECK(mask & TB_MODIFIERS);
  CHECK_EQ(warns.size(), size_t(2));
  auto natureOf = [&](Id c) -> std::string {
    for (Id m : w.character(c)->modifiers)
      if (const Modifier* x = w.modifier(m); x && schema::isNatureKey(x->key)) return x->key;
    return {};
  };
  CHECK_EQ(natureOf(1), std::string(schema::mod::Living));
  CHECK_EQ(natureOf(2), std::string(schema::mod::Undead));
  CHECK_EQ(natureOf(3), std::string());   // герой гильдии
  CHECK_EQ(natureOf(4), std::string());   // без фракции
  // Повторное чтение ничего не меняет; снятая природа не возвращается, пока запись модификатора есть.
  World again = w;
  io::Warnings w2;
  CHECK_EQ(io::normalize(again, w2), 0u);
  CHECK(w2.empty());
  Tx t2(again);
  t2.character(1).modifiers.clear();
  World removed = std::move(t2).finish();
  io::Warnings w3;
  io::normalize(removed, w3);
  CHECK(w3.empty());
  CHECK(removed.character(1)->modifiers.empty());
}
