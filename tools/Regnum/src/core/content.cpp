// Regnum — базовые записи мира: должности, эссенции, группы ресурсов, религии, особый отряд и его постройка.
#include "core/content.h"

#include "core/arch.h"
#include "core/schema.h"

namespace rg::content {

namespace {

// ---------------------------------------------------------------- данные
struct Named {
  const char* name;
  u32 color;
  const char* icon;
};

// Базовые должности: прежние названия переименовываются (ТЗ «Добавления в справочники», п.1).
struct PositionRename {
  const char* from;
  const char* to;
};
const PositionRename kPositionRenames[] = {
    {"Канцлер", "Десница"},
    {"Казначей", "Лорд-Мастер над экономикой"},
    {"Маршал", "Лорд-Мастер над войной"},
    {"Адмирал", "Лорд-Мастер над флотом"},
    {"Тайный советник", "Лорд-Мастер над разведкой"},
    {"Придворный маг", "Хранитель знаний"},
};
const char* const kPositionsNew[] = {"Патриарх", "Верховный магистр", "Архимаг", "Верховный друид"};

const Named kEssences[] = {
    {"Эссенция пламени", 0xff6a2b, "essence"},   {"Эссенция воды", 0x2f8fff, "essence"},     {"Эссенция земли", 0xa07a4a, "essence"},
    {"Эссенция воздуха", 0x9fe3f7, "essence"},   {"Эссенция света", 0xffe98a, "essence"},    {"Эссенция тьмы", 0x6a4fb0, "essence"},
    {"Эссенция смерти", 0x8f9a86, "essence"},    {"Эссенция хаоса", 0xe0428f, "essence"},    {"Эссенция природы", 0x4fc24f, "essence"},
    {"Эссенция крови", 0xd0233f, "essence"},     {"Эссенция чумы", 0x9bb52c, "essence"},     {"Эссенция льда", 0xbdefff, "essence"},
    {"Эссенция арканы", 0x8b6bff, "essence"},    {"Эссенция господства", 0xd9a520, "essence"}, {"Эссенция силы", 0xf07b2a, "essence"},
    {"Эссенция сумерек", 0x7d6bb8, "essence"},   {"Эссенция гармонии", 0x5fd8c0, "essence"}, {"Эссенция бездны", 0x5a3fa0, "essence"},
};

const BaseGroup kGroups[] = {
    {schema::grp::Ore, "Руда", nullptr},
    {schema::grp::OreCommon, "Обычная", schema::grp::Ore},
    {schema::grp::OreSpecial, "Особая", schema::grp::Ore},
    {schema::grp::Beasts, "Звери", nullptr},
    {schema::grp::MountsGround, "Ездовые наземные", schema::grp::Beasts},
    {schema::grp::MountsFlying, "Ездовые летающие", schema::grp::Beasts},
    {schema::grp::WarBeasts, "Боевые", schema::grp::Beasts},
    {schema::grp::Monsters, "Чудовища", schema::grp::Beasts},
    {schema::grp::Provisions, "Провизия", nullptr},
    {schema::grp::ProvAgri, "Сельскохозяйственная", schema::grp::Provisions},
    {schema::grp::ProvAnimal, "Животная", schema::grp::Provisions},
    {schema::grp::ProvGather, "Собираемая", schema::grp::Provisions},
    {schema::grp::Materials, "Материалы", nullptr},
    {schema::grp::MatRaw, "Сырьевые", schema::grp::Materials},
    {schema::grp::MatPrecious, "Драгоценные", schema::grp::Materials},
    {schema::grp::MatAlchemy, "Алхимические", schema::grp::Materials},
    {schema::grp::MatIndustrial, "Промышленные", schema::grp::Materials},
    {schema::grp::Currencies, "Валюты", nullptr},
    {schema::grp::SeaMonsters, "Морские чудовища", schema::grp::Beasts},
};

struct GroupItems {
  const char* group;
  std::vector<Named> items;
};
const std::vector<GroupItems>& groupItems() {
  static const std::vector<GroupItems> v = {
      {schema::grp::OreCommon,
       {{"Серебро", 0xc9ced6, "pickaxe"}, {"Медь", 0xc8743a, "pickaxe"}, {"Железо", 0x6d7c8f, "iron"}, {"Титан", 0x9aa8b8, "pickaxe"},
        {"Черное железо", 0x3c4048, "iron"}, {"Алое железо", 0xb3392f, "iron"}, {"Олово", 0xa9b0a8, "pickaxe"}, {"Платина", 0xd8dde3, "pickaxe"}}},
      {schema::grp::OreSpecial,
       {{"Мифрил", 0x7fc4dd, "gem"}, {"Лунное серебро", 0xc6d4ff, "gem"}, {"Обсидиан", 0x2e2a3a, "gem"}, {"Темный обсидиан", 0x1d1626, "gem"},
        {"Магнитный обсидиан", 0x48405e, "gem"}, {"Волшебная руда", 0x9b6bff, "gem"}, {"Метеоритная руда", 0x8a5a3c, "gem"}}},
      {schema::grp::MountsGround,
       {{"Лошади", 0xa0714f, "horse"},          {"Рапторы", 0x7f9a3c, "horse"},        {"Василиски", 0x5f8a4a, "horse"},
        {"Демигрифы", 0xc29a5a, "horse"},       {"Единороги", 0xe9e4f5, "horse"},      {"Медведи", 0x7a5232, "horse"},
        {"Лютоволки", 0x8a8f99, "horse"},       {"Гигантские ящеры", 0x6f8f3c, "horse"}, {"Королевские Тигры", 0xe08a2c, "horse"},
        {"Сумеречные Пантеры", 0x4a3f6a, "horse"}, {"Королевские Львы", 0xd8a440, "horse"}, {"Буйволы", 0x6a4a32, "horse"},
        {"Носороги", 0x8c8a86, "horse"},        {"Верблюды", 0xc8a46a, "horse"},       {"Гигантские Пауки", 0x3a3340, "horse"},
        {"Горные Бараны", 0xb7a68a, "horse"},   {"Лесные Кабаны", 0x6b4a36, "horse"},  {"Гигантские Скорпионы", 0x9a6a2a, "horse"},
        {"Адские Скакуны", 0xb3311f, "horse"},  {"Адские Рапторы", 0x9e2a2a, "horse"}, {"Скакуны Нокс", 0x3c2f5c, "horse"},
        {"Скакуны Смерти", 0x5c6656, "horse"},  {"Механопауки", 0x7d8794, "horse"}}},
      {schema::grp::MountsFlying,
       {{"Грифоны", 0xc9a05a, "u-flying"},   {"Пегасы", 0xeef0fa, "u-flying"},        {"Птеродактили", 0x8a7a5a, "u-flying"},
        {"Гиппогрифы", 0xb0905a, "u-flying"}, {"Мантикоры", 0xb0502f, "u-flying"},     {"Крылатые Львы", 0xd9b24a, "u-flying"},
        {"Гигантские орлы", 0x8a6440, "u-flying"}, {"Виверны", 0x5a7a3c, "u-flying"}, {"Гигантские летучие мыши", 0x463a4f, "u-flying"},
        {"Кошмары", 0x2e2238, "u-flying"},    {"Костяные грифоны", 0xd8d2c0, "u-flying"}, {"Винтолеты", 0x8a929c, "u-flying"}}},
      {schema::grp::WarBeasts,
       {{"Слоны", 0x8f8a86, "u-beasts"},       {"Мамонты", 0x7a5a3c, "u-beasts"},      {"Карнотавры", 0xa0482f, "u-beasts"},
        {"Трицератопсы", 0x7a8f4a, "u-beasts"}, {"Адские гончие", 0xb3311f, "u-beasts"}, {"Церберы", 0x7a1f1f, "u-beasts"},
        {"Свежеватели Разума", 0x8a5a9e, "u-beasts"}, {"Гончие Нокс", 0x3c2f5c, "u-beasts"}, {"Баргесты", 0x4a4a52, "u-beasts"},
        {"Стекозмеи", 0x9fd8e0, "u-beasts"},    {"Богомолы", 0x6fae3c, "u-beasts"},     {"Ядовитые твари", 0x7fa82a, "u-beasts"},
        {"Ламасу", 0xd8b05a, "u-beasts"},       {"Каппы", 0x4a8a6a, "u-beasts"},        {"Ра'шотхи", 0x9e6a3c, "u-beasts"}}},
      {schema::grp::Monsters,
       {{"Гидры", 0x3f8a5a, "u-monsters"},          {"Красные Драконы", 0xc0302a, "u-monsters"}, {"Черные Драконы", 0x2a2630, "u-monsters"},
        {"Зеленые Драконы", 0x3c9a4a, "u-monsters"}, {"Золотые Драконы", 0xe0b030, "u-monsters"}, {"Химеры", 0xa05a3c, "u-monsters"},
        {"Великаны", 0x9a7a5a, "u-monsters"},        {"Древние Чудища", 0x5a4a3c, "u-monsters"},  {"Лазурные Драконы", 0x2f7fe0, "u-monsters"},
        {"Харибды", 0x2a5a7a, "u-monsters"},         {"Кирины", 0xe0c070, "u-monsters"},         {"Рухи", 0x8a6a4a, "u-monsters"},
        {"Циклопы", 0x8a7060, "u-monsters"},         {"Ракшасы", 0xd07a2f, "u-monsters"},        {"Бехолдеры", 0x9e4a8a, "u-monsters"},
        {"Сколопендроморфы", 0x7a3a2a, "u-monsters"}, {"Архиспоры", 0x8aa03c, "u-monsters"},      {"Горгоны", 0x6a8a5a, "u-monsters"},
        {"Энты", 0x5a7a3a, "u-monsters"},            {"Архидьяволы", 0x9e1f1f, "u-monsters"},    {"Владыки Преисподней", 0x7a1414, "u-monsters"},
        {"Черви Преисподней", 0x8a3a2a, "u-monsters"}, {"Хтонические ужасы", 0x3a2a4a, "u-monsters"}, {"Искаженные Драконы", 0x6a2a7a, "u-monsters"},
        {"Костяные Драконы", 0xd8d0bc, "u-monsters"}, {"Намтары", 0x4a3a5a, "u-monsters"},        {"Кристаллические Драконы", 0x9fe0f0, "u-monsters"},
        {"Охотники Роя", 0x7a6a2a, "u-monsters"},    {"Пепельные Драконы", 0x6a625a, "u-monsters"}, {"Тиранозавры", 0x6a7a3a, "u-monsters"},
        {"Коатли", 0x3fb09a, "u-monsters"}}},
      {schema::grp::ProvAgri,
       {{"Виноград", 0x7a3a8a, "grain"}, {"Зерно", 0xd9c36b, "grain"}, {"Фрукты", 0xe0702f, "grain"}, {"Овощи", 0x7fb04a, "grain"},
        {"Хмель", 0x9fb84a, "grain"}, {"Дурманящие травы", 0x6a9a5a, "grain"}}},
      {schema::grp::ProvAnimal,
       {{"Свиньи", 0xe0a0a0, "u-beasts"}, {"Коровы", 0x8a6a4a, "u-beasts"}, {"Курицы", 0xe8d8b0, "u-beasts"}, {"Индейки", 0x9a5a3a, "u-beasts"},
        {"Бараны", 0xd8d0c0, "u-beasts"}}},
      {schema::grp::ProvGather,
       {{"Ягоды", 0xb02f5a, "grain"}, {"Грибы", 0xb08a5a, "grain"}, {"Мед", 0xe0a82f, "grain"}, {"Рыба", 0x5a8ab0, "grain"},
        {"Морепродукты", 0xe07a5a, "grain"}, {"Бамбук", 0x8ab04a, "grain"}}},
      {schema::grp::MatRaw,
       {{"Кожа", 0x9a6a3c, "resource"}, {"Кость", 0xe0d8c0, "skull"}, {"Древесина", 0x8a6a43, "wood"}, {"Камень", 0x9aa0a8, "stone"},
        {"Ткань", 0xc8b8a0, "resource"}, {"Шелк", 0xe8d8f0, "resource"}, {"Нефть", 0x2a2a2a, "flame"}}},
      {schema::grp::MatPrecious,
       {{"Кристаллы", 0x9fd8f0, "gem"}, {"Самоцветы", 0xa35fc9, "gem"}, {"Жемчуг", 0xf0ece0, "gem"}, {"Шантирийские кристаллы", 0x5fe0c8, "gem"}}},
      {schema::grp::MatAlchemy,
       {{"Ртуть", 0xb8c0c8, "sparkles"}, {"Сера", 0xe0d040, "sparkles"}, {"Селитра", 0xe8e8e0, "sparkles"}, {"Магическая пыль", 0xb08aff, "sparkles"},
        {"Лечебные травы", 0x5fb05a, "sparkles"}, {"Токсины", 0x8ab02a, "sparkles"}, {"Магический порох", 0x8a4ad0, "sparkles"}}},
      {schema::grp::MatIndustrial,
       {{"Сталь", 0x8a9aaa, "hammer"},          {"Топливо", 0x5a4a3a, "flame"},          {"Механические детали", 0x7a8494, "hammer"},
        {"Бронза", 0xb0803a, "hammer"},         {"Дамасская сталь", 0x6a7a8a, "hammer"}, {"Мифриловый сплав", 0x8ad0e8, "hammer"},
        {"Шантирийский сплав", 0x4fc8b0, "hammer"}, {"Порох", 0x3a3a3a, "flame"},         {"Запчасти механизмов", 0x8d97a5, "hammer"},
        {"Обсидиановая сталь", 0x3a3448, "hammer"}, {"Шантирийские запчасти", 0x4fb0a0, "hammer"}}},
  };
  return v;
}

const char* const kReligions[] = {
    "Культ Смерти", "Церковь света", "Орден Вечного Пламени", "Ковенант Приливов", "Церковь Глубинного Милосердия",
    "Кузница Огненного Слова", "Искрящий Синод", "Стражи Нерушимого Камня", "Небесный Конклав", "Вольное Братство Бури",
    "Изумрудный Круг", "Плетение Дикой Лозы", "Церковь Священного Истока", "Ложа Сокрытого Знания", "Секта Семи Вуалей",
    "Завеса Шепчущих Истин", "Иерархия Высшего Трона", "Орден Сокрушающей Длани", "Конклав Архитекторов Судьбы", "Орден Изначального Слова",
    "Палата Первородной Ночи", "Орден Черного Солнца", "Церковь Алого Истока", "Культ Мора", "Культ Удовольствий",
    "Братство Изменчивого Ока", "Черный Сонм Наз'ферата", "Вече Предвечных Духов", "Багряная Династия Валахии", "Гнездо Первородного Голода",
    "Пантеон Чистой Крови", "Орден Света Пустоты", "Академия Семи Сфер", "Стражи Серой Завесы", "Культ Сломанного Трона",
    "Орден Пожирателей", "Ковенант Наслаждений", "Легион Кипящей Крови", "Высший Синод Высокомерия", "Секта Искаженного Эха",
    "Иерархия Пернатого Змея", "Священный Календарь Итца", "Династия Вечного Начертания", "Великий Доминион Шелковой Зари", "Талассократия Семи Ветров",
    "Синдикат Пустого Зеркала", "Гильдия Невидимой Руки", "Союз Железного Кулака", "Слушающие Тотемы", "Клятвенники Чести",
    "Орден Сияющих Граней", "Династия Солнечного Ветра", "Патрициат Орлиного Крыла", "Культ Кровавого Прилива", "Сонм Тотема Медведя",
    "Драккары Ледяного Савана", "Вече Вольного Ветра", "Церковь Зимнего Змея", "Синод Шепчущей Бездны", "Теократия Невыразимого Имени",
    "Храм Пяти Хмельных Рассветов",
};

// Особый отряд и его постройка (ТЗ «Ввод новых механик», п.5.1).
constexpr const char* kAbyssDragons = "Драконы Бездны";
constexpr const char* kAbyssCitadel = "Цитадель Бездны";

// ---------------------------------------------------------------- версия 2 (ТЗ «Доработки №1–4», 2026-10-10)
// Ресурсы: морские чудовища (п.3.14), уголь (п.3.11), освящённые, осквернённые и проклятые металлы (п.4.4).
struct NamedIn {
  const char* group;
  Named item;
};
const std::vector<NamedIn>& resourcesV2() {
  static const std::vector<NamedIn> v = {
      {schema::grp::SeaMonsters, {"Левиафаны", 0x2f5f8f, "u-monsters"}},
      {schema::grp::SeaMonsters, {"Кракены", 0x7a3f6a, "u-monsters"}},
      {schema::grp::SeaMonsters, {"Морские драконы", 0x2f8f8a, "u-monsters"}},
      {schema::grp::SeaMonsters, {"Сциллы", 0x5a6f9a, "u-monsters"}},
      {schema::grp::SeaMonsters, {"Морские змеи", 0x3f9f6a, "u-monsters"}},
      {schema::grp::MatRaw, {"Уголь", 0x2e2e33, "stone"}},
      {schema::grp::MatIndustrial, {"Проклятая обсидиановая сталь", 0x3a2440, "hammer"}},
      {schema::grp::OreSpecial, {"Проклятый обсидиан", 0x2a1a33, "gem"}},
      {schema::grp::OreCommon, {"Освященное железо", 0xd8cfa0, "iron"}},
      {schema::grp::MatIndustrial, {"Освященная сталь", 0xe8dfb0, "hammer"}},
      {schema::grp::MatIndustrial, {"Оскверненная сталь", 0x6a2a2a, "hammer"}},
      {schema::grp::OreCommon, {"Оскверненное железо", 0x5a2626, "iron"}},
      {schema::grp::MatIndustrial, {"Багряная сталь", 0xa0262f, "hammer"}},
      {schema::grp::MountsFlying, {"Скакуны алой зари", 0xd0403a, "u-flying"}},
  };
  return v;
}

// Религии (ТЗ «Доработки №4», п.2).
const char* const kReligionsV2[] = {
    "Ковенант Малассы", "Храм Шалассы", "Церковь Илата", "Синод Арката", "Культ Ургаша",
    "Последователи Сар-Илама", "Орден Асхи", "Шаманы Отца Неба и Матери Земли", "Шаманы темного тотема",
};

// Классы героев (ТЗ «Доработки №4», п.6).
const char* const kClasses[] = {
    "Воин", "Палладин", "Стрелок", "Жрец", "Друид", "Маг", "Некромант", "Рыцарь Смерти", "Вампир", "Чуматворец",
    "Рыцарь тьмы", "Шаман", "Монах", "Культист", "Рыцарь Дракона", "Рыцарь Господства", "Варвар", "Рыцарь Света Пустоты", "Жнец", "Ассасин",
};

// Значок базового класса героя.
const char* classIcon(std::string_view name) {
  static const std::pair<const char*, const char*> kIcons[] = {
      {"Воин", "swords"}, {"Палладин", "shield"}, {"Стрелок", "bow"}, {"Жрец", "religion"}, {"Друид", "wood"},
      {"Маг", "wand"}, {"Некромант", "skull"}, {"Рыцарь Смерти", "lich"}, {"Вампир", "essence"}, {"Чуматворец", "plague"},
      {"Рыцарь тьмы", "moon"}, {"Шаман", "staff"}, {"Монах", "book"}, {"Культист", "b-cult"}, {"Рыцарь Дракона", "flame"},
      {"Рыцарь Господства", "crown"}, {"Варвар", "war"}, {"Рыцарь Света Пустоты", "sun"}, {"Жнец", "hourglass"}, {"Ассасин", "sword"},
  };
  for (auto& [n, i] : kIcons)
    if (name == n) return i;
  return "hero-class";
}

// Особые отряды и их постройки доступа (ТЗ «Доработки №4», п.1.9–1.10, п.5).
struct SpecialDef {
  const char* name;
  UnitType type;
  const char* race;
  const char* keyRes = nullptr;          // ключевой ресурс
  double keyPer = 1;
  const char* extra = nullptr;           // дополнительный ресурс (1 на юнит)
  const char* essence = nullptr;         // эссенция на юнит
  double essPer = 0;
  double upkeep = 0;                     // золото на юнит в ход
  double essUpkeep = 0;                  // элементали: эссенции на юнит в ход
};
struct AccessDef {
  const char* building;
  const char* icon;
  SpecialDef unit;
  std::vector<std::pair<const char*, double>> cost;   // ресурсы
  const char* essCost = nullptr;         // эссенция цены
  double essAmount = 0;
  const char* tech = nullptr;            // технология, которая открывает постройку (ключ)
};
const std::vector<AccessDef>& accessDefs() {
  static const std::vector<AccessDef> v = {
      {"Мавзолей Короля Лича", "skull",
       {"Драконы-Личи", UnitType::Monsters, schema::kRaceUndead, "Костяные Драконы", 10, nullptr, "Эссенция смерти", 750, 1},
       {{"Проклятая обсидиановая сталь", 5000}}, "Эссенция смерти", 5000},
      {"Огненное гнездовье", "flame",
       {"Фениксы", UnitType::Elementals, schema::kRaceElemental, nullptr, 1, nullptr, "Эссенция пламени", 1000, 0, 10},
       {{"Волшебная руда", 5000}}, "Эссенция пламени", 5000},
      {"Чертоги доблести", "shield",
       {"Эйнхейрии", UnitType::HeavyInf, schema::kRaceLiving, nullptr, 1, "Мифриловый сплав", "Эссенция силы", 1, 0.08},
       {{"Мифриловый сплав", 5000}}, "Эссенция силы", 5000},
      {"Храм чистейшего света", "sun",
       {"Золотые Драконы", UnitType::Monsters, schema::kRaceLiving, "Золотые Драконы", 1, nullptr, "Эссенция света", 900, 1},
       {{"Освященная сталь", 5000}}, "Эссенция света", 5000},
      {"Невидимая библиотека", "book",
       {"Безликие Кукловоды", UnitType::Flying, schema::kRaceLiving, nullptr, 1, "Обсидиановая сталь", "Эссенция тьмы", 1, 0.08},
       {{"Обсидиановая сталь", 5000}}, "Эссенция тьмы", 5000},
      {"Глубинная Бездна", "flame",
       {"Балроги", UnitType::Monsters, schema::kRaceDemonic, "Архидьяволы", 50, nullptr, "Эссенция хаоса", 1500, 10},
       {{"Оскверненная сталь", 10000}}, "Эссенция хаоса", 10000},
      {"Цитадель забытой династии", "castle",
       {"Рыцари забытой династии", UnitType::AirCav, schema::kRaceUndead, "Скакуны алой зари", 1, "Багряная сталь", "Эссенция крови", 10, 0.08},
       {{"Багряная сталь", 5000}}, "Эссенция крови", 5000},
      {"Шантирийская фабрика големов", "factory",
       {"Шантирийские Големы", UnitType::Machines, schema::kRaceMechanical, "Шантирийские запчасти", 150, nullptr, nullptr, 0, 0.1},
       {{"Археологические сокровища", 10000}}, nullptr, 0, "archGuards"},
      {"Шантирийская фабрика титанов", "factory",
       {"Шантирийские Титаны", UnitType::Machines, schema::kRaceMechanical, "Шантирийские запчасти", 1500, nullptr, nullptr, 0, 1},
       {{"Археологические сокровища", 20000}}, nullptr, 0, "archGreatness"},
  };
  return v;
}

// Постройки преобразования ветки «Археология» (п.4.1.6–1.8).
struct ConvertDef {
  const char* name;
  double cost;                                         // археологические сокровища
  std::vector<std::pair<const char*, double>> in;
  std::pair<const char*, double> out;
  const char* tech;
};
const std::vector<ConvertDef>& convertDefs() {
  static const std::vector<ConvertDef> v = {
      {"Шантирийский обогатитель", 1000, {{"Кристаллы", 10}}, {"Шантирийские кристаллы", 1}, "archConverters"},
      {"Шантирийская кузня", 2500, {{"Мифриловый сплав", 10}, {"Уголь", 20}, {"Магнитный обсидиан", 10}}, {"Шантирийский сплав", 1}, "archForges"},
      {"Шантирийская мастерская", 5000, {{"Шантирийский сплав", 1}, {"Шантирийские кристаллы", 1}}, {"Шантирийские запчасти", 1}, "archWorkshops"},
  };
  return v;
}

// Ветка общего дерева технологий «Археология» (п.4.1): каждая следующая требует предыдущую, первая — достроенную
// «Гильдию Археологов» государства.
struct TechDef {
  const char* key;
  const char* name;
  double treasures;
  double scrolls;
  int turns;
  const char* modName = nullptr;         // глобальный модификатор технологии
  Fx fx = Fx::Count;
  double fxValue = 0;
  const char* desc = "";
};
const std::vector<TechDef>& techDefs() {
  static const std::vector<TechDef> v = {
      {"archBasics", "Основы Археологии", 100, 0, 1, "Основы археологии", Fx::ArchTreasurePct, 1,
       "Археологические сокровища от действий и наград археологических групп +1 %."},
      {"archTools", "Продвинутые инструменты", 150, 0, 1, "Продвинутые инструменты", Fx::ArchTreasurePct, 1,
       "Археологические сокровища от действий и наград археологических групп +1 %."},
      {"archTrained", "Обученные исследователи", 200, 0, 1, "Обученные исследователи", Fx::ArchStartLevel, 1,
       "Новая археологическая группа начинает со 2 уровня."},
      {"archDiligent", "Старательные искатели", 300, 0, 2, "Старательные искатели", Fx::ArchDiscoveryPct, 10,
       "Шанс обнаружения археологического места +10 % для всех групп."},
      {schema::tech::ArchValues, "Ценности археологии", 500, 0, 2, nullptr, Fx::Count, 0,
       "«Гильдия Археологов» получает модификатор «Ценности археологии»: 25 археологических сокровищ за ход."},
      {"archConverters", "Древние преобразователи", 1000, 1, 3, nullptr, Fx::Count, 0, "Открывает «Шантирийский обогатитель»."},
      {"archForges", "Древние кузни", 2000, 2, 4, nullptr, Fx::Count, 0, "Открывает «Шантирийскую кузню»."},
      {"archWorkshops", "Древние мастерские", 4000, 4, 4, nullptr, Fx::Count, 0, "Открывает «Шантирийскую мастерскую»."},
      {"archGuards", "Стража доисторической Империи", 8000, 8, 4, nullptr, Fx::Count, 0, "Открывает «Шантирийскую фабрику големов»."},
      {"archGreatness", "Величие доисторической Империи", 20000, 20, 5, nullptr, Fx::Count, 0, "Открывает «Шантирийскую фабрику титанов»."},
  };
  return v;
}

// Базовая стоимость кораблей (ТЗ «Доработки №3», п.7–8): можно увеличивать и дополнять, но не меньше этой.
struct ShipCostDef {
  const char* key;
  std::vector<std::pair<const char*, double>> res;
  std::vector<std::pair<const char*, double>> ess;
};
const std::vector<ShipCostDef>& shipCosts() {
  static const std::vector<ShipCostDef> v = {
      {schema::cst::GalleonCost, {{"Древесина", 100}, {"Золото", 10}, {"Ткань", 100}}, {}},
      {schema::cst::FrigateCost, {{"Древесина", 250}, {"Золото", 25}, {"Ткань", 250}, {"Железо", 50}}, {}},
      {schema::cst::ShipLineCost, {{"Древесина", 1000}, {"Золото", 100}, {"Ткань", 1000}, {"Сталь", 250}}, {}},
      {schema::cst::SeaMonsterCost, {}, {{"Эссенция воды", 750}}},
  };
  return v;
}

// ---------------------------------------------------------------- помощники
std::string key(std::string_view s) { return utf8::searchKey(s); }

template <class T>
T* byName(std::vector<T>& list, std::string_view name) {
  const std::string k = key(name);
  for (T& x : list)
    if (key(x.name) == k) return &x;
  return nullptr;
}

struct Seeder {
  Tx& tx;
  std::vector<std::string> notes;
  int positions = 0, essences = 0, groups = 0, resources = 0, religions = 0, specials = 0, buildings = 0;
  int chests = 0, sites = 0, techs = 0, classes = 0, relicGroups = 0, modifiers = 0;

