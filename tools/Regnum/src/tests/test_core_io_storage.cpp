// Тесты core/io — целостность хранения: метка поколения в файлах папки мира, смешанное сохранение, сохранение
// одним целым (все временные файлы, затем замены; неудача — прежние версии), пропавший файл данных и мир новее
// программы — только для чтения, восстановление из последних целых резервных копий.
#include <chrono>
#include <fstream>
#include <functional>
#include <map>

#include "base/json.h"
#include "core/content.h"
#include "core/io_internal.h"
#include "tests/test_core_io_util.h"

using namespace rg;
using namespace rg::iotest;
using test::errorOf;

namespace {

World edited(const World& w, const std::function<void(Tx&)>& fn) {
  Tx tx(w);
  fn(tx);
  return std::move(tx).finish();
}

std::string at(const std::string& dir, const std::string& rel) { return fs::join(dir, rel); }

// Тексты всех файлов мира в папке.
std::map<std::string, std::string> texts(const std::string& dir) {
  std::map<std::string, std::string> m;
  for (auto& f : io::projectFiles()) m[f.path] = read(at(dir, f.path));
  return m;
}

// Копии файла rel в очереди ручных сохранений.
size_t copies(const std::string& dir, const std::string& stem) {
  size_t n = 0;
  for (auto& e : fs::list(at(dir, ".regnum-backup/data")))
    if (!e.dir && e.name.rfind(stem + ".", 0) == 0) n++;
  return n;
}

}  // namespace

// Файлы, записанные одним сохранением, несут одно поколение; текст без метки — канонический текст таблицы.
// Один файл заменяется атомарно сам (поколение world.json); несколько — новое поколение, world.json — последним.
TEST(core_io_storage_generation_in_files) {
  World w = richWorld();
  std::string dir = tempDir("st_gen");
  io::FileState st;
  io::save(dir, w, TB_ALL, &st);
  for (auto& f : io::projectFiles()) {
    const std::string text = read(at(dir, f.path));
    CHECK_MSG(io::generationIn(text) == 1, f.path);
    CHECK_EQ(io::withoutGeneration(text), io::fileText(w, f.path));
    CHECK_EQ(st.files[f.path].gen, u64(1));
  }
  io::LoadResult r = io::load(dir);
  CHECK_MSG(r.warnings.empty(), warningsText(r.warnings));
  CHECK_EQ(r.generation, u64(1));
  CHECK(r.mixed.empty());
  CHECK(r.readOnly.empty());

  const std::string world1 = read(at(dir, "world.json"));
  World w2 = edited(w, [](Tx& tx) { tx.province(1).name = "Новый Арден"; });
  auto res = io::save(dir, w2, TB_PROVINCES, &r.files);
  CHECK_EQ(res.written.size(), size_t(1));
  CHECK_EQ(io::generationIn(read(at(dir, "data/provinces.json"))), u64(1));
  CHECK_EQ(read(at(dir, "world.json")), world1);

  World w3 = edited(w2, [](Tx& tx) {
    tx.province(1).contentment = 5;
    tx.character(1).name = "Эдмунд II";
  });
  res = io::save(dir, w3, TB_PROVINCES | TB_CHARACTERS, &r.files);
  CHECK_EQ(res.written.size(), size_t(3));
  CHECK_EQ(res.written.back(), std::string("world.json"));
  CHECK(res.unchanged.empty());
  for (const char* f : {"data/provinces.json", "data/characters.json", "world.json"}) CHECK_MSG(io::generationIn(read(at(dir, f))) == 2, f);
  CHECK_EQ(io::generationIn(read(at(dir, "data/factions.json"))), u64(1));
  io::LoadResult r3 = io::load(dir);
  CHECK_MSG(r3.warnings.empty(), warningsText(r3.warnings));
  CHECK_EQ(r3.generation, u64(2));
  CHECK(diffWorld(w3, r3.world).empty());
  // Метка — не содержимое: повторное сохранение того же мира ничего не пишет.
  res = io::save(dir, r3.world, TB_ALL, &r3.files);
  CHECK(res.written.empty());
  CHECK_EQ(res.unchanged.size(), io::projectFiles().size());
}

