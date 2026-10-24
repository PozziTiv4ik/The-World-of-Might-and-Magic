// Сценарии целостности мира в приложении: пропавший файл данных — только для чтения (запись и автосохранение
// запрещены, Ctrl+S — копия, «Восстановить из копии»), исправления нормализации — в папку мира только после согласия,
// исправления восстановленного автосохранения — тем же окном, «Проверить мир» без перезагрузки.
#include <functional>

#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

World edited(const World& w, const std::function<void(Tx&)>& fn) {
  Tx tx(w);
  fn(tx);
  return std::move(tx).finish();
}

std::string text(const std::string& path) { return fs::readFile(path).value_or(std::string()); }

}  // namespace

TEST(app_project_storage_missing_file_read_only) {
  Harness h("project_missing");
  h.demo();
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  Id fid = test::stateWithLand(h->world());
  const std::string name = h->world().faction(fid)->name;
  CHECK(h->act("Переименовать", [&](Tx& tx) { tx.faction(fid).name = "Новое имя"; }));
  h.key(Key::S, ctrl());   // прежняя версия data/factions.json — в .regnum-backup
  CHECK(!h->dirty());
  const std::string factions = fs::join(dir, "data/factions.json");
  CHECK(fs::remove(factions));
  CHECK(h->loadProject(dir));
  h.settle();
  CHECK(h->hasDialog("world.fixes"));
  CHECK(!h->impl().lock.empty());
  CHECK(h->uiRect("fixes.restore") != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("project_read_only"));
  h->closeDialogs();
  h.settle();
  // Запись и автосохранение запрещены: Ctrl+S предлагает сохранить копию, папка мира не меняется.
  const std::string provinces = text(fs::join(dir, "data/provinces.json"));
  CHECK(h->act("Правка", [&](Tx& tx) { tx.meta().notes = "заметка"; }));
  h.key(Key::S, ctrl());
  h.settle();
  CHECK(h->hasDialog("world.saveas"));
  h->closeDialogs();
  h.settle();
  CHECK(!h->saveTo(dir));
  h->autosaveNow();
  h->waitBackground();
  h.frames(2);
  CHECK(!fs::isFile(factions));
  CHECK_EQ(text(fs::join(dir, "data/provinces.json")), provinces);
  CHECK(io::listAutosaves(h.dataDir).empty());
  // «Восстановить из копии»: последняя целая версия файла, мир перечитан и снова доступен для записи.
  CHECK(h->loadProject(dir));
  h.settle();
  CHECK(h.clickUi("fixes.restore"));
  h.settle();
  CHECK(fs::isFile(factions));
  CHECK(h->impl().lock.empty());
  CHECK(h->world().faction(fid) != nullptr);
  CHECK_EQ(h->world().faction(fid)->name, name);
  CHECK(h->act("Правка", [&](Tx& tx) { tx.meta().notes = "после восстановления"; }));
  CHECK(h->save());
  CHECK_EQ(io::load(dir).world.meta->notes, std::string("после восстановления"));
}

