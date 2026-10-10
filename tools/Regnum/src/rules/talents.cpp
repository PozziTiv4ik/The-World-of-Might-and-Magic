// Regnum — классы героев и деревья талантов (ТЗ «Доработки №4», п.6, 8), уровень героя и изучение талантов,
// «Возвысить до Лича» (ТЗ «Доработки №1», п.11).
//
// Дерево талантов класса — как в World of Warcraft: ячейки (ярус, столбец), в ячейке один талант, стрелка-условие из
// более высокого яруса. Ярус r открыт, когда в ярусах выше вложено не меньше r × tierPoints очков. Изучение — у
// каждого героя своё (Character::talents в порядке изучения). Правка дерева не оставляет у героев незаконных талантов:
// после неё список каждого героя класса проходится по порядку изучения, и таланты, которые уже нельзя было бы изучить,
// снимаются (как при чтении файла — лишние снимаются с конца).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

std::string clsName(const HeroClass& c) { return q(c.name.empty() ? std::string("Без названия") : c.name); }
std::string talName(const Talent& t) { return q(t.name.empty() ? std::string("Без названия") : t.name); }

const HeroClass& needClass(const World& w, Id cls) {
  const HeroClass* c = w.heroClass(cls);
  if (!c) fail(cls ? "Класс героя не найден" : "Не выбран класс героя");
  return *c;
}

HeroClass& classRef(Tx& tx, Id cls) {
  needClass(tx.w(), cls);
  for (HeroClass& c : tx.catalogs().classes)
    if (c.id == cls) return c;
  fail("Класс героя не найден");
}

// Класс и талант по ID таланта.
struct Found {
  const HeroClass* cls = nullptr;
  const Talent* t = nullptr;
};
Found needTalent(const World& w, Id talent) {
  const HeroClass* c = w.catalogs->classOfTalent(talent);
  if (!c) fail(talent ? "Талант не найден" : "Не выбран талант");
  return Found{c, c->talent(talent)};
}

Talent& talentRef(Tx& tx, Id cls, Id talent) {
  for (Talent& t : classRef(tx, cls).talents)
    if (t.id == talent) return t;
  fail("Талант не найден");
}

const Talent* talentAt(const HeroClass& c, int row, int col, Id except = 0) {
  for (const Talent& t : c.talents)
    if (t.row == row && t.col == col && t.id != except) return &t;
  return nullptr;
}

void needCell(int row, int col) {
  if (row < 0 || row >= kTalentRows) fail("Ярус дерева — от 1 до " + std::to_string(kTalentRows));
  if (col < 0 || col >= kTalentCols) fail("Столбец дерева — от 1 до " + std::to_string(kTalentCols));
}

// Вложено в ярусы выше row среди талантов set.
int spentAboveIn(const HeroClass& c, const std::vector<Id>& set, int row) {
  int s = 0;
  for (Id id : set)
    if (const Talent* t = c.talent(id); t && t->row < row) s += t->cost;
  return s;
}
int spentIn(const HeroClass& c, const std::vector<Id>& set) {
  int s = 0;
  for (Id id : set)
    if (const Talent* t = c.talent(id)) s += t->cost;
  return s;
}

// Законные таланты героя: проход по порядку изучения, каждый талант должен быть изучаем после уже оставленных
// (условие, порог яруса, очки уровня).
std::vector<Id> lawful(const HeroClass& c, const std::vector<Id>& learned, int level) {
  std::vector<Id> keep;
  int spent = 0;
  for (Id id : learned) {
    const Talent* t = c.talent(id);
    if (!t || contains(keep, id)) continue;
    if (t->prereq && !contains(keep, t->prereq)) continue;
    if (spentAboveIn(c, keep, t->row) < tierThreshold(c, t->row)) continue;
    if (spent + t->cost > level) continue;
    keep.push_back(id);
    spent += t->cost;
  }
  return keep;
}

