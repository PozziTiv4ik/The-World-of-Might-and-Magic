#include "app.hpp"
#include <iostream>
namespace atlas {
int historyCli(Map &map,const Campaign &campaign,const std::string &command,
               const std::function<std::string(const std::string &)> &option,
               const std::function<bool(const std::string &)> &flag) {
    auto emit=[](const Json &v){std::cout<<v.dump()<<"\n";};
    if(command=="history") {
        auto catalog=map.timeline();
        emit(fields({{"history_hash",map.timelineHash},{"file_sha256",map.diskHash},{"chains",catalog["chains"]},
            {"drafts",catalog["drafts"]},{"versions",Json(map.orderedVersions(option("--order").empty()?"story":option("--order"),
            option("--chain"),option("--chapter").empty()?0:std::stoi(option("--chapter")),option("--query")))}}));
        return 0;
    }
    if(command=="timeline") {
        auto catalog=map.timeline();
        auto patch=Json::parse(readText(pathOf(option("--patch"))));
        if(!patch["expected_hash"].isString() || patch["expected_hash"].str()!=map.timelineHash)
            throw std::runtime_error("Timeline patch needs current history_hash as expected_hash");
        if(!patch["operations"].isArray())throw std::runtime_error("Timeline operations are required");
        for(const auto &op:patch["operations"].arr()) {
            auto id=op["id"].str();
            if(op["op"].str()=="set_chain") {
                if(!catalog["chains"].contains(id))catalog["chains"][id]=fields({{"name",id},{"kind","variant"}});
                for(const auto &[k,v]:op["values"].obj())catalog["chains"][id][k]=v;
            }else if(op["op"].str()=="set_version") {
                auto snapshot=map.version(id);
                if(!catalog["entries"].contains(id))catalog["entries"][id]=fields({{"chain_id","main"},
                    {"story_order",0},{"story_anchor",snapshot["document"]["story_anchor"]},
                    {"label",snapshot["label"]},{"status",snapshot["status"]}});
                for(const auto &[k,v]:op["values"].obj()) {
                    if(k!="label" && k!="status" && k!="chain_id" && k!="story_anchor" && k!="story_order" &&
                       k!="description" && k!="after_ids" && k!="before_ids")
                        throw std::runtime_error("Unsupported timeline field: "+k);
                    catalog["entries"][id][k]=v;
                }
                Map candidate;candidate.doc=map.doc;candidate.directory=map.directory;
                candidate.doc["story_anchor"]=catalog["entries"][id]["story_anchor"];
                auto checked=candidate.validate(campaign.entities.empty()?nullptr:&campaign);
                if(checked["errors"].size())throw std::runtime_error(checked["errors"].dump());
            }else throw std::runtime_error("Unknown timeline operation");
        }
        if(!flag("--dry-run"))map.updateTimeline(catalog,patch["expected_hash"].str());
        else validateTimeline(catalog);
        emit(fields({{"dry_run",flag("--dry-run")},{"history_hash",map.timelineHash},{"timeline",catalog}}));return 0;
    }
    if(command=="draft") {
        auto id=option("--restore");
        if(!id.empty()){map.restoreDraft(id);map.save();emit(fields({{"restored_draft",id}}));}
        else if(!option("--name").empty())emit(fields({{"draft_id",map.stashDraft(option("--name"))}}));
        else emit(map.timeline()["drafts"]);
        return 0;
    }
    if(command=="recoveries") {
        if(!option("--restore").empty()){map.recoverSession(option("--restore"));map.save();emit(fields({{"recovered",true}}));}
        else emit(Json(map.recoveries()));
        return 0;
    }
    if(command=="snapshot") {
        auto working=map.doc;
        auto from=option("--from");
        if(!from.empty())map.restore(from);
        Json anchor;
        auto scene=option("--scene");
        if(!scene.empty()) {
            auto e=campaign.find(scene);
            if(!e || e->type!="scene")throw std::runtime_error("Scene not found; supply --project");
            anchor=fields({{"chapter",std::atoi(e->chapter.c_str())},{"branch",e->branch},{"scene_id",scene},
                {"relation",flag("--before")?"before":"after"},{"evidence_ids",e->sources},{"front_ids",e->fronts}});
        }
        if(!option("--chapter").empty())anchor["chapter"]=std::stoi(option("--chapter"));
        if(!option("--chain").empty())anchor["chain_id"]=option("--chain");
        if(!option("--date").empty())anchor["story_date"]=option("--date");
        if(!option("--sort-date").empty())anchor["sort_date"]=option("--sort-date");
        if(!option("--order").empty())anchor["story_order"]=std::stod(option("--order"));
        if(!option("--description").empty())anchor["description"]=option("--description");
        if(anchor.isObject() && !anchor.contains("chapter"))anchor["chapter"]=1;
        Map checked;checked.doc=map.doc;checked.directory=map.directory;checked.doc["story_anchor"]=anchor;
        auto validation=checked.validate(campaign.entities.empty()?nullptr:&campaign);
        if(validation["errors"].size())throw std::runtime_error(validation["errors"].dump());
        auto id=map.snapshot(option("--name"),anchor,flag("--accepted"));
        if(!from.empty() && !flag("--checkout")){map.doc=std::move(working);map.save();}
        emit(fields({{"version_id",id},{"file_sha256",map.diskHash},{"history_hash",map.timelineHash}}));return 0;
    }
    return -1;
}
} // namespace atlas
