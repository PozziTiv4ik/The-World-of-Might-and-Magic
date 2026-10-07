// Regnum — полноэкранный редактор дерева технологий (ТЗ 1.b.v): дерево фракции или общее дерево (ТЗ «Доработки»,
// п.6–7), схема с панорамой и масштабом, карточки технологий (название, бонус, срок изучения, «галочка»
// изученности, ход исследования), связи-зависимости (несколько предшествующих, без циклов), перетаскивание
// карточек, создание и удаление, авторасстановка, копирование дерева другой фракции, панель свойств выбранной
// технологии. Общее дерево каждое государство изучает отдельно: «Изучение за» — чьё состояние показано и правится.
// В дереве фракции условия-общие технологии — плашки-ссылки слева от карточки (состояние для этой фракции).
// «Открывает постройки» — постройки, которым технология нужна (rules::techUnlocks).
#include <unordered_map>

#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/widgets.h"

namespace rg::app {

namespace {

using namespace tree;
using platform::Key;

constexpr float kW = 232, kH = 96;   // карточка технологии (единицы схемы)
constexpr float kSideW = 356;

// ---------------------------------------------------------------- состояние технологии
enum class St : u8 { Locked, Available, Research, Studied };

const char* stIcon(St s) {
  switch (s) {
    case St::Studied: return "check";
    case St::Research: return "hourglass";
    case St::Available: return "research";
    default: return "lock";
  }
}
const char* stName(St s) {
  switch (s) {
    case St::Studied: return "Изучена";
    case St::Research: return "Исследуется";
    case St::Available: return "Доступна";
    default: return "Закрыта";
  }
}
ui::Tone stTone(St s) {
  switch (s) {
    case St::Studied: return ui::Tone::Success;
    case St::Research: return ui::Tone::Info;
    case St::Available: return ui::Tone::Accent;
    default: return ui::Tone::Neutral;
  }
}
Color stColor(const Ink& k, St s) {
  switch (s) {
    case St::Studied: return k.success;
    case St::Research: return k.info;
    case St::Available: return k.accent;
    default: return k.textMuted;
  }
}

int turnsLeft(int turns, int progress) { return std::max(1, turns - std::max(0, progress)); }

// Состояние технологии для изучающей фракции who (технология своего дерева — 0 или её фракция; общая — фракция,
// за которую идёт изучение).
St stateOf(const World& w, Id tech, Id who, std::vector<Id>* missing = nullptr) {
  const TechProgress p = rules::techState(w, tech, who);
  if (p.studied) return St::Studied;
  rules::ResearchCheck rc = rules::canResearch(w, tech, who);
  if (missing) *missing = rc.missing;
  return p.research ? St::Research : (rc.ok ? St::Available : St::Locked);
}

std::string tname(const std::string& s) { return s.empty() ? std::string("Без названия") : s; }

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
Pending& pending() {
  static Pending p;
  return p;
}

double snap(double v) { return std::round(v / 4) * 4; }

Box2 boundsOf(const std::vector<TN>& ts) {
  Box2 b;
  for (const TN& t : ts) {
    b.add(t.pos);
    b.add({t.pos.x + kW, t.pos.y + kH});
  }
  return b;
}

const TN* find(const std::vector<TN>& ts, Id id) {
  for (const TN& t : ts)
    if (t.id == id) return &t;
  return nullptr;
}

// Название технологии этого дерева или другого (общая — условие уникальной).
std::string techLabel(const World& w, const std::vector<TN>& ts, Id id) {
  if (const TN* t = find(ts, id)) return tname(t->name);
  if (const Tech* t = w.tech(id)) return tname(t->name);
  return {};
}

std::string techNames(const World& w, const std::vector<TN>& ts, const std::vector<Id>& ids, size_t maxN = 3) {
  std::vector<std::string> n;
  for (Id id : ids) {
    if (n.size() >= maxN) {
      n.push_back("ещё " + std::to_string(ids.size() - maxN));
      break;
    }
    if (std::string s = techLabel(w, ts, id); !s.empty()) n.push_back(s);
  }
  return join(n, ", ");
}

// Краткий бонус: описание или эффекты модификаторов.
std::string bonusText(const World& w, const TN& t) {
  if (!trim(t.desc).empty()) return t.desc;
  std::vector<std::string> parts;
  for (Id mid : t.mods)
    if (const Modifier* m = w.modifier(mid))
      for (int f = 0; f < kFxCount; f++)
        if (m->has(Fx(f)) && m->fx[size_t(f)] != 0) parts.push_back(w::effectText(Fx(f), m->fx[size_t(f)]));
  return join(parts, "; ");
}

// Изучающая фракция общего дерева по умолчанию: основное игровое государство, иначе первое государство, иначе
// первая гильдия (по названию).
Id defaultLearner(const World& w) {
  std::vector<const Faction*> list;
  w.factions.each([&](const Faction& f) { list.push_back(&f); });
  std::sort(list.begin(), list.end(), [](const Faction* a, const Faction* b) {
    if (a->isState() != b->isState()) return a->isState();
    if (a->mainState != b->mainState) return a->mainState;
    return compareRu(a->name, b->name) < 0;
  });
  return list.empty() ? 0 : list.front()->id;
}

// Выбор изучающей фракции общего дерева (государства, затем гильдии). Доступен и при просмотре прошлого хода.
bool learnerSwitch(std::string_view id, Id& value) {
  const World& w = app().world();
  std::vector<const Faction*> list;
  w.factions.each([&](const Faction& f) { list.push_back(&f); });
  std::sort(list.begin(), list.end(), [](const Faction* a, const Faction* b) {
    if (a->kind != b->kind) return a->kind < b->kind;
    return compareRu(a->name, b->name) < 0;
  });
  std::vector<ui::Option> opts;
  int idx = -1;
  for (size_t i = 0; i < list.size(); i++) {
    opts.push_back(ui::Option{list[i]->name.empty() ? std::string_view("Без названия") : std::string_view(list[i]->name), nullptr, list[i]->color,
                              list[i]->isGuild() ? std::string_view("гильдия") : std::string_view(), false});
    if (list[i]->id == value) idx = int(i);
  }
  ui::ComboOpt co;
  co.placeholder = "Государство";
  co.icon = "crown";
  co.tooltip = "Изучение общих технологий за государство";
  if (!ui::combo(id, idx, std::span<const ui::Option>(opts), co)) return false;
  Id nv = idx >= 0 && idx < int(list.size()) ? list[size_t(idx)]->id : 0;
  if (!nv || nv == value) return false;
  value = nv;
  return true;
}

// ---------------------------------------------------------------- сцена (рисуется при сведении слоя)
struct Card {
  TN t;
  std::string bonus, need;
  bool sel = false, hover = false, dim = false, drop = false, dropBad = false;
  bool checkHot = false, outHot = false, inHot = false, hasIn = false, hasOut = false;
};
struct EdgeD {
  Curve k;
  Color col;
  float width = 2;
  bool hot = false, sel = false;
};
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
// Плашки внешних условий технологии t — по одной на условие, по центру входа.
std::vector<Ext> extsOf(const World& w, const std::vector<TN>& ts, const TN& t, Id faction) {
  std::vector<Ext> out;
  for (Id p : t.prereqs) {
    if (find(ts, p)) continue;
    const Tech* pt = w.tech(p);
    if (!pt) continue;
    Ext e;
    e.pre = p;
    e.tech = t.id;
    e.label = tname(pt->name);
    e.st = stateOf(w, p, faction);
    out.push_back(std::move(e));
  }
  const double h = 24, gap = 6;
  double y0 = t.pos.y + kH * 0.5 - (double(out.size()) * h + double(out.size() > 0 ? out.size() - 1 : 0) * gap) * 0.5;
  gfx::TextStyle st = textStyle(11.5f);
  for (size_t i = 0; i < out.size(); i++) {
    Ext& e = out[i];
    double tw = std::min(150.0, double(gfx::measureText(e.label, st)));
    double wd = tw + 26 + 12;
    e.port = {t.pos.x, t.pos.y + kH * 0.5};
    e.pill = RectF{float(t.pos.x - 40 - wd), float(y0 + double(i) * (h + gap)), float(wd), float(h)};
  }
  return out;
}

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

void drawArc(gfx::Canvas& c, float cx, float cy, float r, float frac, float width, Color col) {
  if (frac <= 0) return;
  gfx::Path p;
  int n = std::max(8, int(48 * frac));
  for (int i = 0; i <= n; i++) {
    float a = float(-kPi / 2 + 2 * kPi * double(frac) * double(i) / double(n));
    float x = cx + r * std::cos(a), y = cy + r * std::sin(a);
    if (i == 0) p.moveTo(x, y);
    else p.lineTo(x, y);
  }
  gfx::Stroke st;
  st.width = width;
  st.cap = gfx::Cap::Round;
  st.join = gfx::Join::Round;
  c.strokePath(p, st, gfx::Paint(col));
}

void drawCard(gfx::Canvas& c, const Scene& s, const Card& cd) {
  const Ink& k = s.k;
  const TN& t = cd.t;
  const float z = float(s.cam.z);
  const float px = 1 / z;   // одна точка интерфейса в единицах схемы
  RectF r{float(t.pos.x), float(t.pos.y), kW, kH};
  Color tone = stColor(k, t.st);
  c.save();
  if (cd.dim) c.setOpacity(0.28f);
  // Тень и подложка
  c.boxShadow(r, 12, cd.sel || cd.hover ? 26 : 14, 0, k.shadow.alpha(cd.sel || cd.hover ? 0.95f : 0.7f), gfx::Pt{0, 5});
  float tint = t.st == St::Locked ? 0.0f : (k.dark ? 0.11f : 0.08f);
  gfx::Gradient g;
  g.kind = gfx::Gradient::Linear;
  g.p0 = {r.x, r.y};
  g.p1 = {r.x, r.bottom()};
  g.stops = {{0.f, Color::mix(k.surface, tone, tint)}, {1.f, k.surface}};
  gfx::Paint gp;
  gp.gradient = &g;
  c.fillRoundRect(r, 12, gp);
  // Полоска состояния сверху
  c.save();
  c.clipRoundRect(r, 12);
  c.fillRect(RectF{r.x, r.y, r.w, 3}, tone.alpha(t.st == St::Locked ? 0.35f : 0.9f));
  c.restore();
  Color bc = cd.sel ? k.accent : cd.hover ? Color::mix(k.borderStrong, tone, 0.45f) : Color::mix(k.border, tone, t.st == St::Locked ? 0.f : 0.3f);
  c.strokeRoundRect(r.inset(0.5f * px), 12, (cd.sel ? 1.6f : 1.f) * px, bc);
  if (cd.sel) c.strokeRoundRect(r.expand(4 * px), 12 + 4 * px, 2 * px, k.accent.alpha(0.45f));
  if (cd.drop) c.strokeRoundRect(r.expand(4 * px), 12 + 4 * px, 2.2f * px, (cd.dropBad ? k.danger : k.success).alpha(0.9f));

  // Медальон состояния (у исследуемой — кольцо хода)
  float mx = r.x + 28, my = r.y + 30;
  c.fillCircle(mx, my, 17, tone.alpha(t.st == St::Locked ? 0.12f : 0.17f));
  if (t.st == St::Research) {
    c.strokeCircle(mx, my, 17, 2.4f, k.surfaceHi);
    drawArc(c, mx, my, 17, clamp(float(t.progress) / float(std::max(1, t.need)), 0.f, 1.f), 2.4f, tone);
  }
  icon(c, stIcon(t.st), RectF{mx - 9, my - 9, 18, 18}, tone);

  // Название, срок, модификаторы
  Color nameCol = t.st == St::Locked ? k.textDim : k.text;
  text(c, t.name.empty() ? "Без названия" : t.name, textStyle(14, gfx::FontWeight::Semibold), RectF{r.x + 54, r.y + 11, kW - 54 - 40, 20}, nameCol);
  {
    float x = r.x + 54, y = r.y + 33;
    icon(c, "hourglass", RectF{x, y + 1, 13, 13}, k.textMuted);
    std::string tt = t.st == St::Research ? "ещё " + nTurns(turnsLeft(t.need, t.progress)) : nTurns(t.need);
    gfx::TextStyle st = textStyle(11.5f);
    float tw = gfx::measureText(tt, st);
    text(c, tt, st, RectF{x + 17, y, tw + 2, 15}, k.textMuted);
    x += 17 + tw + 10;
    if (!t.mods.empty()) {
      icon(c, "sparkles", RectF{x, y + 1, 13, 13}, k.accent.alpha(0.9f));
      std::string mt = std::to_string(t.mods.size());
      text(c, mt, st, RectF{x + 16, y, 24, 15}, k.textMuted);
    }
  }
  // «Галочка» изученности (ТЗ 1.b.v)
  {
    RectF cb{r.right() - 31, r.y + 13, 18, 18};
    if (t.studied) {
      c.fillRoundRect(cb, 5, k.success);
      icon(c, "check", cb.inset(2), k.success.textOn());
    } else {
      if (cd.checkHot && !s.readOnly) c.fillRoundRect(cb, 5, k.accent.alpha(0.18f));
      c.strokeRoundRect(cb.inset(0.6f * px), 5, 1.4f * px, cd.checkHot && !s.readOnly ? k.accent : k.borderStrong);
    }
  }
  // Бонус и ход исследования
  gfx::TextStyle small = textStyle(11.5f);
  if (t.st == St::Research || (t.progress > 0 && !t.studied)) {
    std::string b = cd.bonus.empty() ? std::string("Без бонуса") : cd.bonus;
    text(c, b, small, RectF{r.x + 14, r.y + 56, kW - 28, 16}, cd.bonus.empty() ? k.textMuted : k.textDim);
    RectF bar{r.x + 14, r.y + 79, kW - 28 - 34, 5};
    c.fillRoundRect(bar, 2.5f, k.surfaceHi);
    float fr = clamp(float(t.progress) / float(std::max(1, t.need)), 0.f, 1.f);
    if (fr > 0) c.fillRoundRect(RectF{bar.x, bar.y, std::max(5.f, bar.w * fr), bar.h}, 2.5f, t.research ? k.info : k.textMuted);
    text(c, std::to_string(t.progress) + "/" + std::to_string(t.need), small, RectF{bar.right() + 4, bar.y - 6, 30, 16}, k.textMuted, gfx::Align::Right);
  } else if (!cd.bonus.empty()) {
    text(c, cd.bonus, small, RectF{r.x + 14, r.y + 54, kW - 28, 34}, k.textDim, gfx::Align::Left, 2, gfx::VAlign::Top);
  } else if (!cd.need.empty()) {
    text(c, cd.need, small, RectF{r.x + 14, r.y + 54, kW - 28, 34}, k.textMuted, gfx::Align::Left, 2, gfx::VAlign::Top);
  } else {
    text(c, "Без бонуса", small, RectF{r.x + 14, r.y + 56, kW - 28, 16}, k.textMuted);
  }
  // Гнёзда связей
  Vec2 in{r.x, r.y + kH * 0.5}, out{r.right(), r.y + kH * 0.5};
  if (cd.hasIn || cd.inHot || cd.hover || cd.sel) drawPort(c, in, cd.inHot ? k.accent : Color::mix(k.borderStrong, tone, 0.5f), k.bg, cd.hasIn, cd.inHot, z);
  if (cd.hasOut || cd.outHot || cd.hover || cd.sel) drawPort(c, out, cd.outHot ? k.accent : Color::mix(k.borderStrong, tone, 0.5f), k.bg, cd.hasOut, cd.outHot, z);
  c.restore();
}

void renderScene(const Scene& s, gfx::Canvas& c, RectF dev, float scale) {
  c.save();
  c.clipRoundRect(dev, 10 * scale);
  drawGrid(c, dev, scale, s.cam, s.k);
  applyCamera(c, dev, scale, s.cam);
  const float z = float(s.cam.z);
  for (const EdgeD& e : s.edges) {
    if (e.sel) drawCurve(c, e.k, s.k.accent.alpha(0.22f), e.width + 7, z, false);
    drawCurve(c, e.k, e.col, e.width, z, true);
  }
  // Условия из общего дерева: плашка цвета состояния, пунктир во вход карточки.
  for (const Ext& e : s.exts) {
    Color sc = stColor(s.k, e.st);
    Color col = e.hot ? s.k.accent : Color::mix(s.k.textMuted, sc, 0.5f);
    drawCurve(c, curve({e.pill.right(), e.pill.y + e.pill.h * 0.5}, e.port), col.alpha(0.85f), e.hot ? 2.6f : 1.8f, z, true, true);
    c.fillRoundRect(e.pill, e.pill.h * 0.5f, s.k.surface);
    c.strokeRoundRect(e.pill.inset(0.5f / z), e.pill.h * 0.5f, (e.hot ? 1.6f : 1.f) / z, e.hot ? s.k.accent : sc.alpha(0.6f));
    icon(c, stIcon(e.st), RectF{e.pill.x + 8, e.pill.y + (e.pill.h - 13) * 0.5f, 13, 13}, sc);
    text(c, e.label, textStyle(11.5f), RectF{e.pill.x + 26, e.pill.y, e.pill.w - 34, e.pill.h}, e.hot ? s.k.text : s.k.textDim);
  }
  for (const Card& cd : s.cards) drawCard(c, s, cd);
  if (s.pending) drawCurve(c, s.pend, s.pendCol, 2.2f, z, true, true);
  c.restore();
}

// ---------------------------------------------------------------- действия
void askDelete(App& a, Id tech, const std::string& name) {
  a.confirm("Удалить технологию «" + (name.empty() ? std::string("Без названия") : name) + "»?",
            "Связи с ней исчезнут, зависящие технологии и постройки перестанут её требовать. Действие можно отменить Ctrl+Z.", "Удалить", true,
            [tech](App& x) { x.act("Удалить технологию", [&](Tx& tx) { rules::removeTech(tx, tech); }); });
}

void removeLink(App& a, LinkSel l) {
  a.act("Удалить связь технологий", [&](Tx& tx) { rules::setPrereq(tx, l.tech, l.pre, false); });
}

// Изучение и исследование — за who (общая технология — изучающая фракция; своего дерева — 0).
void setStudied(App& a, Id tech, bool on, Id who) {
  a.act(on ? "Отметить изученной" : "Снять отметку изучения", [&](Tx& tx) { rules::setStudied(tx, tech, on, who); });
}
void startResearch(App& a, Id tech, Id who) {
  a.act("Начать исследование", [&](Tx& tx) { rules::startResearch(tx, tech, who); });
}
void stopResearch(App& a, Id tech, Id who) {
  a.act("Остановить исследование", [&](Tx& tx) { rules::stopResearch(tx, tech, who); });
}

Id createAt(App& a, Id faction, Vec2 center) {
  Id nid = 0;
  Vec2 p{snap(center.x - kW * 0.5), snap(center.y - kH * 0.5)};
  if (!a.act("Новая технология", [&](Tx& tx) {
        nid = rules::createTech(tx, faction, "Новая технология");
        tx.tech(nid).pos = p;
      }))
    return 0;
  return nid;
}

// Копия дерева другой фракции (ТЗ 1.b.v: деревья редактируются и дополняются). Копии ставятся ниже дерева.
void copyTree(App& a, Id from, Id to) {
  const World& w = a.world();
  const Faction* f = w.faction(from);
  if (!f) return;
  int n = 0;
  w.techs.each([&](const Tech& t) { n += t.faction == from; });
  std::string fname = f->name;
  if (a.act("Скопировать дерево технологий", [&](Tx& tx) { rules::copyTechTree(tx, from, to); })) {
    a.toast("Добавлено " + std::to_string(n) + " " + plural(n, "технология", "технологии", "технологий") + " из дерева «" + fname + "»",
            ToastKind::Success, "copy");
    pending().refit = to;
  }
}

void copyMenu(App& a, Id faction, bool nonEmpty) {
  if (!ui::beginMenu("copyfrom")) return;
  const World& w = a.world();
  const bool ro = a.readOnly();
  ui::menuHeader("Добавить копию дерева");
  std::vector<const Faction*> fs;
  w.factions.each([&](const Faction& f) {
    if (f.id != faction) fs.push_back(&f);
  });
  std::sort(fs.begin(), fs.end(), [](const Faction* x, const Faction* y) {
    if (x->kind != y->kind) return x->kind < y->kind;
    return compareRu(x->name, y->name) < 0;
  });
  for (const Faction* f : fs) {
    int n = 0;
    w.techs.each([&](const Tech& t) { n += t.faction == f->id; });
    ui::IdScope s{i64(f->id)};
    std::string label = (f->name.empty() ? std::string("Без названия") : f->name) + " · " + std::to_string(n);
    if (ui::menuItem(label, {.icon = f->isGuild() ? "guild" : "crown", .disabled = n == 0 || ro})) {
      Id from = f->id;
      if (!nonEmpty) {
        copyTree(a, from, faction);
      } else {
        // В дереве уже есть технологии — копии добавятся к ним: спросить.
        a.confirm("Добавить копию дерева «" + f->name + "»?",
                  std::to_string(n) + " " + plural(n, "технология", "технологии", "технологий") +
                      " появятся ниже текущего дерева — неизученными, со своими связями. Действие можно отменить Ctrl+Z.",
                  "Добавить", false, [from, faction](App& x) { copyTree(x, from, faction); });
      }
    }
    a.markUi("tt.copy." + std::to_string(f->id));
  }
  if (fs.empty()) ui::menuItem("Других фракций нет", {.disabled = true});
  ui::endMenu();
}

// Ближайшая технология в направлении (стрелки клавиатуры).
Id neighbor(const std::vector<TN>& ts, const TN& cur, int dx, int dy) {
  // Сначала — по связям.
  if (dx < 0 && !cur.prereqs.empty()) {
    Id best = 0;
    double bd = kInf;
    for (Id p : cur.prereqs)
      if (const TN* t = find(ts, p); t && std::fabs(t->pos.y - cur.pos.y) < bd) {
        bd = std::fabs(t->pos.y - cur.pos.y);
        best = p;
      }
    if (best) return best;
  }
  if (dx > 0) {
    Id best = 0;
    double bd = kInf;
    for (const TN& t : ts)
      if (std::find(t.prereqs.begin(), t.prereqs.end(), cur.id) != t.prereqs.end() && std::fabs(t.pos.y - cur.pos.y) < bd) {
        bd = std::fabs(t.pos.y - cur.pos.y);
        best = t.id;
      }
    if (best) return best;
  }
  Id best = 0;
  double bs = kInf;
  for (const TN& t : ts) {
    if (t.id == cur.id) continue;
    double ddx = t.pos.x - cur.pos.x, ddy = t.pos.y - cur.pos.y;
    double along = dx ? ddx * dx : ddy * dy;
    double across = dx ? std::fabs(ddy) : std::fabs(ddx);
    if (along <= 1) continue;
    double score = along + across * 2.5;
    if (score < bs) {
      bs = score;
      best = t.id;
    }
  }
  return best;
}

// ---------------------------------------------------------------- панель свойств
// Общее дерево: изучение по фракциям — сколько изучено и исследуется; щелчок — изучение за неё.
void learnersSection(App& a, Ed& ed, const std::vector<TN>& ts) {
  const World& w = a.world();
  std::vector<const Faction*> list;
  w.factions.each([&](const Faction& f) { list.push_back(&f); });
  std::sort(list.begin(), list.end(), [](const Faction* x, const Faction* y) {
    if (x->kind != y->kind) return x->kind < y->kind;
    return compareRu(x->name, y->name) < 0;
  });
  if (list.empty()) return;
  ui::Section s("Изучение по государствам", "crown", {.defaultOpen = list.size() <= 12, .badge = std::to_string(list.size())});
  if (!s) return;
  for (const Faction* f : list) {
    int studied = 0, research = 0;
    for (const TN& t : ts) {
      const TechProgress p = rules::techState(w, t.id, f->id);
      studied += p.studied;
      research += p.research && !p.studied;
    }
    ui::IdScope fs{i64(f->id)};
    std::string hint = std::to_string(studied) + " / " + std::to_string(ts.size());
    std::string sub = research ? std::to_string(research) + " " + plural(research, "исследуется", "исследуются", "исследуются") : std::string();
    if (ui::listItem(f->name.empty() ? std::string("Без названия") : f->name,
                     {.dot = f->color, .subtitle = sub, .hint = hint, .selected = ed.learner == f->id, .tooltip = "Изучение за это государство"}))
      ed.learner = f->id;
    a.markUi("tt.learner." + std::to_string(f->id));
  }
}

void sideOverview(App& a, Ed& ed, Id faction, const std::vector<TN>& ts) {
  const World& w = a.world();
  const bool common = faction == 0;
  const Faction* f = w.faction(common ? ed.learner : faction);
  int studied = 0, research = 0, avail = 0;
  for (const TN& t : ts) {
    studied += t.studied;
    research += t.st == St::Research;
    avail += t.st == St::Available;
  }
  {
    ui::Row r({ui::px(54), ui::fr(1)}, 40, 12);
    {
      RectF fr = ui::next(54, 40);
      if (common) {
        ui::draw::rect(RectF{fr.x, fr.y + 2, 54, 36}, ui::theme().accent.alpha(0.14f), 7);
        ui::draw::icon("globe", RectF{fr.x + 15, fr.y + 8, 24, 24}, ui::theme().accent);
      } else {
        ui::at(RectF{fr.x, fr.y + 2, 54, 36});
        ui::flag(ed.flag, 54, 36, 5);
      }
    }
    ui::Group g(0, 0);
    if (common) {
      ui::caption("Общее дерево");
      ui::label(f ? "Изучение за: " + f->name : std::string("Изучать некому"), {.font = ui::Font::Subtitle});
    } else {
      ui::caption(f && f->isGuild() ? "Торговая гильдия" : "Государство");
      ui::label(f ? f->name : std::string("—"), {.font = ui::Font::Subtitle});
    }
  }
  ui::spacer(4);
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 10);
    ui::stat(std::to_string(studied) + " / " + std::to_string(ts.size()), "Изучено", {.icon = "check-circle", .tone = ui::Tone::Success});
    ui::stat(std::to_string(research), "Исследуется", {.icon = "hourglass", .tone = ui::Tone::Info});
  }
  if (!ts.empty()) ui::progress(double(studied) / double(ts.size()), {.tone = ui::Tone::Success, .height = 5, .label = true});
  if (research > 0) {
    if (ui::Section s("Исследуются", "hourglass", {.badge = std::to_string(research)}); s) {
      for (const TN& t : ts) {
        if (t.st != St::Research) continue;
        ui::IdScope sc{i64(t.id)};
        if (ui::listItem(t.name, {.icon = "research", .subtitle = "Осталось " + nTurns(turnsLeft(t.need, t.progress)),
                                  .hint = std::to_string(t.progress) + "/" + std::to_string(t.need)})) {
          ed.sel = t.id;
          ed.revealSel = true;
        }
        ui::progress(double(t.progress) / double(std::max(1, t.need)), {.tone = ui::Tone::Info, .height = 4});
      }
    }
  }
  if (avail > 0) {
    if (ui::Section s("Можно исследовать", "research", {.badge = std::to_string(avail)}); s) {
      for (const TN& t : ts) {
        if (t.st != St::Available) continue;
        ui::IdScope sc{i64(t.id)};
        if (ui::listItem(t.name, {.icon = "research", .hint = nTurns(turnsLeft(t.need, t.progress))})) {
          ed.sel = t.id;
          ed.revealSel = true;
        }
      }
    }
  }
  if (common) learnersSection(a, ed, ts);
  if (ui::Section s("Обозначения", "info", {.defaultOpen = ts.size() < 3}); s) {
    for (St st : {St::Studied, St::Research, St::Available, St::Locked}) {
      ui::IdScope sc{int(st)};
      ui::Row r({ui::px(18), ui::fr(1)}, 22, 8);
      ui::iconColored(stIcon(st), ui::toneColor(stTone(st)), 16);
      ui::label(stName(st), {.ink = ui::Ink::Dim});
    }
  }
}

