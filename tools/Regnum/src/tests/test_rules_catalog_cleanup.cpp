// Regnum — удаление ресурса и эссенции из справочника (rules::removeCatalogItem): ссылки на запись убираются отовсюду,
// где их проверяет нормализация, — следующее чтение мира проходит без исправлений и без висячих ссылок.
#include "core/io.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rulestest;
using rules::CatalogList;

namespace {

std::string text(const io::Warnings& ws) {
  std::string s;
  for (const io::Warning& w : ws) s += w.text() + "\n";
  return s;
}

// Нормализация копии мира: предупреждения и маска исправленных таблиц.
struct Normalized {
  io::Warnings warns;
  u32 mask = 0;
};
Normalized normalized(World w) {
  Normalized r;
  r.mask = io::normalize(w, r.warns);
  return r;
}

bool hasKey(const std::map<Id, double>& m, Id id) { return m.count(id) > 0; }

// Мир, где ресурсы ride (ездовой — ключевой ресурс кавалерии) и sea (морское чудовище — ключевой ресурс флота) и эссенция
// ess используются всюду, где ссылки проверяет нормализация.
struct Refs {
  World w;
  Id ride = 0, sea = 0, ess = 0;
  Id building = 0, deal = 0, dealOnly = 0, techCommon = 0, techOwn = 0, modifier = 0, chest = 0;
  std::string constant;
};

Refs makeRefs() {
  World w0 = base().w;
  io::Warnings pre;
  io::normalize(w0, pre);   // базовый мир тестов — в нормальном виде
  Store s;
  s.replace(w0, "test");
  Refs r;
  const Base& b = base();
  s.transact("refs", [&](Tx& tx) {
    r.ride = rules::addCatalogItem(tx, CatalogList::Resources, "Проверочные скакуны");
    rules::setResourceGroup(tx, r.ride, tx.w().catalogs->groupId(schema::grp::MountsGround));
    r.sea = rules::addCatalogItem(tx, CatalogList::Resources, "Проверочные левиафаны");
    rules::setResourceGroup(tx, r.sea, tx.w().catalogs->groupId(schema::grp::SeaMonsters));
    r.ess = rules::addCatalogItem(tx, CatalogList::Essences, "Эссенция проверки");
    const Id ride = r.ride, sea = r.sea, ess = r.ess;

    // Постройка: цена, производство, генерация и цена эссенцией, рецепт преобразования.
    Building bl;
    bl.name = "Проверочная мастерская";
    bl.essenceGen = true;
    bl.convert = true;
    bl.levels[0].cost = {{ride, 1}, {sea, 1}};
    bl.levels[0].produce = {{ride, 2}};
    bl.levels[0].essence = {{ess, 1}};
    bl.levels[0].essCost = {{ess, 2}};
    Recipe rc;
    rc.in = {{ride, 1}, {sea, 1}};
    rc.out = {ride, 1};
    bl.recipes = {rc};
    r.building = tx.add(bl).id;

    // Провинция: ресурс, строящаяся постройка с уплаченным.
    Province& p = tx.province(b.p[0]);
    p.owner = b.A;
    p.resource = ride;
    p.resourceAmount = 5;
    ProvBuilding pb;
    pb.building = r.building;
    pb.level = 1;
    pb.constructing = true;
    pb.left = 1;
    pb.paid = {{ride, 1}, {sea, 1}, {kGold, 1}};
    pb.payer = b.A;
    pb.paidEss = {{ess, 1}};
    p.buildings.push_back(pb);

    // Технологии: общая (изучение государством с уплаченным) и своя.
    r.techCommon = rules::createTech(tx, 0, "Проверочная общая");
    tx.tech(r.techCommon).cost = {{ride, 5}, {sea, 5}};
    r.techOwn = rules::createTech(tx, b.A, "Проверочная своя");
    Tech& own = tx.tech(r.techOwn);
    own.cost = {{ride, 1}};
    own.research = true;
    own.paid = {{ride, 1}};

    // Государство: запасы, строки армии и флота, формирование, изучение общей технологии.
    const Id cav = rules::addArmyRow(tx, b.A, UnitType::LightCav, "Проверочная конница");
    const Id elem = rules::addArmyRow(tx, b.A, UnitType::Elementals, "Проверочные элементали");
    const Id ships = rules::addFleetRow(tx, b.A, ShipType::SeaMonster, "Проверочные левиафаны");
    Faction& f = tx.faction(b.A);
    f.res[ride] = 10;
    f.res[sea] = 3;
    f.ess[ess] = 7;
    for (ArmyRow& row : f.army) {
      if (row.id == cav) {
        row.keyRes = ride;
        row.extra = {{ride, 2}, {sea, 1}};
        row.essence = {{ess, 3}};
      }
      if (row.id == elem) {
        row.essence = {{ess, 1}};
        row.essUpkeep = {{ess, 1}};
      }
    }
    for (FleetRow& row : f.fleet)
      if (row.id == ships) row.keyRes = sea;
    Formation q;
    q.row = cav;
    q.count = 10;
    q.left = 1;
    q.paid = {{ride, 1}, {sea, 1}};
    q.paidEss = {{ess, 1}};
    f.forming.push_back(q);
    TechProgress tp;
    tp.research = true;
    tp.paid = {{ride, 5}, {sea, 1}};
    f.techs[r.techCommon] = tp;

    // Модификатор с генерацией.
    r.modifier = rules::createModifier(tx, "Проверочный");
    tx.modifier(r.modifier).resGen = {{ride, 1}, {sea, 1}};
    tx.modifier(r.modifier).essGen = {{ess, 1}};

    // Константа-список ресурсов с базовой стоимостью.
    r.constant = rules::addConstant(tx, ConstType::Resources, "Проверочная цена");
    for (Constant& k : tx.constants().list)
      if (k.key == r.constant) {
        k.res = {{ride, 1}, {sea, 2}};
        k.ess = {{ess, 1}};
        k.minRes = {{ride, 1}, {sea, 1}};
        k.minEss = {{ess, 1}};
      }

    // Сделки: со смешанными позициями и только с удаляемым ресурсом.
    Deal d;
    d.a = b.A;
    d.b = b.B;
    DealItem it;
    it.from = DealSide::A;
    it.res = ride;
    it.amount = 1;
    d.items.push_back(it);
    it.from = DealSide::B;
    it.res = kGold;
    d.items.push_back(it);
    r.deal = tx.add(d).id;
    Deal only;
    only.a = b.A;
    only.b = b.B;
    it.res = sea;
    only.items.push_back(it);
    r.dealOnly = tx.add(only).id;

    // Справочники: особые отряды (обычный и элементали), сундук сокровищ.
    Catalogs& c = tx.catalogs();
    SpecialUnit su;
    su.id = tx.nextId(Seq::Special);
    su.name = "Проверочный отряд";
    su.type = UnitType::LightCav;
    su.keyRes = ride;
    su.extra = {{ride, 1}, {sea, 1}};
    su.essence = {{ess, 1}};
    c.specials.push_back(su);
    SpecialUnit se;
    se.id = tx.nextId(Seq::Special);
    se.name = "Проверочные элементали";
    se.type = UnitType::Elementals;
    se.essence = {{ess, 1}};
    se.essUpkeep = {{ess, 1}};
    c.specials.push_back(se);
    Chest ch;
    ch.id = tx.nextId(Seq::Chest);
    ch.name = "Проверочный сундук";
    ChestItem a;
    a.kind = ChestItemKind::Resource;
    a.amount = 1;
    a.res = ride;
    ch.items.push_back(a);
    ChestItem g;
    g.kind = ChestItemKind::Resource;
    g.amount = 1;
    g.group = c.groupId(schema::grp::Beasts);
    g.exclude = {ride, sea};
    ch.items.push_back(g);
    ChestItem e;
    e.kind = ChestItemKind::Essence;
    e.amount = 1;
    e.essence = ess;
    ch.items.push_back(e);
    r.chest = ch.id;
    c.chests.push_back(ch);
  });
  r.w = s.world();
  return r;
}

// Ссылки на ресурс в мире (кроме самого справочника ресурсов).
std::vector<std::string> resourceUses(const World& w, Id res) {
  std::vector<std::string> out;
  auto note = [&](bool used, const std::string& where) {
    if (used) out.push_back(where);
  };
  w.provinces.each([&](const Province& p) {
    note(p.resource == res, "province.resource");
    for (const ProvBuilding& b : p.buildings) note(hasKey(b.paid, res), "province.buildings.paid");
  });
  w.factions.each([&](const Faction& f) {
    note(hasKey(f.res, res), "faction.res");
    for (const Formation& q : f.forming) note(hasKey(q.paid, res), "faction.forming.paid");
    for (const auto& [t, s] : f.techs) note(hasKey(s.paid, res), "faction.techs.paid");
    for (const ArmyRow& r : f.army) note(r.keyRes == res || hasKey(r.extra, res), "faction.army");
    for (const FleetRow& r : f.fleet) note(r.keyRes == res, "faction.fleet.keyRes");
  });
  w.buildings.each([&](const Building& b) {
    for (const BuildingLevel& l : b.levels) note(hasKey(l.cost, res) || hasKey(l.produce, res), "building.levels");
    for (const Recipe& rc : b.recipes) {
      note(rc.out.res == res, "building.recipe.out");
      for (const ResAmount& x : rc.in) note(x.res == res, "building.recipe.in");
    }
  });
  w.techs.each([&](const Tech& t) { note(hasKey(t.cost, res) || hasKey(t.paid, res), "tech"); });
  w.modifiers.each([&](const Modifier& m) { note(hasKey(m.resGen, res), "modifier.resGen"); });
  w.deals.each([&](const Deal& d) {
    for (const DealItem& i : d.items) note(i.res == res, "deal.items");
  });
  for (const Constant& k : w.constants->list) note(hasKey(k.res, res) || hasKey(k.minRes, res), "constant");
  for (const SpecialUnit& s : w.catalogs->specials) note(s.keyRes == res || hasKey(s.extra, res), "special");
  for (const Chest& c : w.catalogs->chests)
    for (const ChestItem& i : c.items) note(i.res == res || std::count(i.exclude.begin(), i.exclude.end(), res) > 0, "chest");
  return out;
}

std::vector<std::string> essenceUses(const World& w, Id ess) {
  std::vector<std::string> out;
  auto note = [&](bool used, const std::string& where) {
    if (used) out.push_back(where);
  };
  w.provinces.each([&](const Province& p) {
    for (const ProvBuilding& b : p.buildings) note(hasKey(b.paidEss, ess), "province.buildings.paidEss");
  });
  w.factions.each([&](const Faction& f) {
    note(hasKey(f.ess, ess), "faction.ess");
    for (const Formation& q : f.forming) note(hasKey(q.paidEss, ess), "faction.forming.paidEss");
    for (const ArmyRow& r : f.army) note(hasKey(r.essence, ess) || hasKey(r.essUpkeep, ess), "faction.army");
  });
  w.buildings.each([&](const Building& b) {
    for (const BuildingLevel& l : b.levels) note(hasKey(l.essence, ess) || hasKey(l.essCost, ess), "building.levels");
  });
  w.modifiers.each([&](const Modifier& m) { note(hasKey(m.essGen, ess), "modifier.essGen"); });
  for (const Constant& k : w.constants->list) note(hasKey(k.ess, ess) || hasKey(k.minEss, ess), "constant");
  for (const SpecialUnit& s : w.catalogs->specials) note(hasKey(s.essence, ess) || hasKey(s.essUpkeep, ess), "special");
  for (const Chest& c : w.catalogs->chests)
    for (const ChestItem& i : c.items) note(i.essence == ess, "chest");
  return out;
}

std::string joined(const std::vector<std::string>& v) { return join(v, ", "); }

}  // namespace

