// Regnum — общие элементы интерфейса хода, истории ходов и хроники: строки хроники и туман войны в ней, вкладка
// «События» сущностей, страницы и сводка итога хода, готовность игроков, итоги хода игроку, мелкие элементы таблиц,
// окна области (turn/*.cpp, panels/chronicle.cpp, panels/events.cpp, окна торговли). Снимки ходов и возврат —
// turn/history.h; журнал действий игроков — turn/journal.h.
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

// Участники записи: её фракции, владелец её провинции и фракции групп её войска (без повторов, по порядку).
std::vector<Id> entryParties(const World& w, const LogEntry& e);
// Запись касается государства state: оно среди участников.
bool entryConcerns(const World& w, const LogEntry& e, Id state);
// Туман войны (App::knows): администратору видны все записи; игроку — общие (без участников) и записи с участием
// известных ему государств (своё, союзники, сюзерен, вассалы).
bool entryVisible(const App& a, const World& w, const LogEntry& e);
// Имя упомянуто в тексте словом целиком (без учёта регистра, ё = е): записи о герое.
bool mentions(std::string_view text, std::string_view name);

// Вкладка «События» провинции, государства, героя и войска: записи хроники, которые отбирает match и видит роль, по
// ходам — новые сверху, строками logRow (щелчок — выделить и показать объект записи); пусто — одна строка. Строки
// помечаются «<mark>.<запись>», кнопка «Показать ещё» — «<mark>.more». Отбор кешируется по mark и salt (salt —
// то, от чего ещё зависит match: например, имя героя).
void entityEvents(App& a, const World& w, const std::function<bool(const LogEntry&)>& match, std::string_view mark, u64 salt = 0);

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

// Строки итога хода страницы: государства, затем гильдии, по названию; state ≠ 0 — только строка этого государства.
std::vector<const TurnFactionLine*> sortedLines(const World& w, const TurnRecord& rep, int page = kPageAll, Id state = 0);
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
// state ≠ 0 — только записи, которые касаются этого государства (страница не учитывается).
TurnDigest digest(const World& w, const TurnRecord& rep, int page = kPageAll, Id state = 0);

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
// Основные игровые государства: всего и отметивших готовность в этом ходу («Готовность 2/5»).
struct MainReady {
  int ready = 0, total = 0;
  bool all() const { return ready >= total; }
};
MainReady mainReady(const World& w);
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
// Игрок нажал «Завершить ход»: ещё не готов — предпросмотр хода своего государства (окно «turn.ready» с кнопкой
// «Готов»); уже готов — отметка снимается сразу.
void askReady(App& a);

// ---------------------------------------------------------------- итоги хода игроку
// Каждый кадр экрана мира: игрок впервые видит мир после смены хода (последний виденный ход его государства в этом мире
// — turns.json данных пользователя) — окно «Итоги хода N» по его государству (Meta::lastTurn).
void playerArrival(App& a);
// Последний ход мира path, итоги которого видел игрок за state (0 — не видел), и запись отметки.
int seenTurn(const App& a, const std::string& path, Id state);
void markSeen(const App& a, const std::string& path, Id state, int turn);

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
// Игрок: предпросмотр хода только со строкой своего государства и кнопкой «Готов» (окно «turn.ready»).
std::unique_ptr<Dialog> makeReadyPreview(App& a);
// Итоги хода (окно «turn.report»); state ≠ 0 — по государству игрока: его казна и запасы, записи хода о нём.
std::unique_ptr<Dialog> makeReport(App& a, const TurnRecord& rep, Id state = 0);
std::unique_ptr<Dialog> makeHistory(App& a);

}  // namespace rg::app::turnui
