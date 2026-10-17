// Regnum — нормализация: справочники (простые списки, группы ресурсов и реликвий, реликвии, особые отряды, сундуки,
// археологические места, классы героев) и глобальные константы.
#include "core/arch.h"
#include "core/content.h"
#include "core/io_normalize.h"

namespace rg::io::norm {

void Norm::catalogs() {
  Catalogs c = *w().catalogs;
  bool ch = false;
  for (const schema::CatalogListInfo& L : schema::kCatalogLists) ch |= fixIds(c.*L.items, L.seq, L.key, "запись");
  // Встроенный ресурс «Золото» (казна) — всегда rs1.
  auto gold = std::find_if(c.resources.begin(), c.resources.end(), [](const CatalogItem& it) { return it.id == kGold; });
  if (gold == c.resources.end()) {
    CatalogItem g;
    g.id = kGold;
    g.name = "Золото";
    g.color = Color::hex(0xe2b33c);
    g.icon = "coins";
    g.builtin = true;
    c.resources.insert(c.resources.begin(), g);
    warn(F_CATALOGS, "resources", "нет встроенного ресурса «Золото» (rs1, казна) — добавлен");
    ch = true;
  } else if (!gold->builtin) {
    gold->builtin = true;
    warn(F_CATALOGS, "resources.rs1", "rs1 — встроенный ресурс «Золото» (казна): отмечен как встроенный");
    ch = true;
  }
  // Ключи встроенных ресурсов (трупы, демоническая энергия, запчасти механизмов): известные, без повторов; «gold» —
  // только у rs1. Прежний ключ «provisions» остаётся до переноса в группу «Провизия» (core/content.cpp).
  {
    std::unordered_set<std::string> seenKey;
    for (auto& it : c.resources) {
      if (it.key.empty()) continue;
      const std::string where = "resources." + refStr(Seq::Resource, it.id) + ".key";
      bool known = it.key == schema::kResLegacyProvisions && w().meta->content < content::kVersion;
      for (const auto& b : schema::kBuiltinResources) known = known || it.key == b.key;
      if (!known || (it.key == schema::kResGold && it.id != kGold)) {
        warn(F_CATALOGS, where, "неизвестный ключ встроенного ресурса «" + it.key + "» — снят");
        it.key.clear();
        ch = true;
      } else if (!seenKey.insert(it.key).second) {
        warn(F_CATALOGS, where, "ключ «" + it.key + "» уже у другого ресурса — снят");
        it.key.clear();
        ch = true;
      }
    }
    // Ресурс с названием встроенного без ключа становится встроенным (запасы и производство сохраняются).
    for (const auto& b : schema::kBuiltinResources) {
      if (b.key == std::string_view(schema::kResGold) || seenKey.count(b.key)) continue;
      auto hit = std::find_if(c.resources.begin(), c.resources.end(), [&](const CatalogItem& it) {
        return it.key.empty() && it.id != kGold && utf8::searchKey(it.name) == utf8::searchKey(b.name);
      });
      if (hit == c.resources.end()) continue;
      warn(F_CATALOGS, "resources." + refStr(Seq::Resource, hit->id), "ресурс «" + hit->name + "» отмечен как встроенный");
      hit->key = b.key;
      hit->builtin = true;
      seenKey.insert(b.key);
      ch = true;
    }
    for (auto& it : c.resources)
      if (!it.modifiers.empty() || !it.vacantModifiers.empty()) {
        it.modifiers.clear();
        it.vacantModifiers.clear();
        ch = true;
      }
  }
  // Должности: модификаторы занятой и пустующей должности — существующие, без повторов.
  for (auto& it : c.positions) {
    const std::string where = "positions." + refStr(Seq::Position, it.id);
    ch |= fixRefList(it.modifiers, Seq::Modifier, F_CATALOGS, where, "modifiers");
    ch |= fixRefList(it.vacantModifiers, Seq::Modifier, F_CATALOGS, where, "vacantModifiers");
  }
  // Группа бывает только у ресурса.
  for (const schema::CatalogListInfo& L : schema::kCatalogLists)
    if (L.list != CatalogList::Resources)
      for (auto& it : c.*L.items)
        if (it.group) {
          it.group = 0;
          ch = true;
        }
  ch |= resGroups(c);
  ch |= relicGroups(c);
  ch |= relics(c);
  ch |= specials(c);
  ch |= chests(c);
  ch |= archSites(c);
  ch |= classes(c);
  if (ch) tx.catalogs() = std::move(c);
  catalogSets();
}

// Наборы ID справочников для проверки ссылок (после правки справочников и после базовых записей).
void Norm::catalogSets() {
  const Catalogs& c = *w().catalogs;
  for (const schema::CatalogListInfo& L : schema::kCatalogLists) {
    auto& ids = cat[int(L.list)];
    ids.clear();
    for (auto& it : c.*L.items) ids.insert(it.id);
  }
  relicIds.clear();
  specialIds.clear();
  groupIds.clear();
  relicGroupIds.clear();
  classIds.clear();
  siteIds.clear();
  chestIds.clear();
  for (auto& r : c.relics) relicIds.insert(r.id);
  for (auto& s : c.specials) specialIds.insert(s.id);
  for (auto& g : c.resGroups) groupIds.insert(g.id);
  for (auto& g : c.relicGroups) relicGroupIds.insert(g.id);
  for (auto& k : c.classes) classIds.insert(k.id);
  for (auto& s : c.archSites) siteIds.insert(s.id);
  for (auto& x : c.chests) chestIds.insert(x.id);
}

// Группы ресурсов: ключи базовых групп — известные и без повторов; группа ресурса — существующая.
bool Norm::resGroups(Catalogs& c) {
  bool ch = fixTree(c.resGroups, Seq::ResGroup, "resGroups", [](const std::string& k, std::unordered_set<std::string>& keys) {
    bool known = false;
    for (const content::BaseGroup& b : content::baseGroups()) known = known || k == b.key;
    if (known && keys.insert(k).second) return std::string();
    return (known ? "ключ «" + k + "» уже у другой группы" : "неизвестный ключ группы «" + k + "»") + std::string(" — снят");
  });
  std::unordered_set<Id> ids;
  for (auto& g : c.resGroups) ids.insert(g.id);
  for (auto& r : c.resources)
    if (r.group && !ids.count(r.group)) {
      warn(F_CATALOGS, "resources." + refStr(Seq::Resource, r.id) + ".group", "нет группы " + refStr(Seq::ResGroup, r.group) + " — ресурс без группы");
      r.group = 0;
      ch = true;
    }
  return ch;
}

bool Norm::relics(Catalogs& c) {
  bool ch = false;
  u32 maxId = 0;
  for (auto& r : c.relics) maxId = std::max(maxId, r.id);
  std::unordered_set<Id> seen;
  for (auto& r : c.relics) {
    if (r.id == 0 || seen.count(r.id)) {
      Id old = r.id;
      r.id = fresh(Seq::Relic, maxId);
      warn(F_CATALOGS, "relics." + refStr(Seq::Relic, r.id), old ? "повторный ID " + refStr(Seq::Relic, old) + " — назначен новый"
                                                                  : std::string("реликвия без ID — назначен новый"));
      ch = true;
    }
    seen.insert(r.id);
    const std::string where = "relics." + refStr(Seq::Relic, r.id);
    ch |= fixEnum(r.rarity, int(Rarity::Count), Rarity::Common, F_CATALOGS, where, "rarity");
    if (r.group && !c.relicGroup(r.group)) {
      warn(F_CATALOGS, where + ".group", "нет группы реликвий " + refStr(Seq::RelicGroup, r.group) + " — реликвия без группы");
      r.group = 0;
      ch = true;
    }
    if (!r.image.empty() && !isPng(r.image) && !isJpeg(r.image)) {
      warn(F_CATALOGS, where + ".image", "изображение не является PNG или JPEG — удалено");
      r.image.clear();
      ch = true;
    }
  }
  return ch;
}

// Группы реликвий: ключ — только «Археологические находки», без повторов.
bool Norm::relicGroups(Catalogs& c) {
  return fixTree(c.relicGroups, Seq::RelicGroup, "relicGroups", [](const std::string& k, std::unordered_set<std::string>& keys) {
    if (k == schema::kRelicArchFinds && keys.insert(k).second) return std::string();
    return "неизвестный или повторный ключ группы «" + k + "» — снят";
  });
}

// Сундуки сокровищ: поля своего вида, ссылки на существующие записи, вложенные сундуки — без циклов.
bool Norm::chests(Catalogs& c) {
  bool ch = fixIds(c.chests, Seq::Chest, "chests", "сундук");
  std::unordered_set<Id> ids, res, ess, rgroups, groups;
  for (auto& x : c.chests) ids.insert(x.id);
  for (auto& r : c.resources) res.insert(r.id);
  for (auto& e : c.essences) ess.insert(e.id);
  for (auto& g : c.relicGroups) rgroups.insert(g.id);
  for (auto& g : c.resGroups) groups.insert(g.id);
  std::unordered_set<std::string> keys;
  for (auto& x : c.chests) {
    const std::string where = "chests." + refStr(Seq::Chest, x.id);
    if (!x.key.empty() && !keys.insert(x.key).second) {
      warn(F_CATALOGS, where + ".key", "ключ «" + x.key + "» уже у другого сундука — снят");
      x.key.clear();
      ch = true;
    }
    for (size_t i = 0; i < x.items.size(); i++) {
      ChestItem& it = x.items[i];
      const std::string at = where + ".items[" + std::to_string(i) + "]";
      ch |= fixEnum(it.kind, int(ChestItemKind::Count), ChestItemKind::Treasure, F_CATALOGS, at, "kind");
      ch |= fixNum(it.amount, 0.0, 1e12, 0.0, F_CATALOGS, at, "amount");
      ChestItem keep;
      keep.kind = it.kind;
      keep.amount = it.amount;
      switch (it.kind) {
        case ChestItemKind::Resource:
          keep.res = res.count(it.res) ? it.res : 0;
          keep.group = groups.count(it.group) ? it.group : 0;
          for (Id r : it.exclude)
            if (res.count(r) && std::find(keep.exclude.begin(), keep.exclude.end(), r) == keep.exclude.end()) keep.exclude.push_back(r);
          break;
        case ChestItemKind::Essence: keep.essence = ess.count(it.essence) ? it.essence : 0; break;
        case ChestItemKind::Relic:
          keep.relicGroup = rgroups.count(it.relicGroup) ? it.relicGroup : 0;
          keep.rarities = it.rarities & ((1u << int(Rarity::Count)) - 1);
          break;
        case ChestItemKind::Chest:
          for (Id k : it.chests)
            if (ids.count(k) && std::find(keep.chests.begin(), keep.chests.end(), k) == keep.chests.end()) keep.chests.push_back(k);
          break;
        default: break;
      }
      if (!(keep == it)) {
        warn(F_CATALOGS, at, "поле сундука исправлено: ссылки на несуществующие записи и поля другого вида удалены");
        it = keep;
        ch = true;
      }
    }
  }
  // Циклы вложенных сундуков (сундук сам в себе через цепочку) — ссылка, замыкающая цикл, удаляется.
  std::unordered_map<Id, std::vector<Id>> deps;
  for (auto& x : c.chests)
    for (auto& it : x.items)
      if (it.kind == ChestItemKind::Chest)
        for (Id k : it.chests) deps[x.id].push_back(k);
  std::vector<Id> order;
  for (auto& x : c.chests) order.push_back(x.id);
  std::sort(order.begin(), order.end());
  auto cut = cycleEdges(order, [&](Id id) -> const std::vector<Id>& { return deps[id]; });
  for (auto& [from, to] : cut) {
    warn(F_CATALOGS, "chests." + refStr(Seq::Chest, from), "вложенный сундук " + refStr(Seq::Chest, to) + " замыкает цикл — удалён");
    for (auto& x : c.chests)
      if (x.id == from)
        for (auto& it : x.items) it.chests.erase(std::remove(it.chests.begin(), it.chests.end(), to), it.chests.end());
    ch = true;
  }
  return ch;
}

// Археологические места: постоянный список (ключи базовых мест, без повторов), награды — существующие сундуки.
bool Norm::archSites(Catalogs& c) {
  bool ch = fixIds(c.archSites, Seq::ArchSite, "archSites", "место");
  std::unordered_set<Id> ids;
  for (auto& x : c.chests) ids.insert(x.id);
  std::unordered_set<std::string> keys;
  for (auto it = c.archSites.begin(); it != c.archSites.end();) {
    const std::string where = "archSites." + refStr(Seq::ArchSite, it->id);
    bool known = false;
    for (const arch::BaseSite& b : arch::baseSites()) known = known || it->key == b.key;
    if (!known || !keys.insert(it->key).second) {
      warn(F_CATALOGS, where, known ? "место «" + it->key + "» уже есть — повтор удалён" : "археологические места — постоянный список: «" + it->name + "» удалено");
      it = c.archSites.erase(it);
      ch = true;
      continue;
    }
    for (int s = 0; s < kArchSlots; s++) {
      auto& list = it->rewards[size_t(s)];
      std::vector<Id> keep;
      for (Id k : list)
        if (ids.count(k) && std::find(keep.begin(), keep.end(), k) == keep.end()) keep.push_back(k);
      if (keep != list) {
        warn(F_CATALOGS, where + ".rewards[" + std::to_string(s) + "]", "несуществующие и повторные сундуки удалены");
        list = std::move(keep);
        ch = true;
      }
    }
    ++it;
  }
  return ch;
}

namespace {
// Название, не совпадающее с занятыми (сравнение без регистра): «Имя», «Имя 2», «Имя 3»…
std::string freeName(const std::vector<std::string>& taken, const std::string& base) {
  auto used = [&](const std::string& n) {
    for (const std::string& t : taken)
      if (utf8::searchKey(t) == utf8::searchKey(n)) return true;
    return false;
  };
  if (!used(base)) return base;
  for (int i = 2;; i++)
    if (std::string n = base + " " + std::to_string(i); !used(n)) return n;
}
}  // namespace

// Классы героев: подклассы с уникальными ID и названиями внутри класса, очки на ярус 1…60; таланты подклассов — с
// уникальными ID во всём мире, ярус и столбец в пределах дерева, ранги 1…5, стоимость ранга 1…5, предшествующий талант —
// того же подкласса и выше в дереве (циклов нет), модификаторы — существующие.
bool Norm::classes(Catalogs& c) {
  bool ch = fixIds(c.classes, Seq::HeroClass, "classes", "класс");
  u32 maxTalent = 0, maxSub = 0;
  for (auto& k : c.classes)
    for (auto& sc : k.subclasses) {
      maxSub = std::max(maxSub, sc.id);
      for (auto& t : sc.talents) maxTalent = std::max(maxTalent, t.id);
    }
  std::unordered_set<Id> usedTalents, usedSubs;
  for (auto& k : c.classes) {
    const std::string where = "classes." + refStr(Seq::HeroClass, k.id);
    std::vector<std::string> names;
    for (size_t si = 0; si < k.subclasses.size(); si++) {
      SubClass& sc = k.subclasses[si];
      if (sc.id == 0 || usedSubs.count(sc.id)) {
        Id old = sc.id;
        sc.id = fresh(Seq::SubClass, maxSub);
        if (old) warn(F_CATALOGS, where + ".subclasses[" + std::to_string(si) + "]", "повторный ID " + refStr(Seq::SubClass, old) + " — назначен " + refStr(Seq::SubClass, sc.id));
        ch = true;
      }
      usedSubs.insert(sc.id);
      const std::string sw = where + ".subclasses." + refStr(Seq::SubClass, sc.id);
      if (trim(sc.name).empty()) {
        sc.name = freeName(names, "Подкласс");
        warn(F_CATALOGS, sw + ".name", "подкласс без названия — назван «" + sc.name + "»");
        ch = true;
      }
      for (const std::string& n : names)
        if (utf8::searchKey(n) == utf8::searchKey(sc.name)) {
          const std::string was = sc.name;
          sc.name = freeName(names, sc.name);
          warn(F_CATALOGS, sw + ".name", "повтор названия «" + was + "» в классе — назван «" + sc.name + "»");
          ch = true;
          break;
        }
      names.push_back(sc.name);
      ch |= fixNum(sc.tierPoints, 1, schema::kMaxTierPoints, schema::kDefaultTierPoints, F_CATALOGS, sw, "tierPoints");
      for (size_t i = 0; i < sc.talents.size(); i++) {
        Talent& t = sc.talents[i];
        const std::string at = sw + ".talents[" + std::to_string(i) + "]";
        if (t.id == 0 || usedTalents.count(t.id)) {
          Id old = t.id;
          t.id = fresh(Seq::Talent, maxTalent);
          warn(F_CATALOGS, at, old ? "повторный ID " + refStr(Seq::Talent, old) + " — назначен " + refStr(Seq::Talent, t.id)
                                   : "талант без ID — назначен " + refStr(Seq::Talent, t.id));
          ch = true;
        }
        usedTalents.insert(t.id);
        ch |= fixNum(t.row, 0, schema::kTalentRows - 1, 0, F_CATALOGS, at, "row");
        ch |= fixNum(t.col, 0, schema::kTalentCols - 1, 0, F_CATALOGS, at, "col");
        ch |= fixNum(t.cost, schema::kMinTalentCost, schema::kMaxTalentCost, 1, F_CATALOGS, at, "cost");
        ch |= fixNum(t.ranks, 1, schema::kMaxTalentRanks, 1, F_CATALOGS, at, "ranks");
        ch |= fixRefList(t.modifiers, Seq::Modifier, F_CATALOGS, at, "modifiers");
      }
      // В ячейке дерева — один талант: повтор переносится в ближайшую свободную ячейку ниже.
      {
        std::set<std::pair<int, int>> taken;
        for (auto& t : sc.talents) {
          if (taken.insert({t.row, t.col}).second) continue;
          for (int cell = t.row * schema::kTalentCols; cell < schema::kTalentRows * schema::kTalentCols; cell++)
            if (!taken.count({cell / schema::kTalentCols, cell % schema::kTalentCols})) {
              t.row = cell / schema::kTalentCols;
              t.col = cell % schema::kTalentCols;
              break;
            }
          taken.insert({t.row, t.col});
          warn(F_CATALOGS, sw + ".talents." + refStr(Seq::Talent, t.id),
               "ячейка дерева занята — талант перенесён в ярус " + std::to_string(t.row + 1) + ", столбец " + std::to_string(t.col + 1));
          ch = true;
        }
      }
      for (auto& t : sc.talents) {
        if (t.prereq && (t.prereq == t.id || !sc.talent(t.prereq))) {
          warn(F_CATALOGS, sw + ".talents." + refStr(Seq::Talent, t.id) + ".prereq", "предшествующий талант не из дерева подкласса — снят");
          t.prereq = 0;
          ch = true;
        }
        // Условие — из яруса выше (правило дерева талантов; заодно исключает циклы).
        if (const Talent* q = t.prereq ? sc.talent(t.prereq) : nullptr; q && q->row >= t.row) {
          warn(F_CATALOGS, sw + ".talents." + refStr(Seq::Talent, t.id) + ".prereq", "предшествующий талант не выше в дереве — снят");
          t.prereq = 0;
          ch = true;
        }
      }
      for (auto& t : sc.talents)
        if (chainReaches(t.prereq, t.id, 256, [&](Id q) { const Talent* x = sc.talent(q); return x ? x->prereq : Id(0); })) {
          warn(F_CATALOGS, sw + ".talents." + refStr(Seq::Talent, t.id) + ".prereq", "таланты замыкаются в цикл — снят");
          t.prereq = 0;
          ch = true;
        }
    }
  }
  return ch;
}

bool Norm::specials(Catalogs& c) {
  bool ch = false;
  u32 maxId = 0;
  for (auto& s : c.specials) maxId = std::max(maxId, s.id);
  std::unordered_set<Id> seen, resIds, essIds;
  for (auto& r : c.resources) resIds.insert(r.id);
  for (auto& e : c.essences) essIds.insert(e.id);
  cat[int(CatalogList::Essences)] = essIds;
  for (auto& s : c.specials) {
    if (s.id == 0 || seen.count(s.id)) {
      Id old = s.id;
      s.id = fresh(Seq::Special, maxId);
      warn(F_CATALOGS, "specials." + refStr(Seq::Special, s.id), old ? "повторный ID " + refStr(Seq::Special, old) + " — назначен новый"
                                                                      : std::string("особый отряд без ID — назначен новый"));
      ch = true;
    }
    seen.insert(s.id);
    const std::string where = "specials." + refStr(Seq::Special, s.id);
    ch |= fixEnum(s.type, int(UnitType::Count), UnitType::Monsters, F_CATALOGS, where, "type");
    ch |= fixNum(s.upkeep, 0.0, 1e12, 0.0, F_CATALOGS, where, "upkeep");
    ch |= fixUnitCost(s, c, resIds, F_CATALOGS, where, true);
  }
  return ch;
}

// Глобальные константы: ключи непусты и уникальны; встроенные — своего типа; числа конечны; ресурсы — из справочника.
void Norm::constants() {
  Constants c = *w().constants;
  bool ch = false;
  std::unordered_set<std::string> keys;
  int userN = 0;
  for (auto& k : c.list)
    if (startsWith(k.key, "user")) userN = std::max(userN, int(parseNum(k.key.substr(4)).value_or(0)));
  for (size_t i = 0; i < c.list.size(); i++) {
    Constant& k = c.list[i];
    const std::string where = "constants[" + std::to_string(i) + "]";
    if (k.key.empty() || keys.count(k.key)) {
      std::string old = k.key;
      k.key = "user" + std::to_string(++userN);
      warn(F_CONSTANTS, where, old.empty() ? "константа без ключа — назначен «" + k.key + "»" : "повторный ключ «" + old + "» — назначен «" + k.key + "»");
      ch = true;
    }
    keys.insert(k.key);
    const Constant* b = nullptr;
    for (const Constant& x : schema::builtinConstants())
      if (x.key == k.key) b = &x;
    if (b) {
      if (!k.builtin) { k.builtin = true; ch = true; }
      if (k.type != b->type) {
        warn(F_CONSTANTS, where + ".type", "встроенная константа «" + b->name + "» — тип «" + schema::kConstTypes[int(b->type)].name + "»");
        Constant fixed = *b;
        fixed.name = k.name.empty() ? b->name : k.name;
        k = fixed;
        ch = true;
      }
    } else if (k.builtin) {
      warn(F_CONSTANTS, where + ".builtin", "«" + k.key + "» — не встроенная константа: признак снят");
      k.builtin = false;
      ch = true;
    }
    if (int(k.type) < 0 || int(k.type) >= int(ConstType::Count)) {
      warn(F_CONSTANTS, where + ".type", "недопустимый тип — взято «число»");
      k.type = ConstType::Number;
      ch = true;
    }
    ch |= fixNum(k.num, -1e12, 1e12, 0.0, F_CONSTANTS, where, "num");
    ch |= fixAmounts(k.res, Seq::Resource, resourceIds(), F_CONSTANTS, where, "res");
    ch |= fixAmounts(k.ess, Seq::Essence, essenceIds(), F_CONSTANTS, where, "ess");
    ch |= fixAmounts(k.minRes, Seq::Resource, resourceIds(), F_CONSTANTS, where, "minRes");
    ch |= fixAmounts(k.minEss, Seq::Essence, essenceIds(), F_CONSTANTS, where, "minEss");
    if (k.type != ConstType::Resources && (!k.ess.empty() || !k.minRes.empty() || !k.minEss.empty())) {
      k.ess.clear();
      k.minRes.clear();
      k.minEss.clear();
      ch = true;
    }
    // Базовая цена: не меньше наименьших значений, базовые позиции не удаляются.
    for (auto& [r, v] : k.minRes)
      if (k.res[r] + 1e-9 < v) {
        warn(F_CONSTANTS, where + ".res." + refStr(Seq::Resource, r), "меньше базовой стоимости " + ns(v) + " — поднято");
        k.res[r] = v;
        ch = true;
      }
    for (auto& [e, v] : k.minEss)
      if (k.ess[e] + 1e-9 < v) {
        warn(F_CONSTANTS, where + ".ess." + refStr(Seq::Essence, e), "меньше базовой стоимости " + ns(v) + " — поднято");
        k.ess[e] = v;
        ch = true;
      }
  }
  if (ch) tx.constants() = std::move(c);
}

}  // namespace rg::io::norm
