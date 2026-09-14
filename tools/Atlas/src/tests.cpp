#include "app.hpp"
#include <iostream>
#include <set>

namespace atlas {
int selfTest(const fs::path &target, const fs::path &pdn, const fs::path &objectMap) {
    if (target.empty())
        throw std::runtime_error("self-test requires --out");
    auto dir = target / pathOf("atlas-tests-" + newId(""));
    fs::create_directories(dir);
    int passed = 0;
    auto require = [&](bool condition, const std::string &name) {
        if (!condition)
            throw std::runtime_error("FAILED: " + name);
        passed++;
        std::cout << "PASS " << name << "\n";
    };
    auto throws = [&](const std::function<void()> &action, const std::string &name) {
        bool caught = false;
        try {
            action();
        } catch (...) {
            caught = true;
        }
        require(caught, name);
    };
    Json j =
        fields({{"text", "Мир • карта"}, {"n", -.03125}, {"unknown_field", Json::Array{true, nullptr, 7}}});
    require(Json::parse(j.dump()) == j, "JSON Unicode and unknown fields round trip");
    require(Json::parse("\"\\ud83c\\udf0d\"").str() == "🌍", "JSON surrogate pairs");
    for (auto s : {"[1,]", "{\"x\":1,\"x\":2}", "01", "1e999", "\"\\ud800\"", "[true]garbage"})
        throws([&] { Json::parse(s); }, std::string("reject malformed JSON: ") + s);
    throws([&] { safeChild(dir, "../escape"); }, "reject relative path traversal");
    throws([&] { safeChild(dir, "C:/outside"); }, "reject absolute asset paths");
    Bytes gzip = {0x1f, 0x8b, 0x08, 0, 0,    0,    0,    0,    0, 3, 0xcb, 0x48, 0xcd,
                  0xc9, 0xc9, 7,    0, 0x86, 0xa6, 0x10, 0x36, 5, 0, 0,    0};
    require(gunzip(gzip, 5) == Bytes({'h', 'e', 'l', 'l', 'o'}), "DEFLATE fixed Huffman and gzip CRC");
    gzip[17] ^= 1;
    throws([&] { gunzip(gzip, 5); }, "reject corrupted gzip CRC");
    Map map;
    map.create(200, 120, "Проверка — карта");
    map.doc["symbols"] = defaultSymbols();
    auto originalSymbols = map.doc["symbols"];
    auto invalidSymbol = originalSymbols["fleet"];
    invalidSymbol["paths"][size_t(0)]["fill"] = "invalid-paint";
    auto templateOperation = fields({{"op", "put_symbol"}, {"id", "fleet"}, {"value", invalidSymbol}});
    auto templatePatch = fields({{"operations", Json::Array{templateOperation}}});
    throws([&] { map.apply(templatePatch); }, "invalid multicolour symbol patch is rejected");
    require(map.doc["symbols"] == originalSymbols,
            "failed symbol update leaves the original template intact");
    auto layer = map.vectorLayer();
    auto a =
        map.addPath({{10, 10}, {100, 10}, {100, 100}, {10, 100}}, true, "region", layer, "#E2BB71", 2, .001);
    auto b = map.addPath({{100, 10}, {190, 10}, {190, 100}, {100, 100}}, true, "region", layer, "#71BBCD", 2,
                         .001);
    std::set<std::string> arcs;
    for (auto &r : map.doc["features"][a]["arcs"].arr())
        arcs.insert(r["id"].str());
    int common = 0;
    for (auto &r : map.doc["features"][b]["arcs"].arr())
        if (arcs.contains(r["id"].str()))
            common++;
    require(common == 1, "adjacent regions share one arc");
    auto node = map.nearestNode({100, 10}, .001);
    auto before = map.doc;
    map.moveNode(node, {110, 12});
    auto checkPoint = [&](const std::string &id) {
        for (auto &ring : map.paths(map.doc["features"][id]))
            for (auto p : ring)
                if (p.x == 110 && p.y == 12)
                    return true;
        return false;
    };
    require(checkPoint(a) && checkPoint(b), "moving common node changes both regions");
    auto after = map.doc;
    History history;
    history.push(before, after, "Move border");
    require(history.undo(map.doc) && map.doc == before, "undo restores shared geometry");
    require(history.redo(map.doc) && map.doc == after, "redo restores shared geometry");
    {
        Map rigid;
        rigid.doc = before;
        auto neighbour = rigid.paths(rigid.doc["features"][b]);
        auto original = rigid.paths(rigid.doc["features"][a]);
        auto initial = rigid.doc;
        rigid.moveFeature(a, {27, -13});
        auto translated = rigid.paths(rigid.doc["features"][a]);
        bool exact = true, stationary = true;
        for (size_t r = 0; r < original.size(); r++)
            for (size_t i = 0; i < original[r].size(); i++)
                exact &= distance(translated[r][i], {original[r][i].x + 27, original[r][i].y - 13}) < 1e-8;
        auto untouched = rigid.paths(rigid.doc["features"][b]);
        for (size_t r = 0; r < neighbour.size(); r++)
            for (size_t i = 0; i < neighbour[r].size(); i++)
                stationary &= distance(neighbour[r][i], untouched[r][i]) < 1e-8;
        require(exact && stationary,
                "rigid country drag detaches shared border without stretching neighbour");
        auto moved = rigid.doc;
        History undo;
        undo.push(initial, moved, "Move country");
        require(undo.undo(rigid.doc) && rigid.doc == initial && undo.redo(rigid.doc) && rigid.doc == moved,
                "undo and redo exactly restore split topology");
        auto nodeCount = rigid.doc["nodes"].size();
        rigid.moveFeature(a, {1, 2});
        require(rigid.doc["nodes"].size() == nodeCount,
                "repeated country movement does not keep duplicating nodes");
        require(rigid.validate()["errors"].size() == 0, "detached country remains a valid map document");
        rigid.doc = initial;
        auto locked = rigid.addLayer("Locked neighbour");
        rigid.doc["features"][b]["layer_id"] = locked;
        (*rigid.layer(locked))["locked"] = true;
        auto frozen = rigid.paths(rigid.doc["features"][b]);
        rigid.moveFeature(a, {4, 9});
        auto frozenAfter = rigid.paths(rigid.doc["features"][b]);
        bool same = true;
        for (size_t r = 0; r < frozen.size(); r++)
            for (size_t i = 0; i < frozen[r].size(); i++)
                same &= distance(frozen[r][i], frozenAfter[r][i]) < 1e-8;
        require(same, "country movement preserves geometry belonging to locked neighbour");
    }
    {
        Map physical;
        physical.doc = before;
        physical.doc["features"][a]["role"] = "country";
        physical.doc["features"][b]["role"] = "country";
        MapRenderer r;
        require(r.separateLand(physical), "migrate legacy states to a separate land geometry");
        std::string land;
        for (const auto &[id, f] : physical.doc["features"].obj())
            if (f["role"].str() == "land")
                land = id;
        auto landNodes = physical.featureNodes(physical.doc["features"][land]);
        Json coast = Json::object();
        for (const auto &id : landNodes)
            coast[id] = physical.doc["nodes"][id];
        auto arc = physical.sharedBorder(a, {100, 55});
        require(!arc.empty(), "state body finds the nearest unlocked shared border");
        auto beforeBorder = physical.doc;
        physical.moveBorder(arc, {100, 55}, {25, 0}, 80);
        bool same = true;
        for (const auto &[id, p] : coast.obj())
            same &= physical.doc["nodes"][id] == p;
        require(same, "pulling a state boundary never moves land or coast nodes");
        bool moved = false;
        for (auto &[id, p] : physical.borderShape(arc, {100, 55}, {0, 0}, 80))
            moved |= p.x > 110;
        require(moved, "two-point shared border gains editable curvature");
        auto pa = physical.paths(physical.doc["features"][a]),
             pb = physical.paths(physical.doc["features"][b]);
        require(inside({110, 55}, pa[0]) && !inside({110, 55}, pb[0]),
                "one state gains the exact area the neighbour loses");
        require(physical.validate()["errors"].size() == 0,
                "moved political border has connected closed rings");
        History hist;
        hist.push(beforeBorder, physical.doc, "Border");
        require(hist.undo(physical.doc) && physical.doc == beforeBorder,
                "undo restores the shared boundary exactly");
        auto st = physical.doc;
        require(!r.separateLand(physical) && physical.doc == st, "land migration is idempotent");
        physical.eraseFeature(a);
        require(physical.doc["features"].contains(land), "deleting a state does not delete its land");
        r.exportSvg(physical, dir / L"separate-land.svg");
        require(readText(dir / L"separate-land.svg").find("clip-path=\"url(#atlas-land-clip)\"") !=
                    std::string::npos,
                "SVG clips state fills to independent vector land");
        physical.doc = beforeBorder;
        auto country = physical.addPath({{50, 25}, {140, 25}, {140, 80}, {50, 80}}, true, "region",
                                        physical.domainLayer(false), "#B8BCDA", 2);
        physical.doc["features"][country]["role"] = "country";
        r.claimCountry(physical, country);
        require(physical.validate()["errors"].size() == 0,
                "new state partitions existing countries into valid closed regions");
        auto shared = physical.sharedBorder(country, {60, 25});
        require(!shared.empty(), "new state automatically shares its border with the remaining neighbours");
        bool overlap = false;
        for (auto old : {a, b}) {
            bool in = false;
            for (auto &ring : physical.paths(physical.doc["features"][old]))
                if (inside({90, 50}, ring))
                    in = !in;
            overlap |= in;
        }
        require(!overlap, "creating a state claims land without overlapping older state fills");
        bool coastUnchanged = true;
        for (const auto &[id, p] : coast.obj())
            coastUnchanged &= physical.doc["nodes"][id] == p;
        require(coastUnchanged, "new states never rewrite physical coast coordinates");
        auto snapped = physical.addPath({{10, 10}, {30, 10}, {30, 22}, {10, 22}}, true, "region",
                                        physical.domainLayer(false), "#C8B48B", 2, 2);
        physical.doc["features"][snapped]["role"] = "country";
        bool separate = true;
        for (const auto &n : physical.featureNodes(physical.doc["features"][snapped]))
            separate &= !coast.contains(n);
        require(separate, "political snapping cannot reuse physical coast nodes");
    }
    {
        Map controls;
        controls.doc = before;
        for (auto id : {a, b}) {
            auto &f = controls.doc["features"][id];
            f["role"] = "country";
            f["stroke"] = "#FF0000";
            f["opacity"] = 1;
            f["show_label"] = false;
        }
        MapRenderer r;
        r.separateLand(controls);
        auto arc = controls.sharedBorder(a, {100, 55});
        auto original = controls.doc;
        auto handles = controls.borderControls(arc);
        require(controls.doc == original && handles.size() == 2,
                "selecting control handles does not simplify or edit the document");
        auto middle = controls.insertBorderControl(arc, {100, 55});
        auto validControlState = controls.doc;
        throws(
            [&] {
                controls.apply(fields(
                    {{"operations",
                      Json::Array{fields({{"op", "move_control"}, {"id", arc}, {"node_id", middle}})}}}));
            },
            "control patches reject a missing coordinate");
        require(controls.doc == validControlState, "invalid control patch leaves the whole map intact");
        auto nsBefore = controls.doc["nodes"];
        controls.moveBorderControl(arc, middle, {132, 55});
        size_t moved = 0;
        for (const auto &[id, p] : nsBefore.obj())
            if (controls.doc["nodes"][id] != p)
                moved++;
        require(moved == 1, "moving one handle changes exactly one coordinate, with no rubber influence");
        require(controls.doc["features"][a]["territory_scope"].str() == "land_sea" &&
                    controls.doc["features"][b]["territory_scope"].str() == "land_sea",
                "shared owners can both extend into the sea");
        require(controls.validate()["errors"].size() == 0,
                "explicit shared control preserves valid connected country rings");
        auto state = controls.doc;
        History h;
        h.push(original, state, "Control edit");
        require(h.undo(controls.doc) && controls.doc == original && h.redo(controls.doc) &&
                    controls.doc == state,
                "control insertion and movement undo and redo exactly");
        controls.deleteBorderControl(arc, middle);
        require(controls.doc["arcs"][arc]["nodes"].size() == 2 && controls.validate()["errors"].size() == 0,
                "deleting a handle joins its two adjacent segments");
        auto objects = controls.addLayer("Objects");
        auto overlay = controls.addPath({{65, 25}, {135, 25}, {135, 85}, {65, 85}}, true, "region", objects,
                                        "#FFFFFF", 0);
        controls.doc["features"][overlay]["opacity"] = 1;
        controls.doc["features"][overlay]["show_label"] = false;
        auto icon = controls.addSymbol({100, 55}, "mountain", objects, "#333333", 30);
        controls.doc["features"][icon]["show_label"] = false;
        auto picked = controls.hit({100, 55}, 3, true, 1, SelectionDomain::Borders);
        require(picked == a || picked == b,
                "border selection ignores an overlapping mountain and object layer");
        require(controls.hit({100, 55}, 3, true, 1, SelectionDomain::Objects) == icon,
                "object mode deliberately selects that same mountain");
        require(controls.hit({100, 55}, 3, true, 1, SelectionDomain::ActiveLayer, objects) == icon,
                "active-layer isolation selects only its own objects");
        auto labels = controls.addLayer("Text");
        auto label = controls.addSymbol({100, 55}, "label", labels, "#333333", 20);
        controls.doc["features"][label]["parent_id"] = a;
        controls.doc["features"][label]["role"] = "country_label";
        require(controls.hit({100, 55}, 3, true, 1, SelectionDomain::ActiveLayer, labels) == label,
                "isolated text layer cannot redirect selection to a country");
        r.clear();
        auto rendered = r.renderImage(controls);
        size_t px = (55 * rendered.width + 100) * 4;
        require(rendered.bgra[px + 2] > 200 && rendered.bgra[px + 1] < 80,
                "political border renders above mountains and labels even when its layer is underneath");
        r.exportSvg(controls, dir / L"borders-on-top.svg");
        auto svg = readText(dir / L"borders-on-top.svg");
        require(svg.find("atlas-political-borders") > svg.find(overlay),
                "SVG places the border pass after all object layers");
        auto *l = controls.layer(controls.doc["features"][a]["layer_id"].str());
        (*l)["locked"] = true;
        require(controls.politicalBorder({100, 55}, 8).empty(),
                "locked country boundaries cannot be picked through another layer");
        throws([&] { controls.insertBorderControl(arc, {100, 55}); },
               "locked shared border refuses point insertion");
        Map sea;
        sea.create(200, 120, "Sea");
        sea.doc["land_state_separated"] = true;
        auto sl = sea.domainLayer(false);
        auto country =
            sea.addPath({{20, 20}, {160, 20}, {160, 100}, {20, 100}}, true, "region", sl, "#EF7755", 2);
        sea.doc["features"][country]["role"] = "country";
        sea.doc["features"][country]["territory_scope"] = "land_sea";
        sea.doc["features"][country]["show_label"] = false;
        r.claimCountry(sea, country);
        require(sea.validate()["errors"].size() == 0,
                "a maritime state can be created without a land polygon");
        auto ocean = sea.doc["background"].str();
        auto im = r.renderImage(sea);
        auto bg = color(ocean);
        px = (60 * im.width + 90) * 4;
        require(std::abs(int(im.bgra[px + 2]) - int(bg.r * 255)) > 5,
                "maritime territory has a visible translucent ocean fill");
        auto outer = sea.politicalBorder({20, 20}, 5);
        require(!outer.empty() && sea.borderOwners(outer).size() == 1,
                "outer borders are editable without a second state");
        auto cs = sea.borderControls(outer);
        auto p = point(sea.doc["nodes"][cs.front()]);
        sea.moveBorderControl(outer, cs.front(), {p.x - 30, p.y - 12});
        require(sea.validate()["errors"].size() == 0,
                "moving the first handle of a closed sea boundary keeps it closed");
        sea.deleteBorderControl(outer, cs.front());
        require(sea.validate()["errors"].size() == 0,
                "first control of a closed outline can be deleted and reclosed");
        savePng(dir / L"maritime-controls.png", r.renderImage(sea));
    }
    auto pointId = map.addSymbol({45, 48}, "fortress", layer, "#FFFFFF", 24);
    map.doc["features"][pointId]["name"] = "Крепость";
    require((*map.orderedFeatures(layer).back())["id"].str() == pointId,
            "new objects are drawn above earlier objects in their layer");
    require(map.hit({70, 78}, 1) == pointId, "clicking the label selects its symbol");
    auto folder = dir / pathOf("Карта с русским именем");
    map.save(folder);
    Map reopened;
    reopened.load(folder);
    require(reopened.doc == map.doc, "map save and reopen are lossless");
    auto disk = map.diskHash;
    map.save();
    require(disk == map.diskHash, "unchanged save is byte stable");
    auto version = map.snapshot("Первый снимок", nullptr, true);
    auto versionPath = folder / L"versions" / pathOf(version + ".json");
    auto frozen = hashBytes(readBytes(versionPath));
    auto savedDefinition = map.doc["symbols"]["fortress"];
    map.doc["features"][pointId]["name"] = "Новое имя";
    map.doc["symbols"]["fortress"]["stroke_width"] = .2;
    map.restore(version);
    require(map.doc["features"][pointId]["name"].str() == "Крепость" &&
                map.doc["symbols"]["fortress"] == savedDefinition,
            "snapshot restores geometry labels and symbol definitions");
    require(hashBytes(readBytes(versionPath)) == frozen, "restoring does not modify immutable version");
    map.save();
    Map other;
    other.load(folder);
    map.doc["name"] = "Правка первого редактора";
    map.save();
    other.doc["name"] = "Правка второго редактора";
    throws([&] { other.save(); }, "concurrent save cannot overwrite another editor");
    Map check;
    check.load(folder);
    require(check.doc["name"].str() == "Правка первого редактора", "conflict preserves first writer");
    map.doc["features"][pointId]["name"] = "Автосохранение";
    map.autosave();
    Map recovered;
    recovered.load(folder);
    require(recovered.hasRecovery(), "detect unsaved recovery");
    recovered.recover();
    require(recovered.doc["features"][pointId]["name"].str() == "Автосохранение", "restore autosave");
    auto state = map.doc;
    Json invalidPatch = Json::object();
    invalidPatch["operations"] =
        Json::Array{fields({{"op", "move_node"}, {"id", node}, {"position", Json::Array{50, 50}}}),
                    fields({{"op", "unknown"}})};
    throws([&] { map.apply(invalidPatch); }, "invalid patch rolls back atomically");
    require(map.doc == state, "failed patch leaves document unchanged");
    map.doc["features"][pointId]["visibility"] = "public";
    require(map.forCharacter("CHAR-TEST")["features"].size() == 0,
            "public export requires an evidence reference");
    map.doc["features"][pointId]["evidence_ids"] = Json::Array{"SRC-TEST"};
    auto player = map.forCharacter("CHAR-TEST");
    require(player["features"].size() == 1 && player["features"].contains(pointId),
            "character view excludes GM features");
    require(!player.contains("import") && player["story_anchor"].null(),
            "character view strips import provenance");
    Image pixels(12, 12);
    for (int y = 1; y < 11; y++)
        for (int x = 1; x < 11; x++) {
            auto i = (y * 12 + x) * 4;
            pixels.bgra[i] = 80;
            pixels.bgra[i + 3] = 255;
        }
    for (int y = 4; y < 8; y++)
        for (int x = 4; x < 8; x++)
            pixels.bgra[(y * 12 + x) * 4] = 120;
    auto rings = traceRegion(pixels, {2, 2}, 0);
    require(rings.size() == 2, "raster tracing preserves an inner hole");
    savePng(dir / L"pixels.png", pixels);
    auto decoded = loadImage(dir / L"pixels.png");
    require(decoded->bgra == pixels.bgra, "WIC PNG straight alpha round trip");
    MapRenderer renderer;
    auto image = renderer.renderImage(map);
    require(image.width == 200 && image.height == 120, "same renderer exports full map dimensions");
    savePng(dir / L"render.png", image);
    renderer.exportSvg(map, dir / L"render.svg");
    require(readText(dir / L"render.svg").find(pointId) != std::string::npos,
            "SVG preserves semantic object IDs");
    require(map.validate()["errors"].size() == 0, "geometry and references validate");
    auto savedDoc = map.doc;
    map.doc["features"][pointId]["visibility"] = "restricted";
    map.doc["features"][pointId]["known_to"] = Json::Array{"CHAR-TEST"};
    require(map.forCharacter("CHAR-TEST")["features"].size() == 0,
            "known_to alone cannot establish character knowledge");
    map.doc["features"][pointId]["knowledge_evidence"] =
        Json::Array{fields({{"character_id", "CHAR-TEST"},
                            {"evidence_ids", Json::Array{"SRC-TEST"}},
                            {"basis", "Explicitly disclosed in the test source"}})};
    require(map.forCharacter("CHAR-TEST")["features"].size() == 1,
            "character export accepts an explicit knowledge basis");
    map.doc = savedDoc;
    auto broken = map.doc;
    map.doc["features"][a]["arcs"].arr().pop_back();
    require(map.validate()["errors"].size() > 0, "reject open territory contour");
    map.doc = broken;
    auto originalHashes = hashBytes(readBytes(versionPath));
    map.save(dir / L"save-as");
    require(map.versions().size() == 1 && hashBytes(readBytes(map.directory / L"versions" /
                                                              pathOf(version + ".json"))) == originalHashes,
            "Save As carries immutable history");
    Image fillImage(10, 10);
    floodFill(fillImage, {1, 1}, "#123456", 0, {{0, 0}, {5, 0}, {5, 5}, {0, 5}});
    require(fillImage.bgra[(1 * 10 + 1) * 4] == 0x56 && fillImage.bgra[(8 * 10 + 8) * 4 + 3] == 0,
            "raster flood fill respects selection mask");
    auto encoded = encodePng(fillImage);
    atomicWrite(dir / L"encoded.png", encoded);
    require(loadImage(dir / L"encoded.png")->bgra == fillImage.bgra,
            "memory PNG encoder preserves exact pixels");
    Map blendMap;
    blendMap.create(8, 8, "Blend test");
    blendMap.directory = dir / L"blend";
    Image rasterA(8, 8), rasterB(8, 8);
    for (size_t i = 0; i < rasterA.bgra.size(); i += 4) {
        rasterA.bgra[i] = 200;
        rasterA.bgra[i + 1] = 100;
        rasterA.bgra[i + 2] = 50;
        rasterA.bgra[i + 3] = 255;
        rasterB.bgra[i] = 128;
        rasterB.bgra[i + 1] = 128;
        rasterB.bgra[i + 2] = 128;
        rasterB.bgra[i + 3] = 255;
    }
    auto la = blendMap.addLayer("A", "raster"), lb = blendMap.addLayer("B", "raster");
    (*blendMap.layer(la))["image"] = blendMap.storeImage(rasterA);
    (*blendMap.layer(la))["source_type"] = "png";
    (*blendMap.layer(lb))["image"] = blendMap.storeImage(rasterB);
    (*blendMap.layer(lb))["source_type"] = "png";
    (*blendMap.layer(lb))["blend_mode"] = 1;
    auto mixed = renderer.renderImage(blendMap);
    require(std::abs(int(mixed.bgra[0]) - 100) <= 1 && std::abs(int(mixed.bgra[1]) - 50) <= 1,
            "native renderer applies multiply blend mode");
    for (size_t i = 0; i < rasterA.bgra.size(); i += 4) {
        rasterA.bgra[i] = 250;
        rasterA.bgra[i + 1] = 31;
        rasterA.bgra[i + 2] = 8;
        rasterA.bgra[i + 3] = 255;
        rasterB.bgra[i] = 180;
        rasterB.bgra[i + 1] = 200;
        rasterB.bgra[i + 2] = 220;
        rasterB.bgra[i + 3] = 128;
    }
    (*blendMap.layer(la))["image"] = blendMap.storeImage(rasterA);
    (*blendMap.layer(lb))["image"] = blendMap.storeImage(rasterB);
    (*blendMap.layer(lb))["blend_mode"] = 0;
    blendMap.doc["style"] = "original";
    auto originalBlend = renderer.renderImage(blendMap);
    blendMap.doc["style"] = "atlas";
    auto themedBlend = renderer.renderImage(blendMap);
    require(std::abs(int(originalBlend.bgra[0]) - int(themedBlend.bgra[0])) <= 1 &&
                std::abs(int(originalBlend.bgra[1]) - int(themedBlend.bgra[1])) <= 1,
            "atlas style preserves non-ocean colors formed by transparent layers");
    if (!pdn.empty()) {
        PdnSource source(pdn);
        require(source.layers.size() == 32 && source.width == 4000 && source.height == 3000,
                "actual supplied PDN metadata");
        int visible = 0;
        for (auto &l : source.layers)
            if (l.visible)
                visible++;
        require(visible == 5, "actual PDN visibility preserved");
        require(source.layers[8].name == source.layers[9].name, "duplicate PDN layer names remain distinct");
        Map imported;
        imported.importPdn(pdn, dir / L"imported");
        require(imported.doc["layers"].size() == 35,
                "all 32 PDN layers plus logical drawing layers imported");
        auto file = imported.doc["layers"][size_t(0)]["image"].str();
        require(hashBytes(readBytes(safeChild(imported.directory, file))) == hashBytes(readBytes(pdn)),
                "PDN source preserved byte for byte");
        Json hashes = Json::array();
        for (size_t i = 0; i < source.layers.size(); i++)
            hashes.push(hashBytes(source.decode(i)->bgra));
        atomicText(dir / L"pdn-pixel-hashes.json", hashes.dump());
        require(hashes.size() == 32, "every original layer decompresses and verifies CRC");
        Map again;
        again.load(imported.directory);
        require(again.doc == imported.doc, "PDN project round trip preserves all metadata");
    }
    if (!objectMap.empty()) {
        Map world;
        world.load(objectMap);
        auto sourceHash = hashBytes(readBytes(world.directory / L"map.json"));
        size_t countries = 0, peaks = 0;
        bool rasters = false;
        std::map<std::string, std::vector<std::string>> owners;
        for (auto &l : world.doc["layers"].arr())
            rasters |= l["kind"].str() == "raster";
        for (auto &[id, f] : world.doc["features"].obj()) {
            if (f["role"].str() == "relief")
                peaks++;
            if (f["role"].str() == "country") {
                countries++;
                for (auto &ring : f["rings"].arr())
                    for (auto &ref : ring.arr())
                        owners[ref["id"].str()].push_back(id);
            }
        }
        require(!rasters && countries == 47 && peaks > 1000,
                "actual world is 47 object territories and vector relief without raster layers");
        require((*world.layer(world.vectorLayer()))["visible"].boolean(true),
                "new drawings use a visible layer instead of hidden archive notes");
        auto label = world.doc["features"]["MAPOBJ-NAME-LIGHT-EMPIRE-14"];
        require(label.isObject() && world.hit(point(label["position"]), 1) == "MAPOBJ-COUNTRY-LIGHT-EMPIRE",
                "country title selects its editable territory");
        require(world.hit(point(label["position"]), 1, false) == label["id"].str(),
                "Alt selection can target the label independently");
        std::string shared;
        for (auto &[id, fs] : owners)
            if (fs.size() == 2 && world.doc["arcs"][id]["nodes"].size() > 5) {
                shared = id;
                break;
            }
        require(!shared.empty(), "reconstructed neighbours use a common boundary arc");
        auto node = world.doc["arcs"][shared]["nodes"][size_t(2)].str();
        auto old = point(world.doc["nodes"][node]);
        Point edited{old.x + 5, old.y - 3};
        world.moveNode(node, edited);
        bool both = true;
        for (auto &id : owners[shared]) {
            bool found = false;
            for (auto &ring : world.paths(world.doc["features"][id]))
                for (auto p : ring)
                    found |= distance(p, edited) < .0001;
            both &= found;
        }
        require(both, "moving a real shared boundary updates both territories");
        auto territory = owners[shared][0];
        auto beforeMove = world.doc;
        auto movedNode = point(world.doc["nodes"][node]);
        world.moveFeatures(owners[shared], {7, 9});
        require(distance(point(world.doc["nodes"][node]), {movedNode.x + 7, movedNode.y + 9}) < .0001,
                "group dragging moves shared nodes once");
        world.doc = beforeMove;
        {
            const std::string sun = "MAPOBJ-COUNTRY-ETERNAL-SUN";
            auto id = world.doc["features"].contains(sun) ? sun : territory;
            Map original;
            original.doc = world.doc;
            world.moveFeature(id, {340, -210});
            bool intact = true, rigid = true;
            for (const auto &[other, f] : original.doc["features"].obj()) {
                auto a = original.paths(f), b = world.paths(world.doc["features"][other]);
                if (a.size() != b.size()) {
                    intact = false;
                    break;
                }
                for (size_t r = 0; r < a.size(); r++) {
                    if (a[r].size() != b[r].size()) {
                        intact = false;
                        break;
                    }
                    for (size_t i = 0; i < a[r].size(); i++) {
                        if (other == id)
                            rigid &= distance(b[r][i], {a[r][i].x + 340, a[r][i].y - 210}) < 1e-8;
                        else
                            intact &= distance(a[r][i], b[r][i]) < 1e-8;
                    }
                }
            }
            require(intact && rigid,
                    "actual world country drag preserves every neighbouring contour and water polygon");
            require(world.validate()["errors"].size() == 0,
                    "actual world stays valid after a large country movement");
            world.doc = original.doc;
        }
        auto folder = dir / L"object-world";
        world.save(folder);
        Map loaded;
        loaded.load(folder);
        require(world.doc == loaded.doc, "full reconstructed world survives save and reload");
        MapRenderer objectRenderer;
        objectRenderer.exportSvg(world, dir / L"object-world.svg");
        auto svg = readText(dir / L"object-world.svg");
        require(svg.find("<image") == std::string::npos && svg.find("data:image") == std::string::npos,
                "world SVG contains native geometry without embedded images");
        require(svg.find("#F4EDD9") != std::string::npos, "SVG preserves filled cartographic symbol artwork");
        require(svg.find(">Гора</text>") == std::string::npos &&
                    svg.find(">Гора</tspan>") == std::string::npos,
                "hidden relief captions stay hidden in SVG");
        require(hashBytes(readBytes(objectMap / L"map.json")) == sourceHash,
                "object integration checks preserve the user's map");
        {
            Map checkMap;
            checkMap.doc = world.doc;
            MapRenderer physical;
            physical.separateLand(checkMap);
            Json coast = Json::object();
            for (const auto &[id, f] : checkMap.doc["features"].obj())
                if (f["role"].str() == "land")
                    for (const auto &node : checkMap.featureNodes(f))
                        coast[node] = checkMap.doc["nodes"][node];
            auto newState = checkMap.addPath({{1650, 1500}, {2100, 1500}, {2100, 1900}, {1650, 1900}}, true,
                                             "region", checkMap.domainLayer(false), "#B8B4DD", 2);
            checkMap.doc["features"][newState]["role"] = "country";
            physical.claimCountry(checkMap, newState);
            require(checkMap.validate()["errors"].size() == 0,
                    "partitioning the real world preserves valid political topology");
            auto border = checkMap.sharedBorder(newState, {1800, 1500});
            require(!border.empty(), "new state in the real world has draggable shared borders");
            checkMap.moveBorder(border, {1800, 1500}, {20, 10}, 180);
            bool unchanged = true;
            for (const auto &[id, p] : coast.obj())
                unchanged &= checkMap.doc["nodes"][id] == p;
            require(unchanged && checkMap.validate()["errors"].size() == 0,
                    "new real-world border moves while every land coordinate stays fixed");
            savePng(dir / L"physical-political.png", physical.renderImage(checkMap, 1200, 900));
        }
    }
    Map lod;
    auto lodLayer = lod.vectorLayer();
    auto area = lod.addPath({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true, "region", lodLayer, "#AABBCC", 2);
    lod.doc["features"][area]["show_label"] = false;
    auto caption = lod.addSymbol({50, 50}, "label", lodLayer, "#445566", 20);
    lod.doc["features"][caption]["min_zoom"] = .5;
    lod.doc["features"][caption]["parent_id"] = area;
    require(lod.hit({50, 50}, 1, false, .2) == area && lod.hit({50, 50}, 1, false, 1) == caption,
            "zoom-hidden labels do not intercept map clicks");
    lod.eraseFeature(area);
    require(!lod.doc["features"].contains(area) && !lod.doc["features"].contains(caption),
            "deleting an object removes its attached label");
    {
        Map interactive;
        interactive.create(400, 300, "Кеш отрисовки");
        interactive.doc["style"] = "cartographic";
        interactive.doc["symbols"] = defaultSymbols();
        auto layer = interactive.vectorLayer();
        auto region = interactive.addPath({{25, 25}, {300, 25}, {300, 210}, {25, 210}}, true, "region", layer,
                                          "#E2C99A", 3);
        interactive.doc["features"][region]["name"] = "Общая граница";
        interactive.addSymbol({150, 110}, "fleet", layer, "#6A553D", 75);
        MapRenderer cached;
        MapRenderer direct(cached.factory.get());
        Com<IWICImagingFactory> wic;
        atlas::check(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())),
            "test WIC");
        Com<IWICBitmap> pixels;
        atlas::check(
            wic->CreateBitmap(640, 480, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, pixels.put()),
            "test surface");
        Com<ID2D1RenderTarget> target;
        atlas::check(
            cached.factory->CreateWicBitmapRenderTarget(
                pixels.get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), target.put()),
            "test target");
        auto capture = [&](bool fast, double z, Point offset, bool moving = false) {
            target->BeginDraw();
            target->Clear(color("#D9E1D6"));
            if (fast)
                cached.drawInteractive(interactive, target.get(), D2D1::RectF(0, 0, 640, 480), z, offset, {},
                                       false, moving);
            else {
                direct.clear();
                direct.draw(interactive, target.get(), D2D1::RectF(0, 0, 640, 480), z, offset);
            }
            atlas::check(target->EndDraw(), "test frame");
            Bytes result(640 * 480 * 4);
            atlas::check(pixels->CopyPixels(nullptr, 640 * 4, UINT(result.size()), result.data()),
                         "test pixels");
            return result;
        };
        auto close = [&](const Bytes &a, const Bytes &b) {
            double sum = 0;
            for (size_t i = 0; i < a.size(); i++)
                sum += std::abs(int(a[i]) - int(b[i]));
            return sum / a.size() < .5;
        };
        auto first = capture(true, 1.3, {29, 33});
        require(close(first, capture(false, 1.3, {29, 33})), "cached view matches direct vector rendering");
        auto builds = cached.sceneBuilds;
        require(capture(true, 1.3, {29, 33}) == first && cached.sceneBuilds == builds,
                "hover reuses the prepared view without rebuilding the map");
        auto pan = capture(true, 1.3, {45, 49});
        require(cached.sceneBuilds == builds && close(pan, capture(false, 1.3, {45, 49})),
                "panning reuses overscan and preserves vector appearance");
        capture(true, 1.4, {45, 49}, true);
        require(cached.sceneBuilds == builds, "active zoom gesture reuses the existing view");
        require(close(capture(true, 1.4, {45, 49}), capture(false, 1.4, {45, 49})) &&
                    cached.sceneBuilds == builds + 1,
                "settled zoom restores an exact render");
        auto n = interactive.featureNodes(interactive.doc["features"][region]).front();
        interactive.moveNode(n, {65, 75});
        cached.invalidateScene();
        auto edited = capture(true, 1.3, {29, 33});
        require(edited != first && close(edited, capture(false, 1.3, {29, 33})),
                "editing a node invalidates the view and its cached geometry");
        interactive.doc["symbols"]["fleet"]["paths"][size_t(0)]["stroke"] = "#FF0000";
        cached.clear();
        require(close(capture(true, 1.3, {29, 33}), capture(false, 1.3, {29, 33})),
                "symbol changes refresh the compiled template cache");
        auto stable = interactive.doc;
        auto sceneCount = cached.sceneBuilds;
        for (int i = 0; i < 30; i++) {
            target->BeginDraw();
            cached.drawInteractive(interactive, target.get(), {0, 0, 640, 480}, 1.3, {29, 33}, {region});
            cached.drawDragPreview(interactive, target.get(), {0, 0, 640, 480}, 1.3, {29, 33}, {region},
                                   {double(i * 3), double(i)});
            atlas::check(target->EndDraw(), "drag preview frame");
        }
        require(interactive.doc == stable && cached.sceneBuilds == sceneCount,
                "30 drag frames reuse static map and never alter document before release");
        MapRenderer async(cached.factory.get());
        auto asyncFrame = [&](double z, Point at, bool moving) {
            target->BeginDraw();
            async.drawResponsive(interactive, target.get(), {0, 0, 640, 480}, z, at, {}, false, moving,
                                 nullptr);
            atlas::check(target->EndDraw(), "async view");
        };
        auto settle = [&](double z, Point at) {
            auto start = GetTickCount64();
            do {
                asyncFrame(z, at, false);
                if (async.responsiveSettled())
                    return true;
                Sleep(5);
            } while (GetTickCount64() - start < 10000);
            return false;
        };
        require(settle(1.3, {29, 33}),
                "background worker delivers initial map without blocking the render loop");
        Bytes asyncPixels(640 * 480 * 4);
        atlas::check(pixels->CopyPixels(nullptr, 640 * 4, UINT(asyncPixels.size()), asyncPixels.data()),
                     "async pixels");
        require(close(asyncPixels, capture(false, 1.3, {29, 33})),
                "settled asynchronous frame matches direct vector drawing");
        for (int i = 0; i < 60; i++)
            asyncFrame(.03 + i * .1, {double(-i * 200), double(i * 150)}, true);
        require(interactive.doc == stable, "large camera jumps and wide zoom range do not edit map data");
        asyncFrame(.45, {5, 7}, false);
        interactive.doc["features"][region]["fill"] = "#B34371";
        async.invalidateScene();
        require(settle(.9, {11, 17}),
                "document edits supersede in-flight frames and finish at the newest camera");
        atlas::check(pixels->CopyPixels(nullptr, 640 * 4, UINT(asyncPixels.size()), asyncPixels.data()),
                     "latest async pixels");
        require(close(asyncPixels, capture(false, .9, {11, 17})),
                "obsolete worker result cannot overwrite the latest edited map");
        interactive.doc["features"][region]["role"] = "country";
        interactive.doc["features"][region]["stroke"] = "#FF0000";
        interactive.doc["features"][region]["stroke_width"] = 5;
        interactive.doc["features"][region]["opacity"] = 1;
        async.clear();
        require(settle(1.3, {29, 33}),
                "asynchronous political overlay is ready independently of object layers");
        auto path = interactive.paths(interactive.doc["features"][region]).front();
        Point mid{(path[0].x + path[1].x) / 2, (path[0].y + path[1].y) / 2};
        auto topFrame = [&] {
            target->BeginDraw();
            async.drawResponsive(interactive, target.get(), {0, 0, 640, 480}, 1.3, {29, 33}, {}, false, true,
                                 nullptr, false);
            Painter paint(target.get(), async.textFactory.get());
            paint.fill({0, 0, 640, 480}, "#FFFFFF");
            async.drawResponsiveBorders(interactive, target.get(), {0, 0, 640, 480}, 1.3, {29, 33});
            atlas::check(target->EndDraw(), "top cached border");
            atlas::check(pixels->CopyPixels(nullptr, 640 * 4, UINT(asyncPixels.size()), asyncPixels.data()),
                         "top border pixels");
            size_t p = (size_t(mid.y * 1.3 + 33) * 640 + size_t(mid.x * 1.3 + 29)) * 4;
            return asyncPixels[p + 2] > 200 && asyncPixels[p + 1] < 80;
        };
        require(topFrame(), "cached political border remains above an opaque object preview");
        for (const auto &ref : interactive.doc["features"][region]["arcs"].arr())
            async.excludedBorderArcs.insert(ref["id"].str());
        require(!topFrame(), "control drag suppresses the old cached political line immediately");
    }
    auto result = fields({{"passed", passed}, {"failed", 0}, {"artifacts", pathText(dir)}});
    atomicText(dir / L"result.json", result.dump() + "\n");
    std::cout << result.dump() << "\n";
    return 0;
}
} // namespace atlas
