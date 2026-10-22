// Regnum — базовые записи мира, версия 3 (ТЗ «Доработки №1–4»): подклассы героев с деревьями талантов (класс «Жнец»
// стал «Мехомантом»), базовые расы и культуры, археологические находки и общие артефакты, новые ресурсы, постройки
// общего дерева (рынок, лечебница, чумная лаборатория, хранилище реликвий, казармы, шахта, лесопилка, каменоломня,
// алхимическая лаборатория и преобразователь, мясопилка, иссушительница, окропительница, сталелитейная), технология
// «Производство магического пороха», стоимость «Цитадели Бездны»; ресурсы, группы, расы и культуры — по алфавиту.
#include <cctype>

#include "core/content_seed.h"
#include "core/schema.h"

namespace rg::content::detail {

namespace {

// Базовые расы и культуры (справочники сортируются по названию).
const char* const kRaces[] = {
    "Люди", "Лесные Эльфы", "Гномы", "Вампиры", "Высшие Эльфы", "Людоящеры", "Крысолаки", "Минотавры", "Ракшасы", "Кентавры",
    "Орки", "Гарпии", "Гноллы", "Темные Эльфы", "Кровавые Эльфы", "Сумеречные Эльфы", "Светлые Эльфы", "Падшие Эльфы", "Наги",
    "Арборианы", "Эльфы Бездны", "Кристаллиты", "Сянмаорены", "Лаваниды", "Гоблины", "Демоны", "Тролли", "Огры", "Великаны",
    "Циклопы", "Сатиры", "Троглодиты", "Кобольды", "Кархароны", "Ктониды", "Морозные Эльфы", "Морские Эльфы", "Пустынные Эльфы",
    "Энты", "Дриады", "Безликие", "Ангелы", "Ибисианцы", "Канидисы", "Кицунэ", "Ши-цзы", "Они", "Луниды", "Арахниды", "Кецали",
    "Лунные Эльфы",
};
const char* const kCultures[] = {
    "Нордлинги", "Люминорская", "Авентинская", "Эйварская", "Сильванитская", "Друидический ордо-натурализм", "Триархия тьмы",
    "Умбральская", "Люминорская-Бастионная", "Абиссальные падшие", "Хтонический декаданс", "Спиритуалисты тотемов",
    "Анимисты великого духа", "Племена Необузданной стали", "Клан Клятвы", "Танато-аристократическая", "Вечный Холод", "Кланы Игнис",
    "Кузнецы Сердца Горнила", "Отрекшиеся рунологи", "Священный Сплав", "Селениты", "Ланикейская", "Сянмао-философия",
    "Магократическая", "Номократическая", "Обсидиановая", "Племена Железа", "Орда кланов", "Ягуаровые Потрошители",
    "Племена неба и земли", "Ветроградская", "Велесов хлад", "Торгарская", "Фьордовые собиратели", "Черных Скал",
    "Первородство льда", "Железные налетчики", "Валькирионская", "Мутагенный уклад Рат`гарда", "Паразитарный декаданс",
    "Бубонный синкретизм", "Иеро-хронология", "Девяти Вуалей", "Оплот Пагоды", "Канабо-Авангард", "Жемчужной Волны",
    "Небесный Мандат", "Династия Шелковой Нити", "Нити Асхи", "Культ Солнца", "Селахийская", "Хтоническая", "Плетение",
    "Призматическая",
};

// Реликвии по редкостям: археологические находки и общие артефакты. Пустые позиции ТЗ дополнены названиями в духе
// списка.
struct RelicDef {
  const char* name;
  Rarity rarity;
};
const std::vector<RelicDef>& archFinds() {
  static const std::vector<RelicDef> v = {
      {"Древняя Амфора", Rarity::Common}, {"Табличка с письменами", Rarity::Common}, {"Древний клинок", Rarity::Common},
      {"Древний доспех", Rarity::Common}, {"Древний шлем", Rarity::Common}, {"Руна на камне", Rarity::Common},
      {"Древний посох", Rarity::Common}, {"Древняя игрушка", Rarity::Common}, {"Свиток с поэзией", Rarity::Common},
      {"Древний инструмент", Rarity::Common},
      {"Племенное ожерелье", Rarity::Rare}, {"Окаменевший клык чудовища", Rarity::Rare}, {"Древняя жемчужина", Rarity::Rare},
      {"Костяной амулет", Rarity::Rare}, {"Хопеш стража династии", Rarity::Rare}, {"Щит с гравировкой Сокола", Rarity::Rare},
      {"Лаванидский хитиновый панцирь", Rarity::Rare}, {"Эмблема забытого королевства", Rarity::Rare},
      {"Древнее подношение", Rarity::Rare}, {"Медаль Силанны", Rarity::Rare},
      {"Секира древних гномов", Rarity::Epic}, {"Кольцо вождя клана", Rarity::Epic}, {"Кинжал Игг-Шайлской Ведьмы", Rarity::Epic},
      {"Четки древнего шамана", Rarity::Epic}, {"Диадема древнего аристократа", Rarity::Epic},
      {"Кулон знатного вампира древности", Rarity::Epic}, {"Элегия древности", Rarity::Epic},
      {"Шантирийский кристалл активации", Rarity::Epic}, {"Беспроглядное зеркало Безликих", Rarity::Epic},
      {"Печать шантирийского архивариуса", Rarity::Epic},
      {"Сфера Азкаала", Rarity::Legendary}, {"Шантирийский посох", Rarity::Legendary}, {"Пылающий Клинок Дракона", Rarity::Legendary},
      {"Шантирийский щит", Rarity::Legendary}, {"Шантирийский лук", Rarity::Legendary}, {"Топор Куньяка", Rarity::Legendary},
      {"Перстень Лайама Сокола", Rarity::Legendary}, {"Шантирийское копье", Rarity::Legendary},
      {"Корона Императоров Грифона", Rarity::Legendary}, {"Корона Сандро", Rarity::Legendary},
      {"Кровавый камень Дракх'лукара", Rarity::Epochal}, {"Книга Мертвых", Rarity::Epochal}, {"Скипетр Сар-Илама", Rarity::Epochal},
      {"Слеза Первородной Богини", Rarity::Epochal}, {"Меч Первого Светоносного", Rarity::Epochal},
      {"Семя Мирового Древа", Rarity::Epochal}, {"Шкатулка Безымянного Бога", Rarity::Epochal},
      {"Корона Бессмертного Императора", Rarity::Epochal}, {"Пожиратель", Rarity::Epochal},
      {"Свиток тайны Вознесения", Rarity::Epochal},
  };
  return v;
}
const std::vector<RelicDef>& commonArtifacts() {
  static const std::vector<RelicDef> v = {
      {"Стальной меч", Rarity::Common}, {"Железный кинжал", Rarity::Common}, {"Кожаный доспех", Rarity::Common},
      {"Деревянный щит", Rarity::Common}, {"Охотничий лук", Rarity::Common}, {"Бронзовый шлем", Rarity::Common},
      {"Боевой топор", Rarity::Common}, {"Копье ополченца", Rarity::Common}, {"Посох странника", Rarity::Common},
      {"Кольчужная рубаха", Rarity::Common},
      {"Посох Монаха", Rarity::Rare}, {"Руна Стихии", Rarity::Rare}, {"Связующий кристалл", Rarity::Rare},
      {"Эльфийский плащ", Rarity::Rare}, {"Гномий боевой молот", Rarity::Rare}, {"Амулет защиты", Rarity::Rare},
      {"Кольцо силы", Rarity::Rare}, {"Зачарованный кинжал", Rarity::Rare}, {"Серебряный меч охотника", Rarity::Rare},
      {"Шлем ветерана", Rarity::Rare},
      {"Лук из звездного серебра", Rarity::Epic}, {"Мифриловый доспех", Rarity::Epic}, {"Доспех из темного обсидиана", Rarity::Epic},
      {"Клинок Багровой Гвардии", Rarity::Epic}, {"Рунный Щит", Rarity::Epic}, {"Магический колчан", Rarity::Epic},
      {"Плащ ночной тени", Rarity::Epic}, {"Посох архимага", Rarity::Epic}, {"Драконья чешуйчатая броня", Rarity::Epic},
      {"Кольцо стихий", Rarity::Epic},
      {"Рунный клинок Падшего Принца", Rarity::Legendary}, {"Молот Светоносного", Rarity::Legendary},
      {"Лук из рога Единорога", Rarity::Legendary}, {"Перстень Императора Света", Rarity::Legendary},
      {"Пламенный Искуситель", Rarity::Legendary}, {"Щит Тирионгарда", Rarity::Legendary},
      {"Корона Небесного Престола", Rarity::Legendary}, {"Посох Вечного Пламени", Rarity::Legendary},
      {"Доспех Железного Короля", Rarity::Legendary}, {"Кинжал Тысячи Ядов", Rarity::Legendary},
      {"Ледяная Скорбь", Rarity::Epochal}, {"Посох Тысячи Теней", Rarity::Epochal}, {"Корона Господства", Rarity::Epochal},
      {"Коса Демитриоса", Rarity::Epochal}, {"Меч Владыки Бездны", Rarity::Epochal}, {"Клинок Бессмертного Императора", Rarity::Epochal},
      {"Меч Короля Дикой Охоты", Rarity::Epochal}, {"Клинок Царской Династии", Rarity::Epochal}, {"Посох Наз'Ферата", Rarity::Epochal},
      {"Копье Поднебесного Владыки", Rarity::Epochal},
  };
  return v;
}

// Значок подкласса — значок его класса; у «Мехоманта» свой.
const char* mechIcon() { return "factory"; }

// Значок базового таланта — по началам слов названия (основа с «$» — слово целиком; первое совпавшее правило по
// порядку), иначе общий «талант».
struct IconRule {
  const char* icon;
  std::vector<std::string_view> stems;
};
const char* talentIcon(std::string_view name) {
  static const std::vector<IconRule> rules = {
      {"flame", {"огн", "плам", "пожар", "угли", "углей", "уголь", "инферн", "пепел", "пепл", "жар", "испепел",
                 "пыла", "горящ", "раскален", "раскалён", "горен", "сжиг", "воспламен", "изверж", "преисподн",
                 "адск", "тепл", "взрыв", "детонат", "феникс", "дым", "искр"}},
      {"frost", {"лед", "лёд", "льд", "мороз", "снеж", "зим", "холод", "иней", "стуж", "вьюг", "метел", "замороз",
                 "заморож", "север", "ноль$"}},
      {"bolt", {"молни", "гроз", "гром", "разряд", "шторм", "бур", "электр", "вольт", "статич", "заряд", "импульс",
                "раскат"}},
      {"plague", {"зараз", "токси", "нейротокс", "яд$", "яды$", "яда$", "ядом$", "ядов", "отрав", "чум", "нарыв",
                  "инфек", "гнил", "миазм", "эпидем", "язв", "скверн", "мутац", "разлож", "разлаг", "кислот", "едк",
                  "септич", "сыпн", "грызун", "паразит", "катализ", "пропитк"}},
      {"skull", {"смерт", "некро", "мертв", "могил", "кост", "череп", "труп", "кадавр", "жатв", "гибел", "умерщв",
                 "тлен", "проклят", "сглаз", "вуду", "порч", "саркофаг", "гробниц", "мумиф", "погреб", "окоченен"}},
      {"heal", {"исцел", "целеб", "милосерд", "спасен", "омола", "лечеб", "отвар", "регенер", "восстанов", "жизн",
                "лечен", "живит", "настой"}},
      {"heart", {"самопожертв", "кров", "сердц", "вампир", "жажд", "плот", "орган", "сосуд", "гемогл", "анеми",
                 "жил$", "жилы$", "голод", "жертв", "пожертв", "рана", "раны", "ранен", "боль$", "боли$", "болью$",
                 "агони", "мук$", "муки$"}},
      {"shackles", {"оков", "цеп", "сет", "ловуш", "подчин", "контрол", "плен", "узы", "паралич", "сковыв", "подавл",
                    "покорн", "сломл", "сдержив", "тюрьм", "паутин", "паук", "шелк", "шёлк", "кокон", "захват",
                    "хватк"}},
      {"shield", {"щит", "барьер", "эгид", "бастион", "доспе", "брон", "защит", "заслон", "оборон", "стойк",
                  "непоколеб", "несокруш", "крепост", "панцир", "кожа", "стен"}},
      {"u-elementals", {"бездн", "хаос", "хаот", "энтроп", "пустот", "сингуляр", "вихр", "искаж", "разлом", "портал",
                        "измерен", "коллапс", "аномал", "циклон", "тайфун", "ветер", "ветр", "порыв", "смерч"}},
      {"eye-off", {"зеркал", "двойник", "копи", "обман", "мерца", "силуэт", "маскиров", "плащ", "бесшумн", "безмолв",
                   "подл", "засад", "скрыт", "невидим", "незамет", "рокиров"}},
      {"moon", {"тен", "тьм", "темн", "тёмн", "сумер", "мрак", "затме", "вуаль", "ночн", "ночь", "мгл", "полутен",
                "пелен", "туман"}},
      {"religion", {"искорен", "свящ", "божеств", "благо", "небес", "праведн", "молитв", "свят", "обет", "ангел",
                    "инквиз", "анафем", "искупл", "грех", "вера", "веры", "веру", "верой", "покаян"}},
      {"bow", {"стрел", "лучник", "тетив", "залп", "рикош", "обстрел", "колчан", "лук"}},
      {"sun", {"свет", "луч", "солн", "сиян", "рассвет", "заря", "зари", "озарен", "вспыш"}},
      {"u-flying", {"драко", "дракон", "крыл", "полёт", "полет", "парящ", "парени", "нетопыр", "эхолок", "ворон",
                    "перн", "перо", "перыш", "воздуш", "аэро", "цапл"}},
      {"u-beasts", {"звер", "стая", "стаи", "вожак", "когт", "оскал", "укус", "охот", "дик", "клык", "хищн", "облик",
                    "медвед", "волк", "волч", "рык", "зме"}},
      {"wood", {"лес", "чащ", "кора", "крон", "корн", "роща", "рощ", "лоз", "шип", "терн", "листв", "друид",
                "древес", "побег", "спор", "гриб", "сок$", "соки$", "соков", "живиц", "трав", "укорен", "почв",
                "перегно", "рассадн", "симбиоз", "джунгл", "природ"}},
      {"magnet", {"магнит"}},
      {"mountain", {"земл", "земн", "камен", "окамен", "горн", "гора", "горы", "монолит", "тектон", "скал", "лавин",
                    "обвал", "песч", "песок", "песк", "гранит", "пустын", "дюн"}},
      {"pickaxe", {"руд"}},
      {"sea", {"морск", "море", "моря", "морей", "волн", "прилив", "океан", "глубин", "глубок", "течени", "водн",
               "вод", "пучин", "влаг", "ливен", "ливн", "рос", "родник", "гидро", "давлен", "омовен"}},
      {"u-machines", {"голем", "меха", "сборк", "шестер", "конструк", "автомат", "паров", "поршн", "турел", "механ",
                      "детал", "ремонт", "модерниз", "привод", "экзо", "обшив", "гаек", "гайк", "разгон"}},
      {"crosshair", {"выстр", "прицел", "калибр", "снаряд", "пул", "картеч", "мушк", "порох", "снайп", "точн",
                     "анатом", "ликвид", "целеук", "болт", "шрапнел", "перезаряд", "бомб"}},
      {"eye", {"разум", "ментал", "безум", "кошмар", "ужас", "шепот", "шёпот", "страх", "иллюз", "фантом", "видени",
               "прозрен", "провид", "мысл", "психи", "ослеп", "взгляд", "транс", "замешат", "паник", "оглуш",
               "смятен", "воля$", "воли$", "волю$", "волей$", "волев"}},
      {"scales", {"баланс", "гармон", "равновес", "покой", "медитат", "ци$", "меридиан", "невозмут", "двойствен",
                  "противополож", "крайност", "дзен", "безмятеж", "приговор", "кара", "суд", "закон"}},
      {"hourglass", {"врем", "вечн", "судьб", "хроно", "мгновен", "эпох", "ускорен", "замедлен"}},
      {"crown", {"власт", "повел", "приказ", "корол", "владык", "господ", "трон", "величи", "доминир", "тиран",
                 "вожд", "декрет", "дворян", "честь", "гордын", "триумф", "слав", "фараон"}},
      {"army", {"строя", "строй", "армия", "армии", "ряд", "легион", "марш", "отряд", "миньон", "прислужн", "слуг"}},
      {"music", {"песн", "мелод", "ритм", "аккорд", "баллад", "гимн", "струн", "танец", "танц", "вальс"}},
      {"scroll", {"рун", "печат", "клейм", "знак", "символ", "глиф", "стигм", "письм"}},
      {"star", {"астрал", "звезд", "звёзд", "космос", "небосвод", "комет", "метеор"}},
      {"gem", {"призм", "кристал", "самоцвет", "алмаз"}},
      {"essence", {"аур", "энерг", "эссен", "поток", "резонан", "мана", "маны", "маной", "эфир", "иссуш", "сущност",
                   "поглощ", "кинетик", "истощ"}},
      {"wand", {"чарод", "аркан", "магич", "волшеб", "тайн", "маги"}},
      {"sparkles", {"призы", "ритуа", "тотем", "заклин", "чар", "колдов", "призрач", "дух", "душ"}},
      {"hammer", {"молот", "кузн", "наковальн", "закал"}},
      {"sword", {"клин", "лезви", "фехто", "выпад", "пируэт", "парир", "порез", "разрез", "укол", "сталь", "кинжал",
                 "меч", "нож", "топор"}},
      {"battle", {"ярост", "гнев", "неист", "берсерк", "битв", "боев", "кураж", "клич", "крик", "вызов", "рывок",
                  "натиск", "атак", "рвени", "войн", "упоени"}},
      {"swords", {"удар", "замах", "шквал", "черед", "сокруш", "круш", "раскол", "рассеч", "рубк", "кромс",
                  "контруд", "апперкот", "джеб", "кулач", "казн", "резн", "прорыв", "опустош", "сотряс"}},
      {"iron", {"желез", "титан"}},
      {"u-elementals", {"сдвиг", "фазов", "телепорт", "реальн", "катаклизм", "разрыв", "нестабил", "врата", "небыти",
                        "грань", "геометр", "вероятн", "инверс", "первоэлем", "стихи"}},
      {"compass", {"горизонт", "троп", "путь", "дорог", "отход", "странств", "следопыт", "навиг"}},
      {"coins", {"нажив", "золот", "торг", "цена", "цены", "контракт", "наём", "наем", "добыч"}},
      {"dice", {"азарт", "удач", "случа", "риск", "фортун", "трюк", "ловк", "хмел", "пошат", "шата", "кувыр",
                "падени", "фестивал"}},
      {"heal", {"очищ", "дыхан"}},
      {"trend-up", {"мастер", "идеал", "совершен", "сверх", "усилен", "перегруз", "превосх", "сила$", "силы$",
                    "сил$", "мощ", "мышеч", "атлет", "напор", "стимул"}},
  };
  // Слова названия в нижнем регистре (разделители — пробелы, дефисы и знаки препинания ASCII).
  const std::string low = utf8::lower(name);
  std::vector<std::string_view> words;
  for (size_t i = 0; i < low.size();) {
    while (i < low.size() && u8(low[i]) < 0x80 && !std::isalnum(u8(low[i]))) i++;
    const size_t b = i;
    while (i < low.size() && (u8(low[i]) >= 0x80 || std::isalnum(u8(low[i])))) i++;
    if (i > b) words.push_back(std::string_view(low).substr(b, i - b));
  }
  for (const IconRule& r : rules)
    for (std::string_view stem : r.stems)
      for (std::string_view w : words)
        if (stem.ends_with('$') ? w == stem.substr(0, stem.size() - 1) : w.starts_with(stem)) return r.icon;
  return "talent";
}

using Cost = std::vector<std::pair<const char*, double>>;
struct LevelDef {
  Cost cost;
  double gold = 0;          // золото за ход (рынок)
  double extract = 0;       // добыча +% (шахта)
};

}  // namespace

// ---- подклассы и таланты
void Seeder::classesV3() {
  // «Жнец» → «Мехомант» (ТЗ «Доработки №1», п.1.19).
  if (HeroClass* old = byName(cat().classes, "Жнец"); old && !byName(cat().classes, "Мехомант")) {
    old->name = "Мехомант";
    old->icon = mechIcon();
    notes.push_back("класс «Жнец» переименован в «Мехомант»");
  }
  // Классы без записи (мир без версии 2) создаются.
  for (const SubclassDef& d : subclassDefs())
    if (!byName(cat().classes, d.cls)) {
      HeroClass c;
      c.id = tx.nextId(Seq::HeroClass);
      c.name = d.cls;
      c.icon = std::string_view(d.cls) == "Мехомант" ? mechIcon() : "hero-class";
      c.color = Color::palette(int(cat().classes.size()) * 5 + 2);
      cat().classes.push_back(std::move(c));
      classes++;
    }
  for (const SubclassDef& d : subclassDefs()) {
    HeroClass* c = byName(cat().classes, d.cls);
    if (!c || byName(c->subclasses, d.name)) continue;
    SubClass s;
    s.id = tx.nextId(Seq::SubClass);
    s.name = d.name;
    s.icon = c->icon;
    s.tierPoints = schema::kDefaultTierPoints;
    for (size_t row = 0; row < d.tiers.size(); row++) {
      const auto& tier = d.tiers[row];
      // Ячейки яруса по середине дерева из пяти столбцов: два таланта — 1 и 3, три — 0, 2 и 4.
      static const int kTwo[] = {1, 3}, kThree[] = {0, 2, 4};
      for (size_t i = 0; i < tier.size(); i++) {
        Talent t;
        t.id = tx.nextId(Seq::Talent);
        t.name = tier[i].name;
        t.icon = talentIcon(t.name);
        t.row = int(row);
        t.col = tier.size() == 2 ? kTwo[i] : tier.size() == 3 ? kThree[i] : std::min(int(i), schema::kTalentCols - 1);
        t.ranks = tier[i].ranks;
        t.cost = tier[i].cost;
        s.talents.push_back(std::move(t));
        talents++;
      }
    }
    c->subclasses.push_back(std::move(s));
    subclasses++;
  }
}

// ---- расы, культуры
namespace {
template <class List>
void sortByName(List& list) {
  std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return compareRu(a.name, b.name) < 0; });
}
}  // namespace

