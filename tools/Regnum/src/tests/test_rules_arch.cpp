// Regnum — правила археологии (ТЗ «Доработки №2»; «№4», п.1): группы (цена, начальный уровень, роспуск, модификаторы
// со сроками), одно задание за ход и раненые группы, «Найти археологическое место», этапы исследования с наградами и
// финальным сокровищем, трагедии и пробуждение бедствий (войска без государства, чума группы), спасение группы,
// раскопки, сундуки (случайные ресурсы, реликвии, вложенные), тайник реликвий, режим правки, справочники мест и сундуков.
#include "core/arch.h"
#include "core/content.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

Id resNamed(const World& w, const char* name) {
  for (const CatalogItem& r : w.catalogs->resources)
    if (r.name == name) return r.id;
  return 0;
}
Id essNamed(const World& w, const char* name) {
  for (const CatalogItem& e : w.catalogs->essences)
    if (e.name == name) return e.id;
  return 0;
}
Id siteKey(const World& w, const char* key) {
  const ArchSite* s = w.catalogs->archSiteByKey(key);
  return s ? s->id : 0;
}
Id chestKey(const World& w, const char* key) {
  const Chest* c = w.catalogs->chestByKey(key);
  return c ? c->id : 0;
}
double treasure(const World& w, Id state) { return w.faction(state)->stock(w.catalogs->resourceId(schema::kResArchTreasure)); }

// Государство A владеет p0…p3 (по 1000 жителей), достроена «Гильдия Археологов», казна 100; слоты p0 заданы явно:
// руины, склеп, долина, гробница (сокровища — первые сундуки списков наград).
struct ArchFix : Fix {
  Id guild = 0;
  ArchFix() {
    tx([&](Tx& t) {
      Id race = t.w().catalogs->races.empty() ? addCatalogItem(t, CatalogList::Races, "Люди") : t.w().catalogs->races.front().id;
      for (int i = 0; i < 4; i++) {
        t.province(p[i]).owner = A;
        t.province(p[i]).races = {RacePop{race, 1000}};
      }
      t.faction(A).res[kGold] = 100;
      guild = t.add(content::archGuildFor(A, {})).id;
      placeBuilding(t, p[0], guild, 1);
      const char* sites[kArchSlots] = {arch::site::Ruins, arch::site::Crypt, arch::site::Valley, arch::site::Tomb};
      for (int i = 0; i < kArchSlots; i++) {
        ArchSlot& s = t.province(p[0]).arch[size_t(i)];
        s = ArchSlot{};
        s.site = siteKey(t.w(), sites[i]);
        s.chest = t.w().catalogs->archSite(s.site)->rewards[size_t(i)].front();
      }
    });
  }
  Id group(const std::string& name = {}) {
    Id g = 0;
    tx([&](Tx& t) { g = createArchGroup(t, A, name); });
    return g;
  }
  // Группа снова свободна (новое задание в том же ходу) и без модификаторов.
  void refresh(Id g) {
    tx([&](Tx& t) {
      for (ArchGroup& x : t.faction(A).archGroups)
        if (x.id == g) {
          x.busy = 0;
          x.modifiers.clear();
          x.modTurns.clear();
        }
    });
  }
};

}  // namespace

// ---------------------------------------------------------------- группы
TEST(rules_arch_group_create_cost_and_level) {
  ArchFix f;
  // Без гильдии — нельзя.
  f.tx([&](Tx& tx) { demolish(tx, f.p[0], f.guild); });
  CHECK(has(errorOf([&] { f.group(); }), "Гильдия Археологов"));
  f.tx([&](Tx& tx) { placeBuilding(tx, f.p[0], f.guild, 1); });
  const i64 pop = statePopulation(f.w(), f.A);
  const Id g = f.group();
  CHECK_EQ(statePopulation(f.w(), f.A), pop - 100);
  CHECK_NEAR(f.w().faction(f.A)->treasury(), 95, 1e-9);
  const ArchGroup* ag = f.w().faction(f.A)->archGroup(g);
  CHECK(ag && ag->exp == 0 && ag->name == "Археологическая группа");
  CHECK(has(lastLog(f.w()), "формирует археологическую группу"));
  CHECK_EQ(logCount(f.w(), LogKind::Archaeology), 1);
  // Вторая — с новым именем; «Обученные исследователи» (начальный уровень +1) — со 2 уровня.
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Faction, f.A, makeMod(tx, {{Fx::ArchStartLevel, 1}})); });
  CHECK_EQ(archStartLevel(f.w(), f.A), 2);
  const Id g2 = f.group();
  CHECK_EQ(f.w().faction(f.A)->archGroup(g2)->exp, arch::levelMinExp(2));
  CHECK_EQ(f.w().faction(f.A)->archGroup(g2)->name, std::string("Археологическая группа 2"));
  // Золото кончилось — нельзя (сообщение в тысячах).
  f.tx([&](Tx& tx) { tx.faction(f.A).res[kGold] = 1; });
  CHECK(has(errorOf([&] { f.group(); }), "тыс."));
  // Государство нежити платит трупами.
  f.tx([&](Tx& tx) {
    tx.faction(f.A).res[kGold] = 10;
    setStateKind(tx, f.A, StateKind::Undead);
  });
  CHECK(archGroupCost(f.w(), f.A).corpses);
  CHECK(has(errorOf([&] { f.group(); }), "трупов"));
  const Id corpses = resourceId(f.w(), schema::kResCorpses);
  f.tx([&](Tx& tx) { tx.faction(f.A).res[corpses] = 150; });
  const i64 pop2 = statePopulation(f.w(), f.A);
  f.group("Мертвецы-копатели");
  CHECK_NEAR(f.w().faction(f.A)->stock(corpses), 50, 1e-9);
  CHECK_EQ(statePopulation(f.w(), f.A), pop2);
  // Переименование, опыт, роспуск.
  f.tx([&](Tx& tx) {
    renameArchGroup(tx, f.A, g, "Первая экспедиция");
    setArchGroupExp(tx, f.A, g, 460);
  });
  CHECK_EQ(archStats(f.w(), f.A, g).level, 4);
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setArchGroupExp(tx, f.A, g, 1001); }); }), "1000"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { renameArchGroup(tx, f.A, g, "  "); }); }), "пустым"));
  f.tx([&](Tx& tx) { disbandArchGroup(tx, f.A, g); });
  CHECK(!f.w().faction(f.A)->archGroup(g));
  CHECK_EQ(f.w().faction(f.A)->archGroups.size(), size_t(2));
}