// Метка встаёт туда же, куда её поставила бы запись JSON с отсортированными ключами (и в файлах «по строке на запись»).
TEST(core_io_storage_generation_placement) {
  World w = richWorld();
  for (int i = 0; i < io::detail::F_COUNT; i++) {
    const io::detail::FileId f = io::detail::FileId(i);
    json::Value v = io::detail::encode(w, f);
    v.set(io::kGenerationKey, 42);
    const std::string expect = io::detail::format(v, f);
    const std::string plain = io::fileText(w, io::detail::kFiles[i].path);
    CHECK_MSG(io::withGeneration(plain, 42) == expect, io::detail::kFiles[i].path);
    CHECK_EQ(io::withoutGeneration(expect), plain);
    CHECK_EQ(io::generationIn(expect), u64(42));
    CHECK_EQ(io::generationIn(plain), u64(0));
    CHECK_EQ(io::withGeneration(expect, 0), plain);
    CHECK_EQ(io::withGeneration(expect, 43), io::withGeneration(plain, 43));
    CHECK(json::tryParse(expect).has_value());
  }
  // Текст не в принятом виде не портится.
  CHECK_EQ(io::withoutGeneration("{\"generation\": 3}"), std::string("{\"generation\": 3}"));
  CHECK_EQ(io::generationIn("{\n  \"generation\": x\n}"), u64(0));
}

// Сохранение прервалось после замены части файлов: файл новее world.json — «смешанное сохранение» (предупреждение,
// запись не запрещается). Восстановление — последняя целая копия не новее world.json; смешанная версия не теряется.
TEST(core_io_storage_mixed_save_and_restore) {
  World w = richWorld();
  std::string dir = tempDir("st_mixed");
  const std::string prov = at(dir, "data/provinces.json");
  io::save(dir, w, TB_ALL);
  auto edit = [&](const std::string& name) {
    w = edited(w, [&](Tx& tx) {
      tx.province(1).name = name;
      tx.character(1).name = "Эдмунд " + name;
    });
  };
  edit("второй");
  io::save(dir, w, TB_PROVINCES | TB_CHARACTERS);   // поколение 2; в копиях — версии поколения 1
  const std::string prov2 = read(prov);
  CHECK_EQ(io::generationIn(prov2), u64(2));
  // Сохранение поколения 3: копия прежней версии записана, затем заменён только data/provinces.json.
  write(at(dir, ".regnum-backup/data/provinces.000002.json"), prov2);
  edit("третий");
  write(prov, io::withGeneration(io::fileText(w, "data/provinces.json"), 3));
  io::LoadResult r = io::load(dir);
  CHECK_EQ(r.mixed.size(), size_t(1));
  CHECK(!r.mixed.empty() && r.mixed[0] == "data/provinces.json");
  CHECK_MSG(hasWarning(r.warnings, "data/provinces.json", "", "смешанное сохранение"), warningsText(r.warnings));
  CHECK(r.readOnly.empty());
  CHECK_EQ(r.generation, u64(2));

  auto copy = io::latestBackup(dir, "data/provinces.json", r.generation);
  CHECK(copy.has_value());
  CHECK(copy && copy->copy == ".regnum-backup/data/provinces.000002.json" && copy->generation == 2);
  std::vector<std::string> none;
  auto done = io::restoreBackups(dir, r.mixed, r.generation, &none);
  CHECK_EQ(done.size(), size_t(1));
  CHECK(none.empty());
  CHECK_EQ(read(prov), prov2);
  io::LoadResult back = io::load(dir);
  CHECK_MSG(back.warnings.empty(), warningsText(back.warnings));
  CHECK_EQ(back.world.province(1)->name, std::string("второй"));
  // Смешанная версия ушла в копии: без ограничения поколения она — последняя, с ограничением — не выбирается.
  auto newest = io::latestBackup(dir, "data/provinces.json");
  CHECK(newest && newest->generation == 3);
  auto limited = io::latestBackup(dir, "data/provinces.json", 2);
  CHECK(limited && limited->generation <= 2);
}

