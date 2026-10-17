// Regnum — нормализация: фракции (строки войск и флота, цены найма, совет, рабы, формирование, археологические
// группы, вассалитет) и персонажи (инвентарь, класс и таланты).
#include "core/arch.h"
#include "core/io_normalize.h"

namespace rg::io::norm {

void Norm::factions() {
  const World base = w();
  u32 maxRow = 0, maxSeat = 0;
  base.factions.each([&](const Faction& f) {
    for (auto& r : f.army) maxRow = std::max(maxRow, r.id);
    for (auto& r : f.fleet) maxRow = std::max(maxRow, r.id);
    for (auto& s : f.council) maxSeat = std::max(maxSeat, s.id);
  });
  std::unordered_set<Id> usedRows, usedSeats, usedArchGroups;
  u32 maxArchGroup = 0;
  base.factions.each([&](const Faction& f) {
    for (auto& g : f.archGroups) maxArchGroup = std::max(maxArchGroup, g.id);
  });
  base.factions.each([&](const Faction& f0) {
    Faction f = f0;
    const std::string where = refStr(Seq::Faction, f.id);
    bool ch = fixEnum(f.kind, 3, FactionKind::State, F_FACTIONS, where, "kind");

    // Строки войск и флота: ID уникальны во всём мире (общая последовательность u).
    auto rows = [&](auto& list, bool fleet, const char* field, int types) {
      std::unordered_set<Id> local;
      for (size_t i = 0; i < list.size(); i++) {
        auto& r = list[i];
        std::string at = where + "." + field + "[" + std::to_string(i) + "]";
        Id old = r.id;
        if (old == 0) {
          r.id = fresh(Seq::Row, maxRow);
          warn(F_FACTIONS, at, "строка без ID — назначен " + refStr(Seq::Row, r.id));
          ch = true;
        } else if (local.count(old)) {
          r.id = fresh(Seq::Row, maxRow);
          warn(F_FACTIONS, at, "повторный ID " + refStr(Seq::Row, old) + " в списке — назначен " + refStr(Seq::Row, r.id) + " (ссылки остаются на первую строку)");
          ch = true;
        } else if (usedRows.count(old)) {
          r.id = fresh(Seq::Row, maxRow);
          rowRemap[{f.id, fleet, old}] = r.id;
          warn(F_FACTIONS, at, "ID " + refStr(Seq::Row, old) + " уже занят другой строкой — назначен " + refStr(Seq::Row, r.id) + ", ссылки обновлены");
          ch = true;
        }
        local.insert(r.id);
        usedRows.insert(r.id);
        if (int(r.type) < 0 || int(r.type) >= types) {
          warn(F_FACTIONS, at + ".type", "недопустимый тип — взят по умолчанию");
          r.type = decltype(r.type)(fleet ? int(ShipType::Frigate) : 0);
          ch = true;
        }
        ch |= fixNum(r.total, i64(0), kMaxCount, i64(0), F_FACTIONS, at, "total");
        ch |= fixNum(r.upkeep, 0.0, 1e12, 0.0, F_FACTIONS, at, "upkeep");
      }
    };
    // Ключевой ресурс строки флота — только у морского чудовища (ресурс подгруппы «Морские чудовища»).
    for (size_t i = 0; i < f.fleet.size(); i++) {
      FleetRow& r = f.fleet[i];
      if (r.keyRes && !schema::shipKeyAllowed(*w().catalogs, r.type, r.keyRes)) {
        warn(F_FACTIONS, where + ".fleet[" + std::to_string(i) + "].keyRes",
             refStr(Seq::Resource, r.keyRes) + " не подходит ключевым ресурсом для «" + schema::shipType(r.type).name + "» — снят");
        r.keyRes = 0;
        ch = true;
      }
    }
    rows(f.army, false, "army", int(UnitType::Count));
    rows(f.fleet, true, "fleet", int(ShipType::Count));
    // Цена найма строк армии; строка особого отряда повторяет запись справочника.
    for (size_t i = 0; i < f.army.size(); i++) {
      ArmyRow& r = f.army[i];
      const std::string at = where + ".army[" + std::to_string(i) + "]";
      if (r.special && !specialIds.count(r.special)) {
        warn(F_FACTIONS, at + ".special", "нет особого отряда " + refStr(Seq::Special, r.special) + " — строка стала обычной");
        r.special = 0;
        ch = true;
      }
      // Наёмники: золото за найм ≥ 0; только пехота и кавалерия, раса «Наемники», без особого отряда.
      ch |= fixNum(r.hire, 0.0, 1e12, 0.0, F_FACTIONS, at, "hire");
      if (r.merc) {
        if (!schema::isMercType(r.type) || r.special) {
          warn(F_FACTIONS, at + ".merc", "наёмники — только пехота и кавалерия без особого отряда: признак снят");
          r.merc = false;
          ch = true;
        } else {
          if (r.race != schema::kRaceMercenary) {
            r.race = schema::kRaceMercenary;
            ch = true;
          }
          if (r.keyRes || !r.extra.empty() || !r.essence.empty() || !r.essUpkeep.empty()) {
            r.keyRes = 0;
            r.keyPer = 1;
            r.extra.clear();
            r.essence.clear();
            r.essUpkeep.clear();
            ch = true;
          }
          continue;
        }
      } else if (r.hire != 0) {
        r.hire = 0;
        ch = true;
      }
      if (const SpecialUnit* s = r.special ? w().catalogs->special(r.special) : nullptr) {
        if (r.type != s->type || r.race != s->race || r.keyRes != s->keyRes || r.keyPer != s->keyPer || r.extra != s->extra ||
            r.essence != s->essence || r.upkeep != s->upkeep || r.essUpkeep != s->essUpkeep) {
          r.type = s->type;
          r.race = s->race;
          r.keyRes = s->keyRes;
          r.keyPer = s->keyPer;
          r.extra = s->extra;
          r.essence = s->essence;
          r.upkeep = s->upkeep;
          r.essUpkeep = s->essUpkeep;
          ch = true;
        }
        continue;
      }
      ch |= fixUnitCost(r, *w().catalogs, resourceIds(), F_FACTIONS, at);
    }
    // Эссенции, недостача провизии, изучение общих технологий.
    ch |= fixAmountsSigned(f.ess, Seq::Essence, essenceIds(), F_FACTIONS, where, "ess");
    ch |= fixNum(f.provisionDebt, 0.0, 1e15, 0.0, F_FACTIONS, where, "provisionDebt");
    for (auto it = f.techs.begin(); it != f.techs.end();) {
      const Tech* t = base.tech(it->first);
      if (!t || t->faction != 0) {
        warn(F_FACTIONS, where + ".techs", (t ? refStr(Seq::Tech, it->first) + " — не общая технология" : "нет технологии " + refStr(Seq::Tech, it->first)) +
                                              std::string(" — запись удалена"));
        it = f.techs.erase(it);
        ch = true;
        continue;
      }
      TechProgress& s = it->second;
      ch |= fixNum(s.progress, 0, kMaxTurns, 0, F_FACTIONS, where + ".techs." + refStr(Seq::Tech, it->first), "progress");
      if (s.studied && s.research) {
        s.research = false;
        ch = true;
      }
      if (!s.research && !s.paid.empty()) {
        s.paid.clear();
        ch = true;
      }
      ch |= fixAmounts(s.paid, Seq::Resource, resourceIds(), F_FACTIONS, where + ".techs." + refStr(Seq::Tech, it->first), "paid");
      if (s == TechProgress{}) {
        it = f.techs.erase(it);
        ch = true;
        continue;
      }
      ++it;
    }

    // Археологические группы: ID уникальны во всём мире, опыт 0…1000, модификаторы — только археологических групп.
    for (size_t i = 0; i < f.archGroups.size(); i++) {
      ArchGroup& g = f.archGroups[i];
      const std::string at = where + ".archGroups[" + std::to_string(i) + "]";
      if (g.id == 0 || usedArchGroups.count(g.id)) {
        Id old = g.id;
        g.id = fresh(Seq::ArchGroup, maxArchGroup);
        warn(F_FACTIONS, at, old ? "повторный ID " + refStr(Seq::ArchGroup, old) + " — назначен " + refStr(Seq::ArchGroup, g.id)
                                 : "группа без ID — назначен " + refStr(Seq::ArchGroup, g.id));
        ch = true;
      }
      usedArchGroups.insert(g.id);
      ch |= fixNum(g.exp, 0, arch::kMaxExp, 0, F_FACTIONS, at, "exp");
      ch |= fixNum(g.busy, 0, kMaxTurn, 0, F_FACTIONS, at, "busy");
      ch |= fixRefList(g.modifiers, Seq::Modifier, F_FACTIONS, at, "modifiers",
                       [&](Id id) { const Modifier* m = base.modifier(id); return m && m->kind == ModKind::ArchGroup; },
                       "не модификатор археологических групп");
      ch |= fixModTurns(g.modTurns, g.modifiers, F_FACTIONS, at);
    }
    if (!f.isState() && !f.archGroups.empty()) {
      warn(F_FACTIONS, where + ".archGroups", "археологические группы бывают только у государства — удалены");
      f.archGroups.clear();
      ch = true;
    }
    // Инвентарь государства: стопки реликвий (уникальная — в одном месте мира).
    ch |= fixStacks(f.relics, F_FACTIONS, where, "relics");

    for (size_t i = 0; i < f.council.size(); i++) {
      auto& s = f.council[i];
      std::string at = where + ".council[" + std::to_string(i) + "]";
      if (s.id == 0 || usedSeats.count(s.id)) {
        Id old = s.id;
        s.id = fresh(Seq::Council, maxSeat);
        warn(F_FACTIONS, at, old ? "повторный ID " + refStr(Seq::Council, old) + " — назначен " + refStr(Seq::Council, s.id)
                                 : "место без ID — назначен " + refStr(Seq::Council, s.id));
        ch = true;
      }
      usedSeats.insert(s.id);
      ch |= fixRef(s.character, Seq::Character, F_FACTIONS, at, "character");
    }

    if (f.flag.pattern >= FlagPattern::Count) {
      warn(F_FACTIONS, where + ".flag.pattern", "недопустимый узор — взят однотонный");
      f.flag.pattern = FlagPattern::Solid;
      ch = true;
    }
    if (!f.flag.png.empty() && !isPng(f.flag.png)) {
      warn(F_FACTIONS, where + ".flag.png", "данные флага не являются PNG — изображение удалено");
      f.flag.png.clear();
      f.flag.image = false;
      ch = true;
    }
    if (f.flag.image && f.flag.png.empty()) {
      warn(F_FACTIONS, where + ".flag.image", "флаг-изображение без PNG — включён конструктор");
      f.flag.image = false;
      ch = true;
    }

    ch |= fixRef(f.culture, Seq::Culture, F_FACTIONS, where, "culture");
    ch |= fixRef(f.government, Seq::Government, F_FACTIONS, where, "government");
    ch |= fixRef(f.religion, Seq::Religion, F_FACTIONS, where, "religion");
    ch |= fixRef(f.ruler, Seq::Character, F_FACTIONS, where, "ruler");
    ch |= fixRef(f.capital, Seq::Province, F_FACTIONS, where, "capital");

    ch |= fixAmountsSigned(f.res, Seq::Resource, resourceIds(), F_FACTIONS, where, "res");
    ch |= fixRefList(f.modifiers, Seq::Modifier, F_FACTIONS, where, "modifiers");
    ch |= fixModTurns(f.modTurns, f.modifiers, F_FACTIONS, where);
    ch |= fixNum(f.tax, 0.0, 100.0, 10.0, F_FACTIONS, where, "tax");
    ch |= fixEnum(f.stateKind, int(StateKind::Count), StateKind::Living, F_FACTIONS, where, "stateKind");
    ch |= fixNum(f.pirateRisk, 0.0, 100.0, 0.0, F_FACTIONS, where, "pirateRisk");

    // Рабы: раса справочника, численность 0…kMaxCount, довольство −100…100, повторы рас складываются.
    ch |= fixKeyed(
        f.slaves, F_FACTIONS, where, "slaves", [](const SlaveGroup& x) { return x.race; },
        [&](SlaveGroup& x, const std::string& at, bool& sch) {
          if (!hasCat(Seq::Race, x.race)) {
            warn(F_FACTIONS, at + ".race", "нет расы " + ref(Seq::Race, x.race) + " — рабы удалены");
            return false;
          }
          sch |= fixNum(x.count, i64(0), kMaxCount, i64(0), F_FACTIONS, at, "count");
          sch |= fixNum(x.contentment, -100.0, 100.0, 0.0, F_FACTIONS, at, "contentment");
          return true;
        },
        [](const SlaveGroup& x) { return "повтор расы " + refStr(Seq::Race, x.race) + " — рабы сложены"; },
        [](SlaveGroup& a, const SlaveGroup& b) { a.count = std::min(kMaxCount, a.count + b.count); });
    // Формирование отрядов и кораблей: строка фракции, число > 0, 1…kMaxTurns ходов, уплаченное — ресурсы справочника.
    if (!f.forming.empty()) {
      std::vector<Formation> res;
      bool fch = false;
      for (size_t i = 0; i < f.forming.size(); i++) {
        Formation q = f.forming[i];
        std::string at = where + ".forming[" + std::to_string(i) + "]";
        Id ra = remapRow(f.id, false, q.row), rf = remapRow(f.id, true, q.row);
        Id row = f.armyRow(ra) ? ra : rf;
        if (row != q.row) { q.row = row; fch = true; }
        if (!f.armyRow(q.row) && !f.fleetRow(q.row)) {
          warn(F_FACTIONS, at + ".row", "нет строки " + ref(Seq::Row, q.row) + " — формирование удалено");
          fch = true;
          continue;
        }
        if (q.count <= 0) {
          warn(F_FACTIONS, at + ".count", "численность " + std::to_string(q.count) + " — формирование удалено");
          fch = true;
          continue;
        }
        fch |= fixNum(q.count, i64(1), kMaxCount, i64(1), F_FACTIONS, at, "count");
        fch |= fixNum(q.left, 1, kMaxTurns, 1, F_FACTIONS, at, "left");
        fch |= fixNum(q.people, i64(0), kMaxCount, i64(0), F_FACTIONS, at, "people");
        fch |= fixAmounts(q.paidEss, Seq::Essence, essenceIds(), F_FACTIONS, at, "paidEss");
        fch |= fixAmounts(q.paid, Seq::Resource, resourceIds(), F_FACTIONS, at, "paid");
        res.push_back(std::move(q));
      }
      if (fch) { f.forming = std::move(res); ch = true; }
    }
    // Корабли в торговле: строки флота фракции.
    ch |= fixRowCounts(f.tradeFleet, F_FACTIONS, where, "tradeFleet", [&](Id row) { return f.fleetRow(row) != nullptr; },
                       [&](Id row) { return remapRow(f.id, true, row); }, "нет строки флота");

    if (f.isState()) {
      if (f.homeState) {
        warn(F_FACTIONS, where + ".homeState", "у государства не бывает государства расположения — удалено");
        f.homeState = 0;
        ch = true;
      }
      if (f.stateGuild) {
        warn(F_FACTIONS, where + ".stateGuild", "признак государственной гильдии у государства — снят");
        f.stateGuild = false;
        ch = true;
      }
      // Сюзерен и мятежное происхождение — другое существующее государство.
      for (auto [field, ptr] : {std::pair{"suzerain", &f.suzerain}, std::pair{"rebelOf", &f.rebelOf}}) {
        Id& v = *ptr;
        if (!v) continue;
        if (v == f.id || !isState(v)) {
          warn(F_FACTIONS, where + "." + field, v == f.id ? std::string("ссылка на само государство — удалена")
                                                : exists(Seq::Faction, v) ? refStr(Seq::Faction, v) + " — не государство: ссылка удалена"
                                                                          : "нет фракции " + refStr(Seq::Faction, v) + " — ссылка удалена");
          v = 0;
          ch = true;
        }
      }
    } else {
      if (f.homeState && !isState(f.homeState)) {
        warn(F_FACTIONS, where + ".homeState", exists(Seq::Faction, f.homeState) ? refStr(Seq::Faction, f.homeState) + " — не государство: ссылка удалена"
                                                                               : "нет фракции " + refStr(Seq::Faction, f.homeState) + " — ссылка удалена");
        f.homeState = 0;
        ch = true;
      }
      // У гильдии нет вида государства, вассалитета и признака основного государства.
      if (f.mainState || f.suzerain || f.rebelOf || f.stateKind != StateKind::Living || !f.slaves.empty()) {
        warn(F_FACTIONS, where, "у гильдии не бывает вида государства, сюзерена, рабов и признака основного государства — сняты");
        f.mainState = false;
        f.suzerain = f.rebelOf = 0;
        f.stateKind = StateKind::Living;
        f.slaves.clear();
        ch = true;
      }
    }
    if (ch) tx.faction(f.id) = std::move(f);
  });
  // Вассалитет без циклов: сюзерен не может быть (прямо или через цепочку) вассалом своего вассала.
  const World cur = w();
  cur.factions.each([&](const Faction& f) {
    if (chainReaches(f.suzerain, f.id, 64, [&](Id s) { const Faction* x = tx.w().faction(s); return x ? x->suzerain : Id(0); })) {
      warn(F_FACTIONS, refStr(Seq::Faction, f.id) + ".suzerain", "цепочка вассалитета замыкается на само государство — сюзерен снят");
      tx.faction(f.id).suzerain = 0;
    }
  });
}

void Norm::characters() {
  const World base = w();
  base.characters.each([&](const Character& c0) {
    Character c = c0;
    const std::string where = refStr(Seq::Character, c.id);
    bool ch = fixRef(c.faction, Seq::Faction, F_CHARACTERS, where, "faction");
    ch |= fixNum(c.upkeep, 0.0, 1e12, 0.0, F_CHARACTERS, where, "upkeep");
    if (!c.portrait.empty() && !isPng(c.portrait) && !isJpeg(c.portrait)) {
      warn(F_CHARACTERS, where + ".portrait", "портрет не является PNG или JPEG — удалён");
      c.portrait.clear();
      ch = true;
    }
    ch |= fixRefList(c.modifiers, Seq::Modifier, F_CHARACTERS, where, "modifiers");
    ch |= fixModTurns(c.modTurns, c.modifiers, F_CHARACTERS, where);
    if (c.captor && (!isState(c.captor) || c.captor == c.faction)) {
      warn(F_CHARACTERS, where + ".captor", c.captor == c.faction ? std::string("пленён собственным государством — ссылка удалена")
                                            : exists(Seq::Faction, c.captor) ? refStr(Seq::Faction, c.captor) + " — не государство: ссылка удалена"
                                                                             : "нет фракции " + refStr(Seq::Faction, c.captor) + " — ссылка удалена");
      c.captor = 0;
      ch = true;
    }
    ch |= fixRef(c.burial, Seq::Province, F_CHARACTERS, where, "burial");
    // Инвентарь: стопки реликвий справочника; уникальная — только у одного персонажа (первого по ID).
    ch |= fixStacks(c.inventory, F_CHARACTERS, where, "inventory");
    // Уровень 1…60, класс справочника, подкласс — своего класса (у прежнего мира герой класса с единственным
    // подклассом получает его); таланты — ранги дерева подкласса, по порядку изучения законные (условие изучено
    // полностью, ярус открыт, рангов не больше наибольшего, очков не больше уровня).
    ch |= fixNum(c.level, 1, schema::kMaxHeroLevel, 1, F_CHARACTERS, where, "level");
    ch |= fixRef(c.heroClass, Seq::HeroClass, F_CHARACTERS, where, "heroClass");
    const HeroClass* hc = base.heroClass(c.heroClass);
    if (c.subclass && (!hc || !hc->subclass(c.subclass))) {
      warn(F_CHARACTERS, where + ".subclass", "подкласс " + refStr(Seq::SubClass, c.subclass) + " не из класса героя — снят");
      c.subclass = 0;
      ch = true;
    }
    if (!c.subclass && hc && hc->subclasses.size() == 1 && !c.talents.empty()) {
      c.subclass = hc->subclasses[0].id;
      ch = true;
    }
    const SubClass* sc = hc ? hc->subclass(c.subclass) : nullptr;
    if (!sc && !c.talents.empty()) {
      warn(F_CHARACTERS, where + ".talents", "у героя не выбран подкласс — таланты сняты");
      c.talents.clear();
      ch = true;
    }
    if (sc) {
      std::vector<Id> keep = lawfulTalents(*sc, c.talents, c.level);
      if (keep.size() != c.talents.size()) {
        warn(F_CHARACTERS, where + ".talents", "таланты, которые нельзя изучить (дерево подкласса, условие, порог яруса, ранги, очки уровня), сняты");
        c.talents = std::move(keep);
        ch = true;
      }
    }
    if (ch) tx.character(c.id) = std::move(c);
  });
}

}  // namespace rg::io::norm
