// Regnum — правила: археология: группы, поиск и исследование мест, раскопки, сундуки, тайник реликвий, справочники мест и сундуков (rules/archaeology.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include "core/world.h"
#include "rules/entities.h"

namespace rg::rules {

// ================================================================ археология
// Случайность — rng с зерном из счётчика мира Meta::rng (каждое событие увеличивает его) и ID участников; хроника —
// LogKind::Archaeology (тайник реликвий и режим правки — без хроники).
//
// ---- археологические группы
// Цена группы: население государства (нежить — трупы) и золото по глобальным константам. problems — почему нельзя.
struct ArchGroupCost {
  i64 people = 0;
  bool corpses = false;               // государство нежити платит трупами
  double gold = 0;
  std::vector<std::string> problems;  // нет гильдии, не хватает населения, трупов или золота
};
ArchGroupCost archGroupCost(const World& w, Id state);
// Сформировать группу (нужна достроенная «Гильдия Археологов»): опыт — наименьший опыт начального уровня
// (archStartLevel). Возвращает ID группы.
Id createArchGroup(Tx& tx, Id state, const std::string& name = {});
void renameArchGroup(Tx& tx, Id state, Id group, const std::string& name);
void disbandArchGroup(Tx& tx, Id state, Id group);             // без возврата цены
void setArchGroupExp(Tx& tx, Id state, Id group, int exp);      // 0…1000
// Модификаторы группы — только модификаторы археологических групп (ModKind::ArchGroup): добавленные получают срок по
// умолчанию, снятые теряют срок; turns 0 — бессрочно.
void setArchGroupModifiers(Tx& tx, Id state, Id group, const std::vector<Id>& mods);
void addArchGroupModifier(Tx& tx, Id state, Id group, Id modifier, int turns = -1);   // turns < 0 — по умолчанию
void setArchGroupModTurns(Tx& tx, Id state, Id group, Id modifier, int turns);
// Группа может получить задание в этот ход: не занята в этом ходу и не ранена (с чумой — может).
bool archGroupReady(const World& w, Id state, Id group, std::string* why = nullptr);

// ---- задания групп в провинциях своего государства
// Полученное: ресурс (сокровища, золото, свитки, ресурс сундука), эссенция, реликвия (к реликвиям государства), сундук
// (его содержимое — следующими строками на глубине depth + 1), «реликвии не нашлось».
struct ArchGain {
  enum Kind : u8 { Resource, Essence, Relic, Chest, NoRelic } kind = Resource;
  Id id = 0;
  double amount = 0;
  int depth = 0;
};
// Итог события «Результат исследования в провинции «…»» / «Результат раскопок в провинции «…»» (для окна).
struct ArchReport {
  enum Kind : u8 { Discover, Explore, Dig } kind = Discover;
  enum Tragedy : u8 { NoTragedy, Wound, Danger, Calamity } tragedy = NoTragedy;
  std::string title;
  Id state = 0, group = 0, province = 0;
  std::string groupName;              // имя группы (группа может погибнуть позже)
  int slot = -1;                      // найденный или исследуемый слот
  Id site = 0;
  bool success = false;
  double chance = 0;                  // шанс успеха, %
  int stage = 0, stages = 0;          // этап исследования (1…) и этапов места
  bool done = false;                  // место исследовано полностью (финальная награда получена)
  std::vector<std::string> lines;     // заметки: чума группы, реликвия из тайника, трагедия и бедствие (исход — в полях)
  std::vector<ArchGain> gains;
  int exp = 0, expAfter = 0, levelBefore = 1, levelAfter = 1;
  std::string calamity;               // заголовок пробуждения бедствия
  bool wounded = false;               // группа ранена
  bool plague = false;                // группа получила «Заражение чумой»
  bool awaiting = false;              // смертельная опасность: ждёт решения (resolveArchDanger)
  Id army = 0;                        // появившееся войско без государства
  Id infected = 0;                    // провинция, заражённая чумой группы
  int tragedyLine = -1;               // первая строка lines о трагедии или бедствии (−1 — их нет)
};
// Шанс «Найти археологическое место»: 50 % + шанс успеха группы + «Старательные искатели», %.
double discoveryChance(const World& w, Id state, Id group);
// Шанс «Успеха» следующего этапа места слота с шансом успеха группы, %.
double exploreChance(const World& w, Id state, Id group, Id province, int slot);
// Провинция государства: есть закрытый слот (найти), открытое неисследованное место (исследовать), все места
// исследованы полностью (раскопки).
bool canDiscover(const World& w, Id state, Id province, std::string* why = nullptr);
bool canExplore(const World& w, Id state, Id province, int slot, std::string* why = nullptr);
bool canExcavate(const World& w, Id state, Id province, std::string* why = nullptr);
ArchReport discoverSite(Tx& tx, Id state, Id group, Id province);
ArchReport exploreSite(Tx& tx, Id state, Id group, Id province, int slot);
ArchReport excavate(Tx& tx, Id state, Id group, Id province);
// «Группу может спасти только божественное вмешательство»: essence ≠ 0 — 1000 этой эссенции государства, группа
// спасена (ранена); 0 — отказ, группа погибает.
void resolveArchDanger(Tx& tx, Id state, Id group, Id essence);
std::vector<Id> divineEssences(const World& w, Id state);      // эссенции, которых хватает на спасение
// Открыть сундук для государства (всё полученное — ему): treasurePct — прибавка к археологическим сокровищам.
std::vector<ArchGain> openChest(Tx& tx, Id state, Id chest, Rng& rng, double treasurePct);
// Строка полученного: «Археологические сокровища 50», «Реликвия «…»», «Ящик».
std::string archGainText(const World& w, const ArchGain& g);

// ---- тайник реликвий: без хроники
// Реликвии, которые можно спрятать в провинции: инвентари героев, хранилища построек и реликвии её владельца.
std::vector<Id> hideableRelics(const World& w, Id province);
void hideRelic(Tx& tx, Id province, Id relic);
// Откопать может только спрятавшее государство, пока владеет провинцией: реликвия — в инвентарь государства.
bool canUnearth(const World& w, Id province, std::string* why = nullptr);
void unearthRelic(Tx& tx, Id province);

// ---- режим правки: без хроники
void setArchSlotOpen(Tx& tx, Id province, int slot, bool open);   // закрытие сбрасывает этапы
// Другое место из справочника (без повторов в провинции): этапы сброшены, сокровище выбрано заново из наград места.
void setArchSlotSite(Tx& tx, Id province, int slot, Id site);

// ---- справочники «Археологические места» и «Сундуки сокровищ»
// Места не добавляются и не удаляются: правятся название, значок и списки наград по слотам.
void setArchSite(Tx& tx, Id site, const std::string& name, const std::string& icon);
void setArchSiteRewards(Tx& tx, Id site, int slot, const std::vector<Id>& chests);
Id addChest(Tx& tx, const std::string& name = {});
// Записать сундук с проверкой полей: ресурс — конкретный («Руда», «Материалы» или «Трупы») или случайный из группы
// «Руда»/«Материалы» (с подгруппами); артефакт — группа реликвий и редкости; сундук — список без самого себя.
void setChest(Tx& tx, const Chest& c);
// Удалить сундук: он убирается из наград мест и вложенных списков; сокровища провинций с ним выбираются заново.
void removeChest(Tx& tx, Id chest);
bool chestResourceAllowed(const World& w, Id res);
bool chestGroupAllowed(const World& w, Id group);
// Поле сундука одной строкой: «Археологические сокровища 50», «Руда / Обычная 50», «Артефакт: обычный»…
std::string chestItemText(const World& w, const ChestItem& it);

}  // namespace rg::rules
