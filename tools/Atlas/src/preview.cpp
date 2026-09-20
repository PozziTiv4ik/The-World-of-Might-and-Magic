#include "app.hpp"
namespace atlas {
// Uses the same chrome and map drawing functions as the window, on a WIC surface for visual review.
Image App::preview(const fs::path &path, int state) {
    map.load(path);
    auto original = map.doc;
    width = state==28||state==29?800:state==9||state==22||state==24?1366:state==10||state==25?1920:state==23?960:1680;
    height = state==28||state==29?500:state==9||state==22||state==24?768:state==10||state==25?1080:state==23?600:945;
    mouse = {-100, -100};
    panel = state == 0 ? 1 : state == 1 ? 4 : state == 2 ? 5 : 0;
    showPanels=state<3||state==26;panel=state==26?4:panel;
    refreshVersions();
    if((state>=6&&state<20)||state==21||state==22||state==23||state==28) {
        historyView=true;compareMode=state==7?1:0;
        historyChain="main";refreshVersions();
        if(versions.empty()){historyChain="archive";refreshVersions();}
        if(!versions.empty()) {
            versionDetails=state==7?versions.front():versions.back();
            comparison=std::make_unique<Map>();comparison->doc=map.version(versionDetails["id"].str())["document"];
            comparison->directory=map.directory;comparison->doc["_comparison_id"]=versionDetails["id"];
            revealVersion(versionDetails["id"].str());
            if(compareMode)buildDiff();
        }
    }
    computeLayout();
    activeLayer = map.vectorLayer();
    fit();
    if(state>=8&&state<20 && comparison && comparison->doc["campaign_event"]["position"].isArray()) {
        auto at=point(comparison->doc["campaign_event"]["position"]);zoom=1.45;
        offset={(canvas.left+canvas.right)/2-at.x*zoom,(canvas.top+canvas.bottom)/2-at.y*zoom};
    }
    for (const auto &[id, f] : map.doc["features"].obj())
        if (state<20 && f["name"].str() == "Бронзовая Орда") {
            selected = {id};
            break;
        }
    if (state == 5) {
        std::string a;
        double longest = 0;
        const Json &f = map.doc["features"]["MAPOBJ-COUNTRY-ETERNAL-SUN"];
        for (const auto &ring : f["rings"].arr())
            for (const auto &r : ring.arr()) {
                const auto &ns = map.doc["arcs"][r["id"].str()]["nodes"];
                double length = 0;
                for (size_t i = 1; i < ns.size(); i++)
                    length += distance(point(map.doc["nodes"][ns[i - 1].str()]),
                                       point(map.doc["nodes"][ns[i].str()]));
                if (length > longest) {
                    longest = length;
                    a = r["id"].str();
                }
            }
        if (!a.empty())
            selectBorder(a);
        zoom *= 2.0;
        offset = {width / 2 - 1850 * zoom, height / 2 - 1550 * zoom};
    }
    if (state == 4) {
        zoom *= 1.8;
        offset = {width / 2 - 1900 * zoom, height / 2 - 1900 * zoom};
    }
    auto result=captureFrame();
    if(historyView && comparison && !compareMode) {
        bool visible=false;auto event=selectedEvent();
        for(const auto &hit:hits)visible |= hit.command==SelectVersion && event && hit.data==event->moment["id"].str();
        if(!visible)throw std::runtime_error("Selected event is outside the visible timeline");
    }
    if (!(map.doc == original))
        throw std::runtime_error("UI preview mutated the map document");
    return result;
}

Image App::captureFrame(float scale) {
    if(scale<.5f || scale>4 || width<1 || height<1 || width*height*scale*scale>33000000)
        throw std::runtime_error("Invalid capture dimensions");
    auto original=map.doc;
    int pixelWidth=int(std::lround(width*scale)),pixelHeight=int(std::lround(height*scale));
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.put())),"Capture WIC");
    Com<IWICBitmap> bitmap;
    check(wic->CreateBitmap(pixelWidth,pixelHeight,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.put()),"Capture pixels");
    Com<ID2D1RenderTarget> rt;
    auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
    properties.dpiX=properties.dpiY=scale*96;
    check(renderer.factory->CreateWicBitmapRenderTarget(bitmap.get(),properties,rt.put()),"Capture target");
    // Cached device resources cannot outlive this offscreen target.
    for(auto &[key,thumb]:thumbnails){thumb->bitmap.reset();thumb->owner=nullptr;}
    rt->BeginDraw();drawFrame(rt.get(),false);check(rt->EndDraw(),"Capture frame");
    Image result(pixelWidth,pixelHeight);
    check(bitmap->CopyPixels(nullptr,pixelWidth*4,UINT(result.bgra.size()),result.bgra.data()),"Capture copy");
    for(auto &[key,thumb]:thumbnails){thumb->bitmap.reset();thumb->owner=nullptr;}
    if(!(map.doc==original))throw std::runtime_error("Rendering modified the map document");
    return result;
}
} // namespace atlas
