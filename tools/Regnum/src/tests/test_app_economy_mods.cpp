// Сценарии модификаторов провинции (ТЗ «Модификаторы», п.1.1–1.3, 1.11): «Столица государства» — автоматически,
// только чтение с причиной (+3 слота); список — общий список модификаторов через правило: встроенный модификатор без
// записи мира создаётся при выборе, срок по умолчанию модификатора, оставшиеся ходы на фишке и их правка (0 —
// бессрочно), ход уменьшает срок, удаление крестиком; источники эффектов — с автоматическими модификаторами.
#include "tests/test_app_economy_util.h"

using namespace rg;
using namespace rg::econtest;

namespace {

int chipIndex(const Province& p, Id mid) {
  for (size_t i = 0; i < p.modifiers.size(); i++)
    if (p.modifiers[i] == mid) return int(i);
  return -1;
}

// Срок модификатора через фишку: щелчок — всплывающее поле срока, ввод, Enter, Esc.
bool setTerm(Harness& h, Id pid, Id mid, const std::string& value) {
  int idx = chipIndex(*prov(h, pid), mid);
  if (idx < 0) return false;
  if (!clickIn(h, "mods.chip." + std::to_string(idx))) return false;
  h.settle();
  if (!h.clickUi("mods.term")) return false;
  h.retype(value);
  h.key(Key::Enter);
  h.settle();
  h.key(Key::Escape);
  h.settle();
  return true;
}

}  // namespace

TEST(app_economy_province_modifiers) {
  HideTestRegs hide;
  Harness h("economy_province_mods", 1440, 1100);
  h.demo();
  const Id st = biggestState(h->world());
  const Id cap = h->world().faction(st)->capital;
  CHECK(cap != 0);
  CHECK_EQ(rules::builtinModId(h->world(), schema::mod::Plundered), Id(0));
  openProvince(h, cap, "province.modifiers");
  // Столица: автоматический модификатор, только чтение; +3 слота — в расчёте и источниках.
  CHECK(h->uiRect("province.autoMods") != nullptr);
  CHECK(h->uiRect("province.autoMod." + std::string(schema::mod::Capital)) != nullptr);
  auto c = rules::calc(h->world());
  bool autoSource = false;
  for (const rules::EffectSource& s : c->province(cap)->fx.sources) autoSource = autoSource || (s.kind == rules::EffectSource::Auto && s.key == schema::mod::Capital);
  CHECK(autoSource);
  CHECK_EQ(c->province(cap)->slotsMods, 3);
  CHECK(!h->act("Столица", [&](Tx& tx) {
    rules::addModifier(tx, rules::ModTarget::Province, cap, rules::ensureBuiltinMod(tx, schema::mod::Capital));
  }));
  h->toasts().clear();
  // Добавить «Разграбленную провинцию» (записи в мире ещё нет — создаётся): срок по умолчанию — 5 ходов.
  CHECK(pickInCombo(h, "province.modAdd", "Разграбл"));
  h.settle();
  const Id plundered = rules::builtinModId(h->world(), schema::mod::Plundered);
  CHECK(plundered != 0);
  CHECK(chipIndex(*prov(h, cap), plundered) >= 0);
  CHECK_EQ(prov(h, cap)->modTurns.count(plundered), size_t(1));
  CHECK_EQ(prov(h, cap)->modTurns.at(plundered), schema::kCaptureModTurns);
  CHECK_EQ(h->store.undoLabel(), std::string("Модификаторы провинции"));
  // Оставшиеся ходы — правка на фишке; 0 — бессрочно.
  CHECK(setTerm(h, cap, plundered, "3"));
  CHECK_EQ(prov(h, cap)->modTurns.at(plundered), 3);
  CHECK(ensureVisible(h, "mods.chip." + std::to_string(chipIndex(*prov(h, cap), plundered))));
  shotClean(h, "economy_province_mods");
  CHECK(setTerm(h, cap, plundered, "0"));
  CHECK_EQ(prov(h, cap)->modTurns.count(plundered), size_t(0));
  CHECK(setTerm(h, cap, plundered, "5"));
  CHECK_EQ(prov(h, cap)->modTurns.at(plundered), 5);
  // Ход уменьшает срок.
  CHECK(h->endTurnNow());
  h->toasts().clear();
  CHECK_EQ(prov(h, cap)->modTurns.at(plundered), 4);
  // Убрать крестиком на фишке и отменить.
  h.settle();
  {
    const std::string chip = "mods.chip." + std::to_string(chipIndex(*prov(h, cap), plundered));
    CHECK(ensureVisible(h, chip));
    const RectF* r = h->uiRect(chip);
    CHECK(r != nullptr);
    if (r) {
      RectF rr = *r;
      h.click(rr.right() - 14, rr.cy());
    }
  }
  CHECK(chipIndex(*prov(h, cap), plundered) < 0);
  CHECK_EQ(prov(h, cap)->modTurns.count(plundered), size_t(0));
  h.key(Key::Z, ctrl());
  CHECK(chipIndex(*prov(h, cap), plundered) >= 0);
  // Источники эффектов показываются (с автоматическими).
  h.settle();
  CHECK(h->uiRect("province.effects") != nullptr);
}