TEST(rules_arch_group_modifiers_and_turns) {
  ArchFix f;
  const Id g = f.group();
  Id own = 0, global = 0;
  f.tx([&](Tx& tx) {
    own = makeMod(tx, {{Fx::ArchVitalityPct, 5}});
    tx.modifier(own).kind = ModKind::ArchGroup;
    tx.modifier(own).duration = 3;
    global = makeMod(tx, {{Fx::ArchSuccessPct, 2}});
    tx.modifier(global).kind = ModKind::Faction;
  });
  // Группе — только модификаторы археологических групп.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setArchGroupModifiers(tx, f.A, g, {global}); }); }), "не модификатор археологических групп"));
  f.tx([&](Tx& tx) { setArchGroupModifiers(tx, f.A, g, {own}); });
  CHECK_EQ(f.w().faction(f.A)->archGroup(g)->modTurns.at(own), 3);   // срок по умолчанию
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Faction, f.A, global); });   // глобальный — на все группы
  const ArchStats st = archStats(f.w(), f.A, g);
  CHECK_NEAR(st.vitality, 3 + 5, 1e-9);
  CHECK_NEAR(st.success, 3 + 2, 1e-9);
  f.tx([&](Tx& tx) { setArchGroupModTurns(tx, f.A, g, own, 0); });
  CHECK(!f.w().faction(f.A)->archGroup(g)->modTurns.count(own));
  // Удалённый модификатор уходит и из групп.
  f.tx([&](Tx& tx) { setArchGroupModTurns(tx, f.A, g, own, 2); });
  f.tx([&](Tx& tx) { removeModifier(tx, own); });
  CHECK(f.w().faction(f.A)->archGroup(g)->modifiers.empty() && f.w().faction(f.A)->archGroup(g)->modTurns.empty());
  // Ранение — 2 хода, раненую нельзя назначить; после двух ходов модификатор снимается сам.
  f.tx([&](Tx& tx) { addArchGroupModifier(tx, f.A, g, ensureBuiltinMod(tx, schema::mod::ArchWounded)); });
  CHECK(archStats(f.w(), f.A, g).wounded);
  std::string why;
  CHECK(!archGroupReady(f.w(), f.A, g, &why) && has(why, "ранена"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { discoverSite(tx, f.A, g, f.p[0]); }); }), "ранена"));
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(!archGroupReady(f.w(), f.A, g));
  f.tx([&](Tx& tx) { endTurn(tx); });
  CHECK(archGroupReady(f.w(), f.A, g));
  CHECK(!archStats(f.w(), f.A, g).wounded);
}

// ---------------------------------------------------------------- обнаружение
TEST(rules_arch_discover_nearest_slot_once_per_turn) {
  ArchFix f;
  const Id g = f.group();
  CHECK_NEAR(discoveryChance(f.w(), f.A, g), 53, 1e-9);   // 50 % + 3 % первого уровня
  CHECK(canDiscover(f.w(), f.A, f.p[0]));
  CHECK(!canDiscover(f.w(), f.A, f.p[5]));   // не своя провинция
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { discoverSite(tx, f.A, g, f.p[5]); }); }), "не принадлежит"));
  ArchReport r;
  f.tx([&](Tx& tx) { r = discoverSite(tx, f.A, g, f.p[0]); });
  CHECK(has(r.title, "Результат исследования в провинции «П0»"));
  // Одно задание за ход.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { discoverSite(tx, f.A, g, f.p[0]); }); }), "в этом ходу"));
  int found = r.success ? 1 : 0, tries = 1;
  const u32 rng0 = f.w().meta->rng;
  CHECK(rng0 > 0);
  while (found < kArchSlots && tries < 200) {
    f.refresh(g);
    f.tx([&](Tx& tx) { r = discoverSite(tx, f.A, g, f.p[0]); });
    tries++;
    if (r.success) {
      CHECK_EQ(r.slot, found);   // ближайший закрытый слот
      CHECK_EQ(r.site, f.w().province(f.p[0])->arch[size_t(found)].site);
      found++;
    } else {
      CHECK(r.slot < 0 && r.lines.empty());
    }
  }
  CHECK_EQ(found, kArchSlots);
  for (const ArchSlot& s : f.w().province(f.p[0])->arch) CHECK(s.open && s.stage == 0);
  CHECK(!canDiscover(f.w(), f.A, f.p[0]));
  CHECK_EQ(logCount(f.w(), LogKind::Archaeology), tries + 1);
}

