// Regnum — вкладки «Войска» и «Флот» фракции и полноэкранные таблицы (military_faction.cpp, military_rows.cpp,
// military_price.cpp, military_table.cpp, military_card.cpp): состояние вкладки, столбцы, общие помощники частей.
#pragma once
#include "app/panels/military.h"

namespace rg::app::mil::forces {

constexpr float kWide = 640;   // ширина, начиная с которой таблица правится в ячейках
// Столбцы широкой таблицы (точки): обязательные и те, что показываются, пока наименованию хватает kNameMin.
constexpr float kNameMin = 240, kTypeIcon = 48, kTypeLabel = 196, kRaceW = 150, kKeyIcon = 64, kKeyFull = 180, kCostW = 156,
                kTotalW = 92, kFieldW = 80, kReserveW = 84, kUpkeepW = 104, kUpkeepTotalW = 96, kActionsW = 104;

struct ForcesState {
  Id selRow = 0;
  Id shownRow = 0;            // строка, карточка которой уже показана (новую — прокрутить в видимую часть)
  bool editReserve = false;   // «Правка резерва» (п.10.3): общая численность правится напрямую
};

// Число во всплывающих окнах «Сформировать» и «Распустить» (у каждой строки своё).
struct CountState {
  i64 count = 0;
  bool init = false;
};

// Добавление ресурса или эссенции на юнит (всплывающее окно у карточки строки).
struct AddPart {
  Id pick = 0;
  double amount = 1;
};


// ---------------------------------------------------------------- подписи и правка строк (military_rows.cpp)
const char* noun(bool fleet);        // «Судно» / «Отряд»
const char* fieldWord(bool fleet);   // «в море» / «в поле»
const ArmyRow* armyRowOf(const World& w, Id fid, Id row);
const FleetRow* fleetRowOf(const World& w, Id fid, Id row);
std::string rowRace(const World& w, Id fid, Id row);   // раса строки армии (пусто — не строка армии)
Color legible(Color c);              // значок ресурса или эссенции, различимый на тёмном фоне (порог 0,12)
void renameRow(App& a, Id fid, Id row, bool fleet, const std::string& name);
// Тип строки: войска — по правилам (имя и раса по умолчанию следуют за типом, неподходящий ключевой ресурс снимается),
// судно — имя по типу следует за типом.
void setRowType(App& a, Id fid, Id row, bool fleet, int type);
void setRowUpkeep(App& a, Id fid, Id row, bool fleet, double v);
void setTotal(App& a, Id fid, Id row, i64 v);
// Почему особый отряд недоступен: какая постройка доступа нужна.
std::string specialNeed(const World& w, Id special);
// Меню выбора типа для новой строки (ui::openPopup(id)): все типы и особые отряды справочника.
void addRowMenu(App& a, const char* id, Id fid, bool fleet, ForcesState& st);
void raceCombo(App& a, std::string_view id, Id fid, Id row, bool disabled);
void keyCombo(App& a, std::string_view id, Id fid, const ArmyRow& r, bool disabled);          // ключевой ресурс строки армии
void fleetKeyCombo(App& a, std::string_view id, Id fid, const FleetRow& r, bool disabled);    // ключевой ресурс судна

// ---------------------------------------------------------------- цена, найм и роспуск (military_price.cpp)
struct Part {
  enum Kind : u8 { People, Res, Ess };
  const char* icon = "resource";
  Color color{0, 0, 0, 0};   // a = 0 — приглушённый цвет темы
  std::string name;
  double v = 0;
  Kind kind = Res;
  Id id = 0;
};
// Цена найма: население, ключевой ресурс строки (первым), прочие ресурсы (трупы, энергия, дополнительные), эссенции.
std::vector<Part> costParts(const World& w, const rules::RecruitCost& c, Id key = 0);
std::vector<Part> essenceParts(const World& w, const std::map<Id, double>& m);
// Части в ячейке таблицы: значок и число подряд; не поместилось — «+N»; полный список — в подсказке.
void partsCell(std::string_view key, RectF r, const std::vector<Part>& parts, std::string_view title, ui::Align align = ui::Align::Left);
void recruitPopup(App& a, const char* id, Id fid, const UnitRow& row, bool fleet);                 // «Сформировать»
void disbandPopup(App& a, const char* id, Id fid, const UnitRow& row, bool fleet, i64 reserve);    // «Распустить»
void cancelForming(App& a, Id fid, int index, Id row, i64 count);
// Цена и содержание строки армии в карточке (ключевой ресурс, если не в таблице; ресурсы и эссенции на юнит).
// Ключевой ресурс — игровое действие за фракцию, ресурсы и эссенции на юнит — правка мира.
void armyPrice(App& a, Id fid, const ArmyRow& r, bool keyInTable);

// ---------------------------------------------------------------- показатели и таблица (military_table.cpp)
// Столбцы, показанные широкой таблицей (их правка — в ячейках, а не в карточке).
struct Shown {
  bool race = false, key = false;
};
void statTiles(App& a, Id fid, bool fleet, bool wide);
Shown forcesTable(App& a, Id fid, bool fleet, ForcesState& st, bool wide);
void typeCombo(std::string_view id, int& type, bool fleet, bool disabled);

// ---------------------------------------------------------------- карточка строки (military_card.cpp)
bool rowCard(App& a, Id fid, bool fleet, ForcesState& st, bool editors, Shown inTable);
void formingList(App& a, Id fid, bool fleet);

}  // namespace rg::app::mil::forces