  Catalogs& cat() { return tx.catalogs(); }

  // ---- должности
  void positionsV1() {
    // Прежние названия → новые, вместе с местами совета (они хранят название должности текстом).
    for (const PositionRename& r : kPositionRenames) {
      CatalogItem* old = byName(cat().positions, r.from);
      if (!old || byName(cat().positions, r.to)) continue;
      old->name = r.to;
      const std::string ok = key(r.from);
      std::vector<Id> fids;
      tx.w().factions.each([&](const Faction& f) {
        for (const CouncilSeat& s : f.council)
          if (key(s.position) == ok) {
            fids.push_back(f.id);
            break;
          }
      });
      for (Id fid : fids)
        for (CouncilSeat& s : tx.faction(fid).council)
          if (key(s.position) == ok) s.position = r.to;
      positions++;
    }
    auto add = [&](const char* name) {
      if (byName(cat().positions, name)) return;
      CatalogItem it;
      it.id = tx.nextId(Seq::Position);
      it.name = name;
      cat().positions.push_back(std::move(it));
      positions++;
    };
    for (const PositionRename& r : kPositionRenames) add(r.to);
    for (const char* n : kPositionsNew) add(n);
  }

  // ---- группы и ресурсы
  Id groupOf(const char* gkey) {
    if (!gkey) return 0;
    if (const ResGroup* g = cat().groupByKey(gkey)) return g->id;
    const BaseGroup* bg = nullptr;
    for (const BaseGroup& b : kGroups)
      if (std::string_view(b.key) == gkey) bg = &b;
    if (!bg) return 0;
    const Id parent = groupOf(bg->parent);
    // Группа с тем же названием и родителем без ключа становится базовой.
    for (ResGroup& g : cat().resGroups)
      if (g.key.empty() && g.parent == parent && key(g.name) == key(bg->name)) {
        g.key = bg->key;
        return g.id;
      }
    ResGroup g;
    g.id = tx.nextId(Seq::ResGroup);
    g.name = bg->name;
    g.parent = parent;
    g.key = bg->key;
    cat().resGroups.push_back(g);
    groups++;
    return g.id;
  }

