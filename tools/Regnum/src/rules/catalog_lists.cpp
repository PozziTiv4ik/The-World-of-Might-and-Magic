// Regnum — простые списки справочников (ресурсы, расы, культуры, религии, формы правления, должности, эссенции):
// добавление записи и удаление с очисткой всех ссылок.
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

// Записать в транзакцию каждую запись таблицы, которую fix(копия) изменил (остальные записи не трогаются).
template <class T, class Fix>
void fixEach(Tx& tx, const Table<T>& table, T& (Tx::*mut)(Id), Fix&& fix) {
  for (Id id : table.ids()) {
    T copy = *table.get(id);
    if (fix(copy)) (tx.*mut)(id) = std::move(copy);
  }
}

// Убрать позицию key из карты «ссылка → количество»; true — была.
bool dropKey(std::map<Id, double>& m, Id key) { return m.erase(key) > 0; }
// Обнулить одиночную ссылку на key; true — была.
bool dropRef(Id& ref, Id key) {
  if (ref != key) return false;
  ref = 0;
  return true;
}

// Ссылки на удаляемый ресурс убираются отовсюду, где их проверяет нормализация при чтении (одиночные ссылки обнуляются,
// позиции списков и карт удаляются), — следующее чтение мира проходит без исправлений.
void dropResourceRefs(Tx& tx, Id res) {
  fixEach(tx, tx.w().provinces, &Tx::province, [&](Province& p) {
    bool ch = dropRef(p.resource, res);
    for (ProvBuilding& b : p.buildings) ch |= dropKey(b.paid, res);
    return ch;
  });
  fixEach(tx, tx.w().factions, &Tx::faction, [&](Faction& f) {
    bool ch = dropKey(f.res, res);
    for (Formation& q : f.forming) ch |= dropKey(q.paid, res);
    for (auto& [tech, progress] : f.techs) ch |= dropKey(progress.paid, res);
    // Цены найма строк армии, ключевой ресурс строк флота.
    for (ArmyRow& r : f.army) {
      ch |= dropRef(r.keyRes, res);
      ch |= dropKey(r.extra, res);
    }
    for (FleetRow& r : f.fleet) ch |= dropRef(r.keyRes, res);
    return ch;
  });
  fixEach(tx, tx.w().buildings, &Tx::building, [&](Building& b) {
    bool ch = false;
    for (BuildingLevel& l : b.levels) {
      ch |= dropKey(l.cost, res);
      ch |= dropKey(l.produce, res);
      ch |= dropKey(l.produceOnRes, res);
    }
    // Рецепты постройки преобразования и исключения постройки добычи.
    for (Recipe& rc : b.recipes) {
      const size_t in = rc.in.size();
      rc.in.erase(std::remove_if(rc.in.begin(), rc.in.end(), [&](const ResAmount& x) { return x.res == res; }), rc.in.end());
      ch |= rc.in.size() != in;
      const size_t out = rc.out.size();
      rc.out.erase(std::remove_if(rc.out.begin(), rc.out.end(), [&](const ResAmount& x) { return x.res == res; }), rc.out.end());
      ch |= rc.out.size() != out;
    }
    if (contains(b.extractExclude, res)) {
      eraseValue(b.extractExclude, res);
      ch = true;
    }
    return ch;
  });
  fixEach(tx, tx.w().techs, &Tx::tech, [&](Tech& t) {
    bool ch = dropKey(t.cost, res);
    ch |= dropKey(t.paid, res);
    return ch;
  });
  fixEach(tx, tx.w().modifiers, &Tx::modifier, [&](Modifier& m) { return dropKey(m.resGen, res); });
  fixEach(tx, tx.w().deals, &Tx::deal, [&](Deal& d) {
    const size_t n = d.items.size();
    d.items.erase(std::remove_if(d.items.begin(), d.items.end(), [&](const DealItem& i) { return i.res == res; }), d.items.end());
    if (d.items.size() == n) return false;
    if (d.items.empty() && d.status == DealStatus::Active) d.status = DealStatus::Cancelled;
    return true;
  });
  {
    Constants c = *tx.w().constants;
    bool ch = false;
    for (Constant& k : c.list) {
      ch |= dropKey(k.res, res);
      ch |= dropKey(k.minRes, res);
    }
    if (ch) tx.constants() = std::move(c);
  }
  // Справочники: цены особых отрядов, поля сундуков сокровищ (запись справочника удаляется ниже — справочники меняются
  // в любом случае).
  Catalogs& c = tx.catalogs();
  for (SpecialUnit& s : c.specials) {
    dropRef(s.keyRes, res);
    dropKey(s.extra, res);
  }
  for (Chest& ch : c.chests)
    for (ChestItem& it : ch.items) {
      dropRef(it.res, res);
      eraseValue(it.exclude, res);
    }
}

