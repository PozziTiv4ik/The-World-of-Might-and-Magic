// Режимы карты «Дипломатия» (клавиша 0; отношения относительно выбранного государства, у игрока — своего) и «Чума и
// пустоши» (слои карты) — аудит 2026-10-23, 8.1 и раздел 11.
#include "app/shell/role.h"
#include "tests/test_app_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;

TEST(app_map_modes_diplomacy_focus) {
  Harness h("map_modes");
  h.demo();
  const Id hel = factionByName(h->world(), "Хельдвиг"), kor = factionByName(h->world(), "Корвен");
  CHECK(hel && kor);
  h.key(Key::D0);
  CHECK(h->ui.mapMode == schema::MapMode::Diplomacy);
  CHECK_EQ(app::detail::renderOptions(h.a()).focusState, Id(0));   // администратор без выделения — без фокуса
  h->select(app::SelType::Faction, hel);
  h.settle();
  CHECK_EQ(app::detail::renderOptions(h.a()).focusState, hel);
  // Игрок: своё государство, пока не выбрано другое государство.
  role::becomePlayer(h.a(), kor);
  h.settle();
  CHECK_EQ(app::detail::renderOptions(h.a()).focusState, kor);
  Id helProv = 0;
  h->world().provinces.each([&](const Province& p) {
    if (!helProv && !p.sea && p.owner == hel) helProv = p.id;
  });
  h->select(app::SelType::Province, helProv);
  h.settle();
  CHECK_EQ(app::detail::renderOptions(h.a()).focusState, kor);
  // Слои карты: режимы с клавишами 1–9 и 0, «Чума и пустоши» — щелчком; легенда режима.
  h->clearSelection();
  h->openDrawer("layers");
  h.settle();
  CHECK(h->uiRect("mode.10") != nullptr);
  CHECK(h->uiRect("mode.11") != nullptr);
  CHECK(h->uiRect("legend") != nullptr);
  shotClean(h, "map_mode_diplomacy");
  CHECK(h.clickUi("mode.11"));
  h.settle();
  CHECK(h->ui.mapMode == schema::MapMode::Plague);
  CHECK_EQ(app::detail::renderOptions(h.a()).focusState, Id(0));
  shotClean(h, "map_mode_plague");
}