TEST(rules_arch_discover_chance_with_tech) {
  ArchFix f;
  const Id g = f.group();
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Faction, f.A, makeMod(tx, {{Fx::ArchDiscoveryPct, 10}})); });
  CHECK_NEAR(discoveryChance(f.w(), f.A, g), 63, 1e-9);
  // Успех группы не выше 25 %.
  f.tx([&](Tx& tx) { addModifier(tx, ModTarget::Faction, f.A, makeMod(tx, {{Fx::ArchSuccessPct, 50}})); });
  CHECK_NEAR(discoveryChance(f.w(), f.A, g), 50 + 25 + 10, 1e-9);
}

// ---------------------------------------------------------------- исследование мест
TEST(rules_arch_explore_stages_rewards_and_final_chest) {
  ArchFix f;
  const Id g = f.group();
  f.tx([&](Tx& tx) { setArchSlotOpen(tx, f.p[0], 0, true); });
  const Id finalChest = f.w().province(f.p[0])->arch[0].chest;
  CHECK(finalChest != 0);
  CHECK_NEAR(exploreChance(f.w(), f.A, g, f.p[0], 0), 85 + 3, 1e-9);
  CHECK(!canExplore(f.w(), f.A, f.p[0], 1));   // слот 2 ещё закрыт
  int successes = 0, tries = 0;
  ArchReport r;
  while (!arch::slotDone(f.w().province(f.p[0])->arch[0], 0) && tries < 100) {
    const int stage = f.w().province(f.p[0])->arch[0].stage;
    const int expBefore = f.w().faction(f.A)->archGroup(g)->exp;
    const double tBefore = treasure(f.w(), f.A);
    CHECK_NEAR(exploreChance(f.w(), f.A, g, f.p[0], 0), arch::stageSuccess(0, stage + 1, 3), 1e-9);
    f.tx([&](Tx& tx) { r = exploreSite(tx, f.A, g, f.p[0], 0); });
    tries++;
    CHECK_EQ(r.stage, stage + 1);
    if (r.success) {
      successes++;
      CHECK_EQ(f.w().province(f.p[0])->arch[0].stage, stage + 1);
      CHECK_EQ(r.exp, arch::stageInfo(0, stage + 1).exp);
      if (stage == 0) {   // этап 1: только 5–10 сокровищ
        CHECK(r.gains.size() == 1 && r.gains[0].kind == ArchGain::Resource);
        CHECK(treasure(f.w(), f.A) - tBefore >= 5 - 1e-9 && treasure(f.w(), f.A) - tBefore <= 10 + 1e-9);
      }
      if (r.done) {
        CHECK(!r.gains.empty() && r.gains[0].kind == ArchGain::Chest && r.gains[0].id == finalChest);
        CHECK(treasure(f.w(), f.A) > tBefore);
      }
    } else {
      CHECK_EQ(f.w().faction(f.A)->archGroup(g)->exp - expBefore, arch::kFailExp);   // на обычном месте трагедий нет
      CHECK(r.tragedy == ArchReport::NoTragedy);
    }
    f.refresh(g);
  }
  CHECK_EQ(successes, 3);
  CHECK(r.done);
  CHECK(arch::slotDone(f.w().province(f.p[0])->arch[0], 0));
  std::string why;
  CHECK(!canExplore(f.w(), f.A, f.p[0], 0, &why) && has(why, "полностью"));
  // Финальный сундук «Деревянный сундук/ларь» даёт обычную находку — находок нет: строка «не нашлось».
  bool noRelic = false;
  for (const ArchGain& x : r.gains) noRelic = noRelic || x.kind == ArchGain::NoRelic;
  CHECK(noRelic);
  CHECK(has(lastLog(f.w()), "исследовано полностью"));
}

TEST(rules_arch_experience_bonus_and_cap) {
  ArchFix f;
  const Id g = f.group();
  f.tx([&](Tx& tx) {
    Id m = makeMod(tx, {{Fx::ArchExpPct, 20}});
    tx.modifier(m).kind = ModKind::ArchGroup;
    setArchGroupModifiers(tx, f.A, g, {m});
    setArchGroupExp(tx, f.A, g, 995);
    setArchSlotOpen(tx, f.p[0], 0, true);
  });
  ArchReport r;
  f.tx([&](Tx& tx) { r = exploreSite(tx, f.A, g, f.p[0], 0); });
  CHECK_EQ(f.w().faction(f.A)->archGroup(g)->exp, 1000);   // не больше 1000
  CHECK_EQ(r.exp, 5);
  CHECK_EQ(r.levelAfter, 5);
}

