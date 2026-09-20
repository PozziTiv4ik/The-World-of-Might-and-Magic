#include "app.hpp"
#include <algorithm>
#include <commdlg.h>
#include <set>
#include <shellapi.h>
#include <sstream>

namespace atlas {
bool App::discard() {
    if(window)SetFocus(window);
    if (!dirty)
        return true;
    int result = confirmDialog(window, L"Сохранить изменения текущей карты?", L"АТЛАС",
                             MB_YESNOCANCEL | MB_ICONQUESTION);
    if (result == IDCANCEL)
        return false;
    if (result == IDYES) {
        save();
        return !dirty;
    }
    return true;
}
void App::save(bool as) {
    if(historyView)command(EditorView);
    if(!as && dirty && !map.doc["parent_version"].str().empty()) {revisionDialog(false);return;}
    if(window)SetFocus(window);
    if (as || map.directory.empty()) {
        auto folder = chooseFolder(window, "Выбери папку для проекта карты");
        if (!folder)
            return;
        auto name = form(window, "Сохранить карту", {{"Имя новой папки", map.doc["name"].str("Карта мира")}});
        if (!name)
            return;
        auto target = safeChild(*folder, (*name)[0]);
        map.save(target);
    } else
        map.save();
    dirty = false;
    status = "Карта сохранена";
    noticeUntil = now() + 2000;
    if(window)SetTimer(window, 5, 2000, nullptr);
    SetWindowTextW(window, wide("АТЛАС — " + map.doc["name"].str()).c_str());
    invalidate();
}
void App::open() {
    if (!discard())
        return;
    auto file = openFile(window, L"Карты и изображения\0*.json;*.pdn;*.png;*.jpg;*.bmp\0Проект "
                                 L"Atlas\0map.json\0Paint.NET\0*.pdn\0Все файлы\0*.*\0\0");
    if (!file)
        return;
    auto ext = file->extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext == L".json")
        map.load(*file);
    else {
        auto folder = chooseFolder(window, "Где сохранить импортированную карту?");
        if (!folder)
            return;
        auto name =
            form(window, "Импорт карты", {{"Имя новой папки", utf8(file->stem().wstring()) + " — Атлас"}});
        if (!name)
            return;
        auto dest = safeChild(*folder, (*name)[0]);
        if (ext == L".pdn")
            map.importPdn(*file, dest);
        else
            map.importRaster(*file, dest);
        map.doc["style"] = "atlas";
        map.doc["symbols"] = defaultSymbols();
        map.save();
    }
    activeLayer = map.vectorLayer();
    selected.clear();
    history.clear();
    comparison.reset();
    historyView = false;
    dirty = false;
    drawing.clear();
    versions = map.versions();
    renderer.clear();
    layout();
    fit();
    updateSelection();
    status = "Открыта карта: " + map.doc["name"].str();
    SetWindowTextW(window, wide("АТЛАС — " + map.doc["name"].str()).c_str());
}
void App::exportMap(bool svg) {
    if(window)SetFocus(window);
    auto file =
        saveFile(window, svg ? L"SVG\0*.svg\0\0" : L"PNG\0*.png\0\0", svg ? L"Карта.svg" : L"Карта.png");
    if (!file)
        return;
    if (svg)
        renderer.exportSvg(map, *file);
    else {
        auto image = renderer.renderImage(map);
        savePng(*file, image);
    }
    status = "Экспорт: " + utf8(file->filename().wstring());
    renderer.clear();
    invalidate();
}
static double number(const std::string &s, double minimum, double maximum) {
    size_t end = 0;
    double value = std::stod(s, &end);
    if (end != s.size() || !std::isfinite(value) || value < minimum || value > maximum)
        throw std::runtime_error("Число должно быть от " + std::to_string(minimum) + " до " +
                                 std::to_string(maximum));
    return value;
}
void App::properties() {
    const Json &f=selected.empty()?map.doc:map.doc["features"][*selected.begin()];
    std::vector<Field> fields;
    std::vector<std::string> keys;
    auto add=[&](const std::string &label,const std::string &key,const std::string &fallback="") {
        keys.push_back(key);
        fields.push_back({label,f[key].isNumber()?std::to_string(f[key].num()):f[key].str(fallback)});
    };
    add("Название","name");
    if(selected.empty())add("Цвет фона (#RRGGBB)","background","#D3E0DE");
    else {
        auto kind=f["kind"].str();
        if(kind=="label"){add("Размер текста","font_size","24");add("Цвет текста (#RRGGBB)","text_color","#333333");}
        else if(kind=="symbol"){add("Размер значка","size","36");add("Поворот, градусы","rotation","0");add("Цвет значка (#RRGGBB)","stroke","#635D4D");}
        else {
            if(f["closed"].boolean())add("Заливка (#RRGGBB или none)","fill","#C4D5BD");
            add("Цвет линии (#RRGGBB)","stroke","#635D4D");add("Толщина линии","stroke_width","2");
        }
        add("Непрозрачность (0–1)","opacity","1");
        if(f.contains("campaign_state"))add("Состояние на карте","campaign_state");
    }
    auto result=form(window,selected.empty()?"Свойства карты":"Свойства объекта",fields,"Применить");
    if(!result)return;
    before=map.doc;
    auto apply=[&](Json &item) {
        for(size_t i=0;i<keys.size();++i) {
            const auto &key=keys[i];const auto &value=(*result)[i];
            if(key=="name" && selected.size()>1)continue;
            if(key=="opacity")item[key]=number(value,0,1);
            else if(key=="size" || key=="font_size")item[key]=number(value,2,2000);
            else if(key=="rotation")item[key]=number(value,-3600,3600);
            else if(key=="stroke_width")item[key]=number(value,.1,300);
            else item[key]=value;
        }
    };
    try {
        if(selected.empty())apply(map.doc);else for(const auto &id:selected)apply(map.doc["features"][id]);
        auto validation=map.validate();if(validation["errors"].size())throw std::runtime_error(validation["errors"].dump());
    }catch(...){map.doc=before;throw;}
    changed("Свойства сохранены");
}
void App::bindEntity() {
    if (selected.empty()) {
        status = "Сначала выбери объект карты";
        invalidate();
        return;
    }
    std::vector<std::string> names{"Не связано"};
    std::vector<const Entity *> entries;
    for (auto &e : campaign.entities)
        if (e.type == "location" || e.type == "character" || e.type == "character_asset") {
            names.push_back(e.id + " · " + e.name);
            entries.push_back(&e);
        }
    auto &f = map.doc["features"][*selected.begin()];
    auto current = campaign.find(f["entity_id"].str());
    auto result = form(window, "Связь с миром",
                       {{"Карточка (место, персонаж или актив)",
                         current ? current->id + " · " + current->name : names[0], names}});
    if (!result)
        return;
    auto choice = std::find(names.begin(), names.end(), (*result)[0]);
    size_t i = size_t(choice - names.begin());
    before = map.doc;
    for (auto &id : selected) {
        auto &obj = map.doc["features"][id];
        obj["entity_id"] = i ? Json(entries.at(i - 1)->id) : Json();
        if (i && obj["name"].str().empty())
            obj["name"] = entries.at(i - 1)->name;
    }
    changed("Объект связан с карточкой");
}
void App::copy() {
    if (selected.empty())
        return;
    Json bundle = fields({{"format", "atlas-selection-1"},
                          {"features", Json::object()},
                          {"nodes", Json::object()},
                          {"arcs", Json::object()},
                          {"symbols", map.doc["symbols"]}});
    for (auto &id : selected) {
        const auto &f = static_cast<const Json &>(map.doc)["features"][id];
        bundle["features"][id] = f;
        auto add = [&](const Json &list) {
            if (!list.isArray())
                return;
            for (auto &ref : list.arr()) {
                auto aid = ref["id"].str();
                bundle["arcs"][aid] = map.doc["arcs"][aid];
                for (auto &n : map.doc["arcs"][aid]["nodes"].arr())
                    bundle["nodes"][n.str()] = map.doc["nodes"][n.str()];
            }
        };
        add(f["arcs"]);
        if (f["rings"].isArray())
            for (auto &ring : f["rings"].arr())
                add(ring);
    }
    writeClipboard(window,bundle.dump());
    status = "Объекты скопированы";
    invalidate();
}
void App::paste() {
    auto text=readClipboard(window);if(!text)return;
    auto bundle=Json::parse(*text);
    if (bundle["format"].str() != "atlas-selection-1")
        throw std::runtime_error("В буфере нет объектов Atlas");
    before = map.doc;
    auto selectedBefore = selected;
    try {
        std::map<std::string, std::string> nodes, arcs;
        for (auto &[id, j] : bundle["nodes"].obj()) {
            auto q = point(j);
            auto nid = map.nodeAt({q.x + 24, q.y + 24}, 0);
            nodes[id] = nid;
        }
        for (auto &[id, a] : bundle["arcs"].obj()) {
            auto copy = a;
            for (auto &n : copy["nodes"].arr())
                n = nodes.at(n.str());
            if(copy.contains("control_nodes")&&copy["control_nodes"].isArray())for(auto &n:copy["control_nodes"].arr())n=nodes.at(n.str());
            auto aid = newId("ARC-");
            map.doc["arcs"][aid] = copy;
            arcs[id] = aid;
        }
        selected.clear();
        auto layer = drawingLayer();
        std::map<std::string,std::string> featureIds;
        for(const auto &[id,f]:bundle["features"].obj())featureIds[id]=newId("MAPOBJ-");
        for (auto &[id, f] : bundle["features"].obj()) {
            auto obj = f;
            auto change = [&](Json &refs) {
                if (refs.isArray())
                    for (auto &r : refs.arr())
                        r["id"] = arcs.at(r["id"].str());
            };
            if(obj.contains("arcs"))change(obj["arcs"]);
            if (obj.contains("rings")&&obj["rings"].isArray())
                for (auto &ring : obj["rings"].arr())
                    change(ring);
            if (obj.contains("position")&&obj["position"].isArray()) {
                auto q = point(obj["position"]);
                obj["position"] = pointJson({q.x + 24, q.y + 24});
            }
            auto fid = featureIds.at(id);
            obj["id"] = fid;
            if(obj.contains("parent_id")&&featureIds.contains(obj["parent_id"].str()))obj["parent_id"]=featureIds.at(obj["parent_id"].str());
            map.doc["next_z"] = map.doc["next_z"].num() + 1;
            obj["z_order"] = map.doc["next_z"];
            obj["layer_id"] = layer;
            map.doc["features"][fid] = obj;
            selected.insert(fid);
        }
        if (bundle["symbols"].isObject())
            for (auto &[id, s] : bundle["symbols"].obj())
                if (!map.doc["symbols"].contains(id))
                    map.doc["symbols"][id] = s;
        auto errors=map.validate()["errors"];
        if(errors.size())throw std::runtime_error("Некорректные объекты в буфере: "+errors.dump());
        changed("Вставлены объекты");
    } catch (...) {
        map.doc = before;
        selected = selectedBefore;
        map.clearCache();
        throw;
    }
}
void App::chooseStyle(const std::string &style) {
    before = map.doc;
    map.doc["style"] = style;
    changed("Изменено оформление карты");
}
void App::simplifySelected() {
    if (selected.empty())
        return;
    before = map.doc;
    auto ids = selected;
    for (auto &id : ids) {
        auto f = map.doc["features"][id];
        auto paths = map.paths(f);
        if (paths.empty())
            continue;
        Json rings = Json::array();
        for (auto &ring : paths) {
            auto pts = simplify(ring, 2 / zoom, f["closed"].boolean());
            if (pts.size() < (f["closed"].boolean() ? 3u : 2u))
                continue;
            auto temp = map.addPath(pts, f["closed"].boolean(), f["kind"].str(), f["layer_id"].str(),
                                    f["stroke"].str(), f["stroke_width"].num(), .001);
            rings.push(map.doc["features"][temp]["arcs"]);
            map.eraseFeature(temp);
        }
        if (rings.size()) {
            map.doc["features"][id].obj().erase("arcs");
            map.doc["features"][id]["rings"] = rings;
        }
    }
    changed("Упрощены выбранные контуры");
}
void App::insertNode() {
    if (selected.empty())
        return;
    double best = 1e30;
    std::string arcId;
    size_t segment = 0;
    Point closest;
    for (auto &id : selected) {
        auto &f = map.doc["features"][id];
        auto process = [&](const Json &refs) {
            if (!refs.isArray())
                return;
            for (auto &ref : refs.arr()) {
                auto aid = ref["id"].str();
                auto &ns = map.doc["arcs"][aid]["nodes"];
                for (size_t i = 1; i < ns.size(); i++) {
                    auto a = point(map.doc["nodes"][ns[i - 1].str()]),
                         b = point(map.doc["nodes"][ns[i].str()]);
                    double d = segmentDistance(lastCanvas, a, b);
                    if (d < best) {
                        best = d;
                        arcId = aid;
                        segment = i;
                        double dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
                        double t =
                            len ? std::clamp(((lastCanvas.x - a.x) * dx + (lastCanvas.y - a.y) * dy) / len,
                                             .02, .98)
                                : .5;
                        closest = {a.x + t * dx, a.y + t * dy};
                    }
                }
            }
        };
        process(f["arcs"]);
        if (f["rings"].isArray())
            for (auto &ring : f["rings"].arr())
                process(ring);
    }
    if (arcId.empty())
        return;
    before = map.doc;
    auto n = map.nodeAt(closest, 0);
    auto &nodes = map.doc["arcs"][arcId]["nodes"].arr();
    nodes.insert(nodes.begin() + segment, n);
    tool = Tool::Node;
    changed("Узел добавлен в общую границу");
}
namespace {
class ContourSink final : public ID2D1SimplifiedGeometrySink {
    ULONG refs = 1;

