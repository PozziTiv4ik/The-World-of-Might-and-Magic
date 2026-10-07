// Regnum — общие помощники реализации правил (только для src/rules/*.cpp).
#pragma once
#include <unordered_map>

#include "geo/topo.h"
#include "rules/rules.h"

namespace rg::rules::detail {

// ---------------------------------------------------------------- тексты
inline std::string q(const std::string& s) { return "«" + s + "»"; }  // «…»
std::string amount(double v);                          // число для сообщений (до тысячных)
std::string resName(const World& w, Id res);           // название ресурса
std::string facName(const World& w, Id f);             // «Название» фракции
std::string provName(const World& w, Id p);            // «Название» провинции
std::string armyName(const World& w, Id a);            // «Название» войска/флота
std::string buildingName(const World& w, Id b);        // «Название» постройки
std::string techName(const World& w, Id t);            // «Название» технологии
// Название, не совпадающее с существующими (base, base 2, base 3, …).
std::string uniqueName(const std::vector<std::string>& taken, const std::string& base);

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
int slotsOf(const Province& p, const Effects& fx);
double costFactorOf(const Effects& fx);

// ---------------------------------------------------------------- расчёт
std::shared_ptr<const Calc> compute(const World& w, const geo::FaceSet* fs);

// ---------------------------------------------------------------- строительство
// Стоимость уровня (1…) с множителем провинции; только положительные позиции.
std::map<Id, double> levelCost(const Building& b, int level, double factor);
// Вернуть плательщику уплаченное за строящийся уровень (pb — копия записи). Возвращает получателя (0 — никому).
Id refundPaid(Tx& tx, const ProvBuilding& pb);

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

// ---------------------------------------------------------------- сделки
Id conclude(Tx& tx, Deal d, bool log);
std::string dealText(const World& w, const Deal& d);

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

}  // namespace rg::rules::detail
