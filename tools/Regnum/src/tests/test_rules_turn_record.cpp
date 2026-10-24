// Regnum — итоги хода и события хода в мире (Meta::lastTurn, Meta::turnEvents) и сохранение новых полей модели:
// предложения, пароль администратора, модификаторы реликвий, классов и справочников, филактерия, ход смены класса,
// ходы без земель, «рассмотрен» у заказа, мир после перемирия; нормализация удаляет невыполнимые ссылки.
#include "core/io.h"
#include "tests/test_rules_util.h"

using namespace rg;
using namespace rg::rules;
using namespace rg::rulestest;

namespace {

std::string text(const io::Warnings& ws) {
  std::string s;
  for (const io::Warning& w : ws) s += w.text() + "\n";
  return s;
}

}  // namespace

TEST(rules_turn_record_written_to_world) {
  Fix f;
  TurnReport rep;
  f.tx([&](Tx& tx) { rep = endTurn(tx); });
  const TurnRecord& t = f.w().meta->lastTurn;
  CHECK_EQ(t.turnFrom, 1);
  CHECK_EQ(t.turnTo, 2);
  CHECK(!t.logIds.empty());
  CHECK(t.logIds == rep.logIds);
  CHECK_EQ(t.factions.size(), rep.factions.size());
  CHECK(f.w().meta->turnEvents == rep.events);
  // Пробный ход мир не меняет.
  const TurnReport pre = previewTurn(f.w());
  CHECK_EQ(pre.turnTo, 3);
  CHECK_EQ(f.w().meta->lastTurn.turnTo, 2);
}

TEST(rules_turn_record_model_roundtrip) {
  Fix f;
  {
    World w0 = f.w();
    io::Warnings pre;
    io::normalize(w0, pre);   // базовый мир тестов — в нормальном виде
    f.s.replace(w0, "test");
  }
  Id relic = 0, cls = 0, cu = 0, mod = 0, hero = 0, prop = 0, truceProp = 0;
  f.tx([&](Tx& tx) {
    mod = makeMod(tx, {{Fx::IncomePct, 3}});
    relic = addRelic(tx, "Амулет", Rarity::Rare);
    for (Relic& r : tx.catalogs().relics)
      if (r.id == relic) {
        r.modifiers = {mod};
        r.phylactery = true;
      }
    cls = addHeroClass(tx, "Воитель");
    for (HeroClass& c : tx.catalogs().classes)
      if (c.id == cls) c.modifiers = {mod};
    cu = addCatalogItem(tx, CatalogList::Cultures, "Горная");
    for (CatalogItem& c : tx.catalogs().cultures)
      if (c.id == cu) c.modifiers = {mod};
    tx.settings().adminKey = "abc123";
    hero = createCharacter(tx, f.A, "Герой");
    tx.character(hero).respecTurn = 1;
    tx.faction(f.B).landless = 2;
    Proposal p;
    p.kind = ProposalKind::Alliance;
    p.from = f.A;
    p.to = f.B;
    prop = propose(tx, p);
    tx.province(f.p[0]).owner = f.C;
    declareWar(tx, f.A, f.C);
    Proposal t;
    t.kind = ProposalKind::Truce;
    t.from = f.A;
    t.to = f.C;
    t.truce.fromB.provinces = {f.p[0]};
    t.truce.fromA.reparations = 2;
    t.truce.fromA.reparationsTurns = 3;
    t.truce.fromA.vassal = true;
    truceProp = propose(tx, t);
    Relation r = tx.w().relation(f.A, f.B);
    r.calm = 7;
    tx.setRelation(f.A, f.B, r);
    // Итоги хода и события — как их пишет endTurn.
    Meta& m = tx.meta();
    m.lastTurn.turnFrom = 1;
    m.lastTurn.turnTo = 2;
    TurnFactionLine line;
    line.faction = f.A;
    line.treasuryBefore = 1;
    line.treasuryAfter = 2.5;
    line.income = 3;
    line.expenses = 1.5;
    line.resources[kGold + 1] = 4;
    m.lastTurn.factions.push_back(line);
    m.lastTurn.rebellions = {f.p[0]};
    m.lastTurn.hits.push_back(HitOutcome{0, hero, 40, 12.5, true});
    TurnEvent e;
    e.kind = TurnEvent::Mutiny;
    e.province = f.p[0];
    e.origin = f.A;
    e.heroes = {hero};
    m.turnEvents.push_back(e);
  });
  const World& w = f.w();
  const io::LoadResult lr = io::fromJson(io::toJson(w));
  CHECK_MSG(lr.warnings.empty(), text(lr.warnings));
  CHECK_EQ(lr.fixedTables, u32(0));
  const World& r = lr.world;
  CHECK(r.relic(relic) && r.relic(relic)->phylactery && r.relic(relic)->modifiers == std::vector<Id>{mod});
  CHECK(r.heroClass(cls) && r.heroClass(cls)->modifiers == std::vector<Id>{mod});
  CHECK(Catalogs::find(r.catalogs->cultures, cu) && Catalogs::find(r.catalogs->cultures, cu)->modifiers == std::vector<Id>{mod});
  CHECK_EQ(r.settings->adminKey, std::string("abc123"));
  CHECK_EQ(r.character(hero)->respecTurn, 1);
  CHECK_EQ(r.faction(f.B)->landless, 2);
  CHECK_EQ(r.relation(f.A, f.B).calm, 7);
  CHECK(r.proposal(prop) && r.proposal(prop)->kind == ProposalKind::Alliance);
  CHECK(r.proposal(truceProp) && r.proposal(truceProp)->truce == w.proposal(truceProp)->truce);
  CHECK(r.meta->lastTurn == w.meta->lastTurn);
  CHECK(r.meta->turnEvents == w.meta->turnEvents);
}

TEST(rules_turn_record_normalize_drops_dangling) {
  Fix f;
  Id prop = 0;
  f.tx([&](Tx& tx) {
    Proposal p;
    p.kind = ProposalKind::Alliance;
    p.from = f.A;
    p.to = f.C;
    prop = propose(tx, p);
    TurnEvent e;
    e.kind = TurnEvent::Uprising;
    e.army = 999;
    tx.meta().turnEvents.push_back(e);
    TurnEvent war;
    war.kind = TurnEvent::RebelWar;
    war.origin = f.A;
    war.rebelState = f.B;
    tx.meta().turnEvents.push_back(war);
    tx.eraseFaction(f.C);   // без очистки ссылок
  });
  World w = f.w();
  io::Warnings warns;
  const u32 mask = io::normalize(w, warns);
  CHECK(mask & TB_PROPOSALS);
  CHECK(w.proposal(prop) == nullptr);
  CHECK_EQ(w.meta->turnEvents.size(), size_t(1));   // мятеж без войска снят, война мятежников осталась
  CHECK(w.meta->turnEvents.at(0).kind == TurnEvent::RebelWar);
}
