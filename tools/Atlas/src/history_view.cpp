#include "app.hpp"
#include <algorithm>
#include <shellapi.h>
namespace atlas {
namespace {
std::string caption(const Json &v) {
    auto chapter=int(v["story_anchor"]["chapter"].num());
    return (chapter?"Глава "+std::to_string(chapter)+" · ":"")+v["label"].str("Версия")+" · "+v["id"].str();
}
double insertionOrder(const std::vector<Json> &list,const std::string &chain,int chapter,
                      const std::string &relative,int direction) {
    double end=0,center=0,lo=-1e20,hi=1e20;bool found=false;
    for(const auto &v:list)if(v["chain_id"].str()==chain && v["story_anchor"]["chapter"].num()==chapter) {
        end=std::max(end,v["story_order"].num());
        if(v["id"].str()==relative){center=v["story_order"].num();found=true;}
    }
    if(!found || !direction)return end+1024;
    for(const auto &v:list)if(v["chain_id"].str()==chain && v["story_anchor"]["chapter"].num()==chapter) {
        auto order=v["story_order"].num();
        if(order<center)lo=std::max(lo,order);
        if(order>center)hi=std::min(hi,order);
    }
    return direction<0?(lo<-1e19?center-1024:(lo+center)/2):(hi>1e19?center+1024:(hi+center)/2);
}
}
void App::refreshVersions(bool filters) {
    if(filters && chainBox) {
        syncHistory=true;
        chainIds={"","main","archive"};
        SendMessageW(chainBox,CB_RESETCONTENT,0,0);
        for(const auto &name:{L"Все цепочки",L"Основная история",L"Исходные карты"})
            SendMessageW(chainBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        try {
            auto catalog=map.timeline();
            auto mainName=wide(catalog["chains"]["main"]["name"].str("Основная история"));
            SendMessageW(chainBox,CB_DELETESTRING,1,0);
            SendMessageW(chainBox,CB_INSERTSTRING,1,reinterpret_cast<LPARAM>(mainName.c_str()));
            for(const auto &[id,c]:catalog["chains"].obj())if(id!="main" && id!="archive") {
                chainIds.push_back(id);auto text=wide(c["name"].str());
                SendMessageW(chainBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
            }
        }catch(...) {}
        int selectedChain=0;for(size_t i=0;i<chainIds.size();++i)if(chainIds[i]==historyChain)selectedChain=int(i);
        SendMessageW(chainBox,CB_SETCURSEL,selectedChain,0);
        SendMessageW(chapterBox,CB_RESETCONTENT,0,0);
        SendMessageW(chapterBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Все главы"));
        std::set<int> chapters;for(const auto &v:map.versions())if(v["story_anchor"]["chapter"].num()>0)chapters.insert(int(v["story_anchor"]["chapter"].num()));
        chapterIds={0};int chosen=0;
        for(int ch:chapters){chapterIds.push_back(ch);auto label=wide("Глава "+std::to_string(ch));SendMessageW(chapterBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(ch==historyChapter)chosen=int(chapterIds.size()-1);}
        SendMessageW(chapterBox,CB_SETCURSEL,chosen,0);
        SendMessageW(sortBox,CB_SETCURSEL,historyOrder=="saved"?1:historyOrder=="parents"?2:historyOrder=="date"?3:0,0);
        syncHistory=false;
    }
    auto all=map.versions();
    auto ordered=map.orderedVersions(historyOrder,historyChain,historyChapter);
    storyEvents=groupStoryEvents(ordered,all);
    if(!query.empty()) {
        auto lower=[](const std::string &s){auto w=wide(s);if(!w.empty())CharLowerBuffW(w.data(),DWORD(w.size()));return w;};
        auto q=lower(query);
        storyEvents.erase(std::remove_if(storyEvents.begin(),storyEvents.end(),[&](const auto &e){
            for(const auto &v:e.revisions)if(lower(v["label"].str()+" "+v["story_anchor"].dump(0)+" "+v["description"].str()).find(q)!=std::wstring::npos)return false;
            return true;
        }),storyEvents.end());
    }
    versions.clear();for(const auto &event:storyEvents)versions.push_back(event.moment);
    versionScroll=std::clamp(versionScroll,0,std::max(0,int(versions.size())-visibleEvents()));
    if(!selectedEvent()){comparison.reset();comparisonRenderer.reset();versionDetails=Json::object();}

}
void App::showVersion(const std::string &id,bool second) {
    bool hadView=bool(comparison),followScene=comparisonFocus.contains("MAPOBJ-CAMPAIGN-CURRENT-EVENT");
    auto snap=map.version(id);
    auto value=std::make_unique<Map>();value->doc=snap["document"];value->directory=map.directory;value->doc["_comparison_id"]=id;
    if(second){comparisonB=std::move(value);comparisonBRenderer.reset();}
    else {
        comparison=std::move(value);comparisonRenderer.reset();
        versionDetails=snap;versionDetails.obj().erase("document");
        for(const auto &v:map.versions())if(v["id"].str()==id){versionDetails=v;break;}
    }
    comparisonFocus.clear();diffVisible=false;diffRows=Json::array();diffScroll=0;
    detailScroll=0;
    if(!second)revealVersion(id);
    layout();if(!hadView)fit();
    if(followScene && comparison && comparison->doc["campaign_event"]["position"].isArray()) {
        auto at=point(comparison->doc["campaign_event"]["position"]);
        double w=(canvas.right-canvas.left)/(compareMode==1?2:1);
        offset={canvas.left+w/2-at.x*zoom,(canvas.top+canvas.bottom)/2-at.y*zoom};
        comparisonFocus={"MAPOBJ-CAMPAIGN-CURRENT-EVENT"};
    }
    invalidate();
}
void App::versionDialog() {
    if(map.directory.empty()){save();if(map.directory.empty())return;}
    const Json current=historyView&&comparison?versionDetails["story_anchor"]:map.doc["story_anchor"];
    std::vector<std::string> scenes{"Без отдельной сцены"},ids{""};
    for(const auto &e:campaign.entities)if(e.type=="scene"){scenes.push_back("Гл. "+e.chapter+" · "+e.name);ids.push_back(e.id);}
    auto r=form(window,"Новое событие на карте",{{"Название",""},
        {"Глава",std::to_string(int(current["chapter"].num(1)))},{"Дата / момент мира",current["story_date"].str()},
        {"Основание",scenes[0],scenes},{"Место в истории",comparison?"После выбранного события":"В конце главы",
            {"После выбранного события","Перед выбранным событием","В конце главы"}}},"Добавить событие");
    if(!r)return;if((*r)[0].empty())throw std::runtime_error("Укажи название события");
    int chapter=std::stoi((*r)[1]);if(chapter<1)throw std::runtime_error("Номер главы должен быть положительным");
    Json anchor=fields({{"event_id",newId("EVENT-")},{"chain_id","main"},{"chapter",chapter},
        {"story_date",(*r)[2].empty()?"Глава "+std::to_string(chapter):(*r)[2]}});
    size_t scene=size_t(std::find(scenes.begin(),scenes.end(),(*r)[3])-scenes.begin());
    if(scene && scene<ids.size()) {auto e=campaign.find(ids[scene]);anchor["scene_id"]=e->id;anchor["chapter"]=std::atoi(e->chapter.c_str());chapter=std::atoi(e->chapter.c_str());anchor["relation"]="after";anchor["branch"]=e->branch;anchor["evidence_ids"]=e->sources;}
    anchor["story_order"]=insertionOrder(map.versions(),"main",chapter,versionDetails["id"].str(),(*r)[4]=="После выбранного события"?1:(*r)[4]=="Перед выбранным событием"?-1:0);
    if((*r)[2].size()==10 && (*r)[2][4]=='-' && (*r)[2][7]=='-')anchor["sort_date"]=(*r)[2];
    auto original=map.doc;
    if(historyView&&comparison) {map.stashDraft("Перед новым событием");map.doc=comparison->doc;map.doc.obj().erase("_comparison_id");map.doc["parent_version"]=versionDetails["id"];}
    std::string id;
    try{id=map.snapshot((*r)[0],anchor,true);}catch(...){map.doc=original;throw;}
    dirty=false;history.clear();renderer.clear();historyChain="main";historyChapter=chapter;query.clear();
    if(searchBox)SetWindowTextW(searchBox,L"");refreshVersions(true);historyView=true;compareMode=0;showPanels=false;showVersion(id);
    status="Событие добавлено";noticeUntil=now()+2400;invalidate();
}
void App::editVersion() {
    const auto *event=selectedEvent();if(!event)return;
    auto revisions=event->revisions;auto original=event->moment;auto eventKey=event->key;
    auto catalog=map.timeline();auto expected=map.timelineHash;
    std::vector<std::string> names{"Сохранить место"},ids{""};
    for(const auto &other:storyEvents)if(other.key!=eventKey){names.push_back(caption(other.moment));ids.push_back(other.moment["id"].str());}
    auto r=form(window,"Дата и порядок события",{{"Название",original["label"].str()},
        {"Глава",std::to_string(int(original["story_anchor"]["chapter"].num(1)))},
        {"Дата / момент мира",original["story_anchor"]["story_date"].str()},
        {"Рядом с событием",names[0],names},{"Положение","После",{"До","После"}}},"Сохранить");
    if(!r)return;int chapter=std::stoi((*r)[1]);if(chapter<1 || (*r)[0].empty())throw std::runtime_error("Укажи название и положительный номер главы");
    auto chain=original["chain_id"].str();
    size_t neighbour=size_t(std::find(names.begin(),names.end(),(*r)[3])-names.begin());
    double order=neighbour?insertionOrder(map.versions(),chain,chapter,ids.at(neighbour),(*r)[4]=="До"?-1:1):original["story_order"].num();
    for(const auto &v:revisions) {
        auto id=v["id"].str();auto &entry=catalog["entries"][id];
        if(!entry.isObject())entry=fields({{"chain_id",v["chain_id"].str("archive")}});
        auto anchor=v["story_anchor"];if(!anchor.isObject())anchor=Json::object();
        anchor["event_id"]=eventKey;anchor["chapter"]=chapter;anchor["story_date"]=(*r)[2];anchor["story_order"]=order;
        if((*r)[2].size()==10&&(*r)[2][4]=='-'&&(*r)[2][7]=='-')anchor["sort_date"]=(*r)[2];else anchor.obj().erase("sort_date");
        if(auto scene=campaign.find(anchor["scene_id"].str());scene && std::atoi(scene->chapter.c_str())!=chapter) {
            anchor["reference_scene_id"]=anchor["scene_id"];anchor.obj().erase("scene_id");anchor.obj().erase("relation");anchor.obj().erase("branch");
        }
        entry["story_anchor"]=anchor;entry["story_order"]=order;entry["label"]=(*r)[0];
    }
    auto selectedId=versionDetails["id"].str();map.updateTimeline(catalog,expected);
    if(historyChapter && historyChapter!=chapter)historyChapter=chapter;
    refreshVersions(true);showVersion(selectedId);
}
void App::newChain() {
    if(map.directory.empty()){save();if(map.directory.empty())return;}
    auto result=form(window,"Новая цепочка",{{"Название","Мой вариант"}},"Создать от открытой версии");
    if(!result)return;
    auto catalog=map.timeline();auto id=newId("CHAIN-");
    catalog["chains"][id]=fields({{"name",(*result)[0]},{"kind","variant"}});
    map.updateTimeline(catalog);
    if(comparison)map.restore(versionDetails["id"].str());else map.stashDraft("Перед созданием цепочки");
    auto anchor=map.doc["story_anchor"];if(!anchor.isObject())anchor=fields({{"chapter",1}});
    anchor["chain_id"]=id;anchor["story_order"]=1024;
    auto version=map.snapshot("Начало: "+(*result)[0],anchor);
    historyChain=id;historyChapter=0;dirty=false;history.clear();refreshVersions(true);showVersion(version);
}
void App::draftsDialog() {
    auto catalog=map.timeline();std::vector<std::string> names,ids;
    for(const auto &[id,d]:catalog["drafts"].obj()){ids.push_back(id);names.push_back(d["label"].str()+" · "+d["recorded_at"].str());}
    if(names.empty()){status="Сохранённых черновиков пока нет";noticeUntil=now()+3000;invalidate();return;}
    auto result=form(window,"Открыть черновик",{{"Черновик",names.back(),names}},"Открыть");
    if(!result)return;
    auto at=size_t(std::find(names.begin(),names.end(),(*result)[0])-names.begin());
    map.restoreDraft(ids.at(at));history.clear();dirty=true;++editSerial;historyView=false;compareMode=0;
    selected.clear();renderer.clear();layout();fit();updateSelection();
}
void App::recoveryDialog() {
    auto list=map.recoveries();std::vector<std::string> names;
    for(const auto &v:list)names.push_back(v["name"].str()+" · "+v["recorded_at"].str()+(v["base_matches"].boolean()?"":" · другая основа"));
    if(names.empty()){status="Автосохранений нет";noticeUntil=now()+2500;invalidate();return;}
    auto r=form(window,"Восстановление работы",{{"Сессия",names[0],names}},"Восстановить");
    if(!r)return;
    auto at=size_t(std::find(names.begin(),names.end(),(*r)[0])-names.begin());
    if(!list[at]["base_matches"].boolean()) {
        auto folder=chooseFolder(window,"Выбери папку для отдельной восстановленной карты");if(!folder)return;
        auto state=Json::parse(readText(pathOf(list[at]["path"].str())))["document"];
        Map restored;restored.directory=map.directory;restored.doc=state;
        restored.save(*folder/pathOf("Восстановление-"+newId("")));
        map=std::move(restored);
    } else {
        if(!map.directory.empty())map.stashDraft("Перед восстановлением сессии");
        map.recoverSession(list[at]["id"].str());
    }
    history.clear();dirty=true;++editSerial;historyView=false;compareMode=0;renderer.clear();layout();fit();updateSelection();
}
void App::chooseComparison(bool second) {
    auto list=map.orderedVersions();std::vector<std::string> names,ids;
    if(second){names.push_back("Рабочий черновик");ids.push_back("");}
    for(const auto &v:list)if(v["status"].str()!="damaged"){names.push_back(caption(v));ids.push_back(v["id"].str());}
    if(names.empty())return;
    auto r=form(window,second?"Правая карта (B)":"Левая карта (A)",{{"Версия",names[0],names}},"Сравнить");
    if(!r)return;
    auto at=size_t(std::find(names.begin(),names.end(),(*r)[0])-names.begin());
    if(second && ids[at].empty()){comparisonB.reset();comparisonBRenderer.reset();}
    else showVersion(ids[at],second);
    historyView=true;if(!compareMode)compareMode=1;buildDiff();layout();fit();
}
void App::buildDiff() {
    diffRows=Json::array();diffScroll=0;diffVisible=true;
    if(!comparison)return;
    Map &other=comparisonB?*comparisonB:map;
    auto diff=other.diff(comparison->doc);
    std::map<std::string,std::string> features;std::set<std::string> changedNodes,changedArcs;
    for(const auto &c:diff["changes"].arr()) {
        auto id=c["id"].str(),kind=c["collection"].str();
        if(kind=="features")features[id]=c["before"].null()?"Добавлен":c["after"].null()?"Удалён":"Изменены свойства";
        else if(kind=="nodes")changedNodes.insert(id);
        else if(kind=="arcs")changedArcs.insert(id);
        else if(c["field"].isString()) {
            auto field=c["field"].str();
            if(field=="parent_version" || field=="story_anchor" || field=="status" || field=="next_z" || field=="land_state_separated")continue;
            std::map<std::string,std::string> names{{"background","Фон карты"},{"layers","Слои"},{"name","Название карты"},
                {"campaign","Параметры кампании"},{"campaign_event","Сюжетное событие"},{"width","Ширина карты"},
                {"height","Высота карты"},{"style","Оформление карты"},{"reconstruction","Основа карты"},{"import","Исходное изображение"}};
            diffRows.push(fields({{"id",""},{"name",names.contains(field)?names[field]:"Дополнительные данные карты"},{"change","Изменено"}}));
        }else if(kind=="symbols") {
            auto name=c["after"]["name"].str(c["before"]["name"].str(id));
            diffRows.push(fields({{"id",""},{"name","Шаблон: "+name},{"change",c["before"].null()?"Добавлен":c["after"].null()?"Удалён":"Изменён"}}));
        }
    }
    for(Map *source:{comparison.get(),&other})for(const auto &[id,f]:source->doc["features"].obj()) {
        bool affected=false;
        for(const auto &n:source->featureNodes(f))if(changedNodes.contains(n)){affected=true;break;}
        auto scan=[&](const Json &refs){if(refs.isArray())for(const auto &a:refs.arr())if(changedArcs.contains(a["id"].str()))affected=true;};
        scan(f["arcs"]);if(f["rings"].isArray())for(const auto &ring:f["rings"].arr())scan(ring);
        if(affected && !features.contains(id))features[id]="Изменена геометрия";
    }
    for(const auto &[id,change]:features) {
        const auto &f=other.doc["features"].contains(id)?other.doc["features"][id]:comparison->doc["features"][id];
        diffRows.push(fields({{"id",id},{"name",f["name"].str(id)},{"change",change}}));
    }
    invalidate();
}
void App::focusDiff(size_t index) {
    if(index>=diffRows.size() || !comparison)return;
    auto id=diffRows[index]["id"].str();if(id.empty())return;
    Map &source=(comparisonB?comparisonB->doc:map.doc)["features"].contains(id)?(comparisonB?*comparisonB:map):*comparison;
    auto &f=source.doc["features"][id];auto p=source.anchor(f);
    double x0=p.x-90,x1=p.x+90,y0=p.y-90,y1=p.y+90;
    for(const auto &ring:source.paths(f))for(auto q:ring){x0=std::min(x0,q.x);x1=std::max(x1,q.x);y0=std::min(y0,q.y);y1=std::max(y1,q.y);}
    double w=(canvas.right-canvas.left)/(compareMode==1?2:1);
    zoom=std::clamp(std::min((w-50)/(x1-x0),(canvas.bottom-canvas.top-50)/(y1-y0)),.01,24.0);
    offset={canvas.left+w/2-(x0+x1)*zoom/2,(canvas.top+canvas.bottom)/2-(y0+y1)*zoom/2};
    comparisonFocus={id};invalidate();
}
void App::drawHistoryMaps(Painter &p) {
    if(!comparison)return;
    if(!comparisonRenderer)comparisonRenderer=std::make_unique<MapRenderer>(renderer.factory.get());
    auto render=[&](Map &m,MapRenderer &r,D2D1_RECT_F view,Point at) {
        std::set<std::string> focus;for(const auto &id:comparisonFocus)if(m.doc["features"].contains(id))focus.insert(id);
        if(window)r.drawResponsive(m,p.target,view,zoom,at,focus,false,panning || now()<cameraMovingUntil,window);
        else r.drawInteractive(m,p.target,view,zoom,at,focus);
    };
    if(!compareMode){render(*comparison,*comparisonRenderer,canvas,offset);return;}
    if(!comparisonBRenderer)comparisonBRenderer=std::make_unique<MapRenderer>(renderer.factory.get());
    Map &other=comparisonB?*comparisonB:map;
    if(compareMode==1) {
        float mid=(canvas.left+canvas.right)/2;auto a=canvas,b=canvas;a.right=mid-2;b.left=mid+2;
        render(*comparison,*comparisonRenderer,a,offset);
        render(other,*comparisonBRenderer,b,{offset.x+(canvas.right-canvas.left)/2,offset.y});
        p.line({mid,canvas.top},{mid,canvas.bottom},"#778D87",2);
    }else {
        render(*comparison,*comparisonRenderer,canvas,offset);
        Com<ID2D1Layer> layer;check(p.target->CreateLayer(nullptr,layer.put()),"Comparison layer");
        p.target->PushLayer(D2D1::LayerParameters(canvas,nullptr,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,D2D1::Matrix3x2F::Identity(),.5f),layer.get());
        render(other,*comparisonBRenderer,canvas,offset);p.target->PopLayer();
    }
}
} // namespace atlas
