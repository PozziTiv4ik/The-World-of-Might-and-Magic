// Regnum — дипломатия: симметричные отношения пары фракций (ТЗ 1.b.iv, 1.b.vi).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

void setRelation(Tx& tx, Id a, Id b, double value, RelStatus status) {
  const Faction& fa = needFaction(tx.w(), a);
  const Faction& fb = needFaction(tx.w(), b);
  if (a == b) fail("Отношения фракции с самой собой не задаются");
  needFinite(value, "Отношения");
  if (int(status) < 0 || int(status) > 3) fail("Неизвестное состояние отношений");
  Relation old = tx.w().relation(a, b);
  Relation nr{clamp(value, -100.0, 100.0), status};
  if (old == nr) return;
  // ТЗ «Механика войн», п.2: войну государств завершает только перемирие (rules::concludeTruce — только между
  // государствами); война с участием гильдии меняется как прежде.
  if (old.s == RelStatus::War && status != RelStatus::War && fa.isState() && fb.isState())
    fail("Войну " + facName(tx.w(), a) + " и " + facName(tx.w(), b) + " можно завершить только перемирием");
  tx.setRelation(a, b, nr);
  if (old.s != status)
    addLog(tx, status == RelStatus::War ? LogKind::War : LogKind::Diplomacy,
           "Отношения " + facName(tx.w(), a) + " и " + facName(tx.w(), b) + ": " + utf8::lower(schema::relStatus(status).name),
           LogRefs{0, 0, {a, b}});
  // Союзное войско существует только между союзниками (ТЗ 1.c.iv): конец союза распускает общие объекты.
  if (status != RelStatus::Alliance) splitBrokenAlliances(tx, a, b);
}

void shiftRelation(Tx& tx, Id a, Id b, double delta) {
  if (!a || !b || a == b || !tx.w().faction(a) || !tx.w().faction(b) || delta == 0) return;
  Relation r = tx.w().relation(a, b);
  r.v = clamp(r.v + delta, -100.0, 100.0);
  tx.setRelation(a, b, r);
}

std::vector<RelationRow> relationsOf(const World& w, Id faction) {
  std::vector<RelationRow> rows;
  if (!w.faction(faction)) return rows;
  std::vector<const Faction*> others;
  w.factions.each([&](const Faction& f) { if (f.id != faction) others.push_back(&f); });
  std::stable_sort(others.begin(), others.end(), [](const Faction* x, const Faction* y) {
    if (x->kind != y->kind) return x->kind == FactionKind::State;  // сначала государства
    int c = compareRu(x->name, y->name);
    if (c != 0) return c < 0;
    return x->id < y->id;
  });
  rows.reserve(others.size());
  for (const Faction* f : others) {
    Relation r = w.relation(faction, f->id);
    rows.push_back(RelationRow{f->id, r.v, r.s});
  }
  return rows;
}

}  // namespace rg::rules
