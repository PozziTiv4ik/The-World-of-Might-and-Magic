// Regnum — правила: заговоры (ТЗ «Доработки №2», rules/plots.cpp): состав заговора, приверженность провинций и
// снабжение вооружением, приглашения героев и решения администратора, лидер, заказные убийства угроз, контрразведка
// цели (обнаружение, поэтапное раскрытие, аресты, тюрьмы государства), осуществление заговора — восстание провинций и
// раскол войск с государством «Заговор …». Общий заголовок правил — rules/rules.h.
#pragma once
#include <map>

#include "core/world.h"

namespace rg::rules {

// ================================================================ состав
// Заговор, в котором провинция (0 — ни в каком).
Id plotOfProvince(const World& w, Id province);
// Заговор, в котором герой — участник, приглашённый или угроза (0 — ни в каком).
Id plotOfHero(const World& w, Id character);
// Почему провинцию нельзя взять в заговор plot (пусто — можно): провинция цели, сухопутная, не в другом заговоре, без
// «Раскрытого заговора»; player — правила игрока: довольство населения ниже 0.
std::string plotProvinceProblem(const World& w, Id plot, Id province, bool player);
// Почему героя нельзя взять в заговор plot (пусто — можно): герой цели, жив, не в плену и не под арестом, не в другом
// заговоре; player — правила игрока: с «Недовольством правителем».
std::string plotHeroProblem(const World& w, Id plot, Id character, bool player);

// Новый заговор: внешний — заказчик owner против target, внутренний — против target без заказчика (только
// администратор). Возвращает ID.
Id createPlot(Tx& tx, PlotKind kind, Id owner, Id target, const std::string& name = {});
void removePlot(Tx& tx, Id plot);
void renamePlot(Tx& tx, Id plot, const std::string& name);
std::string plotTitle(const World& w, const Plot& p);   // название или «Заговор против …»
void addPlotProvince(Tx& tx, Id plot, Id province, bool player);
void removePlotProvince(Tx& tx, Id plot, Id province);  // снабжение провинции теряется
// Администратор: приверженность заговору провинции 0…100 %.
void setPlotCommitment(Tx& tx, Id plot, Id province, double pct);
// «Спонсировать заговор»: заказчик платит 20 % текущей ценности провинции золотом, приверженность +10 % (раз в ход;
// не при «Арестах заговорщиков»). Внутренний заговор — без оплаты.
double sponsorCost(const World& w, Id province);
std::string sponsorProblem(const World& w, Id plot, Id province);   // пусто — можно
void sponsorPlot(Tx& tx, Id plot, Id province);
// Ресурсы, которыми можно «Снабдить вооружением» (в порядке приоритета отрядов), — записи справочника.
std::vector<Id> plotSupplyResources(const World& w);
// «Снабдить вооружением»: ресурсы со склада заказчика (внутренний заговор — без списания) переходят заговорщикам
// провинции; add — сколько добавить (> 0).
void supplyPlot(Tx& tx, Id plot, Id province, const std::map<Id, double>& add);
// Герой в заговор: администратор — сразу участник; игрок — «приглашён в заговор» (решает администратор).
void addPlotHero(Tx& tx, Id plot, Id character, bool player);
// Убрать героя из заговора (участник, приглашённый или угроза; заказ убийства снимается с возвратом золота).
void removePlotHero(Tx& tx, Id plot, Id character);
// Решение администратора о приглашении: согласился — участник, отказался — «Угрозы заговору».
void answerPlotInvite(Tx& tx, Id plot, Id character, bool agree);
void setPlotLeader(Tx& tx, Id plot, Id character);      // участник заговора; 0 — снять

// ================================================================ заказные убийства
// Значимая цель: правитель или советник своего государства, «Значимый герой фракции» — дороже (500–25000, 1 % за 500).
bool hitVip(const World& w, Id character);
struct HitRange {
  double min = 0, max = 0, perPct = 1;
};
HitRange hitRange(const World& w, Id character);
// Вероятность успеха заказа на gold золота: база (1 % за perPct), «Штаб Разведки» заказчика (внешний заговор),
// защита государства героя («Штаб Контрразведки»), модификаторы героя; «Защитить от убийства» — 0 %.
struct HitChance {
  double base = 0, intel = 0, guard = 0, hero = 0, total = 0;
  bool guarded = false;
};
HitChance hitChance(const World& w, Id plot, Id character, double gold);
// «Заказать убийство» угрозы заговору: заказчик платит gold (внутренний заговор — без оплаты); исполнение — в конце
// хода. Повторный заказ того же героя в этом ходу заменяет прежний (разница — доплата или возврат).
void orderHit(Tx& tx, Id plot, Id character, double gold);
void cancelHit(Tx& tx, Id plot, Id character);           // золото возвращается заказчику
void guardHit(Tx& tx, Id plot, Id character, bool on);   // администратор: «Защитить от убийства»
// Администратор рассмотрел заказ (исход не меняется; рассмотренный заказ не ждёт решения).
void setHitReviewed(Tx& tx, Id plot, Id character, bool on);
// Модификатор героя к успеху заказного убийства (модификаторы и таланты, %).
double heroAssassinPct(const World& w, Id character);
// Исход заказа в конце хода (rules::endTurn; HitOutcome — core/world.h): roll — бросок 0…100.
using rg::HitOutcome;
std::vector<HitOutcome> resolveHits(Tx& tx);

// ================================================================ контрразведка
int intelLevel(const World& w, Id state);           // уровень достроенного «Штаба Разведки» (0 — нет)
int counterIntelLevel(const World& w, Id state);    // уровень достроенного «Штаба Контрразведки» (0 — нет)
double detectChance(const World& w, Id state);      // 10 % за уровень штаба контрразведки
// Заговоры против государства (внутренние и других государств), которые контрразведка обнаружила, — по ID.
std::vector<Id> foundPlots(const World& w, Id state);
// «Обнаружить заговоры»: с шансом штаба контрразведки обнаруживается один из необнаруженных заговоров против
// государства (игрок — раз в ход). Возвращает обнаруженный (0 — ничего не найдено).
std::string detectProblem(const World& w, Id state, bool player);   // пусто — можно
Id detectPlots(Tx& tx, Id state, bool player);
// «Раскрыть заговор»: с тем же шансом раскрывается следующий этап (1 — провинции, 2 — приверженность, 3 — герои без
// «Значимого героя фракции», 4 — значимые герои, 5 — лидер); неудача на этапе 3 и дальше — заговорщики видят
// расследование (Plot::alarm). Игрок — раз в ход на заговор. true — раскрыт.
std::string revealProblem(const World& w, Id state, Id plot, bool player);
bool revealPlot(Tx& tx, Id state, Id plot, bool player);
// «Арестовать заговорщиков» по раскрытому этапу (1 — 20 % приверженности и 2 % населения провинций, «Аресты
// заговорщиков»; 2 — приверженность целиком и население по ней, провинции выходят из заговора, «Раскрытый заговор»;
// 3 — и герои без «Значимого героя» кроме лидера; 4 — все герои кроме лидера; 5 — все с лидером, заговор удаляется).
// Арестованные — в тюрьме государства.
struct ArrestResult {
  i64 people = 0;
  std::vector<Id> heroes;
  bool removed = false;
};
ArrestResult arrestPlotters(Tx& tx, Id state, Id plot);
// Освободить заключённых: раса → сколько; люди возвращаются в провинции государства (к своей расе).
void releasePrisoners(Tx& tx, Id state, const std::map<Id, i64>& byRace);
std::vector<Id> prisonHeroes(const World& w, Id state);   // герои «Под арестом» в тюрьме государства
void releaseHero(Tx& tx, Id state, Id character);

// ================================================================ осуществление
// Почему заговор нельзя осуществить (пусто — можно): нужен лидер и хотя бы одна провинция или герой.
std::string executeProblem(const World& w, Id plot);
// Отряды восстания одной провинции по снабжению и численности восставших (без учёта мира — для подсказок).
struct PlotSquad {
  std::string name;
  UnitType type = UnitType::LightInf;
  std::string race;
  i64 count = 0;
};
std::vector<PlotSquad> plotSquads(const World& w, const PlotProvince& pp, i64 rebels);
i64 plotRebels(const World& w, const PlotProvince& pp);   // восставших жителей провинции
struct PlotResult {
  Id state = 0;                       // государство заговора
  std::vector<Id> armies;             // войска восстаний провинций и отколовшиеся войска и флот
  i64 rebels = 0;                     // восставших жителей
};
// «Осуществить заговор»: в провинциях — восстание по приверженности (воины по снабжению), войска заговора — новому
// государству «Заговор <лидер>» (война с целью; внешний заговор — союз с заказчиком), герои заговора уходят к нему
// (свободные — в одно из войск восстания, герои войск, флота и гарнизонов уводят часть войска по доле героев);
// отрицательное довольство провинций заговора становится 0. Заговор удаляется.
PlotResult executePlot(Tx& tx, Id plot);

// ================================================================ очистка ссылок
// Провинция сменила владельца, герой — фракцию, умер или пропал, фракция упразднена: заговоры теряют чужие провинции и
// героев, заговоры против упразднённого государства (и его внешние) удаляются.
void fitPlots(Tx& tx);

}  // namespace rg::rules
