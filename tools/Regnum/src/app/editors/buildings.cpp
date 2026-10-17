// Regnum — полноэкранный редактор дерева построек (ТЗ 1.h.i–ii, 1.f.ii): общее дерево (одинаковое для всех
// государств) или уникальные постройки государства. Дорожки категорий (военные, экономические, промышленные,
// жилые и сельско-хозяйственные, религиозные, культовые), карточки построек, требования к другим постройкам (связи,
// уровень), «на государство» (соборы) и к технологиям, панель свойств: особые возможности (преобразование ресурсов по
// рецепту, генерация эссенции, доступ к особым отрядам, хранилище реликвий, целительство, чума, верфь, гильдия
// наёмников, только приморская), уровни — срок, стоимость ресурсами и эссенциями, корабли верфи, ресурсы и эссенции за
// ход, модификаторы (окно выбора с созданием нового), описание. ТЗ «Доработки», п.2–5, 7, «Ввод новых механик», п.4–5,
// «Доработки №1–4». Встроенная постройка (с ключом правил) отмечена замком.
#include "app/editors/buildings_internal.h"

namespace rg::app {
namespace bldtree {

namespace {

// Кадр редактора: копия дерева, раскладка и то, что нашёл ввод холста (наведение, перенос, связь). Фазы кадра —
// методы, по порядку вызова в drawBuildingTree.
struct View {
  App& a;
  const World& w;
  Id owner = 0;
  const Faction* fac = nullptr;
  bool ro = false;
  Ed& ed;
  const ui::Theme& th = ui::theme();
  const ui::Mouse& mouse = ui::mouse();
  std::vector<BN> bs;
  Lanes L;
  RectF bar, canvas, content;
  Box2 bounds;
  ui::Interaction bg{};
  Vec2 mw;                       // точка схемы под указателем
  std::vector<size_t> order;     // порядок карточек: выделенная и переносимая — поверх
  Id hoverNode = 0, hoverOut = 0, hoverIn = 0;
  bool connReleased = false, openMenu = false;
  int dropLane = -1;             // дорожка под переносимой карточкой
  std::vector<Ext> exts;         // плашки требований к постройкам другого дерева
  int hoverExt = -1;
  Id openExt = 0, openExtOwner = 0;
  Id dropTarget = 0;             // карточка под концом новой связи
  bool dropBad = false;
  std::string dropWhy;
  LinkSel hoverLink;