  // Ресурс по названию: существующий без группы получает группу; нет — создаётся.
  Id resource(const Named& n, Id group, const char* rkey = nullptr) {
    CatalogItem* found = nullptr;
    if (rkey)
      for (CatalogItem& c : cat().resources)
        if (c.key == rkey) found = &c;
    if (!found) found = byName(cat().resources, n.name);
    if (found) {
      if (!found->group && group) found->group = group;
      if (rkey && found->key.empty()) found->key = rkey;
      return found->id;
    }
    CatalogItem it;
    it.id = tx.nextId(Seq::Resource);
    it.name = n.name;
    it.color = Color::hex(n.color);
    it.icon = n.icon;
    it.group = group;
    if (rkey) {
      it.key = rkey;
      it.builtin = true;
    }
    cat().resources.push_back(std::move(it));
    resources++;
    return cat().resources.back().id;
  }

  void builtinResources() {
    for (const schema::BuiltinResource& b : schema::kBuiltinResources) {
      if (std::string_view(b.key) == schema::kResGold) continue;
      const Id id = resource(Named{b.name, b.color, b.icon}, groupOf(b.group), b.key);
      for (CatalogItem& c : cat().resources)
        if (c.id == id) c.builtin = true;
    }
  }

  // Прежний встроенный ресурс «Провизия»: нигде не используется — удаляется; иначе остаётся ресурсом группы
  // «Провизия» (запасы сохраняются, отрицательный запас становится недостачей провизии).
  void legacyProvisions(Id provGroup) {
    auto it = std::find_if(cat().resources.begin(), cat().resources.end(), [](const CatalogItem& c) { return c.key == schema::kResLegacyProvisions; });
    if (it == cat().resources.end()) return;
    const Id id = it->id;
    const World& w = tx.w();
    bool used = false;
    w.provinces.each([&](const Province& p) { used = used || p.resource == id; });
    w.factions.each([&](const Faction& f) {
      used = used || f.stock(id) != 0;
      for (const Formation& q : f.forming) used = used || q.paid.count(id);
    });
    w.buildings.each([&](const Building& b) {
      for (const BuildingLevel& l : b.levels) used = used || l.cost.count(id) || l.produce.count(id);
    });
    w.deals.each([&](const Deal& d) {
      for (const DealItem& x : d.items) used = used || (x.kind == DealItemKind::Resource && x.res == id);
    });
    for (const Constant& c : w.constants->list) used = used || c.res.count(id);
    if (!used) {
      cat().resources.erase(std::remove_if(cat().resources.begin(), cat().resources.end(), [&](const CatalogItem& c) { return c.id == id; }),
                            cat().resources.end());
      notes.push_back("прежний ресурс «Провизия» не использовался и убран: провизия — группа ресурсов");
      return;
    }
    for (CatalogItem& c : cat().resources)
      if (c.id == id) {
        c.key.clear();
        c.builtin = false;
        if (!c.group) c.group = provGroup;
      }
    std::vector<Id> debtors;
    w.factions.each([&](const Faction& f) {
      if (f.stock(id) < 0) debtors.push_back(f.id);
    });
    for (Id fid : debtors) {
      Faction& f = tx.faction(fid);
      f.provisionDebt += -f.res[id];
      f.res[id] = 0;
    }
    notes.push_back("ресурс «Провизия» перенесён в группу «Провизия»");
  }

