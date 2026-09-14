#include "model.hpp"
#include <algorithm>
#include <array>
#include <numeric>
#include <queue>
#include <set>
#include <unordered_map>

namespace atlas {
double distance(Point a, Point b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
double segmentDistance(Point p, Point a, Point b) {
    double dx = b.x - a.x, dy = b.y - a.y, d = dx * dx + dy * dy;
    if (d < 1e-16)
        return distance(p, a);
    double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / d, 0.0, 1.0);
    return distance(p, {a.x + t * dx, a.y + t * dy});
}
bool inside(Point p, const std::vector<Point> &poly) {
    if (poly.size() < 3)
        return false;
    bool result = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        auto a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            result = !result;
    }
    return result;
}
Json pointJson(Point p) {
    return Json::Array{p.x, p.y};
}
Point point(const Json &j) {
    if (!j.isArray() || j.size() != 2)
        return {};
    return {j[size_t(0)].num(), j[size_t(1)].num()};
}
static std::string lower(const std::string &text) {
    auto w = wide(text);
    if (!w.empty())
        CharLowerBuffW(w.data(), DWORD(w.size()));
    return utf8(w);
}
void Campaign::load(const fs::path &dir) {
    root = dir;
    entities.clear();
    auto file = dir / pathOf("09_Реестры/Сущности.json");
    if (!fs::exists(file))
        return;
    auto j = Json::parse(readText(file));
    if (!j["entities"].isArray())
        return;
    for (auto &e : j["entities"].arr())
        entities.push_back({e["id"].str(), e["name"].str(), e["type"].str(), e["path"].str(),
                            e["chapter"].str(), e["branch"].str(), e["source_ids"], e["front_ids"],
                            e["aliases"]});
}
const Entity *Campaign::find(const std::string &id) const {
    for (auto &e : entities)
        if (e.id == id)
            return &e;
    return nullptr;
}
std::vector<const Entity *> Campaign::search(const std::string &q, const std::string &type) const {
    std::vector<const Entity *> result;
    auto query = lower(q);
    for (auto &e : entities) {
        if (!type.empty() && e.type != type)
            continue;
        auto text = lower(e.id + " " + e.name + " " + e.aliases.dump(0));
        if (text.find(query) != std::string::npos)
            result.push_back(&e);
    }
    return result;
}
Map::Map() {
    create(4000, 3000, "Карта мира");
}
void Map::create(int w, int h, const std::string &name) {
    directory.clear();
    diskHash.clear();
    clearCache();
    doc = fields({{"schema_version", 1},
                  {"id", newId("MAP-")},
                  {"name", name},
                  {"width", w},
                  {"height", h},
                  {"status", "draft"},
                  {"story_anchor", nullptr},
                  {"layers", Json::array()},
                  {"features", Json::object()},
                  {"nodes", Json::object()},
                  {"arcs", Json::object()},
                  {"symbols", Json::object()},
                  {"background", "#18374D"},
                  {"style", "original"},
                  {"parent_version", nullptr}});
    addLayer("Территории");
    addLayer("Маршруты");
    addLayer("Места и подписи");
}
std::string Map::addLayer(const std::string &name, const std::string &kind) {
    auto id = newId("LAYER-");
    doc["layers"].push(fields({{"id", id},
                               {"name", name},
                               {"kind", kind},
                               {"visible", true},
                               {"locked", false},
                               {"opacity", 1.0},
                               {"blend_mode", 0}}));
    return id;
}
void Map::clearCache() {
    imageCache.clear();
    pdns.clear();
    edgeIndex.clear();
    edgeIndexCount = 0;
}
void Map::load(const fs::path &path) {
    auto dir = fs::is_directory(path) ? path : path.parent_path();
    auto text = readText(dir / L"map.json");
    auto value = Json::parse(text);
    if (value["schema_version"].num() != 1)
        throw std::runtime_error("Unsupported Atlas schema version; source file was not modified");
    Map candidate;
    candidate.doc = std::move(value);
    candidate.directory = dir;
    auto report = candidate.validate();
    if (report["errors"].size())
        throw std::runtime_error(report["errors"].dump());
    doc = std::move(candidate.doc);
    directory = dir;
    diskHash = hashText(text);
    clearCache();
}
void Map::save(const fs::path &path) {
    auto target = path.empty() ? directory : (path.extension() == L".json" ? path.parent_path() : path);
    if (target.empty())
        throw std::runtime_error("Choose a map directory first");
    auto report = validate();
    if (report["errors"].size())
        throw std::runtime_error(report["errors"].dump());
    FileLock lock(target);
    auto manifest = target / L"map.json";
    bool same = !directory.empty() && fs::weakly_canonical(target) == fs::weakly_canonical(directory);
    if (fs::exists(manifest)) {
        if (!same)
            throw std::runtime_error("Destination already contains a map; choose another folder");
        if (hashText(readText(manifest)) != diskHash)
            throw std::runtime_error(
                "The map changed on disk. Save to a new folder or reload before applying your changes.");
    }
    if (!same && !directory.empty()) {
        std::set<std::string> assets;
        for (auto &l : doc["layers"].arr())
            if (l["image"].isString())
                assets.insert(l["image"].str());
        for (auto &asset : assets) {
            auto src = safeChild(directory, asset), dst = safeChild(target, asset);
            auto bytes = readBytes(src);
            if (fs::exists(dst) && hashBytes(readBytes(dst)) != hashBytes(bytes))
                throw std::runtime_error("Destination contains a different asset: " + asset);
            if (!fs::exists(dst))
                atomicWrite(dst, bytes);
        }
        // Versions may reference earlier raster revisions, so Save As carries their assets too.
        for (auto section : {L"assets", L"versions"}) {
            auto source = directory / section;
            if (!fs::exists(source))
                continue;
            for (auto &entry : fs::directory_iterator(source)) {
                if (!entry.is_regular_file())
                    continue;
                auto relative = pathText(fs::path(section) / entry.path().filename());
                auto src = safeChild(directory, relative), dst = safeChild(target, relative);
                auto bytes = readBytes(src);
                if (fs::exists(dst) && hashBytes(readBytes(dst)) != hashBytes(bytes))
                    throw std::runtime_error("Destination file conflict: " + relative);
                if (!fs::exists(dst))
                    atomicWrite(dst, bytes);
            }
        }
    }
    auto text = doc.dump() + "\n";
    if (same && hashText(text) == diskHash)
        return;
    if (fs::exists(manifest))
        atomicText(target / L".atlas" / L"last-save.json", readText(manifest));
    atomicText(manifest, text);
    directory = target;
    diskHash = hashText(text);
}
void Map::autosave() const {
    if (directory.empty())
        return;
    auto data = fields({{"base_hash", diskHash}, {"recorded_at", nowUtc()}, {"document", doc}});
    atomicText(directory / L".atlas" / L"autosave.json", data.dump() + "\n");
}
bool Map::hasRecovery() const {
    if (directory.empty())
        return false;
    auto p = directory / L".atlas" / L"autosave.json";
    if (!fs::exists(p))
        return false;
    try {
        return !(Json::parse(readText(p))["document"] == doc);
    } catch (...) {
        return false;
    }
}
void Map::recover() {
    auto j = Json::parse(readText(directory / L".atlas" / L"autosave.json"));
    if (j["base_hash"].str() != diskHash)
        throw std::runtime_error(
            "Recovery belongs to a different disk revision. Open its JSON separately to merge.");
    Map candidate;
    candidate.directory = directory;
    candidate.doc = j["document"];
    if (candidate.validate()["errors"].size())
        throw std::runtime_error("Recovery document is invalid");
    doc = std::move(candidate.doc);
    clearCache();
}
void Map::importPdn(const fs::path &path, const fs::path &target) {
    PdnSource source(path);
    if (fs::exists(target / L"map.json"))
        throw std::runtime_error("Import destination already contains a map");
    auto data = readBytes(path);
    auto hash = hashBytes(data);
    Map result;
    result.create(source.width, source.height, "Карта мира");
    result.directory = target;
    result.doc["layers"] = Json::array();
    auto asset = "assets/" + hash + ".pdn";
    atomicWrite(safeChild(target, asset), data);
    for (size_t i = 0; i < source.layers.size(); i++) {
        auto &l = source.layers[i];
        auto id = "LAYER-PDN-" + hash.substr(0, 12) + "-" + std::to_string(i);
        result.doc["layers"].push(fields({{"id", id},
                                          {"name", l.name},
                                          {"kind", "raster"},
                                          {"visible", l.visible},
                                          {"locked", true},
                                          {"opacity", l.opacity / 255.0},
                                          {"blend_mode", l.blend},
                                          {"image", asset},
                                          {"source_type", "pdn"},
                                          {"pdn_layer", i},
                                          {"visibility", "gm"},
                                          {"known_to", Json::array()}}));
    }
    result.addLayer("Территории");
    result.addLayer("Маршруты");
    result.addLayer("Места и подписи");
    result.doc["import"] = fields({{"file_name", utf8(path.filename().wstring())},
                                   {"sha256", hash},
                                   {"recorded_at", nowUtc()},
                                   {"story_time", nullptr},
                                   {"pdn", source.metadata()}});
    result.save();
    *this = std::move(result);
}
void Map::importRaster(const fs::path &path, const fs::path &target) {
    auto image = loadImage(path);
    if (fs::exists(target / L"map.json"))
        throw std::runtime_error("Destination already contains a map");
    Map result;
    result.create(image->width, image->height, "Карта мира");
    result.directory = target;
    auto asset = result.storeImage(*image);
    auto raster = fields({{"id", newId("LAYER-")},
                          {"name", "Исходное изображение"},
                          {"kind", "raster"},
                          {"image", asset},
                          {"source_type", "png"},
                          {"visible", true},
                          {"locked", true},
                          {"opacity", 1},
                          {"blend_mode", 0},
                          {"visibility", "gm"},
                          {"known_to", Json::array()}});
    result.doc["layers"].arr().insert(result.doc["layers"].arr().begin(), raster);
    result.save();
    *this = std::move(result);
}
std::shared_ptr<Image> Map::raster(const Json &l) {
    auto key = l["image"].str() + "#" + l["pdn_layer"].dump(0);
    for (auto it = imageCache.begin(); it != imageCache.end(); ++it)
        if (it->first == key) {
            auto found = *it;
            imageCache.erase(it);
            imageCache.push_front(found);
            return found.second;
        }
    auto file = safeChild(directory, l["image"].str());
    std::shared_ptr<Image> image;
    if (l["source_type"].str() == "pdn") {
        auto asset = l["image"].str();
        if (!pdns.contains(asset)) {
            auto stem = utf8(file.stem().wstring());
            if (stem.size() == 64 && hashBytes(readBytes(file)) != stem)
                throw std::runtime_error("PDN asset checksum differs from its content address");
            pdns[asset] = std::make_shared<PdnSource>(file);
        }
        image = pdns.at(asset)->decode(size_t(l["pdn_layer"].num()));
    } else
        image = loadImage(file);
    if (image->width != doc["width"].num() || image->height != doc["height"].num())
        throw std::runtime_error("Raster layer dimensions do not match the map");
    imageCache.push_front({key, image});
    while (imageCache.size() > 2)
        imageCache.pop_back();
    return image;
}
std::string Map::storeImage(const Image &im) {
    if (directory.empty())
        throw std::runtime_error("Save the map before painting a raster layer");
    auto id = "assets/" + hashBytes(im.bgra) + "-" + std::to_string(im.width) + "x" +
              std::to_string(im.height) + ".png";
    auto path = safeChild(directory, id);
    if (!fs::exists(path))
        savePng(path, im);
    return id;
}
std::string Map::nodeAt(Point p, double snap) {
    if (snap > 0) {
        auto id = nearestNode(p, snap);
        if (!id.empty())
            return id;
    }
    auto id = newId("NODE-");
    doc["nodes"][id] = pointJson(p);
    return id;
}
std::string Map::edge(const std::string &a, const std::string &b) {
    if (edgeIndexCount != doc["arcs"].size()) {
        edgeIndex.clear();
        for (auto &[id, arc] : doc["arcs"].obj()) {
            auto &ns = arc["nodes"];
            if (ns.size() == 2) {
                auto first = ns[size_t(0)].str(), second = ns[size_t(1)].str();
                edgeIndex[std::minmax(first, second)] = id;
            }
        }
        edgeIndexCount = doc["arcs"].size();
    }
    std::pair<std::string, std::string> key = std::minmax(a, b);
    if (edgeIndex.contains(key))
        return edgeIndex.at(key);
    auto id = newId("ARC-");
    doc["arcs"][id] = fields({{"nodes", Json::Array{a, b}}});
    edgeIndex[key] = id;
    edgeIndexCount = doc["arcs"].size();
    return id;
}
std::string Map::addPath(const std::vector<Point> &pts, bool closed, const std::string &kind,
                         const std::string &layerId, const std::string &color, double width, double snap) {
    if (pts.size() < (closed ? 3u : 2u))
        throw std::runtime_error("Not enough points");
    std::vector<std::string> ids;
    std::set<std::string> domainNodes;
    const auto *owner = layer(layerId);
    std::string domain = owner ? (*owner)["domain"].str() : "";
    if (snap > 0 && !domain.empty())
        for (const auto &[id, f] : doc["features"].obj()) {
            const auto *l = layer(f["layer_id"].str());
            if (l && (*l)["domain"].str() == domain) {
                auto ns = featureNodes(f);
                domainNodes.insert(ns.begin(), ns.end());
            }
        }
    for (auto p : pts) {
        std::string n;
        if (snap > 0 && !domain.empty()) {
            double nearest = snap;
            for (const auto &id : domainNodes) {
                double d = distance(p, point(doc["nodes"][id]));
                if (d < nearest) {
                    nearest = d;
                    n = id;
                }
            }
            if (n.empty()) {
                n = nodeAt(p, 0);
                domainNodes.insert(n);
            }
        } else
            n = nodeAt(p, snap);
        if (ids.empty() || ids.back() != n)
            ids.push_back(n);
    }
    if (ids.size() > 1 && ids.front() == ids.back())
        ids.pop_back();
    if (ids.size() < (closed ? 3u : 2u))
        throw std::runtime_error("Contour collapsed after snapping");
    Json arcs = Json::array();
    for (size_t i = 1; i < ids.size() + (closed ? 1 : 0); i++) {
        auto a = ids[i - 1], b = ids[i % ids.size()];
        auto eid = edge(a, b);
        bool reverse = doc["arcs"][eid]["nodes"][size_t(0)].str() != a;
        arcs.push(fields({{"id", eid}, {"reverse", reverse}}));
    }
    auto id = newId("MAPOBJ-");
    doc["features"][id] = fields({{"id", id},
                                  {"kind", kind},
                                  {"name", kind == "region"  ? "Новая территория"
                                           : kind == "river" ? "Река"
                                           : kind == "route" ? "Маршрут"
                                           : kind == "zone"  ? "Зона"
                                                             : "Линия"},
                                  {"layer_id", layerId},
                                  {"entity_id", nullptr},
                                  {"arcs", arcs},
                                  {"closed", closed},
                                  {"fill", closed ? color : "none"},
                                  {"stroke", color},
                                  {"stroke_width", width},
                                  {"opacity", closed ? 0.65 : 1.0},
                                  {"font_size", 24},
                                  {"label_offset", Json::Array{0, 0}},
                                  {"visibility", "gm"},
                                  {"known_to", Json::array()},
                                  {"truth", "unknown"},
                                  {"evidence_ids", Json::array()}});
    doc["next_z"] = doc["next_z"].num() + 1;
    doc["features"][id]["z_order"] = doc["next_z"];
    return id;
}
std::string Map::addSymbol(Point p, const std::string &symbol, const std::string &layerId,
                           const std::string &color, double size) {
    auto id = newId("MAPOBJ-");
    doc["features"][id] = fields({{"id", id},
                                  {"kind", symbol == "label" ? "label" : "symbol"},
                                  {"name", symbol == "label" ? "Подпись" : ""},
                                  {"symbol_id", symbol},
                                  {"position", pointJson(p)},
                                  {"size", size},
                                  {"rotation", 0},
                                  {"layer_id", layerId},
                                  {"entity_id", nullptr},
                                  {"fill", color},
                                  {"stroke", color},
                                  {"stroke_width", 2},
                                  {"font_size", 24},
                                  {"opacity", 1.0},
                                  {"label_offset", Json::Array{0, 30}},
                                  {"visibility", "gm"},
                                  {"known_to", Json::array()},
                                  {"truth", "unknown"},
                                  {"evidence_ids", Json::array()}});
    doc["next_z"] = doc["next_z"].num() + 1;
    doc["features"][id]["z_order"] = doc["next_z"];
    return id;
}
std::vector<std::vector<Point>> Map::paths(const Json &f) const {
    std::vector<std::vector<Point>> result;
    std::vector<Json> rings;
    if (f["rings"].isArray())
        rings = f["rings"].arr();
    else if (f["arcs"].isArray())
        rings.push_back(f["arcs"]);
    for (auto &refs : rings) {
        std::vector<Point> pts;
        for (auto &ref : refs.arr()) {
            auto id = ref["id"].str();
            if (!doc["arcs"].contains(id))
                continue;
            auto ns = doc["arcs"][id]["nodes"].arr();
            if (ref["reverse"].boolean())
                std::reverse(ns.begin(), ns.end());
            for (auto &n : ns) {
                auto p = point(doc["nodes"][n.str()]);
                if (pts.empty() || distance(pts.back(), p) > 1e-8)
                    pts.push_back(p);
            }
        }
        result.push_back(std::move(pts));
    }
    return result;
}
Point Map::anchor(const Json &f) const {
    if (f["position"].isArray())
        return point(f["position"]);
    auto ps = paths(f);
    Point p{};
    size_t n = 0;
    for (auto &ring : ps)
        for (auto q : ring) {
            p.x += q.x;
            p.y += q.y;
            n++;
        }
    if (n) {
        p.x /= n;
        p.y /= n;
    }
    return p;
}
std::vector<std::string> Map::featureNodes(const Json &f) const {
    std::set<std::string> ids;
    auto append = [&](const Json &refs) {
        if (!refs.isArray())
            return;
        for (auto &ref : refs.arr()) {
            auto &ns = doc["arcs"][ref["id"].str()]["nodes"];
            if (ns.isArray())
                for (auto &n : ns.arr())
                    ids.insert(n.str());
        }
    };
    append(f["arcs"]);
    if (f["rings"].isArray())
        for (auto &ring : f["rings"].arr())
            append(ring);
    return {ids.begin(), ids.end()};
}
Json *Map::layer(const std::string &id) {
    for (auto &l : doc["layers"].arr())
        if (l["id"].str() == id)
            return &l;
    return nullptr;
}
const Json *Map::layer(const std::string &id) const {
    for (auto &l : doc["layers"].arr())
        if (l["id"].str() == id)
            return &l;
    return nullptr;
}
std::string Map::vectorLayer() {
    for (auto it = doc["layers"].arr().rbegin(); it != doc["layers"].arr().rend(); ++it)
        if ((*it)["kind"].str() == "vector" && !(*it)["locked"].boolean() && (*it)["visible"].boolean(true))
            return (*it)["id"].str();
    return addLayer("Объекты карты");
}
std::string Map::nearestNode(Point p, double radius) const {
    std::string found;
    for (auto &[id, n] : doc["nodes"].obj()) {
        double d = distance(p, point(n));
        if (d < radius) {
            radius = d;
            found = id;
        }
    }
    return found;
}
std::vector<const Json *> Map::orderedFeatures(const std::string &layerId) const {
    std::vector<const Json *> result;
    for (auto &[id, f] : doc["features"].obj())
        if (f["layer_id"].str() == layerId)
            result.push_back(&f);
    std::sort(result.begin(), result.end(), [](const Json *a, const Json *b) {
        if ((*a)["z_order"].num() != (*b)["z_order"].num())
            return (*a)["z_order"].num() < (*b)["z_order"].num();
        return (*a)["id"].str() < (*b)["id"].str();
    });
    return result;
}
std::string Map::hit(Point p, double tolerance, bool parent, double zoom, SelectionDomain scope,
                     const std::string &active) const {
    for (auto lit = doc["layers"].arr().rbegin(); lit != doc["layers"].arr().rend(); ++lit) {
        auto &l = *lit;
        if (!l["visible"].boolean(true) || l["locked"].boolean())
            continue;
        auto ordered = orderedFeatures(l["id"].str());
        for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
            auto &f = **it;
            if (!selectable(f, scope, active))
                continue;
            if (f["kind"].str() == "label" && (!f["show_label"].boolean(true) || zoom < f["min_zoom"].num()))
                continue;
            if (f["role"].str() == "settlement_label" && f["font_size"].num(14) * zoom < 10)
                continue;
            auto resultId =
                parent && scope != SelectionDomain::Objects && scope != SelectionDomain::ActiveLayer &&
                        f["role"].str() == "country_label" && doc["features"].contains(f["parent_id"].str())
                    ? f["parent_id"].str()
                    : f["id"].str();
            if (f["layer_id"].str() != l["id"].str())
                continue;
            if (f["position"].isArray() &&
                distance(p, point(f["position"])) < std::max(tolerance, f["size"].num(24) * 0.75))
                return resultId;
            if (!f["name"].str().empty() && f["show_label"].boolean(true)) {
                auto a = anchor(f), d = point(f["label_offset"]);
                double h = f["font_size"].num(24), w = std::max(32.0, wide(f["name"].str()).size() * h * .65);
                if (std::abs(p.x - a.x - d.x) < w / 2 + tolerance &&
                    std::abs(p.y - a.y - d.y) < h * .65 + tolerance)
                    return resultId;
            }
            bool in = false;
            for (auto &ring : paths(f)) {
                if (f["closed"].boolean() && inside(p, ring))
                    in = !in;
                for (size_t i = 1; i < ring.size(); i++)
                    if (segmentDistance(p, ring[i - 1], ring[i]) < tolerance + f["stroke_width"].num() / 2)
                        return f["id"].str();
            }
            if (in)
                return f["id"].str();
        }
    }
    return {};
}
void Map::eraseFeature(const std::string &id) {
    std::vector<std::string> children;
    for (auto &[child, f] : doc["features"].obj())
        if (child != id && f["kind"].str() == "label" && f["parent_id"].str() == id)
            children.push_back(child);
    doc["features"].obj().erase(id);
    for (auto &child : children)
        doc["features"].obj().erase(child);
}
void Map::moveNode(const std::string &id, Point p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y))
        throw std::runtime_error("Invalid node coordinate");
    if (!doc["nodes"].contains(id))
        throw std::runtime_error("Unknown node " + id);
    for (auto &[fid, feature] : doc["features"].obj()) {
        auto owner = layer(feature["layer_id"].str());
        if (!owner || !(*owner)["locked"].boolean())
            continue;
        auto nodes = featureNodes(feature);
        if (std::find(nodes.begin(), nodes.end(), id) != nodes.end())
            throw std::runtime_error("Узел связан с заблокированным слоем. Сначала разблокируй слой.");
    }
    doc["nodes"][id] = pointJson(p);
}
void Map::moveFeature(const std::string &id, Point delta) {
    moveFeatures({id}, delta);
}
void Map::moveFeatures(const std::vector<std::string> &ids, Point delta) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y))
        throw std::runtime_error("Invalid movement");
    if (delta.x == 0 && delta.y == 0)
        return;
    std::set<std::string> nodes, moving(ids.begin(), ids.end());
    for (auto &id : ids) {
        if (!doc["features"].contains(id))
            throw std::runtime_error("Unknown object");
        const auto &f = doc["features"][id];
        auto l = layer(f["layer_id"].str());
        if (!l || (*l)["locked"].boolean())
            throw std::runtime_error("Слой объекта заблокирован");
        auto ns = featureNodes(f);
        nodes.insert(ns.begin(), ns.end());
    }
    for (const auto &[id, f] : doc["features"].obj()) {
        auto l = layer(f["layer_id"].str());
        if (f["role"].str() == "country_label" && moving.contains(f["parent_id"].str()) && l &&
            !(*l)["locked"].boolean())
            moving.insert(id);
    }
    // Translation is a rigid object operation. Border editing deliberately shares nodes;
    // moving a country must never stretch a neighbour (including locked water geometry).
    // Clone only nodes referenced outside the moving group, preserving internal topology.
    std::set<std::string> externalNodes, externalArcs;
    auto refs = [](Json &f, const std::function<void(Json &)> &visit) {
        if (f.contains("arcs") && f["arcs"].isArray())
            for (auto &r : f["arcs"].arr())
                visit(r);
        if (f.contains("rings") && f["rings"].isArray())
            for (auto &ring : f["rings"].arr())
                for (auto &r : ring.arr())
                    visit(r);
    };
    for (auto &[id, f] : doc["features"].obj()) {
        if (moving.contains(id))
            continue;
        refs(f, [&](Json &r) {
            auto aid = r["id"].str();
            externalArcs.insert(aid);
            for (auto &n : doc["arcs"][aid]["nodes"].arr())
                if (nodes.contains(n.str()))
                    externalNodes.insert(n.str());
        });
    }
    std::map<std::string, std::string> clonedNodes, clonedArcs;
    for (const auto &id : externalNodes) {
        auto clone = newId("NODE-");
        doc["nodes"][clone] = doc["nodes"][id];
        clonedNodes[id] = clone;
        nodes.erase(id);
        nodes.insert(clone);
    }
    std::set<std::string> prepared;
    for (const auto &id : moving) {
        refs(doc["features"][id], [&](Json &ref) {
            auto aid = ref["id"].str();
            bool touched = false;
            for (const auto &n : doc["arcs"][aid]["nodes"].arr())
                touched = touched || clonedNodes.contains(n.str());
            if (!touched && !clonedArcs.contains(aid))
                return;
            if (externalArcs.contains(aid)) {
                if (!clonedArcs.contains(aid)) {
                    auto clone = newId("ARC-");
                    doc["arcs"][clone] = doc["arcs"][aid];
                    doc["arcs"][clone]["id"] = clone;
                    clonedArcs[aid] = clone;
                }
                aid = clonedArcs[aid];
                ref["id"] = aid;
            }
            if (prepared.insert(aid).second)
                for (auto &n : doc["arcs"][aid]["nodes"].arr())
                    if (clonedNodes.contains(n.str()))
                        n = clonedNodes[n.str()];
        });
    }
    edgeIndex.clear();
    edgeIndexCount = 0;
    for (auto &id : nodes) {
        auto p = point(doc["nodes"][id]);
        doc["nodes"][id] = pointJson({p.x + delta.x, p.y + delta.y});
    }
    for (auto &id : moving) {
        auto &f = doc["features"][id];
        if (!f["position"].isArray())
            continue;
        auto p = point(f["position"]);
        f["position"] = pointJson({p.x + delta.x, p.y + delta.y});
    }
}
Json Map::validate(const Campaign *campaign) const {
    Json result = fields({{"errors", Json::array()}, {"warnings", Json::array()}});
    auto error = [&](std::string s) { result["errors"].push(s); };
    auto validColor = [](const Json &j) {
        if (j.null())
            return true;
        auto s = j.str();
        return s == "none" || (s.size() == 7 && s[0] == '#' &&
                               s.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos);
    };
    if (doc["schema_version"].num() != 1)
        error("Unsupported schema_version");
    if (!doc["layers"].isArray() || !doc["features"].isObject() || !doc["nodes"].isObject() ||
        !doc["arcs"].isObject()) {
        error("Missing document collections");
        return result;
    }
    double w = doc["width"].num(), h = doc["height"].num();
    if (w < 1 || h < 1 || w > 16384 || h > 16384 || w * h > 64000000 || std::floor(w) != w ||
        std::floor(h) != h)
        error("Invalid dimensions");
    std::set<std::string> layerIds;
    for (auto &l : doc["layers"].arr()) {
        auto id = l["id"].str();
        if (id.empty() || !layerIds.insert(id).second)
            error("Duplicate/missing layer ID");
        if (l["opacity"].num(1) < 0 || l["opacity"].num(1) > 1)
            error("Invalid layer opacity");
        if (l["kind"].str() != "vector" && l["kind"].str() != "raster")
            error("Unsupported layer kind");
        double blend = l["blend_mode"].num();
        if (blend < 0 || blend > 13 || blend != std::floor(blend))
            error("Unsupported blend mode");
        if (l["kind"].str() == "raster") {
            if (l["image"].str().empty())
                error("Missing raster asset");
            else if (!directory.empty())
                try {
                    if (!fs::exists(safeChild(directory, l["image"].str())))
                        error("Missing asset: " + l["image"].str());
                } catch (const std::exception &e) {
                    error(e.what());
                }
        }
    }
    for (auto &[id, n] : doc["nodes"].obj())
        if (!n.isArray() || n.size() != 2 || !n[size_t(0)].isNumber() || !n[size_t(1)].isNumber())
            error("Invalid node: " + id);
    for (auto &[id, arc] : doc["arcs"].obj()) {
        auto &ns = arc["nodes"];
        if (!ns.isArray() || ns.size() < 2) {
            error("Invalid arc: " + id);
            continue;
        }
        for (auto &n : ns.arr())
            if (!doc["nodes"].contains(n.str()))
                error("Missing node in arc: " + id);
        if (arc.contains("control_nodes")) {
            const auto &cs = arc["control_nodes"];
            if (!cs.isArray() || cs.size() < 2)
                error("Invalid border controls: " + id);
            else {
                size_t next = 0;
                for (const auto &c : cs.arr()) {
                    while (next < ns.size() && ns[next].str() != c.str())
                        next++;
                    if (next == ns.size()) {
                        error("Control outside arc or out of order: " + id);
                        break;
                    }
                    next++;
                }
                if (cs[0].str() != ns[0].str() || cs[cs.size() - 1].str() != ns[ns.size() - 1].str())
                    error("Border control endpoints missing: " + id);
            }
        }
    }
    std::set<std::string> physicalNodes, politicalNodes;
    if (doc["land_state_separated"].boolean())
        for (const auto &[id, f] : doc["features"].obj()) {
            auto role = f["role"].str();
            if (role != "land" && role != "country")
                continue;
            auto ns = featureNodes(f);
            auto &set = role == "land" ? physicalNodes : politicalNodes;
            set.insert(ns.begin(), ns.end());
        }
    for (const auto &id : physicalNodes)
        if (politicalNodes.contains(id))
            error("Physical and political geometry share a node: " + id);
    for (auto &[id, f] : doc["features"].obj()) {
        if (!validColor(f["stroke"]) || !validColor(f["fill"]) || !validColor(f["text_color"]))
            error("Invalid feature color: " + id);
        if (f["id"].str() != id)
            error("Feature ID/key mismatch: " + id);
        if (f.contains("territory_scope") && f["territory_scope"].str() != "land" &&
            f["territory_scope"].str() != "land_sea")
            error("Invalid territory scope: " + id);
        if (!layerIds.contains(f["layer_id"].str()))
            error("Missing feature layer: " + id);
        if (campaign && !f["entity_id"].str().empty() && !campaign->find(f["entity_id"].str()))
            error("Unresolved entity: " + f["entity_id"].str());
        auto refs = [&](const Json &list) {
            if (!list.isArray()) {
                error("Invalid arc list: " + id);
                return;
            }
            std::string first, last;
            for (auto &ref : list.arr()) {
                if (!doc["arcs"].contains(ref["id"].str())) {
                    error("Missing arc: " + ref["id"].str());
                    continue;
                }
                auto &nodes = doc["arcs"][ref["id"].str()]["nodes"];
                if (!nodes.isArray() || nodes.size() < 2)
                    continue;
                auto start = nodes[size_t(0)].str(), end = nodes[nodes.size() - 1].str();
                if (ref["reverse"].boolean())
                    std::swap(start, end);
                if (first.empty())
                    first = start;
                if (!last.empty() && last != start)
                    error("Disconnected contour: " + id);
                last = end;
            }
            if (f["closed"].boolean() && (first.empty() || first != last))
                error("Unclosed region: " + id);
        };
        if (f["arcs"].isArray())
            refs(f["arcs"]);
        if (f["rings"].isArray())
            for (auto &ring : f["rings"].arr())
                refs(ring);
        if (f["kind"].str() == "symbol" || f["kind"].str() == "label") {
            if (!f["position"].isArray() || f["position"].size() != 2 ||
                !f["position"][size_t(0)].isNumber() || !f["position"][size_t(1)].isNumber())
                error("Invalid symbol position: " + id);
        }
        if (f["opacity"].num(1) < 0 || f["opacity"].num(1) > 1)
            error("Invalid feature opacity: " + id);
        if (f["stroke_width"].num(1) < 0 || f["stroke_width"].num(1) > 10000)
            error("Invalid stroke width: " + id);
    }
    if (doc["symbols"].isObject())
        for (auto &[id, def] : doc["symbols"].obj()) {
            if (!def["paths"].isArray()) {
                error("Missing symbol paths: " + id);
                continue;
            }
            auto paintValid = [&](const Json &v) {
                return v.null() || (v.isString() && (v.str() == "currentColor" || validColor(v)));
            };
            for (auto &path : def["paths"].arr()) {
                if (!path["points"].isArray()) {
                    error("Invalid symbol path: " + id);
                    continue;
                }
                for (auto &p : path["points"].arr())
                    if (!p.isArray() || p.size() != 2 || !p[size_t(0)].isNumber() || !p[size_t(1)].isNumber())
                        error("Invalid symbol point: " + id);
                if ((!path["fill"].isBool() && !paintValid(path["fill"])) || !paintValid(path["stroke"]))
                    error("Invalid symbol paint: " + id);
                if (!path["stroke_width"].null() &&
                    (!path["stroke_width"].isNumber() || path["stroke_width"].num() < 0))
                    error("Invalid symbol stroke: " + id);
            }
        }
    if (campaign && doc["story_anchor"].isObject()) {
        auto &a = doc["story_anchor"];
        auto s = campaign->find(a["scene_id"].str());
        if (!s || s->type != "scene")
            error("Unknown story scene");
        else if (a["chapter"].num() != std::strtod(s->chapter.c_str(), nullptr))
            error("Scene/chapter mismatch");
    }
    return result;
}
std::string Map::snapshot(const std::string &label, const Json &anchor, bool accepted) {
    if (directory.empty())
        throw std::runtime_error("Save the map before creating a version");
    if (label.empty())
        throw std::runtime_error("Version name is required");
    FileLock lock(directory);
    if (fs::exists(directory / L"map.json") && hashText(readText(directory / L"map.json")) != diskHash)
        throw std::runtime_error("Map changed on disk before snapshot. Reload or save to another directory.");
    auto id = newId("MAPVER-");
    Json state = doc;
    state["story_anchor"] = anchor;
    auto snap = fields({{"id", id},
                        {"label", label},
                        {"recorded_at", nowUtc()},
                        {"status", accepted ? "accepted" : "draft"},
                        {"parent", doc["parent_version"]},
                        {"document_hash", hashText(state.dump())},
                        {"document", state}});
    snap["asset_hashes"] = Json::object();
    for (auto &l : state["layers"].arr())
        if (l["image"].isString()) {
            auto asset = l["image"].str();
            if (!snap["asset_hashes"].contains(asset))
                snap["asset_hashes"][asset] = hashBytes(readBytes(safeChild(directory, asset)));
        }
    atomicText(directory / L"versions" / pathOf(id + ".json"), snap.dump() + "\n");
    doc["parent_version"] = id;
    doc["story_anchor"] = anchor;
    return id;
}
std::vector<Json> Map::versions() const {
    std::vector<Json> list;
    auto dir = directory / L"versions";
    if (directory.empty() || !fs::exists(dir))
        return list;
    for (auto &f : fs::directory_iterator(dir))
        if (f.path().extension() == L".json") {
            auto j = Json::parse(readText(f.path()));
            Json header = j;
            header.obj().erase("document");
            list.push_back(std::move(header));
        }
    std::sort(list.begin(), list.end(),
              [](auto &a, auto &b) { return a["recorded_at"].str() < b["recorded_at"].str(); });
    return list;
}
Json Map::version(const std::string &id) const {
    if (id.empty() ||
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") !=
            std::string::npos)
        throw std::runtime_error("Invalid version ID");
    auto snap = Json::parse(readText(directory / L"versions" / pathOf(id + ".json")));
    if (hashText(snap["document"].dump()) != snap["document_hash"].str())
        throw std::runtime_error("Version integrity check failed");
    if (snap["asset_hashes"].isObject())
        for (auto &[asset, hash] : snap["asset_hashes"].obj())
            if (hashBytes(readBytes(safeChild(directory, asset))) != hash.str())
                throw std::runtime_error("Version raster asset integrity check failed: " + asset);
    return snap;
}
void Map::restore(const std::string &id) {
    doc = version(id)["document"];
    doc["status"] = "draft";
    doc["parent_version"] = id;
    clearCache();
}
Json Map::diff(const Json &old) const {
    Json out = fields({{"changes", Json::array()}});
    for (auto key : {"nodes", "arcs", "features", "symbols"}) {
        std::set<std::string> ids;
        if (old[key].isObject())
            for (auto &[id, v] : old[key].obj())
                ids.insert(id);
        if (doc[key].isObject())
            for (auto &[id, v] : doc[key].obj())
                ids.insert(id);
        for (auto &id : ids)
            if (!(old[key][id] == doc[key][id]))
                out["changes"].push(fields(
                    {{"collection", key}, {"id", id}, {"before", old[key][id]}, {"after", doc[key][id]}}));
    }
    for (auto key : {"layers", "story_anchor", "name", "style"})
        if (!(old[key] == doc[key]))
            out["changes"].push(fields({{"field", key}, {"before", old[key]}, {"after", doc[key]}}));
    return out;
}
void Map::apply(const Json &patch) {
    if (!patch["operations"].isArray())
        throw std::runtime_error("Patch operations must be an array");
    Json before = doc;
    try {
        for (auto &op : patch["operations"].arr()) {
            auto action = op["op"].str(), id = op["id"].str();
            if ((action == "move_node" || action == "move_control" || action == "insert_control") &&
                (!op["position"].isArray() || op["position"].size() != 2 ||
                 !op["position"][size_t(0)].isNumber() || !op["position"][size_t(1)].isNumber()))
                throw std::runtime_error(action + " requires two numeric coordinates");
            if (action == "move_control")
                moveBorderControl(id, op["node_id"].str(), point(op["position"]),
                                  op["allow_sea"].boolean(true));
            else if (action == "insert_control")
                insertBorderControl(id, point(op["position"]), op["allow_sea"].boolean(true));
            else if (action == "delete_control")
                deleteBorderControl(id, op["node_id"].str());
            else if (action == "move_border")
                moveBorder(id, point(op["grab"]), point(op["delta"]), op["radius"].num(200));
            else if (action == "move_node")
                moveNode(id, point(op["position"]));
            else if (action == "set_feature") {
                if (!doc["features"].contains(id))
                    throw std::runtime_error("Unknown feature " + id);
                for (auto &[k, v] : op["values"].obj())
                    if (k != "id")
                        doc["features"][id][k] = v;
            } else if (action == "delete_feature")
                eraseFeature(id);
            else if (action == "add_feature") {
                auto f = op["feature"];
                id = f["id"].str();
                if (id.empty() || doc["features"].contains(id))
                    throw std::runtime_error("Duplicate/missing feature ID");
                doc["features"][id] = f;
            } else if (action == "put_node" || action == "put_arc" || action == "put_symbol")
                doc[action == "put_node"  ? "nodes"
                    : action == "put_arc" ? "arcs"
                                          : "symbols"][id] = op["value"];
            else if (action == "set_anchor")
                doc["story_anchor"] = op["value"];
            else if (action == "set_layer") {
                auto l = layer(id);
                if (!l)
                    throw std::runtime_error("Unknown layer");
                for (auto &[k, v] : op["values"].obj())
                    if (k != "id")
                        (*l)[k] = v;
            } else
                throw std::runtime_error("Unknown patch operation: " + action);
        }
        auto errors = validate()["errors"];
        if (errors.size())
            throw std::runtime_error(errors.dump());
        clearCache();
    } catch (...) {
        doc = std::move(before);
        throw;
    }
}
Json Map::forCharacter(const std::string &id) const {
    if (id.empty())
        throw std::runtime_error("Character ID required");
    Json out = doc;
    out["features"] = Json::object();
    out["nodes"] = Json::object();
    out["arcs"] = Json::object();
    out["layers"] = Json::array();
    out["story_anchor"] = nullptr;
    out.obj().erase("import");
    out["parent_version"] = nullptr;
    auto allowed = [&](const Json &item) {
        if (item["visibility"].str() == "gm")
            return false;
        if (item["visibility"].str() == "public")
            return item["evidence_ids"].isArray() && item["evidence_ids"].size() > 0;
        bool knows = false;
        if (item["known_to"].isArray())
            for (auto &who : item["known_to"].arr())
                if (who.str() == id)
                    knows = true;
        if (knows && item["knowledge_evidence"].isArray())
            for (auto &basis : item["knowledge_evidence"].arr())
                if (basis["character_id"].str() == id && basis["evidence_ids"].isArray() &&
                    basis["evidence_ids"].size() > 0 && !basis["basis"].str().empty())
                    return true;
        return false;
    };
    std::set<std::string> lids;
    for (auto &l : doc["layers"].arr())
        if (l["kind"].str() == "vector" || allowed(l)) {
            out["layers"].push(l);
            lids.insert(l["id"].str());
        }
    for (auto &[fid, f] : doc["features"].obj())
        if (lids.contains(f["layer_id"].str()) && allowed(f)) {
            out["features"][fid] = f;
            auto add = [&](const Json &refs) {
                if (!refs.isArray())
                    return;
                for (auto &ref : refs.arr()) {
                    auto aid = ref["id"].str();
                    out["arcs"][aid] = doc["arcs"][aid];
                    for (auto &n : doc["arcs"][aid]["nodes"].arr())
                        out["nodes"][n.str()] = doc["nodes"][n.str()];
                }
            };
            add(f["arcs"]);
            if (f["rings"].isArray())
                for (auto &ring : f["rings"].arr())
                    add(ring);
        }
    return out;
}
void History::push(const Json &before, const Json &after, const std::string &action) {
    if (before == after)
        return;
    undoStack.push_back({action, before.dump(0)});
    redoStack.clear();
    size_t n = 0;
    for (auto &e : undoStack)
        n += e.second.size();
    while (undoStack.size() > 200 || (n > 32 * 1024 * 1024 && undoStack.size() > 1)) {
        n -= undoStack.front().second.size();
        undoStack.pop_front();
    }
}
bool History::undo(Json &doc) {
    if (undoStack.empty())
        return false;
    auto e = std::move(undoStack.back());
    undoStack.pop_back();
    redoStack.push_back({e.first, doc.dump(0)});
    doc = Json::parse(e.second);
    return true;
}
bool History::redo(Json &doc) {
    if (redoStack.empty())
        return false;
    auto e = std::move(redoStack.back());
    redoStack.pop_back();
    undoStack.push_back({e.first, doc.dump(0)});
    doc = Json::parse(e.second);
    return true;
}
void History::clear() {
    undoStack.clear();
    redoStack.clear();
}
std::vector<std::string> History::labels() const {
    std::vector<std::string> result;
    for (auto it = undoStack.rbegin(); it != undoStack.rend(); ++it)
        result.push_back(it->first);
    return result;
}
std::vector<Point> simplify(const std::vector<Point> &input, double tol, bool closed) {
    if (input.size() < 3)
        return input;
    auto pts = input;
    if (closed && distance(pts.front(), pts.back()) > 1e-8)
        pts.push_back(pts.front());
    std::vector<bool> keep(pts.size());
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> work{{0, pts.size() - 1}};
    while (!work.empty()) {
        auto [a, b] = work.back();
        work.pop_back();
        double best = tol;
        size_t index = 0;
        for (size_t i = a + 1; i < b; i++) {
            double d = segmentDistance(pts[i], pts[a], pts[b]);
            if (d > best) {
                best = d;
                index = i;
            }
        }
        if (index) {
            keep[index] = true;
            work.push_back({a, index});
            work.push_back({index, b});
        }
    }
    std::vector<Point> out;
    for (size_t i = 0; i < pts.size(); i++)
        if (keep[i])
            out.push_back(pts[i]);
    if (closed && out.size() > 1 && distance(out.front(), out.back()) < 1e-8)
        out.pop_back();
    return out;
}
std::vector<std::vector<Point>> traceRegion(const Image &image, Point seed, int tolerance, size_t limit) {
    int w = image.width, h = image.height, sx = int(seed.x), sy = int(seed.y);
    if (sx < 0 || sy < 0 || sx >= w || sy >= h)
        return {};
    size_t start = size_t(sy) * w + sx;
    std::vector<uint8_t> visited(size_t(w) * h);
    std::vector<uint32_t> queue;
    queue.reserve(65536);
    queue.push_back(uint32_t(start));
    visited[start] = 1;
    auto matches = [&](size_t p) {
        for (int c = 0; c < 4; c++)
            if (std::abs(int(image.bgra[p * 4 + c]) - int(image.bgra[start * 4 + c])) > tolerance)
                return false;
        return true;
    };
    for (size_t at = 0; at < queue.size(); at++) {
        if (queue.size() > limit)
            throw std::runtime_error("Region exceeds trace limit");
        size_t p = queue[at];
        int x = int(p % w), y = int(p / w);
        auto visit = [&](size_t q) {
            if (!visited[q] && matches(q)) {
                visited[q] = 1;
                queue.push_back(uint32_t(q));
            }
        };
        if (x > 0)
            visit(p - 1);
        if (x + 1 < w)
            visit(p + 1);
        if (y > 0)
            visit(p - w);
        if (y + 1 < h)
            visit(p + w);
    }
    // Each exposed pixel edge is oriented with filled area on its right; all loops, including holes, survive.
    std::unordered_multimap<uint64_t, uint64_t> edges;
    auto key = [&](int x, int y) { return uint64_t(y) * (w + 1) + x; };
    for (auto p : queue) {
        int x = p % w, y = p / w;
        if (y == 0 || !visited[p - w])
            edges.emplace(key(x, y), key(x + 1, y));
        if (x == w - 1 || !visited[p + 1])
            edges.emplace(key(x + 1, y), key(x + 1, y + 1));
        if (y == h - 1 || !visited[p + w])
            edges.emplace(key(x + 1, y + 1), key(x, y + 1));
        if (x == 0 || !visited[p - 1])
            edges.emplace(key(x, y + 1), key(x, y));
    }
    std::vector<std::vector<Point>> rings;
    while (!edges.empty()) {
        auto first = edges.begin()->first, at = first;
        std::vector<Point> ring;
        do {
            ring.push_back({double(at % (w + 1)), double(at / (w + 1))});
            auto it = edges.find(at);
            if (it == edges.end())
                throw std::runtime_error("Trace contour did not close");
            at = it->second;
            edges.erase(it);
        } while (at != first);
        if (ring.size() >= 4) {
            auto simplified = simplify(ring, 1.1, true);
            if (simplified.size() >= 3)
                rings.push_back(std::move(simplified));
        }
    }
    return rings;
}
void floodFill(Image &im, Point seed, const std::string &hex, int tolerance, const std::vector<Point> &mask) {
    int sx = int(seed.x), sy = int(seed.y), w = im.width, h = im.height;
    if (sx < 0 || sy < 0 || sx >= w || sy >= h)
        return;
    unsigned rgb = unsigned(std::stoul(hex.substr(1), nullptr, 16));
    std::array<uint8_t, 4> replacement = {uint8_t(rgb & 255), uint8_t((rgb >> 8) & 255),
                                          uint8_t((rgb >> 16) & 255), 255};
    size_t start = size_t(sy) * w + sx;
    std::array<uint8_t, 4> target{};
    for (int c = 0; c < 4; c++)
        target[c] = im.bgra[start * 4 + c];
    if (target == replacement)
        return;
    if (!mask.empty() && !inside({sx + .5, sy + .5}, mask))
        return;
    std::vector<uint8_t> seen(size_t(w) * h);
    std::vector<uint32_t> queue{uint32_t(start)};
    seen[start] = 1;
    auto visit = [&](size_t q) {
        if (seen[q])
            return;
        seen[q] = 1;
        for (int c = 0; c < 4; c++)
            if (std::abs(int(im.bgra[q * 4 + c]) - int(target[c])) > tolerance)
                return;
        if (!mask.empty() && !inside({double(q % w) + .5, double(q / w) + .5}, mask))
            return;
        queue.push_back(uint32_t(q));
    };
    for (size_t i = 0; i < queue.size(); i++) {
        size_t p = queue[i];
        for (int c = 0; c < 4; c++)
            im.bgra[p * 4 + c] = replacement[c];
        int x = int(p % w), y = int(p / w);
        if (x)
            visit(p - 1);
        if (x + 1 < w)
            visit(p + 1);
        if (y)
            visit(p - w);
        if (y + 1 < h)
            visit(p + w);
    }
}
} // namespace atlas
