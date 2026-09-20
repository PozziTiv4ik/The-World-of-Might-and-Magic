#include "app.hpp"
#include <algorithm>
#include <chrono>

namespace atlas {
namespace {
constexpr auto paper="#FAFBF8", ink="#233432", muted="#758480", teal="#22685F", rule="#D8E1DC";
Image thumbnail(const fs::path &directory,const std::string &id) {
    struct Apartment {
        HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
    } apartment;
    Map reader;reader.directory=directory;
    Map value;value.directory=directory;value.doc=reader.version(id)["document"];
    MapRenderer renderer;renderer.displayLabels=false;
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.put())),"Thumbnail WIC");
    Com<IWICBitmap> bitmap;
    check(wic->CreateBitmap(360,220,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.put()),"Thumbnail pixels");
    Com<ID2D1RenderTarget> rt;
    check(renderer.factory->CreateWicBitmapRenderTarget(bitmap.get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE),rt.put()),"Thumbnail target");
    rt->BeginDraw();rt->Clear(color(value.doc["background"].str("#E0EBE6")));
    double z=std::min(360/value.doc["width"].num(4000),220/value.doc["height"].num(3000));
    Point offset{(360-value.doc["width"].num()*z)/2,(220-value.doc["height"].num()*z)/2};
    const auto &event=static_cast<const Json&>(value.doc)["campaign_event"];
    if(event["position"].isArray()) {
        auto focus=point(event["position"]);z=360./1200.;offset={180-focus.x*z,110-focus.y*z};
    }
    renderer.draw(value,rt.get(),{0,0,360,220},z,offset,{},false,false);
    check(rt->EndDraw(),"Thumbnail render");
    Image result(360,220);check(bitmap->CopyPixels(nullptr,1440,UINT(result.bgra.size()),result.bgra.data()),"Thumbnail copy");
    return result;
}
std::string eventTitle(const Json &v) {
    auto title=v["label"].str("Состояние карты");
    auto prefix=v["story_anchor"]["branch"].str()+" · ";
    if(title.starts_with(prefix))title=title.substr(prefix.size());
    return title;
}
}

void App::computeLayout() {
    left=right=0;canvas={0,0,width,height};drawerArea={};
    if(historyView && compareMode) {
        canvas.top=106;
        if(showPanels){right=306;drawerArea={width-322,118,width-16,height-76};}
    } else if(!historyView && showPanels) {
        if(panel==5){right=306;drawerArea={width-322,78,width-16,height-76};}
        else {left=300;drawerArea={78,78,378,height-76};}
    }
    float footer=height-(height<650?185.f:234.f);
    bool timelineResized=std::abs((timelineArea.right-timelineArea.left)-(width-96))>.1f;
    timelineArea={48,94,width-48,footer-12};
    versionScroll=std::clamp(versionScroll,0,std::max(0,int(storyEvents.size())-visibleEvents()));
    if(timelineResized && historyView && !compareMode)revealVersion(versionDetails["id"].str());
}
int App::visibleEvents() const {return std::max(2,int((width-112)/eventSpacing));}
const StoryEvent *App::selectedEvent() const {
    auto id=versionDetails["id"].str();
    for(const auto &event:storyEvents)for(const auto &v:event.revisions)if(v["id"].str()==id)return &event;
    return nullptr;
}
void App::revealVersion(const std::string &id) {
    for(size_t i=0;i<storyEvents.size();++i)for(const auto &v:storyEvents[i].revisions)if(v["id"].str()==id) {
        if(int(i)<versionScroll)versionScroll=int(i);
        if(int(i)>=versionScroll+visibleEvents())versionScroll=int(i)-visibleEvents()+1;
        versionScroll=std::clamp(versionScroll,0,std::max(0,int(storyEvents.size())-visibleEvents()));return;
    }
}
void App::paintThumbnail(Painter &p,const Json &v,D2D1_RECT_F r) {
    auto id=v["id"].str(),key=pathText(map.directory)+":"+id;
    auto &entry=thumbnails[key];if(!entry)entry=std::make_unique<Thumbnail>();
    if(entry->pending.valid() && entry->pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try{entry->image=std::make_shared<Image>(entry->pending.get());}catch(const std::exception &e){entry->error=e.what();}
    }
    if(!entry->image && !entry->pending.valid() && entry->error.empty()) {
        if(!window) {try{entry->image=std::make_shared<Image>(thumbnail(map.directory,id));}catch(const std::exception &e){entry->error=e.what();}}
        else {
            size_t active=0;for(const auto &[k,t]:thumbnails)if(t->pending.valid())++active;
            if(active<2)entry->pending=std::async(std::launch::async,thumbnail,map.directory,id);
        }
    }
    if(entry->image) {
        if(entry->owner!=p.target){entry->bitmap.reset();entry->owner=p.target;}
        if(!entry->bitmap) {
            auto &im=*entry->image;
            check(p.target->CreateBitmap(D2D1::SizeU(im.width,im.height),im.bgra.data(),im.width*4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),entry->bitmap.put()),"Thumbnail bitmap");
        }
        p.target->DrawBitmap(entry->bitmap.get(),r,1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        p.rounded(r,"#E8EFE9",6);p.text(entry->error.empty()?"Загрузка…":"Нет предпросмотра",r,11,muted,false,true);
        if(window && entry->error.empty())if(window)SetTimer(window,7,60,nullptr);
    }
}

