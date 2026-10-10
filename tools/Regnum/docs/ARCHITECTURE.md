# Regnum — архитектура

---
type: tool_documentation
status: active
canon_level: support
updated_real_date: 2026-10-10
---

Regnum — нативный редактор мира «Меча и Магии»: карта провинций, государства, гильдии, войска, экономика и ходы. Документ — контракт для всех, кто пишет код редактора (людей и агентов).

## 1. Жёсткие правила

1. **Язык — C++20, только стандартная библиотека.** Никаких сторонних библиотек, заголовков, систем сборки, скриптовых движков, веб-технологий. Всё своё: растеризатор, шрифты, интерфейс, PNG/JPEG, zlib, zip, JSON.
2. **Системные API ОС допустимы только в `src/platform/*`**: Win32/GDI (Windows), Xlib через `dlopen("libX11.so.6")` (Linux), Objective-C runtime + AppKit + CoreGraphics через `dlopen` (macOS). Остальной код не знает об ОС, кроме `base/fs.cpp` (пути) и `base/jobs.cpp` (потоки std).
3. **Одна кодовая база на три ОС.** Код вне `platform/` обязан компилироваться Clang и GCC на Windows, Linux, macOS. Нет `#ifdef _WIN32` вне `platform/` и `base/`.
4. **Мир неизменяем.** Читать — `const World&`. Менять — только `Store::transact(label, [&](Tx& tx){...})`. Исключение внутри — откат. Причина отказа для пользователя — `rg::fail("…")` (UserError, по-русски).
5. **Интерфейс по-русски, минимум текста, максимум значков.** Каждый значок-кнопка имеет всплывающую подсказку с названием и сочетанием клавиш.
6. **Никаких заглушек.** Функция либо работает до конца, либо её нет. Нет TODO в выпуске.
7. **Детерминизм.** Расчёты правил не зависят от порядка обхода хеш-таблиц, времени и потоков. Случайность — только `Rng` с явным зерном.
8. **Ошибки не роняют приложение.** Ошибка панели/инструмента показывается уведомлением, мир остаётся согласованным (транзакция откатывается).
9. **Люди — только целые.** Население, воины, рабы, корабли, жители для переселения — `i64`; любой расчёт с ними (доли, проценты, распределение по провинциям и расам) даёт целые числа: остаток раздаётся по одному (`rules::splitEven`, `rules::splitProportional`). Никогда не бывает «полутора человек». Правило действует для всех будущих изменений. Золото и прочие ресурсы — дробные (золото — до тысячных).

## 2. Каталоги

```
tools/Regnum/
  build.sh               сборка (Git Bash на Windows, Linux, macOS)
  build.ps1              сборка на Windows без Git Bash
  src/
    base/   base.h (типы, UTF-8, числа по-русски), json, fs, jobs
    codec/  zlib (inflate/deflate, crc32), png, jpeg, zip, base64
    gfx/    path.h, image, raster (AA), stroke, canvas, font (TrueType), icons, emblems, flag
    platform/ platform.h, win32.cpp, x11.cpp, cocoa.cpp, headless.cpp
    core/   world.h (модель, транзакции, хранилище), schema (перечисления), content (базовые записи мира), arch (таблицы археологии), io (JSON, папка проекта, zip-архив, снимки ходов)
    geo/    geom, grid, topo (грани плоского графа), ops (операции над провинциями)
    rules/  mods, builtins, calc, entities, catalog, population, forces, military, capture, heroes, rebellion, diplomacy, truce, trade, build, tech, guilds, routes, turn, log, areas, mapobjects, plague, archaeology, naval, talents
    map/    art, art_render, art_scene, art_extract (карта кодом: объекты, отрисовка, сцена мира, разбор), basemap, mapview, tiles, labels, overlay, marks, export, demo_world
    ui/     ui.h — собственный immediate-mode интерфейс, тема, виджеты, таблицы, всплывающие окна
    app/    main.cpp, приложение, экраны, панели, диалоги, редакторы деревьев, проект
    cli/    regnum-cli: validate, inspect, convert, new, build-map, icon
    tests/  test.h, test_main.cpp, test_<модуль>_*.cpp, сценарии интерфейса
  assets/
    source/Expanded Map.png   исходное изображение карты 8000 × 4500 — только вход разбора (редактор его не загружает)
    basemap/map.json           объекты карты, которые редактор рисует кодом (regnum-cli build-map)
    regnum.rc, regnum.ico, regnum.manifest
  bin/<os>/                    готовые сборки
  docs/                        ARCHITECTURE.md, FORMAT.md, RULES.md
```

Зависимости модулей (build.sh): `base ← codec ← gfx`, `codec ← core ← geo ← rules`, `gfx+rules ← map`, `gfx ← ui`, всё ← `app`. Модуль не включает заголовки модулей правее себя. `platform.h` не зависит от gfx: кадр передаётся как буфер пикселей.

Тесты модуля: `src/tests/test_<модуль>_<тема>.cpp`. Цель `./build.sh test-<модуль> --test` собирает модуль с зависимостями и его тесты. Артефакты тестов (PNG) — в `.wmma/regnum-tests/` корня репозитория.

## 3. Соглашения кода

- Пространство имён `rg`, подпространства по модулю: `rg::gfx`, `rg::geo`, `rg::rules`, `rg::ui`, `rg::platform`, `rg::map`, `rg::app`, `rg::codec`, `rg::json`, `rg::fs`, `rg::jobs`.
- Строки — UTF-8 `std::string`. Пути — UTF-8; преобразование в `std::filesystem::path` только через `fs::path(const std::string&)`.
- Числа для пользователя — только `fmtNum/fmtPct/fmtSigned/nTurns`. Сравнение названий — `compareRu`, поиск — `utf8::matches`.
- Комментарии — по-русски, кратко. Имена — по-английски.
- Предупреждения компилятора `-Wall -Wextra` не допускаются в своём коде.
- Глобальное изменяемое состояние — только в `app/` и кешах с явной синхронизацией.
- Пиксели изображений `gfx::Image` — premultiplied BGRA в `u32` (байты B, G, R, A в памяти). Это формат DIB Windows, XImage и CoreGraphics (PremultipliedFirst | ByteOrder32Little).

## 4. Модель мира

`src/core/world.h` — единственный источник формы данных. Ключевое:

- `World` — набор устойчивых таблиц `Table<T>` (блоки по 64 записи под `shared_ptr<const>`), метаданных, настроек, каталогов, отношений.
- `Tx` — черновик: `tx.province(id)` возвращает изменяемую копию; `tx.add(Province{})` выдаёт ID; `tx.eraseX(id)` удаляет без очистки ссылок.
- `Store` — текущее значение, отмена/повтор (400 шагов), объединение (`TxOptions::coalesce`), `dirtyTables()` для сохранения только изменённых файлов, подписки `subscribe`.
- `World::diff(a, b)` и `Table::diff` дают точечные изменения для инвалидации кешей и тайлов.
- Снимок хода — просто `World` (копирование O(1)).
- Объекты карты — таблицы `symbols` (`MapSymbol`: вид, точка привязки, масштаб, рисунок, порядок `z`) и `shapes` (`MapShape`: вода с островами, островок, стена, река), файл `data/map.json`; `Meta::mapObjects` / `World::ownMapObjects()` — мир хранит свои объекты (иначе показывает объекты базовой карты).

Перечисления, подписи, значки, пределы эффектов и константы ТЗ — `src/core/schema.h`. Формулы — [RULES.md](RULES.md). Формат файлов — [FORMAT.md](FORMAT.md).

Базовые записи мира — `src/core/content.h`: должности, 18 эссенций элементов, группы ресурсов «Руда», «Звери», «Провизия», «Материалы» с подгруппами и ресурсами, 61 религия, встроенные ресурсы (трупы, демоническая энергия, запчасти механизмов), особый отряд «Драконы Бездны» и общая постройка «Цитадель Бездны». `newWorld` создаёт их сразу и пишет версию `Meta::content`; мир меньшей версии дополняется при чтении (нормализация) один раз — записи сопоставляются по названию, прежние названия базовых должностей переименовываются вместе с местами совета. Новые базовые записи — новой версией `content::kVersion` с отдельным шагом `seed`.

Справочники (`Catalogs`): ресурсы (с группой `CatalogItem::group`), группы ресурсов (`ResGroup`, дерево по `parent`; ключи групп правил — `schema::grp`), расы, культуры, религии, формы правления, должности, эссенции элементов, реликвии (`Relic`, редкость `Rarity`), особые отряды (`SpecialUnit`). Общее дерево технологий — технологии с `faction == 0`, их изучение — у каждой фракции своё (`Faction::techs`).

## 5. Модули и их контракты

Точные сигнатуры — в заголовках модулей (они главнее этого раздела); здесь описано назначение и важные решения.

### 5.1 base (json, fs, jobs)

`base/json.h`: `json::Value` (null, bool, число double, строка, массив, объект с сохранением порядка ключей). `json::parse(text) -> Value` (строгий, ошибка с номером строки и столбца, `\u` и суррогаты), `json::write(value, {indent, sortKeys, compactNumbersArrays})`. Числа пишутся кратчайшим точным представлением (`std::to_chars`). Удобные методы: `get(key)`, `num(key, def)`, `str(key, def)`, `boolean(key, def)`, `arr(key)`, `obj(key)`, `set(key, v)`, `push(v)`.

`base/fs.h`: `readFile(path) -> optional<string>`, `writeFileAtomic(path, data)` (временный файл + замена; на Windows `MoveFileExW` с заменой), `exists`, `isDir`, `makeDirs`, `list(dir)`, `remove`, `removeAll`, `rename`, `copyFile`, `mtime`, `fileSize`, `exeDir`, `userDataDir` (Windows `%APPDATA%/Regnum`, Linux `$XDG_CONFIG_HOME/regnum` или `~/.config/regnum`, macOS `~/Library/Application Support/Regnum`), `homeDir`, `documentsDir`, `tempDir`, `join`, `parent`, `filename`, `stem`, `ext`, `path(const std::string&) -> std::filesystem::path`, `fromPath(path) -> std::string`.

`base/jobs.h`: пул потоков (`std::thread`), `jobs::parallelFor(n, fn(i))`, `jobs::submit(fn) -> std::future`, `jobs::workers()`.

### 5.2 codec

`codec/zlib.h`: `inflate(bytes, format)` (zlib/raw/gzip), `deflate(bytes, level, format)` (LZ77 + динамический Хаффман), `crc32`, `adler32`.
`codec/png.h`: `decodePng(bytes) -> RgbaImage{w, h, std::vector<u8> rgba}` (все типы цвета, 1–16 бит, палитра, tRNS, Adam7); `encodePng(RgbaImage, level)`.
`codec/jpeg.h`: `decodeJpeg(bytes) -> RgbaImage` (baseline и progressive, 4:4:4/4:2:2/4:2:0, оттенки серого, EXIF-ориентация).
`codec/zip.h`: чтение и запись zip (stored + deflate, UTF-8 имена).
`codec/base64.h`: `encode/decode`.

### 5.3 gfx

