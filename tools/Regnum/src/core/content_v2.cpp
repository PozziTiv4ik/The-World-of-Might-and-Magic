// Regnum — базовые записи мира, версия 2: валюты и новые ресурсы, религии, сундуки сокровищ и археологические места,
// классы героев, порт, верфь, гильдия наёмников, шантирийские постройки преобразования, особые отряды и постройки
// доступа, храмы и соборы эссенций, ветка технологий «Археология», «Гильдия Археологов» у каждого государства,
// базовая стоимость кораблей, генерация встроенных модификаторов.
#include "core/arch.h"
#include "core/content_seed.h"
#include "core/schema.h"

namespace rg::content::detail {

struct SpecialDef {
  const char* name;
  UnitType type;
  const char* race;
  const char* keyRes = nullptr;          // ключевой ресурс
  double keyPer = 1;
  const char* extra = nullptr;           // дополнительный ресурс (1 на юнит)
  const char* essence = nullptr;         // эссенция на юнит
  double essPer = 0;
  double upkeep = 0;                     // золото на юнит в ход
  double essUpkeep = 0;                  // элементали: эссенции на юнит в ход
};

namespace {

// Ресурсы: морские чудовища, уголь, освящённые, осквернённые и проклятые металлы.
struct NamedIn {
  const char* group;
  Named item;
};
const std::vector<NamedIn>& resourcesV2() {
  static const std::vector<NamedIn> v = {
      {schema::grp::SeaMonsters, {"Левиафаны", 0x2f5f8f, "u-monsters"}},
      {schema::grp::SeaMonsters, {"Кракены", 0x7a3f6a, "u-monsters"}},
      {schema::grp::SeaMonsters, {"Морские драконы", 0x2f8f8a, "u-monsters"}},
      {schema::grp::SeaMonsters, {"Сциллы", 0x5a6f9a, "u-monsters"}},
      {schema::grp::SeaMonsters, {"Морские змеи", 0x3f9f6a, "u-monsters"}},
      {schema::grp::MatRaw, {"Уголь", 0x2e2e33, "stone"}},
      {schema::grp::MatIndustrial, {"Проклятая обсидиановая сталь", 0x3a2440, "hammer"}},
      {schema::grp::OreSpecial, {"Проклятый обсидиан", 0x2a1a33, "gem"}},
      {schema::grp::OreCommon, {"Освященное железо", 0xd8cfa0, "iron"}},
      {schema::grp::MatIndustrial, {"Освященная сталь", 0xe8dfb0, "hammer"}},
      {schema::grp::MatIndustrial, {"Оскверненная сталь", 0x6a2a2a, "hammer"}},
      {schema::grp::OreCommon, {"Оскверненное железо", 0x5a2626, "iron"}},
      {schema::grp::MatIndustrial, {"Багряная сталь", 0xa0262f, "hammer"}},
      {schema::grp::MountsFlying, {"Скакуны алой зари", 0xd0403a, "u-flying"}},
  };
  return v;
}

// Религии.
const char* const kReligionsV2[] = {
    "Ковенант Малассы", "Храм Шалассы", "Церковь Илата", "Синод Арката", "Культ Ургаша",
    "Последователи Сар-Илама", "Орден Асхи", "Шаманы Отца Неба и Матери Земли", "Шаманы темного тотема",
};

// Классы героев.
const char* const kClasses[] = {
    "Воин", "Палладин", "Стрелок", "Жрец", "Друид", "Маг", "Некромант", "Рыцарь Смерти", "Вампир", "Чуматворец",
    "Рыцарь тьмы", "Шаман", "Монах", "Культист", "Рыцарь Дракона", "Рыцарь Господства", "Варвар", "Рыцарь Света Пустоты", "Жнец", "Ассасин",
};

// Значок базового класса героя.
const char* classIcon(std::string_view name) {
  static const std::pair<const char*, const char*> kIcons[] = {
      {"Воин", "swords"}, {"Палладин", "shield"}, {"Стрелок", "bow"}, {"Жрец", "religion"}, {"Друид", "wood"},
      {"Маг", "wand"}, {"Некромант", "skull"}, {"Рыцарь Смерти", "lich"}, {"Вампир", "essence"}, {"Чуматворец", "plague"},
      {"Рыцарь тьмы", "moon"}, {"Шаман", "staff"}, {"Монах", "book"}, {"Культист", "b-cult"}, {"Рыцарь Дракона", "flame"},
      {"Рыцарь Господства", "crown"}, {"Варвар", "war"}, {"Рыцарь Света Пустоты", "sun"}, {"Жнец", "hourglass"}, {"Ассасин", "sword"},
  };
  for (auto& [n, i] : kIcons)
    if (name == n) return i;
  return "hero-class";
}

// Особые отряды и их постройки доступа.
struct AccessDef {
  const char* building;
  const char* icon;
  SpecialDef unit;
  std::vector<std::pair<const char*, double>> cost;   // ресурсы
  const char* essCost = nullptr;         // эссенция цены
  double essAmount = 0;
  const char* tech = nullptr;            // технология, которая открывает постройку (ключ)
};
const std::vector<AccessDef>& accessDefs() {
  static const std::vector<AccessDef> v = {
      {"Мавзолей Короля Лича", "skull",
       {"Драконы-Личи", UnitType::Monsters, schema::kRaceUndead, "Костяные Драконы", 10, nullptr, "Эссенция смерти", 750, 1},
       {{"Проклятая обсидиановая сталь", 5000}}, "Эссенция смерти", 5000},
      {"Огненное гнездовье", "flame",
       {"Фениксы", UnitType::Elementals, schema::kRaceElemental, nullptr, 1, nullptr, "Эссенция пламени", 1000, 0, 10},
       {{"Волшебная руда", 5000}}, "Эссенция пламени", 5000},
      {"Чертоги доблести", "shield",
       {"Эйнхейрии", UnitType::HeavyInf, schema::kRaceLiving, nullptr, 1, "Мифриловый сплав", "Эссенция силы", 1, 0.08},
       {{"Мифриловый сплав", 5000}}, "Эссенция силы", 5000},
      {"Храм чистейшего света", "sun",
       {"Золотые Драконы", UnitType::Monsters, schema::kRaceLiving, "Золотые Драконы", 1, nullptr, "Эссенция света", 900, 1},
       {{"Освященная сталь", 5000}}, "Эссенция света", 5000},
      {"Невидимая библиотека", "book",
       {"Безликие Кукловоды", UnitType::Flying, schema::kRaceLiving, nullptr, 1, "Обсидиановая сталь", "Эссенция тьмы", 1, 0.08},
       {{"Обсидиановая сталь", 5000}}, "Эссенция тьмы", 5000},
      {"Глубинная Бездна", "flame",
       {"Балроги", UnitType::Monsters, schema::kRaceDemonic, "Архидьяволы", 50, nullptr, "Эссенция хаоса", 1500, 10},
       {{"Оскверненная сталь", 10000}}, "Эссенция хаоса", 10000},
      {"Цитадель забытой династии", "castle",
       {"Рыцари забытой династии", UnitType::AirCav, schema::kRaceUndead, "Скакуны алой зари", 1, "Багряная сталь", "Эссенция крови", 10, 0.08},
       {{"Багряная сталь", 5000}}, "Эссенция крови", 5000},
      {"Шантирийская фабрика големов", "factory",
       {"Шантирийские Големы", UnitType::Machines, schema::kRaceMechanical, "Шантирийские запчасти", 150, nullptr, nullptr, 0, 0.1},
       {{"Археологические сокровища", 10000}}, nullptr, 0, "archGuards"},
      {"Шантирийская фабрика титанов", "factory",
       {"Шантирийские Титаны", UnitType::Machines, schema::kRaceMechanical, "Шантирийские запчасти", 1500, nullptr, nullptr, 0, 1},
       {{"Археологические сокровища", 20000}}, nullptr, 0, "archGreatness"},
  };
  return v;
}

// Постройки преобразования ветки «Археология».
struct ConvertDef {
  const char* name;
  double cost;                                         // археологические сокровища
  std::vector<std::pair<const char*, double>> in;
  std::pair<const char*, double> out;
  const char* tech;
};
const std::vector<ConvertDef>& convertDefs() {
  static const std::vector<ConvertDef> v = {
      {"Шантирийский обогатитель", 1000, {{"Кристаллы", 10}}, {"Шантирийские кристаллы", 1}, "archConverters"},
      {"Шантирийская кузня", 2500, {{"Мифриловый сплав", 10}, {"Уголь", 20}, {"Магнитный обсидиан", 10}}, {"Шантирийский сплав", 1}, "archForges"},
      {"Шантирийская мастерская", 5000, {{"Шантирийский сплав", 1}, {"Шантирийские кристаллы", 1}}, {"Шантирийские запчасти", 1}, "archWorkshops"},
  };
  return v;
}

// Ветка общего дерева технологий «Археология»: каждая следующая требует предыдущую, первая — достроенную
// «Гильдию Археологов» государства.
struct TechDef {
  const char* key;
  const char* name;
  double treasures;
  double scrolls;
  int turns;
  const char* modName = nullptr;         // глобальный модификатор технологии
  Fx fx = Fx::Count;
  double fxValue = 0;
  const char* desc = "";
};
const std::vector<TechDef>& techDefs() {
  static const std::vector<TechDef> v = {
      {"archBasics", "Основы Археологии", 100, 0, 1, "Основы археологии", Fx::ArchTreasurePct, 1,
       "Археологические сокровища от действий и наград археологических групп +1 %."},
      {"archTools", "Продвинутые инструменты", 150, 0, 1, "Продвинутые инструменты", Fx::ArchTreasurePct, 1,
       "Археологические сокровища от действий и наград археологических групп +1 %."},
      {"archTrained", "Обученные исследователи", 200, 0, 1, "Обученные исследователи", Fx::ArchStartLevel, 1,
       "Новая археологическая группа начинает со 2 уровня."},
      {"archDiligent", "Старательные искатели", 300, 0, 2, "Старательные искатели", Fx::ArchDiscoveryPct, 10,
       "Шанс обнаружения археологического места +10 % для всех групп."},
      {schema::tech::ArchValues, "Ценности археологии", 500, 0, 2, nullptr, Fx::Count, 0,
       "«Гильдия Археологов» получает модификатор «Ценности археологии»: 25 археологических сокровищ за ход."},
      {"archConverters", "Древние преобразователи", 1000, 1, 3, nullptr, Fx::Count, 0, "Открывает «Шантирийский обогатитель»."},
      {"archForges", "Древние кузни", 2000, 2, 4, nullptr, Fx::Count, 0, "Открывает «Шантирийскую кузню»."},
      {"archWorkshops", "Древние мастерские", 4000, 4, 4, nullptr, Fx::Count, 0, "Открывает «Шантирийскую мастерскую»."},
      {"archGuards", "Стража доисторической Империи", 8000, 8, 4, nullptr, Fx::Count, 0, "Открывает «Шантирийскую фабрику големов»."},
      {"archGreatness", "Величие доисторической Империи", 20000, 20, 5, nullptr, Fx::Count, 0, "Открывает «Шантирийскую фабрику титанов»."},
  };
  return v;
}

// Базовая стоимость кораблей: можно увеличивать и дополнять, но не меньше этой.
struct ShipCostDef {
  const char* key;
  std::vector<std::pair<const char*, double>> res;
  std::vector<std::pair<const char*, double>> ess;
};
const std::vector<ShipCostDef>& shipCosts() {
  static const std::vector<ShipCostDef> v = {
      {schema::cst::GalleonCost, {{"Древесина", 100}, {"Золото", 10}, {"Ткань", 100}}, {}},
      {schema::cst::FrigateCost, {{"Древесина", 250}, {"Золото", 25}, {"Ткань", 250}, {"Железо", 50}}, {}},
      {schema::cst::ShipLineCost, {{"Древесина", 1000}, {"Золото", 100}, {"Ткань", 1000}, {"Сталь", 250}}, {}},
      {schema::cst::SeaMonsterCost, {}, {{"Эссенция воды", 750}}},
  };
  return v;
}

}  // namespace

// Валюты: золото, трупы, демоническая энергия, археологические сокровища, древние свитки Шантири.
void Seeder::currenciesV2() {
  for (const BaseGroup& b : baseGroups()) groupOf(b.key);
  builtinResources();
  const Id cur = groupOf(schema::grp::Currencies);
  for (CatalogItem& c : cat().resources)
    if (!c.group && (c.id == kGold || c.key == schema::kResCorpses || c.key == schema::kResEnergy)) c.group = cur;
  for (const NamedIn& n : resourcesV2()) resource(item(n.item, groupOf(n.group)));
}

// Группа реликвий «Археологические находки».
Id Seeder::archFindsGroup() {
  if (Id g = cat().relicGroupId(schema::kRelicArchFinds)) return g;
  for (RelicGroup& g : cat().relicGroups)
    if (g.key.empty() && key(g.name) == key("Археологические находки")) {
      g.key = schema::kRelicArchFinds;
      return g.id;
    }
  RelicGroup g;
  g.id = tx.nextId(Seq::RelicGroup);
  g.name = "Археологические находки";
  g.key = schema::kRelicArchFinds;
  cat().relicGroups.push_back(g);
  relicGroups++;
  return g.id;
}

// Сундуки сокровищ и археологические места со списками наград.
void Seeder::archV2() {
  const Id finds = archFindsGroup();
  for (const arch::BaseChest& bc : arch::baseChests()) {
    if (cat().chestByKey(bc.key)) continue;
    Chest c;
    c.id = tx.nextId(Seq::Chest);
    c.key = bc.key;
    c.name = bc.name;
    cat().chests.push_back(std::move(c));
    chests++;
  }
  for (const arch::BaseChest& bc : arch::baseChests()) {
    Chest* c = nullptr;
    for (Chest& x : cat().chests)
      if (x.key == bc.key) c = &x;
    if (!c || !c->items.empty()) continue;
    for (const arch::BaseChestItem& bi : bc.items) {
      ChestItem it;
      it.kind = bi.kind;
      it.amount = bi.amount;
      switch (bi.kind) {
        case ChestItemKind::Resource:
          if (bi.resName) it.res = resId(bi.resName);
          if (bi.group) it.group = groupOf(bi.group);
          for (const char* ex : bi.exclude)
            if (Id r = resId(ex)) it.exclude.push_back(r);
          break;
        case ChestItemKind::Essence:
          if (bi.resName) it.essence = essId(bi.resName);
          break;
        case ChestItemKind::Relic:
          it.relicGroup = finds;
          it.rarities = bi.rarities;
          break;
        case ChestItemKind::Chest:
          for (const char* k : bi.chests)
            if (const Chest* x = cat().chestByKey(k)) it.chests.push_back(x->id);
          break;
        default: break;
      }
      c->items.push_back(std::move(it));
    }
  }
  for (const arch::BaseSite& bs : arch::baseSites()) {
    if (cat().archSiteByKey(bs.key)) continue;
    ArchSite s;
    s.id = tx.nextId(Seq::ArchSite);
    s.key = bs.key;
    s.name = bs.name;
    s.icon = bs.icon;
    for (int i = 0; i < kArchSlots; i++)
      for (const char* k : arch::baseRewards(bs.key, i))
        if (const Chest* x = cat().chestByKey(k)) s.rewards[size_t(i)].push_back(x->id);
    cat().archSites.push_back(std::move(s));
    sites++;
  }
}

void Seeder::classesV2() {
  int k = int(cat().classes.size());
  for (const char* n : kClasses) {
    if (byName(cat().classes, n)) continue;
    HeroClass c;
    c.id = tx.nextId(Seq::HeroClass);
    c.name = n;
    c.icon = classIcon(n);
    c.color = Color::palette(k++ * 5 + 2);
    cat().classes.push_back(std::move(c));
    classes++;
  }
}

// ---- постройки, технологии, особые отряды
Id Seeder::commonBuilding(const char* name) {
  return tx.w().buildings.firstId([&](const Building& b) { return b.owner == 0 && key(b.name) == key(name); });
}

Seeder::LaneCursor& Seeder::lane(BuildingCat c) {
  LaneCursor& l = lanes[int(c)];
  if (l.y0 < 0) {
    double y = 0;
    bool any = false;
    tx.w().buildings.each([&](const Building& b) {
      if (b.owner != 0 || b.cat != c) return;
      y = any ? std::max(y, b.pos.y) : b.pos.y;
      any = true;
    });
    l.y0 = any ? y + kRowStep : 0;
  }
  return l;
}

Vec2 Seeder::slot(BuildingCat c) {
  LaneCursor& l = lane(c);
  const Vec2 p{double(l.n % kPerRow) * kColStep, l.y0 + double(l.n / kPerRow) * kRowStep};
  l.n++;
  return p;
}

// Следующая группа построек дорожки — с нового ряда.
void Seeder::newRow(BuildingCat c) {
  LaneCursor& l = lane(c);
  if (l.n % kPerRow) l.n += kPerRow - l.n % kPerRow;
}

std::map<Id, double> Seeder::costOf(const std::vector<std::pair<const char*, double>>& list) {
  std::map<Id, double> m;
  for (auto& [n, v] : list)
    if (Id r = resId(n)) m[r] = v;
  return m;
}

Id Seeder::ensureSpecial(const SpecialDef& d) {
  if (SpecialUnit* have = byName(cat().specials, d.name)) return have->id;
  SpecialUnit s;
  s.id = tx.nextId(Seq::Special);
  s.name = d.name;
  s.type = d.type;
  s.race = d.race;
  if (d.keyRes) s.keyRes = resId(d.keyRes);
  s.keyPer = std::max(1.0, d.keyPer);
  if (d.extra)
    if (Id r = resId(d.extra)) s.extra[r] = 1;
  if (d.essence)
    if (Id e = essId(d.essence)) s.essence[e] = d.essPer;
  s.upkeep = d.upkeep;
  if (d.essUpkeep > 0 && d.essence)
    if (Id e = essId(d.essence)) s.essUpkeep[e] = d.essUpkeep;
  const Id id = s.id;
  cat().specials.push_back(std::move(s));
  specials++;
  return id;
}

Id Seeder::modifierNamed(const char* name, Fx fx, double v, const char* desc) {
  if (Id found = tx.w().modifiers.firstId([&](const Modifier& m) { return m.key.empty() && key(m.name) == key(name); })) return found;
  Modifier m;
  m.name = name;
  m.kind = ModKind::Faction;
  m.icon = "pickaxe";
  m.color = Color::hex(0xd4a64a);
  m.desc = desc;
  m.fx[size_t(fx)] = v;
  m.fxMask |= 1u << int(fx);
  modifiers++;
  return tx.add(std::move(m)).id;
}

void Seeder::techsV2(std::map<std::string, Id>& techOf) {
  // Технологии ветки — общие (фракция 0); ниже всех общих технологий.
  double y = 0;
  bool any = false;
  tx.w().techs.each([&](const Tech& t) {
    if (t.faction != 0) return;
    y = any ? std::max(y, t.pos.y) : t.pos.y;
    any = true;
  });
  if (any) y += 160;
  Id prev = 0;
  int col = 0;
  for (const TechDef& d : techDefs()) {
    const Id found =
        tx.w().techs.firstId([&](const Tech& t) { return t.faction == 0 && (t.key == d.key || (t.key.empty() && key(t.name) == key(d.name))); });
    if (found) {
      if (tx.w().tech(found)->key.empty()) tx.tech(found).key = d.key;
      techOf[d.key] = prev = found;
      col++;
      continue;
    }
    Tech t;
    t.faction = 0;
    t.name = d.name;
    t.desc = d.desc;
    t.turns = d.turns;
    t.key = d.key;
    t.pos = Vec2{double(col++) * schema::kTreeColStep, y};
    if (Id r = cat().resourceId(schema::kResArchTreasure); r && d.treasures > 0) t.cost[r] = d.treasures;
    if (Id r = cat().resourceId(schema::kResShantiriScrolls); r && d.scrolls > 0) t.cost[r] = d.scrolls;
    if (prev) t.prereqs.push_back(prev);
    else t.needKeys.push_back(schema::bld::ArchGuild);
    if (d.modName) t.modifiers.push_back(modifierNamed(d.modName, d.fx, d.fxValue, d.desc));
    prev = tx.add(std::move(t)).id;
    techOf[d.key] = prev;
    techs++;
  }
}

void Seeder::buildingsV2() {
  std::map<std::string, Id> techOf;
  techsV2(techOf);
  const Id wood = resId("Древесина"), stone = resId("Камень"), iron = resId("Железо"), steel = resId("Сталь");
  const Id gems = resId("Самоцветы"), treasure = cat().resourceId(schema::kResArchTreasure);
  // Порт: 5 уровней, золото за ход; только в приморской провинции.
  const Id port = ensureBuilding(schema::bld::Port, "Порт", BuildingCat::Economic, "anchor", [&](Building& b) {
    b.coastal = true;
    struct L { double w, s, g, i, st, gold; };
    const L lv[5] = {{100, 100, 20, 0, 0, 5}, {50, 50, 10, 0, 0, 10}, {50, 50, 10, 50, 0, 15}, {100, 100, 20, 100, 0, 20}, {150, 150, 30, 0, 100, 30}};
    b.levels.clear();
    for (const L& l : lv) {
      BuildingLevel x;
      if (wood) x.cost[wood] = l.w;
      if (stone) x.cost[stone] = l.s;
      x.cost[kGold] = l.g;
      if (iron && l.i > 0) x.cost[iron] = l.i;
      if (steel && l.st > 0) x.cost[steel] = l.st;
      x.produce[kGold] = l.gold;
      b.levels.push_back(std::move(x));
    }
  });
  // Верфь: 3 уровня, требует порт 4 уровня; уровни открывают типы кораблей.
  ensureBuilding(schema::bld::Shipyard, "Верфь", BuildingCat::Industrial, "shipyard", [&](Building& b) {
    b.coastal = true;
    b.shipyard = true;
    b.requires_.push_back(BuildingReq{port, 4});
    const u32 gal = 1u << int(ShipType::Galleon), fr = 1u << int(ShipType::Frigate), ln = 1u << int(ShipType::ShipOfLine),
              sm = 1u << int(ShipType::SeaMonster);
    struct L { double w, s, g, st; u32 ships; };
    const L lv[3] = {{100, 100, 20, 0, gal | sm}, {150, 150, 30, 150, gal | fr | sm}, {150, 150, 30, 150, gal | fr | ln | sm}};
    b.levels.clear();
    for (const L& l : lv) {
      BuildingLevel x;
      if (wood) x.cost[wood] = l.w;
      if (stone) x.cost[stone] = l.s;
      x.cost[kGold] = l.g;
      if (steel && l.st > 0) x.cost[steel] = l.st;
      x.ships = l.ships;
      b.levels.push_back(std::move(x));
    }
  });
  // Гильдия наёмников.
  ensureBuilding(schema::bld::MercGuild, "Гильдия Наемников", BuildingCat::Military, "mercenary", [&](Building& b) {
    b.mercenary = true;
    if (wood) b.levels[0].cost[wood] = 200;
    if (stone) b.levels[0].cost[stone] = 150;
    b.levels[0].cost[kGold] = 2;
  });
  // Шантирийские постройки преобразования — с нового ряда дорожки.
  newRow(BuildingCat::Industrial);
  for (const ConvertDef& d : convertDefs()) {
    ensureBuilding(nullptr, d.name, BuildingCat::Industrial, "convert", [&](Building& b) {
      if (treasure) b.levels[0].cost[treasure] = d.cost;
      b.convert = true;
      Recipe rc;
      for (auto& [n, v] : d.in)
        if (Id r = resId(n)) rc.in.push_back(ResAmount{r, v});
      rc.out = ResAmount{resId(d.out.first), d.out.second};
      rc.turns = 1;
      b.recipes.push_back(std::move(rc));
      if (auto it = techOf.find(d.tech); it != techOf.end()) b.techs.push_back(it->second);
    });
  }
  // Постройки доступа к особым отрядам.
  newRow(BuildingCat::Military);
  for (const AccessDef& d : accessDefs()) {
    const Id sp = ensureSpecial(d.unit);
    const Id bid = ensureBuilding(nullptr, d.building, BuildingCat::Military, d.icon, [&](Building& b) {
      b.specialAccess = true;
      b.specials = {sp};
      b.levels[0].cost = costOf(d.cost);
      if (d.essCost)
        if (Id e = essId(d.essCost)) b.levels[0].essCost[e] = d.essAmount;
      if (d.tech)
        if (auto it = techOf.find(d.tech); it != techOf.end()) b.techs.push_back(it->second);
    });
    Building& b = tx.building(bid);
    b.specialAccess = true;
    if (std::find(b.specials.begin(), b.specials.end(), sp) == b.specials.end()) b.specials.push_back(sp);
  }
  // Храмы и соборы эссенций: храм — 10 эссенции за ход, собор — 50; каждый собор требует ещё 4 храма той же
  // эссенции в государстве.
  std::vector<Id> essList;
  for (const CatalogItem& e : cat().essences) essList.push_back(e.id);
  newRow(BuildingCat::Religious);
  std::map<Id, Id> templeOf;
  for (Id e : essList) {
    const CatalogItem* ci = Catalogs::find(cat().essences, e);
    if (!ci || !startsWith(ci->name, "Эссенция ")) continue;
    const std::string suffix = ci->name.substr(std::string("Эссенция ").size());
    const std::string name = "Храм " + suffix;
    templeOf[e] = ensureBuilding(nullptr, name.c_str(), BuildingCat::Religious, "b-religious", [&](Building& b) {
      b.essenceGen = true;
      if (stone) b.levels[0].cost[stone] = 500;
      if (wood) b.levels[0].cost[wood] = 250;
      if (gems) b.levels[0].cost[gems] = 100;
      b.levels[0].essence[e] = 10;
    });
  }
  // Соборы — под храмами той же эссенции (тот же столбец, с нового ряда).
  newRow(BuildingCat::Religious);
  for (Id e : essList) {
    const CatalogItem* ci = Catalogs::find(cat().essences, e);
    if (!ci || !startsWith(ci->name, "Эссенция ") || !templeOf.count(e)) continue;
    const std::string suffix = ci->name.substr(std::string("Эссенция ").size());
    const std::string name = "Собор " + suffix;
    ensureBuilding(nullptr, name.c_str(), BuildingCat::Religious, "religion", [&](Building& b) {
      b.essenceGen = true;
      if (stone) b.levels[0].cost[stone] = 1500;
      if (wood) b.levels[0].cost[wood] = 750;
      if (gems) b.levels[0].cost[gems] = 300;
      b.levels[0].essCost[e] = 150;
      b.levels[0].essence[e] = 50;
      b.stateReqs.push_back(StateReq{templeOf[e], 4});
    });
  }
}

// «Гильдия Археологов» — культовая постройка в уникальном дереве каждого государства.
void Seeder::archGuilds() {
  std::vector<Id> states;
  tx.w().factions.each([&](const Faction& f) {
    if (f.isState()) states.push_back(f.id);
  });
  for (Id s : states) {
    if (tx.w().buildings.findIf([&](const Building& b) { return b.owner == s && b.key == schema::bld::ArchGuild; })) continue;
    tx.add(archGuildFor(s, schema::nextTreePos(tx.w().buildings, [&](const Building& b) { return b.owner == s; })));
    buildings++;
  }
}

// Стоимость кораблей и наименьшие значения; прежние цены не уменьшаются.
void Seeder::shipCostsV2() {
  for (const ShipCostDef& d : shipCosts()) {
    auto& list = tx.constants().list;
    auto it = std::find_if(list.begin(), list.end(), [&](const Constant& c) { return c.key == d.key; });
    if (it == list.end()) {
      for (const Constant& b : schema::builtinConstants())
        if (b.key == d.key) list.push_back(b);
      it = std::find_if(list.begin(), list.end(), [&](const Constant& c) { return c.key == d.key; });
      if (it == list.end()) continue;
    }
    for (auto& [n, v] : d.res)
      if (Id r = resId(n)) {
        it->minRes[r] = v;
        it->res[r] = std::max(it->res[r], v);
      }
    for (auto& [n, v] : d.ess)
      if (Id e = essId(n)) {
        it->minEss[e] = v;
        it->ess[e] = std::max(it->ess[e], v);
      }
  }
}

// Генерация встроенных модификаторов (некромант — эссенция смерти +10): записи мира без генерации дополняются.
void Seeder::builtinGensV2() {
  for (const schema::BuiltinGen& g : schema::builtinGens()) {
    const Id mid = tx.w().modifiers.firstId([&](const Modifier& m) { return m.key == g.key && m.essGen.empty() && m.resGen.empty(); });
    if (!mid) continue;
    const Id ref = g.essence ? essId(g.name) : resId(g.name);
    if (!ref) continue;
    Modifier& m = tx.modifier(mid);
    (g.essence ? m.essGen : m.resGen)[ref] = g.perTurn;
  }
}

void Seeder::v2() {
  currenciesV2();
  addReligions(kReligionsV2, std::size(kReligionsV2));
  archV2();
  classesV2();
  buildingsV2();
  archGuilds();
  shipCostsV2();
  builtinGensV2();
}

}  // namespace rg::content::detail