TEST(app_project_storage_fixes_need_consent) {
  Harness h("project_fixes");
  h.demo();
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  Id pid = test::firstOwnedLand(h->world());
  const std::string provPath = fs::join(dir, "data/provinces.json");
  // Ручная правка: владелец провинции — несуществующая фракция (нормализация владельца удалит).
  CHECK(fs::writeFileAtomic(provPath, io::fileText(edited(h->store.world(), [&](Tx& tx) { tx.province(pid).owner = 9999; }), "data/provinces.json")));
  io::Warnings warnings = io::load(dir).warnings;
  CHECK(!warnings.empty());
  CHECK(h->loadProject(dir));
  h.settle();
  CHECK(h->hasDialog("world.fixes"));
  CHECK(h->uiRect("fixes.save") != nullptr);
  CHECK(h->impl().fixedTables & TB_PROVINCES);
  CHECK(h.clickUi("fixes.later"));
  h.settle();
  CHECK(!h->hasDialog("world.fixes"));
  // Правка и срок автосохранения: исправления без согласия в папку мира не пишутся — только копия в данных пользователя.
  const std::string before = text(provPath);
  CHECK(h->act("Период", [](Tx& tx) { tx.settings().autosaveSec = 10; }));
  CHECK(h->act("Правка", [&](Tx& tx) { tx.meta().notes = "заметка"; }));
  hl::advance(11);
  h.frames(3);
  h->waitBackground();
  h.frames(2);
  CHECK_EQ(text(provPath), before);
  CHECK(h->dirty());
  CHECK(!io::listAutosaves(h.dataDir).empty());
  // Согласие — «Сохранить исправления» (обычное сохранение); дальше автосохранение снова пишет в папку мира.
  app::detail::showFixes(h.a(), warnings);
  h.settle();
  CHECK(h.clickUi("fixes.save"));
  h.settle();
  CHECK(!h->dirty());
  CHECK_EQ(h->impl().fixedTables, 0u);
  CHECK(io::load(dir).warnings.empty());
  CHECK(h->act("Правка 2", [&](Tx& tx) { tx.meta().notes = "автосохранение"; }));
  hl::advance(11);
  h.frames(3);
  h->waitBackground();
  h.frames(2);
  CHECK(!h->dirty());
  CHECK_EQ(io::load(dir).world.meta->notes, std::string("автосохранение"));
}

// Восстановленное автосохранение с исправлениями нормализации — то же окно; без согласия они в папку не пишутся.
TEST(app_project_storage_restore_autosave_fixes) {
  Harness h("project_restore_fixes");
  h.demo();
  std::string dir = fs::join(h.root, "Мир");
  CHECK(h->saveTo(dir));
  Id pid = test::firstOwnedLand(h->world());
  io::writeAutosave(edited(h->store.world(), [&](Tx& tx) { tx.province(pid).owner = 9999; }), dir, h.dataDir);
  CHECK(h->loadProject(dir));
  h.settle();
  CHECK(h->hasDialog("choice"));
  CHECK(h.clickUi("dialog.button.1"));   // «Восстановить»
  h.settle();
  CHECK(h->hasDialog("world.fixes"));
  CHECK(h->impl().fixedTables & TB_PROVINCES);
  CHECK_EQ(h->world().province(pid)->owner, Id(0));
  CHECK(h->dirty());
}

TEST(app_project_storage_check_world) {
  Harness h("project_check");
  h.demo();
  CHECK(app::runCommand(h.a(), "file.check"));
  h.settle();
  CHECK(h->hasDialog("world.check"));
  CHECK(h->uiRect("check.summary") != nullptr);
  CHECK(h->uiRect("check.fix") == nullptr);   // исправлений нет
  h->closeDialogs();
  h.settle();
  h.settle();
  Id pid = test::firstOwnedLand(h->world());
  CHECK(h->act("Порча", [&](Tx& tx) { tx.province(pid).owner = 9999; }));
  CHECK(app::runCommand(h.a(), "file.check"));
  h.settle();
  CHECK(h->hasDialog("world.check"));
  CHECK(h->uiRect("check.fix") != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("project_check_world"));
  // Игроку проверка и исправление недоступны (команда выключена, кнопка открытого окна — тоже).
  h->ui.role = app::Role::Player;
  h->ui.playerState = test::stateWithLand(h->world());
  h.settle();
  CHECK(!app::findCommand("file.check")->enabled(h.a()));
  CHECK(h.clickUi("check.fix"));
  h.settle();
  CHECK(h->hasDialog("world.check"));
  CHECK_EQ(h->world().province(pid)->owner, Id(9999));
  h->ui.role = app::Role::Admin;
  h.settle();
  CHECK(h.clickUi("check.fix"));
  h.settle();
  CHECK(!h->hasDialog("world.check"));
  CHECK_EQ(h->world().province(pid)->owner, Id(0));
  // Все исправления — одна транзакция: одна отмена возвращает мир.
  h->undo();
  CHECK_EQ(h->world().province(pid)->owner, Id(9999));
}
