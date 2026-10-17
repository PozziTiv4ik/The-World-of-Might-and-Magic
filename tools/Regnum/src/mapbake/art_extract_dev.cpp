// Regnum — разбор исходной карты: отладочные изображения (regnum-cli build-map --debug) и средства разработки
// (--inventory, --fit-icons): инвентаризация знаков, образцы гор и подбор рисунка горы под штамп исходника.
#include <cstdio>
#include <unordered_map>

#include "base/fs.h"
#include "codec/png.h"
#include "map/art_render.h"
#include "mapbake/art_extract_internal.h"

namespace rg::map::art {

using namespace detail;

// ---------------------------------------------------------------- отладочные изображения
namespace detail {

void writeMountainSheet(const std::string& debugDir, const std::vector<Stamp>& mountains, int mountainVariants) {
  // Образцы гор: по одному на вариант рисунка, ×10 (форма — краска образца, белое — незначимые пиксели).
  fs::makeDirs(debugDir);
  const int K = 10, cell = 22 * K;
  codec::RgbaImage sheet;
  sheet.w = cell * std::max(1, mountainVariants);
  sheet.h = cell;
  sheet.rgba.assign(size_t(sheet.w) * size_t(sheet.h) * 4, 255);
  std::vector<u8> shown(size_t(mountainVariants), 0);
  for (const Stamp& st : mountains) {
    if (shown[size_t(st.variant)]) continue;
    shown[size_t(st.variant)] = 1;
    for (int j = 0; j < st.h * K; j++)
      for (int i = 0; i < st.w * K; i++) {
        const int v = st.ink[size_t(j / K) * size_t(st.w) + size_t(i / K)];
        u8* d = &sheet.rgba[(size_t(j + K) * size_t(sheet.w) + size_t(st.variant * cell + i + K)) * 4];
        const u8 g = u8(v < 0 ? 255 : 255 - v);
        d[0] = d[1] = d[2] = g;
        if (i % K == 0 || j % K == 0) d[0] = d[1] = d[2] = u8(g * 0.85);
      }
    // Точка привязки — красным.
    for (int dy = -2; dy <= 2; dy++)
      for (int dx = -2; dx <= 2; dx++) {
        const int px = st.variant * cell + K + st.ax * K + dx, py = K + st.ay * K + dy;
        if (px < 0 || py < 0 || px >= sheet.w || py >= sheet.h) continue;
        u8* d = &sheet.rgba[(size_t(py) * size_t(sheet.w) + size_t(px)) * 4];
        d[0] = 255;
        d[1] = d[2] = 0;
      }
  }
  codec::writePngFile(fs::join(debugDir, "mountains.png"), sheet, 6);
}

void writeExplainedImages(const std::string& debugDir, const codec::RgbaImage& flat, const std::vector<u8>& explained, const MapArt& art) {
  const int w = flat.w, h = flat.h;
  fs::makeDirs(debugDir);
  codec::RgbaImage dbg = flat;
  for (size_t i = 0; i < explained.size(); i++) {
    const bool inkHere = 255 - int(flat.rgba[i * 4 + 2]) >= 32 && int(flat.rgba[i * 4 + 2]) - int(flat.rgba[i * 4]) <= 40;
    if (inkHere && !explained[i]) {
      dbg.rgba[i * 4] = 255;
      dbg.rgba[i * 4 + 1] = 0;
      dbg.rgba[i * 4 + 2] = 0;
    }
  }
  codec::writePngFile(fs::join(debugDir, "unexplained.png"), dbg, 6);
  // Точки привязки знаков: зелёные (первый проход) и голубые (второй) крестики.
  codec::RgbaImage anc = flat;
  for (size_t k = 0; k < art.symbols.size(); k++) {
    const Symbol& q = art.symbols[k];
    const int cx = int(std::lround(q.x)), cy = int(std::lround(q.y));
    const bool second = std::fabs(q.x - std::round(q.x)) > 0.01 || std::fabs(q.y - std::round(q.y)) > 0.01 || q.s != 1;
    for (int d = -2; d <= 2; d++)
      for (int e = 0; e < 2; e++) {
        const int px = e ? cx + d : cx, py = e ? cy : cy + d;
        if (px < 0 || py < 0 || px >= w || py >= h) continue;
        u8* o = &anc.rgba[(size_t(py) * size_t(w) + size_t(px)) * 4];
        o[0] = 0;
        o[1] = second ? 160 : 200;
        o[2] = second ? 255 : 0;
      }
  }
  codec::writePngFile(fs::join(debugDir, "anchors.png"), anc, 6);
}

}  // namespace detail

// ---------------------------------------------------------------- разработка
std::vector<MountainSample> mountainSamples(const codec::RgbaImage& flat) {
  std::vector<MountainSample> out;
  for (const Stamp& st : mountainStamps(flat, nullptr)) {
    bool have = false;
    for (const MountainSample& m : out) have = have || m.design == st.design;
    if (have) continue;
    out.push_back({st.design, st.w, st.h, st.ax, st.ay, st.ink});
  }
  std::sort(out.begin(), out.end(), [](const MountainSample& a, const MountainSample& b) { return a.design < b.design; });
  return out;
}

void writeInventory(const codec::RgbaImage& flat, const std::string& dir) {
  fs::makeDirs(dir);
  const std::vector<InkComponent> comps = inkComponents(flat);
  struct Group {
    u64 shape = 0;
    std::vector<size_t> members;
  };
  std::unordered_map<u64, size_t> index;
  std::vector<Group> groups;
  for (size_t i = 0; i < comps.size(); i++) {
    auto [it, fresh] = index.try_emplace(comps[i].shape, groups.size());
    if (fresh) groups.push_back({comps[i].shape, {}});
    groups[it->second].members.push_back(i);
  }
  std::vector<size_t> order(groups.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    if (groups[a].members.size() != groups[b].members.size()) return groups[a].members.size() > groups[b].members.size();
    return comps[groups[a].members[0]].pixels > comps[groups[b].members[0]].pixels;
  });
  std::string txt = strf("Связных областей краски: %zu, разных форм: %zu\n", comps.size(), groups.size());
  int mountains = 0, darkComps = 0;
  for (const InkComponent& c : comps) {
    mountains += c.mountain;
    darkComps += c.dark > 0;
  }
  txt += strf("С телом горы: %d, с тёмными пикселями: %d\n\n", mountains, darkComps);
  txt += "№    раз  ширина×высота  пикселей  тёмных  гора  пример (x, y)\n";
  for (size_t k = 0; k < order.size() && k < 400; k++) {
    const Group& g = groups[order[k]];
    const InkComponent& c = comps[g.members[0]];
    txt += strf("%-4zu %5zu  %4d×%-4d      %7d  %6d  %s   %d, %d\n", k, g.members.size(), c.x1 - c.x0 + 1, c.y1 - c.y0 + 1, c.pixels, c.dark,
                c.mountain ? "да " : "нет", c.x0, c.y0);
  }
  // Крупные области (скопления гор, стены, пунктиры, необычные знаки).
  std::vector<size_t> big;
  for (size_t i = 0; i < comps.size(); i++) big.push_back(i);
  std::sort(big.begin(), big.end(), [&](size_t a, size_t b) { return comps[a].pixels > comps[b].pixels; });
  txt += "\nКрупнейшие области:\n";
  for (size_t k = 0; k < big.size() && k < 60; k++) {
    const InkComponent& c = comps[big[k]];
    txt += strf("  %4d×%-4d  пикселей %7d  тёмных %6d  гора %s  (%d, %d)\n", c.x1 - c.x0 + 1, c.y1 - c.y0 + 1, c.pixels, c.dark, c.mountain ? "да " : "нет",
                c.x0, c.y0);
  }
  std::string err;
  fs::writeFileAtomic(fs::join(dir, "inventory.txt"), txt, &err);
  std::fputs(txt.substr(0, std::min<size_t>(txt.size(), 6000)).c_str(), stdout);

