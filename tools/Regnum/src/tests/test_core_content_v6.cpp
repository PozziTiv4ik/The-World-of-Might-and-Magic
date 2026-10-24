// Regnum — базовые записи версии 6 (аудит 2026-10-23, раздел 7 и п.4.14–4.15): модификаторы талантов, классов,
// реликвий, рас, культур, религий, форм правления и должностей; ступени «Стимуляции промышленности» и перенос
// изученности прежней ветки; потребители мёртвых ресурсов и эссенций; особые отряды; сроки строительства по цене;
// исправленные названия; дополнение мира версии 5.
#include <map>
#include <set>

#include "core/content.h"
#include "core/schema.h"
#include "tests/test.h"
#include "tests/test_world_util.h"

using namespace rg;
using test::buildingByKey;
using test::idByName;

namespace {

const Modifier* modOf(const World& w, const std::vector<Id>& mods) { return mods.size() == 1 ? w.modifier(mods[0]) : nullptr; }

bool startsWithText(const std::string& s, std::string_view p) { return s.compare(0, p.size(), p) == 0; }

// Технологии общего дерева ветки «Стимуляция промышленности»: множитель → технология.
std::map<int, Id> industryOf(const World& w) {
  std::map<int, Id> out;
  w.techs.each([&](const Tech& t) {
    if (t.faction == 0)
      if (const int m = schema::industryMultOf(t.key)) out[m] = t.id;
  });
  return out;
}

Id commonNamed(const World& w, std::string_view name) {
  return test::idWhere(w.buildings, [&](const Building& b) { return b.owner == 0 && b.name == name; });
}

// Ресурс где-то тратится: вход рецепта, цена уровня постройки, цена особого отряда, константа-список ресурсов.
bool consumed(const World& w, Id res) {
  bool r = false;
  w.buildings.each([&](const Building& b) {
    for (const BuildingLevel& l : b.levels) r = r || l.cost.count(res);
    for (const Recipe& x : b.recipes)
      for (const ResAmount& in : x.in) r = r || in.res == res;
  });
  for (const SpecialUnit& s : w.catalogs->specials) r = r || s.keyRes == res || s.extra.count(res);
  for (const Constant& c : w.constants->list) r = r || c.res.count(res);
  return r;
}
// Ресурс где-то производится: выход рецепта или производство уровня.
bool produced(const World& w, Id res) {
  bool r = false;
  w.buildings.each([&](const Building& b) {
    for (const BuildingLevel& l : b.levels) r = r || l.produce.count(res) || l.produceOnRes.count(res);
    for (const Recipe& x : b.recipes)
      for (const ResAmount& o : x.out) r = r || o.res == res;
  });
  return r;
}

}  // namespace

// ---------------------------------------------------------------- «Стимуляция промышленности»
TEST(core_content_v6_industry_steps) {
  const World w = newWorld("Ступени");
  CHECK_EQ(w.meta->content, content::kVersion);
  const std::map<int, Id> br = industryOf(w);
  const int mults[] = {2, 5, 10, 20, 35, 60, 100}, turns[] = {3, 4, 5, 6, 7, 8, 10};
  CHECK_EQ(br.size(), size_t(7));
  const Id parts = idByName(w.catalogs->resources, "Механические детали");
  Id prev = 0;
  for (size_t i = 0; i < std::size(mults); i++) {
    auto it = br.find(mults[i]);
    CHECK_MSG(it != br.end(), std::to_string(mults[i]));
    if (it == br.end()) continue;
    const Tech* t = w.tech(it->second);
    CHECK_EQ(t->name, "Стимуляция промышленности ×" + std::to_string(mults[i]));
    CHECK_EQ(t->turns, turns[i]);
    CHECK_NEAR(t->cost.at(kGold), schema::kIndustryTechGold * mults[i], 1e-9);
    CHECK_NEAR(t->cost.at(parts), schema::kIndustryTechParts * mults[i], 1e-9);
    CHECK((prev ? t->prereqs == std::vector<Id>{prev} : t->prereqs.empty()));
    if (prev) CHECK(t->pos.x > w.tech(prev)->pos.x && t->pos.y == w.tech(prev)->pos.y);   // одним рядом
    prev = t->id;
  }
}

