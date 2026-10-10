// Regnum — полноэкранный редактор дерева построек (ТЗ 1.h.i–ii, 1.f.ii): общее дерево (одинаковое для всех
// государств) или уникальные постройки государства. Дорожки категорий (военные, экономические, промышленные,
// жилые и сельско-хозяйственные, религиозные, культовые), карточки построек, требования к другим постройкам (связи,
// уровень), «на государство» (соборы) и к технологиям, панель свойств: особые возможности (преобразование ресурсов по
// рецепту, генерация эссенции, доступ к особым отрядам, хранилище реликвий, целительство, чума, верфь, гильдия
// наёмников, только приморская), уровни — срок, стоимость ресурсами и эссенциями, корабли верфи, ресурсы и эссенции за
// ход, модификаторы (окно выбора с созданием нового), описание. ТЗ «Доработки», п.2–5, 7, «Ввод новых механик», п.4–5,
// «Доработки №1–4». Встроенная постройка (с ключом правил) отмечена замком.
#include <unordered_map>
#include <unordered_set>

#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/widgets.h"
#include "gfx/icons.h"

namespace rg::app {

// ================================================================ помощники
namespace bld {

Color catColor(BuildingCat c) {
  int i = clamp(int(c), 0, int(BuildingCat::Count) - 1);
  return Color::hex(schema::kBuildingCats[i].color);
}
const char* catIcon(BuildingCat c) { return schema::kBuildingCats[clamp(int(c), 0, int(BuildingCat::Count) - 1)].icon; }
const char* catName(BuildingCat c) { return schema::kBuildingCats[clamp(int(c), 0, int(BuildingCat::Count) - 1)].name; }

const char* iconOf(const Building& b) { return !b.icon.empty() && gfx::hasIcon(b.icon) ? b.icon.c_str() : catIcon(b.cat); }

int levelTurns(const Building& b, int level) {
  if (level < 1 || level > int(b.levels.size())) return 1;
  return std::max(1, b.levels[size_t(level - 1)].turns);
}

bool affordable(const std::map<Id, double>& cost, const Faction* payer) {
  if (!payer) return false;
  for (auto& [res, v] : cost)
    if (payer->stock(res) + 1e-9 < v) return false;
  return true;
}

void costChips(const std::map<Id, double>& cost, const Faction* payer, bool showEmpty) {
  const World& w = app().world();
  if (cost.empty()) {
    if (showEmpty) ui::label("Бесплатно", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "coins"});
    return;
  }
  ui::HStack row(20, ui::Align::Left, 4);
  bool first = true;
  for (auto& [res, v] : cost) {
    ui::IdScope s{i64(res)};
    if (!first) ui::spacer(10);
    first = false;
    const CatalogItem* c = w.resource(res);
    bool lack = payer && payer->stock(res) + 1e-9 < v;
    std::string tip = (c ? c->name : std::string("Ресурс"));
    if (payer) tip += ": нужно " + fmtNum(v, 3) + ", есть " + fmtNum(std::max(0.0, payer->stock(res)), 3);
    ui::iconColored(w::resourceIcon(w, res), lack ? ui::theme().danger : w::resourceColor(w, res), 16, tip);
    ui::label(fmtNum(v, 3) + (res == kGold ? " тыс." : ""), {.font = ui::Font::Strong, .ink = lack ? ui::Ink::Danger : ui::Ink::Normal, .tooltip = tip});
  }
}

void essCostChips(const std::map<Id, double>& cost, const Faction* payer) {
  const World& w = app().world();
  if (cost.empty()) return;
  ui::IdScope scope("esscost");
  ui::HStack row(20, ui::Align::Left, 4);
  bool first = true;
  for (auto& [e, v] : cost) {
    ui::IdScope s{i64(e)};
    if (!first) ui::spacer(10);
    first = false;
    const CatalogItem* c = w.essence(e);
    const bool lack = payer && payer->essence(e) + 1e-9 < v;
    std::string tip = c ? c->name : std::string("Эссенция");
    if (payer) tip += ": нужно " + fmtNum(v, 3) + ", есть " + fmtNum(std::max(0.0, payer->essence(e)), 3);
    ui::iconColored("essence", lack ? ui::theme().danger : w::essenceColor(w, e), 16, tip);
    ui::label(fmtNum(v, 3), {.font = ui::Font::Strong, .ink = lack ? ui::Ink::Danger : ui::Ink::Normal, .tooltip = tip});
  }
}

std::string essenceCostText(const World& w, const std::map<Id, double>& cost) {
  std::vector<std::string> p;
  for (auto& [e, v] : cost) {
    const CatalogItem* c = w.essence(e);
    p.push_back((c ? c->name : std::string("Эссенция")) + " " + fmtNum(v, 3));
  }
  return join(p, ", ");
}

std::string shipsText(u32 ships) {
  std::vector<std::string> p;
  for (int t = 0; t < int(ShipType::Count); t++)
    if ((ships >> t) & 1u) p.push_back(schema::shipType(ShipType(t)).name);
  return p.empty() ? std::string("—") : join(p, ", ");
}

void produceChips(const std::map<Id, double>& produce) {
  const World& w = app().world();
  if (produce.empty()) return;
  ui::IdScope scope("produce");
  ui::HStack row(20, ui::Align::Left, 4);
  ui::icon("repeat", ui::Ink::Muted, 14, "Даёт за ход");
  for (auto& [res, v] : produce) {
    ui::IdScope s{i64(res)};
    ui::spacer(6);
    const CatalogItem* c = w.resource(res);
    const std::string unit = res == kGold ? " тыс." : "";
    std::string tip = (c ? c->name : std::string("Ресурс")) + ": +" + fmtNum(v, 3) + unit + " за ход";
    ui::iconColored(w::resourceIcon(w, res), w::resourceColor(w, res), 16, tip);
    ui::label("+" + fmtNum(v, 3) + unit, {.font = ui::Font::Strong, .ink = ui::Ink::Success, .tooltip = tip});
  }
}

std::string produceText(const World& w, const std::map<Id, double>& produce) {
  std::vector<std::string> p;
  for (auto& [res, v] : produce) {
    const CatalogItem* c = w.resource(res);
    p.push_back("+" + fmtNum(v, 3) + (res == kGold ? std::string(" тыс. золота") : " " + (c ? utf8::lower(c->name) : std::string("ресурс"))));
  }
  return join(p, ", ");
}

void levelEffects(const World& w, const BuildingLevel& L, bool compact) {
  ui::IdScope s("fx");
  if (!tree::effectChips(w, L.modifiers) && !compact) ui::label("Без эффектов", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
}

std::string costText(const World& w, const std::map<Id, double>& cost) {
  if (cost.empty()) return "бесплатно";
  std::vector<std::string> p;
  for (auto& [res, v] : cost) {
    const CatalogItem* c = w.resource(res);
    p.push_back((c ? c->name : std::string("Ресурс")) + " " + fmtNum(v, 3) + (res == kGold ? " тыс." : ""));
  }
  return join(p, ", ");
}

void iconTile(const Building& b, float size, bool dim) {
  RectF r = ui::next(size, size);
  Color c = catColor(b.cat);
  ui::draw::rect(r, c.alpha(dim ? 0.08f : 0.16f), std::round(size * 0.26f));
  ui::draw::rectStroke(r, c.alpha(dim ? 0.2f : 0.35f), std::round(size * 0.26f), 1);
  float is = std::round(size * 0.56f);
  ui::draw::icon(iconOf(b), RectF{r.cx() - is * 0.5f, r.cy() - is * 0.5f, is, is}, dim ? c.alpha(0.5f) : c);
}

void tokens(const std::vector<Token>& list, float gap) {
  if (list.empty()) return;
  ui::IdScope scope("tokens");
  const RectF area = ui::avail();
  const float h = 20, rowGap = 4;
  float x = area.x, y = area.y;
  for (size_t i = 0; i < list.size(); i++) {
    const Token& t = list[i];
    ui::IdScope s{int(i)};
    const float iw = t.icon.empty() ? 0.f : (t.text.empty() ? 16.f : 19.f);
    const float tw = t.text.empty() ? 0.f : std::ceil(ui::measure(t.text, t.font)) + 2;
    const float w = iw + tw;
    if (x > area.x && x + w > area.right() + 0.5f) {
      x = area.x;
      y += h + rowGap;
    }
    if (!t.icon.empty()) {
      ui::at(RectF{x, y + 2, 16, 16});
      ui::iconColored(t.icon.c_str(), t.color, 16, t.tip);
    }
    if (!t.text.empty()) {
      ui::at(RectF{x + iw, y, tw, h});
      ui::label(t.text, {.font = t.font, .ink = t.ink, .tooltip = t.tip});
    }
    x += w + gap;
  }
  ui::next(y + h - area.y);
}

bool recipeValid(const World& w, const Recipe& r) {
  bool in = false;
  for (const ResAmount& x : r.in) in = in || (x.amount > 0 && w.resource(x.res));
  return in && r.out.res && w.resource(r.out.res);
}

std::vector<Token> recipeTokens(const World& w, const Recipe& r, const Faction* payer) {
  const ui::Theme& th = ui::theme();
  std::vector<Token> out;
  for (const ResAmount& x : r.in) {
    const CatalogItem* c = w.resource(x.res);
    if (!c || !(x.amount > 0)) continue;
    const bool lack = payer && payer->stock(x.res) + 1e-9 < x.amount;
    std::string tip = (c->name.empty() ? std::string("Ресурс") : c->name) + " на входе: " + fmtNum(x.amount, 3);
    if (payer) tip += ", есть " + fmtNum(std::max(0.0, payer->stock(x.res)), 3);
    out.push_back({w::resourceIcon(w, x.res), lack ? th.danger : w::resourceColor(w, x.res), fmtNum(x.amount, 3), lack ? ui::Ink::Danger : ui::Ink::Normal, tip});
  }
  if (out.empty()) out.push_back({"warning", th.warning, "нет входа", ui::Ink::Warning, {}, ui::Font::Small});
  out.push_back({"arrow-right", th.textMuted, {}, ui::Ink::Muted, {}});
  if (const CatalogItem* c = w.resource(r.out.res))
    out.push_back({w::resourceIcon(w, r.out.res), w::resourceColor(w, r.out.res), fmtNum(r.out.amount, 3), ui::Ink::Success,
                   (c->name.empty() ? std::string("Ресурс") : c->name) + " на выходе: " + fmtNum(r.out.amount, 3)});
  else
    out.push_back({"warning", th.warning, "нет выхода", ui::Ink::Warning, {}, ui::Font::Small});
  out.push_back({"hourglass", th.textMuted, nTurns(std::max(1, r.turns)), ui::Ink::Muted, "Цикл преобразования", ui::Font::Small});
  return out;
}

std::string recipeText(const World& w, const Recipe& r) {
  std::vector<std::string> in;
  for (const ResAmount& x : r.in)
    if (const CatalogItem* c = w.resource(x.res); c && x.amount > 0) in.push_back(c->name + " " + fmtNum(x.amount, 3));
  const CatalogItem* o = w.resource(r.out.res);
  return (in.empty() ? std::string("—") : join(in, " + ")) + " → " + (o ? o->name + " " + fmtNum(r.out.amount, 3) : std::string("—")) + " · " +
         nTurns(std::max(1, r.turns));
}

std::vector<Token> essenceTokens(const World& w, const std::map<Id, double>& essence) {
  std::vector<Token> out;
  for (auto& [e, v] : essence) {
    const CatalogItem* c = w.essence(e);
    if (!c || !(v > 0)) continue;
    out.push_back({"essence", w::essenceColor(w, e), "+" + fmtNum(v, 3), ui::Ink::Success, (c->name.empty() ? std::string("Эссенция") : c->name) + ": +" + fmtNum(v, 3) + " за ход"});
  }
  return out;
}

std::string essenceText(const World& w, const std::map<Id, double>& essence) {
  std::vector<std::string> p;
  for (auto& [e, v] : essence)
    if (const CatalogItem* c = w.essence(e); c && v > 0) p.push_back("+" + fmtNum(v, 3) + " " + utf8::lower(c->name));
  return join(p, ", ");
}

const char* cultRule(const Building& b) {
  if (b.cat != BuildingCat::Cult) return nullptr;
  return b.owner ? "Одна на государство" : "Одна на всю карту";
}

std::string specialsText(const World& w, const std::vector<Id>& specials) {
  std::vector<std::string> n;
  for (Id s : specials)
    if (const SpecialUnit* su = w.special(s)) n.push_back(su->name.empty() ? std::string("Без названия") : su->name);
  return join(n, ", ");
}

}  // namespace bld

namespace {

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
  Recipe recipe;
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
  bool skipClick = false;
  bool refit = false;       // вписать дерево в следующем кадре (после правки раскладки)   // отпускание после двойного щелчка — не снимать выделение
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
Pending& pending() {
  static Pending p;
  return p;
}

double snap(double v) { return std::round(v / 4) * 4; }

const BN* find(const std::vector<BN>& bs, Id id) {
  for (const BN& b : bs)
    if (b.id == id) return &b;
  return nullptr;
}

Lanes lanesOf(const std::vector<BN>& bs) {
  Lanes L;
  double maxY[kCats];
  for (int c = 0; c < kCats; c++) maxY[c] = -1;
  for (const BN& b : bs) {
    int c = clamp(int(b.cat), 0, kCats - 1);
    maxY[c] = std::max(maxY[c], std::max(0.0, b.pos.y));
    L.count[c]++;
  }
  double y = 0;
  for (int c = 0; c < kCats; c++) {
    L.top[c] = y;
    L.h[c] = kHead + std::max(0.0, maxY[c]) + kBH + kLanePad;
    y += L.h[c];
  }
  L.total = y;
  return L;
}

// Левый верх карточки в координатах схемы.
Vec2 cardPos(const Lanes& L, const BN& b) {
  int c = clamp(int(b.cat), 0, kCats - 1);
  return {b.pos.x, L.top[c] + kHead + std::max(0.0, b.pos.y)};
}

Box2 boundsOf(const std::vector<BN>& bs, const Lanes& L) {
  Box2 b;
  for (const BN& x : bs) {
    Vec2 p = cardPos(L, x);
    b.add(p);
    b.add({p.x + kBW, p.y + kBH});
  }
  if (b.empty()) b = Box2{0, 0, kBW * 3, L.total};
  b.add({b.x0, 0});
  b.add({b.x1, L.total});
  return b;
}

std::string bname(const BN& b) { return b.name.empty() ? std::string("Без названия") : b.name; }
std::string bname(const Building& b) { return b.name.empty() ? std::string("Без названия") : b.name; }
std::string tname(const Tech& t) { return t.name.empty() ? std::string("Без названия") : t.name; }

// Требования построек — правилами (ТЗ «Доработки», п.4 и 7): общая постройка требует только общие, уникальная —
// общие и своего государства; без циклов. Отказ правила — уведомлением с причиной.
void addReq(App& a, Id b, Id req) { a.act("Требование постройки", [&](Tx& tx) { rules::setBuildingReq(tx, b, req, 1); }); }

void removeReq(App& a, LinkSel l) {
  a.act("Удалить требование постройки", [&](Tx& tx) { rules::setBuildingReq(tx, l.b, l.req, 0); });
}

void setReqLevel(App& a, Id b, Id req, int level) {
  a.act("Уровень требуемой постройки", [&](Tx& tx) { rules::setBuildingReq(tx, b, req, level); });
}

// Цвет значка особой возможности постройки (карточка, переключатели).
Color roleColor(rules::BuildingRole r) {
  const ui::Theme& th = ui::theme();
  switch (r) {
    case rules::BuildingRole::Convert: return th.info;
    case rules::BuildingRole::Essence: return Color::hex(0x9b7bff);
    case rules::BuildingRole::Special: return th.accent;
  }
  return th.textDim;
}

void askDelete(App& a, const BN& b) {
  std::string text = "Постройка исчезнет из дерева";
  if (b.built + b.building > 0)
    text += " и из " + std::to_string(b.built + b.building) + " " + plural(b.built + b.building, "провинции", "провинций", "провинций") +
            "; незавершённое строительство вернёт стоимость";
  text += ". Действие можно отменить Ctrl+Z.";
  Id id = b.id;
  a.confirm("Удалить постройку «" + bname(b) + "»?", text, "Удалить", true,
            [id](App& x) { x.act("Удалить постройку", [&](Tx& tx) { rules::removeBuilding(tx, id); }); });
}

Id createAt(App& a, Id owner, const Lanes& L, Vec2 center) {
  int cat = L.at(center.y);
  Vec2 p{snap(center.x - kBW * 0.5), snap(std::max(0.0, center.y - kBH * 0.5 - L.top[cat] - kHead))};
  Id nid = 0;
  if (!a.act("Новая постройка", [&](Tx& tx) {
        nid = rules::createBuilding(tx, owner, "Новая постройка");
        Building& b = tx.building(nid);
        b.cat = BuildingCat(cat);
        b.pos = p;
        b.icon = bld::catIcon(BuildingCat(cat));
      }))
    return 0;
  return nid;
}

// Авторасстановка: столбец — глубина по требованиям (внутри дерева), строки — внутри дорожки категории.
// Связанные постройки стоят по глубине (порядок в столбце — по средней строке требований, меньше пересечений);
// одиночные (без связей) заполняют свободные места дорожки слева направо — дорожки остаются невысокими.
void autoLayout(App& a, const std::vector<BN>& bs) {
  std::unordered_map<Id, int> depth;
  std::function<int(Id, int)> dep = [&](Id id, int guard) -> int {
    if (auto it = depth.find(id); it != depth.end()) return it->second;
    const BN* b = find(bs, id);
    if (!b || guard > 64) return 0;
    int d = 0;
    for (const BuildingReq& r : b->reqs)
      if (find(bs, r.building) && r.building != id) d = std::max(d, dep(r.building, guard + 1) + 1);
    depth[id] = d;
    return d;
  };
  std::unordered_set<Id> linked;
  for (const BN& b : bs) {
    dep(b.id, 0);
    for (const BuildingReq& r : b.reqs)
      if (find(bs, r.building) && r.building != b.id) {
        linked.insert(b.id);
        linked.insert(r.building);
      }
  }
  std::unordered_map<Id, Vec2> place;   // столбец, строка
  std::unordered_map<Id, int> rowOf;
  int maxDepth = 0;
  for (auto& [id, d] : depth) maxDepth = std::max(maxDepth, d);
  for (int c = 0; c < kCats; c++) {
    std::vector<const BN*> lane, single;
    for (const BN& b : bs)
      if (int(b.cat) == c) (linked.count(b.id) ? lane : single).push_back(&b);
    auto byName = [](const BN* x, const BN* y) {
      int r = compareRu(x->name, y->name);
      return r != 0 ? r < 0 : x->id < y->id;
    };
    std::sort(single.begin(), single.end(), byName);
    std::map<std::pair<int, int>, bool> used;   // (столбец, строка)
    int rows = 0;
    for (int d = 0; d <= maxDepth; d++) {
      std::vector<const BN*> col;
      for (const BN* b : lane)
        if (depth[b->id] == d) col.push_back(b);
      // Порядок: средняя строка требований этой дорожки (корни — по названию).
      auto key = [&](const BN* b) {
        double sum = 0;
        int n = 0;
        for (const BuildingReq& r : b->reqs)
          if (auto it = rowOf.find(r.building); it != rowOf.end()) {
            sum += it->second;
            n++;
          }
        return n ? sum / n : 1e9;
      };
      std::stable_sort(col.begin(), col.end(), byName);
      std::stable_sort(col.begin(), col.end(), [&](const BN* x, const BN* y) { return key(x) < key(y); });
      int k = 0;
      for (const BN* b : col) {
        place[b->id] = {double(d), double(k)};
        rowOf[b->id] = k;
        used[{d, k}] = true;
        k++;
      }
      rows = std::max(rows, k);
    }
    // Дорожка растёт вширь (холст широкий); строк больше — только если столбцов стало бы больше шести.
    int total = int(lane.size() + single.size());
    rows = std::max({rows, 1, int(std::ceil(double(total) / 6.0))});
    for (const BN* b : single) {
      for (int col = 0;; col++) {
        int row = -1;
        for (int r = 0; r < rows; r++)
          if (!used.count({col, r})) {
            row = r;
            break;
          }
        if (row < 0) continue;
        used[{col, row}] = true;
        place[b->id] = {double(col), double(row)};
        break;
      }
    }
  }
  a.act("Расставить дерево построек", [&](Tx& tx) {
    for (const BN& b : bs) {
      auto it = place.find(b.id);
      if (it == place.end() || !tx.w().building(b.id)) continue;
      Vec2 p{it->second.x * double(rules::kTreeColStep), it->second.y * double(kBH + 24)};
      if (tx.w().building(b.id)->pos != p) tx.building(b.id).pos = p;
    }
  });
}

// ---------------------------------------------------------------- сцена
struct Card {
  BN b;
  Vec2 at;
  std::string cultAt;      // культовая: провинция, где уже стоит
  bool sel = false, hover = false, dim = false, drop = false, dropBad = false, outHot = false, inHot = false, hasIn = false, hasOut = false, ghost = false;
};
constexpr u32 kEssenceInk = 0x9b7bff;   // значок генерации эссенции на карточке
constexpr u32 kRelicInk = 0xa45cff;     // хранилище реликвий
constexpr u32 kPlagueInk = 0x9aac3a;    // здание чумы
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

void drawCard(gfx::Canvas& c, const Scene& s, const Card& cd) {
  const Ink& k = s.k;
  const BN& b = cd.b;
  const float z = float(s.cam.z), px = 1 / z;
  RectF r{float(cd.at.x), float(cd.at.y), kBW, kBH};
  Color cc = bld::catColor(b.cat);
  c.save();
  if (cd.dim) c.setOpacity(0.28f);
  c.boxShadow(r, 12, cd.sel || cd.hover || cd.ghost ? 28 : 14, 0, k.shadow.alpha(cd.sel || cd.hover || cd.ghost ? 0.95f : 0.7f), gfx::Pt{0, cd.ghost ? 10.f : 5.f});
  gfx::Gradient g;
  g.kind = gfx::Gradient::Linear;
  g.p0 = {r.x, r.y};
  g.p1 = {r.x, r.bottom()};
  g.stops = {{0.f, Color::mix(k.surface, cc, k.dark ? 0.09f : 0.06f)}, {1.f, k.surface}};
  gfx::Paint gp;
  gp.gradient = &g;
  c.fillRoundRect(r, 12, gp);
  Color bc = cd.sel ? k.accent : cd.hover ? Color::mix(k.borderStrong, cc, 0.4f) : Color::mix(k.border, cc, 0.22f);
  c.strokeRoundRect(r.inset(0.5f * px), 12, (cd.sel ? 1.6f : 1.f) * px, bc);
  if (cd.sel) c.strokeRoundRect(r.expand(4 * px), 12 + 4 * px, 2 * px, k.accent.alpha(0.45f));
  if (cd.drop) c.strokeRoundRect(r.expand(4 * px), 12 + 4 * px, 2.2f * px, (cd.dropBad ? k.danger : k.success).alpha(0.9f));
  // Плитка значка
  RectF tile{r.x + 14, r.y + 14, 42, 42};
  c.fillRoundRect(tile, 11, cc.alpha(0.17f));
  c.strokeRoundRect(tile.inset(0.5f * px), 11, 1 * px, cc.alpha(0.35f));
  std::string ic = !b.icon.empty() && gfx::hasIcon(b.icon) ? b.icon : std::string(bld::catIcon(b.cat));
  icon(c, ic, tile.inset(10), cc);
  // Название и подпись (культовая — одна на карту или на государство)
  text(c, bname(b), textStyle(14, gfx::FontWeight::Semibold), RectF{r.x + 66, r.y + 13, kBW - 66 - 14, 20}, k.text);
  std::string sub = b.cat == BuildingCat::Cult ? std::string(b.owner ? "Одна на государство" : "Одна на всю карту")
                                               : std::string(bld::catName(b.cat)) + " · " + std::to_string(b.levels.size()) + " " +
                                                     plural(i64(b.levels.size()), "уровень", "уровня", "уровней");
  text(c, sub, textStyle(11.5f), RectF{r.x + 66, r.y + 35, kBW - 66 - 14, 16}, b.cat == BuildingCat::Cult ? Color::mix(k.textMuted, cc, 0.55f) : k.textMuted);
  // Особые возможности, требуемые технологии, где уже стоит культовая постройка.
  {
    float fx = r.x + 66;
    const float fy = r.y + 53, fr = r.right() - 14;
    auto feat = [&](const char* name, Color col) {
      icon(c, name, RectF{fx, fy, 13, 13}, col);
      fx += 18;
    };
    if (!b.key.empty()) feat("lock", k.textMuted);
    if (b.convert) feat("convert", k.info);
    if (b.essenceGen) feat("essence", Color::hex(kEssenceInk));
    if (b.specialAccess) feat("special-unit", k.accent);
    if (b.relicStore) feat("relic", Color::hex(kRelicInk));
    if (b.healing) feat("heal", k.success);
    if (b.plague) feat("plague", Color::hex(kPlagueInk));
    if (b.shipyard) feat("shipyard", k.info);
    if (b.mercenary) feat("mercenary", k.accent);
    if (b.coastal) feat("anchor", k.textMuted);
    if (!b.techs.empty()) {
      icon(c, "tech-tree", RectF{fx, fy, 13, 13}, k.textMuted);
      std::string n = std::to_string(b.techs.size());
      gfx::TextStyle st = textStyle(10.5f, gfx::FontWeight::Semibold);
      float tw = gfx::measureText(n, st);
      text(c, n, st, RectF{fx + 15, fy - 1, tw + 2, 15}, k.textMuted);
      fx += 15 + tw + 8;
    }
    if (!cd.cultAt.empty() && fx < fr - 30) {
      icon(c, "map-pin", RectF{fx, fy, 13, 13}, k.accent);
      text(c, cd.cultAt, textStyle(10.5f), RectF{fx + 15, fy - 1, fr - fx - 15, 15}, k.textDim);
    }
  }
  // Уровни: римские цифры, построено
  float x = r.x + 14, y = r.y + 70;
  gfx::TextStyle lv = textStyle(10.5f, gfx::FontWeight::Semibold);
  int shown = std::min<int>(int(b.levels.size()), 5);
  for (int i = 0; i < shown; i++) {
    std::string rn = roman(i + 1);
    float w = std::max(22.f, gfx::measureText(rn, lv) + 12);
    RectF pr{x, y, w, 18};
    c.fillRoundRect(pr, 9, k.surfaceHi);
    c.strokeRoundRect(pr.inset(0.5f * px), 9, 1 * px, cc.alpha(0.3f));
    text(c, rn, lv, pr, k.textDim, gfx::Align::Center);
    x += w + 4;
  }
  if (int(b.levels.size()) > shown) text(c, "+" + std::to_string(b.levels.size() - size_t(shown)), lv, RectF{x, y, 30, 18}, k.textMuted);
  if (b.built + b.building > 0) {
    std::string t = std::to_string(b.built + b.building);
    gfx::TextStyle st = textStyle(11.5f, gfx::FontWeight::Semibold);
    float tw = gfx::measureText(t, st);
    RectF br{r.right() - 14 - tw - 22, y, tw + 22, 18};
    icon(c, "province", RectF{br.x, br.y + 2, 14, 14}, k.textMuted);
    text(c, t, st, RectF{br.x + 18, br.y, tw + 4, 18}, k.textDim);
  }
  // Требования к постройкам другого дерева (общие у уникальной)
  Vec2 in{r.x, r.y + kBH * 0.5}, out{r.right(), r.y + kBH * 0.5};
  if (cd.hasIn || cd.inHot || cd.hover || cd.sel) drawPort(c, in, cd.inHot ? k.accent : Color::mix(k.borderStrong, cc, 0.5f), k.bg, cd.hasIn, cd.inHot, z);
  if (cd.hasOut || cd.outHot || cd.hover || cd.sel) drawPort(c, out, cd.outHot ? k.accent : Color::mix(k.borderStrong, cc, 0.5f), k.bg, cd.hasOut, cd.outHot, z);
  c.restore();
}

void renderScene(const Scene& s, gfx::Canvas& c, RectF dev, float scale) {
  c.save();
  c.clipRoundRect(dev, 10 * scale);
  drawGrid(c, dev, scale, s.cam, s.k);
  applyCamera(c, dev, scale, s.cam);
  const float z = float(s.cam.z);
  // Дорожки категорий
  double x0 = s.cam.x - 10, x1 = s.cam.x + dev.w / (z * scale) + 10;
  double y0 = s.cam.y - 10, y1 = s.cam.y + dev.h / (z * scale) + 10;
  for (int i = 0; i < kCats; i++) {
    Color cc = bld::catColor(BuildingCat(i));
    // Первая и последняя дорожки продолжаются до краёв холста (щелчок там попадает в них же).
    double top = i == 0 ? std::min(y0, s.lanes.top[i]) : s.lanes.top[i];
    double bottom = i == kCats - 1 ? std::max(y1, s.lanes.top[i] + s.lanes.h[i]) : s.lanes.top[i] + s.lanes.h[i];
    RectF lr{float(x0), float(top), float(x1 - x0), float(bottom - top)};
    c.fillRect(lr, cc.alpha(s.dropLane == i ? 0.10f : (i % 2 ? 0.035f : 0.055f)));
    if (i > 0) c.fillRect(RectF{lr.x, float(s.lanes.top[i]), lr.w, 1.5f / z}, cc.alpha(0.35f));
  }
  for (const EdgeD& e : s.edges) {
    if (e.sel) drawCurve(c, e.k, s.k.accent.alpha(0.22f), e.width + 7, z, false);
    drawCurve(c, e.k, e.col, e.width, z, true);
    if (e.level > 1) {
      Vec2 m = curveAt(e.k, 0.5);
      std::string t = "ур. " + roman(e.level);
      gfx::TextStyle st = textStyle(10.5f, gfx::FontWeight::Semibold);
      float tw = gfx::measureText(t, st) + 12;
      RectF pr{float(m.x) - tw * 0.5f, float(m.y) - 9, tw, 18};
      c.fillRoundRect(pr, 9, s.k.surface);
      c.strokeRoundRect(pr, 9, 1 / z, e.col);
      text(c, t, st, pr, s.k.textDim, gfx::Align::Center);
    }
  }
  for (const Ext& e : s.exts) {
    Color col = e.hot ? s.k.accent : Color::mix(s.k.textMuted, e.col, 0.45f);
    drawCurve(c, curve({e.pill.right(), e.pill.y + e.pill.h * 0.5}, e.port), col.alpha(0.85f), e.hot ? 2.6f : 1.8f, z, true, true);
    c.fillRoundRect(e.pill, e.pill.h * 0.5f, s.k.surface);
    c.strokeRoundRect(e.pill.inset(0.5f / z), e.pill.h * 0.5f, (e.hot ? 1.6f : 1.f) / z, e.hot ? s.k.accent : e.col.alpha(0.55f));
    icon(c, e.icon, RectF{e.pill.x + 8, e.pill.y + (e.pill.h - 13) * 0.5f, 13, 13}, e.col);
    text(c, e.label, textStyle(11.5f), RectF{e.pill.x + 26, e.pill.y, e.pill.w - 34, e.pill.h}, e.hot ? s.k.text : s.k.textDim);
  }
  for (const Card& cd : s.cards) drawCard(c, s, cd);
  if (s.pending) drawCurve(c, s.pend, s.pendCol, 2.2f, z, true, true);
  c.restore();
}

std::vector<Ext> extsOf(const World& w, const std::vector<BN>& bs, const BN& b, Vec2 at) {
  std::vector<Ext> out;
  for (const BuildingReq& r : b.reqs) {
    if (find(bs, r.building)) continue;
    const Building* rb = w.building(r.building);
    if (!rb) continue;
    Ext e;
    e.req = r.building;
    e.b = b.id;
    e.owner = rb->owner;
    e.label = (rb->name.empty() ? std::string("Без названия") : rb->name) + (r.level > 1 ? " · " + roman(r.level) : std::string());
    e.icon = bld::iconOf(*rb);
    e.col = bld::catColor(rb->cat);
    out.push_back(std::move(e));
  }
  const double h = 24, gap = 6;
  double y0 = at.y + kBH * 0.5 - (double(out.size()) * h + double(out.size() > 0 ? out.size() - 1 : 0) * gap) * 0.5;
  gfx::TextStyle st = textStyle(11.5f);
  for (size_t i = 0; i < out.size(); i++) {
    Ext& e = out[i];
    double tw = std::min(150.0, double(gfx::measureText(e.label, st)));
    double w = tw + 26 + 12;
    e.port = {at.x, at.y + kBH * 0.5};
    e.pill = RectF{float(at.x - 40 - w), float(y0 + double(i) * (h + gap)), float(w), float(h)};
  }
  return out;
}

// ---------------------------------------------------------------- панель свойств
struct IconChoice {
  const char* name;
  const char* title;
};
const IconChoice kIcons[] = {
    {"building", "Постройка"},   {"house", "Жильё"},          {"home", "Дом"},             {"castle", "Замок"},
    {"tower", "Башня"},          {"hq", "Штаб"},              {"capital", "Столица"},      {"crown", "Корона"},
    {"coins", "Монеты"},         {"treasury", "Казна"},       {"income", "Доход"},         {"trade", "Торговля"},
    {"trade-value", "Рынок"},    {"scales", "Весы"},          {"handshake", "Сделка"},     {"guild", "Гильдия"},
    {"hammer", "Ремесло"},       {"pickaxe", "Рудник"},       {"factory", "Мастерская"},   {"build", "Стройка"},
    {"grain", "Зерно"},          {"wood", "Лес"},             {"stone", "Камень"},         {"iron", "Железо"},
    {"gem", "Самоцветы"},        {"resource", "Ресурс"},      {"army", "Войско"},          {"sword", "Меч"},
    {"swords", "Арена"},         {"shield", "Защита"},        {"bow", "Стрельбище"},       {"horse", "Конюшня"},
    {"anchor", "Гавань"},        {"fleet", "Верфь"},          {"banner", "Знамя"},         {"flag", "Флаг"},
    {"book", "Библиотека"},      {"scroll", "Архив"},         {"quill", "Писцы"},          {"research", "Лаборатория"},
    {"tech", "Наука"},           {"staff", "Посох"},          {"wand", "Магия"},           {"sparkles", "Чудо"},
    {"religion", "Храм"},        {"culture", "Культура"},     {"population", "Население"}, {"heart", "Лечебница"},
    {"b-military", "Военная"},   {"b-economic", "Экономическая"}, {"b-industrial", "Промышленная"}, {"b-residential", "Жилая"},
    {"b-religious", "Религиозная"}, {"b-cult", "Культовая"},  {"sun", "Святилище"},       {"moon", "Обитель"},
    {"convert", "Преобразование"}, {"essence", "Эссенция"},   {"special-unit", "Особые войска"}, {"flame", "Горнило"},
    {"skull", "Склеп"},          {"eye", "Око"},              {"relic", "Сокровищница"},   {"star", "Чудо света"},
    {"shipyard", "Стапель"},     {"mercenary", "Наёмники"},   {"heal", "Целители"},        {"plague", "Чумной двор"},
    {"chest", "Хранилище"},      {"shovel", "Раскопки"},      {"talent", "Академия"},      {"lich", "Некрополь"},
};

// «Даёт за ход» (ТЗ «Виды государств», п.4, 8): ресурсы, которые достроенный уровень даёт владельцу каждый ход, —
// любые ресурсы справочника, в том числе трупы и демоническая энергия (встроенный ресурс создаётся при выборе).
void levelProduce(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const std::map<Id, double> produce = b.levels[size_t(li)].produce;
  const Id bid = b.id;
  ui::IdScope ps("produce");
  ui::caption("Даёт за ход");
  int ri = 0;
  for (auto [res, amount] : produce) {
    ui::IdScope rs(ri++);
    ui::Row row({ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    Id nr = res;
    if (w::catalogPicker("res", rules::CatalogList::Resources, nr, "", false, ro) && nr != res) {
      Id old = res;
      double v = amount;
      a.act("Ресурс уровня", [&](Tx& tx) {
        auto& pr = tx.building(bid).levels[size_t(li)].produce;
        if (pr.count(nr)) fail("Этот ресурс уровень уже даёт");
        pr.erase(old);
        pr[nr] = v;
      });
    }
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .tooltip = "Количество за ход"}))
      a.act("Ресурс за ход", [&](Tx& tx) { tx.building(bid).levels[size_t(li)].produce[res] = std::max(0.0, v); },
            {.coalesce = "bt-prod:" + std::to_string(bid) + ":" + std::to_string(li) + ":" + std::to_string(res)});
    a.markUi("bt.level." + std::to_string(li) + ".produce." + std::to_string(res));
    if (ui::iconButton("close", "Убрать ресурс")) {
      Id r = res;
      a.act("Убрать ресурс за ход", [&](Tx& tx) { tx.building(bid).levels[size_t(li)].produce.erase(r); });
    }
  }
  if (ro) return;
  // Добавить: ресурсы справочника и встроенные, которых в мире ещё нет (провизия, трупы, демоническая энергия).
  struct Pick {
    Id id = 0;
    const char* key = nullptr;
    std::string label;
    const char* icon = nullptr;
    Color color;
  };
  std::vector<Pick> cand;
  for (const CatalogItem& c : w.catalogs->resources)
    if (!produce.count(c.id)) cand.push_back({c.id, nullptr, c.name.empty() ? std::string("Без названия") : c.name, w::resourceIcon(w, c.id), w::resourceColor(w, c.id)});
  for (const schema::BuiltinResource& br : schema::kBuiltinResources)
    if (std::string_view(br.key) != schema::kResGold && !rules::resourceId(w, br.key)) cand.push_back({0, br.key, br.name, br.icon, Color::hex(br.color)});
  if (cand.empty()) return;
  std::vector<ui::Option> opts;
  for (const Pick& o : cand) opts.push_back(ui::Option{o.label, o.icon, o.color});
  int idx = -1;
  if (ui::combo("add", idx, opts, {.placeholder = "Ресурс за ход", .search = 1, .icon = "plus"}) && idx >= 0 && idx < int(cand.size())) {
    const Pick o = cand[size_t(idx)];
    a.act("Ресурс за ход", [&](Tx& tx) {
      Id r = o.id ? o.id : rules::ensureResource(tx, o.key);
      tx.building(bid).levels[size_t(li)].produce[r] = 1;
    });
  }
  a.markUi("bt.level." + std::to_string(li) + ".addproduce");
}

// «Эссенция за ход» (постройка генерации эссенции, ТЗ «Ввод новых механик»): эссенции, которые достроенный уровень
// даёт владельцу каждый ход (rules::setLevelEssence; 0 — убрать).
void levelEssence(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const std::map<Id, double> ess = b.levels[size_t(li)].essence;
  const Id bid = b.id;
  const int level = li + 1;
  const std::string mark = "bt.level." + std::to_string(li);
  ui::IdScope es("essence");
  ui::caption("Эссенция за ход");
  for (auto [e, amount] : ess) {
    ui::IdScope rs{i64(e)};
    ui::Row row({ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    Id ne = e;
    if (w::essencePicker("ess", ne, {}, ro) && ne && ne != e) {
      const Id old = e;
      const double v = amount;
      a.act("Эссенция уровня", [&](Tx& tx) {
        if (tx.w().building(bid)->levels[size_t(li)].essence.count(ne)) fail("Эту эссенцию уровень уже даёт");
        rules::setLevelEssence(tx, bid, level, old, 0);
        rules::setLevelEssence(tx, bid, level, ne, v);
      });
    }
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0.001, .max = 1e9, .step = 1, .digits = 3, .tooltip = "Эссенции за ход"}))
      a.act("Эссенция за ход", [&](Tx& tx) { rules::setLevelEssence(tx, bid, level, e, v); },
            {.coalesce = "bt-ess:" + std::to_string(bid) + ":" + std::to_string(li) + ":" + std::to_string(e)});
    a.markUi(mark + ".ess." + std::to_string(e));
    if (ui::iconButton("close", "Убрать эссенцию", {.disabled = ro})) {
      const Id x = e;
      a.act("Убрать эссенцию уровня", [&](Tx& tx) { rules::setLevelEssence(tx, bid, level, x, 0); });
    }
  }
  if (ro) return;
  // Добавить: эссенции справочника, которых у уровня ещё нет.
  std::vector<const CatalogItem*> cand;
  for (const CatalogItem& c : w.catalogs->essences)
    if (!ess.count(c.id)) cand.push_back(&c);
  if (cand.empty()) {
    if (w.catalogs->essences.empty() && ui::link("Справочник эссенций", "essence")) a.openEditor("catalogs", 7);
    return;
  }
  std::vector<ui::Option> opts;
  for (const CatalogItem* c : cand)
    opts.push_back(ui::Option{c->name.empty() ? std::string_view("Без названия") : std::string_view(c->name), "essence", c->color});
  int idx = -1;
  if (ui::combo("add", idx, opts, {.placeholder = "Эссенция за ход", .search = 1, .icon = "plus"}) && idx >= 0 && idx < int(cand.size())) {
    const Id e = cand[size_t(idx)]->id;
    a.act("Эссенция за ход", [&](Tx& tx) { rules::setLevelEssence(tx, bid, level, e, 1); });
  }
  a.markUi(mark + ".addess");
}

