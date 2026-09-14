#include "app.hpp"
#include <algorithm>
namespace atlas {
static bool over(D2D1_RECT_F r, Point p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
static bool matches(const std::string &s, const std::string &q) {
    auto a = wide(s), b = wide(q);
    if (!a.empty())
        CharLowerBuffW(a.data(), DWORD(a.size()));
    if (!b.empty())
        CharLowerBuffW(b.data(), DWORD(b.size()));
    return a.find(b) != std::wstring::npos;
}
static constexpr auto surface = "#252F34", ink = "#E4EBEA", muted = "#96A8A9", accent = "#62C7C5";
static void dock(Painter &p, D2D1_RECT_F r) {
    auto s = r;
    s.left += 2;
    s.right += 2;
    s.top += 3;
    s.bottom += 3;
    p.rounded(s, "#9BAEAA", 8);
    p.rounded(r, surface, 7);
}
// Deterministic, font-independent line icons.
static void icon(Painter &p, const std::string &n, D2D1_RECT_F r, const std::string &c) {
    double x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    auto line = [&](double a, double b, double d, double e) {
        p.line({x + a, y + b}, {x + d, y + e}, c, 1.6f);
    };
    auto path = [&](std::initializer_list<Point> ps) {
        bool first = true;
        Point old{};
        for (auto q : ps) {
            if (!first)
                line(old.x, old.y, q.x, q.y);
            old = q;
            first = false;
        }
    };
    auto circle = [&](double a, double b, float rad) { p.circle({x + a, y + b}, rad, c, false); };
    if (n == "arrow")
        path({{-7, -11}, {9, 1}, {2, 3}, {-1, 11}, {-7, -11}});
    else if (n == "hand")
        path({{-8, 1},  {-8, -4}, {-5, -5}, {-5, 2},  {-5, -9}, {-2, -10}, {-2, 1}, {-2, -12},
              {1, -12}, {1, 1},   {1, -9},  {4, -9},  {4, 2},   {4, -5},   {7, -5}, {7, 4},
              {4, 10},  {-3, 10}, {-8, 4},  {-11, 0}, {-8, -2}, {-5, 2}});
    else if (n == "node") {
        path({{-9, 7}, {-2, -6}, {9, -2}});
        circle(-9, 7, 2.6f);
        circle(-2, -6, 2.6f);
        circle(9, -2, 2.6f);
    } else if (n == "pen") {
        path({{-9, 9}, {-6, 2}, {6, -10}, {10, -6}, {-2, 6}, {-9, 9}});
        line(4, -8, 8, -4);
    } else if (n == "land") {
        path({{-11, 7},
              {-9, 1},
              {-5, -1},
              {-4, -7},
              {1, -10},
              {5, -5},
              {9, -3},
              {11, 3},
              {7, 8},
              {-1, 10},
              {-11, 7}});
        line(-8, 12, 8, 12);
    } else if (n == "flag") {
        line(-8, -10, -8, 11);
        path({{-8, -9}, {2, -9}, {2, -4}, {10, -4}, {6, 2}, {-8, 2}});
    } else if (n == "mountain") {
        path({{-11, 8}, {-4, -8}, {0, 0}, {4, -11}, {12, 8}, {-11, 8}});
        path({{-6, -4}, {-3, 0}, {-1, -2}});
    } else if (n == "castle") {
        path({{-10, 10}, {-10, -3}, {-7, -3}, {-7, 0}, {-4, 0}, {-4, -3}, {-1, -3}, {-1, 10}});
        path({{1, 10}, {1, -7}, {4, -11}, {7, -7}, {7, 10}});
        line(-12, 10, 11, 10);
    } else if (n == "text") {
        line(-8, -9, 8, -9);
        line(0, -9, 0, 10);
        line(-4, 10, 4, 10);
    } else if (n == "ruler") {
        path({{-11, 5}, {5, -11}, {11, -5}, {-5, 11}, {-11, 5}});
        for (int i = 0; i < 4; i++)
            line(-6 + i * 4, 2 - i * 4, -3 + i * 4, 5 - i * 4);
    } else if (n == "menu") {
        line(-8, -6, 8, -6);
        line(-8, 0, 8, 0);
        line(-8, 6, 8, 6);
    } else if (n == "more") {
        for (int i = -1; i <= 1; i++)
            p.circle({x + i * 6, y}, 1.5f, c);
    } else if (n == "undo" || n == "redo") {
        double s = n == "redo" ? -1 : 1;
        line(-9 * s, -3, 3 * s, -3);
        path({{3 * s, -3}, {8 * s, 0}, {8 * s, 5}, {4 * s, 8}, {-2 * s, 8}});
        line(-9 * s, -3, -4 * s, -8);
        line(-9 * s, -3, -4 * s, 2);
    } else if (n == "search") {
        circle(-2, -2, 6);
        line(3, 3, 10, 10);
    } else if (n == "save") {
        path({{-9, -10}, {6, -10}, {10, -6}, {10, 10}, {-9, 10}, {-9, -10}});
        path({{-4, -10}, {-4, -3}, {5, -3}, {5, -10}});
        path({{-4, 10}, {-4, 3}, {5, 3}, {5, 10}});
    } else if (n == "export") {
        path({{-9, 0}, {-9, 9}, {9, 9}, {9, 0}});
        line(0, 3, 0, -10);
        line(0, -10, -5, -5);
        line(0, -10, 5, -5);
    } else if (n == "layers") {
        path({{-11, -4}, {0, -10}, {11, -4}, {0, 2}, {-11, -4}});
        path({{-11, 2}, {0, 8}, {11, 2}});
        path({{-11, 7}, {0, 13}, {11, 7}});
    } else if (n == "book")
        path({{0, 10},
              {0, -8},
              {-8, -10},
              {-11, -8},
              {-11, 8},
              {-7, 7},
              {0, 10},
              {7, 7},
              {11, 8},
              {11, -8},
              {8, -10},
              {0, -8}});
    else if (n == "gear") {
        circle(0, 0, 6);
        circle(0, 0, 2);
        for (int i = 0; i < 8; i++) {
            double a = i * 3.14159265 / 4;
            line(7 * cos(a), 7 * sin(a), 10 * cos(a), 10 * sin(a));
        }
    } else if (n == "x") {
        line(-6, -6, 6, 6);
        line(-6, 6, 6, -6);
    } else if (n == "plus") {
        line(-7, 0, 7, 0);
        line(0, -7, 0, 7);
    } else if (n == "minus")
        line(-7, 0, 7, 0);
    else if (n == "fit") {
        for (int a : {-1, 1})
            for (int b : {-1, 1}) {
                line(a * 9, b * 3, a * 9, b * 9);
                line(a * 9, b * 9, a * 3, b * 9);
            }
    } else if (n == "sea") {
        for (int y : {-5, 2, 9})
            path({{-11, double(y)},
                  {-7, double(y - 2)},
                  {-2, double(y + 1)},
                  {3, double(y - 2)},
                  {8, double(y + 1)},
                  {11, double(y)}});
    } else if (n == "history") {
        circle(0, 0, 9);
        line(0, -5, 0, 0);
        line(0, 0, 5, 3);
    } else if (n == "eye" || n == "eye_off") {
        path({{-11, 0}, {-5, -6}, {5, -6}, {11, 0}, {5, 6}, {-5, 6}, {-11, 0}});
        circle(0, 0, 3);
        if (n == "eye_off")
            line(-10, -10, 10, 10);
    } else if (n == "unlock") {
        path({{-7, -1}, {7, -1}, {7, 10}, {-7, 10}, {-7, -1}});
        path({{-4, -1}, {-4, -7}, {-1, -10}, {3, -10}, {7, -7}});
        line(0, 3, 0, 6);
    } else if (n == "lock") {
        path({{-7, -1}, {7, -1}, {7, 10}, {-7, 10}, {-7, -1}});
        path({{-4, -1}, {-4, -7}, {-1, -10}, {2, -10}, {5, -7}, {5, -1}});
        line(0, 3, 0, 6);
    } else if (n == "move") {
        line(-10, 0, 10, 0);
        line(0, -10, 0, 10);
        path({{-6, -4}, {-10, 0}, {-6, 4}});
        path({{6, -4}, {10, 0}, {6, 4}});
        path({{-4, -6}, {0, -10}, {4, -6}});
        path({{-4, 6}, {0, 10}, {4, 6}});
    } else if (n == "link") {
        path({{-1, -7}, {3, -11}, {9, -11}, {11, -8}, {11, -3}, {7, 1}});
        path({{1, 7}, {-3, 11}, {-9, 11}, {-11, 8}, {-11, 3}, {-7, -1}});
        line(-5, 5, 5, -5);
    } else if (n == "trash") {
        path({{-7, -5}, {-6, 10}, {6, 10}, {7, -5}});
        line(-10, -6, 10, -6);
        path({{-4, -6}, {-4, -10}, {4, -10}, {4, -6}});
        line(-2, -2, -2, 6);
        line(2, -2, 2, 6);
    } else if (n == "up" || n == "down") {
        double s = n == "up" ? 1 : -1;
        line(0, -9 * s, 0, 9 * s);
        line(0, -9 * s, -5, -4 * s);
        line(0, -9 * s, 5, -4 * s);
    } else if (n == "color")
        p.circle({x, y}, 8, c);
    else {
        circle(0, 0, 9);
        p.text("?", r, 15, c, false, true);
    }
}
void App::iconButton(Painter &p, D2D1_RECT_F r, const std::string &glyph, const std::string &tip, int cmd,
                     const std::string &data, bool active) {
    if (active || over(r, mouse))
        p.rounded(r, active ? "#285D62" : "#39474D", 5);
    icon(p, glyph, r, active ? accent : ink);
    hits.push_back({r, cmd, data, tip});
}
void App::button(Painter &p, D2D1_RECT_F r, const std::string &title, int cmd, const std::string &data,
                 bool active, bool subdued) {
    p.rounded(r, active ? "#285D62" : over(r, mouse) ? "#3C4B50" : subdued ? surface : "#303D43", 5);
    p.text(title, r, 12, active ? accent : ink, active, true);
    hits.push_back({r, cmd, data, title});
}
void App::paintTools(Painter &p) {
    p.fill({0, 0, width, 44}, surface);
    iconButton(p, {8, 4, 44, 40}, "menu", "Файл и команды", FileMenu);
    p.text("АТЛАС 4", {54, 0, 139, 44}, 17, ink, true);
    p.line({142, 13}, {142, 31}, "#58676C");
    p.text(map.doc["name"].str(), {157, 0, width - 255, 44}, 12, muted);
    const char *gs[] = {"undo", "redo", "search", "save", "export"};
    const char *ts[] = {"Отменить · Ctrl+Z", "Повторить · Ctrl+Y", "Объекты · Ctrl+F", "Сохранить · Ctrl+S",
                        "Экспорт PNG"};
    int cs[] = {Undo, Redo, ObjectsTab, Save, ExportPng};
    for (int i = 0; i < 5; i++)
        iconButton(p, {width - 220 + i * 43, 4, width - 184 + i * 43, 40}, gs[i], ts[i], cs[i], "",
                   i == 3 && dirty);
    dock(p, {84, 60, 424, 106});
    hits.push_back({{84, 60, 424, 106}, 0, {}});
    button(p, {90, 66, 198, 100}, "Границы", ScopeBorders, "", editScope == SelectionDomain::Borders);
    button(p, {204, 66, 296, 100}, "Суша", ScopeLand, "", editScope == SelectionDomain::Land);
    button(p, {302, 66, 418, 100}, "Объекты", ScopeObjects, "", editScope == SelectionDomain::Objects);
    if (isolateLayer) {
        const auto *l = map.layer(activeLayer);
        if (l) {
            p.rounded({84, 108, 424, 138}, "#29383D", 5);
            hits.push_back({{84, 108, 424, 138}, IsolateLayer, {}});
            p.text("Только слой: " + (*l)["name"].str(), {94, 109, 415, 137}, 11, "#B5D9D5");
        }
    }
    dock(p, {16, 60, 68, 527});
    hits.push_back({{16, 60, 68, 527}, 0, {}});
    int tools[] = {0, 9, 18, 17, 2, 6, 7};
    const char *g[] = {"arrow", "hand", "node", "land", "flag", "mountain", "text"};
    const char *t[] = {
        "Выбор · V",   "Камера · H", "Перетянуть границу · K", "Новая земля · J", "Новое государство · P",
        "Символы · S", "Подпись · T"};
    for (int i = 0; i < 7; i++)
        iconButton(p, {22, 66 + i * 50.f, 62, 106 + i * 50.f}, g[i], t[i], SetTool, std::to_string(tools[i]),
                   int(tool) == tools[i]);
    iconButton(p, {22, 416, 62, 456}, "castle", "Поселения", LibraryTab, "places");
    iconButton(p, {22, 476, 62, 516}, "more", "Все инструменты", MoreTools);
    dock(p, {width - 168, 60, width - 16, 108});
    hits.push_back({{width - 168, 60, width - 16, 108}, 0, {}});
    iconButton(p, {width - 163, 64, width - 119, 104}, "layers", "Слои", LayersPanel, "", panel == 4);
    iconButton(p, {width - 115, 64, width - 71, 104}, "book", "Символы", LibraryTab, "", panel == 1);
    iconButton(p, {width - 67, 64, width - 23, 104}, "gear", "Свойства", InspectorPanel, "", panel == 5);
}
void App::paintSidebar(Painter &p) {
    if (!showPanels || panel < 1 || panel > 3 || historyView)
        return;
    float x = width - 340, y = 124, b = std::min(height - 78, panel == 1 ? 626.f : 720.f);
    dock(p, {x, y, width - 16, b});
    hits.push_back({{x, y, width - 16, b}, 0, {}});
    p.text(panel == 1   ? "Символы"
           : panel == 2 ? "Объекты"
                        : "Кампания",
           {x + 16, y + 6, width - 64, y + 44}, 15, ink, true);
    iconButton(p, {width - 58, y + 4, width - 22, y + 40}, "x", "Закрыть", ClosePanel);
    if (panel == 1) {
        const char *labels[] = {"Все", "Рельеф", "Места", "Войска"};
        const char *filters[] = {"", "Рельеф", "Поселения", "Войска и флот"};
        for (int i = 0; i < 4; i++)
            button(p, {x + 14 + i * 75, y + 90, x + 84 + i * 75, y + 118}, labels[i], FilterSymbols,
                   filters[i], symbolFilter == filters[i], true);
        std::vector<std::string> ids;
        for (auto s :
             {"mountain", "ridge", "forest", "castle", "fortress", "capital", "port", "ship", "fleet"})
            if (map.doc["symbols"].contains(s))
                ids.push_back(s);
        for (auto &[id, d] : map.doc["symbols"].obj())
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
        int n = 0, skip = 0;
        for (auto &id : ids) {
            const auto &d = map.doc["symbols"][id];
            if (!matches(d["name"].str(), query) ||
                (!symbolFilter.empty() && d["category"].str() != symbolFilter))
                continue;
            if (skip++ < objectScroll)
                continue;
            float tx = x + 14 + (n % 3) * 99.f, ty = y + 132 + (n / 3) * 91.f;
            if (ty + 82 > b - 56)
                break;
            D2D1_RECT_F r{tx, ty, tx + 91, ty + 82};
            p.rounded(r, activeSymbol == id ? "#DCE9DD" : "#EEE8D5", 5);
            renderer.symbol(d, p.target, {tx + 45, ty + 41}, 64, 0, "#655E4D");
            if (activeSymbol == id)
                p.rect({tx + 1, ty + 1, tx + 90, ty + 81}, accent, 2);
            hits.push_back({r, SetSymbol, id, d["name"].str()});
            n++;
        }
        iconButton(p, {x + 16, b - 47, x + 52, b - 11}, "minus", "Уменьшить символ", Smaller);
        p.text(std::to_string(int(brushSize * 3)), {x + 57, b - 47, x + 111, b - 11}, 12, ink, false, true);
        iconButton(p, {x + 115, b - 47, x + 151, b - 11}, "plus", "Увеличить символ", Larger);
        iconButton(p, {x + 167, b - 47, x + 203, b - 11}, "color", "Цвет символа", StrokeColor);
        iconButton(p, {width - 104, b - 47, width - 68, b - 11}, "up", "Предыдущие символы", FilterSymbols,
                   "@prev");
        iconButton(p, {width - 58, b - 47, width - 22, b - 11}, "down", "Следующие символы", FilterSymbols,
                   "@next");
    } else if (panel == 2) {
        const char *ns[] = {"Страны", "Места", "Все"};
        const char *fs[] = {"country", "settlement", ""};
        for (int i = 0; i < 3; i++)
            button(p, {x + 14 + i * 99, y + 90, x + 106 + i * 99, y + 118}, ns[i], FilterObjects, fs[i],
                   objectFilter == fs[i], true);
        std::vector<const Json *> list;
        for (const auto &[id, f] : map.doc["features"].obj()) {
            if (!query.empty()) {
                if (!matches(f["name"].str() + " " + id, query))
                    continue;
            } else if (!objectFilter.empty() && f["role"].str() != objectFilter &&
                       !(objectFilter == "country" && f["kind"].str() == "region"))
                continue;
            list.push_back(&f);
        }
        std::sort(list.begin(), list.end(),
                  [](auto a, auto b) { return (*a)["name"].str() < (*b)["name"].str(); });
        objectScroll = std::clamp(objectScroll, 0, std::max(0, int(list.size()) - 1));
        int n = 0;
        for (size_t i = objectScroll; i < list.size(); i++) {
            const auto &f = *list[i];
            float yy = y + 130 + n++ * 42;
            if (yy + 38 > b - 12)
                break;
            D2D1_RECT_F r{x + 10, yy, width - 26, yy + 38};
            bool a = selected.contains(f["id"].str());
            if (a || over(r, mouse))
                p.rounded(r, a ? "#285D62" : "#34434A", 4);
            p.circle({x + 25, yy + 19}, 5, f["fill"].str("#9BAEAB"));
            p.text(f["name"].str("Без названия"), {x + 42, yy, width - 32, yy + 38}, 12, ink);
            hits.push_back({r, SelectObject, f["id"].str(), f["name"].str()});
        }
    } else {
        auto list = campaign.search(query);
        int n = 0;
        for (size_t i = objectScroll; i < list.size(); i++) {
            float yy = y + 94 + n++ * 44;
            if (yy + 40 > b - 12)
                break;
            auto e = list[i];
            button(p, {x + 12, yy, width - 28, yy + 38}, e->name, SelectEntity, e->id, false, true);
        }
    }
}
void App::paintProperties(Painter &p) {
    if (!showPanels || (!historyView && panel != 4 && panel != 5))
        return;
    float x = width - 340, y = 124, b = std::min(height - 78, 720.f);
    dock(p, {x, y, width - 16, b});
    hits.push_back({{x, y, width - 16, b}, 0, {}});
    p.text(historyView  ? "Версии"
           : panel == 4 ? "Слои"
                        : "Свойства",
           {x + 16, y + 6, width - 64, y + 44}, 15, ink, true);
    iconButton(p, {width - 58, y + 4, width - 22, y + 40}, "x", "Закрыть", ClosePanel);
    if (historyView) {
        button(p, {x + 14, y + 54, width - 30, y + 90}, "+  Версия по событию", Snapshot, "", true);
        int n = 0;
        int skipped = 0;
        for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
            if (skipped++ < versionScroll)
                continue;
            float yy = y + 108 + n++ * 68;
            if (yy + 62 > b - 58)
                break;
            D2D1_RECT_F r{x + 14, yy, width - 30, yy + 60};
            p.rounded(r, "#334148", 5);
            p.text((*it)["label"].str(), {x + 24, yy + 3, width - 38, yy + 31}, 12, ink, true);
            p.text((*it)["recorded_at"].str(), {x + 24, yy + 30, width - 38, yy + 57}, 10, muted);
            hits.push_back({r, SelectVersion, (*it)["id"].str()});
        }
        button(p, {x + 14, b - 46, x + 144, b - 12}, "Изменения", ShowDiff);
        button(p, {x + 152, b - 46, width - 30, b - 12}, "Восстановить", RestoreVersion);
    } else if (panel == 4) {
        p.text("↑ Границы всегда сверху", {x + 16, y + 45, width - 30, y + 73}, 12, accent);
        iconButton(p, {x + 12, y + 78, x + 48, y + 114}, "plus", "Новый слой", AddLayer);
        iconButton(p, {x + 55, y + 78, x + 91, y + 114}, "arrow", "Выбирать только в активном слое",
                   IsolateLayer, "", isolateLayer);
        iconButton(p, {x + 98, y + 78, x + 134, y + 114}, "up", "Заливка слоя выше (границы закреплены)",
                   LayerUp);
        iconButton(p, {x + 141, y + 78, x + 177, y + 114}, "down", "Заливка слоя ниже (границы закреплены)",
                   LayerDown);
        button(p, {x + 195, y + 81, width - 30, y + 111}, "Архив", ShowArchiveLayers, "", showArchiveLayers,
               true);
        std::vector<const Json *> list;
        for (const auto &l : map.doc["layers"].arr()) {
            bool archive = l["name"].str().starts_with("Архив");
            if (archive && !showArchiveLayers)
                continue;
            list.push_back(&l);
        }
        auto rank = [](const Json &l) {
            return l["domain"].str() == "political"       ? 0
                   : l["domain"].str() == "physical"      ? 1
                   : l["name"].str().starts_with("Архив") ? 3
                                                          : 2;
        };
        std::stable_sort(list.begin(), list.end(), [&](auto a, auto b) { return rank(*a) < rank(*b); });
        layerScroll = std::clamp(layerScroll, 0, std::max(0, int(list.size()) - 1));
        int n = 0;
        for (size_t i = layerScroll; i < list.size(); i++) {
            const Json &l = *list[i];
            float yy = y + 132 + n++ * 46;
            if (yy + 40 > b - 12)
                break;
            if (l["id"].str() == activeLayer)
                p.rounded({x + 8, yy, width - 24, yy + 40}, "#28545A", 4);
            iconButton(p, {x + 10, yy + 2, x + 44, yy + 36}, l["visible"].boolean(true) ? "eye" : "eye_off",
                       "Показать / скрыть слой", Visibility, l["id"].str());
            iconButton(p, {x + 46, yy + 2, x + 80, yy + 36}, l["locked"].boolean() ? "lock" : "unlock",
                       "Запретить / разрешить изменение", Lock, l["id"].str(), l["locked"].boolean());
            p.text(l["name"].str(), {x + 91, yy, width - 31, yy + 40}, 12,
                   l["visible"].boolean(true) ? ink : muted);
            hits.push_back({{x + 85, yy, width - 26, yy + 40},
                            SelectLayer,
                            l["id"].str(),
                            "Редактировать только: " + l["name"].str()});
        }
    } else if (selected.empty()) {
        p.text("Выберите объект", {x + 18, y + 57, width - 32, y + 96}, 13, muted);
        button(p, {x + 14, y + 112, width - 30, y + 146}, "Объекты мира", ObjectsTab);
        button(p, {x + 14, y + 158, width - 30, y + 192}, "Кампания", CampaignTab);
        iconButton(p, {x + 14, y + 210, x + 54, y + 250}, "eye", "Подписи карты", ToggleNames, "", mapLabels);
        iconButton(p, {x + 66, y + 210, x + 106, y + 250}, "node", "Привязка к точкам", Snap, "", snap);
        iconButton(p, {x + 118, y + 210, x + 158, y + 250}, "ruler", "Сетка", Grid, "", grid);
    } else {
        const auto &f = map.doc["features"][*selected.begin()];
        p.text(f["name"].str(), {x + 18, y + 49, width - 32, y + 84}, 14, ink, true);
        int cs[] = {Properties, FillColor, StrokeColor, FitSelection,
                    BindEntity, OpenCard,  Duplicate,   Delete};
        const char *gs[] = {"gear", "color", "pen", "fit", "link", "export", "plus", "trash"};
        const char *ts[] = {"Все свойства",        "Заливка",          "Цвет линии / символа", "К объекту",
                            "Связать с карточкой", "Открыть карточку", "Дублировать",          "Удалить"};
        for (int i = 0; i < 8; i++)
            iconButton(p,
                       {x + 17 + (i % 4) * 72.f, y + 101 + (i / 4) * 52.f, x + 61 + (i % 4) * 72.f,
                        y + 145 + (i / 4) * 52.f},
                       gs[i], ts[i], cs[i]);
        button(p, {x + 14, y + 225, width - 30, y + 260}, "Кампания", CampaignTab);
        if (f["kind"].str() == "symbol") {
            iconButton(p, {x + 18, y + 280, x + 58, y + 320}, "minus", "Уменьшить", ScaleDown);
            iconButton(p, {x + 71, y + 280, x + 111, y + 320}, "plus", "Увеличить", ScaleUp);
            iconButton(p, {x + 124, y + 280, x + 164, y + 320}, "undo", "Повернуть влево", RotateLeft);
            iconButton(p, {x + 177, y + 280, x + 217, y + 320}, "redo", "Повернуть вправо", RotateRight);
            iconButton(p, {x + 230, y + 280, x + 270, y + 320}, "save", "Сохранить шаблон", SavePreset);
        }
    }
}
void App::paintTimeline(Painter &p) {
    if (!drawing.empty()) {
        float x = (width - 176) / 2, y = height - 118;
        dock(p, {x, y, x + 176, y + 42});
        button(p, {x + 4, y + 4, x + 126, y + 38}, "Готово · Enter", FinishContour, "", true);
        iconButton(p, {x + 134, y + 4, x + 172, y + 38}, "x", "Отменить · Esc", CancelContour);
    } else if (editScope == SelectionDomain::Borders && !activeBorder.empty()) {
        float x = (width - 306) / 2, y = height - 118;
        dock(p, {x, y, x + 306, y + 42});
        iconButton(p, {x + 4, y + 3, x + 42, y + 39}, "plus", "Добавить точку · двойной клик по линии",
                   AddControl);
        iconButton(p, {x + 46, y + 3, x + 84, y + 39}, "trash", "Удалить выбранную точку · Delete",
                   RemoveControl);
        button(p, {x + 94, y + 5, x + 193, y + 37}, "Море", ToggleSea, "", allowSea);
        p.text(std::to_string(activeControls.size()) + " точек", {x + 203, y + 3, x + 296, y + 39}, 12, muted,
               false, true);
    } else if (tool == Tool::Region) {
        float x = (width - 120) / 2, y = height - 118;
        dock(p, {x, y, x + 120, y + 42});
        button(p, {x + 5, y + 5, x + 115, y + 37}, "Море", ToggleSea, "", allowSea);
    }
    dock(p, {16, height - 56, 147, height - 16});
    iconButton(p, {20, height - 52, 56, height - 20}, "history", "Версии по событиям", HistoryView, "",
               historyView);
    button(p, {58, height - 52, 143, height - 20}, "Версии", HistoryView, "", historyView, true);
    dock(p, {width - 213, height - 56, width - 16, height - 16});
    iconButton(p, {width - 210, height - 53, width - 176, height - 19}, "minus", "Отдалить", ZoomOut);
    p.text(std::to_string(int(zoom * 100)) + "%", {width - 174, height - 53, width - 117, height - 19}, 12,
           ink, false, true);
    iconButton(p, {width - 115, height - 53, width - 81, height - 19}, "plus", "Приблизить", ZoomIn);
    iconButton(p, {width - 58, height - 53, width - 22, height - 19}, "fit", "Вся карта · Home", Fit);
    if (!selected.empty() && !historyView && !down) {
        float x = std::max(174.f, (width - 250) / 2), y = height - 66;
        dock(p, {x, y, x + 246, y + 48});
        hits.push_back({{x, y, x + 246, y + 48}, 0, {}});
        iconButton(p, {x + 5, y + 4, x + 45, y + 44}, "node", "Контрольные точки границы · K", SetTool, "18",
                   tool == Tool::Border);
        iconButton(p, {x + 53, y + 4, x + 93, y + 44}, "move", "Выбор границы · V", SetTool, "0",
                   tool == Tool::Select);
        iconButton(p, {x + 101, y + 4, x + 141, y + 44}, "color", "Цвет",
                   map.doc["features"][*selected.begin()]["kind"].str() == "symbol" ? StrokeColor
                                                                                    : FillColor);
        iconButton(p, {x + 149, y + 4, x + 189, y + 44}, "gear", "Свойства", InspectorPanel, "", panel == 5);
        iconButton(p, {x + 197, y + 4, x + 237, y + 44}, "more", "Действия с объектом", SelectionMenu);
    }
}
void App::paintChrome(Painter &p) {
    paintTools(p);
    paintSidebar(p);
    paintProperties(p);
    paintTimeline(p);
    if (GetTickCount64() < noticeUntil) {
        float w = std::min(440.f, 32.f + float(wide(status).size()) * 7.f), x = (width - w) / 2;
        p.rounded({x, 58, x + w, 94}, "#243B3E", 5);
        p.text(status, {x + 14, 58, x + w - 14, 94}, 12, ink);
    }
    if (!down && GetTickCount64() - hoverSince >= 500)
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (over(it->rect, mouse)) {
                if (!it->tip.empty()) {
                    float w = std::min(340.f, 32.f + float(wide(it->tip).size()) * 7.f),
                          x = std::clamp(float(mouse.x) + 16, 8.f, width - w - 8),
                          y = std::clamp(float(mouse.y) + 23, 48.f, height - 96);
                    p.rounded({x, y, x + w, y + 32}, "#152328", 5);
                    p.text(it->tip, {x + 12, y, x + w - 8, y + 32}, 12, ink);
                }
                break;
            }
}
} // namespace atlas
