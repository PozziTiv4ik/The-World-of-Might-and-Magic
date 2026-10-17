// Сценарии панелей государств и гильдий на демонстрационном мире: создание из списков и переименование в шапке,
// редактор флага (узор, эмблема, изображение PNG/JPEG, ввод пути без системных диалогов), налог и запасы ресурсов
// (щелчок по казне округляет), государственная гильдия и закреплённое государство (союз, войско гильдии), удаление с
// очисткой ссылок, штабы гильдии и влияние, герои, модификаторы (и эффекты изученных технологий), поля «Обзора»,
// списки и сочетания клавиш, режим только для чтения. Каждое изменение проверяется и отменой. Совет —
// test_app_faction_council.cpp, отношения и дань — test_app_diplomacy_relations.cpp.
#include "app/editors/techtree.h"
#include "app/widgets.h"
#include "tests/test_app_faction_util.h"
#include "tests/test_app_military_util.h"
#include "tests/test_codec_util.h"

using namespace rg;
using namespace rg::apptest;
using namespace rg::factest;

// ---------------------------------------------------------------- создание и переименование (ТЗ 1.b.ii, 1.d.i)
TEST(app_faction_create_and_rename) {
  Harness h("faction_create");
  h.demo();
  h.waitMap();
  h.dropToasts();
  const u32 before = h->world().factions.size();
  std::vector<Color> colors;
  h->world().factions.each([&](const Faction& f) { colors.push_back(f.color); });

  h->openDrawer("states");
  h.step();
  CHECK(h.clickUi("states.add"));
  h.step();
  CHECK_EQ(h->world().factions.size(), before + 1);
  app::Selection sel = h->ui.sel;
  CHECK(sel.type == app::SelType::Faction);
  const Faction* f = h->world().faction(sel.id);
  CHECK(f && f->isState());
  // Новый цвет отличается от всех существующих, флаг — узор с эмблемой в цвете фракции.
  for (Color c : colors) CHECK(!(c == f->color));
  CHECK(!f->flag.image);
  CHECK(!f->flag.emblem.empty());
  CHECK(f->flag.colors[0] == f->color);
  CHECK(h->view() == app::View::Entity);   // новая фракция — сразу своей страницей
  // Сразу после создания шапка ждёт новое название.
  CHECK(h->uiRect("faction.rename") != nullptr);
  h.retype("Новая Империя");
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(sel.id)->name, std::string("Новая Империя"));
  CHECK(h.shot("faction_created_state"));
  h->undo();
  h.step();
  CHECK(h->world().faction(sel.id)->name != "Новая Империя");
  h->redo();
  h.step();
  CHECK_EQ(h->world().faction(sel.id)->name, std::string("Новая Империя"));

  // Переименование щелчком по названию; Esc отменяет правку.
  CHECK(h.clickUi("faction.name"));
  CHECK(h->uiRect("faction.rename") != nullptr);
  h.retype("Не применять");
  h.key(Key::Escape);
  h.step();
  CHECK_EQ(h->world().faction(sel.id)->name, std::string("Новая Империя"));

  // Гильдия из своего списка.
  h->openDrawer("guilds");
  h.step();
  CHECK(h.clickUi("guilds.add"));
  h.step();
  CHECK_EQ(h->world().factions.size(), before + 2);
  Id gid = h->ui.sel.id;
  const Faction* g = h->world().faction(gid);
  CHECK(g && g->isGuild() && !g->stateGuild);
  h.type("Гильдия пряностей");
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(gid)->name, std::string("Гильдия пряностей"));
  // Поиск в каталоге гильдий.
  CHECK(h.clickUi("page.directory"));
  h.step();
  CHECK_EQ(h->ui.directory, std::string("guilds"));
  CHECK(h.clickUi("guilds.search"));
  h.type("пряност");
  h.step();
  CHECK(h->uiRect("guilds.row." + std::to_string(gid)) != nullptr);
  CHECK(h.shot("faction_drawer_guilds_search"));
  // Щелчок по строке каталога открывает страницу гильдии.
  h->clearSelection();
  h.step();
  CHECK(h.clickUi("guilds.row." + std::to_string(gid)));
  CHECK(h->ui.sel == (app::Selection{app::SelType::Faction, gid}));
  CHECK(h->view() == app::View::Entity);
}

