// Regnum — окно модификаторов (ТЗ 1.g, ТЗ «Модификаторы»): список своих и встроенных модификаторов с поиском и
// фильтром, сгруппированный по виду (для провинций, глобальные, для армий, для героев, везде; группы сворачиваются,
// поиск и фильтр их раскрывают), создание, копия, удаление своих с предупреждением о местах использования; карточка модификатора
// (название, значок, цвет, описание, где действует, срок по умолчанию), все эффекты schema::kEffects в пределах ТЗ
// (локальные, глобальные, эффекты войск), цели дипломатии, живой предпросмотр и «Где используется» со ссылками.
// Встроенные (schema::builtinModifiers, 1.1–1.26) видны всегда: пока записи мира нет, показывается шаблон, первая
// правка создаёт запись (rules::ensureBuiltinMod); встроенные не удаляются, автоматические помечены.
// Быстрый доступ — выдвижная панель «Модификаторы» на ленте слева и команда палитры.
#include <algorithm>

#include "app/app_internal.h"
#include "app/widgets.h"
#include "gfx/icons.h"

namespace rg::app {

// Общие элементы редакторов справочников (определены ниже, используются и в catalogs.cpp).
namespace edkit {
bool iconPicker(std::string_view id, std::string& icon, bool allowNone, bool disabled, std::string_view tip);
const char* iconTitle(std::string_view icon);
void iconTile(const std::string& icon, Color tint, float size, const char* fallback);
bool entityRow(std::string_view title, std::string_view subtitle, const char* icon, Color tint, std::string_view hint, bool selected,
               bool warn, std::string_view tip);
void goTo(App& a, Selection s);
void chipsBegin();
ui::ChipAction chip(std::string_view label, const ui::ChipOpt& o);
void chipsEnd();
void effectChips(const Modifier& m);
}  // namespace edkit

namespace {

using platform::Key;
using detail::later;
using rules::ModTarget;

// Фишки с переносом строк (ряд ui::HStack не переносит).
struct ChipFlow {
  ChipFlow() { edkit::chipsBegin(); }
  ~ChipFlow() { edkit::chipsEnd(); }
  ChipFlow(const ChipFlow&) = delete;
  ChipFlow& operator=(const ChipFlow&) = delete;
};

template <class V, class T>
bool has(const V& v, const T& x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}
std::string orName(const std::string& s, const char* fallback) { return s.empty() ? std::string(fallback) : s; }
const char* modIcon(const Modifier& m) { return m.icon.empty() || !gfx::hasIcon(m.icon) ? "sparkles" : m.icon.c_str(); }

// ---------------------------------------------------------------- значки на выбор
struct IconChoice {
  const char* name;
  const char* title;
};
const IconChoice kIcons[] = {
    {"sparkles", "Чудо"},        {"star", "Звезда"},          {"crown", "Корона"},        {"scroll", "Указ"},
    {"book", "Знание"},          {"tech", "Наука"},           {"research", "Исследование"}, {"wand", "Магия"},
    {"bolt", "Молния"},          {"sun", "Солнце"},           {"moon", "Луна"},           {"eye", "Око"},
    {"coins", "Монеты"},         {"treasury", "Казна"},       {"income", "Доход"},        {"expense", "Расход"},
    {"trade", "Торговля"},       {"trade-value", "Торговая ценность"}, {"route", "Тракт"}, {"scales", "Весы"},
    {"handshake", "Сделка"},     {"diplomacy", "Дипломатия"}, {"alliance", "Союз"},       {"war", "Война"},
    {"population", "Население"}, {"contentment", "Довольство"}, {"discontent", "Недовольство"}, {"rebellion", "Мятеж"},
    {"heart", "Здоровье"},       {"religion", "Вера"},        {"culture", "Культура"},    {"race", "Народ"},
    {"grain", "Зерно"},          {"wood", "Древесина"},       {"stone", "Камень"},        {"iron", "Железо"},
    {"gem", "Самоцвет"},         {"pickaxe", "Добыча"},       {"resource", "Ресурс"},     {"magnet", "Притяжение"},
    {"hammer", "Ремесло"},       {"factory", "Мастерская"},   {"building", "Постройка"},  {"house", "Жильё"},
    {"castle", "Замок"},         {"tower", "Башня"},          {"slots", "Слоты"},         {"shield", "Защита"},
    {"sword", "Меч"},            {"swords", "Битва"},         {"army", "Войско"},         {"bow", "Стрелки"},
    {"horse", "Конница"},        {"fleet", "Флот"},           {"anchor", "Гавань"},       {"skull", "Гибель"},
    {"mountain", "Горы"},        {"sea", "Море"},             {"island", "Остров"},       {"globe", "Мир"},
    {"compass", "Путь"},         {"flag", "Флаг"},            {"banner", "Знамя"},        {"hourglass", "Время"},
    {"dice", "Удача"},           {"lock", "Запрет"},          {"chronicle", "Летопись"},  {"capital", "Столица"},
    {"flame", "Пламя"},          {"hero", "Герой"},           {"council", "Совет"},       {"warning", "Беда"},
};

// ---------------------------------------------------------------- встроенные модификаторы
// Выбор шаблона встроенного модификатора, у которого ещё нет записи мира: kTplBase + номер в builtinModifiers().
constexpr Id kTplBase = 0xFFFF0000u;
int tplIndex(Id id) {
  return id >= kTplBase && size_t(id - kTplBase) < schema::builtinModifiers().size() ? int(id - kTplBase) : -1;
}
bool isAuto(const Modifier& m) { return !m.key.empty() && schema::isAutoKey(m.key); }
bool isBuiltin(const Modifier& m) { return !m.key.empty() && schema::builtinModifier(m.key) != nullptr; }

// ---------------------------------------------------------------- группы списка по виду модификатора
// Список сгруппирован по тому, где модификатор действует (ModKind); группы сворачиваются, поиск и фильтр их раскрывают.
struct KindGroup {
  ModKind kind;
  const char* title;
  const char* icon;
  const char* mark;   // метка строки группы: modifiers.group.<mark>
};
const KindGroup kKindGroups[] = {
    {ModKind::Province, "Для провинций", "province", "province"},
    {ModKind::Faction, "Глобальные", "crown", "faction"},
    {ModKind::Army, "Для армий", "army", "army"},
    {ModKind::Hero, "Для героев", "hero", "hero"},
    {ModKind::ArchGroup, "Для археологических групп", "pickaxe", "arch"},
    {ModKind::Any, "Везде", "sparkles", "any"},
};
constexpr int kKindGroupCount = int(std::size(kKindGroups));
int groupIndex(ModKind k) {
  for (int i = 0; i < kKindGroupCount; i++)
    if (kKindGroups[i].kind == k) return i;
  return kKindGroupCount - 1;
}

// Генерация за ход (ТЗ «Доработки №1», п.5): виды «Везде», «Для провинций», «Глобальный», «Для героев».
bool genKind(ModKind k) { return k == ModKind::Any || k == ModKind::Province || k == ModKind::Faction || k == ModKind::Hero; }

// Свёрнутые группы списка: состояние окна, пока открыт тот же мир (другой мир — все раскрыты).
std::array<bool, kKindGroupCount>& closedGroups(App& a) {
  static std::array<bool, kKindGroupCount> closed{};
  static u64 tag = 0;
  const Meta& m = *a.store.world().meta;
  const u64 t = hashMix(hash64(a.dataDir()), hash64(m.createdAt + "|" + m.basemap));
  if (t != tag) {
    tag = t;
    closed = {};
  }
  return closed;
}

// ---------------------------------------------------------------- использование
struct ModUse {
  std::vector<Id> provinces, states, guilds, techs, armies, heroes, positions;
  std::vector<std::pair<Id, int>> levels;   // постройка, номер уровня (с 1)
  std::vector<std::pair<Id, Id>> groups;    // археологические группы: государство, группа
  size_t total() const {
    return provinces.size() + states.size() + guilds.size() + techs.size() + levels.size() + armies.size() + heroes.size() + positions.size() +
           groups.size();
  }
};

ModUse usageOf(const World& w, Id mod) {
  ModUse u;
  if (!mod) return u;
  w.provinces.each([&](const Province& p) {
    if (has(p.modifiers, mod)) u.provinces.push_back(p.id);
  });
  w.factions.each([&](const Faction& f) {
    if (has(f.modifiers, mod)) (f.isGuild() ? u.guilds : u.states).push_back(f.id);
    for (const ArchGroup& g : f.archGroups)
      if (has(g.modifiers, mod)) u.groups.push_back({f.id, g.id});
  });
  w.techs.each([&](const Tech& t) {
    if (has(t.modifiers, mod)) u.techs.push_back(t.id);
  });
  w.buildings.each([&](const Building& b) {
    for (size_t i = 0; i < b.levels.size(); i++)
      if (has(b.levels[i].modifiers, mod)) u.levels.push_back({b.id, int(i) + 1});
  });
  w.armies.each([&](const Army& a) {
    if (has(a.modifiers, mod)) u.armies.push_back(a.id);
  });
  w.characters.each([&](const Character& c) {
    if (has(c.modifiers, mod)) u.heroes.push_back(c.id);
  });
  for (const CatalogItem& p : w.catalogs->positions)
    if (has(p.modifiers, mod) || has(p.vacantModifiers, mod)) u.positions.push_back(p.id);
  std::sort(u.provinces.begin(), u.provinces.end(), [&](Id x, Id y) { return compareRu(w.provinceName(x), w.provinceName(y)) < 0; });
  std::sort(u.states.begin(), u.states.end(), [&](Id x, Id y) { return compareRu(w.factionName(x), w.factionName(y)) < 0; });
  std::sort(u.guilds.begin(), u.guilds.end(), [&](Id x, Id y) { return compareRu(w.factionName(x), w.factionName(y)) < 0; });
  std::sort(u.heroes.begin(), u.heroes.end(), [&](Id x, Id y) { return compareRu(w.characterName(x), w.characterName(y)) < 0; });
  return u;
}

// Число мест использования каждого модификатора (для списка).
std::unordered_map<Id, int> usageCounts(const World& w) {
  std::unordered_map<Id, int> n;
  w.provinces.each([&](const Province& p) {
    for (Id m : p.modifiers) n[m]++;
  });
  w.factions.each([&](const Faction& f) {
    for (Id m : f.modifiers) n[m]++;
    for (const ArchGroup& g : f.archGroups)
      for (Id m : g.modifiers) n[m]++;
  });
  w.techs.each([&](const Tech& t) {
    for (Id m : t.modifiers) n[m]++;
  });
  w.buildings.each([&](const Building& b) {
    for (auto& l : b.levels)
      for (Id m : l.modifiers) n[m]++;
  });
  w.armies.each([&](const Army& a) {
    for (Id m : a.modifiers) n[m]++;
  });
  w.characters.each([&](const Character& c) {
    for (Id m : c.modifiers) n[m]++;
  });
  for (const CatalogItem& p : w.catalogs->positions) {
    for (Id m : p.modifiers) n[m]++;
    for (Id m : p.vacantModifiers)
      if (!has(p.modifiers, m)) n[m]++;
  }
  return n;
}

// Где сейчас действуют модификаторы, которые ставятся сами (ключ → государства; модификаторы провинций — столица,
// «Ценности археологии» — провинции).
std::map<std::string, std::vector<Id>> autoUse(const World& w) {
  std::map<std::string, std::vector<Id>> out;
  w.factions.each([&](const Faction& f) {
    if (!f.isState()) return;
    for (const rules::AutoMod& am : rules::autoModifiers(w, f.id))
      if (!am.key.empty() && !has(out[am.key], f.id)) out[am.key].push_back(f.id);
  });
  w.provinces.each([&](const Province& p) {
    if (p.sea || !p.owner) return;
    for (const rules::AutoMod& am : rules::autoProvinceModifiers(w, p.id))
      if (!am.key.empty()) out[am.key].push_back(p.id);
  });
  return out;
}

std::string usageText(const ModUse& u) {
  std::vector<std::string> parts;
  auto add = [&](size_t n, const char* one, const char* few, const char* many) {
    if (n) parts.push_back(fmtInt(i64(n)) + "\xC2\xA0" + plural(i64(n), one, few, many));
  };
  add(u.provinces.size(), "провинция", "провинции", "провинций");
  add(u.states.size(), "государство", "государства", "государств");
  add(u.guilds.size(), "гильдия", "гильдии", "гильдий");
  add(u.armies.size(), "войско", "войска", "войск");
  add(u.heroes.size(), "персонаж", "персонажа", "персонажей");
  add(u.positions.size(), "должность", "должности", "должностей");
  add(u.groups.size(), "археологическая группа", "археологические группы", "археологических групп");
  add(u.techs.size(), "технология", "технологии", "технологий");
  add(u.levels.size(), "уровень постройки", "уровня построек", "уровней построек");
  std::string s;
  for (size_t i = 0; i < parts.size(); i++) s += (i ? ", " : "") + parts[i];
  return s;
}

int effectCount(const Modifier& m) {
  int n = 0;
  for (int f = 0; f < kFxCount; f++) n += m.has(Fx(f)) ? 1 : 0;
  return n;
}
bool hasLocal(const Modifier& m) {
  for (int f = 0; f < kFxCount; f++)
    if (m.has(Fx(f)) && schema::kEffects[f].local) return true;
  return false;
}
// Глобальные эффекты и эффекты войск (у государства они действуют на все его войска).
bool hasGlobal(const Modifier& m) {
  for (int f = 0; f < kFxCount; f++)
    if (m.has(Fx(f)) && !schema::kEffects[f].local) return true;
  return false;
}
// Дипломатия включена, а цели не выбраны (ТЗ 1.g.ii.2.b: «с указанными для модификатора государствами»).
bool missingTargets(const Modifier& m) { return m.has(Fx::DiplomacyPerTurn) && m.targets.empty(); }

// Генерация за ход подписями: «+10 эссенция смерти», «+25 археологические сокровища» (у шаблона встроенного — по
// умолчанию).
std::vector<std::string> genTexts(const World& w, const Modifier& m) {
  std::vector<std::string> out;
  for (auto& [e, v] : rules::modEssGen(w, m))
    if (const CatalogItem* c = w.essence(e); c && v > 0) out.push_back("+" + fmtNum(v, 3) + " " + utf8::lower(c->name));
  for (auto& [r, v] : rules::modResGen(w, m))
    if (const CatalogItem* c = w.resource(r); c && v > 0) out.push_back("+" + fmtNum(v, 3) + (r == kGold ? " тыс. золота" : " " + utf8::lower(c->name)));
  return out;
}

// Краткая сводка эффектов для строки списка.
std::string effectSummary(const World& w, const Modifier& m) {
  int n = effectCount(m);
  const std::vector<std::string> gen = genTexts(w, m);
  n += int(gen.size());
  if (n == 0) return isAuto(m) ? "Ставится сам" : "Без эффектов";
  std::string s;
  for (int f = 0; f < kFxCount && s.empty(); f++)
    if (m.has(Fx(f))) s = w::effectText(Fx(f), m.fx[size_t(f)]);
  if (s.empty()) s = gen.front() + " за ход";
  if (n > 1) s += " · ещё " + std::to_string(n - 1);
  return s;
}

// ---------------------------------------------------------------- эффекты: группы, единицы и шаги
// Arch — эффекты археологических групп (у модификатора группы — эта группа, у глобального — все группы государства).
enum class FxGroup : u8 { Local, Global, Army, Arch };
FxGroup groupOf(int f) {
  const auto& e = schema::kEffects[f];
  return e.arch ? FxGroup::Arch : e.army ? FxGroup::Army : e.local ? FxGroup::Local : FxGroup::Global;
}
u32 archMask() {
  u32 m = 0;
  for (int f = 0; f < kFxCount; f++)
    if (schema::kEffects[f].arch) m |= 1u << f;
  return m;
}

const char* fxUnit(Fx f) {
  switch (f) {
    case Fx::PopGrowthPct:
    case Fx::LoyaltyPerTurn: return "% за ход";
    case Fx::ContentmentPerTurn:
    case Fx::DiplomacyPerTurn: return "за ход";
    case Fx::ResourceFlat: return "ед.";
    case Fx::Slots: return "слот|слота|слотов";
    case Fx::ArchStartLevel: return "уровень|уровня|уровней";
    default: return schema::effect(f).unit[0] == '%' ? "%" : "";
  }
}
int fxDigits(Fx f) { return f == Fx::TradeFlat || f == Fx::Slots || f == Fx::ArchStartLevel ? 0 : 1; }
double fxStep(Fx f) { return schema::effect(f).max >= 1000 ? 10 : 1; }
// Начальное значение при включении эффекта (заметное, в пределах ТЗ).
double fxDefault(Fx f) {
  double mx = schema::effect(f).max;
  if (mx <= 5) return 1;
  if (mx <= 50) return 5;
  if (mx <= 100) return 10;
  return 100;
}
std::string rangeText(Fx f) {
  const auto& e = schema::effect(f);
  std::string u = e.unit[0] == '%' ? "\xC2\xA0%" : "";
  return fmtSigned(e.min) + "…" + fmtSigned(e.max) + u;
}

// ---------------------------------------------------------------- строки списка
// Модификатор строки (копия на кадр: правка посреди кадра заменяет мир).
struct Entry {
  Id id = 0;            // запись мира или kTplBase + номер шаблона
  Modifier m;           // у шаблона m.id = 0
  bool builtin = false;
  int uses = 0;         // мест использования; у автоматических — где действует сейчас
};

// Свои модификаторы по названию, затем встроенные в порядке ТЗ (запись мира или шаблон).
void collect(const World& w, const std::unordered_map<Id, int>& uses, const std::map<std::string, std::vector<Id>>* autos,
             std::vector<Entry>& own, std::vector<Entry>& built) {
  auto count = [&](Id id) {
    auto it = uses.find(id);
    return it == uses.end() ? 0 : it->second;
  };
  const auto& tpl = schema::builtinModifiers();
  std::vector<Id> canonical;
  for (size_t i = 0; i < tpl.size(); i++) {
    Entry e;
    e.builtin = true;
    if (Id rid = rules::builtinModId(w, tpl[i].key)) {
      e.id = rid;
      e.m = *w.modifier(rid);
      canonical.push_back(rid);
    } else {
      e.id = kTplBase + Id(i);
      e.m = tpl[i];
    }
    e.uses = count(e.m.id);
    if (autos && isAuto(e.m))
      if (auto it = autos->find(e.m.key); it != autos->end()) e.uses = int(it->second.size());
    built.push_back(std::move(e));
  }
  w.modifiers.each([&](const Modifier& m) {
    if (has(canonical, m.id)) return;
    Entry e;
    e.id = m.id;
    e.m = m;
    e.uses = count(m.id);
    own.push_back(std::move(e));
  });
  std::sort(own.begin(), own.end(), [](const Entry& a, const Entry& b) {
    int c = compareRu(a.m.name, b.m.name);
    return c != 0 ? c < 0 : a.id < b.id;
  });
}

// ---------------------------------------------------------------- состояние окна
struct EdState {
  std::string query;
  int filter = 0;          // 0 — все, 1 — с локальными, 2 — с глобальными, 3 — нигде не используются
  bool focusName = false;
  bool focusSearch = false;
  std::string seenQuery;   // запрос и фильтр прошлого кадра (смена — выделение к первому найденному)
  int seenFilter = 0;
  Id seenSel = 0;          // выделение прошлого кадра (новое — прокрутить список к строке)
  int seenGroup = -1;      // группа выделенного прошлого кадра (новая — раскрыть её)
  std::string selKey;      // выделен встроенный: его ключ (запись отменили Ctrl+Z — выделение остаётся на шаблоне)
};

bool passes(const Entry& e, const EdState& st) {
  const Modifier& m = e.m;
  if (!st.query.empty() && !utf8::matches(m.name, st.query) && !utf8::matches(m.desc, st.query)) return false;
  switch (st.filter) {
    case 1: return hasLocal(m);
    case 2: return hasGlobal(m);
    case 3: return e.uses == 0;
    default: return true;
  }
}

// ---------------------------------------------------------------- действия
Id createModifierAct(App& a) {
  Id nid = 0;
  a.act("Новый модификатор", [&](Tx& tx) { nid = rules::createModifier(tx); });
  return nid;
}

// Правка модификатора: свой — по ID; встроенный без записи мира — запись создаётся по шаблону, окно переходит на неё.
bool editMod(App& a, const Modifier& m, std::string_view label, const std::function<void(Modifier&)>& fn, std::string coalesce = {}) {
  const Id id = m.id;
  const std::string key = m.key;
  Id made = 0;
  TxOptions opt;
  opt.coalesce = std::move(coalesce);
  bool ok = a.act(label, [&](Tx& tx) {
    made = id ? id : rules::ensureBuiltinMod(tx, key);
    fn(tx.modifier(made));
  }, opt);
  if (ok && !id && made && a.ui.editor == "modifiers") a.ui.editorArg = made;
  return ok;
}

// Запись мира модификатора внутри транзакции (встроенный — создаётся по шаблону).
Id recordOf(Tx& tx, const Modifier& m) { return m.id ? m.id : rules::ensureBuiltinMod(tx, m.key); }

// Ключ слияния правок одного модификатора (у встроенного — по ключу: шаблон и созданная запись — одно и то же).
std::string mergeKey(const Modifier& m, std::string_view part) {
  return "mod:" + (m.key.empty() ? std::to_string(m.id) : m.key) + ":" + std::string(part);
}

Id duplicateAct(App& a, const Modifier& src) {
  Modifier copy = src;
  Id nid = 0;
  a.act("Копия модификатора", [&](Tx& tx) {
    nid = rules::createModifier(tx, orName(copy.name, "Модификатор") + " — копия");
    Modifier& d = tx.modifier(nid);
    std::string name = d.name;
    d = copy;
    d.id = nid;
    d.name = name;
    d.key.clear();   // копия встроенного — свой модификатор
  });
  return nid;
}

// Удаление своего модификатора с подтверждением: в тексте — где он используется. next — что выделить после.
void askDelete(App& a, Id mod, Id next) {
  const World& w = a.world();
  const Modifier* m = w.modifier(mod);
  if (!m || isBuiltin(*m)) return;
  ModUse u = usageOf(w, mod);
  std::string name = "«" + orName(m->name, "Модификатор") + "»";
  std::string text = u.total() ? name + " используется: " + usageText(u) + ". Модификатор будет убран из всех этих списков."
                               : name + " нигде не используется.";
  text += " Действие можно отменить Ctrl+Z.";
  a.confirm("Удалить модификатор?", text, "Удалить", true, [mod, next](App& x) {
    if (x.act("Удалить модификатор", [&](Tx& tx) { rules::removeModifier(tx, mod); }) && x.ui.editor == "modifiers") x.ui.editorArg = next;
  });
}

// ---------------------------------------------------------------- список
void listRow(App& a, const Entry& e, Id& sel, EdState& st) {
  const World& w = a.world();
  const Modifier& m = e.m;
  ui::IdScope s{i64(e.id)};
  std::string tip = missingTargets(m) ? std::string("Дипломатия без государств-целей")
                    : isAuto(m)       ? (e.uses ? "Действует сейчас: " + fmtInt(e.uses) : std::string("Ставится сам"))
                    : e.uses          ? "Используется: " + usageText(usageOf(w, m.id))
                                      : std::string("Нигде не используется");
  if (edkit::entityRow(orName(m.name, "Без названия"), effectSummary(w, m), modIcon(m), m.color, e.uses ? std::to_string(e.uses) : std::string(),
                       e.id == sel, missingTargets(m), e.builtin ? "Встроенный · " + tip : tip))
    sel = e.id;
  if (e.builtin) {   // встроенный — замок на плитке значка
    const ui::Theme& th = ui::theme();
    const RectF rr = ui::lastItem().rect;
    const float cx = rr.x + 10 + 30 - 1, cy = rr.cy() + 15 - 1;
    ui::draw::circle(cx, cy, 7, th.surface1);
    ui::draw::icon("lock", RectF{cx - 5, cy - 5, 10, 10}, th.textMuted);
  }
  if (e.id == sel) {
    a.markUi("modifiers.selected");
    if (st.seenSel != sel) ui::scrollToItem();
  }
  if (e.builtin && !m.key.empty()) a.markUi("modifiers.builtin." + m.key);
}

// Строка группы: шеврон, значок вида, название, число. true — щелчок (свернуть/развернуть).
bool groupHeader(App& a, const KindGroup& g, size_t n, bool open, bool forced) {
  const ui::Theme& th = ui::theme();
  ui::IdScope s(g.mark);
  const RectF r = ui::next(0, 30);
  ui::Interaction it = ui::interact(ui::id("##head"), r);
  if (it.hovered && !forced) ui::setCursor(platform::Cursor::Hand);
  const float hv = ui::animate(ui::id("##hv"), it.hovered && !forced ? 1.f : 0.f);
  if (hv > 0.01f) ui::draw::rect(r, th.hover.alpha(hv), 6);
  ui::draw::icon(open ? "chevron-down" : "chevron-right", RectF{r.x + 6, r.cy() - 7, 14, 14}, forced ? th.textMuted.alpha(0.5f) : th.textMuted);
  ui::draw::icon(g.icon, RectF{r.x + 26, r.cy() - 8, 16, 16}, th.textDim);
  const std::string cnt = fmtInt(i64(n));
  const float pw = std::max(22.f, std::ceil(ui::measure(cnt, ui::Font::Caption)) + 12);
  const RectF pr{r.right() - 10 - pw, r.cy() - 9, pw, 18};
  ui::draw::text(g.title, RectF{r.x + 50, r.y, pr.x - r.x - 58, r.h}, ui::Font::Strong, th.text);
  ui::draw::rect(pr, th.surface3, 9);
  ui::draw::text(cnt, pr, ui::Font::Caption, th.textDim, ui::Align::Center);
  a.markUi(std::string("modifiers.group.") + g.mark, r);
  return it.clicked && !forced;
}

using Groups = std::array<std::vector<const Entry*>, kKindGroupCount>;

void drawList(App& a, EdState& st, Id& sel, const Groups& groups, size_t shown, size_t totalOwn, size_t totalAll) {
  bool ro = a.readOnly();
  {
    ui::Row r({ui::fr(1), ui::px(30)}, 30, 6);
    if (st.focusSearch) {
      ui::setKeyboardFocus(ui::id("q"));
      st.focusSearch = false;
    }
    ui::searchField("q", st.query, "Поиск модификаторов");
    a.markUi("modifiers.search");
    if (ui::iconButton("plus", "Новый модификатор", {.variant = ui::Variant::Secondary, .disabled = ro, .shortcut = {Key::Insert, 0}})) {
      if (Id nid = createModifierAct(a)) {
        sel = nid;
        st.query.clear();
        st.filter = 0;
        st.focusName = true;
      }
    }
    a.markUi("modifiers.new");
  }
  ui::segmented("filter", st.filter,
                {{"list", {}, "Все модификаторы"}, {"province", {}, "С локальными эффектами"}, {"crown", {}, "С глобальными эффектами"},
                 {"unlink", {}, "Нигде не используются"}},
                {.size = ui::Size::Small});
  a.markUi("modifiers.filter");
  RectF rest = ui::avail();
  float footH = ui::lineHeight(ui::Font::Small);
  const bool filtering = !st.query.empty() || st.filter != 0;   // поиск и фильтр раскрывают группы
  auto& closed = closedGroups(a);
  {
    const float listH = std::max(60.f, rest.h - footH - 8);
    a.markUi("modifiers.list", RectF{rest.x, rest.y, rest.w, listH});
    ui::Scroll sc("list", listH);
    ui::gap(2);
    if (shown == 0) {
      ui::spacer(16);
      ui::label("Ничего не найдено", {.ink = ui::Ink::Muted, .align = ui::Align::Center});
    }
    for (int gi = 0; gi < kKindGroupCount; gi++) {
      const auto& items = groups[size_t(gi)];
      if (items.empty()) continue;
      const bool open = filtering || !closed[size_t(gi)];
      if (groupHeader(a, kKindGroups[gi], items.size(), open, filtering)) closed[size_t(gi)] = !closed[size_t(gi)];
      if (!open) continue;
      for (const Entry* e : items) listRow(a, *e, sel, st);
      ui::spacer(2);
    }
    if (totalOwn == 0 && !filtering) ui::label("Своих модификаторов пока нет", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }
  st.seenSel = sel;
  std::string cnt = fmtInt(i64(totalAll)) + " " + plural(i64(totalAll), "модификатор", "модификатора", "модификаторов");
  if (shown != totalAll) cnt = "Показано " + fmtInt(i64(shown)) + " из " + fmtInt(i64(totalAll));
  ui::label(cnt, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
}

// ---------------------------------------------------------------- карточка: эффект
void effectRow(App& a, const Modifier& m, Fx f, bool ro) {
  const auto& e = schema::effect(f);
  const ui::Theme& th = ui::theme();
  ui::IdScope s{int(f)};
  bool on = m.has(f);
  double v = on ? m.fx[size_t(f)] : 0;
  RectF row = ui::next(0, 46);
  if (on) ui::draw::rect(row, th.accent.alpha(th.dark ? 0.06f : 0.07f), 8);
  ui::Area ar(row.inset(8, 0), 0);
  ui::Row r({ui::px(22), ui::px(30), ui::fr(1.2f, 150), ui::fr(1, 90), ui::px(136)}, 46, 10);
  bool on2 = on;
  if (ui::checkbox("##on", on2, ro)) {
    editMod(a, m, on2 ? "Включить эффект модификатора" : "Выключить эффект модификатора", [&](Modifier& x) {
      if (on2) {
        x.fxMask |= 1u << int(f);
        double cur = x.fx[size_t(f)];
        x.fx[size_t(f)] = cur == 0 || cur < e.min || cur > e.max ? fxDefault(f) : cur;
      } else {
        x.fxMask &= ~(1u << int(f));
        x.fx[size_t(f)] = 0;
      }
    });
  }
  ui::tooltip(on ? "Выключить эффект" : "Включить эффект");
  a.markUi(std::string("modifiers.fx.") + e.id + ".on");
  {
    RectF ic = ui::next(30, 30);
    Color c = !on ? th.textMuted : v == 0 ? th.accent : w::effectGood(f, v) ? th.success : th.danger;
    ui::draw::rect(ic, c.alpha(on ? 0.16f : 0.08f), 8);
    ui::draw::icon(e.icon, ic.inset(6), c);
  }
  {
    ui::Group g(0, 0);
    ui::label(e.name, {.font = ui::Font::Body, .ink = on ? ui::Ink::Normal : ui::Ink::Dim});
    std::string sub = rangeText(f);
    if (e.perTurn) sub += " · каждый ход";
    ui::label(sub, {.font = ui::Font::Caption, .ink = ui::Ink::Muted});
  }
  bool dis = ro || !on;
  double sv = v;
  ui::Tone tone = sv == 0 ? ui::Tone::Accent : (w::effectGood(f, sv) ? ui::Tone::Success : ui::Tone::Danger);
  const std::string key = mergeKey(m, e.id);
  if (on) {
    if (ui::slider("s", sv, e.min, e.max, {.step = fxStep(f), .digits = fxDigits(f), .showValue = false, .disabled = ro, .tone = tone})) {
      double nv = clamp(sv, e.min, e.max);
      editMod(a, m, "Эффект модификатора", [&](Modifier& x) { x.fx[size_t(f)] = nv; }, key);
    }
  } else {
    // Выключенный эффект: только шкала с отметкой нуля (без ползунка; у пределов без нуля — без отметки).
    RectF tr = ui::next(0, 30).inset(8, 0);
    ui::draw::rect(RectF{tr.x, tr.cy() - 2, tr.w, 4}, th.track, 2);
    if (e.min < 0 && e.max > 0) {
      float zx = tr.x + float((0 - e.min) / (e.max - e.min)) * tr.w;
      ui::draw::rect(RectF{std::round(zx) - 1, tr.cy() - 6, 2, 12}, th.borderStrong, 1);
    }
  }
  a.markUi(std::string("modifiers.fx.") + e.id + ".slider");
  double nv = v;
  if (ui::numberField("v", nv, {.min = e.min, .max = e.max, .step = fxStep(f), .digits = fxDigits(f), .unit = fxUnit(f), .sign = true,
                                .disabled = dis, .tooltip = "Значение в пределах ТЗ: " + rangeText(f)})) {
    nv = clamp(nv, e.min, e.max);
    editMod(a, m, "Эффект модификатора", [&](Modifier& x) { x.fx[size_t(f)] = nv; }, key);
  }
  a.markUi(std::string("modifiers.fx.") + e.id + ".value");
}

// Цели дипломатии: несколько фракций (обязательны при включённом эффекте).
void targetsRow(App& a, const Modifier& m, bool ro) {
  ui::IdScope scope("targets");
  const World& w = a.world();
  std::vector<const Faction*> fs;
  w.factions.each([&](const Faction& f) { fs.push_back(&f); });
  std::sort(fs.begin(), fs.end(), [](const Faction* x, const Faction* y) {
    if (x->kind != y->kind) return x->kind < y->kind;
    return compareRu(x->name, y->name) < 0;
  });
  ui::Indent ind(70);
  {
    ui::HStack hs(22, ui::Align::Left, 6);
    ui::icon("target", missingTargets(m) ? ui::Ink::Warning : ui::Ink::Muted, 14);
    ui::label("Государства-цели", {.font = ui::Font::Small, .ink = ui::Ink::Dim});
    if (missingTargets(m)) ui::tag("Укажите хотя бы одно", ui::Tone::Warning);
  }
  // Выбранные цели — фишки (крестик убирает), ниже — выбор следующей цели с поиском.
  if (!m.targets.empty()) {
    edkit::chipsBegin();
    for (Id t : m.targets) {
      const Faction* f = w.faction(t);
      if (!f) continue;
      ui::IdScope s{i64(t)};
      ui::ChipOpt co;
      co.color = f->color;
      co.removable = !ro;
      co.clickable = true;
      co.tooltip = f->isGuild() ? "Гильдия — открыть" : "Государство — открыть";
      auto act = edkit::chip(orName(f->name, "Без названия"), co);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, t});
      if (act == ui::ChipAction::Remove)
        editMod(a, m, "Цели модификатора дипломатии", [&](Modifier& x) { x.targets.erase(std::remove(x.targets.begin(), x.targets.end(), t), x.targets.end()); });
    }
    edkit::chipsEnd();
  }
  if (!ro) {
    std::vector<const Faction*> left;
    for (const Faction* f : fs)
      if (!has(m.targets, f->id)) left.push_back(f);
    int idx = -1;
    if (ui::combo("addtarget", idx, int(left.size()),
                  [&](int i) {
                    const Faction* f = left[size_t(i)];
                    return ui::Option{f->name, nullptr, f->color, f->isGuild() ? "гильдия" : ""};
                  },
                  {.placeholder = "Добавить государство-цель", .search = 1, .icon = "plus", .disabled = left.empty()}) &&
        idx >= 0 && idx < int(left.size())) {
      Id t = left[size_t(idx)]->id;
      editMod(a, m, "Цели модификатора дипломатии", [&](Modifier& x) { x.targets.push_back(t); });
    }
    a.markUi("modifiers.targets");
  }
}

void effectGroup(App& a, const Modifier& m, FxGroup group, bool ro) {
  int on = 0, total = 0;
  for (int f = 0; f < kFxCount; f++) {
    if (groupOf(f) != group) continue;
    total++;
    on += m.has(Fx(f)) ? 1 : 0;
  }
  std::string badge = std::to_string(on) + " из " + std::to_string(total);
  const char* title = group == FxGroup::Local    ? "Локальные (провинция)"
                      : group == FxGroup::Global ? "Глобальные (государство)"
                      : group == FxGroup::Arch   ? "Археологические группы"
                                                 : "Войско";
  const char* icon = group == FxGroup::Local ? "province" : group == FxGroup::Global ? "crown" : group == FxGroup::Arch ? "pickaxe" : "army";
  ui::Section sec(title, icon, {.badge = badge});
  a.markUi(group == FxGroup::Local    ? "modifiers.local"
           : group == FxGroup::Global ? "modifiers.global"
           : group == FxGroup::Arch   ? "modifiers.arch"
                                      : "modifiers.army");
  if (!sec) return;
  ui::gap(2);
  for (int f = 0; f < kFxCount; f++) {
    if (groupOf(f) != group) continue;
    effectRow(a, m, Fx(f), ro);
    if (Fx(f) == Fx::DiplomacyPerTurn && m.has(Fx::DiplomacyPerTurn)) targetsRow(a, m, ro);
  }
}

// ---------------------------------------------------------------- карточка: генерация за ход
// Эссенции и ресурсы, которые модификатор даёт каждый ход (ТЗ «Доработки №1», п.5): владельцу провинции, государству,
// фракции героя. У встроенного без записи — значения по умолчанию (некромант, лич); первая правка создаёт запись.
void genRows(App& a, const Modifier& m, bool essence, const std::map<Id, double>& cur, bool ro) {
  const World& w = a.world();
  ui::IdScope scope(essence ? "ess" : "res");
  const std::string mark = std::string("modifiers.gen.") + (essence ? "ess" : "res");
  auto setMap = [&](std::string_view label, std::map<Id, double> nm, std::string coalesce = {}) {
    editMod(a, m, label, [&](Modifier& x) { (essence ? x.essGen : x.resGen) = nm; }, std::move(coalesce));
  };
  for (auto [id, amount] : cur) {
    ui::IdScope s{i64(id)};
    const Id key = id;
    ui::Row r({ui::px(16), ui::fr(1, 100), ui::px(132), ui::px(28)}, 30, 6);
    const CatalogItem* c = essence ? w.essence(key) : w.resource(key);
    Color col = essence ? w::essenceColor(w, key) : w::resourceColor(w, key);
    if (col.luminance() < 0.12f) col = col.lighten(0.45f);   // тёмный цвет — светлее на тёмном фоне
    ui::iconColored(essence ? "essence" : w::resourceIcon(w, key), col, 16);
    ui::label(c && !c->name.empty() ? c->name : std::string(essence ? "Эссенция" : "Ресурс"));
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .unit = !essence && key == kGold ? "тыс." : nullptr,
                                      .disabled = ro, .tooltip = "За ход"})) {
      std::map<Id, double> nm = cur;
      nm[key] = std::max(0.0, v);
      setMap("Генерация модификатора", nm, mergeKey(m, mark + "." + std::to_string(key)));
    }
    a.markUi(mark + "." + std::to_string(key));
    if (ui::iconButton("close", essence ? "Убрать эссенцию" : "Убрать ресурс", {.size = ui::Size::Small, .disabled = ro})) {
      std::map<Id, double> nm = cur;
      nm.erase(key);
      setMap("Убрать генерацию модификатора", nm);
    }
    a.markUi(mark + "." + std::to_string(key) + ".remove");
  }
  if (ro) return;
  Id pick = 0;
  bool picked = false;
  if (essence) {
    std::vector<const CatalogItem*> left;
    for (const CatalogItem& e : w.catalogs->essences)
      if (!cur.count(e.id)) left.push_back(&e);
    int idx = -1;
    picked = ui::combo("addess", idx, int(left.size()),
                       [&](int i) {
                         const std::string& n = left[size_t(i)]->name;   // подпись — вид на запись справочника, не на временную строку
                         return ui::Option{n.empty() ? std::string_view("Без названия") : std::string_view(n), "essence", left[size_t(i)]->color};
                       },
                       {.placeholder = "Эссенция за ход", .search = 1, .icon = "plus", .disabled = left.empty()}) &&
             idx >= 0 && idx < int(left.size());
    if (picked) pick = left[size_t(idx)]->id;
  } else {
    std::vector<Id> left;
    for (const CatalogItem& r : w.catalogs->resources)
      if (!cur.count(r.id)) left.push_back(r.id);
    picked = w::resourceByGroup("addres", pick, false, &left) && pick;
  }
  a.markUi(mark + ".add");
  if (picked) {
    std::map<Id, double> nm = cur;
    nm[pick] = 1;
    setMap(essence ? "Эссенция за ход" : "Ресурс за ход", nm);
  }
}

