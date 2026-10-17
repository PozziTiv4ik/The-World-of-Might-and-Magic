// Regnum — полноэкранный редактор дерева построек (buildings*.cpp): копия построек на кадр, дорожки категорий,
// состояние редактора, действия, сцена холста и панель свойств. Только для файлов редактора.
#pragma once
#include <unordered_map>
#include <unordered_set>

#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/widgets.h"
#include "gfx/icons.h"

namespace rg::app::bldtree {

using namespace tree;
using platform::Key;

constexpr float kBW = 232, kBH = 100;          // карточка постройки (единицы схемы)
constexpr float kHead = 30, kLanePad = 30;     // верхний и нижний отступы дорожки (подписи — в колонке слева)
constexpr float kSideW = 372;
constexpr float kGutter = 152;                // колонка подписей дорожек (точки интерфейса)
constexpr int kCats = int(BuildingCat::Count);

// Копия постройки на кадр.
struct BN {
  Id id = 0;
  Id owner = 0;
  std::string name, desc, icon;
  BuildingCat cat = BuildingCat::Economic;
  std::vector<BuildingReq> reqs;
  std::vector<Id> techs;           // требуемые технологии
  std::vector<BuildingLevel> levels;
  Vec2 pos;
  int built = 0, building = 0;   // провинций с постройкой: достроено / строится
  bool match = true;
  // Особые возможности.
  bool convert = false, essenceGen = false, specialAccess = false;
  bool relicStore = false, healing = false, plague = false, shipyard = false, mercenary = false, coastal = false;
  bool market = false, drain = false, upkeepCut = false, extract = false;
  std::vector<Id> extractGroups, extractExclude;   // постройка добычи: группы ресурсов и исключения
  u32 stateKinds = 0;                              // только для видов государства (0 — для всех)
  std::vector<Recipe> recipes;
  std::vector<Id> specials;
  std::vector<StateReq> stateReqs;  // требования «на государство» (соборы)
  std::string key;                  // встроенная постройка (правила узнают её по ключу)
  Id cultAt = 0;                   // культовая: провинция, где она уже есть или строится (0 — нигде)
};

struct LinkSel {
  Id req = 0, b = 0;   // b требует req
  explicit operator bool() const { return req && b; }
  bool operator==(const LinkSel&) const = default;
};

struct Lanes {
  double top[kCats]{}, h[kCats]{};
  int count[kCats]{};
  double total = 0;
  int at(double y) const {
    for (int c = 0; c < kCats; c++)
      if (y < top[c] + h[c]) return c;
    return kCats - 1;
  }
};

struct Ed {
  Id sel = 0;
  LinkSel link;
  Camera cam;
  Pan pan;
  bool bgDragged = false;
  Id drag = 0;
  Vec2 dragStart, dragPos;   // мировое положение карточки (левый верх) при перетаскивании
  bool dragMoved = false;
  Id conn = 0;
  bool connOut = true;
  std::string query;
  Id focusName = 0;     // технология/постройка, чьё имя получит фокус клавиатуры
  bool skipClick = false;   // отпускание после двойного щелчка — не снимать выделение
  bool refit = false;       // вписать дерево в следующем кадре (после правки раскладки)
  bool revealSel = false;
  Id menuB = 0;
  LinkSel menuLink;
  Vec2 menuAt;
  Flag flag;
};

struct Pending {
  Id owner = 0, building = 0;
  bool set = false;
};

// ---------------------------------------------------------------- раскладка и действия (buildings_actions.cpp)
Pending& pending();                                    // постройка, которую выделить при открытии (openBuildingTree)
double snap(double v);                                 // шаг сетки схемы — 4
const BN* find(const std::vector<BN>& bs, Id id);
Lanes lanesOf(const std::vector<BN>& bs);
Vec2 cardPos(const Lanes& L, const BN& b);             // левый верх карточки в координатах схемы
Box2 boundsOf(const std::vector<BN>& bs, const Lanes& L);
std::string bname(const BN& b);                        // название или «Без названия»
std::string bname(const Building& b);
std::string tname(const Tech& t);
// Требования построек — правилами: отказ правила — уведомлением с причиной.
void addReq(App& a, Id b, Id req);
void removeReq(App& a, LinkSel l);
void setReqLevel(App& a, Id b, Id req, int level);
Color roleColor(rules::BuildingRole r);                // значок особой возможности постройки
void askDelete(App& a, const BN& b);
Id createAt(App& a, Id owner, const Lanes& L, Vec2 center);   // новая постройка у точки схемы; 0 — не создана
void autoLayout(App& a, const std::vector<BN>& bs);

// ---------------------------------------------------------------- сцена (buildings_scene.cpp)
struct Card {
  BN b;
  Vec2 at;
  std::string cultAt;      // культовая: провинция, где уже стоит
  bool sel = false, hover = false, dim = false, drop = false, dropBad = false, outHot = false, inHot = false, hasIn = false, hasOut = false, ghost = false;
};
using bld::kDrainInk;
using bld::kEssenceInk;
using bld::kExtractInk;
using bld::kPlagueInk;
using bld::kRelicInk;
struct EdgeD {
  Curve k;
  Color col;
  float width = 2;
  bool sel = false;
  int level = 1;
};
// Требование к постройке другого дерева (уникальная требует общую): плашка слева от карточки и стрелка во вход.
struct Ext {
  Id req = 0, b = 0;
  Id owner = 0;            // дерево требуемой постройки (0 — общее)
  RectF pill;              // мировые единицы
  Vec2 port;               // вход карточки
  std::string label, icon;
  Color col;
  bool hot = false;
};
// Плашки внешних требований карточки at (левый верх) — по одной на требование, по центру входа.
std::vector<Ext> extsOf(const World& w, const std::vector<BN>& bs, const BN& b, Vec2 at);

struct Scene {
  Camera cam;
  Ink k;
  Lanes lanes;
  int dropLane = -1;
  std::vector<Card> cards;
  std::vector<EdgeD> edges;
  std::vector<Ext> exts;
  bool pending = false;
  Curve pend;
  Color pendCol;
};
void renderScene(const Scene& s, gfx::Canvas& c, RectF dev, float scale);

// ---------------------------------------------------------------- панель свойств (buildings_side.cpp, buildings_levels.cpp,
// buildings_roles.cpp)
void levelCard(App& a, const BN& b, int li, bool ro);
void rolesSection(App& a, const BN& b, bool ro);   // особые возможности и виды государства
void sideBuilding(App& a, Ed& ed, const std::vector<BN>& bs, const BN& b);
void sideLink(App& a, Ed& ed, const std::vector<BN>& bs);
void sideOverview(App& a, Ed& ed, Id owner, const std::vector<BN>& bs, const Lanes& L);

// Редактор на месте карты (buildings.cpp).
void drawBuildingTree(App& a, Id owner);

}  // namespace rg::app::bldtree
