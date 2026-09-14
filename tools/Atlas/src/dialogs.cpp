#include "dialogs.hpp"
#include "render.hpp"
#include <commdlg.h>
#include <cstdlib>
#include <dwmapi.h>
#include <shobjidl.h>

namespace atlas {
namespace {
struct FormState {
    std::vector<Field> fields;
    std::vector<HWND> controls;
    std::vector<std::string> result;
    bool done = false, ok = false;
    HFONT font = nullptr;
};
std::string controlText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(size_t(n) + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return utf8(s);
}
LRESULT CALLBACK formProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto s = reinterpret_cast<FormState *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        s = static_cast<FormState *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s)
        return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            s->result.clear();
            for (auto c : s->controls)
                s->result.push_back(controlText(c));
            s->ok = true;
            s->done = true;
            DestroyWindow(h);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            s->done = true;
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_CLOSE:
        s->done = true;
        DestroyWindow(h);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wp), RGB(235, 241, 245));
        SetBkColor(reinterpret_cast<HDC>(wp), RGB(30, 43, 53));
        SetDCBrushColor(reinterpret_cast<HDC>(wp), RGB(30, 43, 53));
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wp), RGB(217, 228, 237));
        SetBkColor(reinterpret_cast<HDC>(wp), RGB(30, 43, 53));
        SetDCBrushColor(reinterpret_cast<HDC>(wp), RGB(30, 43, 53));
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        auto dc = reinterpret_cast<HDC>(wp);
        SetDCBrushColor(dc, RGB(30, 43, 53));
        FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        return 1;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}
} // namespace
std::optional<std::vector<std::string>> form(HWND owner, const std::string &title,
                                             const std::vector<Field> &fields, const std::string &submit) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = formProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"AtlasForm";
        RegisterClassW(&wc);
        registered = true;
    }
    FormState s;
    s.fields = fields;
    float scale = owner ? float(GetDpiForWindow(owner)) / 96 : 1;
    int width = int(620 * scale), height = int((100 + fields.size() * 72) * scale);
    for (auto &f : fields)
        if (f.multiline)
            height += int(65 * scale);
    height = std::min(height, GetSystemMetrics(SM_CYSCREEN) - 90);
    RECT parent{};
    if (owner)
        GetWindowRect(owner, &parent);
    else {
        parent.right = GetSystemMetrics(SM_CXSCREEN);
        parent.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int x = int(parent.left) + (parent.right - parent.left - width) / 2,
        y = std::max(20, int(parent.top + (parent.bottom - parent.top - height) / 2));
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"AtlasForm", wide(title).c_str(),
                             WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, width, height, owner, nullptr,
                             GetModuleHandleW(nullptr), &s);
    if (!h)
        throw std::runtime_error("Cannot create dialog");
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
    s.font = CreateFontW(int(-15 * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
    int cy = int(16 * scale);
    auto child = [&](const wchar_t *klass, const std::wstring &text, DWORD style, int cx, int yy, int ww,
                     int hh, int id) {
        HWND c = CreateWindowExW(0, klass, text.c_str(), WS_CHILD | WS_VISIBLE | style, cx, yy, ww, hh, h,
                                 reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(s.font), TRUE);
        return c;
    };
    for (size_t i = 0; i < fields.size(); i++) {
        auto &f = fields[i];
        child(L"STATIC", wide(f.label), 0, int(20 * scale), cy, int(570 * scale), int(22 * scale), 0);
        cy += int(25 * scale);
        HWND c;
        if (f.choices.empty())
            c = child(L"EDIT", wide(f.value),
                      WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL |
                          (f.multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL) : 0),
                      int(20 * scale), cy, int(565 * scale), int((f.multiline ? 90 : 29) * scale),
                      100 + int(i));
        else {
            c = child(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, int(20 * scale), cy,
                      int(565 * scale), int(260 * scale), 100 + int(i));
            int selected = 0;
            for (size_t j = 0; j < f.choices.size(); j++) {
                SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(f.choices[j]).c_str()));
                if (f.choices[j] == f.value)
                    selected = int(j);
            }
            SendMessageW(c, CB_SETCURSEL, selected, 0);
        }
        s.controls.push_back(c);
        cy += int((f.multiline ? 108 : 47) * scale);
    }
    child(L"BUTTON", wide(submit), WS_TABSTOP | BS_DEFPUSHBUTTON, int(405 * scale), cy, int(180 * scale),
          int(32 * scale), IDOK);
    child(L"BUTTON", L"Отмена", WS_TABSTOP, int(285 * scale), cy, int(108 * scale), int(32 * scale),
          IDCANCEL);
    if (owner)
        EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    if (!s.controls.empty())
        SetFocus(s.controls.front());
    MSG msg;
    while (!s.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    DeleteObject(s.font);
    if (s.ok)
        return s.result;
    return std::nullopt;
}
std::optional<fs::path> openFile(HWND owner, const wchar_t *filter) {
    std::wstring buf(32768, 0);
    OPENFILENAMEW o{sizeof o};
    o.hwndOwner = owner;
    o.lpstrFilter = filter;
    o.lpstrFile = buf.data();
    o.nMaxFile = DWORD(buf.size());
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&o))
        return {};
    return fs::path(buf.c_str());
}
std::optional<fs::path> saveFile(HWND owner, const wchar_t *filter, const std::wstring &name) {
    std::wstring buf(32768, 0);
    std::copy(name.begin(), name.end(), buf.begin());
    OPENFILENAMEW o{sizeof o};
    o.hwndOwner = owner;
    o.lpstrFilter = filter;
    o.lpstrFile = buf.data();
    o.nMaxFile = DWORD(buf.size());
    auto extension = fs::path(name).extension().wstring();
    if (!extension.empty() && extension[0] == L'.')
        extension.erase(extension.begin());
    o.lpstrDefExt = extension.empty() ? L"png" : extension.c_str();
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&o))
        return {};
    return fs::path(buf.c_str());
}
std::optional<fs::path> chooseFolder(HWND owner, const std::string &title) {
    Com<IFileOpenDialog> dialog;
    check(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put())),
          "Folder dialog");
    DWORD options;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(wide(title).c_str());
    auto hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    check(hr, "Choose folder");
    Com<IShellItem> item;
    check(dialog->GetResult(item.put()), "Folder selection");
    PWSTR name = nullptr;
    check(item->GetDisplayName(SIGDN_FILESYSPATH, &name), "Folder path");
    fs::path p(name);
    CoTaskMemFree(name);
    return p;
}
std::optional<std::string> chooseColor(HWND owner, const std::string &current) {
    static COLORREF customs[16] = {};
    auto n = std::strtoul(current.c_str() + (current.starts_with('#') ? 1 : 0), nullptr, 16);
    CHOOSECOLORW c{sizeof c};
    c.hwndOwner = owner;
    c.rgbResult = RGB((n >> 16) & 255, (n >> 8) & 255, n & 255);
    c.lpCustColors = customs;
    c.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&c))
        return {};
    return hexColor(c.rgbResult);
}
void showError(HWND owner, const std::exception &e) {
    MessageBoxW(owner, wide(e.what()).c_str(), L"АТЛАС — действие не завершено", MB_ICONERROR | MB_OK);
}
} // namespace atlas