void App::paintHistoryScreen(Painter &p) {
    p.fill({0,0,width,height},paper);
    button(p,{20,18,144,56},"← К карте",EditorView,"",false,true);
    p.line({159,22},{159,52},rule);
    p.text(width<1000?"История":"История карты",{181,17,width-525,58},width<1000?19.f:22.f,ink,true);
    iconButton(p,{width-502,18,width-464,56},"search","Поиск событий · Ctrl+F",HistorySearch,"",historySearch);
    button(p,{width-452,18,width-302,56},historyChapter?"Глава "+std::to_string(historyChapter):"Все главы",HistoryFilter,"",false,true);
    button(p,{width-292,18,width-158,56},"+ Событие",NewEvent,"",true);
    iconButton(p,{width-146,18,width-108,56},"gear","Порядок и подборки",HistoryOptions);
    iconButton(p,{width-99,18,width-63,56},"minus","Мельче события",HistoryScale,"out");
    iconButton(p,{width-56,18,width-20,56},"plus","Крупнее события",HistoryScale,"in");
    if(historySearch && !searchBox)p.text(query.empty()?"Поиск по событию, месту, дате…":query,{24,70,width-120,101},13,muted);
    if(historyChain!="main")p.text(historyChain=="chapters"?"Итоги глав":historyChain=="archive"?"Исходные карты":"Архив редакций",{24,68,width-150,94},12,muted);
    float footer=height-(height<650?185.f:234.f),available=footer-94;
    float axis=94+available*.57f,thumbHeight=std::clamp(available*.3f,56.f,110.f);
    int count=visibleEvents(),end=std::min(int(storyEvents.size()),versionScroll+count);
    float step=(width-128)/count,thumbWidth=std::min(180.f,step-34),start=64+step/2;
    const auto *chosen=selectedEvent();
    if(storyEvents.empty()) {
        p.text(query.empty()?"Здесь появится история карты":"Ничего не найдено",{80,axis-65,width-80,axis-20},22,ink,true,true);
        p.text(query.empty()?"Добавь первое событие":"Измени запрос или выбери другую главу",{80,axis-14,width-80,axis+30},14,muted,false,true);
        if(!query.empty())button(p,{width/2-84,axis+44,width/2+84,axis+82},"Очистить поиск",HistorySearch,"clear");
    } else {
        float last=start+(end-versionScroll-1)*step;
        p.line({std::max(48.f,start-step/2),axis},{std::min(width-48,last+step/2),axis},teal,1.5f);
        for(int i=versionScroll;i<end;) {
            int chapter=int(storyEvents[i].moment["story_anchor"]["chapter"].num()),j=i+1;
            while(j<end && int(storyEvents[j].moment["story_anchor"]["chapter"].num())==chapter)++j;
            float x=start+(i-versionScroll)*step-thumbWidth/2,x2=start+(j-versionScroll-1)*step+thumbWidth/2,cy=axis-thumbHeight-70;
            p.text(chapter?"Глава "+std::to_string(chapter):"Исходная карта",{x,cy,x2,cy+31},15,ink,false,true);
            p.line({x,cy+37},{x2,cy+37},rule);i=j;
        }
        for(int i=versionScroll;i<end;++i) {
            const auto &event=storyEvents[i];const auto &v=event.moment;
            bool active=chosen && chosen->key==event.key;
            float x=start+(i-versionScroll)*step,top=axis-thumbHeight-28;
            D2D1_RECT_F image{x-thumbWidth/2,top,x+thumbWidth/2,top+thumbHeight};
            // The selected event's preview follows its selected editing revision.
            paintThumbnail(p,active?versionDetails:v,image);
            if(active)p.rect({image.left-3,image.top-3,image.right+3,image.bottom+3},teal,2);
            p.line({x,image.bottom+4},{x,axis-10},rule,1);
            p.circle({x,axis},active?14.f:10.f,teal,false);p.circle({x,axis},active?8.f:8.2f,active?teal:paper);
            p.text(eventTitle(v),{x-thumbWidth/2,axis+24,x+thumbWidth/2,axis+88},13,ink,active);
            auto date=v["story_anchor"]["story_date"].str();
            hits.push_back({{x-step/2+8,top-3,x+step/2-8,std::min(footer-47,axis+100)},SelectVersion,v["id"].str(),v["label"].str()+" · "+date});
            if(active && i+1<end)iconButton(p,{x+step/2-15,axis-15,x+step/2+15,axis+15},"plus","Вставить событие после выбранного",NewEvent,"after");
        }
        iconButton(p,{16,axis-19,53,axis+19},"back","Предыдущие события · Page Up",HistoryPage,"previous");
        iconButton(p,{width-53,axis-19,width-16,axis+19},"next","Следующие события · Page Down",HistoryPage,"next");
        p.text(std::to_string(versionScroll+1)+"–"+std::to_string(end)+" из "+std::to_string(storyEvents.size())+" событий",{width/2-144,footer-48,width/2+144,footer-18},12,muted,false,true);
    }
    p.line({24,footer},{width-24,footer},rule);
    if(chosen) {
        float titleEnd=width*.33f,revisionLeft=titleEnd+28,revisionRight=width-400;
        if(width<1150){titleEnd=width*.3f;revisionLeft=titleEnd+24;revisionRight=width-282;}
        p.text("Выбрано",{32,footer+14,titleEnd-12,footer+38},12,muted);
        p.text(eventTitle(chosen->moment),{32,footer+42,titleEnd-16,footer+110},width<1000?14.f:width<1250?16.f:18.f,ink,true);
        auto chapter=int(chosen->moment["story_anchor"]["chapter"].num());
        p.text(chapter?"Глава "+std::to_string(chapter):"Исходная карта",{32,footer+112,titleEnd-16,footer+137},12,muted);
        button(p,{28,footer+141,titleEnd-16,footer+174},"Дата и порядок",EditVersion,"",false,true);
        p.text("Редакции карты",{revisionLeft,footer+14,revisionRight,footer+38},12,muted);
        int selectedIndex=0,total=int(chosen->revisions.size());
        for(int i=0;i<total;++i)if(chosen->revisions[i]["id"].str()==versionDetails["id"].str())selectedIndex=i;
        int from=std::clamp(selectedIndex-1,0,std::max(0,total-3)),to=std::min(total,from+3);
        float space=(revisionRight-revisionLeft-24)/std::max(1,to-from),x0=revisionLeft+space/2,vy=footer+76;
        if(to-from>1)p.line({x0,vy},{x0+(to-from-1)*space,vy},rule,1.5f);
        for(int i=from;i<to;++i) {
            float x=x0+(i-from)*space;bool active=i==selectedIndex;
            p.circle({x,vy},active?13.f:9.f,active?teal:muted,false);p.circle({x,vy},7,active?teal:paper);
            p.text("v"+std::to_string(i+1),{x-22,vy+16,x+22,vy+42},13,active?teal:muted,active,true);
            hits.push_back({{x-space/2,vy-19,x+space/2,vy+47},SelectRevision,chosen->revisions[i]["id"].str(),chosen->revisions[i]["recorded_at"].str()});
        }
        if(total>3)button(p,{revisionLeft,footer+127,revisionRight-15,footer+157},"Все редакции · "+std::to_string(total),SelectRevision,"choose",false,true);
        else p.text(versionDetails["id"].str()==chosen->moment["id"].str()?"Текущая редакция":"Предыдущая редакция",{revisionLeft,footer+127,revisionRight,footer+158},11,muted);
        float actions=width<1150?width-267:width-385;
        button(p,{actions,footer+49,actions+115,footer+86},"Сравнить",CompareRevisions);
        button(p,{actions,footer+97,actions+115,footer+134},"+ Редакция",NewRevision);
        button(p,{width-150,footer+65,width-24,footer+115},"Открыть карту →",OpenMoment,"",true);
        if(height>=650)p.text(versionDetails["story_anchor"]["story_date"].str(),{32,footer+178,width-230,footer+214},12,muted);
    }
}