// ---------------------------------------------------------------- модификаторы талантов, классов и реликвий
TEST(core_content_v6_hero_modifiers) {
  const World w = newWorld("Герои");
  const Catalogs& c = *w.catalogs;
  int talents = 0, ultimates = 0;
  std::set<Id> distinct;
  for (const HeroClass& k : c.classes) {
    CHECK_MSG(k.modifiers.size() == 1, k.name);
    for (const SubClass& s : k.subclasses) {
      CHECK_EQ(s.tierPoints, 7);   // ярус открывается раньше полного изучения предыдущего — есть выбор
      for (const Talent& t : s.talents) {
        talents++;
        const Modifier* m = modOf(w, t.modifiers);
        CHECK_MSG(m != nullptr, t.name);
        if (!m) continue;
        distinct.insert(m->id);
        CHECK(m->kind == ModKind::Hero);
        const bool ult = t.ranks == 1 && t.cost == schema::kMaxTalentCost;
        ultimates += ult;
        CHECK_MSG(startsWithText(m->name, ult ? "Финальный талант: " : "Талант: "), t.name + " → " + m->name);
      }
    }
  }
  CHECK_EQ(talents, 980);
  CHECK_EQ(ultimates, 140);
  CHECK(distinct.size() >= 20 && distinct.size() <= 60);   // общие записи по темам, а не 980 разных
  // Класс «Некромант» — встроенный «Некромант»; прочие — свой модификатор героя класса.
  const HeroClass* necro = w.heroClass(idByName(c.classes, "Некромант"));
  CHECK(necro && modOf(w, necro->modifiers) && modOf(w, necro->modifiers)->key == schema::mod::Necromancer);
  for (const char* n : {"Чуматворец", "Ассасин", "Мехомант"}) {
    const HeroClass* k = w.heroClass(idByName(c.classes, n));
    const Modifier* m = k ? modOf(w, k->modifiers) : nullptr;
    CHECK_MSG(m && m->kind == ModKind::Hero && m->key.empty() && (m->fxMask || !m->essGen.empty() || !m->resGen.empty()), n);
  }
  // Реликвии: у всех 100 базовых — модификатор героя; сила растёт с редкостью.
  CHECK_EQ(c.relics.size(), size_t(100));
  for (const Relic& r : c.relics) CHECK_MSG(modOf(w, r.modifiers) && modOf(w, r.modifiers)->kind == ModKind::Hero, r.name);
  auto loyalty = [&](const char* relic) {
    const Relic* r = c.relic(idByName(c.relics, relic));
    const Modifier* m = r ? modOf(w, r->modifiers) : nullptr;
    return m ? m->get(Fx::LoyaltyPerTurn) : 0.0;
  };
  CHECK(loyalty("Стальной меч") > 0);
  CHECK(loyalty("Ледяная Скорбь") > loyalty("Молот Светоносного"));
  CHECK(loyalty("Молот Светоносного") > loyalty("Стальной меч"));
}

// ---------------------------------------------------------------- расы, культуры, религии, формы правления, должности
TEST(core_content_v6_catalog_modifiers) {
  const World w = newWorld("Справочники");
  const Catalogs& c = *w.catalogs;
  CHECK_EQ(c.races.size(), size_t(51));
  CHECK_EQ(c.cultures.size(), size_t(55));
  CHECK_EQ(c.religions.size(), size_t(70));
  // Раса — только локальные эффекты (провинции, где она преобладает).
  for (const CatalogItem& r : c.races) {
    const Modifier* m = modOf(w, r.modifiers);
    CHECK_MSG(m != nullptr, r.name);
    for (int f = 0; m && f < kFxCount; f++) CHECK_MSG(!m->has(Fx(f)) || schema::kEffects[f].local, r.name);
  }
  for (const CatalogItem& x : c.cultures) CHECK_MSG(modOf(w, x.modifiers) != nullptr, x.name);
  std::set<Id> faiths;
  for (const CatalogItem& x : c.religions) {
    CHECK_MSG(modOf(w, x.modifiers) != nullptr, x.name);
    if (!x.modifiers.empty()) faiths.insert(x.modifiers[0]);
  }
  CHECK(faiths.size() >= 10 && faiths.size() <= 20);   // общий модификатор веры у религий одной темы
  // Восемь форм правления — глобальные эффекты.
  CHECK_EQ(c.governments.size(), size_t(8));
  for (const char* n : {"Монархия", "Республика", "Теократия", "Олигархия", "Магократия", "Империя", "Племенной союз", "Деспотия"}) {
    const CatalogItem* g = Catalogs::find(c.governments, idByName(c.governments, n));
    const Modifier* m = g ? modOf(w, g->modifiers) : nullptr;
    CHECK_MSG(m && m->kind == ModKind::Faction, n);
    for (int f = 0; m && f < kFxCount; f++) CHECK_MSG(!m->has(Fx(f)) || !schema::kEffects[f].local, n);
  }
  // Должности: занятая и пустующая; «Лорд-Мастер над экономикой» — доход, над войной — содержание войск, над флотом —
  // содержание флота.
  CHECK_EQ(c.positions.size(), size_t(10));
  for (const CatalogItem& p : c.positions) CHECK_MSG(p.modifiers.size() == 1 && p.vacantModifiers.size() == 1, p.name);
  auto taken = [&](const char* n) { return modOf(w, Catalogs::find(c.positions, idByName(c.positions, n))->modifiers); };
  CHECK(taken("Лорд-Мастер над экономикой")->get(Fx::IncomePct) > 0);
  CHECK(taken("Лорд-Мастер над войной")->get(Fx::ArmyUpkeepPct) < 0);
  CHECK(taken("Лорд-Мастер над флотом")->get(Fx::FleetUpkeepPct) < 0);
  // Эффекты, которые прежде не давала ни одна запись (аудит, п.7.9).
  std::set<int> used;
  w.modifiers.each([&](const Modifier& m) {
    for (int f = 0; f < kFxCount; f++)
      if (m.has(Fx(f))) used.insert(f);
  });
  for (Fx f : {Fx::TradeFlat, Fx::ResourceFlat, Fx::IncomePct, Fx::ArmyUpkeepPct, Fx::FleetUpkeepPct, Fx::ArchSuccessPct, Fx::ArchVitalityPct,
               Fx::ArchExpPct, Fx::ArchUpkeepPct})
    CHECK_MSG(used.count(int(f)), schema::effect(f).name);
}

