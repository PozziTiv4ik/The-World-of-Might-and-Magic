// Regnum — базовые записи мира, версия 6 (аудит 2026-10-23, раздел 7 и п.4.14–4.15): модификаторы талантов (общие
// записи «Талант: …» на ранг и «Финальный талант: …»), классов героев, реликвий, рас, культур, религий, форм правления
// и должностей; порог ярусов базовых подклассов — 7 очков; ветка «Стимуляция промышленности» — 7 ступеней вместо 99
// (изученность переносится, множители провинций — в пределах доступного); «Основы археологии» и «Продвинутые
// инструменты» — +10 и +20 % сокровищ; потребители мёртвых ресурсов (нефть → топливо, шёлк → ткань, мех, хитин,
// чешуя, титан, платина, метеоритная руда, жемчуг, кость — в ценах построек и особых отрядов); особые отряды:
// элементали соборов, стрелки с порохом, топливом, магическим порохом, дамасской сталью и лунным серебром, бронированная
// пехота «Оружейной палаты»; шантирийские големы и титаны дешевле; содержание и раса «Драконов Бездны», содержание
// фениксов; виды государства алтаря культа, мавзолея и глубинной бездны; модификаторы уровней порта, рынка и верфи;
// «Общие» артефакты — из сундуков; «Гильдия строителей» — N + 1 слот; сроки строительства уровней базовых построек по
// цене; исправленные названия («Паладин», «Рат'гарда», ресурсы-чудовища, «Драконы Чистейшего Света»). Записи
// сопоставляются по ключам и названиям базовых; правки пользователя (другие значения, свои списки) не перезаписываются.
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "core/content_seed.h"
#include "core/schema.h"