// ---------------------------------------------------------------- трагедии и бедствия
TEST(rules_arch_tragedies_calamities_and_rescue) {
  ArchFix f;
  const Id death = essNamed(f.w(), "Эссенция смерти");
  f.tx([&](Tx& tx) {
    setArchSlotOpen(tx, f.p[0], 3, true);   // легендарное место «Древняя гробница»
    tx.faction(f.A).ess[death] = 100000;
    tx.faction(f.A).res[kGold] = 10000;
    for (int i = 0; i < 4; i++) tx.province(f.p[i]).races[0].pop = 100000;
  });
  Id g = f.group();
  bool wound = false, awaiting = false, dangerWound = false, plague = false, guards = false, special = false;
  int tries = 0;
  while (tries < 6000 && !(wound && awaiting && dangerWound && plague && guards && special)) {
    f.tx([&](Tx& tx) { tx.province(f.p[0]).arch[3].stage = 7; });   // этап 8: неудача — 25 % трагедии
    ArchReport r;
    f.tx([&](Tx& tx) { r = exploreSite(tx, f.A, g, f.p[0], 3); });
    tries++;
    if (r.tragedy == ArchReport::Wound) {
      wound = true;
      CHECK(r.wounded && archStats(f.w(), f.A, g).wounded);
    }
    if (r.tragedy == ArchReport::Danger || r.calamity == arch::kGuardsTitle) {
      if (r.awaiting) {
        // Смертельная опасность без ранения: решение о спасении.
        CHECK(!r.wounded);
        CHECK(f.w().faction(f.A)->archGroup(g)->danger);
        CHECK(!archGroupReady(f.w(), f.A, g, nullptr));
        if (!awaiting) {
          awaiting = true;
          const double before = f.w().faction(f.A)->essence(death);
          const std::vector<Id> can = divineEssences(f.w(), f.A);   // финальные сундуки гробницы тоже дают эссенции
          CHECK(std::find(can.begin(), can.end(), death) != can.end());
          f.tx([&](Tx& tx) { resolveArchDanger(tx, f.A, g, death); });
          CHECK_NEAR(f.w().faction(f.A)->essence(death), before - 1000, 1e-9);
          CHECK(archStats(f.w(), f.A, g).wounded);
          CHECK(!f.w().faction(f.A)->archGroup(g)->danger);
          CHECK(has(lastLog(f.w()), "Божественное вмешательство"));
          CHECK_THROWS(f.tx([&](Tx& tx) { resolveArchDanger(tx, f.A, g, death); }));   // опасности уже нет
        } else {
          f.tx([&](Tx& tx) { resolveArchDanger(tx, f.A, g, 0); });   // отказ — группа погибает
          CHECK(!f.w().faction(f.A)->archGroup(g));
          CHECK(has(lastLog(f.w()), "погибла"));
          g = f.group();
          continue;
        }
      } else {
        dangerWound = dangerWound || r.wounded;
      }
    }
    if (r.tragedy == ArchReport::Calamity) {
      if (r.calamity == arch::kPlagueTitle) {
        plague = true;
        CHECK(r.plague && archStats(f.w(), f.A, g).plague);
      } else if (r.calamity == arch::kGuardsTitle) {
        guards = true;
        CHECK(r.army != 0);
        const Army* a = f.w().army(r.army);
        CHECK(a && a->leader() == wildFaction(f.w()));
        CHECK_EQ(a->groups[0].units[0].count, i64(5000));
      } else {
        special = true;
        CHECK_EQ(r.calamity, std::string("Пробуждение древней гробницы!"));
        const Army* a = f.w().army(r.army);
        CHECK(a && a->groups[0].units.size() == 3);
        const Faction* wf = f.w().faction(wildFaction(f.w()));
        CHECK(wf && wf->armyRow(a->groups[0].units[0].row)->race == schema::kRaceUndead);
      }
    }
    if (r.army) f.tx([&](Tx& tx) { tx.eraseArmy(r.army); });   // место у провинции не кончается
    f.refresh(g);
  }
  CHECK(wound);
  CHECK(awaiting);
  CHECK(dangerWound);
  CHECK(plague);
  CHECK(guards);
  CHECK(special);
}

TEST(rules_arch_vault_calamity_always_army) {
  ArchFix f;
  const Id vault = siteKey(f.w(), arch::site::Vault);
  f.tx([&](Tx& tx) {
    setArchSlotSite(tx, f.p[0], 3, vault);
    setArchSlotOpen(tx, f.p[0], 3, true);
    tx.faction(f.A).res[kGold] = 100000;
    for (int i = 0; i < 4; i++) tx.province(f.p[i]).races[0].pop = 1000000;
  });
  CHECK_EQ(f.w().province(f.p[0])->arch[3].chest, chestKey(f.w(), "emperor"));
  Id g = f.group();
  int calamities = 0;
  for (int tries = 0; tries < 8000 && calamities < 3; tries++) {
    f.tx([&](Tx& tx) { tx.province(f.p[0]).arch[3].stage = 7; });
    ArchReport r;
    f.tx([&](Tx& tx) { r = exploreSite(tx, f.A, g, f.p[0], 3); });
    if (r.tragedy == ArchReport::Calamity) {
      calamities++;
      CHECK_EQ(r.calamity, std::string("Хранители Императора здесь!"));
      CHECK(r.army && f.w().army(r.army)->groups[0].units.size() == 3);
    }
    if (r.awaiting) {
      f.tx([&](Tx& tx) { resolveArchDanger(tx, f.A, g, 0); });
      g = f.group();
      continue;
    }
    f.refresh(g);
  }
  CHECK(calamities >= 3);
}

