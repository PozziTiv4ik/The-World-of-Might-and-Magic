// Regnum — разделы правой ленты из редакторов: «Экономика» (переговоры и торговля, дерево построек) и «Справочники»
// (модификаторы, справочники, глобальные константы; только администратору). Редактор входит в раздел полем
// EditorDef::group; раздел открывает последний открытый в нём редактор.
#include "app/app.h"

namespace rg::app {
namespace {

SectionReg economy({.id = "economy", .icon = "coins", .title = "Экономика", .order = 50, .shortcut = "Ctrl+6"});
SectionReg reference({.id = "reference", .icon = "book", .title = "Справочники", .order = 60, .shortcut = "Ctrl+7", .adminOnly = true});

}  // namespace
}  // namespace rg::app
