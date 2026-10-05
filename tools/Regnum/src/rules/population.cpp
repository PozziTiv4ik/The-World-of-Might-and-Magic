// Regnum — население и рабы: распределение людей между провинциями только целыми числами (ТЗ «Фиксы», п.4),
// рабы государства и на работах, колонизация, пустошь нежити и осквернение (ТЗ «Виды государств»).
#include "rules/internal.h"

namespace rg::rules {

using namespace detail;

namespace {

constexpr i64 kMaxCount = 1000000000000000LL;

i64 provincePop(const Province& p) {
  i64 n = 0;
  for (const RacePop& r : p.races) n += std::max<i64>(0, r.pop);
  return n;
}

// Сухопутные провинции государства по возрастанию ID.
std::vector<Id> ownedProvinces(const World& w, Id state, Id except = 0) {
  std::vector<Id> out;
  w.provinces.each([&](const Province& p) {
    if (p.owner == state && !p.sea && p.id != except) out.push_back(p.id);
  });
  return out;
}

void addRace(std::vector<RacePop>& races, Id race, i64 n) {
  if (n == 0) return;
  for (RacePop& r : races)
    if (r.race == race) {
      r.pop = clamp<i64>(r.pop + n, 0, kMaxCount);
      return;
    }
  if (n > 0) races.push_back(RacePop{race, std::min(n, kMaxCount)});
}

}  // namespace

// ================================================================ распределение
std::vector<i64> splitEven(i64 n, const std::vector<i64>& cap) {
  std::vector<i64> out(cap.size(), 0);
  if (n <= 0 || cap.empty()) return out;
  i64 left = n;
  // Поровну между теми, у кого ещё есть место; кому не хватает места — отдаёт всё, остаток делится дальше.
  for (int guard = 0; left > 0 && guard < 64; guard++) {
    std::vector<size_t> open;
    for (size_t i = 0; i < cap.size(); i++)
      if (cap[i] < 0 || out[i] < cap[i]) open.push_back(i);
    if (open.empty()) break;
    const i64 k = i64(open.size()), each = left / k;
    i64 rem = left % k;
    bool limited = false;
    for (size_t i : open) {
      i64 want = each + (rem > 0 ? 1 : 0);
      if (rem > 0) rem--;
      i64 room = cap[i] < 0 ? want : std::min(want, cap[i] - out[i]);
      if (room < want) limited = true;
      out[i] += room;
      left -= room;
    }
    if (!limited) break;
  }
  return out;
}

std::vector<i64> splitProportional(i64 n, const std::vector<i64>& weights) {
  std::vector<i64> out(weights.size(), 0);
  if (n <= 0 || weights.empty()) return out;
  double total = 0;
  for (i64 w : weights) total += double(std::max<i64>(0, w));
  if (total <= 0) return out;
  std::vector<std::pair<double, size_t>> frac;
  i64 given = 0;
  for (size_t i = 0; i < weights.size(); i++) {
    const double exact = double(n) * double(std::max<i64>(0, weights[i])) / total;
    const i64 f = i64(std::floor(exact));
    out[i] = f;
    given += f;
    if (weights[i] > 0) frac.push_back({exact - double(f), i});
  }
  // Наибольшие остатки; при равных — по порядку.
  std::stable_sort(frac.begin(), frac.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  for (size_t k = 0; given < n && !frac.empty(); k = (k + 1) % frac.size()) {
    out[frac[k].second]++;
    given++;
  }
  return out;
}

i64 statePopulation(const World& w, Id state) {
  i64 n = 0;
  w.provinces.each([&](const Province& p) {
    if (p.owner == state && !p.sea) n += provincePop(p);
  });
  return n;
}

std::map<Id, i64> takePopulation(Tx& tx, Id state, i64 n, Id exceptProvince) {
  std::map<Id, i64> taken;
  if (n <= 0) return taken;
  needState(tx.w(), state);
  std::vector<Id> provs = ownedProvinces(tx.w(), state, exceptProvince);
  std::vector<i64> caps;
  i64 have = 0;
  for (Id pid : provs) {
    i64 p = provincePop(*tx.w().province(pid));
    caps.push_back(p);
    have += p;
  }
  if (have < n) fail("Недостаточно населения у " + facName(tx.w(), state) + ": нужно " + fmtInt(n) + ", есть " + fmtInt(have));
  std::vector<i64> share = splitEven(n, caps);
  for (size_t i = 0; i < provs.size(); i++) {
    if (share[i] <= 0) continue;
    Province& p = tx.province(provs[i]);
    std::vector<i64> weights;
    for (const RacePop& r : p.races) weights.push_back(std::max<i64>(0, r.pop));
    std::vector<i64> part = splitProportional(share[i], weights);
    for (size_t k = 0; k < p.races.size(); k++) {
      i64 t = std::min(part[k], std::max<i64>(0, p.races[k].pop));
      p.races[k].pop -= t;
      taken[p.races[k].race] += t;
    }
  }
  return taken;
}

void givePopulation(Tx& tx, Id state, i64 n, Id onlyProvince, const std::map<Id, i64>& races) {
  if (n <= 0) return;
  const World& w0 = tx.w();
  std::vector<Id> provs;
  if (onlyProvince) provs.push_back(onlyProvince);
  else
    for (Id pid : ownedProvinces(w0, state))
      if (!provinceHas(w0, pid, schema::mod::UndeadWaste)) provs.push_back(pid);   // в пустошь нежити не селятся
  if (provs.empty()) return;
  // Раса для пустой провинции: самая многочисленная у государства, иначе первая в справочнике.
  Id fallback = 0;
  {
    std::map<Id, i64> all;
    w0.provinces.each([&](const Province& p) {
      if (p.owner == state && !p.sea)
        for (const RacePop& r : p.races) all[r.race] += std::max<i64>(0, r.pop);
    });
    i64 best = -1;
    for (auto& [r, c] : all)
      if (c > best) { best = c; fallback = r; }
    if (!fallback && !w0.catalogs->races.empty()) fallback = w0.catalogs->races.front().id;
  }
  std::vector<i64> share = splitEven(n, std::vector<i64>(provs.size(), -1));
  for (size_t i = 0; i < provs.size(); i++) {
    if (share[i] <= 0) continue;
    Province& p = tx.province(provs[i]);
    std::vector<Id> ids;
    std::vector<i64> weights;
    if (!races.empty()) {
      for (auto& [r, c] : races) {
        ids.push_back(r);
        weights.push_back(c);
      }
    } else {
      for (const RacePop& r : p.races) {
        ids.push_back(r.race);
        weights.push_back(std::max<i64>(0, r.pop));
      }
    }
    i64 sum = 0;
    for (i64 x : weights) sum += x;
    if (sum <= 0) {
      if (!fallback) continue;   // рас нет вовсе — жителей некуда записать
      ids = {fallback};
      weights = {1};
    }
    std::vector<i64> part = splitProportional(share[i], weights);
    for (size_t k = 0; k < ids.size(); k++) addRace(p.races, ids[k], part[k]);
  }
}

void setRacePop(Tx& tx, Id province, Id race, i64 pop) {
  const Province& p = needProvince(tx.w(), province);
  if (p.sea) fail("В морской провинции нет населения");
  if (pop < 0 || pop > kMaxCount) fail("Численность должна быть от 0 до " + fmtInt(kMaxCount));
  if (pop > 0 && hasModKey(tx.w(), p.modifiers, schema::mod::UndeadWaste)) fail("В пустоши нежити население не может быть больше 0");
  auto& rs = tx.province(province).races;
  for (RacePop& r : rs)
    if (r.race == race) {
      r.pop = pop;
      return;
    }
  rs.push_back(RacePop{race, pop});
}

// ================================================================ рабы
void setSlaves(Tx& tx, Id state, Id race, i64 count, double contentment) {
  needState(tx.w(), state);
  if (!Catalogs::find(tx.w().catalogs->races, race)) fail("Раса не найдена");
  if (count < 0 || count > kMaxCount) fail("Численность должна быть от 0 до " + fmtInt(kMaxCount));
  needFinite(contentment, "Довольство");
  contentment = clamp(contentment, -100.0, 100.0);
  // Рабов на работах не больше, чем рабов этой расы.
  i64 atWork = 0;
  tx.w().provinces.each([&](const Province& p) {
    if (p.owner != state) return;
    for (const SlaveWork& s : p.slaves)
      if (s.race == race) atWork += s.count;
  });
  if (count < atWork) fail("На работах " + fmtInt(atWork) + " рабов этой расы — сначала снимите их с работ");
  auto& list = tx.faction(state).slaves;
  for (SlaveGroup& g : list)
    if (g.race == race) {
      g.count = count;
      g.contentment = contentment;
      return;
    }
  list.push_back(SlaveGroup{race, count, contentment});
}

i64 slaveWorkLimit(const World& w, Id province) {
  const Province* p = w.province(province);
  if (!p) return 0;
  return i64(std::floor(double(provincePop(*p)) * schema::kSlaveWorkShare));
}

void setSlaveWork(Tx& tx, Id province, Id race, i64 count) {
  const Province& p = needProvince(tx.w(), province);
  if (p.sea) fail("В морской провинции нет работ");
  if (!p.owner) fail("У провинции нет владельца");
  if (count < 0) fail("Численность не может быть отрицательной");
  const Faction& f = needState(tx.w(), p.owner);
  i64 cur = 0, otherRaces = 0;
  for (const SlaveWork& s : p.slaves) (s.race == race ? cur : otherRaces) += s.count;
  if (count > cur) {
    const i64 limit = slaveWorkLimit(tx.w(), province);
    if (otherRaces + count > limit)
      fail("На работы — не больше 10 % населения провинции: " + fmtInt(limit) + " (уже занято " + fmtInt(otherRaces) + ")");
    i64 pool = 0, atWork = 0;
    for (const SlaveGroup& g : f.slaves)
      if (g.race == race) pool = g.count;
    tx.w().provinces.each([&](const Province& x) {
      if (x.owner != p.owner) return;
      for (const SlaveWork& s : x.slaves)
        if (s.race == race) atWork += s.count;
    });
    if (count - cur > pool - atWork)
      fail("Свободных рабов этой расы: " + fmtInt(std::max<i64>(0, pool - atWork)));
  }
  auto& list = tx.province(province).slaves;
  auto it = std::find_if(list.begin(), list.end(), [&](const SlaveWork& s) { return s.race == race; });
  if (count == 0) {
    if (it != list.end()) list.erase(it);
  } else if (it != list.end()) {
    it->count = count;
  } else {
    list.push_back(SlaveWork{race, count});
  }
}

// ================================================================ колонизация
void colonize(Tx& tx, Id province, Id state) {
  const Province& p = needProvince(tx.w(), province);
  needState(tx.w(), state);
  if (p.sea) fail("Морскую провинцию нельзя колонизировать");
  if (p.owner) fail("У провинции уже есть владелец — " + facName(tx.w(), p.owner));
  if (hasModKey(tx.w(), p.modifiers, schema::mod::Devastated)) fail("Опустошённую провинцию нельзя назначить ни одному государству");
  const double cost = std::max(0.0, constantOf(tx.w(), schema::cst::ColonizationCost).num);
  const Faction& f = *tx.w().faction(state);
  if (cost > 0 && f.treasury() + 1e-9 < cost)
    fail("Недостаточно золота у " + facName(tx.w(), state) + ": колонизация стоит " + amount(cost) + ", в казне " + amount(f.treasury()));
  setProvinceOwner(tx, province, state);
  if (cost > 0) addStock(tx.faction(state), kGold, -cost);
  addLog(tx, LogKind::Province,
         facName(tx.w(), state) + " колонизирует провинцию " + provName(tx.w(), province) + (cost > 0 ? ": " + amount(cost) + " золота" : std::string()),
         LogRefs{province, 0, {state}});
}

// ================================================================ пустошь нежити и осквернение
void makeWasteland(Tx& tx, Id province) {
  const Province& p = needProvince(tx.w(), province);
  if (p.sea) fail("Морская провинция не может стать пустошью");
  const Faction* o = tx.w().faction(p.owner);
  if (!o || !o->isState() || o->stateKind != StateKind::Undead) fail("Обратить в пустошь может только государство нежити — владелец провинции");
  if (hasModKey(tx.w(), p.modifiers, schema::mod::UndeadWaste)) fail("Провинция уже пустошь нежити");
  const Id owner = p.owner;
  const i64 pop = provincePop(p);
  for (RacePop& r : tx.province(province).races) r.pop = 0;
  tx.province(province).slaves.clear();
  if (pop > 0) addStock(tx.faction(owner), ensureResource(tx, schema::kResCorpses), double(pop));
  addModifier(tx, ModTarget::Province, province, ensureBuiltinMod(tx, schema::mod::UndeadWaste), 0);
  addLog(tx, LogKind::Province, "Провинция " + provName(tx.w(), province) + " обращена в пустошь нежити: трупов " + fmtInt(pop),
         LogRefs{province, 0, {owner}});
}

void cleanseWasteland(Tx& tx, Id province, i64 settlers) {
  const Province& p = needProvince(tx.w(), province);
  const Faction* o = tx.w().faction(p.owner);
  if (!o || !o->isState() || o->stateKind != StateKind::Living) fail("Очистить пустошь может только государство живых — владелец провинции");
  Id mid = 0;
  for (Id m : p.modifiers)
    if (const Modifier* x = tx.w().modifier(m); x && x->key == schema::mod::UndeadWaste) mid = m;
  if (!mid) fail("Провинция не пустошь нежити");
  if (settlers < 0) fail("Переселенцев не может быть меньше нуля");
  const Id owner = p.owner;
  dropModifier(tx, ModTarget::Province, province, mid);
  if (settlers > 0) {
    std::map<Id, i64> moved = takePopulation(tx, owner, settlers, province);
    givePopulation(tx, owner, settlers, province, moved);
  }
  addLog(tx, LogKind::Province,
         "Пустошь нежити в провинции " + provName(tx.w(), province) + " очищена" + (settlers > 0 ? ", переселено " + fmtInt(settlers) : std::string()),
         LogRefs{province, 0, {owner}});
}

void desecrate(Tx& tx, Id province) {
  const Province& p = needProvince(tx.w(), province);
  if (p.sea) fail("Морскую провинцию нельзя осквернить");
  const Faction* o = tx.w().faction(p.owner);
  if (!o || !o->isState() || o->stateKind != StateKind::Demonic) fail("Осквернить может только государство демонов — владелец провинции");
  if (hasModKey(tx.w(), p.modifiers, schema::mod::UndeadWaste)) fail("Пустошь нежити нельзя осквернить");
  if (hasModKey(tx.w(), p.modifiers, schema::mod::Desecrated)) fail("Провинция уже осквернена");
  const Id owner = p.owner;
  const double energy = schema::kDesecrateEnergy * std::floor(double(provincePop(p)) * schema::kDesecrateShare);
  if (energy > 0) addStock(tx.faction(owner), ensureResource(tx, schema::kResEnergy), energy);
  addModifier(tx, ModTarget::Province, province, ensureBuiltinMod(tx, schema::mod::Desecrated), 0);
  addLog(tx, LogKind::Province, "Провинция " + provName(tx.w(), province) + " осквернена: демонической энергии " + amount(energy),
         LogRefs{province, 0, {owner}});
}

void cleanseDesecration(Tx& tx, Id province) {
  const Province& p = needProvince(tx.w(), province);
  const Faction* o = tx.w().faction(p.owner);
  if (!o || !o->isState() || o->stateKind == StateKind::Demonic)
    fail("Очистить осквернённую провинцию может государство живых или нежити — владелец провинции");
  Id mid = 0;
  for (Id m : p.modifiers)
    if (const Modifier* x = tx.w().modifier(m); x && x->key == schema::mod::Desecrated) mid = m;
  if (!mid) fail("Провинция не осквернена");
  const Id owner = p.owner;
  dropModifier(tx, ModTarget::Province, province, mid);
  addLog(tx, LogKind::Province, "Осквернённая провинция " + provName(tx.w(), province) + " очищена", LogRefs{province, 0, {owner}});
}

}  // namespace rg::rules