// ---------------------------------------------------------------- редактор флага
TEST(app_faction_flag_editor) {
  Harness h("faction_flag");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id st = findFaction(h->world(), "Королевство Альмарин");
  openTab(h, st, "faction.overview");
  const Flag orig = h->world().faction(st)->flag;
  CHECK(h.clickUi("faction.flag"));
  h.step();
  CHECK(h->hasDialog("flag"));
  CHECK(h.clickUi("flag.pattern." + std::to_string(int(FlagPattern::Saltire))));
  CHECK(h.clickUi("flag.emblem.wolf"));
  h.step();
  CHECK(h.shot("faction_flag_editor_changed"));
  CHECK(h.clickUi("flag.ok"));
  h.step();
  CHECK(!h->hasDialog("flag"));
  const Flag& f1 = h->world().faction(st)->flag;
  CHECK(f1.pattern == FlagPattern::Saltire);
  CHECK_EQ(f1.emblem, std::string("wolf"));
  h->undo();
  h.step();
  CHECK(h->world().faction(st)->flag.pattern == orig.pattern);
  CHECK_EQ(h->world().faction(st)->flag.emblem, orig.emblem);

  // «Без эмблемы» и отмена диалога не меняют мир.
  CHECK(h.clickUi("faction.flag"));
  h.step();
  CHECK(h.clickUi("flag.emblem.none"));
  CHECK(h.clickUi("flag.cancel"));
  h.step();
  CHECK_EQ(h->world().faction(st)->flag.emblem, orig.emblem);

  // Изображение PNG через системный диалог.
  std::string png = fs::join(h.root, "flag.png");
  CHECK(codec::writePngFile(png, test::makeRgba(90, 60, [](int x, int, u8* p) {
    p[0] = x < 30 ? 30 : x < 60 ? 240 : 200;
    p[1] = x < 30 ? 80 : x < 60 ? 240 : 40;
    p[2] = x < 30 ? 200 : x < 60 ? 240 : 40;
    p[3] = 255;
  })));
  hl::setDialogsSupported(true);
  hl::queueDialogResult(png);
  CHECK(h.clickUi("faction.flag"));
  h.step();
  CHECK(h.clickUi("flag.upload"));
  h.step();
  CHECK(h->uiRect("flag.reset") != nullptr);
  CHECK(h.shot("faction_flag_editor_image"));
  CHECK(h.clickUi("flag.ok"));
  h.step();
  {
    const Flag& f2 = h->world().faction(st)->flag;
    CHECK(f2.image);
    auto dec = codec::decodePng(f2.png);
    CHECK(dec.has_value());
    CHECK(dec && dec->w == 90 && dec->h == 60);
  }
  // Без системных диалогов — ввод пути; JPEG сохраняется как PNG.
  hl::setDialogsSupported(false);
  CHECK(h.clickUi("faction.flag"));
  h.step();
  CHECK(h.clickUi("flag.upload"));
  h.step();
  CHECK(h->hasDialog("prompt"));
  CHECK(h.clickUi("dialog.field"));
  h.type(fs::absolute(test::dataPath("jpeg/b420.jpg")));
  CHECK(h.clickUi("dialog.ok"));
  h.step();
  CHECK(h->hasDialog("flag"));
  // Сброс к узору и снова изображение — затем применить изображение JPEG.
  CHECK(h.clickUi("flag.reset"));
  h.step();
  CHECK(h->uiRect("flag.reset") == nullptr);
  CHECK(h.clickUi("flag.upload"));
  h.step();
  CHECK(h.clickUi("dialog.field"));
  h.type(fs::absolute(test::dataPath("jpeg/b420.jpg")));
  CHECK(h.clickUi("dialog.ok"));
  h.step();
  CHECK(h.clickUi("flag.ok"));
  h.step();
  {
    const Flag& f3 = h->world().faction(st)->flag;
    CHECK(f3.image);
    CHECK(codec::decodePng(f3.png).has_value());   // JPEG перекодирован в PNG (FORMAT.md)
  }
  // Неверный путь — предупреждение поверх диалога, флаг прежний.
  h.dropToasts();
  CHECK(h.clickUi("faction.flag"));
  h.step();
  CHECK(h.clickUi("flag.upload"));
  h.step();
  CHECK(h.clickUi("dialog.field"));
  h.type(fs::join(h.root, "нет-такого-файла.png"));
  CHECK(h.clickUi("dialog.ok"));
  h.step();
  CHECK(h->hasDialog("flag"));
  CHECK(!h->hasDialog("prompt"));
  CHECK(ui::toastCount() > 0 || liveToasts(h) > 0);
  CHECK(h.clickUi("flag.cancel"));
  h.step();
  CHECK(h->world().faction(st)->flag.image);
}