// Подготовленный мир сам по себе нормален: все ссылки законны, иначе проверка ниже ничего бы не доказывала.
TEST(rules_catalog_cleanup_fixture_is_normal) {
  const Refs r = makeRefs();
  const Normalized n = normalized(r.w);
  CHECK_MSG(n.warns.empty(), text(n.warns));
  CHECK_EQ(n.mask, u32(0));
  CHECK(!resourceUses(r.w, r.ride).empty() && !resourceUses(r.w, r.sea).empty());
  CHECK(!essenceUses(r.w, r.ess).empty());
}

TEST(rules_catalog_cleanup_resource) {
  const Refs r = makeRefs();
  Store s;
  s.replace(r.w, "test");
  s.transact("remove", [&](Tx& tx) {
    rules::removeCatalogItem(tx, CatalogList::Resources, r.ride);
    rules::removeCatalogItem(tx, CatalogList::Resources, r.sea);
  });
  const World& w = s.world();
  CHECK(!w.resource(r.ride) && !w.resource(r.sea));
  CHECK_MSG(resourceUses(w, r.ride).empty(), joined(resourceUses(w, r.ride)));
  CHECK_MSG(resourceUses(w, r.sea).empty(), joined(resourceUses(w, r.sea)));
  // Нормализация и чтение сохранённого мира — без исправлений.
  const Normalized n = normalized(w);
  CHECK_MSG(n.warns.empty(), text(n.warns));
  CHECK_EQ(n.mask, u32(0));
  const io::LoadResult lr = io::fromJson(io::toJson(w));
  CHECK_MSG(lr.warnings.empty(), text(lr.warnings));
  CHECK_EQ(lr.fixedTables, u32(0));
  // Прочее остаётся: золото в уплаченном, позиция золота в сделке; сделка только с удалённым ресурсом отменена.
  CHECK_EQ(w.province(base().p[0])->buildings.at(0).paid.count(kGold), size_t(1));
  CHECK_EQ(w.deal(r.deal)->items.size(), size_t(1));
  CHECK(w.deal(r.deal)->status == DealStatus::Active);
  CHECK(w.deal(r.dealOnly)->items.empty());
  CHECK(w.deal(r.dealOnly)->status == DealStatus::Cancelled);
  CHECK(w.building(r.building)->recipes.at(0).in.empty());
  CHECK_EQ(w.building(r.building)->recipes.at(0).out.res, Id(0));
  // Эссенция не задета.
  CHECK(!essenceUses(w, r.ess).empty());
}

