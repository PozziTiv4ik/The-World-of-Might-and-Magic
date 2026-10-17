// Regnum — хранилище мира: текущее значение, транзакции, отмена и повтор, подписки на изменения.
#pragma once
#include "core/world.h"

namespace rg {

struct TxOptions {
  std::string coalesce;          // непустой ключ: подряд идущие транзакции объединяются в одну запись отмены
  double coalesceSec = 1.5;
  bool history = true;           // false — не попадает в стек отмены
};

struct Change {
  enum Kind { Commit, Undo, Redo, Load } kind = Commit;
  std::string label;
  u32 tables = 0;                // TableBit
  const World* before = nullptr; // предыдущее состояние (действительно только во время уведомления)
  const World* after = nullptr;
};

class Store {
 public:
  Store();
  const World& world() const { return cur_; }
  u64 version() const { return version_; }

  // Выполнить изменение мира. fn(Tx&) может вернуть значение. Исключение — откат.
  template <class F>
  decltype(auto) transact(std::string_view label, F&& fn, const TxOptions& opt = {}) {
    Tx tx(cur_);
    if constexpr (std::is_void_v<decltype(fn(tx))>) {
      fn(tx);
      commit(label, std::move(tx), opt);
    } else {
      auto result = fn(tx);
      commit(label, std::move(tx), opt);
      return result;
    }
  }

  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  std::string undoLabel() const { return undo_.empty() ? std::string() : undo_.back().label; }
  std::string redoLabel() const { return redo_.empty() ? std::string() : redo_.back().label; }
  bool undo();
  bool redo();
  void endCoalesce();
  void clearHistory();

  // Заменить мир целиком (открытие файла, восстановление снимка). Очищает отмену.
  void replace(World w, std::string_view label, bool markSaved = true);

  void markSaved();
  u32 dirtyTables() const { return World::diff(cur_, saved_); }
  bool dirty() const { return dirtyTables() != 0; }

  // Подписка на изменения. Возвращает номер для отписки.
  int subscribe(std::function<void(const Change&)> fn);
  void unsubscribe(int token);

  size_t limit = 400;

 private:
  void commit(std::string_view label, Tx&& tx, const TxOptions& opt);
  void notify(const Change& c);

  struct Entry { std::string label; World before, after; std::string coalesce; double time = 0; };
  World cur_, saved_;
  std::vector<Entry> undo_, redo_;
  u64 version_ = 1;
  std::vector<std::pair<int, std::function<void(const Change&)>>> subs_;
  int nextSub_ = 1;
};

}  // namespace rg
