// Regnum — наёмники (ТЗ «Доработки №3», п.12–13): во вкладке государства «Войска» — кнопка «Создать войско наемников»
// (доступна, пока в государстве достроена хотя бы одна «Гильдия Наемников»; иначе причина в подсказке) и лимит
// «Наёмники X из Y» (гильдии × константа «Наемников за гильдию наемников»). Окно создания: тип (лёгкая, средняя,
// тяжёлая пехота и кавалерия), название, цена найма за юнит и содержание за юнит в ход — золотом (тыс.). Найм и
// содержание — только золото (rules::recruitCost), раса — «Наемники»; цена найма строки правится в её карточке
// (rules::setMercHire).
#include "app/panels/military.h"

namespace rg::app::mil {

namespace {

constexpr UnitType kMercTypes[] = {UnitType::LightInf, UnitType::MediumInf, UnitType::HeavyInf,
                                   UnitType::LightCav, UnitType::MediumCav, UnitType::HeavyCav};

// Почему нельзя создать войско наёмников (пусто — можно).
std::string mercWhy(App& a, Id faction) {
  const World& w = a.world();
  const Faction* f = w.faction(faction);
  if (!f || !f->isState()) return "Наёмников нанимают только государства";
  if (a.readOnly()) return "Открыт прошлый ход";
  if (rules::mercLimit(w, faction) <= 0) return "Нужна достроенная «" + rules::buildingKeyName(w, faction, schema::bld::MercGuild) + "»";
  return {};
}

struct MercDialog : Dialog {
  Id faction = 0;
  int type = 0;                 // номер в kMercTypes
  std::string name;
  double hire = 0, upkeep = schema::kNewRowUpkeep;
  bool init = false;
  const char* id() const override { return "mil.merc"; }
  Style style(App&) override { return {"Войско наемников", "mercenary", ui::Tone::Accent, 460}; }
  bool draw(App& a) override {
    const World& w = a.world();
    if (!w.faction(faction)) return false;
    if (!init) {
      init = true;
      hire = std::max(0.0, rules::constantOf(w, schema::cst::MercHire).num);
    }
    // Тип — значками (подпись в подсказке).
    std::vector<ui::Segment> segs;
    for (UnitType t : kMercTypes) segs.push_back(ui::Segment{schema::unitType(t).icon, {}, schema::unitType(t).name});
    ui::segmented("type", type, std::span<const ui::Segment>(segs));
    a.markUi("mil.merc.type");
    const UnitType ut = kMercTypes[size_t(clamp(type, 0, int(std::size(kMercTypes)) - 1))];
    const std::string def = std::string("Наёмники: ") + schema::unitType(ut).name;
    ui::textField("name", name, {.placeholder = def, .icon = "edit", .live = true, .maxLength = 60, .tooltip = "Название строки войск"});
    a.markUi("mil.merc.name");
    {
      ui::prop("Найм за юнит", "coins");
      ui::numberField("hire", hire, {.min = 0, .max = 1e9, .step = 0.01, .digits = 3, .unit = "тыс.", .tooltip = "Золото за найм одного наёмника"});
      a.markUi("mil.merc.hire");
    }
    {
      ui::prop("Содержание за юнит", "expense");
      ui::numberField("upkeep", upkeep, {.min = 0, .max = 1e9, .step = 0.001, .digits = 3, .unit = "тыс.", .tooltip = "Золото за одного наёмника в ход"});
      a.markUi("mil.merc.upkeep");
    }
    const i64 limit = rules::mercLimit(w, faction), have = rules::mercCount(w, faction);
    ui::label("Наёмники " + fmtCount(have) + " из " + fmtCount(limit), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "mercenary",
                                                                      .tooltip = "Лимит: гильдии наёмников × константа «Наемников за гильдию наемников»"});
    ui::ModalFooter f;
    if (ui::button("Отмена")) return false;
    const std::string why = mercWhy(a, faction);
    if (ui::button("Создать", {.variant = ui::Variant::Primary, .icon = "plus", .disabled = !why.empty(), .isDefault = true, .tooltip = why})) {
      const Id fid = faction;
      const std::string nm = trim(name);
      const double h = std::max(0.0, hire), up = std::max(0.0, upkeep);
      if (a.act("Войско наемников", [&](Tx& tx) {
            const Id row = rules::addMercRow(tx, fid, ut, nm, h);
            for (ArmyRow& r : tx.faction(fid).army)
              if (r.id == row) r.upkeep = up;
          })) {
        a.toast("Войско наемников добавлено в таблицу войск", ToastKind::Success, "mercenary");
        return false;
      }
    }
    a.markUi("mil.merc.ok");
    return true;
  }
};

std::unique_ptr<Dialog> makeMerc(App&, Id faction) {
  auto d = std::make_unique<MercDialog>();
  d->faction = faction;
  return d;
}

DialogReg regMerc({"mil.merc", makeMerc});

}  // namespace

void mercBar(App& a, Id faction) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(faction);
  if (!f || !f->isState()) return;
  const i64 limit = rules::mercLimit(w, faction), have = rules::mercCount(w, faction);
  const std::string why = mercWhy(a, faction);
  ui::IdScope scope("merc");
  ui::HStack hs(30, ui::Align::Left, 8);
  if (ui::button("Создать войско наемников", {.icon = "mercenary", .size = ui::Size::Small, .disabled = !why.empty(),
                                               .tooltip = why.empty() ? std::string("Наёмники: найм и содержание — только золотом") : why}))
    a.openDialog(makeMerc(a, faction));
  a.markUi("mil.merc.create");
  if (limit > 0 || have > 0) {
    ui::flex();
    ui::tag("Наёмники " + fmtCount(have) + " из " + fmtCount(limit), have >= limit ? ui::Tone::Warning : ui::Tone::Neutral, "mercenary");
    ui::tooltip("Лимит численности наёмников: каждая достроенная «Гильдия Наемников» — " +
                fmtCount(i64(std::max(0.0, rules::constantOf(w, schema::cst::MercPerGuild).num))));
    a.markUi("mil.merc.limit");
  }
}

void mercHireField(App& a, Id faction, Id row) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(faction);
  const ArmyRow* r = f ? f->armyRow(row) : nullptr;
  if (!r || !r->merc) return;
  ui::IdScope scope("merchire");
  ui::prop("Найм за юнит", "mercenary");
  double hire = r->hire;
  if (ui::numberField("hire", hire, {.min = 0, .max = 1e9, .step = 0.01, .digits = 3, .unit = "тыс.", .disabled = a.readOnly(),
                                     .tooltip = "Золото за найм одного наёмника (только золото, без населения и ресурсов)"})) {
    const double h = std::max(0.0, hire);
    a.act("Цена найма наёмников", [&](Tx& tx) { rules::setMercHire(tx, faction, row, h); }, {.coalesce = "merchire:" + std::to_string(row)});
  }
  a.markUi("mil.detail.hire");
}

}  // namespace rg::app::mil