- `gfx/path.h` — `Path`, `Affine`, `Pt` (готово).
- `gfx/image.h` — `Image{w, h, std::vector<u32> px}` premultiplied BGRA; `fromRgba(RgbaImage)`, `toRgba()`, `resize` (бокс/билинейный), `crop`, `fill`.
- `gfx/raster.*` — сглаженная заливка контуров (non-zero и even-odd) с точной площадью покрытия, отсечение прямоугольником и маской.
- `gfx/stroke.*` — обводка: толщина, соединения miter/round/bevel, концы butt/round/square, пунктир.
- `gfx/canvas.h` — `Canvas(Image&)`: стек преобразований и отсечений (прямоугольник, скруглённый прямоугольник, произвольный контур), `fillPath`, `strokePath`, `fillRect`, `fillRoundRect` (радиусы углов), `strokeRoundRect`, `fillCircle`, `line`, `drawImage` (аффинно, билинейно, прозрачность), `drawImageRect`, `linear/radial` градиенты, `boxShadow(rect, radius, blur, spread, color)` с кешем, режимы смешивания normal/multiply/screen, `drawText`, `measureText`, `drawIcon`.
- `gfx/font.h` — TrueType/TTC: cmap 4/12, glyf (простые и составные), hmtx, kern; вариативные шрифты — экземпляр по умолчанию. Кеш глифов по (глиф, кегль ¼ px, субпиксельный сдвиг ¼ px). Раскладка: кернинг, перенос по словам, многоточие, выравнивание, цепочка запасных шрифтов. Отсутствующие U+202F/U+2009 — узкий пробел. Поиск системных шрифтов: Windows `%WINDIR%/Fonts` (Segoe UI, Segoe UI Semibold/Bold, Cambria/Constantia/Georgia, Consolas, Segoe UI Symbol), macOS `/System/Library/Fonts` и `/Library/Fonts` (SFNS, Helvetica Neue, Arial, Georgia, Menlo), Linux `/usr/share/fonts`, `~/.local/share/fonts`, `~/.fonts` (DejaVu, Noto, Liberation, Ubuntu, Cantarell). Шрифт с кириллицей обязателен; при отсутствии системного — сообщение об ошибке.
- `gfx/icons.h` — реестр значков: имя → контур на сетке 24 × 24, заданный строкой пути SVG (свой разборщик M/L/H/V/C/S/Q/T/A/Z, относительные команды). Стиль: линия 1,75, скруглённые концы и соединения; часть значков — заливкой. `drawIcon(canvas, name, rect, color, strokeScale)`. Неизвестное имя — заметный значок «?» и запись в журнал.
- `gfx/emblems.h` — геральдические эмблемы для флагов: одноцветный силуэт на сетке 100 × 100, детали — вырезы. Каталог разбит на группы (`emblemGroups`, каждая эмблема ровно в одной группе; имена хранятся в мирах и не меняются). «Классические»: корона, башня, замок, звезда, солнце, луна, дерево, лилия, меч, скрещённые мечи, щит, череп, якорь, корабль, колосья, самоцвет, молот, топор, лук, ключ, глаз, пламя, орёл, лев, дракон, волк, бык, конь, змей, кракен, руна, роза, грифон, медведь. Фантазийные группы (`gfx/emblems_fantasy.cpp`, имена с префиксом группы: `dragon-wings`, `dark-elf-spider`…): драконы (в полёте, свернувшийся, глаз, череп), вампиры (летучая мышь, клыки, кубок крови, гроб), природа (дубовый лист, олень, мухомор, ель), стихии (огонь, вода, воздух, земля, молния, лёд), гномы (наковальня, скрещённые кирки, гном, врата в горе), эльфы (лист, звезда, венок, лебедь), некроманты (череп и кости, коса, рука скелета, надгробие), тёмные эльфы (паук, паутина, скрещённые кинжалы, полумесяц и паук), наги (кобра, трезубец, раковина, два змея), рыцари (шлем, шахматный конь, латная перчатка, щит и меч), святые (крест, крылья и нимб, голубь, Святой Грааль), демоны (голова, бес, когтистая лапа, вилы), культисты (пентаграмма, капюшон, всевидящее око, голова козла), магия (шляпа мага, книга заклинаний, хрустальный шар, посох, зелье), орки (голова, вепрь, скрещённые тесаки, тотем), ящеры (ящерица, голова ящера, крокодил, храм-пирамида, след), зверолюды (минотавр, кентавр, сатир, гарпия, русалка). Окно флага показывает группы блоками в прокручиваемой области.
- `gfx/flag.h` — отрисовка `Flag` (узор + цвета + эмблема или PNG) в прямоугольник со скруглением, лёгкой тканевой тенью и рамкой.
- Фигурки карты: воин (войско) и корабль (флот) с цветом фракции — значки `fig-army`, `fig-fleet`, `fig-allied-army`, `fig-allied-fleet`.

### 5.4 platform

`platform/platform.h`:

```cpp
namespace rg::platform {
enum class Key : u16 { Unknown, A..Z, D0..D9, F1..F12, Escape, Enter, Tab, Backspace, Delete, Insert, Home, End, PageUp, PageDown,
                       Left, Right, Up, Down, Space, Minus, Equal, LBracket, RBracket, Semicolon, Quote, Comma, Period, Slash,
                       Backslash, Grave, NumPad0..NumPad9, NumAdd, NumSub, NumMul, NumDiv, NumDecimal, NumEnter, Shift, Ctrl, Alt, Super };
enum Mod : u32 { ModShift = 1, ModCtrl = 2, ModAlt = 4, ModSuper = 8 };
u32 primaryMod();                 // Ctrl на Windows/Linux, Super (⌘) на macOS
struct Event { Type type; float x, y; int button; int clicks; float wheelX, wheelY; bool precise; Key key; u32 mods; bool repeat;
               std::string text; std::vector<std::string> files; };
enum class Cursor { Arrow, Hand, IBeam, Crosshair, Move, ResizeH, ResizeV, ResizeNWSE, ResizeNESW, Grab, Grabbing, NotAllowed, Wait };
struct Frame { u32* px; int w, h, stride; float scale; };   // физические пиксели, premultiplied BGRA, непрозрачный результат
class App { public: virtual void onEvent(const Event&) = 0; virtual void onFrame(Frame&) = 0; virtual bool animating() = 0; virtual bool onCloseRequest() = 0; };
int run(App& app, const WindowConfig& cfg);   // цикл событий; кадр перерисовывается после invalidate() или пока animating()
void invalidate(); void wake();   // wake — потокобезопасно, из фоновых задач
void setCursor(Cursor); void setTitle(const std::string&); void setFullscreen(bool); bool fullscreen();
std::string clipboardText(); void setClipboardText(const std::string&);
std::optional<std::string> openFileDialog(title, filters, startDir); std::optional<std::string> saveFileDialog(...);
std::optional<std::string> pickFolderDialog(title, startDir);   // nullopt и dialogsSupported()==false → приложение показывает свой проводник
bool dialogsSupported(); void openPath(const std::string&); void openUrl(const std::string&);
void showFatal(const std::string& title, const std::string& text);
double time();
}
```

Координаты событий — логические пиксели (физические / scale). Колесо: `wheelY` в «строках» (1 щелчок = 1), у тачпада `precise = true` и пиксели. Двойной щелчок — `clicks = 2`. Перетаскивание файлов в окно — `FilesDropped`.

Реализации: `win32.cpp` (DPI v2, `WM_DPICHANGED`, DIB-секция, `WM_CHAR` с суррогатами, IME, захват мыши, `IFileOpenDialog`, буфер обмена, тёмная рамка окна, запоминание положения окна), `x11.cpp` (`dlopen` libX11, XIM/`Xutf8LookupString`, `XPutImage`, выделения CLIPBOARD/UTF8_STRING, курсоры, `Xft.dpi`), `cocoa.cpp` (`dlopen` libobjc/AppKit/CoreGraphics, класс NSView во время выполнения, Retina, NSPasteboard, NSOpenPanel/NSSavePanel), `headless.cpp` (кадры в память и сценарии для тестов и CLI).

