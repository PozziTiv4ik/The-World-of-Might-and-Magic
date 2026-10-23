// Regnum — общие элементы интерфейса хода, истории ходов и хроники: строки хроники, страницы и сводка итога хода,
// мелкие элементы таблиц, окна области (turn/*.cpp, panels/chronicle.cpp, окна торговли). Снимки ходов и возврат —
// turn/history.h.
#pragma once
#include <optional>

#include "app/app.h"

namespace rg::app::turnui {

// ---------------------------------------------------------------- хроника
ui::Tone logTone(LogKind k);              // смысловой тон вида записи
const char* logIcon(LogKind k);
const char* logName(LogKind k);

// Есть ли у записи объект на карте (провинция, войско или фракция).
bool entryFocusable(const World& w, const LogEntry& e);
// Выделить и показать объект записи: провинция → войско → первая фракция.
void focusEntry(App& a, const LogEntry& e);

struct LogRowOpt {
  bool clickable = true;                  // наведение и щелчок (если есть объект)
  bool showTime = true;                   // реальное время записи справа
  bool showTurn = false;                  // «ход N» в строке ссылок
  int maxLines = 4;
};
// Строка записи: значок вида, текст с переносом, ссылки (фракции, провинция), время. true — щелчок.
bool logRow(const World& w, const LogEntry& e, const LogRowOpt& o = {});

// ---------------------------------------------------------------- мелкие элементы
void iconTile(RectF r, const char* icon, ui::Tone tone);        // значок на тонированной подложке
void factionLabel(const World& w, Id faction, float h = 24);    // флаг и название в строке (ячейка таблицы)
ui::Ink deltaInk(double v);
std::string money(double v);                                    // число без лишних знаков
// Слот текущей ячейки таблицы для своего рисования (занимает ячейку).
RectF cellRect(ui::Table& t);
// Полосы дохода (сверху) и расхода (снизу) в прямоугольнике; scale — общий максимум.
void flowBars(RectF r, double income, double expense, double scale);
// Разбор дохода (income) или расхода фракции для подсказки (строки через перевод строки).
std::string flowText(const rules::FactionCalc& fc, bool income);

// Страницы итогов хода (ТЗ «Фиксы», п.16): 0 — основные игровые государства (Faction::mainState), 1 — остальные
// фракции (прочие государства и гильдии); −1 — все.
constexpr int kPageMain = 0, kPageOther = 1, kPageAll = -1;
bool isMainState(const World& w, Id faction);
// Запись хроники — на странице: касается основного государства (страница 0); прочих фракций или ни одной (страница 1).
// Учитываются фракции записи, владелец её провинции и фракции её войска; запись о двух сторонах — на обеих страницах.
bool entryOnPage(const World& w, const LogEntry& e, int page);
// Подписи переключателя страниц с числом фракций: «Основные государства · 2», «Остальные · 9».
std::string pageLabel(int page, size_t count);

// Строки итога хода страницы: государства, затем гильдии, по названию.
std::vector<const rules::TurnFactionLine*> sortedLines(const World& w, const rules::TurnReport& rep, int page = kPageAll);
// Есть ли у строки изменения запасов: ресурсы, эссенции элементов или недостача провизии после хода.
bool hasStockChanges(const rules::TurnFactionLine& l);
// Изменения запасов строкой шириной width (w — мир, в котором есть ресурсы и эссенции строки): «Голод» с недостачей
// провизии, эссенции (значок цвета эссенции), ресурсы (значок цвета ресурса и изменение); что не поместилось —
// «ещё N» с подсказкой. Наведение на значение — название и изменение.
void stockDeltas(const World& w, const rules::TurnFactionLine& l, float width, float height = 24, ui::Font font = ui::Font::Body);

// Записи хроники хода по смыслу (w — мир после хода, где есть записи rep.logIds), только записи страницы page.
// Экономика: «провизия закончилась… — голод» — голод; «недостача провизии покрыта — голод закончился» и
// преобразование ресурсов — события экономики; «не хватает эссенций на содержание элементалей» — эссенции; долг
// казны — долги.
struct TurnDigest {
  std::vector<const LogEntry*> all;                                   // все записи хода, кроме итога
  std::vector<const LogEntry*> builds, techs, deals, economy, other;  // события
  std::vector<const LogEntry*> debts, famine, essences, shortfalls, rebellions, plague, demolished;  // предупреждения
  const LogEntry* summary = nullptr;                                  // итог хода (LogKind::Turn)
  int warnings() const {
    return int(debts.size() + famine.size() + essences.size() + shortfalls.size() + rebellions.size() + plague.size() + demolished.size());
  }
  int events() const { return int(builds.size() + techs.size() + deals.size() + economy.size() + other.size()); }
};
TurnDigest digest(const World& w, const rules::TurnReport& rep, int page = kPageAll);

// ---------------------------------------------------------------- готовность игроков (ТЗ «Доработки №1», п.1)
// Игрок отмечает готовность завершить ход за своё государство (rules::setTurnReady), ход завершает администратор.
// Строки таблицы готовности: государства с отметкой этого хода, затем — приглушённо — основные игровые государства
// без неё; внутри — основные первыми, по названию.
struct ReadyRow {
  Id state = 0;
  bool ready = false, main = false;
};
std::vector<ReadyRow> readiness(const World& w);
int readyCount(const World& w);                                 // государств, отметивших готовность в этом ходу
// Таблица готовности: флаг и название, отметка «Готов» (основные без отметки — приглушённо); height > 0 — своя
// прокрутка. Строки помечаются «<mark>.<государство>».
void readinessTable(App& a, const World& w, std::string_view id, float height, std::string_view mark);
// Отметка-кнопка состояния («Готов», «Защищён»): зелёная подложка и рамка, значок и подпись по ширине подписи;
// key — ID в текущей области, tip — подсказка (с сочетанием s). true — щелчок (снять отметку).
bool statePill(std::string_view key, std::string_view label, const char* icon, std::string_view tip, ui::Shortcut s = {},
               ui::Size size = ui::Size::Normal);
float statePillWidth(std::string_view label, ui::Size size = ui::Size::Normal);
// Игрок: отметить готовность завершить ход за своё государство или снять отметку (игровое действие, отменяется
// Ctrl+Z); короткое уведомление.
void toggleReady(App& a);

// Прокрутка высотой по содержимому прошлого кадра, но не выше maxH (модальные окна).
class FitScroll {
 public:
  FitScroll(std::string_view id, float maxH, float minH = 40);
  ~FitScroll();
  FitScroll(const FitScroll&) = delete;
  FitScroll& operator=(const FitScroll&) = delete;

 private:
  float* h_ = nullptr;
  float y0_ = 0;
  std::optional<ui::Scroll> sc_;
};

// Хроника: открыть панель с фильтром по ходу (0 — без фильтра).
void showChronicle(App& a, int turn);

// Диалоги области.
std::unique_ptr<Dialog> makeConfirm(App& a);
std::unique_ptr<Dialog> makeReport(App& a, const rules::TurnReport& rep);
std::unique_ptr<Dialog> makeHistory(App& a);

}  // namespace rg::app::turnui
