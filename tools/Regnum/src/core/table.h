// Regnum — устойчивая таблица сущностей по ID (Table) и её редактор внутри транзакции (TableEdit).
#pragma once
#include <memory>
#include <unordered_map>

#include "core/types.h"

namespace rg {

// Устойчивая таблица сущностей по ID: блоки по 64 записи, общие между версиями мира.
template <class T>
class Table {
 public:
  static constexpr u32 kBits = 6, kSize = 1u << kBits, kMask = kSize - 1;
  using Chunk = std::array<std::shared_ptr<const T>, kSize>;
  using Chunks = std::vector<std::shared_ptr<const Chunk>>;

  const T* get(Id id) const {
    if (!chunks_ || id == 0) return nullptr;
    u32 ci = id >> kBits;
    if (ci >= chunks_->size()) return nullptr;
    const auto& c = (*chunks_)[ci];
    return c ? (*c)[id & kMask].get() : nullptr;
  }
  bool has(Id id) const { return get(id) != nullptr; }
  u32 size() const { return count_; }
  bool empty() const { return count_ == 0; }
  // Обход по возрастанию ID: f(const T&).
  template <class F> void each(F&& f) const {
    if (!chunks_) return;
    for (const auto& c : *chunks_) {
      if (!c) continue;
      for (const auto& e : *c) if (e) f(*e);
    }
  }
  // Первая по возрастанию ID запись, для которой pred(запись) — true (nullptr — нет), и её ID (0 — нет).
  template <class P> const T* findIf(P&& pred) const {
    if (!chunks_) return nullptr;
    for (const auto& c : *chunks_) {
      if (!c) continue;
      for (const auto& e : *c)
        if (e && pred(*e)) return e.get();
    }
    return nullptr;
  }
  template <class P> Id firstId(P&& pred) const {
    const T* e = findIf(pred);
    return e ? e->id : 0;
  }
  std::vector<Id> ids() const { std::vector<Id> r; r.reserve(count_); each([&](const T& e) { r.push_back(e.id); }); return r; }
  std::vector<const T*> all() const { std::vector<const T*> r; r.reserve(count_); each([&](const T& e) { r.push_back(&e); }); return r; }
  // Тождество версии: таблицы не менялись, если указатели блоков совпадают.
  bool same(const Table& o) const { return chunks_ == o.chunks_; }
  // Блоки, отличающиеся от другой версии (для точечной инвалидации кешей): вызывает f(id) для изменённых/удалённых/новых.
  template <class F> void diff(const Table& o, F&& f) const {
    size_t n = std::max(chunks_ ? chunks_->size() : 0, o.chunks_ ? o.chunks_->size() : 0);
    for (size_t ci = 0; ci < n; ci++) {
      auto a = chunks_ && ci < chunks_->size() ? (*chunks_)[ci] : nullptr;
      auto b = o.chunks_ && ci < o.chunks_->size() ? (*o.chunks_)[ci] : nullptr;
      if (a == b) continue;
      for (u32 k = 0; k < kSize; k++) {
        const T* x = a ? (*a)[k].get() : nullptr;
        const T* y = b ? (*b)[k].get() : nullptr;
        if (x != y) f(Id((ci << kBits) | k));
      }
    }
  }

 private:
  template <class> friend class TableEdit;
  std::shared_ptr<const Chunks> chunks_;
  u32 count_ = 0;
};

// Редактор таблицы внутри транзакции: копирует блоки и сущности при первом изменении.
template <class T>
class TableEdit {
 public:
  using Chunk = typename Table<T>::Chunk;
  using Chunks = typename Table<T>::Chunks;
  static constexpr u32 kBits = Table<T>::kBits, kMask = Table<T>::kMask;

  explicit TableEdit(Table<T>* t) : t_(t) {}

  bool touched() const { return chunks_ != nullptr; }

  T* mut(Id id) {
    const T* cur = t_->get(id);
    if (!cur) return nullptr;
    auto it = owned_.find(id);
    if (it != owned_.end()) return it->second;
    auto p = std::make_shared<T>(*cur);
    T* raw = p.get();
    chunkFor(id)[id & kMask] = std::move(p);
    owned_[id] = raw;
    return raw;
  }

  T& put(T value) {
    Id id = value.id;
    if (id == 0) throw std::logic_error("TableEdit::put: id = 0");
    bool existed = t_->get(id) != nullptr;
    auto p = std::make_shared<T>(std::move(value));
    T* raw = p.get();
    chunkFor(id)[id & kMask] = std::move(p);
    owned_[id] = raw;
    if (!existed) t_->count_++;
    return *raw;
  }

  bool erase(Id id) {
    if (!t_->get(id)) return false;
    chunkFor(id)[id & kMask].reset();
    owned_.erase(id);
    t_->count_--;
    return true;
  }

 private:
  Chunk& chunkFor(Id id) {
    if (!chunks_) {
      chunks_ = t_->chunks_ ? std::make_shared<Chunks>(*t_->chunks_) : std::make_shared<Chunks>();
      t_->chunks_ = chunks_;
    }
    u32 ci = id >> kBits;
    if (ci >= chunks_->size()) chunks_->resize(ci + 1);
    if (ci >= ownedChunks_.size()) ownedChunks_.resize(ci + 1);
    if (!ownedChunks_[ci]) {
      auto c = (*chunks_)[ci] ? std::make_shared<Chunk>(*(*chunks_)[ci]) : std::make_shared<Chunk>();
      ownedChunks_[ci] = c;
      (*chunks_)[ci] = c;
    }
    return *ownedChunks_[ci];
  }

  Table<T>* t_;
  std::shared_ptr<Chunks> chunks_;
  std::vector<std::shared_ptr<Chunk>> ownedChunks_;
  std::unordered_map<Id, T*> owned_;
};

}  // namespace rg
