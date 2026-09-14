#include "app.hpp"
#include <algorithm>
#include <dwmapi.h>
#include <shellapi.h>
#include <sstream>
#include <windowsx.h>

namespace atlas {
static const char *toolKeys[] = {"V", "N", "P", "R", "U", "B", "S", "T", "E", "H",
                                 "Z", "W", "M", "G", "L", "F", "I", "J", "K"};
static bool contains(D2D1_RECT_F r, Point p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
static std::string editText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return utf8(s);
}
void App::invalidate() {
    if (window)
        InvalidateRect(window, nullptr, FALSE);
}
Point App::world(Point p) const {
    return {(p.x - offset.x) / zoom, (p.y - offset.y) / zoom};
}
bool App::inCanvas(Point p) const {
    if (!contains(canvas, p))
        return false;
    for (const auto &hit : hits)
        if (contains(hit.rect, p))
            return false;
    return true;
}
void App::fit() {
    double w = map.doc["width"].num(4000), h = map.doc["height"].num(3000);
    zoom = std::clamp(std::min((canvas.right - canvas.left - 32) / w, (canvas.bottom - canvas.top - 32) / h),
                      .01, 8.0);
    offset = {canvas.left + (canvas.right - canvas.left - w * zoom) / 2,
              canvas.top + (canvas.bottom - canvas.top - h * zoom) / 2};
    invalidate();
}
void App::fitSelection() {
    if (selected.empty()) {
        fit();
        return;
    }
    double minX = 1e20, minY = 1e20, maxX = -1e20, maxY = -1e20;
    auto add = [&](Point p) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    };
    for (auto &id : selected) {
        const auto &f = map.doc["features"][id];
        if (f["position"].isArray()) {
            auto p = point(f["position"]);
            double r = std::max(90.0, f["size"].num(24) * 2);
            add({p.x - r, p.y - r});
            add({p.x + r, p.y + r});
        } else
            for (auto &ring : map.paths(f))
                for (auto p : ring)
                    add(p);
    }
    if (minX > maxX)
        return;
    zoom = std::clamp(std::min((canvas.right - canvas.left - 90) / std::max(100.0, maxX - minX),
                               (canvas.bottom - canvas.top - 90) / std::max(100.0, maxY - minY)),
                      .04, 3.0);
    offset = {(canvas.left + canvas.right) / 2 - (minX + maxX) * zoom / 2,
              (canvas.top + canvas.bottom) / 2 - (minY + maxY) * zoom / 2};
    invalidate();
}
void App::layout() {
    RECT r;
    GetClientRect(window, &r);
    dpi = GetDpiForWindow(window) / 96.0f;
    width = r.right / dpi;
    height = r.bottom / dpi;
    left = right = 0;
    canvas = D2D1::RectF(0, 44, width, height);
    if (target) {
        target->Resize(D2D1::SizeU(r.right, r.bottom));
        target->SetDpi(dpi * 96, dpi * 96);
    }
    MoveWindow(searchBox, int((width - 324) * dpi), int(178 * dpi), int(292 * dpi), int(28 * dpi), TRUE);
    ShowWindow(searchBox, showPanels && panel >= 1 && panel <= 3 && !historyView ? SW_SHOW : SW_HIDE);
    ShowWindow(nameBox, SW_HIDE);
    invalidate();
}
void App::updateSelection() {
    for (auto it = selected.begin(); it != selected.end();) {
        const auto &features = map.doc["features"];
        auto &f = features[*it];
        auto l = map.layer(f["layer_id"].str());
        if (!f.isObject() || !l || (*l)["locked"].boolean())
            it = selected.erase(it);
        else
            ++it;
    }
    refreshControls();
    syncName = true;
    if (!selected.empty() && map.doc["features"].contains(*selected.begin()))
        SetWindowTextW(nameBox, wide(map.doc["features"][*selected.begin()]["name"].str()).c_str());
    else
        SetWindowTextW(nameBox, L"");
    syncName = false;
    EnableWindow(nameBox, !selected.empty());
    ShowWindow(nameBox, SW_HIDE);
    invalidate();
}
void App::changed(const std::string &label) {
    history.push(before, map.doc, label);
    dirty = true;
    ++editSerial;
    noticeUntil = GetTickCount64() + 2400;
    if (window)
        SetTimer(window, 5, 2400, nullptr);
    status = label;
    renderer.clear();
    painted.reset();
    paintedLayer.clear();
    updateSelection();
    auto title = "АТЛАС 4 — " + map.doc["name"].str() + " *";
    SetWindowTextW(window, wide(title).c_str());
}
void App::paint() {
    PAINTSTRUCT ps;
    BeginPaint(window, &ps);
    try {
        if (!target) {
            RECT r;
            GetClientRect(window, &r);
            auto props = D2D1::RenderTargetProperties();
            props.dpiX = props.dpiY = dpi * 96;
            check(renderer.factory->CreateHwndRenderTarget(
                      props, D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(r.right, r.bottom)),
                      target.put()),
                  "Create editor render target");
        }
        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->Clear(color(map.doc["background"].str("#D3E0DE")));
        hits.clear();
        Painter p(target.get(), renderer.textFactory.get(), &uiTextCache);
        if (historyView && comparison) {
            if (!comparisonRenderer)
                comparisonRenderer = std::make_unique<MapRenderer>(renderer.factory.get());
            float mid = (canvas.left + canvas.right) / 2;
            auto a = canvas;
            a.right = mid - 5;
            a.top += 28;
            auto b = canvas;
            b.left = mid + 5;
            b.top += 28;
            auto fitView = [&](Map &m, MapRenderer &viewRenderer, D2D1_RECT_F r) {
                double z = std::min((r.right - r.left) / m.doc["width"].num(),
                                    (r.bottom - r.top) / m.doc["height"].num());
                viewRenderer.drawResponsive(m, target.get(), r, z,
                                            {r.left + (r.right - r.left - m.doc["width"].num() * z) / 2,
                                             r.top + (r.bottom - r.top - m.doc["height"].num() * z) / 2},
                                            {}, false, false, window);
            };
            fitView(*comparison, *comparisonRenderer, a);
            fitView(map, renderer, b);
            p.text("Сохранённая версия", D2D1::RectF(a.left, canvas.top, a.right, a.top), 13, "#DFE9F0",
                   true);
            p.text("Рабочая копия", D2D1::RectF(b.left, canvas.top, b.right, b.top), 13, "#78DDD8", true);
        } else
            renderer.drawResponsive(map, target.get(), canvas, zoom, offset,
                                    editScope == SelectionDomain::Borders ? std::set<std::string>{}
                                                                          : selected,
                                    tool == Tool::Node && editScope != SelectionDomain::Borders,
                                    panning || GetTickCount64() < cameraMovingUntil, window, false);
        if (dragPreview && !draggingControl)
            renderer.drawDragPreview(map, target.get(), canvas, zoom, offset, selected, dragDelta,
                                     movingNode);
        if (grid && !historyView) {
            target->PushAxisAlignedClip(canvas, D2D1_ANTIALIAS_MODE_ALIASED);
            double step = 100 * zoom;
            if (step >= 12) {
                for (double x = std::fmod(offset.x, step); x < canvas.right; x += step)
                    if (x >= canvas.left)
                        p.line({x, canvas.top}, {x, canvas.bottom}, "#668899", .5f, .3f);
                for (double y = std::fmod(offset.y, step); y < canvas.bottom; y += step)
                    if (y >= canvas.top)
                        p.line({canvas.left, y}, {canvas.right, y}, "#668899", .5f, .3f);
            }
            target->PopAxisAlignedClip();
        }
        if (!drawing.empty()) {
            target->PushAxisAlignedClip(canvas, D2D1_ANTIALIAS_MODE_ALIASED);
            std::vector<Point> pts = drawing;
            if (tool == Tool::Land || tool == Tool::Region || tool == Tool::Route || tool == Tool::River ||
                tool == Tool::Zone)
                pts.push_back(world(mouse));
            for (size_t i = 1; i < pts.size(); i++)
                p.line({pts[i - 1].x * zoom + offset.x, pts[i - 1].y * zoom + offset.y},
                       {pts[i].x * zoom + offset.x, pts[i].y * zoom + offset.y}, painted ? stroke : "#73FFF0",
                       painted ? float(brushSize * zoom) : 2);
            if (!painted)
                for (auto q : drawing)
                    p.circle({q.x * zoom + offset.x, q.y * zoom + offset.y}, 3, "#E2FFFF");
            target->PopAxisAlignedClip();
        }
        if (!selectionMask.empty() && !historyView) {
            target->PushAxisAlignedClip(canvas, D2D1_ANTIALIAS_MODE_ALIASED);
            for (size_t i = 0; i < selectionMask.size(); i++) {
                auto a = selectionMask[i], b = selectionMask[(i + 1) % selectionMask.size()];
                p.line({a.x * zoom + offset.x, a.y * zoom + offset.y},
                       {b.x * zoom + offset.x, b.y * zoom + offset.y}, "#E8FFFF", 1);
            }
            target->PopAxisAlignedClip();
        }
        if (selectionBox || (down && tool == Tool::Rectangle)) {
            auto r =
                D2D1::RectF(float(std::min(dragStart.x, mouse.x)), float(std::min(dragStart.y, mouse.y)),
                            float(std::max(dragStart.x, mouse.x)), float(std::max(dragStart.y, mouse.y)));
            p.fill(r, "#51DDD8", .13f);
            p.rect(r, "#73EEE6");
        }
        if (!historyView) {
            renderer.drawResponsiveBorders(map, target.get(), canvas, zoom, offset);
            if (!activeBorder.empty() && editScope == SelectionDomain::Borders)
                renderer.drawControlEditor(map, target.get(), canvas, zoom, offset, activeBorder,
                                           activeControls, activeControl, controlTarget,
                                           draggingControl && dragPreview, controlPreviewIds);
        }
        paintChrome(p);
        auto hr = target->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            target.reset();
            renderer.clear();
            if (comparisonRenderer)
                comparisonRenderer->clear();
        } else
            check(hr, "Draw editor");
    } catch (const std::exception &e) {
        status = e.what();
        if (target)
            target->EndDraw();
        target.reset();
        renderer.clear();
        if (comparisonRenderer)
            comparisonRenderer->clear();
    }
    EndPaint(window, &ps);
}
std::string App::drawingLayer() {
    auto l = map.layer(activeLayer);
    if (editScope == SelectionDomain::Objects) {
        const Json *read = l;
        if (!read || (*read)["locked"].boolean() || !(*read)["visible"].boolean(true) ||
            (*read)["domain"].str() == "physical" || (*read)["domain"].str() == "political") {
            for (auto it = map.doc["layers"].arr().rbegin(); it != map.doc["layers"].arr().rend(); ++it) {
                const Json &candidate = *it;
                if (candidate["kind"].str() == "vector" && !candidate["locked"].boolean() &&
                    candidate["visible"].boolean(true) && candidate["domain"].str().empty()) {
                    activeLayer = candidate["id"].str();
                    return activeLayer;
                }
            }
            activeLayer = map.addLayer("Объекты");
            return activeLayer;
        }
    }
    if (l && (*l)["kind"].str() == "vector" && !(*l)["locked"].boolean() && (*l)["visible"].boolean(true))
        return activeLayer;
    activeLayer = map.vectorLayer();
    for (const auto &l : map.doc["layers"].arr())
        if (l["domain"].str() == "political" && l["visible"].boolean(true) && !l["locked"].boolean()) {
            activeLayer = l["id"].str();
            break;
        }
    return activeLayer;
}
void App::pointerDown(Point p, bool middle) {
    mouse = p;
    if (!middle) {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (contains(it->rect, p)) {
                auto cmd = it->command;
                auto data = it->data;
                SetFocus(window);
                if (cmd)
                    command(cmd, data);
                return;
            }
    }
    if (!inCanvas(p) || historyView)
        return;
    SetFocus(window);
    down = true;
    dragStart = last = p;
    SetCapture(window);
    if (middle || tool == Tool::Pan || (GetKeyState(VK_SPACE) & 0x8000)) {
        panning = true;
        panOrigin = offset;
        return;
    }
    before = map.doc;
    dragPreview = false;
    dragDelta = {};
    auto w = world(p);
    movingNode.clear();
    movingBorder.clear();
    selectionBox = false;
    draggingControl = false;
    lastCanvas = w;
    if (beginBoundaryEdit(w))
        return;
    if (tool == Tool::Select) {
        auto id = hitObject(w, 7 / zoom, !(GetKeyState(VK_MENU) & 0x8000));
        if (!id.empty()) {
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (selected.contains(id))
                    selected.erase(id);
                else
                    selected.insert(id);
            } else if (!selected.contains(id)) {
                selected.clear();
                selected.insert(id);
            }
            updateSelection();
            if (selected.size() == 1 && map.doc["features"][*selected.begin()]["role"].str() == "country") {
                movingBorder = map.sharedBorder(*selected.begin(), w);
                borderGrab = w;
                if (movingBorder.empty())
                    status = "У государства нет общей границы; берега редактируются отдельно";
            }
        } else {
            if (!(GetKeyState(VK_CONTROL) & 0x8000))
                selected.clear();
            selectionBox = true;
        }
    } else if (tool == Tool::Border) {
        movingBorder = map.sharedBorder(selected.size() == 1 ? *selected.begin() : "", w);
        if (movingBorder.empty())
            movingBorder = map.sharedBorder("", w);
        if (!movingBorder.empty()) {
            const auto &ns = map.doc["arcs"][movingBorder]["nodes"];
            double d = 1e100;
            for (size_t i = 1; i < ns.size(); i++)
                d = std::min(d, segmentDistance(w, point(map.doc["nodes"][ns[i - 1].str()]),
                                                point(map.doc["nodes"][ns[i].str()])));
            if (d > 16 / zoom)
                movingBorder.clear();
        }
        borderGrab = w;
    } else if (tool == Tool::Node) {
        if (selected.empty()) {
            auto f = hitObject(w, 7 / zoom);
            if (!f.empty()) {
                selected = {f};
                updateSelection();
            }
        }
        double nearest = 10 / zoom;
        for (auto &fid : selected) {
            for (auto &node : map.featureNodes(map.doc["features"][fid])) {
                auto d = distance(w, point(map.doc["nodes"][node]));
                if (d < nearest) {
                    nearest = d;
                    movingNode = node;
                }
            }
        }
        if (movingNode.empty()) {
            auto f = hitObject(w, 7 / zoom);
            if (!f.empty()) {
                selected = {f};
                updateSelection();
            }
        }
    } else if (tool == Tool::Stamp || tool == Tool::Label) {
        down = false;
        ReleaseCapture();
        std::string label;
        if (tool == Tool::Label) {
            auto result = form(window, "Подпись", {{"Текст", "Подпись"}});
            if (!result)
                return;
            label = (*result)[0];
        }
        auto id = map.addSymbol(w, tool == Tool::Label ? "label" : activeSymbol, drawingLayer(), stroke,
                                brushSize * 3);
        auto &defaults = map.doc["symbols"][activeSymbol]["defaults"];
        if (tool == Tool::Stamp && defaults.isObject())
            for (auto &[k, v] : defaults.obj())
                map.doc["features"][id][k] = v;
        if (tool == Tool::Label)
            map.doc["features"][id]["name"] = label;
        selected = {id};
        changed("Добавлен объект");
    } else if (tool == Tool::Region || tool == Tool::Land || tool == Tool::Route || tool == Tool::River ||
               tool == Tool::Zone) {
        if (tool == Tool::Land && drawing.empty()) {
            double nearest = 10 / zoom;
            std::string owner;
            for (const auto &[id, f] : map.doc["features"].obj())
                if (f["role"].str() == "land")
                    for (const auto &n : map.featureNodes(f)) {
                        double d = distance(w, point(map.doc["nodes"][n]));
                        if (d < nearest) {
                            nearest = d;
                            movingNode = n;
                            owner = id;
                        }
                    }
            if (!movingNode.empty()) {
                selected = {owner};
                updateSelection();
                return;
            }
        }
        if (snap && tool != Tool::Land && tool != Tool::Region) {
            auto n = map.nearestNode(w, 8 / zoom);
            if (!n.empty())
                w = point(map.doc["nodes"][n]);
        }
        if (drawing.empty() || distance(drawing.back(), w) > 1 / zoom)
            drawing.push_back(w);
        status = "Клик — узел · Enter / двойной клик — закончить · Esc — отменить";
    } else if (tool == Tool::Brush || tool == Tool::Eraser) {
        auto l = map.layer(activeLayer);
        if (l && (*l)["kind"].str() == "raster" && !(*l)["locked"].boolean()) {
            painted = std::make_shared<Image>(*map.raster(*l));
            paintedLayer = activeLayer;
            strokeRaster(w, w, tool == Tool::Eraser);
            drawing = {w};
        } else if (tool == Tool::Brush)
            drawing = {w};
        else {
            auto id = hitObject(w, brushSize / zoom);
            if (!id.empty()) {
                map.eraseFeature(id);
                selected.erase(id);
                renderer.invalidateScene();
            }
        }
    } else if (tool == Tool::Lasso)
        drawing = {w};
    else if (tool == Tool::Trace) {
        auto l = map.layer(activeLayer);
        std::shared_ptr<Image> im;
        if (l && (*l)["kind"].str() == "raster")
            im = map.raster(*l);
        else
            im = std::make_shared<Image>(renderer.renderImage(map));
        auto rings = traceRegion(*im, w, 24);
        if (!rings.empty()) {
            auto id = map.addPath(rings[0], true, "region", drawingLayer(), stroke, 2, .01);
            Json refs = Json::array();
            refs.push(map.doc["features"][id]["arcs"]);
            for (size_t i = 1; i < rings.size(); i++) {
                if (rings[i].size() < 3)
                    continue;
                auto hole = map.addPath(rings[i], true, "region", activeLayer, stroke, 2, .01);
                refs.push(map.doc["features"][hole]["arcs"]);
                map.eraseFeature(hole);
            }
            map.doc["features"][id]["rings"] = refs;
            map.doc["features"][id].obj().erase("arcs");
            map.doc["features"][id]["name"] = "Обведённая область";
            map.doc["features"][id]["opacity"] = .28;
            map.doc["features"][id]["provenance"] =
                fields({{"kind", "raster_trace"}, {"source_sha256", map.doc["import"]["sha256"]}});
            selected = {id};
            changed("Цветовая область превращена в контур");
        }
        down = false;
        ReleaseCapture();
    } else if (tool == Tool::Fill) {
        auto l = map.layer(activeLayer);
        if (l && (*l)["kind"].str() == "raster" && !(*l)["locked"].boolean()) {
            auto im = std::make_shared<Image>(*map.raster(*l));
            floodFill(*im, w, stroke, 24, selectionMask);
            auto asset = map.storeImage(*im);
            (*l)["image"] = asset;
            (*l)["source_type"] = "png";
            l->obj().erase("pdn_layer");
            changed("Заливка на растровом слое");
            map.clearCache();
            down = false;
            ReleaseCapture();
            return;
        }
        auto id = hitObject(w, 3 / zoom);
        if (!id.empty()) {
            map.doc["features"][id]["fill"] = stroke;
            selected = {id};
            changed("Изменена заливка");
        } else
            status = "Выбери территорию; для растровой области сначала используй W";
    } else if (tool == Tool::Picker) {
        auto im = renderer.renderImage(map);
        int x = int(w.x), y = int(w.y);
        if (x >= 0 && y >= 0 && x < im.width && y < im.height) {
            size_t i = (size_t(y) * im.width + x) * 4;
            stroke = hexColor(RGB(im.bgra[i + 2], im.bgra[i + 1], im.bgra[i]));
            status = "Цвет выбран: " + stroke;
        }
    }
    invalidate();
}
void App::strokeRaster(Point a, Point b, bool erase) {
    if (!painted)
        return;
    double d = distance(a, b);
    int steps = std::max(1, int(d / std::max(1.0, brushSize / 4)));
    auto col = color(stroke);
    for (int step = 0; step <= steps; step++) {
        double t = double(step) / steps, x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t;
        int loX = std::max(0, int(x - brushSize)), hiX = std::min(painted->width - 1, int(x + brushSize)),
            loY = std::max(0, int(y - brushSize)), hiY = std::min(painted->height - 1, int(y + brushSize));
        for (int yy = loY; yy <= hiY; yy++)
            for (int xx = loX; xx <= hiX; xx++)
                if (std::hypot(xx - x, yy - y) <= brushSize / 2 &&
                    (selectionMask.empty() || inside({xx + .5, yy + .5}, selectionMask))) {
                    size_t i = (size_t(yy) * painted->width + xx) * 4;
                    if (erase) {
                        painted->bgra[i] = painted->bgra[i + 1] = painted->bgra[i + 2] =
                            painted->bgra[i + 3] = 0;
                    } else {
                        painted->bgra[i] = uint8_t(col.b * 255);
                        painted->bgra[i + 1] = uint8_t(col.g * 255);
                        painted->bgra[i + 2] = uint8_t(col.r * 255);
                        painted->bgra[i + 3] = 255;
                    }
                }
    }
}
void App::pointerMove(Point p) {
    auto prev = world(last);
    mouse = p;
    if (inCanvas(p))
        lastCanvas = world(p);
    if (down) {
        if (panning) {
            offset = {panOrigin.x + p.x - dragStart.x, panOrigin.y + p.y - dragStart.y};
            cameraMovingUntil = GetTickCount64() + 180;
            SetTimer(window, 2, 180, nullptr);
        } else if (draggingControl) {
            controlTarget = world(p);
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                auto origin = point(map.doc["nodes"][activeControl]);
                if (std::abs(controlTarget.x - origin.x) >= std::abs(controlTarget.y - origin.y))
                    controlTarget.y = origin.y;
                else
                    controlTarget.x = origin.x;
            }
            dragPreview = distance(dragStart, p) > 2;
        } else if (!movingBorder.empty()) {
            auto a = world(dragStart), b = world(p);
            dragDelta = {b.x - a.x, b.y - a.y};
            dragPreview = distance(dragStart, p) > 3;
        } else if (!movingNode.empty()) {
            auto origin = point(map.doc["nodes"][movingNode]), at = world(p);
            dragDelta = {at.x - origin.x, at.y - origin.y};
            dragPreview = distance(dragStart, p) > 3;
        } else if (tool == Tool::Select && !selectionBox && !selected.empty() &&
                   map.doc["features"][*selected.begin()]["role"].str() != "country") {
            auto a = world(dragStart), b = world(p);
            dragDelta = {b.x - a.x, b.y - a.y};
            dragPreview = distance(dragStart, p) > 3;
        } else if (tool == Tool::Brush || tool == Tool::Lasso) {
            if (painted) {
                strokeRaster(prev, world(p), false);
                drawing.push_back(world(p));
            } else if (drawing.empty() || distance(drawing.back(), world(p)) * zoom > 2)
                drawing.push_back(world(p));
        } else if (tool == Tool::Eraser) {
            if (painted) {
                strokeRaster(prev, world(p), true);
                drawing.push_back(world(p));
            } else {
                auto id = hitObject(world(p), brushSize / zoom);
                if (!id.empty()) {
                    map.eraseFeature(id);
                    selected.erase(id);
                    renderer.invalidateScene();
                }
            }
        } else if (tool == Tool::Measure) {
            double d = distance(world(dragStart), world(p));
            status =
                "Расстояние: " + std::to_string(int(d)) + " пикселей карты · масштаб в километрах не задан";
            drawing = {world(dragStart), world(p)};
        }
    }
    last = p;
    if (!down) {
        int hit = -1;
        for (int i = int(hits.size()) - 1; i >= 0; --i)
            if (contains(hits[size_t(i)].rect, p)) {
                hit = i;
                break;
            }
        if (hit != hoverHit) {
            hoverHit = hit;
            hoverSince = GetTickCount64();
            SetTimer(window, 4, 550, nullptr);
        } else if (drawing.empty() && tool != Tool::Stamp)
            return;
    }
    auto tick = GetTickCount64();
    if (tick - lastPointerPaint >= 16) {
        invalidate();
        lastPointerPaint = tick;
    } else if (!pointerPaintPending) {
        pointerPaintPending = true;
        SetTimer(window, 3, 16, nullptr);
    }
}
void App::pointerUp(Point p) {
    mouse = p;
    if (!down)
        return;
    // Commit the final pointer position once; queued WM_MOUSEMOVE messages never mutate the document.
    if (!panning && (dragPreview || distance(dragStart, p) > 3) && !selectionBox) {
        if (draggingControl) {
            controlTarget = world(p);
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                auto origin = point(map.doc["nodes"][activeControl]);
                if (std::abs(controlTarget.x - origin.x) >= std::abs(controlTarget.y - origin.y))
                    controlTarget.y = origin.y;
                else
                    controlTarget.x = origin.x;
            }
            map.moveBorderControl(activeBorder, activeControl, controlTarget, allowSea);
        } else if (!movingBorder.empty()) {
            auto a = world(dragStart), b = world(p);
            map.moveBorder(movingBorder, borderGrab, {b.x - a.x, b.y - a.y}, borderRadius / zoom);
        } else if (!movingNode.empty()) {
            map.moveNode(movingNode, world(p));
        } else if (tool == Tool::Select && !selected.empty() &&
                   map.doc["features"][*selected.begin()]["role"].str() != "country") {
            auto a = world(dragStart), b = world(p);
            map.moveFeatures(std::vector<std::string>(selected.begin(), selected.end()),
                             {b.x - a.x, b.y - a.y});
        }
    }
    dragPreview = false;
    renderer.excludedBorderArcs.clear();
    down = false;
    ReleaseCapture();
    if (panning) {
        panning = false;
        invalidate();
        return;
    }
    if (painted) {
        auto asset = map.storeImage(*painted);
        auto l = map.layer(paintedLayer);
        (*l)["image"] = asset;
        (*l)["source_type"] = "png";
        l->obj().erase("pdn_layer");
        changed(tool == Tool::Eraser ? "Ластик на растровом слое" : "Кисть на растровом слое");
        drawing.clear();
        map.clearCache();
    } else if (tool == Tool::Rectangle) {
        auto a = world(dragStart), b = world(p);
        if (distance(a, b) > 4 / zoom) {
            auto id = map.addPath({a, {b.x, a.y}, b, {a.x, b.y}}, true, "region", drawingLayer(), stroke,
                                  lineWidth, snap ? 5 / zoom : 0);
            selected = {id};
            changed("Добавлена прямоугольная область");
        }
    } else if (selectionBox) {
        auto a = world(dragStart), b = world(p);
        if (distance(a, b) > 3 / zoom)
            selectionMask = {a, {b.x, a.y}, b, {a.x, b.y}};
        else
            selectionMask.clear();
        for (auto &[id, f] : map.doc["features"].obj()) {
            if (!map.selectable(f, isolateLayer ? SelectionDomain::ActiveLayer : editScope, activeLayer))
                continue;
            auto q = map.anchor(f);
            if (q.x >= std::min(a.x, b.x) && q.x <= std::max(a.x, b.x) && q.y >= std::min(a.y, b.y) &&
                q.y <= std::max(a.y, b.y))
                selected.insert(id);
        }
        selectionBox = false;
        updateSelection();
    } else if (tool == Tool::Lasso) {
        selectionMask = drawing;
        for (auto &[id, f] : map.doc["features"].obj())
            if (map.selectable(f, isolateLayer ? SelectionDomain::ActiveLayer : editScope, activeLayer) &&
                inside(map.anchor(f), drawing))
                selected.insert(id);
        drawing.clear();
        updateSelection();
    } else if (tool == Tool::Brush && !drawing.empty())
        finishDrawing();
    else if (draggingControl || !movingBorder.empty() || !movingNode.empty() || tool == Tool::Select ||
             tool == Tool::Eraser) {
        if (!(before == map.doc))
            changed(draggingControl         ? "Контрольная точка перемещена"
                    : !movingBorder.empty() ? "Общая граница изменена · суша сохранена"
                    : movingNode.empty()    ? "Перемещены объекты"
                                            : "Изменён общий узел");
        movingNode.clear();
        movingBorder.clear();
    }
    draggingControl = false;
    invalidate();
}
void App::finishDrawing() {
    bool closed = tool == Tool::Region || tool == Tool::Land || tool == Tool::Zone;
    if (drawing.size() < (closed ? 3u : 2u)) {
        drawing.clear();
        invalidate();
        return;
    }
    before = map.doc;
    auto pts = tool == Tool::Brush ? simplify(drawing, 1.5 / zoom) : drawing;
    auto kind = tool == Tool::Zone                           ? "zone"
                : tool == Tool::River                        ? "river"
                : tool == Tool::Route                        ? "route"
                : tool == Tool::Region || tool == Tool::Land ? "region"
                                                             : "stroke";
    auto layer =
        (tool == Tool::Land || tool == Tool::Region) ? map.domainLayer(tool == Tool::Land) : drawingLayer();
    auto id = map.addPath(pts, closed, kind, layer, tool == Tool::Land ? "#E6DDC4" : stroke,
                          tool == Tool::Brush ? brushSize : lineWidth,
                          (tool == Tool::Land || tool == Tool::Region) ? 0
                          : snap                                       ? 4 / zoom
                                                                       : 0);
    if (tool == Tool::Land || tool == Tool::Region) {
        auto &f = map.doc["features"][id];
        f["role"] = tool == Tool::Land ? "land" : "country";
        f["domain"] = tool == Tool::Land ? "physical" : "political";
        f["name"] = tool == Tool::Land ? "Новая земля" : "Новое государство";
        f["show_label"] = tool == Tool::Region;
        if (tool == Tool::Region)
            f["territory_scope"] = allowSea ? "land_sea" : "land";
        f["opacity"] = 1;
        map.doc["land_state_separated"] = true;
    }
    if (tool == Tool::Region)
        try {
            renderer.claimCountry(map, id);
        } catch (...) {
            map.doc = before;
            map.clearCache();
            renderer.clear();
            throw;
        }
    if (tool == Tool::Brush)
        map.doc["features"][id]["name"] = "";
    selected = {id};
    drawing.clear();
    changed("Нарисован контур");
}
LRESULT CALLBACK App::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App *a = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        a = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        a->window = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
    }
    if (!a)
        return DefWindowProcW(h, m, w, l);
    try {
        return a->message(m, w, l);
    } catch (const std::exception &e) {
        if (a->down && a->before["schema_version"].num() == 1) {
            a->map.doc = a->before;
            a->map.clearCache();
            a->renderer.clear();
        }
        a->down = false;
        a->panning = false;
        ReleaseCapture();
        showError(h, e);
        a->invalidate();
        return 0;
    }
}
LRESULT App::message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP + 8:
        invalidate();
        return 0;
    case WM_CREATE: {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(window, 20, &dark, sizeof dark);
        COLORREF caption = RGB(24, 36, 46);
        DwmSetWindowAttribute(window, 35, &caption, sizeof caption);
        dpi = GetDpiForWindow(window) / 96.0f;
        font = CreateFontW(int(-15 * dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
        searchBox = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0,
                                    100, 30, window, reinterpret_cast<HMENU>(20001), nullptr, nullptr);
        SendMessageW(searchBox, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(searchBox, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Поиск по имени или ID…"));
        nameBox = CreateWindowExW(0, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0,
                                  100, 30, window, reinterpret_cast<HMENU>(20002), nullptr, nullptr);
        SendMessageW(nameBox, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        EnableWindow(nameBox, FALSE);
        layout();
        fit();
        SetTimer(window, 1, 15000, nullptr);
        return 0;
    }
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) {
            // An occluded HWND target can skip presentation. Request a frame after
            // foreground activation has completed, even without mouse movement.
            SetTimer(window, 6, 60, nullptr);
            invalidate();
        }
        break;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            layout();
        return 0;
    case WM_DPICHANGED: {
        auto r = reinterpret_cast<RECT *>(lp);
        SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto m = reinterpret_cast<MINMAXINFO *>(lp);
        m->ptMinTrackSize = {LONG(940 * dpi), LONG(650 * dpi)};
        return 0;
    }
    case WM_LBUTTONDOWN:
        pointerDown({GET_X_LPARAM(lp) / dpi, GET_Y_LPARAM(lp) / dpi});
        return 0;
    case WM_MBUTTONDOWN:
        pointerDown({GET_X_LPARAM(lp) / dpi, GET_Y_LPARAM(lp) / dpi}, true);
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
        pointerUp({GET_X_LPARAM(lp) / dpi, GET_Y_LPARAM(lp) / dpi});
        return 0;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (down) {
            if (!panning) {
                map.doc = before;
                map.clearCache();
                renderer.clear();
            }
            down = false;
            panning = false;
            dragPreview = false;
            selectionBox = false;
            movingNode.clear();
            movingBorder.clear();
            draggingControl = false;
            renderer.excludedBorderArcs.clear();
            painted.reset();
            drawing.clear();
            updateSelection();
        }
        return 0;
    case WM_MOUSEMOVE:
        pointerMove({GET_X_LPARAM(lp) / dpi, GET_Y_LPARAM(lp) / dpi});
        return 0;
    case WM_LBUTTONDBLCLK:
        if (editScope == SelectionDomain::Borders &&
            (tool == Tool::Select || tool == Tool::Border || tool == Tool::Node)) {
            mouse = {GET_X_LPARAM(lp) / dpi, GET_Y_LPARAM(lp) / dpi};
            lastCanvas = world(mouse);
            if (!activeBorder.empty())
                command(AddControl);
            return 0;
        }
        if (!drawing.empty())
            finishDrawing();
        else if (!selected.empty())
            properties();
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pos{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(window, &pos);
        Point at{pos.x / dpi, pos.y / dpi};
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        if (showPanels && historyView && at.x > width - 340 && at.y > 124 && at.y < height - 78) {
            versionScroll =
                std::clamp(versionScroll - (d > 0 ? 1 : -1), 0, std::max(0, int(versions.size()) - 1));
        } else if (showPanels && panel == 4 && at.x > width - 340 && at.y > 124 && at.y < height - 78) {
            layerScroll = std::max(0, layerScroll - (d > 0 ? 3 : -3));
        } else if (showPanels && panel >= 1 && panel <= 3 && at.x > width - 340 && at.y > 124 &&
                   at.y < height - 78) {
            objectScroll = std::max(0, objectScroll - (d > 0 ? 3 : -3));
        } else {
            auto anchor = world(at);
            zoom = std::clamp(zoom * std::pow(1.2, d / 120.0), .01, 24.0);
            offset = {at.x - anchor.x * zoom, at.y - anchor.y * zoom};
            cameraMovingUntil = GetTickCount64() + 120;
            SetTimer(window, 2, 120, nullptr);
        }
        invalidate();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 20001 && HIWORD(wp) == EN_CHANGE) {
            query = editText(searchBox);
            objectScroll = 0;
            invalidate();
            return 0;
        }
        if (LOWORD(wp) == 20002 && HIWORD(wp) == EN_KILLFOCUS && !syncName && !selected.empty()) {
            auto &f = map.doc["features"][*selected.begin()];
            auto name = editText(nameBox);
            if (name != f["name"].str()) {
                before = map.doc;
                f["name"] = name;
                changed("Изменена подпись");
            }
            return 0;
        }
        command(LOWORD(wp));
        return 0;
    case WM_CTLCOLOREDIT: {
        auto dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, RGB(48, 65, 49));
        SetBkColor(dc, RGB(235, 240, 231));
        SetDCBrushColor(dc, RGB(235, 240, 231));
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) & 0x8000, shift = GetKeyState(VK_SHIFT) & 0x8000;
        if (ctrl) {
            switch (wp) {
            case 'S':
                save(shift);
                return 0;
            case 'O':
                open();
                return 0;
            case 'N':
                command(NewFile);
                return 0;
            case 'Z':
                command(shift ? Redo : Undo);
                return 0;
            case 'Y':
                command(Redo);
                return 0;
            case 'C':
                copy();
                return 0;
            case 'V':
                paste();
                return 0;
            case 'F':
                panel = 2;
                sideTab = 1;
                showPanels = true;
                historyView = false;
                layout();
                SetFocus(searchBox);
                return 0;
            case 'D':
                command(Duplicate);
                return 0;
            case 'A':
                selected.clear();
                for (auto &[id, f] : map.doc["features"].obj())
                    if (map.selectable(f, isolateLayer ? SelectionDomain::ActiveLayer : editScope,
                                       activeLayer))
                        selected.insert(id);
                updateSelection();
                return 0;
            }
        }
        if (wp == VK_ESCAPE) {
            dragPreview = false;
            drawing.clear();
            selectionMask.clear();
            movingNode.clear();
            movingBorder.clear();
            draggingControl = false;
            renderer.excludedBorderArcs.clear();
            if (down) {
                if (!panning) {
                    map.doc = before;
                    renderer.clear();
                }
                panning = false;
                down = false;
                ReleaseCapture();
            }
            selectionBox = false;
            invalidate();
            return 0;
        }
        if (wp == VK_RETURN) {
            finishDrawing();
            return 0;
        }
        if (wp == VK_DELETE) {
            command(editScope == SelectionDomain::Borders && !activeControl.empty() ? RemoveControl : Delete);
            return 0;
        }
        if (wp == VK_F2) {
            properties();
            return 0;
        }
        if (wp == VK_HOME) {
            fit();
            return 0;
        }
        if (wp == VK_TAB) {
            command(Panels);
            return 0;
        }
        for (int i = 0; i < 19; i++)
            if (wp == UINT(toolKeys[i][0])) {
                command(SetTool, std::to_string(i));
                return 0;
            }
        break;
    }
    case WM_TIMER:
        if (wp == 6) {
            KillTimer(window, 6);
            invalidate();
            return 0;
        }
        if (wp == 5) {
            KillTimer(window, 5);
            invalidate();
            return 0;
        }
        if (wp == 4) {
            KillTimer(window, 4);
            invalidate();
            return 0;
        }
        if (wp == 2) {
            KillTimer(window, 2);
            cameraMovingUntil = 0;
            invalidate();
            return 0;
        }
        if (wp == 3) {
            KillTimer(window, 3);
            pointerPaintPending = false;
            lastPointerPaint = GetTickCount64();
            invalidate();
            return 0;
        }
        if (dirty && !down && editSerial != autosavedSerial && !map.directory.empty())
            try {
                map.autosave();
                autosavedSerial = editSerial;
            } catch (const std::exception &e) {
                status = e.what();
            }
        return 0;
    case WM_CLOSE:
        if (!discard())
            return 0;
        if (!map.directory.empty())
            try {
                atomicText(map.directory / L".atlas" / L"view.json",
                           fields({{"zoom", zoom},
                                   {"offset", pointJson(offset)},
                                   {"active_layer", activeLayer},
                                   {"side_tab", sideTab},
                                   {"project_root", pathText(projectRoot)},
                                   {"panels", showPanels},
                                   {"ui_revision", 4},
                                   {"edit_scope", int(editScope)},
                                   {"isolate_layer", isolateLayer},
                                   {"panel", panel},
                                   {"snap", snap},
                                   {"grid", grid}})
                               .dump());
            } catch (...) {
            }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        KillTimer(window, 2);
        KillTimer(window, 3);
        DeleteObject(font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, msg, wp, lp);
}
int App::run(HINSTANCE instance, int show) {
    campaign.load(projectRoot);
    if (!initialMap.empty() && fs::exists(initialMap))
        map.load(initialMap);
    if (map.doc["symbols"].size() == 0)
        map.doc["symbols"] = defaultSymbols();
    activeLayer = map.vectorLayer();
    versions = map.versions();
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"AtlasMapEditor";
    RegisterClassExW(&wc);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = std::min(1500, int(work.right - work.left) - 60),
        h = std::min(940, int(work.bottom - work.top) - 60);
    auto title = wide("АТЛАС 4 — " + map.doc["name"].str());
    window = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, instance, this);
    if (!window)
        throw std::runtime_error("Cannot create Atlas window");
    ShowWindow(window, show == SW_HIDE ? SW_SHOWNORMAL : show);
    UpdateWindow(window);
    if (!map.directory.empty() && fs::exists(map.directory / L".atlas" / L"view.json"))
        try {
            auto view = Json::parse(readText(map.directory / L".atlas" / L"view.json"));
            auto savedRoot = pathOf(view["project_root"].str());
            if (!savedRoot.empty() && fs::exists(savedRoot / pathOf("09_Реестры/Сущности.json"))) {
                campaign.load(savedRoot);
                projectRoot = savedRoot;
            }
            showPanels = view["panels"].boolean(true);
            snap = view["snap"].boolean(true);
            grid = view["grid"].boolean();
            sideTab = int(view["side_tab"].num());
            if (view["ui_revision"].num() == 4) {
                editScope = SelectionDomain(std::clamp(int(view["edit_scope"].num(1)), 1, 3));
                isolateLayer = view["isolate_layer"].boolean();
            }
            layout();
            if (view["ui_revision"].num() >= 3)
                zoom = std::clamp(view["zoom"].num(zoom), .01, 24.0);
            if (view["ui_revision"].num() >= 3 && view["offset"].isArray())
                offset = point(view["offset"]);
            if (map.layer(view["active_layer"].str()))
                activeLayer = view["active_layer"].str();
            invalidate();
        } catch (...) {
        }
    if (map.hasRecovery())
        status = "Есть автосохранение: Файл → Восстановить автосохранение";
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) &&
            (message.wParam == 'S' || message.wParam == 'O' || message.wParam == 'N' ||
             message.wParam == 'F')) {
            SetFocus(window);
            SendMessageW(window, WM_KEYDOWN, message.wParam, message.lParam);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return int(message.wParam);
}
} // namespace atlas
