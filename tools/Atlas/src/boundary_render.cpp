#include "render.hpp"
#include <algorithm>

#include <tuple>
namespace atlas {
void MapRenderer::drawBorders(Map &m, ID2D1RenderTarget *t, D2D1_RECT_F v, double z, Point offset) {
    if (borderMap != &m || borderExcluded != excludedBorderArcs) {
        borderGroups.clear();

        borderMap = &m;
        borderExcluded = excludedBorderArcs;
        using Key = std::tuple<std::string, float, float>;
        std::map<Key, std::vector<std::vector<Point>>> groups;
        std::set<std::string> seen;
        for (const auto &l : m.doc["layers"].arr())
            if (l["visible"].boolean(true)) {
                for (const auto *f : m.orderedFeatures(l["id"].str()))
                    if ((*f)["role"].str() == "country") {
                        auto add = [&](const Json &refs) {
                            if (!refs.isArray())
                                return;
                            for (const auto &r : refs.arr()) {
                                auto id = r["id"].str();
                                if (seen.contains(id) || excludedBorderArcs.contains(id))
                                    continue;
                                seen.insert(id);
                                std::vector<Point> points;
                                for (const auto &n :
                                     static_cast<const Json &>(m.doc)["arcs"][id]["nodes"].arr())
                                    points.push_back(
                                        point(static_cast<const Json &>(m.doc)["nodes"][n.str()]));
                                groups[{(*f)["stroke"].str("#657573"), float((*f)["stroke_width"].num(2)),
                                        float(l["opacity"].num(1) * (*f)["opacity"].num(1))}]
                                    .push_back(std::move(points));
                            }
                        };
                        add((*f)["arcs"]);
                        if ((*f)["rings"].isArray())
                            for (const auto &r : (*f)["rings"].arr())
                                add(r);
                    }
            }
        for (auto &[key, paths] : groups)
            borderGroups.push_back(
                {geometry(paths, false), std::get<0>(key), std::get<1>(key), std::get<2>(key)});
    }
    t->PushAxisAlignedClip(v, D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_MATRIX_3X2_F old;
    t->GetTransform(&old);
    t->SetTransform(D2D1::Matrix3x2F::Scale(float(z), float(z)) *
                    D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y)));
    Painter p(t, textFactory.get(), &textCache);
    for (auto &g : borderGroups)
        t->DrawGeometry(g.shape.get(), p.ink(g.stroke, g.opacity), float(std::max(double(g.width), 1.1 / z)),
                        roundStroke.get());
    t->SetTransform(old);
    t->PopAxisAlignedClip();
}
void MapRenderer::drawControlEditor(Map &m, ID2D1RenderTarget *t, D2D1_RECT_F v, double z, Point offset,
                                    const std::string &arc, const std::vector<std::string> &controls,
                                    const std::string &node, Point at, bool dragging,
                                    const std::vector<std::string> &previewIds) {
    const Json &doc = m.doc;
    if (!doc["arcs"].contains(arc))
        return;
    t->PushAxisAlignedClip(v, D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_MATRIX_3X2_F old;
    t->GetTransform(&old);
    t->SetTransform(D2D1::Matrix3x2F::Scale(float(z), float(z)) *
                    D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y)));
    Painter p(t, textFactory.get(), &textCache);
    auto draw = [&](const std::string &id, bool primary) {
        std::vector<Point> ps;
        if (primary && dragging)
            for (auto &n : previewIds)
                ps.push_back(n == node ? at : point(doc["nodes"][n]));
        else
            for (const auto &n : doc["arcs"][id]["nodes"].arr())
                ps.push_back(dragging && n.str() == node ? at : point(doc["nodes"][n.str()]));
        auto g = geometry({ps}, false);
        t->DrawGeometry(g.get(), p.ink(primary ? "#07889C" : "#54797C"), float((primary ? 2.3 : 1.5) / z),
                        roundStroke.get());
    };
    draw(arc, true);
    if (dragging)
        for (const auto &a : excludedBorderArcs)
            if (a != arc)
                draw(a, false);
    t->SetTransform(old);
    std::set<std::string> shown;
    for (auto &n : controls) {
        if (!shown.insert(n).second)
            continue;
        auto pt = dragging && n == node ? at : point(doc["nodes"][n]);
        Point s{pt.x * z + offset.x, pt.y * z + offset.y};
        if (s.x < v.left - 7 || s.x > v.right + 7 || s.y < v.top - 7 || s.y > v.bottom + 7)
            continue;
        p.circle(s, n == node ? 6 : 4.5f, "#FFFFFF");
        p.circle(s, n == node ? 6 : 4.5f, "#087D91", false);
        if (n == node)
            p.circle(s, 2.5f, "#087D91");
    }
    t->PopAxisAlignedClip();
}
} // namespace atlas
