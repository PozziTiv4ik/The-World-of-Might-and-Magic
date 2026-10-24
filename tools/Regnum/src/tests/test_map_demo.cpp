// Тесты демонстрационного мира (вымышленный пример, не канон): целостность геометрии и данных, состав,
// варианты построения (Tx, Store), расчёты правил, правдоподобная экономика государств, правка областей правилами
// (rules::removeArea/createProvince) на настоящей береговой линии — провинция, у которой не осталось области,
// удаляется вместе с записью; новая провинция поверх существующей её не трогает.
#include "core/io.h"
#include "geo/topo.h"
#include "gfx/flag.h"
#include "gfx/text.h"
#include "rules/rules.h"
#include "tests/test_map_view_util.h"

using namespace rg;

namespace {

// Небольшая сухопутная провинция с владельцем, рамка которой (с запасом) целиком на суше.
Id smallInland(const World& w, Box2& box) {
  auto fs = geo::faces(w);
  Id victim = 0;
  double best = 1e18;
  w.provinces.each([&](const Province& p) {
    if (p.sea || !p.owner) return;
    const geo::ProvinceShape* s = fs->shape(p.id);
    if (!s) return;
    Box2 b = s->box.inflated(25);
    Vec2 cs[] = {{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}};
    for (Vec2 c : cs)
      if (fs->terrainAt(c) != Terrain::Land) return;
    if (s->area < best) {
      best = s->area;
      victim = p.id;
      box = b;
    }
  });
  return victim;
}

std::vector<Vec2> boxPoly(const Box2& b) { return {{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}}; }

}  // namespace

TEST(map_demo_world_valid) {
  const double t0 = nowSeconds();
  const World& w = mvtest::demo();
  std::printf("  demo world: %.0f ms (cached after first build)\n", (nowSeconds() - t0) * 1000);
  // Геометрия без ошибок.
  const auto issues = geo::validate(w);
  CHECK_MSG(issues.empty(), issues.empty() ? std::string() : issues[0].code + ": " + issues[0].msg);
  // Нормализация ничего не исправляет.
  World copy = w;
  io::Warnings warns;
  const u32 fixed = io::normalize(copy, warns);
  CHECK_MSG(warns.empty(), warns.empty() ? std::string() : warns[0].text());
  CHECK_EQ(fixed, u32(0));
  // Чтение и запись JSON сохраняют мир.
  const io::LoadResult back = io::fromJson(io::toJson(w));
  CHECK(back.warnings.empty());
  CHECK_EQ(io::toJson(back.world), io::toJson(w));
  CHECK_EQ(w.meta->basemap, mvtest::basemap().id());
  CHECK(w.meta->notes.find("Не канон") != std::string::npos);
}

