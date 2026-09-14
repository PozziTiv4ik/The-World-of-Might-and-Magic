#include "pdn.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <regex>

namespace atlas {
namespace {
struct Reader {
    const Bytes &b;
    size_t p = 0;
    uint8_t u8() {
        if (p >= b.size())
            throw std::runtime_error("Truncated PDN/gzip stream");
        return b[p++];
    }
    uint32_t le32() {
        uint32_t n = 0;
        for (int i = 0; i < 4; i++)
            n |= uint32_t(u8()) << (8 * i);
        return n;
    }
    uint32_t be32() {
        uint32_t n = 0;
        for (int i = 0; i < 4; i++)
            n = (n << 8) | u8();
        return n;
    }
    uint64_t le64() {
        uint64_t a = le32();
        return a | (uint64_t(le32()) << 32);
    }
    void skip(size_t n) {
        if (n > b.size() - p)
            throw std::runtime_error("PDN field exceeds file");
        p += n;
    }
    Bytes take(size_t n) {
        if (n > b.size() - p)
            throw std::runtime_error("PDN field exceeds file");
        Bytes v(b.begin() + p, b.begin() + p + n);
        p += n;
        return v;
    }
    std::string string() {
        uint32_t n = 0;
        int shift = 0;
        for (;;) {
            auto v = u8();
            if (shift >= 28 && (v & 0xf8))
                throw std::runtime_error("String length overflow");
            n |= uint32_t(v & 127) << shift;
            if (!(v & 128))
                break;
            shift += 7;
            if (shift > 28)
                throw std::runtime_error("String length overflow");
        }
        if (n > 4 * 1024 * 1024)
            throw std::runtime_error("PDN string too long");
        auto v = take(n);
        return std::string(v.begin(), v.end());
    }
};
uint32_t crc32(const Bytes &data) {
    static const auto table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++)
                c = (c >> 1) ^ ((c & 1) ? 0xedb88320 : 0);
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = ~0u;
    for (auto b : data)
        c = (c >> 8) ^ table[(c ^ b) & 255];
    return ~c;
}
struct Bits {
    Reader &r;
    uint32_t buffer = 0;
    int count = 0;
    unsigned read(int n) {
        while (count < n) {
            buffer |= uint32_t(r.u8()) << count;
            count += 8;
        }
        auto result = buffer & ((1u << n) - 1);
        buffer >>= n;
        count -= n;
        return result;
    }
    void align() {
        buffer = 0;
        count = 0;
    }
};
struct Huffman {
    struct Node {
        int child[2] = {-1, -1};
        int symbol = -1;
    };
    std::vector<Node> nodes{Node{}};
    explicit Huffman(const std::vector<unsigned> &lengths) {
        std::array<unsigned, 16> counts{}, next{};
        for (auto n : lengths) {
            if (n > 15)
                throw std::runtime_error("Invalid Huffman length");
            if (n)
                ++counts[n];
        }
        unsigned code = 0;
        for (int i = 1; i <= 15; i++) {
            code = (code + counts[i - 1]) << 1;
            next[i] = code;
            if (code + counts[i] > (1u << i))
                throw std::runtime_error("Oversubscribed Huffman tree");
        }
        for (size_t symbol = 0; symbol < lengths.size(); symbol++) {
            unsigned len = lengths[symbol];
            if (!len)
                continue;
            unsigned c = next[len]++;
            int index = 0;
            for (int j = int(len) - 1; j >= 0; j--) {
                int bit = (c >> j) & 1;
                if (nodes[index].symbol >= 0)
                    throw std::runtime_error("Huffman prefix collision");
                int child = nodes[index].child[bit];
                if (child < 0) {
                    child = int(nodes.size());
                    nodes[index].child[bit] = child;
                    nodes.push_back(Node{});
                }
                index = child;
            }
            nodes[index].symbol = int(symbol);
        }
    }
    int decode(Bits &b) const {
        int n = 0;
        for (int i = 0; i <= 15; i++) {
            if (n < 0)
                throw std::runtime_error("Invalid Huffman code");
            if (nodes[n].symbol >= 0)
                return nodes[n].symbol;
            n = nodes[n].child[b.read(1)];
        }
        throw std::runtime_error("Huffman code too long");
    }
};
struct Type {
    int tag = 0;
    Json extra;
};
struct Meta {
    std::string name;
    std::vector<std::string> keys;
    std::vector<Type> types;
};
class Nrbf {
    Reader &r;
    int depth = 0;
    size_t recordCount = 0;
    Json primitive(int t) {
        switch (t) {
        case 1:
            return r.u8() != 0;
        case 2:
            return int(r.u8());
        case 7: {
            int16_t v = r.u8();
            v |= int16_t(r.u8()) << 8;
            return int(v);
        }
        case 8:
            return int(int32_t(r.le32()));
        case 9:
        case 12:
            return double(int64_t(r.le64()));
        case 13:
        case 16:
            return double(r.le64());
        case 10:
            return int(int8_t(r.u8()));
        case 11: {
            auto v = r.le32();
            float f;
            std::memcpy(&f, &v, 4);
            return double(f);
        }
        case 6: {
            auto v = r.le64();
            double f;
            std::memcpy(&f, &v, 8);
            return f;
        }
        case 14: {
            unsigned a = r.u8();
            return a | (unsigned(r.u8()) << 8);
        }
        case 15:
            return double(r.le32());
        case 5:
        case 18:
            return r.string();
        case 17:
            return nullptr;
        default:
            throw std::runtime_error("Unsupported PDN primitive: " + std::to_string(t));
        }
    }
    Json extra(int t) {
        if (t == 0 || t == 7)
            return int(r.u8());
        if (t == 3)
            return r.string();
        if (t == 4) {
            auto name = r.string();
            r.le32();
            return name;
        }
        if (t >= 1 && t <= 6)
            return nullptr;
        throw std::runtime_error("Invalid NRBF binary type");
    }
    Json val(const Type &t) { return t.tag == 0 ? primitive(int(t.extra.num())) : record(); }
    Json array(size_t count, const Type &t) {
        if (count > 1000000)
            throw std::runtime_error("PDN metadata array too large");
        Json a = Json::array();
        while (a.size() < count) {
            auto v = val(t);
            if (v.contains("nulls")) {
                size_t n = size_t(v["nulls"].num());
                if (n > count - a.size())
                    throw std::runtime_error("Invalid NRBF null run");
                while (n--)
                    a.push(nullptr);
            } else
                a.push(std::move(v));
        }
        return a;
    }
    Json instance(int id, const Meta &m) {
        if (objects.contains(id))
            throw std::runtime_error("Duplicate NRBF object");
        objects[id] = fields({{"class", m.name}, {"fields", Json::object()}});
        for (size_t i = 0; i < m.keys.size(); i++)
            objects[id]["fields"][m.keys[i]] = val(m.types[i]);
        if (m.name == "PaintDotNet.MemoryBlock")
            memoryIds.push_back(id);
        return fields({{"ref", id}});
    }

