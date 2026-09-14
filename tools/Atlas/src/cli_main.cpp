#include "app.hpp"
#include <iostream>

int wmain(int argc, wchar_t **argv) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        int result = atlas::runCli(argc, argv);
        if (SUCCEEDED(hr))
            CoUninitialize();
        return result;
    } catch (const std::exception &e) {
        std::cerr << "Atlas: " << e.what() << "\n";
        if (SUCCEEDED(hr))
            CoUninitialize();
        return 1;
    }
}
