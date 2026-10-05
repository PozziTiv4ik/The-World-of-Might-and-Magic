// Regnum — эмблемы: полнота каталога, подписи, группы, заполнение и центровка, неизвестные имена, наглядные листы.
#include <unordered_map>
#include <unordered_set>

#include "gfx/emblems.h"
#include "tests/test_gfx_icons_util.h"

using namespace rg;
using namespace rg::gfx;
using namespace rg::test::icons;

namespace {

const char* const kClassicEmblems[] = {"crown", "tower", "castle", "star", "sun", "moon", "tree", "lily", "sword",
                                       "swords", "shield", "skull", "anchor", "ship", "wheat", "gem", "hammer", "axe",
                                       "bow", "key", "eye", "flame", "eagle", "lion", "dragon", "wolf", "bull", "horse",
                                       "serpent", "kraken", "rune", "rose", "griffin", "bear"};

// Группы каталога: название и обязательные эмблемы (имена хранятся в мирах — менять нельзя).
struct GroupSpec {
  const char* title;
  std::vector<const char*> names;
};
const GroupSpec kGroups[] = {
    {"Драконы", {"dragon-wings", "dragon-coiled", "dragon-eye", "dragon-skull"}},
    {"Вампиры", {"vampire-bat", "vampire-fangs", "vampire-chalice", "vampire-coffin"}},
    {"Природа", {"nature-leaf", "nature-stag", "nature-mushroom", "nature-pine"}},
    {"Стихии", {"element-fire", "element-water", "element-air", "element-earth", "element-lightning", "element-frost"}},
    {"Гномы", {"dwarf-anvil", "dwarf-pickaxes", "dwarf-face", "dwarf-gate"}},
    {"Эльфы", {"elf-leaf", "elf-star", "elf-wreath", "elf-swan"}},
    {"Некроманты", {"necro-crossbones", "necro-scythe", "necro-hand", "necro-tomb"}},
    {"Тёмные эльфы", {"dark-elf-spider", "dark-elf-web", "dark-elf-daggers", "dark-elf-crescent"}},
    {"Наги", {"naga-cobra", "naga-trident", "naga-shell", "naga-serpents"}},
    {"Рыцари", {"knight-helm", "knight-chess", "knight-gauntlet", "knight-shield"}},
    {"Святые", {"holy-cross", "holy-wings", "holy-dove", "holy-grail"}},
    {"Демоны", {"demon-head", "demon-imp", "demon-claw", "demon-pitchfork"}},
    {"Культисты", {"cult-pentagram", "cult-hood", "cult-eye", "cult-goat"}},
    {"Магия", {"magic-hat", "magic-book", "magic-orb", "magic-staff", "magic-potion"}},
    {"Орки", {"orc-head", "orc-boar", "orc-cleavers", "orc-totem"}},
    {"Ящеры", {"lizard-crawl", "lizard-head", "lizard-croc", "lizard-pyramid", "lizard-track"}},
    {"Зверолюды", {"beast-minotaur", "beast-centaur", "beast-satyr", "beast-harpy", "beast-mermaid"}},
};

std::vector<std::string> requiredEmblems() {
  std::vector<std::string> out(std::begin(kClassicEmblems), std::end(kClassicEmblems));
  for (const GroupSpec& g : kGroups)
    for (const char* n : g.names) out.push_back(n);
  return out;
}

Image renderEmblem(std::string_view name, int px, int pad) {
  Image img(px + 2 * pad, px + 2 * pad, 0);
  Canvas c(img);
  drawEmblem(c, name, {float(pad), float(pad), float(px), float(px)}, Color(255, 255, 255));
  return img;
}

// Имя латиницей в kebab-case.
bool kebab(std::string_view s) {
  if (s.empty() || s.front() == '-' || s.back() == '-') return false;
  for (size_t i = 0; i < s.size(); i++) {
    const char ch = s[i];
    if (ch == '-' ? s[i - 1] == '-' : !(ch >= 'a' && ch <= 'z')) return false;
  }
  return true;
}

}  // namespace

