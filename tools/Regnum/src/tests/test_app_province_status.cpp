// Метки состояния провинции в шапке (аудит 2026-10-23, 8.1): «Чума · N», «Иммунитет · N», «Карантин · N», «Пустошь»,
// «Осквернена»; «Карантин» — игровое действие владельца у провинции с чумой или соседней с ней, снять — крестик метки.
#include "app/shell/role.h"
#include "tests/test_app_role_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

bool hasKey(const World& w, Id pid, std::string_view key) {
  const Province* p = w.province(pid);
  if (!p) return false;
  for (Id m : p->modifiers)
    if (const Modifier* x = w.modifier(m); x && x->key == key) return true;
  return false;
}

}  // namespace

TEST(app_province_status_plague_and_quarantine) {
  Harness h("province_status", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг"), kor = factionByName(h->world(), "Корвен");
  const Id pid = calmProvince(h, hel, 100);
  CHECK(pid && kor);
  if (!pid) return;
  Id nb = 0;
  for (Id n : neighborsOf(h->world(), pid))
    if (const Province* p = h->world().province(n); !nb && p && p->owner && rules::canPlague(h->world(), n)) nb = n;
  CHECK(h->act("Чума", [&](Tx& tx) { CHECK(rules::infectPlague(tx, pid)); }));
  provinceTab(h, pid, "province.overview");
  CHECK(h->uiRect("province.status.plague") != nullptr);
  CHECK(h->uiRect("province.status.quarantine") == nullptr);
  CHECK(h->uiRect("province.quarantine") != nullptr);
  // «Карантин» — метка со сроком, кнопки больше нет.
  CHECK(h.clickUi("province.quarantine"));
  h.settle();
  CHECK(hasKey(h->world(), pid, schema::mod::Quarantine));
  CHECK(h->uiRect("province.status.quarantine") != nullptr);
  CHECK(h->uiRect("province.quarantine") == nullptr);
  h.dropToasts();
  h.settle();
  if (const RectF* hd = h->uiRect("inspector")) CHECK(cropShot("province_status_header", RectF{hd->x, hd->y, hd->w, 220}, 2));
  // Соседняя с чумой провинция — «Карантин» доступен её владельцу.
  if (nb) {
    provinceTab(h, nb, "province.overview");
    CHECK(h->uiRect("province.quarantine") != nullptr);
    CHECK(h->uiRect("province.status.plague") == nullptr);
  }
  // Игрок другого государства: метки видны, кнопок нет.
  app::detail::role::becomePlayer(h.a(), kor);
  h.settle();
  provinceTab(h, pid, "province.overview");
  CHECK(h->uiRect("province.status.plague") != nullptr);
  CHECK(h->uiRect("province.status.quarantine") != nullptr);
  CHECK(h->uiRect("province.quarantine") == nullptr);
  // Игрок-владелец снимает карантин крестиком метки.
  app::detail::role::becomePlayer(h.a(), hel);
  h.settle();
  provinceTab(h, pid, "province.overview");
  const RectF* chip = h->uiRect("province.status.quarantine");
  CHECK(chip != nullptr);
  if (chip) h.click(chip->right() - 10, chip->cy());
  h.settle();
  CHECK(!hasKey(h->world(), pid, schema::mod::Quarantine));
}

TEST(app_province_status_waste_and_desecration) {
  Harness h("province_status_waste", 1440, 1000);
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  const Id pid = calmProvince(h, hel, 100);
  CHECK(pid != 0);
  if (!pid) return;
  CHECK(h->act("Пустошь и осквернение", [&](Tx& tx) {
    rules::addModifier(tx, rules::ModTarget::Province, pid, rules::ensureBuiltinMod(tx, schema::mod::UndeadWaste));
    rules::addModifier(tx, rules::ModTarget::Province, pid, rules::ensureBuiltinMod(tx, schema::mod::Desecrated));
  }));
  provinceTab(h, pid, "province.overview");
  CHECK(h->uiRect(std::string("province.status.") + schema::mod::UndeadWaste) != nullptr);
  CHECK(h->uiRect(std::string("province.status.") + schema::mod::Desecrated) != nullptr);
  CHECK(h->uiRect("province.quarantine") == nullptr);   // чумы нет рядом
}