// Ссылки на удаляемую эссенцию — так же, как у ресурса.
void dropEssenceRefs(Tx& tx, Id ess) {
  fixEach(tx, tx.w().provinces, &Tx::province, [&](Province& p) {
    bool ch = false;
    for (ProvBuilding& b : p.buildings) ch |= dropKey(b.paidEss, ess);
    return ch;
  });
  fixEach(tx, tx.w().factions, &Tx::faction, [&](Faction& f) {
    bool ch = dropKey(f.ess, ess);
    for (ArmyRow& r : f.army) {
      ch |= dropKey(r.essence, ess);
      ch |= dropKey(r.essUpkeep, ess);
    }
    for (Formation& q : f.forming) ch |= dropKey(q.paidEss, ess);
    return ch;
  });
  fixEach(tx, tx.w().buildings, &Tx::building, [&](Building& b) {
    bool ch = false;
    for (BuildingLevel& l : b.levels) {
      ch |= dropKey(l.essence, ess);
      ch |= dropKey(l.essCost, ess);
      ch |= dropKey(l.drainEss, ess);
    }
    for (Recipe& rc : b.recipes) {
      const size_t in = rc.essIn.size();
      rc.essIn.erase(std::remove_if(rc.essIn.begin(), rc.essIn.end(), [&](const ResAmount& x) { return x.res == ess; }), rc.essIn.end());
      ch |= rc.essIn.size() != in;
    }
    return ch;
  });
  fixEach(tx, tx.w().modifiers, &Tx::modifier, [&](Modifier& m) { return dropKey(m.essGen, ess); });
  {
    Constants c = *tx.w().constants;
    bool ch = false;
    for (Constant& k : c.list) {
      ch |= dropKey(k.ess, ess);
      ch |= dropKey(k.minEss, ess);
    }
    if (ch) tx.constants() = std::move(c);
  }
  Catalogs& c = tx.catalogs();
  for (SpecialUnit& s : c.specials) {
    dropKey(s.essence, ess);
    dropKey(s.essUpkeep, ess);
  }
  for (Chest& ch : c.chests)
    for (ChestItem& it : ch.items) dropRef(it.essence, ess);
}

}  // namespace

// ================================================================ справочники
Id addCatalogItem(Tx& tx, CatalogList list, const std::string& name) {
  const schema::CatalogListInfo& L = schema::catalogListInfo(list);
  const auto& cur = catalogList(*tx.w().catalogs, list);
  std::vector<std::string> taken;
  for (auto& c : cur) taken.push_back(c.name);
  std::string n = trim(name);
  if (n.empty()) {
    n = uniqueName(taken, L.fresh);
  } else {
    std::string k = utf8::searchKey(n);
    for (auto& c : cur)
      if (utf8::searchKey(c.name) == k) fail(std::string(L.noun) + " " + q(c.name) + " уже есть в справочнике");
  }
  CatalogItem it;
  it.id = tx.nextId(L.seq);
  it.name = n;
  it.color = Color::palette(int(it.id) + 7 * int(list));
  catalogList(tx.catalogs(), list).push_back(std::move(it));
  return catalogList(*tx.w().catalogs, list).back().id;
}

void removeCatalogItem(Tx& tx, CatalogList list, Id item) {
  const CatalogItem* ci = Catalogs::find(catalogList(*tx.w().catalogs, list), item);
  if (!ci) fail("Запись справочника не найдена");
  if (list == CatalogList::Resources && item == kGold) fail("«Золото» — встроенный ресурс казны, его нельзя удалить");
  if (ci->builtin) fail(q(ci->name) + " — встроенная запись справочника, её нельзя удалить");
  const World& w = tx.w();
  switch (list) {
    case CatalogList::Resources: dropResourceRefs(tx, item); break;
    case CatalogList::Essences: dropEssenceRefs(tx, item); break;
    case CatalogList::Races:
      for (Id pid : idsWhere(w.provinces, [&](const Province& p) {
             return std::any_of(p.races.begin(), p.races.end(), [&](const RacePop& r) { return r.race == item; });
           })) {
        auto& rs = tx.province(pid).races;
        rs.erase(std::remove_if(rs.begin(), rs.end(), [&](const RacePop& r) { return r.race == item; }), rs.end());
      }
      // Рабы этой расы (у государств и на работах).
      for (Id pid : idsWhere(w.provinces, [&](const Province& p) {
             return std::any_of(p.slaves.begin(), p.slaves.end(), [&](const SlaveWork& s) { return s.race == item; });
           })) {
        auto& ss = tx.province(pid).slaves;
        ss.erase(std::remove_if(ss.begin(), ss.end(), [&](const SlaveWork& s) { return s.race == item; }), ss.end());
      }
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) {
             return std::any_of(f.slaves.begin(), f.slaves.end(), [&](const SlaveGroup& s) { return s.race == item; });
           })) {
        auto& ss = tx.faction(fid).slaves;
        ss.erase(std::remove_if(ss.begin(), ss.end(), [&](const SlaveGroup& s) { return s.race == item; }), ss.end());
      }
      break;
    case CatalogList::Cultures:
      for (Id pid : idsWhere(w.provinces, [&](const Province& p) { return p.culture == item; })) tx.province(pid).culture = 0;
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) { return f.culture == item; })) tx.faction(fid).culture = 0;
      break;
    case CatalogList::Religions:
      for (Id pid : idsWhere(w.provinces, [&](const Province& p) { return p.religion == item; })) tx.province(pid).religion = 0;
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) { return f.religion == item; })) tx.faction(fid).religion = 0;
      break;
    case CatalogList::Governments:
      for (Id fid : idsWhere(w.factions, [&](const Faction& f) { return f.government == item; })) tx.faction(fid).government = 0;
      break;
    case CatalogList::Positions:
      break;  // должность в совете хранится текстом и остаётся как есть
  }
  auto& l = catalogList(tx.catalogs(), list);
  l.erase(std::remove_if(l.begin(), l.end(), [&](const CatalogItem& c) { return c.id == item; }), l.end());
}

}  // namespace rg::rules
