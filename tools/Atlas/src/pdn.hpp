#pragma once
#include "platform.hpp"

namespace atlas {
Bytes gunzip(const Bytes &compressed, size_t expected);
struct PdnChunk {
    size_t fileOffset = 0, size = 0, outputOffset = 0, outputSize = 0;
};
struct PdnLayer {
    std::string name;
    bool visible = true;
    int opacity = 255, blend = 0;
    int memoryId = 0;
};
class PdnSource {
    std::shared_ptr<Bytes> bytes;
    struct Block {
        size_t length = 0;
        int format = 0;
        std::vector<PdnChunk> chunks;
    };
    std::map<int, Block> blocks;

  public:
    int width = 0, height = 0;
    std::string savedWith;
    std::vector<PdnLayer> layers;
    explicit PdnSource(const fs::path &file);
    std::shared_ptr<Image> decode(size_t index) const;
    Json metadata() const;
};
} // namespace atlas
