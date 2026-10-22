// Regnum — классы и подклассы героев, деревья талантов подклассов, уровень героя и изучение рангов талантов,
// «Возвысить до Лича».
//
// Дерево талантов подкласса — как в World of Warcraft: ячейки (ярус, столбец), в ячейке один талант с 1…5 рангами,
// стрелка-условие из более высокого яруса (условие изучается полностью). Ярус r открыт, когда в ярусах выше вложено не
// меньше r × tierPoints очков. Изучение — у каждого героя своё (Character::talents — ранги в порядке изучения). Правка
// дерева не оставляет у героев незаконных рангов: после неё список каждого героя подкласса проходится по порядку
// изучения (lawfulTalents), и ранги, которые уже нельзя было бы изучить, снимаются (как при чтении файла).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

std::string clsName(const HeroClass& c) { return q(nameOr(c.name)); }
std::string subName(const SubClass& s) { return q(nameOr(s.name)); }
std::string talName(const Talent& t) { return q(nameOr(t.name)); }

const HeroClass& needClass(const World& w, Id cls) {
  const HeroClass* c = w.heroClass(cls);
  if (!c) fail(cls ? "Класс героя не найден" : "Не выбран класс героя");
  return *c;
}

HeroClass& classRef(Tx& tx, Id cls) {
  needClass(tx.w(), cls);
  if (HeroClass* c = findById(tx.catalogs().classes, cls)) return *c;
  fail("Класс героя не найден");
}

const SubClass& needSub(const World& w, Id sub) {
  const SubClass* s = w.catalogs->subclass(sub);
  if (!s) fail(sub ? "Подкласс героя не найден" : "Не выбран подкласс героя");
  return *s;
}

SubClass& subRef(Tx& tx, Id sub) {
  needSub(tx.w(), sub);
  for (HeroClass& c : tx.catalogs().classes)
    if (SubClass* s = findById(c.subclasses, sub)) return *s;
  fail("Подкласс героя не найден");
}

// Подкласс и талант по ID таланта.
struct Found {
  const SubClass* sub = nullptr;
  const Talent* t = nullptr;
};
Found needTalent(const World& w, Id talent) {
  const SubClass* s = w.catalogs->subclassOfTalent(talent);
  if (!s) fail(talent ? "Талант не найден" : "Не выбран талант");
  return Found{s, s->talent(talent)};
}

Talent& talentRef(Tx& tx, Id sub, Id talent) {
  if (Talent* t = findById(subRef(tx, sub).talents, talent)) return *t;
  fail("Талант не найден");
}

const Talent* talentAt(const SubClass& s, int row, int col, Id except = 0) {
  for (const Talent& t : s.talents)
    if (t.row == row && t.col == col && t.id != except) return &t;
  return nullptr;
}

void needCell(int row, int col) {
  if (row < 0 || row >= kTalentRows) fail("Ярус дерева — от 1 до " + std::to_string(kTalentRows));
  if (col < 0 || col >= kTalentCols) fail("Столбец дерева — от 1 до " + std::to_string(kTalentCols));
}

// Привести ранги героев подкласса к законным; возвращает число героев, у которых ранги сняты.
int fitHeroes(Tx& tx, Id sub) {
  const SubClass* s = tx.w().catalogs->subclass(sub);
  if (!s) return 0;
  const SubClass sc = *s;
  int n = 0;
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& x) { return x.subclass == sub && !x.talents.empty(); })) {
    const Character& ch = *tx.w().character(cid);
    std::vector<Id> keep = lawfulTalents(sc, ch.talents, ch.level);
    if (keep.size() == ch.talents.size()) continue;
    tx.character(cid).talents = std::move(keep);
    n++;
  }
  return n;
}

// Условия талантов дерева выше зависимых (после переноса).
void checkLinks(const SubClass& s) {
  for (const Talent& t : s.talents) {
    if (!t.prereq) continue;
    const Talent* p = s.talent(t.prereq);
    if (p && p->row >= t.row)
      fail("Условие " + talName(*p) + " должно стоять выше таланта " + talName(t) + " — сначала снимите стрелку");
  }
}