// Цена уровня в эссенциях (ТЗ «Доработки №4», п.5, 7: соборы, постройки доступа): списывается с запасов государства
// вместе со стоимостью ресурсами (rules::setLevelEssCost; 0 — убрать).
void levelEssCost(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const std::map<Id, double> ess = b.levels[size_t(li)].essCost;
  const Id bid = b.id;
  const int level = li + 1;
  const std::string mark = "bt.level." + std::to_string(li);
  ui::IdScope es("esscost");
  if (!ess.empty() || !ro) ui::caption("Цена в эссенциях");
  for (auto [e, amount] : ess) {
    ui::IdScope rs{i64(e)};
    ui::Row row({ui::px(16), ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    const CatalogItem* c = w.essence(e);
    ui::iconColored("essence", w::essenceColor(w, e), 16);
    ui::label(c && !c->name.empty() ? c->name : std::string("Эссенция"));
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0.001, .max = 1e9, .step = 10, .digits = 3, .tooltip = "Эссенции за уровень"}))
      a.act("Цена в эссенции", [&](Tx& tx) { rules::setLevelEssCost(tx, bid, level, e, v); },
            {.coalesce = "bt-esscost:" + std::to_string(bid) + ":" + std::to_string(li) + ":" + std::to_string(e)});
    a.markUi(mark + ".esscost." + std::to_string(e));
    if (ui::iconButton("close", "Убрать эссенцию из цены", {.disabled = ro})) {
      const Id x = e;
      a.act("Убрать эссенцию из цены", [&](Tx& tx) { rules::setLevelEssCost(tx, bid, level, x, 0); });
    }
  }
  if (ro) return;
  std::vector<const CatalogItem*> cand;
  for (const CatalogItem& c : w.catalogs->essences)
    if (!ess.count(c.id)) cand.push_back(&c);
  if (cand.empty()) return;
  std::vector<ui::Option> opts;
  for (const CatalogItem* c : cand)
    opts.push_back(ui::Option{c->name.empty() ? std::string_view("Без названия") : std::string_view(c->name), "essence", c->color});
  int idx = -1;
  if (ui::combo("add", idx, opts, {.placeholder = "Эссенция в цене", .search = 1, .icon = "plus"}) && idx >= 0 && idx < int(cand.size())) {
    const Id e = cand[size_t(idx)]->id;
    a.act("Цена в эссенции", [&](Tx& tx) { rules::setLevelEssCost(tx, bid, level, e, 100); });
  }
  a.markUi(mark + ".addesscost");
}