void sideLink(App& a, Ed& ed, const std::vector<TN>& ts) {
  const TN* pre = find(ts, ed.link.pre);
  const TN* tech = find(ts, ed.link.tech);
  if (!pre || !tech) return;
  ui::caption("Зависимость");
  {
    // Требуемая → зависимая, по строке на каждую (названия бывают длинными).
    ui::Group g(0, 4);
    if (ui::chip(pre->name + "##pre", {.icon = stIcon(pre->st), .tone = stTone(pre->st), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
      ed.sel = pre->id;
      ed.link = {};
      ed.revealSel = true;
    }
    {
      ui::Indent in(10);
      ui::icon("arrow-down", ui::Ink::Muted, 16);
    }
    if (ui::chip(tech->name + "##tech", {.icon = stIcon(tech->st), .tone = stTone(tech->st), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
      ed.sel = tech->id;
      ed.link = {};
      ed.revealSel = true;
    }
  }
  ui::text("Для изучения «" + tech->name + "» нужна «" + pre->name + "».", ui::Font::Small, ui::Ink::Dim);
  ui::spacer(4);
  ui::Disabled d(a.readOnly());
  if (ui::button("Удалить связь", {.variant = ui::Variant::Danger, .icon = "unlink", .fill = true, .tooltip = "Удалить зависимость (Delete)"})) {
    removeLink(a, ed.link);
    ed.link = {};
  }
  a.markUi("tt.side.unlink");
}

// «Открывает постройки» (ТЗ «Доработки», п.4): постройки, которым технология нужна. Общей технологии можно назначить
// любую постройку, технологии государства — только его уникальные постройки (rules::canRequireTech).
void unlocksSection(App& a, const TN& t, bool ro) {
  const World& w = a.world();
  const Tech* rec = w.tech(t.id);
  if (!rec) return;
  const Id tid = t.id, tfaction = rec->faction;
  struct B {
    Id id = 0, owner = 0;
    std::string name;
    const char* icon = nullptr;
    BuildingCat cat = BuildingCat::Economic;
  };
  std::vector<B> have, cand;
  const std::vector<Id> unlocks = rules::techUnlocks(w, tid);
  w.buildings.each([&](const Building& b) {
    B x{b.id, b.owner, b.name.empty() ? std::string("Без названия") : b.name, bld::iconOf(b), b.cat};
    if (std::find(unlocks.begin(), unlocks.end(), b.id) != unlocks.end()) have.push_back(std::move(x));
    else if (rules::canRequireTech(w, b.id, tid)) cand.push_back(std::move(x));
  });
  if (have.empty() && cand.empty()) return;   // некому открывать (гильдия: построек нет)
  ui::Section s("Открывает постройки", "building", {.badge = have.empty() ? std::string() : std::to_string(have.size())});
  a.markUi("tt.side.unlocks");
  if (!s) return;
  if (!have.empty()) {
    ui::IdScope us("unlocks");
    tree::ChipFlow flow;
    for (const B& b : have) {
      ui::IdScope s2{i64(b.id)};
      std::string tip = (b.owner ? "Уникальная · " + w.factionName(b.owner) : std::string("Общее дерево")) + " — показать в дереве построек";
      ui::ChipAction act = tree::chip(b.name, {.icon = b.icon, .color = bld::catColor(b.cat), .removable = !ro, .clickable = true, .tooltip = tip});
      a.markUi("tt.unlock." + std::to_string(b.id));
      if (act == ui::ChipAction::Click) {
        openBuildingTree(a, b.owner, b.id);
      } else if (act == ui::ChipAction::Remove) {
        const Id bid = b.id;
        a.act("Убрать постройку из открываемых", [&](Tx& tx) { rules::setBuildingTech(tx, bid, tid, false); });
      }
    }
  }
  if (ro || cand.empty()) return;
  // Общей технологии — постройки общего дерева первыми, затем уникальные по государствам.
  std::stable_sort(cand.begin(), cand.end(), [&](const B& x, const B& y) {
    if ((x.owner == 0) != (y.owner == 0)) return x.owner == 0;
    if (x.owner != y.owner) return compareRu(w.factionName(x.owner), w.factionName(y.owner)) < 0;
    if (x.cat != y.cat) return x.cat < y.cat;
    return compareRu(x.name, y.name) < 0;
  });
  std::vector<std::string> hints;
  for (const B& b : cand) hints.push_back(b.owner ? w.factionName(b.owner) : std::string(bld::catName(b.cat)));
  std::vector<ui::Option> opts;
  for (size_t i = 0; i < cand.size(); i++) opts.push_back(ui::Option{cand[i].name, cand[i].icon, Color(0, 0, 0, 0), hints[i]});
  int idx = -1;
  if (ui::combo("addunlock", idx, std::span<const ui::Option>(opts), {.placeholder = "Добавить постройку", .search = 1, .icon = "plus"}) && idx >= 0 &&
      idx < int(cand.size())) {
    const Id bid = cand[size_t(idx)].id;
    a.act(tfaction ? "Технология открывает постройку" : "Общая технология открывает постройку", [&](Tx& tx) { rules::setBuildingTech(tx, bid, tid, true); });
  }
  a.markUi("tt.side.addunlock");
}

void sideTech(App& a, Ed& ed, Id faction, Id who, const std::vector<TN>& ts, const TN& t) {
  const World& w = a.world();
  const bool ro = a.readOnly();
  const bool common = faction == 0;
  const Color none(0, 0, 0, 0);
  ui::IdScope scope{i64(t.id)};
  {
    ui::HStack hs(28, ui::Align::Left, 6);
    ui::tag(stName(t.st), stTone(t.st), stIcon(t.st));
    if (common) ui::tag("Общая", ui::Tone::Accent, "globe");
    ui::flex();
    if (ui::iconButton("target", "Показать на схеме")) ed.revealSel = true;
    if (ui::iconButton("trash", "Удалить технологию", {.disabled = ro, .shortcut = {Key::Delete, 0}, .tone = ui::Tone::Danger})) askDelete(a, t.id, t.name);
    a.markUi("tt.side.delete");
  }
  {
    ui::Disabled dis(ro);
    // Название
    if (ed.focusName == t.id) {
      ui::setKeyboardFocus(ui::id("name"));
      ed.focusName = 0;
    }
    std::string name = t.name;
    if (ui::textField("name", name, {.placeholder = "Название технологии", .maxLength = 80, .selectAllOnFocus = true}) && !trim(name).empty() &&
        trim(name) != t.name) {
      std::string n = trim(name);
      a.act("Переименовать технологию", [&](Tx& tx) { tx.tech(t.id).name = n; });
    }
    a.markUi("tt.side.name");
  }
  {
    // «Галочка» изученности (ТЗ 1.b.v); у общей — за изучающую фракцию.
    ui::Disabled dis(ro || (common && !who));
    bool studied = t.studied;
    if (ui::checkbox("Изучена", studied)) setStudied(a, t.id, studied, who);
    a.markUi("tt.side.studied");
  }
  // Исследование
  {
    ui::Card card({.pad = 12, .tone = stTone(t.st)});
    if (common) {
      if (const Faction* lf = w.faction(who)) {
        ui::HStack hs(22, ui::Align::Left, 6);
        ui::icon(lf->isGuild() ? "guild" : "crown", ui::Ink::Muted, 14, "Изучение за");
        ui::label(lf->name, {.font = ui::Font::Small, .color = lf->color});
      } else {
        ui::label("Изучать некому", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "crown"});
      }
    }
    if (t.studied) {
      ui::label("Технология изучена — её бонусы действуют.", {.ink = ui::Ink::Success, .icon = "check-circle", .wrap = true});
    } else if (t.research) {
      ui::Row r({ui::fr(1), ui::px(60)}, 22, 8);
      ui::label("Исследуется · ещё " + nTurns(turnsLeft(t.need, t.progress)), {.font = ui::Font::Strong, .ink = ui::Ink::Info});
      ui::label(std::to_string(t.progress) + " / " + std::to_string(t.need), {.ink = ui::Ink::Dim, .align = ui::Align::Right});
    } else if (t.st == St::Available) {
      ui::label("Условия выполнены. Изучение займёт " + nTurns(turnsLeft(t.need, t.progress)) + ".", {.ink = ui::Ink::Dim, .wrap = true});
    } else if (!t.missing.empty()) {
      ui::label("Сначала изучите:", {.ink = ui::Ink::Dim, .icon = "lock"});
      ui::IdScope ms("missing");
      tree::ChipFlow flow;
      for (Id m : t.missing) {
        ui::IdScope s2{i64(m)};
        if (const TN* mt = find(ts, m)) {
          if (tree::chip(mt->name, {.icon = stIcon(mt->st), .tone = stTone(mt->st), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
            ed.sel = m;
            ed.revealSel = true;
          }
        } else if (const Tech* x = w.tech(m)) {
          // Общая технология — условие технологии фракции: состояние для этой фракции, переход в общее дерево.
          St ms2 = stateOf(w, m, faction);
          if (tree::chip(tname(x->name), {.icon = stIcon(ms2), .tone = stTone(ms2), .clickable = true, .tooltip = "Общее дерево — показать"}) == ui::ChipAction::Click)
            openTechTree(a, 0, m, faction);
        }
      }
    }
    if (t.research) {
      ui::progress(double(t.progress) / double(std::max(1, t.need)), {.tone = ui::Tone::Info, .height = 6});
      if (ui::button("Остановить исследование", {.icon = "close", .fill = true, .disabled = ro})) stopResearch(a, t.id, who);
      a.markUi("tt.side.research");
    } else if (!t.studied) {
      if (t.progress > 0) ui::progress(double(t.progress) / double(std::max(1, t.need)), {.tone = ui::Tone::Neutral, .height = 4});
      bool can = t.st == St::Available;
      if (ui::button("Начать исследование", {.variant = ui::Variant::Primary, .icon = "play", .fill = true, .disabled = ro || !can,
                                              .tooltip = can ? std::string_view("Исследование продвигается на один ход при завершении хода")
                                                             : std::string_view("Не изучены предшествующие технологии")}))
        startResearch(a, t.id, who);
      a.markUi("tt.side.research");
    }
  }
  {
    ui::Disabled dis(ro);
    // Срок изучения
    ui::prop("Срок изучения", "hourglass");
    int turns = t.turns;
    if (ui::numberField("turns", turns, {.min = 1, .max = 999, .unit = "ход|хода|ходов", .steppers = true}))
      a.act("Срок изучения технологии", [&](Tx& tx) { tx.tech(t.id).turns = std::max(1, turns); }, {.coalesce = "tt-turns:" + std::to_string(t.id)});
    a.markUi("tt.side.turns");
    if (t.need != t.turns) {   // срок с модификатором государства
      ui::label("с модификаторами — " + nTurns(t.need),
                {.font = ui::Font::Small, .ink = ui::Ink::Muted, .align = ui::Align::Right, .icon = "hourglass",
                 .tooltip = "Модификатор «Время исследования технологий» государства"});
      a.markUi("tt.side.need");
    }
    // Бонус (описание)
    const Faction* owner = w.faction(faction);
    ui::caption(common ? "Бонус изучившему" : owner && owner->isGuild() ? "Бонус для гильдии" : "Бонус для государства");
    std::string desc = t.desc;
    if (ui::textArea("desc", desc, 76, {.placeholder = "Что даёт технология: «+10 % к торговле», «осадные орудия»…", .maxLength = 600}) && desc != t.desc)
      a.act("Описание технологии", [&](Tx& tx) { tx.tech(t.id).desc = desc; });
    a.markUi("tt.side.desc");
  }
  // Модификаторы и их эффекты (бонус технологии в расчётах)
  ui::caption("Модификаторы");
  std::vector<Id> mods = t.mods;
  if (tree::modifierList("mods", mods, ro)) a.act("Модификаторы технологии", [&](Tx& tx) { tx.tech(t.id).modifiers = mods; });
  a.markUi("tt.side.mods");
  {
    ui::IdScope fs("fx");
    tree::effectChips(w, t.mods);
  }
  // Зависимости. У технологии фракции условиями могут быть и общие технологии (ТЗ «Доработки», п.7); у общей — только
  // общие.
  if (ui::Section s("Требует", "link", {.badge = t.prereqs.empty() ? std::string() : std::to_string(t.prereqs.size())}); s) {
    if (!t.prereqs.empty()) {
      ui::IdScope ps("pre");
      tree::ChipFlow flow;
      for (Id p : t.prereqs) {
        ui::IdScope s2{i64(p)};
        if (const TN* pt = find(ts, p)) {
          ui::ChipAction ca = tree::chip(pt->name, {.icon = stIcon(pt->st), .tone = stTone(pt->st), .removable = !ro, .clickable = true, .tooltip = "Выбрать"});
          if (ca == ui::ChipAction::Click) {
            ed.sel = p;
            ed.revealSel = true;
          } else if (ca == ui::ChipAction::Remove) {
            removeLink(a, {p, t.id});
          }
        } else if (const Tech* x = w.tech(p)) {
          St ps2 = stateOf(w, p, faction);
          ui::ChipAction ca = tree::chip(tname(x->name) + "##common",
                                         {.icon = stIcon(ps2), .tone = stTone(ps2), .removable = !ro, .clickable = true, .tooltip = "Общее дерево — показать"});
          a.markUi("tt.side.pre." + std::to_string(p));
          if (ca == ui::ChipAction::Click) openTechTree(a, 0, p, faction);
          else if (ca == ui::ChipAction::Remove) removeLink(a, {p, t.id});
        }
      }
    } else {
      ui::label("Нет условий — можно изучать сразу.", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    }
    // Добавить условие: технологии этого дерева (по порядку), затем — у дерева фракции — общие. Цикл недоступен.
    struct Cand {
      Id id = 0;
      std::string label;
      const char* icon = nullptr;
      bool bad = false, common = false;
    };
    std::vector<Cand> cand;
    for (const TN& o : ts) {
      if (o.id == t.id || std::find(t.prereqs.begin(), t.prereqs.end(), o.id) != t.prereqs.end()) continue;
      cand.push_back(Cand{o.id, o.name.empty() ? std::string("Без названия") : o.name, stIcon(o.st), rules::wouldCycle(w, t.id, o.id), false});
    }
    if (!common) {
      std::vector<Cand> extra;
      w.techs.each([&](const Tech& o) {
        if (o.faction != 0 || std::find(t.prereqs.begin(), t.prereqs.end(), o.id) != t.prereqs.end()) return;
        extra.push_back(Cand{o.id, tname(o.name), stIcon(stateOf(w, o.id, faction)), rules::wouldCycle(w, t.id, o.id), true});
      });
      std::stable_sort(extra.begin(), extra.end(), [](const Cand& x, const Cand& y) { return compareRu(x.label, y.label) < 0; });
      for (Cand& c : extra) cand.push_back(std::move(c));
    }
    if (!cand.empty()) {
      std::vector<ui::Option> opts;
      for (const Cand& c : cand)
        opts.push_back(ui::Option{c.label, c.icon, none, c.bad ? std::string_view("цикл") : c.common ? std::string_view("общая") : std::string_view(), c.bad});
      int idx = -1;
      if (ui::combo("addpre", idx, std::span<const ui::Option>(opts), {.placeholder = "Добавить условие", .search = 1, .icon = "plus", .disabled = ro}) && idx >= 0 &&
          idx < int(cand.size())) {
        Id pre = cand[size_t(idx)].id;
        a.act("Связать технологии", [&](Tx& tx) { rules::setPrereq(tx, t.id, pre, true); });
      }
      a.markUi("tt.side.addpre");
    }
  }
  // Открывает: технологии этого дерева; у общей — и технологии фракций, которым она нужна.
  {
    struct Dep {
      Id id = 0, faction = 0;
      std::string name;
      St st = St::Locked;
    };
    std::vector<Dep> deps;
    for (const TN& o : ts)
      if (std::find(o.prereqs.begin(), o.prereqs.end(), t.id) != o.prereqs.end()) deps.push_back(Dep{o.id, faction, o.name, o.st});
    if (common)
      w.techs.each([&](const Tech& o) {
        if (o.faction && std::find(o.prereqs.begin(), o.prereqs.end(), t.id) != o.prereqs.end())
          deps.push_back(Dep{o.id, o.faction, tname(o.name), stateOf(w, o.id, o.faction)});
      });
    if (!deps.empty()) {
      if (ui::Section s("Открывает", "arrow-right", {.badge = std::to_string(deps.size())}); s) {
        ui::IdScope ds("deps");
        tree::ChipFlow flow;
        for (const Dep& d : deps) {
          ui::IdScope s2{i64(d.id)};
          if (d.faction == faction) {
            if (tree::chip(d.name, {.icon = stIcon(d.st), .tone = stTone(d.st), .clickable = true, .tooltip = "Выбрать"}) == ui::ChipAction::Click) {
              ed.sel = d.id;
              ed.revealSel = true;
            }
          } else {
            const std::string tip = w.factionName(d.faction) + " — показать в её дереве";
            if (tree::chip(d.name, {.color = w::factionColor(w, d.faction), .clickable = true, .tooltip = tip}) == ui::ChipAction::Click)
              openTechTree(a, d.faction, d.id);
            a.markUi("tt.dep." + std::to_string(d.id));
          }
        }
      }
    }
  }
  unlocksSection(a, t, ro);
}

// ---------------------------------------------------------------- редактор
void drawTechTree(App& a, Id faction) {
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  const bool ro = a.readOnly();
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
  if (common && (!ed.learner || !w.faction(ed.learner))) ed.learner = defaultLearner(w);
  const Id who = common ? ed.learner : 0;   // за кого изучение (своё дерево — 0)

  // Снимок дерева на кадр. Срок с модификатором государства одинаков для одинаковых базовых сроков — один расчёт
  // эффектов на значение.
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
    ts.push_back(std::move(n));
  });
  for (TN& t : ts) {
    if (t.studied) t.st = St::Studied;
    else {
      rules::ResearchCheck rc = rules::canResearch(w, t.id, who);
      t.missing = rc.missing;
      t.st = t.research ? St::Research : (rc.ok ? St::Available : St::Locked);
    }
    t.match = ed.query.empty() || utf8::matches(t.name, ed.query) || utf8::matches(t.desc, ed.query);
  }
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
  int studiedN = 0, researchN = 0;
  for (const TN& t : ts) {
    studiedN += t.studied;
    researchN += t.st == St::Research;
  }

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

  // ---- панель инструментов
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
    if (ro) ui::tag("Ход " + std::to_string(a.ui.viewTurn.value_or(0)) + " · только просмотр", ui::Tone::Warning, "lock");
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

  // ---- холст: ввод
  a.markUi("tt.canvas", canvas);
  const ui::Mouse& mouse = ui::mouse();
  ui::Interaction bg = ui::interact(ui::id("bg"), canvas, ui::IfAllowOverlap | ui::IfMiddleButton | ui::IfRightButton);
  if (bg.pressed) ed.bgDragged = false;
  if (bg.dragging) ed.bgDragged = true;
  Vec2 mw = toWorld(ed.cam, canvas, mouse.x, mouse.y);

  Id hoverNode = 0, hoverCheck = 0, hoverOut = 0, hoverIn = 0;
  bool connReleased = false, openMenu = false;
  // Выбранная карточка — последней (сверху).
  std::vector<size_t> order(ts.size());
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
  // Кнопку отпустили вне карточки (перетаскивание прервано) — закончить.
  if (ed.drag && !mouse.down[0]) {
    ed.drag = 0;
    a.store.endCoalesce();
  }
  // Условия из общего дерева (у дерева фракции): плашки слева от карточек; щелчок — общее дерево.
  std::vector<Ext> exts;
  if (!common)
    for (const TN& t : ts)
      for (Ext& e : extsOf(w, ts, t, faction)) exts.push_back(std::move(e));
  Id openExt = 0;
  int hoverExt = -1;
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
  // Цель связи под указателем.
  Id dropTarget = 0;
  bool dropBad = false;
  std::string dropWhy;
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
  // Связь под указателем (только над фоном).
  LinkSel hoverLink;
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
      if (t->research) {
        if (ui::menuItem("Остановить исследование", {.icon = "close", .disabled = ro || noWho})) stopResearch(a, tid, who);
      } else if (!t->studied) {
        if (ui::menuItem("Начать исследование", {.icon = "play", .disabled = ro || noWho || t->st != St::Available})) startResearch(a, tid, who);
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

  // ---- холст: сцена
  auto sc = std::make_shared<Scene>();
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
  ui::custom(canvas, [sc](gfx::Canvas& c, RectF dev, float scale) { renderScene(*sc, c, dev, scale); });
  ui::draw::rectStroke(canvas, th.border, 10, 1);

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

  // ---- панель свойств
  {
    ui::draw::rect(side, th.surface2, th.radiusCard);
    ui::draw::rectStroke(side, th.border, th.radiusCard, 1);
    ui::Area ar(side.inset(14, 12), 0);
    ui::Scroll scroll("side");
    if (const TN* t = find(ts, ed.sel)) sideTech(a, ed, faction, who, ts, *t);
    else if (ed.link) sideLink(a, ed, ts);
    else sideOverview(a, ed, faction, ts);
  }

  // ---- карточка сведений при наведении (карточка технологии или плашка общей технологии)
  Id tipId = (hoverNode && !ed.drag && !ed.conn && !ui::anyModalOpen()) ? hoverNode : 0;
  const Ext* tipExt = hoverExt >= 0 && !ui::anyModalOpen() ? &exts[size_t(hoverExt)] : nullptr;
  u64 tipKey = tipExt ? hash64("tt-ext") ^ (u64(tipExt->tech) << 32 | tipExt->pre) : tipId ? hash64("tt") ^ tipId : 0;
  const bool tipShow = hoverDelay(tipKey);
  if (tipShow && tipExt) {
    if (const Tech* pt = w.tech(tipExt->pre)) {
      std::vector<Id> missing;
      St st = stateOf(w, pt->id, faction, &missing);
      Tip tip;
      tip.title = tname(pt->name);
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
  if (openExt) openTechTree(a, 0, openExt, faction);
}

EditorReg regTechTree({"techtree", "Дерево технологий", drawTechTree, "tech-tree"});

// Команда палитры: дерево технологий выделенной фракции (или первого государства).
Id commandFaction(App& a) {
  const World& w = a.world();
  if (a.ui.sel.type == SelType::Faction && w.faction(a.ui.sel.id)) return a.ui.sel.id;
  if (a.ui.sel.type == SelType::Province)
    if (const Province* p = w.province(a.ui.sel.id); p && w.faction(p->owner)) return p->owner;
  Id first = 0;
  w.factions.each([&](const Faction& f) {
    if (!first && f.isState()) first = f.id;
  });
  return first;
}
CommandReg cmdTechTree({"trees.tech", "Дерево технологий", "tech-tree", nullptr, [](App& a) { openTechTree(a, commandFaction(a)); },
                        [](App& a) { return a.ui.screen == Screen::Editor && commandFaction(a) != 0; }, false, "Вид"});
// Общее дерево технологий (изучение — за фракцию выделения, если она есть).
CommandReg cmdCommonTech({"trees.commonTech", "Общее дерево технологий", "globe", nullptr, [](App& a) { openTechTree(a, 0, 0, commandFaction(a)); },
                          [](App& a) { return a.ui.screen == Screen::Editor; }, false, "Вид"});

}  // namespace

void openTechTree(App& a, Id faction, Id tech, Id learner) {
  pending() = Pending{faction, tech, learner, true, pending().refit};
  a.openEditor("techtree", faction);
}

}  // namespace rg::app
