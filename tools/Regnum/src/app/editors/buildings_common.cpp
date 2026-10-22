// Regnum — помощники отображения построек (buildings.h, bld::): категории, стоимость и цена в эссенциях, выход и
// рецепт преобразования, генерация эссенции, значки и строки с переносом для редактора, вкладок и выбора строительства.
#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/widgets.h"
#include "gfx/icons.h"

namespace rg::app {

// ================================================================ помощники
namespace bld {

Color catColor(BuildingCat c) {
  int i = clamp(int(c), 0, int(BuildingCat::Count) - 1);
  return Color::hex(schema::kBuildingCats[i].color);
}
const char* catIcon(BuildingCat c) { return schema::kBuildingCats[clamp(int(c), 0, int(BuildingCat::Count) - 1)].icon; }
const char* catName(BuildingCat c) { return schema::kBuildingCats[clamp(int(c), 0, int(BuildingCat::Count) - 1)].name; }

const char* iconOf(const Building& b) { return !b.icon.empty() && gfx::hasIcon(b.icon) ? b.icon.c_str() : catIcon(b.cat); }

int levelTurns(const Building& b, int level) {
  if (level < 1 || level > int(b.levels.size())) return 1;
  return std::max(1, b.levels[size_t(level - 1)].turns);
}

void costChips(const std::map<Id, double>& cost, const Faction* payer, bool showEmpty) {
  const World& w = app().world();
  if (cost.empty()) {
    if (showEmpty) ui::label("Бесплатно", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "coins"});
    return;
  }
  ui::HStack row(20, ui::Align::Left, 4);
  bool first = true;
  for (auto& [res, v] : cost) {
    ui::IdScope s{i64(res)};
    if (!first) ui::spacer(10);
    first = false;
    const CatalogItem* c = w.resource(res);
    bool lack = payer && payer->stock(res) + 1e-9 < v;
    std::string tip = (c ? c->name : std::string("Ресурс"));
    if (payer) tip += ": нужно " + fmtNum(v, 3) + ", есть " + fmtNum(std::max(0.0, payer->stock(res)), 3);
    ui::iconColored(w::resourceIcon(w, res), lack ? ui::theme().danger : w::resourceColor(w, res), 16, tip);
    ui::label(w::amountText(res, v), {.font = ui::Font::Strong, .ink = lack ? ui::Ink::Danger : ui::Ink::Normal, .tooltip = tip});
  }
}

void essCostChips(const std::map<Id, double>& cost, const Faction* payer) {
  const World& w = app().world();
  if (cost.empty()) return;
  ui::IdScope scope("esscost");
  ui::HStack row(20, ui::Align::Left, 4);
  bool first = true;
  for (auto& [e, v] : cost) {
    ui::IdScope s{i64(e)};
    if (!first) ui::spacer(10);
    first = false;
    const CatalogItem* c = w.essence(e);
    const bool lack = payer && payer->essence(e) + 1e-9 < v;
    std::string tip = c ? c->name : std::string("Эссенция");
    if (payer) tip += ": нужно " + fmtNum(v, 3) + ", есть " + fmtNum(std::max(0.0, payer->essence(e)), 3);
    ui::iconColored("essence", lack ? ui::theme().danger : w::essenceColor(w, e), 16, tip);
    ui::label(fmtNum(v, 3), {.font = ui::Font::Strong, .ink = lack ? ui::Ink::Danger : ui::Ink::Normal, .tooltip = tip});
  }
}

std::string essenceCostText(const World& w, const std::map<Id, double>& cost) {
  std::vector<std::string> p;
  for (auto& [e, v] : cost) {
    const CatalogItem* c = w.essence(e);
    p.push_back((c ? c->name : std::string("Эссенция")) + " " + fmtNum(v, 3));
  }
  return join(p, ", ");
}

std::string shipsText(u32 ships) {
  std::vector<std::string> p;
  for (int t = 0; t < int(ShipType::Count); t++)
    if ((ships >> t) & 1u) p.push_back(schema::shipType(ShipType(t)).name);
  return p.empty() ? std::string("—") : join(p, ", ");
}

void produceChips(const std::map<Id, double>& produce) {
  const World& w = app().world();
  if (produce.empty()) return;
  ui::IdScope scope("produce");
  ui::HStack row(20, ui::Align::Left, 4);
  ui::icon("repeat", ui::Ink::Muted, 14, "Даёт за ход");
  for (auto& [res, v] : produce) {
    ui::IdScope s{i64(res)};
    ui::spacer(6);
    const CatalogItem* c = w.resource(res);
    const std::string unit = res == kGold ? " тыс." : "";
    std::string tip = (c ? c->name : std::string("Ресурс")) + ": +" + fmtNum(v, 3) + unit + " за ход";
    ui::iconColored(w::resourceIcon(w, res), w::resourceColor(w, res), 16, tip);
    ui::label("+" + fmtNum(v, 3) + unit, {.font = ui::Font::Strong, .ink = ui::Ink::Success, .tooltip = tip});
  }
}

std::string produceText(const World& w, const std::map<Id, double>& produce) {
  std::vector<std::string> p;
  for (auto& [res, v] : produce) {
    const CatalogItem* c = w.resource(res);
    p.push_back("+" + fmtNum(v, 3) + (res == kGold ? std::string(" тыс. золота") : " " + (c ? utf8::lower(c->name) : std::string("ресурс"))));
  }
  return join(p, ", ");
}

std::map<Id, double> produceAt(const BuildingLevel& L, Id provinceRes) {
  std::map<Id, double> prod = L.produce;
  for (auto& [res, v] : L.produceOnRes)
    if (provinceRes && res == provinceRes) prod[res] = v;
  return prod;
}

std::string produceOnResText(const World& w, const BuildingLevel& L) {
  std::vector<std::string> p;
  for (auto& [res, v] : L.produceOnRes) {
    const std::string n = w::resourceName(w, res);
    p.push_back("с ресурсом «" + n + "» — " + fmtNum(v, 3) + " " + utf8::lower(n));
  }
  return join(p, ", ");
}

void levelProduceChips(const World& w, const BuildingLevel& L, Id provinceRes, bool inProvince) {
  std::map<Id, double> prod = inProvince ? produceAt(L, provinceRes) : L.produce;
  if (!inProvince)
    for (auto& [res, v] : L.produceOnRes)
      if (!prod.count(res)) prod[res] = 0;
  if (prod.empty()) return;
  ui::IdScope scope("lvprod");
  std::vector<Token> tk;
  tk.push_back({"repeat", ui::theme().textMuted, {}, ui::Ink::Muted, "Даёт за ход"});
  for (auto& [res, v] : prod) {
    const std::string name = w::resourceName(w, res);
    const std::string unit = res == kGold ? " тыс." : "";
    auto on = L.produceOnRes.find(res);
    std::string text = "+" + fmtNum(v, 3) + unit;
    std::string tip = name + ": +" + fmtNum(v, 3) + unit + " за ход";
    if (on != L.produceOnRes.end()) {
      if (inProvince && provinceRes == res) {
        tip += " — ресурс провинции «" + name + "»";
      } else {
        if (!inProvince) text += " · " + fmtNum(on->second, 3);
        tip += "; с ресурсом провинции «" + name + "» — +" + fmtNum(on->second, 3) + unit;
      }
    }
    tk.push_back({w::resourceIcon(w, res), w::legible(w::resourceColor(w, res), 0.12f), text, ui::Ink::Success, tip});
  }
  tokens(tk, 6);
}

void levelEffects(const World& w, const BuildingLevel& L, bool compact) {
  ui::IdScope s("fx");
  if (!tree::effectChips(w, L.modifiers) && !compact) ui::label("Без эффектов", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
}

std::string costText(const World& w, const std::map<Id, double>& cost) {
  if (cost.empty()) return "бесплатно";
  std::vector<std::string> p;
  for (auto& [res, v] : cost) {
    const CatalogItem* c = w.resource(res);
    p.push_back((c ? c->name : std::string("Ресурс")) + " " + w::amountText(res, v));
  }
  return join(p, ", ");
}

void iconTile(const Building& b, float size, bool dim) {
  RectF r = ui::next(size, size);
  Color c = catColor(b.cat);
  ui::draw::rect(r, c.alpha(dim ? 0.08f : 0.16f), std::round(size * 0.26f));
  ui::draw::rectStroke(r, c.alpha(dim ? 0.2f : 0.35f), std::round(size * 0.26f), 1);
  float is = std::round(size * 0.56f);
  ui::draw::icon(iconOf(b), RectF{r.cx() - is * 0.5f, r.cy() - is * 0.5f, is, is}, dim ? c.alpha(0.5f) : c);
}

void tokens(const std::vector<Token>& list, float gap) {
  if (list.empty()) return;
  ui::IdScope scope("tokens");
  const RectF area = ui::avail();
  const float h = 20, rowGap = 4;
  float x = area.x, y = area.y;
  for (size_t i = 0; i < list.size(); i++) {
    const Token& t = list[i];
    ui::IdScope s{int(i)};
    const float iw = t.icon.empty() ? 0.f : (t.text.empty() ? 16.f : 19.f);
    const float tw = t.text.empty() ? 0.f : std::ceil(ui::measure(t.text, t.font)) + 2;
    const float w = iw + tw;
    if (x > area.x && x + w > area.right() + 0.5f) {
      x = area.x;
      y += h + rowGap;
    }
    if (!t.icon.empty()) {
      ui::at(RectF{x, y + 2, 16, 16});
      ui::iconColored(t.icon.c_str(), t.color, 16, t.tip);
    }
    if (!t.text.empty()) {
      ui::at(RectF{x + iw, y, tw, h});
      ui::label(t.text, {.font = t.font, .ink = t.ink, .tooltip = t.tip});
    }
    x += w + gap;
  }
  ui::next(y + h - area.y);
}

bool recipeValid(const World& w, const Recipe& r) {
  bool in = false;
  for (const ResAmount& x : r.in) in = in || (x.amount > 0 && w.resource(x.res));
  for (const ResAmount& x : r.essIn) in = in || (x.amount > 0 && w.essence(x.res));
  bool out = false;
  for (const ResAmount& x : r.out) out = out || (x.amount > 0 && w.resource(x.res));
  return in && out;
}

std::vector<Token> recipeTokens(const World& w, const Recipe& r, const Faction* payer) {
  const ui::Theme& th = ui::theme();
  std::vector<Token> out;
  for (const ResAmount& x : r.in) {
    const CatalogItem* c = w.resource(x.res);
    if (!c || !(x.amount > 0)) continue;
    const bool lack = payer && payer->stock(x.res) + 1e-9 < x.amount;
    std::string tip = w::orName(c->name, "Ресурс") + " на входе: " + fmtNum(x.amount, 3);
    if (payer) tip += ", есть " + fmtNum(std::max(0.0, payer->stock(x.res)), 3);
    out.push_back({w::resourceIcon(w, x.res), lack ? th.danger : w::legible(w::resourceColor(w, x.res), 0.12f), fmtNum(x.amount, 3), lack ? ui::Ink::Danger : ui::Ink::Normal, tip});
  }
  for (const ResAmount& x : r.essIn) {
    const CatalogItem* c = w.essence(x.res);
    if (!c || !(x.amount > 0)) continue;
    const bool lack = payer && payer->essence(x.res) + 1e-9 < x.amount;
    std::string tip = w::orName(c->name, "Эссенция") + " на входе: " + fmtNum(x.amount, 3);
    if (payer) tip += ", есть " + fmtNum(std::max(0.0, payer->essence(x.res)), 3);
    out.push_back({"essence", lack ? th.danger : w::essenceColor(w, x.res), fmtNum(x.amount, 3), lack ? ui::Ink::Danger : ui::Ink::Normal, tip});
  }
  if (out.empty()) out.push_back({"warning", th.warning, "нет входа", ui::Ink::Warning, {}, ui::Font::Small});
  out.push_back({"arrow-right", th.textMuted, {}, ui::Ink::Muted, {}});
  bool any = false;
  for (const ResAmount& x : r.out)
    if (const CatalogItem* c = w.resource(x.res)) {
      out.push_back({w::resourceIcon(w, x.res), w::legible(w::resourceColor(w, x.res), 0.12f), fmtNum(x.amount, 3), ui::Ink::Success,
                     w::orName(c->name, "Ресурс") + " на выходе: " + fmtNum(x.amount, 3)});
      any = true;
    }
  if (!any) out.push_back({"warning", th.warning, "нет выхода", ui::Ink::Warning, {}, ui::Font::Small});
  out.push_back({"hourglass", th.textMuted, nTurns(std::max(1, r.turns)), ui::Ink::Muted, "Цикл преобразования", ui::Font::Small});
  return out;
}

std::string recipeText(const World& w, const Recipe& r) {
  std::vector<std::string> in;
  for (const ResAmount& x : r.in)
    if (const CatalogItem* c = w.resource(x.res); c && x.amount > 0) in.push_back(c->name + " " + fmtNum(x.amount, 3));
  for (const ResAmount& x : r.essIn)
    if (const CatalogItem* c = w.essence(x.res); c && x.amount > 0) in.push_back(c->name + " " + fmtNum(x.amount, 3));
  std::vector<std::string> out;
  for (const ResAmount& x : r.out)
    if (const CatalogItem* c = w.resource(x.res)) out.push_back(c->name + " " + fmtNum(x.amount, 3));
  std::string s = (in.empty() ? std::string("—") : join(in, " + ")) + " → " + (out.empty() ? std::string("—") : join(out, " + ")) + " · " +
                  nTurns(std::max(1, r.turns));
  return trim(r.name).empty() ? s : trim(r.name) + ": " + s;
}

std::vector<Token> drainTokens(const World& w, const BuildingLevel& L) {
  const ui::Theme& th = ui::theme();
  std::vector<Token> out;
  if (L.drainPop <= 0 && L.drainEss.empty()) return out;
  const std::string pop = fmtNum(double(std::max<i64>(0, L.drainPop)));
  out.push_back({"population", Color::hex(kDrainInk), "−" + pop, ui::Ink::Danger, "Жителей выбранной расы провинции за ход"});
  out.push_back({"arrow-right", th.textMuted, {}, ui::Ink::Muted, {}});
  bool any = false;
  for (auto& [e, v] : L.drainEss) {
    const CatalogItem* c = w.essence(e);
    if (!c || !(v > 0)) continue;
    any = true;
    out.push_back({"essence", w::essenceColor(w, e), "+" + fmtNum(v, 3), ui::Ink::Success,
                   w::orName(c->name, "Эссенция") + " за " + pop + " жителей (меньше жителей — меньше)"});
  }
  if (!any) out.push_back({"warning", th.warning, "нет эссенции", ui::Ink::Warning, {}, ui::Font::Small});
  return out;
}

std::string drainText(const World& w, const BuildingLevel& L) {
  std::vector<std::string> e;
  for (auto& [id, v] : L.drainEss)
    if (const CatalogItem* c = w.essence(id); c && v > 0) e.push_back(utf8::lower(c->name) + " " + fmtNum(v, 3));
  return fmtNum(double(std::max<i64>(0, L.drainPop))) + " жителей за ход" + (e.empty() ? std::string() : " → " + join(e, ", "));
}

std::string stateKindsText(u32 kinds) {
  std::vector<std::string> p;
  for (int k = 0; k < int(StateKind::Count); k++)
    if ((kinds >> unsigned(k)) & 1u) p.push_back(schema::stateKind(StateKind(k)).name);
  return join(p, ", ");
}

std::string extractText(const World& w, const Building& b) {
  std::vector<std::string> g, x;
  for (Id id : b.extractGroups)
    if (w.catalogs->group(id)) g.push_back(rules::groupPath(w, id));
  for (Id id : b.extractExclude)
    if (const CatalogItem* c = w.resource(id)) x.push_back(w::orName(c->name, "Ресурс"));
  std::string s = g.empty() ? std::string("группы не заданы") : join(g, ", ");
  if (!x.empty()) s += "; кроме: " + join(x, ", ");
  return s;
}

namespace {
std::string pctText(double v) { return fmtPct(v, std::fabs(v - std::round(v)) > 1e-9 ? 1 : 0); }
}  // namespace

std::vector<Token> featureTokens(const World& w, const Building& b, const BuildingLevel* L) {
  const ui::Theme& th = ui::theme();
  std::vector<Token> tk;
  for (int k = 0; k < int(StateKind::Count); k++)
    if ((b.stateKinds >> unsigned(k)) & 1u)
      tk.push_back({schema::stateKind(StateKind(k)).icon, th.warning, {}, ui::Ink::Normal,
                    std::string("Только для вида государства: ") + schema::stateKind(StateKind(k)).name});
  if (b.market) tk.push_back({"scales", th.accent, {}, ui::Ink::Normal, "Рынок: «Продать реликвии» из хранилищ реликвий государства"});
  if (b.upkeepCut) {
    const double v = L ? L->upkeepCut : 0;
    tk.push_back({"army", th.success, L && v > 0 ? "−" + pctText(v) : std::string(), ui::Ink::Success,
                  "Снижение содержания армии государства" + (L ? ": −" + pctText(v) : std::string()) + " (сумма по государству — не больше " +
                      pctText(schema::kMaxUpkeepCut) + ")",
                  ui::Font::Small});
  }
  if (b.extract) {
    const double v = L ? L->extractPct : 0;
    const std::string more = "Добыча ресурса провинции" + (L ? " +" + pctText(v) : std::string()) + ": " + extractText(w, b);
    tk.push_back({"pickaxe", Color::hex(kExtractInk), L && v > 0 ? "+" + pctText(v) : std::string(), ui::Ink::Success,
                  b.extractOptional ? "Добыча не обязательна: ставится при любом ресурсе провинции\n" + more + " — ресурс провинции из этих групп добывается больше"
                                    : more,
                  ui::Font::Small});
  }
  if (b.drain) {
    std::vector<Token> d = L ? drainTokens(w, *L) : std::vector<Token>{};
    if (d.empty()) d.push_back({"population", Color::hex(kDrainInk), {}, ui::Ink::Normal, "Иссушение населения: жители выбранной расы — в эссенции"});
    tk.insert(tk.end(), d.begin(), d.end());
  }
  if (b.relicStore) tk.push_back({"relic", Color::hex(kRelicInk), {}, ui::Ink::Normal, "Хранилище реликвий"});
  if (b.healing) tk.push_back({"heal", th.success, {}, ui::Ink::Normal, "Здание целительства: «Вылечить провинцию» — эссенция или лечебные травы"});
  if (b.plague) tk.push_back({"plague", Color::hex(kPlagueInk), {}, ui::Ink::Normal, "Здание чумы: «Заразить чумой», чума даёт прирост"});
  if (b.shipyard)
    tk.push_back({"shipyard", th.info, L ? shipsText(L->ships) : std::string(), ui::Ink::Dim, "Верфь: открывает найм кораблей", ui::Font::Small});
  if (b.mercenary) tk.push_back({"mercenary", th.accent, {}, ui::Ink::Normal, "Гильдия наёмников: лимит наёмников государства"});
  if (b.coastal) tk.push_back({"anchor", th.textDim, {}, ui::Ink::Normal, "Только в приморской провинции"});
  return tk;
}

std::vector<Token> essenceTokens(const World& w, const std::map<Id, double>& essence) {
  std::vector<Token> out;
  for (auto& [e, v] : essence) {
    const CatalogItem* c = w.essence(e);
    if (!c || !(v > 0)) continue;
    out.push_back({"essence", w::essenceColor(w, e), "+" + fmtNum(v, 3), ui::Ink::Success, (c->name.empty() ? std::string("Эссенция") : c->name) + ": +" + fmtNum(v, 3) + " за ход"});
  }
  return out;
}

std::string essenceText(const World& w, const std::map<Id, double>& essence) {
  std::vector<std::string> p;
  for (auto& [e, v] : essence)
    if (const CatalogItem* c = w.essence(e); c && v > 0) p.push_back("+" + fmtNum(v, 3) + " " + utf8::lower(c->name));
  return join(p, ", ");
}

const char* cultRule(const Building& b) {
  if (b.cat != BuildingCat::Cult) return nullptr;
  return b.owner ? "Одна на государство" : "Одна на всю карту";
}

std::string specialsText(const World& w, const std::vector<Id>& specials) {
  std::vector<std::string> n;
  for (Id s : specials)
    if (const SpecialUnit* su = w.special(s)) n.push_back(su->name.empty() ? std::string("Без названия") : su->name);
  return join(n, ", ");
}

}  // namespace bld

}  // namespace rg::app
