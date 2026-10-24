// Regnum — роль за редактором (ТЗ «Доработки №4», п.5): окно выбора роли и государства игрока, ограничения игрока
// в оболочке, очко действий войск игрока (добавка к транзакциям встреч, приглушённые фигурки, отметка в панели и
// нижней строке войска), признак роли игрока в верхней строке.
#include "app/shell/role.h"

#include "app/shell/shell_internal.h"
#include "app/widgets.h"
#include "base/fs.h"
#include "base/json.h"
#include "codec/sha256.h"

namespace rg::app::detail::role {

using platform::Key;

namespace {

// ================================================================ запомненная роль мира (roles.json)
struct Remembered {
  bool player = false;
  Id state = 0;
};

std::string rolesPath(const App& a) { return fs::join(a.dataDir(), "roles.json"); }

json::Value readRoles(const App& a) {
  std::optional<json::Value> v;
  if (auto text = fs::readFile(rolesPath(a))) v = json::tryParse(*text);
  if (!v || !v->isObj()) return json::Value::object();
  return std::move(*v);
}

Remembered remembered(const App& a) {
  Remembered r;
  if (a.projectPath().empty()) return r;
  const json::Value v = readRoles(a);
  const json::Value& e = v.obj(a.projectPath());
  r.player = e.str("role") == "player";
  r.state = Id(std::max<i64>(0, e.integer("state")));
  return r;
}

void remember(const App& a, bool player, Id state) {
  if (a.projectPath().empty()) return;
  json::Value v = readRoles(a);
  json::Value e = json::Value::object();
  e.set("role", std::string(player ? "player" : "admin"));
  if (player) e.set("state", double(state));
  v.set(a.projectPath(), std::move(e));
  // Не больше 64 миров: старые записи — первыми.
  while (v.size() > 64) v.erase(v.members().front().first);
  std::string text = json::write(v, json::WriteOptions{2, false, false});
  text.push_back('\n');
  std::string err;
  if (!fs::writeFileAtomic(rolesPath(a), text, &err)) logWarn("roles.json: %s", err.c_str());
}

// ================================================================ очко действий во встрече
std::vector<Id>& pendingSpend() {
  static std::vector<Id> v;
  return v;
}

bool stateExists(const World& w, Id state) {
  const Faction* f = w.faction(state);
  return f && f->isState();
}

// Область показа столицы: провинция с окрестностями (не меньше 1600 × 900 единиц карты), без столицы — государство.
Box2 capitalBox(const World& w, Id state) {
  const Faction* f = w.faction(state);
  if (!f) return {};
  Box2 b = f->capital && w.province(f->capital) ? selectionBox(w, {SelType::Province, f->capital}) : Box2{};
  if (b.empty()) return selectionBox(w, {SelType::Faction, state});
  const Vec2 c = b.center();
  const double hw = std::max(b.w(), 1600.0) * 0.5, hh = std::max(b.h(), 900.0) * 0.5;
  return Box2(c.x - hw, c.y - hh, c.x + hw, c.y + hh);
}

void focusCapital(App& a, Id state) {
  if (!a.mapShown() || a.ui.playerState != state) return;
  const Box2 b = capitalBox(a.world(), state);
  if (!b.empty()) a.focusMap(b);
}

// Карта — с настройками вида роли (игрок — свои).
void syncMapView(App& a) {
  if (a.ui.screen != Screen::Editor) return;
  a.map().setWorld(mapWorld(a, a.world()));
  a.impl().mapGen++;
  a.requestRedraw();
}

// Мир с паролем администратора при открытии: до выбора роли — игрок (запомненное государство мира или первое из
// списка), чтобы закрытое окно роли не оставляло администратора без пароля.
void lockedStart(App& a) {
  const World& w = a.store.world();
  if (!adminLocked(w)) return;
  const Remembered r = remembered(a);
  Id st = r.player && stateExists(w, r.state) ? r.state : 0;
  if (!st) {
    const std::vector<Id> list = playableStates(w);
    if (!list.empty()) st = list.front();
  }
  if (!st) return;
  a.ui.role = Role::Player;
  a.ui.playerState = st;
  a.store.clearHistory();
  syncMapView(a);
}

// Число кодовых точек UTF-8 (точки поля пароля).
size_t utf8Count(std::string_view s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) n++;
  return n;
}

// Окно «Пароль администратора»: новый пароль дважды.
struct PasswordDlg final : Dialog {
  std::string pw, again;
  const char* id() const override { return "role.password"; }
  Style style(App&) override { return {"Пароль администратора", "lock", ui::Tone::Accent, 420}; }
  bool draw(App& a) override {
    passwordField("pw", pw, "Новый пароль", true);
    a.markUi("role.password.new");
    passwordField("again", again, "Ещё раз");
    a.markUi("role.password.again");
    const bool differ = !pw.empty() && !again.empty() && pw != again;
    if (differ) ui::label("Пароли не совпадают", {.font = ui::Font::Small, .ink = ui::Ink::Danger, .icon = "warning"});
    ui::ModalFooter f;
    if (ui::button("Отмена")) return false;
    a.markUi("role.password.cancel");
    if (ui::button("Сохранить", {.variant = ui::Variant::Primary, .icon = "lock", .disabled = pw.empty() || pw != again || !a.canEdit(),
                                 .isDefault = true})) {
      const std::string key = adminKeyOf(a.store.world(), pw);
      if (a.act("Пароль администратора", [&](Tx& tx) { tx.settings().adminKey = key; })) {
        a.toast("Пароль администратора задан", ToastKind::Success, "lock");
        return false;
      }
    }
    a.markUi("role.password.ok");
    return true;
  }
};

// ================================================================ окно выбора роли
struct StateRow {
  Id id = 0;
  bool main = false;
  std::string name, capital;
  i64 population = 0;
  int provinces = 0;
};

class RoleDialog final : public Dialog {
 public:
  explicit RoleDialog(int page) : page_(page) {}
  const char* id() const override { return "role"; }
  Style style(App& a) override {
    Style s;
    s.title = a.worldTitle();
    s.icon = "globe";
    s.width = 680;
    return s;
  }

