#include "model.hpp"
#include <algorithm>
#include <set>
#include <unordered_map>
namespace atlas {
namespace {
std::string fileHash(const fs::path &p) { return fs::exists(p)?hashText(readText(p)):""; }
bool validId(const std::string &id) {
    return !id.empty() && id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")==std::string::npos;
}
Json emptyTimeline() {
    return fields({{"schema_version",1},{"chains",fields({{"main",fields({{"name","Основная история"},{"kind","main"}})}})},
                   {"entries",Json::object()},{"drafts",Json::object()}});
}
Json readDocument(const fs::path &root,const std::string &ref,int depth=0) {
    if(depth>20)throw std::runtime_error("History document chain exceeds its limit");
    auto file=safeChild(root,ref);
    auto stored=Json::parse(readText(file));
    if(hashText(stored.dump())!=file.stem().string())throw std::runtime_error("History document checksum mismatch");
    if(stored.contains("storage") && stored["storage"].str()=="atlas-delta-1") {
        auto doc=readDocument(root,stored["base"].str(),depth+1);
        applyDocumentDelta(doc,stored["changes"]);
        return doc;
    }
    return stored;
}
std::pair<std::string,int> storeDocument(Map &map,const Json &doc,const Json &base=Json()) {
    Json payload=doc;
    int depth=0;
    if(base["document_ref"].isString() && base["document_depth"].num()<15 && base["document"].isObject()) {
        auto changes=documentDelta(base["document"],doc);
        auto delta=fields({{"storage","atlas-delta-1"},{"base",base["document_ref"]},{"changes",changes}});
        if(delta.dump(0).size()<doc.dump(0).size()/2) {payload=std::move(delta);depth=int(base["document_depth"].num())+1;}
    }
    auto text=payload.dump();
    auto ref="history/documents/"+hashText(text)+".json";
    auto file=safeChild(map.directory,ref);
    if(fs::exists(file)) {
        if(hashText(Json::parse(readText(file)).dump())!=hashText(text))throw std::runtime_error("Existing history object is corrupt");
    } else atomicText(file,text+"\n");
    return {ref,depth};
}
void validateCatalog(const Json &value) {
    if(value["schema_version"].num()!=1 || !value["entries"].isObject() || !value["chains"].isObject() ||
       !value["drafts"].isObject())throw std::runtime_error("Invalid history manifest");
    for(const auto &[id,c]:value["chains"].obj())
        if(!validId(id) || c["name"].str().empty())throw std::runtime_error("Chain needs an ID and a name");
    std::map<std::string,std::vector<std::string>> edges;
    for(const auto &[id,e]:value["entries"].obj()) {
        if(!validId(id) || !value["chains"].contains(e["chain_id"].str()))throw std::runtime_error("Unknown version chain: "+id);
        if(e.contains("story_order") && !e["story_order"].isNumber())throw std::runtime_error("Story order must be numeric");
        if(e.contains("status") && e["status"].str()!="accepted" && e["status"].str()!="draft" &&
           e["status"].str()!="archived")throw std::runtime_error("Invalid version status");
        for(auto field:{"after_ids","before_ids"})if(e[field].isArray())for(const auto &other:e[field].arr()) {
            if(!value["entries"].contains(other.str()))throw std::runtime_error("Unknown chronology reference: "+other.str());
            if(std::string(field)=="after_ids")edges[other.str()].push_back(id);
            else edges[id].push_back(other.str());
        }
    }
    std::map<std::string,int> color;
    std::function<void(const std::string &)> visit=[&](const auto &id) {
        if(color[id]==1)throw std::runtime_error("Chronology contains a cycle");
        if(color[id]==2)return;
        color[id]=1;for(const auto &next:edges[id])visit(next);color[id]=2;
    };
    for(const auto &[id,e]:value["entries"].obj())visit(id);
}
void commitTimeline(Map &map,const Json &catalog,const Json *document=nullptr) {
    validateCatalog(catalog);
    auto mapFile=map.directory/L"map.json",historyFile=map.directory/L"history"/L"manifest.json";
    if(map.directory.empty())throw std::runtime_error("Save the map before editing its history");
    FileLock lock(map.directory);
    if(fileHash(mapFile)!=map.diskHash)throw std::runtime_error("Карта изменена другим редактором. Загрузи актуальную копию.");
    if(fileHash(historyFile)!=map.timelineHash)throw std::runtime_error("История изменена другим редактором. Обнови список версий.");
    auto mapText=document?document->dump()+"\n":readText(mapFile);
    auto historyText=catalog.dump()+"\n";
    auto journal=fields({{"map_before",map.diskHash},{"history_before",map.timelineHash},
        {"map_after",hashText(mapText)},{"history_after",hashText(historyText)},
        {"map_text",mapText},{"history_text",historyText}});
    auto journalFile=map.directory/L".atlas"/L"history-transaction.json";
    atomicText(journalFile,journal.dump()+"\n");
    if(document && hashText(mapText)!=map.diskHash) {
        atomicText(map.directory/L".atlas"/L"last-save.json",readText(mapFile));
        atomicText(mapFile,mapText);
    }
    atomicText(historyFile,historyText);
    std::error_code ec;fs::remove(journalFile,ec);
    map.diskHash=hashText(mapText);map.timelineHash=hashText(historyText);
    if(document)map.clearSessionRecovery();
}
Json headerOnly(Json value) {
    if(value["document"].isObject() && !value.contains("story_anchor"))value["story_anchor"]=value["document"]["story_anchor"];
    value.obj().erase("document");return value;
}
std::string lower(const std::string &s) {
    auto w=wide(s);if(!w.empty())CharLowerBuffW(w.data(),DWORD(w.size()));return utf8(w);
}
}
void recoverHistoryTransaction(const fs::path &root) {
    auto journal=root/L".atlas"/L"history-transaction.json";
    if(!fs::exists(journal))return;
    FileLock lock(root);
    if(!fs::exists(journal))return;
    auto j=Json::parse(readText(journal));
    auto map=root/L"map.json",history=root/L"history"/L"manifest.json";
    if(hashText(j["map_text"].str())!=j["map_after"].str() ||
       hashText(j["history_text"].str())!=j["history_after"].str())throw std::runtime_error("History recovery journal is damaged");
    auto mh=fileHash(map),hh=fileHash(history);
    if((mh!=j["map_before"].str() && mh!=j["map_after"].str()) ||
       (hh!=j["history_before"].str() && hh!=j["history_after"].str()))
        throw std::runtime_error("History recovery found an external edit; its journal has been preserved");
    if(mh!=j["map_after"].str())atomicText(map,j["map_text"].str());
    if(hh!=j["history_after"].str())atomicText(history,j["history_text"].str());
    std::error_code ec;fs::remove(journal,ec);
}
Json Map::timeline() const {
    auto file=directory/L"history"/L"manifest.json";
    if(directory.empty() || !fs::exists(file)){timelineHash="";return emptyTimeline();}
    auto text=readText(file);
    auto value=Json::parse(text);validateCatalog(value);timelineHash=hashText(text);return value;
}
void Map::updateTimeline(const Json &value,const std::string &expected) {
    if(!expected.empty() && expected!=timelineHash)throw std::runtime_error("Expected history hash does not match");
    for(const auto &[id,e]:value["entries"].obj())
        if(!fs::exists(directory/L"versions"/pathOf(id+".json")))throw std::runtime_error("Unknown version: "+id);
    commitTimeline(*this,value);
}
void validateTimeline(const Json &value) { validateCatalog(value); }
std::string Map::snapshot(const std::string &label,const Json &anchor,bool accepted) {
    if(directory.empty())throw std::runtime_error("Сначала сохрани карту");
    if(label.empty())throw std::runtime_error("Укажи название версии");
    auto catalog=timeline();
    auto id=newId("MAPVER-");
    catalog["next_sequence"]=catalog["next_sequence"].num()+1;
    Json state=doc;state["story_anchor"]=anchor;
    Map candidate;candidate.directory=directory;candidate.doc=state;
    auto errors=candidate.validate()["errors"];if(errors.size())throw std::runtime_error(errors.dump());
    Json base;
    auto parent=doc["parent_version"].str();
    if(!parent.empty())base=version(parent);
    auto stored=storeDocument(*this,state,base);
    auto snap=fields({{"format",2},{"id",id},{"map_id",doc["id"]},{"label",label},{"recorded_at",nowUtc()},
        {"sequence",catalog["next_sequence"]},
        {"status",accepted?"accepted":"draft"},{"parent",doc["parent_version"]},{"story_anchor",anchor},
        {"document_ref",stored.first},{"document_depth",stored.second},{"document_hash",hashText(state.dump())},
        {"asset_hashes",Json::object()}});
    for(const auto &l:state["layers"].arr())if(l["image"].isString())
        snap["asset_hashes"][l["image"].str()]=hashBytes(readBytes(safeChild(directory,l["image"].str())));
    snap["integrity"]=hashText(snap.dump());
    // Immutable objects are published first; the journal atomically recovers both mutable heads.
    atomicText(directory/L"versions"/pathOf(id+".json"),snap.dump()+"\n");
    auto chain=anchor["chain_id"].str("main");
    if(!catalog["chains"].contains(chain))catalog["chains"][chain]=fields({{"name",chain},{"kind","variant"}});
    double order=0;
    for(const auto &[other,e]:catalog["entries"].obj())if(e["chain_id"].str()==chain)
        order=std::max(order,e["story_order"].num());
    catalog["entries"][id]=fields({{"chain_id",chain},{"story_order",anchor["story_order"].num(order+1024)},
        {"story_anchor",anchor},{"label",label},{"status",snap["status"]},{"description",anchor["description"].str()}});
    auto working=doc;working["parent_version"]=id;working["story_anchor"]=anchor;
    commitTimeline(*this,catalog,&working);
    doc=std::move(working);return id;
}
std::vector<Json> Map::versions() const {
    std::vector<Json> out;
    auto folder=directory/L"versions";
    if(directory.empty() || !fs::exists(folder))return out;
    Json cache=Json::object(),next=Json::object();
    auto cacheFile=directory/L".atlas"/L"versions-cache.json";
    try{if(fs::exists(cacheFile))cache=Json::parse(readText(cacheFile));}catch(...) {}
    Json catalog;
    try{catalog=timeline();}catch(const std::exception &e){catalog=emptyTimeline();}
    for(const auto &entry:fs::directory_iterator(folder)) {
        if(!entry.is_regular_file() || entry.path().extension()!=L".json")continue;
        auto name=entry.path().filename().string();
        auto stamp=std::to_string(entry.file_size())+":"+std::to_string(static_cast<long long>(entry.last_write_time().time_since_epoch().count()));
        Json head;
        try {
            if(cache[name]["stamp"].str()==stamp)head=cache[name]["header"];
            else head=headerOnly(Json::parse(readText(entry.path())));
            if(head["id"].str()!=entry.path().stem().string() || !head["document_hash"].isString())
                throw std::runtime_error("Invalid version header");
            next[name]=fields({{"stamp",stamp},{"header",head}});
        }catch(const std::exception &e) {
            head=fields({{"id",entry.path().stem().string()},{"label","Повреждённая версия"},
                {"status","damaged"},{"error",e.what()},{"recorded_at",""}});
        }
        auto id=head["id"].str();
        if(catalog["entries"].contains(id)) {
            const auto &meta=catalog["entries"][id];
            for(auto key:{"label","status","story_anchor","chain_id","story_order","description","after_ids","before_ids"})
                if(meta.contains(key))head[key]=meta[key];
        }else {head["chain_id"]="archive";head["story_order"]=0;}
        out.push_back(std::move(head));
    }
    try{if(!(next==cache))atomicText(cacheFile,next.dump()+"\n");}catch(...) {}
    std::sort(out.begin(),out.end(),[](const auto &a,const auto &b){
        if(a["recorded_at"].str()!=b["recorded_at"].str())return a["recorded_at"].str()<b["recorded_at"].str();
        if(a["sequence"].num()!=b["sequence"].num())return a["sequence"].num()<b["sequence"].num();
        return a["id"].str()<b["id"].str();
    });
    return out;
}
Json Map::version(const std::string &id) const {
    if(!validId(id))throw std::runtime_error("Invalid version ID");
    auto snap=Json::parse(readText(directory/L"versions"/pathOf(id+".json")));
    if(snap["id"].str()!=id)throw std::runtime_error("Version ID mismatch");
    if(snap.contains("integrity")) {
        auto check=snap;check.obj().erase("integrity");
        if(hashText(check.dump())!=snap["integrity"].str())throw std::runtime_error("Version header integrity check failed");
    }
    if(snap["document_ref"].isString())snap["document"]=readDocument(directory,snap["document_ref"].str());
    if(!snap["document"].isObject() || hashText(snap["document"].dump())!=snap["document_hash"].str())
        throw std::runtime_error("Version integrity check failed");
    if(snap["asset_hashes"].isObject())for(const auto &[asset,hash]:snap["asset_hashes"].obj())
        if(hashBytes(readBytes(safeChild(directory,asset)))!=hash.str())throw std::runtime_error("Version raster integrity failed: "+asset);
    return snap;
}
std::vector<Json> Map::orderedVersions(const std::string &order,const std::string &chain,int chapter,const std::string &query) const {
    auto list=versions();auto q=lower(query);
    list.erase(std::remove_if(list.begin(),list.end(),[&](const auto &v){
        if(!chain.empty() && v["chain_id"].str()!=chain)return true;
        if(chapter && v["story_anchor"]["chapter"].num()!=chapter)return true;
        return !q.empty() && lower(v["label"].str()+" "+v["id"].str()+" "+v["story_anchor"].dump(0)+" "+v["description"].str()).find(q)==std::string::npos;
    }),list.end());
    if(order=="saved"){std::reverse(list.begin(),list.end());return list;}
    std::stable_sort(list.begin(),list.end(),[&](const auto &a,const auto &b){
        auto ac=a["story_anchor"]["chapter"].num(1e9),bc=b["story_anchor"]["chapter"].num(1e9);
        if(ac!=bc)return ac<bc;
        auto ad=a["story_anchor"]["sort_date"].str(),bd=b["story_anchor"]["sort_date"].str();
        if(order=="date") {
            if(ad.empty()!=bd.empty())return !ad.empty();
            if(ad!=bd)return ad<bd;
        }
        if(a["story_order"].num()!=b["story_order"].num())return a["story_order"].num()<b["story_order"].num();
        if(a["recorded_at"].str()!=b["recorded_at"].str())return a["recorded_at"].str()<b["recorded_at"].str();
        return a["id"].str()<b["id"].str();
    });
    if(order=="parents") {
        std::vector<Json> tree;std::set<std::string> included;
        std::function<void(const Json &,int)> add=[&](const Json &v,int depth){
            if(!included.insert(v["id"].str()).second)return;
            auto row=v;row["depth"]=depth;tree.push_back(row);
            for(const auto &child:list)if(child["parent"].str()==v["id"].str())add(child,depth+1);
        };
        std::set<std::string> ids;for(const auto &v:list)ids.insert(v["id"].str());
        for(const auto &v:list)if(!ids.contains(v["parent"].str()))add(v,0);
        for(const auto &v:list)add(v,0);
        return tree;
    }
    if(order=="story") {
        // The displayed order is a stable topological order of explicit temporal
        // relations. Recording ancestry remains a separate, immutable graph.
        std::map<std::string,size_t> index;for(size_t i=0;i<list.size();++i)index[list[i]["id"].str()]=i;
        std::vector<std::set<size_t>> next(list.size());std::vector<int> incoming(list.size());
        auto edge=[&](size_t from,size_t to){if(next[from].insert(to).second)++incoming[to];};
        for(size_t i=0;i<list.size();++i) {
            const auto &row=list[i];
            if(row["after_ids"].isArray())for(const auto &v:row["after_ids"].arr())if(index.contains(v.str()))edge(index[v.str()],i);
            if(row["before_ids"].isArray())for(const auto &v:row["before_ids"].arr())if(index.contains(v.str()))edge(i,index[v.str()]);
        }
        std::set<size_t> ready;for(size_t i=0;i<list.size();++i)if(!incoming[i])ready.insert(i);
        std::vector<Json> ordered;
        while(!ready.empty()){auto i=*ready.begin();ready.erase(ready.begin());ordered.push_back(list[i]);for(auto j:next[i])if(!--incoming[j])ready.insert(j);}
        if(ordered.size()==list.size())return ordered;
    }
    return list;
}
std::string Map::stashDraft(const std::string &label) {
    if(directory.empty())throw std::runtime_error("Сначала сохрани карту");
    auto catalog=timeline();
    auto hash=hashText(doc.dump());
    for(const auto &[id,d]:catalog["drafts"].obj())if(d["document_hash"].str()==hash)return id;
    auto stored=storeDocument(*this,doc);
    auto id=newId("DRAFT-");
    catalog["drafts"][id]=fields({{"label",label},{"recorded_at",nowUtc()},{"document_ref",stored.first},
        {"document_hash",hash},{"parent_version",doc["parent_version"]}});
    commitTimeline(*this,catalog);return id;
}
void Map::restoreDraft(const std::string &id) {
    auto catalog=timeline();if(!catalog["drafts"].contains(id))throw std::runtime_error("Черновик не найден");
    auto d=catalog["drafts"][id];auto state=readDocument(directory,d["document_ref"].str());
    if(hashText(state.dump())!=d["document_hash"].str())throw std::runtime_error("Draft integrity check failed");
    stashDraft("Перед переключением черновика");
    doc=std::move(state);doc["status"]="draft";clearCache();
}
void Map::restore(const std::string &id) {
    auto state=version(id)["document"];
    stashDraft("Перед переходом к версии");
    doc=std::move(state);doc["status"]="draft";doc["parent_version"]=id;clearCache();
}
Json Map::validateHistory(const Campaign *campaign) const {
    Json report=fields({{"errors",Json::array()},{"warnings",Json::array()}});
    Json catalog;
    try{catalog=timeline();}catch(const std::exception &e){report["errors"].push(e.what());return report;}
    std::map<std::string,std::string> parents;
    for(const auto &head:versions()) {
        auto id=head["id"].str();
        try {
            auto snap=version(id);
            Map candidate;candidate.directory=directory;candidate.doc=snap["document"];
            auto validation=candidate.validate(campaign);
            for(const auto &e:validation["errors"].arr())report["errors"].push(id+": "+e.str());
            parents[id]=snap["parent"].str();
        }catch(const std::exception &e){report["errors"].push(id+": "+e.what());}
    }
    for(const auto &[id,parent]:parents) {
        if(!parent.empty() && !parents.contains(parent))report["errors"].push("Missing version parent: "+id);
        std::set<std::string> seen;auto at=id;
        while(!at.empty() && parents.contains(at)) {
            if(!seen.insert(at).second){report["errors"].push("Version ancestry cycle: "+id);break;}
            at=parents[at];
        }
    }
    for(const auto &[id,e]:catalog["entries"].obj()) {
        if(!parents.contains(id))report["errors"].push("Missing indexed version: "+id);
        if(campaign && !e["story_anchor"]["scene_id"].str().empty()) {
            Map candidate;candidate.doc=doc;candidate.directory=directory;candidate.doc["story_anchor"]=e["story_anchor"];
            auto validation=candidate.validate(campaign);
            for(const auto &v:validation["errors"].arr())report["errors"].push(id+": "+v.str());
        }
    }
    auto parent=doc["parent_version"].str();
    if(!parent.empty() && !parents.contains(parent))report["errors"].push("Working base version is missing");
    for(const auto &[id,d]:catalog["drafts"].obj())try {
        auto state=readDocument(directory,d["document_ref"].str());
        if(hashText(state.dump())!=d["document_hash"].str())report["errors"].push("Draft integrity: "+id);
    }catch(const std::exception &e){report["errors"].push(id+": "+e.what());}
    return report;
}
} // namespace atlas
