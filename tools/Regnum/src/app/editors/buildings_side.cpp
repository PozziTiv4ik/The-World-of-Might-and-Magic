// Regnum — дерево построек: панель свойств справа (постройка, требование, обзор дерева): значок, название, категория,
// требования к постройкам, технологиям и «на государство», особые возможности (buildings_roles.cpp), уровни.
#include "app/editors/buildings_internal.h"

namespace rg::app::bldtree {

namespace {

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
  w::IdOptions opts;
  for (const Cand& c : cand) opts.add(c.id, c.label, c.icon, none, c.ok ? bld::catName(c.cat) : "цикл", !c.ok);
  if (const Id req = w::addCombo("addreq", opts, {.placeholder = "Добавить требование", .search = 1, .icon = "plus"})) addReq(a, b.id, req);
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
    ui::ChipFlow flow(40);
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
      ui::ChipAction act = ui::flowChip(tname(*t), co);
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
  w::IdOptions opts;
  for (const Cand& c : cand) opts.add(c.id, c.label, c.faction ? "crown" : "globe", none, c.faction ? "своя" : "общая");
  if (const Id tid = w::addCombo("addtech", opts, {.placeholder = "Добавить технологию", .search = 1, .icon = "plus"}))
    a.act("Требуемая технология", [&](Tx& tx) { rules::setBuildingTech(tx, bid, tid, true); });
  a.markUi("bt.side.addtech");
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
  w::IdOptions opts;
  for (const Cand& c : cand) opts.add(c.id, c.label, c.icon, Color(0, 0, 0, 0), bld::catName(c.cat));
  if (const Id rid = w::addCombo("addsreq", opts, {.placeholder = "Добавить требование", .search = 1, .icon = "plus"}))
    a.act("Требование на государство", [&](Tx& tx) { rules::setStateReq(tx, bid, rid, 4); });
  a.markUi("bt.side.addsreq");
}

}  // namespace

void sideBuilding(App& a, Ed& ed, const std::vector<BN>& bs, const BN& b) {
  const bool ro = !a.canEdit();
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
    // Категории — значками (подпись — в подсказке и в метке над названием).
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
  const bool ro = !a.canEdit();
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

}  // namespace rg::app::bldtree
