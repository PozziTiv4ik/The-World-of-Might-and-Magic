#include "app.hpp"
namespace atlas {
class LandSink final : public ID2D1SimplifiedGeometrySink {
    ULONG refs = 1;

  public:
    std::vector<std::vector<Point>> paths;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID i, void **o) noexcept override {
        if (i == __uuidof(IUnknown) || i == __uuidof(ID2D1SimplifiedGeometrySink)) {
            *o = this;
            AddRef();
            return S_OK;
        }
        *o = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) noexcept override {}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) noexcept override {}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN) noexcept override {
        paths.push_back({{p.x, p.y}});
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F *p, UINT n) noexcept override {
        for (UINT i = 0; i < n; i++)
            paths.back().push_back({p[i].x, p[i].y});
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT *, UINT) noexcept override {}
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) noexcept override {}
    HRESULT STDMETHODCALLTYPE Close() noexcept override { return S_OK; }
};
bool MapRenderer::separateLand(Map &m) {
    if (m.doc["land_state_separated"].boolean())
        return false;
    Json previous = m.doc;
    try {
        Com<ID2D1PathGeometry> merged;
        std::vector<std::string> countries;
        for (const auto &[id, f] : m.doc["features"].obj())
            if (f["role"].str() == "country") {
                countries.push_back(id);
                auto g = geometry(m.paths(f), true);
                if (!merged) {
                    merged = std::move(g);
                    continue;
                }
                Com<ID2D1PathGeometry> r;
                check(factory->CreatePathGeometry(r.put()), "Land union");
                Com<ID2D1GeometrySink> s;
                check(r->Open(s.put()), "Land sink");
                check(merged->CombineWithGeometry(g.get(), D2D1_COMBINE_MODE_UNION, nullptr, .15f, s.get()),
                      "Land union");
                check(s->Close(), "Land union close");
                merged = std::move(r);
            }
        if (!merged)
            return false;
        if (m.doc["features"].contains("MAPOBJ-WATER-NETWORK"))
            merged = geometry({{{0, 0},
                                {m.doc["width"].num(), 0},
                                {m.doc["width"].num(), m.doc["height"].num()},
                                {0, m.doc["height"].num()}}},
                              true);
        // The imported water network is an ocean/lake mask. Subtract once; thereafter the coast
        // has its own coordinates and is independent of all political arcs.
        std::vector<std::string> water;
        for (const auto &[id, f] : m.doc["features"].obj())
            if (f["role"].str() == "water") {
                water.push_back(id);
                auto g = geometry(m.paths(f), true);
                Com<ID2D1PathGeometry> r;
                check(factory->CreatePathGeometry(r.put()), "Land water cut");
                Com<ID2D1GeometrySink> s;
                check(r->Open(s.put()), "Water sink");
                check(merged->CombineWithGeometry(g.get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, .15f, s.get()),
                      "Separate coast");
                check(s->Close(), "Coast close");
                merged = std::move(r);
            }
        auto sink = new LandSink;
        auto hr = merged->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES, nullptr, .15f, sink);
        auto paths = sink->paths;
        sink->Release();
        check(hr, "Land outline");
        auto lid = m.domainLayer(true);
        Json rings = Json::array();
        std::string fid;
        // Each contour is one private arc; no quadratic global snap search and no shared state nodes.
        for (auto &ring : paths) {
            if (ring.size() < 3)
                continue;
            if (distance(ring.front(), ring.back()) < 1e-6)
                ring.pop_back();
            if (ring.size() < 3)
                continue;
            Json ns = Json::array();
            std::string first;
            for (auto p : ring) {
                auto id = newId("NODE-LAND-");
                if (first.empty())
                    first = id;
                m.doc["nodes"][id] = pointJson(p);
                ns.push(id);
            }
            ns.push(first);
            auto a = newId("ARC-LAND-");
            m.doc["arcs"][a] = fields({{"nodes", ns}});
            rings.push(Json::Array{fields({{"id", a}, {"reverse", false}})});
        }
        if (rings.size() == 0)
            throw std::runtime_error("Land outline is empty");
        fid = newId("MAPOBJ-LAND-");
        m.doc["features"][fid] = fields(
            {{"id", fid},
             {"kind", "region"},
             {"role", "land"},
             {"domain", "physical"},
             {"name", "Суша мира"},
             {"layer_id", lid},
             {"closed", true},
             {"rings", rings},
             {"fill", "#E6DDC4"},
             {"stroke", "#9BAF9E"},
             {"stroke_width", 1.2},
             {"opacity", 1},
             {"show_label", false},
             {"visibility", "gm"},
             {"known_to", Json::array()},
             {"evidence_ids", Json::array()},
             {"truth", "unknown"},
             {"z_order", 0},
             {"provenance",
              fields({{"kind", "physical_outline_from_existing_map"}, {"source_features", Json::array()}})}});
        for (auto &id : countries) {
            m.doc["features"][fid]["provenance"]["source_features"].push(id);
            m.doc["features"][id]["domain"] = "political";
            auto l = m.layer(m.doc["features"][id]["layer_id"].str());
            if (l) {
                (*l)["domain"] = "political";
                (*l)["name"] = "Государства · границы";
            }
        }
        for (auto &id : water) {
            auto &f = m.doc["features"][id];
            f["role"] = "source_water_mask";
            auto l = m.layer(f["layer_id"].str());
            if (l) {
                (*l)["visible"] = false;
                (*l)["name"] = "Архив · исходная маска воды";
                (*l)["domain"] = "reference";
            }
        }
        m.doc["land_state_separated"] = true;
        auto errors = m.validate()["errors"];
        if (errors.size())
            throw std::runtime_error(errors.dump());
        m.clearCache();
        clear();
        return true;
    } catch (...) {
        m.doc = std::move(previous);
        throw;
    }
}
void MapRenderer::drawBorderPreview(Map &m, ID2D1RenderTarget *t, D2D1_RECT_F v, double z, Point offset,
                                    const std::string &a, Point grab, Point delta, double radius) {
    auto shape = m.borderShape(a, grab, delta, radius);
    std::vector<Point> pts;
    for (auto &[id, p] : shape)
        pts.push_back(p);
    auto g = geometry({pts}, false);
    D2D1_MATRIX_3X2_F old;
    t->GetTransform(&old);
    t->PushAxisAlignedClip(v, D2D1_ANTIALIAS_MODE_ALIASED);
    t->SetTransform(D2D1::Matrix3x2F::Scale(float(z), float(z)) *
                    D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y)));
    Painter p(t, textFactory.get(), &textCache);
    t->DrawGeometry(g.get(), p.ink("#168B98"), float(3 / z));
    t->SetTransform(old);
    t->PopAxisAlignedClip();
}
void MapRenderer::claimCountry(Map &m, const std::string &id) {
    auto before = m.doc;
    try {
        if(!m.doc["features"].contains(id))throw std::runtime_error("Country not found");
        const auto *claimLayer=m.layer(m.doc["features"][id]["layer_id"].str());
        if(!claimLayer || (*claimLayer)["locked"].boolean())throw std::runtime_error("Claimed country is locked");
        auto combine = [&](ID2D1Geometry *a, ID2D1Geometry *b, D2D1_COMBINE_MODE mode) {
            Com<ID2D1PathGeometry> r;
            check(factory->CreatePathGeometry(r.put()), "Country region");
            Com<ID2D1GeometrySink> s;
            check(r->Open(s.put()), "Country sink");
            check(a->CombineWithGeometry(b, mode, nullptr, .1f, s.get()), "Country partition");
            check(s->Close(), "Country close");
            return r;
        };
        auto flatten = [&](ID2D1Geometry *g) {
            auto s = new LandSink;
            auto hr = g->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES, nullptr, .1f, s);
            auto paths = s->paths;
            s->Release();
            check(hr, "Country contours");
            return paths;
        };
        Com<ID2D1PathGeometry> land;
        for (const auto &[fid, f] : m.doc["features"].obj())
            if (f["role"].str() == "land") {
                auto g = geometry(m.paths(f), true);
                if (!land)
                    land = std::move(g);
                else
                    land = combine(land.get(), g.get(), D2D1_COMBINE_MODE_UNION);
            }
        const Json &country=m.doc["features"][id];
        bool marine = country["territory_scope"].str() == "land_sea";
        if (!land && !marine)
            throw std::runtime_error("Для территории на суше сначала нарисуй землю · J");
        auto claim = geometry(m.paths(m.doc["features"][id]), true);
        std::vector<std::string> mergedCountries;
        if(country["claim_sources"].isArray())for(const auto &source:country["claim_sources"].arr()) {
            if(!m.doc["features"].contains(source.str()) || m.doc["features"][source.str()]["role"].str()!="country")
                throw std::runtime_error("Merge source is not a country");
            auto region=geometry(m.paths(m.doc["features"][source.str()]),true);
            claim=combine(claim.get(),region.get(),D2D1_COMBINE_MODE_UNION);
            mergedCountries.push_back(source.str());
        }
        auto within=country["claim_within"].str();
        if(!within.empty()) {
            if(!m.doc["features"].contains(within) || m.doc["features"][within]["role"].str()!="country")
                throw std::runtime_error("Partition source is not a country");
            auto region=geometry(m.paths(m.doc["features"][within]),true);
            claim=combine(claim.get(),region.get(),D2D1_COMBINE_MODE_INTERSECT);
        }
        if (!marine)
            claim = combine(claim.get(), land.get(), D2D1_COMBINE_MODE_INTERSECT);
        auto claimPaths = flatten(claim.get());
        if (claimPaths.empty())
            throw std::runtime_error("Государство должно занимать часть суши");
        auto replace = [&](const std::string &fid, const std::vector<std::vector<Point>> &paths) {
            Json rings = Json::array();
            for (auto ps : paths) {
                if (ps.size() < 3)
                    continue;
                if (distance(ps.front(), ps.back()) < 1e-6)
                    ps.pop_back();
                if (ps.size() < 3)
                    continue;
                Json ns = Json::array();
                std::string first;
                for (auto p : ps) {
                    auto n = newId("NODE-POL-");
                    if (first.empty())
                        first = n;
                    m.doc["nodes"][n] = pointJson(p);
                    ns.push(n);
                }
                ns.push(first);
                auto aid = newId("ARC-POL-");
                m.doc["arcs"][aid] = fields({{"nodes", ns}});
                rings.push(Json::Array{fields({{"id", aid}, {"reverse", false}})});
            }
            auto &f = m.doc["features"][fid];
            f.obj().erase("arcs");
            f["rings"] = rings;
        };
        std::vector<std::string> countries;
        for (const auto &[fid, f] : m.doc["features"].obj())
            if (fid != id && f["role"].str() == "country")
                countries.push_back(fid);
        for (auto &fid : countries) {
            if(std::find(mergedCountries.begin(),mergedCountries.end(),fid)!=mergedCountries.end()) {
                auto sourceLayer=m.layer(m.doc["features"][fid]["layer_id"].str());
                if(!sourceLayer || (*sourceLayer)["locked"].boolean())throw std::runtime_error("Merge source country is locked");
                // The source was included in the union. Do not retain microscopic
                // subtraction residue as a separate state after its annexation.
                m.eraseFeature(fid);continue;
            }
            auto g = geometry(m.paths(m.doc["features"][fid]), true);
            D2D1_GEOMETRY_RELATION relation;
            check(g->CompareWithGeometry(claim.get(), nullptr, .1f, &relation), "Country overlap");
            if (relation == D2D1_GEOMETRY_RELATION_DISJOINT)
                continue;
            auto cut = combine(g.get(), claim.get(), D2D1_COMBINE_MODE_EXCLUDE);
            float oldArea=0,newArea=0;
            check(g->ComputeArea(nullptr,.1f,&oldArea),"Country area");
            check(cut->ComputeArea(nullptr,.1f,&newArea),"Remaining country area");
            if(std::abs(oldArea-newArea)<.001f)continue;
            auto ownerLayer=m.layer(m.doc["features"][fid]["layer_id"].str());
            if (!ownerLayer || (*ownerLayer)["locked"].boolean())
                throw std::runtime_error("Новая территория затрагивает заблокированное государство: " +
                                         m.doc["features"][fid]["name"].str(fid));
            auto ps = flatten(cut.get());
            if (ps.empty())
                m.eraseFeature(fid);
            else
                replace(fid, ps);
        }
        replace(id, claimPaths);
        m.doc["features"][id].obj().erase("claim_sources");
        m.doc["features"][id].obj().erase("claim_within");
        m.rebuildPoliticalTopology();
        auto errors = m.validate()["errors"];
        if (errors.size()) {
            throw std::runtime_error(errors.dump());
        }
        clear();
    } catch (...) {
        m.doc = std::move(before);
        m.clearCache();
        clear();
        throw;
    }
}
} // namespace atlas
