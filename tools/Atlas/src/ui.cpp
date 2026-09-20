#include "app.hpp"
#include <algorithm>
namespace atlas {
static bool over(D2D1_RECT_F r, Point p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
static bool matches(const std::string &s, const std::string &q) {
    auto a = wide(s), b = wide(q);
    if (!a.empty())
        CharLowerBuffW(a.data(), DWORD(a.size()));
    if (!b.empty())
        CharLowerBuffW(b.data(), DWORD(b.size()));
    return a.find(b) != std::wstring::npos;
}
static constexpr auto surface = "#FAFBF8", ink = "#233432", muted = "#758480", accent = "#22685F";
static void dock(Painter &p, D2D1_RECT_F r) {
    auto s = r;
    s.left += 2;
    s.right += 2;
    s.top += 3;
    s.bottom += 3;
    p.rounded(s, "#C7D4CE", 8);
    p.rounded(r, surface, 7);
}
// Deterministic, font-independent line icons.
static void icon(Painter &p, const std::string &n, D2D1_RECT_F r, const std::string &c) {
    double x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    auto line = [&](double a, double b, double d, double e) {
        p.line({x + a, y + b}, {x + d, y + e}, c, 1.6f);
    };
    auto path = [&](std::initializer_list<Point> ps) {
        bool first = true;
        Point old{};
        for (auto q : ps) {
            if (!first)
                line(old.x, old.y, q.x, q.y);
            old = q;
            first = false;
        }
    };
    auto circle = [&](double a, double b, float rad) { p.circle({x + a, y + b}, rad, c, false); };
    if (n == "arrow")
        path({{-7, -11}, {9, 1}, {2, 3}, {-1, 11}, {-7, -11}});
    else if (n == "hand")
        path({{-8, 1},  {-8, -4}, {-5, -5}, {-5, 2},  {-5, -9}, {-2, -10}, {-2, 1}, {-2, -12},
              {1, -12}, {1, 1},   {1, -9},  {4, -9},  {4, 2},   {4, -5},   {7, -5}, {7, 4},
              {4, 10},  {-3, 10}, {-8, 4},  {-11, 0}, {-8, -2}, {-5, 2}});
    else if (n == "node") {
        path({{-9, 7}, {-2, -6}, {9, -2}});
        circle(-9, 7, 2.6f);
        circle(-2, -6, 2.6f);
        circle(9, -2, 2.6f);
    } else if (n == "pen") {
        path({{-9, 9}, {-6, 2}, {6, -10}, {10, -6}, {-2, 6}, {-9, 9}});
        line(4, -8, 8, -4);
    } else if (n == "land") {
        path({{-11, 7},
              {-9, 1},
              {-5, -1},
              {-4, -7},
              {1, -10},
              {5, -5},
              {9, -3},
              {11, 3},
              {7, 8},
              {-1, 10},
              {-11, 7}});
        line(-8, 12, 8, 12);
    } else if (n == "flag") {
        line(-8, -10, -8, 11);
        path({{-8, -9}, {2, -9}, {2, -4}, {10, -4}, {6, 2}, {-8, 2}});
    } else if (n == "mountain") {
        path({{-11, 8}, {-4, -8}, {0, 0}, {4, -11}, {12, 8}, {-11, 8}});
        path({{-6, -4}, {-3, 0}, {-1, -2}});
    } else if (n == "castle") {
        path({{-10, 10}, {-10, -3}, {-7, -3}, {-7, 0}, {-4, 0}, {-4, -3}, {-1, -3}, {-1, 10}});
        path({{1, 10}, {1, -7}, {4, -11}, {7, -7}, {7, 10}});
        line(-12, 10, 11, 10);
    } else if (n == "text") {
        line(-8, -9, 8, -9);
        line(0, -9, 0, 10);
        line(-4, 10, 4, 10);
    } else if (n == "ruler") {
        path({{-11, 5}, {5, -11}, {11, -5}, {-5, 11}, {-11, 5}});
        for (int i = 0; i < 4; i++)
            line(-6 + i * 4, 2 - i * 4, -3 + i * 4, 5 - i * 4);
    } else if (n == "menu") {
        line(-8, -6, 8, -6);
        line(-8, 0, 8, 0);
        line(-8, 6, 8, 6);
    } else if (n == "more") {
        for (int i = -1; i <= 1; i++)
            p.circle({x + i * 6, y}, 1.5f, c);
    } else if (n == "undo" || n == "redo") {
        double s = n == "redo" ? -1 : 1;
        line(-9 * s, -3, 3 * s, -3);
        path({{3 * s, -3}, {8 * s, 0}, {8 * s, 5}, {4 * s, 8}, {-2 * s, 8}});
        line(-9 * s, -3, -4 * s, -8);
        line(-9 * s, -3, -4 * s, 2);
    } else if (n == "search") {
        circle(-2, -2, 6);
        line(3, 3, 10, 10);
    } else if (n == "save") {
        path({{-9, -10}, {6, -10}, {10, -6}, {10, 10}, {-9, 10}, {-9, -10}});
        path({{-4, -10}, {-4, -3}, {5, -3}, {5, -10}});
        path({{-4, 10}, {-4, 3}, {5, 3}, {5, 10}});
    } else if (n == "export") {
        path({{-9, 0}, {-9, 9}, {9, 9}, {9, 0}});
        line(0, 3, 0, -10);
        line(0, -10, -5, -5);
        line(0, -10, 5, -5);
    } else if (n == "layers") {
        path({{-11, -4}, {0, -10}, {11, -4}, {0, 2}, {-11, -4}});
        path({{-11, 2}, {0, 8}, {11, 2}});
        path({{-11, 7}, {0, 13}, {11, 7}});
    } else if (n == "book")
        path({{0, 10},
              {0, -8},
              {-8, -10},
              {-11, -8},
              {-11, 8},
              {-7, 7},
              {0, 10},
              {7, 7},
              {11, 8},
              {11, -8},
              {8, -10},
              {0, -8}});
    else if (n == "gear") {
        circle(0, 0, 6);
        circle(0, 0, 2);
        for (int i = 0; i < 8; i++) {
            double a = i * 3.14159265 / 4;
            line(7 * cos(a), 7 * sin(a), 10 * cos(a), 10 * sin(a));
        }
    } else if (n == "x") {
        line(-6, -6, 6, 6);
        line(-6, 6, 6, -6);
    } else if (n == "plus") {
        line(-7, 0, 7, 0);
        line(0, -7, 0, 7);
    } else if (n == "minus")
        line(-7, 0, 7, 0);
    else if (n == "fit") {
        for (int a : {-1, 1})
            for (int b : {-1, 1}) {
                line(a * 9, b * 3, a * 9, b * 9);
                line(a * 9, b * 9, a * 3, b * 9);
            }
    } else if (n == "sea") {
        for (int y : {-5, 2, 9})
            path({{-11, double(y)},
                  {-7, double(y - 2)},
                  {-2, double(y + 1)},
                  {3, double(y - 2)},
                  {8, double(y + 1)},
                  {11, double(y)}});
    } else if (n == "history") {
        circle(0, 0, 9);
        line(0, -5, 0, 0);
        line(0, 0, 5, 3);
    } else if (n == "eye" || n == "eye_off") {
        path({{-11, 0}, {-5, -6}, {5, -6}, {11, 0}, {5, 6}, {-5, 6}, {-11, 0}});
        circle(0, 0, 3);
        if (n == "eye_off")
            line(-10, -10, 10, 10);
    } else if (n == "unlock") {
        path({{-7, -1}, {7, -1}, {7, 10}, {-7, 10}, {-7, -1}});
        path({{-4, -1}, {-4, -7}, {-1, -10}, {3, -10}, {7, -7}});
        line(0, 3, 0, 6);
    } else if (n == "lock") {
        path({{-7, -1}, {7, -1}, {7, 10}, {-7, 10}, {-7, -1}});
        path({{-4, -1}, {-4, -7}, {-1, -10}, {2, -10}, {5, -7}, {5, -1}});
        line(0, 3, 0, 6);
    } else if (n == "move") {
        line(-10, 0, 10, 0);
        line(0, -10, 0, 10);
        path({{-6, -4}, {-10, 0}, {-6, 4}});
        path({{6, -4}, {10, 0}, {6, 4}});
        path({{-4, -6}, {0, -10}, {4, -6}});
        path({{-4, 6}, {0, 10}, {4, 6}});
    } else if (n == "link") {
        path({{-1, -7}, {3, -11}, {9, -11}, {11, -8}, {11, -3}, {7, 1}});
        path({{1, 7}, {-3, 11}, {-9, 11}, {-11, 8}, {-11, 3}, {-7, -1}});
        line(-5, 5, 5, -5);
    } else if (n == "trash") {
        path({{-7, -5}, {-6, 10}, {6, 10}, {7, -5}});
        line(-10, -6, 10, -6);
        path({{-4, -6}, {-4, -10}, {4, -10}, {4, -6}});
        line(-2, -2, -2, 6);
        line(2, -2, 2, 6);
    } else if (n == "back" || n == "next") {
        double s=n=="back"?-1:1;
        line(-9,0,9,0);line(s*9,0,s*3,-6);line(s*9,0,s*3,6);
    } else if (n == "up" || n == "down") {
        double s = n == "up" ? 1 : -1;
        line(0, -9 * s, 0, 9 * s);
        line(0, -9 * s, -5, -4 * s);
        line(0, -9 * s, 5, -4 * s);
    } else if (n == "color")
        p.circle({x, y}, 8, c);
    else {
        circle(0, 0, 9);
        p.text("?", r, 15, c, false, true);
    }
}
void App::iconButton(Painter &p, D2D1_RECT_F r, const std::string &glyph, const std::string &tip, int cmd,
                     const std::string &data, bool active) {
    if (active || over(r, mouse))
        p.rounded(r, active ? "#DDEDE7" : "#EAF0EB", 5);
    icon(p, glyph, r, active ? accent : ink);
    hits.push_back({r, cmd, data, tip});
}
void App::button(Painter &p, D2D1_RECT_F r, const std::string &title, int cmd, const std::string &data,
                 bool active, bool subdued) {
    p.rounded(r, active ? accent : over(r, mouse) ? "#E7EEEA" : subdued ? surface : "#EFF3EF", 5);
    p.text(title, r, 13, active ? "#FFFFFF" : ink, active, true);
    hits.push_back({r, cmd, data, title});
}

void App::paintMapChrome(Painter &p) {
    float projectWidth=width<1150?216.f:324.f;
    dock(p,{16,16,16+projectWidth,60});
    iconButton(p,{22,20,58,56},"mountain","Проект и команды",FileMenu);
    auto projectName=map.doc["name"].str("Карта");
    if(auto separator=projectName.find(" · ");separator!=std::string::npos)projectName.resize(separator);
    p.text(width<1150?"АТЛАС": "АТЛАС  ·  "+projectName,{66,24,projectWidth+6,52},14,ink,true);
    hits.push_back({{62,16,16+projectWidth,60},FileMenu,{},"Проект и команды"});
    const auto &a=map.doc["story_anchor"];
    std::string moment="История карты";
    auto id=map.doc["parent_version"].str();
    for(const auto &v:versions)if(v["id"].str()==id) {moment=v["label"].str();break;}
    if(moment=="История карты" && a["scene_id"].isString()) {
        if(auto e=campaign.find(a["scene_id"].str()))moment=e->name;
    }
    if(a["chapter"].num()>0)moment="Глава "+std::to_string(int(a["chapter"].num()))+" · "+moment;
    float x=16+projectWidth+20,w=std::min(470.f,width-x-206);
    x=std::max(x,(width-w)/2);
    dock(p,{x,16,x+w,60});
    iconButton(p,{x+6,20,x+42,56},"history","Выбрать главу, событие и редакцию",HistoryView);
    p.text(moment,{x+48,25,x+w-16,51},13,ink,true);
    hits.push_back({{x+44,16,x+w,60},HistoryView,{},moment+" · Ctrl+H"});
    dock(p,{width-188,16,width-68,60});
    button(p,{width-186,18,width-70,58},dirty?"Сохранить":"✓ Сохранено",Save,"",dirty,true);
    dock(p,{width-58,16,width-16,60});
    iconButton(p,{width-55,20,width-19,56},"more","Все команды",FileMenu);

    float railHeight=7*42.f+12,ry=std::clamp((height-railHeight)/2,82.f,std::max(82.f,height-railHeight-78));
    dock(p,{16,ry,64,ry+railHeight});
    hits.push_back({{16,ry,64,ry+railHeight},0,{},{}});
    const char* glyphs[]={"arrow","node","pen","castle","text","layers","more"};
    const char* tips[]={"Выбор · V","Точки границ · K","Маршрут · R","Символ · S","Подпись · T","Слои","Все инструменты"};
    int tools[]={0,18,3,6,7};
    for(int i=0;i<7;++i)iconButton(p,{21,ry+6+i*42,59,ry+44+i*42},glyphs[i],tips[i],
        i<5?SetTool:i==5?LayersPanel:MoreTools,i<5?std::to_string(tools[i]):"",i<5&&int(tool)==tools[i]);
    dock(p,{16,height-58,108,height-16});
    iconButton(p,{20,height-55,60,height-19},"undo","Отменить · Ctrl+Z",Undo);
    iconButton(p,{62,height-55,102,height-19},"redo","Повторить · Ctrl+Y",Redo);
    dock(p,{width-214,height-58,width-16,height-16});
    iconButton(p,{width-210,height-55,width-174,height-19},"minus","Отдалить",ZoomOut);
    button(p,{width-172,height-54,width-110,height-20},std::to_string(int(zoom*100))+"%",Fit,"",false,true);
    iconButton(p,{width-108,height-55,width-72,height-19},"plus","Приблизить",ZoomIn);
    iconButton(p,{width-64,height-55,width-22,height-19},"fit","Вся карта · Home",Fit);
    if(!drawing.empty()) {
        float cx=width/2-122,y=height-64;
        dock(p,{cx-5,y-5,cx+251,y+43});
        button(p,{cx,y,cx+148,y+38},"Готово · Enter",FinishContour,"",true);
        button(p,{cx+156,y,cx+246,y+38},"Отмена",CancelContour);
    } else if(!activeBorder.empty()) {
        float cx=(width-346)/2,y=height-112;
        dock(p,{cx-6,y-6,cx+352,y+43});
        button(p,{cx,y,cx+112,y+37},"+ Точка",AddControl);
        button(p,{cx+118,y,cx+230,y+37},"− Точка",RemoveControl);
        button(p,{cx+236,y,cx+346,y+37},"Море",ToggleSea,"",allowSea);
    } else if(!selected.empty() && !(showPanels && panel==5)) {
        const auto &f=static_cast<const Json&>(map.doc)["features"][*selected.begin()];
        float cx=std::clamp(float(width/2-164),126.f,width-558.f),y=height-68;
        dock(p,{cx,y,cx+328,y+52});
        p.text(f["name"].str("Выбранный объект"),{cx+14,y+3,cx+244,y+29},13,ink,true);
        p.text(selected.size()>1?std::to_string(selected.size())+" объектов":"Свойства объекта",{cx+14,y+27,cx+244,y+49},11,muted);
        iconButton(p,{cx+246,y+7,cx+283,y+45},"pen","Изменить · F2",Properties);
        iconButton(p,{cx+286,y+7,cx+322,y+45},"more","Действия с объектом",SelectionMenu);
    }
    if(isolateLayer)button(p,{width-246,76,width-16,110},"Изоляция слоя · снять",IsolateLayer,"",true);
    if(tool!=Tool::Select && tool!=Tool::Pan && drawing.empty()) {
        std::string hint=tool==Tool::Border?"Выбери границу · двойной клик добавит точку":
            tool==Tool::Stamp?"Клик на карте · значки в библиотеке":tool==Tool::Label?"Клик на карте добавит подпись":
            "Клики — точки · Enter — завершить · Esc — отменить";
        if(!showPanels)p.text(hint,{84,76,width-270,102},12,ink);
    }
}

void App::paintSidebar(Painter &p) {
    if(!showPanels || historyView || panel==5)return;
    const auto r=drawerArea;float x=r.left,y=r.top,w=r.right-r.left,b=r.bottom;
    dock(p,r);hits.push_back({r,0,{},{}});
    p.text("Карта и объекты",{x+16,y+8,r.right-50,y+42},16,ink,true);
    iconButton(p,{r.right-44,y+8,r.right-8,y+42},"x","Закрыть · Esc",ClosePanel);
    button(p,{x+10,y+50,x+101,y+84},"Объекты",ObjectsTab,"",panel==2||panel==0,true);
    button(p,{x+105,y+50,x+188,y+84},"Слои",LayersPanel,"",panel==4,true);
    button(p,{x+192,y+50,r.right-10,y+84},"Значки",LibraryTab,"",panel==1,true);
    if(panel==4) {
        button(p,{x+12,y+96,x+98,y+127},"+ Слой",AddLayer);
        button(p,{x+104,y+96,x+176,y+127},"Архив",ShowArchiveLayers,"",showArchiveLayers);
        iconButton(p,{x+187,y+96,x+224,y+127},"up","Поднять слой",LayerUp);
        iconButton(p,{x+231,y+96,x+268,y+127},"down","Опустить слой",LayerDown);
        std::vector<const Json*> list;
        for(const auto &l:map.doc["layers"].arr())if(showArchiveLayers || (!l["archived"].boolean()&&!l["name"].str().starts_with("Архив")))list.push_back(&l);
        layerScroll=std::clamp(layerScroll,0,std::max(0,int(list.size())-int((b-y-147)/54)));
        float row=y+142;
        for(size_t i=layerScroll;i<list.size() && row+50<b-8;++i,row+=54) {
            const auto &l=*list[i];auto id=l["id"].str();
            if(id==activeLayer)p.rounded({x+8,row,r.right-8,row+48},"#E2EEE7",5);
            iconButton(p,{x+10,row+7,x+42,row+41},l["visible"].boolean(true)?"eye":"eye_off","Видимость слоя",Visibility,id);
            iconButton(p,{x+45,row+7,x+77,row+41},l["locked"].boolean()?"lock":"unlock","Защита слоя",Lock,id);
            p.text(l["name"].str(),{x+84,row+2,r.right-12,row+45},12,l["visible"].boolean(true)?ink:muted);
            hits.push_back({{x+80,row,r.right-8,row+48},SelectLayer,id,"Выбрать слой"});
        }
        return;
    }
    if(!searchBox)p.text(query.empty()?"Поиск по имени…":query,{x+16,y+96,r.right-16,y+124},13,muted);
    if(panel==1) {
        std::vector<std::pair<std::string,const Json*>> list;
        for(const auto &[id,d]:map.doc["symbols"].obj())if(matches(d["name"].str(),query))list.push_back({id,&d});
        int rows=std::max(1,int((b-y-197)/91));objectScroll=std::clamp(objectScroll,0,std::max(0,int(list.size())-rows*2));
        int n=0;for(size_t i=objectScroll;i<list.size();++i) {
            float px=x+12+(n%2)*(w-24)/2,py=y+142+(n/2)*91.f;++n;if(py+84>b-46)break;
            auto &[id,def]=list[i];D2D1_RECT_F cell{px,py,px+(w-32)/2,py+84};
            p.rounded(cell,id==activeSymbol?"#DCEDE4":"#F0F2EB",5);
            renderer.symbol(*def,p.target,{(cell.left+cell.right)/2,py+33},42,0,"#536358");
            p.text((*def)["name"].str(),{cell.left+4,py+59,cell.right-4,py+82},11,ink,false,true);
            hits.push_back({cell,SetSymbol,id,(*def)["name"].str()});
        }
        button(p,{x+12,b-42,x+103,b-10},"Меньше",Smaller);
        button(p,{x+112,b-42,x+203,b-10},"Больше",Larger);
        iconButton(p,{r.right-51,b-43,r.right-13,b-8},"color","Цвет",StrokeColor);return;
    }
    if(panel==3) {
        auto entries=campaign.search(query);int n=0;
        for(size_t i=objectScroll;i<entries.size();++i){auto e=entries[i];if(e->type!="location"&&e->type!="character"&&e->type!="character_asset")continue;
            float py=y+142+n++*49;if(py+43>b-8)break;button(p,{x+12,py,r.right-12,py+43},e->name,SelectEntity,e->id,false,true);}
        return;
    }
    button(p,{x+12,y+139,x+97,y+170},"Страны",FilterObjects,"country",objectFilter=="country",true);
    button(p,{x+103,y+139,x+190,y+170},"Места",FilterObjects,"settlement",objectFilter=="settlement",true);
    button(p,{x+196,y+139,r.right-12,y+170},"Все",FilterObjects,"",objectFilter.empty(),true);
    std::vector<const Json*> list;
    for(const auto &[id,f]:map.doc["features"].obj()) {
        if(!objectFilter.empty()&&f["role"].str()!=objectFilter&&!(objectFilter=="settlement"&&!f["entity_id"].str().empty()&&f["kind"].str()=="symbol"))continue;
        if(matches(f["name"].str()+" "+id,query))list.push_back(&f);
    }
    std::sort(list.begin(),list.end(),[](auto a,auto b){return (*a)["name"].str()<(*b)["name"].str();});
    objectScroll=std::clamp(objectScroll,0,std::max(0,int(list.size())-std::max(1,int((b-y-187)/48))));
    float row=y+182;
    for(size_t i=objectScroll;i<list.size()&&row+44<b-8;++i,row+=48) {
        auto &f=*list[i];auto id=f["id"].str();button(p,{x+12,row,r.right-12,row+43},f["name"].str(id),SelectObject,id,selected.contains(id),true);
    }
}

void App::paintProperties(Painter &p) {
    if(!showPanels || (!historyView&&panel!=5) || (historyView&&!compareMode))return;
    auto r=drawerArea;float x=r.left,y=r.top+8-detailScroll,b=r.bottom;
    dock(p,r);size_t first=hits.size();hits.push_back({r,0,{},{}});
    p.target->PushAxisAlignedClip(r,D2D1_ANTIALIAS_MODE_ALIASED);
    p.text(historyView?"Изменения":"Свойства",{x+16,y,r.right-52,y+38},16,ink,true);y+=48;
    if(historyView) {
        for(size_t i=diffScroll;i<diffRows.size();++i) {
            if(y+62>b-12)break;const auto &v=diffRows[i];
            p.text(v["name"].str(),{x+16,y,r.right-16,y+37},12,ink,true);
            p.text(v["change"].str(),{x+16,y+36,r.right-16,y+58},11,muted);
            hits.push_back({{x+8,y,r.right-8,y+61},FocusDiff,std::to_string(i),v["name"].str()});y+=66;
        }
        if(diffRows.size()==0)p.text("Изменений нет",{x+16,y,r.right-16,y+44},13,muted);
    } else {
        auto action=[&](const std::string &label,int cmd,bool active=false){button(p,{x+14,y,r.right-14,y+35},label,cmd,"",active);y+=44;};
        if(selected.empty()){p.text("Выбери объект на карте",{x+16,y,r.right-16,y+48},13,muted);y+=59;action("Свойства карты",Properties);}
        else {
            const auto &f=static_cast<const Json&>(map.doc)["features"][*selected.begin()];
            p.text(f["name"].str(),{x+16,y,r.right-16,y+59},14,ink,true);y+=70;
            action("Название и оформление",Properties);action("Цвет заливки",FillColor);action("Цвет линии",StrokeColor);
            auto e=campaign.find(f["entity_id"].str());if(e)action("Открыть карточку",OpenCard);
            action(e?"Изменить связь":"Связать с карточкой",BindEntity);action("Дублировать",Duplicate);action("Приблизить объект",FitSelection);
            if(f["kind"].str()=="symbol")action("Сохранить как шаблон",SavePreset);
        }
        action("Изолировать слой",IsolateLayer,isolateLayer);action("Подписи карты",ToggleNames,mapLabels);action("Привязка к точкам",Snap,snap);
    }
    p.target->PopAxisAlignedClip();
    for(size_t i=first;i<hits.size();++i){hits[i].rect.top=std::max(r.top,hits[i].rect.top);hits[i].rect.bottom=std::min(b,hits[i].rect.bottom);if(hits[i].rect.bottom<hits[i].rect.top)hits[i].command=0;}
    iconButton(p,{r.right-43,r.top+9,r.right-9,r.top+41},"x","Закрыть",historyView?ToggleDiff:ClosePanel);
}

void App::paintComparisonChrome(Painter &p) {
    p.fill({0,0,width,106},surface);
    button(p,{20,16,155,54},"← История",HistoryView,"",false,true);
    p.text("Сравнение редакций",{176,16,485,54},20,ink,true);
    button(p,{width-385,17,width-260,54},compareMode==2?"Наложение":"Рядом",CompareOverlay);
    button(p,{width-248,17,width-134,54},"Изменения",ToggleDiff,"",showPanels);
    button(p,{width-122,17,width-20,54},"К карте",EditorView);
    float mid=width/2;
    button(p,{20,64,mid-14,99},"A · "+versionDetails["label"].str(),ChooseCompareA,"",false,true);
    std::string title="Рабочий черновик";
    if(comparisonB)for(const auto &v:map.versions())if(v["id"].str()==comparisonB->doc["_comparison_id"].str())title=v["label"].str();
    button(p,{mid+14,64,width-20,99},"B · "+title,ChooseCompareB,"",false,true);
    dock(p,{width-181,height-58,width-16,height-16});
    iconButton(p,{width-176,height-55,width-136,height-19},"minus","Отдалить",ZoomOut);
    iconButton(p,{width-128,height-55,width-88,height-19},"fit","Вся карта",Fit);
    iconButton(p,{width-80,height-55,width-40,height-19},"plus","Приблизить",ZoomIn);
}

void App::paintChrome(Painter &p) {
    if(historyView&&!compareMode)paintHistoryScreen(p);
    else {
        if(historyView)paintComparisonChrome(p);else paintMapChrome(p);
        paintSidebar(p);paintProperties(p);
    }
    if(keyboardHit>=0 && keyboardHit<int(hits.size()))p.rect(hits[keyboardHit].rect,accent,2);
    if(now()<noticeUntil) {
        float w=std::min(540.f,width-180),x=(width-w)/2,y=historyView?78.f:height-130;
        dock(p,{x,y,x+w,y+48});p.text(status,{x+14,y+2,x+w-14,y+45},12,ink);
        hits.push_back({{x,y,x+w,y+48},0,{},{}});
    }
    if(!down && now()-hoverSince>=650)for(auto it=hits.rbegin();it!=hits.rend();++it)if(over(it->rect,mouse)){
        if(!it->tip.empty()) {float w=std::min(400.f,30.f+float(wide(it->tip).size())*6.f),x=std::clamp(float(mouse.x)+16,8.f,width-w-8),y=std::clamp(float(mouse.y)+24,8.f,height-50);
            p.rounded({x,y,x+w,y+37},"#233C37",5);p.text(it->tip,{x+10,y,x+w-10,y+37},12,"#FFFFFF");}break;
    }
}
} // namespace atlas
