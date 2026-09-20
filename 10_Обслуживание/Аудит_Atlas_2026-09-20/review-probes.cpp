#include "render.hpp"
#include <chrono>
#include <iostream>
#include <set>
using namespace atlas;
using Clock = std::chrono::steady_clock;
int wmain(int argc, wchar_t **argv) {
    if (argc != 4) return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        fs::path source(argv[1]), out(argv[2]), campaignRoot(argv[3]);
        fs::create_directories(out);
        Json report = Json::object();
        Map world; world.load(source);
        report["compact_document_bytes"] = world.doc.dump(0).size();
        report["pretty_document_bytes"] = world.doc.dump().size();
        {
            Map m; m.doc = world.doc;
            History history;
            for (int i = 0; i < 40; ++i) {
                auto before = m.doc;
                m.doc["name"] = "Audit " + std::to_string(i);
                history.push(before, m.doc, "Rename");
            }
            int count = 0; while (history.undo(m.doc)) ++count;
            report["undo_steps_after_40_edits"] = count;
        }
        {
            Map m; m.save(out / L"ancestry");
            auto a = m.snapshot("A", nullptr); m.save();
            auto before = m.doc;
            auto b = m.snapshot("B", nullptr);
            History h; h.push(before, m.doc, "Snapshot B"); m.save();
            h.undo(m.doc);
            auto c = m.snapshot("C after undo", nullptr); m.save();
            report["snapshot_undo"] = fields({{"a", a}, {"b", b}, {"c_parent", m.version(c)["parent"]},
                {"b_still_exists", fs::exists(m.directory / L"versions" / pathOf(b + ".json"))}});
        }
        {
            Map base; base.save(out / L"autosave");
            Map a,b; a.load(base.directory); b.load(base.directory);
            a.doc["name"] = "Unsaved A"; a.autosave();
            b.doc["name"] = "Unsaved B"; b.autosave();
            a.recover();
            report["autosave_recovered_by_a"] = a.doc["name"];
        }
        {
            Map m; auto old = m.doc;
            m.doc["background"] = "#123456";
            report["background_only_diff_count"] = m.diff(old)["changes"].size();
            m.doc["parent_version"] = "MAPVER-DOES-NOT-EXIST";
            report["missing_parent_validation_errors"] = m.validate()["errors"].size();
            Campaign c; c.load(campaignRoot);
            for (const auto &e : c.entities) if (e.type == "scene") {
                m.doc["story_anchor"] = fields({{"scene_id", e.id}, {"chapter", std::atoi(e.chapter.c_str())},
                    {"relation", "nonsense"}, {"branch", "unrelated"}, {"evidence_ids", Json::array()}});
                break;
            }
            report["bad_anchor_validation_errors"] = m.validate(&c)["errors"].size();
        }
        {
            Map m; m.save(out / L"broken-history");
            atomicText(m.directory / L"versions" / L"broken.json", "{invalid");
            report["broken_history_validation_errors"] = m.validate()["errors"].size();
            try { m.versions(); report["broken_history_list_failed"] = false; }
            catch(const std::exception &e) { report["broken_history_list_failed"] = true; report["broken_history_error"] = e.what(); }
        }
        {
            Map m; m.create(400,300,"Locked overlap");
            auto landLayer = m.doc["layers"][size_t(0)]["id"].str();
            auto lockedLayer = m.doc["layers"][size_t(1)]["id"].str();
            auto freeLayer = m.doc["layers"][size_t(2)]["id"].str();
            auto land = m.addPath({{0,0},{400,0},{400,300},{0,300}},true,"region",landLayer,"#AAAAAA",1,0);
            m.doc["features"][land]["role"]="land";
            auto old = m.addPath({{20,20},{180,20},{180,180},{20,180}},true,"region",lockedLayer,"#AAAAAA",1,0);
            m.doc["features"][old]["role"]="country";
            (*m.layer(lockedLayer))["locked"] = true;
            auto claimed = m.addPath({{10,10},{200,10},{200,200},{10,200}},true,"region",freeLayer,"#BBBBBB",1,0);
            m.doc["features"][claimed]["role"]="country";
            m.doc["land_state_separated"] = true;
            MapRenderer renderer; renderer.claimCountry(m, claimed);
            report["locked_country_removed_by_claim"] = !m.doc["features"].contains(old);
            report["claim_validation_errors"] = m.validate()["errors"].size();
        }
        {
            Map m; m.doc = world.doc;
            m.rebuildPoliticalTopology();
            auto old = m.doc;
            m.rebuildPoliticalTopology();
            auto diff = m.diff(old);
            std::map<std::string,int> counts;
            for (const auto &d : diff["changes"].arr()) ++counts[d["collection"].str()];
            report["second_noop_topology_rebuild_changes"] = Json::object();
            for (auto &[key,count] : counts) report["second_noop_topology_rebuild_changes"][key] = count;
        }
        {
            Map m; m.doc=world.doc; m.doc["parent_version"]=nullptr;
            m.save(out / L"history-scale");
            Json measures=Json::array();
            for (int i=1; i<=25; ++i) {
                m.snapshot("Scale " + std::to_string(i), nullptr);
                if (i==2 || i==10 || i==25) {
                    auto start=Clock::now(); auto list=m.versions();
                    double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
                    uint64_t bytes=0;
                    for (const auto &entry:fs::directory_iterator(m.directory / L"versions")) bytes+=entry.file_size();
                    measures.push(fields({{"versions",i},{"list_ms",ms},{"bytes",double(bytes)}}));
                }
            }
            measures.push(fields({{"note","Single warm local sample; measures versions() only, not GUI FPS."}}));
            report["history_scale"] = measures;
        }
        atomicText(out / L"results.json",report.dump()+"\n");
        std::cout << report.dump() << "\n";
    } catch(const std::exception &e) { std::cerr << e.what() << "\n"; return 1; }
    CoUninitialize();
}
