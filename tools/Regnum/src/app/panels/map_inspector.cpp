// Regnum — инспектор объектов карты (режим «Правка карты»): знак (гора, крупная гора, замок, башня) — вид, рисунок,
// размер, положение, порядок наложения; несколько знаков — сводка и удаление; фигура (озеро, островок, стена,
// река) — точки и острова, ширина и пунктир линии. Все правки — одной записью отмены (перетаскивание чисел
// склеивается), первая правка переносит объекты базовой карты в мир.
#include "app/tools_map.h"

namespace rg::app {

namespace {

using platform::Key;

struct KindItem {
  SymbolKind kind;
  u8 variant;
  const char* label;
  const char* tip;
};
constexpr KindItem kKinds[] = {
    {SymbolKind::Mountain, 0, "Гора", "Гора со светлой вершиной"},
    {SymbolKind::Mountain, 1, "Гора 2", "Гора с тёмной вершиной"},
    {SymbolKind::Peak, 0, "Крупная", "Крупная гора"},
    {SymbolKind::Castle, 0, "Замок", "Замок"},
    {SymbolKind::Tower, 0, "Башня", "Башня"},
};

bool editable(const App& a) { return !a.readOnly() && a.ui.editMap; }

// Подсказка, когда правка карты выключена.
void needMapEdit(App& a) {
  if (a.readOnly() || a.ui.editMap) return;
  ui::spacer(4);
  ui::HStack hs(30, ui::Align::Left, 8);
  ui::label("Правка — в режиме «Правка карты»", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
  ui::flex();
  if (ui::button("Включить", {.icon = "map-edit", .size = ui::Size::Small})) a.setEditMap(true);
  ui::tooltip("Правка карты", {Key::T, 0});
}

// ---------------------------------------------------------------- знак
void symbolHeader(App& a, Id id) {
  const MapSymbol* s = detail::mapSymbol(a.world(), id);
  if (!s) return;
  const std::vector<Id> group = mapedit::selectedSymbols(a);
  ui::caption("Знак карты");
  if (group.size() > 1) ui::label("Выбрано знаков: " + std::to_string(group.size()), {.font = ui::Font::Display});
  else ui::label(mapedit::symbolName(s->kind), {.font = ui::Font::Display, .icon = mapedit::symbolIcon(s->kind)});
}

void symbolProps(App& a, Id id) {
  const World& w = a.world();
  const MapSymbol* s = detail::mapSymbol(w, id);
  if (!s) return;
  const bool ro = !editable(a);
  const std::vector<Id> group = mapedit::selectedSymbols(a);
  if (group.size() > 1) {
    ui::text("Тяните любой из выбранных знаков на карте — сдвинутся все. Shift+щелчок — добавить или убрать знак.", ui::Font::Small, ui::Ink::Dim);
    ui::spacer(4);
    ui::HStack hs(30, ui::Align::Left, 8);
    if (ui::button("Снять выбор", {.icon = "close"})) mapedit::selectSymbols(a, {});
    ui::flex();
    if (ui::button("Удалить все", {.variant = ui::Variant::Danger, .icon = "trash", .disabled = ro})) {
      const std::vector<Id> ids = group;
      if (mapedit::act(a, "Удалить знаки", [&](Tx& tx) {
            for (Id x : ids)
              if (tx.w().symbol(x)) rules::removeSymbol(tx, x);
          }))
        mapedit::selectSymbols(a, {});
    }
    a.markUi("mapobject.delete");
    return;
  }
  ui::Disabled dis(ro);
  // Вид и рисунок.
  ui::caption("Вид");
  int idx = 0;
  for (int i = 0; i < int(std::size(kKinds)); i++)
    if (kKinds[i].kind == s->kind && kKinds[i].variant == s->v) idx = i;
  std::vector<ui::Segment> segs;
  for (const KindItem& k : kKinds) segs.push_back(ui::Segment{nullptr, k.label, k.tip});
  if (ui::segmented("kind", idx, std::span<const ui::Segment>(segs))) {
    const KindItem k = kKinds[idx];
    const float sc = s->s;
    mapedit::act(a, "Знак: вид", [&](Tx& tx) { rules::setSymbol(tx, id, k.kind, sc, k.variant); });
  }
  a.markUi("mapobject.kind");
  ui::spacer(6);
  // Размер.
  ui::caption("Размер");
  {
    float sc = s->s;
    TxOptions opt;
    opt.coalesce = "mapobject.scale." + std::to_string(id);
    if (ui::numberField("scale", sc,
                        {.min = schema::kMinSymbolScale, .max = schema::kMaxSymbolScale, .step = 0.1, .digits = 2, .label = "×", .steppers = true,
                         .tooltip = "1 — как на исходной карте"})) {
      const SymbolKind kind = s->kind;
      const u8 v = s->v;
      mapedit::act(a, "Знак: размер", [&](Tx& tx) { rules::setSymbol(tx, id, kind, sc, v); }, opt);
    }
    a.markUi("mapobject.scale");
  }
  ui::spacer(6);
  // Положение (точка привязки — середина основания значка).
  ui::caption("Положение");
  {
    ui::Row row({ui::fr(1), ui::fr(1)}, 30, 8);
    double x = s->p.x, y = s->p.y;
    TxOptions opt;
    opt.coalesce = "mapobject.pos." + std::to_string(id);
    const double z = s->z;
    bool ch = ui::numberField("x", x, {.min = 0, .max = schema::kMapWidth, .step = 1, .digits = 1, .label = "X", .tooltip = "Середина основания значка"});
    a.markUi("mapobject.x");
    ch |= ui::numberField("y", y, {.min = 0, .max = schema::kMapHeight, .step = 1, .digits = 1, .label = "Y", .tooltip = "Середина основания значка"});
    a.markUi("mapobject.y");
    if (ch) mapedit::act(a, "Знак: положение", [&](Tx& tx) { rules::placeSymbol(tx, id, {x, y}, z); }, opt);
  }
  ui::spacer(6);
  // Порядок наложения.
  ui::caption("Порядок");
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    if (ui::button("Выше соседей", {.icon = "arrow-up"})) mapedit::restack(a, {id}, true);
    ui::tooltip("Поверх пересекающихся знаков");
    a.markUi("mapobject.front");
    if (ui::button("Ниже", {.icon = "arrow-down"})) mapedit::restack(a, {id}, false);
    ui::tooltip("Под пересекающимися знаками");
    a.markUi("mapobject.back");
  }
  ui::spacer(8);
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    if (ui::button("Показать на карте", {.icon = "target"})) a.focusSelection();
    ui::flex();
    if (ui::button("Удалить", {.variant = ui::Variant::Danger, .icon = "trash"})) {
      if (mapedit::act(a, "Удалить знак", [&](Tx& tx) { rules::removeSymbol(tx, id); })) mapedit::selectSymbols(a, {});
    }
    ui::tooltip("Удалить знак", {Key::Delete, 0});
    a.markUi("mapobject.delete");
  }
  needMapEdit(a);
}

// ---------------------------------------------------------------- фигура
void shapeHeader(App& a, Id id) {
  const MapShape* s = detail::mapShape(a.world(), id);
  if (!s) return;
  ui::caption("Фигура карты");
  ui::label(detail::mapShapeName(*s), {.font = ui::Font::Display, .icon = mapedit::shapeIcon(s->kind)});
}

void shapeProps(App& a, Id id) {
  const World& w = a.world();
  const MapShape* s = detail::mapShape(w, id);
  if (!s) return;
  const bool ro = !editable(a);
  // Сводка.
  {
    ui::Row row({ui::fr(1), ui::fr(1)}, 64, 10);
    ui::stat(std::to_string(s->pts.size()), "Точек", {.icon = "map-pin"});
    if (s->closed()) {
      double area = std::fabs(geo::signedArea(s->pts));
      for (const auto& h : s->holes) area -= std::fabs(geo::signedArea(h));
      ui::stat(fmtNum(area), "Площадь", {.icon = "measure", .tone = ui::Tone::Info, .tooltip = "В квадратных пикселях карты"});
    } else {
      double len = 0;
      for (size_t i = 1; i < s->pts.size(); i++) len += dist(s->pts[i - 1], s->pts[i]);
      ui::stat(fmtNum(len), "Длина", {.icon = "ruler", .tone = ui::Tone::Info, .tooltip = "В пикселях карты"});
    }
  }
  if (s->kind == ShapeKind::Water && !s->holes.empty())
    ui::label("Островов внутри: " + std::to_string(s->holes.size()), {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "island"});
  ui::spacer(6);
  ui::Disabled dis(ro);
  if (!s->closed()) {
    ui::caption("Линия");
    float width = s->w, dash = s->dash;
    TxOptions opt;
    opt.coalesce = "mapobject.line." + std::to_string(id);
    bool ch = false;
    ui::prop("Ширина");
    ch |= ui::numberField("width", width,
                          {.min = schema::kMinShapeWidth, .max = schema::kMaxShapeWidth, .step = 0.5, .digits = 1, .steppers = true,
                           .tooltip = "Ширина линии в единицах карты"});
    a.markUi("mapobject.width");
    if (s->kind == ShapeKind::Wall) {
      bool dashed = dash > 0;
      ui::prop("Пунктир");
      if (ui::toggle("", dashed)) {
        dash = dashed ? std::max(1.f, width * 1.5f) : 0.f;
        ch = true;
      }
      a.markUi("mapobject.dashed");
      if (dashed) {
        ui::prop("Длина штриха");
        ch |= ui::numberField("dash", dash, {.min = 0.5, .max = schema::kMaxShapeWidth, .step = 0.5, .digits = 1, .steppers = true});
      }
    }
    if (ch) mapedit::act(a, detail::mapShapeName(*s) + ": линия", [&](Tx& tx) { rules::setShapeLine(tx, id, width, dash); }, opt);
    ui::spacer(6);
  }
  {
    ui::HStack hs(30, ui::Align::Left, 8);
    if (ui::button("Точки на карте", {.icon = "tool-map-objects", .disabled = ro})) {
      a.setTool(ToolId::MapObjects);
      a.focusSelection();
    }
    ui::tooltip("Тяните точки · двойной щелчок по контуру — новая · Delete — удалить", {Key::O, 0});
    a.markUi("mapobject.points");
    ui::flex();
    if (ui::button("Удалить", {.variant = ui::Variant::Danger, .icon = "trash"})) {
      if (mapedit::act(a, "Удалить: " + detail::mapShapeName(*s), [&](Tx& tx) { rules::removeShape(tx, id); })) a.clearSelection();
    }
    a.markUi("mapobject.delete");
  }
  needMapEdit(a);
}

HeaderReg symbolHeaderReg({"mapsymbol.header", SelType::Symbol, 0, symbolHeader});
TabReg symbolTabReg({"mapsymbol.props", "sliders", "Свойства", 10, SelType::Symbol, nullptr, symbolProps});
HeaderReg shapeHeaderReg({"mapshape.header", SelType::Shape, 0, shapeHeader});
TabReg shapeTabReg({"mapshape.props", "sliders", "Свойства", 10, SelType::Shape, nullptr, shapeProps});

}  // namespace

}  // namespace rg::app