std::string uniqueTalentName(const SubClass& s, const std::string& name, Id except) {
  std::string n = trim(name);
  std::vector<std::string> taken;
  for (const Talent& t : s.talents)
    if (t.id != except) taken.push_back(t.name);
  if (n.empty()) return uniqueName(taken, "Новый талант");
  for (const std::string& x : taken)
    if (utf8::searchKey(x) == utf8::searchKey(n)) fail("Талант «" + x + "» уже есть в дереве подкласса");
  return n;
}

std::string uniqueSubName(const HeroClass& c, const std::string& name, Id except) {
  std::string n = trim(name);
  std::vector<std::string> taken;
  for (const SubClass& s : c.subclasses)
    if (s.id != except) taken.push_back(s.name);
  if (n.empty()) return uniqueName(taken, "Новый подкласс");
  for (const std::string& x : taken)
    if (utf8::searchKey(x) == utf8::searchKey(n)) fail("Подкласс «" + x + "» уже есть у класса " + clsName(c));
  return n;
}

const Character& needHero(const World& w, Id character) { return needCharacter(w, character); }

// Снять последнее вхождение таланта (один ранг).
void eraseLast(std::vector<Id>& v, Id id) {
  for (size_t i = v.size(); i-- > 0;)
    if (v[i] == id) {
      v.erase(v.begin() + long(i));
      return;
    }
}

}  // namespace

// ================================================================ классы
Id addHeroClass(Tx& tx, const std::string& name) {
  std::vector<std::string> taken;
  for (const HeroClass& c : tx.w().catalogs->classes) taken.push_back(c.name);
  std::string n = trim(name);
  if (n.empty()) {
    n = uniqueName(taken, "Новый класс");
  } else {
    for (const std::string& t : taken)
      if (utf8::searchKey(t) == utf8::searchKey(n)) fail("Класс «" + t + "» уже есть");
  }
  HeroClass c;
  c.id = tx.nextId(Seq::HeroClass);
  c.name = n;
  c.icon = "hero-class";
  c.color = Color::palette(int(tx.w().catalogs->classes.size()) * 5 + 2);
  tx.catalogs().classes.push_back(std::move(c));
  return tx.w().catalogs->classes.back().id;
}

void setHeroClass(Tx& tx, const HeroClass& c0) {
  needClass(tx.w(), c0.id);
  const std::string n = trim(c0.name);
  if (n.empty()) fail("Название класса не может быть пустым");
  for (const HeroClass& o : tx.w().catalogs->classes)
    if (o.id != c0.id && utf8::searchKey(o.name) == utf8::searchKey(n)) fail("Класс «" + o.name + "» уже есть");
  HeroClass& c = classRef(tx, c0.id);
  c.name = n;
  c.desc = c0.desc;
  c.icon = c0.icon;
  c.color = c0.color;
}

void removeHeroClass(Tx& tx, Id cls) {
  needClass(tx.w(), cls);
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& x) { return x.heroClass == cls; })) {
    Character& ch = tx.character(cid);
    ch.heroClass = 0;
    ch.subclass = 0;
    ch.talents.clear();
  }
  auto& list = tx.catalogs().classes;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const HeroClass& c) { return c.id == cls; }), list.end());
}

// ================================================================ подклассы
Id addSubClass(Tx& tx, Id cls, const std::string& name) {
  const HeroClass& c = needClass(tx.w(), cls);
  SubClass s;
  s.id = tx.nextId(Seq::SubClass);
  s.name = uniqueSubName(c, name, 0);
  s.icon = c.icon;
  s.tierPoints = schema::kDefaultTierPoints;
  const Id id = s.id;
  classRef(tx, cls).subclasses.push_back(std::move(s));
  return id;
}

