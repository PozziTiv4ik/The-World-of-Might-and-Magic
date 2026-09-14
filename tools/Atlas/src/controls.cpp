#include "model.hpp"
#include <algorithm>
#include <functional>
#include <set>
namespace atlas {
static std::vector<std::string> refs(const Json &f) {
    std::vector<std::string> out;
    auto add = [&](const Json &r) {
        if (r.isArray())
            for (const auto &a : r.arr())
                out.push_back(a["id"].str());
    };
    add(f["arcs"]);
    if (f["rings"].isArray())
        for (const auto &r : f["rings"].arr())
            add(r);
    return out;
}
bool Map::selectable(const Json &f, SelectionDomain scope, const std::string &active) const {
    const auto *l = layer(f["layer_id"].str());
    if (!l || !(*l)["visible"].boolean(true) || (*l)["locked"].boolean())
        return false;
    auto role = f["role"].str();
    switch (scope) {
    case SelectionDomain::Borders:
        return role == "country";
    case SelectionDomain::Land:
        return role == "land";
    case SelectionDomain::Objects:
        return role != "country" && role != "land" && role != "water" && role != "source_water_mask";
    case SelectionDomain::ActiveLayer:
        return f["layer_id"].str() == active;
    default:
        return true;
    }
}
std::vector<std::string> Map::borderOwners(const std::string &arc) const {
    std::vector<std::string> out;
    for (const auto &[id, f] : doc["features"].obj())
        if (f["role"].str() == "country") {
            auto as = refs(f);
            if (std::find(as.begin(), as.end(), arc) != as.end())
                out.push_back(id);
        }
    return out;
}
std::string Map::politicalBorder(Point at, double tolerance, const std::string &active) const {
    std::set<std::string> candidates, blocked;
    for (const auto &[id, f] : doc["features"].obj())
        if (f["role"].str() == "country") {
            auto *l = layer(f["layer_id"].str());
            if (!l || (*l)["locked"].boolean()) {
                auto as = refs(f);
                blocked.insert(as.begin(), as.end());
            } else if ((*l)["visible"].boolean(true) && (active.empty() || f["layer_id"].str() == active)) {
                auto as = refs(f);
                candidates.insert(as.begin(), as.end());
            }
        }
    std::string result;
    for (const auto &a : candidates) {
        if (blocked.contains(a))
            continue;
        const auto &ns = doc["arcs"][a]["nodes"];
        for (size_t i = 1; i < ns.size(); i++) {
            double d =
                segmentDistance(at, point(doc["nodes"][ns[i - 1].str()]), point(doc["nodes"][ns[i].str()]));
            if (d < tolerance) {
                tolerance = d;
                result = a;
            }
        }
    }
    return result;
}
std::vector<std::string> Map::borderControls(const std::string &arc) const {
    const auto &a = doc["arcs"][arc];
    const auto &ns = a["nodes"];
    std::vector<std::string> out;
    if (!ns.isArray() || ns.size() < 2)
        return out;
    if (a["control_nodes"].isArray()) {
        for (const auto &n : a["control_nodes"].arr())
            out.push_back(n.str());
        return out;
    }
    std::vector<Point> pts;
    for (const auto &n : ns.arr())
        pts.push_back(point(doc["nodes"][n.str()]));
    double tolerance = std::max(1., std::max(doc["width"].num(), doc["height"].num()) / 320.);
    std::set<size_t> keep{0, pts.size() - 1};
    std::function<void(size_t, size_t)> rdp = [&](size_t l, size_t r) {
        double best = tolerance;
        size_t at = l;
        for (size_t i = l + 1; i < r; i++) {
            double d = segmentDistance(pts[i], pts[l], pts[r]);
            if (d > best) {
                best = d;
                at = i;
            }
        }
        if (at != l) {
            keep.insert(at);
            rdp(l, at);
            rdp(at, r);
        }
    };
    rdp(0, pts.size() - 1);
    // Shared junctions must remain explicit controls even when they lie on a straight line.
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < ns.size(); i++)
        index[ns[i].str()] = i;
    for (const auto &[id, other] : doc["arcs"].obj())
        if (id != arc)
            for (const auto &n : other["nodes"].arr()) {
                auto it = index.find(n.str());
                if (it != index.end())
                    keep.insert(it->second);
            }
    if (keep.size() < 3 && ns.size() > 2)
        keep.insert(ns.size() / 2);
    if (ns[0].str() == ns[ns.size() - 1].str() && keep.size() < 4 && ns.size() >= 4) {
        keep.insert((ns.size() - 1) / 3);
        keep.insert(2 * (ns.size() - 1) / 3);
    }
    for (auto i : keep)
        out.push_back(ns[i].str());
    return out;
}
static void writable(const Map &m, const std::string &arc) {
    auto owners = m.borderOwners(arc);
    if (owners.empty())
        throw std::runtime_error("Выбери политическую границу");
    for (const auto &id : owners) {
        const auto *l = m.layer(m.doc["features"][id]["layer_id"].str());
        if (!l || (*l)["locked"].boolean())
            throw std::runtime_error("Слой государства заблокирован");
    }
}
static std::vector<std::string> editedIds(const Map &m, const std::string &a, const std::string &node,
                                          bool remove) {
    auto controls = m.borderControls(a);
    auto it = std::find(controls.begin(), controls.end(), node);
    if (it == controls.end())
        throw std::runtime_error("Выбери контрольную точку");
    size_t c = size_t(it - controls.begin());
    const auto &ns = m.doc["arcs"][a]["nodes"];
    auto pos = [&](const std::string &id) {
        for (size_t i = 0; i < ns.size(); i++)
            if (ns[i].str() == id)
                return i;
        throw std::runtime_error("Missing control node");
    };
    bool closed = ns[0].str() == ns[ns.size() - 1].str();
    if (closed && c == 0) {
        size_t hi = pos(controls[1]), lo = pos(controls[controls.size() - 2]);
        std::vector<std::string> out;
        for (size_t i = 0; i < ns.size(); i++)
            if (i == 0 || i + 1 == ns.size() || (i >= hi && i <= lo))
                out.push_back(ns[i].str());
        return out;
    }
    size_t at = pos(node), lo = c ? pos(controls[c - 1]) : at,
           hi = c + 1 < controls.size()
                    ? (closed && c + 2 == controls.size() ? ns.size() - 1 : pos(controls[c + 1]))
                    : at;
    std::vector<std::string> out;
    for (size_t i = 0; i < ns.size(); i++)
        if (i <= lo || i >= hi || (!remove && i == at))
            out.push_back(ns[i].str());
    return out;
}
std::vector<Point> Map::controlledBorder(const std::string &a, const std::string &node, Point at) const {
    std::vector<Point> out;
    if (node.empty()) {
        for (const auto &n : doc["arcs"][a]["nodes"].arr())
            out.push_back(point(doc["nodes"][n.str()]));
        return out;
    }
    for (const auto &id : editedIds(*this, a, node, false))
        out.push_back(id == node ? at : point(doc["nodes"][id]));
    return out;
}
std::vector<std::string> Map::controlPathNodes(const std::string &a, const std::string &n) const {
    return editedIds(*this, a, n, false);
}
void Map::moveBorderControl(const std::string &a, const std::string &node, Point at, bool sea) {
    writable(*this, a);
    if (!std::isfinite(at.x) || !std::isfinite(at.y))
        throw std::runtime_error("Invalid control position");
    auto cs = borderControls(a);
    auto ids = editedIds(*this, a, node, false);
    // moveNode checks every owner, including locked non-political references, before writing.
    moveNode(node, at);
    Json ns = Json::array(), controls = Json::array();
    for (auto &id : ids)
        ns.push(id);
    for (auto &id : cs)
        controls.push(id);
    doc["arcs"][a]["nodes"] = ns;
    doc["arcs"][a]["control_nodes"] = controls;
    if (sea)
        for (auto &id : borderOwners(a))
            doc["features"][id]["territory_scope"] = "land_sea";
    clearCache();
}
std::string Map::insertBorderControl(const std::string &a, Point at, bool sea) {
    writable(*this, a);
    if (!std::isfinite(at.x) || !std::isfinite(at.y))
        throw std::runtime_error("Invalid control position");
    auto cs = borderControls(a);
    const auto &nodes = doc["arcs"][a]["nodes"];
    double best = 1e100;
    size_t segment = 1;
    Point projected{};
    for (size_t i = 1; i < nodes.size(); i++) {
        auto p = point(doc["nodes"][nodes[i - 1].str()]), q = point(doc["nodes"][nodes[i].str()]);
        double len = distance(p, q);
        double t = len ? std::clamp(((at.x - p.x) * (q.x - p.x) + (at.y - p.y) * (q.y - p.y)) / (len * len),
                                    .001, .999)
                       : .5;
        Point v{p.x + t * (q.x - p.x), p.y + t * (q.y - p.y)};
        if (distance(v, at) < best) {
            best = distance(v, at);
            segment = i;
            projected = v;
        }
    }
    for (auto &id : cs)
        if (distance(point(doc["nodes"][id]), projected) < .01)
            return id;
    auto id = newId("NODE-CONTROL-");
    doc["nodes"][id] = pointJson(projected);
    auto &ns = doc["arcs"][a]["nodes"].arr();
    ns.insert(ns.begin() + segment, id);
    std::set<std::string> wanted(cs.begin(), cs.end());
    wanted.insert(id);
    Json controls = Json::array();
    for (auto &n : ns)
        if (wanted.contains(n.str()))
            controls.push(n);
    doc["arcs"][a]["control_nodes"] = controls;
    if (sea)
        for (auto &owner : borderOwners(a))
            doc["features"][owner]["territory_scope"] = "land_sea";
    clearCache();
    return id;
}
void Map::deleteBorderControl(const std::string &a, const std::string &node) {
    writable(*this, a);
    auto cs = borderControls(a);
    bool loop = cs.front() == cs.back();
    if (cs.size() <= (loop ? 4u : 2u) || (!loop && (cs.front() == node || cs.back() == node)))
        throw std::runtime_error("Недостаточно точек или выбран концевой стык");
    for (const auto &[id, arc] : doc["arcs"].obj())
        if (id != a)
            for (const auto &n : arc["nodes"].arr())
                if (n.str() == node)
                    throw std::runtime_error("Точка соединяет несколько границ");
    if (loop && node == cs.front()) {
        const auto &old = doc["arcs"][a]["nodes"].arr();
        auto first = std::find_if(old.begin(), old.end(), [&](const Json &n) { return n.str() == cs[1]; });
        auto last =
            std::find_if(old.begin(), old.end(), [&](const Json &n) { return n.str() == cs[cs.size() - 2]; });
        Json ns = Json::array(), controls = Json::array();
        for (auto it = first; it <= last; ++it)
            ns.push(*it);
        ns.push(cs[1]);
        for (size_t i = 1; i + 1 < cs.size(); i++)
            controls.push(cs[i]);
        controls.push(cs[1]);
        doc["arcs"][a]["nodes"] = ns;
        doc["arcs"][a]["control_nodes"] = controls;
        clearCache();
        return;
    }
    auto ids = editedIds(*this, a, node, true);
    bool closed = ids.front() == ids.back();
    if (closed && ids.size() < 4)
        throw std::runtime_error("Замкнутой границе нужны три точки");
    Json ns = Json::array(), controls = Json::array();
    for (auto &id : ids)
        ns.push(id);
    for (auto &id : cs)
        if (id != node)
            controls.push(id);
    doc["arcs"][a]["nodes"] = ns;
    doc["arcs"][a]["control_nodes"] = controls;
    clearCache();
}
} // namespace atlas
