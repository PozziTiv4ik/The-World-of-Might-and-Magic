// Regnum — помощники тестов правил: маленький мир с геометрией (остров 8 провинций + 2 морские), фракции.
//
//   y=0 ┌──────────────────────────────── море ───────────────────────────────┐
//       │ seaW │  p0 (200,200) │  p1 (400,200) │  p2 (600,200) │  p3 (800,200) │ seaE │
//       │      │  p4 (200,400) │  p5 (400,400) │  p6 (600,400) │  p7 (800,400) │      │
//   y=600└────────────────────────────────────────────────────────────────────────┘
// Суша — прямоугольник [100,900] × [100,500]; провинции суши 200 × 200; seaW/seaE — морские полосы.
//
//   Fix f;  f.tx([&](Tx& tx) { own(tx, f.p[0], f.A, addRace(tx), 1000); });   // копия мира, владелец и население
//   f.pc(f.p[0]), f.fc(f.A)                                // расчёт провинции и фракции
//   has(errorOf([&] { f.tx(...); }), "текст отказа")       // errorOf, has — из test.h и test_world_util.h
//   popOf(w, p), unitsIn(w, army), setFx(tx, key, {{Fx::LoyaltyPerTurn, -5}}), makeMod(tx, {...}), lastLog(w, "...")
#pragma once
#include <algorithm>
#include <string>

#include "geo/ops.h"
#include "geo/topo.h"
#include "rules/rules.h"
#include "tests/test.h"
#include "tests/test_geo_util.h"
#include "tests/test_world_util.h"

namespace rg::rulestest {

constexpr double W = 1000, H = 600;

using geotest::rect;
inline Vec2 center(int i) { return {200.0 + 200.0 * (i % 4), 200.0 + 200.0 * (i / 4)}; }
inline const Vec2 kSeaNorth{500, 50};   // неназначенное море над островом
inline const Vec2 kSeaWest{35, 300};    // морская провинция seaW

struct Base {
  World w;
  Id p[8]{};
  Id seaW = 0, seaE = 0;
  Id A = 0, B = 0, C = 0, G = 0, H = 0;  // государства A, B, C; гильдии G, H
};

// Мир собирается один раз (геометрия дорогая), тесты получают его копию (копия World — O(1)).
inline const Base& base() {
  static const Base b = [] {
    Base r;
    Store s;
    s.transact("init", [&](Tx& tx) {
      geo::Coast c;
      c.width = W;
      c.height = H;
      c.landRings.push_back(rect(100, 100, 900, 500));
      geo::initFromCoast(tx, c);
      for (int i = 0; i < 8; i++) {
        int col = i % 4, row = i / 4;
        double x0 = 100 + 200 * col, x1 = x0 + 200, y0 = 100 + 200 * row, y1 = y0 + 200;
        if (col == 0) x0 = 50;
        if (col == 3) x1 = 950;
        if (row == 0) y0 = 50;
        if (row == 1) y1 = 550;
        r.p[i] = geo::createProvince(tx, rect(x0, y0, x1, y1), Terrain::Land);
        tx.province(r.p[i]).name = "П" + std::to_string(i);
      }
      r.seaW = geo::createProvince(tx, rect(10, 10, 60, 590), Terrain::Sea);
      tx.province(r.seaW).name = "Западное море";
      r.seaE = geo::createProvince(tx, rect(940, 10, 990, 590), Terrain::Sea);
      tx.province(r.seaE).name = "Восточное море";
      r.A = rules::createFaction(tx, FactionKind::State, "Арден");
      r.B = rules::createFaction(tx, FactionKind::State, "Бельмар");
      r.C = rules::createFaction(tx, FactionKind::State, "Церис");
      r.G = rules::createFaction(tx, FactionKind::Guild, "Гильдия весов");
      r.H = rules::createFaction(tx, FactionKind::Guild, "Дом Хорна");
      // Модификаторы совета («Децентрализация» и др.) действуют на каждое государство; в базовом мире тестов они
      // без эффектов, чтобы расчёты прочих правил были прежними (тесты совета задают эффекты сами — neutralCouncil).
      for (const char* key : {schema::mod::Decentralization, schema::mod::WeakControl, schema::mod::Centralized, schema::mod::Famine,
                              schema::mod::Unpaid}) {
        Modifier m = *schema::builtinModifier(key);
        m.id = 0;
        m.fx = {};
        m.fxMask = 0;
        tx.add(std::move(m));
      }
      // Естественный прирост и гибель от голода меняют население каждый ход, а провизии у государств тестов нет: в
      // базовом мире они выключены (тесты этих правил задают константы сами — test_rules_econ_loops.cpp).
      rules::ensureConstant(tx, schema::cst::PopGrowthBase).num = 0;
      rules::ensureConstant(tx, schema::cst::FamineDeaths).num = 0;
      // Постройки базового набора (общая «Цитадель Бездны») убираются: тесты строят свои деревья с нуля. Базовые
      // расы, культуры и реликвии тоже: тесты заводят свои (и проверяют пустой пул находок).
      for (Id b : tx.w().buildings.ids()) tx.eraseBuilding(b);
      tx.catalogs().races.clear();
      tx.catalogs().cultures.clear();
      tx.catalogs().relics.clear();
      // Модификаторы базовых записей (должности — занятые и пустующие, религии, формы правления, классы героев и
      // таланты) действуют на каждое государство и героя; в базовом мире тестов их нет — тесты задают свои.
      for (auto* list : {&tx.catalogs().positions, &tx.catalogs().religions, &tx.catalogs().governments})
        for (CatalogItem& c : *list) {
          c.modifiers.clear();
          c.vacantModifiers.clear();
        }
      for (HeroClass& k : tx.catalogs().classes) {
        k.modifiers.clear();
        for (SubClass& sc : k.subclasses)
          for (Talent& t : sc.talents) t.modifiers.clear();
      }
      // Тесты называют ресурс rs5 «Железо» (как в мире до базовых групп ресурсов): названия rs4 и rs5 меняются местами.
      for (CatalogItem& c : tx.catalogs().resources) {
        if (c.id == 4) c.name = "Титан";
        if (c.id == 5) c.name = "Железо";
      }
    });
    r.w = s.world();
    return r;
  }();
  return b;
}

// Хранилище с копией базового мира.
struct Fix {
  Store s;
  Id p[8]{};
  Id seaW = 0, seaE = 0, A = 0, B = 0, C = 0, G = 0, H = 0;
  mutable std::shared_ptr<const rules::Calc> cache;

