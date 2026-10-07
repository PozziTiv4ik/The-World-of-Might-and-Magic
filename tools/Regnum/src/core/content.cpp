// Regnum — базовые записи мира: должности, эссенции, группы ресурсов, религии, особый отряд и его постройка.
#include "core/content.h"

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
};

std::string count(int n, const char* what) { return std::string(what) + " (" + std::to_string(n) + ")"; }

}  // namespace

const std::vector<BaseGroup>& baseGroups() {
  static const std::vector<BaseGroup> v(std::begin(kGroups), std::end(kGroups));
  return v;
}

std::vector<std::string> seed(Tx& tx) {
  const int from = tx.w().meta->content;
  if (from >= kVersion) return {};
  Seeder s{tx};
  if (from < 1) s.v1();
  tx.meta().content = kVersion;
  std::vector<std::string> parts;
  if (s.positions) parts.push_back(count(s.positions, "должности"));
  if (s.essences) parts.push_back(count(s.essences, "эссенции элементов"));
  if (s.groups) parts.push_back(count(s.groups, "группы ресурсов"));
  if (s.resources) parts.push_back(count(s.resources, "ресурсы"));
  if (s.religions) parts.push_back(count(s.religions, "религии"));
  if (s.specials) parts.push_back(count(s.specials, "особые отряды"));
  if (s.buildings) parts.push_back(count(s.buildings, "постройки общего дерева"));
  std::vector<std::string> out;
  if (!parts.empty()) out.push_back("базовые записи: " + join(parts, ", "));
  for (auto& n : s.notes) out.push_back(n);
  return out;
}

}  // namespace rg::content
