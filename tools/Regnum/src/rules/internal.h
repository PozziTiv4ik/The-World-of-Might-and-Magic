// Regnum — общие помощники реализации правил (только для src/rules/*.cpp).
#pragma once
#include <unordered_map>

#include "geo/topo.h"
#include "rules/rules.h"

namespace rg::rules::detail {

using schema::kMaxCount;   // предел численностей

// ---------------------------------------------------------------- тексты
inline std::string q(const std::string& s) { return "«" + s + "»"; }  // «…»
std::string amount(double v);                          // число для сообщений (до тысячных)
std::string amountOf(Id res, double v);                // количество ресурса (золото — «… тыс.»)
std::string resName(const World& w, Id res);           // название ресурса
std::string facName(const World& w, Id f);             // «Название» фракции
std::string provName(const World& w, Id p);            // «Название» провинции
std::string armyName(const World& w, Id a);            // «Название» войска/флота
std::string buildingName(const World& w, Id b);        // «Название» постройки
std::string techName(const World& w, Id t);            // «Название» технологии
// Название, не совпадающее с существующими (base, base 2, base 3, …).
std::string uniqueName(const std::vector<std::string>& taken, const std::string& base);

using schema::nextTreePos;   // позиция нового узла схемы дерева

// ---------------------------------------------------------------- цены найма (rules/rows.cpp)
constexpr double kMaxAmount = 1e12;   // предел количеств ресурсов и эссенций в ценах
void needAmount(double v, const char* what);   // число 0…kMaxAmount, иначе отказ с подписью what
// Ключевой ресурс юнита типа type (per на юнит); special — особый отряд: любой ресурс справочника, не меньше 1 на юнит.
void checkKey(const World& w, UnitType type, Id res, double per, bool special = false);
// Ключевой ресурс, раса и цены под новый тип: неподходящее снимается (у особого отряда ключевой ресурс — любой).
void fitToType(const World& w, ArmyRow& r);
void fitToType(const World& w, SpecialUnit& s);

// ---------------------------------------------------------------- проверки
const Faction& needFaction(const World& w, Id id);
const Faction& needState(const World& w, Id id);
const Faction& needGuild(const World& w, Id id);
const Province& needProvince(const World& w, Id id);
const Character& needCharacter(const World& w, Id id);
const Army& needArmy(const World& w, Id id);
const Building& needBuilding(const World& w, Id id);
const Tech& needTech(const World& w, Id id);
void needFinite(double v, const char* what);

// ID записей таблицы, удовлетворяющих условию (по возрастанию). Сначала собрать, потом менять:
// изменение через Tx во время обхода той же таблицы недопустимо.
template <class T, class Pred>
std::vector<Id> idsWhere(const Table<T>& t, Pred&& pred) {
  std::vector<Id> r;
  t.each([&](const T& e) { if (pred(e)) r.push_back(e.id); });
  return r;
}
template <class V, class X> bool contains(const V& v, const X& x) { return std::find(v.begin(), v.end(), x) != v.end(); }
template <class V, class X> bool eraseValue(V& v, const X& x) {
  auto it = std::remove(v.begin(), v.end(), x);
  if (it == v.end()) return false;
  v.erase(it, v.end());
  return true;
}

// ---------------------------------------------------------------- численности
// Население провинции (сумма рас, отрицательные не считаются).
inline i64 provincePop(const Province& p) {
  i64 n = 0;
  for (const RacePop& r : p.races) n += std::max<i64>(0, r.pop);
  return n;
}
// Отряды объекта (все группы).
inline i64 unitsOf(const Army& a) {
  i64 n = 0;
  for (const ArmyGroup& g : a.groups)
    for (const ArmyUnit& u : g.units) n += u.count;
  return n;
}
// Фракции объекта в порядке групп (без повторов).
inline std::vector<Id> factionsOf(const Army& a) {
  std::vector<Id> r;
  for (const ArmyGroup& g : a.groups)
    if (std::find(r.begin(), r.end(), g.faction) == r.end()) r.push_back(g.faction);
  return r;
}

// ---------------------------------------------------------------- запасы
void addStock(Faction& f, Id res, double delta);

// ---------------------------------------------------------------- геометрия в транзакции
// Кеш граней — только если геометрия в транзакции не менялась (таблицы черновика меняются на месте).
std::shared_ptr<const geo::FaceSet> facesFor(const Tx& tx);

// ---------------------------------------------------------------- эффекты
// Изученные технологии по фракциям (по возрастанию ID) и модификаторы, которые ставятся сами (совет, голод, должности).
struct SourceIndex {
  std::unordered_map<Id, std::vector<const Tech*>> studied;
  std::unordered_map<Id, std::vector<AutoMod>> autos;
  explicit SourceIndex(const World& w);
  const std::vector<const Tech*>* of(Id faction) const;
  const std::vector<AutoMod>* autosOf(Id faction) const;
};
Effects provinceFx(const World& w, const SourceIndex& si, const Province& p);
// owned — провинции государства (для эффектов построек); для гильдии пусто.
Effects factionFx(const World& w, const SourceIndex& si, const Faction& f, const std::vector<const Province*>& owned);
// Эффекты войска; leaderFx — готовые эффекты государства-лидера (nullptr — посчитать).
Effects armyFx(const World& w, const Army& a, const Effects* leaderFx);
double loyaltyDeltaOf(const World& w, const Army& a, const Effects& armyFx);
// Показатели археологической группы с готовыми глобальными эффектами государства.
ArchStats archStatsWith(const World& w, const ArchGroup& g, const Effects& factionFx);
int slotsOf(const Province& p, const Effects& fx);
double costFactorOf(const Effects& fx);

// ---------------------------------------------------------------- строительство
// Вернуть плательщику уплаченное за строящийся уровень (pb — копия записи). Возвращает получателя (0 — никому).
Id refundPaid(Tx& tx, const ProvBuilding& pb);
// Снесённая постройка-хранилище: её реликвии переходят государству state (нет государства — становятся свободными).
void releaseBuildingRelics(Tx& tx, Id state, const ProvBuilding& pb);
// Прерванный цикл преобразования (pb — копия записи; цикл идёт, если pb.cycle > 0): вход цикла — рецепт × множитель
// (не больше открытого state), ресурсы и эссенции — возвращается state. Возвращает true, если цикл шёл.
bool refundCycle(Tx& tx, Id state, const ProvBuilding& pb);

// ---------------------------------------------------------------- экономика хода (calc.cpp, population.cpp, forces.cpp)
// Оккупант провинции (0 — не оккупирована): существующая фракция, не владелец.
Id occupierOf(const World& w, const Province& p);
// Рабы на работах сверх 10 % населения провинции снимаются с работ (с конца списка, целыми); хроника.
void trimSlaveWork(Tx& tx);
// Долг эссенции: строки элементалей, которые содержатся эссенцией с отрицательным запасом, теряют ⌈10 %⌉ численности
// (сначала из резерва, затем из гарнизонов и войск); хроника.
void elementalDecay(Tx& tx, Id faction);

// ---------------------------------------------------------------- размещение войск
class Placement {
 public:
  Placement(const World& w, std::shared_ptr<const geo::FaceSet> fs);
  bool valid(ArmyKind kind, Vec2 p, Id exclude, std::string* why) const;
  std::optional<Vec2> freeSpot(ArmyKind kind, Vec2 near, Id exclude) const;
  void set(Id id, Vec2 p);
  void remove(Id id);
  // Ближайший объект, чья фигурка ближе dist к точке (0 — нет).
  Id nearest(Vec2 p, double maxDist, Id exclude) const;
  Id provinceAt(Vec2 p) const { return fs_ ? fs_->provinceAt(p) : 0; }