namespace rg::content::detail {

namespace {

using Cost = std::vector<std::pair<const char*, double>>;
using F = Fx;

// ================================================================ данные
struct FxV {
  Fx fx;
  double v;
};
// Генерация за ход: эссенция (по умолчанию) или ресурс справочника по названию.
struct GenV {
  const char* name;
  double v;
  bool res = false;
};
struct ModSpec {
  std::string name;
  ModKind kind = ModKind::Any;
  const char* icon = "sparkles";
  u32 color = 0x8c7ae6;
  const char* desc = "";
  std::vector<FxV> fx;
  std::vector<GenV> gen;
};

// ---------------------------------------------------------------- таланты
// Тема таланта: эффекты одного ранга (правила умножают их на изученные ранги) и финального таланта.
struct TalentTheme {
  const char* name;
  const char* icon;
  u32 color;
  std::vector<FxV> rank;
  std::vector<FxV> ult;
  std::vector<GenV> ultGen;
};
const std::vector<TalentTheme>& talentThemes() {
  static const std::vector<TalentTheme> v = {
      {"Доблесть", "swords", 0xc0503a, {{F::LoyaltyPerTurn, 0.2}}, {{F::LoyaltyPerTurn, 1.5}, {F::AssassinPct, -5}}, {}},
      {"Мощь", "trend-up", 0xd07a2f, {{F::LoyaltyPerTurn, 0.1}, {F::AssassinPct, -0.5}}, {{F::LoyaltyPerTurn, 1}, {F::AssassinPct, -5}},
       {{"Эссенция силы", 5}}},
      {"Оборона", "shield", 0x3f7fbf, {{F::AssassinPct, -1}, {F::RebellionPct, -0.2}}, {{F::AssassinPct, -10}, {F::RebellionPct, -2}}, {}},
      {"Командование", "banner", 0xd9a441, {{F::LoyaltyPerTurn, 0.2}, {F::ArmyUpkeepPct, -0.3}}, {{F::LoyaltyPerTurn, 1}, {F::ArmyUpkeepPct, -3}}, {}},
      {"Меткость", "bow", 0x7f9a3c, {{F::LoyaltyPerTurn, 0.1}, {F::PlotAssassinPct, 0.5}}, {{F::LoyaltyPerTurn, 1}, {F::PlotAssassinPct, 5}}, {}},
      {"Скрытность", "eye-off", 0x5b4a7a, {{F::AssassinPct, -1}, {F::PlotAssassinPct, 0.5}}, {{F::AssassinPct, -10}, {F::PlotAssassinPct, 5}}, {}},
      {"Прозорливость", "eye", 0x4cabff, {{F::AssassinPct, -0.5}, {F::ResearchTimePct, -0.3}},
       {{F::AssassinPct, -5}, {F::ResearchTimePct, -3}, {F::AssassinGuardPct, -3}}, {}},
      {"Магия", "wand", 0x8b6bff, {{F::ResearchTimePct, -0.4}}, {{F::ResearchTimePct, -4}}, {{"Эссенция арканы", 5}}},
      {"Пламя", "flame", 0xff6a2b, {{F::LoyaltyPerTurn, 0.1}, {F::RebellionPct, -0.2}}, {{F::LoyaltyPerTurn, 1}}, {{"Эссенция пламени", 5}}},
      {"Лёд", "frost", 0x9fd8f0, {{F::AssassinPct, -0.8}}, {{F::AssassinPct, -6}}, {{"Эссенция льда", 5}}},
      {"Буря", "bolt", 0x9fe3f7, {{F::LoyaltyPerTurn, 0.1}, {F::FleetUpkeepPct, -0.3}}, {{F::LoyaltyPerTurn, 1}}, {{"Эссенция воздуха", 5}}},
      {"Море", "sea", 0x2f8fff, {{F::FleetUpkeepPct, -0.5}, {F::TradePct, 0.5}}, {{F::FleetUpkeepPct, -5}}, {{"Эссенция воды", 5}}},
      {"Земля", "mountain", 0xa07a4a, {{F::ResourcePct, 1}, {F::BuildCostPct, -0.3}}, {{F::ResourcePct, 8}}, {{"Эссенция земли", 5}}},
      {"Природа", "wood", 0x4fc24f, {{F::PopGrowthPct, 0.1}}, {{F::PopGrowthPct, 0.5}}, {{"Эссенция природы", 5}}},
      {"Свет", "sun", 0xffd86a, {{F::ContentmentPerTurn, 0.1}, {F::RebellionPct, -0.2}}, {{F::ContentmentPerTurn, 1}}, {{"Эссенция света", 5}}},
      {"Исцеление", "heal", 0x4fb09a, {{F::PopGrowthPct, 0.1}, {F::ContentmentPerTurn, 0.1}},
       {{F::PopGrowthPct, 0.5}, {F::ContentmentPerTurn, 0.5}, {F::AssassinPct, -5}}, {}},
      {"Смерть", "skull", 0x8f9a86, {{F::RebellionPct, -0.3}, {F::LoyaltyPerTurn, 0.1}}, {{F::RebellionPct, -3}}, {{"Эссенция смерти", 5}}},
      {"Кровь", "heart", 0xd0233f, {{F::LoyaltyPerTurn, 0.1}, {F::AssassinPct, -0.5}}, {{F::LoyaltyPerTurn, 1}}, {{"Эссенция крови", 5}}},
      {"Чума", "plague", 0x9bb52c, {{F::PlotAssassinPct, 0.5}, {F::RebellionPct, -0.2}}, {{F::PlotAssassinPct, 5}}, {{"Эссенция чумы", 5}}},
      {"Тьма", "moon", 0x6a4fb0, {{F::AssassinPct, -0.8}, {F::AssassinGuardPct, -0.3}}, {{F::AssassinPct, -6}}, {{"Эссенция тьмы", 5}}},
      {"Бездна", "u-elementals", 0x5a3fa0, {{F::RebellionPct, -0.3}, {F::LoyaltyPerTurn, 0.1}}, {{F::RebellionPct, -3}}, {{"Эссенция бездны", 5}}},
      {"Господство", "shackles", 0xd9a520, {{F::RebellionPct, -0.4}, {F::IncomePct, 0.2}}, {{F::RebellionPct, -3}}, {{"Эссенция господства", 5}}},
      {"Механика", "u-machines", 0x7f8a99, {{F::BuildCostPct, -0.5}, {F::ArmyUpkeepPct, -0.2}}, {{F::BuildCostPct, -5}},
       {{"Механические детали", 5, true}}},
      {"Гармония", "scales", 0x5fd8c0, {{F::ContentmentPerTurn, 0.1}, {F::AssassinPct, -0.5}}, {{F::ContentmentPerTurn, 1}}, {{"Эссенция гармонии", 5}}},
      {"Удача", "dice", 0xe2b33c, {{F::IncomePct, 0.2}, {F::TradePct, 0.5}}, {{F::IncomePct, 2}, {F::TradeFlat, 50}}, {}},
      {"Небо", "u-flying", 0xbfe6ff, {{F::LoyaltyPerTurn, 0.1}, {F::AssassinPct, -0.5}}, {{F::LoyaltyPerTurn, 1}}, {{"Эссенция воздуха", 5}}},
      {"Сумерки", "star", 0x7d6bb8, {{F::AssassinPct, -0.5}, {F::AssassinGuardPct, -0.3}}, {{F::AssassinGuardPct, -3}}, {{"Эссенция сумерек", 5}}},
  };
  return v;
}
const TalentTheme* talentTheme(std::string_view name) {
  for (const TalentTheme& t : talentThemes())
    if (name == t.name) return &t;
  return nullptr;
}

// Тема таланта по его значку (значок базового таланта подобран по словам названия — content_v3.cpp).
const char* themeOfIcon(std::string_view icon) {
  static const std::pair<const char*, const char*> k[] = {
      {"flame", "Пламя"},        {"frost", "Лёд"},          {"bolt", "Буря"},           {"plague", "Чума"},
      {"skull", "Смерть"},       {"heal", "Исцеление"},     {"heart", "Кровь"},         {"shackles", "Господство"},
      {"shield", "Оборона"},     {"iron", "Оборона"},       {"u-elementals", "Бездна"}, {"eye-off", "Скрытность"},
      {"moon", "Тьма"},          {"religion", "Свет"},      {"sun", "Свет"},            {"bow", "Меткость"},
      {"crosshair", "Меткость"}, {"u-flying", "Небо"},      {"u-beasts", "Природа"},    {"wood", "Природа"},
      {"magnet", "Земля"},       {"mountain", "Земля"},     {"pickaxe", "Земля"},       {"sea", "Море"},
      {"u-machines", "Механика"}, {"hammer", "Механика"},   {"eye", "Прозорливость"},   {"hourglass", "Прозорливость"},
      {"star", "Прозорливость"}, {"scales", "Гармония"},    {"music", "Гармония"},      {"crown", "Командование"},
      {"army", "Командование"},  {"scroll", "Магия"},       {"gem", "Магия"},           {"essence", "Магия"},
      {"wand", "Магия"},         {"sparkles", "Магия"},     {"sword", "Доблесть"},      {"swords", "Доблесть"},
      {"battle", "Доблесть"},    {"trend-up", "Мощь"},      {"compass", "Удача"},       {"coins", "Удача"},
      {"dice", "Удача"},
  };
  for (auto& [i, t] : k)
    if (icon == i) return t;
  return nullptr;
}

// ---------------------------------------------------------------- классы героев
// Модификатор класса (действует на всех героев класса) и тема талантов класса без своего значка.
struct ClassSpec {
  const char* name;
  const char* theme;
  std::vector<FxV> fx;
  std::vector<GenV> gen;
  const char* builtin = nullptr;   // встроенный модификатор вместо своего
};
const std::vector<ClassSpec>& classSpecs() {
  static const std::vector<ClassSpec> v = {
      {"Воин", "Доблесть", {{F::LoyaltyPerTurn, 1}}, {}},
      {"Паладин", "Свет", {{F::LoyaltyPerTurn, 0.5}, {F::ContentmentPerTurn, 0.5}}, {{"Эссенция света", 2}}},
      {"Стрелок", "Меткость", {{F::LoyaltyPerTurn, 0.5}, {F::AssassinPct, -5}}, {}},
      {"Жрец", "Свет", {{F::ContentmentPerTurn, 1}, {F::RebellionPct, -2}}, {{"Эссенция света", 3}}},
      {"Друид", "Природа", {{F::PopGrowthPct, 0.3}}, {{"Эссенция природы", 3}}},
      {"Маг", "Магия", {{F::ResearchTimePct, -3}}, {{"Эссенция арканы", 3}}},
      {"Некромант", "Смерть", {}, {}, schema::mod::Necromancer},
      {"Рыцарь Смерти", "Смерть", {{F::LoyaltyPerTurn, 1}, {F::RebellionPct, -2}}, {{"Эссенция смерти", 2}}},
      {"Вампир", "Кровь", {{F::AssassinPct, -10}}, {{"Эссенция крови", 3}}},
      {"Чуматворец", "Чума", {{F::PlotAssassinPct, 5}, {F::RebellionPct, -1}}, {{"Эссенция чумы", 5}}},
      {"Рыцарь тьмы", "Тьма", {{F::AssassinPct, -5}, {F::LoyaltyPerTurn, 0.5}}, {{"Эссенция тьмы", 3}}},
      {"Шаман", "Буря", {{F::ContentmentPerTurn, 0.5}}, {{"Эссенция воздуха", 2}, {"Эссенция воды", 2}}},
      {"Монах", "Гармония", {{F::AssassinPct, -10}}, {{"Эссенция гармонии", 3}}},
      {"Культист", "Бездна", {{F::RebellionPct, -2}}, {{"Эссенция хаоса", 3}}},
      {"Рыцарь Дракона", "Пламя", {{F::LoyaltyPerTurn, 1}}, {{"Эссенция пламени", 3}}},
      {"Рыцарь Господства", "Господство", {{F::RebellionPct, -3}}, {{"Эссенция господства", 3}}},
      {"Варвар", "Доблесть", {{F::LoyaltyPerTurn, 1.5}, {F::ArmyUpkeepPct, -2}}, {}},
      {"Рыцарь Света Пустоты", "Сумерки", {{F::AssassinGuardPct, -3}, {F::LoyaltyPerTurn, 0.5}}, {{"Эссенция сумерек", 3}}},
      {"Мехомант", "Механика", {{F::BuildCostPct, -3}}, {{"Механические детали", 3, true}}},
      {"Ассасин", "Скрытность", {{F::AssassinPct, -10}, {F::PlotAssassinPct, 10}}, {}},
  };
  return v;
}

// Прежние названия базовых записей, исправленные в версии 6 (данные версий 2–3 их ещё содержат).
std::string_view fixedName(std::string_view n) {
  if (n == "Палладин") return "Паладин";
  if (n == "Палладин-Гармонии") return "Паладин-Гармонии";
  return n;
}

// ---------------------------------------------------------------- реликвии
// Тема реликвии: эффекты по редкости (обычная … эпохальная) — действуют на героя-носителя.
enum RelicTheme : int { kWeapon, kArmor, kRegalia, kMagic, kWard, kAntique, kRelicThemes };
const char* const kRelicThemeName[kRelicThemes] = {"оружие", "доспехи", "регалии", "магия", "обереги", "древности"};
const char* const kRarityWord[int(Rarity::Count)] = {"обычная", "редкая", "эпическая", "легендарная", "эпохальная"};
std::vector<FxV> relicFx(int theme, int r) {
  static const double kLoyal[] = {0.5, 1, 2, 3, 5}, kGuard[] = {-3, -5, -10, -15, -25}, kContent[] = {0.2, 0.4, 0.6, 1, 2},
                      kIncome[] = {0.5, 1, 2, 3, 5}, kResearch[] = {-1, -2, -4, -6, -10}, kRebel[] = {-1, -2, -3, -5, -8},
                      kWardGuard[] = {-1, -2, -3, -5, -8}, kTrade[] = {1, 2, 4, 6, 10};
  switch (theme) {
    case kWeapon: return {{F::LoyaltyPerTurn, kLoyal[r]}};
    case kArmor: return {{F::AssassinPct, kGuard[r]}};
    case kRegalia: return {{F::ContentmentPerTurn, kContent[r]}, {F::IncomePct, kIncome[r]}};
    case kMagic: return {{F::ResearchTimePct, kResearch[r]}};
    case kWard: return {{F::RebellionPct, kRebel[r]}, {F::AssassinPct, kWardGuard[r]}};
    default: return {{F::TradePct, kTrade[r]}, {F::ArchTreasurePct, kTrade[r]}};
  }
}
std::vector<GenV> relicGen(int theme, int r) {
  if (theme == kMagic && r >= int(Rarity::Legendary)) return {{"Эссенция арканы", r == int(Rarity::Legendary) ? 5.0 : 10.0}};
  return {};
}
// Базовые реликвии (археологические находки и «Общие» артефакты версии 3) и их темы.
const std::vector<std::pair<const char*, int>>& relicThemes() {
  static const std::vector<std::pair<const char*, int>> v = {
      // Археологические находки.
      {"Древняя Амфора", kAntique}, {"Табличка с письменами", kAntique}, {"Древний клинок", kWeapon}, {"Древний доспех", kArmor},
      {"Древний шлем", kArmor}, {"Руна на камне", kMagic}, {"Древний посох", kMagic}, {"Древняя игрушка", kAntique},
      {"Свиток с поэзией", kAntique}, {"Древний инструмент", kAntique},
      {"Племенное ожерелье", kWard}, {"Окаменевший клык чудовища", kWard}, {"Древняя жемчужина", kRegalia}, {"Костяной амулет", kWard},
      {"Хопеш стража династии", kWeapon}, {"Щит с гравировкой Сокола", kArmor}, {"Лаванидский хитиновый панцирь", kArmor},
      {"Эмблема забытого королевства", kRegalia}, {"Древнее подношение", kWard}, {"Медаль Силанны", kRegalia},
      {"Секира древних гномов", kWeapon}, {"Кольцо вождя клана", kRegalia}, {"Кинжал Игг-Шайлской Ведьмы", kWeapon},
      {"Четки древнего шамана", kWard}, {"Диадема древнего аристократа", kRegalia}, {"Кулон знатного вампира древности", kWard},
      {"Элегия древности", kAntique}, {"Шантирийский кристалл активации", kMagic}, {"Беспроглядное зеркало Безликих", kMagic},
      {"Печать шантирийского архивариуса", kAntique},
      {"Сфера Азкаала", kMagic}, {"Шантирийский посох", kMagic}, {"Пылающий Клинок Дракона", kWeapon}, {"Шантирийский щит", kArmor},
      {"Шантирийский лук", kWeapon}, {"Топор Куньяка", kWeapon}, {"Перстень Лайама Сокола", kRegalia}, {"Шантирийское копье", kWeapon},
      {"Корона Императоров Грифона", kRegalia}, {"Корона Сандро", kRegalia},
      {"Кровавый камень Дракх'лукара", kMagic}, {"Книга Мертвых", kMagic}, {"Скипетр Сар-Илама", kRegalia},
      {"Слеза Первородной Богини", kWard}, {"Меч Первого Светоносного", kWeapon}, {"Семя Мирового Древа", kWard},
      {"Шкатулка Безымянного Бога", kMagic}, {"Корона Бессмертного Императора", kRegalia}, {"Пожиратель", kWeapon},
      {"Свиток тайны Вознесения", kMagic},
      // «Общие» артефакты.
      {"Стальной меч", kWeapon}, {"Железный кинжал", kWeapon}, {"Кожаный доспех", kArmor}, {"Деревянный щит", kArmor},
      {"Охотничий лук", kWeapon}, {"Бронзовый шлем", kArmor}, {"Боевой топор", kWeapon}, {"Копье ополченца", kWeapon},
      {"Посох странника", kMagic}, {"Кольчужная рубаха", kArmor},
      {"Посох Монаха", kMagic}, {"Руна Стихии", kMagic}, {"Связующий кристалл", kMagic}, {"Эльфийский плащ", kArmor},
      {"Гномий боевой молот", kWeapon}, {"Амулет защиты", kWard}, {"Кольцо силы", kRegalia}, {"Зачарованный кинжал", kWeapon},
      {"Серебряный меч охотника", kWeapon}, {"Шлем ветерана", kArmor},
      {"Лук из звездного серебра", kWeapon}, {"Мифриловый доспех", kArmor}, {"Доспех из темного обсидиана", kArmor},
      {"Клинок Багровой Гвардии", kWeapon}, {"Рунный Щит", kArmor}, {"Магический колчан", kWeapon}, {"Плащ ночной тени", kArmor},
      {"Посох архимага", kMagic}, {"Драконья чешуйчатая броня", kArmor}, {"Кольцо стихий", kMagic},
      {"Рунный клинок Падшего Принца", kWeapon}, {"Молот Светоносного", kWeapon}, {"Лук из рога Единорога", kWeapon},
      {"Перстень Императора Света", kRegalia}, {"Пламенный Искуситель", kWeapon}, {"Щит Тирионгарда", kArmor},
      {"Корона Небесного Престола", kRegalia}, {"Посох Вечного Пламени", kMagic}, {"Доспех Железного Короля", kArmor},
      {"Кинжал Тысячи Ядов", kWeapon},
      {"Ледяная Скорбь", kWeapon}, {"Посох Тысячи Теней", kMagic}, {"Корона Господства", kRegalia}, {"Коса Демитриоса", kWeapon},
      {"Меч Владыки Бездны", kWeapon}, {"Клинок Бессмертного Императора", kWeapon}, {"Меч Короля Дикой Охоты", kWeapon},
      {"Клинок Царской Династии", kWeapon}, {"Посох Наз'Ферата", kMagic}, {"Копье Поднебесного Владыки", kWeapon},
  };
  return v;
}

// ---------------------------------------------------------------- расы, культуры, религии, формы правления, должности
struct NamedFx {
  const char* name;
  std::vector<FxV> fx;
  std::vector<GenV> gen = {};
};
// Расы: локальные эффекты провинций, где раса преобладает.
const std::vector<NamedFx>& raceSpecs() {
  static const std::vector<NamedFx> v = {
      {"Ангелы", {{F::ContentmentPerTurn, 0.5}, {F::PopGrowthPct, -0.2}}},
      {"Арахниды", {{F::ResourcePct, 6}, {F::PopGrowthPct, 0.2}}},
      {"Арборианы", {{F::ResourcePct, 8}, {F::BuildCostPct, 3}}},
      {"Безликие", {{F::RebellionPct, -3}, {F::TradePct, -2}}},
      {"Вампиры", {{F::RebellionPct, -3}, {F::PopGrowthPct, -0.2}}},
      {"Великаны", {{F::BuildCostPct, -8}, {F::PopGrowthPct, -0.2}}},
      {"Высшие Эльфы", {{F::TradePct, 5}, {F::PopGrowthPct, -0.1}}},
      {"Гарпии", {{F::TradeFlat, 10}, {F::RebellionPct, 1}}},
      {"Гноллы", {{F::PopGrowthPct, 0.3}, {F::TradePct, -3}}},
      {"Гномы", {{F::ResourcePct, 10}, {F::BuildCostPct, -5}}},
      {"Гоблины", {{F::PopGrowthPct, 0.4}, {F::RebellionPct, 2}}},
      {"Дриады", {{F::PopGrowthPct, 0.2}, {F::ContentmentPerTurn, 0.2}}},
      {"Демоны", {{F::RebellionPct, -2}, {F::ContentmentPerTurn, -0.3}}},
      {"Ибисианцы", {{F::TradePct, 3}, {F::ContentmentPerTurn, 0.2}}},
      {"Канидисы", {{F::ResourcePct, 4}, {F::RebellionPct, -1}}},
      {"Кархароны", {{F::TradeFlat, 15}, {F::RebellionPct, 1}}},
      {"Кентавры", {{F::TradePct, 3}, {F::ResourcePct, 3}}},
      {"Кецали", {{F::TradeFlat, 15}, {F::ContentmentPerTurn, 0.2}}},
      {"Кицунэ", {{F::TradePct, 5}, {F::RebellionPct, 1}}},
      {"Кобольды", {{F::ResourcePct, 6}, {F::RebellionPct, 1}}},
      {"Кристаллиты", {{F::ResourceFlat, 2}, {F::PopGrowthPct, -0.2}}},
      {"Кровавые Эльфы", {{F::ResourcePct, 5}, {F::ContentmentPerTurn, -0.2}}},
      {"Крысолаки", {{F::PopGrowthPct, 0.4}, {F::ContentmentPerTurn, -0.3}}},
      {"Ктониды", {{F::ResourceFlat, 3}, {F::PopGrowthPct, -0.1}}},
      {"Лаваниды", {{F::BuildCostPct, -5}, {F::PopGrowthPct, -0.1}}},
      {"Лесные Эльфы", {{F::ResourcePct, 5}, {F::PopGrowthPct, -0.1}}},
      {"Луниды", {{F::ContentmentPerTurn, 0.3}, {F::TradePct, 2}}},
      {"Лунные Эльфы", {{F::ContentmentPerTurn, 0.3}, {F::RebellionPct, -1}}},
      {"Люди", {{F::PopGrowthPct, 0.2}, {F::TradePct, 3}}},
      {"Людоящеры", {{F::PopGrowthPct, 0.2}, {F::RebellionPct, -1}}},
      {"Минотавры", {{F::BuildCostPct, -3}, {F::RebellionPct, 2}}},
      {"Морозные Эльфы", {{F::RebellionPct, -2}, {F::PopGrowthPct, -0.1}}},
      {"Морские Эльфы", {{F::TradeFlat, 20}, {F::TradePct, 2}}},
      {"Наги", {{F::TradeFlat, 20}, {F::PopGrowthPct, -0.1}}},
      {"Огры", {{F::BuildCostPct, -4}, {F::TradePct, -3}}},
      {"Они", {{F::BuildCostPct, -4}, {F::ContentmentPerTurn, -0.2}}},
      {"Орки", {{F::PopGrowthPct, 0.3}, {F::ContentmentPerTurn, -0.3}}},
      {"Падшие Эльфы", {{F::ResourcePct, 5}, {F::RebellionPct, 2}}},
      {"Пустынные Эльфы", {{F::TradePct, 4}, {F::PopGrowthPct, -0.1}}},
      {"Ракшасы", {{F::TradePct, 4}, {F::RebellionPct, 1}}},
      {"Сатиры", {{F::ContentmentPerTurn, 0.4}, {F::ResourcePct, -3}}},
      {"Светлые Эльфы", {{F::ContentmentPerTurn, 0.3}, {F::PopGrowthPct, -0.1}}},
      {"Сумеречные Эльфы", {{F::RebellionPct, -2}, {F::TradePct, 2}}},
      {"Сянмаорены", {{F::TradePct, 4}, {F::ContentmentPerTurn, 0.2}}},
      {"Темные Эльфы", {{F::TradePct, 4}, {F::RebellionPct, 1}}},
      {"Троглодиты", {{F::ResourceFlat, 2}, {F::ContentmentPerTurn, -0.2}}},
      {"Тролли", {{F::PopGrowthPct, 0.2}, {F::BuildCostPct, 3}}},
      {"Циклопы", {{F::ResourcePct, 8}, {F::PopGrowthPct, -0.2}}},
      {"Ши-цзы", {{F::RebellionPct, -2}, {F::ContentmentPerTurn, 0.2}}},
      {"Эльфы Бездны", {{F::ResourcePct, 5}, {F::ContentmentPerTurn, -0.2}}},
      {"Энты", {{F::ResourcePct, 5}, {F::BuildCostPct, -3}, {F::PopGrowthPct, -0.3}}},
  };
  return v;
}
// Культуры: локальные эффекты — провинциям этой культуры и всем провинциям государств этой культуры, глобальные —
// государствам этой культуры (rules/mods.cpp, FxSum::trait).
const std::vector<NamedFx>& cultureSpecs() {
  static const std::vector<NamedFx> v = {
      {"Абиссальные падшие", {{F::ResourcePct, 4}, {F::LoyaltyPerTurn, 0.5}}},
      {"Авентинская", {{F::TradePct, 3}, {F::IncomePct, 2}}},
      {"Анимисты великого духа", {{F::PopGrowthPct, 0.1}, {F::ContentmentPerTurn, 0.2}}},
      {"Бубонный синкретизм", {{F::RebellionPct, -2}, {F::PopGrowthPct, -0.1}}},
      {"Валькирионская", {{F::LoyaltyPerTurn, 1}, {F::ContentmentPerTurn, 0.1}}},
      {"Велесов хлад", {{F::RebellionPct, -1}, {F::PopGrowthPct, 0.1}}},
      {"Ветроградская", {{F::TradePct, 3}, {F::FleetUpkeepPct, -3}}},
      {"Вечный Холод", {{F::RebellionPct, -1}, {F::ArmyUpkeepPct, -3}}},
      {"Девяти Вуалей", {{F::AssassinGuardPct, -5}, {F::PlotAssassinPct, 3}}},
      {"Династия Шелковой Нити", {{F::TradePct, 5}, {F::IncomePct, 1}}},
      {"Друидический ордо-натурализм", {{F::PopGrowthPct, 0.2}, {F::ResearchTimePct, -2}}},
      {"Железные налетчики", {{F::LoyaltyPerTurn, 0.5}, {F::FleetUpkeepPct, -5}}},
      {"Жемчужной Волны", {{F::TradeFlat, 15}, {F::FleetUpkeepPct, -3}}},
      {"Иеро-хронология", {{F::ResearchTimePct, -3}, {F::ArchSuccessPct, 1}}},
      {"Канабо-Авангард", {{F::LoyaltyPerTurn, 1}, {F::ArmyUpkeepPct, 2}}},
      {"Клан Клятвы", {{F::RebellionPct, -2}, {F::LoyaltyPerTurn, 0.5}}},
      {"Кланы Игнис", {{F::ResourcePct, 4}, {F::LoyaltyPerTurn, 0.5}}},
      {"Кузнецы Сердца Горнила", {{F::BuildCostPct, -5}, {F::ArmyUpkeepPct, -2}}},
      {"Культ Солнца", {{F::ContentmentPerTurn, 0.3}, {F::LoyaltyPerTurn, 0.5}}},
      {"Ланикейская", {{F::TradePct, 4}, {F::FleetUpkeepPct, -3}}},
      {"Люминорская", {{F::ContentmentPerTurn, 0.3}, {F::IncomePct, 1}}},
      {"Люминорская-Бастионная", {{F::BuildCostPct, -3}, {F::ArmyUpkeepPct, -3}}},
      {"Магократическая", {{F::ResearchTimePct, -5}, {F::ContentmentPerTurn, -0.2}}},
      {"Мутагенный уклад Рат'гарда", {{F::PopGrowthPct, 0.2}, {F::ContentmentPerTurn, -0.3}}},
      {"Небесный Мандат", {{F::RebellionPct, -2}, {F::IncomePct, 1}}},
      {"Нити Асхи", {{F::ContentmentPerTurn, 0.2}, {F::AssassinGuardPct, -3}}},
      {"Номократическая", {{F::RebellionPct, -2}, {F::IncomePct, 1}}},
      {"Нордлинги", {{F::RebellionPct, -1}, {F::FleetUpkeepPct, -5}}},
      {"Обсидиановая", {{F::ResourcePct, 5}, {F::AssassinGuardPct, -3}}},
      {"Оплот Пагоды", {{F::RebellionPct, -2}, {F::BuildCostPct, -2}}},
      {"Орда кланов", {{F::LoyaltyPerTurn, 1}, {F::ContentmentPerTurn, -0.2}}},
      {"Отрекшиеся рунологи", {{F::ResearchTimePct, -4}, {F::ContentmentPerTurn, -0.2}}},
      {"Паразитарный декаданс", {{F::IncomePct, 2}, {F::ContentmentPerTurn, -0.3}}},
      {"Первородство льда", {{F::RebellionPct, -2}, {F::ResearchTimePct, 2}}},
      {"Плетение", {{F::ResearchTimePct, -3}, {F::ContentmentPerTurn, 0.1}}},
      {"Племена Железа", {{F::ResourcePct, 4}, {F::ArmyUpkeepPct, -2}}},
      {"Племена Необузданной стали", {{F::LoyaltyPerTurn, 1}, {F::BuildCostPct, 3}}},
      {"Племена неба и земли", {{F::PopGrowthPct, 0.1}, {F::ResourcePct, 3}}},
      {"Призматическая", {{F::TradePct, 3}, {F::ResearchTimePct, -2}}},
      {"Селахийская", {{F::TradePct, 3}, {F::ResearchTimePct, -2}}},
      {"Селениты", {{F::ContentmentPerTurn, 0.2}, {F::ResearchTimePct, -2}}},
      {"Сильванитская", {{F::PopGrowthPct, 0.1}, {F::ResearchTimePct, -2}}},
      {"Спиритуалисты тотемов", {{F::RebellionPct, -1}, {F::LoyaltyPerTurn, 0.5}}},
      {"Священный Сплав", {{F::BuildCostPct, -3}, {F::ContentmentPerTurn, 0.2}}},
      {"Сянмао-философия", {{F::ContentmentPerTurn, 0.3}, {F::ResearchTimePct, -3}}},
      {"Танато-аристократическая", {{F::PopGrowthPct, -0.1}, {F::IncomePct, 3}}},
      {"Торгарская", {{F::TradePct, 5}, {F::IncomePct, 1}}},
      {"Триархия тьмы", {{F::RebellionPct, -2}, {F::PlotAssassinPct, 5}}},
      {"Умбральская", {{F::RebellionPct, -1}, {F::AssassinGuardPct, -5}}},
      {"Фьордовые собиратели", {{F::ResourcePct, 4}, {F::FleetUpkeepPct, -5}}},
      {"Хтонический декаданс", {{F::ContentmentPerTurn, 0.3}, {F::ArmyUpkeepPct, 3}}},
      {"Хтоническая", {{F::ResourceFlat, 2}, {F::RebellionPct, 1}}},
      {"Черных Скал", {{F::BuildCostPct, -3}, {F::AssassinGuardPct, -3}}},
      {"Эйварская", {{F::ResourcePct, 4}, {F::ResearchTimePct, -3}}},
      {"Ягуаровые Потрошители", {{F::PlotAssassinPct, 5}, {F::RebellionPct, -1}}},
  };
  return v;
}
// Религии: общий модификатор веры для религий одной темы (действует, как модификатор культуры).
struct FaithSpec {
  const char* name;
  u32 color;
  std::vector<FxV> fx;
  std::vector<const char*> religions;
};
const std::vector<FaithSpec>& faithSpecs() {
  static const std::vector<FaithSpec> v = {
      {"Свет", 0xffd86a, {{F::ContentmentPerTurn, 0.3}, {F::RebellionPct, -1}},
       {"Церковь света", "Орден Вечного Пламени", "Небесный Конклав", "Церковь Священного Истока", "Орден Изначального Слова",
        "Орден Сияющих Граней"}},
      {"Смерть", 0x8f9a86, {{F::RebellionPct, -2}, {F::PopGrowthPct, -0.1}, {F::LoyaltyPerTurn, 0.5}},
       {"Культ Смерти", "Культ Мора", "Черный Сонм Наз'ферата"}},
      {"Природа", 0x4fc24f, {{F::PopGrowthPct, 0.2}, {F::ResourcePct, 2}},
       {"Изумрудный Круг", "Плетение Дикой Лозы", "Вече Предвечных Духов", "Слушающие Тотемы", "Сонм Тотема Медведя",
        "Шаманы Отца Неба и Матери Земли"}},
      {"Море и ветер", 0x2f8fff, {{F::TradePct, 2}, {F::FleetUpkeepPct, -5}},
       {"Ковенант Приливов", "Церковь Глубинного Милосердия", "Вольное Братство Бури", "Вече Вольного Ветра", "Церковь Зимнего Змея",
        "Храм Шалассы", "Церковь Илата"}},
      {"Ремесло", 0xa07a4a, {{F::BuildCostPct, -3}, {F::ResourcePct, 3}},
       {"Кузница Огненного Слова", "Стражи Нерушимого Камня", "Синод Арката"}},
      {"Знание", 0x8b6bff, {{F::ResearchTimePct, -4}, {F::ArchExpPct, 5}},
       {"Искрящий Синод", "Ложа Сокрытого Знания", "Завеса Шепчущих Истин", "Конклав Архитекторов Судьбы", "Академия Семи Сфер",
        "Священный Календарь Итца", "Династия Вечного Начертания", "Последователи Сар-Илама"}},
      {"Власть", 0xd9a441, {{F::RebellionPct, -2}, {F::IncomePct, 1}},
       {"Иерархия Высшего Трона", "Высший Синод Высокомерия", "Иерархия Пернатого Змея", "Династия Солнечного Ветра",
        "Патрициат Орлиного Крыла"}},
      {"Война", 0xc0503a, {{F::LoyaltyPerTurn, 1}, {F::ArmyUpkeepPct, -3}},
       {"Орден Сокрушающей Длани", "Легион Кипящей Крови", "Союз Железного Кулака", "Клятвенники Чести", "Драккары Ледяного Савана"}},
      {"Тьма", 0x6a4fb0, {{F::RebellionPct, -1}, {F::AssassinGuardPct, -5}, {F::PlotAssassinPct, 5}},
       {"Секта Семи Вуалей", "Палата Первородной Ночи", "Орден Черного Солнца", "Стражи Серой Завесы", "Синод Шепчущей Бездны",
        "Теократия Невыразимого Имени", "Ковенант Малассы", "Шаманы темного тотема"}},
      {"Кровь", 0xd0233f, {{F::RebellionPct, -1}, {F::PopGrowthPct, -0.1}, {F::LoyaltyPerTurn, 0.5}},
       {"Церковь Алого Истока", "Багряная Династия Валахии", "Гнездо Первородного Голода", "Пантеон Чистой Крови", "Орден Пожирателей",
        "Культ Кровавого Прилива"}},
      {"Хаос", 0xe0428f, {{F::ContentmentPerTurn, -0.2}, {F::LoyaltyPerTurn, 1}},
       {"Братство Изменчивого Ока", "Культ Сломанного Трона", "Секта Искаженного Эха", "Культ Ургаша"}},
      {"Наслаждения", 0xe0a82f, {{F::ContentmentPerTurn, 0.5}, {F::IncomePct, -1}},
       {"Культ Удовольствий", "Ковенант Наслаждений", "Храм Пяти Хмельных Рассветов"}},
      {"Торговля", 0xe2b33c, {{F::TradePct, 3}, {F::IncomePct, 2}},
       {"Великий Доминион Шелковой Зари", "Талассократия Семи Ветров", "Синдикат Пустого Зеркала", "Гильдия Невидимой Руки"}},
      {"Равновесие", 0x5fd8c0, {{F::ContentmentPerTurn, 0.2}, {F::RebellionPct, -1}, {F::AssassinGuardPct, -2}},
       {"Орден Света Пустоты", "Орден Асхи"}},
  };
  return v;
}
// Формы правления: глобальные эффекты.
const std::vector<NamedFx>& governmentSpecs() {
  static const std::vector<NamedFx> v = {
      {"Монархия", {{F::IncomePct, 2}, {F::LoyaltyPerTurn, 0.5}}},
      {"Республика", {{F::IncomePct, 4}, {F::ResearchTimePct, -5}, {F::LoyaltyPerTurn, -0.5}}},
      {"Теократия", {{F::LoyaltyPerTurn, 0.5}, {F::AssassinGuardPct, -5}, {F::ResearchTimePct, 5}}},
      {"Олигархия", {{F::IncomePct, 5}, {F::ArmyUpkeepPct, 5}}},
      {"Магократия", {{F::ResearchTimePct, -10}, {F::ArmyUpkeepPct, 5}}},
      {"Империя", {{F::ArmyUpkeepPct, -5}, {F::FleetUpkeepPct, -5}, {F::ResearchTimePct, 5}}},
      {"Племенной союз", {{F::ArmyUpkeepPct, -10}, {F::LoyaltyPerTurn, 0.5}, {F::ResearchTimePct, 10}}},
      {"Деспотия", {{F::LoyaltyPerTurn, 1}, {F::AssassinGuardPct, -10}, {F::IncomePct, -3}}},
  };
  return v;
}
// Должности совета: модификатор, пока должность занята, и модификатор, пока она пустует (глобальные: их локальные
// эффекты действуют во всех провинциях государства).
struct PositionSpec {
  const char* name;
  std::vector<FxV> taken;
  std::vector<GenV> gen;
  std::vector<FxV> vacant;
};
const std::vector<PositionSpec>& positionSpecs() {
  static const std::vector<PositionSpec> v = {
      {"Десница", {{F::IncomePct, 2}, {F::ResearchTimePct, -3}}, {}, {{F::IncomePct, -1}}},
      {"Лорд-Мастер над экономикой", {{F::IncomePct, 5}, {F::ArchUpkeepPct, -10}}, {}, {{F::IncomePct, -2}}},
      {"Лорд-Мастер над войной", {{F::ArmyUpkeepPct, -5}, {F::LoyaltyPerTurn, 0.5}}, {}, {{F::ArmyUpkeepPct, 3}}},
      {"Лорд-Мастер над флотом", {{F::FleetUpkeepPct, -10}}, {}, {{F::FleetUpkeepPct, 5}}},
      {"Лорд-Мастер над разведкой", {{F::PlotAssassinPct, 10}, {F::AssassinGuardPct, -10}}, {}, {{F::AssassinGuardPct, 5}}},
      {"Хранитель знаний", {{F::ResearchTimePct, -5}, {F::ArchExpPct, 10}}, {}, {{F::ResearchTimePct, 3}}},
      {"Патриарх", {{F::ContentmentPerTurn, 0.5}}, {}, {{F::ContentmentPerTurn, -0.2}}},
      {"Верховный магистр", {{F::LoyaltyPerTurn, 1}, {F::AssassinGuardPct, -5}}, {}, {{F::LoyaltyPerTurn, -0.5}}},
      {"Архимаг", {{F::ResearchTimePct, -5}}, {{"Эссенция арканы", 5}}, {{F::ResearchTimePct, 2}}},
      {"Верховный друид", {{F::PopGrowthPct, 0.2}, {F::ArchVitalityPct, 3}}, {{"Эссенция природы", 5}}, {{F::PopGrowthPct, -0.1}}},
  };
  return v;
}

// ---------------------------------------------------------------- особые отряды
struct SpecialSpec {
  const char* name;
  UnitType type;
  const char* race;                   // "" — по виду государства и типу
  Cost extra;                         // ресурсы на юнит
  Cost essence;                       // эссенции на юнит (найм)
  double upkeep = 0;                  // золото на юнит в ход
  double essUpkeep = 0;               // элементали: эссенции найма на юнит в ход
  const char* accessKey = nullptr;    // постройка доступа: ключ (schema::bld)…
  const char* accessName = nullptr;   // …или название общей постройки
  const char* desc = "";
};
const std::vector<SpecialSpec>& specialSpecs() {
  using U = UnitType;
  static const std::vector<SpecialSpec> v = {
      // Стрелки и пехота (ТЗ аудита 7.8, 7.6): порох, топливо, магический порох, дамасская сталь, лунное серебро, хитин,
      // прочная чешуя, мех.
      {"Мушкетеры", U::Ranged, "", {{"Порох", 1}, {"Сталь", 1}}, {}, 0.003, 0, schema::bld::WarWorkshop, nullptr,
       "Огнестрельное оружие: порох и сталь на каждого."},
      {"Огнеметчики", U::Ranged, "", {{"Топливо", 2}, {"Сталь", 1}}, {}, 0.004, 0, schema::bld::WarWorkshop, nullptr,
       "Огнеметы на топливе."},
      {"Стрелки магического пороха", U::Ranged, "", {{"Магический порох", 1}, {"Сталь", 1}}, {{"Эссенция арканы", 1}}, 0.004, 0,
       schema::bld::AlchemyConverter, nullptr, "Чародейские ружья на магическом порохе."},
      {"Арбалетчики дамасской стали", U::Ranged, "", {{"Дамасская сталь", 1}}, {}, 0.003, 0, nullptr, "Оружейная палата",
       "Тяжелые арбалеты из дамасской стали."},
      {"Стрелки лунного серебра", U::Ranged, "", {{"Лунное серебро", 1}}, {{"Эссенция сумерек", 1}}, 0.004, 0, nullptr, "Оружейная палата",
       "Стрелы с наконечниками из лунного серебра."},
      {"Хитиновые стражи", U::HeavyInf, "", {{"Хитин", 2}}, {}, 0.003, 0, nullptr, "Оружейная палата", "Доспехи из хитина."},
      {"Чешуйчатые латники", U::HeavyInf, "", {{"Прочная чешуя", 2}, {"Сталь", 1}}, {}, 0.004, 0, nullptr, "Оружейная палата",
       "Доспехи из прочной чешуи."},
      {"Меховые налетчики", U::MediumInf, "", {{"Мех", 2}}, {}, 0.002, 0, nullptr, "Оружейная палата", "Налетчики в меховых доспехах."},
      // Элементали соборов (ТЗ аудита 7.7): эссенции, которые прежде некуда было тратить.
      {"Земляные элементали", U::Elementals, schema::kRaceElemental, {}, {{"Эссенция земли", 20}}, 0, 0.2, nullptr, "Собор земли", ""},
      {"Воздушные элементали", U::Elementals, schema::kRaceElemental, {}, {{"Эссенция воздуха", 20}}, 0, 0.2, nullptr, "Собор воздуха", ""},
      {"Духи природы", U::Elementals, schema::kRaceElemental, {}, {{"Эссенция природы", 20}}, 0, 0.2, nullptr, "Собор природы", ""},
      {"Ледяные элементали", U::Elementals, schema::kRaceElemental, {}, {{"Эссенция льда", 20}}, 0, 0.2, nullptr, "Собор льда", ""},
      {"Элементали арканы", U::Elementals, schema::kRaceElemental, {}, {{"Эссенция арканы", 20}}, 0, 0.2, nullptr, "Собор арканы", ""},
      {"Элементали господства", U::Elementals, schema::kRaceElemental, {}, {{"Эссенция господства", 25}}, 0, 0.25, nullptr,
       "Собор господства", ""},
      {"Сумеречные элементали", U::Elementals, schema::kRaceElemental, {}, {{"Эссенция сумерек", 20}}, 0, 0.2, nullptr, "Собор сумерек", ""},
      {"Духи гармонии", U::Elementals, schema::kRaceElemental, {}, {{"Эссенция гармонии", 20}}, 0, 0.2, nullptr, "Собор гармонии", ""},
  };
  return v;
}

// ---------------------------------------------------------------- постройки
// Добавка к цене уровня базовой постройки (ключ или название; уровень с 1): ресурс, которого в цене ещё нет.
struct CostAdd {
  const char* key;
  const char* name;
  int level;
  const char* res;
  double v;
};
const CostAdd kCostAdds[] = {
    {schema::bld::Port, nullptr, 5, "Жемчуг", 100},
    {schema::bld::Market, nullptr, 4, "Жемчуг", 50},
    {schema::bld::Market, nullptr, 5, "Платина", 50},
    {schema::bld::RelicVault, nullptr, 1, "Платина", 50},
    {schema::bld::Barracks, nullptr, 1, "Мех", 50},
    {schema::bld::PlagueLab, nullptr, 1, "Хитин", 50},
    {schema::bld::NecroEnricher, nullptr, 1, "Кость", 100},
    {schema::bld::Shipyard, nullptr, 3, "Титан", 100},
    {nullptr, "Чертоги доблести", 1, "Титан", 1000},
    {nullptr, "Чертоги доблести", 1, "Мех", 1000},
    {nullptr, "Шантирийская фабрика титанов", 1, "Титан", 2000},
    {nullptr, "Храм чистейшего света", 1, "Платина", 1000},
    {nullptr, "Цитадель Бездны", 1, "Метеоритная руда", 1000},
    {nullptr, "Цитадель Бездны", 1, "Прочная чешуя", 1000},
    {nullptr, "Невидимая библиотека", 1, "Метеоритная руда", 500},
    {nullptr, "Невидимая библиотека", 1, "Шелк", 500},
    {nullptr, "Огненное гнездовье", 1, "Метеоритная руда", 500},
    {nullptr, "Огненное гнездовье", 1, "Прочная чешуя", 500},
    {nullptr, "Собор воды", 1, "Жемчуг", 200},
    {nullptr, "Мавзолей Короля Лича", 1, "Кость", 2000},
    {nullptr, "Глубинная Бездна", 1, "Хитин", 1000},
};
// Модификаторы уровней базовых построек (ТЗ аудита 7.9: торговая ценность количеством, содержание флота).
struct LevelMods {
  const char* key;
  const char* title;
  const char* icon;
  u32 color;
  ModKind kind;
  Fx fx;
  std::vector<double> perLevel;
  const char* desc;
};
const std::vector<LevelMods>& levelMods() {
  static const std::vector<LevelMods> v = {
      {schema::bld::Port, "Порт", "anchor", 0x3f8ab0, ModKind::Province, F::TradeFlat, {10, 20, 30, 40, 50}, "Торговая ценность провинции с портом."},
      {schema::bld::Market, "Рынок", "coins", 0xd6a531, ModKind::Province, F::TradePct, {2, 4, 6, 8, 10}, "Торговая ценность провинции с рынком."},
      {schema::bld::Shipyard, "Верфь", "shipyard", 0x5b8fd6, ModKind::Faction, F::FleetUpkeepPct, {-2, -4, -6}, "Содержание флота государства."},
  };
  return v;
}
const char* const kRoman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};

// Ветка «Стимуляция промышленности»: ступени множителя и сроки исследования.
struct IndustryStep {
  int mult;
  int turns;
};
const IndustryStep kIndustry[] = {{2, 3}, {5, 4}, {10, 5}, {20, 6}, {35, 7}, {60, 8}, {100, 10}};

// Общие постройки без ключа, которые считаются базовыми (сроки строительства по цене).
const char* const kBaseNamed[] = {
    "Цитадель Бездны",          "Мавзолей Короля Лича",       "Огненное гнездовье",          "Чертоги доблести",
    "Храм чистейшего света",    "Невидимая библиотека",       "Глубинная Бездна",            "Цитадель забытой династии",
    "Шантирийская фабрика големов", "Шантирийская фабрика титанов", "Шантирийский обогатитель", "Шантирийская кузня",
    "Шантирийская мастерская",  "Нефтяная вышка",             "Ткацкая мастерская",          "Оружейная палата",
};

// ================================================================ контекст шага
// Seeder и поиск по названию (ключ сравнения): ресурсы, эссенции, свои модификаторы (без перебора на каждый запрос).
struct V6 {
  Seeder& s;
  std::unordered_map<std::string, Id> res, ess, mods;

