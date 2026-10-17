// Regnum — файлы проекта и общее для JSON: таблица файлов, идентификаторы перечислений без таблиц schema, ссылки
// вида «p12». Запись — core/io_encode.cpp, чтение — core/io_decode.cpp, сборка мира — core/io_assemble.cpp.
#include <charconv>

#include "core/io_internal.h"

namespace rg::io::detail {

using json::Value;
using schema::EnumInfo;

const FileDef kFiles[F_COUNT] = {
  {"data/catalogs.json", "", TB_CATALOGS},
  {"data/geo.json", "", TB_GEO},
  {"data/map.json", "", TB_MAPART, true},
  {"data/provinces.json", "provinces", TB_PROVINCES},
  {"data/factions.json", "factions", TB_FACTIONS},
  {"data/characters.json", "characters", TB_CHARACTERS},
  {"data/relations.json", "relations", TB_RELATIONS},
  {"data/modifiers.json", "modifiers", TB_MODIFIERS},
  {"data/buildings.json", "buildings", TB_BUILDINGS},
  {"data/techs.json", "techs", TB_TECHS},
  {"data/armies.json", "armies", TB_ARMIES},
  {"data/routes.json", "routes", TB_ROUTES},
  {"data/deals.json", "deals", TB_DEALS},
  {"data/log.json", "log", TB_LOG},
  {"data/constants.json", "constants", TB_CONSTANTS, true},
  {"world.json", "", TB_META | TB_SETTINGS},
};

int fileIndex(std::string_view rel) {
  for (int i = 0; i < F_COUNT; i++)
    if (rel == kFiles[i].path) return i;
  return -1;
}

FileId fileOfTable(u32 bit) {
  for (int i = 0; i < F_COUNT; i++)
    if (kFiles[i].tables & bit) return FileId(i);
  return F_WORLD;
}

// ================================================================ перечисления
const EnumInfo kTerrainE[3] = {{"none", "", ""}, {"land", "", ""}, {"sea", "", ""}};
const EnumInfo kEdgeKindE[3] = {{"border", "", ""}, {"coast", "", ""}, {"frame", "", ""}};
const EnumInfo kArmyKindE[2] = {{"army", "", ""}, {"fleet", "", ""}};
const EnumInfo kDealSideE[2] = {{"a", "", ""}, {"b", "", ""}};
const EnumInfo kDealStatusE[3] = {{"active", "", ""}, {"done", "", ""}, {"cancelled", "", ""}};
const EnumInfo kSymbolKindE[4] = {{"mountain", "", ""}, {"peak", "", ""}, {"castle", "", ""}, {"tower", "", ""}};
const EnumInfo kShapeKindE[4] = {{"water", "", ""}, {"islet", "", ""}, {"wall", "", ""}, {"river", "", ""}};
const EnumInfo kRarityE[5] = {{"common", "", ""}, {"rare", "", ""}, {"epic", "", ""}, {"legendary", "", ""}, {"epochal", "", ""}};

std::string enumChoices(Enum e) {
  std::string s;
  for (int i = 0; i < e.n; i++) {
    if (i) s += ", ";
    s += e.list[i].id;
  }
  return s;
}

// ================================================================ ссылки
std::string refStr(Seq s, Id id) {
  if (!id) return {};
  std::string r = schema::idPrefix(s);
  char buf[16];
  auto res = std::to_chars(buf, buf + sizeof buf, id);
  r.append(buf, size_t(res.ptr - buf));
  return r;
}

std::optional<Id> parseRef(const Value& v, Seq s) {
  if (v.isNull()) return Id(0);
  if (v.isNum()) {
    double d = v.asNum();
    if (d >= 0 && d <= 4294967295.0 && d == std::floor(d)) return Id(d);
    return std::nullopt;
  }
  if (!v.isStr()) return std::nullopt;
  std::string_view t = v.asStr();
  if (t.empty()) return Id(0);
  std::string_view p = schema::idPrefix(s);
  if (!p.empty() && t.size() > p.size() && t.substr(0, p.size()) == p && t[p.size()] >= '0' && t[p.size()] <= '9')
    t.remove_prefix(p.size());
  if (t.empty() || t[0] < '0' || t[0] > '9') return std::nullopt;
  u64 n = 0;
  auto r = std::from_chars(t.data(), t.data() + t.size(), n);
  if (r.ec != std::errc() || r.ptr != t.data() + t.size() || n > 0xFFFFFFFFull) return std::nullopt;
  return Id(n);
}

}  // namespace rg::io::detail