  void groupsV1() {
    for (const BaseGroup& b : kGroups) groupOf(b.key);
    for (const GroupItems& gi : groupItems()) {
      const Id g = groupOf(gi.group);
      for (const Named& n : gi.items) resource(n, g, std::string_view(n.name) == "Запчасти механизмов" ? schema::kResMechParts : nullptr);
    }
  }

  // ---- эссенции и религии
  void essencesV1() {
    for (const Named& n : kEssences) {
      if (byName(cat().essences, n.name)) continue;
      CatalogItem it;
      it.id = tx.nextId(Seq::Essence);
      it.name = n.name;
      it.color = Color::hex(n.color);
      it.icon = n.icon;
      cat().essences.push_back(std::move(it));
      essences++;
    }
  }

  void religionsV1() {
    int k = int(cat().religions.size());
    for (const char* n : kReligions) {
      if (byName(cat().religions, n)) continue;
      CatalogItem it;
      it.id = tx.nextId(Seq::Religion);
      it.name = n;
      it.color = Color::palette(k++ * 3 + 1);
      it.icon = "religion";
      cat().religions.push_back(std::move(it));
      religions++;
    }
  }

  // ---- особый отряд «Драконы Бездны» и «Цитадель Бездны»
  void abyssV1() {
    Id special = 0;
    if (SpecialUnit* have = byName(cat().specials, kAbyssDragons)) {
      special = have->id;
    } else {
      SpecialUnit s;
      s.id = tx.nextId(Seq::Special);
      s.name = kAbyssDragons;
      s.type = UnitType::Monsters;
      if (CatalogItem* r = byName(cat().resources, "Черные Драконы")) s.keyRes = r->id;
      s.keyPer = 1;
      if (CatalogItem* r = byName(cat().resources, "Обсидиановая сталь")) s.extra[r->id] = 100;
      if (CatalogItem* e = byName(cat().essences, "Эссенция бездны")) s.essence[e->id] = 1000;
      special = s.id;
      cat().specials.push_back(std::move(s));
      specials++;
    }
    Id found = 0;
    tx.w().buildings.each([&](const Building& b) {
      if (!found && b.owner == 0 && key(b.name) == key(kAbyssCitadel)) found = b.id;
    });
    if (found) {
      Building& b = tx.building(found);
      b.specialAccess = true;
      if (std::find(b.specials.begin(), b.specials.end(), special) == b.specials.end()) b.specials.push_back(special);
      return;
    }
    // Место на схеме общего дерева: справа от общих военных построек.
    double x = 0;
    bool any = false;
    tx.w().buildings.each([&](const Building& b) {
      if (b.owner != 0 || b.cat != BuildingCat::Military) return;
      x = any ? std::max(x, b.pos.x + 280) : b.pos.x + 280;
      any = true;
    });
    Building b;
    b.owner = 0;
    b.name = kAbyssCitadel;
    b.icon = "castle";
    b.cat = BuildingCat::Military;
    b.pos = Vec2{x, 0};
    b.specialAccess = true;
    b.specials = {special};
    tx.add(std::move(b));
    buildings++;
  }

