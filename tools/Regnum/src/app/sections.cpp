// Regnum — разделы правой ленты из редакторов: «Экономика» (переговоры и торговля, дерево построек) и «Справочники»
// (модификаторы, справочники, глобальные константы). Редактор входит в раздел полем EditorDef::group; раздел
// открывает последний открытый в нём редактор.
#include "app/app.h"

namespace rg::app {
namespace {

SectionReg economy({"economy", "coins", "Экономика", 50, "Ctrl+6"});
SectionReg reference({"reference", "book", "Справочники", 60, "Ctrl+7"});

}  // namespace
}  // namespace rg::app