int setSubClass(Tx& tx, const SubClass& s0) {
  const SubClass& cur = needSub(tx.w(), s0.id);
  const HeroClass& c = *tx.w().catalogs->classOfSubclass(s0.id);
  if (trim(s0.name).empty()) fail("Название подкласса не может быть пустым");
  const std::string n = uniqueSubName(c, s0.name, s0.id);
  if (s0.tierPoints < 1 || s0.tierPoints > kMaxTierPoints) fail("Очков на ярус — от 1 до " + std::to_string(kMaxTierPoints));
  const bool tiers = cur.tierPoints != s0.tierPoints;
  SubClass& s = subRef(tx, s0.id);
  s.name = n;
  s.desc = s0.desc;
  s.icon = s0.icon;
  s.tierPoints = s0.tierPoints;
  return tiers ? fitHeroes(tx, s0.id) : 0;
}

void removeSubClass(Tx& tx, Id sub) {
  needSub(tx.w(), sub);
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& x) { return x.subclass == sub; })) {
    Character& ch = tx.character(cid);
    ch.subclass = 0;
    ch.talents.clear();
  }
  for (HeroClass& c : tx.catalogs().classes)
    c.subclasses.erase(std::remove_if(c.subclasses.begin(), c.subclasses.end(), [&](const SubClass& s) { return s.id == sub; }), c.subclasses.end());
}

const HeroClass* subclassClass(const World& w, Id sub) { return w.catalogs->classOfSubclass(sub); }

// ================================================================ таланты дерева
Id addTalent(Tx& tx, Id sub, int row, int col, const std::string& name) {
  const SubClass& s = needSub(tx.w(), sub);
  needCell(row, col);
  if (const Talent* x = talentAt(s, row, col)) fail("Ячейка занята талантом " + talName(*x));
  Talent t;
  t.id = tx.nextId(Seq::Talent);
  t.name = uniqueTalentName(s, name, 0);
  t.icon = "talent";
  t.row = row;
  t.col = col;
  const Id id = t.id;
  subRef(tx, sub).talents.push_back(std::move(t));
  return id;
}

int setTalent(Tx& tx, const Talent& t0) {
  const Found f = needTalent(tx.w(), t0.id);
  const Id sub = f.sub->id;
  if (trim(t0.name).empty()) fail("Название таланта не может быть пустым");
  const std::string n = uniqueTalentName(*f.sub, t0.name, t0.id);
  if (t0.cost < schema::kMinTalentCost || t0.cost > schema::kMaxTalentCost)
    fail("Стоимость ранга — от " + std::to_string(schema::kMinTalentCost) + " до " + std::to_string(schema::kMaxTalentCost) + " очков");
  if (t0.ranks < 1 || t0.ranks > schema::kMaxTalentRanks) fail("Рангов таланта — от 1 до " + std::to_string(schema::kMaxTalentRanks));
  std::vector<Id> mods;
  for (Id m : t0.modifiers) {
    const Modifier* x = tx.w().modifier(m);
    if (!x) fail("Модификатор не найден");
    if (contains(mods, m)) continue;
    if (x->kind != ModKind::Any && x->kind != ModKind::Hero)
      fail("Модификатор «" + x->name + "» не для героев: таланту подходят модификаторы видов «Везде» и «Для героев»");
    if (x->key == schema::mod::Dead || x->key == schema::mod::Captive || x->key == schema::mod::Arrested || schema::isAutoKey(x->key))
      fail("Модификатор «" + x->name + "» — состояние героя или ставится сам: талант его не даёт");
    mods.push_back(m);
  }
  const bool refit = f.t->cost != t0.cost || f.t->ranks != t0.ranks;
  Talent& t = talentRef(tx, sub, t0.id);
  t.name = n;
  t.desc = t0.desc;
  t.icon = t0.icon;
  t.cost = t0.cost;
  t.ranks = t0.ranks;
  t.modifiers = std::move(mods);
  return refit ? fitHeroes(tx, sub) : 0;
}