  public:
    std::vector<std::vector<Point>> paths;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) noexcept override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(ID2D1SimplifiedGeometrySink)) {
            *out = static_cast<ID2D1SimplifiedGeometrySink *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) noexcept override {}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) noexcept override {}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN) noexcept override {
        paths.push_back({{p.x, p.y}});
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F *points, UINT32 n) noexcept override {
        for (UINT32 i = 0; i < n; i++)
            paths.back().push_back({points[i].x, points[i].y});
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT *, UINT32) noexcept override {}
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) noexcept override {}
    HRESULT STDMETHODCALLTYPE Close() noexcept override { return S_OK; }
};
} // namespace
void App::booleanRegions(bool subtract) {
    if (selected.size() < 2) {
        status = "Выбери две территории с Ctrl";
        invalidate();
        return;
    }
    bool land = false, state = false;
    for (const auto &id : selected) {
        auto role = map.doc["features"][id]["role"].str();
        land |= role == "land";
        state |= role == "country";
    }
    if (land && state)
        throw std::runtime_error("Суша и государства редактируются отдельно");
    auto first = *selected.begin();
    auto feature = map.doc["features"][first];
    for (auto &id : selected)
        if (!map.doc["features"][id]["closed"].boolean())
            throw std::runtime_error("Операция работает с замкнутыми территориями");
    auto result = renderer.geometry(map.paths(feature), true);
    bool skip = true;
    for (auto &id : selected) {
        if (skip) {
            skip = false;
            continue;
        }
        auto other = renderer.geometry(map.paths(map.doc["features"][id]), true);
        Com<ID2D1PathGeometry> combined;
        check(renderer.factory->CreatePathGeometry(combined.put()), "Combined geometry");
        Com<ID2D1GeometrySink> sink;
        check(combined->Open(sink.put()), "Combine sink");
        check(result->CombineWithGeometry(other.get(),
                                          subtract ? D2D1_COMBINE_MODE_EXCLUDE : D2D1_COMBINE_MODE_UNION,
                                          nullptr, 1.0f, sink.get()),
              "Combine regions");
        check(sink->Close(), "Close combined geometry");
        result = std::move(combined);
    }
    auto sink = new ContourSink;
    auto hr = result->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES, nullptr, 1.0f, sink);
    auto paths = sink->paths;
    sink->Release();
    check(hr, "Flatten combined boundary");
    if (paths.empty())
        throw std::runtime_error("Результат операции пуст");
    before = map.doc;
    Json rings = Json::array();
    for (auto &p : paths) {
        if (p.size() < 3)
            continue;
        auto id = map.addPath(p, true, "region", feature["layer_id"].str(), feature["fill"].str(),
                              feature["stroke_width"].num(), .001);
        rings.push(map.doc["features"][id]["arcs"]);
        map.eraseFeature(id);
    }
    for (auto &id : selected)
        map.eraseFeature(id);
    auto id = newId("MAPOBJ-");
    feature["id"] = id;
    feature["rings"] = rings;
    feature.obj().erase("arcs");
    feature["entity_id"] = nullptr;
    feature["name"] = subtract ? "Результат вычитания" : "Объединённая область";
    map.doc["features"][id] = feature;
    selected = {id};
    changed(subtract ? "Вычтена область" : "Территории объединены");
}
void App::command(int cmd, const std::string &data) {
    if(headless && (cmd==Print || cmd==OpenCard || cmd==OpenVersionSource || cmd==ExitApp || cmd==OpenCampaign))
        throw std::runtime_error("External OS action is unavailable in windowless replay");
    if(historyView) {
        const std::set<int> writes={Undo,Redo,Paste,Duplicate,Delete,Properties,BindEntity,StrokeColor,FillColor,Smaller,Larger,InsertNode,Simplify,Union,Subtract,Style,AddLayer,Visibility,Lock,LayerUp,LayerDown,LayerProperties,SetTool,SetSymbol,SavePreset,SelectEntity,AddRaster,ToggleSea};
        if(writes.contains(cmd)){status="Для правки нажми «Открыть карту»";noticeUntil=now()+3000;invalidate();return;}
    }
    keyboardHit=-1;
    if ((down || !drawing.empty() || painted) &&
        (cmd==SetTool || cmd==ScopeBorders || cmd==ScopeLand || cmd==ScopeObjects || cmd==SelectLayer ||
         cmd==HistoryView || cmd==CompareView || cmd==EditorView || cmd==OpenMoment || cmd==RestoreVersion))
        rollbackInteraction();
    if (comparisonRenderer && (cmd == SelectVersion || cmd == HistoryView))
        comparisonRenderer->clear();
    switch (cmd) {
    case ScopeBorders:
        setScope(SelectionDomain::Borders);
        invalidate();
        return;
    case ScopeLand:
        setScope(SelectionDomain::Land);
        invalidate();
        return;
    case ScopeObjects:
        setScope(SelectionDomain::Objects);
        invalidate();
        return;
    case IsolateLayer:
        isolateLayer = !isolateLayer;
        selected.clear();
        activeBorder.clear();
        activeControls.clear();
        updateSelection();
        return;
    case ShowArchiveLayers:
        showArchiveLayers = !showArchiveLayers;
        layerScroll = 0;
        invalidate();
        return;
    case ToggleSea:
        allowSea = !allowSea;
        if (!selected.empty()) {
            before = map.doc;
            for (const auto &id : selected)
                if (map.doc["features"][id]["role"].str() == "country")
                    map.doc["features"][id]["territory_scope"] = allowSea ? "land_sea" : "land";
            if (!(before == map.doc))
                changed(allowSea ? "Морская территория включена" : "Заливка только на суше");
        }
        invalidate();
        return;
    case AddControl:
        if (activeBorder.empty())
            return;
        before = map.doc;
        activeControl = map.insertBorderControl(activeBorder, lastCanvas, allowSea);
        changed("Контрольная точка добавлена");
        return;
    case RemoveControl:
        if (activeBorder.empty() || activeControl.empty())
            return;
        before = map.doc;
        map.deleteBorderControl(activeBorder, activeControl);
        activeControl.clear();
        changed("Контрольная точка удалена");
        return;
    case BorderNarrow:
        borderRadius = std::max(24., borderRadius * .75);
        invalidate();
        return;
    case BorderWide:
        borderRadius = std::min(600., borderRadius * 1.333333);
        invalidate();
        return;
    case FinishContour:
        finishDrawing();
        return;
    case CancelContour:
        rollbackInteraction();
        return;
    case ClosePanel:
        panel = 0;showPanels=false;
        layout();
        return;
    case LayersPanel:
    case InspectorPanel:
        panel = panel == (cmd == LayersPanel ? 4 : 5) ? 0 : (cmd == LayersPanel ? 4 : 5);
        showPanels = panel!=0;
        historyView = false;
        layout();
        return;
    case ToggleNames:
        mapLabels = !mapLabels;
        renderer.displayLabels = mapLabels;
        renderer.invalidateScene();
        break;
    case SelectionMenu: {
        HMENU menu = CreatePopupMenu();
        for (auto pair : std::vector<std::pair<int, const wchar_t *>>{{Properties, L"Свойства…"},
                                                                      {Duplicate, L"Дублировать"},
                                                                      {Delete, L"Удалить"},
                                                                      {BindEntity, L"Связать с карточкой…"},
                                                                      {InsertNode, L"Добавить точку"},
                                                                      {Simplify, L"Упростить контур"},
                                                                      {Union, L"Объединить территории"},
                                                                      {Subtract, L"Вычесть территории"}})
            AppendMenuW(menu, MF_STRING, pair.first, pair.second);
        POINT at;
        GetCursorPos(&at);
        int choice = pickMenu(window,menu);
        DestroyMenu(menu);
        if (choice)
            command(choice);
        return;
    }
    case MoreTools: {
        HMENU menu = CreatePopupMenu();
        const wchar_t *names[] = {L"Выбор · V",
                                  L"Узлы границы · N",
                                  L"Государство · P",
                                  L"Маршрут · R",
                                  L"Река · U",
                                  L"Кисть · B",
                                  L"Символ · S",
                                  L"Подпись · T",
                                  L"Ластик · E",
                                  L"Рука · H",
                                  L"Зона · Z",
                                  L"Обводка · W",
                                  L"Измерение · M",
                                  L"Прямоугольник · G",
                                  L"Лассо · L",
                                  L"Заливка · F",
                                  L"Пипетка · I",
                                  L"Новая земля · J",
                                  L"Перетянуть общую границу · K"};
        for (int i = 0; i < 19; i++)
            AppendMenuW(menu, MF_STRING, 2000 + i, names[i]);
        POINT pos;
        GetCursorPos(&pos);
        int id = pickMenu(window,menu);
        DestroyMenu(menu);
        if (id >= 2000 && id < 2019)
            command(SetTool, std::to_string(id - 2000));
        return;
    }
    case FitSelection:
        fitSelection();
        return;
    case ShowSourceNote:
        if (!selected.empty()) {
            const auto &f = map.doc["features"][*selected.begin()];
            confirmDialog(window, wide(f["source_note"].str("Текст не распознан")).c_str(),
                        L"Заметка исходного PDN · требуется проверка", MB_OK);
        }
        return;
    case FilterObjects:
        objectFilter = data;
        objectScroll = 0;
        break;
    case FilterSymbols:
        if (data == "@next" || data == "@prev") {
            int count = 0;
            for (const auto &[id, d] : map.doc["symbols"].obj())
                if (symbolFilter.empty() || d["category"].str() == symbolFilter)
                    count++;
            objectScroll = std::clamp(objectScroll + (data == "@next" ? 3 : -3), 0, std::max(0, count - 3));
            break;
        }
        symbolFilter = data;
        objectScroll = 0;
        break;
    case RotateLeft:
    case RotateRight:
    case ScaleDown:
    case ScaleUp:
        before = map.doc;
        for (auto &id : selected) {
            auto &f = map.doc["features"][id];
            if (f["kind"].str() != "symbol")
                continue;
            if (cmd == ScaleDown || cmd == ScaleUp)
                f["size"] = std::clamp(f["size"].num(32) * (cmd == ScaleUp ? 1.15 : 1 / 1.15), 4.0, 1500.0);
            else
                f["rotation"] = std::fmod(f["rotation"].num() + (cmd == RotateRight ? 15 : 345), 360.0);
        }
        changed("Изменён символ");
        return;
    case FileMenu:
    case EditMenu:
    case MapMenu:
    case VersionMenu:
    case ViewMenu:
    case HelpMenu: {
        HMENU menu = CreatePopupMenu();
        auto item = [&](int id, const wchar_t *title) { AppendMenuW(menu, MF_STRING, id, title); };
        if (cmd == FileMenu) {
            item(HistoryView,L"История карты\tCtrl+H");
            item(Snapshot,L"Сохранить редакцию…\tCtrl+Shift+S");
            item(LibraryTab,L"Библиотека символов");
            item(ObjectsTab,L"Объекты карты");
            item(EditMenu, L"Правка…");
            item(MapMenu, L"Карта…");
            item(ViewMenu, L"Вид…");
            item(VersionMenu, L"Версии…");
            item(HelpMenu, L"Справка и клавиши…");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            item(NewFile, L"Новая карта…\tCtrl+N");
            item(OpenFile, L"Открыть / импортировать…\tCtrl+O");
            item(OpenCampaign, L"Выбрать папку кампании…");
            item(Save, L"Сохранить\tCtrl+S");
            item(SaveAs, L"Сохранить в новую папку…");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            item(AddRaster, L"Добавить изображение слоем…");
            item(Recover, L"Восстановить автосохранение");
            item(ExportPng, L"Экспорт PNG…");
            item(ExportSvg, L"Экспорт SVG…");
            item(Print, L"Печать…");
            item(ExitApp, L"Выйти");
        } else if (cmd == EditMenu) {
            item(Undo, L"Отменить\tCtrl+Z");
            item(Redo, L"Повторить\tCtrl+Y");
            item(Copy, L"Копировать объекты\tCtrl+C");
            item(Paste, L"Вставить объекты\tCtrl+V");
            item(Duplicate, L"Дублировать\tCtrl+D");
            item(Delete, L"Удалить выделенные\tDelete");
            item(Properties, L"Свойства…");
        } else if (cmd == MapMenu) {
            item(BindEntity, L"Связать с карточкой…");
            item(InsertNode, L"Добавить общий узел");
            item(Simplify, L"Упростить контур");
            item(Union, L"Объединить территории");
            item(Subtract, L"Вычесть территории");
            item(Fit, L"Показать всю карту\tHome");
        } else if (cmd == VersionMenu) {
            item(Snapshot, L"Сохранить редакцию…\tCtrl+Shift+S");
            item(NewEvent, L"Новое событие…");
            item(HistoryView, L"История карты\tCtrl+H");
            item(CompareView, L"Сравнить карты…");
            item(DraftsDialog, L"Именованные черновики…");
            item(ShowDiff, L"Посмотреть список изменений");
            item(RestoreVersion, L"Создать рабочую копию выбранной версии");
        } else if (cmd == ViewMenu) {
            item(Fit, L"Весь лист\tHome");
            item(ZoomIn, L"Приблизить");
            item(ZoomOut, L"Отдалить");
            item(Grid, L"Сетка");
            item(ToggleNames, L"Показать / скрыть подписи карты");
            item(LayersPanel, L"Слои");
            item(ObjectsTab, L"Объекты мира");
            item(CampaignTab, L"Кампания");
            item(Snap, L"Привязка к узлам");
            item(Panels, L"Открыть / закрыть панель\tF6");
            item(InspectorPanel,L"Свойства объекта");
            item(FullScreen,L"Полный экран\tF11");
        } else {
            confirmDialog(
                window,
                L"АТЛАС — редактор карт\n\nV — выбор, Ctrl+клик — несколько объектов\nN — общие узлы, P — "
                L"территория\nR — маршрут, U — река, Z — зона\nB — кисть, E — ластик, S — символ, T — "
                L"текст\nW — обводка области по цвету\nG — прямоугольник, L — лассо\nM — измерение, I — "
                L"пипетка, F — заливка\n\nКолесо — масштаб, пробел или средняя кнопка — рука\nEnter / "
                L"двойной клик — завершить линию\nEsc — отменить незавершённый контур\n\nСохраняемые "
                L"координаты — пиксели карты.\nИсходные слои PDN заблокированы; кружок слева от имени "
                L"снимает блокировку.\nКарта не меняет литературный канон автоматически.",
                L"Как работать в редакторе", MB_OK);
            DestroyMenu(menu);
            return;
        }
        POINT pt;
        GetCursorPos(&pt);
        int chosen = pickMenu(window,menu);
        DestroyMenu(menu);
        if (chosen)
            command(chosen);
        return;
    }
    case NewFile: {
        ++editSerial;
        if (!discard())
            return;
        auto result =
            form(window, "Новая карта",
                 {{"Название", "Новая карта"}, {"Ширина в пикселях", "4000"}, {"Высота в пикселях", "3000"}});
        if (!result)
            return;
        int w = int(number((*result)[1], 32, 16384)), h = int(number((*result)[2], 32, 16384));
        if (uint64_t(w) * h > 64000000)
            throw std::runtime_error("Максимум 64 миллиона пикселей");
        map.create(w, h, (*result)[0]);
        map.doc["symbols"] = defaultSymbols();
        activeLayer = map.vectorLayer();
        selected.clear();
        history.clear();
        versions.clear();
        comparison.reset();
        historyView = false;
        dirty = true;
        renderer.clear();
        fit();
        updateSelection();
        return;
    }
    case OpenFile:
        open();
        return;
    case OpenCampaign: {
        auto dir = chooseFolder(window, "Выбери корень проекта кампании");
        if (!dir)
            return;
        if (!fs::exists(*dir / pathOf("09_Реестры/Сущности.json")))
            throw std::runtime_error("В этой папке нет реестра сущностей кампании");
        campaign.load(*dir);
        projectRoot = *dir;
        status = "Кампания подключена: " + utf8(dir->filename().wstring());
        sideTab = 2;
        objectScroll = 0;
        invalidate();
        return;
    }
    case Save:
        save();
        return;
    case SaveAs:
        save(true);
        return;
    case ExportPng:
        exportMap();
        return;
    case ExportSvg:
        exportMap(true);
        return;
    case ExitApp:
        SendMessageW(window, WM_CLOSE, 0, 0);
        return;
    case Undo:
        if (history.undo(map.doc)) {
            dirty = true;
            renderer.clear();
            map.clearCache();
            selected.clear();
            updateSelection();
            status = "Действие отменено";
            ++editSerial;
        }
        break;
    case Redo:
        if (history.redo(map.doc)) {
            dirty = true;
            renderer.clear();
            map.clearCache();
            selected.clear();
            updateSelection();
            status = "Действие повторено";
            ++editSerial;
        }
        break;
    case Copy:
        copy();
        return;
    case Paste:
        paste();
        return;
    case Duplicate:
        copy();
        paste();
        return;
    case Delete:
        if (!selected.empty()) {
            before = map.doc;
            for (auto &id : selected)
                map.eraseFeature(id);
            selected.clear();
            changed("Удалены выделенные объекты");
        }
        break;
    case Properties:
        properties();
        return;
    case BindEntity:
        bindEntity();
        return;
    case OpenCard:
        if (!selected.empty()) {
            auto e = campaign.find(map.doc["features"][*selected.begin()]["entity_id"].str());
            if (e) {
                auto file = safeChild(campaign.root, e->path);
                if (file.extension() != L".md" || !fs::is_regular_file(file))
                    throw std::runtime_error("Карточка должна быть существующим Markdown-файлом проекта");
                ShellExecuteW(window, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else
                status = "У объекта нет связанной карточки";
        }
        break;
    case StrokeColor: {
        auto chosen = data.empty() ? chooseColor(window, stroke) : std::optional<std::string>(data);
        if (chosen) {
            stroke = *chosen;
            if (!selected.empty()) {
                before = map.doc;
                for (auto &id : selected)
                    map.doc["features"][id]
                           [map.doc["features"][id]["kind"].str() == "label" ? "text_color" : "stroke"] =
                        stroke;
                changed("Изменён цвет линии");
            }
        }
        break;
    }
    case FillColor: {
        if (selected.empty())
            return;
        auto chosen = chooseColor(window, map.doc["features"][*selected.begin()]["fill"].str(stroke));
        if (chosen) {
            before = map.doc;
            for (auto &id : selected)
                map.doc["features"][id]["fill"] = *chosen;
            changed("Изменена заливка");
        }
        break;
    }
    case Smaller:
        brushSize = std::max(1.0, brushSize - 2);
        break;
    case Larger:
        brushSize = std::min(500.0, brushSize + 2);
        break;
    case Fit:
        comparisonFocus.clear();
        fit();
        return;
    case ZoomIn:
    case ZoomOut: {
        Point center{(canvas.left + canvas.right) / 2, (canvas.top + canvas.bottom) / 2};
        auto a = world(center);
        zoom = std::clamp(zoom * (cmd == ZoomIn ? 1.2 : 1 / 1.2), .01, 24.0);
        offset = {center.x - a.x * zoom, center.y - a.y * zoom};
        break;
    }
    case Snap:
        snap = !snap;
        break;
    case Grid:
        grid = !grid;
        break;
    case Panels:
        if(historyView&&!compareMode)break;
        showPanels = !showPanels;
        if(!panel)panel=2;
        layout();
        return;
    case Style:
        chooseStyle(data);
        return;
    case Snapshot:revisionDialog();return;
    case NewEvent:versionDialog();return;
    case NewRevision:
        openMoment();status="Правь карту · Ctrl+S сохранит новую редакцию";noticeUntil=now()+3500;invalidate();return;
    case OpenMoment:openMoment();return;
    case CompareRevisions:compareRevisions();return;
    case FullScreen:toggleFullscreen();return;
    case HistoryFilter:
        if(data.empty()){historyMenu(cmd);return;}
        historyChapter=std::stoi(data);versionScroll=0;refreshVersions(true);
        if(!selectedEvent()&&!versions.empty())showVersion(versions.front()["id"].str());
        layout();break;
    case HistoryOptions:historyMenu(cmd);return;
    case HistoryCollection:
        historyChain=data;historyChapter=0;versionScroll=0;refreshVersions(true);
        if(!versions.empty())showVersion(versions.front()["id"].str());layout();break;
    case HistorySort:
        historyOrder=data;versionScroll=0;refreshVersions();revealVersion(versionDetails["id"].str());break;
    case HistorySearch:
        if(data=="clear"){query.clear();if(searchBox)SetWindowTextW(searchBox,L"");refreshVersions();if(!versions.empty())showVersion(versions.front()["id"].str());}
        else historySearch=!historySearch;
        layout();if(historySearch){if(searchBox)SetFocus(searchBox);}else if(window)SetFocus(window);break;
    case HistoryPage:
        versionScroll=std::clamp(versionScroll+(data=="next"?1:-1)*std::max(1,visibleEvents()-1),0,std::max(0,int(storyEvents.size())-visibleEvents()));break;
    case HistoryScale:
        eventSpacing=std::clamp(eventSpacing*(data=="in"?1.2f:1/1.2f),150.f,360.f);revealVersion(versionDetails["id"].str());break;
    case SelectRevision:
        if(data=="choose") {
            const auto *event=selectedEvent();if(!event)break;
            auto list=event->revisions;std::vector<std::string> labels;
            for(size_t i=0;i<list.size();++i)labels.push_back("v"+std::to_string(i+1)+" · "+list[i]["recorded_at"].str()+" · "+list[i]["story_anchor"]["revision_note"].str());
            auto r=form(window,"Редакции карты",{{"Редакция",labels.back(),labels}},"Выбрать");
            if(r){auto index=size_t(std::find(labels.begin(),labels.end(),(*r)[0])-labels.begin());showVersion(list.at(index)["id"].str());}
        }else showVersion(data);
        break;
    case EditorView:
        historyView=false;compareMode=0;diffVisible=false;showPanels=false;panel=0;detailScroll=0;
        layout();if(editorCameraSaved){zoom=editorZoom;offset=editorOffset;}else fit();break;
    case HistoryView:
    case CompareView: {
        if(!historyView){editorZoom=zoom;editorOffset=offset;editorCameraSaved=true;}
        drawing.clear();selected.clear();activeBorder.clear();activeControl.clear();
        historyView=true;showPanels=false;compareMode=cmd==CompareView?1:0;detailScroll=0;
        refreshVersions(true);layout();
        if(!comparison && !versions.empty()) {
            auto id=map.doc["parent_version"].str();bool found=false;
            for(const auto &event:storyEvents)for(const auto &v:event.revisions)if(v["id"].str()==id)found=true;
            if(!found)id=versions.back()["id"].str();
            showVersion(id);
        }
        revealVersion(versionDetails["id"].str());
        if(compareMode){buildDiff();fit();}
        break;
    }
    case SelectVersion:
        showVersion(data);
        if(compareMode)buildDiff();
        break;
    case CompareOverlay:
        compareMode=compareMode==2?1:2;fit();break;
    case ChooseCompareA:chooseComparison(false);return;
    case ChooseCompareB:chooseComparison(true);return;
    case EditVersion:editVersion();return;
    case NewChain:newChain();return;
    case DraftsDialog:draftsDialog();return;
    case RecoveryDialog:recoveryDialog();return;
    case SaveNamedDraft: {
        auto name=form(window,"Сохранить черновик",{{"Название","Моя работа"}});
        if(name)map.stashDraft((*name)[0]);
        break;
    }
    case PreviousVersion:
    case NextVersion: {
        if(versions.empty())break;
        size_t index=0;auto event=selectedEvent();for(size_t i=0;i<storyEvents.size();++i)if(event&&storyEvents[i].key==event->key)index=i;
        int next=std::clamp(int(index)+(cmd==NextVersion?1:-1),0,int(versions.size())-1);
        showVersion(versions[size_t(next)]["id"].str());
        if(compareMode)buildDiff();
        break;
    }
    case FocusDiff:focusDiff(size_t(std::stoul(data)));return;
    case FocusScene: {
        const Json &doc=historyView&&comparison?comparison->doc:map.doc;
        if(doc["campaign_event"]["position"].isArray()) {
            auto at=point(doc["campaign_event"]["position"]);
            double w=(canvas.right-canvas.left)/(compareMode==1?2:1);
            zoom=std::clamp(std::min(w/520.,(canvas.bottom-canvas.top)/400.),.2,3.0);
            offset={canvas.left+w/2-at.x*zoom,(canvas.top+canvas.bottom)/2-at.y*zoom};
            comparisonFocus={"MAPOBJ-CAMPAIGN-CURRENT-EVENT"};
        } else {status="Для этой версии ещё не выбрано место события";noticeUntil=now()+3000;}
        break;
    }
    case ToggleDiff:diffVisible=!showPanels;showPanels=diffVisible;detailScroll=0;layout();break;
    case OpenVersionSource: {
        auto id=versionDetails["story_anchor"]["scene_id"].str();
        if(id.empty())id=versionDetails["story_anchor"]["reference_scene_id"].str();
        auto e=campaign.find(id);
        if(e){auto file=safeChild(campaign.root,e->path);ShellExecuteW(window,L"open",file.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
        else{status="У этой версии нет отдельной сцены";noticeUntil=now()+3000;}
        break;
    }
    case RestoreVersion:openMoment();return;
    case ShowDiff:
        if(comparison){historyView=true;if(!compareMode)compareMode=1;buildDiff();layout();fit();}
        break;
    case Recover:
        recoveryDialog();return;
    case AddLayer: {
        auto result = form(window, "Новый слой",
                           {{"Название", "Новый слой"}, {"Тип", "Объекты", {"Объекты", "Растровый"}}});
        if (!result)
            return;
        if ((*result)[1] == "Растровый" && map.directory.empty()) {
            save();
            if (map.directory.empty())
                return;
        }
        before = map.doc;
        activeLayer = map.addLayer((*result)[0], (*result)[1] == "Растровый" ? "raster" : "vector");
        if ((*result)[1] == "Растровый") {
            Image empty(int(map.doc["width"].num()), int(map.doc["height"].num()));
            auto l = map.layer(activeLayer);
            (*l)["image"] = map.storeImage(empty);
            (*l)["source_type"] = "png";
            (*l)["visibility"] = "gm";
        }
        layerScroll = 0;
        changed("Добавлен слой");
        break;
    }
    case Visibility:
    case Lock: {
        auto l = map.layer(data);
        if (l) {
            before = map.doc;
            auto key = cmd == Visibility ? "visible" : "locked";
            (*l)[key] = !(*l)[key].boolean();
            changed(cmd == Visibility ? "Изменена видимость слоя" : "Изменена блокировка слоя");
        }
        break;
    }
    case SelectLayer: {
        auto *l = map.layer(data);
        auto domain = l ? (*l)["domain"].str() : "";
        setScope(domain == "political"  ? SelectionDomain::Borders
                 : domain == "physical" ? SelectionDomain::Land
                                        : SelectionDomain::Objects);
        isolateLayer = false;
    }
        activeLayer = data;
        status = "Слой: " + (*map.layer(data))["name"].str();
        break;
    case LayerUp:
    case LayerDown: {
        auto &layers = map.doc["layers"].arr();
        for (size_t i = 0; i < layers.size(); i++)
            if (layers[i]["id"].str() == activeLayer) {
                int j = int(i) + (cmd == LayerUp ? 1 : -1);
                if (j >= 0 && j < int(layers.size())) {
                    before = map.doc;
                    std::swap(layers[i], layers[size_t(j)]);
                    changed("Изменён порядок слоёв");
                }
                break;
            }
        break;
    }
    case LayerProperties: {
        auto l = map.layer(activeLayer);
        if (!l)
            return;
        std::vector<std::string> modes = {
            "Обычный",   "Умножение", "Сложение",   "Затемнение основы", "Осветление основы",
            "Отражение", "Свечение",  "Перекрытие", "Разница",           "Негатив",
            "Светлее",   "Темнее",    "Экран",      "Исключающее ИЛИ"};
        auto result = form(window, "Свойства слоя",
                           {{"Название", (*l)["name"].str()},
                            {"Непрозрачность (0–100)", std::to_string(int((*l)["opacity"].num(1) * 100))},
                            {"Режим редактирования",
                             (*l)["locked"].boolean() ? "Заблокирован" : "Доступен",
                             {"Доступен", "Заблокирован"}},
                            {"Режим смешивания", modes.at(size_t((*l)["blend_mode"].num())), modes}});
        if (result) {
            double opacity = number((*result)[1], 0, 100) / 100;
            before = map.doc;
            (*l)["name"] = (*result)[0];
            (*l)["opacity"] = opacity;
            (*l)["locked"] = (*result)[2] == "Заблокирован";
            (*l)["blend_mode"] = size_t(std::find(modes.begin(), modes.end(), (*result)[3]) - modes.begin());
            changed("Изменены свойства слоя");
        }
        break;
    }
    case SetTool: {
        Tool wanted = Tool(std::clamp(std::stoi(data), 0, 18));
        SelectionDomain desired = wanted==Tool::Select?SelectionDomain::All:editScope;
        if (wanted == Tool::Border || wanted == Tool::Region)
            desired = SelectionDomain::Borders;
        else if (wanted == Tool::Land)
            desired = SelectionDomain::Land;
        else if (wanted != Tool::Select && wanted != Tool::Pan && wanted != Tool::Node &&
                 wanted != Tool::Measure)
            desired = SelectionDomain::Objects;
        if (desired != editScope)
            setScope(desired);
        if (wanted != Tool::Select && wanted != Tool::Pan)
            isolateLayer = false;
    }
        tool = Tool(std::clamp(std::stoi(data), 0, 18));
        drawing.clear();
        movingNode.clear();
        movingBorder.clear();
        if (tool == Tool::Stamp) {
            panel = 1;
            showPanels = true;
            historyView = false;
            layout();
            sideTab = 0;
            objectScroll = 0;
            SetWindowTextW(searchBox, L"");
        }
        status = tool == Tool::Node ? "Выбери страну; перетаскивай точки её границы" : "Инструмент выбран";
        break;
    case SetSymbol: {
        const Json &symbols=map.doc["symbols"];
        const auto &definition=symbols[data];
        if(!definition.isObject())throw std::runtime_error("Неизвестный символ: "+data);
        if (editScope != SelectionDomain::Objects)
            setScope(SelectionDomain::Objects);
        activeSymbol = data;
        tool = Tool::Stamp;
        status = "Символ: " + definition["name"].str(data);
        if (definition["defaults"].isObject()) {
            stroke = definition["defaults"]["stroke"].str(stroke);
            brushSize = definition["defaults"]["size"].num(brushSize * 3) / 3;
        }
        break;
    }
    case SavePreset: {
        if (selected.empty())
            return;
        auto &f = map.doc["features"][*selected.begin()];
        if (f["kind"].str() != "symbol")
            return;
        auto result = form(window, "Шаблон символа", {{"Название шаблона", "Мой символ"}});
        if (!result || (*result)[0].empty())
            return;
        auto definition = map.doc["symbols"][f["symbol_id"].str()];
        if (!definition.isObject())
            definition = defaultSymbols()[f["symbol_id"].str()];
        definition["name"] = (*result)[0];
        definition["defaults"] = Json::object();
        for (auto k :
             {"size", "rotation", "stroke", "fill", "stroke_width", "font_size", "opacity", "label_offset"})
            definition["defaults"][k] = f[k];
        before = map.doc;
        activeSymbol = newId("SYMBOL-");
        map.doc["symbols"][activeSymbol] = definition;
        sideTab = 0;
        objectScroll = 0;
        SetWindowTextW(searchBox, L"");
        changed("Сохранён шаблон символа");
        break;
    }
    case LibraryTab:
    case ObjectsTab:
    case CampaignTab:
        sideTab = cmd - LibraryTab;
        panel = sideTab + 1;
        if (data == "places") {
            panel = 1;
            symbolFilter = "Поселения";
        }
        showPanels = true;
        historyView = false;
        layout();
        objectScroll = 0;
        SetWindowTextW(searchBox, L"");
        break;
    case SelectObject: {
        auto role = static_cast<const Json &>(map.doc)["features"][data]["role"].str();
        setScope(role == "country" ? SelectionDomain::Borders
                 : role == "land"  ? SelectionDomain::Land
                                   : SelectionDomain::Objects);
    }
        selected = {data};
        updateSelection();
        fitSelection();
        break;
    case SelectEntity:
        if (!selected.empty()) {
            before = map.doc;
            for (auto &id : selected)
                map.doc["features"][id]["entity_id"] = data;
            changed("Добавлена связь с сущностью");
        } else {
            for (auto &[id, f] : map.doc["features"].obj())
                if (f["entity_id"].str() == data)
                    selected.insert(id);
            if (selected.empty())
                status = "Поставь символ и выбери эту карточку для связи";
            updateSelection();
        }
        break;
    case InsertNode:
        if (editScope == SelectionDomain::Borders && !activeBorder.empty()) {
            command(AddControl);
            return;
        }
        insertNode();
        return;
    case Simplify:
        simplifySelected();
        return;
    case Union:
        booleanRegions(false);
        return;
    case Subtract:
        booleanRegions(true);
        return;
    case AddRaster: {
        if (map.directory.empty()) {
            save();
            if (map.directory.empty())
                return;
        }
        auto file = openFile(window, L"Изображения\0*.png;*.jpg;*.bmp\0\0");
        if (!file)
            return;
        auto im = loadImage(*file);
        if (im->width != map.doc["width"].num() || im->height != map.doc["height"].num())
            throw std::runtime_error("Размер слоя должен совпадать с размером карты");
        before = map.doc;
        activeLayer = map.addLayer(utf8(file->filename().wstring()), "raster");
        auto l = map.layer(activeLayer);
        (*l)["image"] = map.storeImage(*im);
        (*l)["source_type"] = "png";
        (*l)["visibility"] = "gm";
        changed("Добавлен растровый слой");
        break;
    }
    case Print: {
        PRINTDLGW pd{sizeof pd};
        pd.hwndOwner = window;
        pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS;
        if (!PrintDlgW(&pd))
            return;
        auto im = renderer.renderImage(map);
        DOCINFOW info{sizeof info};
        info.lpszDocName = L"Карта — АТЛАС";
        if (StartDocW(pd.hDC, &info) > 0) {
            StartPage(pd.hDC);
            BITMAPINFO bitmap{};
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap.bmiHeader.biWidth = im.width;
            bitmap.bmiHeader.biHeight = -im.height;
            bitmap.bmiHeader.biPlanes = 1;
            bitmap.bmiHeader.biBitCount = 32;
            bitmap.bmiHeader.biCompression = BI_RGB;
            int pw = GetDeviceCaps(pd.hDC, HORZRES), ph = GetDeviceCaps(pd.hDC, VERTRES);
            double z = std::min(double(pw) / im.width, double(ph) / im.height);
            SetStretchBltMode(pd.hDC, HALFTONE);
            StretchDIBits(pd.hDC, 0, 0, int(im.width * z), int(im.height * z), 0, 0, im.width, im.height,
                          im.bgra.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
            EndPage(pd.hDC);
            EndDoc(pd.hDC);
        }
        DeleteDC(pd.hDC);
        if (pd.hDevMode)
            GlobalFree(pd.hDevMode);
        if (pd.hDevNames)
            GlobalFree(pd.hDevNames);
        renderer.clear();
        break;
    }
    }
    invalidate();
}
} // namespace atlas
