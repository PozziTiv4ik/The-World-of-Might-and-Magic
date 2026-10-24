// Тесты core/io — трёхстороннее слияние миров (io::merge3) для совместной работы сессий над одной папкой мира:
// записи, изменённые одной стороной, конфликты (наша версия), удаление против изменения, новые записи с одним ID
// (наша получает новый ID, ссылки переводятся), поля meta и settings, отношения по парам, списки справочников,
// геометрия файлом целиком, нормализация итога, миры, прочитанные с диска.
#include <functional>

#include "tests/test_core_io_util.h"

using namespace rg;
using namespace rg::iotest;

namespace {

World edited(const World& w, const std::function<void(Tx&)>& fn) {
  Tx tx(w);
  fn(tx);
  return std::move(tx).finish();
}

bool hasId(const std::vector<std::pair<std::string, std::string>>& ids, const std::string& from, const std::string& to) {
  for (auto& [a, b] : ids)
    if (a == from && b == to) return true;
  return false;
}

}  // namespace

// Изменения разных записей и полей обеих сторон входят в мир; отчёт — по записям с диска.
TEST(core_io_merge_disjoint_changes) {
  World base = richWorld();
  World ours = edited(base, [](Tx& tx) {
    tx.province(1).name = "Наш Арден";
    tx.meta().notes = "наша заметка";
  });
  World theirs = edited(base, [](Tx& tx) {
    tx.character(2).name = "Их Бертран";
    tx.settings().autosaveSec = 300;
    tx.eraseRoute(2);
  });
  io::MergeResult m = io::merge3(base, ours, theirs);
  CHECK_EQ(m.world.province(1)->name, std::string("Наш Арден"));
  CHECK_EQ(m.world.meta->notes, std::string("наша заметка"));
  CHECK_EQ(m.world.character(2)->name, std::string("Их Бертран"));
  CHECK_EQ(m.world.settings->autosaveSec, 300);
  CHECK(!m.world.routes.has(2));
  CHECK(m.world.routes.has(1));
  CHECK_MSG(m.conflicts.empty(), warningsText(m.conflicts));
  CHECK(m.renamed.empty());
  CHECK(m.ids.empty());
  CHECK_MSG(m.fixes.empty(), warningsText(m.fixes));
  CHECK_MSG(hasWarning(m.taken, "data/characters.json", "c2 «Их Бертран»", "изменено на диске"), warningsText(m.taken));
  CHECK_MSG(hasWarning(m.taken, "data/routes.json", "r2", "удалено на диске"), warningsText(m.taken));
  CHECK_MSG(hasWarning(m.taken, "world.json", "settings.autosaveSec", "изменено на диске"), warningsText(m.taken));
  CHECK_EQ(m.taken.size(), size_t(3));
  CHECK(m.theirTables & TB_CHARACTERS);
  CHECK(m.theirTables & TB_ROUTES);
  CHECK(!(m.theirTables & TB_PROVINCES));
}

// Запись, изменённая обеими сторонами (пусть и в разных полях), — наша версия и конфликт в отчёте.
TEST(core_io_merge_same_record_keeps_ours) {
  World base = richWorld();
  World ours = edited(base, [](Tx& tx) { tx.province(1).contentment = 10; });
  World theirs = edited(base, [](Tx& tx) {
    tx.province(1).name = "Их Арден";
    tx.province(3).name = "Их Зарград";
  });
  io::MergeResult m = io::merge3(base, ours, theirs);
  CHECK_NEAR(m.world.province(1)->contentment, 10, 1e-9);
  CHECK_EQ(m.world.province(1)->name, std::string("Арден"));
  CHECK_EQ(m.world.province(3)->name, std::string("Их Зарград"));
  CHECK_EQ(m.conflicts.size(), size_t(1));
  CHECK_MSG(hasWarning(m.conflicts, "data/provinces.json", "p1 «Арден»", "изменено здесь и на диске"), warningsText(m.conflicts));
}