  bool draw(App& a) override {
    if (!init_) start(a);
    return page_ == 0 ? roles(a) : page_ == 2 ? password(a) : states(a);
  }

 private:
  int page_ = 0;              // 0 — роли, 1 — государства игрока, 2 — пароль администратора
  bool init_ = false;
  Remembered last_;
  std::vector<StateRow> rows_;
  std::string q_, builtQ_ = "\x01";
  std::vector<int> shown_;    // строки rows_ после поиска
  int hi_ = 0;
  bool scrollToHi_ = true;
  bool focused_ = false;
  float lastMx_ = -1, lastMy_ = -1;
  std::string pw_;            // введённый пароль администратора
  bool wrong_ = false;        // неверный пароль (до следующего ввода)
  std::string tried_;

  // ---------------------------------------------------------------- пароль администратора
  bool password(App& a) {
    if (ui::keyPressed(Key::Escape)) {
      ui::consumeKey(Key::Escape);
      page_ = 0;
      return true;
    }
    {
      ui::Row head({ui::px(30), ui::fr(1)}, 30, 8);
      if (ui::iconButton("arrow-left", "Назад")) page_ = 0;
      ui::tooltip("Назад", {Key::Escape, 0});
      a.markUi("role.back");
      passwordField("pw", pw_, "Пароль администратора", true);
      a.markUi("role.password");
    }
    if (pw_ != tried_) wrong_ = false;
    if (wrong_) {
      ui::label("Неверный пароль", {.font = ui::Font::Small, .ink = ui::Ink::Danger, .icon = "warning"});
      a.markUi("role.password.wrong");
    }
    ui::ModalFooter f;
    if (ui::button("Войти", {.variant = ui::Variant::Primary, .icon = "unlock", .disabled = pw_.empty(), .isDefault = true})) {
      if (adminPasswordOk(a.store.world(), pw_)) {
        later(a, [](App& x) { becomeAdmin(x); });
        return false;
      }
      wrong_ = true;
      tried_ = pw_;
    }
    a.markUi("role.password.ok");
    return true;
  }

  void start(App& a) {
    init_ = true;
    last_ = remembered(a);
    if (a.player()) last_ = Remembered{true, a.ui.playerState};   // смена роли: текущее государство — первым
    const World& wd = a.store.world();
    auto calc = rules::calc(wd);
    for (Id id : playableStates(wd)) {
      const Faction* f = wd.faction(id);
      StateRow r;
      r.id = id;
      r.main = f->mainState;
      r.name = w::orName(f->name);
      if (f->capital && wd.province(f->capital)) r.capital = wd.provinceName(f->capital);
      if (const rules::FactionCalc* fc = calc->faction(id)) {
        r.population = fc->population;
        r.provinces = int(fc->provinces.size());
      }
      rows_.push_back(std::move(r));
    }
    if (last_.player && !stateExists(wd, last_.state)) last_ = Remembered{};
  }

