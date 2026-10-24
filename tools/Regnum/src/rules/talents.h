// Regnum — правила: классы героев и деревья талантов, уровень героя, лич, группы реликвий (rules/talents.cpp, rules/catalog.cpp).
// Общий заголовок правил — rules/rules.h.
#pragma once
#include "core/schema.h"
#include "core/world.h"

namespace rg::rules {

// ================================================================ классы и подклассы героев, таланты, лич, группы реликвий
// Класс героя — набор подклассов (число не ограничено); у подкласса своё дерево талантов (как в World of Warcraft):
// ячейка — ярус row (0 — верхний) и столбец col (0…4), в ячейке не больше одного таланта; у таланта 1…5 рангов по
// cost очков; условие — талант того же подкласса из более высокого яруса (меньший row), изученный полностью, поэтому
// циклов нет. Ярус r открыт герою, когда в ярусах выше он вложил не меньше r × tierPoints очков. Очков — по одному за
// уровень. Герой выбирает класс и подкласс; изучение у каждого героя своё (Character::talents — ранги в порядке
// изучения). Правка дерева (стоимость, ранги, ячейка, условие, очки на ярус, удаление) снимает у героев подкласса
// ранги, которые перестали быть законными (по порядку изучения); функции правки возвращают число таких героев.
using schema::kMaxTierPoints, schema::kTalentCols, schema::kTalentRows;   // столбцы, ярусы (как при чтении файла), очки на ярус
Id addHeroClass(Tx& tx, const std::string& name = {});
// Название (уникальное), описание, значок, цвет; подклассы записи не меняются.
void setHeroClass(Tx& tx, const HeroClass& c);
void removeHeroClass(Tx& tx, Id cls);                          // герои класса теряют класс, подкласс и таланты
// Подкласс класса: название уникально внутри класса; новое дерево — 10 очков на ярус.
Id addSubClass(Tx& tx, Id cls, const std::string& name = {});
// Название, описание, значок, очки на ярус (1…60); таланты записи не меняются.
int setSubClass(Tx& tx, const SubClass& s);
void removeSubClass(Tx& tx, Id sub);                           // герои подкласса теряют подкласс и таланты
const HeroClass* subclassClass(const World& w, Id sub);        // класс подкласса
// Новый талант в свободной ячейке дерева подкласса; возвращает его ID.
Id addTalent(Tx& tx, Id sub, int row, int col, const std::string& name = {});
// Название (уникальное в дереве), описание, значок, стоимость ранга 1…5, рангов 1…5, модификаторы героя (вид «Везде»
// или «Для героев», без состояний «Мертв» и «Взят в плен»). Ячейка и условие — moveTalent и setTalentPrereq.
int setTalent(Tx& tx, const Talent& t);
// Перенести талант в ячейку; занятая ячейка — таланты меняются местами. Условия остаются выше зависимых — иначе отказ.
int moveTalent(Tx& tx, Id talent, int row, int col);
int setTalentPrereq(Tx& tx, Id talent, Id prereq);             // 0 — снять условие
// Удалить талант: у героев снимается, у зависимых талантов условие снимается.
int removeTalent(Tx& tx, Id talent);
const SubClass* talentSubClass(const World& w, Id talent);     // подкласс, в дереве которого талант
// Таланты, для которых talent — условие.
std::vector<Id> talentDependents(const World& w, Id talent);
// Очков, которые нужно вложить в ярусы выше, чтобы открыть ярус row.
inline int tierThreshold(const SubClass& s, int row) { return std::max(0, row) * std::max(1, s.tierPoints); }
// Ярусов в дереве (наибольший занятый + 1; пустое дерево — 0).
int talentTiers(const SubClass& s);

// ---- герой: уровень, класс, подкласс, изучение талантов
const SubClass* heroSubClass(const World& w, Id character);   // подкласс героя (его класса); nullptr — нет
int talentPoints(const World& w, Id character);                // всего очков (уровень героя)
int talentPointsSpent(const World& w, Id character);           // вложено в изученные ранги
int heroSpentAbove(const World& w, Id character, int row);     // вложено в ярусы выше row
int heroTalentRank(const World& w, Id character, Id talent);   // изучено рангов таланта
// Изучить следующий ранг можно: талант дерева подкласса героя, ранги не исчерпаны, условие изучено полностью, ярус
// открыт, хватает свободных очков.
bool canLearnTalent(const World& w, Id character, Id talent, std::string* why = nullptr);
void learnTalent(Tx& tx, Id character, Id talent);
// Снять последний ранг нельзя, если от него зависят другие изученные (условие или порог ярусов).
bool canUnlearnTalent(const World& w, Id character, Id talent, std::string* why = nullptr);
// player — действие игрока: снятие ранга, сброс талантов, смена класса или подкласса (кроме первого выбора) — не чаще
// «Ходов между сменами класса» (константа) и не у мёртвого, пленного или арестованного героя; отметка — respecTurn.
void unlearnTalent(Tx& tx, Id character, Id talent, bool player = false);
void resetTalents(Tx& tx, Id character, bool player = false);
// Сколько ходов игроку ждать смены класса, подкласса или талантов героя (0 — можно).
int respecWait(const World& w, Id character);
// Уровень героя 1…60. Ниже вложенных очков — отказ с причиной; resetTalents = true — сначала таланты сбрасываются.
void setHeroLevel(Tx& tx, Id character, int level, bool resetTalents = false);
// Класс героя (0 — без класса): смена класса снимает подкласс и таланты; у класса с единственным подклассом он
// выбирается сразу.
void setCharacterClass(Tx& tx, Id character, Id cls, bool player = false);
// Подкласс класса героя (0 — не выбран): смена подкласса сбрасывает таланты.
void setCharacterSubclass(Tx& tx, Id character, Id sub, bool player = false);

// ---- «Возвысить до Лича»: герой с «Некромантом» отдаёт 20 000 трупов и 5000 эссенции
// смерти своего государства и реликвию инвентаря не ниже эпической; «Некромант» сменяется «Личем», реликвия остаётся
// у героя с припиской « (Филактерия «Имя героя»)».
bool lichOffered(const World& w, Id character);                // кнопка видна: «Некромант» и ещё не лич
Id deathEssence(const World& w);                               // «Эссенция смерти» справочника (0 — нет)
std::string phylacteryName(const std::string& relic, const std::string& hero);
bool isPhylactery(const Relic& r);
// Реликвии инвентаря героя, годные в филактерию: не ниже эпической и ещё не филактерия (по главенству редкости).
std::vector<Id> phylacteryRelics(const World& w, Id character);
// Почему возвысить нельзя (пусто — можно); relic 0 — реликвия ещё не выбрана.
std::vector<std::string> lichProblems(const World& w, Id character, Id relic);
void ascendLich(Tx& tx, Id character, Id relic);

// ---- группы реликвий (как группы ресурсов) и реликвия в справочнике (rules/catalog.cpp)
Id addRelicGroup(Tx& tx, const std::string& name = {}, Id parent = 0);
void renameRelicGroup(Tx& tx, Id group, const std::string& name);
void setRelicGroupParent(Tx& tx, Id group, Id parent);         // без циклов
// Удалить группу: подгруппы и реликвии переходят к родителю. «Археологические находки» удалить нельзя.
void removeRelicGroup(Tx& tx, Id group);
void setRelicGroup(Tx& tx, Id relic, Id group);                // 0 — без группы
std::vector<Id> relicsIn(const World& w, Id group);            // с подгруппами; 0 — все реликвии
std::vector<Id> childRelicGroups(const World& w, Id parent);   // прямые подгруппы (0 — верхний уровень)
std::string relicGroupPath(const World& w, Id group);          // «Находки / Шантири»
// Порядок реликвий по главенству редкости (эпохальная выше), затем по названию.
void sortByRarity(const World& w, std::vector<Id>& relics);
// Изображение реликвии (PNG или JPEG; пусто — значок) и карточка проекта (05_Активы_персонажей, ASSET-…).
void setRelicImage(Tx& tx, Id relic, const std::string& bytes);
void setRelicEntity(Tx& tx, Id relic, const std::string& entity);

}  // namespace rg::rules