// Привести таланты героев класса к законным; возвращает число героев, у которых таланты сняты.
int fitHeroes(Tx& tx, Id cls) {
  const HeroClass* c = tx.w().heroClass(cls);
  if (!c) return 0;
  const HeroClass hc = *c;
  int n = 0;
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& x) { return x.heroClass == cls && !x.talents.empty(); })) {
    const Character& ch = *tx.w().character(cid);
    std::vector<Id> keep = lawful(hc, ch.talents, ch.level);
    if (keep.size() == ch.talents.size()) continue;
    tx.character(cid).talents = std::move(keep);
    n++;
  }
  return n;
}

// Условия талантов дерева выше зависимых (после переноса).
void checkLinks(const HeroClass& c) {
  for (const Talent& t : c.talents) {
    if (!t.prereq) continue;
    const Talent* p = c.talent(t.prereq);
    if (p && p->row >= t.row)
      fail("Условие " + talName(*p) + " должно стоять выше таланта " + talName(t) + " — сначала снимите стрелку");
  }
}

std::string uniqueTalentName(const HeroClass& c, const std::string& name, Id except) {
  std::string n = trim(name);
  std::vector<std::string> taken;
  for (const Talent& t : c.talents)
    if (t.id != except) taken.push_back(t.name);
  if (n.empty()) return uniqueName(taken, "Новый талант");
  for (const std::string& x : taken)
    if (utf8::searchKey(x) == utf8::searchKey(n)) fail("Талант «" + x + "» уже есть в дереве класса");
  return n;
}

const Character& needHero(const World& w, Id character) { return needCharacter(w, character); }

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

int setHeroClass(Tx& tx, const HeroClass& c0) {
  const HeroClass& cur = needClass(tx.w(), c0.id);
  const std::string n = trim(c0.name);
  if (n.empty()) fail("Название класса не может быть пустым");
  for (const HeroClass& o : tx.w().catalogs->classes)
    if (o.id != c0.id && utf8::searchKey(o.name) == utf8::searchKey(n)) fail("Класс «" + o.name + "» уже есть");
  if (c0.tierPoints < 1 || c0.tierPoints > kMaxTierPoints)
    fail("Очков на ярус — от 1 до " + std::to_string(kMaxTierPoints));
  const bool tiers = cur.tierPoints != c0.tierPoints;
  HeroClass& c = classRef(tx, c0.id);
  c.name = n;
  c.desc = c0.desc;
  c.icon = c0.icon;
  c.color = c0.color;
  c.tierPoints = c0.tierPoints;
  return tiers ? fitHeroes(tx, c0.id) : 0;
}

void removeHeroClass(Tx& tx, Id cls) {
  needClass(tx.w(), cls);
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& x) { return x.heroClass == cls; })) {
    Character& ch = tx.character(cid);
    ch.heroClass = 0;
    ch.talents.clear();
  }
  auto& list = tx.catalogs().classes;
  list.erase(std::remove_if(list.begin(), list.end(), [&](const HeroClass& c) { return c.id == cls; }), list.end());
}

// ================================================================ таланты дерева
Id addTalent(Tx& tx, Id cls, int row, int col, const std::string& name) {
  const HeroClass& c = needClass(tx.w(), cls);
  needCell(row, col);
  if (const Talent* x = talentAt(c, row, col)) fail("Ячейка занята талантом " + talName(*x));
  Talent t;
  t.id = tx.nextId(Seq::Talent);
  t.name = uniqueTalentName(c, name, 0);
  t.icon = "talent";
  t.row = row;
  t.col = col;
  const Id id = t.id;
  classRef(tx, cls).talents.push_back(std::move(t));
  return id;
}