// ---------------------------------------------------------------- налог и ресурсы (ТЗ 1.d.iv, 1.b.iv)
TEST(app_faction_tax_and_resources) {
  Harness h("faction_economy");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Королевство Альмарин");
  openTab(h, a, "faction.economy");
  const double tax0 = h->world().faction(a)->tax;
  CHECK(typeNumber(h, "economy.tax", "17"));
  CHECK_NEAR(h->world().faction(a)->tax, 17, 1e-9);
  // Налог государства не бывает отрицательным.
  CHECK(typeNumber(h, "economy.tax", "-5"));
  CHECK_NEAR(h->world().faction(a)->tax, 0, 1e-9);
  // Запас зерна правится, ниже нуля не опускается; казна может уйти в долг.
  Id grain = 0;
  for (const CatalogItem& c : h->world().catalogs->resources)
    if (c.name == "Зерно") grain = c.id;
  CHECK(grain != 0 && h->world().faction(a)->stock(grain) > 0);
  const std::string field = "economy.stock." + std::to_string(grain);
  CHECK(typeNumber(h, field, "1234"));
  CHECK_NEAR(h->world().faction(a)->stock(grain), 1234, 1e-9);
  CHECK(typeNumber(h, field, "-7"));
  CHECK_NEAR(h->world().faction(a)->stock(grain), 0, 1e-9);
  CHECK(typeNumber(h, "economy.stock.1", "-50"));
  CHECK_NEAR(h->world().faction(a)->treasury(), -50, 1e-9);
  CHECK(h.shot("faction_economy_edited"));
  for (int i = 0; i < 5; i++) h->undo();
  h.step();
  CHECK_NEAR(h->world().faction(a)->tax, tax0, 1e-9);
  // Казна в шапке ведёт на вкладку экономики.
  openTab(h, a, "faction.overview");
  CHECK(h.clickUi("faction.treasury"));
  CHECK_EQ(h->ui.tabOf[app::SelType::Faction], std::string("faction.economy"));
}

// ---------------------------------------------------------------- 1.e.i: щелчок по полю казны меняет казну
// После хода казна дробная (чистый доход дробный). Поле «Запас» золота во вкладке «Экономика» показывает один знак
// после запятой; щелчок в поле и уход фокуса (Tab) без ввода записывают округлённое значение в мир — появляется шаг
// отмены «Казна», мир помечается несохранённым, точное значение теряется.
TEST(app_faction_treasury_click_rounds) {
  Harness h("audit_tzstates_treasury");
  h.demo();
  h.waitMap();
  Id alm = findFaction(h->world(), "Королевство Альмарин");
  CHECK(alm);
  CHECK(h->endTurnNow());
  h.settle();
  h.dropToasts();
  const double t0 = h->world().faction(alm)->treasury();
  CHECK(std::fabs(t0 * 10 - std::round(t0 * 10)) > 1e-6);   // больше одного знака после запятой
  const std::string undo0 = h->store.canUndo() ? h->store.undoLabel() : std::string();
  openTab(h, alm, "faction.economy");
  CHECK(clickIn(h, "economy.stock.1"));
  h.key(Key::Tab);
  h.settle();
  const double t1 = h->world().faction(alm)->treasury();
  const std::string undo1 = h->store.canUndo() ? h->store.undoLabel() : std::string();
  CHECK_MSG(t1 == t0, "щелчок по полю казны и Tab без ввода изменили казну: " + fmtNum(t0, 6) + " → " + fmtNum(t1, 6) + " (шаг отмены «" + undo1 + "»)");
  CHECK_MSG(undo1 == undo0, "после щелчка по полю казны появился шаг отмены «" + undo1 + "»");
}

// ---------------------------------------------------------------- государственная гильдия (ТЗ 1.d.vi)
TEST(app_faction_state_guild) {
  Harness h("faction_state_guild");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Республика Корвен");
  openTab(h, a, "faction.guilds");
  const u32 n0 = h->world().factions.size();
  CHECK(clickIn(h, "guilds.create"));
  h.step();
  CHECK_EQ(h->world().factions.size(), n0 + 1);
  Id g = 0;
  h->world().factions.each([&](const Faction& f) {
    if (f.isGuild() && f.stateGuild && f.homeState == a) g = f.id;
  });
  CHECK(g != 0);
  CHECK(h->uiRect("guilds.row." + std::to_string(g)) != nullptr);
  CHECK(h.shot("faction_state_guild_created"));
  // Государство расположения закреплено: выбор недоступен, правило отказывает.
  CHECK(h.clickUi("guilds.row." + std::to_string(g)));
  h.step();
  CHECK(h->ui.sel == (app::Selection{app::SelType::Faction, g}));
  openTab(h, g, "faction.overview");
  CHECK(clickIn(h, "overview.home"));
  h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(g)->homeState, a);
  CHECK(!h->act("Государство гильдии", [&](Tx& tx) { rules::setHomeState(tx, g, findFaction(tx.w(), "Княжество Мирель")); }));
  CHECK_EQ(h->world().faction(g)->homeState, a);
  // Обычная гильдия меняет государство через выбор.
  Id amber = findFaction(h->world(), "Янтарная лига");
  Id mirel = findFaction(h->world(), "Княжество Мирель");
  openTab(h, amber, "faction.overview");
  CHECK(clickIn(h, "overview.home"));
  // Список государств по алфавиту: «Вольные города Ольсты» (текущее) → «Империя…» → «Княжество Мирель».
  h.key(Key::Down);
  h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(amber)->homeState, mirel);
  // Меню шапки государства тоже учреждает государственную гильдию.
  openTab(h, a, "faction.overview");
  CHECK(h.clickUi("faction.more"));
  h.step();
  CHECK(h.clickUi("faction.menu.stateguild"));
  h.step();
  CHECK_EQ(h->world().factions.size(), n0 + 2);
}

