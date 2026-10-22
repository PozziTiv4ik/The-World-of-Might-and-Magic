// Regnum — базовые записи мира, версия 4 (ТЗ «Доработки №1–2» 2026-10-22): ресурсы «Мех», «Прочная чешуя», «Хитин»,
// «Титанобоа», свой значок «Археологических сокровищ» (не путается с золотом), постройки общего дерева (ферма, бойня,
// алтарь культа, храмы освящения и осквернения, обогатитель бездны, некротический обогатитель, жилой квартал, военная
// и инженерная мастерские, углежогная яма, гильдия строителей), технологии «Производство пороха» и ветка «Стимуляция
// промышленности» (множители производства ×2…×100), штабы разведки и контрразведки у каждого государства.
#include "core/content_seed.h"
#include "core/schema.h"

namespace rg::content::detail {

namespace {

using Cost = std::vector<std::pair<const char*, double>>;

// Уровень постройки: цена, добыча +% (ферма), производство за ход, модификатор уровня (ключ — префикс и номер).
struct LevelV4 {
  Cost cost;
  double extract = 0;
  double produce = 0;
};

// Рецепт: название, ресурсы и эссенции на входе, ресурсы на выходе.
struct RecipeV4 {
  const char* name;
  Cost in;
  Cost essIn;
  Cost out;
};

}  // namespace

// ---- ресурсы, значок археологических сокровищ
void Seeder::resourcesV4() {
  resource(item(Named{"Мех", 0x8a6a4f, "resource"}, groupOf(schema::grp::MatRaw)));
  resource(item(Named{"Прочная чешуя", 0x5f8a6a, "shield"}, groupOf(schema::grp::MatRaw)));
  resource(item(Named{"Хитин", 0x7a5a2f, "shield"}, groupOf(schema::grp::MatRaw)));
  resource(item(Named{"Титанобоа", 0x4f7a3a, "u-beasts"}, groupOf(schema::grp::WarBeasts)));
  // «Археологические сокровища» — амфора терракотового цвета вместо монет золота.
  for (CatalogItem& c : cat().resources)
    if (c.key == schema::kResArchTreasure && (c.icon == "coins" || c.icon.empty())) {
      const schema::BuiltinResource* b = schema::builtinResource(schema::kResArchTreasure);
      c.icon = b->icon;
      c.color = Color::hex(b->color);
      notes.push_back("у «Археологических сокровищ» новый значок");
    }
  std::stable_sort(cat().resources.begin(), cat().resources.end(), [](const CatalogItem& a, const CatalogItem& b) {
    if ((a.id == kGold) != (b.id == kGold)) return a.id == kGold;
    return compareRu(a.name, b.name) < 0;
  });
}

// ---- технологии: «Производство пороха» и «Стимуляция промышленности 1…99»
void Seeder::techsV4(Id& powderTech) {
  double y = 0;
  bool any = false;
  tx.w().techs.each([&](const Tech& t) {
    if (t.faction != 0) return;
    y = any ? std::max(y, t.pos.y) : t.pos.y;
    any = true;
  });
  // «Производство пороха» — как «Производство магического пороха», рядом с ней.
  powderTech = tx.w().techs.firstId([&](const Tech& t) { return t.faction == 0 && (t.key == schema::tech::Powder || (t.key.empty() && key(t.name) == key("Производство пороха"))); });
  if (!powderTech) {
    const Tech* magic = nullptr;
    tx.w().techs.each([&](const Tech& t) {
      if (t.faction == 0 && t.key == schema::tech::MagicPowder) magic = &t;
    });
    Tech t;
    t.faction = 0;
    t.name = "Производство пороха";
    t.desc = "Открывает «Военную мастерскую»: селитра, уголь и сера — в порох.";
    t.turns = magic ? magic->turns : 2;
    t.cost = magic ? magic->cost : std::map<Id, double>{{kGold, 30}};
    t.prereqs = magic ? magic->prereqs : std::vector<Id>{};
    t.needKeys = magic ? magic->needKeys : std::vector<std::string>{};
    t.key = schema::tech::Powder;
    t.pos = magic ? Vec2{magic->pos.x + schema::kTreeColStep, magic->pos.y} : Vec2{0, any ? y + 160 : 0};
    powderTech = tx.add(std::move(t)).id;
    techs++;
  } else if (tx.w().tech(powderTech)->key.empty()) {
    tx.tech(powderTech).key = schema::tech::Powder;
  }

  // Ветка «Стимуляция промышленности»: технология N открывает множитель ×(N + 1); змейкой по 10 в ряд ниже прежних
  // общих технологий; цена — (10 золота и 50 механических деталей) × множитель, 5 ходов.
  tx.w().techs.each([&](const Tech& t) {
    if (t.faction != 0) return;
    y = std::max(y, t.pos.y);
  });
  const double y0 = y + 200;
  const Id parts = resId("Механические детали");
  Id prev = 0;
  for (int mult = 2; mult <= schema::kMaxProductionMult; mult++) {
    const std::string k = schema::industryKey(mult);
    const std::string name = "Стимуляция промышленности " + std::to_string(mult - 1);
    Id found = tx.w().techs.firstId([&](const Tech& t) { return t.faction == 0 && (t.key == k || (t.key.empty() && key(t.name) == key(name))); });
    if (found) {
      if (tx.w().tech(found)->key.empty()) tx.tech(found).key = k;
      prev = found;
      continue;
    }
    const int i = mult - 2, row = i / 10, col = row % 2 == 0 ? i % 10 : 9 - i % 10;
    Tech t;
    t.faction = 0;
    t.name = name;
    t.desc = "Открывает множитель производства ×" + std::to_string(mult) + " построек преобразования.";
    t.turns = schema::kIndustryTechTurns;
    t.key = k;
    t.pos = Vec2{double(col) * schema::kTreeColStep, y0 + double(row) * 160};
    t.cost[kGold] = schema::kIndustryTechGold * mult;
    if (parts) t.cost[parts] = schema::kIndustryTechParts * mult;
    if (prev) t.prereqs.push_back(prev);
    prev = tx.add(std::move(t)).id;
    techs++;
  }
}

// ---- постройки общего дерева
void Seeder::buildingsV4(Id powderTech) {
  auto levels = [&](Building& b, const std::vector<LevelV4>& defs, const char* modPrefix, const std::vector<const char*>& produce) {
    b.levels.clear();
    for (size_t i = 0; i < defs.size(); i++) {
      BuildingLevel l;
      l.cost = costOf(defs[i].cost);
      l.extractPct = defs[i].extract;
      if (defs[i].produce > 0)
        for (const char* n : produce)
          if (Id r = resId(n)) l.produce[r] = defs[i].produce;
      if (modPrefix)
        if (Id m = builtinModifierRecord(tx, schema::levelModKey(modPrefix, int(i) + 1))) l.modifiers.push_back(m);
      b.levels.push_back(std::move(l));
    }
  };
  auto recipes = [&](Building& b, const std::vector<RecipeV4>& defs) {
    b.convert = true;
    b.recipes.clear();
    for (const RecipeV4& d : defs) {
      Recipe r;
      r.name = d.name ? d.name : "";
      for (auto& [n, v] : d.in)
        if (Id id = resId(n)) r.in.push_back(ResAmount{id, v});
      for (auto& [n, v] : d.essIn)
        if (Id id = essId(n)) r.essIn.push_back(ResAmount{id, v});
      for (auto& [n, v] : d.out)
        if (Id id = resId(n)) r.out.push_back(ResAmount{id, v});
      r.turns = 1;
      b.recipes.push_back(std::move(r));
    }
  };
  auto essCost = [&](Building& b, const char* essence, double v) {
    if (Id e = essId(essence)) b.levels[0].essCost[e] = v;
  };

  // Жилые и сельскохозяйственные: ферма, жилой квартал, гильдия строителей.
  newRow(BuildingCat::Residential);
  adoptBuilding(schema::bld::Farm, "Ферма", BuildingCat::Residential, "grain", [&](Building& b) {
    b.desc = "Добыча ресурса провинции из провизии «Сельскохозяйственная» или «Животная» растёт с уровнем; овощи, зерно, фрукты и курицы каждый ход.";
    b.extract = true;
    b.extractOptional = true;
    b.extractGroups.clear();
    for (const char* g : {schema::grp::ProvAgri, schema::grp::ProvAnimal})
      if (Id id = groupOf(g)) b.extractGroups.push_back(id);
    levels(b,
           {{{{"Древесина", 300}, {"Камень", 50}, {"Золото", 10}}, 25, 10},
            {{{"Древесина", 150}, {"Камень", 25}, {"Золото", 5}}, 50, 15},
            {{{"Древесина", 250}, {"Камень", 35}, {"Золото", 7.5}}, 75, 20},
            {{{"Древесина", 300}, {"Камень", 50}, {"Золото", 10}, {"Механические детали", 25}, {"Бронза", 25}}, 100, 25},
            {{{"Древесина", 500}, {"Камень", 100}, {"Золото", 20}, {"Механические детали", 50}, {"Бронза", 50}}, 200, 40}},
           nullptr, {"Овощи", "Зерно", "Фрукты", "Курицы"});
  });
  adoptBuilding(schema::bld::Housing, "Жилой квартал", BuildingCat::Residential, "house", [&](Building& b) {
    b.desc = "Прирост населения провинции: +1 % за уровень.";
    levels(b,
           {{{{"Древесина", 250}, {"Камень", 250}, {"Золото", 25}}},
            {{{"Древесина", 100}, {"Камень", 100}, {"Золото", 10}}},
            {{{"Древесина", 175}, {"Камень", 175}, {"Золото", 17.5}}},
            {{{"Древесина", 250}, {"Камень", 250}, {"Золото", 25}}},
            {{{"Древесина", 350}, {"Камень", 350}, {"Золото", 35}}}},
           schema::mod::HousingLevel, {});
  });
  adoptBuilding(schema::bld::BuildersGuild, "Гильдия строителей", BuildingCat::Residential, "build", [&](Building& b) {
    b.desc = "Слоты построек провинции: +1 за уровень.";
    levels(b,
           {{{{"Древесина", 200}, {"Камень", 1000}, {"Золото", 100}}},
            {{{"Древесина", 100}, {"Камень", 500}, {"Золото", 50}}},
            {{{"Древесина", 225}, {"Камень", 1100}, {"Золото", 110}}},
            {{{"Древесина", 400}, {"Камень", 2000}, {"Золото", 200}}},
            {{{"Древесина", 800}, {"Камень", 4000}, {"Золото", 400}}}},
           schema::mod::BuildersLevel, {});
  });

  // Промышленные: бойня, обогатители, инженерная мастерская, углежогная яма.
  newRow(BuildingCat::Industrial);
  adoptBuilding(schema::bld::Slaughterhouse, "Бойня", BuildingCat::Industrial, "skull", [&](Building& b) {
    b.desc = "Переработка скота и чудовищ: кожа, кость, мех, прочная чешуя, хитин и токсины.";
    b.levels[0].cost = costOf({{"Древесина", 200}, {"Камень", 200}, {"Золото", 50}, {"Медь", 100}});
    recipes(b, {
                   {"Переработка коров", {{"Коровы", 1}}, {}, {{"Кожа", 5}}},
                   {"Переработка верблюдов", {{"Верблюды", 1}}, {}, {{"Кожа", 5}}},
                   {"Переработка буйволов", {{"Буйволы", 1}}, {}, {{"Кожа", 5}}},
                   {"Переработка лошадей", {{"Лошади", 1}}, {}, {{"Кожа", 5}}},
                   {"Переработка носорогов", {{"Носороги", 1}}, {}, {{"Кожа", 20}, {"Кость", 10}}},
                   {"Переработка слонов", {{"Слоны", 1}}, {}, {{"Кожа", 40}, {"Кость", 20}}},
                   {"Переработка мамонтов", {{"Мамонты", 1}}, {}, {{"Кожа", 40}, {"Кость", 20}, {"Мех", 20}}},
                   {"Переработка ящеров", {{"Гигантские ящеры", 1}}, {}, {{"Прочная чешуя", 20}}},
                   {"Переработка василисков", {{"Василиски", 1}}, {}, {{"Прочная чешуя", 30}, {"Токсины", 20}}},
                   {"Переработка скорпионов", {{"Гигантские Скорпионы", 1}}, {}, {{"Хитин", 30}, {"Токсины", 30}}},
                   {"Переработка пауков", {{"Гигантские Пауки", 1}}, {}, {{"Хитин", 10}, {"Токсины", 45}}},
                   {"Переработка трицератопсов", {{"Трицератопсы", 1}}, {}, {{"Кожа", 80}, {"Кость", 40}}},
               });
  });
  adoptBuilding(schema::bld::AbyssEnricher, "Обогатитель бездны", BuildingCat::Industrial, "gem", [&](Building& b) {
    b.desc = "Обсидиан и тёмный обсидиан с эссенцией бездны — в заражённый бездной обсидиан.";
    b.levels[0].cost = costOf({{"Камень", 400}, {"Золото", 20}});
    essCost(b, "Эссенция бездны", 200);
    recipes(b, {
                   {"Зараженный бездной обсидиан (1)", {{"Обсидиан", 1}}, {{"Эссенция бездны", 1}}, {{"Зараженный бездной обсидиан", 1}}},
                   {"Зараженный бездной обсидиан (2)", {{"Темный обсидиан", 1}}, {{"Эссенция бездны", 1}}, {{"Зараженный бездной обсидиан", 1}}},
               });
  });
  adoptBuilding(schema::bld::NecroEnricher, "Некротический обогатитель", BuildingCat::Industrial, "lich", [&](Building& b) {
    b.desc = "Обсидиан и тёмный обсидиан с эссенцией смерти — в проклятый обсидиан.";
    b.levels[0].cost = costOf({{"Камень", 400}, {"Золото", 20}});
    essCost(b, "Эссенция смерти", 200);
    recipes(b, {
                   {"Проклятый обсидиан (1)", {{"Обсидиан", 1}}, {{"Эссенция смерти", 1}}, {{"Проклятый обсидиан", 1}}},
                   {"Проклятый обсидиан (2)", {{"Темный обсидиан", 1}}, {{"Эссенция смерти", 1}}, {{"Проклятый обсидиан", 1}}},
               });
  });
  adoptBuilding(schema::bld::EngWorkshop, "Инженерная мастерская", BuildingCat::Industrial, "u-machines", [&](Building& b) {
    b.desc = "Механические детали из железа, бронзы и кожи; запчасти механизмов из деталей, стали и кожи.";
    b.levels[0].cost = costOf({{"Камень", 450}, {"Золото", 50}, {"Сталь", 250}});
    recipes(b, {
                   {"Механические детали", {{"Железо", 1}, {"Бронза", 1}, {"Кожа", 1}}, {}, {{"Механические детали", 1}}},
                   {"Запчасти механизмов", {{"Механические детали", 10}, {"Сталь", 5}, {"Кожа", 20}}, {}, {{"Запчасти механизмов", 1}}},
               });
  });
  adoptBuilding(schema::bld::CharcoalPit, "Углежогная яма", BuildingCat::Industrial, "flame", [&](Building& b) {
    b.desc = "Древесина — в уголь.";
    b.levels[0].cost = costOf({{"Золото", 15}});
    recipes(b, {{"Уголь", {{"Древесина", 1}}, {}, {{"Уголь", 1}}}});
  });

  // Военные: военная мастерская (технология «Производство пороха»).
  newRow(BuildingCat::Military);
  adoptBuilding(schema::bld::WarWorkshop, "Военная мастерская", BuildingCat::Military, "hammer", [&](Building& b) {
    b.desc = "Порох из селитры, угля и серы. Открывает технология «Производство пороха».";
    b.levels[0].cost = costOf({{"Камень", 450}, {"Золото", 50}, {"Сталь", 250}});
    recipes(b, {{"Порох", {{"Селитра", 3}, {"Уголь", 1}, {"Сера", 1}}, {}, {{"Порох", 5}}}});
    if (powderTech && std::find(b.techs.begin(), b.techs.end(), powderTech) == b.techs.end()) b.techs.push_back(powderTech);
  });

  // Религиозные: алтарь культа, храмы освящения и осквернения.
  newRow(BuildingCat::Religious);
  adoptBuilding(schema::bld::CultAltar, "Алтарь культа", BuildingCat::Religious, "b-cult", [&](Building& b) {
    b.desc = "Демоническая энергия каждый ход.";
    b.levels[0].cost = costOf({{"Оскверненное железо", 200}, {"Золото", 20}});
    if (Id e = cat().resourceId(schema::kResEnergy)) b.levels[0].produce[e] = 5000;
  });
  adoptBuilding(schema::bld::HolyTemple, "Храм освящения", BuildingCat::Religious, "sun", [&](Building& b) {
    b.desc = "Железо с эссенцией света — в освящённое железо.";
    b.levels[0].cost = costOf({{"Камень", 400}, {"Золото", 20}});
    essCost(b, "Эссенция света", 200);
    recipes(b, {{"Освященное железо", {{"Железо", 1}}, {{"Эссенция света", 1}}, {{"Освященное железо", 1}}}});
  });
  adoptBuilding(schema::bld::UnholyTemple, "Храм осквернения", BuildingCat::Religious, "flame", [&](Building& b) {
    b.desc = "Железо с эссенцией хаоса — в осквернённое железо.";
    b.levels[0].cost = costOf({{"Камень", 400}, {"Золото", 20}});
    essCost(b, "Эссенция хаоса", 200);
    recipes(b, {{"Оскверненное железо", {{"Железо", 1}}, {{"Эссенция хаоса", 1}}, {{"Оскверненное железо", 1}}}});
  });
}

// ---- штабы разведки и контрразведки у каждого государства
void Seeder::intelV4() {
  std::vector<Id> states;
  tx.w().factions.each([&](const Faction& f) {
    if (f.isState()) states.push_back(f.id);
  });
  for (Id s : states) {
    const int before = int(tx.w().buildings.size());
    addIntelBuildings(tx, s);
    buildings += int(tx.w().buildings.size()) - before;
  }
}

void Seeder::v4() {
  resourcesV4();
  Id powderTech = 0;
  techsV4(powderTech);
  buildingsV4(powderTech);
  intelV4();
}

}  // namespace rg::content::detail