// Верфь (ТЗ «Доработки №3», п.2): какие типы кораблей открывает уровень (rules::setLevelShips) — значки-переключатели.
void levelShips(App& a, const BN& b, int li, bool ro) {
  const u32 ships = b.levels[size_t(li)].ships;
  const Id bid = b.id;
  const int level = li + 1;
  ui::IdScope ss("ships");
  ui::caption("Открывает корабли");
  ui::HStack hs(30, ui::Align::Left, 4);
  for (int t = 0; t < int(ShipType::Count); t++) {
    ui::IdScope s(t);
    const bool on = (ships >> t) & 1u;
    const schema::EnumInfo& si = schema::shipType(ShipType(t));
    if (ui::iconButton(si.icon, std::string(si.name) + (on ? " — открыт" : " — закрыт"), {.variant = ui::Variant::Secondary, .toggled = on, .disabled = ro})) {
      const u32 nv = on ? ships & ~(1u << t) : ships | (1u << t);
      a.act(on ? "Верфь: закрыть тип кораблей" : "Верфь: открыть тип кораблей", [&](Tx& tx) { rules::setLevelShips(tx, bid, level, nv); });
    }
    a.markUi("bt.level." + std::to_string(li) + ".ship." + std::to_string(t));
  }
}

// ---------------------------------------------------------------- окно модификаторов уровня (ТЗ «Доработки», п.8)
// Все модификаторы мира, которые может давать постройка: виды «Везде», «Для провинций», «Глобальный» (не для армий
// и героев, не автоматические), по видам, с поиском. Отметка — модификатор в уровне: добавляется и убирается сразу
// (Ctrl+Z отменяет). «Новый модификатор» создаёт запись вида «Для провинций» и сразу добавляет её в уровень.
const char* modIconOf(const Modifier& m) { return m.icon.empty() || !gfx::hasIcon(m.icon) ? "sparkles" : m.icon.c_str(); }

std::string effectsLine(const Modifier& m) {
  std::vector<std::string> p;
  for (int f = 0; f < kFxCount; f++)
    if (m.has(Fx(f)) && m.fx[size_t(f)] != 0) p.push_back(w::effectText(Fx(f), m.fx[size_t(f)]));
  return join(p, "; ");
}

struct LevelMods : Dialog {
  Id bid = 0;
  int li = 0;                  // уровень с 0
  std::string query, name;
  Id made = 0;                 // созданный в окне модификатор: подсветка и переход в редактор
  bool scrollMade = false;     // показать его строку в списке (после создания)
  std::optional<RectF> madeRow;   // строка созданного модификатора в этом кадре
  const char* id() const override { return "bt.mods"; }
  Style style(App& a) override {
    const Building* b = a.world().building(bid);
    return {"Модификаторы · " + (b ? bname(*b) : std::string("постройка")) + " · " + roman(li + 1), "sparkles", ui::Tone::Accent, 620};
  }
  bool draw(App& a) override;

  // Копия модификатора на кадр (действие посреди кадра заменяет мир — указатели на записи не держим).
  struct Item {
    Id id = 0;
    std::string name, icon, fx;
    Color color;
    ModKind kind = ModKind::Any;
    bool in = false, fits = true;
  };
  bool row(App& a, const Item& it, bool ro);   // false — закрыть окно (переход в редактор)
  void toggle(App& a, Id mid, bool on);
};

void LevelMods::toggle(App& a, Id mid, bool on) {
  const Id b = bid;
  const int l = li;
  a.act(on ? "Модификатор в уровне постройки" : "Убрать модификатор из уровня", [&](Tx& tx) {
    const Building* x = tx.w().building(b);
    if (!x || l >= int(x->levels.size())) fail("Уровень постройки не найден");
    auto& mods = tx.building(b).levels[size_t(l)].modifiers;
    const bool has = std::find(mods.begin(), mods.end(), mid) != mods.end();
    if (on && !has) mods.push_back(mid);
    if (!on && has) mods.erase(std::remove(mods.begin(), mods.end(), mid), mods.end());
  });
}

bool LevelMods::row(App& a, const Item& it, bool ro) {
  const ui::Theme& th = ui::theme();
  ui::IdScope s{i64(it.id)};
  const RectF r = ui::next(42);
  // Строка — цель для мыши (Enter в окне остаётся за «Готово»).
  const ui::Interaction in = ui::interact(ui::id("row"), r, ui::IfAllowOverlap);
  const bool fresh = made == it.id;
  if (it.in || fresh) ui::draw::rect(r, th.accent.alpha(fresh ? 0.15f : 0.07f), 8);
  if (in.hovered && !ro) {
    ui::draw::rect(r, th.hover, 8);
    ui::setCursor(platform::Cursor::Hand);
  }
  // Отметка «в уровне»
  const RectF cb{r.x + 10, r.cy() - 9, 18, 18};
  if (it.in) {
    ui::draw::rect(cb, th.accent, 5);
    ui::draw::icon("check", cb.inset(2), th.onAccent);
  } else {
    ui::draw::rectStroke(cb, in.hovered && !ro ? th.accent : th.borderStrong, 5, 1.4f);
  }
  // Значок, название, эффекты
  ui::draw::icon(it.icon, RectF{r.x + 38, r.cy() - 9, 18, 18}, it.color);
  const float tx = r.x + 66, right = r.right() - 46;
  ui::draw::text(it.name.empty() ? std::string("Модификатор") : it.name, RectF{tx, r.y + 4, right - tx, 18}, ui::Font::Strong, th.text);
  std::string sub = it.fits ? (it.fx.empty() ? std::string("Без эффектов") : it.fx) : std::string("Не для построек: ") + schema::modKind(it.kind).name;
  ui::draw::text(sub, RectF{tx, r.y + 22, right - tx, 16}, ui::Font::Small, it.fits ? th.textMuted : th.warning);
  a.markUi("bt.mods.row." + std::to_string(it.id), r);
  // Переход в редактор модификаторов
  ui::at(RectF{r.right() - 38, r.cy() - 15, 30, 30});
  const bool open = ui::iconButton("edit", "Открыть в редакторе модификаторов");
  a.markUi("bt.mods.open." + std::to_string(it.id));
  if (fresh) madeRow = r;
  if (open) {
    a.openEditor("modifiers", it.id);
    return false;
  }
  if (in.clicked && !ro) toggle(a, it.id, !it.in);
  return true;
}

