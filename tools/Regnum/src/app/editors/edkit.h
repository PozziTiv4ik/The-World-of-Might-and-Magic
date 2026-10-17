// Regnum — общие элементы редакторов справочников, констант и модификаторов (editors/*.cpp). Фишки с переносом
// строк — ui::ChipFlow и ui::flowChip.
#pragma once
#include "app/app.h"

namespace rg::app::edkit {

// Кнопка-значок с выбором из сетки во всплывающем окне. true — выбран другой значок.
bool iconPicker(std::string_view id, std::string& icon, bool allowNone, bool disabled, std::string_view tip);
// Подпись значка из сетки: «Чудо», «Корона»…; пусто — «Без значка», значок не из сетки — «Свой значок».
const char* iconTitle(std::string_view icon);
// Плитка значка цвета сущности в потоке (крупный значок в шапке карточки); значка нет — fallback.
void iconTile(const std::string& icon, Color tint, float size, const char* fallback);
// Строка списка сущности (высота 44): плитка значка цвета сущности, название, подпись, число справа (hint),
// предупреждение. Основа — ui::listItem (наведение, выделение, фокус, подсказка, lastItem для меню). true — щелчок.
bool entityRow(std::string_view title, std::string_view subtitle, const char* icon, Color tint, std::string_view hint, bool selected,
               bool warn, std::string_view tip);
// Перейти к сущности на карте: закрыть редактор и выделить (после кадра).
void goTo(App& a, Selection s);

// Окно «список и подробности»: список слева (27 % ширины, 280…380 точек), подробности справа, между ними — линия.
struct ListDetail {
  RectF list, detail;
};
ListDetail listDetail(RectF area);

// Генерация модификатора за ход подписями: «+10 эссенция смерти», «+25 археологические сокровища» (у шаблона
// встроенного — по умолчанию).
std::vector<std::string> genTexts(const World& w, const Modifier& m);
// Фишки эффектов модификатора и генерации за ход с переносом; без них — «Без эффектов».
void effectChips(const Modifier& m);

}  // namespace rg::app::edkit
