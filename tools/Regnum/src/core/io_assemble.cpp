// Regnum — сборка мира из прочитанных файлов: ID без значения и повторные ID получают новые, затем нормализация;
// предупреждения — по файлам в порядке записи.
#include "core/io_internal.h"

namespace rg::io::detail {

// ================================================================ сборка мира
namespace {

template <class T>
void putAll(Tx& tx, std::vector<T>& items, const Table<T>& table, Seq s, FileId f, Meta& meta, Warnings& warns) {
  u32 dataMax = 0;
  for (const T& e : items) dataMax = std::max(dataMax, e.id);
  // Счётчик меньше ID в данных — его поднимет нормализация (с предупреждением); здесь он не трогается.
  const bool seqValid = meta.seq[size_t(s)] >= dataMax;
  u32 maxId = std::max(meta.seq[size_t(s)], dataMax);
  std::vector<size_t> pending;
  for (size_t i = 0; i < items.size(); i++) {
    T& e = items[i];
    if (e.id == 0 || table.has(e.id)) {
      pending.push_back(i);
      continue;
    }
    tx.add(std::move(e));
  }
  if (pending.empty()) return;
  for (size_t i : pending) {
    T& e = items[i];
    Id old = e.id;
    e.id = ++maxId;
    if (old) warns.push_back({kFiles[f].path, refStr(s, old), "повторный ID — запись получила новый ID " + refStr(s, e.id)});
    tx.add(std::move(e));
  }
  if (seqValid) meta.seq[size_t(s)] = maxId;
}

}  // namespace

World assemble(Parts&& p, Warnings& warns, u32* fixed) {
  World base;
  Tx tx(base);
  Meta meta = p.meta;
#define RG_PUT_ALL(T, field, name, Name, bit, seq, ...) putAll(tx, p.field, tx.w().field, seq, fileOfTable(bit), meta, warns);
  RG_WORLD_TABLES(RG_PUT_ALL)
#undef RG_PUT_ALL
  if (meta.mapObjects && !p.present[F_MAP]) {
    warns.push_back({kFiles[F_WORLD].path, "meta.mapObjects", "нет файла data/map.json — показаны объекты базовой карты"});
    meta.mapObjects = false;
  }
  tx.meta() = meta;
  tx.settings() = p.settings;
  tx.catalogs() = std::move(p.catalogs);
  tx.constants() = std::move(p.constants);
  tx.relations() = std::move(p.relations);
  World w = std::move(tx).finish();
  u32 mask = normalize(w, warns);
  for (const Warning& x : warns) mask |= tablesOfFile(x.file);
  if (fixed) *fixed = mask;
  // Предупреждения группируются по файлам (world.json, затем data/* в порядке записи); внутри файла —
  // в порядке появления: разбор, затем нормализация.
  auto order = [](const std::string& file) {
    if (file == kFiles[F_WORLD].path) return -1;
    int i = fileIndex(file);
    return i < 0 ? int(F_COUNT) : i;
  };
  std::stable_sort(warns.begin(), warns.end(), [&](const Warning& a, const Warning& b) { return order(a.file) < order(b.file); });
  return w;
}

}  // namespace rg::io::detail