// ---------------------------------------------------------------- ресурсы, эссенции, особые отряды, постройки
TEST(core_content_v6_consumers) {
  const World w = newWorld("Потребители");
  const Catalogs& c = *w.catalogs;
  for (const char* n : {"Титан", "Платина", "Метеоритная руда", "Шелк", "Жемчуг", "Кость", "Мех", "Прочная чешуя", "Хитин", "Нефть", "Топливо"}) {
    const Id r = idByName(c.resources, n);
    CHECK_MSG(r != 0, n);
    CHECK_MSG(consumed(w, r), n);
  }
  for (const char* n : {"Нефть", "Топливо", "Ткань"}) CHECK_MSG(produced(w, idByName(c.resources, n)), n);
  // Эссенции, которые прежде тратились только на соборы: особые отряды-элементали соборов.
  for (const char* n : {"Эссенция земли", "Эссенция воздуха", "Эссенция природы", "Эссенция льда", "Эссенция арканы", "Эссенция господства",
                        "Эссенция сумерек", "Эссенция гармонии"}) {
    const Id e = idByName(c.essences, n);
    bool unit = false;
    for (const SpecialUnit& s : c.specials) unit = unit || (s.type == UnitType::Elementals && s.essence.count(e) && s.essUpkeep.count(e));
    CHECK_MSG(unit, n);
  }
  const std::string suffix = "земли";
  const Building* cathedral = w.building(commonNamed(w, "Собор " + suffix));
  CHECK(cathedral && cathedral->specialAccess && cathedral->specials.size() == 1);
  // Стрелки с порохом, магическим порохом, дамасской сталью и лунным серебром.
  for (const char* n : {"Порох", "Магический порох", "Дамасская сталь", "Лунное серебро"}) {
    const Id r = idByName(c.resources, n);
    bool shooters = false;
    for (const SpecialUnit& s : c.specials) shooters = shooters || (s.type == UnitType::Ranged && s.extra.count(r));
    CHECK_MSG(shooters, n);
  }
  CHECK_EQ(c.specials.size(), size_t(26));
  // Шантирийские големы и титаны — заметно дешевле; «Драконы Бездны» — содержание и раса; фениксы — содержание.
  CHECK_NEAR(c.special(idByName(c.specials, "Шантирийские Големы"))->keyPer, 10, 1e-9);
  CHECK_NEAR(c.special(idByName(c.specials, "Шантирийские Титаны"))->keyPer, 100, 1e-9);
  const SpecialUnit* abyss = c.special(idByName(c.specials, "Драконы Бездны"));
  CHECK(abyss && abyss->upkeep > 0 && abyss->race == schema::kRaceDemonic);
  const SpecialUnit* phoenix = c.special(idByName(c.specials, "Фениксы"));
  CHECK(phoenix && phoenix->essUpkeep.size() == 1 && phoenix->essUpkeep.begin()->second < 10);
  // Виды государства, как у мясопилки.
  CHECK_EQ(w.building(buildingByKey(w, schema::bld::CultAltar))->stateKinds, 1u << int(StateKind::Demonic));
  CHECK_EQ(w.building(commonNamed(w, "Мавзолей Короля Лича"))->stateKinds, 1u << int(StateKind::Undead));
  CHECK_EQ(w.building(commonNamed(w, "Глубинная Бездна"))->stateKinds, 1u << int(StateKind::Demonic));
  // «Гильдия строителей»: уровень N — N + 1 слот.
  const Building* guild = w.building(buildingByKey(w, schema::bld::BuildersGuild));
  for (size_t i = 0; guild && i < guild->levels.size(); i++) CHECK_EQ(w.modifier(guild->levels[i].modifiers.at(0))->get(Fx::Slots), double(i) + 2);
  // «Основы археологии» и «Продвинутые инструменты»: +10 и +20 % сокровищ.
  for (auto [key, v] : {std::pair{"archBasics", 10.0}, std::pair{"archTools", 20.0}}) {
    const Tech* t = w.tech(test::idByKey(w.techs, key));
    CHECK_MSG(t && modOf(w, t->modifiers) && modOf(w, t->modifiers)->get(Fx::ArchTreasurePct) == v, key);
  }
  // «Общие» артефакты — из сундуков, археологические находки — из ларей.
  Id general = 0;
  for (const RelicGroup& g : c.relicGroups)
    if (g.name == "Общие") general = g.id;
  auto relicGroupOf = [&](const char* chest) {
    for (const ChestItem& it : c.chestByKey(chest)->items)
      if (it.kind == ChestItemKind::Relic) return it.relicGroup;
    return Id(0);
  };
  CHECK(general != 0);
  CHECK_EQ(relicGroupOf("woodChest"), general);
  CHECK_EQ(relicGroupOf("jarl"), general);
  CHECK_EQ(relicGroupOf("woodCoffer"), c.relicGroupId(schema::kRelicArchFinds));
}

