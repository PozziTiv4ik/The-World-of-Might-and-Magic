// Regnum — поля ТЗ «Доработки №1–4» (2026-10-10): археология провинций и государств, реликвии в постройках, классы и
// таланты героев, генерация модификаторов, возможности построек, стоимость технологий, войско на борту флота, наёмники,
// морские чудовища, наименьшая стоимость кораблей — чтение, запись и нормализация.
#include "core/content.h"
#include "tests/test_core_io_util.h"

using namespace rg;
using namespace rg::iotest;

namespace {

Id resNamed(const World& w, const char* name) {
  for (const CatalogItem& r : w.catalogs->resources)
    if (r.name == name) return r.id;
  return 0;
}

// Мир с заполненными полями доработок (значения уже нормализованы).
World tz2World() {
  World base = richWorld();
  Tx tx(base);
  tx.meta().rng = 42;
  tx.settings().freeFleets = true;

  Catalogs& c = tx.catalogs();
  const Id finds = c.relicGroupId(schema::kRelicArchFinds);
  const Id sub = tx.nextId(Seq::RelicGroup);
  c.relicGroups.push_back(RelicGroup{sub, "Черепки", finds, ""});
  for (Id id = 1; id <= 4; id++) {
    Relic r;
    r.id = tx.nextId(Seq::Relic);
    r.name = "Реликвия " + std::to_string(id);
    r.rarity = Rarity(id % 5);
    r.group = id % 2 ? finds : sub;
    r.entity = id == 1 ? "ITEM-0001" : "";
    r.image = id == 1 ? pngBytes(4, 4, 7) : std::string();
    c.relics.push_back(r);
  }
  HeroClass& hc = c.classes[0];
  Talent t1;
  t1.id = tx.nextId(Seq::Talent);
  t1.name = "Стойкость";
  t1.cost = 2;
  t1.modifiers = {1};
  Talent t2;
  t2.id = tx.nextId(Seq::Talent);
  t2.name = "Ярость";
  t2.row = 1;
  t2.col = 2;
  t2.cost = 5;
  t2.prereq = t1.id;
  hc.talents = {t1, t2};
  hc.tierPoints = 2;
  const Id classId = hc.id;
  const Id talent1 = t1.id, talent2 = t2.id;

  Modifier arch;
  arch.name = "Опытные проводники";
  arch.kind = ModKind::ArchGroup;
  arch.fx[size_t(Fx::ArchSuccessPct)] = 3;
  arch.fxMask |= 1u << int(Fx::ArchSuccessPct);
  const Id archMod = tx.add(arch).id;
  Modifier& m1 = tx.modifier(1);
  m1.essGen = {{c.essences[0].id, 5}};
  m1.resGen = {{2, 1.5}};

  Character& ch = tx.character(1);
  ch.level = 12;
  ch.heroClass = classId;
  ch.talents = {talent1, talent2};

  Building& b1 = tx.building(1);
  b1.relicStore = b1.healing = b1.plague = b1.coastal = b1.shipyard = true;
  b1.mercenary = true;
  b1.key = "testKey";
  b1.levels[0].essCost = {{c.essences[1].id, 10}};
  b1.levels[0].ships = (1u << int(ShipType::Galleon)) | (1u << int(ShipType::SeaMonster));
  Building& b2 = tx.building(2);
  b2.stateReqs = {StateReq{1, 4}};

  Tech& t = tx.tech(2);
  t.cost = {{kGold, 5}};
  t.paid = {{kGold, 5}};
  t.needKeys = {schema::bld::ArchGuild};
  t.key = "testTech";

  Province& p1 = tx.province(1);
  p1.buildings[0].paidEss = {{c.essences[1].id, 10}};
  p1.buildings[0].relics = {c.relics.back().id - 3};
  p1.hiddenRelic = c.relics.back().id - 2;
  p1.hiddenBy = 1;
  p1.arch[0].open = true;
  p1.arch[0].stage = 2;
  p1.arch[1].open = true;

  Faction& f1 = tx.faction(1);
  ArchGroup g;
  g.id = tx.nextId(Seq::ArchGroup);
  g.name = "Первая экспедиция";
  g.exp = 150;
  g.modifiers = {archMod};
  g.modTurns = {{archMod, 2}};
  g.busy = 7;
  f1.archGroups = {g};
  f1.relics = {c.relics.back().id - 1};
  ArmyRow merc;
  merc.id = tx.nextId(Seq::Row);
  merc.name = "Вольные клинки";
  merc.type = UnitType::HeavyInf;
  merc.total = 500;
  merc.upkeep = 0.001;
  merc.race = schema::kRaceMercenary;
  merc.merc = true;
  merc.hire = 0.02;
  f1.army.push_back(merc);
  Faction& f3 = tx.faction(3);
  FleetRow sm;
  sm.id = tx.nextId(Seq::Row);
  sm.name = "Левиафан";
  sm.type = ShipType::SeaMonster;
  sm.total = 1;
  sm.keyRes = resNamed(tx.w(), "Левиафаны");
  f3.fleet.push_back(sm);

  Army& a1 = tx.army(1);
  Army& a2 = tx.army(2);
  a1.carrier = 2;
  a2.cargo = 1;
  a1.pos = a2.pos;
  return std::move(tx).finish();
}

}  // namespace