TEST(gfx_icons_emblems_catalog) {
  const std::vector<std::string> required = requiredEmblems();
  std::vector<std::string> missing;
  for (const std::string& n : required)
    if (!hasEmblem(n)) missing.push_back(n);
  CHECK_MSG(missing.empty(), "нет эмблем: " + join(missing, ", "));
  const auto& names = emblemNames();
  CHECK_EQ(names.size(), required.size());
  std::unordered_set<std::string> uniq(names.begin(), names.end());
  CHECK_EQ(uniq.size(), names.size());
  const auto issues = emblemRegistryIssues();
  CHECK_MSG(issues.empty(), join(issues, "\n"));
  CHECK_EQ(std::string(emblemTitle("crown")), std::string("Корона"));
  CHECK_EQ(std::string(emblemTitle("dragon-wings")), std::string("Дракон в полёте"));
  CHECK(emblemTitle("nothing").empty());
  CHECK(emblemGlyph("lion") != nullptr);
  CHECK(emblemGlyph("") == nullptr);
  std::vector<std::string> badNames;
  for (const std::string& n : names)
    if (!kebab(n)) badNames.push_back(n);
  CHECK_MSG(badNames.empty(), "имена не в kebab-case: " + join(badNames, ", "));
}

TEST(gfx_icons_emblems_groups) {
  const auto& groups = emblemGroups();
  CHECK_EQ(groups.size(), std::size(kGroups) + 1);
  if (groups.size() != std::size(kGroups) + 1) return;
  // Первая группа — классические эмблемы в прежнем порядке.
  CHECK_EQ(std::string(groups[0].title), std::string("Классические"));
  CHECK_EQ(groups[0].names.size(), std::size(kClassicEmblems));
  for (size_t i = 0; i < std::min(groups[0].names.size(), std::size(kClassicEmblems)); i++)
    CHECK_EQ(groups[0].names[i], std::string(kClassicEmblems[i]));
  // Фантазийные группы: название, состав, не меньше трёх эмблем, общий префикс категории, подписи.
  std::vector<std::string> bad;
  for (size_t gi = 0; gi < std::size(kGroups); gi++) {
    const EmblemGroup& g = groups[gi + 1];
    const GroupSpec& spec = kGroups[gi];
    if (g.title != spec.title) bad.push_back("группа " + std::to_string(gi + 1) + ": «" + std::string(g.title) + "» вместо «" + spec.title + "»");
    if (g.names.size() < 3) bad.push_back(std::string(spec.title) + ": меньше трёх эмблем");
    std::unordered_set<std::string> have(g.names.begin(), g.names.end());
    for (const char* n : spec.names)
      if (!have.count(n)) bad.push_back(std::string(spec.title) + ": нет " + n);
    const std::string prefix = std::string(spec.names.front()).substr(0, std::string(spec.names.front()).rfind('-') + 1);
    for (const std::string& n : g.names) {
      if (n.rfind(prefix, 0) != 0) bad.push_back(std::string(spec.title) + ": " + n + " без префикса " + prefix);
      if (emblemTitle(n).empty()) bad.push_back(n + ": нет подписи");
    }
  }
  // Каждая эмблема — ровно в одной группе.
  std::unordered_map<std::string, int> seen;
  for (const EmblemGroup& g : groups)
    for (const std::string& n : g.names) seen[n]++;
  for (const std::string& n : emblemNames())
    if (seen[n] != 1) bad.push_back(n + ": в группах " + std::to_string(seen[n]) + " раз");
  CHECK_EQ(seen.size(), emblemNames().size());
  CHECK_MSG(bad.empty(), join(bad, "\n"));
}

TEST(gfx_icons_emblems_fill_and_center) {
  std::vector<std::string> bad;
  for (const std::string& n : emblemNames()) {
    const int px = 100, pad = 8;
    const Image img = renderEmblem(n, px, pad);
    const Coverage cv = coverage(img);
    const double area = cv.sum / (px * px);
    // Силуэт заметен, но не сплошной квадрат.
    if (area < 0.12 || area > 0.8) bad.push_back(n + ": площадь " + fmtNum(area * 100) + "%");
    if (cv.x0 < pad - 1 || cv.y0 < pad - 1 || cv.x1 > pad + px || cv.y1 > pad + px) bad.push_back(n + ": выход за квадрат");
    const double bx = (cv.x0 + cv.x1 + 1) * 0.5 - pad - px * 0.5, by = (cv.y0 + cv.y1 + 1) * 0.5 - pad - px * 0.5;
    if (std::fabs(bx) > 6 || std::fabs(by) > 6) bad.push_back(n + ": габариты смещены (" + fmtNum(bx) + ", " + fmtNum(by) + ")");
    const int span = std::max(cv.x1 - cv.x0 + 1, cv.y1 - cv.y0 + 1);
    if (span < 80) bad.push_back(n + ": мелко (" + std::to_string(span) + ")");
    // Малые размеры: что-то видно.
    for (int s : {12, 16, 24}) {
      const Coverage small = coverage(renderEmblem(n, s, 2));
      if (small.sum < s * s * 0.12) bad.push_back(n + ": пусто на " + std::to_string(s));
    }
  }
  CHECK_MSG(bad.empty(), join(bad, "\n"));
}

