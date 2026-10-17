// Сценарии встречи войск и битвы: диалог встречи (объявление войны, затем вассалитет), союзное войско (распадается при
// войне между участниками, текст диалога точен, когда отряды складываются с плиткой своей фракции), панель битвы
// («Отступить» с введёнными потерями переспрашивает, победителем нельзя выбрать сторону без отрядов).
#include "tests/test_app_war_util.h"

using namespace rg;
using namespace rg::apptest;

namespace {

// Ввести потери в поле отряда панели битвы.
bool enterLoss(Harness& h, Id army, Id row, i64 n) {
  if (!h.clickUi("battle.loss." + std::to_string(army) + "." + std::to_string(row))) return false;
  h.retype(std::to_string(n));
  h.key(Key::Enter);
  h.settle();
  return true;
}

}  // namespace

TEST(app_war_battle_declare_war_then_vassal) {
  Harness h("war_declare_vassal", 1440, 1000);
  h.demo();
  Id alm = factionByName(h->world(), "Альмарин");
  Id vk = factionByName(h->world(), "Валь-Кетра");
  Id mir = factionByName(h->world(), "Мирель");
  CHECK(alm && vk && mir);
  CHECK(h->world().relation(alm, vk).s == RelStatus::Neutral);
  CHECK(h->act("Вассал", [&](Tx& tx) { rules::setSuzerain(tx, vk, mir); }));
  const Id v = armyOf(h->world(), vk, ArmyKind::Army);
  CHECK(v != 0);
  if (!v) return;
  const Vec2 vpos = h->world().army(v)->pos;
  const Id z = spawnArmy(h, alm, vpos + Vec2(-190, 30), 300);
  CHECK(z != 0);
  if (!z) return;
  const Vec2 zpos = h->world().army(z)->pos;
  h->select(app::SelType::Army, z);
  h.settle();
  showAt(h, (vpos + zpos) * 0.5, 0.6);
  h.dropToasts();
  dragTo(h, screenOf(h, zpos), screenOf(h, vpos));
  h.settle();
  CHECK(h->hasDialog("army.encounter"));
  CHECK(h.clickUi("encounter.ok"));
  h.settle();
  CHECK(h->world().relation(alm, vk).s == RelStatus::War);
  CHECK(h->hasDialog("battle"));
  CHECK(!h->hasDialog("vassal.defend"));   // сначала битва
  CHECK(h.clickUi("battle.retreat"));
  h.settle();
  CHECK(!h->hasDialog("battle"));
  CHECK(h->hasDialog("vassal.defend"));   // затем решение сюзерена атакованного вассала
  h.dropToasts();
  h.settle();
  CHECK(h.shot("war_declare_vassal"));
}

