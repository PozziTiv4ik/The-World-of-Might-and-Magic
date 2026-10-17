// Regnum — персонажи: общие части каталога (characters.cpp), инспектора (character_inspector.cpp) и талантов
// (character_talents.cpp). Реализация — character_common.cpp и character_talents.cpp.
#pragma once
#include "app/widgets.h"

namespace rg::app::chars {

// Роли персонажа: правитель, лорд провинций, совет, войска и флот (ТЗ 1.a.vi, 1.b.iv, 1.c.iii), гарнизон провинции.
struct Roles {
  std::vector<Id> rulerOf;                   // фракции, где персонаж — правитель
  std::vector<Id> lordOf;                    // провинции, где он лорд
  std::vector<std::pair<Id, Id>> seats;      // место в совете: фракция, ID места
  std::vector<Id> armies;                    // войска и флоты, где он герой
  std::vector<Id> commands;                  // из них — главный полководец
  Id garrison = 0;                           // провинция, в гарнизоне которой он герой
  size_t total() const { return rulerOf.size() + lordOf.size() + seats.size() + armies.size() + (garrison ? 1 : 0); }
};
Roles rolesOf(const World& w, Id character);
std::string rolesText(const World& w, const Roles& r);                       // роли через запятую (подсказка)
std::string roleLine(const World& w, const Character& c, const Roles& r);    // подпись строки каталога
std::string positionOf(const World& w, Id faction, Id seat);                 // должность места совета

// Портрет (декодированный и уменьшенный; кеш по персонажу и содержимому). square — квадрат по лицу (для круга).
// nullptr — нет портрета или он ещё декодируется.
const gfx::Image* portraitImage(const Character& c, bool square);
void avatar(const Character& c, float size, bool ring, std::string_view tip);
void loadPortrait(App& a, Id character);                                    // выбор файла PNG/JPEG
bool setPortraitFromFile(App& a, Id character, const std::string& path);
void askClearPortrait(App& a, Id character);
void askDelete(App& a, Id character);                                       // удаление с подтверждением

// Поля «Класс», «Подкласс» и «Уровень» вкладки «Сведения» и кнопка «Возвысить до Лича» (character_talents.cpp).
// Доступность — по роли, а не по ro: класс, подкласс и Лич — игровые действия за фракцию героя (App::canPlay),
// уровень — правка мира (App::canEdit); ro оставлен для единообразия вызовов вкладки.
void classFields(App& a, const Character& c, bool ro);
void lichButton(App& a, const Character& c, bool ro);

// Вкладки (ID для перехода и тестов).
constexpr const char* kTabInfo = "character.info";

}  // namespace rg::app::chars
