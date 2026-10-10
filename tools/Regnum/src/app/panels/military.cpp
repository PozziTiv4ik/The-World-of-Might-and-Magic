// Regnum — войска и флот: общие помощники (данные строк, фигурки и портреты полководцев, строки списков,
// подтверждения).
#include "app/panels/military.h"

#include "geo/topo.h"
#include "gfx/figures.h"

namespace rg::app::mil {

// ================================================================ данные
const World& frameWorld(App& a) {
  struct Holder {
    u64 frame = ~0ull;
    const App* app = nullptr;
    World w;
  };
  // Состояние интерфейса (а не static): очищается вместе с ui::shutdown, новое приложение не увидит старый мир.
  Holder& h = ui::state<Holder>(hash64("rg.app.mil.frameWorld"));
  u64 f = ui::frameIndex();
  if (h.frame != f || h.app != &a) {
    h.w = a.world();
    h.frame = f;
    h.app = &a;
  }
  return h.w;
}

std::vector<UnitRow> unitRows(const World& w, Id faction, bool fleet) {
  std::vector<UnitRow> out;
  const Faction* f = w.faction(faction);
  if (!f) return out;
  if (fleet) {
    out.reserve(f->fleet.size());
    for (const FleetRow& r : f->fleet) {
      const schema::EnumInfo& ti = schema::shipType(r.type);
      UnitRow u{r.id, r.name.empty() ? std::string(ti.name) : r.name, ti.icon, ti.name, int(r.type), r.total, r.upkeep};
      // Морское чудовище: ключевой ресурс подгруппы «Морские чудовища» обязателен (ТЗ «Доработки №3», п.8).
      if (r.type == ShipType::SeaMonster) {
        const std::vector<Id> keys = rules::shipKeyResources(w, r.type);
        u.keyMissing = std::find(keys.begin(), keys.end(), r.keyRes) == keys.end();
      }
      out.push_back(std::move(u));
    }
  } else {
    out.reserve(f->army.size());
    for (const ArmyRow& r : f->army) {
      const schema::EnumInfo& ti = schema::unitType(r.type);
      UnitRow u{r.id, r.name.empty() ? std::string(ti.name) : r.name, ti.icon, ti.name, int(r.type), r.total, r.upkeep};
      u.special = r.special && w.special(r.special) ? r.special : 0;
      u.keyMissing = !r.merc && schema::needsKeyResource(r.type) && !rules::keyAllowed(w, r.type, r.keyRes);   // наёмники — только золото
      u.elemental = schema::isElemental(r.type);
      u.merc = r.merc;
      out.push_back(std::move(u));
    }
  }
  return out;
}

std::optional<UnitRow> unitRow(const World& w, Id faction, Id row, bool fleet) {
  for (UnitRow& r : unitRows(w, faction, fleet))
    if (r.id == row) return r;
  return std::nullopt;
}

const rules::RowCalc* rowCalc(const rules::Calc& c, Id faction, Id row, bool fleet) {
  const rules::FactionCalc* fc = c.faction(faction);
  if (!fc) return nullptr;
  for (const rules::RowCalc& r : fleet ? fc->fleet : fc->army)
    if (r.row == row) return &r;
  return nullptr;
}

i64 reserveOf(const World& w, Id faction, Id row, bool fleet) {
  auto c = rules::calc(w);
  const rules::RowCalc* r = rowCalc(*c, faction, row, fleet);
  return r ? std::max<i64>(0, r->reserve) : 0;
}

i64 groupCount(const ArmyGroup& g) {
  i64 n = 0;
  for (const ArmyUnit& u : g.units) n += u.count;
  return n;
}

i64 unitCount(const Army& a) {
  i64 n = 0;
  for (const ArmyGroup& g : a.groups) n += groupCount(g);
  return n;
}

i64 rowCount(const ArmyGroup& g, Id row) {
  i64 n = 0;
  for (const ArmyUnit& u : g.units)
    if (u.row == row) n += u.count;
  return n;
}

std::vector<Id> factionsIn(const Army& a) {
  std::vector<Id> r;
  for (const ArmyGroup& g : a.groups)
    if (std::find(r.begin(), r.end(), g.faction) == r.end()) r.push_back(g.faction);
  return r;
}

std::string objectCaption(const Army& a) {
  if (a.isFleet()) return a.allied() ? "Союзный флот" : "Флот";
  return a.allied() ? "Союзное войско" : "Войско";
}

const char* objectIcon(const Army& a) { return a.isFleet() ? "fleet" : "army"; }

std::string objectName(const Army& a) { return a.name.empty() ? std::string("Без названия") : a.name; }

Id provinceUnder(const World& w, Vec2 p) {
  auto fs = geo::faces(w);
  return fs ? fs->provinceAt(p) : 0;
}

Id heroLocation(const World& w, Id character, Id skip) {
  Id found = 0;
  w.armies.each([&](const Army& a) {
    if (found || a.id == skip) return;
    for (const ArmyGroup& g : a.groups)
      if (std::find(g.heroes.begin(), g.heroes.end(), character) != g.heroes.end()) found = a.id;
  });
  return found;
}

std::string heroBusy(const World& w, Id character, Id skipArmy, Id skipProvince) {
  if (Id at = heroLocation(w, character, skipArmy)) return "в «" + objectName(*w.army(at)) + "»";
  if (Id p = rules::heroGarrison(w, character); p && p != skipProvince) return "гарнизон «" + w.provinceName(p) + "»";
  return {};
}

std::map<Id, double> essenceUpkeep(const World& w, Id faction, const ArmyRow& r) {
  std::map<Id, double> out;
  if (!schema::isElemental(r.type) || r.total <= 0) return out;
  // Как в расчёте мира: эссенция на юнит × численность × модификатор содержания войск государства.
  auto c = rules::calc(w);
  const rules::FactionCalc* fc = c->faction(faction);
  const double k = fc ? std::max(0.0, 1.0 + fc->fx[Fx::ArmyUpkeepPct] / 100.0) : 1.0;
  for (auto& [e, v] : r.essUpkeep)
    if (v > 0 && w.essence(e)) out[e] = double(r.total) * v * k;
  return out;
}

std::string fmtCount(i64 n) { return fmtInt(n); }

std::string fmtMoney(double v) { return fmtNum(v, 3); }   // лишние нули fmtNum отбрасывает

Color leaderColor(const World& w, const Army& a) { return w::factionColor(w, a.leader()); }

Color allyColor(const World& w, const Army& a) {
  if (!a.allied()) return Color(0, 0, 0, 0);
  return w::factionColor(w, a.groups[1].faction);
}

// ================================================================ виджеты
void figureIn(RectF r, ArmyKind kind, Color c1, bool allied, Color c2, bool selected) {
  ui::custom(r, [=](gfx::Canvas& c, RectF dev, float) {
    // Габариты фигурки: ширина 1,28 диаметра постамента, высота 1,40 (наконечник выше, тень ниже).
    float size = std::min(dev.w / 1.28f, dev.h / 1.40f);
    gfx::Pt ctr{dev.cx(), dev.y + (dev.h - size * 1.40f) * 0.5f + size * 0.74f};
    if (kind == ArmyKind::Fleet) gfx::drawFleetFigure(c, ctr, size, c1, selected, allied, c2);
    else gfx::drawArmyFigure(c, ctr, size, c1, selected, allied, c2);
  });
}

const gfx::Image* commanderFace(const World& w, const Army& a) {
  const Character* c = a.commander ? w.character(a.commander) : nullptr;
  if (!c || c->portrait.empty()) return nullptr;
  const gfx::Image* img = w::faceImage(*c);   // ещё декодируется — nullptr (следующий кадр покажет портрет)
  return img && !img->empty() ? img : nullptr;
}

void objectBadgeIn(const World& w, const Army& a, RectF r, bool selected) {
  const gfx::Image* face = commanderFace(w, a);
  if (!face) {
    figureIn(r, a.kind, leaderColor(w, a), a.allied(), allyColor(w, a), selected);
    return;
  }
  const ui::Theme& t = ui::theme();
  const float d = std::floor(std::min(r.w, r.h) * 0.92f);   // диаметр с кольцом
  const float cx = r.cx(), cy = r.cy(), R = d * 0.5f;
  const float ring = std::max(2.f, std::round(d * 0.075f));
  if (selected) ui::draw::ring(cx, cy, R + 2.5f, 2, t.accent);
  ui::draw::circle(cx, cy, R, t.surface3);
  const float ir = R - ring;   // портрет внутри кольца
  ui::draw::image(*face, RectF{cx - ir, cy - ir, ir * 2, ir * 2}, ir);
  // Кольцо цвета фракции-лидера; союзное войско — правая половина цветом второго союзника.
  const float rm = R - ring * 0.5f;
  ui::draw::ring(cx, cy, rm, ring, leaderColor(w, a));
  if (a.allied()) {
    gfx::Path arc;
    const int n = 24;
    for (int i = 0; i <= n; i++) {
      const float ang = float(kPi) * (-0.5f + float(i) / float(n));
      const float x = cx + rm * std::cos(ang), y = cy + rm * std::sin(ang);
      if (i == 0) arc.moveTo(x, y);
      else arc.lineTo(x, y);
    }
    ui::draw::pathStroke(arc, allyColor(w, a), ring, gfx::Cap::Butt);
  }
  ui::draw::ring(cx, cy, R, 1, Color(0, 0, 0, t.dark ? 90 : 50));
  // Крупный значок: вид объекта (войско или флот) — в уголке.
  if (d >= 40) {
    const float bs = std::round(d * 0.36f);
    const RectF b{cx + R * 0.7071f - bs * 0.5f, cy + R * 0.7071f - bs * 0.5f, bs, bs};
    ui::draw::circle(b.cx(), b.cy(), bs * 0.5f + 1, t.surface1);
    ui::draw::circle(b.cx(), b.cy(), bs * 0.5f, t.surface3);
    const float is = std::round(bs * 0.62f);
    ui::draw::icon(objectIcon(a), RectF{b.cx() - is * 0.5f, b.cy() - is * 0.5f, is, is}, t.textDim);
  }
}

void figure(const World& w, const Army& a, float size, bool selected) {
  RectF r = ui::next(size, size);
  objectBadgeIn(w, a, r, selected);
}

void cellTip(std::string_view key, RectF r, std::string_view text) { ui::hoverTip(key, r, text); }

void factionFlag(const World& w, Id faction, float width, float height) {
  const Faction* f = w.faction(faction);
  float h = height > 0 ? height : std::round(width * 2.f / 3.f);
  if (!f) {
    RectF r = ui::next(width, h);
    ui::draw::rect(r, ui::theme().surface3, 3);
    return;
  }
  ui::flag(f->flag, width, h, 3, f->name);
}

void factionLabel(const World& w, Id faction, float flagW) {
  ui::IdScope s(i64(faction) + 0x51000000LL);
  ui::HStack hs(24, ui::Align::Left, 6);
  factionFlag(w, faction, flagW);
  w::factionChip(faction);
}

void typeTile(RectF r, const char* icon, Color tint) {
  const ui::Theme& t = ui::theme();
  ui::draw::rect(r, t.surface3, 7);
  float s = std::min(r.w, r.h) * 0.62f;
  ui::draw::icon(icon, RectF{r.cx() - s * 0.5f, r.cy() - s * 0.5f, s, s}, tint);
}

bool objectItem(const World& w, const Army& a, bool selected, std::string_view subtitle) {
  ui::IdScope s(i64(a.id) + 0x52000000LL);
  i64 n = unitCount(a);
  std::string hint = n >= 100000 ? fmtShort(double(n)) : fmtCount(n);
  const std::string name = objectName(a), caption = objectCaption(a);
  const std::string_view badge = a.allied() ? std::string_view("союз") : std::string_view();
  bool clicked = ui::listItem(name, {.dot = leaderColor(w, a), .subtitle = subtitle, .hint = hint, .badge = badge, .selected = selected,
                                     .tooltip = caption});
  // Значок поверх цветной точки списка: портрет главного полководца или фигурка, как на карте.
  RectF r = ui::lastItem().rect;
  objectBadgeIn(w, a, RectF{r.x + 4, r.cy() - 13, 26, 26}, false);
  // Название не поместилось (место — как в строке списка: точка, бейдж, число справа) — полное в подсказке.
  float room = r.w - 12 - 20 - 10;
  if (!badge.empty()) room -= std::max(18.f, ui::measure(badge, ui::Font::Caption) + 14) + 8;
  room -= std::min(ui::measure(hint, ui::Font::Small), room * 0.45f) + 10;
  if (ui::measure(name, selected ? ui::Font::Strong : ui::Font::Body) > room) ui::tooltip(name + "\n" + caption);
  return clicked;
}

void cellLines(RectF r, std::string_view top, std::string_view bottom, ui::Align align, ui::Ink topInk, ui::Ink bottomInk) {
  float h1 = ui::lineHeight(ui::Font::Body), h2 = ui::lineHeight(ui::Font::Caption);
  if (bottom.empty()) {
    ui::draw::text(top, RectF{r.x, r.y, r.w, r.h}, ui::Font::Body, ui::inkColor(topInk), align);
    return;
  }
  float y0 = std::round(r.cy() - (h1 + h2) * 0.5f);
  ui::draw::text(top, RectF{r.x, y0, r.w, h1}, ui::Font::Body, ui::inkColor(topInk), align);
  ui::draw::text(bottom, RectF{r.x, y0 + h1, r.w, h2}, ui::Font::Caption, ui::inkColor(bottomInk), align);
}

void nameCell(RectF r, std::string_view name, std::string_view caption) {
  const ui::Theme& t = ui::theme();
  const float full = ui::measure(name, ui::Font::Body);
  size_t cut = std::string_view::npos;
  if (full > r.w)   // перенос по последнему пробелу, при котором первая строка помещается
    for (size_t p = name.find(' '); p != std::string_view::npos; p = name.find(' ', p + 1)) {
      if (ui::measure(name.substr(0, p), ui::Font::Body) <= r.w) cut = p;
      else break;
    }
  bool clipped = false;
  if (cut == std::string_view::npos) {
    cellLines(r, name, caption, ui::Align::Left);
    clipped = full > r.w + 0.5f;
  } else {
    float h = ui::lineHeight(ui::Font::Body) - 1;
    float y0 = std::round(r.cy() - h);
    ui::draw::text(name.substr(0, cut), RectF{r.x, y0, r.w, h}, ui::Font::Body, t.text);
    ui::draw::text(name.substr(cut + 1), RectF{r.x, y0 + h, r.w, h}, ui::Font::Body, t.text);
    clipped = ui::measure(name.substr(cut + 1), ui::Font::Body) > r.w + 0.5f;
  }
  if (clipped) cellTip("##name", r, name);
}

void unitCell(RectF r, const UnitRow& row, bool accent, std::string_view caption, bool hire) {
  const ui::Theme& t = ui::theme();
  const bool keyMissing = hire && row.keyMissing;
  float s = std::min(28.f, r.h - 8);
  RectF tile{r.x, std::round(r.cy() - s * 0.5f), s, s};
  ui::draw::rect(tile, t.surface3, 7);
  if (keyMissing) ui::draw::rectStroke(tile, t.danger, 7, 1.5f);
  float is = std::round(s * 0.62f);
  // Подсказка плитки — тип войск, особый отряд и незаданный ключевой ресурс.
  std::string tip = row.typeName;
  if (row.special) tip += "\nОсобый отряд";
  if (row.merc) tip += "\nНаёмники";
  if (keyMissing) tip += "\nНе указан ключевой ресурс — найм недоступен";
  ui::at(RectF{std::round(tile.cx() - is * 0.5f), std::round(tile.cy() - is * 0.5f), is, is});
  ui::icon(row.icon, keyMissing ? ui::Ink::Danger : accent ? ui::Ink::Accent : ui::Ink::Dim, is, tip);
  // Отметки в углах плитки: особый отряд (сверху справа), нет ключевого ресурса (снизу справа).
  const float m = std::max(10.f, std::round(s * 0.42f));
  if (row.special) {
    const RectF b{tile.right() - m * 0.6f, tile.y - m * 0.4f, m, m};
    ui::draw::circle(b.cx(), b.cy(), m * 0.5f + 1, t.surface1);
    ui::draw::circle(b.cx(), b.cy(), m * 0.5f, t.accent);
    ui::draw::icon("special-unit", b.inset(m * 0.18f), t.onAccent);
  }
  if (row.merc) {   // наёмники — отметка сверху справа (золото)
    const RectF b{tile.right() - m * 0.6f, tile.y - m * 0.4f, m, m};
    ui::draw::circle(b.cx(), b.cy(), m * 0.5f + 1, t.surface1);
    ui::draw::circle(b.cx(), b.cy(), m * 0.5f, t.warning);
    ui::draw::icon("mercenary", b.inset(m * 0.16f), t.onAccent);
  }
  if (keyMissing) {
    const RectF b{tile.right() - m * 0.6f, tile.bottom() - m * 0.6f, m, m};
    ui::draw::circle(b.cx(), b.cy(), m * 0.5f + 1, t.surface1);
    ui::draw::circle(b.cx(), b.cy(), m * 0.5f, t.danger);
    ui::draw::text("!", b, ui::Font::Caption, Color(255, 255, 255), ui::Align::Center);
  }
  nameCell(RectF{r.x + s + 10, r.y, r.w - s - 10, r.h}, row.name, caption);
}

// ================================================================ действия
void askDisband(App& a, Id army) {
  const Army* ar = a.world().army(army);
  if (!ar) return;
  if (a.readOnly()) {
    a.act("Расформировать", [](Tx&) {});   // сообщение о просмотре прошлого хода
    return;
  }
  bool fleet = ar->isFleet();
  i64 n = unitCount(*ar);
  std::string title = fleet ? "Расформировать флот?" : "Расформировать войско?";
  std::string text = objectName(*ar) + ": " + fmtCount(n) + " " +
                     (fleet ? plural(n, "корабль вернётся", "корабля вернутся", "кораблей вернутся")
                            : plural(n, "воин вернётся", "воина вернутся", "воинов вернутся")) +
                     " в резерв. Герои останутся у фракции.";
  std::string label = std::string(fleet ? "Расформировать флот " : "Расформировать войско ") + objectName(*ar);
  a.confirm(title, text, "Расформировать", true, [army, label](App& x) {
    if (x.act(label, [&](Tx& tx) { rules::disband(tx, army); })) x.toast("Отряды возвращены в резерв", ToastKind::Success, "disband");
  });
}

void dissolveAllied(App& a, Id army) {
  const Army* ar = a.world().army(army);
  if (!ar || !ar->allied()) return;
  bool fleet = ar->isFleet();
  std::vector<Id> out;
  if (a.act(fleet ? "Распустить союзный флот" : "Распустить союзное войско", [&](Tx& tx) { out = rules::dissolveAllied(tx, army); })) {
    a.toast(std::string(fleet ? "Союзный флот распущен: " : "Союзное войско распущено: ") + std::to_string(out.size()) + " " +
                plural(i64(out.size()), "объект", "объекта", "объектов") + " на карте",
            ToastKind::Success, "dissolve");
  }
}

void askRemoveRow(App& a, Id faction, Id row, bool fleet) {
  auto r = unitRow(a.world(), faction, row, fleet);
  if (!r) return;
  if (a.readOnly()) {
    a.act("Удалить строку", [](Tx&) {});
    return;
  }
  const World& w = a.world();
  auto c = rules::calc(w);
  const rules::RowCalc* rc = rowCalc(*c, faction, row, fleet);
  i64 field = rc ? rc->field : 0, forming = rc ? rc->forming : 0;
  std::vector<std::string> parts;
  // ТЗ «Общие доработки», п.10.4: воины удалённой строки возвращаются в население (нежить — в трупы, демоны —
  // в демоническую энергию).
  const Faction* f = w.faction(faction);
  const ArmyRow* ar = f && !fleet ? f->armyRow(row) : nullptr;
  if (ar && ar->total > 0) {
    const std::string race = rules::unitRace(w, faction, *ar);
    const char* to = race == schema::kRaceUndead    ? " в запас трупов"
                     : race == schema::kRaceDemonic ? " в запас демонической энергии"
                     : f->isState()                 ? " в население государства"
                                                    : nullptr;
    if (to) parts.push_back(fmtCount(ar->total) + " " + plural(ar->total, "воин вернётся", "воина вернутся", "воинов вернутся") + to + ".");
  }
  if (field > 0)
    parts.push_back(std::string(fleet ? "Корабли уйдут из флотов на карте" : "Отряды уйдут из войск и гарнизонов") + ": " + fmtCount(field) + ".");
  if (forming > 0) parts.push_back("Формирование " + fmtCount(forming) + " отменится с возвратом.");
  std::string text = parts.empty() ? std::string("Строка будет удалена из таблицы.") : join(parts, " ");
  a.confirm(std::string(fleet ? "Удалить судно «" : "Удалить отряд «") + r->name + "»?", text, "Удалить", true, [faction, row, fleet](App& x) {
    x.act(fleet ? "Удалить судно из таблицы флота" : "Удалить отряд из таблицы войск", [&](Tx& tx) { rules::removeRow(tx, faction, row); });
  });
}

}  // namespace rg::app::mil
