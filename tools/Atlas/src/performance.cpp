#include "app.hpp"
#include <algorithm>
#include <chrono>

namespace atlas {
Json benchmarkMap(const fs::path &path) {
    auto now = [] { return std::chrono::steady_clock::now(); };
    auto ms = [&](auto start) { return std::chrono::duration<double, std::milli>(now() - start).count(); };
    auto start = now();
    Map map;
    map.load(path);
    double loadMs = ms(start);
    auto initial = hashText(map.doc.dump());
    MapRenderer renderer;
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())),
          "WIC");
    Com<IWICBitmap> bitmap;
    check(wic->CreateBitmap(1280, 900, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.put()),
          "benchmark bitmap");
    Com<ID2D1RenderTarget> rt;
    check(renderer.factory->CreateWicBitmapRenderTarget(
              bitmap.get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), rt.put()),
          "benchmark target");
    Json result = fields({{"backend", "Direct2D software / WIC"},
                          {"width", 1280},
                          {"height", 900},
                          {"load_ms", loadMs},
                          {"features", map.doc["features"].size()},
                          {"scenarios", Json::array()}});
    std::set<std::string> selection;
    for (const auto &[id, f] : map.doc["features"].obj())
        if (f["name"].str() == "Империя Вечного Солнца" && f["kind"].str() == "region") {
            selection.insert(id);
            break;
        }
    for (auto mode : {"hover", "pan", "zoom_gesture", "zoom_settled", "edit", "country_drag"}) {
        renderer.clear();
        std::vector<double> samples;
        double cold = 0;
        for (int i = 0; i < 13; i++) {
            double zoom = .28;
            Point offset{30, 20};
            std::string scenario = mode;
            if (scenario == "pan")
                offset.x += i * 8;
            if (scenario.starts_with("zoom"))
                zoom *= 1 + i * .025;
            if (scenario == "edit")
                renderer.invalidateScene();
            auto t = now();
            rt->BeginDraw();
            rt->Clear(color("#D9E1D6"));
            renderer.drawInteractive(map, rt.get(), D2D1::RectF(0, 0, 1280, 900), zoom, offset,
                                     scenario == "country_drag" ? selection : std::set<std::string>{}, false,
                                     scenario == "zoom_gesture");
            if (scenario == "country_drag")
                renderer.drawDragPreview(map, rt.get(), D2D1::RectF(0, 0, 1280, 900), zoom, offset, selection,
                                         {double(i * 12), double(i * 5)});
            check(rt->EndDraw(), "benchmark frame");
            double elapsed = ms(t);
            if (i == 0)
                cold = elapsed;
            else
                samples.push_back(elapsed);
        }
        std::sort(samples.begin(), samples.end());
        result["scenarios"].push(fields({{"name", mode},
                                         {"cold_ms", cold},
                                         {"median_ms", samples[samples.size() / 2]},
                                         {"max_ms", samples.back()}}));
    }
    if (hashText(map.doc.dump()) != initial)
        throw std::runtime_error("Benchmark changed map data");
    result["document_unchanged"] = true;
    return result;
}
} // namespace atlas