bool LevelMods::draw(App& a) {
  const World& w = a.world();
  const Building* b = w.building(bid);
  if (!b || li < 0 || li >= int(b->levels.size())) return false;
  const bool ro = a.readOnly();
  const std::vector<Id> inLevel = b->levels[size_t(li)].modifiers;
  int n = 0;
  for (Id mid : inLevel) n += w.modifier(mid) != nullptr;
  {
    ui::Row r({ui::fr(1), ui::px(130)}, 30, 10);
    ui::searchField("q", query, "Найти модификатор");
    a.markUi("bt.mods.search");
    ui::tag("В уровне: " + std::to_string(n), n ? ui::Tone::Accent : ui::Tone::Neutral, "check");
  }
  // Модификаторы по видам: «Везде», «Для провинций», «Глобальный»; модификатор другого вида — только если он уже в
  // уровне (его можно убрать).
  std::vector<Item> groups[int(ModKind::Count)];
  w.modifiers.each([&](const Modifier& m) {
    const bool in = std::find(inLevel.begin(), inLevel.end(), m.id) != inLevel.end();
    const bool fits = w::modifierFits(m, w::ModScope::Any);
    if (!fits && !in) return;
    std::string fx = effectsLine(m);
    if (!query.empty() && !utf8::matches(m.name, query) && !utf8::matches(m.desc, query) && !utf8::matches(fx, query)) return;
    const int k = clamp(int(m.kind), 0, int(ModKind::Count) - 1);
    groups[k].push_back(Item{m.id, m.name, modIconOf(m), std::move(fx), m.color, ModKind(k), in, fits});
  });
  bool keep = true;
  {
    const float h = std::min(440.f, std::max(220.f, ui::viewport().h - 360));
    const RectF lr = ui::avail();
    a.markUi("bt.mods.list", RectF{lr.x, lr.y, lr.w, h});
    ui::Scroll sc("list", h);
    madeRow.reset();
    int shown = 0;
    for (int k = 0; k < int(ModKind::Count); k++) {
      auto& list = groups[k];
      if (list.empty()) continue;
      std::stable_sort(list.begin(), list.end(), [](const Item& x, const Item& y) { return compareRu(x.name, y.name) < 0; });
      ui::IdScope ks(k);
      {
        ui::HStack hs(26, ui::Align::Left, 8);
        ui::icon(schema::kModKinds[k].icon, ui::Ink::Muted, 16);
        ui::label(schema::kModKinds[k].name, {.font = ui::Font::Strong});
        ui::badge(std::to_string(list.size()), ui::Tone::Neutral);
      }
      a.markUi("bt.mods.kind." + std::to_string(k));
      for (const Item& it : list) {
        if (!keep) break;
        keep = row(a, it, ro);
        shown++;
      }
    }
    if (shown == 0) ui::emptyState("search", w.modifiers.empty() ? "Модификаторов пока нет." : "Ничего не найдено.");
    // Созданный модификатор — строкой целиком в видимой части списка.
    if (scrollMade && madeRow) {
      const float top = lr.y + 4, bottom = lr.y + h - 4;
      if (madeRow->bottom() > bottom) sc.scrollTo(sc.offset() + madeRow->bottom() - bottom);
      else if (madeRow->y < top) sc.scrollTo(std::max(0.f, sc.offset() - (top - madeRow->y)));
      scrollMade = false;
    }
  }
  if (!keep) return false;
  // Новый модификатор: запись вида «Для провинций», сразу в уровне; рядом — переход в редактор модификаторов.
  if (!ro) {
    ui::Row r({ui::fr(1), ui::px(184), ui::px(34)}, 30, 8);
    // Enter в поле названия — создать (а не «Готово»).
    const bool enter = ui::keyboardFocus() == ui::id("newname") && ui::keyPressed(Key::Enter);
    if (enter) ui::consumeKey(Key::Enter);
    ui::textField("newname", name, {.placeholder = "Название нового модификатора", .live = true, .maxLength = 80});
    a.markUi("bt.mods.name");
    if (ui::button("Новый модификатор", {.icon = "plus", .fill = true}) || enter) {
      const Id b0 = bid;
      const int l = li;
      const std::string nm = trim(name);
      Id nid = 0;
      if (a.act("Новый модификатор уровня", [&](Tx& tx) {
            if (!tx.w().building(b0) || l >= int(tx.w().building(b0)->levels.size())) fail("Уровень постройки не найден");
            nid = rules::createModifier(tx, nm);
            tx.modifier(nid).kind = ModKind::Province;
            tx.building(b0).levels[size_t(l)].modifiers.push_back(nid);
          })) {
        made = nid;
        scrollMade = true;
        name.clear();
        query.clear();
      }
    }
    a.markUi("bt.mods.create");
    const Modifier* fresh = w.modifier(made);
    if (ui::iconButton("sparkles", fresh ? "Открыть «" + (fresh->name.empty() ? std::string("Модификатор") : fresh->name) + "» в редакторе модификаторов"
                                         : std::string("Открыть в редакторе модификаторов"))) {
      a.openEditor("modifiers", fresh ? made : 0);
      return false;
    }
    a.markUi("bt.mods.editor");
  }
  ui::ModalFooter f;
  if (ui::button("Готово", {.variant = ui::Variant::Primary, .isDefault = true})) return false;
  a.markUi("bt.mods.done");
  return true;
}

void levelCard(App& a, const BN& b, int li, bool ro) {
  const World& w = a.world();
  const BuildingLevel& L = b.levels[size_t(li)];
  ui::IdScope s(li);
  std::string title = "Уровень " + roman(li + 1);
  ui::Card card({.pad = 12, .icon = "slots", .title = title});
  ui::Disabled dis(ro);
  {
    ui::prop("Срок", "hourglass", 0.4f);
    int turns = std::max(1, L.turns);
    if (ui::numberField("turns", turns, {.min = 1, .max = 999, .unit = "ход|хода|ходов", .steppers = true}))
      a.act("Срок строительства", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].turns = std::max(1, turns); },
            {.coalesce = "bt-turns:" + std::to_string(b.id) + ":" + std::to_string(li)});
    a.markUi("bt.level." + std::to_string(li) + ".turns");
  }
  ui::caption("Стоимость");
  int ri = 0;
  for (auto [res, amount] : L.cost) {
    ui::IdScope rs(ri++);
    ui::Row row({ui::fr(1), ui::px(96), ui::px(30)}, 30, 6);
    Id nr = res;
    if (w::catalogPicker("res", rules::CatalogList::Resources, nr, "", false, ro) && nr != res) {
      Id old = res;
      double v = amount;
      a.act("Ресурс стоимости", [&](Tx& tx) {
        auto& cost = tx.building(b.id).levels[size_t(li)].cost;
        if (cost.count(nr)) fail("Этот ресурс уже есть в стоимости уровня");
        cost.erase(old);
        cost[nr] = v;
      });
    }
    double v = amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 10, .digits = 3, .unit = res == kGold ? "тыс." : nullptr}))
      a.act("Стоимость уровня", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].cost[res] = std::max(0.0, v); },
            {.coalesce = "bt-cost:" + std::to_string(b.id) + ":" + std::to_string(li) + ":" + std::to_string(res)});
    a.markUi("bt.level." + std::to_string(li) + ".cost." + std::to_string(res));
    if (ui::iconButton("close", "Убрать ресурс")) {
      Id r = res;
      a.act("Убрать ресурс из стоимости", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].cost.erase(r); });
    }
  }
  {
    // Следующий ресурс, которого ещё нет в стоимости (золото — первым).
    Id next = 0;
    if (!L.cost.count(kGold)) next = kGold;
    else
      for (const CatalogItem& c : w.catalogs->resources)
        if (!L.cost.count(c.id)) {
          next = c.id;
          break;
        }
    if (ui::button("Ресурс", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small, .disabled = next == 0,
                              .tooltip = next ? std::string_view("Добавить ресурс в стоимость уровня") : std::string_view("Все ресурсы уже в стоимости")})) {
      a.act("Ресурс в стоимости", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].cost[next] = 100; });
    }
    a.markUi("bt.level." + std::to_string(li) + ".addcost");
  }
  levelEssCost(a, b, li, ro);
  if (b.shipyard) levelShips(a, b, li, ro);
  levelProduce(a, b, li, ro);
  if (b.essenceGen) levelEssence(a, b, li, ro);
  // Модификаторы уровня: фишки (щелчок — редактор модификаторов, крестик — убрать) и окно выбора.
  ui::caption("Модификаторы уровня");
  std::vector<Id> mods = L.modifiers;
  if (tree::modifierList("mods", mods, ro, false)) a.act("Модификаторы уровня", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].modifiers = mods; });
  if (!ro) {
    if (ui::button("Выбрать модификаторы…", {.variant = ui::Variant::Ghost, .icon = "sparkles", .size = ui::Size::Small}))
      a.openDialog(levelModsDialog(b.id, li + 1));
    a.markUi("bt.level." + std::to_string(li) + ".mods");
  }
  bld::levelEffects(w, L, true);
  std::string desc = L.desc;
  if (ui::textArea("desc", desc, 48, {.placeholder = "Что даёт уровень", .maxLength = 400}) && desc != L.desc)
    a.act("Описание уровня", [&](Tx& tx) { tx.building(b.id).levels[size_t(li)].desc = desc; });
  // Удалить можно только последний уровень, если он нигде не построен.
  if (li == int(b.levels.size()) - 1 && b.levels.size() > 1) {
    ui::HStack hs(28, ui::Align::Right, 4);
    if (ui::iconButton("trash", "Удалить уровень " + roman(li + 1), {.size = ui::Size::Small, .tone = ui::Tone::Danger})) {
      Id bid = b.id;
      int n = int(b.levels.size());
      a.confirm("Удалить уровень " + roman(n) + "?", "Срок, стоимость и модификаторы уровня будут удалены. Действие можно отменить Ctrl+Z.", "Удалить", true,
                [bid, n](App& x) {
                  // Правило проверяет провинции и опускает требования других построек к удалённому уровню.
                  std::vector<Id> lowered;
                  bool ok = x.act("Удалить уровень постройки", [&](Tx& tx) {
                    if (int(tx.w().building(bid)->levels.size()) == n) lowered = rules::removeLastBuildingLevel(tx, bid);
                  });
                  if (ok && !lowered.empty()) {
                    std::vector<std::string> names;
                    for (Id d : lowered)
                      if (const Building* db = x.world().building(d)) names.push_back("«" + (db->name.empty() ? std::string("Без названия") : db->name) + "»");
                    x.toast("Требование к уровню " + roman(n) + " заменено на уровень " + roman(n - 1) + ": " + join(names, ", "), ToastKind::Info,
                            "building");
                  }
                });
    }
    a.markUi("bt.level." + std::to_string(li) + ".delete");
  }
}

// Культовая постройка (ТЗ «Доработки», п.2): одна на всю карту (общее дерево) или на государство (уникальная) и где
// она уже стоит.
void cultInfo(App& a, const BN& b) {
  const World& w = a.world();
  ui::IdScope s("cult");
  {
    ui::HStack hs(24, ui::Align::Left, 6);
    ui::iconColored("b-cult", bld::catColor(BuildingCat::Cult), 16,
                    b.owner ? "Культовая постройка государства: в его провинциях — не больше одной" : "Культовая постройка общего дерева: на всей карте — не больше одной");
    ui::label(b.owner ? "Одна на государство" : "Одна на всю карту", {.font = ui::Font::Strong});
  }
  a.markUi("bt.side.cult");
  if (const Province* p = w.province(b.cultAt)) {
    ui::HStack hs(26, ui::Align::Left, 6);
    ui::icon("map-pin", ui::Ink::Accent, 16, "Уже стоит");
    w::provinceChip(p->id);
    if (!b.owner && p->owner) w::factionChip(p->owner);
    a.markUi("bt.side.cultAt");
  }
}

// Требования к постройкам (ТЗ «Доработки», п.4 и 7): общая постройка требует общие, уникальная — общие и своего
// государства; без циклов (rules::canRequireBuilding — недоступные варианты с причиной).
void reqSection(App& a, Ed& ed, const std::vector<BN>& bs, const BN& b, bool ro) {
  const World& w = a.world();
  const Color none(0, 0, 0, 0);
  ui::Section s("Требует построек", "link", {.badge = b.reqs.empty() ? std::string() : std::to_string(b.reqs.size())});
  if (!s) return;
  if (b.reqs.empty()) ui::label("Можно строить без других построек.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  for (const BuildingReq& rq : b.reqs) {
    const Building* rb = w.building(rq.building);
    if (!rb) continue;
    const Id rid = rq.building, rowner = rb->owner;
    const std::string rname = bname(*rb);
    const int maxL = std::max<int>(1, int(rb->levels.size()));
    ui::IdScope s2{i64(rid)};
    ui::Row row({ui::fr(1), ui::px(110), ui::px(30)}, 30, 6);
    if (ui::chip(rname, {.icon = bld::iconOf(*rb), .color = bld::catColor(rb->cat), .clickable = true, .tooltip = "Показать"}) == ui::ChipAction::Click) {
      if (find(bs, rid)) {
        ed.sel = rid;
        ed.revealSel = true;
      } else {
        openBuildingTree(a, rowner, rid);
      }
    }
    int lvl = rq.level;
    {
      ui::Disabled dis(ro || maxL <= 1);
      if (ui::numberField("lvl", lvl, {.min = 1, .max = double(maxL), .label = "ур.", .steppers = maxL > 1})) setReqLevel(a, b.id, rid, clamp(lvl, 1, maxL));
      a.markUi("bt.req." + std::to_string(rid) + ".level");
    }
    if (ui::iconButton("close", "Убрать требование", {.disabled = ro})) removeReq(a, {rid, b.id});
    a.markUi("bt.req." + std::to_string(rid) + ".remove");
  }
  if (ro) return;
  // Добавить требование: общее дерево и уникальные этого государства; создающие цикл — недоступны.
  struct Cand {
    Id id = 0;
    std::string label;
    const char* icon = nullptr;
    BuildingCat cat = BuildingCat::Economic;
    bool ok = true;
  };
  std::vector<Cand> cand;
  w.buildings.each([&](const Building& x) {
    if (x.id == b.id || (x.owner != 0 && x.owner != b.owner)) return;
    for (const BuildingReq& r : b.reqs)
      if (r.building == x.id) return;
    cand.push_back(Cand{x.id, bname(x), bld::iconOf(x), x.cat, rules::canRequireBuilding(w, b.id, x.id)});
  });
  if (cand.empty()) return;
  std::stable_sort(cand.begin(), cand.end(), [](const Cand& x, const Cand& y) {
    if (x.cat != y.cat) return x.cat < y.cat;
    return compareRu(x.label, y.label) < 0;
  });
  std::vector<ui::Option> opts;
  for (const Cand& c : cand) opts.push_back(ui::Option{c.label, c.icon, none, c.ok ? std::string_view(bld::catName(c.cat)) : std::string_view("цикл"), !c.ok});
  int idx = -1;
  if (ui::combo("addreq", idx, std::span<const ui::Option>(opts), {.placeholder = "Добавить требование", .search = 1, .icon = "plus"}) && idx >= 0 &&
      idx < int(cand.size()))
    addReq(a, b.id, cand[size_t(idx)].id);
  a.markUi("bt.side.addreq");
}