// ТЗ 1.d.vi: государственная гильдия создаётся из вкладки государства и закреплена за ним, но по отношению к своему
// государству она «незнакомы»: войско государственной гильдии, встретив войско своего государства, получает
// предложение объявить ему войну.
TEST(app_faction_state_guild_home_relation) {
  Harness h("audit_tzmil_stateguild");
  h.demo();
  const World& w = h->world();
  Id alm = factionByName(w, "Альмарин");
  Id comp = factionByName(w, "Альмарская торговая");
  CHECK(alm && comp);
  CHECK(w.faction(comp)->stateGuild && w.faction(comp)->homeState == alm);
  Relation rel = w.relation(alm, comp);
  std::printf("  [audit tzmil] отношения Альмарина и его государственной гильдии: %.0f, состояние %d (3 = незнакомы)\n", rel.v, int(rel.s));
  // Новая государственная гильдия из вкладки государства — то же.
  Id ng = 0;
  CHECK(h->act("Гильдия", [&](Tx& tx) { ng = rules::createStateGuild(tx, alm, ""); }));
  Relation rel2 = h->world().relation(alm, ng);
  CHECK_MSG(rel2.s == RelStatus::Alliance, "новая государственная гильдия «незнакома» своему государству");
  CHECK_MSG(rel.s == RelStatus::Alliance, "государственная гильдия демонстрационного мира не в союзе со своим государством");
  // Войско гильдии рядом с войсками Альмарина: собственное войско государства — союзник (не «объявить войну»);
  // у союзного войска Альмарина и Хельдвига войну предлагают разве что незнакомому Хельдвигу, но не своему государству.
  Id aa = alliedArmy(h->world(), ArmyKind::Army);
  CHECK(aa != 0);
  Vec2 apos = h->world().army(aa)->pos;
  Id ga = 0, sa = 0;
  CHECK(h->act("Войска гильдии и государства", [&](Tx& tx) {
    Id row = rules::addArmyRow(tx, comp, UnitType::Ranged, "Охрана караванов", 300, 1);
    auto spot = rules::findFreeSpot(tx.w(), ArmyKind::Army, apos + Vec2(200, 0));
    if (!spot) fail("нет места");
    ga = rules::createArmy(tx, ArmyKind::Army, comp, *spot);
    rules::setUnits(tx, ga, comp, row, 200);
    Id srow = rules::addArmyRow(tx, alm, UnitType::LightInf, "Ополчение", 300, 0.05);
    auto spot2 = rules::findFreeSpot(tx.w(), ArmyKind::Army, apos + Vec2(-200, 120));
    if (!spot2) fail("нет места");
    sa = rules::createArmy(tx, ArmyKind::Army, alm, *spot2);
    rules::setUnits(tx, sa, alm, srow, 200);
  }));
  rules::Encounter es = rules::encounter(h->world(), ga, h->world().army(sa)->pos);
  CHECK_MSG(es.type == rules::EncounterType::Alliance, "войско государственной гильдии не союзно войску своего государства");
  rules::Encounter e = rules::encounter(h->world(), ga, apos);
  std::printf("  [audit tzmil] встреча войска государственной гильдии с союзным войском её государства: тип %d (4 = объявить войну)\n", int(e.type));
  CHECK_MSG(!(e.type == rules::EncounterType::DeclareWar && (e.them == alm || e.us == alm)),
            "войску государственной гильдии предлагают объявить войну своему государству");
  if (e.type == rules::EncounterType::DeclareWar) {
    app::mil::openEncounter(h.a(), ga, aa, e, h->world().army(ga)->pos, [](app::App&, bool) {});
    h.settle();
    h.dropToasts();
    h.settle();
    CHECK(h.shot("audit_tzmil_stateguild_declare_war"));
  }
}

