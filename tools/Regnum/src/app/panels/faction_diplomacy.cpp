// Regnum — вкладка «Дипломатия» (ТЗ 1.b.iv, 1.b.vi): отношения со всеми прочими фракциями (заполняется
// автоматически по списку фракций): флаг и название, значение −100…100 (поле и двуполярный индикатор),
// состояние «в войне / в союзе / статус-кво / незнакомы». Отношения симметричны (rules::setRelation):
// изменение здесь видно во вкладке другой стороны. Поиск по названию, сортировка (войны и союзы первыми, по названию,
// по отношениям), фильтр государств и гильдий; войны и союзы выделены, ждущее ответа предложение — песочные часы.
// «В войне» здесь — объявление войны этим государством после подтверждения (rules::declareWar, затем вассалитет —
// flow::afterWarDeclared); если объявить нельзя (срок мира после перемирия, вассал против сюзерена —
// rules::canDeclareWar), пункт выключен с причиной в подсказке. Война двух государств заблокирована и завершается
// только перемирием (ТЗ «Механика войн», п.2 — flow::openTruce). Разрыв союза — с подтверждением.
// Роли (ТЗ «Доработки №4», п.5; аудит 2026-10-23, 1.1 и 5.16): состояние отношений, объявление войны и перемирие —
// игровые действия этой фракции (App::play); союз у игрока — предложение другой стороне (rules::propose), у
// администратора — сразу; значение отношений −100…100 — правка мира (администратор).
#include "app/flows.h"
#include "app/panels/faction_common.h"