// Последняя целая копия: обе очереди (ручная и автосохранения) по времени записи; повреждённая копия пропускается.
TEST(core_io_storage_latest_backup) {
  World w = richWorld();
  std::string dir = tempDir("st_latest");
  io::save(dir, w, TB_ALL);
  for (int i = 1; i <= 3; i++) {
    w = edited(w, [&](Tx& tx) { tx.province(1).contentment = i; });
    io::save(dir, w, TB_PROVINCES);
  }
  auto c = io::latestBackup(dir, "data/provinces.json");
  CHECK(c && c->copy == ".regnum-backup/data/provinces.000003.json");
  // Версия автосохранения записана позже — она последняя.
  io::SaveOptions so;
  so.autoBackups = {"data/provinces.json"};
  w = edited(w, [](Tx& tx) { tx.province(1).contentment = 40; });
  io::save(dir, w, TB_PROVINCES, nullptr, so);
  std::error_code ec;
  const auto old = std::filesystem::last_write_time(fs::path(at(dir, ".regnum-backup/data/provinces.000003.json")), ec);
  CHECK(!ec);
  std::filesystem::last_write_time(fs::path(at(dir, ".regnum-backup/auto/data/provinces.000001.json")), old + std::chrono::seconds(5), ec);
  CHECK(!ec);
  c = io::latestBackup(dir, "data/provinces.json");
  CHECK(c && c->copy == ".regnum-backup/auto/data/provinces.000001.json");
  // Повреждённые копии пропускаются.
  write(at(dir, ".regnum-backup/auto/data/provinces.000001.json"), "{обрыв");
  write(at(dir, ".regnum-backup/data/provinces.000003.json"), "[1, 2]");
  c = io::latestBackup(dir, "data/provinces.json");
  CHECK(c && c->copy == ".regnum-backup/data/provinces.000002.json");
  CHECK(!io::latestBackup(dir, "data/routes.json").has_value());
}

// Пропал обязательный файл данных: таблица пуста, мир — только для чтения; восстановление из копии возвращает файл.
TEST(core_io_storage_missing_file_read_only) {
  World w = richWorld();
  std::string dir = tempDir("st_missing");
  io::save(dir, w, TB_ALL);
  const std::string name = w.faction(1)->name;
  io::save(dir, edited(w, [](Tx& tx) { tx.faction(1).name = "Новое Арденское королевство"; }), TB_FACTIONS);
  CHECK(fs::remove(at(dir, "data/factions.json")));
  io::LoadResult r = io::load(dir);
  CHECK(r.world.factions.empty());
  CHECK_EQ(r.missing.size(), size_t(1));
  CHECK_MSG(r.readOnly.find("data/factions.json") != std::string::npos, r.readOnly);
  CHECK(hasWarning(r.warnings, "data/factions.json", "", "не найден"));
  std::vector<std::string> none;
  auto done = io::restoreBackups(dir, r.missing, r.generation, &none);
  CHECK_EQ(done.size(), size_t(1));
  io::LoadResult back = io::load(dir);
  CHECK(back.missing.empty());
  CHECK(back.readOnly.empty());
  CHECK_EQ(back.world.faction(1)->name, name);   // последняя целая копия — версия до последнего сохранения
  // Нет копии — файл в noCopy, ничего не записано.
  CHECK(fs::remove(at(dir, "data/routes.json")));
  none.clear();
  done = io::restoreBackups(dir, {"data/routes.json"}, 0, &none);
  CHECK(done.empty());
  CHECK(none.size() == 1 && none[0] == "data/routes.json");
  // Необязательный файл (заговоры) не делает мир только для чтения.
  CHECK(fs::remove(at(dir, "data/plots.json")));
  io::LoadResult opt = io::load(dir);
  CHECK(opt.missing.size() == 1 && opt.missing[0] == "data/routes.json");
}