  // Лист образцов: пример каждой из первых групп, увеличенный в K раз (полками слева направо).
  auto sheet = [&](const std::vector<size_t>& compIdx, int K, const std::string& name) {
    const int W = 2400, gap = 6;
    struct Place { size_t c; int x, y, cw, ch; };
    std::vector<Place> places;
    int x = gap, y = gap, rowH = 0;
    for (size_t ci : compIdx) {
      const InkComponent& c = comps[ci];
      const int cw = std::min(W - 2 * gap, (c.x1 - c.x0 + 5) * K), ch = std::min(600, (c.y1 - c.y0 + 5) * K);
      if (x + cw + gap > W) {
        x = gap;
        y += rowH + gap;
        rowH = 0;
      }
      places.push_back({ci, x, y, cw, ch});
      x += cw + gap;
      rowH = std::max(rowH, ch);
    }
    codec::RgbaImage out;
    out.w = W;
    out.h = y + rowH + gap;
    out.rgba.assign(size_t(out.w) * size_t(out.h) * 4, 255);
    for (size_t i = 0; i < out.rgba.size(); i += 4) {
      out.rgba[i] = 200;
      out.rgba[i + 1] = 30;
      out.rgba[i + 2] = 200;
    }
    for (const Place& p : places) {
      const InkComponent& c = comps[p.c];
      for (int yy = 0; yy < p.ch; yy++)
        for (int xx = 0; xx < p.cw; xx++) {
          const int sx = c.x0 - 2 + xx / K, sy = c.y0 - 2 + yy / K;
          u8* d = &out.rgba[(size_t(p.y + yy) * size_t(out.w) + size_t(p.x + xx)) * 4];
          if (sx < 0 || sy < 0 || sx >= flat.w || sy >= flat.h) continue;
          const u8* s = &flat.rgba[(size_t(sy) * size_t(flat.w) + size_t(sx)) * 4];
          d[0] = s[0];
          d[1] = s[1];
          d[2] = s[2];
        }
    }
    codec::writePngFile(fs::join(dir, name), out, 6);
  };
  std::vector<size_t> top;
  for (size_t k = 0; k < order.size() && k < 160; k++) top.push_back(groups[order[k]].members[0]);
  sheet(top, 6, "inventory.png");
  std::vector<size_t> largest(big.begin(), big.begin() + std::min<size_t>(big.size(), 40));
  sheet(largest, 2, "inventory_large.png");
}