  Fix() {
    const Base& b = base();
    s.replace(b.w, "test");
    for (int i = 0; i < 8; i++) p[i] = b.p[i];
    seaW = b.seaW;
    seaE = b.seaE;
    A = b.A;
    B = b.B;
    C = b.C;
    G = b.G;
    H = b.H;
  }
  const World& w() const { return s.world(); }
  template <class F> decltype(auto) tx(F&& f) { return s.transact("test", std::forward<F>(f)); }
  const rules::ProvinceCalc& pc(Id id) const {
    cache = rules::calc(w());
    return *cache->province(id);
  }
  const rules::FactionCalc& fc(Id id) const {
    cache = rules::calc(w());
    return *cache->faction(id);
  }
};

using test::errorOf;
using test::has;   // элемент в векторе
inline bool has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

// Раса «Люди» в справочнике.
inline Id addRace(Tx& tx) { return rules::addCatalogItem(tx, rules::CatalogList::Races, "Люди"); }

// Владелец провинции и её население — одна раса.
inline void own(Tx& tx, Id p, Id owner, Id race, i64 pop) {
  Province& m = tx.province(p);
  m.owner = owner;
  m.races = {RacePop{race, pop}};
}

// Тип города задаётся населением: аванпост — 500, деревня — 5000, небольшой город — 20 000, большой город — 60 000
// жителей (первая раса справочника).
inline void setCity(Tx& tx, Id p, CityType c) {
  static const i64 kPop[] = {500, 5000, 20000, 60000};
  const Id race = tx.w().catalogs->races.empty() ? addRace(tx) : tx.w().catalogs->races.front().id;
  tx.province(p).races = {RacePop{race, kPop[std::clamp(int(c), 0, 3)]}};
}

// Население провинции (все расы).
inline i64 popOf(const World& w, Id p) {
  i64 n = 0;
  for (const RacePop& r : w.province(p)->races) n += r.pop;
  return n;
}

// Добавить модификатору эффекты (значения и биты).
inline void addFx(Modifier& m, std::initializer_list<std::pair<Fx, double>> fx) {
  for (auto& [f, v] : fx) {
    m.fx[size_t(f)] = v;
    m.fxMask |= 1u << unsigned(f);
  }
}

// Эффекты встроенного модификатора в мире теста (в базовом мире совет и голод — без эффектов).
inline void setFx(Tx& tx, const char* key, std::initializer_list<std::pair<Fx, double>> fx) {
  Modifier& m = tx.modifier(rules::ensureBuiltinMod(tx, key));
  m.fx = {};
  m.fxMask = 0;
  addFx(m, fx);
}

// Отрядов в войске (все группы и строки).
inline i64 unitsIn(const World& w, Id army) {
  i64 n = 0;
  for (const ArmyGroup& g : w.army(army)->groups)
    for (const ArmyUnit& u : g.units) n += u.count;
  return n;
}

// Отрядов строки row фракции faction в войске; −1 — войска нет.
inline i64 unitsIn(const World& w, Id army, Id faction, Id row) {
  const Army* a = w.army(army);
  if (!a) return -1;
  i64 n = 0;
  for (const ArmyGroup& g : a->groups)
    if (g.faction == faction)
      for (const ArmyUnit& u : g.units)
        if (u.row == row) n += u.count;
  return n;
}

// Общая постройка с одним уровнем (и возможностью flag, если on); ставится готовой в провинцию province.
inline Id simpleBuilding(Tx& tx, const char* name, Id province = 0, rules::BuildingFlag flag = rules::BuildingFlag::Coastal,
                         bool on = false) {
  const Id b = rules::createBuilding(tx, 0, name);
  if (on) rules::setBuildingFlag(tx, b, flag, true);
  if (province) rules::placeBuilding(tx, province, b, 1);
  return b;
}

// Модификатор с заданными эффектами (и целями дипломатии).
inline Id makeMod(Tx& tx, std::initializer_list<std::pair<rg::Fx, double>> fx, std::vector<Id> targets = {}) {
  Id id = rules::createModifier(tx, "Мод");
  Modifier& m = tx.modifier(id);
  addFx(m, fx);
  m.targets = std::move(targets);
  return id;
}

// Строка хроники, содержащая подстроку (последняя по ID), или пусто.
inline std::string lastLog(const World& w, const std::string& sub = {}) {
  std::string r;
  w.log.each([&](const LogEntry& e) { if (sub.empty() || has(e.text, sub)) r = e.text; });
  return r;
}
inline int logCount(const World& w, LogKind k) {
  int n = 0;
  w.log.each([&](const LogEntry& e) { n += e.kind == k; });
  return n;
}

}  // namespace rg::rulestest
