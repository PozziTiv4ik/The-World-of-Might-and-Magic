// Regnum — общие помощники заговоров (ТЗ «Доработки №2», п.1, 3–5, 7): вкладка государства «Заговоры»
// (panels/faction_plots.cpp), контрразведка и тюрьмы государства (panels/plots_counter.cpp), окна заговоров
// (dialogs/plot_*.cpp). Внешний заговор ведёт его заказчик (игровое действие — App::play), внутренний — только
// администратор (App::act).
#pragma once
#include "app/app_internal.h"
#include "app/widgets.h"
#include "rules/plots.h"

namespace rg::app::plotui {

constexpr const char* kTab = "faction.plots";

// Заговоры вкладки государства state по ID: внешние, где оно заказчик; администратору — и внутренние против него.
std::vector<Id> visiblePlots(App& a, const World& w, Id state);
// Выбранный заговор вкладки государства (на сеанс мира); 0 — первый из списка.
Id& selected(Id state);
// Действовать за заговор: внешний — игровое действие заказчика (App::canPlay), внутренний — администратор (canEdit).
bool canManage(App& a, const Plot& p);
// Изменение заговора: внешний — App::play, внутренний — App::act.
bool plotAct(App& a, const Plot& p, std::string_view label, const std::function<void(Tx&)>& fn, const TxOptions& opt = {});
// Опасное действие с подтверждением (App::confirmPlay или confirmAct по виду заговора).
void plotConfirm(App& a, const Plot& p, std::string title, std::string text, std::string ok, std::string label, std::function<void(Tx&)> fn,
                 std::function<void(App&)> then = {});

// Подписи.
std::string heroName(const World& w, Id character);
std::string provName(const World& w, Id province);
std::string raceName(const World& w, Id race);
// Значок отряда восстания по типу.
const char* squadIcon(const rules::PlotSquad& s);
// Почему герой — «значимая цель» заказного убийства (правитель, советник, значимый герой); пусто — нет.
std::string vipReason(const World& w, Id character);
// Этап раскрытия контрразведкой: «провинции», «приверженность», «герои», «значимые герои», «лидер».
const char* stageName(int stage);
// Что сделают «Аресты заговорщиков» на раскрытом этапе заговора: строки (значок, текст) окна подтверждения.
std::vector<std::pair<const char*, std::string>> arrestLines(const World& w, const Plot& p);

// Метка со значком и подсказкой (числа и состояния заговора).
void tagTip(std::string_view text, ui::Tone tone, const char* icon, std::string_view tip);

// Окна (dialogs/plot_*.cpp).
void openCreate(App& a, Id state);              // «Создать заговор»: вид, государство-цель, название
void openSupply(App& a, Id plot, Id province);  // «Снабдить вооружением»
void openHit(App& a, Id plot, Id character);    // «Заказать убийство»
void openExecute(App& a, Id plot);              // «Осуществить заговор»: подтверждение и итог
void openRelease(App& a, Id state);             // «Тюрьмы государства»: освободить заключённых по расам
// «Арестовать заговорщиков» обнаруженного заговора plot (name — как его видит контрразведка): что будет на раскрытом
// этапе, затем аресты и итог уведомлением.
void openArrest(App& a, Id state, Id plot, const std::string& name);

// Контрразведка государства и его тюрьмы (panels/plots_counter.cpp): выделенная зона вкладки.
void counterIntel(App& a, const World& w, const Faction& f);

}  // namespace rg::app::plotui
