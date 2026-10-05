// Regnum — судьба героев (ТЗ «Механика героев»): сбежал, убит (место захоронения, воскрешение), взят в плен
// (список пленников государства, обмен в переговорах и торговле).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

Id modOf(const World& w, Id character, std::string_view key) {
  const Character* c = w.character(character);
  if (!c) return 0;
  for (Id m : c->modifiers)
    if (const Modifier* x = w.modifier(m); x && x->key == key) return m;
  return 0;
}

}  // namespace

void heroFate(Tx& tx, Id character, Fate fate, Id captor, Id burial) {
  const Character& c = needCharacter(tx.w(), character);
  const std::string name = q(tx.w().characterName(character));
  const Id own = c.faction;
  switch (fate) {
    case Fate::Fled:
      addLog(tx, LogKind::Battle, name + " спасся бегством", LogRefs{0, 0, own ? std::vector<Id>{own} : std::vector<Id>{}});
      return;
    case Fate::Killed: {
      if (burial) needProvince(tx.w(), burial);
      addModifier(tx, ModTarget::Character, character, ensureBuiltinMod(tx, schema::mod::Dead), 0);
      tx.character(character).burial = burial;
      addLog(tx, LogKind::Battle, name + " погиб" + (burial ? ", место захоронения — " + provName(tx.w(), burial) : std::string()),
             LogRefs{burial, 0, own ? std::vector<Id>{own} : std::vector<Id>{}});
      return;
    }
    case Fate::Captured: {
      needState(tx.w(), captor);
      if (captor == own) fail("Герой не может попасть в плен к собственному государству");
      addModifier(tx, ModTarget::Character, character, ensureBuiltinMod(tx, schema::mod::Captive), 0);
      tx.character(character).captor = captor;
      LogRefs refs{0, 0, {captor}};
      if (own) refs.factions.push_back(own);
      addLog(tx, LogKind::Battle, name + " взят в плен: " + facName(tx.w(), captor), refs);
      return;
    }
  }
}

void resurrect(Tx& tx, Id character, std::string_view natureKey, Id faction) {
  const Character& c = needCharacter(tx.w(), character);
  const Id dead = modOf(tx.w(), character, schema::mod::Dead);
  if (!dead) fail(q(tx.w().characterName(character)) + " жив");
  if (!schema::isNatureKey(natureKey)) fail("Выберите способ воскрешения");
  if (faction) needState(tx.w(), faction);
  const Id was = c.faction;
  dropModifier(tx, ModTarget::Character, character, dead);
  addModifier(tx, ModTarget::Character, character, ensureBuiltinMod(tx, natureKey), 0);   // прежняя природа заменяется
  if (faction && faction != was) {
    Character& m = tx.character(character);
    m.faction = faction;
    m.captor = 0;
  }
  const Modifier* nm = builtinMod(tx.w(), natureKey);
  LogRefs refs{0, 0, {}};
  if (faction) refs.factions.push_back(faction);
  if (was && was != faction) refs.factions.push_back(was);
  addLog(tx, LogKind::Note,
         q(tx.w().characterName(character)) + " воскрешён: " + utf8::lower(nm ? nm->name : std::string(natureKey)) +
             (faction ? ", герой " + facName(tx.w(), faction) : std::string()),
         refs);
}

std::vector<Id> captivesOf(const World& w, Id state) {
  std::vector<Id> out;
  w.characters.each([&](const Character& c) {
    if (c.captor == state && hasModKey(w, c.modifiers, schema::mod::Captive)) out.push_back(c.id);
  });
  return out;
}

}  // namespace rg::rules
