#include "platform.hpp"
#include <algorithm>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace atlas {
void check(HRESULT hr, const char *op) {
    if (FAILED(hr)) {
        std::ostringstream s;
        s << op << " (HRESULT 0x" << std::hex << uint32_t(hr) << ")";
        throw std::runtime_error(s.str());
    }
}
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), r.data(), n);
    return r;
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0, nullptr,
                                nullptr);
    if (!n)
        throw std::runtime_error("Invalid UTF-16");
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
std::string pathText(const fs::path &p) {
    return utf8(p.generic_wstring());
}
fs::path pathOf(const std::string &s) {
    return fs::path(wide(s));
}
fs::path nativePath(const fs::path &path) {
    auto raw=path.wstring();
    std::replace(raw.begin(),raw.end(),L'/',L'\\');
    if(raw.starts_with(L"\\\\?\\UNC\\"))raw=L"\\\\"+raw.substr(8);
    else if(raw.starts_with(L"\\\\?\\"))raw=raw.substr(4);
    auto normalized=fs::absolute(fs::path(raw)).lexically_normal();
    normalized.make_preferred();
    auto full=normalized.wstring();
    if(full.starts_with(L"\\\\?\\"))return fs::path(full);
    if(full.starts_with(L"\\\\"))return fs::path(L"\\\\?\\UNC\\"+full.substr(2));
    return fs::path(L"\\\\?\\"+full);
}
Bytes readBytes(const fs::path &p, size_t limit) {
    std::ifstream f(nativePath(p), std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Cannot read: " + pathText(p));
    auto n = f.tellg();
    if (n < 0 || uint64_t(n) > limit)
        throw std::runtime_error("File exceeds limit: " + pathText(p));
    Bytes b(size_t(n), 0);
    f.seekg(0);
    if (!b.empty() && !f.read(reinterpret_cast<char *>(b.data()), std::streamsize(b.size())))
        throw std::runtime_error("Incomplete read: " + pathText(p));
    return b;
}
std::string readText(const fs::path &p) {
    auto b = readBytes(p, 64 * 1024 * 1024);
    return std::string(b.begin(), b.end());
}
std::string newId(const std::string &prefix) {
    GUID g;
    check(CoCreateGuid(&g), "CoCreateGuid");
    wchar_t buf[40];
    StringFromGUID2(g, buf, 40);
    std::wstring s(buf);
    s = s.substr(1, 36);
    return prefix + utf8(s);
}
void atomicWrite(const fs::path &p, const Bytes &b) {
    if (!p.parent_path().empty())
        fs::create_directories(p.parent_path());
    fs::path temp = p;
    temp += pathOf("." + newId("") + ".tmp");
    HANDLE h =
        CreateFileW(nativePath(temp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot write: " + pathText(p));
    size_t offset = 0;
    bool ok = true;
    while (offset < b.size()) {
        DWORD written = 0, n = DWORD(std::min<size_t>(b.size() - offset, 16 * 1024 * 1024));
        if (!WriteFile(h, b.data() + offset, n, &written, nullptr) || written != n) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (!FlushFileBuffers(h))
        ok = false;
    CloseHandle(h);
    if (ok)
        ok = MoveFileExW(nativePath(temp).c_str(), nativePath(p).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        DeleteFileW(nativePath(temp).c_str());
        throw std::runtime_error("Atomic save failed: " + pathText(p));
    }
}
void atomicText(const fs::path &p, const std::string &s) {
    atomicWrite(p, Bytes(s.begin(), s.end()));
}
std::string hashBytes(const Bytes &b) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    DWORD len = 0, n = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 unavailable");
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&len), 4, &n, 0);
    Bytes object(len), out(32);
    auto status = BCryptCreateHash(alg, &h, object.data(), len, nullptr, 0, 0);
    if (status >= 0)
        status = BCryptHashData(h, const_cast<PUCHAR>(b.data()), ULONG(b.size()), 0);
    if (status >= 0)
        status = BCryptFinishHash(h, out.data(), 32, 0);
    if (h)
        BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status < 0)
        throw std::runtime_error("SHA256 failed");
    std::string text;
    const char *digits = "0123456789abcdef";
    for (auto c : out) {
        text += digits[c >> 4];
        text += digits[c & 15];
    }
    return text;
}
std::string hashText(const std::string &s) {
    return hashBytes(Bytes(s.begin(), s.end()));
}
std::string nowUtc() {
    SYSTEMTIME t;
    GetSystemTime(&t);
    char b[32];
    std::snprintf(b, sizeof b, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond,t.wMilliseconds);
    return b;
}
fs::path safeChild(const fs::path &root, const std::string &rel) {
    auto p = pathOf(rel);
    if (p.is_absolute() || p.has_root_name())
        throw std::runtime_error("Expected relative asset path");
    for (auto x : p)
        if (x == L"..")
            throw std::runtime_error("Asset path escapes map directory");
    auto result = (root / p).lexically_normal();
    auto canonRoot = fs::weakly_canonical(root);
    auto canon = fs::weakly_canonical(result);
    auto r = canon.lexically_relative(canonRoot);
    if (r.empty() || *r.begin() == L"..")
        throw std::runtime_error("Asset outside map directory");
    return result;
}
FileLock::FileLock(const fs::path &directory) {
    fs::create_directories(directory);
    auto p = directory / L".atlas.lock";
    h = CreateFileW(nativePath(p).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN,
                    nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        auto code=GetLastError();
        if(code==ERROR_SHARING_VIOLATION)throw std::runtime_error("Map is being saved by another process. Retry after it finishes.");
        throw std::runtime_error("Cannot lock map ("+std::to_string(code)+"): "+utf8(nativePath(p).wstring()));
    }
}
Image::Image(int w, int h) : width(w), height(h) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || uint64_t(w) * h > 64000000)
        throw std::runtime_error("Image size exceeds 64 megapixels");
    bgra.resize(size_t(w) * h * 4);
}
static Com<IWICImagingFactory> factory() {
    Com<IWICImagingFactory> f;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(f.put())),
          "WIC factory");
    return f;
}
std::shared_ptr<Image> loadImage(const fs::path &p) {
    auto f = factory();
    Com<IWICBitmapDecoder> d;
    check(
        f->CreateDecoderFromFilename(nativePath(p).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, d.put()),
        "Decode image");
    Com<IWICBitmapFrameDecode> frame;
    check(d->GetFrame(0, frame.put()), "Image frame");
    UINT w, h;
    check(frame->GetSize(&w, &h), "Image size");
    auto im = std::make_shared<Image>(int(w), int(h));
    Com<IWICFormatConverter> conv;
    check(f->CreateFormatConverter(conv.put()), "WIC converter");
    check(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                           WICBitmapPaletteTypeCustom),
          "Convert BGRA");
    check(conv->CopyPixels(nullptr, w * 4, UINT(im->bgra.size()), im->bgra.data()), "Read pixels");
    return im;
}
void savePng(const fs::path &p, const Image &im) {
    auto f = factory();
    Com<IWICStream> stream;
    check(f->CreateStream(stream.put()), "PNG stream");
    fs::create_directories(p.parent_path());
    auto tmp = p;
    tmp += pathOf("." + newId("") + ".tmp");
    try {
        check(stream->InitializeFromFilename(nativePath(tmp).c_str(), GENERIC_WRITE), "PNG output");
        Com<IWICBitmapEncoder> e;
        check(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, e.put()), "PNG encoder");
        check(e->Initialize(stream.get(), WICBitmapEncoderNoCache), "PNG initialize");
        Com<IWICBitmapFrameEncode> frame;
        Com<IPropertyBag2> opts;
        check(e->CreateNewFrame(frame.put(), opts.put()), "PNG frame");
        check(frame->Initialize(opts.get()), "PNG frame initialize");
        check(frame->SetSize(im.width, im.height), "PNG size");
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        check(frame->SetPixelFormat(&format), "PNG pixel format");
        if (format != GUID_WICPixelFormat32bppBGRA)
            throw std::runtime_error("BGRA PNG unsupported");
        check(frame->WritePixels(im.height, im.width * 4, UINT(im.bgra.size()),
                                 const_cast<BYTE *>(im.bgra.data())),
              "PNG pixels");
        check(frame->Commit(), "PNG frame commit");
        check(e->Commit(), "PNG commit");
        frame.reset();
        e.reset();
        stream.reset();
        if (!MoveFileExW(nativePath(tmp).c_str(), nativePath(p).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("PNG replace failed");
    } catch (...) {
        stream.reset();
        DeleteFileW(nativePath(tmp).c_str());
        throw;
    }
}
Bytes encodePng(const Image &im) {
    auto f = factory();
    Com<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()), "PNG memory stream");
    Com<IWICBitmapEncoder> encoder;
    check(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()), "PNG memory encoder");
    check(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache), "PNG memory initialize");
    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> options;
    check(encoder->CreateNewFrame(frame.put(), options.put()), "PNG memory frame");
    check(frame->Initialize(options.get()), "PNG memory frame initialize");
    check(frame->SetSize(im.width, im.height), "PNG memory size");
    auto format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format), "PNG memory format");
    if (format != GUID_WICPixelFormat32bppBGRA)
        throw std::runtime_error("Unsupported PNG pixel format");
    check(
        frame->WritePixels(im.height, im.width * 4, UINT(im.bgra.size()), const_cast<BYTE *>(im.bgra.data())),
        "PNG memory pixels");
    check(frame->Commit(), "PNG memory frame commit");
    check(encoder->Commit(), "PNG memory commit");
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME), "PNG memory length");
    if (stat.cbSize.QuadPart > 512 * 1024 * 1024)
        throw std::runtime_error("PNG output exceeds limit");
    Bytes data(size_t(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    check(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "PNG memory rewind");
    ULONG read = 0;
    check(stream->Read(data.data(), ULONG(data.size()), &read), "PNG memory read");
    if (read != data.size())
        throw std::runtime_error("Incomplete PNG encoding");
    return data;
}
void composite(Image &dst, const Image &src, double opacity, int blend) {
    if (dst.width != src.width || dst.height != src.height)
        throw std::runtime_error("Layer dimensions differ");
    for (size_t i = 0; i < dst.bgra.size(); i += 4) {
        double a = src.bgra[i + 3] / 255.0 * opacity;
        if (a <= 0)
            continue;
        double da = dst.bgra[i + 3] / 255.0, oa = a + da * (1 - a);
        for (int c = 0; c < 3; c++) {
            double s = src.bgra[i + c], d = dst.bgra[i + c], m = s;
            switch (blend) {
            case 1:
                m = s * d / 255;
                break;
            case 2:
                m = std::min(255.0, s + d);
                break;
            case 3:
                m = s <= 0 ? 0 : std::max(0.0, 255 - (255 - d) * 255 / s);
                break;
            case 4:
                m = s >= 255 ? 255 : std::min(255.0, d * 255 / (255 - s));
                break;
            case 5:
                m = s >= 255 ? 255 : std::min(255.0, d * d / (255 - s));
                break;
            case 6:
                m = d >= 255 ? 255 : std::min(255.0, s * s / (255 - d));
                break;
            case 7:
                m = d < 128 ? 2 * s * d / 255 : 255 - 2 * (255 - s) * (255 - d) / 255;
                break;
            case 8:
                m = std::abs(d - s);
                break;
            case 9:
                m = 255 - std::abs(255 - d - s);
                break;
            case 10:
                m = std::max(s, d);
                break;
            case 11:
                m = std::min(s, d);
                break;
            case 12:
                m = 255 - (255 - s) * (255 - d) / 255;
                break;
            case 13:
                m = int(d) ^ int(s);
                break;
            }
            dst.bgra[i + c] = uint8_t(
                std::clamp(std::lround((a * ((1 - da) * s + da * m) + da * (1 - a) * d) / oa), 0l, 255l));
        }
        dst.bgra[i + 3] = uint8_t(std::clamp(std::lround(oa * 255), 0l, 255l));
    }
}
} // namespace atlas