void Seeder::racesCulturesV3() {
  auto add = [&](std::vector<CatalogItem>& list, Seq seq, const char* name, const char* icon, int& counter) {
    if (byName(list, name)) return;
    CatalogItem it;
    it.id = tx.nextId(seq);
    it.name = name;
    it.color = Color::palette(int(list.size()) * 7 + 3);
    it.icon = icon;
    list.push_back(std::move(it));
    counter++;
  };
  for (const char* n : kRaces) add(cat().races, Seq::Race, n, "race", races);
  for (const char* n : kCultures) add(cat().cultures, Seq::Culture, n, "culture", cultures);
  sortByName(cat().races);
  sortByName(cat().cultures);
}

// ---- ресурсы: новые записи; ресурсы внутри групп, подгруппы и группы — по алфавиту (иерархия не меняется)
void Seeder::resourcesV3() {
  for (const BaseGroup& b : baseGroups()) groupOf(b.key);
  resource(item(Named{"Сталь обсидиана бездны", 0x4a2a6a, "hammer"}, groupOf(schema::grp::MatIndustrial)));
  resource(item(Named{"Зараженный бездной обсидиан", 0x3a1a4f, "gem"}, groupOf(schema::grp::OreSpecial)));
  sortByName(cat().resGroups);
  sortByName(cat().resources);
  // Золото (казна, rs1) остаётся первой записью справочника.
  std::stable_partition(cat().resources.begin(), cat().resources.end(), [](const CatalogItem& r) { return r.id == kGold; });
}

