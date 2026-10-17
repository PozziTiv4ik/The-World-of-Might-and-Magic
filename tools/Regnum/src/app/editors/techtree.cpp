// Regnum — полноэкранный редактор дерева технологий (ТЗ 1.b.v): дерево фракции или общее дерево (ТЗ «Доработки»,
// п.6–7), схема с панорамой и масштабом, карточки технологий (название, бонус, срок изучения, «галочка»
// изученности, ход исследования), связи-зависимости (несколько предшествующих, без циклов), перетаскивание
// карточек, создание и удаление, авторасстановка, копирование дерева другой фракции, панель свойств выбранной
// технологии. Общее дерево каждое государство изучает отдельно: «Изучение за» — чьё состояние показано и правится.
// В дереве фракции условия-общие технологии — плашки-ссылки слева от карточки (состояние для этой фракции).
// «Открывает постройки» — постройки, которым технология нужна (rules::techUnlocks).
#include "app/editors/techtree_internal.h"

namespace rg::app {
namespace techtree {

namespace {

// Кадр редактора: копия дерева, раскладка и то, что нашёл ввод холста (наведение, перенос, связь). Фазы кадра —
// методы, по порядку вызова в drawTechTree.
struct View {
  App& a;
  const World& w;
  Id faction = 0;
  const Faction* fac = nullptr;
  bool ro = false, common = false, guild = false;
  std::string facName;
  Id who = 0;                    // за кого изучение (своё дерево — 0)
  Ed& ed;
  const ui::Theme& th = ui::theme();
  const ui::Mouse& mouse = ui::mouse();
  std::vector<TN> ts;
  RectF bar, canvas;
  Box2 bounds;
  ui::Interaction bg{};
  Vec2 mw;                       // точка схемы под указателем
  std::vector<size_t> order;     // порядок карточек: выделенная и переносимая — поверх
  Id hoverNode = 0, hoverCheck = 0, hoverOut = 0, hoverIn = 0;
  bool connReleased = false, openMenu = false;
  std::vector<Ext> exts;         // плашки условий из общего дерева
  Id openExt = 0;
  int hoverExt = -1;
  Id dropTarget = 0;             // карточка под концом новой связи
  bool dropBad = false;
  std::string dropWhy;
  LinkSel hoverLink;
  std::shared_ptr<Scene> sc;     // сцена кадра (цвета схемы — и для мини-карты)

