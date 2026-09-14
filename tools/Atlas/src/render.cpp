#include "render.hpp"
#include "symbols_data.hpp"
#include <algorithm>
#include <set>
#include <sstream>

namespace atlas {
static void atlasStyle(Image &image) {
    auto original = image.bgra;
    int w = image.width, h = image.height;
    auto blue = [&](size_t i) {
        return original[i] > 210 && original[i + 1] < 90 && original[i + 2] < 65 && original[i + 3] > 128;
    };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t i = (size_t(y) * w + x) * 4;
            if (blue(i)) {
                image.bgra[i] = 104;
                image.bgra[i + 1] = 65;
                image.bgra[i + 2] = 35;
            } else if (original[i] < 90 && original[i + 1] < 70 && original[i + 2] < 70 &&
                       original[i + 3] > 128) {
                bool sea = false;
                for (int dy = -3; dy <= 3 && !sea; dy++)
                    for (int dx = -3; dx <= 3; dx++)
                        if (x + dx >= 0 && x + dx < w && y + dy >= 0 && y + dy < h &&
                            blue((size_t(y + dy) * w + x + dx) * 4)) {
                            sea = true;
                            break;
                        }
                if (sea) {
                    image.bgra[i] = 247;
                    image.bgra[i + 1] = 239;
                    image.bgra[i + 2] = 227;
                }
            }
        }
}
D2D1_COLOR_F color(const std::string &hex, float opacity) {
    if (hex == "none")
        return D2D1::ColorF(0, 0);
    unsigned value = 0x66d4d7;
    try {
        if (hex.size() == 7 && hex[0] == '#')
            value = unsigned(std::stoul(hex.substr(1), nullptr, 16));
    } catch (...) {
    }
    return D2D1::ColorF(value, opacity);
}
std::string hexColor(COLORREF c) {
    char b[8];
    std::snprintf(b, 8, "#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return b;
}
Painter::Painter(ID2D1RenderTarget *t, IDWriteFactory *f, TextCache *cache)
    : textCache(cache ? cache : &localText), target(t), textFactory(f) {
    check(t->CreateSolidColorBrush(D2D1::ColorF(0), brush.put()), "Create brush");
}
ID2D1Brush *Painter::ink(const std::string &c, float opacity) {
    brush->SetColor(color(c, opacity));
    return brush.get();
}
void Painter::fill(D2D1_RECT_F r, const std::string &c, float a) {
    target->FillRectangle(r, ink(c, a));
}
void Painter::rect(D2D1_RECT_F r, const std::string &c, float n) {
    target->DrawRectangle(r, ink(c), n);
}
void Painter::rounded(D2D1_RECT_F r, const std::string &c, float radius) {
    target->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), ink(c));
}
void Painter::line(Point a, Point b, const std::string &c, float n, float opacity) {
    target->DrawLine(D2D1::Point2F(float(a.x), float(a.y)), D2D1::Point2F(float(b.x), float(b.y)),
                     ink(c, opacity), n);
}
void Painter::circle(Point p, float radius, const std::string &c, bool filled) {
    auto ellipse = D2D1::Ellipse(D2D1::Point2F(float(p.x), float(p.y)), radius, radius);
    if (filled)
        target->FillEllipse(ellipse, ink(c));
    else
        target->DrawEllipse(ellipse, ink(c), 1.5f);
}
void Painter::text(const std::string &t, D2D1_RECT_F r, float size, const std::string &c, bool bold,
                   bool center, bool serif) {
    auto s = wide(t);
    auto &formats = textCache->formats;
    int key = int(size * 10) * 8 + (serif ? 4 : 0) + (bold ? 2 : 0) + (center ? 1 : 0);
    if (!formats.contains(key)) {
        Com<IDWriteTextFormat> f;
        check(textFactory->CreateTextFormat(serif ? L"Georgia" : L"Segoe UI", nullptr,
                                            bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                            L"ru-RU", f.put()),
              "Create text format");
        f->SetWordWrapping(serif ? DWRITE_WORD_WRAPPING_WHOLE_WORD : DWRITE_WORD_WRAPPING_NO_WRAP);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (center)
            f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        formats.emplace(key, std::move(f));
    }
    const auto layoutKey = std::to_string(key) + ":" + std::to_string(r.right - r.left) + ":" +
                           std::to_string(r.bottom - r.top) + ":" + t;
    auto &layouts = textCache->layouts;
    if (layouts.size() > 2048)
        layouts.clear();
    if (!layouts.contains(layoutKey)) {
        Com<IDWriteTextLayout> layout;
        check(textFactory->CreateTextLayout(s.data(), UINT32(s.size()), formats.at(key).get(),
                                            std::max(.1f, r.right - r.left), std::max(.1f, r.bottom - r.top),
                                            layout.put()),
              "Text layout");
        layouts.emplace(layoutKey, std::move(layout));
    }
    target->DrawTextLayout(D2D1::Point2F(r.left, r.top), layouts.at(layoutKey).get(), ink(c),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
Json defaultSymbols() {
    static const Json definitions = Json::parse(symbolsData);
    return definitions;
}
MapRenderer::MapRenderer(ID2D1Factory *sharedFactory) {
    if (sharedFactory) {
        sharedFactory->AddRef();
        *factory.put() = sharedFactory;
    } else
        check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()), "Direct2D factory");
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown **>(textFactory.put())),
          "DirectWrite factory");
    auto props = D2D1::StrokeStyleProperties();
    props.startCap = props.endCap = props.dashCap = D2D1_CAP_STYLE_ROUND;
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    check(factory->CreateStrokeStyle(props, nullptr, 0, roundStroke.put()), "Round stroke");
}
void MapRenderer::invalidateScene() {
    borderMap = nullptr;
    borderGroups.clear();
    resetResponsive();
    sceneBitmap.reset();
    sceneTarget.reset();
    sceneMap = nullptr;
    featurePaths.clear();
    if (sceneRenderer)
        sceneRenderer->invalidateScene();
}
void MapRenderer::clear() {
    invalidateScene();
    if (sceneRenderer)
        sceneRenderer->clear();
    geometryMap = nullptr;
    symbolPaths.clear();
    bitmaps.clear();
    order.clear();
    lastTarget = nullptr;
}
ID2D1Bitmap *MapRenderer::bitmap(Map &map, const Json &l, ID2D1RenderTarget *target) {
    if (lastTarget != target) {
        clear();
        lastTarget = target;
    }
    auto style = map.doc["style"].str();
    auto key = l["image"].str() + ":" + l["pdn_layer"].dump(0) + ":" + style;
    if (bitmaps.contains(key))
        return bitmaps.at(key).get();
    auto image = map.raster(l);
    Bytes pixels = image->bgra;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        unsigned a = pixels[i + 3];
        if (a != 255)
            for (int c = 0; c < 3; c++)
                pixels[i + c] = uint8_t((pixels[i + c] * a + 127) / 255);
    }
    Com<ID2D1Bitmap> b;
    check(target->CreateBitmap(
              D2D1::SizeU(image->width, image->height), pixels.data(), image->width * 4,
              D2D1::BitmapProperties(
                  D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96),
              b.put()),
          "Create raster bitmap");
    order.push_back(key);
    bitmaps.emplace(key, std::move(b));
    while (order.size() > 8) {
        bitmaps.erase(order.front());
        order.pop_front();
    }
    return bitmaps.at(key).get();
}
Com<ID2D1PathGeometry> MapRenderer::geometry(const std::vector<std::vector<Point>> &paths, bool closed) {
    Com<ID2D1PathGeometry> g;
    check(factory->CreatePathGeometry(g.put()), "Path geometry");
    Com<ID2D1GeometrySink> sink;
    check(g->Open(sink.put()), "Geometry sink");
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
    for (auto &pts : paths) {
        if (pts.empty())
            continue;
        sink->BeginFigure(D2D1::Point2F(float(pts[0].x), float(pts[0].y)),
                          closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        for (size_t i = 1; i < pts.size(); i++)
            sink->AddLine(D2D1::Point2F(float(pts[i].x), float(pts[i].y)));
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    }
    check(sink->Close(), "Close geometry");
    return g;
}
void MapRenderer::symbol(const Json &def, ID2D1RenderTarget *target, Point at, double size, double angle,
                         const std::string &stroke, float opacity) {
    if (!def["paths"].isArray())
        return;
    const auto key = &def;
    if (!symbolPaths.contains(key)) {
        std::vector<SymbolPart> paths;
        for (auto &path : def["paths"].arr()) {
            std::vector<Point> points;
            for (auto &p : path["points"].arr())
                points.push_back(point(p));
            paths.push_back({geometry({points}, path["closed"].boolean()),
                             path["fill"].str(path["fill"].boolean() ? "currentColor" : "none"),
                             path["stroke"].str("currentColor"),
                             float(path["stroke_width"].num(def["stroke_width"].num(.075)))});
        }
        symbolPaths.emplace(key, std::move(paths));
    }
    D2D1_MATRIX_3X2_F old;
    target->GetTransform(&old);
    target->SetTransform(D2D1::Matrix3x2F::Scale(float(size * .5), float(size * .5)) *
                         D2D1::Matrix3x2F::Rotation(float(angle)) *
                         D2D1::Matrix3x2F::Translation(float(at.x), float(at.y)) * old);
    Painter p(target, textFactory.get(), &textCache);
    for (auto &part : symbolPaths.at(key)) {
        auto resolve = [&](const std::string &c) -> const std::string & {
            return c == "currentColor" ? stroke : c;
        };
        if (part.fill != "none")
            target->FillGeometry(part.geometry.get(), p.ink(resolve(part.fill), opacity));
        if (part.stroke != "none")
            target->DrawGeometry(part.geometry.get(), p.ink(resolve(part.stroke), opacity), part.width,
                                 roundStroke.get());
    }
    target->SetTransform(old);
}
void MapRenderer::feature(Map &map, const Json &f, ID2D1RenderTarget *target, float layerOpacity, double zoom,
                          bool labels, bool drawStroke, bool drawFill) {
    Painter p(target, textFactory.get(), &textCache);
    auto kind = f["kind"].str();
    float opacity = float(f["opacity"].num(1)) * layerOpacity;
    auto stroke = f["stroke"].str("#62D6D4");
    if (kind == "symbol") {
        static const Json fallback = defaultSymbols();
        const auto &defs = map.doc["symbols"].isObject() && map.doc["symbols"].contains(f["symbol_id"].str())
                               ? map.doc["symbols"][f["symbol_id"].str()]
                               : fallback[f["symbol_id"].str()];
        symbol(defs, target, point(f["position"]), f["size"].num(32), f["rotation"].num(), stroke, opacity);
    } else if (kind != "label") {
        auto geom = featureGeometry(map, f);
        if (drawFill && f["closed"].boolean() && f["fill"].str() != "none")
            target->FillGeometry(geom, p.ink(f["fill"].str(), opacity));
        if (drawStroke && kind == "route") {
            Com<ID2D1StrokeStyle> dash;
            auto props = D2D1::StrokeStyleProperties();
            props.dashStyle = D2D1_DASH_STYLE_DASH;
            check(factory->CreateStrokeStyle(props, nullptr, 0, dash.put()), "Dashed route");
            target->DrawGeometry(geom, p.ink(stroke, opacity), float(f["stroke_width"].num(3)), dash.get());
        } else if (drawStroke)
            target->DrawGeometry(geom, p.ink(stroke, opacity), float(f["stroke_width"].num(3)));
        if (drawFill && kind == "zone") {
            D2D1_RECT_F bounds;
            geom->GetBounds(nullptr, &bounds);
            Com<ID2D1Layer> layer;
            check(target->CreateLayer(layer.put()), "Zone clip");
            auto params = D2D1::LayerParameters();
            params.geometricMask = geom;
            target->PushLayer(params, layer.get());
            float step = 20;
            for (float x = bounds.left - (bounds.bottom - bounds.top); x < bounds.right; x += step)
                p.line({x, bounds.bottom}, {x + bounds.bottom - bounds.top, bounds.top}, stroke, 2, opacity);
            target->PopLayer();
        }
    }
    auto name = f["label_text"].str(f["name"].str());
    if (f["role"].str() == "country_label" && map.doc["features"].contains(f["parent_id"].str())) {
        auto parentName = map.doc["features"][f["parent_id"].str()]["name"].str();
        if (parentName != f["name"].str())
            name = parentName;
    }
    if (labels && !name.empty() && f["show_label"].boolean(true) && zoom >= f["min_zoom"].num() &&
        (f["role"].str() != "settlement_label" || f["font_size"].num(14) * zoom >= 10)) {
        auto a = map.anchor(f), shift = point(f["label_offset"]);
        a.x += shift.x;
        a.y += shift.y;
        float size = float(f["font_size"].num(24));
        bool cartographic = map.doc["style"].str() == "cartographic";
        float width = float(f["label_width"].num(std::max(100.0, double(wide(name).size()) * size * .6)));
        float height = float(f["label_height"].num(size * 2));
        if (cartographic && f["role"].str() == "country_label" && zoom < .38) {
            if (map.doc["features"].contains(f["parent_id"].str()))
                name = map.doc["features"][f["parent_id"].str()]["name"].str(name);
            for (auto prefix :
                 {std::string("Королевство "), std::string("Царство "), std::string("Республика ")})
                if (name.starts_with(prefix)) {
                    name = name.substr(prefix.size());
                    break;
                }
            size = float(11.5 / std::max(.08, zoom));
            width = float(140 / std::max(.08, zoom));
            height = float(34 / std::max(.08, zoom));
        }
        auto rect = D2D1::RectF(float(a.x) - width / 2, float(a.y) - height / 2, float(a.x) + width / 2,
                                float(a.y) + height / 2);
        if (cartographic && zoom > .65) {
            for (auto delta : std::vector<Point>{{0, 1}}) {
                auto halo = rect;
                halo.left += float(delta.x);
                halo.right += float(delta.x);
                halo.top += float(delta.y);
                halo.bottom += float(delta.y);
                p.text(name, halo, size, "#EFEAD9", false, true, true);
            }
        }
        p.text(name, rect, size, f["text_color"].str("#514D43"), !cartographic && kind == "region", true,
               cartographic);
    }
}
void MapRenderer::draw(Map &map, ID2D1RenderTarget *target, D2D1_RECT_F viewport, double zoom, Point offset,
                       const std::set<std::string> &selected, bool nodes, bool labels) {
    target->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_MATRIX_3X2_F old;
    target->GetTransform(&old);
    auto transform = D2D1::Matrix3x2F::Scale(float(zoom), float(zoom)) *
                     D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y));
    target->SetTransform(transform);
    Painter p(target, textFactory.get(), &textCache);
    p.fill(D2D1::RectF(float((viewport.left - offset.x) / zoom), float((viewport.top - offset.y) / zoom),
                       float((viewport.right - offset.x) / zoom), float((viewport.bottom - offset.y) / zoom)),
           map.doc["background"].str("#234168"));
    bool blended = false;
    bool hasRaster = false, interleaved = false, seenVector = false;
    Json rasterLayers = Json::array();
    for (const auto &l : map.doc["layers"].arr())
        if (l["visible"].boolean(true) && l["blend_mode"].num() != 0)
            blended = true;
    for (const auto &l : map.doc["layers"].arr())
        if (l["visible"].boolean(true)) {
            if (l["kind"].str() == "raster") {
                hasRaster = true;
                rasterLayers.push(l);
                if (seenVector)
                    interleaved = true;
            } else if (!map.orderedFeatures(l["id"].str()).empty())
                seenVector = true;
        }
    bool styled = map.doc["style"].str() != "original" && hasRaster;
    // Stable, scale-dependent label layout. Order is independent of the viewport/overscan,
    // so panning cannot make neighbouring captions jump in and out of the collision set.
    std::set<std::string> hiddenLabels;
    if (map.doc["style"].str() == "cartographic" && labels) {
        std::vector<std::pair<double, const Json *>> captions;
        for (const auto &[id, f] : map.doc["features"].obj())
            if (f["role"].str() == "country_label") {
                double priority = 0;
                const Json &parent = map.doc["features"][f["parent_id"].str()];
                if (parent.isObject()) {
                    D2D1_RECT_F b;
                    featureGeometry(map, parent)->GetBounds(nullptr, &b);
                    priority = (b.right - b.left) * (b.bottom - b.top);
                }
                captions.push_back({priority, &f});
            }
        std::stable_sort(captions.begin(), captions.end(),
                         [](auto &a, auto &b) { return a.first > b.first; });
        std::vector<D2D1_RECT_F> occupied;
        for (auto &[rank, ptr] : captions) {
            const Json &f = *ptr;
            auto at = point(f["position"]);
            double w = zoom < .38 ? 140 : f["label_width"].num(300) * zoom,
                   h = zoom < .38 ? 34 : f["label_height"].num(80) * zoom;
            D2D1_RECT_F b{float(at.x * zoom - w / 2), float(at.y * zoom - h / 2), float(at.x * zoom + w / 2),
                          float(at.y * zoom + h / 2)};
            bool overlap = false;
            for (auto &a : occupied)
                if (b.left < a.right + 4 && b.right > a.left - 4 && b.top < a.bottom + 3 &&
                    b.bottom > a.top - 3) {
                    overlap = true;
                    break;
                }
            if (overlap)
                hiddenLabels.insert(f["id"].str());
            else
                occupied.push_back(b);
        }
    }
    if (styled && interleaved)
        blended = true;
    if (styled && !blended) {
        if (lastTarget != target) {
            clear();
            lastTarget = target;
        }
        auto key = "raster-base:" + hashText(rasterLayers.dump(0));
        if (!bitmaps.contains(key)) {
            auto im = rasterBase(map);
            for (size_t i = 0; i < im.bgra.size(); i += 4)
                for (int c = 0; c < 3; c++)
                    im.bgra[i + c] = uint8_t((unsigned(im.bgra[i + c]) * im.bgra[i + 3] + 127) / 255);
            Com<ID2D1Bitmap> b;
            check(
                target->CreateBitmap(
                    D2D1::SizeU(im.width, im.height), im.bgra.data(), im.width * 4,
                    D2D1::BitmapProperties(
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96),
                    b.put()),
                "Styled raster base");
            bitmaps.emplace(key, std::move(b));
            order.push_back(key);
            while (order.size() > 8) {
                bitmaps.erase(order.front());
                order.pop_front();
            }
        }
        target->DrawBitmap(bitmaps.at(key).get(),
                           D2D1::RectF(0, 0, float(map.doc["width"].num()), float(map.doc["height"].num())));
    }
    if (blended) {
        if (lastTarget != target) {
            clear();
            lastTarget = target;
        }
        auto key = "flatten:" + hashText(map.doc.dump(0));
        if (!bitmaps.contains(key)) {
            auto im = flatten(map);
            auto pixels = std::move(im.bgra);
            for (size_t i = 0; i < pixels.size(); i += 4)
                for (int c = 0; c < 3; c++)
                    pixels[i + c] = uint8_t((unsigned(pixels[i + c]) * pixels[i + 3] + 127) / 255);
            Com<ID2D1Bitmap> b;
            check(
                target->CreateBitmap(
                    D2D1::SizeU(im.width, im.height), pixels.data(), im.width * 4,
                    D2D1::BitmapProperties(
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96),
                    b.put()),
                "Blended map bitmap");
            bitmaps.emplace(key, std::move(b));
            order.push_back(key);
            while (order.size() > 8) {
                bitmaps.erase(order.front());
                order.pop_front();
            }
        }
        target->DrawBitmap(bitmaps.at(key).get(),
                           D2D1::RectF(0, 0, float(map.doc["width"].num()), float(map.doc["height"].num())));
    }
    Com<ID2D1GeometryGroup> landMask;
    if (static_cast<const Json &>(map.doc)["land_state_separated"].boolean()) {
        std::vector<ID2D1Geometry *> lands;
        for (const auto &[id, f] : map.doc["features"].obj())
            if (f["role"].str() == "land")
                lands.push_back(featureGeometry(map, f));
        if (!lands.empty())
            check(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, lands.data(), UINT32(lands.size()),
                                               landMask.put()),
                  "Land clip");
    }
    if (!blended)
        for (const auto &l : map.doc["layers"].arr()) {
            if (!l["visible"].boolean(true))
                continue;
            if (l["kind"].str() == "raster") {
                if (styled)
                    continue;
                auto b = bitmap(map, l, target);
                target->DrawBitmap(
                    b, D2D1::RectF(0, 0, float(map.doc["width"].num()), float(map.doc["height"].num())),
                    float(l["opacity"].num(1)),
                    zoom > 2 ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                             : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                for (auto ptr : map.orderedFeatures(l["id"].str())) {
                    const auto &f = *ptr;
                    if (hiddenLabels.contains(f["id"].str()))
                        continue;
                    if (f["position"].isArray()) {
                        if (f["kind"].str() == "label" &&
                            (!labels || zoom < f["min_zoom"].num() || !f["show_label"].boolean(true)))
                            continue;
                        auto at = point(f["position"]), shift = point(f["label_offset"]);
                        double radius = f["size"].num(24);
                        if (labels && f["show_label"].boolean(true))
                            radius =
                                std::max({radius, 140 / zoom, f["label_width"].num(0),
                                          double(wide(f["name"].str()).size()) * f["font_size"].num(24)});
                        radius += std::abs(shift.x) + std::abs(shift.y);
                        if ((at.x + radius) * zoom + offset.x < viewport.left ||
                            (at.x - radius) * zoom + offset.x > viewport.right ||
                            (at.y + radius) * zoom + offset.y < viewport.top ||
                            (at.y - radius) * zoom + offset.y > viewport.bottom)
                            continue;
                    }
                    float opacity = float(l["opacity"].num(1));
                    if (f["role"].str() == "country" &&
                        static_cast<const Json &>(map.doc)["land_state_separated"].boolean()) {
                        if (f["territory_scope"].str() == "land_sea")
                            feature(map, f, target, opacity * .18f, zoom, false, false, true);
                        if (landMask) {
                            Com<ID2D1Layer> clip;
                            check(target->CreateLayer(clip.put()), "Country fill");
                            auto params = D2D1::LayerParameters();
                            params.geometricMask = landMask.get();
                            target->PushLayer(params, clip.get());
                            feature(map, f, target, opacity, zoom, false, false, true);
                            target->PopLayer();
                        }
                        feature(map, f, target, opacity, zoom, labels, false, false);
                    } else
                        feature(map, f, target, opacity, zoom, labels, f["role"].str() != "country");
                }
            }
        }
    target->SetTransform(old);
    target->PopAxisAlignedClip();
    if (paintBorders)
        drawBorders(map, target, viewport, zoom, offset);
    selection(map, target, viewport, zoom, offset, selected, nodes);
}
void MapRenderer::selection(Map &map, ID2D1RenderTarget *target, D2D1_RECT_F viewport, double zoom,
                            Point offset, const std::set<std::string> &selected, bool nodes) {
    if (selected.empty())
        return;
    target->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_MATRIX_3X2_F old;
    target->GetTransform(&old);
    target->SetTransform(D2D1::Matrix3x2F::Scale(float(zoom), float(zoom)) *
                         D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y)));
    Painter p(target, textFactory.get(), &textCache);
    for (auto &id : selected) {
        if (!map.doc["features"].contains(id))
            continue;
        const auto &f = map.doc["features"][id];
        if (f["position"].isArray()) {
            auto a = point(f["position"]);
            p.circle(a, float(std::max(10.0, f["size"].num(24) * .7)), "#52E4E1", false);
        } else {
            auto g = featureGeometry(map, f);
            target->DrawGeometry(g, p.ink("#42E6E4"), float(2 / zoom));
        }
        if (nodes)
            for (auto &n : map.featureNodes(f)) {
                auto a = point(map.doc["nodes"][n]);
                if (a.x * zoom + offset.x < viewport.left - 8 || a.x * zoom + offset.x > viewport.right + 8 ||
                    a.y * zoom + offset.y < viewport.top - 8 || a.y * zoom + offset.y > viewport.bottom + 8)
                    continue;
                float r = float(3.5 / zoom);
                auto box = D2D1::RectF(float(a.x) - r, float(a.y) - r, float(a.x) + r, float(a.y) + r);
                p.fill(box, "#FFFFFF");
                p.rect(box, "#125B6A", float(1 / zoom));
            }
    }
    target->SetTransform(old);
    target->PopAxisAlignedClip();
}
ID2D1PathGeometry *MapRenderer::featureGeometry(Map &map, const Json &f) {
    if (geometryMap != &map) {
        featurePaths.clear();
        symbolPaths.clear();
        geometryMap = &map;
    }
    if (!featurePaths.contains(&f))
        featurePaths.emplace(&f, geometry(map.paths(f), f["closed"].boolean()));
    return featurePaths.at(&f).get();
}
void MapRenderer::drawInteractive(Map &map, ID2D1RenderTarget *target, D2D1_RECT_F viewport, double zoom,
                                  Point offset, const std::set<std::string> &selected, bool nodes,
                                  bool cameraMoving) {
    if (zoom <= 0 || viewport.right <= viewport.left || viewport.bottom <= viewport.top)
        return;
    float dx, dy;
    target->GetDpi(&dx, &dy);
    auto visible =
        D2D1::RectF(float((viewport.left - offset.x) / zoom), float((viewport.top - offset.y) / zoom),
                    float((viewport.right - offset.x) / zoom), float((viewport.bottom - offset.y) / zoom));
    bool zoomValid = sceneZoom == zoom ||
                     (cameraMoving && sceneZoom > 0 && zoom / sceneZoom >= .5 && zoom / sceneZoom <= 2);
    bool valid = sceneBitmap && sceneMap == &map && sceneOwner == target && zoomValid && dx == sceneDpiX &&
                 dy == sceneDpiY && visible.left >= sceneBounds.left && visible.top >= sceneBounds.top &&
                 visible.right <= sceneBounds.right && visible.bottom <= sceneBounds.bottom;
    if (!valid) {
        ++sceneBuilds;
        sceneBitmap.reset();
        sceneTarget.reset();
        float margin = 192;
        float w = std::ceil(viewport.right - viewport.left + margin * 2),
              h = std::ceil(viewport.bottom - viewport.top + margin * 2);
        // Bound the transient view surface at high monitor DPI; the map itself stays vector data.
        if (double(w) * h * (dx / 96) * (dy / 96) > 16000000) {
            margin = 0;
            w = std::ceil(viewport.right - viewport.left);
            h = std::ceil(viewport.bottom - viewport.top);
        }
        check(target->CreateCompatibleRenderTarget(D2D1::SizeF(w, h), sceneTarget.put()), "View cache");
        auto origin = Point{visible.left - margin / zoom, visible.top - margin / zoom};
        sceneBounds = D2D1::RectF(float(origin.x), float(origin.y), float(origin.x + w / zoom),
                                  float(origin.y + h / zoom));
        if (!sceneRenderer) {
            sceneRenderer = std::make_unique<MapRenderer>(factory.get());
        }
        sceneTarget->BeginDraw();
        sceneTarget->Clear(color("#D9E1D6"));
        sceneRenderer->draw(map, sceneTarget.get(), D2D1::RectF(0, 0, w, h), zoom,
                            {-origin.x * zoom, -origin.y * zoom}, {}, false, displayLabels);
        check(sceneTarget->EndDraw(), "Prepare view");
        check(sceneTarget->GetBitmap(sceneBitmap.put()), "View bitmap");
        sceneMap = &map;
        sceneOwner = target;
        sceneZoom = zoom;
        sceneDpiX = dx;
        sceneDpiY = dy;
    } else
        ++sceneHits;
    target->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    target->DrawBitmap(
        sceneBitmap.get(),
        D2D1::RectF(float(sceneBounds.left * zoom + offset.x), float(sceneBounds.top * zoom + offset.y),
                    float(sceneBounds.right * zoom + offset.x), float(sceneBounds.bottom * zoom + offset.y)),
        1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    target->PopAxisAlignedClip();
    selection(map, target, viewport, zoom, offset, selected, nodes);
}
void MapRenderer::drawDragPreview(Map &map, ID2D1RenderTarget *target, D2D1_RECT_F viewport, double zoom,
                                  Point offset, const std::set<std::string> &selected, Point delta,
                                  const std::string &node) {
    target->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_MATRIX_3X2_F old;
    target->GetTransform(&old);
    target->SetTransform(
        D2D1::Matrix3x2F::Scale(float(zoom), float(zoom)) *
        D2D1::Matrix3x2F::Translation(float(offset.x + (node.empty() ? delta.x * zoom : 0)),
                                      float(offset.y + (node.empty() ? delta.y * zoom : 0))));
    Painter p(target, textFactory.get(), &textCache);
    for (const auto &id : selected) {
        const Json &f = map.doc["features"][id];
        if (f["position"].isArray()) {
            feature(map, f, target, .75f, zoom, displayLabels);
        } else if (node.empty()) {
            auto g = featureGeometry(map, f);
            if (f["closed"].boolean())
                target->FillGeometry(g, p.ink(f["fill"].str("#62C7C5"), .32f));
            target->DrawGeometry(g, p.ink("#138E98"), float(2 / zoom));
        } else {
            auto paths = map.paths(f);
            auto origin = point(map.doc["nodes"][node]);
            for (auto &ring : paths)
                for (auto &at : ring)
                    if (distance(at, origin) < 1e-8) {
                        at.x += delta.x;
                        at.y += delta.y;
                    }
            auto g = geometry(paths, f["closed"].boolean());
            target->DrawGeometry(g.get(), p.ink("#138E98"), float(2 / zoom));
            p.circle({origin.x + delta.x, origin.y + delta.y}, float(5 / zoom), "#138E98");
        }
    }
    target->SetTransform(old);
    target->PopAxisAlignedClip();
}
Image MapRenderer::rasterBase(Map &map) {
    Image image(int(map.doc["width"].num()), int(map.doc["height"].num()));
    for (const auto &l : map.doc["layers"].arr())
        if (l["visible"].boolean(true) && l["kind"].str() == "raster")
            composite(image, *map.raster(l), l["opacity"].num(1), int(l["blend_mode"].num()));
    if (map.doc["style"].str() != "original")
        atlasStyle(image);
    return image;
}
Image MapRenderer::flatten(Map &map) {
    Image result(int(map.doc["width"].num()), int(map.doc["height"].num()));
    auto bg = color(map.doc["background"].str());
    for (size_t i = 0; i < result.bgra.size(); i += 4) {
        result.bgra[i] = uint8_t(bg.b * 255);
        result.bgra[i + 1] = uint8_t(bg.g * 255);
        result.bgra[i + 2] = uint8_t(bg.r * 255);
        result.bgra[i + 3] = uint8_t(bg.a * 255);
    }
    MapRenderer worker;
    for (const auto &l : map.doc["layers"].arr()) {
        if (!l["visible"].boolean(true))
            continue;
        std::shared_ptr<Image> source;
        if (l["kind"].str() == "raster") {
            source = std::make_shared<Image>(*map.raster(l));
        } else {
            Map isolated;
            isolated.doc = map.doc;
            isolated.directory = map.directory;
            auto single = l;
            single["opacity"] = 1;
            single["blend_mode"] = 0;
            isolated.doc["layers"] = Json::Array{single};
            isolated.doc["background"] = "none";
            isolated.doc["style"] = "original";
            source = std::make_shared<Image>(worker.renderImage(isolated));
        }
        composite(result, *source, l["opacity"].num(1), int(l["blend_mode"].num()));
    }
    if (map.doc["style"].str() != "original")
        atlasStyle(result);
    return result;
}
Image MapRenderer::renderImage(Map &map, int w, int h) {
    if (!w)
        w = int(map.doc["width"].num());
    if (!h)
        h = int(map.doc["height"].num());
    Image out(w, h);
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())),
          "WIC");
    Com<IWICBitmap> b;
    check(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, b.put()),
          "Render bitmap");
    Com<ID2D1RenderTarget> rt;
    check(factory->CreateWicBitmapRenderTarget(
              b.get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), rt.put()),
          "PNG render target");
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0));
    draw(map, rt.get(), D2D1::RectF(0, 0, float(w), float(h)), double(w) / map.doc["width"].num(), {0, 0});
    check(rt->EndDraw(), "Render PNG");
    check(b->CopyPixels(nullptr, w * 4, UINT(out.bgra.size()), out.bgra.data()), "Read rendered image");
    for (size_t i = 0; i < out.bgra.size(); i += 4) {
        unsigned a = out.bgra[i + 3];
        if (a && a < 255)
            for (int c = 0; c < 3; c++)
                out.bgra[i + c] = uint8_t(std::min(255u, (unsigned(out.bgra[i + c]) * 255 + a / 2) / a));
    }
    clear();
    return out;
}
static std::string xml(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}
static std::string base64(const Bytes &b) {
    static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    for (size_t i = 0; i < b.size(); i += 3) {
        unsigned v = unsigned(b[i]) << 16;
        if (i + 1 < b.size())
            v |= unsigned(b[i + 1]) << 8;
        if (i + 2 < b.size())
            v |= b[i + 2];
        s += table[(v >> 18) & 63];
        s += table[(v >> 12) & 63];
        s += i + 1 < b.size() ? table[(v >> 6) & 63] : '=';
        s += i + 2 < b.size() ? table[v & 63] : '=';
    }
    return s;
}
void MapRenderer::exportSvg(Map &map, const fs::path &file) {
    std::ostringstream out;
    int w = int(map.doc["width"].num()), h = int(map.doc["height"].num());
    bool hasRaster = false, interleaved = false, seenVector = false, blended = false;
    for (const auto &l : map.doc["layers"].arr())
        if (l["visible"].boolean(true)) {
            if (l["blend_mode"].num() != 0)
                blended = true;
            if (l["kind"].str() == "raster") {
                hasRaster = true;
                if (seenVector)
                    interleaved = true;
            } else if (!map.orderedFeatures(l["id"].str()).empty())
                seenVector = true;
        }
    bool styled = map.doc["style"].str() != "original" && hasRaster;
    if (blended || (styled && interleaved)) {
        auto image = renderImage(map);
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << w << "\" height=\"" << h
            << "\"><desc>Flattened to preserve layer blending.</desc><image width=\"" << w << "\" height=\""
            << h << "\" href=\"data:image/png;base64," << base64(encodePng(image)) << "\"/></svg>";
        atomicText(file, out.str());
        return;
    }
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << w << "\" height=\"" << h
        << "\" viewBox=\"0 0 " << w << " " << h << "\"><title>" << xml(map.doc["name"].str())
        << "</title><rect width=\"100%\" height=\"100%\" fill=\"" << xml(map.doc["background"].str())
        << "\"/>\n";
    if (static_cast<const Json &>(map.doc)["land_state_separated"].boolean()) {
        out << "<defs><clipPath id=\"atlas-land-clip\">";
        for (const auto &[id, f] : map.doc["features"].obj())
            if (f["role"].str() == "land") {
                out << "<path clip-rule=\"evenodd\" d=\"";
                for (const auto &ring : map.paths(f))
                    if (!ring.empty()) {
                        out << "M " << ring[0].x << "," << ring[0].y;
                        for (size_t i = 1; i < ring.size(); i++)
                            out << " L " << ring[i].x << "," << ring[i].y;
                        out << " Z ";
                    }
                out << "\"/>";
            }
        out << "</clipPath></defs>\n";
    }
    if (styled) {
        auto image = rasterBase(map);
        out << "<image id=\"atlas-raster-base\" width=\"" << w << "\" height=\"" << h
            << "\" href=\"data:image/png;base64," << base64(encodePng(image)) << "\"/>\n";
    }
    for (const auto &l : map.doc["layers"].arr()) {
        if (!l["visible"].boolean(true))
            continue;
        if (styled && l["kind"].str() == "raster")
            continue;
        out << "<g id=\"" << xml(l["id"].str()) << "\" opacity=\"" << l["opacity"].num(1) << "\"";
        out << ">\n";
        if (l["kind"].str() == "raster") {
            auto image = map.raster(l);
            Image styled = *image;
            if (map.doc["style"].str() != "original")
                for (size_t i = 0; i < styled.bgra.size(); i += 4)
                    if (styled.bgra[i] > 210 && styled.bgra[i + 1] < 90 && styled.bgra[i + 2] < 65) {
                        styled.bgra[i] = 104;
                        styled.bgra[i + 1] = 65;
                        styled.bgra[i + 2] = 35;
                    }
            out << "<image width=\"" << w << "\" height=\"" << h << "\" href=\"data:image/png;base64,"
                << base64(encodePng(styled)) << "\"/>\n";
        } else
            for (auto ptr : map.orderedFeatures(l["id"].str())) {
                const auto &f = *ptr;
                auto id = f["id"].str();
                if (f["layer_id"].str() != l["id"].str())
                    continue;
                out << "<g id=\"" << xml(id) << "\" opacity=\"" << f["opacity"].num(1) << "\"><title>"
                    << xml(f["entity_id"].str() + " " + f["name"].str()) << "</title>";
                auto path = [&](const std::vector<Point> &pts, bool close, std::string fill,
                                std::string stroke, double width) {
                    if (pts.empty())
                        return;
                    out << "<path d=\"M " << pts[0].x << "," << pts[0].y;
                    for (size_t i = 1; i < pts.size(); i++)
                        out << " L " << pts[i].x << "," << pts[i].y;
                    if (close)
                        out << " Z";
                    out << "\" fill=\"" << xml(fill) << "\" stroke=\"" << xml(stroke) << "\" stroke-width=\""
                        << width << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>";
                };
                if (f["role"].str() == "country") {
                    std::ostringstream d;
                    for (const auto &pts : map.paths(f))
                        if (!pts.empty()) {
                            d << "M " << pts[0].x << "," << pts[0].y;
                            for (size_t i = 1; i < pts.size(); i++)
                                d << " L " << pts[i].x << "," << pts[i].y;
                            d << " Z ";
                        }
                    bool separated = static_cast<const Json &>(map.doc)["land_state_separated"].boolean();
                    if (separated && f["territory_scope"].str() == "land_sea")
                        out << "<path fill-rule=\"evenodd\" fill=\"" << xml(f["fill"].str())
                            << "\" opacity=\"0.18\" stroke=\"none\" d=\"" << d.str() << "\"/>";
                    out << "<path fill-rule=\"evenodd\" fill=\"" << xml(f["fill"].str())
                        << "\" stroke=\"none\"";
                    if (separated)
                        out << " clip-path=\"url(#atlas-land-clip)\"";
                    out << " d=\"" << d.str() << "\"/>";
                } else if (f["kind"].str() == "symbol") {
                    auto def = map.doc["symbols"].contains(f["symbol_id"].str())
                                   ? map.doc["symbols"][f["symbol_id"].str()]
                                   : defaultSymbols()[f["symbol_id"].str()];
                    auto a = point(f["position"]);
                    out << "<g transform=\"translate(" << a.x << "," << a.y << ") rotate("
                        << f["rotation"].num() << ") scale(" << f["size"].num(24) * .5 << ")\">";
                    if (def["paths"].isArray())
                        for (auto &p : def["paths"].arr()) {
                            std::vector<Point> pts;
                            for (auto &j : p["points"].arr())
                                pts.push_back(point(j));
                            auto ink = f["stroke"].str();
                            auto resolve = [&](std::string c) { return c == "currentColor" ? ink : c; };
                            path(pts, p["closed"].boolean(),
                                 resolve(p["fill"].str(p["fill"].boolean() ? ink : "none")),
                                 resolve(p["stroke"].str("currentColor")),
                                 p["stroke_width"].num(def["stroke_width"].num(.075)));
                        }
                    out << "</g>";
                } else {
                    auto paths = map.paths(f);
                    out << "<path fill-rule=\"evenodd\" fill=\""
                        << xml(f["closed"].boolean() ? f["fill"].str() : "none") << "\" stroke=\""
                        << xml(f["stroke"].str()) << "\" stroke-width=\"" << f["stroke_width"].num(2)
                        << "\" d=\"";
                    for (auto &pts : paths)
                        if (!pts.empty()) {
                            out << "M " << pts[0].x << "," << pts[0].y;
                            for (size_t i = 1; i < pts.size(); i++)
                                out << " L " << pts[i].x << "," << pts[i].y;
                            if (f["closed"].boolean())
                                out << " Z ";
                        }
                    out << "\"/>";
                }
                if (!f["name"].str().empty() && f["show_label"].boolean(true)) {
                    auto a = map.anchor(f), d = point(f["label_offset"]);
                    auto label = f["label_text"].str(f["name"].str());
                    if (f["role"].str() == "country_label" &&
                        map.doc["features"].contains(f["parent_id"].str())) {
                        auto name = map.doc["features"][f["parent_id"].str()]["name"].str();
                        if (name != f["name"].str())
                            label = name;
                    }
                    std::vector<std::string> lines;
                    std::istringstream stream(label);
                    std::string line;
                    while (std::getline(stream, line))
                        lines.push_back(line);
                    double size = f["font_size"].num(24), step = size * 1.18;
                    out << "<text text-anchor=\"middle\" font-family=\""
                        << (map.doc["style"].str() == "cartographic" ? "Georgia, serif"
                                                                     : "Segoe UI, sans-serif")
                        << "\" font-size=\"" << size << "\" fill=\"" << xml(f["text_color"].str("#514D43"))
                        << "\" stroke=\"#EFEAD9\" stroke-width=\"2\" paint-order=\"stroke fill\">";
                    for (size_t i = 0; i < lines.size(); i++)
                        out << "<tspan x=\"" << a.x + d.x << "\" y=\""
                            << a.y + d.y + (double(i) - (lines.size() - 1) * .5) * step + size * .35 << "\">"
                            << xml(lines[i]) << "</tspan>";
                    out << "</text>";
                }
                out << "</g>\n";
            }
        out << "</g>\n";
    }
    out << "<g id=\"atlas-political-borders\" fill=\"none\" stroke-linejoin=\"round\" "
           "stroke-linecap=\"round\">\n";
    std::set<std::string> borderIds;
    for (const auto &l : map.doc["layers"].arr())
        if (l["visible"].boolean(true))
            for (const auto *f : map.orderedFeatures(l["id"].str()))
                if ((*f)["role"].str() == "country") {
                    auto emit = [&](const Json &refs) {
                        if (!refs.isArray())
                            return;
                        for (const auto &r : refs.arr()) {
                            auto aid = r["id"].str();
                            if (!borderIds.insert(aid).second)
                                continue;
                            const auto &ns = static_cast<const Json &>(map.doc)["arcs"][aid]["nodes"];
                            out << "<path data-arc-id=\"" << xml(aid) << "\" stroke=\""
                                << xml((*f)["stroke"].str("#657573")) << "\" stroke-width=\""
                                << std::max(1.1, (*f)["stroke_width"].num(2)) << "\" opacity=\""
                                << l["opacity"].num(1) * (*f)["opacity"].num(1) << "\" d=\"";
                            for (size_t i = 0; i < ns.size(); i++) {
                                auto p = point(static_cast<const Json &>(map.doc)["nodes"][ns[i].str()]);
                                out << (i ? " L " : "M ") << p.x << "," << p.y;
                            }
                            out << "\"/>";
                        }
                    };
                    emit((*f)["arcs"]);
                    if ((*f)["rings"].isArray())
                        for (const auto &r : (*f)["rings"].arr())
                            emit(r);
                }
    out << "</g>\n";
    out << "</svg>\n";
    atomicText(file, out.str());
}
} // namespace atlas