int moveTalent(Tx& tx, Id talent, int row, int col) {
  const Found f = needTalent(tx.w(), talent);
  needCell(row, col);
  if (f.t->row == row && f.t->col == col) return 0;
  const Id sub = f.sub->id;
  SubClass next = *f.sub;
  const int fromRow = f.t->row, fromCol = f.t->col;
  for (Talent& t : next.talents) {
    if (t.id == talent) {
      t.row = row;
      t.col = col;
    } else if (t.row == row && t.col == col) {
      t.row = fromRow;   // занятая ячейка — таланты меняются местами
      t.col = fromCol;
    }
  }
  checkLinks(next);
  subRef(tx, sub).talents = next.talents;
  return fitHeroes(tx, sub);
}

int setTalentPrereq(Tx& tx, Id talent, Id prereq) {
  const Found f = needTalent(tx.w(), talent);
  const Id sub = f.sub->id;
  if (f.t->prereq == prereq) return 0;
  if (prereq) {
    if (prereq == talent) fail("Талант не может требовать сам себя");
    const Talent* p = f.sub->talent(prereq);
    if (!p) fail("Условие — талант того же подкласса");
    if (p->row >= f.t->row) fail("Условие " + talName(*p) + " должно стоять в ярусе выше таланта " + talName(*f.t));
  }
  talentRef(tx, sub, talent).prereq = prereq;
  return fitHeroes(tx, sub);
}

int removeTalent(Tx& tx, Id talent) {
  const Found f = needTalent(tx.w(), talent);
  const Id sub = f.sub->id;
  // Герои, у которых ранги изменятся: изучившие удаляемый и те, у кого он держал порог яруса или условие.
  std::vector<Id> lost;
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& x) { return contains(x.talents, talent); })) {
    eraseValue(tx.character(cid).talents, talent);
    lost.push_back(cid);
  }
  SubClass& s = subRef(tx, sub);
  for (Talent& t : s.talents)
    if (t.prereq == talent) t.prereq = 0;
  s.talents.erase(std::remove_if(s.talents.begin(), s.talents.end(), [&](const Talent& t) { return t.id == talent; }), s.talents.end());
  const SubClass sc = *tx.w().catalogs->subclass(sub);
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& x) { return x.subclass == sub && !x.talents.empty(); })) {
    const Character& ch = *tx.w().character(cid);
    std::vector<Id> keep = lawfulTalents(sc, ch.talents, ch.level);
    if (keep.size() == ch.talents.size()) continue;
    tx.character(cid).talents = std::move(keep);
    if (!contains(lost, cid)) lost.push_back(cid);
  }
  return int(lost.size());
}

const SubClass* talentSubClass(const World& w, Id talent) { return w.catalogs->subclassOfTalent(talent); }

std::vector<Id> talentDependents(const World& w, Id talent) {
  std::vector<Id> out;
  if (const SubClass* s = talentSubClass(w, talent))
    for (const Talent& t : s->talents)
      if (t.prereq == talent) out.push_back(t.id);
  return out;
}

int talentTiers(const SubClass& s) {
  int n = 0;
  for (const Talent& t : s.talents) n = std::max(n, t.row + 1);
  return n;
}

// ================================================================ герой
const SubClass* heroSubClass(const World& w, Id character) {
  const Character* c = w.character(character);
  const HeroClass* hc = c ? w.heroClass(c->heroClass) : nullptr;
  return hc ? hc->subclass(c->subclass) : nullptr;
}

int talentPoints(const World& w, Id character) {
  const Character* c = w.character(character);
  return c ? std::clamp(c->level, 1, schema::kMaxHeroLevel) : 0;
}

int talentPointsSpent(const World& w, Id character) {
  const SubClass* s = heroSubClass(w, character);
  return s ? talentSpent(*s, w.character(character)->talents) : 0;
}

int heroSpentAbove(const World& w, Id character, int row) {
  const SubClass* s = heroSubClass(w, character);
  return s ? talentSpentAbove(*s, w.character(character)->talents, row) : 0;
}