  // ---------------------------------------------------------------- карточки ролей
  // locked — роль под паролем: значок замка на плитке, карточка доступна (дальше — ввод пароля).
  bool card(App& a, const char* mark, RectF r, const char* icon, std::string_view title, bool primary, bool disabled,
            const Faction* sub, bool locked = false) {
    const ui::Theme& th = ui::theme();
    ui::WidgetId wid = ui::id(mark);
    ui::Interaction it = ui::interact(wid, r, disabled ? ui::IfNone : ui::IfFocusable);
    const float hv = ui::animate(wid ^ 1, it.hovered && !disabled ? 1.f : 0.f, 0.14f);
    const float dn = ui::animate(wid ^ 2, it.held ? 1.f : 0.f, 0.08f);
    const RectF rr{r.x, r.y - 2 * hv + dn, r.w, r.h};
    ui::draw::shadow(rr, 14, 18 + 8 * hv, th.shadow.alpha(disabled ? 0.3f : 0.6f), 4 + 3 * hv);
    ui::draw::rect(rr, Color::mix(th.surface2, th.surface3, disabled ? 0.f : 0.25f + 0.55f * hv), 14);
    if (primary && !disabled) ui::draw::gradient(RectF{rr.x, rr.y, rr.w, rr.h * 0.6f}, th.accent.alpha(0.10f + 0.06f * hv), th.accent.alpha(0.f), 14);
    const Color bc = disabled ? th.border : primary ? th.accent.alpha(0.5f + 0.35f * hv) : Color::mix(th.border, th.borderStrong, hv);
    ui::draw::rectStroke(rr, bc, 14, 1);
    const RectF ic{std::round(rr.cx() - 30), rr.y + 26, 60, 60};
    if (disabled) ui::draw::rect(ic, th.surface3.alpha(0.6f), 16);
    else if (primary) ui::draw::gradient(ic, th.accentHover, th.accent.darken(0.08f), 16);
    else ui::draw::rect(ic, th.surface3, 16);
    ui::draw::icon(icon, ic.inset(15), disabled ? th.textMuted : primary ? th.onAccent : th.accent);
    if (disabled || locked) {
      const RectF lb{ic.right() - 14, ic.bottom() - 14, 20, 20};
      ui::draw::circle(lb.cx(), lb.cy(), 11, th.surface2);
      ui::draw::icon("lock", lb.inset(3), locked && !disabled ? th.accent : th.textMuted);
    }
    ui::draw::text(title, RectF{rr.x + 12, ic.bottom() + 14, rr.w - 24, 24}, ui::Font::Subtitle, disabled ? th.textMuted : th.text, ui::Align::Center);
    if (sub) {
      // Последнее государство игрока в этом мире: флаг и название под заголовком.
      const std::string name = w::orName(sub->name);
      const float tw = std::min(std::ceil(ui::measure(name, ui::Font::Small)) + 2, rr.w - 70);
      const float x0 = std::round(rr.cx() - (21 + 8 + tw) * 0.5f), y0 = ic.bottom() + 44;
      ui::at(RectF{x0, y0 + 2, 21, 14});
      ui::flag(sub->flag, 21, 14, 2);
      ui::draw::text(name, RectF{x0 + 29, y0, tw, 18}, ui::Font::Small, th.textDim);
    }
    if (it.focused) ui::draw::rectStroke(rr.expand(3), th.accent, 17, 2);
    a.markUi(mark, r);
    return !disabled && (it.clicked || it.keyActivated);
  }

  bool roles(App& a) {
    const World& w = a.store.world();
    const RectF row = ui::next(178);
    const float gap = 16, cw = std::floor((row.w - gap) * 0.5f);
    const bool noStates = rows_.empty();
    const bool lastPlayer = last_.player && !noStates;
    const Faction* sub = lastPlayer ? w.faction(last_.state) : nullptr;
    const RectF ra{row.x, row.y + 4, cw, row.h - 8}, rp{row.x + cw + gap, row.y + 4, cw, row.h - 8};
    const bool locked = adminLocked(w) && !a.admin();   // пароль — для входа в роль администратора
    if (card(a, "role.admin", ra, "map-edit", "Администратор редактора", !lastPlayer, false, nullptr, locked)) {
      if (locked) {
        page_ = 2;
        pw_.clear();
        wrong_ = false;
        return true;
      }
      later(a, [](App& x) { becomeAdmin(x); });
      return false;
    }
    if (locked) ui::hoverTip("role.admin.lock", ra, "Пароль администратора");
    if (card(a, "role.player", rp, "crown", "Игрок", lastPlayer, noStates, sub)) {
      page_ = 1;
      focused_ = false;
      scrollToHi_ = true;
    }
    if (noStates) ui::hoverTip("role.player.why", rp, "В мире нет государств");
    ui::spacer(4);
    return true;
  }