### 5.5 geo

Плоский граф провинций в `World::nodes` и `World::edges`. Дуга — полилиния между узлами; береговые дуги (`Coast`) получены из базовой карты и правятся в режиме «Правка карты», граница карты — `Frame`. Стороны дуги помечены провинцией и сушей/морем. Грани графа — части провинций; провинция может состоять из нескольких граней (острова). `landRings(const World&)` собирает из дуг с сушей по одну сторону контуры суши (суша слева: внешние контуры и дыры — противоположной ориентации) — по ним рисуется море.

`geo/geom.h`: пересечение отрезков с допуском, проекция на отрезок, площадь и ориентация кольца, точка в многоугольнике с дырами (чёт-нечет), полюс недоступности (polylabel), упрощение Дугласа — Пекера с сохранением топологии, габариты.

`geo/grid.h`: пространственный хеш для отрезков и граней.

`geo/topo.h`:
```cpp
struct Face { Id province; Terrain terrain; double area; Box2 box; std::vector<std::vector<Vec2>> rings; /* [0] внешний, далее дыры */ std::vector<std::vector<HalfEdge>> ringEdges; Vec2 label; };
struct ProvinceShape { std::vector<int> faces; double area; Box2 box; Vec2 label; };
struct FaceSet {
  std::vector<Face> faces; std::unordered_map<Id, ProvinceShape> provinces;
  int locate(Vec2) const; Id provinceAt(Vec2) const; Terrain terrainAt(Vec2) const;
  std::vector<Id> provincesOnPolyline(const std::vector<Vec2>&) const;   // для торговых маршрутов
  std::vector<std::pair<Id, Id>> neighbors() const;                        // смежность провинций
};
std::shared_ptr<const FaceSet> faces(const World&);   // кеш по тождеству таблиц nodes/edges (потокобезопасно)
struct Issue { std::string code, msg; Vec2 at; };
std::vector<Issue> validate(const World&);            // пересечения, вырожденные дуги, висячие узлы, несогласованные метки
```

`geo/ops.h` — все операции внутри транзакции; ошибка — `fail("…")`:
```cpp
void initFromCoast(Tx&, const Coast&);                          // рамка + береговые кольца; всё не назначено
Id createProvince(Tx&, const std::vector<Vec2>& poly, Terrain t /*None = по первой точке*/);   // новая провинция из свободной площади внутри многоугольника: соседи не режутся, граница идёт по их границе; узкие щели у соседей поглощаются; берег «прилипает»
void addArea(Tx&, Id province, const std::vector<Vec2>& poly);  // расширить (соседи уменьшаются)
void removeArea(Tx&, Id province, const std::vector<Vec2>& poly);  // вырезать в «не назначено»
Id fillAt(Tx&, Vec2 p, Id province /*0 = новая*/);              // назначить грань целиком (остров одним щелчком)
double paintTerrain(Tx&, const std::vector<Vec2>& poly, Terrain t);   // суша или море внутри контура; берег — где рельеф по сторонам разный
Id split(Tx&, Id province, const std::vector<Vec2>& line, NewProvinceFn fn = {});   // нож; возвращает новую провинцию
void merge(Tx&, Id target, Id source);                          // геометрия source → target (запись source удаляет rules)
void unassign(Tx&, Id province);                                // геометрия → «не назначено»
struct Handle { enum Kind { None, Node, Point } kind; Id edge, node; int index; };
// coast = false — ручки границ (берег и рамка закреплены), coast = true — ручки берега (рамка закреплена)
Handle hitHandle(const World&, Vec2 p, double tol, Id province /*0 = любые*/, bool coast = false);
std::optional<EdgeHit> hitEdge(const World&, Vec2 p, double tol, Id province = 0, bool coast = false);
Vec2 handlePos(const World&, const Handle&); bool handleLocked(const World&, const Handle&, bool coast = false);
bool canMove(const World&, const Handle&, Vec2 to, bool coast = false);   // без пересечений
void moveHandle(Tx&, const Handle&, Vec2 to, bool coast = false);         // общая граница двигает обе провинции
Handle insertPoint(Tx&, Id edge, int segment, Vec2 p, bool coast = false);
void deletePoint(Tx&, const Handle&, bool coast = false);
void slideJunction(Tx&, Id node, Vec2 to);                      // узел на берегу скользит вдоль берега
```
При правке границ береговые точки заблокированы, стык границы с берегом скользит вдоль берега; в режиме «Правка карты» точки и узлы берега подвижны (стык двигает и конец границы), а `paintTerrain` делает сушей или морем целые области. Провинция «морская» (`Province::sea`) — флаг данных; рельеф граней провинции согласован с ним (перекраска рельефа снимает с граней провинцию другого типа, `rules::paintTerrain` удаляет провинции без области). Рельеф грани используется для прилипания и проверок войск (войско — суша, флот — море).

### 5.6 rules

Один заголовок `rules/rules.h`, реализация по файлам. Расчёты — от `const World&`, изменения — через `Tx&`. Все формулы — [RULES.md](RULES.md).

```cpp
struct EffectSource { enum Kind { Province, Faction, Tech, Building, Guild } kind; Id id; Id modifier; };
struct Effects { std::array<double, kFxCount> v{}; std::map<Id, double> diplomacy; std::vector<EffectSource> sources; double operator[](Fx f) const; };
Effects provinceEffects(const World&, Id province);
Effects factionEffects(const World&, Id faction);

struct GuildShare { Id guild; double pct; bool hq; double gross, tax, net; };
struct ProvinceCalc { bool sea; Id owner, recipient; int slotsSize, slotsCity, slotsMods, slots, slotsUsed; double tradeValue; int routes;
                      double production; double rebellion; double buildCostFactor; double taxState, taxLocal, taxTotal; i64 population;
                      std::vector<std::pair<Id, i64>> races; std::vector<GuildShare> guilds; double provinceTax, guildTax; Effects fx; };
struct RowCalc { Id row; i64 total, field, garrison, reserve; double upkeepEach, upkeepTotal; };
struct ResourceFlow { double stock, production, tradeIn, tradeOut, net; };
struct FactionCalc { std::vector<Id> provinces, hqs; i64 population; std::vector<std::pair<Id, i64>> races;
                     double incProvinces, incGuildTax, incGuilds, incTrade, incTribute, incGross, incomePct, incTotal;
                     double expArmy, expFleet, expSpecialists, expTrade, expTribute, expTotal, net, treasury;
                     std::vector<RowCalc> army, fleet; std::map<Id, ResourceFlow> resources; Effects fx; };
struct Calc { std::unordered_map<Id, ProvinceCalc> provinces; std::unordered_map<Id, FactionCalc> factions; std::unordered_map<Id, int> routeCounts; };
std::shared_ptr<const Calc> calc(const World&);   // кеш по версии мира, потокобезопасно
```