// Требуемые технологии (ТЗ «Доработки», п.4 и 7): без них постройку не начать. Общей постройке — общие технологии,
// уникальной — общие и своего государства (rules::canRequireTech).
void techSection(App& a, const BN& b, bool ro) {
  const World& w = a.world();
  const Color none(0, 0, 0, 0);
  bool any = false;
  for (Id t : b.techs) any = any || w.tech(t) != nullptr;
  ui::Section s("Требуемые технологии", "tech-tree", {.badge = any ? std::to_string(b.techs.size()) : std::string()});
  a.markUi("bt.side.techs");
  if (!s) return;
  const Id bid = b.id;
  if (any) {
    ui::IdScope ts("techs");
    tree::ChipFlow flow;
    for (Id tid : b.techs) {
      const Tech* t = w.tech(tid);
      if (!t) continue;
      const Id tf = t->faction;
      ui::IdScope s2{i64(tid)};
      ui::ChipOpt co;
      co.icon = tf ? "crown" : "globe";
      co.tone = ui::Tone::Info;
      co.removable = !ro;
      co.clickable = true;
      const std::string tip = (tf ? w.factionName(tf) : std::string("Общее дерево")) + " — показать в дереве технологий";
      co.tooltip = tip;
      ui::ChipAction act = tree::chip(tname(*t), co);
      a.markUi("bt.tech." + std::to_string(tid));
      if (act == ui::ChipAction::Click) openTechTree(a, tf, tid, tf ? 0 : b.owner);
      else if (act == ui::ChipAction::Remove) a.act("Убрать требуемую технологию", [&](Tx& tx) { rules::setBuildingTech(tx, bid, tid, false); });
    }
  }
  if (ro) return;
  struct Cand {
    Id id = 0, faction = 0;
    std::string label;
  };
  std::vector<Cand> cand;
  w.techs.each([&](const Tech& t) {
    if (std::find(b.techs.begin(), b.techs.end(), t.id) != b.techs.end()) return;
    if (rules::canRequireTech(w, bid, t.id)) cand.push_back(Cand{t.id, t.faction, tname(t)});
  });
  if (cand.empty()) {
    if (!any && ui::link(b.owner ? "Дерево технологий государства" : "Общее дерево технологий", "tech-tree")) openTechTree(a, b.owner, 0, 0);
    a.markUi("bt.side.techtree");
    return;
  }
  // Общие — первыми, затем своего государства.
  std::stable_sort(cand.begin(), cand.end(), [](const Cand& x, const Cand& y) {
    if ((x.faction == 0) != (y.faction == 0)) return x.faction == 0;
    return compareRu(x.label, y.label) < 0;
  });
  std::vector<ui::Option> opts;
  for (const Cand& c : cand) opts.push_back(ui::Option{c.label, c.faction ? "crown" : "globe", none, c.faction ? std::string_view("своя") : std::string_view("общая")});
  int idx = -1;
  if (ui::combo("addtech", idx, std::span<const ui::Option>(opts), {.placeholder = "Добавить технологию", .search = 1, .icon = "plus"}) && idx >= 0 &&
      idx < int(cand.size())) {
    const Id tid = cand[size_t(idx)].id;
    a.act("Требуемая технология", [&](Tx& tx) { rules::setBuildingTech(tx, bid, tid, true); });
  }
  a.markUi("bt.side.addtech");
}

// Рецепт постройки преобразования (ТЗ «Доработки», п.3): до трёх разных ресурсов на входе (выбор через категорию),
// ресурс на выходе, срок цикла. Проверки — rules::setRecipe.
void recipeEditor(App& a, const BN& b, bool ro) {
  const World& w = a.world();
  const Recipe rc = b.recipe;
  const Id bid = b.id;
  ui::IdScope rs("recipe");
  auto set = [&](std::string_view label, const Recipe& nr, std::string coalesce = {}) {
    a.act(label, [&](Tx& tx) { rules::setRecipe(tx, bid, nr); }, {.coalesce = std::move(coalesce)});
  };
  ui::caption("На входе");
  for (size_t i = 0; i < rc.in.size(); i++) {
    ui::IdScope s{i64(rc.in[i].res) * 4 + i64(i)};
    Id res = rc.in[i].res;
    if (w::resourceByGroup("res", res, ro) && res && res != rc.in[i].res) {
      Recipe nr = rc;
      nr.in[i].res = res;
      set("Ресурс на входе", nr);
    }
    a.markUi("bt.recipe.in." + std::to_string(i));
    ui::Row row({ui::fr(1), ui::px(30)}, 30, 6);
    double v = rc.in[i].amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .label = "×", .disabled = ro, .tooltip = "Количество на входе за цикл"})) {
      Recipe nr = rc;
      nr.in[i].amount = v;
      set("Количество на входе", nr, "bt-rin:" + std::to_string(bid) + ":" + std::to_string(i));
    }
    a.markUi("bt.recipe.inamount." + std::to_string(i));
    if (ui::iconButton("close", "Убрать ресурс на входе", {.disabled = ro})) {
      Recipe nr = rc;
      nr.in.erase(nr.in.begin() + long(i));
      set("Убрать ресурс на входе", nr);
    }
    a.markUi("bt.recipe.inremove." + std::to_string(i));
  }
  if (rc.in.size() < 3) {
    // Следующий ресурс справочника, которого ещё нет в рецепте (не золото).
    Id next = 0;
    for (const CatalogItem& c : w.catalogs->resources) {
      if (c.id == kGold || c.id == rc.out.res) continue;
      if (std::none_of(rc.in.begin(), rc.in.end(), [&](const ResAmount& x) { return x.res == c.id; })) {
        next = c.id;
        break;
      }
    }
    if (ui::button("Ресурс на входе", {.variant = ui::Variant::Ghost, .icon = "plus", .size = ui::Size::Small, .disabled = ro || !next,
                                       .tooltip = "До трёх разных ресурсов на входе"})) {
      Recipe nr = rc;
      nr.in.push_back(ResAmount{next, 1});
      set("Ресурс на входе", nr);
    }
    a.markUi("bt.recipe.addin");
  }
  ui::caption("На выходе");
  {
    ui::IdScope os("out");
    Id res = rc.out.res;
    if (w::resourceByGroup("res", res, ro) && res && res != rc.out.res) {
      Recipe nr = rc;
      nr.out.res = res;
      if (!(nr.out.amount > 0)) nr.out.amount = 1;
      set("Ресурс на выходе", nr);
    }
    a.markUi("bt.recipe.out");
    double v = rc.out.amount;
    if (ui::numberField("amount", v, {.min = 0, .max = 1e9, .step = 1, .digits = 3, .label = "×", .disabled = ro || !rc.out.res, .tooltip = "Количество на выходе за цикл"})) {
      Recipe nr = rc;
      nr.out.amount = v;
      set("Количество на выходе", nr, "bt-rout:" + std::to_string(bid));
    }
    a.markUi("bt.recipe.outamount");
  }
  ui::prop("Цикл", "hourglass", 0.4f);
  int turns = std::max(1, rc.turns);
  if (ui::numberField("turns", turns, {.min = 1, .max = 1000, .unit = "ход|хода|ходов", .steppers = true, .disabled = ro, .tooltip = "Ходов на один цикл преобразования"})) {
    Recipe nr = rc;
    nr.turns = turns;
    set("Срок преобразования", nr, "bt-rturns:" + std::to_string(bid));
  }
  a.markUi("bt.recipe.turns");
  if (!bld::recipeValid(w, rc)) ui::label("Рецепт не задан", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
}

// Особые отряды постройки доступа (ТЗ «Ввод новых механик», п.4–5): фишки (щелчок — справочник «Особые отряды»,
// крестик — убрать) и выбор из справочника.
void specialsEditor(App& a, const BN& b, bool ro) {
  const World& w = a.world();
  const Id bid = b.id;
  ui::IdScope ss("specials");
  bool any = false;
  for (Id sid : b.specials) any = any || w.special(sid) != nullptr;
  if (any) {
    tree::ChipFlow flow;
    for (Id sid : b.specials) {
      const SpecialUnit* su = w.special(sid);
      if (!su) continue;
      ui::IdScope s2{i64(sid)};
      std::string tip = std::string(schema::unitType(su->type).name) + (su->desc.empty() ? std::string() : "\n" + su->desc) + "\nОткрыть в справочнике";
      ui::ChipAction act = tree::chip(su->name.empty() ? std::string("Без названия") : su->name,
                                      {.icon = schema::unitType(su->type).icon, .tone = ui::Tone::Accent, .removable = !ro, .clickable = true, .tooltip = tip});
      a.markUi("bt.special." + std::to_string(sid));
      if (act == ui::ChipAction::Click) a.openEditor("catalogs", 9);
      else if (act == ui::ChipAction::Remove) a.act("Убрать особый отряд", [&](Tx& tx) { rules::setBuildingSpecial(tx, bid, sid, false); });
    }
  }
  if (ro) return;
  std::vector<const SpecialUnit*> cand;
  for (const SpecialUnit& su : w.catalogs->specials)
    if (std::find(b.specials.begin(), b.specials.end(), su.id) == b.specials.end()) cand.push_back(&su);
  if (cand.empty()) {
    if (w.catalogs->specials.empty() && ui::link("Справочник особых отрядов", "special-unit")) a.openEditor("catalogs", 9);
    return;
  }
  std::vector<ui::Option> opts;
  for (const SpecialUnit* su : cand)
    opts.push_back(ui::Option{su->name.empty() ? std::string_view("Без названия") : std::string_view(su->name), schema::unitType(su->type).icon, Color(0, 0, 0, 0),
                              schema::unitType(su->type).name});
  int idx = -1;
  if (ui::combo("add", idx, std::span<const ui::Option>(opts), {.placeholder = "Особый отряд", .search = 1, .icon = "plus"}) && idx >= 0 && idx < int(cand.size())) {
    const Id sid = cand[size_t(idx)]->id;
    a.act("Особый отряд постройки", [&](Tx& tx) { rules::setBuildingSpecial(tx, bid, sid, true); });
  }
  a.markUi("bt.addspecial");
}

// Особые возможности постройки: преобразование ресурсов, генерация эссенции, доступ к особым отрядам
// (rules::setBuildingRole; выключение снимает их данные — Ctrl+Z возвращает).
void rolesSection(App& a, const BN& b, bool ro) {
  const int on = int(b.convert) + int(b.essenceGen) + int(b.specialAccess) + int(b.relicStore) + int(b.healing) + int(b.plague) + int(b.shipyard) +
                 int(b.mercenary) + int(b.coastal);
  ui::Section s("Особые возможности", "sparkles", {.badge = on ? std::to_string(on) : std::string()});
  a.markUi("bt.side.roles");
  if (!s) return;
  const Id bid = b.id;
  auto role = [&](rules::BuildingRole r, const char* icon, const char* label, bool cur, const char* mark, const char* tip) {
    ui::IdScope rs(mark);
    {
      ui::Row row({ui::px(20), ui::fr(1)}, 30, 8);
      ui::iconColored(icon, cur ? roleColor(r) : ui::theme().textMuted, 18, tip);
      bool v = cur;
      if (ui::toggle(label, v, ro) && v != cur)
        a.act(std::string(v ? "Включить: " : "Выключить: ") + utf8::lower(label), [&](Tx& tx) { rules::setBuildingRole(tx, bid, r, v); });
      a.markUi(mark);
    }
  };
  // Возможности ТЗ «Доработки №1–3» (rules::setBuildingFlag; выключение снимает их данные — Ctrl+Z возвращает).
  auto flag = [&](rules::BuildingFlag f, const char* icon, Color col, const char* label, bool cur, const char* mark, const char* tip) {
    ui::IdScope rs(mark);
    ui::Row row({ui::px(20), ui::fr(1)}, 30, 8);
    ui::iconColored(icon, cur ? col : ui::theme().textMuted, 18, tip);
    bool v = cur;
    if (ui::toggle(label, v, ro) && v != cur)
      a.act(std::string(v ? "Включить: " : "Выключить: ") + utf8::lower(label), [&](Tx& tx) { rules::setBuildingFlag(tx, bid, f, v); });
    a.markUi(mark);
  };
  role(rules::BuildingRole::Convert, "convert", "Преобразование ресурсов", b.convert, "bt.role.convert",
       "До трёх ресурсов на входе превращаются в один на выходе за цикл; в провинции — «Работает» или «Простаивает»");
  if (b.convert) {
    ui::Indent in(28);
    recipeEditor(a, b, ro);
  }
  role(rules::BuildingRole::Essence, "essence", "Генерация эссенции", b.essenceGen, "bt.role.essence", "Эссенция за ход — у каждого уровня");
  role(rules::BuildingRole::Special, "special-unit", "Доступ к особым отрядам", b.specialAccess, "bt.role.special",
       "Пока постройка достроена, государство может нанимать особые отряды");
  if (b.specialAccess) {
    ui::Indent in(28);
    specialsEditor(a, b, ro);
  }
  const ui::Theme& th = ui::theme();
  flag(rules::BuildingFlag::RelicStore, "relic", Color::hex(kRelicInk), "Хранилище реликвий", b.relicStore, "bt.flag.relics",
       "В постройку можно положить реликвии государства и его героев");
  flag(rules::BuildingFlag::Healing, "heal", th.success, "Здание целительства", b.healing, "bt.flag.healing",
       "«Вылечить провинцию» от чумы за 500 любой эссенции");
  flag(rules::BuildingFlag::Plague, "plague", Color::hex(kPlagueInk), "Здание чумы", b.plague, "bt.flag.plague",
       "Чума в провинции даёт прирост населения +2,5 %; «Заразить чумой» за 2500 эссенции чумы");
  flag(rules::BuildingFlag::Shipyard, "shipyard", th.info, "Верфь", b.shipyard, "bt.flag.shipyard", "Уровни открывают найм типов кораблей");
  flag(rules::BuildingFlag::Mercenary, "mercenary", th.accent, "Гильдия наёмников", b.mercenary, "bt.flag.merc",
       "Каждая такая постройка поднимает лимит наёмников государства");
  flag(rules::BuildingFlag::Coastal, "anchor", th.textDim, "Только в приморской провинции", b.coastal, "bt.flag.coastal",
       "Строится только в провинции, граничащей с морем");
}

// Требования «на государство» (соборы, ТЗ «Доработки №4», п.7): на каждую новую такую постройку в провинциях
// государства должно быть достроено ещё per построек req (rules::setStateReq).
void stateReqSection(App& a, const BN& b, bool ro) {
  const World& w = a.world();
  ui::Section s("Требует в государстве", "crown",
                {.defaultOpen = !b.stateReqs.empty(), .badge = b.stateReqs.empty() ? std::string() : std::to_string(b.stateReqs.size())});
  a.markUi("bt.side.statereqs");
  if (!s) return;
  const Id bid = b.id;
  for (const StateReq& sr : b.stateReqs) {
    const Building* rb = w.building(sr.building);
    if (!rb) continue;
    const Id rid = sr.building;
    ui::IdScope s2{i64(rid)};
    ui::Row row({ui::px(76), ui::fr(1), ui::px(30)}, 30, 6);
    int per = sr.per;
    if (ui::numberField("per", per, {.min = 1, .max = 100, .icon = "hash", .disabled = ro,
                                     .tooltip = "На каждую новую — столько достроенных построек в провинциях государства"}))
      a.act("Требование на государство", [&](Tx& tx) { rules::setStateReq(tx, bid, rid, clamp(per, 1, 100)); },
            {.coalesce = "bt-sreq:" + std::to_string(bid) + ":" + std::to_string(rid)});
    a.markUi("bt.sreq." + std::to_string(rid) + ".per");
    if (ui::chip(bname(*rb), {.icon = bld::iconOf(*rb), .color = bld::catColor(rb->cat), .clickable = true, .tooltip = "Показать"}) == ui::ChipAction::Click)
      openBuildingTree(a, rb->owner, rid);
    if (ui::iconButton("close", "Убрать требование", {.disabled = ro})) a.act("Убрать требование на государство", [&](Tx& tx) { rules::setStateReq(tx, bid, rid, 0); });
    a.markUi("bt.sreq." + std::to_string(rid) + ".remove");
  }
  if (ro) return;
  struct Cand {
    Id id = 0;
    std::string label;
    const char* icon = nullptr;
    BuildingCat cat = BuildingCat::Economic;
  };
  std::vector<Cand> cand;
  w.buildings.each([&](const Building& x) {
    if (x.id == bid || (x.owner != 0 && x.owner != b.owner)) return;
    for (const StateReq& r : b.stateReqs)
      if (r.building == x.id) return;
    cand.push_back(Cand{x.id, bname(x), bld::iconOf(x), x.cat});
  });
  if (cand.empty()) return;
  std::stable_sort(cand.begin(), cand.end(), [](const Cand& x, const Cand& y) {
    if (x.cat != y.cat) return x.cat < y.cat;
    return compareRu(x.label, y.label) < 0;
  });
  std::vector<ui::Option> opts;
  for (const Cand& c : cand) opts.push_back(ui::Option{c.label, c.icon, Color(0, 0, 0, 0), bld::catName(c.cat)});
  int idx = -1;
  if (ui::combo("addsreq", idx, std::span<const ui::Option>(opts), {.placeholder = "Добавить требование", .search = 1, .icon = "plus"}) && idx >= 0 &&
      idx < int(cand.size())) {
    const Id rid = cand[size_t(idx)].id;
    a.act("Требование на государство", [&](Tx& tx) { rules::setStateReq(tx, bid, rid, 4); });
  }
  a.markUi("bt.side.addsreq");
}

