// Классы героев и таланты (ТЗ «Доработки №4», п.6, 8), «Возвысить до Лича» (ТЗ «Доработки №1», п.11), реликвии:
// группы, изображение, связь с карточкой проекта (п.8, 14), хранилище реликвий постройки (п.9). Сценарии справочника
// «Классы героев» (новый талант щелчком по пустой ячейке, стоимость, условие-стрелка, перенос перетаскиванием,
// отказ переноса, ломающего условие, очки на ярус, модификатор героя, удаление), вкладки «Таланты» героя (изучить,
// закрытый ярус, отменить, сброс), полей «Класс» и «Уровень» с подтверждением сброса талантов, окна «Возвысить до
// Лича», справочника реликвий деревом групп и хранилища в провинции; снимки экранов.
#include "app/canon.h"
#include "tests/test_app_editors_util.h"
#include "tests/test_app_faction_util.h"

using namespace rg;
using namespace rg::apptest;

namespace ed = rg::apptest::editors;
namespace fc = rg::factest;

namespace {

Id classNamed(const World& w, const char* name) {
  for (const HeroClass& c : w.catalogs->classes)
    if (c.name == name) return c.id;
  return 0;
}

const Talent* talentOf(const World& w, Id id) {
  const HeroClass* c = rules::talentClass(w, id);
  return c ? c->talent(id) : nullptr;
}

// Герой государства (не правитель): skip — сколько пропустить.
Id stateHero(const World& w, Id st, int skip = 0) {
  Id r = 0;
  w.characters.each([&](const Character& c) {
    if (r || c.faction != st || w.faction(st)->ruler == c.id || !rules::heroAvailable(w, c.id)) return;
    if (skip-- > 0) return;
    r = c.id;
  });
  return r;
}

Id stateWithLand(const World& w) {
  Id r = 0;
  w.provinces.each([&](const Province& p) {
    if (r || p.sea || !p.owner) return;
    const Faction* f = w.faction(p.owner);
    if (f && f->isState()) r = p.owner;
  });
  return r;
}

// Изображение реликвии: самоцвет на тёмном фоне (радиальный градиент).
std::string gemPng(u8 r0, u8 g0, u8 b0) {
  codec::RgbaImage img;
  img.w = img.h = 96;
  img.rgba.resize(size_t(img.w) * size_t(img.h) * 4);
  for (int y = 0; y < img.h; y++)
    for (int x = 0; x < img.w; x++) {
      u8* p = &img.rgba[(size_t(y) * size_t(img.w) + size_t(x)) * 4];
      const double dx = (x - 48) / 30.0, dy = (y - 50) / 34.0, d = dx * dx + dy * dy, hl = std::max(0.0, 1 - ((x - 40) * (x - 40) + (y - 38) * (y - 38)) / 90.0);
      const double k = d < 1 ? 1 - 0.55 * d : 0.12;
      p[0] = u8(std::min(255.0, r0 * k + 255 * hl * 0.6));
      p[1] = u8(std::min(255.0, g0 * k + 255 * hl * 0.6));
      p[2] = u8(std::min(255.0, b0 * k + 255 * hl * 0.6));
      p[3] = 255;
    }
  const std::vector<u8> png = codec::encodePng(img, 6);
  return std::string(png.begin(), png.end());
}

bool hasToast(Harness& h, std::string_view part) {
  for (auto& t : h->toasts())
    if (t.text.find(part) != std::string::npos) return true;
  return false;
}

// Перетащить центр элемента a в центр элемента b.
bool dragUi(Harness& h, const std::string& a, const std::string& b) {
  const RectF* ra = h->uiRect(a);
  const RectF* rb = h->uiRect(b);
  if (!ra || !rb) return false;
  const RectF x = *ra, y = *rb;
  h.drag(x.cx(), x.cy(), y.cx(), y.cy());
  ed::quick(h);
  return true;
}

// Щелчок по строке таблицы справочника у её левого края (не по полям строки).
bool clickRow(Harness& h, const std::string& mark) {
  if (!ed::reveal(h, mark)) return false;
  const RectF* r = h->uiRect(mark);
  if (!r) return false;
  const RectF x = *r;
  h.click(x.x + 8, x.cy());
  ed::quick(h);
  return true;
}

void openHero(Harness& h, Id hero, const char* tab) {
  h->ui.tabOf[app::SelType::Character] = tab;
  h->select(app::SelType::Character, hero);
  h.settle();
  h.dropToasts();
}

}  // namespace