Действия (все бросают `UserError` при нарушении правил и пишут хронику, где это событие мира):
- `entities`: `createFaction(tx, kind)`, `removeFaction`, `createCharacter`, `removeCharacter`, `createModifier`, `removeModifier`, `createBuilding(tx, owner)`, `removeBuilding`, `createTech(tx, faction)`, `removeTech`, `addCatalogItem(tx, list)`, `removeCatalogItem`, `deleteProvince(tx, id)` (геометрия + запись + ссылки), `mergeProvinces(tx, target, source)`, `setProvinceOwner(tx, id, faction)`, `setOccupied(tx, id, occupier)`, `copyTechTree(tx, from, to)`.
- `military`: `createArmy(tx, kind, faction, pos)`, `setUnits(tx, army, faction, row, count)`, `setHero(tx, army, character, on)`, `setCommander`, `setGarrison(tx, province, row, count)`, `disband`, `moveArmy(tx, army, pos)`, `encounter(w, moving, target) -> Encounter{Merge, Battle, Alliance, DeclareWar, Blocked}`, `mergeArmies`, `formAllied`, `dissolveAllied`, `splitArmy(tx, army, spec, pos)`, `resolveBattle(tx, BattleResult)`, `findFreeSpot(w, kind, near, exclude)`, `deployed(w, faction)`.
- `diplomacy`: `setRelation(tx, a, b, value, status)`, `declareWar`, `relationsOf(w, faction)` — все прочие фракции.
- `trade`: `validateDeal`, `concludeDeal(tx, Deal)` (разовые позиции исполняются сразу), `cancelDeal`, `imposeTribute(tx, kind, receiver, payer, amount, turns)`.
- `build`: `options(w, province) -> вариантов с ценой и причинами недоступности`, `startBuilding(tx, province, building)`, `cancelBuilding` (полный возврат стоимости), `demolish`.
- `tech`: `canResearch`, `setStudied`, `startResearch`, `stopResearch`, `wouldCycle`, `autoLayout(tx, faction)`.
- `guilds`: `buildHq`, `removeHq`, `setInfluence`, `setHomeState`, `createStateGuild(tx, state)`.
- `routes`: `createRoute`, `setRoutePoints`, `removeRoute`.
- `log`: `addLog(tx, kind, text, refs)`.
- `turn`: `TurnReport endTurn(Tx&)`, `TurnReport previewTurn(const World&)`.

Объекты карты (`rules/mapobjects.cpp`): `ensureMapObjects` — перенос объектов базовой карты в мир при первой правке (те же ID); знаки — `addSymbol`, `placeSymbol` (точка и порядок `z`), `setSymbol` (вид, масштаб, рисунок), `removeSymbol`; фигуры — `addShape`, точки контура и островов (`setShapePoint`, `insertShapePoint`, `removeShapePoint`; контур не пересекает сам себя), `moveShape`, `setShapeLine` (ширина и пунктир), `removeShape`.

Новые механики (ТЗ 2026-10): `builtins.cpp` — встроенные модификаторы, ресурсы и глобальные константы (создаются в мире при первом обращении; до этого действует шаблон `schema`), модификаторы сущностей со сроками и правилами (`setModifiers`, `addModifier`, `dropModifier`), модификаторы, которые ставятся сами (`autoModifiers`: столица, совет, голод, должности); `population.cpp` — распределение людей целыми числами, рабы, колонизация, пустошь нежити и осквернение; `forces.cpp` — формирование отрядов (население, трупы, демоническая энергия, стоимость кораблей), роспуск резерва, флот в торговле, оккупационный гарнизон, верность; `capture.cpp` — штурм гарнизона и захват провинции; `heroes.cpp` — судьба героев, воскрешение, пленники; `rebellion.cpp` — мятеж войск, мятежное государство, восстание провинции, переход перед боем; `truce.cpp` — перемирие и вассалитет. События хода, требующие решения (восстания, мятежи с верностью −100 %), — `TurnReport::events`.

ТЗ 2026-10-07: `catalog.cpp` — группы ресурсов (`resourcesIn` — с подгруппами), реликвии и инвентарь, особые отряды и доступ к ним (`specialAccess`, `addSpecialRow`, `syncSpecialRows`), ключевой ресурс и цены строк армии (`setRowType`, `setRowKey`, `keyResources`, `setRowExtra`, `setRowEssence`, `setRowEssUpkeep`), запасы эссенций; `forces.cpp` — найм с ключевым и дополнительными ресурсами и эссенциями (звери, чудовища, механизмы и элементали — без людей), верность гарнизона; `military.cpp` — герои гарнизона; `rebellion.cpp` — мятеж гарнизона и переход гарнизона перед штурмом; `build.cpp` — культовые постройки, требования к технологиям и правила общего/уникального дерева, мгновенное завершение и изначальные постройки, постройки преобразования и их возможности (`setBuildingRole`, `setRecipe`, `setLevelEssence`, `setBuildingSpecial`); `tech.cpp` — общее дерево (`techState`, изучение за фракцию); `calc.cpp` — провизия по группе (поровну, недостача), план построек преобразования (`FactionCalc::conversions` — его же исполняет `endTurn`), эссенции (генерация, содержание элементалей).