void App::openMoment() {
    if(!comparison)return;
    auto id=versionDetails["id"].str();
    if(map.doc["parent_version"].str()!=id) {
        map.restore(id);history.clear();
        // Apply editable chronology metadata without rewriting the immutable snapshot.
        map.doc["story_anchor"]=versionDetails["story_anchor"];
        map.save();dirty=false;autosavedSerial=editSerial;
        renderer.clear();editorCameraSaved=false;
    }
    historyView=false;compareMode=0;showPanels=false;panel=0;selected.clear();activeBorder.clear();
    layout();if(editorCameraSaved){zoom=editorZoom;offset=editorOffset;}else fit();
    updateSelection();invalidate();
}
void App::revisionDialog(bool askName) {
    if(historyView){openMoment();}
    auto parent=map.doc["parent_version"].str();
    if(parent.empty()){versionDialog();return;}
    Json base;for(const auto &v:map.versions())if(v["id"].str()==parent){base=v;break;}
    if(!base.isObject()){versionDialog();return;}
    std::string note="Правки карты";
    if(askName) {
        auto result=form(window,"Сохранить редакцию карты",{{"Комментарий",note}},"Сохранить редакцию");
        if(!result)return;note=(*result)[0];
    }
    auto anchor=base["story_anchor"];
    if(!anchor.isObject())anchor=Json::object();
    anchor["event_id"]=storyEventKey(base);anchor["chain_id"]=base["chain_id"].str("main");
    if(anchor["chain_id"].str()=="editions")anchor["chain_id"]="main";
    anchor["story_order"]=base["story_order"];anchor["revision_note"]=note;
    auto id=map.snapshot(base["label"].str(),anchor,true);
    dirty=false;autosavedSerial=editSerial;historyChain=anchor["chain_id"].str();refreshVersions(true);
    // Keep the editor open after saving, while history now knows the new revision.
    versionDetails=map.version(id);versionDetails.obj().erase("document");
    for(const auto &v:map.versions())if(v["id"].str()==id)versionDetails=v;
    comparison.reset();comparisonRenderer.reset();status="Редакция сохранена";noticeUntil=now()+2400;
    SetWindowTextW(window,wide("АТЛАС — "+map.doc["name"].str()).c_str());invalidate();
}
void App::compareRevisions() {
    const auto *event=selectedEvent();if(!event || event->revisions.empty())return;
    auto list=event->revisions;auto selectedId=versionDetails["id"].str();
    if(list.size()<2){chooseComparison(true);return;}
    size_t current=0;for(size_t i=0;i<list.size();++i)if(list[i]["id"].str()==selectedId)current=i;
    auto a=list[current?current-1:0]["id"].str(),b=list[current?current:1]["id"].str();
    showVersion(a);showVersion(b,true);historyView=true;compareMode=1;showPanels=false;buildDiff();layout();fit();
}
void App::historyMenu(int cmd) {
    HMENU menu=CreatePopupMenu();std::vector<std::pair<int,std::string>> values;
    auto add=[&](const std::string &label,int command,const std::string &data="",bool checked=false){
        values.push_back({command,data});auto title=wide(label);AppendMenuW(menu,MF_STRING|(checked?MF_CHECKED:0),4000+values.size(),title.c_str());};
    if(cmd==HistoryFilter) {
        add("Все главы",HistoryFilter,"0",historyChapter==0);
        std::set<int> chapters;for(const auto &v:map.versions())if(v["story_anchor"]["chapter"].num()>0)chapters.insert(int(v["story_anchor"]["chapter"].num()));
        for(int ch:chapters)add("Глава "+std::to_string(ch),HistoryFilter,std::to_string(ch),historyChapter==ch);
    } else {
        add("Порядок сюжета",HistorySort,"story",historyOrder=="story");
        add("По дате мира",HistorySort,"date",historyOrder=="date");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        add("События",HistoryCollection,"main",historyChain=="main");
        add("Итоги глав",HistoryCollection,"chapters",historyChain=="chapters");
        add("Исходные карты",HistoryCollection,"archive",historyChain=="archive");
        add("Архив редакций",HistoryCollection,"editions",historyChain=="editions");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);add("Открыть исходную сцену",OpenVersionSource);
        add("Именованные черновики",DraftsDialog);
    }
    POINT at;GetCursorPos(&at);int chosen=pickMenu(window,menu);DestroyMenu(menu);
    if(chosen>4000 && chosen<=4000+int(values.size())){auto [c,d]=values[chosen-4001];command(c,d);}
}
void App::toggleFullscreen() {
    if(!window)return;
    if(!fullscreen) {
        GetWindowPlacement(window,&normalPlacement);
        MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
        SetWindowLongPtrW(window,GWL_STYLE,GetWindowLongPtrW(window,GWL_STYLE)&~WS_OVERLAPPEDWINDOW);
        SetWindowPos(window,nullptr,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,
            monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_NOZORDER|SWP_FRAMECHANGED);fullscreen=true;
    } else {
        SetWindowLongPtrW(window,GWL_STYLE,GetWindowLongPtrW(window,GWL_STYLE)|WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(window,&normalPlacement);SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);fullscreen=false;
    }
    layout();
}
} // namespace atlas