TEST(rules_arch_group_plague_infects_province) {
  ArchFix f;
  const Id g = f.group();
  f.tx([&](Tx& tx) { addArchGroupModifier(tx, f.A, g, ensureBuiltinMod(tx, schema::mod::ArchPlague)); });
  CHECK_EQ(f.w().faction(f.A)->archGroup(g)->modTurns.at(builtinModId(f.w(), schema::mod::ArchPlague)), arch::kPlagueTurns);
  CHECK(archGroupReady(f.w(), f.A, g));   // с чумой — можно
  ArchReport r;
  f.tx([&](Tx& tx) { r = discoverSite(tx, f.A, g, f.p[0]); });
  CHECK_EQ(r.infected, f.p[0]);
  CHECK(provinceHas(f.w(), f.p[0], schema::mod::Plague));
  // Иммунитет уважается.
  f.refresh(g);
  f.tx([&](Tx& tx) {
    addArchGroupModifier(tx, f.A, g, ensureBuiltinMod(tx, schema::mod::ArchPlague));
    addModifier(tx, ModTarget::Province, f.p[1], ensureBuiltinMod(tx, schema::mod::PlagueImmunity));
    tx.province(f.p[1]).arch[0].site = siteKey(tx.w(), arch::site::Ruins);
  });
  f.tx([&](Tx& tx) { r = discoverSite(tx, f.A, g, f.p[1]); });
  CHECK_EQ(r.infected, Id(0));
  CHECK(!provinceHas(f.w(), f.p[1], schema::mod::Plague));
}

// ---------------------------------------------------------------- раскопки
TEST(rules_arch_excavation) {
  ArchFix f;
  Id find = 0, foreign = 0, hero = 0;
  f.tx([&](Tx& tx) {
    for (int i = 0; i < kArchSlots; i++) {
      tx.province(f.p[0]).arch[size_t(i)].open = true;
      tx.province(f.p[0]).arch[size_t(i)].stage = arch::stagesOf(i);
    }
    tx.faction(f.A).res[kGold] = 100000;
    for (int i = 0; i < 4; i++) tx.province(f.p[i]).races[0].pop = 1000000;
    const Id finds = tx.w().catalogs->relicGroupId(schema::kRelicArchFinds);
    find = addRelic(tx, "Черепок", Rarity::Rare);
    foreign = addRelic(tx, "Чужой амулет", Rarity::Epic);
    for (Relic& r : tx.catalogs().relics)
      if (r.id == find) r.group = finds;
    hero = createCharacter(tx, f.B, "Чужак");
    giveRelic(tx, hero, foreign);
  });
  CHECK(canExcavate(f.w(), f.A, f.p[0]));
  CHECK(!canExcavate(f.w(), f.A, f.p[1]));
  // Реликвию спрятало государство B (провинция тогда была его).
  f.tx([&](Tx& tx) {
    tx.province(f.p[0]).owner = f.B;
    hideRelic(tx, f.p[0], foreign);
    tx.province(f.p[0]).owner = f.A;
  });
  CHECK_EQ(f.w().province(f.p[0])->hiddenBy, f.B);
  Id g = f.group();
  bool tr = false, scroll = false, relicHidden = false, relicFind = false, nothing = false;
  const Id scrolls = f.w().catalogs->resourceId(schema::kResShantiriScrolls);
  for (int tries = 0; tries < 3000 && !(tr && scroll && relicHidden && relicFind && nothing); tries++) {
    ArchReport r;
    f.tx([&](Tx& tx) { r = excavate(tx, f.A, g, f.p[0]); });
    CHECK(has(r.title, "Результат раскопок"));
    if (!r.success) nothing = true;
    for (const ArchGain& x : r.gains) {
      if (x.kind == ArchGain::Resource && x.id == scrolls) scroll = true;
      if (x.kind == ArchGain::Resource && x.id != scrolls) {
        tr = true;
        CHECK(x.amount >= 50 && x.amount <= 200);
      }
      if (x.kind == ArchGain::Relic && x.id == foreign) relicHidden = true;
      if (x.kind == ArchGain::Relic && x.id == find) relicFind = true;
    }
    f.refresh(g);
  }
  CHECK(tr && scroll && nothing);
  CHECK(relicHidden);
  CHECK(relicFind);
  CHECK(relicPlace(f.w(), foreign).kind == RelicPlace::State && relicPlace(f.w(), foreign).id == f.A);
  CHECK_EQ(f.w().province(f.p[0])->hiddenRelic, Id(0));
  CHECK(relicPlace(f.w(), find).kind == RelicPlace::State);
}