// Мир новее программы (младшая версия формата, базовые записи) — только для чтения; преобразование отказывает.
TEST(core_io_storage_newer_world_read_only) {
  World w = richWorld();
  std::string dir = tempDir("st_newer");
  io::save(dir, w, TB_ALL);
  const std::string worldPath = at(dir, "world.json");
  const std::string wj = read(worldPath);
  write(worldPath, replaceAll(wj, "\"version\": 1", "\"version\": 1.4"));
  io::LoadResult r = io::load(dir);
  CHECK_MSG(r.readOnly.find("Формат мира новее") != std::string::npos, r.readOnly);
  CHECK(r.warnings.empty());
  std::string e = errorOf([&] { io::convert(dir, at(dir, "../st_newer.regnum"), nullptr, true); });
  CHECK_MSG(e.find("только для чтения") != std::string::npos, e);
  const std::string cur = "\"content\": " + std::to_string(content::kVersion);
  CHECK(wj.find(cur) != std::string::npos);
  write(worldPath, replaceAll(wj, cur, "\"content\": " + std::to_string(content::kVersion + 1)));
  r = io::load(dir);
  CHECK_MSG(r.readOnly.find("Базовые записи мира новее") != std::string::npos, r.readOnly);
  write(worldPath, wj);
  CHECK(io::load(dir).readOnly.empty());
  // Снимок мира (автосохранение) новее программы — так же.
  World newer = edited(w, [](Tx& tx) { tx.meta().content = content::kVersion + 1; });
  CHECK(!io::unpack(io::pack(newer)).readOnly.empty());
}

// Замена файла не удалась (здесь — на месте файла папка): уже заменённые файлы возвращаются к прежним версиям,
// временных файлов не остаётся, копии прежних версий при повторной попытке не дублируются.
TEST(core_io_storage_failed_save_keeps_disk) {
  World w = richWorld();
  std::string dir = tempDir("st_fail");
  io::FileState st;
  io::save(dir, w, TB_ALL, &st);
  const auto before = texts(dir);
  CHECK(fs::remove(at(dir, "data/proposals.json")));
  CHECK(fs::makeDirs(at(dir, "data/proposals.json")));
  World w2 = edited(w, [](Tx& tx) {
    tx.province(1).name = "Не записано";
    tx.character(1).name = "Не записан";
    tx.faction(1).tax = 33;
  });
  const u32 dirty = TB_PROVINCES | TB_CHARACTERS | TB_FACTIONS;
  std::string e = errorOf([&] { io::save(dir, w2, dirty, &st); });
  CHECK_MSG(e.find("Мир не сохранён") != std::string::npos && e.find("не изменены") != std::string::npos &&
                e.find("proposals.json") != std::string::npos, e);
  for (auto& [rel, text] : before)
    if (rel != "data/proposals.json") CHECK_MSG(read(at(dir, rel)) == text, rel);
  for (const std::string& f : fs::listTree(dir)) CHECK_MSG(f.find(".tmp-") == std::string::npos, f);
  for (auto& c : io::externalChanges(st)) CHECK_MSG(c.file == "data/proposals.json", c.file);
  e = errorOf([&] { io::save(dir, w2, dirty, &st); });
  CHECK(!e.empty());
  CHECK_EQ(copies(dir, "provinces"), size_t(1));
  CHECK_EQ(copies(dir, "characters"), size_t(1));
  // Помеха убрана — сохранение проходит целиком.
  CHECK(fs::removeAll(at(dir, "data/proposals.json")));
  auto res = io::save(dir, w2, dirty, &st);
  CHECK_EQ(res.written.size(), size_t(5));
  io::LoadResult r = io::load(dir);
  CHECK_MSG(r.warnings.empty(), warningsText(r.warnings));
  CHECK_EQ(r.world.province(1)->name, std::string("Не записано"));
  CHECK_EQ(r.generation, u64(2));
  CHECK_EQ(copies(dir, "provinces"), size_t(1));
}

// Файл занят другой программой (Windows: открыт без разрешения удаления): понятная ошибка, прежние версии на месте.
// На Linux и macOS открытый файл заменяется — тогда сохранение просто проходит.
TEST(core_io_storage_busy_file) {
  World w = richWorld();
  std::string dir = tempDir("st_busy");
  io::FileState st;
  io::save(dir, w, TB_ALL, &st);
  const auto before = texts(dir);
  World w2 = edited(w, [](Tx& tx) {
    tx.province(1).name = "Занято";
    tx.character(1).name = "Занят";
  });
  std::string e;
  {
    std::ifstream hold(fs::path(at(dir, "data/characters.json")), std::ios::binary);
    CHECK(hold.good());
    e = errorOf([&] { io::save(dir, w2, TB_PROVINCES | TB_CHARACTERS, &st); });
  }
  if (e.empty()) {
    CHECK_EQ(io::load(dir).world.character(1)->name, std::string("Занят"));
    return;
  }
  CHECK_MSG(e.find("занят другой программой") != std::string::npos && e.find("characters.json") != std::string::npos, e);
  for (auto& [rel, text] : before) CHECK_MSG(read(at(dir, rel)) == text, rel);
  CHECK(io::externalChanges(st).empty());
  io::save(dir, w2, TB_PROVINCES | TB_CHARACTERS, &st);
  CHECK_EQ(io::load(dir).world.character(1)->name, std::string("Занят"));
}