// ---------------------------------------------------------------- справочник «Классы героев»
TEST(app_heroes2_classes_editor) {
  Harness h("heroes2_classes", 1440, 900);
  h.demo();
  h.dropToasts();
  h->openEditor("catalogs", 10);
  ed::quick(h);
  CHECK(h->uiRect("catalogs.view.classes") != nullptr);
  CHECK_EQ(h->world().catalogs->classes.size(), size_t(20));
  const Id pal = classNamed(h->world(), "Палладин");
  CHECK(pal != 0);
  CHECK(h.clickUi("catalogs.class." + std::to_string(pal)));
  ed::quick(h);
  // Пустая ячейка — новый талант, название сразу в правке.
  CHECK(h.clickUi("catalogs.tree.cell.0.1"));
  ed::quick(h);
  CHECK_EQ(h->world().heroClass(pal)->talents.size(), size_t(1));
  const Id t1 = h->world().heroClass(pal)->talents[0].id;
  h.retype("Святой щит");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK_EQ(talentOf(h->world(), t1)->name, std::string("Святой щит"));
  CHECK(ed::enterValue(h, "catalogs.talent.cost", "3"));
  CHECK_EQ(talentOf(h->world(), t1)->cost, 3);
  // Ярус 2: «Кара небес» с условием «Святой щит» (стрелка).
  CHECK(h.clickUi("catalogs.tree.cell.1.1"));
  ed::quick(h);
  CHECK_EQ(h->world().heroClass(pal)->talents.size(), size_t(2));
  const Id t2 = h->world().heroClass(pal)->talents[1].id;
  h.retype("Кара небес");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK(ed::pickInCombo(h, "catalogs.talent.prereq", "Святой"));
  CHECK_EQ(talentOf(h->world(), t2)->prereq, t1);
  // Ярус 3, другой столбец: «Гнев» с условием «Кара небес».
  CHECK(h.clickUi("catalogs.tree.cell.2.3"));
  ed::quick(h);
  const Id t3 = h->world().heroClass(pal)->talents[2].id;
  h.retype("Гнев");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK(ed::pickInCombo(h, "catalogs.talent.prereq", "Кара"));
  CHECK_EQ(talentOf(h->world(), t3)->prereq, t2);
  // Модификатор героя у таланта: встроенный «Непреклонный лоялист».
  CHECK(ed::pickInCombo(h, "catalogs.talent.mods", "Непреклонный"));
  CHECK_EQ(talentOf(h->world(), t3)->modifiers.size(), size_t(1));
  // Ещё таланты для вида дерева.
  CHECK(h->act("Таланты", [&](Tx& tx) {
    const Id a = rules::addTalent(tx, pal, 0, 3, "Благословение");
    const Id b = rules::addTalent(tx, pal, 1, 3, "Аура света");
    rules::setTalentPrereq(tx, b, a);
    rules::addTalent(tx, pal, 0, 0, "Молитва");
  }));
  ed::quick(h);
  // Перетаскивание «Гнев» в ярус 4 (пустой ярус для новых талантов), столбец 1.
  CHECK(dragUi(h, "catalogs.tree.talent." + std::to_string(t3), "catalogs.tree.cell.3.0"));
  CHECK(talentOf(h->world(), t3)->row == 3 && talentOf(h->world(), t3)->col == 0);
  // Перенос условия ниже зависимого — отказ с причиной.
  h.dropToasts();
  CHECK(dragUi(h, "catalogs.tree.talent." + std::to_string(t1), "catalogs.tree.cell.2.2"));
  CHECK_EQ(talentOf(h->world(), t1)->row, 0);
  CHECK(hasToast(h, "должно стоять выше"));
  // Очков на ярус.
  CHECK(ed::enterValue(h, "catalogs.classCard.tier", "4"));
  CHECK_EQ(h->world().heroClass(pal)->tierPoints, 4);
  CHECK(h.clickUi("catalogs.tree.talent." + std::to_string(t2)));
  ed::quick(h);
  ed::shotClean(h, "heroes2_classes");
  // Удаление таланта: подтверждение; у зависимого условие снимается.
  CHECK(ed::clickRevealed(h, "catalogs.talent.delete", "catalogs.classCard"));
  CHECK(ed::confirmDialog(h));
  CHECK(talentOf(h->world(), t2) == nullptr);
  CHECK_EQ(talentOf(h->world(), t3)->prereq, Id(0));
  h->undo();
  ed::quick(h);
  CHECK(talentOf(h->world(), t2) != nullptr);
  // Новый класс: название сразу в правке.
  CHECK(h.clickUi("catalogs.add"));
  ed::quick(h);
  h.retype("Странник");
  h.key(Key::Enter);
  ed::quick(h);
  CHECK(classNamed(h->world(), "Странник") != 0);
  ed::shotClean(h, "heroes2_classes_new");
}