  public:
    std::map<int, Json> objects;
    std::map<int, Meta> types;
    std::vector<int> memoryIds;
    int root = 1;
    explicit Nrbf(Reader &reader) : r(reader) {}
    Json record() {
        if (++recordCount > 1000000 || ++depth > 128)
            throw std::runtime_error("PDN metadata nesting limit");
        int tag = r.u8();
        Json out;
        switch (tag) {
        case 0: {
            root = int(r.le32());
            r.le32();
            if (r.le32() != 1 || r.le32() != 0)
                throw std::runtime_error("Unsupported NRBF version");
            break;
        }
        case 12:
            r.le32();
            r.string();
            out = record();
            break;
        case 4:
        case 5: {
            int id = int(r.le32());
            Meta m;
            m.name = r.string();
            unsigned n = r.le32();
            if (n > 256)
                throw std::runtime_error("PDN class too large");
            for (unsigned i = 0; i < n; i++)
                m.keys.push_back(r.string());
            for (unsigned i = 0; i < n; i++)
                m.types.push_back({r.u8(), {}});
            for (auto &t : m.types)
                t.extra = extra(t.tag);
            if (tag == 5)
                r.le32();
            types[id] = m;
            out = instance(id, m);
            break;
        }
        case 1: {
            int id = int(r.le32()), meta = int(r.le32());
            out = instance(id, types.at(meta));
            break;
        }
        case 6: {
            int id = int(r.le32());
            objects[id] = r.string();
            out = fields({{"ref", id}});
            break;
        }
        case 7: {
            int id = int(r.le32()), kind = r.u8();
            unsigned rank = r.le32();
            if (rank == 0 || rank > 8)
                throw std::runtime_error("Invalid PDN array rank");
            size_t n = 1;
            for (unsigned i = 0; i < rank; i++) {
                auto size = r.le32();
                if (size > 1000000 || n > 1000000 / std::max(1u, size))
                    throw std::runtime_error("PDN array too large");
                n *= size;
            }
            if (kind >= 3 && kind <= 5)
                for (unsigned i = 0; i < rank; i++)
                    r.le32();
            Type t{r.u8(), {}};
            t.extra = extra(t.tag);
            objects[id] = array(n, t);
            out = fields({{"ref", id}});
            break;
        }
        case 8:
            out = primitive(r.u8());
            break;
        case 9:
            out = fields({{"ref", int(r.le32())}});
            break;
        case 10:
            break;
        case 11:
            out = fields({{"end", true}});
            break;
        case 13:
            out = fields({{"nulls", int(r.u8())}});
            break;
        case 14:
            out = fields({{"nulls", double(r.le32())}});
            break;
        case 15:
        case 16:
        case 17: {
            int id = int(r.le32());
            size_t n = r.le32();
            Type t{2, {}};
            if (tag == 15) {
                t.tag = 0;
                t.extra = int(r.u8());
            }
            objects[id] = array(n, t);
            out = fields({{"ref", id}});
            break;
        }
        default:
            throw std::runtime_error("Unsupported PDN record " + std::to_string(tag));
        }
        --depth;
        return out;
    }
    const Json &resolve(const Json &j) const {
        return j.contains("ref") ? objects.at(int(j["ref"].num())) : j;
    }
};
} // namespace
Bytes gunzip(const Bytes &compressed, size_t expected) {
    if (expected > 256 * 1024 * 1024)
        throw std::runtime_error("gzip output limit");
    Reader r{compressed};
    if (r.u8() != 31 || r.u8() != 139 || r.u8() != 8)
        throw std::runtime_error("Not gzip DEFLATE");
    unsigned flags = r.u8();
    if (flags & 0xe0)
        throw std::runtime_error("Reserved gzip flags");
    r.skip(6);
    if (flags & 4) {
        unsigned n = r.u8();
        n |= unsigned(r.u8()) << 8;
        r.skip(n);
    }
    if (flags & 8)
        while (r.u8())
            ;
    if (flags & 16)
        while (r.u8())
            ;
    if (flags & 2)
        r.skip(2);
    Bits bits{r};
    Bytes out;
    out.reserve(expected);
    bool final = false;
    const int lengthBase[] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                              31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    const int lengthExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    const int distanceBase[] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    const int distanceExtra[] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    while (!final) {
        final = bits.read(1) != 0;
        int kind = bits.read(2);
        if (kind == 0) {
            bits.align();
            unsigned n = r.u8();
            n |= unsigned(r.u8()) << 8;
            unsigned complement = r.u8();
            complement |= unsigned(r.u8()) << 8;
            if ((n ^ complement) != 65535 || n > expected - out.size())
                throw std::runtime_error("Invalid stored DEFLATE block");
            auto raw = r.take(n);
            out.insert(out.end(), raw.begin(), raw.end());
            continue;
        }
        std::vector<unsigned> lit, dist;
        if (kind == 1) {
            lit.resize(288);
            for (int i = 0; i < 288; i++)
                lit[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            dist.resize(32, 5);
        } else if (kind == 2) {
            unsigned nl = bits.read(5) + 257, nd = bits.read(5) + 1, nc = bits.read(4) + 4;
            const int permutation[] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::vector<unsigned> code(19);
            for (unsigned i = 0; i < nc; i++)
                code[permutation[i]] = bits.read(3);
            Huffman h(code);
            std::vector<unsigned> lens;
            while (lens.size() < nl + nd) {
                int s = h.decode(bits);
                if (s < 16)
                    lens.push_back(s);
                else {
                    unsigned count, value = 0;
                    if (s == 16) {
                        if (lens.empty())
                            throw std::runtime_error("Invalid repeated Huffman length");
                        count = bits.read(2) + 3;
                        value = lens.back();
                    } else if (s == 17)
                        count = bits.read(3) + 3;
                    else if (s == 18)
                        count = bits.read(7) + 11;
                    else
                        throw std::runtime_error("Invalid length symbol");
                    if (count > nl + nd - lens.size())
                        throw std::runtime_error("Huffman length overflow");
                    lens.insert(lens.end(), count, value);
                }
            }
            lit.assign(lens.begin(), lens.begin() + nl);
            dist.assign(lens.begin() + nl, lens.end());
        } else
            throw std::runtime_error("Reserved DEFLATE block");
        if (lit.size() <= 256 || !lit[256])
            throw std::runtime_error("Missing DEFLATE end marker");
        Huffman lh(lit), dh(dist);
        for (;;) {
            int s = lh.decode(bits);
            if (s == 256)
                break;
            if (s < 256) {
                if (out.size() >= expected)
                    throw std::runtime_error("gzip output overflow");
                out.push_back(uint8_t(s));
                continue;
            }
            if (s < 257 || s > 285)
                throw std::runtime_error("Invalid length code");
            int k = s - 257;
            size_t len = lengthBase[k] + bits.read(lengthExtra[k]);
            int ds = dh.decode(bits);
            if (ds < 0 || ds >= 30)
                throw std::runtime_error("Invalid distance code");
            size_t distance = distanceBase[ds] + bits.read(distanceExtra[ds]);
            if (distance > out.size() || len > expected - out.size())
                throw std::runtime_error("Invalid back-reference");
            size_t start = out.size();
            out.resize(start + len);
            for (size_t i = 0; i < len; i++)
                out[start + i] = out[start + i - distance];
        }
    }
    bits.align();
    auto crc = r.le32(), size = r.le32();
    if (size != expected || out.size() != expected || crc32(out) != crc || r.p != compressed.size())
        throw std::runtime_error("gzip checksum/length mismatch");
    return out;
}
PdnSource::PdnSource(const fs::path &file) : bytes(std::make_shared<Bytes>(readBytes(file))) {
    auto &b = *bytes;
    if (b.size() < 16 || std::memcmp(b.data(), "PDN3", 4))
        throw std::runtime_error("Only PDN3 files are supported");
    size_t headerSize = b[4] | (size_t(b[5]) << 8) | (size_t(b[6]) << 16);
    if (headerSize > b.size() - 9)
        throw std::runtime_error("Truncated PDN XML header");
    std::string header(b.begin() + 7, b.begin() + 7 + headerSize);
    auto attr = [&](const std::string &key) {
        std::smatch m;
        if (!std::regex_search(header, m, std::regex(key + "=\"([^\"]+)\"")))
            throw std::runtime_error("Missing PDN attribute: " + key);
        return m[1].str();
    };
    width = std::stoi(attr("width"));
    height = std::stoi(attr("height"));
    savedWith = attr("savedWithVersion");
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || uint64_t(width) * height > 64000000)
        throw std::runtime_error("PDN image dimensions exceed limit");
    Reader r{b, 7 + headerSize};
    if (r.u8() != 0 || r.u8() != 1)
        throw std::runtime_error("Unsupported PDN stream marker");
    Nrbf n(r);
    n.record();
    while (!n.record()["end"].boolean())
        ;
    for (int id : n.memoryIds) {
        auto &info = n.objects.at(id)["fields"];
        if (info["hasParent"].boolean() || !info["deferred"].boolean())
            throw std::runtime_error("Unsupported PDN memory block");
        size_t len = size_t(info["length64"].num(info["length"].num()));
        if (len > 256 * 1024 * 1024)
            throw std::runtime_error("PDN layer exceeds memory limit");
        Block block;
        block.length = len;
        if (len) {
            block.format = r.u8();
            if (block.format > 1)
                throw std::runtime_error("Unsupported PDN chunk compression");
            size_t chunkSize = r.be32();
            if (chunkSize == 0 || chunkSize > 256 * 1024 * 1024)
                throw std::runtime_error("Invalid PDN chunk size");
            size_t count = (len + chunkSize - 1) / chunkSize;
            std::vector<bool> seen(count);
            for (size_t i = 0; i < count; i++) {
                size_t slot = r.be32(), size = r.be32();
                if (slot >= count || seen[slot])
                    throw std::runtime_error("Duplicate/out-of-range PDN chunk");
                seen[slot] = true;
                block.chunks.push_back(
                    {r.p, size, slot * chunkSize, std::min(chunkSize, len - slot * chunkSize)});
                r.skip(size);
            }
        }
        blocks.emplace(id, std::move(block));
    }
    auto &document = n.objects.at(n.root)["fields"];
    auto &layerList = n.resolve(document["layers"])["fields"];
    auto &items = n.resolve(layerList["ArrayList+_items"]);
    int count = int(layerList["ArrayList+_size"].num());
    if (count <= 0 || count > 512 || size_t(count) > items.size() || count != std::stoi(attr("layers")))
        throw std::runtime_error("PDN layer count mismatch");
    for (int i = 0; i < count; i++) {
        auto &f = n.resolve(items[size_t(i)])["fields"];
        if (f["Layer+width"].num() != width || f["Layer+height"].num() != height)
            throw std::runtime_error("Layer dimensions mismatch");
        auto &p = n.resolve(f["Layer+properties"])["fields"];
        auto &surface = n.resolve(f["surface"])["fields"];
        int mem = int(surface["scan0"]["ref"].num());
        if (blocks.at(mem).length != size_t(width) * height * 4 || surface["stride"].num() != width * 4)
            throw std::runtime_error("Unsupported PDN pixel layout");
        int blend = int(n.resolve(p["blendMode"])["fields"]["value__"].num());
        if (blend < 0 || blend > 13)
            throw std::runtime_error("Unsupported PDN blend mode: " + std::to_string(blend));
        layers.push_back(
            {n.resolve(p["name"]).str(), p["visible"].boolean(), int(p["opacity"].num(255)), blend, mem});
    }
    if (r.p != b.size())
        throw std::runtime_error("Unexpected trailing PDN pixel data");
}
std::shared_ptr<Image> PdnSource::decode(size_t index) const {
    auto &block = blocks.at(layers.at(index).memoryId);
    auto image = std::make_shared<Image>(width, height);
    for (auto &c : block.chunks) {
        Bytes raw(bytes->begin() + c.fileOffset, bytes->begin() + c.fileOffset + c.size);
        if (block.format == 0)
            raw = gunzip(raw, c.outputSize);
        if (raw.size() != c.outputSize)
            throw std::runtime_error("PDN chunk size mismatch");
        std::copy(raw.begin(), raw.end(), image->bgra.begin() + c.outputOffset);
    }
    return image;
}
Json PdnSource::metadata() const {
    Json result =
        fields({{"width", width}, {"height", height}, {"saved_with", savedWith}, {"layers", Json::array()}});
    for (size_t i = 0; i < layers.size(); i++) {
        auto &l = layers[i];
        result["layers"].push(fields({{"index", i},
                                      {"name", l.name},
                                      {"visible", l.visible},
                                      {"opacity", l.opacity},
                                      {"blend_mode", l.blend}}));
    }
    return result;
}
} // namespace atlas
