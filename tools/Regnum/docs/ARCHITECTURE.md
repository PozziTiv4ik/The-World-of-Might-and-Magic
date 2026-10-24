# Regnum — архитектура

---
type: tool_documentation
status: active
canon_level: support
updated_real_date: 2026-10-23
---

Regnum — нативный редактор мира «Меча и Магии»: карта провинций, государства, гильдии, войска, экономика и ходы. Документ — контракт для всех, кто пишет код редактора (людей и агентов).

## 1. Жёсткие правила

1. **Язык — C++20, только стандартная библиотека.** Никаких сторонних библиотек, заголовков, систем сборки, скриптовых движков, веб-технологий. Всё своё: растеризатор, шрифты, интерфейс, PNG/JPEG, zlib, zip, JSON.
2. **Системные API ОС допустимы только в `src/platform/*`**: Win32/GDI (Windows), Xlib через `dlopen("libX11.so.6")` (Linux), Objective-C runtime + AppKit + CoreGraphics через `dlopen` (macOS); звук — waveOut и Media Foundation (Windows, DLL Media Foundation загружаются во время выполнения), ALSA, libmpg123 и libvorbisfile через `dlopen` (Linux), AudioToolbox через `dlopen` (macOS). Остальной код не знает об ОС, кроме `base/fs.cpp` (пути) и `base/jobs.cpp` (потоки std).
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
  src/
    base/   base.h (типы, UTF-8, числа по-русски), json, fs, jobs, process (дочерние процессы), flag.h (данные флага)
    codec/  zlib (inflate/deflate, crc32), image (RgbaImage, формат по сигнатуре), png, jpeg, zip, base64, sha256,
            wav (WAV и RF64), audioinfo (теги и длительность звуковых файлов по заголовку)
    gfx/    path.h, image, raster (AA), flatten.h, stroke, blend.h, canvas, font (TrueType), fontsys (системные шрифты),
            text (раскладка), svgpath, glyph (векторные глифы; glyph_builder.h — построитель реестров), icons, emblems
            (emblems.cpp — классические, emblems_fantasy_a/_b — тематические, emblems_races_a/_b/_c — расовые,
            emblems_registry — реестр; emblems_internal.h), figures (фигурки карты), flag, logo.h (знак программы)
    platform/ platform.h, common (общее ядро: очередь событий, щелчки, UTF-16, положение окна), win32.cpp,
            x11.cpp (+ x11_api.h), cocoa.cpp, headless.cpp, demo/ (platform-demo); звук — audio.h (плеер саундтреков:
            audio.cpp — общий движок, audio_internal.h; вывод и декодеры — audio_win32, audio_alsa, audio_mac,
            audio_headless)
    core/   world.h (модель и транзакции; общий заголовок: types.h — ID и перечисления, catalogs.h — справочники
            и константы, table.h — Table/TableEdit, store.h — хранилище), schema (перечисления, подписи, константы
            правил), content (базовые записи мира: content_v1, content_v2, content_v3, content_v4, content_v5, content_v6,
            content_classes — 20 классов героев с подклассами и деревьями талантов, content_seed.h), arch (таблицы археологии),
            io (папка проекта, архив, снимки ходов, автосохранение), io_json (файлы и ссылки), io_encode, io_decode,
            io_assemble, io_normalize (+ io_normalize_<область> — шаги нормализации)
    geo/    geom, grid, graph (внутреннее: граф для операций), topo (грани плоского графа), ops (операции над
            провинциями), handles (ручки границ и берега)
    rules/  rules.h (общий заголовок тематических: effects, calc, entities, mapobjects, forces, military, diplomacy,
            build, tech, turn, archaeology, naval, talents, plots, proposals), internal.h; реализация — раздел 5.6
    map/    art, art_render, art_scene (карта кодом: объекты, отрисовка, сцена мира), basemap, mapview, tiles, labels, overlay, marks, export, demo_world
    mapbake/ разбор исходного изображения карты в map.json (только regnum-cli build-map и тесты): art_extract
            (+ _symbols — поиск штампов, _dev — отладка и разработка), basemap_build.h, basemap_segment, basemap_coast
    ui/     ui.h — собственный immediate-mode интерфейс, тема, виджеты, таблицы, всплывающие окна
    app/    main.cpp; приложение (app, registry, navigation, app_dialogs, frame), общие виджеты (widgets), команды;
            shell/ — экран и его полосы, запуск, палитра команд, настройки; project/ — открытие, сохранение,
            проводник; turn/ — ход, история и их окна; tools/ (+ map/) — инструменты карты; panels/ — вкладки
            выделения; editors/ — полноэкранные редакторы; dialogs/ — окна; common/ — канон, дерево талантов
    cli/    regnum-cli: validate, inspect, convert, new, build-map, icon
    tests/  test.h, test_main.cpp, test_<модуль>_*.cpp, сценарии интерфейса
  assets/
    source/Expanded Map.png   исходное изображение карты 8000 × 4500 — только вход разбора (редактор его не загружает)
    basemap/map.json           объекты карты, которые редактор рисует кодом (regnum-cli build-map)
    soundtracks/               саундтреки плеера верхней строки (файлы WAV, MP3, M4A/AAC, WMA, FLAC… — по ОС)
    regnum.rc, regnum.ico, regnum.manifest
  bin/windows/, bin/linux-x64/, bin/linux-arm64/, bin/macos-arm64/, bin/macos-x64/   готовые сборки (в репозитории)
  bin/linux/, bin/macos/       локальные сборки на Linux и macOS (не в Git)
  docs/                        ARCHITECTURE.md, FORMAT.md, RULES.md, UI.md