// world.json потерял метку (внешняя правка): поколения не сверяются; следующее сохранение нескольких файлов берёт
// поколение больше всех известных — ложного «смешанного сохранения» нет.
TEST(core_io_storage_generation_lost_in_world) {
  World w = richWorld();
  std::string dir = tempDir("st_lost");
  io::save(dir, w, TB_ALL);
  w = edited(w, [](Tx& tx) {
    tx.province(1).contentment = 1;
    tx.character(1).name = "Эдмунд I";
  });
  io::save(dir, w, TB_PROVINCES | TB_CHARACTERS);   // поколение 2
  write(at(dir, "world.json"), io::withoutGeneration(read(at(dir, "world.json"))));
  io::LoadResult r = io::load(dir);
  CHECK(r.mixed.empty());
  CHECK_EQ(r.generation, u64(0));
  w = edited(r.world, [](Tx& tx) {
    tx.faction(1).tax = 20;
    tx.meta().notes = "заметка";
  });
  io::save(dir, w, TB_FACTIONS | TB_META, &r.files);
  CHECK_EQ(io::generationIn(read(at(dir, "world.json"))), u64(3));
  io::LoadResult r2 = io::load(dir);
  CHECK(r2.mixed.empty());
  CHECK_MSG(r2.warnings.empty(), warningsText(r2.warnings));
}

// Несколько файлов без изменений world.json: он всё равно пишется последним (новое поколение), но его изменение
// другой программой не затирается молча.
TEST(core_io_storage_forced_world_respects_external) {
  World w = richWorld();
  std::string dir = tempDir("st_forced");
  io::FileState st;
  io::save(dir, w, TB_ALL, &st);
  const std::string worldPath = at(dir, "world.json");
  const std::string external = replaceAll(read(worldPath), "\"autosaveSec\": 120", "\"autosaveSec\": 300");
  CHECK(external != read(worldPath));
  write(worldPath, external);
  World w2 = edited(w, [](Tx& tx) {
    tx.province(1).name = "Новое имя";
    tx.character(1).name = "Новое имя героя";
  });
  std::string e = errorOf([&] { io::save(dir, w2, TB_PROVINCES | TB_CHARACTERS, &st); });
  CHECK_MSG(e.find("изменены другой программой") != std::string::npos && e.find("world.json") != std::string::npos, e);
  CHECK_EQ(read(worldPath), external);
  CHECK_EQ(io::load(dir).world.province(1)->name, w.province(1)->name);
  // С заменой — запись целиком.
  io::SaveOptions so;
  so.overwriteExternal = true;
  auto res = io::save(dir, w2, TB_PROVINCES | TB_CHARACTERS, &st, so);
  CHECK_EQ(res.written.size(), size_t(3));
  io::LoadResult r = io::load(dir);
  CHECK(r.mixed.empty());
  CHECK_EQ(r.world.settings->autosaveSec, 120);
  CHECK_EQ(r.world.province(1)->name, std::string("Новое имя"));
}

// Документ снимка с меткой поколения в файле (собран из файлов папки) читается без предупреждений.
TEST(core_io_storage_snapshot_ignores_generation) {
  World w = richWorld();
  json::Value doc = json::parse(io::toJson(w));
  doc["files"]["data/provinces.json"].set(io::kGenerationKey, 5);
  doc["files"]["world.json"].set(io::kGenerationKey, 5);
  io::LoadResult r = io::fromJson(json::write(doc));
  CHECK_MSG(r.warnings.empty(), warningsText(r.warnings));
  CHECK(diffWorld(w, r.world).empty());
}