void genSection(App& a, const Modifier& m, bool ro) {
  const World& w = a.world();
  const std::map<Id, double> ess = rules::modEssGen(w, m), res = rules::modResGen(w, m);
  const size_t n = ess.size() + res.size();
  ui::Section sec("Генерация за ход", "repeat", {.badge = n ? std::to_string(n) : std::string()});
  a.markUi("modifiers.gen");
  if (!sec) return;
  ui::caption("Эссенции");
  genRows(a, m, true, ess, ro);
  ui::caption("Ресурсы");
  genRows(a, m, false, res, ro);
}

// ---------------------------------------------------------------- карточка: «Где используется»
ui::ChipAction useChip(std::string_view label, const char* icon, Color dot, std::string_view tip, bool removable) {
  ui::ChipOpt co;
  co.icon = icon;
  co.color = dot;
  co.clickable = true;
  co.removable = removable;
  co.tooltip = tip;
  return edkit::chip(label, co);
}

const EditorDef* editorLike(std::string_view part) {
  for (auto& e : editors())
    if (std::string_view(e.id).find(part) != std::string_view::npos) return &e;
  return nullptr;
}

// Добавить модификатор сущности по правилам (вид, срок по умолчанию, взаимоисключения); встроенный — с записью мира.
void addTo(App& a, const Modifier& m, ModTarget t, Id target, std::string_view label) {
  Id made = 0;
  if (a.act(label, [&](Tx& tx) {
        made = recordOf(tx, m);
        rules::addModifier(tx, t, target, made);
      }) &&
      !m.id && made && a.ui.editor == "modifiers")
    a.ui.editorArg = made;
}

