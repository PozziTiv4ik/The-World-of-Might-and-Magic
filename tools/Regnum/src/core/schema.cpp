// Regnum — таблицы перечислений и эффектов.
#include "core/schema.h"

namespace rg::schema {

const EnumInfo kUnitTypes[int(UnitType::Count)] = {
  {"light_inf", "Лёгкая пехота", "u-light-inf"},
  {"medium_inf", "Средняя пехота", "u-medium-inf"},
  {"heavy_inf", "Тяжёлая пехота", "u-heavy-inf"},
  {"light_cav", "Лёгкая кавалерия", "u-light-cav"},
  {"medium_cav", "Средняя кавалерия", "u-medium-cav"},
  {"heavy_cav", "Тяжёлая кавалерия", "u-heavy-cav"},
  {"air_cav", "Воздушная кавалерия", "u-air-cav"},
  {"flying", "Летающие отряды", "u-flying"},
  {"casters", "Колдующие отряды", "u-casters"},
  {"ranged", "Стрелки", "u-ranged"},
  {"beasts", "Звери", "u-beasts"},
  {"monsters", "Чудовища", "u-monsters"},
  {"machines", "Военные механизмы", "u-machines"},
  {"elementals", "Элементали", "u-elementals"},
};

const EnumInfo kShipTypes[int(ShipType::Count)] = {
  {"ship_line", "Линкор", "s-ship-line", 1},
  {"frigate", "Фрегат", "s-frigate", 1},
  {"galleon", "Торговый галеон", "s-galleon", 0},
  {"sea_monster", "Морское чудовище", "s-sea-monster", 1},
};

const EnumInfo kRelStatus[4] = {
  {"war", "В войне", "war", 0, 0xd0573f},
  {"alliance", "В союзе", "alliance", 0, 0x4f9d69},
  {"neutral", "Статус-кво", "status-quo", 0, 0x9aa3b2},
  {"unknown", "Незнакомы", "unknown", 0, 0x6b7280},
};

const EnumInfo kProvSizes[3] = {
  {"small", "Маленькая", "size-s", 1},
  {"medium", "Средняя", "size-m", 2},
  {"large", "Большая", "size-l", 3},
};

const EnumInfo kCityTypes[4] = {
  {"outpost", "Аванпост", "c-outpost", 0},
  {"village", "Деревня", "c-village", 1},
  {"town", "Небольшой город", "c-town", 2},
  {"city", "Большой город", "c-city", 3},
};

const EnumInfo kBuildingCats[int(BuildingCat::Count)] = {
  {"military", "Военные", "b-military", 0, 0xd0573f},
  {"economic", "Экономические", "b-economic", 0, 0xd6a531},
  {"industrial", "Промышленные", "b-industrial", 0, 0x7f8a99},
  {"residential", "Жилые и сельско-хозяйственные", "b-residential", 0, 0x4f9d69},
  {"religious", "Религиозные", "b-religious", 0, 0x5b8fd6},
  {"cult", "Культовые", "b-cult", 0, 0x9b5bd6},
  {"infrastructure", "Инфраструктурные", "b-infrastructure", 0, 0x3fae9f},
};

const EnumInfo kFactionKinds[3] = {
  {"state", "Государство", "crown"},
  {"guild", "Торговая гильдия", "guild"},
  {"wild", "Без государства", "skull"},
};

const EnumInfo kDealKinds[3] = {
  {"trade", "Торговая сделка", "trade"},
  {"tribute", "Дань", "tribute"},
  {"reparations", "Репарации", "reparations"},
};

const EnumInfo kDealModes[2] = {
  {"once", "Разово", "bolt"},
  {"turn", "Каждый ход", "repeat"},
};

const EnumInfo kLogKinds[int(LogKind::Count)] = {
  {"turn", "Ход", "hourglass"},
  {"economy", "Экономика", "treasury"},
  {"build", "Строительство", "build"},
  {"tech", "Технологии", "tech"},
  {"war", "Война", "war"},
  {"battle", "Битва", "battle"},
  {"army", "Войска", "army"},
  {"fleet", "Флот", "fleet"},
  {"diplomacy", "Дипломатия", "diplomacy"},
  {"trade", "Торговля", "trade"},
  {"province", "Провинция", "province"},
  {"guild", "Гильдия", "guild"},
  {"population", "Население", "population"},
  {"note", "Запись", "note"},
  {"arch", "Археология", "pickaxe"},
};

const EnumInfo kFlagPatterns[int(FlagPattern::Count)] = {
  {"solid", "Однотонный", "flag"}, {"h2", "Две полосы", "flag"}, {"h3", "Три полосы", "flag"},
  {"v2", "Два столбца", "flag"}, {"v3", "Три столбца", "flag"}, {"cross", "Крест", "flag"},
  {"saltire", "Косой крест", "flag"}, {"quarters", "Четверти", "flag"}, {"bend", "Перевязь", "flag"},
  {"chevron", "Стропило", "flag"}, {"border", "Кайма", "flag"}, {"canton", "Крыж", "flag"},
  {"chief", "Глава", "flag"}, {"pale", "Столб", "flag"},
};

const EnumInfo kOccupiedIncome[3] = {
  {"owner", "Владельцу", "crown"},
  {"occupier", "Оккупанту", "occupied"},
  {"none", "Никому", "close"},
};

const EffectInfo kEffects[kFxCount] = {
  {Fx::PopGrowthPct, "popGrowthPct", "Прирост населения за ход", "%", -50, 50, true, true, false, false, "population"},
  {Fx::TradePct, "tradePct", "Торговая ценность, % от базовой", "%", -100, 100, true, false, false, false, "trade-value"},
  {Fx::TradeFlat, "tradeFlat", "Торговая ценность, количество", "", -5000, 5000, true, false, false, false, "trade-value"},
  {Fx::BuildCostPct, "buildCostPct", "Стоимость строительства", "%", -100, 100, true, false, false, false, "build-cost"},
  {Fx::ContentmentPerTurn, "contentmentPerTurn", "Довольство населения за ход", "", -25, 25, true, true, false, false, "contentment"},
  {Fx::RebellionPct, "rebellionPct", "Вероятность восстания", "%", -50, 50, true, false, false, false, "rebellion"},
  {Fx::ResourcePct, "resourcePct", "Добыча ресурса, %", "%", -100, 100, true, false, false, false, "resource"},
  {Fx::ResourceFlat, "resourceFlat", "Добыча ресурса, количество", "", -100, 100, true, false, false, false, "resource"},
  {Fx::Slots, "slots", "Слоты построек", "", -5, 5, true, false, false, true, "slots"},
  {Fx::IncomePct, "incomePct", "Доход в казну", "%", -50, 50, false, false, false, false, "treasury"},
  {Fx::DiplomacyPerTurn, "diplomacyPerTurn", "Отношения за ход", "", -25, 25, false, true, true, false, "diplomacy"},
  {Fx::ArmyUpkeepPct, "armyUpkeepPct", "Содержание войск", "%", -75, 75, false, false, false, false, "army-upkeep"},
  {Fx::FleetUpkeepPct, "fleetUpkeepPct", "Содержание флота", "%", -75, 75, false, false, false, false, "fleet-upkeep"},
  {Fx::ResearchTimePct, "researchTimePct", "Время исследования технологий", "%", -75, 400, false, false, false, false, "research"},
  {Fx::LoyaltyPerTurn, "loyaltyPerTurn", "Верность войска за ход", "%", -50, 50, false, true, false, false, "shield", true},
  {Fx::ArchSuccessPct, "archSuccessPct", "Шанс успеха археологических групп", "%", 1, 5, false, false, false, false, "target", false, true},
  {Fx::ArchVitalityPct, "archVitalityPct", "Живучесть археологических групп", "%", 1, 10, false, false, false, false, "heart", false, true},
  {Fx::ArchExpPct, "archExpPct", "Опыт археологических групп", "%", 1, 20, false, false, false, false, "trend-up", false, true},
  {Fx::ArchUpkeepPct, "archUpkeepPct", "Содержание археологических групп", "%", -50, 50, false, false, false, false, "expense", false, true},
  {Fx::ArchTreasurePct, "archTreasurePct", "Археологические сокровища от действий групп", "%", 1, 100, false, false, false, false, "amphora"},
  {Fx::ArchDiscoveryPct, "archDiscoveryPct", "Шанс обнаружения археологического места", "%", 1, 50, false, false, false, false, "search"},
  {Fx::ArchStartLevel, "archStartLevel", "Начальный уровень археологических групп", "", 1, 4, false, false, false, false, "star"},
  {Fx::AssassinPct, "assassinPct", "Успех заказного убийства героя", "%", -100, 100, false, false, false, false, "crosshair", false, false, true},
  {Fx::PlotAssassinPct, "plotAssassinPct", "Успех заказных убийств заговоров против других государств", "%", -100, 100, false, false, false, false,
   "crosshair"},
  {Fx::AssassinGuardPct, "assassinGuardPct", "Успех заказных убийств против героев государства", "%", -100, 100, false, false, false, false,
   "shield"},
};

int industryMultOf(std::string_view key) {
  const std::string_view p = tech::IndustryPrefix;
  if (key.size() <= p.size() || key.substr(0, p.size()) != p) return 0;
  int n = 0;
  for (char ch : key.substr(p.size())) {
    if (ch < '0' || ch > '9' || n > kMaxProductionMult) return 0;
    n = n * 10 + (ch - '0');
  }
  return n >= 2 && n <= kMaxProductionMult ? n : 0;
}

std::string industryKey(int mult) { return std::string(tech::IndustryPrefix) + std::to_string(mult); }

const std::vector<PlotArmsInfo>& plotArms() {
  static const std::vector<PlotArmsInfo> v = {
      {PlotArms::Firearms, {"Порох", "Магический порох"}},
      {PlotArms::Steel, {"Багряная сталь", "Дамасская сталь", "Мифриловый сплав", "Обсидиановая сталь", "Освященная сталь", "Оскверненная сталь",
                         "Проклятая обсидиановая сталь", "Сталь", "Сталь обсидиана бездны"}},
      {PlotArms::MoonSilver, {"Лунное серебро"}},
      {PlotArms::Iron, {"Бронза", "Алое железо", "Железо", "Освященное железо", "Оскверненное железо", "Черное железо", "Зараженный бездной обсидиан",
                        "Обсидиан", "Проклятый обсидиан", "Темный обсидиан"}},
      {PlotArms::MechParts, {"Механические детали"}},
      {PlotArms::SpareParts, {"Запчасти механизмов"}},
  };
  return v;
}

const EnumInfo kStateKinds[int(StateKind::Count)] = {
  {"living", "Государство живых", "heart"},
  {"undead", "Государство нежити", "skull"},
  {"demonic", "Государство демонов", "flame"},
};

const EnumInfo kModKinds[int(ModKind::Count)] = {
  {"any", "Везде", "sparkles"},
  {"province", "Для провинций", "province"},
  {"faction", "Глобальный", "crown"},
  {"army", "Для армий", "army"},
  {"hero", "Для героев", "hero"},
  {"arch", "Для археологических групп", "pickaxe"},
};

const EnumInfo kPlotKinds[int(PlotKind::Count)] = {
  {"external", "Против другого государства", "target"},
  {"internal", "Внутренний заговор", "eye-off"},
};

const EnumInfo kDealItemKinds[int(DealItemKind::Count)] = {
  {"res", "Ресурс", "coins"},
  {"province", "Провинция", "province"},
  {"hero", "Пленный герой", "hero"},
};

const EnumInfo kConstTypes[int(ConstType::Count)] = {
  {"number", "Число", "hash"},
  {"resources", "Список ресурсов", "coins"},
  {"values", "Список значений", "list"},
};

const EnumInfo kChestItemKinds[int(ChestItemKind::Count)] = {
  {"treasure", "Археологическая награда", "amphora"},
  {"gold", "Золото", "treasury"},
  {"res", "Ресурс", "resource"},
  {"relic", "Артефакт", "relic"},
  {"essence", "Эссенция", "essence"},
  {"chest", "Сундук сокровищ", "chest"},
};

const BuiltinResource kBuiltinResources[6] = {
  {kResGold, "Золото", 0xe2b33c, "coins", grp::Currencies},
  {kResCorpses, "Трупы", 0x8e8a7e, "skull", grp::Currencies},
  {kResEnergy, "Демоническая энергия", 0xc0392b, "flame", grp::Currencies},
  {kResMechParts, "Запчасти механизмов", 0x8d97a5, "hammer", grp::MatIndustrial},
  {kResArchTreasure, "Археологические сокровища", 0xc4673d, "amphora", grp::Currencies},
  {kResShantiriScrolls, "Древние свитки Шантири", 0x5fd0c0, "scroll", grp::Currencies},
};

const BuiltinResource* builtinResource(std::string_view key) {
  for (const BuiltinResource& b : kBuiltinResources)
    if (key == b.key) return &b;
  return nullptr;
}

CatalogItem builtinResourceItem(const BuiltinResource& b) {
  CatalogItem it;
  it.name = b.name;
  it.color = Color::hex(b.color);
  it.icon = b.icon;
  it.key = b.key;
  it.builtin = true;
  return it;
}

const EnumInfo kRarities[int(Rarity::Count)] = {
  {"common", "Обычная", "relic", 0, 0xe9e6dc},
  {"rare", "Редкая", "relic", 1, 0x3f8cff},
  {"epic", "Эпическая", "relic", 2, 0xa45cff},
  {"legendary", "Легендарная", "relic", 3, 0xff9b26},
  {"epochal", "Эпохальная", "relic", 4, 0xff3d3d},
};

bool needsPeople(UnitType t) {
  return t != UnitType::Beasts && t != UnitType::Monsters && t != UnitType::Machines && t != UnitType::Elementals;
}
bool isCavalry(UnitType t) { return t == UnitType::LightCav || t == UnitType::MediumCav || t == UnitType::HeavyCav; }
bool isElemental(UnitType t) { return t == UnitType::Elementals; }

KeyRule keyRule(UnitType t) {
  KeyRule k;
  if (isCavalry(t)) {
    k.group = grp::MountsGround;
  } else if (t == UnitType::AirCav) {
    k.group = grp::MountsFlying;
  } else if (t == UnitType::Beasts) {
    k.group = grp::Beasts;
    k.exclude = grp::Monsters;
    k.exclude2 = grp::SeaMonsters;
  } else if (t == UnitType::Monsters) {
    k.group = grp::Monsters;
  } else if (t == UnitType::Machines) {
    k.resKey = kResMechParts;
    k.fixedOne = false;
  }
  return k;
}
KeyRule shipKeyRule(ShipType t) {
  KeyRule k;
  if (t == ShipType::SeaMonster) k.group = grp::SeaMonsters;
  return k;
}

bool keyAllowed(const Catalogs& c, UnitType t, Id res) {
  const KeyRule k = keyRule(t);
  if (!res || !Catalogs::find(c.resources, res)) return false;
  if (k.resKey) return c.resourceId(k.resKey) == res;
  if (!k.group) return false;
  const Id g = c.groupId(k.group);
  if (!g || !c.resourceIn(res, g)) return false;
  if (k.exclude)
    if (Id x = c.groupId(k.exclude); x && c.resourceIn(res, x)) return false;
  if (k.exclude2)
    if (Id x = c.groupId(k.exclude2); x && c.resourceIn(res, x)) return false;
  return true;
}

bool shipKeyAllowed(const Catalogs& c, ShipType t, Id res) {
  const KeyRule k = shipKeyRule(t);
  if (!res || !k.group || !Catalogs::find(c.resources, res)) return false;
  const Id g = c.groupId(k.group);
  return g && c.resourceIn(res, g);
}

bool isMercType(UnitType t) {
  return t == UnitType::LightInf || t == UnitType::MediumInf || t == UnitType::HeavyInf || t == UnitType::LightCav ||
         t == UnitType::MediumCav || t == UnitType::HeavyCav;
}

bool needsKeyResource(UnitType t) {
  const KeyRule k = keyRule(t);
  return k.group || k.resKey;
}

bool isRuleGroup(std::string_view key) {
  return key == grp::Provisions || key == grp::Beasts || key == grp::MountsGround || key == grp::MountsFlying || key == grp::Monsters ||
         key == grp::SeaMonsters || key == grp::Currencies;
}

namespace {

Modifier mk(const char* key, const char* name, ModKind kind, int duration, const char* icon, u32 color, const char* desc,
            std::initializer_list<std::pair<Fx, double>> fx = {}) {
  Modifier m;
  m.key = key;
  m.name = name;
  m.kind = kind;
  m.duration = duration;
  m.icon = icon;
  m.color = Color::hex(color);
  m.desc = desc;
  for (auto& [f, v] : fx) {
    m.fx[size_t(f)] = v;
    m.fxMask |= 1u << int(f);
  }
  return m;
}

}  // namespace

const std::vector<Modifier>& builtinModifiers() {
  static const std::vector<Modifier> list = [] {
    using F = Fx;
    std::vector<Modifier> v;
    v.push_back(mk(mod::Plundered, "Разграбленная провинция", ModKind::Province, kCaptureModTurns, "coins", 0xc0603a,
                   "Повторный захват — без «Захватить и разграбить».",
                   {{F::ContentmentPerTurn, -5}, {F::RebellionPct, 5}, {F::ResourcePct, -25}}));
    v.push_back(mk(mod::Ravaged, "Разоренная провинция", ModKind::Province, kCaptureModTurns, "war", 0xa8432c,
                   "Повторный захват — без «Захватить и разграбить» и «Разорить».",
                   {{F::ContentmentPerTurn, -8}, {F::RebellionPct, 10}, {F::ResourcePct, -50}}));
    v.push_back(mk(mod::Devastated, "Опустошенная провинция", ModKind::Province, kCaptureModTurns, "skull", 0x5d5a52,
                   "Провинцию нельзя назначить ни одному государству."));
    v.push_back(mk(mod::Discontent, "Недовольство правителем", ModKind::Hero, 0, "discontent", 0xb5523b,
                   "При мятеже войска герой переходит к мятежникам."));
    v.push_back(mk(mod::Loyalist, "Непреклонный лоялист", ModKind::Hero, 0, "shield", 0x3f7fbf,
                   "Верность его войска не уменьшается и растёт на 5 % за ход."));
    v.push_back(mk(mod::Dead, "Мертв", ModKind::Hero, 0, "skull", 0x55575c, "Снят со всех назначений и недоступен для них."));
    v.push_back(mk(mod::Living, "Живой", ModKind::Hero, 0, "heart", 0x4f9d69, ""));
    v.push_back(mk(mod::Undead, "Нежить", ModKind::Hero, 0, "skull", 0x6b6f7a, ""));
    v.push_back(mk(mod::Demon, "Демон", ModKind::Hero, 0, "flame", 0xb83a2e, ""));
    v.push_back(mk(mod::Mechanism, "Механизм", ModKind::Hero, 0, "u-machines", 0x7f8a99, ""));
    v.push_back(mk(mod::Capital, "Столица государства", ModKind::Province, 0, "capital", 0xd9a441,
                   "Ставится и снимается вместе со статусом столицы.", {{F::Slots, 3}}));
    v.push_back(mk(mod::Captive, "Взят в плен", ModKind::Hero, 0, "lock", 0x8a6d3b, "Снят со всех назначений и недоступен для них."));
    v.push_back(mk(mod::UndeadArmy, "Армия нежити", ModKind::Army, 0, "skull", 0x6b6f7a, "Верность — 100 % и не снижается."));
    v.push_back(mk(mod::DemonArmy, "Армия демонов", ModKind::Army, 0, "flame", 0xb83a2e, ""));
    v.push_back(mk(mod::Ruthless, "Безжалостная армия", ModKind::Army, 0, "swords", 0x8b2e2e, ""));
    v.push_back(mk(mod::Unrighteous, "Неправедное деяние", ModKind::Army, 0, "warning", 0xa86a2c, "", {{F::LoyaltyPerTurn, -2}}));
    v.push_back(mk(mod::Conscience, "Мучения совести", ModKind::Army, 0, "discontent", 0x9b4a3a, "", {{F::LoyaltyPerTurn, -5}}));
    v.push_back(mk(mod::Patriotism, "Патриотизм", ModKind::Army, 0, "banner", 0x3f7fbf, "", {{F::LoyaltyPerTurn, 5}}));
    v.push_back(mk(mod::Sadism, "Изуверское наслаждение", ModKind::Army, 0, "skull", 0x7a2230,
                   "Только для войск с «Армия демонов» или «Безжалостная армия».", {{F::LoyaltyPerTurn, 5}}));
    v.push_back(mk(mod::Decentralization, "Децентрализация", ModKind::Faction, 0, "council", 0xa8432c, "В совете нет назначений.",
                   {{F::LoyaltyPerTurn, -5}, {F::TradePct, -10}, {F::ContentmentPerTurn, -5}, {F::ResearchTimePct, 200}, {F::BuildCostPct, 25}}));
    v.push_back(mk(mod::WeakControl, "Слабый контроль", ModKind::Faction, 0, "council", 0xc08a3a, "В совете от 1 до 3 назначений.",
                   {{F::TradePct, -5}, {F::ContentmentPerTurn, -1}, {F::ResearchTimePct, 50}, {F::BuildCostPct, 10}}));
    v.push_back(mk(mod::Centralized, "Централизованная власть", ModKind::Faction, 0, "crown", 0x4f9d69, "В совете больше 3 назначений.",
                   {{F::TradePct, 5}, {F::ContentmentPerTurn, 1}}));
    v.push_back(mk(mod::Famine, "Голод", ModKind::Faction, 0, "grain", 0x9b6a2c, "Запас провизии меньше нуля.",
                   {{F::LoyaltyPerTurn, -1}, {F::PopGrowthPct, -1}, {F::ContentmentPerTurn, -5}}));
    v.push_back(mk(mod::UndeadWaste, "Пустошь нежити", ModKind::Province, 0, "skull", 0x4b4f58,
                   "Население — 0. Строить и получать доход с ценности может только государство нежити."));
    v.push_back(mk(mod::Desecrated, "Оскверненная провинция", ModKind::Province, 0, "flame", 0x8e2a22,
                   "Строить и получать доход с ценности может только государство демонов; даёт демоническую энергию."));
    v.push_back(mk(mod::Necromancer, "Некромант", ModKind::Hero, 0, "skull", 0x5b4a7a,
                   "После победы его войско получает трупы: 10 % побеждённых живых отрядов за каждого некроманта."));
    v.push_back(mk(mod::Lich, "Лич", ModKind::Hero, 0, "lich", 0x3f6a6a,
                   "После победы его войско получает трупы: 50 % побеждённых живых отрядов за каждого лича."));
    v.push_back(mk(mod::CouncilInfluence, "Влияние совета", ModKind::Faction, 0, "council", 0x5b8fd6,
                   "Вместе с «Централизованной властью»: значение действует за каждую должность в совете.",
                   {{F::ResearchTimePct, kCouncilInfluencePerSeat}}));
    v.push_back(mk(mod::Plague, "Чума", ModKind::Province, kPlagueTurns, "plague", 0x7f8f2a,
                   "Истекая сама, распространяется на соседние провинции.", {{F::PopGrowthPct, -5}}));
    v.push_back(mk(mod::PlagueImmunity, "Временный иммунитет", ModKind::Province, kImmunityTurns, "heal", 0x4fb09a,
                   "Пока действует, чуму установить нельзя."));
    v.push_back(mk(mod::ArchWounded, "Ранение в ходе исследования", ModKind::ArchGroup, kWoundTurns, "warning", 0xb5523b,
                   "Группу нельзя отправлять на исследования."));
    v.push_back(mk(mod::ArchPlague, "Заражение чумой", ModKind::ArchGroup, kPlagueTurns, "plague", 0x7f8f2a,
                   "Группа заражает чумой провинцию, на которую её назначают."));
    v.push_back(mk(mod::ArchValues, "Ценности археологии", ModKind::Province, 0, "amphora", 0xc4673d,
                   "Гильдия археологов: археологические сокровища каждый ход."));
    v.push_back(mk(mod::Vigilant, "Бдительный", ModKind::Hero, 0, "eye", 0x3f7fbf, "", {{F::AssassinPct, -25}}));
    v.push_back(mk(mod::Paranoid, "Параноик", ModKind::Hero, 0, "eye-off", 0x5b4a7a, "", {{F::AssassinPct, -50}}));
    v.push_back(mk(mod::Immortal, "Бессмертный", ModKind::Hero, 0, "heart", 0xd9a441, "", {{F::AssassinPct, -100}}));
    v.push_back(mk(mod::Careless, "Неосмотрительный", ModKind::Hero, 0, "dice", 0xc08a3a, "", {{F::AssassinPct, 5}}));
    v.push_back(mk(mod::Overconfident, "Чрезмерно самоуверенный", ModKind::Hero, 0, "crown", 0xb5523b, "", {{F::AssassinPct, 15}}));
    v.push_back(mk(mod::Arrested, "Под арестом", ModKind::Hero, 0, "shackles", 0x8a6d3b,
                   "В тюрьме государства: снят со всех назначений и недоступен для них, пока его не освободят."));
    v.push_back(mk(mod::PlotArrests, "Аресты заговорщиков", ModKind::Province, kPlotModTurns, "shackles", 0x9b4a3a,
                   "Приверженность заговору в провинции не растёт."));
    v.push_back(mk(mod::PlotExposed, "Раскрытый заговор", ModKind::Province, kPlotModTurns, "eye", 0xa8432c,
                   "Провинцию нельзя взять в заговор."));
    // Модификаторы уровней построек: «Жилой квартал», «Гильдия строителей», штабы разведки и контрразведки.
    static const char* const kRoman[] = {"I", "II", "III", "IV", "V"};
    static std::vector<std::string> keys, names;   // строки живут, пока живёт список (ключ и название — const char*)
    if (keys.empty()) {
      for (int i = 1; i <= 5; i++) {
        keys.push_back(levelModKey(mod::HousingLevel, i));
        names.push_back(std::string("Жилой квартал ") + kRoman[i - 1]);
        keys.push_back(levelModKey(mod::BuildersLevel, i));
        names.push_back(std::string("Гильдия строителей ") + kRoman[i - 1]);
        keys.push_back(levelModKey(mod::IntelLevel, i));
        names.push_back(std::string("Штаб Разведки ") + kRoman[i - 1]);
        if (i <= 3) {
          keys.push_back(levelModKey(mod::CounterIntelLevel, i));
          names.push_back(std::string("Штаб Контрразведки ") + kRoman[i - 1]);
        }
      }
    }
    for (size_t k = 0, i = 1; k < keys.size(); i++) {
      v.push_back(mk(keys[k].c_str(), names[k].c_str(), ModKind::Province, 0, "b-residential", 0x5f9d6a, "", {{F::PopGrowthPct, double(i)}}));
      k++;
      v.push_back(mk(keys[k].c_str(), names[k].c_str(), ModKind::Province, 0, "slots", 0xc08a3a, "", {{F::Slots, double(i)}}));
      k++;
      v.push_back(mk(keys[k].c_str(), names[k].c_str(), ModKind::Faction, 0, "crosshair", 0x5b4a7a,
                     "Успех заказных убийств заговоров против других государств.", {{F::PlotAssassinPct, 10.0 * double(i)}}));
      k++;
      if (i <= 3) {
        v.push_back(mk(keys[k].c_str(), names[k].c_str(), ModKind::Faction, 0, "shield", 0x3f7fbf, "Защита героев государства от заказных убийств.",
                       {{F::AssassinGuardPct, -10.0 * double(i)}}));
        k++;
      }
    }
    return v;
  }();
  return list;
}

const Modifier* builtinModifier(std::string_view key) {
  for (const Modifier& m : builtinModifiers())
    if (m.key == key) return &m;
  return nullptr;
}

std::string levelModKey(const char* prefix, int level) { return std::string(prefix) + std::to_string(level); }

bool isNatureKey(std::string_view key) { return key == mod::Living || key == mod::Undead || key == mod::Demon || key == mod::Mechanism; }

bool isAutoKey(std::string_view key) {
  return key == mod::Capital || key == mod::Decentralization || key == mod::WeakControl || key == mod::Centralized || key == mod::Famine ||
         key == mod::CouncilInfluence || key == mod::ArchValues;
}

const std::vector<BuiltinGen>& builtinGens() {
  static const std::vector<BuiltinGen> v = {
      {mod::Necromancer, true, "Эссенция смерти", 10},
      {mod::Lich, true, "Эссенция смерти", 150},
      {mod::ArchValues, false, "Археологические сокровища", 25},
  };
  return v;
}

const std::vector<Constant>& builtinConstants() {
  static const std::vector<Constant> list = [] {
    auto num = [](const char* key, const char* name, double v, const char* desc) {
      Constant c;
      c.key = key;
      c.name = name;
      c.type = ConstType::Number;
      c.num = v;
      c.builtin = true;
      c.desc = desc;
      return c;
    };
    auto res = [](const char* key, const char* name, const char* desc) {
      Constant c;
      c.key = key;
      c.name = name;
      c.type = ConstType::Resources;
      c.builtin = true;
      c.desc = desc;
      return c;
    };
    std::vector<Constant> v;
    v.push_back(num(cst::ColonizationCost, "Стоимость колонизации", 0, "Золото за колонизацию провинции без владельца"));
    v.push_back(res(cst::ShipLineCost, "Стоимость Линкора", "Ресурсы для постройки одного линкора"));
    v.push_back(res(cst::FrigateCost, "Стоимость Фрегата", "Ресурсы для постройки одного фрегата"));
    v.push_back(res(cst::GalleonCost, "Стоимость Торгового Галеона", "Ресурсы для постройки одного торгового галеона"));
    Constant races;
    races.key = cst::UnitRaces;
    races.name = "Расы для отрядов";
    races.type = ConstType::Values;
    races.values = {kRaceLiving, kRaceDemonic, kRaceUndead, kRaceMechanical, kRaceElemental, kRaceMercenary};
    races.builtin = true;
    races.desc = "Раса отряда: нежить и механизмы при мятеже остаются верными";
    v.push_back(races);
    v.push_back(num(cst::CorpsesPerUnit, "Трупов на 1 воина-нежить", 1, "Сколько трупов уходит на одного воина с расой «Нежить»"));
    v.push_back(num(cst::EnergyPerUnit, "Демонической энергии на 1 воина-демона", 100,
                    "Сколько демонической энергии уходит на одного воина с расой «Демонический»"));
    v.push_back(res(cst::SeaMonsterCost, "Стоимость Морского чудовища",
                    "Ресурсы и эссенции для одного морского чудовища (и ресурс подгруппы «Морские чудовища» строки флота)"));
    v.push_back(num(cst::FrigateCapacity, "Вместимость фрегата", 1000, "Сколько воинов перевозит один фрегат"));
    v.push_back(num(cst::LineCapacity, "Вместимость линкора", 5000, "Сколько воинов перевозит один линкор"));
    v.push_back(num(cst::MercPerGuild, "Наемников за гильдию наемников", 2500, "Лимит наёмников государства за каждую «Гильдию Наемников»"));
    v.push_back(num(cst::MercHire, "Найм наемника", 0.01, "Золото за найм одного наёмника (цена новой строки наёмников)"));
    v.push_back(num(cst::ArchGroupPeople, "Население на археологическую группу", 100,
                    "Сколько жителей (у государства нежити — трупов) уходит на одну археологическую группу"));
    v.push_back(num(cst::ArchGroupGold, "Золото на археологическую группу", 5, "Сколько золота стоит одна археологическая группа"));
    return v;
  }();
  return list;
}

const char* shipCostKey(ShipType t) {
  switch (t) {
    case ShipType::ShipOfLine: return cst::ShipLineCost;
    case ShipType::Frigate: return cst::FrigateCost;
    case ShipType::Galleon: return cst::GalleonCost;
    case ShipType::SeaMonster: return cst::SeaMonsterCost;
    default: return cst::FrigateCost;
  }
}

const char* shipCapacityKey(ShipType t) {
  switch (t) {
    case ShipType::ShipOfLine: return cst::LineCapacity;
    case ShipType::Frigate: return cst::FrigateCapacity;
    default: return nullptr;
  }
}

const EnumInfo kMapModes[int(MapMode::Count)] = {
  {"political", "Политическая карта", "mode-political"},
  {"guilds", "Гильдии и торговля", "mode-guilds"},
  {"contentment", "Довольство", "contentment"},
  {"rebellion", "Риск восстания", "rebellion"},
  {"trade", "Торговая ценность", "trade-value"},
  {"resources", "Ресурсы", "resource"},
  {"religion", "Религии", "religion"},
  {"culture", "Культуры", "culture"},
  {"terrain", "Чистая карта", "mode-terrain"},
};

int findEnum(const EnumInfo* list, int n, std::string_view id) {
  for (int i = 0; i < n; i++) if (id == list[i].id) return i;
  return -1;
}

const SeqInfo kSeqs[kSeqCount] = {
  {"province", "p", "провинции"},
  {"faction", "f", "фракции"},
  {"character", "c", "персонажа"},
  {"modifier", "m", "модификатора"},
  {"building", "b", "постройки"},
  {"tech", "t", "технологии"},
  {"army", "a", "войска"},
  {"route", "r", "маршрута"},
  {"deal", "d", "сделки"},
  {"log", "l", "записи хроники"},
  {"row", "u", "строки войск"},
  {"council", "k", "места совета"},
  {"node", "", "узла"},
  {"edge", "", "дуги"},
  {"resource", "rs", "ресурса"},
  {"race", "rc", "расы"},
  {"culture", "cu", "культуры"},
  {"religion", "rl", "религии"},
  {"government", "gv", "формы правления"},
  {"position", "po", "должности"},
  {"symbol", "", "знака карты"},
  {"shape", "", "фигуры карты"},
  {"essence", "es", "эссенции"},
  {"relic", "re", "реликвии"},
  {"special", "su", "особого отряда"},
  {"resGroup", "rg", "группы ресурсов"},
  {"heroClass", "hc", "класса героя"},
  {"talent", "tl", "таланта"},
  {"relicGroup", "rlg", "группы реликвий"},
  {"archSite", "as", "археологического места"},
  {"chest", "ch", "сундука сокровищ"},
  {"archGroup", "ag", "археологической группы"},
  {"subClass", "sc", "подкласса героя"},
  {"plot", "pt", "заговора"},
};

const SeqInfo& seqInfo(Seq s) {
  static const SeqInfo kUnknown{"", "x", "объекта"};
  return int(s) >= 0 && int(s) < kSeqCount ? kSeqs[int(s)] : kUnknown;
}

const CatalogListInfo kCatalogLists[kCatalogListCount] = {
  {CatalogList::Resources, "resources", Seq::Resource, &Catalogs::resources, "Ресурс", "Новый ресурс"},
  {CatalogList::Races, "races", Seq::Race, &Catalogs::races, "Раса", "Новая раса"},
  {CatalogList::Cultures, "cultures", Seq::Culture, &Catalogs::cultures, "Культура", "Новая культура"},
  {CatalogList::Religions, "religions", Seq::Religion, &Catalogs::religions, "Религия", "Новая религия"},
  {CatalogList::Governments, "governments", Seq::Government, &Catalogs::governments, "Форма правления", "Новая форма правления"},
  {CatalogList::Positions, "positions", Seq::Position, &Catalogs::positions, "Должность", "Новая должность"},
  {CatalogList::Essences, "essences", Seq::Essence, &Catalogs::essences, "Эссенция", "Новая эссенция"},
};

const CatalogListInfo& catalogListInfo(CatalogList l) {
  if (int(l) < 0 || int(l) >= kCatalogListCount) fail("Неизвестный справочник");
  return kCatalogLists[int(l)];
}

}  // namespace rg::schema