// ---------------------------------------------------------------- удаление с очисткой ссылок
TEST(app_faction_delete_cleanup) {
  Harness h("faction_delete");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Королевство Альмарин");
  Id sg = findFaction(h->world(), "Альмарская торговая компания");
  std::vector<Id> owned;
  h->world().provinces.each([&](const Province& p) {
    if (p.owner == a) owned.push_back(p.id);
  });
  CHECK(!owned.empty());
  CHECK(h->world().faction(sg)->homeState == a);
  openTab(h, a, "faction.overview");
  CHECK(h.clickUi("faction.more"));
  h.step();
  CHECK(h.clickUi("faction.menu.delete"));
  h.step();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.shot("faction_delete_confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.step();
  CHECK(h->world().faction(a) == nullptr);
  CHECK(!h->ui.sel);
  for (Id p : owned) CHECK_EQ(h->world().province(p)->owner, Id(0));
  CHECK_EQ(h->world().faction(sg)->homeState, Id(0));
  CHECK(!h->world().faction(sg)->stateGuild);
  for (auto& [k, r] : *h->world().relations) CHECK(Id(k >> 32) != a && Id(k & 0xFFFFFFFFu) != a);
  h->undo();
  h.step();
  CHECK(h->world().faction(a) != nullptr);
  for (Id p : owned) CHECK_EQ(h->world().province(p)->owner, a);
  CHECK(h->world().faction(sg)->stateGuild);
  // Удаление из контекстного меню списка; «Отмена» в подтверждении ничего не меняет.
  h->openDrawer("guilds");
  h.step();
  CHECK(h.clickUi("guilds.row." + std::to_string(sg), platform::MouseRight));
  h.step();
  CHECK(h.clickUi("guilds.ctx.delete"));
  h.step();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.cancel"));
  h.step();
  CHECK(h->world().faction(sg) != nullptr);
}

// ---------------------------------------------------------------- штабы гильдии (ТЗ 1.d.ii)
TEST(app_faction_guild_hqs) {
  Harness h("faction_hqs");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id g = findFaction(h->world(), "Гильдия магов Аркана");
  CHECK(g != 0);
  // Провинция без штаба этой гильдии и со свободным местом, с уникальным названием.
  std::map<std::string, int> names;
  h->world().provinces.each([&](const Province& p) { names[p.name]++; });
  Id target = 0;
  std::string tname;
  h->world().provinces.each([&](const Province& p) {
    if (target || p.sea || p.name.empty() || names[p.name] > 1) return;
    if (int(p.hqs.size()) >= 3 || std::find(p.hqs.begin(), p.hqs.end(), g) != p.hqs.end()) return;
    target = p.id;
    tname = p.name;
  });
  CHECK(target != 0);
  openTab(h, g, "faction.hqs");
  size_t hq0 = rules::calc(h->world())->faction(g)->provinces.size();
  CHECK(h.clickUi("hq.pick"));
  h.type(tname);
  h.key(Key::Enter);
  h.step();
  CHECK(h.clickUi("hq.add"));
  h.step();
  const Province* p = h->world().province(target);
  CHECK(std::find(p->hqs.begin(), p->hqs.end(), g) != p->hqs.end());
  CHECK_EQ(rules::calc(h->world())->faction(g)->provinces.size(), hq0 + 1);
  CHECK(h.shot("faction_guild_hq_added"));
  // Закрыть первый штаб таблицы (с подтверждением).
  CHECK(clickIn(h, "hq.remove.0"));
  h.step();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.step();
  CHECK_EQ(rules::calc(h->world())->faction(g)->provinces.size(), hq0);
  h->undo();
  h->undo();
  h.step();
  CHECK_EQ(rules::calc(h->world())->faction(g)->provinces.size(), hq0);
  p = h->world().province(target);
  CHECK(std::find(p->hqs.begin(), p->hqs.end(), g) == p->hqs.end());
}

// ---------------------------------------------------------------- герои и модификаторы (ТЗ 1.b.iv, 1.b.iii)
TEST(app_faction_heroes_and_modifiers) {
  Harness h("faction_heroes");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Княжество Мирель");
  auto heroes = [&] {
    int n = 0;
    h->world().characters.each([&](const Character& c) {
      if (c.faction == a && c.hero) n++;
    });
    return n;
  };
  openTab(h, a, "faction.heroes");
  int n0 = heroes();
  CHECK(n0 > 0);
  CHECK(clickIn(h, "heroes.new"));
  h.step();
  CHECK_EQ(heroes(), n0 + 1);
  CHECK(clickIn(h, "heroes.remove.0"));
  h.step();
  CHECK_EQ(heroes(), n0);
  h->undo();
  h.step();
  CHECK_EQ(heroes(), n0 + 1);
  // Модификатор фракции из выбора (вид «Везде» или глобальный, не автоматический): добавляется в список.
  openTab(h, a, "faction.modifiers");
  const size_t m0 = h->world().faction(a)->modifiers.size();
  std::string mname;
  h->world().modifiers.each([&](const Modifier& m) {
    if (mname.empty() && app::w::modifierFits(m, app::w::ModScope::Faction) &&
        std::find(h->world().faction(a)->modifiers.begin(), h->world().faction(a)->modifiers.end(), m.id) == h->world().faction(a)->modifiers.end())
      mname = m.name;
  });
  CHECK(!mname.empty());
  CHECK(clickIn(h, "mods.add"));
  h.type(mname);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(a)->modifiers.size(), m0 + 1);
  CHECK(h.shot("faction_modifiers_added"));
  h->undo();
  h.step();
  CHECK_EQ(h->world().faction(a)->modifiers.size(), m0);
  // Много модификаторов: фишки переносятся по строкам и не выходят за панель; крестик убирает модификатор.
  std::vector<Id> all = h->world().modifiers.ids();
  CHECK(all.size() >= 5);
  CHECK(h->act("Модификаторы фракции", [&](Tx& tx) { tx.faction(a).modifiers = all; }));
  h.settle();
  const RectF* in = h->uiRect("inspector");
  CHECK(in != nullptr);
  float top = 1e9f, bottom = -1e9f;
  for (size_t i = 0; i < all.size(); i++) {
    const RectF* r = h->uiRect("mods.chip." + std::to_string(i));
    CHECK(r != nullptr);
    if (!r || !in) continue;
    CHECK(r->right() <= in->right() - 8);
    top = std::min(top, r->y);
    bottom = std::max(bottom, r->bottom());
  }
  CHECK(bottom - top > 40);   // больше одной строки
  CHECK(h.shot("faction_modifiers_many"));
  const RectF* c0 = h->uiRect("mods.chip.0");
  CHECK(c0 != nullptr);
  if (c0) {
    RectF r = *c0;
    h.click(r.right() - 14, r.cy());
  }
  CHECK_EQ(h->world().faction(a)->modifiers.size(), all.size() - 1);
  CHECK(std::find(h->world().faction(a)->modifiers.begin(), h->world().faction(a)->modifiers.end(), all[0]) == h->world().faction(a)->modifiers.end());
}