  void v1() {
    positionsV1();
    groupsV1();
    builtinResources();
    legacyProvisions(cat().groupId(schema::grp::Provisions));
    essencesV1();
    religionsV1();
    abyssV1();
  }

  // ================================================================ версия 2
  Id resId(const char* name) {
    if (std::string_view(name) == "Золото") return kGold;
    CatalogItem* r = byName(cat().resources, name);
    return r ? r->id : 0;
  }
  Id essId(const char* name) {
    CatalogItem* e = byName(cat().essences, name);
    return e ? e->id : 0;
  }

  // Валюты (п.2.9): золото, трупы, демоническая энергия, археологические сокровища, древние свитки Шантири.
  void currenciesV2() {
    for (const BaseGroup& b : kGroups) groupOf(b.key);
    builtinResources();
    const Id cur = groupOf(schema::grp::Currencies);
    for (CatalogItem& c : cat().resources)
      if (!c.group && (c.id == kGold || c.key == schema::kResCorpses || c.key == schema::kResEnergy)) c.group = cur;
    for (const NamedIn& n : resourcesV2()) resource(n.item, groupOf(n.group));
  }

  void religionsV2() {
    int k = int(cat().religions.size());
    for (const char* n : kReligionsV2) {
      if (byName(cat().religions, n)) continue;
      CatalogItem it;
      it.id = tx.nextId(Seq::Religion);
      it.name = n;
      it.color = Color::palette(k++ * 3 + 1);
      it.icon = "religion";
      cat().religions.push_back(std::move(it));
      religions++;
    }
  }

