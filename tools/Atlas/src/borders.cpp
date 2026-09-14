#include "model.hpp"
#include <algorithm>
#include <set>
namespace atlas {
static std::vector<std::string> arcIds(const Json &f) {
    std::vector<std::string> ids;
    auto add = [&](const Json &refs) {
        if (refs.isArray())
            for (const auto &r : refs.arr())
                ids.push_back(r["id"].str());
    };
    add(f["arcs"]);
    if (f["rings"].isArray())
        for (const auto &r : f["rings"].arr())
            add(r);
    return ids;
}
std::string Map::domainLayer(bool land) {
    auto domain = land ? "physical" : "political";
    for (const auto &l : doc["layers"].arr())
        if (l["domain"].str() == domain && l["kind"].str() == "vector" && !l["locked"].boolean())
            return l["id"].str();
    auto id = addLayer(land ? "Суша · берега и острова" : "Государства · границы");
    (*layer(id))["domain"] = domain;
    if (land) {
        auto l = doc["layers"].arr().back();
        doc["layers"].arr().pop_back();
        doc["layers"].arr().insert(doc["layers"].arr().begin(), l);
    }
    return id;
}
std::string Map::sharedBorder(const std::string &country, Point grab) const {
    std::map<std::string, std::set<std::string>> owners;
    for (const auto &[id, f] : doc["features"].obj())
        if (f["role"].str() == "country")
            for (const auto &a : arcIds(f))
                owners[a].insert(id);
    double best = 1e100;
    std::string result;
    for (const auto &[aid, ids] : owners) {
        if (ids.size() != 2 || (!country.empty() && !ids.contains(country)))
            continue;
        bool locked = false;
        for (const auto &id : ids) {
            auto l = layer(doc["features"][id]["layer_id"].str());
            locked |= !l || (*l)["locked"].boolean() || !(*l)["visible"].boolean(true);
        }
        if (locked)
            continue;
        const auto &ns = doc["arcs"][aid]["nodes"];
        for (size_t i = 1; i < ns.size(); i++) {
            double d =
                segmentDistance(grab, point(doc["nodes"][ns[i - 1].str()]), point(doc["nodes"][ns[i].str()]));
            if (d < best) {
                best = d;
                result = aid;
            }
        }
    }
    return result;
}
std::vector<std::pair<std::string, Point>> Map::borderShape(const std::string &aid, Point grab, Point delta,
                                                            double radius) const {
    std::vector<std::pair<std::string, Point>> pts;
    const auto &ns = doc["arcs"][aid]["nodes"];
    if (!ns.isArray())
        return pts;
    radius = std::clamp(radius, 4., 10000.);
    // Subdivide long straight boundaries too, allowing an ordinary two-point shared edge to bend.
    for (size_t i = 0; i < ns.size(); i++) {
        auto p = point(doc["nodes"][ns[i].str()]);
        if (i) {
            auto a = pts.back().second;
            int n = std::max(3, int(std::ceil(distance(a, p) / (radius / 5))));
            if (distance(a, p) < radius / 5)
                n = 1;
            if (ns.size() == 2)
                n = 5;
            n = std::min(256, n);
            for (int j = 1; j < n; j++) {
                double t = double(j) / n;
                pts.push_back({"", {a.x + (p.x - a.x) * t, a.y + (p.y - a.y) * t}});
            }
        }
        pts.push_back({ns[i].str(), p});
    }
    if (pts.size() < 3)
        return pts;
    std::vector<double> length(pts.size(), 0);
    double best = 1e100, anchor = 0;
    for (size_t i = 1; i < pts.size(); i++) {
        auto a = pts[i - 1].second, b = pts[i].second;
        double len = distance(a, b);
        length[i] = length[i - 1] + len;
        double t =
            len ? std::clamp(((grab.x - a.x) * (b.x - a.x) + (grab.y - a.y) * (b.y - a.y)) / (len * len), 0.,
                             1.)
                : 0;
        Point q{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
        double d = distance(q, grab);
        if (d < best) {
            best = d;
            anchor = length[i - 1] + t * len;
        }
    }
    // Junctions and coastal endpoints stay fixed. Both states refer to the same moved chain.
    for (size_t i = 1; i + 1 < pts.size(); i++) {
        double d = std::abs(length[i] - anchor),
               w = d >= radius ? 0 : (1 + std::cos(3.141592653589793 * d / radius)) * .5;
        double edge = std::min(length[i], length.back() - length[i]);
        w *= std::clamp(edge / std::min(radius * .2, length.back() * .2), 0., 1.);
        pts[i].second.x += delta.x * w;
        pts[i].second.y += delta.y * w;
    }
    return pts;
}
void Map::moveBorder(const std::string &aid, Point grab, Point delta, double radius) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(radius))
        throw std::runtime_error("Invalid border movement");
    std::set<std::string> owners;
    for (const auto &[id, f] : doc["features"].obj())
        for (const auto &a : arcIds(f))
            if (a == aid) {
                auto l = layer(f["layer_id"].str());
                if (!l || (*l)["locked"].boolean())
                    throw std::runtime_error("Граница связана с заблокированным слоем");
                if (f["role"].str() == "country")
                    owners.insert(id);
                if (f["role"].str() == "land")
                    throw std::runtime_error("Берег должен быть отделён от политической границы");
            }
    if (owners.size() != 2)
        throw std::runtime_error("Нужна общая граница двух государств");
    auto shape = borderShape(aid, grab, delta, radius);
    Json nodes = Json::array();
    for (auto &[id, p] : shape) {
        if (id.empty())
            id = newId("NODE-BORDER-");
        doc["nodes"][id] = pointJson(p);
        nodes.push(id);
    }
    doc["arcs"][aid]["nodes"] = nodes;
    clearCache();
}
} // namespace atlas