int heroTalentRank(const World& w, Id character, Id talent) {
  const Character* c = w.character(character);
  return c ? talentRank(c->talents, talent) : 0;
}

bool canLearnTalent(const World& w, Id character, Id talent, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Character* c = w.character(character);
  if (!c) return no("Персонаж не найден");
  const HeroClass* hc = w.heroClass(c->heroClass);
  if (!hc) return no("У героя нет класса");
  const SubClass* sc = hc->subclass(c->subclass);
  if (!sc) return no("У героя не выбран подкласс класса " + clsName(*hc));
  const Talent* t = sc->talent(talent);
  if (!t) return no("Талант не из дерева подкласса " + subName(*sc));
  const int rank = talentRank(c->talents, talent);
  if (rank >= std::max(1, t->ranks)) return no(talName(*t) + " изучен полностью");
  if (t->prereq) {
    const Talent* p = sc->talent(t->prereq);
    if (!p || talentRank(c->talents, t->prereq) < std::max(1, p->ranks))
      return no("Сначала изучите полностью " + (p ? talName(*p) : std::string("предшествующий талант")));
  }
  const int need = tierThreshold(*sc, t->row), above = talentSpentAbove(*sc, c->talents, t->row);
  if (above < need)
    return no("Ярус " + std::to_string(t->row + 1) + " откроется, когда в ярусах выше будет вложено " + std::to_string(need) + " " +
              plural(need, "очко", "очка", "очков") + " (сейчас " + std::to_string(above) + ")");
  const int free = talentPoints(w, character) - talentSpent(*sc, c->talents);
  if (t->cost > free)
    return no("Не хватает очков талантов: нужно " + std::to_string(t->cost) + ", свободно " + std::to_string(std::max(0, free)));
  if (why) why->clear();
  return true;
}

void learnTalent(Tx& tx, Id character, Id talent) {
  needHero(tx.w(), character);
  std::string why;
  if (!canLearnTalent(tx.w(), character, talent, &why)) fail(why);
  tx.character(character).talents.push_back(talent);
}

bool canUnlearnTalent(const World& w, Id character, Id talent, std::string* why) {
  auto no = [&](std::string s) {
    if (why) *why = std::move(s);
    return false;
  };
  const Character* c = w.character(character);
  if (!c) return no("Персонаж не найден");
  const SubClass* sc = heroSubClass(w, character);
  const Talent* t = sc ? sc->talent(talent) : nullptr;
  if (!t || !contains(c->talents, talent)) return no("Талант не изучен");
  std::vector<Id> rest = c->talents;
  eraseLast(rest, talent);
  if (lawfulTalents(*sc, rest, c->level).size() == rest.size()) {
    if (why) why->clear();
    return true;
  }
  // Что держит ранг: условие или порог яруса.
  const bool last = !contains(rest, talent);
  for (Id id : rest) {
    const Talent* x = sc->talent(id);
    if (!x || id == talent) continue;
    if (x->prereq == talent) return no("От " + talName(*t) + " зависит изученный " + talName(*x) + " — сначала отмените его");
    if (talentSpentAbove(*sc, rest, x->row) < tierThreshold(*sc, x->row))
      return no(std::string(last ? "Без " : "Без этого ранга ") + talName(*t) + " закроется ярус " + std::to_string(x->row + 1) + " с изученным " +
                talName(*x) + " — сначала отмените его");
  }
  return no("Без этого ранга " + talName(*t) + " другие изученные таланты станут незаконными");
}

void unlearnTalent(Tx& tx, Id character, Id talent) {
  needHero(tx.w(), character);
  std::string why;
  if (!canUnlearnTalent(tx.w(), character, talent, &why)) fail(why);
  eraseLast(tx.character(character).talents, talent);
}

void resetTalents(Tx& tx, Id character) {
  needHero(tx.w(), character);
  if (!tx.w().character(character)->talents.empty()) tx.character(character).talents.clear();
}

