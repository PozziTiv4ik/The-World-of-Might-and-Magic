// Сценарии совета государства: должности строками (персонаж, отметка правителя), правка состава, не больше 10
// должностей и «Влияние совета».
#include "app/widgets.h"
#include "tests/test_app_faction_util.h"
#include "tests/test_app_trees_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;
namespace tr = rg::apptest::trees;

// ---------------------------------------------------------------- совет (ТЗ 1.b.iv)
TEST(app_faction_council_rows) {
  Harness h("faction_council");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Республика Корвен");
  openTab(h, a, "faction.council");
  const size_t n0 = h->world().faction(a)->council.size();
  CHECK(n0 > 0);
  CHECK(clickIn(h, "council.add"));
  h.step();
  const Faction* f = h->world().faction(a);
  CHECK_EQ(f->council.size(), n0 + 1);
  // Новое место получает свободную должность справочника.
  const std::string pos = f->council.back().position;
  CHECK(!pos.empty());
  for (size_t i = 0; i + 1 < f->council.size(); i++) CHECK(f->council[i].position != pos);
  CHECK(f->council.back().character == 0);
  h.dropToasts();
  CHECK(h.shot("faction_council_added"));
  // Удалить новое место; уведомление предлагает отмену.
  CHECK(clickIn(h, "council.remove." + std::to_string(n0)));
  h.step();
  CHECK_EQ(h->world().faction(a)->council.size(), n0);
  CHECK(!h->toasts().empty());
  CHECK(!h->toasts().back().actionLabel.empty());
  h->undo();
  h.step();
  CHECK_EQ(h->world().faction(a)->council.size(), n0 + 1);
}

// ---------------------------------------------------------------- совет: своя должность и содержание
TEST(app_faction_council_edit) {
  Harness h("faction_council_edit");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Королевство Альмарин");
  openTab(h, a, "faction.council");
  const Faction* f = h->world().faction(a);
  CHECK(!f->council.empty());
  const Id seat0 = f->council[0].id;
  const Id who0 = f->council[0].character;
  CHECK(who0 != 0);
  // Своя должность: последний пункт списка — запрос названия.
  CHECK(clickIn(h, "council.pos.0"));
  h.key(Key::PageDown);
  h.key(Key::PageDown);
  h.key(Key::Enter);
  h.step();
  CHECK(h->hasDialog("prompt"));
  CHECK(answerPrompt(h, "Хранитель печати"));
  CHECK_EQ(h->world().faction(a)->council[0].position, std::string("Хранитель печати"));
  CHECK_EQ(h->world().faction(a)->council[0].id, seat0);
  // Содержание советника правится в строке совета и попадает в расход «специалисты».
  const double spec0 = rules::calc(h->world())->faction(a)->expSpecialists;
  const double up0 = h->world().character(who0)->upkeep;
  CHECK(typeNumber(h, "council.upkeep.0", "40"));
  CHECK_NEAR(h->world().character(who0)->upkeep, 40, 1e-9);
  CHECK_NEAR(rules::calc(h->world())->faction(a)->expSpecialists, spec0 + 40 - up0, 1e-6);
  // Отрицательное содержание не принимается.
  CHECK(typeNumber(h, "council.upkeep.0", "-5"));
  CHECK(h->world().character(who0)->upkeep >= 0);
  CHECK(h.shot("faction_council_edit"));
  // Вакантное место: поля содержания нет.
  CHECK(h->act("Вакансия", [&](Tx& tx) { tx.faction(a).council[0].character = 0; }));
  h.step();
  CHECK(h->uiRect("council.upkeep.0") == nullptr);
  h->undo();
  h.step();
  CHECK_EQ(h->world().faction(a)->council[0].character, who0);
}

// ---------------------------------------------------------------- совет
TEST(app_faction_council_limit_and_influence) {
  Harness h("econ2_council", 1600, 1000);
  h.demo();
  h.dropToasts();
  Id st = 0;
  h->world().factions.each([&](const Faction& f) {
    if (!st && f.isState()) st = f.id;
  });
  CHECK(st != 0);
  // Десять должностей, пять назначений — «Централизованная власть» и «Влияние совета» −2 % × 5: считаются только
  // занятые места (аудит 2026-10-23, 6.5).
  CHECK(h->act("Совет", [&](Tx& tx) {
    tx.faction(st).council.clear();
    for (int i = 0; i < schema::kMaxCouncilSeats; i++) {
      Id who = 0;
      if (i < 5) who = rules::createCharacter(tx, st, "Советник " + std::to_string(i + 1));
      rules::addCouncilSeat(tx, st, "Должность " + std::to_string(i + 1), who);
    }
  }));
  CHECK(!h->act("Одиннадцатая", [&](Tx& tx) { rules::addCouncilSeat(tx, st, "Лишняя"); }));
  h->toasts().clear();
  const rules::Effects fx = rules::factionEffects(h->world(), st);
  double influence = 0;
  for (const rules::AutoMod& am : rules::autoModifiers(h->world(), st))
    if (am.key == schema::mod::CouncilInfluence) influence = am.scale;
  CHECK_NEAR(influence, 5.0, 1e-9);
  // «Влияние совета» — источник эффектов государства (вклад — множитель × эффект модификатора; прочие источники —
  // засеянные модификаторы формы правления и культуры демонстрационного мира).
  bool fromInfluence = false;
  for (const rules::EffectSource& src : fx.sources) fromInfluence = fromInfluence || src.key == schema::mod::CouncilInfluence;
  CHECK(fromInfluence);
  CHECK(rules::builtinMod(h->world(), schema::mod::CouncilInfluence)->get(Fx::ResearchTimePct) < 0);
  factionTab(h, st, "faction.council");
  CHECK(h->uiRect("council.influence") != nullptr);
  CHECK(tr::revealIn(h, "council.add"));
  tr::clickQuick(h, "council.add");
  CHECK_EQ(h->world().faction(st)->council.size(), size_t(schema::kMaxCouncilSeats));
  shotClean(h, "econ2_council", 60);
  // Сводка модификаторов государства: автоматические — «Влияние совета» с множителем.
  factionTab(h, st, "faction.modifiers");
  CHECK(h->uiRect("mods.auto.section") != nullptr);
  shotClean(h, "econ2_faction_mods", 60);
}
