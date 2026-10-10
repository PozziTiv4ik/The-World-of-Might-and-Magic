// Regnum — окна археологии (ТЗ «Доработки №2», п.3, 14–19): «Результат исследования в провинции «…»» и «Результат
// раскопок в провинции «…»» — исход с шансом, место и этапы, полученное (сундуки с содержимым), опыт и уровень группы,
// трагедия и пробуждение бедствия, появившееся войско без государства («Показать на карте»); затем, если группа в
// смертельной опасности, — «Группу может спасти только божественное вмешательство»: 1000 эссенции государства или
// отказ (группа погибает). Решение обязательно: окно спасения без него не закрывается.
#include "app/panels/arch_common.h"
#include "app/panels/faction_common.h"

namespace rg::app {
namespace {

using namespace archui;
using rules::ArchGain;
using rules::ArchReport;

// Круглый значок исхода.
void bigIcon(const char* icon, ui::Tone tone) {
  const RectF b = ui::next(48, 48);
  ui::draw::circle(b.cx(), b.cy(), 24, ui::toneColor(tone).alpha(0.16f));
  ui::draw::icon(icon, b.inset(12), ui::toneColor(tone));
}

std::string gainLabel(const World& w, const ArchGain& g) {
  switch (g.kind) {
    case ArchGain::Resource: return w.resource(g.id) ? w.resource(g.id)->name : std::string("Ресурс");
    case ArchGain::Essence: return w.essence(g.id) ? w.essence(g.id)->name : std::string("Эссенция");
    case ArchGain::Chest: return chestName(w, g.id);
    case ArchGain::NoRelic: return "Реликвии не нашлось";
    case ArchGain::Relic: break;
  }
  return {};
}

// Строка полученного: значок, название, количество справа; содержимое сундука — с отступом.
void gainRow(const World& w, const ArchGain& g, int i) {
  ui::IdScope sc(i);
  const ui::Theme& th = ui::theme();
  ui::Indent ind(float(std::min(g.depth, 4)) * 18.f);
  if (g.kind == ArchGain::Relic) {
    if (const Relic* r = w.relic(g.id)) w::relicRow(*r, rules::isArchFind(w, g.id) ? std::string("Археологическая находка") : std::string());
    return;
  }
  const RectF row = ui::next(26);
  const char* icon = "coins";
  Color c = th.textDim;
  switch (g.kind) {
    case ArchGain::Resource:
      icon = w::resourceIcon(w, g.id);
      c = w::resourceColor(w, g.id);
      break;
    case ArchGain::Essence:
      icon = "essence";
      c = w::essenceColor(w, g.id);
      break;
    case ArchGain::Chest:
      icon = "chest";
      c = th.accent;
      break;
    case ArchGain::NoRelic:
      icon = "relic";
      c = th.textMuted;
      break;
    default: break;
  }
  ui::draw::icon(icon, RectF{row.x + 2, row.cy() - 8, 16, 16}, c);
  const std::string amt = g.kind == ArchGain::Resource || g.kind == ArchGain::Essence
                              ? fmtNum(g.amount, 3) + (g.kind == ArchGain::Resource && g.id == kGold ? " тыс." : "")
                              : std::string();
  const float aw = amt.empty() ? 0 : ui::measure(amt, ui::Font::Strong) + 4;
  ui::draw::text(gainLabel(w, g), RectF{row.x + 26, row.y, row.w - 30 - aw, row.h}, g.kind == ArchGain::Chest ? ui::Font::Strong : ui::Font::Body,
                 g.kind == ArchGain::NoRelic ? th.textMuted : th.text);
  if (!amt.empty()) ui::draw::text(amt, RectF{row.right() - aw, row.y, aw, row.h}, ui::Font::Strong, th.text, ui::Align::Right);
}

// «Группу может спасти только божественное вмешательство».
struct DivineDlg final : Dialog {
  Id state = 0, group = 0;
  std::string name;
  Id essence = 0;
  bool decided = false;

  const char* id() const override { return "arch.divine"; }
  Style style(App&) override {
    Style s{"Божественное вмешательство", "sparkles", ui::Tone::Danger, 520};
    s.closeButton = false;
    return s;
  }
  // Решение обязательно: закрытое без решения окно открывается снова.
  void dismissed(App& a) override {
    if (decided) return;
    const Id s = state, g = group;
    const std::string n = name;
    detail::later(a, [s, g, n](App& x) {
      auto d = std::make_unique<DivineDlg>();
      d->state = s;
      d->group = g;
      d->name = n;
      x.openDialog(std::move(d));
    });
  }

