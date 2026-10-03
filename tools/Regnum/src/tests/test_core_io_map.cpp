// Тесты объектов карты в мире (data/map.json): запись и чтение, мир прежней версии без файла, нормализация,
// счётчики ID, биты изменённых таблиц.
#include "tests/test_core_io_util.h"

using namespace rg;
using namespace rg::iotest;

namespace {

std::string read(const std::string& path) { return fs::readFile(path).value_or(std::string()); }
void write(const std::string& path, const std::string& text) { CHECK(fs::writeFileAtomic(path, text)); }

}  // namespace

TEST(io_map_file_format) {
  World w = richWorld();
  std::string dir = tempDir("map-format");
  io::save(dir, w, TB_ALL);
  const std::string text = read(fs::join(dir, "data/map.json"));
  // Запись на строку: знаки компактными массивами, фигуры объектами; ширина только у линий.
  CHECK(text.find("\n    [1,\"mountain\",1200.25,800.5,1,1,0]") != std::string::npos);
  CHECK(text.find("\n    [2,\"peak\",1300,820,2.25,0,1.5]") != std::string::npos);
  CHECK(text.find("\"kind\":\"water\"") != std::string::npos);
  CHECK(text.find("\"holes\":[[2040,2040,2040,2060,2060,2060,2060,2040]]") != std::string::npos);
  CHECK(text.find("{\"dash\":3,\"id\":3,\"kind\":\"wall\",\"pts\":[100,4000,200,4010.5,300,4000],\"w\":2.5}") != std::string::npos);
  CHECK(text.find("\"kind\":\"islet\",\"pts\":[3000,3000,3010,3000,3005,3008]}") != std::string::npos);
  CHECK(read(fs::join(dir, "world.json")).find("\"mapObjects\": true") != std::string::npos);
  // Снимок мира (все файлы в одном документе) несёт объекты карты.
  io::LoadResult r = io::fromJson(io::toJson(w));
  CHECK_MSG(r.warnings.empty(), warningsText(r.warnings));
  std::string d = diffWorld(w, r.world);
  CHECK_MSG(d.empty(), d);
}

TEST(io_map_missing_file_old_world) {
  // Мир прежней версии: нет data/map.json, флага нет — без предупреждений, объекты базовой карты.
  World w = richWorld();
  {
    Tx tx(w);
    tx.meta().mapObjects = false;
    for (Id id : w.symbols.ids()) tx.eraseSymbol(id);
    for (Id id : w.shapes.ids()) tx.eraseShape(id);
    w = std::move(tx).finish();
  }
  std::string dir = tempDir("map-old");
  io::save(dir, w, TB_ALL);
  CHECK(fs::remove(fs::join(dir, "data/map.json")));
  io::LoadResult r = io::load(dir);
  CHECK_MSG(r.warnings.empty(), warningsText(r.warnings));
  CHECK(!r.world.meta->mapObjects);
  CHECK(r.world.symbols.empty() && r.world.shapes.empty());
  CHECK(!r.world.ownMapObjects());
  // Сохранение дописывает отсутствующий файл (пустые списки).
  auto res = io::save(dir, r.world, 0, &r.files);
  CHECK(std::find(res.written.begin(), res.written.end(), std::string("data/map.json")) != res.written.end());
  CHECK(read(fs::join(dir, "data/map.json")).find("\"symbols\": []") != std::string::npos);

  // Флаг есть, а файла нет — флаг снимается с предупреждением.
  World own = richWorld();
  std::string dir2 = tempDir("map-lost");
  io::save(dir2, own, TB_ALL);
  CHECK(fs::remove(fs::join(dir2, "data/map.json")));
  io::LoadResult r2 = io::load(dir2);
  CHECK(hasWarning(r2.warnings, "world.json", "meta.mapObjects", "нет файла"));
  CHECK(!r2.world.meta->mapObjects);
}

