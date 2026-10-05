// Regnum — фигурки и метки карты: войско, флот, столица, штаб гильдии, битва.
//
// Фигурка войска и флота — светлый силуэт без постамента (ТЗ «Фиксы», п.3): воин со щитом, гребнем и флажком
// цвета фракции; галеон с парусами цвета фракции над волной. Выделенная фигурка — в золотом кольце.
// center — точка карты (центр фигурки), size — диаметр описанного круга в единицах холста; наконечник копья
// и флажок мачты немного выступают за него, тень — под ногами (см. figureBounds).
// Готовые изображения кешируются по (вид, размер в пикселях устройства, цвета, флаги, доля пикселя).
#pragma once
#include "gfx/canvas.h"

namespace rg::gfx {

void drawArmyFigure(Canvas& c, Pt center, float size, Color faction, bool selected = false, bool allied = false,
                    Color ally2 = Color(0, 0, 0, 0));
void drawFleetFigure(Canvas& c, Pt center, float size, Color faction, bool selected = false, bool allied = false,
                     Color ally2 = Color(0, 0, 0, 0));
// Столица: круглый знак цвета фракции со светлой звездой под короной. size — диаметр знака.
void drawCapitalMarker(Canvas& c, Pt center, float size, Color faction);
// Штаб гильдии: ромб цвета гильдии со светлым зданием с колоннами. size — сторона описанного квадрата.
void drawHqMarker(Canvas& c, Pt center, float size, Color guild);
// Битва: скрещённые мечи на огненной звезде. size — диаметр звезды.
void drawBattleMarker(Canvas& c, Pt center, float size);

// Габариты фигурки (с наконечником, флажком, ореолом выбора и тенью) — для попадания мышью и перерисовки.
RectF figureBounds(Pt center, float size);
// Габариты знаков столицы, штаба и битвы.
RectF markerBounds(Pt center, float size);

// Кеш готовых фигурок (потокобезопасно).
void clearFigureCache();
size_t figureCacheSize();

}  // namespace rg::gfx
