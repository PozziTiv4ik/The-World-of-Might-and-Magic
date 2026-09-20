#include "model.hpp"
#include <algorithm>
#include <set>
#include <sstream>
namespace atlas {
void Map::rebuildPoliticalTopology() {
    struct Edge {
        std::string a, b;
        std::set<std::string> owners;
        std::string arc;
        bool forward = true;
    };
    using Key = std::pair<std::string, std::string>;
    std::map<Key, Edge> edges;
    std::map<std::string, std::vector<std::vector<Key>>> rings;
    std::map<std::pair<long long, long long>, std::string> points;
    auto coordinate = [](Point p) { return std::pair{std::llround(p.x * 1000), std::llround(p.y * 1000)}; };
    std::set<std::pair<long long, long long>> manualControls;
    auto signature = [](const Json &ns) {
        std::vector<std::string> ids;
        for(const auto &n:ns.arr())ids.push_back(n.str());
        bool closed=ids.size()>1 && ids.front()==ids.back();
        if(closed)ids.pop_back();
        auto normalize=[&](std::vector<std::string> a) {
            if(closed && !a.empty())std::rotate(a.begin(),std::min_element(a.begin(),a.end()),a.end());
            std::string key=closed?"closed|":"open|";for(const auto &n:a)key+=n+"|";return key;
        };
        auto f=normalize(ids);std::reverse(ids.begin(),ids.end());return std::min(f,normalize(ids));
    };
    std::map<std::string,std::string> existingArcs;
    for(const auto &[id,a]:doc["arcs"].obj())
        if(a["nodes"].isArray())existingArcs.try_emplace(signature(a["nodes"]),id);
    for (const auto &[id, a] : doc["arcs"].obj())
        if (a["control_nodes"].isArray())
            for (const auto &n : a["control_nodes"].arr())
                manualControls.insert(coordinate(point(doc["nodes"][n.str()])));
    // Only political nodes can be reused. Coast nodes never enter this index.
    for (const auto &[id, f] : doc["features"].obj())
        if (f["role"].str() == "country")
            for (const auto &n : featureNodes(f))
                points.try_emplace(coordinate(point(doc["nodes"][n])), n);
    auto node = [&](Point p) {
        auto key = coordinate(p);
        auto it = points.find(key);
        if (it != points.end())
            return it->second;
        auto id = newId("NODE-POL-");
        doc["nodes"][id] = pointJson(p);
        points[key] = id;
        return id;
    };
    std::map<std::pair<int, int>, std::vector<std::pair<std::string, Point>>> grid;
    for (const auto &[xy, id] : points) {
        auto p = point(doc["nodes"][id]);
        grid[{int(std::floor(p.x / 64)), int(std::floor(p.y / 64))}].push_back({id, p});
    }
    for (const auto &[id, f] : doc["features"].obj())
        if (f["role"].str() == "country") {
            auto ps = paths(f);
            for (auto &path : ps) {
                if (path.size() < 3)
                    continue;
                if (distance(path.front(), path.back()) < 1e-6)
                    path.pop_back();
                std::vector<Key> rs;
                for (size_t i = 0; i < path.size(); i++) {
                    auto pa = path[i], pb = path[(i + 1) % path.size()];
                    auto a = node(pa), b = node(pb);
                    if (a == b)
                        continue;
                    std::vector<std::pair<double, std::string>> split{{0, a}, {1, b}};
                    double dx = pb.x - pa.x, dy = pb.y - pa.y, len2 = dx * dx + dy * dy;
                    for (int x = int(std::floor(std::min(pa.x, pb.x) / 64));
                         x <= int(std::floor(std::max(pa.x, pb.x) / 64)); x++)
                        for (int y = int(std::floor(std::min(pa.y, pb.y) / 64));
                             y <= int(std::floor(std::max(pa.y, pb.y) / 64)); y++) {
                            auto it = grid.find({x, y});
                            if (it == grid.end())
                                continue;
                            for (auto &[nid, p] : it->second) {
                                if (nid == a || nid == b)
                                    continue;
                                double t = ((p.x - pa.x) * dx + (p.y - pa.y) * dy) / len2;
                                if (t > 1e-7 && t < 1 - 1e-7 && segmentDistance(p, pa, pb) < .0015)
                                    split.push_back({t, nid});
                            }
                        }
                    std::sort(split.begin(), split.end());
                    for (size_t k = 1; k < split.size(); k++) {
                        auto na = split[k - 1].second, nb = split[k].second;
                        if (na == nb)
                            continue;
                        Key key = std::minmax(na, nb);
                        if (!edges.contains(key))
                            edges[key] = Edge{key.first, key.second};
                        edges[key].owners.insert(id);
                        rs.push_back({na, nb});
                    }
                }
                if (rs.size() >= 3)
                    rings[id].push_back(std::move(rs));
            }
        }
    std::map<std::string, std::vector<Key>> adj;
    for (const auto &[k, e] : edges) {
        adj[e.a].push_back(k);
        adj[e.b].push_back(k);
    }
    std::set<std::string> turns;
    for(const auto &[id,contours]:rings)for(const auto &ring:contours)
        for(size_t i=0;i<ring.size();++i) {
            const auto &a=ring[(i+ring.size()-1)%ring.size()],&b=ring[i];
            if(a.first==b.second && a.second==b.first)turns.insert(b.first);
        }
    auto neighbours = [&](const std::string &node, const std::set<std::string> &owners) {
        std::vector<Key> out;
        for (auto &k : adj[node])
            if (edges[k].owners == owners)
                out.push_back(k);
        return out;
    };
    for (auto &[key, first] : edges) {
        if (!first.arc.empty())
            continue;
        auto owners = first.owners;
        // Find an endpoint of a maximal chain of edges separating the same owners.
        std::string start = first.a, current = start;
        Key previous = key;
        std::set<Key> seen{key};
        while (true) {
            auto ns = neighbours(current, owners);
            if (ns.size() != 2 || adj[current].size() != 2 || turns.contains(current)) {
                start = current;
                break;
            }
            auto next = ns[0] == previous ? ns[1] : ns[0];
            if (seen.contains(next)) {
                start = first.a;
                break;
            }
            seen.insert(next);
            const auto &e = edges[next];
            current = e.a == current ? e.b : e.a;
            previous = next;
        }
        auto aid = newId("ARC-POL-");
        Json ids = Json::array();
        ids.push(start);
        current = start;
        while (true) {
            auto ns = neighbours(current, owners);
            Key next;
            bool found = false;
            for (auto &k : ns)
                if (edges[k].arc.empty()) {
                    next = k;
                    found = true;
                    break;
                }
            if (!found)
                break;
            auto &e = edges[next];
            e.arc = aid;
            e.forward = e.a == current;
            current = e.a == current ? e.b : e.a;
            ids.push(current);
            if (current == start || neighbours(current, owners).size() != 2 || adj[current].size() != 2 || turns.contains(current))
                break;
        }
        auto reused=existingArcs.find(signature(ids));
        if(reused!=existingArcs.end()) {
            auto oldId=aid;aid=reused->second;
            for(auto &[k,e]:edges)if(e.arc==oldId)e.arc=aid;
        }
        if(!doc["arcs"].contains(aid))doc["arcs"][aid]=fields({{"nodes",ids}});
        else {
            // Keep the existing directed representation and flip edge orientations when needed.
            const auto &old=doc["arcs"][aid]["nodes"];
            if(old.size()>2 && old[0]==old[old.size()-1] && old[0]!=ids[0]) {
                // A previously isolated closed loop can gain a junction. Its cyclic
                // geometry is unchanged, but references must now start at that junction.
                doc["arcs"][aid]["nodes"]=ids;
                doc["arcs"][aid].obj().erase("control_nodes");
            } else if(old[0].str()!=ids[0].str() || (old.size()>1 && old[1].str()!=ids[1].str())) {
                for(auto &[k,e]:edges)if(e.arc==aid) {
                    for(size_t i=1;i<old.size();++i)
                        if((old[i-1].str()==e.a && old[i].str()==e.b) ||
                           (old[i-1].str()==e.b && old[i].str()==e.a)) {
                            e.forward=old[i-1].str()==e.a;break;
                        }
                }
            }
        }
    }
    for (auto &[id, rs] : rings) {
        Json result = Json::array();
        for (auto &ring : rs) {
            struct Ref {
                std::string id;
                bool reverse;
            };
            std::vector<Ref> refs;
            for (auto &[a, b] : ring) {
                auto &e = edges.at(std::minmax(a, b));
                refs.push_back({e.arc, (a == e.a) != e.forward});
            }
            size_t start = 0;
            for (size_t i = 0; i < refs.size(); i++)
                if (refs[i].id != refs[(i + refs.size() - 1) % refs.size()].id ||
                    refs[i].reverse != refs[(i + refs.size() - 1) % refs.size()].reverse) {
                    start = i;
                    break;
                }
            Json out = Json::array();
            std::string prev;
            bool previousReverse=false;
            for (size_t i = 0; i < refs.size(); i++) {
                const auto &r = refs[(start + i) % refs.size()];
                if (r.id != prev || r.reverse != previousReverse) {
                    out.push(fields({{"id", r.id}, {"reverse", r.reverse}}));
                    prev = r.id;
                    previousReverse=r.reverse;
                }
            }
            result.push(out);
        }
        auto &f = doc["features"][id];
        f.obj().erase("arcs");
        f["rings"] = result;
    }
    // Remove only unreferenced geometry; snapshots are separate immutable documents.
    std::set<std::string> usedArcs, usedNodes;
    for (const auto &[id, f] : doc["features"].obj()) {
        auto add = [&](const Json &refs) {
            if (refs.isArray())
                for (const auto &r : refs.arr())
                    usedArcs.insert(r["id"].str());
        };
        add(f["arcs"]);
        if (f["rings"].isArray())
            for (const auto &r : f["rings"].arr())
                add(r);
    }
    for (auto it = doc["arcs"].obj().begin(); it != doc["arcs"].obj().end();) {
        if (!usedArcs.contains(it->first))
            it = doc["arcs"].obj().erase(it);
        else {
            for (auto &n : it->second["nodes"].arr())
                usedNodes.insert(n.str());
            ++it;
        }
    }
    for (auto it = doc["nodes"].obj().begin(); it != doc["nodes"].obj().end();)
        if (!usedNodes.contains(it->first))
            it = doc["nodes"].obj().erase(it);
        else
            ++it;
    if (!manualControls.empty())
        for (auto &[id, a] : doc["arcs"].obj()) {
            bool touches = false;
            for (const auto &n : a["nodes"].arr())
                touches |= manualControls.contains(coordinate(point(doc["nodes"][n.str()])));
            if (!touches || borderOwners(id).empty())
                continue;
            auto autoControls = borderControls(id);
            std::set<std::string> wanted(autoControls.begin(), autoControls.end());
            Json controls = Json::array();
            for (const auto &n : a["nodes"].arr())
                if (wanted.contains(n.str()) ||
                    manualControls.contains(coordinate(point(doc["nodes"][n.str()]))))
                    controls.push(n);
            a["control_nodes"] = controls;
        }
    clearCache();
}
} // namespace atlas
