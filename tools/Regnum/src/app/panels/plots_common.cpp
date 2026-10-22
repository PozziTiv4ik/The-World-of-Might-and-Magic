// Regnum — общие помощники заговоров: списки и права, подписи, отряды восстания, текст арестов по этапу.
#include "app/panels/plots_common.h"

namespace rg::app::plotui {

namespace {

i64 popOf(const Province& p) {
  i64 n = 0;
  for (const RacePop& r : p.races) n += std::max<i64>(0, r.pop);
  return n;
}

}  // namespace

std::vector<Id> visiblePlots(App& a, const World& w, Id state) {
  std::vector<Id> out;
  const bool admin = a.admin();
  w.plots.each([&](const Plot& p) {
    if (p.external() ? p.owner == state : admin && p.target == state) out.push_back(p.id);
  });
  std::sort(out.begin(), out.end());
  return out;
}

// Своё состояние сеанса (sessionState — одно на тип).
struct PlotSelection {
  std::map<Id, Id> of;   // государство → выбранный заговор
};

Id& selected(Id state) { return sessionState<PlotSelection>().of[state]; }

bool canManage(App& a, const Plot& p) { return p.external() ? a.canPlay(p.owner) : a.canEdit(); }

bool plotAct(App& a, const Plot& p, std::string_view label, const std::function<void(Tx&)>& fn, const TxOptions& opt) {
  return p.external() ? a.play(label, fn, opt) : a.act(label, fn, opt);
}

void plotConfirm(App& a, const Plot& p, std::string title, std::string text, std::string ok, std::string label, std::function<void(Tx&)> fn,
                 std::function<void(App&)> then) {
  if (p.external()) a.confirmPlay(std::move(title), std::move(text), std::move(ok), std::move(label), std::move(fn), std::move(then));
  else a.confirmAct(std::move(title), std::move(text), std::move(ok), std::move(label), std::move(fn), std::move(then));
}

void tagTip(std::string_view text, ui::Tone tone, const char* icon, std::string_view tip) {
  ui::tag(text, tone, icon);
  ui::hoverTip(std::string("##tag") + icon + std::string(text), ui::lastItem().rect, tip);
}

std::string heroName(const World& w, Id character) {
  const Character* c = w.character(character);
  return c ? w::orName(c->name, "Без имени") : std::string("—");
}

std::string provName(const World& w, Id province) {
  const Province* p = w.province(province);
  return p ? w::orName(p->name) : std::string("—");
}

std::string raceName(const World& w, Id race) {
  const CatalogItem* c = Catalogs::find(w.catalogs->races, race);
  return c ? w::orName(c->name) : std::string("Раса");
}

const char* squadIcon(const rules::PlotSquad& s) { return schema::unitType(s.type).icon; }

std::string vipReason(const World& w, Id character) {
  const Character* c = w.character(character);
  if (!c || !rules::hitVip(w, character)) return {};
  const Faction* f = w.faction(c->faction);
  if (f && f->ruler == character) return "Правитель";
  if (f && std::any_of(f->council.begin(), f->council.end(), [&](const CouncilSeat& s) { return s.character == character; })) return "Советник";
  return "Значимый герой фракции";
}

const char* stageName(int stage) {
  switch (stage) {
    case 1: return "провинции";
    case 2: return "приверженность";
    case 3: return "герои";
    case 4: return "значимые герои";
    case 5: return "лидер";
    default: return "";
  }
}

std::vector<std::pair<const char*, std::string>> arrestLines(const World& w, const Plot& p) {
  // Сколько жителей уйдёт в тюрьму (то же, что rules::arrestPlotters, — по раскрытым сведениям).
  i64 people = 0;
  for (const PlotProvince& pp : p.provinces) {
    const Province* pr = w.province(pp.province);
    if (!pr || pr->owner != p.target) continue;
    const i64 pop = popOf(*pr);
    people += p.stage <= 1 ? i64(std::floor(double(pop) * schema::kArrestShare))
                           : std::min(pop, i64(std::floor(double(pop) * pp.commitment * schema::kPlotRiseShare)));
  }
  std::vector<std::pair<const char*, std::string>> out;
  const std::string turns = nTurns(schema::kPlotModTurns);
  if (p.stage <= 1) out.push_back({"trend-down", "Приверженность провинций заговора −" + fmtNum(schema::kArrestCommitCut) + " %"});
  out.push_back({"shackles", "В тюрьму — около " + fmtInt(people) + " " + plural(people, "жителя", "жителей", "жителей")});
  if (p.stage <= 1) {
    out.push_back({"hourglass", "«Аресты заговорщиков» — " + turns});
  } else {
    out.push_back({"province", "Провинции выходят из заговора"});
    out.push_back({"hourglass", "«Раскрытый заговор» — " + turns});
  }
  if (p.stage == 3) out.push_back({"hero", "Под арест — герои без «Значимого героя фракции», кроме лидера"});
  else if (p.stage == 4) out.push_back({"hero", "Под арест — все герои заговора, кроме лидера"});
  else if (p.stage >= 5) out.push_back({"crown", "Под арест — все герои заговора и лидер"});
  out.push_back(p.stage >= 5 ? std::pair<const char*, std::string>{"trash", "Заговор будет уничтожен"}
                             : std::pair<const char*, std::string>{"eye-off", "Заговор снова скроется от контрразведки"});
  return out;
}

}  // namespace rg::app::plotui
