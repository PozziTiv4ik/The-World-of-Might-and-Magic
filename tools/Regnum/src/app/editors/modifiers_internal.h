// Regnum — окно модификаторов (modifiers*.cpp): встроенные модификаторы и правка, где используется модификатор,
// эффекты и генерация за ход. Только для файлов окна.
#pragma once
#include <algorithm>

#include "app/app_internal.h"
#include "app/editors/edkit.h"
#include "app/widgets.h"
#include "gfx/icons.h"

namespace rg::app::modedit {

using platform::Key;
using detail::later;
using rules::ModTarget;
using w::orName;

// ---------------------------------------------------------------- встроенные и правка (modifiers.cpp)
bool isAuto(const Modifier& m);   // ставится сам (столица, совет, голод, должности)
// Правка модификатора: свой — по ID; встроенный без записи мира — запись создаётся по шаблону, окно переходит на неё.
bool editMod(App& a, const Modifier& m, std::string_view label, const std::function<void(Modifier&)>& fn, std::string coalesce = {});
// Запись мира модификатора внутри транзакции (встроенный — создаётся по шаблону).
Id recordOf(Tx& tx, const Modifier& m);
// Ключ слияния правок одного модификатора (у встроенного — по ключу: шаблон и созданная запись — одно и то же).
std::string mergeKey(const Modifier& m, std::string_view part);

// ---------------------------------------------------------------- где используется (modifiers_usage.cpp)
struct ModUse {
  std::vector<Id> provinces, states, guilds, techs, armies, heroes, positions;
  std::vector<std::pair<Id, int>> levels;   // постройка, номер уровня (с 1)
  std::vector<std::pair<Id, Id>> groups;    // археологические группы: государство, группа
  size_t total() const {
    return provinces.size() + states.size() + guilds.size() + techs.size() + levels.size() + armies.size() + heroes.size() + positions.size() +
           groups.size();
  }
};
ModUse usageOf(const World& w, Id mod);
std::unordered_map<Id, int> usageCounts(const World& w);           // модификатор → мест использования
std::map<std::string, std::vector<Id>> autoUse(const World& w);    // автоматический (ключ) → где действует сейчас
std::string usageText(const ModUse& u);                            // «3 провинции, 2 государства…»
bool hasLocal(const Modifier& m);
bool hasGlobal(const Modifier& m);
// Дипломатия включена, а цели не выбраны (ТЗ 1.g.ii.2.b).
bool missingTargets(const Modifier& m);
std::string effectSummary(const World& w, const Modifier& m);      // краткая сводка эффектов для строки списка
void usageSection(App& a, const Modifier& m, const std::map<std::string, std::vector<Id>>& autos, bool ro);

// ---------------------------------------------------------------- эффекты и генерация (modifiers_effects.cpp)
enum class FxGroup : u8 { Local, Global, Army, Arch };
u32 archMask();                                                     // эффекты археологических групп
void effectGroup(App& a, const Modifier& m, FxGroup group, bool ro);
void genSection(App& a, const Modifier& m, bool ro);

}  // namespace rg::app::modedit