namespace {

// Среднеквадратичная ошибка краски (255 − синий) рисунка горы против образца w × h с привязкой (ax, ay); вне образца
// и в незначимых пикселях ожидается белое. Штраф — за вывернутые шапку и тень.
double mountainError(const MountainParams& p, const std::vector<i16>& ink, int w, int h, int ax, int ay) {
  const int M = 3, W = w + 2 * M, H = h + 2 * M;
  gfx::Image img(W, H, gfx::premul(Color(255, 255, 255)));
  gfx::Canvas c(img);
  drawMountain(c, p, gfx::Pt{float(ax + M), float(ay + M)}, 1);
  double e = 0;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const int r = 255 - int(img.at(x, y) & 255);
      const int tx = x - M, ty = y - M;
      int t = 0;
      if (tx >= 0 && ty >= 0 && tx < w && ty < h) t = std::max<int>(0, ink[size_t(ty) * size_t(w) + size_t(tx)]);
      e += double(r - t) * double(r - t);
    }
  // Вершина — у середины основания (форма не перекашивается ради отдельных пикселей).
  const double pen = std::max(0.0, std::fabs(double(p[0]) - (double(p[0]) - p[2] + p[0] + p[3]) * 0.5) - 0.6);
  return std::sqrt(e / double(W * H)) + 50 * pen;
}

}  // namespace

std::string fitMountainIcon(const std::vector<i16>& ink, int w, int h, int ax, int ay, int design, int maxEvals, double* error) {
  MountainParams p = mountainParams(design);
  double best = mountainError(p, ink, w, h, ax, ay);
  std::array<float, kMountainParams> step;
  for (int i = 0; i < kMountainParams; i++) {
    const bool grayIdx = i == 5 || i == 7 || i == 9 || i == 12 || i == 14 || i == 17 || i == 19;
    const bool widthIdx = i == 6 || i == 8 || i == 13 || i == 16 || i == 18;
    step[size_t(i)] = grayIdx ? 16.f : widthIdx ? 0.2f : 0.4f;
  }
  int evals = 0;
  for (int round = 0; round < 12 && evals < maxEvals; round++) {
    bool improved = true;
    while (improved && evals < maxEvals) {
      improved = false;
      for (int i = 0; i < kMountainParams && evals < maxEvals; i++) {
        if (design == 0 && (i == 18 || i == 19)) continue;   // у рисунка 0 кончика нет
        for (float sgn : {1.f, -1.f}) {
          MountainParams q = p;
          q[size_t(i)] += sgn * step[size_t(i)];
          const double e = mountainError(q, ink, w, h, ax, ay);
          evals++;
          if (e < best - 1e-6) {
            best = e;
            p = q;
            improved = true;
            break;
          }
        }
      }
    }
    for (float& s : step) s *= 0.5f;
  }
  if (error) *error = best;
  std::string out = "{";
  for (int i = 0; i < kMountainParams; i++) out += strf("%s%.3ff", i ? ", " : "", p[size_t(i)]);
  return out + "}";
}

}  // namespace rg::map::art
