// Regnum — общие помощники тестов, работающих с миром (модуль core и всё, что от него зависит: geo, rules, map,
// mapbake, app): поиск сущностей и элементов справочников по имени, частые выборки демонстрационного мира.
//
//   test::idByName(w.catalogs->resources, "Железо")      // справочник (вектор) или таблица мира (w.buildings)
//   test::idWhere(w.techs, [&](const Tech& t) { return t.faction == f && t.name == "Алхимия"; })
//   test::firstOwnedLand(w), test::twoStates(w), test::has(list, id)
#pragma once
#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "core/world.h"

namespace rg::test {

// ID первого (по порядку справочника или по возрастанию ID таблицы) элемента, для которого pred — true; 0 — нет.
template <class List, class Pred> Id idWhere(const List& list, Pred&& pred) {
  if constexpr (requires { list.firstId(pred); }) {
    return list.firstId(pred);
  } else {
    for (const auto& x : list)
      if (pred(x)) return x.id;
    return 0;
  }
}

// ID первого элемента с данным именем (ресурсы, эссенции, классы героев, особые отряды, постройки, персонажи...).
template <class List> Id idByName(const List& list, std::string_view name) {
  return idWhere(list, [&](const auto& x) { return x.name == name; });
}

// ID первого элемента с данным ключом (постройки и технологии базового набора).
template <class List> Id idByKey(const List& list, std::string_view key) {
  return idWhere(list, [&](const auto& x) { return x.key == key; });
}

template <class T, class U> bool has(const std::vector<T>& v, const U& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// Первая (по ID) сухопутная провинция с владельцем.
inline Id firstOwnedLand(const World& w) {
  return w.provinces.firstId([](const Province& p) { return !p.sea && p.owner; });
}

// Первое (по ID провинций) государство, у которого есть сухопутная провинция.
inline Id stateWithLand(const World& w) {
  Id r = 0;
  w.provinces.each([&](const Province& p) {
    if (r || p.sea || !p.owner) return;
    const Faction* f = w.faction(p.owner);
    if (f && f->isState()) r = p.owner;
  });
  return r;
}

// Два первых (по ID) государства мира (0 на месте недостающего).
inline std::pair<Id, Id> twoStates(const World& w) {
  Id a = 0, b = 0;
  w.factions.each([&](const Faction& f) {
    if (!f.isState()) return;
    if (!a) a = f.id;
    else if (!b) b = f.id;
  });
  return {a, b};
}

// Постройка с данным ключом базового набора; owner — государство уникальной постройки (0 — общее дерево).
inline Id buildingByKey(const World& w, std::string_view key, Id owner = 0) {
  return idWhere(w.buildings, [&](const Building& b) { return b.key == key && b.owner == owner; });
}

// Технология фракции faction (0 — общее дерево) с данным названием.
inline Id techByName(const World& w, Id faction, std::string_view name) {
  return idWhere(w.techs, [&](const Tech& t) { return t.faction == faction && t.name == name; });
}

// Последняя (с наибольшим ID) сделка мира; 0 — сделок нет.
inline Id newestDeal(const World& w) {
  Id id = 0;
  w.deals.each([&](const Deal& d) { id = std::max(id, d.id); });
  return id;
}

// Технология pre — прямое условие изучения tech.
inline bool hasPrereq(const World& w, Id tech, Id pre) {
  const Tech* t = w.tech(tech);
  return t && has(t->prereqs, pre);
}

// Место раскопок справочника по ключу (0 — нет).
inline Id siteKey(const World& w, std::string_view key) {
  const ArchSite* s = w.catalogs->archSiteByKey(key);
  return s ? s->id : 0;
}

// Сундук справочника по ключу (0 — нет).
inline Id chestKey(const World& w, std::string_view key) {
  const Chest* c = w.catalogs->chestByKey(key);
  return c ? c->id : 0;
}

// Тип города задаётся населением провинции (rules::cityType): аванпост — 500, деревня — 5000, небольшой город — 20 000,
// большой город — 60 000 жителей первой расы справочника (раса создаётся, если справочник пуст).
inline void setCityPop(Tx& tx, Id province, CityType c) {
  static const i64 kPop[] = {500, 5000, 20000, 60000};
  Id race = tx.w().catalogs->races.empty() ? 0 : tx.w().catalogs->races.front().id;
  if (!race) {
    CatalogItem r;
    r.id = tx.nextId(Seq::Race);
    r.name = "Люди";
    tx.catalogs().races.push_back(r);
    race = r.id;
  }
  tx.province(province).races = {RacePop{race, kPop[std::clamp(int(c), 0, 3)]}};
}

// Сумма всех запасов фракции (золото и ресурсы).
inline double stockSum(const World& w, Id faction) {
  double s = 0;
  for (const auto& [r, v] : w.faction(faction)->res) s += v;
  return s;
}

}  // namespace rg::test