// ---------------------------------------------------------------- сундуки
TEST(rules_arch_open_chests) {
  ArchFix f;
  Id common = 0, rare = 0;
  f.tx([&](Tx& tx) {
    const Id finds = tx.w().catalogs->relicGroupId(schema::kRelicArchFinds);
    common = addRelic(tx, "Глиняная табличка", Rarity::Common);
    rare = addRelic(tx, "Бронзовый жезл", Rarity::Rare);
    for (Relic& r : tx.catalogs().relics)
      if (r.id == common || r.id == rare) r.group = finds;
  });
  std::vector<ArchGain> got;
  Rng rng(7);
  // «Небольшой сундук»: 50 сокровищ (+10 %) и 1 тыс. золота.
  const double gold0 = f.w().faction(f.A)->treasury();
  f.tx([&](Tx& tx) { got = openChest(tx, f.A, chestKey(tx.w(), "small"), rng, 10); });
  CHECK_EQ(got.size(), size_t(3));
  CHECK(got[0].kind == ArchGain::Chest && got[0].depth == 0 && got[1].depth == 1);
  CHECK_NEAR(treasure(f.w(), f.A), 55, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->treasury(), gold0 + 1, 1e-9);
  // «Ящик»: случайный ресурс подгруппы «Руда / Обычная».
  f.tx([&](Tx& tx) { got = openChest(tx, f.A, chestKey(tx.w(), "box"), rng, 0); });
  const Id oreCommon = f.w().catalogs->groupId(schema::grp::OreCommon);
  CHECK(got.size() == 3 && got[2].kind == ArchGain::Resource && f.w().catalogs->resourceIn(got[2].id, oreCommon));
  // «Деревянный ларь»: без «Нефти».
  const Id oil = resNamed(f.w(), "Нефть");
  for (int i = 0; i < 40; i++) {
    f.tx([&](Tx& tx) { got = openChest(tx, f.A, chestKey(tx.w(), "woodCoffer"), rng, 0); });
    for (const ArchGain& x : got) CHECK(!(x.kind == ArchGain::Resource && x.id == oil && oil != 0));
  }
  // Обычная находка ушла государству при первом «Деревянном ларе», дальше — «не нашлось».
  CHECK(relicPlace(f.w(), common).kind == RelicPlace::State);
  CHECK(got.back().kind == ArchGain::NoRelic);
  CHECK(relicPlace(f.w(), rare).kind == RelicPlace::Free);
  // «Ларец»: случайная эссенция 50.
  f.tx([&](Tx& tx) { got = openChest(tx, f.A, chestKey(tx.w(), "casket"), rng, 0); });
  CHECK(got.size() == 3 && got[2].kind == ArchGain::Essence && got[2].amount == 50);
  CHECK_NEAR(f.w().faction(f.A)->essence(got[2].id), 50, 1e-9);
  // «Сундук древнего некроманта»: эссенция смерти 400 и трупы 500.
  const Id death = essNamed(f.w(), "Эссенция смерти"), corpses = resourceId(f.w(), schema::kResCorpses);
  const double death0 = f.w().faction(f.A)->essence(death), corpses0 = f.w().faction(f.A)->stock(corpses);
  f.tx([&](Tx& tx) { got = openChest(tx, f.A, chestKey(tx.w(), "necromancer"), rng, 0); });
  CHECK_NEAR(f.w().faction(f.A)->essence(death) - death0, 400, 1e-9);
  CHECK_NEAR(f.w().faction(f.A)->stock(corpses) - corpses0, 500, 1e-9);
  // «Сокровище Бессмертного Императора»: вложенный сундук (глубина 1) и его содержимое (глубина 2).
  f.tx([&](Tx& tx) { got = openChest(tx, f.A, chestKey(tx.w(), "emperor"), rng, 0); });
  CHECK(got.size() >= 3 && got[1].kind == ArchGain::Chest && got[1].depth == 1 && got[2].depth == 2);
  const std::vector<Id> four = {chestKey(f.w(), "offering"), chestKey(f.w(), "jarl"), chestKey(f.w(), "necromancer"), chestKey(f.w(), "cultist")};
  CHECK(std::find(four.begin(), four.end(), got[1].id) != four.end());
  CHECK_EQ(archGainText(f.w(), got[0]), std::string("Сокровище Бессмертного Императора"));
}

TEST(rules_arch_nested_chest_cycle_is_bounded) {
  ArchFix f;
  Id a = 0, b = 0;
  f.tx([&](Tx& tx) {
    a = addChest(tx, "Матрёшка");
    b = addChest(tx, "Вторая матрёшка");
    Chest ca = *tx.w().catalogs->chest(a);
    ca.items = {ChestItem{ChestItemKind::Gold, 1}, ChestItem{ChestItemKind::Chest, 1, 0, 0, {}, 0, 0, 0, {b}}};
    setChest(tx, ca);
    Chest cb = *tx.w().catalogs->chest(b);
    cb.items = {ChestItem{ChestItemKind::Chest, 1, 0, 0, {}, 0, 0, 0, {a}}};
    setChest(tx, cb);
  });
  CHECK(has(errorOf([&] {
          f.tx([&](Tx& tx) {
            Chest c = *tx.w().catalogs->chest(a);
            c.items.push_back(ChestItem{ChestItemKind::Chest, 1, 0, 0, {}, 0, 0, 0, {a}});
            setChest(tx, c);
          });
        }),
        "сам в себе"));
  std::vector<ArchGain> got;
  Rng rng(1);
  f.tx([&](Tx& tx) { got = openChest(tx, f.A, a, rng, 0); });
  int maxDepth = 0;
  for (const ArchGain& x : got) maxDepth = std::max(maxDepth, x.depth);
  CHECK(maxDepth <= 5);
  CHECK(got.size() < 20);
}