 private:
  const World* w_;
  std::shared_ptr<const geo::FaceSet> fs_;
  std::vector<std::pair<Id, Vec2>> objs_;
};

// ---------------------------------------------------------------- провинции
// Удалить запись провинции (геометрия уже снята): столицы, возврат за начатое строительство, хроника.
// why — причина для хроники (пусто — «удалена с карты»).
void dropProvinceRecord(Tx& tx, Id province, const std::string& why);

// ---------------------------------------------------------------- союзы
// Отношения пары перестали быть союзом: союзные объекты, где есть обе фракции, распускаются на отдельные.
void splitBrokenAlliances(Tx& tx, Id a, Id b);

// ---------------------------------------------------------------- войска
// Вернуть воинов строки армии: живые — в население государства, нежить — в трупы, демоны — в демоническую энергию.
void returnWarriors(Tx& tx, Id faction, const ArmyRow& row, i64 count);
// Строка армии мятежного государства с теми же названием, типом, содержанием и расой (существующая или новая).
Id mirrorRow(Tx& tx, Id rebelState, Id origin, const ArmyRow& src);
// Провинция под точкой (кеш граней мира транзакции).
Id provinceAtTx(const Tx& tx, Vec2 p);
// Точка подписи провинции (центр для новых объектов); nullopt — у провинции нет области.
std::optional<Vec2> provinceLabel(const Tx& tx, Id province);
// Соседние провинции.
std::vector<Id> neighborsOf(const Tx& tx, Id province);
// Трупы победителю битвы (государство нежити, некроманты среди героев); livingDead — погибшие живые воины побеждённых.
double battleCorpses(Tx& tx, Id winnerFaction, const std::vector<Id>& winnerHeroes, i64 livingDead);
// Мятеж перечисленных войск одного государства (каждое — по своей верности); clicked — войско, к которому
// присоединяются верные части остальных (0 — мятеж начался в гарнизоне); garrison — провинция, гарнизон которой
// восстаёт вместе с ними по своей верности (0 — нет).
MutinyResult mutinyArmies(Tx& tx, const std::vector<Id>& armies, Id clicked, Id garrison = 0);
// Вернуть изучающей фракции уплаченное за исследование (Tech::paid или TechProgress::paid) и очистить запись.
void refundCost(Tx& tx, Id faction, std::map<Id, double>& paid);
// Название нового объекта: «Войско №N» / «Флот №N» — наименьший свободный номер среди объектов фракции.
std::string defaultArmyName(const World& w, ArmyKind kind, Id faction);
// Название союзного объекта по умолчанию (объединение войск разных фракций).
constexpr const char* kAlliedArmy = "Союзное войско";
constexpr const char* kAlliedFleet = "Союзный флот";
inline bool isAlliedName(const std::string& name) { return name == kAlliedArmy || name == kAlliedFleet; }

// ---------------------------------------------------------------- флот (rules/naval.cpp)
// Войско на борту существующего флота (не стоит на карте: размещение и попадание мышью его не учитывают).
bool aboard(const World& w, const Army& a);
// Войско на борту флота — в точку флота (после перемещения флота).
void syncCargo(Tx& tx, Id fleet);
// Удалить объект с учётом войска на борту: флот — его войско высаживается на ближайшую сушу (места нет — гибнет);
// войско на борту — флот освобождается.
void eraseObject(Tx& tx, Id army);
// Флот source объединяется с target: войско на борту source переходит на target (на обоих — войска объединяются).
// Вызывать до удаления source.
void combineCargo(Tx& tx, Id target, Id source);
// Флот уничтожен в битве: войско на борту гибнет вместе с ним (численность строк уменьшается, герои — в heroes).
void sinkCargo(Tx& tx, Id fleet, std::vector<Id>& heroes);
// Вместимость флота стала меньше войска на борту, хотя раньше вмещала его (before — вместимость до правки): отказ.
void needCapacity(const World& w, Id fleet, i64 before);

}  // namespace rg::rules::detail