void setHeroLevel(Tx& tx, Id character, int level, bool reset) {
  needHero(tx.w(), character);
  if (level < 1 || level > schema::kMaxHeroLevel) fail("Уровень героя — от 1 до " + std::to_string(schema::kMaxHeroLevel));
  const int spent = talentPointsSpent(tx.w(), character);
  if (level < spent) {
    if (!reset)
      fail("Вложено " + std::to_string(spent) + " " + plural(spent, "очко", "очка", "очков") + " талантов — уровень не может быть ниже " +
           std::to_string(spent) + ". Сбросьте таланты");
    tx.character(character).talents.clear();
  }
  if (tx.w().character(character)->level != level) tx.character(character).level = level;
}

void setCharacterClass(Tx& tx, Id character, Id cls) {
  const Character& c = needHero(tx.w(), character);
  const HeroClass* hc = cls ? &needClass(tx.w(), cls) : nullptr;
  if (c.heroClass == cls) return;
  const Id only = hc && hc->subclasses.size() == 1 ? hc->subclasses[0].id : 0;
  Character& m = tx.character(character);
  m.heroClass = cls;
  m.subclass = only;
  m.talents.clear();   // другое дерево — изучение заново
}

void setCharacterSubclass(Tx& tx, Id character, Id sub) {
  const Character& c = needHero(tx.w(), character);
  if (sub) {
    const HeroClass* hc = tx.w().heroClass(c.heroClass);
    if (!hc) fail("Сначала выберите класс героя");
    if (!hc->subclass(sub)) fail("Подкласс не из класса " + clsName(*hc));
  }
  if (c.subclass == sub) return;
  Character& m = tx.character(character);
  m.subclass = sub;
  m.talents.clear();
}

// ================================================================ «Возвысить до Лича»
namespace {

constexpr const char* kPhylactery = " (Филактерия «";

}  // namespace

bool lichOffered(const World& w, Id character) {
  return characterHas(w, character, schema::mod::Necromancer) && !characterHas(w, character, schema::mod::Lich);
}

Id deathEssence(const World& w) {
  for (const schema::BuiltinGen& g : schema::builtinGens()) {
    if (g.key != std::string_view(schema::mod::Lich) || !g.essence) continue;
    const std::string k = utf8::searchKey(g.name);
    for (const CatalogItem& e : w.catalogs->essences)
      if (utf8::searchKey(e.name) == k) return e.id;
  }
  return 0;
}

std::string phylacteryName(const std::string& relic, const std::string& hero) {
  return relic + kPhylactery + nameOr(hero, kNameless) + "»)";
}

bool isPhylactery(const Relic& r) { return r.name.find(kPhylactery) != std::string::npos; }

std::vector<Id> phylacteryRelics(const World& w, Id character) {
  std::vector<Id> out;
  const Character* c = w.character(character);
  if (!c) return out;
  for (const RelicStack& st : c->inventory)
    if (const Relic* r = w.relic(st.relic); r && int(r->rarity) >= int(Rarity::Epic) && !isPhylactery(*r)) out.push_back(st.relic);
  sortByRarity(w, out);
  return out;
}