// Выбор сущности для добавления модификатора (ui::combo с поиском). true — выбрана, id — её ID.
bool addCombo(App& a, std::string_view id, const char* mark, const std::vector<std::pair<Id, std::string>>& items, const std::vector<Color>& colors,
              std::string_view placeholder, const char* icon, std::string_view tip, Id& out) {
  int idx = -1;
  bool r = ui::combo(id, idx, int(items.size()),
                     [&](int i) { return ui::Option{items[size_t(i)].second, icon, colors[size_t(i)]}; },
                     {.placeholder = placeholder, .search = 1, .icon = "plus", .disabled = items.empty(), .tooltip = tip}) &&
           idx >= 0 && idx < int(items.size());
  a.markUi(mark);
  if (r) out = items[size_t(idx)].first;
  return r;
}

void usageSection(App& a, const Modifier& m, const std::map<std::string, std::vector<Id>>& autos, bool ro) {
  ui::IdScope scope("usage");
  const World& w = a.world();
  ModUse u = usageOf(w, m.id);
  const Id mid = m.id;
  const bool automatic = isAuto(m);
  const std::vector<Id>* now = nullptr;
  if (automatic)
    if (auto it = autos.find(m.key); it != autos.end()) now = &it->second;
  std::string badge = std::to_string(u.total() + (now ? now->size() : 0));
  ui::Section sec("Где используется", "link", {.badge = badge});
  a.markUi("modifiers.usage");
  if (!sec) return;
  if (u.total() == 0 && !now) ui::label(automatic ? "Сейчас нигде не действует" : "Пока нигде", {.ink = ui::Ink::Muted});
  auto group = [&](const char* title, size_t n) {
    if (!n) return false;
    ui::caption(std::string(title) + " · " + std::to_string(n));
    return true;
  };
  // Подпись фишки с оставшимся сроком: «Эльвенмор · 3» (нет записи срока — бессрочно).
  auto termed = [&](std::string name, const ModTurns& turns) {
    if (auto it = turns.find(mid); it != turns.end() && it->second > 0) name += " · " + std::to_string(it->second);
    return name;
  };
  // Автоматический — где действует сейчас (государства; столица — провинции).
  if (now && group("Действует сейчас", now->size())) {
    ChipFlow cf;
    for (Id id : *now) {
      ui::IdScope s{i64(id) + 0x500000};
      if (m.kind == ModKind::Province) {
        const Province* p = w.province(id);
        if (useChip(orName(p ? p->name : std::string(), "Без названия"), "province", w::factionColor(w, p ? p->owner : 0), "Открыть провинцию",
                    false) == ui::ChipAction::Click)
          edkit::goTo(a, {SelType::Province, id});
      } else if (useChip(w.factionName(id), "crown", w::factionColor(w, id), "Открыть государство", false) == ui::ChipAction::Click) {
        edkit::goTo(a, {SelType::Faction, id});
      }
    }
  }
  if (group("Провинции", u.provinces.size())) {
    if (!hasLocal(m) && hasGlobal(m)) ui::tag("Глобальные эффекты в провинции не действуют", ui::Tone::Warning, "warning");
    ChipFlow cf;
    for (Id pid : u.provinces) {
      const Province* p = w.province(pid);
      ui::IdScope s{i64(pid)};
      auto act = useChip(p ? termed(orName(p->name, "Без названия"), p->modTurns) : std::string("Без названия"), "province",
                         w::factionColor(w, p ? p->owner : 0), "Открыть провинцию", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Province, pid});
      if (act == ui::ChipAction::Remove) a.act("Убрать модификатор провинции", [&](Tx& tx) { rules::dropModifier(tx, ModTarget::Province, pid, mid); });
    }
  }
  auto factionsGroup = [&](const char* title, const std::vector<Id>& ids, bool guilds) {
    if (!group(title, ids.size())) return;
    ChipFlow cf;
    for (Id fid : ids) {
      const Faction* f = w.faction(fid);
      ui::IdScope s{i64(fid)};
      auto act = useChip(f ? termed(orName(f->name, "Без названия"), f->modTurns) : std::string("Без названия"), guilds ? "guild" : "crown",
                         w::factionColor(w, fid), guilds ? "Открыть гильдию" : "Открыть государство", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, fid});
      if (act == ui::ChipAction::Remove)
        a.act(guilds ? "Убрать модификатор гильдии" : "Убрать модификатор государства",
              [&](Tx& tx) { rules::dropModifier(tx, ModTarget::Faction, fid, mid); });
    }
  };
  factionsGroup("Государства", u.states, false);
  factionsGroup("Торговые гильдии", u.guilds, true);
  if (group("Войска и флот", u.armies.size())) {
    ChipFlow cf;
    for (Id aid : u.armies) {
      const Army* ar = w.army(aid);
      if (!ar) continue;
      ui::IdScope s{i64(aid) + 0x600000};
      auto act = useChip(termed(orName(ar->name, ar->isFleet() ? "Флот" : "Войско"), ar->modTurns), ar->isFleet() ? "fleet" : "army",
                         w::factionColor(w, ar->leader()), ar->isFleet() ? "Открыть флот" : "Открыть войско", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Army, aid});
      if (act == ui::ChipAction::Remove) a.act("Убрать модификатор войска", [&](Tx& tx) { rules::dropModifier(tx, ModTarget::Army, aid, mid); });
    }
  }
  if (group("Персонажи", u.heroes.size())) {
    ChipFlow cf;
    for (Id cid : u.heroes) {
      const Character* c = w.character(cid);
      if (!c) continue;
      ui::IdScope s{i64(cid) + 0x700000};
      auto act = useChip(termed(orName(c->name, "Без имени"), c->modTurns), c->hero ? "hero" : "character", w::factionColor(w, c->faction),
                         "Открыть персонажа", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Character, cid});
      if (act == ui::ChipAction::Remove) a.act("Убрать модификатор персонажа", [&](Tx& tx) { rules::dropModifier(tx, ModTarget::Character, cid, mid); });
    }
  }
  if (group("Должности", u.positions.size())) {
    ChipFlow cf;
    for (Id pid : u.positions) {
      const CatalogItem* p = Catalogs::find(w.catalogs->positions, pid);
      if (!p) continue;
      ui::IdScope s{i64(pid) + 0x800000};
      const bool taken = has(p->modifiers, mid), vacant = has(p->vacantModifiers, mid);
      std::string label = orName(p->name, "Без названия") + (taken && vacant ? "" : taken ? " · занята" : " · пустует");
      auto act = useChip(label, "council", Color(0, 0, 0, 0), "Открыть справочник должностей", !ro);
      if (act == ui::ChipAction::Click) later(a, [](App& x) { x.openEditor("catalogs", 6); });
      if (act == ui::ChipAction::Remove)
        a.act("Убрать модификатор должности", [&](Tx& tx) {
          for (CatalogItem& c : tx.catalogs().positions)
            if (c.id == pid) {
              c.modifiers.erase(std::remove(c.modifiers.begin(), c.modifiers.end(), mid), c.modifiers.end());
              c.vacantModifiers.erase(std::remove(c.vacantModifiers.begin(), c.vacantModifiers.end(), mid), c.vacantModifiers.end());
            }
        });
    }
  }
  if (group("Археологические группы", u.groups.size())) {
    ChipFlow cf;
    for (auto [fid, gid] : u.groups) {
      const Faction* f = w.faction(fid);
      const ArchGroup* g = f ? f->archGroup(gid) : nullptr;
      if (!g) continue;
      ui::IdScope s{i64(gid) + 0x900000};
      auto act = useChip(termed(orName(g->name, "Группа"), g->modTurns) + " · " + orName(f->name, "Без названия"), "pickaxe", w::factionColor(w, fid),
                         "Открыть государство", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, fid});
      if (act == ui::ChipAction::Remove)
        a.act("Убрать модификатор археологической группы", [&](Tx& tx) {
          for (ArchGroup& x : tx.faction(fid).archGroups)
            if (x.id == gid) {
              x.modifiers.erase(std::remove(x.modifiers.begin(), x.modifiers.end(), mid), x.modifiers.end());
              x.modTurns.erase(mid);
            }
        });
    }
  }
  if (group("Технологии", u.techs.size())) {
    ChipFlow cf;
    for (Id tid : u.techs) {
      const Tech* t = w.tech(tid);
      if (!t) continue;
      ui::IdScope s{i64(tid)};
      std::string label = orName(t->name, "Без названия") + " · " + w.factionName(t->faction);
      auto act = useChip(label, "tech", w::factionColor(w, t->faction), "Открыть дерево технологий", !ro);
      if (act == ui::ChipAction::Click) {
        Id fac = t->faction;
        if (const EditorDef* ed = editorLike("tech")) {
          std::string id = ed->id;
          later(a, [id, fac](App& x) { x.openEditor(id, fac); });
        } else {
          edkit::goTo(a, {SelType::Faction, fac});
        }
      }
      if (act == ui::ChipAction::Remove)
        a.act("Убрать модификатор технологии", [&](Tx& tx) {
          auto& v = tx.tech(tid).modifiers;
          v.erase(std::remove(v.begin(), v.end(), mid), v.end());
        });
    }
  }
  if (group("Уровни построек", u.levels.size())) {
    ChipFlow cf;
    for (auto [bid, lvl] : u.levels) {
      const Building* b = w.building(bid);
      if (!b) continue;
      ui::IdScope s{i64(bid) * 64 + lvl};
      std::string label = orName(b->name, "Без названия") + " · ур.\xC2\xA0" + std::to_string(lvl);
      auto act = useChip(label, b->icon.empty() || !gfx::hasIcon(b->icon) ? "building" : b->icon.c_str(),
                         b->owner ? w::factionColor(w, b->owner) : Color(0, 0, 0, 0),
                         b->owner ? "Уникальная постройка — открыть дерево построек" : "Общее дерево построек — открыть", !ro);
      if (act == ui::ChipAction::Click) {
        Id owner = b->owner;
        if (const EditorDef* ed = editorLike("build")) {
          std::string id = ed->id;
          later(a, [id, owner](App& x) { x.openEditor(id, owner); });
        } else if (owner) {
          edkit::goTo(a, {SelType::Faction, owner});
        }
      }
      if (act == ui::ChipAction::Remove) {
        int li = lvl - 1;
        Id bb = bid;
        a.act("Убрать модификатор уровня постройки", [&](Tx& tx) {
          Building& x = tx.building(bb);
          if (li < int(x.levels.size())) {
            auto& v = x.levels[size_t(li)].modifiers;
            v.erase(std::remove(v.begin(), v.end(), mid), v.end());
          }
        });
      }
    }
  }
  if (ro || automatic) return;
  // Добавить в списки провинций, государств, войск и персонажей — по виду модификатора (ТЗ «Модификаторы», п.1).
  const bool any = m.kind == ModKind::Any;
  std::vector<std::pair<Id, std::string>> items;
  std::vector<Color> colors;
  ui::spacer(4);
  ui::Row r({ui::fr(1), ui::fr(1)}, 30, 8);
  Id pick = 0;
  if (any || m.kind == ModKind::Province) {
    items.clear();
    colors.clear();
    std::vector<const Province*> ps;
    w.provinces.each([&](const Province& p) {
      if (!p.sea && !has(p.modifiers, mid)) ps.push_back(&p);
    });
    std::sort(ps.begin(), ps.end(), [](const Province* x, const Province* y) { return compareRu(x->name, y->name) < 0; });
    for (const Province* p : ps) {
      items.push_back({p->id, orName(p->name, "Без названия")});
      colors.push_back(w::factionColor(w, p->owner));
    }
    if (addCombo(a, "addprov", "modifiers.addProvince", items, colors, "Добавить провинции", "province", "Добавить модификатор в список провинции", pick))
      addTo(a, m, ModTarget::Province, pick, "Модификатор провинции");
  }
  if (any || m.kind == ModKind::Faction) {
    items.clear();
    colors.clear();
    std::vector<const Faction*> fs;
    w.factions.each([&](const Faction& f) {
      if (!has(f.modifiers, mid)) fs.push_back(&f);
    });
    std::sort(fs.begin(), fs.end(), [](const Faction* x, const Faction* y) {
      if (x->kind != y->kind) return x->kind < y->kind;
      return compareRu(x->name, y->name) < 0;
    });
    for (const Faction* f : fs) {
      items.push_back({f->id, orName(f->name, "Без названия")});
      colors.push_back(f->color);
    }
    if (addCombo(a, "addfac", "modifiers.addFaction", items, colors, "Добавить государству", nullptr, "Добавить модификатор государству или гильдии",
                 pick)) {
      const Faction* f = w.faction(pick);
      addTo(a, m, ModTarget::Faction, pick, f && f->isGuild() ? "Модификатор гильдии" : "Модификатор государства");
    }
  }
  if (any || m.kind == ModKind::Army) {
    items.clear();
    colors.clear();
    w.armies.each([&](const Army& x) {
      if (!has(x.modifiers, mid)) {
        items.push_back({x.id, orName(x.name, x.isFleet() ? "Флот" : "Войско")});
        colors.push_back(w::factionColor(w, x.leader()));
      }
    });
    if (addCombo(a, "addarmy", "modifiers.addArmy", items, colors, "Добавить войску", "army", "Добавить модификатор войску или флоту", pick))
      addTo(a, m, ModTarget::Army, pick, "Модификатор войска");
  }
  if (any || m.kind == ModKind::Hero) {
    items.clear();
    colors.clear();
    std::vector<const Character*> cs;
    w.characters.each([&](const Character& c) {
      if (!has(c.modifiers, mid)) cs.push_back(&c);
    });
    std::sort(cs.begin(), cs.end(), [](const Character* x, const Character* y) { return compareRu(x->name, y->name) < 0; });
    for (const Character* c : cs) {
      items.push_back({c->id, orName(c->name, "Без имени")});
      colors.push_back(w::factionColor(w, c->faction));
    }
    if (addCombo(a, "addhero", "modifiers.addHero", items, colors, "Добавить персонажу", "character", "Добавить модификатор персонажу", pick))
      addTo(a, m, ModTarget::Character, pick, "Модификатор персонажа");
  }
  // Модификатор археологических групп — только группам (ТЗ «Доработки №2», п.1).
  if (m.kind == ModKind::ArchGroup) {
    items.clear();
    colors.clear();
    std::vector<std::pair<Id, Id>> refs;   // государство, группа
    w.factions.each([&](const Faction& f) {
      for (const ArchGroup& g : f.archGroups)
        if (!has(g.modifiers, mid)) {
          refs.push_back({f.id, g.id});
          items.push_back({Id(refs.size()), orName(g.name, "Группа") + " · " + orName(f.name, "Без названия")});
          colors.push_back(f.color);
        }
    });
    if (addCombo(a, "addgroup", "modifiers.addGroup", items, colors, "Добавить группе", "pickaxe", "Добавить модификатор археологической группе", pick) &&
        pick >= 1 && pick <= refs.size()) {
      const auto [fid, gid] = refs[size_t(pick - 1)];
      Id made = 0;
      if (a.act("Модификатор археологической группы", [&](Tx& tx) {
            made = recordOf(tx, m);
            const int turns = std::max(0, tx.w().modifier(made)->duration);
            for (ArchGroup& g : tx.faction(fid).archGroups)
              if (g.id == gid && !has(g.modifiers, made)) {
                g.modifiers.push_back(made);
                if (turns > 0) g.modTurns[made] = turns;
              }
          }) &&
          !m.id && made && a.ui.editor == "modifiers")
        a.ui.editorArg = made;
    }
  }
}