  // ---------------------------------------------------------------- список государств
  void filter() {
    shown_.clear();
    for (size_t i = 0; i < rows_.size(); i++) {
      const StateRow& r = rows_[i];
      if (!q_.empty() && !utf8::matches(r.name + " " + r.capital, q_)) continue;
      shown_.push_back(int(i));
    }
    builtQ_ = q_;
    hi_ = 0;
    if (q_.empty() && last_.player)
      for (size_t k = 0; k < shown_.size(); k++)
        if (rows_[size_t(shown_[k])].id == last_.state) hi_ = int(k);
    scrollToHi_ = true;
  }

  void pick(App& a, Id state) {
    later(a, [state](App& x) { becomePlayer(x, state); });
  }

  // Строка государства; true — выбрано.
  bool stateRow(App& a, const StateRow& s, RectF r, bool active, bool mouseMoved, int k) {
    const ui::Theme& th = ui::theme();
    const World& w = a.store.world();
    const Faction* f = w.faction(s.id);
    if (!f) return false;
    ui::IdScope sc{int(s.id)};
    ui::Interaction in = ui::interact(ui::id("row"), r);
    if (in.hovered && mouseMoved) hi_ = k;
    if (active) {
      ui::draw::rect(r, th.accent.alpha(0.14f), 9);
      ui::draw::rect(RectF{r.x, r.y + 12, 3, r.h - 24}, th.accent, 1.5f);
    } else if (in.hovered) {
      ui::draw::rect(r, th.hover, 9);
    }
    const RectF fr{r.x + 14, std::round(r.cy() - 14), 42, 28};
    ui::at(fr);
    ui::flag(f->flag, 42, 28, 3);
    if (s.main) {
      const RectF m{fr.right() - 8, fr.bottom() - 9, 14, 14};
      ui::draw::circle(m.cx(), m.cy(), 8, th.surface2);
      ui::draw::icon("star-filled", m.inset(1), th.accent);
    }
    // Справа: население и провинции.
    float right = r.right() - 14;
    const std::string provs = std::to_string(s.provinces);
    const float pw = std::ceil(ui::measure(provs, ui::Font::Small)) + 2;
    ui::draw::text(provs, RectF{right - pw, r.y, pw, r.h}, ui::Font::Small, th.textDim, ui::Align::Right);
    ui::draw::icon("province", RectF{right - pw - 20, r.cy() - 7, 14, 14}, th.textMuted);
    right -= pw + 20 + 18;
    const std::string pop = fmtShort(double(s.population));
    const float popW = std::ceil(ui::measure(pop, ui::Font::Small)) + 2;
    ui::draw::text(pop, RectF{right - popW, r.y, popW, r.h}, ui::Font::Small, th.textDim, ui::Align::Right);
    ui::draw::icon("population", RectF{right - popW - 20, r.cy() - 7, 14, 14}, th.textMuted);
    right -= popW + 20 + 16;
    // Название и столица.
    const float tx = fr.right() + 16, tw = std::max(40.f, right - tx);
    if (s.capital.empty()) {
      ui::draw::text(s.name, RectF{tx, r.y, tw, r.h}, ui::Font::Strong, th.text);
    } else {
      ui::draw::text(s.name, RectF{tx, r.y + 7, tw, 22}, ui::Font::Strong, th.text);
      ui::draw::icon("capital", RectF{tx, r.y + 31, 13, 13}, th.textMuted);
      ui::draw::text(s.capital, RectF{tx + 18, r.y + 28, tw - 18, 18}, ui::Font::Small, th.textMuted);
    }
    a.markUi("role.state." + std::to_string(s.id), r);
    if (!in.clicked) return false;
    pick(a, s.id);
    return true;
  }

