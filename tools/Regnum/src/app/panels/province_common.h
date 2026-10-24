// Regnum — общие помощники панели провинции (province_*.cpp): мир кадра, видимость вкладок, подписи и числа,
// подсказки-разбивки расчётов, фишки владельца и оккупанта, перенос фишек по строкам, двуполярный ползунок довольства,
// удаление и показ провинции.
#pragma once
#include "app/widgets.h"

namespace rg::app::prov {

using w::frameWorld;

// Видимость вкладок: только сухопутная или только морская провинция.
bool landOnly(App& a, Id pid);
bool seaOnly(App& a, Id pid);
// Ключ слияния шагов отмены для поля провинции: «province.<what>:<pid>».
std::string key(const char* what, Id pid);

// ---------------------------------------------------------------- подписи и числа
std::string provinceTitle(const Province& p);   // название или «Морская провинция» / «Провинция без названия»
std::string num(double v);                       // целое или с одним знаком
std::string pct(double v, bool sign = false);
std::string signedNum(double v);
std::string popText(double v);                   // население; от миллиона — кратко
std::string gold(double v);                      // золото — в тысячах
std::string goldSigned(double v);

// Источник эффекта: значок, вид, модификатор (запись мира или встроенный шаблон) и название.
const char* sourceIcon(rules::EffectSource::Kind k);
const char* sourceKind(rules::EffectSource::Kind k);
const Modifier* sourceMod(const World& wd, const rules::EffectSource& s);
// Модификатор источника с действующими эффектами: у модификатора провинции — как он действует в ней
// (rules::provinceModView: «Чума» при «Здании чумы» даёт прирост); nullopt — нет модификатора.
std::optional<Modifier> sourceView(const World& wd, const rules::EffectSource& s);
std::string sourceName(const World& wd, const rules::EffectSource& s);

// ---------------------------------------------------------------- подсказка-разбивка
struct TipLine {
  std::string label, value;
  ui::Tone tone = ui::Tone::Neutral;
  bool total = false;              // итог: под чертой, полужирный
};
// Богатая подсказка к последнему элементу (у элемента должна быть своя подсказка — она заменяется этой).
void breakdown(std::string_view title, const std::vector<TipLine>& lines, float width = 300);
// То же над последним элементом без захвата наведения (ui::tag: ui::tooltip и breakdown к метке не показываются).
void breakdownOver(std::string_view title, const std::vector<TipLine>& lines, float width = 300);
std::vector<TipLine> rebellionLines(const World& wd, const Province& p, const rules::ProvinceCalc& pc);
std::vector<TipLine> tradeLines(const World& wd, const Province& p, const rules::ProvinceCalc& pc);
std::vector<TipLine> productionLines(const World& wd, const Province& p, const rules::ProvinceCalc& pc);   // с постройками добычи
std::vector<TipLine> growthLines(const World& wd, const rules::ProvinceCalc& pc);   // прирост населения по источникам
std::string growthPct(double v, bool sign = false);                                  // прирост, % — до сотых
std::vector<TipLine> contentLines(const World& wd, const rules::ProvinceCalc& pc);  // довольство за ход: модификаторы, налог
std::vector<TipLine> taxLines(const rules::ProvinceCalc& pc);
std::vector<TipLine> slotLines(const World& wd, const rules::ProvinceCalc& pc);
// Прибавка маршрутов к торговой ценности, % базы.
double routePct(const rules::ProvinceCalc& pc);

// ---------------------------------------------------------------- фишки
// Фишка государства с флажком и фишка оккупации (штриховка цветом оккупанта); щелчок — true. maxW — предел ширины.
bool flagPill(std::string_view key, const Faction& f, std::string_view tip, float maxW);
bool hatchPill(std::string_view key, const Faction& occ, std::string_view tip, float maxW);
// Штриховка (как на карте: диагональные линии цвета оккупанта) в квадрате r.
void hatchSwatch(RectF r, Color c);

struct ChipSpec {
  std::string label;
  const char* icon = nullptr;
  Color color{0, 0, 0, 0};
  ui::Tone tone = ui::Tone::Neutral;
  std::string tooltip;
  bool clickable = false;
};
// Фишки в несколько строк по ширине области. Возвращает индекс нажатой (−1).
int flowChips(std::string_view key, const std::vector<ChipSpec>& chips);

// Ползунок −mn…mx с отметкой нуля (довольство). true — изменено; rectOut — область.
bool bipolarSlider(std::string_view key, double& v, double mn, double mx, bool disabled, RectF* rectOut);

// ---------------------------------------------------------------- действия
void showProvince(App& a, Id pid);
// Удаление с подтверждением (только в режиме правки границ — иначе kDeleteNeedsEdit в подсказке).
void askDelete(App& a, Id pid);
extern const char* const kDeleteNeedsEdit;
void startMerge(App& a, Id pid);
// Самое вероятное государство-оккупант: в войне с владельцем, иначе первое по алфавиту.
Id defaultOccupier(const World& wd, const Province& p);

}  // namespace rg::app::prov