ТЗ «Доработки №1–4» (2026-10-10): `core/arch.*` — таблицы археологии (слоты и шансы мест с целочисленным выравниванием, этапы и награды, уровни групп, бедствия и их войска, раскопки, базовые сундуки и списки наград, `rollSlots`); `plague.cpp` — чума, иммунитет, лечение и заражение постройками (распространение при естественном окончании — в `endTurn`); `archaeology.cpp` — археологические группы, поиск и исследование мест, трагедии и бедствия, раскопки, сундуки, тайник реликвий, режим правки, справочники мест и сундуков (`ArchReport` — итог события для окна); `naval.cpp` — вместимость флота, посадка и высадка, обмен отрядами, возврат группы союзного войска, флот у верфи; `talents.cpp` — классы героев, дерево талантов, изучение, лич; в `catalog.cpp` — места реликвий (`RelicPlace`, `moveRelic`; находки археологов героям только из владений государства), хранилища построек, группы реликвий; `forces.cpp` — наёмники, верфи и морские чудовища; `build.cpp` — одна стройка в провинции, цена в эссенциях, требования «на государство», приморские постройки, возможности построек (`BuildingFlag`); `tech.cpp` — стоимость исследования и требуемые постройки по ключу; `military.cpp` — войска без государства (фракция `FactionKind::Wild`, враждебна всем через `World::relation`); `mods.cpp` — показатели археологических групп (`ArchStats`), эффекты модификаторов героев войска; `calc.cpp` — генерация эссенций и ресурсов модификаторами, содержание археологических групп; `builtins.cpp` — «Влияние совета» (`AutoMod::scale`), «Ценности археологии», константы с базовой стоимостью.

### 5.7 core/io

Чтение и запись папки проекта (раскладка — FORMAT.md), нормализация (значения по умолчанию, пределы, висячие ссылки → предупреждения), архив `.regnum` (zip той же папки), снимки ходов `history/turn-NNNN-SSSSSS.json.gz` (начало и конец хода, ветви после возврата; новые — в памяти до сохранения), резервные копии `.regnum-backup/` (ручные и автосохранения — разные очереди), обнаружение внешних изменений файлов и архива (по размеру, времени и хешу), список недавних проектов и автосохранение в `userDataDir`. Сохраняются только изменённые таблицы (`Store::dirtyTables`). JSON — с отсортированными ключами и стабильным порядком для аккуратных диффов в Git.

### 5.8 map

- Карта нарисована кодом. `art.*` — объекты карты (`assets/basemap/map.json`): кольца суши (береговая линия мира) и мелких островков, внутренние воды (реки и озёра — кольца с островами), стены (ломаные с шириной) и знаки (горы двух рисунков, крупные горы, замки, башни — точка привязки, масштаб, вариант) в порядке отрисовки. `art_render.*` — отрисовка: море с мягкой светлой каймой у берега (размытая маска суши), воды, стены, знаки; значки заданы геометрией в коде (замок и башня — прямоугольники штампа, гора — треугольник с шапкой и тенью, параметры подобраны по штампам исходника), поэтому при масштабе 1 карта повторяет исходник, а при приближении остаётся чёткой. Пространственный индекс выбирает объекты тайла.
- `art_extract.*`, `basemap_segment.cpp`, `basemap_coast.cpp`, `basemap_build.*` — разбор исходного изображения (`regnum-cli build-map`): слои воды и краски, береговая линия (marching squares, упрощение с сохранением топологии), изолинии внутренних вод, поиск штампов знаков (целые пиксели, затем доли пикселя и другой размер), порядок наложения знаков по пикселям перекрытий, стены по осевым линиям. Повторный разбор детерминирован и даёт тот же map.json (тест `map_art_extract_matches_assets`).
- `basemap.*` — базовая карта во время работы: map.json, береговая линия мира, маска моря и превью с миниатюрой, нарисованные кодом; `objects()` — её знаки и фигуры как записи мира (`art::baseObjects`).
- `art_scene.*` — сцена карты мира (`MapView::artScene()`): контуры суши из графа мира (`geo::landRings`), знаки и фигуры из `World::symbols`/`World::shapes` или, пока мир их не хранит, базовой карты; знаки — в порядке `z`; попадание мышью (`symbolAt`, `shapeAt`, `symbolsIn`). Сцена перестраивается (≈ 1 мс), когда меняются объекты карты или берег; перерисовываются только тайлы у изменённых объектов, у берега — с мягким краем моря. Превью и миниатюра карты мира (запасной слой, мини-карта, миниатюра проекта) рисуются по сцене в фоне, когда правка затихнет.
- `mapview.h/.cpp` — камера, композиция кадра, режимы, легенда, мини-карта, попадание мышью. Внутреннее: `tiles.cpp` и `tilestore.cpp` (фоновые тайлы 512 px на текущем масштабе со всем, что под подписями), `pixels.cpp` (наложение, копирование и масштабирование изображений), `style.cpp` (цвета режимов), `labels.cpp` (подписи со спрайтовым кешем), `overlay.cpp` (маршруты, диаграммы гильдий, маркеры, войска, выделение), `marks.cpp` (раскладка отметок войск и флота).
- Палитра карты (`map::Palette`, `art::paletteStyle`, `paletteId`): `Source` — цвета исходника (стиль map.json, чёрная краска знаков; по умолчанию в редакторе), палитры цветокоров (`Sapphire`, `Emerald`, `Crimson`, `Amethyst`, `Obsidian` — тот же id, что у схемы интерфейса) — насыщенное море тона схемы, светлая суша, реки и озёра глубже моря, знаки тёмной тёплой краской (серый рисунка g → `mix(ink, paper, g / 255)`), за краем карты — море темнее. `MapView::setPalette` (по умолчанию `Source`) перерисовывает в фоне тайлы (палитра — часть стиля тайла), превью, миниатюры и мини-карту; легенда и экспорт (`MapExport`, параметр палитры) — в той же палитре. У `Source` фон окна за краем карты — `RenderOptions::background` (фон страниц цветокора).
- Камера: `setSafeArea` — свободная часть окна между панелями (приложение передаёт её каждый кадр). «Показать всю карту» вписывает мир в неё, `minZoom` — масштаб, при котором весь мир виден в ней; смена свободной части (открылся инспектор) камеру не сдвигает.
- Войска и флот не наслаиваются на экране: отметки, которые при текущем масштабе пересеклись бы (фигурка, значок численности, с зазором), сливаются в стопку — верхний объект (выделенный, иначе самый многочисленный), за ним до двух фигурок других объектов цветами их фракций, золотой значок с числом объектов, численность — сумма. Раскладка не зависит от сдвига камеры; при приближении стопка распадается. `armyAt` возвращает верхний объект, `markAt`/`armyMarks` — отметки, `separateZoom` — масштаб, при котором стопка распадётся (щелчок по стопке инструментом «Выбор» приближает к нему, перетаскивание переносит верхний объект).
- Торговый маршрут рисуется ровно по своей ломаной (изломы скруглены обводкой) — по той же линии считаются его провинции и бонус +10 % (`geo::provincesOnPolyline`).
- `export.h/.cpp` — экспорт всей карты в изображение (`MapExport`, `exportMap`): свой `MapView` с кадром ровно по карте (ширина 640–10000, высота по пропорциям 16 : 9), тот же вид, что в редакторе, без выделения, наведения, инструментов и линии края. Тайлы рисуются в фоне, поэтому экспорт идёт шагами (`step()` каждый кадр, пока не вернёт true). Окно экспорта — `app/dialogs/export.cpp` (Ctrl+E): размер, подписи, войска и флот, файл PNG, запись PNG — в `jobs`.
- `demo_world.*` — вымышленный демонстрационный мир для тестов и показа (не канон кампании).
- Порядок слоёв: суша → заливка провинций → море → реки и озёра → заливка сухопутных провинций (без галочки «Морская») на морской части карты, полупрозрачно → границы, штриховка оккупации → знаки карты (стены, горы, замки, башни) → подписи → маршруты, диаграммы гильдий, штабы, столицы → войска и флот → выделение и инструменты.
- Инвалидация точечная: после изменения мира перерисовываются только тайлы, задетые изменёнными дугами, провинциями и объектами карты; старые тайлы видны до готовности новых.