// Удаление против изменения: изменено здесь, удалено на диске — запись остаётся; удалено здесь — удалена.
TEST(core_io_merge_deleted_and_changed) {
  World base = richWorld();
  World ours = edited(base, [](Tx& tx) {
    tx.deal(2).note = "наша правка";
    tx.eraseRoute(1);
  });
  World theirs = edited(base, [](Tx& tx) {
    tx.eraseDeal(1);
    tx.eraseDeal(2);
    tx.route(1).name = "Их путь";
  });
  io::MergeResult m = io::merge3(base, ours, theirs);
  CHECK(!m.world.deals.has(1));
  CHECK(m.world.deals.has(2));
  CHECK_EQ(m.world.deal(2)->note, std::string("наша правка"));
  CHECK(!m.world.routes.has(1));
  CHECK_MSG(hasWarning(m.taken, "data/deals.json", "d1", "удалено на диске"), warningsText(m.taken));
  CHECK_MSG(hasWarning(m.conflicts, "data/deals.json", "d2", "удалено на диске — оставлено"), warningsText(m.conflicts));
  CHECK_MSG(hasWarning(m.conflicts, "data/routes.json", "r1 «Их путь»", "удалено здесь, изменено на диске"), warningsText(m.conflicts));
}

// Обе сессии добавили записи с одним ID: запись с диска свой ID сохраняет, наша получает новый, ссылки нашего мира
// на неё переводятся; счётчики ID вмещают новые записи.
TEST(core_io_merge_new_records_same_id) {
  World base = richWorld();
  Id ourHero = 0;
  World ours = edited(base, [&](Tx& tx) {
    Character c;
    c.name = "Наш герой";
    c.faction = 1;
    c.hero = true;
    ourHero = tx.add(std::move(c)).id;
    tx.province(1).lord = ourHero;
    LogEntry l;
    l.text = "Наша запись";
    l.turn = 7;
    tx.add(std::move(l));
  });
  World theirs = edited(base, [](Tx& tx) {
    Character c;
    c.name = "Их герой";
    c.faction = 3;
    tx.add(std::move(c));
    LogEntry l;
    l.text = "Их запись";
    l.turn = 7;
    tx.add(std::move(l));
  });
  CHECK_EQ(ourHero, Id(4));
  io::MergeResult m = io::merge3(base, ours, theirs);
  CHECK_EQ(m.world.character(4)->name, std::string("Их герой"));
  CHECK(m.world.character(5) != nullptr);
  CHECK_EQ(m.world.character(5)->name, std::string("Наш герой"));
  CHECK_EQ(m.world.province(1)->lord, Id(5));
  CHECK_EQ(m.world.logEntry(6)->text, std::string("Их запись"));
  CHECK_EQ(m.world.logEntry(7)->text, std::string("Наша запись"));
  CHECK(m.world.meta->seq[int(Seq::Character)] >= 5);
  CHECK(m.world.meta->seq[int(Seq::Log)] >= 7);
  CHECK(hasId(m.ids, "c4", "c5"));
  CHECK(hasId(m.ids, "l6", "l7"));
  CHECK_EQ(m.renamed.size(), size_t(2));
  CHECK_MSG(m.conflicts.empty(), warningsText(m.conflicts));
  CHECK_MSG(hasWarning(m.taken, "data/characters.json", "c4 «Их герой»", "добавлено на диске"), warningsText(m.taken));
  // Одинаковая новая запись с обеих сторон — одна, без перенумерации.
  World same1 = edited(base, [](Tx& tx) { tx.add(LogEntry{0, 7, LogKind::Note, "Общая"}); });
  World same2 = edited(base, [](Tx& tx) { tx.add(LogEntry{0, 7, LogKind::Note, "Общая"}); });
  io::MergeResult s = io::merge3(base, same1, same2);
  CHECK(s.ids.empty());
  CHECK(s.conflicts.empty());
  CHECK_EQ(s.world.log.size(), base.log.size() + 1);
}