  bool draw(App& a) override {
    const World& w = fac::frameWorld(a);
    const Faction* f = w.faction(state);
    // Группы нет или опасность снята (отмена события, решение в другом окне) — окно не нужно.
    if (!f || !f->archGroup(group) || !f->archGroup(group)->danger) {
      decided = true;
      return false;
    }
    {
      ui::Row r({ui::px(48), ui::fr(1)}, ui::kAuto, 12);
      bigIcon("skull", ui::Tone::Danger);
      ui::Group g(0, 2);
      ui::label("«" + name + "»", {.font = ui::Font::Title});
      ui::text("Группу может спасти только божественное вмешательство", ui::Font::Body, ui::Ink::Dim);
    }
    const std::vector<Id> can = rules::divineEssences(w, state);
    if (std::find(can.begin(), can.end(), essence) == can.end()) essence = can.empty() ? 0 : can.front();
    std::vector<ui::Option> opts;
    std::vector<std::string> hints;
    hints.reserve(can.size());
    int idx = -1;
    for (size_t i = 0; i < can.size(); i++) {
      const CatalogItem* e = w.essence(can[i]);
      hints.push_back(fmtNum(f->essence(can[i]), 3));
      opts.push_back(ui::Option{e ? std::string_view(e->name) : std::string_view("Эссенция"), "essence", w::essenceColor(w, can[i]), hints.back()});
      if (can[i] == essence) idx = int(i);
    }
    ui::prop("Эссенция", "essence");
    if (ui::combo("ess", idx, opts, {.placeholder = "Нет эссенции на 1000", .disabled = can.empty()}) && idx >= 0 && idx < int(can.size()))
      essence = can[size_t(idx)];
    a.markUi("arch.divine.essence");
    ui::prop("Цена", "coins");
    ui::label(fmtNum(arch::kDivineCost) + " эссенции", {.font = ui::Font::Strong});
    ui::ModalFooter footer;
    const Id s = state, g = group, e = essence;
    if (ui::button("Отказаться", {.variant = ui::Variant::Danger, .icon = "skull", .tooltip = "Группа погибнет"})) {
      decided = true;
      a.act("Археологическая группа погибла", [&](Tx& tx) { rules::resolveArchDanger(tx, s, g, 0); });
      return false;
    }
    a.markUi("arch.divine.refuse");
    if (ui::button("Спасти", {.variant = ui::Variant::Primary, .icon = "sparkles", .disabled = e == 0, .isDefault = true})) {
      if (a.act("Божественное вмешательство", [&](Tx& tx) { rules::resolveArchDanger(tx, s, g, e); })) {
        decided = true;
        return false;
      }
    }
    a.markUi("arch.divine.save");
    return true;
  }
};

// Итог события.
struct ResultDlg final : Dialog {
  ArchReport r;
  bool closed = false;

  const char* id() const override { return "arch.result"; }
  Style style(App&) override {
    const ui::Tone tone = r.tragedyLine >= 0 ? ui::Tone::Danger : r.success ? ui::Tone::Success : ui::Tone::Accent;
    return {r.title, r.kind == ArchReport::Dig ? "shovel" : "pickaxe", tone, 620};
  }
  void finish(App& a) {
    if (closed) return;
    closed = true;
    if (!r.awaiting) return;
    const Id s = r.state, g = r.group;
    const std::string n = r.groupName.empty() ? std::string("Без названия") : r.groupName;
    detail::later(a, [s, g, n](App& x) {
      auto d = std::make_unique<DivineDlg>();
      d->state = s;
      d->group = g;
      d->name = n;
      x.openDialog(std::move(d));
    });
  }
  void dismissed(App& a) override { finish(a); }

