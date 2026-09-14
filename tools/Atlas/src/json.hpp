#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace atlas {
// Small strict JSON value/parser. Standard C++ only; deterministic object ordering.
struct Json {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v = nullptr;
    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool x) : v(x) {}
    Json(int x) : v(double(x)) {}
    Json(unsigned x) : v(double(x)) {}
    Json(size_t x) : v(double(x)) {}
    Json(double x) : v(x) {}
    Json(const char *x) : v(std::string(x)) {}
    Json(std::string x) : v(std::move(x)) {}
    Json(Array x) : v(std::move(x)) {}
    Json(Object x) : v(std::move(x)) {}
    static Json object() { return Object{}; }
    static Json array() { return Array{}; }
    bool null() const { return std::holds_alternative<std::nullptr_t>(v); }
    bool isObject() const { return std::holds_alternative<Object>(v); }
    bool isArray() const { return std::holds_alternative<Array>(v); }
    bool isString() const { return std::holds_alternative<std::string>(v); }
    bool isNumber() const { return std::holds_alternative<double>(v); }
    bool isBool() const { return std::holds_alternative<bool>(v); }
    std::string str(std::string def = "") const {
        auto p = std::get_if<std::string>(&v);
        return p ? *p : def;
    }
    double num(double def = 0) const {
        auto p = std::get_if<double>(&v);
        return p ? *p : def;
    }
    bool boolean(bool def = false) const {
        auto p = std::get_if<bool>(&v);
        return p ? *p : def;
    }
    Object &obj() { return std::get<Object>(v); }
    const Object &obj() const { return std::get<Object>(v); }
    Array &arr() { return std::get<Array>(v); }
    const Array &arr() const { return std::get<Array>(v); }
    Json &operator[](const std::string &key) {
        if (null())
            v = Object{};
        return obj()[key];
    }
    const Json &operator[](const std::string &key) const {
        static Json empty;
        auto p = std::get_if<Object>(&v);
        if (!p)
            return empty;
        auto it = p->find(key);
        return it == p->end() ? empty : it->second;
    }
    Json &operator[](size_t i) { return arr().at(i); }
    const Json &operator[](size_t i) const { return arr().at(i); }
    bool contains(const std::string &s) const { return isObject() && obj().contains(s); }
    size_t size() const {
        if (isArray())
            return arr().size();
        if (isObject())
            return obj().size();
        return 0;
    }
    void push(Json x) {
        if (null())
            v = Array{};
        arr().push_back(std::move(x));
    }
    bool operator==(const Json &r) const { return v == r.v; }
    static void utf8(std::string &s, uint32_t c) {
        if (c < 0x80)
            s += char(c);
        else if (c < 0x800) {
            s += char(0xc0 | (c >> 6));
            s += char(0x80 | (c & 63));
        } else if (c < 0x10000) {
            s += char(0xe0 | (c >> 12));
            s += char(0x80 | ((c >> 6) & 63));
            s += char(0x80 | (c & 63));
        } else {
            s += char(0xf0 | (c >> 18));
            s += char(0x80 | ((c >> 12) & 63));
            s += char(0x80 | ((c >> 6) & 63));
            s += char(0x80 | (c & 63));
        }
    }
    static Json parse(const std::string &input) {
        struct Parser {
            const std::string &s;
            size_t p = 0;
            int depth = 0;
            [[noreturn]] void fail() {
                throw std::runtime_error("Invalid JSON at byte " + std::to_string(p));
            }
            void ws() {
                while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t'))
                    ++p;
            }
            char get() {
                if (p >= s.size())
                    fail();
                return s[p++];
            }
            unsigned hex() {
                unsigned n = 0;
                for (int i = 0; i < 4; ++i) {
                    char c = get();
                    n <<= 4;
                    if (c >= '0' && c <= '9')
                        n += c - '0';
                    else if (c >= 'a' && c <= 'f')
                        n += c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F')
                        n += c - 'A' + 10;
                    else
                        fail();
                }
                return n;
            }
            std::string string() {
                if (get() != '"')
                    fail();
                std::string out;
                for (;;) {
                    unsigned char c = get();
                    if (c == '"')
                        break;
                    if (c < 32)
                        fail();
                    if (c != '\\') {
                        out += char(c);
                        continue;
                    }
                    char e = get();
                    switch (e) {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u': {
                        unsigned u = hex();
                        if (u >= 0xd800 && u <= 0xdbff) {
                            if (get() != '\\' || get() != 'u')
                                fail();
                            unsigned l = hex();
                            if (l < 0xdc00 || l > 0xdfff)
                                fail();
                            u = 0x10000 + ((u - 0xd800) << 10) + l - 0xdc00;
                        } else if (u >= 0xdc00 && u <= 0xdfff)
                            fail();
                        Json::utf8(out, u);
                        break;
                    }
                    default:
                        fail();
                    }
                }
                return out;
            }
            Json value() {
                ws();
                if (++depth > 128)
                    fail();
                if (p >= s.size())
                    fail();
                Json out;
                char c = s[p];
                if (c == '{') {
                    ++p;
                    out = Json::object();
                    ws();
                    if (p < s.size() && s[p] == '}')
                        ++p;
                    else
                        for (;;) {
                            ws();
                            auto k = string();
                            ws();
                            if (get() != ':' || out.contains(k))
                                fail();
                            out[k] = value();
                            ws();
                            char end = get();
                            if (end == '}')
                                break;
                            if (end != ',')
                                fail();
                        }
                } else if (c == '[') {
                    ++p;
                    out = Json::array();
                    ws();
                    if (p < s.size() && s[p] == ']')
                        ++p;
                    else
                        for (;;) {
                            out.push(value());
                            ws();
                            char end = get();
                            if (end == ']')
                                break;
                            if (end != ',')
                                fail();
                        }
                } else if (c == '"')
                    out = string();
                else if (s.compare(p, 4, "true") == 0) {
                    p += 4;
                    out = true;
                } else if (s.compare(p, 5, "false") == 0) {
                    p += 5;
                    out = false;
                } else if (s.compare(p, 4, "null") == 0) {
                    p += 4;
                    out = nullptr;
                } else {
                    size_t begin = p;
                    if (s[p] == '-')
                        ++p;
                    if (p >= s.size())
                        fail();
                    if (s[p] == '0')
                        ++p;
                    else {
                        if (s[p] < '1' || s[p] > '9')
                            fail();
                        while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                            ++p;
                    }
                    if (p < s.size() && s[p] == '.') {
                        ++p;
                        size_t d = p;
                        while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                            ++p;
                        if (d == p)
                            fail();
                    }
                    if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
                        ++p;
                        if (p < s.size() && (s[p] == '+' || s[p] == '-'))
                            ++p;
                        size_t d = p;
                        while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                            ++p;
                        if (d == p)
                            fail();
                    }
                    double n = 0;
                    auto r = std::from_chars(s.data() + begin, s.data() + p, n);
                    if (r.ec != std::errc() || r.ptr != s.data() + p || !std::isfinite(n))
                        fail();
                    out = n;
                }
                --depth;
                return out;
            }
        } parser{input};
        if (input.size() >= 3 && input.substr(0, 3) == "\xef\xbb\xbf")
            parser.p = 3;
        Json j = parser.value();
        parser.ws();
        if (parser.p != input.size())
            parser.fail();
        return j;
    }
    static std::string quote(const std::string &s) {
        std::string o = "\"";
        for (unsigned char c : s) {
            switch (c) {
            case '"':
                o += "\\\"";
                break;
            case '\\':
                o += "\\\\";
                break;
            case '\n':
                o += "\\n";
                break;
            case '\r':
                o += "\\r";
                break;
            case '\t':
                o += "\\t";
                break;
            default:
                if (c < 32) {
                    char b[7];
                    std::snprintf(b, 7, "\\u%04x", c);
                    o += b;
                } else
                    o += char(c);
            }
        }
        return o + '"';
    }
    std::string dump(int indent = 2, int level = 0) const {
        if (null())
            return "null";
        if (isBool())
            return boolean() ? "true" : "false";
        if (isNumber()) {
            double n = num();
            if (!std::isfinite(n))
                throw std::runtime_error("Nonfinite JSON number");
            char buf[64];
            auto r = std::to_chars(buf, buf + 64, n);
            return std::string(buf, r.ptr);
        }
        if (isString())
            return quote(str());
        bool object = isObject();
        std::string out = object ? "{" : "[";
        size_t index = 0;
        auto append = [&](const std::string &key, const Json &x) {
            if (index++)
                out += ",";
            if (indent > 0)
                out += '\n' + std::string((level + 1) * indent, ' ');
            if (object)
                out += quote(key) + (indent > 0 ? ": " : ":");
            out += x.dump(indent, level + 1);
        };
        if (object)
            for (auto &[k, x] : obj())
                append(k, x);
        else
            for (auto &x : arr())
                append("", x);
        if (index && indent > 0)
            out += '\n' + std::string(level * indent, ' ');
        return out + (object ? "}" : "]");
    }
};
inline Json fields(std::initializer_list<std::pair<const std::string, Json>> x) {
    return Json::Object(x);
}
} // namespace atlas