// ---------------------------------------------------------------- карточка модификатора
void drawDetail(App& a, EdState& st, const Modifier& m, Id next, const std::map<std::string, std::vector<Id>>& autos) {
  const World& w = a.world();
  bool ro = a.readOnly();
  const bool builtin = isBuiltin(m), automatic = isAuto(m);
  ui::Scroll sc("detail");
  // Шапка: плитка значка, название, действия.
  {
    ui::Row head({ui::px(60), ui::fr(1), ui::px(30), ui::px(30)}, 60, 12);
    edkit::iconTile(m.icon, m.color, 60, "sparkles");
    {
      ui::Group g(0, 4);
      ui::caption(std::string(builtin ? "Встроенный модификатор · " : "Модификатор · ") + utf8::lower(schema::modKind(m.kind).name));
      std::string name = m.name;
      if (st.focusName) {
        ui::setKeyboardFocus(ui::id("name"));
        st.focusName = false;
      }
      if (ui::textField("name", name, {.placeholder = "Название модификатора", .icon = "edit", .maxLength = 80, .readOnly = ro,
                                       .selectAllOnFocus = true}) &&
          trim(name) != m.name) {
        std::string n = trim(name);
        editMod(a, m, "Переименовать модификатор", [&](Modifier& x) { x.name = n; });
      }
      a.markUi("modifiers.name");
    }
    {
      ui::Group g(30, 0);
      ui::spacer(22);
      if (ui::iconButton("duplicate", "Копия модификатора", {.disabled = ro, .shortcut = {Key::D, ui::ModPrimary}})) {
        if (Id nid = duplicateAct(a, m)) a.ui.editorArg = nid;
      }
      a.markUi("modifiers.duplicate");
    }
    {
      ui::Group g(30, 0);
      ui::spacer(22);
      if (builtin) {
        ui::icon("lock", ui::Ink::Muted, 18, "Встроенный модификатор — удалить нельзя");
      } else {
        if (ui::iconButton("trash", "Удалить модификатор", {.disabled = ro, .shortcut = {Key::Delete, 0}, .tone = ui::Tone::Danger}))
          askDelete(a, m.id, next);
        a.markUi("modifiers.delete");
      }
    }
  }
  if (builtin) {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::tag("встроенный", ui::Tone::Accent, "lock");
    a.markUi("modifiers.builtinTag");
    if (automatic) {
      ui::tag("ставится сам", ui::Tone::Info, "bolt");
      a.markUi("modifiers.autoTag");
    }
    if (!m.id) ui::label("шаблон", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .tooltip = "Запись мира появится при первой правке"});
  }
  ui::spacer(4);
  // Оформление, применение и предпросмотр.
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, ui::kAuto, 12);
    {
      ui::Group g;
      ui::Card c({.icon = "palette", .title = "Оформление"});
      {
        ui::Row pr({ui::px(76), ui::fr(1)}, 30, 8);
        ui::label("Значок", {.ink = ui::Ink::Dim});
        {
          ui::HStack hs(30, ui::Align::Left, 8);
          std::string icon = m.icon;
          if (edkit::iconPicker("icon", icon, false, ro, "Значок модификатора") && icon != m.icon)
            editMod(a, m, "Значок модификатора", [&](Modifier& x) { x.icon = icon; });
          a.markUi("modifiers.icon");
          ui::label(edkit::iconTitle(m.icon), {.ink = ui::Ink::Dim});
        }
        ui::label("Цвет", {.ink = ui::Ink::Dim});
        Color col = m.color;
        {
          ui::Disabled dcol(ro);
          if (ui::colorButton("color", col, {.tooltip = "Цвет модификатора"}) && !ro)
            editMod(a, m, "Цвет модификатора", [&](Modifier& x) { x.color = col; }, mergeKey(m, "color"));
          a.markUi("modifiers.color");
        }
        // Где действует (ТЗ «Модификаторы», п.1): у встроенного — закреплено.
        ui::label("Действует", {.ink = ui::Ink::Dim});
        {
          std::vector<ui::Option> kinds;
          for (int k = 0; k < int(ModKind::Count); k++) kinds.push_back(ui::Option{schema::kModKinds[k].name, schema::kModKinds[k].icon});
          int kind = int(m.kind);
          if (ui::combo("kind", kind, std::span<const ui::Option>(kinds),
                        {.disabled = ro || builtin, .tooltip = builtin ? std::string_view("У встроенного модификатора закреплено") : std::string_view("Где действует")}) &&
              kind != int(m.kind) && kind >= 0 && kind < int(ModKind::Count))
            a.act("Где действует модификатор", [&](Tx& tx) {
              Modifier& x = tx.modifier(m.id);
              x.kind = ModKind(kind);
              // Модификатор археологических групп — только эффекты групп (ТЗ «Доработки №2», п.1): прочие снимаются.
              if (x.kind == ModKind::ArchGroup) {
                x.fxMask &= archMask();
                for (int f = 0; f < kFxCount; f++)
                  if (!x.has(Fx(f))) x.fx[size_t(f)] = 0;
                x.targets.clear();
              }
              // Генерация за ход — у видов «Везде», «Для провинций», «Глобальный», «Для героев».
              if (!genKind(x.kind)) {
                x.essGen.clear();
                x.resGen.clear();
              }
              // Носители, на которых модификатор по новому виду не действует, его теряют.
              rules::fitModifierUses(tx, m.id);
            });
          a.markUi("modifiers.kind");
        }
        // Срок по умолчанию: ходов действия при установке, 0 — бессрочно.
        ui::label("Срок", {.ink = ui::Ink::Dim});
        {
          int dur = std::max(0, m.duration);
          if (ui::numberField("duration", dur, {.min = 0, .max = 1000, .unit = "ход|хода|ходов", .icon = "hourglass", .steppers = true, .disabled = ro,
                                                .tooltip = "Срок при установке; 0 — бессрочно"}))
            editMod(a, m, "Срок модификатора", [&](Modifier& x) { x.duration = std::max(0, dur); }, mergeKey(m, "duration"));
          a.markUi("modifiers.duration");
        }
      }
      std::string desc = m.desc;
      if (ui::textArea("desc", desc, 64, {.placeholder = "Описание", .readOnly = ro}) && desc != m.desc)
        editMod(a, m, "Описание модификатора", [&](Modifier& x) { x.desc = desc; });
      a.markUi("modifiers.desc");
    }
    {
      ui::Group g;
      ui::Card c({.icon = "eye", .title = "Предпросмотр"});
      {
        ui::HStack hs(26, ui::Align::Left, 6);
        ui::ChipOpt co;
        co.icon = modIcon(m);
        co.color = m.color;
        co.tooltip = m.desc;
        ui::chip(orName(m.name, "Модификатор"), co);
        if (m.duration > 0) ui::tag(nTurns(m.duration), ui::Tone::Neutral, "hourglass");
        if (missingTargets(m)) ui::tag("Нет целей дипломатии", ui::Tone::Warning, "warning");
      }
      edkit::effectChips(m);
      a.markUi("modifiers.preview");
      ModUse u = usageOf(w, m.id);
      ui::Row tiles({ui::fr(1), ui::fr(1)}, 60, 8);
      ui::stat(fmtInt(i64(u.provinces.size())), "Провинции", {.icon = "province", .tone = ui::Tone::Info});
      ui::stat(fmtInt(i64(u.states.size() + u.guilds.size())), "Государства и гильдии", {.icon = "crown", .tone = ui::Tone::Accent});
      ui::stat(fmtInt(i64(u.armies.size() + u.heroes.size())), "Войска и персонажи", {.icon = "army", .tone = ui::Tone::Danger});
      ui::stat(fmtInt(i64(u.techs.size() + u.levels.size())), "Технологии и постройки", {.icon = "tech", .tone = ui::Tone::Success});
    }
  }
  // Эффекты (ТЗ 1.g.ii): на широком экране локальные и глобальные — рядом, эффекты войск — ниже. Модификатор
  // археологических групп — только эффекты групп; у глобального и «везде» они тоже есть (действуют на все группы
  // государства). Генерация за ход — у видов «Везде», «Для провинций», «Глобальный», «Для героев».
  if (m.kind == ModKind::ArchGroup) {
    effectGroup(a, m, FxGroup::Arch, ro);
  } else {
    if (ui::avail().w >= 1180) {
      ui::Row r({ui::fr(1), ui::fr(1)}, ui::kAuto, 12);
      {
        ui::Group g;
        effectGroup(a, m, FxGroup::Local, ro);
      }
      {
        ui::Group g;
        effectGroup(a, m, FxGroup::Global, ro);
      }
    } else {
      effectGroup(a, m, FxGroup::Local, ro);
      effectGroup(a, m, FxGroup::Global, ro);
    }
    effectGroup(a, m, FxGroup::Army, ro);
    if (m.kind == ModKind::Any || m.kind == ModKind::Faction || (m.fxMask & archMask())) effectGroup(a, m, FxGroup::Arch, ro);
  }
  if (genKind(m.kind)) genSection(a, m, ro);
  usageSection(a, m, autos, ro);
  ui::spacer(8);
}

