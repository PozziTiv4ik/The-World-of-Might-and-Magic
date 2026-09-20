#include "app.hpp"
#include <iostream>
#include <algorithm>
namespace atlas {
int workspaceSelfTest(const fs::path &temporary) {
    int passed=0;
    auto require=[&](bool ok,const std::string &name){if(!ok)throw std::runtime_error("FAILED: "+name);++passed;std::cout<<"PASS "<<name<<"\n";};
    App app({},{});
    app.map.create(640,480,"Workspace regression");
    auto country=app.map.addPath({{30,30},{180,30},{180,200},{30,200}},true,"region",app.map.vectorLayer(),"#BBCDBB",1);
    app.map.doc["features"][country]["role"]="country";
    app.command(SetTool,"0");
    require(app.hitObject({80,80},2)==country,"pointer selects territories directly without opening a mode panel");
    app.map.save(temporary/pathOf("minimal-workspace-"+newId("")));
    Json early=fields({{"chapter",1},{"scene_id","SCENE-0001"},{"relation","after"},{"chain_id","main"},{"story_order",1024}});
    Json late=fields({{"chapter",4},{"scene_id","SCENE-0002"},{"relation","after"},{"chain_id","main"},{"story_order",2048}});
    auto a=app.map.snapshot("Раннее событие",early,true);
    auto b=app.map.snapshot("Позднее событие",late,true);
    auto bBytes=readBytes(app.map.directory/L"versions"/pathOf(b+".json"));
    app.map.restore(a);app.map.doc["background"]="#C3DCCE";
    auto revision=app.map.snapshot("Раннее событие",early,true);
    app.refreshVersions();
    require(app.storyEvents.size()==2,"editing an old scene keeps exactly one chronological event per scene");
    require(app.storyEvents[0].revisions.size()==2&&app.storyEvents[0].moment["id"].str()==revision,"old scene revisions have their own sequence and current revision");
    require(app.storyEvents[1].moment["id"].str()==b,"newly saved early revision stays before later story event");
    require(readBytes(app.map.directory/L"versions"/pathOf(b+".json"))==bBytes,"timeline grouping never rewrites later immutable versions");
    auto catalog=app.map.timeline();catalog["chains"]["editions"]=fields({{"kind","archive"},{"name","Редакции"}});
    catalog["entries"][a]["chain_id"]="editions";catalog["entries"][a]["status"]="archived";app.map.updateTimeline(catalog);
    app.refreshVersions();
    require(app.storyEvents.size()==2&&app.storyEvents[0].revisions.size()==2,"previously archived corrections remain available under their scene");
    auto originals=app.map.doc;
    app.width=1366;app.height=768;app.computeLayout();app.zoom=.73;app.offset={-174,32};
    require(app.canvas.left==0&&app.canvas.top==0&&app.canvas.right==1366&&app.canvas.bottom==768,"map uses the entire client area without permanent panels");
    app.command(HistoryView);
    app.command(Paste);
    require(app.historyView&&!app.compareMode&&app.map.doc==originals,"history navigation and paste guard leave editable map unchanged");
    require(!app.inCanvas({650,400}),"history canvas cannot accidentally edit map objects");
    app.command(SelectRevision,a);
    require(app.selectedEvent()&&app.selectedEvent()->revisions.size()==2,"archived revision selection resolves to the same story node");
    app.command(EditorView);
    require(!app.historyView&&app.zoom==.73&&app.offset.x==-174&&app.offset.y==32,"back to map restores its exact camera");
    app.command(HistoryView);app.command(SelectVersion,b);app.command(OpenMoment);
    require(!app.historyView&&!app.dirty&&app.map.doc["parent_version"].str()==b,"open selected event loads a saved editable map with a protected prior draft");
    require(app.map.timeline()["drafts"].size()>0,"opening another event preserves the previous working map as a draft");
    app.before=app.map.doc;app.map.doc["background"]="#A0B1C2";app.changed("Background regression");app.command(Save);
    auto saved=app.map.doc["parent_version"].str();
    app.refreshVersions();
    require(!app.dirty&&saved!=b&&app.storyEvents.size()==2,"Ctrl+S persists a new editing revision without adding a story event");
    require(app.map.version(saved)["parent"].str()==b&&app.map.version(saved)["document"]["background"].str()=="#A0B1C2","saved revision has the actual editing parent and changed document");
    require(readBytes(app.map.directory/L"versions"/pathOf(b+".json"))==bBytes,"saving editor revisions preserves the source snapshot bytes");
    app.command(HistoryView);app.command(SelectVersion,saved);app.compareRevisions();
    require(app.compareMode==1&&app.versionDetails["id"].str()==b&&app.comparisonB->doc["_comparison_id"].str()==saved,"comparison pairs the previous and selected revisions of one event");
    app.command(HistoryView);app.command(HistoryFilter,"1");
    require(app.storyEvents.size()==1&&app.selectedEvent(),"chapter filter selects a visible event and has no stale event detail");
    app.query="no-result-workspace-test";app.refreshVersions();
    require(app.storyEvents.empty()&&!app.selectedEvent()&&!app.comparison,"empty search clears hidden selection and map-opening target");
    app.query.clear();app.historyChapter=0;app.refreshVersions();
    auto beforeAnchor=early;beforeAnchor["relation"]="before";
    auto beforeVersion=app.map.snapshot("До события",beforeAnchor);
    app.refreshVersions();require(app.storyEvents.size()==3,"before and after the same scene remain distinct moments");
    Json withoutScene=fields({{"chapter",2},{"chain_id","main"},{"story_order",3000}});
    auto standalone=app.map.snapshot("Без сцены A",withoutScene);
    auto separate=app.map.snapshot("Без сцены B",withoutScene);
    app.refreshVersions();require(app.storyEvents.size()==5,"unanchored versions in one chapter are not merged by title or date");
    app.map.restore(standalone);withoutScene["event_id"]="version:"+standalone;
    app.map.snapshot("Без сцены A",withoutScene);app.refreshVersions();
    require(app.storyEvents.size()==5,"legacy scene-free states can gain revisions via stable event identity");
    require(app.map.validateHistory()["errors"].size()==0,"event revisions and archived editions pass complete integrity validation");
    for(auto size:std::vector<std::pair<int,int>>{{960,500},{1366,768},{1920,1080}}) {
        app.width=float(size.first);app.height=float(size.second);app.computeLayout();
        app.versionScroll=10000;app.computeLayout();app.revealVersion(standalone);
        require(app.versionScroll>=0&&app.versionScroll<=std::max(0,int(app.storyEvents.size())-app.visibleEvents()),"timeline pagination stays in range at "+std::to_string(size.first));
    }
    for(int state:{20,21,23,26,28,29}) {
        App preview({},{});auto before=readBytes(app.map.directory/L"map.json");auto image=preview.preview(app.map.directory,state);
        require(image.width>=800&&image.height>=500&&readBytes(app.map.directory/L"map.json")==before,"real UI rendering does not alter map data, state "+std::to_string(state));
        bool open=false,history=false,inside=true;
        for(const auto &hit:preview.hits) {open|=hit.command==OpenMoment;history|=hit.command==HistoryView;
            if(hit.command)inside &= hit.rect.left>=0&&hit.rect.right<=preview.width+1&&hit.rect.top>=0&&hit.rect.bottom<=preview.height+1;}
        require(inside,"all visible controls stay within screen bounds, state "+std::to_string(state));
        require(state==21||state==23||state==28?open&&!history:history&&!open,"map and history expose separate navigation actions");
    }
    return passed;
}
} // namespace atlas