// ---------------------------------------------------------------- 1.b.iii: «Действующие эффекты» вкладки «Модификаторы»
// У Альмарина изучена «Торговые гильдии» (модификатор «Торговый тракт»: +6 к торговой ценности каждой провинции).
// Вкладка «Модификаторы» государства в «Действующих эффектах» показывает только свои модификаторы: локальные эффекты
// изученных технологий не видны ни в «В каждой провинции», ни в «Источниках».
TEST(app_faction_modifiers_tab_tech_effects) {
  Harness h("audit_tzstates_mods");
  h.demo();
  h.waitMap();
  Id alm = findFaction(h->world(), "Королевство Альмарин");
  CHECK(alm);
  const World& w = h->world();
  // Локальные эффекты изученных технологий Альмарина.
  std::vector<Id> techLocal;
  w.techs.each([&](const Tech& t) {
    if (t.faction != alm || !t.studied) return;
    for (Id mid : t.modifiers)
      if (const Modifier* m = w.modifier(mid))
        for (int k = 0; k < kFxCount; k++)
          if (schema::kEffects[k].local && m->has(Fx(k)) && m->fx[size_t(k)] != 0 && std::find(techLocal.begin(), techLocal.end(), t.id) == techLocal.end()) techLocal.push_back(t.id);
  });
  CHECK(!techLocal.empty());
  openTab(h, alm, "faction.modifiers");
  shotClean(h, "audit_tzstates_modifiers_tab");
  // Регрессия: каждая изученная технология с локальными эффектами есть в «Источниках» вкладки, её эффекты — в сводке
  // «В каждой провинции».
  for (Id tid : techLocal)
    CHECK_MSG(h->uiRect("mods.source." + std::to_string(int(rules::EffectSource::Tech)) + "." + std::to_string(tid)) != nullptr,
              "вкладка «Модификаторы» не показывает локальные эффекты изученных технологий (+6 к торговой ценности в каждой провинции)");
}

