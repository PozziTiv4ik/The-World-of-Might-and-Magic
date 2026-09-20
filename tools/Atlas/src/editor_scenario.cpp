#include "app.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <sstream>

namespace atlas {
namespace {
const std::map<std::string,int> commands{
    {"new",NewFile},{"open",OpenFile},{"save",Save},{"save_as",SaveAs},{"export_png",ExportPng},{"export_svg",ExportSvg},{"add_raster",AddRaster},
    {"undo",Undo},{"redo",Redo},{"copy",Copy},{"paste",Paste},{"duplicate",Duplicate},{"delete",Delete},
    {"properties",Properties},{"bind",BindEntity},{"stroke_color",StrokeColor},{"fill_color",FillColor},
    {"smaller",Smaller},{"larger",Larger},{"simplify",Simplify},{"union",Union},{"subtract",Subtract},
    {"fit",Fit},{"fit_selection",FitSelection},{"zoom_in",ZoomIn},{"zoom_out",ZoomOut},{"snap",Snap},{"grid",Grid},
    {"panels",Panels},{"objects",ObjectsTab},{"symbols",LibraryTab},{"layers",LayersPanel},{"inspector",InspectorPanel},
    {"close_panel",ClosePanel},{"names",ToggleNames},{"more_tools",MoreTools},{"file_menu",FileMenu},
    {"edit_menu",EditMenu},{"view_menu",ViewMenu},{"version_menu",VersionMenu},{"selection_menu",SelectionMenu},
    {"scope_borders",ScopeBorders},{"scope_land",ScopeLand},{"scope_objects",ScopeObjects},{"isolate",IsolateLayer},
    {"add_control",AddControl},{"remove_control",RemoveControl},{"allow_sea",ToggleSea},
    {"finish",FinishContour},{"cancel",CancelContour},{"tool",SetTool},{"symbol",SetSymbol},
    {"add_layer",AddLayer},{"layer",SelectLayer},{"layer_up",LayerUp},{"layer_down",LayerDown},
    {"visibility",Visibility},{"lock",Lock},{"rename_layer",RenameLayer},{"layer_properties",LayerProperties},
    {"rotate_left",RotateLeft},{"rotate_right",RotateRight},{"scale_down",ScaleDown},{"scale_up",ScaleUp},
    {"history",HistoryView},{"editor",EditorView},{"new_event",NewEvent},{"new_revision",NewRevision},{"snapshot",Snapshot},
    {"open_moment",OpenMoment},{"select_version",SelectVersion},{"select_revision",SelectRevision},{"edit_event",EditVersion},
    {"compare",CompareRevisions},{"compare_view",CompareView},{"compare_overlay",CompareOverlay},{"compare_a",ChooseCompareA},{"compare_b",ChooseCompareB},
    {"chapter",HistoryFilter},{"history_options",HistoryOptions},{"collection",HistoryCollection},{"sort",HistorySort},
    {"history_search",HistorySearch},{"history_page",HistoryPage},{"history_scale",HistoryScale},{"previous",PreviousVersion},{"next",NextVersion},
    {"drafts",DraftsDialog},{"save_draft",SaveNamedDraft},{"recover",RecoveryDialog},{"diff",ShowDiff},{"toggle_diff",ToggleDiff},
    {"select_object",SelectObject},{"filter_objects",FilterObjects},{"focus_scene",FocusScene},{"fullscreen",FullScreen}
};
const std::vector<std::string> toolNames{"select","node","country","route","river","brush","symbol","label","eraser","pan","zone","trace","measure","rectangle","lasso","fill","picker","land","border"};
int commandId(const std::string &name){auto it=commands.find(name);if(it==commands.end())throw std::runtime_error("Unknown editor command: "+name);return it->second;}
std::string commandName(int id){for(const auto &[name,value]:commands)if(value==id)return name;return std::to_string(id);}
std::string plainName(std::string value) {
    for(auto &c:value)if(c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|'||c=='\n'||c=='\r')c='_';
    if(value.empty())value="capture";return value.substr(0,100);
}
bool under(const fs::path &path,const fs::path &directory) {
    auto relative=fs::absolute(path).lexically_normal().lexically_relative(fs::absolute(directory).lexically_normal());
    if(relative.empty())return true;
    return !relative.is_absolute()&&*relative.begin()!=L"..";
}
const Json *pointer(const Json &root,const std::string &path) {
    if(path.empty())return &root;
    if(path[0]!='/')throw std::runtime_error("Expected JSON pointer starting with /: "+path);
    auto at=&root;size_t begin=1;
    while(true){auto end=path.find('/',begin);auto token=path.substr(begin,end==std::string::npos?end:end-begin);
        for(const auto &[from,to]:std::vector<std::pair<std::string,std::string>>{{"~1","/"},{"~0","~"}})
            for(size_t p=0;(p=token.find(from,p))!=std::string::npos;p+=to.size())token.replace(p,from.size(),to);
        if(at->isObject()){if(!at->contains(token))return nullptr;at=&(*at)[token];}
        else if(at->isArray()){size_t used=0,index=0;try{index=std::stoull(token,&used);}catch(...){return nullptr;}
            if(used!=token.size()||index>=at->size())return nullptr;at=&(*at)[index];}
        else return nullptr;
        if(end==std::string::npos)return at;begin=end+1;
    }
}
bool approximatelyEqual(const Json &a,const Json &b,double epsilon) {
    if(a.isNumber()&&b.isNumber())return std::abs(a.num()-b.num())<=epsilon;
    if(a.isArray()&&b.isArray()&&a.size()==b.size()){for(size_t i=0;i<a.size();++i)if(!approximatelyEqual(a[i],b[i],epsilon))return false;return true;}
    return a==b;
}
std::string html(const std::string &s){std::string out;for(char c:s){if(c=='&')out+="&amp;";else if(c=='<')out+="&lt;";else if(c=='>')out+="&gt;";else if(c=='"')out+="&quot;";else out+=c;}return out;}
}

class EditorScenario {
    Json specification,report=Json::object(),trace=Json::array(),answers=Json::array(),interactions=Json::array();
    std::unique_ptr<App> app;
    fs::path directory,source,project;
    Json sourceHashes=Json::object();
    Json lastEditBefore;
    std::map<std::string,Json> aliases;
    std::optional<std::string> clipboard;
    Image frame;
    size_t answerIndex=0,index=0,checks=0;
    float scale=1;
    bool captureAll=false;