TEST(io_tz2_fields_normal_and_round_trip) {
  World w = tz2World();
  World n = w;
  io::Warnings warns;
  u32 mask = io::normalize(n, warns);
  CHECK_MSG(warns.empty(), warningsText(warns));
  CHECK_EQ(mask, 0u);

  std::string dir = tempDir("tz2_round_trip");
  io::save(dir, w, TB_ALL);
  io::LoadResult r = io::load(dir);
  CHECK_MSG(r.warnings.empty(), warningsText(r.warnings));
  CHECK_EQ(io::toJson(w), io::toJson(r.world));
  const World& x = r.world;
  CHECK_EQ(x.meta->rng, u32(42));
  CHECK(x.settings->freeFleets);
  CHECK_EQ(x.character(1)->level, 12);
  CHECK_EQ(x.character(1)->talents.size(), size_t(2));
  CHECK(x.faction(1)->archGroups.size() == 1 && x.faction(1)->archGroups[0].exp == 150);
  CHECK(x.army(1)->carrier == 2 && x.army(2)->cargo == 1);
  CHECK(x.building(1)->levels[0].ships & (1u << int(ShipType::SeaMonster)));
  CHECK_EQ(x.province(1)->arch[0].stage, 2);
}

TEST(io_tz2_new_world_content) {
  World w = newWorld("Содержимое");
  // Стоимость кораблей — базовая и наименьшая (ТЗ «Доработки №3», п.7–8).
  const Constant* fr = w.constants->find(schema::cst::FrigateCost);
  CHECK(fr != nullptr);
  if (fr) {
    CHECK_EQ(fr->res.at(kGold), 25.0);
    CHECK_EQ(fr->minRes.at(kGold), 25.0);
    CHECK_EQ(fr->res.size(), size_t(4));
  }
  const Constant* sm = w.constants->find(schema::cst::SeaMonsterCost);
  CHECK(sm && sm->ess.size() == 1 && sm->ess.begin()->second == 750);
  // Ветка технологий «Археология»: 10 общих технологий цепочкой, первая требует «Гильдию Археологов».
  int arch = 0;
  Id first = 0;
  w.techs.each([&](const Tech& t) {
    if (t.faction || t.cost.empty()) return;
    arch++;
    if (t.prereqs.empty()) first = t.id;
  });
  CHECK_EQ(arch, 10);
  CHECK(first && w.tech(first)->needKeys == std::vector<std::string>{schema::bld::ArchGuild});
  // Постройки: порт (5 уровней), верфь (3 уровня, порт 4 уровня), храмы и соборы эссенций.
  int temples = 0, cathedrals = 0;
  const Building* port = nullptr;
  const Building* yard = nullptr;
  w.buildings.each([&](const Building& b) {
    if (b.key == schema::bld::Port) port = &b;
    if (b.key == schema::bld::Shipyard) yard = &b;
    if (startsWith(b.name, "Храм ") && b.essenceGen) temples++;
    if (startsWith(b.name, "Собор ") && b.essenceGen) cathedrals++;
  });
  CHECK(port && port->levels.size() == 5 && port->coastal && port->levels[4].produce.at(kGold) == 30);
  CHECK(yard && yard->levels.size() == 3 && yard->requires_.size() == 1 && yard->requires_[0].level == 4);
  CHECK_EQ(temples, 18);
  CHECK_EQ(cathedrals, 18);
  // Слоты новой провинции: четыре разных места, сокровища из списков наград.
  auto slots = arch::rollSlots(*w.catalogs, arch::provinceSeed(7));
  std::vector<Id> sites;
  for (int i = 0; i < kArchSlots; i++) {
    CHECK(slots[size_t(i)].site != 0);
    CHECK(std::find(sites.begin(), sites.end(), slots[size_t(i)].site) == sites.end());
    sites.push_back(slots[size_t(i)].site);
    const ArchSite* s = w.catalogs->archSite(slots[size_t(i)].site);
    CHECK(s && std::find(s->rewards[size_t(i)].begin(), s->rewards[size_t(i)].end(), slots[size_t(i)].chest) != s->rewards[size_t(i)].end());
  }
}

