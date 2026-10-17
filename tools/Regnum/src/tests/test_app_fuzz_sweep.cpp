// Regnum — устойчивость оболочки (ui-fuzz): систематический обход на демонстрационном мире.
// Каждая сущность × каждая вкладка инспектора (с прокруткой), каждая выдвижная панель, каждый редактор со всеми
// аргументами, зарегистрированные окна; журнал WARN/ERROR снимается после каждого кадра с точным контекстом (где возник
// повторяющийся ID виджета и т. п.). Обход идёт в двух видах окна (wide — 1440×900, small — 1100×700 с интерфейсом
// 125 %) и разбит на независимые тесты по группам: провинции, фракции, персонажи и редакторы (доли списков — при росте
// мира части растут вместе с ним), войска с маршрутами, панели, окна. Повтор ID виджета роняет тест; остальные находки
// печатаются, REGNUM_AUDIT=1 — роняют тест.
#include <cstdio>
#include <map>

#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;
using app::SelType;

namespace {

// Вид окна обхода.
struct View {
  const char* tag;
  int w, h;
  float uiScale;
  int scheme;          // цветокор (ui::schemeInfo)
  const char* label;   // для отчёта
};
const View kWide{"wide", 1440, 900, 1.f, 1, "1440x900"};
const View kSmall{"small", 1100, 700, 1.25f, 0, "1100x700 ui1.25"};

// На сколько тестов делится группа (по тесту на долю списка — в конце файла).
constexpr int kProvinceParts = 8, kFactionParts = 3, kCharacterParts = 3, kEditorParts = 2;

struct Sweep {
  Harness& h;
  LogTap tap;
  std::map<std::string, std::pair<std::string, int>> found;   // сообщение -> (первый контекст, раз)
  Sweep(Harness& hh, const std::string& name) : h(hh), tap("sweep-" + name) {}
  void check(const std::string& where) {
    for (auto& m : tap.warnings()) {
      // ID в сообщении о повторе уникален для места: оставляем как ключ вместе с контекстом.
      auto& f = found[m];
      if (f.second++ == 0) f.first = where;
    }
  }
  void settle(const std::string& where) {
    h.settle();
    check(where);
  }
};

void sweepTabs(Harness& h, Sweep& s, SelType t, Id id, const std::string& what) {
  h->select(t, id);
  s.settle(what);
  for (auto& td : app::tabs()) {
    if (td.type != t) continue;
    bool vis = !td.visible || td.visible(h.a(), id);
    if (!vis) continue;
    h->ui.tabOf[t] = td.id;
    s.settle(what + " tab=" + td.id);
    // Прокрутить вкладку вниз: нижние разделы тоже рисуются.
    if (const RectF* r = h->uiRect("inspector")) {
      for (int k = 0; k < 4; k++) {
        h.wheel(r->cx(), r->y + r->h * 0.6f, -6);
        s.check(what + " tab=" + td.id + " scroll" + std::to_string(k));
      }
    }
  }
}

void sweepAll(Harness& h, Sweep& s, SelType t, const std::vector<Id>& ids, const char* what) {
  for (Id id : ids) sweepTabs(h, s, t, id, what + (" " + std::to_string(id)));
}

// Часть k из n списка (доли по порядку: при росте мира части растут вместе с ним).
template <class T> std::vector<T> slice(const std::vector<T>& v, int k, int n) {
  const size_t a = v.size() * size_t(k) / size_t(n), b = v.size() * size_t(k + 1) / size_t(n);
  return {v.begin() + std::ptrdiff_t(a), v.begin() + std::ptrdiff_t(b)};
}

// Обход одной части: своё окно с демонстрационным миром, body — что обходить; в конце — отчёт и проверки.
void run(const View& v, const std::string& part, const std::function<void(Harness&, Sweep&)>& body) {
  const std::string name = std::string(v.tag) + "_" + part;
  Harness h("fuzz_sweep_" + name, v.w, v.h, 1, v.scheme);
  if (v.uiScale != 1.f) h->setUiScale(v.uiScale);
  h.demo();
  h.waitMap();
  Sweep s(h, name);
  s.check("demo");
  body(h, s);
  s.settle("end");
  std::printf("  [ui-fuzz sweep %s %s] записей журнала WARN/ERROR: %d\n", v.label, part.c_str(), int(s.found.size()));
  for (auto& [m, f] : s.found) std::printf("  [ui-fuzz sweep] x%d %s\n        при: %s\n", f.second, m.c_str(), f.first.c_str());
  std::fflush(stdout);
  // Регрессия: ни одного повтора ID виджета во всех вкладках, панелях, редакторах и окнах (остальные записи журнала —
  // по REGNUM_AUDIT=1).
  int dups = 0;
  for (auto& kv : s.found)
    if (kv.first.find("повторяющийся ID") != std::string::npos) dups++;
  CHECK_EQ(dups, 0);
  if (test::auditStrict()) CHECK(s.found.empty());
}

std::vector<Id> idsOf(const World& w, SelType t) {
  switch (t) {
    case SelType::Province: return w.provinces.ids();
    case SelType::Faction: return w.factions.ids();
    case SelType::Army: return w.armies.ids();
    case SelType::Route: return w.routes.ids();
    case SelType::Character: return w.characters.ids();
    default: return {};
  }
}

// Вкладки сущностей типа t: часть k из n списка ID (what — сущность в контексте находки).
void entities(const View& v, const char* group, SelType t, const char* what, int k, int n) {
  run(v, std::string(group) + "_" + std::to_string(k + 1), [&](Harness& h, Sweep& s) { sweepAll(h, s, t, slice(idsOf(h->world(), t), k, n), what); });
}

void provinces(const View& v, int k) { entities(v, "provinces", SelType::Province, "province", k, kProvinceParts); }
void factions(const View& v, int k) { entities(v, "factions", SelType::Faction, "faction", k, kFactionParts); }
void characters(const View& v, int k) { entities(v, "characters", SelType::Character, "character", k, kCharacterParts); }

// Войска и флоты, маршруты.
void armies(const View& v) {
  run(v, "armies", [](Harness& h, Sweep& s) {
    sweepAll(h, s, SelType::Army, h->world().armies.ids(), "army");
    sweepAll(h, s, SelType::Route, h->world().routes.ids(), "route");
  });
}

void drawers(const View& v) {
  run(v, "drawers", [](Harness& h, Sweep& s) {
    h->clearSelection();
    for (auto& d : app::drawers()) {
      h->openDrawer(d.id);
      s.settle(std::string("drawer ") + d.id);
      if (const RectF* r = h->uiRect("drawer"))
        for (int k = 0; k < 4; k++) {
          h.wheel(r->cx(), r->y + r->h * 0.6f, -6);
          s.check(std::string("drawer ") + d.id + " scroll");
        }
      h->openDrawer(d.id);
      s.settle("close drawer");
    }
  });
}

// Редакторы со всеми аргументами: часть k списка.
void editors(const View& v, int k) {
  run(v, "editors_" + std::to_string(k + 1), [&](Harness& h, Sweep& s) {
    std::vector<std::pair<std::string, Id>> eds;
    for (Id f : h->world().factions.ids()) {
      eds.push_back({"techtree", f});
      eds.push_back({"military", f});
      if (h->world().faction(f)->isState()) eds.push_back({"buildings", f});
    }
    eds.push_back({"buildings", 0});
    for (Id m : h->world().modifiers.ids()) eds.push_back({"modifiers", m});
    eds.push_back({"modifiers", 0});
    for (Id c = 0; c <= 7; c++) eds.push_back({"catalogs", c});
    eds.push_back({"trade", 0});
    for (auto& [e, arg] : slice(eds, k, kEditorParts)) {
      h->openEditor(e, arg);
      s.settle("editor " + e + "(" + std::to_string(arg) + ")");
      if (const RectF* r = h->uiRect("editor"))
        for (int i = 0; i < 3; i++) {
          h.wheel(r->x + r->w * 0.3f, r->y + r->h * 0.6f, -6);
          s.check("editor " + e + "(" + std::to_string(arg) + ") scroll");
        }
      h->closeEditor();
      s.settle("close editor");
    }
  });
}

// Окна (после хода — с историей и отчётом), настройки со всеми вкладками, справка, палитра.
void dialogs(const View& v) {
  run(v, "dialogs", [](Harness& h, Sweep& s) {
    Id army = h->world().armies.ids().empty() ? 0 : h->world().armies.ids()[0];
    Id fac = h->world().factions.ids()[0];
    Id land = test::firstOwnedLand(h->world());
    h->endTurnNow();
    s.settle("endTurnNow");
    std::vector<std::pair<std::string, Id>> dl = {{"turn.history", 0}, {"turn.report", 0}, {"flag", fac}, {"tribute", fac},
                                                  {"build.picker", land}, {"army.split", army}, {"chronicle.note", 0}};
    for (auto& [d, arg] : dl) {
      h->openDialog(d, arg);
      s.settle("dialog " + d);
      h->closeDialogs();
      s.settle("close dialog");
    }
    h->showSettings();
    s.settle("settings");
    for (int k = 0; k < 4; k++) {
      if (const RectF* r = h->uiRect("settings.tabs")) {
        h.click(r->x + r->w * (0.125f + 0.25f * float(k)), r->cy());
        s.check("settings tab " + std::to_string(k));
      }
    }
    h->closeDialogs();
    h->showHelp();
    s.settle("help");
    h->closeDialogs();
    h->showPalette();
    s.settle("palette");
    h.type("а");
    s.check("palette type");
    h->closeDialogs();
  });
}

}  // namespace