int setTalent(Tx& tx, const Talent& t0) {
  const Found f = needTalent(tx.w(), t0.id);
  const Id cls = f.cls->id;
  if (trim(t0.name).empty()) fail("Название таланта не может быть пустым");
  const std::string n = uniqueTalentName(*f.cls, t0.name, t0.id);
  if (t0.cost < schema::kMinTalentCost || t0.cost > schema::kMaxTalentCost)
    fail("Стоимость таланта — от " + std::to_string(schema::kMinTalentCost) + " до " + std::to_string(schema::kMaxTalentCost) + " очков");
  std::vector<Id> mods;
  for (Id m : t0.modifiers) {
    const Modifier* x = tx.w().modifier(m);
    if (!x) fail("Модификатор не найден");
    if (contains(mods, m)) continue;
    if (x->kind != ModKind::Any && x->kind != ModKind::Hero)
      fail("Модификатор «" + x->name + "» не для героев: таланту подходят модификаторы видов «Везде» и «Для героев»");
    if (x->key == schema::mod::Dead || x->key == schema::mod::Captive || schema::isAutoKey(x->key))
      fail("Модификатор «" + x->name + "» — состояние героя или ставится сам: талант его не даёт");
    mods.push_back(m);
  }
  const bool cost = f.t->cost != t0.cost;
  Talent& t = talentRef(tx, cls, t0.id);
  t.name = n;
  t.desc = t0.desc;
  t.icon = t0.icon;
  t.cost = t0.cost;
  t.modifiers = std::move(mods);
  return cost ? fitHeroes(tx, cls) : 0;
}

int moveTalent(Tx& tx, Id talent, int row, int col) {
  const Found f = needTalent(tx.w(), talent);
  needCell(row, col);
  if (f.t->row == row && f.t->col == col) return 0;
  const Id cls = f.cls->id;
  HeroClass next = *f.cls;
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
  classRef(tx, cls).talents = next.talents;
  return fitHeroes(tx, cls);
}

int setTalentPrereq(Tx& tx, Id talent, Id prereq) {
  const Found f = needTalent(tx.w(), talent);
  const Id cls = f.cls->id;
  if (f.t->prereq == prereq) return 0;
  if (prereq) {
    if (prereq == talent) fail("Талант не может требовать сам себя");
    const Talent* p = f.cls->talent(prereq);
    if (!p) fail("Условие — талант того же класса");
    if (p->row >= f.t->row) fail("Условие " + talName(*p) + " должно стоять в ярусе выше таланта " + talName(*f.t));
  }
  talentRef(tx, cls, talent).prereq = prereq;
  return fitHeroes(tx, cls);
}

int removeTalent(Tx& tx, Id talent) {
  const Found f = needTalent(tx.w(), talent);
  const Id cls = f.cls->id;
  // Герои, у которых таланты изменятся: изучившие удаляемый и те, у кого он держал порог яруса (снимаются и
  // таланты, которые без него нельзя изучить).
  std::vector<Id> lost;
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& x) { return contains(x.talents, talent); })) {
    eraseValue(tx.character(cid).talents, talent);
    lost.push_back(cid);
  }
  HeroClass& c = classRef(tx, cls);
  for (Talent& t : c.talents)
    if (t.prereq == talent) t.prereq = 0;
  c.talents.erase(std::remove_if(c.talents.begin(), c.talents.end(), [&](const Talent& t) { return t.id == talent; }), c.talents.end());
  const HeroClass hc = *tx.w().heroClass(cls);
  for (Id cid : idsWhere(tx.w().characters, [&](const Character& x) { return x.heroClass == cls && !x.talents.empty(); })) {
    const Character& ch = *tx.w().character(cid);
    std::vector<Id> keep = lawful(hc, ch.talents, ch.level);
    if (keep.size() == ch.talents.size()) continue;
    tx.character(cid).talents = std::move(keep);
    if (!contains(lost, cid)) lost.push_back(cid);
  }
  return int(lost.size());
}

const HeroClass* talentClass(const World& w, Id talent) { return w.catalogs->classOfTalent(talent); }

std::vector<Id> talentDependents(const World& w, Id talent) {
  std::vector<Id> out;
  if (const HeroClass* c = talentClass(w, talent))
    for (const Talent& t : c->talents)
      if (t.prereq == talent) out.push_back(t.id);
  return out;
}

int talentTiers(const HeroClass& c) {
  int n = 0;
  for (const Talent& t : c.talents) n = std::max(n, t.row + 1);
  return n;
}

