// Regnum — войска и флот: общие помощники панелей, диалогов и инструментов (ТЗ 1.c, гарнизоны — 1.a.vi).
//
// Панели: вкладки фракции «Войска» и «Флот» и полноэкранные таблицы «Войска и флот» с ценой найма, ключевым ресурсом,
// эссенциями и особыми отрядами (military_faction.cpp), вкладка провинции «Гарнизон» с героями, верностью и мятежом
// гарнизона (military_garrison.cpp), выдвижная панель «Войска и флот» (military_drawer.cpp), инспектор войска и
// флота с верностью, модификаторами и мятежом (army_inspector.cpp). Значок объекта — портрет главного полководца в
// круге или фигурка (objectBadgeIn). Диалоги: битва
// (dialogs/battle.cpp), встреча войск (dialogs/encounter.cpp), разделение (dialogs/split.cpp), штурм и захват
// (dialogs/siege.cpp, dialogs/capture.cpp), судьба героев (dialogs/hero_fate.cpp). Инструменты: новое войско и флот
// (tools_army.cpp), перетаскивание и штурм в инструменте выбора (tools.cpp).
#pragma once
#include "app/app.h"
#include "app/widgets.h"

namespace rg::app::mil {

// ---------------------------------------------------------------- данные
// Мир для рисования кадра: копия a.world(), живущая до следующего кадра. Указатели на сущности и флаги из неё
// не повиснут, если панель в том же кадре вызовет act() (правки со слиянием отмены освобождают прежний мир),
// а флаг дорисуется в endFrame.
const World& frameWorld(App& a);

// Строка таблицы войск или флота фракции в общем виде.
struct UnitRow {
  Id id = 0;
  std::string name;
  const char* icon = "army";      // значок типа
  const char* typeName = "";      // подпись типа
  int type = 0;                   // UnitType или ShipType
  i64 total = 0;
  double upkeep = 0;              // содержание одного (у элементалей — эссенциями, золото не учитывается)
  Id special = 0;                 // особый отряд справочника (строка армии)
  bool keyMissing = false;        // ключевой ресурс обязателен, но не задан или не подходит типу
  bool elemental = false;         // элементали: содержание эссенциями
  bool merc = false;              // наёмники: найм и содержание только золотом (отметка на плитке)
};
std::vector<UnitRow> unitRows(const World& w, Id faction, bool fleet);
std::optional<UnitRow> unitRow(const World& w, Id faction, Id row, bool fleet);
// Расчёт строки (в поле, резерв, содержание с модификатором). nullptr — нет такой строки.
const rules::RowCalc* rowCalc(const rules::Calc& c, Id faction, Id row, bool fleet);
i64 reserveOf(const World& w, Id faction, Id row, bool fleet);

i64 groupCount(const ArmyGroup& g);
i64 unitCount(const Army& a);
i64 rowCount(const ArmyGroup& g, Id row);
std::vector<Id> factionsIn(const Army& a);
std::string objectCaption(const Army& a);          // «Войско», «Союзный флот»…
const char* objectIcon(const Army& a);             // army / fleet
std::string objectName(const Army& a);             // название или «Без названия»
Id provinceUnder(const World& w, Vec2 p);          // провинция под точкой карты (0 — нет)
Id heroLocation(const World& w, Id character, Id skip = 0);   // объект, который сопровождает персонаж
// Где занят герой (подсказка в выборе героя): «в «Войско»», «гарнизон «Провинция»»; пусто — свободен. skipArmy и
// skipProvince — объект и гарнизон, для которых выбирают (там он не «занят»).
std::string heroBusy(const World& w, Id character, Id skipArmy = 0, Id skipProvince = 0);
// Содержание эссенциями строки армии элементалей за ход: эссенция → всего (с модификатором содержания войск).
std::map<Id, double> essenceUpkeep(const World& w, Id faction, const ArmyRow& r);
std::string fmtCount(i64 n);                       // численность: «12 400»
std::string fmtMoney(double v);                    // золото до тысячных: «1 840», «12,5», «0,125»
// Цвета фигурки объекта: лидер и второй союзник.
Color leaderColor(const World& w, const Army& a);
Color allyColor(const World& w, const Army& a);

// ---------------------------------------------------------------- виджеты
// Фигурка как на карте (жетон с силуэтом) в прямоугольнике r (точки интерфейса).
void figureIn(RectF r, ArmyKind kind, Color c1, bool allied = false, Color c2 = Color(0, 0, 0, 0), bool selected = false);
// Портрет главного полководца (флотоводца) для кружка: задан и уже готов; иначе nullptr.
const gfx::Image* commanderFace(const World& w, const Army& a);
// Значок объекта в r: портрет главного полководца в круге с кольцом цвета фракции (у союзного — половина кольца цветом
// второго союзника), без портрета — фигурка как на карте.
void objectBadgeIn(const World& w, const Army& a, RectF r, bool selected = false);
void figure(const World& w, const Army& a, float size, bool selected = false);   // в потоке: слот size × size (значок объекта)
// Подсказка над прямоугольником без перехвата наведения и щелчка (подпись в ячейке строки таблицы).
void cellTip(std::string_view key, RectF r, std::string_view text);
// Флаг фракции в потоке (с подсказкой-названием).
void factionFlag(const World& w, Id faction, float width, float height = 0);
// Флаг и название фракции (щелчок — инспектор фракции).
void factionLabel(const World& w, Id faction, float flagW = 22);
// Плитка значка типа (отряд или судно).
void typeTile(RectF r, const char* icon, Color tint);
// Строка списка объекта: фигурка, название, подзаголовок, численность. true — щелчок.
bool objectItem(const World& w, const Army& a, bool selected, std::string_view subtitle);
// Две строки текста в ячейке таблицы (основная и мелкая подпись).
void cellLines(RectF r, std::string_view top, std::string_view bottom, ui::Align align, ui::Ink topInk = ui::Ink::Normal,
               ui::Ink bottomInk = ui::Ink::Muted);
// Название в ячейке: в одну строку с подписью или, если не помещается, в две строки по пробелу; обрезанное
// многоточием — подсказка с полным названием (без перехвата щелчка по строке).
void nameCell(RectF r, std::string_view name, std::string_view caption = {});
// Ячейка «отряд»: плитка типа (подсказка — тип; особый отряд — отметка на плитке) и название. hire — таблица найма:
// незаданный ключевой ресурс — красная обводка плитки и «!».
void unitCell(RectF r, const UnitRow& row, bool accent = false, std::string_view caption = {}, bool hire = false);

// ---------------------------------------------------------------- действия и диалоги
// Битва (ТЗ 1.c.iv): attacker пришёл из origin на позицию defender. Мятежники, нападающие на войско прежнего
// государства с отрицательной верностью, сначала переманивают его неверную часть (ТЗ «Мятеж», п.3). done(applied) —
// сразу после решения (перетаскивание); after — после всех окон итога (судьба героев, штурм мятежников).
void openBattle(App& a, Id attacker, Id defender, Vec2 origin, std::function<void(App&, bool applied)> done = {},
                std::function<void(App&)> after = {});
// Окна итога битвы или штурма по очереди: уведомление о трупах, «Судьба героев» уничтоженных объектов (пленившее —
// победитель, захоронение — место боя), штурм или захват победившими мятежниками (rebels; ТЗ «Мятеж», п.2).
void battleAftermath(App& a, const rules::BattleOutcome& out, bool rebels, std::function<void(App&)> then);
// Встреча при перетаскивании: объединение, союз, объявление войны. done(accepted) — после выбора.
void openEncounter(App& a, Id moving, Id target, const rules::Encounter& e, Vec2 origin, std::function<void(App&, bool accepted)> done);
// Текст диалога союза: какие отряды сложатся с плитками своей фракции в цели, какие займут новые плитки.
std::string allianceText(const World& w, const Army& moving, const Army& target);
void openSplit(App& a, Id army);
void askDisband(App& a, Id army);                  // подтверждение, затем расформирование
void dissolveAllied(App& a, Id army);              // распустить союзное войско (флот)
void askRemoveRow(App& a, Id faction, Id row, bool fleet);   // удалить строку таблицы с подтверждением

// Наёмники (ТЗ «Доработки №3», п.12–13; military_merc.cpp): кнопка «Создать войско наемников» с окном (тип, название,
// цена найма, содержание) и лимит «Наёмники X из Y» во вкладке «Войска»; цена найма в карточке строки наёмников.
void mercBar(App& a, Id faction);
void mercHireField(App& a, Id faction, Id row);

// ---------------------------------------------------------------- флот (ТЗ «Доработки №3», п.3, 6; «№4», п.9)
// Обмен отрядами двух объектов одного государства (dialogs/exchange.cpp); done(changed) — после закрытия окна.
void openExchange(App& a, Id first, Id second, std::function<void(App&, bool)> done = {});
// Высадка войска с флота: инструмент карты «Высадка войска» (tools_landing.cpp), Esc — отмена.
void startLanding(App& a, Id fleet);
// Вместимость флота строкой «занято / всего» с полосой (перегруз — красным); used < 0 — войско на борту.
void capacityRow(App& a, Id fleet, i64 used = -1);
// «Поставить флот» (military_naval.cpp): при «Свободном редактировании флотов» — инструмент карты, иначе — выбор
// провинции фракции с верфью у моря (флот появляется у её берега). empty — кнопка пустого списка флотов.
void placeFleetButton(App& a, Id faction, bool empty);

// Инструменты карты «Новое войско» и «Новый флот» (повторная регистрация в тестах).
std::unique_ptr<MapTool> makePlaceTool(ArmyKind kind);
// «Новый флот» недоступен без «Свободного редактирования флотов» (ТЗ «Доработки №3», п.3): причина; пусто — доступен.
std::string fleetToolBlocked(App& a);
// Фракция для новых объектов: последняя выбранная в инструменте (0 — ещё не выбирали).
Id& lastPlaceFaction();
// Выбрать инструмент постановки объекта для фракции.
void startPlacing(App& a, ArmyKind kind, Id faction);

}  // namespace rg::app::mil