  bool states(App& a) {
    const ui::Theme& th = ui::theme();
    if (ui::keyPressed(Key::Escape)) {
      ui::consumeKey(Key::Escape);
      if (ui::wantsKeyboard() && !q_.empty()) q_.clear();
      else page_ = 0;
      return true;
    }
    if (q_ != builtQ_) filter();
    int n = int(shown_.size());
    const ui::Mouse& mo = ui::mouse();
    bool mouseMoved = lastMx_ >= 0 && (std::fabs(mo.x - lastMx_) > 0.5f || std::fabs(mo.y - lastMy_) > 0.5f);
    lastMx_ = mo.x;
    lastMy_ = mo.y;
    if (ui::keyPressed(Key::Down)) {
      ui::consumeKey(Key::Down);
      if (n) hi_ = (hi_ + 1) % n;
      scrollToHi_ = true;
    }
    if (ui::keyPressed(Key::Up)) {
      ui::consumeKey(Key::Up);
      if (n) hi_ = (hi_ - 1 + n) % n;
      scrollToHi_ = true;
    }
    if (ui::keyPressed(Key::Enter)) {
      ui::consumeKey(Key::Enter);
      if (hi_ >= 0 && hi_ < n) {
        pick(a, rows_[size_t(shown_[size_t(hi_)])].id);
        return false;
      }
    }
    {
      ui::Row head({ui::px(30), ui::fr(1)}, 30, 8);
      if (ui::iconButton("arrow-left", "Назад")) page_ = 0;
      ui::tooltip("Назад", {Key::Escape, 0});
      a.markUi("role.back");
      if (!focused_) {
        ui::setKeyboardFocus(ui::id("q"));
        focused_ = true;
      }
      ui::searchField("q", q_, "Найти государство");
      a.markUi("role.search");
    }
    if (q_ != builtQ_) filter();
    n = int(shown_.size());
    ui::spacer(6);
    if (n == 0) {
      ui::spacer(10);
      ui::label("Ничего не найдено", {.ink = ui::Ink::Muted, .align = ui::Align::Center});
      ui::spacer(10);
      return true;
    }
    const float rowH = 52;
    const float h = std::min(float(n) * rowH, std::round(clamp(ui::viewport().h - 280, 200.f, 470.f)));
    const RectF top = ui::avail();
    bool chosen = false;
    {
      ui::VirtualList vl("states", n, rowH, h);
      if (scrollToHi_ && hi_ >= 0) vl.scrollToRow(hi_);
      scrollToHi_ = false;
      for (int k : vl) {
        const StateRow& s = rows_[size_t(shown_[size_t(k)])];
        RectF r = ui::next(rowH);
        // Граница групп: перед первым неосновным государством — тонкая линия.
        if (k > 0 && !s.main && rows_[size_t(shown_[size_t(k - 1)])].main) ui::draw::line(r.x + 14, r.y + 0.5f, r.right() - 14, r.y + 0.5f, th.border, 1);
        chosen = stateRow(a, s, r.inset(0, 2), k == hi_, mouseMoved, k) || chosen;
      }
    }
    a.markUi("role.list", RectF{top.x, top.y, top.w, h});
    return !chosen;
  }
};

// ================================================================ отметка очка действий войска
// Панель войска (под шапкой) и нижняя строка: «Может ходить» / «Ход сделан» у своих объектов игрока.
void actionTag(App& a, Id army, bool compact) {
  if (!a.player()) return;
  const World& w = a.world();
  const Army* ar = w.army(army);
  if (!ar || ar->leader() != a.ui.playerState) return;
  const bool can = rules::hasAction(w, army);
  const char* what = ar->isFleet() ? "Флот" : "Войско";
  const std::string tip = can ? std::string(what) + " может перейти в соседнюю провинцию в этом ходу"
                              : std::string(what) + " уже ходило — очки действий вернутся в следующем ходу";
  if (compact) {
    ui::iconColored(can ? "bolt" : "hourglass", ui::toneColor(can ? ui::Tone::Success : ui::Tone::Neutral), 16, tip);
    a.markUi("army.action.quick");
    return;
  }
  ui::HStack hs(24, ui::Align::Left, 6);
  ui::tag(can ? "Может ходить" : "Ход сделан", can ? ui::Tone::Success : ui::Tone::Neutral, can ? "bolt" : "hourglass");
  w::tagTip(tip);
  a.markUi("army.action");
}

void actionHeader(App& a, Id army) { actionTag(a, army, false); }
void actionQuick(App& a, Id army) { actionTag(a, army, true); }

HeaderReg actionHeaderReg({"army.action", SelType::Army, 50, actionHeader});
QuickReg actionQuickReg({"army.action", SelType::Army, 50, actionQuick});

}  // namespace

// ================================================================ выбор роли
std::vector<Id> playableStates(const World& w) {
  std::vector<const Faction*> list;
  w.factions.each([&](const Faction& f) {
    if (f.isState()) list.push_back(&f);
  });
  std::stable_sort(list.begin(), list.end(), [](const Faction* x, const Faction* y) {
    if (x->mainState != y->mainState) return x->mainState;
    const int c = compareRu(x->name, y->name);
    return c != 0 ? c < 0 : x->id < y->id;
  });
  std::vector<Id> out;
  out.reserve(list.size());
  for (const Faction* f : list) out.push_back(f->id);
  return out;
}

void ask(App& a) {
  if (a.ui.screen != Screen::Editor) return;
  a.openDialog(std::make_unique<RoleDialog>(0));
}