  // Группа реликвий «Археологические находки» (п.2.1).
  Id archFindsGroup() {
    if (Id g = cat().relicGroupId(schema::kRelicArchFinds)) return g;
    for (RelicGroup& g : cat().relicGroups)
      if (g.key.empty() && key(g.name) == key("Археологические находки")) {
        g.key = schema::kRelicArchFinds;
        return g.id;
      }
    RelicGroup g;
    g.id = tx.nextId(Seq::RelicGroup);
    g.name = "Археологические находки";
    g.key = schema::kRelicArchFinds;
    cat().relicGroups.push_back(g);
    relicGroups++;
    return g.id;
  }

  // Сундуки сокровищ (п.2.10) и археологические места со списками наград (п.2.4, 2.12).
  void archV2() {
    const Id finds = archFindsGroup();
    for (const arch::BaseChest& bc : arch::baseChests()) {
      if (cat().chestByKey(bc.key)) continue;
      Chest c;
      c.id = tx.nextId(Seq::Chest);
      c.key = bc.key;
      c.name = bc.name;
      cat().chests.push_back(std::move(c));
      chests++;
    }
    for (const arch::BaseChest& bc : arch::baseChests()) {
      Chest* c = nullptr;
      for (Chest& x : cat().chests)
        if (x.key == bc.key) c = &x;
      if (!c || !c->items.empty()) continue;
      for (const arch::BaseChestItem& bi : bc.items) {
        ChestItem it;
        it.kind = bi.kind;
        it.amount = bi.amount;
        switch (bi.kind) {
          case ChestItemKind::Resource:
            if (bi.resName) it.res = resId(bi.resName);
            if (bi.group) it.group = groupOf(bi.group);
            for (const char* ex : bi.exclude)
              if (Id r = resId(ex)) it.exclude.push_back(r);
            break;
          case ChestItemKind::Essence:
            if (bi.resName) it.essence = essId(bi.resName);
            break;
          case ChestItemKind::Relic:
            it.relicGroup = finds;
            it.rarities = bi.rarities;
            break;
          case ChestItemKind::Chest:
            for (const char* k : bi.chests)
              if (const Chest* x = cat().chestByKey(k)) it.chests.push_back(x->id);
            break;
          default: break;
        }
        c->items.push_back(std::move(it));
      }
    }
    for (const arch::BaseSite& bs : arch::baseSites()) {
      if (cat().archSiteByKey(bs.key)) continue;
      ArchSite s;
      s.id = tx.nextId(Seq::ArchSite);
      s.key = bs.key;
      s.name = bs.name;
      s.icon = bs.icon;
      for (int i = 0; i < kArchSlots; i++)
        for (const char* k : arch::baseRewards(bs.key, i))
          if (const Chest* x = cat().chestByKey(k)) s.rewards[size_t(i)].push_back(x->id);
      cat().archSites.push_back(std::move(s));
      sites++;
    }
  }

  void classesV2() {
    int k = int(cat().classes.size());
    for (const char* n : kClasses) {
      if (byName(cat().classes, n)) continue;
      HeroClass c;
      c.id = tx.nextId(Seq::HeroClass);
      c.name = n;
      c.icon = classIcon(n);
      c.color = Color::palette(k++ * 5 + 2);
      cat().classes.push_back(std::move(c));
      classes++;
    }
  }

  // ---- постройки, технологии, особые отряды
  Id commonBuilding(const char* name) {
    Id found = 0;
    tx.w().buildings.each([&](const Building& b) {
      if (!found && b.owner == 0 && key(b.name) == key(name)) found = b.id;
    });
    return found;
  }
  // Место новой постройки на схеме общего дерева: в дорожке её категории (положение — внутри дорожки) ниже прежних
  // построек этой дорожки, по kPerRow в ряд с шагом «Расставить» дерева построек — карточки не налагаются.
  static constexpr int kPerRow = 6;
  static constexpr double kColStep = 280, kRowStep = 124;
  struct LaneCursor {
    double y0 = -1;
    int n = 0;
  };
  std::map<int, LaneCursor> lanes;
  LaneCursor& lane(BuildingCat c) {
    LaneCursor& l = lanes[int(c)];
    if (l.y0 < 0) {
      double y = 0;
      bool any = false;
      tx.w().buildings.each([&](const Building& b) {
        if (b.owner != 0 || b.cat != c) return;
        y = any ? std::max(y, b.pos.y) : b.pos.y;
        any = true;
      });
      l.y0 = any ? y + kRowStep : 0;
    }
    return l;
  }
  Vec2 slot(BuildingCat c) {
    LaneCursor& l = lane(c);
    const Vec2 p{double(l.n % kPerRow) * kColStep, l.y0 + double(l.n / kPerRow) * kRowStep};
    l.n++;
    return p;
  }
  // Следующая группа построек дорожки — с нового ряда.
  void newRow(BuildingCat c) {
    LaneCursor& l = lane(c);
    if (l.n % kPerRow) l.n += kPerRow - l.n % kPerRow;
  }

  // Общая постройка с ключом или названием: есть — дополняется ключом, нет — создаётся (fill заполняет новую).
  template <class Fill>
  Id ensureBuilding(const char* bkey, const char* name, BuildingCat catg, const char* icon, Fill&& fill) {
    Id found = 0;
    if (bkey)
      tx.w().buildings.each([&](const Building& b) {
        if (!found && b.owner == 0 && b.key == bkey) found = b.id;
      });
    if (!found) found = commonBuilding(name);
    if (found) {
      if (bkey && tx.w().building(found)->key.empty()) tx.building(found).key = bkey;
      return found;
    }
    Building b;
    b.owner = 0;
    b.name = name;
    b.icon = icon;
    b.cat = catg;
    b.pos = slot(catg);
    if (bkey) b.key = bkey;
    fill(b);
    buildings++;
    return tx.add(std::move(b)).id;
  }

