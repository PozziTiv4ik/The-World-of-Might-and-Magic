#pragma once
#include "dialogs.hpp"
#include "render.hpp"

namespace atlas {
enum class Tool {
    Select,
    Node,
    Region,
    Route,
    River,
    Brush,
    Stamp,
    Label,
    Eraser,
    Pan,
    Zone,
    Trace,
    Measure,
    Rectangle,
    Lasso,
    Fill,
    Picker,
    Land,
    Border
};
enum Command {
    NewFile = 100,
    OpenFile,
    Save,
    SaveAs,
    ExportPng,
    ExportSvg,
    Print,
    AddRaster,
    ExitApp,
    Recover,
    OpenCampaign,
    Undo = 200,
    Redo,
    Copy,
    Paste,
    Duplicate,
    Delete,
    Properties,
    BindEntity,
    OpenCard,
    Rename,
    StrokeColor,
    FillColor,
    Smaller,
    Larger,
    InsertNode,
    Simplify,
    Union,
    Subtract,
    Fit = 300,
    ZoomIn,
    ZoomOut,
    Snap,
    Grid,
    Panels,
    Style,
    FitSelection,
    FilterObjects,
    FilterSymbols,
    MoreTools,
    RotateLeft,
    RotateRight,
    ScaleDown,
    ScaleUp,
    ShowSourceNote,
    Snapshot = 400,
    HistoryView,
    RestoreVersion,
    SelectVersion,
    ShowDiff,
    AddLayer = 500,
    Visibility,
    Lock,
    SelectLayer,
    RenameLayer,
    LayerUp,
    LayerDown,
    DuplicateLayer,
    DeleteLayer,
    LayerProperties,
    SetTool = 1000,
    SetSymbol = 1100,
    SavePreset = 1101,
    LibraryTab = 1200,
    ObjectsTab,
    CampaignTab,
    SelectObject,
    SelectEntity,
    FileMenu = 1300,
    EditMenu,
    MapMenu,
    VersionMenu,
    ViewMenu,
    HelpMenu,
    ClosePanel,
    LayersPanel,
    InspectorPanel,
    ToggleNames,
    SelectionMenu,
    BorderNarrow,
    BorderWide,
    FinishContour,
    CancelContour,
    ScopeBorders,
    ScopeLand,
    ScopeObjects,
    IsolateLayer,
    AddControl,
    RemoveControl,
    ToggleSea,
    ShowArchiveLayers
};
struct Hit {
    D2D1_RECT_F rect;
    int command = 0;
    std::string data;
    std::string tip;
};
class App {
    HWND window = nullptr, searchBox = nullptr, nameBox = nullptr;
    HFONT font = nullptr;
    Com<ID2D1HwndRenderTarget> target;
    Map map;
    Campaign campaign;
    History history;
    MapRenderer renderer;
    std::unique_ptr<MapRenderer> comparisonRenderer;
    TextCache uiTextCache;
    ULONGLONG lastPointerPaint = 0;
    ULONGLONG cameraMovingUntil = 0;
    bool pointerPaintPending = false;
    std::unique_ptr<Map> comparison;
    fs::path initialMap, projectRoot;
    std::vector<Hit> hits;
    std::vector<Json> versions;
    std::set<std::string> selected;
    std::string activeLayer, activeSymbol = "fleet", stroke = "#635D4D",
                             status = "Выбери страну на карте или в списке слева", query;
    std::string objectFilter = "country", symbolFilter;
    Tool tool = Tool::Select;
    double zoom = .25, brushSize = 12, lineWidth = 3;
    Point offset{}, mouse{}, dragStart{}, last{}, panOrigin{}, lastCanvas{};
    float dpi = 1, width = 1500, height = 950, left = 252, right = 298;
    D2D1_RECT_F canvas{};
    bool dirty = false, snap = true, grid = false, down = false, panning = false, historyView = false,
         selectionBox = false, syncName = false, showPanels = true;
    bool refreshing = false;
    int panel = 0;
    bool mapLabels = true, dragPreview = false;
    Point dragDelta{};
    std::set<std::string> dragObjects;
    ULONGLONG hoverSince = 0;
    ULONGLONG noticeUntil = 0;
    uint64_t editSerial = 0, autosavedSerial = 0;
    int hoverHit = -1;
    int versionScroll = 0;
    int layerScroll = 0, objectScroll = 0, sideTab = 1;
    std::string movingNode;
    std::string movingBorder;
    Point borderGrab{};
    double borderRadius = 160;
    SelectionDomain editScope = SelectionDomain::Borders;
    bool isolateLayer = false, allowSea = true, draggingControl = false;
    bool showArchiveLayers = false;
    std::string activeBorder, activeControl;
    std::vector<std::string> activeControls, controlPreviewIds;
    Point controlTarget{};
    void selectBorder(const std::string &);
    void refreshControls();
    void setScope(SelectionDomain);
    bool beginBoundaryEdit(Point);
    std::string hitObject(Point, double, bool parent = true) const;
    Json before;
    std::vector<Point> drawing;
    std::vector<Point> selectionMask;
    std::shared_ptr<Image> painted;
    std::string paintedLayer;
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void layout();
    void paint();
    void paintTools(Painter &);
    void paintSidebar(Painter &);
    void paintProperties(Painter &);
    void paintTimeline(Painter &);
    void paintChrome(Painter &);
    void iconButton(Painter &, D2D1_RECT_F, const std::string &, const std::string &, int,
                    const std::string &data = "", bool active = false);
    void button(Painter &, D2D1_RECT_F, const std::string &, int, const std::string &data = "",
                bool active = false, bool subdued = false);
    void command(int, const std::string &data = "");
    void changed(const std::string &label);
    void updateSelection();
    void fit();
    void fitSelection();
    void invalidate();
    void pointerDown(Point, bool middle = false);
    void pointerMove(Point);
    void pointerUp(Point);
    void finishDrawing();
    Point world(Point p) const;
    bool inCanvas(Point p) const;
    std::string drawingLayer();
    void strokeRaster(Point a, Point b, bool erase);
    void properties();
    void bindEntity();
    void versionDialog();
    void open();
    void save(bool as = false);
    void exportMap(bool svg = false);
    void copy();
    void paste();
    bool discard();
    void chooseStyle(const std::string &);
    void simplifySelected();
    void booleanRegions(bool subtract = false);
    void insertNode();

  public:
    App(fs::path root, fs::path initial) : initialMap(std::move(initial)), projectRoot(std::move(root)) {}
    int run(HINSTANCE instance, int show);
    Image preview(const fs::path &, int state = 0);
};
int runCli(int argc, wchar_t **argv);
int selfTest(const fs::path &temporary, const fs::path &pdn, const fs::path &objectMap = {});
Json benchmarkMap(const fs::path &path);
Json benchmarkCamera(const fs::path &path, bool borders = true);
} // namespace atlas
