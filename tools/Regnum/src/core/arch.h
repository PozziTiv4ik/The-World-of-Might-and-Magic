// Regnum — таблицы археологии (ТЗ «Доработки №2» и «№4», п.1): слоты археологических мест и шансы мест в них,
// уровни археологических групп, этапы исследования и их награды, трагедии и пробуждение бедствий, раскопки.
//
// Здесь только данные и чистые функции над справочниками (без изменения мира): их используют базовые записи мира
// (core/content), нормализация, правила (rules/archaeology.cpp) и интерфейс (подписи, шансы).
#pragma once
#include "core/world.h"

namespace rg::arch {

// ---------------------------------------------------------------- слоты
// Слот: «Обычное место» (3 этапа, успех 85 %), «Редкое» (4, 75 %), «Эпическое» (5, 60 %), «Легендарное» (8, 40 %).
struct SlotInfo {
  const char* name;
  const char* shortName;
  int stages;
  double success;   // базовый шанс «Успеха» первого этапа, %
  u32 color;        // цвет рамки слота
};
extern const SlotInfo kSlots[kArchSlots];
constexpr double kStagePenalty = 3;       // каждый следующий этап: −3 % к успеху
constexpr double kDiscoverySuccess = 50;  // «Найти археологическое место»: успех 50 %
int stagesOf(int slot);

// ---------------------------------------------------------------- места
struct BaseSite {
  const char* key;
  const char* name;
  const char* icon;
};
const std::vector<BaseSite>& baseSites();   // десять мест в порядке ТЗ (п.4)
namespace site {
constexpr const char* Ruins = "ruins";
constexpr const char* Crypt = "crypt";
constexpr const char* Tunnels = "tunnels";
constexpr const char* Valley = "valley";
constexpr const char* Temple = "temple";
constexpr const char* Citadel = "citadel";
constexpr const char* Portal = "portal";
constexpr const char* Tomb = "tomb";
constexpr const char* Labyrinth = "labyrinth";
constexpr const char* Vault = "vault";
}  // namespace site

// Шансы мест в слоте (п.5): ключ места и целый процент.
struct SiteChance {
  const char* key;
  int pct;
};
const std::vector<SiteChance>& slotTable(int slot);
// Шансы мест слота с учётом уже занятых мест провинции (п.7): занятые убираются, их проценты (и недостача до 100 %)
// раздаются оставшимся поровну целыми числами (остаток — по одному по порядку), сумма — ровно 100. Место без записи
// в справочнике пропускается. Возвращает (место, %).
std::vector<std::pair<Id, int>> slotChances(const Catalogs& c, int slot, const std::vector<Id>& taken);
// Случайное место по шансам (roll 1…100).
Id pickSite(const std::vector<std::pair<Id, int>>& chances, Rng& rng);
// Сокровище слота: случайный сундук из «списка наград» места для этого слота (0 — список пуст).
Id pickChest(const Catalogs& c, Id site, int slot, Rng& rng);
// Четыре слота новой провинции (п.6–8): места без повторов, сокровища по спискам наград; все слоты закрыты.
std::array<ArchSlot, kArchSlots> rollSlots(const Catalogs& c, u64 seed);
// Зерно слотов провинции (детерминированно по ID).
u64 provinceSeed(Id province);
// Место слота открыто и все его этапы пройдены (финальная награда получена).
bool slotDone(const ArchSlot& s, int slot);
bool allDone(const Province& p);

// ---------------------------------------------------------------- археологические группы
// Уровни (п.2): опыт, содержание (золото за ход), дополнительный шанс успеха и живучесть, %.
struct LevelInfo {
  int minExp, maxExp;
  double upkeep, success, vitality;
};
extern const LevelInfo kLevels[5];
constexpr int kMaxExp = 1000;
constexpr int kMaxLevel = 5;
constexpr double kMaxBonus = 25;          // шанс успеха и живучесть группы — не больше 25 %
int levelOf(int exp);                     // 1…5
int levelMinExp(int level);               // наименьший опыт уровня

// ---------------------------------------------------------------- этапы исследования (п.15)
struct StageInfo {
  int lo = 0, hi = 0;                     // археологические сокровища lo…hi (0 — нет)
  std::vector<const char*> chests;        // «или один из сундуков» (ключи базовых сундуков); пусто — только сокровища
  int exp = 0;                            // опыт за «Успех»
  double tragedy = 0;                     // «Неудача» на этом этапе: шанс трагедии, % (иначе +5 опыта)
};
// Этап stage (1…stagesOf(slot)) слота. Последний этап: награда — сокровище провинции (финальная) и опыт.
const StageInfo& stageInfo(int slot, int stage);
constexpr int kFailExp = 5;               // «Неудача»: +5 опыта (если не случилась трагедия)
// Шанс «Успеха» этапа stage (1…) с бонусом группы, %, в пределах 0…100.
double stageSuccess(int slot, int stage, double bonus);

// ---------------------------------------------------------------- трагедии и бедствия (п.18–19)
constexpr double kTragedyWound = 70, kTragedyDanger = 25, kTragedyCalamity = 5;   // %
constexpr double kDangerWound = 50;       // смертельная опасность: ранение 50 % + живучесть, иначе гибель или спасение
constexpr double kDivineCost = 1000;      // «божественное вмешательство»: 1000 эссенции любого вида
constexpr int kWoundTurns = 2, kPlagueTurns = 4;
// Отряд войска без государства.
struct WildUnit {
  const char* name;
  UnitType type;
  const char* race;
  i64 count;
};
// Пробуждение бедствия: что бывает в месте. common — «как в 19.1» (чума группы или стражи); special — особое войско
// места (вторжение) и его шанс; у хранилища императора — только войско (100 %).
struct Calamity {
  double specialPct = 0;                  // шанс особого события места, %
  const char* title = nullptr;            // «Вторжение из другого мира!»
  std::vector<WildUnit> army;             // войско особого события
};
const Calamity& calamityOf(std::string_view siteKey);
constexpr const char* kPlagueTitle = "Древняя чума вышла на волю!";
constexpr const char* kGuardsTitle = "Стражи пробуждены!";
const std::vector<WildUnit>& guardsArmy();   // «Древние стражи» 5000, тяжёлая пехота, «Механический»

// ---------------------------------------------------------------- раскопки (п.17)
struct DigOutcome {
  enum Kind : u8 { Nothing, Treasure, Scroll, Relic } kind = Nothing;
  int lo = 0, hi = 0;
};
// Исход раскопок по броску 1…100: 50 % — 50–100 сокровищ, 25 % — 100–200, 9 % — 1 свиток, 1 % — реликвия, иначе ничего.
DigOutcome digOutcome(int roll);

// ---------------------------------------------------------------- базовые сундуки (п.10, 12)
struct BaseChestItem {
  ChestItemKind kind;
  double amount;
  const char* group = nullptr;            // ресурс: ключ группы (schema::grp) для случайного выбора
  const char* resName = nullptr;          // ресурс или эссенция: название конкретной записи
  std::vector<const char*> exclude;       // ресурс: исключить по названию
  u32 rarities = 0;                       // артефакт: биты Rarity
  std::vector<const char*> chests;        // сундук: случайный из списка (ключи)
};
struct BaseChest {
  const char* key;
  const char* name;
  std::vector<BaseChestItem> items;
};
const std::vector<BaseChest>& baseChests();
// Списки наград мест по слотам (п.12): ключ места → 4 списка ключей сундуков.
const std::vector<const char*>& baseRewards(std::string_view siteKey, int slot);

}  // namespace rg::arch
