// Regnum — реестры расширения (app.h): вкладки, шапки, нижняя строка, разделы, панели, редакторы, инструменты, команды,
// окна. Порядок — по order (равный — в порядке регистрации).
#include "app/app_internal.h"
#include "app/shell/role.h"

namespace rg::app {

// ================================================================ реестры
namespace {
template <class T>
std::vector<T>& reg() {
  static std::vector<T> v;
  return v;
}
template <class T>
void insertSorted(std::vector<T>& v, const T& d) {
  auto it = std::upper_bound(v.begin(), v.end(), d, [](const T& a, const T& b) { return a.order < b.order; });
  v.insert(it, d);
}
}  // namespace

TabReg::TabReg(const TabDef& d) { insertSorted(reg<TabDef>(), d); }
const std::vector<TabDef>& tabs() { return reg<TabDef>(); }

HeaderReg::HeaderReg(const HeaderDef& d) { insertSorted(reg<HeaderDef>(), d); }
const std::vector<HeaderDef>& headers() { return reg<HeaderDef>(); }

QuickReg::QuickReg(const QuickDef& d) { insertSorted(reg<QuickDef>(), d); }
const std::vector<QuickDef>& quicks() { return reg<QuickDef>(); }

DrawerReg::DrawerReg(const DrawerDef& d) { insertSorted(reg<DrawerDef>(), d); }
const std::vector<DrawerDef>& drawers() { return reg<DrawerDef>(); }

SectionReg::SectionReg(const SectionDef& d) {
  auto& v = reg<SectionDef>();
  for (auto it = v.begin(); it != v.end(); ++it)
    if (std::string_view(it->id) == d.id) {
      v.erase(it);
      break;
    }
  insertSorted(v, d);
}
const std::vector<SectionDef>& sections() { return reg<SectionDef>(); }
const SectionDef* findSection(std::string_view id) {
  for (auto& s : reg<SectionDef>())
    if (id == s.id) return &s;
  return nullptr;
}

EditorReg::EditorReg(const EditorDef& d) {
  auto& v = reg<EditorDef>();
  for (auto& e : v)
    if (std::string_view(e.id) == d.id) {
      e = d;
      return;
    }
  v.push_back(d);
}
const EditorDef* findEditor(std::string_view id) {
  for (auto& e : reg<EditorDef>())
    if (id == e.id) return &e;
  return nullptr;
}
const std::vector<EditorDef>& editors() { return reg<EditorDef>(); }

ToolReg::ToolReg(const ToolDef& d) {
  auto& v = reg<ToolDef>();
  // Повторная регистрация того же инструмента заменяет прежнюю (встроенный выбор можно переопределить).
  for (auto it = v.begin(); it != v.end(); ++it)
    if (it->id == d.id) {
      v.erase(it);
      break;
    }
  insertSorted(v, d);
}
namespace {
struct Builtin {
  ToolDef def;
};
}  // namespace
BuiltinToolReg::BuiltinToolReg(const ToolDef& d) {
  reg<Builtin>().push_back({d});
  ToolReg r(d);
}
void restoreBuiltinTools() {
  for (auto& b : reg<Builtin>()) ToolReg r(b.def);
}
const ToolDef* findTool(ToolId id) {
  for (auto& t : reg<ToolDef>())
    if (t.id == id) return &t;
  return nullptr;
}
const std::vector<ToolDef>& toolDefs() { return reg<ToolDef>(); }

CommandReg::CommandReg(const CommandDef& d) {
  auto& v = reg<CommandDef>();
  for (auto it = v.begin(); it != v.end(); ++it)
    if (std::string_view(it->id) == d.id) {
      v.erase(it);
      break;
    }
  insertSorted(v, d);
}
const std::vector<CommandDef>& commands() { return reg<CommandDef>(); }
const CommandDef* findCommand(std::string_view id) {
  for (auto& c : reg<CommandDef>())
    if (id == c.id) return &c;
  return nullptr;
}
bool runCommand(App& a, std::string_view id) {
  const CommandDef* c = findCommand(id);
  if (!c || !c->run) return false;
  if (c->enabled && !c->enabled(a)) return false;
  if (!detail::role::commandAllowed(a, id)) return false;   // игроку — без справочников и режимов правки
  try {
    c->run(a);
  } catch (const std::exception& e) {
    a.error(e);
  }
  return true;
}

DialogReg::DialogReg(const DialogDef& d) {
  auto& v = reg<DialogDef>();
  for (auto& x : v)
    if (std::string_view(x.id) == d.id) {
      x = d;
      return;
    }
  v.push_back(d);
}
const DialogDef* findDialog(std::string_view id) {
  for (auto& d : reg<DialogDef>())
    if (id == d.id) return &d;
  return nullptr;
}

}  // namespace rg::app