// Узкое окно: карточка таланта — под деревом.
TEST(app_heroes2_classes_narrow) {
  Harness h("heroes2_classes_narrow", 1180, 820);
  h.demo();
  h.dropToasts();
  const Id nec = classNamed(h->world(), "Некромант");
  CHECK(h->act("Таланты", [&](Tx& tx) {
    const Id a = rules::addTalent(tx, nec, 0, 1, "Поднять мертвых");
    const Id b = rules::addTalent(tx, nec, 1, 1, "Костяной щит");
    const Id c = rules::addTalent(tx, nec, 2, 2, "Жатва душ");
    rules::setTalentPrereq(tx, b, a);
    rules::setTalentPrereq(tx, c, b);
    rules::addTalent(tx, nec, 0, 3, "Шёпот могил");
  }));
  h->openEditor("catalogs", 10);
  ed::quick(h);
  CHECK(h.clickUi("catalogs.class." + std::to_string(nec)));
  ed::quick(h);
  CHECK(h->uiRect("catalogs.tree") != nullptr);
  ed::shotClean(h, "heroes2_classes_narrow");
}

// ---------------------------------------------------------------- вкладка «Таланты» героя
TEST(app_heroes2_hero_talents) {
  fc::HideTestRegs regs;
  Harness h("heroes2_talents", 1440, 1000);
  h.demo();
  h.waitMap();
  const Id st = stateWithLand(h->world());
  const Id hero = stateHero(h->world(), st);
  CHECK(st && hero);
  const Id pal = classNamed(h->world(), "Палладин"), mag = classNamed(h->world(), "Маг");
  Id t1 = 0, t2 = 0, t3 = 0, t4 = 0;
  CHECK(h->act("Дерево", [&](Tx& tx) {
    HeroClass c = *tx.w().heroClass(pal);
    c.tierPoints = 3;
    rules::setHeroClass(tx, c);
    t1 = rules::addTalent(tx, pal, 0, 0, "Свет");
    t2 = rules::addTalent(tx, pal, 0, 2, "Щит");
    t3 = rules::addTalent(tx, pal, 1, 1, "Кара");
    t4 = rules::addTalent(tx, pal, 2, 1, "Длань");
    Talent a = *tx.w().heroClass(pal)->talent(t1);
    a.cost = 2;
    rules::setTalent(tx, a);
    rules::setTalentPrereq(tx, t3, t1);
    rules::setTalentPrereq(tx, t4, t3);
    rules::setCharacterClass(tx, hero, pal);
    rules::setHeroLevel(tx, hero, 5);
  }));
  openHero(h, hero, "character.talents");
  CHECK(h->uiRect("character.tree.talent." + std::to_string(t1)) != nullptr);
  // Изучить «Свет»; «Кара» — ярус закрыт (вложено 2 из 3).
  CHECK(h.clickUi("character.tree.talent." + std::to_string(t1)));
  h.settle();
  CHECK(h->world().character(hero)->talents == std::vector<Id>{t1});
  h.dropToasts();
  CHECK(h.clickUi("character.tree.talent." + std::to_string(t3)));
  h.settle();
  CHECK(hasToast(h, "Ярус 2"));
  CHECK(h->world().character(hero)->talents.size() == 1);
  CHECK(h.clickUi("character.tree.talent." + std::to_string(t2)));
  h.settle();
  CHECK(h.clickUi("character.tree.talent." + std::to_string(t3)));
  h.settle();
  CHECK((h->world().character(hero)->talents == std::vector<Id>{t1, t2, t3}));
  // Отменить «Свет» нельзя: от него зависит «Кара».
  h.dropToasts();
  CHECK(h.clickUi("character.tree.talent." + std::to_string(t1), platform::MouseRight));
  h.settle();
  CHECK(hasToast(h, "зависит"));
  CHECK_EQ(h->world().character(hero)->talents.size(), size_t(3));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("heroes2_talents"));
  // Отменить «Кару» — правый щелчок.
  CHECK(h.clickUi("character.tree.talent." + std::to_string(t3), platform::MouseRight));
  h.settle();
  CHECK((h->world().character(hero)->talents == std::vector<Id>{t1, t2}));
  // «Сбросить таланты» — с подтверждением.
  CHECK(fc::clickIn(h, "character.talents.reset"));
  CHECK(ed::confirmDialog(h));
  CHECK(h->world().character(hero)->talents.empty());
  // «Сведения»: уровень ниже вложенных очков — подтверждение сброса.
  CHECK(h->act("Таланты", [&](Tx& tx) {
    rules::learnTalent(tx, hero, t1);
    rules::learnTalent(tx, hero, t2);
  }));
  openHero(h, hero, "character.info");
  CHECK(fc::typeNumber(h, "character.level", "12"));
  CHECK_EQ(h->world().character(hero)->level, 12);
  CHECK(fc::typeNumber(h, "character.level", "2"));
  CHECK(h->hasDialog("confirm"));
  CHECK(ed::confirmDialog(h));
  CHECK(h->world().character(hero)->level == 2 && h->world().character(hero)->talents.empty());
  // Смена класса с изученными талантами — подтверждение.
  CHECK(h->act("Таланты", [&](Tx& tx) { rules::learnTalent(tx, hero, t2); }));
  CHECK(fc::pickInCombo(h, "character.class", "Маг"));
  CHECK(ed::confirmDialog(h));
  CHECK(h->world().character(hero)->heroClass == mag && h->world().character(hero)->talents.empty());
  CHECK(fc::ensureVisible(h, "character.level"));
  h.dropToasts();
  h.settle();
  CHECK(h.shot("heroes2_info"));
  // Список персонажей: значок класса и уровень у героя с классом.
  CHECK(h->act("Классы", [&](Tx& tx) {
    const char* names[] = {"Воин", "Маг", "Жрец", "Ассасин"};
    int k = 0;
    const Id olsta = fc::findFaction(tx.w(), "Вольные города Ольсты");
    for (Id cid : tx.w().characters.ids()) {
      if (tx.w().character(cid)->faction != olsta || k >= 4) continue;
      rules::setCharacterClass(tx, cid, classNamed(tx.w(), names[k]));
      rules::setHeroLevel(tx, cid, 5 + 11 * k);
      k++;
    }
  }));
  h->openDirectory("characters");
  h.settle();
  h.dropToasts();
  CHECK(h.shot("heroes2_list"));
}

