// Regnum — базовые записи мира, версия 5: рецепт «Обсидиановая сталь» (обсидиан и уголь) в «Сталелитейной» — без него
// сталь, нужную «Драконам Бездны», «Безликим Кукловодам» и «Невидимой библиотеке», ничто не производило.
#include "core/content_seed.h"
#include "core/schema.h"

namespace rg::content::detail {

void Seeder::v5() {
  const Id steel = resId("Обсидиановая сталь"), obsidian = resId("Обсидиан"), coal = resId("Уголь");
  const Id works = tx.w().buildings.firstId([&](const Building& b) { return b.owner == 0 && b.key == schema::bld::Steelworks; });
  if (!steel || !obsidian || !coal || !works) return;
  for (const Recipe& r : tx.w().building(works)->recipes)
    for (const ResAmount& o : r.out)
      if (o.res == steel) return;   // своя запись мира уже производит сталь
  Recipe r;
  r.name = "Обсидиановая сталь";
  r.in = {ResAmount{obsidian, 1}, ResAmount{coal, 1}};
  r.out = {ResAmount{steel, 1}};
  r.turns = 1;
  tx.building(works).recipes.push_back(std::move(r));
  notes.push_back("«Сталелитейная»: добавлен рецепт «Обсидиановая сталь»");
}

}  // namespace rg::content::detail