void sideBuilding(App& a, Ed& ed, const std::vector<BN>& bs, const BN& b) {
  const bool ro = a.readOnly();
  ui::IdScope scope{i64(b.id)};
  {
    ui::HStack hs(28, ui::Align::Left, 6);
    ui::tag(bld::catName(b.cat), ui::Tone::Neutral, bld::catIcon(b.cat));
    if (b.owner) ui::tag("Уникальная", ui::Tone::Accent, "crown");
    if (!b.key.empty()) {
      ui::icon("lock", ui::Ink::Muted, 16, "Встроенная постройка: на неё опираются правила");
      a.markUi("bt.side.builtin");
    }
    ui::flex();
    if (ui::iconButton("target", "Показать на схеме")) ed.revealSel = true;
    if (ui::iconButton("trash", "Удалить постройку", {.disabled = ro, .shortcut = {Key::Delete, 0}, .tone = ui::Tone::Danger})) askDelete(a, b);
    a.markUi("bt.side.delete");
  }
  {
    ui::Disabled dis(ro);
    ui::Row r({ui::px(44), ui::fr(1)}, 44, 10);
    // Значок постройки: щелчок — выбор из набора.
    {
      RectF ir = ui::next(44, 44);
      ui::Interaction it = ui::interact(ui::id("iconpick"), ir, ui::IfFocusable);
      Color cc = bld::catColor(b.cat);
      ui::draw::rect(ir, cc.alpha(it.hovered ? 0.26f : 0.17f), 11);
      ui::draw::rectStroke(ir, it.hovered ? ui::theme().accent : cc.alpha(0.4f), 11, 1);
      ui::draw::icon(!b.icon.empty() && gfx::hasIcon(b.icon) ? b.icon : std::string(bld::catIcon(b.cat)), ir.inset(11), cc);
      if (it.hovered && !ro) ui::setCursor(platform::Cursor::Hand);
      a.markUi("bt.side.icon", ir);
      if (it.clicked && !ro) ui::openPopup("icons");
    }
    if (ui::beginPopup("icons", {.width = 8 * 34 + 7 * 4 + 16})) {
      ui::caption("Значок постройки");
      ui::Row g({ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34), ui::px(34)}, 34, 4);
      for (const IconChoice& ic : kIcons) {
        if (!gfx::hasIcon(ic.name)) continue;
        ui::IdScope s2(ic.name);
        if (ui::iconButton(ic.name, ic.title, {.toggled = b.icon == ic.name})) {
          std::string v = ic.name;
          a.act("Значок постройки", [&](Tx& tx) { tx.building(b.id).icon = v; });
          ui::closePopup();
        }
        a.markUi(std::string("bt.icon.") + ic.name);
      }
      ui::endPopup();
    }
    {
      ui::Group grp(0, 4);
      if (ed.focusName == b.id) {
        ui::setKeyboardFocus(ui::id("name"));
        ed.focusName = 0;
      }
      std::string name = b.name;
      if (ui::textField("name", name, {.placeholder = "Название постройки", .maxLength = 80, .selectAllOnFocus = true}) && !trim(name).empty() &&
          trim(name) != b.name) {
        std::string n = trim(name);
        a.act("Переименовать постройку", [&](Tx& tx) { tx.building(b.id).name = n; });
      }
      a.markUi("bt.side.name");
    }
  }
  {
    ui::Disabled dis(ro);
    ui::caption("Категория");
    // Шесть категорий — значками (подпись — в подсказке и в метке над названием).
    std::vector<ui::Segment> segs;
    for (int c = 0; c < kCats; c++) segs.push_back(ui::Segment{bld::catIcon(BuildingCat(c)), {}, bld::catName(BuildingCat(c))});
    int cat = int(b.cat);
    if (ui::segmented("cat", cat, std::span<const ui::Segment>(segs)) && cat != int(b.cat) && cat >= 0 && cat < kCats)
      a.act("Категория постройки", [&](Tx& tx) { tx.building(b.id).cat = BuildingCat(cat); });
    a.markUi("bt.side.cat");
    ui::caption("Описание");
    std::string desc = b.desc;
    if (ui::textArea("desc", desc, 60, {.placeholder = "Назначение постройки", .maxLength = 600}) && desc != b.desc)
      a.act("Описание постройки", [&](Tx& tx) { tx.building(b.id).desc = desc; });
  }
  if (b.cat == BuildingCat::Cult) cultInfo(a, b);
  // Использование
  if (b.built + b.building > 0) {
    std::string t = "Построена в " + std::to_string(b.built) + " " + plural(b.built, "провинции", "провинциях", "провинциях");
    if (b.building) t += ", строится в " + std::to_string(b.building);
    ui::label(t, {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "province"});
  }
  reqSection(a, ed, bs, b, ro);
  if (!b.stateReqs.empty() || !ro) stateReqSection(a, b, ro);
  techSection(a, b, ro);
  rolesSection(a, b, ro);
  // Уровни (ТЗ 1.f.ii: постройки имеют несколько уровней и улучшаются)
  {
    ui::Section s("Уровни", "slots", {.badge = std::to_string(b.levels.size()), .actionIcon = ro ? nullptr : "plus", .actionTooltip = "Добавить уровень"});
    if (s.action() && !ro) {
      a.act("Уровень постройки", [&](Tx& tx) {
        Building& m = tx.building(b.id);
        BuildingLevel nl = m.levels.empty() ? BuildingLevel{} : m.levels.back();
        nl.desc.clear();
        m.levels.push_back(nl);
      });
    }
    a.markUi("bt.side.addlevel");
    if (s) {
      if (b.levels.empty()) {
        if (ui::emptyState("slots", "У постройки нет уровней.", ro ? "" : "Добавить уровень", "plus"))
          a.act("Уровень постройки", [&](Tx& tx) { tx.building(b.id).levels.push_back(BuildingLevel{}); });
      }
      for (int li = 0; li < int(b.levels.size()); li++) levelCard(a, b, li, ro);
    }
  }
}

