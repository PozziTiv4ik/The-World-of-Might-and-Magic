// Regnum — общие виджеты археологии: слоты мест (крупные и малые), ячейка сокровища, меню групп, задания групп.
#include "app/panels/arch_common.h"

#include "gfx/icons.h"

namespace rg::app::archui {

namespace {

// Название в две строки по ширине (перенос по пробелу ближе к середине); вторая строка — с многоточием при отрисовке.
std::pair<std::string, std::string> twoLines(const std::string& s, ui::Font f, float width) {
  if (ui::measure(s, f) <= width) return {s, {}};
  size_t best = std::string::npos;
  float bestW = 1e9f;
  for (size_t i = s.find(' '); i != std::string::npos; i = s.find(' ', i + 1)) {
    const float a = ui::measure(s.substr(0, i), f), b = ui::measure(s.substr(i + 1), f);
    const float worst = std::max(a, b);
    if (a <= width && worst < bestW) {
      bestW = worst;
      best = i;
    }
  }
  if (best == std::string::npos) return {s, {}};
  return {s.substr(0, best), s.substr(best + 1)};
}

gfx::TextStyle bigStyle(float size) {
  gfx::TextStyle st = ui::textStyle(ui::Font::Display);
  st.size = size;
  return st;
}

}  // namespace

Color slotColor(int slot) { return Color::hex(slot >= 0 && slot < kArchSlots ? arch::kSlots[slot].color : 0x888888); }

const char* siteIcon(const World& w, Id site) {
  const ArchSite* s = w.catalogs->archSite(site);
  return s && !s->icon.empty() && gfx::hasIcon(s->icon) ? s->icon.c_str() : "pickaxe";
}

std::string siteName(const World& w, Id site) {
  const ArchSite* s = w.catalogs->archSite(site);
  return s ? (s->name.empty() ? std::string("Без названия") : s->name) : std::string("—");
}

std::string chestName(const World& w, Id chest) {
  const Chest* c = w.catalogs->chest(chest);
  return c ? (c->name.empty() ? std::string("Без названия") : c->name) : std::string("—");
}

std::string groupName(const ArchGroup& g) { return g.name.empty() ? std::string("Без названия") : g.name; }

std::string chestTip(const World& w, Id chest) {
  const Chest* c = w.catalogs->chest(chest);
  if (!c) return "Сокровища нет";
  std::string tip = chestName(w, chest);
  for (const ChestItem& it : c->items) tip += "\n· " + rules::chestItemText(w, it);
  return tip;
}

std::string slotTip(const World& w, const ArchSlot& s, int slot, bool reveal) {
  std::string tip = arch::kSlots[slot].name;
  if (!s.site) return tip + "\nМеста нет";
  if (!s.open) return reveal ? tip + " · не найдено\n" + siteName(w, s.site) : tip + " · не найдено";
  tip += "\n" + siteName(w, s.site);
  const int n = arch::stagesOf(slot);
  if (arch::slotDone(s, slot)) tip += "\nИсследовано полностью";
  else tip += "\nЭтап " + std::to_string(s.stage) + " из " + std::to_string(n);
  return tip;
}

void drawSlotLarge(const World& w, const ArchSlot& s, int slot, RectF r, bool hovered) {
  const ui::Theme& th = ui::theme();
  const Color c = slotColor(slot);
  const bool open = s.site && s.open, done = arch::slotDone(s, slot);
  ui::draw::rect(r, open ? c.alpha(0.10f) : th.surface3.alpha(0.55f), 12);
  if (open) ui::draw::gradient(RectF{r.x, r.y, r.w, r.h * 0.6f}, c.alpha(0.12f), c.alpha(0.f), 12);
  ui::draw::rectStroke(r, c.alpha(hovered ? 1.f : open ? 0.85f : 0.55f), 12, hovered ? 2.f : 1.5f);
  const RectF cap{r.x + 8, r.y + 8, r.w - 16, ui::lineHeight(ui::Font::Caption)};
  ui::draw::text(arch::kSlots[slot].name, cap, ui::Font::Caption, c.alpha(open ? 0.95f : 0.75f), ui::Align::Center);
  if (!open) {
    const float q = std::min(56.f, r.h * 0.42f);
    ui::draw::textStyled("?", RectF{r.x, r.cy() - q * 0.62f, r.w, q * 1.25f}, bigStyle(q), c.alpha(hovered ? 0.95f : 0.7f), ui::Align::Center);
    return;
  }
  const float ic = std::min(40.f, r.h * 0.28f);
  const RectF icR{r.cx() - ic * 0.5f - 8, cap.bottom() + 8, ic + 16, ic + 16};
  ui::draw::circle(icR.cx(), icR.cy(), icR.w * 0.5f, c.alpha(0.16f));
  ui::draw::icon(siteIcon(w, s.site), icR.inset(8), c);
  if (done) {
    const RectF ck{r.right() - 24, r.y + 6, 18, 18};
    ui::draw::circle(ck.cx(), ck.cy(), 9, th.success);
    ui::draw::icon("check", ck.inset(3), th.onAccent);
  }
  const float lh = ui::lineHeight(ui::Font::Strong);
  const auto [l1, l2] = twoLines(siteName(w, s.site), ui::Font::Strong, r.w - 16);
  float y = icR.bottom() + 6;
  ui::draw::text(l1, RectF{r.x + 8, y, r.w - 16, lh}, ui::Font::Strong, th.text, ui::Align::Center);
  if (!l2.empty()) ui::draw::text(l2, RectF{r.x + 8, y + lh, r.w - 16, lh}, ui::Font::Strong, th.text, ui::Align::Center);
  // Этапы: полоса и «2/4».
  const int n = arch::stagesOf(slot);
  const float by = r.bottom() - 16;
  const std::string st = std::to_string(std::min(s.stage, n)) + "/" + std::to_string(n);
  const float tw = ui::measure(st, ui::Font::Caption) + 6;
  const RectF bar{r.x + 12, by + 4, r.w - 24 - tw, 5};
  ui::draw::rect(bar, th.track, 2.5f);
  const float k = float(std::clamp(s.stage, 0, n)) / float(std::max(1, n));
  if (k > 0) ui::draw::rect(RectF{bar.x, bar.y, std::max(5.f, bar.w * k), bar.h}, done ? th.success : c, 2.5f);
  ui::draw::text(st, RectF{bar.right() + 6, by - 1, tw, ui::lineHeight(ui::Font::Caption)}, ui::Font::Caption, done ? th.success : th.textDim);
}

void drawSlotSmall(const World& w, const ArchSlot& s, int slot, RectF r, bool hovered, bool active) {
  const ui::Theme& th = ui::theme();
  const Color c = slotColor(slot);
  const bool open = s.site && s.open, done = arch::slotDone(s, slot);
  ui::draw::rect(r, open ? c.alpha(hovered && active ? 0.24f : 0.13f) : th.surface3.alpha(0.6f), 7);
  ui::draw::rectStroke(r, c.alpha(hovered && active ? 1.f : open ? 0.8f : 0.5f), 7, hovered && active ? 2.f : 1.25f);
  if (!open) {
    ui::draw::textStyled("?", RectF{r.x, r.cy() - r.h * 0.36f, r.w, r.h * 0.72f}, bigStyle(r.h * 0.5f), c.alpha(0.75f), ui::Align::Center);
    return;
  }
  ui::draw::icon(siteIcon(w, s.site), r.inset(r.w * 0.24f), done ? c.alpha(0.6f) : c);
  if (done) {
    ui::draw::circle(r.right() - 5, r.y + 5, 6, th.success);
    ui::draw::icon("check", RectF{r.right() - 9, r.y + 1, 8, 8}, th.onAccent);
  } else if (s.stage > 0) {
    const float k = float(s.stage) / float(std::max(1, arch::stagesOf(slot)));
    const RectF bar{r.x + 4, r.bottom() - 5, r.w - 8, 2.5f};
    ui::draw::rect(bar, th.track, 1.25f);
    ui::draw::rect(RectF{bar.x, bar.y, bar.w * k, bar.h}, c, 1.25f);
  }
}

void treasureCell(const World& w, const ArchSlot& s, int slot, RectF r, bool reveal) {
  const ui::Theme& th = ui::theme();
  const bool done = arch::slotDone(s, slot);
  const bool show = s.chest && (reveal || done);
  ui::draw::rect(r, done ? th.success.alpha(0.10f) : th.surface3.alpha(0.6f), 7);
  ui::draw::rectStroke(r, done ? th.success.alpha(0.45f) : th.border, 7, 1);
  const RectF ic{r.x + 7, r.cy() - 7, 14, 14};
  ui::draw::icon("chest", ic, done ? th.success : show ? th.accent : th.textMuted);
  const RectF tr{ic.right() + 6, r.y, r.right() - ic.right() - 12, r.h};
  if (!s.chest) ui::draw::text("—", tr, ui::Font::Small, th.textMuted);
  else if (show) ui::draw::text(chestName(w, s.chest), tr, ui::Font::Small, done ? th.text : th.textDim);
  else ui::draw::text("??????", tr, ui::Font::Mono, th.textMuted);
  std::string tip = "Сокровище места";
  if (show) tip = (done ? "Получено: " : "Сокровище: ") + chestTip(w, s.chest);
  ui::hoverTip("##treasure", r, tip);
}

Id groupMenu(App& a, const World& w, Id state, std::string_view id, std::string_view header, const std::function<double(Id)>& chance) {
  Id pick = 0;
  if (!ui::beginMenu(id)) return 0;
  ui::menuHeader(header);
  const Faction* f = w.faction(state);
  if (!f || f->archGroups.empty()) ui::menuItem("Нет археологических групп", {.icon = "pickaxe", .disabled = true});
  if (f)
    for (const ArchGroup& g : f->archGroups) {
      ui::IdScope s{i64(g.id)};
      std::string why;
      const bool ready = rules::archGroupReady(w, state, g.id, &why);
      const std::string label = groupName(g) + " · ур. " + std::to_string(arch::levelOf(g.exp)) + (chance ? " · " + fmtPct(chance(g.id)) : std::string());
      if (ui::menuItem(label, {.icon = ready ? "pickaxe" : "hourglass", .disabled = !ready})) pick = g.id;
      a.markUi("arch.menu.group." + std::to_string(g.id));
      if (!ready) ui::tooltip(why);
    }
  ui::endMenu();
  return pick;
}

void discover(App& a, Id state, Id group, Id province) {
  rules::ArchReport rep;
  if (a.act("Найти археологическое место", [&](Tx& tx) { rep = rules::discoverSite(tx, state, group, province); })) showReport(a, rep);
}

void explore(App& a, Id state, Id group, Id province, int slot) {
  rules::ArchReport rep;
  if (a.act("Исследование археологического места", [&](Tx& tx) { rep = rules::exploreSite(tx, state, group, province, slot); })) showReport(a, rep);
}

void excavate(App& a, Id state, Id group, Id province) {
  rules::ArchReport rep;
  if (a.act("Раскопки", [&](Tx& tx) { rep = rules::excavate(tx, state, group, province); })) showReport(a, rep);
}

void openStateArch(App& a, Id state) {
  a.ui.tabOf[SelType::Faction] = kTabFaction;
  a.select(SelType::Faction, state);
}

}  // namespace rg::app::archui