TEST(app_fuzz_sweep_wide_provinces_1) { provinces(kWide, 0); }
TEST(app_fuzz_sweep_wide_provinces_2) { provinces(kWide, 1); }
TEST(app_fuzz_sweep_wide_provinces_3) { provinces(kWide, 2); }
TEST(app_fuzz_sweep_wide_provinces_4) { provinces(kWide, 3); }
TEST(app_fuzz_sweep_wide_provinces_5) { provinces(kWide, 4); }
TEST(app_fuzz_sweep_wide_provinces_6) { provinces(kWide, 5); }
TEST(app_fuzz_sweep_wide_provinces_7) { provinces(kWide, 6); }
TEST(app_fuzz_sweep_wide_provinces_8) { provinces(kWide, 7); }
TEST(app_fuzz_sweep_wide_factions_1) { factions(kWide, 0); }
TEST(app_fuzz_sweep_wide_factions_2) { factions(kWide, 1); }
TEST(app_fuzz_sweep_wide_factions_3) { factions(kWide, 2); }
TEST(app_fuzz_sweep_wide_armies) { armies(kWide); }
TEST(app_fuzz_sweep_wide_characters_1) { characters(kWide, 0); }
TEST(app_fuzz_sweep_wide_characters_2) { characters(kWide, 1); }
TEST(app_fuzz_sweep_wide_characters_3) { characters(kWide, 2); }
TEST(app_fuzz_sweep_wide_drawers) { drawers(kWide); }
TEST(app_fuzz_sweep_wide_editors_1) { editors(kWide, 0); }
TEST(app_fuzz_sweep_wide_editors_2) { editors(kWide, 1); }
TEST(app_fuzz_sweep_wide_dialogs) { dialogs(kWide); }

