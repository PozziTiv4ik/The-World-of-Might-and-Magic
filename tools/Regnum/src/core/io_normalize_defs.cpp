// Regnum — нормализация: модификаторы, постройки (уровни, требования, возможности) и технологии (условия, изучение).
#include "core/io_normalize.h"

namespace rg::io::norm {

void Norm::modifiers() {
  const World base = w();
  base.modifiers.each([&](const Modifier& m0) {
    Modifier m = m0;
    const std::string where = refStr(Seq::Modifier, m.id);
    bool ch = false;
    u32 valid = (1u << kFxCount) - 1;
    if (m.fxMask & ~valid) {
      m.fxMask &= valid;
      ch = true;
    }
    for (int i = 0; i < kFxCount; i++) {
      const auto& e = schema::kEffects[i];
      double& v = m.fx[size_t(i)];
      if (!m.has(Fx(i))) {
        if (v != 0) { v = 0; ch = true; }
        continue;
      }
      if (!std::isfinite(v)) {
        warn(F_MODIFIERS, where + ".fx." + e.id, "значение эффекта не является числом — эффект удалён");
        m.fxMask &= ~(1u << i);
        v = 0;
        ch = true;
        continue;
      }
      ch |= fixNum(v, e.min, e.max, 0.0, F_MODIFIERS, where + ".fx", e.id);
    }
    ch |= fixRefList(m.targets, Seq::Faction, F_MODIFIERS, where, "targets");
    ch |= fixEnum(m.kind, int(ModKind::Count), ModKind::Any, F_MODIFIERS, where, "kind");
    ch |= fixNum(m.duration, 0, kMaxTurns, 0, F_MODIFIERS, where, "duration");
    ch |= fixAmounts(m.essGen, Seq::Essence, essenceIds(), F_MODIFIERS, where, "essGen");
    ch |= fixAmounts(m.resGen, Seq::Resource, resourceIds(), F_MODIFIERS, where, "resGen");
    // Ключ встроенного модификатора: известный и у одной записи.
    if (!m.key.empty()) {
      if (!schema::builtinModifier(m.key)) {
        warn(F_MODIFIERS, where + ".key", "неизвестный ключ встроенного модификатора «" + m.key + "» — снят");
        m.key.clear();
        ch = true;
      } else if (!modKeys.insert(m.key).second) {
        warn(F_MODIFIERS, where + ".key", "ключ «" + m.key + "» уже у другого модификатора — снят");
        m.key.clear();
        ch = true;
      }
    }
    if (ch) tx.modifier(m.id) = std::move(m);
  });
}

void Norm::buildings() {
  {
    const World base = w();
    base.buildings.each([&](const Building& b0) {
      Building b = b0;
      const std::string where = refStr(Seq::Building, b.id);
      bool ch = false;
      if (b.owner && !exists(Seq::Faction, b.owner)) {
        warn(F_BUILDINGS, where + ".owner", "нет фракции " + refStr(Seq::Faction, b.owner) + " — постройка стала общей");
        b.owner = 0;
        ch = true;
      } else if (b.owner && !isState(b.owner)) {
        // Уникальные постройки бывают только у государств: гильдия не владеет провинциями.
        warn(F_BUILDINGS, where + ".owner", refStr(Seq::Faction, b.owner) + " — гильдия, а уникальная постройка бывает только у государства: постройка стала общей");
        b.owner = 0;
        ch = true;
      }
      ch |= fixEnum(b.cat, int(BuildingCat::Count), BuildingCat::Economic, F_BUILDINGS, where, "cat");
      if (b.levels.empty()) {
        warn(F_BUILDINGS, where + ".levels", "у постройки нет уровней — добавлен один");
        b.levels.push_back(BuildingLevel{});
        ch = true;
      }
      for (size_t i = 0; i < b.levels.size(); i++) {
        auto& l = b.levels[i];
        std::string at = where + ".levels[" + std::to_string(i) + "]";
        ch |= fixNum(l.turns, 1, kMaxTurns, 1, F_BUILDINGS, at, "turns");
        ch |= fixAmounts(l.cost, Seq::Resource, resourceIds(), F_BUILDINGS, at, "cost", "цена удалена");
        ch |= fixRefList(l.modifiers, Seq::Modifier, F_BUILDINGS, at, "modifiers");
        ch |= fixAmounts(l.produce, Seq::Resource, resourceIds(), F_BUILDINGS, at, "produce", "производство удалено");
        // Эссенции уровня — только у постройки генерации эссенции.
        if (!b.essenceGen && !l.essence.empty()) {
          warn(F_BUILDINGS, at + ".essence", "постройка не генерирует эссенцию — эссенции уровня сняты");
          l.essence.clear();
          ch = true;
        }
        ch |= fixAmounts(l.essence, Seq::Essence, essenceIds(), F_BUILDINGS, at, "essence");
        ch |= fixAmounts(l.essCost, Seq::Essence, essenceIds(), F_BUILDINGS, at, "essCost");
        const u32 ships = b.shipyard ? l.ships & ((1u << int(ShipType::Count)) - 1) : 0u;
        if (ships != l.ships) {
          if (!b.shipyard) warn(F_BUILDINGS, at + ".ships", "постройка не верфь — типы кораблей сняты");
          l.ships = ships;
          ch = true;
        }
      }
      ch |= fixPoint(b.pos, false, F_BUILDINGS, where, "pos");
      // Постройка преобразования: рецепты — до трёх разных ресурсов и до трёх эссенций на входе, ресурс на выходе,
      // срок цикла 1…kMaxTurns.
      if (!b.convert) {
        if (!b.recipes.empty()) {
          b.recipes.clear();
          ch = true;
        }
      } else {
        for (size_t ri = 0; ri < b.recipes.size(); ri++) {
          Recipe& rc = b.recipes[ri];
          const std::string rw = where + ".recipes[" + std::to_string(ri) + "]";
          auto fixIn = [&](std::vector<ResAmount>& list, Seq sq, const char* field, const char* noun) {
            std::vector<ResAmount> in;
            for (const ResAmount& x : list) {
              if (!hasCat(sq, x.res) || std::any_of(in.begin(), in.end(), [&](const ResAmount& y) { return y.res == x.res; })) {
                warn(F_BUILDINGS, rw + "." + field, std::string(hasCat(sq, x.res) ? "повтор " : "нет ") + noun + " " + ref(sq, x.res) + " — позиция удалена");
                ch = true;
                continue;
              }
              ResAmount y = x;
              ch |= fixNum(y.amount, 0.0, 1e12, 0.0, F_BUILDINGS, rw + "." + field, refStr(sq, x.res).c_str());
              in.push_back(y);
            }
            if (in.size() > 3) {
              warn(F_BUILDINGS, rw + "." + field, "на входе больше трёх позиций — лишние удалены");
              in.resize(3);
              ch = true;
            }
            if (in != list) {
              list = std::move(in);
              ch = true;
            }
          };
          fixIn(rc.in, Seq::Resource, "in", "ресурса");
          fixIn(rc.essIn, Seq::Essence, "essIn", "эссенции");
          ch |= fixRef(rc.out.res, Seq::Resource, F_BUILDINGS, rw, "out");
          ch |= fixNum(rc.out.amount, 0.0, 1e12, 0.0, F_BUILDINGS, rw + ".out", "amount");
          ch |= fixNum(rc.turns, 1, kMaxTurns, 1, F_BUILDINGS, rw, "turns");
        }
      }
      // Иссушение, снижение содержания армии, добыча: данные уровней — только у постройки с возможностью.
      for (size_t li = 0; li < b.levels.size(); li++) {
        BuildingLevel& l = b.levels[li];
        const std::string at = where + ".levels[" + std::to_string(li) + "]";
        ch |= fixAmounts(l.produceOnRes, Seq::Resource, resourceIds(), F_BUILDINGS, at, "produceOnRes");
        if (!b.drain && (l.drainPop || !l.drainEss.empty())) {
          l.drainPop = 0;
          l.drainEss.clear();
          ch = true;
        }
        ch |= fixNum(l.drainPop, i64(0), kMaxCount, i64(0), F_BUILDINGS, at, "drainPop");
        ch |= fixAmounts(l.drainEss, Seq::Essence, essenceIds(), F_BUILDINGS, at, "drainEss");
        if (!b.upkeepCut && l.upkeepCut != 0) {
          l.upkeepCut = 0;
          ch = true;
        }
        ch |= fixNum(l.upkeepCut, 0.0, schema::kMaxUpkeepCut, 0.0, F_BUILDINGS, at, "upkeepCut");
        if (!b.extract && l.extractPct != 0) {
          l.extractPct = 0;
          ch = true;
        }
        ch |= fixNum(l.extractPct, 0.0, 10000.0, 0.0, F_BUILDINGS, at, "extractPct");
      }
      if (!b.extract && (!b.extractGroups.empty() || !b.extractExclude.empty())) {
        b.extractGroups.clear();
        b.extractExclude.clear();
        ch = true;
      }
      ch |= fixRefList(b.extractGroups, Seq::ResGroup, F_BUILDINGS, where, "extractGroups");
      ch |= fixRefList(b.extractExclude, Seq::Resource, F_BUILDINGS, where, "extractExclude");
      if (const u32 kinds = b.stateKinds & ((1u << int(StateKind::Count)) - 1); kinds != b.stateKinds) {
        b.stateKinds = kinds;
        ch = true;
      }
      // Доступ к особым отрядам.
      if (!b.specialAccess && !b.specials.empty()) {
        warn(F_BUILDINGS, where + ".specials", "постройка не даёт доступ к особым отрядам — список снят");
        b.specials.clear();
        ch = true;
      }
      ch |= fixRefList(b.specials, Seq::Special, F_BUILDINGS, where, "specials");
      // Требования «на государство»: существующая постройка (общей — только общая), 1…100 на каждую, без повторов.
      {
        std::vector<StateReq> keep;
        for (const StateReq& q0 : b.stateReqs) {
          StateReq q = q0;
          const Building* t = base.building(q.building);
          if (!t || q.building == b.id || (t->owner && t->owner != b.owner) ||
              std::any_of(keep.begin(), keep.end(), [&](const StateReq& x) { return x.building == q.building; })) {
            warn(F_BUILDINGS, where + ".stateReqs", "требование " + ref(Seq::Building, q.building) + " недопустимо — удалено");
            ch = true;
            continue;
          }
          ch |= fixNum(q.per, 1, 100, 4, F_BUILDINGS, where + ".stateReqs", "per");
          keep.push_back(q);
        }
        if (keep != b.stateReqs) b.stateReqs = std::move(keep);
      }
      // Ключ встроенной постройки: у общих построек — без повторов, у уникальных — без повторов у одного государства.
      if (!b.key.empty() && !buildingKeys.insert({b.owner, b.key}).second) {
        warn(F_BUILDINGS, where + ".key", "ключ «" + b.key + "» уже у другой постройки — снят");
        b.key.clear();
        ch = true;
      }
      // Технологии: общая постройка зависит только от общих технологий, уникальная — ещё и от технологий своего
      // государства.
      ch |= fixRefList(b.techs, Seq::Tech, F_BUILDINGS, where, "techs",
                       [&](Id id) { const Tech* t = base.tech(id); return t && (t->faction == 0 || (b.owner && t->faction == b.owner)); },
                       "технология другого государства (общей постройке — только общие)");
      if (ch) tx.building(b.id) = std::move(b);
    });
  }
  // Требования: существующая постройка, не сама, без повторов, уровень в пределах.
  {
    const World base = w();
    base.buildings.each([&](const Building& b0) {
      Building b = b0;
      const std::string where = refStr(Seq::Building, b.id);
      bool ch = false;
      std::vector<BuildingReq> res;
      for (auto& r : b.requires_) {
        const Building* t = base.building(r.building);
        if (!t) {
          warn(F_BUILDINGS, where + ".requires", "нет постройки " + ref(Seq::Building, r.building) + " — требование удалено");
          ch = true;
          continue;
        }
        if (r.building == b.id) {
          warn(F_BUILDINGS, where + ".requires", "постройка требует саму себя — требование удалено");
          ch = true;
          continue;
        }
        // Общая постройка не зависит от уникальных; уникальная — от общих и своего государства.
        if (t->owner && t->owner != b.owner) {
          warn(F_BUILDINGS, where + ".requires", b.owner ? "требование " + refStr(Seq::Building, r.building) + " — уникальная постройка другого государства: удалено"
                                                         : "общая постройка не может зависеть от уникальной " + refStr(Seq::Building, r.building) + " — требование удалено");
          ch = true;
          continue;
        }
        if (std::any_of(res.begin(), res.end(), [&](const BuildingReq& q) { return q.building == r.building; })) {
          warn(F_BUILDINGS, where + ".requires", "повтор требования " + refStr(Seq::Building, r.building) + " — удалён");
          ch = true;
          continue;
        }
        BuildingReq q = r;
        ch |= fixNum(q.level, 1, int(t->levels.size()), 1, F_BUILDINGS, where + ".requires." + refStr(Seq::Building, r.building), "level");
        res.push_back(q);
      }
      if (ch) {
        b.requires_ = std::move(res);
        tx.building(b.id) = std::move(b);
      }
    });
  }
  // Циклы требований.
  const World base = w();
  std::unordered_map<Id, std::vector<Id>> deps;
  base.buildings.each([&](const Building& b) {
    auto& d = deps[b.id];
    for (auto& r : b.requires_) d.push_back(r.building);
  });
  auto cut = cycleEdges(base.buildings.ids(), [&](Id id) -> const std::vector<Id>& { return deps[id]; });
  for (auto& [from, to] : cut) {
    warn(F_BUILDINGS, refStr(Seq::Building, from) + ".requires", "требование " + refStr(Seq::Building, to) + " замыкает цикл — удалено");
    auto& rq = tx.building(from).requires_;
    Id target = to;
    rq.erase(std::remove_if(rq.begin(), rq.end(), [&](const BuildingReq& q) { return q.building == target; }), rq.end());
  }
}

void Norm::techs() {
  {
    const World base = w();
    base.techs.each([&](const Tech& t) {
      if (t.faction && !exists(Seq::Faction, t.faction)) {   // фракция 0 — общее дерево
        warn(F_TECHS, refStr(Seq::Tech, t.id), "нет фракции " + refStr(Seq::Faction, t.faction) + " — технология удалена");
        tx.eraseTech(t.id);
      }
    });
  }
  {
    const World base = w();
    base.techs.each([&](const Tech& t0) {
      Tech t = t0;
      const std::string where = refStr(Seq::Tech, t.id);
      bool ch = fixNum(t.turns, 1, kMaxTurns, 1, F_TECHS, where, "turns");
      // Пройдено ходов исследования: с модификатором «Время исследования технологий» срок может быть больше turns.
      ch |= fixNum(t.progress, 0, kMaxTurns, 0, F_TECHS, where, "progress");
      if (t.studied && t.research) {
        warn(F_TECHS, where + ".research", "изученная технология не может исследоваться — исследование снято");
        t.research = false;
        ch = true;
      }
      // Общая технология изучается каждой фракцией отдельно (Faction::techs): своих отметок у неё нет.
      if (t.faction == 0 && (t.studied || t.research || t.progress)) {
        warn(F_TECHS, where, "общая технология изучается каждым государством отдельно — отметки изучения сняты");
        t.studied = t.research = false;
        t.progress = 0;
        ch = true;
      }
      // Уникальная технология зависит от своих и общих, общая — только от общих.
      ch |= fixRefList(t.prereqs, Seq::Tech, F_TECHS, where, "prereqs",
                       [&](Id id) { const Tech* p = base.tech(id); return p && id != t.id && (p->faction == t.faction || (t.faction && p->faction == 0)); },
                       "технология другой фракции, уникальная у общей или она сама");
      ch |= fixRefList(t.modifiers, Seq::Modifier, F_TECHS, where, "modifiers");
      ch |= fixPoint(t.pos, false, F_TECHS, where, "pos");
      ch |= fixAmounts(t.cost, Seq::Resource, resourceIds(), F_TECHS, where, "cost");
      ch |= fixAmounts(t.paid, Seq::Resource, resourceIds(), F_TECHS, where, "paid");
      if (!t.paid.empty() && (t.faction == 0 || !t.research)) {
        t.paid.clear();
        ch = true;
      }
      {
        std::vector<std::string> keys;
        for (const std::string& k : t.needKeys)
          if (!k.empty() && std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
        if (keys != t.needKeys) {
          t.needKeys = std::move(keys);
          ch = true;
        }
      }
      if (!t.key.empty() && !techKeys.insert({t.faction, t.key}).second) {
        warn(F_TECHS, where + ".key", "ключ «" + t.key + "» уже у другой технологии — снят");
        t.key.clear();
        ch = true;
      }
      if (ch) tx.tech(t.id) = std::move(t);
    });
  }
  const World base = w();
  auto ids = base.techs.ids();
  auto cut = cycleEdges(ids, [&](Id id) -> const std::vector<Id>& { return base.tech(id)->prereqs; });
  for (auto& [from, to] : cut) {
    warn(F_TECHS, refStr(Seq::Tech, from) + ".prereqs", "условие " + refStr(Seq::Tech, to) + " замыкает цикл — удалено");
    auto& pr = tx.tech(from).prereqs;
    pr.erase(std::remove(pr.begin(), pr.end(), to), pr.end());
  }
  // Изученная технология требует изученных предшествующих. Иначе изученность снимается (по цепочке —
  // и у зависящих от неё); пройденные ходы сохраняются.
  for (bool again = true; again;) {
    again = false;
    const World cur = w();
    auto studiedBy = [&](const Tech* pt, Id faction) {
      if (!pt) return true;
      if (pt->faction) return pt->studied;
      const Faction* f = cur.faction(faction);
      auto it = f ? f->techs.find(pt->id) : decltype(f->techs.end()){};
      return f && it != f->techs.end() && it->second.studied;
    };
    cur.techs.each([&](const Tech& t) {
      if (!t.studied || !t.faction) return;
      for (Id pid : t.prereqs) {
        if (studiedBy(cur.tech(pid), t.faction)) continue;
        warn(F_TECHS, refStr(Seq::Tech, t.id) + ".studied",
             "не изучено условие " + refStr(Seq::Tech, pid) + " — изученность снята");
        Tech& m = tx.tech(t.id);
        m.studied = false;
        m.research = false;
        m.progress = std::min(m.progress, std::max(0, m.turns - 1));
        again = true;
        return;
      }
    });
    // Общие технологии, изученные фракцией: их условия изучены этой же фракцией.
    cur.factions.each([&](const Faction& f) {
      for (auto& [tid, s] : f.techs) {
        if (!s.studied) continue;
        const Tech* t = cur.tech(tid);
        if (!t) continue;
        for (Id pid : t->prereqs) {
          if (studiedBy(cur.tech(pid), f.id)) continue;
          warn(F_FACTIONS, refStr(Seq::Faction, f.id) + ".techs." + refStr(Seq::Tech, tid),
               "не изучено условие " + refStr(Seq::Tech, pid) + " — изученность снята");
          TechProgress& m = tx.faction(f.id).techs[tid];
          m.studied = false;
          m.research = false;
          m.progress = std::min(m.progress, std::max(0, t->turns - 1));
          again = true;
          return;
        }
      }
    });
  }
}

}  // namespace rg::io::norm
