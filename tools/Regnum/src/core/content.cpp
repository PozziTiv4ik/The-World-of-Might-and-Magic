// Regnum — базовые записи мира: общие помощники дополнения, базовые группы ресурсов, «Гильдия Археологов», seed.
// Данные и шаги версий — core/content_v1.cpp и core/content_v2.cpp.
#include "core/content.h"

#include "core/content_seed.h"
#include "core/schema.h"

namespace rg::content {

namespace {

const BaseGroup kGroups[] = {
    {schema::grp::Ore, "Руда", nullptr},
    {schema::grp::OreCommon, "Обычная", schema::grp::Ore},
    {schema::grp::OreSpecial, "Особая", schema::grp::Ore},
    {schema::grp::Beasts, "Звери", nullptr},
    {schema::grp::MountsGround, "Ездовые наземные", schema::grp::Beasts},
    {schema::grp::MountsFlying, "Ездовые летающие", schema::grp::Beasts},
    {schema::grp::WarBeasts, "Боевые", schema::grp::Beasts},
    {schema::grp::Monsters, "Чудовища", schema::grp::Beasts},
    {schema::grp::Provisions, "Провизия", nullptr},
    {schema::grp::ProvAgri, "Сельскохозяйственная", schema::grp::Provisions},
    {schema::grp::ProvAnimal, "Животная", schema::grp::Provisions},
    {schema::grp::ProvGather, "Собираемая", schema::grp::Provisions},
    {schema::grp::Materials, "Материалы", nullptr},
    {schema::grp::MatRaw, "Сырьевые", schema::grp::Materials},
    {schema::grp::MatPrecious, "Драгоценные", schema::grp::Materials},
    {schema::grp::MatAlchemy, "Алхимические", schema::grp::Materials},
    {schema::grp::MatIndustrial, "Промышленные", schema::grp::Materials},
    {schema::grp::Currencies, "Валюты", nullptr},
    {schema::grp::SeaMonsters, "Морские чудовища", schema::grp::Beasts},
};

std::string count(int n, const char* what) { return std::string(what) + " (" + std::to_string(n) + ")"; }

}  // namespace

namespace detail {

std::string key(std::string_view s) { return utf8::searchKey(s); }

// ---- группы и ресурсы
Id Seeder::groupOf(const char* gkey) {
  if (!gkey) return 0;
  if (const ResGroup* g = cat().groupByKey(gkey)) return g->id;
  const BaseGroup* bg = nullptr;
  for (const BaseGroup& b : kGroups)
    if (std::string_view(b.key) == gkey) bg = &b;
  if (!bg) return 0;
  const Id parent = groupOf(bg->parent);
  // Группа с тем же названием и родителем без ключа становится базовой.
  for (ResGroup& g : cat().resGroups)
    if (g.key.empty() && g.parent == parent && key(g.name) == key(bg->name)) {
      g.key = bg->key;
      return g.id;
    }
  ResGroup g;
  g.id = tx.nextId(Seq::ResGroup);
  g.name = bg->name;
  g.parent = parent;
  g.key = bg->key;
  cat().resGroups.push_back(g);
  groups++;
  return g.id;
}

CatalogItem Seeder::item(const Named& n, Id group) {
  CatalogItem it;
  it.name = n.name;
  it.color = Color::hex(n.color);
  it.icon = n.icon;
  it.group = group;
  return it;
}

// Ресурс по ключу (встроенный) или названию: существующий без группы получает группу (и ключ); нет — создаётся по
// образцу it.
Id Seeder::resource(CatalogItem it) {
  CatalogItem* found = nullptr;
  if (!it.key.empty())
    for (CatalogItem& c : cat().resources)
      if (c.key == it.key) found = &c;
  if (!found) found = byName(cat().resources, it.name);
  if (found) {
    if (!found->group && it.group) found->group = it.group;
    if (!it.key.empty() && found->key.empty()) found->key = it.key;
    return found->id;
  }
  it.id = tx.nextId(Seq::Resource);
  cat().resources.push_back(std::move(it));
  resources++;
  return cat().resources.back().id;
}

void Seeder::builtinResources() {
  for (const schema::BuiltinResource& b : schema::kBuiltinResources) {
    if (std::string_view(b.key) == schema::kResGold) continue;
    CatalogItem it = schema::builtinResourceItem(b);
    it.group = groupOf(b.group);
    const Id id = resource(std::move(it));
    findById(cat().resources, id)->builtin = true;
  }
}

Id Seeder::resId(const char* name) {
  if (std::string_view(name) == "Золото") return kGold;
  CatalogItem* r = byName(cat().resources, name);
  return r ? r->id : 0;
}

Id Seeder::essId(const char* name) {
  CatalogItem* e = byName(cat().essences, name);
  return e ? e->id : 0;
}

void Seeder::addReligions(const char* const* names, size_t n) {
  int k = int(cat().religions.size());
  for (size_t i = 0; i < n; i++) {
    if (byName(cat().religions, names[i])) continue;
    CatalogItem it;
    it.id = tx.nextId(Seq::Religion);
    it.name = names[i];
    it.color = Color::palette(k++ * 3 + 1);
    it.icon = "religion";
    cat().religions.push_back(std::move(it));
    religions++;
  }
}

}  // namespace detail

const std::vector<BaseGroup>& baseGroups() {
  static const std::vector<BaseGroup> v(std::begin(kGroups), std::end(kGroups));
  return v;
}

Building archGuildFor(Id state, Vec2 pos) {
  Building b;
  b.owner = state;
  b.name = "Гильдия Археологов";
  b.icon = "pickaxe";
  b.cat = BuildingCat::Cult;
  b.key = schema::bld::ArchGuild;
  b.desc = "Открывает археологические группы и ветку технологий «Археология».";
  b.pos = pos;
  return b;
}

Id builtinModifierRecord(Tx& tx, const std::string& key) {
  if (Id id = tx.w().modifiers.firstId([&](const Modifier& m) { return m.key == key; })) return id;
  const Modifier* t = schema::builtinModifier(key);
  if (!t) return 0;
  Modifier m = *t;
  m.id = 0;
  return tx.add(std::move(m)).id;
}

void addIntelBuildings(Tx& tx, Id state) {
  const Faction* f = tx.w().faction(state);
  if (!f || !f->isState()) return;
  auto find = [&](const char* key) { return tx.w().buildings.firstId([&](const Building& b) { return b.owner == state && b.key == key; }); };
  auto costs = [&](Building& b, int levels, const char* modPrefix) {
    b.levels.clear();
    for (int i = 1; i <= levels; i++) {
      BuildingLevel l;
      for (auto& [n, v] : std::vector<std::pair<const char*, double>>{{"Древесина", 100}, {"Камень", 500}, {"Кожа", 500}}) {
        const std::string k = utf8::searchKey(n);
        for (const CatalogItem& c : tx.w().catalogs->resources)
          if (utf8::searchKey(c.name) == k) l.cost[c.id] = v;
      }
      l.cost[kGold] = 50;
      if (Id m = builtinModifierRecord(tx, schema::levelModKey(modPrefix, i))) l.modifiers.push_back(m);
      b.levels.push_back(std::move(l));
    }
  };
  Id intel = find(schema::bld::IntelHq);
  if (!intel) {
    Building b;
    b.owner = state;
    b.name = "Штаб Разведки";
    b.icon = "eye";
    b.cat = BuildingCat::Cult;
    b.key = schema::bld::IntelHq;
    b.desc = "Успех заказных убийств заговоров против других государств: +10 % за уровень.";
    b.pos = schema::nextTreePos(tx.w().buildings, [&](const Building& x) { return x.owner == state; });
    costs(b, 5, schema::mod::IntelLevel);
    intel = tx.add(std::move(b)).id;
  }
  if (!find(schema::bld::CounterIntelHq)) {
    Building b;
    b.owner = state;
    b.name = "Штаб Контрразведки";
    b.icon = "shield";
    b.cat = BuildingCat::Cult;
    b.key = schema::bld::CounterIntelHq;
    b.desc = "Контрразведка: обнаружение и раскрытие заговоров (10 % за уровень), защита героев от заказных убийств.";
    b.pos = schema::nextTreePos(tx.w().buildings, [&](const Building& x) { return x.owner == state; });
    costs(b, 3, schema::mod::CounterIntelLevel);
    b.requires_.push_back(BuildingReq{intel, 4});
    tx.add(std::move(b));
  }
}

std::vector<std::string> seed(Tx& tx) {
  const int from = tx.w().meta->content;
  if (from >= kVersion) return {};
  detail::Seeder s{tx};
  if (from < 1) s.v1();
  if (from < 2) s.v2();
  if (from < 3) s.v3();
  if (from < 4) s.v4();
  if (from < 5) s.v5();
  tx.meta().content = kVersion;
  std::vector<std::string> parts;
  if (s.positions) parts.push_back(count(s.positions, "должности"));
  if (s.essences) parts.push_back(count(s.essences, "эссенции элементов"));
  if (s.groups) parts.push_back(count(s.groups, "группы ресурсов"));
  if (s.resources) parts.push_back(count(s.resources, "ресурсы"));
  if (s.religions) parts.push_back(count(s.religions, "религии"));
  if (s.specials) parts.push_back(count(s.specials, "особые отряды"));
  if (s.buildings) parts.push_back(count(s.buildings, "постройки"));
  if (s.techs) parts.push_back(count(s.techs, "технологии общего дерева"));
  if (s.chests) parts.push_back(count(s.chests, "сундуки сокровищ"));
  if (s.sites) parts.push_back(count(s.sites, "археологические места"));
  if (s.classes) parts.push_back(count(s.classes, "классы героев"));
  if (s.relicGroups) parts.push_back(count(s.relicGroups, "группы реликвий"));
  if (s.modifiers) parts.push_back(count(s.modifiers, "модификаторы"));
  if (s.subclasses) parts.push_back(count(s.subclasses, "подклассы героев"));
  if (s.talents) parts.push_back(count(s.talents, "таланты"));
  if (s.races) parts.push_back(count(s.races, "расы"));
  if (s.cultures) parts.push_back(count(s.cultures, "культуры"));
  if (s.relics) parts.push_back(count(s.relics, "реликвии"));
  std::vector<std::string> out;
  if (!parts.empty()) out.push_back("базовые записи: " + join(parts, ", "));
  for (auto& n : s.notes) out.push_back(n);
  return out;
}

}  // namespace rg::content
