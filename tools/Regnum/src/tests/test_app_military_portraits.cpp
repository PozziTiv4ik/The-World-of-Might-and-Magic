// Портреты в кругах (ТЗ «Исправления», п.2): герои войска и гарнизона, «Судьба героев» — мини-портрет в круге;
// у войска с главным полководцем, у которого есть портрет, вместо значка войска (шапка инспектора, список войск
// выдвижной панели, карточка битвы) — портрет в круге с кольцом цвета фракции; без портрета — фигурка.
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Портрет: пурпурный прямоугольник 60 × 80 (PNG) — легко узнать по пикселям.
std::string magentaPortrait() {
  codec::RgbaImage img;
  img.w = 60;
  img.h = 80;
  img.rgba.resize(size_t(img.w) * size_t(img.h) * 4);
  for (size_t i = 0; i < img.rgba.size(); i += 4) {
    img.rgba[i] = 255;
    img.rgba[i + 1] = 0;
    img.rgba[i + 2] = 255;
    img.rgba[i + 3] = 255;
  }
  const std::vector<u8> png = codec::encodePng(img, 6);
  return std::string(png.begin(), png.end());
}

bool magentaAt(Harness& h, float x, float y) {
  const u32 p = h.pixel(x, y);
  const int r = int((p >> 16) & 255), g = int((p >> 8) & 255), b = int(p & 255);
  return r > 200 && g < 70 && b > 200;
}

// Кадры, пока портрет декодируется в фоне (кадр с готовым портретом рисуется следующим).
void waitPortraits(Harness& h) {
  for (int i = 0; i < 20; i++) {
    h.settle();
    h.frames(2);
  }
}

}  // namespace

TEST(app_military_portraits) {
  Harness h("military_portraits", 1440, 1000);
  RealArmyTools tools;
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id hero = freeHero(h, hel);
  CHECK(hero != 0);
  if (!hero) return;
  // Войско с главным полководцем без портрета — фигурка.
  const Id pid = app::mil::provinceUnder(h->world(), h->world().army(armyOf(h->world(), hel, ArmyKind::Army))->pos);
  CHECK(pid != 0);
  const Id army = spawnArmy(h, hel, h->world().army(armyOf(h->world(), hel, ArmyKind::Army))->pos + Vec2(300, 0), 500, hero);
  CHECK(army != 0);
  if (!army) return;
  CHECK(h->act("Название", [&](Tx& tx) { rules::renameArmy(tx, army, "Гвардия портрета"); }));
  h->ui.tabOf[app::SelType::Army] = "army.units";
  h->select(app::SelType::Army, army, true);
  waitPortraits(h);
  const RectF* fig = h->uiRect("army.figure");
  CHECK(fig != nullptr);
  if (fig) CHECK(!magentaAt(h, fig->cx(), fig->cy()));
  // Портрет у полководца — вместо фигурки портрет в круге (и у героя в списке войска).
  CHECK(h->act("Портрет", [&](Tx& tx) { tx.character(hero).portrait = magentaPortrait(); }));
  waitPortraits(h);
  h.dropToasts();
  h.settle();
  fig = h->uiRect("army.figure");
  CHECK(fig != nullptr);
  if (fig) {
    const RectF r = *fig;
    CHECK(magentaAt(h, r.cx(), r.cy()));
    CHECK(!magentaAt(h, r.x + 1, r.y + 1));   // угол — вне круга
    CHECK(cropShot("military_portrait_header", RectF{r.x - 8, r.y - 8, r.w + 260, r.h + 16}, 3));
  }
  CHECK(reveal(h, "army.hero." + std::to_string(hero)));
  if (const RectF* av = h->uiRect("army.hero." + std::to_string(hero))) CHECK(magentaAt(h, av->cx(), av->cy()));
  if (const RectF* insp = h->uiRect("inspector")) CHECK(cropShot("military_portrait_inspector", *insp));

  // Выдвижная панель «Войска и флот»: в строке войска — портрет полководца.
  h.key(Key::D5, ctrl());
  h.settle();
  CHECK(h.clickUi("mil.drawer.search"));
  h.type("Гвардия портрета");
  waitPortraits(h);
  const std::string item = "mil.drawer.item." + std::to_string(army);
  const RectF* it = h->uiRect(item);
  CHECK(it != nullptr);
  if (it) {
    const RectF r = *it;
    CHECK(magentaAt(h, r.x + 17, r.cy()));
    CHECK(cropShot("military_portrait_drawer", RectF{r.x, r.y - 4, r.w, r.h + 8}, 2));
  }

  // Герой гарнизона — портрет в круге.
  const Id hero2 = freeHero(h, hel);
  CHECK(hero2 != 0);
  Id gp = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!gp && !p.sea && p.owner == hel) gp = p.id;
  });
  CHECK(h->act("Герой в гарнизоне", [&](Tx& tx) {
    tx.character(hero2).portrait = magentaPortrait();
    rules::setGarrisonHero(tx, gp, hero2, true);
  }));
  h->ui.tabOf[app::SelType::Province] = "province.garrison";
  h->select(app::SelType::Province, gp);
  waitPortraits(h);
  CHECK(reveal(h, "garrison.hero." + std::to_string(hero2)));
  waitPortraits(h);
  if (const RectF* av = h->uiRect("garrison.hero." + std::to_string(hero2))) {
    const RectF r = *av;
    CHECK(magentaAt(h, r.cx(), r.cy()));
  }
  if (const RectF* insp = h->uiRect("inspector")) CHECK(cropShot("military_portrait_garrison", *insp));

  // «Судьба героев»: портрет в круге.
  app::flow::openHeroFate(h.a(), {hero}, 0, pid);
  waitPortraits(h);
  CHECK(h->hasDialog("hero.fate"));
  if (const RectF* av = h->uiRect("fate.avatar." + std::to_string(hero))) {
    const RectF r = *av;
    CHECK(magentaAt(h, r.cx(), r.cy()));
  }
  h.dropToasts();
  h.settle();
  CHECK(h.shot("military_portrait_fate"));
  h.key(Key::Escape);
  h.settle();

  // Карточка битвы: у нападающего — портрет полководца вместо фигурки.
  const Id vk = factionByName(h->world(), "Валь-Кетр");
  CHECK(vk != 0);
  const Id enemy = armyOf(h->world(), vk, ArmyKind::Army);
  CHECK(enemy != 0);
  if (vk && enemy) {
    CHECK(h->act("Война", [&](Tx& tx) {
      if (tx.w().relation(hel, vk).s != RelStatus::War) rules::declareWar(tx, hel, vk);
    }));
    app::mil::openBattle(h.a(), army, enemy, h->world().army(army)->pos);
    waitPortraits(h);
    CHECK(h->hasDialog("battle"));
    if (const RectF* bf = h->uiRect("battle.figure.attacker")) {
      const RectF r = *bf;
      CHECK(magentaAt(h, r.cx(), r.cy()));
    }
    h.dropToasts();
    h.settle();
    CHECK(h.shot("military_portrait_battle"));
    CHECK(h.clickUi("battle.retreat"));
    h.settle();
  }
}