// ---------------------------------------------------------------- «Возвысить до Лича», инвентарь со значками
TEST(app_heroes2_lich) {
  fc::HideTestRegs regs;
  Harness h("heroes2_lich", 1440, 1000);
  h.demo();
  h.waitMap();
  const Id st = stateWithLand(h->world());
  const Id hero = stateHero(h->world(), st), other = stateHero(h->world(), st, 1);
  CHECK(st && hero && other);
  Id skull = 0, ring = 0, heart = 0;
  CHECK(h->act("Некромант", [&](Tx& tx) {
    skull = rules::addRelic(tx, "Чёрный череп", Rarity::Epic);
    ring = rules::addRelic(tx, "Медный перстень", Rarity::Rare);
    heart = rules::addRelic(tx, "Сердце зимы", Rarity::Legendary);
    rules::setRelicImage(tx, heart, gemPng(90, 170, 255));
    rules::setRelicImage(tx, skull, gemPng(170, 90, 230));
    rules::giveRelic(tx, hero, ring);
    rules::giveRelic(tx, hero, skull);
    rules::giveRelic(tx, hero, heart);
    rules::addModifier(tx, rules::ModTarget::Character, hero, rules::ensureBuiltinMod(tx, schema::mod::Necromancer));
    Faction& f = tx.faction(st);
    f.res[rules::ensureResource(tx, schema::kResCorpses)] = 30000;
    f.ess[rules::deathEssence(tx.w())] = 6000;
  }));
  openHero(h, other, "character.info");
  CHECK(h->uiRect("character.lich") == nullptr);   // без «Некроманта» кнопки нет
  openHero(h, hero, "character.info");
  // Находку археологов из свободных героям не выдают: уведомление с причиной, инвентарь прежний.
  Id find = 0;
  CHECK(h->act("Находка", [&](Tx& tx) {
    find = rules::addRelic(tx, "Черепок Шантири", Rarity::Common);
    rules::setRelicGroup(tx, find, tx.w().catalogs->relicGroupId(schema::kRelicArchFinds));
  }));
  h.settle();
  CHECK(fc::pickInCombo(h, "character.addRelic", "Черепок"));
  h.settle();
  CHECK(hasToast(h, "археологическая находка"));
  CHECK_EQ(h->world().character(hero)->inventory.size(), size_t(3));
  h.dropToasts();
  // Инвентарь: строки реликвий по главенству редкости, значки с изображением.
  CHECK(fc::ensureVisible(h, "character.relic." + std::to_string(ring)));
  h.settle();
  CHECK(h.shot("heroes2_inventory"));
  CHECK(fc::clickIn(h, "character.lich"));
  h.settle();
  CHECK(h->hasDialog("hero.lich"));
  CHECK(h.clickUi("lich.relic." + std::to_string(heart)));
  h.settle();
  CHECK(h.shot("heroes2_lich_dialog"));
  CHECK(h.clickUi("lich.ok"));
  h.settle();
  CHECK(!h->hasDialog("hero.lich"));
  CHECK(rules::characterHas(h->world(), hero, schema::mod::Lich));
  CHECK(!rules::characterHas(h->world(), hero, schema::mod::Necromancer));
  CHECK_EQ(h->world().relic(heart)->name, "Сердце зимы (Филактерия «" + h->world().character(hero)->name + "»)");
  CHECK_NEAR(h->world().faction(st)->stock(rules::resourceId(h->world(), schema::kResCorpses)), 10000, 1e-9);
  h.settle();
  CHECK(h->uiRect("character.lich") == nullptr);
  // Убрать реликвию из инвентаря — к государству героя.
  CHECK(fc::ensureVisible(h, "character.relic." + std::to_string(ring)));
  {
    const RectF r = *h->uiRect("character.relic." + std::to_string(ring));
    h.click(r.right() - 16, r.cy());
  }
  h.settle();
  CHECK(rules::relicPlace(h->world(), ring).kind == rules::RelicPlace::State);
}