std::vector<std::string> lichProblems(const World& w, Id character, Id relic) {
  std::vector<std::string> out;
  const Character* c = w.character(character);
  if (!c) return {"Персонаж не найден"};
  if (characterHas(w, character, schema::mod::Lich)) return {q(w.characterName(character)) + " уже лич"};
  if (!characterHas(w, character, schema::mod::Necromancer)) out.push_back("Возвысить до Лича можно только некроманта");
  if (characterHas(w, character, schema::mod::Dead)) out.push_back(q(w.characterName(character)) + " мёртв");
  else if (characterHas(w, character, schema::mod::Captive)) out.push_back(q(w.characterName(character)) + " в плену");
  else if (characterHas(w, character, schema::mod::Arrested)) out.push_back(q(w.characterName(character)) + " под арестом");
  const Faction* f = w.faction(c->faction);
  if (!f || !f->isState()) {
    out.push_back("Герой не служит государству — трупы и эссенцию смерти платит его государство");
  } else {
    const Id corpses = resourceId(w, schema::kResCorpses);
    const double have = corpses ? f->stock(corpses) : 0;
    if (have < schema::kLichCorpses)
      out.push_back("Не хватает трупов: нужно " + amount(schema::kLichCorpses) + ", есть " + amount(std::max(0.0, have)));
    const Id death = deathEssence(w);
    if (!death) {
      out.push_back("В справочнике эссенций нет «Эссенции смерти»");
    } else if (f->essence(death) < schema::kLichDeathEssence) {
      out.push_back("Не хватает эссенции смерти: нужно " + amount(schema::kLichDeathEssence) + ", есть " + amount(std::max(0.0, f->essence(death))));
    }
  }
  if (!relic) {
    out.push_back("Выберите реликвию для филактерии");
  } else if (const Relic* r = w.relic(relic); !r) {
    out.push_back("Реликвия не найдена");
  } else {
    if (!relicCount(c->inventory, relic)) out.push_back("Филактерию отдают только из инвентаря героя");
    if (int(r->rarity) < int(Rarity::Epic)) out.push_back("Филактерия — реликвия не ниже эпической");
    if (isPhylactery(*r)) out.push_back(q(r->name) + " уже филактерия");
  }
  return out;
}

void ascendLich(Tx& tx, Id character, Id relic) {
  needHero(tx.w(), character);
  const std::vector<std::string> problems = lichProblems(tx.w(), character, relic);
  if (!problems.empty()) fail(problems.front());
  const Character c = *tx.w().character(character);
  const std::string hero = tx.w().characterName(character);
  const Relic r = *tx.w().relic(relic);
  const std::string newName = phylacteryName(r.name, hero);
  for (const Relic& x : tx.w().catalogs->relics)
    if (x.id != relic && utf8::searchKey(x.name) == utf8::searchKey(newName)) fail("Реликвия «" + x.name + "» уже есть — каждая реликвия уникальна");
  // Цена — с государства героя.
  {
    Faction& f = tx.faction(c.faction);
    addStock(f, ensureResource(tx, schema::kResCorpses), -schema::kLichCorpses);
  }
  {
    const Id death = deathEssence(tx.w());
    Faction& f = tx.faction(c.faction);
    f.ess[death] -= schema::kLichDeathEssence;
    if (f.ess[death] == 0) f.ess.erase(death);
  }
  // «Некромант» сменяется «Личем» (собственный модификатор героя; «Некромант» от таланта остаётся у таланта).
  for (Id m : c.modifiers)
    if (const Modifier* x = tx.w().modifier(m); x && x->key == schema::mod::Necromancer) dropModifier(tx, ModTarget::Character, character, m);
  addModifier(tx, ModTarget::Character, character, ensureBuiltinMod(tx, schema::mod::Lich), 0);
  // Уникальная реликвия переименовывается; от стопки копируемой одна штука становится отдельной реликвией-филактерией.
  if (r.unique()) {
    if (Relic* x = findById(tx.catalogs().relics, relic)) x->name = newName;
  } else {
    Relic ph = r;
    ph.id = tx.nextId(Seq::Relic);
    ph.name = newName;
    tx.catalogs().relics.push_back(ph);
    const RelicPlace at{RelicPlace::Hero, character, 0, c.faction};
    moveRelic(tx, relic, at, RelicPlace{}, 1);
    moveRelic(tx, ph.id, RelicPlace{}, at, 1);
  }
  addLog(tx, LogKind::Note,
         q(hero) + " возвысился до Лича: " + amount(schema::kLichCorpses) + " трупов, " + amount(schema::kLichDeathEssence) +
             " эссенции смерти, филактерия — " + q(r.name),
         LogRefs{0, 0, {c.faction}});
}

}  // namespace rg::rules