// ---------------------------------------------------------------- окно
void drawEditor(App& a, Id arg) {
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  EdState& st = ui::state<EdState>(ui::id("##modstate"));
  auto uses = usageCounts(w);
  auto autos = autoUse(w);
  std::vector<Entry> own, built;
  collect(w, uses, &autos, own, built);
  // Группы по виду (в каждой — свои по названию, затем встроенные в порядке ТЗ) и строки в порядке показа.
  const bool filtering = !st.query.empty() || st.filter != 0;
  auto& closed = closedGroups(a);
  Groups groups;
  size_t matched = 0;
  for (const std::vector<Entry>* list : {&own, &built})
    for (const Entry& e : *list)
      if (passes(e, st)) {
        groups[size_t(groupIndex(e.m.kind))].push_back(&e);
        matched++;
      }
  auto findEntry = [&](Id id) -> const Entry* {
    for (const Entry& e : own)
      if (e.id == id) return &e;
    for (const Entry& e : built)
      if (e.id == id) return &e;
    return nullptr;
  };
  // Выделение — аргумент окна; шаблон, у которого уже есть запись мира, — эта запись; нет такого — первый из видимых.
  Id sel = arg;
  if (int ti = tplIndex(sel); ti >= 0)
    if (Id rid = rules::builtinModId(w, schema::builtinModifiers()[size_t(ti)].key)) sel = rid;
  bool refiltered = st.query != st.seenQuery || st.filter != st.seenFilter;
  st.seenQuery = st.query;
  st.seenFilter = st.filter;
  // Запись встроенного отменили (Ctrl+Z) — выделение остаётся на нём (снова шаблон).
  if (!findEntry(sel) && !st.selKey.empty())
    for (const Entry& e : built)
      if (e.m.key == st.selKey) sel = e.id;
  // Выделение сменилось или модификатор перешёл в другую группу — его группа раскрывается.
  if (const Entry* e = findEntry(sel)) {
    const int gi = groupIndex(e->m.kind);
    if (sel != st.seenSel || gi != st.seenGroup) closed[size_t(gi)] = false;
    st.seenGroup = gi;
  }
  std::vector<const Entry*> shown;   // видимые строки (раскрытые группы) в порядке показа
  for (int gi = 0; gi < kKindGroupCount; gi++)
    if (filtering || !closed[size_t(gi)]) shown.insert(shown.end(), groups[size_t(gi)].begin(), groups[size_t(gi)].end());
  bool visible = std::any_of(shown.begin(), shown.end(), [&](const Entry* e) { return e->id == sel; });
  if (!findEntry(sel) || (refiltered && !visible && !shown.empty())) {
    const Entry* first = !shown.empty() ? shown.front() : !own.empty() ? &own.front() : !built.empty() ? &built.front() : nullptr;
    sel = first ? first->id : 0;
  }
  auto neighbor = [&](Id id) -> Id {
    for (size_t i = 0; i < shown.size(); i++)
      if (shown[i]->id == id) {
        if (i + 1 < shown.size()) return shown[i + 1]->id;
        if (i > 0) return shown[i - 1]->id;
      }
    return 0;
  };
  if (ui::shortcut({Key::F, ui::ModPrimary})) st.focusSearch = true;

  RectF R = ui::avail();
  float listW = std::round(clamp(R.w * 0.27f, 280.f, 380.f));
  RectF L{R.x, R.y, listW, R.h};
  RectF D{R.x + listW + 24, R.y, R.w - listW - 24, R.h};
  ui::draw::line(L.right() + 12, R.y, L.right() + 12, R.bottom(), th.border, 1);
  {
    ui::Area la(L, 0);
    drawList(a, st, sel, groups, matched, own.size(), own.size() + built.size());
  }
  {
    ui::Area da(D, 0);
    const Entry* e = findEntry(sel);
    if (!e) {
      ui::spacer(std::max(0.f, D.h * 0.3f));
      ui::emptyState("sparkles", "Модификатор не выбран.");
    } else {
      Modifier m = e->m;   // копия: правка посреди кадра заменяет мир
      drawDetail(a, st, m, neighbor(sel), autos);
    }
  }
  // Стрелки — по списку (если их не забрало поле или ползунок).
  if (!shown.empty()) {
    int idx = -1;
    for (size_t i = 0; i < shown.size(); i++)
      if (shown[i]->id == sel) idx = int(i);
    if (ui::shortcut({Key::Down, 0})) sel = shown[size_t(std::min(int(shown.size()) - 1, idx + 1))]->id;
    if (ui::shortcut({Key::Up, 0})) sel = shown[size_t(std::max(0, idx - 1))]->id;
  }
  if (const Entry* e = findEntry(sel)) st.selKey = e->builtin ? e->m.key : std::string();
  // Выбор в списке — в аргумент окна (если действие кадра не выбрало другое: копия, удаление, запись встроенного).
  if (a.ui.editor == "modifiers" && a.ui.editorArg == arg) a.ui.editorArg = sel;
}

