// Regnum — роль за редактором (ТЗ «Доработки №4», п.5): «Администратор редактора» (всё без ограничений) или «Игрок» за
// одно государство мира. Роль выбирается окном при каждом открытии мира (AppConfig::askRole) и пунктом «Сменить роль»
// меню мира; в мире она не хранится (сеанс), последняя роль и государство мира запоминаются в roles.json данных
// пользователя — окно предлагает их первыми. Игроку закрыты справочники, режимы правки мира и инструменты правки
// карты; его войска и флот ходят по правилам очка действий (rules::canPlayerMove, spendAction).
// Пароль администратора (аудит 2026-10-23, 1.4): Settings::adminKey — SHA-256 соли мира и пароля; при заданном пароле
// роль администратора (окно роли при открытии мира и «Сменить роль…») — только после ввода пароля, а до выбора роли
// при открытии мира сеанс идёт за игрока. Вид карты игрока (1.6) — свой, в app.json (UiState::playerView).
#pragma once
#include "app/app_internal.h"
#include "base/json.h"

namespace rg::app::detail::role {

constexpr const char* kAdminOnly = "Доступно только администратору редактора";

// ---- пароль администратора
// Ключ пароля мира: SHA-256 (hex) от соли и пароля; соль — время создания мира (Meta::createdAt), без него — название.
std::string adminKeyOf(const World& w, std::string_view password);
bool adminLocked(const World& w);                              // у мира задан пароль администратора
bool adminPasswordOk(const World& w, std::string_view password);
// Поле пароля: ввод скрыт точками (значение меняется при каждом вводе).
void passwordField(std::string_view id, std::string& value, std::string_view placeholder, bool autofocus = false);
// Окно «Пароль администратора»: задать или сменить (администратор; правка мира, отменяется Ctrl+Z).
void askSetPassword(App& a);
// Снять пароль (с подтверждением).
void askClearPassword(App& a);

// ---- вид карты (подписи, заливка, «Скрыть войска», размер значков)
// Действующие настройки вида: у игрока — свои (UiState::playerView), иначе — мира.
Settings viewSettings(const App& a);
// Изменить вид карты: игрок — свои настройки (app.json, мир не меняется), администратор — настройки мира (play,
// отменяется Ctrl+Z; coalesce — слияние шагов отмены ползунка).
void setView(App& a, std::string_view label, const std::function<void(Settings&)>& fn, std::string_view coalesce = {});
// Мир для карты: у игрока — с его настройками вида (правила и сохранение — мира).
World mapWorld(const App& a, const World& w);
void loadView(App& a, const json::Value& v);                   // app.json «playerView»
json::Value viewJson(const App& a);                            // null — своих настроек нет

// ---- выбор роли
// Государства мира для игрока: сначала основные игровые (Faction::mainState), затем остальные; внутри — по названию.
std::vector<Id> playableStates(const World& w);
void ask(App& a);                          // окно выбора роли (на месте — с выбором государства для игрока)
void askState(App& a);                     // окно сразу со списком государств
void becomeAdmin(App& a);
// Игрок за state: ограничения игрока, своё государство выделено, карта плавно показывает его столицу.
void becomePlayer(App& a, Id state);
// Мир открыт (project.cpp): keep — тот же мир перечитан, роль остаётся (если её государство есть в мире); иначе —
// администратор до выбора и окно роли (если AppConfig::askRole).
void worldOpened(App& a, bool keep);
void worldClosed(App& a);
// Мир заменён целиком (восстановление автосохранения): государство игрока пропало — снова выбор роли.
void validate(App& a);

// ---- ограничения игрока (администратору всё разрешено)
bool sectionAllowed(const App& a, std::string_view section);   // раздел правой ленты (SectionDef::adminOnly — нет)
bool editorAllowed(const App& a, std::string_view editor);     // редакторы таких разделов (справочники, модификаторы…) — нет
bool toolAllowed(const App& a, ToolId t);                       // на ленте и сочетанием: выбор и панорама
bool commandAllowed(const App& a, std::string_view command);    // палитра, сочетания, меню
// Инструмент карты для игрока при выборе (кнопки панелей): выбор, панорама, высадка, постановка своего войска и
// флота из панели государства. Пусто — можно; иначе причина.
std::string toolBlocked(const App& a, ToolId t);

// ---- войска и флот игрока
bool ownArmy(const App& a, Id army);       // объект своего государства (лидер); администратору — любой
// Объекты игрока без очка действий в этом ходу (по возрастанию ID): приглушены на карте.
std::vector<Id> spentArmies(const App& a);
// Встреча при перетаскивании (окна объединения, союза, битвы, посадки, штурма): игровые транзакции, пока окно открыто,
// тратят очко действий этих объектов (те, что останутся в мире). Сбрасывается после решения (spendDone).
void spendPending(std::vector<Id> armies);
void spendDone();
void decorate(App& a, Tx& tx);             // App::act: добавка к игровой транзакции

// ---- верхняя строка
// Признак роли игрока (флаг и название государства; щелчок — страница государства) в начале свободной середины
// верхней строки; на карте место параметров инструмента сужается.
void topBadge(App& a);

}  // namespace rg::app::detail::role