// ---------------------------------------------------------------- тайник
TEST(rules_arch_hide_and_unearth_relic) {
  ArchFix f;
  Id hero = 0, relic = 0, store = 0;
  f.tx([&](Tx& tx) {
    hero = createCharacter(tx, f.A, "Хранитель");
    relic = addRelic(tx, "Корона Севера", Rarity::Legendary);
    giveRelic(tx, hero, relic);
    store = createBuilding(tx, 0, "Сокровищница");
    setBuildingFlag(tx, store, BuildingFlag::RelicStore, true);
    placeBuilding(tx, f.p[1], store, 1);
  });
  CHECK((hideableRelics(f.w(), f.p[0]) == std::vector<Id>{relic}));
  const int logs = int(f.w().log.size());
  f.tx([&](Tx& tx) { hideRelic(tx, f.p[0], relic); });
  CHECK_EQ(f.w().province(f.p[0])->hiddenRelic, relic);
  CHECK_EQ(f.w().province(f.p[0])->hiddenBy, f.A);
  CHECK(f.w().character(hero)->inventory.empty());
  CHECK_EQ(int(f.w().log.size()), logs);   // без хроники
  // Второй тайник в провинции нельзя.
  Id other = 0;
  f.tx([&](Tx& tx) {
    other = addRelic(tx, "Кубок", Rarity::Common);
    giveRelic(tx, hero, other);
  });
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { hideRelic(tx, f.p[0], other); }); }), "уже спрятана"));
  // Провинцией владеет другое государство — откопать нельзя.
  f.tx([&](Tx& tx) { tx.province(f.p[0]).owner = f.B; });
  CHECK(!canUnearth(f.w(), f.p[0]));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { unearthRelic(tx, f.p[0], RelicPlace{RelicPlace::Hero, hero, 0, f.A}); }); }), "спрятавшее"));
  f.tx([&](Tx& tx) { tx.province(f.p[0]).owner = f.A; });
  CHECK(canUnearth(f.w(), f.p[0]));
  // В хранилище постройки своего государства.
  const int logs2 = int(f.w().log.size());
  f.tx([&](Tx& tx) { unearthRelic(tx, f.p[0], RelicPlace{RelicPlace::Building, f.p[1], store, f.A}); });
  CHECK(relicPlace(f.w(), relic).kind == RelicPlace::Building);
  CHECK_EQ(f.w().province(f.p[0])->hiddenRelic, Id(0));
  CHECK_EQ(int(f.w().log.size()), logs2);
  // Из хранилища — снова спрятать, откопать герою.
  f.tx([&](Tx& tx) { hideRelic(tx, f.p[0], relic); });
  f.tx([&](Tx& tx) { unearthRelic(tx, f.p[0], RelicPlace{RelicPlace::Hero, hero, 0, f.A}); });
  CHECK_EQ(relicHolder(f.w(), relic), hero);
}

// ---------------------------------------------------------------- режим правки
TEST(rules_arch_edit_mode_slots) {
  ArchFix f;
  const Id crypt = siteKey(f.w(), arch::site::Crypt), valley = siteKey(f.w(), arch::site::Valley);
  f.tx([&](Tx& tx) {
    setArchSlotOpen(tx, f.p[0], 0, true);
    tx.province(f.p[0]).arch[0].stage = 2;
  });
  // Место, которое уже есть в провинции, — нельзя.
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setArchSlotSite(tx, f.p[0], 0, crypt); }); }), "уже есть"));
  // Замена: этапы сброшены, сокровище — из наград нового места для слота.
  f.tx([&](Tx& tx) { setArchSlotSite(tx, f.p[0], 2, siteKey(tx.w(), arch::site::Temple)); });
  const ArchSlot s2 = f.w().province(f.p[0])->arch[2];
  const auto& rewards = f.w().catalogs->archSite(s2.site)->rewards[2];
  CHECK(std::find(rewards.begin(), rewards.end(), s2.chest) != rewards.end());
  f.tx([&](Tx& tx) { setArchSlotSite(tx, f.p[0], 0, valley); });
  CHECK(f.w().province(f.p[0])->arch[0].open && f.w().province(f.p[0])->arch[0].stage == 0);
  // Закрытие сбрасывает этапы.
  f.tx([&](Tx& tx) { tx.province(f.p[0]).arch[0].stage = 1; });
  f.tx([&](Tx& tx) { setArchSlotOpen(tx, f.p[0], 0, false); });
  CHECK(!f.w().province(f.p[0])->arch[0].open && f.w().province(f.p[0])->arch[0].stage == 0);
  CHECK_EQ(logCount(f.w(), LogKind::Archaeology), 0);
}

