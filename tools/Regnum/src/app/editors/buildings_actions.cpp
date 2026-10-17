// Regnum — дерево построек: раскладка дорожек и действия с постройками и требованиями (buildings_internal.h).
#include "app/editors/buildings_internal.h"

namespace rg::app::bldtree {

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
    case rules::BuildingRole::Drain: return Color::hex(0xd0233f);
    case rules::BuildingRole::UpkeepCut: return th.success;
    case rules::BuildingRole::Extract: return Color::hex(0xc8743a);
  }
  return th.textDim;
}

void askDelete(App& a, const BN& b) {
  std::string text = "Постройка исчезнет из дерева";
  if (b.built + b.building > 0)
    text += " и из " + std::to_string(b.built + b.building) + " " + plural(b.built + b.building, "провинции", "провинций", "провинций") +
            "; незавершённое строительство вернёт стоимость";
  text += ".";
  Id id = b.id;
  a.confirmAct("Удалить постройку «" + bname(b) + "»?", text, "Удалить", "Удалить постройку", [id](Tx& tx) { rules::removeBuilding(tx, id); });
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

}  // namespace rg::app::bldtree