// ---------------------------------------------------------------- просмотр прошлого хода
TEST(app_faction_read_only) {
  Harness h("faction_readonly");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Королевство Альмарин");
  CHECK(h->endTurnNow());
  h.step();
  CHECK(h->viewTurn(1));
  h.step();
  CHECK(h->readOnly());
  openTab(h, a, "faction.council");
  CHECK(h->uiRect("council.add") == nullptr);
  CHECK(h.clickUi("faction.name"));
  CHECK(h->uiRect("faction.rename") == nullptr);
  openTab(h, a, "faction.economy");
  const double tax = h->world().faction(a)->tax;
  CHECK(typeNumber(h, "economy.tax", "33"));
  CHECK_NEAR(h->world().faction(a)->tax, tax, 1e-9);
  CHECK(h.shot("faction_read_only"));
  h->backToCurrent();
  h.step();
  CHECK(!h->readOnly());
}

// ---------------------------------------------------------------- обзор: столица, правитель, справочники (ТЗ 1.b.iv)
TEST(app_faction_overview_fields) {
  Harness h("faction_overview");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Республика Корвен");
  CHECK(a != 0);
  openTab(h, a, "faction.overview");
  // Столица — только своя провинция; список: «Не выбрана», затем провинции по алфавиту.
  std::vector<const Province*> own;
  h->world().provinces.each([&](const Province& p) {
    if (p.owner == a && !p.sea) own.push_back(&p);
  });
  std::sort(own.begin(), own.end(), [](const Province* x, const Province* y) { return compareRu(x->name, y->name) < 0; });
  CHECK(own.size() >= 2);
  const Id cap0 = h->world().faction(a)->capital;
  const size_t target = own[0]->id == cap0 ? 1 : 0;
  const Id newCap = own[target]->id;
  CHECK(clickIn(h, "overview.capital"));
  h.key(Key::PageUp);
  for (size_t k = 0; k <= target; k++) h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(a)->capital, newCap);
  h->undo();
  h.step();
  CHECK_EQ(h->world().faction(a)->capital, cap0);

  // Титул правителя.
  CHECK(clickIn(h, "overview.title"));
  h.retype("Верховный консул");
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(a)->rulerTitle, std::string("Верховный консул"));

  // Правитель — выбор персонажа: «Не назначен», затем доступные герои этого государства по алфавиту (ТЗ «Фиксы», п.9).
  Id candidate = 0;
  std::vector<const Character*> pool;
  h->world().characters.each([&](const Character& c) {
    if (c.faction == a && rules::heroAvailable(h->world(), c.id)) pool.push_back(&c);
    if (!candidate && c.faction == a && c.id != h->world().faction(a)->ruler && !c.name.empty()) candidate = c.id;
  });
  CHECK(candidate != 0);
  std::sort(pool.begin(), pool.end(), [](const Character* x, const Character* y) { return compareRu(x->name, y->name) < 0; });
  size_t at = 0;
  while (at < pool.size() && pool[at]->id != candidate) at++;
  CHECK(at < pool.size());
  CHECK(clickIn(h, "overview.ruler"));
  h.key(Key::PageUp);
  for (size_t k = 0; k <= at; k++) h.key(Key::Down);
  h.key(Key::Enter);
  h.step();
  CHECK_EQ(h->world().faction(a)->ruler, candidate);

  // Новая форма правления: «Добавить…» → название → запись создана и назначена одним действием.
  const size_t g0 = h->world().catalogs->governments.size();
  CHECK(clickIn(h, "overview.gov"));
  h.key(Key::PageDown);
  h.key(Key::PageDown);
  h.key(Key::Enter);
  h.step();
  CHECK(h->hasDialog("prompt"));
  CHECK(answerPrompt(h, "Триумвират"));
  CHECK_EQ(h->world().catalogs->governments.size(), g0 + 1);
  const CatalogItem* gov = Catalogs::find(h->world().catalogs->governments, h->world().faction(a)->government);
  CHECK(gov && gov->name == "Триумвират");
  CHECK(h.shot("faction_overview_fields"));
  h->undo();
  h.step();
  CHECK_EQ(h->world().catalogs->governments.size(), g0);
  const CatalogItem* gov2 = Catalogs::find(h->world().catalogs->governments, h->world().faction(a)->government);
  CHECK(!gov2 || gov2->name != "Триумвират");
}