  Vec2 posOf(const BN& b) const { return ed.drag == b.id && ed.dragMoved ? ed.dragPos : cardPos(L, b); }
  void toolbar();
  void nodesInput();
  void dragInput();
  void extsInput();
  void connectInput();
  void backgroundInput();
  void keys();
  void scene();
  void laneGutter();
  void overlay();
  void hoverCard();
};

// Копия построек дерева на кадр: свойства, число провинций с постройкой, где стоит культовая.
std::vector<BN> snapshot(const World& w, Id owner, const Ed& ed) {
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
    n.market = b.market;
    n.drain = b.drain;
    n.upkeepCut = b.upkeepCut;
    n.extract = b.extract;
    n.extractGroups = b.extractGroups;
    n.extractExclude = b.extractExclude;
    n.stateKinds = b.stateKinds;
    n.recipes = b.recipes;
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
  return bs;
}

// Выделение из openBuildingTree; связь, перенос и выделение исчезнувших построек снимаются.
void syncSelection(Ed& ed, const std::vector<BN>& bs, Id owner) {
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
}

// Панель инструментов: флаг, выбор дерева, число построек, поиск, новая, авторасстановка, удаление.
void View::toolbar() {
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
    if (a.readOnly()) ui::tag("Ход " + std::to_string(a.ui.viewTurn.value_or(0)) + " · только просмотр", ui::Tone::Warning, "lock");
    else if (ro) ui::tag("Только просмотр", ui::Tone::Neutral, "lock");
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
}

// Холст: наведение и нажатия по карточкам (выбор, перенос, начало связи с выхода или входа).
void View::nodesInput() {
  a.markUi("bt.canvas", canvas);
  content = RectF{canvas.x + kGutter, canvas.y, std::max(0.f, canvas.w - kGutter), canvas.h};
  bg = ui::interact(ui::id("bg"), canvas, ui::IfAllowOverlap | ui::IfMiddleButton | ui::IfRightButton);
  if (bg.pressed) ed.bgDragged = false;
  if (bg.dragging) ed.bgDragged = true;
  mw = toWorld(ed.cam, canvas, mouse.x, mouse.y);
  order.resize(bs.size());
  for (size_t i = 0; i < bs.size(); i++) order[i] = i;
  std::stable_partition(order.begin(), order.end(), [&](size_t i) { return bs[i].id != ed.sel && bs[i].id != ed.drag; });
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
}

// Перенос карточки закончен — одно действие: место и, если сменилась дорожка, категория.
void View::dragInput() {
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
}

// Требования к общим постройкам (у уникальных): плашки слева от карточек; щелчок — общее дерево.
void View::extsInput() {
  for (const BN& b : bs)
    for (Ext& e : extsOf(w, bs, b, posOf(b))) exts.push_back(std::move(e));
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
}

// Цель новой связи и её создание при отпускании.
void View::connectInput() {
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
}

// Связь под указателем, панорама и масштаб, щелчки по фону, контекстное меню.
void View::backgroundInput() {
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
}

// Клавиатура: показать всё, масштаб, удаление выделенного.
void View::keys() {
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
}

// Сцена холста (ui::custom): связи, карточки, тянущаяся связь.
void View::scene() {
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
}

// Колонка подписей дорожек (закреплена у левого края холста).
void View::laneGutter() {
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
}

// Поверх холста: пустое дерево, кнопка удаления выбранной связи, причина отказа связи, масштаб и мини-карта.
void View::overlay() {
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
}

// Карточка сведений: карточка постройки или плашка требования к постройке другого дерева.
void View::hoverCard() {
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
      if (b->stateKinds) tip.lines.push_back({"lock", "Только для: " + utf8::lower(bld::stateKindsText(b->stateKinds)), th.warning, true});
      if (b->convert) {
        // Много рецептов (сталелитейная) — первые шесть и «ещё N».
        const size_t shown = std::min<size_t>(b->recipes.size(), 6);
        for (size_t i = 0; i < shown; i++) tip.lines.push_back({"convert", bld::recipeText(w, b->recipes[i]), th.info, true});
        if (b->recipes.size() > shown)
          tip.lines.push_back({"convert", "ещё " + std::to_string(b->recipes.size() - shown) + " " + plural(i64(b->recipes.size() - shown), "рецепт", "рецепта", "рецептов"),
                               th.info});
        if (b->recipes.empty()) tip.lines.push_back({"convert", "Преобразование: рецептов нет", th.warning});
      }
      if (const Building* rb = w.building(b->id); rb && b->extract) tip.lines.push_back({"pickaxe", "Добыча: " + bld::extractText(w, *rb), Color::hex(kExtractInk), true});
      if (b->market) tip.lines.push_back({"scales", "Рынок: «Продать реликвии»", th.accent});
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
        if (!lv.produceOnRes.empty()) tip.lines.push_back({"map-pin", "За ход " + bld::produceOnResText(w, lv), th.success, true});
        if (b->drain && (lv.drainPop > 0 || !lv.drainEss.empty())) tip.lines.push_back({"population", "Иссушение: " + bld::drainText(w, lv), Color::hex(kDrainInk), true});
        if (b->upkeepCut && lv.upkeepCut > 0) tip.lines.push_back({"army", "Содержание армии государства −" + fmtPct(lv.upkeepCut, 1), th.success});
        if (b->extract && lv.extractPct > 0) tip.lines.push_back({"pickaxe", "Добыча ресурса провинции +" + fmtPct(lv.extractPct, 1), Color::hex(kExtractInk)});
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
}

}  // namespace

// ---------------------------------------------------------------- редактор
void drawBuildingTree(App& a, Id owner) {
  const World& w = a.world();
  const bool ro = !a.canEdit();   // дерево построек правит только администратор
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

  View v{a, w, owner, fac, ro, ed};
  v.bs = snapshot(w, owner, ed);
  syncSelection(ed, v.bs, owner);
  const std::vector<BN>& bs = v.bs;
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

  v.L = L;
  v.bar = bar;
  v.canvas = canvas;
  v.bounds = bounds;

  v.toolbar();
  v.nodesInput();
  v.dragInput();
  v.extsInput();
  v.connectInput();
  v.backgroundInput();
  v.keys();
  v.scene();
  ui::draw::pushClip(canvas);
  v.laneGutter();
  v.overlay();
  ui::draw::popClip();
  // ---- панель свойств
  {
    ui::draw::rect(side, v.th.surface2, v.th.radiusCard);
    ui::draw::rectStroke(side, v.th.border, v.th.radiusCard, 1);
    ui::Area ar(side.inset(14, 12), 0);
    ui::Scroll scroll("side");
    if (const BN* b = find(bs, ed.sel)) sideBuilding(a, ed, bs, *b);
    else if (ed.link) sideLink(a, ed, bs);
    else sideOverview(a, ed, owner, bs, L);
  }

  v.hoverCard();
  if (v.openExt) openBuildingTree(a, v.openExtOwner, v.openExt);
}

}  // namespace bldtree

namespace {
EditorReg regBuildings({"buildings", "Дерево построек", bldtree::drawBuildingTree, "building", "economy", 20});
DialogReg regLevelMods({"bt.mods", [](App&, Id building) { return levelModsDialog(building, 1); }});
}  // namespace

void openBuildingTree(App& a, Id owner, Id building) {
  bldtree::pending() = bldtree::Pending{owner, building, building != 0};
  a.openEditor("buildings", owner);
}

}  // namespace rg::app
