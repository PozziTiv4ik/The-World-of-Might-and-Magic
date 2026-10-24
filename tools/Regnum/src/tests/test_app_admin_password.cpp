// Пароль администратора (аудит 2026-10-23, 1.4): задаётся, меняется и снимается в настройках («Правила») только
// администратором, хранится в мире хешем (Settings::adminKey — SHA-256 соли мира и пароля); переход в роль
// администратора из окна роли — только с паролем (поле скрывает ввод), мир с паролем открывается за игрока до выбора
// роли; ход завершает только администратор. Вид карты игрока (1.6) — свой, в app.json: мир не меняется.
#include "app/shell/role.h"
#include "base/fs.h"
#include "tests/test_app_role_util.h"

using namespace rg;
using namespace rg::apptest;
namespace role = rg::app::detail::role;

namespace {

constexpr const char* kSecret = "Тайна-42";

// Ввести текст в поле окна: щелчок, выделить всё, текст.
void typeInto(Harness& h, const std::string& field, const std::string& text) {
  CHECK_MSG(h.clickUi(field), field);
  h.key(Key::A, ctrl());
  h.type(text);
  h.settle();
}

// Настройки → «Правила» → «Задать…» / «Сменить…»: пароль дважды.
void setPassword(Harness& h, const std::string& pw) {
  h->showSettings();
  h.settle();
  if (const RectF* t = h->uiRect("settings.tabs")) h.click(t->x + t->w * 2.5f / 4, t->cy());
  h.settle();
  CHECK(h.clickUi("settings.password.set"));
  h.settle();
  CHECK(h->hasDialog("role.password"));
  typeInto(h, "role.password.new", pw);
  typeInto(h, "role.password.again", pw);
  CHECK(h.clickUi("role.password.ok"));
  h.settle();
  h->closeDialogs();
  h.settle();
}

}  // namespace

// Задать пароль, войти в роль администратора: неверный пароль — остаётся игрок, верный — администратор; снять пароль.
TEST(app_admin_password_set_and_enter) {
  Harness h("admin_password");
  h.demo();
  h.dropToasts();
  CHECK(!role::adminLocked(h->world()));
  setPassword(h, kSecret);
  CHECK(role::adminLocked(h->world()));
  CHECK_EQ(h->world().settings->adminKey, role::adminKeyOf(h->world(), kSecret));
  CHECK(h->world().settings->adminKey.find(kSecret) == std::string::npos);
  CHECK_EQ(h->world().settings->adminKey.size(), size_t(64));
  CHECK(role::adminPasswordOk(h->world(), kSecret));
  CHECK(!role::adminPasswordOk(h->world(), "тайна-42"));
  // Соль — мир: тот же пароль в другом мире даёт другой ключ.
  World other = h->world();
  {
    Tx tx(other);
    tx.meta().createdAt = "2000-01-01T00:00:00Z";
    other = std::move(tx).finish();
  }
  CHECK(role::adminKeyOf(other, kSecret) != role::adminKeyOf(h->world(), kSecret));

  const Id hel = factionByName(h->world(), "Хельдвиг");
  role::becomePlayer(h.a(), hel);
  h.settle();
  // Игроку настройки пароля недоступны, ход он не завершает.
  h.dropToasts();
  CHECK(!h->endTurnNow());
  CHECK(hasToast(h, "администратор"));
  // «Сменить роль…» → «Администратор редактора» — поле пароля.
  role::ask(h.a());
  h.settle();
  CHECK(h.clickUi("role.admin"));
  h.settle();
  CHECK(h->uiRect("role.password") != nullptr);
  CHECK(h->player());
  typeInto(h, "role.password", "неверно");
  shotClean(h, "admin_password_enter");
  CHECK(h.clickUi("role.password.ok"));
  h.settle();
  CHECK(h->uiRect("role.password.wrong") != nullptr);
  CHECK(h->player());
  // Esc — назад к ролям, повторно — пароль верный.
  h.key(Key::Escape);
  h.settle();
  CHECK(h->uiRect("role.admin") != nullptr);
  CHECK(h.clickUi("role.admin"));
  h.settle();
  typeInto(h, "role.password", kSecret);
  CHECK(h.clickUi("role.password.ok"));
  h.settle();
  CHECK(!h->hasDialog("role"));
  CHECK(h->admin());
  // Снять пароль: подтверждение; отменяется Ctrl+Z.
  h->showSettings();
  h.settle();
  if (const RectF* t = h->uiRect("settings.tabs")) h.click(t->x + t->w * 2.5f / 4, t->cy());
  h.settle();
  CHECK(h.clickUi("settings.password.clear"));
  h.settle();
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  h->closeDialogs();
  h.settle();
  CHECK(!role::adminLocked(h->world()));
  h->undo();
  h.settle();
  CHECK(role::adminLocked(h->world()));
}