EditorReg editorReg({"modifiers", "Модификаторы", drawEditor, "sparkles", "reference", 10});
CommandReg commandReg({"editor.modifiers", "Модификаторы", "sparkles", "Ctrl+8", [](App& a) { a.openEditor("modifiers", 0); },
                       [](App& a) { return a.ui.screen == Screen::Editor; }, false, "Справочники"});

}  // namespace

// ================================================================ общие элементы редакторов
namespace edkit {

const char* iconTitle(std::string_view icon) {
  for (auto& c : kIcons)
    if (icon == c.name) return c.title;
  return icon.empty() ? "Без значка" : "Свой значок";
}

// Кнопка-значок с выбором из сетки во всплывающем окне. true — выбран другой значок.
bool iconPicker(std::string_view id, std::string& icon, bool allowNone, bool disabled, std::string_view tip) {
  ui::IdScope scope(id);
  bool none = icon.empty() || !gfx::hasIcon(icon);
  std::string t = std::string(tip) + ": " + iconTitle(none ? std::string_view() : std::string_view(icon));
  if (ui::iconButton(none ? "image" : icon.c_str(), t, {.variant = ui::Variant::Secondary, .disabled = disabled})) ui::openPopup("grid");
  bool changed = false;
  if (ui::beginPopup("grid", {.side = ui::Side::Below, .width = 8 * 34 + 7 * 4 + 16})) {
    ui::caption(tip);
    {
      ui::Row g({ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34)}, 34, 4);
      if (allowNone && ui::iconButton("close", "Без значка", {.toggled = none})) {
        changed = !icon.empty();
        icon.clear();
        ui::closePopup();
      }
      for (auto& c : kIcons) {
        if (!gfx::hasIcon(c.name)) continue;
        if (ui::iconButton(c.name, c.title, {.toggled = icon == c.name})) {
          changed = icon != c.name;
          icon = c.name;
          ui::closePopup();
        }
      }
    }
    ui::endPopup();
  }
  return changed;
}

