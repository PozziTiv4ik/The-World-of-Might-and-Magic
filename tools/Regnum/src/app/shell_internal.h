// Regnum — общие части оболочки основного экрана (shell*.cpp): размеры закреплённых областей и помощники.
//
// Раскладка: сверху строка мира (знак, мир, режим карты, параметры инструмента, правка, поиск, сохранение),
// снизу строка выделения и хода; слева лента инструментов карты, справа лента разделов. Между лентами — карта и
// закреплённые панели справа от неё (выделение, панель ленты) или страница раздела на месте карты (навигация слева,
// содержимое справа). Плавающих панелей нет: всё прилегает к краям окна и друг к другу.
#pragma once
#include "app/app_internal.h"

namespace rg::app::detail::shell {

constexpr float kTopH = 40;      // верхняя строка
constexpr float kBotH = 36;      // нижняя строка
constexpr float kStripW = 48;    // ленты инструментов (слева) и разделов (справа)
constexpr float kNavW = 220;     // навигация страницы
constexpr float kPageCol = 820;  // колонка содержимого страницы (вкладки без TabDef::wide)

inline App::Impl& D(App& a) { return a.impl(); }

// Закреплённая область: панель без скругления, тени и рамки (перекрывает ввод карты под собой).
ui::PanelOpt docked(float pad = 0);
// Линия-разделитель по краю прямоугольника.
void edgeLine(RectF r, bool left, bool top, bool right, bool bottom);
// Кнопка команды реестра: значок, подсказка с сочетанием, доступность.
bool commandButton(App& a, const char* cmd, const char* icon = nullptr, bool toggled = false, std::string_view tip = {},
                   ui::Size size = ui::Size::Normal);
// Ручка ширины панели у края r (leftEdge — тянут левый край). true — тянут.
bool resizeHandle(const char* id, RectF r, float& width, float minW, float maxW, bool leftEdge);
// Пункт навигации: значок, подпись, справа подсказка (клавиша, число) или бейдж; выбранный — золотая полоска слева.
bool navItem(std::string_view label, const char* icon, bool active, std::string_view hint = {}, int badge = 0, std::string_view tooltip = {});
// Подпись группы навигации (мелко, прописными).
void navCaption(std::string_view text);

// ---- shell_bars.cpp
void topBar(App& a, RectF r);
void bottomBar(App& a, RectF r);
// ---- shell_side.cpp
void toolStrip(App& a, RectF r);
void sectionStrip(App& a, RectF r);
void inspectorPanel(App& a, RectF r, float maxW);
void drawerPanel(App& a, const DrawerDef& dr, RectF r, float maxW);
void minimapView(App& a, RectF r);
// ---- shell_pages.cpp
void pageHost(App& a, RectF r);
const char* pageTitle(App& a);   // заголовок страницы для верхней строки (раздел, редактор, сущность)
const SectionDef* activeSection(App& a);   // раздел ленты, страница которого сейчас на экране (nullptr — карта)

}  // namespace rg::app::detail::shell
