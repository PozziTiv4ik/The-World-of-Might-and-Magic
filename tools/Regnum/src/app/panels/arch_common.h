// Regnum — общие виджеты археологии (ТЗ «Доработки №2»): слоты археологических мест провинции (крупные — вкладка
// провинции, малые — список провинций государства), ячейка сокровища, меню выбора археологической группы, задания
// групп с окном итога (dialogs/arch_result.cpp). Только для panels/province_arch.cpp, panels/faction_arch.cpp,
// dialogs/arch_result.cpp и editors/catalogs_arch.cpp.
#pragma once
#include "app/app_internal.h"
#include "app/widgets.h"
#include "core/arch.h"

namespace rg::app::archui {

constexpr const char* kTabProvince = "province.arch";
constexpr const char* kTabFaction = "faction.arch";

Color slotColor(int slot);
const char* siteIcon(const World& w, Id site);
std::string siteName(const World& w, Id site);
std::string chestName(const World& w, Id chest);
std::string groupName(const ArchGroup& g);
// Содержимое сундука строками (подсказка ячейки сокровища, справочник).
std::string chestTip(const World& w, Id chest);
// Подсказка слота: вид слота и место, этапы; reveal — с местом закрытого слота (режим правки).
std::string slotTip(const World& w, const ArchSlot& s, int slot, bool reveal);

// Крупный слот: рамка цвета слота; закрыт — «?» и вид слота, открыт — значок и название места, этапы, исследован —
// отметка. hovered — подсветка (щелчок обрабатывает вызывающий).
void drawSlotLarge(const World& w, const ArchSlot& s, int slot, RectF r, bool hovered);
// Малый слот (сторона r.w): «?», значок места, этапы, отметка; active — можно назначить группу.
void drawSlotSmall(const World& w, const ArchSlot& s, int slot, RectF r, bool hovered, bool active);
// Ячейка сокровища под слотом: «??????»; reveal или место исследовано полностью — название сундука.
void treasureCell(const World& w, const ArchSlot& s, int slot, RectF r, bool reveal);

// Меню выбора группы (всплывающее окно id, открывается вызывающим): группы государства с уровнем и шансом (пустая
// chance — без шанса); занятые в этом ходу и раненые — недоступны (причина в подсказке). Возвращает группу, выбранную
// в этом кадре (0 — нет).
Id groupMenu(App& a, const World& w, Id state, std::string_view id, std::string_view header, const std::function<double(Id)>& chance);

// Задания групп: действие мира (отмена Ctrl+Z) и окно итога; группа в смертельной опасности — затем окно спасения.
void discover(App& a, Id state, Id group, Id province);
void explore(App& a, Id state, Id group, Id province, int slot);
void excavate(App& a, Id state, Id group, Id province);
void showReport(App& a, const rules::ArchReport& r);   // dialogs/arch_result.cpp
void showDivine(App& a, Id state, Id group);            // окно спасения группы в смертельной опасности

// Открыть страницу государства на вкладке «Археология».
void openStateArch(App& a, Id state);

}  // namespace rg::app::archui
