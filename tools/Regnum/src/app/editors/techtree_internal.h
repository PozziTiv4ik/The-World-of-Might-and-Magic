// Regnum — полноэкранный редактор дерева технологий (techtree*.cpp): копия дерева на кадр, состояние технологии для
// изучающей фракции, состояние редактора, действия, сцена холста и панель свойств. Только для файлов редактора.
#pragma once
#include <unordered_map>
#include <unordered_set>

#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/widgets.h"

namespace rg::app::techtree {

using namespace tree;
using platform::Key;

constexpr float kW = 232, kH = 96;   // карточка технологии (единицы схемы)
constexpr float kSideW = 356;

// ---------------------------------------------------------------- состояние технологии
enum class St : u8 { Locked, Available, Research, Studied };

// Копия технологии на кадр (действия посреди кадра заменяют мир — указатели на записи не держим).
// turns — базовый срок, need — срок с модификатором «Время исследования технологий» государства (rules::researchTurns);
// progress — пройдено ходов (может быть больше turns, если срок увеличен). Для общей технологии изучение, срок и
// состояние — изучающей фракции.
struct TN {
  Id id = 0;
  std::string name, desc;
  int turns = 1, need = 1, progress = 0;
  bool studied = false, research = false;
  Vec2 pos;
  std::vector<Id> prereqs, mods;
  St st = St::Locked;
  std::vector<Id> missing;
  std::vector<std::string> problems;   // не достроены нужные постройки, не хватает ресурсов на стоимость
  std::map<Id, double> cost;           // стоимость исследования (ТЗ «Доработки №4», п.1)
  std::vector<std::string> needKeys;   // постройки государства (ключи schema::bld)
  bool match = true;
};

struct LinkSel {
  Id pre = 0, tech = 0;   // tech требует pre
  explicit operator bool() const { return pre && tech; }
  bool operator==(const LinkSel&) const = default;
};

struct Ed {
  Id sel = 0;
  LinkSel link;
  Camera cam;
  Pan pan;
  bool bgDragged = false;
  Id drag = 0;
  Vec2 dragStart;
  Id conn = 0;
  bool connOut = true;
  std::string query;
  Id focusName = 0;         // технология, чьё имя получит фокус клавиатуры
  bool skipClick = false;   // отпускание после двойного щелчка — не снимать выделение
  bool refit = false;       // вписать дерево в следующем кадре (после правки раскладки)
  bool revealSel = false;
  Id menuTech = 0;
  LinkSel menuLink;
  Vec2 menuAt;
  Flag flag;   // копия флага: ui::flag держит ссылку до конца кадра
  Id learner = 0;           // общее дерево: фракция, за которую показано и правится изучение
};

struct Pending {
  Id faction = 0, tech = 0, learner = 0;
  bool set = false;
  Id refit = 0;   // фракция, чьё дерево вписать в следующем кадре (после копирования из подтверждения)
};
// ---------------------------------------------------------------- состояние и действия (techtree_actions.cpp)
const char* stIcon(St s);
const char* stName(St s);
ui::Tone stTone(St s);
Color stColor(const Ink& k, St s);
int turnsLeft(int turns, int progress);   // ходов до конца исследования (не меньше 1)
// Состояние технологии для изучающей фракции who (технология своего дерева — 0 или её фракция; общая — фракция,
// за которую идёт изучение); missing — недостающие условия.
St stateOf(const World& w, Id tech, Id who, std::vector<Id>* missing = nullptr);
Pending& pending();                       // технология, которую выделить при открытии (openTechTree)
double snap(double v);                    // шаг сетки схемы — 4
Box2 boundsOf(const std::vector<TN>& ts);
const TN* find(const std::vector<TN>& ts, Id id);
std::string techNames(const World& w, const std::vector<TN>& ts, const std::vector<Id>& ids, size_t maxN = 3);   // «А, Б и ещё 2»
std::string bonusText(const World& w, const TN& t);
Id defaultLearner(const World& w);        // общее дерево: за кого изучение по умолчанию
bool learnerSwitch(std::string_view id, Id& value);
void askDelete(App& a, Id tech, const std::string& name);
void removeLink(App& a, LinkSel l);
void setStudied(App& a, Id tech, bool on, Id who);
void startResearch(App& a, Id tech, Id who);
// Начать или остановить исследование в дереве faction (общее — 0, за who) может эта роль: администратор — за любую
// фракцию, игрок — за своё государство (остальное в дереве игроку только для просмотра).
bool canResearch(const App& a, Id faction, Id who);
void stopResearch(App& a, Id tech, Id who);
Id createAt(App& a, Id faction, Vec2 center);   // новая технология у точки схемы; 0 — не создана
void copyMenu(App& a, Id faction, bool nonEmpty);   // меню «Скопировать дерево» (copyfrom)
Id neighbor(const std::vector<TN>& ts, const TN& cur, int dx, int dy);   // соседняя карточка для стрелок

// ---------------------------------------------------------------- сцена (techtree_scene.cpp)
struct PriceTag {
  std::string icon;
  Color col;
  std::string text;
};
struct Card {
  TN t;
  std::string bonus, need;
  std::vector<PriceTag> price;   // стоимость исследования: значок ресурса и количество
  bool sel = false, hover = false, dim = false, drop = false, dropBad = false;
  bool checkHot = false, outHot = false, inHot = false, hasIn = false, hasOut = false;
};
struct EdgeD {
  Curve k;
  Color col;
  Color from, to;          // гнёзда на концах связи (цвета состояния карточек)
  float width = 2;
  bool hot = false, sel = false;
};
// Прямоугольник карточки и связь «условие → технология» по взаимному положению карточек (tree::linkCurve: ветка
// «змейкой» идёт и справа налево, и вниз).
inline Box2 cardBox(const TN& t) { return Box2{t.pos.x, t.pos.y, t.pos.x + kW, t.pos.y + kH}; }
inline Curve techLink(const TN& pre, const TN& t) { return linkCurve(cardBox(pre), cardBox(t)); }
// Условие из общего дерева у технологии фракции: плашка слева от карточки (состояние для этой фракции) и стрелка во
// вход; щелчок — общее дерево с этой технологией.
struct Ext {
  Id pre = 0, tech = 0;
  RectF pill;              // мировые единицы
  Vec2 port;               // вход карточки
  std::string label;
  St st = St::Locked;
  bool hot = false;
};
struct Scene {
  Camera cam;
  Ink k;
  std::vector<Card> cards;
  std::vector<EdgeD> edges;
  std::vector<Ext> exts;
  bool pending = false;
  Curve pend;
  Color pendCol;
  bool readOnly = false;
};
// Плашки условий из общего дерева у технологии t — по одной на условие, по центру входа.
std::vector<Ext> extsOf(const World& w, const std::vector<TN>& ts, const TN& t, Id faction);
void renderScene(const Scene& s, gfx::Canvas& c, RectF dev, float scale);

// ---------------------------------------------------------------- панель свойств (techtree_side.cpp)
void sideOverview(App& a, Ed& ed, Id faction, const std::vector<TN>& ts);
void sideLink(App& a, Ed& ed, const std::vector<TN>& ts);
void sideTech(App& a, Ed& ed, Id faction, Id who, const std::vector<TN>& ts, const TN& t);

// Редактор на месте карты (techtree.cpp).
void drawTechTree(App& a, Id faction);


}  // namespace rg::app::techtree
