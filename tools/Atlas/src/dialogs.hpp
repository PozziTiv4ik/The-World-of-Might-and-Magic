#pragma once
#include "platform.hpp"
#include <optional>

namespace atlas {
struct Field {
    std::string label, value;
    std::vector<std::string> choices;
    bool multiline = false;
};
std::optional<std::vector<std::string>> form(HWND owner, const std::string &title,
                                             const std::vector<Field> &fields,
                                             const std::string &submit = "Применить");
std::optional<fs::path> openFile(HWND owner, const wchar_t *filter);
std::optional<fs::path> saveFile(HWND owner, const wchar_t *filter, const std::wstring &defaultName);
std::optional<fs::path> chooseFolder(HWND owner, const std::string &title);
std::optional<std::string> chooseColor(HWND owner, const std::string &current);
void showError(HWND owner, const std::exception &e);
} // namespace atlas
