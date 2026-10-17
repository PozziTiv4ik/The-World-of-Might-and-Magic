// Regnum — дерево талантов подкласса героя (см. talent_tree.h).
#include "app/common/talent_tree.h"

#include <algorithm>

#include "app/widgets.h"
#include "gfx/icons.h"
#include "gfx/path.h"

namespace rg::app::talents {

namespace {

using w::orName;

// Раскладка доски: поле слева (номер яруса, порог, дорожка очков), сетка ячеек, промежутки. Три вида ячеек по
// ширине области: с названием обычным шрифтом, с названием мелким шрифтом, только значок.
constexpr float kPadX = 10, kPadTop = 14, kPadBottom = 18, kGutter = 40;
constexpr float kNamedMin = 88, kNamedMax = 150, kNamedGap = 14;   // название шрифтом Small, значок 22
constexpr float kSmallMin = 64, kSmallGap = 10;                    // название шрифтом Caption, значок 18
constexpr float kIconMin = 36, kIconMax = 60;                      // только значок
constexpr float kPillH = 16;

struct Lay {
  bool named = false;                 // в ячейке — название
  ui::Font font = ui::Font::Small;    // шрифт названия
  float icon = 22;                    // значок в ячейке с названием
  float cell = 52, cellH = 52, gap = 10, rowGap = 28;
  float gridW() const { return float(rules::kTalentCols) * cell + float(rules::kTalentCols - 1) * gap; }
};

Lay layoutFor(float width) {
  Lay L;
  constexpr float n = float(rules::kTalentCols), gaps = float(rules::kTalentCols - 1);
  const float W = std::max(0.f, width - 2 * kPadX - kGutter);
  const float big = (W - gaps * kNamedGap) / n, small = (W - gaps * kSmallGap) / n;
  if (big >= kNamedMin) {
    L.named = true;
    L.cell = std::floor(std::min(big, kNamedMax));
    L.gap = kNamedGap;
    L.cellH = std::round(10 + L.icon + 6 + 2 * ui::lineHeight(L.font) + 12);
    L.rowGap = 34;
  } else if (small >= kSmallMin) {
    L.named = true;
    L.font = ui::Font::Caption;
    L.icon = 18;
    L.cell = std::floor(small);
    L.gap = kSmallGap;
    L.cellH = std::round(8 + L.icon + 4 + 2 * ui::lineHeight(L.font) + 11);
    L.rowGap = 30;
  } else {
    L.cell = std::floor(std::clamp((W - gaps * 10) / n, kIconMin, kIconMax));
    L.gap = std::floor(std::clamp((W - n * L.cell) / gaps, 6.f, 22.f));
    L.cellH = L.cell;
    L.rowGap = 30;
  }
  return L;
}

float heightOf(const Lay& L, int tiers) {
  tiers = std::max(1, tiers);
  return kPadTop + float(tiers) * L.cellH + float(tiers - 1) * L.rowGap + kPadBottom;
}

const char* iconOf(const Talent& t) { return !t.icon.empty() && gfx::hasIcon(t.icon) ? t.icon.c_str() : "talent"; }

const Talent* talentAt(const SubClass& c, int row, int col) {
  for (const Talent& t : c.talents)
    if (t.row == row && t.col == col) return &t;
  return nullptr;
}

std::string points(int n) { return std::to_string(n) + " " + plural(n, "очко", "очка", "очков"); }

// Название в две строки по словам (вторая — с многоточием при переполнении).
std::pair<std::string, std::string> twoLines(const std::string& s, float width, ui::Font f) {
  if (ui::measure(s, f) <= width) return {s, {}};
  std::vector<std::string> words = split(s, ' ');
  std::string a;
  size_t i = 0;
  for (; i < words.size(); i++) {
    const std::string next = a.empty() ? words[i] : a + " " + words[i];
    if (!a.empty() && ui::measure(next, f) > width) break;
    a = next;
  }
  std::string b;
  for (; i < words.size(); i++) b += (b.empty() ? "" : " ") + words[i];
  return {a, b};
}

// Состояние таланта у героя.
enum class St : u8 { Edit, Full, Partial, Available, Locked };

// Восьмиугольник (финальный талант): прямоугольник со срезанными углами.
gfx::Path octagon(RectF r, float k) {
  gfx::Path p;
  p.moveTo(r.x + k, r.y);
  p.lineTo(r.right() - k, r.y);
  p.lineTo(r.right(), r.y + k);
  p.lineTo(r.right(), r.bottom() - k);
  p.lineTo(r.right() - k, r.bottom());
  p.lineTo(r.x + k, r.bottom());
  p.lineTo(r.x, r.bottom() - k);
  p.lineTo(r.x, r.y + k);
  p.close();
  return p;
}

// Подсказка ячейки: название, ранги и стоимость, ярус, условие, что даёт, описание, состояние.
void cellTip(const World& w, const SubClass& c, const Talent& t, St st, int rank, const std::string& why, const Character* hero, bool interactive,
             Color cc, RectF r) {
  if (!ui::beginTooltipOver("##tip", r, 300)) return;
  ui::label(orName(t.name), {.font = ui::Font::Strong, .color = cc, .wrap = true});
  const bool ult = isUltimate(t);
  std::string line;
  if (ult) line = hero ? (rank > 0 ? "Изучен · финальный талант, " : "Не изучен · финальный талант, ") + points(t.cost) : "Финальный талант · " + points(t.cost);
  else if (t.ranks > 1)
    line = (hero ? "Ранг " + std::to_string(rank) + " из " + std::to_string(t.ranks) : "Рангов: " + std::to_string(t.ranks)) + " · " + points(t.cost) + " за ранг";
  else line = (hero ? (rank > 0 ? std::string("Изучен · ") : std::string("Не изучен · ")) : std::string("Один ранг · ")) + points(t.cost);
  ui::label(line, {.font = ui::Font::Small, .ink = ui::Ink::Dim, .wrap = true});
  const int need = rules::tierThreshold(c, t.row);
  ui::label("Ярус " + std::to_string(t.row + 1) + (need ? " · от " + points(need) + " в ярусах выше" : std::string(" · открыт сразу")),
            {.font = ui::Font::Small, .ink = ui::Ink::Muted, .wrap = true});
  if (const Talent* p = t.prereq ? c.talent(t.prereq) : nullptr) {
    const bool done = hero && talentRank(hero->talents, p->id) >= std::max(1, p->ranks);
    ui::label("Требует: «" + orName(p->name) + "»" + (p->ranks > 1 ? " (все ранги)" : ""),
              {.font = ui::Font::Small, .ink = done ? ui::Ink::Success : hero ? ui::Ink::Warning : ui::Ink::Normal, .icon = "link", .wrap = true});
  }
  std::vector<std::string> mods;
  for (Id m : t.modifiers)
    if (const Modifier* x = w.modifier(m)) mods.push_back(orName(x->name));
  if (!mods.empty()) ui::label("Даёт: " + join(mods, ", "), {.font = ui::Font::Small, .ink = ui::Ink::Info, .icon = "sparkles", .wrap = true});
  if (!t.desc.empty()) ui::label(t.desc, {.font = ui::Font::Small, .ink = ui::Ink::Dim, .wrap = true, .maxLines = 8});
  if (hero) {
    std::string tail;
    ui::Ink ink = ui::Ink::Accent;
    switch (st) {
      case St::Full: tail = interactive ? "Правый щелчок — снять ранг" : ""; break;
      case St::Partial:
        if (!why.empty()) {
          tail = why;
          ink = ui::Ink::Warning;
        } else {
          tail = interactive ? "Щелчок — следующий ранг · правый щелчок — снять" : "";
        }
        break;
      case St::Available: tail = interactive ? "Щелчок — изучить" : ""; break;
      case St::Locked:
        tail = why;
        ink = ui::Ink::Warning;
        break;
      case St::Edit: break;
    }
    if (!tail.empty()) {
      ui::gap(2);
      ui::label(tail, {.font = ui::Font::Small, .ink = ink, .icon = st == St::Locked || ink == ui::Ink::Warning ? "lock" : nullptr, .wrap = true});
    }
  } else {
    size_t learned = 0;
    w.characters.each([&](const Character& ch) {
      if (std::find(ch.talents.begin(), ch.talents.end(), t.id) != ch.talents.end()) learned++;
    });
    if (learned)
      ui::label("Изучили: " + std::to_string(learned) + " " + plural(i64(learned), "герой", "героя", "героев"),
                {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "users"});
  }
  ui::endTooltip();
}

// Перетаскивание таланта (правка).
struct Drag {
  Id talent = 0;
  bool moved = false;
};

}  // namespace

// ================================================================ сведения о дереве
bool isUltimate(const Talent& t) { return t.ranks <= 1 && t.cost >= schema::kMaxTalentCost; }

bool ultimateTier(const SubClass& s, int row) {
  bool any = false;
  for (const Talent& t : s.talents) {
    if (t.row != row) continue;
    if (!isUltimate(t)) return false;
    any = true;
  }
  return any;
}

std::string ranksText(const Talent& t) {
  if (isUltimate(t)) return points(t.cost);
  const std::string r = t.ranks > 1 ? "1–" + std::to_string(t.ranks) : std::string("1");
  return t.cost > 1 ? r + " × " + std::to_string(t.cost) : r;
}

int shownTiers(const SubClass& c, bool editable) {
  const int used = rules::talentTiers(c);
  return std::clamp(std::max(3, used + (editable ? 1 : 0)), 1, rules::kTalentRows);
}

float treeHeight(const SubClass& s, bool editable, float width) { return heightOf(layoutFor(width), shownTiers(s, editable)); }

float minTreeWidth() { return 2 * kPadX + kGutter + float(rules::kTalentCols) * kIconMin + float(rules::kTalentCols - 1) * 6; }

// ================================================================ доска
Events tree(App& a, const Options& o) {
  Events ev;
  if (!o.sub) return ev;
  const SubClass& c = *o.sub;
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  const Character* hero = o.hero ? w.character(o.hero) : nullptr;
  const bool heroMode = hero != nullptr;
  const bool clicks = heroMode ? o.interactive : true;
  const int tiers = shownTiers(c, o.editable);
  const Lay L = layoutFor(ui::avail().w);
  const RectF slot = ui::next(0, heightOf(L, tiers));
  a.markUi(o.mark, slot);
  const float blockW = kGutter + L.gridW();
  const float x0 = slot.x + std::max(kPadX, std::round((slot.w - blockW) * 0.5f));
  const float gx = x0 + kGutter;
  auto cellRect = [&](int r, int col) {
    return RectF{gx + float(col) * (L.cell + L.gap), slot.y + kPadTop + float(r) * (L.cellH + L.rowGap), L.cell, L.cellH};
  };
  const Color cc = w::legible(o.cls ? o.cls->color : th.accent);
  ui::IdScope scope("talenttree");
  Drag& drag = ui::state<Drag>(ui::id("##drag"));
  if (drag.talent && (!o.editable || !c.talent(drag.talent))) drag = Drag{};

  // Доска.
  ui::draw::rect(slot, th.surface1, 12);
  ui::draw::rectStroke(slot, th.border, 12, 1);

  // Ярусы: полосы, номер и порог, дорожка вложенных очков героя.
  const float trackX = x0 + kGutter - 9;
  std::vector<int> above(size_t(tiers) + 1, 0);
  if (heroMode)
    for (int r = 0; r <= tiers; r++) above[size_t(r)] = rules::heroSpentAbove(w, o.hero, r);
  for (int r = 0; r < tiers; r++) {
    const RectF a0 = cellRect(r, 0);
    const RectF band{slot.x + 4, a0.y - 6, slot.w - 8, L.cellH + 17};
    const int need = rules::tierThreshold(c, r);
    const bool open = !heroMode || above[size_t(r)] >= need;
    const bool ult = ultimateTier(c, r);
    if (ult) {
      ui::draw::gradient(band, th.accent.alpha(0.13f), th.accent.alpha(0.04f), 9);
      ui::draw::rectStroke(band, th.accent.alpha(0.32f), 9, 1);
    } else {
      ui::draw::rect(band, th.text.alpha(heroMode && open ? 0.045f : 0.025f), 9);
    }
    // Номер (у финального яруса — корона) и порог.
    const float cy = a0.cy();
    const RectF col{x0, cy - 17, kGutter - 18, 34};
    const Color num = ult ? th.accent : heroMode ? (open ? th.text : th.textMuted) : th.textDim;
    if (ult) ui::draw::icon("crown", RectF{col.cx() - 8, col.y + 1, 16, 16}, num);
    else ui::draw::text(std::to_string(r + 1), RectF{col.x, col.y, col.w, 18}, ui::Font::Strong, num, ui::Align::Center);
    ui::draw::text(std::to_string(need), RectF{col.x, col.y + 19, col.w, 14}, ui::Font::Caption,
                   heroMode && open && r > 0 ? th.success : th.textMuted, ui::Align::Center);
    std::string tip = "Ярус " + std::to_string(r + 1) + (ult ? " · финальные таланты" : "") + "\n" +
                      (r == 0 ? std::string("Открыт сразу") : "Открывается, когда в ярусах выше вложено " + points(need));
    if (heroMode && r > 0) tip += "\nВложено: " + std::to_string(above[size_t(r)]) + (open ? " · открыт" : " · ещё " + std::to_string(need - above[size_t(r)]));
    const RectF tr{x0, a0.y, kGutter - 4, L.cellH};
    ui::hoverTip("##tier" + std::to_string(r), tr, tip);
    a.markUi(o.mark + ".tier." + std::to_string(r), tr);
  }
  if (heroMode && tiers > 1) {
    // Дорожка: от яруса к ярусу заполняется по мере вложения очков к порогу следующего.
    const float y0 = cellRect(0, 0).cy(), y1 = cellRect(tiers - 1, 0).cy();
    ui::draw::line(trackX, y0, trackX, y1, th.border, 2);
    for (int r = 0; r + 1 < tiers; r++) {
      const int t0 = rules::tierThreshold(c, r), t1 = rules::tierThreshold(c, r + 1);
      const float k = t1 > t0 ? std::clamp(float(above[size_t(r + 1)] - t0) / float(t1 - t0), 0.f, 1.f) : 1.f;
      if (k <= 0) continue;
      const float ya = cellRect(r, 0).cy(), yb = cellRect(r + 1, 0).cy();
      ui::draw::line(trackX, ya, trackX, ya + (yb - ya) * k, th.accent, 2);
    }
    for (int r = 0; r < tiers; r++) {
      const float cy = cellRect(r, 0).cy();
      const bool open = above[size_t(r)] >= rules::tierThreshold(c, r);
      ui::draw::circle(trackX, cy, 4.5f, th.surface1);
      if (open) ui::draw::circle(trackX, cy, 3.5f, th.accent);
      else ui::draw::ring(trackX, cy, 3.5f, 1.5f, th.borderStrong);
    }
  }

  // Состояния талантов героя.
  std::unordered_map<Id, St> state;
  std::unordered_map<Id, std::string> why;
  std::unordered_map<Id, int> rank;
  for (const Talent& t : c.talents) {
    if (!heroMode) {
      state[t.id] = St::Edit;
      continue;
    }
    std::string reason;
    const int rk = talentRank(hero->talents, t.id);
    rank[t.id] = rk;
    const bool can = rules::canLearnTalent(w, o.hero, t.id, &reason);
    if (rk >= std::max(1, t.ranks)) state[t.id] = St::Full;
    else if (rk > 0) state[t.id] = St::Partial;
    else state[t.id] = can ? St::Available : St::Locked;
    if (!can && rk < std::max(1, t.ranks)) why[t.id] = reason;
  }

  // Стрелки условий: от нижней кромки условия (под плашкой рангов) по промежуткам к верху зависимого.
  for (const Talent& t : c.talents) {
    const Talent* p = t.prereq ? c.talent(t.prereq) : nullptr;
    if (!p || p->row >= t.row || t.row >= tiers) continue;
    const RectF A = cellRect(p->row, p->col), B = cellRect(t.row, t.col);
    gfx::Path path;
    const float ya = A.bottom() + L.rowGap * 0.5f + 2, yb = B.y - L.rowGap * 0.5f + 4;
    path.moveTo(A.cx(), A.bottom() + kPillH * 0.5f + 1);
    if (t.row == p->row + 1) {
      path.lineTo(A.cx(), ya);
      path.lineTo(B.cx(), ya);
    } else {
      // Через промежуток столбцов рядом с зависимым (со стороны условия), чтобы не пересекать ячейки.
      const float ch = t.col > p->col   ? B.x - L.gap * 0.5f
                       : t.col < p->col ? B.right() + L.gap * 0.5f
                       : t.col > 0      ? B.x - L.gap * 0.5f
                                        : B.right() + L.gap * 0.5f;
      path.lineTo(A.cx(), ya);
      path.lineTo(ch, ya);
      path.lineTo(ch, yb);
      path.lineTo(B.cx(), yb);
    }
    path.lineTo(B.cx(), B.y - 7);
    Color col;
    if (heroMode) col = state[p->id] == St::Full ? th.accent : th.textMuted.alpha(0.6f);
    else col = (o.selected == t.id || o.selected == p->id) ? th.accent : cc.alpha(0.7f);
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
      a.markUi(o.mark + ".cell." + std::to_string(r) + "." + std::to_string(col), R);
      const Talent* t = talentAt(c, r, col);
      if (!t) {
        if (!o.editable) continue;
        ui::IdScope s(i64(r) * 16 + col + 0x40000000LL);
        const ui::Interaction it = ui::interact(ui::id("##empty"), R);
        const bool dropHere = drag.talent && drag.moved && R.contains(m.x, m.y);
        const bool hot = (it.hovered && !drag.talent) || dropHere;
        ui::draw::rect(R, th.surface3.alpha(hot ? 0.5f : 0.22f), 10);
        ui::draw::rectStroke(R, hot ? th.accent : th.borderStrong.alpha(0.45f), 10, hot ? 1.5f : 1);
        if (hot && !dropHere) {
          ui::setCursor(platform::Cursor::Hand);
          const float s2 = std::min(22.f, R.h * 0.4f);
          ui::draw::icon("plus", RectF{R.cx() - s2 * 0.5f, R.cy() - s2 * 0.5f, s2, s2}, th.accent);
        }
        ui::hoverTip("##emptytip", R, "Новый талант: ярус " + std::to_string(r + 1) + ", столбец " + std::to_string(col + 1));
        if (it.clicked) {
          ev.newRow = r;
          ev.newCol = col;
        }
        continue;
      }
      const St st = state[t->id];
      const int rk = heroMode ? rank[t->id] : 0;
      const bool ult = isUltimate(*t);
      ui::IdScope s(i64(t->id));
      const ui::Interaction it = ui::interact(ui::id("##talent"), R);
      const bool sel = !heroMode && o.selected == t->id;
      const bool dragging = drag.talent == t->id && drag.moved;
      const bool hover = it.hovered && clicks && (st != St::Locked);
      // Цвета по состоянию.
      Color fill, border, ink, name;
      float bw = 1.5f;
      bool glow = false;
      switch (st) {
        case St::Edit:
          fill = cc.alpha(ult ? 0.24f : 0.16f);
          border = sel ? th.accent : ult ? th.accent.alpha(0.8f) : cc.alpha(0.6f);
          ink = ult ? th.accentHover : cc;
          name = th.text;
          bw = sel ? 2.5f : 1.5f;
          glow = sel;
          break;
        case St::Full:
          fill = ult ? th.accent.alpha(0.32f) : cc.alpha(0.45f);
          border = th.accent;
          ink = th.text;
          name = th.text;
          bw = 2.25f;
          glow = true;
          break;
        case St::Partial:
          fill = cc.alpha(0.16f);
          border = th.accent.alpha(0.7f);
          ink = cc;
          name = th.text;
          bw = 1.75f;
          break;
        case St::Available:
          fill = th.surface3;
          border = th.accent.alpha(0.6f);
          ink = cc;
          name = th.textDim;
          break;
        case St::Locked:
          fill = th.surface2.alpha(0.55f);
          border = th.border;
          ink = th.textMuted.alpha(0.75f);
          name = th.textMuted;
          bw = 1;
          break;
      }
      if (hover) border = th.accentHover;
      if (dragging) {
        fill = fill.alpha(0.3f);
        ink = ink.alpha(0.3f);
        name = name.alpha(0.3f);
      }
      const float rad = L.named ? 10 : 9;
      if (glow && !dragging) ui::draw::shadow(R, rad, 14, (sel ? th.accent : st == St::Full && ult ? th.accent : cc).alpha(0.45f), 0, 1);
      if (ult) {
        const float k = std::round(std::min(R.w, R.h) * 0.2f);
        const RectF halo = R.inset(-3.5f);
        ui::draw::pathStroke(octagon(halo, k + 1.5f), th.accent.alpha(st == St::Locked ? 0.25f : 0.5f), 1);
        const gfx::Path oct = octagon(R, k);
        ui::draw::path(oct, th.surface1);
        ui::draw::path(oct, fill);
        ui::draw::pathStroke(octagon(R.inset(bw * 0.5f), std::max(1.f, k - bw * 0.3f)), border, bw);
      } else {
        ui::draw::rect(R, th.surface1, rad);
        ui::draw::rect(R, fill, rad);
        ui::draw::rectStroke(R, border, rad, bw);
      }
      // Значок и название.
      if (L.named) {
        const bool big = L.font == ui::Font::Small;
        const float top = big ? 10 : 8, pad = big ? 6 : 4;
        ui::draw::icon(iconOf(*t), RectF{R.cx() - L.icon * 0.5f, R.y + top, L.icon, L.icon}, ink);
        const float lh = ui::lineHeight(L.font);
        const auto [l1, l2] = twoLines(orName(t->name), R.w - 2 * pad, L.font);
        const float ty = R.y + top + L.icon + (big ? 6 : 4) + (l2.empty() ? lh * 0.5f : 0);
        ui::draw::text(l1, RectF{R.x + pad, ty, R.w - 2 * pad, lh}, L.font, name, ui::Align::Center);
        if (!l2.empty()) ui::draw::text(l2, RectF{R.x + pad, ty + lh, R.w - 2 * pad, lh}, L.font, name, ui::Align::Center);
        if (st == St::Locked && big) ui::draw::icon("lock", RectF{R.right() - 17, R.y + 5, 12, 12}, th.textMuted.alpha(0.8f));
      } else {
        const float pad = std::round(R.w * 0.25f);
        ui::draw::icon(iconOf(*t), R.inset(pad), ink);
      }
      // Плашка рангов на нижней кромке: «1–5» в справочнике, «2/5» у героя, «5 очков» у финального.
      {
        std::string label;
        if (ult) label = points(t->cost);
        else if (heroMode) label = std::to_string(rk) + "/" + std::to_string(std::max(1, t->ranks));
        else label = ranksText(*t);
        float pw = std::ceil(ui::measure(label, ui::Font::Caption)) + 12;
        if (ult && pw > R.w - 4) {   // узкая ячейка: только число
          label = std::to_string(t->cost);
          pw = std::ceil(ui::measure(label, ui::Font::Caption)) + 12;
        }
        pw = std::max(pw, 26.f);
        const RectF p{std::round(R.cx() - pw * 0.5f), R.bottom() - kPillH * 0.5f, pw, kPillH};
        Color pf = th.surface2, pb = th.border, pt = th.textDim;
        switch (st) {
          case St::Edit:
            pf = ult ? th.accent.alpha(0.22f) : th.surface2;
            pb = ult ? th.accent.alpha(0.8f) : cc.alpha(0.5f);
            pt = ult ? th.accentHover : th.text;
            break;
          case St::Full:
            pf = th.accent;
            pb = th.accent;
            pt = th.onAccent;
            break;
          case St::Partial:
            pf = th.accent.alpha(0.25f);
            pb = th.accent.alpha(0.9f);
            pt = th.accentHover;
            break;
          case St::Available:
            pf = th.surface2;
            pb = th.accent.alpha(0.55f);
            pt = th.text;
            break;
          case St::Locked:
            pf = th.surface2;
            pb = th.border;
            pt = th.textMuted;
            break;
        }
        if (dragging) {
          pf = pf.alpha(0.3f);
          pt = pt.alpha(0.3f);
        }
        ui::draw::rect(p.inset(-2), th.surface1, kPillH);
        ui::draw::rect(p, pf, kPillH);
        ui::draw::rectStroke(p, pb, kPillH, 1);
        ui::draw::text(label, p, ui::Font::Caption, pt, ui::Align::Center);
      }
      if (hover || (it.hovered && !heroMode)) ui::setCursor(platform::Cursor::Hand);
      a.markUi(o.mark + ".talent." + std::to_string(t->id), R);
      if (!drag.talent) {
        auto wit = why.find(t->id);
        cellTip(w, c, *t, st, rk, wit == why.end() ? std::string() : wit->second, hero, clicks, cc, R);
      }
      // Перетаскивание (правка): перенос в другую ячейку.
      if (o.editable && it.dragging) {
        drag.talent = t->id;
        drag.moved = true;
        const RectF g{m.x - L.cell * 0.4f, m.y - L.cellH * 0.4f, L.cell * 0.8f, L.cellH * 0.8f};
        ui::draw::shadow(g, 8, 12, th.shadow, 4);
        ui::draw::rect(g, th.surface2, 8);
        ui::draw::rect(g, cc.alpha(0.3f), 8);
        ui::draw::rectStroke(g, th.accent, 8, 1.5f);
        const float is = std::min(g.w, g.h) * 0.5f;
        ui::draw::icon(iconOf(*t), RectF{g.cx() - is * 0.5f, g.cy() - is * 0.5f, is, is}, th.text);
        ui::setCursor(platform::Cursor::Grabbing);
      }
      if (it.released && drag.talent == t->id) {
        if (drag.moved)
          for (int rr = 0; rr < tiers; rr++)
            for (int c2 = 0; c2 < rules::kTalentCols; c2++)
              if (cellRect(rr, c2).contains(m.x, m.y) && (rr != t->row || c2 != t->col)) {
                ev.moved = t->id;
                ev.toRow = rr;
                ev.toCol = c2;
              }
        const bool wasDrag = drag.moved;
        drag = Drag{};
        if (wasDrag) continue;
      }
      if (!clicks) continue;
      if (it.clicked) ev.clicked = t->id;
      if (it.rightClicked) ev.rightClicked = t->id;
    }
  return ev;
}

}  // namespace rg::app::talents
