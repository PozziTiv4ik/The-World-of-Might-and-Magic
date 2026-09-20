#pragma once
#include "platform.hpp"
#include <optional>

namespace atlas {
struct Field {
    std::string label, value;
    std::vector<std::string> choices;
    bool multiline = false;
};
// Scoped interaction port for windowless replay. The desktop keeps using native
// dialogs; a test must explicitly answer every prompt instead of opening one.
using DialogHandler = std::function<Json(const Json &)>;
class ScopedDialogHandler {
    DialogHandler previous;
  public:
    explicit ScopedDialogHandler(DialogHandler);
    ~ScopedDialogHandler();
    ScopedDialogHandler(const ScopedDialogHandler &) = delete;
};
bool hasDialogHandler();
int confirmDialog(HWND, const std::string &, const std::string &, UINT);
inline int confirmDialog(HWND owner,const wchar_t *message,const wchar_t *title,UINT flags) {
    return confirmDialog(owner,utf8(message),utf8(title),flags);
}
int pickMenu(HWND, HMENU);
void writeClipboard(HWND, const std::string &);
std::optional<std::string> readClipboard(HWND);
std::optional<std::vector<std::string>> form(HWND owner, const std::string &title,
                                             const std::vector<Field> &fields,
                                             const std::string &submit = "Применить");
std::optional<fs::path> openFile(HWND owner, const wchar_t *filter);
std::optional<fs::path> saveFile(HWND owner, const wchar_t *filter, const std::wstring &defaultName);
std::optional<fs::path> chooseFolder(HWND owner, const std::string &title);
std::optional<std::string> chooseColor(HWND owner, const std::string &current);
void showError(HWND owner, const std::exception &e);
} // namespace atlas