// ---------------------------------------------------------------- сроки строительства по цене
TEST(core_content_v6_build_turns) {
  const World w = newWorld("Сроки");
  CHECK_EQ(w.building(buildingByKey(w, schema::bld::CharcoalPit))->levels[0].turns, 1);   // 15 золота
  CHECK_EQ(w.building(buildingByKey(w, schema::bld::Barracks))->levels[0].turns, 2);
  CHECK_EQ(w.building(commonNamed(w, "Собор земли"))->levels[0].turns, 3);
  CHECK_EQ(w.building(commonNamed(w, "Цитадель Бездны"))->levels[0].turns, 5);
  const Building* port = w.building(buildingByKey(w, schema::bld::Port));
  CHECK(port && port->levels[0].turns == 1 && port->levels[4].turns == 2);
  const Building* guild = w.building(buildingByKey(w, schema::bld::BuildersGuild));
  CHECK(guild && guild->levels[0].turns == 3 && guild->levels[1].turns == 2 && guild->levels[4].turns == 4);
  // Срок не короче 1 хода и не дольше 5 у всех базовых построек.
  w.buildings.each([&](const Building& b) {
    for (const BuildingLevel& l : b.levels) CHECK_MSG(l.turns >= 1 && l.turns <= 5, b.name);
  });
}

// ---------------------------------------------------------------- названия
TEST(core_content_v6_names) {
  const World w = newWorld("Названия");
  const Catalogs& c = *w.catalogs;
  const HeroClass* pal = w.heroClass(idByName(c.classes, "Паладин"));
  CHECK(pal && !idByName(c.classes, "Палладин"));
  CHECK(pal && idByName(pal->subclasses, "Паладин-Гармонии"));
  CHECK(idByName(c.cultures, "Мутагенный уклад Рат'гарда") && !idByName(c.cultures, "Мутагенный уклад Рат`гарда"));
  // Ресурсы-чудовища и расы — разные названия; особый отряд и его ключевой ресурс — тоже.
  for (const char* n : {"Великаны", "Циклопы", "Ракшасы", "Энты"}) {
    CHECK_MSG(idByName(c.races, n) && !idByName(c.resources, n), n);
  }
  for (const char* n : {"Дикие Великаны", "Дикие Циклопы", "Дикие Ракшасы", "Древние Энты"})
    CHECK_MSG(c.resourceIn(idByName(c.resources, n), c.groupId(schema::grp::Monsters)), n);
  const SpecialUnit* gold = c.special(idByName(c.specials, "Драконы Чистейшего Света"));
  CHECK(gold && gold->keyRes == idByName(c.resources, "Золотые Драконы") && !idByName(c.specials, "Золотые Драконы"));
}

