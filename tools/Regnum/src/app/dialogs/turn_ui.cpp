// Regnum — общие элементы интерфейса хода, истории и хроники (см. turn_ui.h).
#include "app/dialogs/turn_ui.h"

#include "app/app_internal.h"
#include "app/widgets.h"
#include "gfx/text.h"

#include <unordered_map>

namespace rg::app::turnui {

// ================================================================ хроника
ui::Tone logTone(LogKind k) {
  switch (k) {
    case LogKind::Turn: return ui::Tone::Accent;
    case LogKind::Economy: return ui::Tone::Warning;
    case LogKind::Build: return ui::Tone::Info;
    case LogKind::Tech: return ui::Tone::Info;
    case LogKind::War: return ui::Tone::Danger;
    case LogKind::Battle: return ui::Tone::Danger;
    case LogKind::Army: return ui::Tone::Neutral;
    case LogKind::Fleet: return ui::Tone::Info;
    case LogKind::Diplomacy: return ui::Tone::Success;
    case LogKind::Trade: return ui::Tone::Accent;
    case LogKind::Province: return ui::Tone::Warning;
    case LogKind::Guild: return ui::Tone::Accent;
    case LogKind::Population: return ui::Tone::Success;
    default: return ui::Tone::Neutral;
  }
}

const char* logIcon(LogKind k) {
  if (int(k) < 0 || k >= LogKind::Count) return "note";
  if (k == LogKind::Note) return "quill";
  return schema::logKind(k).icon;
}

const char* logName(LogKind k) {
  if (int(k) < 0 || k >= LogKind::Count) return "Запись";
  return schema::logKind(k).name;
}

bool entryFocusable(const World& w, const LogEntry& e) {
  if (e.province && w.province(e.province)) return true;
  if (e.army && w.army(e.army)) return true;
  for (Id f : e.factions)
    if (w.faction(f)) return true;
  return false;
}

void focusEntry(App& a, const LogEntry& e) {
  const World& w = a.world();
  if (e.province && w.province(e.province)) {
    a.select(SelType::Province, e.province, true);
    return;
  }
  if (e.army && w.army(e.army)) {
    a.select(SelType::Army, e.army, true);
    return;
  }
  for (Id f : e.factions)
    if (w.faction(f)) {
      a.select(SelType::Faction, f, true);
      return;
    }
}

namespace {
// Запись хроники экономики по смыслу (тексты — rules/turn.cpp, шаги 2, 2а–2в): долг казны, голод начался, голод
// закончился (недостача провизии покрыта), не хватает эссенций на содержание элементалей, преобразование ресурсов.
enum class Eco : u8 { Debt, Famine, FamineEnd, Essence, Convert, Other };
Eco economyOf(const LogEntry& e) {
  const std::string k = utf8::searchKey(e.text);
  auto has = [&](const char* s) { return k.find(s) != std::string::npos; };
  if (has("провизия закончилась")) return Eco::Famine;
  if (has("голод закончился")) return Eco::FamineEnd;
  if (has("эссенц")) return Eco::Essence;
  if (has("преобразование")) return Eco::Convert;
  if (has("долг")) return Eco::Debt;
  return Eco::Other;
}

// Значок и тон записи: у экономики — по смыслу (голод, эссенции, преобразование), у прочих — по виду.
const char* entryIcon(const LogEntry& e) {
  if (e.kind == LogKind::Economy) switch (economyOf(e)) {
      case Eco::Famine:
      case Eco::FamineEnd: return "grain";
      case Eco::Essence: return "essence";
      case Eco::Convert: return "convert";
      case Eco::Debt: return "treasury";
      case Eco::Other: break;
    }
  return logIcon(e.kind);
}
ui::Tone entryTone(const LogEntry& e) {
  if (e.kind == LogKind::Economy) switch (economyOf(e)) {
      case Eco::Famine:
      case Eco::Essence:
      case Eco::Debt: return ui::Tone::Danger;
      case Eco::FamineEnd:
      case Eco::Convert: return ui::Tone::Success;
      case Eco::Other: break;
    }
  return logTone(e.kind);
}

// Высота текста записи при ширине (кеш: хроника перерисовывает сотни строк за кадр).
float textHeight(std::string_view text, float width, int maxLines) {
  static std::unordered_map<u64, float> cache;
  u64 k = hashMix(hashMix(hash64(text), u64(std::lround(width * 4))), u64(maxLines) ^ u64(std::lround(ui::deviceScale() * 100)) << 8);
  if (auto it = cache.find(k); it != cache.end()) return it->second;
  if (cache.size() > 8192) cache.clear();
  gfx::TextLayout L = gfx::layoutText(text, ui::textStyle(ui::Font::Body), width, maxLines, true);
  float h = std::ceil(L.height);
  cache[k] = h;
  return h;
}
}  // namespace

bool logRow(const World& w, const LogEntry& e, const LogRowOpt& o) {
  const ui::Theme& th = ui::theme();
  ui::IdScope scope{i64(e.id)};
  RectF av = ui::avail();
  const float pad = 7, tile = 28, gx = 10, mh = 16;
  float textW = std::max(60.f, av.w - pad - tile - gx - pad);
  float textH = textHeight(e.text, textW, o.maxLines);
  std::vector<Id> facs;
  for (Id f : e.factions)
    if (w.faction(f)) facs.push_back(f);
  const Province* prov = e.province ? w.province(e.province) : nullptr;
  const Army* army = e.army ? w.army(e.army) : nullptr;
  bool meta = !facs.empty() || prov || army || o.showTurn;
  float h = pad + std::max(tile, textH + (meta ? mh + 2 : 0.f)) + pad;
  RectF r = ui::next(h);
  bool focusable = o.clickable && entryFocusable(w, e);
  ui::WidgetId wid = ui::id("##row");
  ui::Interaction it = ui::interact(wid, r, focusable ? ui::IfFocusable : ui::IfNone);
  float hv = ui::animate(wid ^ 0x5e11ull, it.hovered || it.focused ? 1.f : 0.f);
  if (focusable && hv > 0.01f) ui::draw::rect(r, th.hover.alpha(std::min(1.f, hv * 1.4f)), th.radiusField + 1);
  // Фокус с клавиатуры — тонкая золотая полоска слева (щелчок мышью тоже даёт фокус: без яркого кольца).
  if (it.focused) ui::draw::rect(RectF{r.x, r.y + 6, 2, r.h - 12}, th.accent, 1);
  iconTile(RectF{r.x + pad, r.y + pad, tile, tile}, entryIcon(e), entryTone(e));
  float tx = r.x + pad + tile + gx;
  {
    ui::Area ta(RectF{tx, r.y + pad - 1, textW, textH + 2}, 0);
    ui::label(e.text, {.font = ui::Font::Body, .wrap = true, .maxLines = o.maxLines});
  }
  if (meta) {
    float my = r.y + pad + textH + 2;
    float x = tx, right = r.right() - pad;
    // При наведении справа — время записи и знак «показать на карте».
    if (hv > 0.01f) {
      if (focusable) {
        ui::draw::icon("target", RectF{right - 14, my + 1, 14, 14}, th.accent.alpha(hv));
        right -= 20;
      }
      std::string time = o.showTime ? detail::localTime(e.at) : std::string();
      if (!time.empty()) {
        float tw = ui::measure(time, ui::Font::Caption) + 2;
        ui::draw::text(time, RectF{right - tw, my, tw, mh}, ui::Font::Caption, th.textMuted.alpha(hv), ui::Align::Right);
        right -= tw + 10;
      }
    }
    auto piece = [&](const std::string& s, Color dot, const char* icon) {
      float tw = ui::measure(s, ui::Font::Caption) + 2;
      float lead = icon ? 16.f : 11.f;
      float need = lead + tw + 10;
      if (x + lead + tw > right) {
        if (x + 12 < right) ui::draw::text("…", RectF{x, my, 12, mh}, ui::Font::Caption, th.textMuted);
        x = right + 1;
        return false;
      }
      if (icon) ui::draw::icon(icon, RectF{x, my + 2, 12, 12}, th.textMuted);
      else ui::draw::circle(x + 3.5f, my + mh * 0.5f, 3.5f, dot);
      ui::draw::text(s, RectF{x + lead, my, tw, mh}, ui::Font::Caption, th.textDim);
      x += need;
      return true;
    };
    if (o.showTurn) piece("ход " + std::to_string(e.turn), {}, "hourglass");
    if (prov) piece(prov->name.empty() ? std::string("Провинция") : prov->name, {}, "map-pin");
    if (army) piece(army->name.empty() ? std::string(army->isFleet() ? "Флот" : "Войско") : army->name, {}, army->isFleet() ? "fleet" : "army");
    for (Id f : facs) {
      const Faction* fa = w.faction(f);
      if (!piece(fa->name.empty() ? std::string("Без названия") : fa->name, fa->color, nullptr)) break;
    }
  }
  if (focusable && it.hovered) ui::setCursor(platform::Cursor::Hand);
  return focusable && it.clicked;
}

// ================================================================ мелкие элементы
u64 sessionTag(App& a) {
  const Meta& m = *a.store.world().meta;
  return hashMix(hash64(a.dataDir()), hash64(m.createdAt + "|" + m.basemap));
}

void iconTile(RectF r, const char* icon, ui::Tone tone) {
  const ui::Theme& th = ui::theme();
  Color c = ui::toneColor(tone);
  ui::draw::rect(r, c.alpha(th.dark ? 0.15f : 0.12f), std::min(8.f, r.w * 0.3f));
  float s = std::round(r.w * 0.58f);
  ui::draw::icon(icon, RectF{std::round(r.cx() - s * 0.5f), std::round(r.cy() - s * 0.5f), s, s}, c);
}

void factionLabel(const World& w, Id fid, float h) {
  const Faction* f = w.faction(fid);
  ui::HStack hs(h, ui::Align::Left, 8);
  if (f) {
    if (f->isState()) ui::flag(f->flag, 24, 16, 3);
    else {
      RectF dr = ui::next(24, h);
      ui::draw::rect(RectF{dr.x, dr.cy() - 8, 24, 16}, f->color.alpha(0.22f), 3);
      ui::draw::icon("guild", RectF{dr.cx() - 6, dr.cy() - 6, 12, 12}, f->color);
    }
  }
  ui::label(f ? (f->name.empty() ? std::string("Без названия") : f->name) : std::string("—"));
}

ui::Ink deltaInk(double v) { return v > 0.5 ? ui::Ink::Success : v < -0.5 ? ui::Ink::Danger : ui::Ink::Muted; }

std::string money(double v) {
  double a = std::fabs(v);
  int digits = a >= 100 || std::fabs(v - std::round(v)) < 1e-9 ? 0 : (a >= 10 ? 1 : 2);
  return fmtNum(v, digits);
}

RectF cellRect(ui::Table& t) {
  RectF cr = t.cell();
  ui::next(cr.w, cr.h);
  return cr;
}

void flowBars(RectF r, double income, double expense, double scale) {
  const ui::Theme& th = ui::theme();
  float bh = 5, gap = 3;
  float y = std::round(r.cy() - bh - gap * 0.5f);
  double k = scale > 0 ? 1.0 / scale : 0;
  auto bar = [&](float yy, double v, Color c) {
    ui::draw::rect(RectF{r.x, yy, r.w, bh}, th.track, bh * 0.5f);
    float w = float(clamp(v * k, 0.0, 1.0)) * r.w;
    if (v > 0 && w < 2) w = 2;
    if (w > 0) ui::draw::rect(RectF{r.x, yy, w, bh}, c, bh * 0.5f);
  };
  bar(y, income, th.success);
  bar(y + bh + gap, expense, th.danger);
}

std::string flowText(const rules::FactionCalc& fc, bool income) {
  std::vector<std::string> lines;
  auto line = [&](const char* name, double v) {
    if (std::fabs(v) >= 0.005) lines.push_back(std::string(name) + ": " + money(v));
  };
  if (income) {
    line("Провинции", fc.incProvinces);
    line("Налог гильдий", fc.incGuildTax);
    line("Штабы гильдии", fc.incGuilds);
    line("Торговля", fc.incTrade);
    line("Дань и репарации", fc.incTribute);
    line("Рабы на работах", fc.incSlaves);
    line("Флот в торговле", fc.incTradeFleet);
    line("Торговые маршруты", fc.incRoutes);
    if (std::fabs(fc.incomePct) >= 0.005) lines.push_back("Модификаторы: " + fmtPct(fc.incomePct, 0, true));
  } else {
    line("Войска", fc.expArmy);
    line("Флот", fc.expFleet);
    line("Специалисты", fc.expSpecialists);
    line("Торговля", fc.expTrade);
    line("Дань и репарации", fc.expTribute);
    line("Содержание рабов", fc.expSlaves);
  }
  if (lines.empty()) return income ? "Доходов нет" : "Расходов нет";
  return join(lines, "\n");
}

bool isMainState(const World& w, Id faction) {
  const Faction* f = w.faction(faction);
  return f && f->isState() && f->mainState;
}

bool entryOnPage(const World& w, const LogEntry& e, int page) {
  if (page == kPageAll) return true;
  bool main = false, other = false;
  auto mark = [&](Id f) {
    if (w.faction(f)) (isMainState(w, f) ? main : other) = true;
  };
  for (Id f : e.factions) mark(f);
  if (const Province* p = e.province ? w.province(e.province) : nullptr) mark(p->owner);
  if (const Army* a = e.army ? w.army(e.army) : nullptr)
    for (const ArmyGroup& g : a->groups) mark(g.faction);
  if (!main && !other) other = true;   // запись без фракций — на странице прочих
  return page == kPageMain ? main : other;
}

std::string pageLabel(int page, size_t count) {
  return std::string(page == kPageMain ? "Основные государства" : "Остальные") + " · " + std::to_string(count);
}

std::vector<const rules::TurnFactionLine*> sortedLines(const World& w, const rules::TurnReport& rep, int page) {
  std::vector<const rules::TurnFactionLine*> v;
  for (auto& l : rep.factions)
    if (page == kPageAll || isMainState(w, l.faction) == (page == kPageMain)) v.push_back(&l);
  std::stable_sort(v.begin(), v.end(), [&](auto* x, auto* y) {
    const Faction* fx = w.faction(x->faction);
    const Faction* fy = w.faction(y->faction);
    bool gx = fx && fx->isGuild(), gy = fy && fy->isGuild();
    if (gx != gy) return !gx;
    return compareRu(fx ? fx->name : "", fy ? fy->name : "") < 0;
  });
  return v;
}

bool hasStockChanges(const rules::TurnFactionLine& l) { return !l.resources.empty() || !l.essences.empty() || l.provisionDebt > 1e-9; }

namespace {

// Изменение запаса: до тысячных у малых нецелых, иначе целое.
std::string deltaText(double v) {
  const double a = std::fabs(v);
  return fmtSigned(v, std::fabs(v - std::round(v)) < 1e-9 ? 0 : a < 10 ? 3 : a < 1000 ? 1 : 0);
}

}  // namespace

void stockDeltas(const World& w, const rules::TurnFactionLine& l, float width, float height, ui::Font font) {
  const ui::Theme& th = ui::theme();
  struct Item {
    const char* icon;
    Color color;
    std::string name, text;
    ui::Ink ink;
    bool famine = false;
  };
  std::vector<Item> items;
  if (l.provisionDebt > 1e-9)
    items.push_back({"grain", th.danger, "Голод: недостача провизии после хода " + fmtNum(l.provisionDebt, 3), "Голод", ui::Ink::Danger, true});
  // Эссенции — раньше ресурсов: их меньше, и долг эссенции важнее (содержание элементалей).
  for (auto& [eid, v] : l.essences) {
    const CatalogItem* e = w.essence(eid);
    items.push_back({"essence", w::essenceColor(w, eid), e ? e->name : std::string("Эссенция"), deltaText(v),
                     v < -1e-9 ? ui::Ink::Danger : deltaInk(v)});
  }
  for (auto& [rid, v] : l.resources) {
    const CatalogItem* c = w.resource(rid);
    items.push_back({w::resourceIcon(w, rid), w::resourceColor(w, rid), c ? c->name : std::string("Ресурс"), deltaText(v), deltaInk(v)});
  }
  if (items.empty()) return;
  const float gap = 12, iconW = 16, iconGap = 4;
  auto itemW = [&](const Item& it) {
    const float tw = std::ceil(ui::measure(it.text, it.famine ? ui::Font::Small : font)) + 2;
    return it.famine ? tw + iconW + iconGap + 14 : iconW + iconGap + tw;
  };
  // Сколько помещается (с «ещё N» в конце, если не все).
  size_t fit = 0;
  float used = 0;
  for (size_t i = 0; i < items.size(); i++) {
    const float iw = itemW(items[i]) + (i ? gap : 0);
    const bool last = i + 1 == items.size();
    const float more = last ? 0 : gap + ui::measure("ещё " + std::to_string(items.size() - i - 1), ui::Font::Small) + 4;
    if (used + iw + more > width && i > 0) break;
    used += iw;
    fit = i + 1;
  }
  ui::HStack hs(height, ui::Align::Left, gap);
  for (size_t i = 0; i < fit; i++) {
    const Item& it = items[i];
    ui::IdScope s{i64(i)};
    const RectF r = ui::next(itemW(it), height);
    if (it.famine) {
      const RectF pill{r.x, r.cy() - 10, r.w, 20};
      ui::draw::rect(pill, th.danger.alpha(0.16f), 10);
      ui::draw::icon(it.icon, RectF{pill.x + 7, pill.cy() - 7, 14, 14}, th.danger);
      ui::draw::text(it.text, RectF{pill.x + 7 + iconW + iconGap - 2, pill.y, pill.w - 7 - iconW - iconGap, pill.h}, ui::Font::Small, th.danger);
    } else {
      ui::draw::icon(it.icon, RectF{r.x, r.cy() - iconW * 0.5f, iconW, iconW}, it.color);
      ui::draw::text(it.text, RectF{r.x + iconW + iconGap, r.y, r.w - iconW - iconGap, r.h}, font, ui::inkColor(it.ink));
    }
    ui::at(r);
    ui::label("##delta", {.font = ui::Font::Display, .tooltip = it.famine ? std::string_view(it.name) : std::string_view(it.name + ": " + it.text)});
  }
  if (fit < items.size()) {
    std::vector<std::string> rest;
    for (size_t i = fit; i < items.size(); i++) rest.push_back(items[i].name + ": " + items[i].text);
    ui::label("ещё " + std::to_string(items.size() - fit), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .tooltip = join(rest, "\n")});
  }
}