TEST(map_demo_world_content) {
  const World& w = mvtest::demo();
  int states = 0, guilds = 0, stateGuilds = 0;
  w.factions.each([&](const Faction& f) {
    if (f.isState()) {
      states++;
      CHECK(f.ruler != 0);
      CHECK(f.capital != 0);
      CHECK(!f.rulerTitle.empty());
      CHECK(f.council.size() >= 3);
      CHECK(f.culture && f.religion && f.government);
    } else {
      guilds++;
      stateGuilds += f.stateGuild;
      CHECK(f.homeState != 0);
    }
  });
  CHECK_EQ(states, 8);
  CHECK_EQ(guilds, 4);
  CHECK_EQ(stateGuilds, 1);
  // Цвета государств различимы.
  std::vector<Color> cols;
  w.factions.each([&](const Faction& f) { if (f.isState()) cols.push_back(f.color); });
  for (size_t i = 0; i < cols.size(); i++)
    for (size_t j = i + 1; j < cols.size(); j++)
      CHECK(std::abs(cols[i].r - cols[j].r) + std::abs(cols[i].g - cols[j].g) + std::abs(cols[i].b - cols[j].b) > 60);

  auto fs = geo::faces(w);
  int land = 0, sea = 0, owned = 0, unowned = 0, occupied = 0, withHq = 0, withInf = 0, withBuild = 0;
  w.provinces.each([&](const Province& p) {
    CHECK_MSG(fs->shape(p.id) != nullptr, p.name);
    CHECK(!p.name.empty());
    if (p.sea) {
      sea++;
      return;
    }
    land++;
    if (p.owner) owned++; else unowned++;
    occupied += p.occupied;
    withHq += !p.hqs.empty();
    withInf += !p.influence.empty();
    withBuild += !p.buildings.empty();
    CHECK(!p.races.empty());
    CHECK(p.resource != 0);
    if (p.owner) CHECK(p.culture != 0 && p.religion != 0);
  });
  std::printf("  demo: %d land (%d owned, %d unowned), %d sea, %d occupied, %d with HQ, %d with influence\n", land, owned, unowned, sea,
              occupied, withHq, withInf);
  CHECK(land >= 55 && land <= 75);
  CHECK_EQ(sea, 6);
  CHECK(unowned >= 3);
  CHECK_EQ(occupied, 3);
  CHECK(withHq >= 6);
  CHECK(withInf >= 15);
  CHECK(withBuild >= 20);

  int armies = 0, fleets = 0, allied = 0;
  w.armies.each([&](const Army& a) {
    (a.isFleet() ? fleets : armies)++;
    allied += a.allied();
    CHECK(rules::validPosition(w, a.kind, a.pos, a.id));
  });
  CHECK(armies >= 7);
  CHECK(fleets >= 4);
  CHECK_EQ(allied, 2);
  CHECK(w.routes.size() >= 4);
  int prereqs = 0, studied = 0, research = 0;
  w.techs.each([&](const Tech& t) {
    prereqs += int(t.prereqs.size());
    studied += t.studied;
    research += t.research;
  });
  CHECK(prereqs >= 10);
  CHECK(studied >= 4);
  CHECK(research >= 2);
  CHECK(w.buildings.size() >= 5);
  CHECK(w.modifiers.size() >= 5);
  CHECK(w.deals.size() >= 3);
  int wars = 0, alliances = 0;
  for (const auto& [k, r] : *w.relations) {
    wars += r.s == RelStatus::War;
    alliances += r.s == RelStatus::Alliance;
  }
  CHECK_EQ(wars, 2);
  CHECK_EQ(alliances, 3);  // два союза государств и государственная гильдия со своим государством
  int heroes = 0;
  w.characters.each([&](const Character& c) { heroes += c.hero; });
  CHECK(heroes >= 16);
  // Каталоги для режимов карты.
  CHECK(w.catalogs->races.size() >= 5);
  CHECK(w.catalogs->cultures.size() >= 8);
  CHECK(w.catalogs->religions.size() >= 4);
  // Расчёт правил: доходы, торговля, восстания.
  auto calc = rules::calc(w);
  int traded = 0;
  for (const auto& [pid, pc] : calc->provinces) traded += pc.tradeValue > 0;
  CHECK(traded > 40);
  const rules::TurnReport rep = rules::previewTurn(w);
  CHECK_EQ(rep.turnTo, rep.turnFrom + 1);
}

TEST(map_demo_world_flags) {
  // Флаги и цвета фракций демонстрационного мира — для просмотра.
  const World& w = mvtest::demo();
  const float s = 2;
  gfx::Image img(int(900 * s), int(330 * s), gfx::premul(Color::hex(0x151a22)));
  gfx::Canvas c(img);
  c.scale(s, s);
  int i = 0;
  w.factions.each([&](const Faction& f) {
    const float x = 20 + float(i % 3) * 296, y = 18 + float(i / 3) * 76;
    gfx::drawFlag(c, f.flag, RectF(x, y, 72, 48), 4);
    c.fillRoundRect(RectF(x + 82, y + 4, 10, 10), 3, f.color);
    gfx::TextStyle ts;
    ts.size = 13;
    ts.weight = gfx::FontWeight::Semibold;
    gfx::drawText(c, f.name, ts, x + 98, y + 1, Color::hex(0xece8df));
    ts.weight = gfx::FontWeight::Regular;
    ts.size = 11.5f;
    const std::string sub = f.isState() ? f.rulerTitle + " " + w.characterName(f.ruler) : (f.stateGuild ? "государственная гильдия" : "гильдия");
    gfx::drawText(c, sub, ts, x + 82, y + 22, Color::hex(0xa7afbb));
    i++;
  });
  CHECK_EQ(i, 12);
  mvtest::savePng(img, "map_demo_flags.png");
}

TEST(map_demo_world_build_variants) {
  // В пустую транзакцию.
  World base = newWorld("Пустой");
  Tx tx(base);
  map::buildDemoWorld(tx, mvtest::basemap());
  const World w = std::move(tx).finish();
  CHECK_EQ(io::toJson(w), io::toJson(mvtest::demo()));
  // Повторно в непустой мир — отказ.
  Tx again(w);
  CHECK_THROWS(map::buildDemoWorld(again, mvtest::basemap()));
  // Хранилище.
  Store s;
  map::buildDemoWorld(s, mvtest::basemap());
  CHECK_EQ(s.world().provinces.size(), mvtest::demo().provinces.size());
  CHECK(!s.canUndo());
  // Без базовой карты.
  map::Basemap none;
  CHECK_THROWS(map::makeDemoWorld(none));
}