// meta и settings — по полям: счётчик случайных событий — сумма приращений, время сохранения — наше; отношения —
// по парам фракций.
TEST(core_io_merge_meta_settings_relations) {
  World base = edited(richWorld(), [](Tx& tx) { tx.meta().rng = 5; });
  World ours = edited(base, [](Tx& tx) {
    tx.meta().rng = 7;
    tx.meta().updatedAt = "2026-10-24T10:00:00Z";
    tx.meta().turn = 8;
    tx.settings().fillOpacity = 0.75f;
    tx.setRelation(1, 2, Relation{70, RelStatus::Alliance});
  });
  World theirs = edited(base, [](Tx& tx) {
    tx.meta().rng = 9;
    tx.meta().updatedAt = "2026-10-24T09:00:00Z";
    tx.meta().turn = 9;
    tx.meta().notes = "их заметка";
    tx.settings().autosaveSec = 90;
    tx.setRelation(1, 3, Relation{-60, RelStatus::War});
  });
  io::MergeResult m = io::merge3(base, ours, theirs);
  CHECK_EQ(m.world.meta->rng, u32(11));
  CHECK_EQ(m.world.meta->updatedAt, std::string("2026-10-24T10:00:00Z"));
  CHECK_EQ(m.world.meta->turn, 8);
  CHECK_EQ(m.world.meta->notes, std::string("их заметка"));
  CHECK_NEAR(m.world.settings->fillOpacity, 0.75, 1e-6);
  CHECK_EQ(m.world.settings->autosaveSec, 90);
  CHECK_NEAR(m.world.relation(1, 2).v, 70, 1e-9);
  CHECK_NEAR(m.world.relation(1, 3).v, -60, 1e-9);
  CHECK_EQ(m.conflicts.size(), size_t(1));
  CHECK_MSG(hasWarning(m.conflicts, "world.json", "meta.turn", "оставлена версия этой сессии"), warningsText(m.conflicts));
}

// Списки справочников — по записям: обе сессии добавили расу с одним ID — наша получает новый, ссылки (ключи
// населения провинции) переводятся.
TEST(core_io_merge_catalog_lists) {
  World base = richWorld();
  auto addRace = [](Tx& tx, const char* name) {
    CatalogItem it;
    it.id = tx.nextId(Seq::Race);
    it.name = name;
    it.color = Color::hex(0x808080);
    it.icon = "person";
    tx.catalogs().races.push_back(it);
    return it.id;
  };
  Id orcs = 0;
  World ours = edited(base, [&](Tx& tx) {
    orcs = addRace(tx, "Орки");
    tx.province(3).races = {RacePop{orcs, 500}};
  });
  World theirs = edited(base, [&](Tx& tx) { addRace(tx, "Гномы"); });
  CHECK_EQ(orcs, Id(3));
  io::MergeResult m = io::merge3(base, ours, theirs);
  const auto& races = m.world.catalogs->races;
  CHECK_EQ(races.size(), size_t(4));
  const CatalogItem* g = findById(races, Id(3));
  const CatalogItem* o = findById(races, Id(4));
  CHECK(g && g->name == "Гномы");
  CHECK(o && o->name == "Орки");
  const auto& pop = m.world.province(3)->races;
  CHECK(pop.size() == 1 && pop[0].race == Id(4) && pop[0].pop == 500);
  CHECK(hasId(m.ids, "rc3", "rc4"));
  CHECK_MSG(m.conflicts.empty(), warningsText(m.conflicts));
}

