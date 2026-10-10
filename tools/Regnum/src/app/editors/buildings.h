// Regnum — дерево построек (ТЗ 1.h): общее для всех государств и уникальные постройки государства.
// Общие помощники отображения построек для редактора, вкладок и выбора строительства.
#pragma once
#include "app/app.h"

namespace rg::app {

// Открыть дерево построек: owner 0 — общее дерево, иначе уникальные постройки государства; building — выделить.
void openBuildingTree(App& a, Id owner, Id building = 0);
// Окно выбора строительства в провинции (ТЗ 1.f.ii); также по имени: app.openDialog("build.picker", province).
std::unique_ptr<Dialog> buildPickerDialog(Id province);
// Окно выбора модификаторов уровня постройки (level — с 1); также по имени: app.openDialog("bt.mods", building),
// тогда — первый уровень.
std::unique_ptr<Dialog> levelModsDialog(Id building, int level);

namespace bld {

Color catColor(BuildingCat c);
const char* catIcon(BuildingCat c);
const char* catName(BuildingCat c);
const char* iconOf(const Building& b);                 // значок постройки (неизвестный — по категории)
int levelTurns(const Building& b, int level);          // срок уровня (1…)
// Стоимость фишками «значок + количество»; payer != nullptr — нехватка красным.
void costChips(const std::map<Id, double>& cost, const Faction* payer, bool showEmpty = true);
// Цена в эссенциях (BuildingLevel::essCost) тем же видом: значок цвета эссенции и количество; пусто — ничего.
void essCostChips(const std::map<Id, double>& cost, const Faction* payer);
std::string essenceCostText(const World& w, const std::map<Id, double>& cost);   // «Эссенция смерти 150»
std::string shipsText(u32 ships);                                                 // типы кораблей верфи через запятую
// Действия достроенной постройки в провинции (ТЗ «Доработки №3», п.4–5): «Вылечить провинцию» у здания целительства,
// «Заразить чумой» у здания чумы; недоступность — с причиной в подсказке (panels/construction_roles.cpp).
void roleActions(App& a, Id province, const ProvBuilding& pb, const Building& b);
// Что достроенный уровень даёт владельцу за ход (BuildingLevel::produce): «+количество» по ресурсам; пусто — ничего.
void produceChips(const std::map<Id, double>& produce);
std::string produceText(const World& w, const std::map<Id, double>& produce);   // «+2 трупы, +100 демоническая энергия»
// Хватает ли ресурса у плательщика.
bool affordable(const std::map<Id, double>& cost, const Faction* payer);
// Эффекты модификаторов уровня фишками (или «Без эффектов»).
void levelEffects(const World& w, const BuildingLevel& L, bool compact = false);
std::string costText(const World& w, const std::map<Id, double>& cost);   // «150 золота, 40 железа» (подсказки)
// Плитка значка постройки цвета категории (в потоке, сторона size).
void iconTile(const Building& b, float size, bool dim = false);

// Значок и подпись (ресурс и количество, срок) — элемент строки с переносом по ширине области.
struct Token {
  std::string icon;
  Color color;
  std::string text;
  ui::Ink ink = ui::Ink::Normal;
  std::string tip;
  ui::Font font = ui::Font::Strong;
};
void tokens(const std::vector<Token>& list, float gap = 10);
// Постройка преобразования (ТЗ «Доработки», п.3): входы → выход, срок цикла. payer — нехватка входов красным.
std::vector<Token> recipeTokens(const World& w, const Recipe& r, const Faction* payer = nullptr);
std::string recipeText(const World& w, const Recipe& r);   // «Железо 10 + Уголь 5 → Сталь 3 · 2 хода»
bool recipeValid(const World& w, const Recipe& r);          // есть вход с количеством и ресурс на выходе
// Постройка генерации эссенции: эссенции уровня за ход («+5» значком цвета эссенции).
std::vector<Token> essenceTokens(const World& w, const std::map<Id, double>& essence);
std::string essenceText(const World& w, const std::map<Id, double>& essence);   // «+5 эссенция пламени»
// Культовая постройка (ТЗ «Доработки», п.2): «Одна на всю карту» (общая) или «Одна на государство» (уникальная);
// nullptr — не культовая.
const char* cultRule(const Building& b);
// Особые отряды постройки доступа: названия через запятую.
std::string specialsText(const World& w, const std::vector<Id>& specials);

}  // namespace bld
}  // namespace rg::app
