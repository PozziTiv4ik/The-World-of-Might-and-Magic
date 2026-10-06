// Regnum — карта мира как объекты: чтение и запись map.json (см. art.h).
#include "map/art.h"

#include <cstdio>
#include <cstring>

#include "base/fs.h"
#include "base/json.h"

namespace rg::map::art {

const char* symName(Sym k) {
  switch (k) {
    case Sym::Mountain: return "mountain";
    case Sym::Peak: return "peak";
    case Sym::Castle: return "castle";
    case Sym::Tower: return "tower";
    default: return "?";
  }
}

// ================================================================ палитра
Color Style::tone(u8 g) const {
  // Целочисленно: при чёрной краске и белой бумаге серый остаётся тем же до бита.
  auto ch = [g](u8 a, u8 b) { return u8((u32(a) * (255u - g) + u32(b) * g + 127u) / 255u); };
  return Color(ch(ink.r, paper.r), ch(ink.g, paper.g), ch(ink.b, paper.b));
}

Style paletteStyle(const Style& s, Palette p) {
  Style r = s;
  switch (p) {
    case Palette::Parchment:
      r.sea = Color::hex(0x375168);
      r.land = Color::hex(0xece6d6);
      r.water = Color::hex(0x2f4c6b);
      r.ink = Color::hex(0x3a352e);
      r.paper = r.land;
      r.outside = r.sea.darken(0.09f);   // океан продолжается за краем карты
      break;
    case Palette::Source:
    case Palette::Count: break;
  }
  return r;
}

namespace {

// Число с не более чем dec знаками после запятой, без хвостовых нулей.
void num(std::string& o, double v, int dec) {
  char b[48];
  std::snprintf(b, sizeof b, "%.*f", dec, v);
  char* e = b + std::strlen(b);
  if (std::strchr(b, '.')) {
    while (e > b && e[-1] == '0') --e;
    if (e > b && e[-1] == '.') --e;
  }
  *e = 0;
  if (std::strcmp(b, "-0") == 0) std::strcpy(b, "0");
  o += b;
}

void points(std::string& o, const std::vector<Vec2>& p, int dec) {
  o += '[';
  for (size_t i = 0; i < p.size(); i++) {
    if (i) o += ',';
    num(o, p[i].x, dec);
    o += ',';
    num(o, p[i].y, dec);
  }
  o += ']';
}

void rings(std::string& o, const char* key, const std::vector<Ring>& rs, int dec) {
  o += "  \"";
  o += key;
  o += "\": [";
  for (size_t i = 0; i < rs.size(); i++) {
    o += i ? ",\n    " : "\n    ";
    points(o, rs[i], dec);
  }
  o += rs.empty() ? "],\n" : "\n  ],\n";
}

std::string color(Color c) {
  char b[16];
  std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
  return b;
}

std::vector<Vec2> readPoints(const json::Value& v, const char* what) {
  const json::Array& a = v.items();
  if (a.size() % 2) fail(strf("map.json: нечётное число координат (%s).", what));
  std::vector<Vec2> out(a.size() / 2);
  for (size_t i = 0; i < out.size(); i++) out[i] = Vec2(a[2 * i].asNum(), a[2 * i + 1].asNum());
  return out;
}

std::vector<Ring> readRings(const json::Value& root, const char* key) {
  std::vector<Ring> out;
  for (const json::Value& r : root.arr(key)) {
    Ring ring = readPoints(r, key);
    if (ring.size() >= 3) out.push_back(std::move(ring));
  }
  return out;
}

Color readColor(const json::Value& v, std::string_view key, Color def) {
  const std::string s = v.str(key);
  if (s.size() != 7 || s[0] != '#') return def;
  auto hex = [&](size_t i) { return u8(std::strtoul(s.substr(i, 2).c_str(), nullptr, 16)); };
  return Color(hex(1), hex(3), hex(5));
}

}  // namespace

std::string toJson(const MapArt& a) {
  std::string o;
  o.reserve(size_t(8) << 20);
  o += "{\n";
  o += "  \"format\": \"regnum-map\",\n  \"version\": 1,\n";
  o += "  \"id\": " + json::write(json::Value(a.id)) + ",\n";
  o += "  \"source\": " + json::write(json::Value(a.source)) + ",\n";
  o += "  \"sourceSha256\": " + json::write(json::Value(a.sourceSha256)) + ",\n";
  o += "  \"width\": " + std::to_string(a.width) + ",\n  \"height\": " + std::to_string(a.height) + ",\n";
  o += "  \"style\": {\"sea\": \"" + color(a.style.sea) + "\", \"land\": \"" + color(a.style.land) + "\", \"water\": \"" + color(a.style.water) +
       "\", \"coastSoft\": ";
  num(o, a.style.coastSoft, 3);
  o += ", \"coastGlow\": ";
  num(o, a.style.coastGlow, 3);
  o += "},\n";
  rings(o, "land", a.land, 2);
  rings(o, "islets", a.islets, 2);
  rings(o, "water", a.water, 2);
  o += "  \"rivers\": [";
  for (size_t i = 0; i < a.rivers.size(); i++) {
    const River& r = a.rivers[i];
    o += i ? ",\n    {\"p\": " : "\n    {\"p\": ";
    points(o, r.pts, 2);
    o += ", \"w\": [";
    for (size_t k = 0; k < r.w.size(); k++) {
      if (k) o += ',';
      num(o, r.w[k], 2);
    }
    o += "]}";
  }
  o += a.rivers.empty() ? "],\n" : "\n  ],\n";
  o += "  \"lines\": [";
  for (size_t i = 0; i < a.lines.size(); i++) {
    const Line& l = a.lines[i];
    o += i ? ",\n    {\"p\": " : "\n    {\"p\": ";
    points(o, l.pts, 2);
    o += ", \"w\": ";
    num(o, l.w, 2);
    if (l.dash > 0) {
      o += ", \"dash\": ";
      num(o, l.dash, 2);
    }
    o += "}";
  }
  o += a.lines.empty() ? "],\n" : "\n  ],\n";
  // Знаки в порядке отрисовки (поздние перекрывают ранние): [вид, x, y, масштаб, вариант, ...]; виды — symbolKinds.
  o += "  \"symbolKinds\": [";
  for (int k = 0; k < int(Sym::Count); k++) o += std::string(k ? ", \"" : "\"") + symName(Sym(k)) + "\"";
  o += "],\n  \"symbols\": [";
  for (size_t i = 0; i < a.symbols.size(); i++) {
    const Symbol& s = a.symbols[i];
    o += i ? (i % 8 ? ", " : ",\n    ") : "\n    ";
    o += std::to_string(int(s.kind));
    o += ',';
    num(o, s.x, 2);
    o += ',';
    num(o, s.y, 2);
    o += ',';
    num(o, s.s, 3);
    o += ',';
    o += std::to_string(int(s.v));
  }
  o += a.symbols.empty() ? "]\n}\n" : "\n  ]\n}\n";
  return o;
}

MapArt fromJson(std::string_view text) {
  const json::Value v = json::parse(text);
  if (v.str("format") != "regnum-map") fail("map.json: это не карта Regnum (нет \"format\": \"regnum-map\").");
  MapArt a;
  a.id = v.str("id");
  a.source = v.str("source");
  a.sourceSha256 = v.str("sourceSha256");
  a.width = int(v.integer("width"));
  a.height = int(v.integer("height"));
  if (a.width <= 0 || a.height <= 0 || a.width > 100000 || a.height > 100000) fail("map.json: неверный размер карты.");
  const json::Value& st = v.obj("style");
  a.style.sea = readColor(st, "sea", a.style.sea);
  a.style.land = readColor(st, "land", a.style.land);
  a.style.water = readColor(st, "water", a.style.water);
  a.style.coastSoft = float(clamp(st.num("coastSoft", a.style.coastSoft), 0.0, 16.0));
  a.style.coastGlow = float(clamp(st.num("coastGlow", a.style.coastGlow), 0.0, 1.0));
  a.land = readRings(v, "land");
  a.islets = readRings(v, "islets");
  a.water = readRings(v, "water");
  for (const json::Value& r : v.arr("rivers")) {
    River rv;
    rv.pts = readPoints(r.get("p"), "rivers");
    for (const json::Value& w : r.arr("w")) rv.w.push_back(float(w.asNum()));
    if (rv.pts.size() >= 2 && rv.w.size() == rv.pts.size()) a.rivers.push_back(std::move(rv));
  }
  for (const json::Value& l : v.arr("lines")) {
    Line ln;
    ln.pts = readPoints(l.get("p"), "lines");
    ln.w = float(l.num("w", 2));
    ln.dash = float(l.num("dash", 0));
    if (ln.pts.size() >= 2) a.lines.push_back(std::move(ln));
  }
  // Знаки — в порядке отрисовки из файла (вид указывается по имени из symbolKinds).
  std::vector<int> kindOf;
  for (const json::Value& k : v.arr("symbolKinds")) {
    int code = -1;
    for (int q = 0; q < int(Sym::Count); q++)
      if (k.asStr() == symName(Sym(q))) code = q;
    kindOf.push_back(code);
  }
  const json::Array& sy = v.arr("symbols");
  if (sy.size() % 5) fail("map.json: знаки — ожидаются пятёрки: вид, x, y, масштаб, вариант.");
  a.symbols.reserve(sy.size() / 5);
  for (size_t i = 0; i + 4 < sy.size(); i += 5) {
    const i64 k = sy[i].asInt(-1);
    if (k < 0 || k >= i64(kindOf.size()) || kindOf[size_t(k)] < 0) continue;   // неизвестный вид знака — пропуск
    a.symbols.push_back(Symbol{Sym(kindOf[size_t(k)]), float(sy[i + 1].asNum()), float(sy[i + 2].asNum()), float(sy[i + 3].asNum(1)),
                               u8(clamp(sy[i + 4].asInt(0), i64(0), i64(255)))});
  }
  return a;
}

MapArt load(const std::string& path) {
  std::string err;
  auto text = fs::readFile(path, &err);
  if (!text) fail("Не удалось прочитать карту «" + path + "»: " + err);
  return fromJson(*text);
}

void save(const MapArt& a, const std::string& path) {
  std::string err;
  if (!fs::writeFileAtomic(path, toJson(a), &err)) fail("Не удалось записать карту «" + path + "»: " + err);
}

}  // namespace rg::map::art