TEST(rules_catalog_cleanup_essence) {
  const Refs r = makeRefs();
  Store s;
  s.replace(r.w, "test");
  const u32 before = s.world().factions.size();
  s.transact("remove", [&](Tx& tx) { rules::removeCatalogItem(tx, CatalogList::Essences, r.ess); });
  const World& w = s.world();
  CHECK(!w.essence(r.ess));
  CHECK_MSG(essenceUses(w, r.ess).empty(), joined(essenceUses(w, r.ess)));
  const Normalized n = normalized(w);
  CHECK_MSG(n.warns.empty(), text(n.warns));
  CHECK_EQ(n.mask, u32(0));
  const io::LoadResult lr = io::fromJson(io::toJson(w));
  CHECK_MSG(lr.warnings.empty(), text(lr.warnings));
  CHECK_EQ(lr.fixedTables, u32(0));
  CHECK_EQ(w.factions.size(), before);
  // Ресурсы не задеты.
  CHECK(!resourceUses(w, r.ride).empty());
}

// Записи, которые не ссылались на удалённую эссенцию, не переписываются (таблицы без изменений сохранять не нужно).
TEST(rules_catalog_cleanup_untouched_tables) {
  const Refs r = makeRefs();
  Store s;
  s.replace(r.w, "test");
  Id unused = 0;
  s.transact("add", [&](Tx& tx) { unused = rules::addCatalogItem(tx, CatalogList::Essences, "Неиспользуемая эссенция"); });
  const World before = s.world();
  s.transact("remove", [&](Tx& tx) { rules::removeCatalogItem(tx, CatalogList::Essences, unused); });
  CHECK_EQ(World::diff(before, s.world()), u32(TB_CATALOGS));
}
