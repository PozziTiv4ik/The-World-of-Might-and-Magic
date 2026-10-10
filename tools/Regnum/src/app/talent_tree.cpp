// Regnum — дерево талантов класса героя сеткой (см. talent_tree.h).
#include "app/talent_tree.h"

#include "gfx/icons.h"
#include "gfx/path.h"

namespace rg::app::talents {

namespace {

constexpr float kCell = 52, kColGap = 26, kRowGap = 30, kGutter = 48, kPad = 10;

const char* iconOf(const Talent& t) { return !t.icon.empty() && gfx::hasIcon(t.icon) ? t.icon.c_str() : "talent"; }

const Talent* talentAt(const HeroClass& c, int row, int col) {
  for (const Talent& t : c.talents)
    if (t.row == row && t.col == col) return &t;
  return nullptr;
}

std::string orName(const std::string& s) { return s.empty() ? std::string("Без названия") : s; }

std::string roman(int n) {
  static const char* const k[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
  return n >= 1 && n <= 10 ? std::string(k[n - 1]) : std::to_string(n);
}

// Состояние таланта у героя.
enum class St : u8 { Edit, Learned, Available, Locked };

std::string tipOf(const World& w, const HeroClass& c, const Talent& t, St st, const std::string& why) {
  std::string s = orName(t.name) + "\nСтоимость: " + std::to_string(t.cost) + " " + plural(t.cost, "очко", "очка", "очков") + " · ярус " +
                  std::to_string(t.row + 1);
  if (const Talent* p = t.prereq ? c.talent(t.prereq) : nullptr) s += "\nТребует: «" + orName(p->name) + "»";
  std::vector<std::string> mods;
  for (Id m : t.modifiers)
    if (const Modifier* x = w.modifier(m)) mods.push_back(x->name);
  if (!mods.empty()) s += "\nДаёт: " + join(mods, ", ");
  if (!t.desc.empty()) s += "\n" + t.desc;
  switch (st) {
    case St::Learned: s += "\nИзучен · правый щелчок — отменить"; break;
    case St::Available: s += "\nЩелчок — изучить"; break;
    case St::Locked: s += "\n" + why; break;
    case St::Edit: break;
  }
  return s;
}

// Перетаскивание таланта (правка).
struct Drag {
  Id talent = 0;
  bool moved = false;
};

}  // namespace

float treeWidth() { return kGutter + 4 * kCell + 3 * kColGap; }
float treeHeight(int tiers) { return std::max(1, tiers) * kCell + std::max(0, tiers - 1) * kRowGap + 2 * kPad; }

int shownTiers(const HeroClass& c, bool editable) {
  const int used = rules::talentTiers(c);
  return std::clamp(std::max(3, used + (editable ? 1 : 0)), 1, rules::kTalentRows);
}

Events tree(App& a, const Options& o) {
  Events ev;
  if (!o.cls) return ev;
  const HeroClass& c = *o.cls;
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  const Character* hero = o.hero ? w.character(o.hero) : nullptr;
  const bool heroMode = hero != nullptr;
  const int tiers = shownTiers(c, o.editable);
  const RectF slot = ui::next(0, treeHeight(tiers));
  a.markUi(o.mark, slot);
  const float x0 = slot.x + std::max(0.f, std::round((slot.w - treeWidth()) * 0.5f));
  const float gx = x0 + kGutter;
  auto cellRect = [&](int r, int col) {
    return RectF{gx + float(col) * (kCell + kColGap), slot.y + kPad + float(r) * (kCell + kRowGap), kCell, kCell};
  };
  const Color cc = c.color.luminance() < 0.1f ? c.color.lighten(0.45f) : c.color;
  ui::IdScope scope("talenttree");
  Drag& drag = ui::state<Drag>(ui::id("##drag"));
  if (drag.talent && !c.talent(drag.talent)) drag = Drag{};

  // Подложка: полосы ярусов.
  for (int r = 0; r < tiers; r++) {
    const RectF a0 = cellRect(r, 0), a3 = cellRect(r, rules::kTalentCols - 1);
    const RectF band{a0.x - 10, a0.y - 8, a3.right() - a0.x + 20, kCell + 16};
    ui::draw::rect(band, th.text.alpha(r % 2 ? 0.02f : 0.035f), 10);
    // Номер яруса и порог очков.
    const RectF lr{x0, a0.y, kGutter - 14, kCell};
    const int need = rules::tierThreshold(c, r);
    const bool open = !heroMode || rules::talentSpentAbove(w, o.hero, r) >= need;
    ui::draw::text(roman(r + 1), RectF{lr.x, lr.y + 8, lr.w, 18}, ui::Font::Strong, heroMode ? (open ? th.success : th.textMuted) : th.textDim, ui::Align::Center);
    ui::draw::text(std::to_string(need), RectF{lr.x, lr.y + 28, lr.w, 14}, ui::Font::Caption, th.textMuted, ui::Align::Center);
    ui::hoverTip("##tier" + std::to_string(r), lr,
                 r == 0 ? std::string("Ярус I: открыт сразу")
                        : "Ярус " + roman(r + 1) + ": открывается, когда в ярусах выше вложено " + std::to_string(need) + " " +
                              plural(need, "очко", "очка", "очков") +
                              (heroMode ? " (вложено " + std::to_string(rules::talentSpentAbove(w, o.hero, r)) + ")" : std::string()));
    a.markUi(o.mark + ".tier." + std::to_string(r), lr);
  }

  // Состояния талантов героя.
  std::unordered_map<Id, St> state;
  std::unordered_map<Id, std::string> why;
  for (const Talent& t : c.talents) {
    if (!heroMode) {
      state[t.id] = St::Edit;
      continue;
    }
    std::string reason;
    if (std::find(hero->talents.begin(), hero->talents.end(), t.id) != hero->talents.end()) state[t.id] = St::Learned;
    else if (rules::canLearnTalent(w, o.hero, t.id, &reason)) state[t.id] = St::Available;
    else state[t.id] = St::Locked;
    why[t.id] = reason;
  }

  // Стрелки условий: от низа условия по промежуткам между ячейками к верху зависимого.
  for (const Talent& t : c.talents) {
    const Talent* p = t.prereq ? c.talent(t.prereq) : nullptr;
    if (!p || p->row >= t.row || t.row >= tiers) continue;
    const RectF A = cellRect(p->row, p->col), B = cellRect(t.row, t.col);
    gfx::Path path;
    const float ya = A.bottom() + kRowGap * 0.5f, yb = B.y - kRowGap * 0.5f;
    path.moveTo(A.cx(), A.bottom() + 1);
    if (t.row == p->row + 1) {
      path.lineTo(A.cx(), ya);
      path.lineTo(B.cx(), ya);
    } else {
      // Через промежуток столбцов рядом с зависимым (со стороны условия), чтобы не пересекать ячейки.
      const float ch = t.col > p->col ? B.x - kColGap * 0.5f : t.col < p->col ? B.right() + kColGap * 0.5f
                                                             : (t.col > 0 ? B.x - kColGap * 0.5f : B.right() + kColGap * 0.5f);
      path.lineTo(A.cx(), ya);
      path.lineTo(ch, ya);
      path.lineTo(ch, yb);
      path.lineTo(B.cx(), yb);
    }
    path.lineTo(B.cx(), B.y - 6);
    Color col;
    if (heroMode) col = state[p->id] == St::Learned ? th.accent : th.textMuted.alpha(0.7f);
    else col = (o.selected == t.id || o.selected == p->id) ? th.accent : cc.alpha(0.75f);
    ui::draw::pathStroke(path, col, 2);
    gfx::Path head;
    head.moveTo(B.cx() - 5, B.y - 8);
    head.lineTo(B.cx() + 5, B.y - 8);
    head.lineTo(B.cx(), B.y - 1);
    head.close();
    ui::draw::path(head, col);
  }

  // Ячейки.
  const ui::Mouse& m = ui::mouse();
  for (int r = 0; r < tiers; r++)
    for (int col = 0; col < rules::kTalentCols; col++) {
      const RectF R = cellRect(r, col);
      const std::string cellMark = o.mark + ".cell." + std::to_string(r) + "." + std::to_string(col);
      a.markUi(cellMark, R);
      const Talent* t = talentAt(c, r, col);
      if (!t) {
        if (!o.editable) continue;
        ui::IdScope s(i64(r) * 16 + col + 0x40000000LL);
        const ui::Interaction it = ui::interact(ui::id("##empty"), R);
        const bool dropHere = drag.talent && drag.moved && R.contains(m.x, m.y);
        ui::draw::rect(R, th.surface3.alpha(it.hovered || dropHere ? 0.55f : 0.28f), 10);
        ui::draw::rectStroke(R, it.hovered || dropHere ? th.accent : th.borderStrong.alpha(0.55f), 10, it.hovered || dropHere ? 1.5f : 1);
        if (it.hovered && !drag.talent) {
          ui::setCursor(platform::Cursor::Hand);
          ui::draw::icon("plus", R.inset(16), th.accent);
        }
        ui::hoverTip("##emptytip", R, "Новый талант: ярус " + roman(r + 1) + ", столбец " + std::to_string(col + 1));
        if (it.clicked) {
          ev.newRow = r;
          ev.newCol = col;
        }
        continue;
      }
      const St st = state[t->id];
      ui::IdScope s(i64(t->id));
      const ui::Interaction it = ui::interact(ui::id("##talent"), R);
      const bool sel = !heroMode && o.selected == t->id;
      const bool dragging = drag.talent == t->id && drag.moved;
      // Основа ячейки.
      Color fill, border, ink;
      float bw = 1.5f;
      switch (st) {
        case St::Edit:
          fill = cc.alpha(0.2f);
          border = sel ? th.accent : cc.alpha(0.7f);
          ink = cc;
          bw = sel ? 2.5f : 1.5f;
          break;
        case St::Learned:
          fill = cc.alpha(0.42f);
          border = th.accent;
          ink = th.text;
          bw = 2.5f;
          break;
        case St::Available:
          fill = th.surface3;
          border = th.accent.alpha(0.75f);
          ink = cc;
          break;
        case St::Locked:
          fill = th.surface2.alpha(0.7f);
          border = th.border;
          ink = th.textMuted.alpha(0.8f);
          bw = 1;
          break;
      }
      if (sel || st == St::Learned) ui::draw::shadow(R, 10, 12, (sel ? th.accent : cc).alpha(0.45f), 0, 1);
      ui::draw::rect(R, th.surface1, 10);
      ui::draw::rect(R, dragging ? fill.alpha(0.15f) : fill, 10);
      ui::draw::rectStroke(R, it.hovered && st != St::Locked ? th.accentHover : border, 10, bw);
      ui::draw::icon(iconOf(*t), R.inset(13), dragging ? ink.alpha(0.3f) : ink);
      // Стоимость — в углу.
      {
        const RectF b{R.right() - 17, R.bottom() - 17, 20, 20};
        ui::draw::circle(b.cx(), b.cy(), 10, th.surface1);
        ui::draw::circle(b.cx(), b.cy(), 9, st == St::Learned ? th.accent : th.surface3);
        ui::draw::text(std::to_string(t->cost), b, ui::Font::Caption, st == St::Learned ? th.onAccent : st == St::Locked ? th.textMuted : th.text,
                       ui::Align::Center);
      }
      if (it.hovered && (st != St::Locked || !heroMode)) ui::setCursor(platform::Cursor::Hand);
      ui::hoverTip("##tip", R, tipOf(w, c, *t, st, why[t->id]));
      a.markUi(o.mark + ".talent." + std::to_string(t->id), R);
      // Перетаскивание (правка): перенос в другую ячейку.
      if (o.editable && it.dragging) {
        drag.talent = t->id;
        drag.moved = true;
        const RectF g{m.x - kCell * 0.4f, m.y - kCell * 0.4f, kCell * 0.8f, kCell * 0.8f};
        ui::draw::rect(g, cc.alpha(0.35f), 8);
        ui::draw::rectStroke(g, th.accent, 8, 1.5f);
        ui::draw::icon(iconOf(*t), g.inset(9), th.text);
        ui::setCursor(platform::Cursor::Grabbing);
      }
      if (it.released && drag.talent == t->id) {
        if (drag.moved)
          for (int rr = 0; rr < tiers; rr++)
            for (int cc2 = 0; cc2 < rules::kTalentCols; cc2++)
              if (cellRect(rr, cc2).contains(m.x, m.y) && (rr != t->row || cc2 != t->col)) {
                ev.moved = t->id;
                ev.toRow = rr;
                ev.toCol = cc2;
              }
        const bool wasDrag = drag.moved;
        drag = Drag{};
        if (wasDrag) continue;
      }
      if (it.clicked) ev.clicked = t->id;
      if (it.rightClicked) ev.rightClicked = t->id;
    }
  return ev;
}

}  // namespace rg::app::talents