// Плитка значка цвета сущности (крупный значок в шапке карточки).
void iconTile(const std::string& icon, Color tint, float size, const char* fallback) {
  const ui::Theme& th = ui::theme();
  RectF r = ui::next(size, size);
  float rad = size >= 48 ? 14 : 8;
  ui::draw::rect(r, tint.alpha(th.dark ? 0.2f : 0.16f), rad);
  ui::draw::rectStroke(r, tint.alpha(0.5f), rad, 1);
  bool ok = !icon.empty() && gfx::hasIcon(icon);
  ui::draw::icon(ok ? std::string_view(icon) : std::string_view(fallback), r.inset(size * 0.24f), tint);
}

// Строка списка сущности (высота 44): плитка значка цвета сущности, название, подпись, число справа,
// предупреждение. Основа — ui::listItem (наведение, выделение, фокус, подсказка, lastItem для меню).
bool entityRow(std::string_view title, std::string_view subtitle, const char* icon, Color tint, std::string_view hint, bool selected,
               bool warn, std::string_view tip) {
  const ui::Theme& th = ui::theme();
  RectF r = ui::next(0, 44);
  ui::at(r);
  bool clicked = ui::listItem("##row", {.subtitle = " ", .selected = selected, .tooltip = tip});
  RectF tile{r.x + 10, r.cy() - 15, 30, 30};
  ui::draw::rect(tile, tint.alpha(th.dark ? 0.2f : 0.16f), 8);
  ui::draw::icon(icon && gfx::hasIcon(icon) ? icon : "sparkles", tile.inset(7), tint);
  float x = tile.right() + 12;
  float right = r.right() - 10;
  if (warn) {
    RectF wr{right - 16, r.cy() - 8, 16, 16};
    ui::draw::icon("warning", wr, th.warning);
    right = wr.x - 8;
  }
  if (!hint.empty()) {
    float bw = std::max(22.f, ui::measure(hint, ui::Font::Caption) + 12);
    RectF br{right - bw, r.cy() - 9, bw, 18};
    ui::draw::rect(br, selected ? th.accent.alpha(0.22f) : th.surface3, 9);
    ui::draw::text(hint, br, ui::Font::Caption, selected ? th.accent : th.textDim, ui::Align::Center);
    right = br.x - 8;
  }
  float lh = ui::lineHeight(ui::Font::Body), sh = ui::lineHeight(ui::Font::Small);
  float y0 = std::round(r.cy() - (lh + sh) * 0.5f);
  ui::draw::text(title, RectF{x, y0, right - x, lh}, selected ? ui::Font::Strong : ui::Font::Body, th.text);
  ui::draw::text(subtitle, RectF{x, y0 + lh, right - x, sh}, ui::Font::Small, th.textMuted);
  return clicked;
}