```

Зависимости модулей (build.sh): `base ← codec ← gfx`, `codec ← core ← geo ← rules`, `gfx+rules ← map ← mapbake`, `gfx ← ui`, `map+ui ← app` (без `mapbake`), `mapbake ← cli`. Модуль не включает заголовки модулей правее себя. `platform.h` не зависит от gfx: кадр передаётся как буфер пикселей. Платформу компонуют только `regnum` (своя ОС), `regnum-tests` и тесты интерфейса (`headless`); `regnum-cli` и тесты модулей без `ui` обходятся без неё.

Тесты модуля: `src/tests/test_<модуль>_<тема>.cpp`. Цель `./build.sh test-<модуль> --test` собирает модуль с зависимостями и его тесты (`regnum-test-<модуль>`). Тесты запускаются из корня репозитория: артефакты (PNG) — в `.wmma/regnum-tests/` относительно текущей папки (другая — `--out=<папка>` или переменная `REGNUM_TEST_OUT`).

## 3. Соглашения кода

- Пространство имён `rg`, подпространства по модулю: `rg::gfx`, `rg::geo`, `rg::rules`, `rg::ui`, `rg::platform`, `rg::map`, `rg::app`, `rg::codec`, `rg::json`, `rg::fs`, `rg::jobs`.
- Строки — UTF-8 `std::string`. Пути — UTF-8; преобразование в `std::filesystem::path` только через `fs::path(const std::string&)`.
- Числа для пользователя — только `fmtNum/fmtPct/fmtSigned/nTurns`. Сравнение названий — `compareRu`, поиск — `utf8::matches`.
- Комментарии — по-русски, кратко. Имена — по-английски.
- Предупреждения компилятора `-Wall -Wextra` не допускаются в своём коде.
- Глобальное изменяемое состояние — только в `app/` и кешах с явной синхронизацией.
- Пиксели изображений `gfx::Image` — premultiplied BGRA в `u32` (байты B, G, R, A в памяти). Это формат DIB Windows, XImage и CoreGraphics (PremultipliedFirst | ByteOrder32Little).

## 4. Модель мира

`src/core/world.h` — единственный источник формы данных (общий заголовок модели: базовые типы и перечисления — `core/types.h`, справочники и глобальные константы — `core/catalogs.h`, устойчивая таблица — `core/table.h`, хранилище — `core/store.h`). Ключевое:

- `World` — набор устойчивых таблиц `Table<T>` (блоки по 64 записи под `shared_ptr<const>`), метаданных, настроек, каталогов, отношений.
- Таблицы сущностей перечислены один раз — `RG_WORLD_TABLES` в `world.h` (тип, поле, методы `Tx`, бит `TableBit`, последовательность ID, подпись); из этого списка строятся поля `World`, методы `Tx`, сравнение версий, чтение и счётчики ID. Новая таблица — строка в списке, бит `TableBit`, файл и формат в `core/io`.
- `Tx` — черновик: `tx.province(id)` возвращает изменяемую копию; `tx.add(Province{})` выдаёт ID; `tx.eraseX(id)` удаляет без очистки ссылок.
- `Store` — текущее значение, отмена/повтор (400 шагов), объединение (`TxOptions::coalesce`), `dirtyTables()` для сохранения только изменённых файлов, подписки `subscribe`.
- `World::diff(a, b)` и `Table::diff` дают точечные изменения для инвалидации кешей и тайлов.
- Снимок хода — просто `World` (копирование O(1)).
- Заговоры — таблица `plots` (`Plot`: вид, заказчик и цель, провинции с приверженностью и снабжением, участники, приглашённые, угрозы, лидер, заказные убийства, сведения контрразведки цели), файл `data/plots.json`.
- Объекты карты — таблицы `symbols` (`MapSymbol`: вид, точка привязки, масштаб, рисунок, порядок `z`) и `shapes` (`MapShape`: вода с островами, островок, стена, река), файл `data/map.json`; `Meta::mapObjects` / `World::ownMapObjects()` — мир хранит свои объекты (иначе показывает объекты базовой карты).
- Поиск в списках записей — `findById`, `findByKey` (`core/types.h`), в таблицах — `Table::findIf`, `Table::firstId`; цепочки ссылок (родители групп, условия талантов, сюзерены) — `chainReaches` с защитой от циклов.

Перечисления, подписи, значки, пределы эффектов, константы правил, последовательности ID (`schema::kSeqs`: имя счётчика, префикс ID, падеж для сообщений) и простые списки справочников (`schema::kCatalogLists`) — `src/core/schema.h`. Формулы — [RULES.md](RULES.md). Формат файлов — [FORMAT.md](FORMAT.md).

Базовые записи мира — `src/core/content.h`: должности, 18 эссенций элементов, группы ресурсов «Руда», «Звери», «Провизия», «Материалы», «Валюты» с подгруппами и ресурсами, 70 религий, встроенные ресурсы (`schema::kBuiltinResources`: трупы, демоническая энергия, запчасти механизмов, археологические сокровища, свитки Шантири), особые отряды и постройки доступа к ним, сундуки сокровищ и археологические места, классы героев, ветка технологий «Археология», порт, верфь, гильдия наёмников, храмы и соборы эссенций. `newWorld` создаёт их сразу и пишет версию `Meta::content`; мир меньшей версии дополняется при чтении (нормализация) один раз — записи сопоставляются по ключу или названию, прежние названия базовых должностей переименовываются вместе с местами совета. Версия 3 добавляет подклассы и деревья талантов (`content_classes.cpp`: 20 классов, 70 подклассов, 980 талантов), 51 расу и 55 культур, ресурсы бездны, около ста реликвий (археологические находки и «Общие»), постройки «Доработок №4» (рынок, лечебница, чумная лаборатория, хранилище реликвий, казармы, шахта, лесопилка, каменоломня, алхимические, мясопилка, иссушительница, окропительница, сталелитейная) и технологию «Магический порошок». Версия 4 (`content_v4.cpp`) добавляет ресурсы «Мех», «Прочная чешуя», «Хитин», «Титанобоа», значок «Археологических сокровищ», постройки «Доработок №1» 2026-10-22 (ферма, бойня, алтарь культа, храмы освящения и осквернения, обогатители, жилой квартал, мастерские, углежогная яма, гильдия строителей; модификаторы уровней — встроенные `housingN`, `buildersN`), технологии «Производство пороха» и ветку «Стимуляция промышленности» (99, ключи `industryN`), «Штаб Разведки» и «Штаб Контрразведки» каждого государства (`content::addIntelBuildings` — и в `rules::createFaction`). Версия 5 (`content_v5.cpp`) добавляет «Сталелитейной» рецепт «Обсидиановая сталь». Версия 6 (`content_v6.cpp`, шаги `stepRenames` … `stepBuildTimes`; аудит 2026-10-23, п.7): модификаторы справочников (расы, культуры, веры, формы правления, должности), классов, талантов и реликвий, уровней порта, рынка и верфи; 7 ступеней «Стимуляции промышленности» вместо 99 (ID прежних `industryN` сохраняются); потребители мёртвых ресурсов (нефтяная вышка, ткацкая мастерская, оружейная палата, рецепты); элементали соборов, стрелки и пехота на редких материалах; сроки уровней по цене (`detail::buildTurnsOf`). Шаги версий — `content_v1.cpp` … `content_v6.cpp` (общий `detail::Seeder` — `content_seed.h`); новые базовые записи — новой версией `content::kVersion` с отдельным шагом `seed`; встроенный модификатор записывается в мир `content::builtinModifierRecord`.

Справочники (`Catalogs`): ресурсы (с группой `CatalogItem::group`), группы ресурсов (`ResGroup`, дерево по `parent`; ключи групп правил — `schema::grp`), расы, культуры, религии, формы правления, должности, эссенции элементов, реликвии (`Relic`, редкость `Rarity`) и их группы (`RelicGroup`; группы ресурсов и реликвий — один шаблон `TreeGroup`), особые отряды (`SpecialUnit`), классы героев с деревьями талантов, археологические места, сундуки сокровищ. Общее дерево технологий — технологии с `faction == 0`, их изучение — у каждой фракции своё (`Faction::techs`).

## 5. Модули и их контракты

Точные сигнатуры — в заголовках модулей (они главнее этого раздела); здесь описано назначение и важные решения.

### 5.1 base (json, fs, jobs, process)

`base/json.h`: `json::Value` (null, bool, число double, строка, массив, объект с сохранением порядка ключей). `json::parse(text) -> Value` (строгий, ошибка с номером строки и столбца, `\u` и суррогаты), `json::write(value, {indent, sortKeys, compactNumbersArrays})`. Числа пишутся кратчайшим точным представлением (`std::to_chars`). Удобные методы: `get(key)`, `num(key, def)`, `str(key, def)`, `boolean(key, def)`, `arr(key)`, `obj(key)`, `set(key, v)`, `push(v)`.

`base/fs.h`: `readFile(path) -> optional<string>`, `writeFileAtomic(path, data)` (временный файл + замена; на Windows `MoveFileExW` с заменой), `writeTemp` и `replaceFile` (то же по шагам — сохранение нескольких файлов одним целым), `exists`, `isDir`, `makeDirs`, `list(dir)`, `remove`, `removeAll`, `rename`, `copyFile`, `mtime`, `fileSize`, `exePath`, `exeDir`, `userDataDir` (Windows `%APPDATA%/Regnum`, Linux `$XDG_CONFIG_HOME/regnum` или `~/.config/regnum`, macOS `~/Library/Application Support/Regnum`), `homeDir`, `documentsDir`, `tempDir`, `join`, `parent`, `filename`, `stem`, `ext`, `path(const std::string&) -> std::filesystem::path`, `fromPath(path) -> std::string`.

`base/jobs.h`: пул потоков (`std::thread`), `jobs::parallelFor(n, fn(i))`, `jobs::submit(fn) -> std::future`, `jobs::workers()`.

`base/process.h`: `process::run(exe, args, outFile)` — дочерний процесс с выводом в файл (параллельный прогон тестов).

### 5.2 codec

`codec/zlib.h`: `inflate(bytes, format)` (zlib/raw/gzip), `deflate(bytes, level, format)` (LZ77 + динамический Хаффман), `crc32`, `adler32`.
`codec/image.h`: `RgbaImage{w, h, std::vector<u8> rgba}` (не premultiplied); `decodeImage(bytes)` — PNG или JPEG по сигнатуре.
`codec/png.h`: `decodePng(bytes) -> RgbaImage` (все типы цвета, 1–16 бит, палитра, tRNS, Adam7); `encodePng(RgbaImage, level)`, `writePngFile(path, RgbaImage, level)`.
`codec/jpeg.h`: `decodeJpeg(bytes) -> RgbaImage` (baseline и progressive, 4:4:4/4:2:2/4:2:0, оттенки серого, EXIF-ориентация).
`codec/zip.h`: чтение и запись zip (stored + deflate, UTF-8 имена).
`codec/base64.h`: `encode/decode`.
`codec/sha256.h`: `sha256Hex(data, size)` (контрольная сумма исходника карты).
`codec/wav.h`: `parseWav(readAt, size)` (заголовок по частям файла: PCM, IEEE float, WAVE_FORMAT_EXTENSIBLE, RF64), `wavToFloat` (8/16/24/32 бита, float 32/64 → float), `decodeWav`, `encodeWav` (PCM 16/24, float 32, теги LIST/INFO).
`codec/audioinfo.h`: `probeAudio(readAt, size)` — вид, название, исполнитель и длительность звукового файла без декодирования: MP3 (Xing/Info, VBRI, иначе по битрейту), WAV, FLAC, Ogg Vorbis и Opus, MP4/M4A, WMA, AIFF, AAC (ADTS); теги ID3v2.2–2.4 (сжатие, рассинхронизация), ID3v1, LIST/INFO, Vorbis comment, iTunes ilst, ASF; однобайтовые теги — UTF-8, Windows-1251 или Latin-1 по составу (`tagText8`).

### 5.3 gfx

- `gfx/path.h` — `Path`, `Affine`, `Pt` (готово).
- `gfx/image.h` — `Image{w, h, std::vector<u32> px}` premultiplied BGRA; `Image::fromRgba(rgba, w, h)`, `toRgba()`, `toRgbaImage()` (для записи PNG), `scaled` (уменьшение по площади, увеличение бикубически), `cropped`; `Mask` — 8-битное покрытие.
- `gfx/raster.*` — сглаженная заливка контуров (non-zero и even-odd) с точной площадью покрытия, отсечение прямоугольником и маской.
- `gfx/stroke.*` — обводка: толщина, соединения miter/round/bevel, концы butt/round/square, пунктир.
- `gfx/canvas.h` — `Canvas(Image&)`: стек преобразований и отсечений (прямоугольник, скруглённый прямоугольник, произвольный контур), `fillPath`, `strokePath`, `fillRect`, `fillRoundRect` (радиусы углов), `strokeRoundRect`, `fillCircle`, `line`, `drawImage` (аффинно, билинейно, прозрачность), `drawImageRect`, `linear/radial` градиенты, `boxShadow(rect, radius, blur, spread, color)` с кешем, режимы смешивания normal/multiply/screen, `drawText`, `measureText`, `drawIcon`.
- `gfx/font.h` — TrueType/TTC: cmap 4/12, glyf (простые и составные), hmtx, kern; вариативные шрифты — экземпляр по умолчанию. Кеш глифов по (глиф, кегль ¼ px, субпиксельный сдвиг ¼ px). Раскладка: кернинг, перенос по словам, многоточие, выравнивание, цепочка запасных шрифтов. Отсутствующие U+202F/U+2009 — узкий пробел. Поиск системных шрифтов: Windows `%WINDIR%/Fonts` (Segoe UI, Segoe UI Semibold/Bold, Cambria/Constantia/Georgia, Consolas, Segoe UI Symbol), macOS `/System/Library/Fonts` и `/Library/Fonts` (SFNS, Helvetica Neue, Arial, Georgia, Menlo), Linux `/usr/share/fonts`, `~/.local/share/fonts`, `~/.fonts` (DejaVu, Noto, Liberation, Ubuntu, Cantarell). Шрифт с кириллицей обязателен; при отсутствии системного — сообщение об ошибке.
- `gfx/fontsys.cpp` — поиск системных шрифтов; `gfx/text.h` — системные шрифты, метрики, раскладка (перенос, многоточие, табуляция), отрисовка и поддержка редактирования текста (`initFonts`, `fontReport`).
- `gfx/svgpath.h` — разбор строки пути SVG (M/L/H/V/C/S/Q/T/A/Z, относительные команды) и запись чисел пути; `gfx/flatten.h` — аппроксимация кривых ломаными; `gfx/blend.h` — арифметика premultiplied-пикселей и ядра смешивания (оба — внутренние заголовки gfx).
- `gfx/glyph.h` — векторные глифы (значки, эмблемы, фигурки): слои обводки, заливки и ластика на квадратной сетке сводятся в одну маску покрытия; подгонка к пикселям до 40 px, кеш масок (`clearGlyphCache`). `gfx/glyph_builder.h` (только внутри gfx) — построитель реестров глифов из строк SVG и генераторы фигур (`shapes::circ`, `star`, `gear`…).
- `gfx/icons.h` — реестр значков: имя → глиф на сетке 24 × 24. Стиль: линия 1,75, скруглённые концы и соединения; часть значков — заливкой. `drawIcon(canvas, name, rect, color, strokeScale)`. Неизвестное имя — заметный значок «?» и запись в журнал.
- `gfx/emblems.h` — геральдические эмблемы для флагов: одноцветный силуэт на сетке 100 × 100, детали — вырезы. Каталог разбит на группы (`emblemGroups`, каждая эмблема ровно в одной группе; имена хранятся в мирах и не меняются) двух разделов (`EmblemGroup::section`): «Темы» — «Классические» (`emblems.cpp`, 42 эмблемы: корона, башня, замок, звезда, солнце, луна, дерево, лилия, меч, щит, череп, орёл, лев, дракон…) и 18 тематических групп (`emblems_fantasy_a/_b.cpp`, имена с префиксом группы: `dragon-wings`, `dark-elf-spider`…; 158 эмблем: драконы, вампиры, природа, стихии, гномы, эльфы, некроманты, тёмные эльфы, наги, рыцари, святые, демоны, культисты, магия, орки, ящеры, зверолюды); «Расы» — по группе на каждую из 51 расы базовых записей (`emblems_races_a/_b/_c.cpp`, имена `race-…`, по 4 эмблемы). `emblemRegistryIssues` проверяет подписи и группы. Окно флага: разделы «Темы / Расы», поиск по подписи эмблемы и названию группы, список групп для перехода, сетка превью блоками групп.
- `gfx/flag.h` — отрисовка `Flag` (узор + цвета + эмблема или PNG) в прямоугольник со скруглением, лёгкой тканевой тенью и рамкой.
- `gfx/figures.h` — фигурки и метки карты (войско, флот, столица, штаб гильдии, битва); воин и корабль с цветом фракции — ещё и значки `fig-army`, `fig-fleet`, `fig-allied-army`, `fig-allied-fleet`.
- `gfx/logo.h` — знак программы (золотой щит с короной) и плитка значка: экран запуска и `regnum-cli icon`.

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

Реализации: `win32.cpp` (DPI v2, `WM_DPICHANGED`, DIB-секция, `WM_CHAR` с суррогатами, IME, захват мыши, `IFileOpenDialog`, буфер обмена, тёмная рамка окна, запоминание положения окна), `x11.cpp` (`dlopen` libX11, XIM/`Xutf8LookupString`, `XPutImage`, выделения CLIPBOARD/UTF8_STRING, курсоры, `Xft.dpi`), `cocoa.cpp` (`dlopen` libobjc/AppKit/CoreGraphics, класс NSView во время выполнения, Retina, NSPasteboard, NSOpenPanel/NSSavePanel), `headless.cpp` (кадры в память и сценарии для тестов). Общее для всех — `common.h/.cpp` (только внутри platform): очередь событий и вызовы приложения `detail::Dispatch` (состояние окна win32, x11 и cocoa наследует его: `push`, `guarded`, `flushEvents`, `callAnimating`, `requestClose`), счётчик щелчков, сборка UTF-16, названия клавиш, положение окна. `demo/demo_main.cpp` — окно-демонстрация `platform-demo` с самопроверкой (`--selftest`).

Звук — `platform/audio.h`: плеер саундтреков приложения. `audio::scanFolder(dir)` — поддерживаемые файлы папки по имени (`compareRu`) с тегами и длительностью (`codec::probeAudio`), `audio::formats()` — расширения этой сборки. `audio::Player` — потокобезопасное управление (играть, пауза, стоп, следующий, предыдущий, перемотка, громкость, без звука, перемешивание, повтор) и поток воспроизведения: декодер → стерео (сведение многоканального звука) → частота устройства (свой ресемплер windowed sinc, 32 отсчёта) → громкость и затухания 6 мс при паузе, перемотке и смене трека → устройство (стерео float, 48 кГц). Позиция и звучащий трек — по тому, что устройство уже проиграло: треки идут подряд без паузы, а трек на экране сменяется вместе со звуком. Разбор папки — в своём потоке. Окно движок будит `platform::invalidate()` только при смене трека, состояния, списка, ошибки и по ходу полосы (`Player::setTick`). Системная часть (`audio_internal.h`: `Device`, `Decoder`): `audio_win32.cpp` — waveOut (float 32 или PCM 16 с треугольным шумом) и Media Foundation (`mfplat.dll`, `mfreadwrite.dll` через `LoadLibrary`: MP3, AAC/M4A, WMA, FLAC), `audio_alsa.cpp` — ALSA (`libasound.so.2`), MP3 — `libmpg123.so.0`, Ogg Vorbis — `libvorbisfile.so.3`, все через `dlopen` (нет библиотеки — формат не попадает в список), `audio_mac.cpp` — AudioQueue и ExtAudioFile (AudioToolbox и CoreFoundation через `dlopen`: MP3, AAC/M4A, AIFF, FLAC, CAF), `audio_headless.cpp` — тихое устройство, которое проигрывает очередь по `platform::time()` (виртуальное время тестов). WAV везде декодируется своим кодом (`codec/wav.h`). Звук собирается только в программы с приложением (`regnum`, `regnum-tests`, `regnum-test-app`; `build.sh`, функция `audio_sources`).

### 5.5 geo

Плоский граф провинций в `World::nodes` и `World::edges`. Дуга — полилиния между узлами; береговые дуги (`Coast`) получены из базовой карты и правятся в режиме «Правка карты», граница карты — `Frame`. Стороны дуги помечены провинцией и сушей/морем. Грани графа — части провинций; провинция может состоять из нескольких граней (острова). `landRings(const World&)` собирает из дуг с сушей по одну сторону контуры суши (суша слева: внешние контуры и дыры — противоположной ориентации) — по ним рисуется море.

`geo/geom.h`: пересечение отрезков с допуском, проекция на отрезок, площадь и ориентация кольца, точка в многоугольнике с дырами (чёт-нечет), полюс недоступности (polylabel), упрощение Дугласа — Пекера, габариты, допуск прилипания по умолчанию `kDefaultSnap`.

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

`geo/ops.h` — все операции внутри транзакции; ошибка — `fail("…")`. Операции с областями (`geo/ops.cpp`) идут через копию графа (`geo/graph.h`) с полной проверкой; ручки и скольжение по берегу (`geo/handles.cpp`) правят узлы и дуги транзакции напрямую:
```cpp
void initFromCoast(Tx&, const Coast&);                          // рамка + береговые кольца; всё не назначено
struct EditOptions { double snap = kDefaultSnap; };             // допуск прилипания ввода (инструменты — в пикселях экрана / масштаб)
// createProvince, addArea, removeArea, split, paintTerrain — и с EditOptions (без него — kDefaultSnap)
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
bool isCoastJunction(const World&, Id node);                    // стык границы с берегом (в режиме границ скользит)
void slideJunction(Tx&, Id node, Vec2 to);                      // узел на берегу скользит вдоль берега
std::optional<Vec2> slideTarget(const World&, Id node, Vec2 to);   // куда сдвинется стык (предпросмотр)
```
При правке границ береговые точки заблокированы, стык границы с берегом скользит вдоль берега; в режиме «Правка карты» точки и узлы берега подвижны (стык двигает и конец границы), а `paintTerrain` делает сушей или морем целые области. Провинция «морская» (`Province::sea`) — флаг данных; рельеф граней провинции согласован с ним (перекраска рельефа снимает с граней провинцию другого типа, `rules::paintTerrain` удаляет провинции без области). Рельеф грани используется для прилипания и проверок войск (войско — суша, флот — море).

### 5.6 rules

Общий заголовок `rules/rules.h` включает тематические заголовки (`effects.h`, `calc.h`, `entities.h`, `mapobjects.h`, `forces.h`, `military.h`, `diplomacy.h`, `build.h`, `tech.h`, `turn.h`, `archaeology.h`, `naval.h`, `talents.h`, `plots.h`, `proposals.h`; каждый собирается сам по себе); общие помощники реализации — `rules/internal.h`. Расчёты — от `const World&`, изменения — через `Tx&`. Все формулы — [RULES.md](RULES.md).

```cpp
struct EffectSource { enum Kind { Province, Faction, Tech, Building, Guild, Army, Auto, Hero, Trait } kind; Id id; Id modifier; std::string key; };
// Auto — модификатор, который ставится сам (key — его ключ); Hero — модификатор героя (свой, класса, реликвии, таланта);
// Trait — раса, культура, религия или форма правления (key — «race», «culture», «religion», «government»)
struct HeroMod { Id modifier; double scale; };
std::vector<HeroMod> heroModifiers(const World&, Id character);   // модификаторы героя с весами (таланты — × ранги)
struct Effects { std::array<double, kFxCount> v{}; std::map<Id, double> diplomacy; std::vector<EffectSource> sources; double operator[](Fx f) const; };
Effects provinceEffects(const World&, Id province);
Effects factionEffects(const World&, Id faction);
Effects armyEffects(const World&, Id army);