  Tx& tx() { return s.tx; }
  Catalogs& cat() { return s.cat(); }
  void index() {
    res.clear();
    ess.clear();
    mods.clear();
    for (const CatalogItem& r : cat().resources) res.emplace(key(r.name), r.id);
    for (const CatalogItem& e : cat().essences) ess.emplace(key(e.name), e.id);
    s.tx.w().modifiers.each([&](const Modifier& m) {
      if (m.key.empty()) mods.emplace(key(m.name), m.id);
    });
  }
  Id resId(std::string_view n) const {
    if (n == "Золото") return kGold;
    auto it = res.find(key(n));
    return it == res.end() ? 0 : it->second;
  }
  Id essId(std::string_view n) const {
    auto it = ess.find(key(n));
    return it == ess.end() ? 0 : it->second;
  }
  std::map<Id, double> cost(const Cost& list) const {
    std::map<Id, double> m;
    for (auto& [n, v] : list)
      if (Id r = resId(n)) m[r] = v;
    return m;
  }
  // Свой модификатор с названием: есть — его ID (не меняется), нет — создаётся.
  Id mod(const ModSpec& d) {
    const std::string k = key(d.name);
    if (auto it = mods.find(k); it != mods.end() && s.tx.w().modifier(it->second)) return it->second;
    Modifier m;
    m.name = d.name;
    m.kind = d.kind;
    m.icon = d.icon;
    m.color = Color::hex(d.color);
    m.desc = d.desc;
    for (const FxV& f : d.fx) {
      m.fx[size_t(f.fx)] = f.v;
      m.fxMask |= 1u << int(f.fx);
    }
    for (const GenV& g : d.gen) {
      const Id ref = g.res ? resId(g.name) : essId(g.name);
      if (ref) (g.res ? m.resGen : m.essGen)[ref] = g.v;
    }
    const Id id = s.tx.add(std::move(m)).id;
    mods[k] = id;
    s.modifiers++;
    return id;
  }
  Id building(const char* bkey) {
    return bkey ? s.tx.w().buildings.firstId([&](const Building& b) { return b.owner == 0 && b.key == bkey; }) : 0;
  }
  Id buildingOf(const char* bkey, const char* name) { return bkey ? building(bkey) : s.commonBuilding(name); }
};

// Строки армий особого отряда повторяют его запись (как rules::setSpecial).
void syncRows(V6& c, Id special) {
  const SpecialUnit* sp = c.cat().special(special);
  if (!sp) return;
  const SpecialUnit S = *sp;
  std::vector<Id> fids;
  c.tx().w().factions.each([&](const Faction& f) {
    for (const ArmyRow& r : f.army)
      if (r.special == special) {
        fids.push_back(f.id);
        break;
      }
  });
  for (Id fid : fids)
    for (ArmyRow& r : c.tx().faction(fid).army) {
      if (r.special != special) continue;
      r.type = S.type;
      r.race = S.race;
      r.keyRes = S.keyRes;
      r.keyPer = S.keyPer;
      r.extra = S.extra;
      r.essence = S.essence;
      r.upkeep = S.upkeep;
      r.essUpkeep = S.essUpkeep;
    }
}

u32 rgbOf(Color c) { return (u32(c.r) << 16) | (u32(c.g) << 8) | u32(c.b); }

template <class List>
void sortByName(List& list, bool goldFirst = false) {
  std::stable_sort(list.begin(), list.end(), [&](const auto& a, const auto& b) {
    if (goldFirst && (a.id == kGold) != (b.id == kGold)) return a.id == kGold;
    return compareRu(a.name, b.name) < 0;
  });
}

// ================================================================ шаги
// ---- названия (ТЗ аудита 7.15)
void stepRenames(V6& c) {
  Catalogs& k = c.cat();
  if (HeroClass* x = byName(k.classes, "Палладин"); x && !byName(k.classes, "Паладин")) {
    x->name = "Паладин";
    c.s.notes.push_back("класс «Палладин» переименован в «Паладин»");
  }
  for (HeroClass& x : k.classes)
    if (SubClass* sc = byName(x.subclasses, "Палладин-Гармонии"); sc && !byName(x.subclasses, "Паладин-Гармонии")) sc->name = "Паладин-Гармонии";
  for (CatalogItem& x : k.cultures)
    if (x.name == "Мутагенный уклад Рат`гарда" && !byName(k.cultures, "Мутагенный уклад Рат'гарда")) {
      x.name = "Мутагенный уклад Рат'гарда";
      sortByName(k.cultures);
      break;
    }
  // Ресурсы-чудовища с названиями рас: различимые названия.
  static const std::pair<const char*, const char*> kRes[] = {
      {"Великаны", "Дикие Великаны"}, {"Циклопы", "Дикие Циклопы"}, {"Ракшасы", "Дикие Ракшасы"}, {"Энты", "Древние Энты"}};
  const Id beasts = k.groupId(schema::grp::Beasts);
  bool renamed = false;
  for (auto [from, to] : kRes)
    if (CatalogItem* r = byName(k.resources, from); r && beasts && k.inGroup(r->group, beasts) && !byName(k.resources, to)) {
      r->name = to;
      renamed = true;
    }
  if (renamed) {
    sortByName(k.resources, true);
    c.s.notes.push_back("ресурсы «Великаны», «Циклопы», «Ракшасы», «Энты» переименованы (их названия носят расы)");
  }
  // Особый отряд с названием ресурса «Золотые Драконы» (его ключевого ресурса).
  if (SpecialUnit* sp = byName(k.specials, "Золотые Драконы"); sp && !byName(k.specials, "Драконы Чистейшего Света")) {
    sp->name = "Драконы Чистейшего Света";
    const Id id = sp->id;
    std::vector<Id> fids;
    c.tx().w().factions.each([&](const Faction& f) {
      for (const ArmyRow& r : f.army)
        if (r.special == id) {
          fids.push_back(f.id);
          break;
        }
    });
    for (Id fid : fids)
      for (ArmyRow& r : c.tx().faction(fid).army)
        if (r.special == id && key(r.name) == key("Золотые Драконы")) r.name = "Драконы Чистейшего Света";
    c.s.notes.push_back("особый отряд «Золотые Драконы» переименован в «Драконы Чистейшего Света»");
  }
}

// ---- «Стимуляция промышленности»: 7 ступеней вместо 99 технологий (ТЗ аудита 7.10)
void stepIndustry(V6& c) {
  Tx& tx = c.tx();
  std::map<int, Id> old;   // множитель → технология прежней ветки
  tx.w().techs.each([&](const Tech& t) {
    if (t.faction != 0) return;
    if (const int m = schema::industryMultOf(t.key)) old.emplace(m, t.id);
  });
  auto isStep = [](int m) {
    for (const IndustryStep& s : kIndustry)
      if (s.mult == m) return true;
    return false;
  };
  bool fresh = old.empty();
  for (auto& [m, id] : old) fresh = fresh || !isStep(m);
  if (!fresh && old.size() == std::size(kIndustry)) return;   // ветка уже в новом виде
  // Изученность прежней ветки: наибольший изученный множитель фракции; идущие исследования прекращаются с возвратом.
  std::map<Id, int> top;
  std::vector<Id> fids;
  tx.w().factions.each([&](const Faction& f) {
    for (auto& [m, tid] : old)
      if (f.techs.count(tid)) {
        fids.push_back(f.id);
        break;
      }
  });
  for (Id fid : fids) {
    Faction& f = tx.faction(fid);
    int best = 0;
    for (auto& [m, tid] : old) {
      auto it = f.techs.find(tid);
      if (it == f.techs.end()) continue;
      if (it->second.studied) best = std::max(best, m);
      else if (it->second.research)
        for (auto& [res, v] : it->second.paid)
          if (std::isfinite(v) && v > 0 && tx.w().resource(res)) f.res[res] += v;
      f.techs.erase(it);
    }
    if (best) top[fid] = best;
  }
  // Ступени: прежняя технология с тем же ключом становится ступенью, остальные удаляются.
  double y = 0;
  if (auto it = old.find(2); it != old.end()) {
    y = tx.w().tech(it->second)->pos.y;
  } else {
    bool any = false;
    tx.w().techs.each([&](const Tech& t) {
      if (t.faction != 0) return;
      y = any ? std::max(y, t.pos.y) : t.pos.y;
      any = true;
    });
    if (any) y += 200;
  }
  std::unordered_set<Id> oldIds;
  for (auto& [m, id] : old) oldIds.insert(id);
  const Id parts = c.resId("Механические детали");
  std::vector<std::pair<int, Id>> steps;
  Id prev = 0;
  for (size_t i = 0; i < std::size(kIndustry); i++) {
    const int m = kIndustry[i].mult;
    Id id = 0;
    if (auto it = old.find(m); it != old.end()) id = it->second;
    if (!id) {
      Tech t;
      t.faction = 0;
      t.key = schema::industryKey(m);
      id = tx.add(std::move(t)).id;
      c.s.techs++;
    }
    Tech& t = tx.tech(id);
    t.name = "Стимуляция промышленности ×" + std::to_string(m);
    t.desc = "Открывает множитель производства до ×" + std::to_string(m) + " построек преобразования.";
    t.turns = kIndustry[i].turns;
    t.cost.clear();
    t.cost[kGold] = schema::kIndustryTechGold * m;
    if (parts) t.cost[parts] = schema::kIndustryTechParts * m;
    std::vector<Id> pre;
    for (Id p : t.prereqs)
      if (!oldIds.count(p)) pre.push_back(p);
    if (prev) pre.push_back(prev);
    t.prereqs = pre;
    t.pos = Vec2{double(i) * schema::kTreeColStep, y};
    t.studied = t.research = false;
    t.progress = 0;
    t.paid.clear();
    steps.push_back({m, id});
    prev = id;
  }
  // Удалённые ступени: ссылки на них (условия технологий, требования построек) — на ближайшую ступень не ниже.
  auto stepFor = [&](int m) {
    for (auto& [sm, sid] : steps)
      if (sm >= m) return sid;
    return steps.back().second;
  };
  std::map<Id, Id> remap;
  for (auto& [m, tid] : old)
    if (!isStep(m)) remap[tid] = stepFor(m);
  auto fix = [&](std::vector<Id>& v) {
    std::vector<Id> out;
    for (Id x : v) {
      auto it = remap.find(x);
      const Id y2 = it == remap.end() ? x : it->second;
      if (std::find(out.begin(), out.end(), y2) == out.end()) out.push_back(y2);
    }
    v = out;
  };
  std::vector<Id> techRefs, bldRefs;
  tx.w().techs.each([&](const Tech& t) {
    if (remap.count(t.id)) return;
    for (Id p : t.prereqs)
      if (remap.count(p)) {
        techRefs.push_back(t.id);
        break;
      }
  });
  tx.w().buildings.each([&](const Building& b) {
    for (Id p : b.techs)
      if (remap.count(p)) {
        bldRefs.push_back(b.id);
        break;
      }
  });
  for (Id t : techRefs) fix(tx.tech(t).prereqs);
  for (Id b : bldRefs) fix(tx.building(b).techs);
  for (auto& [tid, to] : remap) tx.eraseTech(tid);
  const bool createdNow = c.s.techs >= int(remap.size());   // ветку создала версия 4 этого же дополнения
  c.s.techs = std::max(0, c.s.techs - int(remap.size()));   // в сводке — оставшиеся из созданных этим дополнением
  // Изученность: ступень ×s изучена, если изучена прежняя технология ×N ≥ s; множители провинций — не больше доступного.
  for (auto& [fid, best] : top)
    for (auto& [m, id] : steps)
      if (m <= best) tx.faction(fid).techs[id].studied = true;
  auto allowed = [&](Id owner) {
    int a = 1;
    if (auto it = top.find(owner); it != top.end())
      for (auto& [m, id] : steps)
        if (m <= it->second) a = m;
    return a;
  };
  std::vector<Id> pids;
  tx.w().provinces.each([&](const Province& p) {
    for (const ProvBuilding& pb : p.buildings)
      if (pb.mult > allowed(p.owner)) {
        pids.push_back(p.id);
        break;
      }
  });
  for (Id pid : pids) {
    const int a = allowed(tx.w().province(pid)->owner);
    for (ProvBuilding& pb : tx.province(pid).buildings) pb.mult = std::min(pb.mult, a);
  }
  if (!remap.empty() && !createdNow)
    c.s.notes.push_back("ветка «Стимуляция промышленности»: " + std::to_string(old.size()) + " технологий заменены 7 ступенями (×2, ×5, ×10, ×20, ×35, ×60, ×100)");
}

// ---- «Основы археологии» и «Продвинутые инструменты» (ТЗ аудита 7.11)
void stepArchTechs(V6& c) {
  struct Up {
    const char* key;
    double v;
  };
  for (const Up& u : {Up{"archBasics", 10}, Up{"archTools", 20}}) {
    const Id tid = c.tx().w().techs.firstId([&](const Tech& t) { return t.faction == 0 && t.key == u.key; });
    if (!tid) continue;
    const std::string oldDesc = "Археологические сокровища от действий и наград археологических групп +1 %.";
    const std::string newDesc = "Археологические сокровища от действий и наград археологических групп +" + fmtNum(u.v, 0) + " %.";
    if (c.tx().w().tech(tid)->desc == oldDesc) c.tx().tech(tid).desc = newDesc;
    for (Id mid : c.tx().w().tech(tid)->modifiers) {
      const Modifier* m = c.tx().w().modifier(mid);
      if (!m || !m->has(Fx::ArchTreasurePct) || m->fx[size_t(Fx::ArchTreasurePct)] != 1) continue;
      Modifier& x = c.tx().modifier(mid);
      x.fx[size_t(Fx::ArchTreasurePct)] = u.v;
      if (x.desc == oldDesc) x.desc = newDesc;
    }
  }
}

// ---- постройки: новые (нефтяная вышка, ткацкая мастерская, оружейная палата), рецепты, цены, виды государства,
// гильдия строителей, модификаторы уровней, «Общие» артефакты в сундуках
void stepBuildings(V6& c) {
  Seeder& s = c.s;
  Tx& tx = c.tx();
  auto recipe = [&](const char* name, const Cost& in, const Cost& out) {
    Recipe r;
    r.name = name;
    for (auto& [n, v] : in)
      if (Id id = c.resId(n)) r.in.push_back(ResAmount{id, v});
    for (auto& [n, v] : out)
      if (Id id = c.resId(n)) r.out.push_back(ResAmount{id, v});
    r.turns = 1;
    return r;
  };
  // Нефть: вышка (больше — в провинции с нефтью) и перегонка в топливо в инженерной мастерской.
  s.newRow(BuildingCat::Industrial);
  s.ensureBuilding(nullptr, "Нефтяная вышка", BuildingCat::Industrial, "tower", [&](Building& b) {
    b.desc = "Нефть каждый ход; в провинции с ресурсом «Нефть» — больше.";
    b.levels[0].cost = c.cost({{"Древесина", 200}, {"Камень", 100}, {"Сталь", 50}, {"Золото", 20}});
    if (Id oil = c.resId("Нефть")) {
      b.levels[0].produce[oil] = 5;
      b.levels[0].produceOnRes[oil] = 40;
    }
  });
  s.ensureBuilding(nullptr, "Ткацкая мастерская", BuildingCat::Industrial, "convert", [&](Building& b) {
    b.desc = "Шелк — в ткань.";
    b.levels[0].cost = c.cost({{"Древесина", 150}, {"Камень", 100}, {"Золото", 15}});
    b.convert = true;
    b.recipes = {recipe("Ткань из шелка", {{"Шелк", 1}}, {{"Ткань", 4}})};
  });
  s.newRow(BuildingCat::Military);
  s.ensureBuilding(nullptr, "Оружейная палата", BuildingCat::Military, "swords", [&](Building& b) {
    b.desc = "Особые отряды: арбалетчики дамасской стали, стрелки лунного серебра, хитиновые стражи, чешуйчатые латники, меховые налетчики.";
    b.levels[0].cost = c.cost({{"Древесина", 200}, {"Камень", 400}, {"Сталь", 200}, {"Титан", 50}, {"Золото", 50}});
    b.specialAccess = true;
  });
  // Рецепты: топливо из нефти, сталь на топливе, шелк из пауков.
  auto outputs = [&](const Building& b, Id res) {
    for (const Recipe& r : b.recipes)
      for (const ResAmount& o : r.out)
        if (o.res == res) return true;
    return false;
  };
  const Id fuel = c.resId("Топливо"), silk = c.resId("Шелк");
  if (Id eng = c.building(schema::bld::EngWorkshop); eng && fuel && !outputs(*tx.w().building(eng), fuel)) {
    Building& b = tx.building(eng);
    b.convert = true;
    b.recipes.push_back(recipe("Топливо", {{"Нефть", 2}}, {{"Топливо", 1}}));
    if (b.desc == "Механические детали из железа, бронзы и кожи; запчасти механизмов из деталей, стали и кожи.")
      b.desc = "Механические детали из железа, бронзы и кожи; запчасти механизмов из деталей, стали и кожи; топливо из нефти.";
  }
  if (Id works = c.building(schema::bld::Steelworks); works && fuel) {
    bool has = false;
    for (const Recipe& r : tx.w().building(works)->recipes)
      for (const ResAmount& x : r.in) has = has || x.res == fuel;
    if (!has) tx.building(works).recipes.push_back(recipe("Сталь на топливе", {{"Железо", 2}, {"Топливо", 1}}, {{"Сталь", 3}}));
  }
  if (Id sh = c.building(schema::bld::Slaughterhouse); sh && silk && !outputs(*tx.w().building(sh), silk)) {
    for (Recipe& r : tx.building(sh).recipes)
      if (r.name == "Переработка пауков" && r.out.size() < 3) r.out.push_back(ResAmount{silk, 10});
  }
  // Мертвые ресурсы в ценах построек (ТЗ аудита 7.6): ресурс, которого в цене уровня еще нет.
  for (const CostAdd& a : kCostAdds) {
    const Id bid = c.buildingOf(a.key, a.name);
    const Id r = c.resId(a.res);
    if (!bid || !r) continue;
    const Building* b = tx.w().building(bid);
    if (a.level < 1 || a.level > int(b->levels.size()) || b->levels[size_t(a.level - 1)].cost.count(r)) continue;
    tx.building(bid).levels[size_t(a.level - 1)].cost[r] = a.v;
  }
  // Виды государства (ТЗ аудита 7.13), как у мясопилки: алтарь культа и глубинная бездна — демонам, мавзолей — нежити.
  struct Kind {
    const char* key;
    const char* name;
    StateKind kind;
  };
  for (const Kind& k : {Kind{schema::bld::CultAltar, nullptr, StateKind::Demonic}, Kind{nullptr, "Мавзолей Короля Лича", StateKind::Undead},
                        Kind{nullptr, "Глубинная Бездна", StateKind::Demonic}})
    if (Id bid = c.buildingOf(k.key, k.name); bid && tx.w().building(bid)->stateKinds == 0) tx.building(bid).stateKinds = 1u << int(k.kind);
  // «Гильдия строителей» сама занимает слот (ТЗ аудита 4.14): уровень N — N + 1 слот.
  for (int i = 1; i <= 5; i++) {
    const std::string mk = schema::levelModKey(schema::mod::BuildersLevel, i);
    const Id mid = tx.w().modifiers.firstId([&](const Modifier& m) { return m.key == mk; });
    if (!mid) continue;
    const Modifier* m = tx.w().modifier(mid);
    if (m->has(Fx::Slots) && m->fx[size_t(Fx::Slots)] == double(i)) tx.modifier(mid).fx[size_t(Fx::Slots)] = double(i) + 1;
  }
  if (Id g = c.building(schema::bld::BuildersGuild); g && tx.w().building(g)->desc == "Слоты построек провинции: +1 за уровень.")
    tx.building(g).desc = "Слоты построек провинции: уровень N — N + 1 слот (N сверх собственного).";
  // Модификаторы уровней порта, рынка и верфи (ТЗ аудита 7.9).
  for (const LevelMods& lm : levelMods()) {
    const Id bid = c.building(lm.key);
    if (!bid) continue;
    const size_t n = std::min(tx.w().building(bid)->levels.size(), lm.perLevel.size());
    for (size_t i = 0; i < n; i++) {
      ModSpec d;
      d.name = std::string(lm.title) + " " + kRoman[i];
      d.kind = lm.kind;
      d.icon = lm.icon;
      d.color = lm.color;
      d.desc = lm.desc;
      d.fx = {{lm.fx, lm.perLevel[i]}};
      const Id mid = c.mod(d);
      std::vector<Id>& mods = tx.building(bid).levels[i].modifiers;
      if (std::find(mods.begin(), mods.end(), mid) == mods.end()) mods.push_back(mid);
    }
  }
  // «Общие» артефакты — из сундуков (лари и прочие сундуки — археологические находки).
  Id general = 0;
  for (const RelicGroup& g : c.cat().relicGroups)
    if (!g.parent && key(g.name) == key("Общие")) general = g.id;
  const Id finds = c.cat().relicGroupId(schema::kRelicArchFinds);
  if (general && finds)
    for (Chest& ch : c.cat().chests) {
      if (ch.key != "woodChest" && ch.key != "reinfChest" && ch.key != "steelChest" && ch.key != "mithrilChest" && ch.key != "jarl") continue;
      for (ChestItem& it : ch.items)
        if (it.kind == ChestItemKind::Relic && it.relicGroup == finds) it.relicGroup = general;
    }
}

// ---- особые отряды (ТЗ аудита 7.6–7.8, 7.12–7.13)
void stepSpecials(V6& c) {
  Catalogs& k = c.cat();
  std::vector<Id> changed;
  auto edit = [&](const char* name, auto&& fn) {
    if (SpecialUnit* sp = byName(k.specials, name))
      if (fn(*sp)) changed.push_back(sp->id);
  };
  // Шантирийские големы и титаны — заметно дешевле в запчастях.
  edit("Шантирийские Големы", [](SpecialUnit& x) { return x.keyPer == 150 ? (x.keyPer = 10, true) : false; });
  edit("Шантирийские Титаны", [](SpecialUnit& x) { return x.keyPer == 1500 ? (x.keyPer = 100, true) : false; });
  // «Драконы Бездны»: содержание и раса; фениксы — содержание эссенцией меньше выработки храма.
  edit("Драконы Бездны", [](SpecialUnit& x) {
    bool ch = false;
    if (x.upkeep == 0) {
      x.upkeep = 1;
      ch = true;
    }
    if (x.race.empty()) {
      x.race = schema::kRaceDemonic;
      ch = true;
    }
    return ch;
  });
  const Id fire = c.essId("Эссенция пламени");
  edit("Фениксы", [&](SpecialUnit& x) {
    auto it = x.essUpkeep.find(fire);
    if (it == x.essUpkeep.end() || it->second != 10) return false;
    it->second = 2;
    return true;
  });
  const Id bone = c.resId("Кость");
  edit("Драконы-Личи", [&](SpecialUnit& x) { return bone && !x.extra.count(bone) ? (x.extra[bone] = 10, true) : false; });
  for (Id id : changed) syncRows(c, id);
  // Новые особые отряды и постройки доступа к ним.
  for (const SpecialSpec& d : specialSpecs()) {
    Id id = 0;
    if (const SpecialUnit* have = byName(k.specials, d.name)) {
      id = have->id;
    } else {
      SpecialUnit s;
      s.id = c.tx().nextId(Seq::Special);
      s.name = d.name;
      s.type = d.type;
      s.race = d.race;
      s.keyRes = 0;
      s.keyPer = 1;
      for (auto& [n, v] : d.extra)
        if (Id r = c.resId(n)) s.extra[r] = v;
      for (auto& [n, v] : d.essence)
        if (Id e = c.essId(n)) {
          s.essence[e] = v;
          if (d.essUpkeep > 0) s.essUpkeep[e] = d.essUpkeep;
        }
      s.upkeep = d.upkeep;
      s.desc = d.desc;
      id = s.id;
      k.specials.push_back(std::move(s));
      c.s.specials++;
    }
    if (const Id bid = c.buildingOf(d.accessKey, d.accessName)) {
      Building& b = c.tx().building(bid);
      b.specialAccess = true;
      if (std::find(b.specials.begin(), b.specials.end(), id) == b.specials.end()) b.specials.push_back(id);
    }
  }
}

// ---- модификаторы рас, культур, религий, форм правления и должностей (ТЗ аудита 7.5, 7.9)
void stepCatalogMods(V6& c) {
  Catalogs& k = c.cat();
  // Формы правления: восемь базовых (новый мир создаёт шесть).
  std::vector<std::string> added;
  for (const NamedFx& g : governmentSpecs())
    if (!byName(k.governments, g.name)) {
      CatalogItem it;
      it.id = c.tx().nextId(Seq::Government);
      it.name = g.name;
      it.color = Color::hex(0x888888);
      k.governments.push_back(std::move(it));
      added.push_back(std::string("«") + g.name + "»");
    }
  if (!added.empty()) c.s.notes.push_back("формы правления: " + join(added, ", "));
  auto attach = [](std::vector<Id>& list, Id mod) {
    if (list.empty()) list.push_back(mod);
  };
  for (const NamedFx& r : raceSpecs())
    if (CatalogItem* it = byName(k.races, r.name); it && it->modifiers.empty()) {
      attach(it->modifiers, c.mod(ModSpec{std::string("Раса: ") + it->name, ModKind::Province, "race", rgbOf(it->color),
                                          "Действует в провинциях, где больше всего жителей этой расы.", r.fx, r.gen}));
    }
  for (const NamedFx& r : cultureSpecs())
    if (CatalogItem* it = byName(k.cultures, r.name); it && it->modifiers.empty()) {
      attach(it->modifiers, c.mod(ModSpec{std::string("Культура: ") + it->name, ModKind::Any, "culture", rgbOf(it->color),
                                          "Провинциям и государствам этой культуры (локальные эффекты государства — во всех его провинциях).", r.fx,
                                          r.gen}));
    }
  for (const FaithSpec& f : faithSpecs()) {
    Id mod = 0;
    for (const char* n : f.religions)
      if (CatalogItem* it = byName(k.religions, n); it && it->modifiers.empty()) {
        if (!mod)
          mod = c.mod(ModSpec{std::string("Вера: ") + f.name, ModKind::Any, "religion", f.color,
                              "Провинциям и государствам этой веры (локальные эффекты государства — во всех его провинциях).", f.fx, {}});
        it->modifiers.push_back(mod);
      }
  }
  for (const NamedFx& g : governmentSpecs())
    if (CatalogItem* it = byName(k.governments, g.name); it && it->modifiers.empty())
      attach(it->modifiers, c.mod(ModSpec{std::string("Правление: ") + it->name, ModKind::Faction, "crown", 0xd9a441,
                                          "Действует на государства с этой формой правления.", g.fx, g.gen}));
  for (const PositionSpec& p : positionSpecs()) {
    CatalogItem* it = byName(k.positions, p.name);
    if (!it) continue;
    if (it->modifiers.empty())
      it->modifiers.push_back(c.mod(ModSpec{std::string("Совет: ") + it->name, ModKind::Faction, "council", 0x4f9d69,
                                            "Пока должность в совете занята.", p.taken, p.gen}));
    if (it->vacantModifiers.empty())
      it->vacantModifiers.push_back(c.mod(ModSpec{std::string("Пустует: ") + it->name, ModKind::Faction, "user", 0xa8432c,
                                                  "Пока должность в совете пустует.", p.vacant, {}}));
  }
}

// ---- модификаторы классов, талантов (порог ярусов — 7 очков) и реликвий (ТЗ аудита 7.1, 7.4)
Id talentMod(V6& c, const TalentTheme& t, bool ult) {
  ModSpec d;
  d.name = std::string(ult ? "Финальный талант: " : "Талант: ") + t.name;
  d.kind = ModKind::Hero;
  d.icon = t.icon;
  d.color = t.color;
  d.desc = ult ? "Финальный талант." : "За каждый изученный ранг таланта.";
  d.fx = ult ? t.ult : t.rank;
  if (ult) d.gen = t.ultGen;
  return c.mod(d);
}

void stepHeroMods(V6& c) {
  Catalogs& k = c.cat();
  // Классы: базовый модификатор («Некромант» — встроенный).
  for (const ClassSpec& cs : classSpecs()) {
    HeroClass* hc = byName(k.classes, cs.name);
    if (!hc || !hc->modifiers.empty()) continue;
    Id mod = 0;
    if (cs.builtin) {
      mod = builtinModifierRecord(c.tx(), cs.builtin);   // с генерацией по умолчанию («Некромант» — эссенция смерти)
    } else {
      mod = c.mod(ModSpec{std::string("Класс: ") + hc->name, ModKind::Hero, hc->icon.empty() ? "hero-class" : hc->icon.c_str(),
                          rgbOf(hc->color), "Действует на героев этого класса.", cs.fx, cs.gen});
    }
    if (mod) hc->modifiers.push_back(mod);
  }
  // Таланты базовых деревьев: тема по значку таланта (иначе — тема класса), финальные — сильнее; ярус — 7 очков.
  for (const SubclassDef& d : subclassDefs()) {
    HeroClass* hc = byName(k.classes, fixedName(d.cls));
    SubClass* sc = hc ? byName(hc->subclasses, fixedName(d.name)) : nullptr;
    if (!sc) continue;
    if (sc->tierPoints == schema::kDefaultTierPoints) sc->tierPoints = 7;
    const char* fallback = "Доблесть";
    for (const ClassSpec& cs : classSpecs())
      if (key(cs.name) == key(fixedName(d.cls))) fallback = cs.theme;
    std::unordered_set<std::string> names;
    for (const auto& tier : d.tiers)
      for (const TalentDef& t : tier) names.insert(key(t.name));
    for (Talent& t : sc->talents) {
      if (!t.modifiers.empty() || !names.count(key(t.name))) continue;
      const char* th = themeOfIcon(t.icon);
      const TalentTheme* theme = talentTheme(th ? th : fallback);
      if (!theme) theme = &talentThemes().front();
      t.modifiers.push_back(talentMod(c, *theme, t.ranks == 1 && t.cost >= schema::kMaxTalentCost));
    }
  }
  // Реликвии: тема по названию базовой реликвии, сила — по редкости.
  std::unordered_map<std::string, int> themes;
  for (auto& [n, t] : relicThemes()) themes.emplace(key(n), t);
  for (Relic& r : k.relics) {
    if (!r.modifiers.empty() || r.phylactery) continue;
    auto it = themes.find(key(r.name));
    if (it == themes.end()) continue;
    const int rar = std::clamp(int(r.rarity), 0, int(Rarity::Count) - 1);
    static const u32 kColors[] = {0xe9e6dc, 0x3f8cff, 0xa45cff, 0xff9b26, 0xff3d3d};
    ModSpec d{std::string("Реликвия: ") + kRelicThemeName[it->second] + " · " + kRarityWord[rar], ModKind::Hero, "relic", kColors[rar],
              "Действует на героя, у которого реликвия.", relicFx(it->second, rar), relicGen(it->second, rar)};
    r.modifiers.push_back(c.mod(d));
  }
}

// ---- сроки строительства уровней базовых построек по цене (ТЗ аудита 4.15)
void stepBuildTimes(V6& c) {
  Tx& tx = c.tx();
  std::unordered_set<std::string> named;
  for (const char* n : kBaseNamed) named.insert(key(n));
  for (const CatalogItem& e : c.cat().essences)
    if (startsWith(e.name, "Эссенция ")) {
      const std::string suffix = e.name.substr(std::string("Эссенция ").size());
      named.insert(key("Храм " + suffix));
      named.insert(key("Собор " + suffix));
    }
  std::vector<Id> ids;
  tx.w().buildings.each([&](const Building& b) {
    const bool base = b.owner == 0 ? !b.key.empty() || named.count(key(b.name))
                                   : b.key == schema::bld::IntelHq || b.key == schema::bld::CounterIntelHq;
    if (!base) return;
    for (const BuildingLevel& l : b.levels)
      if (l.turns == 1 && buildTurnsOf(l) > 1) {
        ids.push_back(b.id);
        return;
      }
  });
  for (Id id : ids)
    for (BuildingLevel& l : tx.building(id).levels)
      if (l.turns == 1) l.turns = buildTurnsOf(l);
}

}  // namespace

int buildTurnsOf(const BuildingLevel& l) {
  double v = 0;
  for (auto& [res, x] : l.cost)
    if (std::isfinite(x) && x > 0) v += res == kGold ? x * 10 : x;
  for (auto& [e, x] : l.essCost)
    if (std::isfinite(x) && x > 0) v += x;
  return v <= 400 ? 1 : v <= 1500 ? 2 : v <= 4000 ? 3 : v <= 9000 ? 4 : 5;
}

void Seeder::v6() {
  V6 c{*this, {}, {}, {}};
  stepRenames(c);
  c.index();
  stepIndustry(c);
  stepArchTechs(c);
  stepBuildings(c);
  stepSpecials(c);
  stepCatalogMods(c);
  stepHeroMods(c);
  stepBuildTimes(c);
}

}  // namespace rg::content::detail