  std::map<Id, double> costOf(const std::vector<std::pair<const char*, double>>& list) {
    std::map<Id, double> m;
    for (auto& [n, v] : list)
      if (Id r = resId(n)) m[r] = v;
    return m;
  }

  Id ensureSpecial(const SpecialDef& d) {
    if (SpecialUnit* have = byName(cat().specials, d.name)) return have->id;
    SpecialUnit s;
    s.id = tx.nextId(Seq::Special);
    s.name = d.name;
    s.type = d.type;
    s.race = d.race;
    if (d.keyRes) s.keyRes = resId(d.keyRes);
    s.keyPer = std::max(1.0, d.keyPer);
    if (d.extra)
      if (Id r = resId(d.extra)) s.extra[r] = 1;
    if (d.essence)
      if (Id e = essId(d.essence)) s.essence[e] = d.essPer;
    s.upkeep = d.upkeep;
    if (d.essUpkeep > 0 && d.essence)
      if (Id e = essId(d.essence)) s.essUpkeep[e] = d.essUpkeep;
    const Id id = s.id;
    cat().specials.push_back(std::move(s));
    specials++;
    return id;
  }

  Id modifierNamed(const char* name, Fx fx, double v, const char* desc) {
    Id found = 0;
    tx.w().modifiers.each([&](const Modifier& m) {
      if (!found && m.key.empty() && key(m.name) == key(name)) found = m.id;
    });
    if (found) return found;
    Modifier m;
    m.name = name;
    m.kind = ModKind::Faction;
    m.icon = "pickaxe";
    m.color = Color::hex(0xd4a64a);
    m.desc = desc;
    m.fx[size_t(fx)] = v;
    m.fxMask |= 1u << int(fx);
    modifiers++;
    return tx.add(std::move(m)).id;
  }

  void techsV2(std::map<std::string, Id>& techOf) {
    // Технологии ветки — общие (фракция 0); ниже всех общих технологий.
    double y = 0;
    bool any = false;
    tx.w().techs.each([&](const Tech& t) {
      if (t.faction != 0) return;
      y = any ? std::max(y, t.pos.y) : t.pos.y;
      any = true;
    });
    if (any) y += 160;
    Id prev = 0;
    int col = 0;
    for (const TechDef& d : techDefs()) {
      Id found = 0;
      tx.w().techs.each([&](const Tech& t) {
        if (!found && t.faction == 0 && (t.key == d.key || (t.key.empty() && key(t.name) == key(d.name)))) found = t.id;
      });
      if (found) {
        if (tx.w().tech(found)->key.empty()) tx.tech(found).key = d.key;
        techOf[d.key] = prev = found;
        col++;
        continue;
      }
      Tech t;
      t.faction = 0;
      t.name = d.name;
      t.desc = d.desc;
      t.turns = d.turns;
      t.key = d.key;
      t.pos = Vec2{double(col++) * 280, y};
      if (Id r = resId("Археологические сокровища"); r && d.treasures > 0) t.cost[r] = d.treasures;
      if (Id r = resId("Древние свитки Шантири"); r && d.scrolls > 0) t.cost[r] = d.scrolls;
      if (prev) t.prereqs.push_back(prev);
      else t.needKeys.push_back(schema::bld::ArchGuild);
      if (d.modName) t.modifiers.push_back(modifierNamed(d.modName, d.fx, d.fxValue, d.desc));
      prev = tx.add(std::move(t)).id;
      techOf[d.key] = prev;
      techs++;
    }
  }

  void buildingsV2() {
    std::map<std::string, Id> techOf;
    techsV2(techOf);
    const Id wood = resId("Древесина"), stone = resId("Камень"), iron = resId("Железо"), steel = resId("Сталь");
    const Id gems = resId("Самоцветы"), treasure = resId("Археологические сокровища");
    // Порт (п.3.1): 5 уровней, золото за ход; только в приморской провинции.
    const Id port = ensureBuilding(schema::bld::Port, "Порт", BuildingCat::Economic, "anchor", [&](Building& b) {
      b.coastal = true;
      struct L { double w, s, g, i, st, gold; };
      const L lv[5] = {{100, 100, 20, 0, 0, 5}, {50, 50, 10, 0, 0, 10}, {50, 50, 10, 50, 0, 15}, {100, 100, 20, 100, 0, 20}, {150, 150, 30, 0, 100, 30}};
      b.levels.clear();
      for (const L& l : lv) {
        BuildingLevel x;
        if (wood) x.cost[wood] = l.w;
        if (stone) x.cost[stone] = l.s;
        x.cost[kGold] = l.g;
        if (iron && l.i > 0) x.cost[iron] = l.i;
        if (steel && l.st > 0) x.cost[steel] = l.st;
        x.produce[kGold] = l.gold;
        b.levels.push_back(std::move(x));
      }
    });
    // Верфь (п.3.2): 3 уровня, требует порт 4 уровня; уровни открывают типы кораблей.
    ensureBuilding(schema::bld::Shipyard, "Верфь", BuildingCat::Industrial, "shipyard", [&](Building& b) {
      b.coastal = true;
      b.shipyard = true;
      b.requires_.push_back(BuildingReq{port, 4});
      const u32 gal = 1u << int(ShipType::Galleon), fr = 1u << int(ShipType::Frigate), ln = 1u << int(ShipType::ShipOfLine),
                sm = 1u << int(ShipType::SeaMonster);
      struct L { double w, s, g, st; u32 ships; };
      const L lv[3] = {{100, 100, 20, 0, gal | sm}, {150, 150, 30, 150, gal | fr | sm}, {150, 150, 30, 150, gal | fr | ln | sm}};
      b.levels.clear();
      for (const L& l : lv) {
        BuildingLevel x;
        if (wood) x.cost[wood] = l.w;
        if (stone) x.cost[stone] = l.s;
        x.cost[kGold] = l.g;
        if (steel && l.st > 0) x.cost[steel] = l.st;
        x.ships = l.ships;
        b.levels.push_back(std::move(x));
      }
    });
    // Гильдия наёмников (п.3.12).
    ensureBuilding(schema::bld::MercGuild, "Гильдия Наемников", BuildingCat::Military, "mercenary", [&](Building& b) {
      b.mercenary = true;
      if (wood) b.levels[0].cost[wood] = 200;
      if (stone) b.levels[0].cost[stone] = 150;
      b.levels[0].cost[kGold] = 2;
    });
    // Шантирийские постройки преобразования (п.4.1.6–1.8) — с нового ряда дорожки.
    newRow(BuildingCat::Industrial);
    for (const ConvertDef& d : convertDefs()) {
      ensureBuilding(nullptr, d.name, BuildingCat::Industrial, "convert", [&](Building& b) {
        if (treasure) b.levels[0].cost[treasure] = d.cost;
        b.convert = true;
        for (auto& [n, v] : d.in)
          if (Id r = resId(n)) b.recipe.in.push_back(ResAmount{r, v});
        b.recipe.out = ResAmount{resId(d.out.first), d.out.second};
        b.recipe.turns = 1;
        if (auto it = techOf.find(d.tech); it != techOf.end()) b.techs.push_back(it->second);
      });
    }
    // Постройки доступа к особым отрядам (п.4.1.9–1.10, п.4.5).
    newRow(BuildingCat::Military);
    for (const AccessDef& d : accessDefs()) {
      const Id sp = ensureSpecial(d.unit);
      const Id bid = ensureBuilding(nullptr, d.building, BuildingCat::Military, d.icon, [&](Building& b) {
        b.specialAccess = true;
        b.specials = {sp};
        b.levels[0].cost = costOf(d.cost);
        if (d.essCost)
          if (Id e = essId(d.essCost)) b.levels[0].essCost[e] = d.essAmount;
        if (d.tech)
          if (auto it = techOf.find(d.tech); it != techOf.end()) b.techs.push_back(it->second);
      });
      Building& b = tx.building(bid);
      b.specialAccess = true;
      if (std::find(b.specials.begin(), b.specials.end(), sp) == b.specials.end()) b.specials.push_back(sp);
    }
    // Храмы и соборы эссенций (п.4.7): храм — 10 эссенции за ход, собор — 50; каждый собор требует ещё 4 храма той же
    // эссенции в государстве.
    std::vector<Id> essList;
    for (const CatalogItem& e : cat().essences) essList.push_back(e.id);
    newRow(BuildingCat::Religious);
    std::map<Id, Id> templeOf;
    for (Id e : essList) {
      const CatalogItem* ci = Catalogs::find(cat().essences, e);
      if (!ci || !startsWith(ci->name, "Эссенция ")) continue;
      const std::string suffix = ci->name.substr(std::string("Эссенция ").size());
      const std::string name = "Храм " + suffix;
      templeOf[e] = ensureBuilding(nullptr, name.c_str(), BuildingCat::Religious, "b-religious", [&](Building& b) {
        b.essenceGen = true;
        if (stone) b.levels[0].cost[stone] = 500;
        if (wood) b.levels[0].cost[wood] = 250;
        if (gems) b.levels[0].cost[gems] = 100;
        b.levels[0].essence[e] = 10;
      });
    }
    // Соборы — под храмами той же эссенции (тот же столбец, с нового ряда).
    newRow(BuildingCat::Religious);
    for (Id e : essList) {
      const CatalogItem* ci = Catalogs::find(cat().essences, e);
      if (!ci || !startsWith(ci->name, "Эссенция ") || !templeOf.count(e)) continue;
      const std::string suffix = ci->name.substr(std::string("Эссенция ").size());
      const std::string name = "Собор " + suffix;
      ensureBuilding(nullptr, name.c_str(), BuildingCat::Religious, "religion", [&](Building& b) {
        b.essenceGen = true;
        if (stone) b.levels[0].cost[stone] = 1500;
        if (wood) b.levels[0].cost[wood] = 750;
        if (gems) b.levels[0].cost[gems] = 300;
        b.levels[0].essCost[e] = 150;
        b.levels[0].essence[e] = 50;
        b.stateReqs.push_back(StateReq{templeOf[e], 4});
      });
    }
  }