namespace rg::app {
namespace {

using namespace fac;

// Подтверждение войны (как при перетаскивании войска на чужое — dialogs/encounter.cpp): флаги сторон, отношения
// сейчас и после; «Объявить войну» — война и вопросы вассалитета.
struct WarDlg final : Dialog {
  Id us = 0, them = 0;
  const char* id() const override { return "dip.war"; }
  Style style(App&) override { return {"Объявить войну?", "war", ui::Tone::Danger, 520}; }
  bool draw(App& a) override {
    const World& w = frameWorld(a);
    if (!w.faction(us) || !w.faction(them)) return false;
    {
      ui::Row r({ui::fr(1), ui::px(64), ui::fr(1)}, ui::kAuto, 12);
      factionBig(w, us);
      {
        RectF ic = ui::next(64, 78);
        const ui::Theme& th = ui::theme();
        RectF b{ic.cx() - 22, ic.y + 4, 44, 44};
        ui::draw::rect(b, th.danger.alpha(0.16f), 22);
        ui::draw::icon("war", b.inset(10), th.danger);
      }
      factionBig(w, them);
    }
    const Relation rel = w.relation(us, them);
    {
      ui::Card c({.pad = 10});
      ui::Row r({ui::fr(1), ui::px(150)}, 26, 10);
      ui::label(std::string("Сейчас: ") + utf8::lower(schema::relStatus(rel.s).name) + ", отношения " + fmtSigned(rules::relationValue(w, us, them)),
                {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = schema::relStatus(rel.s).icon});
      ui::label("→ в войне, " + fmtSigned(schema::kWarRelation), {.font = ui::Font::Strong, .ink = ui::Ink::Danger, .align = ui::Align::Right});
    }
    std::string why;   // срок мира после перемирия, вассал против сюзерена (rules::canDeclareWar)
    const bool can = rules::canDeclareWar(w, us, them, &why);
    if (!can) ui::label(why, {.font = ui::Font::Small, .ink = ui::Ink::Warning, .icon = "warning", .wrap = true});
    ui::ModalFooter f;
    if (ui::button("Отмена")) return false;
    a.markUi("dip.war.cancel");
    const Id u = us, t = them;
    if (ui::button("Объявить войну", {.variant = ui::Variant::Danger, .icon = "war", .disabled = !a.canPlay(u) || !can, .isDefault = true,
                                      .tooltip = why})) {
      if (a.play("Объявить войну: " + w.factionName(t), [&](Tx& tx) { rules::declareWar(tx, u, t); }))
        detail::later(a, [u, t](App& x) { flow::afterWarDeclared(x, u, t); });
      return false;
    }
    a.markUi("dip.war.ok");
    return true;
  }
};

void askWar(App& a, Id us, Id them) {
  if (!w::checkPlay(a, us)) return;
  auto d = std::make_unique<WarDlg>();
  d->us = us;
  d->them = them;
  a.openDialog(std::move(d));
}

// Союз: игрок предлагает (решает другая сторона или администратор в «Решениях»), администратор ставит сразу.
void setAlliance(App& a, Id us, Id them, double value) {
  const World& w = a.world();
  if (a.admin()) {
    a.play("Состояние отношений", [&](Tx& tx) { rules::setRelation(tx, us, them, value, RelStatus::Alliance); });
    return;
  }
  Proposal p;
  p.kind = ProposalKind::Alliance;
  p.from = us;
  p.to = them;
  if (a.play("Предложить союз: " + w.factionName(them), [&](Tx& tx) { rules::propose(tx, p); }))
    a.toast("Союз предложен: «" + w.factionName(them) + "»", ToastKind::Success, "alliance");
}

// Предложение между сторонами, ждущее ответа (любой вид, в любую сторону); nullptr — нет.
const Proposal* pendingBetween(const World& w, Id x, Id y) {
  const Id id = w.proposals.firstId([&](const Proposal& p) { return (p.from == x && p.to == y) || (p.from == y && p.to == x); });
  return id ? w.proposal(id) : nullptr;
}

void relationRow(App& a, const World& w, Id id, const rules::RelationRow& r) {
  const bool ro = !a.canPlay(id), edit = a.canEdit();
  const ui::Theme& t = ui::theme();
  const Faction* o = w.faction(r.other);
  const Faction* self = w.faction(id);
  if (!o || !self) return;
  const Id other = r.other;
  const bool war = r.status == RelStatus::War, ally = r.status == RelStatus::Alliance;
  // Война двух государств: состояние заблокировано, выход — перемирие.
  const bool locked = war && self->isState() && o->isState();
  const bool wide = ui::avail().w >= 520;
  const float h = wide ? 46 : 74;
  RectF rr = ui::next(h);
  a.markUi("dip.row." + std::to_string(other), rr);
  Color bg = t.stripe;
  if (war) bg = t.danger.alpha(0.09f);
  else if (ally) bg = t.success.alpha(0.08f);
  ui::draw::rect(rr, bg, t.radiusCard);
  ui::draw::rectStroke(rr, war ? t.danger.alpha(0.35f) : ally ? t.success.alpha(0.32f) : t.hover, t.radiusCard, 1);
  if (war || ally) ui::draw::rect(RectF{rr.x, rr.y + 10, 3, rr.h - 20}, relColor(r.status), 1.5f);
  ui::Area area(rr.inset(12, 8), 0);
  ui::gap(4);
  const std::string name = displayName(*o);
  const char* kindTip = o->isState() ? "Государство — открыть" : "Гильдия — открыть";
  const Proposal* pend = pendingBetween(w, id, other);
  auto nameCell = [&] {
    ui::label(name, {.font = ui::Font::Strong, .tooltip = kindTip});
    const ui::Item it = ui::lastItem();
    if (it.hovered) ui::setCursor(platform::Cursor::Hand);
    if (it.clicked) a.select(SelType::Faction, other);
    if (pend) {   // предложение ждёт ответа — песочные часы за названием
      const float x = it.rect.x + std::min(ui::measure(name, ui::Font::Strong) + 6, std::max(0.f, it.rect.w - 16));
      const RectF ir{x, it.rect.cy() - 7, 14, 14};
      ui::draw::icon("hourglass", ir, t.warning);
      ui::hoverTip("##pending", ir, rules::proposalTitle(w, *pend));
      a.markUi("dip.pending." + std::to_string(other), ir);
    }
  };
  // Одна религия (ТЗ «Доработки №4», п.3): отношения всегда смещены на +10. Поле и шкала — действующее значение
  // (rules::relationValue); правка меняет хранимое значение на ту же разницу.
  const double bonus = r.bonus;
  const double shown = rules::relationValue(w, id, other);
  auto valueField = [&] {
    double v = shown;
    if (ui::numberField("value", v, {.min = -100, .max = 100, .step = 1, .sign = true, .disabled = !edit,
                                     .tooltip = bonus != 0 ? "Отношения −100…100 с учётом одной веры" : "Отношения −100…100"})) {
      const double stored = clamp(v - bonus, -100.0, 100.0);
      a.act("Отношения", [&](Tx& tx) { rules::setRelation(tx, id, other, stored, r.status); },
            {.coalesce = "relation:" + std::to_string(std::min(id, other)) + ":" + std::to_string(std::max(id, other))});
    }
    a.markUi("dip.value." + std::to_string(other));
  };
  auto faithMark = [&] {
    if (bonus == 0) {
      ui::next(0, 20);
      return;
    }
    const Faction* self2 = w.faction(id);
    const CatalogItem* rel = self2 ? Catalogs::find(w.catalogs->religions, self2->religion) : nullptr;
    ui::label(fmtSigned(bonus), {.font = ui::Font::Small, .ink = ui::Ink::Success, .icon = "religion",
                                 .tooltip = "Одна вера" + (rel && !rel->name.empty() ? " («" + rel->name + "»)" : std::string()) + ": " + fmtSigned(bonus) +
                                            " к отношениям\nБез неё — " + fmtSigned(r.value)});
    a.markUi("dip.faith." + std::to_string(other));
  };
  auto statusCell = [&] {
    RelStatus s = r.status;
    // «В войне» недоступно — причина в подсказке пункта (срок мира после перемирия, вассал против сюзерена).
    std::string warWhy;
    if (!locked && !ro && r.status != RelStatus::War) rules::canDeclareWar(w, id, other, &warWhy);
    if (locked) {
      ui::Group g(0, 0);
      ui::Row inner({ui::fr(1), ui::px(26)}, 26, 4);
      statusPicker("status", s, true);
      a.markUi("dip.status." + std::to_string(other));
      if (ro && !a.admin()) ui::next(26, 26);   // чужое государство у игрока — без кнопки
      else if (ui::iconButton("handshake", a.admin() ? "Заключить перемирие" : "Предложить перемирие",
                              {.variant = ui::Variant::Secondary, .size = ui::Size::Small, .disabled = ro}))
        flow::openTruce(a, id, other);
      if (!ro || a.admin()) a.markUi("dip.truce." + std::to_string(other));
      return;
    }
    if (statusPicker("status", s, ro, warWhy)) {
      const double value = r.value;
      if (s == RelStatus::War) {
        askWar(a, id, other);   // «в войне», −25; затем сюзерены и вассалы (ТЗ «Вассалитет»)
      } else if (s == RelStatus::Alliance) {
        setAlliance(a, id, other, value);
      } else if (r.status == RelStatus::Alliance) {
        const RelStatus to = s;
        a.confirmPlay("Разорвать союз?", "«" + displayName(*self) + "» и «" + name + "» больше не союзники.", "Разорвать союз", "Разорвать союз",
                      [id, other, value, to](Tx& tx) { rules::setRelation(tx, id, other, value, to); });
      } else {
        a.play("Состояние отношений", [&](Tx& tx) { rules::setRelation(tx, id, other, value, s); });
      }
    }
    a.markUi("dip.status." + std::to_string(other));
  };
  if (wide) {
    ui::Row row({ui::px(30), ui::fr(1.3f), ui::fr(1), ui::px(44), ui::px(72), ui::px(132)}, 30, 10);
    ui::flag(o->flag, 30, 20, 2.5f);
    nameCell();
    ui::meter(shown, {.label = false});
    faithMark();
    valueField();
    statusCell();
  } else {
    {
      ui::Row row({ui::px(30), ui::fr(1), ui::px(132)}, 26, 10);
      ui::flag(o->flag, 30, 20, 2.5f);
      nameCell();
      statusCell();
    }
    {
      ui::Row row({ui::px(30), ui::fr(1), ui::px(44), ui::px(76)}, 28, 10);
      RectF k = ui::next(30, 28);
      ui::draw::icon(o->isState() ? "crown" : "guild", RectF{k.cx() - 7, k.cy() - 7, 14, 14}, t.textMuted);
      ui::meter(shown, {.label = false});
      faithMark();
      valueField();
    }
  }
}

// Поиск, порядок и фильтр списка (на сеанс панели).
struct DipState {
  std::string q;
  int order = 0;    // 0 — войны и союзы первыми, 1 — по названию, 2 — по отношениям (лучшие первыми)
  int filter = 0;   // 0 — все, 1 — государства, 2 — гильдии
};

void drawDiplomacy(App& a, Id id) {
  const World& w = frameWorld(a);
  const Faction* f = w.faction(id);
  if (!f) return;
  const bool ro = !a.canEdit();   // новое государство — правка мира
  std::vector<rules::RelationRow> all = rules::relationsOf(w, id);
  int counts[4] = {};
  for (auto& r : all) counts[int(r.status)]++;
  if (all.empty()) {
    if (ui::emptyState("diplomacy", "Других фракций пока нет.", ro ? "" : "Новое государство", "plus")) createFactionUi(a, FactionKind::State);
    return;
  }
  // Сводка состояний: метки с переносом на следующую строку, если не помещаются.
  {
    struct T {
      std::string text;
      ui::Tone tone;
      const char* icon;
    };
    std::vector<T> tags;
    if (counts[0]) tags.push_back({std::to_string(counts[0]) + " " + plural(counts[0], "война", "войны", "войн"), ui::Tone::Danger, "war"});
    if (counts[1]) tags.push_back({std::to_string(counts[1]) + " " + plural(counts[1], "союз", "союза", "союзов"), ui::Tone::Success, "alliance"});
    if (counts[2]) tags.push_back({std::to_string(counts[2]) + " статус-кво", ui::Tone::Info, "status-quo"});
    if (counts[3]) tags.push_back({std::to_string(counts[3]) + " незнакомы", ui::Tone::Neutral, "unknown"});
    float W = ui::avail().w;
    size_t i = 0;
    int line = 0;
    while (i < tags.size()) {
      ui::IdScope ls(line++);
      ui::HStack hs(24, ui::Align::Left, 6);
      float x = 0;
      for (bool first = true; i < tags.size(); first = false, i++) {
        float tw = ui::measure(tags[i].text, ui::Font::Strong) + 16 + 18;
        if (!first && x + tw > W) break;
        ui::tag(tags[i].text, tags[i].tone, tags[i].icon);
        x += tw + 6;
      }
    }
  }
  DipState& st = ui::state<DipState>(ui::id("##dip"));
  {
    ui::Row r({ui::fr(1), ui::px(30)}, 30, 6);
    ui::searchField("q", st.q, "Найти фракцию");
    a.markUi("dip.search");
    static const char* kOrder[] = {"Войны и союзы первыми", "По названию", "По отношениям"};
    if (ui::iconButton("sort", std::string("Порядок: ") + utf8::lower(kOrder[st.order]))) ui::openPopup("order");
    a.markUi("dip.sort");
    if (ui::beginMenu("order")) {
      for (int k = 0; k < 3; k++) {
        ui::IdScope s(k);
        if (ui::menuItem(kOrder[k], {.checked = st.order == k})) st.order = k;
        a.markUi("dip.sort." + std::to_string(k), ui::lastItem().rect);
      }
      ui::endMenu();
    }
  }
  int states = 0, guilds = 0;
  for (auto& r : all)
    if (const Faction* o = w.faction(r.other)) (o->isState() ? states : guilds)++;
  std::string sAll = "Все · " + std::to_string(all.size()), sSt = "Государства · " + std::to_string(states), sGu = "Гильдии · " + std::to_string(guilds);
  ui::segmented("filter", st.filter, {{nullptr, sAll, "Все фракции"}, {nullptr, sSt, "Только государства"}, {nullptr, sGu, "Только гильдии"}},
                {.size = ui::Size::Small});
  a.markUi("dip.filter");
  ui::spacer(2);
  std::vector<const rules::RelationRow*> list;
  for (const rules::RelationRow& r : all) {
    const Faction* o = w.faction(r.other);
    if (!o) continue;
    if ((st.filter == 1 && !o->isState()) || (st.filter == 2 && !o->isGuild())) continue;
    if (!st.q.empty() && !utf8::matches(displayName(*o), st.q)) continue;
    list.push_back(&r);
  }
  auto rank = [](RelStatus s) { return s == RelStatus::War ? 0 : s == RelStatus::Alliance ? 1 : 2; };
  std::stable_sort(list.begin(), list.end(), [&](const rules::RelationRow* x, const rules::RelationRow* y) {
    if (st.order == 0 && rank(x->status) != rank(y->status)) return rank(x->status) < rank(y->status);
    if (st.order == 2) {
      const double vx = rules::relationValue(w, id, x->other), vy = rules::relationValue(w, id, y->other);
      if (vx != vy) return vx > vy;
    }
    return compareRu(displayName(*w.faction(x->other)), displayName(*w.faction(y->other))) < 0;
  });
  for (const rules::RelationRow* r : list) {
    ui::IdScope sc{i64(r->other)};
    relationRow(a, w, id, *r);
  }
  if (list.empty()) {
    if (!st.q.empty()) ui::emptyState("search", "Ничего не найдено.");
    else ui::emptyState("diplomacy", st.filter == 2 ? "Гильдий пока нет." : "Государств, кроме этого, пока нет.");
  }
}

// Точка на значке вкладки — фракция ведёт войну (точка не теснит соседние значки узкой полосы вкладок).
int warsBadge(App& a, Id id) {
  for (auto& r : rules::relationsOf(a.world(), id))
    if (r.status == RelStatus::War) return -1;
  return 0;
}

TabReg tab({kTabDiplomacy, "diplomacy", "Дипломатия", 40, SelType::Faction, nullptr, drawDiplomacy, warsBadge});

}  // namespace
}  // namespace rg::app