void askState(App& a) {
  if (a.ui.screen != Screen::Editor) return;
  if (playableStates(a.store.world()).empty()) {
    a.toast("В мире нет государств", ToastKind::Info, "crown");
    return;
  }
  a.openDialog(std::make_unique<RoleDialog>(1));
}

void becomeAdmin(App& a) {
  spendDone();
  a.ui.role = Role::Admin;
  a.ui.playerState = 0;
  remember(a, false, 0);
  syncMapView(a);   // вид карты — настройки мира
}

void becomePlayer(App& a, Id state) {
  if (a.ui.screen != Screen::Editor) return;
  if (!stateExists(a.store.world(), state)) {
    a.toast("Государство не найдено", ToastKind::Warning, "crown");
    return;
  }
  if (a.readOnly()) a.backToCurrent();
  App::Impl& d = a.impl();
  const bool was = a.player();
  spendDone();
  a.setEditBorders(false);
  a.setEditMap(false);
  a.ui.role = Role::Player;
  a.ui.playerState = state;
  // Отмена у игрока — только его действий: история правок администратора к нему не переходит.
  if (!was) {
    a.store.clearHistory();
    d.headUndo.clear();
    d.headRedo.clear();
  }
  if (!toolAllowed(a, a.ui.tool)) a.setTool(ToolId::Select);
  a.toMap();
  // Своё государство выделено (панель справа от карты), карта плавно показывает его столицу — когда известна раскладка
  // с панелью выделения (через кадр).
  a.ui.sel = {SelType::Faction, state};
  a.ui.page = false;
  a.ui.backSel = {};
  a.ui.hover = {};
  a.ui.inspectorHidden = false;
  if (const SectionDef* sec = a.sectionOf(a.ui.sel)) a.ui.lastOf[sec->id] = a.ui.sel;
  remember(a, true, state);
  syncMapView(a);   // вид карты — свои настройки игрока
  later(a, [state](App& x) { later(x, [state](App& y) { focusCapital(y, state); }); });
}

void worldOpened(App& a, bool keep) {
  spendDone();
  if (keep && a.player() && stateExists(a.store.world(), a.ui.playerState)) return;
  const bool wasAdmin = keep && a.admin();   // перечитанный мир у администратора — без пароля
  a.ui.role = Role::Admin;
  a.ui.playerState = 0;
  if (a.impl().cfg.askRole) {
    if (!wasAdmin) lockedStart(a);
    later(a, [](App& x) {
      if (x.ui.screen == Screen::Editor && !x.hasDialog("role")) ask(x);
    });
  }
}

void worldClosed(App& a) {
  spendDone();
  a.ui.role = Role::Admin;
  a.ui.playerState = 0;
}

void validate(App& a) {
  if (!a.player() || stateExists(a.store.world(), a.ui.playerState)) return;
  a.ui.role = Role::Admin;
  a.ui.playerState = 0;
  spendDone();
  lockedStart(a);   // пароль администратора: без него — снова игрок до выбора
  if ((a.player() || a.impl().cfg.askRole) && !a.hasDialog("role")) ask(a);
}

// ================================================================ ограничения игрока
bool sectionAllowed(const App& a, std::string_view section) {
  if (a.admin()) return true;
  const SectionDef* s = findSection(section);
  return !(s && s->adminOnly);   // справочники, решения администратора
}

bool editorAllowed(const App& a, std::string_view editor) {
  if (a.admin()) return true;
  const EditorDef* e = findEditor(editor);
  return !(e && e->group && !sectionAllowed(a, e->group));
}

bool toolAllowed(const App& a, ToolId t) { return a.admin() || t == ToolId::Select || t == ToolId::Pan; }

bool commandAllowed(const App& a, std::string_view command) {
  if (a.admin()) return true;
  static constexpr std::string_view kDeny[] = {"map.borders", "map.edit", "editor.catalogs", "editor.modifiers", "editor.constants"};
  for (std::string_view d : kDeny)
    if (command == d) return false;
  return true;
}

std::string toolBlocked(const App& a, ToolId t) {
  if (a.admin()) return {};
  switch (t) {
    case ToolId::Select:
    case ToolId::Pan:
    case ToolId::Landing:
    case ToolId::NewArmy:
    case ToolId::NewFleet: return {};
    default: return kAdminOnly;
  }
}

// ================================================================ войска и флот игрока
bool ownArmy(const App& a, Id army) {
  if (a.admin()) return true;
  const Army* ar = a.world().army(army);
  return ar && ar->leader() != 0 && ar->leader() == a.ui.playerState;
}