// Мир с паролем при открытии (окно роли): до выбора роли — игрок; закрытое окно не делает администратором.
TEST(app_admin_password_world_open) {
  Harness h("admin_password_open");
  h.demo();
  CHECK(h->act("Пароль", [&](Tx& tx) { tx.settings().adminKey = role::adminKeyOf(tx.w(), kSecret); }));
  const std::string folder = fs::join(h.root, "Мир");
  CHECK(h->saveTo(folder));
  h->closeWorld();   // открыть заново, а не перечитать (перечитанный мир оставляет роль сеанса)
  h.settle();
  h->impl().cfg.askRole = true;
  CHECK(h->loadProject(folder));
  h.settle();
  CHECK(h->hasDialog("role"));
  CHECK(h->player());
  CHECK(h->playerState() != 0);
  h->closeDialogs();
  h.settle();
  CHECK(h->player());
  CHECK(!h->act("Правка", [&](Tx& tx) { tx.settings().adminKey.clear(); }));
  CHECK(role::adminLocked(h->world()));
}

// Вид карты у игрока — свой (app.json), мир не меняется; администратор меняет настройки мира. Автосохранение и
// правила — только администратору.
TEST(app_admin_password_player_view_local) {
  Harness h("admin_view", 1440, 1400);   // панель «Слои карты» целиком, без прокрутки
  h.demo();
  h.dropToasts();
  const Id hel = factionByName(h->world(), "Хельдвиг");
  role::becomePlayer(h.a(), hel);
  h.settle();
  const Settings s0 = *h->world().settings;
  CHECK(s0.labelStates);
  h->openDrawer("layers");
  h.settle();
  CHECK(h.clickUi("layers.labelStates"));
  h.settle();
  CHECK(h->world().settings->labelStates == s0.labelStates);   // мир не изменился
  CHECK(h->ui.playerView.has_value());
  CHECK(!role::viewSettings(h.a()).labelStates);
  CHECK(!role::mapWorld(h.a(), h->world()).settings->labelStates);
  CHECK(h.clickUi("tool.hideArmies"));
  h.settle();
  CHECK(h->world().settings->showArmies);
  CHECK(!role::viewSettings(h.a()).showArmies);
  CHECK(!h->store.canUndo());   // своё — не шаг отмены мира
  // В app.json.
  app::detail::savePrefs(h.a());
  auto text = fs::readFile(fs::join(h->dataDir(), "app.json"));
  CHECK(text.has_value() && text->find("playerView") != std::string::npos);
  // Настройки «Карта» у игрока — тоже свои: ползунок заливки не меняет мир.
  h->showSettings();
  h.settle();
  if (const RectF* t = h->uiRect("settings.tabs")) h.click(t->x + t->w * 1.5f / 4, t->cy());
  h.settle();
  CHECK(h->uiRect("settings.fill") != nullptr);
  if (const RectF* f = h->uiRect("settings.fill")) h.click(f->x + f->w * 0.1f, f->cy());
  h.settle();
  CHECK_NEAR(h->world().settings->fillOpacity, s0.fillOpacity, 1e-6);
  CHECK(std::fabs(role::viewSettings(h.a()).fillOpacity - s0.fillOpacity) > 0.05);
  h->closeDialogs();
  h.settle();
  // Администратор: настройки мира (своё игрока не действует).
  role::becomeAdmin(h.a());
  h.settle();
  CHECK(role::viewSettings(h.a()).labelStates);
  CHECK(h.clickUi("tool.hideArmies"));
  h.settle();
  CHECK(!h->world().settings->showArmies);
  CHECK(h->store.canUndo());
}
