#include "app.hpp"
namespace atlas {
// Uses the same chrome and map drawing functions as the window, on a WIC surface for visual review.
Image App::preview(const fs::path &path, int state) {
    map.load(path);
    auto original = map.doc;
    width = 1680;
    height = 945;
    canvas = {0, 44, width, height};
    mouse = {-100, -100};
    panel = state == 0 ? 1 : state == 1 ? 4 : state == 2 ? 5 : 0;
    activeLayer = map.vectorLayer();
    fit();
    for (const auto &[id, f] : map.doc["features"].obj())
        if (f["name"].str() == "Бронзовая Орда") {
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
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())),
          "Preview WIC");
    Com<IWICBitmap> b;
    check(wic->CreateBitmap(UINT(width), UINT(height), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                            b.put()),
          "Preview bitmap");
    Com<ID2D1RenderTarget> rt;
    check(renderer.factory->CreateWicBitmapRenderTarget(
              b.get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), rt.put()),
          "Preview target");
    rt->BeginDraw();
    rt->Clear(color(map.doc["background"].str("#D3E4E2")));
    renderer.drawInteractive(map, rt.get(), canvas, zoom, offset,
                             editScope == SelectionDomain::Borders ? std::set<std::string>{} : selected);
    Painter p(rt.get(), renderer.textFactory.get(), &uiTextCache);
    if (!activeBorder.empty())
        renderer.drawControlEditor(map, rt.get(), canvas, zoom, offset, activeBorder, activeControls,
                                   activeControl, {}, false, {});
    paintChrome(p);
    if (panel >= 1 && panel <= 3) {
        p.rounded({width - 324, 178, width - 32, 206}, "#34434A", 4);
        p.text("Поиск…", {width - 313, 178, width - 44, 206}, 12, "#96A8A9");
    }
    check(rt->EndDraw(), "Preview frame");
    Image result{int(width), int(height)};
    check(b->CopyPixels(nullptr, int(width) * 4, UINT(result.bgra.size()), result.bgra.data()),
          "Preview pixels");
    if (!(map.doc == original))
        throw std::runtime_error("UI preview mutated the map document");
    return result;
}
} // namespace atlas