// ТЗ 1.c.iv: союзное войско существует только между союзниками (регрессия аудита). Войско фракции на союзном объекте
// со своей группой складывается в неё (не битва); при объявлении войны между участниками союзное войско распускается
// на отдельные объекты; битва, где фракция есть в обоих объектах, правилами не принимается.
TEST(app_war_battle_allied_army_split_on_war) {
  Harness h("audit_tzmil_allied_war");
  h.demo();
  const World& w0 = h->world();
  Id alm = factionByName(w0, "Альмарин");
  Id hel = factionByName(w0, "Хельдвиг");
  Id allied = alliedArmy(w0, ArmyKind::Army);
  CHECK(alm && hel && allied);
  CHECK(w0.relation(alm, hel).s == RelStatus::Alliance);
  Id x = armyOf(w0, hel, ArmyKind::Army);
  CHECK(x != 0);
  // Пока союз: войско Хельдвига на союзном войске со своей группой — объединение в свою группу.
  CHECK(rules::encounter(h->world(), x, h->world().army(allied)->pos).type == rules::EncounterType::Merge);
  // Повреждённые данные (война внутри союзного объекта, как из файла): встреча — не битва, итог битвы не принимается.
  CHECK(h->act("Сырые отношения", [&](Tx& tx) { tx.setRelation(alm, hel, Relation{-60, RelStatus::War}); }));
  rules::Encounter e = rules::encounter(h->world(), x, h->world().army(allied)->pos);
  CHECK_MSG(e.type != rules::EncounterType::Battle, "войско Хельдвига вступает в битву с объектом, где стоят отряды самого Хельдвига");
  const ArmyGroup* helGroup = nullptr;
  for (const ArmyGroup& g : h->world().army(allied)->groups)
    if (g.faction == hel) helGroup = &g;
  CHECK(helGroup && !helGroup->units.empty());
  rules::BattleResult r;
  r.attacker = x;
  r.defender = allied;
  r.attackerWins = true;
  r.attackerOrigin = h->world().army(x)->pos;
  if (helGroup) r.losses[allied][{hel, helGroup->units[0].row}] = 100;
  bool applied = h->act("Битва", [&](Tx& tx) { rules::resolveBattle(tx, r); });
  CHECK_MSG(!applied, "битва, где Хельдвиг теряет отряды в собственной группе союзного войска, принята правилами");
  h->undo();
  h.settle();
  CHECK(h->world().relation(alm, hel).s == RelStatus::Alliance);
  // Пользователь меняет состояние отношений во вкладке «Дипломатия» на «в войне»: союзное войско распускается.
  const u32 armies0 = h->world().armies.size();
  CHECK(h->act("Отношения", [&](Tx& tx) { rules::setRelation(tx, alm, hel, -60, RelStatus::War); }));
  const Army* al = h->world().army(allied);
  CHECK(al != nullptr);
  CHECK_MSG(!al->allied(), "союзное войско Альмарина и Хельдвига осталось одним объектом после объявления войны между ними");
  CHECK_EQ(h->world().armies.size(), armies0 + 1);
  // Теперь это обычная битва Хельдвига с войском Альмарина.
  CHECK(rules::encounter(h->world(), x, al->pos).type == rules::EncounterType::Battle);
  h.settle();
  app::mil::openBattle(h.a(), x, allied, h->world().army(x)->pos);
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tzmil_battle_vs_own_group"));
}

