#include "app.hpp"
#include <algorithm>
namespace atlas {
std::string App::hitObject(Point p, double tolerance, bool parent) const {
    return map.hit(p, tolerance, parent, zoom, isolateLayer ? SelectionDomain::ActiveLayer : editScope,
                   isolateLayer ? activeLayer : "");
}
void App::setScope(SelectionDomain s) {
    editScope = s;
    isolateLayer = false;
    auto matches = [&](const Json &l) {
        auto d = l["domain"].str();
        return l["visible"].boolean(true) && !l["locked"].boolean() &&
               (s == SelectionDomain::Borders ? d == "political"
                : s == SelectionDomain::Land  ? d == "physical"
                                              : d != "physical" && d != "political");
    };
    const Json *current = map.layer(activeLayer);
    if (!current || !matches(*current))
        for (const auto &l : map.doc["layers"].arr())
            if (matches(l)) {
                activeLayer = l["id"].str();
                break;
            }
    selected.clear();
    activeBorder.clear();
    activeControl.clear();
    activeControls.clear();
    drawing.clear();
    renderer.excludedBorderArcs.clear();
    tool = Tool::Select;
    updateSelection();
}
void App::refreshControls() {
    if (activeBorder.empty())
        return;
    if (!map.doc["arcs"].contains(activeBorder)) {
        activeBorder.clear();
        activeControl.clear();
        activeControls.clear();
        return;
    }
    for (const auto &id : map.borderOwners(activeBorder)) {
        const auto *l = map.layer(map.doc["features"][id]["layer_id"].str());
        if (!l || (*l)["locked"].boolean()) {
            activeBorder.clear();
            activeControl.clear();
            activeControls.clear();
            return;
        }
    }
    activeControls = map.borderControls(activeBorder);
    if (std::find(activeControls.begin(), activeControls.end(), activeControl) == activeControls.end())
        activeControl.clear();
}
void App::selectBorder(const std::string &a) {
    activeBorder = a;
    activeControl.clear();
    activeControls.clear();
    selected.clear();
    for (const auto &id : map.borderOwners(a))
        selected.insert(id);
    if (!selected.empty()) {
        const Json &f = map.doc["features"][*selected.begin()];
        activeLayer = f["layer_id"].str();
        allowSea = f["territory_scope"].str() == "land_sea";
    }
    refreshControls();
    updateSelection();
}
bool App::beginBoundaryEdit(Point w) {
    bool political = editScope == SelectionDomain::Borders;
    if (isolateLayer) {
        const auto *l = map.layer(activeLayer);
        political = l && (*l)["domain"].str() == "political";
    }
    if (!political || (tool != Tool::Select && tool != Tool::Border && tool != Tool::Node))
        return false;
    double nearest = 10 / zoom;
    std::string node;
    for (const auto &n : activeControls) {
        double d = distance(w, point(map.doc["nodes"][n]));
        if (d < nearest) {
            nearest = d;
            node = n;
        }
    }
    if (!node.empty()) {
        activeControl = node;
        controlTarget = point(map.doc["nodes"][node]);
        draggingControl = true;
        controlPreviewIds = map.controlPathNodes(activeBorder, node);
        renderer.excludedBorderArcs = {activeBorder};
        for (const auto &[id, a] : map.doc["arcs"].obj())
            if (id != activeBorder)
                for (const auto &n : a["nodes"].arr())
                    if (n.str() == node) {
                        if (!map.borderOwners(id).empty())
                            renderer.excludedBorderArcs.insert(id);
                        break;
                    }
        invalidate();
        return true;
    }
    auto arc = map.politicalBorder(w, 12 / zoom, isolateLayer ? activeLayer : "");
    if (!arc.empty()) {
        selectBorder(arc);
        down = false;
        if(window)ReleaseCapture();
        invalidate();
        return true;
    }
    auto country = hitObject(w, 5 / zoom);
    if (!country.empty()) {
        selected = {country};
        activeBorder.clear();
        activeControls.clear();
        activeControl.clear();
        // A body click selects the country and reveals the closest boundary's handles. It never drags a
        // symbol.
        double best = 1e100;
        std::string nearestArc;
        const Json &f = map.doc["features"][country];
        activeLayer = f["layer_id"].str();
        allowSea = f["territory_scope"].str() == "land_sea";
        auto scan = [&](const Json &refs) {
            if (!refs.isArray())
                return;
            for (const auto &r : refs.arr()) {
                const auto &ns = map.doc["arcs"][r["id"].str()]["nodes"];
                for (size_t i = 1; i < ns.size(); i++) {
                    double d = segmentDistance(w, point(map.doc["nodes"][ns[i - 1].str()]),
                                               point(map.doc["nodes"][ns[i].str()]));
                    if (d < best) {
                        best = d;
                        nearestArc = r["id"].str();
                    }
                }
            }
        };
        scan(f["arcs"]);
        if (f["rings"].isArray())
            for (const auto &r : f["rings"].arr())
                scan(r);
        activeBorder = nearestArc;
        refreshControls();
        updateSelection();
        down = false;
        if(window)ReleaseCapture();
        return true;
    }
    selected.clear();
    activeBorder.clear();
    activeControl.clear();
    activeControls.clear();
    selectionBox = true;
    invalidate();
    return true;
}
} // namespace atlas
