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
  {"residential", "Жилые", "b-residential", 0, 0x4f9d69},
  {"religious", "Религиозные", "b-religious", 0, 0x5b8fd6},
  {"cult", "Культовые", "b-cult", 0, 0x9b5bd6},
};

const EnumInfo kFactionKinds[2] = {
  {"state", "Государство", "crown"},
  {"guild", "Торговая гильдия", "guild"},
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
};

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

const BuiltinResource kBuiltinResources[4] = {
  {kResGold, "Золото", 0xe2b33c, "coins", nullptr},
  {kResCorpses, "Трупы", 0x8e8a7e, "skull", nullptr},
  {kResEnergy, "Демоническая энергия", 0xc0392b, "flame", nullptr},
  {kResMechParts, "Запчасти механизмов", 0x8d97a5, "hammer", grp::MatIndustrial},
};

const BuiltinResource* builtinResource(std::string_view key) {
  for (const BuiltinResource& b : kBuiltinResources)
    if (key == b.key) return &b;
  return nullptr;
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
  } else if (t == UnitType::Monsters) {
    k.group = grp::Monsters;
  } else if (t == UnitType::Machines) {
    k.resKey = kResMechParts;
    k.fixedOne = false;
  }
  return k;
}
bool needsKeyResource(UnitType t) {
  const KeyRule k = keyRule(t);
  return k.group || k.resKey;
}

bool isRuleGroup(std::string_view key) {
  return key == grp::Provisions || key == grp::Beasts || key == grp::MountsGround || key == grp::MountsFlying || key == grp::Monsters;
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
    return v;
  }();
  return list;
}

const Modifier* builtinModifier(std::string_view key) {
  for (const Modifier& m : builtinModifiers())
    if (m.key == key) return &m;
  return nullptr;
}

bool isNatureKey(std::string_view key) { return key == mod::Living || key == mod::Undead || key == mod::Demon || key == mod::Mechanism; }

bool isAutoKey(std::string_view key) {
  return key == mod::Capital || key == mod::Decentralization || key == mod::WeakControl || key == mod::Centralized || key == mod::Famine;
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
    races.values = {kRaceLiving, kRaceDemonic, kRaceUndead, kRaceMechanical, kRaceElemental};
    races.builtin = true;
    races.desc = "Раса отряда: нежить и механизмы при мятеже остаются верными";
    v.push_back(races);
    v.push_back(num(cst::CorpsesPerUnit, "Трупов на 1 воина-нежить", 1, "Сколько трупов уходит на одного воина с расой «Нежить»"));
    v.push_back(num(cst::EnergyPerUnit, "Демонической энергии на 1 воина-демона", 100,
                    "Сколько демонической энергии уходит на одного воина с расой «Демонический»"));
    return v;
  }();
  return list;
}

const char* shipCostKey(ShipType t) {
  switch (t) {
    case ShipType::ShipOfLine: return cst::ShipLineCost;
    case ShipType::Frigate: return cst::FrigateCost;
    case ShipType::Galleon: return cst::GalleonCost;
    default: return cst::FrigateCost;
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

const char* idPrefix(Seq s) {
  switch (s) {
    case Seq::Province: return "p";
    case Seq::Faction: return "f";
    case Seq::Character: return "c";
    case Seq::Modifier: return "m";
    case Seq::Building: return "b";
    case Seq::Tech: return "t";
    case Seq::Army: return "a";
    case Seq::Route: return "r";
    case Seq::Deal: return "d";
    case Seq::Log: return "l";
    case Seq::Row: return "u";
    case Seq::Council: return "k";
    case Seq::Node: return "";
    case Seq::Edge: return "";
    case Seq::Resource: return "rs";
    case Seq::Race: return "rc";
    case Seq::Culture: return "cu";
    case Seq::Religion: return "rl";
    case Seq::Government: return "gv";
    case Seq::Position: return "po";
    case Seq::Symbol: return "";
    case Seq::Shape: return "";
    case Seq::Essence: return "es";
    case Seq::Relic: return "re";
    case Seq::Special: return "su";
    case Seq::ResGroup: return "rg";
    default: return "x";
  }
}

}  // namespace rg::schema