// Границы и берег — файлом целиком: изменены обеими сторонами — наши (конфликт), только на диске — с диска.
TEST(core_io_merge_geometry_whole_file) {
  World base = richWorld();
  World ours = edited(base, [](Tx& tx) { tx.node(1).p = {110, 210}; });
  World theirs = edited(base, [](Tx& tx) { tx.node(2).p = {320, 220}; });
  io::MergeResult m = io::merge3(base, ours, theirs);
  CHECK_EQ(m.world.nodes.get(1)->p.x, 110.0);
  CHECK_EQ(m.world.nodes.get(2)->p.x, 300.0);
  CHECK_MSG(hasWarning(m.conflicts, "data/geo.json", "", "изменены здесь и на диске"), warningsText(m.conflicts));
  io::MergeResult t = io::merge3(base, base, theirs);
  CHECK_EQ(t.world.nodes.get(2)->p.x, 320.0);
  CHECK(t.conflicts.empty());
  CHECK_MSG(hasWarning(t.taken, "data/geo.json", "", "изменены на диске"), warningsText(t.taken));
}

// Итог нормализуется: наша запись ссылается на фракцию, удалённую на диске, — ссылка исправлена, исправление в отчёте.
TEST(core_io_merge_normalizes_result) {
  World base = richWorld();
  World ours = edited(base, [](Tx& tx) { tx.province(3).name = "Наш Зарград"; });
  World theirs = edited(base, [](Tx& tx) {
    tx.eraseFaction(3);
    tx.relations().erase(relKey(1, 3));
    tx.relations().erase(relKey(2, 3));
  });
  io::MergeResult m = io::merge3(base, ours, theirs);
  CHECK(!m.world.faction(3));
  CHECK_EQ(m.world.province(3)->name, std::string("Наш Зарград"));
  CHECK_EQ(m.world.province(3)->occupier, Id(0));
  CHECK(!m.fixes.empty());
  CHECK(hasWarning(m.taken, "data/factions.json", "f3 «Империя Зар»", "удалено на диске"));
  // Повторная нормализация слитого мира ничего не меняет.
  World again = m.world;
  io::Warnings warns;
  io::normalize(again, warns);
  CHECK_MSG(warns.empty(), warningsText(warns));
}

// Одинаковые изменения обеих сторон — ни конфликта, ни изменений с диска.
TEST(core_io_merge_identical_changes) {
  World base = richWorld();
  auto fn = [](Tx& tx) {
    tx.province(1).name = "Общее имя";
    tx.settings().labelArmies = true;
  };
  io::MergeResult m = io::merge3(base, edited(base, fn), edited(base, fn));
  CHECK(m.conflicts.empty());
  CHECK(m.taken.empty());
  CHECK_EQ(m.world.province(1)->name, std::string("Общее имя"));
}

// Через диск: база и мир «с диска» прочитаны из папки, наш мир — с нескруглёнными координатами; ложных конфликтов
// и изменений нет, приходит только правка другой сессии.
TEST(core_io_merge_worlds_from_files) {
  std::string dir = tempDir("merge_files");
  io::save(dir, richWorld(), TB_ALL);
  World base = io::load(dir).world;
  World ours = edited(base, [](Tx& tx) {
    tx.army(1).pos = {210.123456, 300.987654};
    tx.node(3).p = {300.004, 400.756};
  });
  io::save(dir, edited(base, [](Tx& tx) { tx.character(1).name = "Эдмунд Второй"; }), TB_CHARACTERS);
  World theirs = io::load(dir).world;
  io::MergeResult m = io::merge3(base, ours, theirs);
  CHECK_MSG(m.conflicts.empty(), warningsText(m.conflicts));
  CHECK_EQ(m.taken.size(), size_t(1));
  CHECK_MSG(hasWarning(m.taken, "data/characters.json", "c1 «Эдмунд Второй»", "изменено на диске"), warningsText(m.taken));
  CHECK_EQ(m.world.character(1)->name, std::string("Эдмунд Второй"));
  CHECK_NEAR(m.world.army(1)->pos.x, 210.12, 1e-9);
  CHECK_MSG(m.fixes.empty(), warningsText(m.fixes));
}
