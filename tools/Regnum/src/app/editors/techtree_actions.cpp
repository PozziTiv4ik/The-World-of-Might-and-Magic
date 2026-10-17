// Regnum — дерево технологий: состояние технологии для изучающей фракции, копия дерева, подписи и действия (создать,
// удалить, связи, изучение, копия дерева другой фракции, сосед для стрелок) — techtree_internal.h.
#include "app/editors/techtree_internal.h"

namespace rg::app::techtree {

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
St stateOf(const World& w, Id tech, Id who, std::vector<Id>* missing) {
  const TechProgress p = rules::techState(w, tech, who);
  if (p.studied) return St::Studied;
  rules::ResearchCheck rc = rules::canResearch(w, tech, who);
  if (missing) *missing = rc.missing;
  return p.research ? St::Research : (rc.ok ? St::Available : St::Locked);
}

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

namespace {

// Название технологии этого дерева или другого (общая — условие уникальной).
std::string techLabel(const World& w, const std::vector<TN>& ts, Id id) {
  if (const TN* t = find(ts, id)) return w::orName(t->name);
  if (const Tech* t = w.tech(id)) return w::orName(t->name);
  return {};
}

}  // namespace

std::string techNames(const World& w, const std::vector<TN>& ts, const std::vector<Id>& ids, size_t maxN) {
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
  w.factions.each([&](const Faction& f) {
    if (!f.isWild()) list.push_back(&f);   // «Без государства» технологий не изучает
  });
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
  w.factions.each([&](const Faction& f) {
    if (!f.isWild()) list.push_back(&f);   // «Без государства» технологий не изучает
  });
  std::sort(list.begin(), list.end(), [](const Faction* a, const Faction* b) {
    if (a->kind != b->kind) return a->kind < b->kind;
    return compareRu(a->name, b->name) < 0;
  });
  w::IdOptions opts;
  for (const Faction* f : list) opts.add(f->id, w::orName(f->name), nullptr, f->color, f->isGuild() ? "гильдия" : "");
  ui::ComboOpt co;
  co.placeholder = "Государство";
  co.icon = "crown";
  co.tooltip = "Изучение общих технологий за государство";
  Id nv = value;
  if (!w::idCombo(id, nv, opts, co) || !nv) return false;   // за «никого» изучение не идёт
  value = nv;
  return true;
}

// ---------------------------------------------------------------- действия
void askDelete(App& a, Id tech, const std::string& name) {
  a.confirmAct("Удалить технологию «" + w::orName(name) + "»?",
               "Связи с ней исчезнут, зависящие технологии и постройки перестанут её требовать.", "Удалить", "Удалить технологию",
               [tech](Tx& tx) { rules::removeTech(tx, tech); });
}

void removeLink(App& a, LinkSel l) {
  a.act("Удалить связь технологий", [&](Tx& tx) { rules::setPrereq(tx, l.tech, l.pre, false); });
}

// Изучение и исследование — за who (общая технология — изучающая фракция; своего дерева — 0).
void setStudied(App& a, Id tech, bool on, Id who) {
  a.act(on ? "Отметить изученной" : "Снять отметку изучения", [&](Tx& tx) { rules::setStudied(tx, tech, on, who); });
}
// Исследование — игровое действие (и игроку за своё государство: canResearch).
void startResearch(App& a, Id tech, Id who) {
  a.play("Начать исследование", [&](Tx& tx) { rules::startResearch(tx, tech, who); });
}
void stopResearch(App& a, Id tech, Id who) {
  a.play("Остановить исследование", [&](Tx& tx) { rules::stopResearch(tx, tech, who); });
}
bool canResearch(const App& a, Id faction, Id who) { return a.canPlay(faction ? faction : who); }

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

namespace {

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

}  // namespace

void copyMenu(App& a, Id faction, bool nonEmpty) {
  if (!ui::beginMenu("copyfrom")) return;
  const World& w = a.world();
  const bool ro = !a.canEdit();
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

}  // namespace rg::app::techtree