  void head() {
    const char* icon = "check-circle";
    ui::Tone tone = ui::Tone::Success;
    std::string status;
    switch (r.kind) {
      case ArchReport::Discover: status = r.success ? "Место найдено" : "Ничего не найдено"; break;
      case ArchReport::Explore: status = r.success ? (r.done ? "Место исследовано полностью" : "Успех") : "Неудача"; break;
      case ArchReport::Dig: status = r.success ? "Находки" : "Ничего не найдено"; break;
    }
    if (!r.success) {
      icon = r.kind == ArchReport::Explore ? "close" : "search";
      tone = r.kind == ArchReport::Explore ? ui::Tone::Danger : ui::Tone::Neutral;
    }
    ui::Row row({ui::px(48), ui::fr(1)}, ui::kAuto, 12);
    bigIcon(icon, tone);
    ui::Group g(0, 2);
    ui::label(status, {.font = ui::Font::Title});
    std::string sub = "Группа «" + (r.groupName.empty() ? std::string("Без названия") : r.groupName) + "», ур. " + std::to_string(r.levelBefore);
    if (r.kind != ArchReport::Dig) sub += " · шанс успеха " + fmtPct(r.chance);
    ui::label(sub, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  }

  void siteCard(App& a, const World& w) {
    if (r.slot < 0 || !r.site) return;
    const ui::Theme& th = ui::theme();
    const Color c = slotColor(r.slot);
    const RectF box = ui::next(56);
    ui::draw::rect(box, c.alpha(0.08f), th.radiusCard);
    ui::draw::rectStroke(box, c.alpha(0.6f), th.radiusCard, 1);
    const RectF ic{box.x + 10, box.cy() - 18, 36, 36};
    ui::draw::circle(ic.cx(), ic.cy(), 18, c.alpha(0.16f));
    ui::draw::icon(siteIcon(w, r.site), ic.inset(8), c);
    const float x = ic.right() + 12, right = 120;
    ui::draw::text(siteName(w, r.site), RectF{x, box.y + 9, box.right() - x - right, ui::lineHeight(ui::Font::Strong)}, ui::Font::Strong, th.text);
    ui::draw::text(arch::kSlots[r.slot].name, RectF{x, box.y + 9 + ui::lineHeight(ui::Font::Strong), box.right() - x - right, ui::lineHeight(ui::Font::Small)},
                   ui::Font::Small, c);
    const int n = std::max(1, r.stages);
    const int passed = r.kind == ArchReport::Explore ? (r.success ? r.stage : r.stage - 1) : 0;
    const std::string st = r.kind == ArchReport::Explore ? "Этап " + std::to_string(r.stage) + " из " + std::to_string(n) : std::string("Этапов: ") + std::to_string(n);
    const RectF sr{box.right() - right, box.y + 10, right - 12, ui::lineHeight(ui::Font::Small)};
    ui::draw::text(st, sr, ui::Font::Small, th.textDim, ui::Align::Right);
    const RectF bar{sr.x, sr.bottom() + 8, sr.w, 6};
    ui::draw::rect(bar, th.track, 3);
    const float k = float(std::clamp(passed, 0, n)) / float(n);
    if (k > 0) ui::draw::rect(RectF{bar.x, bar.y, std::max(6.f, bar.w * k), bar.h}, r.done ? th.success : c, 3);
    a.markUi("arch.result.site", box);
  }

  bool draw(App& a) override {
    const World& w = fac::frameWorld(a);
    head();
    siteCard(a, w);
    const int tl = r.tragedyLine >= 0 ? std::min<int>(r.tragedyLine, int(r.lines.size())) : int(r.lines.size());
    for (int i = 0; i < tl; i++) ui::label(r.lines[size_t(i)], {.ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
    if (!r.gains.empty()) {
      ui::caption("Получено");
      ui::Scroll sc("gains", std::min(260.f, float(r.gains.size()) * 34.f + 4.f));
      for (size_t i = 0; i < r.gains.size(); i++) gainRow(w, r.gains[i], int(i));
    }
    a.markUi("arch.result.gains");
    if (r.exp > 0) {
      ui::HStack hs(24, ui::Align::Left, 8);
      ui::icon("star", ui::Ink::Accent, 16);
      ui::label("+" + fmtInt(r.exp) + " опыта", {.font = ui::Font::Strong});
      ui::label(fmtInt(r.expAfter) + " / " + fmtInt(arch::kMaxExp), {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      if (r.levelAfter > r.levelBefore) ui::badge("Уровень " + std::to_string(r.levelAfter), ui::Tone::Accent, true);
      a.markUi("arch.result.exp");
    }
    if (tl < int(r.lines.size())) {
      ui::Card card({.icon = "warning", .title = r.calamity.empty() ? std::string_view("Трагедия при исследовании") : std::string_view(r.calamity), .tone = ui::Tone::Danger});
      for (size_t i = size_t(tl) + 1; i < r.lines.size(); i++)
        if (r.lines[i] != r.calamity) ui::label(r.lines[i], {.ink = ui::Ink::Dim, .wrap = true});
      a.markUi("arch.result.tragedy");
    }
    if (const Army* army = r.army ? w.army(r.army) : nullptr) {
      i64 total = 0;
      for (const ArmyGroup& g : army->groups)
        for (const ArmyUnit& u : g.units) total += u.count;
      ui::Row row({ui::px(22), ui::fr(1), ui::px(170)}, 30, 8);
      ui::icon("army", ui::Ink::Danger, 18);
      ui::label((army->name.empty() ? std::string("Войско без государства") : army->name) + " · " + fmtInt(total),
                {.font = ui::Font::Strong, .tooltip = "Войско без государства"});
      if (ui::button("Показать на карте", {.icon = "map-pin", .fill = true})) {
        const Id id = army->id;
        finish(a);
        detail::later(a, [id](App& x) {
          x.toMap();
          x.select(SelType::Army, id, true);
        });
        return false;
      }
      a.markUi("arch.result.army");
    }
    ui::ModalFooter footer;
    if (ui::button(r.awaiting ? "Далее" : "Готово", {.variant = ui::Variant::Primary, .icon = r.awaiting ? "arrow-right" : "check", .isDefault = true})) {
      finish(a);
      return false;
    }
    a.markUi("arch.result.ok");
    return true;
  }
};

}  // namespace

namespace archui {
void showReport(App& a, const rules::ArchReport& r) {
  auto d = std::make_unique<ResultDlg>();
  d->r = r;
  a.openDialog(std::move(d));
}

void showDivine(App& a, Id state, Id group) {
  const Faction* f = a.store.world().faction(state);
  const ArchGroup* g = f ? f->archGroup(group) : nullptr;
  if (!g || !g->danger || a.hasDialog("arch.divine")) return;
  auto d = std::make_unique<DivineDlg>();
  d->state = state;
  d->group = group;
  d->name = g->name.empty() ? std::string("Без названия") : g->name;
  a.openDialog(std::move(d));
}
}  // namespace archui

}  // namespace rg::app