std::vector<Id> spentArmies(const App& a) {
  std::vector<Id> out;
  if (!a.player()) return out;
  const World& w = a.world();
  const int turn = w.turn();
  w.armies.each([&](const Army& ar) {
    if (ar.leader() == a.ui.playerState && ar.acted == turn) out.push_back(ar.id);
  });
  std::sort(out.begin(), out.end());
  return out;
}

void spendPending(std::vector<Id> armies) { pendingSpend() = std::move(armies); }
void spendDone() { pendingSpend().clear(); }

void decorate(App& a, Tx& tx) {
  const std::vector<Id>& s = pendingSpend();
  if (s.empty() || !a.player() || !(tx.touched() & TB_ARMIES)) return;
  for (Id id : s)
    if (tx.w().army(id)) rules::spendAction(tx, id);
}

// ================================================================ верхняя строка
void topBadge(App& a) {
  if (!a.player()) return;
  const Faction* f = a.world().faction(a.ui.playerState);
  if (!f) return;
  App::Impl& d = a.impl();
  const ui::Theme& th = ui::theme();
  const float s = ui::uiScale();
  auto mark = [&](const char* n) -> std::optional<RectF> {
    auto it = d.rectsNext.find(n);
    if (it == d.rectsNext.end()) return std::nullopt;
    const RectF r = it->second;
    return RectF{r.x / s, r.y / s, r.w / s, r.h / s};
  };
  // Свободная середина верхней строки: на карте — место параметров инструмента, на странице — от заголовка страницы до
  // плеера и поиска.
  const bool onMap = a.view() == View::Map;
  float left = 0, right = 0;
  if (onMap) {
    if (d.toolSlot.w <= 0) return;
    left = d.toolSlot.x;
    right = d.toolSlot.right();
  } else {
    const std::optional<RectF> wr = mark("topbar.world");
    if (!wr) return;
    left = wr->right() + 2 + 17 + 2 + std::ceil(ui::measure(shell::pageTitle(a), ui::Font::Strong)) + 16;
    right = ui::viewport().w;
    for (const char* n : {"topbar.player", "topbar.palette"})
      if (const std::optional<RectF> r = mark(n)) right = std::min(right, r->x - 14);
  }
  const std::string name = w::orName(f->name);
  const float nameW = std::min(std::ceil(ui::measure(name, ui::Font::Strong)) + 2, 200.f);
  const float full = 10 + 24 + 9 + nameW + 12;
  const float room = right - left;
  if (room < 10 + 24 + 10) return;
  const float bw = std::floor(std::min(full, room));
  const float h = shell::kTopH;
  const RectF r{std::round(left), std::round((h - 30) * 0.5f), bw, 30};
  {
    ui::Panel p("##role.badge", r, ui::PanelOpt{.pad = 0, .shadow = false, .border = false, .radius = 0});
    ui::WidgetId wid = ui::id("##role.badge.btn");
    ui::Interaction it = ui::interact(wid, r, ui::IfFocusable);
    const float hv = ui::animate(wid ^ 1, it.hovered ? 1.f : 0.f, 0.12f);
    ui::draw::rect(r, Color::mix(f->color.alpha(0.12f), th.hover, hv), th.radiusField);
    ui::draw::rectStroke(r, Color::mix(f->color.alpha(0.45f), th.borderStrong, hv), th.radiusField, 1);
    ui::at(RectF{r.x + 10, std::round(r.cy() - 8), 24, 16});
    ui::flag(f->flag, 24, 16, 2.5f);
    if (bw >= 10 + 24 + 9 + 24) ui::draw::text(name, RectF{r.x + 43, r.y, bw - 43 - 10, r.h}, ui::Font::Strong, hv > 0.5f ? th.accentHover : th.text);
    if (it.focused) ui::draw::rectStroke(r.expand(2), th.accent, th.radiusField + 2, 2);
    ui::hoverTip("role.badge", r, "Игрок: " + name + " — страница государства");
    if (it.clicked || it.keyActivated) {
      const Id st = f->id;
      later(a, [st](App& x) { x.select(SelType::Faction, st); });
    }
  }
  a.markUi("topbar.role", r);
  if (onMap) {
    const float old = d.toolSlot.right();
    d.toolSlot.x = r.right() + 8;
    d.toolSlot.w = std::max(0.f, old - d.toolSlot.x);
  }
}

// ================================================================ пароль администратора
std::string adminKeyOf(const World& w, std::string_view password) {
  const std::string salt = w.meta->createdAt.empty() ? w.meta->name : w.meta->createdAt;
  const std::string text = salt + std::string(password);
  return codec::sha256Hex(text.data(), text.size());
}

bool adminLocked(const World& w) { return !w.settings->adminKey.empty(); }