  // «Гильдия Археологов» — культовая постройка в уникальном дереве каждого государства (п.1.10).
  void archGuilds() {
    std::vector<Id> states;
    tx.w().factions.each([&](const Faction& f) {
      if (f.isState()) states.push_back(f.id);
    });
    for (Id s : states) {
      bool have = false;
      double x = 0;
      bool any = false;
      tx.w().buildings.each([&](const Building& b) {
        if (b.owner != s) return;
        have = have || b.key == schema::bld::ArchGuild;
        x = any ? std::max(x, b.pos.x + 280) : b.pos.x + 280;
        any = true;
      });
      if (have) continue;
      tx.add(archGuildFor(s, Vec2{x, 0}));
      buildings++;
    }
  }

  // Стоимость кораблей и наименьшие значения (п.3.7–3.8); прежние цены не уменьшаются.
  void shipCostsV2() {
    for (const ShipCostDef& d : shipCosts()) {
      auto& list = tx.constants().list;
      auto it = std::find_if(list.begin(), list.end(), [&](const Constant& c) { return c.key == d.key; });
      if (it == list.end()) {
        for (const Constant& b : schema::builtinConstants())
          if (b.key == d.key) list.push_back(b);
        it = std::find_if(list.begin(), list.end(), [&](const Constant& c) { return c.key == d.key; });
        if (it == list.end()) continue;
      }
      for (auto& [n, v] : d.res)
        if (Id r = resId(n)) {
          it->minRes[r] = v;
          it->res[r] = std::max(it->res[r], v);
        }
      for (auto& [n, v] : d.ess)
        if (Id e = essId(n)) {
          it->minEss[e] = v;
          it->ess[e] = std::max(it->ess[e], v);
        }
    }
  }

  // Генерация встроенных модификаторов (некромант — эссенция смерти +10, п.1.6): записи мира без генерации дополняются.
  void builtinGensV2() {
    for (const schema::BuiltinGen& g : schema::builtinGens()) {
      Id mid = 0;
      tx.w().modifiers.each([&](const Modifier& m) {
        if (!mid && m.key == g.key && m.essGen.empty() && m.resGen.empty()) mid = m.id;
      });
      if (!mid) continue;
      const Id ref = g.essence ? essId(g.name) : resId(g.name);
      if (!ref) continue;
      Modifier& m = tx.modifier(mid);
      (g.essence ? m.essGen : m.resGen)[ref] = g.perTurn;
    }
  }

  void v2() {
    currenciesV2();
    religionsV2();
    archV2();
    classesV2();
    buildingsV2();
    archGuilds();
    shipCostsV2();
    builtinGensV2();
  }
};

std::string count(int n, const char* what) { return std::string(what) + " (" + std::to_string(n) + ")"; }

}  // namespace

const std::vector<BaseGroup>& baseGroups() {
  static const std::vector<BaseGroup> v(std::begin(kGroups), std::end(kGroups));
  return v;
}

Building archGuildFor(Id state, Vec2 pos) {
  Building b;
  b.owner = state;
  b.name = "Гильдия Археологов";
  b.icon = "pickaxe";
  b.cat = BuildingCat::Cult;
  b.key = schema::bld::ArchGuild;
  b.desc = "Открывает археологические группы и ветку технологий «Археология».";
  b.pos = pos;
  return b;
}

std::vector<std::string> seed(Tx& tx) {
  const int from = tx.w().meta->content;
  if (from >= kVersion) return {};
  Seeder s{tx};
  if (from < 1) s.v1();
  if (from < 2) s.v2();
  tx.meta().content = kVersion;
  std::vector<std::string> parts;
  if (s.positions) parts.push_back(count(s.positions, "должности"));
  if (s.essences) parts.push_back(count(s.essences, "эссенции элементов"));
  if (s.groups) parts.push_back(count(s.groups, "группы ресурсов"));
  if (s.resources) parts.push_back(count(s.resources, "ресурсы"));
  if (s.religions) parts.push_back(count(s.religions, "религии"));
  if (s.specials) parts.push_back(count(s.specials, "особые отряды"));
  if (s.buildings) parts.push_back(count(s.buildings, "постройки"));
  if (s.techs) parts.push_back(count(s.techs, "технологии общего дерева"));
  if (s.chests) parts.push_back(count(s.chests, "сундуки сокровищ"));
  if (s.sites) parts.push_back(count(s.sites, "археологические места"));
  if (s.classes) parts.push_back(count(s.classes, "классы героев"));
  if (s.relicGroups) parts.push_back(count(s.relicGroups, "группы реликвий"));
  if (s.modifiers) parts.push_back(count(s.modifiers, "модификаторы"));
  std::vector<std::string> out;
  if (!parts.empty()) out.push_back("базовые записи: " + join(parts, ", "));
  for (auto& n : s.notes) out.push_back(n);
  return out;
}

}  // namespace rg::content
