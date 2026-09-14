#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000003
#endif
#include "json.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <wincodec.h>
#include <windows.h>

namespace atlas {
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
template <class T> class Com {
    T *p = nullptr;

  public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    Com(Com &&o) noexcept : p(o.p) { o.p = nullptr; }
    Com &operator=(Com &&o) noexcept {
        if (this != &o) {
            reset();
            p = o.p;
            o.p = nullptr;
        }
        return *this;
    }
    T *get() const { return p; }
    T *operator->() const { return p; }
    operator bool() const { return p != nullptr; }
    T **put() {
        reset();
        return &p;
    }
    void reset() {
        if (p)
            p->Release();
        p = nullptr;
    }
};
void check(HRESULT hr, const char *operation);
std::wstring wide(const std::string &s);
std::string utf8(const std::wstring &s);
std::string pathText(const fs::path &p);
fs::path pathOf(const std::string &s);
Bytes readBytes(const fs::path &p, size_t limit = 512 * 1024 * 1024);
std::string readText(const fs::path &p);
void atomicWrite(const fs::path &p, const Bytes &b);
void atomicText(const fs::path &p, const std::string &s);
std::string hashBytes(const Bytes &b);
std::string hashText(const std::string &s);
std::string newId(const std::string &prefix);
std::string nowUtc();
fs::path safeChild(const fs::path &root, const std::string &relative);
struct Image {
    int width = 0, height = 0;
    Bytes bgra; // straight alpha, never premultiplied in persisted data
    Image() = default;
    Image(int w, int h);
};
std::shared_ptr<Image> loadImage(const fs::path &p);
void savePng(const fs::path &p, const Image &image);
Bytes encodePng(const Image &image);
void composite(Image &dst, const Image &src, double opacity = 1, int blend = 0);
class FileLock {
    HANDLE h = INVALID_HANDLE_VALUE;

  public:
    explicit FileLock(const fs::path &directory);
    ~FileLock() {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    FileLock(const FileLock &) = delete;
};
} // namespace atlas