  void toolbar();
  void nodesInput();
  void dragEnd();
  void extsInput();
  void connectInput();
  void backgroundInput();
  void keys();
  void scene();
  void overlay();
  void hoverCard();
};

// Копия технологий дерева на кадр. Срок с модификатором государства одинаков для одинаковых базовых сроков — один
// расчёт эффектов на значение.
std::vector<TN> snapshot(const World& w, Id faction, Id who, const Ed& ed) {
  std::vector<TN> ts;
  std::map<int, int> needOf;
  w.techs.each([&](const Tech& t) {
    if (t.faction != faction) return;
    TN n;
    n.id = t.id;
    n.name = t.name;
    n.desc = t.desc;
    n.turns = std::max(1, t.turns);
    auto it = needOf.find(n.turns);
    if (it == needOf.end()) it = needOf.emplace(n.turns, rules::researchTurns(w, t, who)).first;
    n.need = it->second;
    const TechProgress p = rules::techState(w, t.id, who);
    n.progress = p.progress;
    n.studied = p.studied;
    n.research = p.research;
    n.pos = t.pos;
    n.prereqs = t.prereqs;
    n.mods = t.modifiers;
    n.cost = t.cost;
    n.needKeys = t.needKeys;
    ts.push_back(std::move(n));
  });
  for (TN& t : ts) {
    if (t.studied) t.st = St::Studied;
    else {
      rules::ResearchCheck rc = rules::canResearch(w, t.id, who);
      t.missing = rc.missing;
      t.problems = rc.problems;
      t.st = t.research ? St::Research : (rc.ok ? St::Available : St::Locked);
    }
    t.match = ed.query.empty() || utf8::matches(t.name, ed.query) || utf8::matches(t.desc, ed.query);
  }
  return ts;
}

// Выделение из openTechTree; связь, перенос и выделение исчезнувших технологий снимаются.
void syncSelection(Ed& ed, const std::vector<TN>& ts, Id faction, Pending& pd) {
  if (pd.set && pd.faction == faction) {
    if (pd.tech && find(ts, pd.tech)) {
      ed.sel = pd.tech;
      ed.link = {};
      ed.revealSel = true;
    }
    pd.set = false;
    pd.faction = pd.tech = pd.learner = 0;
  }
  if (ed.sel && !find(ts, ed.sel)) ed.sel = 0;
  if (ed.link) {
    const TN* lt = find(ts, ed.link.tech);
    if (!lt || !find(ts, ed.link.pre) || std::find(lt->prereqs.begin(), lt->prereqs.end(), ed.link.pre) == lt->prereqs.end()) ed.link = {};
  }
  if (ed.focusName && ed.focusName != ed.sel) ed.focusName = 0;
  if (ed.drag && !find(ts, ed.drag)) ed.drag = 0;
  if (ed.conn && !find(ts, ed.conn)) ed.conn = 0;
}

// Панель инструментов: флаг, выбор дерева, «Изучение за», счётчики, поиск, новая, авторасстановка, копия, удаление.
void View::toolbar() {
  int studiedN = 0, researchN = 0;
  for (const TN& t : ts) {
    studiedN += t.studied;
    researchN += t.st == St::Research;
  }

  {
    ui::Area ar(bar, 0);
    ui::HStack hs(34, ui::Align::Left, 8);
    {
      RectF fr = ui::next(42, 34);
      if (fac) {
        ui::at(RectF{fr.x, fr.y + 3, 42, 28});
        ui::flag(ed.flag, 42, 28, 4, facName);
      } else {
        ui::draw::rect(RectF{fr.x, fr.y + 3, 42, 28}, th.accent.alpha(0.14f), 6);
        ui::draw::icon("globe", RectF{fr.x + 11, fr.y + 7, 20, 20}, th.accent);
      }
    }
    {
      ui::Group g(common ? 230 : 280, 0);
      ui::spacer(2);
      Id pick = faction;
      if (factionSwitch("faction", pick, false, "Общее дерево") && pick != faction) a.openEditor("techtree", pick);
      a.markUi("tt.picker");
    }
    if (common) {
      // «Изучение за»: чьё изучение общего дерева показано и правится (переход, а не правка — и в прошлом ходу).
      ui::label("Изучение за", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      ui::Group g(210, 0);
      ui::spacer(2);
      Id lr = ed.learner;
      if (learnerSwitch("learner", lr)) ed.learner = lr;
      a.markUi("tt.learner");
    }
    ui::tag(std::to_string(studiedN) + " / " + std::to_string(ts.size()) + " изучено", ui::Tone::Success, "check-circle");
    if (researchN) ui::tag(std::to_string(researchN) + " " + plural(researchN, "исследуется", "исследуются", "исследуются"), ui::Tone::Info, "hourglass");
    if (a.readOnly()) ui::tag("Ход " + std::to_string(a.ui.viewTurn.value_or(0)) + " · только просмотр", ui::Tone::Warning, "lock");
    else if (ro) {
      ui::tag("Только просмотр", ui::Tone::Neutral, "lock");
      ui::tooltip("Дерево меняет администратор редактора; исследование — за своё государство");
    }
    ui::flex();
    {
      RectF sr = ui::next(common ? 190 : 220, 34);
      ui::at(RectF{sr.x, sr.y + 2, sr.w, 30});
      std::string q = ed.query;
      if (ui::searchField("search", q, "Найти технологию")) {
        ed.query = q;
        for (const TN& t : ts)
          if (!q.empty() && (utf8::matches(t.name, q) || utf8::matches(t.desc, q))) {
            ed.sel = t.id;
            ed.link = {};
            ed.revealSel = true;
            break;
          }
      }
      a.markUi("tt.search");
    }
    ui::separatorV();
    ui::Disabled d(ro);
    if (ui::iconButton("plus", common ? "Новая общая технология" : "Новая технология", {.shortcut = {Key::N, 0}})) {
      Vec2 c = toWorld(ed.cam, canvas, canvas.cx(), canvas.cy());
      if (Id nid = createAt(a, faction, c)) {
        ed.sel = nid;
        ed.link = {};
        ed.focusName = ed.sel;
      }
    }
    a.markUi("tt.add");
    if (ui::iconButton("wand", "Расставить дерево автоматически", {.disabled = ts.size() < 2})) {
      if (a.act("Расставить дерево технологий", [&](Tx& tx) { rules::autoLayout(tx, faction); })) ed.refit = true;
    }
    a.markUi("tt.layout");
    if (!common) {
      if (ui::iconButton("copy", "Скопировать дерево другой фракции")) ui::openPopup("copyfrom");
      a.markUi("tt.copy");
    }
    ui::separatorV();
    bool canDel = ed.sel || ed.link;
    if (ui::iconButton("trash", ed.link ? "Удалить связь" : "Удалить технологию", {.disabled = !canDel, .shortcut = {Key::Delete, 0}, .tone = ui::Tone::Danger})) {
      if (ed.link) {
        removeLink(a, ed.link);
        ed.link = {};
      } else if (const TN* t = find(ts, ed.sel)) {
        askDelete(a, t->id, t->name);
      }
    }
    a.markUi("tt.delete");
  }
}

// Холст: наведение и нажатия по карточкам (выбор, перенос, «галочка», начало связи).
void View::nodesInput() {
  // ---- холст: ввод
  a.markUi("tt.canvas", canvas);
  bg = ui::interact(ui::id("bg"), canvas, ui::IfAllowOverlap | ui::IfMiddleButton | ui::IfRightButton);
  if (bg.pressed) ed.bgDragged = false;
  if (bg.dragging) ed.bgDragged = true;
  mw = toWorld(ed.cam, canvas, mouse.x, mouse.y);
  // Выбранная карточка — последней (сверху).
  order.resize(ts.size());
  for (size_t i = 0; i < ts.size(); i++) order[i] = i;
  std::stable_partition(order.begin(), order.end(), [&](size_t i) { return ts[i].id != ed.sel && ts[i].id != ed.drag; });
  for (size_t oi : order) {
    const TN& t = ts[oi];
    RectF sr = toScreen(ed.cam, canvas, t.pos.x, t.pos.y, kW, kH);
    bool off = sr.right() < canvas.x - 20 || sr.x > canvas.right() + 20 || sr.bottom() < canvas.y - 20 || sr.y > canvas.bottom() + 20;
    if (off && t.id != ed.drag && t.id != ed.conn) continue;
    ui::IdScope s{i64(t.id)};
    a.markUi("tt.node." + std::to_string(t.id), sr);
    ui::Interaction ni = ui::interact(ui::id("node"), sr.intersect(canvas), ui::IfAllowOverlap | ui::IfRightButton);
    float ps = std::max(18.f, float(16 * ed.cam.z));
    RectF po{sr.right() - ps * 0.5f, sr.cy() - ps * 0.5f, ps, ps}, pin{sr.x - ps * 0.5f, sr.cy() - ps * 0.5f, ps, ps};
    float cs = std::max(18.f, float(22 * ed.cam.z));
    RectF cr{float(sr.right() - 22 * ed.cam.z - cs * 0.5f), float(sr.y + 22 * ed.cam.z - cs * 0.5f), cs, cs};
    ui::Interaction ci = ui::interact(ui::id("check"), cr.intersect(canvas), ui::IfAllowOverlap);
    ui::Interaction oi2 = ui::interact(ui::id("out"), po.intersect(canvas), ui::IfAllowOverlap);
    ui::Interaction ii = ui::interact(ui::id("in"), pin.intersect(canvas), ui::IfAllowOverlap);
    a.markUi("tt.check." + std::to_string(t.id), cr);
    a.markUi("tt.out." + std::to_string(t.id), po);
    a.markUi("tt.in." + std::to_string(t.id), pin);
    if (ni.hovered) hoverNode = t.id;
    if (ci.hovered) hoverCheck = t.id;
    if (oi2.hovered) hoverOut = t.id;
    if (ii.hovered) hoverIn = t.id;
    // Выбор и перетаскивание карточки
    if (ni.pressed) {
      ed.sel = t.id;
      ed.link = {};
      if (ni.button == 0 && !ro) {
        ed.drag = t.id;
        ed.dragStart = t.pos;
      }
      if (ni.doubleClicked) ed.focusName = ed.sel;
    }
    if (ed.drag == t.id && ni.held && ni.dragging) {
      Vec2 np{snap(ed.dragStart.x + double(ni.dx) / ed.cam.z), snap(ed.dragStart.y + double(ni.dy) / ed.cam.z)};
      if (np != t.pos) a.act("Переместить технологию", [&](Tx& tx) { tx.tech(t.id).pos = np; }, {.coalesce = "tt-move:" + std::to_string(t.id), .coalesceSec = 3600});
      ui::setCursor(platform::Cursor::Grabbing);
    }
    if (ed.drag == t.id && !ni.held) {
      ed.drag = 0;
      a.store.endCoalesce();
    }
    if (ni.rightClicked) {
      ed.menuTech = t.id;
      ed.menuLink = {};
      openMenu = true;
    }
    if (ni.hovered && !ed.drag && !ed.conn) ui::setCursor(platform::Cursor::Hand);
    // «Галочка» (у общей технологии — за изучающую фракцию)
    const bool canCheck = !ro && (!common || who);
    if (ci.hovered && canCheck) ui::setCursor(platform::Cursor::Hand);
    if (ci.clicked && canCheck) {
      ed.sel = t.id;
      ed.link = {};
      setStudied(a, t.id, !t.studied, who);
    }
    // Связи от гнёзд
    if ((oi2.hovered || ii.hovered) && !ro) ui::setCursor(platform::Cursor::Crosshair);
    if (oi2.pressed && !ro) {
      ed.conn = t.id;
      ed.connOut = true;
    }
    if (ii.pressed && !ro) {
      ed.conn = t.id;
      ed.connOut = false;
    }
    if (ed.conn == t.id && ((ed.connOut && oi2.released) || (!ed.connOut && ii.released))) connReleased = true;
  }
}

// Кнопку отпустили вне карточки (перетаскивание прервано) — закончить.
void View::dragEnd() {
  if (ed.drag && !mouse.down[0]) {
    ed.drag = 0;
    a.store.endCoalesce();
  }
}

// Условия из общего дерева (у дерева фракции): плашки слева от карточек; щелчок — общее дерево.
void View::extsInput() {
  if (!common)
    for (const TN& t : ts)
      for (Ext& e : extsOf(w, ts, t, faction)) exts.push_back(std::move(e));
  for (size_t i = 0; i < exts.size(); i++) {
    Ext& e = exts[i];
    RectF sr = toScreen(ed.cam, canvas, e.pill.x, e.pill.y, e.pill.w, e.pill.h);
    RectF vis = sr.intersect(canvas);
    if (vis.empty()) continue;
    ui::IdScope s{i64(e.tech) * 1000003 + i64(e.pre)};
    ui::Interaction it = ui::interact(ui::id("ext"), vis);
    a.markUi("tt.ext." + std::to_string(e.tech) + "." + std::to_string(e.pre), sr);
    if (it.hovered && !ed.drag && !ed.conn) {
      e.hot = true;
      hoverExt = int(i);
      ui::setCursor(platform::Cursor::Hand);
    }
    if (it.clicked && !ed.drag && !ed.conn) openExt = e.pre;
  }
}

// Цель новой связи под указателем и её создание при отпускании.
void View::connectInput() {
  if (ed.conn) {
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      const TN& t = ts[*it];
      if (t.id == ed.conn) continue;
      if (mw.x >= t.pos.x - 10 && mw.x <= t.pos.x + kW + 10 && mw.y >= t.pos.y - 10 && mw.y <= t.pos.y + kH + 10) {
        dropTarget = t.id;
        break;
      }
    }
    if (dropTarget) {
      Id tech = ed.connOut ? dropTarget : ed.conn, pre = ed.connOut ? ed.conn : dropTarget;
      const TN* tt = find(ts, tech);
      if (tt && std::find(tt->prereqs.begin(), tt->prereqs.end(), pre) != tt->prereqs.end()) {
        dropBad = true;
        dropWhy = "Уже связаны";
      } else if (rules::wouldCycle(w, tech, pre)) {
        dropBad = true;
        dropWhy = "Связь создаст цикл";
      }
    }
    ui::setCursor(platform::Cursor::Crosshair);
    if (connReleased || !mouse.down[0]) {
      if (dropTarget) {
        Id tech = ed.connOut ? dropTarget : ed.conn, pre = ed.connOut ? ed.conn : dropTarget;
        const TN* tt = find(ts, tech);
        bool linked = tt && std::find(tt->prereqs.begin(), tt->prereqs.end(), pre) != tt->prereqs.end();
        if (!linked && a.act("Связать технологии", [&](Tx& tx) { rules::setPrereq(tx, tech, pre, true); })) {
          ed.link = {pre, tech};
          ed.sel = 0;
        }
      }
      ed.conn = 0;
    }
  }
}

// Связь под указателем, панорама и масштаб, щелчки по фону, контекстное меню.
void View::backgroundInput() {
  if (bg.hovered && !ed.conn && !ed.drag && !(ed.pan.on && ed.bgDragged) && hoverExt < 0) {
    double best = 8 / ed.cam.z;
    for (const TN& t : ts)
      for (Id p : t.prereqs) {
        const TN* pt = find(ts, p);
        if (!pt) continue;
        double d = curveDistance(curve({pt->pos.x + kW, pt->pos.y + kH * 0.5}, {t.pos.x, t.pos.y + kH * 0.5}), mw);
        if (d < best) {
          best = d;
          hoverLink = {p, t.id};
        }
      }
    if (hoverLink) ui::setCursor(platform::Cursor::Hand);
  }
  // Фон: панорама, масштаб, щелчок, двойной щелчок, меню.
  bool overCanvas = bg.hovered || hoverNode || hoverCheck || hoverOut || hoverIn || ed.pan.on;
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
  if (bg.doubleClicked && !ro && !hoverLink && hoverExt < 0) {
    if (Id nid = createAt(a, faction, mw)) {
      ed.sel = nid;
      ed.link = {};
      ed.focusName = ed.sel;
    }
  }
  if (bg.rightClicked) {
    ed.menuTech = 0;
    ed.menuLink = hoverLink;
    ed.menuAt = mw;
    openMenu = true;
  }
  if (openMenu) ui::openContextMenu("ctx");
  if (ui::beginMenu("ctx")) {
    if (const TN* t = find(ts, ed.menuTech)) {
      ui::menuHeader(t->name.empty() ? std::string("Без названия") : t->name);
      bool st = t->studied;
      Id tid = t->id;
      const bool noWho = common && !who;
      if (ui::menuItem("Изучена", {.icon = "check", .checked = st, .disabled = ro || noWho})) setStudied(a, tid, !st, who);
      const bool noResearch = noWho || !canResearch(a, faction, who);
      if (t->research) {
        if (ui::menuItem("Остановить исследование", {.icon = "close", .disabled = noResearch})) stopResearch(a, tid, who);
      } else if (!t->studied) {
        if (ui::menuItem("Начать исследование", {.icon = "play", .disabled = noResearch || t->st != St::Available})) startResearch(a, tid, who);
      }
      ui::menuSeparator();
      if (ui::menuItem("Удалить", {.icon = "trash", .shortcut = {Key::Delete, 0}, .danger = true, .disabled = ro})) askDelete(a, tid, t->name);
    } else if (ed.menuLink) {
      ui::menuHeader("Зависимость");
      if (ui::menuItem("Удалить связь", {.icon = "unlink", .shortcut = {Key::Delete, 0}, .danger = true, .disabled = ro})) removeLink(a, ed.menuLink);
    } else {
      if (ui::menuItem(common ? "Новая общая технология здесь" : "Новая технология здесь", {.icon = "plus", .disabled = ro})) {
        if (Id nid = createAt(a, faction, ed.menuAt)) {
          ed.sel = nid;
          ed.focusName = ed.sel;
        }
      }
      if (ui::menuItem("Расставить автоматически", {.icon = "wand", .disabled = ro || ts.size() < 2}))
        if (a.act("Расставить дерево технологий", [&](Tx& tx) { rules::autoLayout(tx, faction); })) ed.refit = true;
      if (ui::menuItem("Показать всё дерево", {.icon = "zoom-fit", .shortcut = {Key::F, 0}})) fit(ed.cam, canvas, bounds, true);
    }
    ui::endMenu();
  }
}

// Клавиатура: показать всё, масштаб, удаление, стрелки — переход по дереву.
void View::keys() {
  // ---- клавиатура
  if (!ui::anyModalOpen()) {
    if (ui::shortcut({Key::F, 0})) fit(ed.cam, canvas, bounds, true);
    if (ui::shortcut({Key::Equal, 0}) || ui::shortcut({Key::NumAdd, 0})) zoomCenter(ed.cam, canvas, 1.25);
    if (ui::shortcut({Key::Minus, 0}) || ui::shortcut({Key::NumSub, 0})) zoomCenter(ed.cam, canvas, 1 / 1.25);
    if (!ro && (ui::shortcut({Key::Delete, 0}) || ui::shortcut({Key::Backspace, 0}))) {
      if (ed.link) {
        removeLink(a, ed.link);
        ed.link = {};
      } else if (const TN* t = find(ts, ed.sel)) {
        askDelete(a, t->id, t->name);
      }
    }
    // Стрелки — переход по дереву, если клавиатура не у виджета панели (числа, списки).
    if (const TN* cur = find(ts, ed.sel); cur && ui::keyboardFocus() == 0) {
      Id nx = 0;
      if (ui::shortcut({Key::Left, 0})) nx = neighbor(ts, *cur, -1, 0);
      else if (ui::shortcut({Key::Right, 0})) nx = neighbor(ts, *cur, 1, 0);
      else if (ui::shortcut({Key::Up, 0})) nx = neighbor(ts, *cur, 0, -1);
      else if (ui::shortcut({Key::Down, 0})) nx = neighbor(ts, *cur, 0, 1);
      if (nx) {
        ed.sel = nx;
        ed.revealSel = true;
      }
    }
  }
  if (ed.revealSel) {
    if (const TN* t = find(ts, ed.sel)) reveal(ed.cam, canvas, Box2{t->pos.x, t->pos.y, t->pos.x + kW, t->pos.y + kH}, true);
    ed.revealSel = false;
  }
}

// Сцена холста (ui::custom): связи, карточки, тянущаяся связь.
void View::scene() {
  // ---- холст: сцена
  sc = std::make_shared<Scene>();
  sc->cam = ed.cam;
  sc->k = ink();
  sc->readOnly = ro;
  sc->exts = exts;
  for (const TN& t : ts)
    for (Id p : t.prereqs) {
      const TN* pt = find(ts, p);
      if (!pt) continue;
      EdgeD e;
      e.k = curve({pt->pos.x + kW, pt->pos.y + kH * 0.5}, {t.pos.x, t.pos.y + kH * 0.5});
      LinkSel ls{p, t.id};
      e.sel = ed.link == ls;
      e.hot = hoverLink == ls;
      Color base = pt->studied ? (t.studied ? sc->k.success.alpha(0.75f) : t.st == St::Research ? sc->k.info.alpha(0.9f) : sc->k.accent.alpha(0.85f))
                               : sc->k.textMuted.alpha(0.55f);
      if (!t.match || !pt->match) base = base.alpha(0.3f);
      e.col = e.sel ? sc->k.accent : e.hot ? base.lighten(0.25f).alpha(1.f) : base;
      e.width = e.sel || e.hot ? 3.f : 2.f;
      sc->edges.push_back(e);
    }
  for (size_t oi : order) {
    const TN& t = ts[oi];
    Card cd;
    cd.t = t;
    cd.bonus = bonusText(w, t);
    if (t.st == St::Locked && !t.missing.empty()) cd.need = "Нужно: " + techNames(w, ts, t.missing);
    else if (t.st == St::Locked && !t.problems.empty()) cd.need = t.problems.front();
    for (auto& [res, v] : t.cost) cd.price.push_back({w::resourceIcon(w, res), w::resourceColor(w, res), res == kGold ? fmtNum(v, 3) + " тыс." : fmtShort(v)});
    cd.sel = ed.sel == t.id;
    cd.hover = hoverNode == t.id || hoverCheck == t.id || hoverOut == t.id || hoverIn == t.id;
    cd.dim = !t.match;
    cd.drop = dropTarget == t.id;
    cd.dropBad = dropBad;
    cd.checkHot = hoverCheck == t.id;
    cd.outHot = hoverOut == t.id || (ed.conn == t.id && ed.connOut);
    cd.inHot = hoverIn == t.id || (ed.conn == t.id && !ed.connOut);
    cd.hasIn = !t.prereqs.empty();
    for (const TN& o : ts)
      if (std::find(o.prereqs.begin(), o.prereqs.end(), t.id) != o.prereqs.end()) cd.hasOut = true;
    sc->cards.push_back(std::move(cd));
  }
  if (ed.conn) {
    if (const TN* src = find(ts, ed.conn)) {
      sc->pending = true;
      Vec2 from = ed.connOut ? Vec2{src->pos.x + kW, src->pos.y + kH * 0.5} : Vec2{src->pos.x, src->pos.y + kH * 0.5};
      Vec2 to = mw;
      if (const TN* tg = find(ts, dropTarget); tg && !dropBad) to = ed.connOut ? Vec2{tg->pos.x, tg->pos.y + kH * 0.5} : Vec2{tg->pos.x + kW, tg->pos.y + kH * 0.5};
      sc->pend = ed.connOut ? curve(from, to) : curve(to, from);
      sc->pendCol = dropTarget ? (dropBad ? sc->k.danger : sc->k.success) : sc->k.accent;
    }
  }
  ui::custom(canvas, [sc = sc](gfx::Canvas& c, RectF dev, float scale) { renderScene(*sc, c, dev, scale); });
  ui::draw::rectStroke(canvas, th.border, 10, 1);
}

// Поверх холста: пустое дерево, кнопка удаления выбранной связи, причина отказа связи, масштаб и мини-карта.
void View::overlay() {
  // ---- поверх холста
  ui::draw::pushClip(canvas);
  if (ts.empty()) {
    int act = common ? canvasEmpty(canvas, "globe", "Общее дерево пусто.", "Новая технология", "plus", ro, {}, nullptr, "tt.empty")
                     : canvasEmpty(canvas, "tech-tree", guild ? "У гильдии пока нет технологий." : "Дерево технологий пусто.", "Новая технология", "plus", ro,
                                   "Скопировать дерево другой фракции", "copy", "tt.empty");
    if (act == 1) {
      if (Id nid = createAt(a, faction, {kW * 0.5, kH * 0.5})) {
        ed.sel = nid;
        ed.focusName = ed.sel;
        ed.refit = true;
      }
    } else if (act == 2) {
      ui::openPopup("copyfrom");   // якорь — кнопка пустого холста
    }
  }
  // Кнопка удаления у выбранной связи.
  if (ed.link && !ro) {
    const TN* pt = find(ts, ed.link.pre);
    const TN* tt = find(ts, ed.link.tech);
    if (pt && tt) {
      Vec2 mid = curveAt(curve({pt->pos.x + kW, pt->pos.y + kH * 0.5}, {tt->pos.x, tt->pos.y + kH * 0.5}), 0.5);
      gfx::Pt sp = toScreen(ed.cam, canvas, mid);
      if (canvas.inset(14).contains(sp.x, sp.y)) {
        RectF br{std::round(sp.x - 15), std::round(sp.y - 15), 30, 30};
        ui::draw::shadow(br, 15, 12, th.shadow, 3);
        ui::draw::rect(br, th.surface1, 15);
        ui::draw::rectStroke(br, th.danger.alpha(0.6f), 15, 1.2f);
        ui::at(br);
        if (ui::iconButton("unlink", "Удалить связь", {.shortcut = {Key::Delete, 0}, .tone = ui::Tone::Danger})) {
          removeLink(a, ed.link);
          ed.link = {};
        }
        a.markUi("tt.link.delete");
      }
    }
  }
  // Подпись к недопустимой связи у указателя.
  if (ed.conn && dropBad && !dropWhy.empty()) {
    float tw = ui::measure(dropWhy, ui::Font::Small) + 30;
    RectF r{mouse.x + 14, mouse.y + 14, tw, 24};
    ui::draw::rect(r, th.surface1, 12);
    ui::draw::rectStroke(r, th.danger.alpha(0.7f), 12, 1);
    ui::draw::icon("warning", RectF{r.x + 8, r.y + 5, 14, 14}, th.danger);
    ui::draw::text(dropWhy, RectF{r.x + 25, r.y, tw - 28, r.h}, ui::Font::Small, th.danger);
  }
  if (!ts.empty()) {
    zoomBar(a, ed.cam, canvas, bounds, "tt.zoom");
    std::vector<MiniItem> mi;
    for (const TN& t : ts) mi.push_back({Box2{t.pos.x, t.pos.y, t.pos.x + kW, t.pos.y + kH}, stColor(sc->k, t.st).alpha(t.match ? 0.85f : 0.3f), t.id == ed.sel});
    minimap(ed.cam, canvas, bounds, mi, "tt.mini");
  }
  ui::draw::popClip();
  // Меню «Скопировать дерево» (из панели инструментов или пустого холста).
  if (!common) copyMenu(a, faction, !ts.empty());
}

// Карточка сведений при наведении: карточка технологии или плашка общей технологии.
void View::hoverCard() {
  Id tipId = (hoverNode && !ed.drag && !ed.conn && !ui::anyModalOpen()) ? hoverNode : 0;
  const Ext* tipExt = hoverExt >= 0 && !ui::anyModalOpen() ? &exts[size_t(hoverExt)] : nullptr;
  u64 tipKey = tipExt ? hash64("tt-ext") ^ (u64(tipExt->tech) << 32 | tipExt->pre) : tipId ? hash64("tt") ^ tipId : 0;
  const bool tipShow = hoverDelay(tipKey);
  if (tipShow && tipExt) {
    if (const Tech* pt = w.tech(tipExt->pre)) {
      std::vector<Id> missing;
      St st = stateOf(w, pt->id, faction, &missing);
      Tip tip;
      tip.title = w::orName(pt->name);
      tip.subtitle = std::string("Общее дерево · ") + stName(st) + " · " + facName;
      tip.accent = stColor(sc->k, st);
      tip.icon = stIcon(st);
      if (!trim(pt->desc).empty()) tip.lines.push_back({"", pt->desc, th.textDim, true});
      if (st == St::Research) {
        const TechProgress p = rules::techState(w, pt->id, faction);
        const int need = rules::researchTurns(w, *pt, faction);
        tip.lines.push_back({"hourglass", "Пройдено " + std::to_string(p.progress) + " из " + std::to_string(need), th.info});
      }
      if (!missing.empty()) tip.lines.push_back({"lock", "Не изучены: " + techNames(w, ts, missing, 4), th.warning, true});
      tip.lines.push_back({"arrow-right", "Щелчок — открыть в общем дереве", Color(0, 0, 0, 0)});
      tipCard(tip, toScreen(ed.cam, canvas, tipExt->pill.x, tipExt->pill.y, tipExt->pill.w, tipExt->pill.h), canvas);
    }
  }
  if (tipShow && tipId && !tipExt) {
    if (const TN* t = find(ts, tipId)) {
      Tip tip;
      tip.title = t->name.empty() ? "Без названия" : t->name;
      tip.subtitle = std::string(stName(t->st)) + " · " + nTurns(t->need) + " изучения";
      if (common)
        if (const Faction* lf = w.faction(who)) tip.subtitle += " · " + lf->name;
      tip.accent = stColor(sc->k, t->st);
      tip.icon = stIcon(t->st);
      if (!trim(t->desc).empty()) tip.lines.push_back({"", t->desc, th.textDim, true});
      for (Id mid : t->mods)
        if (const Modifier* m = w.modifier(mid))
          for (int fx = 0; fx < kFxCount; fx++)
            if (m->has(Fx(fx)) && m->fx[size_t(fx)] != 0)
              tip.lines.push_back({schema::effect(Fx(fx)).icon, w::effectText(Fx(fx), m->fx[size_t(fx)]),
                                   w::effectGood(Fx(fx), m->fx[size_t(fx)]) ? th.success : th.danger});
      if (t->research || (t->progress > 0 && !t->studied))
        tip.lines.push_back({"hourglass", "Пройдено " + std::to_string(t->progress) + " из " + std::to_string(t->need) + ", осталось " + nTurns(turnsLeft(t->need, t->progress)), th.info});
      if (!t->prereqs.empty()) tip.lines.push_back({"link", "Требует: " + techNames(w, ts, t->prereqs, 4), Color(0, 0, 0, 0), true});
      if (!t->missing.empty()) tip.lines.push_back({"lock", "Не изучены: " + techNames(w, ts, t->missing, 4), th.warning, true});
      if (!t->cost.empty()) tip.lines.push_back({"coins", "Стоимость: " + bld::costText(w, t->cost), Color(0, 0, 0, 0), true});
      if (!t->studied && !t->research)
        for (const std::string& pr : t->problems) tip.lines.push_back({"warning", pr, th.warning, true});
      std::vector<Id> deps;
      for (const TN& o : ts)
        if (std::find(o.prereqs.begin(), o.prereqs.end(), t->id) != o.prereqs.end()) deps.push_back(o.id);
      if (!deps.empty()) tip.lines.push_back({"arrow-right", "Открывает: " + techNames(w, ts, deps, 4), Color(0, 0, 0, 0), true});
      {
        std::vector<std::string> bn;
        for (Id b : rules::techUnlocks(w, t->id))
          if (const Building* x = w.building(b)) bn.push_back(x->name.empty() ? std::string("Без названия") : x->name);
        if (!bn.empty()) tip.lines.push_back({"building", "Открывает постройки: " + join(bn, ", "), th.success, true});
      }
      tipCard(tip, toScreen(ed.cam, canvas, t->pos.x, t->pos.y, kW, kH), canvas);
    }
  }
}

}  // namespace

// ---------------------------------------------------------------- редактор
void drawTechTree(App& a, Id faction) {
  const World& w = a.world();
  const bool ro = !a.canEdit();   // игроку дерево — для просмотра (исследование — canResearch)
  RectF R = ui::avail();
  const bool common = faction == 0;
  const Faction* fac = common ? nullptr : w.faction(faction);
  if (!common && !fac) {
    // Фракции нет (удалена или не выбрана) — предложить выбрать.
    ui::Area ar(RectF{R.cx() - 180, R.cy() - 120, 360, 240}, 0);
    ui::emptyState("tech-tree", "Выберите государство, гильдию или общее дерево.");
    Id pick = faction;
    if (factionSwitch("pick", pick, false, "Общее дерево") && pick != faction) a.openEditor("techtree", pick);
    return;
  }
  Ed& ed = ui::state<Ed>(ui::id("tt#" + std::to_string(faction)));
  if (fac) ed.flag = fac->flag;
  const std::string facName = fac ? fac->name : std::string("Общее дерево");
  const bool guild = fac && fac->isGuild();
  Pending& pd = pending();
  if (common && pd.set && pd.faction == 0 && pd.learner && w.faction(pd.learner)) ed.learner = pd.learner;
  if (common && (!ed.learner || !w.faction(ed.learner))) ed.learner = a.player() ? a.ui.playerState : defaultLearner(w);
  const Id who = common ? ed.learner : 0;   // за кого изучение (своё дерево — 0)

  View v{a, w, faction, fac, ro, common, guild, facName, who, ed};
  v.ts = snapshot(w, faction, who, ed);
  syncSelection(ed, v.ts, faction, pd);
  const std::vector<TN>& ts = v.ts;

  // Раскладка: панель инструментов, холст, панель свойств.
  RectF bar = R.cutTop(36);
  R.cutTop(12);
  RectF side = R.cutRight(kSideW);
  R.cutRight(12);
  RectF canvas = R;
  Box2 bounds = boundsOf(ts);
  if (!common)
    for (const TN& t : ts)
      for (const Ext& e : extsOf(w, ts, t, faction)) bounds.add(Box2{e.pill.x, e.pill.y, e.pill.right(), e.pill.bottom()});
  if (pd.refit && pd.refit == faction) {
    ed.refit = true;
    pd.refit = 0;
  }
  if (!ed.cam.ready && !canvas.empty()) fit(ed.cam, canvas, bounds, false, 1.0);
  else if (ed.refit && !canvas.empty()) fit(ed.cam, canvas, bounds, true, 1.0);
  ed.refit = false;

  v.bar = bar;
  v.canvas = canvas;
  v.bounds = bounds;

  v.toolbar();
  v.nodesInput();
  v.dragEnd();
  v.extsInput();
  v.connectInput();
  v.backgroundInput();
  v.keys();
  v.scene();
  v.overlay();
  // ---- панель свойств
  {
    ui::draw::rect(side, v.th.surface2, v.th.radiusCard);
    ui::draw::rectStroke(side, v.th.border, v.th.radiusCard, 1);
    ui::Area ar(side.inset(14, 12), 0);
    ui::Scroll scroll("side");
    if (const TN* t = find(ts, ed.sel)) sideTech(a, ed, faction, who, ts, *t);
    else if (ed.link) sideLink(a, ed, ts);
    else sideOverview(a, ed, faction, ts);
  }

  v.hoverCard();
  if (v.openExt) openTechTree(a, 0, v.openExt, faction);
}

}  // namespace techtree

namespace {
using techtree::drawTechTree;

EditorReg regTechTree({"techtree", "Дерево технологий", drawTechTree, "tech-tree"});

// Команда палитры: дерево технологий выделенной фракции (или первого государства).
Id commandFaction(App& a) {
  const World& w = a.world();
  if (a.ui.sel.type == SelType::Faction && w.faction(a.ui.sel.id)) return a.ui.sel.id;
  if (a.ui.sel.type == SelType::Province)
    if (const Province* p = w.province(a.ui.sel.id); p && w.faction(p->owner)) return p->owner;
  if (a.player()) return a.ui.playerState;   // игрок — своё государство
  Id first = 0;
  w.factions.each([&](const Faction& f) {
    if (!first && f.isState()) first = f.id;
  });
  return first;
}
CommandReg cmdTechTree({"trees.tech", "Дерево технологий", "tech-tree", nullptr, [](App& a) { openTechTree(a, commandFaction(a)); },
                        [](App& a) { return a.ui.screen == Screen::Editor && commandFaction(a) != 0; }, false, "Вид", 50});
// Общее дерево технологий (изучение — за фракцию выделения, если она есть).
CommandReg cmdCommonTech({"trees.commonTech", "Общее дерево технологий", "globe", nullptr, [](App& a) { openTechTree(a, 0, 0, commandFaction(a)); },
                          [](App& a) { return a.ui.screen == Screen::Editor; }, false, "Вид", 50});

}  // namespace

void openTechTree(App& a, Id faction, Id tech, Id learner) {
  techtree::pending() = techtree::Pending{faction, tech, learner, true, techtree::pending().refit};
  a.openEditor("techtree", faction);
}

}  // namespace rg::app
