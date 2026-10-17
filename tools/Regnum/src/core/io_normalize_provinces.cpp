// Regnum — нормализация: провинции (гарнизон, население, влияние, штабы, постройки, рабы, оккупация,
// археологические места, тайник).
#include "core/arch.h"
#include "core/io_normalize.h"

namespace rg::io::norm {

void Norm::provinces() {
  const World base = w();
  base.provinces.each([&](const Province& p0) {
    Province p = p0;
    const std::string where = refStr(Seq::Province, p.id);
    bool ch = false;
    if (p.owner && !isState(p.owner)) {
      warn(F_PROVINCES, where + ".owner", exists(Seq::Faction, p.owner) ? refStr(Seq::Faction, p.owner) + " — гильдия, а владельцем может быть только государство: владелец удалён"
                                                                       : "нет фракции " + refStr(Seq::Faction, p.owner) + " — владелец удалён");
      p.owner = 0;
      ch = true;
    }
    ch |= fixRef(p.lord, Seq::Character, F_PROVINCES, where, "lord");
    ch |= fixEnum(p.size, 3, ProvSize::Medium, F_PROVINCES, where, "size");
    ch |= fixRef(p.resource, Seq::Resource, F_PROVINCES, where, "resource");
    ch |= fixNum(p.resourceAmount, 0.0, 1e12, 0.0, F_PROVINCES, where, "resourceAmount");

    // Гарнизон: строки армии владельца, без повторов (численности складываются).
    const Faction* owner = base.faction(p.owner);
    ch |= fixKeyed(
        p.garrison, F_PROVINCES, where, "garrison", rowOf,
        [&](GarrisonEntry& g, const std::string& at, bool& gch) {
          if (Id row = owner ? remapRow(owner->id, false, g.row) : g.row; row != g.row) {
            g.row = row;
            gch = true;
          }
          if (!owner) {
            warn(F_PROVINCES, at, "у провинции нет владельца — гарнизон удалён");
            return false;
          }
          if (!owner->armyRow(g.row)) {
            warn(F_PROVINCES, at + ".row", "у владельца " + refStr(Seq::Faction, owner->id) + " нет строки армии " + ref(Seq::Row, g.row) + " — запись удалена");
            return false;
          }
          gch |= fixNum(g.count, i64(0), kMaxCount, i64(0), F_PROVINCES, at, "count");
          return true;
        },
        rowsAdded, addCount);
    // Герои в гарнизоне: доступные герои владельца, не больше чем в одном гарнизоне или войске.
    ch |= fixRefList(p.garrisonHeroes, Seq::Character, F_PROVINCES, where, "garrisonHeroes",
                     [&](Id id) {
                       const Character* c = base.character(id);
                       if (!c || !p.owner || c->faction != p.owner || heroUsed.count(id)) return false;
                       for (Id m : c->modifiers)
                         if (const Modifier* x = base.modifier(m); x && (x->key == schema::mod::Dead || x->key == schema::mod::Captive)) return false;
                       return true;
                     },
                     "не доступный герой владельца или уже в другом гарнизоне");
    for (Id h : p.garrisonHeroes) heroUsed.insert(h);
    ch |= fixNum(p.garrisonLoyalty, schema::kMinLoyalty, schema::kMaxLoyalty, schema::kMaxLoyalty, F_PROVINCES, where, "garrisonLoyalty");

    ch |= fixNum(p.contentment, -100.0, 100.0, 0.0, F_PROVINCES, where, "contentment");
    ch |= fixRef(p.culture, Seq::Culture, F_PROVINCES, where, "culture");
    ch |= fixRef(p.religion, Seq::Religion, F_PROVINCES, where, "religion");

    ch |= fixKeyed(
        p.races, F_PROVINCES, where, "races", [](const RacePop& x) { return x.race; },
        [&](RacePop& r, const std::string& at, bool& rch) {
          if (!hasCat(Seq::Race, r.race)) {
            warn(F_PROVINCES, at + ".race", "нет расы " + ref(Seq::Race, r.race) + " — запись удалена");
            return false;
          }
          rch |= fixNum(r.pop, i64(0), kMaxCount, i64(0), F_PROVINCES, at, "pop");
          return true;
        },
        [](const RacePop& r) { return "повтор расы " + refStr(Seq::Race, r.race) + " — население сложено"; },
        [](RacePop& a, const RacePop& b) { a.pop = std::min(kMaxCount, a.pop + b.pop); });

    // Влияние гильдий: только гильдии, без повторов, 0…100, сумма ≤ 100.
    ch |= fixKeyed(
        p.influence, F_PROVINCES, where, "influence", [](const Influence& x) { return x.guild; },
        [&](Influence& x, const std::string& at, bool& ich) {
          if (!isGuild(x.guild)) {
            warn(F_PROVINCES, at + ".guild", exists(Seq::Faction, x.guild) ? refStr(Seq::Faction, x.guild) + " — не гильдия: запись удалена"
                                                                          : "нет гильдии " + ref(Seq::Faction, x.guild) + " — запись удалена");
            return false;
          }
          ich |= fixNum(x.pct, 0.0, 100.0, 0.0, F_PROVINCES, at, "pct");
          return true;
        },
        [](const Influence& x) { return "повтор гильдии " + refStr(Seq::Faction, x.guild) + " — влияние сложено"; },
        [](Influence& a, const Influence& b) { a.pct = std::min(100.0, a.pct + b.pct); });
    {
      double sum = 0;
      for (auto& x : p.influence) sum += x.pct;
      if (sum > 100.0 + 1e-9) {
        double k = 100.0 / sum;
        for (auto& x : p.influence) x.pct *= k;
        warn(F_PROVINCES, where + ".influence", "сумма влияния " + ns(sum) + " % больше 100 % — доли уменьшены пропорционально");
        ch = true;
      }
    }

    // Штабы: только гильдии, без повторов, не больше kMaxHqPerProvince.
    ch |= fixRefList(p.hqs, Seq::Faction, F_PROVINCES, where, "hqs", [&](Id id) { return isGuild(id); }, "не гильдия");
    if (int(p.hqs.size()) > schema::kMaxHqPerProvince) {
      warn(F_PROVINCES, where + ".hqs", "штабов " + std::to_string(p.hqs.size()) + ", допустимо не больше " +
                                            std::to_string(schema::kMaxHqPerProvince) + " — лишние удалены");
      p.hqs.resize(size_t(schema::kMaxHqPerProvince));
      ch = true;
    }

    ch |= fixNum(p.baseTrade, 0.0, 1e12, 0.0, F_PROVINCES, where, "baseTrade");
    ch |= fixNum(p.localTax, -100.0, 100.0, 0.0, F_PROVINCES, where, "localTax");

    if (!p.buildings.empty()) {
      std::vector<ProvBuilding> res;
      bool bch = false;
      for (size_t i = 0; i < p.buildings.size(); i++) {
        ProvBuilding b = p.buildings[i];
        std::string at = where + ".buildings[" + std::to_string(i) + "]";
        const Building* def = base.building(b.building);
        if (!def) {
          warn(F_PROVINCES, at + ".building", "нет постройки " + ref(Seq::Building, b.building) + " — запись удалена");
          bch = true;
          continue;
        }
        if (std::any_of(res.begin(), res.end(), [&](const ProvBuilding& x) { return x.building == b.building; })) {
          warn(F_PROVINCES, at, "повтор постройки " + refStr(Seq::Building, b.building) + " — удалён");
          bch = true;
          continue;
        }
        bch |= fixNum(b.level, 1, int(def->levels.size()), 1, F_PROVINCES, at, "level");
        bch |= fixNum(b.left, 0, kMaxTurns, 0, F_PROVINCES, at, "left");
        // Состояние преобразования — только у постройки преобразования; цикл не длиннее срока рецепта.
        if (!def->convert) {
          if (b.idle || b.cycle) {
            b.idle = false;
            b.cycle = 0;
            bch = true;
          }
        } else {
          int maxTurns = 1;
          for (const Recipe& rc : def->recipes) maxTurns = std::max(maxTurns, rc.turns);
          bch |= fixNum(b.cycle, 0, maxTurns, 0, F_PROVINCES, at, "cycle");
        }
        if (!b.constructing && b.left != 0) {
          b.left = 0;
          bch = true;
        }
        // Уплаченное за строящийся уровень: только у строящейся постройки; ресурсы каталога, числа ≥ 0;
        // плательщик — существующее государство (иначе возвращать некому).
        if (!b.constructing) {
          if (!b.paid.empty() || b.payer) {
            warn(F_PROVINCES, at + ".paid", "уплаченное указано у достроенной постройки — удалено");
            b.paid.clear();
            b.payer = 0;
            bch = true;
          }
        } else {
          bch |= fixAmounts(b.paid, Seq::Resource, resourceIds(), F_PROVINCES, at, "paid");
          if (b.payer && !isState(b.payer)) {
            warn(F_PROVINCES, at + ".payer", exists(Seq::Faction, b.payer) ? refStr(Seq::Faction, b.payer) + " — не государство: плательщик снят"
                                                                          : "нет фракции " + refStr(Seq::Faction, b.payer) + " — плательщик снят");
            b.payer = 0;
            bch = true;
          }
        }
        if (!b.constructing && !b.paidEss.empty()) {
          b.paidEss.clear();
          bch = true;
        }
        bch |= fixAmounts(b.paidEss, Seq::Essence, essenceIds(), F_PROVINCES, at, "paidEss");
        // Хранилище реликвий: только у постройки с этой возможностью; уникальная реликвия — в одном месте мира. Без
        // хранилища реликвии переходят в инвентарь государства-владельца (иначе — удаляются).
        if (!b.relics.empty()) {
          bch |= fixStacks(b.relics, F_PROVINCES, at, "relics");
          if (!def->relicStore && !b.relics.empty()) {
            if (isState(p.owner))
              for (const RelicStack& st : b.relics) orphanRelics[p.owner].push_back(st);
            warn(F_PROVINCES, at + ".relics", "постройка не хранилище реликвий — реликвии переданы государству-владельцу");
            b.relics.clear();
            bch = true;
          }
        }
        // Выбранный рецепт — из рецептов постройки; раса иссушения — из справочника.
        if (b.recipe < 0 || b.recipe >= std::max<int>(1, int(def->recipes.size()))) {
          b.recipe = 0;
          bch = true;
        }
        if (b.race && (!def->drain || !hasCat(Seq::Race, b.race))) {
          b.race = 0;
          bch = true;
        }
        res.push_back(b);
      }
      if (bch) { p.buildings = std::move(res); ch = true; }
    }
    ch |= fixRefList(p.modifiers, Seq::Modifier, F_PROVINCES, where, "modifiers");
    ch |= fixModTurns(p.modTurns, p.modifiers, F_PROVINCES, where);

    // Рабы на работах: раса справочника, без повторов; только у провинции с владельцем.
    ch |= fixKeyed(
        p.slaves, F_PROVINCES, where, "slaves", [](const SlaveWork& x) { return x.race; },
        [&](SlaveWork& x, const std::string& at, bool& sch) {
          if (!p.owner) {
            warn(F_PROVINCES, at, "у провинции нет владельца — рабы сняты с работ");
            return false;
          }
          if (!hasCat(Seq::Race, x.race)) {
            warn(F_PROVINCES, at + ".race", "нет расы " + ref(Seq::Race, x.race) + " — запись удалена");
            return false;
          }
          sch |= fixNum(x.count, i64(0), kMaxCount, i64(0), F_PROVINCES, at, "count");
          return true;
        },
        [](const SlaveWork& x) { return "повтор расы " + refStr(Seq::Race, x.race) + " — рабы сложены"; },
        [](SlaveWork& a, const SlaveWork& b) { a.count = std::min(kMaxCount, a.count + b.count); });

    if (p.occupied) {
      if (!isState(p.occupier) || p.occupier == p.owner) {
        warn(F_PROVINCES, where + ".occupier", p.occupier == 0 ? std::string("оккупация без оккупанта — снята")
                                               : p.occupier == p.owner ? "оккупант совпадает с владельцем — оккупация снята"
                                               : exists(Seq::Faction, p.occupier) ? refStr(Seq::Faction, p.occupier) + " — не государство: оккупация снята"
                                                                                  : "нет фракции " + refStr(Seq::Faction, p.occupier) + " — оккупация снята");
        p.occupied = false;
        p.occupier = 0;
        ch = true;
      }
    } else if (p.occupier) {
      warn(F_PROVINCES, where + ".occupier", "оккупант указан, но провинция не оккупирована — оккупант удалён");
      p.occupier = 0;
      ch = true;
    }
    // Оккупационный гарнизон: строки армии оккупанта; без оккупации — снимается.
    if (!p.occupied && (!p.occGarrison.empty() || p.occIdle)) {
      if (!p.occGarrison.empty()) warn(F_PROVINCES, where + ".occGarrison", "провинция не оккупирована — оккупационный гарнизон снят");
      p.occGarrison.clear();
      p.occIdle = 0;
      ch = true;
    }
    if (p.occupied) {
      const Faction* occ = base.faction(p.occupier);
      ch |= fixRowCounts(p.occGarrison, F_PROVINCES, where, "occGarrison", [&](Id row) { return occ && occ->armyRow(row) != nullptr; },
                         [&](Id row) { return occ ? remapRow(occ->id, false, row) : row; }, "у оккупанта нет строки армии");
      ch |= fixNum(p.occIdle, 0, kMaxTurns, 0, F_PROVINCES, where, "occIdle");
    }
    // Археологические места: существующие места без повторов, сокровища — сундуки справочника, этапы в пределах слота.
    {
      std::vector<Id> taken;
      bool bad = false;
      for (int i = 0; i < kArchSlots; i++) {
        ArchSlot& s = p.arch[size_t(i)];
        if (s.site && (!siteIds.count(s.site) || std::find(taken.begin(), taken.end(), s.site) != taken.end())) {
          s = ArchSlot{};
          bad = true;
        }
        if (s.site) taken.push_back(s.site);
        if (s.chest && !chestIds.count(s.chest)) {
          s.chest = 0;
          ch = true;
        }
        if (!s.site && (s.open || s.stage || s.chest)) {
          s = ArchSlot{};
          ch = true;
        }
        const int maxStage = s.open ? arch::stagesOf(i) : 0;
        if (s.stage < 0 || s.stage > maxStage) {
          s.stage = std::clamp(s.stage, 0, maxStage);
          ch = true;
        }
      }
      // Пустые слоты (провинция прежней версии или место удалено) заполняются по шансам мест (core/arch.h).
      if (!siteIds.empty() && std::any_of(p.arch.begin(), p.arch.end(), [](const ArchSlot& s) { return s.site == 0; })) {
        const auto rolled = arch::rollSlots(*w().catalogs, arch::provinceSeed(p.id));
        Rng rng(hashMix(arch::provinceSeed(p.id), 0x5245524F4C4Cull));
        for (int i = 0; i < kArchSlots; i++) {
          ArchSlot& s = p.arch[size_t(i)];
          if (s.site) continue;
          std::vector<Id> others;
          for (const ArchSlot& o : p.arch)
            if (o.site) others.push_back(o.site);
          Id site = std::find(others.begin(), others.end(), rolled[size_t(i)].site) == others.end() ? rolled[size_t(i)].site
                                                                                                   : arch::pickSite(arch::slotChances(*w().catalogs, i, others), rng);
          s = ArchSlot{};
          s.site = site;
          s.chest = site == rolled[size_t(i)].site ? rolled[size_t(i)].chest : arch::pickChest(*w().catalogs, site, i, rng);
        }
        if (bad) warn(F_PROVINCES, where + ".arch", "недопустимые или повторные археологические места заменены");
        ch = true;
      } else if (bad) {
        ch = true;
      }
    }
    // Тайник: существующая реликвия, которая больше нигде не лежит; спрятавшее государство — существующее государство.
    if (p.hiddenRelic) {
      const Relic* hr = w().relic(p.hiddenRelic);
      if (!hr || !relicIds.count(p.hiddenRelic) || (hr->unique() && relicTaken.count(p.hiddenRelic))) {
        warn(F_PROVINCES, where + ".hiddenRelic", ref(Seq::Relic, p.hiddenRelic) + ": нет реликвии или она уже в другом месте — тайник пуст");
        p.hiddenRelic = 0;
        ch = true;
      } else if (hr->unique()) {
        relicTaken.insert(p.hiddenRelic);
      }
    }
    ch |= fixNum(p.buildTurn, 0, 1000000, 0, F_PROVINCES, where, "buildTurn");
    if (!p.hiddenRelic && p.hiddenBy) {
      p.hiddenBy = 0;
      ch = true;
    }
    if (p.hiddenBy && !isState(p.hiddenBy)) {
      p.hiddenBy = 0;
      ch = true;
    }
    if (ch) tx.province(p.id) = std::move(p);
  });
  for (auto& [fid, list] : orphanRelics)
    for (const RelicStack& st : list) {
      auto& inv = tx.faction(fid).relics;
      auto it = std::find_if(inv.begin(), inv.end(), [&](const RelicStack& x) { return x.relic == st.relic; });
      if (it != inv.end()) it->count = std::min(kMaxCount, it->count + st.count);
      else inv.push_back(st);
    }
}

}  // namespace rg::io::norm