### 5.9 ui

Собственный immediate-mode интерфейс поверх `gfx::Canvas`. Требования:

- Устойчивые ID виджетов (хеш строки + стек областей), горячий/активный элемент, фокус клавиатуры, Tab/Shift+Tab, Enter/Esc.
- Раскладка: строки/столбцы с отступами и промежутками, фиксированные и «гибкие» размеры, прокручиваемые области с инерцией, виртуализация длинных списков, разделители с перетаскиванием.
- Слои: основной, всплывающие (выпадающие списки, контекстные меню), подсказки, модальные окна, уведомления, перетаскиваемый объект.
- Виджеты: кнопка (основная, вторичная, призрачная, опасная), кнопка-значок, сегментный переключатель, переключатель, флажок, радио, ползунок, числовое поле (перетаскивание по подписи, колесо, ввод с проверкой пределов, единицы), текстовое поле (однострочное и многострочное, выделение, буфер обмена, отмена в поле, поиск слов), выпадающий список с поиском, выбор цвета (палитра + HSV + hex), вкладки-значки, таблица (сортировка, правка ячеек, добавление/удаление строк, итоговая строка, закреплённая шапка), дерево, значки-чипы фракций/провинций/ресурсов, полоса прогресса, индикатор (−100…100), круговая диаграмма, бейджи, пустые состояния, уведомления, подтверждения.
- Анимации: плавные переходы наведения (120 мс), выезд панелей (180 мс), без дёрганий; при отсутствии анимаций кадр не перерисовывается.
- Масштаб интерфейса: системный DPI × пользовательский множитель (90–150 %).

### 5.10 app

Реестры `app.h`: TabReg (вкладки выделения: в панели — значками, на странице — пунктами навигации; `wide` — во всю ширину страницы), HeaderReg (шапка выделения), QuickReg (самое нужное о выделении в нижней строке), SectionReg (разделы на месте карты — верх правой ленты: каталог и страницы сущностей или редакторы группы), DrawerReg (панели справа от карты — низ правой ленты), EditorReg (редакторы на месте карты; `group` — раздел, в навигации которого стоит редактор), ToolReg (инструменты карты), CommandReg (команды и сочетания), DialogReg (модальные окна по ID). Общие виджеты предметной области — `app/widgets.h`. Панели: `app/panels/*` (провинция, фракции, войска, персонажи, хроника, торговля, постройки, технологии, маршрут, объекты карты), диалоги `app/dialogs/*`, редакторы `app/editors/*`, инструменты `app/tools*.cpp`.

Сквозные окна, которые открываются из разных мест, — `app/flows.h` (`flow::`): штурм гарнизона и захват провинции (`dialogs/siege.cpp`, `dialogs/capture.cpp`), действия мятежников после боя (`rebelAftermath`), «Судьба героев» (`dialogs/hero_fate.cpp`), мятеж (`panels/army_inspector.cpp`), перемирие (`dialogs/truce.cpp`), вассалитет после объявления войны (`dialogs/vassal.cpp`), события хода (`app/turn_events.cpp`). Каждое принимает `done` и вызывает его на любом пути (решение, отказ, закрытие), поэтому окна выстраиваются в очередь. Связь с каноном проекта (карточки персонажей и локаций по ID и названию, заметки только для чтения, портрет) — `app/canon.h`.

Доработки 2026-10-10: вкладки «Археология» провинции и государства (`panels/province_arch.cpp`, `panels/faction_arch.cpp`, общие слоты — `panels/arch_common.*`, окна итога и «божественного вмешательства» — `dialogs/arch_result.cpp`), справочники мест, сундуков и классов героев (`editors/catalogs_arch.cpp`, `editors/catalogs_classes.cpp`, дерево талантов — `app/talent_tree.*`), вкладка героя «Таланты» и лич (`panels/character_talents.cpp`), хранилище реликвий постройки (`panels/relic_store.cpp`), кнопки лечения и заражения (`panels/construction_roles.cpp`), наёмники (`panels/military_merc.cpp`), флот у верфи и войско на борту (`panels/military_naval.cpp`), обмен отрядами (`dialogs/exchange.cpp`), инструмент высадки (`tools_landing.cpp`, `ToolId::Landing`; `ToolDef::hidden/blocked` — инструмент без значка на ленте и недоступный с причиной). Золото подписывается «тыс.» (`fmtGold`, `fmtGoldSigned`, `fmtGoldShort` в `base`).

Режимы правки карты взаимоисключающие: «Правка границ» (`UiState::editBorders`, E; `ToolDef::editMode`) и «Правка карты» (`UiState::editMap`, T; `ToolDef::mapMode`). Инструменты правки карты — `app/tools_map_*.cpp` (объекты, знаки и кисть, озеро, река, стена, суша и море контуром, берег), общие помощники — `app/tools_map.h` (`mapedit::act` — перенос объектов в мир и правка одной транзакцией, порядок нового знака по соседям), инспектор — `app/panels/map_inspector.cpp` (`SelType::Symbol`, `SelType::Shape`; несколько знаков — `UiState::symbolGroup`).