struct ProvinceCalc { /* владелец и получатель дохода (occupied), слоты, торговая ценность и маршруты, добыча, риск восстания,
                         налоги и довольство за ход (taxUnrest, contentDelta), предел и прирост населения (popCap, growthBase,
                         growthPct), население по расам, доли гильдий (guildBlocked), рабы на работах, производство и эссенции
                         построек, Effects fx */ };
struct FactionCalc { /* провинции, население, доходы и расходы по статьям, строки армии и флота (RowCalc), ресурсы
                        (ResourceFlow) и эссенции (EssenceFlow) за ход, провизия (provisionSoldiers), голод (famineDeaths),
                        невыплата жалованья (unpaid), план преобразования (conversions — его же исполняет endTurn), риск
                        пиратов, множитель времени исследования */ };
struct Calc { std::unordered_map<Id, ProvinceCalc> provinces; std::unordered_map<Id, FactionCalc> factions; std::unordered_map<Id, int> routeCounts; };
std::shared_ptr<const Calc> calc(const World&);   // кеш по тождеству таблиц мира, потокобезопасно
std::shared_ptr<const Calc> calc(const Tx&);      // внутри транзакции: кеш — только пока транзакция ничего не меняла
```

Действия (все бросают `UserError` при нарушении правил и пишут хронику, где это событие мира) — по файлам реализации:

- `mods.cpp` — источники эффектов (провинция с лордом, культурой, религией и преобладающей расой; государство с правителем, советом, культурой, религией, формой правления и модификаторами своих провинций; войско с героями), показатели археологических групп (`ArchStats`), общие помощники текстов и проверок.
- `proposals.cpp` — предложения другой стороне и ожидающие решения государств (`Proposal`, `data/proposals.json`): предложить, принять (действие по правилам сделок, дани, перемирия, союза, вассалитета), отклонить, отозвать, очистка невозможных (`fitProposals` — в начале хода и при упразднении фракции).
- `builtins.cpp` — встроенные модификаторы, ресурсы и глобальные константы (создаются в мире при первом обращении; до этого действует шаблон `schema`), расы отрядов, модификаторы сущностей со сроками и правилами (`setModifiers`, `addModifier`, `dropModifier`), модификаторы, которые ставятся сами (`autoModifiers`: столица, совет и «Влияние совета», голод, невыплата жалованья, должности, «Ценности археологии»; `autoProvinceModifiers`: «Чужая вера»), число назначений в совет.
- `calc.cpp` — полный расчёт мира и его кеш, развёрнутые силы (`deployed`), оккупант провинции (`occupierOf`); `forces.cpp` — резерв строки, формирование и роспуск, флот в торговле, оккупационный гарнизон, верность войск и гарнизонов, распад элементалей при долге эссенции (`elementalDecay`), лимит наёмников, доступ к верфям.
- `entities.cpp` — фракции, персонажи и их назначения (правитель, совет, лорд), модификаторы: создание и удаление с очисткой ссылок; `trees.cpp` — постройки, их уровни и технологии деревьев, копирование дерева технологий; `catalog_lists.cpp` — простые списки справочников (`addCatalogItem`, `removeCatalogItem`); `catalog.cpp` — группы ресурсов и реликвий (одна реализация дерева групп), реликвии и их места (`RelicPlace`, `moveRelic`), особые отряды и доступ к ним, запасы эссенций.
- `rows.cpp` — строки войск и флота: создание (обычные, наёмники, особые отряды), численность, раса, тип, ключевой ресурс (`keyAllowed` — правило `schema::keyAllowed`) и цены найма, удаление строки с отрядами.
- `areas.cpp` — провинции: владелец, оккупация, столица, суша и море, удаление, объединение и разделение, правка областей инструментами карты с уборкой провинций без области; `population.cpp` — население целыми числами, рабы (`trimSlaveWork` — рабы сверх 10 % снимаются с работ), колонизация, пустошь нежити и осквернение.
- `mapobjects.cpp` — знаки и фигуры карты (`ensureMapObjects` — перенос объектов базовой карты в мир при первой правке).
- `military.cpp` — войска и флот на карте: создание, отряды, герои и гарнизоны, перемещение и встречи (`encounter(w, moving, pos)`), союзные войска, разделение (`splitArmy(tx, army, spec)`), битвы, войска без государства (`FactionKind::Wild`, враждебна всем через `World::relation`); `capture.cpp` — штурм гарнизона и захват провинции, освобождение оккупированной (`canLiberate`, `resolveLiberation`, `assaultBlocker`); `heroes.cpp` — судьба героев, воскрешение, пленники (отпустить, казнить, арест); `rebellion.cpp` — мятеж войск и гарнизона, мятежное государство, восстание провинции, переход перед боем.
- `diplomacy.cpp` — отношения (одна религия — смещение +10); `truce.cpp` — перемирие (срок мира, `conquestLoot`) и вассалитет; `trade.cpp` — сделки, дань и репарации (`cancelWarDeals` при войне); `guilds.cpp` — штабы, влияние, государственные гильдии; `routes.cpp` — торговые маршруты.
- `build.cpp` — варианты строительства с ценой и причинами (`buildOptions`), начало, отмена с возвратом, снос, требования и культовые постройки, мгновенное завершение и изначальные постройки, возможности построек (преобразование, генерация эссенции, доступ к особым отрядам, `BuildingFlag`), прерывание цикла с возвратом входа (`refundCycle`), слоты; `plague.cpp` — чума, иммунитет, лечение и заражение постройками.
- `tech.cpp` — изучение общего дерева и деревьев фракций, стоимость, требуемые постройки, лимит одновременных исследований (`researchSlots`, `researchCount`), связи без циклов, `autoLayout`.
- `log.cpp` — хроника; `turn.cpp` — `endTurn(Tx&)` и `previewTurn(const World&)`; итоги хода (`TurnRecord`: строки фракций, записи хроники хода по диапазону ID, восстания, исходы заказов) пишутся в мир — `Meta::lastTurn`; события хода, требующие решения (восстания, мятежи с верностью −100 %), — `TurnReport::events` и `Meta::turnEvents` (разобранные удаляются, неразобранные переживают перезапуск).
- `archaeology.cpp` — археологические группы, поиск и исследование мест, трагедии и бедствия, раскопки, сундуки, тайник реликвий, режим правки, справочники мест и сундуков (`ArchReport` — итог события для окна); таблицы — `core/arch.h`.
- `naval.cpp` — вместимость флота, посадка и высадка (`landingSpot`), перегруз (`fleetOverloaded`), обмен отрядами, возврат группы союзного войска, флот у верфи; `talents.cpp` — классы героев, деревья талантов, уровень героя, лич.
- `plots.cpp` — заговоры: состав и правила игрока, спонсирование и снабжение вооружением, приглашения героев и решения администратора, лидер, заказные убийства (`hitChance`, `resolveHits` в конце хода), контрразведка (обнаружение и раскрытие по этапам, аресты, тюрьмы государства), осуществление (`executePlot`: восстание провинций по приверженности, отряды по снабжению, раскол войск, флота и гарнизонов по доле героев, государство «Заговор …»); `fitPlots` — очистка ссылок после смены владельца провинции, фракции или смерти героя и упразднения государства. Случайность — `Meta::rng` (кнопки контрразведки) и зерно «ход + заговор + герой» (заказы). Готовность игрока к концу хода — `turnReady`/`setTurnReady` (`turn.cpp`); трофеи перемирия при потере всех провинций — `truce.cpp`; множитель производства — `setPbMult`/`maxProductionMult` (`build.cpp`).

### 5.7 core/io

Файлы: `io.cpp` — папка проекта, архив, история ходов, резервные копии, внешние изменения, недавние проекты, автосохранение; `io_json.cpp` — таблица файлов, ссылки, идентификаторы перечислений; `io_encode.cpp` и `io_decode.cpp` — запись и разбор JSON; `io_assemble.cpp` — сборка мира из файлов; `io_normalize*.cpp` — нормализация (разборщик `norm::Norm` в `io_normalize.h`, шаги по областям). Чтение и запись папки проекта (раскладка — FORMAT.md), нормализация (значения по умолчанию, пределы, висячие ссылки → предупреждения), архив `.regnum` (zip той же папки), снимки ходов `history/turn-NNNN-SSSSSS.json.gz` (начало и конец хода, ветви после возврата; новые — в памяти до сохранения), резервные копии `.regnum-backup/` (ручные и автосохранения — разные очереди), обнаружение внешних изменений файлов и архива (по размеру, времени и хешу), список недавних проектов и автосохранение в `userDataDir`. Сохраняются только изменённые таблицы (`Store::dirtyTables`). JSON — с отсортированными ключами и стабильным порядком для аккуратных диффов в Git.

Целостность (аудит 2026-10-23, п.3.1–3.7): `io::save` пишет резервные копии, затем все временные файлы, затем замены (`world.json` — последним) и при неудачной замене возвращает уже заменённые файлы; метка поколения `generation` в каждом файле папки (`io::withoutGeneration` — сравнение без неё). `LoadResult::missing` (пропавшие обязательные файлы), `readOnly` (причина запрета записи: пропавший файл, мир или формат новее программы), `mixed` (файл новее `world.json`); `io::latestBackup`, `io::restoreBackups` — «Восстановить из копии». `io_merge.cpp` — трёхстороннее слияние мира с диском (`io::merge3(base, ours, disk)`: по записям таблиц и справочников, по полям `meta` и `settings`, геометрия — файлом; отчёт о записях, изменённых обеими сторонами).

### 5.8 map

- Карта нарисована кодом. `art.*` — объекты карты (`assets/basemap/map.json`): кольца суши (береговая линия мира) и мелких островков, внутренние воды (реки и озёра — кольца с островами), стены (ломаные с шириной) и знаки (горы двух рисунков, крупные горы, замки, башни — точка привязки, масштаб, вариант) в порядке отрисовки. `art_render.*` — отрисовка: море с мягкой светлой каймой у берега (размытая маска суши), воды, стены, знаки; значки заданы геометрией в коде (замок и башня — прямоугольники штампа, гора — треугольник с шапкой и тенью, параметры `MountainParams` подобраны по штампам исходника: `build-map --fit-icons`), поэтому при масштабе 1 карта повторяет исходник, а при приближении остаётся чёткой. Пространственный индекс выбирает объекты тайла.
- Разбор исходного изображения — отдельный модуль `src/mapbake` (в редактор не входит): `art_extract.*` (этапы разбора), `art_extract_symbols.cpp` (поиск штампов), `art_extract_dev.cpp` (отладочные изображения, `--inventory`, `--fit-icons`), `basemap_segment.cpp`, `basemap_coast.cpp`, `basemap_build.h` (`regnum-cli build-map`): слои воды и краски, береговая линия (marching squares, упрощение с сохранением топологии), изолинии внутренних вод, поиск штампов знаков (целые пиксели, затем доли пикселя и другой размер; бледные и сглаженные замки и башни — по корреляции формы с масштабом по габариту), порядок наложения знаков по пикселям перекрытий, стены по осевым линиям (бледные края найденных замков и башен стенами не считаются). Повторный разбор детерминирован и даёт тот же map.json (тест `mapbake_extract_matches_assets`).
- `basemap.*` — базовая карта во время работы: map.json, береговая линия мира, маска моря и превью с миниатюрой, нарисованные кодом; `objects()` — её знаки и фигуры как записи мира (`art::baseObjects`). `findBasemapDir()`/`findAssetsDir()` — единый поиск папки ресурсов (рядом с программой, затем от текущей папки) для редактора и `regnum-cli`.
- `art_scene.*` — сцена карты мира (`MapView::artScene()`): контуры суши из графа мира (`geo::landRings`), знаки и фигуры из `World::symbols`/`World::shapes` или, пока мир их не хранит, базовой карты; знаки — в порядке `z`; попадание мышью (`symbolAt`, `shapeAt`, `symbolsIn`). Сцена перестраивается (≈ 1 мс), когда меняются объекты карты или берег; перерисовываются только тайлы у изменённых объектов, у берега — с мягким краем моря. Превью и миниатюра карты мира (запасной слой, мини-карта, миниатюра проекта) рисуются по сцене в фоне, когда правка затихнет.
- `mapview.h/.cpp` — камера, композиция кадра, режимы, легенда, мини-карта, попадание мышью. Внутреннее: `tiles.cpp` и `tilestore.cpp` (фоновые тайлы 512 px на текущем масштабе со всем, что под подписями), `pixels.cpp` (наложение, копирование и масштабирование изображений), `style.cpp` (цвета режимов), `labels.cpp` (подписи со спрайтовым кешем), `overlay.cpp` (маршруты, диаграммы гильдий, маркеры, войска, выделение), `marks.cpp` (раскладка отметок войск и флота).
- Палитра карты (`map::Palette`, `art::paletteStyle`, `paletteId`): `Source` — цвета исходника (стиль map.json, чёрная краска знаков; по умолчанию в редакторе), палитры цветокоров (`Sapphire`, `Emerald`, `Crimson`, `Amethyst`, `Obsidian` — тот же id, что у схемы интерфейса) — насыщенное море тона схемы, светлая суша, реки и озёра глубже моря, знаки тёмной тёплой краской (серый рисунка g → `mix(ink, paper, g / 255)`), за краем карты — море темнее. `MapView::setPalette` (по умолчанию `Source`) перерисовывает в фоне тайлы (палитра — часть стиля тайла), превью, миниатюры и мини-карту; легенда и экспорт (`MapExport`, параметр палитры) — в той же палитре. У `Source` фон окна за краем карты — `RenderOptions::background` (фон страниц цветокора).
- Камера: `setSafeArea` — свободная часть окна между панелями (приложение передаёт её каждый кадр). «Показать всю карту» вписывает мир в неё, `minZoom` — масштаб, при котором весь мир виден в ней; смена свободной части (открылся инспектор) камеру не сдвигает.
- Войска и флот не наслаиваются на экране: отметки, которые при текущем масштабе пересеклись бы (фигурка, значок численности, с зазором), сливаются в стопку — верхний объект (выделенный, иначе самый многочисленный), за ним до двух фигурок других объектов цветами их фракций, золотой значок с числом объектов, численность — сумма. Раскладка не зависит от сдвига камеры; при приближении стопка распадается. `armyAt` возвращает верхний объект, `markAt`/`armyMarks` — отметки, `separateZoom` — масштаб, при котором стопка распадётся (щелчок по стопке инструментом «Выбор» приближает к нему, перетаскивание переносит верхний объект).
- Торговый маршрут рисуется ровно по своей ломаной (изломы скруглены обводкой) — по той же линии считаются его провинции и бонус +10 % (`geo::provincesOnPolyline`).
- `export.h/.cpp` — экспорт всей карты в изображение (`MapExport`, `exportMap`): свой `MapView` с кадром ровно по карте (ширина 640–10000, высота по пропорциям 16 : 9), тот же вид, что в редакторе, без выделения, наведения, инструментов и линии края. Тайлы рисуются в фоне, поэтому экспорт идёт шагами (`step()` каждый кадр, пока не вернёт true). Окно экспорта — `app/dialogs/export.cpp` (Ctrl+E): размер, подписи, войска и флот, файл PNG, запись PNG — в `jobs`.
- `demo_world.*` — вымышленный демонстрационный мир для тестов и показа (не канон кампании).
- Режимы «Дипломатия» и «Чума и пустоши» (`MapMode::Diplomacy`, `MapMode::Plague`): `RenderOptions::focusState` и `TileStyle::focus` — государство режима «Дипломатия» (часть ключа стиля тайлов); экспорт карты получает то же государство в фокусе, что на экране.
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
- Фишки с переносом строк — контейнер `ui::ChipFlow` и `ui::flowChip` (ряд `HStack` не переносит).

Реализация: `context.cpp` (кадр, ввод, ID, фокус, состояние), `layout.cpp` (раскладка, панели, карточки, разделы, прокрутка, `ChipFlow`), `widgets.cpp` (текст и подсказки обрезанных подписей), `widgets_controls.cpp` (кнопки, переключатели, ползунок, вкладки), `widgets_display.cpp` (бейджи, фишки, полосы, диаграммы, показатели, аватары, пункты списка), `text.cpp`, `select.cpp`, `table.cpp`, `popup.cpp`, `render.cpp`, `theme.cpp`.

### 5.10 app

Реестры `app.h`: TabReg (вкладки выделения: в панели — значками, на странице — пунктами навигации; `wide` — во всю ширину страницы), HeaderReg (шапка выделения), QuickReg (самое нужное о выделении в нижней строке), SectionReg (разделы на месте карты — верх правой ленты: каталог и страницы сущностей или редакторы группы), DrawerReg (панели справа от карты — низ правой ленты), EditorReg (редакторы на месте карты; `group` — раздел, в навигации которого стоит редактор), BuiltinToolReg (инструменты карты; `ToolReg` заменяет инструмент, `restoreBuiltinTools()` возвращает встроенные после подмены в тестах), CommandReg (команды и сочетания; `order` — порядок в палитре и справке F1), DialogReg (модальные окна по ID). Порядок записей задаёт `order` (у вкладок одного вида выделения и у инструментов он уникален), а не порядок статической инициализации: тот разный на разных ОС и меняется при переносе файлов.

Раскладка `src/app`. В корне — каркас: `app.h`, `app_internal.h` (состояние `App::Impl`, `detail::`, `sessionState<T>()` — состояние окна на сеанс мира), `app.cpp` (экземпляр, настройки app.json, действия с миром), `registry.cpp`, `navigation.cpp` (выделение, что на месте карты, разделы, инструменты и режимы), `app_dialogs.cpp` (окна, уведомления, подтверждения; `App::confirmAct` — опасное действие с подтверждением и отменой Ctrl+Z), `frame.cpp` (события и кадр), `commands.cpp`, `sections.cpp`, `util.cpp`, `flows.h`, `main.cpp` и общие виджеты предметной области `widgets.h` (выбор сущностей, фишки-ссылки, эффекты, ресурсы, реликвии, портреты; `w::frameWorld` — мир кадра, `w::orName`, `w::amountText`/`w::amountUnit` — количество с «тыс.» у золота, `w::legible`, `w::resourceName`). Папки: `shell/` — оболочка основного экрана, экран запуска, палитра команд, настройки (`shell_internal.h`); `project/` — проект, встроенный проводник, общие окна (подтверждение, выбор, запрос строки, сообщение, новый мир); `turn/` — ход, события хода, окна итога и истории ходов (`turn_ui.h` — элементы интерфейса хода и хроники, `history.h` — снимки ходов и возврат); `tools/` — инструменты карты (`edit.h` — общие помощники правки), `tools/map/` — правка карты (`mapedit.h`); `common/` — связь с каноном (`canon.h`) и дерево талантов (`talent_tree.h`); `panels/` — вкладки, шапки и панели (провинция, фракции, войска, персонажи, хроника, торговля, постройки, технологии, маршрут, объекты карты; общие заголовки `province_common.h`, `faction_common.h`, `character_common.h`, `military.h` и `military_internal.h`, `arch_common.h`); `dialogs/` — окна; `editors/` — редакторы на месте карты (`edkit.h` — общие элементы; внутренние заголовки `catalogs_internal.h`, `buildings_internal.h`, `techtree_internal.h`, `modifiers_internal.h`). Функции, общие для нескольких файлов, объявляются только в заголовках — без копий объявлений в .cpp.

Сквозные окна, которые открываются из разных мест, — `app/flows.h` (`flow::`): штурм гарнизона и захват провинции (`dialogs/siege.cpp`, `dialogs/capture.cpp`), действия мятежников после боя (`rebelAftermath`), «Судьба героев» (`dialogs/hero_fate.cpp`), мятеж (`panels/army_inspector.cpp`), перемирие (`dialogs/truce.cpp`), вассалитет после объявления войны (`dialogs/vassal.cpp`), события хода (`app/turn/events.cpp`). Каждое принимает `done` и вызывает его на любом пути (решение, отказ, закрытие), поэтому окна выстраиваются в очередь. Связь с каноном проекта (карточки персонажей и локаций по ID и названию, заметки только для чтения, портрет) — `app/common/canon.h`.

Хранение (аудит 2026-10-23, п.3): `App::Impl::base` — общий предок для слияния (мир, прочитанный или сохранённый последним), `lock` — причина «только чтение» (Ctrl+S открывает «Сохранить как», автосохранение не идёт, статус в верхней строке), `seenExternal` — окно о внешних изменениях уже показано; `project.cpp` — `mergeDisk` (Ctrl+S при изменениях на диске сливает и показывает окно `merge`), окно исправлений `world.fixes` (`detail::showFixes`, «Сохранить исправления»), «Восстановить из копии»; `project/check.cpp` — команда `file.check` «Проверить мир» (только администратору): нормализует копию мира, показывает исправления и ошибки геометрии, «Исправить» — одной транзакцией.

Доработки 2026-10-10: вкладки «Археология» провинции и государства (`panels/province_arch.cpp`, `panels/faction_arch.cpp`, общие слоты — `panels/arch_common.*`, окна итога и «божественного вмешательства» — `dialogs/arch_result.cpp`), справочники мест, сундуков и классов героев (`editors/catalogs_arch.cpp`, `editors/catalogs_classes.cpp`, дерево талантов — `app/common/talent_tree.*`), вкладка героя «Таланты» и лич (`panels/character_talents.cpp`), хранилище реликвий постройки (`panels/relic_store.cpp`), кнопки лечения и заражения (`panels/construction_roles.cpp`), наёмники (`panels/military_merc.cpp`), флот у верфи и войско на борту (`panels/military_naval.cpp`), обмен отрядами (`dialogs/exchange.cpp`), инструмент высадки (`tools/landing.cpp`, `ToolId::Landing`; `ToolDef::hidden/blocked` — инструмент без значка на ленте и недоступный с причиной). Золото подписывается «тыс.» (`fmtGold`, `fmtGoldSigned`, `fmtGoldShort` в `base`).

Доработки 2026-10-17: подклассы в справочнике классов и дереве талантов (`editors/catalogs_classes.cpp`, `common/talent_tree.*` — 5 столбцов, ранги, финальный ярус), подкласс героя (`panels/character_talents.cpp`); реликвии стопками — общие помощники `common/relics.*` (места за один проход, строка стопки «× N», окна «куда и сколько»), инвентари героя и государства, хранилище постройки, «Где лежит» в справочнике; окно рынка «Продать реликвии» (`dialogs/market.cpp`, по имени `"market"`); возможности построек — иссушение, снижение содержания, добыча, рецепты списком, «только для вида государства» (`editors/buildings_roles.cpp`); окно колонизации с переселенцами (`panels/province_overview.cpp`); действующий эффект «Чумы» в подсказках (`rules::provinceModView`, `ModEdit::province`); окно флага с разделами «Темы» и «Расы» (`dialogs/flag_editor.cpp`); плеер саундтреков (`shell/player.cpp`); роль за редактором (`shell/role.*`, ниже).

Режимы правки карты взаимоисключающие: «Правка границ» (`UiState::editBorders`, E; `ToolDef::editMode`) и «Правка карты» (`UiState::editMap`, T; `ToolDef::mapMode`). Инструменты правки карты — `app/tools/map/*.cpp` (объекты, знаки и кисть, озеро, река, стена, суша и море контуром, берег), общие помощники — `app/tools/map/mapedit.h` (`mapedit::act` — перенос объектов в мир и правка одной транзакцией, порядок нового знака по соседям), инспектор — `app/panels/map_inspector.cpp` (`SelType::Symbol`, `SelType::Shape`; несколько знаков — `UiState::symbolGroup`).

Доработки 2026-10-22: вкладка государства «Заговоры» (`panels/faction_plots.cpp`; общие помощники — `panels/plots_common.*`: какие заговоры видит роль, кто действует, `a.play` для внешнего заговора и `a.act` для внутреннего; контрразведка и «Тюрьмы государства» — `panels/plots_counter.cpp`; окна `dialogs/plot_create`, `plot_supply`, `plot_hit`, `plot_execute`, `plot_arrest`, `plot_prison`); раздел правой ленты «Решения» (`panels/decisions.cpp`, только администратору — `SectionDef::adminOnly`, счётчик на значке — `SectionDef::badge`; Ctrl+0): готовность игроков, приглашения героев в заговоры, заказные убийства следующего хода; готовность игрока к концу хода — `App::endTurn` у игрока ставит и снимает `rules::setTurnReady`, вкладка «Готовность» окна подтверждения хода, исходы заказов — в отчёте хода; множитель производства в карточке постройки (`panels/construction.cpp`), выходы рецептов списком (`editors/buildings_roles.cpp`), группа эффектов «Герой» в редакторе модификаторов, дерево технологий с веткой «Стимуляция промышленности» (связи по рядам змейкой, отсечение невидимого, поиск с «следующим совпадением»).

Роль за редактором (ТЗ «Доработки №4», п.5) — `app/shell/role.*`: окно выбора роли при каждом открытии мира (`AppConfig::askRole`, программа `regnum`): «Администратор редактора» — всё без ограничений, «Игрок» — за государство из списка (основные игровые первыми; карта плавно показывает его столицу). Роль — на сеанс (`UiState::role`, `playerState`), в мир не пишется; последняя роль и государство мира — `roles.json` данных пользователя; смена — «Сменить роль…» меню мира и палитры. `App::canEdit()` — правка мира (администратор), `canPlay(faction)` — игровое действие за фракцию, `App::play` / `confirmPlay` — игровые транзакции; `act()` игроку отказывает. Игроку скрыты и недоступны клавишами и палитрой разделы «Справочники» и «Решения» (`SectionDef::adminOnly`), режимы правки границ и карты с их инструментами, «Новое войско» и «Новый флот» (только кнопкой своего государства — в своей провинции и у своего берега), «Торговый маршрут»; история ходов — без возврата; дерево технологий — просмотр и исследование за своё государство; правила мира в настройках — только просмотр. Войска и флот игрока ходят по очку действий (`rules::canPlayerMove`, `moveSpendsAction`, `spendAction`): свои объекты, в соседнюю провинцию — с тратой очка (пока призрак в пути, соседние провинции подсвечены), внутри своей — свободно; посадка, обмен с бортом, бой и штурм — через `role::spendPending` (добавка к игровым транзакциям окна встречи, `role::decorate` в `App::act`), высадка — в инструменте. Объекты без очка приглушены с песочными часами (`map::RenderOptions::dimArmies`), «Ход сделан» — в панели и нижней строке войска.


Ход и события (аудит 2026-10-23, п.2, 8, 11): окно итогов хода строится из `Meta::lastTurn` (`turn/report_dialog.cpp`), `turnui::playerArrival` открывает игроку «Итоги хода N» при первом открытии мира после смены хода (отметка — `turns.json` данных пользователя); `turnui::askReady` и окно `turn.ready` — предпросмотр игрока с «Готов»; `turn/events.cpp` — очередь событий из `Meta::turnEvents` (`flow::processTurnEvents(App&)`, `turnEventsRunning`; поколение окон `detail::dialogEpoch`), разобранное событие удаляется транзакцией без истории. `App::closeDialogs` вызывает `dismissed()` у всех окон и отбрасывает отложенные ими продолжения; `detail::closeDialog(id)` — F1 и Ctrl+K закрывают только своё окно. Вкладки «События» провинции, государства, героя и войска — `turnui::entityEvents` (`panels/events.cpp`), туман войны хроники — `turnui::entryVisible`. Раздел «Мой ход» — `panels/my_turn.cpp`; блоки «Решений», связанные с ходом, — `panels/decisions_turn.cpp` (`decisions.h`: события хода, археологи в опасности, `archDangerBadge`, журнал); журнал действий игроков — `turn/journal.*` (`detail::journalPlay` из `App::play` и подтверждённых действий игрока).

Режим игрока (аудит 2026-10-23, п.1, 5.15, 5.16): пароль роли — `shell/role.*` (`adminKeyOf`, `adminLocked`, `adminPasswordOk`, окно `role.password`, `lockedStart`); вид карты игрока — `UiState::playerView` (ключ `playerView` в app.json; `role::viewSettings`, `setView`, `mapWorld` — карта, история ходов); предложения — `panels/decisions.cpp`, `editors/trade.cpp`, `dialogs/tribute.cpp`, `dialogs/truce.cpp`, `panels/faction_diplomacy.cpp` (окно `dip.war`); вассалитет — `dialogs/vassal.cpp` (цепочка окон, предложения `Defend` и `Call` без записи в отмену); туман войны — `App::knows` и помощники `widgets.*` (`kHidden`, `hiddenMark`, `hiddenStat`, `hiddenFor`, `fogText`); подсказка метки — `w::tagTip`, разбивка к метке — `prov::breakdownOver`.

Модификаторы справочников (аудит 2026-10-23, п.7): раздел «Модификаторы» карточек рас, культур, религий и форм правления — `editors/catalogs.cpp` (`traitMods`), модификаторы героя у реликвий и классов — `editors/catalogs_common.cpp` (`heroModList`: только виды «Герой» и «Любой», без состояний героя); редактор модификаторов показывает только применимые группы эффектов (`editors/modifiers_effects.cpp`, `groupShown`) и где модификатор используется (`editors/modifiers_usage.cpp`).

Экран запуска (новый мир, открыть папку, открыть архив, недавние; `app/shell/start.cpp`) и основной экран (`app/shell/`). Основной экран закреплён по краям окна, плавающих панелей нет: верхняя строка (`shell/bars.cpp`: знак, мир и состояние сохранения, режим карты, параметры инструмента — `tools::OptionsBar` в её середине, в узком окне строкой над картой; плеер саундтреков — `shell/player.cpp`, папка `assets/soundtracks`, настройки — объект `player` в app.json; поиск, отмена, повтор, сохранение, настройки), нижняя строка (выделение и QuickReg, «Скрыть панель», координаты, ход, «Завершить ход»), лента инструментов слева и лента разделов справа (`shell/side.cpp`). Между лентами — карта и закреплённые панели справа от неё (панель выделения и панель ленты; ширина тянется за левый край) или страница на месте карты (`shell/pages.cpp`): навигация слева, содержимое на фоне страницы (колонка до 820 точек по середине или во всю ширину). Что на месте карты — `App::view()`: редактор (`ui.editor`), каталог раздела (`ui.directory`), страница выделения (`ui.page`; государство, гильдия и персонаж открываются страницей сразу, остальное — кнопкой «Развернуть»), иначе карта. `back()` снимает верхний слой (Esc), `toMap()` возвращает карту и выделение на ней, с которого перешли на страницу (`ui.backSel`); раздел ленты открывается вместо текущей страницы (`openSection`), инструмент карты со страницы возвращает карту. Цветокор и цвета карты — настройки программы (`UiState::scheme`, `UiState::schemeMap`; app.json `scheme`, `schemeMap`): по умолчанию «Сапфир» и исходные цвета карты. Диалоги (битва, встреча войск, разделение, штурм, захват, судьба героев, воскрешение, перемирие, вассалитет, дань, выборщики, флаг, итог хода, история ходов, настройки, экспорт карты, проводник файлов), палитра команд Ctrl+K, справка по клавишам F1 — модальные окна.

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

Радиусы: поля 7, карточки 10, всплывающие и модальные окна 14, таблетки — полный; закреплённые строки, ленты, панели и страницы — без скругления, тени и рамки (разделители — линия border). Тени окон: 0 14 36 rgba(0,0,0,.45). Отступы: 4/8/12/16/24/32. Шрифты: интерфейс — системный без засечек (13 px основной, 12 px вторичный, 15/18/24 заголовки); названия государств и заголовки разделов — системный шрифт с засечками (Cambria/Georgia/DejaVu Serif). Значки — линия 1,75 на сетке 24, размер 18–20 px. Фокус клавиатуры — золотое кольцо 2 px.

Принципы: значок вместо слова, подсказка на каждом значке; числа — крупно, подписи — мелко; опасные действия — красным и с подтверждением; пустое состояние — одна строка и кнопка; никаких обучающих и поясняющих надписей в панелях, на экране запуска и в строке состояния (как пользоваться — во всплывающей подсказке значка и в списке сочетаний F1); ни одного «технического» текста для пользователя; всё, что меняет мир, — отменяется Ctrl+Z. Заголовок окна — «Regnum»; название мира и состояние сохранения — в верхней панели.

## 7. Проверки

### 7.1 Запуск

- `./build.sh --test` — все модульные тесты (ядро, геометрия с фаззингом, правила, кодеки, растеризация с эталонными PNG, разбор карты, интерфейс в headless-режиме). Фильтр — подстрока имени теста: в `build.sh` это слова после `--test` (кроме имён целей) и все слова после `--` (`./build.sh --test app_turn_history core_io_` — история ходов и чтение и запись мира).
- `./build.sh test-<модуль> --test` — тесты одного модуля (`test-base`, `test-codec`, `test-gfx`, `test-core`, `test-geo`, `test-rules`, `test-map`, `test-mapbake`, `test-ui`, `test-app`): модуль с зависимостями и его `test_<модуль>_*.cpp` (программа `regnum-test-<модуль>`).
- Программа тестов: `regnum-tests [-j N] [--list] [--out=папка] [фильтр ...]`, запускается из корня репозитория. `-j N` — N процессов (`-j 0` и `build.sh --test` — по числу ядер, `REGNUM_TEST_JOBS=1` — по одному): тесты делятся на 4N частей вперемешку в порядке регистрации, освободившийся процесс берёт следующую часть; журналы частей с временем тестов дольше 300 мс — `<out>/shards/part-*.log`. Тесты скорости (в имени `perf`, `speed` или `bench`) идут после остальных по одному. `--list` — только имена. Артефакты (PNG, временные миры `app-tmp/`) — в `.wmma/regnum-tests/` (другая папка — `--out=` или `REGNUM_TEST_OUT`).
- Переменные окружения: `REGNUM_PERF_STRICT=1` — строгие пороги скорости (иначе с запасом ×4: машина может быть занята); `REGNUM_AUDIT=1` — проверки аудита (`AUDIT_EXPECT`) и находки обходов интерфейса роняют тест, без неё только печатаются; `REGNUM_BENCH=1` — долгие замеры `app_bench_real_world` (мир — `REGNUM_BENCH_WORLD`, по умолчанию `12_Карты/Мир_Меча_и_Магии`) и `app_export_bench_8k`, без неё они пропускаются. Для расследований: `REGNUM_AUDIT_CFG=dl` — только выбранные виды галереи; `REGNUM_FUZZ_EVENTS`, `REGNUM_FUZZ_SEEDS` (зёрна делятся между `app_fuzz_monkey_1..3`), `REGNUM_FUZZ_FOCUS=editor:<id>|tab:<вид>|drawer`, `REGNUM_FUZZ_VERBOSE`, `REGNUM_FUZZ_SLOW_MS` — обезьяний тест; `REGNUM_JSON_FUZZ=N` — правок на файл в `app_fuzz_json_types`; `GEO_FUZZ_SEEDS`, `GEO_FUZZ_STEPS`, `GEO_FUZZ_SEED0`, `GEO_FUZZ_STOP`, `GEO_FUZZ_REASONS`, `GEO_SNAP_SEEDS`, `GEO_SNAP_STEPS` — серии фаззинга геометрии; `REGNUM_ICON_FOCUS` — лист выбранных значков.
- `./build.sh --asan --test [фильтр]` — тесты с AddressSanitizer (использование освобождённой памяти, выход за границы): свои папки `.build/<os>-asan/`, программы не попадают в `bin/`; ошибка памяти останавливает часть прогона с отчётом в её журнале. Замеры времени под ним медленнее порогов — их провалы не показательны. Полезно после больших правок: так найдены висячие ссылки в тестах (`CHECK_EQ` по части временного объекта, цикл по полю временного `shared_ptr`) и в `App::uiRect`.
- `regnum --selftest` — настоящее окно: ~20 кадров с демонстрационным миром и действиями оболочки, код возврата 0/1.
- `./build.sh platform-demo`, затем `platform-demo --selftest` (`--with-clipboard` — с буфером обмена) — проверка оконной платформы синтетическими сообщениями ОС (единственная автоматическая проверка обработки сообщений win32).
- Сборка для Linux и macOS (`./build.sh --target linux-x64|macos-arm64 regnum regnum-cli`) проверяет, что `x11.cpp` и `cocoa.cpp` компилируются.
- `regnum-cli validate <папка мира>` — целостность данных и геометрии; используется в `tools/Проверить_проект.ps1`.

### 7.2 Устройство набора тестов

- Один файл — одна функция модуля: `src/tests/test_<модуль>_<функция>[_<часть>].cpp` (`test_app_turn_history.cpp` — история ходов, `test_rules_build.cpp` — строительство, `test_core_io_map.cpp` — карта в файлах мира). Файлы не называют по заданиям, аудитам и исправлениям: сценарий из аудита или исправления ставится в файл своей функции рядом с похожими (один сценарий, проверенный несколько раз, — подряд). Новый файл — для новой функции; большая функция делится на части (`test_app_faction_flow.cpp`, `test_app_faction_council.cpp`).
- Имя теста — `<модуль>_<функция>_<что проверяется>`, начало имени совпадает с темой файла (`app_turn_history_restart_after_rollback`, `core_io_roundtrip_folder`, `rules_build_lifecycle`): фильтр по началу имени выбирает модуль (`app_`), функцию (`app_turn_`) или файл. Имя говорит, что должно выполняться, а не какой дефект был. Слова `perf`, `speed`, `bench` — только у замеров времени.
- Сценарии интерфейса (`test_app_*.cpp`) — настоящий `app::App` на `headless`-платформе (`apptest::Harness`): обработчики событий, снимки экрана PNG и проверки состояния мира. Снимки только для просмотра глазами — `test_app_gallery.cpp` (`app_gallery_<экран>_<вид>`, виды `d`, `l`, `s`, `z`: цветокор по умолчанию, другой, малое окно, интерфейс 125 %); устойчивость — `test_app_fuzz_sweep.cpp` (обход всех вкладок, панелей, редакторов и окон с журналом предупреждений), `test_app_fuzz_monkey.cpp` (случайные потоки событий), `test_app_fuzz_json.cpp` (испорченный JSON), `test_app_widget_ids.cpp` (уникальность ID виджетов); замеры — `test_app_perf.cpp`, `test_*_perf.cpp`.
- Долгий сценарий делится на независимые тесты с тем же охватом (обход — по видам окна и долям списков сущностей, галерея — по видам): параллельный прогон раскладывает части по разным процессам, и время набора не упирается в один тест. Отдельный тест под `-j 8` — не дольше пары минут.
- Реестры приложения (`TabReg`, `DrawerReg`, `EditorReg`, `ToolReg`...) общие для всей программы тестов: сценарий со своими вкладками или инструментами регистрирует их на время теста и возвращает реестры после (`Fakes` в `test_app_editor.cpp`); остальные сценарии видят реестры как в приложении. Временные папки сценариев (`Harness("имя")`, `apptest::tempDir`) — с уникальными именами: части прогона идут одновременно.
- Помощник, нужный двум файлам, — в общем заголовке, а не копией в файле:
  - `test.h` — `TEST`, `CHECK*`, `errorOf` (текст `UserError`), `envFlag`, `envInt`, `perf` (порог скорости), `auditStrict` и `AUDIT_EXPECT` (ожидаемое поведение, которое пока не выполняется; исправив дефект, `AUDIT_EXPECT` заменяют на `CHECK`).
  - `test_png.h` (base и codec — годится любому модулю) — `savePng` (артефакт из `codec::RgbaImage` или `gfx::Image`), `argbToRgba` (кадр окна, часть, увеличение), `makeRgba`, `pngBytes`, `gemPng` (входные картинки сценариев).
  - `test_world_util.h` (core) — `idByName`, `idWhere`, `idByKey` (справочники и таблицы мира), `has`, `firstOwnedLand`, `stateWithLand`, `twoStates`, `techByName`, `buildingByKey`, `newestDeal`, `hasPrereq`, `siteKey`, `chestKey`, `stockSum`.
  - Модули: `test_codec_util.h` (эталонные данные `src/tests/data`, сравнение изображений), `test_core_io_util.h` (мир со всеми полями, сравнение миров, временные папки, чтение и запись файлов), `test_geo_util.h` (синтетические берега, `initStore`, площади и местность, отказ без изменения мира, граф вручную, проверка инвариантов, отладочные PNG), `test_rules_util.h` (маленький мир `Fix` из восьми провинций, раса и владелец, эффекты и модификаторы, отряды, простая постройка, хроника), `test_gfx_icons_util.h` (подписи, увеличение, покрытие), `test_gfx_text_util.h` (системные шрифты, стиль текста), `test_map_view_util.h` (базовая карта, демонстрационный мир, кадр с тайлами, войско для отметок, PNG кадров и фрагментов), `test_mapbake_util.h` (синтетическая карта), `test_ui_util.h` (кадры без окна, синтетические события, снимки).
  - Приложение: `test_app_util.h` (`Harness`; точки карты на экране, снимки `shotClean` и `cropShot`, поиск фракций и героев, вкладки инспектора, щелчки по частям элементов, ввод в окнах, уведомления `hasToast`, журнал `LogTap`, время кадра `frameMs`) и тематические: `test_app_editors_util.h` (редакторы на месте карты), `test_app_faction_util.h` (страницы государств и гильдий), `test_app_economy_util.h` (провинции и экономика), `test_app_trees_util.h` (деревья технологий и построек, строительство, запасы), `test_app_military_util.h` и `test_app_war_util.h` (войска, флот, война), `test_app_tools_util.h` (инструменты карты).