TEST(io_map_normalize) {
  World w = richWorld();
  std::string dir = tempDir("map-norm");
  io::save(dir, w, TB_ALL);
  // Флаг снят, а объекты есть; неверные значения в файле.
  std::string wj = read(fs::join(dir, "world.json"));
  const size_t at = wj.find("\"mapObjects\": true");
  CHECK(at != std::string::npos);
  wj.replace(at, std::string("\"mapObjects\": true").size(), "\"mapObjects\": false");
  write(fs::join(dir, "world.json"), wj);
  write(fs::join(dir, "data/map.json"), R"({
  "symbols": [
    [1, "mountain", 100, 200, 50, 3, 0],
    [2, "dragon", 1, 2, 1, 0, 0],
    [3, "castle", -10, 9000, 1, 1, "z"],
    [4, "tower", 10, 20, 1, 0],
    [5, "peak", 30, 40, 1, 0, 7]
  ],
  "shapes": [
    {"id": 1, "kind": "water", "pts": [0, 0, 10, 0]},
    {"id": 2, "kind": "wall", "pts": [0, 0, 10, 0], "w": -1, "holes": [[1, 1, 2, 2, 3, 1]]},
    {"id": 3, "kind": "river", "pts": [5, 5, 50, 50], "w": 500, "dash": 4},
    {"id": 4, "kind": "water", "pts": [0, 0, 100, 0, 100, 100], "holes": [[10, 10, 20, 10]], "w": 3},
    {"id": 9, "kind": "islet", "pts": [1, 1, 5, 1, 3, 4]}
  ]
}
)");
  io::LoadResult r = io::load(dir);
  const World& x = r.world;
  CHECK(x.meta->mapObjects);
  CHECK(hasWarning(r.warnings, "world.json", "meta.mapObjects", "mapObjects = true"));
  CHECK(hasWarning(r.warnings, "data/map.json", "symbols.1.s", "вне пределов"));
  CHECK_NEAR(x.symbol(1)->s, 8, 1e-6);
  CHECK(hasWarning(r.warnings, "data/map.json", "symbols.1.v"));
  CHECK_EQ(int(x.symbol(1)->v), 1);
  CHECK(hasWarning(r.warnings, "data/map.json", "symbols[1]", "неизвестный вид знака"));
  CHECK(!x.symbol(2));
  CHECK(hasWarning(r.warnings, "data/map.json", "symbols[2]", "знак пропущен"));   // z не число
  CHECK(!x.symbol(3));
  CHECK(x.symbol(4) && x.symbol(4)->z == 0);   // без z — 0
  CHECK(x.symbol(5) && x.symbol(5)->z == 7);
  CHECK(hasWarning(r.warnings, "data/map.json", "shapes.1", "фигура удалена"));
  CHECK(!x.shape(1));
  const MapShape* wall = x.shape(2);
  CHECK(wall && wall->holes.empty() && wall->w == 2);
  CHECK(hasWarning(r.warnings, "data/map.json", "shapes.2.holes", "только у воды"));
  const MapShape* river = x.shape(3);
  CHECK(river && river->w == 100 && river->dash == 0);
  const MapShape* lake = x.shape(4);
  CHECK(lake && lake->holes.empty() && lake->w == 0);
  CHECK(hasWarning(r.warnings, "data/map.json", "shapes.4.holes[0]", "короче 3 точек"));
  CHECK(x.shape(9));
  CHECK_EQ(x.meta->seq[size_t(Seq::Shape)], 9u);   // счётчик поднят до наибольшего ID
  CHECK(r.fixedTables & TB_MAPART);
}

TEST(io_map_tx_and_diff) {
  World w = newWorld("Карта");
  Tx tx(w);
  MapSymbol s;
  s.kind = SymbolKind::Castle;
  s.p = {10, 20};
  const Id sid = tx.add(s).id;
  CHECK_EQ(sid, 1u);
  MapShape sh;
  sh.kind = ShapeKind::River;
  sh.pts = {{0, 0}, {5, 5}};
  sh.w = 3;
  const Id hid = tx.add(sh).id;
  World w2 = std::move(tx).finish();
  CHECK_EQ(World::diff(w, w2), u32(TB_SYMBOLS | TB_SHAPES | TB_META));
  CHECK(w2.ownMapObjects());
  Tx t2(w2);
  t2.symbol(sid).p = {11, 21};
  World w3 = std::move(t2).finish();
  CHECK_EQ(World::diff(w2, w3), u32(TB_SYMBOLS));
  Tx t3(w3);
  t3.eraseShape(hid);
  World w4 = std::move(t3).finish();
  CHECK_EQ(World::diff(w3, w4), u32(TB_SHAPES));
  CHECK(!w4.shape(hid));
  CHECK_THROWS(Tx(w4).symbol(99));
}