bool adminPasswordOk(const World& w, std::string_view password) {
  return !adminLocked(w) || adminKeyOf(w, password) == w.settings->adminKey;
}

void passwordField(std::string_view id, std::string& value, std::string_view placeholder, bool autofocus) {
  ui::textField(id, value, {.placeholder = placeholder, .icon = "lock", .live = true, .maxLength = 128, .autofocus = autofocus});
  if (value.empty()) return;
  // Введённое закрывается точками: подложка поля поверх текста и по точке на знак (ввод, выделение и вставка — у поля).
  const ui::Item it = ui::lastItem();
  const ui::Theme& th = ui::theme();
  const RectF tr{it.rect.x + 34, it.rect.y + 2, std::max(0.f, it.rect.w - 44), it.rect.h - 4};
  ui::draw::rect(tr, th.surface3, 0);
  const size_t n = std::min<size_t>(utf8Count(value), 40);
  float x = tr.x;
  for (size_t i = 0; i < n && x + 6 <= tr.right(); i++, x += 9) ui::draw::circle(x + 3, tr.cy(), 3, th.text);
  if (it.focused) ui::draw::line(x + 1, tr.cy() - 7, x + 1, tr.cy() + 7, th.accent, 1);
}

void askSetPassword(App& a) {
  if (!a.canEdit()) {
    a.toast(kAdminOnly, ToastKind::Warning, "lock");
    return;
  }
  a.openDialog(std::make_unique<PasswordDlg>());
}

void askClearPassword(App& a) {
  if (!a.canEdit()) {
    a.toast(kAdminOnly, ToastKind::Warning, "lock");
    return;
  }
  a.confirmAct("Снять пароль администратора?", "Роль администратора станет доступна без пароля.", "Снять пароль", "Снять пароль администратора",
               [](Tx& tx) { tx.settings().adminKey.clear(); });
}

// ================================================================ вид карты игрока
namespace {
// Поля вида карты из src в dst (правила и сохранение — мира).
void copyView(Settings& dst, const Settings& src) {
  dst.fillOpacity = src.fillOpacity;
  dst.labelStates = src.labelStates;
  dst.labelProvinces = src.labelProvinces;
  dst.labelArmies = src.labelArmies;
  dst.showArmies = src.showArmies;
  dst.figureSize = src.figureSize;
}
}  // namespace

Settings viewSettings(const App& a) {
  Settings s = *a.world().settings;
  if (a.player() && a.ui.playerView) copyView(s, *a.ui.playerView);
  return s;
}

void setView(App& a, std::string_view label, const std::function<void(Settings&)>& fn, std::string_view coalesce) {
  if (a.admin()) {
    a.play(label, [&](Tx& tx) { fn(tx.settings()); }, {.coalesce = std::string(coalesce)});
    return;
  }
  Settings s = viewSettings(a);
  fn(s);
  s.fillOpacity = clamp(s.fillOpacity, 0.f, 1.f);
  s.figureSize = clamp(s.figureSize, schema::kFigureSizeMin, schema::kFigureSizeMax);
  a.ui.playerView = s;
  a.impl().prefsDirty = true;
  syncMapView(a);
}

World mapWorld(const App& a, const World& w) {
  if (!a.player() || !a.ui.playerView) return w;
  World m = w;
  Settings s = *w.settings;
  copyView(s, *a.ui.playerView);
  m.settings = std::make_shared<const Settings>(std::move(s));
  return m;
}

void loadView(App& a, const json::Value& v) {
  if (!v.isObj()) return;
  Settings s;
  s.fillOpacity = float(clamp(v.num("fillOpacity", s.fillOpacity), 0.0, 1.0));
  s.labelStates = v.boolean("labelStates", s.labelStates);
  s.labelProvinces = v.boolean("labelProvinces", s.labelProvinces);
  s.labelArmies = v.boolean("labelArmies", s.labelArmies);
  s.showArmies = v.boolean("showArmies", s.showArmies);
  s.figureSize = int(clamp(v.num("figureSize", s.figureSize), double(schema::kFigureSizeMin), double(schema::kFigureSizeMax)));
  a.ui.playerView = s;
}

json::Value viewJson(const App& a) {
  if (!a.ui.playerView) return json::Value();
  const Settings& s = *a.ui.playerView;
  json::Value o = json::Value::object();
  o.set("fillOpacity", double(s.fillOpacity));
  o.set("labelStates", s.labelStates);
  o.set("labelProvinces", s.labelProvinces);
  o.set("labelArmies", s.labelArmies);
  o.set("showArmies", s.showArmies);
  o.set("figureSize", double(s.figureSize));
  return o;
}

}  // namespace rg::app::detail::role