// ---------------------------------------------------------------- справочники
TEST(rules_arch_catalogs_sites_and_chests) {
  ArchFix f;
  const Id ruins = siteKey(f.w(), arch::site::Ruins);
  CHECK_EQ(f.w().catalogs->archSites.size(), size_t(10));
  CHECK_EQ(f.w().catalogs->chests.size(), size_t(16));
  f.tx([&](Tx& tx) { setArchSite(tx, ruins, "Руины первых королей", "castle"); });
  CHECK_EQ(f.w().catalogs->archSite(ruins)->name, std::string("Руины первых королей"));
  CHECK(has(errorOf([&] { f.tx([&](Tx& tx) { setArchSite(tx, ruins, "Заброшенный склеп", ""); }); }), "уже есть"));
  // Новый сундук: ресурс только из «Руды»/«Материалов» или «Трупы».
  Id c = 0;
  f.tx([&](Tx& tx) { c = addChest(tx, "Ларчик"); });
  const Id corpses = resourceId(f.w(), schema::kResCorpses);
  CHECK(chestResourceAllowed(f.w(), corpses));
  CHECK(!chestResourceAllowed(f.w(), kGold));
  CHECK(chestGroupAllowed(f.w(), f.w().catalogs->groupId(schema::grp::MatRaw)));
  CHECK(!chestGroupAllowed(f.w(), f.w().catalogs->groupId(schema::grp::Beasts)));
  CHECK(has(errorOf([&] {
          f.tx([&](Tx& tx) {
            Chest x = *tx.w().catalogs->chest(c);
            x.items = {ChestItem{ChestItemKind::Resource, 10, kGold}};
            setChest(tx, x);
          });
        }),
        "Руда"));
  f.tx([&](Tx& tx) {
    Chest x = *tx.w().catalogs->chest(c);
    x.items = {ChestItem{ChestItemKind::Resource, 10, corpses}, ChestItem{ChestItemKind::Treasure, 5}};
    setChest(tx, x);
    setArchSiteRewards(tx, ruins, 0, {c});
  });
  CHECK_EQ(chestItemText(f.w(), f.w().catalogs->chest(c)->items[0]), std::string("Трупы 10"));
  // Сокровище p0 слота 1 (руины) — сундук из старого списка; удаление этого сундука выбирает новое из наград места.
  const Id old = f.w().province(f.p[0])->arch[0].chest;
  f.tx([&](Tx& tx) { removeChest(tx, old); });
  CHECK(!f.w().catalogs->chest(old));
  CHECK_EQ(f.w().province(f.p[0])->arch[0].chest, c);
  for (const ArchSite& s : f.w().catalogs->archSites)
    for (const auto& list : s.rewards) CHECK(std::find(list.begin(), list.end(), old) == list.end());
  // Удаление вложенного сундука убирает его из списков «Сундук» (пустое поле — убирается).
  const Id offering = chestKey(f.w(), "offering");
  f.tx([&](Tx& tx) { removeChest(tx, offering); });
  for (const ChestItem& it : f.w().catalogs->chest(chestKey(f.w(), "emperor"))->items)
    if (it.kind == ChestItemKind::Chest) CHECK(std::find(it.chests.begin(), it.chests.end(), offering) == it.chests.end());
}

// ---------------------------------------------------------------- ветка технологий «Археология» (ТЗ «Доработки №4», п.1)
TEST(rules_arch_tech_branch_effects) {
  ArchFix f;
  auto techKey = [&](const char* key) {
    Id id = 0;
    f.w().techs.each([&](const Tech& t) {
      if (!id && t.faction == 0 && t.key == key) id = t.id;
    });
    return id;
  };
  const Id basics = techKey("archBasics"), tools = techKey("archTools"), trained = techKey("archTrained"), diligent = techKey("archDiligent");
  CHECK(basics && tools && trained && diligent);
  f.tx([&](Tx& tx) {
    for (Id t : {basics, tools, trained, diligent}) setStudied(tx, t, true, f.A);
  });
  // «Обученные исследователи»: новые группы — со 2 уровня.
  const Id g = f.group();
  CHECK_EQ(archStats(f.w(), f.A, g).level, 2);
  // «Старательные искатели»: +10 % к обнаружению (50 % + 5 % второго уровня + 10 %).
  CHECK_NEAR(discoveryChance(f.w(), f.A, g), 65, 1e-9);
  // «Основы археологии» и «Продвинутые инструменты»: +1 % + 1 % сокровищ от всех действий групп.
  const ArchStats st = archStats(f.w(), f.A, g);
  CHECK_NEAR(st.treasurePct, 2, 1e-9);
  std::vector<ArchGain> got;
  Rng rng(3);
  f.tx([&](Tx& tx) { got = openChest(tx, f.A, chestKey(tx.w(), "small"), rng, st.treasurePct); });
  CHECK_NEAR(treasure(f.w(), f.A), 51, 1e-9);
}
