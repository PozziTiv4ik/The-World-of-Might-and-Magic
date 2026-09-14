#include "app.hpp"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        wchar_t module[32768];
        GetModuleFileNameW(nullptr, module, 32768);
        auto executable = atlas::fs::path(module).parent_path();
        auto root = executable.parent_path().parent_path().parent_path();
        auto map = root / atlas::pathOf("12_Карты/Карта_мира");
        auto objectMap = root / atlas::pathOf("12_Карты/Карта_мира_Объекты");
        if (atlas::fs::exists(objectMap / L"map.json"))
            map = objectMap;
        if (atlas::fs::exists(executable / atlas::pathOf("Карта")))
            map = executable / atlas::pathOf("Карта");
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i + 1 < argc; i++) {
            if (std::wstring(argv[i]) == L"--map")
                map = argv[++i];
            else if (std::wstring(argv[i]) == L"--project")
                root = argv[++i];
        }
        LocalFree(argv);
        int result;
        {
            atlas::App app(root, map);
            result = app.run(instance, show);
        }
        if (SUCCEEDED(hr))
            CoUninitialize();
        return result;
    } catch (const std::exception &e) {
        atlas::showError(nullptr, e);
        if (SUCCEEDED(hr))
            CoUninitialize();
        return 1;
    }
}