// ================================================================ экономика государств
// Правдоподобная экономика государств: большинство слегка в плюсе, одно-два в минусе; доходы и расходы одного порядка,
// числа читаемые (аудит ТЗ 1.e).
TEST(map_demo_economy_balanced) {
  const World& w = mvtest::demo();
  auto c = rules::calc(w);
  w.factions.each([&](const Faction& f) {
    if (const rules::FactionCalc* fc = c->faction(f.id))
      std::printf("  %-32s казна %8.1f доход %7.1f расход %7.1f (войска %6.1f, флот %5.1f, спец. %4.1f) чистый %+7.1f\n", f.name.c_str(),
                  f.treasury(), fc->incTotal, fc->expTotal, fc->expArmy, fc->expFleet, fc->expSpecialists, fc->net);
  });
  int states = 0, negative = 0, positive = 0;
  w.factions.each([&](const Faction& f) {
    const rules::FactionCalc* fc = c->faction(f.id);
    CHECK(fc != nullptr);
    if (!fc) return;
    if (!f.isState()) {
      CHECK(fc->net >= 0);  // гильдии живут доходом штабов
      return;
    }
    states++;
    CHECK(fc->incTotal > 100);                       // доход — сотни, не десятки
    CHECK(fc->expTotal > 100);
    CHECK(fc->expTotal < fc->incTotal * 1.5);         // расходы одного порядка с доходом
    CHECK(fc->expArmy > fc->expSpecialists);          // армия — главная статья расходов
    CHECK(std::fabs(fc->net) < fc->incTotal * 0.4);   // «слегка»: не больше 40 % дохода
    if (fc->net < 0) CHECK(f.treasury() > 10 * -fc->net);   // дефицита казны хватает надолго
    if (fc->net < 0) negative++;
    else positive++;
  });
  CHECK_EQ(states, 8);
  CHECK(negative >= 1 && negative <= 2);
  CHECK(positive >= 6);
}

// ================================================================ правка областей на настоящей береговой линии
// Провинция, у которой не осталось области (rules::removeArea), удаляется вместе с записью.
TEST(map_demo_cut_whole_province_removes_record) {
  Store s;
  s.replace(mvtest::demo(), "демо");
  Box2 box;
  const Id victim = smallInland(s.world(), box);
  CHECK(victim != 0);
  const Id owner = s.world().province(victim)->owner;
  const std::string name = s.world().province(victim)->name;
  rules::AreaEdit r;
  s.transact("Вырезать", [&](Tx& tx) { r = rules::removeArea(tx, victim, boxPoly(box), 4.0); });
  CHECK(!s.world().province(victim));
  CHECK(!geo::faces(s.world())->shape(victim));
  CHECK_EQ(r.province, Id(0));
  CHECK(r.removed == std::vector<std::string>{name});
  auto c = rules::calc(s.world());
  const auto& prov = c->faction(owner)->provinces;
  CHECK(std::find(prov.begin(), prov.end(), victim) == prov.end());
  CHECK(geo::validate(s.world()).empty());
}

// ТЗ «Доработки №1», п.1: новая провинция поверх существующей её не трогает. Контур внутри занятой суши — отказ;
// рамка, охватывающая провинцию целиком (соседи внутри рамки вырезаны), даёт новую провинцию вокруг неё.
TEST(map_demo_cover_whole_province_keeps_it) {
  Store s;
  s.replace(mvtest::demo(), "демо");
  Box2 box;
  const Id victim = smallInland(s.world(), box);
  CHECK(victim != 0);
  const double area0 = geo::faces(s.world())->shape(victim)->area;
  const Vec2 c = geo::faces(s.world())->shape(victim)->label;
  const World before = s.world();
  std::string err;
  try {
    s.transact("Новая провинция", [&](Tx& tx) { rules::createProvince(tx, boxPoly(Box2(c.x - 2, c.y - 2, c.x + 2, c.y + 2)), Terrain::None, 4.0); });
  } catch (const UserError& e) {
    err = e.what();
  }
  CHECK_MSG(err.find("нет свободной суши") != std::string::npos, err);
  CHECK_EQ(World::diff(before, s.world()), 0u);
  std::vector<Id> others;
  auto fs = geo::faces(s.world());
  for (const auto& [id, sh] : fs->provinces)
    if (id != victim && sh.box.intersects(box) && !s.world().province(id)->sea) others.push_back(id);
  std::sort(others.begin(), others.end());
  for (Id p : others) {
    try {
      s.transact("Вырезать", [&](Tx& tx) { rules::removeArea(tx, p, boxPoly(box), 4.0); });
    } catch (const UserError&) {
    }
  }
  rules::AreaEdit r;
  s.transact("Новая провинция", [&](Tx& tx) { r = rules::createProvince(tx, boxPoly(box), Terrain::None, 4.0); });
  CHECK(r.province != 0 && s.world().province(r.province));
  CHECK(s.world().province(victim));
  CHECK(r.removed.empty());
  auto fs2 = geo::faces(s.world());
  CHECK_NEAR(fs2->shape(victim)->area, area0, 1e-6 * area0);
  auto nb = fs2->neighbors();
  CHECK(std::find(nb.begin(), nb.end(), std::make_pair(std::min(victim, r.province), std::max(victim, r.province))) != nb.end());
  CHECK(geo::validate(s.world()).empty());
}