TEST(gfx_icons_emblems_unknown) {
  Image img(40, 40, 0);
  Canvas c(img);
  drawEmblem(c, "", {0, 0, 40, 40}, Color(255, 255, 255));
  drawEmblem(c, "no-such-emblem", {0, 0, 40, 40}, Color(255, 255, 255));
  drawEmblem(c, "no-such-emblem", {0, 0, 40, 40}, Color(255, 255, 255));
  CHECK(!coverage(img).any());
  drawEmblem(c, "lion", {0, 0, 40, 40}, Color(255, 255, 255, 0));
  CHECK(!coverage(img).any());
}

TEST(gfx_icons_emblems_sheet) {
  // Листы по группам (gfx_emblems — классические, gfx_emblems_2… — фантазийные): заголовок группы, ячейка
  // с эмблемой 120 px и малыми размерами 16, 24, 32 px. Группа не делится между листами.
  const int cols = 7, cell = 190, head = 30, maxH = 1000;
  struct Page {
    std::vector<const EmblemGroup*> groups;
    int h = 0;
  };
  std::vector<Page> pages(1);
  for (const EmblemGroup& g : emblemGroups()) {
    const int gh = head + int((g.names.size() + cols - 1) / cols) * cell;
    if (!pages.back().groups.empty() && pages.back().h + gh > maxH) pages.emplace_back();
    pages.back().groups.push_back(&g);
    pages.back().h += gh;
  }
  const Color bgs[] = {Color::hex(0x7a2430), Color::hex(0x1f4e79), Color::hex(0x2f5d3a), Color::hex(0x5b3f8c),
                       Color::hex(0x1d2333), Color::hex(0x8c5a1b)};
  size_t shown = 0, k = 0;
  for (size_t p = 0; p < pages.size(); p++) {
    Image img(cols * cell, pages[p].h);
    Canvas c(img);
    c.clear(Color::hex(0x151a22));
    float y0 = 0;
    for (const EmblemGroup* g : pages[p].groups) {
      label(c, std::string(g->title), 10, y0 + 7, Color::hex(0xd9a441), 14);
      y0 += float(head);
      for (size_t i = 0; i < g->names.size(); i++, k++) {
        const std::string& nm = g->names[i];
        const float x = float(int(i) % cols) * cell, y = y0 + float(int(i) / cols) * cell;
        c.fillRoundRect({x + 6, y + 6, cell - 12.f, cell - 12.f}, 10, bgs[k % std::size(bgs)]);
        label(c, nm + "  " + std::string(emblemTitle(nm)), x + 14, y + 10, Color(255, 255, 255, 170), 11);
        drawEmblem(c, nm, {x + 18, y + 28, 120, 120}, Color::hex(0xf2e3b3));
        float iy = y + 30;
        for (int s : {16, 24, 32}) {
          drawEmblem(c, nm, {x + 146, iy, float(s), float(s)}, Color::hex(0xf2e3b3));
          iy += float(s) + 8;
        }
        shown++;
      }
      y0 += float(int((g->names.size() + cols - 1) / cols) * cell);
    }
    savePng(img, p == 0 ? std::string("gfx_emblems") : "gfx_emblems_" + std::to_string(p + 1));
  }
  CHECK_EQ(shown, emblemNames().size());
  // Тёмные эмблемы на светлом фоне: все на одном листе.
  const auto& names = emblemNames();
  const int lc = 10;
  Image light(lc * 110, int((names.size() + lc - 1) / lc) * 110);
  Canvas cl(light);
  cl.clear(Color::hex(0xf2e3b3));
  for (size_t i = 0; i < names.size(); i++)
    drawEmblem(cl, names[i], {float(int(i) % lc) * 110 + 10, float(int(i) / lc) * 110 + 10, 90, 90}, Color::hex(0x1d2333));
  savePng(light, "gfx_emblems_light");
}