// ---------------------------------------------------------------- дополнение мира версии 5
// Мир версии 5: ветка из 99 технологий (как в версии 4) с изученностью, исследованием и своей технологией с условием
// «×50», множители провинций ×7 и ×12, прежнее название класса и слоты «Гильдии строителей» без собственного.
TEST(core_content_v6_migration) {
  World base = newWorld("Прежний мир");
  Tx tx(base);
  for (auto& [m, id] : industryOf(base)) tx.eraseTech(id);
  std::map<int, Id> old;
  Id prev = 0;
  for (int m = 2; m <= 100; m++) {
    Tech t;
    t.faction = 0;
    t.key = schema::industryKey(m);
    t.name = "Стимуляция промышленности " + std::to_string(m - 1);
    t.turns = 5;
    if (prev) t.prereqs = {prev};
    prev = old[m] = tx.add(std::move(t)).id;
  }
  Tech own;
  own.name = "Своя технология";
  own.prereqs = {old[50]};
  const Id ownTech = tx.add(std::move(own)).id;
  Faction fa;
  fa.name = "Арден";
  fa.res[kGold] = 0;
  for (int m = 2; m <= 7; m++) fa.techs[old[m]].studied = true;
  fa.techs[old[8]].research = true;
  fa.techs[old[8]].paid[kGold] = 80;
  const Id A = tx.add(std::move(fa)).id;
  Faction fb;
  fb.name = "Бельмар";
  for (int m = 2; m <= 12; m++) fb.techs[old[m]].studied = true;
  const Id B = tx.add(std::move(fb)).id;
  const Id works = buildingByKey(tx.w(), schema::bld::Steelworks);
  auto province = [&](Id owner, int mult) {
    Province p;
    p.name = "Провинция";
    p.owner = owner;
    ProvBuilding pb;
    pb.building = works;
    pb.mult = mult;
    p.buildings = {pb};
    return tx.add(std::move(p)).id;
  };
  const Id pa = province(A, 7), pb = province(B, 12);
  for (HeroClass& k : tx.catalogs().classes)
    if (k.name == "Паладин") k.name = "Палладин";
  for (int i = 1; i <= 5; i++) {
    const std::string mk = schema::levelModKey(schema::mod::BuildersLevel, i);
    const Id mid = tx.w().modifiers.firstId([&](const Modifier& m) { return m.key == mk; });
    if (mid) tx.modifier(mid).fx[size_t(Fx::Slots)] = i;
  }
  tx.meta().content = 5;
  const std::vector<std::string> notes = content::seed(tx);
  World w = std::move(tx).finish();
  CHECK_EQ(w.meta->content, content::kVersion);
  std::string all;
  for (const std::string& n : notes) all += n + "\n";
  CHECK_MSG(all.find("Стимуляция промышленности") != std::string::npos, all);
  // Семь ступеней; изучена ступень ×s, если изучена прежняя ×N ≥ s; идущее исследование вернуло уплаченное.
  const std::map<int, Id> br = industryOf(w);
  CHECK_EQ(br.size(), size_t(7));
  auto studied = [&](Id f) {
    std::vector<int> out;
    for (auto& [m, id] : br)
      if (auto it = w.faction(f)->techs.find(id); it != w.faction(f)->techs.end() && it->second.studied) out.push_back(m);
    return out;
  };
  CHECK((studied(A) == std::vector<int>{2, 5}));
  CHECK((studied(B) == std::vector<int>{2, 5, 10}));
  CHECK_EQ(w.faction(A)->techs.size(), size_t(2));   // записи удалённых технологий убраны
  CHECK_NEAR(w.faction(A)->stock(kGold), 80, 1e-9);
  // Множители провинций — в пределах доступного; условие своей технологии — ближайшая ступень не ниже.
  CHECK_EQ(w.province(pa)->buildings[0].mult, 5);
  CHECK_EQ(w.province(pb)->buildings[0].mult, 10);
  CHECK((w.tech(ownTech)->prereqs == std::vector<Id>{br.at(60)}));
  // Названия и слоты гильдии строителей.
  CHECK(idByName(w.catalogs->classes, "Паладин") && !idByName(w.catalogs->classes, "Палладин"));
  const Building* guild = w.building(buildingByKey(w, schema::bld::BuildersGuild));
  CHECK(guild && w.modifier(guild->levels[2].modifiers.at(0))->get(Fx::Slots) == 4);
  // Модификаторы базовых записей не задваиваются; второй раз мир не дополняется.
  int talentMods = 0;
  w.modifiers.each([&](const Modifier& m) { talentMods += m.name == "Талант: Доблесть"; });
  CHECK_EQ(talentMods, 1);
  Tx tx2(w);
  CHECK(content::seed(tx2).empty());
}
