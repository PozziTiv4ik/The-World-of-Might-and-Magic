// Regnum — реестр геральдических эмблем: части каталога по порядку показа (emblems_internal.h), подписи, группы,
// проверка реестра, отрисовка.
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "gfx/emblems_internal.h"

namespace rg::gfx {

namespace {

struct SvHash {
  using is_transparent = void;
  size_t operator()(std::string_view s) const { return size_t(hash64(s)); }
};
struct SvEq {
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const { return a == b; }
};

struct EmblemRegistry {
  GlyphBuilder b{100, 0x656d626cull << 32};
  std::vector<EmblemGroup> groups;
  std::vector<std::pair<const char*, const char*>> titles;   // в порядке объявления (для проверки)
  std::unordered_map<std::string, std::string_view, SvHash, SvEq> titleOf;
};

const EmblemRegistry& registry() {
  static const EmblemRegistry r = [] {
    EmblemRegistry x;
    emb::Catalog c{x.b, x.groups, x.titles};
    emb::classicEmblems(c);
    emb::fantasyEmblemsA(c);
    emb::fantasyEmblemsB(c);
    emb::raceEmblemsA(c);
    emb::raceEmblemsB(c);
    emb::raceEmblemsC(c);
    for (const auto& [name, title] : x.titles) x.titleOf.emplace(name, title);
    return x;
  }();
  return r;
}

std::mutex gUnknownMu;
std::unordered_set<std::string> gUnknown;

}  // namespace

const std::vector<std::string>& emblemNames() { return registry().b.names(); }
bool hasEmblem(std::string_view name) { return registry().b.find(name) != nullptr; }
const VecGlyph* emblemGlyph(std::string_view name) { return registry().b.find(name); }
const std::vector<EmblemGroup>& emblemGroups() { return registry().groups; }

std::vector<std::string> emblemRegistryIssues() {
  const EmblemRegistry& r = registry();
  std::vector<std::string> out = r.b.issues();
  std::unordered_map<std::string, int> titled;
  for (const auto& [name, title] : r.titles) {
    if (!hasEmblem(name)) out.push_back(std::string("подпись без эмблемы: ") + name);
    if (std::string_view(title).empty()) out.push_back(std::string("пустая подпись: ") + name);
    if (++titled[name] == 2) out.push_back(std::string("повторная подпись: ") + name);
  }
  for (const std::string& nm : emblemNames())
    if (!titled.count(nm)) out.push_back("эмблема без подписи: " + nm);
  // Каждая эмблема — ровно в одной группе; группы непустые, названия различны.
  std::unordered_map<std::string, int> inGroups;
  std::unordered_set<std::string> groupTitles;
  for (const EmblemGroup& g : r.groups) {
    if (g.title.empty()) out.push_back("группа без названия");
    else if (!groupTitles.insert(std::to_string(int(g.section)) + ":" + std::string(g.title)).second)
      out.push_back("повторная группа: " + std::string(g.title));
    if (g.names.empty()) out.push_back("пустая группа: " + std::string(g.title));
    for (const std::string& nm : g.names) {
      if (!hasEmblem(nm)) out.push_back("в группе «" + std::string(g.title) + "» нет эмблемы " + nm);
      inGroups[nm]++;
    }
  }
  for (const std::string& nm : emblemNames()) {
    auto it = inGroups.find(nm);
    if (it == inGroups.end()) out.push_back("эмблема вне групп: " + nm);
    else if (it->second > 1) out.push_back("эмблема в нескольких группах: " + nm);
  }
  return out;
}

std::string_view emblemTitle(std::string_view name) {
  const auto& m = registry().titleOf;
  auto it = m.find(name);
  return it == m.end() ? std::string_view{} : it->second;
}

void drawEmblem(Canvas& c, std::string_view name, RectF rect, Color color) {
  if (name.empty()) return;
  if (const VecGlyph* g = registry().b.find(name)) {
    drawGlyph(c, *g, rect, color);
    return;
  }
  std::lock_guard<std::mutex> lock(gUnknownMu);
  if (gUnknown.insert(std::string(name)).second) logWarn("Эмблема «%.*s» не найдена", int(name.size()), name.data());
}

}  // namespace rg::gfx