// ---------------------------------------------------------------- каталоги: сочетания, строки, переход на страницу
TEST(app_faction_drawers_keys_sort) {
  Harness h("faction_drawers");
  h.demo();
  h.waitMap();
  h.dropToasts();
  h.key(Key::D2, ctrl());
  CHECK_EQ(h->ui.directory, std::string("states"));
  h.key(Key::D3, ctrl());
  CHECK_EQ(h->ui.directory, std::string("guilds"));
  h.key(Key::D2, ctrl());
  CHECK_EQ(h->ui.directory, std::string("states"));
  // В каталоге — строки всех государств (по названию).
  h->world().factions.each([&](const Faction& f) {
    if (f.isState()) CHECK(h->uiRect("states.row." + std::to_string(f.id)) != nullptr);
  });
  Id richest = 0;
  double best = -1e18;
  h->world().factions.each([&](const Faction& f) {
    if (f.isState() && f.treasury() > best) {
      best = f.treasury();
      richest = f.id;
    }
  });
  // Щелчок по строке — страница государства; Ctrl+2 повторно — к карте.
  const RectF* row = h->uiRect("states.row." + std::to_string(richest));
  CHECK(row != nullptr);
  if (row) {
    RectF rr = *row;
    h.click(rr.cx(), rr.cy());
  }
  CHECK(h->ui.sel == (app::Selection{app::SelType::Faction, richest}));
  CHECK(h->view() == app::View::Entity);
  h.settle();
  CHECK(h.shot("faction_drawer_sorted"));
}

// ---------------------------------------------------------------- влияние штаба (ТЗ 1.d.iv)
TEST(app_faction_hq_influence) {
  Harness h("faction_hq_influence");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id g = findFaction(h->world(), "Альмарская торговая компания");
  openTab(h, g, "faction.hqs");
  auto calc0 = rules::calc(h->world());
  const rules::FactionCalc* fc = calc0->faction(g);
  CHECK(fc && !fc->provinces.empty());
  const Id pid = fc->provinces[0];
  const double inc0 = fc->incGuilds;
  auto influenceOf = [&](Id guild) {
    double v = 0;
    for (const Influence& in : h->world().province(pid)->influence)
      if (in.guild == guild) v += in.pct;
    return v;
  };
  double total0 = 0;
  for (const Influence& in : h->world().province(pid)->influence) total0 += in.pct;
  const double pct0 = influenceOf(g);
  const double want = std::min(100.0 - (total0 - pct0), pct0 + 10);
  CHECK(want > pct0);
  CHECK(typeNumber(h, "hq.influence." + std::to_string(pid), fmtNum(want)));
  CHECK_NEAR(influenceOf(g), want, 1e-9);
  CHECK(rules::calc(h->world())->faction(g)->incGuilds > inc0);
  // Сумма влияния в провинции не превышает 100 %.
  h.dropToasts();
  CHECK(typeNumber(h, "hq.influence." + std::to_string(pid), "100"));
  double total = 0;
  for (const Influence& in : h->world().province(pid)->influence) total += in.pct;
  CHECK(total <= 100 + 1e-9);
  h->undo();
  h.step();
  CHECK_NEAR(influenceOf(g), pct0, 1e-9);
}

// ---------------------------------------------------------------- герои: назначить существующего персонажа
TEST(app_faction_heroes_assign) {
  Harness h("faction_heroes_assign");
  h.demo();
  h.waitMap();
  h.dropToasts();
  Id a = findFaction(h->world(), "Королевство Альмарин");
  Id cand = 0;
  std::string cname;
  h->world().characters.each([&](const Character& c) {
    if (!cand && c.faction == a && !c.hero && !c.name.empty()) {
      cand = c.id;
      cname = c.name;
    }
  });
  CHECK(cand != 0);
  openTab(h, a, "faction.heroes");
  CHECK(clickIn(h, "heroes.assign"));
  // Порядок списка: кандидаты фракции первыми, по алфавиту; длинный список — с поиском.
  std::vector<const Character*> c;
  h->world().characters.each([&](const Character& x) {
    if (!x.hero && (x.faction == a || x.faction == 0)) c.push_back(&x);
  });
  std::sort(c.begin(), c.end(), [a](const Character* x, const Character* y) {
    if ((x->faction == a) != (y->faction == a)) return x->faction == a;
    return compareRu(x->name, y->name) < 0;
  });
  size_t k = 0;
  while (k < c.size() && c[k]->id != cand) k++;
  CHECK(k < c.size());
  if (c.size() > 8) {
    h.type(cname);
  } else {
    for (size_t i = 0; i < k; i++) h.key(Key::Down);
  }
  h.key(Key::Enter);
  h.step();
  CHECK(h->world().character(cand)->hero);
  CHECK_EQ(h->world().character(cand)->faction, a);
  h->undo();
  h.step();
  CHECK(!h->world().character(cand)->hero);
}