// «Отступить» с введёнными потерями: заметка над кнопками и вопрос; «Отмена» — панель битвы остаётся,
// «Отступить без потерь» — панель закрыта, численность не изменилась.
TEST(app_war_battle_retreat_warns_about_losses) {
  Harness h("fix_ui_battle_retreat", 1440, 1000);
  h.demo();
  Id hel = factionByName(h->world(), "Хельдвиг");
  Id vk = factionByName(h->world(), "Валь-Кетра");
  Id x = armyOf(h->world(), hel, ArmyKind::Army);
  Id y = armyOf(h->world(), vk, ArmyKind::Army);
  CHECK(x && y);
  if (!x || !y) return;
  Id row = h->world().army(x)->groups[0].units[0].row;
  const i64 before = h->world().army(x)->groups[0].units[0].count;
  CHECK(before > 1);
  app::mil::openBattle(h.a(), x, y, h->world().army(x)->pos);
  h.settle();
  CHECK(h->hasDialog("battle"));
  // Без потерь — заметки нет.
  CHECK(h->uiRect("battle.retreatNote") == nullptr);
  CHECK(enterLoss(h, x, row, 1));
  CHECK(h->uiRect("battle.retreatNote") != nullptr);
  h.dropToasts();
  h.settle();
  CHECK(h.shot("fix_ui_battle_retreat_note"));
  // Вопрос; «Отмена» — назад к панели битвы, потери сохранены.
  CHECK(h.clickUi("battle.retreat"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h->hasDialog("battle"));
  CHECK(h.clickUi("dialog.cancel"));
  h.settle();
  CHECK(!h->hasDialog("confirm"));
  CHECK(h->hasDialog("battle"));
  CHECK(h->uiRect("battle.retreatNote") != nullptr);
  // Подтверждение — отступление без потерь.
  CHECK(h.clickUi("battle.retreat"));
  h.settle();
  CHECK(h->hasDialog("confirm"));
  CHECK(h.clickUi("dialog.ok"));
  h.settle();
  CHECK(!h->hasDialog("battle"));
  CHECK(!h->hasDialog("confirm"));
  CHECK(h->world().army(x) != nullptr);
  CHECK_EQ(h->world().army(x)->groups[0].units[0].count, before);
}

// ТЗ 1.c.iv: «Отступить» и изменение численности по потерям — обе возможности панели битвы. Введённые потери при
// отступлении молча отбрасываются.
TEST(app_war_battle_retreat_with_losses_asks) {
  Harness h("audit_tzmil_retreat");
  h.demo();
  Id hel = factionByName(h->world(), "Хельдвиг");
  Id vk = factionByName(h->world(), "Валь-Кетра");
  Id x = armyOf(h->world(), hel, ArmyKind::Army);
  Id y = armyOf(h->world(), vk, ArmyKind::Army);
  CHECK(x && y);
  Id row = h->world().army(x)->groups[0].units[0].row;
  i64 before = h->world().army(x)->groups[0].units[0].count;
  app::mil::openBattle(h.a(), x, y, h->world().army(x)->pos);
  h.settle();
  CHECK(h->hasDialog("battle"));
  CHECK(h.clickUi("battle.loss." + std::to_string(x) + "." + std::to_string(row)));
  h.retype("250");
  h.key(Key::Enter);
  h.settle();
  CHECK(h.shot("audit_tzmil_retreat_with_losses"));
  CHECK(h.clickUi("battle.retreat"));
  h.settle();
  i64 after = h->world().army(x)->groups[0].units[0].count;
  std::printf("  [audit tzmil] потери 250 введены, «Отступить»: было %lld, стало %lld\n", (long long)before, (long long)after);
  // «Отступить» с введёнными потерями переспрашивает (подробно — app_war_battle_retreat_warns_about_losses).
  CHECK_MSG(h->hasDialog("confirm"), "введённые в панели битвы потери отброшены при отступлении без предупреждения");
  CHECK_EQ(after, before);
}

// Нападающий теряет все отряды: победителем его не выбрать (щелчок по его половине ничего не меняет), итог —
// победа защитника, нападающий исчезает.
TEST(app_war_battle_winner_needs_units) {
  Harness h("fix_ui_battle_winner", 1440, 1000);
  h.demo();
  Id hel = factionByName(h->world(), "Хельдвиг");
  Id vk = factionByName(h->world(), "Валь-Кетра");
  Id x = armyOf(h->world(), hel, ArmyKind::Army);
  Id y = armyOf(h->world(), vk, ArmyKind::Army);
  CHECK(x && y);
  if (!x || !y) return;
  CHECK_EQ(h->world().army(x)->groups.size(), size_t(1));
  std::vector<std::pair<Id, i64>> rows;
  for (const ArmyUnit& u : h->world().army(x)->groups[0].units) rows.push_back({u.row, u.count});
  app::mil::openBattle(h.a(), x, y, h->world().army(x)->pos);
  h.settle();
  CHECK(h->hasDialog("battle"));
  for (auto& [row, n] : rows) CHECK(enterLoss(h, x, row, n));
  CHECK(h.clickUi("battle.winner.attacker"));
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("fix_ui_battle_winner_no_units"));
  CHECK(h.clickUi("battle.apply"));
  h.settle();
  CHECK(!h->hasDialog("battle"));
  CHECK(h->world().army(x) == nullptr);
  CHECK(h->world().army(y) != nullptr);
}

// ТЗ 1.c.iv: в панели битвы можно выбрать победителем сторону, у которой после потерь не осталось отрядов.
// Тогда «победитель» исчезает, проигравший остаётся на месте, а хроника пишет «Победа: …» уничтоженному.
TEST(app_war_battle_destroyed_winner_rejected) {
  Harness h("audit_tzmil_battle_winner");
  h.demo();
  const World& w = h->world();
  Id hel = factionByName(w, "Хельдвиг");
  Id vk = factionByName(w, "Валь-Кетра");
  CHECK(w.relation(hel, vk).s == RelStatus::War);
  Id x = armyOf(w, hel, ArmyKind::Army);
  Id y = armyOf(w, vk, ArmyKind::Army);
  CHECK(x && y);
  Vec2 ypos = w.army(y)->pos;
  rules::BattleResult r;
  r.attacker = x;
  r.defender = y;
  r.attackerWins = true;
  r.attackerOrigin = w.army(x)->pos;
  for (const ArmyGroup& g : w.army(x)->groups)
    for (const ArmyUnit& u : g.units) r.losses[x][{g.faction, u.row}] = u.count;   // нападающий теряет всё
  bool ok = h->act("Битва", [&](Tx& tx) { rules::resolveBattle(tx, r); });
  CHECK_MSG(!ok, "победителем принята сторона без отрядов");
  CHECK(h->world().army(x) != nullptr);                                        // мир не изменился
  CHECK(h->world().army(y) && dist(h->world().army(y)->pos, ypos) < 1e-6);
  bool toasted = false;
  for (const auto& t : h->toasts()) toasted = toasted || t.text.find("не осталось отрядов") != std::string::npos;
  CHECK_MSG(toasted, "отказ без понятной причины");
}

