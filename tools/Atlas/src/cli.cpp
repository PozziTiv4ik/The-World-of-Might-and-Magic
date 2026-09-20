#include "app.hpp"
#include <algorithm>
#include <iostream>
#include <set>

namespace atlas {
int runCli(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    auto arg = [&](int i) { return i < argc ? utf8(argv[i]) : std::string(); };
    auto option = [&](const std::string &key) {
        for (int i = 2; i + 1 < argc; i++)
            if (arg(i) == key)
                return arg(i + 1);
        return std::string();
    };
    auto flag = [&](const std::string &key) {
        for (int i = 2; i < argc; i++)
            if (arg(i) == key)
                return true;
        return false;
    };
    if(arg(1)=="--version"){std::cout<<fields({{"application","Atlas"},{"version","5.2.0"},{"history_format",2},{"editor_scenario_format",1}}).dump()<<"\n";return 0;}
    if (argc < 2 || arg(1) == "help" || arg(1) == "--help") {
        std::cout
            << "ATLAS native Windows map editor\n\nCommands:\n  import <source.pdn|png> --out "
               "<new-directory>\n  new <directory> --width 4000 --height 3000 --name <name>\n  inspect "
               "<map-directory> [--entity <id>] [--json]\n  validate <map-directory> [--project "
               "<campaign-directory>]\n  apply <map-directory> --patch <file> [--dry-run]\n  snapshot "
               "<map-directory> --name <label> [--scene <id> --project <campaign>]\n  diff <map-directory> "
               "--from <version-id> [--to <version-id|working>]\n  restore <map-directory> --version <id>\n  "
               "render <map-directory> --output <file.png|svg> [--version <id>]\n  render <map-directory> "
               "--output <file.png> --character <id>\n  pdn-info <file.pdn> [--verify-pixels]\n  self-test "
               "--out <temporary-directory> [--pdn <file>]\n  trace <map-directory> --x <x> --y <y> --name "
               "<name> [--layer <id>] [--dry-run]\n";
        std::cout<<"\nAtlas 5 history:\n"
            "  history <map> [--order story|date|saved|parents] [--chain <id>] [--chapter N] [--query <text>]\n"
            "  timeline <map> --patch <json> [--dry-run] [--project <campaign>]\n"
            "  snapshot <map> --name <label> [--chapter N --chain <id> --date <text> --order N]\n"
            "      [--scene <id> --project <campaign>] [--from <version> --checkout] [--accepted]\n"
            "  draft <map> [--name <label> | --restore <draft-id>]\n"
            "  recoveries <map> [--restore <session-id>]\n"
            "  validate <map> --history [--project <campaign>]\n";
        std::cout<<"  claim-country <map> --id <country> --expected-hash <hash> [--dry-run]\n";
        std::cout<<"\nWindowless editor replay (always edits an isolated copy):\n"
            "  editor-test --scenario <file.json> --out <reports> [--map <source>] [--project <campaign>]\n"
            "  workspace-test --out <reports>\n"
            "  ui-preview <map> --state <N> --output <image.png>\n";
        return 0;
    }
    if (arg(1) == "self-test")
        return selfTest(pathOf(option("--out")), pathOf(option("--pdn")), pathOf(option("--map")));
    if(arg(1)=="workspace-test") {
        auto dir=pathOf(option("--out"));if(dir.empty())throw std::runtime_error("workspace-test requires --out");
        int passed=workspaceSelfTest(dir);std::cout<<fields({{"passed",passed},{"failed",0}}).dump()<<"\n";return 0;
    }
    if(arg(1)=="editor-test") {
        auto scenario=pathOf(option("--scenario"));if(scenario.empty())throw std::runtime_error("editor-test requires --scenario");
        auto result=runEditorScenario(scenario,pathOf(option("--out")),pathOf(option("--map")),pathOf(option("--project")));
        auto summary=result;summary.obj().erase("steps");summary.obj().erase("interactions");std::cout<<summary.dump()<<"\n";
        return result["status"].str()=="passed"?0:1;
    }
    if (arg(1) == "benchmark") {
        std::cout << benchmarkMap(pathOf(arg(2))).dump() << "\n";
        return 0;
    }
    if (arg(1) == "benchmark-camera") {
        std::cout << benchmarkCamera(pathOf(arg(2)), !flag("--without-borders")).dump() << "\n";
        return 0;
    }
    if (arg(1) == "separate-land") {
        Map m;
        m.load(pathOf(arg(2)));
        MapRenderer r;
        bool changed = r.separateLand(m);
        if (changed)
            m.save();
        std::cout << fields({{"changed", changed}, {"validation", m.validate()}}).dump() << "\n";
        return 0;
    }
    if (arg(1) == "ui-preview") {
        App preview({}, {});
        savePng(
            pathOf(option("--output")),
            preview.preview(pathOf(arg(2)), option("--state").empty() ? 0 : std::stoi(option("--state"))));
        return 0;
    }
    if (arg(1) == "pdn-extract") {
        auto output = pathOf(option("--out"));
        if (output.empty() || fs::exists(output))
            throw std::runtime_error("pdn-extract requires a new --out directory");
        PdnSource source(pathOf(arg(2)));
        fs::create_directories(output);
        atomicText(output / L"layers.json", source.metadata().dump());
        for (size_t i = 0; i < source.layers.size(); ++i) {
            savePng(output / pathOf("layer-" + std::to_string(i) + ".png"), *source.decode(i));
            std::cout << i << " " << source.layers[i].name << "\n";
        }
        return 0;
    }
    if (arg(1) == "pdn-info") {
        PdnSource pdn(pathOf(arg(2)));
        auto info = pdn.metadata();
        if (flag("--verify-pixels")) {
            Json hashes = Json::array();
            for (size_t i = 0; i < pdn.layers.size(); i++)
                hashes.push(hashBytes(pdn.decode(i)->bgra));
            info["pixel_sha256"] = hashes;
        }
        std::cout << info.dump() << "\n";
        return 0;
    }
    if (arg(1) == "new") {
        Map map;
        auto width = option("--width"), height = option("--height"), name = option("--name");
        map.create(width.empty() ? 4000 : std::stoi(width), height.empty() ? 3000 : std::stoi(height),
                   name.empty() ? "Новая карта" : name);
        map.doc["symbols"] = defaultSymbols();
        map.save(pathOf(arg(2)));
        std::cout << fields({{"id", map.doc["id"]}, {"path", arg(2)}}).dump() << "\n";
        return 0;
    }
    if (arg(1) == "import") {
        auto out = option("--out");
        if (out.empty())
            throw std::runtime_error("--out is required");
        Map map;
        auto path = pathOf(arg(2));
        auto ext = path.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".pdn")
            map.importPdn(path, pathOf(out));
        else
            map.importRaster(path, pathOf(out));
        map.doc["symbols"] = defaultSymbols();
        map.doc["style"] = "atlas";
        map.save();
        std::cout
            << fields({{"id", map.doc["id"]}, {"layers", map.doc["layers"].size()}, {"output", out}}).dump()
            << "\n";
        return 0;
    }
    Map map;
    map.load(pathOf(arg(2)));
    Campaign campaign;
    if (!option("--project").empty()) {
        campaign.load(pathOf(option("--project")));
        if(campaign.entities.empty())throw std::runtime_error("Campaign registry is missing or empty: "+option("--project"));
    }
    auto emit = [&](const Json &value) { std::cout << value.dump() << "\n"; };
    if(auto handled=historyCli(map,campaign,arg(1),option,flag);handled>=0)return handled;
    if(arg(1)=="claim-country") {
        if(option("--expected-hash")!=map.diskHash)throw std::runtime_error("claim-country needs --expected-hash from inspect");
        auto id=option("--id");
        if(!map.doc["features"].contains(id) || map.doc["features"][id]["role"].str()!="country")
            throw std::runtime_error("Country not found");
        auto old=map.doc;
        MapRenderer renderer;renderer.claimCountry(map,id);
        auto result=map.diff(old);result["dry_run"]=flag("--dry-run");
        if(!flag("--dry-run"))map.save();
        emit(result);return 0;
    }
    if (arg(1) == "hit-test") {
        Point p{std::stod(option("--x")), std::stod(option("--y"))};
        auto id = map.hit(p, option("--tolerance").empty() ? 4 : std::stod(option("--tolerance")));
        emit(fields({{"id", id}, {"feature", map.doc["features"][id]}}));
        return id.empty() ? 2 : 0;
    }
    if (arg(1) == "trace") {
        double x = std::stod(option("--x")), y = std::stod(option("--y"));
        if (!std::isfinite(x) || !std::isfinite(y))
            throw std::runtime_error("Trace coordinates must be finite");
        const Json *layer = nullptr;
        auto layerId = option("--layer");
        if (!layerId.empty())
            layer = map.layer(layerId);
        else
            for (auto &l : map.doc["layers"].arr())
                if (l["kind"].str() == "raster" && l["visible"].boolean(true)) {
                    layer = &l;
                    break;
                }
        if (!layer || (*layer)["kind"].str() != "raster")
            throw std::runtime_error("Trace requires a raster layer");
        auto source = map.raster(*layer);
        auto rings = traceRegion(*source, {x, y},
                                 option("--tolerance").empty() ? 20 : std::stoi(option("--tolerance")));
        if (rings.empty())
            throw std::runtime_error("No region at that coordinate");
        auto targetLayer = map.vectorLayer();
        auto name = option("--name");
        if (name.empty())
            name = "Обведённая область";
        Json refs = Json::array();
        std::string feature;
        for (auto &ring : rings) {
            auto id = map.addPath(ring, true, "region", targetLayer, "#56D6D0", 2, 0);
            refs.push(map.doc["features"][id]["arcs"]);
            if (feature.empty())
                feature = id;
            else
                map.eraseFeature(id);
        }
        auto &f = map.doc["features"][feature];
        f["name"] = name;
        f["rings"] = refs;
        f.obj().erase("arcs");
        f["opacity"] = .3;
        f["provenance"] = fields({{"kind", "raster_trace"}, {"source_sha256", map.doc["import"]["sha256"]}});
        auto entity = option("--entity");
        if (!entity.empty()) {
            if (!campaign.find(entity))
                throw std::runtime_error("Entity not found; use --project");
            f["entity_id"] = entity;
        }
        auto validation = map.validate(campaign.entities.empty() ? nullptr : &campaign);
        if (validation["errors"].size())
            throw std::runtime_error(validation["errors"].dump());
        if (!flag("--dry-run"))
            map.save();
        emit(fields({{"feature_id", feature},
                     {"name", name},
                     {"rings", rings.size()},
                     {"nodes", map.featureNodes(f).size()},
                     {"dry_run", flag("--dry-run")}}));
        return 0;
    }
    if (arg(1) == "inspect") {
        auto entity = option("--entity");
        Json result = fields({{"file_sha256", map.diskHash},
                              {"id", map.doc["id"]},
                              {"name", map.doc["name"]},
                              {"width", map.doc["width"]},
                              {"height", map.doc["height"]},
                              {"story_anchor", map.doc["story_anchor"]},
                              {"layers", map.doc["layers"]},
                              {"feature_count", map.doc["features"].size()},
                              {"node_count", map.doc["nodes"].size()},
                              {"arc_count", map.doc["arcs"].size()},
                              {"versions", Json(map.versions())}});
        if (!entity.empty()) {
            result["features"] = Json::object();
            for (auto &[id, f] : map.doc["features"].obj())
                if (id == entity || f["entity_id"].str() == entity) {
                    result["features"][id] = f;
                    if(!result.contains("nodes"))result["nodes"] = Json::object();
                    for (auto &n : map.featureNodes(f))
                        result["nodes"][n] = map.doc["nodes"][n];
                }
        }
        emit(result);
        return 0;
    }
    if (arg(1) == "validate") {
        auto result = map.validate(campaign.entities.empty() ? nullptr : &campaign);
        if(flag("--history")) {
            auto h=map.validateHistory(campaign.entities.empty()?nullptr:&campaign);
            for(auto key:{"errors","warnings"})for(const auto &e:h[key].arr())result[key].push(e);
        }
        emit(result);
        return result["errors"].size() ? 1 : 0;
    }
    if (arg(1) == "apply") {
        auto patch = Json::parse(readText(pathOf(option("--patch"))));
        auto expected = patch["expected_hash"].str();
        if (expected.empty() || expected != map.diskHash)
            throw std::runtime_error("Patch must include expected_hash matching the current file_sha256");
        auto old = map.doc;
        map.apply(patch);
        auto report = map.validate(campaign.entities.empty() ? nullptr : &campaign);
        if (report["errors"].size())
            throw std::runtime_error(report["errors"].dump());
        auto diff = map.diff(old);
        diff["dry_run"] = flag("--dry-run");
        if (!flag("--dry-run"))
            map.save();
        emit(diff);
        return 0;
    }
    if (arg(1) == "snapshot") {
        Json anchor;
        auto scene = option("--scene");
        if (!scene.empty()) {
            auto e = campaign.find(scene);
            if (!e || e->type != "scene")
                throw std::runtime_error("Scene not found; supply --project");
            anchor = fields({{"chapter", std::atoi(e->chapter.c_str())},
                             {"branch", e->branch},
                             {"scene_id", scene},
                             {"relation", flag("--before") ? "before" : "after"},
                             {"evidence_ids", e->sources},
                             {"front_ids", e->fronts}});
        }
        auto id = map.snapshot(option("--name"), anchor, flag("--accepted"));
        map.save();
        emit(fields({{"version_id", id}}));
        return 0;
    }
    if (arg(1) == "diff") {
        auto from = map.version(option("--from"))["document"];
        auto to = option("--to");
        if (!to.empty() && to != "working")
            map.doc = map.version(to)["document"];
        emit(map.diff(from));
        return 0;
    }
    if (arg(1) == "restore") {
        map.restore(option("--version"));
        map.save();
        emit(fields({{"restored_from", option("--version")}, {"status", "working_copy"}}));
        return 0;
    }
    if (arg(1) == "render") {
        auto id = option("--version"), character = option("--character"), output = option("--output");
        if (output.empty())
            throw std::runtime_error("--output is required");
        if (!id.empty())
            map.doc = map.version(id)["document"];
        if (!character.empty())
            map.doc = map.forCharacter(character);
        MapRenderer renderer;
        auto file = pathOf(output);
        if (file.extension() == L".svg") {
            if (!character.empty())
                throw std::runtime_error("Character export is flattened PNG only");
            renderer.exportSvg(map, file);
        } else
            savePng(file, renderer.renderImage(map));
        emit(fields({{"output", output}, {"audience", character.empty() ? "gm" : character}}));
        return 0;
    }
    throw std::runtime_error("Unknown command. Run Atlas.Cli.exe help");
}
} // namespace atlas
