// Regnum — хранение мира: папка проекта, архив .regnum, снимки ходов, резервные копии,
// обнаружение внешних изменений, недавние проекты, автосохранение, нормализация.
// Формат файлов — docs/FORMAT.md.
//
//   auto r = io::load("D:/Миры/Арден");            // папка или .regnum; UserError — понятная причина
//   store.replace(r.world, "Открыть");
//   io::save(folder, store.world(), store.dirtyTables() | r.fixedTables, &r.files);
//   if (!io::externalChanges(r.files).empty()) ... // файлы изменены другой программой
//
// Все функции бросают UserError (сообщение по-русски) при ошибках ввода-вывода и разбора.
// Чтение никогда не роняет редактор из-за содержимого: неверные значения исправляются
// нормализацией и попадают в предупреждения.
#pragma once
#include <map>
#include <span>

#include "core/world.h"

namespace rg::io {

constexpr const char* kFormat = "regnum-world";      // world.json: поле format
constexpr const char* kSnapshotFormat = "regnum-snapshot";
constexpr int kVersion = 1;                           // старшая версия формата; более новая — отказ
constexpr int kKeepBackups = 5;                       // версий каждого файла в .regnum-backup
constexpr int kMaxRecent = 12;
constexpr const char* kBundleExt = ".regnum";
constexpr const char* kBackupDir = ".regnum-backup";
// Метка поколения сохранения — член верхнего уровня world.json и каждого файла данных папки мира: файлы, записанные
// одним сохранением, получают одно число (FORMAT.md, раздел 3.1).
constexpr const char* kGenerationKey = "generation";

// ---------------------------------------------------------------- предупреждения
struct Warning {
  std::string file;   // "data/provinces.json"; пусто — проект целиком
  std::string where;  // "p12.garrison[0].row"; пусто — файл целиком
  std::string msg;    // по-русски
  std::string text() const;  // «data/provinces.json: p12.garrison[0].row: …»
};
using Warnings = std::vector<Warning>;

// ---------------------------------------------------------------- файлы проекта
struct ProjectFile {
  const char* path;  // относительный путь в папке проекта
  u32 tables;        // TableBit, хранящиеся в файле
};
// world.json и data/*.json в порядке записи (world.json — последним).
const std::vector<ProjectFile>& projectFiles();
u32 tablesOfFile(std::string_view relPath);          // 0 — не файл таблиц
bool isBundlePath(const std::string& path);           // оканчивается на .regnum (без учёта регистра)
bool isProject(const std::string& path);              // папка с world.json или файл .regnum

// Отпечаток файла для обнаружения внешних изменений.
struct FileStamp {
  bool exists = false;
  u64 size = 0;
  i64 mtime = 0;    // мс с 1970
  u32 crc = 0;      // CRC-32 содержимого
  i64 checked = 0;  // когда снят отпечаток (мс с 1970): время файла ближе 2 с к нему — сверяется и содержимое
  u64 gen = 0;      // метка поколения файла (0 — нет)
};
struct FileState {
  std::string folder;                       // папка проекта, абсолютный путь (пусто — архив, слежение не ведётся)
  std::map<std::string, FileStamp> files;   // относительный путь -> отпечаток
};

struct LoadResult {
  World world;
  Warnings warnings;
  u32 fixedTables = 0;  // таблицы, исправленные при чтении: их стоит переписать при сохранении
  FileState files;      // отпечатки прочитанных файлов (для папки)
  bool bundle = false;  // прочитано из .regnum
  // Мир открывается только для чтения (запись и автосохранение запрещены): причина по-русски, пусто — запись
  // разрешена. Нет обязательного файла данных; формат (младшая версия) или базовые записи (meta.content) новее этой
  // версии программы — запись потеряла бы данные.
  std::string readOnly;
  std::vector<std::string> missing;  // обязательные файлы данных, которых нет (их таблицы пусты)
  std::vector<std::string> mixed;    // файлы новее world.json: сохранение прервалось на середине («смешанное сохранение»)
  u64 generation = 0;                // поколение сохранения в world.json (0 — без метки)
};

// ---------------------------------------------------------------- папка и архив
// Прочитать проект: папку (или её world.json) либо архив .regnum.
LoadResult load(const std::string& path);

struct SaveOptions {
  bool backups = true;            // прежние версии изменённых файлов — в .regnum-backup
  int keepBackups = kKeepBackups;
  bool overwriteExternal = false; // при переданном state: перезаписать файлы, изменённые извне
  // Файлы (относительные пути), версия которых на диске записана автосохранением: их прежние версии
  // кладутся в .regnum-backup/auto/ со своей очередью из keepBackups, не вытесняя копии ручных сохранений.
  std::vector<std::string> autoBackups;
};
struct SaveResult {
  std::vector<std::string> written;    // записанные файлы (относительные пути)
  std::vector<std::string> unchanged;  // требовали записи, но содержимое на диске совпало
  std::vector<std::string> backups;    // созданные резервные копии (относительные пути)
};
// Записать в папку файлы таблиц dirtyTables (TB_ALL — всё) и все отсутствующие на диске файлы.
// Файлы с прежним содержимым (без учёта метки поколения) не переписываются. Остальные пишутся одним целым: сначала
// резервные копии и все временные файлы, затем замены (world.json — последним). Замена не удалась (файл занят
// антивирусом или облаком) — уже заменённые файлы возвращаются к прежним версиям и UserError: мир на диске не
// смешивается. Несколько файлов — новое поколение (метка kGenerationKey; world.json переписывается и без изменений),
// один файл — поколение world.json на диске. С state: файлы, изменённые извне после чтения/сохранения, не
// перезаписываются (UserError), если не задано overwriteExternal; state обновляется.
// Мир записывается как есть: meta.updatedAt ставит вызывающий (транзакцией) перед сохранением.
SaveResult save(const std::string& folder, const World& w, u32 dirtyTables, FileState* state = nullptr,
                const SaveOptions& opt = {});

// Метка поколения в тексте файла мира (член верхнего уровня "generation", как его пишет save): withoutGeneration —
// текст без неё (метки нет — как есть), withGeneration — с меткой на месте по алфавиту ключей (0 — без метки),
// generationIn — её значение (0 — нет).
std::string withoutGeneration(std::string_view text);
std::string withGeneration(std::string_view text, u64 gen);
u64 generationIn(std::string_view text);

// ---------------------------------------------------------------- резервные копии
struct BackupCopy {
  std::string file;   // файл мира (относительный путь)
  std::string copy;   // копия: относительный путь в папке мира (.regnum-backup/...)
  u64 generation = 0; // метка поколения копии (0 — нет)
  i64 mtime = 0;      // время записи копии (мс с 1970)
};
// Последняя целая копия файла мира: самая новая по времени из очередей ручных сохранений и автосохранения, которая
// читается как объект JSON; maxGeneration > 0 — не новее этого поколения. nullopt — такой копии нет.
std::optional<BackupCopy> latestBackup(const std::string& folder, const std::string& relPath, u64 maxGeneration = 0);
// Восстановить файлы мира из последних целых копий (атомарно; нынешняя версия файла, если есть, сначала уходит в
// копии). Возвращает восстановленные файлы; файлы без целой копии — в noCopy.
std::vector<std::string> restoreBackups(const std::string& folder, const std::vector<std::string>& files, u64 maxGeneration = 0,
                                        std::vector<std::string>* noCopy = nullptr);

enum class ChangeKind : u8 { Modified, Added, Removed };
struct ExternalChange {
  std::string file;  // относительный путь
  ChangeKind kind = ChangeKind::Modified;
  u32 tables = 0;
};
// Файлы проекта, изменённые другой программой после чтения/сохранения (по размеру и времени, затем по CRC).
// Файлы с прежним содержимым и новым временем остаются неизменёнными (время в state обновляется).
std::vector<ExternalChange> externalChanges(FileState& state);
// Отпечаток одного файла (архив .regnum): размер, время, CRC-32; файла нет — exists = false.
FileStamp stampFile(const std::string& path);
// Файл изменён другой программой после снятия отпечатка (размер и время, затем CRC; прежнее содержимое
// с новым временем изменением не считается — время в st обновляется).
bool fileChanged(const std::string& path, FileStamp& st);
// Время последнего сохранения мира на диске (meta.updatedAt из world.json папки или архива) без чтения
// остальных файлов; пусто — не прочитано.
std::string savedTime(const std::string& path);

// Архив .regnum: zip с той же раскладкой (world.json, data/, history/). Запись атомарная.
// История ходов берётся из historyFrom (папка или архив); пусто — из прежней версии архива path.
void saveBundle(const std::string& path, const World& w, const std::string& historyFrom = {});
// Папка <-> архив (по расширению dst). Мир читается с нормализацией и записывается заново, история копируется.
// Непустая папка или существующий архив назначения — ошибка, если не overwrite.
void convert(const std::string& src, const std::string& dst, Warnings* warnings = nullptr, bool overwrite = false);

// ---------------------------------------------------------------- снимки ходов
struct SnapshotInfo {
  int turn = 0;
  std::string file;   // "history/turn-0003.json.gz" или "history/turn-0003-000012.json.gz"
  std::string at;     // время создания снимка (ISO)
  std::string label;
  std::string name;   // название мира в момент снимка
  u64 size = 0;       // байт в сжатом виде
  // История с ветвями (редактор): номер снимка уникален в истории мира, снимки не заменяют друг друга.
  u64 seq = 0;        // 0 — снимок без номера (turn-NNNN.json.gz)
  std::string kind;   // kSnapStart, kSnapEnd, kSnapBranch; пусто — снимок без вида
  u64 parent = 0;     // предыдущий снимок той же ветви (seq; 0 — нет)
};
constexpr const char* kSnapStart = "start";    // начало хода (сразу после расчёта прошлого хода или открытия)
constexpr const char* kSnapEnd = "end";        // конец хода — мир перед расчётом
constexpr const char* kSnapBranch = "branch";  // состояние перед возвратом к другому ходу
// Снимки истории (path — папка или архив) по возрастанию хода, затем номера; снимки прежних версий без номера
// (turn-NNNN.json.gz) читаются вместе с новыми.
std::vector<SnapshotInfo> listSnapshots(const std::string& path);

// Снимок для записи: info (turn, seq, kind, parent, at, label, name) и мир, сжатый pack().
struct NewSnapshot {
  SnapshotInfo info;
  std::vector<u8> gz;
};
struct History {
  std::vector<SnapshotInfo> list;  // по возрастанию хода, затем номера
  u64 head = 0;                    // снимок, от которого идёт сохранённый мир (seq; 0 — неизвестно)
};
History listHistory(const std::string& path);
// Добавить снимки с номерами (файлы turn-NNNN-SSSSSS.json.gz; время и подпись — из info) и запомнить head.
void addSnapshots(const std::string& path, const std::vector<NewSnapshot>& snaps, u64 head);
LoadResult loadSnapshotFile(const std::string& path, const std::string& file);
// Архив одной записью: мир, история из historyFrom (папка или архив; пусто — без истории), новые снимки, head.
void saveBundleWith(const std::string& path, const World& w, const std::string& historyFrom,
                    const std::vector<NewSnapshot>& add, u64 head);
// История папки to заменяется историей from (файлы как есть — время, подписи, номера сохраняются);
// from пусто — история папки удаляется.
void replaceHistory(const std::string& to, const std::string& from);

// ---------------------------------------------------------------- мир целиком
// Документ снимка (все таблицы в одном JSON). indent 0 — компактно.
std::string toJson(const World& w, int indent = 0);
LoadResult fromJson(std::string_view text, const std::string& origin = "снимок");
std::vector<u8> pack(const World& w, int level = 6);                                // gzip(toJson)
LoadResult unpack(std::span<const u8> gz, const std::string& origin = "снимок");
// Текст одного файла проекта (как он будет записан, без метки поколения).
std::string fileText(const World& w, std::string_view relPath);

// ---------------------------------------------------------------- слияние (core/io_merge.cpp)
// Трёхстороннее слияние для совместной работы нескольких сессий над одной папкой мира: base — мир на диске при
// открытии или последнем сохранении, ours — текущий мир этой сессии, theirs — мир на диске сейчас. Сравнение — по
// записям файлов мира (как они записываются): запись или поле, изменённые одной стороной, берутся с этой стороны;
// изменённые обеими — наша версия (конфликт в отчёте). Таблицы — по записям (ID), справочники — по записям каждого
// списка, отношения — по парам, константы — по ключу, meta и settings — по полям (счётчики ID — наибольшие, счётчик
// случайных событий — сумма приращений), геометрия и объекты карты — файлом целиком. Наша новая запись с ID, который
// на диске занят другой новой записью, получает новый ID (ссылки на неё в нашем мире переводятся). Слитый мир
// нормализуется.
struct MergeResult {
  World world;
  Warnings taken;      // изменения с диска, вошедшие в мир: файл, запись, что
  Warnings conflicts;  // изменено обеими сторонами — оставлена наша версия
  Warnings renamed;    // наши новые записи с занятым на диске ID — новые ID
  Warnings fixes;      // исправления нормализации слитого мира
  std::vector<std::pair<std::string, std::string>> ids;  // ссылки наших записей до и после перенумерации: {"a7", "a9"}
  u32 theirTables = 0; // таблицы, в которые вошли изменения с диска
};
MergeResult merge3(const World& base, const World& ours, const World& theirs);

// ---------------------------------------------------------------- нормализация
// Привести мир к инвариантам схемы: пределы значений, висячие ссылки (обнуляются или удаляются),
// повторы в списках, штабы (≤ 5, без повторов), сумма влияния ≤ 100 (пропорционально), счётчики ID,
// встроенный ресурс «Золото». Возвращает маску изменённых таблиц; каждое исправление — предупреждение.
u32 normalize(World& w, Warnings& warnings);

// ---------------------------------------------------------------- недавние проекты и автосохранение
// dataDir — папка данных пользователя (пусто — fs::userDataDir()).
struct RecentProject {
  std::string path, name, at;
  bool exists = false;
  bool bundle = false;
};
std::vector<RecentProject> recentProjects(const std::string& dataDir = {});  // новые первыми
void addRecent(const std::string& path, const std::string& name, const std::string& dataDir = {});
void removeRecent(const std::string& path, const std::string& dataDir = {});

struct AutosaveInfo {
  std::string file;     // полный путь архива автосохранения
  std::string project;  // путь проекта (пусто — новый несохранённый мир)
  std::string name;     // название мира
  std::string at;       // время (ISO)
  int turn = 0;
  u64 size = 0;
};
struct Autosave {
  AutosaveInfo info;
  World world;
  Warnings warnings;
  std::string readOnly;  // как LoadResult::readOnly (мир новее этой версии программы)
};
// Автосохранение (для восстановления после сбоя): отдельный слот на каждый проект.
AutosaveInfo writeAutosave(const World& w, const std::string& project = {}, const std::string& dataDir = {});
std::vector<AutosaveInfo> listAutosaves(const std::string& dataDir = {});  // новые первыми
// Автосохранение проекта (пусто — нового несохранённого мира). Нет или повреждено — nullopt (причина в error).
std::optional<Autosave> loadAutosaveFor(const std::string& project, const std::string& dataDir = {},
                                        std::string* error = nullptr);
void clearAutosave(const std::string& project = {}, const std::string& dataDir = {});

}  // namespace rg::io