void sideLink(App& a, Ed& ed, const std::vector<BN>& bs) {
  const BN* req = find(bs, ed.link.req);
  const BN* b = find(bs, ed.link.b);
  if (!req || !b) return;
  const bool ro = a.readOnly();
  ui::caption("Требование");
  {
    // Требуемая → зависимая, по строке на каждую (названия бывают длинными).
    ui::Group g(0, 4);
    if (ui::chip(bname(*req) + "##req", {.color = bld::catColor(req->cat), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
      ed.sel = req->id;
      ed.link = {};
      ed.revealSel = true;
    }
    {
      ui::Indent in(10);
      ui::icon("arrow-down", ui::Ink::Muted, 16);
    }
    if (ui::chip(bname(*b) + "##b", {.color = bld::catColor(b->cat), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
      ed.sel = b->id;
      ed.link = {};
      ed.revealSel = true;
    }
  }
  int lvl = 1;
  for (const BuildingReq& r : b->reqs)
    if (r.building == req->id) lvl = r.level;
  int maxL = std::max<int>(1, int(req->levels.size()));
  ui::text("Для строительства «" + bname(*b) + "» в провинции нужна «" + bname(*req) + "» уровня " + roman(lvl) + " или выше.", ui::Font::Small, ui::Ink::Dim);
  {
    ui::Disabled dis(ro || maxL <= 1);
    ui::prop("Нужный уровень", "slots");
    if (ui::numberField("lvl", lvl, {.min = 1, .max = double(maxL), .steppers = true})) setReqLevel(a, b->id, req->id, clamp(lvl, 1, maxL));
    a.markUi("bt.side.reqlevel");
  }
  if (ui::button("Удалить требование", {.variant = ui::Variant::Danger, .icon = "unlink", .fill = true, .disabled = ro, .tooltip = "Delete"})) {
    removeReq(a, ed.link);
    ed.link = {};
  }
  a.markUi("bt.side.unlink");
}

void sideOverview(App& a, Ed& ed, Id owner, const std::vector<BN>& bs, const Lanes& L) {
  const World& w = a.world();
  const Faction* f = w.faction(owner);
  {
    ui::Row r({ui::px(54), ui::fr(1)}, 40, 12);
    {
      RectF fr = ui::next(54, 40);
      if (f) {
        ui::at(RectF{fr.x, fr.y + 2, 54, 36});
        ui::flag(ed.flag, 54, 36, 5);
      } else {
        ui::draw::rect(RectF{fr.x, fr.y + 2, 54, 36}, ui::theme().accent.alpha(0.14f), 7);
        ui::draw::icon("globe", RectF{fr.x + 15, fr.y + 8, 24, 24}, ui::theme().accent);
      }
    }
    ui::Group g(0, 0);
    ui::caption(f ? "Уникальные постройки" : "Общее дерево");
    ui::label(f ? f->name : std::string("Для всех государств"), {.font = ui::Font::Subtitle});
  }
  int built = 0;
  for (const BN& b : bs) built += b.built + b.building;
  // Постройки общего дерева (ТЗ 1.h.i: дерево государства = общее дерево + уникальные постройки). Здесь — только
  // просмотр; правка — в общем дереве.
  std::vector<const Building*> common;
  std::map<Id, int> use;   // провинций государства с постройкой
  int commonBuilt = 0;      // постройки общего дерева в провинциях государства
  if (f) {
    w.buildings.each([&](const Building& b) {
      if (b.owner == 0) common.push_back(&b);
    });
    w.provinces.each([&](const Province& p) {
      if (p.owner != owner) return;
      for (const ProvBuilding& pb : p.buildings) {
        use[pb.building]++;
        if (const Building* b = w.building(pb.building); b && b->owner == 0) commonBuilt++;
      }
    });
    std::stable_sort(common.begin(), common.end(), [](const Building* x, const Building* y) {
      if (x->cat != y->cat) return x->cat < y->cat;
      return compareRu(x->name, y->name) < 0;
    });
  }
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 10);
    ui::stat(std::to_string(bs.size() + common.size()), "В дереве",
             {.icon = "building", .tone = ui::Tone::Accent,
              .tooltip = f ? "Уникальных: " + std::to_string(bs.size()) + ", общего дерева: " + std::to_string(common.size()) : std::string()});
    ui::stat(std::to_string(built + commonBuilt), "В провинциях",
             {.icon = "province", .tone = ui::Tone::Info,
              .tooltip = f ? "Уникальных: " + std::to_string(built) + ", общего дерева: " + std::to_string(commonBuilt) : std::string()});
  }
  if (f) {
    if (ui::Section s("Постройки общего дерева", "globe", {.badge = std::to_string(common.size())}); s) {
      if (common.empty()) ui::label("В общем дереве пока нет построек", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      for (const Building* b : common) {
        ui::IdScope cs{i64(b->id)};
        int n = int(b->levels.size());
        std::string sub = std::string(bld::catName(b->cat)) + " · " + std::to_string(n) + " " + plural(n, "уровень", "уровня", "уровней");
        int u = use.count(b->id) ? use[b->id] : 0;
        if (ui::listItem(b->name.empty() ? std::string("Без названия") : b->name,
                         {.icon = bld::iconOf(*b), .subtitle = sub, .hint = u ? std::to_string(u) : std::string(),
                          .tooltip = "Открыть в общем дереве построек"}))
          openBuildingTree(a, 0, b->id);
        a.markUi("bt.common." + std::to_string(b->id));
      }
      if (ui::link("Изменить общее дерево", "globe")) openBuildingTree(a, 0);
      a.markUi("bt.common.edit");
    }
  }
  if (ui::Section s("Категории", "layers"); s) {
    for (int c = 0; c < kCats; c++) {
      ui::IdScope sc(c);
      ui::Row r({ui::px(22), ui::fr(1), ui::px(40)}, 26, 8);
      ui::iconColored(bld::catIcon(BuildingCat(c)), bld::catColor(BuildingCat(c)), 18);
      ui::label(bld::catName(BuildingCat(c)));
      ui::label(std::to_string(L.count[c]), {.ink = ui::Ink::Dim, .align = ui::Align::Right});
    }
  }
}

// ---------------------------------------------------------------- редактор
void drawBuildingTree(App& a, Id owner) {
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  const bool ro = a.readOnly();
  RectF R = ui::avail();
  const Faction* fac = owner ? w.faction(owner) : nullptr;
  if (owner && (!fac || !fac->isState())) {
    ui::Area ar(RectF{R.cx() - 180, R.cy() - 120, 360, 240}, 0);
    ui::emptyState("building", "Уникальные постройки бывают только у государств.");
    if (ui::button("Общее дерево построек", {.icon = "globe", .fill = true})) a.openEditor("buildings", 0);
    return;
  }
  Ed& ed = ui::state<Ed>(ui::id("bt#" + std::to_string(owner)));
  if (fac) ed.flag = fac->flag;

  // Снимок дерева на кадр.
  std::vector<BN> bs;
  std::unordered_map<Id, size_t> idx;
  w.buildings.each([&](const Building& b) {
    if (b.owner != owner) return;
    BN n;
    n.id = b.id;
    n.owner = b.owner;
    n.name = b.name;
    n.desc = b.desc;
    n.icon = b.icon;
    n.cat = BuildingCat(clamp(int(b.cat), 0, kCats - 1));
    n.reqs = b.requires_;
    n.techs = b.techs;
    n.levels = b.levels;
    n.pos = b.pos;
    n.convert = b.convert;
    n.essenceGen = b.essenceGen;
    n.specialAccess = b.specialAccess;
    n.relicStore = b.relicStore;
    n.healing = b.healing;
    n.plague = b.plague;
    n.shipyard = b.shipyard;
    n.mercenary = b.mercenary;
    n.coastal = b.coastal;
    n.recipe = b.recipe;
    n.specials = b.specials;
    n.stateReqs = b.stateReqs;
    n.key = b.key;
    n.match = ed.query.empty() || utf8::matches(b.name, ed.query) || utf8::matches(b.desc, ed.query);
    idx[b.id] = bs.size();
    bs.push_back(std::move(n));
  });
  w.provinces.each([&](const Province& p) {
    for (const ProvBuilding& pb : p.buildings)
      if (auto it = idx.find(pb.building); it != idx.end()) {
        BN& n = bs[it->second];
        if (pb.builtLevel() > 0) n.built++;
        else n.building++;
        // Культовая: где уже стоит (как rules::cultBuiltIn — уникальная только в провинциях своего государства).
        if (n.cat == BuildingCat::Cult && !n.cultAt && (!n.owner || p.owner == n.owner)) n.cultAt = p.id;
      }
  });
  if (Pending& p = pending(); p.set && p.owner == owner) {
    if (find(bs, p.building)) {
      ed.sel = p.building;
      ed.link = {};
      ed.revealSel = true;
    }
    p = {};
  }
  if (ed.sel && !find(bs, ed.sel)) ed.sel = 0;
  if (ed.link) {
    const BN* lb = find(bs, ed.link.b);
    bool ok = lb && find(bs, ed.link.req);
    if (ok) {
      ok = false;
      for (const BuildingReq& r : lb->reqs) ok = ok || r.building == ed.link.req;
    }
    if (!ok) ed.link = {};
  }
  if (ed.focusName && ed.focusName != ed.sel) ed.focusName = 0;
  if (ed.drag && !find(bs, ed.drag)) ed.drag = 0;
  if (ed.conn && !find(bs, ed.conn)) ed.conn = 0;
  Lanes L = lanesOf(bs);

  RectF bar = R.cutTop(36);
  R.cutTop(12);
  RectF side = R.cutRight(kSideW);
  R.cutRight(12);
  RectF canvas = R;
  Box2 bounds = boundsOf(bs, L);
  for (const BN& b : bs)
    for (const Ext& e : extsOf(w, bs, b, cardPos(L, b))) bounds.add(Box2{e.pill.x, e.pill.y, e.pill.right(), e.pill.bottom()});
  ed.cam.insetLeft = kGutter;
  if (!ed.cam.ready && !canvas.empty()) fit(ed.cam, canvas, bounds, false, 1.0);
  else if (ed.refit && !canvas.empty()) fit(ed.cam, canvas, bounds, true, 1.0);
  ed.refit = false;

  // ---- панель инструментов
  {
    ui::Area ar(bar, 0);
    ui::HStack hs(34, ui::Align::Left, 8);
    {
      RectF fr = ui::next(42, 34);
      if (fac) {
        ui::at(RectF{fr.x, fr.y + 3, 42, 28});
        ui::flag(ed.flag, 42, 28, 4, fac->name);
      } else {
        ui::draw::rect(RectF{fr.x, fr.y + 3, 42, 28}, th.accent.alpha(0.14f), 6);
        ui::draw::icon("globe", RectF{fr.x + 11, fr.y + 7, 20, 20}, th.accent);
      }
    }
    {
      ui::Group g(280, 0);
      ui::spacer(2);
      Id pick = owner;
      if (factionSwitch("tree", pick, true, "Общее дерево") && pick != owner) a.openEditor("buildings", pick);
      a.markUi("bt.picker");
    }
    ui::tag(std::to_string(bs.size()) + " " + plural(i64(bs.size()), "постройка", "постройки", "построек"), ui::Tone::Neutral, "building");
    if (fac) ui::tag("уникальные", ui::Tone::Accent, "crown");
    if (ro) ui::tag("Ход " + std::to_string(a.ui.viewTurn.value_or(0)) + " · только просмотр", ui::Tone::Warning, "lock");
    ui::flex();
    {
      RectF sr = ui::next(220, 34);
      ui::at(RectF{sr.x, sr.y + 2, sr.w, 30});
      std::string q = ed.query;
      if (ui::searchField("search", q, "Найти постройку")) {
        ed.query = q;
        for (const BN& b : bs)
          if (!q.empty() && (utf8::matches(b.name, q) || utf8::matches(b.desc, q))) {
            ed.sel = b.id;
            ed.link = {};
            ed.revealSel = true;
            break;
          }
      }
    }
    ui::separatorV();
    ui::Disabled d(ro);
    if (ui::iconButton("plus", "Новая постройка", {.shortcut = {Key::N, 0}})) {
      Vec2 c = toWorld(ed.cam, canvas, canvas.cx(), canvas.cy());
      if (ed.sel)
        if (const BN* s = find(bs, ed.sel)) c = {cardPos(L, *s).x + kBW * 0.5 + rules::kTreeColStep, cardPos(L, *s).y + kBH * 0.5};
      if (Id nid = createAt(a, owner, L, c)) {
        ed.sel = nid;
        ed.link = {};
        ed.focusName = ed.sel;
      }
    }
    a.markUi("bt.add");
    if (ui::iconButton("wand", "Расставить дерево автоматически", {.disabled = bs.size() < 2})) {
      autoLayout(a, bs);
      ed.refit = true;
    }
    a.markUi("bt.layout");
    ui::separatorV();
    bool canDel = ed.sel || ed.link;
    if (ui::iconButton("trash", ed.link ? "Удалить требование" : "Удалить постройку", {.disabled = !canDel, .shortcut = {Key::Delete, 0}, .tone = ui::Tone::Danger})) {
      if (ed.link) {
        removeReq(a, ed.link);
        ed.link = {};
      } else if (const BN* b = find(bs, ed.sel)) {
        askDelete(a, *b);
      }
    }
    a.markUi("bt.delete");
  }

  // ---- холст: ввод
  a.markUi("bt.canvas", canvas);
  RectF content{canvas.x + kGutter, canvas.y, std::max(0.f, canvas.w - kGutter), canvas.h};
  const ui::Mouse& mouse = ui::mouse();
  ui::Interaction bg = ui::interact(ui::id("bg"), canvas, ui::IfAllowOverlap | ui::IfMiddleButton | ui::IfRightButton);
  if (bg.pressed) ed.bgDragged = false;
  if (bg.dragging) ed.bgDragged = true;
  Vec2 mw = toWorld(ed.cam, canvas, mouse.x, mouse.y);

  Id hoverNode = 0, hoverOut = 0, hoverIn = 0;
  bool connReleased = false, openMenu = false;
  std::vector<size_t> order(bs.size());
  for (size_t i = 0; i < bs.size(); i++) order[i] = i;
  std::stable_partition(order.begin(), order.end(), [&](size_t i) { return bs[i].id != ed.sel && bs[i].id != ed.drag; });
  auto posOf = [&](const BN& b) { return ed.drag == b.id && ed.dragMoved ? ed.dragPos : cardPos(L, b); };
  for (size_t oi : order) {
    const BN& b = bs[oi];
    Vec2 p = posOf(b);
    RectF sr = toScreen(ed.cam, canvas, p.x, p.y, kBW, kBH);
    bool off = sr.right() < canvas.x - 20 || sr.x > canvas.right() + 20 || sr.bottom() < canvas.y - 20 || sr.y > canvas.bottom() + 20;
    if (off && b.id != ed.drag && b.id != ed.conn) continue;
    ui::IdScope s{i64(b.id)};
    a.markUi("bt.node." + std::to_string(b.id), sr);
    ui::Interaction ni = ui::interact(ui::id("node"), sr.intersect(content), ui::IfAllowOverlap | ui::IfRightButton);
    float ps = std::max(18.f, float(16 * ed.cam.z));
    RectF po{sr.right() - ps * 0.5f, sr.cy() - ps * 0.5f, ps, ps}, pin{sr.x - ps * 0.5f, sr.cy() - ps * 0.5f, ps, ps};
    ui::Interaction oi2 = ui::interact(ui::id("out"), po.intersect(content), ui::IfAllowOverlap);
    ui::Interaction ii = ui::interact(ui::id("in"), pin.intersect(content), ui::IfAllowOverlap);
    a.markUi("bt.out." + std::to_string(b.id), po);
    a.markUi("bt.in." + std::to_string(b.id), pin);
    if (ni.hovered) hoverNode = b.id;
    if (oi2.hovered) hoverOut = b.id;
    if (ii.hovered) hoverIn = b.id;
    if (ni.pressed) {
      ed.sel = b.id;
      ed.link = {};
      if (ni.button == 0 && !ro) {
        ed.drag = b.id;
        ed.dragStart = cardPos(L, b);
        ed.dragPos = ed.dragStart;
        ed.dragMoved = false;
      }
      if (ni.doubleClicked) ed.focusName = ed.sel;
    }
    if (ed.drag == b.id && ni.held && ni.dragging) {
      ed.dragPos = {ed.dragStart.x + double(ni.dx) / ed.cam.z, ed.dragStart.y + double(ni.dy) / ed.cam.z};
      ed.dragMoved = true;
      ui::setCursor(platform::Cursor::Grabbing);
    }
    if (ni.rightClicked) {
      ed.menuB = b.id;
      ed.menuLink = {};
      openMenu = true;
    }
    if (ni.hovered && !ed.drag && !ed.conn) ui::setCursor(platform::Cursor::Hand);
    if ((oi2.hovered || ii.hovered) && !ro) ui::setCursor(platform::Cursor::Crosshair);
    if (oi2.pressed && !ro) {
      ed.conn = b.id;
      ed.connOut = true;
    }
    if (ii.pressed && !ro) {
      ed.conn = b.id;
      ed.connOut = false;
    }
    if (ed.conn == b.id && ((ed.connOut && oi2.released) || (!ed.connOut && ii.released))) connReleased = true;
  }
  // Перенос карточки закончен — одно действие: место и, если сменилась дорожка, категория.
  int dropLane = -1;
  if (ed.drag && ed.dragMoved) dropLane = L.at(ed.dragPos.y + kBH * 0.5);
  if (ed.drag && !mouse.down[0]) {
    if (const BN* b = find(bs, ed.drag); b && ed.dragMoved) {
      int cat = clamp(L.at(ed.dragPos.y + kBH * 0.5), 0, kCats - 1);
      Vec2 np{snap(ed.dragPos.x), snap(std::max(0.0, ed.dragPos.y - L.top[cat] - kHead))};
      Id bid = b->id;
      bool catChanged = cat != int(b->cat);
      a.act(catChanged ? "Категория постройки" : "Переместить постройку", [&](Tx& tx) {
        Building& m = tx.building(bid);
        m.pos = np;
        m.cat = BuildingCat(cat);
      });
    }
    ed.drag = 0;
    ed.dragMoved = false;
  }
  // Требования к общим постройкам (у уникальных): плашки слева от карточек; щелчок — общее дерево.
  std::vector<Ext> exts;
  for (const BN& b : bs)
    for (Ext& e : extsOf(w, bs, b, posOf(b))) exts.push_back(std::move(e));
  Id openExt = 0, openExtOwner = 0;
  int hoverExt = -1;
  for (size_t i = 0; i < exts.size(); i++) {
    Ext& e = exts[i];
    RectF sr = toScreen(ed.cam, canvas, e.pill.x, e.pill.y, e.pill.w, e.pill.h);
    RectF vis = sr.intersect(content);
    if (vis.empty()) continue;
    ui::IdScope s{i64(e.b) * 1000003 + i64(e.req)};
    ui::Interaction it = ui::interact(ui::id("ext"), vis);
    a.markUi("bt.ext." + std::to_string(e.b) + "." + std::to_string(e.req), sr);
    if (it.hovered && !ed.drag && !ed.conn) {
      e.hot = true;
      hoverExt = int(i);
      ui::setCursor(platform::Cursor::Hand);
    }
    if (it.clicked && !ed.drag && !ed.conn) {
      openExt = e.req;
      openExtOwner = e.owner;
    }
  }
  // Цель связи.
  Id dropTarget = 0;
  bool dropBad = false;
  std::string dropWhy;
  if (ed.conn) {
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      const BN& b = bs[*it];
      if (b.id == ed.conn) continue;
      Vec2 p = cardPos(L, b);
      if (mw.x >= p.x - 10 && mw.x <= p.x + kBW + 10 && mw.y >= p.y - 10 && mw.y <= p.y + kBH + 10) {
        dropTarget = b.id;
        break;
      }
    }
    bool linked = false;
    if (dropTarget) {
      Id bid = ed.connOut ? dropTarget : ed.conn, req = ed.connOut ? ed.conn : dropTarget;
      if (const BN* tb = find(bs, bid))
        for (const BuildingReq& r : tb->reqs) linked = linked || r.building == req;
      if (linked) {
        dropBad = true;
        dropWhy = "Уже связаны";
      } else if (!rules::canRequireBuilding(w, bid, req, &dropWhy)) {
        dropBad = true;   // причина — от правил (цикл, чужое дерево)
      }
    }
    ui::setCursor(platform::Cursor::Crosshair);
    if (connReleased || !mouse.down[0]) {
      if (dropTarget && !linked) {
        // Недопустимую связь правило отклонит с той же причиной (уведомление).
        Id bid = ed.connOut ? dropTarget : ed.conn, req = ed.connOut ? ed.conn : dropTarget;
        if (a.act("Требование постройки", [&](Tx& tx) { rules::setBuildingReq(tx, bid, req, 1); })) {
          ed.link = {req, bid};
          ed.sel = 0;
        }
      }
      ed.conn = 0;
    }
  }
  // Связь под указателем.
  LinkSel hoverLink;
  if (bg.hovered && !ed.conn && !ed.drag && !(ed.pan.on && ed.bgDragged)) {
    double best = 8 / ed.cam.z;
    for (const BN& b : bs)
      for (const BuildingReq& r : b.reqs) {
        const BN* rb = find(bs, r.building);
        if (!rb) continue;
        Vec2 pa = cardPos(L, *rb), pb = cardPos(L, b);
        double d = curveDistance(curve({pa.x + kBW, pa.y + kBH * 0.5}, {pb.x, pb.y + kBH * 0.5}), mw);
        if (d < best) {
          best = d;
          hoverLink = {r.building, b.id};
        }
      }
    if (hoverLink) ui::setCursor(platform::Cursor::Hand);
  }
  bool overCanvas = bg.hovered || hoverNode || hoverOut || hoverIn || ed.pan.on;
  panZoom(ed.cam, canvas, bg, ed.pan, overCanvas && !ed.conn && !ed.drag);
  if (bg.doubleClicked) ed.skipClick = true;
  if (bg.clicked && !ed.bgDragged && bg.button == 0) {
    if (ed.skipClick) {
      ed.skipClick = false;
    } else {
      ed.sel = 0;
      ed.link = hoverLink;
    }
  }
  if (bg.doubleClicked && !ro && !hoverLink) {
    if (Id nid = createAt(a, owner, L, mw)) {
      ed.sel = nid;
      ed.link = {};
      ed.focusName = ed.sel;
    }
  }
  if (bg.rightClicked) {
    ed.menuB = 0;
    ed.menuLink = hoverLink;
    ed.menuAt = mw;
    openMenu = true;
  }
  if (openMenu) ui::openContextMenu("ctx");
  if (ui::beginMenu("ctx")) {
    if (const BN* b = find(bs, ed.menuB)) {
      ui::menuHeader(bname(*b));
      if (ui::beginSubmenu("Категория", "layers")) {
        for (int c = 0; c < kCats; c++) {
          ui::IdScope sc(c);
          if (ui::menuItem(bld::catName(BuildingCat(c)), {.icon = bld::catIcon(BuildingCat(c)), .checked = int(b->cat) == c, .disabled = ro})) {
            Id bid = b->id;
            a.act("Категория постройки", [&](Tx& tx) { tx.building(bid).cat = BuildingCat(c); });
          }
        }
        ui::endSubmenu();
      }
      ui::menuSeparator();
      if (ui::menuItem("Удалить", {.icon = "trash", .shortcut = {Key::Delete, 0}, .danger = true, .disabled = ro})) askDelete(a, *b);
    } else if (ed.menuLink) {
      ui::menuHeader("Требование");
      if (ui::menuItem("Удалить требование", {.icon = "unlink", .shortcut = {Key::Delete, 0}, .danger = true, .disabled = ro})) removeReq(a, ed.menuLink);
    } else {
      int lane = L.at(ed.menuAt.y);
      if (ui::menuItem(std::string("Новая постройка: ") + bld::catName(BuildingCat(lane)), {.icon = "plus", .disabled = ro})) {
        if (Id nid = createAt(a, owner, L, ed.menuAt)) {
          ed.sel = nid;
          ed.focusName = ed.sel;
        }
      }
      if (ui::menuItem("Расставить автоматически", {.icon = "wand", .disabled = ro || bs.size() < 2})) {
        autoLayout(a, bs);
        ed.refit = true;
      }
      if (ui::menuItem("Показать всё дерево", {.icon = "zoom-fit", .shortcut = {Key::F, 0}})) fit(ed.cam, canvas, bounds, true);
    }
    ui::endMenu();
  }

  // ---- клавиатура
  if (!ui::anyModalOpen()) {
    if (ui::shortcut({Key::F, 0})) fit(ed.cam, canvas, bounds, true);
    if (ui::shortcut({Key::Equal, 0}) || ui::shortcut({Key::NumAdd, 0})) zoomCenter(ed.cam, canvas, 1.25);
    if (ui::shortcut({Key::Minus, 0}) || ui::shortcut({Key::NumSub, 0})) zoomCenter(ed.cam, canvas, 1 / 1.25);
    if (!ro && (ui::shortcut({Key::Delete, 0}) || ui::shortcut({Key::Backspace, 0}))) {
      if (ed.link) {
        removeReq(a, ed.link);
        ed.link = {};
      } else if (const BN* b = find(bs, ed.sel)) {
        askDelete(a, *b);
      }
    }
  }
  if (ed.revealSel) {
    if (const BN* b = find(bs, ed.sel)) {
      Vec2 p = cardPos(L, *b);
      reveal(ed.cam, canvas, Box2{p.x, p.y, p.x + kBW, p.y + kBH}, true);
    }
    ed.revealSel = false;
  }

  // ---- сцена
  auto sc = std::make_shared<Scene>();
  sc->cam = ed.cam;
  sc->k = ink();
  sc->lanes = L;
  sc->dropLane = dropLane;
  sc->exts = exts;
  for (const BN& b : bs)
    for (const BuildingReq& r : b.reqs) {
      const BN* rb = find(bs, r.building);
      if (!rb) continue;
      Vec2 pa = posOf(*rb), pb = posOf(b);
      EdgeD e;
      e.k = curve({pa.x + kBW, pa.y + kBH * 0.5}, {pb.x, pb.y + kBH * 0.5});
      LinkSel ls{r.building, b.id};
      e.sel = ed.link == ls;
      Color base = Color::mix(sc->k.textMuted, bld::catColor(rb->cat), 0.45f).alpha(0.85f);
      if (!b.match || !rb->match) base = base.alpha(0.3f);
      e.col = e.sel ? sc->k.accent : hoverLink == ls ? base.lighten(0.3f).alpha(1.f) : base;
      e.width = e.sel || hoverLink == ls ? 3.f : 2.f;
      e.level = r.level;
      sc->edges.push_back(e);
    }
  for (size_t oi : order) {
    const BN& b = bs[oi];
    Card cd;
    cd.b = b;
    cd.at = posOf(b);
    if (b.cultAt) cd.cultAt = w.provinceName(b.cultAt);
    cd.sel = ed.sel == b.id;
    cd.hover = hoverNode == b.id || hoverOut == b.id || hoverIn == b.id;
    cd.ghost = ed.drag == b.id && ed.dragMoved;
    cd.dim = !b.match;
    cd.drop = dropTarget == b.id;
    cd.dropBad = dropBad;
    cd.outHot = hoverOut == b.id || (ed.conn == b.id && ed.connOut);
    cd.inHot = hoverIn == b.id || (ed.conn == b.id && !ed.connOut);
    cd.hasIn = !b.reqs.empty();
    for (const BN& o : bs)
      for (const BuildingReq& r : o.reqs) cd.hasOut = cd.hasOut || r.building == b.id;
    sc->cards.push_back(std::move(cd));
  }
  if (ed.conn) {
    if (const BN* src = find(bs, ed.conn)) {
      sc->pending = true;
      Vec2 p = cardPos(L, *src);
      Vec2 from = ed.connOut ? Vec2{p.x + kBW, p.y + kBH * 0.5} : Vec2{p.x, p.y + kBH * 0.5};
      Vec2 to = mw;
      if (const BN* tg = find(bs, dropTarget); tg && !dropBad) {
        Vec2 q = cardPos(L, *tg);
        to = ed.connOut ? Vec2{q.x, q.y + kBH * 0.5} : Vec2{q.x + kBW, q.y + kBH * 0.5};
      }
      sc->pend = ed.connOut ? curve(from, to) : curve(to, from);
      sc->pendCol = dropTarget ? (dropBad ? sc->k.danger : sc->k.success) : sc->k.accent;
    }
  }
  ui::custom(canvas, [sc](gfx::Canvas& c, RectF dev, float scale) { renderScene(*sc, c, dev, scale); });

  // ---- поверх холста
  ui::draw::pushClip(canvas);
  // Колонка подписей дорожек (закреплена у левого края холста).
  {
    RectF gut{canvas.x, canvas.y, kGutter, canvas.h};
    ui::draw::rect(gut, th.surface2, 10);
    ui::draw::rect(RectF{gut.right() - 12, gut.y, 12, gut.h}, th.surface2);
    ui::draw::line(gut.right(), gut.y, gut.right(), gut.bottom(), th.border, 1);
    ui::draw::pushClip(gut);
    for (int c = 0; c < kCats; c++) {
      float y0 = toScreen(ed.cam, canvas, {0, L.top[c]}).y, y1 = toScreen(ed.cam, canvas, {0, L.top[c] + L.h[c]}).y;
      // Полоса: первая и последняя дорожки — до краёв холста; подпись — по центру видимой части самой дорожки.
      float by0 = c == 0 ? std::min(y0, canvas.y) : y0, by1 = c == kCats - 1 ? std::max(y1, canvas.bottom()) : y1;
      float bv0 = std::max(by0, canvas.y), bv1 = std::min(by1, canvas.bottom());
      if (bv1 - bv0 < 2) continue;
      Color cc = bld::catColor(BuildingCat(c));
      ui::draw::rect(RectF{gut.x, bv0, gut.w, bv1 - bv0}, cc.alpha(dropLane == c ? 0.16f : 0.07f));
      ui::draw::rect(RectF{gut.x, bv0, 3, bv1 - bv0}, cc.alpha(0.8f));
      if (c > 0 && y0 >= canvas.y) ui::draw::line(gut.x, y0, gut.right(), y0, cc.alpha(0.35f), 1);
      float vy0 = std::max(y0, canvas.y), vy1 = std::min(y1, canvas.bottom());
      if (vy1 - vy0 < 2) {
        vy0 = bv0;
        vy1 = bv1;
      }
      std::string cnt = std::to_string(L.count[c]);
      float cy = std::round((vy0 + vy1) * 0.5f);
      // Длинное название («Жилые и сельско-хозяйственные») — в несколько строк: перенос после пробела или дефиса.
      const std::string name = bld::catName(BuildingCat(c));
      const float nameW = gut.w - 22;
      std::vector<std::string> lines;
      {
        std::string cur, word;
        auto flush = [&](bool last) {
          const std::string cand = cur.empty() ? word : cur + word;
          if (!cur.empty() && ui::measure(trim(cand), ui::Font::Strong) > nameW) {
            lines.push_back(trim(cur));
            cur = word;
          } else {
            cur = cand;
          }
          word.clear();
          if (last && !trim(cur).empty()) lines.push_back(trim(cur));
        };
        for (char ch : name) {
          word += ch;
          if (ch == ' ' || ch == '-') flush(false);
        }
        flush(true);
        if (lines.size() > 3) lines.resize(3);   // последняя строка — с многоточием
      }
      const float textH = 17 * float(lines.size());
      bool compact = vy1 - vy0 < (lines.size() <= 1 ? 46.f : 30 + 3 + textH + 12);
      if (compact) {
        ui::draw::icon(bld::catIcon(BuildingCat(c)), RectF{gut.x + 14, cy - 9, 18, 18}, cc);
        const RectF nr{gut.x + 38, cy - 9, gut.w - 46, 18};
        ui::draw::text(name, nr, ui::Font::Small, th.textDim);
        if (ui::measure(name, ui::Font::Small) > nr.w) ui::hoverTip("##lane" + std::to_string(c), nr, name);   // обрезано — полностью в подсказке
      } else {
        const float top = std::round(cy - (30 + 3 + textH) * 0.5f);
        ui::draw::rect(RectF{gut.x + 14, top, 30, 30}, cc.alpha(0.16f), 8);
        ui::draw::icon(bld::catIcon(BuildingCat(c)), RectF{gut.x + 20, top + 6, 18, 18}, cc);
        ui::draw::text(cnt, RectF{gut.x + 52, top, gut.w - 60, 30}, ui::Font::Number, th.text);
        for (size_t li = 0; li < lines.size(); li++)
          ui::draw::text(lines[li], RectF{gut.x + 14, top + 33 + 17 * float(li), nameW, 18}, ui::Font::Strong, th.text);
      }
    }
    ui::draw::popClip();
  }
  ui::draw::rectStroke(canvas, th.border, 10, 1);
  if (bs.empty()) {
    if (canvasEmpty(canvas, "building", fac ? "Уникальных построек пока нет." : "В общем дереве пока нет построек.", "Новая постройка", "plus", ro, {},
                    nullptr, "bt.empty")) {
      if (Id nid = createAt(a, owner, L, {kBW * 0.5, L.top[1] + kHead + kBH * 0.5})) {
        ed.sel = nid;
        ed.focusName = ed.sel;
        ed.refit = true;
      }
    }
  }
  if (ed.link && !ro) {
    const BN* rb = find(bs, ed.link.req);
    const BN* b = find(bs, ed.link.b);
    if (rb && b) {
      Vec2 pa = cardPos(L, *rb), pb = cardPos(L, *b);
      Vec2 mid = curveAt(curve({pa.x + kBW, pa.y + kBH * 0.5}, {pb.x, pb.y + kBH * 0.5}), 0.5);
      gfx::Pt sp = toScreen(ed.cam, canvas, mid);
      sp.y += 26;   // под подписью уровня
      if (canvas.inset(14).contains(sp.x, sp.y)) {
        RectF br{std::round(sp.x - 15), std::round(sp.y - 15), 30, 30};
        ui::draw::shadow(br, 15, 12, th.shadow, 3);
        ui::draw::rect(br, th.surface1, 15);
        ui::draw::rectStroke(br, th.danger.alpha(0.6f), 15, 1.2f);
        ui::at(br);
        if (ui::iconButton("unlink", "Удалить требование", {.shortcut = {Key::Delete, 0}, .tone = ui::Tone::Danger})) {
          removeReq(a, ed.link);
          ed.link = {};
        }
        a.markUi("bt.link.delete");
      }
    }
  }
  if (ed.conn && dropBad && !dropWhy.empty()) {
    float tw = ui::measure(dropWhy, ui::Font::Small) + 30;
    RectF r{mouse.x + 14, mouse.y + 14, tw, 24};
    ui::draw::rect(r, th.surface1, 12);
    ui::draw::rectStroke(r, th.danger.alpha(0.7f), 12, 1);
    ui::draw::icon("warning", RectF{r.x + 8, r.y + 5, 14, 14}, th.danger);
    ui::draw::text(dropWhy, RectF{r.x + 25, r.y, tw - 28, r.h}, ui::Font::Small, th.danger);
  }
  if (!bs.empty()) {
    zoomBar(a, ed.cam, canvas, bounds, "bt.zoom");
    std::vector<MiniItem> mi;
    for (const BN& b : bs) {
      Vec2 p = cardPos(L, b);
      mi.push_back({Box2{p.x, p.y, p.x + kBW, p.y + kBH}, bld::catColor(b.cat).alpha(b.match ? 0.9f : 0.3f), b.id == ed.sel});
    }
    minimap(ed.cam, canvas, bounds, mi, "bt.mini");
  }
  ui::draw::popClip();

  // ---- панель свойств
  {
    ui::draw::rect(side, th.surface2, th.radiusCard);
    ui::draw::rectStroke(side, th.border, th.radiusCard, 1);
    ui::Area ar(side.inset(14, 12), 0);
    ui::Scroll scroll("side");
    if (const BN* b = find(bs, ed.sel)) sideBuilding(a, ed, bs, *b);
    else if (ed.link) sideLink(a, ed, bs);
    else sideOverview(a, ed, owner, bs, L);
  }

  // ---- карточка сведений (карточка постройки или плашка требования к общей постройке)
  Id tipId = (hoverNode && !ed.drag && !ed.conn && !ui::anyModalOpen()) ? hoverNode : 0;
  const Ext* tipExt = hoverExt >= 0 && !ui::anyModalOpen() ? &exts[size_t(hoverExt)] : nullptr;
  u64 tipKey = tipExt ? hash64("bt-ext") ^ (u64(tipExt->b) << 32 | tipExt->req) : tipId ? hash64("bt") ^ tipId : 0;
  bool tipShow = hoverDelay(tipKey);
  if (tipShow && tipExt) {
    if (const Building* rb = w.building(tipExt->req)) {
      int lvl = 1;
      if (const BN* b = find(bs, tipExt->b))
        for (const BuildingReq& r : b->reqs)
          if (r.building == tipExt->req) lvl = r.level;
      Tip tip;
      tip.title = rb->name.empty() ? "Без названия" : rb->name;
      const Faction* of = rb->owner ? w.faction(rb->owner) : nullptr;
      tip.subtitle = (of ? "Уникальная · " + of->name : std::string("Общее дерево")) + " · " + bld::catName(rb->cat);
      tip.accent = bld::catColor(rb->cat);
      tip.icon = bld::iconOf(*rb);
      tip.lines.push_back({"link", "Нужна в провинции уровня " + roman(lvl) + " или выше", th.warning, true});
      tip.lines.push_back({"arrow-right", of ? "Щелчок — открыть её дерево" : "Щелчок — открыть в общем дереве", Color(0, 0, 0, 0)});
      tipCard(tip, toScreen(ed.cam, canvas, tipExt->pill.x, tipExt->pill.y, tipExt->pill.w, tipExt->pill.h), canvas);
    }
  }
  if (tipShow && tipId && !tipExt) {
    if (const BN* b = find(bs, tipId)) {
      Tip tip;
      tip.title = bname(*b);
      tip.subtitle = std::string(bld::catName(b->cat)) + " · " + std::to_string(b->levels.size()) + " " + plural(i64(b->levels.size()), "уровень", "уровня", "уровней");
      tip.accent = bld::catColor(b->cat);
      tip.icon = !b->icon.empty() && gfx::hasIcon(b->icon) ? b->icon : bld::catIcon(b->cat);
      if (!trim(b->desc).empty()) tip.lines.push_back({"", b->desc, th.textDim, true});
      if (b->cat == BuildingCat::Cult) {
        tip.lines.push_back({"b-cult", b->owner ? "Одна на государство" : "Одна на всю карту", bld::catColor(BuildingCat::Cult), false, true});
        if (b->cultAt) tip.lines.push_back({"map-pin", "Уже стоит: " + w.provinceName(b->cultAt), th.accent});
      }
      if (b->convert) tip.lines.push_back({"convert", "Преобразование: " + bld::recipeText(w, b->recipe), th.info, true});
      if (b->specialAccess && !b->specials.empty())
        tip.lines.push_back({"special-unit", "Особые отряды: " + bld::specialsText(w, b->specials), th.accent, true});
      if (!b->key.empty()) tip.lines.push_back({"lock", "Встроенная постройка", th.textMuted});
      if (b->relicStore) tip.lines.push_back({"relic", "Хранилище реликвий", Color::hex(kRelicInk)});
      if (b->healing) tip.lines.push_back({"heal", "Здание целительства", th.success});
      if (b->plague) tip.lines.push_back({"plague", "Здание чумы", Color::hex(kPlagueInk)});
      if (b->mercenary) tip.lines.push_back({"mercenary", "Гильдия наёмников", th.accent});
      if (b->coastal) tip.lines.push_back({"anchor", "Только в приморской провинции", th.textDim});
      for (const StateReq& sr : b->stateReqs)
        if (const Building* rb = w.building(sr.building))
          tip.lines.push_back({"crown", "На каждую — ещё " + std::to_string(sr.per) + " «" + bname(*rb) + "» в государстве", th.warning, true});
      for (size_t li = 0; li < b->levels.size() && li < 6; li++) {
        const BuildingLevel& lv = b->levels[li];
        std::string price = bld::costText(w, lv.cost);
        if (!lv.essCost.empty()) price += ", " + bld::essenceCostText(w, lv.essCost);
        tip.lines.push_back({"slots", roman(int(li) + 1) + " · " + nTurns(std::max(1, lv.turns)) + " · " + price, Color(0, 0, 0, 0), true});
        if (b->shipyard && lv.ships) tip.lines.push_back({"shipyard", "Корабли: " + bld::shipsText(lv.ships), th.info, true});
        if (!lv.produce.empty()) tip.lines.push_back({"repeat", "За ход: " + bld::produceText(w, lv.produce), th.success});
        if (b->essenceGen && !lv.essence.empty()) tip.lines.push_back({"essence", "Эссенция за ход: " + bld::essenceText(w, lv.essence), Color::hex(kEssenceInk)});
        for (Id mid : lv.modifiers)
          if (const Modifier* m = w.modifier(mid))
            for (int fx = 0; fx < kFxCount; fx++)
              if (m->has(Fx(fx)) && m->fx[size_t(fx)] != 0)
                tip.lines.push_back({schema::effect(Fx(fx)).icon, w::effectText(Fx(fx), m->fx[size_t(fx)]),
                                     w::effectGood(Fx(fx), m->fx[size_t(fx)]) ? th.success : th.danger});
      }
      for (const BuildingReq& r : b->reqs)
        if (const Building* rb = w.building(r.building))
          tip.lines.push_back({"link", "Требует: " + rb->name + (r.level > 1 ? " (ур. " + roman(r.level) + ")" : ""), th.warning});
      {
        std::vector<std::string> tn;
        for (Id t : b->techs)
          if (const Tech* x = w.tech(t)) tn.push_back(tname(*x));
        if (!tn.empty()) tip.lines.push_back({"tech-tree", "Нужны технологии: " + join(tn, ", "), th.warning, true});
      }
      if (b->built + b->building > 0)
        tip.lines.push_back({"province", "В провинциях: " + std::to_string(b->built) + (b->building ? ", строится " + std::to_string(b->building) : ""), Color(0, 0, 0, 0)});
      Vec2 p = cardPos(L, *b);
      tipCard(tip, toScreen(ed.cam, canvas, p.x, p.y, kBW, kBH), canvas);
    }
  }
  if (openExt) openBuildingTree(a, openExtOwner, openExt);
}

EditorReg regBuildings({"buildings", "Дерево построек", drawBuildingTree, "building", "economy", 20});
DialogReg regLevelMods({"bt.mods", [](App&, Id building) { return levelModsDialog(building, 1); }});

}  // namespace

void openBuildingTree(App& a, Id owner, Id building) {
  pending() = Pending{owner, building, building != 0};
  a.openEditor("buildings", owner);
}

std::unique_ptr<Dialog> levelModsDialog(Id building, int level) {
  auto d = std::make_unique<LevelMods>();
  d->bid = building;
  d->li = std::max(0, level - 1);
  return d;
}

}  // namespace rg::app