    std::string expand(std::string value) const {
        for(size_t pos=0;(pos=value.find("${",pos))!=std::string::npos;) {
            auto end=value.find('}',pos);if(end==std::string::npos)throw std::runtime_error("Unclosed alias: "+value);
            auto key=value.substr(pos+2,end-pos-2);auto it=aliases.find(key);if(it==aliases.end())throw std::runtime_error("Unknown alias: "+key);
            auto replacement=it->second.isString()?it->second.str():it->second.dump(0);value.replace(pos,end-pos+1,replacement);pos+=replacement.size();
        }
        return value;
    }
    fs::path owned(const std::string &relative) const {
        auto value=expand(relative);if(value.starts_with("@run/"))value.erase(0,5);
        auto result=fs::absolute(directory/pathOf(value)).lexically_normal();
        if(!under(result,directory))throw std::runtime_error("Scenario path escapes its isolated run directory");
        return result;
    }
    void require(bool condition,const std::string &message){++checks;if(!condition)throw std::runtime_error(message);}
    Json inventory(const fs::path &root) const {
        Json values=Json::object();if(root.empty())return values;
        for(fs::recursive_directory_iterator it(root),end;it!=end;++it) {
            if(it->path().filename()==L".atlas"){if(it->is_directory())it.disable_recursion_pending();continue;}
            if(it->is_symlink())throw std::runtime_error("Scenario input contains a symbolic link");
            if(it->is_regular_file())values[pathText(fs::relative(it->path(),root))]=hashBytes(readBytes(it->path()));
        }
        return values;
    }
    void initialize(const fs::path &mapPath) {
        app=std::make_unique<App>(project,mapPath);app->headless=true;
        app->campaign.load(project);app->map.load(mapPath);
        if(app->map.doc["symbols"].size()==0)app->map.doc["symbols"]=defaultSymbols();
        app->activeLayer=app->map.vectorLayer();app->editScope=SelectionDomain::All;
        app->width=float(specification["viewport"]["width"].num(1280));app->height=float(specification["viewport"]["height"].num(800));
        require(app->width>=800&&app->height>=500,"Viewport below the supported logical minimum");
        app->dpi=scale;app->mouse={-100,-100};app->computeLayout();app->refreshVersions();app->fit();
    }
    void render(){frame=app->captureFrame(scale);require(!app->window,"Replay unexpectedly created a desktop window");}
    Json state(bool controls=true) const {
        const App &a=*app;const Json &doc=a.map.doc;Json selected=Json::array(),undo=Json::array();
        for(const auto &id:a.selected)selected.push(id);for(const auto &name:a.history.labels())undo.push(name);
        const auto *event=a.selectedEvent();Json boundary=Json::array();for(const auto &id:a.activeControls)boundary.push(id);
        Json s=fields({{"mode",a.historyView?(a.compareMode?"compare":"history"):"map"},{"tool",toolNames.at(int(a.tool))},
            {"dirty",a.dirty},{"selection",selected},{"features",doc["features"].size()},{"layers",doc["layers"].size()},
            {"zoom",a.zoom},{"offset",pointJson(a.offset)},{"width",a.width},{"height",a.height},{"dpi",scale},
            {"canvas",Json::Array{a.canvas.left,a.canvas.top,a.canvas.right,a.canvas.bottom}},
            {"drawing_points",a.drawing.size()},{"down",a.down},{"panning",a.panning},{"drag_preview",a.dragPreview},
            {"selected_control",a.activeControl},{"selected_border",a.activeBorder},{"boundary_controls",boundary},{"controls",Json::array()},
            {"panel",a.panel},{"panels",a.showPanels},{"search_visible",a.historySearch},{"query",a.query},
            {"events",a.storyEvents.size()},{"event_scroll",a.versionScroll},{"visible_events",a.visibleEvents()},
            {"selected_version",a.versionDetails["id"]},{"parent_version",doc["parent_version"]},
            {"revisions",event?event->revisions.size():0},{"compare_mode",a.compareMode},{"diff_count",a.diffRows.size()},
            {"keyboard_focus",a.keyboardHit},{"active_layer",a.activeLayer},{"undo",undo},{"status",a.status},
            {"document_hash",hashText(doc.dump())},{"map_path",pathText(a.map.directory)}});
        if(controls)for(const auto &hit:a.hits)if(hit.command)s["controls"].push(fields({{"command",commandName(hit.command)},
            {"data",hit.data},{"tip",hit.tip},{"rect",Json::Array{hit.rect.left,hit.rect.top,hit.rect.right,hit.rect.bottom}}}));
        return s;
    }
    void capture(const std::string &name,Json &record) {
        std::string stem=std::to_string(index)+"-"+plainName(name);
        auto file=directory/pathOf(stem+".png");savePng(file,frame);
        auto snapshot=state();snapshot["document"]=app->map.doc;
        atomicText(directory/pathOf(stem+".json"),snapshot.dump()+"\n");
        record["image"]=stem+".png";record["state_file"]=stem+".json";
    }
    Json interaction(const Json &request) {
        auto kind=request["kind"].str();
        if(kind=="clipboard_set"){clipboard=request["value"].str();return Json::object();}
        if(kind=="clipboard_get")return fields({{"value",clipboard?Json(*clipboard):Json()}});
        if(answerIndex>=answers.size())throw std::runtime_error("Unexpected interaction: "+request.dump(0));
        const auto answer=answers[answerIndex++];
        require(answer["kind"].str()==kind,"Expected "+answer["kind"].str()+", application requested "+kind);
        if(answer.contains("title"))require(request["title"].str().find(expand(answer["title"].str()))!=std::string::npos,"Unexpected dialog title: "+request["title"].str());
        interactions.push(fields({{"request",request},{"answer",answer}}));
        if(answer["cancel"].boolean())return fields({{"cancel",true}});
        if(kind=="form") {
            Json result=Json::array();std::set<std::string> consumed;
            for(const auto &field:request["fields"].arr()) {
                auto label=field["label"].str();auto value=field["value"].str();
                if(answer["values"].contains(label)){value=expand(answer["values"][label].str());consumed.insert(label);}
                result.push(value);
            }
            if(answer["values"].isObject())for(const auto &[label,value]:answer["values"].obj())require(consumed.contains(label),"Unknown dialog field: "+label);
            return fields({{"values",result}});
        }
        if(kind=="menu") {
            auto label=expand(answer["label"].str());Json selected;int count=0;
            for(const auto &item:request["options"].arr())if(item["label"].str().find(label)!=std::string::npos){selected=item;++count;}
            require(count==1,"Menu selector must match exactly one item: "+label);return fields({{"value",selected["id"]}});
        }
        if(kind=="confirm") {auto value=answer["value"].str();return fields({{"value",value=="yes"?IDYES:value=="no"?IDNO:value=="ok"?IDOK:IDCANCEL}});}
        if(kind=="color")return fields({{"value",expand(answer["value"].str())}});
        if(kind=="save_file"||kind=="open_file"||kind=="folder") {
            auto path=owned(answer["value"].str());fs::create_directories(kind=="folder"?path:path.parent_path());
            return fields({{"value",pathText(path)}});
        }
        throw std::runtime_error("Unsupported scripted interaction: "+kind);
    }
    void modifiers(const Json &step) {
        app->replayKeys.clear();
        if(!step["modifiers"].isArray())return;
        for(const auto &key:step["modifiers"].arr()) {
            auto name=key.str();int value=name=="ctrl"?VK_CONTROL:name=="shift"?VK_SHIFT:name=="alt"?VK_MENU:name=="space"?VK_SPACE:0;
            if(!value)throw std::runtime_error("Unknown modifier: "+name);app->replayKeys.insert(value);
        }
    }
    Point position(const Json &value,const Json &step) const {
        requirePoint(value);auto p=point(value);
        if(step["space"].str()=="world")p={p.x*app->zoom+app->offset.x,p.y*app->zoom+app->offset.y};
        else if(step["space"].str()=="pixel")p={p.x/scale,p.y/scale};
        return p;
    }
    static void requirePoint(const Json &value){if(!value.isArray()||value.size()!=2||!value[0].isNumber()||!value[1].isNumber())throw std::runtime_error("Expected [x,y]");}
    void key(std::string value) {
        std::map<std::string,int> names{{"Enter",VK_RETURN},{"Escape",VK_ESCAPE},{"Delete",VK_DELETE},{"Tab",VK_TAB},{"Left",VK_LEFT},{"Right",VK_RIGHT},
            {"Home",VK_HOME},{"End",VK_END},{"PageUp",VK_PRIOR},{"PageDown",VK_NEXT},{"F2",VK_F2},{"F6",VK_F6},{"F11",VK_F11}};
        size_t separator;
        while((separator=value.find('+'))!=std::string::npos){auto prefix=value.substr(0,separator);value.erase(0,separator+1);
            int mod=prefix=="Ctrl"?VK_CONTROL:prefix=="Shift"?VK_SHIFT:prefix=="Alt"?VK_MENU:0;if(!mod)throw std::runtime_error("Unknown chord modifier");app->replayKeys.insert(mod);}
        int code=value.size()==1?std::toupper(static_cast<unsigned char>(value[0])):names.contains(value)?names.at(value):0;
        if(!code)throw std::runtime_error("Unknown key: "+value);
        app->message(WM_KEYDOWN,code,0);
    }
    Json readTarget(const Json &step) const {
        auto target=step["target"].str();
        if(target=="document")return app->map.doc;
        if(target=="saved")return Json::parse(readText(app->map.directory/L"map.json"));
        if(target=="file_hash")return hashBytes(readBytes(owned(step["file"].str())));
        if(target=="geometry") {
            const Json &doc=app->map.doc;auto id=expand(step["feature"].str());
            if(!doc["features"].contains(id))throw std::runtime_error("Missing geometry feature: "+id);
            Json rings=Json::array();for(const auto &ring:app->map.paths(doc["features"][id])){Json points=Json::array();for(auto p:ring)points.push(pointJson(p));rings.push(points);}return rings;
        }
        return state();
    }
    void assertion(const Json &step) {
        if(step.contains("file")&&step["target"].str()!="file_hash") {
            auto path=owned(step["file"].str());require(fs::is_regular_file(path),"Missing output file: "+pathText(path));
            if(step.contains("contains"))require(readText(path).find(expand(step["contains"].str()))!=std::string::npos,"Export does not contain expected text");
            if(step.contains("image_size")){auto image=loadImage(path);require(image->width==step["image_size"][0].num()&&image->height==step["image_size"][1].num(),"Export dimensions differ");}return;
        }
        Json root=readTarget(step);
        require(step.contains("equals")||step.contains("equals_ref")||step.contains("not_equals_ref")||step.contains("exists")||step.contains("count")||step.contains("gt")||step.contains("contains"),"Assertion requires an expectation operator");
        auto value=pointer(root,expand(step["pointer"].str()));
        if(step.contains("exists")){require(bool(value)==step["exists"].boolean(),"Unexpected field existence: "+step["pointer"].str());if(!value)return;}
        require(value!=nullptr,"Missing assertion field: "+step["pointer"].str());
        Json expected;
        bool compare=step.contains("equals")||step.contains("equals_ref");
        if(step.contains("equals_ref")){auto name=step["equals_ref"].str();require(aliases.contains(name),"Unknown expected alias: "+name);expected=aliases.at(name);}
        else if(step.contains("equals")){expected=step["equals"];if(expected.isString())expected=expand(expected.str());}
        if(compare)require(approximatelyEqual(*value,expected,step["epsilon"].num(0)),"Assertion "+step["pointer"].str()+": expected "+expected.dump(0).substr(0,400)+", got "+value->dump(0).substr(0,400));
        if(step.contains("not_equals_ref"))require(!(*value==aliases.at(step["not_equals_ref"].str())),"Expected changed value: "+step["pointer"].str());
        if(step.contains("count"))require(value->size()==step["count"].num(),"Wrong collection size: "+step["pointer"].str());
        if(step.contains("gt"))require(value->num()>step["gt"].num(),"Expected greater value: "+step["pointer"].str());
        if(step.contains("contains"))require(value->str().find(expand(step["contains"].str()))!=std::string::npos,"Expected text missing: "+step["pointer"].str());
    }
    void act(const Json &step) {
        auto action=step["action"].str();modifiers(step);app->replayTime+=20;
        if(action=="answer"){answers.push(step["answer"]);return;}
        if(action=="command")app->command(commandId(step["command"].str()),expand(step["data"].str()));
        else if(action=="tool") {auto name=step["tool"].str();auto at=std::find(toolNames.begin(),toolNames.end(),name);if(at==toolNames.end())throw std::runtime_error("Unknown tool: "+name);app->command(SetTool,std::to_string(at-toolNames.begin()));}
        else if(action=="key")key(step["key"].str());
        else if(action=="click"||action=="double_click"||action=="pointer_down"||action=="pointer_up"||action=="pointer_move") {
            Point at;
            if(step.contains("control")) {
                const auto &selector=step["control"];int command=commandId(selector["command"].str());std::vector<Hit> candidates;
                for(const auto &hit:app->hits)if(hit.command==command&&(!selector.contains("data")||hit.data==expand(selector["data"].str())))candidates.push_back(hit);
                if(selector.contains("index")){int n=int(selector["index"].num());require(n>=0&&n<int(candidates.size()),"Control index outside visible UI");candidates={candidates[n]};}
                require(candidates.size()==1,"Control selector must match exactly one visible control: "+selector.dump(0));
                const auto &r=candidates[0].rect;at={(r.left+r.right)/2,(r.top+r.bottom)/2};
            }else at=position(step["at"],step);
            bool middle=step["button"].str()=="middle";
            if(action=="pointer_move")app->pointerMove(at);
            else if(action=="pointer_up")app->pointerUp(at);
            else {app->pointerDown(at,middle);if(action!="pointer_down")app->pointerUp(at);
                if(action=="double_click")app->message(WM_LBUTTONDBLCLK,0,MAKELPARAM(int(at.x*app->dpi),int(at.y*app->dpi)));}
        } else if(action=="drag") {
            auto from=position(step["from"],step),to=position(step["to"],step);app->pointerDown(from,step["button"].str()=="middle");
            int count=std::clamp(int(step["segments"].num(8)),1,100);
            for(int n=1;n<=count;++n)app->pointerMove({from.x+(to.x-from.x)*n/count,from.y+(to.y-from.y)*n/count});
            if(step["preview"].boolean()){render();Json record=fields({{"action","drag_preview"},{"status","passed"}});capture("drag-preview",record);trace.push(record);}
            app->pointerUp(to);
        } else if(action=="search")app->setQuery(expand(step["text"].str()));
        else if(action=="wheel")app->wheel(position(step["at"],step),int(step["delta"].num()));
        else if(action=="resize") {
            app->width=float(step["width"].num());app->height=float(step["height"].num());scale=float(step["dpi"].num(scale));
            require(app->width>=800&&app->height>=500,"Viewport below the supported logical minimum");app->dpi=scale;app->computeLayout();
        } else if(action=="camera") {app->zoom=step["zoom"].num(1);require(app->zoom>=.01&&app->zoom<=24,"Invalid camera zoom");app->offset=point(step["offset"]);}
        else if(action=="wait") {auto ms=step["milliseconds"].num();require(ms>=0&&ms<=86400000,"Invalid virtual wait duration");app->replayTime+=ULONGLONG(ms);if(step["autosave"].boolean())app->message(WM_TIMER,1,0);}
        else if(action=="capture") {}
        else if(action=="assert")assertion(step);
        else if(action=="remember") {auto root=readTarget(step);auto value=pointer(root,expand(step["pointer"].str()));require(value!=nullptr,"Cannot remember a missing field");aliases[step["name"].str()]=*value;}
        else if(action=="clipboard")clipboard=step["text"].str();
        else if(action=="restart") {auto path=app->map.directory;initialize(path);}
        else if(action=="validate_history")require(app->map.validateHistory()["errors"].size()==0,"Saved history integrity failed");
        else if(action=="assert_ui") {
            for(const auto &hit:app->hits)if(hit.command)require(hit.rect.left>=0&&hit.rect.top>=0&&hit.rect.right<=app->width+.1f&&hit.rect.bottom<=app->height+.1f&&hit.rect.right>=hit.rect.left&&hit.rect.bottom>=hit.rect.top,"Control outside viewport: "+commandName(hit.command));
        } else if(action=="explore")explore(step);
        else throw std::runtime_error("Unknown scenario action: "+action);
        app->replayKeys.clear();
    }
    void explore(const Json &step) {
        std::mt19937 random(unsigned(step["seed"].num(20260920)));int count=std::clamp(int(step["steps"].num(120)),1,2000);
        for(int n=0;n<count;++n) {
            app->command(EditorView);app->command(ClosePanel);app->keyboardHit=-1;app->drawing.clear();
            auto original=app->map.doc;lastEditBefore=original;size_t undo=app->history.labels().size();int action=int(random()%6);
            Json operation=fields({{"kind",action==0||app->map.doc["features"].size()==0?"symbol":action==1?"rectangle":action==2?"route":action==3?"rotate":action==4?"delete":"duplicate"}});
            report["exploration"]=fields({{"seed",step["seed"]},{"iteration",n},{"choice",action}});
            double x=180+random()%std::max(1,int(app->width-430)),y=140+random()%std::max(1,int(app->height-310));
            if(action==0||app->map.doc["features"].size()==0) {
                app->command(SetSymbol,"fortress");app->command(ClosePanel);render();app->pointerDown({x,y});app->pointerUp({x,y});
            } else if(action==1) {
                app->command(SetTool,std::to_string(int(Tool::Rectangle)));render();app->pointerDown({x,y});app->pointerMove({x+40,y+35});app->pointerUp({x+40,y+35});
            } else if(action==2) {
                app->command(SetTool,std::to_string(int(Tool::Route)));render();app->pointerDown({x,y});app->pointerUp({x,y});app->pointerDown({x+50,y+40});app->pointerUp({x+50,y+40});key("Enter");
            } else {
                std::vector<std::string> eligible;for(const auto &[id,f]:app->map.doc["features"].obj())if(app->map.selectable(f,SelectionDomain::Objects))eligible.push_back(id);
                const Json &features=app->map.doc["features"];
                std::sort(eligible.begin(),eligible.end(),[&](const auto &a,const auto &b){return features[a]["z_order"].num()<features[b]["z_order"].num();});
                if(eligible.empty())continue;
                auto chosen=eligible[random()%eligible.size()];operation["target_z"]=features[chosen]["z_order"];operation["target_kind"]=features[chosen]["kind"];
                app->command(SelectObject,chosen);
                app->command(action==3?RotateRight:action==4?Delete:Duplicate);
            }
            auto validation=app->map.validate();
            require(validation["errors"].size()==0,"Explore invalid document, seed "+step["seed"].dump(0)+", action "+std::to_string(n)+": "+validation["errors"].dump(0));
            auto after=app->map.doc;
            if(!(original==after)) {
                require(app->history.labels().size()>undo||undo==250,"Edit missing undo record at explore action "+std::to_string(n));
                app->command(Undo);require(app->map.doc==original,"Undo does not restore explore action "+std::to_string(n));
                app->command(Redo);require(app->map.doc==after,"Redo does not restore explore action "+std::to_string(n));
            }
            trace.push(fields({{"action","explore_edit"},{"seed",step["seed"]},{"iteration",n},{"choice",action},{"operation",operation},{"point",pointJson({x,y})},{"status","passed"},{"document_hash",hashText(after.dump())}}));
            if(n%25==0){render();Json record=fields({{"action","explore_checkpoint"},{"iteration",n},{"status","passed"}});capture("explore-"+std::to_string(n),record);trace.push(record);}
        }
    }
    void writeReport() {
        report["steps"]=trace;report["interactions"]=interactions;report["checks"]=checks;report["output"]=pathText(directory);
        atomicText(directory/L"report.json",report.dump()+"\n");
        std::ostringstream page;page<<"<!doctype html><html lang='ru'><meta charset='utf-8'><title>Atlas: "<<html(report["name"].str())<<"</title>"
            "<style>body{font:15px system-ui;max-width:1100px;margin:40px auto;background:#fafbf8;color:#233432;padding:0 24px}h1{font-size:28px}article{border-top:1px solid #d8e1dc;padding:18px 0}img{max-width:100%;border:1px solid #d8e1dc}pre{white-space:pre-wrap;overflow-wrap:anywhere}.failed{color:#a33429}a{color:#22685f}</style>"
            "<h1>"<<html(report["name"].str())<<"</h1><p class='"<<report["status"].str()<<"'>"<<html(report["status"].str())<<" · "<<checks<<" проверок</p><p>"<<html(report["error"].str())<<"</p><p><a href='report.json'>Журнал JSON</a></p>";
        for(const auto &row:trace.arr()){page<<"<article><b>"<<html(row["action"].str())<<"</b> · "<<html(row["status"].str());
            if(row["image"].isString())page<<"<p><a href='"<<html(row["state_file"].str())<<"'>Состояние редактора</a></p><img src='"<<html(row["image"].str())<<"'>";
            page<<"<details><summary>Подробности</summary><pre>"<<html(row.dump())<<"</pre></details></article>";}
        page<<"</html>";atomicText(directory/L"report.html",page.str());
    }
  public:
    EditorScenario(Json spec,fs::path out,fs::path input,fs::path campaign):specification(std::move(spec)),source(std::move(input)),project(std::move(campaign)) {
        require(specification["schema_version"].num()==1,"Unsupported editor scenario schema");
        require(specification["steps"].isArray(),"Scenario requires a steps array");
        require(!specification["requires_map"].boolean()||!source.empty(),"This scenario requires --map; it always runs on a copy");
        if(!source.empty()){source=fs::absolute(source).lexically_normal();if(fs::is_regular_file(source))source=source.parent_path();
            if(under(out,source))throw std::runtime_error("Replay output must be outside the source map");}
        directory=fs::absolute(out/pathOf(plainName(specification["name"].str("scenario"))+"-"+newId(""))).lexically_normal();
        fs::create_directories(directory);
        report=fields({{"name",specification["name"].str()},{"status","running"},{"computer_use",false},{"native_window",false}});
        captureAll=specification["capture_all"].boolean();scale=float(specification["viewport"]["dpi"].num(1));
    }
    Json run() {
        ScopedDialogHandler driver([&](const Json &request){return interaction(request);});
        try {
            auto mapPath=directory/L"map";
            if(!source.empty()) {
                sourceHashes=inventory(source);fs::create_directories(mapPath);
                for(const auto &[name,sha]:sourceHashes.obj()){auto to=mapPath/pathOf(name);fs::create_directories(to.parent_path());fs::copy_file(source/pathOf(name),to);}
            } else {Map map;map.create(int(specification["create"]["width"].num(1600)),int(specification["create"]["height"].num(1000)),specification["create"]["name"].str("Тестовая карта"));map.doc["symbols"]=defaultSymbols();map.save(mapPath);}
            if(specification["images"].isArray())for(const auto &definition:specification["images"].arr()) {
                int w=int(definition["width"].num(64)),h=int(definition["height"].num(64));
                require(w>0&&h>0&&w<=2048&&h<=2048,"Invalid fixture image dimensions");
                Image image(w,h);auto fill=color(definition["fill"].str("#FFFFFF"));
                for(size_t i=0;i<image.bgra.size();i+=4){image.bgra[i]=uint8_t(fill.b*255);image.bgra[i+1]=uint8_t(fill.g*255);image.bgra[i+2]=uint8_t(fill.r*255);image.bgra[i+3]=255;}
                auto file=owned(definition["file"].str());fs::create_directories(file.parent_path());savePng(file,image);
            }
            atomicText(directory/L"scenario.json",specification.dump()+"\n");initialize(mapPath);render();
            Json initial=fields({{"action","initial"},{"status","passed"}});capture("initial",initial);trace.push(initial);
            for(const auto &step:specification["steps"].arr()) {
                ++index;auto started=std::chrono::steady_clock::now();auto before=app->map.doc;
                Json record=fields({{"index",index},{"action",step["action"]},{"input",step}});
                std::string error;
                try{act(step);}catch(const std::exception &e){error=e.what();app->rollbackInteraction();}
                if(step.contains("expect_error"))require(!error.empty()&&error.find(expand(step["expect_error"].str()))!=std::string::npos,"Expected error was not produced: "+error);
                else if(!error.empty()){record["status"]="failed";record["error"]=error;trace.push(record);throw std::runtime_error(error);}
                require(app->map.validate()["errors"].size()==0,"Document invalid after "+step["action"].str()+": "+app->map.validate()["errors"].dump(0));
                require(app->map.directory.empty()||under(app->map.directory,directory),"Editor escaped isolated test map");
                render();record["status"]="passed";record["expected_error"]=error;record["state"]=state(false);record["changed"]=!(before==app->map.doc);
                record["milliseconds"]=double(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count());
                if(captureAll||step["action"].str()=="capture"||step["capture"].isString())capture(step["capture"].str(step["action"].str()),record);
                trace.push(record);atomicText(directory/L"trace.json",trace.dump()+"\n");
            }
            require(answerIndex==answers.size(),"Unused scripted dialog answers");
            require(!app->down&&!app->timelineDragging,"Scenario ended with an unfinished pointer gesture");
            require(source.empty()||inventory(source)==sourceHashes,"Source map changed during scenario");
            render();Json final=fields({{"action","final"},{"status","passed"}});capture("final",final);trace.push(final);
            report["status"]="passed";report["source_unchanged"]=true;
        } catch(const std::exception &e) {
            report["status"]="failed";report["error"]=e.what();report["failed_step"]=index;
            if(app)try{render();Json failed=fields({{"action","failure"},{"status","failed"},{"error",e.what()}});capture("failure",failed);trace.push(failed);}catch(const std::exception &renderError){report["capture_error"]=renderError.what();}
            if(app){auto snapshot=state();snapshot["document"]=app->map.doc;report["validation"]=app->map.validate();atomicText(directory/L"failure-state.json",snapshot.dump()+"\n");}
            if(lastEditBefore.isObject())atomicText(directory/L"failure-before.json",lastEditBefore.dump()+"\n");
            if(!source.empty())report["source_unchanged"]=inventory(source)==sourceHashes;
        }
        writeReport();return report;
    }
};

Json runEditorScenario(const fs::path &scenario,const fs::path &output,const fs::path &source,const fs::path &project) {
    if(output.empty())throw std::runtime_error("editor-test requires --out");
    EditorScenario replay(Json::parse(readText(scenario)),output,source,project);return replay.run();
}
} // namespace atlas
