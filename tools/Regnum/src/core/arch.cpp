// Regnum — таблицы археологии: слоты и шансы мест, уровни групп, этапы, бедствия, раскопки, базовые сундуки.
#include "core/arch.h"

#include "core/schema.h"

namespace rg::arch {

// ================================================================ слоты
const SlotInfo kSlots[kArchSlots] = {
    {"Обычное место", "Обычное", 3, 85, 0xe9e6dc},
    {"Редкое место", "Редкое", 4, 75, 0x3f8cff},
    {"Эпическое место", "Эпическое", 5, 60, 0xa45cff},
    {"Легендарное место", "Легендарное", 8, 40, 0xff9b26},
};

int stagesOf(int slot) { return slot >= 0 && slot < kArchSlots ? kSlots[slot].stages : 1; }

// ================================================================ места
const std::vector<BaseSite>& baseSites() {
  static const std::vector<BaseSite> v = {
      {site::Ruins, "Древние руины", "castle"},
      {site::Crypt, "Заброшенный склеп", "skull"},
      {site::Tunnels, "Сеть тоннелей", "pickaxe"},
      {site::Valley, "Спрятанная долина", "mountain"},
      {site::Temple, "Заброшенный храм", "b-religious"},
      {site::Citadel, "Руины древней цитадели", "tower"},
      {site::Portal, "Руины портала", "sparkles"},
      {site::Tomb, "Древняя гробница", "b-cult"},
      {site::Labyrinth, "Подземный лабиринт", "compass"},
      {site::Vault, "Хранилище бессмертного императора", "crown"},
  };
  return v;
}

const std::vector<SiteChance>& slotTable(int slot) {
  static const std::vector<SiteChance> t[kArchSlots] = {
      {{site::Ruins, 30}, {site::Crypt, 30}, {site::Tunnels, 30}, {site::Valley, 9}, {site::Temple, 1}},
      {{site::Ruins, 20}, {site::Crypt, 20}, {site::Tunnels, 20}, {site::Valley, 15}, {site::Temple, 15}, {site::Citadel, 9}, {site::Portal, 1}},
      {{site::Valley, 21}, {site::Temple, 21}, {site::Citadel, 21}, {site::Portal, 21}, {site::Tomb, 3}, {site::Labyrinth, 3}},
      {{site::Tomb, 45}, {site::Labyrinth, 45}, {site::Vault, 10}},
  };
  static const std::vector<SiteChance> none;
  return slot >= 0 && slot < kArchSlots ? t[slot] : none;
}

std::vector<std::pair<Id, int>> slotChances(const Catalogs& c, int slot, const std::vector<Id>& taken) {
  std::vector<std::pair<Id, int>> out;
  for (const SiteChance& sc : slotTable(slot)) {
    const ArchSite* s = c.archSiteByKey(sc.key);
    if (!s || std::find(taken.begin(), taken.end(), s->id) != taken.end()) continue;
    out.push_back({s->id, sc.pct});
  }
  if (out.empty()) return out;
  // Недостающее до 100 % (проценты занятых мест и неполная таблица) — поровну целыми, остаток по одному по порядку.
  int sum = 0;
  for (auto& [id, p] : out) sum += p;
  int left = 100 - sum;
  const int n = int(out.size());
  if (left > 0) {
    const int each = left / n;
    int rest = left % n;
    for (auto& [id, p] : out) {
      p += each + (rest > 0 ? 1 : 0);
      if (rest > 0) rest--;
    }
  }
  return out;
}

Id pickSite(const std::vector<std::pair<Id, int>>& chances, Rng& rng) {
  if (chances.empty()) return 0;
  int total = 0;
  for (auto& [id, p] : chances) total += std::max(0, p);
  if (total <= 0) return chances.front().first;
  int roll = rng.range(1, total);
  for (auto& [id, p] : chances) {
    roll -= std::max(0, p);
    if (roll <= 0) return id;
  }
  return chances.back().first;
}

Id pickChest(const Catalogs& c, Id site, int slot, Rng& rng) {
  const ArchSite* s = c.archSite(site);
  if (!s || slot < 0 || slot >= kArchSlots) return 0;
  std::vector<Id> list;
  for (Id ch : s->rewards[size_t(slot)])
    if (c.chest(ch)) list.push_back(ch);
  if (list.empty()) return 0;
  return list[size_t(rng.range(0, int(list.size()) - 1))];
}

u64 provinceSeed(Id province) { return hashMix(0x4152434841454F4Cull, u64(province)); }

std::array<ArchSlot, kArchSlots> rollSlots(const Catalogs& c, u64 seed) {
  std::array<ArchSlot, kArchSlots> out{};
  Rng rng(seed);
  std::vector<Id> taken;
  for (int i = 0; i < kArchSlots; i++) {
    Id site = pickSite(slotChances(c, i, taken), rng);
    out[size_t(i)].site = site;
    if (site) {
      taken.push_back(site);
      out[size_t(i)].chest = pickChest(c, site, i, rng);
    }
  }
  return out;
}

bool slotDone(const ArchSlot& s, int slot) { return s.site && s.open && s.stage >= stagesOf(slot); }

bool allDone(const Province& p) {
  for (int i = 0; i < kArchSlots; i++)
    if (!slotDone(p.arch[size_t(i)], i)) return false;
  return true;
}

// ================================================================ группы
const LevelInfo kLevels[5] = {
    {0, 100, 0.1, 3, 3},
    {101, 250, 0.2, 5, 5},
    {251, 450, 0.3, 7, 7},
    {451, 700, 0.4, 10, 10},
    {701, 1000, 0.5, 15, 15},
};

int levelOf(int exp) {
  for (int i = kMaxLevel - 1; i >= 0; i--)
    if (exp >= kLevels[i].minExp) return i + 1;
  return 1;
}

int levelMinExp(int level) { return kLevels[std::clamp(level, 1, kMaxLevel) - 1].minExp; }

// ================================================================ этапы
const StageInfo& stageInfo(int slot, int stage) {
  static const char* const kSmall[] = {"small", "box", "casket"};
  static const char* const kWood[] = {"woodChest", "woodCoffer"};
  static const char* const kReinf[] = {"reinfChest", "reinfCoffer"};
  static const char* const kSteel[] = {"steelChest", "steelCoffer"};
  auto S = [](const char* const* a, size_t n) { return std::vector<const char*>(a, a + n); };
  static const std::vector<StageInfo> t[kArchSlots] = {
      // Обычное место: 3 этапа.
      {{5, 10, {}, 10, 0}, {10, 25, S(kSmall, 3), 15, 0}, {0, 0, {}, 25, 0}},
      // Редкое место: 4 этапа.
      {{10, 25, S(kSmall, 3), 15, 0}, {25, 50, S(kSmall, 3), 30, 0}, {50, 75, S(kSmall, 3), 45, 0}, {0, 0, {}, 60, 0}},
      // Эпическое место: 5 этапов; неудача на 4-м — 5 % трагедии, на 5-м — 10 %.
      {{25, 50, S(kWood, 2), 25, 0},
       {50, 75, S(kWood, 2), 30, 0},
       {75, 100, S(kWood, 2), 35, 0},
       {100, 125, S(kReinf, 2), 60, 5},
       {0, 0, {}, 100, 10}},
      // Легендарное место: 8 этапов; неудача на 1–3 — 5 %, 4–6 — 10 %, 7 — 20 %, 8 — 25 %.
      {{50, 75, S(kWood, 2), 25, 5},
       {75, 100, S(kReinf, 2), 30, 5},
       {100, 125, S(kReinf, 2), 35, 5},
       {125, 150, S(kReinf, 2), 40, 10},
       {150, 175, S(kSteel, 2), 45, 10},
       {175, 200, S(kSteel, 2), 50, 10},
       {200, 225, S(kSteel, 2), 55, 20},
       {0, 0, {}, 120, 25}},
  };
  static const StageInfo none;
  if (slot < 0 || slot >= kArchSlots) return none;
  const auto& v = t[slot];
  if (stage < 1 || stage > int(v.size())) return none;
  return v[size_t(stage - 1)];
}

double stageSuccess(int slot, int stage, double bonus) {
  if (slot < 0 || slot >= kArchSlots) return 0;
  const double base = kSlots[slot].success - kStagePenalty * double(std::max(0, stage - 1));
  return std::clamp(base + bonus, 0.0, 100.0);
}

// ================================================================ бедствия
const Calamity& calamityOf(std::string_view siteKey) {
  static const Calamity kNone;
  static const Calamity kPortal{50,
                                "Вторжение из другого мира!",
                                {{"Легионеры неведомого", UnitType::HeavyInf, schema::kRaceLiving, 5000},
                                 {"Стрелки неведомого", UnitType::Ranged, schema::kRaceLiving, 2500},
                                 {"Звери неведомого", UnitType::Beasts, schema::kRaceLiving, 500}}};
  static const Calamity kTomb{50,
                              "Пробуждение древней гробницы!",
                              {{"Скелеты-воины", UnitType::LightInf, schema::kRaceUndead, 10000},
                               {"Скелеты-лучники", UnitType::Ranged, schema::kRaceUndead, 2500},
                               {"Баргесты", UnitType::Beasts, schema::kRaceUndead, 500}}};
  static const Calamity kLabyrinth{50,
                                   "Вторжение подземных племен!",
                                   {{"Троглодиты", UnitType::LightInf, schema::kRaceLiving, 10000},
                                    {"Кобольды-стрелки", UnitType::Ranged, schema::kRaceLiving, 2500},
                                    {"Бехолдеры", UnitType::Monsters, schema::kRaceLiving, 50}}};
  static const Calamity kVault{100,
                               "Хранители Императора здесь!",
                               {{"Хранители", UnitType::HeavyInf, schema::kRaceLiving, 10000},
                                {"Големы-часовые", UnitType::Ranged, schema::kRaceMechanical, 2500},
                                {"Черные драконы", UnitType::Monsters, schema::kRaceLiving, 1}}};
  if (siteKey == site::Portal) return kPortal;
  if (siteKey == site::Tomb) return kTomb;
  if (siteKey == site::Labyrinth) return kLabyrinth;
  if (siteKey == site::Vault) return kVault;
  return kNone;
}

const std::vector<WildUnit>& guardsArmy() {
  static const std::vector<WildUnit> v = {{"Древние стражи", UnitType::HeavyInf, schema::kRaceMechanical, 5000}};
  return v;
}

// ================================================================ раскопки
DigOutcome digOutcome(int roll) {
  DigOutcome o;
  if (roll <= 50) {
    o.kind = DigOutcome::Treasure;
    o.lo = 50;
    o.hi = 100;
  } else if (roll <= 75) {
    o.kind = DigOutcome::Treasure;
    o.lo = 100;
    o.hi = 200;
  } else if (roll <= 84) {
    o.kind = DigOutcome::Scroll;
    o.lo = o.hi = 1;
  } else if (roll <= 85) {
    o.kind = DigOutcome::Relic;
  }
  return o;
}

// ================================================================ базовые сундуки
const std::vector<BaseChest>& baseChests() {
  using K = ChestItemKind;
  namespace g = schema::grp;
  constexpr u32 C = 1u << int(Rarity::Common), R = 1u << int(Rarity::Rare), E = 1u << int(Rarity::Epic),
                L = 1u << int(Rarity::Legendary), X = 1u << int(Rarity::Epochal);
  auto T = [](double v) { return BaseChestItem{K::Treasure, v}; };
  auto G = [](double v) { return BaseChestItem{K::Gold, v}; };
  auto Rs = [](const char* grp, double v, std::vector<const char*> ex = {}) { return BaseChestItem{K::Resource, v, grp, nullptr, std::move(ex)}; };
  auto Rn = [](const char* name, double v) { return BaseChestItem{K::Resource, v, nullptr, name}; };
  auto Es = [](double v, const char* name = nullptr) { return BaseChestItem{K::Essence, v, nullptr, name}; };
  auto A = [](u32 rar) { return BaseChestItem{K::Relic, 1, nullptr, nullptr, {}, rar}; };
  static const std::vector<BaseChest> v = {
      {"small", "Небольшой сундук", {T(50), G(1)}},
      {"box", "Ящик", {T(50), Rs(g::OreCommon, 50)}},
      {"casket", "Ларец", {T(50), Es(50)}},
      {"woodChest", "Деревянный сундук", {T(75), G(2.5), A(C)}},
      {"woodCoffer", "Деревянный ларь", {T(75), Rs(g::MatRaw, 50, {"Нефть"}), A(C)}},
      {"reinfChest", "Укрепленный сундук", {T(100), G(5), Rs(g::OreCommon, 100), Rs(g::MatRaw, 100, {"Нефть"}), A(R)}},
      {"reinfCoffer", "Укрепленный ларь", {T(100), Es(100), Rs(g::OreCommon, 100), Rs(g::MatAlchemy, 100), A(R)}},
      {"steelChest", "Стальной сундук", {T(200), G(10), Rs(g::OreSpecial, 100), Rs(g::MatPrecious, 100), A(R | E)}},
      {"steelCoffer", "Стальной ларь", {T(200), Es(200), Rs(g::OreSpecial, 100), Rs(g::MatAlchemy, 200), A(R | E)}},
      {"mithrilChest", "Мифриловый сундук", {T(400), G(20), Rs(g::OreSpecial, 200), Rs(g::MatPrecious, 200), A(E | L)}},
      {"mithrilCoffer", "Мифриловый ларь", {T(400), Es(300), Rs(g::OreSpecial, 200), Rs(g::MatAlchemy, 400), A(E | L)}},
      {"cultist", "Сундук древнего культиста", {T(600), Es(400), Rs(g::OreSpecial, 300), Rs(g::MatAlchemy, 600), A(L)}},
      {"necromancer", "Сундук древнего некроманта", {T(600), Es(400, "Эссенция смерти"), Rn("Трупы", 500), Rs(g::MatAlchemy, 600), A(L)}},
      {"jarl", "Сейф жадного ярла", {T(600), G(40), Rs(g::OreSpecial, 600), Rs(g::MatPrecious, 600), A(L)}},
      {"offering", "Хранилище забытого подношения", {T(600), Es(400), Es(400), Es(400), A(L)}},
      {"emperor",
       "Сокровище Бессмертного Императора",
       {BaseChestItem{K::Chest, 1, nullptr, nullptr, {}, 0, {"offering", "jarl", "necromancer", "cultist"}}, A(X)}},
  };
  return v;
}

const std::vector<const char*>& baseRewards(std::string_view siteKey, int slot) {
  struct Row {
    const char* site;
    std::vector<const char*> s[kArchSlots];
  };
  static const std::vector<Row> rows = {
      {site::Ruins, {{"woodChest", "woodCoffer"}, {"reinfChest", "reinfCoffer"}, {}, {}}},
      {site::Crypt, {{"woodChest", "woodCoffer"}, {"reinfChest", "reinfCoffer"}, {}, {}}},
      {site::Tunnels, {{"woodChest", "woodCoffer"}, {"reinfChest", "reinfCoffer"}, {}, {}}},
      {site::Valley, {{"reinfChest", "reinfCoffer"}, {"steelChest", "steelCoffer"}, {"mithrilChest", "mithrilCoffer"}, {}}},
      {site::Temple,
       {{"steelChest", "steelCoffer"}, {"reinfChest", "reinfCoffer", "steelChest", "steelCoffer"}, {"mithrilChest", "mithrilCoffer"}, {}}},
      {site::Citadel, {{}, {"steelChest", "steelCoffer", "mithrilChest", "mithrilCoffer"}, {"mithrilChest", "mithrilCoffer"}, {}}},
      {site::Portal, {{}, {"mithrilChest", "mithrilCoffer"}, {"mithrilChest", "mithrilCoffer"}, {}}},
      {site::Tomb, {{}, {}, {"mithrilChest", "mithrilCoffer", "cultist", "necromancer"}, {"cultist", "necromancer"}}},
      {site::Labyrinth, {{}, {}, {"mithrilChest", "mithrilCoffer", "jarl", "offering"}, {"jarl", "offering"}}},
      {site::Vault, {{}, {}, {}, {"emperor"}}},
  };
  static const std::vector<const char*> none;
  if (slot < 0 || slot >= kArchSlots) return none;
  for (const Row& r : rows)
    if (siteKey == r.site) return r.s[slot];
  return none;
}

}  // namespace rg::arch