// ================================================================ герой
int talentPoints(const World& w, Id character) {
  const Character* c = w.character(character);
  return c ? std::clamp(c->level, 1, schema::kMaxHeroLevel) : 0;
}

int talentPointsSpent(const World& w, Id character) {
  const Character* c = w.character(character);
  const HeroClass* hc = c ? w.heroClass(c->heroClass) : nullptr;
  return hc ? spentIn(*hc, c->talents) : 0;
}

int talentSpentAbove(const World& w, Id character, int row) {
  const Character* c = w.character(character);
  const HeroClass* hc = c ? w.heroClass(c->heroClass) : nullptr;
  return hc ? spentAboveIn(*hc, c->talents, row) : 0;
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
  const Talent* t = hc->talent(talent);
  if (!t) return no("Талант не из дерева класса " + clsName(*hc));
  if (contains(c->talents, talent)) return no(talName(*t) + " уже изучен");
  if (t->prereq && !contains(c->talents, t->prereq)) {
    const Talent* p = hc->talent(t->prereq);
    return no("Сначала изучите " + (p ? talName(*p) : std::string("предшествующий талант")));
  }
  const int need = tierThreshold(*hc, t->row), above = spentAboveIn(*hc, c->talents, t->row);
  if (above < need)
    return no("Ярус " + std::to_string(t->row + 1) + " откроется, когда в ярусах выше будет вложено " + std::to_string(need) + " " +
              plural(need, "очко", "очка", "очков") + " (сейчас " + std::to_string(above) + ")");
  const int free = talentPoints(w, character) - spentIn(*hc, c->talents);
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
  const HeroClass* hc = w.heroClass(c->heroClass);
  const Talent* t = hc ? hc->talent(talent) : nullptr;
  if (!t || !contains(c->talents, talent)) return no("Талант не изучен");
  std::vector<Id> rest = c->talents;
  eraseValue(rest, talent);
  for (Id id : rest) {
    const Talent* x = hc->talent(id);
    if (!x) continue;
    if (x->prereq == talent) return no("От " + talName(*t) + " зависит изученный " + talName(*x) + " — сначала отмените его");
    if (spentAboveIn(*hc, rest, x->row) < tierThreshold(*hc, x->row))
      return no("Без " + talName(*t) + " закроется ярус " + std::to_string(x->row + 1) + " с изученным " + talName(*x) + " — сначала отмените его");
  }
  if (why) why->clear();
  return true;
}

void unlearnTalent(Tx& tx, Id character, Id talent) {
  needHero(tx.w(), character);
  std::string why;
  if (!canUnlearnTalent(tx.w(), character, talent, &why)) fail(why);
  eraseValue(tx.character(character).talents, talent);
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
  if (cls) needClass(tx.w(), cls);
  if (c.heroClass == cls) return;
  Character& m = tx.character(character);
  m.heroClass = cls;
  m.talents.clear();   // дерево другого класса — изучение заново
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
  return relic + kPhylactery + (hero.empty() ? std::string("Без имени") : hero) + "»)";
}

bool isPhylactery(const Relic& r) { return r.name.find(kPhylactery) != std::string::npos; }

std::vector<Id> phylacteryRelics(const World& w, Id character) {
  std::vector<Id> out;
  const Character* c = w.character(character);
  if (!c) return out;
  for (Id rid : c->inventory)
    if (const Relic* r = w.relic(rid); r && int(r->rarity) >= int(Rarity::Epic) && !isPhylactery(*r)) out.push_back(rid);
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
    if (!contains(c->inventory, relic)) out.push_back("Филактерию отдают только из инвентаря героя");
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
  for (Relic& x : tx.catalogs().relics)
    if (x.id == relic) x.name = newName;
  addLog(tx, LogKind::Note,
         q(hero) + " возвысился до Лича: " + amount(schema::kLichCorpses) + " трупов, " + amount(schema::kLichDeathEssence) +
             " эссенции смерти, филактерия — " + q(r.name),
         LogRefs{0, 0, {c.faction}});
}

}  // namespace rg::rules
