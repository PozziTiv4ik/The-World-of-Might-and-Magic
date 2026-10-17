// Regnum — окно модификаторов: где используется модификатор (провинции, фракции, войска, герои, технологии, постройки,
// гильдии, археологические группы) — подсчёт, подпись и раздел карточки со ссылками и добавлением.
#include "app/editors/modifiers_internal.h"

namespace rg::app::modedit {

// ---------------------------------------------------------------- использование
ModUse usageOf(const World& w, Id mod) {
  ModUse u;
  if (!mod) return u;
  w.provinces.each([&](const Province& p) {
    if (has(p.modifiers, mod)) u.provinces.push_back(p.id);
  });
  w.factions.each([&](const Faction& f) {
    if (has(f.modifiers, mod)) (f.isGuild() ? u.guilds : u.states).push_back(f.id);
    for (const ArchGroup& g : f.archGroups)
      if (has(g.modifiers, mod)) u.groups.push_back({f.id, g.id});
  });
  w.techs.each([&](const Tech& t) {
    if (has(t.modifiers, mod)) u.techs.push_back(t.id);
  });
  w.buildings.each([&](const Building& b) {
    for (size_t i = 0; i < b.levels.size(); i++)
      if (has(b.levels[i].modifiers, mod)) u.levels.push_back({b.id, int(i) + 1});
  });
  w.armies.each([&](const Army& a) {
    if (has(a.modifiers, mod)) u.armies.push_back(a.id);
  });
  w.characters.each([&](const Character& c) {
    if (has(c.modifiers, mod)) u.heroes.push_back(c.id);
  });
  for (const CatalogItem& p : w.catalogs->positions)
    if (has(p.modifiers, mod) || has(p.vacantModifiers, mod)) u.positions.push_back(p.id);
  std::sort(u.provinces.begin(), u.provinces.end(), [&](Id x, Id y) { return compareRu(w.provinceName(x), w.provinceName(y)) < 0; });
  std::sort(u.states.begin(), u.states.end(), [&](Id x, Id y) { return compareRu(w.factionName(x), w.factionName(y)) < 0; });
  std::sort(u.guilds.begin(), u.guilds.end(), [&](Id x, Id y) { return compareRu(w.factionName(x), w.factionName(y)) < 0; });
  std::sort(u.heroes.begin(), u.heroes.end(), [&](Id x, Id y) { return compareRu(w.characterName(x), w.characterName(y)) < 0; });
  return u;
}

// Число мест использования каждого модификатора (для списка).
std::unordered_map<Id, int> usageCounts(const World& w) {
  std::unordered_map<Id, int> n;
  w.provinces.each([&](const Province& p) {
    for (Id m : p.modifiers) n[m]++;
  });
  w.factions.each([&](const Faction& f) {
    for (Id m : f.modifiers) n[m]++;
    for (const ArchGroup& g : f.archGroups)
      for (Id m : g.modifiers) n[m]++;
  });
  w.techs.each([&](const Tech& t) {
    for (Id m : t.modifiers) n[m]++;
  });
  w.buildings.each([&](const Building& b) {
    for (auto& l : b.levels)
      for (Id m : l.modifiers) n[m]++;
  });
  w.armies.each([&](const Army& a) {
    for (Id m : a.modifiers) n[m]++;
  });
  w.characters.each([&](const Character& c) {
    for (Id m : c.modifiers) n[m]++;
  });
  for (const CatalogItem& p : w.catalogs->positions) {
    for (Id m : p.modifiers) n[m]++;
    for (Id m : p.vacantModifiers)
      if (!has(p.modifiers, m)) n[m]++;
  }
  return n;
}

// Где сейчас действуют модификаторы, которые ставятся сами (ключ → государства; модификаторы провинций — столица,
// «Ценности археологии» — провинции).
std::map<std::string, std::vector<Id>> autoUse(const World& w) {
  std::map<std::string, std::vector<Id>> out;
  w.factions.each([&](const Faction& f) {
    if (!f.isState()) return;
    for (const rules::AutoMod& am : rules::autoModifiers(w, f.id))
      if (!am.key.empty() && !has(out[am.key], f.id)) out[am.key].push_back(f.id);
  });
  w.provinces.each([&](const Province& p) {
    if (p.sea || !p.owner) return;
    for (const rules::AutoMod& am : rules::autoProvinceModifiers(w, p.id))
      if (!am.key.empty()) out[am.key].push_back(p.id);
  });
  return out;
}

std::string usageText(const ModUse& u) {
  std::vector<std::string> parts;
  auto add = [&](size_t n, const char* one, const char* few, const char* many) {
    if (n) parts.push_back(fmtInt(i64(n)) + "\xC2\xA0" + plural(i64(n), one, few, many));
  };
  add(u.provinces.size(), "провинция", "провинции", "провинций");
  add(u.states.size(), "государство", "государства", "государств");
  add(u.guilds.size(), "гильдия", "гильдии", "гильдий");
  add(u.armies.size(), "войско", "войска", "войск");
  add(u.heroes.size(), "персонаж", "персонажа", "персонажей");
  add(u.positions.size(), "должность", "должности", "должностей");
  add(u.groups.size(), "археологическая группа", "археологические группы", "археологических групп");
  add(u.techs.size(), "технология", "технологии", "технологий");
  add(u.levels.size(), "уровень постройки", "уровня построек", "уровней построек");
  std::string s;
  for (size_t i = 0; i < parts.size(); i++) s += (i ? ", " : "") + parts[i];
  return s;
}

namespace {

int effectCount(const Modifier& m) {
  int n = 0;
  for (int f = 0; f < kFxCount; f++) n += m.has(Fx(f)) ? 1 : 0;
  return n;
}

}  // namespace

bool hasLocal(const Modifier& m) {
  for (int f = 0; f < kFxCount; f++)
    if (m.has(Fx(f)) && schema::kEffects[f].local) return true;
  return false;
}
// Глобальные эффекты и эффекты войск (у государства они действуют на все его войска).
bool hasGlobal(const Modifier& m) {
  for (int f = 0; f < kFxCount; f++)
    if (m.has(Fx(f)) && !schema::kEffects[f].local) return true;
  return false;
}
// Дипломатия включена, а цели не выбраны (ТЗ 1.g.ii.2.b: «с указанными для модификатора государствами»).
bool missingTargets(const Modifier& m) { return m.has(Fx::DiplomacyPerTurn) && m.targets.empty(); }

// Краткая сводка эффектов для строки списка.
std::string effectSummary(const World& w, const Modifier& m) {
  int n = effectCount(m);
  const std::vector<std::string> gen = edkit::genTexts(w, m);
  n += int(gen.size());
  if (n == 0) return isAuto(m) ? "Ставится сам" : "Без эффектов";
  std::string s;
  for (int f = 0; f < kFxCount && s.empty(); f++)
    if (m.has(Fx(f))) s = w::effectText(Fx(f), m.fx[size_t(f)]);
  if (s.empty()) s = gen.front() + " за ход";
  if (n > 1) s += " · ещё " + std::to_string(n - 1);
  return s;
}

namespace {

// ---------------------------------------------------------------- карточка: «Где используется»
ui::ChipAction useChip(std::string_view label, const char* icon, Color dot, std::string_view tip, bool removable) {
  ui::ChipOpt co;
  co.icon = icon;
  co.color = dot;
  co.clickable = true;
  co.removable = removable;
  co.tooltip = tip;
  return ui::flowChip(label, co);
}

const EditorDef* editorLike(std::string_view part) {
  for (auto& e : editors())
    if (std::string_view(e.id).find(part) != std::string_view::npos) return &e;
  return nullptr;
}

// Добавить модификатор сущности по правилам (вид, срок по умолчанию, взаимоисключения); встроенный — с записью мира.
void addTo(App& a, const Modifier& m, ModTarget t, Id target, std::string_view label) {
  Id made = 0;
  if (a.act(label, [&](Tx& tx) {
        made = recordOf(tx, m);
        rules::addModifier(tx, t, target, made);
      }) &&
      !m.id && made && a.ui.editor == "modifiers")
    a.ui.editorArg = made;
}

// Выбор сущности для добавления модификатора (ui::combo с поиском). true — выбрана, id — её ID.
bool addCombo(App& a, std::string_view id, const char* mark, const std::vector<std::pair<Id, std::string>>& items, const std::vector<Color>& colors,
              std::string_view placeholder, const char* icon, std::string_view tip, Id& out) {
  int idx = -1;
  bool r = ui::combo(id, idx, int(items.size()),
                     [&](int i) { return ui::Option{items[size_t(i)].second, icon, colors[size_t(i)]}; },
                     {.placeholder = placeholder, .search = 1, .icon = "plus", .disabled = items.empty(), .tooltip = tip}) &&
           idx >= 0 && idx < int(items.size());
  a.markUi(mark);
  if (r) out = items[size_t(idx)].first;
  return r;
}

}  // namespace

void usageSection(App& a, const Modifier& m, const std::map<std::string, std::vector<Id>>& autos, bool ro) {
  ui::IdScope scope("usage");
  const World& w = a.world();
  ModUse u = usageOf(w, m.id);
  const Id mid = m.id;
  const bool automatic = isAuto(m);
  const std::vector<Id>* now = nullptr;
  if (automatic)
    if (auto it = autos.find(m.key); it != autos.end()) now = &it->second;
  std::string badge = std::to_string(u.total() + (now ? now->size() : 0));
  ui::Section sec("Где используется", "link", {.badge = badge});
  a.markUi("modifiers.usage");
  if (!sec) return;
  if (u.total() == 0 && !now) ui::label(automatic ? "Сейчас нигде не действует" : "Пока нигде", {.ink = ui::Ink::Muted});
  auto group = [&](const char* title, size_t n) {
    if (!n) return false;
    ui::caption(std::string(title) + " · " + std::to_string(n));
    return true;
  };
  // Подпись фишки с оставшимся сроком: «Эльвенмор · 3» (нет записи срока — бессрочно).
  auto termed = [&](std::string name, const ModTurns& turns) {
    if (auto it = turns.find(mid); it != turns.end() && it->second > 0) name += " · " + std::to_string(it->second);
    return name;
  };
  // Автоматический — где действует сейчас (государства; столица — провинции).
  if (now && group("Действует сейчас", now->size())) {
    ui::ChipFlow cf;
    for (Id id : *now) {
      ui::IdScope s{i64(id) + 0x500000};
      if (m.kind == ModKind::Province) {
        const Province* p = w.province(id);
        if (useChip(orName(p ? p->name : std::string(), "Без названия"), "province", w::factionColor(w, p ? p->owner : 0), "Открыть провинцию",
                    false) == ui::ChipAction::Click)
          edkit::goTo(a, {SelType::Province, id});
      } else if (useChip(w.factionName(id), "crown", w::factionColor(w, id), "Открыть государство", false) == ui::ChipAction::Click) {
        edkit::goTo(a, {SelType::Faction, id});
      }
    }
  }
  if (group("Провинции", u.provinces.size())) {
    if (!hasLocal(m) && hasGlobal(m)) ui::tag("Глобальные эффекты в провинции не действуют", ui::Tone::Warning, "warning");
    ui::ChipFlow cf;
    for (Id pid : u.provinces) {
      const Province* p = w.province(pid);
      ui::IdScope s{i64(pid)};
      auto act = useChip(p ? termed(orName(p->name, "Без названия"), p->modTurns) : std::string("Без названия"), "province",
                         w::factionColor(w, p ? p->owner : 0), "Открыть провинцию", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Province, pid});
      if (act == ui::ChipAction::Remove) a.act("Убрать модификатор провинции", [&](Tx& tx) { rules::dropModifier(tx, ModTarget::Province, pid, mid); });
    }
  }
  auto factionsGroup = [&](const char* title, const std::vector<Id>& ids, bool guilds) {
    if (!group(title, ids.size())) return;
    ui::ChipFlow cf;
    for (Id fid : ids) {
      const Faction* f = w.faction(fid);
      ui::IdScope s{i64(fid)};
      auto act = useChip(f ? termed(orName(f->name, "Без названия"), f->modTurns) : std::string("Без названия"), guilds ? "guild" : "crown",
                         w::factionColor(w, fid), guilds ? "Открыть гильдию" : "Открыть государство", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, fid});
      if (act == ui::ChipAction::Remove)
        a.act(guilds ? "Убрать модификатор гильдии" : "Убрать модификатор государства",
              [&](Tx& tx) { rules::dropModifier(tx, ModTarget::Faction, fid, mid); });
    }
  };
  factionsGroup("Государства", u.states, false);
  factionsGroup("Торговые гильдии", u.guilds, true);
  if (group("Войска и флот", u.armies.size())) {
    ui::ChipFlow cf;
    for (Id aid : u.armies) {
      const Army* ar = w.army(aid);
      if (!ar) continue;
      ui::IdScope s{i64(aid) + 0x600000};
      auto act = useChip(termed(orName(ar->name, ar->isFleet() ? "Флот" : "Войско"), ar->modTurns), ar->isFleet() ? "fleet" : "army",
                         w::factionColor(w, ar->leader()), ar->isFleet() ? "Открыть флот" : "Открыть войско", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Army, aid});
      if (act == ui::ChipAction::Remove) a.act("Убрать модификатор войска", [&](Tx& tx) { rules::dropModifier(tx, ModTarget::Army, aid, mid); });
    }
  }
  if (group("Персонажи", u.heroes.size())) {
    ui::ChipFlow cf;
    for (Id cid : u.heroes) {
      const Character* c = w.character(cid);
      if (!c) continue;
      ui::IdScope s{i64(cid) + 0x700000};
      auto act = useChip(termed(orName(c->name, "Без имени"), c->modTurns), c->hero ? "hero" : "character", w::factionColor(w, c->faction),
                         "Открыть персонажа", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Character, cid});
      if (act == ui::ChipAction::Remove) a.act("Убрать модификатор персонажа", [&](Tx& tx) { rules::dropModifier(tx, ModTarget::Character, cid, mid); });
    }
  }
  if (group("Должности", u.positions.size())) {
    ui::ChipFlow cf;
    for (Id pid : u.positions) {
      const CatalogItem* p = Catalogs::find(w.catalogs->positions, pid);
      if (!p) continue;
      ui::IdScope s{i64(pid) + 0x800000};
      const bool taken = has(p->modifiers, mid), vacant = has(p->vacantModifiers, mid);
      std::string label = orName(p->name, "Без названия") + (taken && vacant ? "" : taken ? " · занята" : " · пустует");
      auto act = useChip(label, "council", Color(0, 0, 0, 0), "Открыть справочник должностей", !ro);
      if (act == ui::ChipAction::Click) later(a, [](App& x) { x.openEditor("catalogs", 6); });
      if (act == ui::ChipAction::Remove)
        a.act("Убрать модификатор должности", [&](Tx& tx) {
          for (CatalogItem& c : tx.catalogs().positions)
            if (c.id == pid) {
              c.modifiers.erase(std::remove(c.modifiers.begin(), c.modifiers.end(), mid), c.modifiers.end());
              c.vacantModifiers.erase(std::remove(c.vacantModifiers.begin(), c.vacantModifiers.end(), mid), c.vacantModifiers.end());
            }
        });
    }
  }
  if (group("Археологические группы", u.groups.size())) {
    ui::ChipFlow cf;
    for (auto [fid, gid] : u.groups) {
      const Faction* f = w.faction(fid);
      const ArchGroup* g = f ? f->archGroup(gid) : nullptr;
      if (!g) continue;
      ui::IdScope s{i64(gid) + 0x900000};
      auto act = useChip(termed(orName(g->name, "Группа"), g->modTurns) + " · " + orName(f->name, "Без названия"), "pickaxe", w::factionColor(w, fid),
                         "Открыть государство", !ro);
      if (act == ui::ChipAction::Click) edkit::goTo(a, {SelType::Faction, fid});
      if (act == ui::ChipAction::Remove)
        a.act("Убрать модификатор археологической группы", [&](Tx& tx) {
          for (ArchGroup& x : tx.faction(fid).archGroups)
            if (x.id == gid) {
              x.modifiers.erase(std::remove(x.modifiers.begin(), x.modifiers.end(), mid), x.modifiers.end());
              x.modTurns.erase(mid);
            }
        });
    }
  }
  if (group("Технологии", u.techs.size())) {
    ui::ChipFlow cf;
    for (Id tid : u.techs) {
      const Tech* t = w.tech(tid);
      if (!t) continue;
      ui::IdScope s{i64(tid)};
      std::string label = orName(t->name, "Без названия") + " · " + w.factionName(t->faction);
      auto act = useChip(label, "tech", w::factionColor(w, t->faction), "Открыть дерево технологий", !ro);
      if (act == ui::ChipAction::Click) {
        Id fac = t->faction;
        if (const EditorDef* ed = editorLike("tech")) {
          std::string id = ed->id;
          later(a, [id, fac](App& x) { x.openEditor(id, fac); });
        } else {
          edkit::goTo(a, {SelType::Faction, fac});
        }
      }
      if (act == ui::ChipAction::Remove)
        a.act("Убрать модификатор технологии", [&](Tx& tx) {
          auto& v = tx.tech(tid).modifiers;
          v.erase(std::remove(v.begin(), v.end(), mid), v.end());
        });
    }
  }
  if (group("Уровни построек", u.levels.size())) {
    ui::ChipFlow cf;
    for (auto [bid, lvl] : u.levels) {
      const Building* b = w.building(bid);
      if (!b) continue;
      ui::IdScope s{i64(bid) * 64 + lvl};
      std::string label = orName(b->name, "Без названия") + " · ур.\xC2\xA0" + std::to_string(lvl);
      auto act = useChip(label, b->icon.empty() || !gfx::hasIcon(b->icon) ? "building" : b->icon.c_str(),
                         b->owner ? w::factionColor(w, b->owner) : Color(0, 0, 0, 0),
                         b->owner ? "Уникальная постройка — открыть дерево построек" : "Общее дерево построек — открыть", !ro);
      if (act == ui::ChipAction::Click) {
        Id owner = b->owner;
        if (const EditorDef* ed = editorLike("build")) {
          std::string id = ed->id;
          later(a, [id, owner](App& x) { x.openEditor(id, owner); });
        } else if (owner) {
          edkit::goTo(a, {SelType::Faction, owner});
        }
      }
      if (act == ui::ChipAction::Remove) {
        int li = lvl - 1;
        Id bb = bid;
        a.act("Убрать модификатор уровня постройки", [&](Tx& tx) {
          Building& x = tx.building(bb);
          if (li < int(x.levels.size())) {
            auto& v = x.levels[size_t(li)].modifiers;
            v.erase(std::remove(v.begin(), v.end(), mid), v.end());
          }
        });
      }
    }
  }
  if (ro || automatic) return;
  // Добавить в списки провинций, государств, войск и персонажей — по виду модификатора (ТЗ «Модификаторы», п.1).
  const bool any = m.kind == ModKind::Any;
  std::vector<std::pair<Id, std::string>> items;
  std::vector<Color> colors;
  ui::spacer(4);
  ui::Row r({ui::fr(1), ui::fr(1)}, 30, 8);
  Id pick = 0;
  if (any || m.kind == ModKind::Province) {
    items.clear();
    colors.clear();
    std::vector<const Province*> ps;
    w.provinces.each([&](const Province& p) {
      if (!p.sea && !has(p.modifiers, mid)) ps.push_back(&p);
    });
    std::sort(ps.begin(), ps.end(), [](const Province* x, const Province* y) { return compareRu(x->name, y->name) < 0; });
    for (const Province* p : ps) {
      items.push_back({p->id, orName(p->name, "Без названия")});
      colors.push_back(w::factionColor(w, p->owner));
    }
    if (addCombo(a, "addprov", "modifiers.addProvince", items, colors, "Добавить провинции", "province", "Добавить модификатор в список провинции", pick))
      addTo(a, m, ModTarget::Province, pick, "Модификатор провинции");
  }
  if (any || m.kind == ModKind::Faction) {
    items.clear();
    colors.clear();
    std::vector<const Faction*> fs;
    w.factions.each([&](const Faction& f) {
      if (!has(f.modifiers, mid)) fs.push_back(&f);
    });
    std::sort(fs.begin(), fs.end(), [](const Faction* x, const Faction* y) {
      if (x->kind != y->kind) return x->kind < y->kind;
      return compareRu(x->name, y->name) < 0;
    });
    for (const Faction* f : fs) {
      items.push_back({f->id, orName(f->name, "Без названия")});
      colors.push_back(f->color);
    }
    if (addCombo(a, "addfac", "modifiers.addFaction", items, colors, "Добавить государству", nullptr, "Добавить модификатор государству или гильдии",
                 pick)) {
      const Faction* f = w.faction(pick);
      addTo(a, m, ModTarget::Faction, pick, f && f->isGuild() ? "Модификатор гильдии" : "Модификатор государства");
    }
  }
  if (any || m.kind == ModKind::Army) {
    items.clear();
    colors.clear();
    w.armies.each([&](const Army& x) {
      if (!has(x.modifiers, mid)) {
        items.push_back({x.id, orName(x.name, x.isFleet() ? "Флот" : "Войско")});
        colors.push_back(w::factionColor(w, x.leader()));
      }
    });
    if (addCombo(a, "addarmy", "modifiers.addArmy", items, colors, "Добавить войску", "army", "Добавить модификатор войску или флоту", pick))
      addTo(a, m, ModTarget::Army, pick, "Модификатор войска");
  }
  if (any || m.kind == ModKind::Hero) {
    items.clear();
    colors.clear();
    std::vector<const Character*> cs;
    w.characters.each([&](const Character& c) {
      if (!has(c.modifiers, mid)) cs.push_back(&c);
    });
    std::sort(cs.begin(), cs.end(), [](const Character* x, const Character* y) { return compareRu(x->name, y->name) < 0; });
    for (const Character* c : cs) {
      items.push_back({c->id, orName(c->name, "Без имени")});
      colors.push_back(w::factionColor(w, c->faction));
    }
    if (addCombo(a, "addhero", "modifiers.addHero", items, colors, "Добавить персонажу", "character", "Добавить модификатор персонажу", pick))
      addTo(a, m, ModTarget::Character, pick, "Модификатор персонажа");
  }
  // Модификатор археологических групп — только группам (ТЗ «Доработки №2», п.1).
  if (m.kind == ModKind::ArchGroup) {
    items.clear();
    colors.clear();
    std::vector<std::pair<Id, Id>> refs;   // государство, группа
    w.factions.each([&](const Faction& f) {
      for (const ArchGroup& g : f.archGroups)
        if (!has(g.modifiers, mid)) {
          refs.push_back({f.id, g.id});
          items.push_back({Id(refs.size()), orName(g.name, "Группа") + " · " + orName(f.name, "Без названия")});
          colors.push_back(f.color);
        }
    });
    if (addCombo(a, "addgroup", "modifiers.addGroup", items, colors, "Добавить группе", "pickaxe", "Добавить модификатор археологической группе", pick) &&
        pick >= 1 && pick <= refs.size()) {
      const auto [fid, gid] = refs[size_t(pick - 1)];
      Id made = 0;
      if (a.act("Модификатор археологической группы", [&](Tx& tx) {
            made = recordOf(tx, m);
            const int turns = std::max(0, tx.w().modifier(made)->duration);
            for (ArchGroup& g : tx.faction(fid).archGroups)
              if (g.id == gid && !has(g.modifiers, made)) {
                g.modifiers.push_back(made);
                if (turns > 0) g.modTurns[made] = turns;
              }
          }) &&
          !m.id && made && a.ui.editor == "modifiers")
        a.ui.editorArg = made;
    }
  }
}


}  // namespace rg::app::modedit