TurnDigest digest(const World& w, const rules::TurnReport& rep, int page) {
  TurnDigest d;
  for (Id lid : rep.logIds) {
    const LogEntry* e = w.log.get(lid);
    if (!e) continue;
    if (e->kind != LogKind::Turn && !entryOnPage(w, *e, page)) continue;
    switch (e->kind) {
      case LogKind::Turn: d.summary = e; continue;
      case LogKind::Build: d.builds.push_back(e); break;
      case LogKind::Tech: d.techs.push_back(e); break;
      case LogKind::Economy:
        switch (economyOf(*e)) {
          case Eco::Famine: d.famine.push_back(e); break;
          case Eco::Essence: d.essences.push_back(e); break;
          case Eco::Debt: d.debts.push_back(e); break;
          case Eco::FamineEnd:
          case Eco::Convert:
          case Eco::Other: d.economy.push_back(e); break;
        }
        break;
      case LogKind::Province: d.rebellions.push_back(e); break;
      case LogKind::Trade:
        if (startsWith(e->text, "Недостача")) d.shortfalls.push_back(e);
        else d.deals.push_back(e);
        break;
      case LogKind::Diplomacy: d.deals.push_back(e); break;
      default: d.other.push_back(e); break;
    }
    d.all.push_back(e);
  }
  return d;
}

// ================================================================ прокрутка по содержимому
FitScroll::FitScroll(std::string_view id, float maxH, float minH) {
  h_ = &ui::state<float>(ui::id(std::string(id) + "##fit"));
  float h = *h_ > 0 ? *h_ : std::min(maxH, 320.f);
  h = std::round(clamp(h, std::min(minH, maxH), std::max(minH, maxH)));
  sc_.emplace(id, h);
  y0_ = ui::avail().y;
}

FitScroll::~FitScroll() {
  float used = ui::avail().y - y0_ - ui::theme().gap;
  if (used > 0 && std::fabs(used - *h_) > 0.5f) {
    *h_ = used;
    ui::requestRedraw();
  }
  sc_.reset();
}

}  // namespace rg::app::turnui