Экран запуска (новый мир, открыть папку, открыть архив, недавние) и основной экран (`app/shell*.cpp`). Основной экран закреплён по краям окна, плавающих панелей нет: верхняя строка (`shell_bars.cpp`: знак, мир и состояние сохранения, режим карты, параметры инструмента — `tools::OptionsBar` в её середине, в узком окне строкой над картой; поиск, отмена, повтор, сохранение, настройки), нижняя строка (выделение и QuickReg, «Скрыть панель», координаты, ход, «Завершить ход»), лента инструментов слева и лента разделов справа (`shell_side.cpp`). Между лентами — карта и закреплённые панели справа от неё (панель выделения и панель ленты; ширина тянется за левый край) или страница на месте карты (`shell_pages.cpp`): навигация слева, содержимое на фоне страницы (колонка до 820 точек по середине или во всю ширину). Что на месте карты — `App::view()`: редактор (`ui.editor`), каталог раздела (`ui.directory`), страница выделения (`ui.page`; государство, гильдия и персонаж открываются страницей сразу, остальное — кнопкой «Развернуть»), иначе карта. `back()` снимает верхний слой (Esc), `toMap()` возвращает карту и выделение на ней, с которого перешли на страницу (`ui.backSel`); раздел ленты открывается вместо текущей страницы (`openSection`), инструмент карты со страницы возвращает карту. Цветокор и цвета карты — настройки программы (`UiState::scheme`, `UiState::schemeMap`; app.json `scheme`, `schemeMap`): по умолчанию «Сапфир» и исходные цвета карты. Диалоги (битва, встреча войск, разделение, штурм, захват, судьба героев, воскрешение, перемирие, вассалитет, дань, выборщики, флаг, итог хода, история ходов, настройки, экспорт карты, проводник файлов), палитра команд Ctrl+K, справка по клавишам F1 — модальные окна.

## 6. Дизайн

Цветокоры (`ui::schemeInfo`, `ui::setScheme`) вместо светлой и тёмной темы: насыщенные, но не светлые основы разного тона — поверхности несут тон схемы, текст почти белый, акцент — чистое золото (у обсидиана — янтарь). Первый — «Сапфир», по умолчанию. Ощущение: дорогой стратегический редактор, ничего лишнего; всё прилегает к краям окна и друг к другу, области разделены тонкими линиями.

| Токен | Сапфир | Изумруд | Багрянец | Аметист | Обсидиан |
| --- | --- | --- | --- | --- | --- |
| bg (фон страниц) | #0a1426 | #061b18 | #180910 | #110b22 | #09090b |
| surface1 (строки, ленты, панели) | #0f1d38 | #0a2723 | #230e18 | #18102f | #111216 |
| surface2 (карточки) | #152649 | #0f332d | #2e1420 | #20163e | #18191f |
| surface3 (поля, наведение) | #1b3159 | #154038 | #3b1a2a | #2a1e4f | #212329 |
| border | #22396a | #1a4b42 | #4a2133 | #33255e | #272a31 |
| borderStrong | #30508c | #25685b | #672e48 | #4a3684 | #383c46 |
| text | #f4f2ec | #f2f4ee | #f7f0ec | #f4f1fb | #f6f6f3 |
| textDim | #b5c4e0 | #b0d2c6 | #dcbcc6 | #c4b9e2 | #babec7 |
| textMuted | #7b8fb8 | #739c8f | #a07c88 | #8b80b2 | #7d828f |
| accent | #f5b83d | #f2b33d | #f5b83d | #f5b83d | #ffb21f |
| accentHover | #ffcb5c | #ffc75a | #ffcb5c | #ffcb5c | #ffc44f |
| success | #3ccf82 | #4fd08f | #45c97f | #45c98a | #35cc7c |
| warning | #f6a623 | #f6a623 | #f6a623 | #f6a623 | #ffa62b |
| danger | #f25a52 | #f25a52 | #ff6b5e | #f25a6c | #ff5a4f |
| info | #4cabff | #4cabff | #5ab0ff | #5aa9ff | #3ea6ff |
| море карты (цвета цветокора) | #1d5596 | #11606c | #1c4466 | #2b3f8f | #1a4c78 |

onAccent — #1a1103; наложения наведения, нажатия, выделения и чередования строк — из текста и акцента схемы. Суша карты в цветах цветокора — светлая тёплая (#efe9dd…#f3ebda).

Радиусы: поля 7, карточки 10, всплывающие и модальные окна 14, таблетки — полный; закреплённые строки, ленты, панели и страницы — без скругления, тени и рамки (разделители — линия border). Тени окон: 0 14 36 rgba(0,0,0,.45) (тёмная). Отступы: 4/8/12/16/24/32. Шрифты: интерфейс — системный без засечек (13 px основной, 12 px вторичный, 15/18/24 заголовки); названия государств и заголовки разделов — системный шрифт с засечками (Cambria/Georgia/DejaVu Serif). Значки — линия 1,75 на сетке 24, размер 18–20 px. Фокус клавиатуры — золотое кольцо 2 px.

Принципы: значок вместо слова, подсказка на каждом значке; числа — крупно, подписи — мелко; опасные действия — красным и с подтверждением; пустое состояние — одна строка и кнопка; никаких обучающих и поясняющих надписей в панелях, на экране запуска и в строке состояния (как пользоваться — во всплывающей подсказке значка и в списке сочетаний F1); ни одного «технического» текста для пользователя; всё, что меняет мир, — отменяется Ctrl+Z. Заголовок окна — «Regnum»; название мира и состояние сохранения — в верхней панели.

## 7. Проверки

- `./build.sh --test` — все модульные тесты (ядро, геометрия с фаззингом, правила, кодеки, растеризация с эталонными PNG, интерфейс в headless-режиме).
- Сценарии интерфейса (`src/tests/test_app_*.cpp`) — настоящие обработчики событий на `headless`-платформе, снимки экрана PNG и проверки состояния мира.
- `regnum-cli validate <папка мира>` — целостность данных и геометрии; используется в `tools/Проверить_проект.ps1`.
