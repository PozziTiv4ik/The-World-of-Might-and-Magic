#pragma once
#include "model.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <set>

namespace atlas {
Json defaultSymbols();
D2D1_COLOR_F color(const std::string &hex, float opacity = 1);
std::string hexColor(COLORREF color);
struct TextCache {
    std::map<int, Com<IDWriteTextFormat>> formats;
    std::map<std::string, Com<IDWriteTextLayout>> layouts;
};
class Painter {
    Com<ID2D1SolidColorBrush> brush;
    TextCache localText;
    TextCache *textCache;

  public:
    ID2D1RenderTarget *target = nullptr;
    IDWriteFactory *textFactory = nullptr;
    Painter(ID2D1RenderTarget *target, IDWriteFactory *textFactory, TextCache *cache = nullptr);
    ID2D1Brush *ink(const std::string &c, float opacity = 1);
    void fill(D2D1_RECT_F r, const std::string &c, float opacity = 1);
    void rect(D2D1_RECT_F r, const std::string &c, float thickness = 1);
    void line(Point a, Point b, const std::string &c, float thickness = 1, float opacity = 1);
    void circle(Point p, float radius, const std::string &c, bool filled = true);
    void rounded(D2D1_RECT_F r, const std::string &c, float radius = 6);
    void text(const std::string &t, D2D1_RECT_F r, float size, const std::string &c, bool bold = false,
              bool center = false, bool serif = false);
};
class MapRenderer {
    struct BorderGroup {
        Com<ID2D1PathGeometry> shape;
        std::string stroke;
        float width = 1, opacity = 1;
    };
    std::vector<BorderGroup> borderGroups;
    const Map *borderMap = nullptr;
    std::set<std::string> borderExcluded;
    struct Responsive;
    std::shared_ptr<Responsive> responsive;
    struct SymbolPart {
        Com<ID2D1PathGeometry> geometry;
        std::string fill, stroke;
        float width;
    };
    std::map<const Json *, std::vector<SymbolPart>> symbolPaths;
    std::map<const Json *, Com<ID2D1PathGeometry>> featurePaths;
    Com<ID2D1StrokeStyle> roundStroke;
    TextCache textCache;
    const Map *geometryMap = nullptr;
    std::unique_ptr<MapRenderer> sceneRenderer;
    Com<ID2D1BitmapRenderTarget> sceneTarget;
    Com<ID2D1Bitmap> sceneBitmap;
    const Map *sceneMap = nullptr;
    ID2D1RenderTarget *sceneOwner = nullptr;
    D2D1_RECT_F sceneBounds{};
    double sceneZoom = 0;
    float sceneDpiX = 0, sceneDpiY = 0;
    ID2D1PathGeometry *featureGeometry(Map &, const Json &);
    void selection(Map &, ID2D1RenderTarget *, D2D1_RECT_F, double, Point, const std::set<std::string> &,
                   bool);
    std::map<std::string, Com<ID2D1Bitmap>> bitmaps;
    std::deque<std::string> order;
    ID2D1RenderTarget *lastTarget = nullptr;
    ID2D1Bitmap *bitmap(Map &map, const Json &layer, ID2D1RenderTarget *target);

  public:
    Com<ID2D1Factory> factory;
    uint64_t sceneBuilds = 0, sceneHits = 0;
    bool displayLabels = true;
    bool paintBorders = true;
    std::set<std::string> excludedBorderArcs;
    void drawBorders(Map &, ID2D1RenderTarget *, D2D1_RECT_F, double, Point);
    void drawControlEditor(Map &, ID2D1RenderTarget *, D2D1_RECT_F, double, Point, const std::string &arc,
                           const std::vector<std::string> &controls, const std::string &node, Point at,
                           bool dragging, const std::vector<std::string> &previewIds);
    void drawDragPreview(Map &, ID2D1RenderTarget *, D2D1_RECT_F, double, Point,
                         const std::set<std::string> &, Point delta, const std::string &node = "");
    Com<IDWriteFactory> textFactory;
    MapRenderer(ID2D1Factory *sharedFactory = nullptr);
    ~MapRenderer();
    void drawResponsive(Map &, ID2D1RenderTarget *, D2D1_RECT_F, double, Point, const std::set<std::string> &,
                        bool nodes, bool moving, HWND notify, bool overlayBorders = true);
    void resetResponsive();
    void drawResponsiveBorders(Map &, ID2D1RenderTarget *, D2D1_RECT_F, double, Point);
    bool responsiveSettled() const;
    bool separateLand(Map &);
    void claimCountry(Map &, const std::string &);
    void drawBorderPreview(Map &, ID2D1RenderTarget *, D2D1_RECT_F, double, Point, const std::string &,
                           Point grab, Point delta, double radius);
    void clear();
    // Call after document edits (including during a drag); camera changes need no invalidation.
    void invalidateScene();
    void drawInteractive(Map &, ID2D1RenderTarget *, D2D1_RECT_F, double, Point,
                         const std::set<std::string> &selected = {}, bool nodes = false,
                         bool cameraMoving = false);
    void draw(Map &map, ID2D1RenderTarget *target, D2D1_RECT_F viewport, double zoom, Point offset,
              const std::set<std::string> &selected = {}, bool nodes = false, bool labels = true);
    void feature(Map &map, const Json &f, ID2D1RenderTarget *target, float layerOpacity = 1, double zoom = 1,
                 bool labels = true, bool drawStroke = true, bool drawFill = true);
    void symbol(const Json &definition, ID2D1RenderTarget *target, Point p, double size, double angle,
                const std::string &stroke, float opacity = 1);
    Com<ID2D1PathGeometry> geometry(const std::vector<std::vector<Point>> &paths, bool closed);
    Image renderImage(Map &map, int width = 0, int height = 0);
    Image flatten(Map &map);
    Image rasterBase(Map &map);
    void exportSvg(Map &map, const fs::path &path);
};
} // namespace atlas
