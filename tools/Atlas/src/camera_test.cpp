#include "app.hpp"
#include <algorithm>
#include <chrono>
namespace atlas {
Json benchmarkCamera(const fs::path &path, bool borders) {
    Map m;
    m.load(path);
    auto original = m.doc;
    MapRenderer r;
    r.paintBorders = borders;
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())),
          "Camera WIC");
    Com<IWICBitmap> b;
    check(wic->CreateBitmap(1600, 1000, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, b.put()),
          "Camera pixels");
    Com<ID2D1RenderTarget> t;
    check(r.factory->CreateWicBitmapRenderTarget(
              b.get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), t.put()),
          "Camera target");
    auto frame = [&](double z, Point offset, bool moving) {
        auto start = std::chrono::steady_clock::now();
        t->BeginDraw();
        r.drawResponsive(m, t.get(), {0, 0, 1600, 1000}, z, offset, {}, false, moving, nullptr);
        check(t->EndDraw(), "Camera frame");
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    auto settle = [&](double z, Point at) {
        auto start = GetTickCount64();
        do {
            frame(z, at, false);
            if (r.responsiveSettled())
                return double(GetTickCount64() - start);
            Sleep(5);
        } while (GetTickCount64() - start < 10000);
        throw std::runtime_error("Camera worker did not produce the requested view");
    };
    double startup = settle(.35, {40, 20});
    Json scenarios = Json::array();
    for (auto mode : {"large_pan", "zoom_out", "zoom_in"}) {
        std::vector<double> times;
        double z = .35;
        Point at{};
        for (int i = 0; i < 180; i++) {
            if (std::string(mode) == "large_pan") {
                z = .7;
                at = {double(600 - i * 36), double(200 - i * 13)};
            } else {
                z = std::string(mode) == "zoom_out" ? 2.8 / std::pow(1.015, i) : .08 * std::pow(1.025, i);
                at = {800 - 1900 * z, 500 - 1500 * z};
            }
            times.push_back(frame(z, at, true));
        }
        double finish = settle(z, at);
        std::sort(times.begin(), times.end());
        scenarios.push(fields({{"name", mode},
                               {"frames", times.size()},
                               {"median_main_thread_ms", times[times.size() / 2]},
                               {"p95_main_thread_ms", times[size_t(times.size() * .95)]},
                               {"max_main_thread_ms", times.back()},
                               {"refinement_ms", finish}}));
    }
    if (!(m.doc == original))
        throw std::runtime_error("Camera changed document");
    return fields({{"backend", "UI-side asynchronous renderer / Direct2D WIC 1600x1000"},
                   {"initial_ready_ms", startup},
                   {"borders", borders},
                   {"document_unchanged", true},
                   {"scenarios", scenarios}});
}
} // namespace atlas
