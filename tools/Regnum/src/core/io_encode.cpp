// Regnum — запись мира в JSON: значение каждого файла проекта и его текст (стабильный порядок для диффов в Git).
#include <charconv>
#include <cstdio>

#include "codec/base64.h"
#include "core/io_internal.h"

namespace rg::io::detail {

using json::Value;
using schema::CatalogList;
using schema::EnumInfo;

// ================================================================ запись
namespace {

double fin(double v) { return std::isfinite(v) ? v : 0.0; }
// Координаты карты и схем — с точностью 0,01.
double r2(double v) { return std::isfinite(v) ? std::round(v * 100.0) / 100.0 : 0.0; }
// float -> кратчайшее десятичное (0.3f пишется как 0.3).
double floatV(float f) {
  if (!std::isfinite(f)) return 0;
  char buf[32];
  auto r = std::to_chars(buf, buf + sizeof buf, f);
  double d = 0;
  std::from_chars(buf, r.ptr, d);
  return d;
}

Value ref(Seq s, Id id) { return id ? Value(refStr(s, id)) : Value(); }
Value refs(Seq s, const std::vector<Id>& ids) {
  json::Array a;
  a.reserve(ids.size());
  for (Id id : ids) a.emplace_back(ref(s, id));
  return Value(std::move(a));
}
Value colorV(Color c) {
  char buf[12];
  if (c.a == 255) std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
  else std::snprintf(buf, sizeof buf, "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
  return Value(buf);
}
Value posV(Vec2 p) {
  json::Array a;
  a.reserve(2);
  a.emplace_back(r2(p.x));
  a.emplace_back(r2(p.y));
  return Value(std::move(a));
}
Value ptsV(const std::vector<Vec2>& v) {
  json::Array a;
  a.reserve(v.size() * 2);
  for (const Vec2& p : v) {
    a.emplace_back(r2(p.x));
    a.emplace_back(r2(p.y));
  }
  return Value(std::move(a));
}
Value resV(const std::map<Id, double>& m, Seq s = Seq::Resource) {
  Value o = Value::object();
  for (auto& [id, v] : m) o.set(refStr(s, id), fin(v));
  return o;
}
Value essV(const std::map<Id, double>& m) { return resV(m, Seq::Essence); }
// Набор значений перечисления (биты) — массив идентификаторов.
Value maskV(Enum e, u32 mask) {
  json::Array a;
  for (int i = 0; i < e.n && i < 32; i++)
    if (mask & (1u << i)) a.emplace_back(e.list[i].id);
  return Value(std::move(a));
}
Value strsV(const std::vector<std::string>& v) {
  json::Array a;
  for (auto& s : v) a.emplace_back(s);
  return Value(std::move(a));
}
Value enumV(Enum e, int v) { return Value(e.list[(v >= 0 && v < e.n) ? v : 0].id); }
Value bytesV(const std::string& b) { return Value(codec::base64::encode(b)); }
// Стопки реликвий: объект «ID реликвии → количество».
Value stacksV(const std::vector<RelicStack>& v) {
  Value o = Value::object();
  std::map<Id, i64> m;
  for (const RelicStack& x : v)
    if (x.relic && x.count > 0) m[x.relic] += x.count;
  for (auto& [r, n] : m) o.set(refStr(Seq::Relic, r), double(n));
  return o;
}
// Массив JSON из списка записей (std::vector, std::array): enc(запись) -> Value.
template <class List, class F> Value arrayOf(const List& list, F&& enc) {
  json::Array a;
  a.reserve(list.size());
  for (const auto& x : list) a.push_back(enc(x));
  return Value(std::move(a));
}
Value talentV(const Talent& t) {
  Value x = Value::object();
  x.set("id", ref(Seq::Talent, t.id));
  x.set("name", t.name);
  x.set("desc", t.desc);
  x.set("icon", t.icon);
  x.set("row", t.row);
  x.set("col", t.col);
  x.set("cost", t.cost);
  x.set("ranks", t.ranks);
  x.set("prereq", ref(Seq::Talent, t.prereq));
  x.set("modifiers", refs(Seq::Modifier, t.modifiers));
  return x;
}
Value resAmountsV(const std::vector<ResAmount>& v, Seq s) {
  return arrayOf(v, [&](const ResAmount& x) {
    Value e = Value::object();
    e.set(s == Seq::Essence ? "essence" : "res", ref(s, x.res));
    e.set("amount", fin(x.amount));
    return e;
  });
}
// Группы дерева справочника (группы ресурсов, группы реликвий).
template <class Tag> Value groupsV(const std::vector<TreeGroup<Tag>>& list, Seq s) {
  return arrayOf(list, [&](const TreeGroup<Tag>& g) {
    Value e = Value::object();
    e.set("id", ref(s, g.id));
    e.set("name", g.name);
    e.set("parent", ref(s, g.parent));
    e.set("key", g.key);
    return e;
  });
}

Value encMeta(const Meta& m) {
  Value o = Value::object();
  o.set("name", m.name);
  o.set("createdAt", m.createdAt);
  o.set("updatedAt", m.updatedAt);
  o.set("turn", m.turn);
  Value seq = Value::object();
  for (int i = 0; i < kSeqCount; i++) seq.set(schema::kSeqs[i].name, m.seq[size_t(i)]);
  o.set("seq", std::move(seq));
  o.set("basemap", m.basemap);
  o.set("mapObjects", m.mapObjects);
  o.set("content", m.content);
  o.set("rng", m.rng);
  o.set("notes", m.notes);
  // Итоги последнего хода и неразобранные события хода.
  if (m.lastTurn.turnTo) {
    const TurnRecord& t = m.lastTurn;
    Value x = Value::object();
    x.set("turnFrom", t.turnFrom);
    x.set("turnTo", t.turnTo);
    x.set("factions", arrayOf(t.factions, [](const TurnFactionLine& l) {
      Value e = Value::object();
      e.set("faction", ref(Seq::Faction, l.faction));
      e.set("treasuryBefore", fin(l.treasuryBefore));
      e.set("treasuryAfter", fin(l.treasuryAfter));
      e.set("income", fin(l.income));
      e.set("expenses", fin(l.expenses));
      e.set("resources", resV(l.resources));
      e.set("essences", essV(l.essences));
      e.set("provisionDebt", fin(l.provisionDebt));
      return e;
    }));
    x.set("log", refs(Seq::Log, t.logIds));
    x.set("rebellions", refs(Seq::Province, t.rebellions));
    x.set("hits", arrayOf(t.hits, [](const HitOutcome& h) {
      Value e = Value::object();
      e.set("plot", ref(Seq::Plot, h.plot));
      e.set("character", ref(Seq::Character, h.character));
      e.set("chance", fin(h.chance));
      e.set("roll", fin(h.roll));
      e.set("killed", h.killed);
      return e;
    }));
    o.set("lastTurn", std::move(x));
  }
  if (!m.turnEvents.empty())
    o.set("turnEvents", arrayOf(m.turnEvents, [](const TurnEvent& t) {
      Value e = Value::object();
      e.set("kind", enumV(en(kTurnEventE), int(t.kind)));
      e.set("army", ref(Seq::Army, t.army));
      e.set("province", ref(Seq::Province, t.province));
      e.set("origin", ref(Seq::Faction, t.origin));
      e.set("rebelState", ref(Seq::Faction, t.rebelState));
      e.set("heroes", refs(Seq::Character, t.heroes));
      return e;
    }));
  return o;
}

Value encSettings(const Settings& s) {
  Value o = Value::object();
  o.set("fillOpacity", floatV(s.fillOpacity));
  o.set("labelStates", s.labelStates);
  o.set("labelProvinces", s.labelProvinces);
  o.set("labelArmies", s.labelArmies);
  o.set("showArmies", s.showArmies);
  o.set("figureSize", s.figureSize);
  o.set("occupiedIncome", enumV(en(schema::kOccupiedIncome), int(s.occupiedIncome)));
  o.set("rebellionRoll", s.rebellionRoll);
  o.set("autosaveFolder", s.autosaveFolder);
  o.set("autosaveSec", s.autosaveSec);
  o.set("freeFleets", s.freeFleets);
  if (!s.adminKey.empty()) o.set("adminKey", s.adminKey);
  return o;
}

Value encCatalogs(const Catalogs& c) {
  Value o = Value::object();
  for (const schema::CatalogListInfo& L : schema::kCatalogLists)
    o.set(L.key, arrayOf(c.*L.items, [&](const CatalogItem& it) {
      Value e = Value::object();
      e.set("id", ref(L.seq, it.id));
      e.set("name", it.name);
      e.set("color", colorV(it.color));
      e.set("icon", it.icon);
      e.set("builtin", it.builtin);
      if (L.list == CatalogList::Resources) {   // ресурсы: ключ встроенного ресурса и группа
        e.set("key", it.key);
        e.set("group", ref(Seq::ResGroup, it.group));
      }
      if (L.list == CatalogList::Positions) {   // должности: модификаторы занятой и пустующей должности
        e.set("modifiers", refs(Seq::Modifier, it.modifiers));
        e.set("vacantModifiers", refs(Seq::Modifier, it.vacantModifiers));
      }
      if (schema::catalogHasModifiers(L.list) && L.list != CatalogList::Positions && !it.modifiers.empty())
        e.set("modifiers", refs(Seq::Modifier, it.modifiers));   // раса, культура, религия, форма правления
      return e;
    }));
  o.set("resGroups", groupsV(c.resGroups, Seq::ResGroup));
  o.set("relics", arrayOf(c.relics, [](const Relic& r) {
    Value e = Value::object();
    e.set("id", ref(Seq::Relic, r.id));
    e.set("name", r.name);
    e.set("rarity", enumV(en(kRarityE), int(r.rarity)));
    e.set("desc", r.desc);
    e.set("group", ref(Seq::RelicGroup, r.group));
    e.set("entity", r.entity);
    e.set("image", bytesV(r.image));
    if (!r.modifiers.empty()) e.set("modifiers", refs(Seq::Modifier, r.modifiers));
    if (r.phylactery) e.set("phylactery", true);
    return e;
  }));
  o.set("relicGroups", groupsV(c.relicGroups, Seq::RelicGroup));
  o.set("classes", arrayOf(c.classes, [](const HeroClass& k) {
    Value e = Value::object();
    e.set("id", ref(Seq::HeroClass, k.id));
    e.set("name", k.name);
    e.set("desc", k.desc);
    e.set("icon", k.icon);
    e.set("color", colorV(k.color));
    e.set("subclasses", arrayOf(k.subclasses, [](const SubClass& sc) {
      Value x = Value::object();
      x.set("id", ref(Seq::SubClass, sc.id));
      x.set("name", sc.name);
      x.set("desc", sc.desc);
      x.set("icon", sc.icon);
      x.set("tierPoints", sc.tierPoints);
      x.set("talents", arrayOf(sc.talents, [](const Talent& t) { return talentV(t); }));
      return x;
    }));
    if (!k.modifiers.empty()) e.set("modifiers", refs(Seq::Modifier, k.modifiers));
    return e;
  }));
  o.set("archSites", arrayOf(c.archSites, [](const ArchSite& s) {
    Value e = Value::object();
    e.set("id", ref(Seq::ArchSite, s.id));
    e.set("key", s.key);
    e.set("name", s.name);
    e.set("icon", s.icon);
    json::Array rw;
    for (const auto& list : s.rewards) rw.push_back(refs(Seq::Chest, list));
    e.set("rewards", Value(std::move(rw)));
    return e;
  }));
  o.set("chests", arrayOf(c.chests, [](const Chest& ch) {
    Value e = Value::object();
    e.set("id", ref(Seq::Chest, ch.id));
    e.set("key", ch.key);
    e.set("name", ch.name);
    e.set("icon", ch.icon);
    e.set("desc", ch.desc);
    e.set("items", arrayOf(ch.items, [](const ChestItem& it) {
      Value x = Value::object();
      x.set("kind", enumV(en(schema::kChestItemKinds), int(it.kind)));
      x.set("amount", fin(it.amount));
      switch (it.kind) {
        case ChestItemKind::Resource:
          x.set("res", ref(Seq::Resource, it.res));
          x.set("group", ref(Seq::ResGroup, it.group));
          x.set("exclude", refs(Seq::Resource, it.exclude));
          break;
        case ChestItemKind::Essence: x.set("essence", ref(Seq::Essence, it.essence)); break;
        case ChestItemKind::Relic:
          x.set("relicGroup", ref(Seq::RelicGroup, it.relicGroup));
          x.set("rarities", maskV(en(kRarityE), it.rarities));
          break;
        case ChestItemKind::Chest: x.set("chests", refs(Seq::Chest, it.chests)); break;
        default: break;
      }
      return x;
    }));
    return e;
  }));
  o.set("specials", arrayOf(c.specials, [](const SpecialUnit& s) {
    Value e = Value::object();
    e.set("id", ref(Seq::Special, s.id));
    e.set("name", s.name);
    e.set("type", enumV(en(schema::kUnitTypes), int(s.type)));
    e.set("race", s.race);
    e.set("keyRes", ref(Seq::Resource, s.keyRes));
    e.set("keyPer", fin(s.keyPer));
    e.set("extra", resV(s.extra));
    e.set("essence", essV(s.essence));
    e.set("upkeep", fin(s.upkeep));
    e.set("essUpkeep", essV(s.essUpkeep));
    e.set("desc", s.desc);
    return e;
  }));
  return o;
}

// Срок модификаторов: {"m3": 4}.
Value modTurnsV(const ModTurns& m) {
  Value o = Value::object();
  for (auto& [id, n] : m) o.set(refStr(Seq::Modifier, id), n);
  return o;
}
Value garrisonV(const std::vector<GarrisonEntry>& list) {
  return arrayOf(list, [](const GarrisonEntry& e) {
    Value x = Value::object();
    x.set("row", ref(Seq::Row, e.row));
    x.set("count", e.count);
    return x;
  });
}

Value encConstants(const Constants& c) {
  json::Array a;
  for (const Constant& k : c.list) {
    Value e = Value::object();
    e.set("key", k.key);
    e.set("name", k.name);
    e.set("type", enumV(en(schema::kConstTypes), int(k.type)));
    e.set("builtin", k.builtin);
    e.set("desc", k.desc);
    switch (k.type) {
      case ConstType::Number: e.set("num", fin(k.num)); break;
      case ConstType::Resources:
        e.set("res", resV(k.res));
        e.set("ess", essV(k.ess));
        e.set("minRes", resV(k.minRes));
        e.set("minEss", essV(k.minEss));
        break;
      case ConstType::Values: {
        json::Array v;
        for (auto& s : k.values) v.emplace_back(s);
        e.set("values", Value(std::move(v)));
        break;
      }
      default: break;
    }
    a.push_back(std::move(e));
  }
  Value o = Value::object();
  o.set("constants", Value(std::move(a)));
  return o;
}

Value encGeo(const World& w) {
  json::Array nodes, edges;
  nodes.reserve(w.nodes.size());
  edges.reserve(w.edges.size());
  w.nodes.each([&](const Node& n) {
    json::Array a;
    a.reserve(3);
    a.emplace_back(n.id);
    a.emplace_back(r2(n.p.x));
    a.emplace_back(r2(n.p.y));
    nodes.emplace_back(std::move(a));
  });
  w.edges.each([&](const Edge& e) {
    Value o = Value::object();
    o.set("id", e.id);
    o.set("a", e.a);
    o.set("b", e.b);
    o.set("kind", enumV(en(kEdgeKindE), int(e.kind)));
    o.set("pl", ref(Seq::Province, e.pl));
    o.set("pr", ref(Seq::Province, e.pr));
    o.set("tl", enumV(en(kTerrainE), int(e.tl)));
    o.set("tr", enumV(en(kTerrainE), int(e.tr)));
    o.set("pts", ptsV(e.pts));
    edges.push_back(std::move(o));
  });
  Value o = Value::object();
  o.set("nodes", Value(std::move(nodes)));
  o.set("edges", Value(std::move(edges)));
  return o;
}

// Знаки — компактно [id, вид, x, y, масштаб, рисунок, z]; фигуры — объектами.
Value encMap(const World& w) {
  json::Array symbols, shapes;
  symbols.reserve(w.symbols.size());
  shapes.reserve(w.shapes.size());
  w.symbols.each([&](const MapSymbol& s) {
    json::Array a;
    a.reserve(7);
    a.emplace_back(s.id);
    a.emplace_back(enumV(en(kSymbolKindE), int(s.kind)));
    a.emplace_back(r2(s.p.x));
    a.emplace_back(r2(s.p.y));
    a.emplace_back(floatV(s.s));
    a.emplace_back(int(s.v));
    a.emplace_back(fin(s.z));
    symbols.emplace_back(std::move(a));
  });
  w.shapes.each([&](const MapShape& s) {
    Value o = Value::object();
    o.set("id", s.id);
    o.set("kind", enumV(en(kShapeKindE), int(s.kind)));
    o.set("pts", ptsV(s.pts));
    if (!s.holes.empty()) {
      json::Array h;
      h.reserve(s.holes.size());
      for (const auto& r : s.holes) h.push_back(ptsV(r));
      o.set("holes", Value(std::move(h)));
    }
    if (!s.closed()) o.set("w", floatV(s.w));
    if (s.dash > 0) o.set("dash", floatV(s.dash));
    shapes.push_back(std::move(o));
  });
  Value o = Value::object();
  o.set("symbols", Value(std::move(symbols)));
  o.set("shapes", Value(std::move(shapes)));
  return o;
}

Value encProvince(const Province& p) {
  Value o = Value::object();
  o.set("id", ref(Seq::Province, p.id));
  o.set("name", p.name);
  o.set("owner", ref(Seq::Faction, p.owner));
  o.set("lord", ref(Seq::Character, p.lord));
  o.set("sea", p.sea);
  o.set("capital", p.capital);
  o.set("size", enumV(en(schema::kProvSizes), int(p.size)));
  o.set("resource", ref(Seq::Resource, p.resource));
  o.set("resourceAmount", fin(p.resourceAmount));
  o.set("garrison", garrisonV(p.garrison));
  o.set("garrisonHeroes", refs(Seq::Character, p.garrisonHeroes));
  o.set("garrisonLoyalty", fin(p.garrisonLoyalty));
  o.set("contentment", fin(p.contentment));
  o.set("culture", ref(Seq::Culture, p.culture));
  o.set("religion", ref(Seq::Religion, p.religion));
  o.set("races", arrayOf(p.races, [&](const auto& e) {
    Value x = Value::object();
    x.set("race", ref(Seq::Race, e.race));
    x.set("pop", e.pop);
    return x;
  }));
  o.set("influence", arrayOf(p.influence, [&](const auto& e) {
    Value x = Value::object();
    x.set("guild", ref(Seq::Faction, e.guild));
    x.set("pct", fin(e.pct));
    return x;
  }));
  o.set("hqs", refs(Seq::Faction, p.hqs));
  o.set("baseTrade", fin(p.baseTrade));
  o.set("localTax", fin(p.localTax));
  o.set("buildings", arrayOf(p.buildings, [&](const auto& e) {
    Value x = Value::object();
    x.set("building", ref(Seq::Building, e.building));
    x.set("level", e.level);
    x.set("constructing", e.constructing);
    x.set("left", e.left);
    if (e.constructing) {  // уплаченное за строящийся уровень и плательщик (возврат при отмене)
      x.set("paid", resV(e.paid));
      x.set("payer", ref(Seq::Faction, e.payer));
    }
    if (e.constructing && !e.paidEss.empty()) x.set("paidEss", essV(e.paidEss));
    if (e.idle) x.set("idle", true);      // постройка преобразования: простаивает
    if (e.cycle) x.set("cycle", e.cycle);  // ходов до конца цикла преобразования
    if (e.recipe) x.set("recipe", e.recipe);   // выбранный рецепт преобразования
    if (e.mult != 1) x.set("mult", e.mult);    // множитель производства преобразования
    if (e.race) x.set("race", ref(Seq::Race, e.race));   // раса постройки иссушения
    if (!e.relics.empty()) x.set("relics", stacksV(e.relics));   // хранилище реликвий
    return x;
  }));
  o.set("modifiers", refs(Seq::Modifier, p.modifiers));
  o.set("modTurns", modTurnsV(p.modTurns));
  if (p.buildTurn) o.set("buildTurn", p.buildTurn);
  o.set("occupied", p.occupied);
  o.set("occupier", ref(Seq::Faction, p.occupier));
  o.set("occGarrison", garrisonV(p.occGarrison));
  o.set("occIdle", p.occIdle);
  o.set("slaves", arrayOf(p.slaves, [&](const auto& s) {
    Value x = Value::object();
    x.set("race", ref(Seq::Race, s.race));
    x.set("count", s.count);
    return x;
  }));
  o.set("arch", arrayOf(p.arch, [&](const auto& s) {
    Value x = Value::object();
    x.set("site", ref(Seq::ArchSite, s.site));
    x.set("open", s.open);
    x.set("stage", s.stage);
    x.set("chest", ref(Seq::Chest, s.chest));
    return x;
  }));
  o.set("hiddenRelic", ref(Seq::Relic, p.hiddenRelic));
  o.set("hiddenBy", ref(Seq::Faction, p.hiddenBy));
  o.set("notes", p.notes);
  o.set("entity", p.entity);
  return o;
}

Value encFaction(const Faction& f) {
  Value o = Value::object();
  o.set("id", ref(Seq::Faction, f.id));
  o.set("kind", enumV(en(schema::kFactionKinds), int(f.kind)));
  o.set("name", f.name);
  o.set("color", colorV(f.color));
  Value flag = Value::object();
  flag.set("image", f.flag.image);
  flag.set("pattern", enumV(en(schema::kFlagPatterns), int(f.flag.pattern)));
  json::Array cols;
  for (Color c : f.flag.colors) cols.push_back(colorV(c));
  flag.set("colors", Value(std::move(cols)));
  flag.set("emblem", f.flag.emblem);
  flag.set("emblemColor", colorV(f.flag.emblemColor));
  flag.set("png", bytesV(f.flag.png));
  o.set("flag", std::move(flag));
  o.set("culture", ref(Seq::Culture, f.culture));
  o.set("government", ref(Seq::Government, f.government));
  o.set("religion", ref(Seq::Religion, f.religion));
  o.set("ruler", ref(Seq::Character, f.ruler));
  o.set("rulerTitle", f.rulerTitle);
  o.set("council", arrayOf(f.council, [&](const auto& s) {
    Value x = Value::object();
    x.set("id", ref(Seq::Council, s.id));
    x.set("position", s.position);
    x.set("character", ref(Seq::Character, s.character));
    return x;
  }));
  o.set("capital", ref(Seq::Province, f.capital));
  o.set("army", arrayOf(f.army, [&](const auto& r) {
    Value x = Value::object();
    x.set("id", ref(Seq::Row, r.id));
    x.set("name", r.name);
    x.set("type", enumV(en(schema::kUnitTypes), int(r.type)));
    x.set("total", r.total);
    x.set("upkeep", fin(r.upkeep));
    x.set("race", r.race);
    x.set("keyRes", ref(Seq::Resource, r.keyRes));
    x.set("keyPer", fin(r.keyPer));
    x.set("extra", resV(r.extra));
    x.set("essence", essV(r.essence));
    x.set("essUpkeep", essV(r.essUpkeep));
    x.set("special", ref(Seq::Special, r.special));
    x.set("merc", r.merc);
    x.set("hire", fin(r.hire));
    return x;
  }));
  o.set("fleet", arrayOf(f.fleet, [&](const auto& r) {
    Value x = Value::object();
    x.set("id", ref(Seq::Row, r.id));
    x.set("name", r.name);
    x.set("type", enumV(en(schema::kShipTypes), int(r.type)));
    x.set("total", r.total);
    x.set("upkeep", fin(r.upkeep));
    x.set("keyRes", ref(Seq::Resource, r.keyRes));
    return x;
  }));
  o.set("res", resV(f.res));
  o.set("modifiers", refs(Seq::Modifier, f.modifiers));
  o.set("modTurns", modTurnsV(f.modTurns));
  o.set("tax", fin(f.tax));
  o.set("homeState", ref(Seq::Faction, f.homeState));
  o.set("stateGuild", f.stateGuild);
  o.set("stateKind", enumV(en(schema::kStateKinds), int(f.stateKind)));
  o.set("mainState", f.mainState);
  o.set("suzerain", ref(Seq::Faction, f.suzerain));
  o.set("rebelOf", ref(Seq::Faction, f.rebelOf));
  o.set("slaves", arrayOf(f.slaves, [&](const auto& s) {
    Value x = Value::object();
    x.set("race", ref(Seq::Race, s.race));
    x.set("count", s.count);
    x.set("contentment", fin(s.contentment));
    return x;
  }));
  o.set("forming", arrayOf(f.forming, [&](const auto& q) {
    Value x = Value::object();
    x.set("row", ref(Seq::Row, q.row));
    x.set("count", q.count);
    x.set("left", q.left);
    x.set("people", q.people);
    x.set("paid", resV(q.paid));
    x.set("paidEss", essV(q.paidEss));
    return x;
  }));
  o.set("tradeFleet", garrisonV(f.tradeFleet));
  o.set("pirateRisk", fin(f.pirateRisk));
  o.set("ess", essV(f.ess));
  o.set("provisionDebt", fin(f.provisionDebt));
  Value techs = Value::object();
  for (auto& [t, s] : f.techs) {
    Value x = Value::object();
    x.set("studied", s.studied);
    x.set("research", s.research);
    x.set("progress", s.progress);
    if (!s.paid.empty()) x.set("paid", resV(s.paid));
    techs.set(refStr(Seq::Tech, t), std::move(x));
  }
  o.set("techs", std::move(techs));
  o.set("archGroups", arrayOf(f.archGroups, [&](const auto& g) {
    Value x = Value::object();
    x.set("id", ref(Seq::ArchGroup, g.id));
    x.set("name", g.name);
    x.set("exp", g.exp);
    x.set("modifiers", refs(Seq::Modifier, g.modifiers));
    x.set("modTurns", modTurnsV(g.modTurns));
    x.set("busy", g.busy);
    if (g.danger) x.set("danger", true);
    return x;
  }));
  o.set("relics", stacksV(f.relics));
  if (f.readyTurn) o.set("readyTurn", f.readyTurn);
  if (f.detectTurn) o.set("detectTurn", f.detectTurn);
  if (!f.prison.empty())
    o.set("prison", arrayOf(f.prison, [&](const auto& e) {
      Value x = Value::object();
      x.set("race", ref(Seq::Race, e.race));
      x.set("pop", e.pop);
      return x;
    }));
  if (f.conspiracy) o.set("conspiracy", true);
  if (f.landless) o.set("landless", f.landless);
  o.set("notes", f.notes);
  o.set("entity", f.entity);
  return o;
}

Value encCharacter(const Character& c) {
  Value o = Value::object();
  o.set("id", ref(Seq::Character, c.id));
  o.set("name", c.name);
  o.set("title", c.title);
  o.set("faction", ref(Seq::Faction, c.faction));
  o.set("hero", c.hero);
  o.set("upkeep", fin(c.upkeep));
  o.set("portrait", bytesV(c.portrait));
  o.set("modifiers", refs(Seq::Modifier, c.modifiers));
  o.set("modTurns", modTurnsV(c.modTurns));
  o.set("captor", ref(Seq::Faction, c.captor));
  o.set("burial", ref(Seq::Province, c.burial));
  o.set("inventory", stacksV(c.inventory));
  o.set("level", c.level);
  o.set("heroClass", ref(Seq::HeroClass, c.heroClass));
  o.set("subclass", ref(Seq::SubClass, c.subclass));
  o.set("talents", refs(Seq::Talent, c.talents));
  if (c.jailer) o.set("jailer", ref(Seq::Faction, c.jailer));
  if (c.respecTurn) o.set("respecTurn", c.respecTurn);
  o.set("notes", c.notes);
  o.set("entity", c.entity);
  return o;
}

Value encModifier(const Modifier& m) {
  Value o = Value::object();
  o.set("id", ref(Seq::Modifier, m.id));
  o.set("name", m.name);
  o.set("icon", m.icon);
  o.set("color", colorV(m.color));
  o.set("desc", m.desc);
  Value fx = Value::object();
  for (int i = 0; i < kFxCount; i++)
    if (m.has(Fx(i))) fx.set(schema::kEffects[i].id, fin(m.fx[size_t(i)]));
  o.set("fx", std::move(fx));
  o.set("targets", refs(Seq::Faction, m.targets));
  o.set("key", m.key);
  o.set("kind", enumV(en(schema::kModKinds), int(m.kind)));
  o.set("duration", m.duration);
  o.set("essGen", essV(m.essGen));
  o.set("resGen", resV(m.resGen));
  return o;
}

Value encBuilding(const Building& b) {
  Value o = Value::object();
  o.set("id", ref(Seq::Building, b.id));
  o.set("owner", ref(Seq::Faction, b.owner));
  o.set("name", b.name);
  o.set("icon", b.icon);
  o.set("cat", enumV(en(schema::kBuildingCats), int(b.cat)));
  o.set("desc", b.desc);
  o.set("requires", arrayOf(b.requires_, [&](const auto& r) {
    Value x = Value::object();
    x.set("building", ref(Seq::Building, r.building));
    x.set("level", r.level);
    return x;
  }));
  o.set("techs", refs(Seq::Tech, b.techs));
  o.set("levels", arrayOf(b.levels, [&](const auto& l) {
    Value x = Value::object();
    x.set("turns", l.turns);
    x.set("cost", resV(l.cost));
    x.set("modifiers", refs(Seq::Modifier, l.modifiers));
    x.set("desc", l.desc);
    x.set("produce", resV(l.produce));
    x.set("essence", essV(l.essence));
    x.set("essCost", essV(l.essCost));
    if (b.shipyard) x.set("ships", maskV(en(schema::kShipTypes), l.ships));
    if (!l.produceOnRes.empty()) x.set("produceOnRes", resV(l.produceOnRes));
    if (b.extract) x.set("extractPct", fin(l.extractPct));
    if (b.drain) {
      x.set("drainPop", l.drainPop);
      x.set("drainEss", essV(l.drainEss));
    }
    if (b.upkeepCut) x.set("upkeepCut", fin(l.upkeepCut));
    return x;
  }));
  o.set("pos", posV(b.pos));
  o.set("convert", b.convert);
  if (b.convert)
    o.set("recipes", arrayOf(b.recipes, [&](const Recipe& r) {
      Value rc = Value::object();
      rc.set("name", r.name);
      rc.set("in", resAmountsV(r.in, Seq::Resource));
      if (!r.essIn.empty()) rc.set("essIn", resAmountsV(r.essIn, Seq::Essence));
      rc.set("out", resAmountsV(r.out, Seq::Resource));
      rc.set("turns", r.turns);
      return rc;
    }));
  o.set("essenceGen", b.essenceGen);
  o.set("specialAccess", b.specialAccess);
  o.set("specials", refs(Seq::Special, b.specials));
  o.set("relicStore", b.relicStore);
  o.set("healing", b.healing);
  o.set("plague", b.plague);
  o.set("shipyard", b.shipyard);
  o.set("mercenary", b.mercenary);
  o.set("coastal", b.coastal);
  o.set("market", b.market);
  o.set("drain", b.drain);
  o.set("upkeepCut", b.upkeepCut);
  o.set("extract", b.extract);
  if (b.extract) {
    o.set("extractGroups", refs(Seq::ResGroup, b.extractGroups));
    o.set("extractExclude", refs(Seq::Resource, b.extractExclude));
    if (b.extractOptional) o.set("extractOptional", true);
  }
  if (b.stateKinds) o.set("stateKinds", maskV(en(schema::kStateKinds), b.stateKinds));
  o.set("stateReqs", arrayOf(b.stateReqs, [&](const auto& q) {
    Value x = Value::object();
    x.set("building", ref(Seq::Building, q.building));
    x.set("per", q.per);
    return x;
  }));
  o.set("key", b.key);
  return o;
}

Value encTech(const Tech& t) {
  Value o = Value::object();
  o.set("id", ref(Seq::Tech, t.id));
  o.set("faction", ref(Seq::Faction, t.faction));
  o.set("name", t.name);
  o.set("desc", t.desc);
  o.set("turns", t.turns);
  o.set("prereqs", refs(Seq::Tech, t.prereqs));
  o.set("modifiers", refs(Seq::Modifier, t.modifiers));
  o.set("studied", t.studied);
  o.set("research", t.research);
  o.set("progress", t.progress);
  o.set("pos", posV(t.pos));
  o.set("cost", resV(t.cost));
  if (!t.paid.empty()) o.set("paid", resV(t.paid));
  o.set("needKeys", strsV(t.needKeys));
  o.set("key", t.key);
  return o;
}

Value encArmy(const Army& a) {
  Value o = Value::object();
  o.set("id", ref(Seq::Army, a.id));
  o.set("kind", enumV(en(kArmyKindE), int(a.kind)));
  o.set("name", a.name);
  o.set("pos", posV(a.pos));
  o.set("groups", arrayOf(a.groups, [&](const auto& g) {
    Value x = Value::object();
    x.set("faction", ref(Seq::Faction, g.faction));
    x.set("units", arrayOf(g.units, [&](const auto& u) {
      Value y = Value::object();
      y.set("row", ref(Seq::Row, u.row));
      y.set("count", u.count);
      return y;
    }));
    x.set("heroes", refs(Seq::Character, g.heroes));
    return x;
  }));
  o.set("commander", ref(Seq::Character, a.commander));
  o.set("loyalty", fin(a.loyalty));
  o.set("modifiers", refs(Seq::Modifier, a.modifiers));
  o.set("modTurns", modTurnsV(a.modTurns));
  o.set("carrier", ref(Seq::Army, a.carrier));
  o.set("cargo", ref(Seq::Army, a.cargo));
  if (a.acted) o.set("acted", a.acted);
  return o;
}

Value encRoute(const Route& r) {
  Value o = Value::object();
  o.set("id", ref(Seq::Route, r.id));
  o.set("name", r.name);
  o.set("guild", ref(Seq::Faction, r.guild));
  o.set("pts", ptsV(r.pts));
  o.set("color", r.color ? colorV(*r.color) : Value());
  return o;
}

// Сделка: id — только у записи таблицы (у сделки внутри предложения его нет).
Value dealV(const Deal& d, bool withId) {
  Value o = Value::object();
  if (withId) o.set("id", ref(Seq::Deal, d.id));
  o.set("kind", enumV(en(schema::kDealKinds), int(d.kind)));
  o.set("a", ref(Seq::Faction, d.a));
  o.set("b", ref(Seq::Faction, d.b));
  o.set("items", arrayOf(d.items, [&](const auto& it) {
    Value x = Value::object();
    x.set("from", enumV(en(kDealSideE), int(it.from)));
    x.set("res", ref(Seq::Resource, it.res));
    x.set("amount", fin(it.amount));
    x.set("mode", enumV(en(schema::kDealModes), int(it.mode)));
    x.set("turns", it.turns);
    x.set("left", it.left);
    x.set("kind", enumV(en(schema::kDealItemKinds), int(it.kind)));
    x.set("ref", it.kind == DealItemKind::Province ? ref(Seq::Province, it.ref) : it.kind == DealItemKind::Hero ? ref(Seq::Character, it.ref) : Value());
    return x;
  }));
  o.set("turn", d.turn);
  o.set("status", enumV(en(kDealStatusE), int(d.status)));
  o.set("note", d.note);
  return o;
}
Value encDeal(const Deal& d) { return dealV(d, true); }

Value truceTermsV(const TruceTerms& t) {
  Value o = Value::object();
  o.set("provinces", refs(Seq::Province, t.provinces));
  o.set("reparations", fin(t.reparations));
  o.set("reparationsTurns", t.reparationsTurns);
  o.set("resources", resV(t.resources));
  o.set("slaves", t.slaves);
  o.set("heroes", refs(Seq::Character, t.heroes));
  o.set("vassal", t.vassal);
  return o;
}

Value encProposal(const Proposal& p) {
  Value o = Value::object();
  o.set("id", ref(Seq::Proposal, p.id));
  o.set("kind", enumV(en(schema::kProposalKinds), int(p.kind)));
  o.set("from", ref(Seq::Faction, p.from));
  o.set("to", ref(Seq::Faction, p.to));
  if (p.other) o.set("other", ref(Seq::Faction, p.other));
  o.set("turn", p.turn);
  if (p.kind == ProposalKind::Deal) o.set("deal", dealV(p.deal, false));
  if (p.kind == ProposalKind::Truce) {
    Value t = Value::object();
    t.set("a", ref(Seq::Faction, p.truce.a));
    t.set("b", ref(Seq::Faction, p.truce.b));
    t.set("fromA", truceTermsV(p.truce.fromA));
    t.set("fromB", truceTermsV(p.truce.fromB));
    t.set("status", enumV(en(schema::kRelStatus), int(p.truce.status)));
    o.set("truce", std::move(t));
  }
  o.set("note", p.note);
  return o;
}

Value encLog(const LogEntry& l) {
  Value o = Value::object();
  o.set("id", ref(Seq::Log, l.id));
  o.set("turn", l.turn);
  o.set("kind", enumV(en(schema::kLogKinds), int(l.kind)));
  o.set("text", l.text);
  o.set("province", ref(Seq::Province, l.province));
  o.set("army", ref(Seq::Army, l.army));
  o.set("factions", refs(Seq::Faction, l.factions));
  o.set("at", l.at);
  return o;
}

Value encPlot(const Plot& p) {
  Value o = Value::object();
  o.set("id", ref(Seq::Plot, p.id));
  o.set("kind", enumV(en(schema::kPlotKinds), int(p.kind)));
  o.set("owner", ref(Seq::Faction, p.owner));
  o.set("target", ref(Seq::Faction, p.target));
  o.set("name", p.name);
  o.set("provinces", arrayOf(p.provinces, [&](const PlotProvince& x) {
    Value v = Value::object();
    v.set("province", ref(Seq::Province, x.province));
    v.set("commitment", fin(x.commitment));
    if (x.sponsored) v.set("sponsored", x.sponsored);
    v.set("supplies", resV(x.supplies));
    return v;
  }));
  o.set("heroes", refs(Seq::Character, p.heroes));
  o.set("invited", refs(Seq::Character, p.invited));
  o.set("threats", refs(Seq::Character, p.threats));
  o.set("leader", ref(Seq::Character, p.leader));
  o.set("hits", arrayOf(p.hits, [&](const PlotHit& h) {
    Value v = Value::object();
    v.set("character", ref(Seq::Character, h.character));
    v.set("gold", fin(h.gold));
    v.set("turn", h.turn);
    if (h.guarded) v.set("guarded", true);
    if (h.reviewed) v.set("reviewed", true);
    return v;
  }));
  o.set("created", p.created);
  o.set("found", p.found);
  o.set("stage", p.stage);
  if (p.revealTurn) o.set("revealTurn", p.revealTurn);
  if (p.alarm) o.set("alarm", true);
  return o;
}

template <class T, class F>
Value encTable(const Table<T>& t, const char* key, F&& enc) {
  json::Array a;
  a.reserve(t.size());
  t.each([&](const T& e) { a.push_back(enc(e)); });
  Value o = Value::object();
  o.set(key, Value(std::move(a)));
  return o;
}

}  // namespace

Value encode(const World& w, FileId f) {
  switch (f) {
    case F_WORLD: {
      Value o = Value::object();
      o.set("format", kFormat);
      o.set("version", kVersion);
      o.set("meta", encMeta(*w.meta));
      o.set("settings", encSettings(*w.settings));
      return o;
    }
    case F_CATALOGS: return encCatalogs(*w.catalogs);
    case F_GEO: return encGeo(w);
    case F_MAP: return encMap(w);
    case F_PROVINCES: return encTable(w.provinces, "provinces", encProvince);
    case F_FACTIONS: return encTable(w.factions, "factions", encFaction);
    case F_CHARACTERS: return encTable(w.characters, "characters", encCharacter);
    case F_RELATIONS: {
      Value rel = Value::object();
      for (auto& [key, r] : *w.relations) {
        Id a = Id(key >> 32), b = Id(key & 0xFFFFFFFFu);
        Value x = Value::object();
        x.set("v", fin(r.v));
        x.set("s", enumV(en(schema::kRelStatus), int(r.s)));
        if (r.calm) x.set("calm", r.calm);
        rel.set(refStr(Seq::Faction, a) + "|" + refStr(Seq::Faction, b), std::move(x));
      }
      Value o = Value::object();
      o.set("relations", std::move(rel));
      return o;
    }
    case F_MODIFIERS: return encTable(w.modifiers, "modifiers", encModifier);
    case F_BUILDINGS: return encTable(w.buildings, "buildings", encBuilding);
    case F_TECHS: return encTable(w.techs, "techs", encTech);
    case F_ARMIES: return encTable(w.armies, "armies", encArmy);
    case F_ROUTES: return encTable(w.routes, "routes", encRoute);
    case F_DEALS: return encTable(w.deals, "deals", encDeal);
    case F_LOG: return encTable(w.log, "log", encLog);
    case F_CONSTANTS: return encConstants(*w.constants);
    case F_PLOTS: return encTable(w.plots, "plots", encPlot);
    case F_PROPOSALS: return encTable(w.proposals, "proposals", encProposal);
    default: return Value::object();
  }
}

// Запись «по строке на элемент»: верхний объект с отсортированными ключами, внутри — массивы и объекты,
// каждый элемент которых компактно в своей строке (порядок элементов и членов сохраняется).
static std::string formatLines(const Value& v) {
  const json::WriteOptions compact{0, true, true};
  std::vector<const json::Member*> order;
  for (auto& m : v.members()) order.push_back(&m);
  std::sort(order.begin(), order.end(), [](const json::Member* a, const json::Member* b) { return a->first < b->first; });
  std::string out = "{";
  for (size_t i = 0; i < order.size(); i++) {
    out += i ? ",\n  " : "\n  ";
    json::appendString(out, order[i]->first);
    out += ": ";
    const Value& x = order[i]->second;
    if (x.isArr() && !x.empty()) {
      out += "[";
      const auto& items = x.items();
      for (size_t k = 0; k < items.size(); k++) {
        out += k ? ",\n    " : "\n    ";
        json::write(out, items[k], compact);
      }
      out += "\n  ]";
    } else if (x.isObj() && !x.empty()) {
      out += "{";
      const auto& ms = x.members();
      for (size_t k = 0; k < ms.size(); k++) {
        out += k ? ",\n    " : "\n    ";
        json::appendString(out, ms[k].first);
        out += ": ";
        json::write(out, ms[k].second, compact);
      }
      out += "\n  }";
    } else {
      json::write(out, x, compact);
    }
  }
  out += order.empty() ? "}\n" : "\n}\n";
  return out;
}

std::string format(const Value& v, FileId f) {
  if (f == F_GEO || f == F_MAP || f == F_RELATIONS) return formatLines(v);
  std::string out;
  json::write(out, v, json::WriteOptions{2, true, true});
  out.push_back('\n');
  return out;
}

}  // namespace rg::io::detail
