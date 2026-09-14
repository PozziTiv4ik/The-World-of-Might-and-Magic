#include "render.hpp"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
namespace atlas {
struct MapRenderer::Responsive {
    struct Job {
        uint64_t revision = 0, serial = 0;
        std::shared_ptr<const Json> doc;
        fs::path directory;
        D2D1_RECT_F bounds{};
        double zoom = 1;
        float width = 1, height = 1, dpi = 1;
        bool labels = true;
        HWND notify = nullptr;
    };
    struct Frame {
        Image image;
        Image borders;
        double zoom = 1;
        D2D1_RECT_F bounds{};
        uint64_t revision = 0, serial = 0;
    };
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    uint64_t revision = 1, serial = 0;
    bool snapshotNeeded = true;
    std::shared_ptr<const Json> snapshot;
    std::optional<Job> pending;
    std::shared_ptr<Frame> overviewReady, fineReady;
    Com<ID2D1Bitmap> overview, fine;
    Com<ID2D1Bitmap> overviewBorders, fineBorders;
    double fineZoom = 1;
    D2D1_RECT_F overviewBounds{}, fineBounds{};
    ID2D1RenderTarget *owner = nullptr;
    uint64_t uploadedOverview = 0, uploadedFine = 0;
    Job last;
    bool haveLast = false;
    std::string error;
    std::thread thread;
    Responsive() : thread([this] { run(); }) {}
    ~Responsive() {
        {
            std::lock_guard lock(mutex);
            stop = true;
        }
        wake.notify_one();
        if (thread.joinable())
            thread.join();
    }
    std::shared_ptr<Frame> render(Map &m, MapRenderer &r, const Job &j, bool coarse) {
        auto f = std::make_shared<Frame>();
        f->revision = j.revision;
        f->serial = j.serial;
        f->zoom = j.zoom;
        float dw = j.width, dh = j.height;
        double z = j.zoom;
        Point offset{-j.bounds.left * z, -j.bounds.top * z};
        float scale = j.dpi;
        if (coarse) {
            dw = 2048;
            dh = float(2048 * m.doc["height"].num() / m.doc["width"].num());
            z = dw / m.doc["width"].num();
            offset = {0, 0};
            scale = 1;
            f->bounds = {0, 0, float(m.doc["width"].num()), float(m.doc["height"].num())};
        } else
            f->bounds = j.bounds;
        // Bound upload cost on very dense displays while retaining an exact coordinate transform.
        double count = double(dw) * dh * scale * scale;
        if (count > 4200000)
            scale *= float(std::sqrt(4200000 / count));
        int w = std::max(1, int(std::ceil(dw * scale))), h = std::max(1, int(std::ceil(dh * scale)));
        Com<IWICImagingFactory> wic;
        check(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())),
            "Worker WIC");
        Com<IWICBitmap> b;
        check(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, b.put()),
              "Worker pixels");
        Com<ID2D1RenderTarget> t;
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
        props.dpiX = props.dpiY = 96 * scale;
        check(r.factory->CreateWicBitmapRenderTarget(b.get(), props, t.put()), "Worker target");
        t->BeginDraw();
        t->Clear(color(m.doc["background"].str("#D3E0DE")));
        r.draw(m, t.get(), {0, 0, dw, dh}, z, offset, {}, false, j.labels);
        check(t->EndDraw(), "Worker render");
        f->image = Image(w, h);
        check(b->CopyPixels(nullptr, w * 4, UINT(f->image.bgra.size()), f->image.bgra.data()),
              "Worker pixels");
        t->BeginDraw();
        t->Clear(D2D1::ColorF(0, 0));
        r.drawBorders(m, t.get(), {0, 0, dw, dh}, z, offset);
        check(t->EndDraw(), "Worker borders");
        f->borders = Image(w, h);
        check(b->CopyPixels(nullptr, w * 4, UINT(f->borders.bgra.size()), f->borders.bgra.data()),
              "Worker border pixels");
        return f;
    }
    void run() {
        auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            Map m;
            MapRenderer r;
            r.paintBorders = false;
            uint64_t loaded = 0;
            while (true) {
                Job j;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] { return stop || pending.has_value(); });
                    if (stop)
                        break;
                    j = *pending;
                    pending.reset();
                }
                try {
                    if (loaded != j.revision) {
                        m.doc = *j.doc;
                        m.directory = j.directory;
                        m.clearCache();
                        r.clear();
                        loaded = j.revision;
                        auto frame = render(m, r, j, true);
                        {
                            std::lock_guard lock(mutex);
                            if (stop)
                                break;
                            if (revision == j.revision)
                                overviewReady = std::move(frame);
                            if (pending && pending->revision == j.revision) {
                                j = *pending;
                                pending.reset();
                            }
                        }
                        if (j.notify)
                            PostMessageW(j.notify, WM_APP + 8, 0, 0);
                    }
                    auto frame = render(m, r, j, false);
                    {
                        std::lock_guard lock(mutex);
                        if (stop)
                            break;
                        if (revision == j.revision)
                            fineReady = std::move(frame);
                    }
                    if (j.notify)
                        PostMessageW(j.notify, WM_APP + 8, 0, 0);
                } catch (const std::exception &e) {
                    std::lock_guard lock(mutex);
                    error = e.what();
                    if (j.notify)
                        PostMessageW(j.notify, WM_APP + 8, 0, 0);
                }
            }
        }
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};
MapRenderer::~MapRenderer() = default;
bool MapRenderer::responsiveSettled() const {
    if (!responsive)
        return false;
    auto &s = *responsive;
    std::lock_guard lock(s.mutex);
    return s.fineReady && s.fineReady->revision == s.revision && s.fineReady->serial == s.last.serial &&
           s.uploadedFine == s.last.serial;
}
void MapRenderer::resetResponsive() {
    if (!responsive)
        return;
    auto &s = *responsive;
    std::lock_guard lock(s.mutex);
    ++s.revision;
    s.snapshotNeeded = true;
    s.haveLast = false;
    s.pending.reset();
    s.fineReady.reset();
    s.overviewReady.reset();
    // Keep the previous overview during an edit, but never accept an old revision as a fresh frame.
    s.fine.reset();
    s.fineBorders.reset();
    s.uploadedFine = 0;
}
void MapRenderer::drawResponsive(Map &m, ID2D1RenderTarget *t, D2D1_RECT_F viewport, double zoom,
                                 Point offset, const std::set<std::string> &selected, bool nodes, bool moving,
                                 HWND notify, bool overlayBorders) {
    if (!responsive)
        responsive = std::make_shared<Responsive>();
    auto &s = *responsive;
    float dx, dy;
    t->GetDpi(&dx, &dy);
    D2D1_RECT_F bounds{float((viewport.left - offset.x) / zoom), float((viewport.top - offset.y) / zoom),
                       float((viewport.right - offset.x) / zoom), float((viewport.bottom - offset.y) / zoom)};
    {
        std::lock_guard lock(s.mutex);
        if (s.snapshotNeeded) {
            s.snapshot = std::make_shared<const Json>(m.doc);
            s.snapshotNeeded = false;
        }
        bool different = !s.haveLast || s.last.zoom != zoom || s.last.dpi != dx / 96 ||
                         s.last.labels != displayLabels || s.last.bounds.left != bounds.left ||
                         s.last.bounds.top != bounds.top || s.last.bounds.right != bounds.right ||
                         s.last.bounds.bottom != bounds.bottom;
        if (different && (!moving || !s.haveLast)) {
            Responsive::Job j;
            j.revision = s.revision;
            j.serial = ++s.serial;
            j.doc = s.snapshot;
            j.directory = m.directory;
            j.bounds = bounds;
            j.zoom = zoom;
            j.width = viewport.right - viewport.left;
            j.height = viewport.bottom - viewport.top;
            j.dpi = dx / 96;
            j.labels = displayLabels;
            j.notify = notify;
            s.last = j;
            s.haveLast = true;
            s.pending = j;
            s.wake.notify_one();
        }
    }
    if (s.owner != t) {
        s.owner = t;
        s.overview.reset();
        s.fine.reset();
        s.overviewBorders.reset();
        s.fineBorders.reset();
        s.uploadedFine = s.uploadedOverview = 0;
    }
    std::shared_ptr<Responsive::Frame> coarse, fine;
    {
        std::lock_guard lock(s.mutex);
        coarse = s.overviewReady;
        fine = s.fineReady;
    }
    auto upload = [&](const auto &f, Com<ID2D1Bitmap> &b, uint64_t &stamp, D2D1_RECT_F &bnd, bool overview) {
        if (!f || (overview ? f->revision : f->serial) == stamp)
            return;
        Com<ID2D1Bitmap> next;
        check(t->CreateBitmap(
                  D2D1::SizeU(f->image.width, f->image.height), f->image.bgra.data(), f->image.width * 4,
                  D2D1::BitmapProperties(
                      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96),
                  next.put()),
              "Upload view");
        b = std::move(next);
        Com<ID2D1Bitmap> border;
        check(t->CreateBitmap(
                  D2D1::SizeU(f->borders.width, f->borders.height), f->borders.bgra.data(),
                  f->borders.width * 4,
                  D2D1::BitmapProperties(
                      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96),
                  border.put()),
              "Upload borders");
        (overview ? s.overviewBorders : s.fineBorders) = std::move(border);
        if (!overview)
            s.fineZoom = f->zoom;
        stamp = overview ? f->revision : f->serial;
        bnd = f->bounds;
    };
    upload(coarse, s.overview, s.uploadedOverview, s.overviewBounds, true);
    upload(fine, s.fine, s.uploadedFine, s.fineBounds, false);
    Painter p(t, textFactory.get(), &textCache);
    p.fill(viewport, m.doc["background"].str("#D3E0DE"));
    t->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    auto blit = [&](ID2D1Bitmap *b, D2D1_RECT_F r) {
        if (b)
            t->DrawBitmap(b,
                          {float(r.left * zoom + offset.x), float(r.top * zoom + offset.y),
                           float(r.right * zoom + offset.x), float(r.bottom * zoom + offset.y)},
                          1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    };
    blit(s.overview.get(), s.overviewBounds);
    if (s.fine) {
        double ratio = zoom / s.fineZoom;
        if (ratio > .4 && ratio < 2.5)
            blit(s.fine.get(), s.fineBounds);
    }
    t->PopAxisAlignedClip();
    if (!s.overview && !s.fine)
        p.text("Подготовка карты…",
               {viewport.left + 90, viewport.top + 24, viewport.left + 360, viewport.top + 60}, 13,
               "#526B68");
    {
        std::lock_guard lock(s.mutex);
        if (!s.error.empty())
            p.text(s.error, {90, 84, 900, 120}, 12, "#9A3E3E");
    }
    if (paintBorders && overlayBorders)
        drawResponsiveBorders(m, t, viewport, zoom, offset);
    selection(m, t, viewport, zoom, offset, selected, nodes);
}
void MapRenderer::drawResponsiveBorders(Map &m, ID2D1RenderTarget *t, D2D1_RECT_F v, double z, Point offset) {
    if (!responsive || !excludedBorderArcs.empty() || responsive->uploadedOverview != responsive->revision ||
        !responsive->overviewBorders || responsive->owner != t) {
        drawBorders(m, t, v, z, offset);
        return;
    }
    auto &s = *responsive;
    auto rect = [&](D2D1_RECT_F b) {
        return D2D1::RectF(float(b.left * z + offset.x), float(b.top * z + offset.y),
                           float(b.right * z + offset.x), float(b.bottom * z + offset.y));
    };
    auto overview = rect(s.overviewBounds);
    auto drawCoarse = [&](D2D1_RECT_F clip) {
        if (clip.right <= clip.left || clip.bottom <= clip.top)
            return;
        t->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        t->DrawBitmap(s.overviewBorders.get(), overview, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        t->PopAxisAlignedClip();
    };
    t->PushAxisAlignedClip(v, D2D1_ANTIALIAS_MODE_ALIASED);
    double ratio = z / s.fineZoom;
    if (s.fineBorders && ratio > .4 && ratio < 2.5) {
        auto fine = rect(s.fineBounds);
        auto cut =
            D2D1::RectF(std::clamp(fine.left, v.left, v.right), std::clamp(fine.top, v.top, v.bottom),
                        std::clamp(fine.right, v.left, v.right), std::clamp(fine.bottom, v.top, v.bottom));
        // Transparent border tiles replace one another; compositing both would darken the same line twice.
        drawCoarse({v.left, v.top, v.right, cut.top});
        drawCoarse({v.left, cut.bottom, v.right, v.bottom});
        drawCoarse({v.left, cut.top, cut.left, cut.bottom});
        drawCoarse({cut.right, cut.top, v.right, cut.bottom});
        t->DrawBitmap(s.fineBorders.get(), fine, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else
        drawCoarse(v);
    t->PopAxisAlignedClip();
}
} // namespace atlas