// Перейти к сущности на карте: закрыть окно и выделить (после кадра).
void goTo(App& a, Selection s) {
  later(a, [s](App& x) {
    x.closeEditor();
    x.select(s, true);
  });
}

// ---- поток фишек с переносом: размер фишки — как в ui::chip (текст Small + поля, значок/точка, крестик)
namespace {
struct Flow {
  RectF area;
  float x = 0, y = 0;
  bool any = false;
};
std::vector<Flow>& flows() {
  static std::vector<Flow> f;
  return f;
}
constexpr float kChipH = 26, kChipGap = 6;
}  // namespace

void chipsBegin() {
  Flow f;
  f.area = ui::avail();
  f.x = f.area.x;
  f.y = f.area.y;
  flows().push_back(f);
}

ui::ChipAction chip(std::string_view label, const ui::ChipOpt& o) {
  if (flows().empty()) return ui::chip(label, o);
  Flow& f = flows().back();
  bool lead = o.icon || o.color.a > 0;
  float w = std::ceil(ui::measure(ui::displayText(label), ui::Font::Small)) + 20 + (lead ? 16 : 0) + (o.removable ? 18 : 0);
  w = std::min(w, f.area.w);
  if (f.any && f.x + w > f.area.right() + 0.5f) {
    f.x = f.area.x;
    f.y += kChipH + kChipGap;
  }
  ui::at(RectF{f.x, f.y, w, kChipH});
  f.x += w + kChipGap;
  f.any = true;
  return ui::chip(label, o);
}

void chipsEnd() {
  if (flows().empty()) return;
  Flow f = flows().back();
  flows().pop_back();
  if (f.any) ui::next(0, f.y + kChipH - f.area.y);   // занять место в потоке
}

// Фишки эффектов модификатора (как w::effectChips, но с переносом по ширине).
void effectChips(const Modifier& m) {
  ui::IdScope s(i64(m.id) + 0x41000000LL);
  bool any = false;
  chipsBegin();
  for (int f = 0; f < kFxCount; f++) {
    if (!m.has(Fx(f))) continue;
    double v = m.fx[size_t(f)];
    if (v == 0) continue;
    any = true;
    ui::IdScope s2{f};
    ui::ChipOpt co;
    co.icon = schema::effect(Fx(f)).icon;
    co.tone = w::effectGood(Fx(f), v) ? ui::Tone::Success : ui::Tone::Danger;
    edkit::chip(w::effectText(Fx(f), v), co);
  }
  // Генерация за ход (эссенции и ресурсы).
  const std::vector<std::string> gen = genTexts(app().world(), m);
  for (size_t i = 0; i < gen.size(); i++) {
    any = true;
    ui::IdScope s2{i64(i) + 0x100};
    ui::ChipOpt co;
    co.icon = "repeat";
    co.tone = ui::Tone::Success;
    co.tooltip = "Каждый ход";
    edkit::chip(gen[i] + " за ход", co);
  }
  chipsEnd();
  if (!any) ui::label("Без эффектов", {.ink = ui::Ink::Muted});
}

}  // namespace edkit
}  // namespace rg::app
