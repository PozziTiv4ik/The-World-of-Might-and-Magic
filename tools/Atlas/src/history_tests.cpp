#include "app.hpp"
#include <chrono>
#include <iostream>
namespace atlas {
int historySelfTest(const fs::path &dir,const fs::path &objectMap) {
    int passed=0;
    auto require=[&](bool ok,const std::string &name){if(!ok)throw std::runtime_error("FAILED: "+name);++passed;std::cout<<"PASS "<<name<<"\n";};
    auto rejects=[&](const std::function<void()> &f,const std::string &name){bool fail=false;try{f();}catch(...){fail=true;}require(fail,name);};
    {
        Map m;m.create(300,200,"History");m.save(dir/L"history-v2");
        Json anchor=fields({{"chapter",3},{"chain_id","main"},{"story_order",10}});
        auto a=m.snapshot("A",anchor,true);
        m.doc["background"]="#112233";anchor["story_order"]=30;
        auto c=m.snapshot("C",anchor,true);
        auto cBytes=readBytes(m.directory/L"versions"/pathOf(c+".json"));
        m.restore(a);m.doc["background"]="#445566";anchor["story_order"]=20;
        auto b=m.snapshot("B",anchor);
        auto list=m.orderedVersions();
        require(list.size()==3 && list[0]["id"].str()==a && list[1]["id"].str()==b && list[2]["id"].str()==c,
                "past insertion uses story order independently of recording order");
        require(m.orderedVersions("saved").front()["id"].str()==b,"newly inserted past state is the newest recording");
        require(m.version(b)["parent"].str()==a && m.version(c)["parent"].str()==a,"past insertion preserves honest revision ancestry");
        require(readBytes(m.directory/L"versions"/pathOf(c+".json"))==cBytes,"inserting a past state never rewrites a later snapshot");
        auto relation=m.timeline();relation["entries"][b]["after_ids"]=Json::Array{c};m.updateTimeline(relation);
        auto ordered=m.orderedVersions();
        require(ordered[1]["id"].str()==c && ordered[2]["id"].str()==b,"explicit temporal links take precedence over numerical display order");
        relation=m.timeline();relation["entries"][b].obj().erase("after_ids");m.updateTimeline(relation);
        auto dated=m.timeline();dated["entries"][b]["story_anchor"]["sort_date"]="2020-01-01";
        dated["entries"][c]["story_anchor"]["sort_date"]="2019-01-01";m.updateTimeline(dated);
        require(m.orderedVersions("date")[0]["id"].str()==c && m.orderedVersions()[0]["id"].str()==a,
                "calendar sorting is total and independent of the narrative sequence");
        require(m.timeline()["drafts"].size()>0,"restoring a snapshot preserves the previous working draft");
        auto draft=m.stashDraft("My current draft");auto prior=m.doc;
        m.restore(a);m.restoreDraft(draft);
        require(m.doc==prior,"draft switching restores the exact editable document");
        auto original=m.doc;m.doc["background"]="#ABCDEF";
        require(m.diff(original)["changes"].size()==1,"diff reports background changes");
        m.doc=original;m.save();
        auto catalog=m.timeline();
        catalog["entries"][a]["after_ids"]=Json::Array{c};
        catalog["entries"][c]["after_ids"]=Json::Array{a};
        rejects([&]{m.updateTimeline(catalog);},"chronology cycles are rejected");
        require(m.validateHistory()["errors"].size()==0,"full history checks document hashes ancestry drafts and timeline");
        Map other;other.load(m.directory);
        auto stale=other.timeline();auto next=m.timeline();next["chains"]["main"]["name"]="Edited chain";m.updateTimeline(next);
        rejects([&]{other.updateTimeline(stale);},"concurrent history writer cannot overwrite newer metadata");
        auto mainFile=m.directory/L"map.json",historyFile=m.directory/L"history"/L"manifest.json";
        auto after=m.doc;after["name"]="Recovered transaction";
        auto newCatalog=m.timeline();newCatalog["chains"]["main"]["name"]="Recovered history";
        auto mapText=after.dump()+"\n",historyText=newCatalog.dump()+"\n";
        auto journal=fields({{"map_before",hashText(readText(mainFile))},{"history_before",hashText(readText(historyFile))},
            {"map_after",hashText(mapText)},{"history_after",hashText(historyText)},{"map_text",mapText},{"history_text",historyText}});
        atomicText(m.directory/L".atlas"/L"history-transaction.json",journal.dump());
        atomicText(mainFile,mapText);
        Map recovered;recovered.load(m.directory);
        require(recovered.doc["name"].str()=="Recovered transaction" && recovered.timeline()["chains"]["main"]["name"].str()=="Recovered history",
                "interrupted two-file publication resumes both map and history");
        atomicText(m.directory/L"versions"/L"MAPVER-BROKEN.json","{bad");
        Map opens;opens.load(m.directory);
        require(opens.versions().size()==4 && opens.doc["name"].str()=="Recovered transaction","broken snapshot does not prevent opening the working map");
        require(opens.validateHistory()["errors"].size()>0,"history validation reports the broken snapshot");
    }
    {
        Map base;base.save(dir/L"recovery-sessions");
        Map a,b;a.load(base.directory);b.load(base.directory);
        a.doc["name"]="Session A";a.autosave();b.doc["name"]="Session B";b.autosave();
        a.recover();require(a.doc["name"].str()=="Session A","independent session autosaves cannot overwrite each other");
        require(a.recoveries().size()==1 || a.recoveries().size()==2,"other session recovery remains available");
        a.save();require(!a.hasRecovery() || a.recoveries()[0]["id"].str()==b.sessionId,"saving clears only the current session recovery");
    }
    {
        Map m;m.create(400,300,"Locked countries");
        auto land=m.addPath({{0,0},{400,0},{400,300},{0,300}},true,"region",m.doc["layers"][0]["id"].str(),"#AAAAAA",1);
        m.doc["features"][land]["role"]="land";
        auto locked=m.doc["layers"][1]["id"].str();
        auto country=m.addPath({{20,20},{180,20},{180,180},{20,180}},true,"region",locked,"#AAAAAA",1);
        m.doc["features"][country]["role"]="country";(*m.layer(locked))["locked"]=true;
        auto claim=m.addPath({{10,10},{200,10},{200,200},{10,200}},true,"region",m.doc["layers"][2]["id"].str(),"#BBBBBB",1);
        m.doc["features"][claim]["role"]="country";auto before=m.doc;
        MapRenderer renderer;rejects([&]{renderer.claimCountry(m,claim);},"territory partition respects locked neighbouring layers");
        require(m.doc==before,"failed partition leaves all geography unchanged");
    }
    {
        Map m;m.create(400,300,"Historical partitions");
        auto layer=m.doc["layers"][0]["id"].str();
        auto land=m.addPath({{0,0},{400,0},{400,300},{0,300}},true,"region",layer,"#AAAAAA",1);
        m.doc["features"][land]["role"]="land";
        auto empire=m.addPath({{0,0},{400,0},{400,300},{0,300}},true,"region",layer,"#AAAAAA",1);
        m.doc["features"][empire]["role"]="country";
        auto past=m.addPath({{-40,30},{150,30},{150,200},{-40,200}},true,"region",layer,"#BBBBBB",1);
        m.doc["features"][past]["role"]="country";m.doc["features"][past]["claim_within"]=empire;
        MapRenderer renderer;renderer.claimCountry(m,past);
        auto contains=[&](const std::string &id,Point p){bool result=false;for(const auto &ring:m.paths(m.doc["features"][id]))if(inside(p,ring))result=!result;return result;};
        require(contains(past,{50,100}) && !contains(empire,{50,100}),
                "historical partition removes its area from the later empire");
        require(!contains(past,{-20,100}) && !m.doc["features"][past].contains("claim_within"),
                "historical partition is clipped to the source country");
        m.doc["features"][empire]["claim_sources"]=Json::Array{past};renderer.claimCountry(m,empire);
        require(!m.doc["features"].contains(past) && contains(empire,{50,100}) &&
                contains(empire,{350,250}),"reunification preserves both historical territories");
        require(m.validate()["errors"].size()==0,"historical partition and reunification retain closed shared geometry");
    }
    {
        Map m;m.create(100,100,"Many revisions");m.save(dir/L"many-headers");
        auto example=m.snapshot("First",fields({{"chapter",1}}));
        const auto header=Json::parse(readText(m.directory/L"versions"/pathOf(example+".json")));
        for(int i=0;i<1000;++i) {
            auto h=header;auto id="MAPVER-PERF-"+std::to_string(i);h["id"]=id;
            h.obj().erase("integrity");h["integrity"]=hashText(h.dump());
            atomicText(m.directory/L"versions"/pathOf(id+".json"),h.dump()+"\n");
        }
        auto start=std::chrono::steady_clock::now();auto list=m.orderedVersions();
        auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        require(list.size()==1001,"history browser lists more than 1000 lightweight revision headers");
        std::cout<<"METRIC history_1001_headers_ms "<<ms<<"\n";
    }
    if(!objectMap.empty()) {
        Map m;m.load(objectMap);
        auto source=m.doc;History h;
        for(int i=0;i<215;++i){auto before=m.doc;m.doc["name"]="History action "+std::to_string(i);h.push(before,m.doc,"Rename");}
        int count=0;while(h.undo(m.doc))++count;
        require(count==215 && m.doc==source,"full world retains more than 200 exact vector undo steps");
        int redo=0;while(h.redo(m.doc))++redo;
        require(redo==215,"full world redo retains the complete command history");
        m.doc=source;m.rebuildPoliticalTopology();auto topology=m.doc;m.rebuildPoliticalTopology();
        require(m.doc==topology,"rebuilding unchanged political topology preserves all IDs and bytes");
        require(m.validate()["errors"].size()==0,"stable political topology remains connected and closed");
        auto claim=m.addPath({{1300,1120},{1640,1120},{1640,1450},{1300,1450}},true,"region",
                            "LAYER-TERRITORIES","#AABBCC",1);
        m.doc["features"][claim]["role"]="country";
        m.doc["features"][claim]["claim_within"]="MAPOBJ-COUNTRY-ETERNAL-SUN";
        m.directory=dir;
        MapRenderer renderer;renderer.claimCountry(m,claim);
        require(m.validate()["errors"].size()==0,"real-world partition keeps point-touching contours connected at every junction");
        std::vector<std::string> former{claim};
        for(const auto &ps:std::vector<std::vector<Point>>{
                {{1720,1500},{1940,1500},{1940,1770},{1720,1770}},
                {{1190,1450},{1720,1450},{1720,1810},{1190,1810}},
                {{1940,1370},{2220,1370},{2220,1760},{1940,1760}}}) {
            auto id=m.addPath(ps,true,"region","LAYER-TERRITORIES","#ABCDEF",1);
            m.doc["features"][id]["role"]="country";m.doc["features"][id]["claim_within"]="MAPOBJ-COUNTRY-ETERNAL-SUN";
            renderer.claimCountry(m,id);former.push_back(id);
        }
        for(const auto &id:former) {
            m.doc["features"]["MAPOBJ-COUNTRY-ETERNAL-SUN"]["claim_sources"]=Json::Array{id};
            renderer.claimCountry(m,"MAPOBJ-COUNTRY-ETERNAL-SUN");
        }
        require(m.validate()["errors"].size()==0,"four historical countries reunify without breaking retraced contour edges");
        bool merged=true;for(const auto &id:former)merged &= !m.doc["features"].contains(id);
        require(merged,"annexed countries cannot survive as microscopic boolean-operation residue");
    }
    return passed;
}
} // namespace atlas
