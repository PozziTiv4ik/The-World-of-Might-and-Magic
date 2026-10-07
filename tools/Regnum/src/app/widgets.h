// Regnum — общие виджеты предметной области для панелей, диалогов и редакторов.
// Единый вид выбора фракций, провинций, персонажей, записей справочников и модификаторов,
// «фишки» сущностей со щелчком-переходом и подписи эффектов. Только из главного потока внутри кадра.
//
//   Id owner = p->owner;
//   if (w::factionPicker("owner", owner, w::FactionFilter::States, "Без владельца"))
//     a.act("Владелец провинции", [&](Tx& tx) { rules::setProvinceOwner(tx, pid, owner); });
#pragma once
#include "app/app.h"

namespace rg::app::w {

enum class FactionFilter : u8 { Any, States, Guilds };

// Выпадающий выбор фракции (цветная точка, значок вида). value — ID или 0 («нет», если noneLabel не пуст).
// exclude — скрыть фракцию (например, саму себя). true — значение изменилось.
bool factionPicker(std::string_view id, Id& value, FactionFilter filter = FactionFilter::Any,
                   std::string_view noneLabel = "—", Id exclude = 0, bool disabled = false);

// Выбор провинции (owner != 0 — только провинции этого государства). Морские провинции не предлагаются.
bool provincePicker(std::string_view id, Id& value, Id owner = 0, std::string_view noneLabel = "—", bool disabled = false);

// Выбор персонажа. faction != 0 — только персонажи этой фракции, доступные для назначений (не «Мертв» и не
// «Взят в плен»; ТЗ «Фиксы», п.9): назначенный недоступный виден, но не выбирается. faction = 0 — все персонажи.
// allowCreate — последний пункт «Новый персонаж»: создаёт запись (с фракцией faction) и выбирает её.
bool characterPicker(std::string_view id, Id& value, Id faction = 0, std::string_view noneLabel = "—",
                     bool allowCreate = true, bool disabled = false);

// Выбор записи справочника (ресурсы, расы, культуры, религии, формы правления, должности).
// allowCreate — пункт «Добавить…»: запрос названия и создание записи.
bool catalogPicker(std::string_view id, rules::CatalogList list, Id& value, std::string_view noneLabel = "—",
                   bool allowCreate = false, bool disabled = false);

// Где действует список модификаторов (ТЗ 1.g.ii): Local — провинция (действуют только локальные эффекты: модификатор
// лишь с глобальными эффектами помечается предупреждением и не предлагается к добавлению); Any — технологии и
// постройки (локальные эффекты — в провинции, глобальные — государству); Faction — государство или гильдия;
// Army — войско (эффекты войск); Hero — герой (эффектов у героя нет: природа, состояния).
enum class ModScope { Any, Local, Faction, Army, Hero };
// Правка списка модификаторов помимо состава (modifierList с edit).
struct ModEdit {
  Id termOf = 0;          // модификатор, срок которого изменён (список при этом не меняется)
  int turns = 0;          // его новый срок, ходов (0 — бессрочно)
  std::string addKey;     // выбран встроенный модификатор без записи мира: создать (rules::ensureBuiltinMod) и добавить
};
// Список модификаторов фишками (удаление крестиком) + выбор для добавления. true — изменился состав (ids) или выбран
// встроенный модификатор без записи (edit->addKey). Предлагаются модификаторы вида «Везде» или вида области
// (modifierFits), с edit — и встроенные шаблоны, которых ещё нет в мире. Фишка i помечается в App::uiRect как
// «<id>.chip.<i>», фишка без действующих здесь эффектов — ещё и «<id>.warn.<модификатор>».
// turns — сроки (нет записи — бессрочно): срок на фишке, щелчок по фишке — правка срока (в edit->termOf/turns).
// Без turns щелчок открывает редактор модификаторов.
bool modifierList(std::string_view id, std::vector<Id>& ids, bool disabled = false, ModScope where = ModScope::Any,
                  const ModTurns* turns = nullptr, ModEdit* edit = nullptr);
// У модификатора есть эффекты, но ни один не действует там, где список (where).
bool modifierInert(const Modifier& m, ModScope where);
// Модификатор можно добавить в список области: вид «Везде» или вид области, не автоматический (столица, совет, голод).
bool modifierFits(const Modifier& m, ModScope where);

// Состояние героя одной строкой: «Мертв · <место захоронения>», «В плену · <государство>»; пусто — доступен.
std::string heroState(const World& w, const Character& c);

// Фишки-ссылки: щелчок выделяет сущность и открывает её инспектор.
void factionChip(Id faction, bool showKind = false);
void provinceChip(Id province);
void characterChip(Id character);

// Подпись эффекта: «+10 % торговой ценности», «−5 довольства за ход».
std::string effectText(Fx f, double value);
// Фишки всех эффектов модификатора (значок эффекта, знак, тон: польза — success, вред — danger).
void effectChips(const Modifier& m);
// Хорош ли эффект для владельца при данном знаке (рост восстания и стоимости — плохо).
bool effectGood(Fx f, double value);

// Значок и цвет ресурса (из справочника; «Золото» — coins).
const char* resourceIcon(const World& w, Id res);
Color resourceColor(const World& w, Id res);
// Строка «значок + количество» для ресурса (например, стоимость постройки).
void resourceAmount(Id res, double amount, ui::Ink ink = ui::Ink::Normal);

// Цвет фракции (серый, если нет).
Color factionColor(const World& w, Id faction);

// ---------------------------------------------------------------- группы ресурсов (ТЗ «Добавления в справочники», п.3)
// Группы в порядке дерева: (группа, глубина) — родитель, затем его подгруппы.
std::vector<std::pair<Id, int>> groupOrder(const World& w);
// Выбор группы ресурсов (подгруппы — с отступом, справа — число ресурсов с подгруппами). noneLabel непусто — пункт
// «нет» (0): «Все ресурсы», «Без группы». true — значение изменилось.
bool resGroupPicker(std::string_view id, Id& group, std::string_view noneLabel = "Все ресурсы", bool disabled = false);
// Выбор ресурса из списка ids (подпись справа — группа ресурса; поиск по названию всегда). noneLabel непусто —
// пункт «нет» (0) первым.
bool resourceFrom(std::string_view id, Id& value, const std::vector<Id>& ids, std::string_view placeholder = "Ресурс",
                  bool disabled = false, std::string_view tooltip = {}, std::string_view noneLabel = {});
// Ресурс через категорию (ТЗ «Ввод новых механик», п.1): ряд «[Группа ▾] [Ресурс ▾]» — сначала группа (с подгруппами),
// затем ресурс из неё. only — только эти ресурсы (nullptr — все). Группа запоминается у виджета. true — выбран ресурс.
bool resourceByGroup(std::string_view id, Id& value, bool disabled = false, const std::vector<Id>* only = nullptr);

// ---------------------------------------------------------------- эссенции и реликвии
bool essencePicker(std::string_view id, Id& value, std::string_view noneLabel = {}, bool disabled = false);
Color essenceColor(const World& w, Id essence);
// Цвет подсветки редкости (обычная — белая, редкая — синяя, эпическая — фиолетовая, легендарная — оранжевая,
// эпохальная — красная) и фишка реликвии с подсветкой этим цветом (в потоке фишек edkit::chipsBegin/chipsEnd или в ряду).
Color rarityColor(Rarity r);
ui::ChipAction relicChip(const Relic& r, bool removable = false, std::string_view tooltip = {});

// ---------------------------------------------------------------- портреты в кругах (ТЗ «Исправления», п.2)
// Портрет персонажа для круглого аватара: квадрат, смещённый к верху изображения (лицо в круге); nullptr — нет
// портрета или он ещё декодируется (тогда — инициалы).
const gfx::Image* faceImage(const Character& c);
// initials — фон инициалов без портрета (a = 0 — по имени).
void heroAvatar(const Character& c, float size, bool ring = false, std::string_view tooltip = {}, Color initials = Color(0, 0, 0, 0));

}  // namespace rg::app::w
