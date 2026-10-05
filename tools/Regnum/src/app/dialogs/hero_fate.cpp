// Regnum — окно «Судьба героев» (ТЗ «Механика героев», п.1): герои уничтоженного войска (флота) или восставшего
// целиком войска — сбежал (вернулся в своё государство), убит (модификатор «Мертв», место захоронения — провинция
// битвы), взят в плен (модификатор «Взят в плен», пленившее государство). Решение — одной записью отмены.
#include "app/app_internal.h"
#include "app/flows.h"
#include "app/panels/military.h"

namespace rg::app {

namespace {

using rules::Fate;

struct HeroFateDialog final : Dialog {
  std::vector<Id> heroes;
  Id captor = 0, burial = 0;
  std::function<void(App&)> done;
  std::map<Id, Fate> fate;   // по умолчанию — сбежал
  bool decided = false;

  const char* id() const override { return "hero.fate"; }
  Style style(App&) override { return {heroes.size() > 1 ? "Судьба героев" : "Судьба героя", "hero", ui::Tone::Accent, 640}; }

  void close(App& a) {
    if (decided) return;
    decided = true;
    if (done) {
      auto fn = done;
      detail::later(a, [fn](App& x) { fn(x); });
    }
  }
  // Закрыто без решения — никто не пострадал: все вернулись в свои государства.
  void dismissed(App& a) override { close(a); }

  // Пленить может государство, которому герой не принадлежит.
  bool canCapture(const World& w, Id hero) const {
    const Character* c = w.character(hero);
    const Faction* f = w.faction(captor);
    return c && f && f->isState() && c->faction != captor;
  }

  Fate fateOf(const World& w, Id h) const {
    auto it = fate.find(h);
    Fate f = it == fate.end() ? Fate::Fled : it->second;
    return f == Fate::Captured && !canCapture(w, h) ? Fate::Fled : f;
  }

  void apply(App& a) {
    const World& w = mil::frameWorld(a);
    std::vector<std::pair<Id, Fate>> list;
    for (Id h : heroes)
      if (w.character(h)) list.push_back({h, fateOf(w, h)});
    const Id cap = captor, bur = w.province(burial) ? burial : 0;
    if (!a.act(list.size() > 1 ? "Судьба героев" : "Судьба героя", [&](Tx& tx) {
          for (auto& [h, f] : list)
            if (tx.w().character(h)) rules::heroFate(tx, h, f, f == Fate::Captured ? cap : 0, bur);
        }))
      return;
    close(a);
  }

  bool draw(App& a) override {
    const World& w = mil::frameWorld(a);
    std::vector<Id> list;
    for (Id h : heroes)
      if (w.character(h)) list.push_back(h);
    if (list.empty()) {
      close(a);
      return false;
    }
    const bool anyCapture = std::any_of(list.begin(), list.end(), [&](Id h) { return canCapture(w, h); });
    const std::string buried = w.province(burial) ? "«Мертв», место захоронения — " + w.provinceName(burial) : std::string("«Мертв»");
    const std::string caught = w.faction(captor) ? "«Взят в плен» — " + w.factionName(captor) : std::string();
    // Место захоронения и пленившее государство; выбор для всех сразу.
    {
      ui::HStack hs(26, ui::Align::Left, 6);
      if (w.province(burial)) {
        ui::icon("skull", ui::Ink::Dim, 16, "Место захоронения");
        w::provinceChip(burial);
      }
      if (w.faction(captor)) {
        ui::icon("shackles", ui::Ink::Dim, 16, "Пленившее государство");
        w::factionChip(captor);
      }
      if (list.size() > 1) {
        ui::flex();
        const char* icons[3] = {"retreat", "skull", "shackles"};
        const char* tips[3] = {"Все сбежали", "Все убиты", "Все в плен"};
        for (int k = 0; k < 3; k++) {
          ui::IdScope s(k);
          if (ui::iconButton(icons[k], tips[k], {.disabled = k == 2 && !anyCapture}))
            for (Id h : list) fate[h] = k == 2 && !canCapture(w, h) ? Fate::Fled : Fate(k);
          a.markUi("fate.all." + std::to_string(k));
        }
      }
    }
    ui::spacer(2);
    const float rowH = 52;
    {
      ui::Scroll sc("heroes", std::min(float(list.size()) * (rowH + 8), 372.f));
      for (Id h : list) {
        const Character& c = *w.character(h);
        ui::IdScope s{i64(h)};
        ui::Row r({ui::px(38), ui::fr(1, 120), ui::px(300)}, rowH, 10);
        ui::avatar(c.name.empty() ? std::string("?") : c.name, {.color = w::factionColor(w, c.faction), .size = 38});
        {
          ui::Group g(0, 2);
          ui::label(c.name.empty() ? std::string("Без имени") : c.name, {.font = ui::Font::Strong});
          ui::HStack hs(18, ui::Align::Left, 6);
          mil::factionFlag(w, c.faction, 22, 15);
          ui::label(w.factionName(c.faction), {.font = ui::Font::Small, .ink = ui::Ink::Dim});
        }
        const bool cap = canCapture(w, h);
        int idx = int(fateOf(w, h));
        const ui::Segment all[3] = {{"retreat", "Сбежал", "Вернётся в своё государство"}, {"skull", "Убит", buried}, {"shackles", "В плен", caught}};
        const int n = cap ? 3 : 2;
        if (ui::segmented("fate", idx, std::span<const ui::Segment>(all, size_t(n)))) fate[h] = Fate(idx);
        const RectF sr = ui::lastItem().rect;
        const float sw = (sr.w - 4) / float(n);
        for (int k = 0; k < n; k++) a.markUi("fate." + std::to_string(h) + "." + std::to_string(k), RectF{sr.x + 2 + sw * float(k), sr.y, sw, sr.h});
      }
    }
    ui::ModalFooter f;
    {
      ui::Disabled dis(a.readOnly());
      if (ui::button("Применить", {.variant = ui::Variant::Primary, .icon = "check", .isDefault = true})) {
        apply(a);
        if (decided) return false;
      }
      a.markUi("fate.ok");
    }
    return true;
  }
};

}  // namespace

void flow::openHeroFate(App& a, std::vector<Id> heroes, Id captor, Id burial, std::function<void(App&)> done) {
  std::vector<Id> list;
  for (Id h : heroes)
    if (a.world().character(h) && std::find(list.begin(), list.end(), h) == list.end()) list.push_back(h);
  if (list.empty()) {
    if (done) detail::later(a, [done](App& x) { done(x); });
    return;
  }
  auto d = std::make_unique<HeroFateDialog>();
  d->heroes = std::move(list);
  d->captor = captor;
  d->burial = burial;
  d->done = std::move(done);
  a.openDialog(std::move(d));
}

}  // namespace rg::app