TEST(app_fuzz_sweep_small_provinces_1) { provinces(kSmall, 0); }
TEST(app_fuzz_sweep_small_provinces_2) { provinces(kSmall, 1); }
TEST(app_fuzz_sweep_small_provinces_3) { provinces(kSmall, 2); }
TEST(app_fuzz_sweep_small_provinces_4) { provinces(kSmall, 3); }
TEST(app_fuzz_sweep_small_provinces_5) { provinces(kSmall, 4); }
TEST(app_fuzz_sweep_small_provinces_6) { provinces(kSmall, 5); }
TEST(app_fuzz_sweep_small_provinces_7) { provinces(kSmall, 6); }
TEST(app_fuzz_sweep_small_provinces_8) { provinces(kSmall, 7); }
TEST(app_fuzz_sweep_small_factions_1) { factions(kSmall, 0); }
TEST(app_fuzz_sweep_small_factions_2) { factions(kSmall, 1); }
TEST(app_fuzz_sweep_small_factions_3) { factions(kSmall, 2); }
TEST(app_fuzz_sweep_small_armies) { armies(kSmall); }
TEST(app_fuzz_sweep_small_characters_1) { characters(kSmall, 0); }
TEST(app_fuzz_sweep_small_characters_2) { characters(kSmall, 1); }
TEST(app_fuzz_sweep_small_characters_3) { characters(kSmall, 2); }
TEST(app_fuzz_sweep_small_drawers) { drawers(kSmall); }
TEST(app_fuzz_sweep_small_editors_1) { editors(kSmall, 0); }
TEST(app_fuzz_sweep_small_editors_2) { editors(kSmall, 1); }
TEST(app_fuzz_sweep_small_dialogs) { dialogs(kSmall); }
