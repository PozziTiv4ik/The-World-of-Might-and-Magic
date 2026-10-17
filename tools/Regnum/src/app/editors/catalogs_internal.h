// Regnum — окно справочников: общее для вкладок (catalogs.cpp — окно, вкладки и простые справочники; catalogs_common.cpp —
// состояние, оформление и действия с записями; catalogs_usage.cpp — где используются записи; catalogs_resources.cpp — ресурсы деревом групп; catalogs_relics.cpp — реликвии деревом групп; catalogs_specials.cpp —
// особые отряды; catalogs_classes.cpp — классы героев и деревья талантов; catalogs_arch.cpp — археологические места и
// сундуки сокровищ). Только для этих файлов.
#pragma once
#include <unordered_map>

#include "app/app_internal.h"
#include "app/editors/edkit.h"
#include "app/widgets.h"

namespace rg::app {

namespace cat {

// Вкладки окна в порядке a.openEditor("catalogs", N): N = номер вкладки + 1.
enum Tab : int { kResources, kRaces, kCultures, kReligions, kGovernments, kPositions, kEssences, kRelics, kSpecials, kClasses, kArchSites, kChests, kTabCount };

// Выделение во вкладке «Ресурсы»: группа, «Без группы» (не настоящая группа) или ресурс.
struct ResSel {
  enum Kind : u8 { None, Group, NoGroup, Resource } kind = None;
  Id id = 0;
  bool operator==(const ResSel&) const = default;
};

// Состояние окна (не мира): живёт, пока открыт тот же мир (другой мир — чистое состояние).
struct State {
  int tab = kResources;
  std::string query;                       // поиск текущей вкладки (смена вкладки — сброс)
  std::array<Id, kTabCount> sel{};         // выбранная запись вкладки (кроме ресурсов)
  bool focusSearch = false;
  Id focusName = 0;                        // запись, чьё название получит фокус (после добавления)
  bool revealSel = false;                  // показать выбранную строку (переименование в списке по алфавиту)
  // ---- ресурсы
  ResSel res;
  ResSel scrollTo;                         // показать строку (прокрутка к ней)
  Id editGroup = 0;                        // название группы правится в строке таблицы
  Id focusGroup = 0;                       // в следующем кадре — фокус в поле названия этой группы
  Id pendingEdit = 0;                      // двойной щелчок по названию группы: правка — после отпускания кнопки
  std::unordered_map<Id, bool> open;       // раскрытие групп (нет записи — верхний уровень свёрнут, подгруппы раскрыты)
  bool noGroupOpen = false;                // «Без группы»
  // ---- реликвии
  Id editRelic = 0;                        // название реликвии правится в таблице
  bool focusRelic = false;                 // в следующем кадре — фокус в поле названия
  Id pendingRelic = 0;                     // щелчок по названию: правка — после отпускания кнопки
  // Дерево групп реликвий: выбрана группа (relGroup), «Без группы» (relNoGroup) или реликвия sel[kRelics].
  Id relGroup = 0;
  bool relNoGroup = false;
  Id relScroll = 0, relScrollGroup = 0;    // показать строку реликвии или группы (прокрутка)
  Id editRelGroup = 0, focusRelGroup = 0, pendingRelGroup = 0;   // правка названия группы на месте
  std::unordered_map<Id, bool> relOpen;    // раскрытие групп реликвий (нет записи — раскрыта)
  bool relNoGroupOpen = true;
  Id relState = 0, relHero = 0;            // карточка копируемой реликвии: государство и герой, чьё количество правится
  // ---- классы героев
  Id subclass = 0;                         // выбранный подкласс класса sel[kClasses] (его дерево)
  Id focusSub = 0;                         // в следующем кадре — фокус в поле названия подкласса
  Id talent = 0;                           // выбранный талант дерева подкласса
  Id focusTalent = 0;                      // в следующем кадре — фокус в поле названия таланта
};
State& state(App& a);

// ---------------------------------------------------------------- записи справочников (CatalogItem)
struct ListDef {
  rules::CatalogList list;
  int tab;
  const char* icon;
  const char* title;     // вкладка
  const char* noun;      // «Ресурс»
  const char* newLabel;  // кнопка добавления
  bool colored;          // цвет используется на карте и в фишках
};
const ListDef& listDef(rules::CatalogList l);
const ListDef* defOfTab(int tab);   // простой справочник вкладки; nullptr — вкладка со своим видом
// Встроенная запись («Золото», трупы, энергия, запчасти механизмов): не удаляется.
bool locked(rules::CatalogList l, const CatalogItem& c);
// Значок записи: ресурс — свой (золото — монеты), эссенция — капля, иначе — значок справочника.
const char* itemIcon(rules::CatalogList l, const CatalogItem& c);
// Плитка записи в строке таблицы (значок или точка цвета записи), сторона size.
void swatch(rules::CatalogList l, const CatalogItem& c, float size);
// Добавить запись (ресурс — в группу group); 0 — отказ.
Id addItem(App& a, rules::CatalogList l, Id group = 0);
void renameItem(App& a, rules::CatalogList l, Id id, const std::string& raw);
void setItemColor(App& a, rules::CatalogList l, Id id, Color c);
// Удаление с подтверждением (встроенная — уведомление).
void askRemove(App& a, rules::CatalogList l, Id id);

// ---------------------------------------------------------------- таблица в своей прокрутке
// Таблица по содержимому внутри прокрутки окна: строку можно показать прокруткой, даже если она ещё не строилась.
// Номер строки на экране при сортировке cmp (как ui::Table::sort).
int displayIndex(int row, int rows, int sortCol, bool desc, const std::function<int(int, int, int)>& cmp);
// Прокрутить так, чтобы строка k (с шапкой headerH) была видна в области высотой viewH.
void revealRow(ui::Scroll& sc, float viewH, float headerH, float rowH, int k);

// ---------------------------------------------------------------- использование записей справочника
// Одно место использования в строках армий: фракция и строка.
struct RowRef {
  Id faction = 0, row = 0;
};
struct Use {
  std::vector<Id> provinces;               // ресурс (добыча), раса, культура, религия
  std::vector<Id> factions;                // культура, религия, форма правления; ресурс и эссенция — запасы; раса — рабы
  std::vector<Id> buildings;               // ресурс — стоимость и производство уровней; эссенция — генерация
  std::vector<Id> recipes;                 // ресурс — рецепты построек преобразования
  std::vector<Id> deals;                   // ресурс — действующие сделки
  std::vector<Id> specials;                // ресурс и эссенция — особые отряды
  std::vector<std::string> constants;      // ресурс — глобальные константы (ключи)
  std::vector<RowRef> rows;                // ресурс и эссенция — строки армий (найм, содержание)
  std::vector<std::pair<Id, Id>> seats;    // должность: фракция, место в совете
  i64 population = 0;                      // раса
  double stock = 0, production = 0;        // запасы фракций (раса — рабов); ресурс — добыча провинций за ход
  int states = 0;                          // эссенция: государства с запасом
  size_t total() const {
    return provinces.size() + factions.size() + buildings.size() + recipes.size() + deals.size() + specials.size() + constants.size() +
           rows.size() + seats.size();
  }
};
// Использование всех записей справочника: один проход по миру, кеш по тождеству мира. Держите указатель до конца
// кадра (правка посреди кадра заменяет мир). Нет записи — пусто.
using UseMap = std::unordered_map<Id, Use>;
std::shared_ptr<const UseMap> usageMap(const World& w, rules::CatalogList list);
const Use& useOf(const UseMap& m, Id item);
// Текст для подтверждения удаления: «3 провинции, запасы 2 фракций…».
std::string usageText(rules::CatalogList list, const Use& u);

// ---------------------------------------------------------------- общее оформление
using w::legible;   // очень тёмный цвет записи («Черные Драконы») — светлее, чтобы значок был виден
using w::orName;
std::string nb(i64 n, const char* one, const char* few, const char* many);   // «3 ресурса»
// ---------------------------------------------------------------- деревья групп (ресурсы, реликвии)
constexpr float kTreeIndent = 20, kTreeChevW = 18;   // отступ уровня и ширина стрелки раскрытия
// Направляющие дерева: тонкие вертикальные линии уровней слева от строки.
void treeGuides(RectF cell, int depth);
// Плашка с числом (как в строках списков) и её ширина.
void countPill(RectF r, std::string_view text, bool selected);
float countPillW(std::string_view text);
// Значок «папка с плюсом»: кнопка новой подгруппы.
bool newSubButton(bool disabled);

// Кнопка «Новый …» вкладки (основная, Insert; метка catalogs.add). true — нажата.
bool newButton(App& a, std::string_view label, std::string_view tip = {});
// Кнопка «Удалить: what» карточки записи (опасная, на всю ширину; метка catalogs.delete). true — нажата.
bool deleteButton(App& a, std::string_view what);
// Выбор держится на видимой строке: выбранной нет среди rows — первая строка (строк нет — 0).
template <class Rows, class IdOf>
void keepSelection(Id& sel, const Rows& rows, IdOf idOf) {
  if (std::none_of(rows.begin(), rows.end(), [&](const auto& r) { return idOf(r) == sel; })) sel = rows.empty() ? 0 : idOf(rows.front());
}
// Плитка значка цвета записи: мягкая подложка, обводка, значок (glow — свечение цветом, для редкости).
void tile(RectF r, const char* icon, Color tint, float radius, bool glow = false);
// Шапка карточки: плитка 52 × 52, подпись вида и название.
void cardHead(const char* icon, Color tint, std::string_view caption, std::string_view title);
// Поле поиска вкладки (Ctrl+F) шириной w; placeholder — подсказка в поле.
void searchBox(App& a, State& st, float w, std::string_view placeholder = "Поиск в справочнике");
// Число в ячейке таблицы: 0 — прочерк.
void numCell(ui::Table& t, double v, int digits = 0);
// Значок и число ресурса или эссенции (компактная цена) с подсказкой — внутри ряда ui::HStack.
void amountGlyph(const char* icon, Color color, double amount, std::string_view tip, bool gold = false);   // золото — «тыс.»
// Раздел «Где используется» карточки записи: все места использования фишками со ссылками.
void usageSection(App& a, State& st, const World& w, rules::CatalogList list, const CatalogItem& c, const Use& u);
// Строки армий: перейти к войскам фракции.
void openArmy(App& a, Id faction);
// Перейти к записи другой вкладки окна.
void selectInTab(State& st, int tab, Id id);
// Дерево построек на постройке (после кадра).
void openBuilding(App& a, const World& w, Id building);
// Фишка строки армии «Фракция · Отряд · 120» цвета фракции; щелчок — войска фракции. false — строки нет.
bool rowChip(App& a, const World& w, const RowRef& r, bool showCount);

// ---------------------------------------------------------------- вкладки
// Строка поиска и кнопок, таблица и карточка вкладки (в потоке окна, под строкой вкладок).
void drawResources(App& a, State& st);
void drawRelics(App& a, State& st);
void drawSpecials(App& a, State& st);
void drawClasses(App& a, State& st);
void drawArchSites(App& a, State& st);   // catalogs_arch.cpp: археологические места
void drawChests(App& a, State& st);      // catalogs_arch.cpp: сундуки сокровищ
// Раскладка вкладки: таблица слева, линия, карточка справа; в узком окне карточка — под таблицей.
struct Split {
  RectF table, card;
};
Split split(RectF area);

}  // namespace cat
}  // namespace rg::app
