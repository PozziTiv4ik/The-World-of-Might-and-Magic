// Regnum — вкладка провинции «Постройки» (ТЗ 1.f.i–ii): слоты (величина, тип города, модификаторы), постройки
// с уровнями и улучшением, стройки с ходом, отменой (возврат стоимости) и мгновенным завершением, снос, выбор нового
// строительства. Постройка преобразования — «Работает / Простаивает», рецепт, ход цикла, «Ждёт ресурсы»; генерация
// эссенции и доступ к особым отрядам — что даёт постройка (ТЗ «Доработки», п.3, 5; «Ввод новых механик», п.4–5).
#include "app/editors/buildings.h"
#include "app/editors/techtree.h"
#include "app/widgets.h"

namespace rg::app {

// Хранилище реликвий достроенной постройки (ТЗ «Доработки №1», п.9) — panels/relic_store.cpp.
void relicStoreBlock(App& a, Id province, Id building);

namespace {

using platform::Key;

// Постройка преобразования в провинции: переключатель «Работает / Простаивает» (rules::setConvertIdle), рецепт
// (нехватка входов у владельца — красным), ход цикла (ProvBuilding::cycle — ходов до конца) или состояние: новый цикл
// начнётся в конце хода (шаг есть в плане FactionCalc::conversions) либо «Ждёт ресурсы».
void converter(App& a, Id pid, const ProvBuilding& pb, const Building& b, const Faction* owner, const rules::FactionCalc* fc, bool ro) {
  const World& w = a.world();
  ui::IdScope cs("convert");
  const Id bid = b.id;
  const std::string id = std::to_string(bid);
  const int turns = std::max(1, b.recipe.turns);
  ui::separator();
  {
    int mode = pb.idle ? 1 : 0;
    if (ui::segmented("mode", mode,
                      {{"play", "Работает", "Преобразует ресурсы по рецепту"}, {"moon", "Простаивает", "Ничего не делает, ресурсы не тратятся"}},
                      {.size = ui::Size::Small, .disabled = ro}) &&
        (mode == 1) != pb.idle) {
      const bool idle = mode == 1;
      a.act(idle ? "Постройка простаивает" : "Постройка работает", [&](Tx& tx) { rules::setConvertIdle(tx, pid, bid, idle); });
    }
    a.markUi("prov.convert." + id);
  }
  {
    std::vector<bld::Token> tk = bld::recipeTokens(w, b.recipe, pb.idle ? nullptr : owner);
    tk.insert(tk.begin(), bld::Token{"convert", ui::theme().info, {}, ui::Ink::Normal, "Преобразование ресурсов"});
    bld::tokens(tk);
    a.markUi("prov.recipe." + id);
  }
  // Шаг плана хода для этой постройки.
  const rules::ConvertStep* step = nullptr;
  if (fc)
    for (const rules::ConvertStep& s : fc->conversions)
      if (s.province == pid && s.building == bid) step = &s;
  if (pb.cycle > 0) {
    const int left = std::min(pb.cycle, turns);
    const std::string text = pb.idle ? "приостановлен · ещё " + nTurns(left) : left == 1 ? std::string("завершится в конце хода") : "ещё " + nTurns(left);
    ui::progress(double(turns - left) / double(turns), {.tone = pb.idle ? ui::Tone::Neutral : ui::Tone::Info, .height = 5, .text = text});
    a.markUi("prov.cycle." + id);
  } else if (!pb.idle) {
    if (!bld::recipeValid(w, b.recipe)) {
      ui::label("Рецепт не задан", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
    } else if (!owner) {
      ui::label("Без владельца-государства не работает", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning"});
    } else if (step && step->start) {
      ui::label(turns == 1 ? std::string("Преобразование — в конце хода") : "Новый цикл — в конце хода, " + nTurns(turns),
                {.font = ui::Font::Small, .ink = ui::Ink::Info, .icon = "repeat"});
      a.markUi("prov.cycleStart." + id);
    } else {
      ui::label("Ждёт ресурсы", {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "hourglass",
                                 .tooltip = "Цикл начнётся, когда у государства хватит ресурсов на входе"});
      a.markUi("prov.waiting." + id);
    }
  }
}

// Что ещё даёт достроенная постройка: эссенции за ход (генерация), особые отряды (доступ).
void extras(App& a, const Building& b, int level, const Faction* owner) {
  const World& w = a.world();
  if (b.essenceGen && owner && level >= 1 && level <= int(b.levels.size())) {
    std::vector<bld::Token> tk = bld::essenceTokens(w, b.levels[size_t(level - 1)].essence);
    if (!tk.empty()) {
      tk.insert(tk.begin(), bld::Token{"repeat", ui::theme().textMuted, {}, ui::Ink::Muted, "Даёт за ход"});
      bld::tokens(tk, 6);
      a.markUi("prov.essence." + std::to_string(b.id));
    }
  }
  if (b.specialAccess && !b.specials.empty()) {
    ui::IdScope ss("specials");
    tree::ChipFlow flow;
    for (Id sid : b.specials) {
      const SpecialUnit* su = w.special(sid);
      if (!su) continue;
      ui::IdScope s2{i64(sid)};
      std::string tip = "Особый отряд: " + std::string(schema::unitType(su->type).name) + "\nОткрыть в справочнике";
      if (tree::chip(su->name.empty() ? std::string("Без названия") : su->name, {.icon = "special-unit", .tone = ui::Tone::Accent, .clickable = true, .tooltip = tip}) ==
          ui::ChipAction::Click)
        a.openEditor("catalogs", 9);
    }
    a.markUi("prov.specials." + std::to_string(b.id));
  }
}

const Faction* ownerState(const World& w, const Province& p) {
  const Faction* f = w.faction(p.owner);
  return f && f->isState() ? f : nullptr;
}

// ТЗ «Виды государств», п.5 и 9: в пустоши нежити строит только государство нежити, в осквернённой — демонов
// (то же в причинах rules::buildOptions).
std::string landBlock(const World& w, const Province& p, const Faction* owner) {
  if (!owner) return {};
  if (rules::hasModKey(w, p.modifiers, schema::mod::UndeadWaste) && owner->stateKind != StateKind::Undead) return "В пустоши нежити строит только государство нежити";
  if (rules::hasModKey(w, p.modifiers, schema::mod::Desecrated) && owner->stateKind != StateKind::Demonic)
    return "В осквернённой провинции строит только государство демонов";
  return {};
}

void askCancel(App& a, Id pid, Id bid, const std::string& name, int level) {
  a.confirm("Отменить строительство «" + name + "»?",
            level > 1 ? "Улучшение прекратится, останется уровень " + tree::roman(level - 1) + ". Стоимость вернётся полностью."
                      : "Постройка освободит слот. Стоимость вернётся полностью.",
            "Отменить строительство", true,
            [pid, bid](App& x) { x.act("Отменить строительство", [&](Tx& tx) { rules::cancelBuilding(tx, pid, bid); }); });
}

void askDemolish(App& a, Id pid, Id bid, const std::string& name) {
  a.confirm("Снести «" + name + "»?", "Постройка и её бонусы исчезнут, слот освободится. Стоимость не возвращается. Действие можно отменить Ctrl+Z.", "Снести", true,
            [pid, bid](App& x) { x.act("Снести постройку", [&](Tx& tx) { rules::demolish(tx, pid, bid); }); });
}

// Ячейки слотов: постройки, стройки, свободные места, переполнение.
void slotGrid(App& a, Id pid, const std::vector<ProvBuilding>& list, int slots, bool canBuild) {
  const World& w = a.world();
  const ui::Theme& th = ui::theme();
  const float cell = 42, gap = 6;
  RectF av = ui::avail();
  int cols = std::max(1, int((av.w + gap) / (cell + gap)));
  int n = std::max(slots, int(list.size()));
  // Постройки — сначала готовые, затем стройки.
  std::vector<const ProvBuilding*> order;
  for (const ProvBuilding& pb : list)
    if (!pb.constructing) order.push_back(&pb);
  for (const ProvBuilding& pb : list)
    if (pb.constructing) order.push_back(&pb);
  int rows = (n + cols - 1) / cols;
  RectF area = ui::next(rows * cell + std::max(0, rows - 1) * gap);
  for (int i = 0; i < n; i++) {
    ui::IdScope s(i);
    RectF r{area.x + float(i % cols) * (cell + gap), area.y + float(i / cols) * (cell + gap), cell, cell};
    bool over = i >= slots;
    std::string tip;
    const char* iconName = "plus";
    if (i < int(order.size())) {
      const ProvBuilding& pb = *order[size_t(i)];
      const Building* b = w.building(pb.building);
      Color cc = b ? bld::catColor(b->cat) : th.textMuted;
      ui::draw::rect(r, cc.alpha(pb.constructing ? 0.08f : 0.17f), 10);
      ui::draw::rectStroke(r, over ? th.danger : cc.alpha(pb.constructing ? 0.3f : 0.45f), 10, over ? 1.5f : 1);
      ui::draw::icon(b ? bld::iconOf(*b) : "building", r.inset(11), pb.constructing ? cc.alpha(0.55f) : cc);
      std::string nm = b ? b->name : std::string("Постройка");
      if (pb.constructing) {
        int total = b ? bld::levelTurns(*b, pb.level) : 1;
        float fr = clamp(float(total - pb.left) / float(std::max(1, total)), 0.f, 1.f);
        if (fr <= 0) fr = 0.04f;   // только начато — точка начала дуги
        // Кольцо хода строительства
        ui::draw::ring(r.right() - 9, r.y + 9, 6, 2, th.surface3);
        if (fr > 0) {
          gfx::Path p;
          int seg = std::max(4, int(24 * fr));
          for (int k = 0; k <= seg; k++) {
            float ang = float(-kPi / 2 + 2 * kPi * double(fr) * double(k) / double(seg));
            float x = r.right() - 9 + 6 * std::cos(ang), y = r.y + 9 + 6 * std::sin(ang);
            if (k == 0) p.moveTo(x, y);
            else p.lineTo(x, y);
          }
          ui::draw::pathStroke(p, th.info, 2);
        }
        tip = nm + (pb.level > 1 ? " — улучшение до " + tree::roman(pb.level) : std::string(" — строится")) + ", ещё " + nTurns(pb.left);
      } else {
        std::string rn = tree::roman(pb.level);
        float lw = ui::measure(rn, ui::Font::Caption) + 7;
        RectF lb{r.right() - lw - 2, r.bottom() - 15, lw, 13};
        ui::draw::rect(lb, th.surface1, 6);
        ui::draw::text(rn, lb, ui::Font::Caption, th.textDim, ui::Align::Center);
        tip = nm + ", уровень " + tree::roman(pb.level);
      }
      if (over) tip += " · сверх доступных слотов";
      iconName = b ? bld::iconOf(*b) : "building";
    } else {
      // Свободный слот
      ui::draw::rect(r, th.surface3.alpha(0.5f), 10);
      ui::draw::rectStroke(r, th.borderStrong.alpha(0.8f), 10, 1);
      tip = canBuild ? "Свободный слот — построить" : "Свободный слот";
    }
    ui::at(r);
    ui::iconColored(iconName, Color(0, 0, 0, 0), cell, tip);
    const ui::Item& it = ui::lastItem();
    if (i >= int(order.size()) && !over) {
      if (it.hovered && canBuild) {
        ui::draw::rectStroke(r, th.accent, 10, 1.5f);
        ui::setCursor(platform::Cursor::Hand);
      }
      ui::draw::icon("plus", r.inset(13), it.hovered && canBuild ? th.accent : th.textMuted);
      if (it.clicked && canBuild) a.openDialog(buildPickerDialog(pid));
    }
    a.markUi("prov.slot." + std::to_string(i), r);
  }
}

void drawBuildings(App& a, Id pid) {
  const World& w = a.world();
  const Province* p0 = w.province(pid);
  if (!p0) return;
  const bool ro = a.readOnly();
  const std::vector<ProvBuilding> list = p0->buildings;   // копия: действия посреди кадра меняют мир
  const ProvSize size = p0->size;
  const CityType city = p0->city;
  const Faction* owner = ownerState(w, *p0);
  const Id ownerId = owner ? owner->id : 0;
  auto calc = rules::calc(w);
  const rules::ProvinceCalc* pc = calc->province(pid);
  int slots = pc ? pc->slots : 0;
  int used = int(list.size());
  int built = 0, constructing = 0;
  for (const ProvBuilding& pb : list) (pb.constructing ? constructing : built)++;
  bool over = used > slots;
  const std::string block = landBlock(w, *p0, owner);
  // В провинции одновременно строится только одна постройка (ТЗ «Доработки №1», п.13).
  const ProvBuilding* busy = nullptr;
  for (const ProvBuilding& pb : list)
    if (pb.constructing && !busy) busy = &pb;
  const Building* busyB = busy ? w.building(busy->building) : nullptr;
  bool canBuild = !ro && owner && used < slots && block.empty() && !busy;

  // Показатели
  {
    ui::Row r({ui::fr(1), ui::fr(1)}, 64, 8);
    std::string tip = "Величина «" + std::string(schema::provSize(size).name) + "»: " + fmtSigned(pc ? pc->slotsSize : 0) + "\nТип города «" +
                      schema::cityType(city).name + "»: " + fmtSigned(pc ? pc->slotsCity : 0) + "\nМодификаторы: " + fmtSigned(pc ? pc->slotsMods : 0);
    ui::stat(std::to_string(used) + " / " + std::to_string(slots), "Слоты", {.icon = "slots", .tone = over ? ui::Tone::Danger : ui::Tone::Accent, .tooltip = tip});
    a.markUi("prov.slots");
    ui::stat(std::to_string(constructing), "Строится", {.icon = "hourglass", .tone = ui::Tone::Info, .tooltip = "Построено: " + std::to_string(built)});
  }
  // Слоты: откуда берутся и сетка
  {
    ui::Card card({.pad = 12});
    {
      ui::HStack hs(0, ui::Align::Left, 6);
      ui::tag(std::string(schema::provSize(size).name) + " " + fmtSigned(pc ? pc->slotsSize : 0), ui::Tone::Neutral, schema::provSize(size).icon);
      ui::tag(std::string(schema::cityType(city).name) + " " + fmtSigned(pc ? pc->slotsCity : 0), ui::Tone::Neutral, schema::cityType(city).icon);
    }
    if (pc && pc->slotsMods != 0) {
      ui::HStack hs(0, ui::Align::Left, 6);
      ui::tag("Модификаторы " + fmtSigned(pc->slotsMods), pc->slotsMods > 0 ? ui::Tone::Success : ui::Tone::Danger, "sparkles");
    }
    if (slots == 0 && list.empty()) ui::label("У провинции нет слотов для построек.", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "info"});
    else slotGrid(a, pid, list, slots, canBuild);
    if (over)
      ui::label("Занято " + std::to_string(used) + " из " + std::to_string(slots) + " слотов — новые постройки недоступны, пока не освободится место.",
                {.font = ui::Font::Small, .ink = ui::Ink::Danger, .icon = "warning", .wrap = true});
  }
  // Построить
  {
    std::string why;
    if (ro) why = "Открыт прошлый ход";
    else if (!owner) why = "У провинции нет владельца-государства";
    else if (!block.empty()) why = block;
    else if (used >= slots) why = "Нет свободных слотов";
    else if (busy) why = "Уже строится «" + (busyB ? busyB->name : std::string("постройка")) + "»: одновременно — одна постройка";
    if (ui::button("Построить", {.variant = ui::Variant::Primary, .icon = "build", .fill = true, .disabled = !canBuild,
                                 .tooltip = why.empty() ? std::string_view("Выбрать постройку из дерева государства") : std::string_view(why)}))
      a.openDialog(buildPickerDialog(pid));
    a.markUi("prov.build");
    if (!owner) ui::label("Провинция без владельца: строить некому.", {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "info"});
    if (!block.empty()) {
      ui::label(block, {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "lock", .wrap = true});
      a.markUi("prov.buildBlock");
    }
  }
  if (list.empty()) {
    ui::spacer(4);
    ui::emptyState("building", "Построек пока нет.");
    return;
  }
  std::vector<rules::BuildOption> opts = rules::buildOptions(w, pid);
  auto optionOf = [&](Id b) -> const rules::BuildOption* {
    for (const auto& o : opts)
      if (o.building == b) return &o;
    return nullptr;
  };
  // Стройки
  if (constructing > 0) {
    if (ui::Section s("Строится", "hourglass", {.badge = std::to_string(constructing)}); s) {
      for (const ProvBuilding& pb : list) {
        if (!pb.constructing) continue;
        const Building* b = w.building(pb.building);
        if (!b) continue;
        ui::IdScope sc{i64(pb.building)};
        std::string name = b->name;
        int total = bld::levelTurns(*b, pb.level);
        ui::Card card({.pad = 10, .tone = ui::Tone::Info});
        ui::Row row({ui::px(40), ui::fr(1), ui::px(30), ui::px(30)}, ui::kAuto, 8);
        bld::iconTile(*b, 40, true);
        {
          ui::Group g(0, 4);
          ui::label(name, {.font = ui::Font::Strong});
          std::string sub = pb.level > 1 ? "Улучшение " + tree::roman(pb.level - 1) + " → " + tree::roman(pb.level) + " · действует уровень " + tree::roman(pb.level - 1)
                                         : std::string("Новая постройка · уровень I");
          ui::label(sub, {.font = ui::Font::Small, .ink = ui::Ink::Muted});
          ui::progress(double(total - pb.left) / double(std::max(1, total)),
                       {.tone = ui::Tone::Info, .height = 5, .text = "ещё " + nTurns(pb.left)});
        }
        // Мгновенное завершение (ТЗ «Доработки», п.5): уровень готов сразу, уплаченное остаётся уплаченным.
        if (ui::iconButton("bolt", "Завершить сейчас", {.disabled = ro, .tone = ui::Tone::Accent})) {
          const Id bid = pb.building;
          const int lvl = pb.level;
          if (a.act("Завершить постройку сейчас", [&](Tx& tx) { rules::completeBuilding(tx, pid, bid); }))
            a.toast("Достроено: " + name + (lvl > 1 ? " " + tree::roman(lvl) : std::string()), ToastKind::Success, "bolt");
        }
        a.markUi("prov.complete." + std::to_string(pb.building));
        if (ui::iconButton("close", "Отменить строительство: стоимость вернётся", {.disabled = ro, .tone = ui::Tone::Danger}))
          askCancel(a, pid, pb.building, name, pb.level);
        a.markUi("prov.cancel." + std::to_string(pb.building));
      }
    }
  }
  // Постройки
  if (built > 0) {
    if (ui::Section s("Постройки", "building", {.badge = std::to_string(built)}); s) {
      for (const ProvBuilding& pb : list) {
        if (pb.constructing) continue;
        const Building* b = w.building(pb.building);
        if (!b) continue;
        ui::IdScope sc{i64(pb.building)};
        std::string name = b->name;
        int maxL = int(b->levels.size());
        ui::Card card({.pad = 10});
        {
          ui::Row row({ui::px(40), ui::fr(1), ui::px(30)}, ui::kAuto, 10);
          bld::iconTile(*b, 40);
          {
            ui::Group g(0, 4);
            {
              ui::HStack hs(20, ui::Align::Left, 6);
              ui::label(name, {.font = ui::Font::Strong});
              if (b->owner) ui::icon("crown", ui::Ink::Accent, 14, "Уникальная постройка государства");
              if (const char* rule = bld::cultRule(*b)) ui::iconColored("b-cult", bld::catColor(BuildingCat::Cult), 14, std::string("Культовая постройка: ") + utf8::lower(rule));
            }
            ui::label("Уровень " + tree::roman(pb.level) + " из " + tree::roman(std::max(1, maxL)) + " · " + bld::catName(b->cat),
                      {.font = ui::Font::Small, .ink = ui::Ink::Muted});
            if (pb.level >= 1 && pb.level <= maxL) {
              bld::levelEffects(w, b->levels[size_t(pb.level - 1)], true);
              bld::produceChips(b->levels[size_t(pb.level - 1)].produce);
            }
            extras(a, *b, pb.builtLevel(), owner);
            bld::roleActions(a, pid, pb, *b);
          }
          if (ui::iconButton("trash", "Снести постройку", {.disabled = ro, .tone = ui::Tone::Danger})) askDemolish(a, pid, pb.building, name);
          a.markUi("prov.demolish." + std::to_string(pb.building));
        }
        // Преобразование ресурсов (ТЗ «Доработки», п.3).
        if (b->convert) converter(a, pid, pb, *b, owner, ownerId ? calc->faction(ownerId) : nullptr, ro);
        if (b->relicStore) relicStoreBlock(a, pid, pb.building);
        // Улучшение: стоимость и срок следующего уровня
        if (pb.level < maxL) {
          const rules::BuildOption* o = optionOf(pb.building);
          // Постройки нет среди вариантов строительства провинции — объяснить почему (например, уникальная постройка
          // прежнего владельца после смены владельца провинции).
          std::string why;
          if (!o) {
            if (b->owner && b->owner != ownerId)
              why = "Уникальная постройка другого государства (" + w.factionName(b->owner) + "): улучшать её может только оно";
            else if (!owner)
              why = "Улучшать постройки можно только в провинции государства";
            else
              why = "Следующий уровень сейчас недоступен";
          }
          ui::separator();
          ui::Row row({ui::fr(1), ui::px(122)}, ui::kAuto, 8);
          {
            ui::Group g(0, 4);
            {
              ui::HStack hs(22, ui::Align::Left, 8);
              ui::label("До " + tree::roman(pb.level + 1), {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "trend-up"});
              ui::label(nTurns(o ? o->turns : bld::levelTurns(*b, pb.level + 1)), {.font = ui::Font::Small, .ink = ui::Ink::Muted, .icon = "hourglass"});
            }
            if (o) bld::costChips(o->cost, owner);
            if (o) bld::essCostChips(o->essCost, owner);
            if (o && !o->can && !o->reasons.empty())
              ui::label(o->reasons.front(), {.font = ui::Font::Small, .ink = ui::Ink::Danger, .wrap = true});
            if (!why.empty()) {
              ui::label(why, {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "lock", .wrap = true});
              a.markUi("prov.upgradeWhy." + std::to_string(pb.building));
            }
          }
          bool can = o && o->can && !ro;
          std::string tip = o && !o->reasons.empty() ? join(o->reasons, "\n") : !why.empty() ? why : std::string("Начать строительство следующего уровня");
          if (ui::button("Улучшить", {.icon = "trend-up", .size = ui::Size::Small, .fill = true, .disabled = !can, .tooltip = tip})) {
            Id bid = pb.building;
            if (a.act("Улучшить постройку", [&](Tx& tx) { rules::startBuilding(tx, pid, bid); }))
              a.toast("Улучшение начато: " + name + " → " + tree::roman(pb.level + 1), ToastKind::Success, "trend-up");
          }
          a.markUi("prov.upgrade." + std::to_string(pb.building));
        } else {
          ui::tag("Наибольший уровень", ui::Tone::Success, "star");
        }
      }
    }
  }
  if (ownerId) {
    if (ui::link("Дерево построек государства", "building")) openBuildingTree(a, ownerId);
  }
}

bool visibleBuildings(App& a, Id pid) {
  const Province* p = a.world().province(pid);
  return p && !p->sea;
}

int badgeBuildings(App& a, Id pid) {
  const Province* p = a.world().province(pid);
  if (!p) return 0;
  int n = 0;
  for (const ProvBuilding& pb : p->buildings) n += pb.constructing;
  return n;
}

TabReg regTab({"province.buildings", "building", "Постройки", 45, SelType::Province, visibleBuildings, drawBuildings, badgeBuildings});

}  // namespace
}  // namespace rg::app