// ---- реликвии
void Seeder::relicsV3() {
  const Id finds = archFindsGroup();
  Id general = 0;
  for (const RelicGroup& g : cat().relicGroups)
    if (!g.parent && key(g.name) == key("Общие")) general = g.id;
  if (!general) {
    RelicGroup g;
    g.id = tx.nextId(Seq::RelicGroup);
    g.name = "Общие";
    general = g.id;
    cat().relicGroups.push_back(g);
    relicGroups++;
  }
  auto add = [&](const RelicDef& d, Id group) {
    if (byName(cat().relics, d.name)) return;
    Relic r;
    r.id = tx.nextId(Seq::Relic);
    r.name = d.name;
    r.rarity = d.rarity;
    r.group = group;
    cat().relics.push_back(std::move(r));
    relics++;
  };
  for (const RelicDef& d : archFinds()) add(d, finds);
  for (const RelicDef& d : commonArtifacts()) add(d, general);
}

// ---- технология и постройки
void Seeder::buildingsV3() {
  // Технология общего дерева «Производство магического пороха» — ниже прежних общих технологий.
  Id powderTech = tx.w().techs.firstId([&](const Tech& t) { return t.faction == 0 && (t.key == schema::tech::MagicPowder || key(t.name) == key("Производство магического пороха")); });
  if (!powderTech) {
    double y = 0;
    bool any = false;
    tx.w().techs.each([&](const Tech& t) {
      if (t.faction != 0) return;
      y = any ? std::max(y, t.pos.y) : t.pos.y;
      any = true;
    });
    Tech t;
    t.faction = 0;
    t.name = "Производство магического пороха";
    t.desc = "Открывает «Алхимический преобразователь»: селитра и магическая пыль — в магический порох.";
    t.turns = 2;
    t.key = schema::tech::MagicPowder;
    t.pos = Vec2{0, any ? y + 160 : 0};
    t.cost[kGold] = 30;
    powderTech = tx.add(std::move(t)).id;
    techs++;
  } else if (tx.w().tech(powderTech)->key.empty()) {
    tx.tech(powderTech).key = schema::tech::MagicPowder;
  }

  auto levelsOf = [&](Building& b, const std::vector<LevelDef>& defs) {
    b.levels.clear();
    for (const LevelDef& d : defs) {
      BuildingLevel l;
      l.cost = costOf(d.cost);
      if (d.gold > 0) l.produce[kGold] = d.gold;
      l.extractPct = d.extract;
      b.levels.push_back(std::move(l));
    }
  };
  auto res = [&](const char* n) { return resId(n); };

  // Рынок: 5 уровней, золото за ход; «Продать реликвии».
  newRow(BuildingCat::Economic);
  adoptBuilding(schema::bld::Market, "Рынок", BuildingCat::Economic, "coins", [&](Building& b) {
    b.market = true;
    levelsOf(b, {{{{"Древесина", 100}, {"Камень", 100}, {"Золото", 20}}, 5},
                 {{{"Древесина", 50}, {"Камень", 50}, {"Золото", 10}}, 10},
                 {{{"Древесина", 50}, {"Камень", 50}, {"Золото", 10}, {"Бронза", 50}}, 15},
                 {{{"Древесина", 100}, {"Камень", 100}, {"Золото", 20}, {"Бронза", 100}}, 20},
                 {{{"Древесина", 150}, {"Камень", 150}, {"Золото", 30}, {"Бронза", 200}}, 30}});
  });
  // Инфраструктурные: лечебница, чумная лаборатория, хранилище реликвий.
  adoptBuilding(schema::bld::Infirmary, "Лечебница", BuildingCat::Infrastructure, "heal", [&](Building& b) {
    b.healing = true;
    b.levels[0].cost = costOf({{"Древесина", 150}, {"Камень", 100}, {"Золото", 15}});
  });
  adoptBuilding(schema::bld::PlagueLab, "Чумная лаборатория", BuildingCat::Infrastructure, "plague", [&](Building& b) {
    b.plague = true;
    b.essenceGen = true;
    b.levels[0].cost = costOf({{"Древесина", 300}, {"Камень", 300}, {"Железо", 100}, {"Токсины", 100}, {"Золото", 30}});
    if (Id e = essId("Эссенция чумы")) {
      b.levels[0].essCost[e] = 100;
      b.levels[0].essence[e] = 15;
    }
  });
  adoptBuilding(schema::bld::RelicVault, "Хранилище реликвий", BuildingCat::Infrastructure, "relic", [&](Building& b) {
    b.relicStore = true;
    b.levels[0].cost = costOf({{"Древесина", 100}, {"Камень", 100}, {"Серебро", 100}, {"Золото", 10}});
  });
  // Военные: казармы.
  newRow(BuildingCat::Military);
  adoptBuilding(schema::bld::Barracks, "Казармы", BuildingCat::Military, "army", [&](Building& b) {
    b.upkeepCut = true;
    b.levels[0].cost = costOf({{"Древесина", 100}, {"Камень", 300}, {"Сталь", 100}, {"Золото", 50}});
    b.levels[0].upkeepCut = 1;
  });
  // Промышленные.
  newRow(BuildingCat::Industrial);
  adoptBuilding(schema::bld::Mine, "Шахта", BuildingCat::Industrial, "pickaxe", [&](Building& b) {
    b.extract = true;
    for (const char* g : {schema::grp::OreCommon, schema::grp::OreSpecial, schema::grp::MatPrecious})
      if (Id id = groupOf(g)) b.extractGroups.push_back(id);
    if (Id pearl = res("Жемчуг")) b.extractExclude.push_back(pearl);
    levelsOf(b, {{{{"Древесина", 200}, {"Камень", 200}, {"Золото", 20}}, 0, 25},
                 {{{"Древесина", 100}, {"Камень", 100}, {"Золото", 15}}, 0, 50},
                 {{{"Древесина", 150}, {"Камень", 150}, {"Золото", 20}, {"Механические детали", 50}}, 0, 75},
                 {{{"Древесина", 200}, {"Камень", 200}, {"Золото", 30}, {"Механические детали", 100}}, 0, 100},
                 {{{"Древесина", 400}, {"Камень", 400}, {"Золото", 50}, {"Механические детали", 200}, {"Сталь", 400}}, 0, 200}});
  });
  adoptBuilding(schema::bld::Sawmill, "Лесопилка", BuildingCat::Industrial, "wood", [&](Building& b) {
    b.levels[0].cost = costOf({{"Древесина", 200}, {"Камень", 100}, {"Золото", 10}});
    if (Id wood = res("Древесина")) {
      b.levels[0].produce[wood] = 20;
      b.levels[0].produceOnRes[wood] = 100;
    }
  });
  adoptBuilding(schema::bld::Quarry, "Каменоломня", BuildingCat::Industrial, "stone", [&](Building& b) {
    b.levels[0].cost = costOf({{"Древесина", 100}, {"Камень", 200}, {"Золото", 10}});
    if (Id stone = res("Камень")) {
      b.levels[0].produce[stone] = 20;
      b.levels[0].produceOnRes[stone] = 100;
    }
  });
  adoptBuilding(schema::bld::AlchemyLab, "Алхимическая лаборатория", BuildingCat::Industrial, "sparkles", [&](Building& b) {
    b.levels[0].cost = costOf({{"Древесина", 100}, {"Камень", 100}, {"Золото", 30}, {"Ртуть", 50}, {"Сера", 50}});
    if (Id dust = res("Магическая пыль")) b.levels[0].produce[dust] = 10;
  });
  auto recipe = [&](const char* name, const Cost& in, const char* out) {
    Recipe r;
    r.name = name ? name : "";
    for (auto& [n, v] : in)
      if (Id id = res(n)) r.in.push_back(ResAmount{id, v});
    r.out = {ResAmount{res(out), 1}};
    r.turns = 1;
    return r;
  };
  adoptBuilding(schema::bld::AlchemyConverter, "Алхимический преобразователь", BuildingCat::Industrial, "convert", [&](Building& b) {
    b.levels[0].cost = costOf({{"Древесина", 200}, {"Камень", 200}, {"Золото", 50}, {"Магическая пыль", 200}});
    b.convert = true;
    b.recipes.push_back(recipe(nullptr, {{"Селитра", 1}, {"Магическая пыль", 1}}, "Магический порох"));
    b.techs.push_back(powderTech);
  });
  adoptBuilding(schema::bld::CorpseMill, "Мясопилка", BuildingCat::Industrial, "skull", [&](Building& b) {
    b.levels[0].cost = costOf({{"Древесина", 50}, {"Камень", 100}, {"Золото", 10}});
    if (Id corpses = cat().resourceId(schema::kResCorpses)) b.levels[0].produce[corpses] = 100;
    b.stateKinds = 1u << int(StateKind::Undead);
  });
  adoptBuilding(schema::bld::Drainer, "Иссушительница", BuildingCat::Industrial, "essence", [&](Building& b) {
    b.levels[0].cost = costOf({{"Древесина", 150}, {"Камень", 150}, {"Золото", 30}, {"Багряная сталь", 100}});
    b.drain = true;
    b.levels[0].drainPop = 100;
    if (Id e = essId("Эссенция крови")) b.levels[0].drainEss[e] = 50;
  });
  adoptBuilding(schema::bld::Sprinkler, "Окропительница", BuildingCat::Industrial, "convert", [&](Building& b) {
    b.levels[0].cost = costOf({{"Древесина", 100}, {"Камень", 100}, {"Золото", 30}, {"Медь", 50}});
    b.convert = true;
    Recipe r = recipe(nullptr, {{"Железо", 1}}, "Алое железо");
    if (Id e = essId("Эссенция крови")) r.essIn.push_back(ResAmount{e, 1});
    b.recipes.push_back(std::move(r));
  });
  adoptBuilding(schema::bld::Steelworks, "Сталелитейная", BuildingCat::Industrial, "factory", [&](Building& b) {
    b.levels[0].cost = costOf({{"Древесина", 300}, {"Камень", 300}, {"Золото", 50}, {"Железо", 100}});
    b.convert = true;
    b.recipes = {
        recipe("Сталь", {{"Железо", 1}, {"Уголь", 1}}, "Сталь"),
        recipe("Багряная сталь", {{"Алое железо", 1}, {"Уголь", 1}}, "Багряная сталь"),
        recipe("Оскверненная сталь", {{"Оскверненное железо", 1}, {"Уголь", 1}}, "Оскверненная сталь"),
        recipe("Освященная сталь", {{"Освященное железо", 1}, {"Уголь", 1}}, "Освященная сталь"),
        recipe("Проклятая обсидиановая сталь", {{"Проклятый обсидиан", 1}, {"Уголь", 1}}, "Проклятая обсидиановая сталь"),
        recipe("Мифриловый сплав", {{"Мифрил", 1}, {"Уголь", 1}, {"Сталь", 1}}, "Мифриловый сплав"),
        recipe("Дамасская сталь", {{"Черное железо", 1}, {"Уголь", 1}}, "Дамасская сталь"),
        recipe("Бронза", {{"Медь", 1}, {"Уголь", 1}, {"Олово", 1}}, "Бронза"),
        recipe("Сталь обсидиана бездны", {{"Зараженный бездной обсидиан", 1}, {"Уголь", 1}}, "Сталь обсидиана бездны"),
    };
  });
  // «Цитадель Бездны»: 5000 стали обсидиана бездны и 5000 эссенции бездны; без генерации эссенции.
  if (Id cit = commonBuilding("Цитадель Бездны")) {
    Building& b = tx.building(cit);
    b.essenceGen = false;
    if (b.levels.empty()) b.levels.push_back(BuildingLevel{});
    for (BuildingLevel& l : b.levels) l.essence.clear();
    if (Id st = res("Сталь обсидиана бездны")) b.levels[0].cost = {{st, 5000}};
    if (Id e = essId("Эссенция бездны")) b.levels[0].essCost = {{e, 5000}};
  }
}

void Seeder::v3() {
  classesV3();
  racesCulturesV3();
  resourcesV3();
  relicsV3();
  buildingsV3();
}

}  // namespace rg::content::detail
