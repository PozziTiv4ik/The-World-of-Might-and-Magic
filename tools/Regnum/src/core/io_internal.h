// Regnum — внутренние объявления core/io (не для других модулей).
#pragma once
#include "base/json.h"
#include "core/io.h"
#include "core/schema.h"

namespace rg::io::detail {

// Файлы проекта (порядок записи: данные, затем world.json).
enum FileId : int {
  F_CATALOGS, F_GEO, F_MAP, F_PROVINCES, F_FACTIONS, F_CHARACTERS, F_RELATIONS, F_MODIFIERS,
  F_BUILDINGS, F_TECHS, F_ARMIES, F_ROUTES, F_DEALS, F_LOG, F_CONSTANTS, F_WORLD, F_COUNT
};
struct FileDef {
  const char* path;  // относительный путь
  const char* key;   // ключ массива записей (пусто — особая структура)
  u32 tables;
  bool optional = false;  // может отсутствовать без предупреждения (мир прежней версии)
};
extern const FileDef kFiles[F_COUNT];
int fileIndex(std::string_view rel);  // -1 — не файл таблиц

// Файл, в котором хранится таблица с битом bit (TableBit).
FileId fileOfTable(u32 bit);

// Прочитанное содержимое файлов до сборки мира: записи таблиц (RG_WORLD_TABLES) — std::vector<Province> provinces; …
struct Parts {
  bool present[F_COUNT] = {};
  Meta meta;
  Settings settings;
  Catalogs catalogs;
  RelMap relations;
  Constants constants;
#define RG_PARTS_TABLE(T, field, ...) std::vector<T> field;
  RG_WORLD_TABLES(RG_PARTS_TABLE)
#undef RG_PARTS_TABLE
};

// Значение JSON одного файла.
json::Value encode(const World& w, FileId f);
// Текст файла: отступ 2, ключи по алфавиту, LF, перевод строки в конце; geo, map и relations — запись на строку.
std::string format(const json::Value& v, FileId f);
// Разобрать значение файла в parts (файлы независимы — можно параллельно). world.json: версия — checkVersion.
void decode(const json::Value& v, FileId f, Parts& parts, Warnings& warns);
// Проверить format/version world.json; чужой формат или более новая версия — UserError.
void checkVersion(const json::Value& world, const std::string& origin, Warnings& warns);
// Собрать мир: ID без значения и повторные ID получают новые, затем нормализация.
// fixed — таблицы, упомянутые в предупреждениях warns (только этого чтения) или изменённые нормализацией.
World assemble(Parts&& parts, Warnings& warns, u32* fixed);

// Разобрать текст файла: ошибка разбора — UserError с именем файла, строкой и столбцом.
json::Value parseFile(std::string_view text, const std::string& origin);

std::string refStr(Seq s, Id id);                 // "p12"; 0 — пусто
std::optional<Id> parseRef(const json::Value& v, Seq s);

// Перечисление в JSON: таблица идентификаторов (schema::EnumInfo) и её длина.
struct Enum {
  const schema::EnumInfo* list;
  int n;
};
template <size_t N> Enum en(const schema::EnumInfo (&a)[N]) { return {a, int(N)}; }
// Перечисления без таблиц в schema (только идентификаторы для JSON).
extern const schema::EnumInfo kTerrainE[3], kEdgeKindE[3], kArmyKindE[2], kDealSideE[2], kDealStatusE[3], kSymbolKindE[4],
    kShapeKindE[4], kRarityE[5];
std::string enumChoices(Enum e);   // «a, b, c» — для сообщений о неизвестном значении

}  // namespace rg::io::detail
