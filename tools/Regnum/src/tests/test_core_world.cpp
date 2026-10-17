// Тесты модели мира: транзакции, откат, отмена, объединение, грязные таблицы, отношения, ID без повторов.
#include "core/world.h"
#include "tests/test.h"

using namespace rg;

TEST(core_world_tx_add_edit_undo) {
  Store s;
  Id pid = s.transact("add", [](Tx& tx) {
    Province p; p.name = "Арден";
    return tx.add(p).id;
  });
  CHECK(pid > 0);
  CHECK_EQ(s.world().province(pid)->name, std::string("Арден"));
  s.transact("rename", [&](Tx& tx) { tx.province(pid).name = "Беррин"; });
  CHECK_EQ(s.world().province(pid)->name, std::string("Беррин"));
  CHECK(s.undo());
  CHECK_EQ(s.world().province(pid)->name, std::string("Арден"));
  CHECK(s.undo());
  CHECK(s.world().province(pid) == nullptr);
  CHECK(s.redo());
  CHECK(s.world().province(pid) != nullptr);
}

TEST(core_world_tx_exception_rolls_back) {
  Store s;
  Id pid = s.transact("add", [](Tx& tx) { return tx.add(Province{}).id; });
  World before = s.world();
  CHECK_THROWS(s.transact("bad", [&](Tx& tx) { tx.province(pid).name = "X"; fail("стоп"); }));
  CHECK(s.world().province(pid)->name.empty());
  CHECK_EQ(World::diff(before, s.world()), 0u);
}

TEST(core_world_structural_sharing) {
  Store s;
  std::vector<Id> ids;
  s.transact("many", [&](Tx& tx) { for (int i = 0; i < 500; i++) ids.push_back(tx.add(Province{}).id); });
  World a = s.world();
  s.transact("one", [&](Tx& tx) { tx.province(ids[3]).name = "Z"; });
  World b = s.world();
  CHECK(a.province(ids[3]) != b.province(ids[3]));
  CHECK(a.province(ids[400]) == b.province(ids[400]));  // общий объект
  int changed = 0;
  b.provinces.diff(a.provinces, [&](Id) { changed++; });
  CHECK_EQ(changed, 1);
  CHECK_EQ(World::diff(a, b), u32(TB_PROVINCES));
}

TEST(core_world_coalesce) {
  Store s;
  Id pid = s.transact("add", [](Tx& tx) { return tx.add(Province{}).id; });
  TxOptions o; o.coalesce = "drag";
  for (int i = 0; i < 10; i++) s.transact("drag", [&](Tx& tx) { tx.province(pid).contentment = i; }, o);
  CHECK_EQ(s.world().province(pid)->contentment, 9.0);
  CHECK(s.undo());
  CHECK_EQ(s.world().province(pid)->contentment, 0.0);
  CHECK(s.undo());
  CHECK(!s.canUndo());
}

TEST(core_world_dirty_and_relations) {
  Store s;
  s.markSaved();
  CHECK(!s.dirty());
  Id a = s.transact("f", [](Tx& tx) { return tx.add(Faction{}).id; });
  Id b = s.transact("f", [](Tx& tx) { return tx.add(Faction{}).id; });
  s.transact("rel", [&](Tx& tx) { tx.setRelation(b, a, Relation{-150, RelStatus::War}); });
  CHECK_EQ(s.world().relation(a, b).v, -100.0);
  CHECK(s.world().relation(a, b).s == RelStatus::War);
  CHECK(s.world().relation(b, a).s == RelStatus::War);
  CHECK(s.dirtyTables() & TB_RELATIONS);
  s.markSaved();
  CHECK(!s.dirty());
}

TEST(core_world_ids_not_reused) {
  Store s;
  Id a = s.transact("a", [](Tx& tx) { return tx.add(Army{}).id; });
  s.transact("del", [&](Tx& tx) { tx.eraseArmy(a); });
  Id b = s.transact("b", [](Tx& tx) { return tx.add(Army{}).id; });
  CHECK(b != a);
  CHECK_EQ(s.world().armies.size(), 1u);
}

TEST(core_world_tx_replace_world_undo) {
  Store s;
  Id a = s.transact("a", [](Tx& tx) { Province p; p.name = "Старая"; return tx.add(p).id; });
  World snap = s.world();
  s.transact("b", [&](Tx& tx) { tx.province(a).name = "Новая"; tx.add(Faction{}); });
  s.transact("откат", [&](Tx& tx) { tx.replaceWorld(snap); });
  CHECK_EQ(s.world().province(a)->name, std::string("Старая"));
  CHECK_EQ(s.world().factions.size(), 0u);
  CHECK(s.undo());
  CHECK_EQ(s.world().province(a)->name, std::string("Новая"));
  CHECK_EQ(s.world().factions.size(), 1u);
  // после замены можно продолжать правки в той же транзакции
  s.transact("откат+", [&](Tx& tx) { tx.replaceWorld(snap); tx.province(a).name = "Третья"; });
  CHECK_EQ(s.world().province(a)->name, std::string("Третья"));
  CHECK_EQ(s.world().factions.size(), 0u);
}