// ---------------------------------------------------------------- справочник реликвий: группы, изображение, канон
TEST(app_heroes2_relic_catalog) {
  Harness h("heroes2_relics", 1440, 900);
  h.demo();
  h.dropToasts();
  const Id st = stateWithLand(h->world());
  const Id hero = stateHero(h->world(), st);
  const Id finds = h->world().catalogs->relicGroupId(schema::kRelicArchFinds);
  CHECK(finds != 0);
  Id arms = 0, blades = 0, crown = 0, blade = 0, shard = 0, orb = 0, cup = 0;
  CHECK(h->act("Реликвии", [&](Tx& tx) {
    arms = rules::addRelicGroup(tx, "Оружие");
    blades = rules::addRelicGroup(tx, "Клинки", arms);
    crown = rules::addRelic(tx, "Корона Предвечных", Rarity::Epochal);
    blade = rules::addRelic(tx, "Клинок скорби", Rarity::Legendary);
    orb = rules::addRelic(tx, "Сфера бурь", Rarity::Epic);
    cup = rules::addRelic(tx, "Кубок пира", Rarity::Rare);
    shard = rules::addRelic(tx, "Черепок Шантири", Rarity::Common);
    rules::setRelicGroup(tx, blade, blades);
    rules::setRelicGroup(tx, orb, arms);
    rules::setRelicGroup(tx, shard, finds);
    rules::setRelicImage(tx, blade, gemPng(255, 140, 40));
    rules::setRelicImage(tx, orb, gemPng(160, 80, 255));
    rules::giveRelic(tx, hero, blade);
    rules::moveRelic(tx, shard, rules::RelicPlace{rules::RelicPlace::State, st, 0, st});
  }));
  h->openEditor("catalogs", 8);
  ed::quick(h);
  CHECK(h->uiRect("catalogs.view.relics") != nullptr);
  // Группа «Археологические находки» — с замком (удалить нельзя).
  CHECK(h->uiRect("catalogs.relicGroup." + std::to_string(finds)) != nullptr);
  CHECK(h->uiRect("catalogs.relicGroup." + std::to_string(finds) + ".delete") == nullptr);
  ed::shotClean(h, "heroes2_relics");
  // Новая группа: название сразу в правке.
  CHECK(h.clickUi("catalogs.relicNewGroup"));
  ed::quick(h);
  h.retype("Посохи");
  h.key(Key::Enter);
  ed::quick(h);
  Id staffs = 0;
  for (const RelicGroup& g : h->world().catalogs->relicGroups)
    if (g.name == "Посохи") staffs = g.id;
  CHECK(staffs != 0);
  // Реликвия «Кубок пира» — в группу «Посохи» (карточка).
  CHECK(clickRow(h, "catalogs.relic." + std::to_string(cup)));
  CHECK(ed::pickInCombo(h, "catalogs.relicCard.group", "Посохи"));
  CHECK_EQ(h->world().relic(cup)->group, staffs);
  // Связь с карточкой проекта по ID: описание и изображение — из карточки.
  CHECK(clickRow(h, "catalogs.relic." + std::to_string(crown)));
  CHECK(ed::enterValue(h, "canon.relic.entity", "ASSET-0037"));
  CHECK_EQ(h->world().relic(crown)->entity, std::string("ASSET-0037"));
  CHECK(!h->world().relic(crown)->desc.empty());
  CHECK(!h->world().relic(crown)->image.empty());
  // Поиск карточек реликвий — только предметы.
  {
    const auto list = app::canon::find(h.a(), app::canon::Kind::Relic, "амулет");
    CHECK(!list.empty() && list[0].id == "ASSET-0037");
    for (const auto& c : app::canon::find(h.a(), app::canon::Kind::Relic, "армия")) CHECK(c.assetKind == "item");
  }
  for (int i = 0; i < 20; i++) h.frames(2);   // изображение декодируется в фоне
  ed::shotClean(h, "heroes2_relic_card");
  // Удаление группы «Оружие»: подгруппа и реликвия — на верхний уровень.
  CHECK(clickRow(h, "catalogs.relicGroup." + std::to_string(arms)));
  CHECK(ed::clickRevealed(h, "catalogs.relicGroup." + std::to_string(arms) + ".delete"));
  CHECK(ed::confirmDialog(h));
  CHECK(h->world().catalogs->relicGroup(arms) == nullptr);
  CHECK(h->world().catalogs->relicGroup(blades)->parent == 0);
  CHECK(h->world().relic(orb)->group == 0);
}

