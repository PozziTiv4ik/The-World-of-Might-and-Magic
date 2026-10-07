// Regnum — связь сущностей мира с каноном кампании (ТЗ «Фиксы», п.5–8; «Исправления», п.1): поиск карточек по части
// имени, подтверждение одной найденной или выбор из нескольких (с уточняющим поиском), заметки из «Кратко», портрет
// героя из карточки.
#include "app/canon.h"

#include "app/app_internal.h"
#include "base/fs.h"
#include "codec/jpeg.h"
#include "codec/png.h"

namespace rg::app::canon {

namespace {

const char* dirOf(Kind k) { return k == Kind::Character ? "03_Персонажи" : "04_Локации"; }

// Поля карточки: заголовок «# Имя», id, type, aliases, portrait (front matter в первых строках).
Card readCard(const std::string& path, const std::string& text) {
  Card c;
  c.path = path;
  size_t pos = 0;
  int lines = 0;
  while (pos < text.size() && lines < 80) {
    size_t e = text.find('\n', pos);
    std::string_view ln(text.data() + pos, (e == std::string::npos ? text.size() : e) - pos);
    if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
    if (c.title.empty() && startsWith(ln, "# ")) c.title = trim(ln.substr(2));
    if (c.id.empty() && startsWith(ln, "id:")) c.id = trim(ln.substr(3));
    if (c.type.empty() && startsWith(ln, "type:")) c.type = trim(ln.substr(5));
    if (c.portrait.empty() && startsWith(ln, "portrait:")) c.portrait = trim(ln.substr(9));
    if (startsWith(ln, "aliases:")) {
      std::string v = trim(ln.substr(8));
      v = replaceAll(replaceAll(replaceAll(v, "[", ""), "]", ""), "\"", "");
      for (auto& s : split(v, ',')) {
        std::string t = trim(s);
        if (!t.empty()) c.aliases.push_back(t);
      }
    }
    if (e == std::string::npos) break;
    pos = e + 1;
    lines++;
  }
  if (c.title.empty()) c.title = replaceAll(fs::stem(path), "_", " ");
  return c;
}

// Кеш карточек папки (повторный обход — не чаще раза в 3 секунды).
struct DirCache {
  std::string dir;
  double at = -1e9;
  std::vector<Card> cards;
};
DirCache& cacheOf(Kind k) {
  static DirCache c[2];
  return c[int(k)];
}

const std::vector<Card>& cards(App& a, Kind kind) {
  DirCache& dc = cacheOf(kind);
  const std::string root = projectRoot(a);
  const std::string dir = root.empty() ? std::string() : fs::join(root, dirOf(kind));
  const double now = nowSeconds();
  if (dc.dir == dir && now - dc.at < 3) return dc.cards;
  dc.dir = dir;
  dc.at = now;
  dc.cards.clear();
  if (dir.empty()) return dc.cards;
  for (const fs::DirEntry& e : fs::list(dir)) {
    if (e.dir || !endsWith(e.name, ".md") || startsWith(e.name, "00_") || startsWith(e.name, "01_") || startsWith(e.name, "02_")) continue;
    auto text = fs::readFile(e.path);
    if (!text) continue;
    Card c = readCard(e.path, text->substr(0, std::min<size_t>(text->size(), 6144)));
    if (c.id.empty()) continue;   // без ID связать нельзя
    dc.cards.push_back(std::move(c));
  }
  std::sort(dc.cards.begin(), dc.cards.end(), [](const Card& x, const Card& y) { return compareRu(x.title, y.title) < 0; });
  return dc.cards;
}

const std::string& entityOf(const World& w, SelType t, Id id) {
  static const std::string kNone;
  switch (t) {
    case SelType::Character: if (const Character* c = w.character(id)) return c->entity; break;
    case SelType::Province: if (const Province* p = w.province(id)) return p->entity; break;
    case SelType::Faction: if (const Faction* f = w.faction(id)) return f->entity; break;
    default: break;
  }
  return kNone;
}
const std::string& notesOf(const World& w, SelType t, Id id) {
  static const std::string kNone;
  switch (t) {
    case SelType::Character: if (const Character* c = w.character(id)) return c->notes; break;
    case SelType::Province: if (const Province* p = w.province(id)) return p->notes; break;
    case SelType::Faction: if (const Faction* f = w.faction(id)) return f->notes; break;
    default: break;
  }
  return kNone;
}
std::string nameOf(const World& w, SelType t, Id id) {
  switch (t) {
    case SelType::Character: return w.characterName(id);
    case SelType::Province: return w.provinceName(id);
    case SelType::Faction: return w.factionName(id);
    default: return {};
  }
}
Kind kindOf(SelType t) { return t == SelType::Character ? Kind::Character : Kind::Location; }

void setEntity(Tx& tx, SelType t, Id id, const std::string& entity) {
  switch (t) {
    case SelType::Character: tx.character(id).entity = entity; break;
    case SelType::Province: tx.province(id).entity = entity; break;
    case SelType::Faction: tx.faction(id).entity = entity; break;
    default: break;
  }
}
void setNotes(Tx& tx, SelType t, Id id, const std::string& notes) {
  switch (t) {
    case SelType::Character: tx.character(id).notes = notes; break;
    case SelType::Province: tx.province(id).notes = notes; break;
    case SelType::Faction: tx.faction(id).notes = notes; break;
    default: break;
  }
}

// Портрет из файла карточки: большие уменьшаются (мир хранит портрет внутри файла персонажей).
std::string portraitBytes(const std::string& root, const std::string& rel) {
  if (rel.empty() || root.empty()) return {};
  std::string path = fs::join(root, rel);
  auto bytes = fs::readFile(path);
  if (!bytes) return {};
  auto img = codec::decodeImage(*bytes);
  if (!img || img->empty()) return {};
  constexpr int kMaxSide = 1024, kTarget = 512;
  constexpr size_t kMaxBytes = size_t(2) << 20;
  if (std::max(img->w, img->h) <= kMaxSide && bytes->size() <= kMaxBytes) return std::move(*bytes);
  gfx::Image g = gfx::Image::fromRgba(img->rgba.data(), img->w, img->h);
  double k = double(kTarget) / std::max(img->w, img->h);
  if (k < 1) g = g.scaled(std::max(1, int(std::lround(img->w * k))), std::max(1, int(std::lround(img->h * k))));
  codec::RgbaImage out;
  out.w = g.w;
  out.h = g.h;
  out.rgba = g.toRgba();
  std::vector<u8> png = codec::encodePng(out, 6);
  return std::string(png.begin(), png.end());
}

// Насколько карточка подходит запросу key (searchKey): 0 — поле совпадает целиком, 1 — начинается с запроса,
// 2 — содержит его, 3 — содержит все слова запроса; −1 — не подходит. Поля: заголовок, имя файла, псевдонимы, ID.
int matchRank(const Card& c, const std::string& key) {
  int best = -1;
  auto field = [&](std::string_view text) {
    const std::string f = utf8::searchKey(replaceAll(std::string(text), "_", " "));
    if (f.empty()) return;
    const int r = f == key ? 0 : startsWith(f, key) ? 1 : f.find(key) != std::string::npos ? 2 : utf8::matches(f, key) ? 3 : -1;
    if (r >= 0 && (best < 0 || r < best)) best = r;
  };
  field(c.title);
  field(replaceAll(fs::stem(c.path), "_", " "));
  for (const auto& al : c.aliases) field(al);
  field(c.id);
  return best;
}

// Выбор карточки среди найденных (ТЗ «Фиксы», п.5: «уточнять, с каким именно героем связать»; «Исправления», п.1:
// «если будет несколько персонажей… показывать списком… где можно будет выбрать кого привязать»). Поиск сверху
// заполнен именем сущности — его можно уточнить или изменить.
struct PickDialog final : Dialog {
  SelType type = SelType::Character;
  Id target = 0;
  std::string query, shown;   // запрос и запрос, по которому найдены found
  std::vector<Card> found;
  int sel = 0;
  bool focus = true;
  const char* id() const override { return "canon.pick"; }
  Style style(App&) override { return {"Связать с каноном", "book", ui::Tone::Accent, 560}; }
  bool draw(App& a) override {
    const char* icon = type == SelType::Character ? "user" : "province";
    ui::label(nameOf(a.world(), type, target), {.font = ui::Font::Strong, .icon = icon});
    if (focus) {
      ui::setKeyboardFocus(ui::id("q"));
      focus = false;
    }
    ui::searchField("q", query, "Имя, ID или псевдоним");
    a.markUi("canon.pick.search");
    if (query != shown) {
      // Пустой запрос — все карточки (выбор вручную).
      shown = query;
      found = trim(query).empty() ? cards(a, kindOf(type)) : find(a, kindOf(type), query);
      sel = found.empty() ? -1 : 0;
    }
    // Стрелки — выбор в списке (поле поиска в фокусе).
    bool moved = false;
    if (!found.empty()) {
      if (ui::keyPressed(platform::Key::Down)) {
        ui::consumeKey(platform::Key::Down);
        sel = std::min(int(found.size()) - 1, sel + 1);
        moved = true;
      }
      if (ui::keyPressed(platform::Key::Up)) {
        ui::consumeKey(platform::Key::Up);
        sel = std::max(0, sel - 1);
        moved = true;
      }
    }
    bool pick = false;
    if (found.empty()) {
      ui::label("Карточек не найдено", {.font = ui::Font::Small, .ink = ui::Ink::Muted});
    } else {
      ui::label(fmtInt(i64(found.size())) + " " + plural(i64(found.size()), "карточка", "карточки", "карточек"),
                {.font = ui::Font::Small, .ink = ui::Ink::Muted});
      a.markUi("canon.pick.count");
      ui::VirtualList vl("list", int(found.size()), 44, std::min(396.f, float(found.size()) * 44.f));
      if (moved && sel >= 0) vl.scrollToRow(sel);
      for (int i : vl) {
        const Card& c = found[size_t(i)];
        std::string sub = c.id + (c.aliases.empty() ? std::string() : " · " + join(c.aliases, ", "));
        if (ui::listItem(c.title, {.icon = icon, .subtitle = sub, .selected = sel == i})) sel = i;
        if (ui::lastItem().doubleClicked) {
          sel = i;
          pick = true;
        }
        a.markUi("canon.pick." + std::to_string(i));
      }
    }
    ui::ModalFooter f;
    if (ui::button("Отмена")) return false;
    const bool ok = sel >= 0 && sel < int(found.size());
    if (ui::button("Связать", {.variant = ui::Variant::Primary, .icon = "link", .disabled = !ok, .isDefault = true}) || (pick && ok)) {
      link(a, type, target, found[size_t(sel)]);
      return false;
    }
    a.markUi("canon.pick.ok");
    return true;
  }
};

void findAndLink(App& a, SelType type, Id id) {
  const std::string name = nameOf(a.world(), type, id);
  std::vector<Card> found = find(a, kindOf(type), name);
  if (found.empty()) {
    a.toast(projectRoot(a).empty() ? std::string("Папка кампании не найдена рядом с миром") : "Карточка «" + name + "» не найдена", ToastKind::Info, "book");
    return;
  }
  if (found.size() == 1) {
    // ТЗ «Фиксы», п.7: «точно ли это то государство», — вопрос перед связью.
    Card c = found[0];
    const char* what = type == SelType::Character ? "персонаж" : type == SelType::Faction ? "государство" : "провинция";
    a.confirm("Связать с каноном?", "Точно ли это " + std::string(what) + " «" + c.title + "» (" + c.id + ")?", "Связать", false,
              [type, id, c](App& x) { link(x, type, id, c); });
    return;
  }
  auto d = std::make_unique<PickDialog>();
  d->type = type;
  d->target = id;
  d->query = d->shown = name;
  d->found = std::move(found);
  a.openDialog(std::move(d));
}

}  // namespace

std::string projectRoot(App& a) {
  std::vector<std::string> roots;
  if (!a.projectPath().empty()) roots.push_back(fs::absolute(a.projectPath()));
  roots.push_back(fs::absolute("."));
  for (std::string r : roots) {
    for (int i = 0; i < 8 && !r.empty(); i++) {
      if (fs::isDir(fs::join(r, "03_Персонажи")) || fs::isDir(fs::join(r, "04_Локации"))) return r;
      std::string p = fs::parent(r);
      if (p == r) break;
      r = p;
    }
  }
  return {};
}

std::vector<Card> find(App& a, Kind kind, std::string_view name) {
  const std::string key = utf8::searchKey(replaceAll(trim(name), "_", " "));   // «Капитан_Эйганн» — как имя файла
  if (key.empty()) return {};
  // Карточки уже по заголовку: устойчивая сортировка по рангу оставляет этот порядок внутри ранга.
  std::vector<std::pair<int, const Card*>> hits;
  for (const Card& c : cards(a, kind))
    if (const int r = matchRank(c, key); r >= 0) hits.push_back({r, &c});
  std::stable_sort(hits.begin(), hits.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
  std::vector<Card> out;
  out.reserve(hits.size());
  for (const auto& h : hits) out.push_back(*h.second);
  return out;
}

std::optional<Card> byId(App& a, Kind kind, std::string_view id) {
  const std::string key = utf8::searchKey(trim(id));
  if (key.empty()) return std::nullopt;
  for (const Card& c : cards(a, kind))
    if (utf8::searchKey(c.id) == key) return c;
  return std::nullopt;
}

std::string summary(const Card& c) {
  auto text = fs::readFile(c.path);
  if (!text) return {};
  std::vector<std::string> lines = split(replaceAll(*text, "\r", ""), '\n');
  // Пропустить заголовок и front matter.
  size_t i = 0, fmEnd = 0;
  int dashes = 0;
  for (size_t k = 0; k < lines.size() && k < 80; k++)
    if (trim(lines[k]) == "---" && ++dashes == 2) {
      fmEnd = k + 1;
      break;
    }
  i = fmEnd;
  std::vector<std::string> out;
  bool inSummary = false, any = false;
  for (size_t k = i; k < lines.size(); k++) {
    const std::string t = trim(lines[k]);
    if (startsWith(t, "## ")) {
      if (inSummary) break;
      inSummary = utf8::searchKey(t.substr(3)) == utf8::searchKey("Кратко");
      any = any || inSummary;
      continue;
    }
    if (inSummary) out.push_back(lines[k]);
  }
  if (!any) {   // без раздела «Кратко» — первый абзац текста
    for (size_t k = i; k < lines.size(); k++) {
      const std::string t = trim(lines[k]);
      if (t.empty() || startsWith(t, "#")) {
        if (!out.empty()) break;
        continue;
      }
      out.push_back(t);
    }
  }
  std::string s = trim(join(out, "\n"));
  while (s.find("\n\n\n") != std::string::npos) s = replaceAll(s, "\n\n\n", "\n\n");
  return s;
}

void link(App& a, SelType type, Id id, const Card& card) {
  const std::string notes = summary(card);
  std::string portrait;
  if (type == SelType::Character) portrait = portraitBytes(projectRoot(a), card.portrait);
  const bool ok = a.act("Связь с каноном", [&](Tx& tx) {
    setEntity(tx, type, id, card.id);
    setNotes(tx, type, id, notes);
    if (!portrait.empty()) tx.character(id).portrait = portrait;   // ТЗ «Фиксы», п.5: портрет из карточки
  });
  if (ok) a.toast("Связано с карточкой " + card.id + (portrait.empty() ? std::string() : ", портрет добавлен"), ToastKind::Success, "book");
}

void section(App& a, SelType type, Id id) {
  const World& w = a.world();
  const std::string ent = entityOf(w, type, id);
  const bool ro = a.readOnly();
  {
    ui::Row r({ui::fr(1), ui::px(30), ui::px(30), ui::px(30)}, 30, 6);
    std::string e = ent;
    if (ui::textField("entity", e, {.placeholder = type == SelType::Character ? "ID карточки, напр. CHAR-0001" : "ID карточки, напр. LOC-0020",
                                    .icon = "link", .maxLength = 40, .readOnly = ro, .tooltip = "ID карточки канона"}) &&
        trim(e) != ent) {
      std::string v = trim(e);
      if (v.empty()) a.act("Связь с каноном", [&](Tx& tx) { setEntity(tx, type, id, ""); });
      else if (auto c = byId(a, kindOf(type), v)) link(a, type, id, *c);
      else a.act("Связь с каноном", [&](Tx& tx) { setEntity(tx, type, id, v); });
    }
    a.markUi("canon.entity");
    if (ui::iconButton("search", "Найти карточку по названию", {.disabled = ro})) findAndLink(a, type, id);
    a.markUi("canon.find");
    if (ui::iconButton("external", "Открыть карточку", {.disabled = ent.empty()})) {
      if (auto c = byId(a, kindOf(type), ent)) platform::openPath(c->path);
      else a.toast("Карточка " + ent + " не найдена рядом с проектом", ToastKind::Warning, "book");
    }
    a.markUi("canon.open");
    if (ui::iconButton("unlink", "Отвязать от канона", {.disabled = ro || ent.empty()}))
      a.act("Связь с каноном", [&](Tx& tx) { setEntity(tx, type, id, ""); });
    a.markUi("canon.unlink");
  }
  if (!ent.empty())
    if (auto c = byId(a, kindOf(type), ent)) ui::label(c->title, {.font = ui::Font::Small, .ink = ui::Ink::Dim, .icon = "book"});
}

void notesField(App& a, SelType type, Id id, float height) {
  const World& w = a.world();
  const bool linked = !entityOf(w, type, id).empty();
  std::string notes = notesOf(w, type, id);
  const bool ro = a.readOnly() || linked;
  if (ui::textArea("notes", notes, height, {.placeholder = linked ? "" : "Заметки", .readOnly = ro,
                                            .tooltip = linked ? "Заметки из канона — правятся в карточке" : ""}) &&
      !ro && notes != notesOf(w, type, id))
    a.act("Заметки", [&](Tx& tx) { setNotes(tx, type, id, notes); });
  a.markUi("canon.notes");
  if (linked && !a.readOnly()) {
    ui::HStack hs(24, ui::Align::Right, 4);
    if (ui::iconButton("refresh", "Обновить заметки из канона", {.size = ui::Size::Small})) {
      const std::string ent = entityOf(w, type, id);
      if (auto c = byId(a, kindOf(type), ent)) {
        const std::string s = summary(*c);
        a.act("Заметки из канона", [&](Tx& tx) { setNotes(tx, type, id, s); });
      } else {
        a.toast("Карточка " + ent + " не найдена рядом с проектом", ToastKind::Warning, "book");
      }
    }
    a.markUi("canon.refresh");
  }
}

}  // namespace rg::app::canon