TEST(io_tz2_slot_chances_redistribute) {
  World w = newWorld("Шансы");
  const Catalogs& c = *w.catalogs;
  // Слот 3 в ТЗ даёт 90 % — недостающие 10 % раздаются поровну целыми (остаток по одному по порядку).
  auto ch = arch::slotChances(c, 2, {});
  int sum = 0;
  for (auto& [id, p] : ch) sum += p;
  CHECK_EQ(sum, 100);
  CHECK_EQ(ch.size(), size_t(6));
  CHECK_EQ(ch[0].second, 23);
  CHECK_EQ(ch[5].second, 4);
  // Место, занятое прошлым слотом, убирается; его процент — оставшимся.
  const Id tomb = c.archSiteByKey(arch::site::Tomb)->id;
  auto last = arch::slotChances(c, 3, {tomb});
  CHECK_EQ(last.size(), size_t(2));
  CHECK_EQ(last[0].second + last[1].second, 100);
  CHECK_EQ(last[0].second, 68);   // 45 + 23
  CHECK_EQ(last[1].second, 32);   // 10 + 22
}

TEST(io_tz2_normalize_fixes) {
  World w = tz2World();
  Tx tx(w);
  // Талантов больше, чем уровней героя.
  tx.character(1).level = 3;
  // Повтор места в слотах провинции и этап больше числа этапов.
  Province& p3 = tx.province(3);
  p3.arch[1].site = p3.arch[0].site;
  p3.arch[0].open = true;
  p3.arch[0].stage = 99;
  // Реликвия сразу у государства и в тайнике.
  tx.province(1).hiddenRelic = tx.w().faction(1)->relics[0];
  // Торговый галеон во флоте на карте.
  tx.faction(1).fleet[0].type = ShipType::Galleon;
  // Наёмники-звери.
  tx.faction(1).army.back().type = UnitType::Beasts;
  // Цена фрегата ниже базовой; эссенция генерации без записи справочника.
  for (Constant& k : tx.constants().list)
    if (k.key == schema::cst::FrigateCost) k.res[kGold] = 1;
  tx.modifier(1).essGen[9999] = 3;
  // Сундук сам в себе.
  Chest& box = tx.catalogs().chests[0];
  box.items.push_back(ChestItem{ChestItemKind::Chest, 1, 0, 0, {}, 0, 0, 0, {box.id}});
  World bad = std::move(tx).finish();
  io::Warnings warns;
  io::normalize(bad, warns);
  CHECK_EQ(bad.character(1)->talents.size(), size_t(1));   // уровень 3: хватает на первый талант (2 очка), не на второй (5)
  CHECK(hasWarning(warns, "data/characters.json", "c1.talents"));
  const Province* q = bad.province(3);
  CHECK(q->arch[0].site != q->arch[1].site);
  CHECK(q->arch[0].stage <= arch::stagesOf(0));
  CHECK_EQ(bad.province(1)->hiddenRelic, Id(0));
  CHECK(bad.army(2)->groups[0].units.empty());
  CHECK(!bad.faction(1)->army.back().merc);
  CHECK_EQ(bad.constants->find(schema::cst::FrigateCost)->res.at(kGold), 25.0);
  CHECK(!bad.modifier(1)->essGen.count(9999));
  bool loop = false;
  for (const ChestItem& it : bad.catalogs->chests[0].items) loop = loop || std::count(it.chests.begin(), it.chests.end(), bad.catalogs->chests[0].id);
  CHECK(!loop);
}

// Мир прежней версии: нет археологических мест и сундуков в справочнике, у провинций пустые слоты — за одно чтение
// справочник дополняется, а каждая провинция получает четыре разных места; повторное чтение их не меняет.
TEST(io_tz2_old_world_gets_arch_slots) {
  World w = richWorld();
  {
    Tx tx(w);
    tx.catalogs().archSites.clear();
    tx.catalogs().chests.clear();
    tx.meta().content = 1;
    for (Id id : tx.w().provinces.ids()) tx.province(id).arch = {};
    w = std::move(tx).finish();
  }
  const std::string dir = tempDir("tz2_old_arch");
  io::save(dir, w, TB_ALL);
  io::LoadResult r = io::load(dir);
  const World& x = r.world;
  CHECK_EQ(x.catalogs->archSites.size(), size_t(10));
  CHECK(!x.catalogs->chests.empty());
  CHECK(x.provinces.size() > 0);
  x.provinces.each([&](const Province& p) {
    std::vector<Id> sites;
    for (const ArchSlot& s : p.arch) {
      CHECK(s.site != 0 && !s.open && s.stage == 0);
      CHECK(std::find(sites.begin(), sites.end(), s.site) == sites.end());
      sites.push_back(s.site);
    }
  });
  const std::string dir2 = tempDir("tz2_old_arch_again");
  io::save(dir2, x, TB_ALL);
  io::LoadResult r2 = io::load(dir2);
  CHECK_MSG(r2.warnings.empty(), warningsText(r2.warnings));
  CHECK_EQ(io::toJson(x), io::toJson(r2.world));
}