// Узкое окно: дерево реликвий и карточка под ним.
TEST(app_heroes2_relics_narrow) {
  Harness h("heroes2_relics_narrow", 1180, 820);
  h.demo();
  h.dropToasts();
  CHECK(h->act("Реликвии", [&](Tx& tx) {
    const Id g = rules::addRelicGroup(tx, "Регалии");
    const Id crown = rules::addRelic(tx, "Корона Предвечных", Rarity::Epochal);
    const Id orb = rules::addRelic(tx, "Сфера бурь", Rarity::Epic);
    rules::addRelic(tx, "Медный амулет", Rarity::Common);
    rules::setRelicGroup(tx, crown, g);
    rules::setRelicGroup(tx, orb, g);
    rules::setRelicImage(tx, orb, gemPng(160, 80, 255));
  }));
  h->openEditor("catalogs", 8);
  ed::quick(h);
  for (int i = 0; i < 10; i++) h.frames(2);
  CHECK(h->uiRect("catalogs.relicCard.name") != nullptr);
  ed::shotClean(h, "heroes2_relics_narrow");
}

// ---------------------------------------------------------------- хранилище реликвий в провинции
TEST(app_heroes2_relic_store) {
  fc::HideTestRegs regs;
  Harness h("heroes2_store", 1440, 1100);
  h.demo();
  h.waitMap();
  const Id st = stateWithLand(h->world());
  const Id hero = stateHero(h->world(), st);
  Id pid = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!pid && !p.sea && p.owner == st) pid = p.id;
  });
  CHECK(pid && hero);
  Id store = 0, free = 0, mine = 0, held = 0;
  CHECK(h->act("Хранилище", [&](Tx& tx) {
    store = rules::createBuilding(tx, 0, "Сокровищница");
    rules::setBuildingFlag(tx, store, rules::BuildingFlag::RelicStore, true);
    Province& p = tx.province(pid);   // место для постройки
    p.size = ProvSize::Large;
    p.city = CityType::City;
    rules::placeBuilding(tx, pid, store, 1);
    free = rules::addRelic(tx, "Кубок королей", Rarity::Legendary);
    mine = rules::addRelic(tx, "Знамя предков", Rarity::Rare);
    held = rules::addRelic(tx, "Перстень мудреца", Rarity::Epic);
    rules::setRelicImage(tx, free, gemPng(255, 190, 60));
    rules::moveRelic(tx, mine, rules::RelicPlace{rules::RelicPlace::State, st, 0, st});
    rules::giveRelic(tx, hero, held);
  }));
  h->ui.tabOf[app::SelType::Province] = "province.buildings";
  h->select(app::SelType::Province, pid);
  h.settle();
  h.dropToasts();
  const std::string mark = "prov.store." + std::to_string(store);
  CHECK(fc::pickInCombo(h, mark + ".put", "Кубок"));
  CHECK(rules::relicPlace(h->world(), free).kind == rules::RelicPlace::Building);
  CHECK(fc::pickInCombo(h, mark + ".put", "Перстень"));
  CHECK(rules::relicPlace(h->world(), held).kind == rules::RelicPlace::Building);
  CHECK(h->world().character(hero)->inventory.empty());
  CHECK(fc::pickInCombo(h, mark + ".put", "Знамя"));
  CHECK_EQ(h->world().province(pid)->buildings.back().relics.size(), size_t(3));
  CHECK(fc::ensureVisible(h, mark + ".relic." + std::to_string(free)));
  h.dropToasts();
  h.move(300, 600);   // без подсказки под указателем
  h.settle();
  CHECK(h.shot("heroes2_relic_store"));
  // Вынуть — реликвия переходит государству-владельцу.
  CHECK(fc::clickIn(h, mark + ".take." + std::to_string(held)));
  h.settle();
  CHECK(rules::relicPlace(h->world(), held).kind == rules::RelicPlace::State);
}
