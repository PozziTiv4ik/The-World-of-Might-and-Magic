// Regnum — значки интерфейса: реестр имя → векторный глиф на сетке 24 × 24 (механизм глифов — gfx/glyph.h).
#pragma once
#include "gfx/glyph.h"

namespace rg::gfx {

// ---------------------------------------------------------------- значки по имени
bool hasIcon(std::string_view name);
// Нарисовать значок по центру rect (квадрат со стороной min(w, h)). Неизвестное имя — заметный значок «?»
// и одна запись в журнал на имя.
void drawIcon(Canvas& c, std::string_view name, RectF rect, Color color, float strokeScale = 1);
// Все имена в порядке определения (общие, предметные, величины, постройки, отряды, корабли, инструменты),
// затем фигурки карты fig-army, fig-fleet, fig-allied-army, fig-allied-fleet (gfx/figures.h; цвет значка —
// цвет фракции, союзник — его осветлённый вариант). «ruler» — правитель (трон), «measure» — линейка-измеритель.
const std::vector<std::string>& iconNames();

// Глиф значка из реестра (nullptr — нет такого имени).
const VecGlyph* iconGlyph(std::string_view name);

// Проблемы реестра (ошибки разбора путей, пустые слои, выход за сетку) — пусто, если всё в порядке.
std::vector<std::string> iconRegistryIssues();

}  // namespace rg::gfx