// Союз: войско фракции, уже представленной в союзном войске, складывается с её плиткой — текст так и говорит;
// новый союз из двух объектов разных фракций — «не сложатся».
TEST(app_war_battle_alliance_text) {
  Harness h("fix_ui_alliance_text");
  h.demo();
  const World& w = h->world();
  Id allied = 0;
  w.armies.each([&](const Army& a) {
    if (!allied && a.allied() && a.kind == ArmyKind::Army) allied = a.id;
  });
  CHECK(allied != 0);
  if (!allied) return;
  // Объект фракции из союзного войска.
  Id member = 0;
  for (const ArmyGroup& g : w.army(allied)->groups)
    if (!member) member = armyOf(w, g.faction, ArmyKind::Army);
  CHECK(member != 0);
  if (member) {
    std::string t = app::mil::allianceText(w, *w.army(member), *w.army(allied));
    std::printf("  join: %s\n", t.c_str());
    CHECK(t.find("сложатся с её плиткой") != std::string::npos);
    CHECK(t.find("не сложатся") == std::string::npos);
  }
  // Новый союз: два несоюзных объекта союзных фракций.
  Id m = 0, tg = 0;
  w.armies.each([&](const Army& a) {
    if (m || a.allied()) return;
    w.armies.each([&](const Army& b) {
      if (m || b.allied() || b.id == a.id || b.kind != a.kind) return;
      if (rules::encounter(w, a.id, b.pos).type == rules::EncounterType::Alliance) {
        m = a.id;
        tg = b.id;
      }
    });
  });
  if (m) {
    std::string t = app::mil::allianceText(w, *w.army(m), *w.army(tg));
    std::printf("  new: %s\n", t.c_str());
    CHECK(t.find("не сложатся") != std::string::npos);
  }
}

// ТЗ 1.c.iv: войско фракции A, перенесённое на союзное войско {A, B}, получает диалог «Создать союзное войско?»
// с текстом «Отряды фракций не сложатся», хотя отряды A складываются с группой A, а союзное войско уже есть.
TEST(app_war_battle_join_allied_text) {
  Harness h("audit_tzmil_join_allied");
  h.demo();
  const World& w = h->world();
  Id hel = factionByName(w, "Хельдвиг");
  Id allied = alliedArmy(w, ArmyKind::Army);
  Id x = armyOf(w, hel, ArmyKind::Army);
  CHECK(allied && x);
  rules::Encounter e = rules::encounter(w, x, w.army(allied)->pos);
  std::printf("  [audit tzmil] встреча Хельдвиг -> союзное войско с группой Хельдвига: тип %d (3 = союз)\n", int(e.type));
  app::mil::openEncounter(h.a(), x, allied, e, w.army(x)->pos, [](app::App&, bool) {});
  h.settle();
  h.dropToasts();
  h.settle();
  CHECK(h.shot("audit_tzmil_join_allied_dialog"));
  i64 groupsBefore = i64(w.army(allied)->groups.size());
  CHECK(h.clickUi("encounter.ok"));
  h.settle();
  const Army* m = h->world().army(allied);
  CHECK(m != nullptr);
  std::printf("  [audit tzmil] групп было %lld, стало %zu (отряды Хельдвига сложились в его плитку)\n", (long long)groupsBefore, m->groups.size());
  CHECK_MSG(e.type != rules::EncounterType::Alliance || i64(m->groups.size()) != groupsBefore,
               "диалог «Создать союзное войско» обещает раздельные плитки, а отряды складываются в существующую группу");
}
